/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2016 Maxime Ripard
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
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

/* PLL_VIDEO0 exported for HDMI PHY */

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

/* All the DRAM gates are exported */

/* And the DSI and GPU module clock is exported */

#define CLK_NUMBER			(CLK_GPU + 1)

#endif /* _CCU_SUN50I_A64_H_ */
