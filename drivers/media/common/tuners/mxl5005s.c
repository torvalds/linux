/*
 * For the Realtek RTL chip RTL2831U
 * Realtek Release Date: 2008-03-14, ver 080314
 * Realtek version RTL2831 Linux driver version 080314
 * ver 080314
 *
 * for linux kernel version 2.6.21.4 - 2.6.22-14
 * support MXL5005s and MT2060 tuners (support tuner auto-detecting)
 * support two IR types -- RC5 and NEC
 *
 * Known boards with Realtek RTL chip RTL2821U
 *    Freecom USB stick 14aa:0160 (version 4)
 *    Conceptronic CTVDIGRCU
 *
 * Copyright (c) 2008 Realtek
 * Copyright (c) 2008 Jan Hoogenraad, Barnaby Shearer, Andy Hasper
 * This code is placed under the terms of the GNU General Public License
 *
 * Released by Realtek under GPLv2.
 * Thanks to Realtek for a lot of support we received !
 *
 *  Revision: 080314 - original version
 */


/**

@file

@brief   MxL5005S tuner module definition

One can manipulate MxL5005S tuner through MxL5005S module.
MxL5005S module is derived from tuner module.

*/


#include "tuner_mxl5005s.h"
#include "tuner_demod_io.h"






/**

@defgroup   MXL5005S_TUNER_MODULE   MxL5005S tuner module

MxL5005S tuner module is drived from tuner base module.

@see TUNER_BASE_MODULE

*/





/**

@defgroup   MXL5005S_MODULE_BUILDER   MxL5005S module builder
@ingroup    MXL5005S_TUNER_MODULE

One should call MxL5005S module builder before using MxL5005S module.

*/
/// @{





/**

@brief   MxL5005S tuner module builder

Use BuildMxl5005sModule() to build MxL5005S module, set all module function pointers with the corresponding functions,
and initialize module private variables.


@param [in]   ppTuner                      Pointer to MxL5005S tuner module pointer
@param [in]   pTunerModuleMemory           Pointer to an allocated tuner module memory
@param [in]   pMxl5005sExtraModuleMemory   Pointer to an allocated MxL5005S extra module memory
@param [in]   pI2cBridgeModuleMemory       Pointer to an allocated I2C bridge module memory
@param [in]   DeviceAddr                   MxL5005S I2C device address
@param [in]   CrystalFreqHz                MxL5005S crystal frequency in Hz


@note \n
	-# One should call BuildMxl5005sModule() to build MxL5005S module before using it.

*/
void
BuildMxl5005sModule(
	TUNER_MODULE **ppTuner,
	TUNER_MODULE *pTunerModuleMemory,
	MXL5005S_EXTRA_MODULE *pMxl5005sExtraModuleMemory,
	BASE_INTERFACE_MODULE *pBaseInterfaceModuleMemory,
	I2C_BRIDGE_MODULE *pI2cBridgeModuleMemory,
	unsigned char DeviceAddr,
	int StandardMode
	)
{
	MXL5005S_EXTRA_MODULE *pExtra;

	int            MxlModMode;
	int            MxlIfMode;
	unsigned long  MxlBandwitdh;
	unsigned long  MxlIfFreqHz;
	unsigned long  MxlCrystalFreqHz;
	int            MxlAgcMode;
	unsigned short MxlTop;
	unsigned short MxlIfOutputLoad;
	int            MxlClockOut;
	int            MxlDivOut;
	int            MxlCapSel;
	int            MxlRssiOnOff;
	unsigned char  MxlStandard;
	unsigned char  MxlTfType;



	// Set tuner module pointer, tuner extra module pointer, and I2C bridge module pointer.
	*ppTuner = pTunerModuleMemory;
	(*ppTuner)->pExtra = pMxl5005sExtraModuleMemory;
	(*ppTuner)->pBaseInterface = pBaseInterfaceModuleMemory;
	(*ppTuner)->pI2cBridge = pI2cBridgeModuleMemory;

	// Get tuner extra module pointer.
	pExtra = (MXL5005S_EXTRA_MODULE *)(*ppTuner)->pExtra;


	// Set I2C bridge tuner arguments.
	mxl5005s_SetI2cBridgeModuleTunerArg(*ppTuner);


	// Set tuner module manipulating function pointers.
	(*ppTuner)->SetDeviceAddr = mxl5005s_SetDeviceAddr;

	(*ppTuner)->GetTunerType  = mxl5005s_GetTunerType;
	(*ppTuner)->GetDeviceAddr = mxl5005s_GetDeviceAddr;

	(*ppTuner)->Initialize    = mxl5005s_Initialize;
	(*ppTuner)->SetRfFreqHz   = mxl5005s_SetRfFreqHz;
	(*ppTuner)->GetRfFreqHz   = mxl5005s_GetRfFreqHz;


	// Set tuner extra module manipulating function pointers.
	pExtra->SetRegsWithTable = mxl5005s_SetRegsWithTable;
	pExtra->SetRegMaskBits   = mxl5005s_SetRegMaskBits;
	pExtra->SetSpectrumMode  = mxl5005s_SetSpectrumMode;
	pExtra->SetBandwidthHz   = mxl5005s_SetBandwidthHz;


	// Initialize tuner parameter setting status.
	(*ppTuner)->IsDeviceAddrSet    = NO;
	(*ppTuner)->IsRfFreqHzSet      = NO;


	// Set MxL5005S parameters.
	MxlModMode       = MXL_DIGITAL_MODE;
	MxlIfMode        = MXL_ZERO_IF;
	MxlBandwitdh     = MXL5005S_BANDWIDTH_8MHZ;
	MxlIfFreqHz      = IF_FREQ_4570000HZ;
	MxlCrystalFreqHz = CRYSTAL_FREQ_16000000HZ;
	MxlAgcMode       = MXL_SINGLE_AGC;
	MxlTop           = MXL5005S_TOP_25P2;
	MxlIfOutputLoad  = MXL5005S_IF_OUTPUT_LOAD_200_OHM;
	MxlClockOut      = MXL_CLOCK_OUT_DISABLE;
	MxlDivOut        = MXL_DIV_OUT_4;
	MxlCapSel        = MXL_CAP_SEL_ENABLE;
	MxlRssiOnOff     = MXL_RSSI_ENABLE;
	MxlTfType        = MXL_TF_C_H;


	// Set MxL5005S parameters according to standard mode
	switch(StandardMode)
	{
		default:
		case MXL5005S_STANDARD_DVBT:	MxlStandard = MXL_DVBT;			break;
		case MXL5005S_STANDARD_ATSC:	MxlStandard = MXL_ATSC;			break;
	}


	// Set MxL5005S extra module.
	pExtra->AgcMasterByte = (MxlAgcMode == MXL_DUAL_AGC) ? 0x4 : 0x0;

	MXL5005_TunerConfig(&pExtra->MxlDefinedTunerStructure, (unsigned char)MxlModMode, (unsigned char)MxlIfMode,
		MxlBandwitdh, MxlIfFreqHz, MxlCrystalFreqHz, (unsigned char)MxlAgcMode, MxlTop, MxlIfOutputLoad,
		(unsigned char)MxlClockOut, (unsigned char)MxlDivOut, (unsigned char)MxlCapSel, (unsigned char)MxlRssiOnOff,
		MxlStandard, MxlTfType);



	// Note: Need to set all module arguments before using module functions.


	// Set tuner type.
	(*ppTuner)->TunerType = TUNER_TYPE_MXL5005S;

	// Set tuner I2C device address.
	(*ppTuner)->SetDeviceAddr(*ppTuner, DeviceAddr);


	return;
}





/// @}





/**

@defgroup   MXL5005S_MANIPULATING_FUNCTIONS   MxL5005S manipulating functions derived from tuner base module
@ingroup    MXL5005S_TUNER_MODULE

One can use the MxL5005S tuner module manipulating interface implemented by MxL5005S manipulating functions to
manipulate MxL5005S tuner.

*/
/// @{





/**

@brief   Set MxL5005S tuner I2C device address.

@note \n
	-# MxL5005S tuner builder will set TUNER_FP_SET_DEVICE_ADDR() function pointer with mxl5005s_SetDeviceAddr().

@see   TUNER_FP_SET_DEVICE_ADDR

*/
void
mxl5005s_SetDeviceAddr(
	TUNER_MODULE *pTuner,
	unsigned char DeviceAddr
	)
{
	// Set tuner I2C device address.
	pTuner->DeviceAddr      = DeviceAddr;
	pTuner->IsDeviceAddrSet = YES;


	return;
}





/**

@brief   Get MxL5005S tuner type.

@note \n
	-# MxL5005S tuner builder will set TUNER_FP_GET_TUNER_TYPE() function pointer with mxl5005s_GetTunerType().

@see   TUNER_FP_GET_TUNER_TYPE

*/
void
mxl5005s_GetTunerType(
	TUNER_MODULE *pTuner,
	int *pTunerType
	)
{
	// Get tuner type from tuner module.
	*pTunerType = pTuner->TunerType;


	return;
}





/**

@brief   Get MxL5005S tuner I2C device address.

@note \n
	-# MxL5005S tuner builder will set TUNER_FP_GET_DEVICE_ADDR() function pointer with mxl5005s_GetDeviceAddr().

@see   TUNER_FP_GET_DEVICE_ADDR

*/
int
mxl5005s_GetDeviceAddr(
	TUNER_MODULE *pTuner,
	unsigned char *pDeviceAddr
	)
{
	// Get tuner I2C device address from tuner module.
	if(pTuner->IsDeviceAddrSet != YES)
		goto error_status_get_tuner_i2c_device_addr;

	*pDeviceAddr = pTuner->DeviceAddr;


	return FUNCTION_SUCCESS;


error_status_get_tuner_i2c_device_addr:
	return FUNCTION_ERROR;
}





/**

@brief   Initialize MxL5005S tuner.

@note \n
	-# MxL5005S tuner builder will set TUNER_FP_INITIALIZE() function pointer with mxl5005s_Initialize().

@see   TUNER_FP_INITIALIZE

*/
int
mxl5005s_Initialize(
	struct dvb_usb_device*        dib,
	TUNER_MODULE *pTuner
	)
{
	MXL5005S_EXTRA_MODULE *pExtra;

	unsigned char AgcMasterByte;
	unsigned char AddrTable[MXL5005S_REG_WRITING_TABLE_LEN_MAX];
	unsigned char ByteTable[MXL5005S_REG_WRITING_TABLE_LEN_MAX];
	int TableLen;



	// Get tuner extra module.
	pExtra = (MXL5005S_EXTRA_MODULE *)pTuner->pExtra;


	// Get AGC master byte
	AgcMasterByte = pExtra->AgcMasterByte;


	// Initialize MxL5005S tuner according to MxL5005S tuner example code.

	// Tuner initialization stage 0
	MXL_GetMasterControl(ByteTable, MC_SYNTH_RESET);
	AddrTable[0] = MASTER_CONTROL_ADDR;
	ByteTable[0] |= AgcMasterByte;

	if(pExtra->SetRegsWithTable( dib,pTuner, AddrTable, ByteTable, LEN_1_BYTE) != FUNCTION_SUCCESS)
		goto error_status_set_tuner_registers;


	// Tuner initialization stage 1
	MXL_GetInitRegister(&pExtra->MxlDefinedTunerStructure, AddrTable, ByteTable, &TableLen);

	if(pExtra->SetRegsWithTable( dib,pTuner, AddrTable, ByteTable, TableLen) != FUNCTION_SUCCESS)
		goto error_status_set_tuner_registers;


	return FUNCTION_SUCCESS;


error_status_set_tuner_registers:
	return FUNCTION_ERROR;
}





/**

@brief   Set MxL5005S tuner RF frequency in Hz.

@note \n
	-# MxL5005S tuner builder will set TUNER_FP_SET_RF_FREQ_HZ() function pointer with mxl5005s_SetRfFreqHz().

@see   TUNER_FP_SET_RF_FREQ_HZ

*/
int
mxl5005s_SetRfFreqHz(
	struct dvb_usb_device*        dib,
	TUNER_MODULE *pTuner,
	unsigned long RfFreqHz
	)
{
	MXL5005S_EXTRA_MODULE *pExtra;
	BASE_INTERFACE_MODULE *pBaseInterface;

	unsigned char AgcMasterByte;
	unsigned char AddrTable[MXL5005S_REG_WRITING_TABLE_LEN_MAX];
	unsigned char ByteTable[MXL5005S_REG_WRITING_TABLE_LEN_MAX];
	int TableLen;

	unsigned long IfDivval;
	unsigned char MasterControlByte;



	// Get tuner extra module and base interface module.
	pExtra = (MXL5005S_EXTRA_MODULE *)pTuner->pExtra;
	pBaseInterface = pTuner->pBaseInterface;


	// Get AGC master byte
	AgcMasterByte = pExtra->AgcMasterByte;


	// Set MxL5005S tuner RF frequency according to MxL5005S tuner example code.

	// Tuner RF frequency setting stage 0
	MXL_GetMasterControl(ByteTable, MC_SYNTH_RESET) ;
	AddrTable[0] = MASTER_CONTROL_ADDR;
	ByteTable[0] |= AgcMasterByte;

	if(pExtra->SetRegsWithTable( dib,pTuner, AddrTable, ByteTable, LEN_1_BYTE) != FUNCTION_SUCCESS)
		goto error_status_set_tuner_registers;


	// Tuner RF frequency setting stage 1
	MXL_TuneRF(&pExtra->MxlDefinedTunerStructure, RfFreqHz);

	MXL_ControlRead(&pExtra->MxlDefinedTunerStructure, IF_DIVVAL, &IfDivval);

	MXL_ControlWrite(&pExtra->MxlDefinedTunerStructure, SEQ_FSM_PULSE, 0);
	MXL_ControlWrite(&pExtra->MxlDefinedTunerStructure, SEQ_EXTPOWERUP, 1);
	MXL_ControlWrite(&pExtra->MxlDefinedTunerStructure, IF_DIVVAL, 8);

	MXL_GetCHRegister(&pExtra->MxlDefinedTunerStructure, AddrTable, ByteTable, &TableLen) ;

	MXL_GetMasterControl(&MasterControlByte, MC_LOAD_START) ;
	AddrTable[TableLen] = MASTER_CONTROL_ADDR ;
	ByteTable[TableLen] = MasterControlByte | AgcMasterByte;
	TableLen += 1;

	if(pExtra->SetRegsWithTable( dib,pTuner, AddrTable, ByteTable, TableLen) != FUNCTION_SUCCESS)
		goto error_status_set_tuner_registers;


	// Wait 30 ms.
	pBaseInterface->WaitMs(pBaseInterface, 30);


	// Tuner RF frequency setting stage 2
	MXL_ControlWrite(&pExtra->MxlDefinedTunerStructure, SEQ_FSM_PULSE, 1) ;
	MXL_ControlWrite(&pExtra->MxlDefinedTunerStructure, IF_DIVVAL, IfDivval) ;
	MXL_GetCHRegister_ZeroIF(&pExtra->MxlDefinedTunerStructure, AddrTable, ByteTable, &TableLen) ;

	MXL_GetMasterControl(&MasterControlByte, MC_LOAD_START) ;
	AddrTable[TableLen] = MASTER_CONTROL_ADDR ;
	ByteTable[TableLen] = MasterControlByte | AgcMasterByte ;
	TableLen += 1;

	if(pExtra->SetRegsWithTable( dib,pTuner, AddrTable, ByteTable, TableLen) != FUNCTION_SUCCESS)
		goto error_status_set_tuner_registers;


	// Set tuner RF frequency parameter.
	pTuner->RfFreqHz      = RfFreqHz;
	pTuner->IsRfFreqHzSet = YES;


	return FUNCTION_SUCCESS;


error_status_set_tuner_registers:
	return FUNCTION_ERROR;
}





/**

@brief   Get MxL5005S tuner RF frequency in Hz.

@note \n
	-# MxL5005S tuner builder will set TUNER_FP_GET_RF_FREQ_HZ() function pointer with mxl5005s_GetRfFreqHz().

@see   TUNER_FP_GET_RF_FREQ_HZ

*/
int
mxl5005s_GetRfFreqHz(
	struct dvb_usb_device*        dib,
	TUNER_MODULE *pTuner,
	unsigned long *pRfFreqHz
	)
{
	// Get tuner RF frequency in Hz from tuner module.
	if(pTuner->IsRfFreqHzSet != YES)
		goto error_status_get_tuner_rf_frequency;

	*pRfFreqHz = pTuner->RfFreqHz;


	return FUNCTION_SUCCESS;


error_status_get_tuner_rf_frequency:
	return FUNCTION_ERROR;
}





/**

@brief   Set MxL5005S tuner registers with table.

*/
/*
int
mxl5005s_SetRegsWithTable(
	struct dvb_usb_device*        dib,
	TUNER_MODULE *pTuner,
	unsigned char *pAddrTable,
	unsigned char *pByteTable,
	int TableLen
	)
{
	BASE_INTERFACE_MODULE *pBaseInterface;
	I2C_BRIDGE_MODULE *pI2cBridge;
	unsigned char WritingByteNumMax;

	int i;
	unsigned char WritingBuffer[I2C_BUFFER_LEN];
	unsigned char WritingIndex;



	// Get base interface, I2C bridge, and maximum writing byte number.
	pBaseInterface    = pTuner->pBaseInterface;
	pI2cBridge        = pTuner->pI2cBridge;
	WritingByteNumMax = pBaseInterface->I2cWritingByteNumMax;


	// Set registers with table.
	// Note: 1. The I2C format of MxL5005S is described as follows:
	//          start_bit + (device_addr | writing_bit) + (register_addr + writing_byte) * n + stop_bit
	//          ...
	//          start_bit + (device_addr | writing_bit) + (register_addr + writing_byte) * m + latch_byte + stop_bit
	//       2. The latch_byte is 0xfe.
	//       3. The following writing byte separating scheme takes latch_byte as two byte data.
	for(i = 0, WritingIndex = 0; i < TableLen; i++)
	{
		// Put register address and register byte value into writing buffer.
		WritingBuffer[WritingIndex]     = pAddrTable[i];
		WritingBuffer[WritingIndex + 1] = pByteTable[i];
		WritingIndex += 2;

		// If writing buffer is full, send the I2C writing command with writing buffer.
		if(WritingIndex > (WritingByteNumMax - 2))
		{
			if(pI2cBridge->ForwardI2cWritingCmd(pI2cBridge, WritingBuffer, WritingIndex) != FUNCTION_SUCCESS)
				goto error_status_set_tuner_registers;

			WritingIndex = 0;
		}
	}


	// Send the last I2C writing command with writing buffer and latch byte.
	WritingBuffer[WritingIndex] = MXL5005S_LATCH_BYTE;
	WritingIndex += 1;

	if(pI2cBridge->ForwardI2cWritingCmd(pI2cBridge, WritingBuffer, WritingIndex) != FUNCTION_SUCCESS)
		goto error_status_set_tuner_registers;


	return FUNCTION_SUCCESS;


error_status_set_tuner_registers:
	return FUNCTION_ERROR;
}
*/


int
mxl5005s_SetRegsWithTable(
	struct dvb_usb_device*        dib,
	TUNER_MODULE *pTuner,
	unsigned char *pAddrTable,
	unsigned char *pByteTable,
	int TableLen
	)
{
	int	i;
	u8	end_two_bytes_buf[]={ 0 , 0 };
	u8	tuner_addr=0x00;

	pTuner->GetDeviceAddr(pTuner , &tuner_addr);

	for( i = 0 ; i < TableLen - 1 ; i++)
	{
		if ( TUNER_WI2C(dib , tuner_addr , pAddrTable[i] , &pByteTable[i] , 1 ) )
				return FUNCTION_ERROR;
	}

	end_two_bytes_buf[0] = pByteTable[i];
	end_two_bytes_buf[1] = MXL5005S_LATCH_BYTE;

	if ( TUNER_WI2C(dib , tuner_addr , pAddrTable[i] , end_two_bytes_buf , 2 ) )
			return FUNCTION_ERROR;

	return FUNCTION_SUCCESS;
}





/**

@brief   Set MxL5005S tuner register bits.

*/
int
mxl5005s_SetRegMaskBits(
	struct dvb_usb_device*        dib,
	TUNER_MODULE *pTuner,
	unsigned char RegAddr,
	unsigned char Msb,
	unsigned char Lsb,
	const unsigned char WritingValue
	)
{
	MXL5005S_EXTRA_MODULE *pExtra;

	int i;

	unsigned char Mask;
	unsigned char Shift;

	unsigned char RegByte;



	// Get tuner extra module.
	pExtra = (MXL5005S_EXTRA_MODULE *)pTuner->pExtra;


	// Generate mask and shift according to MSB and LSB.
	Mask = 0;
	for(i = Lsb; i < (unsigned char)(Msb + 1); i++)
		Mask |= 0x1 << i;

	Shift = Lsb;


	// Get tuner register byte according to register adddress.
	MXL_RegRead(&pExtra->MxlDefinedTunerStructure, RegAddr, &RegByte);


	// Reserve register byte unmask bit with mask and inlay writing value into it.
	RegByte &= ~Mask;
	RegByte |= (WritingValue << Shift) & Mask;


	// Update tuner register byte table.
	MXL_RegWrite(&pExtra->MxlDefinedTunerStructure, RegAddr, RegByte);


	// Write tuner register byte with writing byte.
	if(pExtra->SetRegsWithTable( dib, pTuner, &RegAddr, &RegByte, LEN_1_BYTE) != FUNCTION_SUCCESS)
		goto error_status_set_tuner_registers;


	return FUNCTION_SUCCESS;


error_status_set_tuner_registers:
	return FUNCTION_ERROR;
}





/**

@brief   Set MxL5005S tuner spectrum mode.

*/
int
mxl5005s_SetSpectrumMode(
	struct dvb_usb_device*        dib,
	TUNER_MODULE *pTuner,
	int SpectrumMode
	)
{
	static const unsigned char BbIqswapTable[SPECTRUM_MODE_NUM] =
	{
		// BB_IQSWAP
		0,				// Normal spectrum
		1,				// Inverse spectrum
	};


	MXL5005S_EXTRA_MODULE *pExtra;



	// Get tuner extra module.
	pExtra = (MXL5005S_EXTRA_MODULE *)pTuner->pExtra;


	// Set BB_IQSWAP according to BB_IQSWAP table and spectrum mode.
	if(pExtra->SetRegMaskBits(dib,pTuner, MXL5005S_BB_IQSWAP_ADDR, MXL5005S_BB_IQSWAP_MSB,
		MXL5005S_BB_IQSWAP_LSB, BbIqswapTable[SpectrumMode]) != FUNCTION_SUCCESS)
		goto error_status_set_tuner_registers;


	return FUNCTION_SUCCESS;


error_status_set_tuner_registers:
	return FUNCTION_ERROR;
}





/**

@brief   Set MxL5005S tuner bandwidth in Hz.

*/
int
mxl5005s_SetBandwidthHz(
	struct dvb_usb_device*        dib,
	TUNER_MODULE *pTuner,
	unsigned long BandwidthHz
	)
{
	MXL5005S_EXTRA_MODULE *pExtra;

	unsigned char BbDlpfBandsel;



	// Get tuner extra module.
	pExtra = (MXL5005S_EXTRA_MODULE *)pTuner->pExtra;


	// Set BB_DLPF_BANDSEL according to bandwidth.
	switch(BandwidthHz)
	{
		default:
		case MXL5005S_BANDWIDTH_6MHZ:		BbDlpfBandsel = 3;		break;
		case MXL5005S_BANDWIDTH_7MHZ:		BbDlpfBandsel = 2;		break;
		case MXL5005S_BANDWIDTH_8MHZ:		BbDlpfBandsel = 0;		break;
	}

	if(pExtra->SetRegMaskBits(dib,pTuner, MXL5005S_BB_DLPF_BANDSEL_ADDR, MXL5005S_BB_DLPF_BANDSEL_MSB,
		MXL5005S_BB_DLPF_BANDSEL_LSB, BbDlpfBandsel) != FUNCTION_SUCCESS)
		goto error_status_set_tuner_registers;


	return FUNCTION_SUCCESS;


error_status_set_tuner_registers:
	return FUNCTION_ERROR;
}





/// @}





/**

@defgroup   MXL5005S_DEPENDENCE   MxL5005S dependence
@ingroup    MXL5005S_TUNER_MODULE

MxL5005S dependence is the related functions for MxL5005S tuner module interface.
One should not use MxL5005S dependence directly.

*/
/// @{





/**

@brief   Set I2C bridge module tuner arguments.

MxL5005S builder will use mxl5005s_SetI2cBridgeModuleTunerArg() to set I2C bridge module tuner arguments.


@param [in]   pTuner   The tuner module pointer


@see   BuildMxl5005sModule()

*/
void
mxl5005s_SetI2cBridgeModuleTunerArg(
	TUNER_MODULE *pTuner
	)
{
	I2C_BRIDGE_MODULE *pI2cBridge;



	// Get I2C bridge module.
	pI2cBridge = pTuner->pI2cBridge;

	// Set I2C bridge module tuner arguments.
	pI2cBridge->pTunerDeviceAddr = &pTuner->DeviceAddr;


	return;
}





/// @}























// The following context is source code provided by MaxLinear.





// MaxLinear source code - MXL5005_Initialize.cpp



//#ifdef _MXL_HEADER
//#include "stdafx.h"
//#endif
//#include "MXL5005_c.h"

