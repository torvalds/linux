/**

@file

@brief   MxL5007T tuner module definition

One can manipulate MxL5007T tuner through MxL5007T module.
MxL5007T module is derived from tuner module.

*/


#include "tuner_mxl5007t.h"





/**

@brief   MxL5007T tuner module builder

Use BuildMxl5007tModule() to build MxL5007T module, set all module function pointers with the corresponding functions,
and initialize module private variables.


@param [in]   ppTuner                      Pointer to MxL5007T tuner module pointer
@param [in]   pTunerModuleMemory           Pointer to an allocated tuner module memory
@param [in]   pBaseInterfaceModuleMemory   Pointer to an allocated base interface module memory
@param [in]   pI2cBridgeModuleMemory       Pointer to an allocated I2C bridge module memory
@param [in]   DeviceAddr                   MxL5007T I2C device address
@param [in]   CrystalFreqHz                MxL5007T crystal frequency in Hz
@param [in]   StandardMode                 MxL5007T standard mode
@param [in]   IfFreqHz                     MxL5007T IF frequency in Hz
@param [in]   LoopThroughMode              MxL5007T loop-through mode
@param [in]   ClkOutMode                   MxL5007T clock output mode
@param [in]   ClkOutAmpMode                MxL5007T clock output amplitude mode
@param [in]   QamIfDiffOutLevel            MxL5007T QAM IF differential output level for QAM standard only


@note
	-# One should call BuildMxl5007tModule() to build MxL5007T module before using it.

*/
void
BuildMxl5007tModule(
	TUNER_MODULE **ppTuner,
	TUNER_MODULE *pTunerModuleMemory,
	BASE_INTERFACE_MODULE *pBaseInterfaceModuleMemory,
	I2C_BRIDGE_MODULE *pI2cBridgeModuleMemory,
	unsigned char DeviceAddr,
	unsigned long CrystalFreqHz,
	int StandardMode,
	unsigned long IfFreqHz,
	int SpectrumMode,
	int LoopThroughMode,
	int ClkOutMode,
	int ClkOutAmpMode,
	long QamIfDiffOutLevel
	)
{
	TUNER_MODULE *pTuner;
	MXL5007T_EXTRA_MODULE *pExtra;
	MxL5007_TunerConfigS *pTunerConfigS;



	// Set tuner module pointer.
	*ppTuner = pTunerModuleMemory;

	// Get tuner module.
	pTuner = *ppTuner;

	// Set base interface module pointer and I2C bridge module pointer.
	pTuner->pBaseInterface = pBaseInterfaceModuleMemory;
	pTuner->pI2cBridge = pI2cBridgeModuleMemory;

	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Mxl5007t);

	// Set and get MaxLinear-defined MxL5007T structure pointer.
	pExtra->pTunerConfigS = &(pExtra->TunerConfigSMemory);
	pTunerConfigS = pExtra->pTunerConfigS;

	// Set additional definition tuner module pointer.
	pTunerConfigS->pTuner = pTuner;



	// Set tuner type.
	pTuner->TunerType = TUNER_TYPE_MXL5007T;

	// Set tuner I2C device address.
	pTuner->DeviceAddr = DeviceAddr;


	// Initialize tuner parameter setting status.
	pTuner->IsRfFreqHzSet = NO;


	// Set tuner module manipulating function pointers.
	pTuner->GetTunerType  = mxl5007t_GetTunerType;
	pTuner->GetDeviceAddr = mxl5007t_GetDeviceAddr;

	pTuner->Initialize    = mxl5007t_Initialize;
	pTuner->SetRfFreqHz   = mxl5007t_SetRfFreqHz;
	pTuner->GetRfFreqHz   = mxl5007t_GetRfFreqHz;


	// Initialize tuner extra module variables.
	pExtra->LoopThroughMode    = LoopThroughMode;
	pExtra->IsBandwidthModeSet = NO;


	// Initialize MaxLinear-defined MxL5007T structure variables.
	// Note: The API doesn't use I2C device address of MaxLinear-defined MxL5007T structure.
	switch(StandardMode)
	{
		default:
		case MXL5007T_STANDARD_DVBT:	pTunerConfigS->Mode = MxL_MODE_DVBT;		break;
		case MXL5007T_STANDARD_ATSC:	pTunerConfigS->Mode = MxL_MODE_ATSC;		break;
		case MXL5007T_STANDARD_QAM:		pTunerConfigS->Mode = MxL_MODE_CABLE;		break;
		case MXL5007T_STANDARD_ISDBT:	pTunerConfigS->Mode = MxL_MODE_ISDBT;		break;
	}

	pTunerConfigS->IF_Diff_Out_Level = (SINT32)QamIfDiffOutLevel;

	switch(CrystalFreqHz)
	{
		default:
		case CRYSTAL_FREQ_16000000HZ:	pTunerConfigS->Xtal_Freq = MxL_XTAL_16_MHZ;			break;
		case CRYSTAL_FREQ_24000000HZ:	pTunerConfigS->Xtal_Freq = MxL_XTAL_24_MHZ;			break;
		case CRYSTAL_FREQ_25000000HZ:	pTunerConfigS->Xtal_Freq = MxL_XTAL_25_MHZ;			break;
		case CRYSTAL_FREQ_27000000HZ:	pTunerConfigS->Xtal_Freq = MxL_XTAL_27_MHZ;			break;
		case CRYSTAL_FREQ_28800000HZ:	pTunerConfigS->Xtal_Freq = MxL_XTAL_28_8_MHZ;		break;
	}

	switch(IfFreqHz)
	{
		default:
		case IF_FREQ_4570000HZ:		pTunerConfigS->IF_Freq = MxL_IF_4_57_MHZ;		break;
		case IF_FREQ_36150000HZ:	pTunerConfigS->IF_Freq = MxL_IF_36_15_MHZ;		break;
		case IF_FREQ_44000000HZ:	pTunerConfigS->IF_Freq = MxL_IF_44_MHZ;			break;
	}

	switch(SpectrumMode)
	{
		default:
		case SPECTRUM_NORMAL:		pTunerConfigS->IF_Spectrum = MxL_NORMAL_IF;			break;
		case SPECTRUM_INVERSE:		pTunerConfigS->IF_Spectrum = MxL_INVERT_IF;			break;
	}

	switch(ClkOutMode)
	{
		default:
		case MXL5007T_CLK_OUT_DISABLE:		pTunerConfigS->ClkOut_Setting = MxL_CLKOUT_DISABLE;			break;
		case MXL5007T_CLK_OUT_ENABLE:		pTunerConfigS->ClkOut_Setting = MxL_CLKOUT_ENABLE;			break;
	}

	switch(ClkOutAmpMode)
	{
		default:
		case MXL5007T_CLK_OUT_AMP_0:		pTunerConfigS->ClkOut_Amp = MxL_CLKOUT_AMP_0;		break;
		case MXL5007T_CLK_OUT_AMP_1:		pTunerConfigS->ClkOut_Amp = MxL_CLKOUT_AMP_1;		break;
		case MXL5007T_CLK_OUT_AMP_2:		pTunerConfigS->ClkOut_Amp = MxL_CLKOUT_AMP_2;		break;
		case MXL5007T_CLK_OUT_AMP_3:		pTunerConfigS->ClkOut_Amp = MxL_CLKOUT_AMP_3;		break;
		case MXL5007T_CLK_OUT_AMP_4:		pTunerConfigS->ClkOut_Amp = MxL_CLKOUT_AMP_4;		break;
		case MXL5007T_CLK_OUT_AMP_5:		pTunerConfigS->ClkOut_Amp = MxL_CLKOUT_AMP_5;		break;
		case MXL5007T_CLK_OUT_AMP_6:		pTunerConfigS->ClkOut_Amp = MxL_CLKOUT_AMP_6;		break;
		case MXL5007T_CLK_OUT_AMP_7:		pTunerConfigS->ClkOut_Amp = MxL_CLKOUT_AMP_7;		break;
	}

	pTunerConfigS->BW_MHz = MXL5007T_BANDWIDTH_MODE_DEFAULT;
	pTunerConfigS->RF_Freq_Hz = MXL5007T_RF_FREQ_HZ_DEFAULT;


	// Set tuner extra module function pointers.
	pExtra->SetBandwidthMode = mxl5007t_SetBandwidthMode;
	pExtra->GetBandwidthMode = mxl5007t_GetBandwidthMode;


	return;
}





