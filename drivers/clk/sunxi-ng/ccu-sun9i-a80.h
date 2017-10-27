/*
 * Copyright 2016 Chen-Yu Tsai
 *
 * Chen-Yu Tsai <wens@csie.org>
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

#ifndef _CCU_SUN9I_A80_H_
#define _CCU_SUN9I_A80_H_

#include <dt-bindings/clock/sun9i-a80-ccu.h>
#include <dt-bindings/reset/sun9i-a80-ccu.h>

#define CLK_PLL_C0CPUX		0
#define CLK_PLL_C1CPUX		1

/* pll-audio and pll-periph0 are exported to the PRCM block */

#define CLK_PLL_VE		4
#define CLK_PLL_DDR		5
#define CLK_PLL_VIDEO0		6
#define CLK_PLL_VIDEO1		7
#define CLK_PLL_GPU		8
#define CLK_PLL_DE		9
#define CLK_PLL_ISP		10
#define CLK_PLL_PERIPH1		11

/* The CPUX clocks are exported */

#define CLK_ATB0		14
#define CLK_AXI0		15
#define CLK_ATB1		16
#define CLK_AXI1		17
#define CLK_GTBUS		18
#define CLK_AHB0		19
#define CLK_AHB1		20
#define CLK_AHB2		21
#define CLK_APB0		22
#define CLK_APB1		23
#define CLK_CCI400		24
#define CLK_ATS			25
#define CLK_TRACE		26

/* module clocks and bus gates exported */

#define CLK_NUMBER		(CLK_BUS_UART5 + 1)

#endif /* _CCU_SUN9I_A80_H_ */