_u16 MXL5005_RegisterInit (Tuner_struct * Tuner)
{
	Tuner->TunerRegs_Num = TUNER_REGS_NUM ;
//	Tuner->TunerRegs = (TunerReg_struct *) calloc( TUNER_REGS_NUM, sizeof(TunerReg_struct) ) ;

	Tuner->TunerRegs[0].Reg_Num = 9 ;
	Tuner->TunerRegs[0].Reg_Val = 0x40 ;

	Tuner->TunerRegs[1].Reg_Num = 11 ;
	Tuner->TunerRegs[1].Reg_Val = 0x19 ;

	Tuner->TunerRegs[2].Reg_Num = 12 ;
	Tuner->TunerRegs[2].Reg_Val = 0x60 ;

	Tuner->TunerRegs[3].Reg_Num = 13 ;
	Tuner->TunerRegs[3].Reg_Val = 0x00 ;

	Tuner->TunerRegs[4].Reg_Num = 14 ;
	Tuner->TunerRegs[4].Reg_Val = 0x00 ;

	Tuner->TunerRegs[5].Reg_Num = 15 ;
	Tuner->TunerRegs[5].Reg_Val = 0xC0 ;

	Tuner->TunerRegs[6].Reg_Num = 16 ;
	Tuner->TunerRegs[6].Reg_Val = 0x00 ;

	Tuner->TunerRegs[7].Reg_Num = 17 ;
	Tuner->TunerRegs[7].Reg_Val = 0x00 ;

	Tuner->TunerRegs[8].Reg_Num = 18 ;
	Tuner->TunerRegs[8].Reg_Val = 0x00 ;

	Tuner->TunerRegs[9].Reg_Num = 19 ;
	Tuner->TunerRegs[9].Reg_Val = 0x34 ;

	Tuner->TunerRegs[10].Reg_Num = 21 ;
	Tuner->TunerRegs[10].Reg_Val = 0x00 ;

	Tuner->TunerRegs[11].Reg_Num = 22 ;
	Tuner->TunerRegs[11].Reg_Val = 0x6B ;

	Tuner->TunerRegs[12].Reg_Num = 23 ;
	Tuner->TunerRegs[12].Reg_Val = 0x35 ;

	Tuner->TunerRegs[13].Reg_Num = 24 ;
	Tuner->TunerRegs[13].Reg_Val = 0x70 ;

	Tuner->TunerRegs[14].Reg_Num = 25 ;
	Tuner->TunerRegs[14].Reg_Val = 0x3E ;

	Tuner->TunerRegs[15].Reg_Num = 26 ;
	Tuner->TunerRegs[15].Reg_Val = 0x82 ;

	Tuner->TunerRegs[16].Reg_Num = 31 ;
	Tuner->TunerRegs[16].Reg_Val = 0x00 ;

	Tuner->TunerRegs[17].Reg_Num = 32 ;
	Tuner->TunerRegs[17].Reg_Val = 0x40 ;

	Tuner->TunerRegs[18].Reg_Num = 33 ;
	Tuner->TunerRegs[18].Reg_Val = 0x53 ;

	Tuner->TunerRegs[19].Reg_Num = 34 ;
	Tuner->TunerRegs[19].Reg_Val = 0x81 ;

	Tuner->TunerRegs[20].Reg_Num = 35 ;
	Tuner->TunerRegs[20].Reg_Val = 0xC9 ;

	Tuner->TunerRegs[21].Reg_Num = 36 ;
	Tuner->TunerRegs[21].Reg_Val = 0x01 ;

	Tuner->TunerRegs[22].Reg_Num = 37 ;
	Tuner->TunerRegs[22].Reg_Val = 0x00 ;

	Tuner->TunerRegs[23].Reg_Num = 41 ;
	Tuner->TunerRegs[23].Reg_Val = 0x00 ;

	Tuner->TunerRegs[24].Reg_Num = 42 ;
	Tuner->TunerRegs[24].Reg_Val = 0xF8 ;

	Tuner->TunerRegs[25].Reg_Num = 43 ;
	Tuner->TunerRegs[25].Reg_Val = 0x43 ;

	Tuner->TunerRegs[26].Reg_Num = 44 ;
	Tuner->TunerRegs[26].Reg_Val = 0x20 ;

	Tuner->TunerRegs[27].Reg_Num = 45 ;
	Tuner->TunerRegs[27].Reg_Val = 0x80 ;

	Tuner->TunerRegs[28].Reg_Num = 46 ;
	Tuner->TunerRegs[28].Reg_Val = 0x88 ;

	Tuner->TunerRegs[29].Reg_Num = 47 ;
	Tuner->TunerRegs[29].Reg_Val = 0x86 ;

	Tuner->TunerRegs[30].Reg_Num = 48 ;
	Tuner->TunerRegs[30].Reg_Val = 0x00 ;

	Tuner->TunerRegs[31].Reg_Num = 49 ;
	Tuner->TunerRegs[31].Reg_Val = 0x00 ;

	Tuner->TunerRegs[32].Reg_Num = 53 ;
	Tuner->TunerRegs[32].Reg_Val = 0x94 ;

	Tuner->TunerRegs[33].Reg_Num = 54 ;
	Tuner->TunerRegs[33].Reg_Val = 0xFA ;

	Tuner->TunerRegs[34].Reg_Num = 55 ;
	Tuner->TunerRegs[34].Reg_Val = 0x92 ;

	Tuner->TunerRegs[35].Reg_Num = 56 ;
	Tuner->TunerRegs[35].Reg_Val = 0x80 ;

	Tuner->TunerRegs[36].Reg_Num = 57 ;
	Tuner->TunerRegs[36].Reg_Val = 0x41 ;

	Tuner->TunerRegs[37].Reg_Num = 58 ;
	Tuner->TunerRegs[37].Reg_Val = 0xDB ;

	Tuner->TunerRegs[38].Reg_Num = 59 ;
	Tuner->TunerRegs[38].Reg_Val = 0x00 ;

	Tuner->TunerRegs[39].Reg_Num = 60 ;
	Tuner->TunerRegs[39].Reg_Val = 0x00 ;

	Tuner->TunerRegs[40].Reg_Num = 61 ;
	Tuner->TunerRegs[40].Reg_Val = 0x00 ;

	Tuner->TunerRegs[41].Reg_Num = 62 ;
	Tuner->TunerRegs[41].Reg_Val = 0x00 ;

	Tuner->TunerRegs[42].Reg_Num = 65 ;
	Tuner->TunerRegs[42].Reg_Val = 0xF8 ;

	Tuner->TunerRegs[43].Reg_Num = 66 ;
	Tuner->TunerRegs[43].Reg_Val = 0xE4 ;

	Tuner->TunerRegs[44].Reg_Num = 67 ;
	Tuner->TunerRegs[44].Reg_Val = 0x90 ;

	Tuner->TunerRegs[45].Reg_Num = 68 ;
	Tuner->TunerRegs[45].Reg_Val = 0xC0 ;

	Tuner->TunerRegs[46].Reg_Num = 69 ;
	Tuner->TunerRegs[46].Reg_Val = 0x01 ;

	Tuner->TunerRegs[47].Reg_Num = 70 ;
	Tuner->TunerRegs[47].Reg_Val = 0x50 ;

	Tuner->TunerRegs[48].Reg_Num = 71 ;
	Tuner->TunerRegs[48].Reg_Val = 0x06 ;

	Tuner->TunerRegs[49].Reg_Num = 72 ;
	Tuner->TunerRegs[49].Reg_Val = 0x00 ;

	Tuner->TunerRegs[50].Reg_Num = 73 ;
	Tuner->TunerRegs[50].Reg_Val = 0x20 ;

	Tuner->TunerRegs[51].Reg_Num = 76 ;
	Tuner->TunerRegs[51].Reg_Val = 0xBB ;

	Tuner->TunerRegs[52].Reg_Num = 77 ;
	Tuner->TunerRegs[52].Reg_Val = 0x13 ;

	Tuner->TunerRegs[53].Reg_Num = 81 ;
	Tuner->TunerRegs[53].Reg_Val = 0x04 ;

	Tuner->TunerRegs[54].Reg_Num = 82 ;
	Tuner->TunerRegs[54].Reg_Val = 0x75 ;

	Tuner->TunerRegs[55].Reg_Num = 83 ;
	Tuner->TunerRegs[55].Reg_Val = 0x00 ;

	Tuner->TunerRegs[56].Reg_Num = 84 ;
	Tuner->TunerRegs[56].Reg_Val = 0x00 ;

	Tuner->TunerRegs[57].Reg_Num = 85 ;
	Tuner->TunerRegs[57].Reg_Val = 0x00 ;

	Tuner->TunerRegs[58].Reg_Num = 91 ;
	Tuner->TunerRegs[58].Reg_Val = 0x70 ;

	Tuner->TunerRegs[59].Reg_Num = 92 ;
	Tuner->TunerRegs[59].Reg_Val = 0x00 ;

	Tuner->TunerRegs[60].Reg_Num = 93 ;
	Tuner->TunerRegs[60].Reg_Val = 0x00 ;

	Tuner->TunerRegs[61].Reg_Num = 94 ;
	Tuner->TunerRegs[61].Reg_Val = 0x00 ;

	Tuner->TunerRegs[62].Reg_Num = 95 ;
	Tuner->TunerRegs[62].Reg_Val = 0x0C ;

	Tuner->TunerRegs[63].Reg_Num = 96 ;
	Tuner->TunerRegs[63].Reg_Val = 0x00 ;

	Tuner->TunerRegs[64].Reg_Num = 97 ;
	Tuner->TunerRegs[64].Reg_Val = 0x00 ;

	Tuner->TunerRegs[65].Reg_Num = 98 ;
	Tuner->TunerRegs[65].Reg_Val = 0xE2 ;

	Tuner->TunerRegs[66].Reg_Num = 99 ;
	Tuner->TunerRegs[66].Reg_Val = 0x00 ;

	Tuner->TunerRegs[67].Reg_Num = 100 ;
	Tuner->TunerRegs[67].Reg_Val = 0x00 ;

	Tuner->TunerRegs[68].Reg_Num = 101 ;
	Tuner->TunerRegs[68].Reg_Val = 0x12 ;

	Tuner->TunerRegs[69].Reg_Num = 102 ;
	Tuner->TunerRegs[69].Reg_Val = 0x80 ;

	Tuner->TunerRegs[70].Reg_Num = 103 ;
	Tuner->TunerRegs[70].Reg_Val = 0x32 ;

	Tuner->TunerRegs[71].Reg_Num = 104 ;
	Tuner->TunerRegs[71].Reg_Val = 0xB4 ;

	Tuner->TunerRegs[72].Reg_Num = 105 ;
	Tuner->TunerRegs[72].Reg_Val = 0x60 ;

	Tuner->TunerRegs[73].Reg_Num = 106 ;
	Tuner->TunerRegs[73].Reg_Val = 0x83 ;

	Tuner->TunerRegs[74].Reg_Num = 107 ;
	Tuner->TunerRegs[74].Reg_Val = 0x84 ;

	Tuner->TunerRegs[75].Reg_Num = 108 ;
	Tuner->TunerRegs[75].Reg_Val = 0x9C ;

	Tuner->TunerRegs[76].Reg_Num = 109 ;
	Tuner->TunerRegs[76].Reg_Val = 0x02 ;

	Tuner->TunerRegs[77].Reg_Num = 110 ;
	Tuner->TunerRegs[77].Reg_Val = 0x81 ;

	Tuner->TunerRegs[78].Reg_Num = 111 ;
	Tuner->TunerRegs[78].Reg_Val = 0xC0 ;

	Tuner->TunerRegs[79].Reg_Num = 112 ;
	Tuner->TunerRegs[79].Reg_Val = 0x10 ;

	Tuner->TunerRegs[80].Reg_Num = 131 ;
	Tuner->TunerRegs[80].Reg_Val = 0x8A ;

	Tuner->TunerRegs[81].Reg_Num = 132 ;
	Tuner->TunerRegs[81].Reg_Val = 0x10 ;

	Tuner->TunerRegs[82].Reg_Num = 133 ;
	Tuner->TunerRegs[82].Reg_Val = 0x24 ;

	Tuner->TunerRegs[83].Reg_Num = 134 ;
	Tuner->TunerRegs[83].Reg_Val = 0x00 ;

	Tuner->TunerRegs[84].Reg_Num = 135 ;
	Tuner->TunerRegs[84].Reg_Val = 0x00 ;

	Tuner->TunerRegs[85].Reg_Num = 136 ;
	Tuner->TunerRegs[85].Reg_Val = 0x7E ;

	Tuner->TunerRegs[86].Reg_Num = 137 ;
	Tuner->TunerRegs[86].Reg_Val = 0x40 ;

	Tuner->TunerRegs[87].Reg_Num = 138 ;
	Tuner->TunerRegs[87].Reg_Val = 0x38 ;

	Tuner->TunerRegs[88].Reg_Num = 146 ;
	Tuner->TunerRegs[88].Reg_Val = 0xF6 ;

	Tuner->TunerRegs[89].Reg_Num = 147 ;
	Tuner->TunerRegs[89].Reg_Val = 0x1A ;

	Tuner->TunerRegs[90].Reg_Num = 148 ;
	Tuner->TunerRegs[90].Reg_Val = 0x62 ;

	Tuner->TunerRegs[91].Reg_Num = 149 ;
	Tuner->TunerRegs[91].Reg_Val = 0x33 ;

	Tuner->TunerRegs[92].Reg_Num = 150 ;
	Tuner->TunerRegs[92].Reg_Val = 0x80 ;

	Tuner->TunerRegs[93].Reg_Num = 156 ;
	Tuner->TunerRegs[93].Reg_Val = 0x56 ;

	Tuner->TunerRegs[94].Reg_Num = 157 ;
	Tuner->TunerRegs[94].Reg_Val = 0x17 ;

	Tuner->TunerRegs[95].Reg_Num = 158 ;
	Tuner->TunerRegs[95].Reg_Val = 0xA9 ;

	Tuner->TunerRegs[96].Reg_Num = 159 ;
	Tuner->TunerRegs[96].Reg_Val = 0x00 ;

	Tuner->TunerRegs[97].Reg_Num = 160 ;
	Tuner->TunerRegs[97].Reg_Val = 0x00 ;

	Tuner->TunerRegs[98].Reg_Num = 161 ;
	Tuner->TunerRegs[98].Reg_Val = 0x00 ;

	Tuner->TunerRegs[99].Reg_Num = 162 ;
	Tuner->TunerRegs[99].Reg_Val = 0x40 ;

	Tuner->TunerRegs[100].Reg_Num = 166 ;
	Tuner->TunerRegs[100].Reg_Val = 0xAE ;

	Tuner->TunerRegs[101].Reg_Num = 167 ;
	Tuner->TunerRegs[101].Reg_Val = 0x1B ;

	Tuner->TunerRegs[102].Reg_Num = 168 ;
	Tuner->TunerRegs[102].Reg_Val = 0xF2 ;

	Tuner->TunerRegs[103].Reg_Num = 195 ;
	Tuner->TunerRegs[103].Reg_Val = 0x00 ;

	return 0 ;
}

