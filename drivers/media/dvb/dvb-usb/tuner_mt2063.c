/**

@file

@brief   MT2063 tuner module definition

One can manipulate MT2063 tuner through MT2063 module.
MT2063 module is derived from tuner module.

*/


#include "tuner_mt2063.h"





/**

@brief   MT2063 tuner module builder

Use BuildMt2063Module() to build MT2063 module, set all module function pointers with the corresponding functions,
and initialize module private variables.


@param [in]   ppTuner                      Pointer to MT2063 tuner module pointer
@param [in]   pTunerModuleMemory           Pointer to an allocated tuner module memory
@param [in]   pBaseInterfaceModuleMemory   Pointer to an allocated base interface module memory
@param [in]   pI2cBridgeModuleMemory       Pointer to an allocated I2C bridge module memory
@param [in]   DeviceAddr                   MT2063 I2C device address
@param [in]   IfFreqHz                     MT2063 output IF frequency in Hz
@param [in]   StandardMode                 Standard mode
@param [in]   IfVgaGainControl             IF VGA gain control


@note
	-# One should call BuildMt2063Module() to build MT2063 module before using it.

*/
void
BuildMt2063Module(
	TUNER_MODULE **ppTuner,
	TUNER_MODULE *pTunerModuleMemory,
	BASE_INTERFACE_MODULE *pBaseInterfaceModuleMemory,
	I2C_BRIDGE_MODULE *pI2cBridgeModuleMemory,
	unsigned char DeviceAddr,
	int StandardMode,
	unsigned long VgaGc
	)
{
	TUNER_MODULE *pTuner;
	MT2063_EXTRA_MODULE *pExtra;



	// Set tuner module pointer.
	*ppTuner = pTunerModuleMemory;

	// Get tuner module.
	pTuner = *ppTuner;

	// Set base interface module pointer and I2C bridge module pointer.
	pTuner->pBaseInterface = pBaseInterfaceModuleMemory;
	pTuner->pI2cBridge = pI2cBridgeModuleMemory;

	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Mt2063);



	// Set tuner type.
	pTuner->TunerType = TUNER_TYPE_MT2063;

	// Set tuner I2C device address.
	pTuner->DeviceAddr = DeviceAddr;


	// Initialize tuner parameter setting status.
	pTuner->IsRfFreqHzSet = NO;


	// Set tuner module manipulating function pointers.
	pTuner->GetTunerType  = mt2063_GetTunerType;
	pTuner->GetDeviceAddr = mt2063_GetDeviceAddr;

	pTuner->Initialize    = mt2063_Initialize;
	pTuner->SetRfFreqHz   = mt2063_SetRfFreqHz;
	pTuner->GetRfFreqHz   = mt2063_GetRfFreqHz;


	// Initialize tuner extra module variables.
	pExtra->StandardMode     = StandardMode;
	pExtra->VgaGc            = VgaGc;
	pExtra->IsIfFreqHzSet    = NO;
	pExtra->IsBandwidthHzSet = NO;

	// Set tuner extra module function pointers.
	pExtra->OpenHandle     = mt2063_OpenHandle;
	pExtra->CloseHandle    = mt2063_CloseHandle;
	pExtra->GetHandle      = mt2063_GetHandle;
	pExtra->SetIfFreqHz    = mt2063_SetIfFreqHz;
	pExtra->GetIfFreqHz    = mt2063_GetIfFreqHz;
	pExtra->SetBandwidthHz = mt2063_SetBandwidthHz;
	pExtra->GetBandwidthHz = mt2063_GetBandwidthHz;


	return;
}





/**

@see   TUNER_FP_GET_TUNER_TYPE

*/
void
mt2063_GetTunerType(
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
mt2063_GetDeviceAddr(
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
mt2063_Initialize(
	TUNER_MODULE *pTuner
	)
{
	MT2063_EXTRA_MODULE *pExtra;

	Handle_t DeviceHandle;
	UData_t Status;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Mt2063);

	// Get tuner handle.
	DeviceHandle = pExtra->DeviceHandle;


	// Re-initialize tuner.
	Status = MT2063_ReInit(DeviceHandle);

	if(MT_IS_ERROR(Status))
		goto error_status_execute_function;


	// Set tuner output bandwidth.
//	Status = MT2063_SetParam(DeviceHandle, MT2063_OUTPUT_BW, MT2063_BANDWIDTH_6MHZ);

//	if(MT_IS_ERROR(Status))
//		goto error_status_execute_function;


	// Set tuner parameters according to standard mode.
	switch(pExtra->StandardMode)
	{
		default:
		case MT2063_STANDARD_DVBT:

			Status = MT2063_SetParam(DeviceHandle, MT2063_RCVR_MODE, MT2063_OFFAIR_COFDM);

			if(MT_IS_ERROR(Status))
				goto error_status_execute_function;

			break;


		case MT2063_STANDARD_QAM:

			Status = MT2063_SetParam(DeviceHandle, MT2063_RCVR_MODE, MT2063_CABLE_QAM);

			if(MT_IS_ERROR(Status))
				goto error_status_execute_function;

			break;
	}


	// Set tuner VGAGC with IF AGC gain control setting value.
	Status = MT2063_SetParam(DeviceHandle, MT2063_VGAGC, (UData_t)(pExtra->VgaGc));

	if(MT_IS_ERROR(Status))
		goto error_status_execute_function;


	return FUNCTION_SUCCESS;


error_status_execute_function:
	return FUNCTION_ERROR;
}





/**

@see   TUNER_FP_SET_RF_FREQ_HZ

*/
int
mt2063_SetRfFreqHz(
	TUNER_MODULE *pTuner,
	unsigned long RfFreqHz
	)
{
	MT2063_EXTRA_MODULE *pExtra;

	Handle_t DeviceHandle;
	UData_t Status;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Mt2063);

	// Get tuner handle.
	DeviceHandle = pExtra->DeviceHandle;


	// Set tuner RF frequency in Hz.
	Status = MT2063_Tune(DeviceHandle, RfFreqHz);

	if(MT_IS_ERROR(Status))
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
mt2063_GetRfFreqHz(
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

@brief   Open MT2063 tuner handle.

*/
int
mt2063_OpenHandle(
	TUNER_MODULE *pTuner
	)
{
	MT2063_EXTRA_MODULE *pExtra;

	unsigned char DeviceAddr;
	UData_t Status;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Mt2063);

	// Get tuner I2C device address.
	pTuner->GetDeviceAddr(pTuner, &DeviceAddr);


	// Open MT2063 handle.
	// Note: 1. Must take tuner extra module DeviceHandle as handle input argument.
	//       2. Take pTuner as user-defined data input argument.
	Status = MT2063_Open(DeviceAddr, &pExtra->DeviceHandle, pTuner);

	if(MT_IS_ERROR(Status))
		goto error_status_open_mt2063_handle;


	return FUNCTION_SUCCESS;


error_status_open_mt2063_handle:
	return FUNCTION_ERROR;
}





/**

@brief   Close MT2063 tuner handle.

*/
int
mt2063_CloseHandle(
	TUNER_MODULE *pTuner
	)
{
	MT2063_EXTRA_MODULE *pExtra;

	Handle_t DeviceHandle;
	UData_t Status;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Mt2063);

	// Get tuner handle.
	DeviceHandle = pExtra->DeviceHandle;


	// Close MT2063 handle.
	Status = MT2063_Close(DeviceHandle);

	if(MT_IS_ERROR(Status))
		goto error_status_open_mt2063_handle;


	return FUNCTION_SUCCESS;


error_status_open_mt2063_handle:
	return FUNCTION_ERROR;
}





/**

@brief   Get MT2063 tuner handle.

*/
void
mt2063_GetHandle(
	TUNER_MODULE *pTuner,
	void **pDeviceHandle
	)
{
	MT2063_EXTRA_MODULE *pExtra;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Mt2063);

	// Get tuner handle.
	*pDeviceHandle = pExtra->DeviceHandle;


	return;
}





/**

@brief   Set MT2063 tuner IF frequency in Hz.

*/
int
mt2063_SetIfFreqHz(
	TUNER_MODULE *pTuner,
	unsigned long IfFreqHz
	)
{
	MT2063_EXTRA_MODULE *pExtra;

	Handle_t DeviceHandle;
	UData_t Status;

	unsigned int IfFreqHzUint;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Mt2063);

	// Get tuner handle.
	DeviceHandle = pExtra->DeviceHandle;


	// Set tuner output IF frequency.
	IfFreqHzUint = (unsigned int)IfFreqHz;
	Status = MT2063_SetParam(DeviceHandle, MT2063_OUTPUT_FREQ, IfFreqHzUint);

	if(MT_IS_ERROR(Status))
		goto error_status_execute_function;


	// Set tuner IF frequnecy parameter in tuner extra module.
	pExtra->IfFreqHz      = IfFreqHz;
	pExtra->IsIfFreqHzSet = YES;


	return FUNCTION_SUCCESS;


error_status_execute_function:
	return FUNCTION_ERROR;
}





/**

@brief   Get MT2063 tuner IF frequency in Hz.

*/
int
mt2063_GetIfFreqHz(
	TUNER_MODULE *pTuner,
	unsigned long *pIfFreqHz
	)
{
	MT2063_EXTRA_MODULE *pExtra;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Mt2063);


	// Get tuner IF frequency in Hz from tuner extra module.
	if(pExtra->IsIfFreqHzSet != YES)
		goto error_status_get_demod_if_frequency;

	*pIfFreqHz = pExtra->IfFreqHz;


	return FUNCTION_SUCCESS;


error_status_get_demod_if_frequency:
	return FUNCTION_ERROR;
}





/**

@brief   Set MT2063 tuner bandwidth in Hz.

*/
// Note: The function MT2063_SetParam() only sets software bandwidth variables,
//       so execute MT2063_Tune() after this function.
int
mt2063_SetBandwidthHz(
	TUNER_MODULE *pTuner,
	unsigned long BandwidthHz
	)
{
	MT2063_EXTRA_MODULE *pExtra;

	Handle_t DeviceHandle;
	UData_t Status;

//	unsigned long BandwidthHzWithShift;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Mt2063);

	// Get tuner handle.
	DeviceHandle = pExtra->DeviceHandle;


	// Set tuner bandwidth in Hz with shift.
//	BandwidthHzWithShift = BandwidthHz - MT2063_BANDWIDTH_SHIFT_HZ;
//	Status = MT2063_SetParam(DeviceHandle, MT2063_OUTPUT_BW, BandwidthHzWithShift);
	Status = MT2063_SetParam(DeviceHandle, MT2063_OUTPUT_BW, BandwidthHz);

	if(MT_IS_ERROR(Status))
		goto error_status_set_tuner_bandwidth;


	// Set tuner bandwidth parameter.
	pExtra->BandwidthHz      = BandwidthHz;
	pExtra->IsBandwidthHzSet = YES;


	return FUNCTION_SUCCESS;


error_status_set_tuner_bandwidth:
	return FUNCTION_ERROR;
}





/**

@brief   Get MT2063 tuner bandwidth in Hz.

*/
int
mt2063_GetBandwidthHz(
	TUNER_MODULE *pTuner,
	unsigned long *pBandwidthHz
	)
{
	MT2063_EXTRA_MODULE *pExtra;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Mt2063);


	// Get tuner bandwidth in Hz from tuner module.
	if(pExtra->IsBandwidthHzSet != YES)
		goto error_status_get_tuner_bandwidth;

	*pBandwidthHz = pExtra->BandwidthHz;


	return FUNCTION_SUCCESS;


error_status_get_tuner_bandwidth:
	return FUNCTION_ERROR;
}























// The following context is source code provided by Microtune.





// Microtune source code - mt_userdef.c


/*****************************************************************************
**
**  Name: mt_userdef.c
**
**  Description:    User-defined MicroTuner software interface 
**
**  Functions
**  Requiring
**  Implementation: MT_WriteSub
**                  MT_ReadSub
**                  MT_Sleep
**
**  References:     None
**
**  Exports:        None
**
**  CVS ID:         $Id: mt_userdef.c,v 1.2 2008/11/05 13:46:20 software Exp $
**  CVS Source:     $Source: /export/vol0/cvsroot/software/tuners/MT2063/mt_userdef.c,v $
**	               
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   03-25-2004    DAD    Original
**
*****************************************************************************/
//#include "mt_userdef.h"


/*****************************************************************************
**
**  Name: MT_WriteSub
**
**  Description:    Write values to device using a two-wire serial bus.
**
**  Parameters:     hUserData  - User-specific I/O parameter that was
**                               passed to tuner's Open function.
**                  addr       - device serial bus address  (value passed
**                               as parameter to MTxxxx_Open)
**                  subAddress - serial bus sub-address (Register Address)
**                  pData      - pointer to the Data to be written to the 
**                               device 
**                  cnt        - number of bytes/registers to be written
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_COMM_ERR      - Serial bus communications error
**                      user-defined
**
**  Notes:          This is a callback function that is called from the
**                  the tuning algorithm.  You MUST provide code for this
**                  function to write data using the tuner's 2-wire serial 
**                  bus.
**
**                  The hUserData parameter is a user-specific argument.
**                  If additional arguments are needed for the user's
**                  serial bus read/write functions, this argument can be
**                  used to supply the necessary information.
**                  The hUserData parameter is initialized in the tuner's Open
**                  function.
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   03-25-2004    DAD    Original
**
*****************************************************************************/
UData_t MT2063_WriteSub(Handle_t hUserData, 
                    UData_t addr, 
                    U8Data subAddress, 
                    U8Data *pData, 
                    UData_t cnt)
{
//    UData_t status = MT_OK;                  /* Status to be returned        */
    /*
    **  ToDo:  Add code here to implement a serial-bus write
    **         operation to the MTxxxx tuner.  If successful,
    **         return MT_OK.
    */
/*  return status;  */


	TUNER_MODULE *pTuner;
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
	pTuner         = (TUNER_MODULE *)hUserData;
	pBaseInterface = pTuner->pBaseInterface;
	pI2cBridge     = pTuner->pI2cBridge;

	// Get tuner device address.
	pTuner->GetDeviceAddr(pTuner, &DeviceAddr);


	// Get regiser start address, writing bytes, and byte number.
	RegStartAddr  = subAddress;
	pWritingBytes = pData;
	ByteNum       = (unsigned long)cnt;


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


	return MT_OK;


error_status_set_tuner_registers:
	return MT_COMM_ERR;
}


/*****************************************************************************
**
**  Name: MT_ReadSub
**
**  Description:    Read values from device using a two-wire serial bus.
**
**  Parameters:     hUserData  - User-specific I/O parameter that was
**                               passed to tuner's Open function.
**                  addr       - device serial bus address  (value passed
**                               as parameter to MTxxxx_Open)
**                  subAddress - serial bus sub-address (Register Address)
**                  pData      - pointer to the Data to be written to the 
**                               device 
**                  cnt        - number of bytes/registers to be written
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_COMM_ERR      - Serial bus communications error
**                      user-defined
**
**  Notes:          This is a callback function that is called from the
**                  the tuning algorithm.  You MUST provide code for this
**                  function to read data using the tuner's 2-wire serial 
**                  bus.
**
**                  The hUserData parameter is a user-specific argument.
**                  If additional arguments are needed for the user's
**                  serial bus read/write functions, this argument can be
**                  used to supply the necessary information.
**                  The hUserData parameter is initialized in the tuner's Open
**                  function.
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   03-25-2004    DAD    Original
**
*****************************************************************************/
UData_t MT2063_ReadSub(Handle_t hUserData, 
                   UData_t addr, 
                   U8Data subAddress, 
                   U8Data *pData, 
                   UData_t cnt)
{
//    UData_t status = MT_OK;                        /* Status to be returned        */

    /*
    **  ToDo:  Add code here to implement a serial-bus read
    **         operation to the MTxxxx tuner.  If successful,
    **         return MT_OK.
    */
/*  return status;  */


	TUNER_MODULE *pTuner;
	BASE_INTERFACE_MODULE *pBaseInterface;
	I2C_BRIDGE_MODULE *pI2cBridge;
	unsigned char DeviceAddr;

	unsigned int i;

	unsigned char RegStartAddr;
	unsigned char *pReadingBytes;
	unsigned long ByteNum;

	unsigned long ReadingByteNum, ReadingByteNumMax, ReadingByteNumRem;
	unsigned char RegReadingAddr;



	// Get tuner module, base interface, and I2C bridge.
	pTuner         = (TUNER_MODULE *)hUserData;
	pBaseInterface = pTuner->pBaseInterface;
	pI2cBridge     = pTuner->pI2cBridge;

	// Get tuner device address.
	pTuner->GetDeviceAddr(pTuner, &DeviceAddr);


	// Get regiser start address, writing bytes, and byte number.
	RegStartAddr  = subAddress;
	pReadingBytes = pData;
	ByteNum       = (unsigned long)cnt;


	// Calculate maximum reading byte number.
	ReadingByteNumMax = pBaseInterface->I2cReadingByteNumMax;


	// Get tuner register bytes.
	// Note: Get tuner register bytes considering maximum reading byte number.
	for(i = 0; i < ByteNum; i += ReadingByteNumMax)
	{
		// Set register reading address.
		RegReadingAddr = RegStartAddr + i;

		// Calculate remainder reading byte number.
		ReadingByteNumRem = ByteNum - i;

		// Determine reading byte number.
		ReadingByteNum = (ReadingByteNumRem > ReadingByteNumMax) ? ReadingByteNumMax : ReadingByteNumRem;


		// Set tuner register reading address.
		// Note: The I2C format of tuner register reading address setting is as follows:
		//       start_bit + (DeviceAddr | writing_bit) + RegReadingAddr + stop_bit
		if(pI2cBridge->ForwardI2cWritingCmd(pI2cBridge, DeviceAddr, &RegReadingAddr, LEN_1_BYTE) != FUNCTION_SUCCESS)
			goto error_status_set_tuner_register_reading_address;

		// Get tuner register bytes.
		// Note: The I2C format of tuner register byte getting is as follows:
		//       start_bit + (DeviceAddr | reading_bit) + reading_bytes (ReadingByteNum bytes) + stop_bit
		if(pI2cBridge->ForwardI2cReadingCmd(pI2cBridge, DeviceAddr, &pReadingBytes[i], ReadingByteNum) != FUNCTION_SUCCESS)
			goto error_status_get_tuner_registers;
	}


	return MT_OK;


error_status_get_tuner_registers:
error_status_set_tuner_register_reading_address:
	return MT_COMM_ERR;
}


/*****************************************************************************
**
**  Name: MT_Sleep
**
**  Description:    Delay execution for "nMinDelayTime" milliseconds
**
**  Parameters:     hUserData     - User-specific I/O parameter that was
**                                  passed to tuner's Open function.
**                  nMinDelayTime - Delay time in milliseconds
**
**  Returns:        None.
**
**  Notes:          This is a callback function that is called from the
**                  the tuning algorithm.  You MUST provide code that
**                  blocks execution for the specified period of time. 
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   03-25-2004    DAD    Original
**
*****************************************************************************/
void MT2063_Sleep(Handle_t hUserData, 
              UData_t nMinDelayTime)
{
    /*
    **  ToDo:  Add code here to implement a OS blocking
    **         for a period of "nMinDelayTime" milliseconds.
    */


	TUNER_MODULE *pTuner;
	BASE_INTERFACE_MODULE *pBaseInterface;



	// Get tuner module, base interface.
	pTuner         = (TUNER_MODULE *)hUserData;
	pBaseInterface = pTuner->pBaseInterface;


	// Wait nMinDelayTime milliseconds.
	pBaseInterface->WaitMs(pBaseInterface, nMinDelayTime);


	return;
}


#if defined(MT2060_CNT)
#if MT2060_CNT > 0
/*****************************************************************************
**
**  Name: MT_TunerGain  (MT2060 only)
**
**  Description:    Measure the relative tuner gain using the demodulator
**
**  Parameters:     hUserData  - User-specific I/O parameter that was
**                               passed to tuner's Open function.
**                  pMeas      - Tuner gain (1/100 of dB scale).
**                               ie. 1234 = 12.34 (dB)
**
**  Returns:        status:
**                      MT_OK  - No errors
**                      user-defined errors could be set
**
**  Notes:          This is a callback function that is called from the
**                  the 1st IF location routine.  You MUST provide
**                  code that measures the relative tuner gain in a dB
**                  (not linear) scale.  The return value is an integer
**                  value scaled to 1/100 of a dB.
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   06-16-2004    DAD    Original
**   N/A   11-30-2004    DAD    Renamed from MT_DemodInputPower.  This name
**                              better describes what this function does.
**
*****************************************************************************/
UData_t MT_TunerGain(Handle_t hUserData,
                     SData_t* pMeas)
{
    UData_t status = MT_OK;                        /* Status to be returned        */

    /*
    **  ToDo:  Add code here to return the gain / power level measured
    **         at the input to the demodulator.
    */



    return (status);
}
#endif
#endif























// Microtune source code - mt_2063.c


/*****************************************************************************
**
**  Name: mt2063.c
**
**  Copyright 2007-2008 Microtune, Inc. All Rights Reserved
**
**  This source code file contains confidential information and/or trade
**  secrets of Microtune, Inc. or its affiliates and is subject to the
**  terms of your confidentiality agreement with Microtune, Inc. or one of
**  its affiliates, as applicable.
**
*****************************************************************************/

