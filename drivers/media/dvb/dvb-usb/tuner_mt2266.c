/**

@file

@brief   MT2266 tuner module definition

One can manipulate MT2266 tuner through MT2266 module.
MT2266 module is derived from tuner module.

*/


#include "tuner_mt2266.h"





/**

@brief   MT2266 tuner module builder

Use BuildMt2266Module() to build MT2266 module, set all module function pointers with the corresponding functions,
and initialize module private variables.


@param [in]   ppTuner                      Pointer to MT2266 tuner module pointer
@param [in]   pTunerModuleMemory           Pointer to an allocated tuner module memory
@param [in]   pBaseInterfaceModuleMemory   Pointer to an allocated base interface module memory
@param [in]   pI2cBridgeModuleMemory       Pointer to an allocated I2C bridge module memory
@param [in]   DeviceAddr                   MT2266 I2C device address


@note
	-# One should call BuildMt2266Module() to build MT2266 module before using it.

*/
void
BuildMt2266Module(
	TUNER_MODULE **ppTuner,
	TUNER_MODULE *pTunerModuleMemory,
	BASE_INTERFACE_MODULE *pBaseInterfaceModuleMemory,
	I2C_BRIDGE_MODULE *pI2cBridgeModuleMemory,
	unsigned char DeviceAddr
	)
{
	TUNER_MODULE *pTuner;
	MT2266_EXTRA_MODULE *pExtra;



	// Set tuner module pointer.
	*ppTuner = pTunerModuleMemory;

	// Get tuner module.
	pTuner = *ppTuner;

	// Set base interface module pointer and I2C bridge module pointer.
	pTuner->pBaseInterface = pBaseInterfaceModuleMemory;
	pTuner->pI2cBridge = pI2cBridgeModuleMemory;

	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Mt2266);



	// Set tuner type.
	pTuner->TunerType = TUNER_TYPE_MT2266;

	// Set tuner I2C device address.
	pTuner->DeviceAddr = DeviceAddr;


	// Initialize tuner parameter setting status.
	pTuner->IsRfFreqHzSet = NO;


	// Set tuner module manipulating function pointers.
	pTuner->GetTunerType  = mt2266_GetTunerType;
	pTuner->GetDeviceAddr = mt2266_GetDeviceAddr;

	pTuner->Initialize    = mt2266_Initialize;
	pTuner->SetRfFreqHz   = mt2266_SetRfFreqHz;
	pTuner->GetRfFreqHz   = mt2266_GetRfFreqHz;


	// Initialize tuner extra module variables.
	pExtra->IsBandwidthHzSet = NO;

	// Set tuner extra module function pointers.
	pExtra->OpenHandle     = mt2266_OpenHandle;
	pExtra->CloseHandle    = mt2266_CloseHandle;
	pExtra->GetHandle      = mt2266_GetHandle;
	pExtra->SetBandwidthHz = mt2266_SetBandwidthHz;
	pExtra->GetBandwidthHz = mt2266_GetBandwidthHz;


	return;
}





/**

@see   TUNER_FP_GET_TUNER_TYPE

*/
void
mt2266_GetTunerType(
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
mt2266_GetDeviceAddr(
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
mt2266_Initialize(
	TUNER_MODULE *pTuner
	)
{
	MT2266_EXTRA_MODULE *pExtra;

	Handle_t DeviceHandle;
	UData_t Status;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Mt2266);

	// Get tuner handle.
	DeviceHandle = pExtra->DeviceHandle;


	// Re-initialize tuner.
	Status = MT2266_ReInit(DeviceHandle);

	if(MT_IS_ERROR(Status))
		goto error_status_initialize_tuner;


	return FUNCTION_SUCCESS;


error_status_initialize_tuner:
	return FUNCTION_ERROR;
}





/**

@see   TUNER_FP_SET_RF_FREQ_HZ

*/
int
mt2266_SetRfFreqHz(
	TUNER_MODULE *pTuner,
	unsigned long RfFreqHz
	)
{
	MT2266_EXTRA_MODULE *pExtra;

	Handle_t DeviceHandle;
	UData_t Status;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Mt2266);

	// Get tuner handle.
	DeviceHandle = pExtra->DeviceHandle;


	// Set tuner RF frequency in Hz.
	Status = MT2266_ChangeFreq(DeviceHandle, RfFreqHz);

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
mt2266_GetRfFreqHz(
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

@brief   Open MT2266 tuner handle.

*/
int
mt2266_OpenHandle(
	TUNER_MODULE *pTuner
	)
{
	MT2266_EXTRA_MODULE *pExtra;

	unsigned char DeviceAddr;
	UData_t Status;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Mt2266);

	// Get tuner I2C device address.
	pTuner->GetDeviceAddr(pTuner, &DeviceAddr);


	// Open MT2266 handle.
	// Note: 1. Must take tuner extra module DeviceHandle as handle input argument.
	//       2. Take pTuner as user-defined data input argument.
	Status = MT2266_Open(DeviceAddr, &pExtra->DeviceHandle, pTuner);

	if(MT_IS_ERROR(Status))
		goto error_status_open_mt2266_handle;


	return FUNCTION_SUCCESS;


error_status_open_mt2266_handle:
	return FUNCTION_ERROR;
}





/**

@brief   Close MT2266 tuner handle.

*/
int
mt2266_CloseHandle(
	TUNER_MODULE *pTuner
	)
{
	MT2266_EXTRA_MODULE *pExtra;

	Handle_t DeviceHandle;
	UData_t Status;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Mt2266);

	// Get tuner handle.
	DeviceHandle = pExtra->DeviceHandle;


	// Close MT2266 handle.
	Status = MT2266_Close(DeviceHandle);

	if(MT_IS_ERROR(Status))
		goto error_status_open_mt2266_handle;


	return FUNCTION_SUCCESS;


error_status_open_mt2266_handle:
	return FUNCTION_ERROR;
}





/**

@brief   Get MT2266 tuner handle.

*/
void
mt2266_GetHandle(
	TUNER_MODULE *pTuner,
	void **pDeviceHandle
	)
{
	MT2266_EXTRA_MODULE *pExtra;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Mt2266);

	// Get tuner handle.
	*pDeviceHandle = pExtra->DeviceHandle;


	return;
}





/**

@brief   Set MT2266 tuner bandwidth in Hz.

*/
int
mt2266_SetBandwidthHz(
	TUNER_MODULE *pTuner,
	unsigned long BandwidthHz
	)
{
	MT2266_EXTRA_MODULE *pExtra;

	Handle_t DeviceHandle;
	UData_t Status;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Mt2266);

	// Get tuner handle.
	DeviceHandle = pExtra->DeviceHandle;


	// Set tuner bandwidth in Hz.
	Status = MT2266_SetParam(DeviceHandle, MT2266_OUTPUT_BW, BandwidthHz);

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

@brief   Get MT2266 tuner bandwidth in Hz.

