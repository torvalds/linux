#ifndef __DTMB_NIM_BASE_H
#define __DTMB_NIM_BASE_H

/**

@file

@brief   DTMB NIM base module definition

DTMB NIM base module definitions contains NIM module structure, NIM funciton pointers, NIM definitions, and NIM default
functions.



@par Example:
@code


#include "nim_demodx_tunery.h"



int
CustomI2cRead(
	BASE_INTERFACE_MODULE *pBaseInterface,
	unsigned char DeviceAddr,
	unsigned char *pReadingBytes,
	unsigned long ByteNum
	)
{
	// I2C reading format:
	// start_bit + (DeviceAddr | reading_bit) + reading_byte * ByteNum + stop_bit

	...

	return FUNCTION_SUCCESS;

error_status:
	return FUNCTION_ERROR;
}



int
CustomI2cWrite(
	BASE_INTERFACE_MODULE *pBaseInterface,
	unsigned char DeviceAddr,
	const unsigned char *pWritingBytes,
	unsigned long ByteNum
	)
{
	// I2C writing format:
	// start_bit + (DeviceAddr | writing_bit) + writing_byte * ByteNum + stop_bit

	...

	return FUNCTION_SUCCESS;

error_status:
	return FUNCTION_ERROR;
}



void
CustomWaitMs(
	BASE_INTERFACE_MODULE *pBaseInterface,
	unsigned long WaitTimeMs
	)
{
	// Wait WaitTimeMs milliseconds.

	...

	return;
}



int main(void)
{
	DTMB_NIM_MODULE *pNim;
	DTMB_NIM_MODULE DtmbNimModuleMemory;
	DEMODX_EXTRA_MODULE DemodxExtraModuleMemory;
	TUNERY_EXTRA_MODULE TuneryExtraModuleMemory;

	unsigned long RfFreqHz;

	int Answer;
	unsigned long SignalStrength, SignalQuality;
	unsigned long BerNum, BerDen;
	double Ber;
	unsigned long PerNum, PerDen;
	double Per;
	unsigned long SnrDbNum, SnrDbDen;
	double SnrDb;
	long TrOffsetPpm, CrOffsetHz;

	int CarrierMode;
	int PnMode;
	int QamMode;
	int CodeRateMode;
	int TimeInterleaverMode;



	// Build Demod-X Tuner-Y NIM module.
	BuildDemodxTuneryModule(
		&pNim,
		&DtmbNimModuleMemory,

		9,								// Maximum I2C reading byte number is 9.
		8,								// Maximum I2C writing byte number is 8.
		CustomI2cRead,					// Employ CustomI2cRead() as basic I2C reading function.
		CustomI2cWrite,					// Employ CustomI2cWrite() as basic I2C writing function.
		CustomWaitMs					// Employ CustomWaitMs() as basic waiting function.

		&DemodxExtraModuleMemory,		// Employ Demod-X extra module.
		0x3e,							// The Demod-X I2C device address is 0x3e in 8-bit format.
		CRYSTAL_FREQ_27000000HZ,		// The Demod-X crystal frequency is 27.0 MHz.
		...								// Other arguments for Demod-X

		&TunerxExtraModuleMemory,		// Employ Tuner-Y extra module.
		0xc0,							// The Tuner-Y I2C device address is 0xc0 in 8-bit format.
		...								// Other arguments for Tuner-Y
		);



	// Get NIM type.
	// Note: NIM types are defined in the MODULE_TYPE enumeration.
	pNim->GetNimType(pNim, &NimType);







	// ==== Initialize NIM and set its parameters =====

	// Initialize NIM.
	pNim->Initialize(pNim);

	// Set NIM parameters. (RF frequency)
	// Note: In the example: RF frequency is 666 MHz.
	RfFreqHz = 666000000;
	pNim->SetParameters(pNim, RfFreqHz);



	// Wait 1 second for demod convergence.





	// ==== Get NIM information =====

	// Get NIM parameters. (RF frequency)
	pNim->GetParameters(pNim, &RfFreqHz);


	// Get signal present status.
	// Note: 1. The argument Answer is YES when the NIM module has found DTMB signal in the RF channel.
	//       2. The argument Answer is NO when the NIM module does not find DTMB signal in the RF channel.
	// Recommendation: Use the IsSignalPresent() function for channel scan.
	pNim->IsSignalPresent(pNim, &Answer);

	// Get signal lock status.
	// Note: 1. The argument Answer is YES when the NIM module has locked DTMB signal in the RF channel.
	//          At the same time, the NIM module sends TS packets through TS interface hardware pins.
	//       2. The argument Answer is NO when the NIM module does not lock DTMB signal in the RF channel.
	// Recommendation: Use the IsSignalLocked() function for signal lock check.
	pNim->IsSignalLocked(pNim, &Answer);


	// Get signal strength.
	// Note: 1. The range of SignalStrength is 0~100.
	//       2. Need to map SignalStrength value to UI signal strength bar manually.
	pNim->GetSignalStrength(pNim, &SignalStrength);

	// Get signal quality.
	// Note: 1. The range of SignalQuality is 0~100.
	//       2. Need to map SignalQuality value to UI signal quality bar manually.
	pNim->GetSignalQuality(pNim, &SignalQuality);


	// Get BER.
	pNim->GetBer(pNim, &BerNum, &BerDen);
	Ber = (double)BerNum / (double)BerDen;

	// Get PER.
	pNim->GetPer(pNim, &PerNum, &PerDen);
	Per = (double)PerNum / (double)PerDen;

	// Get SNR in dB.
	pNim->GetSnrDb(pNim, &SnrDbNum, &SnrDbDen);
	SnrDb = (double)SnrDbNum / (double)SnrDbDen;


	// Get TR offset (symbol timing offset) in ppm.
	pNim->GetTrOffsetPpm(pNim, &TrOffsetPpm);

	// Get CR offset (RF frequency offset) in Hz.
	pNim->GetCrOffsetHz(pNim, &CrOffsetHz);


	// Get signal information.
	// Note: One can find signal information definitions in the enumerations as follows:
	//       1. DTMB_CARRIER_MODE
	//       2. DTMB_PN_MODE
	//       3. DTMB_QAM_MODE
	//       4. DTMB_CODE_RATE_MODE
	//       5. DTMB_TIME_INTERLEAVER_MODE
	pNim->GetSignalInfo(pNim, &CarrierMode, &PnMode, &QamMode, &CodeRateMode, &TimeInterleaverMode);



	return 0;
}


@endcode

*/