/**

@see   TUNER_FP_GET_TUNER_TYPE

*/
void
mxl5007t_GetTunerType(
	TUNER_MODULE *pTuner,
	int *pTunerType
	)
{
	// Get tuner type from tuner module.
	*pTunerType = pTuner->TunerType;


	return;
}





/**

@see   TUNER_FP_GET_DEVICE_ADDR

*/
void
mxl5007t_GetDeviceAddr(
	TUNER_MODULE *pTuner,
	unsigned char *pDeviceAddr
	)
{
	// Get tuner I2C device address from tuner module.
	*pDeviceAddr = pTuner->DeviceAddr;


	return;
}





/**

@see   TUNER_FP_INITIALIZE

*/
int
mxl5007t_Initialize(
	TUNER_MODULE *pTuner
	)
{
	MXL5007T_EXTRA_MODULE *pExtra;
	MxL5007_TunerConfigS *pTunerConfigS;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Mxl5007t);

	// Get MaxLinear-defined MxL5007T structure.
	pTunerConfigS = pExtra->pTunerConfigS;


	// Initialize tuner.
	if(MxL_Tuner_Init(pTunerConfigS) != MxL_OK)
		goto error_status_initialize_tuner;

	// Set tuner loop-through mode.
	if(MxL_Loop_Through_On(pTunerConfigS, pExtra->LoopThroughMode) != MxL_OK)
		goto error_status_initialize_tuner;


	return FUNCTION_SUCCESS;


error_status_initialize_tuner:
	return FUNCTION_ERROR;
}





