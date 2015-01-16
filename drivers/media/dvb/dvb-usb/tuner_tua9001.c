/**

@file

@brief   TUA9001 tuner module definition

One can manipulate TUA9001 tuner through TUA9001 module.
TUA9001 module is derived from tuner module.

*/


#include "tuner_tua9001.h"





/**

@brief   TUA9001 tuner module builder

Use BuildTua9001Module() to build TUA9001 module, set all module function pointers with the corresponding functions,
and initialize module private variables.


@param [in]   ppTuner                      Pointer to TUA9001 tuner module pointer
@param [in]   pTunerModuleMemory           Pointer to an allocated tuner module memory
@param [in]   pBaseInterfaceModuleMemory   Pointer to an allocated base interface module memory
@param [in]   pI2cBridgeModuleMemory       Pointer to an allocated I2C bridge module memory
@param [in]   DeviceAddr                   TUA9001 I2C device address


@note
	-# One should call BuildTua9001Module() to build TUA9001 module before using it.

*/
void
BuildTua9001Module(
	TUNER_MODULE **ppTuner,
	TUNER_MODULE *pTunerModuleMemory,
	BASE_INTERFACE_MODULE *pBaseInterfaceModuleMemory,
	I2C_BRIDGE_MODULE *pI2cBridgeModuleMemory,
	unsigned char DeviceAddr
	)
{
	TUNER_MODULE *pTuner;
	TUA9001_EXTRA_MODULE *pExtra;



	// Set tuner module pointer.
	*ppTuner = pTunerModuleMemory;

	// Get tuner module.
	pTuner = *ppTuner;

	// Set base interface module pointer and I2C bridge module pointer.
	pTuner->pBaseInterface = pBaseInterfaceModuleMemory;
	pTuner->pI2cBridge = pI2cBridgeModuleMemory;

	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Tua9001);



	// Set tuner type.
	pTuner->TunerType = TUNER_TYPE_TUA9001;

	// Set tuner I2C device address.
	pTuner->DeviceAddr = DeviceAddr;


	// Initialize tuner parameter setting status.
	pTuner->IsRfFreqHzSet = NO;

	// Initialize tuner module variables.
	// Note: Need to give both RF frequency and bandwidth for TUA9001 tuning function,
	//       so we have to give a default RF frequncy.
	pTuner->RfFreqHz = TUA9001_RF_FREQ_HZ_DEFAULT;


	// Set tuner module manipulating function pointers.
	pTuner->GetTunerType  = tua9001_GetTunerType;
	pTuner->GetDeviceAddr = tua9001_GetDeviceAddr;

	pTuner->Initialize    = tua9001_Initialize;
	pTuner->SetRfFreqHz   = tua9001_SetRfFreqHz;
	pTuner->GetRfFreqHz   = tua9001_GetRfFreqHz;


	// Initialize tuner extra module variables.
	// Note: Need to give both RF frequency and bandwidth for TUA9001 tuning function,
	//       so we have to give a default bandwidth.
	pExtra->BandwidthMode = TUA9001_BANDWIDTH_MODE_DEFAULT;
	pExtra->IsBandwidthModeSet = NO;

	// Set tuner extra module function pointers.
	pExtra->SetBandwidthMode       = tua9001_SetBandwidthMode;
	pExtra->GetBandwidthMode       = tua9001_GetBandwidthMode;
	pExtra->GetRegBytesWithRegAddr = tua9001_GetRegBytesWithRegAddr;
	pExtra->SetSysRegByte          = tua9001_SetSysRegByte;
	pExtra->GetSysRegByte          = tua9001_GetSysRegByte;


	return;
}





/**

@see   TUNER_FP_GET_TUNER_TYPE

*/
void
tua9001_GetTunerType(
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
tua9001_GetDeviceAddr(
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
tua9001_Initialize(
	TUNER_MODULE *pTuner
	)
{
	TUA9001_EXTRA_MODULE *pExtra;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Tua9001);

	// Initialize TUA9001 tuner.
	if(initializeTua9001(pTuner) != TUA9001_TUNER_OK)
		goto error_status_initialize_tuner;


	return FUNCTION_SUCCESS;


error_status_initialize_tuner:
	return FUNCTION_ERROR;
}





/**

@see   TUNER_FP_SET_RF_FREQ_HZ

*/
int
tua9001_SetRfFreqHz(
	TUNER_MODULE *pTuner,
	unsigned long RfFreqHz
	)
{
	TUA9001_EXTRA_MODULE *pExtra;

	long RfFreqKhz;
	int BandwidthMode;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Tua9001);

	// Get bandwidth mode.
	BandwidthMode = pExtra->BandwidthMode;

	// Calculate RF frequency in KHz.
	// Note: RfFreqKhz = round(RfFreqHz / 1000)
	RfFreqKhz = (long)((RfFreqHz + 500) / 1000);

	// Set TUA9001 RF frequency and bandwidth.
	if(tuneTua9001(pTuner, RfFreqKhz, BandwidthMode) != TUA9001_TUNER_OK)
		goto error_status_set_tuner_rf_frequency;


	// Set tuner RF frequency parameter.
	pTuner->RfFreqHz      = (unsigned long)(RfFreqKhz * 1000);
	pTuner->IsRfFreqHzSet = YES;


	return FUNCTION_SUCCESS;


error_status_set_tuner_rf_frequency:
	return FUNCTION_ERROR;
}





