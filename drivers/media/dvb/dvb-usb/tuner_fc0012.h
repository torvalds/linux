#ifndef __TUNER_FC0012_H
#define __TUNER_FC0012_H

/**

@file

@brief   FC0012 tuner module declaration

One can manipulate FC0012 tuner through FC0012 module.
FC0012 module is derived from tuner module.



@par Example:
@code

// The example is the same as the tuner example in tuner_base.h except the listed lines.



#include "tuner_fc0012.h"


...



int main(void)
{
	TUNER_MODULE        *pTuner;
	FC0012_EXTRA_MODULE *pTunerExtra;

	TUNER_MODULE          TunerModuleMemory;
	BASE_INTERFACE_MODULE BaseInterfaceModuleMemory;
	I2C_BRIDGE_MODULE     I2cBridgeModuleMemory;

	unsigned long BandwidthMode;


	...



	// Build FC0012 tuner module.
	BuildFc0012Module(
		&pTuner,
		&TunerModuleMemory,
		&BaseInterfaceModuleMemory,
		&I2cBridgeModuleMemory,
		0xc6,								// I2C device address is 0xc6 in 8-bit format.
		CRYSTAL_FREQ_36000000HZ				// Crystal frequency is 36.0 MHz.
		);





	// Get FC0012 tuner extra module.
	pTunerExtra = &(pTuner->Extra.Fc0012);





	// ==== Initialize tuner and set its parameters =====

	...

	// Set FC0012 bandwidth.
	pTunerExtra->SetBandwidthMode(pTuner, FC0012_BANDWIDTH_6000000HZ);





	// ==== Get tuner information =====

	...

	// Get FC0012 bandwidth.
	pTunerExtra->GetBandwidthMode(pTuner, &BandwidthMode);



	// See the example for other tuner functions in tuner_base.h


	return 0;
}


@endcode

*/





#include "tuner_base.h"





// The following context is implemented for FC0012 source code.


// Definitions
enum FC0012_TRUE_FALSE_STATUS
{
	FC0012_FALSE,
	FC0012_TRUE,
};


enum FC0012_I2C_STATUS
{
	FC0012_I2C_SUCCESS,
	FC0012_I2C_ERROR,
};


enum FC0012_FUNCTION_STATUS
{
	FC0012_FUNCTION_SUCCESS,
	FC0012_FUNCTION_ERROR,
};



// Functions
int FC0012_Read(TUNER_MODULE *pTuner, unsigned char RegAddr, unsigned char *pByte);
int FC0012_Write(TUNER_MODULE *pTuner, unsigned char RegAddr, unsigned char Byte);

int
fc0012_SetRegMaskBits(
	TUNER_MODULE *pTuner,
	unsigned char RegAddr,
	unsigned char Msb,
	unsigned char Lsb,
	const unsigned char WritingValue
	);

int
fc0012_GetRegMaskBits(
	TUNER_MODULE *pTuner,
	unsigned char RegAddr,
	unsigned char Msb,
	unsigned char Lsb,
	unsigned char *pReadingValue
	);

int FC0012_Open(TUNER_MODULE *pTuner);
int FC0011_SetFrequency(TUNER_MODULE *pTuner, unsigned long Frequency, unsigned short Bandwidth);



























// The following context is FC0012 tuner API source code





// Definitions

// Bandwidth mode
enum FC0012_BANDWIDTH_MODE
{
	FC0012_BANDWIDTH_6000000HZ = 6,
	FC0012_BANDWIDTH_7000000HZ = 7,
	FC0012_BANDWIDTH_8000000HZ = 8,
};


// Default for initialing
#define FC0012_RF_FREQ_HZ_DEFAULT			50000000
#define FC0012_BANDWIDTH_MODE_DEFAULT		FC0012_BANDWIDTH_6000000HZ


// Tuner LNA
enum FC0012_LNA_GAIN_VALUE
{
	FC0012_LNA_GAIN_LOW    = 0x0,
	FC0012_LNA_GAIN_MIDDLE = 0x1,
	FC0012_LNA_GAIN_HIGH   = 0x2,
};





// Builder
void
BuildFc0012Module(
	TUNER_MODULE **ppTuner,
	TUNER_MODULE *pTunerModuleMemory,
	BASE_INTERFACE_MODULE *pBaseInterfaceModuleMemory,
	I2C_BRIDGE_MODULE *pI2cBridgeModuleMemory,
	unsigned char DeviceAddr,
	unsigned long CrystalFreqHz
	);





// Manipulaing functions
void
fc0012_GetTunerType(
	TUNER_MODULE *pTuner,
	int *pTunerType
	);

void
fc0012_GetDeviceAddr(
	TUNER_MODULE *pTuner,
	unsigned char *pDeviceAddr
	);

int
fc0012_Initialize(
	TUNER_MODULE *pTuner
	);

int
fc0012_SetRfFreqHz(
	TUNER_MODULE *pTuner,
	unsigned long RfFreqHz
	);

int
fc0012_GetRfFreqHz(
	TUNER_MODULE *pTuner,
	unsigned long *pRfFreqHz
	);





// Extra manipulaing functions
int
fc0012_SetBandwidthMode(
	TUNER_MODULE *pTuner,
	int BandwidthMode
	);

int
fc0012_GetBandwidthMode(
	TUNER_MODULE *pTuner,
	int *pBandwidthMode
	);













#endif