/*****************************************************************************
**
**  Name: mt2063.c
**
**  Description:    Microtune MT2063 B0, B1 & B3 Tuner software interface
**                  Supports tuners with Part/Rev code: 0x9B - 0x9E.
**
**  Functions
**  Implemented:    UData_t  MT2063_Open
**                  UData_t  MT2063_Close
**                  UData_t  MT2063_ClearPowerMaskBits
**                  UData_t  MT2063_GetGPIO
**                  UData_t  MT2063_GetLocked
**                  UData_t  MT2063_GetParam
**                  UData_t  MT2063_GetPowerMaskBits
**                  UData_t  MT2063_GetReg
**                  UData_t  MT2063_GetTemp
**                  UData_t  MT2063_GetUserData
**                  UData_t  MT2063_ReInit
**                  UData_t  MT2063_SetGPIO
**                  UData_t  MT2063_SetParam
**                  UData_t  MT2063_SetPowerMaskBits
**                  UData_t  MT2063_SetReg
**                  UData_t  MT2063_Tune
**
**  References:     AN-00189: MT2063 Programming Procedures Application Note
**                  MicroTune, Inc.
**                  AN-00010: MicroTuner Serial Interface Application Note
**                  MicroTune, Inc.
**
**  Exports:        None
**
**  Dependencies:   MT_ReadSub(hUserData, IC_Addr, subAddress, *pData, cnt);
**                  - Read byte(s) of data from the two-wire bus.
**
**                  MT_WriteSub(hUserData, IC_Addr, subAddress, *pData, cnt);
**                  - Write byte(s) of data to the two-wire bus.
**
**                  MT_Sleep(hUserData, nMinDelayTime);
**                  - Delay execution for nMinDelayTime milliseconds
**
**  CVS ID:         $Id: mt2063.c,v 1.6 2009/04/29 09:58:14 software Exp $
**  CVS Source:     $Source: /export/vol0/cvsroot/software/tuners/MT2063/mt2063.c,v $
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**   N/A   08-01-2007    PINZ   Ver 1.01: Changed Analog &DVB-T settings and added
**                                        SAW-less receiver mode.
**   148   09-04-2007    RSK    Ver 1.02: Corrected logic of Reg 3B Reference
**   153   09-07-2007    RSK    Ver 1.03: Lock Time improvements
**   150   09-10-2007    RSK    Ver 1.04: Added GetParam/SetParam support for
**                                        LO1 and LO2 freq for PHY-21 cal.
**   154   09-13-2007    RSK    Ver 1.05: Get/SetParam changes for LOx_FREQ
**   155   10-01-2007    DAD    Ver 1.06: Add receiver mode for SECAM positive
**                                        modulation
**                                        (MT2063_ANALOG_TV_POS_NO_RFAGC_MODE)
**   N/A   10-22-2007    PINZ   Ver 1.07: Changed some Registers at init to have
**                                        the same settings as with MT Launcher
**   N/A   10-30-2007    PINZ             Add SetParam VGAGC & VGAOI
**                                        Add SetParam DNC_OUTPUT_ENABLE
**                                        Removed VGAGC from receiver mode,
**                                        default now 3
**   N/A   10-31-2007    PINZ   Ver 1.08: Add SetParam TAGC, removed from rcvr-mode
**                                        Add SetParam AMPGC, removed from rcvr-mode
**                                        Corrected names of GCU values
**                                        reorganized receiver modes, removed,
**                                        (MT2063_ANALOG_TV_POS_NO_RFAGC_MODE)
**                                        Actualized Receiver-Mode values
**   N/A   11-12-2007    PINZ   Ver 1.09: Actualized Receiver-Mode values
**   N/A   11-27-2007    PINZ             Improved buffered writing
**         01-03-2008    PINZ   Ver 1.10: Added a trigger of BYPATNUP for
**                                        correct wakeup of the LNA after shutdown
**                                        Set AFCsd = 1 as default
**                                        Changed CAP1sel default
**         01-14-2008    PINZ   Ver 1.11: Updated gain settings, default for VGAGC 3
**   173 M 01-23-2008    RSK    Ver 1.12: Read LO1C and LO2C registers from HW
**                                        in GetParam.
**         03-18-2008    PINZ   Ver 1.13: Added Support for MT2063 B3
**         04-10-2008    PINZ   Ver 1.14: Use software-controlled ClearTune
**                                        cross-over frequency values.
**                                        Documentation Updates
**         04-18-2008    PINZ   Ver 1.15: Add SetParam LNARIN & PDxTGT
**                                        Split SetParam up to ACLNA / ACLNA_MAX
**                                        removed ACLNA_INRC/DECR (+RF & FIF)
**                                        removed GCUAUTO / BYPATNDN/UP
**   189 S 05-13-2008    PINZ   Ver 1.16: Correct location for ExtSRO control.
**                                        Add control to avoid DECT freqs.
**                                        Updated Receivermodes for MT2063 B3
**   175 I 06-19-2008    RSK    Ver 1.17: Refactor DECT control to SpurAvoid.
**         06-24-2008    PINZ   Ver 1.18: Add Get/SetParam CTFILT_SW
**         07-10-2008    PINZ   Ver 1.19: Documentation Updates, Add a GetParam
**                                        for each SetParam (LNA_RIN, TGTs)
**         08-05-2008    PINZ   Ver 1.20: Disable SDEXT pin while MT2063_ReInit
**                                        with MT2063B3
**         09-18-2008    PINZ   Ver 1.22: Updated values for CTFILT_SW
**   205 M 10-01-2008    JWS    Ver 1.23: Use DMAX when LO2 FracN is near 4096
**       M 10-24-2008    JWS    Ver 1.25: Use DMAX when LO1 FracN is 32
**         11-05-2008    PINZ   Ver 1.26: Minor code fixes
**         01-07-2009    PINZ   Ver 1.27: Changed value of MT2063_ALL_SD in .h
**         01-20-2009    PINZ   Ver 1.28: Fixed a compare to avoid compiler warning
**         02-16-2009    PINZ   Ver 1.29: Several fixes to improve compiler behavior
**   N/A I 02-26-2009    PINZ   Ver 1.30: RCVRmode 2: acfifmax 29->0, pd2tgt 33->34
**                                        RCVRmode 4: acfifmax 29->0, pd2tgt 30->34
**   N/A I 03-02-2009    PINZ   Ver 1.31: new default Fout 43.75 -> 36.125MHz
**                                        new default Output-BW 6 -> 8MHz
**                                        new default Stepsize 50 -> 62.5kHz
**                                        new default Fin 651 -> 666 MHz
**                                        Changed order of receiver-mode init
**   N/A I 03-19-2009    PINZ   Ver 1.32: Add GetParam VERSION (of API)
**                                        Add GetParam IC_REV
**   N/A I 04-22-2009    PINZ   Ver 1.33: Small fix in GetParam PD1/PD2
**   N/A I 04-29-2009    PINZ   Ver 1.34: Optimized ReInit function
**
*****************************************************************************/
//#include "mt2063.h"
//#include "mt_spuravoid.h"
//#include <stdlib.h>               /*  for NULL      */
#define MT_NULL			0

/*  Version of this module                          */
#define MT2063_MODULE_VERSION 10304             /*  Version 01.34 */


/*
**  The expected version of MT_AvoidSpursData_t
**  If the version is different, an updated file is needed from Microtune
*/
/* Expecting version 1.21 of the Spur Avoidance API */
#define EXPECTED_MT_AVOID_SPURS_INFO_VERSION  010201

#if MT2063_AVOID_SPURS_INFO_VERSION < EXPECTED_MT_AVOID_SPURS_INFO_VERSION
#error Contact Microtune for a newer version of MT_SpurAvoid.c
#elif MT2063_AVOID_SPURS_INFO_VERSION > EXPECTED_MT_AVOID_SPURS_INFO_VERSION
#error Contact Microtune for a newer version of mt2063.c
#endif

#ifndef MT2063_CNT
#error You must define MT2063_CNT in the "mt_userdef.h" file
#endif


/*
**  Two-wire serial bus subaddresses of the tuner registers.
**  Also known as the tuner's register addresses.
*/
enum MT2063_Register_Offsets
{
    PART_REV = 0,   /*  0x00: Part/Rev Code         */
    LO1CQ_1,        /*  0x01: LO1C Queued Byte 1    */
    LO1CQ_2,        /*  0x02: LO1C Queued Byte 2    */
    LO2CQ_1,        /*  0x03: LO2C Queued Byte 1    */
    LO2CQ_2,        /*  0x04: LO2C Queued Byte 2    */
    LO2CQ_3,        /*  0x05: LO2C Queued Byte 3    */
    RSVD_06,        /*  0x06: Reserved              */
    LO_STATUS,      /*  0x07: LO Status             */
    FIFFC,          /*  0x08: FIFF Center           */
    CLEARTUNE,      /*  0x09: ClearTune Filter      */
    ADC_OUT,        /*  0x0A: ADC_OUT               */
    LO1C_1,         /*  0x0B: LO1C Byte 1           */
    LO1C_2,         /*  0x0C: LO1C Byte 2           */
    LO2C_1,         /*  0x0D: LO2C Byte 1           */
    LO2C_2,         /*  0x0E: LO2C Byte 2           */
    LO2C_3,         /*  0x0F: LO2C Byte 3           */
    RSVD_10,        /*  0x10: Reserved              */
    PWR_1,          /*  0x11: PWR Byte 1            */
    PWR_2,          /*  0x12: PWR Byte 2            */
    TEMP_STATUS,    /*  0x13: Temp Status           */
    XO_STATUS,      /*  0x14: Crystal Status        */
    RF_STATUS,      /*  0x15: RF Attn Status        */
    FIF_STATUS,     /*  0x16: FIF Attn Status       */
    LNA_OV,         /*  0x17: LNA Attn Override     */
    RF_OV,          /*  0x18: RF Attn Override      */
    FIF_OV,         /*  0x19: FIF Attn Override     */
    LNA_TGT,        /*  0x1A: Reserved              */
    PD1_TGT,        /*  0x1B: Pwr Det 1 Target      */
    PD2_TGT,        /*  0x1C: Pwr Det 2 Target      */
    RSVD_1D,        /*  0x1D: Reserved              */
    RSVD_1E,        /*  0x1E: Reserved              */
    RSVD_1F,        /*  0x1F: Reserved              */
    RSVD_20,        /*  0x20: Reserved              */
    BYP_CTRL,       /*  0x21: Bypass Control        */
    RSVD_22,        /*  0x22: Reserved              */
    RSVD_23,        /*  0x23: Reserved              */
    RSVD_24,        /*  0x24: Reserved              */
    RSVD_25,        /*  0x25: Reserved              */
    RSVD_26,        /*  0x26: Reserved              */
    RSVD_27,        /*  0x27: Reserved              */
    FIFF_CTRL,      /*  0x28: FIFF Control          */
    FIFF_OFFSET,    /*  0x29: FIFF Offset           */
    CTUNE_CTRL,     /*  0x2A: Reserved              */
    CTUNE_OV,       /*  0x2B: Reserved              */
    CTRL_2C,        /*  0x2C: Reserved              */
    FIFF_CTRL2,     /*  0x2D: Fiff Control          */
    RSVD_2E,        /*  0x2E: Reserved              */
    DNC_GAIN,       /*  0x2F: DNC Control           */
    VGA_GAIN,       /*  0x30: VGA Gain Ctrl         */
    RSVD_31,        /*  0x31: Reserved              */
    TEMP_SEL,       /*  0x32: Temperature Selection */
    RSVD_33,        /*  0x33: Reserved              */
    FN_CTRL,        /*  0x34: FracN Control         */
    RSVD_35,        /*  0x35: Reserved              */
    RSVD_36,        /*  0x36: Reserved              */
    RSVD_37,        /*  0x37: Reserved              */
    RSVD_38,        /*  0x38: Reserved              */
    RSVD_39,        /*  0x39: Reserved              */
    RSVD_3A,        /*  0x3A: Reserved              */
    RSVD_3B,        /*  0x3B: Reserved              */
    RSVD_3C,        /*  0x3C: Reserved              */
    END_REGS
};


/*
** Constants used by the tuning algorithm
*/
#define REF_FREQ          (16000000)  /* Reference oscillator Frequency (in Hz) */
#define IF1_BW            (22000000)  /* The IF1 filter bandwidth (in Hz) */
#define TUNE_STEP_SIZE       (62500)  /* Tune in steps of 62.5 kHz */
#define SPUR_STEP_HZ        (250000)  /* Step size (in Hz) to move IF1 when avoiding spurs */
#define ZIF_BW             (2000000)  /* Zero-IF spur-free bandwidth (in Hz) */
#define MAX_HARMONICS_1         (15)  /* Highest intra-tuner LO Spur Harmonic to be avoided */
#define MAX_HARMONICS_2          (5)  /* Highest inter-tuner LO Spur Harmonic to be avoided */
#define MIN_LO_SEP         (1000000)  /* Minimum inter-tuner LO frequency separation */
#define LO1_FRACN_AVOID          (0)  /* LO1 FracN numerator avoid region (in Hz) */
#define LO2_FRACN_AVOID     (199999)  /* LO2 FracN numerator avoid region (in Hz) */
#define MIN_FIN_FREQ      (44000000)  /* Minimum input frequency (in Hz) */
#define MAX_FIN_FREQ    (1100000000)  /* Maximum input frequency (in Hz) */
#define DEF_FIN_FREQ     (666000000)  /* Default frequency at initialization (in Hz) */
#define MIN_FOUT_FREQ     (36000000)  /* Minimum output frequency (in Hz) */
#define MAX_FOUT_FREQ     (57000000)  /* Maximum output frequency (in Hz) */
#define MIN_DNC_FREQ    (1293000000)  /* Minimum LO2 frequency (in Hz) */
#define MAX_DNC_FREQ    (1614000000)  /* Maximum LO2 frequency (in Hz) */
#define MIN_UPC_FREQ    (1396000000)  /* Minimum LO1 frequency (in Hz) */
#define MAX_UPC_FREQ    (2750000000U) /* Maximum LO1 frequency (in Hz) */


typedef struct
{
    Handle_t    handle;
    Handle_t    hUserData;
    UData_t     address;
    UData_t     version;
    UData_t     tuner_id;
    MT2063_AvoidSpursData_t AS_Data;
    UData_t     f_IF1_actual;
    UData_t     rcvr_mode;
    UData_t     ctfilt_sw;
    UData_t     CTFiltMax[31];
    UData_t     num_regs;
    U8Data      reg[END_REGS];
}  MT2063_Info_t;

static UData_t nMaxTuners = MT2063_CNT;
static MT2063_Info_t MT2063_Info[MT2063_CNT];
static MT2063_Info_t *Avail[MT2063_CNT];
static UData_t nOpenTuners = 0;


/*
**  Constants for setting receiver modes.
**  (6 modes defined at this time, enumerated by MT2063_RCVR_MODES)
**  (DNC1GC & DNC2GC are the values, which are used, when the specific
**   DNC Output is selected, the other is always off)
**
**   If PAL-L or L' is received, set:
**       MT2063_SetParam(hMT2063,MT2063_TAGC,1);
**
**                --------------+----------------------------------------------
**                 Mode 0 :     | MT2063_CABLE_QAM
**                 Mode 1 :     | MT2063_CABLE_ANALOG
**                 Mode 2 :     | MT2063_OFFAIR_COFDM
**                 Mode 3 :     | MT2063_OFFAIR_COFDM_SAWLESS
**                 Mode 4 :     | MT2063_OFFAIR_ANALOG
**                 Mode 5 :     | MT2063_OFFAIR_8VSB
**                --------------+----+----+----+----+-----+-----+--------------
**                 Mode         |  0 |  1 |  2 |  3 |  4  |  5  |
**                --------------+----+----+----+----+-----+-----+
**
**
*/
static const U8Data RFAGCEN[]  = {  0,   0,   0,   0,   0,   0 };
static const U8Data LNARIN[]   = {  0,   0,   3,   3,   3,   3 };
static const U8Data FIFFQEN[]  = {  1,   1,   1,   1,   1,   1 };
static const U8Data FIFFQ[]    = {  0,   0,   0,   0,   0,   0 };
static const U8Data DNC1GC[]   = {  0,   0,   0,   0,   0,   0 };
static const U8Data DNC2GC[]   = {  0,   0,   0,   0,   0,   0 };
static const U8Data ACLNAMAX[] = { 31,  31,  31,  31,  31,  31 };
static const U8Data LNATGT[]   = { 44,  43,  43,  43,  43,  43 };
static const U8Data RFOVDIS[]  = {  0,   0,   0,   0,   0,   0 };
static const U8Data ACRFMAX[]  = { 31,  31,  31,  31,  31,  31 };
static const U8Data PD1TGT[]   = { 36,  36,  38,  38,  36,  38 };
static const U8Data FIFOVDIS[] = {  0,   0,   0,   0,   0,   0 };
static const U8Data ACFIFMAX[] = { 29,   0,  29,  29,   0,  29 };
static const U8Data PD2TGT[]   = { 40,  34,  38,  42,  34,  38 };


static const UData_t df_dosc[] = {
     55000, 127000, 167000, 210000,  238000, 267000, 312000, 350000,
    372000, 396000, 410000, 421000,  417000, 408000, 387000, 278000,
    769000, 816000, 824000, 848000,  903000, 931000, 934000, 961000,
    969000, 987000,1005000,1017000, 1006000, 840000, 850000, 0
    };


/*  Forward declaration(s):  */
static UData_t CalcLO1Mult(UData_t *Div, UData_t *FracN, UData_t f_LO, UData_t f_LO_Step, UData_t f_Ref);
static UData_t CalcLO2Mult(UData_t *Div, UData_t *FracN, UData_t f_LO, UData_t f_LO_Step, UData_t f_Ref);
static UData_t fLO_FractionalTerm(UData_t f_ref, UData_t num, UData_t denom);
static UData_t FindClearTuneFilter(MT2063_Info_t* pInfo, UData_t f_in);


/******************************************************************************
**
**  Name: MT2063_Open
**
**  Description:    Initialize the tuner's register values.
**
**  Parameters:     MT2063_Addr  - Serial bus address of the tuner.
**                  hMT2063      - Tuner handle passed back.
**                  hUserData    - User-defined data, if needed for the
**                                 MT_ReadSub() & MT_WriteSub functions.
**
**  Returns:        status:
**                      MT_OK             - No errors
**                      MT_ARG_NULL       - Null pointer argument passed
**                      MT_COMM_ERR       - Serial bus communications error
**                      MT_TUNER_CNT_ERR  - Too many tuners open
**                      MT_TUNER_ID_ERR   - Tuner Part/Rev code mismatch
**                      MT_TUNER_INIT_ERR - Tuner initialization failed
**
**  Dependencies:   MT_ReadSub  - Read byte(s) of data from the two-wire bus
**                  MT_WriteSub - Write byte(s) of data to the two-wire bus
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**
******************************************************************************/
UData_t MT2063_Open(UData_t MT2063_Addr,
                    Handle_t* hMT2063,
                    Handle_t hUserData)
{
    UData_t status = MT_OK;                        /*  Status to be returned.  */
    SData_t i;
    MT2063_Info_t* pInfo = MT_NULL;

    /*  Check the argument before using  */
    if (hMT2063 == MT_NULL)
        return (UData_t)MT_ARG_NULL;

    /*  Default tuner handle to NULL.  If successful, it will be reassigned  */
    *hMT2063 = MT_NULL;

    /*
    **  If this is our first tuner, initialize the address fields and
    **  the list of available control blocks.
    */
    if (nOpenTuners == 0)
    {
        for (i=MT2063_CNT-1; i>=0; i--)
        {
            MT2063_Info[i].handle = MT_NULL;
            MT2063_Info[i].address = (UData_t)MAX_UDATA;
            MT2063_Info[i].rcvr_mode = MT2063_CABLE_QAM;
            MT2063_Info[i].hUserData = MT_NULL;
            Avail[i] = &MT2063_Info[i];
        }
    }

    /*
    **  Look for an existing MT2063_State_t entry with this address.
    */
    for (i=MT2063_CNT-1; i>=0; i--)
    {
        /*
        **  If an open'ed handle provided, we'll re-initialize that structure.
        **
        **  We recognize an open tuner because the address and hUserData are
        **  the same as one that has already been opened
        */
        if ((MT2063_Info[i].address == MT2063_Addr) &&
            (MT2063_Info[i].hUserData == hUserData))
        {
            pInfo = &MT2063_Info[i];
            break;
        }
    }

    /*  If not found, choose an empty spot.  */
    if (pInfo == MT_NULL)
    {
        /*  Check to see that we're not over-allocating  */
        if (nOpenTuners == MT2063_CNT)
            return (UData_t)MT_TUNER_CNT_ERR;

        /* Use the next available block from the list */
        pInfo = Avail[nOpenTuners];
        nOpenTuners++;
    }

    if (MT_NO_ERROR(status))
        status |= MT2063_RegisterTuner(&pInfo->AS_Data);

    if (MT_NO_ERROR(status))
    {
        pInfo->handle    = (Handle_t) pInfo;
        pInfo->hUserData = hUserData;
        pInfo->address   = MT2063_Addr;
        pInfo->rcvr_mode = MT2063_CABLE_QAM;
//        status |= MT2063_ReInit((Handle_t) pInfo);
    }

    if (MT_IS_ERROR(status))
        /*  MT2063_Close handles the un-registration of the tuner  */
        (void)MT2063_Close((Handle_t) pInfo);
    else
        *hMT2063 = pInfo->handle;

    return (UData_t)(status);
}


static UData_t IsValidHandle(MT2063_Info_t* handle)
{
    return (UData_t)(((handle != MT_NULL) && (handle->handle == handle)) ? 1 : 0);
}


/******************************************************************************
**
**  Name: MT2063_Close
**
**  Description:    Release the handle to the tuner.
**
**  Parameters:     hMT2063      - Handle to the MT2063 tuner
**
**  Returns:        status:
**                      MT_OK         - No errors
**                      MT_INV_HANDLE - Invalid tuner handle
**
**  Dependencies:   mt_errordef.h - definition of error codes
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**
******************************************************************************/
UData_t MT2063_Close(Handle_t hMT2063)
{
    MT2063_Info_t* pInfo = (MT2063_Info_t*) hMT2063;

    if (!IsValidHandle(pInfo))
        return (UData_t)MT_INV_HANDLE;

    /* Unregister tuner with SpurAvoidance routines (if needed) */
    MT2063_UnRegisterTuner(&pInfo->AS_Data);

    /* Now remove the tuner from our own list of tuners */
    pInfo->handle    = MT_NULL;
    pInfo->address   = (UData_t)MAX_UDATA;
    pInfo->hUserData = MT_NULL;
    nOpenTuners--;
    Avail[nOpenTuners] = pInfo; /* Return control block to available list */

    return (UData_t)MT_OK;
}


/******************************************************************************
**
**  Name: MT2063_GetGPIO
**
**  Description:    Get the current MT2063 GPIO value.
**
**  Parameters:     h            - Open handle to the tuner (from MT2063_Open).
**                  gpio_id      - Selects GPIO0, GPIO1 or GPIO2
**                  attr         - Selects input readback, I/O direction or
**                                 output value (MT2063_GPIO_IN,
**                                 MT2063_GPIO_DIR or MT2063_GPIO_OUT)
**                  *value       - current setting of GPIO pin
**
**  Usage:          status = MT2063_GetGPIO(hMT2063, MT2063_GPIO0,
**                                          MT2063_GPIO_OUT, &value);
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_COMM_ERR      - Serial bus communications error
**                      MT_INV_HANDLE    - Invalid tuner handle
**                      MT_ARG_NULL      - Null pointer argument passed
**
**  Dependencies:   MT_ReadSub  - Read byte(s) of data from the serial bus
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**
******************************************************************************/
UData_t MT2063_GetGPIO(Handle_t h,
                       MT2063_GPIO_ID gpio_id,
                       MT2063_GPIO_Attr attr,
                       UData_t* value)
{
    UData_t status = MT_OK;                  /* Status to be returned        */
    U8Data  regno;
    SData_t shift;
    const U8Data GPIOreg[3] = {RF_STATUS, FIF_OV, RF_OV};
    MT2063_Info_t* pInfo = (MT2063_Info_t*) h;

    if (IsValidHandle(pInfo) == 0)
        return (UData_t)MT_INV_HANDLE;

    if (value == MT_NULL)
        return (UData_t)MT_ARG_NULL;

    regno = GPIOreg[attr];

    /*  We'll read the register just in case the write didn't work last time */
    status = MT2063_ReadSub(pInfo->hUserData, pInfo->address, regno, &pInfo->reg[regno], 1);

    shift = (SData_t)(gpio_id - MT2063_GPIO0 + 5);
    *value = (pInfo->reg[regno] >> shift) & 1;

    return (UData_t)(status);
}


/****************************************************************************
**
**  Name: MT2063_GetLocked
**
**  Description:    Checks to see if LO1 and LO2 are locked.
**
**  Parameters:     h            - Open handle to the tuner (from MT2063_Open).
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_UPC_UNLOCK    - Upconverter PLL unlocked
**                      MT_DNC_UNLOCK    - Downconverter PLL unlocked
**                      MT_COMM_ERR      - Serial bus communications error
**                      MT_INV_HANDLE    - Invalid tuner handle
**
**  Dependencies:   MT_ReadSub    - Read byte(s) of data from the serial bus
**                  MT_Sleep      - Delay execution for x milliseconds
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**
****************************************************************************/
UData_t MT2063_GetLocked(Handle_t h)
{
    const UData_t nMaxWait = 100;            /*  wait a maximum of 100 msec   */
    const UData_t nPollRate = 2;             /*  poll status bits every 2 ms */
    const UData_t nMaxLoops = nMaxWait / nPollRate;
    const U8Data LO1LK = 0x80;
    U8Data LO2LK = 0x08;
    UData_t status = MT_OK;                  /* Status to be returned        */
    UData_t nDelays = 0;
    MT2063_Info_t* pInfo = (MT2063_Info_t*) h;

    if (IsValidHandle(pInfo) == 0)
        return (UData_t)MT_INV_HANDLE;

    /*  LO2 Lock bit was in a different place for B0 version  */
    if (pInfo->tuner_id == MT2063_B0)
        LO2LK = 0x40;

    do
    {
        status |= MT2063_ReadSub(pInfo->hUserData, pInfo->address, LO_STATUS, &pInfo->reg[LO_STATUS], 1);

        if (MT_IS_ERROR(status))
            return (UData_t)(status);

        if ((pInfo->reg[LO_STATUS] & (LO1LK | LO2LK)) == (LO1LK | LO2LK))
            return (UData_t)(status);

        MT2063_Sleep(pInfo->hUserData, nPollRate);       /*  Wait between retries  */
    }
    while (++nDelays < nMaxLoops);

    if ((pInfo->reg[LO_STATUS] & LO1LK) == 0x00)
        status |= MT_UPC_UNLOCK;
    if ((pInfo->reg[LO_STATUS] & LO2LK) == 0x00)
        status |= MT_DNC_UNLOCK;

    return (UData_t)(status);
}


