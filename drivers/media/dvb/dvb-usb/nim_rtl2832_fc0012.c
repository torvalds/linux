/**

@file

@brief   RTL2832 FC0012 NIM module definition

One can manipulate RTL2832 FC0012 NIM through RTL2832 FC0012 NIM module.
RTL2832 FC0012 NIM module is derived from DVB-T NIM module.

*/


#include "nim_rtl2832_fc0012.h"





/**

@brief   RTL2832 FC0012 NIM module builder

Use BuildRtl2832Fc0012Module() to build RTL2832 FC0012 NIM module, set all module function pointers with the
corresponding functions, and initialize module private variables.


@param [in]   ppNim                        Pointer to RTL2832 FC0012 NIM module pointer
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
@param [in]   TunerDeviceAddr              FC0012 I2C device address
@param [in]   TunerCrystalFreqHz           FC0012 crystal frequency in Hz


@note
	-# One should call BuildRtl2832Fc0012Module() to build RTL2832 FC0012 NIM module before using it.

*/
void
BuildRtl2832Fc0012Module(
	DVBT_NIM_MODULE **ppNim,								// DVB-T NIM dependence
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
	RTL2832_FC0012_EXTRA_MODULE *pNimExtra;



	// Set NIM module pointer with NIM module memory.
	*ppNim = pDvbtNimModuleMemory;
	
	// Get NIM module.
	pNim = *ppNim;

	// Set I2C bridge module pointer with I2C bridge module memory.
	pNim->pI2cBridge = &pNim->I2cBridgeModuleMemory;

	// Get NIM extra module.
	pNimExtra = &(pNim->Extra.Rtl2832Fc0012);


	// Set NIM type.
	pNim->NimType = DVBT_NIM_RTL2832_FC0012;


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

	// Build FC0012 tuner module.
	BuildFc0012Module(
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
	pNim->Initialize     = rtl2832_fc0012_Initialize;
	pNim->SetParameters  = rtl2832_fc0012_SetParameters;
	pNim->UpdateFunction = rtl2832_fc0012_UpdateFunction;


	// Initialize NIM extra module variables.
	pNimExtra->LnaUpdateWaitTimeMax = DivideWithCeiling(RTL2832_FC0012_LNA_UPDATE_WAIT_TIME_MS, DemodUpdateFuncRefPeriodMs);
	pNimExtra->LnaUpdateWaitTime    = 0;


	return;
}





/**

@see   DVBT_NIM_FP_INITIALIZE

*/
int
rtl2832_fc0012_Initialize_fm(
	DVBT_NIM_MODULE *pNim
	)
{
	typedef struct
	{
		int RegBitName;
		unsigned long Value;
	}
	REG_VALUE_ENTRY;


	static const REG_VALUE_ENTRY AdditionalInitRegValueTable[RTL2832_FC0012_DAB_ADDITIONAL_INIT_REG_TABLE_LEN] =
	{
		// RegBitName,				Value
		{DVBT_DAGC_TRG_VAL,			0x5a	},
		{DVBT_AGC_TARG_VAL_0,		0x0		},
		{DVBT_AGC_TARG_VAL_8_1,		0x5a	},
		{DVBT_AAGC_LOOP_GAIN,		0x16    },
		{DVBT_LOOP_GAIN2_3_0,		0x6		},
		{DVBT_LOOP_GAIN2_4,			0x1		},
		{DVBT_LOOP_GAIN3,			0x16	},
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
		{DVBT_AD7_SETTING,			0xe9bf	},
		{DVBT_EN_GI_PGA,			0x0		},
		{DVBT_THD_LOCK_UP,			0x0		},
		{DVBT_THD_LOCK_DW,			0x0		},
		{DVBT_THD_UP1,				0x11	},
		{DVBT_THD_DW1,				0xef	},
		{DVBT_INTER_CNT_LEN,		0xc		},
		{DVBT_GI_PGA_STATE,			0x0		},
		{DVBT_EN_AGC_PGA,			0x1		},

		//test
		{DVBT_AD_EN_REG,			0x1		},
		{DVBT_AD_EN_REG1,			0x1		},
//		{DVBT_EN_BK_TRK,			0x0		},
//		{DVBT_AD_AV_REF,			0x2a	},
//		{DVBT_REG_PI,				0x3		},
		//----------
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

	// Set tuner registers.
	if(fc0012_SetRegMaskBits(pTuner, 0xd, 7, 0, 0x2) != FC0012_I2C_SUCCESS)
		goto error_status_set_registers;

	// Set tuner registers.
	if(fc0012_SetRegMaskBits(pTuner, 0x11, 7, 0, 0x0) != FC0012_I2C_SUCCESS)
		goto error_status_set_registers;

	// Set tuner registers.
	if(fc0012_SetRegMaskBits(pTuner, 0x15, 7, 0, 0x4) != FC0012_I2C_SUCCESS)
		goto error_status_set_registers;

	// Disable demod DVBT_IIC_REPEAT.
	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_IIC_REPEAT, 0x0) != FUNCTION_SUCCESS)
		goto error_status_set_registers;


	// Initialize demod.
	if(pDemod->Initialize(pDemod) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Set demod IF frequency with 0 Hz.
	//if(pDemod->SetIfFreqHz(pDemod, IF_FREQ_0HZ) != FUNCTION_SUCCESS)
	//	goto error_status_execute_function;

	// Set demod spectrum mode with SPECTRUM_NORMAL.
	//if(pDemod->SetSpectrumMode(pDemod, SPECTRUM_NORMAL) != FUNCTION_SUCCESS)
	//	goto error_status_execute_function;


	// Set demod registers.
	for(i = 0; i < RTL2832_FC0012_DAB_ADDITIONAL_INIT_REG_TABLE_LEN; i++)
	{
		// Get register bit name and its value.
		RegBitName = AdditionalInitRegValueTable[i].RegBitName;
		Value      = AdditionalInitRegValueTable[i].Value;

		// Set demod registers
		if(pDemod->SetRegBitsWithPage(pDemod, RegBitName, Value) != FUNCTION_SUCCESS)
			goto error_status_set_registers;
	}


	// Enable demod DVBT_IIC_REPEAT.
	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_IIC_REPEAT, 0x1) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

	// Get tuner RSSI value when calibration is on.
	// Note: Need to execute rtl2832_fc0012_GetTunerRssiCalOn() after demod AD7 is on.
	if(rtl2832_fc0012_GetTunerRssiCalOn(pNim) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Disable demod DVBT_IIC_REPEAT.
	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_IIC_REPEAT, 0x0) != FUNCTION_SUCCESS)
		goto error_status_set_registers;


	return FUNCTION_SUCCESS;