_u16 MXL5005_ControlInit (Tuner_struct *Tuner)
{
	Tuner->Init_Ctrl_Num = INITCTRL_NUM ;

	Tuner->Init_Ctrl[0].Ctrl_Num = DN_IQTN_AMP_CUT ;
	Tuner->Init_Ctrl[0].size = 1 ;
	Tuner->Init_Ctrl[0].addr[0] = 73;
	Tuner->Init_Ctrl[0].bit[0] = 7;
	Tuner->Init_Ctrl[0].val[0] = 0;

	Tuner->Init_Ctrl[1].Ctrl_Num = BB_MODE ;
	Tuner->Init_Ctrl[1].size = 1 ;
	Tuner->Init_Ctrl[1].addr[0] = 53;
	Tuner->Init_Ctrl[1].bit[0] = 2;
	Tuner->Init_Ctrl[1].val[0] = 1;

	Tuner->Init_Ctrl[2].Ctrl_Num = BB_BUF ;
	Tuner->Init_Ctrl[2].size = 2 ;
	Tuner->Init_Ctrl[2].addr[0] = 53;
	Tuner->Init_Ctrl[2].bit[0] = 1;
	Tuner->Init_Ctrl[2].val[0] = 0;
	Tuner->Init_Ctrl[2].addr[1] = 57;
	Tuner->Init_Ctrl[2].bit[1] = 0;
	Tuner->Init_Ctrl[2].val[1] = 1;

	Tuner->Init_Ctrl[3].Ctrl_Num = BB_BUF_OA ;
	Tuner->Init_Ctrl[3].size = 1 ;
	Tuner->Init_Ctrl[3].addr[0] = 53;
	Tuner->Init_Ctrl[3].bit[0] = 0;
	Tuner->Init_Ctrl[3].val[0] = 0;

	Tuner->Init_Ctrl[4].Ctrl_Num = BB_ALPF_BANDSELECT ;
	Tuner->Init_Ctrl[4].size = 3 ;
	Tuner->Init_Ctrl[4].addr[0] = 53;
	Tuner->Init_Ctrl[4].bit[0] = 5;
	Tuner->Init_Ctrl[4].val[0] = 0;
	Tuner->Init_Ctrl[4].addr[1] = 53;
	Tuner->Init_Ctrl[4].bit[1] = 6;
	Tuner->Init_Ctrl[4].val[1] = 0;
	Tuner->Init_Ctrl[4].addr[2] = 53;
	Tuner->Init_Ctrl[4].bit[2] = 7;
	Tuner->Init_Ctrl[4].val[2] = 1;

	Tuner->Init_Ctrl[5].Ctrl_Num = BB_IQSWAP ;
	Tuner->Init_Ctrl[5].size = 1 ;
	Tuner->Init_Ctrl[5].addr[0] = 59;
	Tuner->Init_Ctrl[5].bit[0] = 0;
	Tuner->Init_Ctrl[5].val[0] = 0;

	Tuner->Init_Ctrl[6].Ctrl_Num = BB_DLPF_BANDSEL ;
	Tuner->Init_Ctrl[6].size = 2 ;
	Tuner->Init_Ctrl[6].addr[0] = 53;
	Tuner->Init_Ctrl[6].bit[0] = 3;
	Tuner->Init_Ctrl[6].val[0] = 0;
	Tuner->Init_Ctrl[6].addr[1] = 53;
	Tuner->Init_Ctrl[6].bit[1] = 4;
	Tuner->Init_Ctrl[6].val[1] = 1;

	Tuner->Init_Ctrl[7].Ctrl_Num = RFSYN_CHP_GAIN ;
	Tuner->Init_Ctrl[7].size = 4 ;
	Tuner->Init_Ctrl[7].addr[0] = 22;
	Tuner->Init_Ctrl[7].bit[0] = 4;
	Tuner->Init_Ctrl[7].val[0] = 0;
	Tuner->Init_Ctrl[7].addr[1] = 22;
	Tuner->Init_Ctrl[7].bit[1] = 5;
	Tuner->Init_Ctrl[7].val[1] = 1;
	Tuner->Init_Ctrl[7].addr[2] = 22;
	Tuner->Init_Ctrl[7].bit[2] = 6;
	Tuner->Init_Ctrl[7].val[2] = 1;
	Tuner->Init_Ctrl[7].addr[3] = 22;
	Tuner->Init_Ctrl[7].bit[3] = 7;
	Tuner->Init_Ctrl[7].val[3] = 0;

	Tuner->Init_Ctrl[8].Ctrl_Num = RFSYN_EN_CHP_HIGAIN ;
	Tuner->Init_Ctrl[8].size = 1 ;
	Tuner->Init_Ctrl[8].addr[0] = 22;
	Tuner->Init_Ctrl[8].bit[0] = 2;
	Tuner->Init_Ctrl[8].val[0] = 0;

	Tuner->Init_Ctrl[9].Ctrl_Num = AGC_IF ;
	Tuner->Init_Ctrl[9].size = 4 ;
	Tuner->Init_Ctrl[9].addr[0] = 76;
	Tuner->Init_Ctrl[9].bit[0] = 0;
	Tuner->Init_Ctrl[9].val[0] = 1;
	Tuner->Init_Ctrl[9].addr[1] = 76;
	Tuner->Init_Ctrl[9].bit[1] = 1;
	Tuner->Init_Ctrl[9].val[1] = 1;
	Tuner->Init_Ctrl[9].addr[2] = 76;
	Tuner->Init_Ctrl[9].bit[2] = 2;
	Tuner->Init_Ctrl[9].val[2] = 0;
	Tuner->Init_Ctrl[9].addr[3] = 76;
	Tuner->Init_Ctrl[9].bit[3] = 3;
	Tuner->Init_Ctrl[9].val[3] = 1;

	Tuner->Init_Ctrl[10].Ctrl_Num = AGC_RF ;
	Tuner->Init_Ctrl[10].size = 4 ;
	Tuner->Init_Ctrl[10].addr[0] = 76;
	Tuner->Init_Ctrl[10].bit[0] = 4;
	Tuner->Init_Ctrl[10].val[0] = 1;
	Tuner->Init_Ctrl[10].addr[1] = 76;
	Tuner->Init_Ctrl[10].bit[1] = 5;
	Tuner->Init_Ctrl[10].val[1] = 1;
	Tuner->Init_Ctrl[10].addr[2] = 76;
	Tuner->Init_Ctrl[10].bit[2] = 6;
	Tuner->Init_Ctrl[10].val[2] = 0;
	Tuner->Init_Ctrl[10].addr[3] = 76;
	Tuner->Init_Ctrl[10].bit[3] = 7;
	Tuner->Init_Ctrl[10].val[3] = 1;

	Tuner->Init_Ctrl[11].Ctrl_Num = IF_DIVVAL ;
	Tuner->Init_Ctrl[11].size = 5 ;
	Tuner->Init_Ctrl[11].addr[0] = 43;
	Tuner->Init_Ctrl[11].bit[0] = 3;
	Tuner->Init_Ctrl[11].val[0] = 0;
	Tuner->Init_Ctrl[11].addr[1] = 43;
	Tuner->Init_Ctrl[11].bit[1] = 4;
	Tuner->Init_Ctrl[11].val[1] = 0;
	Tuner->Init_Ctrl[11].addr[2] = 43;
	Tuner->Init_Ctrl[11].bit[2] = 5;
	Tuner->Init_Ctrl[11].val[2] = 0;
	Tuner->Init_Ctrl[11].addr[3] = 43;
	Tuner->Init_Ctrl[11].bit[3] = 6;
	Tuner->Init_Ctrl[11].val[3] = 1;
	Tuner->Init_Ctrl[11].addr[4] = 43;
	Tuner->Init_Ctrl[11].bit[4] = 7;
	Tuner->Init_Ctrl[11].val[4] = 0;

	Tuner->Init_Ctrl[12].Ctrl_Num = IF_VCO_BIAS ;
	Tuner->Init_Ctrl[12].size = 6 ;
	Tuner->Init_Ctrl[12].addr[0] = 44;
	Tuner->Init_Ctrl[12].bit[0] = 2;
	Tuner->Init_Ctrl[12].val[0] = 0;
	Tuner->Init_Ctrl[12].addr[1] = 44;
	Tuner->Init_Ctrl[12].bit[1] = 3;
	Tuner->Init_Ctrl[12].val[1] = 0;
	Tuner->Init_Ctrl[12].addr[2] = 44;
	Tuner->Init_Ctrl[12].bit[2] = 4;
	Tuner->Init_Ctrl[12].val[2] = 0;
	Tuner->Init_Ctrl[12].addr[3] = 44;
	Tuner->Init_Ctrl[12].bit[3] = 5;
	Tuner->Init_Ctrl[12].val[3] = 1;
	Tuner->Init_Ctrl[12].addr[4] = 44;
	Tuner->Init_Ctrl[12].bit[4] = 6;
	Tuner->Init_Ctrl[12].val[4] = 0;
	Tuner->Init_Ctrl[12].addr[5] = 44;
	Tuner->Init_Ctrl[12].bit[5] = 7;
	Tuner->Init_Ctrl[12].val[5] = 0;

	Tuner->Init_Ctrl[13].Ctrl_Num = CHCAL_INT_MOD_IF ;
	Tuner->Init_Ctrl[13].size = 7 ;
	Tuner->Init_Ctrl[13].addr[0] = 11;
	Tuner->Init_Ctrl[13].bit[0] = 0;
	Tuner->Init_Ctrl[13].val[0] = 1;
	Tuner->Init_Ctrl[13].addr[1] = 11;
	Tuner->Init_Ctrl[13].bit[1] = 1;
	Tuner->Init_Ctrl[13].val[1] = 0;
	Tuner->Init_Ctrl[13].addr[2] = 11;
	Tuner->Init_Ctrl[13].bit[2] = 2;
	Tuner->Init_Ctrl[13].val[2] = 0;
	Tuner->Init_Ctrl[13].addr[3] = 11;
	Tuner->Init_Ctrl[13].bit[3] = 3;
	Tuner->Init_Ctrl[13].val[3] = 1;
	Tuner->Init_Ctrl[13].addr[4] = 11;
	Tuner->Init_Ctrl[13].bit[4] = 4;
	Tuner->Init_Ctrl[13].val[4] = 1;
	Tuner->Init_Ctrl[13].addr[5] = 11;
	Tuner->Init_Ctrl[13].bit[5] = 5;
	Tuner->Init_Ctrl[13].val[5] = 0;
	Tuner->Init_Ctrl[13].addr[6] = 11;
	Tuner->Init_Ctrl[13].bit[6] = 6;
	Tuner->Init_Ctrl[13].val[6] = 0;

	Tuner->Init_Ctrl[14].Ctrl_Num = CHCAL_FRAC_MOD_IF ;
	Tuner->Init_Ctrl[14].size = 16 ;
	Tuner->Init_Ctrl[14].addr[0] = 13;
	Tuner->Init_Ctrl[14].bit[0] = 0;
	Tuner->Init_Ctrl[14].val[0] = 0;
	Tuner->Init_Ctrl[14].addr[1] = 13;
	Tuner->Init_Ctrl[14].bit[1] = 1;
	Tuner->Init_Ctrl[14].val[1] = 0;
	Tuner->Init_Ctrl[14].addr[2] = 13;
	Tuner->Init_Ctrl[14].bit[2] = 2;
	Tuner->Init_Ctrl[14].val[2] = 0;
	Tuner->Init_Ctrl[14].addr[3] = 13;
	Tuner->Init_Ctrl[14].bit[3] = 3;
	Tuner->Init_Ctrl[14].val[3] = 0;
	Tuner->Init_Ctrl[14].addr[4] = 13;
	Tuner->Init_Ctrl[14].bit[4] = 4;
	Tuner->Init_Ctrl[14].val[4] = 0;
	Tuner->Init_Ctrl[14].addr[5] = 13;
	Tuner->Init_Ctrl[14].bit[5] = 5;
	Tuner->Init_Ctrl[14].val[5] = 0;
	Tuner->Init_Ctrl[14].addr[6] = 13;
	Tuner->Init_Ctrl[14].bit[6] = 6;
	Tuner->Init_Ctrl[14].val[6] = 0;
	Tuner->Init_Ctrl[14].addr[7] = 13;
	Tuner->Init_Ctrl[14].bit[7] = 7;
	Tuner->Init_Ctrl[14].val[7] = 0;
	Tuner->Init_Ctrl[14].addr[8] = 12;
	Tuner->Init_Ctrl[14].bit[8] = 0;
	Tuner->Init_Ctrl[14].val[8] = 0;
	Tuner->Init_Ctrl[14].addr[9] = 12;
	Tuner->Init_Ctrl[14].bit[9] = 1;
	Tuner->Init_Ctrl[14].val[9] = 0;
	Tuner->Init_Ctrl[14].addr[10] = 12;
	Tuner->Init_Ctrl[14].bit[10] = 2;
	Tuner->Init_Ctrl[14].val[10] = 0;
	Tuner->Init_Ctrl[14].addr[11] = 12;
	Tuner->Init_Ctrl[14].bit[11] = 3;
	Tuner->Init_Ctrl[14].val[11] = 0;
	Tuner->Init_Ctrl[14].addr[12] = 12;
	Tuner->Init_Ctrl[14].bit[12] = 4;
	Tuner->Init_Ctrl[14].val[12] = 0;
	Tuner->Init_Ctrl[14].addr[13] = 12;
	Tuner->Init_Ctrl[14].bit[13] = 5;
	Tuner->Init_Ctrl[14].val[13] = 1;
	Tuner->Init_Ctrl[14].addr[14] = 12;
	Tuner->Init_Ctrl[14].bit[14] = 6;
	Tuner->Init_Ctrl[14].val[14] = 1;
	Tuner->Init_Ctrl[14].addr[15] = 12;
	Tuner->Init_Ctrl[14].bit[15] = 7;
	Tuner->Init_Ctrl[14].val[15] = 0;

	Tuner->Init_Ctrl[15].Ctrl_Num = DRV_RES_SEL ;
	Tuner->Init_Ctrl[15].size = 3 ;
	Tuner->Init_Ctrl[15].addr[0] = 147;
	Tuner->Init_Ctrl[15].bit[0] = 2;
	Tuner->Init_Ctrl[15].val[0] = 0;
	Tuner->Init_Ctrl[15].addr[1] = 147;
	Tuner->Init_Ctrl[15].bit[1] = 3;
	Tuner->Init_Ctrl[15].val[1] = 1;
	Tuner->Init_Ctrl[15].addr[2] = 147;
	Tuner->Init_Ctrl[15].bit[2] = 4;
	Tuner->Init_Ctrl[15].val[2] = 1;

	Tuner->Init_Ctrl[16].Ctrl_Num = I_DRIVER ;
	Tuner->Init_Ctrl[16].size = 2 ;
	Tuner->Init_Ctrl[16].addr[0] = 147;
	Tuner->Init_Ctrl[16].bit[0] = 0;
	Tuner->Init_Ctrl[16].val[0] = 0;
	Tuner->Init_Ctrl[16].addr[1] = 147;
	Tuner->Init_Ctrl[16].bit[1] = 1;
	Tuner->Init_Ctrl[16].val[1] = 1;

	Tuner->Init_Ctrl[17].Ctrl_Num = EN_AAF ;
	Tuner->Init_Ctrl[17].size = 1 ;
	Tuner->Init_Ctrl[17].addr[0] = 147;
	Tuner->Init_Ctrl[17].bit[0] = 7;
	Tuner->Init_Ctrl[17].val[0] = 0;

	Tuner->Init_Ctrl[18].Ctrl_Num = EN_3P ;
	Tuner->Init_Ctrl[18].size = 1 ;
	Tuner->Init_Ctrl[18].addr[0] = 147;
	Tuner->Init_Ctrl[18].bit[0] = 6;
	Tuner->Init_Ctrl[18].val[0] = 0;

	Tuner->Init_Ctrl[19].Ctrl_Num = EN_AUX_3P ;
	Tuner->Init_Ctrl[19].size = 1 ;
	Tuner->Init_Ctrl[19].addr[0] = 156;
	Tuner->Init_Ctrl[19].bit[0] = 0;
	Tuner->Init_Ctrl[19].val[0] = 0;

	Tuner->Init_Ctrl[20].Ctrl_Num = SEL_AAF_BAND ;
	Tuner->Init_Ctrl[20].size = 1 ;
	Tuner->Init_Ctrl[20].addr[0] = 147;
	Tuner->Init_Ctrl[20].bit[0] = 5;
	Tuner->Init_Ctrl[20].val[0] = 0;

	Tuner->Init_Ctrl[21].Ctrl_Num = SEQ_ENCLK16_CLK_OUT ;
	Tuner->Init_Ctrl[21].size = 1 ;
	Tuner->Init_Ctrl[21].addr[0] = 137;
	Tuner->Init_Ctrl[21].bit[0] = 4;
	Tuner->Init_Ctrl[21].val[0] = 0;

	Tuner->Init_Ctrl[22].Ctrl_Num = SEQ_SEL4_16B ;
	Tuner->Init_Ctrl[22].size = 1 ;
	Tuner->Init_Ctrl[22].addr[0] = 137;
	Tuner->Init_Ctrl[22].bit[0] = 7;
	Tuner->Init_Ctrl[22].val[0] = 0;

	Tuner->Init_Ctrl[23].Ctrl_Num = XTAL_CAPSELECT ;
	Tuner->Init_Ctrl[23].size = 1 ;
	Tuner->Init_Ctrl[23].addr[0] = 91;
	Tuner->Init_Ctrl[23].bit[0] = 5;
	Tuner->Init_Ctrl[23].val[0] = 1;

	Tuner->Init_Ctrl[24].Ctrl_Num = IF_SEL_DBL ;
	Tuner->Init_Ctrl[24].size = 1 ;
	Tuner->Init_Ctrl[24].addr[0] = 43;
	Tuner->Init_Ctrl[24].bit[0] = 0;
	Tuner->Init_Ctrl[24].val[0] = 1;

	Tuner->Init_Ctrl[25].Ctrl_Num = RFSYN_R_DIV ;
	Tuner->Init_Ctrl[25].size = 2 ;
	Tuner->Init_Ctrl[25].addr[0] = 22;
	Tuner->Init_Ctrl[25].bit[0] = 0;
	Tuner->Init_Ctrl[25].val[0] = 1;
	Tuner->Init_Ctrl[25].addr[1] = 22;
	Tuner->Init_Ctrl[25].bit[1] = 1;
	Tuner->Init_Ctrl[25].val[1] = 1;

	Tuner->Init_Ctrl[26].Ctrl_Num = SEQ_EXTSYNTHCALIF ;
	Tuner->Init_Ctrl[26].size = 1 ;
	Tuner->Init_Ctrl[26].addr[0] = 134;
	Tuner->Init_Ctrl[26].bit[0] = 2;
	Tuner->Init_Ctrl[26].val[0] = 0;

	Tuner->Init_Ctrl[27].Ctrl_Num = SEQ_EXTDCCAL ;
	Tuner->Init_Ctrl[27].size = 1 ;
	Tuner->Init_Ctrl[27].addr[0] = 137;
	Tuner->Init_Ctrl[27].bit[0] = 3;
	Tuner->Init_Ctrl[27].val[0] = 0;

	Tuner->Init_Ctrl[28].Ctrl_Num = AGC_EN_RSSI ;
	Tuner->Init_Ctrl[28].size = 1 ;
	Tuner->Init_Ctrl[28].addr[0] = 77;
	Tuner->Init_Ctrl[28].bit[0] = 7;
	Tuner->Init_Ctrl[28].val[0] = 0;

	Tuner->Init_Ctrl[29].Ctrl_Num = RFA_ENCLKRFAGC ;
	Tuner->Init_Ctrl[29].size = 1 ;
	Tuner->Init_Ctrl[29].addr[0] = 166;
	Tuner->Init_Ctrl[29].bit[0] = 7;
	Tuner->Init_Ctrl[29].val[0] = 1;

	Tuner->Init_Ctrl[30].Ctrl_Num = RFA_RSSI_REFH ;
	Tuner->Init_Ctrl[30].size = 3 ;
	Tuner->Init_Ctrl[30].addr[0] = 166;
	Tuner->Init_Ctrl[30].bit[0] = 0;
	Tuner->Init_Ctrl[30].val[0] = 0;
	Tuner->Init_Ctrl[30].addr[1] = 166;
	Tuner->Init_Ctrl[30].bit[1] = 1;
	Tuner->Init_Ctrl[30].val[1] = 1;
	Tuner->Init_Ctrl[30].addr[2] = 166;
	Tuner->Init_Ctrl[30].bit[2] = 2;
	Tuner->Init_Ctrl[30].val[2] = 1;

	Tuner->Init_Ctrl[31].Ctrl_Num = RFA_RSSI_REF ;
	Tuner->Init_Ctrl[31].size = 3 ;
	Tuner->Init_Ctrl[31].addr[0] = 166;
	Tuner->Init_Ctrl[31].bit[0] = 3;
	Tuner->Init_Ctrl[31].val[0] = 1;
	Tuner->Init_Ctrl[31].addr[1] = 166;
	Tuner->Init_Ctrl[31].bit[1] = 4;
	Tuner->Init_Ctrl[31].val[1] = 0;
	Tuner->Init_Ctrl[31].addr[2] = 166;
	Tuner->Init_Ctrl[31].bit[2] = 5;
	Tuner->Init_Ctrl[31].val[2] = 1;

	Tuner->Init_Ctrl[32].Ctrl_Num = RFA_RSSI_REFL ;
	Tuner->Init_Ctrl[32].size = 3 ;
	Tuner->Init_Ctrl[32].addr[0] = 167;
	Tuner->Init_Ctrl[32].bit[0] = 0;
	Tuner->Init_Ctrl[32].val[0] = 1;
	Tuner->Init_Ctrl[32].addr[1] = 167;
	Tuner->Init_Ctrl[32].bit[1] = 1;
	Tuner->Init_Ctrl[32].val[1] = 1;
	Tuner->Init_Ctrl[32].addr[2] = 167;
	Tuner->Init_Ctrl[32].bit[2] = 2;
	Tuner->Init_Ctrl[32].val[2] = 0;

	Tuner->Init_Ctrl[33].Ctrl_Num = RFA_FLR ;
	Tuner->Init_Ctrl[33].size = 4 ;
	Tuner->Init_Ctrl[33].addr[0] = 168;
	Tuner->Init_Ctrl[33].bit[0] = 0;
	Tuner->Init_Ctrl[33].val[0] = 0;
	Tuner->Init_Ctrl[33].addr[1] = 168;
	Tuner->Init_Ctrl[33].bit[1] = 1;
	Tuner->Init_Ctrl[33].val[1] = 1;
	Tuner->Init_Ctrl[33].addr[2] = 168;
	Tuner->Init_Ctrl[33].bit[2] = 2;
	Tuner->Init_Ctrl[33].val[2] = 0;
	Tuner->Init_Ctrl[33].addr[3] = 168;
	Tuner->Init_Ctrl[33].bit[3] = 3;
	Tuner->Init_Ctrl[33].val[3] = 0;

	Tuner->Init_Ctrl[34].Ctrl_Num = RFA_CEIL ;
	Tuner->Init_Ctrl[34].size = 4 ;
	Tuner->Init_Ctrl[34].addr[0] = 168;
	Tuner->Init_Ctrl[34].bit[0] = 4;
	Tuner->Init_Ctrl[34].val[0] = 1;
	Tuner->Init_Ctrl[34].addr[1] = 168;
	Tuner->Init_Ctrl[34].bit[1] = 5;
	Tuner->Init_Ctrl[34].val[1] = 1;
	Tuner->Init_Ctrl[34].addr[2] = 168;
	Tuner->Init_Ctrl[34].bit[2] = 6;
	Tuner->Init_Ctrl[34].val[2] = 1;
	Tuner->Init_Ctrl[34].addr[3] = 168;
	Tuner->Init_Ctrl[34].bit[3] = 7;
	Tuner->Init_Ctrl[34].val[3] = 1;

	Tuner->Init_Ctrl[35].Ctrl_Num = SEQ_EXTIQFSMPULSE ;
	Tuner->Init_Ctrl[35].size = 1 ;
	Tuner->Init_Ctrl[35].addr[0] = 135;
	Tuner->Init_Ctrl[35].bit[0] = 0;
	Tuner->Init_Ctrl[35].val[0] = 0;

	Tuner->Init_Ctrl[36].Ctrl_Num = OVERRIDE_1 ;
	Tuner->Init_Ctrl[36].size = 1 ;
	Tuner->Init_Ctrl[36].addr[0] = 56;
	Tuner->Init_Ctrl[36].bit[0] = 3;
	Tuner->Init_Ctrl[36].val[0] = 0;

	Tuner->Init_Ctrl[37].Ctrl_Num = BB_INITSTATE_DLPF_TUNE ;
	Tuner->Init_Ctrl[37].size = 7 ;
	Tuner->Init_Ctrl[37].addr[0] = 59;
	Tuner->Init_Ctrl[37].bit[0] = 1;
	Tuner->Init_Ctrl[37].val[0] = 0;
	Tuner->Init_Ctrl[37].addr[1] = 59;
	Tuner->Init_Ctrl[37].bit[1] = 2;
	Tuner->Init_Ctrl[37].val[1] = 0;
	Tuner->Init_Ctrl[37].addr[2] = 59;
	Tuner->Init_Ctrl[37].bit[2] = 3;
	Tuner->Init_Ctrl[37].val[2] = 0;
	Tuner->Init_Ctrl[37].addr[3] = 59;
	Tuner->Init_Ctrl[37].bit[3] = 4;
	Tuner->Init_Ctrl[37].val[3] = 0;
	Tuner->Init_Ctrl[37].addr[4] = 59;
	Tuner->Init_Ctrl[37].bit[4] = 5;
	Tuner->Init_Ctrl[37].val[4] = 0;
	Tuner->Init_Ctrl[37].addr[5] = 59;
	Tuner->Init_Ctrl[37].bit[5] = 6;
	Tuner->Init_Ctrl[37].val[5] = 0;
	Tuner->Init_Ctrl[37].addr[6] = 59;
	Tuner->Init_Ctrl[37].bit[6] = 7;
	Tuner->Init_Ctrl[37].val[6] = 0;

	Tuner->Init_Ctrl[38].Ctrl_Num = TG_R_DIV ;
	Tuner->Init_Ctrl[38].size = 6 ;
	Tuner->Init_Ctrl[38].addr[0] = 32;
	Tuner->Init_Ctrl[38].bit[0] = 2;
	Tuner->Init_Ctrl[38].val[0] = 0;
	Tuner->Init_Ctrl[38].addr[1] = 32;
	Tuner->Init_Ctrl[38].bit[1] = 3;
	Tuner->Init_Ctrl[38].val[1] = 0;
	Tuner->Init_Ctrl[38].addr[2] = 32;
	Tuner->Init_Ctrl[38].bit[2] = 4;
	Tuner->Init_Ctrl[38].val[2] = 0;
	Tuner->Init_Ctrl[38].addr[3] = 32;
	Tuner->Init_Ctrl[38].bit[3] = 5;
	Tuner->Init_Ctrl[38].val[3] = 0;
	Tuner->Init_Ctrl[38].addr[4] = 32;
	Tuner->Init_Ctrl[38].bit[4] = 6;
	Tuner->Init_Ctrl[38].val[4] = 1;
	Tuner->Init_Ctrl[38].addr[5] = 32;
	Tuner->Init_Ctrl[38].bit[5] = 7;
	Tuner->Init_Ctrl[38].val[5] = 0;

	Tuner->Init_Ctrl[39].Ctrl_Num = EN_CHP_LIN_B ;
	Tuner->Init_Ctrl[39].size = 1 ;
	Tuner->Init_Ctrl[39].addr[0] = 25;
	Tuner->Init_Ctrl[39].bit[0] = 3;
	Tuner->Init_Ctrl[39].val[0] = 1;


	Tuner->CH_Ctrl_Num = CHCTRL_NUM ;

	Tuner->CH_Ctrl[0].Ctrl_Num = DN_POLY ;
	Tuner->CH_Ctrl[0].size = 2 ;
	Tuner->CH_Ctrl[0].addr[0] = 68;
	Tuner->CH_Ctrl[0].bit[0] = 6;
	Tuner->CH_Ctrl[0].val[0] = 1;
	Tuner->CH_Ctrl[0].addr[1] = 68;
	Tuner->CH_Ctrl[0].bit[1] = 7;
	Tuner->CH_Ctrl[0].val[1] = 1;

	Tuner->CH_Ctrl[1].Ctrl_Num = DN_RFGAIN ;
	Tuner->CH_Ctrl[1].size = 2 ;
	Tuner->CH_Ctrl[1].addr[0] = 70;
	Tuner->CH_Ctrl[1].bit[0] = 6;
	Tuner->CH_Ctrl[1].val[0] = 1;
	Tuner->CH_Ctrl[1].addr[1] = 70;
	Tuner->CH_Ctrl[1].bit[1] = 7;
	Tuner->CH_Ctrl[1].val[1] = 0;

	Tuner->CH_Ctrl[2].Ctrl_Num = DN_CAP_RFLPF ;
	Tuner->CH_Ctrl[2].size = 9 ;
	Tuner->CH_Ctrl[2].addr[0] = 69;
	Tuner->CH_Ctrl[2].bit[0] = 5;
	Tuner->CH_Ctrl[2].val[0] = 0;
	Tuner->CH_Ctrl[2].addr[1] = 69;
	Tuner->CH_Ctrl[2].bit[1] = 6;
	Tuner->CH_Ctrl[2].val[1] = 0;
	Tuner->CH_Ctrl[2].addr[2] = 69;
	Tuner->CH_Ctrl[2].bit[2] = 7;
	Tuner->CH_Ctrl[2].val[2] = 0;
	Tuner->CH_Ctrl[2].addr[3] = 68;
	Tuner->CH_Ctrl[2].bit[3] = 0;
	Tuner->CH_Ctrl[2].val[3] = 0;
	Tuner->CH_Ctrl[2].addr[4] = 68;
	Tuner->CH_Ctrl[2].bit[4] = 1;
	Tuner->CH_Ctrl[2].val[4] = 0;
	Tuner->CH_Ctrl[2].addr[5] = 68;
	Tuner->CH_Ctrl[2].bit[5] = 2;
	Tuner->CH_Ctrl[2].val[5] = 0;
	Tuner->CH_Ctrl[2].addr[6] = 68;
	Tuner->CH_Ctrl[2].bit[6] = 3;
	Tuner->CH_Ctrl[2].val[6] = 0;
	Tuner->CH_Ctrl[2].addr[7] = 68;
	Tuner->CH_Ctrl[2].bit[7] = 4;
	Tuner->CH_Ctrl[2].val[7] = 0;
	Tuner->CH_Ctrl[2].addr[8] = 68;
	Tuner->CH_Ctrl[2].bit[8] = 5;
	Tuner->CH_Ctrl[2].val[8] = 0;

	Tuner->CH_Ctrl[3].Ctrl_Num = DN_EN_VHFUHFBAR ;
	Tuner->CH_Ctrl[3].size = 1 ;
	Tuner->CH_Ctrl[3].addr[0] = 70;
	Tuner->CH_Ctrl[3].bit[0] = 5;
	Tuner->CH_Ctrl[3].val[0] = 0;

	Tuner->CH_Ctrl[4].Ctrl_Num = DN_GAIN_ADJUST ;
	Tuner->CH_Ctrl[4].size = 3 ;
	Tuner->CH_Ctrl[4].addr[0] = 73;
	Tuner->CH_Ctrl[4].bit[0] = 4;
	Tuner->CH_Ctrl[4].val[0] = 0;
	Tuner->CH_Ctrl[4].addr[1] = 73;
	Tuner->CH_Ctrl[4].bit[1] = 5;
	Tuner->CH_Ctrl[4].val[1] = 1;
	Tuner->CH_Ctrl[4].addr[2] = 73;
	Tuner->CH_Ctrl[4].bit[2] = 6;
	Tuner->CH_Ctrl[4].val[2] = 0;

	Tuner->CH_Ctrl[5].Ctrl_Num = DN_IQTNBUF_AMP ;
	Tuner->CH_Ctrl[5].size = 4 ;
	Tuner->CH_Ctrl[5].addr[0] = 70;
	Tuner->CH_Ctrl[5].bit[0] = 0;
	Tuner->CH_Ctrl[5].val[0] = 0;
	Tuner->CH_Ctrl[5].addr[1] = 70;
	Tuner->CH_Ctrl[5].bit[1] = 1;
	Tuner->CH_Ctrl[5].val[1] = 0;
	Tuner->CH_Ctrl[5].addr[2] = 70;
	Tuner->CH_Ctrl[5].bit[2] = 2;
	Tuner->CH_Ctrl[5].val[2] = 0;
	Tuner->CH_Ctrl[5].addr[3] = 70;
	Tuner->CH_Ctrl[5].bit[3] = 3;
	Tuner->CH_Ctrl[5].val[3] = 0;

	Tuner->CH_Ctrl[6].Ctrl_Num = DN_IQTNGNBFBIAS_BST ;
	Tuner->CH_Ctrl[6].size = 1 ;
	Tuner->CH_Ctrl[6].addr[0] = 70;
	Tuner->CH_Ctrl[6].bit[0] = 4;
	Tuner->CH_Ctrl[6].val[0] = 1;

	Tuner->CH_Ctrl[7].Ctrl_Num = RFSYN_EN_OUTMUX ;
	Tuner->CH_Ctrl[7].size = 1 ;
	Tuner->CH_Ctrl[7].addr[0] = 111;
	Tuner->CH_Ctrl[7].bit[0] = 4;
	Tuner->CH_Ctrl[7].val[0] = 0;

	Tuner->CH_Ctrl[8].Ctrl_Num = RFSYN_SEL_VCO_OUT ;
	Tuner->CH_Ctrl[8].size = 1 ;
	Tuner->CH_Ctrl[8].addr[0] = 111;
	Tuner->CH_Ctrl[8].bit[0] = 7;
	Tuner->CH_Ctrl[8].val[0] = 1;

	Tuner->CH_Ctrl[9].Ctrl_Num = RFSYN_SEL_VCO_HI ;
	Tuner->CH_Ctrl[9].size = 1 ;
	Tuner->CH_Ctrl[9].addr[0] = 111;
	Tuner->CH_Ctrl[9].bit[0] = 6;
	Tuner->CH_Ctrl[9].val[0] = 1;

	Tuner->CH_Ctrl[10].Ctrl_Num = RFSYN_SEL_DIVM ;
	Tuner->CH_Ctrl[10].size = 1 ;
	Tuner->CH_Ctrl[10].addr[0] = 111;
	Tuner->CH_Ctrl[10].bit[0] = 5;
	Tuner->CH_Ctrl[10].val[0] = 0;

	Tuner->CH_Ctrl[11].Ctrl_Num = RFSYN_RF_DIV_BIAS ;
	Tuner->CH_Ctrl[11].size = 2 ;
	Tuner->CH_Ctrl[11].addr[0] = 110;
	Tuner->CH_Ctrl[11].bit[0] = 0;
	Tuner->CH_Ctrl[11].val[0] = 1;
	Tuner->CH_Ctrl[11].addr[1] = 110;
	Tuner->CH_Ctrl[11].bit[1] = 1;
	Tuner->CH_Ctrl[11].val[1] = 0;

	Tuner->CH_Ctrl[12].Ctrl_Num = DN_SEL_FREQ ;
	Tuner->CH_Ctrl[12].size = 3 ;
	Tuner->CH_Ctrl[12].addr[0] = 69;
	Tuner->CH_Ctrl[12].bit[0] = 2;
	Tuner->CH_Ctrl[12].val[0] = 0;
	Tuner->CH_Ctrl[12].addr[1] = 69;
	Tuner->CH_Ctrl[12].bit[1] = 3;
	Tuner->CH_Ctrl[12].val[1] = 0;
	Tuner->CH_Ctrl[12].addr[2] = 69;
	Tuner->CH_Ctrl[12].bit[2] = 4;
	Tuner->CH_Ctrl[12].val[2] = 0;

	Tuner->CH_Ctrl[13].Ctrl_Num = RFSYN_VCO_BIAS ;
	Tuner->CH_Ctrl[13].size = 6 ;
	Tuner->CH_Ctrl[13].addr[0] = 110;
	Tuner->CH_Ctrl[13].bit[0] = 2;
	Tuner->CH_Ctrl[13].val[0] = 0;
	Tuner->CH_Ctrl[13].addr[1] = 110;
	Tuner->CH_Ctrl[13].bit[1] = 3;
	Tuner->CH_Ctrl[13].val[1] = 0;
	Tuner->CH_Ctrl[13].addr[2] = 110;
	Tuner->CH_Ctrl[13].bit[2] = 4;
	Tuner->CH_Ctrl[13].val[2] = 0;
	Tuner->CH_Ctrl[13].addr[3] = 110;
	Tuner->CH_Ctrl[13].bit[3] = 5;
	Tuner->CH_Ctrl[13].val[3] = 0;
	Tuner->CH_Ctrl[13].addr[4] = 110;
	Tuner->CH_Ctrl[13].bit[4] = 6;
	Tuner->CH_Ctrl[13].val[4] = 0;
	Tuner->CH_Ctrl[13].addr[5] = 110;
	Tuner->CH_Ctrl[13].bit[5] = 7;
	Tuner->CH_Ctrl[13].val[5] = 1;

	Tuner->CH_Ctrl[14].Ctrl_Num = CHCAL_INT_MOD_RF ;
	Tuner->CH_Ctrl[14].size = 7 ;
	Tuner->CH_Ctrl[14].addr[0] = 14;
	Tuner->CH_Ctrl[14].bit[0] = 0;
	Tuner->CH_Ctrl[14].val[0] = 0;
	Tuner->CH_Ctrl[14].addr[1] = 14;
	Tuner->CH_Ctrl[14].bit[1] = 1;
	Tuner->CH_Ctrl[14].val[1] = 0;
	Tuner->CH_Ctrl[14].addr[2] = 14;
	Tuner->CH_Ctrl[14].bit[2] = 2;
	Tuner->CH_Ctrl[14].val[2] = 0;
	Tuner->CH_Ctrl[14].addr[3] = 14;
	Tuner->CH_Ctrl[14].bit[3] = 3;
	Tuner->CH_Ctrl[14].val[3] = 0;
	Tuner->CH_Ctrl[14].addr[4] = 14;
	Tuner->CH_Ctrl[14].bit[4] = 4;
	Tuner->CH_Ctrl[14].val[4] = 0;
	Tuner->CH_Ctrl[14].addr[5] = 14;
	Tuner->CH_Ctrl[14].bit[5] = 5;
	Tuner->CH_Ctrl[14].val[5] = 0;
	Tuner->CH_Ctrl[14].addr[6] = 14;
	Tuner->CH_Ctrl[14].bit[6] = 6;
	Tuner->CH_Ctrl[14].val[6] = 0;

	Tuner->CH_Ctrl[15].Ctrl_Num = CHCAL_FRAC_MOD_RF ;
	Tuner->CH_Ctrl[15].size = 18 ;
	Tuner->CH_Ctrl[15].addr[0] = 17;
	Tuner->CH_Ctrl[15].bit[0] = 6;
	Tuner->CH_Ctrl[15].val[0] = 0;
	Tuner->CH_Ctrl[15].addr[1] = 17;
	Tuner->CH_Ctrl[15].bit[1] = 7;
	Tuner->CH_Ctrl[15].val[1] = 0;
	Tuner->CH_Ctrl[15].addr[2] = 16;
	Tuner->CH_Ctrl[15].bit[2] = 0;
	Tuner->CH_Ctrl[15].val[2] = 0;
	Tuner->CH_Ctrl[15].addr[3] = 16;
	Tuner->CH_Ctrl[15].bit[3] = 1;
	Tuner->CH_Ctrl[15].val[3] = 0;
	Tuner->CH_Ctrl[15].addr[4] = 16;
	Tuner->CH_Ctrl[15].bit[4] = 2;
	Tuner->CH_Ctrl[15].val[4] = 0;
	Tuner->CH_Ctrl[15].addr[5] = 16;
	Tuner->CH_Ctrl[15].bit[5] = 3;
	Tuner->CH_Ctrl[15].val[5] = 0;
	Tuner->CH_Ctrl[15].addr[6] = 16;
	Tuner->CH_Ctrl[15].bit[6] = 4;
	Tuner->CH_Ctrl[15].val[6] = 0;
	Tuner->CH_Ctrl[15].addr[7] = 16;
	Tuner->CH_Ctrl[15].bit[7] = 5;
	Tuner->CH_Ctrl[15].val[7] = 0;
	Tuner->CH_Ctrl[15].addr[8] = 16;
	Tuner->CH_Ctrl[15].bit[8] = 6;
	Tuner->CH_Ctrl[15].val[8] = 0;
	Tuner->CH_Ctrl[15].addr[9] = 16;
	Tuner->CH_Ctrl[15].bit[9] = 7;
	Tuner->CH_Ctrl[15].val[9] = 0;
	Tuner->CH_Ctrl[15].addr[10] = 15;
	Tuner->CH_Ctrl[15].bit[10] = 0;
	Tuner->CH_Ctrl[15].val[10] = 0;
	Tuner->CH_Ctrl[15].addr[11] = 15;
	Tuner->CH_Ctrl[15].bit[11] = 1;
	Tuner->CH_Ctrl[15].val[11] = 0;
	Tuner->CH_Ctrl[15].addr[12] = 15;
	Tuner->CH_Ctrl[15].bit[12] = 2;
	Tuner->CH_Ctrl[15].val[12] = 0;
	Tuner->CH_Ctrl[15].addr[13] = 15;
	Tuner->CH_Ctrl[15].bit[13] = 3;
	Tuner->CH_Ctrl[15].val[13] = 0;
	Tuner->CH_Ctrl[15].addr[14] = 15;
	Tuner->CH_Ctrl[15].bit[14] = 4;
	Tuner->CH_Ctrl[15].val[14] = 0;
	Tuner->CH_Ctrl[15].addr[15] = 15;
	Tuner->CH_Ctrl[15].bit[15] = 5;
	Tuner->CH_Ctrl[15].val[15] = 0;
	Tuner->CH_Ctrl[15].addr[16] = 15;
	Tuner->CH_Ctrl[15].bit[16] = 6;
	Tuner->CH_Ctrl[15].val[16] = 1;
	Tuner->CH_Ctrl[15].addr[17] = 15;
	Tuner->CH_Ctrl[15].bit[17] = 7;
	Tuner->CH_Ctrl[15].val[17] = 1;

	Tuner->CH_Ctrl[16].Ctrl_Num = RFSYN_LPF_R ;
	Tuner->CH_Ctrl[16].size = 5 ;
	Tuner->CH_Ctrl[16].addr[0] = 112;
	Tuner->CH_Ctrl[16].bit[0] = 0;
	Tuner->CH_Ctrl[16].val[0] = 0;
	Tuner->CH_Ctrl[16].addr[1] = 112;
	Tuner->CH_Ctrl[16].bit[1] = 1;
	Tuner->CH_Ctrl[16].val[1] = 0;
	Tuner->CH_Ctrl[16].addr[2] = 112;
	Tuner->CH_Ctrl[16].bit[2] = 2;
	Tuner->CH_Ctrl[16].val[2] = 0;
	Tuner->CH_Ctrl[16].addr[3] = 112;
	Tuner->CH_Ctrl[16].bit[3] = 3;
	Tuner->CH_Ctrl[16].val[3] = 0;
	Tuner->CH_Ctrl[16].addr[4] = 112;
	Tuner->CH_Ctrl[16].bit[4] = 4;
	Tuner->CH_Ctrl[16].val[4] = 1;

	Tuner->CH_Ctrl[17].Ctrl_Num = CHCAL_EN_INT_RF ;
	Tuner->CH_Ctrl[17].size = 1 ;
	Tuner->CH_Ctrl[17].addr[0] = 14;
	Tuner->CH_Ctrl[17].bit[0] = 7;
	Tuner->CH_Ctrl[17].val[0] = 0;

	Tuner->CH_Ctrl[18].Ctrl_Num = TG_LO_DIVVAL ;
	Tuner->CH_Ctrl[18].size = 4 ;
	Tuner->CH_Ctrl[18].addr[0] = 107;
	Tuner->CH_Ctrl[18].bit[0] = 3;
	Tuner->CH_Ctrl[18].val[0] = 0;
	Tuner->CH_Ctrl[18].addr[1] = 107;
	Tuner->CH_Ctrl[18].bit[1] = 4;
	Tuner->CH_Ctrl[18].val[1] = 0;
	Tuner->CH_Ctrl[18].addr[2] = 107;
	Tuner->CH_Ctrl[18].bit[2] = 5;
	Tuner->CH_Ctrl[18].val[2] = 0;
	Tuner->CH_Ctrl[18].addr[3] = 107;
	Tuner->CH_Ctrl[18].bit[3] = 6;
	Tuner->CH_Ctrl[18].val[3] = 0;

	Tuner->CH_Ctrl[19].Ctrl_Num = TG_LO_SELVAL ;
	Tuner->CH_Ctrl[19].size = 3 ;
	Tuner->CH_Ctrl[19].addr[0] = 107;
	Tuner->CH_Ctrl[19].bit[0] = 7;
	Tuner->CH_Ctrl[19].val[0] = 1;
	Tuner->CH_Ctrl[19].addr[1] = 106;
	Tuner->CH_Ctrl[19].bit[1] = 0;
	Tuner->CH_Ctrl[19].val[1] = 1;
	Tuner->CH_Ctrl[19].addr[2] = 106;
	Tuner->CH_Ctrl[19].bit[2] = 1;
	Tuner->CH_Ctrl[19].val[2] = 1;

	Tuner->CH_Ctrl[20].Ctrl_Num = TG_DIV_VAL ;
	Tuner->CH_Ctrl[20].size = 11 ;
	Tuner->CH_Ctrl[20].addr[0] = 109;
	Tuner->CH_Ctrl[20].bit[0] = 2;
	Tuner->CH_Ctrl[20].val[0] = 0;
	Tuner->CH_Ctrl[20].addr[1] = 109;
	Tuner->CH_Ctrl[20].bit[1] = 3;
	Tuner->CH_Ctrl[20].val[1] = 0;
	Tuner->CH_Ctrl[20].addr[2] = 109;
	Tuner->CH_Ctrl[20].bit[2] = 4;
	Tuner->CH_Ctrl[20].val[2] = 0;
	Tuner->CH_Ctrl[20].addr[3] = 109;
	Tuner->CH_Ctrl[20].bit[3] = 5;
	Tuner->CH_Ctrl[20].val[3] = 0;
	Tuner->CH_Ctrl[20].addr[4] = 109;
	Tuner->CH_Ctrl[20].bit[4] = 6;
	Tuner->CH_Ctrl[20].val[4] = 0;
	Tuner->CH_Ctrl[20].addr[5] = 109;
	Tuner->CH_Ctrl[20].bit[5] = 7;
	Tuner->CH_Ctrl[20].val[5] = 0;
	Tuner->CH_Ctrl[20].addr[6] = 108;
	Tuner->CH_Ctrl[20].bit[6] = 0;
	Tuner->CH_Ctrl[20].val[6] = 0;
	Tuner->CH_Ctrl[20].addr[7] = 108;
	Tuner->CH_Ctrl[20].bit[7] = 1;
	Tuner->CH_Ctrl[20].val[7] = 0;
	Tuner->CH_Ctrl[20].addr[8] = 108;
	Tuner->CH_Ctrl[20].bit[8] = 2;
	Tuner->CH_Ctrl[20].val[8] = 1;
	Tuner->CH_Ctrl[20].addr[9] = 108;
	Tuner->CH_Ctrl[20].bit[9] = 3;
	Tuner->CH_Ctrl[20].val[9] = 1;
	Tuner->CH_Ctrl[20].addr[10] = 108;
	Tuner->CH_Ctrl[20].bit[10] = 4;
	Tuner->CH_Ctrl[20].val[10] = 1;

	Tuner->CH_Ctrl[21].Ctrl_Num = TG_VCO_BIAS ;
	Tuner->CH_Ctrl[21].size = 6 ;
	Tuner->CH_Ctrl[21].addr[0] = 106;
	Tuner->CH_Ctrl[21].bit[0] = 2;
	Tuner->CH_Ctrl[21].val[0] = 0;
	Tuner->CH_Ctrl[21].addr[1] = 106;
	Tuner->CH_Ctrl[21].bit[1] = 3;
	Tuner->CH_Ctrl[21].val[1] = 0;
	Tuner->CH_Ctrl[21].addr[2] = 106;
	Tuner->CH_Ctrl[21].bit[2] = 4;
	Tuner->CH_Ctrl[21].val[2] = 0;
	Tuner->CH_Ctrl[21].addr[3] = 106;
	Tuner->CH_Ctrl[21].bit[3] = 5;
	Tuner->CH_Ctrl[21].val[3] = 0;
	Tuner->CH_Ctrl[21].addr[4] = 106;
	Tuner->CH_Ctrl[21].bit[4] = 6;
	Tuner->CH_Ctrl[21].val[4] = 0;
	Tuner->CH_Ctrl[21].addr[5] = 106;
	Tuner->CH_Ctrl[21].bit[5] = 7;
	Tuner->CH_Ctrl[21].val[5] = 1;

	Tuner->CH_Ctrl[22].Ctrl_Num = SEQ_EXTPOWERUP ;
	Tuner->CH_Ctrl[22].size = 1 ;
	Tuner->CH_Ctrl[22].addr[0] = 138;
	Tuner->CH_Ctrl[22].bit[0] = 4;
	Tuner->CH_Ctrl[22].val[0] = 1;

	Tuner->CH_Ctrl[23].Ctrl_Num = OVERRIDE_2 ;
	Tuner->CH_Ctrl[23].size = 1 ;
	Tuner->CH_Ctrl[23].addr[0] = 17;
	Tuner->CH_Ctrl[23].bit[0] = 5;
	Tuner->CH_Ctrl[23].val[0] = 0;

	Tuner->CH_Ctrl[24].Ctrl_Num = OVERRIDE_3 ;
	Tuner->CH_Ctrl[24].size = 1 ;
	Tuner->CH_Ctrl[24].addr[0] = 111;
	Tuner->CH_Ctrl[24].bit[0] = 3;
	Tuner->CH_Ctrl[24].val[0] = 0;

	Tuner->CH_Ctrl[25].Ctrl_Num = OVERRIDE_4 ;
	Tuner->CH_Ctrl[25].size = 1 ;
	Tuner->CH_Ctrl[25].addr[0] = 112;
	Tuner->CH_Ctrl[25].bit[0] = 7;
	Tuner->CH_Ctrl[25].val[0] = 0;

	Tuner->CH_Ctrl[26].Ctrl_Num = SEQ_FSM_PULSE ;
	Tuner->CH_Ctrl[26].size = 1 ;
	Tuner->CH_Ctrl[26].addr[0] = 136;
	Tuner->CH_Ctrl[26].bit[0] = 7;
	Tuner->CH_Ctrl[26].val[0] = 0;

	Tuner->CH_Ctrl[27].Ctrl_Num = GPIO_4B ;
	Tuner->CH_Ctrl[27].size = 1 ;
	Tuner->CH_Ctrl[27].addr[0] = 149;
	Tuner->CH_Ctrl[27].bit[0] = 7;
	Tuner->CH_Ctrl[27].val[0] = 0;

	Tuner->CH_Ctrl[28].Ctrl_Num = GPIO_3B ;
	Tuner->CH_Ctrl[28].size = 1 ;
	Tuner->CH_Ctrl[28].addr[0] = 149;
	Tuner->CH_Ctrl[28].bit[0] = 6;
	Tuner->CH_Ctrl[28].val[0] = 0;

	Tuner->CH_Ctrl[29].Ctrl_Num = GPIO_4 ;
	Tuner->CH_Ctrl[29].size = 1 ;
	Tuner->CH_Ctrl[29].addr[0] = 149;
	Tuner->CH_Ctrl[29].bit[0] = 5;
	Tuner->CH_Ctrl[29].val[0] = 1;

	Tuner->CH_Ctrl[30].Ctrl_Num = GPIO_3 ;
	Tuner->CH_Ctrl[30].size = 1 ;
	Tuner->CH_Ctrl[30].addr[0] = 149;
	Tuner->CH_Ctrl[30].bit[0] = 4;
	Tuner->CH_Ctrl[30].val[0] = 1;

	Tuner->CH_Ctrl[31].Ctrl_Num = GPIO_1B ;
	Tuner->CH_Ctrl[31].size = 1 ;
	Tuner->CH_Ctrl[31].addr[0] = 149;
	Tuner->CH_Ctrl[31].bit[0] = 3;
	Tuner->CH_Ctrl[31].val[0] = 0;

	Tuner->CH_Ctrl[32].Ctrl_Num = DAC_A_ENABLE ;
	Tuner->CH_Ctrl[32].size = 1 ;
	Tuner->CH_Ctrl[32].addr[0] = 93;
	Tuner->CH_Ctrl[32].bit[0] = 1;
	Tuner->CH_Ctrl[32].val[0] = 0;

	Tuner->CH_Ctrl[33].Ctrl_Num = DAC_B_ENABLE ;
	Tuner->CH_Ctrl[33].size = 1 ;
	Tuner->CH_Ctrl[33].addr[0] = 93;
	Tuner->CH_Ctrl[33].bit[0] = 0;
	Tuner->CH_Ctrl[33].val[0] = 0;

	Tuner->CH_Ctrl[34].Ctrl_Num = DAC_DIN_A ;
	Tuner->CH_Ctrl[34].size = 6 ;
	Tuner->CH_Ctrl[34].addr[0] = 92;
	Tuner->CH_Ctrl[34].bit[0] = 2;
	Tuner->CH_Ctrl[34].val[0] = 0;
	Tuner->CH_Ctrl[34].addr[1] = 92;
	Tuner->CH_Ctrl[34].bit[1] = 3;
	Tuner->CH_Ctrl[34].val[1] = 0;
	Tuner->CH_Ctrl[34].addr[2] = 92;
	Tuner->CH_Ctrl[34].bit[2] = 4;
	Tuner->CH_Ctrl[34].val[2] = 0;
	Tuner->CH_Ctrl[34].addr[3] = 92;
	Tuner->CH_Ctrl[34].bit[3] = 5;
	Tuner->CH_Ctrl[34].val[3] = 0;
	Tuner->CH_Ctrl[34].addr[4] = 92;
	Tuner->CH_Ctrl[34].bit[4] = 6;
	Tuner->CH_Ctrl[34].val[4] = 0;
	Tuner->CH_Ctrl[34].addr[5] = 92;
	Tuner->CH_Ctrl[34].bit[5] = 7;
	Tuner->CH_Ctrl[34].val[5] = 0;

	Tuner->CH_Ctrl[35].Ctrl_Num = DAC_DIN_B ;
	Tuner->CH_Ctrl[35].size = 6 ;
	Tuner->CH_Ctrl[35].addr[0] = 93;
	Tuner->CH_Ctrl[35].bit[0] = 2;
	Tuner->CH_Ctrl[35].val[0] = 0;
	Tuner->CH_Ctrl[35].addr[1] = 93;
	Tuner->CH_Ctrl[35].bit[1] = 3;
	Tuner->CH_Ctrl[35].val[1] = 0;
	Tuner->CH_Ctrl[35].addr[2] = 93;
	Tuner->CH_Ctrl[35].bit[2] = 4;
	Tuner->CH_Ctrl[35].val[2] = 0;
	Tuner->CH_Ctrl[35].addr[3] = 93;
	Tuner->CH_Ctrl[35].bit[3] = 5;
	Tuner->CH_Ctrl[35].val[3] = 0;
	Tuner->CH_Ctrl[35].addr[4] = 93;
	Tuner->CH_Ctrl[35].bit[4] = 6;
	Tuner->CH_Ctrl[35].val[4] = 0;
	Tuner->CH_Ctrl[35].addr[5] = 93;
	Tuner->CH_Ctrl[35].bit[5] = 7;
	Tuner->CH_Ctrl[35].val[5] = 0;

#ifdef _MXL_PRODUCTION
	Tuner->CH_Ctrl[36].Ctrl_Num = RFSYN_EN_DIV ;
	Tuner->CH_Ctrl[36].size = 1 ;
	Tuner->CH_Ctrl[36].addr[0] = 109;
	Tuner->CH_Ctrl[36].bit[0] = 1;
	Tuner->CH_Ctrl[36].val[0] = 1;

	Tuner->CH_Ctrl[37].Ctrl_Num = RFSYN_DIVM ;
	Tuner->CH_Ctrl[37].size = 2 ;
	Tuner->CH_Ctrl[37].addr[0] = 112;
	Tuner->CH_Ctrl[37].bit[0] = 5;
	Tuner->CH_Ctrl[37].val[0] = 0;
	Tuner->CH_Ctrl[37].addr[1] = 112;
	Tuner->CH_Ctrl[37].bit[1] = 6;
	Tuner->CH_Ctrl[37].val[1] = 0;

	Tuner->CH_Ctrl[38].Ctrl_Num = DN_BYPASS_AGC_I2C ;
	Tuner->CH_Ctrl[38].size = 1 ;
	Tuner->CH_Ctrl[38].addr[0] = 65;
	Tuner->CH_Ctrl[38].bit[0] = 1;
	Tuner->CH_Ctrl[38].val[0] = 0;
#endif

	return 0 ;
}















