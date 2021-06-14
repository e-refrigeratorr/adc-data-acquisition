#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include "AgMD1Fundamental.h"

using namespace std;


/*
Big TODO:
- make library with functions initialization and closing;
- make library with functions configuration;
- add readable error messages;
- make a log-file for errors.
*/


ViInt32 initialize (ViSession instrumentID[10]) {
	// Shows number of instruments, it's types and initializes
	// Returns number of instruments, fills array of ID instrument's
	cout << "Initialization..." << endl;

	ViInt32 instrumentsNumber;
	ViStatus status;
	ViString options = "";
	status = Acqrs_getNbrInstruments(&instrumentsNumber);
	// TODO: add exit if number of instruments is 0
	cout << "The number of available instruments is " << instrumentsNumber << endl;

	// Initialize the instruments
	for (ViInt32 devIndex = 0; devIndex < instrumentsNumber; devIndex++) {
		ViInt32 devTypeP;
		ViStatus status = Acqrs_getDevTypeByIndex(devIndex, &devTypeP); // get type of instrument

		if (devTypeP == 1) {
			cout << "The number " << devIndex << " is Digitizer" << endl;
		} else if (devTypeP == 2) {
			cout << "The number " << devIndex << " is RC2xx Generator" << endl;
		} else if (devTypeP == 3) {
			cout << "The number " << devIndex << " is TC Time-to-Digital Converter" << endl;
		}

		// ViRsrc resourceName = "PCI::INSTR0"; // TODO: try to fix it later
		char resourceName[20] = "";
		sprintf_s(resourceName, "PCI::INSTR%d", devIndex);
		status = Acqrs_InitWithOptions(resourceName, VI_FALSE, VI_FALSE, options, &(instrumentID[devIndex]));

		if (status == 0) {
			cout << "Instrument number " << devIndex << " successfully initialized!" << endl;
			cout << "ID: " << instrumentID[devIndex] << endl;
		} else {
			cout << "Something go wrong... Error code: " << status << endl;
		}
	}

	return instrumentsNumber;
}


long configure (ViInt32 nbrInstruments, ViSession instrumentID[10], long nbrSamples, long nbrSegments) {
	// Reads file with parameters and configures equipment
	// Returns number of segments and samples from user's input
	// Configuration values:
	ifstream configFile("acquire.config");
	string configValue;
	long counter = 1;
	double parameters[10];

	if (configFile.is_open()) {
		cout << "Configuration file opened successfull!" << endl;
		while (getline(configFile, configValue)) {
			if (counter % 2 == 0) {
				parameters[counter / 2 - 1] = stod(configValue);
			}

			counter++;
		}

		configFile.close();
	} else {
		cout << "Problems with configuration file..." << endl;
	}

	// TODO: comment variables with documentation
	double sampInterval = parameters[0] * 1e-9; // Convert to seconds
	double delayTime = parameters[1] * 1e-9;; // Convert to seconds
	long channel = (long) parameters[2];
	long coupling = (long) parameters[3];
	long bandwidth = (long) parameters[4];
	double fullScale = parameters[5] / 1000; // Convert to Volts
	double offset = parameters[6];
	long trigCoupling = (long) parameters[7];
	long trigSlope = (long) parameters[8];
	double trigLevel = parameters[9] / parameters[5] * 100; // Convert to percents of vertical Full Scale
	
	// TODO: cover this functions in try-catch
	for (long i = 0; i < nbrInstruments; i++) {
		AcqrsD1_configHorizontal(instrumentID[i], sampInterval, delayTime);
		AcqrsD1_configMemory(instrumentID[i], nbrSamples, nbrSegments);
		AcqrsD1_configVertical(instrumentID[i], channel, fullScale, offset, coupling, bandwidth);
		AcqrsD1_configTrigClass(instrumentID[i], 0, 0x00000001, 0, 0, 0.0, 0.0);
		AcqrsD1_configTrigSource(instrumentID[i], channel, trigCoupling, trigSlope, trigLevel, 0.0);
	}

	cout << "Equipment configurated successfully!" << endl;

	return channel;
}


int main () {
	ViStatus status;

	// Initialization
	ViSession instrumentID[10];
	ViInt32 nbrInstruments;
	nbrInstruments = initialize(instrumentID);
	

	// Configuration
	long nbrSamples, nbrSegments;
	cout << "Enter number of samples: ";
	cin >> nbrSamples;
	cout << "Enter number of segments: ";
	cin >> nbrSegments;
	long channel = configure(nbrInstruments, instrumentID[10], nbrSamples, nbrSegments);


	// Let's get data
	long timeOut = 2000;

	for (long i = 0; i < nbrInstruments; i++) {
		AcqrsD1_acquire(instrumentID[i]);
		status = AcqrsD1_waitForEndOfAcquisition(instrumentID[i], timeOut);
		cout << "Acquisition status is: " << status << endl;
	}

	
	// Definition of the read parameters for raw ADC readout
	long currentSegmentPad, nbrSamplesNom, nbrSegmentsNom;
	AqReadParameters readPar;
	readPar.dataType = ReadInt8; // 8bit, raw ADC values data type
	readPar.readMode = 1; // Multi-segment read mode
	readPar.firstSegment = 0;
	readPar.nbrSegments = nbrSegments;
	readPar.firstSampleInSeg = 0;
	readPar.nbrSamplesInSeg = nbrSamples;
	readPar.segmentOffset = nbrSamples;
	readPar.segDescArraySize = sizeof(AqSegmentDescriptor) * nbrSegments;

	readPar.flags = 0;
	readPar.reserved = 0;
	readPar.reserved2 = 0;
	readPar.reserved3 = 0;
	
	
	// Let's readout data
	for (long i = 0; i < nbrInstruments; i++) {
		Acqrs_getInstrumentInfo(instrumentID[i], "TbSegmentPad", &currentSegmentPad);
		status = AcqrsD1_getMemory(instrumentID[i], &nbrSamplesNom, &nbrSegmentsNom);
		cout << "Getting memory status is: " << status << endl;
		readPar.dataArraySize = (nbrSamplesNom + currentSegmentPad) * (nbrSegments + 1); // Array size in bytes
	}

	// Read the channel 1 waveform as raw ADC values
	AqDataDescriptor dataDesc;
	AqSegmentDescriptor *segDesc = new AqSegmentDescriptor[nbrSegments];
	ViInt8 * adcArrayP = new ViInt8[readPar.dataArraySize];

	for (long i = 0; i < nbrInstruments; i++) {
		AcqrsD1_readData(instrumentID[i], channel, &readPar, adcArrayP, &dataDesc, segDesc);
		cout << "Reading data status is " << status << endl;
	}

	
	// Write the waveform into a file
	// TODO: add date-mark in name of file: "acqiris-data-xx-xx-xxxx.data"
	ofstream outFile("Acqiris.data");
	outFile << "# Agilent Acqiris Waveform Channel 1" << endl;
	outFile << "# Samples acquired: " << dataDesc.returnedSamplesPerSeg * dataDesc.returnedSegments << endl;
	outFile << "# Voltage" << endl;

	for (ViInt32 i = firstPoint; i < firstPoint + dataDesc.returnedSamplesPerSeg; i++) {
		outFile << int(adcArrayP[i]) * dataDesc.vGain - dataDesc.vOffset << endl; // Volts
	}

	outFile.close();
	delete[] adcArrayP;


	// TODO: make a special function for this shit
	// Stop the instruments
	Acqrs_closeAll(); // TODO: try to make for-cycle to stop each instrument independently
	cout << "All instruments successfully stopped." << endl;

	return 0;
}
