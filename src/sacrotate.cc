//
// Copyright (C) David Brodrick
// Copyright (C) CSIRO Australia Telescope National Facility
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.
//
// $Id:  $

#include <PlotArea.h>
#include <RFI.h>
#include <TCPstream.h>
#include <Site.h>
#include <Source.h>
#include <IntegPeriod.h>
#include <TimeCoord.h>
#include <iostream>
#include <stdlib.h>
#include <sstream>
#include <iomanip>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <assert.h>

extern "C" {
#include <cpgplot.h>
    int main(int argc, char *argv[]);
}

using namespace::std;

//Different ways we can output the graph.
typedef enum displayT {
  screen,
  postscript,
  png,
  dump
} displayT;

displayT _display = screen; //Output device to display data to
char *_savefile = NULL; //File to write any output graphs to (if not screen)
IntegPeriod *_data = NULL;
int _numData = 0; //Number of data points
Site *_site = NULL; //Info about receiving station
Source *_refSource = NULL;

//Print a helpful message and exit
void usage();
//Write the given data to a file in native binary format
void writeData(IntegPeriod *data, int datlen, char *fname);
//Write the given data to a file in ASCII text format
void writeASCII(IntegPeriod *data, int datlen, char *fname);

/////////////////////////////////////////////////////////////////
int main (int argc, char *argv[])
{
  if (argc<10) {
    usage();
    exit(1);
  }

  //Build a string for the reference position and make up the flux, since
  //in this program, we are only interested in the phase
  string arg1;
  arg1 += argv[1];
  arg1 += " ";
  arg1 += argv[2];
  arg1 += " ";
  arg1 += "1.0";
  _refSource = AstroPointSource::parseSource(arg1);
  if (_refSource==NULL) {
    exit(1);
  }

  //First is meant to come the file name
  bool badload = !IntegPeriod::load(_data, _numData, argv[3]);
  if (badload || _data==NULL || _numData==0) {
    usage();
    cerr << "I just tried to load \"" << argv[3] << "\" and had no luck.\n";
    exit(1);
  }
  cout << "Loaded " << _numData << " from " << argv[3] << endl;

  //Build a string out of the next 5 arguments
  string arg;
  for (int j=3+1; j<3+7; j++) {
    arg += argv[j];
    arg += " ";
  }
  _site = Site::parseSite(arg);
  if (_site==NULL) {
    exit(1);
  }

  ostringstream out;
  switch (_display) {
  case screen:
    out << "/XS";
    break;
  case postscript:
    out << _savefile << "/CPS";
    break;
  case png:
    out << _savefile << "/PNG";
    break;
  case dump:
    out << _savefile << "/VWD";
    break;
  default:
    assert(false);
    break;
  }

  if (cpgbeg(0, out.str().c_str(), 1, 3) != 1)
    exit(1);
  cpgsvp(0.05,0.95,0.1,0.9);
  PlotArea::setPopulation(3);
  cpgsch(1.2);

  float temp[_numData];
  float timedata[_numData];
  for (int i=0; i<_numData;i++) {
    temp[i] = _data[i].phase;
    timedata[i] = _data[i].timeStamp/float(3600000000.0);
  }
  PlotArea *x = PlotArea::getPlotArea(0);
  x->setTitle("Observed Phases");
  x->setAxisY("Phase", PI, -PI, false);
  x->setAxisX("Time (Hours)", timedata[_numData-1], timedata[0], false);
  x->plotPoints(_numData, timedata, temp, 4);

  float phasebuf[_numData];
  for (int i=0; i<_numData; i++) {
    //Get the sidereal time for this data point
    timegen_t lst = _data[i].timeStamp;
    //Calculate the Az/El position of the source at that time
    pair_t azel = _refSource->getAzEl(lst, _site->getSite());
    //Get the phase response for the given baseline, frequency, etc.
    phasebuf[i] = _site->getPhaseResponse(azel);
    //Subtract reference phase from the observed interferometer phase
    _data[i].phase -= phasebuf[i];
    //Get it as a phase between PI and -PI
    _data[i].phase = _data[i].phase>=PI?(-2*PI+_data[i].phase):_data[i].phase;
    _data[i].phase = _data[i].phase<=-PI?(2*PI+_data[i].phase):_data[i].phase;
  }

  //Write the data output files
  writeData(_data, _numData, "rotate.out");
  writeASCII(_data, _numData, "rotate.txt");

  x = PlotArea::getPlotArea(1);
  x->setTitle("Modelled Phases");
  x->setAxisY("Phase", PI, -PI, false);
  x->setAxisX("Time (Hours)", timedata[_numData-1], timedata[0], false);
  x->plotPoints(_numData, timedata, phasebuf, 10);

  for (int i=0; i<_numData;i++) {
    temp[i] = _data[i].phase;
  }
  x = PlotArea::getPlotArea(2);
  x->setTitle("Result");
  x->setAxisY("Phase", PI, -PI, false);
  x->setAxisX("Time (Hours)", timedata[_numData-1], timedata[0], false);
  x->plotPoints(_numData, timedata, temp, 10);

  //Close the pgplot device
  cpgclos();

  return 0;
}


//////////////////////////////////////////////////////////////////
//Print a much needed error message and exit
void usage()
{
  cerr << endl;
  cerr << "This program takes complex data generated by \"saciq\" and performs\n";
  cerr << "fringe stopping by calculating the expected phases for a nominated\n";
  cerr << "phase reference position, for the specific instrument, and subtracting\n";
  cerr << "these phases from the actual observed data.\n\n";
  cerr << "sacrotate <RA> <Dec> <File> <Long> <Lat> <BlnEW> <BlnNS> <Freq> <phi>\n";
  cerr << "<RA>\tRight ascension of the phase reference position\n";
  cerr << "<Dec>\tDeclination of the phase reference position\n";
  cerr << "<File>\tData file name, containing complex data from saciq\n";
  cerr << "<Long>\tlongitude, in degrees, East is +ve, West is -ve\n";
  cerr << "<Lat>\tlatitude, in degrees, North is +ve, South is -ve\n";
  cerr << "<BlnEW>\tbaseline, East-West component, in metres\n";
  cerr << "<BlnNS>\tbaseline, North-South component, in metres\n";
  cerr << "<Freq>\tFrequency, in MHz\n";
  cerr << "<phi>\tphase offset, in degrees, -180 to 180, set to 0.0 if unsure\n";
}


////////////////////////////////////////////////////////////////////////
//Write the given data to a file in native binary format
void writeData(IntegPeriod *data, int datlen, char *fname) {
  ofstream datfile(fname, ios::out|ios::binary);
  for (int i=0; i<datlen; i++) datfile << data[i];
}


////////////////////////////////////////////////////////////////////////
//Write the given data to a file in ASCII format
void writeASCII(IntegPeriod *data, int datlen, char *fname) {
  ofstream txtfile(fname, ios::out);
  for (int i=0; i<datlen; i++) {
    txtfile << (data[i].timeStamp/1000000.0) << " " <<
      data[i].amplitude << " " << (data[i].phase*360)/(2*PI) << "\n";
  }
}


