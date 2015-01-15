/**

@file

@brief   RTL2836 demod module definition

One can manipulate RTL2836 demod through RTL2836 module.
RTL2836 module is derived from DTMB demod module.

*/


#include "demod_rtl2836.h"





/**

@brief   RTL2836 demod module builder

Use BuildRtl2836Module() to build RTL2836 module, set all module function pointers with the corresponding
functions, and initialize module private variables.


@param [in]   ppDemod                      Pointer to RTL2836 demod module pointer
@param [in]   pDtmbDemodModuleMemory       Pointer to an allocated DTMB demod module memory
@param [in]   pBaseInterfaceModuleMemory   Pointer to an allocated base interface module memory
@param [in]   pI2cBridgeModuleMemory       Pointer to an allocated I2C bridge module memory
@param [in]   DeviceAddr                   RTL2836 I2C device address
@param [in]   CrystalFreqHz                RTL2836 crystal frequency in Hz
@param [in]   TsInterfaceMode              RTL2836 TS interface mode for setting
@param [in]   UpdateFuncRefPeriodMs        RTL2836 update function reference period in millisecond
@param [in]   IsFunc1Enabled               RTL2836 Function 1 enabling status for setting
@param [in]   IsFunc2Enabled               RTL2836 Function 2 enabling status for setting


@note
	-# One should call BuildRtl2836Module() to build RTL2836 module before using it.

*/
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
	)
{
	DTMB_DEMOD_MODULE *pDemod;
	RTL2836_EXTRA_MODULE *pExtra;



	// Set demod module pointer, 
	*ppDemod = pDtmbDemodModuleMemory;

	// Get demod module.
	pDemod = *ppDemod;

	// Set base interface module pointer and I2C bridge module pointer.
	pDemod->pBaseInterface = pBaseInterfaceModuleMemory;
	pDemod->pI2cBridge     = pI2cBridgeModuleMemory;

	// Get demod extra module.
	pExtra = &(pDemod->Extra.Rtl2836);


	// Set demod type.
	pDemod->DemodType = DTMB_DEMOD_TYPE_RTL2836;

	// Set demod I2C device address.
	pDemod->DeviceAddr = DeviceAddr;

	// Set demod crystal frequency in Hz.
	pDemod->CrystalFreqHz = CrystalFreqHz;

	// Set demod TS interface mode.
	pDemod->TsInterfaceMode = TsInterfaceMode;


	// Initialize demod parameter setting status
	pDemod->IsIfFreqHzSet      = NO;
	pDemod->IsSpectrumModeSet  = NO;


	// Initialize demod register table.
	rtl2836_InitRegTable(pDemod);

	
	// Build I2C birdge module.
	rtl2836_BuildI2cBridgeModule(pDemod);


	// Set demod module I2C function pointers with 8-bit address default functions.
	pDemod->RegAccess.Addr8Bit.SetRegPage         = dtmb_demod_addr_8bit_default_SetRegPage;
	pDemod->RegAccess.Addr8Bit.SetRegBytes        = dtmb_demod_addr_8bit_default_SetRegBytes;
	pDemod->RegAccess.Addr8Bit.GetRegBytes        = dtmb_demod_addr_8bit_default_GetRegBytes;
	pDemod->RegAccess.Addr8Bit.SetRegMaskBits     = dtmb_demod_addr_8bit_default_SetRegMaskBits;
	pDemod->RegAccess.Addr8Bit.GetRegMaskBits     = dtmb_demod_addr_8bit_default_GetRegMaskBits;
	pDemod->RegAccess.Addr8Bit.SetRegBits         = dtmb_demod_addr_8bit_default_SetRegBits;
	pDemod->RegAccess.Addr8Bit.GetRegBits         = dtmb_demod_addr_8bit_default_GetRegBits;
	pDemod->RegAccess.Addr8Bit.SetRegBitsWithPage = dtmb_demod_addr_8bit_default_SetRegBitsWithPage;
	pDemod->RegAccess.Addr8Bit.GetRegBitsWithPage = dtmb_demod_addr_8bit_default_GetRegBitsWithPage;


	// Set demod module manipulating function pointers with default functions.
	pDemod->GetDemodType     = dtmb_demod_default_GetDemodType;
	pDemod->GetDeviceAddr    = dtmb_demod_default_GetDeviceAddr;
	pDemod->GetCrystalFreqHz = dtmb_demod_default_GetCrystalFreqHz;

	pDemod->GetIfFreqHz      = dtmb_demod_default_GetIfFreqHz;
	pDemod->GetSpectrumMode  = dtmb_demod_default_GetSpectrumMode;


	// Set demod module manipulating function pointers with particular functions.
	pDemod->IsConnectedToI2c       = rtl2836_IsConnectedToI2c;
	pDemod->SoftwareReset          = rtl2836_SoftwareReset;
	pDemod->Initialize             = rtl2836_Initialize;
	pDemod->SetIfFreqHz            = rtl2836_SetIfFreqHz;
	pDemod->SetSpectrumMode        = rtl2836_SetSpectrumMode;

	pDemod->IsSignalLocked         = rtl2836_IsSignalLocked;

	pDemod->GetSignalStrength      = rtl2836_GetSignalStrength;
	pDemod->GetSignalQuality       = rtl2836_GetSignalQuality;

	pDemod->GetBer                 = rtl2836_GetBer;
	pDemod->GetPer                 = rtl2836_GetPer;
	pDemod->GetSnrDb               = rtl2836_GetSnrDb;

	pDemod->GetRfAgc               = rtl2836_GetRfAgc;
	pDemod->GetIfAgc               = rtl2836_GetIfAgc;
	pDemod->GetDiAgc               = rtl2836_GetDiAgc;

	pDemod->GetTrOffsetPpm         = rtl2836_GetTrOffsetPpm;
	pDemod->GetCrOffsetHz          = rtl2836_GetCrOffsetHz;

	pDemod->GetCarrierMode         = rtl2836_GetCarrierMode;
	pDemod->GetPnMode              = rtl2836_GetPnMode;
	pDemod->GetQamMode             = rtl2836_GetQamMode;
	pDemod->GetCodeRateMode        = rtl2836_GetCodeRateMode;
	pDemod->GetTimeInterleaverMode = rtl2836_GetTimeInterleaverMode;

	pDemod->UpdateFunction         = rtl2836_UpdateFunction;
	pDemod->ResetFunction          = rtl2836_ResetFunction;


	// Initialize demod Function 1 variables.
	pExtra->IsFunc1Enabled = IsFunc1Enabled;
	pExtra->Func1CntThd = DivideWithCeiling(RTL2836_FUNC1_CHECK_TIME_MS, UpdateFuncRefPeriodMs);
	pExtra->Func1Cnt = 0;

	// Initialize demod Function 2 variables.
	pExtra->IsFunc2Enabled = IsFunc2Enabled;
	pExtra->Func2SignalModePrevious = RTL2836_FUNC2_SIGNAL_NORMAL;


	return;
}





