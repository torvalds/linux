/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2017 Icenowy Zheng <icenowy@aosc.io>
 */

#ifndef _CCU_SUN8I_R40_H_
#define _CCU_SUN8I_R40_H_

#include <dt-bindings/clock/sun8i-r40-ccu.h>
#include <dt-bindings/reset/sun8i-r40-ccu.h>

#define CLK_OSC_12M		0
#define CLK_PLL_CPU		1
#define CLK_PLL_AUDIO_BASE	2
#define CLK_PLL_AUDIO		3
#define CLK_PLL_AUDIO_2X	4
#define CLK_PLL_AUDIO_4X	5
#define CLK_PLL_AUDIO_8X	6

/* PLL_VIDEO0 is exported */

#define CLK_PLL_VIDEO0_2X	8
#define CLK_PLL_VE		9
#define CLK_PLL_DDR0		10
#define CLK_PLL_PERIPH0		11
#define CLK_PLL_PERIPH0_SATA	12
#define CLK_PLL_PERIPH0_2X	13
#define CLK_PLL_PERIPH1		14
#define CLK_PLL_PERIPH1_2X	15

/* PLL_VIDEO1 is exported */

#define CLK_PLL_VIDEO1_2X	17
#define CLK_PLL_SATA		18
#define CLK_PLL_SATA_OUT	19
#define CLK_PLL_GPU		20
#define CLK_PLL_MIPI		21
#define CLK_PLL_DE		22
#define CLK_PLL_DDR1		23

/* The CPU clock is exported */

#define CLK_AXI			25
#define CLK_AHB1		26
#define CLK_APB1		27
#define CLK_APB2		28

/* All the bus gates are exported */

/* The first bunch of module clocks are exported */

#define CLK_DRAM		132

/* All the DRAM gates are exported */

/* Some more module clocks are exported */

#define CLK_MBUS		155

/* Another bunch of module clocks are exported */

#define CLK_NUMBER		(CLK_OUTB + 1)

#endif /* _CCU_SUN8I_R40_H_ */