/**

@see   TUNER_FP_GET_RF_FREQ_HZ

*/
int
tua9001_GetRfFreqHz(
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

@brief   Set TUA9001 tuner bandwidth mode.

*/
int
tua9001_SetBandwidthMode(
	TUNER_MODULE *pTuner,
	int BandwidthMode
	)
{
	TUA9001_EXTRA_MODULE *pExtra;

	long RfFreqKhz;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Tua9001);

	// Get RF frequncy in KHz.
	// Note: Doesn't need to take round() of RfFreqHz, because its value unit is 1000 Hz.
	RfFreqKhz = (long)(pTuner->RfFreqHz / 1000);

	// Set TUA9001 RF frequency and bandwidth.
	if(tuneTua9001(pTuner, RfFreqKhz, BandwidthMode) != TUA9001_TUNER_OK)
		goto error_status_set_tuner_bandwidth;


	// Set tuner bandwidth parameter.
	pExtra->BandwidthMode      = BandwidthMode;
	pExtra->IsBandwidthModeSet = YES;


	return FUNCTION_SUCCESS;


error_status_set_tuner_bandwidth:
	return FUNCTION_ERROR;
}





/**

@brief   Get TUA9001 tuner bandwidth mode.

*/
int
tua9001_GetBandwidthMode(
	TUNER_MODULE *pTuner,
	int *pBandwidthMode
	)
{
	TUA9001_EXTRA_MODULE *pExtra;



	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Tua9001);


	// Get tuner bandwidth in Hz from tuner module.
	if(pExtra->IsBandwidthModeSet != YES)
		goto error_status_get_tuner_bandwidth;

	*pBandwidthMode = pExtra->BandwidthMode;


	return FUNCTION_SUCCESS;


error_status_get_tuner_bandwidth:
	return FUNCTION_ERROR;
}





/**

@brief   Get register bytes with address.

*/
int
tua9001_GetRegBytesWithRegAddr(
	TUNER_MODULE *pTuner,
	unsigned char DeviceAddr,
	unsigned char RegAddr,
	unsigned char *pReadingBytes,
	unsigned char ByteNum
	)
{
	// Get tuner register byte.
	//if(rtl2832usb_ReadWithRegAddr(DeviceAddr, RegAddr, pReadingBytes, ByteNum) != FUNCTION_SUCCESS)
	//	goto error_status_get_tuner_registers_with_address;

	I2C_BRIDGE_MODULE *pI2cBridge;
	BASE_INTERFACE_MODULE *pBaseInterface;

	unsigned char TunerDeviceAddr;

	struct dvb_usb_device	*d;	


	// Get I2C bridge.
	pI2cBridge = pTuner->pI2cBridge;
	pBaseInterface = pTuner->pBaseInterface;	

	// Get tuner device address.
	pTuner->GetDeviceAddr(pTuner,&TunerDeviceAddr); 

	pBaseInterface->GetUserDefinedDataPointer(pBaseInterface, (void **)&d);	

	if( read_rtl2832_tuner_register( d, TunerDeviceAddr, RegAddr, pReadingBytes, ByteNum ) )
		goto error_status_get_tuner_registers_with_address;

	return FUNCTION_SUCCESS;


error_status_get_tuner_registers_with_address:
	return FUNCTION_ERROR;
}





/**

@brief   Set system register byte.

*/
int
tua9001_SetSysRegByte(
	TUNER_MODULE *pTuner,
	unsigned short RegAddr,
	unsigned char WritingByte
	)
{
	// Set demod system register byte.
//	if(RTK_SYS_Byte_Write(RegAddr, LEN_1_BYTE, &WritingByte) != TRUE)
//		goto error_status_set_system_registers;

	I2C_BRIDGE_MODULE *pI2cBridge;
	BASE_INTERFACE_MODULE *pBaseInterface;

	struct dvb_usb_device	*d;	

	// Get I2C bridge.
	pI2cBridge = pTuner->pI2cBridge;
	pBaseInterface = pTuner->pBaseInterface;	

	pBaseInterface->GetUserDefinedDataPointer(pBaseInterface, (void **)&d);	

	if ( write_usb_sys_char_bytes( d, RTD2832U_SYS, RegAddr, &WritingByte, LEN_1_BYTE) ) 
		goto error_status_set_system_registers;

	return FUNCTION_SUCCESS;


error_status_set_system_registers:
	return FUNCTION_ERROR;
}





