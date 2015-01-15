#ifndef __QAM_DEMOD_BASE_H
#define __QAM_DEMOD_BASE_H

/**

@file

@brief   QAM demod base module definition

QAM demod base module definitions contains demod module structure, demod funciton pointers, and demod definitions.



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

	QAM_DEMOD_MODULE *pDemod;
	QAM_DEMOD_MODULE QamDemodModuleMemory;

	I2C_BRIDGE_MODULE I2cBridgeModuleMemory;

	int QamMode;
	unsigned long SymbolRateHz;
	int AlphaMode;
	unsigned long IfFreqHz;
	int SpectrumMode;

	int DemodType;
	unsigned char DeviceAddr;
	unsigned long CrystalFreqHz;

	long RfAgc, IfAgc;
	unsigned long DiAgc;

	int Answer;
	long TrOffsetPpm, CrOffsetHz;
	unsigned long BerNum, BerDen, PerNum, PerDen;
	double Ber, Per;
	long SnrDbNum, SnrDbDen;
	double SnrDb;
	unsigned long SignalStrength, SignalQuality;



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


	// Build QAM demod XXX module.
	BuildXxxModule(
		&pDemod,
		&QamDemodModuleMemory,
		&BaseInterfaceModuleMemory,
		&I2cBridgeModuleMemory,
		0x44,							// Demod I2C device address is 0x44 in 8-bit format.
		CRYSTAL_FREQ_28800000HZ,		// Demod crystal frequency is 28.8 MHz.
		TS_INTERFACE_SERIAL,			// Demod TS interface mode is serial.
		...								// Other arguments by each demod module
		);





	// ==== Initialize QAM demod and set its parameters =====

	// Initialize demod.
	pDemod->Initialize(pDemod);


	// Set demod parameters. (QAM mode, symbol rate, alpha mode, IF frequency, spectrum mode)
	// Note: In the example:
	//       1. QAM is 64.
	//       2. Symbol rate is 6.952 MHz.
	//       3. Alpha is 0.15.
	//       4. IF frequency is 36 MHz.
	//       5. Spectrum mode is SPECTRUM_INVERSE.
	QamMode      = QAM_QAM_64;
	SymbolRateHz = 6952000;
	AlphaMode    = QAM_ALPHA_0P15;
	IfFreqHz     = IF_FREQ_36000000HZ;
	SpectrumMode = SPECTRUM_INVERSE;

	pDemod->SetQamMode(pDemod,      QamMode);
	pDemod->SetSymbolRateHz(pDemod, SymbolRateHz);
	pDemod->SetAlphaMode(pDemod,    AlphaMode);
	pDemod->SetIfFreqHz(pDemod,     IfFreqHz);
	pDemod->SetSpectrumMode(pDemod, SpectrumMode);


	// Need to set tuner before demod software reset.
	// The order to set demod and tuner is not important.
	// Note: 1. For 8-bit register address demod, one can use
	//          "pDemod->RegAccess.Addr8Bit.SetRegBitsWithPage(pDemod, QAM_OPT_I2C_RELAY, 0x1);"
	//          for tuner I2C command forwarding.
	//       2. For 16-bit register address demod, one can use
	//          "pDemod->RegAccess.Addr16Bit.SetRegBits(pDemod, QAM_OPT_I2C_RELAY, 0x1);"
	//       for tuner I2C command forwarding.


	// Reset demod by software reset.
	pDemod->SoftwareReset(pDemod);


	// Wait maximum 1000 ms for demod converge.
	for(i = 0; i < 25; i++)
	{
		// Wait 40 ms.
		pBaseInterface->WaitMs(pBaseInterface, 40);

		// Check signal lock status through frame lock.
		// Note: If Answer is YES, frame is locked.
		//       If Answer is NO, frame is not locked.
		pDemod->IsFrameLocked(pDemod, &Answer);

		if(Answer == YES)
		{
			// Signal is locked.
			break;
		}
	}





	// ==== Get QAM demod information =====

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


	// Get demod parameters. (QAM mode, symbol rate, alpha mode, IF frequency, spectrum mode)
	pDemod->GetQamMode(pDemod,      &QamMode);
	pDemod->GetSymbolRateHz(pDemod, &SymbolRateHz);
	pDemod->GetAlphaMode(pDemod,    &AlphaMode);
	pDemod->GetIfFreqHz(pDemod,     &IfFreqHz);
	pDemod->GetSpectrumMode(pDemod, &SpectrumMode);


	// Get demod AGC value.
	// Note: The range of RF AGC and IF AGC value is -1024 ~ 1023.
	//       The range of digital AGC value is 0 ~ 134217727.
	pDemod->GetRfAgc(pDemod, &RfAgc);
	pDemod->GetIfAgc(pDemod, &IfAgc);
	pDemod->GetDiAgc(pDemod, &DiAgc);


	// Get demod lock status.
	// Note: If Answer is YES, it is locked.
	//       If Answer is NO, it is not locked.
	pDemod->IsAagcLocked(pDemod,  &Answer);
	pDemod->IsEqLocked(pDemod,    &Answer);
	pDemod->IsFrameLocked(pDemod, &Answer);


	// Get TR offset (symbol timing offset) in ppm.
	pDemod->GetTrOffsetPpm(pDemod, &TrOffsetPpm);

	// Get CR offset (RF frequency offset) in Hz.
	pDemod->GetCrOffsetHz(pDemod, &CrOffsetHz);


	// Get BER and PER.
	// Note: Test packet number = pow(2, (2 * 5 + 4)) = 16384
	//       Maximum wait time  = 1000 ms = 1 second
	pDemod->GetErrorRate(pDemod, 5, 1000, &BerNum, &BerDen, &PerNum, &PerDen);
	Ber = (double)BerNum / (double)BerDen;
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



	return 0;
}


@endcode

*/


#include "foundation.h"





// Definitions

// Page register address
#define QAM_DEMOD_PAGE_REG_ADDR					0x0


/// QAM QAM modes
enum QAM_QAM_MODE
{
	QAM_QAM_4,					///<   QPSK
	QAM_QAM_16,					///<   16 QAM
	QAM_QAM_32,					///<   32 QAM
	QAM_QAM_64,					///<   64 QAM
	QAM_QAM_128,				///<   128 QAM
	QAM_QAM_256,				///<   256 QAM
	QAM_QAM_512,				///<   512 QAM
	QAM_QAM_1024,				///<   1024 QAM
};
#define QAM_QAM_MODE_NUM			8


/// QAM alpha modes
enum QAM_ALPHA_MODE
{
	QAM_ALPHA_0P12,				///<   Alpha = 0.12
	QAM_ALPHA_0P13,				///<   Alpha = 0.13
	QAM_ALPHA_0P15,				///<   Alpha = 0.15
	QAM_ALPHA_0P18,				///<   Alpha = 0.18
	QAM_ALPHA_0P20,				///<   Alpha = 0.20
};
#define QAM_ALPHA_MODE_NUM			5


/// QAM demod enhancement modes
enum QAM_DEMOD_EN_MODE
{
	QAM_DEMOD_EN_NONE,			///<   None demod enhancement
	QAM_DEMOD_EN_AM_HUM,		///<   AM-hum demod enhancement
};
#define QAM_DEMOD_EN_MODE_NUM		2


/// QAM demod configuration mode
enum QAM_DEMOD_CONFIG_MODE
{
	QAM_DEMOD_CONFIG_OC,			///<   OpenCable demod configuration
	QAM_DEMOD_CONFIG_DVBC,			///<   DVB-C demod configuration
};
#define QAM_DEMOD_CONFIG_MODE_NUM		2





// Register entry definitions

// Base register entry for 8-bit address
typedef struct
{
	int IsAvailable;
	unsigned char PageNo;
	unsigned char RegStartAddr;
	unsigned char Msb;
	unsigned char Lsb;
}
QAM_BASE_REG_ENTRY_ADDR_8BIT;



// Primary base register entry for 8-bit address
typedef struct
{
	int RegBitName;
	unsigned char PageNo;
	unsigned char RegStartAddr;
	unsigned char Msb;
	unsigned char Lsb;
}
QAM_PRIMARY_BASE_REG_ENTRY_ADDR_8BIT;



// Monitor register entry for 8-bit address
#define QAM_MONITOR_REG_INFO_TABLE_LEN		2
typedef struct
{
	int IsAvailable;
	unsigned char      InfoNum;

	struct
	{
		unsigned char SelRegAddr;
		unsigned char SelValue;
		int           RegBitName;
		unsigned char Shift;
	}
	InfoTable[QAM_MONITOR_REG_INFO_TABLE_LEN];
}
QAM_MONITOR_REG_ENTRY_ADDR_8BIT;



// Primary monitor register entry for 8-bit address
typedef struct
{
	int MonitorRegBitName;
	unsigned char      InfoNum;

	struct
	{
		unsigned char SelRegAddr;
		unsigned char SelValue;
		int           RegBitName;
		unsigned char Shift;
	}
	InfoTable[QAM_MONITOR_REG_INFO_TABLE_LEN];
}
QAM_PRIMARY_MONITOR_REG_ENTRY_ADDR_8BIT;



