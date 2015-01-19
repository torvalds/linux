//------------------------------------------------------------------------------
// Project: HDMI Repeater
// Copyright (C) 2002-2013, Silicon Image, Inc.  All rights reserved.
//
// No part of this work may be reproduced, modified, distributed, transmitted,
// transcribed, or translated into any language or computer format, in any form
// or by any means without written permission of: Silicon Image, Inc.,
// 1140 East Arques Avenue, Sunnyvale, California 94085
//------------------------------------------------------------------------------

#include "si_drv_board.h"
#include "si_drv_cra_cfg.h"

#if (FPGA_BUILD_NEW == 1)
    // # define
#else
#if !defined(__KERNEL__)
#include "si_starterkit.h"
#endif
#include "si_cs4384_registers.h"
#endif

bool_t bSpdif = false;
bool_t bTdm = false;
bool_t bExternalTxHdmi = false;


/*****************************************************************************/
/**
 *  @brief		Board Initialization
 *
 *  @return	Status
 *  @retval	true		Success
 *  @retval	false		Failure
 *
 *****************************************************************************/
 bool_t SiiDrvBoardInit(void)
{
    bool_t  success = true;


    // Starter Kit Board
    bool_t gpio_sw3_1 = false;
    bool_t gpio_sw3_2 = false;
    bool_t gpio_sw3_3 = false;
    bool_t gpio_sw3_4 = false;

    gpio_sw3_1 = !SiiGpioRead(GPIO_SW3_1);
    gpio_sw3_2 = !SiiGpioRead(GPIO_SW3_2);
    gpio_sw3_3 = !SiiGpioRead(GPIO_SW3_3);
    gpio_sw3_4 = !SiiGpioRead(GPIO_SW3_4);

    DEBUG_PRINT( MSG_ALWAYS, "GPIO SW3 BIT 1 (TDM-ON / I2S-OFF): %s\n", gpio_sw3_1 ? "On" : "Off" );
    DEBUG_PRINT( MSG_ALWAYS, "GPIO SW3 BIT 2 (S/PDIF): %s\n", gpio_sw3_2 ? "On" : "Off" );
    DEBUG_PRINT( MSG_ALWAYS, "GPIO SW3 BIT 3: %s\n", gpio_sw3_3 ? "On" : "Off" );
    DEBUG_PRINT( MSG_ALWAYS, "GPIO SW3 BIT 4: %s\n", gpio_sw3_4 ? "On" : "Off" );

    bSpdif = gpio_sw3_2;
    bTdm = gpio_sw3_1;


#if 0
    if ( bSpdif )
    {
        // Pass through, no codec used, do nothing
    }
    else if (bTdm)
    {
        // Use codec for TDM 8 channels
        SiiRegWrite( CS_4384_CTRL1, 0x80 );
        SiiRegWrite( CS_4384_CTRL2, 0xc3 );
        SiiRegWrite( CS_4384_CTRL3, 0x00 );
    }
    else
    {
        // Use codec for I2S 2 channels
        SiiRegWrite( CS_4384_CTRL1, 0x80 );
        SiiRegWrite( CS_4384_CTRL2, 0x13 );
        SiiRegWrite( CS_4384_CTRL3, 0x00 );
    }
#endif

    return( success );
}

/*****************************************************************************/
/**
 *  @brief		Board LED Control
 *  @param[in]      ledIndex	index to LED to be controlled
 *  @param[in]      bOn		true to turn on LED; false to turn off
 *
 *****************************************************************************/
void SiiLedControl(uint8_t ledIndex, bool_t bOn)
{
#if !defined(__KERNEL__)
    SkPlatformStarterKitLedControl(ledIndex, bOn);
#endif
}

/*****************************************************************************/
/**
 *  @brief		Board GPIO Control
 *  @param[in]      gpioIndex		index to GPIO to be controlled
 *  @param[in]      bOn			true to turn on; false to turn off
 *
 *****************************************************************************/
void SiiGpioControl(uint8_t gpioIndex, bool_t bOn)
{
#if !defined(__KERNEL__)
    SkPlatformStarterKitGpioControl(gpioIndex, bOn);
#else
    HalGpioSetPin(gpioIndex, bOn);
#endif
}

/*****************************************************************************/
/**
 *  @brief		Board GPIO Read
 *  @param[in]      gpioIndex		index to GPIO to be read
 *
 *  @return	Status
 *  @retval	true		On
 *  @retval	false		Off
 *
 *****************************************************************************/
bool_t SiiGpioRead(uint8_t gpioIndex)
{
#if !defined(__KERNEL__)
    return SkPlatformStarterKitGpioRead(gpioIndex);
#else
    return true;
#endif
}

/*****************************************************************************/
/**
 *  @brief		Board SPDIF Switch Status
 *
 *  @return	Status
 *  @retval	true		On
 *  @retval	false		Off
 *
 *****************************************************************************/
bool_t SiiSpdifEnableGet()
{
    //return bSpdif;
    return true;
}

/*****************************************************************************/
/**
 *  @brief		Board TDM Switch Status
 *
 *  @return	Status
 *  @retval	true		On
 *  @retval	false		Off
 *
 *****************************************************************************/
bool_t SiiTdmEnableGet()
{
    return bTdm;
}