/**

@brief   Get system register byte.

*/
int
tua9001_GetSysRegByte(
	TUNER_MODULE *pTuner,
	unsigned short RegAddr,
	unsigned char *pReadingByte
	)
{
	// Get demod system register byte.
//	if(RTK_SYS_Byte_Read(RegAddr, LEN_1_BYTE, pReadingByte) != TRUE)
//		goto error_status_get_system_registers;

	I2C_BRIDGE_MODULE *pI2cBridge;
	BASE_INTERFACE_MODULE *pBaseInterface;

	struct dvb_usb_device	*d;	

	// Get I2C bridge.
	pI2cBridge = pTuner->pI2cBridge;
	pBaseInterface = pTuner->pBaseInterface;	

	pBaseInterface->GetUserDefinedDataPointer(pBaseInterface, (void **)&d);	

	if ( read_usb_sys_char_bytes(d, RTD2832U_SYS, RegAddr, pReadingByte, LEN_1_BYTE) )
		goto error_status_get_system_registers;

	return FUNCTION_SUCCESS;


error_status_get_system_registers:
	return FUNCTION_ERROR;
}





// TUA9001 custom-implement functions


int tua9001setRESETN (TUNER_MODULE *pTuner, unsigned int i_state)
{
	TUA9001_EXTRA_MODULE *pExtra;
	unsigned char Byte;


	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Tua9001);

	// Get GPO value.

	if(pExtra->GetSysRegByte(pTuner, GPO_ADDR, &Byte) != FUNCTION_SUCCESS)
		goto error_status_get_system_registers;

	// Note: Hardware PCB has inverter in this pin, should give inverted value on GPIO3.
	if (i_state == TUA9001_H_LEVEL)
	{
		/* set tuner RESETN pin to "H"  */
		// Note: The GPIO3 output value should be '0'.
		Byte &= ~BIT_3_MASK;
	}
	else
	{
		/* set tuner RESETN pin to "L"  */
		// Note: The GPIO3 output value should be '1'.
		Byte |= BIT_3_MASK;
	}

	// Set GPO value.
	if(pExtra->SetSysRegByte(pTuner, GPO_ADDR, Byte) != FUNCTION_SUCCESS)
		goto error_status_set_system_registers;


	return TUA9001_TUNER_OK;


error_status_set_system_registers:
error_status_get_system_registers:
	return TUA9001_TUNER_ERR;
}



int tua9001setRXEN (TUNER_MODULE *pTuner, unsigned int i_state)
{
	TUA9001_EXTRA_MODULE *pExtra;
	unsigned char Byte;


	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Tua9001);

	// Get GPO value.
	if(pExtra->GetSysRegByte(pTuner, GPO_ADDR, &Byte) != FUNCTION_SUCCESS)
		goto error_status_get_system_registers;

	if (i_state == TUA9001_H_LEVEL)
	{
		/* set tuner RXEN pin to "H"  */
		// Note: The GPIO1 output value should be '1'.
		Byte |= BIT_1_MASK;
	}
	else
	{
		/* set tuner RXEN pin to "L"  */
		// Note: The GPIO1 output value should be '0'.
		Byte &= ~BIT_1_MASK;
	}

	// Set GPO value.
	if(pExtra->SetSysRegByte(pTuner, GPO_ADDR, Byte) != FUNCTION_SUCCESS)
		goto error_status_set_system_registers;


	return TUA9001_TUNER_OK;


error_status_set_system_registers:
error_status_get_system_registers:
	return TUA9001_TUNER_ERR;
}



int tua9001setCEN (TUNER_MODULE *pTuner, unsigned int i_state)
{
	// Do nothing.
	// Note: Hardware PCB always gives 'H' to tuner CEN pin.
	return TUA9001_TUNER_OK;
}



int tua9001waitloop (TUNER_MODULE *pTuner, unsigned int i_looptime)
{
	BASE_INTERFACE_MODULE *pBaseInterface;
	unsigned long WaitTimeMs;


	// Get base interface.
	pBaseInterface = pTuner->pBaseInterface;

	/* wait time = i_looptime * 1 uS */
	// Note: 1. The unit of WaitMs() function is ms.
	//       2. WaitTimeMs = ceil(i_looptime / 1000)
	WaitTimeMs = i_looptime / 1000;

	if((i_looptime % 1000) > 0)
		WaitTimeMs += 1;

	pBaseInterface->WaitMs(pBaseInterface, WaitTimeMs);


	return TUA9001_TUNER_OK;
}



