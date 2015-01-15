/**

@file

@brief   RTL2840 MT2063 NIM module definition

One can manipulate RTL2840 MT2063 NIM through RTL2840 MT2063 NIM module.
RTL2840 MT2063 NIM module is derived from QAM NIM module.

*/


#include "nim_rtl2840_mt2063.h"





/**

@brief   RTL2840 MT2063 NIM module builder

Use BuildRtl2840Mt2063Module() to build RTL2840 MT2063 NIM module, set all module function pointers with the
corresponding functions, and initialize module private variables.


@param [in]   ppNim                  Pointer to RTL2840 MT2063 NIM module pointer
@param [in]   pQamNimModuleMemory    Pointer to an allocated QAM NIM module memory
@param [in]   I2cReadingByteNumMax   Maximum I2C reading byte number for basic I2C reading function
@param [in]   I2cWritingByteNumMax   Maximum I2C writing byte number for basic I2C writing function
@param [in]   I2cRead                Basic I2C reading function pointer
@param [in]   I2cWrite               Basic I2C writing function pointer
@param [in]   WaitMs                 Basic waiting function pointer
@param [in]   DemodDeviceAddr        RTL2840 I2C device address
@param [in]   DemodCrystalFreqHz     RTL2840 crystal frequency in Hz
@param [in]   DemodTsInterfaceMode   RTL2840 TS interface mode for setting
@param [in]   DemodEnhancementMode   RTL2840 enhancement mode for setting
@param [in]   TunerDeviceAddr        MT2063 I2C device address


@note
	-# One should call BuildRtl2840Mt2063Module() to build RTL2840 MT2063 NIM module before using it.

*/
void
BuildRtl2840Mt2063Module(
	QAM_NIM_MODULE **ppNim,								// QAM NIM dependence
	QAM_NIM_MODULE *pQamNimModuleMemory,
	unsigned long NimIfFreqHz,

	unsigned long I2cReadingByteNumMax,					// Base interface dependence
	unsigned long I2cWritingByteNumMax,
	BASE_FP_I2C_READ I2cRead,
	BASE_FP_I2C_WRITE I2cWrite,
	BASE_FP_WAIT_MS WaitMs,

	unsigned char DemodDeviceAddr,						// Demod dependence
	unsigned long DemodCrystalFreqHz,
	int DemodTsInterfaceMode,
	int DemodEnhancementMode,

	unsigned char TunerDeviceAddr						// Tuner dependence
	)
{
	QAM_NIM_MODULE *pNim;
	RTL2840_MT2063_EXTRA_MODULE *pNimExtra;



	// Set NIM module pointer with NIM module memory.
	*ppNim = pQamNimModuleMemory;
	
	// Get NIM module.
	pNim = *ppNim;

	// Set I2C bridge module pointer with I2C bridge module memory.
	pNim->pI2cBridge = &pNim->I2cBridgeModuleMemory;

	// Get NIM extra module.
	pNimExtra = &(pNim->Extra.Rtl2840Mt2063);


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

	// Build RTL2840 QAM demod module.
	BuildRtl2840Module(
		&pNim->pDemod,
		&pNim->QamDemodModuleMemory,
		&pNim->BaseInterfaceModuleMemory,
		&pNim->I2cBridgeModuleMemory,
		DemodDeviceAddr,
		DemodCrystalFreqHz,
		DemodTsInterfaceMode,
		DemodEnhancementMode
		);

	// Build MT2063 tuner module.
	BuildMt2063Module(
		&pNim->pTuner,
		&pNim->TunerModuleMemory,
		&pNim->BaseInterfaceModuleMemory,
		&pNim->I2cBridgeModuleMemory,
		TunerDeviceAddr,
//		MT2063_STANDARD_QAM,
		MT2063_STANDARD_DVBT,
		MT2063_VGAGC_0X1
		);


	// Set NIM module manipulating function pointers.
	pNim->Initialize        = rtl2840_mt2063_Initialize;
	pNim->SetParameters     = rtl2840_mt2063_SetParameters;

	// Set NIM module manipulating function pointers with default.
	pNim->GetNimType        = qam_nim_default_GetNimType;
	pNim->GetParameters     = qam_nim_default_GetParameters;
	pNim->IsSignalPresent   = qam_nim_default_IsSignalPresent;
	pNim->IsSignalLocked    = qam_nim_default_IsSignalLocked;
	pNim->GetSignalStrength = qam_nim_default_GetSignalStrength;
	pNim->GetSignalQuality  = qam_nim_default_GetSignalQuality;
	pNim->GetErrorRate      = qam_nim_default_GetErrorRate;
	pNim->GetSnrDb          = qam_nim_default_GetSnrDb;
	pNim->GetTrOffsetPpm    = qam_nim_default_GetTrOffsetPpm;
	pNim->GetCrOffsetHz     = qam_nim_default_GetCrOffsetHz;
	pNim->UpdateFunction    = qam_nim_default_UpdateFunction;


	// Set NIM type.
	pNim->NimType = QAM_NIM_RTL2840_MT2063;

	// Set enhancement mode in NIM module.
	pNim->EnhancementMode = DemodEnhancementMode;

	// Set IF frequency variable in NIM extra module.
	pNimExtra->IfFreqHz = NimIfFreqHz;


	return;
}





