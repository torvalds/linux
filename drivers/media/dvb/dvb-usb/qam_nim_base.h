#ifndef __QAM_NIM_BASE_H
#define __QAM_NIM_BASE_H

/**

@file

@brief   QAM NIM base module definition

QAM NIM base module definitions contains NIM module structure, NIM funciton pointers, NIM definitions, and NIM default
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
	QAM_NIM_MODULE *pNim;
	QAM_NIM_MODULE QamNimModuleMemory;
	DEMODX_EXTRA_MODULE DemodxExtraModuleMemory;
	TUNERY_EXTRA_MODULE TuneryExtraModuleMemory;

	unsigned long RfFreqHz;
	int QamMode;
	unsigned long SymbolRateHz;
	int AlphaMode;

	int Answer;
	unsigned long SignalStrength, SignalQuality;
	unsigned long BerNum, BerDen, PerNum, PerDen;
	double Ber, Per;
	unsigned long SnrDbNum, SnrDbDen;
	double SnrDb;
	long TrOffsetPpm, CrOffsetHz;



	// Build Demod-X Tuner-Y NIM module.
	BuildDemodxTuneryModule(
		&pNim,
		&QamNimModuleMemory,

		9,								// Maximum I2C reading byte number is 9.
		8,								// Maximum I2C writing byte number is 8.
		CustomI2cRead,					// Employ CustomI2cRead() as basic I2C reading function.
		CustomI2cWrite,					// Employ CustomI2cWrite() as basic I2C writing function.
		CustomWaitMs,					// Employ CustomWaitMs() as basic waiting function.

		&DemodxExtraModuleMemory,		// Employ Demod-X extra module.
		0x44,							// The Demod-X I2C device address is 0x44 in 8-bit format.
		CRYSTAL_FREQ_28800000HZ,		// The Demod-X crystal frequency is 28.8 MHz.
		TS_INTERFACE_SERIAL,			// The Demod-X TS interface mode is serial.
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

	// Set NIM parameters. (RF frequency, QAM mode, symbol rate, alpha mode)
	// Note: In the example:
	//       1. RF frequency is 738 MHz.
	//       2. QAM is 64.
	//       3. Symbol rate is 6.952 MHz.
	//       4. Alpha is 0.15.
	RfFreqHz     = 738000000;
	QamMode      = QAM_QAM_64;
	SymbolRateHz = 6952000;
	AlphaMode    = QAM_ALPHA_0P15;
	pNim->SetParameters(pNim, RfFreqHz, QamMode, SymbolRateHz, AlphaMode);



	// Wait 1 second for demod convergence.





	// ==== Get NIM information =====

	// Get NIM parameters. (RF frequency, QAM mode, symbol rate, alpha mode)
	pNim->GetParameters(pNim, &RfFreqHz, &QamMode, &SymbolRateHz, &AlphaMode);


	// Get signal present status.
	// Note: 1. The argument Answer is YES when the NIM module has found QAM signal in the RF channel.
	//       2. The argument Answer is NO when the NIM module does not find QAM signal in the RF channel.
	// Recommendation: Use the IsSignalPresent() function for channel scan.
	pNim->IsSignalPresent(pNim, &Answer);

	// Get signal lock status.
	// Note: 1. The argument Answer is YES when the NIM module has locked QAM signal in the RF channel.
	//          At the same time, the NIM module sends TS packets through TS interface hardware pins.
	//       2. The argument Answer is NO when the NIM module does not lock QAM signal in the RF channel.
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


	// Get BER and PER.
	// Note: Test packet number = pow(2, (2 * 4 + 4)) = 4096
	//       Maximum wait time  = 1000 ms = 1 second
	pNim->GetErrorRate(pNim, 4, 1000, &BerNum, &BerDen, &PerNum, &PerDen);
	Ber = (double)BerNum / (double)BerDen;
	Per = (double)PerNum / (double)PerDen;

	// Get SNR in dB.
	pNim->GetSnrDb(pNim, &SnrDbNum, &SnrDbDen);
	SnrDb = (double)SnrDbNum / (double)SnrDbDen;


	// Get TR offset (symbol timing offset) in ppm.
	pNim->GetTrOffsetPpm(pNim, &TrOffsetPpm);

	// Get CR offset (RF frequency offset) in Hz.
	pNim->GetCrOffsetHz(pNim, &CrOffsetHz);



	return 0;
}


@endcode

*/


