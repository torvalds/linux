#ifndef __DVBT_DEMOD_BASE_H
#define __DVBT_DEMOD_BASE_H

/**

@file

@brief   DVB-T demod base module definition

DVB-T demod base module definitions contains demod module structure, demod funciton pointers, and demod definitions.



@par Example:
@code


#include "demod_xxx.h"



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
	BASE_INTERFACE_MODULE *pBaseInterface;
	BASE_INTERFACE_MODULE BaseInterfaceModuleMemory;

	DVBT_DEMOD_MODULE *pDemod;
	DVBT_DEMOD_MODULE DvbtDemodModuleMemory;

	I2C_BRIDGE_MODULE I2cBridgeModuleMemory;

	int BandwidthMode;
	unsigned long IfFreqHz;
	int SpectrumMode;

	int DemodType;
	unsigned char DeviceAddr;
	unsigned long CrystalFreqHz;

	long RfAgc, IfAgc;
	unsigned char DiAgc;

	int Answer;
	long TrOffsetPpm, CrOffsetHz;
	unsigned long BerNum, BerDen;
	double Ber;
	long SnrDbNum, SnrDbDen;
	double SnrDb;
	unsigned long SignalStrength, SignalQuality;

	int Constellation;
	int Hierarchy;
	int CodeRateLp;
	int CodeRateHp;
	int GuardInterval;
	int FftMode;



	// Build base interface module.
	BuildBaseInterface(
		&pBaseInterface,
		&BaseInterfaceModuleMemory,
		9,								// Set maximum I2C reading byte number with 9.
		8,								// Set maximum I2C writing byte number with 8.
		CustomI2cRead,					// Employ CustomI2cRead() as basic I2C reading function.
		CustomI2cWrite,					// Employ CustomI2cWrite() as basic I2C writing function.
		CustomWaitMs					// Employ CustomWaitMs() as basic waiting function.
		);


	// Build DVB-T demod XXX module.
	BuildXxxModule(
		&pDemod,
		&DvbtDemodModuleMemory,
		&BaseInterfaceModuleMemory,
		&I2cBridgeModuleMemory,
		0x20,							// Demod I2C device address is 0x20 in 8-bit format.
		CRYSTAL_FREQ_28800000HZ,		// Demod crystal frequency is 28.8 MHz.
		TS_INTERFACE_SERIAL,			// Demod TS interface mode is serial.
		...								// Other arguments by each demod module
		);





	// ==== Initialize DVB-T demod and set its parameters =====

	// Initialize demod.
	pDemod->Initialize(pDemod);


	// Set demod parameters. (bandwidth mode, IF frequency, spectrum mode)
	// Note: In the example:
	//       1. Bandwidth mode is 8 MHz.
	//       2. IF frequency is 36.125 MHz.
	//       3. Spectrum mode is SPECTRUM_INVERSE.
	BandwidthMode = DVBT_BANDWIDTH_8MHZ;
	IfFreqHz      = IF_FREQ_36125000HZ;
	SpectrumMode  = SPECTRUM_INVERSE;

	pDemod->SetBandwidthMode(pDemod, BandwidthMode);
	pDemod->SetIfFreqHz(pDemod,      IfFreqHz);
	pDemod->SetSpectrumMode(pDemod,  SpectrumMode);


	// Need to set tuner before demod software reset.
	// The order to set demod and tuner is not important.
	// Note: One can use "pDemod->SetRegBitsWithPage(pDemod, DVBT_IIC_REPEAT, 0x1);"
	//       for tuner I2C command forwarding.


	// Reset demod by software reset.
	pDemod->SoftwareReset(pDemod);


	// Wait maximum 1000 ms for demod converge.
	for(i = 0; i < 25; i++)
	{
		// Wait 40 ms.
		pBaseInterface->WaitMs(pBaseInterface, 40);

		// Check signal lock status.
		// Note: If Answer is YES, signal is locked.
		//       If Answer is NO, signal is not locked.
		pDemod->IsSignalLocked(pDemod, &Answer);

		if(Answer == YES)
		{
			// Signal is locked.
			break;
		}
	}





	// ==== Get DVB-T demod information =====

	// Get demod type.
	// Note: One can find demod type in MODULE_TYPE enumeration.
	pDemod->GetDemodType(pDemod, &DemodType);

	// Get demod I2C device address.
	pDemod->GetDeviceAddr(pDemod, &DeviceAddr);

	// Get demod crystal frequency in Hz.
	pDemod->GetCrystalFreqHz(pDemod, &CrystalFreqHz);


	// Ask demod if it is connected to I2C bus.
	// Note: If Answer is YES, demod is connected to I2C bus.
	//       If Answer is NO, demod is not connected to I2C bus.
	pDemod->IsConnectedToI2c(pDemod, &Answer);


	// Get demod parameters. (bandwidth mode, IF frequency, spectrum mode)
	pDemod->GetBandwidthMode(pDemod, &BandwidthMode);
	pDemod->GetIfFreqHz(pDemod,      &IfFreqHz);
	pDemod->GetSpectrumMode(pDemod,  &SpectrumMode);


	// Get demod AGC value.
	// Note: The range of RF AGC and IF AGC value is -8192 ~ 8191.
	//       The range of digital AGC value is 0 ~ 255.
	pDemod->GetRfAgc(pDemod, &RfAgc);
	pDemod->GetIfAgc(pDemod, &IfAgc);
	pDemod->GetDiAgc(pDemod, &DiAgc);


	// Get demod lock status.
	// Note: If Answer is YES, it is locked.
	//       If Answer is NO, it is not locked.
	pDemod->IsTpsLocked(pDemod,    &Answer);
	pDemod->IsSignalLocked(pDemod, &Answer);


	// Get TR offset (symbol timing offset) in ppm.
	pDemod->GetTrOffsetPpm(pDemod, &TrOffsetPpm);

	// Get CR offset (RF frequency offset) in Hz.
	pDemod->GetCrOffsetHz(pDemod, &CrOffsetHz);


	// Get BER.
	pDemod->GetBer(pDemod, &BerNum, &BerDen);
	Ber = (double)BerNum / (double)BerDen;

	// Get SNR in dB.
	pDemod->GetSnrDb(pDemod, &SnrDbNum, &SnrDbDen);
	SnrDb = (double)SnrDbNum / (double)SnrDbDen;


	// Get signal strength.
	// Note: 1. The range of SignalStrength is 0~100.
	//       2. Need to map SignalStrength value to UI signal strength bar manually.
	pDemod->GetSignalStrength(pDemod, &SignalStrength);

	// Get signal quality.
	// Note: 1. The range of SignalQuality is 0~100.
	//       2. Need to map SignalQuality value to UI signal quality bar manually.
	pDemod->GetSignalQuality(pDemod, &SignalQuality);


	// Get TPS information.
	// Note: One can find TPS information definitions in the enumerations as follows:
	//       1. DVBT_CONSTELLATION_MODE
	//       2. DVBT_HIERARCHY_MODE
	//       3. DVBT_CODE_RATE_MODE (for low-priority and high-priority code rate)
	//       4. DVBT_GUARD_INTERVAL_MODE
	//       5. DVBT_FFT_MODE_MODE
	pDemod->GetConstellation(pDemod, &Constellation);
	pDemod->GetHierarchy(pDemod,     &Hierarchy);
	pDemod->GetCodeRateLp(pDemod,    &CodeRateLp);
	pDemod->GetCodeRateHp(pDemod,    &CodeRateHp);
	pDemod->GetGuardInterval(pDemod, &GuardInterval);
	pDemod->GetFftMode(pDemod,       &FftMode);



	return 0;
}


@endcode

*/


#include "foundation.h"





// Definitions

// Page register address
#define DVBT_DEMOD_PAGE_REG_ADDR		0x00


// Bandwidth modes
#define DVBT_BANDWIDTH_NONE			-1
enum DVBT_BANDWIDTH_MODE
{
	DVBT_BANDWIDTH_6MHZ,
	DVBT_BANDWIDTH_7MHZ,
	DVBT_BANDWIDTH_8MHZ,
};
#define DVBT_BANDWIDTH_MODE_NUM		3


// Constellation
enum DVBT_CONSTELLATION_MODE
{
	DVBT_CONSTELLATION_QPSK,
	DVBT_CONSTELLATION_16QAM,
	DVBT_CONSTELLATION_64QAM,
};
#define DVBT_CONSTELLATION_NUM		3


// Hierarchy
enum DVBT_HIERARCHY_MODE
{
	DVBT_HIERARCHY_NONE,
	DVBT_HIERARCHY_ALPHA_1,
	DVBT_HIERARCHY_ALPHA_2,
	DVBT_HIERARCHY_ALPHA_4,
};
#define DVBT_HIERARCHY_NUM			4


// Code rate
enum DVBT_CODE_RATE_MODE
{
	DVBT_CODE_RATE_1_OVER_2,
	DVBT_CODE_RATE_2_OVER_3,
	DVBT_CODE_RATE_3_OVER_4,
	DVBT_CODE_RATE_5_OVER_6,
	DVBT_CODE_RATE_7_OVER_8,
};
#define DVBT_CODE_RATE_NUM			5


