#ifndef __DEMOD_RTL2836_H
#define __DEMOD_RTL2836_H

/**

@file

@brief   RTL2836 demod module declaration

One can manipulate RTL2836 demod through RTL2836 module.
RTL2836 module is derived from DTMB demod module.



@par Example:
@code

// The example is the same as the DTMB demod example in dtmb_demod_base.h except the listed lines.



#include "demod_rtl2836.h"


...



int main(void)
{
	DTMB_DEMOD_MODULE *pDemod;

	DTMB_DEMOD_MODULE     DtmbDemodModuleMemory;
	BASE_INTERFACE_MODULE BaseInterfaceModuleMemory;
	I2C_BRIDGE_MODULE     I2cBridgeModuleMemory;


	...



	// Build RTL2836 demod module.
	BuildRtl2836Module(
		&pDemod,
		&DtmbDemodModuleMemory,
		&BaseInterfaceModuleMemory,
		&I2cBridgeModuleMemory,
		0x3e,								// I2C device address is 0x3e in 8-bit format.
		CRYSTAL_FREQ_27000000HZ,			// Crystal frequency is 27.0 MHz.
		TS_INTERFACE_SERIAL,				// TS interface mode is serial.
		50,									// Update function reference period is 50 millisecond
		YES,								// Function 1 enabling status is on.
		YES									// Function 2 enabling status is on.
		);



	// See the example for other DTMB demod functions in dtmb_demod_base.h

	...


	return 0;
}


@endcode

*/


#include "dtmb_demod_base.h"





// Definitions

// Initializing
#define RTL2836_INIT_REG_TABLE_LEN					86
#define RTL2836_TS_INTERFACE_INIT_TABLE_LEN			3


// Chip ID
#define RTL2836_CHIP_ID_VALUE			0x4


// IF frequency setting
#define RTL2836_ADC_FREQ_HZ			48000000
#define RTL2836_IFFREQ_BIT_NUM		10


// BER
#define RTL2836_BER_DEN_VALUE				1000000


// PER
#define RTL2836_PER_DEN_VALUE				32768


// SNR
#define RTL2836_EST_SNR_BIT_NUM				9
#define RTL2836_SNR_DB_DEN_VALUE			4


// AGC
#define RTL2836_RF_AGC_REG_BIT_NUM		14
#define RTL2836_IF_AGC_REG_BIT_NUM		14


// TR offset and CR offset
#define RTL2836_TR_OUT_R_BIT_NUM			17
#define RTL2836_SFOAQ_OUT_R_BIT_NUM			14
#define RTL2836_CFO_EST_R_BIT_NUM			23


// Register table length
#define RTL2836_REG_TABLE_LEN			25


// Function 1
#define RTL2836_FUNC1_CHECK_TIME_MS			500


// Function 2
enum RTL2836_FUNC2_SIGNAL_MODE
{
	RTL2836_FUNC2_SIGNAL_NORMAL,
	RTL2836_FUNC2_SIGNAL_PARTICULAR,
};





// Demod module builder
void
BuildRtl2836Module(
	DTMB_DEMOD_MODULE **ppDemod,
	DTMB_DEMOD_MODULE *pDtmbDemodModuleMemory,
	BASE_INTERFACE_MODULE *pBaseInterfaceModuleMemory,
	I2C_BRIDGE_MODULE *pI2cBridgeModuleMemory,
	unsigned char DeviceAddr,
	unsigned long CrystalFreqHz,
	int TsInterfaceMode,
	unsigned long UpdateFuncRefPeriodMs,
	int IsFunc1Enabled,
	int IsFunc2Enabled
	);





// Manipulating functions
void
rtl2836_IsConnectedToI2c(
	DTMB_DEMOD_MODULE *pDemod,
	int *pAnswer
);

int
rtl2836_SoftwareReset(
	DTMB_DEMOD_MODULE *pDemod
	);

int
rtl2836_Initialize(
	DTMB_DEMOD_MODULE *pDemod
	);

int
rtl2836_SetIfFreqHz(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned long IfFreqHz
	);

int
rtl2836_SetSpectrumMode(
	DTMB_DEMOD_MODULE *pDemod,
	int SpectrumMode
	);

int
rtl2836_IsSignalLocked(
	DTMB_DEMOD_MODULE *pDemod,
	int *pAnswer
	);

int
rtl2836_GetSignalStrength(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned long *pSignalStrength
	);

int
rtl2836_GetSignalQuality(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned long *pSignalQuality
	);

int
rtl2836_GetBer(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned long *pBerNum,
	unsigned long *pBerDen
	);

int
rtl2836_GetPer(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned long *pPerNum,
	unsigned long *pPerDen
	);

int
rtl2836_GetSnrDb(
	DTMB_DEMOD_MODULE *pDemod,
	long *pSnrDbNum,
	long *pSnrDbDen
	);

int
rtl2836_GetRfAgc(
	DTMB_DEMOD_MODULE *pDemod,
	long *pRfAgc
	);

int
rtl2836_GetIfAgc(
	DTMB_DEMOD_MODULE *pDemod,
	long *pIfAgc
	);

int
rtl2836_GetDiAgc(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned long *pDiAgc
	);

int
rtl2836_GetTrOffsetPpm(
	DTMB_DEMOD_MODULE *pDemod,
	long *pTrOffsetPpm
	);

int
rtl2836_GetCrOffsetHz(
	DTMB_DEMOD_MODULE *pDemod,
	long *pCrOffsetHz
	);

int
rtl2836_GetCarrierMode(
	DTMB_DEMOD_MODULE *pDemod,
	int *pCarrierMode
	);

int
rtl2836_GetPnMode(
	DTMB_DEMOD_MODULE *pDemod,
	int *pPnMode
	);

int
rtl2836_GetQamMode(
	DTMB_DEMOD_MODULE *pDemod,
	int *pQamMode
	);

int
rtl2836_GetCodeRateMode(
	DTMB_DEMOD_MODULE *pDemod,
	int *pCodeRateMode
	);

int
rtl2836_GetTimeInterleaverMode(
	DTMB_DEMOD_MODULE *pDemod,
	int *pTimeInterleaverMode
	);

int
rtl2836_UpdateFunction(
	DTMB_DEMOD_MODULE *pDemod
	);

int
rtl2836_ResetFunction(
	DTMB_DEMOD_MODULE *pDemod
	);





// I2C command forwarding functions
int
rtl2836_ForwardI2cReadingCmd(
	I2C_BRIDGE_MODULE *pI2cBridge,
	unsigned char DeviceAddr,
	unsigned char *pReadingBytes,
	unsigned long ByteNum
	);

int
rtl2836_ForwardI2cWritingCmd(
	I2C_BRIDGE_MODULE *pI2cBridge,
	unsigned char DeviceAddr,
	const unsigned char *pWritingBytes,
	unsigned long ByteNum
	);





// Register table initializing
void
rtl2836_InitRegTable(
	DTMB_DEMOD_MODULE *pDemod
	);





// I2C birdge module builder
void
rtl2836_BuildI2cBridgeModule(
	DTMB_DEMOD_MODULE *pDemod
	);





// RTL2836 dependence
int
rtl2836_func1_Reset(
	DTMB_DEMOD_MODULE *pDemod
	);

int
rtl2836_func1_Update(
	DTMB_DEMOD_MODULE *pDemod
	);

int
rtl2836_func2_Reset(
	DTMB_DEMOD_MODULE *pDemod
	);

int
rtl2836_func2_Update(
	DTMB_DEMOD_MODULE *pDemod
	);

















#endif