#include "foundation.h"
#include "tuner_base.h"
#include "qam_demod_base.h"





// Definitions
#define DEFAULT_QAM_NIM_SINGAL_PRESENT_CHECK_TIMES_MAX		1
#define DEFAULT_QAM_NIM_SINGAL_LOCK_CHECK_TIMES_MAX			1





/// QAM NIM module pre-definition
typedef struct QAM_NIM_MODULE_TAG QAM_NIM_MODULE;





/**

@brief   QAM demod type getting function pointer

One can use QAM_NIM_FP_GET_NIM_TYPE() to get QAM NIM type.


@param [in]    pNim       The NIM module pointer
@param [out]   pNimType   Pointer to an allocated memory for storing NIM type


@note
	-# NIM building function will set QAM_NIM_FP_GET_NIM_TYPE() with the corresponding function.


@see   MODULE_TYPE

*/
typedef void
(*QAM_NIM_FP_GET_NIM_TYPE)(
	QAM_NIM_MODULE *pNim,
	int *pNimType
	);





/**

@brief   QAM NIM initializing function pointer

One can use QAM_NIM_FP_INITIALIZE() to initialie QAM NIM.


@param [in]   pNim   The NIM module pointer


@retval   FUNCTION_SUCCESS   Initialize NIM successfully.
@retval   FUNCTION_ERROR     Initialize NIM unsuccessfully.


@note
	-# NIM building function will set QAM_NIM_FP_INITIALIZE() with the corresponding function.

*/
typedef int
(*QAM_NIM_FP_INITIALIZE)(
	QAM_NIM_MODULE *pNim
	);





/**

@brief   QAM NIM parameter setting function pointer

One can use QAM_NIM_FP_SET_PARAMETERS() to set QAM NIM parameters.


@param [in]   pNim           The NIM module pointer
@param [in]   RfFreqHz       RF frequency in Hz for setting
@param [in]   QamMode        QAM mode for setting
@param [in]   SymbolRateHz   Symbol rate in Hz for setting
@param [in]   AlphaMode      Alpha mode for setting


@retval   FUNCTION_SUCCESS   Set NIM parameters successfully.
@retval   FUNCTION_ERROR     Set NIM parameters unsuccessfully.


@note
	-# NIM building function will set QAM_NIM_FP_SET_PARAMETERS() with the corresponding function.


@see   QAM_QAM_MODE, QAM_ALPHA_MODE

*/
typedef int
(*QAM_NIM_FP_SET_PARAMETERS)(
	QAM_NIM_MODULE *pNim,
	unsigned long RfFreqHz,
	int QamMode,
	unsigned long SymbolRateHz,
	int AlphaMode
	);





/**

@brief   QAM NIM parameter getting function pointer

One can use QAM_NIM_FP_GET_PARAMETERS() to get QAM NIM parameters.


@param [in]    pNim            The NIM module pointer
@param [out]   pRfFreqHz       Pointer to an allocated memory for storing NIM RF frequency in Hz
@param [out]   pQamMode        Pointer to an allocated memory for storing NIM QAM mode
@param [out]   pSymbolRateHz   Pointer to an allocated memory for storing NIM symbol rate in Hz
@param [out]   pAlphaMode      Pointer to an allocated memory for storing NIM alpha mode


@retval   FUNCTION_SUCCESS   Get NIM parameters successfully.
@retval   FUNCTION_ERROR     Get NIM parameters unsuccessfully.


@note
	-# NIM building function will set QAM_NIM_FP_GET_PARAMETERS() with the corresponding function.


@see   QAM_QAM_MODE, QAM_ALPHA_MODE

*/
typedef int
(*QAM_NIM_FP_GET_PARAMETERS)(
	QAM_NIM_MODULE *pNim,
	unsigned long *pRfFreqHz,
	int *pQamMode,
	unsigned long *pSymbolRateHz,
	int *pAlphaMode
	);





/**

@brief   QAM NIM signal present asking function pointer

One can use QAM_NIM_FP_IS_SIGNAL_PRESENT() to ask QAM NIM if signal is present.


@param [in]    pNim      The NIM module pointer
@param [out]   pAnswer   Pointer to an allocated memory for storing answer


@retval   FUNCTION_SUCCESS   Perform signal present asking to NIM successfully.
@retval   FUNCTION_ERROR     Perform signal present asking to NIM unsuccessfully.


@note
	-# NIM building function will set QAM_NIM_FP_IS_SIGNAL_PRESENT() with the corresponding function.

*/
typedef int
(*QAM_NIM_FP_IS_SIGNAL_PRESENT)(
	QAM_NIM_MODULE *pNim,
	int *pAnswer
	);