/**

@see   TUNER_FP_SET_RF_FREQ_HZ

*/
int
mxl5007t_SetRfFreqHz(
	TUNER_MODULE *pTuner,
	unsigned long RfFreqHz
	)
{
	MXL5007T_EXTRA_MODULE *pExtra;
	MxL5007_TunerConfigS *pTunerConfigS;

	UINT32 Mxl5007tRfFreqHz;
	MxL5007_BW_MHz Mxl5007tBandwidthMhz;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Mxl5007t);

	// Get MaxLinear-defined MxL5007T structure.
	pTunerConfigS = pExtra->pTunerConfigS;


	// Get bandwidth.
	Mxl5007tBandwidthMhz = pTunerConfigS->BW_MHz;

	// Set RF frequency.
	Mxl5007tRfFreqHz = (UINT32)RfFreqHz;

	// Set MxL5007T RF frequency and bandwidth.
	if(MxL_Tuner_RFTune(pTunerConfigS, Mxl5007tRfFreqHz, Mxl5007tBandwidthMhz) != MxL_OK)
		goto error_status_set_tuner_rf_frequency;


	// Set tuner RF frequency parameter.
	pTuner->RfFreqHz      = RfFreqHz;
	pTuner->IsRfFreqHzSet = YES;


	return FUNCTION_SUCCESS;


error_status_set_tuner_rf_frequency:
	return FUNCTION_ERROR;
}





/**

@see   TUNER_FP_GET_RF_FREQ_HZ

*/
int
mxl5007t_GetRfFreqHz(
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

@brief   Set MxL5007T tuner bandwidth mode.

*/
int
mxl5007t_SetBandwidthMode(
	TUNER_MODULE *pTuner,
	int BandwidthMode
	)
{
	MXL5007T_EXTRA_MODULE *pExtra;
	MxL5007_TunerConfigS *pTunerConfigS;

	UINT32 Mxl5007tRfFreqHz;
	MxL5007_BW_MHz Mxl5007tBandwidthMhz;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Mxl5007t);

	// Get MaxLinear-defined MxL5007T structure.
	pTunerConfigS = pExtra->pTunerConfigS;


	// Get RF frequency.
	Mxl5007tRfFreqHz = pTunerConfigS->RF_Freq_Hz;

	// Set bandwidth.
	switch(BandwidthMode)
	{
		case MXL5007T_BANDWIDTH_6000000HZ:		Mxl5007tBandwidthMhz = MxL_BW_6MHz;		break;
		case MXL5007T_BANDWIDTH_7000000HZ:		Mxl5007tBandwidthMhz = MxL_BW_7MHz;		break;
		case MXL5007T_BANDWIDTH_8000000HZ:		Mxl5007tBandwidthMhz = MxL_BW_8MHz;		break;

		default:	goto error_status_get_undefined_value;
	}

	// Set MxL5007T RF frequency and bandwidth.
	if(MxL_Tuner_RFTune(pTunerConfigS, Mxl5007tRfFreqHz, Mxl5007tBandwidthMhz) != MxL_OK)
		goto error_status_set_tuner_bandwidth;


	// Set tuner bandwidth parameter.
	pExtra->BandwidthMode      = BandwidthMode;
	pExtra->IsBandwidthModeSet = YES;


	return FUNCTION_SUCCESS;


error_status_set_tuner_bandwidth:
error_status_get_undefined_value:
	return FUNCTION_ERROR;
}





/**

@brief   Get MxL5007T tuner bandwidth mode.

*/
int
mxl5007t_GetBandwidthMode(
	TUNER_MODULE *pTuner,
	int *pBandwidthMode
	)
{
	MXL5007T_EXTRA_MODULE *pExtra;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Mxl5007t);


	// Get tuner bandwidth mode from tuner module.
	if(pExtra->IsBandwidthModeSet != YES)
		goto error_status_get_tuner_bandwidth_mode;

	*pBandwidthMode = pExtra->BandwidthMode;


	return FUNCTION_SUCCESS;


error_status_get_tuner_bandwidth_mode:
	return FUNCTION_ERROR;
}























// The following context is source code provided by MaxLinear.





// MaxLinear source code - MxL_User_Define.c


/*
 
 Driver APIs for MxL5007 Tuner
 
 Copyright, Maxlinear, Inc.
 All Rights Reserved
 
 File Name:      MxL_User_Define.c

 */

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
//																		   //
//					I2C Functions (implement by customer)				   //
//																		   //
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//