// Base register entry for 16-bit address
typedef struct
{
	int IsAvailable;
	unsigned short RegStartAddr;
	unsigned char Msb;
	unsigned char Lsb;
}
QAM_BASE_REG_ENTRY_ADDR_16BIT;



// Primary base register entry for 16-bit address
typedef struct
{
	int RegBitName;
	unsigned short RegStartAddr;
	unsigned char Msb;
	unsigned char Lsb;
}
QAM_PRIMARY_BASE_REG_ENTRY_ADDR_16BIT;



// Monitor register entry for 16-bit address
#define QAM_MONITOR_REG_INFO_TABLE_LEN		2
typedef struct
{
	int IsAvailable;
	unsigned char InfoNum;

	struct
	{
		unsigned short SelRegAddr;
		unsigned char SelValue;
		int RegBitName;
		unsigned char Shift;
	}
	InfoTable[QAM_MONITOR_REG_INFO_TABLE_LEN];
}
QAM_MONITOR_REG_ENTRY_ADDR_16BIT;



// Primary monitor register entry for 16-bit address
typedef struct
{
	int MonitorRegBitName;
	unsigned char InfoNum;

	struct
	{
		unsigned short SelRegAddr;
		unsigned char SelValue;
		int RegBitName;
		unsigned char Shift;
	}
	InfoTable[QAM_MONITOR_REG_INFO_TABLE_LEN];
}
QAM_PRIMARY_MONITOR_REG_ENTRY_ADDR_16BIT;





// Register bit name definitions

/// Base register bit name
enum QAM_REG_BIT_NAME
{
	// Generality
	QAM_SYS_VERSION,
	QAM_OPT_I2C_RELAY,
	QAM_I2CT_EN_CTRL,						// for RTL2836B DVB-C only
	QAM_SOFT_RESET,
	QAM_SOFT_RESET_FF,

	// Miscellany
	QAM_OPT_I2C_DRIVE_CURRENT,
	QAM_GPIO2_OEN,
	QAM_GPIO3_OEN,
	QAM_GPIO2_O,
	QAM_GPIO3_O,
	QAM_GPIO2_I,
	QAM_GPIO3_I,
	QAM_INNER_DATA_STROBE,
	QAM_INNER_DATA_SEL1,
	QAM_INNER_DATA_SEL2,
	QAM_INNER_DATA1,
	QAM_INNER_DATA2,

	// QAM mode
	QAM_QAM_MODE,

	// AD
	QAM_AD_AV,								// for RTL2840 only

	// AAGC
	QAM_OPT_RF_AAGC_DRIVE_CURRENT,			// for RTL2840, RTD2885 QAM only
	QAM_OPT_IF_AAGC_DRIVE_CURRENT,			// for RTL2840, RTD2885 QAM only
	QAM_AGC_DRIVE_LV,						// for RTL2836B DVB-C only
	QAM_DVBC_RSSI_R,						// for RTL2836B DVB-C only
	QAM_OPT_RF_AAGC_DRIVE,
	QAM_OPT_IF_AAGC_DRIVE,
	QAM_OPT_RF_AAGC_OEN,					// for RTL2840 only
	QAM_OPT_IF_AAGC_OEN,					// for RTL2840 only
	QAM_PAR_RF_SD_IB,
	QAM_PAR_IF_SD_IB,
	QAM_AAGC_FZ_OPTION,
	QAM_AAGC_TARGET,
	QAM_RF_AAGC_MAX,
	QAM_RF_AAGC_MIN,
	QAM_IF_AAGC_MAX,
	QAM_IF_AAGC_MIN,
	QAM_VTOP,
	QAM_KRF,								// for RTD2885 QAM only
	QAM_KRF_MSB,
	QAM_KRF_LSB,
	QAM_AAGC_MODE_SEL,
	QAM_AAGC_LD,
	QAM_OPT_RF_AAGC_OE,						// for RTL2820 OpenCable, RTD2885 QAM only
	QAM_OPT_IF_AAGC_OE,						// for RTL2820 OpenCable, RTD2885 QAM only
	QAM_IF_AGC_DRIVING,						// for RTL2820 OpenCable only
	QAM_RF_AGC_DRIVING,						// for RTL2820 OpenCable only
	QAM_AAGC_INIT_LEVEL,

	// DDC
	QAM_DDC_FREQ,
	QAM_SPEC_INV,

	// Timing recovery
	QAM_TR_DECI_RATIO,

	// Carrier recovery
	QAM_CR_LD,

	// Equalizer
	QAM_EQ_LD,
	QAM_MSE,

	// Frame sync. indicator
	QAM_SYNCLOST,							// for RTL2840, RTD2885 QAM only
	QAM_FS_SYNC_STROBE,						// for RTL2820 OpenCable, RTD2885 QAM only
	QAM_FS_SYNC_LOST,						// for RTL2820 OpenCable, RTD2885 QAM only
	QAM_OC_MPEG_SYNC_MODE,


	// BER
	QAM_BER_RD_STROBE,
	QAM_BERT_EN,
	QAM_BERT_HOLD,
	QAM_DIS_AUTO_MODE,
	QAM_TEST_VOLUME,
	QAM_BER_REG0,
	QAM_BER_REG1,
	QAM_BER_REG2_15_0,
	QAM_BER_REG2_18_16,

	QAM_OC_BER_RD_STROBE,					// for RTD2885 QAM only
	QAM_OC_BERT_EN,							// for RTD2885 QAM only
	QAM_OC_BERT_HOLD,						// for RTD2885 QAM only
	QAM_OC_DIS_AUTO_MODE,					// for RTD2885 QAM only
	QAM_OC_TEST_VOLUME,						// for RTD2885 QAM only
	QAM_OC_BER_REG0,						// for RTD2885 QAM only
	QAM_OC_BER_REG1,						// for RTD2885 QAM only
	QAM_OC_BER_REG2_15_0,					// for RTD2885 QAM only
	QAM_OC_BER_REG2_18_16,					// for RTD2885 QAM only

	QAM_DVBC_BER_RD_STROBE,					// for RTD2885 QAM only
	QAM_DVBC_BERT_EN,						// for RTD2885 QAM only
	QAM_DVBC_BERT_HOLD,						// for RTD2885 QAM only
	QAM_DVBC_DIS_AUTO_MODE,					// for RTD2885 QAM only
	QAM_DVBC_TEST_VOLUME,					// for RTD2885 QAM only
	QAM_DVBC_BER_REG0,						// for RTD2885 QAM only
	QAM_DVBC_BER_REG1,						// for RTD2885 QAM only
	QAM_DVBC_BER_REG2_15_0,					// for RTD2885 QAM only
	QAM_DVBC_BER_REG2_18_16,				// for RTD2885 QAM only


	// MPEG TS output interface
	QAM_OPT_MPEG_OUT_SEL,					// for RTD2648 QAM only
	QAM_CKOUTPAR,
	QAM_CKOUT_PWR,
	QAM_CDIV_PH0,
	QAM_CDIV_PH1,
	QAM_MPEG_OUT_EN,						// for RTL2840 only
	QAM_OPT_MPEG_DRIVE_CURRENT,				// for RTL2840 only
	QAM_NO_REINVERT,						// for RTL2840 only
	QAM_FIX_TEI,							// for RTL2840 only
	QAM_SERIAL,								// for RTL2840 only
	QAM_OPT_MPEG_IO,						// for RTL2820 OpenCable, RTD2885 QAM only
	QAM_OPT_M_OEN,							// for RTL2820 OpenCable, RTD2885 QAM only
	QAM_REPLA_SD_EN,						// for RTL2820 OpenCable only
	QAM_TEI_SD_ERR_EN,						// for RTL2820 OpenCable only
	QAM_TEI_RS_ERR_EN,						// for RTL2820 OpenCable only
	QAM_SET_MPEG_ERR,						// for RTL2820 OpenCable only

	QAM_OC_CKOUTPAR,						// for RTD2885 QAM only
	QAM_OC_CKOUT_PWR,						// for RTD2885 QAM only
	QAM_OC_CDIV_PH0,						// for RTD2885 QAM only
	QAM_OC_CDIV_PH1,						// for RTD2885 QAM only
	QAM_OC_SERIAL,							// for RTD2885 QAM only

	QAM_DVBC_CKOUTPAR,						// for RTD2885 QAM, RTL2836B DVB-C only
	QAM_DVBC_CKOUT_PWR,						// for RTD2885 QAM, RTL2836B DVB-C only
	QAM_DVBC_CDIV_PH0,						// for RTD2885 QAM, RTL2836B DVB-C only
	QAM_DVBC_CDIV_PH1,						// for RTD2885 QAM, RTL2836B DVB-C only
	QAM_DVBC_NO_REINVERT,					// for RTD2885 QAM, RTL2836B DVB-C only
	QAM_DVBC_FIX_TEI,						// for RTD2885 QAM, RTL2836B DVB-C only
	QAM_DVBC_SERIAL,						// for RTD2885 QAM, RTL2836B DVB-C only


