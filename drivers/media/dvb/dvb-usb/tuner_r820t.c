/**

@file

@brief   R820T tuner module definition

One can manipulate R820T tuner through R820T module.
R820T module is derived from tuner module.

*/





#include "tuner_r820t.h"





/**

@brief   R820T tuner module builder

Use BuildR820tModule() to build R820T module, set all module function pointers with the corresponding functions,
and initialize module private variables.


@param [in]   ppTuner                      Pointer to R820T tuner module pointer
@param [in]   pTunerModuleMemory           Pointer to an allocated tuner module memory
@param [in]   pBaseInterfaceModuleMemory   Pointer to an allocated base interface module memory
@param [in]   pI2cBridgeModuleMemory       Pointer to an allocated I2C bridge module memory
@param [in]   DeviceAddr                   R820T I2C device address
@param [in]   CrystalFreqHz                R820T crystal frequency in Hz


@note
	-# One should call BuildR820tModule() to build R820T module before using it.

*/
void
BuildR820tModule(
	TUNER_MODULE **ppTuner,
	TUNER_MODULE *pTunerModuleMemory,
	BASE_INTERFACE_MODULE *pBaseInterfaceModuleMemory,
	I2C_BRIDGE_MODULE *pI2cBridgeModuleMemory,
	unsigned char DeviceAddr,
	unsigned char RafaelChipID
	)
{
	TUNER_MODULE *pTuner;
	R820T_EXTRA_MODULE *pExtra;


	// Set tuner module pointer.
	*ppTuner = pTunerModuleMemory;

	// Get tuner module.
	pTuner = *ppTuner;

	// Set base interface module pointer and I2C bridge module pointer.
	pTuner->pBaseInterface = pBaseInterfaceModuleMemory;
	pTuner->pI2cBridge = pI2cBridgeModuleMemory;

	// Get tuner extra module.
	pExtra = &(pTuner->Extra.R820t);



	// Set tuner type.
	pTuner->TunerType = TUNER_TYPE_R820T;

	// Set tuner I2C device address.
	pTuner->DeviceAddr = DeviceAddr;


	// Initialize tuner parameter setting status.
	pTuner->IsRfFreqHzSet = NO;


	// Set tuner module manipulating function pointers.
	pTuner->GetTunerType  = r820t_GetTunerType;
	pTuner->GetDeviceAddr = r820t_GetDeviceAddr;

	pTuner->Initialize    = r820t_Initialize;
	pTuner->SetRfFreqHz   = r820t_SetRfFreqHz;
	pTuner->GetRfFreqHz   = r820t_GetRfFreqHz;


	// Initialize tuner extra module variables.
	pExtra->IsStandardModeSet = NO;
	pExtra->Rafael_Chip = RafaelChipID;

	// Set tuner extra module function pointers.	
	pExtra->SetStandardMode = r820t_SetStandardMode;
	pExtra->GetStandardMode = r820t_GetStandardMode;
	pExtra->SetStandby = r820t_SetStandby;

	
	return;
}