// Guard interval
enum DVBT_GUARD_INTERVAL_MODE
{
	DVBT_GUARD_INTERVAL_1_OVER_32,
	DVBT_GUARD_INTERVAL_1_OVER_16,
	DVBT_GUARD_INTERVAL_1_OVER_8,
	DVBT_GUARD_INTERVAL_1_OVER_4,
};
#define DVBT_GUARD_INTERVAL_NUM		4


// FFT mode
enum DVBT_FFT_MODE_MODE
{
	DVBT_FFT_MODE_2K,
	DVBT_FFT_MODE_8K,
};
#define DVBT_FFT_MODE_NUM			2





// Register entry definitions

// Register entry
typedef struct
{
	int IsAvailable;
	unsigned long PageNo;
	unsigned char RegStartAddr;
	unsigned char Msb;
	unsigned char Lsb;
}
DVBT_REG_ENTRY;



// Primary register entry
typedef struct
{
	int RegBitName;
	unsigned long PageNo;
	unsigned char RegStartAddr;
	unsigned char Msb;
	unsigned char Lsb;
}
DVBT_PRIMARY_REG_ENTRY;





// Register table dependence

// Demod register bit names
enum DVBT_REG_BIT_NAME
{
	// Software reset register
	DVBT_SOFT_RST,

	// Tuner I2C forwording register
	DVBT_IIC_REPEAT,


	// Registers for initializing
	DVBT_TR_WAIT_MIN_8K,
	DVBT_RSD_BER_FAIL_VAL,
	DVBT_EN_BK_TRK,
	DVBT_REG_PI,

	DVBT_REG_PFREQ_1_0,				// For RTL2830 only
	DVBT_PD_DA8,					// For RTL2830 only
	DVBT_LOCK_TH,					// For RTL2830 only
	DVBT_BER_PASS_SCAL,				// For RTL2830 only
	DVBT_CE_FFSM_BYPASS,			// For RTL2830 only
	DVBT_ALPHAIIR_N,				// For RTL2830 only
	DVBT_ALPHAIIR_DIF,				// For RTL2830 only
	DVBT_EN_TRK_SPAN,				// For RTL2830 only
	DVBT_LOCK_TH_LEN,				// For RTL2830 only
	DVBT_CCI_THRE,					// For RTL2830 only
	DVBT_CCI_MON_SCAL,				// For RTL2830 only
	DVBT_CCI_M0,					// For RTL2830 only
	DVBT_CCI_M1,					// For RTL2830 only
	DVBT_CCI_M2,					// For RTL2830 only
	DVBT_CCI_M3,					// For RTL2830 only
	DVBT_SPEC_INIT_0,				// For RTL2830 only
	DVBT_SPEC_INIT_1,				// For RTL2830 only
	DVBT_SPEC_INIT_2,				// For RTL2830 only

	DVBT_AD_EN_REG,					// For RTL2832 only
	DVBT_AD_EN_REG1,				// For RTL2832 only
	DVBT_EN_BBIN,					// For RTL2832 only
	DVBT_MGD_THD0,					// For RTL2832 only
	DVBT_MGD_THD1,					// For RTL2832 only
	DVBT_MGD_THD2,					// For RTL2832 only
	DVBT_MGD_THD3,					// For RTL2832 only
	DVBT_MGD_THD4,					// For RTL2832 only
	DVBT_MGD_THD5,					// For RTL2832 only
	DVBT_MGD_THD6,					// For RTL2832 only
	DVBT_MGD_THD7,					// For RTL2832 only
	DVBT_EN_CACQ_NOTCH,				// For RTL2832 only
	DVBT_AD_AV_REF,					// For RTL2832 only
	DVBT_PIP_ON,					// For RTL2832 only
	DVBT_SCALE1_B92,				// For RTL2832 only
	DVBT_SCALE1_B93,				// For RTL2832 only
	DVBT_SCALE1_BA7,				// For RTL2832 only
	DVBT_SCALE1_BA9,				// For RTL2832 only
	DVBT_SCALE1_BAA,				// For RTL2832 only
	DVBT_SCALE1_BAB,				// For RTL2832 only
	DVBT_SCALE1_BAC,				// For RTL2832 only
	DVBT_SCALE1_BB0,				// For RTL2832 only
	DVBT_SCALE1_BB1,				// For RTL2832 only
	DVBT_KB_P1,						// For RTL2832 only
	DVBT_KB_P2,						// For RTL2832 only
	DVBT_KB_P3,						// For RTL2832 only
	DVBT_OPT_ADC_IQ,				// For RTL2832 only
	DVBT_AD_AVI,					// For RTL2832 only
	DVBT_AD_AVQ,					// For RTL2832 only
	DVBT_K1_CR_STEP12,				// For RTL2832 only

	// Registers for initializing according to mode
	DVBT_TRK_KS_P2,
	DVBT_TRK_KS_I2,
	DVBT_TR_THD_SET2,
	DVBT_TRK_KC_P2,
	DVBT_TRK_KC_I2,
	DVBT_CR_THD_SET2,

	// Registers for IF setting
	DVBT_PSET_IFFREQ,
	DVBT_SPEC_INV,


	// Registers for bandwidth programming
	DVBT_BW_INDEX,					// For RTL2830 only

	DVBT_RSAMP_RATIO,				// For RTL2832 only
	DVBT_CFREQ_OFF_RATIO,			// For RTL2832 only


	// FSM stage register
	DVBT_FSM_STAGE,

	// TPS content registers
	DVBT_RX_CONSTEL,
	DVBT_RX_HIER,
	DVBT_RX_C_RATE_LP,
	DVBT_RX_C_RATE_HP,
	DVBT_GI_IDX,
	DVBT_FFT_MODE_IDX,
	
	// Performance measurement registers
	DVBT_RSD_BER_EST,
	DVBT_CE_EST_EVM,

	// AGC registers
	DVBT_RF_AGC_VAL,
	DVBT_IF_AGC_VAL,
	DVBT_DAGC_VAL,

	// TR offset and CR offset registers
	DVBT_SFREQ_OFF,
	DVBT_CFREQ_OFF,


	// AGC relative registers
	DVBT_POLAR_RF_AGC,
	DVBT_POLAR_IF_AGC,
	DVBT_AAGC_HOLD,
	DVBT_EN_RF_AGC,
	DVBT_EN_IF_AGC,
	DVBT_IF_AGC_MIN,
	DVBT_IF_AGC_MAX,
	DVBT_RF_AGC_MIN,
	DVBT_RF_AGC_MAX,
	DVBT_IF_AGC_MAN,
	DVBT_IF_AGC_MAN_VAL,
	DVBT_RF_AGC_MAN,
	DVBT_RF_AGC_MAN_VAL,
	DVBT_DAGC_TRG_VAL,

	DVBT_AGC_TARG_VAL,				// For RTL2830 only
	DVBT_LOOP_GAIN_3_0,				// For RTL2830 only
	DVBT_LOOP_GAIN_4,				// For RTL2830 only
	DVBT_VTOP,						// For RTL2830 only
	DVBT_KRF,						// For RTL2830 only

	DVBT_AGC_TARG_VAL_0,			// For RTL2832 only
	DVBT_AGC_TARG_VAL_8_1,			// For RTL2832 only
	DVBT_AAGC_LOOP_GAIN,			// For RTL2832 only
	DVBT_LOOP_GAIN2_3_0,			// For RTL2832 only
	DVBT_LOOP_GAIN2_4,				// For RTL2832 only
	DVBT_LOOP_GAIN3,				// For RTL2832 only
	DVBT_VTOP1,						// For RTL2832 only
	DVBT_VTOP2,						// For RTL2832 only
	DVBT_VTOP3,						// For RTL2832 only
	DVBT_KRF1,						// For RTL2832 only
	DVBT_KRF2,						// For RTL2832 only
	DVBT_KRF3,						// For RTL2832 only
	DVBT_KRF4,						// For RTL2832 only
	DVBT_EN_GI_PGA,					// For RTL2832 only
	DVBT_THD_LOCK_UP,				// For RTL2832 only
	DVBT_THD_LOCK_DW,				// For RTL2832 only
	DVBT_THD_UP1,					// For RTL2832 only
	DVBT_THD_DW1,					// For RTL2832 only
	DVBT_INTER_CNT_LEN,				// For RTL2832 only
	DVBT_GI_PGA_STATE,				// For RTL2832 only
	DVBT_EN_AGC_PGA,				// For RTL2832 only


	// TS interface registers
	DVBT_CKOUTPAR,
	DVBT_CKOUT_PWR,
	DVBT_SYNC_DUR,
	DVBT_ERR_DUR,
	DVBT_SYNC_LVL,
	DVBT_ERR_LVL,
	DVBT_VAL_LVL,
	DVBT_SERIAL,
	DVBT_SER_LSB,
	DVBT_CDIV_PH0,
	DVBT_CDIV_PH1,

	DVBT_MPEG_IO_OPT_2_2,			// For RTL2832 only
	DVBT_MPEG_IO_OPT_1_0,			// For RTL2832 only
	DVBT_CKOUTPAR_PIP,				// For RTL2832 only
	DVBT_CKOUT_PWR_PIP,				// For RTL2832 only
	DVBT_SYNC_LVL_PIP,				// For RTL2832 only
	DVBT_ERR_LVL_PIP,				// For RTL2832 only
	DVBT_VAL_LVL_PIP,				// For RTL2832 only
	DVBT_CKOUTPAR_PID,				// For RTL2832 only
	DVBT_CKOUT_PWR_PID,				// For RTL2832 only
	DVBT_SYNC_LVL_PID,				// For RTL2832 only
	DVBT_ERR_LVL_PID,				// For RTL2832 only
	DVBT_VAL_LVL_PID,				// For RTL2832 only


