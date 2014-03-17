// Copyright 2012 Evrytania LLC (http://www.evrytania.com)
//
// Written by James Peroulas <james@evrytania.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <itpp/itbase.h>
#include <iomanip>
#include <sstream>
#include <queue>
#include <curses.h>
#include <boost/math/special_functions/gamma.hpp>
#include "rtl-sdr.h"
#include "common.h"
#include "capbuf.h"
#include "macros.h"
#include "itpp_ext.h"
#include "dsp.h"

using namespace itpp;
using namespace std;

// Number of complex samples to capture.
#define CAPLENGTH 153600

typedef struct {
  vector <unsigned char> * buf;
  rtlsdr_dev_t * dev;
} callback_package_t;
static void capbuf_rtlsdr_callback(
  unsigned char * buf,
  uint32_t len,
  void * ctx
) {
  //vector <char> & capbuf_raw = *((vector <char> *)ctx);
  //callback_package_t * cp=(callback_package_t *)ctx;
  callback_package_t * cp_p=(callback_package_t *)ctx;
  callback_package_t & cp=*cp_p;
  vector <unsigned char> * capbuf_raw_p=cp.buf;
  vector <unsigned char> & capbuf_raw=*capbuf_raw_p;
  rtlsdr_dev_t * dev=cp.dev;

  if (len==0) {
    cerr << "Error: received no samples from USB device..." << endl;
    ABORT(-1);
  }

  for (uint32 t=0;t<len;t++) {
    //cout << capbuf_raw.size() << endl;
    if (capbuf_raw.size()<CAPLENGTH*2) {
      capbuf_raw.push_back(buf[t]);
    }
    if (capbuf_raw.size()==CAPLENGTH*2) {
      //cout << rtlsdr_cancel_async(dev) << endl;
      rtlsdr_cancel_async(dev);
      break;
    }
  }
  //cout << capbuf_raw.size() << endl;
}

// Declared in from_osmocom.cpp
double compute_fc_programmed(const double & fosc,const double & intended_flo);

