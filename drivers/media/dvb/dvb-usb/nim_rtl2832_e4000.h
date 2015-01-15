#ifndef __NIM_RTL2832_E4000
#define __NIM_RTL2832_E4000

/**

@file

@brief   RTL2832 E4000 NIM module declaration

One can manipulate RTL2832 E4000 NIM through RTL2832 E4000 NIM module.
RTL2832 E4000 NIM module is derived from DVB-T NIM module.



@par Example:
@code

// The example is the same as the NIM example in dvbt_nim_base.h except the listed lines.



#include "nim_rtl2832_e4000.h"


...



int main(void)
{
	DVBT_NIM_MODULE *pNim;
	DVBT_NIM_MODULE DvbtNimModuleMemory;

	...



	// Build RTL2832 E4000 NIM module.
	BuildRtl2832E4000Module(
		&pNim,
		&DvbtNimModuleMemory,

		9,									// Maximum I2C reading byte number is 9.
		8,									// Maximum I2C writing byte number is 8.
		CustomI2cRead,						// Employ CustomI2cRead() as basic I2C reading function.
		CustomI2cWrite,						// Employ CustomI2cWrite() as basic I2C writing function.
		CustomWaitMs,						// Employ CustomWaitMs() as basic waiting function.

		0x20,								// The RTL2832 I2C device address is 0x20 in 8-bit format.
		CRYSTAL_FREQ_28800000HZ,			// The RTL2832 crystal frequency is 28.8 MHz.
		TS_INTERFACE_SERIAL,				// The RTL2832 TS interface mode is serial.
		RTL2832_APPLICATION_STB,			// The RTL2832 application mode is STB mode.
		200,								// The RTL2832 update function reference period is 200 millisecond
		YES,								// The RTL2832 Function 1 enabling status is YES.

		0xc8,								// The E4000 I2C device address is 0xc8 in 8-bit format.
		CRYSTAL_FREQ_26000000HZ				// The E4000 crystal frequency is 26 MHz.
		);



	// See the example for other NIM functions in dvbt_nim_base.h

	...


	return 0;
}


@endcode

*/


#include "demod_rtl2832.h"
#include "tuner_e4000.h"
#include "dvbt_nim_base.h"





// Definitions
#define RTL2832_E4000_ADDITIONAL_INIT_REG_TABLE_LEN		34

#define RTL2832_E4000_LNA_GAIN_TABLE_LEN				16
#define RTL2832_E4000_LNA_GAIN_ADD_TABLE_LEN			8
#define RTL2832_E4000_MIXER_GAIN_TABLE_LEN				2
#define RTL2832_E4000_IF_STAGE_1_GAIN_TABLE_LEN			2
#define RTL2832_E4000_IF_STAGE_2_GAIN_TABLE_LEN			4
#define RTL2832_E4000_IF_STAGE_3_GAIN_TABLE_LEN			4
#define RTL2832_E4000_IF_STAGE_4_GAIN_TABLE_LEN			4
#define RTL2832_E4000_IF_STAGE_5_GAIN_TABLE_LEN			8
#define RTL2832_E4000_IF_STAGE_6_GAIN_TABLE_LEN			8

#define RTL2832_E4000_LNA_GAIN_BAND_NUM					2
#define RTL2832_E4000_MIXER_GAIN_BAND_NUM				2

#define RTL2832_E4000_RF_BAND_BOUNDARY_HZ				300000000

#define RTL2832_E4000_LNA_GAIN_ADDR						0x14
#define RTL2832_E4000_LNA_GAIN_MASK						0xf
#define RTL2832_E4000_LNA_GAIN_SHIFT					0

#define RTL2832_E4000_LNA_GAIN_ADD_ADDR					0x24
#define RTL2832_E4000_LNA_GAIN_ADD_MASK					0x7
#define RTL2832_E4000_LNA_GAIN_ADD_SHIFT				0

#define RTL2832_E4000_MIXER_GAIN_ADDR					0x15
#define RTL2832_E4000_MIXER_GAIN_MASK					0x1
#define RTL2832_E4000_MIXER_GAIN_SHIFT					0

#define RTL2832_E4000_IF_STAGE_1_GAIN_ADDR				0x16
#define RTL2832_E4000_IF_STAGE_1_GAIN_MASK				0x1
#define RTL2832_E4000_IF_STAGE_1_GAIN_SHIFT				0

#define RTL2832_E4000_IF_STAGE_2_GAIN_ADDR				0x16
#define RTL2832_E4000_IF_STAGE_2_GAIN_MASK				0x6
#define RTL2832_E4000_IF_STAGE_2_GAIN_SHIFT				1

#define RTL2832_E4000_IF_STAGE_3_GAIN_ADDR				0x16
#define RTL2832_E4000_IF_STAGE_3_GAIN_MASK				0x18
#define RTL2832_E4000_IF_STAGE_3_GAIN_SHIFT				3

#define RTL2832_E4000_IF_STAGE_4_GAIN_ADDR				0x16
#define RTL2832_E4000_IF_STAGE_4_GAIN_MASK				0x60
#define RTL2832_E4000_IF_STAGE_4_GAIN_SHIFT				5

#define RTL2832_E4000_IF_STAGE_5_GAIN_ADDR				0x17
#define RTL2832_E4000_IF_STAGE_5_GAIN_MASK				0x7
#define RTL2832_E4000_IF_STAGE_5_GAIN_SHIFT				0

#define RTL2832_E4000_IF_STAGE_6_GAIN_ADDR				0x17
#define RTL2832_E4000_IF_STAGE_6_GAIN_MASK				0x38
#define RTL2832_E4000_IF_STAGE_6_GAIN_SHIFT				3

#define RTL2832_E4000_TUNER_OUTPUT_POWER_UNIT_0P1_DBM	-100

#define RTL2832_E4000_TUNER_MODE_UPDATE_WAIT_TIME_MS	1000


// Tuner gain mode
enum RTL2832_E4000_TUNER_GAIN_MODE
{
	RTL2832_E4000_TUNER_GAIN_SENSITIVE,
	RTL2832_E4000_TUNER_GAIN_NORMAL,
	RTL2832_E4000_TUNER_GAIN_LINEAR,
};





// Builder
void
BuildRtl2832E4000Module(
	DVBT_NIM_MODULE **ppNim,									// DVB-T NIM dependence
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





// RTL2832 E4000 NIM manipulaing functions
int
rtl2832_e4000_Initialize(
	DVBT_NIM_MODULE *pNim
	);

int
rtl2832_e4000_Initialize_fm(
	DVBT_NIM_MODULE *pNim
	);


int
rtl2832_e4000_SetParameters(
	DVBT_NIM_MODULE *pNim,
	unsigned long RfFreqHz,
	int BandwidthMode
	);

int
rtl2832_e4000_SetParameters_fm(
	DVBT_NIM_MODULE *pNim,
	unsigned long RfFreqHz,
	int BandwidthMode
	);

int
rtl2832_e4000_UpdateFunction(
	DVBT_NIM_MODULE *pNim
	);

int
rtl2832_e4000_UpdateTunerMode(
	DVBT_NIM_MODULE *pNim
	);







#endif