	// FSM state-holding register
	DVBT_SM_PASS,

	// Registers for function 2 (for RTL2830 only)
	DVBT_UPDATE_REG_2,

	// Registers for function 3 (for RTL2830 only)
	DVBT_BTHD_P3,
	DVBT_BTHD_D3,

	// Registers for function 4 (for RTL2830 only)
	DVBT_FUNC4_REG0,
	DVBT_FUNC4_REG1,
	DVBT_FUNC4_REG2,
	DVBT_FUNC4_REG3,
	DVBT_FUNC4_REG4,
	DVBT_FUNC4_REG5,
	DVBT_FUNC4_REG6,
	DVBT_FUNC4_REG7,
	DVBT_FUNC4_REG8,
	DVBT_FUNC4_REG9,
	DVBT_FUNC4_REG10,

	// Registers for functin 5 (for RTL2830 only)
	DVBT_FUNC5_REG0,
	DVBT_FUNC5_REG1,
	DVBT_FUNC5_REG2,
	DVBT_FUNC5_REG3,
	DVBT_FUNC5_REG4,
	DVBT_FUNC5_REG5,
	DVBT_FUNC5_REG6,
	DVBT_FUNC5_REG7,
	DVBT_FUNC5_REG8,
	DVBT_FUNC5_REG9,
	DVBT_FUNC5_REG10,
	DVBT_FUNC5_REG11,
	DVBT_FUNC5_REG12,
	DVBT_FUNC5_REG13,
	DVBT_FUNC5_REG14,
	DVBT_FUNC5_REG15,
	DVBT_FUNC5_REG16,
	DVBT_FUNC5_REG17,
	DVBT_FUNC5_REG18,


	// AD7 registers (for RTL2832 only)
	DVBT_AD7_SETTING,
	DVBT_RSSI_R,

	// ACI detection registers (for RTL2832 only)
	DVBT_ACI_DET_IND,

	// Clock output registers (for RTL2832 only)
	DVBT_REG_MON,
	DVBT_REG_MONSEL,
	DVBT_REG_GPE,
	DVBT_REG_GPO,
	DVBT_REG_4MSEL,


	// Test registers for test only
	DVBT_TEST_REG_1,
	DVBT_TEST_REG_2,
	DVBT_TEST_REG_3,
	DVBT_TEST_REG_4,

	// Item terminator
	DVBT_REG_BIT_NAME_ITEM_TERMINATOR,
};



// Register table length definitions
#define DVBT_REG_TABLE_LEN_MAX			DVBT_REG_BIT_NAME_ITEM_TERMINATOR





// DVB-T demod module pre-definition
typedef struct DVBT_DEMOD_MODULE_TAG DVBT_DEMOD_MODULE;





/**

@brief   DVB-T demod register page setting function pointer

Demod upper level functions will use DVBT_DEMOD_FP_SET_REG_PAGE() to set demod register page.


@param [in]   pDemod   The demod module pointer
@param [in]   PageNo   Page number


@retval   FUNCTION_SUCCESS   Set register page successfully with page number.
@retval   FUNCTION_ERROR     Set register page unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_SET_REG_PAGE() with the corresponding function.


@par Example:
@code


#include "demod_pseudo.h"


int main(void)
{
	DVBT_DEMOD_MODULE *pDemod;
	DVBT_DEMOD_MODULE DvbtDemodModuleMemory;
	PSEUDO_EXTRA_MODULE PseudoExtraModuleMemory;


	// Build pseudo demod module.
	BuildPseudoDemodModule(&pDemod, &DvbtDemodModuleMemory, &PseudoExtraModuleMemory);

	...

	// Set demod register page with page number 2.
	pDemod->SetRegPage(pDemod, 2);

	...

	return 0;
}


@endcode

*/
typedef int
(*DVBT_DEMOD_FP_SET_REG_PAGE)(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned long PageNo
	);





/**

@brief   DVB-T demod register byte setting function pointer

Demod upper level functions will use DVBT_DEMOD_FP_SET_REG_BYTES() to set demod register bytes.


@param [in]   pDemod          The demod module pointer
@param [in]   RegStartAddr    Demod register start address
@param [in]   pWritingBytes   Pointer to writing bytes
@param [in]   ByteNum         Writing byte number


@retval   FUNCTION_SUCCESS   Set demod register bytes successfully with writing bytes.
@retval   FUNCTION_ERROR     Set demod register bytes unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_SET_REG_BYTES() with the corresponding function.
	-# Need to set register page by DVBT_DEMOD_FP_SET_REG_PAGE() before using DVBT_DEMOD_FP_SET_REG_BYTES().


@see   DVBT_DEMOD_FP_SET_REG_PAGE, DVBT_DEMOD_FP_GET_REG_BYTES



@par Example:
@code


#include "demod_pseudo.h"


int main(void)
{
	DVBT_DEMOD_MODULE *pDemod;
	DVBT_DEMOD_MODULE DvbtDemodModuleMemory;
	PSEUDO_EXTRA_MODULE PseudoExtraModuleMemory;
	unsigned char WritingBytes[10];


	// Build pseudo demod module.
	BuildPseudoDemodModule(&pDemod, &DvbtDemodModuleMemory, &PseudoExtraModuleMemory);

	...

	// Set demod register bytes (page 1, address 0x17 ~ 0x1b) with 5 writing bytes.
	pDemod->SetRegPage(pDemod, 1);
	pDemod->SetRegBytes(pDemod, 0x17, WritingBytes, 5);

	...

	return 0;
}


@endcode

*/
typedef int
(*DVBT_DEMOD_FP_SET_REG_BYTES)(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	const unsigned char *pWritingBytes,
	unsigned char ByteNum
	);





/**

@brief   DVB-T demod register byte getting function pointer

Demod upper level functions will use DVBT_DEMOD_FP_GET_REG_BYTES() to get demod register bytes.


@param [in]    pDemod          The demod module pointer
@param [in]    RegStartAddr    Demod register start address
@param [out]   pReadingBytes   Pointer to an allocated memory for storing reading bytes
@param [in]    ByteNum         Reading byte number


@retval   FUNCTION_SUCCESS   Get demod register bytes successfully with reading byte number.
@retval   FUNCTION_ERROR     Get demod register bytes unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_GET_REG_BYTES() with the corresponding function.
	-# Need to set register page by DVBT_DEMOD_FP_SET_REG_PAGE() before using DVBT_DEMOD_FP_GET_REG_BYTES().


@see   DVBT_DEMOD_FP_SET_REG_PAGE, DVBT_DEMOD_FP_SET_REG_BYTES



@par Example:
@code


#include "demod_pseudo.h"


int main(void)
{
	DVBT_DEMOD_MODULE *pDemod;
	DVBT_DEMOD_MODULE DvbtDemodModuleMemory;
	PSEUDO_EXTRA_MODULE PseudoExtraModuleMemory;
	unsigned char ReadingBytes[10];


	// Build pseudo demod module.
	BuildPseudoDemodModule(&pDemod, &DvbtDemodModuleMemory, &PseudoExtraModuleMemory);

	...

	// Get demod register bytes (page 1, address 0x17 ~ 0x1b) with reading byte number 5.
	pDemod->SetRegPage(pDemod, 1);
	pDemod->GetRegBytes(pDemod, 0x17, ReadingBytes, 5);

	...

	return 0;
}


@endcode

*/
typedef int
(*DVBT_DEMOD_FP_GET_REG_BYTES)(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	unsigned char *pReadingBytes,
	unsigned char ByteNum
	);





/**

@brief   DVB-T demod register mask bits setting function pointer

Demod upper level functions will use DVBT_DEMOD_FP_SET_REG_MASK_BITS() to set demod register mask bits.


@param [in]   pDemod         The demod module pointer
@param [in]   RegStartAddr   Demod register start address
@param [in]   Msb            Mask MSB with 0-based index
@param [in]   Lsb            Mask LSB with 0-based index
@param [in]   WritingValue   The mask bits writing value


@retval   FUNCTION_SUCCESS   Set demod register mask bits successfully with writing value.
@retval   FUNCTION_ERROR     Set demod register mask bits unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_SET_REG_MASK_BITS() with the corresponding function.
	-# Need to set register page by DVBT_DEMOD_FP_SET_REG_PAGE() before using DVBT_DEMOD_FP_SET_REG_MASK_BITS().
	-# The constraints of DVBT_DEMOD_FP_SET_REG_MASK_BITS() function usage are described as follows:
		-# The mask MSB and LSB must be 0~31.
		-# The mask MSB must be greater than or equal to LSB.


@see   DVBT_DEMOD_FP_SET_REG_PAGE, DVBT_DEMOD_FP_GET_REG_MASK_BITS



@par Example:
@code


#include "demod_pseudo.h"


int main(void)
{
	DVBT_DEMOD_MODULE *pDemod;
	DVBT_DEMOD_MODULE DvbtDemodModuleMemory;
	PSEUDO_EXTRA_MODULE PseudoExtraModuleMemory;


	// Build pseudo demod module.
	BuildPseudoDemodModule(&pDemod, &DvbtDemodModuleMemory, &PseudoExtraModuleMemory);

	...

	// Set demod register bits (page 1, address {0x18, 0x17} [12:5]) with writing value 0x1d.
	pDemod->SetRegPage(pDemod, 1);
	pDemod->SetRegMaskBits(pDemod, 0x17, 12, 5, 0x1d);


	// Result:
	//
	// Writing value = 0x1d = 0001 1101 b
	// 
	// Page 1
	// Register address   0x17          0x18
	// Register value     xxx0 0011 b   101x xxxx b

	...

	return 0;
}


@endcode

*/
typedef int
(*DVBT_DEMOD_FP_SET_REG_MASK_BITS)(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	unsigned char Msb,
	unsigned char Lsb,
	const unsigned long WritingValue
	);