/******************************************************************************
**
**  Name: MxL_I2C_Write
**
**  Description:    I2C write operations
**
**  Parameters:    	
**					DeviceAddr	- MxL5007 Device address
**					pArray		- Write data array pointer
**					count		- total number of array
**
**  Returns:        0 if success
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   12-16-2007   khuang initial release.
**
******************************************************************************/
//UINT32 MxL_I2C_Write(UINT8 DeviceAddr, UINT8* pArray, UINT32 count)
UINT32 MxL_I2C_Write(MxL5007_TunerConfigS* myTuner, UINT8* pArray, UINT32 count)
{
	TUNER_MODULE *pTuner;
	BASE_INTERFACE_MODULE *pBaseInterface;
	I2C_BRIDGE_MODULE *pI2cBridge;
	unsigned char DeviceAddr;
	unsigned long WritingByteNumMax;

	unsigned long i;
	unsigned char Buffer[I2C_BUFFER_LEN];
	unsigned long WritingIndex;

	unsigned char *pData;
	unsigned long DataLen;



	// Get tuner module, base interface, and I2C bridge.
	pTuner = myTuner->pTuner;
	pBaseInterface = pTuner->pBaseInterface;
	pI2cBridge = pTuner->pI2cBridge;

	// Get tuner device address.
	pTuner->GetDeviceAddr(pTuner, &DeviceAddr);

	// Get writing byte and byte number.
	pData = (unsigned char *)pArray;
	DataLen = (unsigned long)count;

	// Calculate MxL5007T maximum writing byte number.
	// Note: MxL5007T maximum writing byte number must be a multiple of 2.
	WritingByteNumMax = pBaseInterface->I2cWritingByteNumMax;
	WritingByteNumMax = ((WritingByteNumMax % 2) == 0) ? WritingByteNumMax : (WritingByteNumMax - 1);


	// Set register bytes.
	// Note: The 2 kind of I2C formats of MxL5007T is described as follows:
	//       1. start_bit + (device_addr | writing_bit) + (register_addr + writing_byte) * n + stop_bit
	//          ...
	//          start_bit + (device_addr | writing_bit) + (register_addr + writing_byte) * m + stop_bit
	//       2. start_bit + (device_addr | writing_bit) + 0xff + stop_bit
	for(i = 0, WritingIndex = 0; i < DataLen; i++, WritingIndex++)
	{
		// Put data into buffer.
		Buffer[WritingIndex] = pData[i];

		// If writing buffer is full or put data into buffer completely, send the I2C writing command with buffer.
		if( (WritingIndex == (WritingByteNumMax - 1)) || (i == (DataLen - 1)) )
		{
			if(pI2cBridge->ForwardI2cWritingCmd(pI2cBridge, DeviceAddr, Buffer, (WritingIndex + LEN_1_BYTE)) != FUNCTION_SUCCESS)
				goto error_status_set_tuner_registers;

			WritingIndex = -1;
		}
	}


	return MxL_OK;


error_status_set_tuner_registers:
	return MxL_ERR_OTHERS;
}

/******************************************************************************
**
**  Name: MxL_I2C_Read
**
**  Description:    I2C read operations
**
**  Parameters:    	
**					DeviceAddr	- MxL5007 Device address
**					Addr		- register address for read
**					*Data		- data return
**
**  Returns:        0 if success
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   12-16-2007   khuang initial release.
**
******************************************************************************/
//UINT32 MxL_I2C_Read(UINT8 DeviceAddr, UINT8 Addr, UINT8* mData)
UINT32 MxL_I2C_Read(MxL5007_TunerConfigS* myTuner, UINT8 Addr, UINT8* mData)
{
	TUNER_MODULE *pTuner;
	I2C_BRIDGE_MODULE *pI2cBridge;
	unsigned char DeviceAddr;

	unsigned char Buffer[LEN_2_BYTE];



	// Get tuner module and I2C bridge.
	pTuner = myTuner->pTuner;
	pI2cBridge = pTuner->pI2cBridge;

	// Get tuner device address.
	pTuner->GetDeviceAddr(pTuner, &DeviceAddr);

	// Set tuner register reading address.
	// Note: The I2C format of tuner register reading address setting is as follows:
	//       start_bit + (DeviceAddr | writing_bit) + 0xfb + RegReadingAddr + stop_bit
	Buffer[0] = (unsigned char)MXL5007T_I2C_READING_CONST;
	Buffer[1] = (unsigned char)Addr;
	if(pI2cBridge->ForwardI2cWritingCmd(pI2cBridge, DeviceAddr, Buffer, LEN_2_BYTE) != FUNCTION_SUCCESS)
		goto error_status_set_tuner_register_reading_address;

	// Get tuner register bytes.
	// Note: The I2C format of tuner register byte getting is as follows:
	//       start_bit + (DeviceAddr | reading_bit) + reading_byte + stop_bit
	if(pI2cBridge->ForwardI2cReadingCmd(pI2cBridge, DeviceAddr, Buffer, LEN_1_BYTE) != FUNCTION_SUCCESS)
		goto error_status_get_tuner_registers;

	*mData = (UINT8)Buffer[0];


	return MxL_OK;


error_status_get_tuner_registers:
error_status_set_tuner_register_reading_address:
	return MxL_ERR_OTHERS;
}

/******************************************************************************
**
**  Name: MxL_Delay
**
**  Description:    Delay function in milli-second
**
**  Parameters:    	
**					mSec		- milli-second to delay
**
**  Returns:        0
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   12-16-2007   khuang initial release.
**
******************************************************************************/
//void MxL_Delay(UINT32 mSec)
void MxL_Delay(MxL5007_TunerConfigS* myTuner, UINT32 mSec)
{
	TUNER_MODULE *pTuner;
	BASE_INTERFACE_MODULE *pBaseInterface;



	// Get tuner module and base interface.
	pTuner = myTuner->pTuner;
	pBaseInterface = pTuner->pBaseInterface;

	// Wait in ms.
	pBaseInterface->WaitMs(pBaseInterface, mSec);


	return;
}























// MaxLinear source code - MxL5007.c