	// Monitor
	QAM_ADC_CLIP_CNT_REC,
	QAM_DAGC_LEVEL_26_11,
	QAM_DAGC_LEVEL_10_0,
	QAM_RF_AAGC_SD_IN,
	QAM_IF_AAGC_SD_IN,
	QAM_KI_TR_OUT_30_15,
	QAM_KI_TR_OUT_14_0,
	QAM_KI_CR_OUT_15_0,
	QAM_KI_CR_OUT_31_16,

	// Specific register
	QAM_SPEC_SIGNAL_INDICATOR,
	QAM_SPEC_ALPHA_STROBE,
	QAM_SPEC_ALPHA_SEL,
	QAM_SPEC_ALPHA_VAL,
	QAM_SPEC_SYMBOL_RATE_REG_0,
	QAM_SPEC_SYMBOL_RATE_STROBE,
	QAM_SPEC_SYMBOL_RATE_SEL,
	QAM_SPEC_SYMBOL_RATE_VAL,
	QAM_SPEC_REG_0_STROBE,
	QAM_SPEC_REG_0_SEL,
	QAM_SPEC_INIT_A0,						// for RTL2840 only
	QAM_SPEC_INIT_A1,						// for RTL2840 only
	QAM_SPEC_INIT_A2,						// for RTL2840 only
	QAM_SPEC_INIT_B0,						// for RTL2820 OpenCable only
	QAM_SPEC_INIT_C1,						// for RTL2820 OpenCable only
	QAM_SPEC_INIT_C2,						// for RTL2820 OpenCable only
	QAM_SPEC_INIT_C3,						// for RTL2820 OpenCable only

	// GPIO
	QAM_OPT_GPIOA_OE,						// for RTL2836B DVB-C only

	// Pseudo register for test only
	QAM_TEST_REG_0,
	QAM_TEST_REG_1,
	QAM_TEST_REG_2,
	QAM_TEST_REG_3,


	// Item terminator
	QAM_REG_BIT_NAME_ITEM_TERMINATOR,
};


/// Monitor register bit name
enum QAM_MONITOR_REG_BIT_NAME
{
	// Generality
	QAM_ADC_CLIP_CNT,
	QAM_DAGC_VALUE,
	QAM_RF_AGC_VALUE,
	QAM_IF_AGC_VALUE,
	QAM_TR_OFFSET,
	QAM_CR_OFFSET,

	// Specific monitor register
	QAM_SPEC_MONITER_INIT_0,

	// Item terminator
	QAM_MONITOR_REG_BIT_NAME_ITEM_TERMINATOR,
};



// Register table length definitions
#define QAM_BASE_REG_TABLE_LEN_MAX			QAM_REG_BIT_NAME_ITEM_TERMINATOR
#define QAM_MONITOR_REG_TABLE_LEN_MAX		QAM_MONITOR_REG_BIT_NAME_ITEM_TERMINATOR





/// QAM demod module pre-definition
typedef struct QAM_DEMOD_MODULE_TAG QAM_DEMOD_MODULE;





/**

@brief   QAM demod register page setting function pointer

Demod upper level functions will use QAM_DEMOD_FP_SET_REG_PAGE() to set demod register page.


@param [in]   pDemod   The demod module pointer
@param [in]   PageNo   Page number


@retval   FUNCTION_SUCCESS   Set register page successfully with page number.
@retval   FUNCTION_ERROR     Set register page unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_SET_REG_PAGE() with the corresponding function.


@par Example:
@code


#include "demod_pseudo.h"


int main(void)
{
	QAM_DEMOD_MODULE *pDemod;
	QAM_DEMOD_MODULE DvbcDemodModuleMemory;
	PSEUDO_EXTRA_MODULE PseudoExtraModuleMemory;


	// Build pseudo demod module.
	BuildPseudoDemodModule(&pDemod, &DvbcDemodModuleMemory, &PseudoExtraModuleMemory);

	...

	// Set demod register page with page number 2.
	pDemod->SetRegPage(pDemod, 2);

	...

	return 0;
}


@endcode

*/
typedef int
(*QAM_DEMOD_FP_ADDR_8BIT_SET_REG_PAGE)(
	QAM_DEMOD_MODULE *pDemod,
	unsigned long PageNo
	);