/**

@brief   DVB-T demod register mask bits getting function pointer

Demod upper level functions will use DVBT_DEMOD_FP_GET_REG_MASK_BITS() to get demod register mask bits.


@param [in]    pDemod          The demod module pointer
@param [in]    RegStartAddr    Demod register start address
@param [in]    Msb             Mask MSB with 0-based index
@param [in]    Lsb             Mask LSB with 0-based index
@param [out]   pReadingValue   Pointer to an allocated memory for storing reading value


@retval   FUNCTION_SUCCESS   Get demod register mask bits successfully.
@retval   FUNCTION_ERROR     Get demod register mask bits unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_GET_REG_MASK_BITS() with the corresponding function.
	-# Need to set register page by DVBT_DEMOD_FP_SET_REG_PAGE() before using DVBT_DEMOD_FP_GET_REG_MASK_BITS().
	-# The constraints of DVBT_DEMOD_FP_GET_REG_MASK_BITS() function usage are described as follows:
		-# The mask MSB and LSB must be 0~31.
		-# The mask MSB must be greater than or equal to LSB.


@see   DVBT_DEMOD_FP_SET_REG_PAGE, DVBT_DEMOD_FP_SET_REG_MASK_BITS



@par Example:
@code


#include "demod_pseudo.h"


int main(void)
{
	DVBT_DEMOD_MODULE *pDemod;
	DVBT_DEMOD_MODULE DvbtDemodModuleMemory;
	PSEUDO_EXTRA_MODULE PseudoExtraModuleMemory;
	unsigned long ReadingValue;


	// Build pseudo demod module.
	BuildPseudoDemodModule(&pDemod, &DvbtDemodModuleMemory, &PseudoExtraModuleMemory);

	...

	// Get demod register bits (page 1, address {0x18, 0x17} [12:5]).
	pDemod->SetRegPage(pDemod, 1);
	pDemod->GetRegMaskBits(pDemod, 0x17, 12, 5, &ReadingValue);


	// Result:
	//
	// Page 1
	// Register address   0x18          0x17
	// Register value     xxx0 0011 b   101x xxxx b
	//
	// Reading value = 0001 1101 b = 0x1d

	...

	return 0;
}


@endcode

*/
typedef int
(*DVBT_DEMOD_FP_GET_REG_MASK_BITS)(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	unsigned char Msb,
	unsigned char Lsb,
	unsigned long *pReadingValue
	);





/**

@brief   DVB-T demod register bits setting function pointer

Demod upper level functions will use DVBT_DEMOD_FP_SET_REG_BITS() to set demod register bits with bit name.


@param [in]   pDemod         The demod module pointer
@param [in]   RegBitName     Pre-defined demod register bit name
@param [in]   WritingValue   The mask bits writing value


@retval   FUNCTION_SUCCESS   Set demod register bits successfully with bit name and writing value.
@retval   FUNCTION_ERROR     Set demod register bits unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_SET_REG_BITS() with the corresponding function.
	-# Need to set register page before using DVBT_DEMOD_FP_SET_REG_BITS().
	-# Register bit names are pre-defined keys for bit access, and one can find these in demod header file.


@see   DVBT_DEMOD_FP_SET_REG_PAGE, DVBT_DEMOD_FP_GET_REG_BITS



@par Example:
@code


#include "demod_pseudo.h"


int main(void)
{
	DVBT_DEMOD_MODULE *pDemod;
	DVBT_DEMOD_MODULE DvbtDemodModuleMemory;
	PSEUDO_EXTRA_MODULE PseudoExtraModuleMemory;


	// Build pseudo demod module.
	BuildPseudoDemodModule(&pDemod, &DvbtDemodModuleMemory, &PseudoExtraModuleMemory);

	...

	// Set demod register bits with bit name PSEUDO_REG_BIT_NAME and writing value 0x1d.
	// The corresponding information of PSEUDO_REG_BIT_NAME is address {0x18, 0x17} [12:5] on page 1.
	pDemod->SetRegPage(pDemod, 1);
	pDemod->SetRegBits(pDemod, PSEUDO_REG_BIT_NAME, 0x1d);


	// Result:
	//
	// Writing value = 0x1d = 0001 1101 b
	// 
	// Page 1
	// Register address   0x18          0x17
	// Register value     xxx0 0011 b   101x xxxx b

	...

	return 0;
}


@endcode

*/
typedef int
(*DVBT_DEMOD_FP_SET_REG_BITS)(
	DVBT_DEMOD_MODULE *pDemod,
	int RegBitName,
	const unsigned long WritingValue
	);





/**

@brief   DVB-T demod register bits getting function pointer

Demod upper level functions will use DVBT_DEMOD_FP_GET_REG_BITS() to get demod register bits with bit name.


@param [in]    pDemod          The demod module pointer
@param [in]    RegBitName      Pre-defined demod register bit name
@param [out]   pReadingValue   Pointer to an allocated memory for storing reading value


@retval   FUNCTION_SUCCESS   Get demod register bits successfully with bit name.
@retval   FUNCTION_ERROR     Get demod register bits unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_GET_REG_BITS() with the corresponding function.
	-# Need to set register page before using DVBT_DEMOD_FP_GET_REG_BITS().
	-# Register bit names are pre-defined keys for bit access, and one can find these in demod header file.


@see   DVBT_DEMOD_FP_SET_REG_PAGE, DVBT_DEMOD_FP_SET_REG_BITS



@par Example:
@code


#include "demod_pseudo.h"


int main(void)
{
	DVBT_DEMOD_MODULE *pDemod;
	DVBT_DEMOD_MODULE DvbtDemodModuleMemory;
	PSEUDO_EXTRA_MODULE PseudoExtraModuleMemory;
	unsigned long ReadingValue;


	// Build pseudo demod module.
	BuildPseudoDemodModule(&pDemod, &DvbtDemodModuleMemory, &PseudoExtraModuleMemory);

	...

	// Get demod register bits with bit name PSEUDO_REG_BIT_NAME.
	// The corresponding information of PSEUDO_REG_BIT_NAME is address {0x18, 0x17} [12:5] on page 1.
	pDemod->SetRegPage(pDemod, 1);
	pDemod->GetRegBits(pDemod, PSEUDO_REG_BIT_NAME, &ReadingValue);


	// Result:
	//
	// Page 1
	// Register address   0x18          0x17
	// Register value     xxx0 0011 b   101x xxxx b
	//
	// Reading value = 0001 1101 b = 0x1d

	...

	return 0;
}


@endcode

*/
typedef int
(*DVBT_DEMOD_FP_GET_REG_BITS)(
	DVBT_DEMOD_MODULE *pDemod,
	int RegBitName,
	unsigned long *pReadingValue
	);





/**

@brief   DVB-T demod register bits setting function pointer (with page setting)

Demod upper level functions will use DVBT_DEMOD_FP_SET_REG_BITS_WITH_PAGE() to set demod register bits with bit name and
page setting.


@param [in]   pDemod         The demod module pointer
@param [in]   RegBitName     Pre-defined demod register bit name
@param [in]   WritingValue   The mask bits writing value


@retval   FUNCTION_SUCCESS   Set demod register bits successfully with bit name, page setting, and writing value.
@retval   FUNCTION_ERROR     Set demod register bits unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_SET_REG_BITS_WITH_PAGE() with the corresponding function.
	-# Don't need to set register page before using DVBT_DEMOD_FP_SET_REG_BITS_WITH_PAGE().
	-# Register bit names are pre-defined keys for bit access, and one can find these in demod header file.


@see   DVBT_DEMOD_FP_GET_REG_BITS_WITH_PAGE



@par Example:
@code


#include "demod_pseudo.h"


int main(void)
{
	DVBT_DEMOD_MODULE *pDemod;
	DVBT_DEMOD_MODULE DvbtDemodModuleMemory;
	PSEUDO_EXTRA_MODULE PseudoExtraModuleMemory;


	// Build pseudo demod module.
	BuildPseudoDemodModule(&pDemod, &DvbtDemodModuleMemory, &PseudoExtraModuleMemory);

	...

	// Set demod register bits with bit name PSEUDO_REG_BIT_NAME and writing value 0x1d.
	// The corresponding information of PSEUDO_REG_BIT_NAME is address {0x18, 0x17} [12:5] on page 1.
	pDemod->SetRegBitsWithPage(pDemod, PSEUDO_REG_BIT_NAME, 0x1d);


	// Result:
	//
	// Writing value = 0x1d = 0001 1101 b
	// 
	// Page 1
	// Register address   0x18          0x17
	// Register value     xxx0 0011 b   101x xxxx b

	...

	return 0;
}


@endcode

*/
typedef int
(*DVBT_DEMOD_FP_SET_REG_BITS_WITH_PAGE)(
	DVBT_DEMOD_MODULE *pDemod,
	int RegBitName,
	const unsigned long WritingValue
	);