#include "foundation.h"
#include "tuner_base.h"
#include "dtmb_demod_base.h"





// Definitions
#define DTMB_NIM_SINGAL_PRESENT_CHECK_TIMES_MAX_DEFAULT			1
#define DTMB_NIM_SINGAL_LOCK_CHECK_TIMES_MAX_DEFAULT			1





/// DTMB NIM module pre-definition
typedef struct DTMB_NIM_MODULE_TAG DTMB_NIM_MODULE;





/**

@brief   DTMB demod type getting function pointer

One can use DTMB_NIM_FP_GET_NIM_TYPE() to get DTMB NIM type.


@param [in]    pNim       The NIM module pointer
@param [out]   pNimType   Pointer to an allocated memory for storing NIM type


@note
	-# NIM building function will set DTMB_NIM_FP_GET_NIM_TYPE() with the corresponding function.


@see   MODULE_TYPE

*/
typedef void
(*DTMB_NIM_FP_GET_NIM_TYPE)(
	DTMB_NIM_MODULE *pNim,
	int *pNimType
	);





/**

@brief   DTMB NIM initializing function pointer

One can use DTMB_NIM_FP_INITIALIZE() to initialie DTMB NIM.


@param [in]   pNim   The NIM module pointer


@retval   FUNCTION_SUCCESS   Initialize NIM successfully.
@retval   FUNCTION_ERROR     Initialize NIM unsuccessfully.


@note
	-# NIM building function will set DTMB_NIM_FP_INITIALIZE() with the corresponding function.

*/
typedef int
(*DTMB_NIM_FP_INITIALIZE)(
	DTMB_NIM_MODULE *pNim
	);





