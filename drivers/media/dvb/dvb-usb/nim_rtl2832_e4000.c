/**

@file

@brief   RTL2832 E4000 NIM module definition

One can manipulate RTL2832 E4000 NIM through RTL2832 E4000 NIM module.
RTL2832 E4000 NIM module is derived from DVB-T NIM module.

*/


#include "nim_rtl2832_e4000.h"





/**

@brief   RTL2832 E4000 NIM module builder

Use BuildRtl2832E4000Module() to build RTL2832 E4000 NIM module, set all module function pointers with the
corresponding functions, and initialize module private variables.


@param [in]   ppNim                        Pointer to RTL2832 E4000 NIM module pointer
@param [in]   pDvbtNimModuleMemory         Pointer to an allocated DVB-T NIM module memory
@param [in]   I2cReadingByteNumMax         Maximum I2C reading byte number for basic I2C reading function
@param [in]   I2cWritingByteNumMax         Maximum I2C writing byte number for basic I2C writing function
@param [in]   I2cRead                      Basic I2C reading function pointer
@param [in]   I2cWrite                     Basic I2C writing function pointer
@param [in]   WaitMs                       Basic waiting function pointer
@param [in]   DemodDeviceAddr              RTL2832 I2C device address
@param [in]   DemodCrystalFreqHz           RTL2832 crystal frequency in Hz
@param [in]   DemodTsInterfaceMode         RTL2832 TS interface mode for setting
@param [in]   DemodAppMode                 RTL2832 application mode for setting
@param [in]   DemodUpdateFuncRefPeriodMs   RTL2832 update function reference period in millisecond for setting
@param [in]   DemodIsFunc1Enabled          RTL2832 Function 1 enabling status for setting
@param [in]   TunerDeviceAddr              E4000 I2C device address
@param [in]   TunerCrystalFreqHz           E4000 crystal frequency in Hz


@note
	-# One should call BuildRtl2832E4000Module() to build RTL2832 E4000 NIM module before using it.

*/
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
	)
{
	DVBT_NIM_MODULE *pNim;
	RTL2832_E4000_EXTRA_MODULE *pNimExtra;



	// Set NIM module pointer with NIM module memory.
	*ppNim = pDvbtNimModuleMemory;
	
	// Get NIM module.
	pNim = *ppNim;

	// Set I2C bridge module pointer with I2C bridge module memory.
	pNim->pI2cBridge = &pNim->I2cBridgeModuleMemory;

	// Get NIM extra module.
	pNimExtra = &(pNim->Extra.Rtl2832E4000);


	// Set NIM type.
	pNim->NimType = DVBT_NIM_RTL2832_E4000;


	// Build base interface module.
	BuildBaseInterface(
		&pNim->pBaseInterface,
		&pNim->BaseInterfaceModuleMemory,
		I2cReadingByteNumMax,
		I2cWritingByteNumMax,
		I2cRead,
		I2cWrite,
		WaitMs
		);

	// Build RTL2832 demod module.
	BuildRtl2832Module(
		&pNim->pDemod,
		&pNim->DvbtDemodModuleMemory,
		&pNim->BaseInterfaceModuleMemory,
		&pNim->I2cBridgeModuleMemory,
		DemodDeviceAddr,
		DemodCrystalFreqHz,
		DemodTsInterfaceMode,
		DemodAppMode,
		DemodUpdateFuncRefPeriodMs,
		DemodIsFunc1Enabled
		);

	// Build E4000 tuner module.
	BuildE4000Module(
		&pNim->pTuner,
		&pNim->TunerModuleMemory,
		&pNim->BaseInterfaceModuleMemory,
		&pNim->I2cBridgeModuleMemory,
		TunerDeviceAddr,
		TunerCrystalFreqHz
		);


	// Set NIM module function pointers with default functions.
	pNim->GetNimType        = dvbt_nim_default_GetNimType;
	pNim->GetParameters     = dvbt_nim_default_GetParameters;
	pNim->IsSignalPresent   = dvbt_nim_default_IsSignalPresent;
	pNim->IsSignalLocked    = dvbt_nim_default_IsSignalLocked;
	pNim->GetSignalStrength = dvbt_nim_default_GetSignalStrength;
	pNim->GetSignalQuality  = dvbt_nim_default_GetSignalQuality;
	pNim->GetBer            = dvbt_nim_default_GetBer;
	pNim->GetSnrDb          = dvbt_nim_default_GetSnrDb;
	pNim->GetTrOffsetPpm    = dvbt_nim_default_GetTrOffsetPpm;
	pNim->GetCrOffsetHz     = dvbt_nim_default_GetCrOffsetHz;
	pNim->GetTpsInfo        = dvbt_nim_default_GetTpsInfo;

	// Set NIM module function pointers with particular functions.
	pNim->Initialize     = rtl2832_e4000_Initialize;
	pNim->SetParameters  = rtl2832_e4000_SetParameters;
	pNim->UpdateFunction = rtl2832_e4000_UpdateFunction;


	// Initialize NIM extra module variables.
	pNimExtra->TunerModeUpdateWaitTimeMax = 
		DivideWithCeiling(RTL2832_E4000_TUNER_MODE_UPDATE_WAIT_TIME_MS, DemodUpdateFuncRefPeriodMs);
	pNimExtra->TunerModeUpdateWaitTime    = 0;
	pNimExtra->TunerGainMode = RTL2832_E4000_TUNER_GAIN_NORMAL;


	return;
}