/**

@brief   DVB-T demod register bits getting function pointer (with page setting)

Demod upper level functions will use DVBT_DEMOD_FP_GET_REG_BITS_WITH_PAGE() to get demod register bits with bit name and
page setting.


@param [in]    pDemod          The demod module pointer
@param [in]    RegBitName      Pre-defined demod register bit name
@param [out]   pReadingValue   Pointer to an allocated memory for storing reading value


@retval   FUNCTION_SUCCESS   Get demod register bits successfully with bit name and page setting.
@retval   FUNCTION_ERROR     Get demod register bits unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_GET_REG_BITS_WITH_PAGE() with the corresponding function.
	-# Don't need to set register page before using DVBT_DEMOD_FP_GET_REG_BITS_WITH_PAGE().
	-# Register bit names are pre-defined keys for bit access, and one can find these in demod header file.


@see   DVBT_DEMOD_FP_SET_REG_BITS_WITH_PAGE



@par Example:
@code


#include "demod_pseudo.h"


int main(void)
{
	DVBT_DEMOD_MODULE *pDemod;
	DVBT_DEMOD_MODULE DvbtDemodModuleMemory;
	PSEUDO_EXTRA_MODULE PseudoExtraModuleMemory;
	unsigned long ReadingValue;


	// Build pseudo demod module.
	BuildPseudoDemodModule(&pDemod, &DvbtDemodModuleMemory, &PseudoExtraModuleMemory);

	...

	// Get demod register bits with bit name PSEUDO_REG_BIT_NAME.
	// The corresponding information of PSEUDO_REG_BIT_NAME is address {0x18, 0x17} [12:5] on page 1.
	pDemod->GetRegBitsWithPage(pDemod, PSEUDO_REG_BIT_NAME, &ReadingValue);


	// Result:
	//
	// Page 1
	// Register address   0x18          0x17
	// Register value     xxx0 0011 b   101x xxxx b
	//
	// Reading value = 0001 1101 b = 0x1d

	...

	return 0;
}


@endcode

*/
typedef int
(*DVBT_DEMOD_FP_GET_REG_BITS_WITH_PAGE)(
	DVBT_DEMOD_MODULE *pDemod,
	int RegBitName,
	unsigned long *pReadingValue
	);





/**

@brief   DVB-T demod type getting function pointer

One can use DVBT_DEMOD_FP_GET_DEMOD_TYPE() to get DVB-T demod type.


@param [in]    pDemod       The demod module pointer
@param [out]   pDemodType   Pointer to an allocated memory for storing demod type


@note
	-# Demod building function will set DVBT_DEMOD_FP_GET_DEMOD_TYPE() with the corresponding function.


@see   MODULE_TYPE

*/
typedef void
(*DVBT_DEMOD_FP_GET_DEMOD_TYPE)(
	DVBT_DEMOD_MODULE *pDemod,
	int *pDemodType
	);





/**

@brief   DVB-T demod I2C device address getting function pointer

One can use DVBT_DEMOD_FP_GET_DEVICE_ADDR() to get DVB-T demod I2C device address.


@param [in]    pDemod        The demod module pointer
@param [out]   pDeviceAddr   Pointer to an allocated memory for storing demod I2C device address


@retval   FUNCTION_SUCCESS   Get demod device address successfully.
@retval   FUNCTION_ERROR     Get demod device address unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_GET_DEVICE_ADDR() with the corresponding function.

*/
typedef void
(*DVBT_DEMOD_FP_GET_DEVICE_ADDR)(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned char *pDeviceAddr
	);





/**

@brief   DVB-T demod crystal frequency getting function pointer

One can use DVBT_DEMOD_FP_GET_CRYSTAL_FREQ_HZ() to get DVB-T demod crystal frequency in Hz.


@param [in]    pDemod           The demod module pointer
@param [out]   pCrystalFreqHz   Pointer to an allocated memory for storing demod crystal frequency in Hz


@retval   FUNCTION_SUCCESS   Get demod crystal frequency successfully.
@retval   FUNCTION_ERROR     Get demod crystal frequency unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_GET_CRYSTAL_FREQ_HZ() with the corresponding function.

*/
typedef void
(*DVBT_DEMOD_FP_GET_CRYSTAL_FREQ_HZ)(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned long *pCrystalFreqHz
	);





/**

@brief   DVB-T demod I2C bus connection asking function pointer

One can use DVBT_DEMOD_FP_IS_CONNECTED_TO_I2C() to ask DVB-T demod if it is connected to I2C bus.


@param [in]    pDemod    The demod module pointer
@param [out]   pAnswer   Pointer to an allocated memory for storing answer


@note
	-# Demod building function will set DVBT_DEMOD_FP_IS_CONNECTED_TO_I2C() with the corresponding function.

*/
typedef void
(*DVBT_DEMOD_FP_IS_CONNECTED_TO_I2C)(
	DVBT_DEMOD_MODULE *pDemod,
	int *pAnswer
	);





/**

@brief   DVB-T demod software resetting function pointer

One can use DVBT_DEMOD_FP_SOFTWARE_RESET() to reset DVB-T demod by software reset.


@param [in]   pDemod   The demod module pointer


@retval   FUNCTION_SUCCESS   Reset demod by software reset successfully.
@retval   FUNCTION_ERROR     Reset demod by software reset unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_SOFTWARE_RESET() with the corresponding function.

*/
typedef int
(*DVBT_DEMOD_FP_SOFTWARE_RESET)(
	DVBT_DEMOD_MODULE *pDemod
	);





/**

@brief   DVB-T demod initializing function pointer

One can use DVBT_DEMOD_FP_INITIALIZE() to initialie DVB-T demod.


@param [in]   pDemod   The demod module pointer


@retval   FUNCTION_SUCCESS   Initialize demod successfully.
@retval   FUNCTION_ERROR     Initialize demod unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_INITIALIZE() with the corresponding function.

*/
typedef int
(*DVBT_DEMOD_FP_INITIALIZE)(
	DVBT_DEMOD_MODULE *pDemod
	);





/**

@brief   DVB-T demod bandwidth mode setting function pointer

One can use DVBT_DEMOD_FP_SET_DVBT_MODE() to set DVB-T demod bandwidth mode.


@param [in]   pDemod	      The demod module pointer
@param [in]   BandwidthMode   Bandwidth mode for setting


@retval   FUNCTION_SUCCESS   Set demod bandwidth mode successfully.
@retval   FUNCTION_ERROR     Set demod bandwidth mode unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_SET_DVBT_MODE() with the corresponding function.


@see   DVBT_BANDWIDTH_MODE

*/
typedef int
(*DVBT_DEMOD_FP_SET_BANDWIDTH_MODE)(
	DVBT_DEMOD_MODULE *pDemod,
	int BandwidthMode
	);





/**

@brief   DVB-T demod IF frequency setting function pointer

One can use DVBT_DEMOD_FP_SET_IF_FREQ_HZ() to set DVB-T demod IF frequency in Hz.


@param [in]   pDemod     The demod module pointer
@param [in]   IfFreqHz   IF frequency in Hz for setting


@retval   FUNCTION_SUCCESS   Set demod IF frequency successfully.
@retval   FUNCTION_ERROR     Set demod IF frequency unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_SET_IF_FREQ_HZ() with the corresponding function.


@see   IF_FREQ_HZ

*/
typedef int
(*DVBT_DEMOD_FP_SET_IF_FREQ_HZ)(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned long IfFreqHz
	);





/**

@brief   DVB-T demod spectrum mode setting function pointer

One can use DVBT_DEMOD_FP_SET_SPECTRUM_MODE() to set DVB-T demod spectrum mode.


@param [in]   pDemod         The demod module pointer
@param [in]   SpectrumMode   Spectrum mode for setting


@retval   FUNCTION_SUCCESS   Set demod spectrum mode successfully.
@retval   FUNCTION_ERROR     Set demod spectrum mode unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_SET_SPECTRUM_MODE() with the corresponding function.


@see   SPECTRUM_MODE

*/
typedef int
(*DVBT_DEMOD_FP_SET_SPECTRUM_MODE)(
	DVBT_DEMOD_MODULE *pDemod,
	int SpectrumMode
	);





/**

@brief   DVB-T demod bandwidth mode getting function pointer

One can use DVBT_DEMOD_FP_GET_DVBT_MODE() to get DVB-T demod bandwidth mode.


@param [in]    pDemod           The demod module pointer
@param [out]   pBandwidthMode   Pointer to an allocated memory for storing demod bandwidth mode


@retval   FUNCTION_SUCCESS   Get demod bandwidth mode successfully.
@retval   FUNCTION_ERROR     Get demod bandwidth mode unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_GET_DVBT_MODE() with the corresponding function.


@see   DVBT_DVBT_MODE

*/
typedef int
(*DVBT_DEMOD_FP_GET_BANDWIDTH_MODE)(
	DVBT_DEMOD_MODULE *pDemod,
	int *pBandwidthMode
	);





