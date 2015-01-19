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


//***************************************************************************
//!file     si_cra.c
//!brief    Silicon Image Device register I/O support.
//***************************************************************************/

#include "si_common.h"
#include "si_cra.h"
#include "si_drv_cra_cfg.h"
#include "si_cra_internal.h"
#if !defined(__KERNEL__)
#include "si_i2c.h"
#endif

static uint8_t      l_pageInstance[SII_CRA_DEVICE_PAGE_COUNT] = {0};
extern pageConfig_t g_addrDescriptor[SII_CRA_MAX_DEVICE_INSTANCES][SII_CRA_DEVICE_PAGE_COUNT];
extern SiiReg_t     g_siiRegPageBaseReassign [];
extern SiiReg_t     g_siiRegPageBaseRegs[SII_CRA_DEVICE_PAGE_COUNT];

CraInstanceData_t craInstance =
{
    0,                          // structVersion
    0,                          // instanceIndex
    SII_SUCCESS,                // lastResultCode
    0,                          // statusFlags
};

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

#if defined(__KERNEL__)

// Translate SiiPlatformStatus_t return codes into SiiResultCodes_t status codes.
static SiiResultCodes_t TranslatePlatformStatus(SiiPlatformStatus_t platformStatus)
{
	switch(platformStatus)
	{
		case PLATFORM_SUCCESS:
			return SII_SUCCESS;

		case PLATFORM_FAIL:
			return SII_ERR_FAIL;

		case PLATFORM_INVALID_PARAMETER:
			return SII_ERR_INVALID_PARAMETER;

		case PLATFORM_I2C_READ_FAIL:
		case PLATFORM_I2C_WRITE_FAIL:
		default:
			return SII_ERR_FAIL;
	}
}