// MaxLinear source code - MXL5005_c.cpp



// MXL5005.cpp : Defines the initialization routines for the DLL.
// 2.6.12


//#ifdef _MXL_HEADER
//#include "stdafx.h"
//#endif
//#include "MXL5005_c.h"


void InitTunerControls(Tuner_struct *Tuner)
{
	MXL5005_RegisterInit(Tuner) ;
	MXL5005_ControlInit(Tuner) ;
#ifdef _MXL_INTERNAL
	MXL5005_MXLControlInit(Tuner) ;
#endif
}



///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// Function:       MXL_ConfigTuner                                           //
//                                                                           //
// Description:    Configure MXL5005Tuner structure for desired              //
//                 Channel Bandwidth/Channel Frequency                       //
//                                                                           //
//                                                                           //
// Functions used:                                                           //
//                 MXL_SynthIFLO_Calc                                      //
//                                                                           //
// Inputs:                                                                   //
//                 Tuner_struct: structure defined at higher level           //
//                 Mode:         Tuner Mode (Analog/Digital)                 //
//                 IF_Mode:      IF Mode ( Zero/Low )                        //
//				   Bandwidth:    Filter Channel Bandwidth (in Hz)            //
//                 IF_out:       Desired IF out Frequency (in Hz)            //
//                 Fxtal:        Crystal Frerquency (in Hz)                  //
//			   TOP:			 0: Dual AGC; Value: take over point         //
//				   IF_OUT_LOAD:	 IF out load resistor (200/300 Ohms)		 //
//				   CLOCK_OUT:	 0: Turn off clock out; 1: turn on clock out //
//				   DIV_OUT:      0: Div-1; 1: Div-4							 //
//				   CAPSELECT:	 0: Disable On-chip pulling cap; 1: Enable   //
//				   EN_RSSI:		 0: Disable RSSI; 1: Enable RSSI			 //
//                                                                           //
// Outputs:                                                                  //
//                 Tuner                                                     //
//                                                                           //
// Return:                                                                   //
//                 0 : Successful                                            //
//               > 0 : Failed                                                //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////
_u16 MXL5005_TunerConfig(Tuner_struct *Tuner,
		_u8		Mode,		// 0: Analog Mode ; 1: Digital Mode
		_u8		IF_mode,	// for Analog Mode, 0: zero IF; 1: low IF
		_u32	Bandwidth,	// filter  channel bandwidth (6, 7, 8)
		_u32	IF_out,		// Desired IF Out Frequency
		_u32	Fxtal,		// XTAL Frequency
		_u8		AGC_Mode,	// AGC Mode - Dual AGC: 0, Single AGC: 1
		_u16	TOP,        // 0: Dual AGC; Value: take over point
		_u16	IF_OUT_LOAD, // IF Out Load Resistor (200 / 300 Ohms)
		_u8		CLOCK_OUT, 	// 0: turn off clock out; 1: turn on clock out
		_u8		DIV_OUT,	// 0: Div-1; 1: Div-4
		_u8		CAPSELECT, 	// 0: disable On-Chip pulling cap; 1: enable
		_u8		EN_RSSI, 	// 0: disable RSSI; 1: enable RSSI
		_u8		Mod_Type,	// Modulation Type;
							// 0 - Default;	1 - DVB-T; 2 - ATSC; 3 - QAM; 4 - Analog Cable
		_u8		TF_Type		// Tracking Filter
							// 0 - Default; 1 - Off; 2 - Type C; 3 - Type C-H
		)
{
	_u16 status = 0 ;

	Tuner->Mode = Mode ;
	Tuner->IF_Mode = IF_mode ;
	Tuner->Chan_Bandwidth = Bandwidth ;
	Tuner->IF_OUT = IF_out ;
	Tuner->Fxtal = Fxtal ;
	Tuner->AGC_Mode = AGC_Mode ;
	Tuner->TOP = TOP ;
	Tuner->IF_OUT_LOAD = IF_OUT_LOAD ;
	Tuner->CLOCK_OUT = CLOCK_OUT ;
	Tuner->DIV_OUT = DIV_OUT ;
	Tuner->CAPSELECT = CAPSELECT ;
	Tuner->EN_RSSI = EN_RSSI ;
	Tuner->Mod_Type = Mod_Type ;
	Tuner->TF_Type = TF_Type ;



	//
	//	Initialize all the controls and registers
	//
	InitTunerControls (Tuner) ;
	//
	// Synthesizer LO frequency calculation
	//
	MXL_SynthIFLO_Calc( Tuner ) ;

	return status ;
}