int tua9001i2cBusWrite (TUNER_MODULE *pTuner, unsigned char deviceAddress, unsigned char registerAddress, char *data,
				 unsigned int length)
{

#if 0
	BASE_INTERFACE_MODULE *pBaseInterface;

	unsigned char ByteNum;
	unsigned char WritingBytes[I2C_BUFFER_LEN];
	unsigned int i;


	// Get base interface.
	pBaseInterface = pTuner->pBaseInterface;


	/* I2C write data format */
	/* STA  device_address  ACK  register_address  ACK   H_Byte-Data ACK   L_Byte-Data  !ACK  STO */

	/* STA = start condition, ACK = Acknowledge, STO = stop condition                             */   
	/* *data  = pointer to data source   */
	/* length = number of bytes to write */

	// Determine byte number.
	ByteNum = length + LEN_1_BYTE;

	// Determine writing bytes.
	WritingBytes[0] = registerAddress;

	for(i = 0; i < length; i++)
		WritingBytes[LEN_1_BYTE + i] = data[i];


	// Send I2C writing command.
	if(pBaseInterface->I2cWrite(pBaseInterface, deviceAddress, WritingBytes, ByteNum) != FUNCTION_SUCCESS)
		goto error_status_set_tuner_registers;


	return TUA9001_TUNER_OK;


error_status_set_tuner_registers:
	return TUA9001_TUNER_ERR;
#endif
	BASE_INTERFACE_MODULE *pBaseInterface;

	unsigned char ByteNum;
	unsigned char WritingBytes[I2C_BUFFER_LEN];
	unsigned int i=0;

	I2C_BRIDGE_MODULE *pI2cBridge;

	struct dvb_usb_device	*d;

	// Get base interface.
	pBaseInterface = pTuner->pBaseInterface;

	// Get I2C bridge.
	pI2cBridge = pTuner->pI2cBridge;	

	// Get tuner device address.
	pBaseInterface->GetUserDefinedDataPointer(pBaseInterface, (void **)&d);	

	// Determine byte number.
	ByteNum = length;

	// Determine writing bytes.
	//WritingBytes[0] = registerAddress;

	for(i = 0; i < length; i++)
		WritingBytes[i] = data[i];

	// Send I2C writing command.
	if( write_rtl2832_tuner_register( d, deviceAddress, registerAddress, WritingBytes, ByteNum ) )
		goto error_status_set_tuner_registers;


	return TUA9001_TUNER_OK;


error_status_set_tuner_registers:
	return TUA9001_TUNER_ERR;

}



int tua9001i2cBusRead (TUNER_MODULE *pTuner, unsigned char deviceAddress, unsigned char registerAddress, char *data,
				unsigned int length)
{
	TUA9001_EXTRA_MODULE *pExtra;


	// Get tuner extra module.
	pExtra = &(pTuner->Extra.Tua9001);


	/* I2C read data format */
	/* STA  device_address  ACK  register_address  ACK  STA H_Byte-Data ACK  device_address_read  ACK  H_Byte-Data ACK L_Byte-Data  ACKH  STO */

	/* STA = start condition, ACK = Acknowledge (generated by TUA9001), ACKH = Acknowledge (generated by Host), STO = stop condition                             */   
	/* *data  = pointer to data destination   */
	/* length = number of bytes to read       */

	// Get tuner register bytes with address.
	// Note: We must use re-start I2C reading format for TUA9001 tuner register reading.
	if(pExtra->GetRegBytesWithRegAddr(pTuner, deviceAddress, registerAddress, data, length) != FUNCTION_SUCCESS)
		goto error_status_get_tuner_registers;


	return TUA9001_TUNER_OK;


error_status_get_tuner_registers:
	return TUA9001_TUNER_ERR;
}























// The following context is source code provided by Infineon.





// Infineon source code - driver_tua9001.c


/* ============================================================================
** Copyright (C) 1997-2007 Infineon AG All rights reserved.
** ============================================================================
**
** ============================================================================
** Revision Information :
**    File name: driver_tua9001.c
**    Version:  V 1.01
**    Date: 
**
** ============================================================================
** History: 
** 
** Date         Author  Comment
** ----------------------------------------------------------------------------
**
** 2007.11.06   Walter Pichler    created.
** 2008.04.08   Walter Pichler    adaption to TUA 9001E
**
** ============================================================================
*/

/*============================================================================
Includes
============================================================================*/

//#include "driver_tua9001.h"
//#include "driver_tua9001_NeededFunctions.h"   /* Note: This function have to be provided by the user */

/*============================================================================
Local compiler keeys         ( usage depends on the application )
============================================================================*/

#define CRYSTAL_26_0_MHZ
//#define CRYSTAL_19_2_MHZ
//#define CRYSTAL_20_48_MHZ

//#define AGC_BY_IIC
//#define AGC_BY_AGC_BUS
#define AGC_BY_EXT_PIN


/*============================================================================
Named Constants Definitions    ( usage depends on the application )
============================================================================*/

#define TUNERs_TUA9001_DEVADDR    0xC0

/* Note: The correct device address depends hardware settings. See Datasheet
      and User Manual for details. */

