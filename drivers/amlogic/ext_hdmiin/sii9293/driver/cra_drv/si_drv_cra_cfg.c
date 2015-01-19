//***************************************************************************
//!file     si_drv_cra_cfg.c
//!brief    Silicon Image 5293 Starter Kit Firmware CRA configuration data.
//
// No part of this work may be reproduced, modified, distributed, 
// transmitted, transcribed, or translated into any language or computer 
// format, in any form or by any means without written permission of 
// Silicon Image, Inc., 1140 East Arques Avenue, Sunnyvale, California 94085
//
// Copyright 2008-2013, Silicon Image, Inc.  All rights reserved.
//***************************************************************************/

#include "si_cra.h"
#include "si_drv_cra_cfg.h"


// Index to this array is the virtual page number in the MSB of the REG_xxx address values
// Indexed with siiRegPageIndex_t value shifted right 8 bits
// DEV_PAGE values must correspond to the order specified in the SiiRegPageIndex_t enum list
pageConfig_t    g_addrDescriptor[SII_CRA_MAX_DEVICE_INSTANCES][SII_CRA_DEVICE_PAGE_COUNT] =
{
#if (FPGA_BUILD_NEW == 1)

    // Instance 0
    {
    { DEV_I2C_2,        DEV_PAGE_PP_0   },    // System Control and Status
    { DEV_I2C_2,        DEV_PAGE_PP_1   },    // MHL
    { DEV_I2C_2,        DEV_PAGE_PP_2   },    // System Control and Status
    { DEV_I2C_2,        DEV_PAGE_PP_3   },    // not used
    { DEV_I2C_2,        DEV_PAGE_PP_4   },    // not used
    { DEV_I2C_2,        DEV_PAGE_PP_5   },    // not used
    { DEV_I2C_2,        DEV_PAGE_PP_6   },    // not used
    { DEV_I2C_2,        DEV_PAGE_PP_7   },    // not used
    { DEV_I2C_2,        DEV_PAGE_PP_8   },    // CPI
    { DEV_I2C_2,        DEV_PAGE_PP_9   },    // Video RGB2xvYCC Reg and rvs control / VSI control and status
    { DEV_I2C_2,        DEV_PAGE_PP_A   },    // not used
    { DEV_I2C_2,        DEV_PAGE_PP_B   },    // not used
    { DEV_I2C_2,        DEV_PAGE_PP_C   },    // CBUS
    { DEV_I2C_2,        DEV_PAGE_HEAC   },    // HEAC
    { DEV_I2C_2,        DEV_PAGE_OSD    },    // OSD
    { DEV_I2C_2_OFFSET,   0x0000 + DEV_PAGE_AUDIO },    // Audio Extraction instance 1
    }
	
#else

    // Instance 0
    {
    { DEV_I2C_0,        DEV_PAGE_PP_0   },    // System Control and Status
    { DEV_I2C_0,        DEV_PAGE_PP_1   },    // MHL
    { DEV_I2C_0,        DEV_PAGE_PP_2   },    // System Control and Status
    { DEV_I2C_0,        DEV_PAGE_PP_3   },    // not used
    { DEV_I2C_0,        DEV_PAGE_PP_4   },    // not used
    { DEV_I2C_0,        DEV_PAGE_PP_5   },    // not used
    { DEV_I2C_0,        DEV_PAGE_PP_6   },    // not used
    { DEV_I2C_0,        DEV_PAGE_PP_7   },    // not used
    { DEV_I2C_0,        DEV_PAGE_PP_8   },    // CPI
    { DEV_I2C_0,        DEV_PAGE_PP_9   },    // Video RGB2xvYCC Reg and rvs control / VSI control and status
    { DEV_I2C_0,        DEV_PAGE_PP_A   },    // not used
    { DEV_I2C_0,        DEV_PAGE_PP_B   },    // not used
    { DEV_I2C_0,        DEV_PAGE_PP_C   },    // CBUS
    { DEV_I2C_0,        DEV_PAGE_HEAC   },    // HEAC
    { DEV_I2C_0,        DEV_PAGE_OSD    },    // OSD
    { DEV_I2C_0_OFFSET,   0x0000 + DEV_PAGE_AUDIO },    // Audio Extraction instance 1
    }

#endif
};

// Register addresses for re-assigning page base addresses
// These registers specify the I2C address that the SI device will
// respond to for the specific control register page
SiiReg_t g_siiRegPageBaseRegs [SII_CRA_DEVICE_PAGE_COUNT] =
{
    PP_PAGE | 0xFF,     // Device Base  - Cannot be reassigned
    PP_PAGE | 0xFF,     // Unused       - Cannot be reassigned
    PP_PAGE | 0x11,     // RX TMDS
    PP_PAGE | 0x44,     // IPV
    PP_PAGE | 0x12,     // RX TMDS
    PP_PAGE | 0x13,     // PA Pages 5,6,7 have special mapping requirements
    PP_PAGE | 0x13,     // PA
    PP_PAGE | 0x13,     // PA
    PP_PAGE | 0x18,     // CPI
    PP_PAGE | 0x19,     // NVRAM/GPIO
    PP_PAGE | 0x15,     // RX TMDS
    PP_PAGE | 0x14,     // TX TMDS
    PP_PAGE | 0x17,     // CBUS
    PP_PAGE | 0x16,     // HEAC
    PP_PAGE | 0x42,     // OSD
    PP_PAGE | 0x43,     // Audio extraction
};

// TODO:OEM - Add entries to reassign register page base addresses if needed
SiiReg_t g_siiRegPageBaseReassign [] =
{
//        PP_PAGE_3 | 0xFC,       // Example of changing default page 3 device ID (0xFA) to 0xFC

        0xFFFF      // End of reassignment list
};