/*
 MxL5007 Source Code : V4.1.3
 
 Copyright, Maxlinear, Inc.
 All Rights Reserved
 
 File Name:      MxL5007.c

 Description: The source code is for MxL5007 user to quickly integrate MxL5007 into their software.
	There are two functions the user can call to generate a array of I2C command that's require to
	program the MxL5007 tuner. The user should pass an array pointer and an integer pointer in to the 
	function. The funciton will fill up the array with format like follow:
	
		addr1
		data1
		addr2
		data2
		...
	
	The user can then pass this array to their I2C function to perform progromming the tuner. 
*/
//#include "StdAfx.h"
//#include "MxL5007_Common.h"
//#include "MxL5007.h"



UINT32 MxL5007_Init(UINT8* pArray,				// a array pointer that store the addr and data pairs for I2C write
					UINT32* Array_Size,			// a integer pointer that store the number of element in above array
					UINT8 Mode,				
					SINT32 IF_Diff_Out_Level,		 
					UINT32 Xtal_Freq_Hz,			
					UINT32 IF_Freq_Hz,				
					UINT8 Invert_IF,											
					UINT8 Clk_Out_Enable,    
					UINT8 Clk_Out_Amp													
					)
{
	
	UINT32 Reg_Index=0;
	UINT32 Array_Index=0;

	IRVType IRV_Init[]=
	{
		//{ Addr, Data}	
		{ 0x02, 0x06}, 
		{ 0x03, 0x48},
		{ 0x05, 0x04},  
		{ 0x06, 0x10}, 
		{ 0x2E, 0x15},  //Override
		{ 0x30, 0x10},  //Override
		{ 0x45, 0x58},  //Override
		{ 0x48, 0x19},  //Override
		{ 0x52, 0x03},  //Override
		{ 0x53, 0x44},  //Override
		{ 0x6A, 0x4B},  //Override
		{ 0x76, 0x00},  //Override
		{ 0x78, 0x18},  //Override
		{ 0x7A, 0x17},  //Override
		{ 0x85, 0x06},  //Override		
		{ 0x01, 0x01}, //TOP_MASTER_ENABLE=1
		{ 0, 0}
	};


	IRVType IRV_Init_Cable[]=
	{
		//{ Addr, Data}	
		{ 0x02, 0x06}, 
		{ 0x03, 0x48},
		{ 0x05, 0x04},  
		{ 0x06, 0x10},  
		{ 0x09, 0x3F},  
		{ 0x0A, 0x3F},  
		{ 0x0B, 0x3F},  
		{ 0x2E, 0x15},  //Override
		{ 0x30, 0x10},  //Override
		{ 0x45, 0x58},  //Override
		{ 0x48, 0x19},  //Override
		{ 0x52, 0x03},  //Override
		{ 0x53, 0x44},  //Override
		{ 0x6A, 0x4B},  //Override
		{ 0x76, 0x00},  //Override
		{ 0x78, 0x18},  //Override
		{ 0x7A, 0x17},  //Override
		{ 0x85, 0x06},  //Override	
		{ 0x01, 0x01}, //TOP_MASTER_ENABLE=1
		{ 0, 0}
	};
	//edit Init setting here

	PIRVType myIRV=IRV_Init;

	switch(Mode)
	{
	case MxL_MODE_ISDBT: //ISDB-T Mode	
		myIRV = IRV_Init;
		SetIRVBit(myIRV, 0x06, 0x1F, 0x10);  
		break;
	case MxL_MODE_DVBT: //DVBT Mode			
		myIRV = IRV_Init;
		SetIRVBit(myIRV, 0x06, 0x1F, 0x11);  
		break;
	case MxL_MODE_ATSC: //ATSC Mode			
		myIRV = IRV_Init;
		SetIRVBit(myIRV, 0x06, 0x1F, 0x12);  
		break;	
	case MxL_MODE_CABLE:						
		myIRV = IRV_Init_Cable;
		SetIRVBit(myIRV, 0x09, 0xFF, 0xC1);	
		SetIRVBit(myIRV, 0x0A, 0xFF, 8-IF_Diff_Out_Level);	
		SetIRVBit(myIRV, 0x0B, 0xFF, 0x17);							
		break;
	

	}

	switch(IF_Freq_Hz)
	{
	case MxL_IF_4_MHZ:
		SetIRVBit(myIRV, 0x02, 0x0F, 0x00); 		
		break;
	case MxL_IF_4_5_MHZ: //4.5MHz
		SetIRVBit(myIRV, 0x02, 0x0F, 0x02); 		
		break;
	case MxL_IF_4_57_MHZ: //4.57MHz
		SetIRVBit(myIRV, 0x02, 0x0F, 0x03); 
		break;
	case MxL_IF_5_MHZ:
		SetIRVBit(myIRV, 0x02, 0x0F, 0x04); 
		break;
	case MxL_IF_5_38_MHZ: //5.38MHz
		SetIRVBit(myIRV, 0x02, 0x0F, 0x05); 
		break;
	case MxL_IF_6_MHZ: 
		SetIRVBit(myIRV, 0x02, 0x0F, 0x06); 
		break;
	case MxL_IF_6_28_MHZ: //6.28MHz
		SetIRVBit(myIRV, 0x02, 0x0F, 0x07); 
		break;
	case MxL_IF_9_1915_MHZ: //9.1915MHz
		SetIRVBit(myIRV, 0x02, 0x0F, 0x08); 
		break;
	case MxL_IF_35_25_MHZ:
		SetIRVBit(myIRV, 0x02, 0x0F, 0x09); 
		break;
	case MxL_IF_36_15_MHZ:
		SetIRVBit(myIRV, 0x02, 0x0F, 0x0a); 
		break;
	case MxL_IF_44_MHZ:
		SetIRVBit(myIRV, 0x02, 0x0F, 0x0B); 
		break;
	}

	if(Invert_IF) 
		SetIRVBit(myIRV, 0x02, 0x10, 0x10);   //Invert IF
	else
		SetIRVBit(myIRV, 0x02, 0x10, 0x00);   //Normal IF


	switch(Xtal_Freq_Hz)
	{
	case MxL_XTAL_16_MHZ:
		SetIRVBit(myIRV, 0x03, 0xF0, 0x00);  //select xtal freq & Ref Freq
		SetIRVBit(myIRV, 0x05, 0x0F, 0x00);
		break;
	case MxL_XTAL_20_MHZ:
		SetIRVBit(myIRV, 0x03, 0xF0, 0x10);
		SetIRVBit(myIRV, 0x05, 0x0F, 0x01);
		break;
	case MxL_XTAL_20_25_MHZ:
		SetIRVBit(myIRV, 0x03, 0xF0, 0x20);
		SetIRVBit(myIRV, 0x05, 0x0F, 0x02);
		break;
	case MxL_XTAL_20_48_MHZ:
		SetIRVBit(myIRV, 0x03, 0xF0, 0x30);
		SetIRVBit(myIRV, 0x05, 0x0F, 0x03);
		break;
	case MxL_XTAL_24_MHZ:
		SetIRVBit(myIRV, 0x03, 0xF0, 0x40);
		SetIRVBit(myIRV, 0x05, 0x0F, 0x04);
		break;
	case MxL_XTAL_25_MHZ:
		SetIRVBit(myIRV, 0x03, 0xF0, 0x50);
		SetIRVBit(myIRV, 0x05, 0x0F, 0x05);
		break;
	case MxL_XTAL_25_14_MHZ:
		SetIRVBit(myIRV, 0x03, 0xF0, 0x60);
		SetIRVBit(myIRV, 0x05, 0x0F, 0x06);
		break;
	case MxL_XTAL_27_MHZ:
		SetIRVBit(myIRV, 0x03, 0xF0, 0x70);
		SetIRVBit(myIRV, 0x05, 0x0F, 0x07);
		break;
	case MxL_XTAL_28_8_MHZ: 
		SetIRVBit(myIRV, 0x03, 0xF0, 0x80);
		SetIRVBit(myIRV, 0x05, 0x0F, 0x08);
		break;
	case MxL_XTAL_32_MHZ:
		SetIRVBit(myIRV, 0x03, 0xF0, 0x90);
		SetIRVBit(myIRV, 0x05, 0x0F, 0x09);
		break;
	case MxL_XTAL_40_MHZ:
		SetIRVBit(myIRV, 0x03, 0xF0, 0xA0);
		SetIRVBit(myIRV, 0x05, 0x0F, 0x0A);
		break;
	case MxL_XTAL_44_MHZ:
		SetIRVBit(myIRV, 0x03, 0xF0, 0xB0);
		SetIRVBit(myIRV, 0x05, 0x0F, 0x0B);
		break;
	case MxL_XTAL_48_MHZ:
		SetIRVBit(myIRV, 0x03, 0xF0, 0xC0);
		SetIRVBit(myIRV, 0x05, 0x0F, 0x0C);
		break;
	case MxL_XTAL_49_3811_MHZ:
		SetIRVBit(myIRV, 0x03, 0xF0, 0xD0);
		SetIRVBit(myIRV, 0x05, 0x0F, 0x0D);
		break;	
	}

	if(!Clk_Out_Enable) //default is enable 
		SetIRVBit(myIRV, 0x03, 0x08, 0x00);   

	//Clk_Out_Amp
	SetIRVBit(myIRV, 0x03, 0x07, Clk_Out_Amp);   

	//Generate one Array that Contain Data, Address
	while (myIRV[Reg_Index].Num || myIRV[Reg_Index].Val)
	{
		pArray[Array_Index++] = myIRV[Reg_Index].Num;
		pArray[Array_Index++] = myIRV[Reg_Index].Val;
		Reg_Index++;
	}
	    
	*Array_Size=Array_Index;
	return 0;
}