///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// Function:       MXL_SynthIFLO_Calc                                        //
//                                                                           //
// Description:    Calculate Internal IF-LO Frequency                        //
//                                                                           //
// Globals:                                                                  //
//                 NONE                                                      //
//                                                                           //
// Functions used:                                                           //
//                 NONE                                                      //
//                                                                           //
// Inputs:                                                                   //
//                 Tuner_struct: structure defined at higher level           //
//                                                                           //
// Outputs:                                                                  //
//                 Tuner                                                     //
//                                                                           //
// Return:                                                                   //
//                 0 : Successful                                            //
//               > 0 : Failed                                                //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////
void MXL_SynthIFLO_Calc(Tuner_struct *Tuner)
{
	if (Tuner->Mode == 1) // Digital Mode
	{
		Tuner->IF_LO = Tuner->IF_OUT ;
	}
	else // Analog Mode
	{
		if(Tuner->IF_Mode == 0) // Analog Zero IF mode
		{
			Tuner->IF_LO = Tuner->IF_OUT + 400000 ;
		}
		else // Analog Low IF mode
		{
			Tuner->IF_LO = Tuner->IF_OUT + Tuner->Chan_Bandwidth/2 ;
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// Function:       MXL_SynthRFTGLO_Calc                                      //
//                                                                           //
// Description:    Calculate Internal RF-LO frequency and                    //
//                 internal Tone-Gen(TG)-LO frequency                        //
//                                                                           //
// Globals:                                                                  //
//                 NONE                                                      //
//                                                                           //
// Functions used:                                                           //
//                 NONE                                                      //
//                                                                           //
// Inputs:                                                                   //
//                 Tuner_struct: structure defined at higher level           //
//                                                                           //
// Outputs:                                                                  //
//                 Tuner                                                     //
//                                                                           //
// Return:                                                                   //
//                 0 : Successful                                            //
//               > 0 : Failed                                                //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////
void MXL_SynthRFTGLO_Calc(Tuner_struct *Tuner)
{
	if (Tuner->Mode == 1) // Digital Mode
	{
			//remove 20.48MHz setting for 2.6.10
			Tuner->RF_LO = Tuner->RF_IN ;
			Tuner->TG_LO = Tuner->RF_IN - 750000 ;  //change for 2.6.6
	}
	else // Analog Mode
	{
		if(Tuner->IF_Mode == 0) // Analog Zero IF mode
		{
			Tuner->RF_LO = Tuner->RF_IN - 400000 ;
			Tuner->TG_LO = Tuner->RF_IN - 1750000 ;
		}
		else // Analog Low IF mode
		{
			Tuner->RF_LO = Tuner->RF_IN - Tuner->Chan_Bandwidth/2 ;
			Tuner->TG_LO = Tuner->RF_IN - Tuner->Chan_Bandwidth + 500000 ;
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// Function:       MXL_OverwriteICDefault                                    //
//                                                                           //
// Description:    Overwrite the Default Register Setting                    //
//                                                                           //
//                                                                           //
// Functions used:                                                           //
//                                                                           //
// Inputs:                                                                   //
//                 Tuner_struct: structure defined at higher level           //
// Outputs:                                                                  //
//                 Tuner                                                     //
//                                                                           //
// Return:                                                                   //
//                 0 : Successful                                            //
//               > 0 : Failed                                                //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////
_u16 MXL_OverwriteICDefault( Tuner_struct *Tuner)
{
	_u16 status = 0 ;

	status += MXL_ControlWrite(Tuner, OVERRIDE_1, 1) ;
	status += MXL_ControlWrite(Tuner, OVERRIDE_2, 1) ;
	status += MXL_ControlWrite(Tuner, OVERRIDE_3, 1) ;
	status += MXL_ControlWrite(Tuner, OVERRIDE_4, 1) ;

	return status ;
}

///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// Function:       MXL_BlockInit                                             //
//                                                                           //
// Description:    Tuner Initialization as a function of 'User Settings'     //
//                  * User settings in Tuner strcuture must be assigned      //
//                    first                                                  //
//                                                                           //
// Globals:                                                                  //
//                 NONE                                                      //
//                                                                           //
// Functions used:                                                           //
//                 Tuner_struct: structure defined at higher level           //
//                                                                           //
// Inputs:                                                                   //
//                 Tuner       : Tuner structure defined at higher level     //
//                                                                           //
// Outputs:                                                                  //
//                 Tuner                                                     //
//                                                                           //
// Return:                                                                   //
//                 0 : Successful                                            //
//               > 0 : Failed                                                //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////
_u16 MXL_BlockInit( Tuner_struct *Tuner )
{
	_u16 status = 0 ;

	status += MXL_OverwriteICDefault(Tuner) ;

	//
	// Downconverter Control
	//															  Dig Ana
	status += MXL_ControlWrite(Tuner, DN_IQTN_AMP_CUT, Tuner->Mode ? 1 : 0) ;

	//
	// Filter Control
	//															  Dig  Ana
	status += MXL_ControlWrite(Tuner, BB_MODE,          Tuner->Mode ? 0 : 1) ;
	status += MXL_ControlWrite(Tuner, BB_BUF,           Tuner->Mode ? 3 : 2) ;
	status += MXL_ControlWrite(Tuner, BB_BUF_OA,        Tuner->Mode ? 1 : 0) ;

	status += MXL_ControlWrite(Tuner, BB_IQSWAP,        Tuner->Mode ? 0 : 1) ;
	status += MXL_ControlWrite(Tuner, BB_INITSTATE_DLPF_TUNE,  0) ;

	// Initialize Low-Pass Filter
	if (Tuner->Mode) { // Digital Mode
		switch (Tuner->Chan_Bandwidth) {
			case 8000000:
				status += MXL_ControlWrite(Tuner, BB_DLPF_BANDSEL, 0) ;
				break ;
			case 7000000:
				status += MXL_ControlWrite(Tuner, BB_DLPF_BANDSEL, 2) ;
				break ;
			case 6000000:
				status += MXL_ControlWrite(Tuner, BB_DLPF_BANDSEL, 3) ;
				break ;
		}
	} else { // Analog Mode
		switch (Tuner->Chan_Bandwidth) {
			case 8000000:													// Low Zero
				status += MXL_ControlWrite(Tuner, BB_ALPF_BANDSELECT, (Tuner->IF_Mode ? 0 : 3)) ;
				break ;
			case 7000000:
				status += MXL_ControlWrite(Tuner, BB_ALPF_BANDSELECT, (Tuner->IF_Mode ? 1 : 4)) ;
				break ;
			case 6000000:
				status += MXL_ControlWrite(Tuner, BB_ALPF_BANDSELECT, (Tuner->IF_Mode ? 2 : 5)) ;
				break ;
		}
	}

	//
	// Charge Pump Control
	//															       Dig  Ana
	status += MXL_ControlWrite(Tuner, RFSYN_CHP_GAIN,      Tuner->Mode ? 5 : 8) ;
	status += MXL_ControlWrite(Tuner, RFSYN_EN_CHP_HIGAIN, Tuner->Mode ?  1 :  1) ;
	status += MXL_ControlWrite(Tuner, EN_CHP_LIN_B, Tuner->Mode ? 0 : 0) ;

	//
	// AGC TOP Control
	//
	if (Tuner->AGC_Mode == 0) // Dual AGC
	{
		status += MXL_ControlWrite(Tuner, AGC_IF, 15) ;
		status += MXL_ControlWrite(Tuner, AGC_RF, 15) ;
	}
	else //  Single AGC Mode                                     Dig  Ana
		status += MXL_ControlWrite(Tuner, AGC_RF, Tuner->Mode? 15 : 12) ;


	if (Tuner->TOP == 55) // TOP == 5.5
		status += MXL_ControlWrite(Tuner, AGC_IF, 0x0) ;

	if (Tuner->TOP == 72) // TOP == 7.2
		status += MXL_ControlWrite(Tuner, AGC_IF, 0x1) ;

	if (Tuner->TOP == 92) // TOP == 9.2
		status += MXL_ControlWrite(Tuner, AGC_IF, 0x2) ;

	if (Tuner->TOP == 110) // TOP == 11.0
		status += MXL_ControlWrite(Tuner, AGC_IF, 0x3) ;

	if (Tuner->TOP == 129) // TOP == 12.9
		status += MXL_ControlWrite(Tuner, AGC_IF, 0x4) ;

	if (Tuner->TOP == 147) // TOP == 14.7
		status += MXL_ControlWrite(Tuner, AGC_IF, 0x5) ;

	if (Tuner->TOP == 168) // TOP == 16.8
		status += MXL_ControlWrite(Tuner, AGC_IF, 0x6) ;

	if (Tuner->TOP == 194) // TOP == 19.4
		status += MXL_ControlWrite(Tuner, AGC_IF, 0x7) ;

	if (Tuner->TOP == 212) // TOP == 21.2
		status += MXL_ControlWrite(Tuner, AGC_IF, 0x9) ;

	if (Tuner->TOP == 232) // TOP == 23.2
		status += MXL_ControlWrite(Tuner, AGC_IF, 0xA) ;

	if (Tuner->TOP == 252) // TOP == 25.2
		status += MXL_ControlWrite(Tuner, AGC_IF, 0xB) ;

	if (Tuner->TOP == 271) // TOP == 27.1
		status += MXL_ControlWrite(Tuner, AGC_IF, 0xC) ;

	if (Tuner->TOP == 292) // TOP == 29.2
		status += MXL_ControlWrite(Tuner, AGC_IF, 0xD) ;

	if (Tuner->TOP == 317) // TOP == 31.7
		status += MXL_ControlWrite(Tuner, AGC_IF, 0xE) ;

	if (Tuner->TOP == 349) // TOP == 34.9
		status += MXL_ControlWrite(Tuner, AGC_IF, 0xF) ;

	//
	// IF Synthesizer Control
	//
	status += MXL_IFSynthInit( Tuner ) ;

	//
	// IF UpConverter Control
	if (Tuner->IF_OUT_LOAD == 200)
	{
		status += MXL_ControlWrite(Tuner, DRV_RES_SEL, 6) ;
		status += MXL_ControlWrite(Tuner, I_DRIVER, 2) ;
	}
	if (Tuner->IF_OUT_LOAD == 300)
	{
		status += MXL_ControlWrite(Tuner, DRV_RES_SEL, 4) ;
		status += MXL_ControlWrite(Tuner, I_DRIVER, 1) ;
	}

	//
	// Anti-Alias Filtering Control
	//
	// initialise Anti-Aliasing Filter
	if (Tuner->Mode) {// Digital Mode
		if (Tuner->IF_OUT >= 4000000UL && Tuner->IF_OUT <= 6280000UL) {
			status += MXL_ControlWrite(Tuner, EN_AAF, 1) ;
			status += MXL_ControlWrite(Tuner, EN_3P, 1) ;
			status += MXL_ControlWrite(Tuner, EN_AUX_3P, 1) ;
			status += MXL_ControlWrite(Tuner, SEL_AAF_BAND, 0) ;
		}
		if ((Tuner->IF_OUT == 36125000UL) || (Tuner->IF_OUT == 36150000UL)) {
			status += MXL_ControlWrite(Tuner, EN_AAF, 1) ;
			status += MXL_ControlWrite(Tuner, EN_3P, 1) ;
			status += MXL_ControlWrite(Tuner, EN_AUX_3P, 1) ;
			status += MXL_ControlWrite(Tuner, SEL_AAF_BAND, 1) ;
		}
		if (Tuner->IF_OUT > 36150000UL) {
			status += MXL_ControlWrite(Tuner, EN_AAF, 0) ;
			status += MXL_ControlWrite(Tuner, EN_3P, 1) ;
			status += MXL_ControlWrite(Tuner, EN_AUX_3P, 1) ;
			status += MXL_ControlWrite(Tuner, SEL_AAF_BAND, 1) ;
		}
	} else { // Analog Mode
		if (Tuner->IF_OUT >= 4000000UL && Tuner->IF_OUT <= 5000000UL)
		{
			status += MXL_ControlWrite(Tuner, EN_AAF, 1) ;
			status += MXL_ControlWrite(Tuner, EN_3P, 1) ;
			status += MXL_ControlWrite(Tuner, EN_AUX_3P, 1) ;
			status += MXL_ControlWrite(Tuner, SEL_AAF_BAND, 0) ;
		}
		if (Tuner->IF_OUT > 5000000UL)
		{
			status += MXL_ControlWrite(Tuner, EN_AAF, 0) ;
			status += MXL_ControlWrite(Tuner, EN_3P, 0) ;
			status += MXL_ControlWrite(Tuner, EN_AUX_3P, 0) ;
			status += MXL_ControlWrite(Tuner, SEL_AAF_BAND, 0) ;
		}
	}

	//
	// Demod Clock Out
	//
	if (Tuner->CLOCK_OUT)
		status += MXL_ControlWrite(Tuner, SEQ_ENCLK16_CLK_OUT, 1) ;
	else
		status += MXL_ControlWrite(Tuner, SEQ_ENCLK16_CLK_OUT, 0) ;

	if (Tuner->DIV_OUT == 1)
		status += MXL_ControlWrite(Tuner, SEQ_SEL4_16B, 1) ;
	if (Tuner->DIV_OUT == 0)
		status += MXL_ControlWrite(Tuner, SEQ_SEL4_16B, 0) ;

	//
	// Crystal Control
	//
	if (Tuner->CAPSELECT)
		status += MXL_ControlWrite(Tuner, XTAL_CAPSELECT, 1) ;
	else
		status += MXL_ControlWrite(Tuner, XTAL_CAPSELECT, 0) ;

	if (Tuner->Fxtal >= 12000000UL && Tuner->Fxtal <= 16000000UL)
		status += MXL_ControlWrite(Tuner, IF_SEL_DBL, 1) ;
	if (Tuner->Fxtal > 16000000UL && Tuner->Fxtal <= 32000000UL)
		status += MXL_ControlWrite(Tuner, IF_SEL_DBL, 0) ;

	if (Tuner->Fxtal >= 12000000UL && Tuner->Fxtal <= 22000000UL)
		status += MXL_ControlWrite(Tuner, RFSYN_R_DIV, 3) ;
	if (Tuner->Fxtal > 22000000UL && Tuner->Fxtal <= 32000000UL)
		status += MXL_ControlWrite(Tuner, RFSYN_R_DIV, 0) ;

	//
	// Misc Controls
	//
	if (Tuner->Mode == 0 && Tuner->IF_Mode == 1) // Analog LowIF mode
		status += MXL_ControlWrite(Tuner, SEQ_EXTIQFSMPULSE, 0);
	else
		status += MXL_ControlWrite(Tuner, SEQ_EXTIQFSMPULSE, 1);

//	status += MXL_ControlRead(Tuner, IF_DIVVAL, &IF_DIVVAL_Val) ;

	// Set TG_R_DIV
	status += MXL_ControlWrite(Tuner, TG_R_DIV, MXL_Ceiling(Tuner->Fxtal, 1000000)) ;

	//
	// Apply Default value to BB_INITSTATE_DLPF_TUNE
	//



	//
	// RSSI Control
	//
	if(Tuner->EN_RSSI)
	{
		status += MXL_ControlWrite(Tuner, SEQ_EXTSYNTHCALIF, 1) ;
		status += MXL_ControlWrite(Tuner, SEQ_EXTDCCAL, 1) ;
		status += MXL_ControlWrite(Tuner, AGC_EN_RSSI, 1) ;
		status += MXL_ControlWrite(Tuner, RFA_ENCLKRFAGC, 1) ;
		// RSSI reference point
		status += MXL_ControlWrite(Tuner, RFA_RSSI_REF, 2) ;
		status += MXL_ControlWrite(Tuner, RFA_RSSI_REFH, 3) ;
		status += MXL_ControlWrite(Tuner, RFA_RSSI_REFL, 1) ;
		// TOP point
		status += MXL_ControlWrite(Tuner, RFA_FLR, 0) ;
		status += MXL_ControlWrite(Tuner, RFA_CEIL, 12) ;
	}

	//
	// Modulation type bit settings
	// Override the control values preset
	//
	if (Tuner->Mod_Type == MXL_DVBT) // DVB-T Mode
	{
		Tuner->AGC_Mode = 1 ;		// Single AGC Mode

		// Enable RSSI
		status += MXL_ControlWrite(Tuner, SEQ_EXTSYNTHCALIF, 1) ;
		status += MXL_ControlWrite(Tuner, SEQ_EXTDCCAL, 1) ;
		status += MXL_ControlWrite(Tuner, AGC_EN_RSSI, 1) ;
		status += MXL_ControlWrite(Tuner, RFA_ENCLKRFAGC, 1) ;
		// RSSI reference point
		status += MXL_ControlWrite(Tuner, RFA_RSSI_REF, 3) ;
		status += MXL_ControlWrite(Tuner, RFA_RSSI_REFH, 5) ;
		status += MXL_ControlWrite(Tuner, RFA_RSSI_REFL, 1) ;
		// TOP point
		status += MXL_ControlWrite(Tuner, RFA_FLR, 2) ;
		status += MXL_ControlWrite(Tuner, RFA_CEIL, 13) ;
		if (Tuner->IF_OUT <= 6280000UL)	// Low IF
			status += MXL_ControlWrite(Tuner, BB_IQSWAP, 0) ;
		else // High IF
			status += MXL_ControlWrite(Tuner, BB_IQSWAP, 1) ;

	}
	if (Tuner->Mod_Type == MXL_ATSC) // ATSC Mode
	{
		Tuner->AGC_Mode = 1 ;		// Single AGC Mode

		// Enable RSSI
		status += MXL_ControlWrite(Tuner, SEQ_EXTSYNTHCALIF, 1) ;
		status += MXL_ControlWrite(Tuner, SEQ_EXTDCCAL, 1) ;
		status += MXL_ControlWrite(Tuner, AGC_EN_RSSI, 1) ;
		status += MXL_ControlWrite(Tuner, RFA_ENCLKRFAGC, 1) ;
		// RSSI reference point
		status += MXL_ControlWrite(Tuner, RFA_RSSI_REF, 2) ;
		status += MXL_ControlWrite(Tuner, RFA_RSSI_REFH, 4) ;
		status += MXL_ControlWrite(Tuner, RFA_RSSI_REFL, 1) ;
		// TOP point
		status += MXL_ControlWrite(Tuner, RFA_FLR, 2) ;
		status += MXL_ControlWrite(Tuner, RFA_CEIL, 13) ;

		status += MXL_ControlWrite(Tuner, BB_INITSTATE_DLPF_TUNE, 1) ;
		status += MXL_ControlWrite(Tuner, RFSYN_CHP_GAIN, 5) ;	// Low Zero
		if (Tuner->IF_OUT <= 6280000UL)	// Low IF
			status += MXL_ControlWrite(Tuner, BB_IQSWAP, 0) ;
		else // High IF
			status += MXL_ControlWrite(Tuner, BB_IQSWAP, 1) ;
	}
	if (Tuner->Mod_Type == MXL_QAM) // QAM Mode
	{
		Tuner->Mode = MXL_DIGITAL_MODE;

		//Tuner->AGC_Mode = 1 ;		// Single AGC Mode

		// Disable RSSI											//change here for v2.6.5
		status += MXL_ControlWrite(Tuner, SEQ_EXTSYNTHCALIF, 1) ;
		status += MXL_ControlWrite(Tuner, SEQ_EXTDCCAL, 1) ;
		status += MXL_ControlWrite(Tuner, AGC_EN_RSSI, 0) ;
		status += MXL_ControlWrite(Tuner, RFA_ENCLKRFAGC, 1) ;

		// RSSI reference point
		status += MXL_ControlWrite(Tuner, RFA_RSSI_REFH, 5) ;
		status += MXL_ControlWrite(Tuner, RFA_RSSI_REF, 3) ;
		status += MXL_ControlWrite(Tuner, RFA_RSSI_REFL, 2) ;

		status += MXL_ControlWrite(Tuner, RFSYN_CHP_GAIN, 3) ;	//change here for v2.6.5

		if (Tuner->IF_OUT <= 6280000UL)	// Low IF
			status += MXL_ControlWrite(Tuner, BB_IQSWAP, 0) ;
		else // High IF
			status += MXL_ControlWrite(Tuner, BB_IQSWAP, 1) ;
	}
	if (Tuner->Mod_Type == MXL_ANALOG_CABLE) // Analog Cable Mode
	{
		//Tuner->Mode = MXL_DIGITAL_MODE ;
		Tuner->AGC_Mode = 1 ;		// Single AGC Mode

		// Disable RSSI
		status += MXL_ControlWrite(Tuner, SEQ_EXTSYNTHCALIF, 1) ;
		status += MXL_ControlWrite(Tuner, SEQ_EXTDCCAL, 1) ;
		status += MXL_ControlWrite(Tuner, AGC_EN_RSSI, 0) ;
		status += MXL_ControlWrite(Tuner, RFA_ENCLKRFAGC, 1) ;

		status += MXL_ControlWrite(Tuner, AGC_IF, 1) ;  //change for 2.6.3
		status += MXL_ControlWrite(Tuner, AGC_RF, 15) ;

		status += MXL_ControlWrite(Tuner, BB_IQSWAP, 1) ;
	}

	if (Tuner->Mod_Type == MXL_ANALOG_OTA) //Analog OTA Terrestrial mode add for 2.6.7
	{
		//Tuner->Mode = MXL_ANALOG_MODE;

		// Enable RSSI
		status += MXL_ControlWrite(Tuner, SEQ_EXTSYNTHCALIF, 1) ;
		status += MXL_ControlWrite(Tuner, SEQ_EXTDCCAL, 1) ;
		status += MXL_ControlWrite(Tuner, AGC_EN_RSSI, 1) ;
		status += MXL_ControlWrite(Tuner, RFA_ENCLKRFAGC, 1) ;

		// RSSI reference point
		status += MXL_ControlWrite(Tuner, RFA_RSSI_REFH, 5) ;
		status += MXL_ControlWrite(Tuner, RFA_RSSI_REF, 3) ;
		status += MXL_ControlWrite(Tuner, RFA_RSSI_REFL, 2) ;

		status += MXL_ControlWrite(Tuner, RFSYN_CHP_GAIN, 3) ;

		status += MXL_ControlWrite(Tuner, BB_IQSWAP, 1) ;
	}

	// RSSI disable
	if(Tuner->EN_RSSI==0)
	{
		status += MXL_ControlWrite(Tuner, SEQ_EXTSYNTHCALIF, 1) ;
		status += MXL_ControlWrite(Tuner, SEQ_EXTDCCAL, 1) ;
		status += MXL_ControlWrite(Tuner, AGC_EN_RSSI, 0) ;
		status += MXL_ControlWrite(Tuner, RFA_ENCLKRFAGC, 1) ;
	}

	return status ;
}

///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// Function:       MXL_IFSynthInit                                           //
//                                                                           //
// Description:    Tuner IF Synthesizer related register initialization      //
//                                                                           //
// Globals:                                                                  //
//                 NONE                                                      //
//                                                                           //
// Functions used:                                                           //
//                 Tuner_struct: structure defined at higher level           //
//                                                                           //
// Inputs:                                                                   //
//                 Tuner       : Tuner structure defined at higher level     //
//                                                                           //
// Outputs:                                                                  //
//                 Tuner                                                     //
//                                                                           //
// Return:                                                                   //
//                 0 : Successful                                            //
//               > 0 : Failed                                                //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////
_u16 MXL_IFSynthInit( Tuner_struct * Tuner )
{
	_u16 status = 0 ;
	// Declare Local Variables
	_u32	Fref = 0 ;
	_u32	Kdbl, intModVal ;
    _u32	fracModVal ;
	Kdbl = 2 ;

	if (Tuner->Fxtal >= 12000000UL && Tuner->Fxtal <= 16000000UL)
		Kdbl = 2 ;
	if (Tuner->Fxtal > 16000000UL && Tuner->Fxtal <= 32000000UL)
		Kdbl = 1 ;

	//
	// IF Synthesizer Control
	//
	if (Tuner->Mode == 0 && Tuner->IF_Mode == 1) // Analog Low IF mode
	{
		if (Tuner->IF_LO == 41000000UL) {
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x08) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x0C) ;
			Fref = 328000000UL ;
		}
		if (Tuner->IF_LO == 47000000UL) {
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x08) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x08) ;
			Fref = 376000000UL ;
		}
		if (Tuner->IF_LO == 54000000UL) {
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x10) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x0C) ;
			Fref = 324000000UL ;
		}
		if (Tuner->IF_LO == 60000000UL) {
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x10) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x08) ;
			Fref = 360000000UL ;
		}
		if (Tuner->IF_LO == 39250000UL) {
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x08) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x0C) ;
			Fref = 314000000UL ;
		}
		if (Tuner->IF_LO == 39650000UL) {
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x08) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x0C) ;
			Fref = 317200000UL ;
		}
		if (Tuner->IF_LO == 40150000UL) {
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x08) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x0C) ;
			Fref = 321200000UL ;
		}
		if (Tuner->IF_LO == 40650000UL) {
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x08) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x0C) ;
			Fref = 325200000UL ;
		}
	}

	if (Tuner->Mode || (Tuner->Mode == 0 && Tuner->IF_Mode == 0))
	{
		if (Tuner->IF_LO == 57000000UL) {
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x10) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x08) ;
			Fref = 342000000UL ;
		}
		if (Tuner->IF_LO == 44000000UL) {
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x08) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x08) ;
			Fref = 352000000UL ;
		}
		if (Tuner->IF_LO == 43750000UL) {
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x08) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x08) ;
			Fref = 350000000UL ;
		}
		if (Tuner->IF_LO == 36650000UL) {
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x04) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x08) ;
			Fref = 366500000UL ;
		}
		if (Tuner->IF_LO == 36150000UL) {
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x04) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x08) ;
			Fref = 361500000UL ;
		}
		if (Tuner->IF_LO == 36000000UL) {
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x04) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x08) ;
			Fref = 360000000UL ;
		}
		if (Tuner->IF_LO == 35250000UL) {
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x04) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x08) ;
			Fref = 352500000UL ;
		}
		if (Tuner->IF_LO == 34750000UL) {
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x04) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x08) ;
			Fref = 347500000UL ;
		}
		if (Tuner->IF_LO == 6280000UL) {
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x07) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x08) ;
			Fref = 376800000UL ;
		}
		if (Tuner->IF_LO == 5000000UL) {
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x09) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x08) ;
			Fref = 360000000UL ;
		}
		if (Tuner->IF_LO == 4500000UL) {
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x06) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x08) ;
			Fref = 360000000UL ;
		}
		if (Tuner->IF_LO == 4570000UL) {
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x06) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x08) ;
			Fref = 365600000UL ;
		}
		if (Tuner->IF_LO == 4000000UL) {
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x05) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x08) ;
			Fref = 360000000UL ;
		}
		if (Tuner->IF_LO == 57400000UL)
		{
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x10) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x08) ;
			Fref = 344400000UL ;
		}
		if (Tuner->IF_LO == 44400000UL)
		{
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x08) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x08) ;
			Fref = 355200000UL ;
		}
		if (Tuner->IF_LO == 44150000UL)
		{
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x08) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x08) ;
			Fref = 353200000UL ;
		}
		if (Tuner->IF_LO == 37050000UL)
		{
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x04) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x08) ;
			Fref = 370500000UL ;
		}
		if (Tuner->IF_LO == 36550000UL)
		{
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x04) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x08) ;
			Fref = 365500000UL ;
		}
		if (Tuner->IF_LO == 36125000UL) {
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x04) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x08) ;
			Fref = 361250000UL ;
		}
		if (Tuner->IF_LO == 6000000UL) {
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x07) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x08) ;
			Fref = 360000000UL ;
		}
		if (Tuner->IF_LO == 5400000UL)
		{
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x07) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x0C) ;
			Fref = 324000000UL ;
		}
		if (Tuner->IF_LO == 5380000UL) {
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x07) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x0C) ;
			Fref = 322800000UL ;
		}
		if (Tuner->IF_LO == 5200000UL) {
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x09) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x08) ;
			Fref = 374400000UL ;
		}
		if (Tuner->IF_LO == 4900000UL)
		{
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x09) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x08) ;
			Fref = 352800000UL ;
		}
		if (Tuner->IF_LO == 4400000UL)
		{
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x06) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x08) ;
			Fref = 352000000UL ;
		}
		if (Tuner->IF_LO == 4063000UL)  //add for 2.6.8
		{
			status += MXL_ControlWrite(Tuner, IF_DIVVAL,   0x05) ;
			status += MXL_ControlWrite(Tuner, IF_VCO_BIAS, 0x08) ;
			Fref = 365670000UL ;
		}
	}
	// CHCAL_INT_MOD_IF
	// CHCAL_FRAC_MOD_IF
	intModVal = Fref / (Tuner->Fxtal * Kdbl/2) ;
	status += MXL_ControlWrite(Tuner, CHCAL_INT_MOD_IF, intModVal ) ;

	fracModVal = (2<<15)*(Fref/1000 - (Tuner->Fxtal/1000 * Kdbl/2) * intModVal);
	fracModVal = fracModVal / ((Tuner->Fxtal * Kdbl/2)/1000) ;
	status += MXL_ControlWrite(Tuner, CHCAL_FRAC_MOD_IF, fracModVal) ;



	return status ;
}

