/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2016 Icenowy Zheng <icenowy@aosc.xyz>
 *
 * Based on ccu-sun8i-h3.h, which is:
 * Copyright (c) 2016 Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#ifndef _CCU_SUN8I_H3_H_
#define _CCU_SUN8I_H3_H_

#include <dt-bindings/clock/sun8i-v3s-ccu.h>
#include <dt-bindings/reset/sun8i-v3s-ccu.h>

#define CLK_PLL_CPU		0
#define CLK_PLL_AUDIO_BASE	1
#define CLK_PLL_AUDIO		2
#define CLK_PLL_AUDIO_2X	3
#define CLK_PLL_AUDIO_4X	4
#define CLK_PLL_AUDIO_8X	5
#define CLK_PLL_VIDEO		6
#define CLK_PLL_VE		7
#define CLK_PLL_DDR0		8
#define CLK_PLL_PERIPH0		9
#define CLK_PLL_PERIPH0_2X	10
#define CLK_PLL_ISP		11
#define CLK_PLL_PERIPH1		12
/* Reserve one number for not implemented and not used PLL_DDR1 */

/* The CPU clock is exported */

#define CLK_AXI			15
#define CLK_AHB1		16
#define CLK_APB1		17
#define CLK_APB2		18
#define CLK_AHB2		19

/* All the bus gates are exported */

/* The first bunch of module clocks are exported */

#define CLK_DRAM		58

/* All the DRAM gates are exported */

/* Some more module clocks are exported */

#define CLK_MBUS		72

/* And the GPU module clock is exported */

#define CLK_PLL_DDR1		74

#define CLK_NUMBER		(CLK_I2S0 + 1)

#endif /* _CCU_SUN8I_H3_H_ */