UINT32 MxL5007_RFTune(UINT8* pArray, UINT32* Array_Size, UINT32 RF_Freq, UINT8 BWMHz)
{
	IRVType IRV_RFTune[]=
	{
	//{ Addr, Data}
		{ 0x0F, 0x00},  //abort tune
		{ 0x0C, 0x15},
		{ 0x0D, 0x40},
		{ 0x0E, 0x0E},	
		{ 0x1F, 0x87},  //Override
		{ 0x20, 0x1F},  //Override
		{ 0x21, 0x87},  //Override
		{ 0x22, 0x1F},  //Override		
		{ 0x80, 0x01},  //Freq Dependent Setting
		{ 0x0F, 0x01},	//start tune
		{ 0, 0}
	};

	UINT32 dig_rf_freq=0;
	UINT32 temp;
	UINT32 Reg_Index=0;
	UINT32 Array_Index=0;
	UINT32 i;
	UINT32 frac_divider = 1000000;

	switch(BWMHz)
	{
	case MxL_BW_6MHz: //6MHz
			SetIRVBit(IRV_RFTune, 0x0C, 0x3F, 0x15);  //set DIG_MODEINDEX, DIG_MODEINDEX_A, and DIG_MODEINDEX_CSF
		break;
	case MxL_BW_7MHz: //7MHz
			SetIRVBit(IRV_RFTune, 0x0C, 0x3F, 0x2A);
		break;
	case MxL_BW_8MHz: //8MHz
			SetIRVBit(IRV_RFTune, 0x0C, 0x3F, 0x3F);
		break;
	}


	//Convert RF frequency into 16 bits => 10 bit integer (MHz) + 6 bit fraction
	dig_rf_freq = RF_Freq / MHz;
	temp = RF_Freq % MHz;
	for(i=0; i<6; i++)
	{
		dig_rf_freq <<= 1;
		frac_divider /=2;
		if(temp > frac_divider)
		{
			temp -= frac_divider;
			dig_rf_freq++;
		}
	}

	//add to have shift center point by 7.8124 kHz
	if(temp > 7812)
		dig_rf_freq ++;
    	
	SetIRVBit(IRV_RFTune, 0x0D, 0xFF, (UINT8)dig_rf_freq);
	SetIRVBit(IRV_RFTune, 0x0E, 0xFF, (UINT8)(dig_rf_freq>>8));

	if (RF_Freq >=333*MHz)
		SetIRVBit(IRV_RFTune, 0x80, 0x40, 0x40);

	//Generate one Array that Contain Data, Address 
	while (IRV_RFTune[Reg_Index].Num || IRV_RFTune[Reg_Index].Val)
	{
		pArray[Array_Index++] = IRV_RFTune[Reg_Index].Num;
		pArray[Array_Index++] = IRV_RFTune[Reg_Index].Val;
		Reg_Index++;
	}
    
	*Array_Size=Array_Index;
	
	return 0;
}

