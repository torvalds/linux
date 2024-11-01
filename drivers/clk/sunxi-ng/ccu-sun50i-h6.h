/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2016 Icenowy Zheng <icenowy@aosc.io>
 */

#ifndef _CCU_SUN50I_H6_H_
#define _CCU_SUN50I_H6_H_

#include <dt-bindings/clock/sun50i-h6-ccu.h>
#include <dt-bindings/reset/sun50i-h6-ccu.h>

#define CLK_OSC12M		0
#define CLK_PLL_CPUX		1
#define CLK_PLL_DDR0		2

/* PLL_PERIPH0 exported for PRCM */

#define CLK_PLL_PERIPH0_2X	4
#define CLK_PLL_PERIPH0_4X	5
#define CLK_PLL_PERIPH1		6
#define CLK_PLL_PERIPH1_2X	7
#define CLK_PLL_PERIPH1_4X	8
#define CLK_PLL_GPU		9
#define CLK_PLL_VIDEO0		10
#define CLK_PLL_VIDEO0_4X	11
#define CLK_PLL_VIDEO1		12
#define CLK_PLL_VIDEO1_4X	13
#define CLK_PLL_VE		14
#define CLK_PLL_DE		15
#define CLK_PLL_HSIC		16
#define CLK_PLL_AUDIO_BASE	17
#define CLK_PLL_AUDIO		18
#define CLK_PLL_AUDIO_2X	19
#define CLK_PLL_AUDIO_4X	20

/* CPUX clock exported for DVFS */

#define CLK_AXI			22
#define CLK_CPUX_APB		23
#define CLK_PSI_AHB1_AHB2	24
#define CLK_AHB3		25

/* APB1 clock exported for PIO */

#define CLK_APB2		27
#define CLK_MBUS		28

/* All module clocks and bus gates are exported except DRAM */

#define CLK_DRAM		52

#define CLK_BUS_DRAM		60

#define CLK_NUMBER		(CLK_BUS_HDCP + 1)

#endif /* _CCU_SUN50I_H6_H_ */