/*============================================================================
Local Named Constants Definitions
============================================================================*/
#define		OPERATIONAL_MODE     	0x03 
#define		CHANNEL_BANDWITH    	0x04
#define		SW_CONTR_TIME_SLICING	0x05
#define		BASEBAND_GAIN_CONTROL	0x06
#define		MANUAL_BASEBAND_GAIN	0x0b
#define		REFERENCE_FREQUENCY 	0x1d
#define		CHANNEL_WORD        	0x1f
#define		CHANNEL_OFFSET	    	0x20
#define		CHANNEL_FILTER_TRIMMING	0x2f
#define		OUTPUT_BUFFER	    	0x32
#define		RF_AGC_CONFIG_A	    	0x36
#define		RF_AGC_CONFIG_B	    	0x37
#define		UHF_LNA_SELECT	    	0x39
#define		LEVEL_DETECTOR	    	0x3a
#define		MIXER_CURRENT	    	0x3b
#define		PORT_CONTROL		    0x3e
#define		CRYSTAL_TRIMMING    	0x41
#define		CHANNEL_FILTER_STATUS	0x60
#define		SIG_STRENGHT_INDICATION	0x62
#define		PLL_LOCK	        	0x69
#define		RECEIVER_STATE	    	0x70
#define		RF_INPUT	        	0x71
#define		BASEBAND_GAIN	    	0x72
#define		CHIP_IDENT_CODE	    	0x7e
#define		CHIP_REVISION	    	0x7f

#define TUNERs_TUA9001_BW_8         0xcf
#define TUNERs_TUA9001_BW_7         0x10
#define TUNERs_TUA9001_BW_6         0x20
#define TUNERs_TUA9001_BW_5         0x30




/*============================================================================
 Types definition
============================================================================*/




/*============================================================================
 Public Functions
============================================================================*/


/**
 * tuner initialisation
 * @retval TUA9001_TUNER_OK No error
 * @retval TUA9001_TUNER_ERR Error
*/
 