/**

@brief   QAM demod register byte setting function pointer

Demod upper level functions will use QAM_DEMOD_FP_SET_REG_BYTES() to set demod register bytes.


@param [in]   pDemod          The demod module pointer
@param [in]   RegStartAddr    Demod register start address
@param [in]   pWritingBytes   Pointer to writing bytes
@param [in]   ByteNum         Writing byte number


@retval   FUNCTION_SUCCESS   Set demod register bytes successfully with writing bytes.
@retval   FUNCTION_ERROR     Set demod register bytes unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_SET_REG_BYTES() with the corresponding function.
	-# Need to set register page by QAM_DEMOD_FP_SET_REG_PAGE() before using QAM_DEMOD_FP_SET_REG_BYTES().


@see   QAM_DEMOD_FP_SET_REG_PAGE, QAM_DEMOD_FP_GET_REG_BYTES



@par Example:
@code


#include "demod_pseudo.h"


int main(void)
{
	QAM_DEMOD_MODULE *pDemod;
	QAM_DEMOD_MODULE DvbcDemodModuleMemory;
	PSEUDO_EXTRA_MODULE PseudoExtraModuleMemory;
	unsigned char WritingBytes[10];


	// Build pseudo demod module.
	BuildPseudoDemodModule(&pDemod, &DvbcDemodModuleMemory, &PseudoExtraModuleMemory);

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
(*QAM_DEMOD_FP_ADDR_8BIT_SET_REG_BYTES)(
	QAM_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	const unsigned char *pWritingBytes,
	unsigned long ByteNum
	);

typedef int
(*QAM_DEMOD_FP_ADDR_16BIT_SET_REG_BYTES)(
	QAM_DEMOD_MODULE *pDemod,
	unsigned short RegStartAddr,
	const unsigned char *pWritingBytes,
	unsigned long ByteNum
	);





/**

@brief   QAM demod register byte getting function pointer

Demod upper level functions will use QAM_DEMOD_FP_GET_REG_BYTES() to get demod register bytes.


@param [in]    pDemod          The demod module pointer
@param [in]    RegStartAddr    Demod register start address
@param [out]   pReadingBytes   Pointer to an allocated memory for storing reading bytes
@param [in]    ByteNum         Reading byte number


@retval   FUNCTION_SUCCESS   Get demod register bytes successfully with reading byte number.
@retval   FUNCTION_ERROR     Get demod register bytes unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_GET_REG_BYTES() with the corresponding function.
	-# Need to set register page by QAM_DEMOD_FP_SET_REG_PAGE() before using QAM_DEMOD_FP_GET_REG_BYTES().


@see   QAM_DEMOD_FP_SET_REG_PAGE, QAM_DEMOD_FP_SET_REG_BYTES



@par Example:
@code


#include "demod_pseudo.h"


int main(void)
{
	QAM_DEMOD_MODULE *pDemod;
	QAM_DEMOD_MODULE DvbcDemodModuleMemory;
	PSEUDO_EXTRA_MODULE PseudoExtraModuleMemory;
	unsigned char ReadingBytes[10];


	// Build pseudo demod module.
	BuildPseudoDemodModule(&pDemod, &DvbcDemodModuleMemory, &PseudoExtraModuleMemory);

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
(*QAM_DEMOD_FP_ADDR_8BIT_GET_REG_BYTES)(
	QAM_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	unsigned char *pReadingBytes,
	unsigned long ByteNum
	);

typedef int
(*QAM_DEMOD_FP_ADDR_16BIT_GET_REG_BYTES)(
	QAM_DEMOD_MODULE *pDemod,
	unsigned short RegStartAddr,
	unsigned char *pReadingBytes,
	unsigned long ByteNum
	);





/**

@brief   QAM demod register mask bits setting function pointer

Demod upper level functions will use QAM_DEMOD_FP_SET_REG_MASK_BITS() to set demod register mask bits.


@param [in]   pDemod         The demod module pointer
@param [in]   RegStartAddr   Demod register start address
@param [in]   Msb            Mask MSB with 0-based index
@param [in]   Lsb            Mask LSB with 0-based index
@param [in]   WritingValue   The mask bits writing value


@retval   FUNCTION_SUCCESS   Set demod register mask bits successfully with writing value.
@retval   FUNCTION_ERROR     Set demod register mask bits unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_SET_REG_MASK_BITS() with the corresponding function.
	-# Need to set register page by QAM_DEMOD_FP_SET_REG_PAGE() before using QAM_DEMOD_FP_SET_REG_MASK_BITS().
	-# The constraints of QAM_DEMOD_FP_SET_REG_MASK_BITS() function usage are described as follows:
		-# The mask MSB and LSB must be 0~31.
		-# The mask MSB must be greater than or equal to LSB.


@see   QAM_DEMOD_FP_SET_REG_PAGE, QAM_DEMOD_FP_GET_REG_MASK_BITS



@par Example:
@code


#include "demod_pseudo.h"


int main(void)
{
	QAM_DEMOD_MODULE *pDemod;
	QAM_DEMOD_MODULE DvbcDemodModuleMemory;
	PSEUDO_EXTRA_MODULE PseudoExtraModuleMemory;


	// Build pseudo demod module.
	BuildPseudoDemodModule(&pDemod, &DvbcDemodModuleMemory, &PseudoExtraModuleMemory);

	...

	// Set demod register bits (page 1, address {0x18, 0x17} [12:5]) with writing value 0x1d.
	pDemod->SetRegPage(pDemod, 1);
	pDemod->SetRegMaskBits(pDemod, 0x17, 12, 5, 0x1d);


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
(*QAM_DEMOD_FP_ADDR_8BIT_SET_REG_MASK_BITS)(
	QAM_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	unsigned char Msb,
	unsigned char Lsb,
	const unsigned long WritingValue
	);

typedef int
(*QAM_DEMOD_FP_ADDR_16BIT_SET_REG_MASK_BITS)(
	QAM_DEMOD_MODULE *pDemod,
	unsigned short RegStartAddr,
	unsigned char Msb,
	unsigned char Lsb,
	const unsigned long WritingValue
	);





/**

@brief   QAM demod register mask bits getting function pointer

Demod upper level functions will use QAM_DEMOD_FP_GET_REG_MASK_BITS() to get demod register mask bits.


@param [in]    pDemod          The demod module pointer
@param [in]    RegStartAddr    Demod register start address
@param [in]    Msb             Mask MSB with 0-based index
@param [in]    Lsb             Mask LSB with 0-based index
@param [out]   pReadingValue   Pointer to an allocated memory for storing reading value


@retval   FUNCTION_SUCCESS   Get demod register mask bits successfully.
@retval   FUNCTION_ERROR     Get demod register mask bits unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_GET_REG_MASK_BITS() with the corresponding function.
	-# Need to set register page by QAM_DEMOD_FP_SET_REG_PAGE() before using QAM_DEMOD_FP_GET_REG_MASK_BITS().
	-# The constraints of QAM_DEMOD_FP_GET_REG_MASK_BITS() function usage are described as follows:
		-# The mask MSB and LSB must be 0~31.
		-# The mask MSB must be greater than or equal to LSB.


@see   QAM_DEMOD_FP_SET_REG_PAGE, QAM_DEMOD_FP_SET_REG_MASK_BITS



@par Example:
@code


#include "demod_pseudo.h"


int main(void)
{
	QAM_DEMOD_MODULE *pDemod;
	QAM_DEMOD_MODULE DvbcDemodModuleMemory;
	PSEUDO_EXTRA_MODULE PseudoExtraModuleMemory;
	unsigned long ReadingValue;


	// Build pseudo demod module.
	BuildPseudoDemodModule(&pDemod, &DvbcDemodModuleMemory, &PseudoExtraModuleMemory);

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
(*QAM_DEMOD_FP_ADDR_8BIT_GET_REG_MASK_BITS)(
	QAM_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	unsigned char Msb,
	unsigned char Lsb,
	unsigned long *pReadingValue
	);

typedef int
(*QAM_DEMOD_FP_ADDR_16BIT_GET_REG_MASK_BITS)(
	QAM_DEMOD_MODULE *pDemod,
	unsigned short RegStartAddr,
	unsigned char Msb,
	unsigned char Lsb,
	unsigned long *pReadingValue
	);





/**

@brief   QAM demod register bits setting function pointer

Demod upper level functions will use QAM_DEMOD_FP_SET_REG_BITS() to set demod register bits with bit name.


@param [in]   pDemod         The demod module pointer
@param [in]   RegBitName     Pre-defined demod register bit name
@param [in]   WritingValue   The mask bits writing value


@retval   FUNCTION_SUCCESS   Set demod register bits successfully with bit name and writing value.
@retval   FUNCTION_ERROR     Set demod register bits unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_SET_REG_BITS() with the corresponding function.
	-# Need to set register page before using QAM_DEMOD_FP_SET_REG_BITS().
	-# Register bit names are pre-defined keys for bit access, and one can find these in demod header file.


@see   QAM_DEMOD_FP_SET_REG_PAGE, QAM_DEMOD_FP_GET_REG_BITS



@par Example:
@code


#include "demod_pseudo.h"


int main(void)
{
	QAM_DEMOD_MODULE *pDemod;
	QAM_DEMOD_MODULE DvbcDemodModuleMemory;
	PSEUDO_EXTRA_MODULE PseudoExtraModuleMemory;


	// Build pseudo demod module.
	BuildPseudoDemodModule(&pDemod, &DvbcDemodModuleMemory, &PseudoExtraModuleMemory);

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
(*QAM_DEMOD_FP_ADDR_8BIT_SET_REG_BITS)(
	QAM_DEMOD_MODULE *pDemod,
	int RegBitName,
	const unsigned long WritingValue
	);

typedef int
(*QAM_DEMOD_FP_ADDR_16BIT_SET_REG_BITS)(
	QAM_DEMOD_MODULE *pDemod,
	int RegBitName,
	const unsigned long WritingValue
	);





/**

@brief   QAM demod register bits getting function pointer

Demod upper level functions will use QAM_DEMOD_FP_GET_REG_BITS() to get demod register bits with bit name.


@param [in]    pDemod          The demod module pointer
@param [in]    RegBitName      Pre-defined demod register bit name
@param [out]   pReadingValue   Pointer to an allocated memory for storing reading value


@retval   FUNCTION_SUCCESS   Get demod register bits successfully with bit name.
@retval   FUNCTION_ERROR     Get demod register bits unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_GET_REG_BITS() with the corresponding function.
	-# Need to set register page before using QAM_DEMOD_FP_GET_REG_BITS().
	-# Register bit names are pre-defined keys for bit access, and one can find these in demod header file.


@see   QAM_DEMOD_FP_SET_REG_PAGE, QAM_DEMOD_FP_SET_REG_BITS



@par Example:
@code


#include "demod_pseudo.h"


int main(void)
{
	QAM_DEMOD_MODULE *pDemod;
	QAM_DEMOD_MODULE DvbcDemodModuleMemory;
	PSEUDO_EXTRA_MODULE PseudoExtraModuleMemory;
	unsigned long ReadingValue;


	// Build pseudo demod module.
	BuildPseudoDemodModule(&pDemod, &DvbcDemodModuleMemory, &PseudoExtraModuleMemory);

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
(*QAM_DEMOD_FP_ADDR_8BIT_GET_REG_BITS)(
	QAM_DEMOD_MODULE *pDemod,
	int RegBitName,
	unsigned long *pReadingValue
	);

typedef int
(*QAM_DEMOD_FP_ADDR_16BIT_GET_REG_BITS)(
	QAM_DEMOD_MODULE *pDemod,
	int RegBitName,
	unsigned long *pReadingValue
	);





/**

@brief   QAM demod register bits setting function pointer (with page setting)

Demod upper level functions will use QAM_DEMOD_FP_SET_REG_BITS_WITH_PAGE() to set demod register bits with bit name and
page setting.


@param [in]   pDemod         The demod module pointer
@param [in]   RegBitName     Pre-defined demod register bit name
@param [in]   WritingValue   The mask bits writing value


@retval   FUNCTION_SUCCESS   Set demod register bits successfully with bit name, page setting, and writing value.
@retval   FUNCTION_ERROR     Set demod register bits unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_SET_REG_BITS_WITH_PAGE() with the corresponding function.
	-# Don't need to set register page before using QAM_DEMOD_FP_SET_REG_BITS_WITH_PAGE().
	-# Register bit names are pre-defined keys for bit access, and one can find these in demod header file.


@see   QAM_DEMOD_FP_GET_REG_BITS_WITH_PAGE



@par Example:
@code


#include "demod_pseudo.h"


int main(void)
{
	QAM_DEMOD_MODULE *pDemod;
	QAM_DEMOD_MODULE DvbcDemodModuleMemory;
	PSEUDO_EXTRA_MODULE PseudoExtraModuleMemory;


	// Build pseudo demod module.
	BuildPseudoDemodModule(&pDemod, &DvbcDemodModuleMemory, &PseudoExtraModuleMemory);

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
(*QAM_DEMOD_FP_ADDR_8BIT_SET_REG_BITS_WITH_PAGE)(
	QAM_DEMOD_MODULE *pDemod,
	int RegBitName,
	const unsigned long WritingValue
	);





/**

@brief   QAM demod register bits getting function pointer (with page setting)

Demod upper level functions will use QAM_DEMOD_FP_GET_REG_BITS_WITH_PAGE() to get demod register bits with bit name and
page setting.


@param [in]    pDemod          The demod module pointer
@param [in]    RegBitName      Pre-defined demod register bit name
@param [out]   pReadingValue   Pointer to an allocated memory for storing reading value


@retval   FUNCTION_SUCCESS   Get demod register bits successfully with bit name and page setting.
@retval   FUNCTION_ERROR     Get demod register bits unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_GET_REG_BITS_WITH_PAGE() with the corresponding function.
	-# Don't need to set register page before using QAM_DEMOD_FP_GET_REG_BITS_WITH_PAGE().
	-# Register bit names are pre-defined keys for bit access, and one can find these in demod header file.


@see   QAM_DEMOD_FP_SET_REG_BITS_WITH_PAGE



@par Example:
@code


#include "demod_pseudo.h"


int main(void)
{
	QAM_DEMOD_MODULE *pDemod;
	QAM_DEMOD_MODULE DvbcDemodModuleMemory;
	PSEUDO_EXTRA_MODULE PseudoExtraModuleMemory;
	unsigned long ReadingValue;


	// Build pseudo demod module.
	BuildPseudoDemodModule(&pDemod, &DvbcDemodModuleMemory, &PseudoExtraModuleMemory);

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
(*QAM_DEMOD_FP_ADDR_8BIT_GET_REG_BITS_WITH_PAGE)(
	QAM_DEMOD_MODULE *pDemod,
	int RegBitName,
	unsigned long *pReadingValue
	);





// Demod register access for 8-bit address
typedef struct
{
	QAM_DEMOD_FP_ADDR_8BIT_SET_REG_PAGE             SetRegPage;
	QAM_DEMOD_FP_ADDR_8BIT_SET_REG_BYTES            SetRegBytes;
	QAM_DEMOD_FP_ADDR_8BIT_GET_REG_BYTES            GetRegBytes;
	QAM_DEMOD_FP_ADDR_8BIT_SET_REG_MASK_BITS        SetRegMaskBits;
	QAM_DEMOD_FP_ADDR_8BIT_GET_REG_MASK_BITS        GetRegMaskBits;
	QAM_DEMOD_FP_ADDR_8BIT_SET_REG_BITS             SetRegBits;
	QAM_DEMOD_FP_ADDR_8BIT_GET_REG_BITS             GetRegBits;
	QAM_DEMOD_FP_ADDR_8BIT_SET_REG_BITS_WITH_PAGE   SetRegBitsWithPage;
	QAM_DEMOD_FP_ADDR_8BIT_GET_REG_BITS_WITH_PAGE   GetRegBitsWithPage;
}
QAM_DEMOD_REG_ACCESS_ADDR_8BIT;





// Demod register access for 16-bit address
typedef struct
{
	QAM_DEMOD_FP_ADDR_16BIT_SET_REG_BYTES       SetRegBytes;
	QAM_DEMOD_FP_ADDR_16BIT_GET_REG_BYTES       GetRegBytes;
	QAM_DEMOD_FP_ADDR_16BIT_SET_REG_MASK_BITS   SetRegMaskBits;
	QAM_DEMOD_FP_ADDR_16BIT_GET_REG_MASK_BITS   GetRegMaskBits;
	QAM_DEMOD_FP_ADDR_16BIT_SET_REG_BITS        SetRegBits;
	QAM_DEMOD_FP_ADDR_16BIT_GET_REG_BITS        GetRegBits;
}
QAM_DEMOD_REG_ACCESS_ADDR_16BIT;





/**

@brief   QAM demod type getting function pointer

One can use QAM_DEMOD_FP_GET_DEMOD_TYPE() to get QAM demod type.


@param [in]    pDemod       The demod module pointer
@param [out]   pDemodType   Pointer to an allocated memory for storing demod type


@note
	-# Demod building function will set QAM_DEMOD_FP_GET_DEMOD_TYPE() with the corresponding function.


@see   MODULE_TYPE

*/
typedef void
(*QAM_DEMOD_FP_GET_DEMOD_TYPE)(
	QAM_DEMOD_MODULE *pDemod,
	int *pDemodType
	);





