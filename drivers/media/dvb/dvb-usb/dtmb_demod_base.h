#ifndef __DTMB_DEMOD_BASE_H
#define __DTMB_DEMOD_BASE_H

/**

@file

@brief   DTMB demod base module definition

DTMB demod base module definitions contains demod module structure, demod funciton pointers, and demod definitions.



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

	DTMB_DEMOD_MODULE *pDemod;
	DTMB_DEMOD_MODULE DtmbDemodModuleMemory;

	I2C_BRIDGE_MODULE I2cBridgeModuleMemory;

	unsigned long IfFreqHz;
	int SpectrumMode;

	int DemodType;
	unsigned char DeviceAddr;
	unsigned long CrystalFreqHz;

	long RfAgc, IfAgc;
	unsigned long DiAgc;

	int Answer;
	long TrOffsetPpm, CrOffsetHz;
	unsigned long BerNum, BerDen;
	double Ber;
	unsigned long PerNum, PerDen;
	double Per;
	long SnrDbNum, SnrDbDen;
	double SnrDb;
	unsigned long SignalStrength, SignalQuality;

	int CarrierMode;
	int PnMode;
	int QamMode;
	int CodeRateMode;
	int TimeInterleaverMode;



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


	// Build DTMB demod XXX module.
	BuildXxxModule(
		&pDemod,
		&DtmbDemodModuleMemory,
		&BaseInterfaceModuleMemory,
		&I2cBridgeModuleMemory,
		0x3e,							// Demod I2C device address is 0x3e in 8-bit format.
		CRYSTAL_FREQ_27000000HZ,		// Demod crystal frequency is 27.0 MHz.
		TS_INTERFACE_SERIAL,			// Demod TS interface mode is serial.
		DIVERSITY_PIP_OFF				// Demod diversity and PIP both are disabled.
		...								// Other arguments by each demod module
		);





	// ==== Initialize DTMB demod and set its parameters =====

	// Initialize demod.
	pDemod->Initialize(pDemod);


	// Set demod parameters. (IF frequency and spectrum mode)
	// Note: In the example:
	//       1. IF frequency is 36.125 MHz.
	//       2. Spectrum mode is SPECTRUM_INVERSE.
	IfFreqHz      = IF_FREQ_36125000HZ;
	SpectrumMode  = SPECTRUM_INVERSE;

	pDemod->SetIfFreqHz(pDemod,      IfFreqHz);
	pDemod->SetSpectrumMode(pDemod,  SpectrumMode);


	// Need to set tuner before demod software reset.
	// The order to set demod and tuner is not important.
	// Note: 1. For 8-bit register address demod, one can use
	//          "pDemod->RegAccess.Addr8Bit.SetRegBitsWithPage(pDemod, DTMB_I2CT_EN_CTRL, 0x1);"
	//          for tuner I2C command forwarding.
	//       2. For 16-bit register address demod, one can use
	//          "pDemod->RegAccess.Addr16Bit.SetRegBits(pDemod, DTMB_I2CT_EN_CTRL, 0x1);"
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





	// ==== Get DTMB demod information =====

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


	// Get demod parameters. (IF frequency and spectrum mode)
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
	pDemod->IsSignalLocked(pDemod, &Answer);


	// Get TR offset (symbol timing offset) in ppm.
	pDemod->GetTrOffsetPpm(pDemod, &TrOffsetPpm);

	// Get CR offset (RF frequency offset) in Hz.
	pDemod->GetCrOffsetHz(pDemod, &CrOffsetHz);


	// Get BER.
	pDemod->GetBer(pDemod, &BerNum, &BerDen);
	Ber = (double)BerNum / (double)BerDen;

	// Get PER.
	pDemod->GetPer(pDemod, &PerNum, &PerDen);
	Per = (double)PerNum / (double)PerDen;

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


	// Get signal information.
	// Note: One can find signal information definitions in the enumerations as follows:
	//       1. DTMB_CARRIER_MODE
	//       2. DTMB_PN_MODE
	//       3. DTMB_QAM_MODE
	//       4. DTMB_CODE_RATE_MODE
	//       5. DTMB_TIME_INTERLEAVER_MODE
	pDemod->GetCarrierMode(pDemod, &CarrierMode);
	pDemod->GetPnMode(pDemod, &PnMode);
	pDemod->GetQamMode(pDemod, &QamMode);
	pDemod->GetCodeRateMode(pDemod, &CodeRateMode);
	pDemod->GetTimeInterleaverMode(pDemod, &TimeInterleaverMode);



	return 0;
}


@endcode

*/


#include "foundation.h"





// Definitions

// Page register address
#define DTMB_DEMOD_PAGE_REG_ADDR		0x0


// Carrier mode
enum DTMB_CARRIER_MODE
{
	DTMB_CARRIER_SINGLE,
	DTMB_CARRIER_MULTI,
};


// PN mode
enum DTMB_PN_MODE
{
	DTMB_PN_420,
	DTMB_PN_595,
	DTMB_PN_945,
};


// QAM mode
enum DTMB_QAM_MODE
{
	DTMB_QAM_4QAM_NR,
	DTMB_QAM_4QAM,
	DTMB_QAM_16QAM,
	DTMB_QAM_32QAM,
	DTMB_QAM_64QAM,
};
#define DTMB_QAM_UNKNOWN		-1


// Code rate mode
enum DTMB_CODE_RATE_MODE
{
	DTMB_CODE_RATE_0P4,
	DTMB_CODE_RATE_0P6,
	DTMB_CODE_RATE_0P8,
};
#define DTMB_CODE_RATE_UNKNOWN		-1


// Time interleaver mode
enum DTMB_TIME_INTERLEAVER_MODE
{
	DTMB_TIME_INTERLEAVER_240,
	DTMB_TIME_INTERLEAVER_720,
};
#define DTMB_TIME_INTERLEAVER_UNKNOWN		-1





// Register entry definitions

// Register entry for 8-bit address
typedef struct
{
	int IsAvailable;
	unsigned long PageNo;
	unsigned char RegStartAddr;
	unsigned char Msb;
	unsigned char Lsb;
}
DTMB_REG_ENTRY_ADDR_8BIT;



// Primary register entry for 8-bit address
typedef struct
{
	int RegBitName;
	unsigned long PageNo;
	unsigned char RegStartAddr;
	unsigned char Msb;
	unsigned char Lsb;
}
DTMB_PRIMARY_REG_ENTRY_ADDR_8BIT;



// Register entry for 16-bit address
typedef struct
{
	int IsAvailable;
	unsigned short RegStartAddr;
	unsigned char Msb;
	unsigned char Lsb;
}
DTMB_REG_ENTRY_ADDR_16BIT;



// Primary register entry for 16-bit address
typedef struct
{
	int RegBitName;
	unsigned short RegStartAddr;
	unsigned char Msb;
	unsigned char Lsb;
}
DTMB_PRIMARY_REG_ENTRY_ADDR_16BIT;





// Register table dependence

// Demod register bit names
enum DTMB_REG_BIT_NAME
{
	// Software reset
	DTMB_SOFT_RST_N,

	// Tuner I2C forwording
	DTMB_I2CT_EN_CTRL,

	// Chip ID
	DTMB_CHIP_ID,


