/**

@file

@brief   RTL2840 QAM demod module definition

One can manipulate RTL2840 QAM demod through RTL2840 module.
RTL2840 module is derived from QAM demod module.

*/


#include "demod_rtl2840.h"





/**

@brief   RTL2840 demod module builder

Use BuildRtl2840Module() to build RTL2840 module, set all module function pointers with the corresponding functions, and
initialize module private variables.


@param [in]   ppDemod                      Pointer to RTL2840 demod module pointer
@param [in]   pQamDemodModuleMemory        Pointer to an allocated QAM demod module memory
@param [in]   pBaseInterfaceModuleMemory   Pointer to an allocated base interface module memory
@param [in]   pI2cBridgeModuleMemory       Pointer to an allocated I2C bridge module memory
@param [in]   DeviceAddr                   RTL2840 I2C device address
@param [in]   CrystalFreqHz                RTL2840 crystal frequency in Hz
@param [in]   TsInterfaceMode              RTL2840 TS interface mode for setting
@param [in]   EnhancementMode              RTL2840 enhancement mode for setting


@note
	-# One should call BuildRtl2840Module() to build RTL2840 module before using it.

*/
void
BuildRtl2840Module(
	QAM_DEMOD_MODULE **ppDemod,
	QAM_DEMOD_MODULE *pQamDemodModuleMemory,
	BASE_INTERFACE_MODULE *pBaseInterfaceModuleMemory,
	I2C_BRIDGE_MODULE *pI2cBridgeModuleMemory,
	unsigned char DeviceAddr,
	unsigned long CrystalFreqHz,
	int TsInterfaceMode,
	int EnhancementMode
	)
{
	QAM_DEMOD_MODULE *pDemod;



	// Set demod module pointer.
	*ppDemod = pQamDemodModuleMemory;

	// Get demod module.
	pDemod = *ppDemod;

	// Set base interface module pointer and I2C bridge module pointer.
	pDemod->pBaseInterface = pBaseInterfaceModuleMemory;
	pDemod->pI2cBridge     = pI2cBridgeModuleMemory;


	// Set demod type.
	pDemod->DemodType = QAM_DEMOD_TYPE_RTL2840;

	// Set demod I2C device address.
	pDemod->DeviceAddr = DeviceAddr;

	// Set demod crystal frequency in Hz.
	pDemod->CrystalFreqHz = CrystalFreqHz;

	// Set demod TS interface mode.
	pDemod->TsInterfaceMode = TsInterfaceMode;


	// Initialize demod parameter setting status
	pDemod->IsQamModeSet       = NO;
	pDemod->IsSymbolRateHzSet  = NO;
	pDemod->IsAlphaModeSet     = NO;
	pDemod->IsIfFreqHzSet      = NO;
	pDemod->IsSpectrumModeSet  = NO;


	// Initialize register tables in demod extra module.
	rtl2840_InitBaseRegTable(pDemod);
	rtl2840_InitMonitorRegTable(pDemod);


	// Build I2C birdge module.
	rtl2840_BuildI2cBridgeModule(pDemod);


	// Set demod module I2C function pointers with default functions.
	pDemod->RegAccess.Addr8Bit.SetRegPage         = qam_demod_addr_8bit_default_SetRegPage;
	pDemod->RegAccess.Addr8Bit.SetRegBytes        = qam_demod_addr_8bit_default_SetRegBytes;
	pDemod->RegAccess.Addr8Bit.GetRegBytes        = qam_demod_addr_8bit_default_GetRegBytes;
	pDemod->RegAccess.Addr8Bit.SetRegMaskBits     = qam_demod_addr_8bit_default_SetRegMaskBits;
	pDemod->RegAccess.Addr8Bit.GetRegMaskBits     = qam_demod_addr_8bit_default_GetRegMaskBits;
	pDemod->RegAccess.Addr8Bit.SetRegBits         = qam_demod_addr_8bit_default_SetRegBits;
	pDemod->RegAccess.Addr8Bit.GetRegBits         = qam_demod_addr_8bit_default_GetRegBits;
	pDemod->RegAccess.Addr8Bit.SetRegBitsWithPage = qam_demod_addr_8bit_default_SetRegBitsWithPage;
	pDemod->RegAccess.Addr8Bit.GetRegBitsWithPage = qam_demod_addr_8bit_default_GetRegBitsWithPage;


	// Set demod module manipulating function pointers with default functions.
	pDemod->GetDemodType     = qam_demod_default_GetDemodType;
	pDemod->GetDeviceAddr    = qam_demod_default_GetDeviceAddr;
	pDemod->GetCrystalFreqHz = qam_demod_default_GetCrystalFreqHz;

	pDemod->GetQamMode       = qam_demod_default_GetQamMode;
	pDemod->GetSymbolRateHz  = qam_demod_default_GetSymbolRateHz;
	pDemod->GetAlphaMode     = qam_demod_default_GetAlphaMode;
	pDemod->GetIfFreqHz      = qam_demod_default_GetIfFreqHz;
	pDemod->GetSpectrumMode  = qam_demod_default_GetSpectrumMode;


	// Set demod module manipulating function pointers with particular functions.
	// Note: Need to assign manipulating function pointers according to enhancement mode.
	pDemod->IsConnectedToI2c  = rtl2840_IsConnectedToI2c;
	pDemod->SoftwareReset     = rtl2840_SoftwareReset;

	pDemod->Initialize        = rtl2840_Initialize;
	pDemod->SetSymbolRateHz   = rtl2840_SetSymbolRateHz;
	pDemod->SetAlphaMode      = rtl2840_SetAlphaMode;
	pDemod->SetIfFreqHz       = rtl2840_SetIfFreqHz;
	pDemod->SetSpectrumMode   = rtl2840_SetSpectrumMode;

	pDemod->GetRfAgc          = rtl2840_GetRfAgc;
	pDemod->GetIfAgc          = rtl2840_GetIfAgc;
	pDemod->GetDiAgc          = rtl2840_GetDiAgc;
	pDemod->GetTrOffsetPpm    = rtl2840_GetTrOffsetPpm;
	pDemod->GetCrOffsetHz     = rtl2840_GetCrOffsetHz;

	pDemod->IsAagcLocked      = rtl2840_IsAagcLocked;
	pDemod->IsEqLocked        = rtl2840_IsEqLocked;
	pDemod->IsFrameLocked     = rtl2840_IsFrameLocked;

	pDemod->GetErrorRate      = rtl2840_GetErrorRate;
	pDemod->GetSnrDb          = rtl2840_GetSnrDb;

	pDemod->GetSignalStrength = rtl2840_GetSignalStrength;
	pDemod->GetSignalQuality  = rtl2840_GetSignalQuality;

	pDemod->UpdateFunction    = rtl2840_UpdateFunction;
	pDemod->ResetFunction     = rtl2840_ResetFunction;

	switch(EnhancementMode)
	{
		case QAM_DEMOD_EN_NONE:
			pDemod->SetQamMode = rtl2840_SetQamMode;
			break;

		case QAM_DEMOD_EN_AM_HUM:
			pDemod->SetQamMode = rtl2840_am_hum_en_SetQamMode;
			break;
	}


	return;
}





