/**

@file

@brief   RTL2832 FC0013 NIM module definition

One can manipulate RTL2832 FC0013 NIM through RTL2832 FC0013 NIM module.
RTL2832 FC0013 NIM module is derived from DVB-T NIM module.

*/


#include "nim_rtl2832_fc0013.h"





/**

@brief   RTL2832 FC0013 NIM module builder

Use BuildRtl2832Fc0013Module() to build RTL2832 FC0013 NIM module, set all module function pointers with the
corresponding functions, and initialize module private variables.


@param [in]   ppNim                        Pointer to RTL2832 FC0013 NIM module pointer
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
@param [in]   TunerDeviceAddr              FC0013 I2C device address
@param [in]   TunerCrystalFreqHz           FC0013 crystal frequency in Hz


@note
	-# One should call BuildRtl2832Fc0013Module() to build RTL2832 FC0013 NIM module before using it.

*/
void
BuildRtl2832Fc0013Module(
	DVBT_NIM_MODULE **ppNim,							// DVB-T NIM dependence
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
	RTL2832_FC0013_EXTRA_MODULE *pNimExtra;



	// Set NIM module pointer with NIM module memory.
	*ppNim = pDvbtNimModuleMemory;
	
	// Get NIM module.
	pNim = *ppNim;

	// Set I2C bridge module pointer with I2C bridge module memory.
	pNim->pI2cBridge = &pNim->I2cBridgeModuleMemory;

	// Get NIM extra module.
	pNimExtra = &(pNim->Extra.Rtl2832Fc0013);


	// Set NIM type.
	pNim->NimType = DVBT_NIM_RTL2832_FC0013;


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

	// Build FC0013 tuner module.
	BuildFc0013Module(
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
	pNim->Initialize     = rtl2832_fc0013_Initialize;
	pNim->SetParameters  = rtl2832_fc0013_SetParameters;
	pNim->UpdateFunction = rtl2832_fc0013_UpdateFunction;


	// Initialize NIM extra module variables.
	pNimExtra->LnaUpdateWaitTimeMax = DivideWithCeiling(RTL2832_FC0013_LNA_UPDATE_WAIT_TIME_MS, DemodUpdateFuncRefPeriodMs);
	pNimExtra->LnaUpdateWaitTime    = 0;


	return;
}





/**

@see   DVBT_NIM_FP_INITIALIZE

*/
int
rtl2832_fc0013_Initialize(
	DVBT_NIM_MODULE *pNim
	)
{
	typedef struct
	{
		int RegBitName;
		unsigned long Value;
	}
	REG_VALUE_ENTRY;


	static const REG_VALUE_ENTRY AdditionalInitRegValueTable[RTL2832_FC0013_ADDITIONAL_INIT_REG_TABLE_LEN] =
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

	// Set FC0013 up-dowm AGC.
	//(0xFE for master of dual).
	//(0xFC for slave of dual, and for 2832 mini dongle).
	if(fc0013_SetRegMaskBits(pTuner, 0x0c, 7, 0, 0xFC) != FC0013_I2C_SUCCESS)
		goto error_status_set_tuner_registers;

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
	for(i = 0; i < RTL2832_FC0013_ADDITIONAL_INIT_REG_TABLE_LEN; i++)
	{
		// Get register bit name and its value.
		RegBitName = AdditionalInitRegValueTable[i].RegBitName;
		Value      = AdditionalInitRegValueTable[i].Value;

		// Set demod registers
		if(pDemod->SetRegBitsWithPage(pDemod, RegBitName, Value) != FUNCTION_SUCCESS)
			goto error_status_set_registers;
	}

	// Reset demod by software reset.
	if(pDemod->SoftwareReset(pDemod) != FUNCTION_SUCCESS)
		goto error_status_execute_function;


	// Enable demod DVBT_IIC_REPEAT.
	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_IIC_REPEAT, 0x1) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

	// Get tuner RSSI value when calibration is on.
	// Note: Need to execute rtl2832_fc0013_GetTunerRssiCalOn() after demod AD7 is on.
	if(rtl2832_fc0013_GetTunerRssiCalOn(pNim) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Disable demod DVBT_IIC_REPEAT.
	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_IIC_REPEAT, 0x0) != FUNCTION_SUCCESS)
		goto error_status_set_registers;


	return FUNCTION_SUCCESS;