SiiResultCodes_t CraReadBlockI2c (deviceBusTypes_t busIndex, uint8_t deviceId, uint8_t regAddr, uint8_t *pBuffer, uint16_t count )
{
	SiiI2cMsg_t			msgs[2];
	SiiPlatformStatus_t	platformStatus;
    SiiResultCodes_t	status = SII_ERR_FAIL;
    int retryTimes = 3;

    msgs[0].addr = deviceId;
    msgs[0].cmdFlags = 0;
    msgs[0].len = 1;
    msgs[0].pBuf = &regAddr;

    msgs[1].addr = deviceId;
    msgs[1].cmdFlags = SII_MI2C_RD;
    msgs[1].len = count;
    msgs[1].pBuf = pBuffer;

    do
    {
        platformStatus = SiiMasterI2cTransfer(busIndex, msgs, 2);
        if (!platformStatus)
            break;
    }while(retryTimes--);

    if(platformStatus)
    {
        memset(pBuffer,0xFF,count);
    }

	status = TranslatePlatformStatus(platformStatus);
    return( status );
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

SiiResultCodes_t CraWriteBlockI2c (deviceBusTypes_t busIndex, uint8_t deviceId, uint8_t regAddr, const uint8_t *pBuffer, uint16_t count )
{
	SiiI2cMsg_t			msgs[2];
	SiiPlatformStatus_t	platformStatus;
    SiiResultCodes_t	status = SII_ERR_FAIL;


    msgs[0].addr = deviceId;
    msgs[0].cmdFlags = SII_MI2C_APPEND_NEXT_MSG;
    msgs[0].len = 1;
    msgs[0].pBuf = &regAddr;

    msgs[1].addr = 0;
    msgs[1].cmdFlags = 0;
    msgs[1].len = count;
    msgs[1].pBuf = (uint8_t*)pBuffer;	// cast gets rid of const warning

	platformStatus = SiiMasterI2cTransfer(busIndex, msgs, 2);

	status = TranslatePlatformStatus(platformStatus);

    return( status );
}

#else

static SiiResultCodes_t CraReadBlockI2c (int_t busIndex, uint8_t deviceId, uint8_t regAddr, uint8_t *pBuffer, uint16_t count )
{
    SiiResultCodes_t status = SII_ERR_FAIL;

    do {
        // Send the register address and stop bit
        if ( I2cSendStart( busIndex, deviceId, &regAddr, 1, FALSE ) != PLATFORM_SUCCESS )
        {
            break;
        }
        // Receive the requested number of data bytes
        if ( I2cReceiveStart( busIndex, deviceId, pBuffer, count, TRUE ) != PLATFORM_SUCCESS )
        {
            break;
        }

        status = SII_SUCCESS;
    } while (0);

    return( status );
}


//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

static SiiResultCodes_t CraWriteBlockI2c ( int_t busIndex, uint8_t deviceId, uint8_t regAddr, const uint8_t *pBuffer, uint16_t count )
{
    SiiResultCodes_t status = SII_ERR_FAIL;

    do {
        // Send the register address but don't send stop
        if ( I2cSendStart( busIndex, deviceId, &regAddr, 1, FALSE ) != PLATFORM_SUCCESS )
        {
            break;
        }
        // Send the remainder of the data, if necessary
        if ( I2cSendContinue( busIndex, pBuffer, count, TRUE ) != PLATFORM_SUCCESS )
        {
            break;
        }
        status = SII_SUCCESS;
    } while (0);

    return( status );
}
#endif //_KERNEL_

//------------------------------------------------------------------------------
// Function:    SiiCraInitialize
// Description: Initialize the CRA page instance array and perform any register
//              page base address reassignments required.
// Parameters:  none
// Returns:     None
//------------------------------------------------------------------------------

bool_t SiiCraInitialize ( void )
{
    uint8_t i, index;
    craInstance.lastResultCode = RESULT_CRA_SUCCESS;

    for (i = 0; i < SII_CRA_DEVICE_PAGE_COUNT; i++)
    {
        l_pageInstance[i] = 0;
    }

    // Perform any register page base address reassignments
    i = 0;
    while ( g_siiRegPageBaseReassign[ i] != 0xFFFF )
    {
        index = g_siiRegPageBaseReassign[ i] >> 8;
        if (( index < SII_CRA_DEVICE_PAGE_COUNT ) && ( g_siiRegPageBaseRegs[ index] != 0xFF))
        {
            // The page base registers allow reassignment of the
            // I2C device ID for almost all device register pages.
            SiiRegWrite( g_siiRegPageBaseRegs[ index], g_siiRegPageBaseReassign[ index] & 0x00FF );
        }
        else
        {
            craInstance.lastResultCode = SII_ERR_INVALID_PARAMETER;
            break;
        }
        i++;
    }

    return( craInstance.lastResultCode == RESULT_CRA_SUCCESS );
}

//------------------------------------------------------------------------------
// Function:    SiiCraGetLastResult
// Description: Returns the result of the last call to a CRA driver function.
// Parameters:  none.
// Returns:     Returns the result of the last call to a CRA driver function
//------------------------------------------------------------------------------

SiiResultCodes_t SiiCraGetLastResult ( void )
{
    return( craInstance.lastResultCode );
}

//------------------------------------------------------------------------------
// Function:    SiiRegInstanceSet
// Description: Sets the instance for subsequent register accesses.  The register
//              access functions use this value as an instance index of the multi-
//              dimensional virtual address lookup table.
// Parameters:  newInstance - new value for instance axis of virtual address table.
// Returns:     None
//------------------------------------------------------------------------------

bool_t SiiRegInstanceSet ( SiiReg_t virtualAddress, uint8_t newInstance )
{
    uint8_t va = virtualAddress >> 8;

    craInstance.lastResultCode = RESULT_CRA_SUCCESS;
    if (( va < SII_CRA_DEVICE_PAGE_COUNT) && ( newInstance < SII_CRA_MAX_DEVICE_INSTANCES ))
    {
        l_pageInstance[ va ] = newInstance;
        return( TRUE );
    }

    craInstance.lastResultCode = SII_ERR_INVALID_PARAMETER;
    return( FALSE );
}

//------------------------------------------------------------------------------
// Function:    SiiRegReadBlock
// Description: Reads a block of data from sequential registers.
// Parameters:  regAddr - Sixteen bit register address, including device page.
// Returns:     None
//
// NOTE:        This function relies on the auto-increment model used by
//              Silicon Image devices.  Because of this, if a FIFO register
//              is encountered before the end of the requested count, the
//              data remaining from the count is read from the FIFO, NOT
//              from subsequent registers.
//------------------------------------------------------------------------------

void SiiRegReadBlock ( SiiReg_t virtualAddr, uint8_t *pBuffer, uint16_t count )
{
    uint8_t             regOffset = (uint8_t)virtualAddr;
    pageConfig_t        *pPage;
    SiiResultCodes_t    status = SII_ERR_FAIL;
    virtualAddr >>= 8;
    pPage = &g_addrDescriptor[l_pageInstance[virtualAddr]][virtualAddr];
    switch ( pPage->busType )
    {
        case DEV_I2C_0:
            status = CraReadBlockI2c( DEV_I2C_0, (uint8_t)pPage->address, regOffset, pBuffer, count );
            break;
        case DEV_I2C_2:
            status = CraReadBlockI2c( DEV_I2C_2, (uint8_t)pPage->address, regOffset, pBuffer, count );
            break;
        case DEV_I2C_0_OFFSET:
            status = CraReadBlockI2c( DEV_I2C_0, (uint8_t)pPage->address, regOffset + (uint8_t)(pPage->address >> 8), pBuffer, count );
            break;
        case DEV_I2C_2_OFFSET:
            status = CraReadBlockI2c( DEV_I2C_2, (uint8_t)pPage->address, regOffset + (uint8_t)(pPage->address >> 8), pBuffer, count );
            break;

        default:
            break;
    }
}

//------------------------------------------------------------------------------
// Function:    SiiRegRead
// Description: Read a one byte register.
//              The register address parameter is translated into an I2C slave
//              address and offset. The I2C slave address and offset are used
//              to perform an I2C read operation.
//------------------------------------------------------------------------------

uint8_t SiiRegRead ( SiiReg_t virtualAddr )
{
    uint8_t             value = 0xFF;
    uint8_t             regOffset = (uint8_t)virtualAddr;
    pageConfig_t        *pPage;
    SiiResultCodes_t    status = SII_ERR_FAIL;

    virtualAddr >>= 8;
    pPage = &g_addrDescriptor[l_pageInstance[virtualAddr]][virtualAddr];

    switch ( pPage->busType )
    {
        case DEV_I2C_0:
            status = CraReadBlockI2c( DEV_I2C_0, (uint8_t)pPage->address, regOffset, &value, 1 );
            break;
        case DEV_I2C_2:
            status = CraReadBlockI2c( DEV_I2C_2, (uint8_t)pPage->address, regOffset, &value, 1 );
            break;
        case DEV_I2C_0_OFFSET:
            status = CraReadBlockI2c( DEV_I2C_0, (uint8_t)pPage->address, regOffset + (uint8_t)(pPage->address >> 8), &value, 1 );
            break;
        case DEV_I2C_2_OFFSET:
            status = CraReadBlockI2c( DEV_I2C_2, (uint8_t)pPage->address, regOffset + (uint8_t)(pPage->address >> 8), &value, 1 );
            break;

        default:
            break;
    }
    return( value );
}

//------------------------------------------------------------------------------
// Function:    SiiRegWriteBlock
// Description: Writes a block of data to sequential registers.
// Parameters:  regAddr - Sixteen bit register address, including device page.
// Returns:     None
//
// NOTE:        This function relies on the auto-increment model used by
//              Silicon Image devices.  Because of this, if a FIFO register
//              is encountered before the end of the requested count, the
//              data remaining from the count is read from the FIFO, NOT
//              from subsequent registers.
//------------------------------------------------------------------------------

void SiiRegWriteBlock ( SiiReg_t virtualAddr, const uint8_t *pBuffer, uint16_t count )
{
    uint8_t             regOffset = (uint8_t)virtualAddr;
    pageConfig_t        *pPage;
    SiiResultCodes_t    status = SII_ERR_FAIL;

    virtualAddr >>= 8;
    pPage = &g_addrDescriptor[l_pageInstance[virtualAddr]][virtualAddr];

    switch ( pPage->busType )
    {
        case DEV_I2C_0:
            status = CraWriteBlockI2c( DEV_I2C_0, (uint8_t)pPage->address, regOffset, pBuffer, count );
            break;
        case DEV_I2C_2:
            status = CraWriteBlockI2c( DEV_I2C_2, (uint8_t)pPage->address, regOffset, pBuffer, count );
            break;
        case DEV_I2C_0_OFFSET:
            status = CraWriteBlockI2c( DEV_I2C_0, (uint8_t)pPage->address, regOffset + (uint8_t)(pPage->address >> 8), pBuffer, count );
            break;
        case DEV_I2C_2_OFFSET:
            status = CraWriteBlockI2c( DEV_I2C_2, (uint8_t)pPage->address, regOffset + (uint8_t)(pPage->address >> 8), pBuffer, count );
            break;

        default:
            break;
    }
}

/*****************************************************************************/
/**
 *  @brief		Read a value of word from the specified register
 *
 *  @param[in]		reg_addr		register address
 *
 *  @return	Word read from the specified register
 *
 *****************************************************************************/
uint16_t SiiRegReadWord(SiiReg_t reg_addr)
{
	uint8_t buffer[2];
	SiiRegReadBlock(reg_addr, buffer, 2);
	return buffer[0] | (((uint16_t) buffer[1]) << 8);
}


/*****************************************************************************/
/**
 *  @brief		Write a value of word into the specified register
 *
 *  @param[in]		reg_addr		register address
 *  @param[in]		value		value to write to register
 *
 *****************************************************************************/
void SiiRegWriteWord(SiiReg_t reg_addr, uint16_t value)
{
	uint8_t buffer[2];
	buffer[0] = (uint8_t) value;
	buffer[1] = (uint8_t) (value >> 8);
	SiiRegWriteBlock(reg_addr, buffer, 2);
}


//------------------------------------------------------------------------------
// Function:    SiiRegWrite
// Description: Write a one byte register.
//              The register address parameter is translated into an I2C slave
//              address and offset. The I2C slave address and offset are used
//              to perform an I2C write operation.
//------------------------------------------------------------------------------

void SiiRegWrite ( SiiReg_t virtualAddr, uint8_t value )
{
    uint8_t             regOffset = (uint8_t)virtualAddr;
    pageConfig_t        *pPage;

    SiiResultCodes_t    status = SII_ERR_FAIL;
    virtualAddr >>= 8;
    pPage = &g_addrDescriptor[l_pageInstance[virtualAddr]][virtualAddr];

    switch ( pPage->busType )
    {
        case DEV_I2C_0:
            status = CraWriteBlockI2c( DEV_I2C_0, (uint8_t)pPage->address, regOffset, &value, 1 );
            break;
        case DEV_I2C_2:
            status = CraWriteBlockI2c( DEV_I2C_2, (uint8_t)pPage->address, regOffset, &value, 1 );
            break;
        case DEV_I2C_0_OFFSET:
            status = CraWriteBlockI2c( DEV_I2C_0, (uint8_t)pPage->address, regOffset + (uint8_t)(pPage->address >> 8), &value, 1 );
            break;
        case DEV_I2C_2_OFFSET:
            status = CraWriteBlockI2c( DEV_I2C_2, (uint8_t)pPage->address, regOffset + (uint8_t)(pPage->address >> 8), &value, 1 );
            break;

        default:
            break;
    }
}

//------------------------------------------------------------------------------
// Function:    SiiRegModify
// Description: Reads the register, performs an AND function on the data using
//              the mask parameter, and an OR function on the data using the
//              value ANDed with the mask. The result is then written to the
//              device register specified in the regAddr parameter.
// Parameters:  regAddr - Sixteen bit register address, including device page.
//              mask    - Eight bit mask
//              value   - Eight bit data to be written, combined with mask.
// Returns:     None
//------------------------------------------------------------------------------

void SiiRegModify ( SiiReg_t virtualAddr, uint8_t mask, uint8_t value)
{
    uint8_t aByte;

    aByte = SiiRegRead( virtualAddr );
    aByte &= (~mask);                       // first clear all bits in mask
    aByte |= (mask & value);                // then set bits from value
    SiiRegWrite( virtualAddr, aByte );
}

//------------------------------------------------------------------------------
// Function:    SiiRegBitsSet
// Description: Reads the register, sets the passed bits, and writes the
//              result back to the register.  All other bits are left untouched
// Parameters:  regAddr - Sixteen bit register address, including device page.
//              bits   - bit data to be written
// Returns:     None
//------------------------------------------------------------------------------

void SiiRegBitsSet ( SiiReg_t virtualAddr, uint8_t bitMask, bool_t setBits )
{
    uint8_t aByte;

    aByte = SiiRegRead( virtualAddr );
    aByte = (setBits) ? (aByte | bitMask) : (aByte & ~bitMask);
    SiiRegWrite( virtualAddr, aByte );
}

//------------------------------------------------------------------------------
// Function:    SiiRegBitsSetNew
// Description: Reads the register, sets or clears the specified bits, and
//              writes the result back to the register ONLY if it would change
//              the current register contents.
// Parameters:  regAddr - Sixteen bit register address, including device page.
//              bits   - bit data to be written
//              setBits- TRUE == set, FALSE == clear
// Returns:     None
//------------------------------------------------------------------------------

void SiiRegBitsSetNew ( SiiReg_t virtualAddr, uint8_t bitMask, bool_t setBits )
{
    uint8_t newByte, oldByte;

    oldByte = SiiRegRead( virtualAddr );
    newByte = (setBits) ? (oldByte | bitMask) : (oldByte & ~bitMask);
    if ( oldByte != newByte )
    {
        SiiRegWrite( virtualAddr, newByte );
    }
}
#if 0
//------------------------------------------------------------------------------
// Function:    SiiRegEdidReadBlock
// Description: Reads a block of data from EDID record over DDC link.
// Parameters:  segmentAddr - EDID segment address (16 bit), including device page;
//              offsetAddr  - Sixteen bit register address, including device page.
// Returns:     success flag
//
//------------------------------------------------------------------------------

void SiiRegEdidReadBlock ( SiiReg_t segmentAddr, SiiReg_t virtualAddr, uint8_t *pBuffer, uint16_t count )
{
    uint8_t             regOffset = (uint8_t)virtualAddr;
    pageConfig_t        *pPage;

    SiiResultCodes_t    status = SII_ERR_FAIL;

    if ((segmentAddr & 0xFF) != 0)  // Default segment #0 index should not be sent explicitly
    {
        regOffset = (uint8_t)segmentAddr;
        segmentAddr >>= 8;
        pPage = &g_addrDescriptor[l_pageInstance[segmentAddr]][segmentAddr];
        // Write the segment number to the EDID device ID, but don't send the stop
        I2cSendStart( pPage->busType, pPage->address, &regOffset, 1, FALSE );
    }

    // Read the actual EDID data
    regOffset = (uint8_t)virtualAddr;
    virtualAddr >>= 8;
    pPage = &g_addrDescriptor[l_pageInstance[virtualAddr]][virtualAddr];

    status = CraReadBlockI2c( pPage->busType, pPage->address, regOffset, pBuffer, count );

}
#endif