int initializeTua9001 (TUNER_MODULE *pTuner)
{
//  unsigned int counter;
  char i2cseq[2];
//  tua9001tunerReceiverState_t tunerState;
  unsigned char DeviceAddr;

  // Get tuner deviece address.
  pTuner->GetDeviceAddr(pTuner, &DeviceAddr);

  /* Note: CEN may also be hard wired in the application*/
  if(tua9001setCEN    (pTuner, TUA9001_H_LEVEL) != TUA9001_TUNER_OK) goto error_status;    /* asserting Chip enable      */

  if(tua9001setRESETN (pTuner, TUA9001_L_LEVEL) != TUA9001_TUNER_OK) goto error_status;    /* asserting RESET            */
  
  if(tua9001setRXEN   (pTuner, TUA9001_L_LEVEL) != TUA9001_TUNER_OK) goto error_status;    /* RXEN to low >> IDLE STATE  */
  
  /* Note: 20ms assumes that all external power supplies are settled. If not, add more time here */
  tua9001waitloop (pTuner, 20);          /* wait for 20 uS     */
  
  if(tua9001setRESETN (pTuner, TUA9001_H_LEVEL) != TUA9001_TUNER_OK) goto error_status;    /* de-asserting RESET */

  /* This is to wait for the Crystal Oscillator to settle .. wait until IDLE mode is reached */  
//  counter = 6;
//  do
//    {
//    counter --;
//    tua9001waitloop (pTuner, 1000);      /* wait for 1 mS      */
//    if(getReceiverStateTua9001 (pTuner, &tunerState) != TUA9001_TUNER_OK) goto error_status;
//    }while ((tunerState != TUA9001_IDLE) && (counter));  

//  if (tunerState != TUA9001_IDLE)
//      return TUA9001_TUNER_ERR;   /* error >> break initialization   */

  // Replace the above check loop with 6 ms delay.
  // Because maybe there are undefined cases in getReceiverStateTua9001(), we have to avoid using the function.
  tua9001waitloop (pTuner, 6000);         /* wait for 6 mS      */

  /**** Overwrite default register value ****/ 
  i2cseq[0] = 0x65;    /* Waiting time before PLL cal. start */
  i2cseq[1] = 0x12;   
  if(tua9001i2cBusWrite (pTuner, DeviceAddr, 0x1e, i2cseq, 2) != TUA9001_TUNER_OK) goto error_status;

  i2cseq[0] = 0xB8;    /* VCO Varactor bias fine tuning */
  i2cseq[1] = 0x88;   
  if(tua9001i2cBusWrite (pTuner, DeviceAddr, 0x25, i2cseq, 2) != TUA9001_TUNER_OK) goto error_status;

  i2cseq[0] = 0x54;    /* LNA switching Threshold for UHF1/2 */
  i2cseq[1] = 0x60;
  if(tua9001i2cBusWrite (pTuner, DeviceAddr, 0x39, i2cseq, 2) != TUA9001_TUNER_OK) goto error_status;

  i2cseq[0] = 0x00;    
  i2cseq[1] = 0xC0;
  if(tua9001i2cBusWrite (pTuner, DeviceAddr, 0x3b, i2cseq, 2) != TUA9001_TUNER_OK) goto error_status;

  i2cseq[0] = 0xF0;    /* LO- Path Set LDO output voltage */
  i2cseq[1] = 0x00;
  if(tua9001i2cBusWrite (pTuner, DeviceAddr, 0x3a, i2cseq, 2) != TUA9001_TUNER_OK) goto error_status;
  
  i2cseq[0] = 0x00;    /* Set EXTAGC interval */
  i2cseq[1] = 0x00;               
  if(tua9001i2cBusWrite (pTuner, DeviceAddr, 0x08, i2cseq, 2) != TUA9001_TUNER_OK) goto error_status;
  
  i2cseq[0] = 0x00;    /* Set max. capacitive load */
  i2cseq[1] = 0x30;   
  if(tua9001i2cBusWrite (pTuner, DeviceAddr, 0x32, i2cseq, 2) != TUA9001_TUNER_OK) goto error_status;


  /**** Set Crystal Reference Frequency an Trim value ****/

#if defined(CRYSTAL_26_0_MHZ)       //  Frequency 26 MHz 
  i2cseq[0] = 0x01;
  i2cseq[1] = 0xB0;    
  if(tua9001i2cBusWrite (pTuner, DeviceAddr, 0x1d, i2cseq, 2) != TUA9001_TUNER_OK) goto error_status;

  i2cseq[0] = 0x70;              // NDK 3225 series 26 MHz XTAL 
  i2cseq[1] = 0x3a;
  if(tua9001i2cBusWrite (pTuner, DeviceAddr, 0x41, i2cseq, 2) != TUA9001_TUNER_OK) goto error_status;
  i2cseq[0] = 0x1C;
  i2cseq[1] = 0x78;
  if(tua9001i2cBusWrite (pTuner, DeviceAddr, 0x40, i2cseq, 2) != TUA9001_TUNER_OK) goto error_status;

#elif defined(CRYSTAL_19_2_MHZ)   //  Frequency 19.2 MHz 
  i2cseq[0] = 0x01;
  i2cseq[1] = 0xA0;    
  if(tua9001i2cBusWrite (pTuner, DeviceAddr, 0x1d, i2cseq, 2) != TUA9001_TUNER_OK) goto error_status;
  // Note: Insert optimised register values for 0x40 / 0x41 for used crystal 
  // contact application support for further information 
#elif defined(CRYSTAL_20_48_MHZ)  //   Frequency 20,48 MHz 
  i2cseq[0] = 0x01;
  i2cseq[1] = 0xA8;    
  if(tua9001i2cBusWrite (pTuner, DeviceAddr, 0x1d, i2cseq, 2) != TUA9001_TUNER_OK) goto error_status;
  // Note: Insert optimised register values for 0x40 / 0x41 for used crystal 
  // contact application support for further information 
#endif



 /**** Set desired Analog Baseband AGC mode ****/ 
#if defined (AGC_BY_IIC)
  i2cseq[0] = 0x00;                /* Bypass AGC controller >>  IIC based AGC */
  i2cseq[1] = 0x40;                     
  if(tua9001i2cBusWrite (pTuner, DeviceAddr, 0x06, i2cseq, 2) != TUA9001_TUNER_OK) goto error_status;
#elif defined(AGC_BY_AGC_BUS)      
  i2cseq[0] = 0x00;                /* Digital AGC bus */               
  i2cseq[1] = 0x00;                /* 0,5 dB steps    */                    
  if(tua9001i2cBusWrite (pTuner, DeviceAddr, 0x06, i2cseq, 2) != TUA9001_TUNER_OK) goto error_status;
#elif defined(AGC_BY_EXT_PIN)      
  i2cseq[0] = 0x40;                /* Ext. AGC pin     */               
  i2cseq[1] = 0x00;                                    
  if(tua9001i2cBusWrite (pTuner, DeviceAddr, 0x06, i2cseq, 2) != TUA9001_TUNER_OK) goto error_status;
#endif 

 
  /**** set desired RF AGC parameter *****/
  i2cseq[0] = 0x1c;      /* Set Wideband Detector Current (100 uA) */
  i2cseq[1] = 0x00;
  if(tua9001i2cBusWrite (pTuner, DeviceAddr, 0x2c, i2cseq, 2) != TUA9001_TUNER_OK) goto error_status;
 
  i2cseq[0] = 0xC0;      /* Set RF AGC Threshold (-32.5dBm) */
  i2cseq[1] = 0x13;
  if(tua9001i2cBusWrite (pTuner, DeviceAddr, 0x36, i2cseq, 2) != TUA9001_TUNER_OK) goto error_status;

  i2cseq[0] = 0x6f;      /* RF AGC Parameter */
  i2cseq[1] = 0x18;   
  if(tua9001i2cBusWrite (pTuner, DeviceAddr, 0x37, i2cseq, 2) != TUA9001_TUNER_OK) goto error_status;

  i2cseq[0] = 0x00;      /* aditional VCO settings  */
  i2cseq[1] = 0x08;   
  if(tua9001i2cBusWrite (pTuner, DeviceAddr, 0x27, i2cseq, 2) != TUA9001_TUNER_OK) goto error_status;

  i2cseq[0] = 0x00;      /* aditional PLL settings  */
  i2cseq[1] = 0x01;   
  if(tua9001i2cBusWrite (pTuner, DeviceAddr, 0x2a, i2cseq, 2) != TUA9001_TUNER_OK) goto error_status;

  i2cseq[0] = 0x0a;      /* VCM correction         */
  i2cseq[1] = 0x40;   
  if(tua9001i2cBusWrite (pTuner, DeviceAddr, 0x34, i2cseq, 2) != TUA9001_TUNER_OK) goto error_status;


  return TUA9001_TUNER_OK;


error_status:
  return TUA9001_TUNER_ERR;
}