*/
int
mt2266_GetBandwidthHz(
	TUNER_MODULE *pTuner,
	unsigned long *pBandwidthHz
	)
{
	MT2266_EXTRA_MODULE *pExtra;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Mt2266);


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
**  CVS ID:         $Id: mt_userdef.c,v 1.2 2006/10/26 16:39:18 software Exp $
**  CVS Source:     $Source: /export/home/cvsroot/software/tuners/MT2266/mt_userdef.c,v $
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
UData_t MT2266_WriteSub(Handle_t hUserData, 
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
UData_t MT2266_ReadSub(Handle_t hUserData, 
                   UData_t addr, 
                   U8Data subAddress, 
                   U8Data *pData, 
                   UData_t cnt)
{
  //  UData_t status = MT_OK;                        /* Status to be returned        */

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
void MT2266_Sleep(Handle_t hUserData, 
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























// Microtune source code - mt2266.c



/*****************************************************************************
**
**  Name: mt2266.c
**
**  Copyright 2007 Microtune, Inc. All Rights Reserved
**
**  This source code file contains confidential information and/or trade
**  secrets of Microtune, Inc. or its affiliates and is subject to the
**  terms of your confidentiality agreement with Microtune, Inc. or one of
**  its affiliates, as applicable.
**
*****************************************************************************/

/*****************************************************************************
**
**  Name: mt2266.c
**
**  Description:    Microtune MT2266 Tuner software interface.
**                  Supports tuners with Part/Rev code: 0x85.
**
**  Functions
**  Implemented:    UData_t  MT2266_Open
**                  UData_t  MT2266_Close
**                  UData_t  MT2266_ChangeFreq
**                  UData_t  MT2266_GetLocked
**                  UData_t  MT2266_GetParam
**                  UData_t  MT2266_GetReg
**                  UData_t  MT2266_GetUHFXFreqs
**                  UData_t  MT2266_GetUserData
**                  UData_t  MT2266_ReInit
**                  UData_t  MT2266_SetParam
**                  UData_t  MT2266_SetPowerModes
**                  UData_t  MT2266_SetReg
**                  UData_t  MT2266_SetUHFXFreqs
**
**  References:     AN-00010: MicroTuner Serial Interface Application Note
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
**                  - Delay execution for x milliseconds
**
**  CVS ID:         $Id: mt2266.c,v 1.5 2007/10/02 18:43:17 software Exp $
**  CVS Source:     $Source: /export/home/cvsroot/software/tuners/MT2266/mt2266.c,v $
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   05-30-2006    DAD    Ver 1.0: Modified version of mt2260.c (Ver 1.01).
**   N/A   06-08-2006    JWS    Ver 1.01: Corrected problem with tuner ID check
**   N/A   11-01-2006    RSK    Ver 1.02: Adding multiple-filter support
**                                        as well as Get/Set functions.
**   N/A   11-29-2006    DAD    Ver 1.03: Parenthesis clarification for gcc
**   N/A   12-20-2006    RSK    Ver 1.04: Adding fLO_FractionalTerm() usage.
**   118   05-09-2007    RSK    Ver 1.05: Adding Standard MTxxxx_Tune() API.
**
*****************************************************************************/
//#include "mt2266.h"
//#include <stdlib.h>                     /* for NULL */
#define MT_NULL			0

/*  Version of this module                          */
#define VERSION 10005             /*  Version 01.05 */


#ifndef MT2266_CNT
#error You must define MT2266_CNT in the "mt_userdef.h" file
#endif

/*
**  Normally, the "reg" array in the tuner structure is used as a cache
**  containing the current value of the tuner registers.  If the user's
**  application MUST change tuner registers without using the MT2266_SetReg
**  routine provided, he may compile this code with the __NO_CACHE__
**  variable defined.
**  The PREFETCH macro will insert code code to re-read tuner registers if
**  __NO_CACHE__ is defined.  If it is not defined (normal) then PREFETCH
**  does nothing.
*/

#if defined(__NO_CACHE__)
#define PREFETCH(var, cnt) \
    if (MT_NO_ERROR(status)) \
    status |= MT2266_ReadSub(pInfo->hUserData, pInfo->address, (var), &pInfo->reg[(var)], (cnt));
#else
#define PREFETCH(var, cnt)
#endif



/*
**  Two-wire serial bus subaddresses of the tuner registers.
**  Also known as the tuner's register addresses.
*/
enum MT2266_Register_Offsets
{
    MT2266_PART_REV = 0,   /*  0x00 */
    MT2266_LO_CTRL_1,      /*  0x01 */
    MT2266_LO_CTRL_2,      /*  0x02 */
    MT2266_LO_CTRL_3,      /*  0x03 */
    MT2266_SMART_ANT,      /*  0x04 */
    MT2266_BAND_CTRL,      /*  0x05 */
    MT2266_CLEARTUNE,      /*  0x06 */
    MT2266_IGAIN,          /*  0x07 */
    MT2266_BBFILT_1,       /*  0x08 */
    MT2266_BBFILT_2,       /*  0x09 */
    MT2266_BBFILT_3,       /*  0x0A */
    MT2266_BBFILT_4,       /*  0x0B */
    MT2266_BBFILT_5,       /*  0x0C */
    MT2266_BBFILT_6,       /*  0x0D */
    MT2266_BBFILT_7,       /*  0x0E */
    MT2266_BBFILT_8,       /*  0x0F */
    MT2266_RCC_CTRL,       /*  0x10 */
    MT2266_RSVD_11,        /*  0x11 */
    MT2266_STATUS_1,       /*  0x12 */
    MT2266_STATUS_2,       /*  0x13 */
    MT2266_STATUS_3,       /*  0x14 */
    MT2266_STATUS_4,       /*  0x15 */
    MT2266_STATUS_5,       /*  0x16 */
    MT2266_SRO_CTRL,       /*  0x17 */
    MT2266_RSVD_18,        /*  0x18 */
    MT2266_RSVD_19,        /*  0x19 */
    MT2266_RSVD_1A,        /*  0x1A */
    MT2266_RSVD_1B,        /*  0x1B */
    MT2266_ENABLES,        /*  0x1C */
    MT2266_RSVD_1D,        /*  0x1D */
    MT2266_RSVD_1E,        /*  0x1E */
    MT2266_RSVD_1F,        /*  0x1F */
    MT2266_GPO,            /*  0x20 */
    MT2266_RSVD_21,        /*  0x21 */
    MT2266_RSVD_22,        /*  0x22 */
    MT2266_RSVD_23,        /*  0x23 */
    MT2266_RSVD_24,        /*  0x24 */
    MT2266_RSVD_25,        /*  0x25 */
    MT2266_RSVD_26,        /*  0x26 */
    MT2266_RSVD_27,        /*  0x27 */
    MT2266_RSVD_28,        /*  0x28 */
    MT2266_RSVD_29,        /*  0x29 */
    MT2266_RSVD_2A,        /*  0x2A */
    MT2266_RSVD_2B,        /*  0x2B */
    MT2266_RSVD_2C,        /*  0x2C */
    MT2266_RSVD_2D,        /*  0x2D */
    MT2266_RSVD_2E,        /*  0x2E */
    MT2266_RSVD_2F,        /*  0x2F */
    MT2266_RSVD_30,        /*  0x30 */
    MT2266_RSVD_31,        /*  0x31 */
    MT2266_RSVD_32,        /*  0x32 */
    MT2266_RSVD_33,        /*  0x33 */
    MT2266_RSVD_34,        /*  0x34 */
    MT2266_RSVD_35,        /*  0x35 */
    MT2266_RSVD_36,        /*  0x36 */
    MT2266_RSVD_37,        /*  0x37 */
    MT2266_RSVD_38,        /*  0x38 */
    MT2266_RSVD_39,        /*  0x39 */
    MT2266_RSVD_3A,        /*  0x3A */
    MT2266_RSVD_3B,        /*  0x3B */
    MT2266_RSVD_3C,        /*  0x3C */
    END_REGS
};

/*
** DefaultsEntry points to an array of U8Data used to initialize
** various registers (the first byte is the starting subaddress)
** and a count of the bytes (including subaddress) in the array.
**
** DefaultsList is an array of DefaultsEntry elements terminated
** by an entry with a NULL pointer for the data array.
*/
typedef struct MT2266_DefaultsEntryTag
{
    U8Data *data;
    UData_t cnt;
} MT2266_DefaultsEntry;

typedef MT2266_DefaultsEntry MT2266_DefaultsList[];

#define DEF_LIST_ENTRY(a) {a, sizeof(a)/sizeof(U8Data) - 1}
#define END_DEF_LIST {0,0}

/*
** Constants used by the tuning algorithm
*/
                                        /* REF_FREQ is now the actual crystal frequency */
#define REF_FREQ          (30000000UL)  /* Reference oscillator Frequency (in Hz) */
#define TUNE_STEP_SIZE          (50UL)  /* Tune in steps of 50 kHz */
#define MIN_UHF_FREQ     (350000000UL)  /* Minimum UHF frequency (in Hz) */
#define MAX_UHF_FREQ     (900000000UL)  /* Maximum UHF frequency (in Hz) */
#define MIN_VHF_FREQ     (174000000UL)  /* Minimum VHF frequency (in Hz) */
#define MAX_VHF_FREQ     (230000000UL)  /* Maximum VHF frequency (in Hz) */
#define OUTPUT_BW          (8000000UL)  /* Output channel bandwidth (in Hz) */
#define UHF_DEFAULT_FREQ (600000000UL)  /* Default UHF input frequency (in Hz) */


/*
**  The number of Tuner Registers
*/
static const UData_t Num_Registers = END_REGS;

/*
**  Crossover Frequency sets for 2 filters, without and with attenuation.
*/
typedef struct
{
    MT2266_XFreq_Set    xfreq[ MT2266_NUMBER_OF_XFREQ_SETS ];

}  MT2266_XFreqs_t;


MT2266_XFreqs_t MT2266_default_XFreqs =
{
    /*  xfreq  */
    {
        /*  uhf0  */
        {                                /*          < 0 MHz: 15+1 */
            0UL,                         /*    0 ..    0 MHz: 15 */
            0UL,                         /*    0 ..  443 MHz: 14 */
            443000 / TUNE_STEP_SIZE,     /*  443 ..  470 MHz: 13 */
            470000 / TUNE_STEP_SIZE,     /*  470 ..  496 MHz: 12 */
            496000 / TUNE_STEP_SIZE,     /*  496 ..  525 MHz: 11 */
            525000 / TUNE_STEP_SIZE,     /*  525 ..  552 MHz: 10 */
            552000 / TUNE_STEP_SIZE,     /*  552 ..  580 MHz:  9 */
            580000 / TUNE_STEP_SIZE,     /*  580 ..  657 MHz:  8 */
            657000 / TUNE_STEP_SIZE,     /*  657 ..  682 MHz:  7 */
            682000 / TUNE_STEP_SIZE,     /*  682 ..  710 MHz:  6 */
            710000 / TUNE_STEP_SIZE,     /*  710 ..  735 MHz:  5 */
            735000 / TUNE_STEP_SIZE,     /*  735 ..  763 MHz:  4 */
            763000 / TUNE_STEP_SIZE,     /*  763 ..  802 MHz:  3 */
            802000 / TUNE_STEP_SIZE,     /*  802 ..  840 MHz:  2 */
            840000 / TUNE_STEP_SIZE,     /*  840 ..  877 MHz:  1 */
            877000 / TUNE_STEP_SIZE      /*  877+        MHz:  0 */
        },

        /*  uhf1  */
        {                                /*        < 443 MHz: 15+1 */
            443000 / TUNE_STEP_SIZE,     /*  443 ..  470 MHz: 15 */
            470000 / TUNE_STEP_SIZE,     /*  470 ..  496 MHz: 14 */
            496000 / TUNE_STEP_SIZE,     /*  496 ..  525 MHz: 13 */
            525000 / TUNE_STEP_SIZE,     /*  525 ..  552 MHz: 12 */
            552000 / TUNE_STEP_SIZE,     /*  552 ..  580 MHz: 11 */
            580000 / TUNE_STEP_SIZE,     /*  580 ..  605 MHz: 10 */
            605000 / TUNE_STEP_SIZE,     /*  605 ..  632 MHz:  9 */
            632000 / TUNE_STEP_SIZE,     /*  632 ..  657 MHz:  8 */
            657000 / TUNE_STEP_SIZE,     /*  657 ..  682 MHz:  7 */
            682000 / TUNE_STEP_SIZE,     /*  682 ..  710 MHz:  6 */
            710000 / TUNE_STEP_SIZE,     /*  710 ..  735 MHz:  5 */
            735000 / TUNE_STEP_SIZE,     /*  735 ..  763 MHz:  4 */
            763000 / TUNE_STEP_SIZE,     /*  763 ..  802 MHz:  3 */
            802000 / TUNE_STEP_SIZE,     /*  802 ..  840 MHz:  2 */
            840000 / TUNE_STEP_SIZE,     /*  840 ..  877 MHz:  1 */
            877000 / TUNE_STEP_SIZE      /*  877+        MHz:  0 */
        },

        /*  uhf0_a  */
        {                                /*        <   0 MHz: 15+1 */
            0UL,                         /*    0 ..    0 MHz: 15 */
            0UL,                         /*    0 ..  442 MHz: 14 */
            442000 / TUNE_STEP_SIZE,     /*  442 ..  472 MHz: 13 */
            472000 / TUNE_STEP_SIZE,     /*  472 ..  505 MHz: 12 */
            505000 / TUNE_STEP_SIZE,     /*  505 ..  535 MHz: 11 */
            535000 / TUNE_STEP_SIZE,     /*  535 ..  560 MHz: 10 */
            560000 / TUNE_STEP_SIZE,     /*  560 ..  593 MHz:  9 */
            593000 / TUNE_STEP_SIZE,     /*  593 ..  673 MHz:  8 */
            673000 / TUNE_STEP_SIZE,     /*  673 ..  700 MHz:  7 */
            700000 / TUNE_STEP_SIZE,     /*  700 ..  727 MHz:  6 */
            727000 / TUNE_STEP_SIZE,     /*  727 ..  752 MHz:  5 */
            752000 / TUNE_STEP_SIZE,     /*  752 ..  783 MHz:  4 */
            783000 / TUNE_STEP_SIZE,     /*  783 ..  825 MHz:  3 */
            825000 / TUNE_STEP_SIZE,     /*  825 ..  865 MHz:  2 */
            865000 / TUNE_STEP_SIZE,     /*  865 ..  905 MHz:  1 */
            905000 / TUNE_STEP_SIZE      /*  905+        MHz:  0 */
        },

        /*  uhf1_a  */
        {                                /*        < 442 MHz: 15+1 */
            442000 / TUNE_STEP_SIZE,     /*  442 ..  472 MHz: 15 */
            472000 / TUNE_STEP_SIZE,     /*  472 ..  505 MHz: 14 */
            505000 / TUNE_STEP_SIZE,     /*  505 ..  535 MHz: 13 */
            535000 / TUNE_STEP_SIZE,     /*  535 ..  560 MHz: 12 */
            560000 / TUNE_STEP_SIZE,     /*  560 ..  593 MHz: 11 */
            593000 / TUNE_STEP_SIZE,     /*  593 ..  620 MHz: 10 */
            620000 / TUNE_STEP_SIZE,     /*  620 ..  647 MHz:  9 */
            647000 / TUNE_STEP_SIZE,     /*  647 ..  673 MHz:  8 */
            673000 / TUNE_STEP_SIZE,     /*  673 ..  700 MHz:  7 */
            700000 / TUNE_STEP_SIZE,     /*  700 ..  727 MHz:  6 */
            727000 / TUNE_STEP_SIZE,     /*  727 ..  752 MHz:  5 */
            752000 / TUNE_STEP_SIZE,     /*  752 ..  783 MHz:  4 */
            783000 / TUNE_STEP_SIZE,     /*  783 ..  825 MHz:  3 */
            825000 / TUNE_STEP_SIZE,     /*  825 ..  865 MHz:  2 */
            865000 / TUNE_STEP_SIZE,     /*  865 ..  905 MHz:  1 */
            905000 / TUNE_STEP_SIZE      /*  905+        MHz:  0 */
        }
    }
};

typedef struct
{
    Handle_t    handle;
    Handle_t    hUserData;
    UData_t     address;
    UData_t     version;
    UData_t     tuner_id;
    UData_t     f_Ref;
    UData_t     f_Step;
    UData_t     f_in;
    UData_t     f_LO;
    UData_t     f_bw;
    UData_t     band;
    UData_t     num_regs;
    U8Data      RC2_Value;
    U8Data      RC2_Nominal;
    U8Data      reg[END_REGS];

    MT2266_XFreqs_t xfreqs;

}  MT2266_Info_t;

static UData_t nMaxTuners = MT2266_CNT;
static MT2266_Info_t MT2266_Info[MT2266_CNT];
static MT2266_Info_t *Avail[MT2266_CNT];
static UData_t nOpenTuners = 0;

/*
**  Constants used to write a minimal set of registers when changing bands.
**  If the user wants a total reset, they should call MT2266_Open() again.
**  Skip 01, 02, 03, 04  (get overwritten anyways)
**  Write 05
**  Skip 06 - 18
**  Write 19   (diff for L-Band)
**  Skip 1A 1B 1C
**  Write 1D - 2B
**  Skip 2C - 3C
*/

static U8Data MT2266_VHF_defaults1[] =
{
    0x05,              /* address 0xC0, reg 0x05 */
    0x04,              /* Reg 0x05 LBANDen = 1 (that's right)*/
};
static U8Data MT2266_VHF_defaults2[] =
{
    0x19,              /* address 0xC0, reg 0x19 */
    0x61,              /* Reg 0x19 CAPto = 3*/
};
static U8Data MT2266_VHF_defaults3[] =
{
    0x1D,              /* address 0xC0, reg 0x1D */
    0xFE,              /* reg 0x1D */
    0x00,              /* reg 0x1E */
    0x00,              /* reg 0x1F */
    0xB4,              /* Reg 0x20 GPO = 1*/
    0x03,              /* Reg 0x21 LBIASen = 1, UBIASen = 1*/
    0xA5,              /* Reg 0x22 */
    0xA5,              /* Reg 0x23 */
    0xA5,              /* Reg 0x24 */
    0xA5,              /* Reg 0x25 */
    0x82,              /* Reg 0x26 CASCM = b0001 (bits reversed)*/
    0xAA,              /* Reg 0x27 */
    0xF1,              /* Reg 0x28 */
    0x17,              /* Reg 0x29 */
    0x80,              /* Reg 0x2A MIXbiasen = 1*/
    0x1F,              /* Reg 0x2B */
};

static MT2266_DefaultsList MT2266_VHF_defaults = {
    DEF_LIST_ENTRY(MT2266_VHF_defaults1),
    DEF_LIST_ENTRY(MT2266_VHF_defaults2),
    DEF_LIST_ENTRY(MT2266_VHF_defaults3),
    END_DEF_LIST
};

static U8Data MT2266_UHF_defaults1[] =
{
    0x05,              /* address 0xC0, reg 0x05 */
    0x52,              /* Reg 0x05 */
};
static U8Data MT2266_UHF_defaults2[] =
{
    0x19,              /* address 0xC0, reg 0x19 */
    0x61,              /* Reg 0x19 CAPto = 3*/
};
static U8Data MT2266_UHF_defaults3[] =
{
    0x1D,              /* address 0xC0, reg 0x1D */
    0xDC,              /* Reg 0x1D */
    0x00,              /* Reg 0x1E */
    0x0A,              /* Reg 0x1F */
    0xD4,              /* Reg 0x20 GPO = 1*/
    0x03,              /* Reg 0x21 LBIASen = 1, UBIASen = 1*/
    0x64,              /* Reg 0x22 */
    0x64,              /* Reg 0x23 */
    0x64,              /* Reg 0x24 */
    0x64,              /* Reg 0x25 */
    0x22,              /* Reg 0x26 CASCM = b0100 (bits reversed)*/
    0xAA,              /* Reg 0x27 */
    0xF2,              /* Reg 0x28 */
    0x1E,              /* Reg 0x29 */
    0x80,              /* Reg 0x2A MIXbiasen = 1*/
    0x14,              /* Reg 0x2B */
};

static MT2266_DefaultsList MT2266_UHF_defaults = {
    DEF_LIST_ENTRY(MT2266_UHF_defaults1),
    DEF_LIST_ENTRY(MT2266_UHF_defaults2),
    DEF_LIST_ENTRY(MT2266_UHF_defaults3),
    END_DEF_LIST
};


static UData_t UncheckedSet(MT2266_Info_t* pInfo,
                            U8Data         reg,
                            U8Data         val);

static UData_t UncheckedGet(MT2266_Info_t* pInfo,
                            U8Data   reg,
                            U8Data*  val);


/******************************************************************************
**
**  Name: MT2266_Open
**
**  Description:    Initialize the tuner's register values.
**
**  Parameters:     MT2266_Addr  - Serial bus address of the tuner.
**                  hMT2266      - Tuner handle passed back.
**                  hUserData    - User-defined data, if needed for the
**                                 MT_ReadSub() & MT_WriteSub functions.
**
**  Returns:        status:
**                      MT_OK             - No errors
**                      MT_TUNER_ID_ERR   - Tuner Part/Rev code mismatch
**                      MT_TUNER_INIT_ERR - Tuner initialization failed
**                      MT_COMM_ERR       - Serial bus communications error
**                      MT_ARG_NULL       - Null pointer argument passed
**                      MT_TUNER_CNT_ERR  - Too many tuners open
**
**  Dependencies:   MT_ReadSub  - Read byte(s) of data from the two-wire bus
**                  MT_WriteSub - Write byte(s) of data to the two-wire bus
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   05-30-2006    DAD    Ver 1.0: Modified version of mt2260.c (Ver 1.01).
**   N/A   11-01-2006    RSK    Ver 1.02: Initialize Crossover Tables to Default
**
******************************************************************************/
UData_t MT2266_Open(UData_t MT2266_Addr,
                    Handle_t* hMT2266,
                    Handle_t hUserData)
{
    UData_t status = MT_OK;             /*  Status to be returned.  */
    SData_t i, j;
    MT2266_Info_t* pInfo = MT_NULL;
 

    /*  Check the argument before using  */
    if (hMT2266 == MT_NULL)
        return MT_ARG_NULL;
    *hMT2266 = MT_NULL;

    /*
    **  If this is our first tuner, initialize the address fields and
    **  the list of available control blocks.
    */
    if (nOpenTuners == 0)
    {
        for (i=MT2266_CNT-1; i>=0; i--)
        {
            MT2266_Info[i].handle = MT_NULL;
            MT2266_Info[i].address =MAX_UDATA;
            MT2266_Info[i].hUserData = MT_NULL;

            /* Reset the UHF Crossover Frequency tables on open/init. */
            for (j=0; j< MT2266_NUM_XFREQS; j++ )
            {
                MT2266_Info[i].xfreqs.xfreq[MT2266_UHF0][j]       = MT2266_default_XFreqs.xfreq[MT2266_UHF0][j];
                MT2266_Info[i].xfreqs.xfreq[MT2266_UHF1][j]       = MT2266_default_XFreqs.xfreq[MT2266_UHF1][j];
                MT2266_Info[i].xfreqs.xfreq[MT2266_UHF0_ATTEN][j] = MT2266_default_XFreqs.xfreq[MT2266_UHF0_ATTEN][j];
                MT2266_Info[i].xfreqs.xfreq[MT2266_UHF1_ATTEN][j] = MT2266_default_XFreqs.xfreq[MT2266_UHF1_ATTEN][j];
            }

            Avail[i] = &MT2266_Info[i];
        }
    }

    /*
    **  Look for an existing MT2266_State_t entry with this address.
    */
    for (i=MT2266_CNT-1; i>=0; i--)
    {
        /*
        **  If an open'ed handle provided, we'll re-initialize that structure.
        **
        **  We recognize an open tuner because the address and hUserData are
        **  the same as one that has already been opened
        */
        if ((MT2266_Info[i].address == MT2266_Addr) &&
            (MT2266_Info[i].hUserData == hUserData))
        {
            pInfo = &MT2266_Info[i];
            break;
        }
    }

    /*  If not found, choose an empty spot.  */
    if (pInfo == MT_NULL)
    {
        /*  Check to see that we're not over-allocating.  */
        if (nOpenTuners == MT2266_CNT)
            return MT_TUNER_CNT_ERR;

        /* Use the next available block from the list */
        pInfo = Avail[nOpenTuners];
        nOpenTuners++;
    }

    pInfo->handle = (Handle_t) pInfo;
    pInfo->hUserData = hUserData;
    pInfo->address = MT2266_Addr;

//    status |= MT2266_ReInit((Handle_t) pInfo);

    if (MT_IS_ERROR(status))
        MT2266_Close((Handle_t) pInfo);
    else
        *hMT2266 = pInfo->handle;

    return (status);
}


static UData_t IsValidHandle(MT2266_Info_t* handle)
{
    return ((handle != MT_NULL) && (handle->handle == handle)) ? 1 : 0;
}


/******************************************************************************
**
**  Name: MT2266_Close
**
**  Description:    Release the handle to the tuner.
**
**  Parameters:     hMT2266      - Handle to the MT2266 tuner
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
**   N/A   05-30-2006    DAD    Ver 1.0: Modified version of mt2260.c (Ver 1.01).
**
******************************************************************************/
UData_t MT2266_Close(Handle_t hMT2266)
{
    MT2266_Info_t* pInfo = (MT2266_Info_t*) hMT2266;

    if (!IsValidHandle(pInfo))
        return MT_INV_HANDLE;

    /* Remove the tuner from our list of tuners */
    pInfo->handle = MT_NULL;
    pInfo->address = MAX_UDATA;
    pInfo->hUserData = MT_NULL;
    nOpenTuners--;
    Avail[nOpenTuners] = pInfo; /* Return control block to available list */

    return MT_OK;
}


/******************************************************************************
**
**  Name: Run_BB_RC_Cal2
**
**  Description:    Run Base Band RC Calibration (Method 2)
**                  MT2266 B0 only, others return MT_OK
**
**  Parameters:     hMT2266      - Handle to the MT2266 tuner
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_COMM_ERR      - Serial bus communications error
**                      MT_INV_HANDLE    - Invalid tuner handle
**
**  Dependencies:   mt_errordef.h - definition of error codes
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   05-30-2006    DAD    Ver 1.0: Modified version of mt2260.c (Ver 1.01).
**
******************************************************************************/
static UData_t Run_BB_RC_Cal2(Handle_t h)
{
    UData_t status = MT_OK;                  /* Status to be returned */
    U8Data tmp_rcc;
    U8Data dumy;

    MT2266_Info_t* pInfo = (MT2266_Info_t*) h;

    /*  Verify that the handle passed points to a valid tuner         */
    if (IsValidHandle(pInfo) == 0)
        status |= MT_INV_HANDLE;

    /*
    ** Set the crystal frequency in the calibration register
    ** and enable RC calibration #2
    */
    PREFETCH(MT2266_RCC_CTRL, 1);  /* Fetch register(s) if __NO_CACHE__ defined */
    tmp_rcc = pInfo->reg[MT2266_RCC_CTRL];
    if (pInfo->f_Ref < (36000000 /*/ TUNE_STEP_SIZE*/))
        tmp_rcc = (tmp_rcc & 0xDF) | 0x10;
    else
        tmp_rcc |= 0x30;
    status |= UncheckedSet(pInfo, MT2266_RCC_CTRL, tmp_rcc);

    /*  Read RC Calibration value  */
    status |= UncheckedGet(pInfo, MT2266_STATUS_4, &dumy);

    /* Disable RC Cal 2 */
    status |= UncheckedSet(pInfo, MT2266_RCC_CTRL, pInfo->reg[MT2266_RCC_CTRL] & 0xEF);

    /* Store RC Cal 2 value */
    pInfo->RC2_Value = pInfo->reg[MT2266_STATUS_4];

    if (pInfo->f_Ref < (36000000 /*/ TUNE_STEP_SIZE*/))
        pInfo->RC2_Nominal = (U8Data) ((pInfo->f_Ref + 77570) / 155139);
    else
        pInfo->RC2_Nominal = (U8Data) ((pInfo->f_Ref + 93077) / 186154);

    return (status);
}


/******************************************************************************
**
**  Name: Set_BBFilt
**
**  Description:    Set Base Band Filter bandwidth
**                  Based on SRO frequency & BB RC Calibration
**                  User stores channel bw as 5-8 MHz.  This routine
**                  calculates a 3 dB corner bw based on 1/2 the bandwidth
**                  and a bandwidth related constant.
**
**  Parameters:     hMT2266      - Handle to the MT2266 tuner
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_COMM_ERR      - Serial bus communications error
**                      MT_INV_HANDLE    - Invalid tuner handle
**
**  Dependencies:   mt_errordef.h - definition of error codes
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   05-30-2006    DAD    Ver 1.0: Modified version of mt2260.c (Ver 1.01).
**
******************************************************************************/
static UData_t Set_BBFilt(Handle_t h)
{
    UData_t f_3dB_bw;
    U8Data BBFilt = 0;
    U8Data Sel = 0;
    SData_t TmpFilt;
    SData_t i;
    UData_t status = MT_OK;                  /* Status to be returned */

    MT2266_Info_t* pInfo = (MT2266_Info_t*) h;

    /*  Verify that the handle passed points to a valid tuner         */
    if (IsValidHandle(pInfo) == 0)
        status |= MT_INV_HANDLE;

	/* Check RC2_Value value */
	if(pInfo->RC2_Value == 0)
		return MT_UNKNOWN;

    /*
    ** Convert the channel bandwidth into a 3 dB bw by dividing it by 2
    ** and subtracting 300, 250, 200, or 0 kHz based on 8, 7, 6, 5 MHz
    ** channel bandwidth.
    */
    f_3dB_bw = (pInfo->f_bw / 2);  /* bw -> bw/2 */
    if (pInfo->f_bw > 7500000)
    {
        /*  >3.75 MHz corner  */
        f_3dB_bw -= 300000;
        Sel = 0x00;
        TmpFilt = ((429916107 / pInfo->RC2_Value) * pInfo->RC2_Nominal) / f_3dB_bw - 81;
    }
    else if (pInfo->f_bw > 6500000)
    {
        /*  >3.25 MHz .. 3.75 MHz corner  */
        f_3dB_bw -= 250000;
        Sel = 0x00;
        TmpFilt = ((429916107 / pInfo->RC2_Value) * pInfo->RC2_Nominal) / f_3dB_bw - 81;
    }
    else if (pInfo->f_bw > 5500000)
    {
        /*  >2.75 MHz .. 3.25 MHz corner  */
        f_3dB_bw -= 200000;
        Sel = 0x80;
        TmpFilt = ((429916107 / pInfo->RC2_Value) * pInfo->RC2_Nominal) / f_3dB_bw - 113;
    }
    else
    {
        /*  <= 2.75 MHz corner  */
        Sel = 0xC0;
        TmpFilt = ((429916107 / pInfo->RC2_Value) * pInfo->RC2_Nominal) / f_3dB_bw - 129;
    }

    if (TmpFilt > 63)
        TmpFilt = 63;
    else if (TmpFilt < 0)
        TmpFilt = 0;
    BBFilt = ((U8Data) TmpFilt) | Sel;

    for ( i = MT2266_BBFILT_1; i <= MT2266_BBFILT_8; i++ )
        pInfo->reg[i] = BBFilt;

    if (MT_NO_ERROR(status))
        status |= MT2266_WriteSub(pInfo->hUserData,
                              pInfo->address,
                              MT2266_BBFILT_1,
                              &pInfo->reg[MT2266_BBFILT_1],
                              8);

    return (status);
}


/****************************************************************************
**
**  Name: MT2266_GetLocked
**
**  Description:    Checks to see if the PLL is locked.
**
**  Parameters:     h            - Open handle to the tuner (from MT2266_Open).
**
**  Returns:        status:
**                      MT_OK            - No errors
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
**   N/A   05-30-2006    DAD    Ver 1.0: Modified version of mt2260.c (Ver 1.01).
**
****************************************************************************/
UData_t MT2266_GetLocked(Handle_t h)
{
    const UData_t nMaxWait = 200;            /*  wait a maximum of 200 msec   */
    const UData_t nPollRate = 2;             /*  poll status bits every 2 ms */
    const UData_t nMaxLoops = nMaxWait / nPollRate;
    UData_t status = MT_OK;                  /* Status to be returned */
    UData_t nDelays = 0;
    U8Data statreg;
    MT2266_Info_t* pInfo = (MT2266_Info_t*) h;

    if (IsValidHandle(pInfo) == 0)
        return MT_INV_HANDLE;

    do
    {
        status |= UncheckedGet(pInfo, MT2266_STATUS_1, &statreg);

        if ((MT_IS_ERROR(status)) || ((statreg & 0x40) == 0x40))
            return (status);

        MT2266_Sleep(pInfo->hUserData, nPollRate);       /*  Wait between retries  */
    }
    while (++nDelays < nMaxLoops);

    if ((statreg & 0x40) != 0x40)
        status |= MT_DNC_UNLOCK;

    return (status);
}


/****************************************************************************
**
**  Name: MT2266_GetParam
**
**  Description:    Gets a tuning algorithm parameter.
**
**                  This function provides access to the internals of the
**                  tuning algorithm - mostly for testing purposes.
**
**  Parameters:     h           - Tuner handle (returned by MT2266_Open)
**                  param       - Tuning algorithm parameter
**                                (see enum MT2266_Param)
**                  pValue      - ptr to returned value
**
**                  param                   Description
**                  ----------------------  --------------------------------
**                  MT2266_IC_ADDR          Serial Bus address of this tuner
**                  MT2266_MAX_OPEN         Max number of MT2266's that can be open
**                  MT2266_NUM_OPEN         Number of MT2266's currently open
**                  MT2266_NUM_REGS         Number of tuner registers
**                  MT2266_SRO_FREQ         crystal frequency
**                  MT2266_STEPSIZE         minimum tuning step size
**                  MT2266_INPUT_FREQ       input center frequency
**                  MT2266_LO_FREQ          LO Frequency
**                  MT2266_OUTPUT_BW        Output channel bandwidth
**                  MT2266_RC2_VALUE        Base band filter cal RC code (method 2)
**                  MT2266_RC2_NOMINAL      Base band filter nominal cal RC code
**                  MT2266_RF_ADC           RF attenuator A/D readback
**                  MT2266_RF_ATTN          RF attenuation (0-255)
**                  MT2266_RF_EXT           External control of RF atten
**                  MT2266_LNA_GAIN         LNA gain setting (0-15)
**                  MT2266_BB_ADC           BB attenuator A/D readback
**                  MT2266_BB_ATTN          Baseband attenuation (0-255)
**                  MT2266_BB_EXT           External control of BB atten
**
**  Usage:          status |= MT2266_GetParam(hMT2266,
**                                            MT2266_OUTPUT_BW,
**                                            &f_bw);
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_INV_HANDLE    - Invalid tuner handle
**                      MT_ARG_NULL      - Null pointer argument passed
**                      MT_ARG_RANGE     - Invalid parameter requested
**
**  Dependencies:   USERS MUST CALL MT2266_Open() FIRST!
**
**  See Also:       MT2266_SetParam, MT2266_Open
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   05-30-2006    DAD    Ver 1.0: Modified version of mt2260.c (Ver 1.01).
**
****************************************************************************/
UData_t MT2266_GetParam(Handle_t     h,
                        MT2266_Param param,
                        UData_t*     pValue)
{
    UData_t status = MT_OK;                  /* Status to be returned        */
    U8Data tmp;
    MT2266_Info_t* pInfo = (MT2266_Info_t*) h;

    if (pValue == MT_NULL)
        status |= MT_ARG_NULL;

    /*  Verify that the handle passed points to a valid tuner         */
    if (IsValidHandle(pInfo) == 0)
        status |= MT_INV_HANDLE;

    if (MT_NO_ERROR(status))
    {
        switch (param)
        {
        /*  Serial Bus address of this tuner      */
        case MT2266_IC_ADDR:
            *pValue = pInfo->address;
            break;

        /*  Max # of MT2266's allowed to be open  */
        case MT2266_MAX_OPEN:
            *pValue = nMaxTuners;
            break;

        /*  # of MT2266's open                    */
        case MT2266_NUM_OPEN:
            *pValue = nOpenTuners;
            break;

        /*  Number of tuner registers             */
        case MT2266_NUM_REGS:
            *pValue = Num_Registers;
            break;

        /*  crystal frequency                     */
        case MT2266_SRO_FREQ:
            *pValue = pInfo->f_Ref;
            break;

        /*  minimum tuning step size              */
        case MT2266_STEPSIZE:
            *pValue = pInfo->f_Step;
            break;

        /*  input center frequency                */
        case MT2266_INPUT_FREQ:
            *pValue = pInfo->f_in;
            break;

        /*  LO Frequency                          */
        case MT2266_LO_FREQ:
            *pValue = pInfo->f_LO;
            break;

        /*  Output Channel Bandwidth              */
        case MT2266_OUTPUT_BW:
            *pValue = pInfo->f_bw;
            break;

        /*  Base band filter cal RC code          */
        case MT2266_RC2_VALUE:
            *pValue = (UData_t) pInfo->RC2_Value;
            break;

        /*  Base band filter nominal cal RC code          */
        case MT2266_RC2_NOMINAL:
            *pValue = (UData_t) pInfo->RC2_Nominal;
            break;

        /*  RF attenuator A/D readback            */
        case MT2266_RF_ADC:
            status |= UncheckedGet(pInfo, MT2266_STATUS_2, &tmp);
            if (MT_NO_ERROR(status))
                *pValue = (UData_t) tmp;
            break;

        /*  BB attenuator A/D readback            */
        case MT2266_BB_ADC:
            status |= UncheckedGet(pInfo, MT2266_STATUS_3, &tmp);
            if (MT_NO_ERROR(status))
                *pValue = (UData_t) tmp;
            break;

        /*  RF attenuator setting                 */
        case MT2266_RF_ATTN:
            PREFETCH(MT2266_RSVD_1F, 1);  /* Fetch register(s) if __NO_CACHE__ defined */
            if (MT_NO_ERROR(status))
                *pValue = pInfo->reg[MT2266_RSVD_1F];
            break;

        /*  BB attenuator setting                 */
        case MT2266_BB_ATTN:
            PREFETCH(MT2266_RSVD_2C, 3);  /* Fetch register(s) if __NO_CACHE__ defined */
            *pValue = pInfo->reg[MT2266_RSVD_2C]
                    + pInfo->reg[MT2266_RSVD_2D]
                    + pInfo->reg[MT2266_RSVD_2E] - 3;
            break;

        /*  RF external / internal atten control  */
        case MT2266_RF_EXT:
            PREFETCH(MT2266_GPO, 1);  /* Fetch register(s) if __NO_CACHE__ defined */
            *pValue = ((pInfo->reg[MT2266_GPO] & 0x40) != 0x00);
            break;

        /*  BB external / internal atten control  */
        case MT2266_BB_EXT:
            PREFETCH(MT2266_RSVD_33, 1);  /* Fetch register(s) if __NO_CACHE__ defined */
            *pValue = ((pInfo->reg[MT2266_RSVD_33] & 0x10) != 0x00);
            break;

        /*  LNA gain setting (0-15)               */
        case MT2266_LNA_GAIN:
            PREFETCH(MT2266_IGAIN, 1);  /* Fetch register(s) if __NO_CACHE__ defined */
            *pValue = ((pInfo->reg[MT2266_IGAIN] & 0x3C) >> 2);
            break;

        case MT2266_EOP:
        default:
            status |= MT_ARG_RANGE;
        }
    }
    return (status);
}


/****************************************************************************
**  LOCAL FUNCTION - DO NOT USE OUTSIDE OF mt2266.c
**
**  Name: UncheckedGet
**
**  Description:    Gets an MT2266 register with minimal checking
**
**                  NOTE: This is a local function that performs the same
**                  steps as the MT2266_GetReg function that is available
**                  in the external API.  It does not do any of the standard
**                  error checking that the API function provides and should
**                  not be called from outside this file.
**
**  Parameters:     *pInfo      - Tuner control structure
**                  reg         - MT2266 register/subaddress location
**                  *val        - MT2266 register/subaddress value
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_COMM_ERR      - Serial bus communications error
**                      MT_INV_HANDLE    - Invalid tuner handle
**                      MT_ARG_NULL      - Null pointer argument passed
**                      MT_ARG_RANGE     - Argument out of range
**
**  Dependencies:   USERS MUST CALL MT2266_Open() FIRST!
**
**                  Use this function if you need to read a register from
**                  the MT2266.
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   05-30-2006    DAD    Ver 1.0: Modified version of mt2260.c (Ver 1.01).
**
****************************************************************************/
static UData_t UncheckedGet(MT2266_Info_t* pInfo,
                            U8Data   reg,
                            U8Data*  val)
{
    UData_t status;                  /* Status to be returned        */

#if defined(_DEBUG)
    status = MT_OK;
    /*  Verify that the handle passed points to a valid tuner         */
    if (IsValidHandle(pInfo) == 0)
        status |= MT_INV_HANDLE;

    if (val == MT_NULL)
        status |= MT_ARG_NULL;

    if (reg >= END_REGS)
        status |= MT_ARG_RANGE;

    if (MT_IS_ERROR(status))
        return(status);
#endif

    status = MT2266_ReadSub(pInfo->hUserData, pInfo->address, reg, &pInfo->reg[reg], 1);

    if (MT_NO_ERROR(status))
        *val = pInfo->reg[reg];

    return (status);
}


/****************************************************************************
**
**  Name: MT2266_GetReg
**
**  Description:    Gets an MT2266 register.
**
**  Parameters:     h           - Tuner handle (returned by MT2266_Open)
**                  reg         - MT2266 register/subaddress location
**                  *val        - MT2266 register/subaddress value
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_COMM_ERR      - Serial bus communications error
**                      MT_INV_HANDLE    - Invalid tuner handle
**                      MT_ARG_NULL      - Null pointer argument passed
**                      MT_ARG_RANGE     - Argument out of range
**
**  Dependencies:   USERS MUST CALL MT2266_Open() FIRST!
**
**                  Use this function if you need to read a register from
**                  the MT2266.
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   05-30-2006    DAD    Ver 1.0: Modified version of mt2260.c (Ver 1.01).
**
****************************************************************************/
UData_t MT2266_GetReg(Handle_t h,
                      U8Data   reg,
                      U8Data*  val)
{
    UData_t status = MT_OK;                  /* Status to be returned        */
    MT2266_Info_t* pInfo = (MT2266_Info_t*) h;

    /*  Verify that the handle passed points to a valid tuner         */
    if (IsValidHandle(pInfo) == 0)
        status |= MT_INV_HANDLE;

    if (val == MT_NULL)
        status |= MT_ARG_NULL;

    if (reg >= END_REGS)
        status |= MT_ARG_RANGE;

    if (MT_NO_ERROR(status))
        status |= UncheckedGet(pInfo, reg, val);

    return (status);
}


/****************************************************************************
**
**  Name: MT2266_GetUHFXFreqs
**
**  Description:    Retrieves the specified set of UHF Crossover Frequencies
**
**  Parameters:     h            - Open handle to the tuner (from MT2266_Open).
**
**  Usage:          MT2266_Freq_Set  tmpFreqs;
**                  status = MT2266_GetUHFXFreqs(hMT2266,
**                                               MT2266_UHF1_WITH_ATTENUATION,
**                                               tmpFreqs );
**                  if (status & MT_ARG_RANGE)
**                      // error, Invalid UHF Crossover Frequency Set requested.
**                  else
**                      for( int i = 0;  i < MT2266_NUM_XFREQS; i++ )
**                         . . .
**
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_ARG_RANGE     - freq_type is out of range.
**                      MT_INV_HANDLE    - Invalid tuner handle
**
**  Dependencies:   freqs_buffer *must* be defined of type MT2266_Freq_Set
**                     to assure sufficient space allocation!
**
**                  USERS MUST CALL MT2266_Open() FIRST!
**
**  See Also:       MT2266_SetUHFXFreqs
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   10-26-2006   RSK     Original.
**
****************************************************************************/
UData_t MT2266_GetUHFXFreqs(Handle_t h,
                            MT2266_UHFXFreq_Type freq_type,
                            MT2266_XFreq_Set     freqs_buffer)
{
    UData_t status = MT_OK;                  /* Status to be returned        */
    MT2266_Info_t* pInfo = (MT2266_Info_t*) h;

    /*  Verify that the handle passed points to a valid tuner         */
    if (IsValidHandle(pInfo) == 0)
        status = MT_INV_HANDLE;

    if (freq_type >= MT2266_NUMBER_OF_XFREQ_SETS)
        status |= MT_ARG_RANGE;

    if (MT_NO_ERROR(status))
    {
        int  i;

        for( i = 0; i < MT2266_NUM_XFREQS; i++ )
        {
            freqs_buffer[i] = pInfo->xfreqs.xfreq[ freq_type ][i] * TUNE_STEP_SIZE / 1000;
        }
    }

    return (status);
}


/****************************************************************************
**
**  Name: MT2266_GetUserData
**
**  Description:    Gets the user-defined data item.
**
**  Parameters:     h           - Tuner handle (returned by MT2266_Open)
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_INV_HANDLE    - Invalid tuner handle
**                      MT_ARG_NULL      - Null pointer argument passed
**
**  Dependencies:   USERS MUST CALL MT2266_Open() FIRST!
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
**  See Also:       MT2266_SetUserData, MT2266_Open
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   05-30-2006    DAD    Ver 1.0: Modified version of mt2260.c (Ver 1.01).
**
****************************************************************************/
UData_t MT2266_GetUserData(Handle_t h,
                           Handle_t* hUserData)
{
    UData_t status = MT_OK;                  /* Status to be returned        */
    MT2266_Info_t* pInfo = (MT2266_Info_t*) h;

    /*  Verify that the handle passed points to a valid tuner         */
    if (IsValidHandle(pInfo) == 0)
        status = MT_INV_HANDLE;

    if (hUserData == MT_NULL)
        status |= MT_ARG_NULL;

    if (MT_NO_ERROR(status))
        *hUserData = pInfo->hUserData;

    return (status);
}


/******************************************************************************
**
**  Name: MT2266_ReInit
**
**  Description:    Initialize the tuner's register values.
**
**  Parameters:     h           - Tuner handle (returned by MT2266_Open)
**
**  Returns:        status:
**                      MT_OK             - No errors
**                      MT_TUNER_ID_ERR   - Tuner Part/Rev code mismatch
**                      MT_TUNER_INIT_ERR - Tuner initialization failed
**                      MT_INV_HANDLE     - Invalid tuner handle
**                      MT_COMM_ERR       - Serial bus communications error
**
**  Dependencies:   MT_ReadSub  - Read byte(s) of data from the two-wire bus
**                  MT_WriteSub - Write byte(s) of data to the two-wire bus
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   05-30-2006    DAD    Ver 1.0: Modified version of mt2260.c (Ver 1.01).
**   N/A   06-08-2006    JWS    Ver 1.01: Corrected problem with tuner ID check
**   N/A   11-01-2006    RSK    Ver 1.02: Initialize XFreq Tables to Default
**   N/A   11-29-2006    DAD    Ver 1.03: Parenthesis clarification
**
******************************************************************************/
UData_t MT2266_ReInit(Handle_t h)
{
    int j;

    U8Data MT2266_Init_Defaults1[] =
    {
        0x01,            /* Start w/register 0x01 */
        0x00,            /* Reg 0x01 */
        0x00,            /* Reg 0x02 */
        0x28,            /* Reg 0x03 */
        0x00,            /* Reg 0x04 */
        0x52,            /* Reg 0x05 */
        0x99,            /* Reg 0x06 */
        0x3F,            /* Reg 0x07 */
    };

    U8Data MT2266_Init_Defaults2[] =
    {
        0x17,            /* Start w/register 0x17 */
        0x6D,            /* Reg 0x17 */
        0x71,            /* Reg 0x18 */
        0x61,            /* Reg 0x19 */
        0xC0,            /* Reg 0x1A */
        0xBF,            /* Reg 0x1B */
        0xFF,            /* Reg 0x1C */
        0xDC,            /* Reg 0x1D */
        0x00,            /* Reg 0x1E */
        0x0A,            /* Reg 0x1F */
        0xD4,            /* Reg 0x20 */
        0x03,            /* Reg 0x21 */
        0x64,            /* Reg 0x22 */
        0x64,            /* Reg 0x23 */
        0x64,            /* Reg 0x24 */
        0x64,            /* Reg 0x25 */
        0x22,            /* Reg 0x26 */
        0xAA,            /* Reg 0x27 */
        0xF2,            /* Reg 0x28 */
        0x1E,            /* Reg 0x29 */
        0x80,            /* Reg 0x2A */
        0x14,            /* Reg 0x2B */
        0x01,            /* Reg 0x2C */
        0x01,            /* Reg 0x2D */
        0x01,            /* Reg 0x2E */
        0x01,            /* Reg 0x2F */
        0x01,            /* Reg 0x30 */
        0x01,            /* Reg 0x31 */
        0x7F,            /* Reg 0x32 */
        0x5E,            /* Reg 0x33 */
        0x3F,            /* Reg 0x34 */
        0xFF,            /* Reg 0x35 */
        0xFF,            /* Reg 0x36 */
        0xFF,            /* Reg 0x37 */
        0x00,            /* Reg 0x38 */
        0x77,            /* Reg 0x39 */
        0x0F,            /* Reg 0x3A */
        0x2D,            /* Reg 0x3B */
    };

    UData_t status = MT_OK;                  /* Status to be returned        */
    MT2266_Info_t* pInfo = (MT2266_Info_t*) h;
    U8Data BBVref;
    U8Data tmpreg = 0;
    U8Data statusreg = 0;

    /*  Verify that the handle passed points to a valid tuner         */
    if (IsValidHandle(pInfo) == 0)
        status |= MT_INV_HANDLE;

    /*  Read the Part/Rev code from the tuner */
    if (MT_NO_ERROR(status))
        status |= UncheckedGet(pInfo, MT2266_PART_REV, &tmpreg);
    if (MT_NO_ERROR(status) && (tmpreg != 0x85))  // MT226? B0
        status |= MT_TUNER_ID_ERR;
    else
    {
        /*
        **  Read the status register 5
        */
        tmpreg = pInfo->reg[MT2266_RSVD_11] |= 0x03;
        if (MT_NO_ERROR(status))
            status |= UncheckedSet(pInfo, MT2266_RSVD_11, tmpreg);
        tmpreg &= ~(0x02);
        if (MT_NO_ERROR(status))
            status |= UncheckedSet(pInfo, MT2266_RSVD_11, tmpreg);

        /*  Get and store the status 5 register value  */
        if (MT_NO_ERROR(status))
            status |= UncheckedGet(pInfo, MT2266_STATUS_5, &statusreg);

        /*  MT2266  */
        if (MT_IS_ERROR(status) || ((statusreg & 0x30) != 0x30))
                status |= MT_TUNER_ID_ERR;      /*  Wrong tuner Part/Rev code   */
    }

    if (MT_NO_ERROR(status))
    {
        /*  Initialize the tuner state.  Hold off on f_in and f_LO */
        pInfo->version = VERSION;
        pInfo->tuner_id = pInfo->reg[MT2266_PART_REV];
        pInfo->f_Ref = REF_FREQ;
        pInfo->f_Step = TUNE_STEP_SIZE * 1000;  /* kHz -> Hz */
        pInfo->f_in = UHF_DEFAULT_FREQ;
        pInfo->f_LO = UHF_DEFAULT_FREQ;
        pInfo->f_bw = OUTPUT_BW;
        pInfo->band = MT2266_UHF_BAND;
        pInfo->num_regs = END_REGS;

        /* Reset the UHF Crossover Frequency tables on open/init. */
        for (j=0; j< MT2266_NUM_XFREQS; j++ )
        {
            pInfo->xfreqs.xfreq[MT2266_UHF0][j]       = MT2266_default_XFreqs.xfreq[MT2266_UHF0][j];
            pInfo->xfreqs.xfreq[MT2266_UHF1][j]       = MT2266_default_XFreqs.xfreq[MT2266_UHF1][j];
            pInfo->xfreqs.xfreq[MT2266_UHF0_ATTEN][j] = MT2266_default_XFreqs.xfreq[MT2266_UHF0_ATTEN][j];
            pInfo->xfreqs.xfreq[MT2266_UHF1_ATTEN][j] = MT2266_default_XFreqs.xfreq[MT2266_UHF1_ATTEN][j];
        }

        /*  Write the default values to the tuner registers. Default mode is UHF */
        status |= MT2266_WriteSub(pInfo->hUserData,
                              pInfo->address,
                              MT2266_Init_Defaults1[0],
                              &MT2266_Init_Defaults1[1],
                              sizeof(MT2266_Init_Defaults1)/sizeof(U8Data)-1);
        status |= MT2266_WriteSub(pInfo->hUserData,
                              pInfo->address,
                              MT2266_Init_Defaults2[0],
                              &MT2266_Init_Defaults2[1],
                              sizeof(MT2266_Init_Defaults2)/sizeof(U8Data)-1);
    }

    /*  Read back all the registers from the tuner */
    if (MT_NO_ERROR(status))
    {
        status |= MT2266_ReadSub(pInfo->hUserData, pInfo->address, 0, &pInfo->reg[0], END_REGS);
    }

    /*
    **  Set reg[0x33] based on statusreg
    */
    if (MT_NO_ERROR(status))
    {
        BBVref = (((statusreg >> 6) + 2) & 0x03);
        tmpreg = (pInfo->reg[MT2266_RSVD_33] & ~(0x60)) | (BBVref << 5);
        status |= UncheckedSet(pInfo, MT2266_RSVD_33, tmpreg);
    }

    /*  Run the baseband filter calibration  */
    if (MT_NO_ERROR(status))
        status |= Run_BB_RC_Cal2(h);

    /*  Set the baseband filter bandwidth to the default  */
    if (MT_NO_ERROR(status))
        status |= Set_BBFilt(h);

    return (status);
}


/****************************************************************************
**
**  Name: MT2266_SetParam
**
**  Description:    Sets a tuning algorithm parameter.
**
**                  This function provides access to the internals of the
**                  tuning algorithm.  You can override many of the tuning
**                  algorithm defaults using this function.
**
**  Parameters:     h           - Tuner handle (returned by MT2266_Open)
**                  param       - Tuning algorithm parameter
**                                (see enum MT2266_Param)
**                  nValue      - value to be set
**
**                  param                   Description
**                  ----------------------  --------------------------------
**                  MT2266_SRO_FREQ         crystal frequency
**                  MT2266_STEPSIZE         minimum tuning step size
**                  MT2266_INPUT_FREQ       Center of input channel
**                  MT2266_OUTPUT_BW        Output channel bandwidth
**                  MT2266_RF_ATTN          RF attenuation (0-255)
**                  MT2266_RF_EXT           External control of RF atten
**                  MT2266_LNA_GAIN         LNA gain setting (0-15)
**                  MT2266_LNA_GAIN_DECR    Decrement LNA Gain (arg=min)
**                  MT2266_LNA_GAIN_INCR    Increment LNA Gain (arg=max)
**                  MT2266_BB_ATTN          Baseband attenuation (0-255)
**                  MT2266_BB_EXT           External control of BB atten
**                  MT2266_UHF_MAXSENS      Set for UHF max sensitivity mode
**                  MT2266_UHF_NORMAL       Set for UHF normal mode
**
**  Usage:          status |= MT2266_SetParam(hMT2266,
**                                            MT2266_STEPSIZE,
**                                            50000);
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_INV_HANDLE    - Invalid tuner handle
**                      MT_ARG_RANGE     - Invalid parameter requested
**                                         or set value out of range
**                                         or non-writable parameter
**
**  Dependencies:   USERS MUST CALL MT2266_Open() FIRST!
**
**  See Also:       MT2266_GetParam, MT2266_Open
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   05-30-2006    DAD    Ver 1.0: Modified version of mt2260.c (Ver 1.01).
**   N/A   11-29-2006    DAD    Ver 1.03: Parenthesis clarification for gcc
**
****************************************************************************/
UData_t MT2266_SetParam(Handle_t     h,
                        MT2266_Param param,
                        UData_t      nValue)
{
    UData_t status = MT_OK;                  /* Status to be returned        */
    U8Data tmpreg;
    MT2266_Info_t* pInfo = (MT2266_Info_t*) h;

    /*  Verify that the handle passed points to a valid tuner         */
    if (IsValidHandle(pInfo) == 0)
        status |= MT_INV_HANDLE;

    if (MT_NO_ERROR(status))
    {
        switch (param)
        {
        /*  crystal frequency                     */
        case MT2266_SRO_FREQ:
            pInfo->f_Ref = nValue;
            if (pInfo->f_Ref < 22000000)
            {
                /*  Turn off f_SRO divide by 2  */
                status |= UncheckedSet(pInfo,
                                       MT2266_SRO_CTRL,
                                       (U8Data) (pInfo->reg[MT2266_SRO_CTRL] &= 0xFE));
            }
            else
            {
                /*  Turn on f_SRO divide by 2  */
                status |= UncheckedSet(pInfo,
                                       MT2266_SRO_CTRL,
                                       (U8Data) (pInfo->reg[MT2266_SRO_CTRL] |= 0x01));
            }
            status |= Run_BB_RC_Cal2(h);
            if (MT_NO_ERROR(status))
                status |= Set_BBFilt(h);
            break;

        /*  minimum tuning step size              */
        case MT2266_STEPSIZE:
            pInfo->f_Step = nValue;
            break;

        /*  Width of output channel               */
        case MT2266_OUTPUT_BW:
            pInfo->f_bw = nValue;
            status |= Set_BBFilt(h);
            break;

        /*  BB attenuation (0-255)                */
        case MT2266_BB_ATTN:
            if (nValue > 255)
                status |= MT_ARG_RANGE;
            else
            {
                UData_t BBA_Stage1;
                UData_t BBA_Stage2;
                UData_t BBA_Stage3;

                BBA_Stage3 = (nValue > 102) ? 103 : nValue + 1;
                BBA_Stage2 = (nValue > 175) ? 75 : nValue + 2 - BBA_Stage3;
                BBA_Stage1 = (nValue > 176) ? nValue - 175 : 1;
                pInfo->reg[MT2266_RSVD_2C] = (U8Data) BBA_Stage1;
                pInfo->reg[MT2266_RSVD_2D] = (U8Data) BBA_Stage2;
                pInfo->reg[MT2266_RSVD_2E] = (U8Data) BBA_Stage3;
                pInfo->reg[MT2266_RSVD_2F] = (U8Data) BBA_Stage1;
                pInfo->reg[MT2266_RSVD_30] = (U8Data) BBA_Stage2;
                pInfo->reg[MT2266_RSVD_31] = (U8Data) BBA_Stage3;
                status |= MT2266_WriteSub(pInfo->hUserData,
                                      pInfo->address,
                                      MT2266_RSVD_2C,
                                      &pInfo->reg[MT2266_RSVD_2C],
                                      6);
            }
            break;

        /*  RF attenuation (0-255)                */
        case MT2266_RF_ATTN:
            if (nValue > 255)
                status |= MT_ARG_RANGE;
            else
                status |= UncheckedSet(pInfo, MT2266_RSVD_1F, (U8Data) nValue);
            break;

        /*  RF external / internal atten control  */
        case MT2266_RF_EXT:
            if (nValue == 0)
                tmpreg = pInfo->reg[MT2266_GPO] &= ~0x40;
            else
                tmpreg = pInfo->reg[MT2266_GPO] |= 0x40;
            status |= UncheckedSet(pInfo, MT2266_GPO, tmpreg);
            break;

        /*  LNA gain setting (0-15)               */
        case MT2266_LNA_GAIN:
            if (nValue > 15)
                status |= MT_ARG_RANGE;
            else
            {
                tmpreg = (pInfo->reg[MT2266_IGAIN] & 0xC3) | ((U8Data)nValue << 2);
                status |= UncheckedSet(pInfo, MT2266_IGAIN, tmpreg);
            }
            break;

        /*  Decrement LNA Gain setting, argument is min LNA Gain setting  */
        case MT2266_LNA_GAIN_DECR:
            if (nValue > 15)
                status |= MT_ARG_RANGE;
            else
            {
                PREFETCH(MT2266_IGAIN, 1);
                if (MT_NO_ERROR(status) && ((U8Data) ((pInfo->reg[MT2266_IGAIN] & 0x3C) >> 2) > (U8Data) nValue))
                    status |= UncheckedSet(pInfo, MT2266_IGAIN, pInfo->reg[MT2266_IGAIN] - 0x04);
            }
            break;

        /*  Increment LNA Gain setting, argument is max LNA Gain setting  */
        case MT2266_LNA_GAIN_INCR:
            if (nValue > 15)
                status |= MT_ARG_RANGE;
            else
            {
                PREFETCH(MT2266_IGAIN, 1);
                if (MT_NO_ERROR(status) && ((U8Data) ((pInfo->reg[MT2266_IGAIN] & 0x3C) >> 2) < (U8Data) nValue))
                    status |= UncheckedSet(pInfo, MT2266_IGAIN, pInfo->reg[MT2266_IGAIN] + 0x04);
            }
            break;

        /*  BB external / internal atten control  */
        case MT2266_BB_EXT:
            if (nValue == 0)
                tmpreg = pInfo->reg[MT2266_RSVD_33] &= ~0x08;
            else
                tmpreg = pInfo->reg[MT2266_RSVD_33] |= 0x08;
            status |= UncheckedSet(pInfo, MT2266_RSVD_33, tmpreg);
            break;

        /*  Set for UHF max sensitivity mode  */
        case MT2266_UHF_MAXSENS:
            PREFETCH(MT2266_BAND_CTRL, 1);
            if (MT_NO_ERROR(status) && ((pInfo->reg[MT2266_BAND_CTRL] & 0x30) == 0x10))
                status |= UncheckedSet(pInfo, MT2266_BAND_CTRL, pInfo->reg[MT2266_BAND_CTRL] ^ 0x30);
            break;

        /*  Set for UHF normal mode  */
        case MT2266_UHF_NORMAL:
            if (MT_NO_ERROR(status) && ((pInfo->reg[MT2266_BAND_CTRL] & 0x30) == 0x20))
                status |= UncheckedSet(pInfo, MT2266_BAND_CTRL, pInfo->reg[MT2266_BAND_CTRL] ^ 0x30);
            break;

        /*  These parameters are read-only  */
        case MT2266_IC_ADDR:
        case MT2266_MAX_OPEN:
        case MT2266_NUM_OPEN:
        case MT2266_NUM_REGS:
        case MT2266_INPUT_FREQ:
        case MT2266_LO_FREQ:
        case MT2266_RC2_VALUE:
        case MT2266_RF_ADC:
        case MT2266_BB_ADC:
        case MT2266_EOP:
        default:
            status |= MT_ARG_RANGE;
        }
    }
    return (status);
}


/****************************************************************************
**
**  Name: MT2266_SetPowerModes
**
**  Description:    Sets the bits in the MT2266_ENABLES register and the
**                  SROsd bit in the MT2266_SROADC_CTRL register.
**
**  Parameters:     h           - Tuner handle (returned by MT2266_Open)
**                  flags       - Bit mask of flags to indicate enabled
**                                bits.
**
**  Usage:          status = MT2266_SetPowerModes(hMT2266, flags);
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_INV_HANDLE    - Invalid tuner handle
**
**  Dependencies:   USERS MUST CALL MT2266_Open() FIRST!
**
**                  The bits in the MT2266_ENABLES register and the
**                  SROsd bit are set according to the supplied flags.
**
**                  The pre-defined flags are as follows:
**                      MT2266_SROen
**                      MT2266_LOen
**                      MT2266_ADCen
**                      MT2266_PDen
**                      MT2266_DCOCen
**                      MT2266_BBen
**                      MT2266_MIXen
**                      MT2266_LNAen
**                      MT2266_ALL_ENABLES
**                      MT2266_NO_ENABLES
**                      MT2266_SROsd
**                      MT2266_SRO_NOT_sd
**
**                  ONLY the enable bits (or SROsd bit) specified in the
**                  flags parameter will be set.  Any flag which is not
**                  included, will cause that bit to be disabled.
**
**                  The ALL_ENABLES, NO_ENABLES, and SRO_NOT_sd constants
**                  are for convenience.  The NO_ENABLES and SRO_NOT_sd
**                  do not actually have to be included, but are provided
**                  for clarity.
**
**  See Also:       MT2266_Open
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   05-30-2006    DAD    Ver 1.0: Modified version of mt2260.c (Ver 1.01).
**
****************************************************************************/
UData_t MT2266_SetPowerModes(Handle_t h,
                             UData_t  flags)
{
    UData_t status = MT_OK;                  /* Status to be returned */
    MT2266_Info_t* pInfo = (MT2266_Info_t*) h;
    U8Data tmpreg;

    /*  Verify that the handle passed points to a valid tuner */
    if (IsValidHandle(pInfo) == 0)
        status |= MT_INV_HANDLE;

    PREFETCH(MT2266_SRO_CTRL, 1);  /* Fetch register(s) if __NO_CACHE__ defined */
    if (MT_NO_ERROR(status))
    {
        if (flags & MT2266_SROsd)
            tmpreg = pInfo->reg[MT2266_SRO_CTRL] |= 0x10;  /* set the SROsd bit */
        else
            tmpreg = pInfo->reg[MT2266_SRO_CTRL] &= 0xEF;  /* clear the SROsd bit */
        status |= UncheckedSet(pInfo, MT2266_SRO_CTRL, tmpreg);
    }

    PREFETCH(MT2266_ENABLES, 1);  /* Fetch register(s) if __NO_CACHE__ defined */

    if (MT_NO_ERROR(status))
    {
        status |= UncheckedSet(pInfo, MT2266_ENABLES, (U8Data)(flags & 0xff));
    }

    return status;
}


/****************************************************************************
**  LOCAL FUNCTION - DO NOT USE OUTSIDE OF mt2266.c
**
**  Name: UncheckedSet
**
**  Description:    Sets an MT2266 register.
**
**                  NOTE: This is a local function that performs the same
**                  steps as the MT2266_SetReg function that is available
**                  in the external API.  It does not do any of the standard
**                  error checking that the API function provides and should
**                  not be called from outside this file.
**
**  Parameters:     *pInfo      - Tuner control structure
**                  reg         - MT2266 register/subaddress location
**                  val         - MT2266 register/subaddress value
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_COMM_ERR      - Serial bus communications error
**                      MT_INV_HANDLE    - Invalid tuner handle
**                      MT_ARG_RANGE     - Argument out of range
**
**  Dependencies:   USERS MUST CALL MT2266_Open() FIRST!
**
**                  Sets a register value without any preliminary checking for
**                  valid handles or register numbers.
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   05-30-2006    DAD    Ver 1.0: Modified version of mt2260.c (Ver 1.01).
**
****************************************************************************/
static UData_t UncheckedSet(MT2266_Info_t* pInfo,
                            U8Data         reg,
                            U8Data         val)
{
    UData_t status;                  /* Status to be returned */

#if defined(_DEBUG)
    status = MT_OK;
    /*  Verify that the handle passed points to a valid tuner         */
    if (IsValidHandle(pInfo) == 0)
        status |= MT_INV_HANDLE;

    if (reg >= END_REGS)
        status |= MT_ARG_RANGE;

    if (MT_IS_ERROR(status))
        return (status);
#endif

    status = MT2266_WriteSub(pInfo->hUserData, pInfo->address, reg, &val, 1);

    if (MT_NO_ERROR(status))
        pInfo->reg[reg] = val;

    return (status);
}


/****************************************************************************
**
**  Name: MT2266_SetReg
**
**  Description:    Sets an MT2266 register.
**
**  Parameters:     h           - Tuner handle (returned by MT2266_Open)
**                  reg         - MT2266 register/subaddress location
**                  val         - MT2266 register/subaddress value
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_COMM_ERR      - Serial bus communications error
**                      MT_INV_HANDLE    - Invalid tuner handle
**                      MT_ARG_RANGE     - Argument out of range
**
**  Dependencies:   USERS MUST CALL MT2266_Open() FIRST!
**
**                  Use this function if you need to override a default
**                  register value
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   05-30-2006    DAD    Ver 1.0: Modified version of mt2260.c (Ver 1.01).
**
****************************************************************************/
UData_t MT2266_SetReg(Handle_t h,
                      U8Data   reg,
                      U8Data   val)
{
    UData_t status = MT_OK;                  /* Status to be returned */
    MT2266_Info_t* pInfo = (MT2266_Info_t*) h;

    /*  Verify that the handle passed points to a valid tuner         */
    if (IsValidHandle(pInfo) == 0)
        status |= MT_INV_HANDLE;

    if (reg >= END_REGS)
        status |= MT_ARG_RANGE;

    if (MT_NO_ERROR(status))
        status |= UncheckedSet(pInfo, reg, val);

    return (status);
}


/****************************************************************************
**
**  Name: MT2266_SetUHFXFreqs
**
**  Description:    Assigns the specified set of UHF Crossover Frequencies
**
**  Parameters:     h            - Open handle to the tuner (from MT2266_Open).
**
**  Usage:          MT2266_Freq_Set  tmpFreqs;
**                  status = MT2266_GetUHFXFreqs(hMT2266,
**                                               MT2266_UHF1_WITH_ATTENUATION,
**                                               tmpFreqs );
**                   ...
**                  tmpFreqs[i] = <desired value>
**                   ...
**                  status = MT2266_SetUHFXFreqs(hMT2266,
**                                               MT2266_UHF1_WITH_ATTENUATION,
**                                               tmpFreqs );
**
**                  if (status & MT_ARG_RANGE)
**                      // error, Invalid UHF Crossover Frequency Set requested.
**                  else
**                      for( int i = 0;  i < MT2266_NUM_XFREQS; i++ )
**                         . . .
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_ARG_RANGE     - freq_type is out of range.
**                      MT_INV_HANDLE    - Invalid tuner handle
**
**  Dependencies:   freqs_buffer *must* be defined of type MT2266_Freq_Set
**                     to assure sufficient space allocation!
**
**                  USERS MUST CALL MT2266_Open() FIRST!
**
**  See Also:       MT2266_SetUHFXFreqs
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   10-26-2006   RSK     Original.
**
****************************************************************************/
UData_t MT2266_SetUHFXFreqs(Handle_t h,
                            MT2266_UHFXFreq_Type freq_type,
                            MT2266_XFreq_Set     freqs_buffer)
{
    UData_t status = MT_OK;                     /* Status to be returned */
    MT2266_Info_t* pInfo = (MT2266_Info_t*) h;

    /*  Verify that the handle passed points to a valid tuner         */
    if (IsValidHandle(pInfo) == 0)
        status = MT_INV_HANDLE;

    if (freq_type >= MT2266_NUMBER_OF_XFREQ_SETS)
        status |= MT_ARG_RANGE;

    if (MT_NO_ERROR(status))
    {
        int  i;

        for( i = 0; i < MT2266_NUM_XFREQS; i++ )
        {
            pInfo->xfreqs.xfreq[ freq_type ][i] = freqs_buffer[i] * 1000 / TUNE_STEP_SIZE;
        }
    }

    return (status);
}


/****************************************************************************
** LOCAL FUNCTION
**
**  Name: RoundToStep
**
**  Description:    Rounds the given frequency to the closes f_Step value
**                  given the tuner ref frequency..
**
**
**  Parameters:     freq      - Frequency to be rounded (in Hz).
**                  f_Step    - Step size for the frequency (in Hz).
**                  f_Ref     - SRO frequency (in Hz).
**
**  Returns:        Rounded frequency.
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   05-30-2006    DAD    Ver 1.0: Modified version of mt2260.c (Ver 1.01).
**
****************************************************************************/
static UData_t RoundToStep(UData_t freq, UData_t f_Step, UData_t f_ref)
{
    return f_ref * (freq / f_ref)
        + f_Step * (((freq % f_ref) + (f_Step / 2)) / f_Step);
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
**   N/A   12-20-2006    RSK    Ver 1.04: Adding fLO_FractionalTerm() usage.
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
    return ((term1 << 14) + term2);
}


/****************************************************************************
** LOCAL FUNCTION
**
**  Name: CalcLOMult
**
**  Description:    Calculates Integer divider value and the numerator
**                  value for LO's FracN PLL.
**
**                  This function assumes that the f_LO and f_Ref are
**                  evenly divisible by f_LO_Step.
**
**  Parameters:     Div       - OUTPUT: Whole number portion of the multiplier
**                  FracN     - OUTPUT: Fractional portion of the multiplier
**                  f_LO      - desired LO frequency.
**                  denom     - LO FracN denominator value
**                  f_Ref     - SRO frequency.
**
**  Returns:        Recalculated LO frequency.
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   05-30-2006    DAD    Ver 1.0: Modified version of mt2260.c (Ver 1.01).
**   N/A   12-20-2006    RSK    Ver 1.04: Adding fLO_FractionalTerm() usage.
**
****************************************************************************/
static UData_t CalcLOMult(UData_t *Div,
                          UData_t *FracN,
                          UData_t  f_LO,
                          UData_t  denom,
                          UData_t  f_Ref)
{
    UData_t a, b, i;
    const SData_t TwoNShift = 13;   // bits to shift to obtain 2^n qty
    const SData_t RoundShift = 18;  // bits to shift before rounding

    /*  Calculate the whole number portion of the divider */
    *Div = f_LO / f_Ref;

    /*
    **  Calculate the FracN numerator 1 bit at a time.  This keeps the
    **  integer values from overflowing when large values are multiplied.
    **  This loop calculates the fractional portion of F/20MHz accurate
    **  to 32 bits.  The 2^n factor is represented by the placement of
    **  the value in the 32-bit word.  Since we want as much accuracy
    **  as possible, we'll leave it at the top of the word.
    */
    *FracN = 0;
    a = f_LO;
    for (i=32; i>0; --i)
    {
        b = 2*(a % f_Ref);
        *FracN = (*FracN * 2) + (b >= f_Ref);
        a = b;
    }

    /*
    **  If the denominator is a 2^n - 1 value (the usual case) then the
    **  value we really need is (F/20) * 2^n - (F/20).  Shifting the
    **  calculated (F/20) value to the right and subtracting produces
    **  the desired result -- still accurate to 32 bits.
    */
    if ((denom & 0x01) != 0)
        *FracN -= (*FracN >> TwoNShift);

    /*
    ** Now shift the result so that it is 1 bit bigger than we need,
    ** use the low-order bit to round the remaining bits, and shift
    ** to make the answer the desired size.
    */
    *FracN >>= RoundShift;
    *FracN = (*FracN & 0x01) + (*FracN >> 1);

    /*  Check for rollover (cannot happen with 50 kHz step size) */
    if (*FracN == (denom | 1))
    {
        *FracN = 0;
        ++Div;
    }


    return (f_Ref * (*Div)) + fLO_FractionalTerm( f_Ref, *FracN, denom );
}


/****************************************************************************
** LOCAL FUNCTION
**
**  Name: GetCrossover
**
**  Description:    Determines the appropriate value in the set of
**                  crossover frequencies.
**
**                  This function assumes that the crossover frequency table
**                  ias been properly initialized in descending order.
**
**  Parameters:     f_in      - The input frequency to use.
**                  freqs     - The array of crossover frequency entries.
**
**  Returns:        Index of crossover frequency band to use.
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   10-27-2006    RSK    Original
**
****************************************************************************/
static U8Data GetCrossover( UData_t f_in,  UData_t* freqs )
{
    U8Data idx;
    U8Data retVal = 0;

    for (idx=0; idx< (U8Data)MT2266_NUM_XFREQS; idx++)
    {
        if ( freqs[idx] >= f_in)
        {
            retVal = (U8Data)MT2266_NUM_XFREQS - idx;
            break;
        }
    }

    return retVal;
}


/****************************************************************************
**
**  Name: MT2266_ChangeFreq
**
**  Description:    Change the tuner's tuned frequency to f_in.
**
**  Parameters:     h           - Open handle to the tuner (from MT2266_Open).
**                  f_in        - RF input center frequency (in Hz).
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_INV_HANDLE    - Invalid tuner handle
**                      MT_DNC_UNLOCK    - Downconverter PLL unlocked
**                      MT_COMM_ERR      - Serial bus communications error
**                      MT_FIN_RANGE     - Input freq out of range
**                      MT_DNC_RANGE     - Downconverter freq out of range
**
**  Dependencies:   MUST CALL MT2266_Open BEFORE MT2266_ChangeFreq!
**
**                  MT_ReadSub       - Read byte(s) of data from the two-wire-bus
**                  MT_WriteSub      - Write byte(s) of data to the two-wire-bus
**                  MT_Sleep         - Delay execution for x milliseconds
**                  MT2266_GetLocked - Checks to see if the PLL is locked
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   05-30-2006    DAD    Ver 1.0: Modified version of mt2260.c (Ver 1.01).
**   N/A   11-01-2006    RSK    Ver 1.02: Added usage of UFILT0 and UFILT1.
**   N/A   11-29-2006    DAD    Ver 1.03: Parenthesis clarification
**   118   05-09-2007    RSK    Ver 1.05: Refactored to call _Tune() API.
**
****************************************************************************/
UData_t MT2266_ChangeFreq(Handle_t h,
                          UData_t f_in)     /* RF input center frequency   */
{
    return (MT2266_Tune(h, f_in));
}


/****************************************************************************
**
**  Name: MT2266_Tune
**
**  Description:    Change the tuner's tuned frequency to f_in.
**
**  Parameters:     h           - Open handle to the tuner (from MT2266_Open).
**                  f_in        - RF input center frequency (in Hz).
**
**  Returns:        status:
**                      MT_OK            - No errors
**                      MT_INV_HANDLE    - Invalid tuner handle
**                      MT_DNC_UNLOCK    - Downconverter PLL unlocked
**                      MT_COMM_ERR      - Serial bus communications error
**                      MT_FIN_RANGE     - Input freq out of range
**                      MT_DNC_RANGE     - Downconverter freq out of range
**
**  Dependencies:   MUST CALL MT2266_Open BEFORE MT2266_Tune!
**
**                  MT_ReadSub       - Read byte(s) of data from the two-wire-bus
**                  MT_WriteSub      - Write byte(s) of data to the two-wire-bus
**                  MT_Sleep         - Delay execution for x milliseconds
**                  MT2266_GetLocked - Checks to see if the PLL is locked
**
**  Revision History:
**
**   SCR      Date      Author  Description
**  -------------------------------------------------------------------------
**   N/A   05-30-2006    DAD    Ver 1.0: Modified version of mt2260.c (Ver 1.01).
**   N/A   11-01-2006    RSK    Ver 1.02: Added usage of UFILT0 and UFILT1.
**   N/A   11-29-2006    DAD    Ver 1.03: Parenthesis clarification
**   118   05-09-2007    RSK    Ver 1.05: Adding Standard MTxxxx_Tune() API.
**
****************************************************************************/
UData_t MT2266_Tune(Handle_t h,
                    UData_t f_in)     /* RF input center frequency   */
{
    MT2266_Info_t* pInfo = (MT2266_Info_t*) h;

    UData_t status = MT_OK;       /*  status of operation             */
    UData_t LO;                   /*  LO register value               */
    UData_t Num;                  /*  Numerator for LO reg. value     */
    UData_t ofLO;                 /*  last time's LO frequency        */
    UData_t ofin;                 /*  last time's input frequency     */
    U8Data  LO_Band;              /*  LO Mode bits                    */
    UData_t s_fRef;               /*  Ref Freq scaled for LO Band     */
    UData_t this_band;            /*  Band for the requested freq     */
    UData_t SROx2;                /*  SRO times 2                     */

    /*  Verify that the handle passed points to a valid tuner         */
    if (IsValidHandle(pInfo) == 0)
        return MT_INV_HANDLE;

    /*
    **  Save original input and LO value
    */
    ofLO = pInfo->f_LO;
    ofin = pInfo->f_in;

    /*
    **  Assign in the requested input value
    */
    pInfo->f_in = f_in;

    /*
    **  Get the SRO multiplier value
    */
    SROx2 = (2 - (pInfo->reg[MT2266_SRO_CTRL] & 0x01));

	/* Check f_Step value */
	if(pInfo->f_Step == 0)
		return MT_UNKNOWN;

	/*  Request an LO that is on a step size boundary  */
    pInfo->f_LO = RoundToStep(f_in, pInfo->f_Step, pInfo->f_Ref);

    if (pInfo->f_LO < MIN_VHF_FREQ)
    {
        status |= MT_FIN_RANGE | MT_ARG_RANGE | MT_DNC_RANGE;
        return status;  /* Does not support frequencies below MIN_VHF_FREQ  */
    }
    else if (pInfo->f_LO <= MAX_VHF_FREQ)
    {
        /*  VHF Band  */
        s_fRef = pInfo->f_Ref * SROx2 / 4;
        LO_Band = 0;
        this_band = MT2266_VHF_BAND;
    }
    else if (pInfo->f_LO < MIN_UHF_FREQ)
    {
        status |= MT_FIN_RANGE | MT_ARG_RANGE | MT_DNC_RANGE;
        return status;  /* Does not support frequencies between MAX_VHF_FREQ & MIN_UHF_FREQ */
    }
    else if (pInfo->f_LO <= MAX_UHF_FREQ)
    {
        /*  UHF Band  */
        s_fRef = pInfo->f_Ref * SROx2 / 2;
        LO_Band = 1;
        this_band = MT2266_UHF_BAND;
    }
    else
    {
        status |= MT_FIN_RANGE | MT_ARG_RANGE | MT_DNC_RANGE;
        return status;  /* Does not support frequencies above MAX_UHF_FREQ */
    }

    /*
    ** Calculate the LO frequencies and the values to be placed
    ** in the tuning registers.
    */
    pInfo->f_LO = CalcLOMult(&LO, &Num, pInfo->f_LO, 8191, s_fRef);

    /*
    **  If we have the same LO frequencies and we're already locked,
    **  then just return without writing any registers.
    */
    if ((ofLO == pInfo->f_LO)
        && ((pInfo->reg[MT2266_STATUS_1] & 0x40) == 0x40))
    {
        return (status);
    }

    /*
    ** Reset defaults here if we're tuning into a new band
    */
    if (MT_NO_ERROR(status))
    {
        if (this_band != pInfo->band)
        {
            MT2266_DefaultsEntry *defaults = MT_NULL;
            switch (this_band)
            {
                case MT2266_VHF_BAND:
                    defaults = &MT2266_VHF_defaults[0];
                    break;
                case MT2266_UHF_BAND:
                    defaults = &MT2266_UHF_defaults[0];
                    break;
                default:
                    status |= MT_ARG_RANGE;
            }
            if ( MT_NO_ERROR(status))
            {
                while (defaults->data && MT_NO_ERROR(status))
                {
                    status |= MT2266_WriteSub(pInfo->hUserData, pInfo->address, defaults->data[0], &defaults->data[1], defaults->cnt);
                    defaults++;
                }
                /* re-read the new registers into the cached values */
                status |= MT2266_ReadSub(pInfo->hUserData, pInfo->address, 0, &pInfo->reg[0], END_REGS);
                pInfo->band = this_band;
            }
        }
    }

    /*
    **  Place all of the calculated values into the local tuner
    **  register fields.
    */
    if (MT_NO_ERROR(status))
    {
        pInfo->reg[MT2266_LO_CTRL_1] = (U8Data)(Num >> 8);
        pInfo->reg[MT2266_LO_CTRL_2] = (U8Data)(Num & 0xFF);
        pInfo->reg[MT2266_LO_CTRL_3] = (U8Data)(LO & 0xFF);

        /*
        ** Now write out the computed register values
        */
        status |= MT2266_WriteSub(pInfo->hUserData, pInfo->address, MT2266_LO_CTRL_1, &pInfo->reg[MT2266_LO_CTRL_1], 3);

        if (pInfo->band == MT2266_UHF_BAND)
        {
            U8Data UFilt0 = 0;                        /*  def when f_in > all    */
            U8Data UFilt1 = 0;                        /*  def when f_in > all    */
            UData_t* XFreq0;
            UData_t* XFreq1;
            SData_t ClearTune_Fuse;
            SData_t f_offset;
            UData_t f_in_;

            PREFETCH(MT2266_BAND_CTRL, 2);  /* Fetch register(s) if __NO_CACHE__ defined */
            PREFETCH(MT2266_STATUS_5, 1);  /* Fetch register(s) if __NO_CACHE__ defined */

            XFreq0 = (pInfo->reg[MT2266_BAND_CTRL] & 0x10) ? pInfo->xfreqs.xfreq[ MT2266_UHF0_ATTEN ] : pInfo->xfreqs.xfreq[ MT2266_UHF0 ];
            XFreq1 = (pInfo->reg[MT2266_BAND_CTRL] & 0x10) ? pInfo->xfreqs.xfreq[ MT2266_UHF1_ATTEN ] : pInfo->xfreqs.xfreq[ MT2266_UHF1 ];

            ClearTune_Fuse = pInfo->reg[MT2266_STATUS_5] & 0x07;
            f_offset = (10000000) * ((ClearTune_Fuse > 3) ? (ClearTune_Fuse - 8) : ClearTune_Fuse);
            f_in_ = (f_in - f_offset) / 1000 / TUNE_STEP_SIZE;

            UFilt0 = GetCrossover( f_in_, XFreq0 );
            UFilt1 = GetCrossover( f_in_, XFreq1 );

            /*  If UFilt == 16, set UBANDen and set UFilt = 15  */
            if ( (UFilt0 == 16) || (UFilt1 == 16) )
            {
                pInfo->reg[MT2266_BAND_CTRL] |= 0x01;
                if( UFilt0 > 0 ) UFilt0--;
                if( UFilt1 > 0 ) UFilt1--;
            }
            else
                pInfo->reg[MT2266_BAND_CTRL] &= ~(0x01);

            pInfo->reg[MT2266_BAND_CTRL] =
                    (pInfo->reg[MT2266_BAND_CTRL] & 0x3F) | (LO_Band << 6);

            pInfo->reg[MT2266_CLEARTUNE] = (UFilt1 << 4) | UFilt0;
            /*  Write UBANDsel  [05] & ClearTune [06]  */
            status |= MT2266_WriteSub(pInfo->hUserData, pInfo->address, MT2266_BAND_CTRL, &pInfo->reg[MT2266_BAND_CTRL], 2);
        }
    }

    /*
    **  Check for LO lock
    */
    if (MT_NO_ERROR(status))
    {
        status |= MT2266_GetLocked(h);
    }

    return (status);
}