	// IF setting
	DTMB_EN_SP_INV,
	DTMB_EN_DCR,
	DTMB_BBIN_EN,
	DTMB_IFFREQ,

	// AGC setting
	DTMB_AGC_DRIVE_LV,						// for RTL2836B DTMB only
	DTMB_Z_AGC,								// for RTL2836B DTMB only
	DTMB_EN_PGA_MODE,						// for RTL2836B DTMB only
	DTMB_TARGET_VAL,
	DTMB_AAGC_LOOPGAIN1,					// for RTL2836B DTMB only
	DTMB_POLAR_IFAGC,						// for RTL2836B DTMB only
	DTMB_POLAR_RFAGC,						// for RTL2836B DTMB only
	DTMB_INTEGRAL_CNT_LEN,					// for RTL2836B DTMB only
	DTMB_AAGC_LOCK_PGA_HIT_LEN,				// for RTL2836B DTMB only
	DTMB_THD_LOCK_UP,						// for RTL2836B DTMB only
	DTMB_THD_LOCK_DW,						// for RTL2836B DTMB only
	DTMB_THD_UP1,							// for RTL2836B DTMB only
	DTMB_THD_DW1,							// for RTL2836B DTMB only
	DTMB_THD_UP2,							// for RTL2836B DTMB only
	DTMB_THD_DW2,							// for RTL2836B DTMB only
	DTMB_GAIN_PULSE_SPACE_LEN,				// for RTL2836B DTMB only
	DTMB_GAIN_PULSE_HOLD_LEN,				// for RTL2836B DTMB only
	DTMB_GAIN_STEP_SUM_UP_THD,				// for RTL2836B DTMB only
	DTMB_GAIN_STEP_SUM_DW_THD,				// for RTL2836B DTMB only


	// TS interface
	DTMB_SERIAL,
	DTMB_CDIV_PH0,
	DTMB_CDIV_PH1,
	DTMB_SER_LSB,
	DTMB_SYNC_DUR,
	DTMB_ERR_DUR,
	DTMB_FIX_TEI,

	// Signal lock status
	DTMB_TPS_LOCK,
	DTMB_PN_PEAK_EXIST,

	// FSM
	DTMB_FSM_STATE_R,

	// Performance measurement
	DTMB_RO_PKT_ERR_RATE,
	DTMB_EST_SNR,
	DTMB_RO_LDPC_BER,

	// AGC
	DTMB_GAIN_OUT_R,
	DTMB_RF_AGC_VAL,
	DTMB_IF_AGC_VAL,

	// TR and CR
	DTMB_TR_OUT_R,
	DTMB_SFOAQ_OUT_R,
	DTMB_CFO_EST_R,

	// Signal information
	DTMB_EST_CARRIER,
	DTMB_RX_MODE_R,
	DTMB_USE_TPS,

	// GPIO
	DTMB_DMBT_GPIOA_OE,			// For RTL2836B only
	DTMB_DMBT_GPIOB_OE,			// For RTL2836B only
	DTMB_DMBT_OPT_GPIOA_SEL,	// For RTL2836B only
	DTMB_DMBT_OPT_GPIOB_SEL,	// For RTL2836B only
	DTMB_GPIOA_SEL,				// For RTL2836B only
	DTMB_GPIOB_SEL,				// For RTL2836B only

	// AD7
	DTMB_RSSI_R,				// For RTL2836B only
	DTMB_AV_SET7,				// For RTL2836B only
	DTMB_IBX,					// For RTL2836B only
	DTMB_POW_AD7,				// For RTL2836B only
	DTMB_VICM_SET,				// For RTL2836B only
	DTMB_VRC,					// For RTL2836B only
	DTMB_ATT_AD7,				// For RTL2836B only

	// Diversity
	DTMB_DIV_EN,				// For RTL2836B DTMB only
	DTMB_DIV_MODE,				// For RTL2836B DTMB only
	DTMB_DIV_RX_CLK_XOR,		// For RTL2836B DTMB only
	DTMB_DIV_THD_ERR_CMP0,		// For RTL2836B DTMB only
	DTMB_FSM_10L_MSB_3Byte,		// For RTL2836B DTMB only

	// Item terminator
	DTMB_REG_BIT_NAME_ITEM_TERMINATOR,
};



// Register table length definitions
#define DTMB_REG_TABLE_LEN_MAX			DTMB_REG_BIT_NAME_ITEM_TERMINATOR





// DTMB demod module pre-definition
typedef struct DTMB_DEMOD_MODULE_TAG DTMB_DEMOD_MODULE;





/**

@brief   DTMB demod register page setting function pointer

Demod upper level functions will use DTMB_DEMOD_FP_SET_REG_PAGE() to set demod register page.


@param [in]   pDemod   The demod module pointer
@param [in]   PageNo   Page number


@retval   FUNCTION_SUCCESS   Set register page successfully with page number.
@retval   FUNCTION_ERROR     Set register page unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_SET_REG_PAGE() with the corresponding function.


@par Example:
@code


#include "demod_pseudo.h"


int main(void)
{
	DTMB_DEMOD_MODULE *pDemod;
	DTMB_DEMOD_MODULE DtmbDemodModuleMemory;
	PSEUDO_EXTRA_MODULE PseudoExtraModuleMemory;


	// Build pseudo demod module.
	BuildPseudoDemodModule(&pDemod, &DtmbDemodModuleMemory, &PseudoExtraModuleMemory);

	...

	// Set demod register page with page number 2.
	pDemod->SetRegPage(pDemod, 2);

	...

	return 0;
}


@endcode

*/
typedef int
(*DTMB_DEMOD_FP_ADDR_8BIT_SET_REG_PAGE)(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned long PageNo
	);