/**

@see   DVBT_NIM_FP_INITIALIZE

*/
int
rtl2832_e4000_Initialize(
	DVBT_NIM_MODULE *pNim
	)
{
	typedef struct
	{
		int RegBitName;
		unsigned long Value;
	}
	REG_VALUE_ENTRY;


	static const REG_VALUE_ENTRY AdditionalInitRegValueTable[RTL2832_E4000_ADDITIONAL_INIT_REG_TABLE_LEN] =
	{
		// RegBitName,				Value
		{DVBT_DAGC_TRG_VAL,			0x5a	},
		{DVBT_AGC_TARG_VAL_0,		0x0		},
		{DVBT_AGC_TARG_VAL_8_1,		0x5a	},
		{DVBT_AAGC_LOOP_GAIN,		0x18    },
		{DVBT_LOOP_GAIN2_3_0,		0x8		},
		{DVBT_LOOP_GAIN2_4,			0x1		},
		{DVBT_LOOP_GAIN3,			0x18	},

		{DVBT_VTOP1,				0x35	},
		{DVBT_VTOP2,				0x21	},
		{DVBT_VTOP3,				0x21	},
		{DVBT_KRF1,					0x0		},
		{DVBT_KRF2,					0x40	},
		{DVBT_KRF3,					0x10	},
		{DVBT_KRF4,					0x10	},
		{DVBT_IF_AGC_MIN,			0x80	},
		{DVBT_IF_AGC_MAX,			0x7f	},
		{DVBT_RF_AGC_MIN,			0x80	},
		{DVBT_RF_AGC_MAX,			0x7f	},
		{DVBT_POLAR_RF_AGC,			0x0		},
		{DVBT_POLAR_IF_AGC,			0x0		},
		{DVBT_AD7_SETTING,			0xe9d4	},
		{DVBT_EN_GI_PGA,			0x0		},
		{DVBT_THD_LOCK_UP,			0x0		},
		{DVBT_THD_LOCK_DW,			0x0		},
		{DVBT_THD_UP1,				0x14	},
		{DVBT_THD_DW1,				0xec	},

		{DVBT_INTER_CNT_LEN,		0xc		},
		{DVBT_GI_PGA_STATE,			0x0		},
		{DVBT_EN_AGC_PGA,			0x1		},

		{DVBT_REG_GPE,				0x1		},
		{DVBT_REG_GPO,				0x1		},
		{DVBT_REG_MONSEL,			0x1		},
		{DVBT_REG_MON,				0x1		},
		{DVBT_REG_4MSEL,			0x0		},
	};


	TUNER_MODULE *pTuner;
	DVBT_DEMOD_MODULE *pDemod;

	int i;

	int RegBitName;
	unsigned long Value;



	// Get tuner module and demod module.
	pTuner = pNim->pTuner;
	pDemod = pNim->pDemod;


	// Enable demod DVBT_IIC_REPEAT.
	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_IIC_REPEAT, 0x1) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

	// Initialize tuner.
	if(pTuner->Initialize(pTuner) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Disable demod DVBT_IIC_REPEAT.
	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_IIC_REPEAT, 0x0) != FUNCTION_SUCCESS)
		goto error_status_set_registers;


	// Initialize demod.
	if(pDemod->Initialize(pDemod) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Set demod IF frequency with 0 Hz.
	if(pDemod->SetIfFreqHz(pDemod, IF_FREQ_0HZ) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Set demod spectrum mode with SPECTRUM_NORMAL.
	if(pDemod->SetSpectrumMode(pDemod, SPECTRUM_NORMAL) != FUNCTION_SUCCESS)
		goto error_status_execute_function;


	// Set demod registers.
	for(i = 0; i < RTL2832_E4000_ADDITIONAL_INIT_REG_TABLE_LEN; i++)
	{
		// Get register bit name and its value.
		RegBitName = AdditionalInitRegValueTable[i].RegBitName;
		Value      = AdditionalInitRegValueTable[i].Value;

		// Set demod registers
		if(pDemod->SetRegBitsWithPage(pDemod, RegBitName, Value) != FUNCTION_SUCCESS)
			goto error_status_set_registers;
	}


	return FUNCTION_SUCCESS;


error_status_execute_function:
error_status_set_registers:
	return FUNCTION_ERROR;
}


int
rtl2832_e4000_Initialize_fm(
	DVBT_NIM_MODULE *pNim
	)
{
	typedef struct
	{
		int RegBitName;
		unsigned long Value;
	}
	REG_VALUE_ENTRY;


	static const REG_VALUE_ENTRY AdditionalInitRegValueTable[RTL2832_E4000_ADDITIONAL_INIT_REG_TABLE_LEN] =
	{
		// RegBitName,				Value
		{DVBT_DAGC_TRG_VAL,			0x5a	},
		{DVBT_AGC_TARG_VAL_0,		0x0		},
		{DVBT_AGC_TARG_VAL_8_1,		0x5a	},
		{DVBT_AAGC_LOOP_GAIN,		0x18    },
		{DVBT_LOOP_GAIN2_3_0,		0x8		},
		{DVBT_LOOP_GAIN2_4,			0x1		},
		{DVBT_LOOP_GAIN3,			0x18	},

		{DVBT_VTOP1,				0x35	},
		{DVBT_VTOP2,				0x21	},
		{DVBT_VTOP3,				0x21	},
		{DVBT_KRF1,					0x0		},
		{DVBT_KRF2,					0x40	},
		{DVBT_KRF3,					0x10	},
		{DVBT_KRF4,					0x10	},
		{DVBT_IF_AGC_MIN,			0x80	},
		{DVBT_IF_AGC_MAX,			0x7f	},
		{DVBT_RF_AGC_MIN,			0x80	},
		{DVBT_RF_AGC_MAX,			0x7f	},
		{DVBT_POLAR_RF_AGC,			0x0		},
		{DVBT_POLAR_IF_AGC,			0x0		},
		{DVBT_AD7_SETTING,			0xe9d4	},
		{DVBT_EN_GI_PGA,			0x0		},
		{DVBT_THD_LOCK_UP,			0x0		},
		{DVBT_THD_LOCK_DW,			0x0		},
		{DVBT_THD_UP1,				0x14	},
		{DVBT_THD_DW1,				0xec	},

		{DVBT_INTER_CNT_LEN,		0xc		},
		{DVBT_GI_PGA_STATE,			0x0		},
		{DVBT_EN_AGC_PGA,			0x1		},

		{DVBT_REG_GPE,				0x1		},
		{DVBT_REG_GPO,				0x1		},
		{DVBT_REG_MONSEL,			0x1		},
		{DVBT_REG_MON,				0x1		},
		{DVBT_REG_4MSEL,			0x0		},
	};


	TUNER_MODULE *pTuner;
	DVBT_DEMOD_MODULE *pDemod;

	int i;

	int RegBitName;
	unsigned long Value;



	// Get tuner module and demod module.
	pTuner = pNim->pTuner;
	pDemod = pNim->pDemod;


	// Enable demod DVBT_IIC_REPEAT.
	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_IIC_REPEAT, 0x1) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

	// Initialize tuner.
	if(pTuner->Initialize(pTuner) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Disable demod DVBT_IIC_REPEAT.
	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_IIC_REPEAT, 0x0) != FUNCTION_SUCCESS)
		goto error_status_set_registers;


	// Initialize demod.
	//if(pDemod->Initialize(pDemod) != FUNCTION_SUCCESS)
	//	goto error_status_execute_function;

	// Set demod IF frequency with 0 Hz.
	//if(pDemod->SetIfFreqHz(pDemod, IF_FREQ_0HZ) != FUNCTION_SUCCESS)
	//	goto error_status_execute_function;

	// Set demod spectrum mode with SPECTRUM_NORMAL.
	//if(pDemod->SetSpectrumMode(pDemod, SPECTRUM_NORMAL) != FUNCTION_SUCCESS)
	//	goto error_status_execute_function;


	// Set demod registers.
	for(i = 0; i < RTL2832_E4000_ADDITIONAL_INIT_REG_TABLE_LEN; i++)
	{
		// Get register bit name and its value.
		RegBitName = AdditionalInitRegValueTable[i].RegBitName;
		Value      = AdditionalInitRegValueTable[i].Value;

		// Set demod registers
		if(pDemod->SetRegBitsWithPage(pDemod, RegBitName, Value) != FUNCTION_SUCCESS)
			goto error_status_set_registers;
	}




	return FUNCTION_SUCCESS;