/**

@brief   QAM demod I2C device address getting function pointer

One can use QAM_DEMOD_FP_GET_DEVICE_ADDR() to get QAM demod I2C device address.


@param [in]    pDemod        The demod module pointer
@param [out]   pDeviceAddr   Pointer to an allocated memory for storing demod I2C device address


@retval   FUNCTION_SUCCESS   Get demod device address successfully.
@retval   FUNCTION_ERROR     Get demod device address unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_GET_DEVICE_ADDR() with the corresponding function.

*/
typedef void
(*QAM_DEMOD_FP_GET_DEVICE_ADDR)(
	QAM_DEMOD_MODULE *pDemod,
	unsigned char *pDeviceAddr
	);





/**

@brief   QAM demod crystal frequency getting function pointer

One can use QAM_DEMOD_FP_GET_CRYSTAL_FREQ_HZ() to get QAM demod crystal frequency in Hz.


@param [in]    pDemod           The demod module pointer
@param [out]   pCrystalFreqHz   Pointer to an allocated memory for storing demod crystal frequency in Hz


@retval   FUNCTION_SUCCESS   Get demod crystal frequency successfully.
@retval   FUNCTION_ERROR     Get demod crystal frequency unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_GET_CRYSTAL_FREQ_HZ() with the corresponding function.

*/
typedef void
(*QAM_DEMOD_FP_GET_CRYSTAL_FREQ_HZ)(
	QAM_DEMOD_MODULE *pDemod,
	unsigned long *pCrystalFreqHz
	);





/**

@brief   QAM demod I2C bus connection asking function pointer

One can use QAM_DEMOD_FP_IS_CONNECTED_TO_I2C() to ask QAM demod if it is connected to I2C bus.


@param [in]    pDemod    The demod module pointer
@param [out]   pAnswer   Pointer to an allocated memory for storing answer


@note
	-# Demod building function will set QAM_DEMOD_FP_IS_CONNECTED_TO_I2C() with the corresponding function.

*/
typedef void
(*QAM_DEMOD_FP_IS_CONNECTED_TO_I2C)(
	QAM_DEMOD_MODULE *pDemod,
	int *pAnswer
	);





/**

@brief   QAM demod software resetting function pointer

One can use QAM_DEMOD_FP_SOFTWARE_RESET() to reset QAM demod by software reset.


@param [in]   pDemod   The demod module pointer


@retval   FUNCTION_SUCCESS   Reset demod by software reset successfully.
@retval   FUNCTION_ERROR     Reset demod by software reset unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_SOFTWARE_RESET() with the corresponding function.

*/
typedef int
(*QAM_DEMOD_FP_SOFTWARE_RESET)(
	QAM_DEMOD_MODULE *pDemod
	);





/**

@brief   QAM demod initializing function pointer

One can use QAM_DEMOD_FP_INITIALIZE() to initialie QAM demod.


@param [in]   pDemod   The demod module pointer


@retval   FUNCTION_SUCCESS   Initialize demod successfully.
@retval   FUNCTION_ERROR     Initialize demod unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_INITIALIZE() with the corresponding function.

*/
typedef int
(*QAM_DEMOD_FP_INITIALIZE)(
	QAM_DEMOD_MODULE *pDemod
	);





/**

@brief   QAM demod QAM mode setting function pointer

One can use QAM_DEMOD_FP_SET_QAM_MODE() to set QAM demod QAM mode.


@param [in]   pDemod    The demod module pointer
@param [in]   QamMode   QAM mode for setting


@retval   FUNCTION_SUCCESS   Set demod QAM mode successfully.
@retval   FUNCTION_ERROR     Set demod QAM mode unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_SET_QAM_MODE() with the corresponding function.


@see   QAM_QAM_MODE

*/
typedef int
(*QAM_DEMOD_FP_SET_QAM_MODE)(
	QAM_DEMOD_MODULE *pDemod,
	int QamMode
	);





/**

@brief   QAM demod symbol rate setting function pointer

One can use QAM_DEMOD_FP_SET_SYMBOL_RATE_HZ() to set QAM demod symbol rate in Hz.


@param [in]   pDemod         The demod module pointer
@param [in]   SymbolRateHz   Symbol rate in Hz for setting


@retval   FUNCTION_SUCCESS   Set demod symbol rate successfully.
@retval   FUNCTION_ERROR     Set demod symbol rate unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_SET_SYMBOL_RATE_HZ() with the corresponding function.

*/
typedef int
(*QAM_DEMOD_FP_SET_SYMBOL_RATE_HZ)(
	QAM_DEMOD_MODULE *pDemod,
	unsigned long SymbolRateHz
	);





/**

@brief   QAM demod alpha mode setting function pointer

One can use QAM_DEMOD_FP_SET_ALPHA_MODE() to set QAM demod alpha mode.


@param [in]   pDemod      The demod module pointer
@param [in]   AlphaMode   Alpha mode for setting


@retval   FUNCTION_SUCCESS   Set demod alpha mode successfully.
@retval   FUNCTION_ERROR     Set demod alpha mode unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_SET_ALPHA_MODE() with the corresponding function.


@see   QAM_ALPHA_MODE

*/
typedef int
(*QAM_DEMOD_FP_SET_ALPHA_MODE)(
	QAM_DEMOD_MODULE *pDemod,
	int AlphaMode
	);