/****************************************************************************
**
**  Name: MT2063_GetParam
**
**  Description:    Gets a tuning algorithm parameter.
**
**                  This function provides access to the internals of the
**                  tuning algorithm - mostly for testing purposes.
**
**  Parameters:     h           - Tuner handle (returned by MT2063_Open)
**                  param       - Tuning algorithm parameter
**                                (see enum MT2063_Param)
**                  pValue      - ptr to returned value
**
**                  param                     Description
**                  ----------------------    --------------------------------
**                  MT2063_VERSION            Version of the API
**                  MT2063_IC_ADDR            Serial Bus address of this tuner
**                  MT2063_IC_REV             Tuner revision code
**                  MT2063_MAX_OPEN           Max # of MT2063's allowed open
**                  MT2063_NUM_OPEN           # of MT2063's open
**                  MT2063_SRO_FREQ           crystal frequency
**                  MT2063_STEPSIZE           minimum tuning step size
**                  MT2063_INPUT_FREQ         input center frequency
**                  MT2063_LO1_FREQ           LO1 Frequency
**                  MT2063_LO1_STEPSIZE       LO1 minimum step size
**                  MT2063_LO1_FRACN_AVOID    LO1 FracN keep-out region
**                  MT2063_IF1_ACTUAL         Current 1st IF in use
**                  MT2063_IF1_REQUEST        Requested 1st IF
**                  MT2063_IF1_CENTER         Center of 1st IF SAW filter
**                  MT2063_IF1_BW             Bandwidth of 1st IF SAW filter
**                  MT2063_ZIF_BW             zero-IF bandwidth
**                  MT2063_LO2_FREQ           LO2 Frequency
**                  MT2063_LO2_STEPSIZE       LO2 minimum step size
**                  MT2063_LO2_FRACN_AVOID    LO2 FracN keep-out region
**                  MT2063_OUTPUT_FREQ        output center frequency
**                  MT2063_OUTPUT_BW          output bandwidth
**                  MT2063_LO_SEPARATION      min inter-tuner LO separation
**                  MT2063_AS_ALG             ID of avoid-spurs algorithm in use
**                  MT2063_MAX_HARM1          max # of intra-tuner harmonics
**                  MT2063_MAX_HARM2          max # of inter-tuner harmonics
**                  MT2063_EXCL_ZONES         # of 1st IF exclusion zones
**                  MT2063_NUM_SPURS          # of spurs found/avoided
**                  MT2063_SPUR_AVOIDED       >0 spurs avoided
**                  MT2063_SPUR_PRESENT       >0 spurs in output (mathematically)
**                  MT2063_RCVR_MODE          Predefined modes.
**                  MT2063_LNA_RIN            Get LNA RIN value
**                  MT2063_LNA_TGT            Get target power level at LNA
**                  MT2063_PD1_TGT            Get target power level at PD1
**                  MT2063_PD2_TGT            Get target power level at PD2
**                  MT2063_ACLNA              LNA attenuator gain code
**                  MT2063_ACRF               RF attenuator gain code
**                  MT2063_ACFIF              FIF attenuator gain code
**                  MT2063_ACLNA_MAX          LNA attenuator limit
**                  MT2063_ACRF_MAX           RF attenuator limit
**                  MT2063_ACFIF_MAX          FIF attenuator limit
**                  MT2063_PD1                Actual value of PD1
**                  MT2063_PD2                Actual value of PD2
**                  MT2063_DNC_OUTPUT_ENABLE  DNC output selection
**                  MT2063_VGAGC              VGA gain code
**                  MT2063_VGAOI              VGA output current
**                  MT2063_TAGC               TAGC setting
**                  MT2063_AMPGC              AMP gain code
**                  MT2063_AVOID_DECT         Avoid DECT Frequencies
**                  MT2063_CTFILT_SW          Cleartune filter selection
**
**  Usage:          status |= MT2063_GetParam(hMT2063,
**                                            MT2063_IF1_ACTUAL,
**                                            &f_IF1_Actual);
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_INV_HANDLE    - Invalid tuner handle
**                      MT_ARG_NULL      - Null pointer argument passed
**                      MT_ARG_RANGE     - Invalid parameter requested
**
**  Dependencies:   USERS MUST CALL MT2063_Open() FIRST!
**
**  See Also:       MT2063_SetParam, MT2063_Open
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**   154   09-13-2007    RSK    Ver 1.05: Get/SetParam changes for LOx_FREQ
**         10-31-2007    PINZ   Ver 1.08: Get/SetParam add VGAGC, VGAOI, AMPGC, TAGC
**   173 M 01-23-2008    RSK    Ver 1.12: Read LO1C and LO2C registers from HW
**                                        in GetParam.
**         04-18-2008    PINZ   Ver 1.15: Add SetParam LNARIN & PDxTGT
**                                        Split SetParam up to ACLNA / ACLNA_MAX
**                                        removed ACLNA_INRC/DECR (+RF & FIF)
**                                        removed GCUAUTO / BYPATNDN/UP
**   175 I 16-06-2008    PINZ   Ver 1.16: Add control to avoid US DECT freqs.
**   175 I 06-19-2008    RSK    Ver 1.17: Refactor DECT control to SpurAvoid.
**         06-24-2008    PINZ   Ver 1.18: Add Get/SetParam CTFILT_SW
**         07-10-2008    PINZ   Ver 1.19: Documentation Updates, Add a GetParam
**                                        for each SetParam (LNA_RIN, TGTs)
**   N/A I 03-19-2009    PINZ   Ver 1.32: Add GetParam VERSION (of API)
**                                        Add GetParam IC_REV
**   N/A I 04-22-2009    PINZ   Ver 1.33: Small fix in GetParam PD1/PD2
****************************************************************************/
UData_t MT2063_GetParam(Handle_t     h,
                        MT2063_Param param,
                        UData_t*     pValue)
{
    UData_t status = MT_OK;                  /* Status to be returned        */
    MT2063_Info_t* pInfo = (MT2063_Info_t*) h;
    UData_t Div;
    UData_t Num;
    UData_t i;
    U8Data reg;

    if (pValue == MT_NULL)
        status |= MT_ARG_NULL;

    /*  Verify that the handle passed points to a valid tuner         */
    if (IsValidHandle(pInfo) == 0)
        status |= MT_INV_HANDLE;

    if (MT_NO_ERROR(status))
    {
        *pValue = 0;

        switch (param)
        {
        /*  version of the API, e.g. 10302 = 1.32 */
        case MT2063_VERSION:
            *pValue = pInfo->version;
            break;

        /*  Serial Bus address of this tuner      */
        case MT2063_IC_ADDR:
            *pValue = pInfo->address;
            break;

        /*  tuner revision code (see enum  MT2063_REVCODE for values) */
        case MT2063_IC_REV:
            *pValue = pInfo->tuner_id;
            break;

        /*  Max # of MT2063's allowed to be open  */
        case MT2063_MAX_OPEN:
            *pValue = nMaxTuners;
            break;

        /*  # of MT2063's open                    */
        case MT2063_NUM_OPEN:
            *pValue = nOpenTuners;
            break;

        /*  crystal frequency                     */
        case MT2063_SRO_FREQ:
            *pValue = pInfo->AS_Data.f_ref;
            break;

        /*  minimum tuning step size              */
        case MT2063_STEPSIZE:
            *pValue = pInfo->AS_Data.f_LO2_Step;
            break;

        /*  input center frequency                */
        case MT2063_INPUT_FREQ:
            *pValue = pInfo->AS_Data.f_in;
            break;

        /*  LO1 Frequency                         */
        case MT2063_LO1_FREQ:
            {
                /* read the actual tuner register values for LO1C_1 and LO1C_2 */
                status |= MT2063_ReadSub(pInfo->hUserData, pInfo->address, LO1C_1, &pInfo->reg[LO1C_1], 2);
                Div = pInfo->reg[LO1C_1];
                Num = pInfo->reg[LO1C_2] & 0x3F;
                pInfo->AS_Data.f_LO1 = (pInfo->AS_Data.f_ref * Div) + fLO_FractionalTerm(pInfo->AS_Data.f_ref, Num, 64);
                *pValue = pInfo->AS_Data.f_LO1;
            }
            break;

        /*  LO1 minimum step size                 */
        case MT2063_LO1_STEPSIZE:
            *pValue = pInfo->AS_Data.f_LO1_Step;
            break;

        /*  LO1 FracN keep-out region             */
        case MT2063_LO1_FRACN_AVOID:
            *pValue = pInfo->AS_Data.f_LO1_FracN_Avoid;
            break;

        /*  Current 1st IF in use                 */
        case MT2063_IF1_ACTUAL:
            *pValue = pInfo->f_IF1_actual;
            break;

        /*  Requested 1st IF                      */
        case MT2063_IF1_REQUEST:
            *pValue = pInfo->AS_Data.f_if1_Request;
            break;

        /*  Center of 1st IF SAW filter           */
        case MT2063_IF1_CENTER:
            *pValue = pInfo->AS_Data.f_if1_Center;
            break;

        /*  Bandwidth of 1st IF SAW filter        */
        case MT2063_IF1_BW:
            *pValue = pInfo->AS_Data.f_if1_bw;
            break;

        /*  zero-IF bandwidth                     */
        case MT2063_ZIF_BW:
            *pValue = pInfo->AS_Data.f_zif_bw;
            break;

        /*  LO2 Frequency                         */
        case MT2063_LO2_FREQ:
            {
                /* Read the actual tuner register values for LO2C_1, LO2C_2 and LO2C_3 */
                status |= MT2063_ReadSub(pInfo->hUserData, pInfo->address, LO2C_1, &pInfo->reg[LO2C_1], 3);
                Div =  (pInfo->reg[LO2C_1] & 0xFE ) >> 1;
                Num = ((pInfo->reg[LO2C_1] & 0x01 ) << 12) | (pInfo->reg[LO2C_2] << 4) | (pInfo->reg[LO2C_3] & 0x00F);
                pInfo->AS_Data.f_LO2 = (pInfo->AS_Data.f_ref * Div) + fLO_FractionalTerm(pInfo->AS_Data.f_ref, Num, 8191);
                *pValue = pInfo->AS_Data.f_LO2;
            }
            break;

        /*  LO2 minimum step size                 */
        case MT2063_LO2_STEPSIZE:
            *pValue = pInfo->AS_Data.f_LO2_Step;
            break;

        /*  LO2 FracN keep-out region             */
        case MT2063_LO2_FRACN_AVOID:
            *pValue = pInfo->AS_Data.f_LO2_FracN_Avoid;
            break;

        /*  output center frequency               */
        case MT2063_OUTPUT_FREQ:
            *pValue = pInfo->AS_Data.f_out;
            break;

        /*  output bandwidth                      */
        case MT2063_OUTPUT_BW:
            *pValue = pInfo->AS_Data.f_out_bw - 750000;
            break;

        /*  min inter-tuner LO separation         */
        case MT2063_LO_SEPARATION:
            *pValue = pInfo->AS_Data.f_min_LO_Separation;
            break;

        /*  ID of avoid-spurs algorithm in use    */
        case MT2063_AS_ALG:
            *pValue = pInfo->AS_Data.nAS_Algorithm;
            break;

        /*  max # of intra-tuner harmonics        */
        case MT2063_MAX_HARM1:
            *pValue = pInfo->AS_Data.maxH1;
            break;

        /*  max # of inter-tuner harmonics        */
        case MT2063_MAX_HARM2:
            *pValue = pInfo->AS_Data.maxH2;
            break;

        /*  # of 1st IF exclusion zones           */
        case MT2063_EXCL_ZONES:
            *pValue = pInfo->AS_Data.nZones;
            break;

        /*  # of spurs found/avoided              */
        case MT2063_NUM_SPURS:
            *pValue = pInfo->AS_Data.nSpursFound;
            break;

        /*  >0 spurs avoided                      */
        case MT2063_SPUR_AVOIDED:
            *pValue = pInfo->AS_Data.bSpurAvoided;
            break;

        /*  >0 spurs in output (mathematically)   */
        case MT2063_SPUR_PRESENT:
            *pValue = pInfo->AS_Data.bSpurPresent;
            break;

        /*  Predefined receiver setup combination */
        case MT2063_RCVR_MODE:
            *pValue = pInfo->rcvr_mode;
            break;

        case MT2063_PD1:
        case MT2063_PD2:
            {
                reg = pInfo->reg[BYP_CTRL] | ( (param == MT2063_PD1 ? 0x01 : 0x03) & 0x03 );  /* PD1 vs PD2 */

                /* Initiate ADC output to reg 0x0A */
                if (reg != pInfo->reg[BYP_CTRL])
                    status |= MT2063_WriteSub(pInfo->hUserData, pInfo->address, BYP_CTRL, &reg, 1);

                if (MT_IS_ERROR(status))
                    return (UData_t)(status);

                for (i=0; i<8; i++) {
                    status |= MT2063_ReadSub(pInfo->hUserData, pInfo->address, ADC_OUT, &pInfo->reg[ADC_OUT], 1);

                    if (MT_NO_ERROR(status))
                        *pValue += (pInfo->reg[ADC_OUT] >> 2);   /*  only want 6 MSB's out of 8  */
                    else
                        break;  /* break for-loop */
                }
                *pValue /= (i+1);     /*  divide by number of reads  */

                /* Restore value of Register BYP_CTRL */
                if (reg != pInfo->reg[BYP_CTRL])
                    status |= MT2063_WriteSub(pInfo->hUserData, pInfo->address, BYP_CTRL, &pInfo->reg[BYP_CTRL], 1);
            }
            break;


        /*  Get LNA RIN value                  */
        case MT2063_LNA_RIN:
            status |= (pInfo->reg[CTRL_2C] & 0x03);
            break;

        /*  Get LNA target value                  */
        case MT2063_LNA_TGT:
            status |= (pInfo->reg[LNA_TGT] & 0x3f);
            break;

        /*  Get PD1 target value                  */
        case MT2063_PD1_TGT:
            status |= (pInfo->reg[PD1_TGT] & 0x3f);
            break;

        /*  Get PD2 target value                  */
        case MT2063_PD2_TGT:
            status |= (pInfo->reg[PD2_TGT] & 0x3f);
            break;

        /*  Get LNA attenuator code                */
        case MT2063_ACLNA:
            {
                status |= MT2063_GetReg(pInfo, XO_STATUS, &reg);
                *pValue = reg & 0x1f;
            }
            break;

        /*  Get RF attenuator code                */
        case MT2063_ACRF:
            {
                status |= MT2063_GetReg(pInfo, RF_STATUS, &reg);
                *pValue = reg & 0x1f;
            }
            break;

        /*  Get FIF attenuator code               */
        case MT2063_ACFIF:
            {
                status |= MT2063_GetReg(pInfo, FIF_STATUS, &reg);
                *pValue = reg & 0x1f;
            }
            break;

        /*  Get LNA attenuator limit              */
        case MT2063_ACLNA_MAX:
            status |= (pInfo->reg[LNA_OV] & 0x1f);
            break;

        /*  Get RF attenuator limit               */
        case MT2063_ACRF_MAX:
            status |= (pInfo->reg[RF_OV] & 0x1f);
            break;

        /*  Get FIF attenuator limit               */
        case MT2063_ACFIF_MAX:
            status |= (pInfo->reg[FIF_OV] & 0x1f);
            break;

        /*  Get current used DNC output */
        case MT2063_DNC_OUTPUT_ENABLE:
            {
                if ( (pInfo->reg[DNC_GAIN] & 0x03) == 0x03)   /* if DNC1 is off */
                {
                    if ( (pInfo->reg[VGA_GAIN] & 0x03) == 0x03) /* if DNC2 is off */
                        *pValue = (UData_t)MT2063_DNC_NONE;
                    else
                        *pValue = (UData_t)MT2063_DNC_2;
                }
                else /* DNC1 is on */
                {
                    if ( (pInfo->reg[VGA_GAIN] & 0x03) == 0x03) /* if DNC2 is off */
                        *pValue = (UData_t)MT2063_DNC_1;
                    else
                        *pValue = (UData_t)MT2063_DNC_BOTH;
                }
            }
            break;

       /*  Get VGA Gain Code */
        case MT2063_VGAGC:
           *pValue = ( (pInfo->reg[VGA_GAIN] & 0x0C) >> 2 );
            break;

       /*  Get VGA bias current */
        case MT2063_VGAOI:
            *pValue = (pInfo->reg[RSVD_31] & 0x07);
            break;

       /*  Get TAGC setting */
        case MT2063_TAGC:
            *pValue = (pInfo->reg[RSVD_1E] & 0x03);
            break;

       /*  Get AMP Gain Code */
        case MT2063_AMPGC:
           *pValue = (pInfo->reg[TEMP_SEL] & 0x03);
            break;

       /*  Avoid DECT Frequencies  */
        case MT2063_AVOID_DECT:
            *pValue = pInfo->AS_Data.avoidDECT;
            break;

       /*  Cleartune filter selection: 0 - by IC (default), 1 - by software  */
        case MT2063_CTFILT_SW:
            *pValue = pInfo->ctfilt_sw;
            break;

        case MT2063_EOP:
        default:
            status |= MT_ARG_RANGE;
        }
    }
    return (UData_t)(status);
}


/****************************************************************************
**
**  Name: MT2063_GetReg
**
**  Description:    Gets an MT2063 register.
**
**  Parameters:     h           - Tuner handle (returned by MT2063_Open)
**                  reg         - MT2063 register/subaddress location
**                  *val        - MT2063 register/subaddress value
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_COMM_ERR      - Serial bus communications error
**                      MT_INV_HANDLE    - Invalid tuner handle
**                      MT_ARG_NULL      - Null pointer argument passed
**                      MT_ARG_RANGE     - Argument out of range
**
**  Dependencies:   USERS MUST CALL MT2063_Open() FIRST!
**
**                  Use this function if you need to read a register from
**                  the MT2063.
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**
****************************************************************************/
UData_t MT2063_GetReg(Handle_t h,
                      U8Data   reg,
                      U8Data*  val)
{
    UData_t status = MT_OK;                  /* Status to be returned        */
    MT2063_Info_t* pInfo = (MT2063_Info_t*) h;

    /*  Verify that the handle passed points to a valid tuner         */
    if (IsValidHandle(pInfo) == 0)
        status |= MT_INV_HANDLE;

    if (val == MT_NULL)
        status |= MT_ARG_NULL;

    if (reg >= END_REGS)
        status |= MT_ARG_RANGE;

    if (MT_NO_ERROR(status))
    {
        status |= MT2063_ReadSub(pInfo->hUserData, pInfo->address, reg, &pInfo->reg[reg], 1);
        if (MT_NO_ERROR(status))
            *val = pInfo->reg[reg];
    }

    return (UData_t)(status);
}


/******************************************************************************
**
**  Name: MT2063_GetTemp
**
**  Description:    Get the MT2063 Temperature register.
**
**  Parameters:     h            - Open handle to the tuner (from MT2063_Open).
**                  *value       - value read from the register
**
**                                    Binary
**                  Value Returned    Value    Approx Temp
**                  ---------------------------------------------
**                  MT2063_T_0C       0000         0C
**                  MT2063_T_10C      0001        10C
**                  MT2063_T_20C      0010        20C
**                  MT2063_T_30C      0011        30C
**                  MT2063_T_40C      0100        40C
**                  MT2063_T_50C      0101        50C
**                  MT2063_T_60C      0110        60C
**                  MT2063_T_70C      0111        70C
**                  MT2063_T_80C      1000        80C
**                  MT2063_T_90C      1001        90C
**                  MT2063_T_100C     1010       100C
**                  MT2063_T_110C     1011       110C
**                  MT2063_T_120C     1100       120C
**                  MT2063_T_130C     1101       130C
**                  MT2063_T_140C     1110       140C
**                  MT2063_T_150C     1111       150C
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_COMM_ERR      - Serial bus communications error
**                      MT_INV_HANDLE    - Invalid tuner handle
**                      MT_ARG_NULL      - Null pointer argument passed
**                      MT_ARG_RANGE     - Argument out of range
**
**  Dependencies:   MT_ReadSub  - Read byte(s) of data from the two-wire bus
**                  MT_WriteSub - Write byte(s) of data to the two-wire bus
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**
******************************************************************************/
UData_t MT2063_GetTemp(Handle_t h, MT2063_Temperature* value)
{
    UData_t status = MT_OK;                  /* Status to be returned        */
    MT2063_Info_t* pInfo = (MT2063_Info_t*) h;

    if (IsValidHandle(pInfo) == 0)
        return (UData_t)MT_INV_HANDLE;

    if (value == MT_NULL)
        return (UData_t)MT_ARG_NULL;

    if ((MT_NO_ERROR(status)) && ((pInfo->reg[TEMP_SEL] & 0xE0) != 0x00))
    {
        pInfo->reg[TEMP_SEL] &= (0x1F);
        status |= MT2063_WriteSub(pInfo->hUserData,
                              pInfo->address,
                              TEMP_SEL,
                              &pInfo->reg[TEMP_SEL],
                              1);
    }

    if (MT_NO_ERROR(status))
        status |= MT2063_ReadSub(pInfo->hUserData,
                             pInfo->address,
                             TEMP_STATUS,
                             &pInfo->reg[TEMP_STATUS],
                             1);

    if (MT_NO_ERROR(status))
        *value = (MT2063_Temperature) (pInfo->reg[TEMP_STATUS] >> 4);

    return (UData_t)(status);
}


/****************************************************************************
**
**  Name: MT2063_GetUserData
**
**  Description:    Gets the user-defined data item.
**
**  Parameters:     h           - Tuner handle (returned by MT2063_Open)
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_INV_HANDLE    - Invalid tuner handle
**                      MT_ARG_NULL      - Null pointer argument passed
**
**  Dependencies:   USERS MUST CALL MT2063_Open() FIRST!
**
**                  The hUserData parameter is a user-specific argument
**                  that is stored internally with the other tuner-
**                  specific information.
**
**                  For example, if additional arguments are needed
**                  for the user to identify the device communicating
**                  with the tuner, this argument can be used to supply
**                  the necessary information.
**
**                  The hUserData parameter is initialized in the tuner's
**                  Open function to NULL.
**
**  See Also:       MT2063_Open
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**
****************************************************************************/
UData_t MT2063_GetUserData(Handle_t h,
                           Handle_t* hUserData)
{
    UData_t status = MT_OK;                  /* Status to be returned        */
    MT2063_Info_t* pInfo = (MT2063_Info_t*) h;

    /*  Verify that the handle passed points to a valid tuner         */
    if (IsValidHandle(pInfo) == 0)
        status |= MT_INV_HANDLE;

    if (hUserData == MT_NULL)
        status |= MT_ARG_NULL;

    if (MT_NO_ERROR(status))
        *hUserData = pInfo->hUserData;

    return (UData_t)(status);
}