///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// Function:       MXL_GetXtalInt                                            //
//                                                                           //
// Description:    return the Crystal Integration Value for				     //
//				   TG_VCO_BIAS calculation									 //
//                                                                           //
// Globals:                                                                  //
//                 NONE                                                      //
//                                                                           //
// Functions used:                                                           //
//                 NONE												     //
//                                                                           //
// Inputs:                                                                   //
//                 Crystal Frequency Value in Hz						     //
//                                                                           //
// Outputs:                                                                  //
//                 Calculated Crystal Frequency Integration Value            //
//                                                                           //
// Return:                                                                   //
//                 0 : Successful                                            //
//               > 0 : Failed                                                //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////
_u32 MXL_GetXtalInt(_u32 Xtal_Freq)
{
	if ((Xtal_Freq % 1000000) == 0)
		return (Xtal_Freq / 10000) ;
	else
		return (((Xtal_Freq / 1000000) + 1)*100) ;
}

///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// Function:       MXL5005_TuneRF                                            //
//                                                                           //
// Description:    Set control names to tune to requested RF_IN frequency    //
//                                                                           //
// Globals:                                                                  //
//                 None                                                      //
//                                                                           //
// Functions used:                                                           //
//                 MXL_SynthRFTGLO_Calc                                      //
//                 MXL5005_ControlWrite                                      //
//				   MXL_GetXtalInt											 //
//                                                                           //
// Inputs:                                                                   //
//                 Tuner       : Tuner structure defined at higher level     //
//                                                                           //
// Outputs:                                                                  //
//                 Tuner                                                     //
//                                                                           //
// Return:                                                                   //
//                 0 : Successful                                            //
//                 1 : Unsuccessful                                          //
///////////////////////////////////////////////////////////////////////////////
_u16 MXL_TuneRF(Tuner_struct *Tuner, _u32 RF_Freq)
{
	// Declare Local Variables
	_u16 status = 0 ;
	_u32 divider_val, E3, E4, E5, E5A ;
	_u32 Fmax, Fmin, FmaxBin, FminBin ;
	_u32 Kdbl_RF = 2;
	_u32 tg_divval ;
	_u32 tg_lo ;
	_u32 Xtal_Int ;

	_u32 Fref_TG;
	_u32 Fvco;
//	_u32 temp;


	Xtal_Int = MXL_GetXtalInt(Tuner->Fxtal ) ;

	Tuner->RF_IN = RF_Freq ;

	MXL_SynthRFTGLO_Calc( Tuner ) ;

	if (Tuner->Fxtal >= 12000000UL && Tuner->Fxtal <= 22000000UL)
		Kdbl_RF = 2 ;
	if (Tuner->Fxtal > 22000000 && Tuner->Fxtal <= 32000000)
		Kdbl_RF = 1 ;

	//
	// Downconverter Controls
	//
	// Look-Up Table Implementation for:
	//	DN_POLY
	//	DN_RFGAIN
	//	DN_CAP_RFLPF
	//	DN_EN_VHFUHFBAR
	//	DN_GAIN_ADJUST
	// Change the boundary reference from RF_IN to RF_LO
	if (Tuner->RF_LO < 40000000UL) {
		return -1;
	}
	if (Tuner->RF_LO >= 40000000UL && Tuner->RF_LO <= 75000000UL) {
		// Look-Up Table implementation
		status += MXL_ControlWrite(Tuner, DN_POLY,              2) ;
		status += MXL_ControlWrite(Tuner, DN_RFGAIN,            3) ;
		status += MXL_ControlWrite(Tuner, DN_CAP_RFLPF,         423) ;
		status += MXL_ControlWrite(Tuner, DN_EN_VHFUHFBAR,      1) ;
		status += MXL_ControlWrite(Tuner, DN_GAIN_ADJUST,       1) ;
	}
	if (Tuner->RF_LO > 75000000UL && Tuner->RF_LO <= 100000000UL) {
		// Look-Up Table implementation
		status += MXL_ControlWrite(Tuner, DN_POLY,              3) ;
		status += MXL_ControlWrite(Tuner, DN_RFGAIN,            3) ;
		status += MXL_ControlWrite(Tuner, DN_CAP_RFLPF,         222) ;
		status += MXL_ControlWrite(Tuner, DN_EN_VHFUHFBAR,      1) ;
		status += MXL_ControlWrite(Tuner, DN_GAIN_ADJUST,       1) ;
	}
	if (Tuner->RF_LO > 100000000UL && Tuner->RF_LO <= 150000000UL) {
		// Look-Up Table implementation
		status += MXL_ControlWrite(Tuner, DN_POLY,              3) ;
		status += MXL_ControlWrite(Tuner, DN_RFGAIN,            3) ;
		status += MXL_ControlWrite(Tuner, DN_CAP_RFLPF,         147) ;
		status += MXL_ControlWrite(Tuner, DN_EN_VHFUHFBAR,      1) ;
		status += MXL_ControlWrite(Tuner, DN_GAIN_ADJUST,       2) ;
	}
	if (Tuner->RF_LO > 150000000UL && Tuner->RF_LO <= 200000000UL) {
		// Look-Up Table implementation
		status += MXL_ControlWrite(Tuner, DN_POLY,              3) ;
		status += MXL_ControlWrite(Tuner, DN_RFGAIN,            3) ;
		status += MXL_ControlWrite(Tuner, DN_CAP_RFLPF,         9) ;
		status += MXL_ControlWrite(Tuner, DN_EN_VHFUHFBAR,      1) ;
		status += MXL_ControlWrite(Tuner, DN_GAIN_ADJUST,       2) ;
	}
	if (Tuner->RF_LO > 200000000UL && Tuner->RF_LO <= 300000000UL) {
		// Look-Up Table implementation
		status += MXL_ControlWrite(Tuner, DN_POLY,              3) ;
		status += MXL_ControlWrite(Tuner, DN_RFGAIN,            3) ;
		status += MXL_ControlWrite(Tuner, DN_CAP_RFLPF,         0) ;
		status += MXL_ControlWrite(Tuner, DN_EN_VHFUHFBAR,      1) ;
		status += MXL_ControlWrite(Tuner, DN_GAIN_ADJUST,       3) ;
	}
	if (Tuner->RF_LO > 300000000UL && Tuner->RF_LO <= 650000000UL) {
		// Look-Up Table implementation
		status += MXL_ControlWrite(Tuner, DN_POLY,              3) ;
		status += MXL_ControlWrite(Tuner, DN_RFGAIN,            1) ;
		status += MXL_ControlWrite(Tuner, DN_CAP_RFLPF,         0) ;
		status += MXL_ControlWrite(Tuner, DN_EN_VHFUHFBAR,      0) ;
		status += MXL_ControlWrite(Tuner, DN_GAIN_ADJUST,       3) ;
	}
	if (Tuner->RF_LO > 650000000UL && Tuner->RF_LO <= 900000000UL) {
		// Look-Up Table implementation
		status += MXL_ControlWrite(Tuner, DN_POLY,              3) ;
		status += MXL_ControlWrite(Tuner, DN_RFGAIN,            2) ;
		status += MXL_ControlWrite(Tuner, DN_CAP_RFLPF,         0) ;
		status += MXL_ControlWrite(Tuner, DN_EN_VHFUHFBAR,      0) ;
		status += MXL_ControlWrite(Tuner, DN_GAIN_ADJUST,       3) ;
	}
	if (Tuner->RF_LO > 900000000UL) {
		return -1;
	}
	//	DN_IQTNBUF_AMP
	//	DN_IQTNGNBFBIAS_BST
	if (Tuner->RF_LO >= 40000000UL && Tuner->RF_LO <= 75000000UL) {
		status += MXL_ControlWrite(Tuner, DN_IQTNBUF_AMP,       1) ;
		status += MXL_ControlWrite(Tuner, DN_IQTNGNBFBIAS_BST,  0) ;
	}
	if (Tuner->RF_LO > 75000000UL && Tuner->RF_LO <= 100000000UL) {
		status += MXL_ControlWrite(Tuner, DN_IQTNBUF_AMP,       1) ;
		status += MXL_ControlWrite(Tuner, DN_IQTNGNBFBIAS_BST,  0) ;
	}
	if (Tuner->RF_LO > 100000000UL && Tuner->RF_LO <= 150000000UL) {
		status += MXL_ControlWrite(Tuner, DN_IQTNBUF_AMP,       1) ;
		status += MXL_ControlWrite(Tuner, DN_IQTNGNBFBIAS_BST,  0) ;
	}
	if (Tuner->RF_LO > 150000000UL && Tuner->RF_LO <= 200000000UL) {
		status += MXL_ControlWrite(Tuner, DN_IQTNBUF_AMP,       1) ;
		status += MXL_ControlWrite(Tuner, DN_IQTNGNBFBIAS_BST,  0) ;
	}
	if (Tuner->RF_LO > 200000000UL && Tuner->RF_LO <= 300000000UL) {
		status += MXL_ControlWrite(Tuner, DN_IQTNBUF_AMP,       1) ;
		status += MXL_ControlWrite(Tuner, DN_IQTNGNBFBIAS_BST,  0) ;
	}
	if (Tuner->RF_LO > 300000000UL && Tuner->RF_LO <= 400000000UL) {
		status += MXL_ControlWrite(Tuner, DN_IQTNBUF_AMP,       1) ;
		status += MXL_ControlWrite(Tuner, DN_IQTNGNBFBIAS_BST,  0) ;
	}
	if (Tuner->RF_LO > 400000000UL && Tuner->RF_LO <= 450000000UL) {
		status += MXL_ControlWrite(Tuner, DN_IQTNBUF_AMP,       1) ;
		status += MXL_ControlWrite(Tuner, DN_IQTNGNBFBIAS_BST,  0) ;
	}
	if (Tuner->RF_LO > 450000000UL && Tuner->RF_LO <= 500000000UL) {
		status += MXL_ControlWrite(Tuner, DN_IQTNBUF_AMP,       1) ;
		status += MXL_ControlWrite(Tuner, DN_IQTNGNBFBIAS_BST,  0) ;
	}
	if (Tuner->RF_LO > 500000000UL && Tuner->RF_LO <= 550000000UL) {
		status += MXL_ControlWrite(Tuner, DN_IQTNBUF_AMP,       1) ;
		status += MXL_ControlWrite(Tuner, DN_IQTNGNBFBIAS_BST,  0) ;
	}
	if (Tuner->RF_LO > 550000000UL && Tuner->RF_LO <= 600000000UL) {
		status += MXL_ControlWrite(Tuner, DN_IQTNBUF_AMP,       1) ;
		status += MXL_ControlWrite(Tuner, DN_IQTNGNBFBIAS_BST,  0) ;
	}
	if (Tuner->RF_LO > 600000000UL && Tuner->RF_LO <= 650000000UL) {
		status += MXL_ControlWrite(Tuner, DN_IQTNBUF_AMP,       1) ;
		status += MXL_ControlWrite(Tuner, DN_IQTNGNBFBIAS_BST,  0) ;
	}
	if (Tuner->RF_LO > 650000000UL && Tuner->RF_LO <= 700000000UL) {
		status += MXL_ControlWrite(Tuner, DN_IQTNBUF_AMP,       1) ;
		status += MXL_ControlWrite(Tuner, DN_IQTNGNBFBIAS_BST,  0) ;
	}
	if (Tuner->RF_LO > 700000000UL && Tuner->RF_LO <= 750000000UL) {
		status += MXL_ControlWrite(Tuner, DN_IQTNBUF_AMP,       1) ;
		status += MXL_ControlWrite(Tuner, DN_IQTNGNBFBIAS_BST,  0) ;
	}
	if (Tuner->RF_LO > 750000000UL && Tuner->RF_LO <= 800000000UL) {
		status += MXL_ControlWrite(Tuner, DN_IQTNBUF_AMP,       1) ;
		status += MXL_ControlWrite(Tuner, DN_IQTNGNBFBIAS_BST,  0) ;
	}
	if (Tuner->RF_LO > 800000000UL && Tuner->RF_LO <= 850000000UL) {
		status += MXL_ControlWrite(Tuner, DN_IQTNBUF_AMP,       10) ;
		status += MXL_ControlWrite(Tuner, DN_IQTNGNBFBIAS_BST,  1) ;
	}
	if (Tuner->RF_LO > 850000000UL && Tuner->RF_LO <= 900000000UL) {
		status += MXL_ControlWrite(Tuner, DN_IQTNBUF_AMP,       10) ;
		status += MXL_ControlWrite(Tuner, DN_IQTNGNBFBIAS_BST,  1) ;
	}

	//
	// Set RF Synth and LO Path Control
	//
	// Look-Up table implementation for:
	//	RFSYN_EN_OUTMUX
	//	RFSYN_SEL_VCO_OUT
	//	RFSYN_SEL_VCO_HI
	//  RFSYN_SEL_DIVM
	//	RFSYN_RF_DIV_BIAS
	//	DN_SEL_FREQ
	//
	// Set divider_val, Fmax, Fmix to use in Equations
	FminBin = 28000000UL ;
	FmaxBin = 42500000UL ;
	if (Tuner->RF_LO >= 40000000UL && Tuner->RF_LO <= FmaxBin) {
		status += MXL_ControlWrite(Tuner, RFSYN_EN_OUTMUX,     1) ;
		status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_OUT,   0) ;
		status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_HI,    0) ;
		status += MXL_ControlWrite(Tuner, RFSYN_SEL_DIVM,      0) ;
		status += MXL_ControlWrite(Tuner, RFSYN_RF_DIV_BIAS,   1) ;
		status += MXL_ControlWrite(Tuner, DN_SEL_FREQ,         1) ;
		divider_val = 64 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}
	FminBin = 42500000UL ;
	FmaxBin = 56000000UL ;
	if (Tuner->RF_LO > FminBin && Tuner->RF_LO <= FmaxBin) {
		status += MXL_ControlWrite(Tuner, RFSYN_EN_OUTMUX,     1) ;
		status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_OUT,   0) ;
		status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_HI,    1) ;
		status += MXL_ControlWrite(Tuner, RFSYN_SEL_DIVM,      0) ;
		status += MXL_ControlWrite(Tuner, RFSYN_RF_DIV_BIAS,   1) ;
		status += MXL_ControlWrite(Tuner, DN_SEL_FREQ,         1) ;
		divider_val = 64 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}
	FminBin = 56000000UL ;
	FmaxBin = 85000000UL ;
	if (Tuner->RF_LO > FminBin && Tuner->RF_LO <= FmaxBin) {
		status += MXL_ControlWrite(Tuner, RFSYN_EN_OUTMUX,     0) ;
		status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_OUT,   1) ;
		status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_HI,    0) ;
		status += MXL_ControlWrite(Tuner, RFSYN_SEL_DIVM,      0) ;
		status += MXL_ControlWrite(Tuner, RFSYN_RF_DIV_BIAS,   1) ;
		status += MXL_ControlWrite(Tuner, DN_SEL_FREQ,         1) ;
		divider_val = 32 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}
	FminBin = 85000000UL ;
	FmaxBin = 112000000UL ;
	if (Tuner->RF_LO > FminBin && Tuner->RF_LO <= FmaxBin) {
		status += MXL_ControlWrite(Tuner, RFSYN_EN_OUTMUX,     0) ;
		status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_OUT,   1) ;
		status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_HI,    1) ;
		status += MXL_ControlWrite(Tuner, RFSYN_SEL_DIVM,      0) ;
		status += MXL_ControlWrite(Tuner, RFSYN_RF_DIV_BIAS,   1) ;
		status += MXL_ControlWrite(Tuner, DN_SEL_FREQ,         1) ;
		divider_val = 32 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}
	FminBin = 112000000UL ;
	FmaxBin = 170000000UL ;
	if (Tuner->RF_LO > FminBin && Tuner->RF_LO <= FmaxBin) {
		status += MXL_ControlWrite(Tuner, RFSYN_EN_OUTMUX,     0) ;
		status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_OUT,   1) ;
		status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_HI,    0) ;
		status += MXL_ControlWrite(Tuner, RFSYN_SEL_DIVM,      0) ;
		status += MXL_ControlWrite(Tuner, RFSYN_RF_DIV_BIAS,   1) ;
		status += MXL_ControlWrite(Tuner, DN_SEL_FREQ,         2) ;
		divider_val = 16 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}
	FminBin = 170000000UL ;
	FmaxBin = 225000000UL ;
	if (Tuner->RF_LO > FminBin && Tuner->RF_LO <= FmaxBin) {
		status += MXL_ControlWrite(Tuner, RFSYN_EN_OUTMUX,     0) ;
		status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_OUT,   1) ;
		status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_HI,    1) ;
		status += MXL_ControlWrite(Tuner, RFSYN_SEL_DIVM,      0) ;
		status += MXL_ControlWrite(Tuner, RFSYN_RF_DIV_BIAS,   1) ;
		status += MXL_ControlWrite(Tuner, DN_SEL_FREQ,         2) ;
		divider_val = 16 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}
	FminBin = 225000000UL ;
	FmaxBin = 300000000UL ;
	if (Tuner->RF_LO > FminBin && Tuner->RF_LO <= FmaxBin) {
		status += MXL_ControlWrite(Tuner, RFSYN_EN_OUTMUX,     0) ;
		status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_OUT,   1) ;
		status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_HI,    0) ;
		status += MXL_ControlWrite(Tuner, RFSYN_SEL_DIVM,      0) ;
		status += MXL_ControlWrite(Tuner, RFSYN_RF_DIV_BIAS,   1) ;
		status += MXL_ControlWrite(Tuner, DN_SEL_FREQ,         4) ;
		divider_val = 8 ;
		Fmax = 340000000UL ;
		Fmin = FminBin ;
	}
	FminBin = 300000000UL ;
	FmaxBin = 340000000UL ;
	if (Tuner->RF_LO > FminBin && Tuner->RF_LO <= FmaxBin) {
		status += MXL_ControlWrite(Tuner, RFSYN_EN_OUTMUX,     1) ;
		status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_OUT,   0) ;
		status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_HI,    0) ;
		status += MXL_ControlWrite(Tuner, RFSYN_SEL_DIVM,      0) ;
		status += MXL_ControlWrite(Tuner, RFSYN_RF_DIV_BIAS,   1) ;
		status += MXL_ControlWrite(Tuner, DN_SEL_FREQ,         0) ;
		divider_val = 8 ;
		Fmax = FmaxBin ;
		Fmin = 225000000UL ;
	}
	FminBin = 340000000UL ;
	FmaxBin = 450000000UL ;
	if (Tuner->RF_LO > FminBin && Tuner->RF_LO <= FmaxBin) {
		status += MXL_ControlWrite(Tuner, RFSYN_EN_OUTMUX,     1) ;
		status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_OUT,   0) ;
		status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_HI,    1) ;
		status += MXL_ControlWrite(Tuner, RFSYN_SEL_DIVM,      0) ;
		status += MXL_ControlWrite(Tuner, RFSYN_RF_DIV_BIAS,   2) ;
		status += MXL_ControlWrite(Tuner, DN_SEL_FREQ,         0) ;
		divider_val = 8 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}
	FminBin = 450000000UL ;
	FmaxBin = 680000000UL ;
	if (Tuner->RF_LO > FminBin && Tuner->RF_LO <= FmaxBin) {
		status += MXL_ControlWrite(Tuner, RFSYN_EN_OUTMUX,     0) ;
		status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_OUT,   1) ;
		status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_HI,    0) ;
		status += MXL_ControlWrite(Tuner, RFSYN_SEL_DIVM,      1) ;
		status += MXL_ControlWrite(Tuner, RFSYN_RF_DIV_BIAS,   1) ;
		status += MXL_ControlWrite(Tuner, DN_SEL_FREQ,         0) ;
		divider_val = 4 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}
	FminBin = 680000000UL ;
	FmaxBin = 900000000UL ;
	if (Tuner->RF_LO > FminBin && Tuner->RF_LO <= FmaxBin) {
		status += MXL_ControlWrite(Tuner, RFSYN_EN_OUTMUX,     0) ;
		status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_OUT,   1) ;
		status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_HI,    1) ;
		status += MXL_ControlWrite(Tuner, RFSYN_SEL_DIVM,      1) ;
		status += MXL_ControlWrite(Tuner, RFSYN_RF_DIV_BIAS,   1) ;
		status += MXL_ControlWrite(Tuner, DN_SEL_FREQ,         0) ;
		divider_val = 4 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}

	//	CHCAL_INT_MOD_RF
	//	CHCAL_FRAC_MOD_RF
	//	RFSYN_LPF_R
	//	CHCAL_EN_INT_RF

	// Equation E3
	//	RFSYN_VCO_BIAS
	E3 = (((Fmax-Tuner->RF_LO)/1000)*32)/((Fmax-Fmin)/1000) + 8 ;
	status += MXL_ControlWrite(Tuner, RFSYN_VCO_BIAS, E3) ;

	// Equation E4
	//	CHCAL_INT_MOD_RF
	E4 = (Tuner->RF_LO*divider_val/1000)/(2*Tuner->Fxtal*Kdbl_RF/1000) ;
	MXL_ControlWrite(Tuner, CHCAL_INT_MOD_RF, E4) ;

	// Equation E5
	//	CHCAL_FRAC_MOD_RF
	//  CHCAL_EN_INT_RF
	E5 = ((2<<17)*(Tuner->RF_LO/10000*divider_val - (E4*(2*Tuner->Fxtal*Kdbl_RF)/10000)))/(2*Tuner->Fxtal*Kdbl_RF/10000) ;
	status += MXL_ControlWrite(Tuner, CHCAL_FRAC_MOD_RF, E5) ;

	// Equation E5A
	//  RFSYN_LPF_R
	E5A = (((Fmax - Tuner->RF_LO)/1000)*4/((Fmax-Fmin)/1000)) + 1 ;
	status += MXL_ControlWrite(Tuner, RFSYN_LPF_R, E5A) ;

	// Euqation E5B
	//	CHCAL_EN_INIT_RF
	status += MXL_ControlWrite(Tuner, CHCAL_EN_INT_RF, ((E5 == 0) ? 1 : 0));
	//if (E5 == 0)
	//	status += MXL_ControlWrite(Tuner, CHCAL_EN_INT_RF, 1);
	//else
	//	status += MXL_ControlWrite(Tuner, CHCAL_FRAC_MOD_RF, E5) ;

	//
	// Set TG Synth
	//
	// Look-Up table implementation for:
	//	TG_LO_DIVVAL
	//	TG_LO_SELVAL
	//
	// Set divider_val, Fmax, Fmix to use in Equations
	if (Tuner->TG_LO < 33000000UL) {
		return -1;
	}
	FminBin = 33000000UL ;
	FmaxBin = 50000000UL ;
	if (Tuner->TG_LO >= FminBin && Tuner->TG_LO <= FmaxBin) {
		status += MXL_ControlWrite(Tuner, TG_LO_DIVVAL,	0x6) ;
		status += MXL_ControlWrite(Tuner, TG_LO_SELVAL,	0x0) ;
		divider_val = 36 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}
	FminBin = 50000000UL ;
	FmaxBin = 67000000UL ;
	if (Tuner->TG_LO > FminBin && Tuner->TG_LO <= FmaxBin) {
		status += MXL_ControlWrite(Tuner, TG_LO_DIVVAL,	0x1) ;
		status += MXL_ControlWrite(Tuner, TG_LO_SELVAL,	0x0) ;
		divider_val = 24 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}
	FminBin = 67000000UL ;
	FmaxBin = 100000000UL ;
	if (Tuner->TG_LO > FminBin && Tuner->TG_LO <= FmaxBin) {
		status += MXL_ControlWrite(Tuner, TG_LO_DIVVAL,	0xC) ;
		status += MXL_ControlWrite(Tuner, TG_LO_SELVAL,	0x2) ;
		divider_val = 18 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}
	FminBin = 100000000UL ;
	FmaxBin = 150000000UL ;
	if (Tuner->TG_LO > FminBin && Tuner->TG_LO <= FmaxBin) {
		status += MXL_ControlWrite(Tuner, TG_LO_DIVVAL,	0x8) ;
		status += MXL_ControlWrite(Tuner, TG_LO_SELVAL,	0x2) ;
		divider_val = 12 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}
	FminBin = 150000000UL ;
	FmaxBin = 200000000UL ;
	if (Tuner->TG_LO > FminBin && Tuner->TG_LO <= FmaxBin) {
		status += MXL_ControlWrite(Tuner, TG_LO_DIVVAL,	0x0) ;
		status += MXL_ControlWrite(Tuner, TG_LO_SELVAL,	0x2) ;
		divider_val = 8 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}
	FminBin = 200000000UL ;
	FmaxBin = 300000000UL ;
	if (Tuner->TG_LO > FminBin && Tuner->TG_LO <= FmaxBin) {
		status += MXL_ControlWrite(Tuner, TG_LO_DIVVAL,	0x8) ;
		status += MXL_ControlWrite(Tuner, TG_LO_SELVAL,	0x3) ;
		divider_val = 6 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}
	FminBin = 300000000UL ;
	FmaxBin = 400000000UL ;
	if (Tuner->TG_LO > FminBin && Tuner->TG_LO <= FmaxBin) {
		status += MXL_ControlWrite(Tuner, TG_LO_DIVVAL,	0x0) ;
		status += MXL_ControlWrite(Tuner, TG_LO_SELVAL,	0x3) ;
		divider_val = 4 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}
	FminBin = 400000000UL ;
	FmaxBin = 600000000UL ;
	if (Tuner->TG_LO > FminBin && Tuner->TG_LO <= FmaxBin) {
		status += MXL_ControlWrite(Tuner, TG_LO_DIVVAL,	0x8) ;
		status += MXL_ControlWrite(Tuner, TG_LO_SELVAL,	0x7) ;
		divider_val = 3 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}
	FminBin = 600000000UL ;
	FmaxBin = 900000000UL ;
	if (Tuner->TG_LO > FminBin && Tuner->TG_LO <= FmaxBin) {
		status += MXL_ControlWrite(Tuner, TG_LO_DIVVAL,	0x0) ;
		status += MXL_ControlWrite(Tuner, TG_LO_SELVAL,	0x7) ;
		divider_val = 2 ;
		Fmax = FmaxBin ;
		Fmin = FminBin ;
	}

	// TG_DIV_VAL
	tg_divval = (Tuner->TG_LO*divider_val/100000)
			 *(MXL_Ceiling(Tuner->Fxtal,1000000) * 100) / (Tuner->Fxtal/1000) ;
	status += MXL_ControlWrite(Tuner, TG_DIV_VAL, tg_divval) ;

	if (Tuner->TG_LO > 600000000UL)
		status += MXL_ControlWrite(Tuner, TG_DIV_VAL, tg_divval + 1 ) ;

	Fmax = 1800000000UL ;
	Fmin = 1200000000UL ;



	// to prevent overflow of 32 bit unsigned integer, use following equation. Edit for v2.6.4
	Fref_TG = (Tuner->Fxtal/1000)/ MXL_Ceiling(Tuner->Fxtal, 1000000) ; // Fref_TF = Fref_TG*1000

	Fvco = (Tuner->TG_LO/10000) * divider_val * Fref_TG;  //Fvco = Fvco/10

	tg_lo = (((Fmax/10 - Fvco)/100)*32) / ((Fmax-Fmin)/1000)+8;

	//below equation is same as above but much harder to debug.
	//tg_lo = ( ((Fmax/10000 * Xtal_Int)/100) - ((Tuner->TG_LO/10000)*divider_val*(Tuner->Fxtal/10000)/100) )*32/((Fmax-Fmin)/10000 * Xtal_Int/100) + 8 ;


	status += MXL_ControlWrite(Tuner, TG_VCO_BIAS , tg_lo) ;



	//add for 2.6.5
	//Special setting for QAM
	if(Tuner ->Mod_Type == MXL_QAM)
	{
	if(Tuner->RF_IN < 680000000)
		status += MXL_ControlWrite(Tuner, RFSYN_CHP_GAIN, 3) ;
	else
		status += MXL_ControlWrite(Tuner, RFSYN_CHP_GAIN, 2) ;
	}


	//remove 20.48MHz setting for 2.6.10

	//
	// Off Chip Tracking Filter Control
	//
	if (Tuner->TF_Type == MXL_TF_OFF) // Tracking Filter Off State; turn off all the banks
	{
		status += MXL_ControlWrite(Tuner, DAC_A_ENABLE, 0) ;
		status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 0) ;

		status += MXL_SetGPIO(Tuner, 3, 1) ; // turn off Bank 1
		status += MXL_SetGPIO(Tuner, 1, 1) ; // turn off Bank 2
		status += MXL_SetGPIO(Tuner, 4, 1) ; // turn off Bank 3
	}

	if (Tuner->TF_Type == MXL_TF_C) // Tracking Filter type C
	{
		status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 1) ;
		status += MXL_ControlWrite(Tuner, DAC_DIN_A, 0) ;

		if (Tuner->RF_IN >= 43000000 && Tuner->RF_IN < 150000000)
		{

			status += MXL_ControlWrite(Tuner, DAC_A_ENABLE, 0) ; // Bank4 Off
			status += MXL_ControlWrite(Tuner, DAC_DIN_B, 0) ;
			status += MXL_SetGPIO(Tuner, 3, 0) ; // Bank1 On
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank3 Off
		}
		if (Tuner->RF_IN >= 150000000 && Tuner->RF_IN < 280000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_A_ENABLE, 0) ; // Bank4 Off
			status += MXL_ControlWrite(Tuner, DAC_DIN_B, 0) ;
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 0) ; // Bank2 On
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank3 Off
		}
		if (Tuner->RF_IN >= 280000000 && Tuner->RF_IN < 360000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_A_ENABLE, 0) ; // Bank4 Off
			status += MXL_ControlWrite(Tuner, DAC_DIN_B, 0) ;
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 0) ; // Bank2 On
			status += MXL_SetGPIO(Tuner, 4, 0) ; // Bank3 On
		}
		if (Tuner->RF_IN >= 360000000 && Tuner->RF_IN < 560000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_A_ENABLE, 0) ; // Bank4 Off
			status += MXL_ControlWrite(Tuner, DAC_DIN_B, 0) ;
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 4, 0) ; // Bank3 On
		}
		if (Tuner->RF_IN >= 560000000 && Tuner->RF_IN < 580000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_A_ENABLE, 1) ; // Bank4 On
			status += MXL_ControlWrite(Tuner, DAC_DIN_B, 29) ;
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 4, 0) ; // Bank3 On
		}
		if (Tuner->RF_IN >= 580000000 && Tuner->RF_IN < 630000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_A_ENABLE, 1) ; // Bank4 On
			status += MXL_ControlWrite(Tuner, DAC_DIN_B, 0) ;
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 4, 0) ; // Bank3 On
		}
		if (Tuner->RF_IN >= 630000000 && Tuner->RF_IN < 700000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_A_ENABLE, 1) ; // Bank4 On
			status += MXL_ControlWrite(Tuner, DAC_DIN_B, 16) ;
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank3 Off
		}
		if (Tuner->RF_IN >= 700000000 && Tuner->RF_IN < 760000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_A_ENABLE, 1) ; // Bank4 On
			status += MXL_ControlWrite(Tuner, DAC_DIN_B, 7) ;
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank3 Off
		}
		if (Tuner->RF_IN >= 760000000 && Tuner->RF_IN <= 900000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_A_ENABLE, 1) ; // Bank4 On
			status += MXL_ControlWrite(Tuner, DAC_DIN_B, 0) ;
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank3 Off
		}
	}

	if (Tuner->TF_Type == MXL_TF_C_H) // Tracking Filter type C-H for Hauppauge only
	{
		status += MXL_ControlWrite(Tuner, DAC_DIN_A, 0) ;

		if (Tuner->RF_IN >= 43000000 && Tuner->RF_IN < 150000000)
		{

			status += MXL_ControlWrite(Tuner, DAC_A_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 0) ; // Bank1 On
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank3 Off
		}
		if (Tuner->RF_IN >= 150000000 && Tuner->RF_IN < 280000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_A_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 3, 0) ; // Bank2 On
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank3 Off
		}
		if (Tuner->RF_IN >= 280000000 && Tuner->RF_IN < 360000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_A_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 3, 0) ; // Bank2 On
			status += MXL_SetGPIO(Tuner, 1, 0) ; // Bank3 On
		}
		if (Tuner->RF_IN >= 360000000 && Tuner->RF_IN < 560000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_A_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 1, 0) ; // Bank3 On
		}
		if (Tuner->RF_IN >= 560000000 && Tuner->RF_IN < 580000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_A_ENABLE, 1) ; // Bank4 On
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 1, 0) ; // Bank3 On
		}
		if (Tuner->RF_IN >= 580000000 && Tuner->RF_IN < 630000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_A_ENABLE, 1) ; // Bank4 On
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 1, 0) ; // Bank3 On
		}
		if (Tuner->RF_IN >= 630000000 && Tuner->RF_IN < 700000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_A_ENABLE, 1) ; // Bank4 On
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank3 Off
		}
		if (Tuner->RF_IN >= 700000000 && Tuner->RF_IN < 760000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_A_ENABLE, 1) ; // Bank4 On
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank3 Off
		}
		if (Tuner->RF_IN >= 760000000 && Tuner->RF_IN <= 900000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_A_ENABLE, 1) ; // Bank4 On
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank3 Off
		}
	}

	if (Tuner->TF_Type == MXL_TF_D) // Tracking Filter type D
	{
		status += MXL_ControlWrite(Tuner, DAC_DIN_B, 0) ;

		if (Tuner->RF_IN >= 43000000 && Tuner->RF_IN < 174000000)
		{

			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 0) ; // Bank1 On
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank3 Off
		}
		if (Tuner->RF_IN >= 174000000 && Tuner->RF_IN < 250000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 0) ; // Bank1 On
			status += MXL_SetGPIO(Tuner, 1, 0) ; // Bank2 On
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank3 Off
		}
		if (Tuner->RF_IN >= 250000000 && Tuner->RF_IN < 310000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 0) ; // Bank2 On
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank3 Off
		}
		if (Tuner->RF_IN >= 310000000 && Tuner->RF_IN < 360000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 0) ; // Bank2 On
			status += MXL_SetGPIO(Tuner, 3, 0) ; // Bank3 On
		}
		if (Tuner->RF_IN >= 360000000 && Tuner->RF_IN < 470000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 3, 0) ; // Bank3 On
		}
		if (Tuner->RF_IN >= 470000000 && Tuner->RF_IN < 640000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 1) ; // Bank4 On
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 3, 0) ; // Bank3 On
		}
		if (Tuner->RF_IN >= 640000000 && Tuner->RF_IN <= 900000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 1) ; // Bank4 On
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank3 Off
		}
	}


	if (Tuner->TF_Type == MXL_TF_D_L) // Tracking Filter type D-L for Lumanate ONLY  change for 2.6.3
	{
		status += MXL_ControlWrite(Tuner, DAC_DIN_A, 0) ;

		if (Tuner->RF_IN >= 471000000 && (Tuner->RF_IN - 471000000)%6000000 != 0) // if UHF and terrestrial => Turn off Tracking Filter
		{
			// Turn off all the banks
			status += MXL_SetGPIO(Tuner, 3, 1) ;
			status += MXL_SetGPIO(Tuner, 1, 1) ;
			status += MXL_SetGPIO(Tuner, 4, 1) ;
			status += MXL_ControlWrite(Tuner, DAC_A_ENABLE, 0) ;

			status += MXL_ControlWrite(Tuner, AGC_IF, 10) ;
		}

		else  // if VHF or cable => Turn on Tracking Filter
		{
			if (Tuner->RF_IN >= 43000000 && Tuner->RF_IN < 140000000)
			{

				status += MXL_ControlWrite(Tuner, DAC_A_ENABLE, 0) ; // Bank4 Off
				status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 On
				status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
				status += MXL_SetGPIO(Tuner, 3, 0) ; // Bank3 Off
			}
			if (Tuner->RF_IN >= 140000000 && Tuner->RF_IN < 240000000)
			{
				status += MXL_ControlWrite(Tuner, DAC_A_ENABLE, 0) ; // Bank4 Off
				status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 On
				status += MXL_SetGPIO(Tuner, 1, 0) ; // Bank2 On
				status += MXL_SetGPIO(Tuner, 3, 0) ; // Bank3 Off
			}
			if (Tuner->RF_IN >= 240000000 && Tuner->RF_IN < 340000000)
			{
				status += MXL_ControlWrite(Tuner, DAC_A_ENABLE, 0) ; // Bank4 Off
				status += MXL_SetGPIO(Tuner, 4, 0) ; // Bank1 Off
				status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 On
				status += MXL_SetGPIO(Tuner, 3, 0) ; // Bank3 Off
			}
			if (Tuner->RF_IN >= 340000000 && Tuner->RF_IN < 430000000)
			{
				status += MXL_ControlWrite(Tuner, DAC_A_ENABLE, 0) ; // Bank4 Off
				status += MXL_SetGPIO(Tuner, 4, 0) ; // Bank1 Off
				status += MXL_SetGPIO(Tuner, 1, 0) ; // Bank2 On
				status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank3 On
			}
			if (Tuner->RF_IN >= 430000000 && Tuner->RF_IN < 470000000)
			{
				status += MXL_ControlWrite(Tuner, DAC_A_ENABLE, 1) ; // Bank4 Off
				status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
				status += MXL_SetGPIO(Tuner, 1, 0) ; // Bank2 Off
				status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank3 On
			}
			if (Tuner->RF_IN >= 470000000 && Tuner->RF_IN < 570000000)
			{
				status += MXL_ControlWrite(Tuner, DAC_A_ENABLE, 1) ; // Bank4 On
				status += MXL_SetGPIO(Tuner, 4, 0) ; // Bank1 Off
				status += MXL_SetGPIO(Tuner, 1, 0) ; // Bank2 Off
				status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank3 On
			}
			if (Tuner->RF_IN >= 570000000 && Tuner->RF_IN < 620000000)
			{
				status += MXL_ControlWrite(Tuner, DAC_A_ENABLE, 0) ; // Bank4 On
				status += MXL_SetGPIO(Tuner, 4, 0) ; // Bank1 Off
				status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
				status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank3 Offq
			}
			if (Tuner->RF_IN >= 620000000 && Tuner->RF_IN < 760000000)
			{
				status += MXL_ControlWrite(Tuner, DAC_A_ENABLE, 1) ; // Bank4 On
				status += MXL_SetGPIO(Tuner, 4, 0) ; // Bank1 Off
				status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
				status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank3 Off
			}
			if (Tuner->RF_IN >= 760000000 && Tuner->RF_IN <= 900000000)
			{
				status += MXL_ControlWrite(Tuner, DAC_A_ENABLE, 1) ; // Bank4 On
				status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
				status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
				status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank3 Off
			}
		}
	}

	if (Tuner->TF_Type == MXL_TF_E) // Tracking Filter type E
	{
		status += MXL_ControlWrite(Tuner, DAC_DIN_B, 0) ;

		if (Tuner->RF_IN >= 43000000 && Tuner->RF_IN < 174000000)
		{

			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 0) ; // Bank1 On
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank3 Off
		}
		if (Tuner->RF_IN >= 174000000 && Tuner->RF_IN < 250000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 0) ; // Bank1 On
			status += MXL_SetGPIO(Tuner, 1, 0) ; // Bank2 On
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank3 Off
		}
		if (Tuner->RF_IN >= 250000000 && Tuner->RF_IN < 310000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 0) ; // Bank2 On
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank3 Off
		}
		if (Tuner->RF_IN >= 310000000 && Tuner->RF_IN < 360000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 0) ; // Bank2 On
			status += MXL_SetGPIO(Tuner, 3, 0) ; // Bank3 On
		}
		if (Tuner->RF_IN >= 360000000 && Tuner->RF_IN < 470000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 3, 0) ; // Bank3 On
		}
		if (Tuner->RF_IN >= 470000000 && Tuner->RF_IN < 640000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 1) ; // Bank4 On
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 3, 0) ; // Bank3 On
		}
		if (Tuner->RF_IN >= 640000000 && Tuner->RF_IN <= 900000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 1) ; // Bank4 On
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank3 Off
		}
	}

	if (Tuner->TF_Type == MXL_TF_F) // Tracking Filter type F
	{
		status += MXL_ControlWrite(Tuner, DAC_DIN_B, 0) ;

		if (Tuner->RF_IN >= 43000000 && Tuner->RF_IN < 160000000)
		{

			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 0) ; // Bank1 On
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank3 Off
		}
		if (Tuner->RF_IN >= 160000000 && Tuner->RF_IN < 210000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 0) ; // Bank1 On
			status += MXL_SetGPIO(Tuner, 1, 0) ; // Bank2 On
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank3 Off
		}
		if (Tuner->RF_IN >= 210000000 && Tuner->RF_IN < 300000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 0) ; // Bank2 On
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank3 Off
		}
		if (Tuner->RF_IN >= 300000000 && Tuner->RF_IN < 390000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 0) ; // Bank2 On
			status += MXL_SetGPIO(Tuner, 3, 0) ; // Bank3 On
		}
		if (Tuner->RF_IN >= 390000000 && Tuner->RF_IN < 515000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 3, 0) ; // Bank3 On
		}
		if (Tuner->RF_IN >= 515000000 && Tuner->RF_IN < 650000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 1) ; // Bank4 On
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 3, 0) ; // Bank3 On
		}
		if (Tuner->RF_IN >= 650000000 && Tuner->RF_IN <= 900000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 1) ; // Bank4 On
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank3 Off
		}
	}

	if (Tuner->TF_Type == MXL_TF_E_2) // Tracking Filter type E_2
	{
		status += MXL_ControlWrite(Tuner, DAC_DIN_B, 0) ;

		if (Tuner->RF_IN >= 43000000 && Tuner->RF_IN < 174000000)
		{

			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 0) ; // Bank1 On
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank3 Off
		}
		if (Tuner->RF_IN >= 174000000 && Tuner->RF_IN < 250000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 0) ; // Bank1 On
			status += MXL_SetGPIO(Tuner, 1, 0) ; // Bank2 On
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank3 Off
		}
		if (Tuner->RF_IN >= 250000000 && Tuner->RF_IN < 350000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 0) ; // Bank2 On
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank3 Off
		}
		if (Tuner->RF_IN >= 350000000 && Tuner->RF_IN < 400000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 0) ; // Bank2 On
			status += MXL_SetGPIO(Tuner, 3, 0) ; // Bank3 On
		}
		if (Tuner->RF_IN >= 400000000 && Tuner->RF_IN < 570000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 3, 0) ; // Bank3 On
		}
		if (Tuner->RF_IN >= 570000000 && Tuner->RF_IN < 770000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 1) ; // Bank4 On
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 3, 0) ; // Bank3 On
		}
		if (Tuner->RF_IN >= 770000000 && Tuner->RF_IN <= 900000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 1) ; // Bank4 On
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank3 Off
		}
	}

	if (Tuner->TF_Type == MXL_TF_G) // Tracking Filter type G add for v2.6.8
	{
		status += MXL_ControlWrite(Tuner, DAC_DIN_B, 0) ;

		if (Tuner->RF_IN >= 50000000 && Tuner->RF_IN < 190000000)
		{

			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 0) ; // Bank1 On
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank3 Off
		}
		if (Tuner->RF_IN >= 190000000 && Tuner->RF_IN < 280000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 0) ; // Bank1 On
			status += MXL_SetGPIO(Tuner, 1, 0) ; // Bank2 On
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank3 Off
		}
		if (Tuner->RF_IN >= 280000000 && Tuner->RF_IN < 350000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 0) ; // Bank2 On
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank3 Off
		}
		if (Tuner->RF_IN >= 350000000 && Tuner->RF_IN < 400000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 0) ; // Bank2 On
			status += MXL_SetGPIO(Tuner, 3, 0) ; // Bank3 On
		}
		if (Tuner->RF_IN >= 400000000 && Tuner->RF_IN < 470000000)		//modified for 2.6.11
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 1) ; // Bank4 On
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 On
			status += MXL_SetGPIO(Tuner, 1, 0) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank3 Off
		}
		if (Tuner->RF_IN >= 470000000 && Tuner->RF_IN < 640000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 3, 0) ; // Bank3 On
		}
		if (Tuner->RF_IN >= 640000000 && Tuner->RF_IN < 820000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 1) ; // Bank4 On
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 3, 0) ; // Bank3 On
		}
		if (Tuner->RF_IN >= 820000000 && Tuner->RF_IN <= 900000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 1) ; // Bank4 On
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank3 Off
		}
	}

	if (Tuner->TF_Type == MXL_TF_E_NA) // Tracking Filter type E-NA for Empia ONLY  change for 2.6.8
	{
		status += MXL_ControlWrite(Tuner, DAC_DIN_B, 0) ;

		if (Tuner->RF_IN >= 471000000 && (Tuner->RF_IN - 471000000)%6000000 != 0) //if UHF and terrestrial=> Turn off Tracking Filter
		{
			// Turn off all the banks
			status += MXL_SetGPIO(Tuner, 3, 1) ;
			status += MXL_SetGPIO(Tuner, 1, 1) ;
			status += MXL_SetGPIO(Tuner, 4, 1) ;
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 0) ;

			//2.6.12
			//Turn on RSSI
			status += MXL_ControlWrite(Tuner, SEQ_EXTSYNTHCALIF, 1) ;
			status += MXL_ControlWrite(Tuner, SEQ_EXTDCCAL, 1) ;
			status += MXL_ControlWrite(Tuner, AGC_EN_RSSI, 1) ;
			status += MXL_ControlWrite(Tuner, RFA_ENCLKRFAGC, 1) ;

			// RSSI reference point
			status += MXL_ControlWrite(Tuner, RFA_RSSI_REFH, 5) ;
			status += MXL_ControlWrite(Tuner, RFA_RSSI_REF, 3) ;
			status += MXL_ControlWrite(Tuner, RFA_RSSI_REFL, 2) ;


	    //status += MXL_ControlWrite(Tuner, AGC_IF, 10) ;		//doesn't matter since RSSI is turn on

			//following parameter is from analog OTA mode, can be change to seek better performance
			status += MXL_ControlWrite(Tuner, RFSYN_CHP_GAIN, 3) ;
		}

		else  //if VHF or Cable =>  Turn on Tracking Filter
		{
		//2.6.12
		//Turn off RSSI
		status += MXL_ControlWrite(Tuner, AGC_EN_RSSI, 0) ;

		//change back from above condition
		status += MXL_ControlWrite(Tuner, RFSYN_CHP_GAIN, 5) ;


		if (Tuner->RF_IN >= 43000000 && Tuner->RF_IN < 174000000)
		{

			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 0) ; // Bank1 On
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank3 Off
		}
		if (Tuner->RF_IN >= 174000000 && Tuner->RF_IN < 250000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 0) ; // Bank1 On
			status += MXL_SetGPIO(Tuner, 1, 0) ; // Bank2 On
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank3 Off
		}
		if (Tuner->RF_IN >= 250000000 && Tuner->RF_IN < 350000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 0) ; // Bank2 On
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank3 Off
		}
		if (Tuner->RF_IN >= 350000000 && Tuner->RF_IN < 400000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 0) ; // Bank2 On
			status += MXL_SetGPIO(Tuner, 3, 0) ; // Bank3 On
		}
		if (Tuner->RF_IN >= 400000000 && Tuner->RF_IN < 570000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 0) ; // Bank4 Off
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 3, 0) ; // Bank3 On
		}
		if (Tuner->RF_IN >= 570000000 && Tuner->RF_IN < 770000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 1) ; // Bank4 On
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 3, 0) ; // Bank3 On
		}
		if (Tuner->RF_IN >= 770000000 && Tuner->RF_IN <= 900000000)
		{
			status += MXL_ControlWrite(Tuner, DAC_B_ENABLE, 1) ; // Bank4 On
			status += MXL_SetGPIO(Tuner, 4, 1) ; // Bank1 Off
			status += MXL_SetGPIO(Tuner, 1, 1) ; // Bank2 Off
			status += MXL_SetGPIO(Tuner, 3, 1) ; // Bank3 Off
		}
		}
	}
	return status ;
}