error_status_execute_function:
error_status_set_registers:
error_status_set_tuner_registers:
	return FUNCTION_ERROR;
}




/**

@see   DVBT_NIM_FP_INITIALIZE

*/
int
rtl2832_fc0013_Initialize_fm(
	DVBT_NIM_MODULE *pNim
	)
{
	typedef struct
	{
		int RegBitName;
		unsigned long Value;
	}
	REG_VALUE_ENTRY;


	static const REG_VALUE_ENTRY AdditionalInitRegValueTable[RTL2832_FC0013_DAB_ADDITIONAL_INIT_REG_TABLE_LEN] =
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

	// Set FC0013 up-dowm AGC.
	//(0xFE for master of dual).
	//(0xFC for slave of dual, and for 2832 mini dongle).
	if(fc0013_SetRegMaskBits(pTuner, 0x0c, 7, 0, 0xFC) != FC0013_I2C_SUCCESS)
		goto error_status_set_tuner_registers;

	// Disable demod DVBT_IIC_REPEAT.
	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_IIC_REPEAT, 0x0) != FUNCTION_SUCCESS)
		goto error_status_set_registers;


	// Initialize demod.
	//if(pDemod->Initialize(pDemod) != FUNCTION_SUCCESS)
		//goto error_status_execute_function;

	// Set demod IF frequency with 0 Hz.
	//if(pDemod->SetIfFreqHz(pDemod, IF_FREQ_0HZ) != FUNCTION_SUCCESS)
	//	goto error_status_execute_function;

	// Set demod spectrum mode with SPECTRUM_NORMAL.
	//if(pDemod->SetSpectrumMode(pDemod, SPECTRUM_NORMAL) != FUNCTION_SUCCESS)
	//	goto error_status_execute_function;


	// Set demod registers.
	for(i = 0; i < RTL2832_FC0013_DAB_ADDITIONAL_INIT_REG_TABLE_LEN; i++)
	{
		// Get register bit name and its value.
		RegBitName = AdditionalInitRegValueTable[i].RegBitName;
		Value      = AdditionalInitRegValueTable[i].Value;

		// Set demod registers
		if(pDemod->SetRegBitsWithPage(pDemod, RegBitName, Value) != FUNCTION_SUCCESS)
			goto error_status_set_registers;
	}

	// Reset demod by software reset.
	if(pDemod->SoftwareReset(pDemod) != FUNCTION_SUCCESS)
		goto error_status_execute_function;


	// Enable demod DVBT_IIC_REPEAT.
	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_IIC_REPEAT, 0x1) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

	// Get tuner RSSI value when calibration is on.
	// Note: Need to execute rtl2832_fc0013_GetTunerRssiCalOn() after demod AD7 is on.
	if(rtl2832_fc0013_GetTunerRssiCalOn(pNim) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Disable demod DVBT_IIC_REPEAT.
	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_IIC_REPEAT, 0x0) != FUNCTION_SUCCESS)
		goto error_status_set_registers;


	return FUNCTION_SUCCESS;


error_status_execute_function:
error_status_set_registers:
error_status_set_tuner_registers:
	return FUNCTION_ERROR;
}