/**

@brief   DVB-T demod IF frequency getting function pointer

One can use DVBT_DEMOD_FP_GET_IF_FREQ_HZ() to get DVB-T demod IF frequency in Hz.


@param [in]    pDemod      The demod module pointer
@param [out]   pIfFreqHz   Pointer to an allocated memory for storing demod IF frequency in Hz


@retval   FUNCTION_SUCCESS   Get demod IF frequency successfully.
@retval   FUNCTION_ERROR     Get demod IF frequency unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_GET_IF_FREQ_HZ() with the corresponding function.


@see   IF_FREQ_HZ

*/
typedef int
(*DVBT_DEMOD_FP_GET_IF_FREQ_HZ)(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned long *pIfFreqHz
	);





/**

@brief   DVB-T demod spectrum mode getting function pointer

One can use DVBT_DEMOD_FP_GET_SPECTRUM_MODE() to get DVB-T demod spectrum mode.


@param [in]    pDemod          The demod module pointer
@param [out]   pSpectrumMode   Pointer to an allocated memory for storing demod spectrum mode


@retval   FUNCTION_SUCCESS   Get demod spectrum mode successfully.
@retval   FUNCTION_ERROR     Get demod spectrum mode unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_GET_SPECTRUM_MODE() with the corresponding function.


@see   SPECTRUM_MODE

*/
typedef int
(*DVBT_DEMOD_FP_GET_SPECTRUM_MODE)(
	DVBT_DEMOD_MODULE *pDemod,
	int *pSpectrumMode
	);





/**

@brief   DVB-T demod TPS lock asking function pointer

One can use DVBT_DEMOD_FP_IS_TPS_LOCKED() to ask DVB-T demod if it is TPS-locked.


@param [in]    pDemod    The demod module pointer
@param [out]   pAnswer   Pointer to an allocated memory for storing answer


@retval   FUNCTION_SUCCESS   Perform TPS lock asking to demod successfully.
@retval   FUNCTION_ERROR     Perform TPS lock asking to demod unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_IS_TPS_LOCKED() with the corresponding function.

*/
typedef int
(*DVBT_DEMOD_FP_IS_TPS_LOCKED)(
	DVBT_DEMOD_MODULE *pDemod,
	int *pAnswer
	);





/**

@brief   DVB-T demod signal lock asking function pointer

One can use DVBT_DEMOD_FP_IS_SIGNAL_LOCKED() to ask DVB-T demod if it is signal-locked.


@param [in]    pDemod    The demod module pointer
@param [out]   pAnswer   Pointer to an allocated memory for storing answer


@retval   FUNCTION_SUCCESS   Perform signal lock asking to demod successfully.
@retval   FUNCTION_ERROR     Perform signal lock asking to demod unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_IS_SIGNAL_LOCKED() with the corresponding function.

*/
typedef int
(*DVBT_DEMOD_FP_IS_SIGNAL_LOCKED)(
	DVBT_DEMOD_MODULE *pDemod,
	int *pAnswer
	);





/**

@brief   DVB-T demod signal strength getting function pointer

One can use DVBT_DEMOD_FP_GET_SIGNAL_STRENGTH() to get signal strength.


@param [in]    pDemod            The demod module pointer
@param [out]   pSignalStrength   Pointer to an allocated memory for storing signal strength (value = 0 ~ 100)


@retval   FUNCTION_SUCCESS   Get demod signal strength successfully.
@retval   FUNCTION_ERROR     Get demod signal strength unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_GET_SIGNAL_STRENGTH() with the corresponding function.

*/
typedef int
(*DVBT_DEMOD_FP_GET_SIGNAL_STRENGTH)(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned long *pSignalStrength
	);





/**

@brief   DVB-T demod signal quality getting function pointer

One can use DVBT_DEMOD_FP_GET_SIGNAL_QUALITY() to get signal quality.


@param [in]    pDemod           The demod module pointer
@param [out]   pSignalQuality   Pointer to an allocated memory for storing signal quality (value = 0 ~ 100)


@retval   FUNCTION_SUCCESS   Get demod signal quality successfully.
@retval   FUNCTION_ERROR     Get demod signal quality unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_GET_SIGNAL_QUALITY() with the corresponding function.

*/
typedef int
(*DVBT_DEMOD_FP_GET_SIGNAL_QUALITY)(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned long *pSignalQuality
	);





/**

@brief   DVB-T demod BER getting function pointer

One can use DVBT_DEMOD_FP_GET_BER() to get BER.


@param [in]    pDemod          The demod module pointer
@param [out]   pBerNum         Pointer to an allocated memory for storing BER numerator
@param [out]   pBerDen         Pointer to an allocated memory for storing BER denominator


@retval   FUNCTION_SUCCESS   Get demod error rate value successfully.
@retval   FUNCTION_ERROR     Get demod error rate value unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_GET_BER() with the corresponding function.

*/
typedef int
(*DVBT_DEMOD_FP_GET_BER)(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned long *pBerNum,
	unsigned long *pBerDen
	);





/**

@brief   DVB-T demod SNR getting function pointer

One can use DVBT_DEMOD_FP_GET_SNR_DB() to get SNR in dB.


@param [in]    pDemod      The demod module pointer
@param [out]   pSnrDbNum   Pointer to an allocated memory for storing SNR dB numerator
@param [out]   pSnrDbDen   Pointer to an allocated memory for storing SNR dB denominator


@retval   FUNCTION_SUCCESS   Get demod SNR successfully.
@retval   FUNCTION_ERROR     Get demod SNR unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_GET_SNR_DB() with the corresponding function.

*/
typedef int
(*DVBT_DEMOD_FP_GET_SNR_DB)(
	DVBT_DEMOD_MODULE *pDemod,
	long *pSnrDbNum,
	long *pSnrDbDen
	);





/**

@brief   DVB-T demod RF AGC getting function pointer

One can use DVBT_DEMOD_FP_GET_RF_AGC() to get DVB-T demod RF AGC value.


@param [in]    pDemod   The demod module pointer
@param [out]   pRfAgc   Pointer to an allocated memory for storing RF AGC value


@retval   FUNCTION_SUCCESS   Get demod RF AGC value successfully.
@retval   FUNCTION_ERROR     Get demod RF AGC value unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_GET_RF_AGC() with the corresponding function.
	-# The range of RF AGC value is (-pow(2, 13)) ~ (pow(2, 13) - 1).

*/
typedef int
(*DVBT_DEMOD_FP_GET_RF_AGC)(
	DVBT_DEMOD_MODULE *pDemod,
	long *pRfAgc
	);





/**

@brief   DVB-T demod IF AGC getting function pointer

One can use DVBT_DEMOD_FP_GET_IF_AGC() to get DVB-T demod IF AGC value.


@param [in]    pDemod   The demod module pointer
@param [out]   pIfAgc   Pointer to an allocated memory for storing IF AGC value


@retval   FUNCTION_SUCCESS   Get demod IF AGC value successfully.
@retval   FUNCTION_ERROR     Get demod IF AGC value unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_GET_IF_AGC() with the corresponding function.
	-# The range of IF AGC value is (-pow(2, 13)) ~ (pow(2, 13) - 1).

*/
typedef int
(*DVBT_DEMOD_FP_GET_IF_AGC)(
	DVBT_DEMOD_MODULE *pDemod,
	long *pIfAgc
	);





/**

@brief   DVB-T demod digital AGC getting function pointer

One can use DVBT_DEMOD_FP_GET_DI_AGC() to get DVB-T demod digital AGC value.


@param [in]    pDemod   The demod module pointer
@param [out]   pDiAgc   Pointer to an allocated memory for storing digital AGC value


@retval   FUNCTION_SUCCESS   Get demod digital AGC value successfully.
@retval   FUNCTION_ERROR     Get demod digital AGC value unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_GET_DI_AGC() with the corresponding function.
	-# The range of digital AGC value is 0 ~ (pow(2, 8) - 1).

*/
typedef int
(*DVBT_DEMOD_FP_GET_DI_AGC)(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned char *pDiAgc
	);





/**

@brief   DVB-T demod TR offset getting function pointer

One can use DVBT_DEMOD_FP_GET_TR_OFFSET_PPM() to get TR offset in ppm.


@param [in]    pDemod         The demod module pointer
@param [out]   pTrOffsetPpm   Pointer to an allocated memory for storing TR offset in ppm


@retval   FUNCTION_SUCCESS   Get demod TR offset successfully.
@retval   FUNCTION_ERROR     Get demod TR offset unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_GET_TR_OFFSET_PPM() with the corresponding function.

*/
typedef int
(*DVBT_DEMOD_FP_GET_TR_OFFSET_PPM)(
	DVBT_DEMOD_MODULE *pDemod,
	long *pTrOffsetPpm
	);





/**

@brief   DVB-T demod CR offset getting function pointer

One can use DVBT_DEMOD_FP_GET_CR_OFFSET_HZ() to get CR offset in Hz.


@param [in]    pDemod        The demod module pointer
@param [out]   pCrOffsetHz   Pointer to an allocated memory for storing CR offset in Hz


@retval   FUNCTION_SUCCESS   Get demod CR offset successfully.
@retval   FUNCTION_ERROR     Get demod CR offset unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_GET_CR_OFFSET_HZ() with the corresponding function.

*/
typedef int
(*DVBT_DEMOD_FP_GET_CR_OFFSET_HZ)(
	DVBT_DEMOD_MODULE *pDemod,
	long *pCrOffsetHz
	);





