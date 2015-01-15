/**

@file

@brief   RTL2832 demod module definition

One can manipulate RTL2832 demod through RTL2832 module.
RTL2832 module is derived from DVB-T demod module.

*/


#include "demod_rtl2832.h"





/**

@brief   RTL2832 demod module builder

Use BuildRtl2832Module() to build RTL2832 module, set all module function pointers with the corresponding
functions, and initialize module private variables.


@param [in]   ppDemod                      Pointer to RTL2832 demod module pointer
@param [in]   pDvbtDemodModuleMemory       Pointer to an allocated DVB-T demod module memory
@param [in]   pBaseInterfaceModuleMemory   Pointer to an allocated base interface module memory
@param [in]   pI2cBridgeModuleMemory       Pointer to an allocated I2C bridge module memory
@param [in]   DeviceAddr                   RTL2832 I2C device address
@param [in]   CrystalFreqHz                RTL2832 crystal frequency in Hz
@param [in]   TsInterfaceMode              RTL2832 TS interface mode for setting
@param [in]   AppMode                      RTL2832 application mode for setting
@param [in]   UpdateFuncRefPeriodMs        RTL2832 update function reference period in millisecond for setting
@param [in]   IsFunc1Enabled               RTL2832 Function 1 enabling status for setting


@note
	-# One should call BuildRtl2832Module() to build RTL2832 module before using it.

*/
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
	)
{
	DVBT_DEMOD_MODULE *pDemod;
	RTL2832_EXTRA_MODULE *pExtra;



	// Set demod module pointer, 
	*ppDemod = pDvbtDemodModuleMemory;

	// Get demod module.
	pDemod = *ppDemod;

	// Set base interface module pointer and I2C bridge module pointer.
	pDemod->pBaseInterface = pBaseInterfaceModuleMemory;
	pDemod->pI2cBridge     = pI2cBridgeModuleMemory;

	// Get demod extra module.
	pExtra = &(pDemod->Extra.Rtl2832);


	// Set demod type.
	pDemod->DemodType = DVBT_DEMOD_TYPE_RTL2832;

	// Set demod I2C device address.
	pDemod->DeviceAddr = DeviceAddr;

	// Set demod crystal frequency in Hz.
	pDemod->CrystalFreqHz = CrystalFreqHz;

	// Set demod TS interface mode.
	pDemod->TsInterfaceMode = TsInterfaceMode;


	// Initialize demod parameter setting status
	pDemod->IsBandwidthModeSet = NO;
	pDemod->IsIfFreqHzSet      = NO;
	pDemod->IsSpectrumModeSet  = NO;


	// Initialize demod register table.
	rtl2832_InitRegTable(pDemod);

	
	// Build I2C birdge module.
	rtl2832_BuildI2cBridgeModule(pDemod);


	// Set demod module I2C function pointers with default functions.
	pDemod->SetRegPage         = dvbt_demod_default_SetRegPage;
	pDemod->SetRegBytes        = dvbt_demod_default_SetRegBytes;
	pDemod->GetRegBytes        = dvbt_demod_default_GetRegBytes;
	pDemod->SetRegMaskBits     = dvbt_demod_default_SetRegMaskBits;
	pDemod->GetRegMaskBits     = dvbt_demod_default_GetRegMaskBits;
	pDemod->SetRegBits         = dvbt_demod_default_SetRegBits;
	pDemod->GetRegBits         = dvbt_demod_default_GetRegBits;
	pDemod->SetRegBitsWithPage = dvbt_demod_default_SetRegBitsWithPage;
	pDemod->GetRegBitsWithPage = dvbt_demod_default_GetRegBitsWithPage;


	// Set demod module manipulating function pointers with default functions.
	pDemod->GetDemodType     = dvbt_demod_default_GetDemodType;
	pDemod->GetDeviceAddr    = dvbt_demod_default_GetDeviceAddr;
	pDemod->GetCrystalFreqHz = dvbt_demod_default_GetCrystalFreqHz;

	pDemod->GetBandwidthMode = dvbt_demod_default_GetBandwidthMode;
	pDemod->GetIfFreqHz      = dvbt_demod_default_GetIfFreqHz;
	pDemod->GetSpectrumMode  = dvbt_demod_default_GetSpectrumMode;


	// Set demod module manipulating function pointers with particular functions.
	pDemod->IsConnectedToI2c  = rtl2832_IsConnectedToI2c;
	pDemod->SoftwareReset     = rtl2832_SoftwareReset;
	pDemod->Initialize        = rtl2832_Initialize;
	pDemod->SetBandwidthMode  = rtl2832_SetBandwidthMode;
	pDemod->SetIfFreqHz       = rtl2832_SetIfFreqHz;
	pDemod->SetSpectrumMode   = rtl2832_SetSpectrumMode;

	pDemod->IsTpsLocked       = rtl2832_IsTpsLocked;
	pDemod->IsSignalLocked    = rtl2832_IsSignalLocked;

	pDemod->GetSignalStrength = rtl2832_GetSignalStrength;
	pDemod->GetSignalQuality  = rtl2832_GetSignalQuality;

	pDemod->GetBer            = rtl2832_GetBer;
	pDemod->GetSnrDb          = rtl2832_GetSnrDb;

	pDemod->GetRfAgc          = rtl2832_GetRfAgc;
	pDemod->GetIfAgc          = rtl2832_GetIfAgc;
	pDemod->GetDiAgc          = rtl2832_GetDiAgc;

	pDemod->GetTrOffsetPpm    = rtl2832_GetTrOffsetPpm;
	pDemod->GetCrOffsetHz     = rtl2832_GetCrOffsetHz;

	pDemod->GetConstellation  = rtl2832_GetConstellation;
	pDemod->GetHierarchy      = rtl2832_GetHierarchy;
	pDemod->GetCodeRateLp     = rtl2832_GetCodeRateLp;
	pDemod->GetCodeRateHp     = rtl2832_GetCodeRateHp;
	pDemod->GetGuardInterval  = rtl2832_GetGuardInterval;
	pDemod->GetFftMode        = rtl2832_GetFftMode;

	pDemod->UpdateFunction    = rtl2832_UpdateFunction;
	pDemod->ResetFunction     = rtl2832_ResetFunction;


	// Initialize demod extra module variables.
	pExtra->AppMode = AppMode;


	// Initialize demod Function 1 variables.
	pExtra->Func1State = RTL2832_FUNC1_STATE_NORMAL;

	pExtra->IsFunc1Enabled = IsFunc1Enabled;

	pExtra->Func1WaitTimeMax        = DivideWithCeiling(RTL2832_FUNC1_WAIT_TIME_MS, UpdateFuncRefPeriodMs);
	pExtra->Func1GettingTimeMax     = DivideWithCeiling(RTL2832_FUNC1_GETTING_TIME_MS, UpdateFuncRefPeriodMs);
	pExtra->Func1GettingNumEachTime = DivideWithCeiling(RTL2832_FUNC1_GETTING_NUM_MIN, pExtra->Func1GettingTimeMax + 1);

	pExtra->Func1QamBak  = 0xff;
	pExtra->Func1HierBak = 0xff;
	pExtra->Func1LpCrBak = 0xff;
	pExtra->Func1HpCrBak = 0xff;
	pExtra->Func1GiBak   = 0xff;
	pExtra->Func1FftBak  = 0xff;


	// Set demod extra module function pointers.
	pExtra->GetAppMode = rtl2832_GetAppMode;


	return;
}





/**

@see   DVBT_DEMOD_FP_IS_CONNECTED_TO_I2C

*/
void
rtl2832_IsConnectedToI2c(
	DVBT_DEMOD_MODULE *pDemod,
	int *pAnswer
	)
{
	BASE_INTERFACE_MODULE *pBaseInterface;

	unsigned char Nothing;



	// Get base interface.
	pBaseInterface = pDemod->pBaseInterface;


	// Send read command.
	// Note: The number of reading bytes must be greater than 0.
	if(pBaseInterface->I2cRead(pBaseInterface, pDemod->DeviceAddr, &Nothing, LEN_1_BYTE) == FUNCTION_ERROR)
		goto error_status_i2c_read;


	// Set I2cConnectionStatus with YES.
	*pAnswer = YES;


	return;


error_status_i2c_read:

	// Set I2cConnectionStatus with NO.
	*pAnswer = NO;


	return;
}





/**

@see   DVBT_DEMOD_FP_SOFTWARE_RESET

*/
int
rtl2832_SoftwareReset(
	DVBT_DEMOD_MODULE *pDemod
	)
{
	// Set SOFT_RST with 1. Then, set SOFT_RST with 0.
	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_SOFT_RST, 0x1) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_SOFT_RST, 0x0) != FUNCTION_SUCCESS)
		goto error_status_set_registers;


	return FUNCTION_SUCCESS;


error_status_set_registers:
	return FUNCTION_ERROR;
}