error_status_execute_function:
error_status_set_registers:
	return FUNCTION_ERROR;
}




/**

@see   DVBT_NIM_FP_INITIALIZE

*/
int
rtl2832_fc0012_Initialize(
	DVBT_NIM_MODULE *pNim
	)
{
	typedef struct
	{
		int RegBitName;
		unsigned long Value;
	}
	REG_VALUE_ENTRY;


	static const REG_VALUE_ENTRY AdditionalInitRegValueTable[RTL2832_FC0012_ADDITIONAL_INIT_REG_TABLE_LEN] =
	{
		// RegBitName,				Value
		{DVBT_DAGC_TRG_VAL,			0x5a	},
		{DVBT_AGC_TARG_VAL_0,		0x0		},
		{DVBT_AGC_TARG_VAL_8_1,		0x5a	},
		{DVBT_AAGC_LOOP_GAIN,		0x16    },
		{DVBT_LOOP_GAIN2_3_0,		0x6		},
		{DVBT_LOOP_GAIN2_4,			0x1		},
		{DVBT_LOOP_GAIN3,			0x16	},
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
		{DVBT_AD7_SETTING,			0xe9bf	},
		{DVBT_EN_GI_PGA,			0x0		},
		{DVBT_THD_LOCK_UP,			0x0		},
		{DVBT_THD_LOCK_DW,			0x0		},
		{DVBT_THD_UP1,				0x11	},
		{DVBT_THD_DW1,				0xef	},
		{DVBT_INTER_CNT_LEN,		0xc		},
		{DVBT_GI_PGA_STATE,			0x0		},
		{DVBT_EN_AGC_PGA,			0x1		},
//		{DVBT_REG_GPE,				0x1		},
//		{DVBT_REG_GPO,				0x0		},
//		{DVBT_REG_MONSEL,			0x0		},
//		{DVBT_REG_MON,				0x3		},
//		{DVBT_REG_4MSEL,			0x0		},
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

	// Set tuner registers.
	if(fc0012_SetRegMaskBits(pTuner, 0xd, 7, 0, 0x2) != FC0012_I2C_SUCCESS)
		goto error_status_set_registers;

	// Set tuner registers.
	if(fc0012_SetRegMaskBits(pTuner, 0x11, 7, 0, 0x0) != FC0012_I2C_SUCCESS)
		goto error_status_set_registers;

	// Set tuner registers.
	if(fc0012_SetRegMaskBits(pTuner, 0x15, 7, 0, 0x4) != FC0012_I2C_SUCCESS)
		goto error_status_set_registers;

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
	for(i = 0; i < RTL2832_FC0012_ADDITIONAL_INIT_REG_TABLE_LEN; i++)
	{
		// Get register bit name and its value.
		RegBitName = AdditionalInitRegValueTable[i].RegBitName;
		Value      = AdditionalInitRegValueTable[i].Value;

		// Set demod registers
		if(pDemod->SetRegBitsWithPage(pDemod, RegBitName, Value) != FUNCTION_SUCCESS)
			goto error_status_set_registers;
	}


	// Enable demod DVBT_IIC_REPEAT.
	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_IIC_REPEAT, 0x1) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

	// Get tuner RSSI value when calibration is on.
	// Note: Need to execute rtl2832_fc0012_GetTunerRssiCalOn() after demod AD7 is on.
	if(rtl2832_fc0012_GetTunerRssiCalOn(pNim) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Disable demod DVBT_IIC_REPEAT.
	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_IIC_REPEAT, 0x0) != FUNCTION_SUCCESS)
		goto error_status_set_registers;


	return FUNCTION_SUCCESS;