/**

@brief   DTMB demod register byte setting function pointer

Demod upper level functions will use DTMB_DEMOD_FP_SET_REG_BYTES() to set demod register bytes.


@param [in]   pDemod          The demod module pointer
@param [in]   RegStartAddr    Demod register start address
@param [in]   pWritingBytes   Pointer to writing bytes
@param [in]   ByteNum         Writing byte number


@retval   FUNCTION_SUCCESS   Set demod register bytes successfully with writing bytes.
@retval   FUNCTION_ERROR     Set demod register bytes unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_SET_REG_BYTES() with the corresponding function.
	-# Need to set register page by DTMB_DEMOD_FP_SET_REG_PAGE() before using DTMB_DEMOD_FP_SET_REG_BYTES().


@see   DTMB_DEMOD_FP_SET_REG_PAGE, DTMB_DEMOD_FP_GET_REG_BYTES



@par Example:
@code


#include "demod_pseudo.h"


int main(void)
{
	DTMB_DEMOD_MODULE *pDemod;
	DTMB_DEMOD_MODULE DtmbDemodModuleMemory;
	PSEUDO_EXTRA_MODULE PseudoExtraModuleMemory;
	unsigned char WritingBytes[10];


	// Build pseudo demod module.
	BuildPseudoDemodModule(&pDemod, &DtmbDemodModuleMemory, &PseudoExtraModuleMemory);

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
(*DTMB_DEMOD_FP_ADDR_8BIT_SET_REG_BYTES)(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	const unsigned char *pWritingBytes,
	unsigned long ByteNum
	);

typedef int
(*DTMB_DEMOD_FP_ADDR_16BIT_SET_REG_BYTES)(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned short RegStartAddr,
	const unsigned char *pWritingBytes,
	unsigned long ByteNum
	);





/**

@brief   DTMB demod register byte getting function pointer

Demod upper level functions will use DTMB_DEMOD_FP_GET_REG_BYTES() to get demod register bytes.


@param [in]    pDemod          The demod module pointer
@param [in]    RegStartAddr    Demod register start address
@param [out]   pReadingBytes   Pointer to an allocated memory for storing reading bytes
@param [in]    ByteNum         Reading byte number


@retval   FUNCTION_SUCCESS   Get demod register bytes successfully with reading byte number.
@retval   FUNCTION_ERROR     Get demod register bytes unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_GET_REG_BYTES() with the corresponding function.
	-# Need to set register page by DTMB_DEMOD_FP_SET_REG_PAGE() before using DTMB_DEMOD_FP_GET_REG_BYTES().


@see   DTMB_DEMOD_FP_SET_REG_PAGE, DTMB_DEMOD_FP_SET_REG_BYTES



@par Example:
@code


#include "demod_pseudo.h"


int main(void)
{
	DTMB_DEMOD_MODULE *pDemod;
	DTMB_DEMOD_MODULE DtmbDemodModuleMemory;
	PSEUDO_EXTRA_MODULE PseudoExtraModuleMemory;
	unsigned char ReadingBytes[10];


	// Build pseudo demod module.
	BuildPseudoDemodModule(&pDemod, &DtmbDemodModuleMemory, &PseudoExtraModuleMemory);

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
(*DTMB_DEMOD_FP_ADDR_8BIT_GET_REG_BYTES)(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	unsigned char *pReadingBytes,
	unsigned long ByteNum
	);

typedef int
(*DTMB_DEMOD_FP_ADDR_16BIT_GET_REG_BYTES)(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned short RegStartAddr,
	unsigned char *pReadingBytes,
	unsigned long ByteNum
	);





/**

@brief   DTMB demod register mask bits setting function pointer

Demod upper level functions will use DTMB_DEMOD_FP_SET_REG_MASK_BITS() to set demod register mask bits.


@param [in]   pDemod         The demod module pointer
@param [in]   RegStartAddr   Demod register start address
@param [in]   Msb            Mask MSB with 0-based index
@param [in]   Lsb            Mask LSB with 0-based index
@param [in]   WritingValue   The mask bits writing value


@retval   FUNCTION_SUCCESS   Set demod register mask bits successfully with writing value.
@retval   FUNCTION_ERROR     Set demod register mask bits unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_SET_REG_MASK_BITS() with the corresponding function.
	-# Need to set register page by DTMB_DEMOD_FP_SET_REG_PAGE() before using DTMB_DEMOD_FP_SET_REG_MASK_BITS().
	-# The constraints of DTMB_DEMOD_FP_SET_REG_MASK_BITS() function usage are described as follows:
		-# The mask MSB and LSB must be 0~31.
		-# The mask MSB must be greater than or equal to LSB.


@see   DTMB_DEMOD_FP_SET_REG_PAGE, DTMB_DEMOD_FP_GET_REG_MASK_BITS



@par Example:
@code


#include "demod_pseudo.h"


int main(void)
{
	DTMB_DEMOD_MODULE *pDemod;
	DTMB_DEMOD_MODULE DtmbDemodModuleMemory;
	PSEUDO_EXTRA_MODULE PseudoExtraModuleMemory;


	// Build pseudo demod module.
	BuildPseudoDemodModule(&pDemod, &DtmbDemodModuleMemory, &PseudoExtraModuleMemory);

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
(*DTMB_DEMOD_FP_ADDR_8BIT_SET_REG_MASK_BITS)(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	unsigned char Msb,
	unsigned char Lsb,
	const unsigned long WritingValue
	);

typedef int
(*DTMB_DEMOD_FP_ADDR_16BIT_SET_REG_MASK_BITS)(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned short RegStartAddr,
	unsigned char Msb,
	unsigned char Lsb,
	const unsigned long WritingValue
	);





/**

@brief   DTMB demod register mask bits getting function pointer

Demod upper level functions will use DTMB_DEMOD_FP_GET_REG_MASK_BITS() to get demod register mask bits.


@param [in]    pDemod          The demod module pointer
@param [in]    RegStartAddr    Demod register start address
@param [in]    Msb             Mask MSB with 0-based index
@param [in]    Lsb             Mask LSB with 0-based index
@param [out]   pReadingValue   Pointer to an allocated memory for storing reading value


@retval   FUNCTION_SUCCESS   Get demod register mask bits successfully.
@retval   FUNCTION_ERROR     Get demod register mask bits unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_GET_REG_MASK_BITS() with the corresponding function.
	-# Need to set register page by DTMB_DEMOD_FP_SET_REG_PAGE() before using DTMB_DEMOD_FP_GET_REG_MASK_BITS().
	-# The constraints of DTMB_DEMOD_FP_GET_REG_MASK_BITS() function usage are described as follows:
		-# The mask MSB and LSB must be 0~31.
		-# The mask MSB must be greater than or equal to LSB.


@see   DTMB_DEMOD_FP_SET_REG_PAGE, DTMB_DEMOD_FP_SET_REG_MASK_BITS



@par Example:
@code


#include "demod_pseudo.h"


int main(void)
{
	DTMB_DEMOD_MODULE *pDemod;
	DTMB_DEMOD_MODULE DtmbDemodModuleMemory;
	PSEUDO_EXTRA_MODULE PseudoExtraModuleMemory;
	unsigned long ReadingValue;


	// Build pseudo demod module.
	BuildPseudoDemodModule(&pDemod, &DtmbDemodModuleMemory, &PseudoExtraModuleMemory);

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
(*DTMB_DEMOD_FP_ADDR_8BIT_GET_REG_MASK_BITS)(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	unsigned char Msb,
	unsigned char Lsb,
	unsigned long *pReadingValue
	);

typedef int
(*DTMB_DEMOD_FP_ADDR_16BIT_GET_REG_MASK_BITS)(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned short RegStartAddr,
	unsigned char Msb,
	unsigned char Lsb,
	unsigned long *pReadingValue
	);





/**

@brief   DTMB demod register bits setting function pointer

Demod upper level functions will use DTMB_DEMOD_FP_SET_REG_BITS() to set demod register bits with bit name.


@param [in]   pDemod         The demod module pointer
@param [in]   RegBitName     Pre-defined demod register bit name
@param [in]   WritingValue   The mask bits writing value


@retval   FUNCTION_SUCCESS   Set demod register bits successfully with bit name and writing value.
@retval   FUNCTION_ERROR     Set demod register bits unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_SET_REG_BITS() with the corresponding function.
	-# Need to set register page before using DTMB_DEMOD_FP_SET_REG_BITS().
	-# Register bit names are pre-defined keys for bit access, and one can find these in demod header file.


@see   DTMB_DEMOD_FP_SET_REG_PAGE, DTMB_DEMOD_FP_GET_REG_BITS



@par Example:
@code


#include "demod_pseudo.h"


int main(void)
{
	DTMB_DEMOD_MODULE *pDemod;
	DTMB_DEMOD_MODULE DtmbDemodModuleMemory;
	PSEUDO_EXTRA_MODULE PseudoExtraModuleMemory;


	// Build pseudo demod module.
	BuildPseudoDemodModule(&pDemod, &DtmbDemodModuleMemory, &PseudoExtraModuleMemory);

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
(*DTMB_DEMOD_FP_ADDR_8BIT_SET_REG_BITS)(
	DTMB_DEMOD_MODULE *pDemod,
	int RegBitName,
	const unsigned long WritingValue
	);

typedef int
(*DTMB_DEMOD_FP_ADDR_16BIT_SET_REG_BITS)(
	DTMB_DEMOD_MODULE *pDemod,
	int RegBitName,
	const unsigned long WritingValue
	);





/**

@brief   DTMB demod register bits getting function pointer

Demod upper level functions will use DTMB_DEMOD_FP_GET_REG_BITS() to get demod register bits with bit name.


@param [in]    pDemod          The demod module pointer
@param [in]    RegBitName      Pre-defined demod register bit name
@param [out]   pReadingValue   Pointer to an allocated memory for storing reading value


@retval   FUNCTION_SUCCESS   Get demod register bits successfully with bit name.
@retval   FUNCTION_ERROR     Get demod register bits unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_GET_REG_BITS() with the corresponding function.
	-# Need to set register page before using DTMB_DEMOD_FP_GET_REG_BITS().
	-# Register bit names are pre-defined keys for bit access, and one can find these in demod header file.


@see   DTMB_DEMOD_FP_SET_REG_PAGE, DTMB_DEMOD_FP_SET_REG_BITS



@par Example:
@code


#include "demod_pseudo.h"


int main(void)
{
	DTMB_DEMOD_MODULE *pDemod;
	DTMB_DEMOD_MODULE DtmbDemodModuleMemory;
	PSEUDO_EXTRA_MODULE PseudoExtraModuleMemory;
	unsigned long ReadingValue;


	// Build pseudo demod module.
	BuildPseudoDemodModule(&pDemod, &DtmbDemodModuleMemory, &PseudoExtraModuleMemory);

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
(*DTMB_DEMOD_FP_ADDR_8BIT_GET_REG_BITS)(
	DTMB_DEMOD_MODULE *pDemod,
	int RegBitName,
	unsigned long *pReadingValue
	);

typedef int
(*DTMB_DEMOD_FP_ADDR_16BIT_GET_REG_BITS)(
	DTMB_DEMOD_MODULE *pDemod,
	int RegBitName,
	unsigned long *pReadingValue
	);





/**

@brief   DTMB demod register bits setting function pointer (with page setting)

Demod upper level functions will use DTMB_DEMOD_FP_SET_REG_BITS_WITH_PAGE() to set demod register bits with bit name and
page setting.


@param [in]   pDemod         The demod module pointer
@param [in]   RegBitName     Pre-defined demod register bit name
@param [in]   WritingValue   The mask bits writing value


@retval   FUNCTION_SUCCESS   Set demod register bits successfully with bit name, page setting, and writing value.
@retval   FUNCTION_ERROR     Set demod register bits unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_SET_REG_BITS_WITH_PAGE() with the corresponding function.
	-# Don't need to set register page before using DTMB_DEMOD_FP_SET_REG_BITS_WITH_PAGE().
	-# Register bit names are pre-defined keys for bit access, and one can find these in demod header file.


@see   DTMB_DEMOD_FP_GET_REG_BITS_WITH_PAGE



@par Example:
@code


#include "demod_pseudo.h"


int main(void)
{
	DTMB_DEMOD_MODULE *pDemod;
	DTMB_DEMOD_MODULE DtmbDemodModuleMemory;
	PSEUDO_EXTRA_MODULE PseudoExtraModuleMemory;


	// Build pseudo demod module.
	BuildPseudoDemodModule(&pDemod, &DtmbDemodModuleMemory, &PseudoExtraModuleMemory);

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
(*DTMB_DEMOD_FP_ADDR_8BIT_SET_REG_BITS_WITH_PAGE)(
	DTMB_DEMOD_MODULE *pDemod,
	int RegBitName,
	const unsigned long WritingValue
	);





/**

@brief   DTMB demod register bits getting function pointer (with page setting)

Demod upper level functions will use DTMB_DEMOD_FPT_GET_REG_BITS_WITH_PAGE() to get demod register bits with bit name and
page setting.


@param [in]    pDemod          The demod module pointer
@param [in]    RegBitName      Pre-defined demod register bit name
@param [out]   pReadingValue   Pointer to an allocated memory for storing reading value


@retval   FUNCTION_SUCCESS   Get demod register bits successfully with bit name and page setting.
@retval   FUNCTION_ERROR     Get demod register bits unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_GET_REG_BITS_WITH_PAGE() with the corresponding function.
	-# Don't need to set register page before using DTMB_DEMOD_FP_GET_REG_BITS_WITH_PAGE().
	-# Register bit names are pre-defined keys for bit access, and one can find these in demod header file.


@see   DTMB_DEMOD_FP_SET_REG_BITS_WITH_PAGE



@par Example:
@code


#include "demod_pseudo.h"


int main(void)
{
	DTMB_DEMOD_MODULE *pDemod;
	DTMB_DEMOD_MODULE DtmbDemodModuleMemory;
	PSEUDO_EXTRA_MODULE PseudoExtraModuleMemory;
	unsigned long ReadingValue;


	// Build pseudo demod module.
	BuildPseudoDemodModule(&pDemod, &DtmbDemodModuleMemory, &PseudoExtraModuleMemory);

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
(*DTMB_DEMOD_FP_ADDR_8BIT_GET_REG_BITS_WITH_PAGE)(
	DTMB_DEMOD_MODULE *pDemod,
	int RegBitName,
	unsigned long *pReadingValue
	);





// Demod register access for 8-bit address
typedef struct
{
	DTMB_DEMOD_FP_ADDR_8BIT_SET_REG_PAGE             SetRegPage;
	DTMB_DEMOD_FP_ADDR_8BIT_SET_REG_BYTES            SetRegBytes;
	DTMB_DEMOD_FP_ADDR_8BIT_GET_REG_BYTES            GetRegBytes;
	DTMB_DEMOD_FP_ADDR_8BIT_SET_REG_MASK_BITS        SetRegMaskBits;
	DTMB_DEMOD_FP_ADDR_8BIT_GET_REG_MASK_BITS        GetRegMaskBits;
	DTMB_DEMOD_FP_ADDR_8BIT_SET_REG_BITS             SetRegBits;
	DTMB_DEMOD_FP_ADDR_8BIT_GET_REG_BITS             GetRegBits;
	DTMB_DEMOD_FP_ADDR_8BIT_SET_REG_BITS_WITH_PAGE   SetRegBitsWithPage;
	DTMB_DEMOD_FP_ADDR_8BIT_GET_REG_BITS_WITH_PAGE   GetRegBitsWithPage;
}
DTMB_DEMOD_REG_ACCESS_ADDR_8BIT;





// Demod register access for 16-bit address
typedef struct
{
	DTMB_DEMOD_FP_ADDR_16BIT_SET_REG_BYTES       SetRegBytes;
	DTMB_DEMOD_FP_ADDR_16BIT_GET_REG_BYTES       GetRegBytes;
	DTMB_DEMOD_FP_ADDR_16BIT_SET_REG_MASK_BITS   SetRegMaskBits;
	DTMB_DEMOD_FP_ADDR_16BIT_GET_REG_MASK_BITS   GetRegMaskBits;
	DTMB_DEMOD_FP_ADDR_16BIT_SET_REG_BITS        SetRegBits;
	DTMB_DEMOD_FP_ADDR_16BIT_GET_REG_BITS        GetRegBits;
}
DTMB_DEMOD_REG_ACCESS_ADDR_16BIT;





/**

@brief   DTMB demod type getting function pointer

One can use DTMB_DEMOD_FP_GET_DEMOD_TYPE() to get DTMB demod type.


@param [in]    pDemod       The demod module pointer
@param [out]   pDemodType   Pointer to an allocated memory for storing demod type


@note
	-# Demod building function will set DTMB_DEMOD_FP_GET_DEMOD_TYPE() with the corresponding function.


@see   MODULE_TYPE

*/
typedef void
(*DTMB_DEMOD_FP_GET_DEMOD_TYPE)(
	DTMB_DEMOD_MODULE *pDemod,
	int *pDemodType
	);