/**

@see   DVBT_DEMOD_FP_INITIALIZE

*/
int
rtl2832_Initialize(
	DVBT_DEMOD_MODULE *pDemod
	)
{
	// Initializing table entry only used in Initialize()
	typedef struct
	{
		int RegBitName;
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

	// Application initializing table entry only used in Initialize()
	typedef struct
	{
		int RegBitName;
		unsigned long WritingValue[RTL2832_APPLICATION_MODE_NUM];
	}
	APP_INIT_TABLE_ENTRY;



	static const INIT_TABLE_ENTRY InitTable[RTL2832_INIT_TABLE_LEN] =
	{
		// RegBitName,				WritingValue
		{DVBT_AD_EN_REG,			0x1		},
		{DVBT_AD_EN_REG1,			0x1		},
		{DVBT_RSD_BER_FAIL_VAL,		0x2800	},
		{DVBT_MGD_THD0,				0x10	},
		{DVBT_MGD_THD1,				0x20	},
		{DVBT_MGD_THD2,				0x20	},
		{DVBT_MGD_THD3,				0x40	},
		{DVBT_MGD_THD4,				0x22	},
		{DVBT_MGD_THD5,				0x32	},
		{DVBT_MGD_THD6,				0x37	},
		{DVBT_MGD_THD7,				0x39	},
		{DVBT_EN_BK_TRK,			0x0		},
		{DVBT_EN_CACQ_NOTCH,		0x0		},
		{DVBT_AD_AV_REF,			0x2a	},
		{DVBT_REG_PI,				0x6		},
		{DVBT_PIP_ON,				0x0		},
		{DVBT_CDIV_PH0,				0x8		},
		{DVBT_CDIV_PH1,				0x8		},
		{DVBT_SCALE1_B92,			0x4		},
		{DVBT_SCALE1_B93,			0xb0	},
		{DVBT_SCALE1_BA7,			0x78	},
		{DVBT_SCALE1_BA9,			0x28	},
		{DVBT_SCALE1_BAA,			0x59	},
		{DVBT_SCALE1_BAB,			0x83	},
		{DVBT_SCALE1_BAC,			0xd4	},
		{DVBT_SCALE1_BB0,			0x65	},
		{DVBT_SCALE1_BB1,			0x43	},
		{DVBT_KB_P1,				0x1		},
		{DVBT_KB_P2,				0x4		},
		{DVBT_KB_P3,				0x7		},
		{DVBT_K1_CR_STEP12,			0xa		},
		{DVBT_REG_GPE,				0x1		},
	};

	static const TS_INTERFACE_INIT_TABLE_ENTRY TsInterfaceInitTable[RTL2832_TS_INTERFACE_INIT_TABLE_LEN] =
	{
		// RegBitName,				WritingValue for {Parallel, Serial}
		{DVBT_SERIAL,				{0x0,	0x1}},
		{DVBT_CDIV_PH0,				{0x9,	0x1}},
		{DVBT_CDIV_PH1,				{0x9,	0x2}},
		{DVBT_MPEG_IO_OPT_2_2,		{0x0,	0x0}},
		{DVBT_MPEG_IO_OPT_1_0,		{0x0,	0x1}},
	};

	static const APP_INIT_TABLE_ENTRY AppInitTable[RTL2832_APP_INIT_TABLE_LEN] =
	{
		// RegBitName,				WritingValue for {Dongle, STB}
		{DVBT_TRK_KS_P2,			{0x4,	0x4}},
		{DVBT_TRK_KS_I2,			{0x7,	0x7}},
		{DVBT_TR_THD_SET2,			{0x6,	0x6}},
		{DVBT_TRK_KC_I2,			{0x5,	0x6}},
		{DVBT_CR_THD_SET2,			{0x1,	0x1}},
	};


	BASE_INTERFACE_MODULE *pBaseInterface;
	RTL2832_EXTRA_MODULE *pExtra;

	int i;

	int TsInterfaceMode;
	int AppMode;



	// Get base interface and demod extra module.
	pBaseInterface = pDemod->pBaseInterface;
	pExtra = &(pDemod->Extra.Rtl2832);

	// Get TS interface mode.
	TsInterfaceMode = pDemod->TsInterfaceMode;

	// Get application mode.
	pExtra->GetAppMode(pDemod, &AppMode);


	// Initialize demod registers according to the initializing table.
	for(i = 0; i < RTL2832_INIT_TABLE_LEN; i++)
	{
		if(pDemod->SetRegBitsWithPage(pDemod, InitTable[i].RegBitName, InitTable[i].WritingValue)
			!= FUNCTION_SUCCESS)
			goto error_status_set_registers;
	}


	// Initialize demod registers according to the TS interface initializing table.
	for(i = 0; i < RTL2832_TS_INTERFACE_INIT_TABLE_LEN; i++)
	{
		if(pDemod->SetRegBitsWithPage(pDemod, TsInterfaceInitTable[i].RegBitName,
			TsInterfaceInitTable[i].WritingValue[TsInterfaceMode]) != FUNCTION_SUCCESS)
			goto error_status_set_registers;
	}


	// Initialize demod registers according to the application initializing table.
	for(i = 0; i < RTL2832_APP_INIT_TABLE_LEN; i++)
	{
		if(pDemod->SetRegBitsWithPage(pDemod, AppInitTable[i].RegBitName,
			AppInitTable[i].WritingValue[AppMode]) != FUNCTION_SUCCESS)
			goto error_status_set_registers;
	}


	return FUNCTION_SUCCESS;


error_status_set_registers:
	return FUNCTION_ERROR;
}





/**

@see   DVBT_DEMOD_FP_SET_BANDWIDTH_MODE

*/
int
rtl2832_SetBandwidthMode(
	DVBT_DEMOD_MODULE *pDemod,
	int BandwidthMode
	)
{
	static const unsigned char HlpfxTable[DVBT_BANDWIDTH_MODE_NUM][RTL2832_H_LPF_X_LEN] =
	{
		// H_LPF_X writing value for 6 MHz bandwidth
		{
			0xf5,	0xff,	0x15,	0x38,	0x5d,	0x6d,	0x52,	0x07,	0xfa,	0x2f,
			0x53,	0xf5,	0x3f,	0xca,	0x0b,	0x91,	0xea,	0x30,	0x63,	0xb2,
			0x13,	0xda,	0x0b,	0xc4,	0x18,	0x7e,	0x16,	0x66,	0x08,	0x67,
			0x19,	0xe0,
		},

		// H_LPF_X writing value for 7 MHz bandwidth
		{
			0xe7,	0xcc,	0xb5,	0xba,	0xe8,	0x2f,	0x67,	0x61,	0x00,	0xaf,
			0x86,	0xf2,	0xbf,	0x59,	0x04,	0x11,	0xb6,	0x33,	0xa4,	0x30,
			0x15,	0x10,	0x0a,	0x42,	0x18,	0xf8,	0x17,	0xd9,	0x07,	0x22,
			0x19,	0x10,
		},

		// H_LPF_X writing value for 8 MHz bandwidth
		{
			0x09,	0xf6,	0xd2,	0xa7,	0x9a,	0xc9,	0x27,	0x77,	0x06,	0xbf,
			0xec,	0xf4,	0x4f,	0x0b,	0xfc,	0x01,	0x63,	0x35,	0x54,	0xa7,
			0x16,	0x66,	0x08,	0xb4,	0x19,	0x6e,	0x19,	0x65,	0x05,	0xc8,
			0x19,	0xe0,
		},
	};


	unsigned long CrystalFreqHz;

	long ConstWithBandwidthMode;

	MPI MpiCrystalFreqHz;
	MPI MpiConst, MpiVar0, MpiVar1, MpiNone;

	unsigned long RsampRatio;

	long CfreqOffRatioInt;
	unsigned long CfreqOffRatioBinary;



	// Get demod crystal frequency in Hz.
	pDemod->GetCrystalFreqHz(pDemod, &CrystalFreqHz);


	// Set H_LPF_X registers with HlpfxTable according to BandwidthMode.
	if(pDemod->SetRegPage(pDemod, RTL2832_H_LPF_X_PAGE) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

	if(pDemod->SetRegBytes(pDemod, RTL2832_H_LPF_X_ADDR, HlpfxTable[BandwidthMode], RTL2832_H_LPF_X_LEN) !=
		FUNCTION_SUCCESS)
		goto error_status_set_registers;


	// Determine constant value with bandwidth mode.
	switch(BandwidthMode)
	{
		default:
		case DVBT_BANDWIDTH_6MHZ:	ConstWithBandwidthMode = 48000000;		break;
		case DVBT_BANDWIDTH_7MHZ:	ConstWithBandwidthMode = 56000000;		break;
		case DVBT_BANDWIDTH_8MHZ:	ConstWithBandwidthMode = 64000000;		break;
	}


	// Calculate RSAMP_RATIO value.
	// Note: RSAMP_RATIO = floor(CrystalFreqHz * 7 * pow(2, 22) / ConstWithBandwidthMode)
	MpiSetValue(&MpiCrystalFreqHz, CrystalFreqHz);
	MpiSetValue(&MpiVar1,          ConstWithBandwidthMode);
	MpiSetValue(&MpiConst,         7);

	MpiMul(&MpiVar0, MpiCrystalFreqHz, MpiConst);
	MpiLeftShift(&MpiVar0, MpiVar0, 22);
	MpiDiv(&MpiVar0, &MpiNone, MpiVar0, MpiVar1);

	MpiGetValue(MpiVar0, (long *)&RsampRatio);


	// Set RSAMP_RATIO with calculated value.
	// Note: Use SetRegBitsWithPage() to set register bits with page setting.
	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_RSAMP_RATIO, RsampRatio) != FUNCTION_SUCCESS)
		goto error_status_set_registers;


	// Calculate CFREQ_OFF_RATIO value.
	// Note: CFREQ_OFF_RATIO = - floor(ConstWithBandwidthMode * pow(2, 20) / (CrystalFreqHz * 7))
	MpiSetValue(&MpiCrystalFreqHz, CrystalFreqHz);
	MpiSetValue(&MpiVar0,          ConstWithBandwidthMode);
	MpiSetValue(&MpiConst,         7);

	MpiLeftShift(&MpiVar0, MpiVar0, 20);
	MpiMul(&MpiVar1, MpiCrystalFreqHz, MpiConst);
	MpiDiv(&MpiVar0, &MpiNone, MpiVar0, MpiVar1);

	MpiGetValue(MpiVar0, &CfreqOffRatioInt);
	CfreqOffRatioInt = - CfreqOffRatioInt;

	CfreqOffRatioBinary = SignedIntToBin(CfreqOffRatioInt, RTL2832_CFREQ_OFF_RATIO_BIT_NUM);


	// Set CFREQ_OFF_RATIO with calculated value.
	// Note: Use SetRegBitsWithPage() to set register bits with page setting.
	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_CFREQ_OFF_RATIO, CfreqOffRatioBinary) != FUNCTION_SUCCESS)
		goto error_status_set_registers;



	// Set demod bandwidth mode parameter.
	pDemod->BandwidthMode      = BandwidthMode;
	pDemod->IsBandwidthModeSet = YES;


	return FUNCTION_SUCCESS;


error_status_set_registers:
	return FUNCTION_ERROR;
}





/**

@see   DVBT_DEMOD_FP_SET_IF_FREQ_HZ

*/
int
rtl2832_SetIfFreqHz(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned long IfFreqHz
	)
{
	unsigned long CrystalFreqHz;

	unsigned long EnBbin;

	MPI MpiCrystalFreqHz, MpiVar, MpiNone;

	long PsetIffreqInt;
	unsigned long PsetIffreqBinary;



	// Get demod crystal frequency in Hz.
	pDemod->GetCrystalFreqHz(pDemod, &CrystalFreqHz);


	// Determine and set EN_BBIN value.
	EnBbin = (IfFreqHz == IF_FREQ_0HZ) ? 0x1 : 0x0;

	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_EN_BBIN, EnBbin) != FUNCTION_SUCCESS)
		goto error_status_set_registers;


	// Calculate PSET_IFFREQ value.
	// Note: PSET_IFFREQ = - floor((IfFreqHz % CrystalFreqHz) * pow(2, 22) / CrystalFreqHz)
	MpiSetValue(&MpiCrystalFreqHz, CrystalFreqHz);

	MpiSetValue(&MpiVar, (IfFreqHz % CrystalFreqHz));
	MpiLeftShift(&MpiVar, MpiVar, RTL2832_PSET_IFFREQ_BIT_NUM);
	MpiDiv(&MpiVar, &MpiNone, MpiVar, MpiCrystalFreqHz);

	MpiGetValue(MpiVar, &PsetIffreqInt);
	PsetIffreqInt = - PsetIffreqInt;

	PsetIffreqBinary = SignedIntToBin(PsetIffreqInt, RTL2832_PSET_IFFREQ_BIT_NUM);


	// Set PSET_IFFREQ with calculated value.
	// Note: Use SetRegBitsWithPage() to set register bits with page setting.
	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_PSET_IFFREQ, PsetIffreqBinary) != FUNCTION_SUCCESS)
		goto error_status_set_registers;


	// Set demod IF frequnecy parameter.
	pDemod->IfFreqHz      = IfFreqHz;
	pDemod->IsIfFreqHzSet = YES;


	return FUNCTION_SUCCESS;


error_status_set_registers:
	return FUNCTION_ERROR;
}





/**

@see   DVBT_DEMOD_FP_SET_SPECTRUM_MODE

*/
int
rtl2832_SetSpectrumMode(
	DVBT_DEMOD_MODULE *pDemod,
	int SpectrumMode
	)
{
	unsigned long SpecInv;



	// Determine SpecInv according to spectrum mode.
	switch(SpectrumMode)
	{
		default:
		case SPECTRUM_NORMAL:		SpecInv = 0;		break;
		case SPECTRUM_INVERSE:		SpecInv = 1;		break;
	}


	// Set SPEC_INV with SpecInv.
	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_SPEC_INV, SpecInv) != FUNCTION_SUCCESS)
		goto error_status_set_registers;


	// Set demod spectrum mode parameter.
	pDemod->SpectrumMode      = SpectrumMode;
	pDemod->IsSpectrumModeSet = YES;


	return FUNCTION_SUCCESS;


