/**

@file

@brief   RTL2832 TDA18272 NIM module definition

One can manipulate RTL2832 TDA18272 NIM through RTL2832 TDA18272 NIM module.
RTL2832 TDA18272 NIM module is derived from DVB-T NIM module.

*/


#include "nim_rtl2832_tda18272.h"





/**

@brief   RTL2832 TDA18272 NIM module builder

Use BuildRtl2832Tda18272Module() to build RTL2832 TDA18272 NIM module, set all module function pointers with the
corresponding functions, and initialize module private variables.


@param [in]   ppNim                        Pointer to RTL2832 TDA18272 NIM module pointer
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
@param [in]   TunerDeviceAddr              TDA18272 I2C device address
@param [in]   TunerCrystalFreqHz           TDA18272 crystal frequency in Hz
@param [in]   TunerUnitNo                  TDA18272 unit number
@param [in]   TunerIfOutputVppMode         TDA18272 IF output Vp-p mode


@note
	-# One should call BuildRtl2832Tda18272Module() to build RTL2832 TDA18272 NIM module before using it.

*/
void
BuildRtl2832Tda18272Module(
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
	unsigned long TunerCrystalFreqHz,
	int TunerUnitNo,
	int TunerIfOutputVppMode
	)
{
	DVBT_NIM_MODULE *pNim;


	// Set NIM module pointer with NIM module memory.
	*ppNim = pDvbtNimModuleMemory;
	
	// Get NIM module.
	pNim = *ppNim;


	// Set I2C bridge module pointer with I2C bridge module memory.
	pNim->pI2cBridge = &pNim->I2cBridgeModuleMemory;


	// Set NIM type.
	pNim->NimType = DVBT_NIM_RTL2832_TDA18272;


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

	// Build TDA18272 tuner module.
	BuildTda18272Module(
		&pNim->pTuner,
		&pNim->TunerModuleMemory,
		&pNim->BaseInterfaceModuleMemory,
		&pNim->I2cBridgeModuleMemory,
		TunerDeviceAddr,
		TunerCrystalFreqHz,
		TunerUnitNo,
		TunerIfOutputVppMode
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
	pNim->UpdateFunction    = dvbt_nim_default_UpdateFunction;

	// Set NIM module function pointers with particular functions.
	pNim->Initialize    = rtl2832_tda18272_Initialize;
	pNim->SetParameters = rtl2832_tda18272_SetParameters;


	return;
}





/**

@see   DVBT_NIM_FP_INITIALIZE

*/
int
rtl2832_tda18272_Initialize(
	DVBT_NIM_MODULE *pNim
	)
{
	typedef struct
	{
		int RegBitName;
		unsigned long Value;
	}
	REG_VALUE_ENTRY;


	static const REG_VALUE_ENTRY AdditionalInitRegValueTable[RTL2832_TDA18272_ADDITIONAL_INIT_REG_TABLE_LEN] =
	{
		// RegBitName,				Value
		{DVBT_DAGC_TRG_VAL,			0x39	},
		{DVBT_AGC_TARG_VAL_0,		0x0		},
		{DVBT_AGC_TARG_VAL_8_1,		0x40	},
		{DVBT_AAGC_LOOP_GAIN,		0x16    },
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
		{DVBT_AD7_SETTING,			0xe9f4	},
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
	// Note: TDA18272 tuner uses dynamic IF frequency, so we will set demod IF frequency in SetParameters().
	if(pDemod->Initialize(pDemod) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Set demod spectrum mode with SPECTRUM_INVERSE.
	if(pDemod->SetSpectrumMode(pDemod, SPECTRUM_INVERSE) != FUNCTION_SUCCESS)
		goto error_status_execute_function;


	// Set demod registers.
	for(i = 0; i < RTL2832_TDA18272_ADDITIONAL_INIT_REG_TABLE_LEN; i++)
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
rtl2832_tda18272_SetParameters(
	DVBT_NIM_MODULE *pNim,
	unsigned long RfFreqHz,
	int BandwidthMode
	)
{
	TUNER_MODULE *pTuner;
	DVBT_DEMOD_MODULE *pDemod;

	TDA18272_EXTRA_MODULE *pTunerExtra;
	int TunerStandardBandwidthMode;
	unsigned long IfFreqHz;



	// Get tuner module and demod module.
	pTuner = pNim->pTuner;
	pDemod = pNim->pDemod;

	// Get tuner extra module.
	pTunerExtra = &(pTuner->Extra.Tda18272);


	// Enable demod DVBT_IIC_REPEAT.
	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_IIC_REPEAT, 0x1) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

	// Determine TunerBandwidthMode according to bandwidth mode.
	switch(BandwidthMode)
	{
		default:
		case DVBT_BANDWIDTH_6MHZ:	TunerStandardBandwidthMode = TDA18272_STANDARD_BANDWIDTH_DVBT_6MHZ;		break;
		case DVBT_BANDWIDTH_7MHZ:	TunerStandardBandwidthMode = TDA18272_STANDARD_BANDWIDTH_DVBT_7MHZ;		break;
		case DVBT_BANDWIDTH_8MHZ:	TunerStandardBandwidthMode = TDA18272_STANDARD_BANDWIDTH_DVBT_8MHZ;		break;
	}

	// Set tuner standard and bandwidth mode with TunerStandardBandwidthMode.
	if(pTunerExtra->SetStandardBandwidthMode(pTuner, TunerStandardBandwidthMode) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Set tuner RF frequency in Hz.
	// Note: Must run SetRfFreqHz() after SetStandardBandwidthMode(), because SetRfFreqHz() needs some
	//       SetStandardBandwidthMode() information.
	if(pTuner->SetRfFreqHz(pTuner, RfFreqHz) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Get tuner IF frequency in Hz.
	// Note: 1. Must run GetIfFreqHz() after SetRfFreqHz(), because GetIfFreqHz() needs some SetRfFreqHz() information.
	//       2. TDA18272 tuner uses dynamic IF frequency.
	if(pTunerExtra->GetIfFreqHz(pTuner, &IfFreqHz) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Disable demod DVBT_IIC_REPEAT.
	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_IIC_REPEAT, 0x0) != FUNCTION_SUCCESS)
		goto error_status_set_registers;


	// Set demod IF frequency according to IfFreqHz.
	// Note: TDA18272 tuner uses dynamic IF frequency.
	if(pDemod->SetIfFreqHz(pDemod, IfFreqHz) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

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

@see   DVBT_NIM_FP_GET_RF_POWER_LEVEL_DBM

*/
int
rtl2832_tda18272_GetRfPowerLevelDbm(
	DVBT_NIM_MODULE *pNim,
	long *pRfPowerLevelDbm
	)
{
	DVBT_DEMOD_MODULE *pDemod;

	unsigned long FsmStage;
	long IfAgc;


	// Get demod module.
	pDemod = pNim->pDemod;


	// Get FSM stage and IF AGC value.
	if(pDemod->GetRegBitsWithPage(pDemod, DVBT_FSM_STAGE, &FsmStage) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	if(pDemod->GetIfAgc(pDemod, &IfAgc) != FUNCTION_SUCCESS)
		goto error_status_get_registers;


	//  Determine signal strength according to FSM stage and IF AGC value.
	if(FsmStage < 10)
		*pRfPowerLevelDbm = -120;
	else
	{
		if(IfAgc > -1250)
			*pRfPowerLevelDbm = -71 - (IfAgc / 165);
		else
			*pRfPowerLevelDbm = -60;
	}


	return FUNCTION_SUCCESS;


error_status_get_registers:
	return FUNCTION_ERROR;
}