/******************************************************************************
**
**  Name: MT2063_SetReceiverMode
**
**  Description:    Set the MT2063 receiver mode
**
**   --------------+----------------------------------------------
**    Mode 0 :     | MT2063_CABLE_QAM
**    Mode 1 :     | MT2063_CABLE_ANALOG
**    Mode 2 :     | MT2063_OFFAIR_COFDM
**    Mode 3 :     | MT2063_OFFAIR_COFDM_SAWLESS
**    Mode 4 :     | MT2063_OFFAIR_ANALOG
**    Mode 5 :     | MT2063_OFFAIR_8VSB
**   --------------+----+----+----+----+-----+--------------------
**  (DNC1GC & DNC2GC are the values, which are used, when the specific
**   DNC Output is selected, the other is always off)
**
**                |<----------   Mode  -------------->|
**    Reg Field   |  0  |  1  |  2  |  3  |  4  |  5  |
**    ------------+-----+-----+-----+-----+-----+-----+
**    RFAGCen     | OFF | OFF | OFF | OFF | OFF | OFF |
**    LNARin      |   0 |   0 |   3 |   3 |  3  |  3  |
**    FIFFQen     |   1 |   1 |   1 |   1 |  1  |  1  |
**    FIFFq       |   0 |   0 |   0 |   0 |  0  |  0  |
**    DNC1gc      |   0 |   0 |   0 |   0 |  0  |  0  |
**    DNC2gc      |   0 |   0 |   0 |   0 |  0  |  0  |
**    LNA max Atn |  31 |  31 |  31 |  31 | 31  | 31  |
**    LNA Target  |  44 |  43 |  43 |  43 | 43  | 43  |
**    ign  RF Ovl |   0 |   0 |   0 |   0 |  0  |  0  |
**    RF  max Atn |  31 |  31 |  31 |  31 | 31  | 31  |
**    PD1 Target  |  36 |  36 |  38 |  38 | 36  | 38  |
**    ign FIF Ovl |   0 |   0 |   0 |   0 |  0  |  0  |
**    FIF max Atn |  29 |   0 |  29 |  29 |  0  | 29  |
**    PD2 Target  |  40 |  34 |  38 |  42 | 34  | 38  |
**
**
**  Parameters:     pInfo       - ptr to MT2063_Info_t structure
**                  Mode        - desired reciever mode
**
**  Usage:          status = MT2063_SetReceiverMode(hMT2063, Mode);
**
**  Returns:        status:
**                      MT_OK             - No errors
**                      MT_COMM_ERR       - Serial bus communications error
**
**  Dependencies:   MT2063_SetReg - Write a byte of data to a HW register.
**                  Assumes that the tuner cache is valid.
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**   N/A   01-10-2007    PINZ   Added additional GCU Settings, FIFF Calib will be triggered
**   155   10-01-2007    DAD    Ver 1.06: Add receiver mode for SECAM positive
**                                        modulation
**                                        (MT2063_ANALOG_TV_POS_NO_RFAGC_MODE)
**   N/A   10-22-2007    PINZ   Ver 1.07: Changed some Registers at init to have
**                                        the same settings as with MT Launcher
**   N/A   10-30-2007    PINZ             Add SetParam VGAGC & VGAOI
**                                        Add SetParam DNC_OUTPUT_ENABLE
**                                        Removed VGAGC from receiver mode,
**                                        default now 1
**   N/A   10-31-2007    PINZ   Ver 1.08: Add SetParam TAGC, removed from rcvr-mode
**                                        Add SetParam AMPGC, removed from rcvr-mode
**                                        Corrected names of GCU values
**                                        reorganized receiver modes, removed,
**                                        (MT2063_ANALOG_TV_POS_NO_RFAGC_MODE)
**                                        Actualized Receiver-Mode values
**   N/A   11-12-2007    PINZ   Ver 1.09: Actualized Receiver-Mode values
**   N/A   11-27-2007    PINZ             Improved buffered writing
**         01-03-2008    PINZ   Ver 1.10: Added a trigger of BYPATNUP for
**                                        correct wakeup of the LNA after shutdown
**                                        Set AFCsd = 1 as default
**                                        Changed CAP1sel default
**         01-14-2008    PINZ   Ver 1.11: Updated gain settings
**         04-18-2008    PINZ   Ver 1.15: Add SetParam LNARIN & PDxTGT
**                                        Split SetParam up to ACLNA / ACLNA_MAX
**                                        removed ACLNA_INRC/DECR (+RF & FIF)
**                                        removed GCUAUTO / BYPATNDN/UP
**         11-05-2008    PINZ   Ver 1.25: Bugfix, update rcvr_mode var earlier.
**   N/A I 02-26-2009    PINZ   Ver 1.30: RCVRmode 2: acfifmax 29->0, pd2tgt 33->34
**                                        RCVRmode 4: acfifmax 29->0, pd2tgt 30->34
**
******************************************************************************/
static UData_t MT2063_SetReceiverMode(MT2063_Info_t* pInfo, MT2063_RCVR_MODES Mode)
{
    UData_t status = MT_OK;                  /* Status to be returned        */
    U8Data  val;
    UData_t longval;

    if (IsValidHandle(pInfo) == 0)
        return (UData_t)MT_INV_HANDLE;

    if (Mode >= MT2063_NUM_RCVR_MODES)
        status = MT_ARG_RANGE;

    if (MT_NO_ERROR(status))
        pInfo->rcvr_mode = Mode;

    /* RFAGCen */
    if (MT_NO_ERROR(status))
    {
        val = (pInfo->reg[PD1_TGT] & (U8Data)~0x40) | (RFAGCEN[Mode] ? 0x40 : 0x00);
        if( pInfo->reg[PD1_TGT] != val )
        {
            status |= MT2063_SetReg(pInfo, PD1_TGT, val);
        }
    }

    /* LNARin */
    if (MT_NO_ERROR(status))
    {
        status |= MT2063_SetParam(pInfo, MT2063_LNA_RIN, LNARIN[Mode]);
    }

    /* FIFFQEN and FIFFQ */
    if (MT_NO_ERROR(status))
    {
        val = (pInfo->reg[FIFF_CTRL2] & (U8Data)~0xF0) | (FIFFQEN[Mode] << 7) | (FIFFQ[Mode] << 4);
        if( pInfo->reg[FIFF_CTRL2] != val )
        {
            status |= MT2063_SetReg(pInfo, FIFF_CTRL2, val);
            /* trigger FIFF calibration, needed after changing FIFFQ */
            val = (pInfo->reg[FIFF_CTRL] | (U8Data)0x01);
            status |= MT2063_SetReg(pInfo, FIFF_CTRL, val);
            val = (pInfo->reg[FIFF_CTRL] & (U8Data)~0x01);
            status |= MT2063_SetReg(pInfo, FIFF_CTRL, val);
        }
    }

    /* DNC1GC & DNC2GC */
    status |= MT2063_GetParam(pInfo, MT2063_DNC_OUTPUT_ENABLE, &longval);
    status |= MT2063_SetParam(pInfo, MT2063_DNC_OUTPUT_ENABLE, longval);

    /* acLNAmax */
    if (MT_NO_ERROR(status))
    {
        status |= MT2063_SetParam(pInfo, MT2063_ACLNA_MAX, ACLNAMAX[Mode]);
    }

    /* LNATGT */
    if (MT_NO_ERROR(status))
    {
        status |= MT2063_SetParam(pInfo, MT2063_LNA_TGT, LNATGT[Mode]);
    }

    /* ACRF */
    if (MT_NO_ERROR(status))
    {
        status |= MT2063_SetParam(pInfo, MT2063_ACRF_MAX, ACRFMAX[Mode]);
    }

    /* PD1TGT */
    if (MT_NO_ERROR(status))
    {
        status |= MT2063_SetParam(pInfo, MT2063_PD1_TGT, PD1TGT[Mode]);
    }

    /* FIFATN */
    if (MT_NO_ERROR(status))
    {
        status |= MT2063_SetParam(pInfo, MT2063_ACFIF_MAX, ACFIFMAX[Mode]);
    }

    /* PD2TGT */
    if (MT_NO_ERROR(status))
    {
        status |= MT2063_SetParam(pInfo, MT2063_PD2_TGT, PD2TGT[Mode]);
    }

    /* Ignore ATN Overload */
    if (MT_NO_ERROR(status))
    {
        val = (pInfo->reg[LNA_TGT] & (U8Data)~0x80) | (RFOVDIS[Mode] ? 0x80 : 0x00);
        if( pInfo->reg[LNA_TGT] != val )
        {
            status |= MT2063_SetReg(pInfo, LNA_TGT, val);
        }
    }

    /* Ignore FIF Overload */
    if (MT_NO_ERROR(status))
    {
        val = (pInfo->reg[PD1_TGT] & (U8Data)~0x80) | (FIFOVDIS[Mode] ? 0x80 : 0x00);
        if( pInfo->reg[PD1_TGT] != val )
        {
            status |= MT2063_SetReg(pInfo, PD1_TGT, val);
        }
    }


    return (UData_t)(status);
}


/******************************************************************************
**
**  Name: MT2063_ReInit
**
**  Description:    Initialize the tuner's register values.
**
**  Parameters:     h           - Tuner handle (returned by MT2063_Open)
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_TUNER_ID_ERR   - Tuner Part/Rev code mismatch
**                      MT_INV_HANDLE    - Invalid tuner handle
**                      MT_COMM_ERR      - Serial bus communications error
**
**  Dependencies:   MT_ReadSub  - Read byte(s) of data from the two-wire bus
**                  MT_WriteSub - Write byte(s) of data to the two-wire bus
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**   148   09-04-2007    RSK    Ver 1.02: Corrected logic of Reg 3B Reference
**   153   09-07-2007    RSK    Ver 1.03: Lock Time improvements
**   N/A   10-31-2007    PINZ   Ver 1.08: Changed values suitable to rcvr-mode 0
**   N/A   11-12-2007    PINZ   Ver 1.09: Changed values suitable to rcvr-mode 0
**   N/A   01-03-2007    PINZ   Ver 1.10: Added AFCsd = 1 into defaults
**   N/A   01-04-2007    PINZ   Ver 1.10: Changed CAP1sel default
**         01-14-2008    PINZ   Ver 1.11: Updated gain settings
**         03-18-2008    PINZ   Ver 1.13: Added Support for B3
**   175 I 06-19-2008    RSK    Ver 1.17: Refactor DECT control to SpurAvoid.
**         06-24-2008    PINZ   Ver 1.18: Add Get/SetParam CTFILT_SW
**         08-05-2008    PINZ   Ver 1.20: Disable SDEXT pin while MT2063_ReInit
**                                        with MT2063B3
**   N/A I 03-02-2009    PINZ   Ver 1.31: new default Fout 43.75 -> 36.125MHz
**                                        new default Output-BW 6 -> 8MHz
**                                        new default Stepsize 50 -> 62.5kHz
**                                        new default Fin 651 -> 666 MHz
**                                        Changed order of receiver-mode init
**   N/A I 04-29-2009    PINZ   Ver 1.34: Optimized ReInit function
**
******************************************************************************/
UData_t MT2063_ReInit(Handle_t h)
{
    U8Data all_resets = 0xF0;                /* reset/load bits */
    UData_t status = MT_OK;                  /* Status to be returned */
    MT2063_Info_t* pInfo = (MT2063_Info_t*) h;
    UData_t rev=0;

    U8Data MT2063B0_defaults[] = { /* Reg,  Value */
                                     0x19, 0x05,
                                     0x1B, 0x1D,
                                     0x1C, 0x1F,
                                     0x1D, 0x0F,
                                     0x1E, 0x3F,
                                     0x1F, 0x0F,
                                     0x20, 0x3F,
                                     0x22, 0x21,
                                     0x23, 0x3F,
                                     0x24, 0x20,
                                     0x25, 0x3F,
                                     0x27, 0xEE,
                                     0x2C, 0x27,  /*  bit at 0x20 is cleared below  */
                                     0x30, 0x03,
                                     0x2C, 0x07,  /*  bit at 0x20 is cleared here   */
                                     0x2D, 0x87,
                                     0x2E, 0xAA,
                                     0x28, 0xE1,  /*  Set the FIFCrst bit here      */
                                     0x28, 0xE0,  /*  Clear the FIFCrst bit here    */
                                     0x00 };

    /* writing 0x05 0xf0 sw-resets all registers, so we write only needed changes */
    U8Data MT2063B1_defaults[] = { /* Reg,  Value */
                                     0x05, 0xF0,
                                     0x11, 0x10,  /* New Enable AFCsd */
                                     0x19, 0x05,
                                     0x1A, 0x6C,
                                     0x1B, 0x24,
                                     0x1C, 0x28,
                                     0x1D, 0x8F,
                                     0x1E, 0x14,
                                     0x1F, 0x8F,
                                     0x20, 0x57,
                                     0x22, 0x21,  /* New - ver 1.03 */
                                     0x23, 0x3C,  /* New - ver 1.10 */
                                     0x24, 0x20,  /* New - ver 1.03 */
                                     0x2C, 0x24,  /*  bit at 0x20 is cleared below  */
                                     0x2D, 0x87,  /*  FIFFQ=0  */
                                     0x2F, 0xF3,
                                     0x30, 0x0C,  /* New - ver 1.11 */
                                     0x31, 0x1B,  /* New - ver 1.11 */
                                     0x2C, 0x04,  /*  bit at 0x20 is cleared here  */
                                     0x28, 0xE1,  /*  Set the FIFCrst bit here      */
                                     0x28, 0xE0,  /*  Clear the FIFCrst bit here    */
                                     0x00 };

    /* writing 0x05 0xf0 sw-resets all registers, so we write only needed changes */
    U8Data MT2063B3_defaults[] = { /* Reg,  Value */
                                     0x05, 0xF0,
                                     0x11, 0x13,  /*  disable SDEXT/INTsd for init */
                                     0x2C, 0x24,  /*  bit at 0x20 is cleared below  */
                                     0x2C, 0x04,  /*  bit at 0x20 is cleared here  */
                                     0x28, 0xE1,  /*  Set the FIFCrst bit here      */
                                     0x28, 0xE0,  /*  Clear the FIFCrst bit here    */
                                     0x00 };

    U8Data *def=MT2063B3_defaults;

    /*  Verify that the handle passed points to a valid tuner         */
    if (IsValidHandle(pInfo) == 0)
        status |= MT_INV_HANDLE;

    /*  Read the Part/Rev code from the tuner */
    if (MT_NO_ERROR(status))
        status |= MT2063_ReadSub(pInfo->hUserData, pInfo->address, PART_REV, pInfo->reg, 1);

    /*  Read the Part/Rev codes (2nd byte) from the tuner */
    if (MT_NO_ERROR(status))
        status |= MT2063_ReadSub(pInfo->hUserData, pInfo->address, RSVD_3B, &pInfo->reg[RSVD_3B], 2);

    if (MT_NO_ERROR(status)) { /* Check the part/rev code */
        switch (pInfo->reg[PART_REV]) {
            case 0x9e : {
                if ( (pInfo->reg[RSVD_3C] & 0x18) == 0x08 ) {
                    rev = MT2063_XX;
                    status |= MT_TUNER_ID_ERR;
                }
                else {
                    rev = MT2063_B3;
                    def = MT2063B3_defaults;
                }
                break;
            }
            case 0x9c : {
                rev = MT2063_B1;
                def = MT2063B1_defaults;
                break;
            }
            case 0x9b : {
                rev = MT2063_B0;
                def = MT2063B0_defaults;
                break;
            }
            default : {
                rev = MT2063_XX;
                status |= MT_TUNER_ID_ERR;
                break;
            }
        }
    }

    if (MT_NO_ERROR(status)  /* Check the 2nd part/rev code */
        && ((pInfo->reg[RSVD_3B] & 0x80) != 0x00))    /* b7 != 0 ==> NOT MT2063 */
            status |= MT_TUNER_ID_ERR;      /*  Wrong tuner Part/Rev code */

    /*  Reset the tuner  */
    if (MT_NO_ERROR(status))
        status |= MT2063_WriteSub(pInfo->hUserData,
                              pInfo->address,
                              LO2CQ_3,
                              &all_resets,
                              1);

    while (MT_NO_ERROR(status) && *def)
    {
        U8Data reg = *def++;
        U8Data val = *def++;
        status |= MT2063_WriteSub(pInfo->hUserData, pInfo->address, reg, &val, 1);
    }

    /*  Wait for FIFF location to complete.  */
    if (MT_NO_ERROR(status))
    {
        UData_t FCRUN = 1;
        SData_t maxReads = 10;
        while (MT_NO_ERROR(status) && (FCRUN != 0) && (maxReads-- > 0))
        {
            MT2063_Sleep(pInfo->hUserData, 2);
            status |= MT2063_ReadSub(pInfo->hUserData,
                                pInfo->address,
                                XO_STATUS,
                                &pInfo->reg[XO_STATUS],
                                1);
            FCRUN = (pInfo->reg[XO_STATUS] & 0x40) >> 6;
        }

        if (FCRUN != 0)
            status |= MT_TUNER_INIT_ERR | MT_TUNER_TIMEOUT;

        if (MT_NO_ERROR(status))  /* Re-read FIFFC value */
            status |= MT2063_ReadSub(pInfo->hUserData, pInfo->address, FIFFC, &pInfo->reg[FIFFC], 1);
    }

    /* Read back all the registers from the tuner */
    if (MT_NO_ERROR(status))
        status |= MT2063_ReadSub(pInfo->hUserData,
                             pInfo->address,
                             PART_REV,
                             pInfo->reg,
                             END_REGS);

    if (MT_NO_ERROR(status))
    {
        /*  Initialize the tuner state.  */
        pInfo->version = MT2063_MODULE_VERSION;
        pInfo->tuner_id = rev;
        pInfo->AS_Data.f_ref = REF_FREQ;
        pInfo->AS_Data.f_if1_Center = (pInfo->AS_Data.f_ref / 8) * ((UData_t) pInfo->reg[FIFFC] + 640);
        pInfo->AS_Data.f_if1_bw = IF1_BW;
        pInfo->AS_Data.f_out = 36125000;
        pInfo->AS_Data.f_out_bw = 8000000;
        pInfo->AS_Data.f_zif_bw = ZIF_BW;
        pInfo->AS_Data.f_LO1_Step = pInfo->AS_Data.f_ref / 64;
        pInfo->AS_Data.f_LO2_Step = TUNE_STEP_SIZE;
        pInfo->AS_Data.maxH1 = MAX_HARMONICS_1;
        pInfo->AS_Data.maxH2 = MAX_HARMONICS_2;
        pInfo->AS_Data.f_min_LO_Separation = MIN_LO_SEP;
        pInfo->AS_Data.f_if1_Request = pInfo->AS_Data.f_if1_Center;
        pInfo->AS_Data.f_LO1 = 2181000000U;
        pInfo->AS_Data.f_LO2 = 1486249786;
        pInfo->f_IF1_actual = pInfo->AS_Data.f_if1_Center;
        pInfo->AS_Data.f_in = pInfo->AS_Data.f_LO1 - pInfo->f_IF1_actual;
        pInfo->AS_Data.f_LO1_FracN_Avoid = LO1_FRACN_AVOID;
        pInfo->AS_Data.f_LO2_FracN_Avoid = LO2_FRACN_AVOID;
        pInfo->num_regs = END_REGS;
        pInfo->AS_Data.avoidDECT = MT_AVOID_BOTH;
        pInfo->ctfilt_sw = 1;
    }

    if (MT_NO_ERROR(status))
    {
        pInfo->CTFiltMax[ 0] =   69422000;
        pInfo->CTFiltMax[ 1] =  106211000;
        pInfo->CTFiltMax[ 2] =  140427000;
        pInfo->CTFiltMax[ 3] =  177240000;
        pInfo->CTFiltMax[ 4] =  213091000;
        pInfo->CTFiltMax[ 5] =  241378000;
        pInfo->CTFiltMax[ 6] =  274596000;
        pInfo->CTFiltMax[ 7] =  309696000;
        pInfo->CTFiltMax[ 8] =  342398000;
        pInfo->CTFiltMax[ 9] =  378728000;
        pInfo->CTFiltMax[10] =  416053000;
        pInfo->CTFiltMax[11] =  456693000;
        pInfo->CTFiltMax[12] =  496105000;
        pInfo->CTFiltMax[13] =  534448000;
        pInfo->CTFiltMax[14] =  572893000;
        pInfo->CTFiltMax[15] =  603218000;
        pInfo->CTFiltMax[16] =  632650000;
        pInfo->CTFiltMax[17] =  668229000;
        pInfo->CTFiltMax[18] =  710828000;
        pInfo->CTFiltMax[19] =  735135000;
        pInfo->CTFiltMax[20] =  765601000;
        pInfo->CTFiltMax[21] =  809919000;
        pInfo->CTFiltMax[22] =  842538000;
        pInfo->CTFiltMax[23] =  863353000;
        pInfo->CTFiltMax[24] =  911285000;
        pInfo->CTFiltMax[25] =  942310000;
        pInfo->CTFiltMax[26] =  977602000;
        pInfo->CTFiltMax[27] = 1015100000;
        pInfo->CTFiltMax[28] = 1053415000;
        pInfo->CTFiltMax[29] = 1098330000;
        pInfo->CTFiltMax[30] = 1138990000;
    }

    /*
    **   Fetch the FCU osc value and use it and the fRef value to
    **   scale all of the Band Max values
    */
    if (MT_NO_ERROR(status))
    {
        UData_t fcu_osc;
        UData_t i;

        pInfo->reg[CTUNE_CTRL] = 0x0A;
        status |= MT2063_WriteSub(pInfo->hUserData, pInfo->address, CTUNE_CTRL, &pInfo->reg[CTUNE_CTRL], 1);

        /*  Read the ClearTune filter calibration value  */
        status |= MT2063_ReadSub(pInfo->hUserData, pInfo->address, FIFFC, &pInfo->reg[FIFFC], 1);
        fcu_osc = pInfo->reg[FIFFC];

        pInfo->reg[CTUNE_CTRL] = 0x00;
        status |= MT2063_WriteSub(pInfo->hUserData, pInfo->address, CTUNE_CTRL, &pInfo->reg[CTUNE_CTRL], 1);

        /*  Adjust each of the values in the ClearTune filter cross-over table  */
        for (i = 0; i < 31; i++)
        {
            if (fcu_osc>127) pInfo->CTFiltMax[i] += ( fcu_osc - 128 ) * df_dosc[i];
            else             pInfo->CTFiltMax[i] -= ( 128 - fcu_osc ) * df_dosc[i];
        }
    }

    /*
    **   Set the default receiver mode
    **
    */

//    if (MT_NO_ERROR(status) )
//    {
//        status |= MT2063_SetParam(h,MT2063_RCVR_MODE,MT2063_CABLE_QAM);
//    }


    /*
    **   Tune to the default frequency
    **
    */

//    if (MT_NO_ERROR(status) )
//    {
//        status |= MT2063_Tune(h,DEF_FIN_FREQ);
//    }

    /*
    **   Enable SDEXT pin again
    **
    */

    if ( (MT_NO_ERROR(status)) && (pInfo->tuner_id >= MT2063_B3) )
    {
        status |= MT2063_SetReg(h,PWR_1,0x1b);
    }

    return (UData_t)(status);
}