/**

@see   DVBT_NIM_FP_SET_PARAMETERS

*/
int
rtl2832_fc0013_SetParameters(
	DVBT_NIM_MODULE *pNim,
	unsigned long RfFreqHz,
	int BandwidthMode
	)
{
	TUNER_MODULE *pTuner;
	DVBT_DEMOD_MODULE *pDemod;

	FC0013_EXTRA_MODULE *pTunerExtra;
	int TunerBandwidthMode;



	// Get tuner module and demod module.
	pTuner = pNim->pTuner;
	pDemod = pNim->pDemod;

	// Get tuner extra module.
	pTunerExtra = &(pTuner->Extra.Fc0013);


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
		case DVBT_BANDWIDTH_6MHZ:		TunerBandwidthMode = FC0013_BANDWIDTH_6000000HZ;		break;
		case DVBT_BANDWIDTH_7MHZ:		TunerBandwidthMode = FC0013_BANDWIDTH_7000000HZ;		break;
		case DVBT_BANDWIDTH_8MHZ:		TunerBandwidthMode = FC0013_BANDWIDTH_8000000HZ;		break;
	}

	// Set tuner bandwidth mode with TunerBandwidthMode.
	if(pTunerExtra->SetBandwidthMode(pTuner, TunerBandwidthMode) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Reset tuner IQ LPF BW.
	if(pTunerExtra->RcCalReset(pTuner) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Set tuner IQ LPF BW, val=-2 for D-book ACI test.
	if(pTunerExtra->RcCalAdd(pTuner, -2) != FUNCTION_SUCCESS)
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
rtl2832_fc0013_SetParameters_fm(
	DVBT_NIM_MODULE *pNim,
	unsigned long RfFreqHz,
	int BandwidthMode
	)
{
	TUNER_MODULE *pTuner;
	DVBT_DEMOD_MODULE *pDemod;

	FC0013_EXTRA_MODULE *pTunerExtra;
	int TunerBandwidthMode;



	// Get tuner module and demod module.
	pTuner = pNim->pTuner;
	pDemod = pNim->pDemod;

	// Get tuner extra module.
	pTunerExtra = &(pTuner->Extra.Fc0013);


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
		case DVBT_BANDWIDTH_6MHZ:		TunerBandwidthMode = FC0013_BANDWIDTH_6000000HZ;		break;
		case DVBT_BANDWIDTH_7MHZ:		TunerBandwidthMode = FC0013_BANDWIDTH_7000000HZ;		break;
		case DVBT_BANDWIDTH_8MHZ:		TunerBandwidthMode = FC0013_BANDWIDTH_8000000HZ;		break;
	}

	// Set tuner bandwidth mode with TunerBandwidthMode.
	if(pTunerExtra->SetBandwidthMode(pTuner, TunerBandwidthMode) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Reset tuner IQ LPF BW.
	if(pTunerExtra->RcCalReset(pTuner) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Set tuner IQ LPF BW, val=-2 for D-book ACI test.
	if(pTunerExtra->RcCalAdd(pTuner, -2) != FUNCTION_SUCCESS)
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

@see   DVBT_NIM_FP_UPDATE_FUNCTION

*/
int
rtl2832_fc0013_UpdateFunction(
	DVBT_NIM_MODULE *pNim
	)
{
	DVBT_DEMOD_MODULE *pDemod;
	RTL2832_FC0013_EXTRA_MODULE *pNimExtra;


	// Get demod module.
	pDemod = pNim->pDemod;

	// Get NIM extra module.
	pNimExtra = &(pNim->Extra.Rtl2832Fc0013);


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
		if(rtl2832_fc0013_UpdateTunerLnaGainWithRssi(pNim) != FUNCTION_SUCCESS)
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

One can use rtl2832_fc0013_GetTunerRssiCalOn() to get tuner calibration-on RSSI value.


@param [in]   pNim   The NIM module pointer


@retval   FUNCTION_SUCCESS   Get tuner calibration-on RSSI value successfully.
@retval   FUNCTION_ERROR     Get tuner calibration-on RSSI value unsuccessfully.

*/
int
rtl2832_fc0013_GetTunerRssiCalOn(
	DVBT_NIM_MODULE *pNim
	)
{
	TUNER_MODULE *pTuner;
	DVBT_DEMOD_MODULE *pDemod;
	FC0013_EXTRA_MODULE *pTunerExtra;
	RTL2832_FC0013_EXTRA_MODULE *pNimExtra;
	BASE_INTERFACE_MODULE *pBaseInterface;



	// Get tuner module and demod module.
	pTuner = pNim->pTuner;
	pDemod = pNim->pDemod;

	// Get tuner extra module.
	pTunerExtra = &(pTuner->Extra.Fc0013);

	// Get NIM extra module.
	pNimExtra = &(pNim->Extra.Rtl2832Fc0013);

	// Get NIM base interface.
	pBaseInterface = pNim->pBaseInterface;


	// Set tuner EN_CAL_RSSI to 0x1.
	if(fc0013_SetRegMaskBits(pTuner, 0x9, 4, 4, 0x1) != FC0013_I2C_SUCCESS)
		goto error_status_set_registers;

	// Set tuner LNA_POWER_DOWN to 0x1.
	if(fc0013_SetRegMaskBits(pTuner, 0x6, 0, 0, 0x1) != FC0013_I2C_SUCCESS)
		goto error_status_set_registers;

	// Wait 100 ms.
	pBaseInterface->WaitMs(pBaseInterface, 100);

	// Get demod RSSI_R when tuner RSSI calibration is on.
	if(pDemod->GetRegBitsWithPage(pDemod, DVBT_RSSI_R, &(pNimExtra->RssiRCalOn)) != FUNCTION_SUCCESS)
		goto error_status_get_registers;


	// Set tuner EN_CAL_RSSI to 0x0.
	if(fc0013_SetRegMaskBits(pTuner, 0x9, 4, 4, 0x0) != FC0013_I2C_SUCCESS)
		goto error_status_set_registers;

	// Set tuner LNA_POWER_DOWN to 0x0.
	if(fc0013_SetRegMaskBits(pTuner, 0x6, 0, 0, 0x0) != FC0013_I2C_SUCCESS)
		goto error_status_set_registers;


	return FUNCTION_SUCCESS;


error_status_get_registers:
error_status_set_registers:
	return FUNCTION_ERROR;
}





/**

@brief   Update tuner LNA_GAIN with RSSI.

One can use rtl2832_fc0013_UpdateTunerLnaGainWithRssi() to update tuner LNA_GAIN with RSSI.


@param [in]   pNim   The NIM module pointer


@retval   FUNCTION_SUCCESS   Update tuner LNA_GAIN with RSSI successfully.
@retval   FUNCTION_ERROR     Update tuner LNA_GAIN with RSSI unsuccessfully.

*/
int
rtl2832_fc0013_UpdateTunerLnaGainWithRssi(
	DVBT_NIM_MODULE *pNim
	)
{
	TUNER_MODULE *pTuner;
	DVBT_DEMOD_MODULE *pDemod;
	FC0013_EXTRA_MODULE *pTunerExtra;
	RTL2832_FC0013_EXTRA_MODULE *pNimExtra;

	unsigned long RssiRCalOff;
	long RssiRDiff;
	unsigned char LnaGain;
	unsigned char ReadValue;

	// added from Fitipower, 2011-2-23, v0.8
	int boolVhfFlag;      // 0:false,  1:true
	int boolEnInChgFlag;  // 0:false,  1:true
	int intGainShift;



	// Get tuner module and demod module.
	pTuner = pNim->pTuner;
	pDemod = pNim->pDemod;

	// Get tuner extra module.
	pTunerExtra = &(pTuner->Extra.Fc0013);

	// Get NIM extra module.
	pNimExtra = &(pNim->Extra.Rtl2832Fc0013);


	// Get demod RSSI_R when tuner RSSI calibration in off.
	// Note: Tuner EN_CAL_RSSI and LNA_POWER_DOWN are set to 0x0 after rtl2832_fc0013_GetTunerRssiCalOn() executing.
	if(pDemod->GetRegBitsWithPage(pDemod, DVBT_RSSI_R, &RssiRCalOff) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	// To avoid the wrong rssi calibration value in the environment with strong RF pulse signal.
	if(RssiRCalOff < pNimExtra->RssiRCalOn)
		pNimExtra->RssiRCalOn = RssiRCalOff;


	// Calculate RSSI_R difference.
	RssiRDiff = RssiRCalOff - pNimExtra->RssiRCalOn;

	// Get tuner LNA_GAIN.
	if(fc0013_GetRegMaskBits(pTuner, 0x14, 4, 0, &LnaGain) != FC0013_I2C_SUCCESS)
		goto error_status_get_registers;


	// Determine next LNA_GAIN according to RSSI_R difference and current LNA_GAIN.
	switch(LnaGain)
	{
		default:

			boolVhfFlag = 0;		
			boolEnInChgFlag = 1;
			intGainShift = 10;
			LnaGain = FC0013_LNA_GAIN_HIGH_19;

			// Set tuner LNA_GAIN.
			if(fc0013_SetRegMaskBits(pTuner, 0x14, 4, 0, LnaGain) != FC0013_I2C_SUCCESS)
				goto error_status_set_registers;

			break;


		case FC0013_LNA_GAIN_HIGH_19:

			if(RssiRDiff >= 10)
			{
				boolVhfFlag = 1;		
				boolEnInChgFlag = 0;
				intGainShift = 10;
				LnaGain = FC0013_LNA_GAIN_HIGH_17;

				// Set tuner LNA_GAIN.
				if(fc0013_SetRegMaskBits(pTuner, 0x14, 4, 0, LnaGain) != FC0013_I2C_SUCCESS)
					goto error_status_set_registers;

				break;
			}
			else
			{
				goto success_status_Lna_Gain_No_Change;
			}


		case FC0013_LNA_GAIN_HIGH_17:
			
			if(RssiRDiff <= 2)
			{
				boolVhfFlag = 0;	
				boolEnInChgFlag = 1;
				intGainShift = 10;
				LnaGain = FC0013_LNA_GAIN_HIGH_19;

				// Set tuner LNA_GAIN.
				if(fc0013_SetRegMaskBits(pTuner, 0x14, 4, 0, LnaGain) != FC0013_I2C_SUCCESS)
					goto error_status_set_registers;

				break;
			}

			else if(RssiRDiff >= 24)
			{
				boolVhfFlag = 0;	
				boolEnInChgFlag = 0;
				intGainShift = 7;
				LnaGain = FC0013_LNA_GAIN_MIDDLE;

				// Set tuner LNA_GAIN.
				if(fc0013_SetRegMaskBits(pTuner, 0x14, 4, 0, LnaGain) != FC0013_I2C_SUCCESS)
					goto error_status_set_registers;

				break;
			}

			else
			{
				goto success_status_Lna_Gain_No_Change;
			}


		case FC0013_LNA_GAIN_MIDDLE:

			if(RssiRDiff >= 38)
			{
				boolVhfFlag = 0;	
				boolEnInChgFlag = 0;
				intGainShift = 7;
				LnaGain = FC0013_LNA_GAIN_LOW;

				// Set tuner LNA_GAIN.
				if(fc0013_SetRegMaskBits(pTuner, 0x14, 4, 0, LnaGain) != FC0013_I2C_SUCCESS)
					goto error_status_set_registers;

				break;
			}

			else if(RssiRDiff <= 5)
			{
				boolVhfFlag = 1;
				boolEnInChgFlag = 0;
				intGainShift = 10;
				LnaGain = FC0013_LNA_GAIN_HIGH_17;

				// Set tuner LNA_GAIN.
				if(fc0013_SetRegMaskBits(pTuner, 0x14, 4, 0, LnaGain) != FC0013_I2C_SUCCESS)
					goto error_status_set_registers;

				break;
			}

			else
			{
				goto success_status_Lna_Gain_No_Change;
			}

		
		case FC0013_LNA_GAIN_LOW:

			if(RssiRDiff <= 2)
			{
				boolVhfFlag = 0;
				boolEnInChgFlag = 0;
				intGainShift = 7;
				LnaGain = FC0013_LNA_GAIN_MIDDLE;

				// Set tuner LNA_GAIN.
				if(fc0013_SetRegMaskBits(pTuner, 0x14, 4, 0, LnaGain) != FC0013_I2C_SUCCESS)
					goto error_status_set_registers;

				break;
			}

			else
			{
				goto success_status_Lna_Gain_No_Change;
			}
	}


	if(fc0013_GetRegMaskBits(pTuner, 0x14, 7, 0, &ReadValue) != FC0013_I2C_SUCCESS)
		goto error_status_get_registers;

	if( (ReadValue & 0x60) == 0 )   // disable UHF & GPS ==> lock VHF frequency
	{
		boolVhfFlag = 1;
	}


	if( boolVhfFlag == 1 )
	{
		//FC0013_Write(0x07, (FC0013_Read(0x07) | 0x10));				// VHF = 1
		if(fc0013_GetRegMaskBits(pTuner, 0x07, 7, 0, &ReadValue) != FC0013_I2C_SUCCESS)
			goto error_status_get_registers;

		if(fc0013_SetRegMaskBits(pTuner, 0x07, 7, 0, ReadValue | 0x10) != FC0013_I2C_SUCCESS)
			goto error_status_set_registers;
	}
	else
	{
		//FC0013_Write(0x07, (FC0013_Read(0x07) & 0xEF));				// VHF = 0
		if(fc0013_GetRegMaskBits(pTuner, 0x07, 7, 0, &ReadValue) != FC0013_I2C_SUCCESS)
			goto error_status_get_registers;

		if(fc0013_SetRegMaskBits(pTuner, 0x07, 7, 0, ReadValue & 0xEF) != FC0013_I2C_SUCCESS)
			goto error_status_set_registers;
	}
		

	if( boolEnInChgFlag == 1 )
	{
		//FC0013_Write(0x0A, (FC0013_Read(0x0A) | 0x20));				// EN_IN_CHG = 1
		if(fc0013_GetRegMaskBits(pTuner, 0x0A, 7, 0, &ReadValue) != FC0013_I2C_SUCCESS)
			goto error_status_get_registers;

		if(fc0013_SetRegMaskBits(pTuner, 0x0A, 7, 0, ReadValue | 0x20) != FC0013_I2C_SUCCESS)
			goto error_status_set_registers;
	}
	else
	{
		//FC0013_Write(0x0A, (FC0013_Read(0x0A) & 0xDF));				// EN_IN_CHG = 0
		if(fc0013_GetRegMaskBits(pTuner, 0x0A, 7, 0, &ReadValue) != FC0013_I2C_SUCCESS)
			goto error_status_get_registers;

		if(fc0013_SetRegMaskBits(pTuner, 0x0A, 7, 0, ReadValue & 0xDF) != FC0013_I2C_SUCCESS)
			goto error_status_set_registers;
	}


	if( intGainShift == 10 )
	{
		//FC0013_Write(0x07, (FC0013_Read(0x07) & 0xF0) | 0x0A);		// GS = 10
		if(fc0013_GetRegMaskBits(pTuner, 0x07, 7, 0, &ReadValue) != FC0013_I2C_SUCCESS)
			goto error_status_get_registers;

		if(fc0013_SetRegMaskBits(pTuner, 0x07, 7, 0, (ReadValue & 0xF0) | 0x0A) != FC0013_I2C_SUCCESS)
			goto error_status_set_registers;
	}
	else
	{
		//FC0013_Write(0x07, (FC0013_Read(0x07) & 0xF0) | 0x07);		// GS = 7
		if(fc0013_GetRegMaskBits(pTuner, 0x07, 7, 0, &ReadValue) != FC0013_I2C_SUCCESS)
			goto error_status_get_registers;

		if(fc0013_SetRegMaskBits(pTuner, 0x07, 7, 0, (ReadValue & 0xF0) | 0x07) != FC0013_I2C_SUCCESS)
			goto error_status_set_registers;
	}


success_status_Lna_Gain_No_Change:
	return FUNCTION_SUCCESS;


error_status_get_registers:
error_status_set_registers:
	return FUNCTION_ERROR;
}