//local functions called by Init and RFTune
UINT32 SetIRVBit(PIRVType pIRV, UINT8 Num, UINT8 Mask, UINT8 Val)
{
	while (pIRV->Num || pIRV->Val)
	{
		if (pIRV->Num==Num)
		{
			pIRV->Val&=~Mask;
			pIRV->Val|=Val;
		}
		pIRV++;

	}	
	return 0;
}























// MaxLinear source code - MxL5007_API.h


/*

 Driver APIs for MxL5007 Tuner
 
 Copyright, Maxlinear, Inc.
 All Rights Reserved
 
 File Name:      MxL5007_API.c
 
 Version:    4.1.3
*/


//#include "StdAfx.h"
//#include "MxL5007_API.h"
//#include "MxL_User_Define.c"
//#include "MxL5007.c"


//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
//																		   //
//							Tuner Functions								   //
//																		   //
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
MxL_ERR_MSG MxL_Set_Register(MxL5007_TunerConfigS* myTuner, UINT8 RegAddr, UINT8 RegData)
{
	UINT32 Status=0;
	UINT8 pArray[2];
	pArray[0] = RegAddr;
	pArray[1] = RegData;
//	Status = MxL_I2C_Write((UINT8)myTuner->I2C_Addr, pArray, 2);
	Status = MxL_I2C_Write(myTuner, pArray, 2);
	if(Status) return MxL_ERR_SET_REG;

	return MxL_OK;

}

MxL_ERR_MSG MxL_Get_Register(MxL5007_TunerConfigS* myTuner, UINT8 RegAddr, UINT8 *RegData)
{
//	if(MxL_I2C_Read((UINT8)myTuner->I2C_Addr, RegAddr, RegData))
	if(MxL_I2C_Read(myTuner, RegAddr, RegData))
		return MxL_ERR_GET_REG;
	return MxL_OK;

}

MxL_ERR_MSG MxL_Soft_Reset(MxL5007_TunerConfigS* myTuner)
{
/*	UINT32 Status=0;	*/
	UINT8 reg_reset=0;
	reg_reset = 0xFF;
/*	if(MxL_I2C_Write((UINT8)myTuner->I2C_Addr, &reg_reset, 1))*/
	if(MxL_I2C_Write(myTuner, &reg_reset, 1))
		return MxL_ERR_OTHERS;

	return MxL_OK;
}

MxL_ERR_MSG MxL_Loop_Through_On(MxL5007_TunerConfigS* myTuner, MxL5007_LoopThru isOn)
{	
	UINT8 pArray[2];	// a array pointer that store the addr and data pairs for I2C write	
	
	pArray[0]=0x04;
	if(isOn)
	 pArray[1]= 0x01;
	else
	 pArray[1]= 0x0;

//	if(MxL_I2C_Write((UINT8)myTuner->I2C_Addr, pArray, 2))
	if(MxL_I2C_Write(myTuner, pArray, 2))
		return MxL_ERR_OTHERS;

	return MxL_OK;
}