/******************************************************************************
**
**  Name: MT2063_SetGPIO
**
**  Description:    Modify the MT2063 GPIO value.
**
**  Parameters:     h            - Open handle to the tuner (from MT2063_Open).
**                  gpio_id      - Selects GPIO0, GPIO1 or GPIO2
**                  attr         - Selects input readback, I/O direction or
**                                 output value
**                  value        - value to set GPIO pin 15, 14 or 19
**
**  Usage:          status = MT2063_SetGPIO(hMT2063, MT2063_GPIO1, MT2063_GPIO_OUT, 1);
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_COMM_ERR      - Serial bus communications error
**                      MT_INV_HANDLE    - Invalid tuner handle
**
**  Dependencies:   MT_WriteSub - Write byte(s) of data to the two-wire-bus
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**
******************************************************************************/
UData_t MT2063_SetGPIO(Handle_t h, MT2063_GPIO_ID gpio_id,
                                   MT2063_GPIO_Attr attr,
                                   UData_t value)
{
    UData_t status = MT_OK;                  /* Status to be returned        */
    U8Data regno;
    SData_t shift;
    const U8Data GPIOreg[3] = {0x15, 0x19, 0x18};
    MT2063_Info_t* pInfo = (MT2063_Info_t*) h;

    if (IsValidHandle(pInfo) == 0)
        return (UData_t)MT_INV_HANDLE;

    regno = GPIOreg[attr];

    shift = (SData_t)(gpio_id - MT2063_GPIO0 + 5);

    if (value & 0x01)
        pInfo->reg[regno] |= (0x01 << shift);
    else
        pInfo->reg[regno] &= ~(0x01 << shift);
    status = MT2063_WriteSub(pInfo->hUserData, pInfo->address, regno, &pInfo->reg[regno], 1);

    return (UData_t)(status);
}


/****************************************************************************
**
**  Name: MT2063_SetParam
**
**  Description:    Sets a tuning algorithm parameter.
**
**                  This function provides access to the internals of the
**                  tuning algorithm.  You can override many of the tuning
**                  algorithm defaults using this function.
**
**  Parameters:     h           - Tuner handle (returned by MT2063_Open)
**                  param       - Tuning algorithm parameter
**                                (see enum MT2063_Param)
**                  nValue      - value to be set
**
**                  param                     Description
**                  ----------------------    --------------------------------
**                  MT2063_SRO_FREQ           crystal frequency
**                  MT2063_STEPSIZE           minimum tuning step size
**                  MT2063_LO1_FREQ           LO1 frequency
**                  MT2063_LO1_STEPSIZE       LO1 minimum step size
**                  MT2063_LO1_FRACN_AVOID    LO1 FracN keep-out region
**                  MT2063_IF1_REQUEST        Requested 1st IF
**                  MT2063_ZIF_BW             zero-IF bandwidth
**                  MT2063_LO2_FREQ           LO2 frequency
**                  MT2063_LO2_STEPSIZE       LO2 minimum step size
**                  MT2063_LO2_FRACN_AVOID    LO2 FracN keep-out region
**                  MT2063_OUTPUT_FREQ        output center frequency
**                  MT2063_OUTPUT_BW          output bandwidth
**                  MT2063_LO_SEPARATION      min inter-tuner LO separation
**                  MT2063_MAX_HARM1          max # of intra-tuner harmonics
**                  MT2063_MAX_HARM2          max # of inter-tuner harmonics
**                  MT2063_RCVR_MODE          Predefined modes
**                  MT2063_LNA_RIN            Set LNA Rin (*)
**                  MT2063_LNA_TGT            Set target power level at LNA (*)
**                  MT2063_PD1_TGT            Set target power level at PD1 (*)
**                  MT2063_PD2_TGT            Set target power level at PD2 (*)
**                  MT2063_ACLNA_MAX          LNA attenuator limit (*)
**                  MT2063_ACRF_MAX           RF attenuator limit (*)
**                  MT2063_ACFIF_MAX          FIF attenuator limit (*)
**                  MT2063_DNC_OUTPUT_ENABLE  DNC output selection
**                  MT2063_VGAGC              VGA gain code
**                  MT2063_VGAOI              VGA output current
**                  MT2063_TAGC               TAGC setting
**                  MT2063_AMPGC              AMP gain code
**                  MT2063_AVOID_DECT         Avoid DECT Frequencies
**                  MT2063_CTFILT_SW          Cleartune filter selection
**
**                  (*) This parameter is set by MT2063_RCVR_MODE, do not call
**                      additionally.
**
**  Usage:          status |= MT2063_SetParam(hMT2063,
**                                            MT2063_STEPSIZE,
**                                            50000);
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_INV_HANDLE    - Invalid tuner handle
**                      MT_ARG_NULL      - Null pointer argument passed
**                      MT_ARG_RANGE     - Invalid parameter requested
**                                         or set value out of range
**                                         or non-writable parameter
**
**  Dependencies:   USERS MUST CALL MT2063_Open() FIRST!
**
**  See Also:       MT2063_GetParam, MT2063_Open
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**   154   09-13-2007    RSK    Ver 1.05: Get/SetParam changes for LOx_FREQ
**         10-31-2007    PINZ   Ver 1.08: Get/SetParam add VGAGC, VGAOI, AMPGC, TAGC
**         04-18-2008    PINZ   Ver 1.15: Add SetParam LNARIN & PDxTGT
**                                        Split SetParam up to ACLNA / ACLNA_MAX
**                                        removed ACLNA_INRC/DECR (+RF & FIF)
**                                        removed GCUAUTO / BYPATNDN/UP
**   175 I 06-06-2008    PINZ   Ver 1.16: Add control to avoid US DECT freqs.
**   175 I 06-19-2008    RSK    Ver 1.17: Refactor DECT control to SpurAvoid.
**         06-24-2008    PINZ   Ver 1.18: Add Get/SetParam CTFILT_SW
**         01-20-2009    PINZ   Ver 1.28: Fixed a compare to avoid compiler warning
**
****************************************************************************/
UData_t MT2063_SetParam(Handle_t     h,
                        MT2063_Param param,
                        UData_t      nValue)
{
    UData_t status = MT_OK;                  /* Status to be returned        */
    U8Data val=0;
    MT2063_Info_t* pInfo = (MT2063_Info_t*) h;

    /*  Verify that the handle passed points to a valid tuner         */
    if (IsValidHandle(pInfo) == 0)
        status |= MT_INV_HANDLE;

    if (MT_NO_ERROR(status))
    {
        switch (param)
        {
        /*  crystal frequency                     */
        case MT2063_SRO_FREQ:
            pInfo->AS_Data.f_ref = nValue;
            pInfo->AS_Data.f_LO1_FracN_Avoid = 0;
            pInfo->AS_Data.f_LO2_FracN_Avoid = nValue / 80 - 1;
            pInfo->AS_Data.f_LO1_Step = nValue / 64;
            pInfo->AS_Data.f_if1_Center = (pInfo->AS_Data.f_ref / 8) * (pInfo->reg[FIFFC] + 640);
            break;

        /*  minimum tuning step size              */
        case MT2063_STEPSIZE:
            pInfo->AS_Data.f_LO2_Step = nValue;
            break;


        /*  LO1 frequency                         */
        case MT2063_LO1_FREQ:
            {
                /* Note: LO1 and LO2 are BOTH written at toggle of LDLOos  */
                /* Capture the Divider and Numerator portions of other LO  */
                U8Data tempLO2CQ[3];
                U8Data tempLO2C[3];
                U8Data tmpOneShot;
                UData_t Div, FracN;
                U8Data restore = 0;

                /* Buffer the queue for restoration later and get actual LO2 values. */
                status |= MT2063_ReadSub (pInfo->hUserData, pInfo->address, LO2CQ_1, &(tempLO2CQ[0]), 3);
                status |= MT2063_ReadSub (pInfo->hUserData, pInfo->address, LO2C_1,  &(tempLO2C[0]),  3);

                /* clear the one-shot bits */
                tempLO2CQ[2] = tempLO2CQ[2] & 0x0F;
                tempLO2C[2]  = tempLO2C[2]  & 0x0F;

                /* only write the queue values if they are different from the actual. */
                if( ( tempLO2CQ[0] != tempLO2C[0] ) ||
                    ( tempLO2CQ[1] != tempLO2C[1] ) ||
                    ( tempLO2CQ[2] != tempLO2C[2] )   )
                {
                    /* put actual LO2 value into queue (with 0 in one-shot bits) */
                    status |= MT2063_WriteSub(pInfo->hUserData, pInfo->address, LO2CQ_1, &(tempLO2C[0]),  3);

                    if( status == MT_OK )
                    {
                        /* cache the bytes just written. */
                        pInfo->reg[LO2CQ_1] = tempLO2C[0];
                        pInfo->reg[LO2CQ_2] = tempLO2C[1];
                        pInfo->reg[LO2CQ_3] = tempLO2C[2];
                    }
                    restore = 1;
                }

                /* Calculate the Divider and Numberator components of LO1 */
                status = CalcLO1Mult(&Div, &FracN, nValue, pInfo->AS_Data.f_ref/64, pInfo->AS_Data.f_ref);
                pInfo->reg[LO1CQ_1] = (U8Data)(Div & 0x00FF);
                pInfo->reg[LO1CQ_2] = (U8Data)(FracN);
                status |= MT2063_WriteSub(pInfo->hUserData, pInfo->address, LO1CQ_1, &pInfo->reg[LO1CQ_1],  2);

                /* set the one-shot bit to load the pair of LO values */
                tmpOneShot = tempLO2CQ[2] | 0xE0;
                status |= MT2063_WriteSub(pInfo->hUserData, pInfo->address, LO2CQ_3, &tmpOneShot,  1);

                /* only restore the queue values if they were different from the actual. */
                if( restore )
                {
                    /* put actual LO2 value into queue (0 in one-shot bits) */
                    status |= MT2063_WriteSub(pInfo->hUserData, pInfo->address, LO2CQ_1, &(tempLO2CQ[0]),  3);

                    /* cache the bytes just written. */
                    pInfo->reg[LO2CQ_1] = tempLO2CQ[0];
                    pInfo->reg[LO2CQ_2] = tempLO2CQ[1];
                    pInfo->reg[LO2CQ_3] = tempLO2CQ[2];
                }

                status |= MT2063_GetParam( pInfo->hUserData,  MT2063_LO1_FREQ, &pInfo->AS_Data.f_LO1 );
            }
            break;

        /*  LO1 minimum step size                 */
        case MT2063_LO1_STEPSIZE:
            pInfo->AS_Data.f_LO1_Step = nValue;
            break;

        /*  LO1 FracN keep-out region             */
        case MT2063_LO1_FRACN_AVOID:
            pInfo->AS_Data.f_LO1_FracN_Avoid = nValue;
            break;

        /*  Requested 1st IF                      */
        case MT2063_IF1_REQUEST:
            pInfo->AS_Data.f_if1_Request = nValue;
            break;

        /*  zero-IF bandwidth                     */
        case MT2063_ZIF_BW:
            pInfo->AS_Data.f_zif_bw = nValue;
            break;

        /*  LO2 frequency                         */
        case MT2063_LO2_FREQ:
            {
                /* Note: LO1 and LO2 are BOTH written at toggle of LDLOos  */
                /* Capture the Divider and Numerator portions of other LO  */
                U8Data  tempLO1CQ[2];
                U8Data  tempLO1C[2];
                UData_t Div2;
                UData_t FracN2;
                U8Data  tmpOneShot;
                U8Data  restore = 0;

                /* Buffer the queue for restoration later and get actual LO2 values. */
                status |= MT2063_ReadSub (pInfo->hUserData, pInfo->address, LO1CQ_1, &(tempLO1CQ[0]), 2);
                status |= MT2063_ReadSub (pInfo->hUserData, pInfo->address, LO1C_1,  &(tempLO1C[0]),  2);

                /* only write the queue values if they are different from the actual. */
                if( (tempLO1CQ[0] != tempLO1C[0]) || (tempLO1CQ[1] != tempLO1C[1])   )
                {
                    /* put actual LO1 value into queue */
                    status |= MT2063_WriteSub(pInfo->hUserData, pInfo->address, LO1CQ_1, &(tempLO1C[0]),  2);

                    /* cache the bytes just written. */
                    pInfo->reg[LO1CQ_1] = tempLO1C[0];
                    pInfo->reg[LO1CQ_2] = tempLO1C[1];
                    restore = 1;
                }

                /* Calculate the Divider and Numberator components of LO2 */
                status |= CalcLO2Mult(&Div2, &FracN2, nValue, pInfo->AS_Data.f_ref/8191, pInfo->AS_Data.f_ref);
                pInfo->reg[LO2CQ_1] = (U8Data)((Div2 << 1) | ((FracN2 >> 12) & 0x01) ) & 0xFF;
                pInfo->reg[LO2CQ_2] = (U8Data)((FracN2 >> 4) & 0xFF);
                pInfo->reg[LO2CQ_3] = (U8Data)((FracN2 & 0x0F) );
                status |= MT2063_WriteSub(pInfo->hUserData, pInfo->address, LO1CQ_1, &pInfo->reg[LO1CQ_1],  3);

                /* set the one-shot bit to load the LO values */
                tmpOneShot = pInfo->reg[LO2CQ_3] | 0xE0;
                status |= MT2063_WriteSub(pInfo->hUserData, pInfo->address, LO2CQ_3, &tmpOneShot,  1);

                /* only restore LO1 queue value if they were different from the actual. */
                if( restore )
                {
                    /* put previous LO1 queue value back into queue */
                    status |= MT2063_WriteSub(pInfo->hUserData, pInfo->address, LO1CQ_1, &(tempLO1CQ[0]),  2);

                    /* cache the bytes just written. */
                    pInfo->reg[LO1CQ_1] = tempLO1CQ[0];
                    pInfo->reg[LO1CQ_2] = tempLO1CQ[1];
                }

                status |= MT2063_GetParam( pInfo->hUserData,  MT2063_LO2_FREQ, &pInfo->AS_Data.f_LO2 );
            }
            break;

        /*  LO2 minimum step size                 */
        case MT2063_LO2_STEPSIZE:
            pInfo->AS_Data.f_LO2_Step = nValue;
            break;

        /*  LO2 FracN keep-out region             */
        case MT2063_LO2_FRACN_AVOID:
            pInfo->AS_Data.f_LO2_FracN_Avoid = nValue;
            break;

        /*  output center frequency               */
        case MT2063_OUTPUT_FREQ:
            pInfo->AS_Data.f_out = nValue;
            break;

        /*  output bandwidth                      */
        case MT2063_OUTPUT_BW:
            pInfo->AS_Data.f_out_bw = nValue + 750000;
            break;

        /*  min inter-tuner LO separation         */
        case MT2063_LO_SEPARATION:
            pInfo->AS_Data.f_min_LO_Separation = nValue;
            break;

        /*  max # of intra-tuner harmonics        */
        case MT2063_MAX_HARM1:
            pInfo->AS_Data.maxH1 = nValue;
            break;

        /*  max # of inter-tuner harmonics        */
        case MT2063_MAX_HARM2:
            pInfo->AS_Data.maxH2 = nValue;
            break;

        case MT2063_RCVR_MODE:
            status |= MT2063_SetReceiverMode(pInfo, (MT2063_RCVR_MODES)nValue);
            break;

        /* Set LNA Rin -- nValue is desired value */
        case MT2063_LNA_RIN:
            val = (U8Data)( ( pInfo->reg[CTRL_2C] & (U8Data)~0x03) | (nValue & 0x03) );
            if( pInfo->reg[CTRL_2C] != val )
            {
                status |= MT2063_SetReg(pInfo, CTRL_2C, val);
            }
            break;

        /* Set target power level at LNA -- nValue is desired value */
        case MT2063_LNA_TGT:
            val = (U8Data)( ( pInfo->reg[LNA_TGT] & (U8Data)~0x3F) | (nValue & 0x3F) );
            if( pInfo->reg[LNA_TGT] != val )
            {
                status |= MT2063_SetReg(pInfo, LNA_TGT, val);
            }
            break;

        /* Set target power level at PD1 -- nValue is desired value */
        case MT2063_PD1_TGT:
            val = (U8Data)( ( pInfo->reg[PD1_TGT] & (U8Data)~0x3F) | (nValue & 0x3F) );
            if( pInfo->reg[PD1_TGT] != val )
            {
                status |= MT2063_SetReg(pInfo, PD1_TGT, val);
            }
            break;

        /* Set target power level at PD2 -- nValue is desired value */
        case MT2063_PD2_TGT:
            val = (U8Data)( ( pInfo->reg[PD2_TGT] & (U8Data)~0x3F) | (nValue & 0x3F) );
            if( pInfo->reg[PD2_TGT] != val )
            {
                status |= MT2063_SetReg(pInfo, PD2_TGT, val);
            }
            break;

        /* Set LNA atten limit -- nValue is desired value */
        case MT2063_ACLNA_MAX:
            val = (U8Data)( ( pInfo->reg[LNA_OV] & (U8Data)~0x1F) | (nValue & 0x1F) );
            if( pInfo->reg[LNA_OV] != val )
            {
                status |= MT2063_SetReg(pInfo, LNA_OV, val);
            }
            break;

        /* Set RF atten limit -- nValue is desired value */
        case MT2063_ACRF_MAX:
            val = (U8Data)( ( pInfo->reg[RF_OV] & (U8Data)~0x1F) | (nValue & 0x1F) );
            if( pInfo->reg[RF_OV] != val )
            {
                status |= MT2063_SetReg(pInfo, RF_OV, val);
            }
            break;

        /* Set FIF atten limit -- nValue is desired value, max. 5 if no B3 */
        case MT2063_ACFIF_MAX:
            if ( (pInfo->tuner_id == MT2063_B0 || pInfo->tuner_id == MT2063_B1)  && nValue > 5)
                nValue = 5;
            val = (U8Data)( ( pInfo->reg[FIF_OV] & (U8Data)~0x1F) | (nValue & 0x1F) );
            if( pInfo->reg[FIF_OV] != val )
            {
                status |= MT2063_SetReg(pInfo, FIF_OV, val);
            }
            break;

        case MT2063_DNC_OUTPUT_ENABLE:
            /* selects, which DNC output is used */
            switch ((MT2063_DNC_Output_Enable)nValue)
            {
               case MT2063_DNC_NONE :
               {
                  val = (pInfo->reg[DNC_GAIN] & 0xFC ) | 0x03; /* Set DNC1GC=3 */
                  if (pInfo->reg[DNC_GAIN] != val)
                      status |= MT2063_SetReg(h, DNC_GAIN, val);

                  val = (pInfo->reg[VGA_GAIN] & 0xFC ) | 0x03; /* Set DNC2GC=3 */
                  if (pInfo->reg[VGA_GAIN] != val)
                      status |= MT2063_SetReg(h, VGA_GAIN, val);

                  val = (pInfo->reg[RSVD_20] & ~0x40);   /* Set PD2MUX=0 */
                  if (pInfo->reg[RSVD_20] != val)
                      status |= MT2063_SetReg(h, RSVD_20, val);

                  break;
               }
               case MT2063_DNC_1 :
               {
                  val = (pInfo->reg[DNC_GAIN] & 0xFC ) | (DNC1GC[pInfo->rcvr_mode] & 0x03); /* Set DNC1GC=x */
                  if (pInfo->reg[DNC_GAIN] != val)
                      status |= MT2063_SetReg(h, DNC_GAIN, val);

                  val = (pInfo->reg[VGA_GAIN] & 0xFC ) | 0x03; /* Set DNC2GC=3 */
                  if (pInfo->reg[VGA_GAIN] != val)
                      status |= MT2063_SetReg(h, VGA_GAIN, val);

                  val = (pInfo->reg[RSVD_20] & ~0x40);   /* Set PD2MUX=0 */
                  if (pInfo->reg[RSVD_20] != val)
                      status |= MT2063_SetReg(h, RSVD_20, val);

                  break;
               }
               case MT2063_DNC_2 :
               {
                  val = (pInfo->reg[DNC_GAIN] & 0xFC ) | 0x03; /* Set DNC1GC=3 */
                  if (pInfo->reg[DNC_GAIN] != val)
                      status |= MT2063_SetReg(h, DNC_GAIN, val);

                  val = (pInfo->reg[VGA_GAIN] & 0xFC ) | (DNC2GC[pInfo->rcvr_mode] & 0x03); /* Set DNC2GC=x */
                  if (pInfo->reg[VGA_GAIN] != val)
                      status |= MT2063_SetReg(h, VGA_GAIN, val);

                  val = (pInfo->reg[RSVD_20] | 0x40);   /* Set PD2MUX=1 */
                  if (pInfo->reg[RSVD_20] != val)
                      status |= MT2063_SetReg(h, RSVD_20, val);

                  break;
               }
               case MT2063_DNC_BOTH :
               {
                  val = (pInfo->reg[DNC_GAIN] & 0xFC ) | (DNC1GC[pInfo->rcvr_mode] & 0x03); /* Set DNC1GC=x */
                  if (pInfo->reg[DNC_GAIN] != val)
                      status |= MT2063_SetReg(h, DNC_GAIN, val);

                  val = (pInfo->reg[VGA_GAIN] & 0xFC ) | (DNC2GC[pInfo->rcvr_mode] & 0x03); /* Set DNC2GC=x */
                  if (pInfo->reg[VGA_GAIN] != val)
                      status |= MT2063_SetReg(h, VGA_GAIN, val);

                  val = (pInfo->reg[RSVD_20] | 0x40);   /* Set PD2MUX=1 */
                  if (pInfo->reg[RSVD_20] != val)
                      status |= MT2063_SetReg(h, RSVD_20, val);

                  break;
               }
               default : break;
            }
            break;

        case MT2063_VGAGC:
            /* Set VGA gain code */
            val = (U8Data)( (pInfo->reg[VGA_GAIN] & (U8Data)~0x0C) | ( (nValue & 0x03) << 2) );
            if( pInfo->reg[VGA_GAIN] != val )
            {
                status |= MT2063_SetReg(pInfo, VGA_GAIN, val);
            }
            break;

        case MT2063_VGAOI:
            /* Set VGA bias current */
            val = (U8Data)( (pInfo->reg[RSVD_31] & (U8Data)~0x07) | (nValue & 0x07) );
            if( pInfo->reg[RSVD_31] != val )
            {
                status |= MT2063_SetReg(pInfo, RSVD_31, val);
            }
            break;

        case MT2063_TAGC:
            /* Set TAGC */
            val = (U8Data)( (pInfo->reg[RSVD_1E] & (U8Data)~0x03) | (nValue & 0x03) );
            if( pInfo->reg[RSVD_1E] != val )
            {
                status |= MT2063_SetReg(pInfo, RSVD_1E, val);
            }
            break;

        case MT2063_AMPGC:
            /* Set Amp gain code */
            val = (U8Data)( (pInfo->reg[TEMP_SEL] & (U8Data)~0x03) | (nValue & 0x03) );
            if( pInfo->reg[TEMP_SEL] != val )
            {
                status |= MT2063_SetReg(pInfo, TEMP_SEL, val);
            }
            break;

        /*  Avoid DECT Frequencies                */
        case MT2063_AVOID_DECT:
            {
                MT_DECT_Avoid_Type newAvoidSetting = (MT_DECT_Avoid_Type) nValue;
                if (newAvoidSetting <= MT_AVOID_BOTH)
                {
                    pInfo->AS_Data.avoidDECT = newAvoidSetting;
                }
            }
            break;

       /*  Cleartune filter selection: 0 - by IC (default), 1 - by software  */
        case MT2063_CTFILT_SW:
            pInfo->ctfilt_sw = (nValue & 0x01);
            break;

        /*  These parameters are read-only  */
        case MT2063_VERSION:
        case MT2063_IC_ADDR:
        case MT2063_IC_REV:
        case MT2063_MAX_OPEN:
        case MT2063_NUM_OPEN:
        case MT2063_INPUT_FREQ:
        case MT2063_IF1_ACTUAL:
        case MT2063_IF1_CENTER:
        case MT2063_IF1_BW:
        case MT2063_AS_ALG:
        case MT2063_EXCL_ZONES:
        case MT2063_SPUR_AVOIDED:
        case MT2063_NUM_SPURS:
        case MT2063_SPUR_PRESENT:
        case MT2063_PD1:
        case MT2063_PD2:
        case MT2063_ACLNA:
        case MT2063_ACRF:
        case MT2063_ACFIF:
        case MT2063_EOP:
        default:
            status |= MT_ARG_RANGE;
        }
    }
    return (UData_t)(status);
}


