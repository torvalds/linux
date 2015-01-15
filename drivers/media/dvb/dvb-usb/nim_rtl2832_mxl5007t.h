#ifndef __NIM_RTL2832_MXL5007T
#define __NIM_RTL2832_MXL5007T

/**

@file

@brief   RTL2832 MxL5007T NIM module declaration

One can manipulate RTL2832 MxL5007T NIM through RTL2832 MxL5007T NIM module.
RTL2832 MxL5007T NIM module is derived from DVB-T NIM module.



@par Example:
@code

// The example is the same as the NIM example in dvbt_nim_base.h except the listed lines.



#include "nim_rtl2832_mxl5007t.h"


...



int main(void)
{
	DVBT_NIM_MODULE *pNim;
	DVBT_NIM_MODULE DvbtNimModuleMemory;
	RTL2832_EXTRA_MODULE Rtl2832ExtraModuleMemory;
	MXL5007T_EXTRA_MODULE Mxl5007tExtraModuleMemory;

	...



	// Build RTL2832 MxL5007T NIM module.
	BuildRtl2832Mxl5007tModule(
		&pNim,
		&DvbtNimModuleMemory,

		9,								// Maximum I2C reading byte number is 9.
		8,								// Maximum I2C writing byte number is 8.
		CustomI2cRead,					// Employ CustomI2cRead() as basic I2C reading function.
		CustomI2cWrite,					// Employ CustomI2cWrite() as basic I2C writing function.
		CustomWaitMs,					// Employ CustomWaitMs() as basic waiting function.

		&Rtl2832ExtraModuleMemory,		// Employ RTL2832 extra module for RTL2832 module.
		0x20,							// The RTL2832 I2C device address is 0x20 in 8-bit format.
		CRYSTAL_FREQ_28800000HZ,		// The RTL2832 crystal frequency is 28.8 MHz.
		RTL2832_APPLICATION_STB,		// The RTL2832 application mode is STB mode.
		TS_INTERFACE_SERIAL,			// The RTL2832 TS interface mode is serial.
		50,								// The RTL2832 update function reference period is 50 millisecond
		ON,								// The RTL2832 Function 1 enabling status is on.

		&Mxl5007tExtraModuleMemory,		// Employ Mxl5007T extra module for Mxl5007T module.
		0xc0,							// The MxL5007T I2C device address is 0xc0 in 8-bit format.
		CRYSTAL_FREQ_16000000HZ,		// The MxL5007T Crystal frequency is 16.0 MHz.
		MXL5007T_LOOP_THROUGH_DISABLE,	// The MxL5007T loop-through mode is disabled.
		MXL5007T_CLK_OUT_DISABLE,		// The MxL5007T clock output mode is disabled.
		MXL5007T_CLK_OUT_AMP_0			// The MxL5007T clock output amplitude is 0.
		);



	// See the example for other NIM functions in dvbt_nim_base.h

	...


	return 0;
}


@endcode

*/


#include "demod_rtl2832.h"
#include "tuner_mxl5007t.h"
#include "dvbt_nim_base.h"





// Definitions
#define RTL2832_MXL5007T_ADDITIONAL_INIT_REG_TABLE_LEN		23

// Default
#define RTL2832_MXL5007T_STANDARD_MODE_DEFAULT				MXL5007T_STANDARD_DVBT
#define RTL2832_MXL5007T_IF_FREQ_HZ_DEFAULT					IF_FREQ_4570000HZ
#define RTL2832_MXL5007T_SPECTRUM_MODE_DEFAULT				SPECTRUM_NORMAL
#define RTL2832_MXL5007T_QAM_IF_DIFF_OUT_LEVEL_DEFAULT		0





// Builder
void
BuildRtl2832Mxl5007tModule(
	DVBT_NIM_MODULE **ppNim,								// DVB-T NIM dependence
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
	unsigned long TunerCrystalFreqHz,
	int TunerLoopThroughMode,
	int TunerClkOutMode,
	int TunerClkOutAmpMode
	);



int
rtl2832_mxl5007t_Initialize_fm(
	DVBT_NIM_MODULE *pNim
	);

// RTL2832 MxL5007T NIM manipulaing functions
int
rtl2832_mxl5007t_Initialize(
	DVBT_NIM_MODULE *pNim
	);

int
rtl2832_mxl5007t_SetParameters(
	DVBT_NIM_MODULE *pNim,
	unsigned long RfFreqHz,
	int BandwidthMode
	);

int
rtl2832_mxl5007t_SetParameters_fm(
	DVBT_NIM_MODULE *pNim,
	unsigned long RfFreqHz,
	int BandwidthMode
	);







#endif