MxL_ERR_MSG MxL_Stand_By(MxL5007_TunerConfigS* myTuner)
{
	UINT8 pArray[4];	// a array pointer that store the addr and data pairs for I2C write	
	
	pArray[0] = 0x01;
	pArray[1] = 0x0;
	pArray[2] = 0x0F;
	pArray[3] = 0x0;

//	if(MxL_I2C_Write((UINT8)myTuner->I2C_Addr, pArray, 4))
	if(MxL_I2C_Write(myTuner, pArray, 4))
		return MxL_ERR_OTHERS;

	return MxL_OK;
}

MxL_ERR_MSG MxL_Wake_Up(MxL5007_TunerConfigS* myTuner)
{
	UINT8 pArray[2];	// a array pointer that store the addr and data pairs for I2C write	
	
	pArray[0] = 0x01;
	pArray[1] = 0x01;

//	if(MxL_I2C_Write((UINT8)myTuner->I2C_Addr, pArray, 2))
	if(MxL_I2C_Write(myTuner, pArray, 2))
		return MxL_ERR_OTHERS;

	if(MxL_Tuner_RFTune(myTuner, myTuner->RF_Freq_Hz, myTuner->BW_MHz))
		return MxL_ERR_RFTUNE;

	return MxL_OK;
}

MxL_ERR_MSG MxL_Tuner_Init(MxL5007_TunerConfigS* myTuner)
{	
	UINT8 pArray[MAX_ARRAY_SIZE];	// a array pointer that store the addr and data pairs for I2C write
	UINT32 Array_Size;							// a integer pointer that store the number of element in above array

	//Soft reset tuner
	if(MxL_Soft_Reset(myTuner))
		return MxL_ERR_INIT;

	//perform initialization calculation
	MxL5007_Init(pArray, &Array_Size, (UINT8)myTuner->Mode, myTuner->IF_Diff_Out_Level, (UINT32)myTuner->Xtal_Freq, 
				(UINT32)myTuner->IF_Freq, (UINT8)myTuner->IF_Spectrum, (UINT8)myTuner->ClkOut_Setting, (UINT8)myTuner->ClkOut_Amp);

	//perform I2C write here
//	if(MxL_I2C_Write((UINT8)myTuner->I2C_Addr, pArray, Array_Size))
	if(MxL_I2C_Write(myTuner, pArray, Array_Size))
		return MxL_ERR_INIT;

	return MxL_OK;
}


MxL_ERR_MSG MxL_Tuner_RFTune(MxL5007_TunerConfigS* myTuner, UINT32 RF_Freq_Hz, MxL5007_BW_MHz BWMHz)
{
	//UINT32 Status=0;
	UINT8 pArray[MAX_ARRAY_SIZE];	// a array pointer that store the addr and data pairs for I2C write
	UINT32 Array_Size;							// a integer pointer that store the number of element in above array

	//Store information into struc
	myTuner->RF_Freq_Hz = RF_Freq_Hz;
	myTuner->BW_MHz = BWMHz;

	//perform Channel Change calculation
	MxL5007_RFTune(pArray,&Array_Size,RF_Freq_Hz,BWMHz);

	//perform I2C write here
//	if(MxL_I2C_Write((UINT8)myTuner->I2C_Addr, pArray, Array_Size))
	if(MxL_I2C_Write(myTuner, pArray, Array_Size))
		return MxL_ERR_RFTUNE;

	//wait for 3ms
//	MxL_Delay(3); 
	MxL_Delay(myTuner, 3); 

	return MxL_OK;
}

MxL5007_ChipVersion MxL_Check_ChipVersion(MxL5007_TunerConfigS* myTuner)
{	
	UINT8 Data;
//	if(MxL_I2C_Read((UINT8)myTuner->I2C_Addr, 0xD9, &Data))
	if(MxL_I2C_Read(myTuner, 0xD9, &Data))
		return MxL_GET_ID_FAIL;
		
	switch(Data)
	{
	case 0x14: return MxL_5007T_V4; break;
	default: return MxL_UNKNOWN_ID;
	}	
}

MxL_ERR_MSG MxL_RFSynth_Lock_Status(MxL5007_TunerConfigS* myTuner, BOOL* isLock)
{	
	UINT8 Data;
	*isLock = MxL_FALSE; 
//	if(MxL_I2C_Read((UINT8)myTuner->I2C_Addr, 0xD8, &Data))
	if(MxL_I2C_Read(myTuner, 0xD8, &Data))
		return MxL_ERR_OTHERS;
	Data &= 0x0C;
	if (Data == 0x0C)
		*isLock = MxL_TRUE;  //RF Synthesizer is Lock	
	return MxL_OK;
}

MxL_ERR_MSG MxL_REFSynth_Lock_Status(MxL5007_TunerConfigS* myTuner, BOOL* isLock)
{
	UINT8 Data;
	*isLock = MxL_FALSE; 
//	if(MxL_I2C_Read((UINT8)myTuner->I2C_Addr, 0xD8, &Data))
	if(MxL_I2C_Read(myTuner, 0xD8, &Data))
		return MxL_ERR_OTHERS;
	Data &= 0x03;
	if (Data == 0x03)
		*isLock = MxL_TRUE;   //REF Synthesizer is Lock
	return MxL_OK;
}