/****************************************************************************
**
**  Name: MT2063_SetPowerMaskBits
**
**  Description:    Sets the power-down mask bits for various sections of
**                  the MT2063
**
**  Parameters:     h           - Tuner handle (returned by MT2063_Open)
**                  Bits        - Mask bits to be set.
**
**                  See definition of MT2063_Mask_Bits type for description
**                  of each of the power bits.
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_INV_HANDLE    - Invalid tuner handle
**                      MT_COMM_ERR      - Serial bus communications error
**
**  Dependencies:   USERS MUST CALL MT2063_Open() FIRST!
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**
****************************************************************************/
UData_t MT2063_SetPowerMaskBits(Handle_t h, MT2063_Mask_Bits Bits)
{
    UData_t status = MT_OK;                  /* Status to be returned        */
    MT2063_Info_t* pInfo = (MT2063_Info_t*) h;

    /*  Verify that the handle passed points to a valid tuner         */
    if (IsValidHandle(pInfo) == 0)
        status = MT_INV_HANDLE;
    else
    {
        Bits = (MT2063_Mask_Bits)(Bits & MT2063_ALL_SD);  /* Only valid bits for this tuner */
        if ((Bits & 0xFF00) != 0)
        {
            pInfo->reg[PWR_2] |= (U8Data)((Bits & 0xFF00) >> 8);
            status |= MT2063_WriteSub(pInfo->hUserData, pInfo->address, PWR_2, &pInfo->reg[PWR_2], 1);
        }
        if ((Bits & 0xFF) != 0)
        {
            pInfo->reg[PWR_1] |= ((U8Data)Bits & 0xFF);
            status |= MT2063_WriteSub(pInfo->hUserData, pInfo->address, PWR_1, &pInfo->reg[PWR_1], 1);
        }
    }

    return (UData_t)(status);
}


/****************************************************************************
**
**  Name: MT2063_ClearPowerMaskBits
**
**  Description:    Clears the power-down mask bits for various sections of
**                  the MT2063
**
**  Parameters:     h           - Tuner handle (returned by MT2063_Open)
**                  Bits        - Mask bits to be cleared.
**
**                  See definition of MT2063_Mask_Bits type for description
**                  of each of the power bits.
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_INV_HANDLE    - Invalid tuner handle
**                      MT_COMM_ERR      - Serial bus communications error
**
**  Dependencies:   USERS MUST CALL MT2063_Open() FIRST!
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**
****************************************************************************/
UData_t MT2063_ClearPowerMaskBits(Handle_t h, MT2063_Mask_Bits Bits)
{
    UData_t status = MT_OK;                  /* Status to be returned        */
    MT2063_Info_t* pInfo = (MT2063_Info_t*) h;

    /*  Verify that the handle passed points to a valid tuner         */
    if (IsValidHandle(pInfo) == 0)
        status = MT_INV_HANDLE;
    else
    {
        Bits = (MT2063_Mask_Bits)(Bits & MT2063_ALL_SD);  /* Only valid bits for this tuner */
        if ((Bits & 0xFF00) != 0)
        {
            pInfo->reg[PWR_2] &= ~(U8Data)(Bits >> 8);
            status |= MT2063_WriteSub(pInfo->hUserData, pInfo->address, PWR_2, &pInfo->reg[PWR_2], 1);
        }
        if ((Bits & 0xFF) != 0)
        {
            pInfo->reg[PWR_1] &= ~(U8Data)(Bits & 0xFF);
            status |= MT2063_WriteSub(pInfo->hUserData, pInfo->address, PWR_1, &pInfo->reg[PWR_1], 1);
        }
    }

    return (UData_t)(status);
}


/****************************************************************************
**
**  Name: MT2063_GetPowerMaskBits
**
**  Description:    Returns a mask of the enabled power shutdown bits
**
**  Parameters:     h           - Tuner handle (returned by MT2063_Open)
**                  Bits        - Mask bits to currently set.
**
**                  See definition of MT2063_Mask_Bits type for description
**                  of each of the power bits.
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_INV_HANDLE    - Invalid tuner handle
**                      MT_ARG_NULL      - Output argument is NULL
**                      MT_COMM_ERR      - Serial bus communications error
**
**  Dependencies:   USERS MUST CALL MT2063_Open() FIRST!
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**
****************************************************************************/
UData_t MT2063_GetPowerMaskBits(Handle_t h, MT2063_Mask_Bits *Bits)
{
    UData_t status = MT_OK;                  /* Status to be returned        */
    MT2063_Info_t* pInfo = (MT2063_Info_t*) h;

    /*  Verify that the handle passed points to a valid tuner         */
    if (IsValidHandle(pInfo) == 0)
        status = MT_INV_HANDLE;
    else
    {
        if (Bits == MT_NULL)
            status |= MT_ARG_NULL;

        if (MT_NO_ERROR(status))
            status |= MT2063_ReadSub(pInfo->hUserData, pInfo->address, PWR_1, &pInfo->reg[PWR_1], 2);

        if (MT_NO_ERROR(status))
        {
            *Bits = (MT2063_Mask_Bits)(((SData_t)pInfo->reg[PWR_2] << 8) + pInfo->reg[PWR_1]);
            *Bits = (MT2063_Mask_Bits)(*Bits & MT2063_ALL_SD);  /* Only valid bits for this tuner */
        }
    }

    return (UData_t)(status);
}


/****************************************************************************
**
**  Name: MT2063_EnableExternalShutdown
**
**  Description:    Enables or disables the operation of the external
**                  shutdown pin
**
**  Parameters:     h           - Tuner handle (returned by MT2063_Open)
**                  Enabled     - 0 = disable the pin, otherwise enable it
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_INV_HANDLE    - Invalid tuner handle
**                      MT_COMM_ERR      - Serial bus communications error
**
**  Dependencies:   USERS MUST CALL MT2063_Open() FIRST!
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**
****************************************************************************/
UData_t MT2063_EnableExternalShutdown(Handle_t h, U8Data Enabled)
{
    UData_t status = MT_OK;                  /* Status to be returned        */
    MT2063_Info_t* pInfo = (MT2063_Info_t*) h;

    /*  Verify that the handle passed points to a valid tuner         */
    if (IsValidHandle(pInfo) == 0)
        status = MT_INV_HANDLE;
    else
    {
        if (Enabled == 0)
            pInfo->reg[PWR_1] &= ~0x08;  /* Turn off the bit */
        else
            pInfo->reg[PWR_1] |= 0x08;   /* Turn the bit on */

        status |= MT2063_WriteSub(pInfo->hUserData, pInfo->address, PWR_1, &pInfo->reg[PWR_1], 1);
    }

    return (UData_t)(status);
}

/****************************************************************************
**
**  Name: MT2063_SoftwareShutdown
**
**  Description:    Enables or disables software shutdown function.  When
**                  Shutdown==1, any section whose power mask is set will be
**                  shutdown.
**
**  Parameters:     h           - Tuner handle (returned by MT2063_Open)
**                  Shutdown    - 1 = shutdown the masked sections, otherwise
**                                power all sections on
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_INV_HANDLE    - Invalid tuner handle
**                      MT_COMM_ERR      - Serial bus communications error
**
**  Dependencies:   USERS MUST CALL MT2063_Open() FIRST!
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**         01-03-2008    PINZ   Ver 1.xx: Added a trigger of BYPATNUP for
**                              correct wakeup of the LNA
**
****************************************************************************/
UData_t MT2063_SoftwareShutdown(Handle_t h, U8Data Shutdown)
{
    UData_t status = MT_OK;                  /* Status to be returned        */
    MT2063_Info_t* pInfo = (MT2063_Info_t*) h;

    /*  Verify that the handle passed points to a valid tuner         */
    if (IsValidHandle(pInfo) == 0)
        status = MT_INV_HANDLE;
    else
    {
        if (Shutdown == 1)
            pInfo->reg[PWR_1] |= 0x04;   /* Turn the bit on */
        else
            pInfo->reg[PWR_1] &= ~0x04;  /* Turn off the bit */

        status |= MT2063_WriteSub(pInfo->hUserData, pInfo->address, PWR_1, &pInfo->reg[PWR_1], 1);

        if (Shutdown != 1)
        {
            pInfo->reg[BYP_CTRL] = (pInfo->reg[BYP_CTRL] & 0x9F) | 0x40;
            status |= MT2063_WriteSub(pInfo->hUserData, pInfo->address, BYP_CTRL, &pInfo->reg[BYP_CTRL], 1);
            pInfo->reg[BYP_CTRL] = (pInfo->reg[BYP_CTRL] & 0x9F);
            status |= MT2063_WriteSub(pInfo->hUserData, pInfo->address, BYP_CTRL, &pInfo->reg[BYP_CTRL], 1);
        }
    }

    return (UData_t)(status);
}


/****************************************************************************
**
**  Name: MT2063_SetExtSRO
**
**  Description:    Sets the external SRO driver.
**
**  Parameters:     h           - Tuner handle (returned by MT2063_Open)
**                  Ext_SRO_Setting - external SRO drive setting
**
**       (default)    MT2063_EXT_SRO_OFF  - ext driver off
**                    MT2063_EXT_SRO_BY_1 - ext driver = SRO frequency
**                    MT2063_EXT_SRO_BY_2 - ext driver = SRO/2 frequency
**                    MT2063_EXT_SRO_BY_4 - ext driver = SRO/4 frequency
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_COMM_ERR      - Serial bus communications error
**                      MT_INV_HANDLE    - Invalid tuner handle
**
**  Dependencies:   USERS MUST CALL MT2063_Open() FIRST!
**
**                  The Ext_SRO_Setting settings default to OFF
**                  Use this function if you need to override the default
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**   189 S 05-13-2008    RSK    Ver 1.16: Correct location for ExtSRO control.
**
****************************************************************************/
UData_t MT2063_SetExtSRO(Handle_t h,
                         MT2063_Ext_SRO Ext_SRO_Setting)
{
    UData_t status = MT_OK;                  /* Status to be returned        */
    MT2063_Info_t* pInfo = (MT2063_Info_t*) h;

    /*  Verify that the handle passed points to a valid tuner         */
    if (IsValidHandle(pInfo) == 0)
        status = MT_INV_HANDLE;
    else
    {
        pInfo->reg[CTRL_2C] = (pInfo->reg[CTRL_2C] & 0x3F) | ((U8Data)Ext_SRO_Setting << 6);
        status = MT2063_WriteSub(pInfo->hUserData, pInfo->address, CTRL_2C, &pInfo->reg[CTRL_2C], 1);
    }

    return (UData_t)(status);
}


/****************************************************************************
**
**  Name: MT2063_SetReg
**
**  Description:    Sets an MT2063 register.
**
**  Parameters:     h           - Tuner handle (returned by MT2063_Open)
**                  reg         - MT2063 register/subaddress location
**                  val         - MT2063 register/subaddress value
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_COMM_ERR      - Serial bus communications error
**                      MT_INV_HANDLE    - Invalid tuner handle
**                      MT_ARG_RANGE     - Argument out of range
**
**  Dependencies:   USERS MUST CALL MT2063_Open() FIRST!
**
**                  Use this function if you need to override a default
**                  register value
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**
****************************************************************************/
UData_t MT2063_SetReg(Handle_t h,
                      U8Data   reg,
                      U8Data   val)
{
    UData_t status = MT_OK;                  /* Status to be returned        */
    MT2063_Info_t* pInfo = (MT2063_Info_t*) h;

    /*  Verify that the handle passed points to a valid tuner         */
    if (IsValidHandle(pInfo) == 0)
        status |= MT_INV_HANDLE;

    if (reg >= END_REGS)
        status |= MT_ARG_RANGE;

    if (MT_NO_ERROR(status))
    {
        status |= MT2063_WriteSub(pInfo->hUserData, pInfo->address, reg, &val, 1);
        if (MT_NO_ERROR(status))
            pInfo->reg[reg] = val;
    }

    return (UData_t)(status);
}


static UData_t Round_fLO(UData_t f_LO, UData_t f_LO_Step, UData_t f_ref)
{
    return (UData_t)(  f_ref * (f_LO / f_ref)
        + f_LO_Step * (((f_LO % f_ref) + (f_LO_Step / 2)) / f_LO_Step)  );
}


/****************************************************************************
**
**  Name: fLO_FractionalTerm
**
**  Description:    Calculates the portion contributed by FracN / denom.
**
**                  This function preserves maximum precision without
**                  risk of overflow.  It accurately calculates
**                  f_ref * num / denom to within 1 HZ with fixed math.
**
**  Parameters:     num       - Fractional portion of the multiplier
**                  denom     - denominator portion of the ratio
**                              This routine successfully handles denom values
**                              up to and including 2^18.
**                  f_Ref     - SRO frequency.  This calculation handles
**                              f_ref as two separate 14-bit fields.
**                              Therefore, a maximum value of 2^28-1
**                              may safely be used for f_ref.  This is
**                              the genesis of the magic number "14" and the
**                              magic mask value of 0x03FFF.
**
**  Returns:        f_ref * num / denom
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**
****************************************************************************/
static UData_t fLO_FractionalTerm( UData_t f_ref,
                                   UData_t num,
                                   UData_t denom )
{
    UData_t t1     = (f_ref >> 14) * num;
    UData_t term1  = t1 / denom;
    UData_t loss   = t1 % denom;
    UData_t term2  = ( ((f_ref & 0x00003FFF) * num + (loss<<14)) + (denom/2) )  / denom;
    return (UData_t)(  ((term1 << 14) + term2)  );
}


/****************************************************************************
**
**  Name: CalcLO1Mult
**
**  Description:    Calculates Integer divider value and the numerator
**                  value for a FracN PLL.
**
**                  This function assumes that the f_LO and f_Ref are
**                  evenly divisible by f_LO_Step.
**
**  Parameters:     Div       - OUTPUT: Whole number portion of the multiplier
**                  FracN     - OUTPUT: Fractional portion of the multiplier
**                  f_LO      - desired LO frequency.
**                  f_LO_Step - Minimum step size for the LO (in Hz).
**                  f_Ref     - SRO frequency.
**                  f_Avoid   - Range of PLL frequencies to avoid near
**                              integer multiples of f_Ref (in Hz).
**
**  Returns:        Recalculated LO frequency.
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**
****************************************************************************/
static UData_t CalcLO1Mult(UData_t *Div,
                           UData_t *FracN,
                           UData_t  f_LO,
                           UData_t  f_LO_Step,
                           UData_t  f_Ref)
{
    /*  Calculate the whole number portion of the divider */
    *Div = f_LO / f_Ref;

    /*  Calculate the numerator value (round to nearest f_LO_Step) */
    *FracN = (64 * (((f_LO % f_Ref) + (f_LO_Step / 2)) / f_LO_Step) + (f_Ref / f_LO_Step / 2)) / (f_Ref / f_LO_Step);

    return (UData_t)(  (f_Ref * (*Div)) + fLO_FractionalTerm( f_Ref, *FracN, 64 )  );
}


/****************************************************************************
**
**  Name: CalcLO2Mult
**
**  Description:    Calculates Integer divider value and the numerator
**                  value for a FracN PLL.
**
**                  This function assumes that the f_LO and f_Ref are
**                  evenly divisible by f_LO_Step.
**
**  Parameters:     Div       - OUTPUT: Whole number portion of the multiplier
**                  FracN     - OUTPUT: Fractional portion of the multiplier
**                  f_LO      - desired LO frequency.
**                  f_LO_Step - Minimum step size for the LO (in Hz).
**                  f_Ref     - SRO frequency.
**                  f_Avoid   - Range of PLL frequencies to avoid near
**                              integer multiples of f_Ref (in Hz).
**
**  Returns:        Recalculated LO frequency.
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**   205 M 10-01-2008    JWS    Ver 1.22: Use DMAX when LO2 FracN is near 4096
**
****************************************************************************/
static UData_t CalcLO2Mult(UData_t *Div,
                           UData_t *FracN,
                           UData_t  f_LO,
                           UData_t  f_LO_Step,
                           UData_t  f_Ref)
{
    UData_t denom = 8191;

    /*  Calculate the whole number portion of the divider */
    *Div = f_LO / f_Ref;

    /*  Calculate the numerator value (round to nearest f_LO_Step) */
    *FracN = (8191 * (((f_LO % f_Ref) + (f_LO_Step / 2)) / f_LO_Step) + (f_Ref / f_LO_Step / 2)) / (f_Ref / f_LO_Step);

    /*
    ** FracN values close to 1/2 (4096) will be forced to 4096.  The tuning code must
    ** then set the DMAX bit for the affected LO(s).
    */
    if ((*FracN >= 4083) && (*FracN <= 4107))
    {
        *FracN = 4096;
        denom = 8192;
    }

    return (UData_t)(  (f_Ref * (*Div)) + fLO_FractionalTerm( f_Ref, *FracN, denom )  );
}

/****************************************************************************
**
**  Name: FindClearTuneFilter
**
**  Description:    Calculate the corrrect ClearTune filter to be used for
**                  a given input frequency.
**
**  Parameters:     pInfo       - ptr to tuner data structure
**                  f_in        - RF input center frequency (in Hz).
**
**  Returns:        ClearTune filter number (0-31)
**
**  Dependencies:   MUST CALL MT2064_Open BEFORE FindClearTuneFilter!
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**         04-10-2008   PINZ    Ver 1.14: Use software-controlled ClearTune
**                                        cross-over frequency values.
**
****************************************************************************/
static UData_t FindClearTuneFilter(MT2063_Info_t* pInfo, UData_t f_in)
{
    UData_t RFBand;
    UData_t idx;                  /*  index loop                      */

    /*
    **  Find RF Band setting
    */
    RFBand = 31;                        /*  def when f_in > all    */
    for (idx=0; idx<31; ++idx)
    {
        if (pInfo->CTFiltMax[idx] >= f_in)
        {
            RFBand = idx;
            break;
        }
    }
    return (UData_t)(RFBand);
}



