/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2016 Chen-Yu Tsai
 *
 * Chen-Yu Tsai <wens@csie.org>
 */

#ifndef _CCU_SUN8I_A83T_H_
#define _CCU_SUN8I_A83T_H_

#include <dt-bindings/clock/sun8i-a83t-ccu.h>
#include <dt-bindings/reset/sun8i-a83t-ccu.h>

#define CLK_PLL_C0CPUX		0
#define CLK_PLL_C1CPUX		1
#define CLK_PLL_AUDIO		2
#define CLK_PLL_VIDEO0		3
#define CLK_PLL_VE		4
#define CLK_PLL_DDR		5

/* pll-periph is exported to the PRCM block */

#define CLK_PLL_GPU		7
#define CLK_PLL_HSIC		8

/* pll-de is exported for the display engine */

#define CLK_PLL_VIDEO1		10

/* The CPUX clocks are exported */

#define CLK_AXI0		13
#define CLK_AXI1		14
#define CLK_AHB1		15
#define CLK_AHB2		16
#define CLK_APB1		17
#define CLK_APB2		18

/* bus gates exported */

#define CLK_CCI400		58

/* module and usb clocks exported */

#define CLK_DRAM		82

/* dram gates and more module clocks exported */

#define CLK_MBUS		95

/* more module clocks exported */

#define CLK_NUMBER		(CLK_GPU_HYD + 1)

#endif /* _CCU_SUN8I_A83T_H_ */
