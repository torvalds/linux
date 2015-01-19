//***************************************************************************
//!file     si_drv_cra_internal.h
//!brief    Silicon Image CRA internal driver functions.
//***************************************************************************/
/*
 * SiIxxxx <Firmware or Driver>
 *
 * Copyright (C) 2011 Silicon Image Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed .as is. WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
 * PURPOSE.  See the
 * GNU General Public License for more details.
*/


#ifndef __SI_CRA_DRV_INTERNAL_H__
#define __SI_CRA_DRV_INTERNAL_H__

//-------------------------------------------------------------------------------
// CRA Enums and manifest constants
//-------------------------------------------------------------------------------

typedef enum _SiiDrvCraError_t
{
    RESULT_CRA_SUCCESS,             // Success result code
    RESULT_CRA_FAIL,                // General Failure result code
    RESULT_CRA_INVALID_PARAMETER,   // One or more invalid parameters
} SiiDrvCraError_t;

//------------------------------------------------------------------------------
//  CRA Driver Instance Data
//------------------------------------------------------------------------------

typedef struct _CraInstanceData_t
{
    int                 structVersion;
    int                 instanceIndex;
    SiiDrvCraError_t    lastResultCode;     // Contains the result of the last API function called
    uint16_t            statusFlags;
}	CraInstanceData_t;

extern CraInstanceData_t craInstance;

#ifdef __KERNEL__

/**
 * Status return values returned by platform specific CRA layer support functions.
 */
typedef enum _SiiPlatformStatus_t
{
    PLATFORM_SUCCESS,
    PLATFORM_FAIL,              // General fail
    PLATFORM_INVALID_PARAMETER,
    PLATFORM_I2C_READ_FAIL,
    PLATFORM_I2C_WRITE_FAIL,
    PLATFORM_MMIO_READ_FAIL,
    PLATFORM_MMIO_WRITE_FAIL,
} SiiPlatformStatus_t;



typedef struct
{
	uint16_t addr;							/**< slave address */
	uint16_t cmdFlags;						/**< flags defining message actions */
#define SII_MI2C_TEN	0x0010				/**< set for ten bit chip address; cleared
 	 	 	 	 	 	 	 	 	 	 	 	 otherwise */
#define SII_MI2C_RD		0x0001				/**< set for read data operation; cleared for
 	 	 	 	 	 	 	 	 	 	 	 	 write operations */
#define SII_MI2C_APPEND_NEXT_MSG	0x0002	/**< append the buffer from the next message to
												 this message */
	uint16_t len;							/**< buffer length */
	uint8_t *pBuf;							/**< pointer to input (for write operations)
	 	 	 	 	 	 	 	 	 			 or output (for read operations) buffer */
} SiiI2cMsg_t;

#define MAX_I2C_TRANSFER	255		/* Maximum # of bytes in a single I2c transfer */
#define MAX_I2C_MESSAGES	3		/* Maximum # of I2c message segments supported
									   by SiiMasterI2cTransfer					   */

/*****************************************************************************/
/**
 * @brief CRA platform interface function called to perform I2C bus transfers.
 *
 * This function is called by the Common Register Access (CRA) driver when it
 * needs to perform a transfer to/from register(s) that are accessed over an
 * I2C bus.  The function executes msgNum messages (I2C sub-transactions).
 * Each message is an I2C transaction starting with a start condition.  The
 * stop condition is sent only after the last message is executed.
 *
 *  @param[in]		busIndex		Selects the physical I2C bus to be used.
 *  								Can be ignored if only one I2C bus in system.
 *  @param[in]		pMsgs			Array of I2C messages to be sent..
 *  @param[in]		msgNum			Value to write to register.
 *
 *****************************************************************************/
SiiPlatformStatus_t SiiMasterI2cTransfer(deviceBusTypes_t busIndex,
										 SiiI2cMsg_t *pMsgs, uint8_t msgNum);
#endif

#endif // __SI_CRA_DRV_INTERNAL_H__