/**
 * tuner tune
 * @param i_freq   tuning frequency
 * @param i_bandwidth  channel  bandwidth
 * @retval TUA9001_TUNER_OK No error
 * @retval TUA9001_TUNER_ERR Error
*/

int tuneTua9001 (TUNER_MODULE *pTuner, long i_freq, tua9001tunerDriverBW_t i_bandwidth)
{
  char i2cseq[2];
  unsigned int divider_factor;
  unsigned int ch_offset;
//  unsigned int counter;
  unsigned int lo_path_settings;
//  tua9001tunerReceiverState_t tunerState;
  unsigned char DeviceAddr;

  // Get tuner deviece address.
  pTuner->GetDeviceAddr(pTuner, &DeviceAddr);


 
  /* de-assert RXEN >> IDLE STATE */
  if(tua9001setRXEN   (pTuner, TUA9001_L_LEVEL) != TUA9001_TUNER_OK) goto error_status;          
 

  /* calculate divider factor */
  if (i_freq < 1000000)    /*  divider factor and channel offset for UHF/VHF III */
   {
   ch_offset = 0x1C20;     /* channel offset 150 MHz */
   divider_factor   =  (unsigned int) (((i_freq - 150000) * 48) / 1000);
   lo_path_settings = 0xb6de;
  }

  else                     /* calculate divider factor for L-Band Frequencies */
   {
   ch_offset = 0x5460;     /* channel offset 450 MHz */
   divider_factor   =  (unsigned int) (((i_freq - 450000) * 48) / 1000);
   lo_path_settings = 0xbede;
   }


  // Set LO Path
  i2cseq[0] = lo_path_settings >> 8;
  i2cseq[1] = lo_path_settings & 0xff;
  if(tua9001i2cBusWrite (pTuner, DeviceAddr, 0x2b, i2cseq, 2) != TUA9001_TUNER_OK) goto error_status;

  // Set channel offset
  i2cseq [0] =  ch_offset >> 8;
  i2cseq [1] =  ch_offset & 0xff;
  if(tua9001i2cBusWrite (pTuner, DeviceAddr, 0x20, i2cseq, 2) != TUA9001_TUNER_OK) goto error_status;
  
  // Set Frequency
  i2cseq [0] =  divider_factor >> 8;
  i2cseq [1] =  divider_factor & 0xff;
  if(tua9001i2cBusWrite (pTuner, DeviceAddr, 0x1f, i2cseq, 2) != TUA9001_TUNER_OK) goto error_status;
   
  // Set bandwidth
  if(tua9001i2cBusRead (pTuner, DeviceAddr,  0x04, i2cseq, 2) != TUA9001_TUNER_OK) goto error_status;         /* get current register value */
  i2cseq [0] &= TUNERs_TUA9001_BW_8;
 
  switch (i_bandwidth)
    {
    case TUA9001_TUNER_BANDWIDTH_8MHZ:
         // Do nothing.
         break; 
    case TUA9001_TUNER_BANDWIDTH_7MHZ: i2cseq [0] |= TUNERs_TUA9001_BW_7;
         break; 
    case TUA9001_TUNER_BANDWIDTH_6MHZ: i2cseq [0] |= TUNERs_TUA9001_BW_6;
         break; 
    case TUA9001_TUNER_BANDWIDTH_5MHZ: i2cseq [0] |= TUNERs_TUA9001_BW_5;
         break; 
	default:
         goto error_status;
         break;
    }

  if(tua9001i2cBusWrite (pTuner, DeviceAddr, 0x04, i2cseq, 2) != TUA9001_TUNER_OK) goto error_status;
 
  /* assert RXEN >> RX STATE */
  if(tua9001setRXEN   (pTuner, TUA9001_H_LEVEL) != TUA9001_TUNER_OK) goto error_status;

  /* This is to wait for the RX state to settle .. wait until RX mode is reached */  
//  counter = 3;
//  do
//    {
//    counter --;
//    tua9001waitloop (pTuner, 1000);         /* wait for 1 mS      */
//    if(getReceiverStateTua9001 (pTuner, &tunerState) != TUA9001_TUNER_OK) goto error_status;
//    }while ((tunerState != TUA9001_RX) && (counter));  

//  if (tunerState != TUA9001_RX)
//    {
//    if(tua9001setRXEN  (pTuner, TUA9001_L_LEVEL) != TUA9001_TUNER_OK) goto error_status;      /* d-assert RXEN >> IDLE STATE */
//    return   TUA9001_TUNER_ERR;      /* error >> tuning fail        */
//    }

  // Replace the above check loop with 3 ms delay.
  // Because maybe there are undefined cases in getReceiverStateTua9001(), we have to avoid using the function.
  tua9001waitloop (pTuner, 3000);         /* wait for 3 mS      */

  return TUA9001_TUNER_OK;


error_status:
  return TUA9001_TUNER_ERR;
}