/**

@brief   QAM NIM signal lock asking function pointer

One can use QAM_NIM_FP_IS_SIGNAL_LOCKED() to ask QAM NIM if signal is locked.


@param [in]    pNim      The NIM module pointer
@param [out]   pAnswer   Pointer to an allocated memory for storing answer


@retval   FUNCTION_SUCCESS   Perform signal lock asking to NIM successfully.
@retval   FUNCTION_ERROR     Perform signal lock asking to NIM unsuccessfully.


@note
	-# NIM building function will set QAM_NIM_FP_IS_SIGNAL_LOCKED() with the corresponding function.

*/
typedef int
(*QAM_NIM_FP_IS_SIGNAL_LOCKED)(
	QAM_NIM_MODULE *pNim,
	int *pAnswer
	);





/**

@brief   QAM NIM signal strength getting function pointer

One can use QAM_NIM_FP_GET_SIGNAL_STRENGTH() to get signal strength.


@param [in]    pNim              The NIM module pointer
@param [out]   pSignalStrength   Pointer to an allocated memory for storing signal strength (value = 0 ~ 100)


@retval   FUNCTION_SUCCESS   Get NIM signal strength successfully.
@retval   FUNCTION_ERROR     Get NIM signal strength unsuccessfully.


@note
	-# NIM building function will set QAM_NIM_FP_GET_SIGNAL_STRENGTH() with the corresponding function.

*/
typedef int
(*QAM_NIM_FP_GET_SIGNAL_STRENGTH)(
	QAM_NIM_MODULE *pNim,
	unsigned long *pSignalStrength
	);





/**

@brief   QAM NIM signal quality getting function pointer

One can use QAM_NIM_FP_GET_SIGNAL_QUALITY() to get signal quality.


@param [in]    pNim             The NIM module pointer
@param [out]   pSignalQuality   Pointer to an allocated memory for storing signal quality (value = 0 ~ 100)


@retval   FUNCTION_SUCCESS   Get NIM signal quality successfully.
@retval   FUNCTION_ERROR     Get NIM signal quality unsuccessfully.


@note
	-# NIM building function will set QAM_NIM_FP_GET_SIGNAL_QUALITY() with the corresponding function.

*/
typedef int
(*QAM_NIM_FP_GET_SIGNAL_QUALITY)(
	QAM_NIM_MODULE *pNim,
	unsigned long *pSignalQuality
	);





/**

@brief   QAM NIM error rate value getting function pointer

One can use QAM_NIM_FP_GET_ERROR_RATE() to get error rate value.


@param [in]    pNim            The NIM module pointer
@param [in]    TestVolume      Test volume for setting
@param [in]    WaitTimeMsMax   Maximum waiting time in ms
@param [out]   pBerNum         Pointer to an allocated memory for storing BER numerator
@param [out]   pBerDen         Pointer to an allocated memory for storing BER denominator
@param [out]   pPerNum         Pointer to an allocated memory for storing PER numerator
@param [out]   pPerDen         Pointer to an allocated memory for storing PER denominator


@retval   FUNCTION_SUCCESS   Get NIM error rate value successfully.
@retval   FUNCTION_ERROR     Get NIM error rate value unsuccessfully.


@note
	-# NIM building function will set QAM_NIM_FP_GET_ERROR_RATE() with the corresponding function.
	-# The error test packet number is pow(2, (2 * TestVolume + 4)).

*/
typedef int
(*QAM_NIM_FP_GET_ERROR_RATE)(
	QAM_NIM_MODULE *pNim,
	unsigned long TestVolume,
	unsigned int WaitTimeMsMax,
	unsigned long *pBerNum,
	unsigned long *pBerDen,
	unsigned long *pPerNum,
	unsigned long *pPerDen
	);





/**

@brief   QAM NIM SNR getting function pointer

One can use QAM_NIM_FP_GET_SNR_DB() to get SNR in dB.


@param [in]    pNim        The NIM module pointer
@param [out]   pSnrDbNum   Pointer to an allocated memory for storing SNR dB numerator
@param [out]   pSnrDbDen   Pointer to an allocated memory for storing SNR dB denominator


@retval   FUNCTION_SUCCESS   Get NIM SNR successfully.
@retval   FUNCTION_ERROR     Get NIM SNR unsuccessfully.


@note
	-# NIM building function will set QAM_NIM_FP_GET_SNR_DB() with the corresponding function.

*/
typedef int
(*QAM_NIM_FP_GET_SNR_DB)(
	QAM_NIM_MODULE *pNim,
	long *pSnrDbNum,
	long *pSnrDbDen
	);