error_status_execute_function:
error_status_set_registers:
	return FUNCTION_ERROR;
}






/**

@see   DVBT_NIM_FP_SET_PARAMETERS

*/
int
rtl2832_e4000_SetParameters(
	DVBT_NIM_MODULE *pNim,
	unsigned long RfFreqHz,
	int BandwidthMode
	)
{
	TUNER_MODULE *pTuner;
	DVBT_DEMOD_MODULE *pDemod;
	E4000_EXTRA_MODULE *pTunerExtra;
	RTL2832_E4000_EXTRA_MODULE *pNimExtra;

	unsigned long TunerBandwidthHz;

	int RfFreqKhz;
	int BandwidthKhz;



	// Get tuner module and demod module.
	pTuner = pNim->pTuner;
	pDemod = pNim->pDemod;

	// Get tuner extra module.
	pTunerExtra = &(pTuner->Extra.E4000);

	// Get NIM extra module.
	pNimExtra = &(pNim->Extra.Rtl2832E4000);


	// Enable demod DVBT_IIC_REPEAT.
	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_IIC_REPEAT, 0x1) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

	if(pTuner->Initialize(pTuner) != FUNCTION_SUCCESS)//added by annie
		goto error_status_execute_function;

	// Set tuner RF frequency in Hz.
	if(pTuner->SetRfFreqHz(pTuner, RfFreqHz) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Determine TunerBandwidthHz according to bandwidth mode.
	switch(BandwidthMode)
	{
		default:
		case DVBT_BANDWIDTH_6MHZ:		TunerBandwidthHz = E4000_BANDWIDTH_6000000HZ;		break;
		case DVBT_BANDWIDTH_7MHZ:		TunerBandwidthHz = E4000_BANDWIDTH_7000000HZ;		break;
		case DVBT_BANDWIDTH_8MHZ:		TunerBandwidthHz = E4000_BANDWIDTH_8000000HZ;		break;
	}

	// Set tuner bandwidth Hz with TunerBandwidthHz.
	if(pTunerExtra->SetBandwidthHz(pTuner, TunerBandwidthHz) != FUNCTION_SUCCESS)
		goto error_status_execute_function;


	// Set tuner gain mode with normal condition for update procedure.
	RfFreqKhz = (int)((RfFreqHz + 500) / 1000);
	BandwidthKhz = (int)((TunerBandwidthHz + 500) / 1000);

	if(E4000_nominal(pTuner, RfFreqKhz, BandwidthKhz) != E4000_1_SUCCESS)
//	if(E4000_sensitivity(pTuner, RfFreqKhz, BandwidthKhz) != E4000_1_SUCCESS)
//	if(E4000_linearity(pTuner, RfFreqKhz, BandwidthKhz) != E4000_1_SUCCESS)
		goto error_status_execute_function;

	pNimExtra->TunerGainMode = RTL2832_E4000_TUNER_GAIN_NORMAL;


	// Disable demod DVBT_IIC_REPEAT.
	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_IIC_REPEAT, 0x0) != FUNCTION_SUCCESS)
		goto error_status_set_registers;


	// Set demod bandwidth mode.
	if(pDemod->SetBandwidthMode(pDemod, BandwidthMode) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Reset demod particular registers.
	if(pDemod->ResetFunction(pDemod) != FUNCTION_SUCCESS)
		goto error_status_execute_function;


	// Reset demod by software reset.
	if(pDemod->SoftwareReset(pDemod) != FUNCTION_SUCCESS)
		goto error_status_execute_function;


	return FUNCTION_SUCCESS;


error_status_execute_function:
error_status_set_registers:
	return FUNCTION_ERROR;
}