/****************************************************************************
**
**  Name: MT2063_Tune
**
**  Description:    Change the tuner's tuned frequency to RFin.
**
**  Parameters:     h           - Open handle to the tuner (from MT2063_Open).
**                  f_in        - RF input center frequency (in Hz).
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_INV_HANDLE    - Invalid tuner handle
**                      MT_UPC_UNLOCK    - Upconverter PLL unlocked
**                      MT_DNC_UNLOCK    - Downconverter PLL unlocked
**                      MT_COMM_ERR      - Serial bus communications error
**                      MT_SPUR_CNT_MASK - Count of avoided LO spurs
**                      MT_SPUR_PRESENT  - LO spur possible in output
**                      MT_FIN_RANGE     - Input freq out of range
**                      MT_FOUT_RANGE    - Output freq out of range
**                      MT_UPC_RANGE     - Upconverter freq out of range
**                      MT_DNC_RANGE     - Downconverter freq out of range
**
**  Dependencies:   MUST CALL MT2063_Open BEFORE MT2063_Tune!
**
**                  MT_ReadSub       - Read data from the two-wire serial bus
**                  MT_WriteSub      - Write data to the two-wire serial bus
**                  MT_Sleep         - Delay execution for x milliseconds
**                  MT2063_GetLocked - Checks to see if LO1 and LO2 are locked
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   138   06-19-2007    DAD    Ver 1.00: Initial, derived from mt2067_b.
**         04-10-2008   PINZ    Ver 1.05: Use software-controlled ClearTune
**                                        cross-over frequency values.
**   175 I 16-06-2008   PINZ    Ver 1.16: Add control to avoid US DECT freqs.
**   175 I 06-19-2008    RSK    Ver 1.17: Refactor DECT control to SpurAvoid.
**         06-24-2008    PINZ   Ver 1.18: Add Get/SetParam CTFILT_SW
**   205 M 10-01-2008    JWS    Ver 1.22: Use DMAX when LO2 FracN is near 4096
**       M 10-24-2008    JWS    Ver 1.25: Use DMAX when LO1 FracN is 32
**
****************************************************************************/
UData_t MT2063_Tune(Handle_t h,
                    UData_t f_in)     /* RF input center frequency   */
{
    MT2063_Info_t* pInfo = (MT2063_Info_t*) h;

    UData_t status = MT_OK;       /*  status of operation             */
    UData_t LO1;                  /*  1st LO register value           */
    UData_t Num1;                 /*  Numerator for LO1 reg. value    */
    UData_t f_IF1;                /*  1st IF requested                */
    UData_t LO2;                  /*  2nd LO register value           */
    UData_t Num2;                 /*  Numerator for LO2 reg. value    */
    UData_t ofLO1, ofLO2;         /*  last time's LO frequencies      */
    U8Data  fiffc = 0x80;         /*  FIFF center freq from tuner     */
    UData_t fiffof;               /*  Offset from FIFF center freq    */
    const U8Data LO1LK = 0x80;    /*  Mask for LO1 Lock bit           */
    U8Data LO2LK = 0x08;          /*  Mask for LO2 Lock bit           */
    U8Data val;
    UData_t RFBand;
    U8Data oFN;

    /*  Verify that the handle passed points to a valid tuner         */
    if (IsValidHandle(pInfo) == 0)
        return (UData_t)MT_INV_HANDLE;

    /*  Check the input and output frequency ranges                   */
    if ((f_in < MIN_FIN_FREQ) || (f_in > MAX_FIN_FREQ))
        status |= MT_FIN_RANGE;

    if ((pInfo->AS_Data.f_out < MIN_FOUT_FREQ) || (pInfo->AS_Data.f_out > MAX_FOUT_FREQ))
        status |= MT_FOUT_RANGE;

    /*
    **  Save original LO1 and LO2 register values
    */
    ofLO1 = pInfo->AS_Data.f_LO1;
    ofLO2 = pInfo->AS_Data.f_LO2;

    /*
    **  Find and set RF Band setting
    */
    if (pInfo->ctfilt_sw == 1)
    {
        val = ( pInfo->reg[CTUNE_CTRL] | 0x08 );
        if( pInfo->reg[CTUNE_CTRL] != val )
        {
            status |= MT2063_SetReg(pInfo, CTUNE_CTRL, val);
        }

        RFBand = FindClearTuneFilter(pInfo, f_in);
        val  = (U8Data)( (pInfo->reg[CTUNE_OV] & ~0x1F) | RFBand );
        if (pInfo->reg[CTUNE_OV] != val)
        {
            status |= MT2063_SetReg(pInfo, CTUNE_OV, val);
        }
    }

    /*
    **  Read the FIFF Center Frequency from the tuner
    */
    if (MT_NO_ERROR(status))
    {
        status |= MT2063_ReadSub(pInfo->hUserData, pInfo->address, FIFFC, &pInfo->reg[FIFFC], 1);
        fiffc = pInfo->reg[FIFFC];
    }

    /*
    **  Assign in the requested values
    */
    pInfo->AS_Data.f_in = f_in;
    /*  Request a 1st IF such that LO1 is on a step size */
    pInfo->AS_Data.f_if1_Request = Round_fLO(pInfo->AS_Data.f_if1_Request + f_in, pInfo->AS_Data.f_LO1_Step, pInfo->AS_Data.f_ref) - f_in;

    /*
    **  Calculate frequency settings.  f_IF1_FREQ + f_in is the
    **  desired LO1 frequency
    */
    MT2063_ResetExclZones(&pInfo->AS_Data);

    f_IF1 = MT2063_ChooseFirstIF(&pInfo->AS_Data);
    pInfo->AS_Data.f_LO1 = Round_fLO(f_IF1 + f_in, pInfo->AS_Data.f_LO1_Step, pInfo->AS_Data.f_ref);
    pInfo->AS_Data.f_LO2 = Round_fLO(pInfo->AS_Data.f_LO1 - pInfo->AS_Data.f_out - f_in, pInfo->AS_Data.f_LO2_Step, pInfo->AS_Data.f_ref);

    /*
    ** Check for any LO spurs in the output bandwidth and adjust
    ** the LO settings to avoid them if needed
    */
    status |= MT2063_AvoidSpurs(h, &pInfo->AS_Data);

    /*
    ** MT_AvoidSpurs spurs may have changed the LO1 & LO2 values.
    ** Recalculate the LO frequencies and the values to be placed
    ** in the tuning registers.
    */
    pInfo->AS_Data.f_LO1 = CalcLO1Mult(&LO1, &Num1, pInfo->AS_Data.f_LO1, pInfo->AS_Data.f_LO1_Step, pInfo->AS_Data.f_ref);
    pInfo->AS_Data.f_LO2 = Round_fLO(pInfo->AS_Data.f_LO1 - pInfo->AS_Data.f_out - f_in, pInfo->AS_Data.f_LO2_Step, pInfo->AS_Data.f_ref);
    pInfo->AS_Data.f_LO2 = CalcLO2Mult(&LO2, &Num2, pInfo->AS_Data.f_LO2, pInfo->AS_Data.f_LO2_Step, pInfo->AS_Data.f_ref);

    /*
    **  Check the upconverter and downconverter frequency ranges
    */
    if ((pInfo->AS_Data.f_LO1 < MIN_UPC_FREQ) || (pInfo->AS_Data.f_LO1 > MAX_UPC_FREQ))
        status |= MT_UPC_RANGE;

    if ((pInfo->AS_Data.f_LO2 < MIN_DNC_FREQ) || (pInfo->AS_Data.f_LO2 > MAX_DNC_FREQ))
        status |= MT_DNC_RANGE;

    /*  LO2 Lock bit was in a different place for B0 version  */
    if (pInfo->tuner_id == MT2063_B0)
        LO2LK = 0x40;

    /*
    **  If we have the same LO frequencies and we're already locked,
    **  then skip re-programming the LO registers.
    */
    if ((ofLO1 != pInfo->AS_Data.f_LO1)
        || (ofLO2 != pInfo->AS_Data.f_LO2)
        || ((pInfo->reg[LO_STATUS] & (LO1LK | LO2LK)) != (LO1LK | LO2LK)))
    {
        /*
        **  Calculate the FIFFOF register value
        **
        **            IF1_Actual
        **  FIFFOF = ------------ - 8 * FIFFC - 4992
        **             f_ref/64
        */
        fiffof = (pInfo->AS_Data.f_LO1 - f_in) / (pInfo->AS_Data.f_ref / 64) - 8 * (UData_t)fiffc - 4992;
        if (fiffof > 0xFF)
            fiffof = 0xFF;

        /*
        **  Place all of the calculated values into the local tuner
        **  register fields.
        */
        if (MT_NO_ERROR(status))
        {
            oFN = pInfo->reg[FN_CTRL];  /* save current FN_CTRL settings */

            /*
            ** If either FracN is 4096 (or was rounded to there) then set its
            ** DMAX bit, otherwise clear it.
            */
            if (Num1 == 32)
                pInfo->reg[FN_CTRL] |= 0x20;
            else
                pInfo->reg[FN_CTRL] &= ~0x20;
            if (Num2 == 4096)
                pInfo->reg[FN_CTRL] |= 0x02;
            else
                pInfo->reg[FN_CTRL] &= ~0x02;

            pInfo->reg[LO1CQ_1] = (U8Data)(LO1 & 0xFF);             /* DIV1q */
            pInfo->reg[LO1CQ_2] = (U8Data)(Num1 & 0x3F);            /* NUM1q */
            pInfo->reg[LO2CQ_1] = (U8Data)(((LO2 & 0x7F) << 1)      /* DIV2q */
                                       | (Num2 >> 12));          /* NUM2q (hi) */
            pInfo->reg[LO2CQ_2] = (U8Data)((Num2 & 0x0FF0) >> 4);   /* NUM2q (mid) */
            pInfo->reg[LO2CQ_3] = (U8Data)(0xE0 | (Num2 & 0x000F)); /* NUM2q (lo) */

            /*
            ** Now write out the computed register values
            ** IMPORTANT: There is a required order for writing
            **            (0x05 must follow all the others).
            */
            status |= MT2063_WriteSub(pInfo->hUserData, pInfo->address, LO1CQ_1, &pInfo->reg[LO1CQ_1], 5);   /* 0x01 - 0x05 */

            /* The FN_CTRL register is only needed if it changes */
            if (oFN != pInfo->reg[FN_CTRL])
                status |= MT2063_WriteSub(pInfo->hUserData, pInfo->address, FN_CTRL, &pInfo->reg[FN_CTRL], 1);   /* 0x34 */

            if (pInfo->tuner_id == MT2063_B0)
            {
                /* Re-write the one-shot bits to trigger the tune operation */
                status |= MT2063_WriteSub(pInfo->hUserData, pInfo->address, LO2CQ_3, &pInfo->reg[LO2CQ_3], 1);   /* 0x05 */
            }

            /* Write out the FIFF offset only if it's changing */
            if (pInfo->reg[FIFF_OFFSET] != (U8Data)fiffof)
            {
                pInfo->reg[FIFF_OFFSET] = (U8Data)fiffof;
                status |= MT2063_WriteSub(pInfo->hUserData, pInfo->address, FIFF_OFFSET, &pInfo->reg[FIFF_OFFSET], 1);
            }
        }

        /*
        **  Check for LO's locking
        */
        if (MT_NO_ERROR(status))
        {
            status |= MT2063_GetLocked(h);
        }

        /*
        **  If we locked OK, assign calculated data to MT2063_Info_t structure
        */
        if (MT_NO_ERROR(status))
        {
            pInfo->f_IF1_actual = pInfo->AS_Data.f_LO1 - f_in;
        }
    }

    return (UData_t)(status);
}























// Microtune source code - mt_spuravoid.c


/*****************************************************************************
**
**  Name: mt_spuravoid.c
**
**  Copyright 2006-2008 Microtune, Inc. All Rights Reserved
**
**  This source code file contains confidential information and/or trade
**  secrets of Microtune, Inc. or its affiliates and is subject to the
**  terms of your confidentiality agreement with Microtune, Inc. or one of
**  its affiliates, as applicable.
**
*****************************************************************************/

/*****************************************************************************
**
**  Name: mt_spuravoid.c
**
**  Description:    Microtune spur avoidance software module.
**                  Supports Microtune tuner drivers.
**
**  CVS ID:         $Id: mt_spuravoid.c,v 1.4 2008/11/05 13:46:19 software Exp $
**  CVS Source:     $Source: /export/vol0/cvsroot/software/tuners/MT2063/mt_spuravoid.c,v $
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   082   03-25-2005    JWS    Original multi-tuner support - requires
**                              MTxxxx_CNT declarations
**   096   04-06-2005    DAD    Ver 1.11: Fix divide by 0 error if maxH==0.
**   094   04-06-2005    JWS    Ver 1.11 Added uceil and ufloor to get rid
**                              of compiler warnings
**   N/A   04-07-2005    DAD    Ver 1.13: Merged single- and multi-tuner spur
**                              avoidance into a single module.
**   103   01-31-2005    DAD    Ver 1.14: In MT_AddExclZone(), if the range
**                              (f_min, f_max) < 0, ignore the entry.
**   115   03-23-2007    DAD    Fix declaration of spur due to truncation
**                              errors.
**   117   03-29-2007    RSK    Ver 1.15: Re-wrote to match search order from
**                              tuner DLL.
**   137   06-18-2007    DAD    Ver 1.16: Fix possible divide-by-0 error for
**                              multi-tuners that have
**                              (delta IF1) > (f_out-f_outbw/2).
**   147   07-27-2007    RSK    Ver 1.17: Corrected calculation (-) to (+)
**                              Added logic to force f_Center within 1/2 f_Step.
**   177 S 02-26-2008    RSK    Ver 1.18: Corrected calculation using LO1 > MAX/2
**                              Type casts added to preserve correct sign.
**   195 I 06-17-2008    RSK    Ver 1.19: Refactoring avoidance of DECT
**                              frequencies into MT_ResetExclZones().
**   N/A I 06-20-2008    RSK    Ver 1.21: New VERSION number for ver checking.
**   199   08-06-2008    JWS    Ver 1.22: Added 0x1x1 spur frequency calculations
**                              and indicate success of AddExclZone operation.
**   200   08-07-2008    JWS    Ver 1.23: Moved definition of DECT avoid constants
**                              so users could access them more easily.
**
*****************************************************************************/
//#include "mt_spuravoid.h"
//#include <stdlib.h>
//#include <assert.h>  /*  debug purposes  */

#if !defined(MT_TUNER_CNT)
#error MT_TUNER_CNT is not defined (see mt_userdef.h)
#endif

#if MT_TUNER_CNT == 0
#error MT_TUNER_CNT must be updated in mt_userdef.h
#endif

/*  Version of this module                         */
#define MT_SPURAVOID_VERSION 10203            /*  Version 01.23 */


/*  Implement ceiling, floor functions.  */
#define ceil(n, d) (((n) < 0) ? (-((-(n))/(d))) : (n)/(d) + ((n)%(d) != 0))
#define uceil(n, d) ((n)/(d) + ((n)%(d) != 0))
#define floor(n, d) (((n) < 0) ? (-((-(n))/(d))) - ((n)%(d) != 0) : (n)/(d))
#define ufloor(n, d) ((n)/(d))

/*  Macros to check for DECT exclusion options */
#define MT_EXCLUDE_US_DECT_FREQUENCIES(s) (((s) & MT_AVOID_US_DECT) != 0)
#define MT_EXCLUDE_EURO_DECT_FREQUENCIES(s) (((s) & MT_AVOID_EURO_DECT) != 0)


struct MT_FIFZone_t
{
    SData_t         min_;
    SData_t         max_;
};

#if MT_TUNER_CNT > 1
static MT2063_AvoidSpursData_t* TunerList[MT_TUNER_CNT];
static UData_t              TunerCount = 0;
#endif

UData_t MT2063_RegisterTuner(MT2063_AvoidSpursData_t* pAS_Info)
{
#if MT_TUNER_CNT == 1
    pAS_Info->nAS_Algorithm = 1;
    return MT_OK;
#else
    UData_t index;

    pAS_Info->nAS_Algorithm = 2;

    /*
    **  Check to see if tuner is already registered
    */
    for (index = 0; index < TunerCount; index++)
    {
        if (TunerList[index] == pAS_Info)
        {
            return MT_OK;  /* Already here - no problem  */
        }
    }

    /*
    ** Add tuner to list - if there is room.
    */
    if (TunerCount < MT_TUNER_CNT)
    {
        TunerList[TunerCount] = pAS_Info;
        TunerCount++;
        return MT_OK;
    }
    else
        return MT_TUNER_CNT_ERR;
#endif
}


void MT2063_UnRegisterTuner(MT2063_AvoidSpursData_t* pAS_Info)
{
#if MT_TUNER_CNT == 1
  //  pAS_Info;
   // pAS_Info->nAS_Algorithm = 1;
   // return MT_OK;
#else

    UData_t index;

    for (index = 0; index < TunerCount; index++)
    {
        if (TunerList[index] == pAS_Info)
        {
            TunerList[index] = TunerList[--TunerCount];
        }
    }
#endif
}


/*
**  Reset all exclusion zones.
**  Add zones to protect the PLL FracN regions near zero
**
**   195 I 06-17-2008    RSK    Ver 1.19: Refactoring avoidance of DECT
**                              frequencies into MT_ResetExclZones().
*/
void MT2063_ResetExclZones(MT2063_AvoidSpursData_t* pAS_Info)
{
    UData_t center;
#if MT_TUNER_CNT > 1
    UData_t index;
    MT2063_AvoidSpursData_t* adj;
#endif

    pAS_Info->nZones = 0;                           /*  this clears the used list  */
    pAS_Info->usedZones = MT_NULL;                     /*  reset ptr                  */
    pAS_Info->freeZones = MT_NULL;                     /*  reset ptr                  */

    center = pAS_Info->f_ref * ((pAS_Info->f_if1_Center - pAS_Info->f_if1_bw/2 + pAS_Info->f_in) / pAS_Info->f_ref) - pAS_Info->f_in;
    while (center < pAS_Info->f_if1_Center + pAS_Info->f_if1_bw/2 + pAS_Info->f_LO1_FracN_Avoid)
    {
        /*  Exclude LO1 FracN  */
        MT2063_AddExclZone(pAS_Info, center-pAS_Info->f_LO1_FracN_Avoid, center-1);
        MT2063_AddExclZone(pAS_Info, center+1, center+pAS_Info->f_LO1_FracN_Avoid);
        center += pAS_Info->f_ref;
    }

    center = pAS_Info->f_ref * ((pAS_Info->f_if1_Center - pAS_Info->f_if1_bw/2 - pAS_Info->f_out) / pAS_Info->f_ref) + pAS_Info->f_out;
    while (center < pAS_Info->f_if1_Center + pAS_Info->f_if1_bw/2 + pAS_Info->f_LO2_FracN_Avoid)
    {
        /*  Exclude LO2 FracN  */
        MT2063_AddExclZone(pAS_Info, center-pAS_Info->f_LO2_FracN_Avoid, center-1);
        MT2063_AddExclZone(pAS_Info, center+1, center+pAS_Info->f_LO2_FracN_Avoid);
        center += pAS_Info->f_ref;
    }

    if( MT_EXCLUDE_US_DECT_FREQUENCIES(pAS_Info->avoidDECT) )
    {
        /*  Exclude LO1 values that conflict with DECT channels */
        MT2063_AddExclZone(pAS_Info, 1920836000 - pAS_Info->f_in, 1922236000 - pAS_Info->f_in);  /* Ctr = 1921.536 */
        MT2063_AddExclZone(pAS_Info, 1922564000 - pAS_Info->f_in, 1923964000 - pAS_Info->f_in);  /* Ctr = 1923.264 */
        MT2063_AddExclZone(pAS_Info, 1924292000 - pAS_Info->f_in, 1925692000 - pAS_Info->f_in);  /* Ctr = 1924.992 */
        MT2063_AddExclZone(pAS_Info, 1926020000 - pAS_Info->f_in, 1927420000 - pAS_Info->f_in);  /* Ctr = 1926.720 */
        MT2063_AddExclZone(pAS_Info, 1927748000 - pAS_Info->f_in, 1929148000 - pAS_Info->f_in);  /* Ctr = 1928.448 */
    }

    if( MT_EXCLUDE_EURO_DECT_FREQUENCIES(pAS_Info->avoidDECT) )
    {
        MT2063_AddExclZone(pAS_Info, 1896644000 - pAS_Info->f_in, 1898044000 - pAS_Info->f_in);  /* Ctr = 1897.344 */
        MT2063_AddExclZone(pAS_Info, 1894916000 - pAS_Info->f_in, 1896316000 - pAS_Info->f_in);  /* Ctr = 1895.616 */
        MT2063_AddExclZone(pAS_Info, 1893188000 - pAS_Info->f_in, 1894588000 - pAS_Info->f_in);  /* Ctr = 1893.888 */
        MT2063_AddExclZone(pAS_Info, 1891460000 - pAS_Info->f_in, 1892860000 - pAS_Info->f_in);  /* Ctr = 1892.16  */
        MT2063_AddExclZone(pAS_Info, 1889732000 - pAS_Info->f_in, 1891132000 - pAS_Info->f_in);  /* Ctr = 1890.432 */
        MT2063_AddExclZone(pAS_Info, 1888004000 - pAS_Info->f_in, 1889404000 - pAS_Info->f_in);  /* Ctr = 1888.704 */
        MT2063_AddExclZone(pAS_Info, 1886276000 - pAS_Info->f_in, 1887676000 - pAS_Info->f_in);  /* Ctr = 1886.976 */
        MT2063_AddExclZone(pAS_Info, 1884548000 - pAS_Info->f_in, 1885948000 - pAS_Info->f_in);  /* Ctr = 1885.248 */
        MT2063_AddExclZone(pAS_Info, 1882820000 - pAS_Info->f_in, 1884220000 - pAS_Info->f_in);  /* Ctr = 1883.52  */
        MT2063_AddExclZone(pAS_Info, 1881092000 - pAS_Info->f_in, 1882492000 - pAS_Info->f_in);  /* Ctr = 1881.792 */
    }

#if MT_TUNER_CNT > 1
    /*
    ** Iterate through all adjacent tuners and exclude frequencies related to them
    */
    for (index = 0; index < TunerCount; ++index)
    {
        adj = TunerList[index];
        if (pAS_Info == adj)    /* skip over our own data, don't process it */
            continue;

        /*
        **  Add 1st IF exclusion zone covering adjacent tuner's LO2
        **  at "adjfLO2 + f_out" +/- m_MinLOSpacing
        */
        if (adj->f_LO2 != 0)
            MT2063_AddExclZone(pAS_Info,
                           (adj->f_LO2 + pAS_Info->f_out) - pAS_Info->f_min_LO_Separation,
                           (adj->f_LO2 + pAS_Info->f_out) + pAS_Info->f_min_LO_Separation );

        /*
        **  Add 1st IF exclusion zone covering adjacent tuner's LO1
        **  at "adjfLO1 - f_in" +/- m_MinLOSpacing
        */
        if (adj->f_LO1 != 0)
            MT2063_AddExclZone(pAS_Info,
                           (adj->f_LO1 - pAS_Info->f_in) - pAS_Info->f_min_LO_Separation,
                           (adj->f_LO1 - pAS_Info->f_in) + pAS_Info->f_min_LO_Separation );
    }
#endif
}


static struct MT2063_ExclZone_t* InsertNode(MT2063_AvoidSpursData_t* pAS_Info,
                                  struct MT2063_ExclZone_t* pPrevNode)
{
    struct MT2063_ExclZone_t* pNode;
    /*  Check for a node in the free list  */
    if (pAS_Info->freeZones != MT_NULL)
    {
        /*  Use one from the free list  */
        pNode = pAS_Info->freeZones;
        pAS_Info->freeZones = pNode->next_;
    }
    else
    {
        /*  Grab a node from the array  */
        pNode = &pAS_Info->MT_ExclZones[pAS_Info->nZones];
    }

    if (pPrevNode != MT_NULL)
    {
        pNode->next_ = pPrevNode->next_;
        pPrevNode->next_ = pNode;
    }
    else    /*  insert at the beginning of the list  */
    {
        pNode->next_ = pAS_Info->usedZones;
        pAS_Info->usedZones = pNode;
    }

    pAS_Info->nZones++;
    return pNode;
}


static struct MT2063_ExclZone_t* RemoveNode(MT2063_AvoidSpursData_t* pAS_Info,
                                  struct MT2063_ExclZone_t* pPrevNode,
                                  struct MT2063_ExclZone_t* pNodeToRemove)
{
    struct MT2063_ExclZone_t* pNext = pNodeToRemove->next_;

    /*  Make previous node point to the subsequent node  */
    if (pPrevNode != MT_NULL)
        pPrevNode->next_ = pNext;

    /*  Add pNodeToRemove to the beginning of the freeZones  */
    pNodeToRemove->next_ = pAS_Info->freeZones;
    pAS_Info->freeZones = pNodeToRemove;

    /*  Decrement node count  */
    pAS_Info->nZones--;

    return pNext;
}


/*****************************************************************************
**
**  Name: MT_AddExclZone
**
**  Description:    Add (and merge) an exclusion zone into the list.
**                  If the range (f_min, f_max) is totally outside the
**                  1st IF BW, ignore the entry.
**                  If the range (f_min, f_max) is negative, ignore the entry.
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   103   01-31-2005    DAD    Ver 1.14: In MT_AddExclZone(), if the range
**                              (f_min, f_max) < 0, ignore the entry.
**   199   08-06-2008    JWS    Ver 1.22: Indicate success of addition operation.
**
*****************************************************************************/
UData_t MT2063_AddExclZone(MT2063_AvoidSpursData_t* pAS_Info,
                       UData_t f_min,
                       UData_t f_max)
{
    UData_t status = 1;  /* Assume addition is possible */

    /*  Check to see if this overlaps the 1st IF filter  */
    if ((f_max > (pAS_Info->f_if1_Center - (pAS_Info->f_if1_bw / 2)))
        && (f_min < (pAS_Info->f_if1_Center + (pAS_Info->f_if1_bw / 2)))
        && (f_min < f_max))
    {
        /*
        **                1           2          3        4         5          6
        **
        **   New entry:  |---|    |--|        |--|       |-|      |---|         |--|
        **                     or          or        or       or        or
        **   Existing:  |--|          |--|      |--|    |---|      |-|      |--|
        */
        struct MT2063_ExclZone_t* pNode = pAS_Info->usedZones;
        struct MT2063_ExclZone_t* pPrev = MT_NULL;
        struct MT2063_ExclZone_t* pNext = MT_NULL;

        /*  Check for our place in the list  */
        while ((pNode != MT_NULL) && (pNode->max_ < f_min))
        {
            pPrev = pNode;
            pNode = pNode->next_;
        }

        if ((pNode != MT_NULL) && (pNode->min_ < f_max))
        {
            /*  Combine me with pNode  */
            if (f_min < pNode->min_)
                pNode->min_ = f_min;
            if (f_max > pNode->max_)
                pNode->max_ = f_max;
        }
        else
        {
            pNode = InsertNode(pAS_Info, pPrev);
            pNode->min_ = f_min;
            pNode->max_ = f_max;
        }

        /*  Look for merging possibilities  */
        pNext = pNode->next_;
        while ((pNext != MT_NULL) && (pNext->min_ < pNode->max_))
        {
            if (pNext->max_ > pNode->max_)
                pNode->max_ = pNext->max_;
            pNext = RemoveNode(pAS_Info, pNode, pNext);  /*  Remove pNext, return ptr to pNext->next  */
        }
    }
    else
        status = 0;  /* Did not add the zone - caller determines whether this is a problem */

    return status;
}


