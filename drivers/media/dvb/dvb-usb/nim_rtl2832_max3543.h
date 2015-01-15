#ifndef __NIM_RTL2832_MAX3543
#define __NIM_RTL2832_MAX3543

/**

@file

@brief   RTL2832 MAX3543 NIM module declaration

One can manipulate RTL2832 MAX3543 NIM through RTL2832 MAX3543 NIM module.
RTL2832 MAX3543 NIM module is derived from DVB-T NIM module.



@par Example:
@code

// The example is the same as the NIM example in dvbt_nim_base.h except the listed lines.



#include "nim_rtl2832_max3543.h"


...



int main(void)
{
	DVBT_NIM_MODULE *pNim;
	DVBT_NIM_MODULE DvbtNimModuleMemory;

	...



	// Build RTL2832 MAX3543 NIM module.
	BuildRtl2832Max3543Module(
		&pNim,
		&DvbtNimModuleMemory,

		9,								// Maximum I2C reading byte number is 9.
		8,								// Maximum I2C writing byte number is 8.
		CustomI2cRead,					// Employ CustomI2cRead() as basic I2C reading function.
		CustomI2cWrite,					// Employ CustomI2cWrite() as basic I2C writing function.
		CustomWaitMs,					// Employ CustomWaitMs() as basic waiting function.

		0x20,							// The RTL2832 I2C device address is 0x20 in 8-bit format.
		CRYSTAL_FREQ_28800000HZ,		// The RTL2832 crystal frequency is 28.8 MHz.
		TS_INTERFACE_SERIAL,			// The RTL2832 TS interface mode is serial.
		RTL2832_APPLICATION_STB,		// The RTL2832 application mode is STB mode.
		200,							// The RTL2832 update function reference period is 200 millisecond
		YES,							// The RTL2832 Function 1 enabling status is YES.

		0xc0,							// The MAX3543 I2C device address is 0xc0 in 8-bit format.
		CRYSTAL_FREQ_16000000HZ			// The MAX3543 Crystal frequency is 16.0 MHz.
		);



	// See the example for other NIM functions in dvbt_nim_base.h

	...


	return 0;
}


@endcode

*/


#include "demod_rtl2832.h"
#include "tuner_max3543.h"
#include "dvbt_nim_base.h"





// Definitions
#define RTL2832_MAX3543_ADDITIONAL_INIT_REG_TABLE_LEN		23

// Default
#define RTL2832_MAX3543_STANDARD_MODE_DEFAULT				MAX3543_STANDARD_DVBT
#define RTL2832_MAX3543_IF_FREQ_HZ_DEFAULT					IF_FREQ_36170000HZ
#define RTL2832_MAX3543_SPECTRUM_MODE_DEFAULT				SPECTRUM_INVERSE
#define RTL2832_MAX3543_SAW_INPUT_TYPE_DEFAULT				MAX3543_SAW_INPUT_SE





// Builder
void
BuildRtl2832Max3543Module(
	DVBT_NIM_MODULE **ppNim,							// DVB-T NIM dependence
	DVBT_NIM_MODULE *pDvbtNimModuleMemory,

	unsigned long I2cReadingByteNumMax,					// Base interface dependence
	unsigned long I2cWritingByteNumMax,
	BASE_FP_I2C_READ I2cRead,
	BASE_FP_I2C_WRITE I2cWrite,
	BASE_FP_WAIT_MS WaitMs,

	unsigned char DemodDeviceAddr,						// Demod dependence
	unsigned long DemodCrystalFreqHz,
	int DemodTsInterfaceMode,
	int DemodAppMode,
	unsigned long DemodUpdateFuncRefPeriodMs,
	int DemodIsFunc1Enabled,

	unsigned char TunerDeviceAddr,						// Tuner dependence
	unsigned long TunerCrystalFreqHz
	);





// RTL2832 MAX3543 NIM manipulaing functions
int
rtl2832_max3543_Initialize(
	DVBT_NIM_MODULE *pNim
	);

int
rtl2832_max3543_SetParameters(
	DVBT_NIM_MODULE *pNim,
	unsigned long RfFreqHz,
	int BandwidthMode
	);







#endif