/**

@brief   DTMB NIM parameter setting function pointer

One can use DTMB_NIM_FP_SET_PARAMETERS() to set DTMB NIM parameters.


@param [in]   pNim            The NIM module pointer
@param [in]   RfFreqHz        RF frequency in Hz for setting


@retval   FUNCTION_SUCCESS   Set NIM parameters successfully.
@retval   FUNCTION_ERROR     Set NIM parameters unsuccessfully.


@note
	-# NIM building function will set DTMB_NIM_FP_SET_PARAMETERS() with the corresponding function.


@see   DTMB_BANDWIDTH_MODE

*/
typedef int
(*DTMB_NIM_FP_SET_PARAMETERS)(
	DTMB_NIM_MODULE *pNim,
	unsigned long RfFreqHz
	);





/**

@brief   DTMB NIM parameter getting function pointer

One can use DTMB_NIM_FP_GET_PARAMETERS() to get DTMB NIM parameters.


@param [in]    pNim             The NIM module pointer
@param [out]   pRfFreqHz        Pointer to an allocated memory for storing NIM RF frequency in Hz
@param [out]   pBandwidthMode   Pointer to an allocated memory for storing NIM bandwidth mode


@retval   FUNCTION_SUCCESS   Get NIM parameters successfully.
@retval   FUNCTION_ERROR     Get NIM parameters unsuccessfully.


@note
	-# NIM building function will set DTMB_NIM_FP_GET_PARAMETERS() with the corresponding function.


@see   DTMB_BANDWIDTH_MODE

*/
typedef int
(*DTMB_NIM_FP_GET_PARAMETERS)(
	DTMB_NIM_MODULE *pNim,
	unsigned long *pRfFreqHz
	);





/**

@brief   DTMB NIM signal present asking function pointer

One can use DTMB_NIM_FP_IS_SIGNAL_PRESENT() to ask DTMB NIM if signal is present.


@param [in]    pNim      The NIM module pointer
@param [out]   pAnswer   Pointer to an allocated memory for storing answer


@retval   FUNCTION_SUCCESS   Perform signal present asking to NIM successfully.
@retval   FUNCTION_ERROR     Perform signal present asking to NIM unsuccessfully.


@note
	-# NIM building function will set DTMB_NIM_FP_IS_SIGNAL_PRESENT() with the corresponding function.

*/
typedef int
(*DTMB_NIM_FP_IS_SIGNAL_PRESENT)(
	DTMB_NIM_MODULE *pNim,
	int *pAnswer
	);





/**

@brief   DTMB NIM signal lock asking function pointer

One can use DTMB_NIM_FP_IS_SIGNAL_LOCKED() to ask DTMB NIM if signal is locked.


@param [in]    pNim      The NIM module pointer
@param [out]   pAnswer   Pointer to an allocated memory for storing answer


@retval   FUNCTION_SUCCESS   Perform signal lock asking to NIM successfully.
@retval   FUNCTION_ERROR     Perform signal lock asking to NIM unsuccessfully.


@note
	-# NIM building function will set DTMB_NIM_FP_IS_SIGNAL_LOCKED() with the corresponding function.

*/
typedef int
(*DTMB_NIM_FP_IS_SIGNAL_LOCKED)(
	DTMB_NIM_MODULE *pNim,
	int *pAnswer
	);