/*****************************************************************************
**
**  Name: MT_ChooseFirstIF
**
**  Description:    Choose the best available 1st IF
**                  If f_Desired is not excluded, choose that first.
**                  Otherwise, return the value closest to f_Center that is
**                  not excluded
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   117   03-29-2007    RSK    Ver 1.15: Re-wrote to match search order from
**                              tuner DLL.
**   147   07-27-2007    RSK    Ver 1.17: Corrected calculation (-) to (+)
**                              Added logic to force f_Center within 1/2 f_Step.
**
*****************************************************************************/
UData_t MT2063_ChooseFirstIF(MT2063_AvoidSpursData_t* pAS_Info)
{
    /*
    ** Update "f_Desired" to be the nearest "combinational-multiple" of "f_LO1_Step".
    ** The resulting number, F_LO1 must be a multiple of f_LO1_Step.  And F_LO1 is the arithmetic sum
    ** of f_in + f_Center.  Neither f_in, nor f_Center must be a multiple of f_LO1_Step.
    ** However, the sum must be.
    */
    const UData_t f_Desired = pAS_Info->f_LO1_Step * ((pAS_Info->f_if1_Request + pAS_Info->f_in + pAS_Info->f_LO1_Step/2) / pAS_Info->f_LO1_Step) - pAS_Info->f_in;
    const UData_t f_Step = (pAS_Info->f_LO1_Step > pAS_Info->f_LO2_Step) ? pAS_Info->f_LO1_Step : pAS_Info->f_LO2_Step;
    UData_t f_Center;

    SData_t i;
    SData_t j = 0;
    UData_t bDesiredExcluded = 0;
    UData_t bZeroExcluded = 0;
    SData_t tmpMin, tmpMax;
    SData_t bestDiff;
    struct MT2063_ExclZone_t* pNode = pAS_Info->usedZones;
    struct MT_FIFZone_t zones[MT2063_MAX_ZONES];

    if (pAS_Info->nZones == 0)
        return f_Desired;

    /*  f_Center needs to be an integer multiple of f_Step away from f_Desired */
    if (pAS_Info->f_if1_Center > f_Desired)
        f_Center = f_Desired + f_Step * ((pAS_Info->f_if1_Center - f_Desired + f_Step/2) / f_Step);
    else
        f_Center = f_Desired - f_Step * ((f_Desired - pAS_Info->f_if1_Center + f_Step/2) / f_Step);

//    assert(abs((SData_t) f_Center - (SData_t) pAS_Info->f_if1_Center) <= (SData_t) (f_Step/2));

    /*  Take MT_ExclZones, center around f_Center and change the resolution to f_Step  */
    while (pNode != MT_NULL)
    {
        /*  floor function  */
        tmpMin = floor((SData_t) (pNode->min_ - f_Center), (SData_t) f_Step);

        /*  ceil function  */
        tmpMax = ceil((SData_t) (pNode->max_ - f_Center), (SData_t) f_Step);

        if ((pNode->min_ < f_Desired) && (pNode->max_ > f_Desired))
            bDesiredExcluded = 1;

        if ((tmpMin < 0) && (tmpMax > 0))
            bZeroExcluded = 1;

        /*  See if this zone overlaps the previous  */
        if ((j>0) && (tmpMin < zones[j-1].max_))
            zones[j-1].max_ = tmpMax;
        else
        {
            /*  Add new zone  */
//            assert(j<MT2063_MAX_ZONES);
            zones[j].min_ = tmpMin;
            zones[j].max_ = tmpMax;
            j++;
        }
        pNode = pNode->next_;
    }

    /*
    **  If the desired is okay, return with it
    */
    if (bDesiredExcluded == 0)
        return f_Desired;

    /*
    **  If the desired is excluded and the center is okay, return with it
    */
    if (bZeroExcluded == 0)
        return f_Center;

    /*  Find the value closest to 0 (f_Center)  */
    bestDiff = zones[0].min_;
    for (i=0; i<j; i++)
    {
        if (abs(zones[i].min_) < abs(bestDiff)) bestDiff = zones[i].min_;
        if (abs(zones[i].max_) < abs(bestDiff)) bestDiff = zones[i].max_;
    }


    if (bestDiff < 0)
        return f_Center - ((UData_t) (-bestDiff) * f_Step);

    return f_Center + (bestDiff * f_Step);
}


/****************************************************************************
**
**  Name: gcd
**
**  Description:    Uses Euclid's algorithm
**
**  Parameters:     u, v     - unsigned values whose GCD is desired.
**
**  Global:         None
**
**  Returns:        greatest common divisor of u and v, if either value
**                  is 0, the other value is returned as the result.
**
**  Dependencies:   None.
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   06-01-2004    JWS    Original
**   N/A   08-03-2004    DAD    Changed to Euclid's since it can handle
**                              unsigned numbers.
**
****************************************************************************/
static UData_t gcd(UData_t u, UData_t v)
{
    UData_t r;

    while (v != 0)
    {
        r = u % v;
        u = v;
        v = r;
    }

    return u;
}

/****************************************************************************
**
**  Name: umax
**
**  Description:    Implements a simple maximum function for unsigned numbers.
**                  Implemented as a function rather than a macro to avoid
**                  multiple evaluation of the calling parameters.
**
**  Parameters:     a, b     - Values to be compared
**
**  Global:         None
**
**  Returns:        larger of the input values.
**
**  Dependencies:   None.
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   06-02-2004    JWS    Original
**
****************************************************************************/
static UData_t umax(UData_t a, UData_t b)
{
    return (a >= b) ? a : b;
}

#if MT_TUNER_CNT > 1
static SData_t RoundAwayFromZero(SData_t n, SData_t d)
{
    return (n<0) ? floor(n, d) : ceil(n, d);
}

/****************************************************************************
**
**  Name: IsSpurInAdjTunerBand
**
**  Description:    Checks to see if a spur will be present within the IF's
**                  bandwidth or near the zero IF.
**                  (fIFOut +/- fIFBW/2, -fIFOut +/- fIFBW/2)
**                                  and
**                  (0 +/- fZIFBW/2)
**
**                    ma   mb               me   mf               mc   md
**                  <--+-+-+-----------------+-+-+-----------------+-+-+-->
**                     |   ^                   0                   ^   |
**                     ^   b=-fIFOut+fIFBW/2      -b=+fIFOut-fIFBW/2   ^
**                     a=-fIFOut-fIFBW/2              -a=+fIFOut+fIFBW/2
**
**                  Note that some equations are doubled to prevent round-off
**                  problems when calculating fIFBW/2
**
**                  The spur frequencies are computed as:
**
**                     fSpur = n * f1 - m * f2 - fOffset
**
**  Parameters:     f1      - The 1st local oscillator (LO) frequency
**                            of the tuner whose output we are examining
**                  f2      - The 1st local oscillator (LO) frequency
**                            of the adjacent tuner
**                  fOffset - The 2nd local oscillator of the tuner whose
**                            output we are examining
**                  fIFOut  - Output IF center frequency
**                  fIFBW   - Output IF Bandwidth
**                  nMaxH   - max # of LO harmonics to search
**                  fp      - If spur, positive distance to spur-free band edge (returned)
**                  fm      - If spur, negative distance to spur-free band edge (returned)
**
**  Returns:        1 if an LO spur would be present, otherwise 0.
**
**  Dependencies:   None.
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   01-21-2005    JWS    Original, adapted from MT_DoubleConversion.
**   115   03-23-2007    DAD    Fix declaration of spur due to truncation
**                              errors.
**   137   06-18-2007    DAD    Ver 1.16: Fix possible divide-by-0 error for
**                              multi-tuners that have
**                              (delta IF1) > (f_out-f_outbw/2).
**   177 S 02-26-2008    RSK    Ver 1.18: Corrected calculation using LO1 > MAX/2
**                              Type casts added to preserve correct sign.
**   199   08-06-2008    JWS    Ver 1.22: Added 0x1x1 spur frequency calculations.
**
****************************************************************************/
static UData_t IsSpurInAdjTunerBand(UData_t bIsMyOutput,
                                    UData_t f1,
                                    UData_t f2,
                                    UData_t fOffset,
                                    UData_t fIFOut,
                                    UData_t fIFBW,
                                    UData_t fZIFBW,
                                    UData_t nMaxH,
                                    UData_t *fp,
                                    UData_t *fm)
{
    UData_t bSpurFound = 0;

    const UData_t fHalf_IFBW = fIFBW / 2;
    const UData_t fHalf_ZIFBW = fZIFBW / 2;

    /* Calculate a scale factor for all frequencies, so that our
       calculations all stay within 31 bits */
    const UData_t f_Scale = ((f1 + (fOffset + fIFOut + fHalf_IFBW) / nMaxH) / (MAX_UDATA/2 / nMaxH)) + 1;

    /*
    **  After this scaling, _f1, _f2, and _f3 are guaranteed to fit into
    **  signed data types (smaller than MAX_UDATA/2)
    */
    const SData_t _f1 = (SData_t) (     f1 / f_Scale);
    const SData_t _f2 = (SData_t) (     f2 / f_Scale);
    const SData_t _f3 = (SData_t) (fOffset / f_Scale);

    const SData_t c = (SData_t) (fIFOut - fHalf_IFBW) / (SData_t) f_Scale;
    const SData_t d = (SData_t) ((fIFOut + fHalf_IFBW) / f_Scale);
    const SData_t f = (SData_t) (fHalf_ZIFBW / f_Scale);

    SData_t  ma, mb, mc, md, me, mf;

    SData_t  fp_ = 0;
    SData_t  fm_ = 0;
    SData_t  n;


    /*
    **  If the other tuner does not have an LO frequency defined,
    **  assume that we cannot interfere with it
    */
    if (f2 == 0)
        return 0;


    /* Check out all multiples of f1 from -nMaxH to +nMaxH */
    for (n = -(SData_t)nMaxH; n <= (SData_t)nMaxH; ++n)
    {
        const SData_t nf1 = n*_f1;
        md = floor(_f3 + d - nf1, _f2);

        /* If # f2 harmonics > nMaxH, then no spurs present */
        if (md <= -(SData_t) nMaxH )
            break;

        ma = floor(_f3 - d - nf1, _f2);
        if ((ma == md) || (ma >= (SData_t) (nMaxH)))
            continue;

        mc = floor(_f3 + c - nf1, _f2);
        if (mc != md)
        {
            const SData_t m = md;
            const SData_t fspur = (nf1 + m*_f2 - _f3);
            const SData_t den = (bIsMyOutput ? n - 1 : n);
            if (den == 0)
            {
                fp_ = (d - fspur)* f_Scale;
                fm_ = (fspur - c)* f_Scale;
            }
            else
            {
                fp_ = (SData_t) RoundAwayFromZero((d - fspur)* f_Scale, den);
                fm_ = (SData_t) RoundAwayFromZero((fspur - c)* f_Scale, den);
            }
            if (((UData_t)abs(fm_) >= f_Scale) && ((UData_t)abs(fp_) >= f_Scale))
            {
                bSpurFound = 1;
                break;
            }
        }

        /* Location of Zero-IF-spur to be checked */
        mf = floor(_f3 + f - nf1, _f2);
        me = floor(_f3 - f - nf1, _f2);
        if (me != mf)
        {
            const SData_t m = mf;
            const SData_t fspur = (nf1 + m*_f2 - _f3);
            const SData_t den = (bIsMyOutput ? n - 1 : n);
            if (den == 0)
            {
                fp_ = (d - fspur)* f_Scale;
                fm_ = (fspur - c)* f_Scale;
            }
            else
            {
                fp_ = (SData_t) RoundAwayFromZero((f - fspur)* f_Scale, den);
                fm_ = (SData_t) RoundAwayFromZero((fspur + f)* f_Scale, den);
            }
            if (((UData_t)abs(fm_) >= f_Scale) && ((UData_t)abs(fp_) >= f_Scale))
            {
                bSpurFound = 1;
                break;
            }
        }

        mb = floor(_f3 - c - nf1, _f2);
        if (ma != mb)
        {
            const SData_t m = mb;
            const SData_t fspur = (nf1 + m*_f2 - _f3);
            const SData_t den = (bIsMyOutput ? n - 1 : n);
            if (den == 0)
            {
                fp_ = (d - fspur)* f_Scale;
                fm_ = (fspur - c)* f_Scale;
            }
            else
            {
                fp_ = (SData_t) RoundAwayFromZero((-c - fspur)* f_Scale, den);
                fm_ = (SData_t) RoundAwayFromZero((fspur +d)* f_Scale, den);
            }
            if (((UData_t)abs(fm_) >= f_Scale) && ((UData_t)abs(fp_) >= f_Scale))
            {
                bSpurFound = 1;
                break;
            }
        }
    }

    /*
    **  Verify that fm & fp are both positive
    **  Add one to ensure next 1st IF choice is not right on the edge
    */
    if (fp_ < 0)
    {
        *fp = -fm_ + 1;
        *fm = -fp_ + 1;
    }
    else if (fp_ > 0)
    {
        *fp = fp_ + 1;
        *fm = fm_ + 1;
    }
    else
    {
        *fp = 1;
        *fm = abs(fm_) + 1;
    }

    return bSpurFound;
}
#endif

/****************************************************************************
**
**  Name: IsSpurInBand
**
**  Description:    Checks to see if a spur will be present within the IF's
**                  bandwidth. (fIFOut +/- fIFBW, -fIFOut +/- fIFBW)
**
**                    ma   mb                                     mc   md
**                  <--+-+-+-------------------+-------------------+-+-+-->
**                     |   ^                   0                   ^   |
**                     ^   b=-fIFOut+fIFBW/2      -b=+fIFOut-fIFBW/2   ^
**                     a=-fIFOut-fIFBW/2              -a=+fIFOut+fIFBW/2
**
**                  Note that some equations are doubled to prevent round-off
**                  problems when calculating fIFBW/2
**
**  Parameters:     pAS_Info - Avoid Spurs information block
**                  fm       - If spur, amount f_IF1 has to move negative
**                  fp       - If spur, amount f_IF1 has to move positive
**
**  Global:         None
**
**  Returns:        1 if an LO spur would be present, otherwise 0.
**
**  Dependencies:   None.
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   11-28-2002    DAD    Implemented algorithm from applied patent
**
****************************************************************************/
static UData_t IsSpurInBand(MT2063_AvoidSpursData_t* pAS_Info,
                            UData_t* fm,
                            UData_t* fp)
{
    /*
    **  Calculate LO frequency settings.
    */
    UData_t n, n0;
    const UData_t f_LO1 = pAS_Info->f_LO1;
    const UData_t f_LO2 = pAS_Info->f_LO2;
    const UData_t d = pAS_Info->f_out + pAS_Info->f_out_bw/2;
    const UData_t c = d - pAS_Info->f_out_bw;
    const UData_t f = pAS_Info->f_zif_bw/2;
    const UData_t f_Scale = (f_LO1 / (MAX_UDATA/2 / pAS_Info->maxH1)) + 1;
    SData_t f_nsLO1, f_nsLO2;
    SData_t f_Spur;
    UData_t ma, mb, mc, md, me, mf;
    UData_t lo_gcd, gd_Scale, gc_Scale, gf_Scale, hgds, hgfs, hgcs;
#if MT_TUNER_CNT > 1
    UData_t index;

    MT2063_AvoidSpursData_t *adj;
#endif
    *fm = 0;

    /*
    ** For each edge (d, c & f), calculate a scale, based on the gcd
    ** of f_LO1, f_LO2 and the edge value.  Use the larger of this
    ** gcd-based scale factor or f_Scale.
    */
    lo_gcd = gcd(f_LO1, f_LO2);
    gd_Scale = umax((UData_t) gcd(lo_gcd, d), f_Scale);
    hgds = gd_Scale/2;
    gc_Scale = umax((UData_t) gcd(lo_gcd, c), f_Scale);
    hgcs = gc_Scale/2;
    gf_Scale = umax((UData_t) gcd(lo_gcd, f), f_Scale);
    hgfs = gf_Scale/2;

    n0 = uceil(f_LO2 - d, f_LO1 - f_LO2);

    /*  Check out all multiples of LO1 from n0 to m_maxLOSpurHarmonic  */
    for (n=n0; n<=pAS_Info->maxH1; ++n)
    {
        md = (n*((f_LO1+hgds)/gd_Scale) - ((d+hgds)/gd_Scale)) / ((f_LO2+hgds)/gd_Scale);

        /*  If # fLO2 harmonics > m_maxLOSpurHarmonic, then no spurs present  */
        if (md >= pAS_Info->maxH1)
            break;

        ma = (n*((f_LO1+hgds)/gd_Scale) + ((d+hgds)/gd_Scale)) / ((f_LO2+hgds)/gd_Scale);

        /*  If no spurs between +/- (f_out + f_IFBW/2), then try next harmonic  */
        if (md == ma)
            continue;

        mc = (n*((f_LO1+hgcs)/gc_Scale) - ((c+hgcs)/gc_Scale)) / ((f_LO2+hgcs)/gc_Scale);
        if (mc != md)
        {
            f_nsLO1 = (SData_t) (n*(f_LO1/gc_Scale));
            f_nsLO2 = (SData_t) (mc*(f_LO2/gc_Scale));
            f_Spur = (gc_Scale * (f_nsLO1 - f_nsLO2)) + n*(f_LO1 % gc_Scale) - mc*(f_LO2 % gc_Scale);

            *fp = ((f_Spur - (SData_t) c) / (mc - n)) + 1;
            *fm = (((SData_t) d - f_Spur) / (mc - n)) + 1;
            return 1;
        }

        /*  Location of Zero-IF-spur to be checked  */
        me = (n*((f_LO1+hgfs)/gf_Scale) + ((f+hgfs)/gf_Scale)) / ((f_LO2+hgfs)/gf_Scale);
        mf = (n*((f_LO1+hgfs)/gf_Scale) - ((f+hgfs)/gf_Scale)) / ((f_LO2+hgfs)/gf_Scale);
        if (me != mf)
        {
            f_nsLO1 = n*(f_LO1/gf_Scale);
            f_nsLO2 = me*(f_LO2/gf_Scale);
            f_Spur = (gf_Scale * (f_nsLO1 - f_nsLO2)) + n*(f_LO1 % gf_Scale) - me*(f_LO2 % gf_Scale);

            *fp = ((f_Spur + (SData_t) f) / (me - n)) + 1;
            *fm = (((SData_t) f - f_Spur) / (me - n)) + 1;
            return 1;
        }

        mb = (n*((f_LO1+hgcs)/gc_Scale) + ((c+hgcs)/gc_Scale)) / ((f_LO2+hgcs)/gc_Scale);
        if (ma != mb)
        {
            f_nsLO1 = n*(f_LO1/gc_Scale);
            f_nsLO2 = ma*(f_LO2/gc_Scale);
            f_Spur = (gc_Scale * (f_nsLO1 - f_nsLO2)) + n*(f_LO1 % gc_Scale) - ma*(f_LO2 % gc_Scale);

            *fp = (((SData_t) d + f_Spur) / (ma - n)) + 1;
            *fm = (-(f_Spur + (SData_t) c) / (ma - n)) + 1;
            return 1;
        }
    }

#if MT_TUNER_CNT > 1
    /*  If no spur found, see if there are more tuners on the same board  */
    for (index = 0; index < TunerCount; ++index)
    {
        adj = TunerList[index];
        if (pAS_Info == adj)    /* skip over our own data, don't process it */
            continue;

        /*  Look for LO-related spurs from the adjacent tuner generated into my IF output  */
        if (IsSpurInAdjTunerBand(1,                   /*  check my IF output                     */
                                 pAS_Info->f_LO1,     /*  my fLO1                                */
                                 adj->f_LO1,          /*  the other tuner's fLO1                 */
                                 pAS_Info->f_LO2,     /*  my fLO2                                */
                                 pAS_Info->f_out,     /*  my fOut                                */
                                 pAS_Info->f_out_bw,  /*  my output IF bandwidth                 */
                                 pAS_Info->f_zif_bw,  /*  my Zero-IF bandwidth                   */
                                 pAS_Info->maxH2,
                                 fp,                  /*  minimum amount to move LO's positive   */
                                 fm))                 /*  miminum amount to move LO's negative   */
            return 1;
        /*  Look for LO-related spurs from my tuner generated into the adjacent tuner's IF output  */
        if (IsSpurInAdjTunerBand(0,                   /*  check his IF output                    */
                                 pAS_Info->f_LO1,     /*  my fLO1                                */
                                 adj->f_LO1,          /*  the other tuner's fLO1                 */
                                 adj->f_LO2,          /*  the other tuner's fLO2                 */
                                 adj->f_out,          /*  the other tuner's fOut                 */
                                 adj->f_out_bw,       /*  the other tuner's output IF bandwidth  */
                                 pAS_Info->f_zif_bw,  /*  the other tuner's Zero-IF bandwidth    */
                                 adj->maxH2,
                                 fp,                  /*  minimum amount to move LO's positive   */
                                 fm))                 /*  miminum amount to move LO's negative   */
            return 1;
    }
#endif
    /*  No spurs found  */
    return 0;
}


/*****************************************************************************
**
**  Name: MT_AvoidSpurs
**
**  Description:    Main entry point to avoid spurs.
**                  Checks for existing spurs in present LO1, LO2 freqs
**                  and if present, chooses spur-free LO1, LO2 combination
**                  that tunes the same input/output frequencies.
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   096   04-06-2005    DAD    Ver 1.11: Fix divide by 0 error if maxH==0.
**
*****************************************************************************/
UData_t MT2063_AvoidSpurs(Handle_t h,
                      MT2063_AvoidSpursData_t* pAS_Info)
{
    UData_t status = MT_OK;
    UData_t fm, fp;                     /*  restricted range on LO's        */
    pAS_Info->bSpurAvoided = 0;
    pAS_Info->nSpursFound = 0;
    //h;                          /*  warning expected: code has no effect    */

    if (pAS_Info->maxH1 == 0)
        return MT_OK;

    /*
    **  Avoid LO Generated Spurs
    **
    **  Make sure that have no LO-related spurs within the IF output
    **  bandwidth.
    **
    **  If there is an LO spur in this band, start at the current IF1 frequency
    **  and work out until we find a spur-free frequency or run up against the
    **  1st IF SAW band edge.  Use temporary copies of fLO1 and fLO2 so that they
    **  will be unchanged if a spur-free setting is not found.
    */
    pAS_Info->bSpurPresent = IsSpurInBand(pAS_Info, &fm, &fp);
    if (pAS_Info->bSpurPresent)
    {
        UData_t zfIF1 = pAS_Info->f_LO1 - pAS_Info->f_in; /*  current attempt at a 1st IF  */
        UData_t zfLO1 = pAS_Info->f_LO1;         /*  current attempt at an LO1 freq  */
        UData_t zfLO2 = pAS_Info->f_LO2;         /*  current attempt at an LO2 freq  */
        UData_t delta_IF1;
        UData_t new_IF1;
        UData_t added;                           /*  success of Excl Zone addition  */

        /*
        **  Spur was found, attempt to find a spur-free 1st IF
        */
        do
        {
            pAS_Info->nSpursFound++;

            /*  Raise f_IF1_upper, if needed  */
            added = MT2063_AddExclZone(pAS_Info, zfIF1 - fm, zfIF1 + fp);

            /*
            **  If Exclusion Zone could NOT be added, then we'll just keep picking
            **  the same bad point, so exit this loop and report that there is a
            **  spur present in the output
            */
            if (!added)
                break;

            /*  Choose next IF1 that is closest to f_IF1_CENTER              */
            new_IF1 = MT2063_ChooseFirstIF(pAS_Info);

            if (new_IF1 > zfIF1)
            {
                pAS_Info->f_LO1 += (new_IF1 - zfIF1);
                pAS_Info->f_LO2 += (new_IF1 - zfIF1);
            }
            else
            {
                pAS_Info->f_LO1 -= (zfIF1 - new_IF1);
                pAS_Info->f_LO2 -= (zfIF1 - new_IF1);
            }
            zfIF1 = new_IF1;

            if (zfIF1 > pAS_Info->f_if1_Center)
                delta_IF1 = zfIF1 - pAS_Info->f_if1_Center;
            else
                delta_IF1 = pAS_Info->f_if1_Center - zfIF1;
        }
        /*
        **  Continue while the new 1st IF is still within the 1st IF bandwidth
        **  and there is a spur in the band (again)
        */
        while ((2*delta_IF1 + pAS_Info->f_out_bw <= pAS_Info->f_if1_bw) &&
              (pAS_Info->bSpurPresent = IsSpurInBand(pAS_Info, &fm, &fp)));

        /*
        ** Use the LO-spur free values found.  If the search went all the way to
        ** the 1st IF band edge and always found spurs, just leave the original
        ** choice.  It's as "good" as any other.
        */
        if (pAS_Info->bSpurPresent == 1)
        {
            status |= MT_SPUR_PRESENT;
            pAS_Info->f_LO1 = zfLO1;
            pAS_Info->f_LO2 = zfLO2;
        }
        else
            pAS_Info->bSpurAvoided = 1;
    }

    status |= ((pAS_Info->nSpursFound << MT_SPUR_SHIFT) & MT_SPUR_CNT_MASK);

    return (status);
}


UData_t MT2063_AvoidSpursVersion(void)
{
    return (MT_SPURAVOID_VERSION);
}