int
rtl2832_e4000_SetParameters_fm(
	DVBT_NIM_MODULE *pNim,
	unsigned long RfFreqHz,
	int BandwidthMode
	)
{
	TUNER_MODULE *pTuner;
	DVBT_DEMOD_MODULE *pDemod;
	E4000_EXTRA_MODULE *pTunerExtra;
	RTL2832_E4000_EXTRA_MODULE *pNimExtra;

	unsigned long TunerBandwidthHz;

	int RfFreqKhz;
	int BandwidthKhz;



	// Get tuner module and demod module.
	pTuner = pNim->pTuner;
	pDemod = pNim->pDemod;

	// Get tuner extra module.
	pTunerExtra = &(pTuner->Extra.E4000);

	// Get NIM extra module.
	pNimExtra = &(pNim->Extra.Rtl2832E4000);


	// Enable demod DVBT_IIC_REPEAT.
	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_IIC_REPEAT, 0x1) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

	if(pTuner->Initialize(pTuner) != FUNCTION_SUCCESS)//added by annie
		goto error_status_execute_function;

	// Set tuner RF frequency in Hz.
	if(pTuner->SetRfFreqHz(pTuner, RfFreqHz) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Determine TunerBandwidthHz according to bandwidth mode.
	switch(BandwidthMode)
	{
		default:
		case DVBT_BANDWIDTH_6MHZ:		TunerBandwidthHz = E4000_BANDWIDTH_6000000HZ;		break;
		case DVBT_BANDWIDTH_7MHZ:		TunerBandwidthHz = E4000_BANDWIDTH_7000000HZ;		break;
		case DVBT_BANDWIDTH_8MHZ:		TunerBandwidthHz = E4000_BANDWIDTH_8000000HZ;		break;
	}

	// Set tuner bandwidth Hz with TunerBandwidthHz.
	if(pTunerExtra->SetBandwidthHz(pTuner, TunerBandwidthHz) != FUNCTION_SUCCESS)
		goto error_status_execute_function;


	// Set tuner gain mode with normal condition for update procedure.
	RfFreqKhz = (int)((RfFreqHz + 500) / 1000);
	BandwidthKhz = (int)((TunerBandwidthHz + 500) / 1000);

	if(E4000_nominal(pTuner, RfFreqKhz, BandwidthKhz) != E4000_1_SUCCESS)//change by annie
//	if(E4000_sensitivity(pTuner, RfFreqKhz, BandwidthKhz) != E4000_1_SUCCESS)
//	if(E4000_linearity(pTuner, RfFreqKhz, BandwidthKhz) != E4000_1_SUCCESS)
		goto error_status_execute_function;

	pNimExtra->TunerGainMode = RTL2832_E4000_TUNER_GAIN_NORMAL;


	// Disable demod DVBT_IIC_REPEAT.
	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_IIC_REPEAT, 0x0) != FUNCTION_SUCCESS)
		goto error_status_set_registers;


	return FUNCTION_SUCCESS;