error_status_execute_function:
error_status_set_registers:
	return FUNCTION_ERROR;
}


/**

@see   DVBT_NIM_FP_SET_PARAMETERS

*/
int
rtl2832_fc0012_SetParameters(
	DVBT_NIM_MODULE *pNim,
	unsigned long RfFreqHz,
	int BandwidthMode
	)
{
	TUNER_MODULE *pTuner;
	DVBT_DEMOD_MODULE *pDemod;

	FC0012_EXTRA_MODULE *pTunerExtra;
	int TunerBandwidthMode;



	// Get tuner module and demod module.
	pTuner = pNim->pTuner;
	pDemod = pNim->pDemod;

	// Get tuner extra module.
	pTunerExtra = &(pTuner->Extra.Fc0012);


	// Enable demod DVBT_IIC_REPEAT.
	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_IIC_REPEAT, 0x1) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

	// Set tuner RF frequency in Hz.
	if(pTuner->SetRfFreqHz(pTuner, RfFreqHz) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Determine TunerBandwidthMode according to bandwidth mode.
	switch(BandwidthMode)
	{
		default:
		case DVBT_BANDWIDTH_6MHZ:		TunerBandwidthMode = FC0012_BANDWIDTH_6000000HZ;		break;
		case DVBT_BANDWIDTH_7MHZ:		TunerBandwidthMode = FC0012_BANDWIDTH_7000000HZ;		break;
		case DVBT_BANDWIDTH_8MHZ:		TunerBandwidthMode = FC0012_BANDWIDTH_8000000HZ;		break;
	}

	// Set tuner bandwidth mode with TunerBandwidthMode.
	if(pTunerExtra->SetBandwidthMode(pTuner, TunerBandwidthMode) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

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


/**

@see   DVBT_NIM_FP_SET_PARAMETERS

*/
int
rtl2832_fc0012_SetParameters_fm(
	DVBT_NIM_MODULE *pNim,
	unsigned long RfFreqHz,
	int BandwidthMode
	)
{
	TUNER_MODULE *pTuner;
	DVBT_DEMOD_MODULE *pDemod;

	FC0012_EXTRA_MODULE *pTunerExtra;
	int TunerBandwidthMode;



	// Get tuner module and demod module.
	pTuner = pNim->pTuner;
	pDemod = pNim->pDemod;

	// Get tuner extra module.
	pTunerExtra = &(pTuner->Extra.Fc0012);


	// Enable demod DVBT_IIC_REPEAT.
	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_IIC_REPEAT, 0x1) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

	// Set tuner RF frequency in Hz.
	if(pTuner->SetRfFreqHz(pTuner, RfFreqHz) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Determine TunerBandwidthMode according to bandwidth mode.
	switch(BandwidthMode)
	{
		default:
		case DVBT_BANDWIDTH_6MHZ:		TunerBandwidthMode = FC0012_BANDWIDTH_6000000HZ;		break;
		case DVBT_BANDWIDTH_7MHZ:		TunerBandwidthMode = FC0012_BANDWIDTH_7000000HZ;		break;
		case DVBT_BANDWIDTH_8MHZ:		TunerBandwidthMode = FC0012_BANDWIDTH_8000000HZ;		break;
	}

	// Set tuner bandwidth mode with TunerBandwidthMode.
	if(pTunerExtra->SetBandwidthMode(pTuner, TunerBandwidthMode) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

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
rtl2832_fc0012_UpdateFunction(
	DVBT_NIM_MODULE *pNim
	)
{
	DVBT_DEMOD_MODULE *pDemod;
	RTL2832_FC0012_EXTRA_MODULE *pNimExtra;


	// Get demod module.
	pDemod = pNim->pDemod;

	// Get NIM extra module.
	pNimExtra = &(pNim->Extra.Rtl2832Fc0012);


	// Update demod particular registers.
	if(pDemod->UpdateFunction(pDemod) != FUNCTION_SUCCESS)
		goto error_status_execute_function;


	// Increase tuner LNA_GAIN update waiting time.
	pNimExtra->LnaUpdateWaitTime += 1;


	// Check if need to update tuner LNA_GAIN according to update waiting time.
	if(pNimExtra->LnaUpdateWaitTime == pNimExtra->LnaUpdateWaitTimeMax)
	{
		// Reset update waiting time.
		pNimExtra->LnaUpdateWaitTime = 0;

		// Enable demod DVBT_IIC_REPEAT.
		if(pDemod->SetRegBitsWithPage(pDemod, DVBT_IIC_REPEAT, 0x1) != FUNCTION_SUCCESS)
			goto error_status_set_registers;

		// Update tuner LNA gain with RSSI.
		if(rtl2832_fc0012_UpdateTunerLnaGainWithRssi(pNim) != FUNCTION_SUCCESS)
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

@brief   Get tuner RSSI value when calibration is on.

One can use rtl2832_fc0012_GetTunerRssiCalOn() to get tuner calibration-on RSSI value.


@param [in]   pNim   The NIM module pointer


@retval   FUNCTION_SUCCESS   Get tuner calibration-on RSSI value successfully.
@retval   FUNCTION_ERROR     Get tuner calibration-on RSSI value unsuccessfully.

*/
int
rtl2832_fc0012_GetTunerRssiCalOn(
	DVBT_NIM_MODULE *pNim
	)
{
	TUNER_MODULE *pTuner;
	DVBT_DEMOD_MODULE *pDemod;
	FC0012_EXTRA_MODULE *pTunerExtra;
	RTL2832_FC0012_EXTRA_MODULE *pNimExtra;



	// Get tuner module and demod module.
	pTuner = pNim->pTuner;
	pDemod = pNim->pDemod;

	// Get tuner extra module.
	pTunerExtra = &(pTuner->Extra.Fc0012);

	// Get NIM extra module.
	pNimExtra = &(pNim->Extra.Rtl2832Fc0012);


	// Set tuner EN_CAL_RSSI to 0x1.
	if(fc0012_SetRegMaskBits(pTuner, 0x9, 4, 4, 0x1) != FC0012_I2C_SUCCESS)
		goto error_status_set_registers;

	// Set tuner LNA_POWER_DOWN to 0x1.
	if(fc0012_SetRegMaskBits(pTuner, 0x6, 0, 0, 0x1) != FC0012_I2C_SUCCESS)
		goto error_status_set_registers;


	// Get demod RSSI_R when tuner RSSI calibration is on.
	if(pDemod->GetRegBitsWithPage(pDemod, DVBT_RSSI_R, &(pNimExtra->RssiRCalOn)) != FUNCTION_SUCCESS)
		goto error_status_get_registers;


	// Set tuner EN_CAL_RSSI to 0x0.
	if(fc0012_SetRegMaskBits(pTuner, 0x9, 4, 4, 0x0) != FC0012_I2C_SUCCESS)
		goto error_status_set_registers;

	// Set tuner LNA_POWER_DOWN to 0x0.
	if(fc0012_SetRegMaskBits(pTuner, 0x6, 0, 0, 0x0) != FC0012_I2C_SUCCESS)
		goto error_status_set_registers;


	return FUNCTION_SUCCESS;


error_status_get_registers:
error_status_set_registers:
	return FUNCTION_ERROR;
}





/**

@brief   Update tuner LNA_GAIN with RSSI.

One can use rtl2832_fc0012_UpdateTunerLnaGainWithRssi() to update tuner LNA_GAIN with RSSI.


@param [in]   pNim   The NIM module pointer


@retval   FUNCTION_SUCCESS   Update tuner LNA_GAIN with RSSI successfully.
@retval   FUNCTION_ERROR     Update tuner LNA_GAIN with RSSI unsuccessfully.

*/
int
rtl2832_fc0012_UpdateTunerLnaGainWithRssi(
	DVBT_NIM_MODULE *pNim
	)
{
	TUNER_MODULE *pTuner;
	DVBT_DEMOD_MODULE *pDemod;
	FC0012_EXTRA_MODULE *pTunerExtra;
	RTL2832_FC0012_EXTRA_MODULE *pNimExtra;

	unsigned long RssiRCalOff;
	long RssiRDiff;
	unsigned char LnaGain;



	// Get tuner module and demod module.
	pTuner = pNim->pTuner;
	pDemod = pNim->pDemod;

	// Get tuner extra module.
	pTunerExtra = &(pTuner->Extra.Fc0012);

	// Get NIM extra module.
	pNimExtra = &(pNim->Extra.Rtl2832Fc0012);


	// Get demod RSSI_R when tuner RSSI calibration in off.
	// Note: Tuner EN_CAL_RSSI and LNA_POWER_DOWN are set to 0x0 after rtl2832_fc0012_GetTunerRssiCalOn() executing.
	if(pDemod->GetRegBitsWithPage(pDemod, DVBT_RSSI_R, &RssiRCalOff) != FUNCTION_SUCCESS)
		goto error_status_get_registers;


	// Calculate RSSI_R difference.
	RssiRDiff = RssiRCalOff - pNimExtra->RssiRCalOn;

	// Get tuner LNA_GAIN.
	if(fc0012_GetRegMaskBits(pTuner, 0x13, 4, 3, &LnaGain) != FC0012_I2C_SUCCESS)
		goto error_status_get_registers;


	deb_info("%s: current LnaGain = %d\n", __FUNCTION__, LnaGain);

	// Determine next LNA_GAIN according to RSSI_R difference and current LNA_GAIN.
	switch(LnaGain)
	{
		default:
		case FC0012_LNA_GAIN_LOW:

			if(RssiRDiff <= 0)
				LnaGain = FC0012_LNA_GAIN_MIDDLE;

			break;


		case FC0012_LNA_GAIN_MIDDLE:

			if(RssiRDiff >= 34)
				LnaGain = FC0012_LNA_GAIN_LOW;

			if(RssiRDiff <= 0)
				LnaGain = FC0012_LNA_GAIN_HIGH;

			break;


		case FC0012_LNA_GAIN_HIGH:

			if(RssiRDiff >= 8)
				LnaGain = FC0012_LNA_GAIN_MIDDLE;

			break;
	}


	deb_info("%s: next LnaGain = %d\n", __FUNCTION__, LnaGain);

      
	// Set tuner LNA_GAIN.
	if(fc0012_SetRegMaskBits(pTuner, 0x13, 4, 3, LnaGain) != FC0012_I2C_SUCCESS)
		goto error_status_set_registers;


	return FUNCTION_SUCCESS;


error_status_get_registers:
error_status_set_registers:
	return FUNCTION_ERROR;
}