/**

@brief   DTMB demod I2C device address getting function pointer

One can use DTMB_DEMOD_FP_GET_DEVICE_ADDR() to get DTMB demod I2C device address.


@param [in]    pDemod        The demod module pointer
@param [out]   pDeviceAddr   Pointer to an allocated memory for storing demod I2C device address


@retval   FUNCTION_SUCCESS   Get demod device address successfully.
@retval   FUNCTION_ERROR     Get demod device address unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_GET_DEVICE_ADDR() with the corresponding function.

*/
typedef void
(*DTMB_DEMOD_FP_GET_DEVICE_ADDR)(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned char *pDeviceAddr
	);





/**

@brief   DTMB demod crystal frequency getting function pointer

One can use DTMB_DEMOD_FP_GET_CRYSTAL_FREQ_HZ() to get DTMB demod crystal frequency in Hz.


@param [in]    pDemod           The demod module pointer
@param [out]   pCrystalFreqHz   Pointer to an allocated memory for storing demod crystal frequency in Hz


@retval   FUNCTION_SUCCESS   Get demod crystal frequency successfully.
@retval   FUNCTION_ERROR     Get demod crystal frequency unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_GET_CRYSTAL_FREQ_HZ() with the corresponding function.

*/
typedef void
(*DTMB_DEMOD_FP_GET_CRYSTAL_FREQ_HZ)(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned long *pCrystalFreqHz
	);