double calculate_fc_programmed_in_context(
  // Inputs
  const double & fc_requested,
  const bool & use_recorded_data,
  const char * load_bin_filename,
  rtlsdr_dev_t * & dev
) {
  double fc_programmed;
  bool load_bin_flag = (strlen(load_bin_filename)>4);
  if (use_recorded_data) {
    fc_programmed=fc_requested; // be careful about this!
  }
  else if (load_bin_flag) {
    fc_programmed=fc_requested; // be careful about this!
  } else {
    if (rtlsdr_get_tuner_type(dev)==RTLSDR_TUNER_E4000) {
      // This does not return the true center frequency, only the requested
      // center frequency.
      //fc_programmed=(double)rtlsdr_get_center_freq(dev);
      // Directly call some rtlsdr frequency calculation routines.
      fc_programmed=compute_fc_programmed(28.8e6,fc_requested);
      // For some reason, this will tame the slow time offset drift.
      // I don't know if this is a problem caused by the hardware or a problem
      // with the tracking algorithm.
      fc_programmed=fc_programmed+58;
      //MARK;
      //fc_programmed=fc_requested;
    } else {
      // Unsupported tuner...
      fc_programmed=fc_requested;
    }
  }
  return(fc_programmed);
}
// This function produces a vector of captured data. The data can either
// come from live data received by the RTLSDR, or from a file containing
// previously captured data.
// Also, optionally, this function can save each set of captured data
// to a file.
int capture_data(
  // Inputs
  const double & fc_requested,
  const double & correction,
  const bool & save_cap,
  const char * record_bin_filename,
  const bool & use_recorded_data,
  const char * load_bin_filename,
  const string & data_dir,
  rtlsdr_dev_t * & dev,
  // Output
  cvec & capbuf,
  double & fc_programmed,
  const bool & read_all_in_bin // only for .bin file! if it is true, all data in bin file will be read in one time.
) {
  // Filename used for recording or loading captured data.
  static uint32 capture_number=0;
  stringstream filename;
  filename << data_dir << "/capbuf_" << setw(4) << setfill('0') << capture_number << ".it";

//  cout << use_recorded_data << "\n";
//  cout << load_bin_filename << "\n";

  bool record_bin_flag = (strlen(record_bin_filename)>4);
  bool load_bin_flag = (strlen(load_bin_filename)>4);

//  cout << load_bin_flag << "\n";

  int run_out_of_data = 0;

  if (use_recorded_data) {
    // Read data from a file. Do not use live data.
    if (verbosity>=2) {
      cout << "Reading captured data from file: " << filename.str() << endl;
    }

    it_ifile itf(filename.str());
    itf.seek("capbuf");
    itf>>capbuf;
    itf.seek("fc");
    ivec fc_v;
    itf>>fc_v;
    if (fc_requested!=fc_v(0)) {
      cout << "Warning: while reading capture buffer " << capture_number << ", the read" << endl;
      cout << "center frequency did not match the expected center frequency." << endl;
    }
    itf.close();
    fc_programmed=fc_requested; // be careful about this!
  } else if (load_bin_flag) {
    // Read data from load_bin_filename. Do not use live data.
    // Convert to complex
    unsigned char *capbuf_raw = new unsigned char[2*CAPLENGTH];
    if (read_all_in_bin) {
      capbuf.set_size(0);

      FILE *fp = fopen(load_bin_filename, "rb");
      if (fp == NULL)
      {
        cerr << "Error: unable to open file: " << load_bin_filename << endl;
        ABORT(-1);
      }

      uint32 len_capbuf = 0;
      while(true) { // read until run out of data
        int read_count = fread(capbuf_raw, sizeof(unsigned char), 2*CAPLENGTH, fp);
        len_capbuf = len_capbuf + CAPLENGTH;
        capbuf.set_size(len_capbuf, true);
        for (uint32 t=0;t<CAPLENGTH;t++) {
          uint32 i = len_capbuf-CAPLENGTH+t;
          capbuf(i)=complex<double>((((double)capbuf_raw[(t<<1)])-128.0)/128.0,(((double)capbuf_raw[(t<<1)+1])-128.0)/128.0);
        }
        if (read_count != (2*CAPLENGTH))
        {
//          cerr << "Run of recorded file data.\n";
          break;
//          ABORT(-1);
        }
      }

      fclose(fp);
    } else {
      capbuf.set_size(CAPLENGTH);
      unsigned char *capbuf_raw = new unsigned char[2*CAPLENGTH];
      FILE *fp = fopen(load_bin_filename, "rb");
      if (fp == NULL)
      {
        cerr << "Error: unable to open file: " << load_bin_filename << endl;
        ABORT(-1);
      }
      for(uint16 i=0; i<(capture_number+1); i++) {
        int read_count = fread(capbuf_raw, sizeof(unsigned char), 2*CAPLENGTH, fp);
        if (read_count != (2*CAPLENGTH))
        {
//          fclose(fp);
//          cerr << "Error: file " << load_bin_filename << " size is not sufficient" << endl;
          cerr << "Run of recorded file data.\n";
          run_out_of_data = 1;
          break;
//          ABORT(-1);
        }
      }
      fclose(fp);
      for (uint32 t=0;t<CAPLENGTH;t++) {
        capbuf(t)=complex<double>((((double)capbuf_raw[(t<<1)])-128.0)/128.0,(((double)capbuf_raw[(t<<1)+1])-128.0)/128.0);
        // 127 --> 128.
      }
    }

    delete[] capbuf_raw;

    fc_programmed=fc_requested; // be careful about this!
    //cout << capbuf(0).real() << " " << capbuf(0).imag() << " "<< capbuf(1).real() << " " << capbuf(1).imag() << " "<< capbuf(2).real() << " " << capbuf(2).imag() << " "<< capbuf(3).real() << " " << capbuf(3).imag() << " "<< capbuf(4).real() << " " << capbuf(4).imag() << " "<< capbuf(5).real() << " " << capbuf(5).imag() << " "<< capbuf(6).real() << " " << capbuf(6).imag() << " "<<"\n";
  } else {
    if (verbosity>=2) {
      cout << "Capturing live data" << endl;
    }

    // Center frequency
    uint8 n_fail=0;
    while (rtlsdr_set_center_freq(dev,itpp::round(fc_requested*correction))<0) {
      n_fail++;
      if (n_fail>=5) {
        cerr << "Error: unable to set center frequency" << endl;
        ABORT(-1);
      }
      cerr << "Unable to set center frequency... retrying..." << endl;
      sleep(1);
    }

    // Calculate the actual center frequency that was programmed.
    if (rtlsdr_get_tuner_type(dev)==RTLSDR_TUNER_E4000) {
      // This does not return the true center frequency, only the requested
      // center frequency.
      //fc_programmed=(double)rtlsdr_get_center_freq(dev);
      // Directly call some rtlsdr frequency calculation routines.
      fc_programmed=compute_fc_programmed(28.8e6,fc_requested);
      // For some reason, this will tame the slow time offset drift.
      // I don't know if this is a problem caused by the hardware or a problem
      // with the tracking algorithm.
      fc_programmed=fc_programmed+58;
      //MARK;
      //fc_programmed=fc_requested;
    } else {
      // Unsupported tuner...
      fc_programmed=fc_requested;
    }

    // Reset the buffer
    if (rtlsdr_reset_buffer(dev)<0) {
      cerr << "Error: unable to reset RTLSDR buffer" << endl;
      ABORT(-1);
    }

    // Read and store the data.
    // This will block until the call to rtlsdr_cancel_async().
    vector <unsigned char> capbuf_raw;
    capbuf_raw.reserve(CAPLENGTH*2);
    callback_package_t cp;
    cp.buf=&capbuf_raw;
    cp.dev=dev;

    rtlsdr_read_async(dev,capbuf_rtlsdr_callback,(void *)&cp,0,0);

    // Convert to complex
    capbuf.set_size(CAPLENGTH);
#ifndef NDEBUG
    capbuf=NAN;
#endif
    for (uint32 t=0;t<CAPLENGTH;t++) {
      // Normal
      capbuf(t)=complex<double>((((double)capbuf_raw[(t<<1)])-128.0)/128.0,(((double)capbuf_raw[(t<<1)+1])-128.0)/128.0);
      //// 127 --> 128.
      // Conjugate
      //capbuf(t)=complex<double>((capbuf_raw[(t<<1)]-127.0)/128.0,-(capbuf_raw[(t<<1)+1]-127.0)/128.0);
      // Swap I/Q
      //capbuf(t)=complex<double>((capbuf_raw[(t<<1)+1]-127.0)/128.0,(capbuf_raw[(t<<1)]-127.0)/128.0);
      // Swap I/Q and conjugate
      //capbuf(t)=complex<double>((capbuf_raw[(t<<1)+1]-127.0)/128.0,-(capbuf_raw[(t<<1)]-127.0)/128.0);
    }
    //cout << "capbuf power: " << db10(sigpower(capbuf)) << " dB" << endl;

  }

  // Save the capture data, if requested.
  if (save_cap) {
    if (verbosity>=2) {
      cout << "Saving captured data to file: " << filename.str() << endl;
    }
    it_file itf(filename.str(),true);
    itf << Name("capbuf") << capbuf;
    ivec fc_v(1);
    fc_v(0)=fc_requested;
    itf << Name("fc") << fc_v;
    itf.close();
  }

  if (record_bin_flag) {
    if (verbosity>=2) {
      cout << "Saving captured data to file: " << record_bin_filename << endl;
    }
    FILE *fp = NULL;
    if (capture_number==0){
      fp = fopen(record_bin_filename, "wb");
    } else {
      fp = fopen(record_bin_filename, "ab");
    }

    if (fp == NULL)
    {
      cerr << "Error: unable to open file: " << record_bin_filename << endl;
      ABORT(-1);
    }
    for (uint32 t=0;t<CAPLENGTH;t++) {
      unsigned char tmp;
      tmp = (unsigned char)( capbuf(t).real()*128.0 + 128.0 );
      fwrite(&tmp, sizeof(unsigned char), 1, fp);
      tmp = (unsigned char)( capbuf(t).imag()*128.0 + 128.0 );
      fwrite(&tmp, sizeof(unsigned char), 1, fp);
    }
    fclose(fp);
  }

  capture_number++;
  return(run_out_of_data);
}