/**

@see   DTMB_DEMOD_FP_IS_CONNECTED_TO_I2C

*/
void
rtl2836_IsConnectedToI2c(
	DTMB_DEMOD_MODULE *pDemod,
	int *pAnswer
	)
{
	unsigned long ChipId;



	// Get CHIP_ID.
	if(pDemod->RegAccess.Addr8Bit.GetRegBitsWithPage(pDemod, DTMB_CHIP_ID, &ChipId) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	// Check chip ID value.
	if(ChipId != RTL2836_CHIP_ID_VALUE)
		goto error_status_check_value;


	// Set I2cConnectionStatus with YES.
	*pAnswer = YES;


	return;


error_status_check_value:
error_status_get_registers:

	// Set I2cConnectionStatus with NO.
	*pAnswer = NO;


	return;
}





/**

@see   DTMB_DEMOD_FP_SOFTWARE_RESET

*/
int
rtl2836_SoftwareReset(
	DTMB_DEMOD_MODULE *pDemod
	)
{
	// Set SOFT_RST with 0x0. Then, set SOFT_RST with 0x1.
	if(pDemod->RegAccess.Addr8Bit.SetRegBitsWithPage(pDemod, DTMB_SOFT_RST_N, 0x0) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

	if(pDemod->RegAccess.Addr8Bit.SetRegBitsWithPage(pDemod, DTMB_SOFT_RST_N, 0x1) != FUNCTION_SUCCESS)
		goto error_status_set_registers;


	return FUNCTION_SUCCESS;


error_status_set_registers:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_INITIALIZE

*/
int
rtl2836_Initialize(
	DTMB_DEMOD_MODULE *pDemod
	)
{
	// Initializing table entry only used in Initialize()
	typedef struct
	{
		unsigned char PageNo;
		unsigned char RegStartAddr;
		unsigned char Msb;
		unsigned char Lsb;
		unsigned long WritingValue;
	}
	INIT_TABLE_ENTRY;

	// TS interface initializing table entry only used in Initialize()
	typedef struct
	{
		int RegBitName;
		unsigned long WritingValue[TS_INTERFACE_MODE_NUM];
	}
	TS_INTERFACE_INIT_TABLE_ENTRY;



	static const INIT_TABLE_ENTRY InitRegTable[RTL2836_INIT_REG_TABLE_LEN] =
	{
		// PageNo,	RegStartAddr,	Msb,	Lsb,	WritingValue
		{0x0,		0x1,			0,		0,		0x1			},
		{0x0,		0x2,			4,		4,		0x0			},
		{0x0,		0x3,			2,		0,		0x0			},
		{0x0,		0xe,			5,		5,		0x1			},
		{0x0,		0x11,			3,		3,		0x0			},
		{0x0,		0x12,			1,		0,		0x1			},
		{0x0,		0x16,			2,		0,		0x3			},
		{0x0,		0x19,			7,		0,		0x19		},
		{0x0,		0x1b,			7,		0,		0xcc		},
		{0x0,		0x1f,			7,		0,		0x5			},
		{0x0,		0x20,			2,		2,		0x1			},
		{0x0,		0x20,			3,		3,		0x0			},
		{0x1,		0x3,			7,		0,		0x38		},
		{0x1,		0x31,			1,		1,		0x0			},
		{0x1,		0x67,			7,		0,		0x30		},
		{0x1,		0x68,			7,		0,		0x10		},
		{0x1,		0x7f,			3,		2,		0x1			},
		{0x1,		0xda,			7,		7,		0x1			},
		{0x1,		0xdb,			7,		0,		0x5			},
		{0x2,		0x9,			7,		0,		0xa			},
		{0x2,		0x10,			7,		0,		0x31		},
		{0x2,		0x11,			7,		0,		0x31		},
		{0x2,		0x1b,			7,		0,		0x1e		},
		{0x2,		0x1e,			7,		0,		0x3a		},
		{0x2,		0x1f,			5,		3,		0x3			},
		{0x2,		0x21,			7,		0,		0x3f		},
		{0x2,		0x24,			6,		5,		0x0			},
		{0x2,		0x27,			7,		0,		0x17		},
		{0x2,		0x31,			7,		0,		0x35		},
		{0x2,		0x32,			7,		0,		0x3f		},
		{0x2,		0x4f,			3,		2,		0x2			},
		{0x2,		0x5a,			7,		0,		0x5			},
		{0x2,		0x5b,			7,		0,		0x8			},
		{0x2,		0x5c,			7,		0,		0x8			},
		{0x2,		0x5e,			7,		5,		0x5			},
		{0x2,		0x70,			0,		0,		0x0			},
		{0x2,		0x77,			0,		0,		0x1			},
		{0x2,		0x7a,			7,		0,		0x2f		},
		{0x2,		0x81,			3,		2,		0x2			},
		{0x2,		0x8d,			7,		0,		0x77		},
		{0x2,		0x8e,			7,		4,		0x8			},
		{0x2,		0x93,			7,		0,		0xff		},
		{0x2,		0x94,			7,		0,		0x3			},
		{0x2,		0x9d,			7,		0,		0xff		},
		{0x2,		0x9e,			7,		0,		0x3			},
		{0x2,		0xa8,			7,		0,		0xff		},
		{0x2,		0xa9,			7,		0,		0x3			},
		{0x2,		0xa3,			2,		2,		0x1			},
		{0x3,		0x1,			7,		0,		0x0			},
		{0x3,		0x4,			7,		0,		0x20		},
		{0x3,		0x9,			7,		0,		0x10		},
		{0x3,		0x14,			7,		0,		0xe4		},
		{0x3,		0x15,			7,		0,		0x62		},
		{0x3,		0x16,			7,		0,		0x8c		},
		{0x3,		0x17,			7,		0,		0x11		},
		{0x3,		0x1b,			7,		0,		0x40		},
		{0x3,		0x1c,			7,		0,		0x14		},
		{0x3,		0x23,			7,		0,		0x40		},
		{0x3,		0x24,			7,		0,		0xd6		},
		{0x3,		0x2b,			7,		0,		0x60		},
		{0x3,		0x2c,			7,		0,		0x16		},
		{0x3,		0x33,			7,		0,		0x40		},
		{0x3,		0x3b,			7,		0,		0x44		},
		{0x3,		0x43,			7,		0,		0x41		},
		{0x3,		0x4b,			7,		0,		0x40		},
		{0x3,		0x53,			7,		0,		0x4a		},
		{0x3,		0x58,			7,		0,		0x1c		},
		{0x3,		0x5b,			7,		0,		0x5a		},
		{0x3,		0x5f,			7,		0,		0xe0		},
		{0x4,		0x2,			7,		0,		0x7			},
		{0x4,		0x3,			5,		0,		0x9			},
		{0x4,		0x4,			5,		0,		0xb			},
		{0x4,		0x5,			5,		0,		0xd			},
		{0x4,		0x7,			2,		1,		0x3			},
		{0x4,		0x7,			4,		3,		0x3			},
		{0x4,		0xe,			4,		0,		0x18		},
		{0x4,		0x10,			4,		0,		0x1c		},
		{0x4,		0x12,			4,		0,		0x1c		},
		{0x4,		0x2f,			7,		0,		0x0			},
		{0x4,		0x30,			7,		0,		0x20		},
		{0x4,		0x31,			7,		0,		0x40		},
		{0x4,		0x3e,			0,		0,		0x0			},
		{0x4,		0x3e,			1,		1,		0x1			},
		{0x4,		0x3e,			5,		2,		0x0			},
		{0x4,		0x3f,			5,		0,		0x10		},
		{0x4,		0x4a,			0,		0,		0x1			},
	};

	static const TS_INTERFACE_INIT_TABLE_ENTRY TsInterfaceInitTable[RTL2836_TS_INTERFACE_INIT_TABLE_LEN] =
	{
		// RegBitName,				WritingValue for {Parallel, Serial}
		{DTMB_SERIAL,				{0x0,	0x1}},
		{DTMB_CDIV_PH0,				{0xf,	0x1}},
		{DTMB_CDIV_PH1,				{0xf,	0x1}},
	};

	int i;

	int TsInterfaceMode;

	unsigned char PageNo;
	unsigned char RegStartAddr;
	unsigned char Msb;
	unsigned char Lsb;
	unsigned long WritingValue;



	// Get TS interface mode.
	TsInterfaceMode = pDemod->TsInterfaceMode;

	// Initialize demod registers according to the initializing table.
	for(i = 0; i < RTL2836_INIT_REG_TABLE_LEN; i++)
	{
		// Get all information from each register initializing entry.
		PageNo       = InitRegTable[i].PageNo;
		RegStartAddr = InitRegTable[i].RegStartAddr;
		Msb          = InitRegTable[i].Msb;
		Lsb          = InitRegTable[i].Lsb;
		WritingValue = InitRegTable[i].WritingValue;

		// Set register page number.
		if(pDemod->RegAccess.Addr8Bit.SetRegPage(pDemod, PageNo) != FUNCTION_SUCCESS)
			goto error_status_set_registers;

		// Set register mask bits.
		if(pDemod->RegAccess.Addr8Bit.SetRegMaskBits(pDemod, RegStartAddr, Msb, Lsb, WritingValue) != FUNCTION_SUCCESS)
			goto error_status_set_registers;
	}

	// Initialize demod registers according to the TS interface initializing table.
	for(i = 0; i < RTL2836_TS_INTERFACE_INIT_TABLE_LEN; i++)
	{
		if(pDemod->RegAccess.Addr8Bit.SetRegBitsWithPage(pDemod, TsInterfaceInitTable[i].RegBitName,
			TsInterfaceInitTable[i].WritingValue[TsInterfaceMode]) != FUNCTION_SUCCESS)
			goto error_status_set_registers;
	}


	return FUNCTION_SUCCESS;


error_status_set_registers:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_SET_IF_FREQ_HZ

*/
int
rtl2836_SetIfFreqHz(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned long IfFreqHz
	)
{

	unsigned long BbinEn, EnDcr;

	unsigned long IfFreqHzAdj;

	MPI MpiVar, MpiNone, MpiConst;

	long IffreqInt;
	unsigned long IffreqBinary;



	// Determine and set BBIN_EN and EN_DCR value.
	BbinEn = (IfFreqHz == IF_FREQ_0HZ) ? 0x1 : 0x0;
	EnDcr  = (IfFreqHz == IF_FREQ_0HZ) ? 0x1 : 0x0;

	if(pDemod->RegAccess.Addr8Bit.SetRegBitsWithPage(pDemod, DTMB_BBIN_EN, BbinEn) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

	if(pDemod->RegAccess.Addr8Bit.SetRegBitsWithPage(pDemod, DTMB_EN_DCR, EnDcr) != FUNCTION_SUCCESS)
		goto error_status_set_registers;


	// Calculate IFFREQ value.
	// Note: Case 1: IfFreqHz < 24000000,  IfFreqHzAdj = IfFreqHz;
	//       Case 2: IfFreqHz >= 24000000, IfFreqHzAdj = 48000000 - IfFreqHz; 
	//       IFFREQ = - round( IfFreqHzAdj * pow(2, 10) / 48000000 )
	//              = - floor( (IfFreqHzAdj * pow(2, 10) + 24000000) / 48000000 )
	//       RTL2836_ADC_FREQ_HZ = 48 MHz
	//       IFFREQ_BIT_NUM = 10
	IfFreqHzAdj = (IfFreqHz < (RTL2836_ADC_FREQ_HZ / 2)) ? IfFreqHz : (RTL2836_ADC_FREQ_HZ - IfFreqHz);

	MpiSetValue(&MpiVar, IfFreqHzAdj);
	MpiLeftShift(&MpiVar, MpiVar, RTL2836_IFFREQ_BIT_NUM);

	MpiSetValue(&MpiConst, (RTL2836_ADC_FREQ_HZ / 2));
	MpiAdd(&MpiVar, MpiVar, MpiConst);

	MpiSetValue(&MpiConst, RTL2836_ADC_FREQ_HZ);
	MpiDiv(&MpiVar, &MpiNone, MpiVar, MpiConst);

	MpiGetValue(MpiVar, &IffreqInt);
	IffreqInt = - IffreqInt;

	IffreqBinary = SignedIntToBin(IffreqInt, RTL2836_IFFREQ_BIT_NUM);


	// Set IFFREQ with calculated value.
	// Note: Use SetRegBitsWithPage() to set register bits with page setting.
	if(pDemod->RegAccess.Addr8Bit.SetRegBitsWithPage(pDemod, DTMB_IFFREQ, IffreqBinary) != FUNCTION_SUCCESS)
		goto error_status_set_registers;


	// Set demod IF frequnecy parameter.
	pDemod->IfFreqHz      = IfFreqHz;
	pDemod->IsIfFreqHzSet = YES;


	return FUNCTION_SUCCESS;


error_status_set_registers:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_SET_SPECTRUM_MODE

*/
int
rtl2836_SetSpectrumMode(
	DTMB_DEMOD_MODULE *pDemod,
	int SpectrumMode
	)
{
	unsigned long EnSpInv;



	// Determine SpecInv according to spectrum mode.
	switch(SpectrumMode)
	{
		case SPECTRUM_NORMAL:		EnSpInv = 0;		break;
		case SPECTRUM_INVERSE:		EnSpInv = 1;		break;

		default:	goto error_status_get_undefined_value;
	}


	// Set SPEC_INV with SpecInv.
	if(pDemod->RegAccess.Addr8Bit.SetRegBitsWithPage(pDemod, DTMB_EN_SP_INV, EnSpInv) != FUNCTION_SUCCESS)
		goto error_status_set_registers;


	// Set demod spectrum mode parameter.
	pDemod->SpectrumMode      = SpectrumMode;
	pDemod->IsSpectrumModeSet = YES;


	return FUNCTION_SUCCESS;


error_status_set_registers:
error_status_get_undefined_value:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_IS_SIGNAL_LOCKED

*/
int
rtl2836_IsSignalLocked(
	DTMB_DEMOD_MODULE *pDemod,
	int *pAnswer
	)
{
	long SnrDbNum;
	long SnrDbDen;
	long SnrDbInt;

	unsigned long PerNum;
	unsigned long PerDen;



	// Get SNR integer part.
	if(pDemod->GetSnrDb(pDemod, &SnrDbNum, &SnrDbDen) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	SnrDbInt = SnrDbNum / SnrDbDen;


	// Get PER.
	if(pDemod->GetPer(pDemod, &PerNum, &PerDen) != FUNCTION_SUCCESS)
		goto error_status_execute_function;


	// Determine answer according to SNR and PER.
	// Note: The criterion is "(0 < SNR_in_Db < 40) && (PER < 1)"
	if((SnrDbInt > 0) && (SnrDbInt < 40) && (PerNum < PerDen))
		*pAnswer = YES;
	else
		*pAnswer = NO;


	return FUNCTION_SUCCESS;


error_status_execute_function:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_GET_SIGNAL_STRENGTH

*/
int
rtl2836_GetSignalStrength(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned long *pSignalStrength
	)
{
	int SignalLockStatus;
	long IfAgc;



	// Get signal lock status.
	if(pDemod->IsSignalLocked(pDemod, &SignalLockStatus) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	// Get IF AGC value.
	if(pDemod->GetIfAgc(pDemod, &IfAgc) != FUNCTION_SUCCESS)
		goto error_status_get_registers;


	// If demod is not signal-locked, set signal strength with zero.
	if(SignalLockStatus != YES)
	{
		*pSignalStrength = 0;
		goto success_status_signal_is_not_locked;
	}

	//  Determine signal strength according to signal lock status and IF AGC value.
	// Note: Map IfAgc value 8191 ~ -8192 to 10 ~ 99
	//       Formula: SignalStrength = 54 - IfAgc / 183
	*pSignalStrength = 54 - IfAgc / 183;


success_status_signal_is_not_locked:
	return FUNCTION_SUCCESS;


error_status_get_registers:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_GET_SIGNAL_QUALITY

*/
int
rtl2836_GetSignalQuality(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned long *pSignalQuality
	)
{
	int SignalLockStatus;
	long SnrDbNum, SnrDbDen;



	// Get signal lock status.
	if(pDemod->IsSignalLocked(pDemod, &SignalLockStatus) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	if(pDemod->GetSnrDb(pDemod, &SnrDbNum, &SnrDbDen) != FUNCTION_SUCCESS)
		goto error_status_get_registers;


	// If demod is not signal-locked, set signal quality with zero.
	if(SignalLockStatus != YES)
	{
		*pSignalQuality = 0;
		goto success_status_signal_is_not_locked;
	}

	// Determine signal quality according to SnrDbNum.
	// Note: Map SnrDbNum value 12 ~ 100 to 12 ~ 100
	//       Formula: SignalQuality = SnrDbNum
	//       If SnrDbNum < 12, signal quality is 10.
	//       If SnrDbNum > 100, signal quality is 100.
	if(SnrDbNum < 12)
	{
		*pSignalQuality = 10;
	}
	else if(SnrDbNum > 100)
	{
		*pSignalQuality = 100;
	}
	else
	{
		*pSignalQuality = SnrDbNum;
	}


success_status_signal_is_not_locked:
	return FUNCTION_SUCCESS;


error_status_get_registers:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_GET_BER

*/
int
rtl2836_GetBer(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned long *pBerNum,
	unsigned long *pBerDen
	)
{
/*
	unsigned long RsdBerEst;



	// Get RSD_BER_EST.
	if(pDemod->GetRegBitsWithPage(pDemod, DTMB_RSD_BER_EST, &RsdBerEst) != FUNCTION_SUCCESS)
		goto error_status_get_registers;


	// Set BER numerator according to RSD_BER_EST.
	*pBerNum = RsdBerEst;

	// Set BER denominator.
	*pBerDen = RTL2836_BER_DEN_VALUE;


	return FUNCTION_SUCCESS;


error_status_get_registers:
	return FUNCTION_ERROR;
*/
	return FUNCTION_SUCCESS;
}





/**

@see   DTMB_DEMOD_FP_GET_PER

*/
int
rtl2836_GetPer(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned long *pPerNum,
	unsigned long *pPerDen
	)
{
	unsigned long RoPktErrRate;



	// Get RO_PKT_ERR_RATE.
	// Note: The function GetRegBitsWithPage() will set register page automatically.
	if(pDemod->RegAccess.Addr8Bit.GetRegBitsWithPage(pDemod, DTMB_RO_PKT_ERR_RATE, &RoPktErrRate) != FUNCTION_SUCCESS)
		goto error_status_get_registers;


	// Set PER numerator according to RO_PKT_ERR_RATE.
	*pPerNum = RoPktErrRate;

	// Set PER denominator.
	*pPerDen = RTL2836_PER_DEN_VALUE;


	return FUNCTION_SUCCESS;


error_status_get_registers:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_GET_SNR_DB

*/
int
rtl2836_GetSnrDb(
	DTMB_DEMOD_MODULE *pDemod,
	long *pSnrDbNum,
	long *pSnrDbDen
	)
{
	unsigned long EstSnr;



	// Get EST_SNR.
	// Note: The function GetRegBitsWithPage() will set register page automatically.
	if(pDemod->RegAccess.Addr8Bit.GetRegBitsWithPage(pDemod, DTMB_EST_SNR, &EstSnr) != FUNCTION_SUCCESS)
		goto error_status_get_registers;


	// Set SNR dB numerator according to EST_SNR.
	*pSnrDbNum = BinToSignedInt(EstSnr, RTL2836_EST_SNR_BIT_NUM);

	// Set SNR dB denominator.
	*pSnrDbDen = RTL2836_SNR_DB_DEN_VALUE;


	return FUNCTION_SUCCESS;


error_status_get_registers:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_GET_RF_AGC

*/
int
rtl2836_GetRfAgc(
	DTMB_DEMOD_MODULE *pDemod,
	long *pRfAgc
	)
{
	unsigned long RfAgcVal;



	// Get RF AGC binary value from RF_AGC_VAL.
	// Note: The function GetRegBitsWithPage() will set register page automatically.
	if(pDemod->RegAccess.Addr8Bit.GetRegBitsWithPage(pDemod, DTMB_RF_AGC_VAL, &RfAgcVal) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	// Convert RF AGC binary value to signed integer.
	*pRfAgc = (long)BinToSignedInt(RfAgcVal, RTL2836_RF_AGC_REG_BIT_NUM);


	return FUNCTION_SUCCESS;


error_status_get_registers:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_GET_IF_AGC

*/
int
rtl2836_GetIfAgc(
	DTMB_DEMOD_MODULE *pDemod,
	long *pIfAgc
	)
{
	unsigned long IfAgcVal;



	// Get IF AGC binary value from IF_AGC_VAL.
	// Note: The function GetRegBitsWithPage() will set register page automatically.
	if(pDemod->RegAccess.Addr8Bit.GetRegBitsWithPage(pDemod, DTMB_IF_AGC_VAL, &IfAgcVal) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	// Convert IF AGC binary value to signed integer.
	*pIfAgc = (long)BinToSignedInt(IfAgcVal, RTL2836_IF_AGC_REG_BIT_NUM);


	return FUNCTION_SUCCESS;


error_status_get_registers:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_GET_DI_AGC

*/
int
rtl2836_GetDiAgc(
	DTMB_DEMOD_MODULE *pDemod,
	unsigned long *pDiAgc
	)
{
	unsigned long GainOutR;



	// Get GAIN_OUT_R to DiAgc.
	// Note: The function GetRegBitsWithPage() will set register page automatically.
	if(pDemod->RegAccess.Addr8Bit.GetRegBitsWithPage(pDemod, DTMB_GAIN_OUT_R, &GainOutR) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	*pDiAgc = GainOutR;


	return FUNCTION_SUCCESS;


error_status_get_registers:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_GET_TR_OFFSET_PPM

*/
int
rtl2836_GetTrOffsetPpm(
	DTMB_DEMOD_MODULE *pDemod,
	long *pTrOffsetPpm
	)
{
	unsigned long TrOutRBinary;
	long TrOutRInt;
	unsigned long SfoaqOutRBinary;
	long SfoaqOutRInt;

	MPI MpiVar, MpiNone, MpiConst;


	// Get TR_OUT_R and SFOAQ_OUT_R binary value.
	// Note: The function GetRegBitsWithPage() will set register page automatically.
	if(pDemod->RegAccess.Addr8Bit.GetRegBitsWithPage(pDemod, DTMB_TR_OUT_R, &TrOutRBinary) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;

	if(pDemod->RegAccess.Addr8Bit.GetRegBitsWithPage(pDemod, DTMB_SFOAQ_OUT_R, &SfoaqOutRBinary) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;


	// Convert TR_OUT_R and SFOAQ_OUT_R binary value to signed integer.
	TrOutRInt = BinToSignedInt(TrOutRBinary, RTL2836_TR_OUT_R_BIT_NUM);
	SfoaqOutRInt = BinToSignedInt(SfoaqOutRBinary, RTL2836_SFOAQ_OUT_R_BIT_NUM);

	
	// Get TR offset in ppm.
	// Note: Original formula:   TrOffsetPpm = ((TrOutRInt + SfoaqOutRInt * 8) * 15.12 * pow(10, 6)) / (48 * pow(2, 23))
	//       Adjusted formula:   TrOffsetPpm = ((TrOutRInt + SfoaqOutRInt * 8) * 15120000) / 402653184
	MpiSetValue(&MpiVar, (TrOutRInt + SfoaqOutRInt * 8));

	MpiSetValue(&MpiConst, 15120000);
	MpiMul(&MpiVar, MpiVar, MpiConst);

	MpiSetValue(&MpiConst, 402653184);
	MpiDiv(&MpiVar, &MpiNone, MpiVar, MpiConst);

	MpiGetValue(MpiVar, pTrOffsetPpm);


	return FUNCTION_SUCCESS;


error_status_get_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_GET_CR_OFFSET_HZ

*/
int
rtl2836_GetCrOffsetHz(
	DTMB_DEMOD_MODULE *pDemod,
	long *pCrOffsetHz
	)
{
	unsigned long CfoEstRBinary;
	long CfoEstRInt;

	MPI MpiVar, MpiConst;


	// Get CFO_EST_R binary value.
	// Note: The function GetRegBitsWithPage() will set register page automatically.
	if(pDemod->RegAccess.Addr8Bit.GetRegBitsWithPage(pDemod, DTMB_CFO_EST_R, &CfoEstRBinary) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;

	// Convert CFO_EST_R binary value to signed integer.
	CfoEstRInt = BinToSignedInt(CfoEstRBinary, RTL2836_CFO_EST_R_BIT_NUM);

	
	// Get CR offset in Hz.
	// Note: Original formula:   CrOffsetHz = (CfoEstRInt * 15.12 * pow(10, 6)) / pow(2, 26)
	//       Adjusted formula:   CrOffsetHz = (CfoEstRInt * 15120000) >> 26
	MpiSetValue(&MpiVar, CfoEstRInt);

	MpiSetValue(&MpiConst, 15120000);
	MpiMul(&MpiVar, MpiVar, MpiConst);

	MpiRightShift(&MpiVar, MpiVar, 26);

	MpiGetValue(MpiVar, pCrOffsetHz);


	return FUNCTION_SUCCESS;


error_status_get_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_GET_CARRIER_MODE

*/
int
rtl2836_GetCarrierMode(
	DTMB_DEMOD_MODULE *pDemod,
	int *pCarrierMode
	)
{
	unsigned long EstCarrier;


	// Get carrier mode from EST_CARRIER.
	if(pDemod->RegAccess.Addr8Bit.GetRegBitsWithPage(pDemod, DTMB_EST_CARRIER, &EstCarrier) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	switch(EstCarrier)
	{
		case 0:		*pCarrierMode = DTMB_CARRIER_SINGLE;		break;
		case 1:		*pCarrierMode = DTMB_CARRIER_MULTI;			break;

		default:	goto error_status_get_undefined_value;
	}


	return FUNCTION_SUCCESS;


error_status_get_registers:
error_status_get_undefined_value:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_GET_PN_MODE

*/
int
rtl2836_GetPnMode(
	DTMB_DEMOD_MODULE *pDemod,
	int *pPnMode
	)
{
	unsigned long RxModeR;


	// Get PN mode from RX_MODE_R.
	if(pDemod->RegAccess.Addr8Bit.GetRegBitsWithPage(pDemod, DTMB_RX_MODE_R, &RxModeR) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	switch(RxModeR)
	{
		case 0:		*pPnMode = DTMB_PN_420;		break;
		case 1:		*pPnMode = DTMB_PN_595;		break;
		case 2:		*pPnMode = DTMB_PN_945;		break;

		default:	goto error_status_get_undefined_value;
	}


	return FUNCTION_SUCCESS;


error_status_get_registers:
error_status_get_undefined_value:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_GET_QAM_MODE

*/
int
rtl2836_GetQamMode(
	DTMB_DEMOD_MODULE *pDemod,
	int *pQamMode
	)
{
	unsigned long UseTps;


	// Get QAM mode from USE_TPS.
	if(pDemod->RegAccess.Addr8Bit.GetRegBitsWithPage(pDemod, DTMB_USE_TPS, &UseTps) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	switch(UseTps)
	{
		case 0:		*pQamMode = DTMB_QAM_UNKNOWN;		break;

		case 2:
		case 3:		*pQamMode = DTMB_QAM_4QAM_NR;		break;

		case 4:
		case 5:
		case 6:
		case 7:
		case 8:
		case 9:		*pQamMode = DTMB_QAM_4QAM;			break;

		case 10:
		case 11:
		case 12:
		case 13:
		case 14:
		case 15:	*pQamMode = DTMB_QAM_16QAM;			break;

		case 16:
		case 17:	*pQamMode = DTMB_QAM_32QAM;			break;

		case 18:
		case 19:
		case 20:
		case 21:
		case 22:
		case 23:	*pQamMode = DTMB_QAM_64QAM;			break;

		default:	goto error_status_get_undefined_value;
	}


	return FUNCTION_SUCCESS;


error_status_get_registers:
error_status_get_undefined_value:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_GET_CODE_RATE_MODE

*/
int
rtl2836_GetCodeRateMode(
	DTMB_DEMOD_MODULE *pDemod,
	int *pCodeRateMode
	)
{
	unsigned long UseTps;


	// Get QAM mode from USE_TPS.
	if(pDemod->RegAccess.Addr8Bit.GetRegBitsWithPage(pDemod, DTMB_USE_TPS, &UseTps) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	switch(UseTps)
	{
		case 0:		*pCodeRateMode = DTMB_CODE_RATE_UNKNOWN;		break;

		case 4:
		case 5:
		case 10:
		case 11:
		case 18:
		case 19:	*pCodeRateMode = DTMB_CODE_RATE_0P4;			break;

		case 6:
		case 7:
		case 12:
		case 13:
		case 20:
		case 21:	*pCodeRateMode = DTMB_CODE_RATE_0P6;			break;

		case 2:
		case 3:
		case 8:
		case 9:
		case 14:
		case 15:
		case 16:
		case 17:
		case 22:
		case 23:	*pCodeRateMode = DTMB_CODE_RATE_0P8;			break;

		default:	goto error_status_get_undefined_value;
	}


	return FUNCTION_SUCCESS;


error_status_get_registers:
error_status_get_undefined_value:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_GET_TIME_INTERLEAVER_MODE

*/
int
rtl2836_GetTimeInterleaverMode(
	DTMB_DEMOD_MODULE *pDemod,
	int *pTimeInterleaverMode
	)
{
	unsigned long UseTps;


	// Get QAM mode from USE_TPS.
	if(pDemod->RegAccess.Addr8Bit.GetRegBitsWithPage(pDemod, DTMB_USE_TPS, &UseTps) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	switch(UseTps)
	{
		case 0:		*pTimeInterleaverMode = DTMB_TIME_INTERLEAVER_UNKNOWN;		break;

		case 2:
		case 4:
		case 6:
		case 8:
		case 10:
		case 12:
		case 14:
		case 16:
		case 18:
		case 20:
		case 22:	*pTimeInterleaverMode = DTMB_TIME_INTERLEAVER_240;			break;

		case 3:
		case 5:
		case 7:
		case 9:
		case 11:
		case 13:
		case 15:
		case 17:
		case 19:
		case 21:
		case 23:	*pTimeInterleaverMode = DTMB_TIME_INTERLEAVER_720;			break;

		default:	goto error_status_get_undefined_value;
	}


	return FUNCTION_SUCCESS;


error_status_get_registers:
error_status_get_undefined_value:
	return FUNCTION_ERROR;
}





/**

@see   DVBT_DEMOD_FP_UPDATE_FUNCTION

*/
int
rtl2836_UpdateFunction(
	DTMB_DEMOD_MODULE *pDemod
	)
{
	RTL2836_EXTRA_MODULE *pExtra;


	// Get demod extra module.
	pExtra = &(pDemod->Extra.Rtl2836);


	// Execute Function 1 according to Function 1 enabling status
	if(pExtra->IsFunc1Enabled == YES)
	{
		if(rtl2836_func1_Update(pDemod) != FUNCTION_SUCCESS)
			goto error_status_execute_function;
	}

	// Execute Function 2 according to Function 2 enabling status
	if(pExtra->IsFunc2Enabled == YES)
	{
		if(rtl2836_func2_Update(pDemod) != FUNCTION_SUCCESS)
			goto error_status_execute_function;
	}


	return FUNCTION_SUCCESS;


error_status_execute_function:
	return FUNCTION_ERROR;
}





/**

@see   DTMB_DEMOD_FP_RESET_FUNCTION

*/
int
rtl2836_ResetFunction(
	DTMB_DEMOD_MODULE *pDemod
	)
{
	RTL2836_EXTRA_MODULE *pExtra;


	// Get demod extra module.
	pExtra = &(pDemod->Extra.Rtl2836);


	// Reset Function 1 settings according to Function 1 enabling status.
	if(pExtra->IsFunc1Enabled == YES)
	{
		if(rtl2836_func1_Reset(pDemod) != FUNCTION_SUCCESS)
			goto error_status_execute_function;
	}

	// Reset Function 2 settings according to Function 2 enabling status.
	if(pExtra->IsFunc2Enabled == YES)
	{
		if(rtl2836_func2_Reset(pDemod) != FUNCTION_SUCCESS)
			goto error_status_execute_function;
	}


	return FUNCTION_SUCCESS;


error_status_execute_function:
	return FUNCTION_ERROR;
}





/**

@see   I2C_BRIDGE_FP_FORWARD_I2C_READING_CMD

*/
int
rtl2836_ForwardI2cReadingCmd(
	I2C_BRIDGE_MODULE *pI2cBridge,
	unsigned char DeviceAddr,
	unsigned char *pReadingBytes,
	unsigned long ByteNum
	)
{
	DTMB_DEMOD_MODULE *pDemod;
	BASE_INTERFACE_MODULE *pBaseInterface;



	// Get demod module.
	pDemod = (DTMB_DEMOD_MODULE *)pI2cBridge->pPrivateData;


	// Get base interface.
	pBaseInterface = pDemod->pBaseInterface;


	// Send I2C reading command.
	if(pBaseInterface->I2cRead(pBaseInterface, DeviceAddr, pReadingBytes, ByteNum) != FUNCTION_SUCCESS)
		goto error_send_i2c_reading_command;


	return FUNCTION_SUCCESS;


error_send_i2c_reading_command:
	return FUNCTION_ERROR;
}





/**

@see   I2C_BRIDGE_FP_FORWARD_I2C_WRITING_CMD

*/
int
rtl2836_ForwardI2cWritingCmd(
	I2C_BRIDGE_MODULE *pI2cBridge,
	unsigned char DeviceAddr,
	const unsigned char *pWritingBytes,
	unsigned long ByteNum
	)
{
	DTMB_DEMOD_MODULE *pDemod;
	BASE_INTERFACE_MODULE *pBaseInterface;



	// Get demod module.
	pDemod = (DTMB_DEMOD_MODULE *)pI2cBridge->pPrivateData;


	// Get base interface.
	pBaseInterface = pDemod->pBaseInterface;


	// Send I2C writing command.
	if(pBaseInterface->I2cWrite(pBaseInterface, DeviceAddr, pWritingBytes, ByteNum) != FUNCTION_SUCCESS)
		goto error_send_i2c_writing_command;


	return FUNCTION_SUCCESS;


error_send_i2c_writing_command:
	return FUNCTION_ERROR;
}





/**

@brief   Initialize RTL2836 register table.

Use rtl2836_InitRegTable() to initialize RTL2836 register table.


@param [in]   pDemod   RTL2836 demod module pointer


@note
	-# The rtl2836_InitRegTable() function will be called by BuildRtl2836Module().

*/
void
rtl2836_InitRegTable(
	DTMB_DEMOD_MODULE *pDemod
	)
{
	static const DTMB_PRIMARY_REG_ENTRY_ADDR_8BIT PrimaryRegTable[RTL2836_REG_TABLE_LEN] =
	{
		// Software reset
		// RegBitName,						PageNo,		RegStartAddr,	MSB,	LSB
		{DTMB_SOFT_RST_N,					0x0,		0x4,			0,		0	},

		// Tuner I2C forwording
		// RegBitName,						PageNo,		RegStartAddr,	MSB,	LSB
		{DTMB_I2CT_EN_CTRL,					0x0,		0x6,			0,		0	},

		// Chip ID
		// RegBitName,						PageNo,		RegStartAddr,	MSB,	LSB
		{DTMB_CHIP_ID,						0x5,		0x10,			15,		0	},

		// IF setting
		// RegBitName,						PageNo,		RegStartAddr,	MSB,	LSB
		{DTMB_EN_SP_INV,					0x1,		0x31,			1,		1	},
		{DTMB_EN_DCR,						0x1,		0x31,			0,		0	},
		{DTMB_BBIN_EN,						0x1,		0x6a,			0,		0	},
		{DTMB_IFFREQ,						0x1,		0x32,			9,		0	},

		// AGC setting
		// RegBitName,						PageNo,		RegStartAddr,	MSB,	LSB
		{DTMB_TARGET_VAL,					0x1,		0x3,			7,		0	},

		// IF setting
		// RegBitName,						PageNo,		RegStartAddr,	MSB,	LSB
		{DTMB_SERIAL,						0x4,		0x50,			7,		7	},
		{DTMB_CDIV_PH0,						0x4,		0x51,			4,		0	},
		{DTMB_CDIV_PH1,						0x4,		0x52,			4,		0	},

		// Signal lock status
		// RegBitName,						PageNo,		RegStartAddr,	MSB,	LSB
		{DTMB_TPS_LOCK,						0x8,		0x2a,			6,		6	},
		{DTMB_PN_PEAK_EXIST,				0x6,		0x53,			0,		0	},

		// FSM
		// RegBitName,						PageNo,		RegStartAddr,	MSB,	LSB
		{DTMB_FSM_STATE_R,					0x6,		0xc0,			4,		0	},

		// Performance measurement
		// RegBitName,						PageNo,		RegStartAddr,	MSB,	LSB
		{DTMB_RO_PKT_ERR_RATE,				0x9,		0x2d,			15,		0	},
		{DTMB_EST_SNR,						0x8,		0x3e,			8,		0	},

		// AGC
		// RegBitName,						PageNo,		RegStartAddr,	MSB,	LSB
		{DTMB_GAIN_OUT_R,					0x6,		0xb4,			12,		1	},
		{DTMB_RF_AGC_VAL,					0x6,		0x16,			13,		0	},
		{DTMB_IF_AGC_VAL,					0x6,		0x14,			13,		0	},

		// TR and CR
		// RegBitName,						PageNo,		RegStartAddr,	MSB,	LSB
		{DTMB_TR_OUT_R,						0x7,		0x7c,			16,		0	},
		{DTMB_SFOAQ_OUT_R,					0x7,		0x21,			13,		0	},
		{DTMB_CFO_EST_R,					0x6,		0x94,			22,		0	},

		// Signal information
		// RegBitName,						PageNo,		RegStartAddr,	MSB,	LSB
		{DTMB_EST_CARRIER,					0x8,		0x2a,			0,		0	},
		{DTMB_RX_MODE_R,					0x7,		0x17,			1,		0	},
		{DTMB_USE_TPS,						0x8,		0x2a,			5,		1	},
	};


	int i;
	int RegBitName;



	// Initialize register table according to primary register table.
	// Note: 1. Register table rows are sorted by register bit name key.
	//       2. The default value of the IsAvailable variable is "NO".
	for(i = 0; i < DTMB_REG_TABLE_LEN_MAX; i++)
		pDemod->RegTable.Addr8Bit[i].IsAvailable  = NO;

	for(i = 0; i < RTL2836_REG_TABLE_LEN; i++)
	{
		RegBitName = PrimaryRegTable[i].RegBitName;

		pDemod->RegTable.Addr8Bit[RegBitName].IsAvailable  = YES;
		pDemod->RegTable.Addr8Bit[RegBitName].PageNo       = PrimaryRegTable[i].PageNo;
		pDemod->RegTable.Addr8Bit[RegBitName].RegStartAddr = PrimaryRegTable[i].RegStartAddr;
		pDemod->RegTable.Addr8Bit[RegBitName].Msb          = PrimaryRegTable[i].Msb;
		pDemod->RegTable.Addr8Bit[RegBitName].Lsb          = PrimaryRegTable[i].Lsb;
	}


	return;
}





/**

@brief   Set I2C bridge module demod arguments.

RTL2836 builder will use rtl2836_BuildI2cBridgeModule() to set I2C bridge module demod arguments.


@param [in]   pDemod   The demod module pointer


@see   BuildRtl2836Module()

*/
void
rtl2836_BuildI2cBridgeModule(
	DTMB_DEMOD_MODULE *pDemod
	)
{
	I2C_BRIDGE_MODULE *pI2cBridge;



	// Get I2C bridge module.
	pI2cBridge = pDemod->pI2cBridge;

	// Set I2C bridge module demod arguments.
	pI2cBridge->pPrivateData = (void *)pDemod;
	pI2cBridge->ForwardI2cReadingCmd = rtl2836_ForwardI2cReadingCmd;
	pI2cBridge->ForwardI2cWritingCmd = rtl2836_ForwardI2cWritingCmd;


	return;
}





/**

@brief   Reset Function 1 variables and registers.

One can use rtl2836_func1_Reset() to reset Function 1 variables and registers.


@param [in]   pDemod   The demod module pointer


@retval   FUNCTION_SUCCESS   Reset Function 1 variables and registers successfully.
@retval   FUNCTION_ERROR     Reset Function 1 variables and registers unsuccessfully.


@note
	-# Need to execute Function 1 reset function when change tuner RF frequency or demod parameters.
	-# Function 1 update flow also employs Function 1 reset function.

*/
int
rtl2836_func1_Reset(
	DTMB_DEMOD_MODULE *pDemod
	)
{
	RTL2836_EXTRA_MODULE *pExtra;



	// Get demod extra module.
	pExtra = &(pDemod->Extra.Rtl2836);

	// Reset demod Function 1 variables.
	pExtra->Func1Cnt = 0;


	return FUNCTION_SUCCESS;
}





/**

@brief   Update demod registers with Function 1.

One can use rtl2836_func1_Update() to update demod registers with Function 1.


@param [in]   pDemod   The demod module pointer


@retval   FUNCTION_SUCCESS   Update demod registers with Function 1 successfully.
@retval   FUNCTION_ERROR     Update demod registers with Function 1 unsuccessfully.


@note
	-# Recommended update period is 50 ~ 200 ms for Function 1.
	-# Need to execute Function 1 reset function when change tuner RF frequency or demod parameters.
	-# Function 1 update flow also employs Function 1 reset function.

*/
int
rtl2836_func1_Update(
	DTMB_DEMOD_MODULE *pDemod
	)
{
	RTL2836_EXTRA_MODULE *pExtra;

	unsigned long Reg0;
	unsigned long Reg1;
	unsigned long PnPeakExist;
	unsigned long FsmStateR;
	unsigned long TpsLock;

	long SnrDbNum;
	long SnrDbDen;
	long SnrDbInt;



	// Get demod extra module.
	pExtra = &(pDemod->Extra.Rtl2836);


	// Update Function 1 counter.
	if(pDemod->RegAccess.Addr8Bit.SetRegPage(pDemod, 0x9) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

	if(pDemod->RegAccess.Addr8Bit.GetRegMaskBits(pDemod, 0x1e, 9, 0, &Reg0) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	if((Reg0 & 0x3fb) == 0)
	{
		pExtra->Func1Cnt += 1;
	}
	else
	{
		pExtra->Func1Cnt = 0;
	}


	// Get PN_PEAK_EXIST and FSM_STATE_R value.
	if(pDemod->RegAccess.Addr8Bit.SetRegPage(pDemod, 0x6) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

	if(pDemod->RegAccess.Addr8Bit.GetRegBits(pDemod, DTMB_PN_PEAK_EXIST, &PnPeakExist) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	if(pDemod->RegAccess.Addr8Bit.GetRegBits(pDemod, DTMB_FSM_STATE_R, &FsmStateR) != FUNCTION_SUCCESS)
		goto error_status_get_registers;


	// Get Reg1 and TPS_LOCK value.
	if(pDemod->RegAccess.Addr8Bit.SetRegPage(pDemod, 0x8) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

	if(pDemod->RegAccess.Addr8Bit.GetRegMaskBits(pDemod, 0x28, 3, 0, &Reg1) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	if(pDemod->RegAccess.Addr8Bit.GetRegBits(pDemod, DTMB_TPS_LOCK, &TpsLock) != FUNCTION_SUCCESS)
		goto error_status_get_registers;


	// Get SNR integer part.
	if(pDemod->GetSnrDb(pDemod, &SnrDbNum, &SnrDbDen) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	SnrDbInt = SnrDbNum / SnrDbDen;


	// Determine if reset demod by software reset.
	// Note: Need to reset Function 2 when reset demod.
	if((pExtra->Func1Cnt > pExtra->Func1CntThd) || ((PnPeakExist == 0) && (FsmStateR > 9)) ||
		((Reg1 >= 6) && (TpsLock == 0)) || (SnrDbInt == -64))
	{
		pExtra->Func1Cnt = 0;

		if(pExtra->IsFunc2Enabled == ON)
		{
			if(rtl2836_func2_Reset(pDemod) != FUNCTION_SUCCESS)
				goto error_status_execute_function;
		}

		if(pDemod->SoftwareReset(pDemod) != FUNCTION_SUCCESS)
			goto error_status_set_registers;
	}


	return FUNCTION_SUCCESS;


error_status_execute_function:
error_status_get_registers:
error_status_set_registers:
	return FUNCTION_ERROR;
}





/**

@brief   Reset Function 2 variables and registers.

One can use rtl2836_func2_Reset() to reset Function 1 variables and registers.


@param [in]   pDemod   The demod module pointer


@retval   FUNCTION_SUCCESS   Reset Function 2 variables and registers successfully.
@retval   FUNCTION_ERROR     Reset Function 2 variables and registers unsuccessfully.


@note
	-# Need to execute Function 2 reset function when change tuner RF frequency or demod parameters.
	-# Function 2 update flow also employs Function 2 reset function.

*/
int
rtl2836_func2_Reset(
	DTMB_DEMOD_MODULE *pDemod
	)
{
	RTL2836_EXTRA_MODULE *pExtra;



	// Get demod extra module.
	pExtra = &(pDemod->Extra.Rtl2836);

	// Reset demod Function 2 variables and registers to signal normal mode.
	pExtra->Func2SignalModePrevious = RTL2836_FUNC2_SIGNAL_NORMAL;

	if(pDemod->RegAccess.Addr8Bit.SetRegPage(pDemod, 0x2) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

	if(pDemod->RegAccess.Addr8Bit.SetRegMaskBits(pDemod, 0x15, 7, 0, 0xf) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

	if(pDemod->RegAccess.Addr8Bit.SetRegMaskBits(pDemod, 0x1e, 6, 0, 0x3a) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

	if(pDemod->RegAccess.Addr8Bit.SetRegMaskBits(pDemod, 0x1f, 5, 0, 0x19) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

	if(pDemod->RegAccess.Addr8Bit.SetRegMaskBits(pDemod, 0x23, 4, 0, 0x1e) != FUNCTION_SUCCESS)
		goto error_status_set_registers;


	return FUNCTION_SUCCESS;


error_status_set_registers:
	return FUNCTION_ERROR;
}





/**

@brief   Update demod registers with Function 2.

One can use rtl2836_func2_Update() to update demod registers with Function 2.


@param [in]   pDemod   The demod module pointer


@retval   FUNCTION_SUCCESS   Update demod registers with Function 2 successfully.
@retval   FUNCTION_ERROR     Update demod registers with Function 2 unsuccessfully.


@note
	-# Recommended update period is 50 ~ 200 ms for Function 2.
	-# Need to execute Function 2 reset function when change tuner RF frequency or demod parameters.
	-# Function 2 update flow also employs Function 2 reset function.

*/
int
rtl2836_func2_Update(
	DTMB_DEMOD_MODULE *pDemod
	)
{
	BASE_INTERFACE_MODULE *pBaseInterface;
	RTL2836_EXTRA_MODULE *pExtra;

	int i;

	unsigned long TpsLock;
	unsigned long PnPeakExist;

	int PnMode;
	int QamMode;
	int CodeRateMode;

	int SignalLockStatus;

	int SignalMode;



	// Get base interface and demod extra module.
	pBaseInterface = pDemod->pBaseInterface;
	pExtra = &(pDemod->Extra.Rtl2836);


	// Get TPS_LOCK value.
	if(pDemod->RegAccess.Addr8Bit.GetRegBitsWithPage(pDemod, DTMB_TPS_LOCK, &TpsLock) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	// Get PN_PEAK_EXIST value.
	if(pDemod->RegAccess.Addr8Bit.GetRegBitsWithPage(pDemod, DTMB_PN_PEAK_EXIST, &PnPeakExist) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	// Get PN mode.
	if(pDemod->GetPnMode(pDemod, &PnMode) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Get QAM mode.
	if(pDemod->GetQamMode(pDemod, &QamMode) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Get code rate mode.
	if(pDemod->GetCodeRateMode(pDemod, &CodeRateMode) != FUNCTION_SUCCESS)
		goto error_status_execute_function;


	// If TPS is not locked or PN peak doesn't exist, do nothing.
	if((TpsLock != 0x1) || (PnPeakExist != 0x1))
		goto success_status_tps_is_not_locked;

	// Determine signal mode.
	if((PnMode == DTMB_PN_945) && (QamMode == DTMB_QAM_64QAM) && (CodeRateMode == DTMB_CODE_RATE_0P6))
	{
		SignalMode = RTL2836_FUNC2_SIGNAL_PARTICULAR;
	}
	else
	{
		SignalMode = RTL2836_FUNC2_SIGNAL_NORMAL;
	}

	// If signal mode is the same as previous one, do nothing.
	if(SignalMode == pExtra->Func2SignalModePrevious)
		goto success_status_signal_mode_is_the_same;


	// Set demod registers according to signal mode
	switch(SignalMode)
	{
		default:
		case RTL2836_FUNC2_SIGNAL_NORMAL:

			if(pDemod->RegAccess.Addr8Bit.SetRegPage(pDemod, 0x2) != FUNCTION_SUCCESS)
				goto error_status_set_registers;

			if(pDemod->RegAccess.Addr8Bit.SetRegMaskBits(pDemod, 0x15, 7, 0, 0xf) != FUNCTION_SUCCESS)
				goto error_status_set_registers;

			if(pDemod->RegAccess.Addr8Bit.SetRegMaskBits(pDemod, 0x1e, 6, 0, 0x3a) != FUNCTION_SUCCESS)
				goto error_status_set_registers;

			if(pDemod->RegAccess.Addr8Bit.SetRegMaskBits(pDemod, 0x1f, 5, 0, 0x19) != FUNCTION_SUCCESS)
				goto error_status_set_registers;

			if(pDemod->RegAccess.Addr8Bit.SetRegMaskBits(pDemod, 0x23, 4, 0, 0x1e) != FUNCTION_SUCCESS)
				goto error_status_set_registers;

			break;


		case RTL2836_FUNC2_SIGNAL_PARTICULAR:

			if(pDemod->RegAccess.Addr8Bit.SetRegPage(pDemod, 0x2) != FUNCTION_SUCCESS)
				goto error_status_set_registers;

			if(pDemod->RegAccess.Addr8Bit.SetRegMaskBits(pDemod, 0x15, 7, 0, 0x4) != FUNCTION_SUCCESS)
				goto error_status_set_registers;

			if(pDemod->RegAccess.Addr8Bit.SetRegMaskBits(pDemod, 0x1e, 6, 0, 0xa) != FUNCTION_SUCCESS)
				goto error_status_set_registers;

			if(pDemod->RegAccess.Addr8Bit.SetRegMaskBits(pDemod, 0x1f, 5, 0, 0x3f) != FUNCTION_SUCCESS)
				goto error_status_set_registers;

			if(pDemod->RegAccess.Addr8Bit.SetRegMaskBits(pDemod, 0x23, 4, 0, 0x1f) != FUNCTION_SUCCESS)
				goto error_status_set_registers;

			break;
	}


	// Reset demod by software reset.
	if(pDemod->SoftwareReset(pDemod) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

	// Wait 1000 ms for signal lock check.
	for(i = 0; i < 10; i++)
	{
		// Wait 100 ms.
		pBaseInterface->WaitMs(pBaseInterface, 100);

		// Check signal lock status on demod.
		// Note: If signal is locked, stop signal lock check.
		if(pDemod->IsSignalLocked(pDemod, &SignalLockStatus) != FUNCTION_SUCCESS)
			goto error_status_execute_function;

		if(SignalLockStatus == YES)
			break;
	}


	// Update previous signal mode.
	pExtra->Func2SignalModePrevious = SignalMode;


success_status_signal_mode_is_the_same:
success_status_tps_is_not_locked:
	return FUNCTION_SUCCESS;


error_status_set_registers:
error_status_execute_function:
error_status_get_registers:
	return FUNCTION_ERROR;
}