/**

@see   QAM_DEMOD_FP_IS_CONNECTED_TO_I2C

*/
void
rtl2840_IsConnectedToI2c(
	QAM_DEMOD_MODULE *pDemod,
	int *pAnswer
	)
{
	unsigned long ReadingValue;



	// Set reading value to zero, and get SYS_VERSION value.
	// Note: Use GetRegBitsWithPage() to get register bits with page setting.
	ReadingValue = 0;

	if(pDemod->RegAccess.Addr8Bit.GetRegBitsWithPage(pDemod, QAM_SYS_VERSION, &ReadingValue) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;


	// Compare SYS_VERSION value with RTL2840_SYS_VERSION_VALUE.
	if(ReadingValue == RTL2840_SYS_VERSION_VALUE)
		*pAnswer = YES;
	else
		*pAnswer = NO;


	return;


error_status_get_demod_registers:

	*pAnswer = NO;

	return;
}





/**

@see   QAM_DEMOD_FP_SOFTWARE_RESET

*/
int
rtl2840_SoftwareReset(
	QAM_DEMOD_MODULE *pDemod
	)
{
	// Set register page number with system page number for software resetting.
	if(pDemod->RegAccess.Addr8Bit.SetRegPage(pDemod, 0) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;


	// Set and clear SOFT_RESET register bit.
	if(pDemod->RegAccess.Addr8Bit.SetRegBits(pDemod, QAM_SOFT_RESET, ON) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;

	if(pDemod->RegAccess.Addr8Bit.SetRegBits(pDemod, QAM_SOFT_RESET, OFF) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;


	return FUNCTION_SUCCESS;


error_status_set_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   QAM_DEMOD_FP_INITIALIZE

*/
int
rtl2840_Initialize(
	QAM_DEMOD_MODULE *pDemod
	)
{
	typedef struct
	{
		unsigned char PageNo;
		unsigned char RegStartAddr;
		unsigned char Msb;
		unsigned char Lsb;
		unsigned long WritingValue;
	}
	INIT_REG_ENTRY;


	typedef struct
	{
		unsigned char SpecReg0Sel;
		unsigned char SpecReg0ValueTable[RTL2840_SPEC_REG_0_VALUE_TABLE_LEN];
	}
	INIT_SPEC_REG_0_ENTRY;


	typedef struct
	{
		int RegBitName;
		unsigned long WritingValue[TS_INTERFACE_MODE_NUM];
	}
	TS_INTERFACE_INIT_TABLE_ENTRY;



	static const INIT_REG_ENTRY InitRegTable[RTL2840_INIT_REG_TABLE_LEN] =
	{
		// PageNo,	RegStartAddr,	Msb,	Lsb,	WritingValue
		{0,			0x04,			2,		0,		0x5			},
		{0,			0x04,			4,		3,		0x0			},
		{0,			0x04,			5,		5,		0x1			},
		{0,			0x06,			0,		0,		0x0			},
		{0,			0x07,			2,		2,		0x0			},
		{1,			0x04,			0,		0,		0x0			},
		{1,			0x04,			1,		1,		0x0			},
		{1,			0x04,			2,		2,		0x0			},
		{1,			0x04,			3,		3,		0x0			},
		{1,			0x0c,			2,		0,		0x7			},
		{1,			0x19,			5,		5,		0x0			},
		{1,			0x19,			6,		6,		0x1			},
		{1,			0x19,			7,		7,		0x1			},
		{1,			0x1a,			0,		0,		0x0			},
		{1,			0x1a,			1,		1,		0x1			},
		{1,			0x1a,			2,		2,		0x0			},
		{1,			0x1a,			3,		3,		0x1			},
		{1,			0x1a,			4,		4,		0x0			},
		{1,			0x1a,			5,		5,		0x1			},
		{1,			0x1a,			6,		6,		0x1			},
		{1,			0x1b,			3,		0,		0x7			},
		{1,			0x1b,			7,		4,		0xc			},
		{1,			0x1c,			2,		0,		0x4			},
		{1,			0x1c,			5,		3,		0x3			},
		{1,			0x1d,			5,		0,		0x7			},
		{1,			0x27,			9,		0,		0x6d		},
		{1,			0x2b,			7,		0,		0x26		},
		{1,			0x2c,			7,		0,		0x1e		},
		{1,			0x2e,			7,		6,		0x3			},
		{1,			0x32,			2,		0,		0x7			},
		{1,			0x32,			5,		3,		0x0			},
		{1,			0x32,			6,		6,		0x1			},
		{1,			0x32,			7,		7,		0x0			},
		{1,			0x33,			6,		0,		0xf			},
		{1,			0x33,			7,		7,		0x1			},
		{1,			0x39,			7,		0,		0x88		},
		{1,			0x3a,			7,		0,		0x36		},
		{1,			0x3e,			7,		0,		0x26		},
		{1,			0x3f,			7,		0,		0x15		},
		{1,			0x4b,			8,		0,		0x166		},
		{1,			0x4d,			8,		0,		0x166		},
		{2,			0x11,			0,		0,		0x0			},
		{2,			0x02,			7,		0,		0x7e		},
		{2,			0x12,			3,		0,		0x7			},
		{2,			0x12,			7,		4,		0x7			},
	};


	static const INIT_SPEC_REG_0_ENTRY InitSpecReg0Table[RTL2840_INIT_SPEC_REG_0_TABLE_LEN] =
	{
		// SpecReg0Sel,		{SpecReg0ValueTable																	}
		{0,					{0x00,	0xd0,	0x49,	0x8e,	0xf2,	0x01,	0x00,	0xc0,	0x62,	0x62,	0x00}	},
		{1,					{0x11,	0x21,	0x89,	0x8e,	0xf2,	0x01,	0x80,	0x8b,	0x62,	0xe2,	0x00}	},
		{2,					{0x22,	0x32,	0x89,	0x8e,	0x72,	0x00,	0xc0,	0x86,	0xe2,	0xe3,	0x00}	},
		{3,					{0x43,	0x44,	0x8b,	0x0e,	0xf2,	0xdd,	0xb5,	0x84,	0xe2,	0xcb,	0x00}	},
		{4,					{0x54,	0x55,	0xcb,	0x1e,	0xf3,	0x4d,	0xb5,	0x84,	0xe2,	0xcb,	0x00}	},
		{5,					{0x65,	0x66,	0xcb,	0x1e,	0xf5,	0x4b,	0xb4,	0x84,	0xe2,	0xcb,	0x00}	},
		{6,					{0x76,	0x77,	0xcb,	0x9e,	0xf7,	0xc7,	0x73,	0x80,	0xe2,	0xcb,	0x00}	},
		{7,					{0x87,	0x88,	0xcb,	0x2e,	0x48,	0x41,	0x72,	0x80,	0xe2,	0xcb,	0x00}	},
		{8,					{0x98,	0x99,	0xcc,	0x3e,	0x48,	0x21,	0x71,	0x80,	0xea,	0xcb,	0x00}	},
		{11,				{0xbb,	0xc8,	0xcb,	0x6e,	0x24,	0x18,	0x73,	0xa0,	0xfa,	0xcf,	0x01}	},
		{13,				{0x1d,	0x1e,	0x4f,	0x8e,	0xf2,	0x01,	0x00,	0x80,	0x62,	0x62,	0x00}	},
		{14,				{0x1e,	0x1f,	0x4f,	0x8e,	0xf2,	0x01,	0x00,	0x80,	0x62,	0x62,	0x00}	},
		{15,				{0x1f,	0x11,	0x4f,	0x8e,	0xf2,	0x01,	0x00,	0x80,	0x62,	0x62,	0x00}	},
	};


	static const TS_INTERFACE_INIT_TABLE_ENTRY TsInterfaceInitTable[RTL2840_TS_INTERFACE_INIT_TABLE_LEN] =
	{
		// RegBitName,				WritingValue for {Parallel, Serial}
		{QAM_SERIAL,				{0x0,	0x1}},
		{QAM_CDIV_PH0,				{0x7,	0x0}},
		{QAM_CDIV_PH1,				{0x7,	0x0}},
	};


	int i;

	int TsInterfaceMode;

	unsigned char PageNo;
	unsigned char RegStartAddr;
	unsigned char Msb;
	unsigned char Lsb;
	unsigned long WritingValue;

	unsigned char SpecReg0Sel;
	const unsigned char *pSpecReg0ValueTable;

	unsigned long QamSpecInitA2Backup;
	unsigned long RegValue, RegValueComparison;
	int AreAllValueEqual;



	// Get TS interface mode.
	TsInterfaceMode = pDemod->TsInterfaceMode;

	// Initialize demod with register initializing table.
	for(i = 0; i < RTL2840_INIT_REG_TABLE_LEN; i++)
	{
		// Get all information from each register initializing entry.
		PageNo       = InitRegTable[i].PageNo;
		RegStartAddr = InitRegTable[i].RegStartAddr;
		Msb          = InitRegTable[i].Msb;
		Lsb          = InitRegTable[i].Lsb;
		WritingValue = InitRegTable[i].WritingValue;

		// Set register page number.
		if(pDemod->RegAccess.Addr8Bit.SetRegPage(pDemod, PageNo) != FUNCTION_SUCCESS)
			goto error_status_set_demod_registers;

		// Set register mask bits.
		if(pDemod->RegAccess.Addr8Bit.SetRegMaskBits(pDemod, RegStartAddr, Msb, Lsb, WritingValue) != FUNCTION_SUCCESS)
			goto error_status_set_demod_registers;
	}


	// Set register page number with inner page number for specific register 0 initializing.
	if(pDemod->RegAccess.Addr8Bit.SetRegPage(pDemod, 1) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;

	// Initialize demod with specific register 0 initializing table.
	for(i = 0; i < RTL2840_INIT_SPEC_REG_0_TABLE_LEN; i++)
	{
		// Get all information from each specific register 0 initializing entry.
		SpecReg0Sel         = InitSpecReg0Table[i].SpecReg0Sel;
		pSpecReg0ValueTable = InitSpecReg0Table[i].SpecReg0ValueTable;

		// Set specific register 0 selection.
		if(pDemod->RegAccess.Addr8Bit.SetRegBits(pDemod, QAM_SPEC_REG_0_SEL, SpecReg0Sel) != FUNCTION_SUCCESS)
			goto error_status_set_demod_registers;

		// Set specific register 0 values.
		if(pDemod->RegAccess.Addr8Bit.SetRegBytes(pDemod, RTL2840_SPEC_REG_0_VAL_START_ADDR, pSpecReg0ValueTable, LEN_11_BYTE) !=
			FUNCTION_SUCCESS)
			goto error_status_set_demod_registers;

		// Set specific register 0 strobe.
		// Note: RTL2840 hardware will clear strobe automatically.
		if(pDemod->RegAccess.Addr8Bit.SetRegBits(pDemod, QAM_SPEC_REG_0_STROBE, ON) != FUNCTION_SUCCESS)
			goto error_status_set_demod_registers;
	}


	// Initialize demod registers according to the TS interface initializing table.
	for(i = 0; i < RTL2840_TS_INTERFACE_INIT_TABLE_LEN; i++)
	{
		if(pDemod->RegAccess.Addr8Bit.SetRegBitsWithPage(pDemod, TsInterfaceInitTable[i].RegBitName,
			TsInterfaceInitTable[i].WritingValue[TsInterfaceMode]) != FUNCTION_SUCCESS)
			goto error_status_set_demod_registers;
	}


	// Backup SPEC_INIT_A2 value.
	if(pDemod->RegAccess.Addr8Bit.GetRegBitsWithPage(pDemod, QAM_SPEC_INIT_A2, &QamSpecInitA2Backup)!= FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;

	// Set SPEC_INIT_A2 with 0.
	if(pDemod->RegAccess.Addr8Bit.SetRegBitsWithPage(pDemod, QAM_SPEC_INIT_A2, 0x0) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;

	// Get SPEC_MONITER_INIT_0 several times.
	// If all SPEC_MONITER_INIT_0 getting values are the same, set SPEC_INIT_A1 with 0.
	// Note: 1. Need to set SPEC_INIT_A2 with 0 when get SPEC_MONITER_INIT_0.
	//       2. The function rtl2840_GetMonitorRegBits() will set register page automatically.
	if(rtl2840_GetMonitorRegBits(pDemod, QAM_SPEC_MONITER_INIT_0, &RegValueComparison) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;

	for(i = 0, AreAllValueEqual = YES; i < RTL2840_SPEC_MONITOR_INIT_0_COMPARISON_TIMES; i++)
	{
		if(rtl2840_GetMonitorRegBits(pDemod, QAM_SPEC_MONITER_INIT_0, &RegValue) != FUNCTION_SUCCESS)
			goto error_status_get_demod_registers;

		if(RegValue != RegValueComparison)
		{
			AreAllValueEqual = NO;

			break;
		}
	}

	if(AreAllValueEqual == YES)
	{
		if(pDemod->RegAccess.Addr8Bit.SetRegBitsWithPage(pDemod, QAM_SPEC_INIT_A1, 0x0) != FUNCTION_SUCCESS)
			goto error_status_set_demod_registers;
	}

	// Restore SPEC_INIT_A2 value.
	if(pDemod->RegAccess.Addr8Bit.SetRegBitsWithPage(pDemod, QAM_SPEC_INIT_A2, QamSpecInitA2Backup) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;


	return FUNCTION_SUCCESS;


error_status_get_demod_registers:
error_status_set_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   QAM_DEMOD_FP_SET_QAM_MODE

*/
int
rtl2840_SetQamMode(
	QAM_DEMOD_MODULE *pDemod,
	int QamMode
	)
{
	typedef struct
	{
		unsigned char PageNo;
		unsigned char RegStartAddr;
		unsigned char Msb;
		unsigned char Lsb;
		unsigned long WritingValue[QAM_QAM_MODE_NUM];
	}
	QAM_MODE_REG_ENTRY;


	typedef struct
	{
		unsigned char SpecReg0Sel;
		unsigned char SpecReg0ValueTable[QAM_QAM_MODE_NUM][RTL2840_SPEC_REG_0_VALUE_TABLE_LEN];
	}
	QAM_MODE_SPEC_REG_0_ENTRY;



	static const QAM_MODE_REG_ENTRY QamModeRegTable[RTL2840_QAM_MODE_REG_TABLE_LEN] =
	{
		// Reg,									WritingValue according to QAM mode
		// PageNo,	StartAddr,	Msb,	Lsb,	{4-Q,   16-Q,  32-Q,  64-Q,  128-Q, 256-Q, 512-Q, 1024-Q}
		{1,			0x02,		2,		0,		{0x7,   0x0,   0x1,   0x2,   0x3,   0x4,   0x5,   0x6	}},
		{1,			0x05,		7,		0,		{0x6b,  0x6b,  0x6b,  0x6b,  0x6b,  0x6b,  0x6b,  0x6b  }},
		{1,			0x2f,		15,		5,		{0x37,  0x82,  0xb9,  0x10e, 0x177, 0x21c, 0x2ee, 0x451	}},
		{1,			0x31,		5,		0,		{0x1,   0x3,   0x4,   0x5,   0x8,   0xa,   0xf,   0x14	}},
		{1,			0x2e,		5,		0,		{0x2,   0x4,   0x6,   0x8,   0xc,   0x10,  0x18,  0x20	}},
		{1,			0x18,		7,		0,		{0x0,   0xdb,  0x79,  0x0,   0x8a,  0x0,   0x8c,  0x0	}},
		{1,			0x19,		4,		0,		{0x14,  0x14,  0xf,   0x14,  0xf,   0x14,  0xf,   0x14  }},
		{1,			0x3b,		2,		0,		{0x0,   0x0,   0x0,   0x0,   0x0,   0x0,   0x1,   0x1	}},
		{1,			0x3b,		5,		3,		{0x1,   0x1,   0x1,   0x1,   0x1,   0x1,   0x2,   0x2	}},
		{1,			0x3c,		2,		0,		{0x1,   0x1,   0x1,   0x1,   0x1,   0x1,   0x2,   0x2	}},
		{1,			0x3c,		4,		3,		{0x0,   0x0,   0x0,   0x0,   0x0,   0x0,   0x1,   0x1	}},
		{1,			0x3c,		6,		5,		{0x1,   0x1,   0x1,   0x1,   0x1,   0x1,   0x2,   0x2	}},
		{1,			0x3d,		1,		0,		{0x1,   0x1,   0x1,   0x1,   0x1,   0x1,   0x2,   0x2	}},
		{1,			0x3d,		3,		2,		{0x0,   0x0,   0x0,   0x0,   0x0,   0x0,   0x1,   0x1	}},
		{1,			0x3d,		5,		4,		{0x1,   0x1,   0x1,   0x1,   0x1,   0x1,   0x2,   0x2	}},
		{1,			0x3d,		7,		6,		{0x1,   0x1,   0x1,   0x1,   0x1,   0x1,   0x2,   0x2	}},
		{1,			0x40,		2,		0,		{0x1,   0x1,   0x1,   0x1,   0x1,   0x1,   0x2,   0x2	}},
		{1,			0x40,		5,		3,		{0x2,   0x2,   0x2,   0x2,   0x2,   0x2,   0x3,   0x3	}},
		{1,			0x41,		2,		0,		{0x3,   0x3,   0x3,   0x3,   0x3,   0x3,   0x4,   0x4	}},
		{1,			0x41,		4,		3,		{0x1,   0x1,   0x1,   0x1,   0x1,   0x1,   0x1,   0x1	}},
		{1,			0x41,		6,		5,		{0x2,   0x2,   0x2,   0x2,   0x2,   0x2,   0x2,   0x2	}},
		{1,			0x42,		1,		0,		{0x3,   0x3,   0x3,   0x3,   0x3,   0x3,   0x3,   0x3	}},
		{1,			0x42,		3,		2,		{0x1,   0x1,   0x1,   0x1,   0x1,   0x1,   0x1,   0x1	}},
		{1,			0x42,		5,		4,		{0x2,   0x2,   0x2,   0x2,   0x2,   0x2,   0x2,   0x2	}},
		{1,			0x42,		7,		6,		{0x3,   0x3,   0x3,   0x3,   0x3,   0x3,   0x3,   0x3	}},
	};


	static const QAM_MODE_SPEC_REG_0_ENTRY QamModeSpecReg0Table[RTL2840_QAM_MODE_SPEC_REG_0_TABLE_LEN] =
	{
		// SpecReg0Sel,		{SpecReg0ValueTable                                              }		   QAM mode
		{9,				{	{0x99, 0xa9, 0xc9, 0x4e, 0x48, 0x20, 0x71, 0x80, 0xfa, 0xcb, 0x00},		// 4-QAM
							{0x99, 0xa9, 0xc9, 0x4e, 0x48, 0x20, 0x71, 0x80, 0xfa, 0xcb, 0x00},		// 16-QAM
							{0x99, 0xa9, 0xc9, 0x4e, 0x48, 0x10, 0x70, 0x80, 0xfa, 0xcb, 0x00},		// 32-QAM
							{0x99, 0xa9, 0xc9, 0x4e, 0x48, 0x20, 0x71, 0x80, 0xfa, 0xcb, 0x00},		// 64-QAM
							{0x99, 0xa9, 0xc9, 0x4e, 0x48, 0x10, 0x70, 0x80, 0xfa, 0xcb, 0x00},		// 128-QAM
							{0x99, 0xa9, 0xc9, 0x4e, 0x48, 0x20, 0x71, 0x80, 0xfa, 0xcb, 0x00},		// 256-QAM
							{0x99, 0xa9, 0xc9, 0x4e, 0x48, 0x10, 0x70, 0x80, 0xfa, 0xcb, 0x00},		// 512-QAM
							{0x99, 0xa9, 0xc9, 0x4e, 0x48, 0x20, 0x71, 0x80, 0xfa, 0xcb, 0x00},	}	// 1024-QAM
		},


		// SpecReg0Sel,		{SpecReg0ValueTable                                              }		   QAM mode
		{10,			{	{0xaa, 0xbb, 0xee, 0x5e, 0x48, 0x20, 0x71, 0xa0, 0xfa, 0xcb, 0x00},		// 4-QAM
							{0xaa, 0xbb, 0xee, 0x5e, 0x48, 0x20, 0x71, 0xa0, 0xfa, 0xcb, 0x00},		// 16-QAM
							{0xaa, 0xbb, 0xee, 0x5e, 0x48, 0x10, 0x70, 0xa0, 0xfa, 0xcb, 0x00},		// 32-QAM
							{0xaa, 0xbb, 0xee, 0x5e, 0x48, 0x20, 0x71, 0xa0, 0xfa, 0xcb, 0x00},		// 64-QAM
							{0xaa, 0xbb, 0xee, 0x5e, 0x48, 0x10, 0x70, 0xa0, 0xfa, 0xcb, 0x00},		// 128-QAM
							{0xaa, 0xbb, 0xee, 0x5e, 0x48, 0x20, 0x71, 0xa0, 0xfa, 0xcb, 0x00},		// 256-QAM
							{0xaa, 0xbb, 0xee, 0x5e, 0x48, 0x10, 0x70, 0xa0, 0xfa, 0xcb, 0x00},		// 512-QAM
							{0xaa, 0xbb, 0xee, 0x5e, 0x48, 0x10, 0x70, 0xa0, 0xfa, 0xcb, 0x00},	}	// 1024-QAM
		},

		// SpecReg0Sel,		{SpecReg0ValueTable                                              }		   QAM mode
		{12,			{	{0xc8, 0xcc, 0x00, 0x7f, 0x28, 0xda, 0x4b, 0xa0, 0xfe, 0xcd, 0x01},		// 4-QAM
							{0xc8, 0xcc, 0x00, 0x7f, 0x28, 0xda, 0x4b, 0xa0, 0xfe, 0xcd, 0x01},		// 16-QAM
							{0xc8, 0xcc, 0x00, 0x7f, 0x28, 0xda, 0x4b, 0xa0, 0xfe, 0xcd, 0x01},		// 32-QAM
							{0xc8, 0xcc, 0x00, 0x7f, 0x28, 0xda, 0x4b, 0xa0, 0xfe, 0xcd, 0x01},		// 64-QAM
							{0xc8, 0xcc, 0x00, 0x7f, 0x28, 0xda, 0x4b, 0xa0, 0xfe, 0xcd, 0x01},		// 128-QAM
							{0xc8, 0xcc, 0x00, 0x7f, 0x28, 0xda, 0x4b, 0xa0, 0xfe, 0xcd, 0x01},		// 256-QAM
							{0xc8, 0xcc, 0x00, 0x7f, 0x28, 0xda, 0x4b, 0xa0, 0xfe, 0xcd, 0x01},		// 512-QAM
							{0xc8, 0xcc, 0x00, 0x7f, 0x28, 0xda, 0x4b, 0xa0, 0xfe, 0xcd, 0x01},	}	// 1024-QAM
		},
	};


	int i;

	unsigned char PageNo;
	unsigned char RegStartAddr;
	unsigned char Msb;
	unsigned char Lsb;
	unsigned long WritingValue;

	unsigned char SpecReg0Sel;
	const unsigned char *pSpecReg0ValueTable;



	// Set demod QAM mode with QAM mode register setting table.
	for(i = 0; i < RTL2840_QAM_MODE_REG_TABLE_LEN; i++)
	{
		// Get all information from each register setting entry according to QAM mode.
		PageNo       = QamModeRegTable[i].PageNo;
		RegStartAddr = QamModeRegTable[i].RegStartAddr;
		Msb          = QamModeRegTable[i].Msb;
		Lsb          = QamModeRegTable[i].Lsb;
		WritingValue = QamModeRegTable[i].WritingValue[QamMode];

		// Set register page number.
		if(pDemod->RegAccess.Addr8Bit.SetRegPage(pDemod, PageNo) != FUNCTION_SUCCESS)
			goto error_status_set_demod_registers;

		// Set register mask bits.
		if(pDemod->RegAccess.Addr8Bit.SetRegMaskBits(pDemod, RegStartAddr, Msb, Lsb, WritingValue) != FUNCTION_SUCCESS)
			goto error_status_set_demod_registers;
	}


	// Set register page number with inner page number for QAM mode specific register 0 setting.
	if(pDemod->RegAccess.Addr8Bit.SetRegPage(pDemod, 1) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;

	// Set demod QAM mode with QAM mode specific register 0 setting table.
	for(i = 0; i < RTL2840_QAM_MODE_SPEC_REG_0_TABLE_LEN; i++)
	{
		// Get all information from each specific register 0 setting entry according to QAM mode.
		SpecReg0Sel         = QamModeSpecReg0Table[i].SpecReg0Sel;
		pSpecReg0ValueTable = QamModeSpecReg0Table[i].SpecReg0ValueTable[QamMode];

		// Set specific register 0 selection.
		if(pDemod->RegAccess.Addr8Bit.SetRegBits(pDemod, QAM_SPEC_REG_0_SEL, SpecReg0Sel) != FUNCTION_SUCCESS)
			goto error_status_set_demod_registers;

		// Set specific register 0 values.
		if(pDemod->RegAccess.Addr8Bit.SetRegBytes(pDemod, RTL2840_SPEC_REG_0_VAL_START_ADDR, pSpecReg0ValueTable, LEN_11_BYTE) != 
			FUNCTION_SUCCESS)
			goto error_status_set_demod_registers;

		// Set specific register 0 strobe.
		// Note: RTL2840 hardware will clear strobe automatically.
		if(pDemod->RegAccess.Addr8Bit.SetRegBits(pDemod, QAM_SPEC_REG_0_STROBE, ON) != FUNCTION_SUCCESS)
			goto error_status_set_demod_registers;
	}


	// Set demod QAM mode parameter.
	pDemod->QamMode      = QamMode;
	pDemod->IsQamModeSet = YES;


	return FUNCTION_SUCCESS;


error_status_set_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   QAM_DEMOD_FP_SET_SYMBOL_RATE_HZ

*/
int
rtl2840_SetSymbolRateHz(
	QAM_DEMOD_MODULE *pDemod,
	unsigned long SymbolRateHz
	)
{
	typedef struct
	{
		unsigned long TrDeciRatioRangeMin;
		unsigned char SymbolRateReg0;
		unsigned long SymbolRateValue[RTL2840_SYMBOL_RATE_VALUE_TABLE_LEN];
	}
	SYMBOL_RATE_ENTRY;



	static const SYMBOL_RATE_ENTRY SymbolRateTable[RTL2840_SYMBOL_RATE_TABLE_LEN] =
	{
		// TrDeciRatioRangeMin,	SymbolRateReg0,	{SymbolRateValue                              }
		{0x1a0000,				0x4,			{10,   14,   1,  988,  955, 977, 68,  257, 438}	},
		{0x160000,				0x5,			{2,    15,   19, 1017, 967, 950, 12,  208, 420}	},
		{0x0,					0x6,			{1019, 1017, 9,  29,   3,   957, 956, 105, 377}	},
	};


	int i;

	unsigned long CrystalFreqHz;
	const SYMBOL_RATE_ENTRY *pSymbolRateEntry;

	MPI MpiCrystalFreqHz, MpiSymbolRateHz, MpiConst, MpiVar, MpiNone;

	unsigned long TrDeciRatio;
	unsigned char SymbolRateReg0;
	unsigned long SymbolRateValue;



	// Get demod crystal frequency in Hz.
	pDemod->GetCrystalFreqHz(pDemod, &CrystalFreqHz);

	
	// Calculate TR_DECI_RATIO value.
	// Note: Original formula:   TR_DECI_RATIO = round( (CrystalFreqHz * pow(2, 18)) / SymbolRateHz )
	//       Adjusted formula:   TR_DECI_RATIO = floor( ((CrystalFreqHz << 19) / SymbolRateHz + 1) >> 1 )
	MpiSetValue(&MpiCrystalFreqHz, CrystalFreqHz);
	MpiSetValue(&MpiSymbolRateHz, SymbolRateHz);
	MpiSetValue(&MpiConst, 1);

	MpiLeftShift(&MpiVar, MpiCrystalFreqHz, 19);
	MpiDiv(&MpiVar, &MpiNone, MpiVar, MpiSymbolRateHz);
	MpiAdd(&MpiVar, MpiVar, MpiConst);
	MpiRightShift(&MpiVar, MpiVar, 1);

	MpiGetValue(MpiVar, (long *)&TrDeciRatio);


	// Determine symbol rate entry according to TR_DECI_RATIO value and minimum of TR_DECI_RATIO range.
	for(i = 0; i < RTL2840_SYMBOL_RATE_TABLE_LEN; i++)
	{
		if(TrDeciRatio >= SymbolRateTable[i].TrDeciRatioRangeMin)
		{
			pSymbolRateEntry = &SymbolRateTable[i];

			break;
		}
	}


	// Set register page number with inner page number for symbol rate setting.
	if(pDemod->RegAccess.Addr8Bit.SetRegPage(pDemod, 1) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;

	// Set TR_DECI_RATIO with calculated value.
	if(pDemod->RegAccess.Addr8Bit.SetRegBits(pDemod, QAM_TR_DECI_RATIO, TrDeciRatio) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;


	// Set SPEC_SYMBOL_RATE_REG_0 value with determined symbol rate entry.
	SymbolRateReg0 = pSymbolRateEntry->SymbolRateReg0;

	if(pDemod->RegAccess.Addr8Bit.SetRegBits(pDemod, QAM_SPEC_SYMBOL_RATE_REG_0, SymbolRateReg0) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;


	// Set symbol rate value with determined symbol rate entry.
	for(i = 0; i < RTL2840_SYMBOL_RATE_VALUE_TABLE_LEN; i++)
	{
		// Get symbol rate value.
		SymbolRateValue = pSymbolRateEntry->SymbolRateValue[i];

		// Set symbol rate register selection.
		if(pDemod->RegAccess.Addr8Bit.SetRegBits(pDemod, QAM_SPEC_SYMBOL_RATE_SEL, i) != FUNCTION_SUCCESS)
			goto error_status_set_demod_registers;

		// Set symbol rate register value.
		if(pDemod->RegAccess.Addr8Bit.SetRegBits(pDemod, QAM_SPEC_SYMBOL_RATE_VAL, SymbolRateValue) != FUNCTION_SUCCESS)
			goto error_status_set_demod_registers;

		// Set symbol rate register strobe.
		// Note: RTL2840 hardware will clear strobe automatically.
		if(pDemod->RegAccess.Addr8Bit.SetRegBits(pDemod, QAM_SPEC_SYMBOL_RATE_STROBE, ON) != FUNCTION_SUCCESS)
			goto error_status_set_demod_registers;
	}


	// Set demod symbol rate parameter.
	pDemod->SymbolRateHz      = SymbolRateHz;
	pDemod->IsSymbolRateHzSet = YES;


	return FUNCTION_SUCCESS;


error_status_set_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   QAM_DEMOD_FP_SET_ALPHA_MODE

*/
int
rtl2840_SetAlphaMode(
	QAM_DEMOD_MODULE *pDemod,
	int AlphaMode
	)
{
	static const unsigned long AlphaValueTable[QAM_ALPHA_MODE_NUM][RTL2840_ALPHA_VALUE_TABLE_LEN] =
	{
		{258,  94,   156,  517,  6,  1015,  1016,  17,  11,  994,   1011,  51,  15,  926,  1008,  313},	// alpha = 0.12
		{258,  31,   28,   3,    6,  1016,  1016,  16,  11,  996,   1010,  50,  16,  927,  1007,  312},	// alpha = 0.13
		{131,  257,  27,   2,    8,  1017,  1013,  16,  14,  996,   1008,  50,  18,  927,  1004,  310},	// alpha = 0.15
		{0,    195,  30,   30,   6,  1022,  1014,  10,  14,  1002,  1006,  45,  21,  931,  1001,  307},	// alpha = 0.18
		{415,  68,   31,   29,   4,  1,     1016,  6,   13,  1006,  1006,  41,  23,  934,  998,   304},	// alpha = 0.20
	};


	int i;
	unsigned long AlphaValue;



	// Set register page number with inner page number for alpha value setting.
	if(pDemod->RegAccess.Addr8Bit.SetRegPage(pDemod, 1) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;

	// Set demod alpha mode with alpha value table.
	for(i = 0; i < RTL2840_ALPHA_VALUE_TABLE_LEN; i++)
	{
		// Get alpha value from alpha value entry according to alpha mode.
		AlphaValue = AlphaValueTable[AlphaMode][i];

		// Set alpha register selection.
		if(pDemod->RegAccess.Addr8Bit.SetRegBits(pDemod, QAM_SPEC_ALPHA_SEL, i) != FUNCTION_SUCCESS)
			goto error_status_set_demod_registers;

		// Set alpha register value.
		if(pDemod->RegAccess.Addr8Bit.SetRegBits(pDemod, QAM_SPEC_ALPHA_VAL, AlphaValue) != FUNCTION_SUCCESS)
			goto error_status_set_demod_registers;

		// Set alpha register strobe.
		// Note: RTL2840 hardware will clear strobe automatically.
		if(pDemod->RegAccess.Addr8Bit.SetRegBits(pDemod, QAM_SPEC_ALPHA_STROBE, ON) != FUNCTION_SUCCESS)
			goto error_status_set_demod_registers;
	}


	// Set demod alpha mode parameter.
	pDemod->AlphaMode      = AlphaMode;
	pDemod->IsAlphaModeSet = YES;


	return FUNCTION_SUCCESS;


error_status_set_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   QAM_DEMOD_FP_SET_IF_FREQ_HZ

*/
int
rtl2840_SetIfFreqHz(
	QAM_DEMOD_MODULE *pDemod,
	unsigned long IfFreqHz
	)
{
	unsigned long CrystalFreqHz;
	unsigned long DdcFreq;

	MPI MpiIfFreqHz, MpiCrystalFreqHz, MpiConst, MpiVar, MpiNone;



	// Get demod crystal frequency in Hz.
	pDemod->GetCrystalFreqHz(pDemod, &CrystalFreqHz);


	// Calculate DDC_FREQ value.
	// Note: Original formula:   DDC_FREQ = round( (CrystalFreqHz - (IfFreqHz % CrystalFreqHz)) * pow(2, 15) /
	//                                             CrystalFreqHz )
	//       Adjusted formula:   DDC_FREQ = floor( ( ((CrystalFreqHz - (IfFreqHz % CrystalFreqHz)) << 16) /
	//                                               CrystalFreqHz + 1 ) >> 1)
	MpiSetValue(&MpiIfFreqHz, IfFreqHz);
	MpiSetValue(&MpiCrystalFreqHz, CrystalFreqHz);
	MpiSetValue(&MpiConst, 1);

	MpiSetValue(&MpiVar, CrystalFreqHz - (IfFreqHz % CrystalFreqHz));
	MpiLeftShift(&MpiVar, MpiVar, 16);
	MpiDiv(&MpiVar, &MpiNone, MpiVar, MpiCrystalFreqHz);
	MpiAdd(&MpiVar, MpiVar, MpiConst);
	MpiRightShift(&MpiVar, MpiVar, 1);

	MpiGetValue(MpiVar, (long *)&DdcFreq);


	// Set DDC_FREQ with calculated value.
	// Note: Use SetRegBitsWithPage() to set register bits with page setting.
	if(pDemod->RegAccess.Addr8Bit.SetRegBitsWithPage(pDemod, QAM_DDC_FREQ, DdcFreq) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;


	// Set demod IF frequnecy parameter.
	pDemod->IfFreqHz      = IfFreqHz;
	pDemod->IsIfFreqHzSet = YES;


	return FUNCTION_SUCCESS;


error_status_set_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   QAM_DEMOD_FP_SET_SPECTRUM_MODE

*/
int
rtl2840_SetSpectrumMode(
	QAM_DEMOD_MODULE *pDemod,
	int SpectrumMode
	)
{
	static const char SpecInvValueTable[SPECTRUM_MODE_NUM] =
	{
		// SpecInv
		0,				// Normal spectrum
		1,				// Inverse spectrum
	};


	unsigned long SpecInv;



	// Get SPEC_INV value from spectrum inverse value table according to spectrum mode.
	SpecInv = SpecInvValueTable[SpectrumMode];


	// Set SPEC_INV with gotten value.
	// Note: Use SetRegBitsWithPage() to set register bits with page setting.
	if(pDemod->RegAccess.Addr8Bit.SetRegBitsWithPage(pDemod, QAM_SPEC_INV, SpecInv) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;


	// Set demod spectrum mode parameter.
	pDemod->SpectrumMode      = SpectrumMode;
	pDemod->IsSpectrumModeSet = YES;


	return FUNCTION_SUCCESS;


error_status_set_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   QAM_DEMOD_FP_GET_RF_AGC

*/
int
rtl2840_GetRfAgc(
	QAM_DEMOD_MODULE *pDemod,
	long *pRfAgc
	)
{
	unsigned long RfAgcBinary;


	// Get RF AGC binary value from RF_AGC_VALUE monitor register bits.
	// Note: The function rtl2840_GetMonitorRegBits() will set register page automatically.
	if(rtl2840_GetMonitorRegBits(pDemod, QAM_RF_AGC_VALUE, &RfAgcBinary) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;


	// Convert RF AGC binary value to signed integer.
	*pRfAgc = BinToSignedInt(RfAgcBinary, RTL2840_RF_AGC_VALUE_BIT_NUM);


	return FUNCTION_SUCCESS;


error_status_get_demod_registers:
	return FUNCTION_ERROR;
}




/**

@see   QAM_DEMOD_FP_GET_IF_AGC

*/
int
rtl2840_GetIfAgc(
	QAM_DEMOD_MODULE *pDemod,
	long *pIfAgc
	)
{
	unsigned long IfAgcBinary;


	// Get IF AGC binary value from IF_AGC_VALUE monitor register bits.
	// Note: The function rtl2840_GetMonitorRegBits() will set register page automatically.
	if(rtl2840_GetMonitorRegBits(pDemod, QAM_IF_AGC_VALUE, &IfAgcBinary) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;


	// Convert IF AGC binary value to signed integer.
	*pIfAgc = BinToSignedInt(IfAgcBinary, RTL2840_IF_AGC_VALUE_BIT_NUM);


	return FUNCTION_SUCCESS;


error_status_get_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   QAM_DEMOD_FP_GET_DI_AGC

*/
int
rtl2840_GetDiAgc(
	QAM_DEMOD_MODULE *pDemod,
	unsigned long *pDiAgc
	)
{
	// Get digital AGC value from DAGC_VALUE monitor register bits.
	// Note: The function rtl2840_GetMonitorRegBits() will set register page automatically.
	if(rtl2840_GetMonitorRegBits(pDemod, QAM_DAGC_VALUE, pDiAgc) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;


	return FUNCTION_SUCCESS;


error_status_get_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   QAM_DEMOD_FP_GET_TR_OFFSET_PPM

*/
int
rtl2840_GetTrOffsetPpm(
	QAM_DEMOD_MODULE *pDemod,
	long *pTrOffsetPpm
	)
{
	unsigned long SymbolRateHz;
	unsigned long CrystalFreqHz;

	unsigned long TrOffsetBinary;
	long          TrOffsetInt;

	MPI MpiTrOffsetInt, MpiSymbolRateHz, MpiCrystalFreqHz, MpiVar0, MpiVar1;



	// Get demod symbol rate in Hz.
	if(pDemod->GetSymbolRateHz(pDemod, &SymbolRateHz) != FUNCTION_SUCCESS)
		goto error_status_get_demod_symbol_rate;


	// Get demod crystal frequency in Hz.
	pDemod->GetCrystalFreqHz(pDemod, &CrystalFreqHz);


	// Get TR offset binary value from TR_OFFSET monitor register bits.
	// Note: The function rtl2840_GetMonitorRegBits() will set register page automatically.
	if(rtl2840_GetMonitorRegBits(pDemod, QAM_TR_OFFSET, &TrOffsetBinary) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;


	// Convert TR offset binary value to signed integer.
	TrOffsetInt = BinToSignedInt(TrOffsetBinary, RTL2840_TR_OFFSET_BIT_NUM);


	// Get TR offset in ppm.
	// Note: (TR offset in ppm) = ((TR offset integer) * (symbol rate in Hz) * 1000000) /
	//                            ((pow(2, 35) * (crystal frequency in Hz))
	//       TR offset integer is 31 bit value.
	MpiSetValue(&MpiTrOffsetInt,   TrOffsetInt);
	MpiSetValue(&MpiSymbolRateHz,  (long)SymbolRateHz);
	MpiSetValue(&MpiCrystalFreqHz, (long)CrystalFreqHz);
	MpiSetValue(&MpiVar0,          1000000);

	MpiMul(&MpiVar0, MpiVar0, MpiTrOffsetInt);
	MpiMul(&MpiVar0, MpiVar0, MpiSymbolRateHz);
	MpiLeftShift(&MpiVar1, MpiCrystalFreqHz, 35);
	MpiDiv(&MpiVar0, &MpiVar1, MpiVar0, MpiVar1);

	MpiGetValue(MpiVar0, pTrOffsetPpm);


	return FUNCTION_SUCCESS;


error_status_get_demod_registers:
error_status_get_demod_symbol_rate:
	return FUNCTION_ERROR;
}





/**

@see   QAM_DEMOD_FP_GET_CR_OFFSET_HZ

*/
int
rtl2840_GetCrOffsetHz(
	QAM_DEMOD_MODULE *pDemod,
	long *pCrOffsetHz
	)
{
	unsigned long SymbolRateHz;

	unsigned long CrOffsetBinary;
	long          CrOffsetInt;

	MPI MpiCrOffsetInt, MpiSymbolRateHz, MpiMiddleResult;



	// Get demod symbol rate in Hz.
	if(pDemod->GetSymbolRateHz(pDemod, &SymbolRateHz) != FUNCTION_SUCCESS)
		goto error_status_get_demod_symbol_rate;


	// Get CR offset binary value from CR_OFFSET monitor register bits.
	// Note: The function rtl2840_GetMonitorRegBits() will set register page automatically.
	if(rtl2840_GetMonitorRegBits(pDemod, QAM_CR_OFFSET, &CrOffsetBinary) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;


	// Convert CR offset binary value to signed integer.
	CrOffsetInt = BinToSignedInt(CrOffsetBinary, RTL2840_CR_OFFSET_BIT_NUM);


	// Get CR offset in Hz.
	// Note: (CR offset in Hz) = (CR offset integer) * (symbol rate in Hz) / pow(2, 34)
	//       CR offset integer is 32 bit value.
	MpiSetValue(&MpiCrOffsetInt,  CrOffsetInt);
	MpiSetValue(&MpiSymbolRateHz, (long)SymbolRateHz);

	MpiMul(&MpiMiddleResult, MpiCrOffsetInt, MpiSymbolRateHz);
	MpiRightShift(&MpiMiddleResult, MpiMiddleResult, 34);

	MpiGetValue(MpiMiddleResult, pCrOffsetHz);


	return FUNCTION_SUCCESS;


error_status_get_demod_registers:
error_status_get_demod_symbol_rate:
	return FUNCTION_ERROR;
}





/**

@see   QAM_DEMOD_FP_IS_AAGC_LOCKED

*/
int
rtl2840_IsAagcLocked(
	QAM_DEMOD_MODULE *pDemod,
	int *pAnswer
	)
{
	unsigned long LockStatus;



	// Get AAGC lock status from AAGC_LD inner strobe register bits.
	// Note: The function rtl2840_GetInnerStrobeRegBits() will set register page automatically.
	if(rtl2840_GetInnerStrobeRegBits(pDemod, QAM_AAGC_LD, &LockStatus) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;


	// Determine answer according to AAGC lock status.
	if(LockStatus == LOCKED)
		*pAnswer = YES;
	else
		*pAnswer = NO;


	return FUNCTION_SUCCESS;


error_status_get_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   QAM_DEMOD_FP_IS_EQ_LOCKED

*/
int
rtl2840_IsEqLocked(
	QAM_DEMOD_MODULE *pDemod,
	int *pAnswer
	)
{
	unsigned long LockStatus;



	// Get EQ lock status from EQ_LD inner strobe register bits.
	// Note: The function rtl2840_GetInnerStrobeRegBits() will set register page automatically.
	if(rtl2840_GetInnerStrobeRegBits(pDemod, QAM_EQ_LD, &LockStatus) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;


	// Determine answer according to EQ lock status.
	if(LockStatus == LOCKED)
		*pAnswer = YES;
	else
		*pAnswer = NO;


	return FUNCTION_SUCCESS;


error_status_get_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   QAM_DEMOD_FP_IS_FRAME_LOCKED

*/
int
rtl2840_IsFrameLocked(
	QAM_DEMOD_MODULE *pDemod,
	int *pAnswer
	)
{
	unsigned long LossStatus;



	// Get frame loss status from SYNCLOST register bits.
	// Note: The function GetRegBitsWithPage() will set register page automatically.
	if(pDemod->RegAccess.Addr8Bit.GetRegBitsWithPage(pDemod, QAM_SYNCLOST, &LossStatus) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;


	// Determine answer according to frame loss status.
	if(LossStatus == NOT_LOST)
		*pAnswer = YES;
	else
		*pAnswer = NO;


	return FUNCTION_SUCCESS;


error_status_get_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   QAM_DEMOD_FP_GET_ERROR_RATE

*/
int
rtl2840_GetErrorRate(
	QAM_DEMOD_MODULE *pDemod,
	unsigned long TestVolume,
	unsigned int WaitTimeMsMax,
	unsigned long *pBerNum,
	unsigned long *pBerDen,
	unsigned long *pPerNum,
	unsigned long *pPerDen
	)
{
	BASE_INTERFACE_MODULE *pBaseInterface;

	unsigned int i;
	unsigned long TestPacketNum;
	unsigned int  WaitCnt;
	int FrameLock;
	unsigned long BerReg2, BerReg2Msb, BerReg2Lsb;
	unsigned long BerReg0, BerReg1;



	// Get base interface.
	pBaseInterface = pDemod->pBaseInterface;


	// Calculate test packet number and wait counter value.
	TestPacketNum = 0x1 << (TestVolume * 2 + 4);
	WaitCnt = WaitTimeMsMax / RTL2840_BER_WAIT_TIME_MS;


	// Set TEST_VOLUME with test volume.
	// Note: The function SetRegBitsWithPage() will set register page automatically.
	if(pDemod->RegAccess.Addr8Bit.SetRegBitsWithPage(pDemod, QAM_TEST_VOLUME, TestVolume) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;


	// Clear and enable error counter.
	// Note: The function SetRegBitsWithPage() will set register page automatically.
	if(pDemod->RegAccess.Addr8Bit.SetRegBitsWithPage(pDemod, QAM_BERT_EN, OFF) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;

	if(pDemod->RegAccess.Addr8Bit.SetRegBitsWithPage(pDemod, QAM_BERT_EN, ON) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;


	// Check if error test is finished.
	for(i = 0; i < WaitCnt; i++)
	{
		// Check if demod is frame-locked.
		if(pDemod->IsFrameLocked(pDemod, &FrameLock) != FUNCTION_SUCCESS)
			goto error_status_get_demod_registers;

		if(FrameLock == NO)
			goto error_status_frame_lock;


		// Wait a minute.
		// Note: The input unit of WaitMs() is ms.
		pBaseInterface->WaitMs(pBaseInterface, RTL2840_BER_WAIT_TIME_MS);


		// Set error counter strobe.
		// Note: RTL2840 hardware will clear strobe automatically.
		//       The function SetRegBitsWithPage() will set register page automatically.
		if(pDemod->RegAccess.Addr8Bit.SetRegBitsWithPage(pDemod, QAM_BER_RD_STROBE, ON) != FUNCTION_SUCCESS)
			goto error_status_set_demod_registers;


		// Check if error test is finished.
		// Note: The function GetRegBitsWithPage() will set register page automatically.
		if(pDemod->RegAccess.Addr8Bit.GetRegBitsWithPage(pDemod, QAM_BER_REG2_15_0, &BerReg2Lsb) != FUNCTION_SUCCESS)
			goto error_status_get_demod_registers;

		if(pDemod->RegAccess.Addr8Bit.GetRegBitsWithPage(pDemod, QAM_BER_REG2_18_16, &BerReg2Msb) != FUNCTION_SUCCESS)
			goto error_status_get_demod_registers;

		BerReg2 = (BerReg2Msb << RTL2840_BER_REG2_MSB_SHIFT) | BerReg2Lsb;

		if(BerReg2 == TestPacketNum)
			break;
	}


	// Check time-out status.
	if(i == WaitCnt)
		goto error_status_time_out;


	// Get BER register 0 from BER_REG0.
	// Note: The function GetRegBitsWithPage() will set register page automatically.
	if(pDemod->RegAccess.Addr8Bit.GetRegBitsWithPage(pDemod, QAM_BER_REG0, &BerReg0) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;


	// Get BER register 1 from BER_REG1.
	// Note: The function GetRegBitsWithPage() will set register page automatically.
	if(pDemod->RegAccess.Addr8Bit.GetRegBitsWithPage(pDemod, QAM_BER_REG1, &BerReg1) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;


	// Set BER numerator and denominator.
	*pBerNum = 27 * BerReg0 + BerReg1;
	*pBerDen = 1632 * TestPacketNum;


	// Set PER numerator and denominator.
	*pPerNum = BerReg0;
	*pPerDen = TestPacketNum;


	return FUNCTION_SUCCESS;


error_status_set_demod_registers:
error_status_get_demod_registers:
error_status_frame_lock:
error_status_time_out:
	return FUNCTION_ERROR;
}





/**

@see   QAM_DEMOD_FP_GET_SNR_DB

*/
int
rtl2840_GetSnrDb(
	QAM_DEMOD_MODULE *pDemod,
	long *pSnrDbNum,
	long *pSnrDbDen
	)
{
	static const unsigned long SnrConstTable[QAM_QAM_MODE_NUM] =
	{
		26880,		// for    4-QAM mode
		29852,		// for   16-QAM mode
		31132,		// for   32-QAM mode
		32502,		// for   64-QAM mode
		33738,		// for  128-QAM mode
		35084,		// for  256-QAM mode
		36298,		// for  512-QAM mode
		37649,		// for 1024-QAM mode
	};

	int QamMode;

	unsigned long Mse;
	long MiddleResult;
	MPI MpiMse, MpiResult;



	// Get demod QAM mode.
	if(pDemod->GetQamMode(pDemod, &QamMode) != FUNCTION_SUCCESS)
		goto error_status_get_demod_qam_mode;


	// Get mean-square error from MSE.
	// Note: The function rtl2840_GetInnerStrobeRegBits() will set register page automatically.
	if(rtl2840_GetInnerStrobeRegBits(pDemod, QAM_MSE, &Mse) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;


	// Calculate SNR dB numerator.
	MpiSetValue(&MpiMse, Mse);
	MpiLog2(&MpiResult, MpiMse, RTL2840_SNR_FRAC_BIT_NUM);
	MpiGetValue(MpiResult, &MiddleResult);

	*pSnrDbNum = SnrConstTable[QamMode] - 10 * MiddleResult;

	
	// Set SNR dB denominator.
	*pSnrDbDen = RTL2840_SNR_DB_DEN;


	return FUNCTION_SUCCESS;


error_status_get_demod_registers:
error_status_get_demod_qam_mode:
	return FUNCTION_ERROR;
}





/**

@see   QAM_DEMOD_FP_GET_SIGNAL_STRENGTH

*/
int
rtl2840_GetSignalStrength(
	QAM_DEMOD_MODULE *pDemod,
	unsigned long *pSignalStrength
	)
{
	int FrameLock;
	long IfAgcValue;



	// Get demod frame lock status.
	if(pDemod->IsFrameLocked(pDemod, &FrameLock) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;

	// If demod is not frame-locked, set signal strength with zero.
	if(FrameLock == NO)
	{
		*pSignalStrength = 0;
		goto success_status_non_frame_lock;
	}


	// Get IF AGC value.
	if(pDemod->GetIfAgc(pDemod, &IfAgcValue) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;


	// Determine signal strength according to IF AGC value.
	// Note: Map IF AGC value (1023 ~ -1024) to signal strength (0 ~ 100).
	*pSignalStrength = (102300 - IfAgcValue * 100) / 2047;


success_status_non_frame_lock:
	return FUNCTION_SUCCESS;


error_status_get_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   QAM_DEMOD_FP_GET_SIGNAL_QUALITY

*/
int
rtl2840_GetSignalQuality(
	QAM_DEMOD_MODULE *pDemod,
	unsigned long *pSignalQuality
	)
{
	int FrameLock;

	unsigned long Mse;
	long MiddleResult;
	MPI MpiMse, MpiResult;



	// Get demod frame lock status.
	if(pDemod->IsFrameLocked(pDemod, &FrameLock) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;

	// If demod is not frame-locked, set signal quality with zero.
	if(FrameLock == NO)
	{
		*pSignalQuality = 0;
		goto success_status_non_frame_lock;
	}


	// Get mean-square error from MSE.
	// Note: The function rtl2840_GetInnerStrobeRegBits() will set register page automatically.
	if(rtl2840_GetInnerStrobeRegBits(pDemod, QAM_MSE, &Mse) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;


	// Determine signal quality according to MSE value.
	// Note: Map MSE value (pow(2, 19) ~ pow(2, 17)) to signal quality (0 ~ 100).
	//       If MSE value < pow(2, 17), signal quality is 100.
	//       If MSE value > pow(2, 19), signal quality is 0.
	if(Mse > 524288)
	{
		*pSignalQuality = 0;
	}
	else if(Mse < 131072)
	{
		*pSignalQuality = 100;
	}
	else
	{
		MpiSetValue(&MpiMse, Mse);
		MpiLog2(&MpiResult, MpiMse, RTL2840_SIGNAL_QUALITY_FRAC_BIT_NUM);
		MpiGetValue(MpiResult, &MiddleResult);

		*pSignalQuality = (243200 - MiddleResult * 100) / 256;
	}


success_status_non_frame_lock:
	return FUNCTION_SUCCESS;


error_status_get_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   QAM_DEMOD_FP_UPDATE_FUNCTION

*/
int
rtl2840_UpdateFunction(
	QAM_DEMOD_MODULE *pDemod
	)
{
	// RTL2840 does not use UpdateFunction(), so we just return FUNCTION_SUCCESS.
	return FUNCTION_SUCCESS;
}





/**

@see   QAM_DEMOD_FP_RESET_FUNCTION

*/
int
rtl2840_ResetFunction(
	QAM_DEMOD_MODULE *pDemod
	)
{
	// RTL2840 does not use UpdateFunction(), so we just return FUNCTION_SUCCESS.
	return FUNCTION_SUCCESS;
}





/**

@see   QAM_DEMOD_FP_SET_QAM_MODE

*/
int
rtl2840_am_hum_en_SetQamMode(
	QAM_DEMOD_MODULE *pDemod,
	int QamMode
	)
{
	typedef struct
	{
		unsigned char PageNo;
		unsigned char RegStartAddr;
		unsigned char Msb;
		unsigned char Lsb;
		unsigned long WritingValue[QAM_QAM_MODE_NUM];
	}
	QAM_MODE_REG_ENTRY;


	typedef struct
	{
		unsigned char SpecReg0Sel;
		unsigned char SpecReg0ValueTable[QAM_QAM_MODE_NUM][RTL2840_SPEC_REG_0_VALUE_TABLE_LEN];
	}
	QAM_MODE_SPEC_REG_0_ENTRY;



	static const QAM_MODE_REG_ENTRY QamModeRegTable[RTL2840_QAM_MODE_REG_TABLE_LEN] =
	{
		// Reg,									WritingValue according to QAM mode
		// PageNo,	StartAddr,	Msb,	Lsb,	{4-Q,   16-Q,  32-Q,  64-Q,  128-Q, 256-Q, 512-Q, 1024-Q}
		{1,			0x02,		2,		0,		{0x7,   0x0,   0x1,   0x2,   0x3,   0x4,   0x5,   0x6	}},
		{1,			0x2f,		15,		5,		{0x37,  0x82,  0xb9,  0x10e, 0x177, 0x21c, 0x2ee, 0x451	}},
		{1,			0x31,		5,		0,		{0x1,   0x3,   0x4,   0x5,   0x8,   0xa,   0xf,   0x14	}},
		{1,			0x2e,		5,		0,		{0x2,   0x4,   0x6,   0x8,   0xc,   0x10,  0x18,  0x20	}},
		{1,			0x18,		7,		0,		{0x0,   0xdb,  0x79,  0x0,   0x8a,  0x0,   0x8c,  0x0	}},
		{1,			0x19,		4,		0,		{0x14,  0x14,  0xf,   0x14,  0xf,   0x14,  0xf,   0x14  }},
		{1,			0x3b,		2,		0,		{0x0,   0x0,   0x0,   0x0,   0x0,   0x0,   0x1,   0x1	}},
		{1,			0x3b,		5,		3,		{0x1,   0x1,   0x1,   0x1,   0x1,   0x1,   0x2,   0x2	}},
		{1,			0x3c,		2,		0,		{0x1,   0x1,   0x1,   0x1,   0x1,   0x1,   0x2,   0x2	}},
		{1,			0x3c,		4,		3,		{0x0,   0x0,   0x0,   0x0,   0x0,   0x0,   0x1,   0x1	}},
		{1,			0x3c,		6,		5,		{0x1,   0x1,   0x1,   0x1,   0x1,   0x1,   0x2,   0x2	}},
		{1,			0x3d,		1,		0,		{0x1,   0x1,   0x1,   0x1,   0x1,   0x1,   0x2,   0x2	}},
		{1,			0x3d,		3,		2,		{0x0,   0x0,   0x0,   0x0,   0x0,   0x0,   0x1,   0x1	}},
		{1,			0x3d,		5,		4,		{0x1,   0x1,   0x1,   0x1,   0x1,   0x1,   0x2,   0x2	}},
		{1,			0x3d,		7,		6,		{0x1,   0x1,   0x1,   0x1,   0x1,   0x1,   0x2,   0x2	}},
		{1,			0x41,		4,		3,		{0x1,   0x1,   0x1,   0x1,   0x1,   0x1,   0x1,   0x1	}},
		{1,			0x41,		6,		5,		{0x2,   0x2,   0x2,   0x2,   0x2,   0x2,   0x2,   0x2	}},
		{1,			0x42,		1,		0,		{0x3,   0x3,   0x3,   0x3,   0x3,   0x3,   0x3,   0x3	}},
		{1,			0x42,		3,		2,		{0x1,   0x1,   0x1,   0x1,   0x1,   0x1,   0x1,   0x1	}},
		{1,			0x42,		5,		4,		{0x2,   0x2,   0x2,   0x2,   0x2,   0x2,   0x2,   0x2	}},
		{1,			0x42,		7,		6,		{0x3,   0x3,   0x3,   0x3,   0x3,   0x3,   0x3,   0x3	}},


		// For AM-hum enhancement
		// Reg,									WritingValue according to QAM mode
		// PageNo,	StartAddr,	Msb,	Lsb,	{4-Q,   16-Q,  32-Q,  64-Q,  128-Q, 256-Q, 512-Q, 1024-Q}
		{1,			0x05,		7,		0,		{0x64,  0x64,  0x64,  0x64,  0x6b,  0x6b,  0x6b,  0x6b  }},
		{1,			0x40,		2,		0,		{0x1,   0x1,   0x1,   0x1,   0x1,   0x1,   0x2,   0x2	}},
		{1,			0x40,		5,		3,		{0x1,   0x1,   0x1,   0x1,   0x2,   0x2,   0x3,   0x3	}},
		{1,			0x41,		2,		0,		{0x0,   0x0,   0x0,   0x0,   0x3,   0x3,   0x4,   0x4	}},
	};


	static const QAM_MODE_SPEC_REG_0_ENTRY QamModeSpecReg0Table[RTL2840_QAM_MODE_SPEC_REG_0_TABLE_LEN] =
	{
		// SpecReg0Sel,		{SpecReg0ValueTable                                              }		   QAM mode
		{9,				{	{0x99, 0xa9, 0xc9, 0x4e, 0x48, 0x20, 0x71, 0x80, 0xfa, 0xcb, 0x00},		// 4-QAM
							{0x99, 0xa9, 0xc9, 0x4e, 0x48, 0x20, 0x71, 0x80, 0xfa, 0xcb, 0x00},		// 16-QAM
							{0x99, 0xa9, 0xc9, 0x4e, 0x48, 0x10, 0x70, 0x80, 0xfa, 0xcb, 0x00},		// 32-QAM
							{0x99, 0xa9, 0xc9, 0x4e, 0x48, 0x20, 0x71, 0x80, 0xfa, 0xcb, 0x00},		// 64-QAM
							{0x99, 0xa9, 0xc9, 0x4e, 0x48, 0x10, 0x70, 0x80, 0xfa, 0xcb, 0x00},		// 128-QAM
							{0x99, 0xa9, 0xc9, 0x4e, 0x48, 0x20, 0x71, 0x80, 0xfa, 0xcb, 0x00},		// 256-QAM
							{0x99, 0xa9, 0xc9, 0x4e, 0x48, 0x10, 0x70, 0x80, 0xfa, 0xcb, 0x00},		// 512-QAM
							{0x99, 0xa9, 0xc9, 0x4e, 0x48, 0x20, 0x71, 0x80, 0xfa, 0xcb, 0x00},	}	// 1024-QAM
		},


		// SpecReg0Sel,		{SpecReg0ValueTable                                              }		   QAM mode
		{10,			{	{0xaa, 0xbb, 0xee, 0x5e, 0x48, 0x20, 0x71, 0xa0, 0xfa, 0xcb, 0x00},		// 4-QAM
							{0xaa, 0xbb, 0xee, 0x5e, 0x48, 0x20, 0x71, 0xa0, 0xfa, 0xcb, 0x00},		// 16-QAM
							{0xaa, 0xbb, 0xee, 0x5e, 0x48, 0x10, 0x70, 0xa0, 0xfa, 0xcb, 0x00},		// 32-QAM
							{0xaa, 0xbb, 0xee, 0x5e, 0x48, 0x20, 0x71, 0xa0, 0xfa, 0xcb, 0x00},		// 64-QAM
							{0xaa, 0xbb, 0xee, 0x5e, 0x48, 0x10, 0x70, 0xa0, 0xfa, 0xcb, 0x00},		// 128-QAM
							{0xaa, 0xbb, 0xee, 0x5e, 0x48, 0x20, 0x71, 0xa0, 0xfa, 0xcb, 0x00},		// 256-QAM
							{0xaa, 0xbb, 0xee, 0x5e, 0x48, 0x10, 0x70, 0xa0, 0xfa, 0xcb, 0x00},		// 512-QAM
							{0xaa, 0xbb, 0xee, 0x5e, 0x48, 0x10, 0x70, 0xa0, 0xfa, 0xcb, 0x00},	}	// 1024-QAM
		},



		// For AM-hum enhancement
		// SpecReg0Sel,		{SpecReg0ValueTable                                              }		   QAM mode
		{12,			{	{0xc8, 0xcc, 0x40, 0x7e, 0x28, 0xda, 0x4b, 0xa0, 0xfe, 0xcd, 0x01},		// 4-QAM
							{0xc8, 0xcc, 0x40, 0x7e, 0x28, 0xda, 0x4b, 0xa0, 0xfe, 0xcd, 0x01},		// 16-QAM
							{0xc8, 0xcc, 0x40, 0x7e, 0x28, 0xda, 0x4b, 0xa0, 0xfe, 0xcd, 0x01},		// 32-QAM
							{0xc8, 0xcc, 0x40, 0x7e, 0x28, 0xda, 0x4b, 0xa0, 0xfe, 0xcd, 0x01},		// 64-QAM
							{0xc8, 0xcc, 0x00, 0x7f, 0x28, 0xda, 0x4b, 0xa0, 0xfe, 0xcd, 0x01},		// 128-QAM
							{0xc8, 0xcc, 0x00, 0x7f, 0x28, 0xda, 0x4b, 0xa0, 0xfe, 0xcd, 0x01},		// 256-QAM
							{0xc8, 0xcc, 0x00, 0x7f, 0x28, 0xda, 0x4b, 0xa0, 0xfe, 0xcd, 0x01},		// 512-QAM
							{0xc8, 0xcc, 0x00, 0x7f, 0x28, 0xda, 0x4b, 0xa0, 0xfe, 0xcd, 0x01},	}	// 1024-QAM
		},
	};


	int i;

	unsigned char PageNo;
	unsigned char RegStartAddr;
	unsigned char Msb;
	unsigned char Lsb;
	unsigned long WritingValue;

	unsigned char SpecReg0Sel;
	const unsigned char *pSpecReg0ValueTable;



	// Set demod QAM mode with QAM mode register setting table.
	for(i = 0; i < RTL2840_QAM_MODE_REG_TABLE_LEN; i++)
	{
		// Get all information from each register setting entry according to QAM mode.
		PageNo       = QamModeRegTable[i].PageNo;
		RegStartAddr = QamModeRegTable[i].RegStartAddr;
		Msb          = QamModeRegTable[i].Msb;
		Lsb          = QamModeRegTable[i].Lsb;
		WritingValue = QamModeRegTable[i].WritingValue[QamMode];

		// Set register page number.
		if(pDemod->RegAccess.Addr8Bit.SetRegPage(pDemod, PageNo) != FUNCTION_SUCCESS)
			goto error_status_set_demod_registers;

		// Set register mask bits.
		if(pDemod->RegAccess.Addr8Bit.SetRegMaskBits(pDemod, RegStartAddr, Msb, Lsb, WritingValue) != FUNCTION_SUCCESS)
			goto error_status_set_demod_registers;
	}


	// Set register page number with inner page number for QAM mode specific register 0 setting.
	if(pDemod->RegAccess.Addr8Bit.SetRegPage(pDemod, 1) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;

	// Set demod QAM mode with QAM mode specific register 0 setting table.
	for(i = 0; i < RTL2840_QAM_MODE_SPEC_REG_0_TABLE_LEN; i++)
	{
		// Get all information from each specific register 0 setting entry according to QAM mode.
		SpecReg0Sel         = QamModeSpecReg0Table[i].SpecReg0Sel;
		pSpecReg0ValueTable = QamModeSpecReg0Table[i].SpecReg0ValueTable[QamMode];

		// Set specific register 0 selection.
		if(pDemod->RegAccess.Addr8Bit.SetRegBits(pDemod, QAM_SPEC_REG_0_SEL, SpecReg0Sel) != FUNCTION_SUCCESS)
			goto error_status_set_demod_registers;

		// Set specific register 0 values.
		if(pDemod->RegAccess.Addr8Bit.SetRegBytes(pDemod, RTL2840_SPEC_REG_0_VAL_START_ADDR, pSpecReg0ValueTable, LEN_11_BYTE) != 
			FUNCTION_SUCCESS)
			goto error_status_set_demod_registers;

		// Set specific register 0 strobe.
		// Note: RTL2840 hardware will clear strobe automatically.
		if(pDemod->RegAccess.Addr8Bit.SetRegBits(pDemod, QAM_SPEC_REG_0_STROBE, ON) != FUNCTION_SUCCESS)
			goto error_status_set_demod_registers;
	}


	// Set demod QAM mode parameter.
	pDemod->QamMode      = QamMode;
	pDemod->IsQamModeSet = YES;


	return FUNCTION_SUCCESS;


error_status_set_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   I2C_BRIDGE_FP_FORWARD_I2C_READING_CMD

*/
int
rtl2840_ForwardI2cReadingCmd(
	I2C_BRIDGE_MODULE *pI2cBridge,
	unsigned char DeviceAddr,
	unsigned char *pReadingBytes,
	unsigned long ByteNum
	)
{
	QAM_DEMOD_MODULE *pDemod;
	BASE_INTERFACE_MODULE *pBaseInterface;



	// Get demod module and tuner device address.
	pDemod = (QAM_DEMOD_MODULE *)pI2cBridge->pPrivateData;

	
	// Get base interface.
	pBaseInterface = pDemod->pBaseInterface;


	// Enable demod I2C relay.
	if(pDemod->RegAccess.Addr8Bit.SetRegBitsWithPage(pDemod, QAM_OPT_I2C_RELAY, 0x1) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;


	// Send I2C reading command.
	if(pBaseInterface->I2cRead(pBaseInterface, DeviceAddr, pReadingBytes, ByteNum) != FUNCTION_SUCCESS)
		goto error_send_i2c_reading_command;


	return FUNCTION_SUCCESS;


error_send_i2c_reading_command:
error_status_set_demod_registers:
	return FUNCTION_ERROR;
}





/**

@see   I2C_BRIDGE_FP_FORWARD_I2C_WRITING_CMD

*/
int
rtl2840_ForwardI2cWritingCmd(
	I2C_BRIDGE_MODULE *pI2cBridge,
	unsigned char DeviceAddr,
	const unsigned char *pWritingBytes,
	unsigned long ByteNum
	)
{
	QAM_DEMOD_MODULE *pDemod;
	BASE_INTERFACE_MODULE *pBaseInterface;



	// Get demod module and tuner device address.
	pDemod = (QAM_DEMOD_MODULE *)pI2cBridge->pPrivateData;

	
	// Get base interface.
	pBaseInterface = pDemod->pBaseInterface;


	// Enable demod I2C relay.
	if(pDemod->RegAccess.Addr8Bit.SetRegBitsWithPage(pDemod, QAM_OPT_I2C_RELAY, 0x1) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;


	// Send I2C writing command.
	if(pBaseInterface->I2cWrite(pBaseInterface, DeviceAddr, pWritingBytes, ByteNum) != FUNCTION_SUCCESS)
		goto error_send_i2c_writing_command;


	return FUNCTION_SUCCESS;


error_send_i2c_writing_command:
error_status_set_demod_registers:
	return FUNCTION_ERROR;
}





/**

@brief   Initialize base register table

RTL2840 builder will use rtl2840_InitBaseRegTable() to initialize base register table.


@param [in]   pDemod   The demod module pointer


@see   BuildRtl2840Module()

*/
void
rtl2840_InitBaseRegTable(
	QAM_DEMOD_MODULE *pDemod
	)
{
	static const QAM_PRIMARY_BASE_REG_ENTRY_ADDR_8BIT PrimaryBaseRegTable[RTL2840_BASE_REG_TABLE_LEN] =
	{
		// Generality
		// RegBitName,						PageNo,		RegStartAddr,	Msb,	Lsb
		{QAM_SYS_VERSION,					0,			0x01,			7,		0	},
		{QAM_OPT_I2C_RELAY,					0,			0x03,			5,		5	},
		{QAM_SOFT_RESET,					0,			0x09,			0,		0	},

		// Miscellany
		// RegBitName,						PageNo,		RegStartAddr,	Msb,	Lsb
		{QAM_OPT_I2C_DRIVE_CURRENT,			0,			0x07,			7,		7	},
		{QAM_GPIO2_OEN,						0,			0x05,			6,		6	},
		{QAM_GPIO3_OEN,						0,			0x05,			7,		7	},
		{QAM_GPIO2_O,						0,			0x0a,			2,		2	},
		{QAM_GPIO3_O,						0,			0x0a,			3,		3	},
		{QAM_GPIO2_I,						0,			0x0a,			6,		6	},
		{QAM_GPIO3_I,						0,			0x0a,			7,		7	},
		{QAM_INNER_DATA_STROBE,				1,			0x69,			0,		0	},
		{QAM_INNER_DATA_SEL1,				1,			0x48,			7,		0	},
		{QAM_INNER_DATA_SEL2,				1,			0x49,			7,		0	},
		{QAM_INNER_DATA1,					1,			0x6a,			15,		0	},
		{QAM_INNER_DATA2,					1,			0x6c,			15,		0	},

		// QAM mode
		// RegBitName,						PageNo,		RegStartAddr,	Msb,	Lsb
		{QAM_QAM_MODE,						1,			0x02,			2,		0	},

		// AD
		// RegBitName,						PageNo,		RegStartAddr,	Msb,	Lsb
		{QAM_AD_AV,							0,			0x0b,			2,		0	},

		// AAGC
		// RegBitName,						PageNo,		RegStartAddr,	Msb,	Lsb
		{QAM_OPT_RF_AAGC_DRIVE_CURRENT,		0,			0x07,			0,		0	},
		{QAM_OPT_IF_AAGC_DRIVE_CURRENT,		0,			0x07,			1,		1	},
		{QAM_OPT_RF_AAGC_DRIVE,				0,			0x04,			3,		3	},
		{QAM_OPT_IF_AAGC_DRIVE,				0,			0x04,			4,		4	},
		{QAM_OPT_RF_AAGC_OEN,				0,			0x04,			6,		6	},
		{QAM_OPT_IF_AAGC_OEN,				0,			0x04,			7,		7	},
		{QAM_PAR_RF_SD_IB,					0,			0x03,			0,		0	},
		{QAM_PAR_IF_SD_IB,					0,			0x03,			1,		1	},
		{QAM_AAGC_FZ_OPTION,				1,			0x04,			5,		4	},
		{QAM_AAGC_TARGET,					1,			0x05,			7,		0	},
		{QAM_RF_AAGC_MAX,					1,			0x06,			7,		0	},
		{QAM_RF_AAGC_MIN,					1,			0x07,			7,		0	},
		{QAM_IF_AAGC_MAX,					1,			0x08,			7,		0	},
		{QAM_IF_AAGC_MIN,					1,			0x09,			7,		0	},
		{QAM_VTOP,							1,			0x0b,			7,		0	},
		{QAM_KRF_MSB,						1,			0x0c,			6,		3	},
		{QAM_KRF_LSB,						1,			0x04,			7,		6	},
		{QAM_AAGC_MODE_SEL,					1,			0x0c,			7,		7	},
		{QAM_AAGC_LD,						1,			0x72,			0,		0	},
		{QAM_AAGC_INIT_LEVEL,				1,			0x0a,			7,		0	},

		// DDC
		// RegBitName,						PageNo,		RegStartAddr,	Msb,	Lsb
		{QAM_DDC_FREQ,						1,			0x0d,			14,		0	},
		{QAM_SPEC_INV,						1,			0x0e,			7,		7	},

		// Timing recovery
		// RegBitName,						PageNo,		RegStartAddr,	Msb,	Lsb
		{QAM_TR_DECI_RATIO,					1,			0x1f,			23,		0	},

		// Carrier recovery
		// RegBitName,						PageNo,		RegStartAddr,	Msb,	Lsb
		{QAM_CR_LD,							1,			0x74,			5,		0	},

		// Equalizer
		// RegBitName,						PageNo,		RegStartAddr,	Msb,	Lsb
		{QAM_EQ_LD,							1,			0x72,			1,		1	},
		{QAM_MSE,							1,			0x76,			21,		0	},

		// Frame sync. indicator
		// RegBitName,						PageNo,		RegStartAddr,	Msb,	Lsb
		{QAM_SYNCLOST,						2,			0x02,			7,		7	},

		// BER
		// RegBitName,						PageNo,		RegStartAddr,	Msb,	Lsb
		{QAM_BER_RD_STROBE,					2,			0x05,			7,		7	},
		{QAM_BERT_EN,						2,			0x06,			0,		0	},
		{QAM_BERT_HOLD,						2,			0x06,			1,		1	},
		{QAM_DIS_AUTO_MODE,					2,			0x06,			2,		2	},
		{QAM_TEST_VOLUME,					2,			0x06,			5,		3	},
		{QAM_BER_REG0,						2,			0x0e,			15,		0	},
		{QAM_BER_REG1,						2,			0x07,			20,		0	},
		{QAM_BER_REG2_15_0,					2,			0x0a,			15,		0	},
		{QAM_BER_REG2_18_16,				2,			0x09,			7,		5	},

		// MPEG TS output interface
		// RegBitName,						PageNo,		RegStartAddr,	Msb,	Lsb
		{QAM_CKOUTPAR,						2,			0x11,			0,		0	},
		{QAM_CKOUT_PWR,						2,			0x11,			1,		1	},
		{QAM_CDIV_PH0,						2,			0x12,			3,		0	},
		{QAM_CDIV_PH1,						2,			0x12,			7,		4	},
		{QAM_MPEG_OUT_EN,					0,			0x04,			5,		5	},
		{QAM_OPT_MPEG_DRIVE_CURRENT,		0,			0x07,			2,		2	},
		{QAM_NO_REINVERT,					2,			0x10,			2,		2	},
		{QAM_FIX_TEI,						2,			0x10,			3,		3	},
		{QAM_SERIAL,						2,			0x11,			2,		2	},

		// Monitor
		// RegBitName,						PageNo,		RegStartAddr,	Msb,	Lsb
		{QAM_ADC_CLIP_CNT_REC,				1,			0x6a,			15,		4	},
		{QAM_DAGC_LEVEL_26_11,				1,			0x6a,			15,		0	},
		{QAM_DAGC_LEVEL_10_0,				1,			0x6c,			15,		5	},
		{QAM_RF_AAGC_SD_IN,					1,			0x6a,			15,		5	},
		{QAM_IF_AAGC_SD_IN,					1,			0x6c,			15,		5	},
		{QAM_KI_TR_OUT_30_15,				1,			0x6a,			15,		0	},
		{QAM_KI_TR_OUT_14_0,				1,			0x6c,			15,		1	},
		{QAM_KI_CR_OUT_15_0,				1,			0x6a,			15,		0	},
		{QAM_KI_CR_OUT_31_16,				1,			0x6c,			15,		0	},

		// Specific register
		// RegBitName,						PageNo,		RegStartAddr,	Msb,	Lsb
		{QAM_SPEC_SIGNAL_INDICATOR,			1,			0x73,			5,		3	},
		{QAM_SPEC_ALPHA_STROBE,				1,			0x57,			0,		0	},
		{QAM_SPEC_ALPHA_SEL,				1,			0x57,			4,		1	},
		{QAM_SPEC_ALPHA_VAL,				1,			0x57,			14,		5	},
		{QAM_SPEC_SYMBOL_RATE_REG_0,		1,			0x0f,			2,		0	},
		{QAM_SPEC_SYMBOL_RATE_STROBE,		1,			0x5b,			0,		0	},
		{QAM_SPEC_SYMBOL_RATE_SEL,			1,			0x5b,			4,		1	},
		{QAM_SPEC_SYMBOL_RATE_VAL,			1,			0x5b,			14,		5	},
		{QAM_SPEC_REG_0_STROBE,				1,			0x5d,			0,		0	},
		{QAM_SPEC_REG_0_SEL,				1,			0x5d,			4,		1	},

		// Specific register for initialization
		// RegBitName,						PageNo,		RegStartAddr,	Msb,	Lsb
		{QAM_SPEC_INIT_A0,					1,			0x6a,			15,		6	},
		{QAM_SPEC_INIT_A1,					0,			0x0f,			0,		0	},
		{QAM_SPEC_INIT_A2,					1,			0x2b,			1,		1	},

		// Pseudo register for test only
		// RegBitName,						PageNo,		RegStartAddr,	Msb,	Lsb
		{QAM_TEST_REG_0,					1,			0x17,			6,		2	},
		{QAM_TEST_REG_1,					1,			0x17,			14,		1	},
		{QAM_TEST_REG_2,					1,			0x17,			21,		3	},
		{QAM_TEST_REG_3,					1,			0x17,			30,		2	},
	};


	int i;
	int RegBitName;



	// Initialize base register table according to primary base register table.
	// Note: 1. Base register table rows are sorted by register bit name key.
	//       2. The default value of the IsAvailable variable is "NO".
	for(i = 0; i < QAM_BASE_REG_TABLE_LEN_MAX; i++)
		pDemod->BaseRegTable.Addr8Bit[i].IsAvailable  = NO;

	for(i = 0; i < RTL2840_BASE_REG_TABLE_LEN; i++)
	{
		RegBitName = PrimaryBaseRegTable[i].RegBitName;

		pDemod->BaseRegTable.Addr8Bit[RegBitName].IsAvailable  = YES;
		pDemod->BaseRegTable.Addr8Bit[RegBitName].PageNo       = PrimaryBaseRegTable[i].PageNo;
		pDemod->BaseRegTable.Addr8Bit[RegBitName].RegStartAddr = PrimaryBaseRegTable[i].RegStartAddr;
		pDemod->BaseRegTable.Addr8Bit[RegBitName].Msb          = PrimaryBaseRegTable[i].Msb;
		pDemod->BaseRegTable.Addr8Bit[RegBitName].Lsb          = PrimaryBaseRegTable[i].Lsb;
	}


	return;
}





/**

@brief   Initialize monitor register table

RTL2840 builder will use rtl2840_InitMonitorRegTable() to initialize monitor register table.


@param [in]   pDemod   The demod module pointer


@see   BuildRtl2840Module()

*/
void
rtl2840_InitMonitorRegTable(
	QAM_DEMOD_MODULE *pDemod
	)
{
	static const QAM_PRIMARY_MONITOR_REG_ENTRY_ADDR_8BIT PrimaryMonitorRegTable[RTL2840_MONITOR_REG_TABLE_LEN] =
	{
		// Generality
		// MonitorRegBitName,		InfoNum,		{SelRegAddr,	SelValue,	RegBitName,				Shift	}
		{QAM_ADC_CLIP_CNT,			1,			{	{0x48,			0x01,		QAM_ADC_CLIP_CNT_REC,	0		},
													{NO_USE,		NO_USE,		NO_USE,					NO_USE	},	}},

		{QAM_DAGC_VALUE,			2,			{	{0x48,			0x20,		QAM_DAGC_LEVEL_26_11,	11		},
													{0x49,			0x20,		QAM_DAGC_LEVEL_10_0,	0		},	}},

		{QAM_RF_AGC_VALUE,			1,			{	{0x48,			0x80,		QAM_RF_AAGC_SD_IN,		0		},
													{NO_USE,		NO_USE,		NO_USE,					NO_USE	},	}},

		{QAM_IF_AGC_VALUE,			1,			{	{0x49,			0x80,		QAM_IF_AAGC_SD_IN,		0		},
													{NO_USE,		NO_USE,		NO_USE,					NO_USE	},	}},

		{QAM_TR_OFFSET,				2,			{	{0x48,			0xc2,		QAM_KI_TR_OUT_30_15,	15		},
													{0x49,			0xc2,		QAM_KI_TR_OUT_14_0,		0		},	}},

		{QAM_CR_OFFSET,				2,			{	{0x48,			0xc3,		QAM_KI_CR_OUT_15_0,		0		},
													{0x49,			0xc3,		QAM_KI_CR_OUT_31_16,	16		},	}},

		// Specific monitor register for initialization
		// MonitorRegBitName,		InfoNum,		{SelRegAddr,	SelValue,	RegBitName,				Shift	}
		{QAM_SPEC_MONITER_INIT_0,	1,			{	{0x48,			0x00,		QAM_SPEC_INIT_A0,		0		},
													{NO_USE,		NO_USE,		NO_USE,					NO_USE	},	}},
	};


	int i, j;
	int MonitorRegBitName;



	// Initialize monitor register table according to primary monitor register table.
	// Note: 1. Monitor register table rows are sorted by monitor register name key.
	//       2. The default value of the IsAvailable variable is "NO".
	for(i = 0; i < QAM_MONITOR_REG_TABLE_LEN_MAX; i++)
		pDemod->MonitorRegTable.Addr8Bit[i].IsAvailable  = NO;

	for(i = 0; i < RTL2840_MONITOR_REG_TABLE_LEN; i++)
	{
		MonitorRegBitName = PrimaryMonitorRegTable[i].MonitorRegBitName;

		pDemod->MonitorRegTable.Addr8Bit[MonitorRegBitName].IsAvailable = YES;
		pDemod->MonitorRegTable.Addr8Bit[MonitorRegBitName].InfoNum = PrimaryMonitorRegTable[i].InfoNum;

		for(j = 0; j < QAM_MONITOR_REG_INFO_TABLE_LEN; j++)
		{
			pDemod->MonitorRegTable.Addr8Bit[MonitorRegBitName].InfoTable[j].SelRegAddr =
				PrimaryMonitorRegTable[i].InfoTable[j].SelRegAddr;
			pDemod->MonitorRegTable.Addr8Bit[MonitorRegBitName].InfoTable[j].SelValue =
				PrimaryMonitorRegTable[i].InfoTable[j].SelValue;
			pDemod->MonitorRegTable.Addr8Bit[MonitorRegBitName].InfoTable[j].RegBitName =
				PrimaryMonitorRegTable[i].InfoTable[j].RegBitName;
			pDemod->MonitorRegTable.Addr8Bit[MonitorRegBitName].InfoTable[j].Shift =
				PrimaryMonitorRegTable[i].InfoTable[j].Shift;
		}
	}


	return;
}





/**

@brief   Get inner strobe register bits.

RTL2840 upper level functions will use rtl2840_GetInnerStrobeRegBits() to get register bits with inner strobe.


@param [in]    pDemod          The demod module pointer
@param [in]    RegBitName      Pre-defined demod register bit name
@param [out]   pReadingValue   Pointer to an allocated memory for storing reading value


@retval   FUNCTION_SUCCESS   Get demod register bits successfully with bit name and inner strobe.
@retval   FUNCTION_ERROR     Get demod register bits unsuccessfully.


@note
	-# Don't need to set register page before using rtl2840_GetInnerStrobeRegBits().

*/
int
rtl2840_GetInnerStrobeRegBits(
	QAM_DEMOD_MODULE *pDemod,
	int RegBitName,
	unsigned long *pReadingValue
	)
{
	// Set register page number with inner page number.
	if(pDemod->RegAccess.Addr8Bit.SetRegPage(pDemod, 1) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;


	// Set inner data strobe.
	// Note: RTL2840 hardware will clear strobe automatically.
	if(pDemod->RegAccess.Addr8Bit.SetRegBits(pDemod, QAM_INNER_DATA_STROBE, ON) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;


	// Get the inner strobe register bits.
	if(pDemod->RegAccess.Addr8Bit.GetRegBits(pDemod, RegBitName, pReadingValue) != FUNCTION_SUCCESS)
		goto error_status_get_demod_registers;


	return FUNCTION_SUCCESS;


error_status_set_demod_registers:
error_status_get_demod_registers:
	return FUNCTION_ERROR;
}





/**

@brief   Get monitor register bits.

RTL2840 upper level functions will use rtl2840_GetMonitorRegBits() to get monitor register bits.


@param [in]    pDemod              The demod module pointer
@param [in]    MonitorRegBitName   Pre-defined demod monitor register bit name
@param [out]   pReadingValue       Pointer to an allocated memory for storing reading value


@retval   FUNCTION_SUCCESS   Get demod monitor register bits successfully with monitor bit name.
@retval   FUNCTION_ERROR     Get demod monitor register bits unsuccessfully.


@note
	-# Don't need to set register page before using rtl2840_GetMonitorRegBits().

*/
int
rtl2840_GetMonitorRegBits(
	QAM_DEMOD_MODULE *pDemod,
	int MonitorRegBitName,
	unsigned long *pReadingValue
	)
{
	int i;

	unsigned char InfoNum;
	unsigned char SelRegAddr;
	unsigned char SelValue;
	int RegBitName;
	unsigned char Shift;
	
	unsigned long Buffer[QAM_MONITOR_REG_INFO_TABLE_LEN];



	// Check if register bit name is available.
	if(pDemod->MonitorRegTable.Addr8Bit[MonitorRegBitName].IsAvailable == NO)
		goto error_status_monitor_register_bit_name;


	// Get information entry number from monitor register table by monitor register name key.
	InfoNum = pDemod->MonitorRegTable.Addr8Bit[MonitorRegBitName].InfoNum;


	// Set register page number with inner page number.
	if(pDemod->RegAccess.Addr8Bit.SetRegPage(pDemod, 1) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;


	// Set selection register with selection value for each information entry.
	for(i = 0; i < InfoNum; i++)
	{
		// Get selection register address and value from information entry by monitor register name key.
		SelRegAddr = pDemod->MonitorRegTable.Addr8Bit[MonitorRegBitName].InfoTable[i].SelRegAddr;
		SelValue   = pDemod->MonitorRegTable.Addr8Bit[MonitorRegBitName].InfoTable[i].SelValue;

		// Set selection register with selection value.
		if(pDemod->RegAccess.Addr8Bit.SetRegBytes(pDemod, SelRegAddr, &SelValue, LEN_1_BYTE) != FUNCTION_SUCCESS)
			goto error_status_set_demod_registers;
	}


	// Set inner data strobe.
	// Note: RTL2840 hardware will clear strobe automatically.
	if(pDemod->RegAccess.Addr8Bit.SetRegBits(pDemod, QAM_INNER_DATA_STROBE, ON) != FUNCTION_SUCCESS)
		goto error_status_set_demod_registers;


	// Get register bits to buffer according to register bit names for each information entry.
	for(i = 0; i < InfoNum; i++)
	{
		// Get register bit name from information entry by monitor register name key.
		RegBitName = pDemod->MonitorRegTable.Addr8Bit[MonitorRegBitName].InfoTable[i].RegBitName;

		// Get register bits and store it to buffer.
		if(pDemod->RegAccess.Addr8Bit.GetRegBits(pDemod, RegBitName, &Buffer[i]) != FUNCTION_SUCCESS)
			goto error_status_get_demod_registers;
	}


	// Combine the buffer values into reading value.
	*pReadingValue = 0;

	for(i = 0; i < InfoNum; i++)
	{
		// Get shift from information entry by monitor register name key.
		Shift = pDemod->MonitorRegTable.Addr8Bit[MonitorRegBitName].InfoTable[i].Shift;

		// Combine the buffer values into reading value with shift.
		*pReadingValue |= Buffer[i] << Shift;
	}


	return FUNCTION_SUCCESS;


error_status_set_demod_registers:
error_status_get_demod_registers:
error_status_monitor_register_bit_name:
	return FUNCTION_ERROR;
}





/**

@brief   Set I2C bridge module demod arguments.

RTL2840 builder will use rtl2840_BuildI2cBridgeModule() to set I2C bridge module demod arguments.


@param [in]   pDemod   The demod module pointer


@see   BuildRtl2840Module()

*/
void
rtl2840_BuildI2cBridgeModule(
	QAM_DEMOD_MODULE *pDemod
	)
{
	I2C_BRIDGE_MODULE *pI2cBridge;



	// Get I2C bridge module.
	pI2cBridge = pDemod->pI2cBridge;

	// Set I2C bridge module demod arguments.
	pI2cBridge->pPrivateData = (void *)pDemod;
	pI2cBridge->ForwardI2cReadingCmd = rtl2840_ForwardI2cReadingCmd;
	pI2cBridge->ForwardI2cWritingCmd = rtl2840_ForwardI2cWritingCmd;


	return;
}












