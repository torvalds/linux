/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020 Yangtao Li <frank@allwinnertech.com>
 */

#ifndef _CCU_SUN50I_A100_H_
#define _CCU_SUN50I_A100_H_

#include <dt-bindings/clock/sun50i-a100-ccu.h>
#include <dt-bindings/reset/sun50i-a100-ccu.h>

#define CLK_OSC12M		0
#define CLK_PLL_CPUX		1
#define CLK_PLL_DDR0		2

/* PLL_PERIPH0 exported for PRCM */

#define CLK_PLL_PERIPH0_2X	4
#define CLK_PLL_PERIPH1		5
#define CLK_PLL_PERIPH1_2X	6
#define CLK_PLL_GPU		7
#define CLK_PLL_VIDEO0		8
#define CLK_PLL_VIDEO0_2X	9
#define CLK_PLL_VIDEO0_4X	10
#define CLK_PLL_VIDEO1		11
#define CLK_PLL_VIDEO1_2X	12
#define CLK_PLL_VIDEO1_4X	13
#define CLK_PLL_VIDEO2		14
#define CLK_PLL_VIDEO2_2X	15
#define CLK_PLL_VIDEO2_4X	16
#define CLK_PLL_VIDEO3		17
#define CLK_PLL_VIDEO3_2X	18
#define CLK_PLL_VIDEO3_4X	19
#define CLK_PLL_VE		20
#define CLK_PLL_COM		21
#define CLK_PLL_COM_AUDIO	22
#define CLK_PLL_AUDIO		23

/* CPUX clock exported for DVFS */

#define CLK_AXI			25
#define CLK_CPUX_APB		26
#define CLK_PSI_AHB1_AHB2	27
#define CLK_AHB3		28

/* APB1 clock exported for PIO */

#define CLK_APB2		30

/* All module clocks and bus gates are exported except DRAM */

#define CLK_BUS_DRAM		58

#define CLK_NUMBER		(CLK_CSI_ISP + 1)

#endif /* _CCU_SUN50I_A100_H_ */