error_status_set_registers:
	return FUNCTION_ERROR;
}





/**

@see   DVBT_DEMOD_FP_IS_TPS_LOCKED

*/
int
rtl2832_IsTpsLocked(
	DVBT_DEMOD_MODULE *pDemod,
	int *pAnswer
	)
{
	unsigned long FsmStage;



	// Get FSM stage from FSM_STAGE.
	if(pDemod->GetRegBitsWithPage(pDemod, DVBT_FSM_STAGE, &FsmStage) != FUNCTION_SUCCESS)
		goto error_status_get_registers;


	// Determine answer according to FSM stage.
	if(FsmStage > 9)
		*pAnswer = YES;
	else
		*pAnswer = NO;


	return FUNCTION_SUCCESS;


error_status_get_registers:
	return FUNCTION_ERROR;
}





/**

@see   DVBT_DEMOD_FP_IS_SIGNAL_LOCKED

*/
int
rtl2832_IsSignalLocked(
	DVBT_DEMOD_MODULE *pDemod,
	int *pAnswer
	)
{
	unsigned long FsmStage;



	// Get FSM stage from FSM_STAGE.
	if(pDemod->GetRegBitsWithPage(pDemod, DVBT_FSM_STAGE, &FsmStage) != FUNCTION_SUCCESS)
		goto error_status_get_registers;


	// Determine answer according to FSM stage.
	if(FsmStage == 11)
		*pAnswer = YES;
	else
		*pAnswer = NO;


	return FUNCTION_SUCCESS;


error_status_get_registers:
	return FUNCTION_ERROR;
}