/**

@brief   QAM demod IF frequency setting function pointer

One can use QAM_DEMOD_FP_SET_IF_FREQ_HZ() to set QAM demod IF frequency in Hz.


@param [in]   pDemod     The demod module pointer
@param [in]   IfFreqHz   IF frequency in Hz for setting


@retval   FUNCTION_SUCCESS   Set demod IF frequency successfully.
@retval   FUNCTION_ERROR     Set demod IF frequency unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_SET_IF_FREQ_HZ() with the corresponding function.


@see   IF_FREQ_HZ

*/
typedef int
(*QAM_DEMOD_FP_SET_IF_FREQ_HZ)(
	QAM_DEMOD_MODULE *pDemod,
	unsigned long IfFreqHz
	);





/**

@brief   QAM demod spectrum mode setting function pointer

One can use QAM_DEMOD_FP_SET_SPECTRUM_MODE() to set QAM demod spectrum mode.


@param [in]   pDemod         The demod module pointer
@param [in]   SpectrumMode   Spectrum mode for setting


@retval   FUNCTION_SUCCESS   Set demod spectrum mode successfully.
@retval   FUNCTION_ERROR     Set demod spectrum mode unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_SET_SPECTRUM_MODE() with the corresponding function.


@see   SPECTRUM_MODE

*/
typedef int
(*QAM_DEMOD_FP_SET_SPECTRUM_MODE)(
	QAM_DEMOD_MODULE *pDemod,
	int SpectrumMode
	);





/**

@brief   QAM demod QAM mode getting function pointer

One can use QAM_DEMOD_FP_GET_QAM_MODE() to get QAM demod QAM mode.


@param [in]    pDemod     The demod module pointer
@param [out]   pQamMode   Pointer to an allocated memory for storing demod QAM mode


@retval   FUNCTION_SUCCESS   Get demod QAM mode successfully.
@retval   FUNCTION_ERROR     Get demod QAM mode unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_GET_QAM_MODE() with the corresponding function.


@see   QAM_QAM_MODE

*/
typedef int
(*QAM_DEMOD_FP_GET_QAM_MODE)(
	QAM_DEMOD_MODULE *pDemod,
	int *pQamMode
	);





/**

@brief   QAM demod symbol rate getting function pointer

One can use QAM_DEMOD_FP_GET_SYMBOL_RATE_HZ() to get QAM demod symbol rate in Hz.


@param [in]    pDemod          The demod module pointer
@param [out]   pSymbolRateHz   Pointer to an allocated memory for storing demod symbol rate in Hz


@retval   FUNCTION_SUCCESS   Get demod symbol rate successfully.
@retval   FUNCTION_ERROR     Get demod symbol rate unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_GET_SYMBOL_RATE_HZ() with the corresponding function.

*/
typedef int
(*QAM_DEMOD_FP_GET_SYMBOL_RATE_HZ)(
	QAM_DEMOD_MODULE *pDemod,
	unsigned long *pSymbolRateHz
	);





/**

@brief   QAM demod alpha mode getting function pointer

One can use QAM_DEMOD_FP_GET_ALPHA_MODE() to get QAM demod alpha mode.


@param [in]    pDemod       The demod module pointer
@param [out]   pAlphaMode   Pointer to an allocated memory for storing demod alpha mode


@retval   FUNCTION_SUCCESS   Get demod alpha mode successfully.
@retval   FUNCTION_ERROR     Get demod alpha mode unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_GET_ALPHA_MODE() with the corresponding function.


@see   QAM_ALPHA_MODE

*/
typedef int
(*QAM_DEMOD_FP_GET_ALPHA_MODE)(
	QAM_DEMOD_MODULE *pDemod,
	int *pAlphaMode
	);





/**

@brief   QAM demod IF frequency getting function pointer

One can use QAM_DEMOD_FP_GET_IF_FREQ_HZ() to get QAM demod IF frequency in Hz.


@param [in]    pDemod      The demod module pointer
@param [out]   pIfFreqHz   Pointer to an allocated memory for storing demod IF frequency in Hz


@retval   FUNCTION_SUCCESS   Get demod IF frequency successfully.
@retval   FUNCTION_ERROR     Get demod IF frequency unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_GET_IF_FREQ_HZ() with the corresponding function.


@see   IF_FREQ_HZ

*/
typedef int
(*QAM_DEMOD_FP_GET_IF_FREQ_HZ)(
	QAM_DEMOD_MODULE *pDemod,
	unsigned long *pIfFreqHz
	);





/**

@brief   QAM demod spectrum mode getting function pointer

One can use QAM_DEMOD_FP_GET_SPECTRUM_MODE() to get QAM demod spectrum mode.


@param [in]    pDemod          The demod module pointer
@param [out]   pSpectrumMode   Pointer to an allocated memory for storing demod spectrum mode


@retval   FUNCTION_SUCCESS   Get demod spectrum mode successfully.
@retval   FUNCTION_ERROR     Get demod spectrum mode unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_GET_SPECTRUM_MODE() with the corresponding function.


@see   SPECTRUM_MODE

*/
typedef int
(*QAM_DEMOD_FP_GET_SPECTRUM_MODE)(
	QAM_DEMOD_MODULE *pDemod,
	int *pSpectrumMode
	);





/**

@brief   QAM demod RF AGC getting function pointer

One can use QAM_DEMOD_FP_GET_RF_AGC() to get QAM demod RF AGC value.


@param [in]    pDemod   The demod module pointer
@param [out]   pRfAgc   Pointer to an allocated memory for storing RF AGC value


@retval   FUNCTION_SUCCESS   Get demod RF AGC value successfully.
@retval   FUNCTION_ERROR     Get demod RF AGC value unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_GET_RF_AGC() with the corresponding function.
	-# The range of RF AGC value is (-pow(2, 10)) ~ (pow(2, 10) - 1).

*/
typedef int
(*QAM_DEMOD_FP_GET_RF_AGC)(
	QAM_DEMOD_MODULE *pDemod,
	long *pRfAgc
	);





/**

@brief   QAM demod IF AGC getting function pointer

One can use QAM_DEMOD_FP_GET_IF_AGC() to get QAM demod IF AGC value.


@param [in]    pDemod   The demod module pointer
@param [out]   pIfAgc   Pointer to an allocated memory for storing IF AGC value


@retval   FUNCTION_SUCCESS   Get demod IF AGC value successfully.
@retval   FUNCTION_ERROR     Get demod IF AGC value unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_GET_IF_AGC() with the corresponding function.
	-# The range of IF AGC value is (-pow(2, 10)) ~ (pow(2, 10) - 1).

*/
typedef int
(*QAM_DEMOD_FP_GET_IF_AGC)(
	QAM_DEMOD_MODULE *pDemod,
	long *pIfAgc
	);





/**

@brief   QAM demod digital AGC getting function pointer

One can use QAM_DEMOD_FP_GET_DI_AGC() to get QAM demod digital AGC value.


@param [in]    pDemod   The demod module pointer
@param [out]   pDiAgc   Pointer to an allocated memory for storing digital AGC value


@retval   FUNCTION_SUCCESS   Get demod digital AGC value successfully.
@retval   FUNCTION_ERROR     Get demod digital AGC value unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_GET_DI_AGC() with the corresponding function.
	-# The range of digital AGC value is 0 ~ (pow(2, 27) - 1).

*/
typedef int
(*QAM_DEMOD_FP_GET_DI_AGC)(
	QAM_DEMOD_MODULE *pDemod,
	unsigned long *pDiAgc
	);





/**

@brief   QAM demod TR offset getting function pointer

One can use QAM_DEMOD_FP_GET_TR_OFFSET_PPM() to get TR offset in ppm.


@param [in]    pDemod         The demod module pointer
@param [out]   pTrOffsetPpm   Pointer to an allocated memory for storing TR offset in ppm


@retval   FUNCTION_SUCCESS   Get demod TR offset successfully.
@retval   FUNCTION_ERROR     Get demod TR offset unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_GET_TR_OFFSET_PPM() with the corresponding function.

*/
typedef int
(*QAM_DEMOD_FP_GET_TR_OFFSET_PPM)(
	QAM_DEMOD_MODULE *pDemod,
	long *pTrOffsetPpm
	);





/**

@brief   QAM demod CR offset getting function pointer

One can use QAM_DEMOD_FP_GET_CR_OFFSET_HZ() to get CR offset in Hz.


@param [in]    pDemod        The demod module pointer
@param [out]   pCrOffsetHz   Pointer to an allocated memory for storing CR offset in Hz


@retval   FUNCTION_SUCCESS   Get demod CR offset successfully.
@retval   FUNCTION_ERROR     Get demod CR offset unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_GET_CR_OFFSET_HZ() with the corresponding function.

*/
typedef int
(*QAM_DEMOD_FP_GET_CR_OFFSET_HZ)(
	QAM_DEMOD_MODULE *pDemod,
	long *pCrOffsetHz
	);





/**

@brief   QAM demod AAGC lock asking function pointer

One can use QAM_DEMOD_FP_IS_AAGC_LOCKED() to ask QAM demod if it is AAGC-locked.


@param [in]    pDemod    The demod module pointer
@param [out]   pAnswer   Pointer to an allocated memory for storing answer


@retval   FUNCTION_SUCCESS   Perform AAGC lock asking to demod successfully.
@retval   FUNCTION_ERROR     Perform AAGC lock asking to demod unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_IS_AAGC_LOCKED() with the corresponding function.

*/
typedef int
(*QAM_DEMOD_FP_IS_AAGC_LOCKED)(
	QAM_DEMOD_MODULE *pDemod,
	int *pAnswer
	);





