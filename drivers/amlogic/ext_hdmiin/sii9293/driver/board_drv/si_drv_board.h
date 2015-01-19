//------------------------------------------------------------------------------
// Project: HDMI Repeater
// Copyright (C) 2002-2013, Silicon Image, Inc.  All rights reserved.
//
// No part of this work may be reproduced, modified, distributed, transmitted,
// transcribed, or translated into any language or computer format, in any form
// or by any means without written permission of: Silicon Image, Inc.,
// 1140 East Arques Avenue, Sunnyvale, California 94085
//------------------------------------------------------------------------------


#ifndef SII_DRV_BOARD_H
#define SII_DRV_BOARD_H

#include "si_common.h"

#if !defined(__KERNEL__)
#define GPIO_RST                            GPIO_ID_P0_1
#endif
#define GPIO_AUD_RST                        GPIO_ID_P0_2
#define GPIO_SW3_5                          GPIO_ID_P0_4
#define GPIO_SW3_6                          GPIO_ID_P0_5
#define GPIO_SW3_7                          GPIO_ID_P0_6
#define GPIO_SW3_8                          GPIO_ID_P0_7

#define GPIO_SW3_1                          GPIO_SW3_8
#define GPIO_SW3_2                          GPIO_SW3_7
#define GPIO_SW3_3                          GPIO_SW3_6
#define GPIO_SW3_4                          GPIO_SW3_5

enum
{
    LED_ID_GREEN = 0x00,
    LED_ID_AMBER = 0x01,
    LED_ID_2 = 0x02,
    LED_ID_3 = 0x03,
};

enum
{
    GPIO_ID_P0_0 = 0x00,
    GPIO_ID_P0_1,
    GPIO_ID_P0_2,
    GPIO_ID_P0_3,
    GPIO_ID_P0_4,
    GPIO_ID_P0_5,
    GPIO_ID_P0_6,
    GPIO_ID_P0_7,

    GPIO_ID_P2_0,
    GPIO_ID_P2_1, 
    GPIO_ID_P2_2,
    GPIO_ID_P2_3, 
    GPIO_ID_P2_4,
    GPIO_ID_P2_5, 
    GPIO_ID_P2_6,
    GPIO_ID_P2_7, 
};

bool_t SiiDrvBoardInit(void);

void SiiLedControl(uint8_t ledIndex, bool_t bOn);

void SiiGpioControl(uint8_t gpioIndex, bool_t bOn);

bool_t SiiGpioRead(uint8_t gpioIndex);

bool_t SiiSpdifEnableGet(void);

bool_t SiiTdmEnableGet(void);

#endif // SII_DRV_BOARD_H
