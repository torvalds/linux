//***************************************************************************
//!file     si_drv_cra_cfg.h
//!brief    Silicon Image Device CRA configuration data.
//
// No part of this work may be reproduced, modified, distributed, 
// transmitted, transcribed, or translated into any language or computer 
// format, in any form or by any means without written permission of 
// Silicon Image, Inc., 1140 East Arques Avenue, Sunnyvale, California 94085
//
// Copyright 2008-2013, Silicon Image, Inc.  All rights reserved.
//***************************************************************************/

#ifndef __SI_DRV_CRA_CFG_H__
#define __SI_DRV_CRA_CFG_H__

typedef enum _deviceBusTypes_t
{
    // The following four should remain together because they are used
    // as bus indices for the CraWriteBlockI2c and CraReadBlockI2c functions
    DEV_I2C_0,          // Main I2C bus
    DEV_I2C_1,          // Separate I2C bus
    DEV_I2C_2,          // Separate I2C bus
    DEV_I2C_3,          // Separate I2C bus
    DEV_I2C_0_OFFSET,     // Main I2C bus with register offset
    DEV_I2C_2_OFFSET,     // Separate I2C bus with register offset
    
    DEV_DDC_0,          // DDC bus for TX 0
    DEV_DDC_1,          // DDC bus for TX 1

    DEV_PARALLEL,       // Parallel interface
} deviceBusTypes_t;

// Actual I2C page addresses for the various devices
// TODO:OEM - If a device ID has been reassigned, update it here AND in
//            the g_siiRegPageBaseReassign[] array
typedef enum _devicePageIds_t
{
    DEV_PAGE_PP_0       = (0x64),
    DEV_PAGE_PP_1       = (0xD0),
    DEV_PAGE_PP_2       = (0x68),
    DEV_PAGE_PP_3       = (0x76),
    DEV_PAGE_PP_4       = (0x7E),
    DEV_PAGE_PP_5       = (0x50),
    DEV_PAGE_PP_6       = (0x52),
    DEV_PAGE_PP_7       = (0x54),
    DEV_PAGE_PP_8       = (0x80),
    DEV_PAGE_PP_9       = (0xE0),
    DEV_PAGE_PP_A       = (0x64),
    DEV_PAGE_PP_B       = (0x90),
    DEV_PAGE_PP_C       = (0xC0),
    DEV_PAGE_HEAC       = (0xD0),
    DEV_PAGE_OSD         = (0xF0),
    DEV_PAGE_AUDIO     = (0x30),
} devicePageIds_t;

// Index into pageConfig_t array (shifted left by 8)
typedef enum _SiiRegPageIndex_t
{
    PP_PAGE               = 0x0000,   // System Control and Status
    PP_PAGE_1           = 0x0100,   // MHL Registers
    PP_PAGE_2           = 0x0200,   // System Control and Status
    PP_PAGE_3           = 0x0300,   // Evita
    PP_PAGE_4           = 0x0400,   // Evita
    PP_PAGE_5           = 0x0500,   // not used
    PP_PAGE_6           = 0x0600,   // not used
    PP_PAGE_7           = 0x0700,   // not used
    CPI_PAGE              = 0x0800,   // CEC bus interface registers for Rx
    PP_PAGE_9           = 0x0900,   // Video RGB2xvYCC Reg and rvs control / VSI control and status
    PP_PAGE_A           = 0x0A00,   // not used
    PP_PAGE_B           = 0x0B00,   // not used
    CBUS_PAGE           = 0x0C00,   // CBUS Registers
    PP_PAGE_HEAC     = 0x0D00,   // not used
    PP_PAGE_OSD       = 0x0E00,   // not used
    PP_PAGE_AUDIO   = 0x0F00,   // not used
} SiiRegPageIndex_t;

#define SII_CRA_MAX_DEVICE_INSTANCES   1  // Maximum size of instance dimension of address descriptor array
#define SII_CRA_DEVICE_PAGE_COUNT       16  // Number of entries in pageConfig_t array

typedef struct pageConfig
{
    deviceBusTypes_t    busType;    // I2C, Parallel
    char               address;             // I2C DEV ID, parallel mem offset
} pageConfig_t;

#endif  // __SI_DRV_CRA_CFG_H__
