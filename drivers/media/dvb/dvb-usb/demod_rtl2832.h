#ifndef __DEMOD_RTL2832_H
#define __DEMOD_RTL2832_H

/**

@file

@brief   RTL2832 demod module declaration

One can manipulate RTL2832 demod through RTL2832 module.
RTL2832 module is derived from DVB-T demod module.



@par Example:
@code

// The example is the same as the DVB-T demod example in dvbt_demod_base.h except the listed lines.



#include "demod_rtl2832.h"


...



int main(void)
{
	DVBT_DEMOD_MODULE *pDemod;

	DVBT_DEMOD_MODULE     DvbtDemodModuleMemory;
	BASE_INTERFACE_MODULE BaseInterfaceModuleMemory;
	I2C_BRIDGE_MODULE     I2cBridgeModuleMemory;


	...



	// Build RTL2832 demod module.
	BuildRtl2832Module(
		&pDemod,
		&DvbtDemodModuleMemory,
		&BaseInterfaceModuleMemory,
		&I2cBridgeModuleMemory,
		0x20,								// I2C device address is 0x20 in 8-bit format.
		CRYSTAL_FREQ_28800000HZ,			// Crystal frequency is 28.8 MHz.
		TS_INTERFACE_SERIAL,				// TS interface mode is serial.
		RTL2832_APPLICATION_STB,			// Application mode is STB.
		200,								// Update function reference period is 200 millisecond
		YES									// Function 1 enabling status is YES.
		);



	// See the example for other DVB-T demod functions in dvbt_demod_base.h

	...


	return 0;
}


@endcode

*/


#include "dvbt_demod_base.h"


extern int dvb_usb_rtl2832u_snrdb;


// Definitions

// Initializing
#define RTL2832_INIT_TABLE_LEN						32
#define RTL2832_TS_INTERFACE_INIT_TABLE_LEN			5
#define RTL2832_APP_INIT_TABLE_LEN					5


// Bandwidth setting
#define RTL2832_H_LPF_X_PAGE		1
#define RTL2832_H_LPF_X_ADDR		0x1c
#define RTL2832_H_LPF_X_LEN			32
#define RTL2832_RATIO_PAGE			1
#define RTL2832_RATIO_ADDR			0x9d
#define RTL2832_RATIO_LEN			6


// Bandwidth setting
#define RTL2832_CFREQ_OFF_RATIO_BIT_NUM		20


// IF frequency setting
#define RTL2832_PSET_IFFREQ_BIT_NUM		22


// Signal quality
#define RTL2832_SQ_FRAC_BIT_NUM			5


// BER
#define RTL2832_BER_DEN_VALUE				1000000


// SNR
#define RTL2832_CE_EST_EVM_MAX_VALUE		65535
#define RTL2832_SNR_FRAC_BIT_NUM			10
#define RTL2832_SNR_DB_DEN					3402


// AGC
#define RTL2832_RF_AGC_REG_BIT_NUM		14
#define RTL2832_IF_AGC_REG_BIT_NUM		14


// TR offset and CR offset
#define RTL2832_SFREQ_OFF_BIT_NUM		14
#define RTL2832_CFREQ_OFF_BIT_NUM		18


// Register table length
#define RTL2832_REG_TABLE_LEN			127


// Function 1
#define RTL2832_FUNC1_WAIT_TIME_MS			500
#define RTL2832_FUNC1_GETTING_TIME_MS		200
#define RTL2832_FUNC1_GETTING_NUM_MIN		20



/// Demod application modes
enum RTL2832_APPLICATION_MODE
{
	RTL2832_APPLICATION_DONGLE,
	RTL2832_APPLICATION_STB,
};
#define RTL2832_APPLICATION_MODE_NUM		2


// Function 1
enum RTL2832_FUNC1_CONFIG_MODE
{
	RTL2832_FUNC1_CONFIG_1,
	RTL2832_FUNC1_CONFIG_2,
	RTL2832_FUNC1_CONFIG_3,
};
#define RTL2832_FUNC1_CONFIG_MODE_NUM		3
#define RTL2832_FUNC1_CONFIG_NORMAL			-1


enum RTL2832_FUNC1_STATE
{
	RTL2832_FUNC1_STATE_NORMAL,
	RTL2832_FUNC1_STATE_NORMAL_GET_BER,
	RTL2832_FUNC1_STATE_CONFIG_1_WAIT,
	RTL2832_FUNC1_STATE_CONFIG_1_GET_BER,
	RTL2832_FUNC1_STATE_CONFIG_2_WAIT,
	RTL2832_FUNC1_STATE_CONFIG_2_GET_BER,
	RTL2832_FUNC1_STATE_CONFIG_3_WAIT,
	RTL2832_FUNC1_STATE_CONFIG_3_GET_BER,
	RTL2832_FUNC1_STATE_DETERMINED_WAIT,
	RTL2832_FUNC1_STATE_DETERMINED,
};





// Demod module builder
void
BuildRtl2832Module(
	DVBT_DEMOD_MODULE **ppDemod,
	DVBT_DEMOD_MODULE *pDvbtDemodModuleMemory,
	BASE_INTERFACE_MODULE *pBaseInterfaceModuleMemory,
	I2C_BRIDGE_MODULE *pI2cBridgeModuleMemory,
	unsigned char DeviceAddr,
	unsigned long CrystalFreqHz,
	int TsInterfaceMode,
	int AppMode,
	unsigned long UpdateFuncRefPeriodMs,
	int IsFunc1Enabled
	);