/**
 * Get pll locked state
 * @param o_pll  pll locked state
 * @retval TUA9001_TUNER_OK No error
 * @retval TUA9001_TUNER_ERR Error
*/

int getPllLockedStateTua9001 (TUNER_MODULE *pTuner, tua9001tunerPllLocked_t *o_pll)
{
  char i2cseq[2];
  unsigned char DeviceAddr;

  // Get tuner deviece address.
  pTuner->GetDeviceAddr(pTuner, &DeviceAddr);

  if(tua9001i2cBusRead (pTuner, DeviceAddr, 0x69, i2cseq, 2) != TUA9001_TUNER_OK) goto error_status;           /* get current register value */
  
  o_pll[0]  = (i2cseq[1] & 0x08) ? TUA9001_PLL_LOCKED : TUA9001_PLL_NOT_LOCKED;
 
  return TUA9001_TUNER_OK;


error_status:
  return TUA9001_TUNER_ERR;
}


/**
 * Get tuner state
 * @param o_tunerState tuner state
 * @retval TUA9001_TUNER_OK No error
 * @retval TUA9001_TUNER_ERR Error
*/
/*
int getReceiverStateTua9001 (TUNER_MODULE *pTuner, tua9001tunerReceiverState_t *o_tunerState)
{
  char i2cseq[2];
  unsigned char DeviceAddr;

  // Get tuner deviece address.
  pTuner->GetDeviceAddr(pTuner, &DeviceAddr);

  if(tua9001i2cBusRead (pTuner, DeviceAddr, 0x70, i2cseq, 2) != TUA9001_TUNER_OK) goto error_status;           // get current register value

//  switch (i2cseq[1] & ~0x1f)
  // Note: Maybe the MSB byte is i2cseq[0]
  //       The original code looks like the MSB byte is i2cseq[1]
  // Note: ~0x1f = 0xffffffe0, not 0xe0 --> i2cseq[0] & ~0x1f result is wrong.
  switch (i2cseq[0] & 0xe0)
     {
     case 0x80: o_tunerState [0] = TUA9001_IDLE;  break;
     case 0x40: o_tunerState [0] = TUA9001_RX;    break;
     case 0x20: o_tunerState [0] = TUA9001_STANDBY;    break;
	 default: goto error_status; break;
     }
 
  return TUA9001_TUNER_OK;


error_status:
  return TUA9001_TUNER_ERR;
}
*/

/**
 * Get active input
 * @param o_activeInput active input info
 * @retval TUA9001_TUNER_OK No error
 * @retval TUA9001_TUNER_ERR Error
*/
/* 
int getActiveInputTua9001 (TUNER_MODULE *pTuner, tua9001tunerActiveInput_t *o_activeInput)
{
  char i2cseq[2];
  unsigned char DeviceAddr;

  // Get tuner deviece address.
  pTuner->GetDeviceAddr(pTuner, &DeviceAddr);

  if(tua9001i2cBusRead (pTuner, DeviceAddr, 0x71, i2cseq, 2) != TUA9001_TUNER_OK) goto error_status;           // get current register value

//  switch (i2cseq[1] & ~0x0f)
  // Note: Maybe the MSB byte is i2cseq[0]
  //       The original code looks like the MSB byte is i2cseq[1]
  // Note: ~0x0f = 0xfffffff0, not 0xf0 --> i2cseq[0] & ~0x0f result is wrong.
  switch (i2cseq[0] & 0xf0)
     {
     case 0x80: o_activeInput [0] = TUA9001_L_INPUT_ACTIVE;   break;
     case 0x20: o_activeInput [0] = TUA9001_UHF_INPUT_ACTIVE; break;
     case 0x10: o_activeInput [0] = TUA9001_VHF_INPUT_ACTIVE; break;
	 default: goto error_status; break;
     }
 
  return TUA9001_TUNER_OK;


error_status:
  return TUA9001_TUNER_ERR;
}
*/

/**
 * Get baseband gain value
 * @param o_basebandGain  baseband gain value
 * @retval TUA9001_TUNER_OK No error
 * @retval TUA9001_TUNER_ERR Error
*/

int getBasebandGainTua9001 (TUNER_MODULE *pTuner, char *o_basebandGain)
{
  char i2cseq[2];
  unsigned char DeviceAddr;

  // Get tuner deviece address.
  pTuner->GetDeviceAddr(pTuner, &DeviceAddr);

  if(tua9001i2cBusRead (pTuner, DeviceAddr, 0x72, i2cseq, 2) != TUA9001_TUNER_OK) goto error_status;           /* get current register value */
//  o_basebandGain [0] = i2cseq [1];
  // Note: Maybe the MSB byte is i2cseq[0]
  //       The original code looks like the MSB byte is i2cseq[1]
  o_basebandGain [0] = i2cseq [0];
 
  return TUA9001_TUNER_OK;


error_status:
  return TUA9001_TUNER_ERR;
}