/**

@see   QAM_NIM_FP_INITIALIZE

*/
int
rtl2840_mt2063_Initialize(
	QAM_NIM_MODULE *pNim
	)
{
	typedef struct
	{
		int RegBitName;
		unsigned long Value;
	}
	REG_VALUE_ENTRY;


	static const REG_VALUE_ENTRY AdditionalInitRegValueTable[RTL2840_MT2063_ADDITIONAL_INIT_REG_TABLE_LEN] =
	{
		// RegBitName,					Value
		{QAM_OPT_RF_AAGC_DRIVE,			0x1		},
		{QAM_OPT_IF_AAGC_DRIVE,			0x1		},
		{QAM_OPT_RF_AAGC_OEN,			0x1		},
		{QAM_OPT_IF_AAGC_OEN,			0x1		},
		{QAM_RF_AAGC_MAX,				0x80	},
		{QAM_RF_AAGC_MIN,				0x80	},
		{QAM_IF_AAGC_MAX,				0xff	},
		{QAM_IF_AAGC_MIN,				0x0		},
		{QAM_AAGC_MODE_SEL,				0x0		},
	};


	TUNER_MODULE *pTuner;
	QAM_DEMOD_MODULE *pDemod;
	MT2063_EXTRA_MODULE *pTunerExtra;
	RTL2840_MT2063_EXTRA_MODULE *pNimExtra;

	int i;

	int RegBitName;
	unsigned long Value;


	// Get modules.
	pTuner = pNim->pTuner;
	pDemod = pNim->pDemod;
	pTunerExtra = &(pTuner->Extra.Mt2063);
	pNimExtra = &(pNim->Extra.Rtl2840Mt2063);


	// Initialize tuner.
	if(pTuner->Initialize(pTuner) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Set tuner IF frequency in Hz.
	if(pTunerExtra->SetIfFreqHz(pTuner, pNimExtra->IfFreqHz) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Initialize demod.
	if(pDemod->Initialize(pDemod) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Set demod IF frequency in Hz.
	if(pDemod->SetIfFreqHz(pDemod, pNimExtra->IfFreqHz) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Set demod spectrum mode with SPECTRUM_INVERSE.
	if(pDemod->SetSpectrumMode(pDemod, SPECTRUM_INVERSE) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Set demod AAGC registers.
	// Note: SetParameters() will set QAM_AAGC_TARGET and QAM_VTOP according to parameters.
	for(i = 0; i < RTL2840_MT2063_ADDITIONAL_INIT_REG_TABLE_LEN; i++)
	{
		// Get register bit name and its value.
		RegBitName = AdditionalInitRegValueTable[i].RegBitName;
		Value      = AdditionalInitRegValueTable[i].Value;

		// Set demod registers
		if(pDemod->RegAccess.Addr8Bit.SetRegBitsWithPage(pDemod, RegBitName, Value) != FUNCTION_SUCCESS)
			goto error_status_set_registers;
	}


	return FUNCTION_SUCCESS;


error_status_set_registers:
error_status_execute_function:
	return FUNCTION_ERROR;
}





/**

@see   QAM_NIM_FP_SET_PARAMETERS

*/
int
rtl2840_mt2063_SetParameters(
	QAM_NIM_MODULE *pNim,
	unsigned long RfFreqHz,
	int QamMode,
	unsigned long SymbolRateHz,
	int AlphaMode
	)
{
	TUNER_MODULE *pTuner;
	QAM_DEMOD_MODULE *pDemod;


	// Get demod module and tuner module.
	pTuner = pNim->pTuner;
	pDemod = pNim->pDemod;


	// Set tuner RF frequency in Hz.
	if(pTuner->SetRfFreqHz(pTuner, RfFreqHz) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Set demod QAM mode.
	if(pDemod->SetQamMode(pDemod, QamMode) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Set demod symbol rate in Hz.
	if(pDemod->SetSymbolRateHz(pDemod, SymbolRateHz) != FUNCTION_SUCCESS)
		goto error_status_execute_function;

	// Set demod alpha mode.
	if(pDemod->SetAlphaMode(pDemod, AlphaMode) != FUNCTION_SUCCESS)
		goto error_status_execute_function;


	// Reset demod by software reset.
	if(pDemod->SoftwareReset(pDemod) != FUNCTION_SUCCESS)
		goto error_status_execute_function;


	return FUNCTION_SUCCESS;


error_status_execute_function:
	return FUNCTION_ERROR;
}






