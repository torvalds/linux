/**

@file

@brief   RTL2832 MT2266 NIM module definition

One can manipulate RTL2832 MT2266 NIM through RTL2832 MT2266 NIM module.
RTL2832 MT2266 NIM module is derived from DVB-T NIM module.

*/


#include "nim_rtl2832_mt2266.h"





/**

@brief   RTL2832 MT2266 NIM module builder

Use BuildRtl2832Mt2266Module() to build RTL2832 MT2266 NIM module, set all module function pointers with the
corresponding functions, and initialize module private variables.


@param [in]   ppNim                        Pointer to RTL2832 MT2266 NIM module pointer
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
@param [in]   TunerDeviceAddr              MT2266 I2C device address


@note
	-# One should call BuildRtl2832Mt2266Module() to build RTL2832 MT2266 NIM module before using it.

*/
void
BuildRtl2832Mt2266Module(
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

	unsigned char TunerDeviceAddr						// Tuner dependence
	)
{
	DVBT_NIM_MODULE *pNim;
	RTL2832_MT2266_EXTRA_MODULE *pNimExtra;



	// Set NIM module pointer with NIM module memory.
	*ppNim = pDvbtNimModuleMemory;
	
	// Get NIM module.
	pNim = *ppNim;

	// Set I2C bridge module pointer with I2C bridge module memory.
	pNim->pI2cBridge = &pNim->I2cBridgeModuleMemory;

	// Get NIM extra module.
	pNimExtra = &(pNim->Extra.Rtl2832Mt2266);


	// Set NIM type.
	pNim->NimType = DVBT_NIM_RTL2832_MT2266;


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

	// Build MT2266 tuner module.
	BuildMt2266Module(
		&pNim->pTuner,
		&pNim->TunerModuleMemory,
		&pNim->BaseInterfaceModuleMemory,
		&pNim->I2cBridgeModuleMemory,
		TunerDeviceAddr
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
	pNim->Initialize     = rtl2832_mt2266_Initialize;
	pNim->SetParameters  = rtl2832_mt2266_SetParameters;
	pNim->UpdateFunction = rtl2832_mt2266_UpdateFunction;


	// Initialize NIM extra module variables.
	pNimExtra->LnaConfig       = 0xff;
	pNimExtra->UhfSens         = 0xff;
	pNimExtra->AgcCurrentState = 0xff;
	pNimExtra->LnaGainOld      = 0xffffffff;


	return;
}





/**

@see   DVBT_NIM_FP_INITIALIZE

*/
int
rtl2832_mt2266_Initialize(
	DVBT_NIM_MODULE *pNim
	)
{
	typedef struct
	{
		int RegBitName;
		unsigned long Value;
	}
	REG_VALUE_ENTRY;


	static const REG_VALUE_ENTRY AdditionalInitRegValueTable[RTL2832_MT2266_ADDITIONAL_INIT_REG_TABLE_LEN] =
	{
		// RegBitName,				Value
		{DVBT_DAGC_TRG_VAL,			0x39	},
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
		{DVBT_IF_AGC_MIN,			0xc0	},	// Note: The IF_AGC_MIN value will be set again by demod_pdcontrol_reset().
		{DVBT_IF_AGC_MAX,			0x7f	},
		{DVBT_RF_AGC_MIN,			0x9c	},
		{DVBT_RF_AGC_MAX,			0x7f	},
		{DVBT_POLAR_RF_AGC,			0x1		},
		{DVBT_POLAR_IF_AGC,			0x1		},
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
	if(pDemod->Initialize(pDemod) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Set demod IF frequency with 0 Hz.
	if(pDemod->SetIfFreqHz(pDemod, IF_FREQ_0HZ) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Set demod spectrum mode with SPECTRUM_NORMAL.
	if(pDemod->SetSpectrumMode(pDemod, SPECTRUM_NORMAL) != FUNCTION_SUCCESS)
		goto error_status_execute_function;


	// Set demod registers.
	for(i = 0; i < RTL2832_MT2266_ADDITIONAL_INIT_REG_TABLE_LEN; i++)
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
rtl2832_mt2266_SetParameters(
	DVBT_NIM_MODULE *pNim,
	unsigned long RfFreqHz,
	int BandwidthMode
	)
{
	TUNER_MODULE *pTuner;
	DVBT_DEMOD_MODULE *pDemod;

	MT2266_EXTRA_MODULE *pTunerExtra;
	Handle_t Mt2266Handle;
	unsigned long BandwidthHz;

	RTL2832_MT2266_EXTRA_MODULE *pNimExtra;

	UData_t Status;



	// Get tuner module and demod module.
	pTuner = pNim->pTuner;
	pDemod = pNim->pDemod;

	// Get tuner extra module.
	pTunerExtra = &(pTuner->Extra.Mt2266);

	// Get tuner handle.
	Mt2266Handle = pTunerExtra->DeviceHandle;

	// Get NIM extra module.
	pNimExtra = &(pNim->Extra.Rtl2832Mt2266);


	// Enable demod DVBT_IIC_REPEAT.
	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_IIC_REPEAT, 0x1) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

	// Set tuner RF frequency in Hz.
	if(pTuner->SetRfFreqHz(pTuner, RfFreqHz) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Determine BandwidthHz according to bandwidth mode.
	switch(BandwidthMode)
	{
		default:
		case DVBT_BANDWIDTH_6MHZ:		BandwidthHz = MT2266_BANDWIDTH_6MHZ;		break;
		case DVBT_BANDWIDTH_7MHZ:		BandwidthHz = MT2266_BANDWIDTH_7MHZ;		break;
		case DVBT_BANDWIDTH_8MHZ:		BandwidthHz = MT2266_BANDWIDTH_8MHZ;		break;
	}

	// Set tuner bandwidth in Hz with BandwidthHz.
	if(pTunerExtra->SetBandwidthHz(pTuner, BandwidthHz) != FUNCTION_SUCCESS)
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

	// Enable demod DVBT_IIC_REPEAT.
	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_IIC_REPEAT, 0x1) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

	// Reset MT2266 update procedure.
	Status = demod_pdcontrol_reset(pDemod, Mt2266Handle, &pNimExtra->AgcCurrentState);

	if(MT_IS_ERROR(Status))
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
rtl2832_mt2266_UpdateFunction(
	DVBT_NIM_MODULE *pNim
	)
{
	TUNER_MODULE *pTuner;
	DVBT_DEMOD_MODULE *pDemod;
	MT2266_EXTRA_MODULE *pTunerExtra;
	RTL2832_MT2266_EXTRA_MODULE *pNimExtra;

	Handle_t Mt2266Handle;
	UData_t Status;
	


	// Get tuner module and demod module.
	pTuner = pNim->pTuner;
	pDemod = pNim->pDemod;

	// Get tuner extra module and tuner handle.
	pTunerExtra = &(pTuner->Extra.Mt2266);
	pTunerExtra->GetHandle(pTuner, &Mt2266Handle);

	// Get NIM extra module.
	pNimExtra = &(pNim->Extra.Rtl2832Mt2266);


	// Update demod particular registers.
	if(pDemod->UpdateFunction(pDemod) != FUNCTION_SUCCESS)
		goto error_status_execute_function;


	// Enable demod DVBT_IIC_REPEAT.
	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_IIC_REPEAT, 0x1) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

	// Update demod and tuner register setting.
	Status = demod_pdcontrol(
		pDemod,
		Mt2266Handle,
		&pNimExtra->LnaConfig,
		&pNimExtra->UhfSens,
		&pNimExtra->AgcCurrentState,
		&pNimExtra->LnaGainOld
		);

/*
	handle_t demod_handle,
	handle_t tuner_handle,
	unsigned char* lna_config,
	unsigned char* uhf_sens,
	unsigned char *agc_current_state,
	unsigned long *lna_gain_old
	
	unsigned char LnaConfig;
	unsigned char UhfSens;
	unsigned char AgcCurrentState;
	unsigned long LnaGainOld;	
	
*/

	if(MT_IS_ERROR(Status))
		goto error_status_execute_function;

	// Disable demod DVBT_IIC_REPEAT.
	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_IIC_REPEAT, 0x0) != FUNCTION_SUCCESS)
		goto error_status_set_registers;


	return FUNCTION_SUCCESS;


error_status_execute_function:
error_status_set_registers:
	return FUNCTION_ERROR;
}





// The following context is source code provided by Microtune.





// Additional definition for mt_control.c
UData_t
demod_get_pd(
	handle_t demod_handle,
	unsigned short *pd_value
	)
{
	DVBT_DEMOD_MODULE *pDemod;
	unsigned long RssiR;


	// Get demod module.
	pDemod = (DVBT_DEMOD_MODULE *)demod_handle;

	// Get RSSI_R value.
	if(pDemod->GetRegBitsWithPage(pDemod, DVBT_RSSI_R, &RssiR) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	// Set pd_value according to RSSI_R.
	*pd_value = (unsigned short)RssiR;


	return MT_OK;


error_status_get_registers:
	return MT_COMM_ERR;
}



UData_t
demod_get_agc(
	handle_t demod_handle,
	unsigned short *rf_level,
	unsigned short *bb_level
	)
{
	DVBT_DEMOD_MODULE *pDemod;
	unsigned long RfAgc;
	unsigned long IfAgc;


	// Get demod module.
	pDemod = (DVBT_DEMOD_MODULE *)demod_handle;

	// Get RF and IF AGC value.
	if(pDemod->GetRfAgc(pDemod, &RfAgc) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	if(pDemod->GetIfAgc(pDemod, &IfAgc) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	// Convert RF and IF AGC value to proper format.
	*rf_level = (unsigned short)((RfAgc + (1 << (RTL2832_RF_AGC_REG_BIT_NUM - 1))) *
		(1 << (MT2266_DEMOD_ASSUMED_AGC_REG_BIT_NUM - RTL2832_RF_AGC_REG_BIT_NUM)));

	*bb_level = (unsigned short)((IfAgc + (1 << (RTL2832_IF_AGC_REG_BIT_NUM - 1))) *
		(1 << (MT2266_DEMOD_ASSUMED_AGC_REG_BIT_NUM - RTL2832_IF_AGC_REG_BIT_NUM)));


	return MT_OK;


error_status_get_registers:
	return MT_COMM_ERR;
}



UData_t
demod_set_bbagclim(
	handle_t demod_handle,
	int on_off_status
	)
{
	DVBT_DEMOD_MODULE *pDemod;
	unsigned long IfAgcMinBinary;
	long IfAgcMinInt;


	// Get demod module.
	pDemod = (DVBT_DEMOD_MODULE *)demod_handle;

	// Get IF_AGC_MIN binary value.
	if(pDemod->GetRegBitsWithPage(pDemod, DVBT_IF_AGC_MIN, &IfAgcMinBinary) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	// Convert IF_AGC_MIN binary value to integer.
	IfAgcMinInt = BinToSignedInt(IfAgcMinBinary, RTL2832_MT2266_IF_AGC_MIN_BIT_NUM);

	// Modify IF_AGC_MIN integer according to on_off_status.
	switch(on_off_status)
	{
		case 1:

			IfAgcMinInt += RTL2832_MT2266_IF_AGC_MIN_INT_STEP;

			if(IfAgcMinInt > RTL2832_MT2266_IF_AGC_MIN_INT_MAX)
				IfAgcMinInt = RTL2832_MT2266_IF_AGC_MIN_INT_MAX;

			break;

		default:
		case 0:

			IfAgcMinInt -= RTL2832_MT2266_IF_AGC_MIN_INT_STEP;

			if(IfAgcMinInt < RTL2832_MT2266_IF_AGC_MIN_INT_MIN)
				IfAgcMinInt = RTL2832_MT2266_IF_AGC_MIN_INT_MIN;

			break;
	}

	// Convert modified IF_AGC_MIN integer to binary value.
	IfAgcMinBinary = SignedIntToBin(IfAgcMinInt, RTL2832_MT2266_IF_AGC_MIN_BIT_NUM);

	// Set IF_AGC_MIN with modified binary value.
	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_IF_AGC_MIN, IfAgcMinBinary) != FUNCTION_SUCCESS)
		goto error_status_set_registers;


	return MT_OK;


error_status_set_registers:
error_status_get_registers:
	return MT_COMM_ERR;
}





UData_t
tuner_set_bw_normal(
	handle_t tuner_handle,
	handle_t demod_handle
	)
{
	DVBT_DEMOD_MODULE *pDemod;

	int DemodBandwidthMode;
	unsigned int TunerBandwidthHz;
	unsigned int TargetTunerBandwidthHz;


	// Get demod module.
	pDemod = (DVBT_DEMOD_MODULE *)demod_handle;

	// Get demod bandwidth mode.
	if(pDemod->GetBandwidthMode(pDemod, &DemodBandwidthMode) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Determine tuner target bandwidth.
	switch(DemodBandwidthMode)
	{
		case DVBT_BANDWIDTH_6MHZ:	TargetTunerBandwidthHz = MT2266_BANDWIDTH_6MHZ;		break;
		case DVBT_BANDWIDTH_7MHZ:	TargetTunerBandwidthHz = MT2266_BANDWIDTH_7MHZ;		break;
		default:
		case DVBT_BANDWIDTH_8MHZ:	TargetTunerBandwidthHz = MT2266_BANDWIDTH_8MHZ;		break;
	}

	// Get tuner bandwidth.
	if(MT_IS_ERROR(MT2266_GetParam(tuner_handle, MT2266_OUTPUT_BW, &TunerBandwidthHz)))
		goto error_status_get_tuner_bandwidth;

	// Set tuner bandwidth with normal setting according to demod bandwidth mode.
	if(TunerBandwidthHz != TargetTunerBandwidthHz)
	{
		if(MT_IS_ERROR(MT2266_SetParam(tuner_handle, MT2266_OUTPUT_BW, TargetTunerBandwidthHz)))
			goto error_status_set_tuner_bandwidth;
	}


	return MT_OK;


error_status_set_tuner_bandwidth:
error_status_get_tuner_bandwidth:
error_status_execute_function:
	return MT_COMM_ERR;
}





UData_t
tuner_set_bw_narrow(
	handle_t tuner_handle,
	handle_t demod_handle
	)
{
	DVBT_DEMOD_MODULE *pDemod;

	int DemodBandwidthMode;
	unsigned long AciDetInd;
	unsigned int TunerBandwidthHz;
	unsigned int TargetTunerBandwidthHz;


	// Get demod module.
	pDemod = (DVBT_DEMOD_MODULE *)demod_handle;

	// Get demod bandwidth mode.
	if(pDemod->GetBandwidthMode(pDemod, &DemodBandwidthMode) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Get demod ACI_DET_IND.
	if(pDemod->GetRegBitsWithPage(pDemod, DVBT_ACI_DET_IND, &AciDetInd) != FUNCTION_SUCCESS)
		goto error_status_get_registers;

	// Determine tuner target bandwidth according to ACI_DET_IND.
	if(AciDetInd == 0x1)
	{
		// Choose narrow target bandwidth.
		switch(DemodBandwidthMode)
		{
			case DVBT_BANDWIDTH_6MHZ:	TargetTunerBandwidthHz = MT2266_BANDWIDTH_5MHZ;		break;
			case DVBT_BANDWIDTH_7MHZ:	TargetTunerBandwidthHz = MT2266_BANDWIDTH_6MHZ;		break;
			default:
			case DVBT_BANDWIDTH_8MHZ:	TargetTunerBandwidthHz = MT2266_BANDWIDTH_7MHZ;		break;
		}
	}
	else
	{
		// Choose normal target bandwidth.
		switch(DemodBandwidthMode)
		{
			case DVBT_BANDWIDTH_6MHZ:	TargetTunerBandwidthHz = MT2266_BANDWIDTH_6MHZ;		break;
			case DVBT_BANDWIDTH_7MHZ:	TargetTunerBandwidthHz = MT2266_BANDWIDTH_7MHZ;		break;
			default:
			case DVBT_BANDWIDTH_8MHZ:	TargetTunerBandwidthHz = MT2266_BANDWIDTH_8MHZ;		break;
		}
	}

	// Get tuner bandwidth.
	if(MT_IS_ERROR(MT2266_GetParam(tuner_handle, MT2266_OUTPUT_BW, &TunerBandwidthHz)))
		goto error_status_get_tuner_bandwidth;

	// Set tuner bandwidth with normal setting according to demod bandwidth mode.
	if(TunerBandwidthHz != TargetTunerBandwidthHz)
	{
		if(MT_IS_ERROR(MT2266_SetParam(tuner_handle, MT2266_OUTPUT_BW, TargetTunerBandwidthHz)))
			goto error_status_set_tuner_bandwidth;
	}


	return MT_OK;


error_status_set_tuner_bandwidth:
error_status_get_tuner_bandwidth:
error_status_get_registers:
error_status_execute_function:
	return MT_COMM_ERR;
}





// Microtune source code - mt_control.c



UData_t demod_pdcontrol_reset(handle_t demod_handle, handle_t tuner_handle, unsigned char *agc_current_state) {

	DVBT_DEMOD_MODULE *pDemod;
	unsigned long BinaryValue;


	// Get demod module.
	pDemod = (DVBT_DEMOD_MODULE *)demod_handle;

	// Reset AGC current state.
	*agc_current_state = AGC_STATE_START;

	// Calculate RTL2832_MT2266_IF_AGC_MIN_INT_MIN binary value.
	BinaryValue = SignedIntToBin(RTL2832_MT2266_IF_AGC_MIN_INT_MIN, RTL2832_MT2266_IF_AGC_MIN_BIT_NUM);

	// Set IF_AGC_MIN with binary value.
	if(pDemod->SetRegBitsWithPage(pDemod, DVBT_IF_AGC_MIN, BinaryValue) != FUNCTION_SUCCESS)
		goto error_status_set_registers;

	// Set tuner bandwidth with normal setting.
	if(MT_IS_ERROR(tuner_set_bw_normal(tuner_handle, demod_handle)))
		goto error_status_set_tuner_bandwidth;


	return MT_OK;


error_status_set_tuner_bandwidth:
error_status_set_registers:
	return MT_COMM_ERR;
}



UData_t demod_pdcontrol(handle_t demod_handle, handle_t tuner_handle, unsigned char* lna_config, unsigned char* uhf_sens,
					 unsigned char *agc_current_state, unsigned long *lna_gain_old) {

	unsigned short pd_value;
	unsigned short rf_level, bb_level;
	unsigned long lna_gain;
	unsigned char zin=0;
	unsigned int tmp_freq=0,tmp_lna_gain=0;
	
//	unsigned char temp[2];
//	unsigned char agc_bb_min;
//	demod_data_t* local_data;

	
	unsigned char band=1;  /* band=0: vhf, band=1: uhf low, band=2: uhf high */
	unsigned long freq;

	// AGC threshold values
	unsigned short sens_on[]  = {11479, 11479, 32763};
	unsigned short sens_off[] = {36867, 36867, 44767};
	unsigned short lin_off[]  = {23619, 23619, 23619};
	unsigned short lin_on[]   = {38355, 38355, 38355};
	unsigned short pd_upper[] = {85,    85,    85};
	unsigned short pd_lower[] = {74,    74,    74};
	unsigned char next_state;

	// demod_data_t* local_data = (demod_data_t*)demod_handle;	

	if(MT_IS_ERROR(MT2266_GetParam(tuner_handle, MT2266_INPUT_FREQ, &tmp_freq))) goto error_status;
	if(MT_IS_ERROR(MT2266_GetParam(tuner_handle, MT2266_LNA_GAIN, &tmp_lna_gain))) goto error_status;
	if(MT_IS_ERROR(MT2266_GetReg(tuner_handle,0x1e,&zin))) goto error_status;

	freq=(unsigned long)(tmp_freq);
	lna_gain=(unsigned long)(tmp_lna_gain);
	
	if (freq <= 250000000) band=0;
	else if (freq < 660000000) band=1;
	else band=2;
	
	if(MT_IS_ERROR(demod_get_pd(demod_handle, &pd_value))) goto error_status;
	if(MT_IS_ERROR(demod_get_agc(demod_handle, &rf_level, &bb_level))) goto error_status;

	rf_level=0xffff-rf_level;
	bb_level=0xffff-bb_level;

/*
#ifndef _HOST_DLL
	uart_write_nr("St:");
	uart_writedez(agc_current_state[num]);

	uart_write_nr(" PD: ");
	uart_writehex16(pd_value);

	uart_write_nr(" AGC: ");
	uart_writehex16(rf_level);
	uart_writehex16(bb_level);	
#endif
*/

	next_state = *agc_current_state;
	
	switch (*agc_current_state) {
	
	case AGC_STATE_START : {
		if ((int)lna_gain < LNAGAIN_MIN)  
			next_state=AGC_STATE_LNAGAIN_BELOW_MIN;
		else if (lna_gain > LNAGAIN_MAX)  
			next_state=AGC_STATE_LNAGAIN_ABOVE_MAX;
		else 
			next_state=AGC_STATE_NORMAL;
		break;
		}
	
	case AGC_STATE_LNAGAIN_BELOW_MIN : {
		if ((int)lna_gain < LNAGAIN_MIN ) 
			next_state=AGC_STATE_LNAGAIN_BELOW_MIN;
		else next_state=AGC_STATE_NORMAL;
		
		break;
		}
	
	case AGC_STATE_LNAGAIN_ABOVE_MAX : {
		if (lna_gain > LNAGAIN_MAX ) 
			next_state=AGC_STATE_LNAGAIN_ABOVE_MAX;
		else next_state=AGC_STATE_NORMAL;
		break;
		}
	
	case AGC_STATE_NORMAL : {
		if (rf_level > lin_on[band] ) {
			*lna_gain_old = lna_gain;
			next_state = AGC_STATE_MAS_GRANDE_SIGNAL;
			}
		else if (pd_value > pd_upper[band]) {
			next_state = AGC_STATE_GRANDE_INTERFERER;
			}
		else if ( (pd_value < pd_lower[band]) && (lna_gain < LNAGAIN_MAX) ) {
			next_state = AGC_STATE_NO_INTERFERER;
			}
		else if ( bb_level < sens_on[band]) {
			next_state = AGC_STATE_SMALL_SIGNAL;
			}
		break;
		}
	
	case AGC_STATE_NO_INTERFERER : {
		if (pd_value > pd_lower[band] ) 
			next_state = AGC_STATE_MEDIUM_INTERFERER;
		else if (pd_value < pd_lower[band] )
			next_state = AGC_STATE_NORMAL;
		else if ( lna_gain == LNAGAIN_MAX )
			next_state = AGC_STATE_NORMAL;
		break;
		}

	case AGC_STATE_MEDIUM_INTERFERER : {
		if (pd_value > pd_upper[band] ) 
			next_state = AGC_STATE_GRANDE_INTERFERER;
		else if (pd_value < pd_lower[band] )
			next_state = AGC_STATE_NO_INTERFERER;
		break;
		}

	
	case AGC_STATE_GRANDE_INTERFERER : {
		if (pd_value < pd_upper[band] )
			next_state = AGC_STATE_MEDIUM_INTERFERER;
		break;
		}
	
	case AGC_STATE_MAS_GRANDE_SIGNAL : {
		if (rf_level < lin_on[band])
			next_state = AGC_STATE_GRANDE_SIGNAL;
		else if (pd_value > pd_upper[band]) {
			next_state = AGC_STATE_GRANDE_INTERFERER;
			}
		break;
		}
		
	case AGC_STATE_MEDIUM_SIGNAL : {
		if (rf_level > lin_off[band])
			next_state = AGC_STATE_GRANDE_SIGNAL;
		else if (lna_gain >= *lna_gain_old) 
			next_state = AGC_STATE_NORMAL;
		else if (pd_value > pd_upper[band])
			next_state = AGC_STATE_GRANDE_INTERFERER;
		break;
		}
	
	case AGC_STATE_GRANDE_SIGNAL : {
		if (rf_level > lin_on[band])
			next_state = AGC_STATE_MAS_GRANDE_SIGNAL;
		else if (rf_level < lin_off[band]) 
			next_state = AGC_STATE_MEDIUM_SIGNAL;
		else if (pd_value > pd_upper[band])
			next_state = AGC_STATE_GRANDE_INTERFERER;
		break;
		}
	
	case AGC_STATE_SMALL_SIGNAL : {
		if (pd_value > pd_upper[band] ) 
			next_state = AGC_STATE_GRANDE_INTERFERER;
		else if (bb_level > sens_off[band]) 
			next_state = AGC_STATE_NORMAL;
		else if ( (bb_level < sens_on[band]) && (lna_gain == LNAGAIN_MAX) )
			next_state = AGC_STATE_MAX_SENSITIVITY;
		break;
		}
		
	case AGC_STATE_MAX_SENSITIVITY : {
		if (bb_level > sens_off[band]) 
			next_state = AGC_STATE_SMALL_SIGNAL;
		break;
		}
		
	}
			
	*agc_current_state = next_state;	
	
	
	switch (*agc_current_state) {
		
		case AGC_STATE_LNAGAIN_BELOW_MIN : {
			if(MT_IS_ERROR(MT2266_SetParam(tuner_handle,MT2266_LNA_GAIN_INCR, LNAGAIN_MAX))) goto error_status;
			break;
			}
		
		case AGC_STATE_LNAGAIN_ABOVE_MAX : {
			if(MT_IS_ERROR(MT2266_SetParam(tuner_handle,MT2266_LNA_GAIN_DECR, LNAGAIN_MIN))) goto error_status;
			break;
			}
			
		case AGC_STATE_NORMAL : {
			if(MT_IS_ERROR(demod_set_bbagclim(demod_handle,0))) goto error_status;
			if (zin >= 2) {
				zin -= 2;
				if(MT_IS_ERROR(MT2266_SetReg(tuner_handle,0x1e,zin))) goto error_status;
			}
			break;
			}
		
		case AGC_STATE_NO_INTERFERER : {
			if(MT_IS_ERROR(MT2266_SetParam(tuner_handle,MT2266_LNA_GAIN_INCR, LNAGAIN_MAX))) goto error_status;
			if (zin >= 2) {
				zin -= 2;
				if(MT_IS_ERROR(MT2266_SetReg(tuner_handle,0x1e,zin))) goto error_status;
			}

			if(MT_IS_ERROR(demod_set_bbagclim(demod_handle,0))) goto error_status;
			break;
			}

		case AGC_STATE_MEDIUM_INTERFERER : {
			if (zin >= 2) {
				zin -= 2;
				if(MT_IS_ERROR(MT2266_SetReg(tuner_handle,0x1e,zin))) goto error_status;
			}

			// Additional setting
			// Set tuner with normal bandwidth.
			if(MT_IS_ERROR(tuner_set_bw_normal(tuner_handle, demod_handle))) goto error_status;

			break;
			}
		
		case AGC_STATE_GRANDE_INTERFERER : {
			if(MT_IS_ERROR(MT2266_SetParam(tuner_handle,MT2266_LNA_GAIN_DECR, LNAGAIN_MIN))) goto error_status;
			if(MT_IS_ERROR(demod_set_bbagclim(demod_handle,1))) goto error_status;

			// Additional setting
			// Set tuner with narrow bandwidth.
			if(MT_IS_ERROR(tuner_set_bw_narrow(tuner_handle, demod_handle))) goto error_status;

			break;
			}
		
		case AGC_STATE_MEDIUM_SIGNAL : {
			if(MT_IS_ERROR(MT2266_SetParam(tuner_handle,MT2266_LNA_GAIN_INCR, LNAGAIN_MAX))) goto error_status;
			if (zin >= 2) {
				zin -= 2;
				if(MT_IS_ERROR(MT2266_SetReg(tuner_handle,0x1e,zin))) goto error_status;
			}
			if(MT_IS_ERROR(demod_set_bbagclim(demod_handle,0))) goto error_status;
			break;
			}
			
		case AGC_STATE_GRANDE_SIGNAL : {
			if(MT_IS_ERROR(demod_set_bbagclim(demod_handle,0))) goto error_status;
			break;
			}

		case AGC_STATE_MAS_GRANDE_SIGNAL : {
			if(MT_IS_ERROR(MT2266_SetParam(tuner_handle,MT2266_LNA_GAIN_DECR, LNAGAIN_MIN))) goto error_status;
			if (lna_gain==0) {
				if (zin <= 64) {
					zin += 2;
					if(MT_IS_ERROR(MT2266_SetReg(tuner_handle,0x1e,zin))) goto error_status;
					}
				}
			if(MT_IS_ERROR(demod_set_bbagclim(demod_handle,0))) goto error_status;
			break;
			}
		
		case AGC_STATE_SMALL_SIGNAL : {
			if(MT_IS_ERROR(MT2266_SetParam(tuner_handle,MT2266_LNA_GAIN_INCR, LNAGAIN_MAX))) goto error_status;
			if(MT_IS_ERROR(MT2266_SetParam(tuner_handle,MT2266_UHF_NORMAL,1))) goto error_status;
			if (zin >= 2) {
				zin -= 2;
				if(MT_IS_ERROR(MT2266_SetReg(tuner_handle,0x1e,zin))) goto error_status;
			}

			if(MT_IS_ERROR(demod_set_bbagclim(demod_handle,0))) goto error_status;
			*uhf_sens=0;
			break;
			}
		
		case AGC_STATE_MAX_SENSITIVITY : {
			if(MT_IS_ERROR(MT2266_SetParam(tuner_handle,MT2266_UHF_MAXSENS,1))) goto error_status;
			if (zin >= 2) {
				zin -= 2;
				if(MT_IS_ERROR(MT2266_SetReg(tuner_handle,0x1e,zin))) goto error_status;
			}
			if(MT_IS_ERROR(demod_set_bbagclim(demod_handle,0))) goto error_status;
			*uhf_sens=1;
			break;
			}
	}	

	if(MT_IS_ERROR(MT2266_GetParam(tuner_handle, MT2266_LNA_GAIN,&tmp_lna_gain))) goto error_status;
	lna_gain=(unsigned long)(tmp_lna_gain);

	*lna_config=(unsigned char)lna_gain;

/*
#ifndef _HOST_DLL
	uart_write_nr(" LNA ");	
	uart_writedez(lna_gain);
	uart_write_nr(" SENS ");
	uart_writedez(*uhf_sens);
	uart_write_nr(" Z ");
	uart_writedez(zin);
	uart_write(" ");
#endif
*/



	return MT_OK;


error_status:
	return MT_COMM_ERR;
}