/**

@brief   QAM demod EQ lock asking function pointer

One can use QAM_DEMOD_FP_IS_EQ_LOCKED() to ask QAM demod if it is EQ-locked.


@param [in]    pDemod    The demod module pointer
@param [out]   pAnswer   Pointer to an allocated memory for storing answer


@retval   FUNCTION_SUCCESS   Perform EQ lock asking to demod successfully.
@retval   FUNCTION_ERROR     Perform EQ lock asking to demod unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_IS_EQ_LOCKED() with the corresponding function.

*/
typedef int
(*QAM_DEMOD_FP_IS_EQ_LOCKED)(
	QAM_DEMOD_MODULE *pDemod,
	int *pAnswer
	);





/**

@brief   QAM demod frame lock asking function pointer

One can use QAM_DEMOD_FP_IS_FRAME_LOCKED() to ask QAM demod if it is frame-locked.


@param [in]    pDemod    The demod module pointer
@param [out]   pAnswer   Pointer to an allocated memory for storing answer


@retval   FUNCTION_SUCCESS   Perform frame lock asking to demod successfully.
@retval   FUNCTION_ERROR     Perform frame lock asking to demod unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_IS_FRAME_LOCKED() with the corresponding function.

*/
typedef int
(*QAM_DEMOD_FP_IS_FRAME_LOCKED)(
	QAM_DEMOD_MODULE *pDemod,
	int *pAnswer
	);





/**

@brief   QAM demod error rate value getting function pointer

One can use QAM_DEMOD_FP_GET_ERROR_RATE() to get error rate value.


@param [in]    pDemod          The demod module pointer
@param [in]    TestVolume      Test volume for setting
@param [in]    WaitTimeMsMax   Maximum waiting time in ms
@param [out]   pBerNum         Pointer to an allocated memory for storing BER numerator
@param [out]   pBerDen         Pointer to an allocated memory for storing BER denominator
@param [out]   pPerNum         Pointer to an allocated memory for storing PER numerator
@param [out]   pPerDen         Pointer to an allocated memory for storing PER denominator


@retval   FUNCTION_SUCCESS   Get demod error rate value successfully.
@retval   FUNCTION_ERROR     Get demod error rate value unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_GET_ERROR_RATE() with the corresponding function.
	-# The error test packet number is pow(2, (2 * TestVolume + 4)).

*/
typedef int
(*QAM_DEMOD_FP_GET_ERROR_RATE)(
	QAM_DEMOD_MODULE *pDemod,
	unsigned long TestVolume,
	unsigned int WaitTimeMsMax,
	unsigned long *pBerNum,
	unsigned long *pBerDen,
	unsigned long *pPerNum,
	unsigned long *pPerDen
	);





/**

@brief   QAM demod SNR getting function pointer

One can use QAM_DEMOD_FP_GET_SNR_DB() to get SNR in dB.


@param [in]    pDemod      The demod module pointer
@param [out]   pSnrDbNum   Pointer to an allocated memory for storing SNR dB numerator
@param [out]   pSnrDbDen   Pointer to an allocated memory for storing SNR dB denominator


@retval   FUNCTION_SUCCESS   Get demod SNR successfully.
@retval   FUNCTION_ERROR     Get demod SNR unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_GET_SNR_DB() with the corresponding function.

*/
typedef int
(*QAM_DEMOD_FP_GET_SNR_DB)(
	QAM_DEMOD_MODULE *pDemod,
	long *pSnrDbNum,
	long *pSnrDbDen
	);





/**

@brief   QAM demod signal strength getting function pointer

One can use QAM_DEMOD_FP_GET_SIGNAL_STRENGTH() to get signal strength.


@param [in]    pDemod            The demod module pointer
@param [out]   pSignalStrength   Pointer to an allocated memory for storing signal strength (value = 0 ~ 100)


@retval   FUNCTION_SUCCESS   Get demod signal strength successfully.
@retval   FUNCTION_ERROR     Get demod signal strength unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_GET_SIGNAL_STRENGTH() with the corresponding function.

*/
typedef int
(*QAM_DEMOD_FP_GET_SIGNAL_STRENGTH)(
	QAM_DEMOD_MODULE *pDemod,
	unsigned long *pSignalStrength
	);





/**

@brief   QAM demod signal quality getting function pointer

One can use QAM_DEMOD_FP_GET_SIGNAL_QUALITY() to get signal quality.


@param [in]    pDemod           The demod module pointer
@param [out]   pSignalQuality   Pointer to an allocated memory for storing signal quality (value = 0 ~ 100)


@retval   FUNCTION_SUCCESS   Get demod signal quality successfully.
@retval   FUNCTION_ERROR     Get demod signal quality unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_GET_SIGNAL_QUALITY() with the corresponding function.

*/
typedef int
(*QAM_DEMOD_FP_GET_SIGNAL_QUALITY)(
	QAM_DEMOD_MODULE *pDemod,
	unsigned long *pSignalQuality
	);