/**

@brief   QAM NIM TR offset getting function pointer

One can use QAM_NIM_FP_GET_TR_OFFSET_PPM() to get TR offset in ppm.


@param [in]    pNim           The NIM module pointer
@param [out]   pTrOffsetPpm   Pointer to an allocated memory for storing TR offset in ppm


@retval   FUNCTION_SUCCESS   Get NIM TR offset successfully.
@retval   FUNCTION_ERROR     Get NIM TR offset unsuccessfully.


@note
	-# NIM building function will set QAM_NIM_FP_GET_TR_OFFSET_PPM() with the corresponding function.

*/
typedef int
(*QAM_NIM_FP_GET_TR_OFFSET_PPM)(
	QAM_NIM_MODULE *pNim,
	long *pTrOffsetPpm
	);





/**

@brief   QAM NIM CR offset getting function pointer

One can use QAM_NIM_FP_GET_CR_OFFSET_HZ() to get CR offset in Hz.


@param [in]    pNim          The NIM module pointer
@param [out]   pCrOffsetHz   Pointer to an allocated memory for storing CR offset in Hz


@retval   FUNCTION_SUCCESS   Get NIM CR offset successfully.
@retval   FUNCTION_ERROR     Get NIM CR offset unsuccessfully.


@note
	-# NIM building function will set QAM_NIM_FP_GET_CR_OFFSET_HZ() with the corresponding function.

*/
typedef int
(*QAM_NIM_FP_GET_CR_OFFSET_HZ)(
	QAM_NIM_MODULE *pNim,
	long *pCrOffsetHz
	);





