/*
 * Copyright 2016 Maxime Ripard
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _CCU_SUN50I_A64_H_
#define _CCU_SUN50I_A64_H_

#include <dt-bindings/clock/sun50i-a64-ccu.h>
#include <dt-bindings/reset/sun50i-a64-ccu.h>

#define CLK_OSC_12M			0
#define CLK_PLL_CPUX			1
#define CLK_PLL_AUDIO_BASE		2
#define CLK_PLL_AUDIO			3
#define CLK_PLL_AUDIO_2X		4
#define CLK_PLL_AUDIO_4X		5
#define CLK_PLL_AUDIO_8X		6
#define CLK_PLL_VIDEO0			7
#define CLK_PLL_VIDEO0_2X		8
#define CLK_PLL_VE			9
#define CLK_PLL_DDR0			10

/* PLL_PERIPH0 exported for PRCM */

#define CLK_PLL_PERIPH0_2X		12
#define CLK_PLL_PERIPH1			13
#define CLK_PLL_PERIPH1_2X		14
#define CLK_PLL_VIDEO1			15
#define CLK_PLL_GPU			16
#define CLK_PLL_MIPI			17
#define CLK_PLL_HSIC			18
#define CLK_PLL_DE			19
#define CLK_PLL_DDR1			20
#define CLK_CPUX			21
#define CLK_AXI				22
#define CLK_APB				23
#define CLK_AHB1			24
#define CLK_APB1			25
#define CLK_APB2			26
#define CLK_AHB2			27

/* All the bus gates are exported */

/* The first bunch of module clocks are exported */

#define CLK_USB_OHCI0_12M		90

#define CLK_USB_OHCI1_12M		92

#define CLK_DRAM			94

/* All the DRAM gates are exported */

/* Some more module clocks are exported */

#define CLK_MBUS			112

/* And the DSI and GPU module clock is exported */

#define CLK_NUMBER			(CLK_GPU + 1)

#endif /* _CCU_SUN50I_A64_H_ */
