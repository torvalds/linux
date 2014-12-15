//------------------------------------------------------------------------------
// Copyright © 2007, Silicon Image, Inc.  All rights reserved.
//
// No part of this work may be reproduced, modified, distributed, transmitted,
// transcribed, or translated into any language or computer format, in any form
// or by any means without written permission of: Silicon Image, Inc.,
// 1060 East Arques Avenue, Sunnyvale, California 94085
//------------------------------------------------------------------------------
#include <local_types.h>
#include <config.h>
#include <hal.h>


//------------------------------------------------------------------------------
// Function: DecodeRegisterAddress
// Description: Generate I2C slave address and offset based on register address.
//              Register address format is composed of 2 uint8_ts, Page Addres and Offset.
//              Page Address uint8_t is mapped int I2C slave address.
//              Offset is used as is.
//------------------------------------------------------------------------------
static void DecodeRegisterAddress(uint16_t regAddr, uint8_t* slaveID, uint8_t* offset)
{
    uint8_t page;
    
    page = (uint8_t) (regAddr >> 8);

	switch (page)
	{
	    case (0):
	        *slaveID = CONF__I2C_SLAVE_PAGE_0;
			break;
	    case (1):
	        *slaveID = CONF__I2C_SLAVE_PAGE_1;
			break;
	    case (8):
	        *slaveID = CONF__I2C_SLAVE_PAGE_8;
			break;		   
		case (9):
	        *slaveID = CONF__I2C_SLAVE_PAGE_9;
			break;
		case (0x0A):
	        *slaveID = CONF__I2C_SLAVE_PAGE_A;
			break;
		case (0x0C):
	        *slaveID = CONF__I2C_SLAVE_PAGE_C;
			break;


	}
    *offset = (uint8_t) (regAddr & 0xFF);
}



//------------------------------------------------------------------------------
// Function: RegisterRead
// Description: Read a one uint8_t register.
//              The register address parameter is translated into an I2C slave address and offset.
//              The I2C slave address and offset are used to perform an I2C read operation.
//------------------------------------------------------------------------------
uint8_t RegisterRead(uint16_t regAddr)
{
    uint8_t slaveID, offset;
    uint8_t aByte;

    DecodeRegisterAddress(regAddr, &slaveID, &offset);

    aByte = I2C_ReadByte(slaveID, offset);

    return (aByte);
}



//------------------------------------------------------------------------------
// Function: RegisterWrite
// Description: Write a one uint8_t register.
//              The register address parameter is translated into an I2C slave address and offset.
//              The I2C slave address and offset are used to perform an I2C write operation.
//------------------------------------------------------------------------------
void RegisterWrite(uint16_t regAddr, uint8_t value)
{
    uint8_t slaveID, offset;

    DecodeRegisterAddress(regAddr, &slaveID, &offset);

    I2C_WriteByte(slaveID, offset, value);
}



//------------------------------------------------------------------------------
// Function: RegisterModify
// Description: Modify a one uint8_t register under mask.
//              The register address parameter is translated into an I2C slave address and offset.
//              The I2C slave address and offset are used to perform I2C read and write operations.
//
//              All bits specified in the mask are set in the register according to the value specified.
//              A mask of 0x00 does not change any bits.
//              A mask of 0xFF is the same a writing a uint8_t - all bits are set to the value given.
//              When only some buts in the mask are set, only those bits are changed to the values given.
//------------------------------------------------------------------------------
void RegisterModify(uint16_t regAddr, uint8_t mask, uint8_t value)
{
    uint8_t slaveID, offset;
    uint8_t aByte;

    DecodeRegisterAddress(regAddr, &slaveID, &offset);

    aByte = I2C_ReadByte(slaveID, offset);

    aByte &= (~mask);        //first clear all bits in mask
    aByte |= (mask & value); //then set bits from value

    I2C_WriteByte(slaveID, offset, aByte);
}



//------------------------------------------------------------------------------
// Function: RegisterBitToggle
// Description: Toggle a bit or bits in a register
//              The register address parameter is translated into an I2C slave address and offset.
//              The I2C slave address and offset are used to perform  I2C read and write operations.
//
//              All bits specified in the mask are first set and then cleared in the register.
//              This is a common operation for toggling a bit manually.
//------------------------------------------------------------------------------
void RegisterBitToggle(uint16_t regAddr, uint8_t mask)
{
    uint8_t slaveID, offset;
    uint8_t aByte;

    DecodeRegisterAddress(regAddr, &slaveID, &offset);

    aByte = I2C_ReadByte(slaveID, offset);

    aByte |=  mask;  //first set the bits in mask
    I2C_WriteByte(slaveID, offset, aByte);    //write register with bits set

    aByte &= ~mask;  //then clear the bits in mask
    I2C_WriteByte(slaveID, offset, aByte);  //write register with bits clear
}



//------------------------------------------------------------------------------
// Function: RegisterReadBlock
// Description: Read a block of registers starting with the specified register.
//              The register address parameter is translated into an I2C slave address and offset.
//              The block of data uint8_ts is read from the I2C slave address and offset.
//------------------------------------------------------------------------------
void RegisterReadBlock(uint16_t regAddr, uint8_t* buffer, uint8_t length)
{
    uint8_t slaveID, offset;

    DecodeRegisterAddress(regAddr, &slaveID, &offset);

    I2C_ReadBlock(slaveID, offset, buffer, length);
}



//------------------------------------------------------------------------------
// Function: RegisterWriteBlock
// Description: Write a block of registers starting with the specified register.
//              The register address parameter is translated into an I2C slave address and offset.
//              The block of data uint8_ts is written to the I2C slave address and offset.
//------------------------------------------------------------------------------
void RegisterWriteBlock(uint16_t regAddr, uint8_t* buffer, uint8_t length)
{
    uint8_t slaveID, offset;

    DecodeRegisterAddress(regAddr, &slaveID, &offset);

    I2C_WriteBlock(slaveID, offset, buffer, length);
}
