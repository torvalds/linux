/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Amlogic A1 PLL Clock Controller internals
 *
 * Copyright (c) 2019 Amlogic, Inc. All rights reserved.
 * Author: Jian Hu <jian.hu@amlogic.com>
 *
 * Copyright (c) 2023, SberDevices. All Rights Reserved.
 * Author: Dmitry Rokosov <ddrokosov@sberdevices.ru>
 */

#ifndef __A1_PLL_H
#define __A1_PLL_H

#include "clk-pll.h"

/* PLL register offset */
#define ANACTRL_FIXPLL_CTRL0	0x0
#define ANACTRL_FIXPLL_CTRL1	0x4
#define ANACTRL_FIXPLL_STS	0x14
#define ANACTRL_HIFIPLL_CTRL0	0xc0
#define ANACTRL_HIFIPLL_CTRL1	0xc4
#define ANACTRL_HIFIPLL_CTRL2	0xc8
#define ANACTRL_HIFIPLL_CTRL3	0xcc
#define ANACTRL_HIFIPLL_CTRL4	0xd0
#define ANACTRL_HIFIPLL_STS	0xd4

/* include the CLKIDs that have been made part of the DT binding */
#include <dt-bindings/clock/amlogic,a1-pll-clkc.h>

/*
 * CLKID index values for internal clocks
 *
 * These indices are entirely contrived and do not map onto the hardware.
 * It has now been decided to expose everything by default in the DT header:
 * include/dt-bindings/clock/a1-pll-clkc.h. Only the clocks ids we don't want
 * to expose, such as the internal muxes and dividers of composite clocks,
 * will remain defined here.
 */
#define CLKID_FIXED_PLL_DCO	0
#define CLKID_FCLK_DIV2_DIV	2
#define CLKID_FCLK_DIV3_DIV	3
#define CLKID_FCLK_DIV5_DIV	4
#define CLKID_FCLK_DIV7_DIV	5
#define NR_PLL_CLKS		11

#endif /* __A1_PLL_H */
