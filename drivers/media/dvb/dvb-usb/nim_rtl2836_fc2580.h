#ifndef __NIM_RTL2836_FC2580
#define __NIM_RTL2836_FC2580

/**

@file

@brief   RTL2836 FC2580 NIM module declaration

One can manipulate RTL2836 FC2580 NIM through RTL2836 FC2580 NIM module.
RTL2836 FC2580 NIM module is derived from DTMB NIM module.



@par Example:
@code

// The example is the same as the NIM example in dtmb_nim_base.h except the listed lines.



#include "nim_rtl2836_fc2580.h"


...



int main(void)
{
	DTMB_NIM_MODULE *pNim;
	DTMB_NIM_MODULE DtmbNimModuleMemory;

	...



	// Build RTL2836 FC2580 NIM module.
	BuildRtl2836Fc2580Module(
		&pNim,
		&DtmbNimModuleMemory,

		9,								// Maximum I2C reading byte number is 9.
		8,								// Maximum I2C writing byte number is 8.
		CustomI2cRead,					// Employ CustomI2cRead() as basic I2C reading function.
		CustomI2cWrite,					// Employ CustomI2cWrite() as basic I2C writing function.
		CustomWaitMs,					// Employ CustomWaitMs() as basic waiting function.

		0x3e,							// The RTL2836 I2C device address is 0x3e in 8-bit format.
		CRYSTAL_FREQ_27000000HZ,		// The RTL2836 crystal frequency is 27.0 MHz.
		TS_INTERFACE_SERIAL,			// The RTL2836 TS interface mode is serial.
		50,								// The RTL2836 update function reference period is 50 millisecond
		YES,							// The RTL2836 Function 1 enabling status is YES.
		YES,							// The RTL2836 Function 2 enabling status is YES.

		0xac,							// The FC2580 I2C device address is 0xac in 8-bit format.
		CRYSTAL_FREQ_16384000HZ,		// The FC2580 crystal frequency is 16.384 MHz.
		FC2580_AGC_INTERNAL				// The FC2580 AGC mode is internal AGC mode.
		);



	// See the example for other NIM functions in dtmb_nim_base.h

	...


	return 0;
}


@endcode

*/


#include "demod_rtl2836.h"
#include "tuner_fc2580.h"
#include "dtmb_nim_base.h"





// Definitions
#define RTL2836_FC2580_ADDITIONAL_INIT_REG_TABLE_LEN		1





// Builder
void
BuildRtl2836Fc2580Module(
	DTMB_NIM_MODULE **ppNim,							// DTMB NIM dependence
	DTMB_NIM_MODULE *pDtmbNimModuleMemory,

	unsigned long I2cReadingByteNumMax,					// Base interface dependence
	unsigned long I2cWritingByteNumMax,
	BASE_FP_I2C_READ I2cRead,
	BASE_FP_I2C_WRITE I2cWrite,
	BASE_FP_WAIT_MS WaitMs,

	unsigned char DemodDeviceAddr,						// Demod dependence
	unsigned long DemodCrystalFreqHz,
	int DemodTsInterfaceMode,
	unsigned long DemodUpdateFuncRefPeriodMs,
	int DemodIsFunc1Enabled,
	int DemodIsFunc2Enabled,

	unsigned char TunerDeviceAddr,						// Tuner dependence
	unsigned long TunerCrystalFreqHz,
	int TunerAgcMode
	);





// RTL2836 FC2580 NIM manipulaing functions
int
rtl2836_fc2580_Initialize(
	DTMB_NIM_MODULE *pNim
	);

int
rtl2836_fc2580_SetParameters(
	DTMB_NIM_MODULE *pNim,
	unsigned long RfFreqHz
	);







#endif