/**

@brief   DVB-T demod constellation mode getting function pointer

One can use DVBT_DEMOD_FP_GET_CONSTELLATION() to get DVB-T demod constellation mode.


@param [in]    pDemod           The demod module pointer
@param [out]   pConstellation   Pointer to an allocated memory for storing demod constellation mode


@retval   FUNCTION_SUCCESS   Get demod constellation mode successfully.
@retval   FUNCTION_ERROR     Get demod constellation mode unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_GET_CONSTELLATION() with the corresponding function.


@see   DVBT_CONSTELLATION_MODE

*/
typedef int
(*DVBT_DEMOD_FP_GET_CONSTELLATION)(
	DVBT_DEMOD_MODULE *pDemod,
	int *pConstellation
	);





/**

@brief   DVB-T demod hierarchy mode getting function pointer

One can use DVBT_DEMOD_FP_GET_HIERARCHY() to get DVB-T demod hierarchy mode.


@param [in]    pDemod       The demod module pointer
@param [out]   pHierarchy   Pointer to an allocated memory for storing demod hierarchy mode


@retval   FUNCTION_SUCCESS   Get demod hierarchy mode successfully.
@retval   FUNCTION_ERROR     Get demod hierarchy mode unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_GET_HIERARCHY() with the corresponding function.


@see   DVBT_HIERARCHY_MODE

*/
typedef int
(*DVBT_DEMOD_FP_GET_HIERARCHY)(
	DVBT_DEMOD_MODULE *pDemod,
	int *pHierarchy
	);





/**

@brief   DVB-T demod low-priority code rate mode getting function pointer

One can use DVBT_DEMOD_FP_GET_CODE_RATE_LP() to get DVB-T demod low-priority code rate mode.


@param [in]    pDemod        The demod module pointer
@param [out]   pCodeRateLp   Pointer to an allocated memory for storing demod low-priority code rate mode


@retval   FUNCTION_SUCCESS   Get demod low-priority code rate mode successfully.
@retval   FUNCTION_ERROR     Get demod low-priority code rate mode unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_GET_CODE_RATE_LP() with the corresponding function.


@see   DVBT_CODE_RATE_MODE

*/
typedef int
(*DVBT_DEMOD_FP_GET_CODE_RATE_LP)(
	DVBT_DEMOD_MODULE *pDemod,
	int *pCodeRateLp
	);





/**

@brief   DVB-T demod high-priority code rate mode getting function pointer

One can use DVBT_DEMOD_FP_GET_CODE_RATE_HP() to get DVB-T demod high-priority code rate mode.


@param [in]    pDemod        The demod module pointer
@param [out]   pCodeRateHp   Pointer to an allocated memory for storing demod high-priority code rate mode


@retval   FUNCTION_SUCCESS   Get demod high-priority code rate mode successfully.
@retval   FUNCTION_ERROR     Get demod high-priority code rate mode unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_GET_CODE_RATE_HP() with the corresponding function.


@see   DVBT_CODE_RATE_MODE

*/
typedef int
(*DVBT_DEMOD_FP_GET_CODE_RATE_HP)(
	DVBT_DEMOD_MODULE *pDemod,
	int *pCodeRateHp
	);





/**

@brief   DVB-T demod guard interval mode getting function pointer

One can use DVBT_DEMOD_FP_GET_GUARD_INTERVAL() to get DVB-T demod guard interval mode.


@param [in]    pDemod           The demod module pointer
@param [out]   pGuardInterval   Pointer to an allocated memory for storing demod guard interval mode


@retval   FUNCTION_SUCCESS   Get demod guard interval mode successfully.
@retval   FUNCTION_ERROR     Get demod guard interval mode unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_GET_GUARD_INTERVAL() with the corresponding function.


@see   DVBT_GUARD_INTERVAL_MODE

*/
typedef int
(*DVBT_DEMOD_FP_GET_GUARD_INTERVAL)(
	DVBT_DEMOD_MODULE *pDemod,
	int *pGuardInterval
	);





/**

@brief   DVB-T demod FFT mode getting function pointer

One can use DVBT_DEMOD_FP_GET_FFT_MODE() to get DVB-T demod FFT mode.


@param [in]    pDemod     The demod module pointer
@param [out]   pFftMode   Pointer to an allocated memory for storing demod FFT mode


@retval   FUNCTION_SUCCESS   Get demod FFT mode successfully.
@retval   FUNCTION_ERROR     Get demod FFT mode unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_GET_FFT_MODE() with the corresponding function.


@see   DVBT_FFT_MODE_MODE

*/
typedef int
(*DVBT_DEMOD_FP_GET_FFT_MODE)(
	DVBT_DEMOD_MODULE *pDemod,
	int *pFftMode
	);





/**

@brief   DVB-T demod updating function pointer

One can use DVBT_DEMOD_FP_UPDATE_FUNCTION() to update demod register setting.


@param [in]   pDemod   The demod module pointer


@retval   FUNCTION_SUCCESS   Update demod setting successfully.
@retval   FUNCTION_ERROR     Update demod setting unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_UPDATE_FUNCTION() with the corresponding function.



@par Example:
@code


#include "demod_pseudo.h"


int main(void)
{
	DVBT_DEMOD_MODULE *pDemod;
	DVBT_DEMOD_MODULE DvbtDemodModuleMemory;
	PSEUDO_EXTRA_MODULE PseudoExtraModuleMemory;


	// Build pseudo demod module.
	BuildPseudoDemodModule(&pDemod, &DvbtDemodModuleMemory, &PseudoExtraModuleMemory);

	...

	// Execute ResetFunction() before demod software reset.
	pDemod->ResetFunction(pDemod);

	// Reset demod by software.
	pDemod->SoftwareReset(pDemod);

	...

	return 0;
}


void PeriodicallyExecutingFunction
{
	// Executing UpdateFunction() periodically.
	pDemod->UpdateFunction(pDemod);
}


@endcode

*/
typedef int
(*DVBT_DEMOD_FP_UPDATE_FUNCTION)(
	DVBT_DEMOD_MODULE *pDemod
	);





/**

@brief   DVB-T demod reseting function pointer

One can use DVBT_DEMOD_FP_RESET_FUNCTION() to reset demod register setting.


@param [in]   pDemod   The demod module pointer


@retval   FUNCTION_SUCCESS   Reset demod setting successfully.
@retval   FUNCTION_ERROR     Reset demod setting unsuccessfully.


@note
	-# Demod building function will set DVBT_DEMOD_FP_RESET_FUNCTION() with the corresponding function.



@par Example:
@code


#include "demod_pseudo.h"


int main(void)
{
	DVBT_DEMOD_MODULE *pDemod;
	DVBT_DEMOD_MODULE DvbtDemodModuleMemory;
	PSEUDO_EXTRA_MODULE PseudoExtraModuleMemory;


	// Build pseudo demod module.
	BuildPseudoDemodModule(&pDemod, &DvbtDemodModuleMemory, &PseudoExtraModuleMemory);

	...

	// Execute ResetFunction() before demod software reset.
	pDemod->ResetFunction(pDemod);

	// Reset demod by software.
	pDemod->SoftwareReset(pDemod);

	...

	return 0;
}


void PeriodicallyExecutingFunction
{
	// Executing UpdateFunction() periodically.
	pDemod->UpdateFunction(pDemod);
}


@endcode

*/
typedef int
(*DVBT_DEMOD_FP_RESET_FUNCTION)(
	DVBT_DEMOD_MODULE *pDemod
	);





/// RTL2830 extra module

// Definitions for Function 4
#define DVBT_FUNC4_REG_VALUE_NUM		5

typedef struct RTL2830_EXTRA_MODULE_TAG RTL2830_EXTRA_MODULE;

/*

@brief   RTL2830 application mode getting function pointer

One can use RTL2830_FP_GET_APP_MODE() to get RTL2830 application mode.


@param [in]    pDemod     The demod module pointer
@param [out]   pAppMode   Pointer to an allocated memory for storing demod application mode


@retval   FUNCTION_SUCCESS   Get demod application mode successfully.
@retval   FUNCTION_ERROR     Get demod application mode unsuccessfully.


@note
	-# Demod building function will set RTL2830_FP_GET_APP_MODE() with the corresponding function.


@see   RTL2830_APPLICATION_MODE

*/
typedef void
(*RTL2830_FP_GET_APP_MODE)(
	DVBT_DEMOD_MODULE *pDemod,
	int *pAppMode
	);

struct RTL2830_EXTRA_MODULE_TAG
{
	// RTL2830 variables
	int AppMode;

	// RTL2830 update procedure enabling status
	int IsFunction2Enabled;
	int IsFunction3Enabled;
	int IsFunction4Enabled;
	int IsFunction5Enabled;