_u16 MXL_SetGPIO(Tuner_struct *Tuner, _u8 GPIO_Num, _u8 GPIO_Val)
{
	_u16 status = 0 ;

	if (GPIO_Num == 1)
		status += MXL_ControlWrite(Tuner, GPIO_1B, GPIO_Val ? 0 : 1) ;
	// GPIO2 is not available
	if (GPIO_Num == 3)
	{
		if (GPIO_Val == 1) {
			status += MXL_ControlWrite(Tuner, GPIO_3, 0) ;
			status += MXL_ControlWrite(Tuner, GPIO_3B, 0) ;
		}
		if (GPIO_Val == 0) {
			status += MXL_ControlWrite(Tuner, GPIO_3, 1) ;
			status += MXL_ControlWrite(Tuner, GPIO_3B, 1) ;
		}
		if (GPIO_Val == 3) { // tri-state
			status += MXL_ControlWrite(Tuner, GPIO_3, 0) ;
			status += MXL_ControlWrite(Tuner, GPIO_3B, 1) ;
		}
	}
	if (GPIO_Num == 4)
	{
		if (GPIO_Val == 1) {
			status += MXL_ControlWrite(Tuner, GPIO_4, 0) ;
			status += MXL_ControlWrite(Tuner, GPIO_4B, 0) ;
		}
		if (GPIO_Val == 0) {
			status += MXL_ControlWrite(Tuner, GPIO_4, 1) ;
			status += MXL_ControlWrite(Tuner, GPIO_4B, 1) ;
		}
		if (GPIO_Val == 3) { // tri-state
			status += MXL_ControlWrite(Tuner, GPIO_4, 0) ;
			status += MXL_ControlWrite(Tuner, GPIO_4B, 1) ;
		}
	}

	return status ;
}

///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// Function:       MXL_ControlWrite                                          //
//                                                                           //
// Description:    Update control name value                                 //
//                                                                           //
// Globals:                                                                  //
//                 NONE                                                      //
//                                                                           //
// Functions used:                                                           //
//                 MXL_ControlWrite( Tuner, controlName, value, Group )      //
//                                                                           //
// Inputs:                                                                   //
//                 Tuner         : Tuner structure                           //
//                 ControlName   : Control name to be updated                //
//                 value         : Value to be written                       //
//                                                                           //
// Outputs:                                                                  //
//                 Tuner       : Tuner structure defined at higher level     //
//                                                                           //
// Return:                                                                   //
//                 0 : Successful write                                      //
//                 >0 : Value exceed maximum allowed for control number      //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////
_u16 MXL_ControlWrite(Tuner_struct *Tuner, _u16 ControlNum, _u32 value)
{
	_u16 status = 0 ;
	// Will write ALL Matching Control Name
	status += MXL_ControlWrite_Group( Tuner, ControlNum, value, 1 ) ;    // Write Matching INIT Control
	status += MXL_ControlWrite_Group( Tuner, ControlNum, value, 2 ) ;    // Write Matching CH Control
#ifdef _MXL_INTERNAL
	status += MXL_ControlWrite_Group( Tuner, ControlNum, value, 3 ) ;    // Write Matching MXL Control
#endif

	return status ;
}