/**

@brief   DTMB NIM signal strength getting function pointer

One can use DTMB_NIM_FP_GET_SIGNAL_STRENGTH() to get signal strength.


@param [in]    pNim              The NIM module pointer
@param [out]   pSignalStrength   Pointer to an allocated memory for storing signal strength (value = 0 ~ 100)


@retval   FUNCTION_SUCCESS   Get NIM signal strength successfully.
@retval   FUNCTION_ERROR     Get NIM signal strength unsuccessfully.


@note
	-# NIM building function will set DTMB_NIM_FP_GET_SIGNAL_STRENGTH() with the corresponding function.

*/
typedef int
(*DTMB_NIM_FP_GET_SIGNAL_STRENGTH)(
	DTMB_NIM_MODULE *pNim,
	unsigned long *pSignalStrength
	);





/**

@brief   DTMB NIM signal quality getting function pointer

One can use DTMB_NIM_FP_GET_SIGNAL_QUALITY() to get signal quality.


@param [in]    pNim             The NIM module pointer
@param [out]   pSignalQuality   Pointer to an allocated memory for storing signal quality (value = 0 ~ 100)


@retval   FUNCTION_SUCCESS   Get NIM signal quality successfully.
@retval   FUNCTION_ERROR     Get NIM signal quality unsuccessfully.


@note
	-# NIM building function will set DTMB_NIM_FP_GET_SIGNAL_QUALITY() with the corresponding function.

*/
typedef int
(*DTMB_NIM_FP_GET_SIGNAL_QUALITY)(
	DTMB_NIM_MODULE *pNim,
	unsigned long *pSignalQuality
	);





/**

@brief   DTMB NIM BER value getting function pointer

One can use DTMB_NIM_FP_GET_BER() to get BER.


@param [in]    pNim      The NIM module pointer
@param [out]   pBerNum   Pointer to an allocated memory for storing BER numerator
@param [out]   pBerDen   Pointer to an allocated memory for storing BER denominator


@retval   FUNCTION_SUCCESS   Get NIM BER value successfully.
@retval   FUNCTION_ERROR     Get NIM BER value unsuccessfully.


@note
	-# NIM building function will set DTMB_NIM_FP_GET_BER() with the corresponding function.

*/
typedef int
(*DTMB_NIM_FP_GET_BER)(
	DTMB_NIM_MODULE *pNim,
	unsigned long *pBerNum,
	unsigned long *pBerDen
	);





/**

@brief   DTMB NIM PER value getting function pointer

One can use DTMB_NIM_FP_GET_PER() to get PER.


@param [in]    pNim      The NIM module pointer
@param [out]   pPerNum   Pointer to an allocated memory for storing PER numerator
@param [out]   pPerDen   Pointer to an allocated memory for storing PER denominator


@retval   FUNCTION_SUCCESS   Get NIM PER value successfully.
@retval   FUNCTION_ERROR     Get NIM PER value unsuccessfully.


@note
	-# NIM building function will set DTMB_NIM_FP_GET_PER() with the corresponding function.

*/
typedef int
(*DTMB_NIM_FP_GET_PER)(
	DTMB_NIM_MODULE *pNim,
	unsigned long *pPerNum,
	unsigned long *pPerDen
	);





/**

@brief   DTMB NIM SNR getting function pointer

One can use DTMB_NIM_FP_GET_SNR_DB() to get SNR in dB.


@param [in]    pNim        The NIM module pointer
@param [out]   pSnrDbNum   Pointer to an allocated memory for storing SNR dB numerator
@param [out]   pSnrDbDen   Pointer to an allocated memory for storing SNR dB denominator


@retval   FUNCTION_SUCCESS   Get NIM SNR successfully.
@retval   FUNCTION_ERROR     Get NIM SNR unsuccessfully.


@note
	-# NIM building function will set DTMB_NIM_FP_GET_SNR_DB() with the corresponding function.

*/
typedef int
(*DTMB_NIM_FP_GET_SNR_DB)(
	DTMB_NIM_MODULE *pNim,
	long *pSnrDbNum,
	long *pSnrDbDen
	);