	// RTL2830 update procedure variables
	unsigned char Func2Executing;
	unsigned char Func3State;
	unsigned char Func3Executing;
	unsigned char Func4State;
	unsigned long Func4DelayCnt;
	unsigned long Func4DelayCntMax;
	unsigned char Func4ParamSetting;
	unsigned long Func4RegValue[DVBT_FUNC4_REG_VALUE_NUM];
	unsigned char Func5State;
	unsigned char Func5QamBak;
	unsigned char Func5HierBak;
	unsigned char Func5LpCrBak;
	unsigned char Func5HpCrBak;
	unsigned char Func5GiBak;
	unsigned char Func5FftBak;

	// RTL2830 extra function pointers
	RTL2830_FP_GET_APP_MODE GetAppMode;
};





/// RTL2832 extra module
typedef struct RTL2832_EXTRA_MODULE_TAG RTL2832_EXTRA_MODULE;

/*

@brief   RTL2832 application mode getting function pointer

One can use RTL2832_FP_GET_APP_MODE() to get RTL2832 application mode.


@param [in]    pDemod     The demod module pointer
@param [out]   pAppMode   Pointer to an allocated memory for storing demod application mode


@retval   FUNCTION_SUCCESS   Get demod application mode successfully.
@retval   FUNCTION_ERROR     Get demod application mode unsuccessfully.


@note
	-# Demod building function will set RTL2832_FP_GET_APP_MODE() with the corresponding function.


@see   RTL2832_APPLICATION_MODE

*/
typedef void
(*RTL2832_FP_GET_APP_MODE)(
	DVBT_DEMOD_MODULE *pDemod,
	int *pAppMode
	);

struct RTL2832_EXTRA_MODULE_TAG
{
	// RTL2832 extra variables
	int AppMode;

	// RTL2832 update procedure enabling status
	int IsFunc1Enabled;

	// RTL2832 update Function 1 variables
	int Func1State;

	int Func1WaitTimeMax;
	int Func1GettingTimeMax;
	int Func1GettingNumEachTime;

	int Func1WaitTime;
	int Func1GettingTime;

	unsigned long Func1RsdBerEstSumNormal;
	unsigned long Func1RsdBerEstSumConfig1;
	unsigned long Func1RsdBerEstSumConfig2;
	unsigned long Func1RsdBerEstSumConfig3;

	int Func1QamBak;
	int Func1HierBak;
	int Func1LpCrBak;
	int Func1HpCrBak;
	int Func1GiBak;
	int Func1FftBak;

	// RTL2832 extra function pointers
	RTL2832_FP_GET_APP_MODE GetAppMode;
};





/// DVB-T demod module structure
struct DVBT_DEMOD_MODULE_TAG
{
	unsigned long CurrentPageNo;
	// Private variables
	int           DemodType;
	unsigned char DeviceAddr;
	unsigned long CrystalFreqHz;
	int           TsInterfaceMode;

	int           BandwidthMode;
	unsigned long IfFreqHz;
	int           SpectrumMode;

	int IsBandwidthModeSet;
	int IsIfFreqHzSet;
	int IsSpectrumModeSet;

	union											///<   Demod extra module used by driving module
	{
		RTL2830_EXTRA_MODULE Rtl2830;
		RTL2832_EXTRA_MODULE Rtl2832;
	}
	Extra;

	BASE_INTERFACE_MODULE *pBaseInterface;
	I2C_BRIDGE_MODULE *pI2cBridge;


	// Demod register table
	DVBT_REG_ENTRY RegTable[DVBT_REG_TABLE_LEN_MAX];


	// Demod I2C function pointers
	DVBT_DEMOD_FP_SET_REG_PAGE             SetRegPage;
	DVBT_DEMOD_FP_SET_REG_BYTES            SetRegBytes;
	DVBT_DEMOD_FP_GET_REG_BYTES            GetRegBytes;
	DVBT_DEMOD_FP_SET_REG_MASK_BITS        SetRegMaskBits;
	DVBT_DEMOD_FP_GET_REG_MASK_BITS        GetRegMaskBits;
	DVBT_DEMOD_FP_SET_REG_BITS             SetRegBits;
	DVBT_DEMOD_FP_GET_REG_BITS             GetRegBits;
	DVBT_DEMOD_FP_SET_REG_BITS_WITH_PAGE   SetRegBitsWithPage;
	DVBT_DEMOD_FP_GET_REG_BITS_WITH_PAGE   GetRegBitsWithPage;


	// Demod manipulating function pointers
	DVBT_DEMOD_FP_GET_DEMOD_TYPE        GetDemodType;
	DVBT_DEMOD_FP_GET_DEVICE_ADDR       GetDeviceAddr;
	DVBT_DEMOD_FP_GET_CRYSTAL_FREQ_HZ   GetCrystalFreqHz;

	DVBT_DEMOD_FP_IS_CONNECTED_TO_I2C   IsConnectedToI2c;

	DVBT_DEMOD_FP_SOFTWARE_RESET        SoftwareReset;

	DVBT_DEMOD_FP_INITIALIZE            Initialize;
	DVBT_DEMOD_FP_SET_BANDWIDTH_MODE    SetBandwidthMode;
	DVBT_DEMOD_FP_SET_IF_FREQ_HZ        SetIfFreqHz;
	DVBT_DEMOD_FP_SET_SPECTRUM_MODE     SetSpectrumMode;
	DVBT_DEMOD_FP_GET_BANDWIDTH_MODE    GetBandwidthMode;
	DVBT_DEMOD_FP_GET_IF_FREQ_HZ        GetIfFreqHz;
	DVBT_DEMOD_FP_GET_SPECTRUM_MODE     GetSpectrumMode;

	DVBT_DEMOD_FP_IS_TPS_LOCKED         IsTpsLocked;
	DVBT_DEMOD_FP_IS_SIGNAL_LOCKED      IsSignalLocked;

	DVBT_DEMOD_FP_GET_SIGNAL_STRENGTH   GetSignalStrength;
	DVBT_DEMOD_FP_GET_SIGNAL_QUALITY    GetSignalQuality;

	DVBT_DEMOD_FP_GET_BER               GetBer;
	DVBT_DEMOD_FP_GET_SNR_DB            GetSnrDb;

	DVBT_DEMOD_FP_GET_RF_AGC            GetRfAgc;
	DVBT_DEMOD_FP_GET_IF_AGC            GetIfAgc;
	DVBT_DEMOD_FP_GET_DI_AGC            GetDiAgc;

	DVBT_DEMOD_FP_GET_TR_OFFSET_PPM     GetTrOffsetPpm;
	DVBT_DEMOD_FP_GET_CR_OFFSET_HZ      GetCrOffsetHz;

	DVBT_DEMOD_FP_GET_CONSTELLATION     GetConstellation;
	DVBT_DEMOD_FP_GET_HIERARCHY         GetHierarchy;
	DVBT_DEMOD_FP_GET_CODE_RATE_LP      GetCodeRateLp;
	DVBT_DEMOD_FP_GET_CODE_RATE_HP      GetCodeRateHp;
	DVBT_DEMOD_FP_GET_GUARD_INTERVAL    GetGuardInterval;
	DVBT_DEMOD_FP_GET_FFT_MODE          GetFftMode;

	DVBT_DEMOD_FP_UPDATE_FUNCTION       UpdateFunction;
	DVBT_DEMOD_FP_RESET_FUNCTION        ResetFunction;
};







// DVB-T demod default I2C functions
int
dvbt_demod_default_SetRegPage(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned long PageNo
	);

int
dvbt_demod_default_SetRegBytes(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	const unsigned char *pWritingBytes,
	unsigned char ByteNum
	);

int
dvbt_demod_default_GetRegBytes(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	unsigned char *pReadingBytes,
	unsigned char ByteNum
	);

int
dvbt_demod_default_SetRegMaskBits(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	unsigned char Msb,
	unsigned char Lsb,
	const unsigned long WritingValue
	);

int
dvbt_demod_default_GetRegMaskBits(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	unsigned char Msb,
	unsigned char Lsb,
	unsigned long *pReadingValue
	);

int
dvbt_demod_default_SetRegBits(
	DVBT_DEMOD_MODULE *pDemod,
	int RegBitName,
	const unsigned long WritingValue
	);

int
dvbt_demod_default_GetRegBits(
	DVBT_DEMOD_MODULE *pDemod,
	int RegBitName,
	unsigned long *pReadingValue
	);

int
dvbt_demod_default_SetRegBitsWithPage(
	DVBT_DEMOD_MODULE *pDemod,
	int RegBitName,
	const unsigned long WritingValue
	);

int
dvbt_demod_default_GetRegBitsWithPage(
	DVBT_DEMOD_MODULE *pDemod,
	int RegBitName,
	unsigned long *pReadingValue
	);





// DVB-T demod default manipulating functions
void
dvbt_demod_default_GetDemodType(
	DVBT_DEMOD_MODULE *pDemod,
	int *pDemodType
	);

void
dvbt_demod_default_GetDeviceAddr(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned char *pDeviceAddr
	);

void
dvbt_demod_default_GetCrystalFreqHz(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned long *pCrystalFreqHz
	);

int
dvbt_demod_default_GetBandwidthMode(
	DVBT_DEMOD_MODULE *pDemod,
	int *pBandwidthMode
	);

int
dvbt_demod_default_GetIfFreqHz(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned long *pIfFreqHz
	);

int
dvbt_demod_default_GetSpectrumMode(
	DVBT_DEMOD_MODULE *pDemod,
	int *pSpectrumMode
	);







#endif