///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// Function:       MXL_ControlWrite                                          //
//                                                                           //
// Description:    Update control name value                                 //
//                                                                           //
// Globals:                                                                  //
//                 NONE                                                      //
//                                                                           //
// Functions used:                                                           //
//                 strcmp                                                    //
//                                                                           //
// Inputs:                                                                   //
//                 Tuner_struct: structure defined at higher level           //
//                 ControlName      : Control Name                           //
//                 value            : Value Assigned to Control Name         //
//                 controlGroup     : Control Register Group                 //
//                                                                           //
// Outputs:                                                                  //
//                 NONE                                                      //
//                                                                           //
// Return:                                                                   //
//                 0 : Successful write                                      //
//                 1 : Value exceed maximum allowed for control name         //
//                 2 : Control name not found                                //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////
_u16 MXL_ControlWrite_Group(Tuner_struct *Tuner, _u16 controlNum, _u32 value, _u16 controlGroup)
{
	_u16 i, j, k ;
	_u32 highLimit ;
	_u32 ctrlVal ;

	if( controlGroup == 1) // Initial Control
	{
		for (i=0; i<Tuner->Init_Ctrl_Num ; i++)
		{
			if ( controlNum == Tuner->Init_Ctrl[i].Ctrl_Num )
			{   // find the control Name
				highLimit = 1 << Tuner->Init_Ctrl[i].size  ;
				if ( value < highLimit)
				{
					for( j=0; j<Tuner->Init_Ctrl[i].size; j++)
					{
						Tuner->Init_Ctrl[i].val[j] = (_u8)((value >> j) & 0x01) ;
						// change the register map accordingly
						MXL_RegWriteBit( Tuner, (_u8)(Tuner->Init_Ctrl[i].addr[j]),
							(_u8)(Tuner->Init_Ctrl[i].bit[j]),
							(_u8)((value>>j) & 0x01) ) ;
					}
					ctrlVal = 0 ;
					for(k=0; k<Tuner->Init_Ctrl[i].size; k++)
					{
						ctrlVal += Tuner->Init_Ctrl[i].val[k] * (1 << k) ;
					}
				}
				else
				{
					return -1 ;
				}
			}
		}
	}
	if ( controlGroup == 2) // Chan change Control
	{
		for (i=0; i<Tuner->CH_Ctrl_Num; i++)
		{
			if ( controlNum == Tuner->CH_Ctrl[i].Ctrl_Num )
			{   // find the control Name
				highLimit = 1 << Tuner->CH_Ctrl[i].size ;
				if ( value < highLimit)
				{
					for( j=0; j<Tuner->CH_Ctrl[i].size; j++)
					{
						Tuner->CH_Ctrl[i].val[j] = (_u8)((value >> j) & 0x01) ;
						// change the register map accordingly
						MXL_RegWriteBit( Tuner, (_u8)(Tuner->CH_Ctrl[i].addr[j]),
							(_u8)(Tuner->CH_Ctrl[i].bit[j]),
							(_u8)((value>>j) & 0x01) ) ;
					}
					ctrlVal = 0 ;
					for(k=0; k<Tuner->CH_Ctrl[i].size; k++)
					{
						ctrlVal += Tuner->CH_Ctrl[i].val[k] * (1 << k) ;
					}
				}
				else
				{
					return -1 ;
				}
			}
		}
	}
#ifdef _MXL_INTERNAL
	if ( controlGroup == 3) // Maxlinear Control
	{
		for (i=0; i<Tuner->MXL_Ctrl_Num; i++)
		{
			if ( controlNum == Tuner->MXL_Ctrl[i].Ctrl_Num )
			{   // find the control Name
				highLimit = (1 << Tuner->MXL_Ctrl[i].size) ;
				if ( value < highLimit)
				{
					for( j=0; j<Tuner->MXL_Ctrl[i].size; j++)
					{
						Tuner->MXL_Ctrl[i].val[j] = (_u8)((value >> j) & 0x01) ;
						// change the register map accordingly
						MXL_RegWriteBit( Tuner, (_u8)(Tuner->MXL_Ctrl[i].addr[j]),
							(_u8)(Tuner->MXL_Ctrl[i].bit[j]),
							(_u8)((value>>j) & 0x01) ) ;
					}
					ctrlVal = 0 ;
					for(k=0; k<Tuner->MXL_Ctrl[i].size; k++)
					{
						ctrlVal += Tuner->MXL_Ctrl[i].val[k] * (1 << k) ;
					}
				}
				else
				{
					return -1 ;
				}
			}
		}
	}
#endif
	return 0 ; // successful return
}

///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// Function:       MXL_RegWrite                                              //
//                                                                           //
// Description:    Update tuner register value                               //
//                                                                           //
// Globals:                                                                  //
//                 NONE                                                      //
//                                                                           //
// Functions used:                                                           //
//                 NONE                                                      //
//                                                                           //
// Inputs:                                                                   //
//                 Tuner_struct: structure defined at higher level           //
//                 RegNum    : Register address to be assigned a value       //
//                 RegVal    : Register value to write                       //
//                                                                           //
// Outputs:                                                                  //
//                 NONE                                                      //
//                                                                           //
// Return:                                                                   //
//                 0 : Successful write                                      //
//                 -1 : Invalid Register Address                             //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////
_u16 MXL_RegWrite(Tuner_struct *Tuner, _u8 RegNum, _u8 RegVal)
{
	int i ;

	for (i=0; i<104; i++)
	{
		if (RegNum == Tuner->TunerRegs[i].Reg_Num )
		{
			Tuner->TunerRegs[i].Reg_Val = RegVal ;
			return 0 ;
		}
	}

	return 1 ;
}

///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// Function:       MXL_RegRead                                               //
//                                                                           //
// Description:    Retrieve tuner register value                             //
//                                                                           //
// Globals:                                                                  //
//                 NONE                                                      //
//                                                                           //
// Functions used:                                                           //
//                 NONE                                                      //
//                                                                           //
// Inputs:                                                                   //
//                 Tuner_struct: structure defined at higher level           //
//                 RegNum    : Register address to be assigned a value       //
//                                                                           //
// Outputs:                                                                  //
//                 RegVal    : Retrieved register value                      //
//                                                                           //
// Return:                                                                   //
//                 0 : Successful read                                       //
//                 -1 : Invalid Register Address                             //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////
_u16 MXL_RegRead(Tuner_struct *Tuner, _u8 RegNum, _u8 *RegVal)
{
	int i ;

	for (i=0; i<104; i++)
	{
		if (RegNum == Tuner->TunerRegs[i].Reg_Num )
		{
			*RegVal = (_u8)(Tuner->TunerRegs[i].Reg_Val) ;
			return 0 ;
		}
	}

	return 1 ;
}

///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// Function:       MXL_ControlRead                                           //
//                                                                           //
// Description:    Retrieve the control value based on the control name      //
//                                                                           //
// Globals:                                                                  //
//                 NONE                                                      //
//                                                                           //
// Inputs:                                                                   //
//                 Tuner_struct  : structure defined at higher level         //
//                 ControlName   : Control Name                              //
//                                                                           //
// Outputs:                                                                  //
//                 value  : returned control value                           //
//                                                                           //
// Return:                                                                   //
//                 0 : Successful read                                       //
//                 -1 : Invalid control name                                 //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////
_u16 MXL_ControlRead(Tuner_struct *Tuner, _u16 controlNum, _u32 * value)
{
	_u32 ctrlVal ;
	_u16 i, k ;

	for (i=0; i<Tuner->Init_Ctrl_Num ; i++)
	{
		if ( controlNum == Tuner->Init_Ctrl[i].Ctrl_Num )
		{
			ctrlVal = 0 ;
			for(k=0; k<Tuner->Init_Ctrl[i].size; k++)
				ctrlVal += Tuner->Init_Ctrl[i].val[k] * (1 << k) ;
			*value = ctrlVal ;
			return 0 ;
		}
	}
	for (i=0; i<Tuner->CH_Ctrl_Num ; i++)
	{
		if ( controlNum == Tuner->CH_Ctrl[i].Ctrl_Num )
		{
			ctrlVal = 0 ;
			for(k=0; k<Tuner->CH_Ctrl[i].size; k++)
				ctrlVal += Tuner->CH_Ctrl[i].val[k] * (1 << k) ;
			*value = ctrlVal ;
			return 0 ;
		}
	}

#ifdef _MXL_INTERNAL
	for (i=0; i<Tuner->MXL_Ctrl_Num ; i++)
	{
		if ( controlNum == Tuner->MXL_Ctrl[i].Ctrl_Num )
		{
			ctrlVal = 0 ;
			for(k=0; k<Tuner->MXL_Ctrl[i].size; k++)
				ctrlVal += Tuner->MXL_Ctrl[i].val[k] * (1<<k) ;
			*value = ctrlVal ;
			return 0 ;
		}
	}
#endif
	return 1 ;
}

///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// Function:       MXL_ControlRegRead                                        //
//                                                                           //
// Description:    Retrieve the register addresses and count related to a    //
//				   a specific control name									 //
//                                                                           //
// Globals:                                                                  //
//                 NONE                                                      //
//                                                                           //
// Inputs:                                                                   //
//                 Tuner_struct  : structure defined at higher level         //
//                 ControlName   : Control Name                              //
//                                                                           //
// Outputs:                                                                  //
//                 RegNum  : returned register address array                 //
//				   count   : returned register count related to a control    //
//                                                                           //
// Return:                                                                   //
//                 0 : Successful read                                       //
//                 -1 : Invalid control name                                 //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////
_u16 MXL_ControlRegRead(Tuner_struct *Tuner, _u16 controlNum, _u8 *RegNum, int * count)
{
	_u16 i, j, k ;
	_u16 Count ;

	for (i=0; i<Tuner->Init_Ctrl_Num ; i++)
	{
		if ( controlNum == Tuner->Init_Ctrl[i].Ctrl_Num )
		{
			Count = 1 ;
			RegNum[0] = (_u8)(Tuner->Init_Ctrl[i].addr[0]) ;

			for(k=1; k<Tuner->Init_Ctrl[i].size; k++)
			{
				for (j= 0; j<Count; j++)
				{
					if (Tuner->Init_Ctrl[i].addr[k] != RegNum[j])
					{
						Count ++ ;
						RegNum[Count-1] = (_u8)(Tuner->Init_Ctrl[i].addr[k]) ;
					}
				}

			}
			*count = Count ;
			return 0 ;
		}
	}
	for (i=0; i<Tuner->CH_Ctrl_Num ; i++)
	{
		if ( controlNum == Tuner->CH_Ctrl[i].Ctrl_Num )
		{
			Count = 1 ;
			RegNum[0] = (_u8)(Tuner->CH_Ctrl[i].addr[0]) ;

			for(k=1; k<Tuner->CH_Ctrl[i].size; k++)
			{
				for (j= 0; j<Count; j++)
				{
					if (Tuner->CH_Ctrl[i].addr[k] != RegNum[j])
					{
						Count ++ ;
						RegNum[Count-1] = (_u8)(Tuner->CH_Ctrl[i].addr[k]) ;
					}
				}
			}
			*count = Count ;
			return 0 ;
		}
	}
#ifdef _MXL_INTERNAL
	for (i=0; i<Tuner->MXL_Ctrl_Num ; i++)
	{
		if ( controlNum == Tuner->MXL_Ctrl[i].Ctrl_Num )
		{
			Count = 1 ;
			RegNum[0] = (_u8)(Tuner->MXL_Ctrl[i].addr[0]) ;

			for(k=1; k<Tuner->MXL_Ctrl[i].size; k++)
			{
				for (j= 0; j<Count; j++)
				{
					if (Tuner->MXL_Ctrl[i].addr[k] != RegNum[j])
					{
						Count ++ ;
						RegNum[Count-1] = (_u8)Tuner->MXL_Ctrl[i].addr[k] ;
					}
				}
			}
			*count = Count ;
			return 0 ;
		}
	}
#endif
	*count = 0 ;
	return 1 ;
}

///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// Function:       MXL_RegWriteBit                                           //
//                                                                           //
// Description:    Write a register for specified register address,          //
//                 register bit and register bit value                       //
//                                                                           //
// Globals:                                                                  //
//                 NONE                                                      //
//                                                                           //
// Inputs:                                                                   //
//                 Tuner_struct  : structure defined at higher level         //
//                 address       : register address                          //
//				   bit			 : register bit number						 //
//				   bitVal		 : register bit value                        //
//                                                                           //
// Outputs:                                                                  //
//                 NONE                                                      //
//                                                                           //
// Return:                                                                   //
//                 NONE                                                      //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

void MXL_RegWriteBit(Tuner_struct *Tuner, _u8 address, _u8 bit, _u8 bitVal)
{
	int i ;

	// Declare Local Constants
	const _u8 AND_MAP[8] = {
		0xFE, 0xFD, 0xFB, 0xF7,
		0xEF, 0xDF, 0xBF, 0x7F } ;

	const _u8 OR_MAP[8] = {
		0x01, 0x02, 0x04, 0x08,
		0x10, 0x20, 0x40, 0x80 } ;

	for(i=0; i<Tuner->TunerRegs_Num; i++) {
		if ( Tuner->TunerRegs[i].Reg_Num == address ) {
			if (bitVal)
				Tuner->TunerRegs[i].Reg_Val |= OR_MAP[bit] ;
			else
				Tuner->TunerRegs[i].Reg_Val &= AND_MAP[bit] ;
			break ;
		}
	}
} ;


///////////////////////////////////////////////////////////////////////////////
//                                                                           //
// Function:       MXL_Ceiling                                               //
//                                                                           //
// Description:    Complete to closest increment of resolution               //
//                                                                           //
// Globals:                                                                  //
//                 NONE                                                      //
//                                                                           //
// Functions used:                                                           //
//                 NONE                                                      //
//                                                                           //
// Inputs:                                                                   //
//                 value       : Input number to compute                     //
//                 resolution  : Increment step                              //
//                                                                           //
// Outputs:                                                                  //
//                 NONE                                                      //
//                                                                           //
// Return:                                                                   //
//                Computed value                                             //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////
_u32 MXL_Ceiling( _u32 value, _u32 resolution )
{
	return (value/resolution + (value%resolution > 0 ? 1 : 0)) ;
};

//
// Retrieve the Initialzation Registers
//
_u16 MXL_GetInitRegister(Tuner_struct *Tuner, _u8 * RegNum, _u8 *RegVal, int *count)
{
	_u16 status = 0;
	int i ;

	_u8 RegAddr[] = {11, 12, 13, 22, 32, 43, 44, 53, 56, 59, 73,
							   76, 77, 91, 134, 135, 137, 147,
							   156, 166, 167, 168, 25 } ;
	*count = sizeof(RegAddr) / sizeof(_u8) ;

	status += MXL_BlockInit(Tuner) ;

	for (i=0 ; i< *count; i++)
	{
		RegNum[i] = RegAddr[i] ;
		status += MXL_RegRead(Tuner, RegNum[i], &RegVal[i]) ;
	}

	return status ;
}

_u16 MXL_GetCHRegister(Tuner_struct *Tuner, _u8 * RegNum, _u8 *RegVal, int *count)
{
	_u16 status = 0;
	int i ;

//add 77, 166, 167, 168 register for 2.6.12
#ifdef _MXL_PRODUCTION
	_u8 RegAddr[] = {14, 15, 16, 17, 22, 43, 65, 68, 69, 70, 73, 92, 93, 106,
							   107, 108, 109, 110, 111, 112, 136, 138, 149, 77, 166, 167, 168 } ;
#else
	_u8 RegAddr[] = {14, 15, 16, 17, 22, 43, 68, 69, 70, 73, 92, 93, 106,
							   107, 108, 109, 110, 111, 112, 136, 138, 149, 77, 166, 167, 168 } ;
	//_u8 RegAddr[171];
	//for (i=0; i<=170; i++)
	//	RegAddr[i] = i;
#endif

	*count = sizeof(RegAddr) / sizeof(_u8) ;

	for (i=0 ; i< *count; i++)
	{
		RegNum[i] = RegAddr[i] ;
		status += MXL_RegRead(Tuner, RegNum[i], &RegVal[i]) ;
	}

	return status ;

}

_u16 MXL_GetCHRegister_ZeroIF(Tuner_struct *Tuner, _u8 * RegNum, _u8 *RegVal, int *count)
{
	_u16 status = 0 ;
	int i ;

	_u8 RegAddr[] = {43, 136} ;

	*count = sizeof(RegAddr) / sizeof(_u8) ;

	for (i=0; i<*count; i++)
	{
		RegNum[i] = RegAddr[i] ;
		status += MXL_RegRead(Tuner, RegNum[i], &RegVal[i]) ;
	}
	return status ;

}

_u16 MXL_GetCHRegister_LowIF(Tuner_struct *Tuner, _u8 * RegNum, _u8 *RegVal, int *count)
{
	_u16 status = 0 ;
	int i ;

	_u8 RegAddr[] = {138} ;

	*count = sizeof(RegAddr) / sizeof(_u8) ;

	for (i=0; i<*count; i++)
	{
		RegNum[i] = RegAddr[i] ;
		status += MXL_RegRead(Tuner, RegNum[i], &RegVal[i]) ;
	}
	return status ;

}

_u16 MXL_GetMasterControl(_u8 *MasterReg, int state)
{
	if (state == 1) // Load_Start
		*MasterReg = 0xF3 ;
	if (state == 2) // Power_Down
		*MasterReg = 0x41 ;
	if (state == 3) // Synth_Reset
		*MasterReg = 0xB1 ;
	if (state == 4) // Seq_Off
		*MasterReg = 0xF1 ;

	return 0 ;
}

#ifdef _MXL_PRODUCTION
_u16 MXL_VCORange_Test(Tuner_struct *Tuner, int VCO_Range)
{
    _u16 status = 0 ;

   if (VCO_Range == 1) {
	 status += MXL_ControlWrite(Tuner, RFSYN_EN_DIV, 1) ;
	 status += MXL_ControlWrite(Tuner, RFSYN_EN_OUTMUX, 0 ) ;
	 status += MXL_ControlWrite(Tuner, RFSYN_SEL_DIVM, 0 ) ;
	 status += MXL_ControlWrite(Tuner, RFSYN_DIVM, 1 ) ;
	 status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_OUT, 1 ) ;
	 status += MXL_ControlWrite(Tuner, RFSYN_RF_DIV_BIAS, 1 ) ;
	 status += MXL_ControlWrite(Tuner, DN_SEL_FREQ, 0 ) ;
	 if (Tuner->Mode == 0 && Tuner->IF_Mode == 1) // Analog Low IF Mode
	 {
	     status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_HI, 1 ) ;
	     status += MXL_ControlWrite(Tuner, RFSYN_VCO_BIAS, 8 ) ;
	     status += MXL_ControlWrite(Tuner, CHCAL_INT_MOD_RF, 56 ) ;
	     status += MXL_ControlWrite(Tuner, CHCAL_FRAC_MOD_RF, 180224 ) ;
	 }
	 if (Tuner->Mode == 0 && Tuner->IF_Mode == 0) // Analog Zero IF Mode
	 {
	     status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_HI, 1 ) ;
	     status += MXL_ControlWrite(Tuner, RFSYN_VCO_BIAS, 8 ) ;
	     status += MXL_ControlWrite(Tuner, CHCAL_INT_MOD_RF, 56 ) ;
	     status += MXL_ControlWrite(Tuner, CHCAL_FRAC_MOD_RF, 222822 ) ;
	 }
	 if (Tuner->Mode == 1) // Digital Mode
	 {
	     status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_HI, 1 ) ;
	     status += MXL_ControlWrite(Tuner, RFSYN_VCO_BIAS, 8 ) ;
	     status += MXL_ControlWrite(Tuner, CHCAL_INT_MOD_RF, 56 ) ;
	     status += MXL_ControlWrite(Tuner, CHCAL_FRAC_MOD_RF, 229376 ) ;
	 }
    }

    if (VCO_Range == 2) {
	 status += MXL_ControlWrite(Tuner, RFSYN_EN_DIV, 1) ;
	 status += MXL_ControlWrite(Tuner, RFSYN_EN_OUTMUX, 0 ) ;
	 status += MXL_ControlWrite(Tuner, RFSYN_SEL_DIVM, 0 ) ;
	 status += MXL_ControlWrite(Tuner, RFSYN_DIVM, 1 ) ;
	 status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_OUT, 1 ) ;
	 status += MXL_ControlWrite(Tuner, RFSYN_RF_DIV_BIAS, 1 ) ;
	 status += MXL_ControlWrite(Tuner, DN_SEL_FREQ, 0 ) ;
	 status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_HI, 1 ) ;
	 status += MXL_ControlWrite(Tuner, RFSYN_VCO_BIAS, 40 ) ;
	 status += MXL_ControlWrite(Tuner, CHCAL_INT_MOD_RF, 41 ) ;
	 if (Tuner->Mode == 0 && Tuner->IF_Mode == 1) // Analog Low IF Mode
	 {
	     status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_HI, 1 ) ;
	     status += MXL_ControlWrite(Tuner, RFSYN_VCO_BIAS, 40 ) ;
	     status += MXL_ControlWrite(Tuner, CHCAL_INT_MOD_RF, 42 ) ;
	     status += MXL_ControlWrite(Tuner, CHCAL_FRAC_MOD_RF, 206438 ) ;
	 }
	 if (Tuner->Mode == 0 && Tuner->IF_Mode == 0) // Analog Zero IF Mode
	 {
	     status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_HI, 1 ) ;
	     status += MXL_ControlWrite(Tuner, RFSYN_VCO_BIAS, 40 ) ;
	     status += MXL_ControlWrite(Tuner, CHCAL_INT_MOD_RF, 42 ) ;
	     status += MXL_ControlWrite(Tuner, CHCAL_FRAC_MOD_RF, 206438 ) ;
	 }
	 if (Tuner->Mode == 1) // Digital Mode
	 {
	     status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_HI, 1 ) ;
	     status += MXL_ControlWrite(Tuner, RFSYN_VCO_BIAS, 40 ) ;
	     status += MXL_ControlWrite(Tuner, CHCAL_INT_MOD_RF, 41 ) ;
	     status += MXL_ControlWrite(Tuner, CHCAL_FRAC_MOD_RF, 16384 ) ;
	 }
    }

    if (VCO_Range == 3) {
	 status += MXL_ControlWrite(Tuner, RFSYN_EN_DIV, 1) ;
	 status += MXL_ControlWrite(Tuner, RFSYN_EN_OUTMUX, 0 ) ;
	 status += MXL_ControlWrite(Tuner, RFSYN_SEL_DIVM, 0 ) ;
	 status += MXL_ControlWrite(Tuner, RFSYN_DIVM, 1 ) ;
	 status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_OUT, 1 ) ;
	 status += MXL_ControlWrite(Tuner, RFSYN_RF_DIV_BIAS, 1 ) ;
	 status += MXL_ControlWrite(Tuner, DN_SEL_FREQ, 0 ) ;
	 status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_HI, 0 ) ;
	 status += MXL_ControlWrite(Tuner, RFSYN_VCO_BIAS, 8 ) ;
	 status += MXL_ControlWrite(Tuner, CHCAL_INT_MOD_RF, 42 ) ;
	 if (Tuner->Mode == 0 && Tuner->IF_Mode == 1) // Analog Low IF Mode
	 {
	     status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_HI, 0 ) ;
	     status += MXL_ControlWrite(Tuner, RFSYN_VCO_BIAS, 8 ) ;
	     status += MXL_ControlWrite(Tuner, CHCAL_INT_MOD_RF, 44 ) ;
	     status += MXL_ControlWrite(Tuner, CHCAL_FRAC_MOD_RF, 173670 ) ;
	 }
	 if (Tuner->Mode == 0 && Tuner->IF_Mode == 0) // Analog Zero IF Mode
	 {
	     status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_HI, 0 ) ;
	     status += MXL_ControlWrite(Tuner, RFSYN_VCO_BIAS, 8 ) ;
	     status += MXL_ControlWrite(Tuner, CHCAL_INT_MOD_RF, 44 ) ;
	     status += MXL_ControlWrite(Tuner, CHCAL_FRAC_MOD_RF, 173670 ) ;
	 }
	 if (Tuner->Mode == 1) // Digital Mode
	 {
	     status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_HI, 0 ) ;
	     status += MXL_ControlWrite(Tuner, RFSYN_VCO_BIAS, 8 ) ;
	     status += MXL_ControlWrite(Tuner, CHCAL_INT_MOD_RF, 42 ) ;
	     status += MXL_ControlWrite(Tuner, CHCAL_FRAC_MOD_RF, 245760 ) ;
	 }
    }

    if (VCO_Range == 4) {
	 status += MXL_ControlWrite(Tuner, RFSYN_EN_DIV, 1) ;
	 status += MXL_ControlWrite(Tuner, RFSYN_EN_OUTMUX, 0 ) ;
	 status += MXL_ControlWrite(Tuner, RFSYN_SEL_DIVM, 0 ) ;
	 status += MXL_ControlWrite(Tuner, RFSYN_DIVM, 1 ) ;
	 status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_OUT, 1 ) ;
	 status += MXL_ControlWrite(Tuner, RFSYN_RF_DIV_BIAS, 1 ) ;
	 status += MXL_ControlWrite(Tuner, DN_SEL_FREQ, 0 ) ;
	 status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_HI, 0 ) ;
	 status += MXL_ControlWrite(Tuner, RFSYN_VCO_BIAS, 40 ) ;
	 status += MXL_ControlWrite(Tuner, CHCAL_INT_MOD_RF, 27 ) ;
	 if (Tuner->Mode == 0 && Tuner->IF_Mode == 1) // Analog Low IF Mode
	 {
	     status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_HI, 0 ) ;
	     status += MXL_ControlWrite(Tuner, RFSYN_VCO_BIAS, 40 ) ;
	     status += MXL_ControlWrite(Tuner, CHCAL_INT_MOD_RF, 27 ) ;
	     status += MXL_ControlWrite(Tuner, CHCAL_FRAC_MOD_RF, 206438 ) ;
	 }
	 if (Tuner->Mode == 0 && Tuner->IF_Mode == 0) // Analog Zero IF Mode
	 {
	     status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_HI, 0 ) ;
	     status += MXL_ControlWrite(Tuner, RFSYN_VCO_BIAS, 40 ) ;
	     status += MXL_ControlWrite(Tuner, CHCAL_INT_MOD_RF, 27 ) ;
	     status += MXL_ControlWrite(Tuner, CHCAL_FRAC_MOD_RF, 206438 ) ;
	 }
	 if (Tuner->Mode == 1) // Digital Mode
	 {
	     status += MXL_ControlWrite(Tuner, RFSYN_SEL_VCO_HI, 0 ) ;
	     status += MXL_ControlWrite(Tuner, RFSYN_VCO_BIAS, 40 ) ;
	     status += MXL_ControlWrite(Tuner, CHCAL_INT_MOD_RF, 27 ) ;
	     status += MXL_ControlWrite(Tuner, CHCAL_FRAC_MOD_RF, 212992 ) ;
	 }
   }

    return status ;
}

_u16 MXL_Hystersis_Test(Tuner_struct *Tuner, int Hystersis)
{
	_u16 status = 0 ;

	if (Hystersis == 1)
		status += MXL_ControlWrite(Tuner, DN_BYPASS_AGC_I2C, 1) ;

	return status ;
}
#endif















