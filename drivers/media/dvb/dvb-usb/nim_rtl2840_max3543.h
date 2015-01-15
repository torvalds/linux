#ifndef __NIM_RTL2840_MAX3543_H
#define __NIM_RTL2840_MAX3543_H

/**

@file

@brief   RTL2840 MAX3543 NIM module definition

One can manipulate RTL2840 MAX3543 NIM through RTL2840 MAX3543 NIM module.
RTL2840 MAX3543 NIM module is derived from QAM NIM module.



@par Example:
@code

// The example is the same as the NIM example in qam_nim_base.h except the listed lines.



#include "nim_rtl2840_max3543.h"


...



int main(void)
{
	QAM_NIM_MODULE *pNim;
	QAM_NIM_MODULE QamNimModuleMemory;

	...



	// Build RTL2840 MAX3543 NIM module.
	BuildRtl2840Max3543Module(
		&pNim,
		&QamNimModuleMemory,

		9,								// Maximum I2C reading byte number is 9.
		8,								// Maximum I2C writing byte number is 8.
		CustomI2cRead,					// Employ CustomI2cRead() as basic I2C reading function.
		CustomI2cWrite,					// Employ CustomI2cWrite() as basic I2C writing function.
		CustomWaitMs,					// Employ CustomWaitMs() as basic waiting function.

		0x44,							// The RTL2840 I2C device address is 0x44 in 8-bit format.
		CRYSTAL_FREQ_28800000HZ,		// The RTL2840 crystal frequency is 28.8 MHz.
		TS_INTERFACE_SERIAL,			// The RTL2840 TS interface mode is serial.
		QAM_DEMOD_EN_AM_HUM,			// Use AM-hum enhancement mode.

		0xc0,							// The MAX3543 I2C device address is 0xc0 in 8-bit format.
		CRYSTAL_FREQ_16000000HZ			// The MAX3543 Crystal frequency is 16.0 MHz.
		);



	// See the example for other NIM functions in qam_nim_base.h
	...


	return 0;
}


@endcode

*/


#include "demod_rtl2840.h"
#include "tuner_max3543.h"
#include "qam_nim_base.h"





// Definitions
#define RTL2840_MAX3543_ADDITIONAL_INIT_REG_TABLE_LEN		9

// Default
#define RTL2840_MAX3543_STANDARD_MODE_DEFAULT				MAX3543_STANDARD_QAM
#define RTL2840_MAX3543_IF_FREQ_HZ_DEFAULT					IF_FREQ_36170000HZ
#define RTL2840_MAX3543_SPECTRUM_MODE_DEFAULT				SPECTRUM_INVERSE
#define RTL2840_MAX3543_SAW_INPUT_TYPE_DEFAULT				MAX3543_SAW_INPUT_SE





// Builder
void
BuildRtl2840Max3543Module(
	QAM_NIM_MODULE **ppNim,							// QAM NIM dependence
	QAM_NIM_MODULE *pQamNimModuleMemory,

	unsigned long I2cReadingByteNumMax,				// Base interface dependence
	unsigned long I2cWritingByteNumMax,
	BASE_FP_I2C_READ I2cRead,
	BASE_FP_I2C_WRITE I2cWrite,
	BASE_FP_WAIT_MS WaitMs,

	unsigned char DemodDeviceAddr,					// Demod dependence
	unsigned long DemodCrystalFreqHz,
	int DemodTsInterfaceMode,
	int DemodEnhancementMode,

	unsigned char TunerDeviceAddr,					// Tuner dependence
	unsigned long TunerCrystalFreqHz
	);





// RTL2840 MAX3543 NIM manipulaing functions
int
rtl2840_max3543_Initialize(
	QAM_NIM_MODULE *pNim
	);

int
rtl2840_max3543_SetParameters(
	QAM_NIM_MODULE *pNim,
	unsigned long RfFreqHz,
	int QamMode,
	unsigned long SymbolRateHz,
	int AlphaMode
	);













#endif