/**

@see   TUNER_FP_GET_TUNER_TYPE

*/
void
r820t_GetTunerType(
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
r820t_GetDeviceAddr(
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
r820t_Initialize(
	TUNER_MODULE *pTuner
	)
{
	// Initialize tuner.
	if(R828_Init(pTuner) != RT_Success)
		goto error_status_initialize_tuner;


	return FUNCTION_SUCCESS;


error_status_initialize_tuner:
	return FUNCTION_ERROR;
}





/**

@see   TUNER_FP_SET_RF_FREQ_HZ

*/
int
r820t_SetRfFreqHz(
	TUNER_MODULE *pTuner,
	unsigned long RfFreqHz
	)
{
	R820T_EXTRA_MODULE *pExtra;
	
	R828_Set_Info R828Info;


	// Get tuner extra module.
	pExtra = &(pTuner->Extra.R820t);

	if(pExtra->IsStandardModeSet==NO)
		goto error_status_set_tuner_rf_frequency;
		
	R828Info.R828_Standard = (R828_Standard_Type)pExtra->StandardMode;
	R828Info.RF_KHz = (UINT32)(RfFreqHz /1000);

	if(R828_SetFrequency(pTuner, R828Info, NORMAL_MODE) != RT_Success)
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
r820t_GetRfFreqHz(
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

@brief   Set R820T tuner standard mode.

*/
int
r820t_SetStandardMode(
	TUNER_MODULE *pTuner,
	int StandardMode
	)
{
	R820T_EXTRA_MODULE *pExtra;


	// Get tuner extra module.
	pExtra = &(pTuner->Extra.R820t);


	if(R828_SetStandard(pTuner, (R828_Standard_Type)StandardMode) != RT_Success)
		goto error_status_set_tuner_standard_mode;


	// Set tuner standard mode parameter.
	pExtra->StandardMode      = StandardMode;
	pExtra->IsStandardModeSet = YES;


	return FUNCTION_SUCCESS;


error_status_set_tuner_standard_mode:
	return FUNCTION_ERROR;
}






/**

@brief   Get R820T tuner standard mode.

*/
int
r820t_GetStandardMode(
	TUNER_MODULE *pTuner,
	int *pStandardMode
	)
{
	R820T_EXTRA_MODULE *pExtra;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.R820t);


	// Get tuner bandwidth mode from tuner module.
	if(pExtra->IsStandardModeSet != YES)
		goto error_status_get_tuner_standard_mode;

	*pStandardMode = pExtra->StandardMode;


	return FUNCTION_SUCCESS;


error_status_get_tuner_standard_mode:
	return FUNCTION_ERROR;
}




/**

@brief   Set R820T tuner standby

*/
int
r820t_SetStandby(
	TUNER_MODULE *pTuner,
	int LoopThroughType
	)
{

	if(R828_Standby(pTuner, (R828_LoopThrough_Type)LoopThroughType) != RT_Success)
		goto error_status_set_tuner_standby;

	return FUNCTION_SUCCESS;


error_status_set_tuner_standby:
	return FUNCTION_ERROR;
}












// The following context is implemented for R820T source code.

int
r820t_Convert(int InvertNum)
{
	int ReturnNum;
	int AddNum;
	int BitNum;
	int CuntNum;

	ReturnNum = 0;
	AddNum    = 0x80;
	BitNum    = 0x01;

	for(CuntNum = 0;CuntNum < 8;CuntNum ++)
	{
		if(BitNum & InvertNum)
			ReturnNum += AddNum;

		AddNum /= 2;
		BitNum *= 2;
	}

	return ReturnNum;
}



R828_ErrCode
I2C_Write_Len(TUNER_MODULE *pTuner, R828_I2C_LEN_TYPE *I2C_Info)
{

	BASE_INTERFACE_MODULE *pBaseInterface;
	I2C_BRIDGE_MODULE *pI2cBridge;
	unsigned char DeviceAddr;

	unsigned int i, j;

	unsigned char RegStartAddr;
	unsigned char *pWritingBytes;
	unsigned long ByteNum;

	unsigned char WritingBuffer[I2C_BUFFER_LEN];
	unsigned long WritingByteNum, WritingByteNumMax, WritingByteNumRem;
	unsigned char RegWritingAddr;



	// Get tuner module, base interface, and I2C bridge.
	pBaseInterface = pTuner->pBaseInterface;
	pI2cBridge     = pTuner->pI2cBridge;

	// Get tuner device address.
	pTuner->GetDeviceAddr(pTuner, &DeviceAddr);


	// Get regiser start address, writing bytes, and byte number.
	RegStartAddr  = I2C_Info->RegAddr;
	pWritingBytes = I2C_Info->Data;
	ByteNum       = (unsigned long)I2C_Info->Len;


	// Calculate maximum writing byte number.
	WritingByteNumMax = pBaseInterface->I2cWritingByteNumMax - LEN_1_BYTE;


	// Set tuner register bytes with writing bytes.
	// Note: Set tuner register bytes considering maximum writing byte number.
	for(i = 0; i < ByteNum; i += WritingByteNumMax)
	{
		// Set register writing address.
		RegWritingAddr = RegStartAddr + i;

		// Calculate remainder writing byte number.
		WritingByteNumRem = ByteNum - i;

		// Determine writing byte number.
		WritingByteNum = (WritingByteNumRem > WritingByteNumMax) ? WritingByteNumMax : WritingByteNumRem;


		// Set writing buffer.
		// Note: The I2C format of tuner register byte setting is as follows:
		//       start_bit + (DeviceAddr | writing_bit) + RegWritingAddr + writing_bytes (WritingByteNum bytes) +
		//       stop_bit
		WritingBuffer[0] = RegWritingAddr;

		for(j = 0; j < WritingByteNum; j++)
			WritingBuffer[LEN_1_BYTE + j] = pWritingBytes[i + j];


		// Set tuner register bytes with writing buffer.
		if(pI2cBridge->ForwardI2cWritingCmd(pI2cBridge, DeviceAddr, WritingBuffer, WritingByteNum + LEN_1_BYTE) != 
			FUNCTION_SUCCESS)
			goto error_status_set_tuner_registers;
	}


	return RT_Success;


error_status_set_tuner_registers:
	
	return RT_Fail;

	
}



R828_ErrCode
I2C_Read_Len(TUNER_MODULE *pTuner, R828_I2C_LEN_TYPE *I2C_Info)
{
	BASE_INTERFACE_MODULE *pBaseInterface;
	I2C_BRIDGE_MODULE *pI2cBridge;
	unsigned char DeviceAddr;

	unsigned int i;

	unsigned char RegStartAddr;
	unsigned char ReadingBytes[I2C_BUFFER_LEN];
	unsigned long ByteNum;


	// Get tuner module, base interface, and I2C bridge.
	pBaseInterface = pTuner->pBaseInterface;
	pI2cBridge     = pTuner->pI2cBridge;

	// Get tuner device address.
	pTuner->GetDeviceAddr(pTuner, &DeviceAddr);


	// Get regiser start address, writing bytes, and byte number.
	RegStartAddr  = 0x00;
	ByteNum       = (unsigned long)I2C_Info->Len;

	// Set tuner register reading address.
	// Note: The I2C format of tuner register reading address setting is as follows:
	//       start_bit + (DeviceAddr | writing_bit) + RegReadingAddr + stop_bit
	if(pI2cBridge->ForwardI2cWritingCmd(pI2cBridge, DeviceAddr, &RegStartAddr, LEN_1_BYTE) != FUNCTION_SUCCESS)
		goto error_status_set_tuner_register_reading_address;

	// Get tuner register bytes.
	// Note: The I2C format of tuner register byte getting is as follows:
	//       start_bit + (DeviceAddr | reading_bit) + reading_bytes (ReadingByteNum bytes) + stop_bit
	if(pI2cBridge->ForwardI2cReadingCmd(pI2cBridge, DeviceAddr, ReadingBytes, ByteNum) != FUNCTION_SUCCESS)
		goto error_status_get_tuner_registers;


	for(i = 0; i<ByteNum; i++)
	{
		 I2C_Info->Data[i] = (UINT8)r820t_Convert(ReadingBytes[i]);
	}


	return RT_Success;


error_status_get_tuner_registers:
error_status_set_tuner_register_reading_address:
	
	return RT_Fail;


	
}



R828_ErrCode
I2C_Write(TUNER_MODULE *pTuner, R828_I2C_TYPE *I2C_Info)
{
	I2C_BRIDGE_MODULE *pI2cBridge;
	unsigned char DeviceAddr;
	unsigned char WritingBuffer[LEN_2_BYTE];


	// Get I2C bridge.
	pI2cBridge = pTuner->pI2cBridge;

	// Get tuner device address.
	pTuner->GetDeviceAddr(pTuner, &DeviceAddr);


	// Set writing bytes.
	// Note: The I2C format of tuner register byte setting is as follows:
	//       start_bit + (DeviceAddr | writing_bit) + addr + data + stop_bit
	WritingBuffer[0] = I2C_Info->RegAddr;
	WritingBuffer[1] = I2C_Info->Data;

	// Set tuner register bytes with writing buffer.
	if(pI2cBridge->ForwardI2cWritingCmd(pI2cBridge, DeviceAddr, WritingBuffer, LEN_2_BYTE) != FUNCTION_SUCCESS)
		goto error_status_set_tuner_registers;


	return RT_Success;

error_status_set_tuner_registers:
	

	return RT_Fail;


}




void
R828_Delay_MS(
	TUNER_MODULE *pTuner,
	unsigned long WaitTimeMs
	)
{
	BASE_INTERFACE_MODULE *pBaseInterface;


	// Get base interface.
	pBaseInterface = pTuner->pBaseInterface;

	// Wait in ms.
	pBaseInterface->WaitMs(pBaseInterface, WaitTimeMs);


	return;
}


//-----------------------------------------------------
//  
// Filename: R820T.c   
//
// This file is R820T tuner driver
// Copyright 2011 by Rafaelmicro., Inc.
//
//-----------------------------------------------------


//#include "stdafx.h"
//#include "R828.h"
//#include "..\I2C_Sys.h"


#if(TUNER_CLK_OUT==TRUE)  //enable tuner clk output for share Xtal application
UINT8 R828_iniArry[27] = {0x83, 0x32, 0x75, 0xC0, 0x40, 0xD6, 0x6C, 0xF5, 0x63,
					/*     0x05  0x06  0x07  0x08  0x09  0x0A  0x0B  0x0C  0x0D                                                    */	   
													  
						   0x75, 0x68, 0x6C, 0x83, 0x80, 0x00, 0x0F, 0x00, 0xC0,//xtal_check
					/*     0x0E  0x0F  0x10  0x11  0x12  0x13  0x14  0x15  0x16                                                    */	   

						   0x30, 0x48, 0xCC, 0x60, 0x00, 0x54, 0xAE, 0x4A, 0xC0};
					/*     0x17  0x18  0x19  0x1A  0x1B  0x1C  0x1D  0x1E  0x1F                                                    */
#else
UINT8 R828_iniArry[27] = {0x83, 0x32, 0x75, 0xC0, 0x40, 0xD6, 0x6C, 0xF5, 0x63,
					/*     0x05  0x06  0x07  0x08  0x09  0x0A  0x0B  0x0C  0x0D                                                    */	   
													  
						   0x75, 0x78, 0x6C, 0x83, 0x80, 0x00, 0x0F, 0x00, 0xC0,//xtal_check
					/*     0x0E  0x0F  0x10  0x11  0x12  0x13  0x14  0x15  0x16                                                    */	   

						   0x30, 0x48, 0xCC, 0x60, 0x00, 0x54, 0xAE, 0x4A, 0xC0};
					/*     0x17  0x18  0x19  0x1A  0x1B  0x1C  0x1D  0x1E  0x1F                                                    */
#endif

UINT8 R828_ADDRESS=0x34;
UINT8 Rafael_Chip = R820T;
//----------------------------------------------------------//
//                   Internal Structs                       //
//----------------------------------------------------------//
typedef struct _R828_SectType
{
	UINT8 Phase_Y;
	UINT8 Gain_X;
	UINT16 Value;
}R828_SectType;

typedef enum _BW_Type
{
	BW_6M = 0,
	BW_7M,
	BW_8M,
	BW_1_7M,
	BW_10M,
	BW_200K
}BW_Type;

typedef struct _Sys_Info_Type
{
	UINT16		IF_KHz;
	BW_Type		BW;
	UINT32		FILT_CAL_LO;
	UINT8		FILT_GAIN;
	UINT8		IMG_R;
	UINT8		FILT_Q;
	UINT8		HP_COR;
	UINT8       EXT_ENABLE;
	UINT8       LOOP_THROUGH;
	UINT8       LT_ATT;
	UINT8       FLT_EXT_WIDEST;
	UINT8       POLYFIL_CUR;
}Sys_Info_Type;

typedef struct _Freq_Info_Type
{
	UINT8		OPEN_D;
	UINT8		RF_MUX_PLOY;
	UINT8		TF_C;
	UINT8		XTAL_CAP20P;	
	UINT8		XTAL_CAP10P;	
	UINT8		XTAL_CAP0P;	
	UINT8		IMR_MEM;
}Freq_Info_Type;


typedef struct _SysFreq_Info_Type
{
	UINT8		LNA_TOP;
	UINT8		LNA_VTH_L;
	UINT8		MIXER_TOP;
	UINT8		MIXER_VTH_L;
	UINT8      AIR_CABLE1_IN;
	UINT8      CABLE2_IN;
	UINT8		PRE_DECT;
	UINT8      LNA_DISCHARGE;
	UINT8      CP_CUR;
	UINT8      DIV_BUF_CUR;
    UINT8      FILTER_CUR;
}SysFreq_Info_Type;

//----------------------------------------------------------//
//                   Internal Parameters                    //
//----------------------------------------------------------//
enum XTAL_CAP_VALUE
{
	XTAL_LOW_CAP_30P = 0,
    XTAL_LOW_CAP_20P,
    XTAL_LOW_CAP_10P,
    XTAL_LOW_CAP_0P,
	XTAL_HIGH_CAP_0P
};
UINT8 R828_Arry[27];
R828_SectType IMR_Data[5] = {
                                                  {0, 0, 0},
                                                  {0, 0, 0},
                                                  {0, 0, 0},
                                                  {0, 0, 0},
                                                  {0, 0, 0}
                                                };//Please keep this array data for standby mode.
R828_I2C_TYPE  R828_I2C;
R828_I2C_LEN_TYPE R828_I2C_Len;

UINT32 R828_IF_khz;
UINT32 R828_CAL_LO_khz;
UINT8  R828_IMR_point_num;
UINT8  R828_IMR_done_flag = FALSE;
UINT8  R828_Fil_Cal_flag[STD_SIZE];
static UINT8 R828_Fil_Cal_code[STD_SIZE];

static UINT8 Xtal_cap_sel = XTAL_LOW_CAP_0P;
static UINT8 Xtal_cap_sel_tmp = XTAL_LOW_CAP_0P;
//----------------------------------------------------------//
//                   Internal static struct                 //
//----------------------------------------------------------//
static SysFreq_Info_Type SysFreq_Info1;
static Sys_Info_Type Sys_Info1;
//static Freq_Info_Type R828_Freq_Info;
static Freq_Info_Type Freq_Info1;
//----------------------------------------------------------//
//                   Internal Functions                     //
//----------------------------------------------------------//
R828_ErrCode R828_Xtal_Check(TUNER_MODULE *pTuner);
R828_ErrCode R828_InitReg(TUNER_MODULE *pTuner);
R828_ErrCode R828_IMR_Prepare(TUNER_MODULE *pTuner);
R828_ErrCode R828_IMR(TUNER_MODULE *pTuner, UINT8 IMR_MEM, BOOL IM_Flag);
R828_ErrCode R828_PLL(TUNER_MODULE *pTuner, UINT32 LO_Freq, R828_Standard_Type R828_Standard);
R828_ErrCode R828_MUX(TUNER_MODULE *pTuner, UINT32 RF_KHz);
R828_ErrCode R828_IQ(TUNER_MODULE *pTuner, R828_SectType* IQ_Pont);
R828_ErrCode R828_IQ_Tree(TUNER_MODULE *pTuner, UINT8 FixPot, UINT8 FlucPot, UINT8 PotReg, R828_SectType* CompareTree);
R828_ErrCode R828_CompreCor(R828_SectType* CorArry);
R828_ErrCode R828_CompreStep(TUNER_MODULE *pTuner, R828_SectType* StepArry, UINT8 Pace);
R828_ErrCode R828_Muti_Read(TUNER_MODULE *pTuner, UINT8 IMR_Reg, UINT16* IMR_Result_Data);
R828_ErrCode R828_Section(TUNER_MODULE *pTuner, R828_SectType* SectionArry);
R828_ErrCode R828_F_IMR(TUNER_MODULE *pTuner, R828_SectType* IQ_Pont);
R828_ErrCode R828_IMR_Cross(TUNER_MODULE *pTuner, R828_SectType* IQ_Pont, UINT8* X_Direct);

Sys_Info_Type R828_Sys_Sel(R828_Standard_Type R828_Standard);
Freq_Info_Type R828_Freq_Sel(UINT32 RF_freq);
SysFreq_Info_Type R828_SysFreq_Sel(R828_Standard_Type R828_Standard,UINT32 RF_freq);

R828_ErrCode R828_Filt_Cal(TUNER_MODULE *pTuner, UINT32 Cal_Freq,BW_Type R828_BW);
//R828_ErrCode R828_SetFrequency(TUNER_MODULE *pTuner, R828_Set_Info R828_INFO, R828_SetFreq_Type R828_SetFreqMode);



Sys_Info_Type R828_Sys_Sel(R828_Standard_Type R828_Standard)
{
	Sys_Info_Type R828_Sys_Info;

	switch (R828_Standard)
	{

	case DVB_T_6M: 
	case DVB_T2_6M: 
		R828_Sys_Info.IF_KHz=3570;
		R828_Sys_Info.BW=BW_6M;
		R828_Sys_Info.FILT_CAL_LO=56000; //52000->56000
		R828_Sys_Info.FILT_GAIN=0x10;  //+3dB, 6MHz on
		R828_Sys_Info.IMG_R=0x00;		//image negative
		R828_Sys_Info.FILT_Q=0x10;		//R10[4]:low Q(1'b1)
		R828_Sys_Info.HP_COR=0x6B;		// 1.7M disable, +2cap, 1.0MHz		
		R828_Sys_Info.EXT_ENABLE=0x60;  //R30[6]=1 ext enable; R30[5]:1 ext at LNA max-1 
		R828_Sys_Info.LOOP_THROUGH=0x00; //R5[7], LT ON
		R828_Sys_Info.LT_ATT=0x00;       //R31[7], LT ATT enable
		R828_Sys_Info.FLT_EXT_WIDEST=0x00;//R15[7]: FLT_EXT_WIDE OFF
		R828_Sys_Info.POLYFIL_CUR=0x60;  //R25[6:5]:Min
		break;

	case DVB_T_7M: 
	case DVB_T2_7M: 
		R828_Sys_Info.IF_KHz=4070;
		R828_Sys_Info.BW=BW_7M;
		R828_Sys_Info.FILT_CAL_LO=60000;
		R828_Sys_Info.FILT_GAIN=0x10;  //+3dB, 6MHz on
		R828_Sys_Info.IMG_R=0x00;		//image negative
		R828_Sys_Info.FILT_Q=0x10;		//R10[4]:low Q(1'b1)
		R828_Sys_Info.HP_COR=0x2B;		// 1.7M disable, +1cap, 1.0MHz		
		R828_Sys_Info.EXT_ENABLE=0x60;  //R30[6]=1 ext enable; R30[5]:1 ext at LNA max-1 
		R828_Sys_Info.LOOP_THROUGH=0x00; //R5[7], LT ON
		R828_Sys_Info.LT_ATT=0x00;       //R31[7], LT ATT enable
		R828_Sys_Info.FLT_EXT_WIDEST=0x00;//R15[7]: FLT_EXT_WIDE OFF
		R828_Sys_Info.POLYFIL_CUR=0x60;  //R25[6:5]:Min
		break;

	case DVB_T_7M_2:  
	case DVB_T2_7M_2:  
		R828_Sys_Info.IF_KHz=4570;
		R828_Sys_Info.BW=BW_7M;
		R828_Sys_Info.FILT_CAL_LO=63000;
		R828_Sys_Info.FILT_GAIN=0x10;  //+3dB, 6MHz on
		R828_Sys_Info.IMG_R=0x00;		//image negative
		R828_Sys_Info.FILT_Q=0x10;		//R10[4]:low Q(1'b1)
		R828_Sys_Info.HP_COR=0x2A;		// 1.7M disable, +1cap, 1.25MHz		
		R828_Sys_Info.EXT_ENABLE=0x60;  //R30[6]=1 ext enable; R30[5]:1 ext at LNA max-1 
		R828_Sys_Info.LOOP_THROUGH=0x00; //R5[7], LT ON
		R828_Sys_Info.LT_ATT=0x00;       //R31[7], LT ATT enable
		R828_Sys_Info.FLT_EXT_WIDEST=0x00;//R15[7]: FLT_EXT_WIDE OFF
		R828_Sys_Info.POLYFIL_CUR=0x60;  //R25[6:5]:Min
		break;

	case DVB_T_8M: 
	case DVB_T2_8M: 
		R828_Sys_Info.IF_KHz=4570;
		R828_Sys_Info.BW=BW_8M;
		R828_Sys_Info.FILT_CAL_LO=68500;
		R828_Sys_Info.FILT_GAIN=0x10;  //+3dB, 6MHz on
		R828_Sys_Info.IMG_R=0x00;		//image negative
		R828_Sys_Info.FILT_Q=0x10;		//R10[4]:low Q(1'b1)
		R828_Sys_Info.HP_COR=0x0B;		// 1.7M disable, +0cap, 1.0MHz		
		R828_Sys_Info.EXT_ENABLE=0x60;  //R30[6]=1 ext enable; R30[5]:1 ext at LNA max-1 
		R828_Sys_Info.LOOP_THROUGH=0x00; //R5[7], LT ON
		R828_Sys_Info.LT_ATT=0x00;       //R31[7], LT ATT enable
		R828_Sys_Info.FLT_EXT_WIDEST=0x00;//R15[7]: FLT_EXT_WIDE OFF
		R828_Sys_Info.POLYFIL_CUR=0x60;  //R25[6:5]:Min
		break;

	case ISDB_T: 
		R828_Sys_Info.IF_KHz=4063;
		R828_Sys_Info.BW=BW_6M;
		R828_Sys_Info.FILT_CAL_LO=59000;
		R828_Sys_Info.FILT_GAIN=0x10;  //+3dB, 6MHz on
		R828_Sys_Info.IMG_R=0x00;		//image negative
		R828_Sys_Info.FILT_Q=0x10;		//R10[4]:low Q(1'b1)
		R828_Sys_Info.HP_COR=0x6A;		// 1.7M disable, +2cap, 1.25MHz		
		R828_Sys_Info.EXT_ENABLE=0x40;  //R30[6], ext enable; R30[5]:0 ext at LNA max 
		R828_Sys_Info.LOOP_THROUGH=0x00; //R5[7], LT ON
		R828_Sys_Info.LT_ATT=0x00;       //R31[7], LT ATT enable
		R828_Sys_Info.FLT_EXT_WIDEST=0x00;//R15[7]: FLT_EXT_WIDE OFF
		R828_Sys_Info.POLYFIL_CUR=0x60;  //R25[6:5]:Min
		break;

	default:  //DVB_T_8M
		R828_Sys_Info.IF_KHz=4570;
		R828_Sys_Info.BW=BW_8M;
		R828_Sys_Info.FILT_CAL_LO=68500;
		R828_Sys_Info.FILT_GAIN=0x10;  //+3dB, 6MHz on
		R828_Sys_Info.IMG_R=0x00;		//image negative
		R828_Sys_Info.FILT_Q=0x10;		//R10[4]:low Q(1'b1)
		R828_Sys_Info.HP_COR=0x0D;		// 1.7M disable, +0cap, 0.7MHz		
		R828_Sys_Info.EXT_ENABLE=0x60;  //R30[6]=1 ext enable; R30[5]:1 ext at LNA max-1 
		R828_Sys_Info.LOOP_THROUGH=0x00; //R5[7], LT ON
		R828_Sys_Info.LT_ATT=0x00;       //R31[7], LT ATT enable
		R828_Sys_Info.FLT_EXT_WIDEST=0x00;//R15[7]: FLT_EXT_WIDE OFF
		R828_Sys_Info.POLYFIL_CUR=0x60;  //R25[6:5]:Min
		break;

	}

	return R828_Sys_Info;
}



Freq_Info_Type R828_Freq_Sel(UINT32 LO_freq)
{
	Freq_Info_Type R828_Freq_Info;

	if(LO_freq<50000)
	{
		R828_Freq_Info.OPEN_D=0x08; // low
		R828_Freq_Info.RF_MUX_PLOY = 0x02;  //R26[7:6]=0 (LPF)  R26[1:0]=2 (low)
		R828_Freq_Info.TF_C=0xDF;     //R27[7:0]  band2,band0
		R828_Freq_Info.XTAL_CAP20P=0x02;  //R16[1:0]  20pF (10)  
		R828_Freq_Info.XTAL_CAP10P=0x01; 
		R828_Freq_Info.XTAL_CAP0P=0x00;
		R828_Freq_Info.IMR_MEM = 0;
	}

	else if(LO_freq>=50000 && LO_freq<55000)
	{
		R828_Freq_Info.OPEN_D=0x08; // low
		R828_Freq_Info.RF_MUX_PLOY = 0x02;  //R26[7:6]=0 (LPF)  R26[1:0]=2 (low)
		R828_Freq_Info.TF_C=0xBE;     //R27[7:0]  band4,band1 
		R828_Freq_Info.XTAL_CAP20P=0x02;  //R16[1:0]  20pF (10)  
		R828_Freq_Info.XTAL_CAP10P=0x01; 
		R828_Freq_Info.XTAL_CAP0P=0x00;
		R828_Freq_Info.IMR_MEM = 0;
	}
	else if( LO_freq>=55000 && LO_freq<60000)
	{
		R828_Freq_Info.OPEN_D=0x08; // low
		R828_Freq_Info.RF_MUX_PLOY = 0x02;  //R26[7:6]=0 (LPF)  R26[1:0]=2 (low)
		R828_Freq_Info.TF_C=0x8B;     //R27[7:0]  band7,band4
		R828_Freq_Info.XTAL_CAP20P=0x02;  //R16[1:0]  20pF (10)  
		R828_Freq_Info.XTAL_CAP10P=0x01; 
		R828_Freq_Info.XTAL_CAP0P=0x00;
		R828_Freq_Info.IMR_MEM = 0;
	}	
	else if( LO_freq>=60000 && LO_freq<65000)
	{
		R828_Freq_Info.OPEN_D=0x08; // low
		R828_Freq_Info.RF_MUX_PLOY = 0x02;  //R26[7:6]=0 (LPF)  R26[1:0]=2 (low)
		R828_Freq_Info.TF_C=0x7B;     //R27[7:0]  band8,band4
		R828_Freq_Info.XTAL_CAP20P=0x02;  //R16[1:0]  20pF (10)  
		R828_Freq_Info.XTAL_CAP10P=0x01; 
		R828_Freq_Info.XTAL_CAP0P=0x00;
		R828_Freq_Info.IMR_MEM = 0;
	}
	else if( LO_freq>=65000 && LO_freq<70000)
	{
		R828_Freq_Info.OPEN_D=0x08; // low
		R828_Freq_Info.RF_MUX_PLOY = 0x02;  //R26[7:6]=0 (LPF)  R26[1:0]=2 (low)
		R828_Freq_Info.TF_C=0x69;     //R27[7:0]  band9,band6
		R828_Freq_Info.XTAL_CAP20P=0x02;  //R16[1:0]  20pF (10)  
		R828_Freq_Info.XTAL_CAP10P=0x01; 
		R828_Freq_Info.XTAL_CAP0P=0x00;
		R828_Freq_Info.IMR_MEM = 0;
	}	
	else if( LO_freq>=70000 && LO_freq<75000)
	{
		R828_Freq_Info.OPEN_D=0x08; // low
		R828_Freq_Info.RF_MUX_PLOY = 0x02;  //R26[7:6]=0 (LPF)  R26[1:0]=2 (low)
		R828_Freq_Info.TF_C=0x58;     //R27[7:0]  band10,band7
		R828_Freq_Info.XTAL_CAP20P=0x02;  //R16[1:0]  20pF (10)  
		R828_Freq_Info.XTAL_CAP10P=0x01; 
		R828_Freq_Info.XTAL_CAP0P=0x00;
		R828_Freq_Info.IMR_MEM = 0;
	}
	else if( LO_freq>=75000 && LO_freq<80000)
	{
		R828_Freq_Info.OPEN_D=0x00; // high
		R828_Freq_Info.RF_MUX_PLOY = 0x02;  //R26[7:6]=0 (LPF)  R26[1:0]=2 (low)
		R828_Freq_Info.TF_C=0x44;     //R27[7:0]  band11,band11
		R828_Freq_Info.XTAL_CAP20P=0x02;  //R16[1:0]  20pF (10)  
		R828_Freq_Info.XTAL_CAP10P=0x01; 
		R828_Freq_Info.XTAL_CAP0P=0x00;
		R828_Freq_Info.IMR_MEM = 0;
	}
	else if( LO_freq>=80000 && LO_freq<90000)
	{
		R828_Freq_Info.OPEN_D=0x00; // high
		R828_Freq_Info.RF_MUX_PLOY = 0x02;  //R26[7:6]=0 (LPF)  R26[1:0]=2 (low)
		R828_Freq_Info.TF_C=0x44;     //R27[7:0]  band11,band11
		R828_Freq_Info.XTAL_CAP20P=0x02;  //R16[1:0]  20pF (10)  
		R828_Freq_Info.XTAL_CAP10P=0x01; 
		R828_Freq_Info.XTAL_CAP0P=0x00;
		R828_Freq_Info.IMR_MEM = 0;
	}
	else if( LO_freq>=90000 && LO_freq<100000)
	{
		R828_Freq_Info.OPEN_D=0x00; // high
		R828_Freq_Info.RF_MUX_PLOY = 0x02;  //R26[7:6]=0 (LPF)  R26[1:0]=2 (low)
		R828_Freq_Info.TF_C=0x34;     //R27[7:0]  band12,band11
		R828_Freq_Info.XTAL_CAP20P=0x01;  //R16[1:0]  10pF (01)  
		R828_Freq_Info.XTAL_CAP10P=0x01; 
		R828_Freq_Info.XTAL_CAP0P=0x00;
		R828_Freq_Info.IMR_MEM = 0;
	}
	else if( LO_freq>=100000 && LO_freq<110000)
	{
		R828_Freq_Info.OPEN_D=0x00; // high
		R828_Freq_Info.RF_MUX_PLOY = 0x02;  //R26[7:6]=0 (LPF)  R26[1:0]=2 (low)
		R828_Freq_Info.TF_C=0x34;     //R27[7:0]  band12,band11
		R828_Freq_Info.XTAL_CAP20P=0x01;  //R16[1:0]  10pF (01)   
		R828_Freq_Info.XTAL_CAP10P=0x01; 
		R828_Freq_Info.XTAL_CAP0P=0x00;
		R828_Freq_Info.IMR_MEM = 0;
	}
	else if( LO_freq>=110000 && LO_freq<120000)
	{
		R828_Freq_Info.OPEN_D=0x00; // high
		R828_Freq_Info.RF_MUX_PLOY = 0x02;  //R26[7:6]=0 (LPF)  R26[1:0]=2 (low)
		R828_Freq_Info.TF_C=0x24;     //R27[7:0]  band13,band11
		R828_Freq_Info.XTAL_CAP20P=0x01;  //R16[1:0]  10pF (01)  
		R828_Freq_Info.XTAL_CAP10P=0x01; 
		R828_Freq_Info.XTAL_CAP0P=0x00;
		R828_Freq_Info.IMR_MEM = 1;
	}
	else if( LO_freq>=120000 && LO_freq<140000)
	{
		R828_Freq_Info.OPEN_D=0x00; // high
		R828_Freq_Info.RF_MUX_PLOY = 0x02;  //R26[7:6]=0 (LPF)  R26[1:0]=2 (low)
		R828_Freq_Info.TF_C=0x24;     //R27[7:0]  band13,band11
		R828_Freq_Info.XTAL_CAP20P=0x01;  //R16[1:0]  10pF (01)  
		R828_Freq_Info.XTAL_CAP10P=0x01; 
		R828_Freq_Info.XTAL_CAP0P=0x00;
		R828_Freq_Info.IMR_MEM = 1;
	}
	else if( LO_freq>=140000 && LO_freq<180000)
	{
		R828_Freq_Info.OPEN_D=0x00; // high
		R828_Freq_Info.RF_MUX_PLOY = 0x02;  //R26[7:6]=0 (LPF)  R26[1:0]=2 (low)
		R828_Freq_Info.TF_C=0x14;     //R27[7:0]  band14,band11
		R828_Freq_Info.XTAL_CAP20P=0x01;  //R16[1:0]  10pF (01)  
		R828_Freq_Info.XTAL_CAP10P=0x01; 
		R828_Freq_Info.XTAL_CAP0P=0x00;
		R828_Freq_Info.IMR_MEM = 1;
	}
	else if( LO_freq>=180000 && LO_freq<220000)
	{
		R828_Freq_Info.OPEN_D=0x00; // high
		R828_Freq_Info.RF_MUX_PLOY = 0x02;  //R26[7:6]=0 (LPF)  R26[1:0]=2 (low)
		R828_Freq_Info.TF_C=0x13;     //R27[7:0]  band14,band12
		R828_Freq_Info.XTAL_CAP20P=0x00;  //R16[1:0]  0pF (00)  
		R828_Freq_Info.XTAL_CAP10P=0x00; 
		R828_Freq_Info.XTAL_CAP0P=0x00;
		R828_Freq_Info.IMR_MEM = 1;
	}
	else if( LO_freq>=220000 && LO_freq<250000)
	{
		R828_Freq_Info.OPEN_D=0x00; // high
		R828_Freq_Info.RF_MUX_PLOY = 0x02;  //R26[7:6]=0 (LPF)  R26[1:0]=2 (low)
		R828_Freq_Info.TF_C=0x13;     //R27[7:0]  band14,band12
		R828_Freq_Info.XTAL_CAP20P=0x00;  //R16[1:0]  0pF (00)  
		R828_Freq_Info.XTAL_CAP10P=0x00; 
		R828_Freq_Info.XTAL_CAP0P=0x00;
		R828_Freq_Info.IMR_MEM = 2;
	}
	else if( LO_freq>=250000 && LO_freq<280000)
	{
		R828_Freq_Info.OPEN_D=0x00; // high
		R828_Freq_Info.RF_MUX_PLOY = 0x02;  //R26[7:6]=0 (LPF)  R26[1:0]=2 (low)
		R828_Freq_Info.TF_C=0x11;     //R27[7:0]  highest,highest
		R828_Freq_Info.XTAL_CAP20P=0x00;  //R16[1:0]  0pF (00)  
		R828_Freq_Info.XTAL_CAP10P=0x00; 
		R828_Freq_Info.XTAL_CAP0P=0x00;
		R828_Freq_Info.IMR_MEM = 2;
	}
	else if( LO_freq>=280000 && LO_freq<310000)
	{
		R828_Freq_Info.OPEN_D=0x00; // high
		R828_Freq_Info.RF_MUX_PLOY = 0x02;  //R26[7:6]=0 (LPF)  R26[1:0]=2 (low)
		R828_Freq_Info.TF_C=0x00;     //R27[7:0]  highest,highest
		R828_Freq_Info.XTAL_CAP20P=0x00;  //R16[1:0]  0pF (00)  
		R828_Freq_Info.XTAL_CAP10P=0x00; 
		R828_Freq_Info.XTAL_CAP0P=0x00;
		R828_Freq_Info.IMR_MEM = 2;
	}
	else if( LO_freq>=310000 && LO_freq<450000)
	{
		R828_Freq_Info.OPEN_D=0x00; // high
		R828_Freq_Info.RF_MUX_PLOY = 0x41;  //R26[7:6]=1 (bypass)  R26[1:0]=1 (middle)
		R828_Freq_Info.TF_C=0x00;     //R27[7:0]  highest,highest
		R828_Freq_Info.XTAL_CAP20P=0x00;  //R16[1:0]  0pF (00)  
		R828_Freq_Info.XTAL_CAP10P=0x00; 
		R828_Freq_Info.XTAL_CAP0P=0x00;
		R828_Freq_Info.IMR_MEM = 2;
	}
	else if( LO_freq>=450000 && LO_freq<588000)
	{
		R828_Freq_Info.OPEN_D=0x00; // high
		R828_Freq_Info.RF_MUX_PLOY = 0x41;  //R26[7:6]=1 (bypass)  R26[1:0]=1 (middle)
		R828_Freq_Info.TF_C=0x00;     //R27[7:0]  highest,highest
		R828_Freq_Info.XTAL_CAP20P=0x00;  //R16[1:0]  0pF (00)  
		R828_Freq_Info.XTAL_CAP10P=0x00; 
		R828_Freq_Info.XTAL_CAP0P=0x00;
		R828_Freq_Info.IMR_MEM = 3;
	}
	else if( LO_freq>=588000 && LO_freq<650000)
	{
		R828_Freq_Info.OPEN_D=0x00; // high
		R828_Freq_Info.RF_MUX_PLOY = 0x40;  //R26[7:6]=1 (bypass)  R26[1:0]=0 (highest)
		R828_Freq_Info.TF_C=0x00;     //R27[7:0]  highest,highest
		R828_Freq_Info.XTAL_CAP20P=0x00;  //R16[1:0]  0pF (00)  
		R828_Freq_Info.XTAL_CAP10P=0x00; 
		R828_Freq_Info.XTAL_CAP0P=0x00;
		R828_Freq_Info.IMR_MEM = 3;
	}
	else
	{
		R828_Freq_Info.OPEN_D=0x00; // high
		R828_Freq_Info.RF_MUX_PLOY = 0x40;  //R26[7:6]=1 (bypass)  R26[1:0]=0 (highest)
		R828_Freq_Info.TF_C=0x00;     //R27[7:0]  highest,highest
		R828_Freq_Info.XTAL_CAP20P=0x00;  //R16[1:0]  0pF (00)  
		R828_Freq_Info.XTAL_CAP10P=0x00; 
		R828_Freq_Info.XTAL_CAP0P=0x00;
		R828_Freq_Info.IMR_MEM = 4;
	}

	return R828_Freq_Info;
}



SysFreq_Info_Type R828_SysFreq_Sel(R828_Standard_Type R828_Standard,UINT32 RF_freq)
{
	SysFreq_Info_Type R828_SysFreq_Info;
	
	switch(R828_Standard)
	{

	case DVB_T_6M:
	case DVB_T_7M:
	case DVB_T_7M_2:
	case DVB_T_8M:
		if( (RF_freq==506000) || (RF_freq==666000) || (RF_freq==818000) )
		{
			R828_SysFreq_Info.MIXER_TOP=0x14;	    // MIXER TOP:14 , TOP-1, low-discharge
			R828_SysFreq_Info.LNA_TOP=0xE5;		    // Detect BW 3, LNA TOP:4, PreDet Top:2
			R828_SysFreq_Info.CP_CUR=0x28;            //101, 0.2
			R828_SysFreq_Info.DIV_BUF_CUR=0x20; // 10, 200u
		}
		else
		{
		    R828_SysFreq_Info.MIXER_TOP=0x24;	    // MIXER TOP:13 , TOP-1, low-discharge
			R828_SysFreq_Info.LNA_TOP=0xE5;		// Detect BW 3, LNA TOP:4, PreDet Top:2
			R828_SysFreq_Info.CP_CUR=0x38;            // 111, auto
			R828_SysFreq_Info.DIV_BUF_CUR=0x30; // 11, 150u
		}
			R828_SysFreq_Info.LNA_VTH_L=0x53;		    // LNA VTH 0.84	,  VTL 0.64
			R828_SysFreq_Info.MIXER_VTH_L=0x75;	// MIXER VTH 1.04, VTL 0.84
			R828_SysFreq_Info.AIR_CABLE1_IN=0x00;
			R828_SysFreq_Info.CABLE2_IN=0x00;
			R828_SysFreq_Info.PRE_DECT=0x40;
			R828_SysFreq_Info.LNA_DISCHARGE=14;
			R828_SysFreq_Info.FILTER_CUR=0x40;         // 10, low
		break;


	case DVB_T2_6M:
	case DVB_T2_7M: 
	case DVB_T2_7M_2:
	case DVB_T2_8M:
		    R828_SysFreq_Info.MIXER_TOP=0x24;	    // MIXER TOP:13 , TOP-1, low-discharge
			R828_SysFreq_Info.LNA_TOP=0xE5;		    // Detect BW 3, LNA TOP:4, PreDet Top:2
			R828_SysFreq_Info.LNA_VTH_L=0x53;		// LNA VTH 0.84	,  VTL 0.64
			R828_SysFreq_Info.MIXER_VTH_L=0x75;	// MIXER VTH 1.04, VTL 0.84
			R828_SysFreq_Info.AIR_CABLE1_IN=0x00;
			R828_SysFreq_Info.CABLE2_IN=0x00;
			R828_SysFreq_Info.PRE_DECT=0x40;
			R828_SysFreq_Info.LNA_DISCHARGE=14;
			R828_SysFreq_Info.CP_CUR=0x38;            // 111, auto
			R828_SysFreq_Info.DIV_BUF_CUR=0x30; // 11, 150u
			R828_SysFreq_Info.FILTER_CUR=0x40;    // 10, low
		break;

	case ISDB_T:
			R828_SysFreq_Info.MIXER_TOP=0x24;	// MIXER TOP:13 , TOP-1, low-discharge
			R828_SysFreq_Info.LNA_TOP=0xE5;		// Detect BW 3, LNA TOP:4, PreDet Top:2
			R828_SysFreq_Info.LNA_VTH_L=0x75;		// LNA VTH 1.04	,  VTL 0.84
			R828_SysFreq_Info.MIXER_VTH_L=0x75;	// MIXER VTH 1.04, VTL 0.84
            R828_SysFreq_Info.AIR_CABLE1_IN=0x00;
			R828_SysFreq_Info.CABLE2_IN=0x00;
			R828_SysFreq_Info.PRE_DECT=0x40;
			R828_SysFreq_Info.LNA_DISCHARGE=14;
			R828_SysFreq_Info.CP_CUR=0x38;            // 111, auto
			R828_SysFreq_Info.DIV_BUF_CUR=0x30; // 11, 150u
			R828_SysFreq_Info.FILTER_CUR=0x40;    // 10, low
		break;

	default: //DVB-T 8M
			R828_SysFreq_Info.MIXER_TOP=0x24;	    // MIXER TOP:13 , TOP-1, low-discharge
			R828_SysFreq_Info.LNA_TOP=0xE5;		// Detect BW 3, LNA TOP:4, PreDet Top:2
			R828_SysFreq_Info.LNA_VTH_L=0x53;		// LNA VTH 0.84	,  VTL 0.64
			R828_SysFreq_Info.MIXER_VTH_L=0x75;	// MIXER VTH 1.04, VTL 0.84
            R828_SysFreq_Info.AIR_CABLE1_IN=0x00;
			R828_SysFreq_Info.CABLE2_IN=0x00;
			R828_SysFreq_Info.PRE_DECT=0x40;
			R828_SysFreq_Info.LNA_DISCHARGE=14;
			R828_SysFreq_Info.CP_CUR=0x38;            // 111, auto
			R828_SysFreq_Info.DIV_BUF_CUR=0x30; // 11, 150u
			R828_SysFreq_Info.FILTER_CUR=0x40;    // 10, low
		break;
	
	} //end switch


//DTV use Diplexer
#if(USE_DIPLEXER==TRUE)
if ((Rafael_Chip==R820C) || (Rafael_Chip==R820T) || (Rafael_Chip==R828S))
{
	// Air-in (>=DIP_FREQ) & cable-1(<DIP_FREQ) 
        if(RF_freq >= DIP_FREQ)
        {
            R828_SysFreq_Info.AIR_CABLE1_IN = 0x00; //air in, cable-1 off
            R828_SysFreq_Info.CABLE2_IN = 0x00;     //cable-2 off
        }
        else
        {
            R828_SysFreq_Info.AIR_CABLE1_IN = 0x60; //cable-1 in, air off
            R828_SysFreq_Info.CABLE2_IN = 0x00;     //cable-2 off
        }
}
#endif
	return R828_SysFreq_Info;
	
	}




R828_ErrCode R828_Xtal_Check(TUNER_MODULE *pTuner)
{

	UINT8 ArrayNum;

	ArrayNum = 27;
	for(ArrayNum=0;ArrayNum<27;ArrayNum++)
    {
          R828_Arry[ArrayNum] = R828_iniArry[ArrayNum];
    }

	//cap 30pF & Drive Low
	R828_I2C.RegAddr = 0x10;
	R828_Arry[11]    = (R828_Arry[11] & 0xF4) | 0x0B ;
	R828_I2C.Data    = R828_Arry[11];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
	    return RT_Fail;

	//set pll autotune = 128kHz
	R828_I2C.RegAddr = 0x1A;
	R828_Arry[21]    = R828_Arry[21] & 0xF3;
	R828_I2C.Data    = R828_Arry[21];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

	//set manual initial reg = 111111; 
	R828_I2C.RegAddr = 0x13;
	R828_Arry[14]    = (R828_Arry[14] & 0x80) | 0x7F;
	R828_I2C.Data    = R828_Arry[14];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

	//set auto
	R828_I2C.RegAddr = 0x13;
	R828_Arry[14]    = (R828_Arry[14] & 0xBF);
	R828_I2C.Data    = R828_Arry[14];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;
	
	R828_Delay_MS(pTuner, 5);

	R828_I2C_Len.RegAddr = 0x00;
	R828_I2C_Len.Len     = 3;
	if(I2C_Read_Len(pTuner, &R828_I2C_Len) != RT_Success)
		return RT_Fail;

	// if 30pF unlock, set to cap 20pF
#if (USE_16M_XTAL==TRUE)
	//VCO=2360MHz for 16M Xtal. VCO band 26
    if(((R828_I2C_Len.Data[2] & 0x40) == 0x00) || ((R828_I2C_Len.Data[2] & 0x3F) > 29) || ((R828_I2C_Len.Data[2] & 0x3F) < 23)) 
#else
	if(((R828_I2C_Len.Data[2] & 0x40) == 0x00) || ((R828_I2C_Len.Data[2] & 0x3F) == 0x3F)) 
#endif
	{
		//cap 20pF 
	    R828_I2C.RegAddr = 0x10;
	    R828_Arry[11]    = (R828_Arry[11] & 0xFC) | 0x02;
	    R828_I2C.Data    = R828_Arry[11];
	    if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		    return RT_Fail;

		R828_Delay_MS(pTuner, 5);
	
		R828_I2C_Len.RegAddr = 0x00;
		R828_I2C_Len.Len     = 3;
		if(I2C_Read_Len(pTuner, &R828_I2C_Len) != RT_Success)
		    return RT_Fail;

		// if 20pF unlock, set to cap 10pF
#if (USE_16M_XTAL==TRUE)
        if(((R828_I2C_Len.Data[2] & 0x40) == 0x00) || ((R828_I2C_Len.Data[2] & 0x3F) > 29) || ((R828_I2C_Len.Data[2] & 0x3F) < 23)) 
#else
	    if(((R828_I2C_Len.Data[2] & 0x40) == 0x00) || ((R828_I2C_Len.Data[2] & 0x3F) == 0x3F)) 
#endif
	   {
		   //cap 10pF 
	       R828_I2C.RegAddr = 0x10;
	       R828_Arry[11]    = (R828_Arry[11] & 0xFC) | 0x01;
	       R828_I2C.Data    = R828_Arry[11];
	       if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		       return RT_Fail;

		   R828_Delay_MS(pTuner, 5);
	
		   R828_I2C_Len.RegAddr = 0x00;
		   R828_I2C_Len.Len     = 3;
		   if(I2C_Read_Len(pTuner, &R828_I2C_Len) != RT_Success)
		       return RT_Fail;

		   // if 10pF unlock, set to cap 0pF
#if (USE_16M_XTAL==TRUE)
           if(((R828_I2C_Len.Data[2] & 0x40) == 0x00) || ((R828_I2C_Len.Data[2] & 0x3F) > 29) || ((R828_I2C_Len.Data[2] & 0x3F) < 23)) 
#else
	       if(((R828_I2C_Len.Data[2] & 0x40) == 0x00) || ((R828_I2C_Len.Data[2] & 0x3F) == 0x3F)) 
#endif 
	      {
		      //cap 0pF 
	          R828_I2C.RegAddr = 0x10;
	          R828_Arry[11]    = (R828_Arry[11] & 0xFC) | 0x00;
	          R828_I2C.Data    = R828_Arry[11];
	          if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		          return RT_Fail;
		
		      R828_Delay_MS(pTuner, 5);
	
		      R828_I2C_Len.RegAddr = 0x00;
		      R828_I2C_Len.Len     = 3;
		      if(I2C_Read_Len(pTuner, &R828_I2C_Len) != RT_Success)
		           return RT_Fail;
	
		      // if unlock, set to high drive
#if (USE_16M_XTAL==TRUE)
               if(((R828_I2C_Len.Data[2] & 0x40) == 0x00) || ((R828_I2C_Len.Data[2] & 0x3F) > 29) || ((R828_I2C_Len.Data[2] & 0x3F) < 23)) 
#else
		      if(((R828_I2C_Len.Data[2] & 0x40) == 0x00) || ((R828_I2C_Len.Data[2] & 0x3F) == 0x3F)) 
#endif 
			  {
				   //X'tal drive high
	               R828_I2C.RegAddr = 0x10;
	               R828_Arry[11]    = (R828_Arry[11] & 0xF7) ;
	               R828_I2C.Data    = R828_Arry[11];
	               if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
                          return RT_Fail;

				   //R828_Delay_MS(15);
				   R828_Delay_MS(pTuner, 20);
	
		           R828_I2C_Len.RegAddr = 0x00;
		           R828_I2C_Len.Len     = 3;
		           if(I2C_Read_Len(pTuner, &R828_I2C_Len) != RT_Success)
		                  return RT_Fail;

#if (USE_16M_XTAL==TRUE)
                   if(((R828_I2C_Len.Data[2] & 0x40) == 0x00) || ((R828_I2C_Len.Data[2] & 0x3F) > 29) || ((R828_I2C_Len.Data[2] & 0x3F) < 23)) 
#else
	               if(((R828_I2C_Len.Data[2] & 0x40) == 0x00) || ((R828_I2C_Len.Data[2] & 0x3F) == 0x3F)) 
#endif
				   {
			             return RT_Fail;
				   }
				   else //0p+high drive lock
				   {
						Xtal_cap_sel_tmp = XTAL_HIGH_CAP_0P;
				   }
			  }
		      else //0p lock
			  {
				   Xtal_cap_sel_tmp = XTAL_LOW_CAP_0P;
			  }
		   }
		   else //10p lock
		   {   
			   Xtal_cap_sel_tmp = XTAL_LOW_CAP_10P;
		   }
		}
		else //20p lock
		{
		   Xtal_cap_sel_tmp = XTAL_LOW_CAP_20P;
		}
	}
	else // 30p lock
	{
		Xtal_cap_sel_tmp = XTAL_LOW_CAP_30P;
	}

    return RT_Success;
}	
R828_ErrCode R828_Init(TUNER_MODULE *pTuner)
{
	R820T_EXTRA_MODULE *pExtra;
    UINT8 i;

	// Get tuner extra module.
	pExtra = &(pTuner->Extra.R820t);

    //write initial reg
	//if(R828_InitReg(pTuner) != RT_Success)         
	//	return RT_Fail;

	if(R828_IMR_done_flag==FALSE)
	{

	  //write initial reg
	  if(R828_InitReg(pTuner) != RT_Success)        
		  return RT_Fail;

	  //Do Xtal check
	  if((Rafael_Chip==R820T) || (Rafael_Chip==R828S) || (Rafael_Chip==R820C))
	  {
		  Xtal_cap_sel = XTAL_HIGH_CAP_0P;
	  }
	  else
	  {
          if(R828_Xtal_Check(pTuner) != RT_Success)        //1st
	          return RT_Fail;

		  Xtal_cap_sel = Xtal_cap_sel_tmp;
		  
		  if(R828_Xtal_Check(pTuner) != RT_Success)        //2nd
	          return RT_Fail;
		  
		  if(Xtal_cap_sel_tmp > Xtal_cap_sel)
		  {
			  Xtal_cap_sel = Xtal_cap_sel_tmp;
		  }

		  if(R828_Xtal_Check(pTuner) != RT_Success)        //3rd
	          return RT_Fail;
		  
		  if(Xtal_cap_sel_tmp > Xtal_cap_sel)
		  {
		      Xtal_cap_sel = Xtal_cap_sel_tmp;
		  }

	  }

	  //reset filter cal.
      for (i=0; i<STD_SIZE; i++)
	  {	  
		  R828_Fil_Cal_flag[i] = FALSE;
		  R828_Fil_Cal_code[i] = 0;
	  }

	  //start imr cal.
	  if(R828_InitReg(pTuner) != RT_Success)        //write initial reg before doing cal
	      return RT_Fail;

	  if(R828_IMR_Prepare(pTuner) != RT_Success)      
		return RT_Fail;

	  if(R828_IMR(pTuner, 3, TRUE) != RT_Success)       //Full K node 3
		return RT_Fail;

	  if(R828_IMR(pTuner, 1, FALSE) != RT_Success)
		return RT_Fail;

	  if(R828_IMR(pTuner, 0, FALSE) != RT_Success)
		return RT_Fail;

	  if(R828_IMR(pTuner, 2, FALSE) != RT_Success)
		return RT_Fail;

	  if(R828_IMR(pTuner, 4, FALSE) != RT_Success)
		return RT_Fail;

	  R828_IMR_done_flag = TRUE;
	}

	//write initial reg
	if(R828_InitReg(pTuner) != RT_Success)        
		return RT_Fail;

	return RT_Success;
}



R828_ErrCode R828_InitReg(TUNER_MODULE *pTuner)
{
	UINT8 InitArryCunt;
	UINT8 InitArryNum;

	InitArryCunt = 0;
	InitArryNum  = 27;

	//UINT32 LO_KHz      = 0;
	
	//Write Full Table
	R828_I2C_Len.RegAddr = 0x05;
	R828_I2C_Len.Len     = InitArryNum;
	for(InitArryCunt = 0;InitArryCunt < InitArryNum;InitArryCunt ++)
	{
		R828_I2C_Len.Data[InitArryCunt] = R828_iniArry[InitArryCunt];
	}
	if(I2C_Write_Len(pTuner, &R828_I2C_Len) != RT_Success)
		return RT_Fail;

	return RT_Success;
}


R828_ErrCode R828_IMR_Prepare(TUNER_MODULE *pTuner)

{
     UINT8 ArrayNum;

     ArrayNum=27;
	 
     for(ArrayNum=0;ArrayNum<27;ArrayNum++)
     {
           R828_Arry[ArrayNum] = R828_iniArry[ArrayNum];
     }
     //IMR Preparation    
     //lna off (air-in off)
     R828_I2C.RegAddr = 0x05;
     R828_Arry[0]     = R828_Arry[0]  | 0x20;
     R828_I2C.Data    = R828_Arry[0];
     if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
           return RT_Fail; 
     //mixer gain mode = manual
     R828_I2C.RegAddr = 0x07;
     R828_Arry[2]     = (R828_Arry[2] & 0xEF);
     R828_I2C.Data    = R828_Arry[2];
     if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
           return RT_Fail;
     //filter corner = lowest
     R828_I2C.RegAddr = 0x0A;
     R828_Arry[5]     = R828_Arry[5] | 0x0F;
     R828_I2C.Data    = R828_Arry[5];
     if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
           return RT_Fail;
     //filter bw=+2cap, hp=5M
     R828_I2C.RegAddr = 0x0B;
     R828_Arry[6]    = (R828_Arry[6] & 0x90) | 0x60;
     R828_I2C.Data    = R828_Arry[6];
     if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
           return RT_Fail;
     //adc=on, vga code mode, gain = 26.5dB  
     R828_I2C.RegAddr = 0x0C;
     R828_Arry[7]    = (R828_Arry[7] & 0x60) | 0x0B;
     R828_I2C.Data    = R828_Arry[7];
     if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
           return RT_Fail;
     //ring clk = on
     R828_I2C.RegAddr = 0x0F;
     R828_Arry[10]   &= 0xF7;
     R828_I2C.Data    = R828_Arry[10];
     if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
           return RT_Fail;
     //ring power = on
     R828_I2C.RegAddr = 0x18;
     R828_Arry[19]    = R828_Arry[19] | 0x10;
     R828_I2C.Data    = R828_Arry[19];
     if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
           return RT_Fail;
     //from ring = ring pll in
     R828_I2C.RegAddr = 0x1C;
     R828_Arry[23]    = R828_Arry[23] | 0x02;
     R828_I2C.Data    = R828_Arry[23];
     if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
           return RT_Fail;
     //sw_pdect = det3
     R828_I2C.RegAddr = 0x1E;
     R828_Arry[25]    = R828_Arry[25] | 0x80;
     R828_I2C.Data    = R828_Arry[25];
     if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
           return RT_Fail;
	// Set filt_3dB
	R828_Arry[1]  = R828_Arry[1] | 0x20;  
	R828_I2C.RegAddr  = 0x06;
	R828_I2C.Data     = R828_Arry[1];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

      return RT_Success;
}









R828_ErrCode R828_IMR(TUNER_MODULE *pTuner, UINT8 IMR_MEM, BOOL IM_Flag)
{

	UINT32 RingVCO;
	UINT32 RingFreq;
	UINT32 RingRef;
	UINT8 n_ring;
	UINT8 n;

	R828_SectType IMR_POINT;


	RingVCO = 0;
	RingFreq = 0;
	RingRef = 0;
	n_ring = 0;

	if (R828_Xtal>24000)
		RingRef = R828_Xtal /2;
	else
		RingRef = R828_Xtal;

	for(n=0;n<16;n++)
	{
		if((16+n)* 8 * RingRef >= 3100000) 
		{
		n_ring=n;
		break;
		}

		if(n==15)   //n_ring not found
		{
            //return RT_Fail;
			n_ring=n;
		}

	}

	R828_Arry[19] &= 0xF0;      //set ring[3:0]
	R828_Arry[19] |= n_ring;
	RingVCO = (16+n_ring)* 8 * RingRef;
	R828_Arry[19]&=0xDF;   //clear ring_se23	
	R828_Arry[20]&=0xFC;   //clear ring_seldiv	
	R828_Arry[26]&=0xFC;   //clear ring_att

	switch(IMR_MEM)
	{
	case 0:
		RingFreq = RingVCO/48;
		R828_Arry[19]|=0x20;  // ring_se23 = 1
		R828_Arry[20]|=0x03;  // ring_seldiv = 3		
		R828_Arry[26]|=0x02;  // ring_att 10
		break;
	case 1:
		RingFreq = RingVCO/16;
		R828_Arry[19]|=0x00;  // ring_se23 = 0
		R828_Arry[20]|=0x02;  // ring_seldiv = 2		
		R828_Arry[26]|=0x00;  // pw_ring 00
		break;
	case 2:
		RingFreq = RingVCO/8;
		R828_Arry[19]|=0x00;  // ring_se23 = 0
		R828_Arry[20]|=0x01;  // ring_seldiv = 1		
		R828_Arry[26]|=0x03;  // pw_ring 11
		break;
	case 3:
		RingFreq = RingVCO/6;
		R828_Arry[19]|=0x20;  // ring_se23 = 1
		R828_Arry[20]|=0x00;  // ring_seldiv = 0		
		R828_Arry[26]|=0x03;  // pw_ring 11
		break;
	case 4:
		RingFreq = RingVCO/4;
		R828_Arry[19]|=0x00;  // ring_se23 = 0
		R828_Arry[20]|=0x00;  // ring_seldiv = 0		
		R828_Arry[26]|=0x01;  // pw_ring 01
		break;
	default:
		RingFreq = RingVCO/4;
		R828_Arry[19]|=0x00;  // ring_se23 = 0
		R828_Arry[20]|=0x00;  // ring_seldiv = 0		
		R828_Arry[26]|=0x01;  // pw_ring 01
		break;
	}


	//write pw_ring,n_ring,ringdiv2 to I2C

	//------------n_ring,ring_se23----------//
	R828_I2C.RegAddr = 0x18;
	R828_I2C.Data    = R828_Arry[19];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;
	//------------ring_sediv----------------//
	R828_I2C.RegAddr = 0x19;
	R828_I2C.Data    = R828_Arry[20];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;
	//------------pw_ring-------------------//
	R828_I2C.RegAddr = 0x1f;
	R828_I2C.Data    = R828_Arry[26];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;
	
	//Must do before PLL() 
	if(R828_MUX(pTuner, RingFreq - 5300) != RT_Success)				//MUX input freq ~ RF_in Freq
		return RT_Fail;

	if(R828_PLL(pTuner, (RingFreq - 5300), STD_SIZE) != RT_Success)                //set pll freq = ring freq - 6M
	    return RT_Fail;

	if(IM_Flag == TRUE)
	{
	if(R828_IQ(pTuner, &IMR_POINT) != RT_Success)
		return RT_Fail;
	}
	else
	{
		IMR_POINT.Gain_X = IMR_Data[3].Gain_X;
		IMR_POINT.Phase_Y = IMR_Data[3].Phase_Y;
		IMR_POINT.Value = IMR_Data[3].Value;
		if(R828_F_IMR(pTuner, &IMR_POINT) != RT_Success)
			return RT_Fail;
	}

	//Save IMR Value
	switch(IMR_MEM)
	{
	case 0:
		IMR_Data[0].Gain_X  = IMR_POINT.Gain_X;
		IMR_Data[0].Phase_Y = IMR_POINT.Phase_Y;
		IMR_Data[0].Value   = IMR_POINT.Value;
		break;
	case 1:
		IMR_Data[1].Gain_X  = IMR_POINT.Gain_X;
		IMR_Data[1].Phase_Y = IMR_POINT.Phase_Y;
		IMR_Data[1].Value   = IMR_POINT.Value;
		break;
	case 2:
		IMR_Data[2].Gain_X  = IMR_POINT.Gain_X;
		IMR_Data[2].Phase_Y = IMR_POINT.Phase_Y;
		IMR_Data[2].Value   = IMR_POINT.Value;
		break;
	case 3:
		IMR_Data[3].Gain_X  = IMR_POINT.Gain_X;
		IMR_Data[3].Phase_Y = IMR_POINT.Phase_Y;
		IMR_Data[3].Value   = IMR_POINT.Value;
		break;
	case 4:
		IMR_Data[4].Gain_X  = IMR_POINT.Gain_X;
		IMR_Data[4].Phase_Y = IMR_POINT.Phase_Y;
		IMR_Data[4].Value   = IMR_POINT.Value;
		break;
    default:
		IMR_Data[4].Gain_X  = IMR_POINT.Gain_X;
		IMR_Data[4].Phase_Y = IMR_POINT.Phase_Y;
		IMR_Data[4].Value   = IMR_POINT.Value;
		break;
	}
	return RT_Success;
}






R828_ErrCode R828_PLL(TUNER_MODULE *pTuner, UINT32 LO_Freq, R828_Standard_Type R828_Standard)
{

	R820T_EXTRA_MODULE *pExtra;
	
	UINT8  MixDiv;
	UINT8  DivBuf;
	UINT8  Ni;
	UINT8  Si;
	UINT8  DivNum;
	UINT8  Nint;
	UINT32 VCO_Min;
	UINT32 VCO_Max;
	UINT32 VCO_Freq;
	UINT32 PLL_Ref;		//Max 24000 (kHz)
	UINT32 VCO_Fra;		//VCO contribution by SDM (kHz)
	UINT16 Nsdm;
	UINT16 SDM;
	UINT16 SDM16to9;
	UINT16 SDM8to1;
	//UINT8  Judge    = 0;
	UINT8  VCO_fine_tune;


	MixDiv   = 2;
	DivBuf   = 0;
	Ni       = 0;
	Si       = 0;
	DivNum   = 0;
	Nint     = 0;
	VCO_Min  = 1770000;
	VCO_Max  = VCO_Min*2;
	VCO_Freq = 0;
	PLL_Ref	= 0;		//Max 24000 (kHz)
	VCO_Fra	= 0;		//VCO contribution by SDM (kHz)
	Nsdm		= 2;
	SDM		= 0;
	SDM16to9	= 0;
	SDM8to1  = 0;
	//UINT8  Judge    = 0;
	VCO_fine_tune = 0;	
	
	// Get tuner extra module.
	pExtra = &(pTuner->Extra.R820t);
	

if ((pExtra->Rafael_Chip==R620D) || (pExtra->Rafael_Chip==R828D) || (pExtra->Rafael_Chip==R828))  //X'tal can't not exceed 20MHz for ATV
{
	if(R828_Standard <= SECAM_L1)	  //ref set refdiv2, reffreq = Xtal/2 on ATV application
	{
       R828_Arry[11] |= 0x10; //b4=1
	   PLL_Ref = R828_Xtal /2;
	}
	else //DTV, FilCal, IMR
	{
       R828_Arry[11] &= 0xEF;
	   PLL_Ref = R828_Xtal;
	}
}
else
{
	if(R828_Xtal > 24000)	 
	{
       R828_Arry[11] |= 0x10; //b4=1
	   PLL_Ref = R828_Xtal /2;
	}
	else
	{
       R828_Arry[11] &= 0xEF;
	   PLL_Ref = R828_Xtal; 
	}
}

	R828_I2C.RegAddr = 0x10;
	R828_I2C.Data = R828_Arry[11];
    if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
	   return RT_Fail;

	//set pll autotune = 128kHz
	R828_I2C.RegAddr = 0x1A;
	R828_Arry[21]    = R828_Arry[21] & 0xF3;
	R828_I2C.Data    = R828_Arry[21];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

	//Set VCO current = 100
	R828_I2C.RegAddr = 0x12;
	R828_Arry[13]    = (R828_Arry[13] & 0x1F) | 0x80; 
	R828_I2C.Data    = R828_Arry[13];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
	    return RT_Fail;

	//Divider
	while(MixDiv <= 64)
	{
		if(((LO_Freq * MixDiv) >= VCO_Min) && ((LO_Freq * MixDiv) < VCO_Max))
		{
			DivBuf = MixDiv;
			while(DivBuf > 2)
			{
				DivBuf = DivBuf >> 1;
				DivNum ++;
			}			
			break;
		}
		MixDiv = MixDiv << 1;
	}

	R828_I2C_Len.RegAddr = 0x00;
	R828_I2C_Len.Len     = 5;
	if(I2C_Read_Len(pTuner, &R828_I2C_Len) != RT_Success)
		return RT_Fail;	

   VCO_fine_tune = (R828_I2C_Len.Data[4] & 0x30)>>4;

	if(VCO_fine_tune > VCO_pwr_ref)
		DivNum = DivNum - 1;
	else if(VCO_fine_tune < VCO_pwr_ref)
	    DivNum = DivNum + 1; 
	
	R828_I2C.RegAddr = 0x10;
	R828_Arry[11] &= 0x1F;
	R828_Arry[11] |= (DivNum << 5);
	R828_I2C.Data = R828_Arry[11];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

	VCO_Freq = LO_Freq * MixDiv;
	Nint     = (UINT8) (VCO_Freq / 2 / PLL_Ref);
	VCO_Fra  = (UINT16) (VCO_Freq - 2 * PLL_Ref * Nint);

	//boundary spur prevention
	if (VCO_Fra < PLL_Ref/64)           //2*PLL_Ref/128
		VCO_Fra = 0;
	else if (VCO_Fra > PLL_Ref*127/64)  //2*PLL_Ref*127/128
	{
		VCO_Fra = 0;
		Nint ++;
	}
	else if((VCO_Fra > PLL_Ref*127/128) && (VCO_Fra < PLL_Ref)) //> 2*PLL_Ref*127/256,  < 2*PLL_Ref*128/256
		VCO_Fra = PLL_Ref*127/128;      // VCO_Fra = 2*PLL_Ref*127/256
	else if((VCO_Fra > PLL_Ref) && (VCO_Fra < PLL_Ref*129/128)) //> 2*PLL_Ref*128/256,  < 2*PLL_Ref*129/256
		VCO_Fra = PLL_Ref*129/128;      // VCO_Fra = 2*PLL_Ref*129/256
	else
		VCO_Fra = VCO_Fra;

	//N & S	
	Ni       = (Nint - 13) / 4;
	Si       = Nint - 4 *Ni - 13;
	R828_I2C.RegAddr = 0x14;
	R828_Arry[15]  = 0x00;
	R828_Arry[15] |= (Ni + (Si << 6));
	R828_I2C.Data = R828_Arry[15];
	
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
	  return RT_Fail;
         	
	//pw_sdm	
	R828_I2C.RegAddr = 0x12;
	R828_Arry[13] &= 0xF7;
	if(VCO_Fra == 0)
		R828_Arry[13] |= 0x08;
	R828_I2C.Data = R828_Arry[13];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

	//SDM calculator
	while(VCO_Fra > 1)
	{			
		if (VCO_Fra > (2*PLL_Ref / Nsdm))
		{		
			SDM = SDM + 32768 / (Nsdm/2);
			VCO_Fra = VCO_Fra - 2*PLL_Ref / Nsdm;
			if (Nsdm >= 0x8000)
				break;
		}
		Nsdm = Nsdm << 1;
	}

	SDM16to9 = SDM >> 8;
	SDM8to1 =  SDM - (SDM16to9 << 8);

	R828_I2C.RegAddr = 0x16;
	R828_Arry[17]    = (UINT8) SDM16to9;
	R828_I2C.Data    = R828_Arry[17];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;
	R828_I2C.RegAddr = 0x15;
	R828_Arry[16]    = (UINT8) SDM8to1;
	R828_I2C.Data    = R828_Arry[16];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;


//	R828_Delay_MS(10);
	
if ((pExtra->Rafael_Chip==R620D) || (pExtra->Rafael_Chip==R828D) || (pExtra->Rafael_Chip==R828))
{
	if(R828_Standard <= SECAM_L1)
	    R828_Delay_MS(pTuner, 20);
	else
	    R828_Delay_MS(pTuner, 10);
}
else
	 R828_Delay_MS(pTuner, 10);


	//check PLL lock status
	R828_I2C_Len.RegAddr = 0x00;
	R828_I2C_Len.Len     = 3;
	if(I2C_Read_Len(pTuner, &R828_I2C_Len) != RT_Success)
		return RT_Fail;

	if( (R828_I2C_Len.Data[2] & 0x40) == 0x00 ) 
	{
		R828_I2C.RegAddr = 0x12;
	    R828_Arry[13]    = (R828_Arry[13] & 0x1F) | 0x60;  //increase VCO current
        R828_I2C.Data    = R828_Arry[13];
	    if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		     return RT_Fail;
	}

	//set pll autotune = 8kHz
	R828_I2C.RegAddr = 0x1A;
	R828_Arry[21]    = R828_Arry[21] | 0x08;
	R828_I2C.Data    = R828_Arry[21];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

	return RT_Success;

}





R828_ErrCode R828_MUX(TUNER_MODULE *pTuner, UINT32 RF_KHz)
{	
	UINT8 RT_Reg08;
	UINT8 RT_Reg09;

	RT_Reg08   = 0;
	RT_Reg09   = 0;

	//Freq_Info_Type Freq_Info1;
	Freq_Info1 = R828_Freq_Sel(RF_KHz);


	// Open Drain
	R828_I2C.RegAddr = 0x17;
	R828_Arry[18] = (R828_Arry[18] & 0xF7) | Freq_Info1.OPEN_D;
	R828_I2C.Data = R828_Arry[18];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;
	

	// RF_MUX,Polymux 
	R828_I2C.RegAddr = 0x1A;
	R828_Arry[21] = (R828_Arry[21] & 0x3C) | Freq_Info1.RF_MUX_PLOY;
	R828_I2C.Data = R828_Arry[21];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

	// TF BAND
	R828_I2C.RegAddr = 0x1B;
	R828_Arry[22] &= 0x00;
	R828_Arry[22] |= Freq_Info1.TF_C;	
	R828_I2C.Data = R828_Arry[22];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;


	// XTAL CAP & Drive
	R828_I2C.RegAddr = 0x10;
	R828_Arry[11] &= 0xF4;
	switch(Xtal_cap_sel)
	{
	   case XTAL_LOW_CAP_30P:
	   case XTAL_LOW_CAP_20P:
		   R828_Arry[11] = R828_Arry[11] | Freq_Info1.XTAL_CAP20P | 0x08;
		   break;

	   case XTAL_LOW_CAP_10P:
           R828_Arry[11] = R828_Arry[11] | Freq_Info1.XTAL_CAP10P | 0x08;
		   break;

	   case XTAL_LOW_CAP_0P:
		   R828_Arry[11] = R828_Arry[11] | Freq_Info1.XTAL_CAP0P | 0x08;
		   break;
	
	   case XTAL_HIGH_CAP_0P:
		   R828_Arry[11] = R828_Arry[11] | Freq_Info1.XTAL_CAP0P | 0x00;
		   break;

	   default:
	       R828_Arry[11] = R828_Arry[11] | Freq_Info1.XTAL_CAP0P | 0x08;
		   break;
	}
	R828_I2C.Data    = R828_Arry[11];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;


	//Set_IMR
	if(R828_IMR_done_flag == TRUE)
	{
		RT_Reg08 = IMR_Data[Freq_Info1.IMR_MEM].Gain_X & 0x3F;
		RT_Reg09 = IMR_Data[Freq_Info1.IMR_MEM].Phase_Y & 0x3F;
	}
	else
	{
		RT_Reg08 = 0;
	    RT_Reg09 = 0;
	}

	R828_I2C.RegAddr = 0x08;
	R828_Arry[3] = R828_iniArry[3] & 0xC0;                   
	R828_Arry[3] = R828_Arry[3] | RT_Reg08;
	R828_I2C.Data = R828_Arry[3];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

	R828_I2C.RegAddr = 0x09;
	R828_Arry[4] = R828_iniArry[4] & 0xC0;
	R828_Arry[4] = R828_Arry[4] | RT_Reg09;
	R828_I2C.Data =R828_Arry[4]  ;
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

	return RT_Success;
}




R828_ErrCode R828_IQ(TUNER_MODULE *pTuner, R828_SectType* IQ_Pont)
{
	R828_SectType Compare_IQ[3];
//	R828_SectType CompareTemp;
//	UINT8 IQ_Cunt  = 0;
	UINT8 VGA_Cunt;
	UINT16 VGA_Read;
	UINT8  X_Direction;  // 1:X, 0:Y

	VGA_Cunt = 0;
	VGA_Read = 0;

	// increase VGA power to let image significant
	for(VGA_Cunt = 12;VGA_Cunt < 16;VGA_Cunt ++)
	{
		R828_I2C.RegAddr = 0x0C;
		R828_I2C.Data    = (R828_Arry[7] & 0xF0) + VGA_Cunt;  
		if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
			return RT_Fail;

		R828_Delay_MS(pTuner, 10); //
		
		if(R828_Muti_Read(pTuner, 0x01, &VGA_Read) != RT_Success)
			return RT_Fail;

		if(VGA_Read > 40*4)
			break;
	}

	//initial 0x08, 0x09
	//Compare_IQ[0].Gain_X  = 0x40; //should be 0xC0 in R828, Jason
	//Compare_IQ[0].Phase_Y = 0x40; //should be 0x40 in R828
	Compare_IQ[0].Gain_X  = R828_iniArry[3] & 0xC0; // Jason modified, clear b[5], b[4:0]
	Compare_IQ[0].Phase_Y = R828_iniArry[4] & 0xC0; //

	//while(IQ_Cunt < 3)
	//{
	    // Determine X or Y
	    if(R828_IMR_Cross(pTuner, &Compare_IQ[0], &X_Direction) != RT_Success)
			return RT_Fail;

		//if(X_Direction==1)
		//{
		//    if(R828_IQ_Tree(Compare_IQ[0].Phase_Y, Compare_IQ[0].Gain_X, 0x09, &Compare_IQ[0]) != RT_Success) //X
		//	  return RT_Fail;
		//}
		//else
		//{
		//   if(R828_IQ_Tree(Compare_IQ[0].Gain_X, Compare_IQ[0].Phase_Y, 0x08, &Compare_IQ[0]) != RT_Success) //Y
		//	return RT_Fail;
		//}

		/*
		//--- X direction ---//
	    //X: 3 points
		if(R828_IQ_Tree(Compare_IQ[0].Phase_Y, Compare_IQ[0].Gain_X, 0x09, &Compare_IQ[0]) != RT_Success) //
			return RT_Fail;
        
		//compare and find min of 3 points. determine I/Q direction
		if(R828_CompreCor(&Compare_IQ[0]) != RT_Success)
			return RT_Fail;

		//increase step to find min value of this direction
		if(R828_CompreStep(&Compare_IQ[0], 0x08) != RT_Success)
			return RT_Fail;
		*/

		if(X_Direction==1)
		{
			//compare and find min of 3 points. determine I/Q direction
		    if(R828_CompreCor(&Compare_IQ[0]) != RT_Success)
			  return RT_Fail;

		    //increase step to find min value of this direction
		    if(R828_CompreStep(pTuner, &Compare_IQ[0], 0x08) != RT_Success)  //X
			  return RT_Fail;
		}
		else
		{
		   //compare and find min of 3 points. determine I/Q direction
		   if(R828_CompreCor(&Compare_IQ[0]) != RT_Success)
		   	 return RT_Fail;

		   //increase step to find min value of this direction
		   if(R828_CompreStep(pTuner, &Compare_IQ[0], 0x09) != RT_Success)  //Y
			 return RT_Fail;
		}
		/*
		//--- Y direction ---//
		//Y: 3 points
		if(R828_IQ_Tree(Compare_IQ[0].Gain_X, Compare_IQ[0].Phase_Y, 0x08, &Compare_IQ[0]) != RT_Success) //
			return RT_Fail;

		//compare and find min of 3 points. determine I/Q direction
		if(R828_CompreCor(&Compare_IQ[0]) != RT_Success)
			return RT_Fail;

		//increase step to find min value of this direction
		if(R828_CompreStep(&Compare_IQ[0], 0x09) != RT_Success)
			return RT_Fail;
        */

		//Another direction
		if(X_Direction==1)
		{	    
           if(R828_IQ_Tree(pTuner, Compare_IQ[0].Gain_X, Compare_IQ[0].Phase_Y, 0x08, &Compare_IQ[0]) != RT_Success) //Y
		     return RT_Fail;

		   //compare and find min of 3 points. determine I/Q direction
		   if(R828_CompreCor(&Compare_IQ[0]) != RT_Success)
		   	 return RT_Fail;

		   //increase step to find min value of this direction
		   if(R828_CompreStep(pTuner, &Compare_IQ[0], 0x09) != RT_Success)  //Y
			 return RT_Fail;
		}
		else
		{
		   if(R828_IQ_Tree(pTuner, Compare_IQ[0].Phase_Y, Compare_IQ[0].Gain_X, 0x09, &Compare_IQ[0]) != RT_Success) //X
		     return RT_Fail;
        
		   //compare and find min of 3 points. determine I/Q direction
		   if(R828_CompreCor(&Compare_IQ[0]) != RT_Success)
		     return RT_Fail;

	       //increase step to find min value of this direction
		   if(R828_CompreStep(pTuner, &Compare_IQ[0], 0x08) != RT_Success) //X
		     return RT_Fail;
		}
		//CompareTemp = Compare_IQ[0];

		//--- Check 3 points again---//
		if(X_Direction==1)
		{
		    if(R828_IQ_Tree(pTuner, Compare_IQ[0].Phase_Y, Compare_IQ[0].Gain_X, 0x09, &Compare_IQ[0]) != RT_Success) //X
			  return RT_Fail;
		}
		else
		{
		   if(R828_IQ_Tree(pTuner, Compare_IQ[0].Gain_X, Compare_IQ[0].Phase_Y, 0x08, &Compare_IQ[0]) != RT_Success) //Y
			return RT_Fail;
		}

		//if(R828_IQ_Tree(Compare_IQ[0].Phase_Y, Compare_IQ[0].Gain_X, 0x09, &Compare_IQ[0]) != RT_Success) //
		//	return RT_Fail;

		if(R828_CompreCor(&Compare_IQ[0]) != RT_Success)
			return RT_Fail;

		//if((CompareTemp.Gain_X == Compare_IQ[0].Gain_X) && (CompareTemp.Phase_Y == Compare_IQ[0].Phase_Y))//Ben Check
		//	break;
		
		//IQ_Cunt ++;
	//}
	//if(IQ_Cunt ==  3)
	//	return RT_Fail;

	//Section-4 Check 
    /*
	CompareTemp = Compare_IQ[0];
	for(IQ_Cunt = 0;IQ_Cunt < 5;IQ_Cunt ++)
	{
		if(R828_Section(&Compare_IQ[0]) != RT_Success)
			return RT_Fail;

		if((CompareTemp.Gain_X == Compare_IQ[0].Gain_X) && (CompareTemp.Phase_Y == Compare_IQ[0].Phase_Y))
			break;
	}
	*/

    //Section-9 check
    //if(R828_F_IMR(&Compare_IQ[0]) != RT_Success)
	if(R828_Section(pTuner, &Compare_IQ[0]) != RT_Success)
			return RT_Fail;

	*IQ_Pont = Compare_IQ[0];

	//reset gain/phase control setting
	R828_I2C.RegAddr = 0x08;
	R828_I2C.Data    = R828_iniArry[3] & 0xC0; //Jason
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

	R828_I2C.RegAddr = 0x09;
	R828_I2C.Data    = R828_iniArry[4] & 0xC0;
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

	return RT_Success;
}



//--------------------------------------------------------------------------------------------
// Purpose: record IMC results by input gain/phase location
//          then adjust gain or phase positive 1 step and negtive 1 step, both record results
// input: FixPot: phase or gain
//        FlucPot phase or gain
//        PotReg: 0x08 or 0x09
//        CompareTree: 3 IMR trace and results
// output: TREU or FALSE
//--------------------------------------------------------------------------------------------
R828_ErrCode R828_IQ_Tree(TUNER_MODULE *pTuner, UINT8 FixPot, UINT8 FlucPot, UINT8 PotReg, R828_SectType* CompareTree)
{
	UINT8 TreeCunt;
	UINT8 TreeTimes;
	UINT8 TempPot;
	UINT8 PntReg;

	TreeCunt  = 0;
	TreeTimes = 3;
	TempPot   = 0;
	PntReg    = 0;
		
	if(PotReg == 0x08)
		PntReg = 0x09; //phase control
	else
		PntReg = 0x08; //gain control

	for(TreeCunt = 0;TreeCunt < TreeTimes;TreeCunt ++)
	{
		R828_I2C.RegAddr = PotReg;
		R828_I2C.Data    = FixPot;
		if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
			return RT_Fail;

		R828_I2C.RegAddr = PntReg;
		R828_I2C.Data    = FlucPot;
		if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
			return RT_Fail;

		if(R828_Muti_Read(pTuner, 0x01, &CompareTree[TreeCunt].Value) != RT_Success)
			return RT_Fail;
	
		if(PotReg == 0x08)
		{
			CompareTree[TreeCunt].Gain_X  = FixPot;
			CompareTree[TreeCunt].Phase_Y = FlucPot;
		}
		else
		{
			CompareTree[TreeCunt].Phase_Y  = FixPot;
			CompareTree[TreeCunt].Gain_X = FlucPot;
		}
		
		if(TreeCunt == 0)   //try right-side point
			FlucPot ++; 
		else if(TreeCunt == 1) //try left-side point
		{
			if((FlucPot & 0x1F) < 0x02) //if absolute location is 1, change I/Q direction
			{
				TempPot = 2 - (FlucPot & 0x1F);
				if(FlucPot & 0x20) //b[5]:I/Q selection. 0:Q-path, 1:I-path
				{
					FlucPot &= 0xC0;
					FlucPot |= TempPot;
				}
				else
				{
					FlucPot |= (0x20 | TempPot);
				}
			}
			else
				FlucPot -= 2;  
		}
	}

	return RT_Success;
}




//-----------------------------------------------------------------------------------/ 
// Purpose: compare IMC result aray [0][1][2], find min value and store to CorArry[0]
// input: CorArry: three IMR data array
// output: TRUE or FALSE
//-----------------------------------------------------------------------------------/
R828_ErrCode R828_CompreCor(R828_SectType* CorArry)
{
	UINT8 CompCunt;
	R828_SectType CorTemp;

	CompCunt = 0;

	for(CompCunt = 3;CompCunt > 0;CompCunt --)
	{
		if(CorArry[0].Value > CorArry[CompCunt - 1].Value) //compare IMC result [0][1][2], find min value
		{
			CorTemp = CorArry[0];
			CorArry[0] = CorArry[CompCunt - 1];
			CorArry[CompCunt - 1] = CorTemp;
		}
	}

	return RT_Success;
}


//-------------------------------------------------------------------------------------//
// Purpose: if (Gain<9 or Phase<9), Gain+1 or Phase+1 and compare with min value
//          new < min => update to min and continue
//          new > min => Exit
// input: StepArry: three IMR data array
//        Pace: gain or phase register
// output: TRUE or FALSE 
//-------------------------------------------------------------------------------------//
R828_ErrCode R828_CompreStep(TUNER_MODULE *pTuner, R828_SectType* StepArry, UINT8 Pace)
{
	//UINT8 StepCunt = 0;
	R828_SectType StepTemp;
	
	//min value already saved in StepArry[0]
	StepTemp.Phase_Y = StepArry[0].Phase_Y;
	StepTemp.Gain_X  = StepArry[0].Gain_X;

	while(((StepTemp.Gain_X & 0x1F) < IMR_TRIAL) && ((StepTemp.Phase_Y & 0x1F) < IMR_TRIAL))  //5->10
	{
		if(Pace == 0x08)
			StepTemp.Gain_X ++;
		else
			StepTemp.Phase_Y ++;
	
		R828_I2C.RegAddr = 0x08;
		R828_I2C.Data    = StepTemp.Gain_X ;
		if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
			return RT_Fail;

		R828_I2C.RegAddr = 0x09;
		R828_I2C.Data    = StepTemp.Phase_Y;
		if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
			return RT_Fail;

		if(R828_Muti_Read(pTuner, 0x01, &StepTemp.Value) != RT_Success)
			return RT_Fail;

		if(StepTemp.Value <= StepArry[0].Value)
		{
			StepArry[0].Gain_X  = StepTemp.Gain_X;
			StepArry[0].Phase_Y = StepTemp.Phase_Y;
			StepArry[0].Value   = StepTemp.Value;
		}
		else
		{
			break;		
		}
		
	} //end of while()
	
	return RT_Success;
}


//-----------------------------------------------------------------------------------/ 
// Purpose: read multiple IMC results for stability
// input: IMR_Reg: IMC result address
//        IMR_Result_Data: result 
// output: TRUE or FALSE
//-----------------------------------------------------------------------------------/
R828_ErrCode R828_Muti_Read(TUNER_MODULE *pTuner, UINT8 IMR_Reg, UINT16* IMR_Result_Data)  //jason modified
{
	UINT8 ReadCunt;
	UINT16 ReadAmount;
	UINT8 ReadMax;
	UINT8 ReadMin;
	UINT8 ReadData;

	ReadCunt     = 0;
	ReadAmount  = 0;
	ReadMax = 0;
	ReadMin = 255;
	ReadData = 0;

    R828_Delay_MS(pTuner, 5);
	
	for(ReadCunt = 0;ReadCunt < 6;ReadCunt ++)
	{
		R828_I2C_Len.RegAddr = 0x00;
		R828_I2C_Len.Len     = IMR_Reg + 1;  //IMR_Reg = 0x01
		if(I2C_Read_Len(pTuner, &R828_I2C_Len) != RT_Success)
			return RT_Fail;

		ReadData = R828_I2C_Len.Data[1];
		
		ReadAmount = ReadAmount + (UINT16)ReadData;
		
		if(ReadData < ReadMin)
			ReadMin = ReadData;
		
        if(ReadData > ReadMax)
			ReadMax = ReadData;
	}
	*IMR_Result_Data = ReadAmount - (UINT16)ReadMax - (UINT16)ReadMin;

	return RT_Success;
}


R828_ErrCode R828_Section(TUNER_MODULE *pTuner, R828_SectType* IQ_Pont)
{
	R828_SectType Compare_IQ[3];
	R828_SectType Compare_Bet[3];

	//Try X-1 column and save min result to Compare_Bet[0]
	if((IQ_Pont->Gain_X & 0x1F) == 0x00)
	{
		/*
		if((IQ_Pont->Gain_X & 0xE0) == 0x40) //bug => only compare b[5],     
			Compare_IQ[0].Gain_X = 0x61; // Gain=1, I-path //Jason
		else
			Compare_IQ[0].Gain_X = 0x41; // Gain=1, Q-path
        */
		Compare_IQ[0].Gain_X = ((IQ_Pont->Gain_X) & 0xDF) + 1;  //Q-path, Gain=1
	}
	else
		Compare_IQ[0].Gain_X  = IQ_Pont->Gain_X - 1;  //left point
	Compare_IQ[0].Phase_Y = IQ_Pont->Phase_Y;

	if(R828_IQ_Tree(pTuner, Compare_IQ[0].Gain_X, Compare_IQ[0].Phase_Y, 0x08, &Compare_IQ[0]) != RT_Success)  // y-direction
		return RT_Fail;

	if(R828_CompreCor(&Compare_IQ[0]) != RT_Success)
		return RT_Fail;

	Compare_Bet[0].Gain_X = Compare_IQ[0].Gain_X;
	Compare_Bet[0].Phase_Y = Compare_IQ[0].Phase_Y;
	Compare_Bet[0].Value = Compare_IQ[0].Value;

	//Try X column and save min result to Compare_Bet[1]
	Compare_IQ[0].Gain_X = IQ_Pont->Gain_X;
	Compare_IQ[0].Phase_Y = IQ_Pont->Phase_Y;

	if(R828_IQ_Tree(pTuner, Compare_IQ[0].Gain_X, Compare_IQ[0].Phase_Y, 0x08, &Compare_IQ[0]) != RT_Success)
		return RT_Fail;

	if(R828_CompreCor(&Compare_IQ[0]) != RT_Success)
		return RT_Fail;

	Compare_Bet[1].Gain_X = Compare_IQ[0].Gain_X;
	Compare_Bet[1].Phase_Y = Compare_IQ[0].Phase_Y;
	Compare_Bet[1].Value = Compare_IQ[0].Value;

	//Try X+1 column and save min result to Compare_Bet[2]
	if((IQ_Pont->Gain_X & 0x1F) == 0x00)		
		Compare_IQ[0].Gain_X = ((IQ_Pont->Gain_X) | 0x20) + 1;  //I-path, Gain=1
	else
	    Compare_IQ[0].Gain_X = IQ_Pont->Gain_X + 1;
	Compare_IQ[0].Phase_Y = IQ_Pont->Phase_Y;

	if(R828_IQ_Tree(pTuner, Compare_IQ[0].Gain_X, Compare_IQ[0].Phase_Y, 0x08, &Compare_IQ[0]) != RT_Success)
		return RT_Fail;

	if(R828_CompreCor(&Compare_IQ[0]) != RT_Success)
		return RT_Fail;

	Compare_Bet[2].Gain_X = Compare_IQ[0].Gain_X;
	Compare_Bet[2].Phase_Y = Compare_IQ[0].Phase_Y;
	Compare_Bet[2].Value = Compare_IQ[0].Value;

	if(R828_CompreCor(&Compare_Bet[0]) != RT_Success)
		return RT_Fail;

	*IQ_Pont = Compare_Bet[0];
	
	return RT_Success;
}


R828_ErrCode R828_IMR_Cross(TUNER_MODULE *pTuner, R828_SectType* IQ_Pont, UINT8* X_Direct)
{

	R828_SectType Compare_Cross[5]; //(0,0)(0,Q-1)(0,I-1)(Q-1,0)(I-1,0)
	R828_SectType Compare_Temp;
	UINT8 CrossCount;
    UINT8 Reg08;
	UINT8 Reg09;

	CrossCount = 0;
    Reg08 = R828_iniArry[3] & 0xC0;
	Reg09 = R828_iniArry[4] & 0xC0;	

	//memset(&Compare_Temp,0, sizeof(R828_SectType));
	Compare_Temp.Gain_X = 0;
	Compare_Temp.Phase_Y = 0;
	Compare_Temp.Value = 0;	

	Compare_Temp.Value = 255;

	for(CrossCount=0; CrossCount<5; CrossCount++)
	{

		if(CrossCount==0)
		{
		  Compare_Cross[CrossCount].Gain_X = Reg08;
		  Compare_Cross[CrossCount].Phase_Y = Reg09;
		}
		else if(CrossCount==1)
		{
		  Compare_Cross[CrossCount].Gain_X = Reg08;       //0
		  Compare_Cross[CrossCount].Phase_Y = Reg09 + 1;  //Q-1
		}
		else if(CrossCount==2)
		{
		  Compare_Cross[CrossCount].Gain_X = Reg08;               //0
		  Compare_Cross[CrossCount].Phase_Y = (Reg09 | 0x20) + 1; //I-1
		}
		else if(CrossCount==3)
		{
		  Compare_Cross[CrossCount].Gain_X = Reg08 + 1; //Q-1
		  Compare_Cross[CrossCount].Phase_Y = Reg09;
		}
		else
		{
		  Compare_Cross[CrossCount].Gain_X = (Reg08 | 0x20) + 1; //I-1
		  Compare_Cross[CrossCount].Phase_Y = Reg09;
		}

    	R828_I2C.RegAddr = 0x08;
	    R828_I2C.Data    = Compare_Cross[CrossCount].Gain_X;
	    if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		   return RT_Fail;

	    R828_I2C.RegAddr = 0x09;
	    R828_I2C.Data    = Compare_Cross[CrossCount].Phase_Y;
	    if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		  return RT_Fail;
	
        if(R828_Muti_Read(pTuner, 0x01, &Compare_Cross[CrossCount].Value) != RT_Success)
		  return RT_Fail;

		if( Compare_Cross[CrossCount].Value < Compare_Temp.Value)
		{
		  Compare_Temp.Value = Compare_Cross[CrossCount].Value;
		  Compare_Temp.Gain_X = Compare_Cross[CrossCount].Gain_X;
		  Compare_Temp.Phase_Y = Compare_Cross[CrossCount].Phase_Y;		
		}
	} //end for loop


    if((Compare_Temp.Phase_Y & 0x1F)==1)  //y-direction
	{
      *X_Direct = (UINT8) 0;
	  IQ_Pont[0].Gain_X = Compare_Cross[0].Gain_X;
	  IQ_Pont[0].Phase_Y = Compare_Cross[0].Phase_Y;
	  IQ_Pont[0].Value = Compare_Cross[0].Value;

	  IQ_Pont[1].Gain_X = Compare_Cross[1].Gain_X;
	  IQ_Pont[1].Phase_Y = Compare_Cross[1].Phase_Y;
	  IQ_Pont[1].Value = Compare_Cross[1].Value;

	  IQ_Pont[2].Gain_X = Compare_Cross[2].Gain_X;
	  IQ_Pont[2].Phase_Y = Compare_Cross[2].Phase_Y;
	  IQ_Pont[2].Value = Compare_Cross[2].Value;
	}
	else //(0,0) or x-direction
	{	
	  *X_Direct = (UINT8) 1;
	  IQ_Pont[0].Gain_X = Compare_Cross[0].Gain_X;
	  IQ_Pont[0].Phase_Y = Compare_Cross[0].Phase_Y;
	  IQ_Pont[0].Value = Compare_Cross[0].Value;

	  IQ_Pont[1].Gain_X = Compare_Cross[3].Gain_X;
	  IQ_Pont[1].Phase_Y = Compare_Cross[3].Phase_Y;
	  IQ_Pont[1].Value = Compare_Cross[3].Value;

	  IQ_Pont[2].Gain_X = Compare_Cross[4].Gain_X;
	  IQ_Pont[2].Phase_Y = Compare_Cross[4].Phase_Y;
	  IQ_Pont[2].Value = Compare_Cross[4].Value;
	}
	return RT_Success;
}


//----------------------------------------------------------------------------------------//
// purpose: search surrounding points from previous point 
//          try (x-1), (x), (x+1) columns, and find min IMR result point
// input: IQ_Pont: previous point data(IMR Gain, Phase, ADC Result, RefRreq)
//                 will be updated to final best point                 
// output: TRUE or FALSE
//----------------------------------------------------------------------------------------//
R828_ErrCode R828_F_IMR(TUNER_MODULE *pTuner, R828_SectType* IQ_Pont)
{
	R828_SectType Compare_IQ[3];
	R828_SectType Compare_Bet[3];
	UINT8 VGA_Cunt;
	UINT16 VGA_Read;

	VGA_Cunt = 0;
	VGA_Read = 0;	

	//VGA
	for(VGA_Cunt = 12;VGA_Cunt < 16;VGA_Cunt ++)
	{
		R828_I2C.RegAddr = 0x0C;
        R828_I2C.Data    = (R828_Arry[7] & 0xF0) + VGA_Cunt;
		if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
			return RT_Fail;

		R828_Delay_MS(pTuner, 10);
		
		if(R828_Muti_Read(pTuner, 0x01, &VGA_Read) != RT_Success)
			return RT_Fail;

		if(VGA_Read > 40*4)
		break;
	}

	//Try X-1 column and save min result to Compare_Bet[0]
	if((IQ_Pont->Gain_X & 0x1F) == 0x00)
	{
		Compare_IQ[0].Gain_X = ((IQ_Pont->Gain_X) & 0xDF) + 1;  //Q-path, Gain=1
	}
	else
		Compare_IQ[0].Gain_X  = IQ_Pont->Gain_X - 1;  //left point
	Compare_IQ[0].Phase_Y = IQ_Pont->Phase_Y;

	if(R828_IQ_Tree(pTuner, Compare_IQ[0].Gain_X, Compare_IQ[0].Phase_Y, 0x08, &Compare_IQ[0]) != RT_Success)  // y-direction
		return RT_Fail;

	if(R828_CompreCor(&Compare_IQ[0]) != RT_Success)
		return RT_Fail;

	Compare_Bet[0].Gain_X = Compare_IQ[0].Gain_X;
	Compare_Bet[0].Phase_Y = Compare_IQ[0].Phase_Y;
	Compare_Bet[0].Value = Compare_IQ[0].Value;

	//Try X column and save min result to Compare_Bet[1]
	Compare_IQ[0].Gain_X = IQ_Pont->Gain_X;
	Compare_IQ[0].Phase_Y = IQ_Pont->Phase_Y;

	if(R828_IQ_Tree(pTuner, Compare_IQ[0].Gain_X, Compare_IQ[0].Phase_Y, 0x08, &Compare_IQ[0]) != RT_Success)
		return RT_Fail;

	if(R828_CompreCor(&Compare_IQ[0]) != RT_Success)
		return RT_Fail;

	Compare_Bet[1].Gain_X = Compare_IQ[0].Gain_X;
	Compare_Bet[1].Phase_Y = Compare_IQ[0].Phase_Y;
	Compare_Bet[1].Value = Compare_IQ[0].Value;

	//Try X+1 column and save min result to Compare_Bet[2]
	if((IQ_Pont->Gain_X & 0x1F) == 0x00)		
		Compare_IQ[0].Gain_X = ((IQ_Pont->Gain_X) | 0x20) + 1;  //I-path, Gain=1
	else
	    Compare_IQ[0].Gain_X = IQ_Pont->Gain_X + 1;
	Compare_IQ[0].Phase_Y = IQ_Pont->Phase_Y;

	if(R828_IQ_Tree(pTuner, Compare_IQ[0].Gain_X, Compare_IQ[0].Phase_Y, 0x08, &Compare_IQ[0]) != RT_Success)
		return RT_Fail;

	if(R828_CompreCor(&Compare_IQ[0]) != RT_Success)
		return RT_Fail;

	Compare_Bet[2].Gain_X = Compare_IQ[0].Gain_X;
	Compare_Bet[2].Phase_Y = Compare_IQ[0].Phase_Y;
	Compare_Bet[2].Value = Compare_IQ[0].Value;

	if(R828_CompreCor(&Compare_Bet[0]) != RT_Success)
		return RT_Fail;

	*IQ_Pont = Compare_Bet[0];
	
	return RT_Success;
}



R828_ErrCode R828_GPIO(TUNER_MODULE *pTuner, R828_GPIO_Type R828_GPIO_Conrl)
{
	if(R828_GPIO_Conrl == HI_SIG)
		R828_Arry[10] |= 0x01;
	else
		R828_Arry[10] &= 0xFE;

	R828_I2C.RegAddr = 0x0F;
	R828_I2C.Data    = R828_Arry[10];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

	return RT_Success;
}





R828_ErrCode R828_SetStandard(TUNER_MODULE *pTuner, R828_Standard_Type RT_Standard)
{	 

	// Used Normal Arry to Modify
	UINT8 ArrayNum;
	
	ArrayNum = 27;
	for(ArrayNum=0;ArrayNum<27;ArrayNum++)
	{
		R828_Arry[ArrayNum] = R828_iniArry[ArrayNum];
	}


	// Record Init Flag & Xtal_check Result
	if(R828_IMR_done_flag == TRUE)
        R828_Arry[7]    = (R828_Arry[7] & 0xF0) | 0x01 | (Xtal_cap_sel<<1);
	else
	    R828_Arry[7]    = (R828_Arry[7] & 0xF0) | 0x00;

	R828_I2C.RegAddr = 0x0C;
    R828_I2C.Data    = R828_Arry[7];
    if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
       return RT_Fail;

	// Record version
	R828_I2C.RegAddr = 0x13;
	R828_Arry[14]    = (R828_Arry[14] & 0xC0) | VER_NUM;
	R828_I2C.Data    = R828_Arry[14];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
	    return RT_Fail;


    //for LT Gain test
	if(RT_Standard > SECAM_L1)
	{
		R828_I2C.RegAddr = 0x1D;  //[5:3] LNA TOP
		R828_I2C.Data = (R828_Arry[24] & 0xC7) | 0x00;
	    if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		    return RT_Fail;

		//R828_Delay_MS(1);
	}

	// Look Up System Dependent Table
	Sys_Info1 = R828_Sys_Sel(RT_Standard);
	R828_IF_khz = Sys_Info1.IF_KHz;
	R828_CAL_LO_khz = Sys_Info1.FILT_CAL_LO;

	// Filter Calibration
    if(R828_Fil_Cal_flag[RT_Standard] == FALSE)
	{
		// do filter calibration 
		if(R828_Filt_Cal(pTuner, Sys_Info1.FILT_CAL_LO,Sys_Info1.BW) != RT_Success)
		    return RT_Fail;


		// read and set filter code
		R828_I2C_Len.RegAddr = 0x00;
		R828_I2C_Len.Len     = 5;
		if(I2C_Read_Len(pTuner, &R828_I2C_Len) != RT_Success)
			return RT_Fail;

		R828_Fil_Cal_code[RT_Standard] = R828_I2C_Len.Data[4] & 0x0F;

		//Filter Cali. Protection
		if(R828_Fil_Cal_code[RT_Standard]==0 || R828_Fil_Cal_code[RT_Standard]==15)
		{
		   if(R828_Filt_Cal(pTuner, Sys_Info1.FILT_CAL_LO,Sys_Info1.BW) != RT_Success)
			   return RT_Fail;

		   R828_I2C_Len.RegAddr = 0x00;
		   R828_I2C_Len.Len     = 5;
		   if(I2C_Read_Len(pTuner, &R828_I2C_Len) != RT_Success)
			   return RT_Fail;

		   R828_Fil_Cal_code[RT_Standard] = R828_I2C_Len.Data[4] & 0x0F;

		   if(R828_Fil_Cal_code[RT_Standard]==15) //narrowest
			   R828_Fil_Cal_code[RT_Standard] = 0;
			   
		}
        R828_Fil_Cal_flag[RT_Standard] = TRUE;
	}

	// Set Filter Q
	R828_Arry[5]  = (R828_Arry[5] & 0xE0) | Sys_Info1.FILT_Q | R828_Fil_Cal_code[RT_Standard];  
	R828_I2C.RegAddr  = 0x0A;
	R828_I2C.Data     = R828_Arry[5];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

	// Set BW, Filter_gain, & HP corner
	R828_Arry[6]= (R828_Arry[6] & 0x10) | Sys_Info1.HP_COR;
	R828_I2C.RegAddr  = 0x0B;
	R828_I2C.Data     = R828_Arry[6];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

	// Set Img_R
	R828_Arry[2]  = (R828_Arry[2] & 0x7F) | Sys_Info1.IMG_R;  
	R828_I2C.RegAddr  = 0x07;
	R828_I2C.Data     = R828_Arry[2];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;


	// Set filt_3dB, V6MHz
	R828_Arry[1]  = (R828_Arry[1] & 0xCF) | Sys_Info1.FILT_GAIN;  
	R828_I2C.RegAddr  = 0x06;
	R828_I2C.Data     = R828_Arry[1];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

    //channel filter extension
	R828_Arry[25]  = (R828_Arry[25] & 0x9F) | Sys_Info1.EXT_ENABLE;  
	R828_I2C.RegAddr  = 0x1E;
	R828_I2C.Data     = R828_Arry[25];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;


	//Loop through
	R828_Arry[0]  = (R828_Arry[0] & 0x7F) | Sys_Info1.LOOP_THROUGH;  
	R828_I2C.RegAddr  = 0x05;
	R828_I2C.Data     = R828_Arry[0];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

	//Loop through attenuation
	R828_Arry[26]  = (R828_Arry[26] & 0x7F) | Sys_Info1.LT_ATT;  
	R828_I2C.RegAddr  = 0x1F;
	R828_I2C.Data     = R828_Arry[26];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

    //filter extention widest
	R828_Arry[10]  = (R828_Arry[10] & 0x7F) | Sys_Info1.FLT_EXT_WIDEST;  
	R828_I2C.RegAddr  = 0x0F;
	R828_I2C.Data     = R828_Arry[10];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

	//RF poly filter current
	R828_Arry[20]  = (R828_Arry[20] & 0x9F) | Sys_Info1.POLYFIL_CUR;  
	R828_I2C.RegAddr  = 0x19;
	R828_I2C.Data     = R828_Arry[20];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

	return RT_Success;
}



R828_ErrCode R828_Filt_Cal(TUNER_MODULE *pTuner, UINT32 Cal_Freq,BW_Type R828_BW)
{
	  //set in Sys_sel()
	/*
	if(R828_BW == BW_8M)
	{
		//set filt_cap = no cap
		R828_I2C.RegAddr = 0x0B;  //reg11
		R828_Arry[6]   &= 0x9F;  //filt_cap = no cap
		R828_I2C.Data    = R828_Arry[6];		
	}
	else if(R828_BW == BW_7M)
	{
		//set filt_cap = +1 cap
		R828_I2C.RegAddr = 0x0B;  //reg11
		R828_Arry[6]   &= 0x9F;  //filt_cap = no cap
		R828_Arry[6]   |= 0x20;  //filt_cap = +1 cap
		R828_I2C.Data    = R828_Arry[6];		
	}
	else if(R828_BW == BW_6M)
	{
		//set filt_cap = +2 cap
		R828_I2C.RegAddr = 0x0B;  //reg11
		R828_Arry[6]   &= 0x9F;  //filt_cap = no cap
		R828_Arry[6]   |= 0x60;  //filt_cap = +2 cap
		R828_I2C.Data    = R828_Arry[6];		
	}


    if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;	
*/

    // Set filt_cap
	R828_I2C.RegAddr  = 0x0B;
	R828_Arry[6]= (R828_Arry[6] & 0x9F) | (Sys_Info1.HP_COR & 0x60);
	R828_I2C.Data     = R828_Arry[6];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;


	//set cali clk =on
	R828_I2C.RegAddr = 0x0F;  //reg15
	R828_Arry[10]   |= 0x04;  //calibration clk=on
	R828_I2C.Data    = R828_Arry[10];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

	//X'tal cap 0pF for PLL
	R828_I2C.RegAddr = 0x10;
	R828_Arry[11]    = (R828_Arry[11] & 0xFC) | 0x00;
	R828_I2C.Data    = R828_Arry[11];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

	//Set PLL Freq = Filter Cali Freq
	if(R828_PLL(pTuner, Cal_Freq, STD_SIZE) != RT_Success)
		return RT_Fail;

	//Start Trigger
	R828_I2C.RegAddr = 0x0B;	//reg11
	R828_Arry[6]   |= 0x10;	    //vstart=1	
	R828_I2C.Data    = R828_Arry[6];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

	//delay 0.5ms
	R828_Delay_MS(pTuner, 1);  

	//Stop Trigger
	R828_I2C.RegAddr = 0x0B;
	R828_Arry[6]   &= 0xEF;     //vstart=0
	R828_I2C.Data    = R828_Arry[6];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;


	//set cali clk =off
	R828_I2C.RegAddr  = 0x0F;	//reg15
	R828_Arry[10]    &= 0xFB;	//calibration clk=off
	R828_I2C.Data     = R828_Arry[10];
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

	return RT_Success;

}




R828_ErrCode R828_SetFrequency(TUNER_MODULE *pTuner, R828_Set_Info R828_INFO, R828_SetFreq_Type R828_SetFreqMode)
{

	 UINT32	LO_KHz;

     // Check Input Frequency Range
	 if((R828_INFO.RF_KHz<40000) || (R828_INFO.RF_KHz>900000))
	 {
	          return RT_Fail;
	 }

	 if(R828_INFO.R828_Standard==SECAM_L1)
		LO_KHz = R828_INFO.RF_KHz - Sys_Info1.IF_KHz;
	 else
	    LO_KHz = R828_INFO.RF_KHz + Sys_Info1.IF_KHz;

	 //Set MUX dependent var. Must do before PLL( ) 
     if(R828_MUX(pTuner, LO_KHz) != RT_Success)
        return RT_Fail;

	 

     //Set PLL
     if(R828_PLL(pTuner, LO_KHz, R828_INFO.R828_Standard) != RT_Success)
        return RT_Fail;

     R828_IMR_point_num = Freq_Info1.IMR_MEM;


     //Set TOP,VTH,VTL
     SysFreq_Info1 = R828_SysFreq_Sel(R828_INFO.R828_Standard, R828_INFO.RF_KHz);

    
     // write DectBW, pre_dect_TOP
     R828_Arry[24] = (R828_Arry[24] & 0x38) | (SysFreq_Info1.LNA_TOP & 0xC7);
     R828_I2C.RegAddr = 0x1D;
     R828_I2C.Data = R828_Arry[24];
     if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
         return RT_Fail;

     // write MIXER TOP, TOP+-1
     R828_Arry[23] = (R828_Arry[23] & 0x07) | (SysFreq_Info1.MIXER_TOP & 0xF8); 
     R828_I2C.RegAddr = 0x1C;
     R828_I2C.Data = R828_Arry[23];
     if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
        return RT_Fail;


     // write LNA VTHL
     R828_Arry[8] = (R828_Arry[8] & 0x00) | SysFreq_Info1.LNA_VTH_L;
     R828_I2C.RegAddr = 0x0D;
     R828_I2C.Data = R828_Arry[8];
     if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
        return RT_Fail;

     // write MIXER VTHL
     R828_Arry[9] = (R828_Arry[9] & 0x00) | SysFreq_Info1.MIXER_VTH_L;
     R828_I2C.RegAddr = 0x0E;
     R828_I2C.Data = R828_Arry[9];
     if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
        return RT_Fail;

	 // Cable-1/Air in 
	 R828_I2C.RegAddr = 0x05;
	 R828_Arry[0] &= 0x9F;
	 R828_Arry[0] |= SysFreq_Info1.AIR_CABLE1_IN;	
	 R828_I2C.Data = R828_Arry[0];
	 if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

	 // Cable-2 in 
	 R828_I2C.RegAddr = 0x06;
	 R828_Arry[1] &= 0xF7;
	 R828_Arry[1] |= SysFreq_Info1.CABLE2_IN;	
	 R828_I2C.Data = R828_Arry[1];
	 if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

     //CP current
	 R828_I2C.RegAddr = 0x11;
	 R828_Arry[12] &= 0xC7;
	 R828_Arry[12] |= SysFreq_Info1.CP_CUR;	
	 R828_I2C.Data = R828_Arry[12];
	 if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		 return RT_Fail;	

	 //div buffer current
	 R828_I2C.RegAddr = 0x17;
	 R828_Arry[18] &= 0xCF;
	 R828_Arry[18] |= SysFreq_Info1.DIV_BUF_CUR;
	 R828_I2C.Data = R828_Arry[18];
	 if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		 return RT_Fail;	

	 // Set channel filter current 
	 R828_I2C.RegAddr  = 0x0A;
	 R828_Arry[5]  = (R828_Arry[5] & 0x9F) | SysFreq_Info1.FILTER_CUR;  
	 R828_I2C.Data     = R828_Arry[5];
     if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
         return RT_Fail;

     //Air-In only for Astrometa
	 R828_Arry[0] =  (R828_Arry[0] & 0x9F) | 0x00;
     R828_Arry[1] =  (R828_Arry[1] & 0xF7) | 0x00;

	 R828_I2C.RegAddr = 0x05;
     R828_I2C.Data = R828_Arry[0];
	 if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
			return RT_Fail;

	 R828_I2C.RegAddr = 0x06;
     R828_I2C.Data = R828_Arry[1];
	 if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
			return RT_Fail;


     //Set LNA
	 if(R828_INFO.R828_Standard > SECAM_L1)
	 {

		 if(R828_SetFreqMode==FAST_MODE)       //FAST mode 
		 {
			 //R828_Arry[24] = (R828_Arry[24] & 0xC7) | 0x20; //LNA TOP:4
			 R828_Arry[24] = (R828_Arry[24] & 0xC7) | 0x00; //LNA TOP:lowest
			 R828_I2C.RegAddr = 0x1D;
			 R828_I2C.Data = R828_Arry[24];
			 if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
				 return RT_Fail;

			 R828_Arry[23] = (R828_Arry[23] & 0xFB);  // 0: normal mode
			 R828_I2C.RegAddr = 0x1C;
			 R828_I2C.Data = R828_Arry[23];
			 if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
				return RT_Fail;
			
			 R828_Arry[1]  = (R828_Arry[1] & 0xBF);   //0: PRE_DECT off
			 R828_I2C.RegAddr  = 0x06;
			 R828_I2C.Data     = R828_Arry[1];
			 if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
				return RT_Fail;

			 //agc clk 250hz
			 R828_Arry[21]  = (R828_Arry[21] & 0xCF) | 0x30;   			
			 R828_I2C.RegAddr  = 0x1A;
			 R828_I2C.Data     = R828_Arry[21];
			 if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
				return RT_Fail;
		 }
		 else  //NORMAL mode
		 {
			 			
			 R828_Arry[24] = (R828_Arry[24] & 0xC7) | 0x00; //LNA TOP:lowest
			 R828_I2C.RegAddr = 0x1D;
			 R828_I2C.Data = R828_Arry[24];
			 if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
				 return RT_Fail;

			 R828_Arry[23] = (R828_Arry[23] & 0xFB);  // 0: normal mode
			 R828_I2C.RegAddr = 0x1C;
			 R828_I2C.Data = R828_Arry[23];
			 if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
				return RT_Fail;
			
			 R828_Arry[1]  = (R828_Arry[1] & 0xBF);   //0: PRE_DECT off			
			 R828_I2C.RegAddr  = 0x06;
			 R828_I2C.Data     = R828_Arry[1];
			 if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
				return RT_Fail;

             //agc clk 250hz
			 R828_Arry[21]  = (R828_Arry[21] & 0xCF) | 0x30;   //250hz			
			 R828_I2C.RegAddr  = 0x1A;
			 R828_I2C.Data     = R828_Arry[21];
			 if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
				return RT_Fail;

			 R828_Delay_MS(pTuner, 250);
			 
			 // PRE_DECT on
			 /*
			 R828_Arry[1]  = (R828_Arry[1] & 0xBF) | SysFreq_Info1.PRE_DECT;
			 R828_I2C.RegAddr  = 0x06;
			 R828_I2C.Data     = R828_Arry[1];
			 if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
				return RT_Fail;			 
             */
			 // write LNA TOP = 3
			 //R828_Arry[24] = (R828_Arry[24] & 0xC7) | (SysFreq_Info1.LNA_TOP & 0x38);
			 R828_Arry[24] = (R828_Arry[24] & 0xC7) | 0x18;  //TOP=3
			 R828_I2C.RegAddr = 0x1D;
			 R828_I2C.Data = R828_Arry[24];
			 if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
				 return RT_Fail;

			 // write discharge mode
			 R828_Arry[23] = (R828_Arry[23] & 0xFB) | (SysFreq_Info1.MIXER_TOP & 0x04); 
			 R828_I2C.RegAddr = 0x1C;
			 R828_I2C.Data = R828_Arry[23];
			 if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
				return RT_Fail;

			 // LNA discharge current
			 R828_Arry[25]  = (R828_Arry[25] & 0xE0) | SysFreq_Info1.LNA_DISCHARGE;  
			 R828_I2C.RegAddr  = 0x1E;
			 R828_I2C.Data     = R828_Arry[25];
			 if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
				return RT_Fail;

			 //agc clk 60hz 
			 R828_Arry[21]  = (R828_Arry[21] & 0xCF) | 0x20;   	
			 R828_I2C.RegAddr  = 0x1A;
			 R828_I2C.Data     = R828_Arry[21];
			 if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
				return RT_Fail;
		 }
	 }
	 else 
	 {
		 if(R828_SetFreqMode==NORMAL_MODE || R828_SetFreqMode==FAST_MODE)
		 {
			 /*
             // PRE_DECT on
			 R828_Arry[1]  = (R828_Arry[1] & 0xBF) | SysFreq_Info1.PRE_DECT;
			 R828_I2C.RegAddr  = 0x06;
			 R828_I2C.Data     = R828_Arry[1];
			 if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
				return RT_Fail;			 
             */
			  // PRE_DECT off
			 R828_Arry[1]  = (R828_Arry[1] & 0xBF);   //0: PRE_DECT off
			 R828_I2C.RegAddr  = 0x06;
			 R828_I2C.Data     = R828_Arry[1];
			 if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
				return RT_Fail;

			 // write LNA TOP
			 R828_Arry[24] = (R828_Arry[24] & 0xC7) | (SysFreq_Info1.LNA_TOP & 0x38);
			 R828_I2C.RegAddr = 0x1D;
			 R828_I2C.Data = R828_Arry[24];
			 if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
				 return RT_Fail;

			 // write discharge mode
			 R828_Arry[23] = (R828_Arry[23] & 0xFB) | (SysFreq_Info1.MIXER_TOP & 0x04); 
			 R828_I2C.RegAddr = 0x1C;
			 R828_I2C.Data = R828_Arry[23];
			 if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
				return RT_Fail;

			 // LNA discharge current
			 R828_Arry[25]  = (R828_Arry[25] & 0xE0) | SysFreq_Info1.LNA_DISCHARGE;  
			 R828_I2C.RegAddr  = 0x1E;
			 R828_I2C.Data     = R828_Arry[25];
			 if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
				return RT_Fail;

			 // agc clk 1Khz, external det1 cap 1u
			 R828_Arry[21]  = (R828_Arry[21] & 0xCF) | 0x00;   			
			 R828_I2C.RegAddr  = 0x1A;
			 R828_I2C.Data     = R828_Arry[21];
			 if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
				return RT_Fail;

			 R828_Arry[11]  = (R828_Arry[11] & 0xFB) | 0x00;   			
			 R828_I2C.RegAddr  = 0x10;
			 R828_I2C.Data     = R828_Arry[11];
			 if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
				return RT_Fail;
		 }
	 }

     return RT_Success;

}




R828_ErrCode R828_Standby(TUNER_MODULE *pTuner, R828_LoopThrough_Type R828_LoopSwitch)
{
	if(R828_LoopSwitch == LOOP_THROUGH)
	{
		R828_I2C.RegAddr = 0x06;
		R828_I2C.Data    = 0xB1;
		if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
			return RT_Fail;
		R828_I2C.RegAddr = 0x05;
		R828_I2C.Data = 0x03;


		if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;
	}
	else
	{
		R828_I2C.RegAddr = 0x05;
		R828_I2C.Data    = 0xA3;
		if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
			return RT_Fail;

		R828_I2C.RegAddr = 0x06;
		R828_I2C.Data    = 0xB1;
		if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
			return RT_Fail;
	}

	R828_I2C.RegAddr = 0x07;
	R828_I2C.Data    = 0x3A;
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

	R828_I2C.RegAddr = 0x08;
	R828_I2C.Data    = 0x40;
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

	R828_I2C.RegAddr = 0x09;
	R828_I2C.Data    = 0xC0;   //polyfilter off
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

	R828_I2C.RegAddr = 0x0A;
	R828_I2C.Data    = 0x36;
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

	R828_I2C.RegAddr = 0x0C;
	R828_I2C.Data    = 0x35;
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

	R828_I2C.RegAddr = 0x0F;
	R828_I2C.Data    = 0x78;
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

	R828_I2C.RegAddr = 0x11;
	R828_I2C.Data    = 0x03;
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

	R828_I2C.RegAddr = 0x17;
	R828_I2C.Data    = 0xF4;
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

	R828_I2C.RegAddr = 0x19;
	R828_I2C.Data    = 0x0C;
	if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		return RT_Fail;

	
	return RT_Success;
}



R828_ErrCode R828_GetRfGain(TUNER_MODULE *pTuner, R828_RF_Gain_Info *pR828_rf_gain)
{

	R828_I2C_Len.RegAddr = 0x00;
	R828_I2C_Len.Len     = 4;
	if(I2C_Read_Len(pTuner, &R828_I2C_Len) != RT_Success)
		return RT_Fail;

	pR828_rf_gain->RF_gain1 = (R828_I2C_Len.Data[3] & 0x0F);
	pR828_rf_gain->RF_gain2 = ((R828_I2C_Len.Data[3] & 0xF0) >> 4);
	pR828_rf_gain->RF_gain_comb = pR828_rf_gain->RF_gain1*2 + pR828_rf_gain->RF_gain2;

    return RT_Success;
}


R828_ErrCode R828_RfGainMode(TUNER_MODULE *pTuner, R828_RF_Gain_TYPE R828_RfGainType)
{
    UINT8 MixerGain;
	UINT8 LnaGain;

    MixerGain = 0;
	LnaGain = 0;

	if(R828_RfGainType==RF_MANUAL)
	{
		//LNA auto off
	     R828_I2C.RegAddr = 0x05;
	     R828_Arry[0] = R828_Arry[0] | 0x10;
		 R828_I2C.Data = R828_Arry[0];
	     if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		       return RT_Fail;

		 //Mixer auto off
	     R828_I2C.RegAddr = 0x07;
	     R828_Arry[2] = R828_Arry[2] & 0xEF;
		 R828_I2C.Data = R828_Arry[2];
	     if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		       return RT_Fail;

		R828_I2C_Len.RegAddr = 0x00;
		R828_I2C_Len.Len     = 4; 
		if(I2C_Read_Len(pTuner, &R828_I2C_Len) != RT_Success)
			return RT_Fail;

		MixerGain = (R828_I2C_Len.Data[3] & 0xF0) >> 4;
		LnaGain = R828_I2C_Len.Data[3] & 0x0F;

		//set LNA gain
	     R828_I2C.RegAddr = 0x05;
	     R828_Arry[0] = (R828_Arry[0] & 0xF0) | LnaGain;
		 R828_I2C.Data = R828_Arry[0];
	     if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		       return RT_Fail;

		 //set Mixer gain
	     R828_I2C.RegAddr = 0x07;
	     R828_Arry[2] = (R828_Arry[2] & 0xF0) | MixerGain;
		 R828_I2C.Data = R828_Arry[2];
	     if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		       return RT_Fail;
	}
	else
	{
	    //LNA
	     R828_I2C.RegAddr = 0x05;
	     R828_Arry[0] = R828_Arry[0] & 0xEF;
		 R828_I2C.Data = R828_Arry[0];
	     if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		       return RT_Fail;

		 //Mixer
	     R828_I2C.RegAddr = 0x07;
	     R828_Arry[2] = R828_Arry[2] | 0x10;
		 R828_I2C.Data = R828_Arry[2];
	     if(I2C_Write(pTuner, &R828_I2C) != RT_Success)
		       return RT_Fail;
	}

    return RT_Success;
}



//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
//                      Smart GUI                               //
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
#if 0
UINT8 R828_IMR_XT[6];
UINT8 R828_IMR_YT[6];
R828_ErrCode R828_ReadIMR(void)
{
	UINT8 IMR_C = 0;
//	UINT8 IMR_X[6] = {0, 0, 0, 0, 0, 0};
//	UINT8 IMR_Y[6] = {0, 0, 0, 0, 0, 0};


	for(IMR_C = 0;IMR_C < 6;IMR_C ++)
	{
		R828_IMR_XT[IMR_C] = IMR_Data[IMR_C].Gain_X;
		R828_IMR_YT[IMR_C] = IMR_Data[IMR_C].Phase_Y;
	}
	
	return RT_Success;
}
#endif 