/**

@brief   DTMB NIM TR offset getting function pointer

One can use DTMB_NIM_FP_GET_TR_OFFSET_PPM() to get TR offset in ppm.


@param [in]    pNim           The NIM module pointer
@param [out]   pTrOffsetPpm   Pointer to an allocated memory for storing TR offset in ppm


@retval   FUNCTION_SUCCESS   Get NIM TR offset successfully.
@retval   FUNCTION_ERROR     Get NIM TR offset unsuccessfully.


@note
	-# NIM building function will set DTMB_NIM_FP_GET_TR_OFFSET_PPM() with the corresponding function.

*/
typedef int
(*DTMB_NIM_FP_GET_TR_OFFSET_PPM)(
	DTMB_NIM_MODULE *pNim,
	long *pTrOffsetPpm
	);





/**

@brief   DTMB NIM CR offset getting function pointer

One can use DTMB_NIM_FP_GET_CR_OFFSET_HZ() to get CR offset in Hz.


@param [in]    pNim          The NIM module pointer
@param [out]   pCrOffsetHz   Pointer to an allocated memory for storing CR offset in Hz


@retval   FUNCTION_SUCCESS   Get NIM CR offset successfully.
@retval   FUNCTION_ERROR     Get NIM CR offset unsuccessfully.


@note
	-# NIM building function will set DTMB_NIM_FP_GET_CR_OFFSET_HZ() with the corresponding function.

*/
typedef int
(*DTMB_NIM_FP_GET_CR_OFFSET_HZ)(
	DTMB_NIM_MODULE *pNim,
	long *pCrOffsetHz
	);





/**

@brief   DTMB NIM signal information getting function pointer

One can use DTMB_NIM_FP_GET_SIGNAL_INFO() to get signal information.


@param [in]    pNim                   The NIM module pointer
@param [out]   pCarrierMode           Pointer to an allocated memory for storing demod carrier mode
@param [out]   pPnMode                Pointer to an allocated memory for storing demod PN mode
@param [out]   pQamMode               Pointer to an allocated memory for storing demod QAM mode
@param [out]   pCodeRateMode          Pointer to an allocated memory for storing demod code rate mode
@param [out]   pTimeInterleaverMode   Pointer to an allocated memory for storing demod time interleaver mode


@retval   FUNCTION_SUCCESS   Get NIM signal information successfully.
@retval   FUNCTION_ERROR     Get NIM signal information unsuccessfully.


@note
	-# NIM building function will set DTMB_NIM_FP_GET_SIGNAL_INFO() with the corresponding function.


@see   DTMB_CARRIER_MODE, DTMB_PN_MODE, DTMB_QAM_MODE, DTMB_CODE_RATE_MODE, DTMB_TIME_INTERLEAVER_MODE

*/
typedef int
(*DTMB_NIM_FP_GET_SIGNAL_INFO)(
	DTMB_NIM_MODULE *pNim,
	int *pCarrierMode,
	int *pPnMode,
	int *pQamMode,
	int *pCodeRateMode,
	int *pTimeInterleaverMode
	);





/**

@brief   DTMB NIM updating function pointer

One can use DTMB_NIM_FP_UPDATE_FUNCTION() to update NIM register setting.


@param [in]   pNim   The NIM module pointer


@retval   FUNCTION_SUCCESS   Update NIM setting successfully.
@retval   FUNCTION_ERROR     Update NIM setting unsuccessfully.


@note
	-# NIM building function will set DTMB_NIM_FP_UPDATE_FUNCTION() with the corresponding function.



@par Example:
@code


#include "nim_demodx_tunery.h"


int main(void)
{
	DTMB_NIM_MODULE *pNim;
	DTMB_NIM_MODULE DtmbNimModuleMemory;
	DEMODX_EXTRA_MODULE DemodxExtraModuleMemory;
	TUNERY_EXTRA_MODULE TuneryExtraModuleMemory;


	// Build Demod-X Tuner-Y NIM module.
	BuildDemodxTuneryModule(
		...
		);

	...


	return 0;
}


void PeriodicallyExecutingFunction
{
	// Executing UpdateFunction() periodically.
	pNim->UpdateFunction(pNim);
}


@endcode

*/
typedef int
(*DTMB_NIM_FP_UPDATE_FUNCTION)(
	DTMB_NIM_MODULE *pNim
	);





