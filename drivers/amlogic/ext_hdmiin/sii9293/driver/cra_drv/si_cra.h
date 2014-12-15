//***************************************************************************
//!file     si_cra.h
//!brief    Silicon Image Device register I/O support.
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



#ifndef __SI_CRA_H__
#define __SI_CRA_H__

#include "si_common.h"
#include "si_drv_cra_cfg.h"

typedef uint16_t    SiiReg_t;

// Standard result codes are in the range of 0 - 4095
typedef enum _SiiResultCodes_t
{
    SII_SUCCESS      = 0,            // Success.
    SII_ERR_FAIL,                   // General failure.
    SII_ERR_INVALID_PARAMETER,      //
    SII_ERR_IN_USE,                  // Module already initialized.
    SII_ERR_NOT_AVAIL,               // Allocation of resources failed.
} SiiResultCodes_t;

#if defined(__KERNEL__)
SiiResultCodes_t CraReadBlockI2c (deviceBusTypes_t busIndex, uint8_t deviceId, uint8_t regAddr, uint8_t *pBuffer, uint16_t count );
SiiResultCodes_t CraWriteBlockI2c (deviceBusTypes_t busIndex, uint8_t deviceId, uint8_t regAddr, const uint8_t *pBuffer, uint16_t count );
#endif
bool_t SiiCraInitialize( void );
SiiResultCodes_t SiiCraGetLastResult( void );
bool_t SiiRegInstanceSet( SiiReg_t virtualAddress, uint8_t newInstance );

void    SiiRegReadBlock ( SiiReg_t virtualAddr, uint8_t *pBuffer, uint16_t count );
uint8_t SiiRegRead ( SiiReg_t virtualAddr );
void    SiiRegWriteBlock ( SiiReg_t virtualAddr, const uint8_t *pBuffer, uint16_t count );
void    SiiRegWrite ( SiiReg_t virtualAddr, uint8_t value );
uint16_t SiiRegReadWord(SiiReg_t reg_addr);
void SiiRegWriteWord(SiiReg_t reg_addr, uint16_t value);
void    SiiRegModify ( SiiReg_t virtualAddr, uint8_t mask, uint8_t value);
void    SiiRegBitsSet ( SiiReg_t virtualAddr, uint8_t bitMask, bool_t setBits );
void    SiiRegBitsSetNew ( SiiReg_t virtualAddr, uint8_t bitMask, bool_t setBits );

// Special purpose
void    SiiRegEdidReadBlock ( SiiReg_t segmentAddr, SiiReg_t virtualAddr, uint8_t *pBuffer, uint16_t count );

#endif  // __SI_CRA_H__