/**

@brief   DTMB demod I2C bus connection asking function pointer

One can use DTMB_DEMOD_FP_IS_CONNECTED_TO_I2C() to ask DTMB demod if it is connected to I2C bus.


@param [in]    pDemod    The demod module pointer
@param [out]   pAnswer   Pointer to an allocated memory for storing answer


@note
	-# Demod building function will set DTMB_DEMOD_FP_IS_CONNECTED_TO_I2C() with the corresponding function.

*/
typedef void
(*DTMB_DEMOD_FP_IS_CONNECTED_TO_I2C)(
	DTMB_DEMOD_MODULE *pDemod,
	int *pAnswer
	);





/**

@brief   DTMB demod software resetting function pointer

One can use DTMB_DEMOD_FP_SOFTWARE_RESET() to reset DTMB demod by software reset.


@param [in]   pDemod   The demod module pointer


@retval   FUNCTION_SUCCESS   Reset demod by software reset successfully.
@retval   FUNCTION_ERROR     Reset demod by software reset unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_SOFTWARE_RESET() with the corresponding function.

*/
typedef int
(*DTMB_DEMOD_FP_SOFTWARE_RESET)(
	DTMB_DEMOD_MODULE *pDemod
	);





/**

@brief   DTMB demod initializing function pointer

One can use DTMB_DEMOD_FP_INITIALIZE() to initialie DTMB demod.


@param [in]   pDemod   The demod module pointer


@retval   FUNCTION_SUCCESS   Initialize demod successfully.
@retval   FUNCTION_ERROR     Initialize demod unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_INITIALIZE() with the corresponding function.

*/
typedef int
(*DTMB_DEMOD_FP_INITIALIZE)(
	DTMB_DEMOD_MODULE *pDemod
	);





/**

@brief   DTMB demod IF frequency setting function pointer

One can use DTMB_DEMOD_FP_SET_IF_FREQ_HZ() to set DTMB demod IF frequency in Hz.


@param [in]   pDemod     The demod module pointer
@param [in]   IfFreqHz   IF frequency in Hz for setting


@retval   FUNCTION_SUCCESS   Set demod IF frequency successfully.
@retval   FUNCTION_ERROR     Set demod IF frequency unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_SET_IF_FREQ_HZ() with the corresponding function.


@see   IF_FREQ_HZ

*/
typedef int
(*DTMB_DEMOD_FP_SET_IF_FREQ_HZ)(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned long IfFreqHz
	);





/**

@brief   DTMB demod spectrum mode setting function pointer

One can use DTMB_DEMOD_FP_SET_SPECTRUM_MODE() to set DTMB demod spectrum mode.


@param [in]   pDemod         The demod module pointer
@param [in]   SpectrumMode   Spectrum mode for setting


@retval   FUNCTION_SUCCESS   Set demod spectrum mode successfully.
@retval   FUNCTION_ERROR     Set demod spectrum mode unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_SET_SPECTRUM_MODE() with the corresponding function.


@see   SPECTRUM_MODE

*/
typedef int
(*DTMB_DEMOD_FP_SET_SPECTRUM_MODE)(
	DTMB_DEMOD_MODULE *pDemod,
	int SpectrumMode
	);





/**

@brief   DTMB demod IF frequency getting function pointer

One can use DTMB_DEMOD_FP_GET_IF_FREQ_HZ() to get DTMB demod IF frequency in Hz.


@param [in]    pDemod      The demod module pointer
@param [out]   pIfFreqHz   Pointer to an allocated memory for storing demod IF frequency in Hz


@retval   FUNCTION_SUCCESS   Get demod IF frequency successfully.
@retval   FUNCTION_ERROR     Get demod IF frequency unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_GET_IF_FREQ_HZ() with the corresponding function.


@see   IF_FREQ_HZ

*/
typedef int
(*DTMB_DEMOD_FP_GET_IF_FREQ_HZ)(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned long *pIfFreqHz
	);





/**

@brief   DTMB demod spectrum mode getting function pointer

One can use DTMB_DEMOD_FP_GET_SPECTRUM_MODE() to get DTMB demod spectrum mode.


@param [in]    pDemod          The demod module pointer
@param [out]   pSpectrumMode   Pointer to an allocated memory for storing demod spectrum mode


@retval   FUNCTION_SUCCESS   Get demod spectrum mode successfully.
@retval   FUNCTION_ERROR     Get demod spectrum mode unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_GET_SPECTRUM_MODE() with the corresponding function.


@see   SPECTRUM_MODE

*/
typedef int
(*DTMB_DEMOD_FP_GET_SPECTRUM_MODE)(
	DTMB_DEMOD_MODULE *pDemod,
	int *pSpectrumMode
	);