// RTL2836 E4000 extra module
typedef struct RTL2836_E4000_EXTRA_MODULE_TAG RTL2836_E4000_EXTRA_MODULE;
struct RTL2836_E4000_EXTRA_MODULE_TAG
{
	// Extra variables
	unsigned long TunerModeUpdateWaitTimeMax;
	unsigned long TunerModeUpdateWaitTime;
	unsigned char TunerGainMode;
};





// RTL2836B DTMB E4000 extra module
typedef struct RTL2836B_DTMB_E4000_EXTRA_MODULE_TAG RTL2836B_DTMB_E4000_EXTRA_MODULE;
struct RTL2836B_DTMB_E4000_EXTRA_MODULE_TAG
{
	// Extra variables
	unsigned long TunerModeUpdateWaitTimeMax;
	unsigned long TunerModeUpdateWaitTime;
	unsigned char TunerGainMode;
};





// RTL2836B DTMB E4005 extra module
typedef struct RTL2836B_DTMB_E4005_EXTRA_MODULE_TAG RTL2836B_DTMB_E4005_EXTRA_MODULE;
struct RTL2836B_DTMB_E4005_EXTRA_MODULE_TAG
{
	// Extra variables
	unsigned long TunerModeUpdateWaitTimeMax;
	unsigned long TunerModeUpdateWaitTime;
	unsigned char TunerGainMode;
};





// RTL2836B DTMB FC0012 extra module
typedef struct RTL2836B_DTMB_FC0012_EXTRA_MODULE_TAG RTL2836B_DTMB_FC0012_EXTRA_MODULE;
struct RTL2836B_DTMB_FC0012_EXTRA_MODULE_TAG
{
	// Extra variables
	unsigned long LnaUpdateWaitTimeMax;
	unsigned long LnaUpdateWaitTime;
	unsigned long RssiRCalOn;
};





// RTL2836B DTMB FC0013B extra module
typedef struct RTL2836B_DTMB_FC0013B_EXTRA_MODULE_TAG RTL2836B_DTMB_FC0013B_EXTRA_MODULE;
struct RTL2836B_DTMB_FC0013B_EXTRA_MODULE_TAG
{
	// Extra variables
	unsigned long LnaUpdateWaitTimeMax;
	unsigned long LnaUpdateWaitTime;
	unsigned long RssiRCalOn;
	unsigned char LowGainTestMode;
};





/// DTMB NIM module structure
struct DTMB_NIM_MODULE_TAG
{
	// Private variables
	int NimType;

	union														///<   NIM extra module used by driving module
	{
		RTL2836_E4000_EXTRA_MODULE        Rtl2836E4000;
		RTL2836B_DTMB_E4000_EXTRA_MODULE  Rtl2836bDtmbE4000;
		RTL2836B_DTMB_E4005_EXTRA_MODULE  Rtl2836bDtmbE4005;
		RTL2836B_DTMB_FC0012_EXTRA_MODULE Rtl2836bDtmbFc0012;
		RTL2836B_DTMB_FC0013B_EXTRA_MODULE Rtl2836bDtmbFc0013b;
	}
	Extra;


	// Modules
	BASE_INTERFACE_MODULE *pBaseInterface;						///<   Base interface module pointer
	BASE_INTERFACE_MODULE BaseInterfaceModuleMemory;			///<   Base interface module memory

	I2C_BRIDGE_MODULE *pI2cBridge;								///<   I2C bridge module pointer
	I2C_BRIDGE_MODULE I2cBridgeModuleMemory;					///<   I2C bridge module memory