/**

@see   DVBT_DEMOD_FP_GET_SIGNAL_STRENGTH

*/
int
rtl2832_GetSignalStrength(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned long *pSignalStrength
	)
{
	unsigned long FsmStage;
	long IfAgc;



	// Get FSM stage and IF AGC value.
	if(pDemod->GetRegBitsWithPage(pDemod, DVBT_FSM_STAGE, &FsmStage) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	if(pDemod->GetIfAgc(pDemod, &IfAgc) != FUNCTION_SUCCESS)
		goto error_status_get_registers;


	//  Determine signal strength according to FSM stage and IF AGC value.
	if(FsmStage < 10)
		*pSignalStrength = 0;
	else
		*pSignalStrength = 55 - IfAgc / 182;


	return FUNCTION_SUCCESS;


error_status_get_registers:
	return FUNCTION_ERROR;
}





/**

@see   DVBT_DEMOD_FP_GET_SIGNAL_QUALITY

*/
int
rtl2832_GetSignalQuality(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned long *pSignalQuality
	)
{
	unsigned long FsmStage, RsdBerEst;

	MPI MpiVar;
	long Var;



	// Get FSM_STAGE and RSD_BER_EST.
	if(pDemod->GetRegBitsWithPage(pDemod, DVBT_FSM_STAGE, &FsmStage) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	if(pDemod->GetRegBitsWithPage(pDemod, DVBT_RSD_BER_EST, &RsdBerEst) != FUNCTION_SUCCESS)
		goto error_status_get_registers;


	// If demod is not signal-locked, set signal quality with zero.
	if(FsmStage < 10)
	{
		*pSignalQuality = 0;
		goto success_status_non_signal_lock;
	}

	// Determine signal quality according to RSD_BER_EST.
	// Note: Map RSD_BER_EST value 8192 ~ 128 to 10 ~ 100
	//       Original formula: SignalQuality = 205 - 15 * log2(RSD_BER_EST)
	//       Adjusted formula: SignalQuality = ((205 << 5) - 15 * (log2(RSD_BER_EST) << 5)) >> 5
	//       If RSD_BER_EST > 8192, signal quality is 10.
	//       If RSD_BER_EST < 128, signal quality is 100.
	if(RsdBerEst > 8192)
	{
		*pSignalQuality = 10;
	}
	else if(RsdBerEst < 128)
	{
		*pSignalQuality = 100;
	}
	else
	{
		MpiSetValue(&MpiVar, RsdBerEst);
		MpiLog2(&MpiVar, MpiVar, RTL2832_SQ_FRAC_BIT_NUM);
		MpiGetValue(MpiVar, &Var);

		*pSignalQuality = ((205 << RTL2832_SQ_FRAC_BIT_NUM) - 15 * Var) >> RTL2832_SQ_FRAC_BIT_NUM;
	}


success_status_non_signal_lock:
	return FUNCTION_SUCCESS;


error_status_get_registers:
	return FUNCTION_ERROR;
}





/**

@see   DVBT_DEMOD_FP_GET_BER

*/
int
rtl2832_GetBer(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned long *pBerNum,
	unsigned long *pBerDen
	)
{
	unsigned long RsdBerEst;



	// Get RSD_BER_EST.
	if(pDemod->GetRegBitsWithPage(pDemod, DVBT_RSD_BER_EST, &RsdBerEst) != FUNCTION_SUCCESS)
		goto error_status_get_registers;


	// Set BER numerator according to RSD_BER_EST.
	*pBerNum = RsdBerEst;

	// Set BER denominator.
	*pBerDen = RTL2832_BER_DEN_VALUE;


	return FUNCTION_SUCCESS;


error_status_get_registers:
	return FUNCTION_ERROR;
}





/**

@see   DVBT_DEMOD_FP_GET_SNR_DB

*/
int
rtl2832_GetSnrDb(
	DVBT_DEMOD_MODULE *pDemod,
	long *pSnrDbNum,
	long *pSnrDbDen
	)
{
	unsigned long FsmStage;
	unsigned long CeEstEvm;
	int Constellation, Hierarchy;

	static const long SnrDbNumConst[DVBT_CONSTELLATION_NUM][DVBT_HIERARCHY_NUM] =
	{
		{122880,	122880,		122880,		122880,		},
		{146657,	146657,		156897,		171013,		},
		{167857,	167857,		173127,		181810,		},
	};

	long Var;
	MPI MpiCeEstEvm, MpiVar;



	// Get FSM stage, CE_EST_EVM, constellation, and hierarchy.
	if(pDemod->GetRegBitsWithPage(pDemod, DVBT_FSM_STAGE, &FsmStage) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	if(pDemod->GetRegBitsWithPage(pDemod, DVBT_CE_EST_EVM, &CeEstEvm) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	if(pDemod->GetConstellation(pDemod, &Constellation) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	if(pDemod->GetHierarchy(pDemod, &Hierarchy) != FUNCTION_SUCCESS)
		goto error_status_get_registers;



	// SNR dB formula
	// Original formula: SNR_dB = 10 * log10(Norm * pow(2, 11) / CeEstEvm)
	// Adjusted formula: SNR_dB = (SNR_DB_NUM_CONST - 10 * log2(CeEstEvm) * pow(2, SNR_FRAC_BIT_NUM)) / SNR_DB_DEN
	//                   SNR_DB_NUM_CONST = 10 * log2(Norm * pow(2, 11)) * pow(2, SNR_FRAC_BIT_NUM)
	//                   SNR_DB_DEN       = log2(10) * pow(2, SNR_FRAC_BIT_NUM)
	// Norm:
	//	                 None      Alpha=1   Alpha=2   Alpha=4
	//        4-QAM      2         2         2         2
	//       16-QAM      10        10        20        52
	//       64-QAM      42        42        60        108


	// If FSM stage < 10, set CE_EST_EVM with max value.
	if(FsmStage < 10)
		CeEstEvm = RTL2832_CE_EST_EVM_MAX_VALUE;


	// Calculate SNR dB numerator.
	MpiSetValue(&MpiCeEstEvm, CeEstEvm);

	MpiLog2(&MpiVar, MpiCeEstEvm, RTL2832_SNR_FRAC_BIT_NUM);

	MpiGetValue(MpiVar, &Var);

	*pSnrDbNum = SnrDbNumConst[Constellation][Hierarchy] - 10 * Var;

	
	// Set SNR dB denominator.
	*pSnrDbDen = RTL2832_SNR_DB_DEN;


	return FUNCTION_SUCCESS;


error_status_get_registers:
	return FUNCTION_ERROR;
}





/**

@see   DVBT_DEMOD_FP_GET_RF_AGC

*/
int
rtl2832_GetRfAgc(
	DVBT_DEMOD_MODULE *pDemod,
	long *pRfAgc
	)
{
	unsigned long Value;



	// Get RF_AGC_VAL to Value.
	if(pDemod->GetRegBitsWithPage(pDemod, DVBT_RF_AGC_VAL, &Value) != FUNCTION_SUCCESS)
		goto error_status_get_registers;


	// Convert Value to signed integer and store the signed integer to RfAgc.
	*pRfAgc = (int)BinToSignedInt(Value, RTL2832_RF_AGC_REG_BIT_NUM);


	return FUNCTION_SUCCESS;


error_status_get_registers:
	return FUNCTION_ERROR;
}





/**

@see   DVBT_DEMOD_FP_GET_IF_AGC

*/
int
rtl2832_GetIfAgc(
	DVBT_DEMOD_MODULE *pDemod,
	long *pIfAgc
	)
{
	unsigned long Value;



	// Get IF_AGC_VAL to Value.
	if(pDemod->GetRegBitsWithPage(pDemod, DVBT_IF_AGC_VAL, &Value) != FUNCTION_SUCCESS)
		goto error_status_get_registers;


	// Convert Value to signed integer and store the signed integer to IfAgc.
	*pIfAgc = (int)BinToSignedInt(Value, RTL2832_IF_AGC_REG_BIT_NUM);


	return FUNCTION_SUCCESS;


error_status_get_registers:
	return FUNCTION_ERROR;
}





/**

@see   DVBT_DEMOD_FP_GET_DI_AGC

*/
int
rtl2832_GetDiAgc(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned char *pDiAgc
	)
{
	unsigned long Value;



	// Get DAGC_VAL to DiAgc.
	if(pDemod->GetRegBitsWithPage(pDemod, DVBT_DAGC_VAL, &Value) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	*pDiAgc = (unsigned char)Value;


	return FUNCTION_SUCCESS;


error_status_get_registers:
	return FUNCTION_ERROR;
}





/**

@see   DVBT_DEMOD_FP_GET_TR_OFFSET_PPM

*/
int
rtl2832_GetTrOffsetPpm(
	DVBT_DEMOD_MODULE *pDemod,
	long *pTrOffsetPpm
	)
{
	unsigned long SfreqOffBinary;
	long SfreqOffInt;

	MPI MpiSfreqOffInt;
	MPI MpiConst, MpiVar;


	// Get SfreqOff binary value from SFREQ_OFF register bits.
	// Note: The function GetRegBitsWithPage() will set register page automatically.
	if(pDemod->GetRegBitsWithPage(pDemod, DVBT_SFREQ_OFF, &SfreqOffBinary) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;

	// Convert SfreqOff binary value to signed integer.
	SfreqOffInt = BinToSignedInt(SfreqOffBinary, RTL2832_SFREQ_OFF_BIT_NUM);

	
	// Get TR offset in ppm.
	// Note: Original formula:   TrOffsetPpm = (SfreqOffInt * 1000000) / pow(2, 24)
	//       Adjusted formula:   TrOffsetPpm = (SfreqOffInt * 1000000) >> 24
	MpiSetValue(&MpiSfreqOffInt, SfreqOffInt);
	MpiSetValue(&MpiConst,       1000000);

	MpiMul(&MpiVar, MpiSfreqOffInt, MpiConst);
	MpiRightShift(&MpiVar, MpiVar, 24);

	MpiGetValue(MpiVar, pTrOffsetPpm);


	return FUNCTION_SUCCESS;


error_status_get_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   DVBT_DEMOD_FP_GET_CR_OFFSET_HZ

*/
int
rtl2832_GetCrOffsetHz(
	DVBT_DEMOD_MODULE *pDemod,
	long *pCrOffsetHz
	)
{
	int BandwidthMode;
	int FftMode;

	unsigned long CfreqOffBinary;
	long CfreqOffInt;

	long ConstWithBandwidthMode, ConstWithFftMode;

	MPI MpiCfreqOffInt;
	MPI MpiConstWithBandwidthMode, MpiConstWithFftMode;
	MPI MpiConst, MpiVar0, MpiVar1, MpiNone;



	// Get demod bandwidth mode.
	if(pDemod->GetBandwidthMode(pDemod, &BandwidthMode) != FUNCTION_SUCCESS)
		goto error_status_get_demod_bandwidth_mode;


	// Get demod FFT mode.
	if(pDemod->GetFftMode(pDemod, &FftMode) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;


	// Get CfreqOff binary value from CFREQ_OFF register bits.
	// Note: The function GetRegBitsWithPage() will set register page automatically.
	if(pDemod->GetRegBitsWithPage(pDemod, DVBT_CFREQ_OFF, &CfreqOffBinary) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;

	// Convert CfreqOff binary value to signed integer.
	CfreqOffInt = BinToSignedInt(CfreqOffBinary, RTL2832_CFREQ_OFF_BIT_NUM);


	// Determine constant value with bandwidth mode.
	switch(BandwidthMode)
	{
		default:
		case DVBT_BANDWIDTH_6MHZ:	ConstWithBandwidthMode = 48000000;		break;
		case DVBT_BANDWIDTH_7MHZ:	ConstWithBandwidthMode = 56000000;		break;
		case DVBT_BANDWIDTH_8MHZ:	ConstWithBandwidthMode = 64000000;		break;
	}


	// Determine constant value with FFT mode.
	switch(FftMode)
	{
		default:
		case DVBT_FFT_MODE_2K:		ConstWithFftMode = 2048;		break;
		case DVBT_FFT_MODE_8K:		ConstWithFftMode = 8192;		break;
	}


	// Get Cr offset in Hz.
	// Note: Original formula:   CrOffsetHz = (CfreqOffInt * ConstWithBandwidthMode) / (ConstWithFftMode * 7 * 128)
	//       Adjusted formula:   CrOffsetHz = (CfreqOffInt * ConstWithBandwidthMode) / ((ConstWithFftMode * 7) << 7)
	MpiSetValue(&MpiCfreqOffInt,            CfreqOffInt);
	MpiSetValue(&MpiConstWithBandwidthMode, ConstWithBandwidthMode);
	MpiSetValue(&MpiConstWithFftMode,       ConstWithFftMode);
	MpiSetValue(&MpiConst,                  7);

	MpiMul(&MpiVar0, MpiCfreqOffInt, MpiConstWithBandwidthMode);
	MpiMul(&MpiVar1, MpiConstWithFftMode, MpiConst);
	MpiLeftShift(&MpiVar1, MpiVar1, 7);
	MpiDiv(&MpiVar0, &MpiNone, MpiVar0, MpiVar1);

	MpiGetValue(MpiVar0, pCrOffsetHz);


	return FUNCTION_SUCCESS;


error_status_get_demod_registers:
error_status_get_demod_bandwidth_mode:
	return FUNCTION_ERROR;
}





/**

@see   DVBT_DEMOD_FP_GET_CONSTELLATION

*/
int
rtl2832_GetConstellation(
	DVBT_DEMOD_MODULE *pDemod,
	int *pConstellation
	)
{
	unsigned long ReadingValue;


	// Get TPS constellation information from RX_CONSTEL.
	if(pDemod->GetRegBitsWithPage(pDemod, DVBT_RX_CONSTEL, &ReadingValue) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	switch(ReadingValue)
	{
		default:
		case 0:		*pConstellation = DVBT_CONSTELLATION_QPSK;			break;
		case 1:		*pConstellation = DVBT_CONSTELLATION_16QAM;			break;
		case 2:		*pConstellation = DVBT_CONSTELLATION_64QAM;			break;
	}


	return FUNCTION_SUCCESS;


error_status_get_registers:
	return FUNCTION_ERROR;
}





/**

@see   DVBT_DEMOD_FP_GET_HIERARCHY

*/
int
rtl2832_GetHierarchy(
	DVBT_DEMOD_MODULE *pDemod,
	int *pHierarchy
	)
{
	unsigned long ReadingValue;


	// Get TPS hierarchy infromation from RX_HIER.
	if(pDemod->GetRegBitsWithPage(pDemod, DVBT_RX_HIER, &ReadingValue) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	switch(ReadingValue)
	{
		default:
		case 0:		*pHierarchy = DVBT_HIERARCHY_NONE;				break;
		case 1:		*pHierarchy = DVBT_HIERARCHY_ALPHA_1;			break;
		case 2:		*pHierarchy = DVBT_HIERARCHY_ALPHA_2;			break;
		case 3:		*pHierarchy = DVBT_HIERARCHY_ALPHA_4;			break;
	}


	return FUNCTION_SUCCESS;


error_status_get_registers:
	return FUNCTION_ERROR;
}





/**

@see   DVBT_DEMOD_FP_GET_CODE_RATE_LP

*/
int
rtl2832_GetCodeRateLp(
	DVBT_DEMOD_MODULE *pDemod,
	int *pCodeRateLp
	)
{
	unsigned long ReadingValue;


	// Get TPS low-priority code rate infromation from RX_C_RATE_LP.
	if(pDemod->GetRegBitsWithPage(pDemod, DVBT_RX_C_RATE_LP, &ReadingValue) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	switch(ReadingValue)
	{
		default:
		case 0:		*pCodeRateLp = DVBT_CODE_RATE_1_OVER_2;			break;
		case 1:		*pCodeRateLp = DVBT_CODE_RATE_2_OVER_3;			break;
		case 2:		*pCodeRateLp = DVBT_CODE_RATE_3_OVER_4;			break;
		case 3:		*pCodeRateLp = DVBT_CODE_RATE_5_OVER_6;			break;
		case 4:		*pCodeRateLp = DVBT_CODE_RATE_7_OVER_8;			break;
	}


	return FUNCTION_SUCCESS;


error_status_get_registers:
	return FUNCTION_ERROR;
}





/**

@see   DVBT_DEMOD_FP_GET_CODE_RATE_HP

*/
int
rtl2832_GetCodeRateHp(
	DVBT_DEMOD_MODULE *pDemod,
	int *pCodeRateHp
	)
{
	unsigned long ReadingValue;


	// Get TPS high-priority code rate infromation from RX_C_RATE_HP.
	if(pDemod->GetRegBitsWithPage(pDemod, DVBT_RX_C_RATE_HP, &ReadingValue) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	switch(ReadingValue)
	{
		default:
		case 0:		*pCodeRateHp = DVBT_CODE_RATE_1_OVER_2;			break;
		case 1:		*pCodeRateHp = DVBT_CODE_RATE_2_OVER_3;			break;
		case 2:		*pCodeRateHp = DVBT_CODE_RATE_3_OVER_4;			break;
		case 3:		*pCodeRateHp = DVBT_CODE_RATE_5_OVER_6;			break;
		case 4:		*pCodeRateHp = DVBT_CODE_RATE_7_OVER_8;			break;
	}


	return FUNCTION_SUCCESS;


error_status_get_registers:
	return FUNCTION_ERROR;
}





/**

@see   DVBT_DEMOD_FP_GET_GUARD_INTERVAL

*/
int
rtl2832_GetGuardInterval(
	DVBT_DEMOD_MODULE *pDemod,
	int *pGuardInterval
	)
{
	unsigned long ReadingValue;


	// Get TPS guard interval infromation from GI_IDX.
	if(pDemod->GetRegBitsWithPage(pDemod, DVBT_GI_IDX, &ReadingValue) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	switch(ReadingValue)
	{
		default:
		case 0:		*pGuardInterval = DVBT_GUARD_INTERVAL_1_OVER_32;			break;
		case 1:		*pGuardInterval = DVBT_GUARD_INTERVAL_1_OVER_16;			break;
		case 2:		*pGuardInterval = DVBT_GUARD_INTERVAL_1_OVER_8;				break;
		case 3:		*pGuardInterval = DVBT_GUARD_INTERVAL_1_OVER_4;				break;
	}


	return FUNCTION_SUCCESS;


error_status_get_registers:
	return FUNCTION_ERROR;
}





/**

@see   DVBT_DEMOD_FP_GET_FFT_MODE

*/
int
rtl2832_GetFftMode(
	DVBT_DEMOD_MODULE *pDemod,
	int *pFftMode
	)
{
	unsigned long ReadingValue;


	// Get TPS FFT mode infromation from FFT_MODE_IDX.
	if(pDemod->GetRegBitsWithPage(pDemod, DVBT_FFT_MODE_IDX, &ReadingValue) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	switch(ReadingValue)
	{
		default:
		case 0:		*pFftMode = DVBT_FFT_MODE_2K;			break;
		case 1:		*pFftMode = DVBT_FFT_MODE_8K;			break;
	}


	return FUNCTION_SUCCESS;


error_status_get_registers:
	return FUNCTION_ERROR;
}





/**

@see   DVBT_DEMOD_FP_UPDATE_FUNCTION

*/
int
rtl2832_UpdateFunction(
	DVBT_DEMOD_MODULE *pDemod
	)
{
	RTL2832_EXTRA_MODULE *pExtra;


	// Get demod extra module.
	pExtra = &(pDemod->Extra.Rtl2832);


	// Execute Function 1 according to Function 1 enabling status
	if(pExtra->IsFunc1Enabled == YES)
	{
		if(rtl2832_func1_Update(pDemod) != FUNCTION_SUCCESS)
			goto error_status_execute_function;
	}


	return FUNCTION_SUCCESS;


error_status_execute_function:
	return FUNCTION_ERROR;
}





/**

@see   DVBT_DEMOD_FP_RESET_FUNCTION

*/
int
rtl2832_ResetFunction(
	DVBT_DEMOD_MODULE *pDemod
	)
{
	RTL2832_EXTRA_MODULE *pExtra;


	// Get demod extra module.
	pExtra = &(pDemod->Extra.Rtl2832);


	// Reset Function 1 settings according to Function 1 enabling status.
	if(pExtra->IsFunc1Enabled == YES)
	{
		if(rtl2832_func1_Reset(pDemod) != FUNCTION_SUCCESS)
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
rtl2832_ForwardI2cReadingCmd(
	I2C_BRIDGE_MODULE *pI2cBridge,
	unsigned char DeviceAddr,
	unsigned char *pReadingBytes,
	unsigned long ByteNum
	)
{
	DVBT_DEMOD_MODULE *pDemod;
	BASE_INTERFACE_MODULE *pBaseInterface;



	// Get demod module.
	pDemod = (DVBT_DEMOD_MODULE *)pI2cBridge->pPrivateData;


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
rtl2832_ForwardI2cWritingCmd(
	I2C_BRIDGE_MODULE *pI2cBridge,
	unsigned char DeviceAddr,
	const unsigned char *pWritingBytes,
	unsigned long ByteNum
	)
{
	DVBT_DEMOD_MODULE *pDemod;
	BASE_INTERFACE_MODULE *pBaseInterface;



	// Get demod module.
	pDemod = (DVBT_DEMOD_MODULE *)pI2cBridge->pPrivateData;


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

@brief   Initialize RTL2832 register table.

Use rtl2832_InitRegTable() to initialize RTL2832 register table.


@param [in]   pDemod   RTL2832 demod module pointer


@note
	-# The rtl2832_InitRegTable() function will be called by BuildRtl2832Module().

*/
void
rtl2832_InitRegTable(
	DVBT_DEMOD_MODULE *pDemod
	)
{
	static const DVBT_PRIMARY_REG_ENTRY PrimaryRegTable[RTL2832_REG_TABLE_LEN] =
	{
		// Software reset register
		// RegBitName,						PageNo,		RegStartAddr,	MSB,	LSB
		{DVBT_SOFT_RST,						0x1,		0x1,			2,		2},

		// Tuner I2C forwording register
		// RegBitName,						PageNo,		RegStartAddr,	MSB,	LSB
		{DVBT_IIC_REPEAT,					0x1,		0x1,			3,		3},

		// Registers for initialization
		// RegBitName,						PageNo,		RegStartAddr,	MSB,	LSB
		{DVBT_TR_WAIT_MIN_8K,				0x1,		0x88,			11,		2},
		{DVBT_RSD_BER_FAIL_VAL,				0x1,		0x8f,			15,		0},
		{DVBT_EN_BK_TRK,					0x1,		0xa6,			7,		7},
		{DVBT_AD_EN_REG,					0x0,		0x8,			7,		7},
		{DVBT_AD_EN_REG1,					0x0,		0x8,			6,		6},
		{DVBT_EN_BBIN,						0x1,		0xb1,			0,		0},
		{DVBT_MGD_THD0,						0x1,		0x95,			7,		0},
		{DVBT_MGD_THD1,						0x1,		0x96,			7,		0},
		{DVBT_MGD_THD2,						0x1,		0x97,			7,		0},
		{DVBT_MGD_THD3,						0x1,		0x98,			7,		0},
		{DVBT_MGD_THD4,						0x1,		0x99,			7,		0},
		{DVBT_MGD_THD5,						0x1,		0x9a,			7,		0},
		{DVBT_MGD_THD6,						0x1,		0x9b,			7,		0},
		{DVBT_MGD_THD7,						0x1,		0x9c,			7,		0},
		{DVBT_EN_CACQ_NOTCH,				0x1,		0x61,			4,		4},
		{DVBT_AD_AV_REF,					0x0,		0x9,			6,		0},
		{DVBT_REG_PI,						0x0,		0xa,			2,		0},
		{DVBT_PIP_ON,						0x0,		0x21,			3,		3},
		{DVBT_SCALE1_B92,					0x2,		0x92,			7,		0},
		{DVBT_SCALE1_B93,					0x2,		0x93,			7,		0},
		{DVBT_SCALE1_BA7,					0x2,		0xa7,			7,		0},
		{DVBT_SCALE1_BA9,					0x2,		0xa9,			7,		0},
		{DVBT_SCALE1_BAA,					0x2,		0xaa,			7,		0},
		{DVBT_SCALE1_BAB,					0x2,		0xab,			7,		0},
		{DVBT_SCALE1_BAC,					0x2,		0xac,			7,		0},
		{DVBT_SCALE1_BB0,					0x2,		0xb0,			7,		0},
		{DVBT_SCALE1_BB1,					0x2,		0xb1,			7,		0},
		{DVBT_KB_P1,						0x1,		0x64,			3,		1},
		{DVBT_KB_P2,						0x1,		0x64,			6,		4},
		{DVBT_KB_P3,						0x1,		0x65,			2,		0},
		{DVBT_OPT_ADC_IQ,					0x0,		0x6,			5,		4},
		{DVBT_AD_AVI,						0x0,		0x9,			1,		0},
		{DVBT_AD_AVQ,						0x0,		0x9,			3,		2},
		{DVBT_K1_CR_STEP12,					0x2,		0xad,			9,		4},

		// Registers for initialization according to mode
		// RegBitName,						PageNo,		RegStartAddr,	MSB,	LSB
		{DVBT_TRK_KS_P2,					0x1,		0x6f,			2,		0},
		{DVBT_TRK_KS_I2,					0x1,		0x70,			5,		3},
		{DVBT_TR_THD_SET2,					0x1,		0x72,			3,		0},
		{DVBT_TRK_KC_P2,					0x1,		0x73,			5,		3},
		{DVBT_TRK_KC_I2,					0x1,		0x75,			2,		0},
		{DVBT_CR_THD_SET2,					0x1,		0x76,			7,		6},

		// Registers for IF setting
		// RegBitName,						PageNo,		RegStartAddr,	MSB,	LSB
		{DVBT_PSET_IFFREQ,					0x1,		0x19,			21,		0},
		{DVBT_SPEC_INV,						0x1,		0x15,			0,		0},

		// Registers for bandwidth programming
		// RegBitName,						PageNo,		RegStartAddr,	MSB,	LSB
		{DVBT_RSAMP_RATIO,					0x1,		0x9f,			27,		2},
		{DVBT_CFREQ_OFF_RATIO,				0x1,		0x9d,			23,		4},

		// FSM stage register
		// RegBitName,						PageNo,		RegStartAddr,	MSB,	LSB
		{DVBT_FSM_STAGE,					0x3,		0x51,			6,		3},

		// TPS content registers
		// RegBitName,						PageNo,		RegStartAddr,	MSB,	LSB
		{DVBT_RX_CONSTEL,					0x3,		0x3c,			3,		2},
		{DVBT_RX_HIER,						0x3,		0x3c,			6,		4},
		{DVBT_RX_C_RATE_LP,					0x3,		0x3d,			2,		0},
		{DVBT_RX_C_RATE_HP,					0x3,		0x3d,			5,		3},
		{DVBT_GI_IDX,						0x3,		0x51,			1,		0},
		{DVBT_FFT_MODE_IDX,					0x3,		0x51,			2,		2},

		// Performance measurement registers
		// RegBitName,						PageNo,		RegStartAddr,	MSB,	LSB
		{DVBT_RSD_BER_EST,					0x3,		0x4e,			15,		0},
		{DVBT_CE_EST_EVM,					0x4,		0xc,			15,		0},

		// AGC registers
		// RegBitName,						PageNo,		RegStartAddr,	MSB,	LSB
		{DVBT_RF_AGC_VAL,					0x3,		0x5b,			13,		0},
		{DVBT_IF_AGC_VAL,					0x3,		0x59,			13,		0},
		{DVBT_DAGC_VAL,						0x3,		0x5,			7,		0},

		// TR offset and CR offset registers
		// RegBitName,						PageNo,		RegStartAddr,	MSB,	LSB
		{DVBT_SFREQ_OFF,					0x3,		0x18,			13,		0},
		{DVBT_CFREQ_OFF,					0x3,		0x5f,			17,		0},

		// AGC relative registers
		// RegBitName,						PageNo,		RegStartAddr,	MSB,	LSB
		{DVBT_POLAR_RF_AGC,					0x0,		0xe,			1,		1},
		{DVBT_POLAR_IF_AGC,					0x0,		0xe,			0,		0},
		{DVBT_AAGC_HOLD,					0x1,		0x4,			5,		5},
		{DVBT_EN_RF_AGC,					0x1,		0x4,			6,		6},
		{DVBT_EN_IF_AGC,					0x1,		0x4,			7,		7},
		{DVBT_IF_AGC_MIN,					0x1,		0x8,			7,		0},
		{DVBT_IF_AGC_MAX,					0x1,		0x9,			7,		0},
		{DVBT_RF_AGC_MIN,					0x1,		0xa,			7,		0},
		{DVBT_RF_AGC_MAX,					0x1,		0xb,			7,		0},
		{DVBT_IF_AGC_MAN,					0x1,		0xc,			6,		6},
		{DVBT_IF_AGC_MAN_VAL,				0x1,		0xc,			13,		0},
		{DVBT_RF_AGC_MAN,					0x1,		0xe,			6,		6},
		{DVBT_RF_AGC_MAN_VAL,				0x1,		0xe,			13,		0},
		{DVBT_DAGC_TRG_VAL,					0x1,		0x12,			7,		0},
		{DVBT_AGC_TARG_VAL_0,				0x1,		0x2,			0,		0},
		{DVBT_AGC_TARG_VAL_8_1,				0x1,		0x3,			7,		0},
		{DVBT_AAGC_LOOP_GAIN,				0x1,		0xc7,			5,		1},
		{DVBT_LOOP_GAIN2_3_0,				0x1,		0x4,			4,		1},
		{DVBT_LOOP_GAIN2_4,					0x1,		0x5,			7,		7},
		{DVBT_LOOP_GAIN3,					0x1,		0xc8,			4,		0},
		{DVBT_VTOP1,						0x1,		0x6,			5,		0},
		{DVBT_VTOP2,						0x1,		0xc9,			5,		0},
		{DVBT_VTOP3,						0x1,		0xca,			5,		0},
		{DVBT_KRF1,							0x1,		0xcb,			7,		0},
		{DVBT_KRF2,							0x1,		0x7,			7,		0},
		{DVBT_KRF3,							0x1,		0xcd,			7,		0},
		{DVBT_KRF4,							0x1,		0xce,			7,		0},
		{DVBT_EN_GI_PGA,					0x1,		0xe5,			0,		0},
		{DVBT_THD_LOCK_UP,					0x1,		0xd9,			8,		0},
		{DVBT_THD_LOCK_DW,					0x1,		0xdb,			8,		0},
		{DVBT_THD_UP1,						0x1,		0xdd,			7,		0},
		{DVBT_THD_DW1,						0x1,		0xde,			7,		0},
		{DVBT_INTER_CNT_LEN,				0x1,		0xd8,			3,		0},
		{DVBT_GI_PGA_STATE,					0x1,		0xe6,			3,		3},
		{DVBT_EN_AGC_PGA,					0x1,		0xd7,			0,		0},

		// TS interface registers
		// RegBitName,						PageNo,		RegStartAddr,	MSB,	LSB
		{DVBT_CKOUTPAR,						0x1,		0x7b,			5,		5},
		{DVBT_CKOUT_PWR,					0x1,		0x7b,			6,		6},
		{DVBT_SYNC_DUR,						0x1,		0x7b,			7,		7},
		{DVBT_ERR_DUR,						0x1,		0x7c,			0,		0},
		{DVBT_SYNC_LVL,						0x1,		0x7c,			1,		1},
		{DVBT_ERR_LVL,						0x1,		0x7c,			2,		2},
		{DVBT_VAL_LVL,						0x1,		0x7c,			3,		3},
		{DVBT_SERIAL,						0x1,		0x7c,			4,		4},
		{DVBT_SER_LSB,						0x1,		0x7c,			5,		5},
		{DVBT_CDIV_PH0,						0x1,		0x7d,			3,		0},
		{DVBT_CDIV_PH1,						0x1,		0x7d,			7,		4},
		{DVBT_MPEG_IO_OPT_2_2,				0x0,		0x6,			7,		7},
		{DVBT_MPEG_IO_OPT_1_0,				0x0,		0x7,			7,		6},
		{DVBT_CKOUTPAR_PIP,					0x0,		0xb7,			4,		4},
		{DVBT_CKOUT_PWR_PIP,				0x0,		0xb7,			3,		3},
		{DVBT_SYNC_LVL_PIP,					0x0,		0xb7,			2,		2},
		{DVBT_ERR_LVL_PIP,					0x0,		0xb7,			1,		1},
		{DVBT_VAL_LVL_PIP,					0x0,		0xb7,			0,		0},
		{DVBT_CKOUTPAR_PID,					0x0,		0xb9,			4,		4},
		{DVBT_CKOUT_PWR_PID,				0x0,		0xb9,			3,		3},
		{DVBT_SYNC_LVL_PID,					0x0,		0xb9,			2,		2},
		{DVBT_ERR_LVL_PID,					0x0,		0xb9,			1,		1},
		{DVBT_VAL_LVL_PID,					0x0,		0xb9,			0,		0},

		// FSM state-holding register
		// RegBitName,						PageNo,		RegStartAddr,	MSB,	LSB
		{DVBT_SM_PASS,						0x1,		0x93,			11,		0},

		// AD7 registers
		// RegBitName,						PageNo,		RegStartAddr,	MSB,	LSB
		{DVBT_AD7_SETTING,					0x0,		0x11,			15,		0},
		{DVBT_RSSI_R,						0x3,		0x1,			6,		0},

		// ACI detection registers
		// RegBitName,						PageNo,		RegStartAddr,	MSB,	LSB
		{DVBT_ACI_DET_IND,					0x3,		0x12,			0,		0},

		// Clock output registers
		// RegBitName,						PageNo,		RegStartAddr,	MSB,	LSB
		{DVBT_REG_MON,						0x0,		0xd,			1,		0},
		{DVBT_REG_MONSEL,					0x0,		0xd,			2,		2},
		{DVBT_REG_GPE,						0x0,		0xd,			7,		7},
		{DVBT_REG_GPO,						0x0,		0x10,			0,		0},
		{DVBT_REG_4MSEL,					0x0,		0x13,			0,		0},
	};


	int i;
	int RegBitName;



	// Initialize register table according to primary register table.
	// Note: 1. Register table rows are sorted by register bit name key.
	//       2. The default value of the IsAvailable variable is "NO".
	for(i = 0; i < DVBT_REG_TABLE_LEN_MAX; i++)
		pDemod->RegTable[i].IsAvailable  = NO;

	for(i = 0; i < RTL2832_REG_TABLE_LEN; i++)
	{
		RegBitName = PrimaryRegTable[i].RegBitName;

		pDemod->RegTable[RegBitName].IsAvailable  = YES;
		pDemod->RegTable[RegBitName].PageNo       = PrimaryRegTable[i].PageNo;
		pDemod->RegTable[RegBitName].RegStartAddr = PrimaryRegTable[i].RegStartAddr;
		pDemod->RegTable[RegBitName].Msb          = PrimaryRegTable[i].Msb;
		pDemod->RegTable[RegBitName].Lsb          = PrimaryRegTable[i].Lsb;
	}


	return;
}





/**

@brief   Set I2C bridge module demod arguments.

RTL2832 builder will use rtl2832_BuildI2cBridgeModule() to set I2C bridge module demod arguments.


@param [in]   pDemod   The demod module pointer


@see   BuildRtl2832Module()

*/
void
rtl2832_BuildI2cBridgeModule(
	DVBT_DEMOD_MODULE *pDemod
	)
{
	I2C_BRIDGE_MODULE *pI2cBridge;



	// Get I2C bridge module.
	pI2cBridge = pDemod->pI2cBridge;

	// Set I2C bridge module demod arguments.
	pI2cBridge->pPrivateData = (void *)pDemod;
	pI2cBridge->ForwardI2cReadingCmd = rtl2832_ForwardI2cReadingCmd;
	pI2cBridge->ForwardI2cWritingCmd = rtl2832_ForwardI2cWritingCmd;


	return;
}





/*

@see   RTL2832_FP_GET_APP_MODE

*/
void
rtl2832_GetAppMode(
	DVBT_DEMOD_MODULE *pDemod,
	int *pAppMode
	)
{
	RTL2832_EXTRA_MODULE *pExtra;



	// Get demod extra module.
	pExtra = &(pDemod->Extra.Rtl2832);


	// Get demod type from demod module.
	*pAppMode = pExtra->AppMode;


	return;
}





/**

@brief   Reset Function 1 variables and registers.

One can use rtl2832_func1_Reset() to reset Function 1 variables and registers.


@param [in]   pDemod   The demod module pointer


@retval   FUNCTION_SUCCESS   Reset Function 1 variables and registers successfully.
@retval   FUNCTION_ERROR     Reset Function 1 variables and registers unsuccessfully.


@note
	-# Need to execute Function 1 reset function when change tuner RF frequency or demod parameters.
	-# Function 1 update flow also employs Function 1 reset function.

*/
int
rtl2832_func1_Reset(
	DVBT_DEMOD_MODULE *pDemod
	)
{
	RTL2832_EXTRA_MODULE *pExtra;



	// Get demod extra module.
	pExtra = &(pDemod->Extra.Rtl2832);

	// Reset demod Function 1 variables.
	pExtra->Func1State               = RTL2832_FUNC1_STATE_NORMAL;
	pExtra->Func1WaitTime            = 0;
	pExtra->Func1GettingTime         = 0;
	pExtra->Func1RsdBerEstSumNormal  = 0;
	pExtra->Func1RsdBerEstSumConfig1 = 0;
	pExtra->Func1RsdBerEstSumConfig2 = 0;
	pExtra->Func1RsdBerEstSumConfig3 = 0;


	// Reset demod Function 1 registers.
    if(rtl2832_func1_ResetReg(pDemod) != FUNCTION_SUCCESS)
		goto error_status_execute_function;


	return FUNCTION_SUCCESS;


error_status_execute_function:
	return FUNCTION_ERROR;
}





/**

@brief   Update demod registers with Function 1.

One can use rtl2832_func1_Update() to update demod registers with Function 1.


@param [in]   pDemod   The demod module pointer


@retval   FUNCTION_SUCCESS   Update demod registers with Function 1 successfully.
@retval   FUNCTION_ERROR     Update demod registers with Function 1 unsuccessfully.

*/
int
rtl2832_func1_Update(
	DVBT_DEMOD_MODULE *pDemod
	)
{
	RTL2832_EXTRA_MODULE *pExtra;

	int Answer;
	int MinWeightedBerConfigMode;



	// Get demod extra module.
	pExtra = &(pDemod->Extra.Rtl2832);


	// Run FSM.
	switch(pExtra->Func1State)
	{
		case RTL2832_FUNC1_STATE_NORMAL:

			// Ask if criterion is matched.
			if(rtl2832_func1_IsCriterionMatched(pDemod, &Answer) != FUNCTION_SUCCESS)
				goto error_status_execute_function;

			if(Answer == YES)
			{
				// Accumulate RSD_BER_EST for normal case.
				if(rtl2832_func1_AccumulateRsdBerEst(pDemod, &pExtra->Func1RsdBerEstSumNormal) != FUNCTION_SUCCESS)
					goto error_status_execute_function;

				// Reset getting time counter.
				pExtra->Func1GettingTime = 0;

				// Go to RTL2832_FUNC1_STATE_NORMAL_GET_BER state.
				pExtra->Func1State = RTL2832_FUNC1_STATE_NORMAL_GET_BER;
			}

			break;


		case RTL2832_FUNC1_STATE_NORMAL_GET_BER:

			// Accumulate RSD_BER_EST for normal case.
			if(rtl2832_func1_AccumulateRsdBerEst(pDemod, &pExtra->Func1RsdBerEstSumNormal) != FUNCTION_SUCCESS)
				goto error_status_execute_function;

			// Use getting time counter to hold RTL2832_FUNC1_STATE_NORMAL_GET_BER state several times.
			pExtra->Func1GettingTime += 1;

			if(pExtra->Func1GettingTime >= pExtra->Func1GettingTimeMax)
			{
				// Set common registers for configuration 1, 2, and 3 case.
				if(rtl2832_func1_SetCommonReg(pDemod) != FUNCTION_SUCCESS)
					goto error_status_execute_function;

				// Set registers with FFT mode for configuration 1, 2, and 3 case.
				if(rtl2832_func1_SetRegWithFftMode(pDemod, pExtra->Func1FftBak) != FUNCTION_SUCCESS)
					goto error_status_execute_function;

				// Set registers for configuration 1 case.
				if(rtl2832_func1_SetRegWithConfigMode(pDemod, RTL2832_FUNC1_CONFIG_1) != FUNCTION_SUCCESS)
					goto error_status_execute_function;

				// Reset demod by software reset.
				if(pDemod->SoftwareReset(pDemod) != FUNCTION_SUCCESS)
					goto error_status_execute_function;

				// Reset wait time counter.
				pExtra->Func1WaitTime = 0;

				// Go to RTL2832_FUNC1_STATE_CONFIG_1_WAIT state.
				pExtra->Func1State = RTL2832_FUNC1_STATE_CONFIG_1_WAIT;
			}

			break;


		case RTL2832_FUNC1_STATE_CONFIG_1_WAIT:

			// Use wait time counter to hold RTL2832_FUNC1_STATE_CONFIG_1_WAIT state several times.
			pExtra->Func1WaitTime += 1;

			if(pExtra->Func1WaitTime >= pExtra->Func1WaitTimeMax)
			{
				// Accumulate RSD_BER_EST for configuration 1 case.
				if(rtl2832_func1_AccumulateRsdBerEst(pDemod, &pExtra->Func1RsdBerEstSumConfig1) != FUNCTION_SUCCESS)
					goto error_status_execute_function;

				// Reset getting time counter.
				pExtra->Func1GettingTime = 0;

				// Go to RTL2832_FUNC1_STATE_CONFIG_1_GET_BER state.
				pExtra->Func1State = RTL2832_FUNC1_STATE_CONFIG_1_GET_BER;
			}

			break;


		case RTL2832_FUNC1_STATE_CONFIG_1_GET_BER:

			// Accumulate RSD_BER_EST for configuration 1 case.
			if(rtl2832_func1_AccumulateRsdBerEst(pDemod, &pExtra->Func1RsdBerEstSumConfig1) != FUNCTION_SUCCESS)
				goto error_status_execute_function;

			// Use getting time counter to hold RTL2832_FUNC1_STATE_CONFIG_1_GET_BER state several times.
			pExtra->Func1GettingTime += 1;

			if(pExtra->Func1GettingTime >= pExtra->Func1GettingTimeMax)
			{
				// Set registers for configuration 2 case.
				if(rtl2832_func1_SetRegWithConfigMode(pDemod, RTL2832_FUNC1_CONFIG_2) != FUNCTION_SUCCESS)
					goto error_status_execute_function;

				// Reset demod by software reset.
				if(pDemod->SoftwareReset(pDemod) != FUNCTION_SUCCESS)
					goto error_status_execute_function;

				// Reset wait time counter.
				pExtra->Func1WaitTime = 0;

				// Go to RTL2832_FUNC1_STATE_CONFIG_2_WAIT state.
				pExtra->Func1State = RTL2832_FUNC1_STATE_CONFIG_2_WAIT;
			}

			break;


		case RTL2832_FUNC1_STATE_CONFIG_2_WAIT:

			// Use wait time counter to hold RTL2832_FUNC1_STATE_CONFIG_2_WAIT state several times.
			pExtra->Func1WaitTime += 1;

			if(pExtra->Func1WaitTime >= pExtra->Func1WaitTimeMax)
			{
				// Accumulate RSD_BER_EST for configuration 2 case.
				if(rtl2832_func1_AccumulateRsdBerEst(pDemod, &pExtra->Func1RsdBerEstSumConfig2) != FUNCTION_SUCCESS)
					goto error_status_execute_function;

				// Reset getting time counter.
				pExtra->Func1GettingTime = 0;

				// Go to RTL2832_FUNC1_STATE_CONFIG_2_GET_BER state.
				pExtra->Func1State = RTL2832_FUNC1_STATE_CONFIG_2_GET_BER;
			}

			break;


		case RTL2832_FUNC1_STATE_CONFIG_2_GET_BER:

			// Accumulate RSD_BER_EST for configuration 2 case.
			if(rtl2832_func1_AccumulateRsdBerEst(pDemod, &pExtra->Func1RsdBerEstSumConfig2) != FUNCTION_SUCCESS)
				goto error_status_execute_function;

			// Use getting time counter to hold RTL2832_FUNC1_STATE_CONFIG_2_GET_BER state several times.
			pExtra->Func1GettingTime += 1;

			if(pExtra->Func1GettingTime >= pExtra->Func1GettingTimeMax)
			{
				// Set registers for configuration 3 case.
				if(rtl2832_func1_SetRegWithConfigMode(pDemod, RTL2832_FUNC1_CONFIG_3) != FUNCTION_SUCCESS)
					goto error_status_execute_function;

				// Reset demod by software reset.
				if(pDemod->SoftwareReset(pDemod) != FUNCTION_SUCCESS)
					goto error_status_execute_function;

				// Reset wait time counter.
				pExtra->Func1WaitTime = 0;

				// Go to RTL2832_FUNC1_STATE_CONFIG_3_WAIT state.
				pExtra->Func1State = RTL2832_FUNC1_STATE_CONFIG_3_WAIT;
			}

			break;


		case RTL2832_FUNC1_STATE_CONFIG_3_WAIT:

			// Use wait time counter to hold RTL2832_FUNC1_STATE_CONFIG_3_WAIT state several times.
			pExtra->Func1WaitTime += 1;

			if(pExtra->Func1WaitTime >= pExtra->Func1WaitTimeMax)
			{
				// Accumulate RSD_BER_EST for configuration 3 case.
				if(rtl2832_func1_AccumulateRsdBerEst(pDemod, &pExtra->Func1RsdBerEstSumConfig3) != FUNCTION_SUCCESS)
					goto error_status_execute_function;

				// Reset getting time counter.
				pExtra->Func1GettingTime = 0;

				// Go to RTL2832_FUNC1_STATE_CONFIG_3_GET_BER state.
				pExtra->Func1State = RTL2832_FUNC1_STATE_CONFIG_3_GET_BER;
			}

			break;


		case RTL2832_FUNC1_STATE_CONFIG_3_GET_BER:

			// Accumulate RSD_BER_EST for configuration 3 case.
			if(rtl2832_func1_AccumulateRsdBerEst(pDemod, &pExtra->Func1RsdBerEstSumConfig3) != FUNCTION_SUCCESS)
				goto error_status_execute_function;

			// Use getting time counter to hold RTL2832_FUNC1_STATE_CONFIG_3_GET_BER state several times.
			pExtra->Func1GettingTime += 1;

			if(pExtra->Func1GettingTime >= pExtra->Func1GettingTimeMax)
			{
				// Determine minimum-weighted-BER configuration mode.
				rtl2832_func1_GetMinWeightedBerConfigMode(pDemod, &MinWeightedBerConfigMode);

				// Set registers with minimum-weighted-BER configuration mode.
				switch(MinWeightedBerConfigMode)
				{
					case RTL2832_FUNC1_CONFIG_NORMAL:

						// Reset registers for normal configuration.
						if(rtl2832_func1_ResetReg(pDemod) != FUNCTION_SUCCESS)
							goto error_status_execute_function;

						break;


					case RTL2832_FUNC1_CONFIG_1:
					case RTL2832_FUNC1_CONFIG_2:
					case RTL2832_FUNC1_CONFIG_3:

						// Set registers for minimum-weighted-BER configuration.
						if(rtl2832_func1_SetRegWithConfigMode(pDemod, MinWeightedBerConfigMode) != FUNCTION_SUCCESS)
							goto error_status_execute_function;

						break;


					default:

						// Get error configuration mode, reset registers.
						if(rtl2832_func1_ResetReg(pDemod) != FUNCTION_SUCCESS)
							goto error_status_execute_function;

						break;
				}

				// Reset demod by software reset.
				if(pDemod->SoftwareReset(pDemod) != FUNCTION_SUCCESS)
					goto error_status_execute_function;

				// Reset wait time counter.
				pExtra->Func1WaitTime = 0;

				// Go to RTL2832_FUNC1_STATE_DETERMINED_WAIT state.
				pExtra->Func1State = RTL2832_FUNC1_STATE_DETERMINED_WAIT;
			}

			break;


		case RTL2832_FUNC1_STATE_DETERMINED_WAIT:

			// Use wait time counter to hold RTL2832_FUNC1_STATE_CONFIG_3_WAIT state several times.
			pExtra->Func1WaitTime += 1;

			if(pExtra->Func1WaitTime >= pExtra->Func1WaitTimeMax)
			{
				// Go to RTL2832_FUNC1_STATE_DETERMINED state.
				pExtra->Func1State = RTL2832_FUNC1_STATE_DETERMINED;
			}

			break;


		case RTL2832_FUNC1_STATE_DETERMINED:

			// Ask if criterion is matched.
			if(rtl2832_func1_IsCriterionMatched(pDemod, &Answer) != FUNCTION_SUCCESS)
				goto error_status_execute_function;

			if(Answer == NO)
			{
				// Reset FSM.
				// Note: rtl2832_func1_Reset() will set FSM state with RTL2832_FUNC1_STATE_NORMAL.
				if(rtl2832_func1_Reset(pDemod) != FUNCTION_SUCCESS)
					goto error_status_execute_function;
			}

			break;


		default:

			// Get error state, reset FSM.
			// Note: rtl2832_func1_Reset() will set FSM state with RTL2832_FUNC1_STATE_NORMAL.
			if(rtl2832_func1_Reset(pDemod) != FUNCTION_SUCCESS)
				goto error_status_execute_function;

			break;
	}




	return FUNCTION_SUCCESS;


error_status_execute_function:
	return FUNCTION_ERROR;
}





/**

@brief   Ask if criterion is matched for Function 1.

One can use rtl2832_func1_IsCriterionMatched() to ask if criterion is matched for Function 1.


@param [in]    pDemod    The demod module pointer
@param [out]   pAnswer   Pointer to an allocated memory for storing answer


@retval   FUNCTION_SUCCESS   Ask if criterion is matched for Function 1 successfully.
@retval   FUNCTION_ERROR     Ask if criterion is matched for Function 1 unsuccessfully.

*/
int
rtl2832_func1_IsCriterionMatched(
	DVBT_DEMOD_MODULE *pDemod,
	int *pAnswer
	)
{
	RTL2832_EXTRA_MODULE *pExtra;

	unsigned long FsmStage;

	int Qam;
	int Hier;
	int LpCr;
	int HpCr;
	int Gi;
	int Fft;

	unsigned long Reg0, Reg1;

	int BandwidthMode;



	// Get demod extra module.
	pExtra = &(pDemod->Extra.Rtl2832);


	// Get FSM_STAGE.
    if(pDemod->GetRegBitsWithPage(pDemod, DVBT_FSM_STAGE, &FsmStage) != FUNCTION_SUCCESS)
		goto error_status_get_registers;


	// Get QAM.
	if(pDemod->GetConstellation(pDemod, &Qam) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Get hierarchy.
	if(pDemod->GetHierarchy(pDemod, &Hier) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Get low-priority code rate.
	if(pDemod->GetCodeRateLp(pDemod, &LpCr) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Get high-priority code rate.
	if(pDemod->GetCodeRateHp(pDemod, &HpCr) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Get guard interval.
	if(pDemod->GetGuardInterval(pDemod, &Gi) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Get FFT mode.
	if(pDemod->GetFftMode(pDemod, &Fft) != FUNCTION_SUCCESS)
		goto error_status_execute_function;


	// Get REG_0 and REG_1.
	if(pDemod->SetRegPage(pDemod, 0x3) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

    if(pDemod->GetRegMaskBits(pDemod, 0x22, 0, 0, &Reg0) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

    if(pDemod->GetRegMaskBits(pDemod, 0x1a, 15, 3, &Reg1) != FUNCTION_SUCCESS)
		goto error_status_set_registers;


	// Get bandwidth mode.
	if(pDemod->GetBandwidthMode(pDemod, &BandwidthMode) != FUNCTION_SUCCESS)
		goto error_status_execute_function;


	// Determine criterion answer.
	*pAnswer = 
		(FsmStage == 11) && 

		(Qam  == pExtra->Func1QamBak) &&
		(Hier == pExtra->Func1HierBak) &&
		(LpCr == pExtra->Func1LpCrBak) &&
		(HpCr == pExtra->Func1HpCrBak) &&
		(Gi   == pExtra->Func1GiBak) &&
		(Fft  == pExtra->Func1FftBak) &&

		(Reg0 == 0x1) &&

		((BandwidthMode == DVBT_BANDWIDTH_8MHZ) &&
		 ( ((Fft == DVBT_FFT_MODE_2K) && (Reg1 > 1424) && (Reg1 < 1440)) ||
		   ((Fft == DVBT_FFT_MODE_8K) && (Reg1 > 5696) && (Reg1 < 5760))    ) );


	// Backup TPS information.
	pExtra->Func1QamBak  = Qam;
	pExtra->Func1HierBak = Hier;
	pExtra->Func1LpCrBak = LpCr;
	pExtra->Func1HpCrBak = HpCr;
	pExtra->Func1GiBak   = Gi;
	pExtra->Func1FftBak  = Fft;


	return FUNCTION_SUCCESS;


error_status_set_registers:
error_status_execute_function:
error_status_get_registers:
	return FUNCTION_ERROR;
}





/**

@brief   Accumulate RSD_BER_EST value for Function 1.

One can use rtl2832_func1_AccumulateRsdBerEst() to accumulate RSD_BER_EST for Function 1.


@param [in]   pDemod               The demod module pointer
@param [in]   pAccumulativeValue   Accumulative RSD_BER_EST value


@retval   FUNCTION_SUCCESS   Accumulate RSD_BER_EST for Function 1 successfully.
@retval   FUNCTION_ERROR     Accumulate RSD_BER_EST for Function 1 unsuccessfully.

*/
int
rtl2832_func1_AccumulateRsdBerEst(
	DVBT_DEMOD_MODULE *pDemod,
	unsigned long *pAccumulativeValue
	)
{
	RTL2832_EXTRA_MODULE *pExtra;

	int i;
	unsigned long RsdBerEst;



	// Get demod extra module.
	pExtra = &(pDemod->Extra.Rtl2832);


	// Get RSD_BER_EST with assigned times.
	for(i = 0; i < pExtra->Func1GettingNumEachTime; i++)
	{
		// Get RSD_BER_EST.
		if(pDemod->GetRegBitsWithPage(pDemod, DVBT_RSD_BER_EST, &RsdBerEst) != FUNCTION_SUCCESS)
			goto error_status_get_registers;

		// Accumulate RSD_BER_EST to accumulative value.
		*pAccumulativeValue += RsdBerEst;
	}


	return FUNCTION_SUCCESS;


error_status_get_registers:
	return FUNCTION_ERROR;
}





/**

@brief   Reset registers for Function 1.

One can use rtl2832_func1_ResetReg() to reset registers for Function 1.


@param [in]   pDemod   The demod module pointer


@retval   FUNCTION_SUCCESS   Reset registers for Function 1 successfully.
@retval   FUNCTION_ERROR     Reset registers for Function 1 unsuccessfully.

*/
int
rtl2832_func1_ResetReg(
	DVBT_DEMOD_MODULE *pDemod
	)
{
	// Reset Function 1 registers.
    if(pDemod->SetRegPage(pDemod, 0x1) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

    if(pDemod->SetRegMaskBits(pDemod, 0x65, 2, 0, 0x7) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

    if(pDemod->SetRegMaskBits(pDemod, 0x68, 5, 4, 0x3) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

    if(pDemod->SetRegMaskBits(pDemod, 0x5b, 2, 0, 0x5) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

    if(pDemod->SetRegMaskBits(pDemod, 0x5b, 5, 3, 0x5) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

    if(pDemod->SetRegMaskBits(pDemod, 0x5c, 2, 0, 0x5) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

    if(pDemod->SetRegMaskBits(pDemod, 0x5c, 5, 3, 0x5) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

    if(pDemod->SetRegMaskBits(pDemod, 0xd0, 3, 2, 0x0) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

    if(pDemod->SetRegMaskBits(pDemod, 0xd1, 14, 0, 0x0) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

    if(pDemod->SetRegMaskBits(pDemod, 0xd3, 14, 0, 0x0) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

    if(pDemod->SetRegMaskBits(pDemod, 0xd5, 14, 0, 0x0) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

    if(pDemod->SetRegPage(pDemod, 0x2) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

    if(pDemod->SetRegMaskBits(pDemod, 0x1, 0, 0, 0x1) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

    if(pDemod->SetRegMaskBits(pDemod, 0xb4, 7, 6, 0x3) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

    if(pDemod->SetRegMaskBits(pDemod, 0xd2, 1, 1, 0x0) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

    if(pDemod->SetRegMaskBits(pDemod, 0xb5, 7, 7, 0x1) != FUNCTION_SUCCESS)
		goto error_status_set_registers;


	return FUNCTION_SUCCESS;


error_status_set_registers:
	return FUNCTION_ERROR;
}





/**

@brief   Set common registers for Function 1.

One can use rtl2832_func1_SetCommonReg() to set common registers for Function 1.


@param [in]   pDemod   The demod module pointer


@retval   FUNCTION_SUCCESS   Set common registers for Function 1 successfully.
@retval   FUNCTION_ERROR     Set common registers for Function 1 unsuccessfully.

*/
int
rtl2832_func1_SetCommonReg(
	DVBT_DEMOD_MODULE *pDemod
	)
{
	// Set common registers for Function 1.
    if(pDemod->SetRegPage(pDemod, 0x1) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

    if(pDemod->SetRegMaskBits(pDemod, 0x65, 2, 0, 0x5) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

    if(pDemod->SetRegMaskBits(pDemod, 0x68, 5, 4, 0x0) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

    if(pDemod->SetRegPage(pDemod, 0x2) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

    if(pDemod->SetRegMaskBits(pDemod, 0xd2, 1, 1, 0x1) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

    if(pDemod->SetRegMaskBits(pDemod, 0xb5, 7, 7, 0x0) != FUNCTION_SUCCESS)
		goto error_status_set_registers;


	return FUNCTION_SUCCESS;


error_status_set_registers:
	return FUNCTION_ERROR;
}





/**

@brief   Set registers with FFT mode for Function 1.

One can use rtl2832_func1_SetRegWithConfigMode() to set registers with FFT mode for Function 1.


@param [in]   pDemod    The demod module pointer
@param [in]   FftMode   FFT mode for setting


@retval   FUNCTION_SUCCESS   Set registers with FFT mode for Function 1 successfully.
@retval   FUNCTION_ERROR     Set registers with FFT mode for Function 1 unsuccessfully.

*/
int
rtl2832_func1_SetRegWithFftMode(
	DVBT_DEMOD_MODULE *pDemod,
	int FftMode
	)
{
	typedef struct
	{
		unsigned long Reg0[DVBT_FFT_MODE_NUM];
		unsigned long Reg1[DVBT_FFT_MODE_NUM];
	}
	FFT_REF_ENTRY;



	static const FFT_REF_ENTRY FftRefTable =
	{
		// 2K mode,   8K mode
		{0x0,         0x1    },
		{0x3,         0x0    },
	};



	// Set registers with FFT mode for Function 1.
    if(pDemod->SetRegPage(pDemod, 0x2) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

    if(pDemod->SetRegMaskBits(pDemod, 0x1, 0, 0, FftRefTable.Reg0[FftMode]) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

    if(pDemod->SetRegMaskBits(pDemod, 0xb4, 7, 6, FftRefTable.Reg1[FftMode]) != FUNCTION_SUCCESS)
		goto error_status_set_registers;


	return FUNCTION_SUCCESS;


error_status_set_registers:
	return FUNCTION_ERROR;
}





/**

@brief   Set registers with configuration mode for Function 1.

One can use rtl2832_func1_SetRegWithConfigMode() to set registers with configuration mode for Function 1.


@param [in]   pDemod       The demod module pointer
@param [in]   ConfigMode   Configuration mode for setting


@retval   FUNCTION_SUCCESS   Set registers with configuration mode for Function 1 successfully.
@retval   FUNCTION_ERROR     Set registers with configuration mode for Function 1 unsuccessfully.


@note
	-# This function can not set RTL2832_FUNC1_CONFIG_NORMAL configuration mode.

*/
int
rtl2832_func1_SetRegWithConfigMode(
	DVBT_DEMOD_MODULE *pDemod,
	int ConfigMode
	)
{
	typedef struct
	{
		unsigned long Reg0[RTL2832_FUNC1_CONFIG_MODE_NUM];
		unsigned long Reg1[RTL2832_FUNC1_CONFIG_MODE_NUM];
		unsigned long Reg2[RTL2832_FUNC1_CONFIG_MODE_NUM];
		unsigned long Reg3[RTL2832_FUNC1_CONFIG_MODE_NUM];
		unsigned long Reg4[RTL2832_FUNC1_CONFIG_MODE_NUM];

		unsigned long Reg5Ref[RTL2832_FUNC1_CONFIG_MODE_NUM];
		unsigned long Reg6Ref[RTL2832_FUNC1_CONFIG_MODE_NUM];
		unsigned long Reg7Ref[RTL2832_FUNC1_CONFIG_MODE_NUM];
	}
	CONFIG_REF_ENTRY;



	static const CONFIG_REF_ENTRY ConfigRefTable =
	{
		// Config 1,   Config 2,   Config 3
		{0x5,          0x4,        0x5     },
		{0x5,          0x4,        0x7     },
		{0x5,          0x4,        0x7     },
		{0x7,          0x6,        0x5     },
		{0x3,          0x3,        0x2     },

		{4437,         4437,       4325    },
		{6000,         5500,       6500    },
		{6552,         5800,       5850    },
	};

	int BandwidthMode;

	static const unsigned long Const[DVBT_BANDWIDTH_MODE_NUM] =
	{
		// 6Mhz, 7Mhz, 8Mhz
		48,      56,   64,
	};

	unsigned long Reg5, Reg6, Reg7;



	// Get bandwidth mode.
	if(pDemod->GetBandwidthMode(pDemod, &BandwidthMode) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Calculate REG_5, REG_6, and REG_7 with bandwidth mode and configuration mode.
	Reg5 = (ConfigRefTable.Reg5Ref[ConfigMode] * 7 * 2048 * 8) / (1000 * Const[BandwidthMode]);
	Reg6 = (ConfigRefTable.Reg6Ref[ConfigMode] * 7 * 2048 * 8) / (1000 * Const[BandwidthMode]);
	Reg7 = (ConfigRefTable.Reg7Ref[ConfigMode] * 7 * 2048 * 8) / (1000 * Const[BandwidthMode]);


	// Set registers with bandwidth mode and configuration mode.
    if(pDemod->SetRegPage(pDemod, 0x1) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

    if(pDemod->SetRegMaskBits(pDemod, 0x5b, 2, 0, ConfigRefTable.Reg0[ConfigMode]) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

    if(pDemod->SetRegMaskBits(pDemod, 0x5b, 5, 3, ConfigRefTable.Reg1[ConfigMode]) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

    if(pDemod->SetRegMaskBits(pDemod, 0x5c, 2, 0, ConfigRefTable.Reg2[ConfigMode]) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

    if(pDemod->SetRegMaskBits(pDemod, 0x5c, 5, 3, ConfigRefTable.Reg3[ConfigMode]) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

    if(pDemod->SetRegMaskBits(pDemod, 0xd0, 3, 2, ConfigRefTable.Reg4[ConfigMode]) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

    if(pDemod->SetRegMaskBits(pDemod, 0xd1, 14, 0, Reg5) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

    if(pDemod->SetRegMaskBits(pDemod, 0xd3, 14, 0, Reg6) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

    if(pDemod->SetRegMaskBits(pDemod, 0xd5, 14, 0, Reg7) != FUNCTION_SUCCESS)
		goto error_status_set_registers;


	return FUNCTION_SUCCESS;


error_status_set_registers:
error_status_execute_function:
	return FUNCTION_ERROR;
}





/**

@brief   Get minimum-weighted-BER configuration mode for Function 1.

One can use rtl2832_func1_GetMinWeightedBerConfigMode() to get minimum-weighted-BER configuration mode for Function 1.


@param [in]    pDemod        The demod module pointer
@param [out]   pConfigMode   Pointer to an allocated memory for storing configuration mode answer


@retval   FUNCTION_SUCCESS   Get minimum-weighted-BER configuration mode for Function 1 successfully.
@retval   FUNCTION_ERROR     Get minimum-weighted-BER configuration mode for Function 1 unsuccessfully.

*/
void
rtl2832_func1_GetMinWeightedBerConfigMode(
	DVBT_DEMOD_MODULE *pDemod,
	int *pConfigMode
	)
{
	RTL2832_EXTRA_MODULE *pExtra;

	unsigned long WeightedBerNormal;
	unsigned long WeightedBerConfig1;
	unsigned long WeightedBerConfig2;
	unsigned long WeightedBerConfig3;



	// Get demod extra module.
	pExtra = &(pDemod->Extra.Rtl2832);


	// Calculate weighted BER for all configuration mode
	WeightedBerNormal  = pExtra->Func1RsdBerEstSumNormal * 2;
	WeightedBerConfig1 = pExtra->Func1RsdBerEstSumConfig1;
	WeightedBerConfig2 = pExtra->Func1RsdBerEstSumConfig2;
	WeightedBerConfig3 = pExtra->Func1RsdBerEstSumConfig3;


	// Determine minimum-weighted-BER configuration mode.
	if(WeightedBerNormal <= WeightedBerConfig1 &&
		WeightedBerNormal <= WeightedBerConfig2 &&
		WeightedBerNormal <= WeightedBerConfig3)
	{
		*pConfigMode = RTL2832_FUNC1_CONFIG_NORMAL;
	}
	else if(WeightedBerConfig1 <= WeightedBerNormal &&
		WeightedBerConfig1 <= WeightedBerConfig2 &&
		WeightedBerConfig1 <= WeightedBerConfig3)
	{
		*pConfigMode = RTL2832_FUNC1_CONFIG_1;
	}
	else if(WeightedBerConfig2 <= WeightedBerNormal &&
		WeightedBerConfig2 <= WeightedBerConfig1 &&
		WeightedBerConfig2 <= WeightedBerConfig3)
	{
		*pConfigMode = RTL2832_FUNC1_CONFIG_2;
	}
	else if(WeightedBerConfig3 <= WeightedBerNormal &&
		WeightedBerConfig3 <= WeightedBerConfig1 &&
		WeightedBerConfig3 <= WeightedBerConfig2)
	{
		*pConfigMode = RTL2832_FUNC1_CONFIG_3;
	}


	return;
}