error_status_execute_function:
error_status_set_registers:
	return FUNCTION_ERROR;
}


/**

@see   DVBT_NIM_FP_UPDATE_FUNCTION

*/
int
rtl2832_e4000_UpdateFunction(
	DVBT_NIM_MODULE *pNim
	)
{
	DVBT_DEMOD_MODULE *pDemod;
	RTL2832_E4000_EXTRA_MODULE *pNimExtra;


	// Get demod module.
	pDemod = pNim->pDemod;

	// Get NIM extra module.
	pNimExtra = &(pNim->Extra.Rtl2832E4000);


	// Update demod particular registers.
	if(pDemod->UpdateFunction(pDemod) != FUNCTION_SUCCESS)
		goto error_status_execute_function;


	// Increase tuner mode update waiting time.
	pNimExtra->TunerModeUpdateWaitTime += 1;


	// Check if need to update tuner mode according to update waiting time.
	if(pNimExtra->TunerModeUpdateWaitTime == pNimExtra->TunerModeUpdateWaitTimeMax)
	{
		// Reset update waiting time.
		pNimExtra->TunerModeUpdateWaitTime = 0;

		// Enable demod DVBT_IIC_REPEAT.
		if(pDemod->SetRegBitsWithPage(pDemod, DVBT_IIC_REPEAT, 0x1) != FUNCTION_SUCCESS)
			goto error_status_set_registers;

		// Update tuner mode.
		if(rtl2832_e4000_UpdateTunerMode(pNim) != FUNCTION_SUCCESS)
			goto error_status_execute_function;

		// Disable demod DVBT_IIC_REPEAT.
		if(pDemod->SetRegBitsWithPage(pDemod, DVBT_IIC_REPEAT, 0x0) != FUNCTION_SUCCESS)
			goto error_status_set_registers;
	}


	return FUNCTION_SUCCESS;


error_status_set_registers:
error_status_execute_function:
	return FUNCTION_ERROR;
}





