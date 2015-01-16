#ifndef __NIM_RTL2840_MT2063_H
#define __NIM_RTL2840_MT2063_H

/**

@file

@brief   RTL2840 MT2063 NIM module definition

One can manipulate RTL2840 MT2063 NIM through RTL2840 MT2063 NIM module.
RTL2840 MT2063 NIM module is derived from QAM NIM module.



@par Example:
@code

// The example is the same as the NIM example in qam_nim_base.h except the listed lines.



#include "nim_rtl2840_mt2063.h"


...



int main(void)
{
	QAM_NIM_MODULE *pNim;
	QAM_NIM_MODULE QamNimModuleMemory;
	TUNER_MODULE *pTuner;
	MT2063_EXTRA_MODULE *pTunerExtra;

	...



	// Build RTL2840 MT2063 NIM module.
	BuildRtl2840Mt2063Module(
		&pNim,
		&QamNimModuleMemory,
		IF_FREQ_36125000HZ,					// The RTL2840 and MT2063 IF frequency is 36.125 MHz.

		9,									// Maximum I2C reading byte number is 9.
		8,									// Maximum I2C writing byte number is 8.
		CustomI2cRead,						// Employ CustomI2cRead() as basic I2C reading function.
		CustomI2cWrite,						// Employ CustomI2cWrite() as basic I2C writing function.
		CustomWaitMs,						// Employ CustomWaitMs() as basic waiting function.

		0x44,								// The RTL2840 I2C device address is 0x44 in 8-bit format.
		CRYSTAL_FREQ_28800000HZ,			// The RTL2840 crystal frequency is 28.8 MHz.
		TS_INTERFACE_SERIAL,				// The RTL2840 TS interface mode is serial.
		QAM_DEMOD_EN_AM_HUM,				// Use AM-hum enhancement mode.

		0xc0								// The MT2063 I2C device address is 0xc0 in 8-bit format.
		);





	// Get MT2063 tuner extra module.
	pTuner = pNim->pTuner;
	pTunerExtra = &(pTuner->Extra.Mt2063);

	// Open MT2063 handle.
	pTunerExtra->OpenHandle(pTuner);





	// See the example for other NIM functions in qam_nim_base.h
	...





	// Close MT2063 handle.
	pTunerExtra->CloseHandle(pTuner);



	return 0;
}


@endcode

*/


#include "demod_rtl2840.h"
#include "tuner_mt2063.h"
#include "qam_nim_base.h"





// Definitions
#define RTL2840_MT2063_ADDITIONAL_INIT_REG_TABLE_LEN		9





// Builder
void
BuildRtl2840Mt2063Module(
	QAM_NIM_MODULE **ppNim,							// QAM NIM dependence
	QAM_NIM_MODULE *pQamNimModuleMemory,
	unsigned long NimIfFreqHz,

	unsigned long I2cReadingByteNumMax,				// Base interface dependence
	unsigned long I2cWritingByteNumMax,
	BASE_FP_I2C_READ I2cRead,
	BASE_FP_I2C_WRITE I2cWrite,
	BASE_FP_WAIT_MS WaitMs,

	unsigned char DemodDeviceAddr,					// Demod dependence
	unsigned long DemodCrystalFreqHz,
	int DemodTsInterfaceMode,
	int DemodEnhancementMode,

	unsigned char TunerDeviceAddr					// Tuner dependence
	);





// RTL2840 MT2063 NIM manipulaing functions
int
rtl2840_mt2063_Initialize(
	QAM_NIM_MODULE *pNim
	);

int
rtl2840_mt2063_SetParameters(
	QAM_NIM_MODULE *pNim,
	unsigned long RfFreqHz,
	int QamMode,
	unsigned long SymbolRateHz,
	int AlphaMode
	);













#endif