/**

@brief   QAM demod updating function pointer

One can use QAM_DEMOD_FP_UPDATE_FUNCTION() to update demod register setting.


@param [in]   pDemod   The demod module pointer


@retval   FUNCTION_SUCCESS   Update demod setting successfully.
@retval   FUNCTION_ERROR     Update demod setting unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_UPDATE_FUNCTION() with the corresponding function.



@par Example:
@code


#include "demod_pseudo.h"


int main(void)
{
	QAM_DEMOD_MODULE *pDemod;
	QAM_DEMOD_MODULE QamDemodModuleMemory;
	PSEUDO_EXTRA_MODULE PseudoExtraModuleMemory;


	// Build pseudo demod module.
	BuildPseudoDemodModule(&pDemod, &QamDemodModuleMemory, &PseudoExtraModuleMemory);

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
(*QAM_DEMOD_FP_UPDATE_FUNCTION)(
	QAM_DEMOD_MODULE *pDemod
	);





/**

@brief   QAM demod reseting function pointer

One can use QAM_DEMOD_FP_RESET_FUNCTION() to reset demod register setting.


@param [in]   pDemod   The demod module pointer


@retval   FUNCTION_SUCCESS   Reset demod setting successfully.
@retval   FUNCTION_ERROR     Reset demod setting unsuccessfully.


@note
	-# Demod building function will set QAM_DEMOD_FP_RESET_FUNCTION() with the corresponding function.



@par Example:
@code


#include "demod_pseudo.h"


int main(void)
{
	QAM_DEMOD_MODULE *pDemod;
	QAM_DEMOD_MODULE QamDemodModuleMemory;
	PSEUDO_EXTRA_MODULE PseudoExtraModuleMemory;


	// Build pseudo demod module.
	BuildPseudoDemodModule(&pDemod, &QamDemodModuleMemory, &PseudoExtraModuleMemory);

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
(*QAM_DEMOD_FP_RESET_FUNCTION)(
	QAM_DEMOD_MODULE *pDemod
	);





/// RTD2885 QAM extra module
typedef struct RTD2885_QAM_EXTRA_MODULE_TAG RTD2885_QAM_EXTRA_MODULE;

// RTD2885 QAM extra module
struct RTD2885_QAM_EXTRA_MODULE_TAG
{
	// Variables
	int ConfigMode;
	unsigned char Func1TickNum;
};





/// RTD2840B QAM extra module
typedef struct RTD2840B_QAM_EXTRA_MODULE_TAG RTD2840B_QAM_EXTRA_MODULE;

// RTD2840B QAM extra module
struct RTD2840B_QAM_EXTRA_MODULE_TAG
{
	// Variables
	int ConfigMode;
	unsigned char Func1TickNum;
};





/// RTD2932 QAM extra module alias
typedef struct RTD2932_QAM_EXTRA_MODULE_TAG RTD2932_QAM_EXTRA_MODULE;

// RTD2932 QAM extra module
struct RTD2932_QAM_EXTRA_MODULE_TAG
{
	// Variables
	int ConfigMode;
	unsigned char Func1TickNum;
};





/// RTL2820 OpenCable extra module alias
typedef struct RTL2820_OC_EXTRA_MODULE_TAG RTL2820_OC_EXTRA_MODULE;

// RTL2820 OpenCable extra module
struct RTL2820_OC_EXTRA_MODULE_TAG
{
	// Variables
	unsigned char Func1TickNum;
};





/// RTD2648 QAM extra module alias
typedef struct RTD2648_QAM_EXTRA_MODULE_TAG RTD2648_QAM_EXTRA_MODULE;

// RTD2648 QAM extra module
struct RTD2648_QAM_EXTRA_MODULE_TAG
{
	// Variables
	int ConfigMode;
};





/// QAM demod module structure
struct QAM_DEMOD_MODULE_TAG
{
	unsigned long CurrentPageNo;
	// Private variables
	int           DemodType;
	unsigned char DeviceAddr;
	unsigned long CrystalFreqHz;
	int           TsInterfaceMode;

	int           QamMode;
	unsigned long SymbolRateHz;
	int           AlphaMode;
	unsigned long IfFreqHz;
	int           SpectrumMode;

	int IsQamModeSet;
	int IsSymbolRateHzSet;
	int IsAlphaModeSet;
	int IsIfFreqHzSet;
	int IsSpectrumModeSet;

	union											///<   Demod extra module used by driving module
	{
		RTD2885_QAM_EXTRA_MODULE  Rtd2885Qam;
		RTD2840B_QAM_EXTRA_MODULE Rtd2840bQam;
		RTD2932_QAM_EXTRA_MODULE  Rtd2932Qam;
		RTL2820_OC_EXTRA_MODULE   Rtl2820Oc;
		RTD2648_QAM_EXTRA_MODULE  Rtd2648Qam;
	}
	Extra;

	BASE_INTERFACE_MODULE *pBaseInterface;
	I2C_BRIDGE_MODULE *pI2cBridge;


	// Register tables
	union
	{
		QAM_BASE_REG_ENTRY_ADDR_8BIT  Addr8Bit[QAM_BASE_REG_TABLE_LEN_MAX];
		QAM_BASE_REG_ENTRY_ADDR_16BIT Addr16Bit[QAM_BASE_REG_TABLE_LEN_MAX];
	}
	BaseRegTable;

	union
	{
		QAM_MONITOR_REG_ENTRY_ADDR_8BIT  Addr8Bit[QAM_MONITOR_REG_TABLE_LEN_MAX];
		QAM_MONITOR_REG_ENTRY_ADDR_16BIT Addr16Bit[QAM_MONITOR_REG_TABLE_LEN_MAX];
	}
	MonitorRegTable;


	// Demod I2C function pointers
	union
	{
		QAM_DEMOD_REG_ACCESS_ADDR_8BIT  Addr8Bit;
		QAM_DEMOD_REG_ACCESS_ADDR_16BIT Addr16Bit;
	}
	RegAccess;


	// Demod manipulating function pointers
	QAM_DEMOD_FP_GET_DEMOD_TYPE        GetDemodType;
	QAM_DEMOD_FP_GET_DEVICE_ADDR       GetDeviceAddr;
	QAM_DEMOD_FP_GET_CRYSTAL_FREQ_HZ   GetCrystalFreqHz;

	QAM_DEMOD_FP_IS_CONNECTED_TO_I2C   IsConnectedToI2c;
	QAM_DEMOD_FP_SOFTWARE_RESET        SoftwareReset;

	QAM_DEMOD_FP_INITIALIZE            Initialize;
	QAM_DEMOD_FP_SET_QAM_MODE          SetQamMode;
	QAM_DEMOD_FP_SET_SYMBOL_RATE_HZ    SetSymbolRateHz;
	QAM_DEMOD_FP_SET_ALPHA_MODE        SetAlphaMode;
	QAM_DEMOD_FP_SET_IF_FREQ_HZ        SetIfFreqHz;
	QAM_DEMOD_FP_SET_SPECTRUM_MODE     SetSpectrumMode;
	QAM_DEMOD_FP_GET_QAM_MODE          GetQamMode;
	QAM_DEMOD_FP_GET_SYMBOL_RATE_HZ    GetSymbolRateHz;
	QAM_DEMOD_FP_GET_ALPHA_MODE        GetAlphaMode;
	QAM_DEMOD_FP_GET_IF_FREQ_HZ        GetIfFreqHz;
	QAM_DEMOD_FP_GET_SPECTRUM_MODE     GetSpectrumMode;

	QAM_DEMOD_FP_GET_RF_AGC            GetRfAgc;
	QAM_DEMOD_FP_GET_IF_AGC            GetIfAgc;
	QAM_DEMOD_FP_GET_DI_AGC            GetDiAgc;
	QAM_DEMOD_FP_GET_TR_OFFSET_PPM     GetTrOffsetPpm;
	QAM_DEMOD_FP_GET_CR_OFFSET_HZ      GetCrOffsetHz;

	QAM_DEMOD_FP_IS_AAGC_LOCKED        IsAagcLocked;
	QAM_DEMOD_FP_IS_EQ_LOCKED          IsEqLocked;
	QAM_DEMOD_FP_IS_FRAME_LOCKED       IsFrameLocked;

	QAM_DEMOD_FP_GET_ERROR_RATE        GetErrorRate;
	QAM_DEMOD_FP_GET_SNR_DB            GetSnrDb;

	QAM_DEMOD_FP_GET_SIGNAL_STRENGTH   GetSignalStrength;
	QAM_DEMOD_FP_GET_SIGNAL_QUALITY    GetSignalQuality;

	QAM_DEMOD_FP_UPDATE_FUNCTION       UpdateFunction;
	QAM_DEMOD_FP_RESET_FUNCTION        ResetFunction;
};







// QAM demod default I2C functions for 8-bit address
int
qam_demod_addr_8bit_default_SetRegPage(
	QAM_DEMOD_MODULE *pDemod,
	unsigned long PageNo
	);

int
qam_demod_addr_8bit_default_SetRegBytes(
	QAM_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	const unsigned char *pWritingBytes,
	unsigned long ByteNum
	);

int
qam_demod_addr_8bit_default_GetRegBytes(
	QAM_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	unsigned char *pReadingBytes,
	unsigned long ByteNum
	);

int
qam_demod_addr_8bit_default_SetRegMaskBits(
	QAM_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	unsigned char Msb,
	unsigned char Lsb,
	const unsigned long WritingValue
	);

int
qam_demod_addr_8bit_default_GetRegMaskBits(
	QAM_DEMOD_MODULE *pDemod,
	unsigned char RegStartAddr,
	unsigned char Msb,
	unsigned char Lsb,
	unsigned long *pReadingValue
	);

int
qam_demod_addr_8bit_default_SetRegBits(
	QAM_DEMOD_MODULE *pDemod,
	int RegBitName,
	const unsigned long WritingValue
	);

int
qam_demod_addr_8bit_default_GetRegBits(
	QAM_DEMOD_MODULE *pDemod,
	int RegBitName,
	unsigned long *pReadingValue
	);

int
qam_demod_addr_8bit_default_SetRegBitsWithPage(
	QAM_DEMOD_MODULE *pDemod,
	int RegBitName,
	const unsigned long WritingValue
	);

int
qam_demod_addr_8bit_default_GetRegBitsWithPage(
	QAM_DEMOD_MODULE *pDemod,
	int RegBitName,
	unsigned long *pReadingValue
	);





// QAM demod default I2C functions for 16-bit address
int
qam_demod_addr_16bit_default_SetRegBytes(
	QAM_DEMOD_MODULE *pDemod,
	unsigned short RegStartAddr,
	const unsigned char *pWritingBytes,
	unsigned long ByteNum
	);

int
qam_demod_addr_16bit_default_GetRegBytes(
	QAM_DEMOD_MODULE *pDemod,
	unsigned short RegStartAddr,
	unsigned char *pReadingBytes,
	unsigned long ByteNum
	);

int
qam_demod_addr_16bit_default_SetRegMaskBits(
	QAM_DEMOD_MODULE *pDemod,
	unsigned short RegStartAddr,
	unsigned char Msb,
	unsigned char Lsb,
	const unsigned long WritingValue
	);

int
qam_demod_addr_16bit_default_GetRegMaskBits(
	QAM_DEMOD_MODULE *pDemod,
	unsigned short RegStartAddr,
	unsigned char Msb,
	unsigned char Lsb,
	unsigned long *pReadingValue
	);

int
qam_demod_addr_16bit_default_SetRegBits(
	QAM_DEMOD_MODULE *pDemod,
	int RegBitName,
	const unsigned long WritingValue
	);

int
qam_demod_addr_16bit_default_GetRegBits(
	QAM_DEMOD_MODULE *pDemod,
	int RegBitName,
	unsigned long *pReadingValue
	);





// QAM demod default manipulating functions
void
qam_demod_default_GetDemodType(
	QAM_DEMOD_MODULE *pDemod,
	int *pDemodType
	);

void
qam_demod_default_GetDeviceAddr(
	QAM_DEMOD_MODULE *pDemod,
	unsigned char *pDeviceAddr
	);

void
qam_demod_default_GetCrystalFreqHz(
	QAM_DEMOD_MODULE *pDemod,
	unsigned long *pCrystalFreqHz
	);

int
qam_demod_default_GetQamMode(
	QAM_DEMOD_MODULE *pDemod,
	int *pQamMode
	);

int
qam_demod_default_GetSymbolRateHz(
	QAM_DEMOD_MODULE *pDemod,
	unsigned long *pSymbolRateHz
	);

int
qam_demod_default_GetAlphaMode(
	QAM_DEMOD_MODULE *pDemod,
	int *pAlphaMode
	);

int
qam_demod_default_GetIfFreqHz(
	QAM_DEMOD_MODULE *pDemod,
	unsigned long *pIfFreqHz
	);

int
qam_demod_default_GetSpectrumMode(
	QAM_DEMOD_MODULE *pDemod,
	int *pSpectrumMode
	);







#endif