/**

@brief   DTMB demod signal lock asking function pointer

One can use DTMB_DEMOD_FP_IS_SIGNAL_LOCKED() to ask DTMB demod if it is signal-locked.


@param [in]    pDemod    The demod module pointer
@param [out]   pAnswer   Pointer to an allocated memory for storing answer


@retval   FUNCTION_SUCCESS   Perform signal lock asking to demod successfully.
@retval   FUNCTION_ERROR     Perform signal lock asking to demod unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_IS_SIGNAL_LOCKED() with the corresponding function.

*/
typedef int
(*DTMB_DEMOD_FP_IS_SIGNAL_LOCKED)(
	DTMB_DEMOD_MODULE *pDemod,
	int *pAnswer
	);





/**

@brief   DTMB demod signal strength getting function pointer

One can use DTMB_DEMOD_FP_GET_SIGNAL_STRENGTH() to get signal strength.


@param [in]    pDemod            The demod module pointer
@param [out]   pSignalStrength   Pointer to an allocated memory for storing signal strength (value = 0 ~ 100)


@retval   FUNCTION_SUCCESS   Get demod signal strength successfully.
@retval   FUNCTION_ERROR     Get demod signal strength unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_GET_SIGNAL_STRENGTH() with the corresponding function.

*/
typedef int
(*DTMB_DEMOD_FP_GET_SIGNAL_STRENGTH)(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned long *pSignalStrength
	);





/**

@brief   DTMB demod signal quality getting function pointer

One can use DTMB_DEMOD_FP_GET_SIGNAL_QUALITY() to get signal quality.


@param [in]    pDemod           The demod module pointer
@param [out]   pSignalQuality   Pointer to an allocated memory for storing signal quality (value = 0 ~ 100)


@retval   FUNCTION_SUCCESS   Get demod signal quality successfully.
@retval   FUNCTION_ERROR     Get demod signal quality unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_GET_SIGNAL_QUALITY() with the corresponding function.

*/
typedef int
(*DTMB_DEMOD_FP_GET_SIGNAL_QUALITY)(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned long *pSignalQuality
	);





/**

@brief   DTMB demod BER getting function pointer

One can use DTMB_DEMOD_FP_GET_BER() to get BER.


@param [in]    pDemod          The demod module pointer
@param [out]   pBerNum         Pointer to an allocated memory for storing BER numerator
@param [out]   pBerDen         Pointer to an allocated memory for storing BER denominator


@retval   FUNCTION_SUCCESS   Get demod error rate value successfully.
@retval   FUNCTION_ERROR     Get demod error rate value unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_GET_BER() with the corresponding function.

*/
typedef int
(*DTMB_DEMOD_FP_GET_BER)(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned long *pBerNum,
	unsigned long *pBerDen
	);





/**

@brief   DTMB demod PER getting function pointer

One can use DTMB_DEMOD_FP_GET_PER() to get PER.


@param [in]    pDemod          The demod module pointer
@param [out]   pPerNum         Pointer to an allocated memory for storing PER numerator
@param [out]   pPerDen         Pointer to an allocated memory for storing PER denominator


@retval   FUNCTION_SUCCESS   Get demod error rate value successfully.
@retval   FUNCTION_ERROR     Get demod error rate value unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_GET_PER() with the corresponding function.

*/
typedef int
(*DTMB_DEMOD_FP_GET_PER)(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned long *pPerNum,
	unsigned long *pPerDen
	);





/**

@brief   DTMB demod SNR getting function pointer

One can use DTMB_DEMOD_FP_GET_SNR_DB() to get SNR in dB.


@param [in]    pDemod      The demod module pointer
@param [out]   pSnrDbNum   Pointer to an allocated memory for storing SNR dB numerator
@param [out]   pSnrDbDen   Pointer to an allocated memory for storing SNR dB denominator


@retval   FUNCTION_SUCCESS   Get demod SNR successfully.
@retval   FUNCTION_ERROR     Get demod SNR unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_GET_SNR_DB() with the corresponding function.

*/
typedef int
(*DTMB_DEMOD_FP_GET_SNR_DB)(
	DTMB_DEMOD_MODULE *pDemod,
	long *pSnrDbNum,
	long *pSnrDbDen
	);





/**

@brief   DTMB demod RF AGC getting function pointer

One can use DTMB_DEMOD_FP_GET_RF_AGC() to get DTMB demod RF AGC value.


@param [in]    pDemod   The demod module pointer
@param [out]   pRfAgc   Pointer to an allocated memory for storing RF AGC value


@retval   FUNCTION_SUCCESS   Get demod RF AGC value successfully.
@retval   FUNCTION_ERROR     Get demod RF AGC value unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_GET_RF_AGC() with the corresponding function.
	-# The range of RF AGC value is 0 ~ (pow(2, 14) - 1).

*/
typedef int
(*DTMB_DEMOD_FP_GET_RF_AGC)(
	DTMB_DEMOD_MODULE *pDemod,
	long *pRfAgc
	);





/**

@brief   DTMB demod IF AGC getting function pointer

One can use DTMB_DEMOD_FP_GET_IF_AGC() to get DTMB demod IF AGC value.


@param [in]    pDemod   The demod module pointer
@param [out]   pIfAgc   Pointer to an allocated memory for storing IF AGC value


@retval   FUNCTION_SUCCESS   Get demod IF AGC value successfully.
@retval   FUNCTION_ERROR     Get demod IF AGC value unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_GET_IF_AGC() with the corresponding function.
	-# The range of IF AGC value is 0 ~ (pow(2, 14) - 1).

*/
typedef int
(*DTMB_DEMOD_FP_GET_IF_AGC)(
	DTMB_DEMOD_MODULE *pDemod,
	long *pIfAgc
	);





/**

@brief   DTMB demod digital AGC getting function pointer

One can use DTMB_DEMOD_FP_GET_DI_AGC() to get DTMB demod digital AGC value.


@param [in]    pDemod   The demod module pointer
@param [out]   pDiAgc   Pointer to an allocated memory for storing digital AGC value


@retval   FUNCTION_SUCCESS   Get demod digital AGC value successfully.
@retval   FUNCTION_ERROR     Get demod digital AGC value unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_GET_DI_AGC() with the corresponding function.
	-# The range of digital AGC value is 0 ~ (pow(2, 12) - 1).

*/
typedef int
(*DTMB_DEMOD_FP_GET_DI_AGC)(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned long *pDiAgc
	);





/**

@brief   DTMB demod TR offset getting function pointer

One can use DTMB_DEMOD_FP_GET_TR_OFFSET_PPM() to get TR offset in ppm.


@param [in]    pDemod         The demod module pointer
@param [out]   pTrOffsetPpm   Pointer to an allocated memory for storing TR offset in ppm


@retval   FUNCTION_SUCCESS   Get demod TR offset successfully.
@retval   FUNCTION_ERROR     Get demod TR offset unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_GET_TR_OFFSET_PPM() with the corresponding function.

*/
typedef int
(*DTMB_DEMOD_FP_GET_TR_OFFSET_PPM)(
	DTMB_DEMOD_MODULE *pDemod,
	long *pTrOffsetPpm
	);





