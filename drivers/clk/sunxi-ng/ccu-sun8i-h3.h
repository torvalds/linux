/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2016 Maxime Ripard
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#ifndef _CCU_SUN8I_H3_H_
#define _CCU_SUN8I_H3_H_

#include <dt-bindings/clock/sun8i-h3-ccu.h>
#include <dt-bindings/reset/sun8i-h3-ccu.h>

#define CLK_PLL_CPUX		0
#define CLK_PLL_AUDIO_BASE	1
#define CLK_PLL_AUDIO		2
#define CLK_PLL_AUDIO_2X	3
#define CLK_PLL_AUDIO_4X	4
#define CLK_PLL_AUDIO_8X	5

/* PLL_VIDEO is exported */

#define CLK_PLL_VE		7
#define CLK_PLL_DDR		8

/* PLL_PERIPH0 exported for PRCM */

#define CLK_PLL_PERIPH0_2X	10
#define CLK_PLL_GPU		11
#define CLK_PLL_PERIPH1		12
#define CLK_PLL_DE		13

/* The CPUX clock is exported */

#define CLK_AXI			15
#define CLK_AHB1		16
#define CLK_APB1		17
#define CLK_APB2		18
#define CLK_AHB2		19

/* All the bus gates are exported */

/* The first bunch of module clocks are exported */

#define CLK_DRAM		96

/* All the DRAM gates are exported */

/* Some more module clocks are exported */

#define CLK_NUMBER_H3		(CLK_GPU + 1)
#define CLK_NUMBER_H5		(CLK_BUS_SCR1 + 1)

#endif /* _CCU_SUN8I_H3_H_ */