// Manipulating functions
void
rtl2832_IsConnectedToI2c(
	DVBT_DEMOD_MODULE *pDemod,
	int *pAnswer
);

int
rtl2832_SoftwareReset(
	DVBT_DEMOD_MODULE *pDemod
	);

int
rtl2832_Initialize(
	DVBT_DEMOD_MODULE *pDemod
	);

int
rtl2832_SetBandwidthMode(
	DVBT_DEMOD_MODULE *pDemod,
	int BandwidthMode
	);

int
rtl2832_SetIfFreqHz(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned long IfFreqHz
	);

int
rtl2832_SetSpectrumMode(
	DVBT_DEMOD_MODULE *pDemod,
	int SpectrumMode
	);

int
rtl2832_IsTpsLocked(
	DVBT_DEMOD_MODULE *pDemod,
	int *pAnswer
	);

int
rtl2832_IsSignalLocked(
	DVBT_DEMOD_MODULE *pDemod,
	int *pAnswer
	);

int
rtl2832_GetSignalStrength(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned long *pSignalStrength
	);

int
rtl2832_GetSignalQuality(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned long *pSignalQuality
	);

int
rtl2832_GetBer(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned long *pBerNum,
	unsigned long *pBerDen
	);

int
rtl2832_GetSnrDb(
	DVBT_DEMOD_MODULE *pDemod,
	long *pSnrDbNum,
	long *pSnrDbDen
	);

int
rtl2832_GetRfAgc(
	DVBT_DEMOD_MODULE *pDemod,
	long *pRfAgc
	);

int
rtl2832_GetIfAgc(
	DVBT_DEMOD_MODULE *pDemod,
	long *pIfAgc
	);

int
rtl2832_GetDiAgc(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned char *pDiAgc
	);

int
rtl2832_GetTrOffsetPpm(
	DVBT_DEMOD_MODULE *pDemod,
	long *pTrOffsetPpm
	);

int
rtl2832_GetCrOffsetHz(
	DVBT_DEMOD_MODULE *pDemod,
	long *pCrOffsetHz
	);

int
rtl2832_GetConstellation(
	DVBT_DEMOD_MODULE *pDemod,
	int *pConstellation
	);

int
rtl2832_GetHierarchy(
	DVBT_DEMOD_MODULE *pDemod,
	int *pHierarchy
	);

int
rtl2832_GetCodeRateLp(
	DVBT_DEMOD_MODULE *pDemod,
	int *pCodeRateLp
	);

int
rtl2832_GetCodeRateHp(
	DVBT_DEMOD_MODULE *pDemod,
	int *pCodeRateHp
	);

int
rtl2832_GetGuardInterval(
	DVBT_DEMOD_MODULE *pDemod,
	int *pGuardInterval
	);

int
rtl2832_GetFftMode(
	DVBT_DEMOD_MODULE *pDemod,
	int *pFftMode
	);

int
rtl2832_UpdateFunction(
	DVBT_DEMOD_MODULE *pDemod
	);

int
rtl2832_ResetFunction(
	DVBT_DEMOD_MODULE *pDemod
	);





// I2C command forwarding functions
int
rtl2832_ForwardI2cReadingCmd(
	I2C_BRIDGE_MODULE *pI2cBridge,
	unsigned char DeviceAddr,
	unsigned char *pReadingBytes,
	unsigned long ByteNum
	);

int
rtl2832_ForwardI2cWritingCmd(
	I2C_BRIDGE_MODULE *pI2cBridge,
	unsigned char DeviceAddr,
	const unsigned char *pWritingBytes,
	unsigned long ByteNum
	);





// Register table initializing
void
rtl2832_InitRegTable(
	DVBT_DEMOD_MODULE *pDemod
	);





// I2C birdge module builder
void
rtl2832_BuildI2cBridgeModule(
	DVBT_DEMOD_MODULE *pDemod
	);





// RTL2832 extra functions
void
rtl2832_GetAppMode(
	DVBT_DEMOD_MODULE *pDemod,
	int *pAppMode
	);





// RTL2832 dependence
int
rtl2832_func1_Reset(
	DVBT_DEMOD_MODULE *pDemod
	);

int
rtl2832_func1_Update(
	DVBT_DEMOD_MODULE *pDemod
	);

int
rtl2832_func1_IsCriterionMatched(
	DVBT_DEMOD_MODULE *pDemod,
	int *pAnswer
	);

int
rtl2832_func1_AccumulateRsdBerEst(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned long *pAccumulativeValue
	);

int
rtl2832_func1_ResetReg(
	DVBT_DEMOD_MODULE *pDemod
	);

int
rtl2832_func1_SetCommonReg(
	DVBT_DEMOD_MODULE *pDemod
	);

int
rtl2832_func1_SetRegWithFftMode(
	DVBT_DEMOD_MODULE *pDemod,
	int FftMode
	);

int
rtl2832_func1_SetRegWithConfigMode(
	DVBT_DEMOD_MODULE *pDemod,
	int ConfigMode
	);

void
rtl2832_func1_GetMinWeightedBerConfigMode(
	DVBT_DEMOD_MODULE *pDemod,
	int *pConfigMode
	);
















#endif