/**

@brief   DTMB demod CR offset getting function pointer

One can use DTMB_DEMOD_FP_GET_CR_OFFSET_HZ() to get CR offset in Hz.


@param [in]    pDemod        The demod module pointer
@param [out]   pCrOffsetHz   Pointer to an allocated memory for storing CR offset in Hz


@retval   FUNCTION_SUCCESS   Get demod CR offset successfully.
@retval   FUNCTION_ERROR     Get demod CR offset unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_GET_CR_OFFSET_HZ() with the corresponding function.

*/
typedef int
(*DTMB_DEMOD_FP_GET_CR_OFFSET_HZ)(
	DTMB_DEMOD_MODULE *pDemod,
	long *pCrOffsetHz
	);





/**

@brief   DTMB demod carrier mode getting function pointer

One can use DTMB_DEMOD_FP_GET_CARRIER_MODE() to get DTMB demod carrier mode.


@param [in]    pDemod         The demod module pointer
@param [out]   pCarrierMode   Pointer to an allocated memory for storing demod carrier mode


@retval   FUNCTION_SUCCESS   Get demod carrier mode successfully.
@retval   FUNCTION_ERROR     Get demod carrier mode unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_GET_CARRIER_MODE() with the corresponding function.


@see   DTMB_CARRIER_MODE

*/
typedef int
(*DTMB_DEMOD_FP_GET_CARRIER_MODE)(
	DTMB_DEMOD_MODULE *pDemod,
	int *pCarrierMode
	);





/**

@brief   DTMB demod PN mode getting function pointer

One can use DTMB_DEMOD_FP_GET_PN_MODE() to get DTMB demod PN mode.


@param [in]    pDemod    The demod module pointer
@param [out]   pPnMode   Pointer to an allocated memory for storing demod PN mode


@retval   FUNCTION_SUCCESS   Get demod PN mode successfully.
@retval   FUNCTION_ERROR     Get demod PN mode unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_GET_PN_MODE() with the corresponding function.


@see   DTMB_PN_MODE

*/
typedef int
(*DTMB_DEMOD_FP_GET_PN_MODE)(
	DTMB_DEMOD_MODULE *pDemod,
	int *pPnMode
	);





/**

@brief   DTMB demod QAM mode getting function pointer

One can use DTMB_DEMOD_FP_GET_QAM_MODE() to get DTMB demod QAM mode.


@param [in]    pDemod     The demod module pointer
@param [out]   pQamMode   Pointer to an allocated memory for storing demod QAM mode


@retval   FUNCTION_SUCCESS   Get demod QAM mode successfully.
@retval   FUNCTION_ERROR     Get demod QAM mode unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_GET_QAM_MODE() with the corresponding function.


@see   DTMB_QAM_MODE

*/
typedef int
(*DTMB_DEMOD_FP_GET_QAM_MODE)(
	DTMB_DEMOD_MODULE *pDemod,
	int *pQamMode
	);





/**

@brief   DTMB demod code rate mode getting function pointer

One can use DTMB_DEMOD_FP_GET_CODE_RATE_MODE() to get DTMB demod code rate mode.


@param [in]    pDemod          The demod module pointer
@param [out]   pCodeRateMode   Pointer to an allocated memory for storing demod code rate mode


@retval   FUNCTION_SUCCESS   Get demod code rate mode successfully.
@retval   FUNCTION_ERROR     Get demod code rate mode unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_GET_CODE_RATE_MODE() with the corresponding function.


@see   DTMB_CODE_RATE_MODE

*/
typedef int
(*DTMB_DEMOD_FP_GET_CODE_RATE_MODE)(
	DTMB_DEMOD_MODULE *pDemod,
	int *pCodeRateMode
	);





/**

@brief   DTMB demod time interleaver mode getting function pointer

One can use DTMB_DEMOD_FP_GET_TIME_INTERLEAVER_MODE() to get DTMB demod time interleaver mode.


@param [in]    pDemod                 The demod module pointer
@param [out]   pTimeInterleaverMode   Pointer to an allocated memory for storing demod time interleaver mode


@retval   FUNCTION_SUCCESS   Get demod time interleaver mode successfully.
@retval   FUNCTION_ERROR     Get demod time interleaver mode unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_GET_TIME_INTERLEAVER_MODE() with the corresponding function.


@see   DTMB_TIME_INTERLEAVER_MODE

*/
typedef int
(*DTMB_DEMOD_FP_GET_TIME_INTERLEAVER_MODE)(
	DTMB_DEMOD_MODULE *pDemod,
	int *pTimeInterleaverMode
	);