/**

@brief   Update tuner mode.

One can use rtl2832_e4000_UpdateTunerMode() to update tuner mode.


@param [in]   pNim   The NIM module pointer


@retval   FUNCTION_SUCCESS   Update tuner mode successfully.
@retval   FUNCTION_ERROR     Update tuner mode unsuccessfully.

*/
int
rtl2832_e4000_UpdateTunerMode(
	DVBT_NIM_MODULE *pNim
	)
{
	static const long LnaGainTable[RTL2832_E4000_LNA_GAIN_TABLE_LEN][RTL2832_E4000_LNA_GAIN_BAND_NUM] =
	{
		// VHF Gain,	UHF Gain,		ReadingByte
		{-50,			-50	},		//	0x0
		{-25,			-25	},		//	0x1
		{-50,			-50	},		//	0x2
		{-25,			-25	},		//	0x3
		{0,				0	},		//	0x4
		{25,			25	},		//	0x5
		{50,			50	},		//	0x6
		{75,			75	},		//	0x7
		{100,			100	},		//	0x8
		{125,			125	},		//	0x9
		{150,			150	},		//	0xa
		{175,			175	},		//	0xb
		{200,			200	},		//	0xc
		{225,			250	},		//	0xd
		{250,			280	},		//	0xe
		{250,			280	},		//	0xf

		// Note: The gain unit is 0.1 dB.
	};

	static const long LnaGainAddTable[RTL2832_E4000_LNA_GAIN_ADD_TABLE_LEN] =
	{
		// Gain,		ReadingByte
		NO_USE,		//	0x0
		NO_USE,		//	0x1
		NO_USE,		//	0x2
		0,			//	0x3
		NO_USE,		//	0x4
		20,			//	0x5
		NO_USE,		//	0x6
		70,			//	0x7

		// Note: The gain unit is 0.1 dB.
	};

	static const long MixerGainTable[RTL2832_E4000_MIXER_GAIN_TABLE_LEN][RTL2832_E4000_MIXER_GAIN_BAND_NUM] =
	{
		// VHF Gain,	UHF Gain,		ReadingByte
		{90,			40	},		//	0x0
		{170,			120	},		//	0x1

		// Note: The gain unit is 0.1 dB.
	};

	static const long IfStage1GainTable[RTL2832_E4000_IF_STAGE_1_GAIN_TABLE_LEN] =
	{
		// Gain,		ReadingByte
		-30,		//	0x0
		60,			//	0x1

		// Note: The gain unit is 0.1 dB.
	};

	static const long IfStage2GainTable[RTL2832_E4000_IF_STAGE_2_GAIN_TABLE_LEN] =
	{
		// Gain,		ReadingByte
		0,			//	0x0
		30,			//	0x1
		60,			//	0x2
		90,			//	0x3

		// Note: The gain unit is 0.1 dB.
	};

	static const long IfStage3GainTable[RTL2832_E4000_IF_STAGE_3_GAIN_TABLE_LEN] =
	{
		// Gain,		ReadingByte
		0,			//	0x0
		30,			//	0x1
		60,			//	0x2
		90,			//	0x3

		// Note: The gain unit is 0.1 dB.
	};

	static const long IfStage4GainTable[RTL2832_E4000_IF_STAGE_4_GAIN_TABLE_LEN] =
	{
		// Gain,		ReadingByte
		0,			//	0x0
		10,			//	0x1
		20,			//	0x2
		20,			//	0x3

		// Note: The gain unit is 0.1 dB.
	};

	static const long IfStage5GainTable[RTL2832_E4000_IF_STAGE_5_GAIN_TABLE_LEN] =
	{
		// Gain,		ReadingByte
		0,			//	0x0
		30,			//	0x1
		60,			//	0x2
		90,			//	0x3
		120,		//	0x4
		120,		//	0x5
		120,		//	0x6
		120,		//	0x7

		// Note: The gain unit is 0.1 dB.
	};

	static const long IfStage6GainTable[RTL2832_E4000_IF_STAGE_6_GAIN_TABLE_LEN] =
	{
		// Gain,		ReadingByte
		0,			//	0x0
		30,			//	0x1
		60,			//	0x2
		90,			//	0x3
		120,		//	0x4
		120,		//	0x5
		120,		//	0x6
		120,		//	0x7

		// Note: The gain unit is 0.1 dB.
	};


	TUNER_MODULE *pTuner;
	E4000_EXTRA_MODULE *pTunerExtra;
	RTL2832_E4000_EXTRA_MODULE *pNimExtra;

	unsigned long RfFreqHz;
	int RfFreqKhz;
	unsigned long BandwidthHz;
	int BandwidthKhz;

	unsigned char ReadingByte;
	int BandIndex;

	unsigned char TunerBitsLna, TunerBitsLnaAdd, TunerBitsMixer;
	unsigned char TunerBitsIfStage1, TunerBitsIfStage2, TunerBitsIfStage3, TunerBitsIfStage4;
	unsigned char TunerBitsIfStage5, TunerBitsIfStage6;

	long TunerGainLna, TunerGainLnaAdd, TunerGainMixer;
	long TunerGainIfStage1, TunerGainIfStage2, TunerGainIfStage3, TunerGainIfStage4;
	long TunerGainIfStage5, TunerGainIfStage6;

	long TunerGainTotal;
	long TunerInputPower;


	// Get tuner module.
	pTuner = pNim->pTuner;

	// Get tuner extra module.
	pTunerExtra = &(pTuner->Extra.E4000);

	// Get NIM extra module.
	pNimExtra = &(pNim->Extra.Rtl2832E4000);


	// Get tuner RF frequency in KHz.
	// Note: RfFreqKhz = round(RfFreqHz / 1000)
	if(pTuner->GetRfFreqHz(pTuner, &RfFreqHz) != FUNCTION_SUCCESS)
		goto error_status_get_tuner_registers;

	RfFreqKhz = (int)((RfFreqHz + 500) / 1000);

	// Get tuner bandwidth in KHz.
	// Note: BandwidthKhz = round(BandwidthHz / 1000)
	if(pTunerExtra->GetBandwidthHz(pTuner, &BandwidthHz) != FUNCTION_SUCCESS)
		goto error_status_get_tuner_registers;

	BandwidthKhz = (int)((BandwidthHz + 500) / 1000);


	// Determine band index.
	BandIndex = (RfFreqHz < RTL2832_E4000_RF_BAND_BOUNDARY_HZ) ? 0 : 1;


	// Get tuner LNA gain according to reading byte and table.
	if(pTunerExtra->GetRegByte(pTuner, RTL2832_E4000_LNA_GAIN_ADDR, &ReadingByte) != FUNCTION_SUCCESS)
		goto error_status_get_tuner_registers;

	TunerBitsLna = (ReadingByte & RTL2832_E4000_LNA_GAIN_MASK) >> RTL2832_E4000_LNA_GAIN_SHIFT;
	TunerGainLna = LnaGainTable[TunerBitsLna][BandIndex];


	// Get tuner LNA additional gain according to reading byte and table.
	if(pTunerExtra->GetRegByte(pTuner, RTL2832_E4000_LNA_GAIN_ADD_ADDR, &ReadingByte) != FUNCTION_SUCCESS)
		goto error_status_get_tuner_registers;

	TunerBitsLnaAdd = (ReadingByte & RTL2832_E4000_LNA_GAIN_ADD_MASK) >> RTL2832_E4000_LNA_GAIN_ADD_SHIFT;
	TunerGainLnaAdd = LnaGainAddTable[TunerBitsLnaAdd];


	// Get tuner mixer gain according to reading byte and table.
	if(pTunerExtra->GetRegByte(pTuner, RTL2832_E4000_MIXER_GAIN_ADDR, &ReadingByte) != FUNCTION_SUCCESS)
		goto error_status_get_tuner_registers;

	TunerBitsMixer = (ReadingByte & RTL2832_E4000_MIXER_GAIN_MASK) >> RTL2832_E4000_LNA_GAIN_ADD_SHIFT;
	TunerGainMixer = MixerGainTable[TunerBitsMixer][BandIndex];


	// Get tuner IF stage 1 gain according to reading byte and table.
	if(pTunerExtra->GetRegByte(pTuner, RTL2832_E4000_IF_STAGE_1_GAIN_ADDR, &ReadingByte) != FUNCTION_SUCCESS)
		goto error_status_get_tuner_registers;

	TunerBitsIfStage1 = (ReadingByte & RTL2832_E4000_IF_STAGE_1_GAIN_MASK) >> RTL2832_E4000_IF_STAGE_1_GAIN_SHIFT;
	TunerGainIfStage1 = IfStage1GainTable[TunerBitsIfStage1];


	// Get tuner IF stage 2 gain according to reading byte and table.
	if(pTunerExtra->GetRegByte(pTuner, RTL2832_E4000_IF_STAGE_2_GAIN_ADDR, &ReadingByte) != FUNCTION_SUCCESS)
		goto error_status_get_tuner_registers;

	TunerBitsIfStage2 = (ReadingByte & RTL2832_E4000_IF_STAGE_2_GAIN_MASK) >> RTL2832_E4000_IF_STAGE_2_GAIN_SHIFT;
	TunerGainIfStage2 = IfStage2GainTable[TunerBitsIfStage2];


	// Get tuner IF stage 3 gain according to reading byte and table.
	if(pTunerExtra->GetRegByte(pTuner, RTL2832_E4000_IF_STAGE_3_GAIN_ADDR, &ReadingByte) != FUNCTION_SUCCESS)
		goto error_status_get_tuner_registers;

	TunerBitsIfStage3 = (ReadingByte & RTL2832_E4000_IF_STAGE_3_GAIN_MASK) >> RTL2832_E4000_IF_STAGE_3_GAIN_SHIFT;
	TunerGainIfStage3 = IfStage3GainTable[TunerBitsIfStage3];


	// Get tuner IF stage 4 gain according to reading byte and table.
	if(pTunerExtra->GetRegByte(pTuner, RTL2832_E4000_IF_STAGE_4_GAIN_ADDR, &ReadingByte) != FUNCTION_SUCCESS)
		goto error_status_get_tuner_registers;

	TunerBitsIfStage4 = (ReadingByte & RTL2832_E4000_IF_STAGE_4_GAIN_MASK) >> RTL2832_E4000_IF_STAGE_4_GAIN_SHIFT;
	TunerGainIfStage4 = IfStage4GainTable[TunerBitsIfStage4];


	// Get tuner IF stage 5 gain according to reading byte and table.
	if(pTunerExtra->GetRegByte(pTuner, RTL2832_E4000_IF_STAGE_5_GAIN_ADDR, &ReadingByte) != FUNCTION_SUCCESS)
		goto error_status_get_tuner_registers;

	TunerBitsIfStage5 = (ReadingByte & RTL2832_E4000_IF_STAGE_5_GAIN_MASK) >> RTL2832_E4000_IF_STAGE_5_GAIN_SHIFT;
	TunerGainIfStage5 = IfStage5GainTable[TunerBitsIfStage5];


	// Get tuner IF stage 6 gain according to reading byte and table.
	if(pTunerExtra->GetRegByte(pTuner, RTL2832_E4000_IF_STAGE_6_GAIN_ADDR, &ReadingByte) != FUNCTION_SUCCESS)
		goto error_status_get_tuner_registers;

	TunerBitsIfStage6 = (ReadingByte & RTL2832_E4000_IF_STAGE_6_GAIN_MASK) >> RTL2832_E4000_IF_STAGE_6_GAIN_SHIFT;
	TunerGainIfStage6 = IfStage6GainTable[TunerBitsIfStage6];


	// Calculate tuner total gain.
	// Note: The unit of tuner total gain is 0.1 dB.
	TunerGainTotal = TunerGainLna + TunerGainLnaAdd + TunerGainMixer + 
	                 TunerGainIfStage1 + TunerGainIfStage2 + TunerGainIfStage3 + TunerGainIfStage4 +
	                 TunerGainIfStage5 + TunerGainIfStage6;

	// Calculate tuner input power.
	// Note: The unit of tuner input power is 0.1 dBm
	TunerInputPower = RTL2832_E4000_TUNER_OUTPUT_POWER_UNIT_0P1_DBM - TunerGainTotal;


	// Determine tuner gain mode according to tuner input power.
	// Note: The unit of tuner input power is 0.1 dBm
	switch(pNimExtra->TunerGainMode)
	{
		default:
		case RTL2832_E4000_TUNER_GAIN_SENSITIVE:

			if(TunerInputPower > -650)
				pNimExtra->TunerGainMode = RTL2832_E4000_TUNER_GAIN_NORMAL;

			break;


		case RTL2832_E4000_TUNER_GAIN_NORMAL:

			if(TunerInputPower < -750)
				pNimExtra->TunerGainMode = RTL2832_E4000_TUNER_GAIN_SENSITIVE;

			if(TunerInputPower > -400)
				pNimExtra->TunerGainMode = RTL2832_E4000_TUNER_GAIN_LINEAR;

			break;


		case RTL2832_E4000_TUNER_GAIN_LINEAR:

			if(TunerInputPower < -500)
				pNimExtra->TunerGainMode = RTL2832_E4000_TUNER_GAIN_NORMAL;

			break;
	}


	// Set tuner gain mode.
	switch(pNimExtra->TunerGainMode)
	{
		default:
		case RTL2832_E4000_TUNER_GAIN_SENSITIVE:

			if(E4000_sensitivity(pTuner, RfFreqKhz, BandwidthKhz) != E4000_1_SUCCESS)
				goto error_status_execute_function;

			break;


		case RTL2832_E4000_TUNER_GAIN_NORMAL:

			if(E4000_nominal(pTuner, RfFreqKhz, BandwidthKhz) != E4000_1_SUCCESS)
				goto error_status_execute_function;

			break;


		case RTL2832_E4000_TUNER_GAIN_LINEAR:

			if(E4000_linearity(pTuner, RfFreqKhz, BandwidthKhz) != E4000_1_SUCCESS)
				goto error_status_execute_function;

			break;
	}


	return FUNCTION_SUCCESS;


error_status_execute_function:
error_status_get_tuner_registers:
	return FUNCTION_ERROR;
}