/**

@brief   QAM NIM updating function pointer

One can use QAM_NIM_FP_UPDATE_FUNCTION() to update NIM register setting.


@param [in]   pNim   The NIM module pointer


@retval   FUNCTION_SUCCESS   Update NIM setting successfully.
@retval   FUNCTION_ERROR     Update NIM setting unsuccessfully.


@note
	-# NIM building function will set QAM_NIM_FP_UPDATE_FUNCTION() with the corresponding function.



@par Example:
@code


#include "nim_demodx_tunery.h"


int main(void)
{
	QAM_NIM_MODULE *pNim;
	QAM_NIM_MODULE QamNimModuleMemory;
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
(*QAM_NIM_FP_UPDATE_FUNCTION)(
	QAM_NIM_MODULE *pNim
	);





// RTL2840 MT2062 extra module
typedef struct RTL2840_MT2062_EXTRA_MODULE_TAG RTL2840_MT2062_EXTRA_MODULE;
struct RTL2840_MT2062_EXTRA_MODULE_TAG
{
	// Extra variables
	unsigned long IfFreqHz;
};





// RTL2840 MT2063 extra module
typedef struct RTL2840_MT2063_EXTRA_MODULE_TAG RTL2840_MT2063_EXTRA_MODULE;
struct RTL2840_MT2063_EXTRA_MODULE_TAG
{
	// Extra variables
	unsigned long IfFreqHz;
};





// RTD2840B QAM MT2062 extra module
typedef struct RTD2840B_QAM_MT2062_EXTRA_MODULE_TAG RTD2840B_QAM_MT2062_EXTRA_MODULE;
struct RTD2840B_QAM_MT2062_EXTRA_MODULE_TAG
{
	// Extra variables
	unsigned long IfFreqHz;
};





// RTL2836B DVBC FC0013B extra module
typedef struct RTL2836B_DVBC_FC0013B_EXTRA_MODULE_TAG RTL2836B_DVBC_FC0013B_EXTRA_MODULE;
struct RTL2836B_DVBC_FC0013B_EXTRA_MODULE_TAG
{
	// Extra variables
	unsigned long LnaUpdateWaitTimeMax;
	unsigned long LnaUpdateWaitTime;
	unsigned long RssiRCalOn;
};





/// QAM NIM module structure
struct QAM_NIM_MODULE_TAG
{
	// Private variables
	int NimType;
	int EnhancementMode;
	int ConfigMode;

	union														///<   NIM extra module used by driving module
	{
		RTL2840_MT2062_EXTRA_MODULE Rtl2840Mt2062;
		RTL2840_MT2063_EXTRA_MODULE Rtl2840Mt2063;
		RTD2840B_QAM_MT2062_EXTRA_MODULE  Rtd2840bQamMt2062;
		RTL2836B_DVBC_FC0013B_EXTRA_MODULE  Rtl2836bDvbcFc0013b;
	}
	Extra;


	// Modules
	BASE_INTERFACE_MODULE *pBaseInterface;						///<   Base interface module pointer
	BASE_INTERFACE_MODULE BaseInterfaceModuleMemory;			///<   Base interface module memory

	I2C_BRIDGE_MODULE *pI2cBridge;								///<   I2C bridge module pointer
	I2C_BRIDGE_MODULE I2cBridgeModuleMemory;					///<   I2C bridge module memory

	TUNER_MODULE *pTuner;										///<   Tuner module pointer
	TUNER_MODULE TunerModuleMemory;								///<   Tuner module memory

	QAM_DEMOD_MODULE *pDemod;									///<   QAM demod module pointer
	QAM_DEMOD_MODULE QamDemodModuleMemory;						///<   QAM demod module memory


	// NIM manipulating functions
	QAM_NIM_FP_GET_NIM_TYPE          GetNimType;
	QAM_NIM_FP_INITIALIZE            Initialize;
	QAM_NIM_FP_SET_PARAMETERS        SetParameters;
	QAM_NIM_FP_GET_PARAMETERS        GetParameters;
	QAM_NIM_FP_IS_SIGNAL_PRESENT     IsSignalPresent;
	QAM_NIM_FP_IS_SIGNAL_LOCKED      IsSignalLocked;
	QAM_NIM_FP_GET_SIGNAL_STRENGTH   GetSignalStrength;
	QAM_NIM_FP_GET_SIGNAL_QUALITY    GetSignalQuality;
	QAM_NIM_FP_GET_ERROR_RATE        GetErrorRate;
	QAM_NIM_FP_GET_SNR_DB            GetSnrDb;
	QAM_NIM_FP_GET_TR_OFFSET_PPM     GetTrOffsetPpm;
	QAM_NIM_FP_GET_CR_OFFSET_HZ      GetCrOffsetHz;
	QAM_NIM_FP_UPDATE_FUNCTION       UpdateFunction;
};







// QAM NIM default manipulaing functions
void
qam_nim_default_GetNimType(
	QAM_NIM_MODULE *pNim,
	int *pNimType
	);

int
qam_nim_default_SetParameters(
	QAM_NIM_MODULE *pNim,
	unsigned long RfFreqHz,
	int QamMode,
	unsigned long SymbolRateHz,
	int AlphaMode
	);

int
qam_nim_default_GetParameters(
	QAM_NIM_MODULE *pNim,
	unsigned long *pRfFreqHz,
	int *pQamMode,
	unsigned long *pSymbolRateHz,
	int *pAlphaMode
	);

int
qam_nim_default_IsSignalPresent(
	QAM_NIM_MODULE *pNim,
	int *pAnswer
	);

int
qam_nim_default_IsSignalLocked(
	QAM_NIM_MODULE *pNim,
	int *pAnswer
	);

int
qam_nim_default_GetSignalStrength(
	QAM_NIM_MODULE *pNim,
	unsigned long *pSignalStrength
	);

int
qam_nim_default_GetSignalQuality(
	QAM_NIM_MODULE *pNim,
	unsigned long *pSignalQuality
	);

int
qam_nim_default_GetErrorRate(
	QAM_NIM_MODULE *pNim,
	unsigned long TestVolume,
	unsigned int WaitTimeMsMax,
	unsigned long *pBerNum,
	unsigned long *pBerDen,
	unsigned long *pPerNum,
	unsigned long *pPerDen
	);

int
qam_nim_default_GetSnrDb(
	QAM_NIM_MODULE *pNim,
	long *pSnrDbNum,
	long *pSnrDbDen
	);

int
qam_nim_default_GetTrOffsetPpm(
	QAM_NIM_MODULE *pNim,
	long *pTrOffsetPpm
	);

int
qam_nim_default_GetCrOffsetHz(
	QAM_NIM_MODULE *pNim,
	long *pCrOffsetHz
	);

int
qam_nim_default_UpdateFunction(
	QAM_NIM_MODULE *pNim
	);







#endif