/**

@brief   DTMB demod updating function pointer

One can use DTMB_DEMOD_FP_UPDATE_FUNCTION() to update demod register setting.


@param [in]   pDemod   The demod module pointer


@retval   FUNCTION_SUCCESS   Update demod setting successfully.
@retval   FUNCTION_ERROR     Update demod setting unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_UPDATE_FUNCTION() with the corresponding function.



@par Example:
@code


#include "demod_pseudo.h"


int main(void)
{
	DTMB_DEMOD_MODULE *pDemod;
	DTMB_DEMOD_MODULE DtmbDemodModuleMemory;
	PSEUDO_EXTRA_MODULE PseudoExtraModuleMemory;


	// Build pseudo demod module.
	BuildPseudoDemodModule(&pDemod, &DtmbDemodModuleMemory, &PseudoExtraModuleMemory);

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
(*DTMB_DEMOD_FP_UPDATE_FUNCTION)(
	DTMB_DEMOD_MODULE *pDemod
	);





/**

@brief   DTMB demod reseting function pointer

One can use DTMB_DEMOD_FP_RESET_FUNCTION() to reset demod register setting.


@param [in]   pDemod   The demod module pointer


@retval   FUNCTION_SUCCESS   Reset demod setting successfully.
@retval   FUNCTION_ERROR     Reset demod setting unsuccessfully.


@note
	-# Demod building function will set DTMB_DEMOD_FP_RESET_FUNCTION() with the corresponding function.



@par Example:
@code


#include "demod_pseudo.h"


int main(void)
{
	DTMB_DEMOD_MODULE *pDemod;
	DTMB_DEMOD_MODULE DtmbDemodModuleMemory;
	PSEUDO_EXTRA_MODULE PseudoExtraModuleMemory;


	// Build pseudo demod module.
	BuildPseudoDemodModule(&pDemod, &DtmbDemodModuleMemory, &PseudoExtraModuleMemory);

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
(*DTMB_DEMOD_FP_RESET_FUNCTION)(
	DTMB_DEMOD_MODULE *pDemod
	);





/// RTL2836 extra module
typedef struct RTL2836_EXTRA_MODULE_TAG RTL2836_EXTRA_MODULE;
struct RTL2836_EXTRA_MODULE_TAG
{
	// RTL2836 update procedure enabling status
	int IsFunc1Enabled;
	int IsFunc2Enabled;

	// RTL2836 update Function 1 variables
	int Func1CntThd;
	int Func1Cnt;

	// RTL2836 update Function 2 variables
	int Func2SignalModePrevious;
};





/// DTMB demod module structure
struct DTMB_DEMOD_MODULE_TAG
{
	// Private variables
	int           DemodType;
	unsigned char DeviceAddr;
	unsigned long CrystalFreqHz;
	int           TsInterfaceMode;
	int           DiversityPipMode;

	unsigned long IfFreqHz;
	int           SpectrumMode;

	int IsIfFreqHzSet;
	int IsSpectrumModeSet;

	unsigned long CurrentPageNo;		// temp, because page register is write-only.

	union											///<   Demod extra module used by driving module
	{
		RTL2836_EXTRA_MODULE Rtl2836;
	}
	Extra;

	BASE_INTERFACE_MODULE *pBaseInterface;
	I2C_BRIDGE_MODULE *pI2cBridge;


	// Demod register table
	union
	{
		DTMB_REG_ENTRY_ADDR_8BIT  Addr8Bit[DTMB_REG_TABLE_LEN_MAX];
		DTMB_REG_ENTRY_ADDR_16BIT Addr16Bit[DTMB_REG_TABLE_LEN_MAX];
	}
	RegTable;


	// Demod I2C function pointers
	union
	{
		DTMB_DEMOD_REG_ACCESS_ADDR_8BIT  Addr8Bit;
		DTMB_DEMOD_REG_ACCESS_ADDR_16BIT Addr16Bit;
	}
	RegAccess;


	// Demod manipulating function pointers
	DTMB_DEMOD_FP_GET_DEMOD_TYPE              GetDemodType;
	DTMB_DEMOD_FP_GET_DEVICE_ADDR             GetDeviceAddr;
	DTMB_DEMOD_FP_GET_CRYSTAL_FREQ_HZ         GetCrystalFreqHz;

	DTMB_DEMOD_FP_IS_CONNECTED_TO_I2C         IsConnectedToI2c;

	DTMB_DEMOD_FP_SOFTWARE_RESET              SoftwareReset;

	DTMB_DEMOD_FP_INITIALIZE                  Initialize;
	DTMB_DEMOD_FP_SET_IF_FREQ_HZ              SetIfFreqHz;
	DTMB_DEMOD_FP_SET_SPECTRUM_MODE           SetSpectrumMode;
	DTMB_DEMOD_FP_GET_IF_FREQ_HZ              GetIfFreqHz;
	DTMB_DEMOD_FP_GET_SPECTRUM_MODE           GetSpectrumMode;

	DTMB_DEMOD_FP_IS_SIGNAL_LOCKED            IsSignalLocked;

	DTMB_DEMOD_FP_GET_SIGNAL_STRENGTH         GetSignalStrength;
	DTMB_DEMOD_FP_GET_SIGNAL_QUALITY          GetSignalQuality;

	DTMB_DEMOD_FP_GET_BER                     GetBer;
	DTMB_DEMOD_FP_GET_PER                     GetPer;
	DTMB_DEMOD_FP_GET_SNR_DB                  GetSnrDb;

	DTMB_DEMOD_FP_GET_RF_AGC                  GetRfAgc;
	DTMB_DEMOD_FP_GET_IF_AGC                  GetIfAgc;
	DTMB_DEMOD_FP_GET_DI_AGC                  GetDiAgc;

	DTMB_DEMOD_FP_GET_TR_OFFSET_PPM           GetTrOffsetPpm;
	DTMB_DEMOD_FP_GET_CR_OFFSET_HZ            GetCrOffsetHz;

	DTMB_DEMOD_FP_GET_CARRIER_MODE            GetCarrierMode;
	DTMB_DEMOD_FP_GET_PN_MODE                 GetPnMode;
	DTMB_DEMOD_FP_GET_QAM_MODE                GetQamMode;
	DTMB_DEMOD_FP_GET_CODE_RATE_MODE          GetCodeRateMode;
	DTMB_DEMOD_FP_GET_TIME_INTERLEAVER_MODE   GetTimeInterleaverMode;

	DTMB_DEMOD_FP_UPDATE_FUNCTION             UpdateFunction;
	DTMB_DEMOD_FP_RESET_FUNCTION              ResetFunction;
};







// DTMB demod default I2C functions for 8-bit address
int
dtmb_demod_addr_8bit_default_SetRegPage(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned long PageNo
	);

int
dtmb_demod_addr_8bit_default_SetRegBytes(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	const unsigned char *pWritingBytes,
	unsigned long ByteNum
	);

int
dtmb_demod_addr_8bit_default_GetRegBytes(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	unsigned char *pReadingBytes,
	unsigned long ByteNum
	);

int
dtmb_demod_addr_8bit_default_SetRegMaskBits(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	unsigned char Msb,
	unsigned char Lsb,
	const unsigned long WritingValue
	);

int
dtmb_demod_addr_8bit_default_GetRegMaskBits(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	unsigned char Msb,
	unsigned char Lsb,
	unsigned long *pReadingValue
	);

int
dtmb_demod_addr_8bit_default_SetRegBits(
	DTMB_DEMOD_MODULE *pDemod,
	int RegBitName,
	const unsigned long WritingValue
	);

int
dtmb_demod_addr_8bit_default_GetRegBits(
	DTMB_DEMOD_MODULE *pDemod,
	int RegBitName,
	unsigned long *pReadingValue
	);

int
dtmb_demod_addr_8bit_default_SetRegBitsWithPage(
	DTMB_DEMOD_MODULE *pDemod,
	int RegBitName,
	const unsigned long WritingValue
	);

int
dtmb_demod_addr_8bit_default_GetRegBitsWithPage(
	DTMB_DEMOD_MODULE *pDemod,
	int RegBitName,
	unsigned long *pReadingValue
	);





// DTMB demod default I2C functions for 16-bit address
int
dtmb_demod_addr_16bit_default_SetRegBytes(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned short RegStartAddr,
	const unsigned char *pWritingBytes,
	unsigned long ByteNum
	);

int
dtmb_demod_addr_16bit_default_GetRegBytes(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned short RegStartAddr,
	unsigned char *pReadingBytes,
	unsigned long ByteNum
	);

int
dtmb_demod_addr_16bit_default_SetRegMaskBits(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned short RegStartAddr,
	unsigned char Msb,
	unsigned char Lsb,
	const unsigned long WritingValue
	);

int
dtmb_demod_addr_16bit_default_GetRegMaskBits(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned short RegStartAddr,
	unsigned char Msb,
	unsigned char Lsb,
	unsigned long *pReadingValue
	);

int
dtmb_demod_addr_16bit_default_SetRegBits(
	DTMB_DEMOD_MODULE *pDemod,
	int RegBitName,
	const unsigned long WritingValue
	);

int
dtmb_demod_addr_16bit_default_GetRegBits(
	DTMB_DEMOD_MODULE *pDemod,
	int RegBitName,
	unsigned long *pReadingValue
	);





// DTMB demod default manipulating functions
void
dtmb_demod_default_GetDemodType(
	DTMB_DEMOD_MODULE *pDemod,
	int *pDemodType
	);

void
dtmb_demod_default_GetDeviceAddr(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned char *pDeviceAddr
	);

void
dtmb_demod_default_GetCrystalFreqHz(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned long *pCrystalFreqHz
	);

int
dtmb_demod_default_GetBandwidthMode(
	DTMB_DEMOD_MODULE *pDemod,
	int *pBandwidthMode
	);

int
dtmb_demod_default_GetIfFreqHz(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned long *pIfFreqHz
	);

int
dtmb_demod_default_GetSpectrumMode(
	DTMB_DEMOD_MODULE *pDemod,
	int *pSpectrumMode
	);







#endif