	TUNER_MODULE *pTuner;										///<   Tuner module pointer
	TUNER_MODULE TunerModuleMemory;								///<   Tuner module memory

	DTMB_DEMOD_MODULE *pDemod;									///<   DTMB demod module pointer
	DTMB_DEMOD_MODULE DtmbDemodModuleMemory;					///<   DTMB demod module memory


	// NIM manipulating functions
	DTMB_NIM_FP_GET_NIM_TYPE          GetNimType;
	DTMB_NIM_FP_INITIALIZE            Initialize;
	DTMB_NIM_FP_SET_PARAMETERS        SetParameters;
	DTMB_NIM_FP_GET_PARAMETERS        GetParameters;
	DTMB_NIM_FP_IS_SIGNAL_PRESENT     IsSignalPresent;
	DTMB_NIM_FP_IS_SIGNAL_LOCKED      IsSignalLocked;
	DTMB_NIM_FP_GET_SIGNAL_STRENGTH   GetSignalStrength;
	DTMB_NIM_FP_GET_SIGNAL_QUALITY    GetSignalQuality;
	DTMB_NIM_FP_GET_BER               GetBer;
	DTMB_NIM_FP_GET_PER               GetPer;
	DTMB_NIM_FP_GET_SNR_DB            GetSnrDb;
	DTMB_NIM_FP_GET_TR_OFFSET_PPM     GetTrOffsetPpm;
	DTMB_NIM_FP_GET_CR_OFFSET_HZ      GetCrOffsetHz;
	DTMB_NIM_FP_GET_SIGNAL_INFO       GetSignalInfo;
	DTMB_NIM_FP_UPDATE_FUNCTION       UpdateFunction;
};







// DTMB NIM default manipulaing functions
void
dtmb_nim_default_GetNimType(
	DTMB_NIM_MODULE *pNim,
	int *pNimType
	);

int
dtmb_nim_default_SetParameters(
	DTMB_NIM_MODULE *pNim,
	unsigned long RfFreqHz
	);

int
dtmb_nim_default_GetParameters(
	DTMB_NIM_MODULE *pNim,
	unsigned long *pRfFreqHz
	);

int
dtmb_nim_default_IsSignalPresent(
	DTMB_NIM_MODULE *pNim,
	int *pAnswer
	);

int
dtmb_nim_default_IsSignalLocked(
	DTMB_NIM_MODULE *pNim,
	int *pAnswer
	);

int
dtmb_nim_default_GetSignalStrength(
	DTMB_NIM_MODULE *pNim,
	unsigned long *pSignalStrength
	);

int
dtmb_nim_default_GetSignalQuality(
	DTMB_NIM_MODULE *pNim,
	unsigned long *pSignalQuality
	);

int
dtmb_nim_default_GetBer(
	DTMB_NIM_MODULE *pNim,
	unsigned long *pBerNum,
	unsigned long *pBerDen
	);

int
dtmb_nim_default_GetPer(
	DTMB_NIM_MODULE *pNim,
	unsigned long *pPerNum,
	unsigned long *pPerDen
	);

int
dtmb_nim_default_GetSnrDb(
	DTMB_NIM_MODULE *pNim,
	long *pSnrDbNum,
	long *pSnrDbDen
	);

int
dtmb_nim_default_GetTrOffsetPpm(
	DTMB_NIM_MODULE *pNim,
	long *pTrOffsetPpm
	);

int
dtmb_nim_default_GetCrOffsetHz(
	DTMB_NIM_MODULE *pNim,
	long *pCrOffsetHz
	);

int
dtmb_nim_default_GetSignalInfo(
	DTMB_NIM_MODULE *pNim,
	int *pCarrierMode,
	int *pPnMode,
	int *pQamMode,
	int *pCodeRateMode,
	int *pTimeInterleaverMode
	);

int
dtmb_nim_default_UpdateFunction(
	DTMB_NIM_MODULE *pNim
	);







#endif
