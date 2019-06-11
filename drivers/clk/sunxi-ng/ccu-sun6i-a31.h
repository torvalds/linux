/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2016 Chen-Yu Tsai
 *
 * Chen-Yu Tsai <wens@csie.org>
 */

#ifndef _CCU_SUN6I_A31_H_
#define _CCU_SUN6I_A31_H_

#include <dt-bindings/clock/sun6i-a31-ccu.h>
#include <dt-bindings/reset/sun6i-a31-ccu.h>

#define CLK_PLL_CPU		0
#define CLK_PLL_AUDIO_BASE	1
#define CLK_PLL_AUDIO		2
#define CLK_PLL_AUDIO_2X	3
#define CLK_PLL_AUDIO_4X	4
#define CLK_PLL_AUDIO_8X	5
#define CLK_PLL_VIDEO0		6

/* The PLL_VIDEO0_2X clock is exported */

#define CLK_PLL_VE		8
#define CLK_PLL_DDR		9

/* The PLL_PERIPH clock is exported */

#define CLK_PLL_PERIPH_2X	11
#define CLK_PLL_VIDEO1		12

/* The PLL_VIDEO1_2X clock is exported */

#define CLK_PLL_GPU		14
#define CLK_PLL_MIPI		15
#define CLK_PLL9		16
#define CLK_PLL10		17

/* The CPUX clock is exported */

#define CLK_AXI			19
#define CLK_AHB1		20
#define CLK_APB1		21
#define CLK_APB2		22

/* All the bus gates are exported */

/* The first bunch of module clocks are exported */

/* EMAC clock is not implemented */

#define CLK_MDFS		107
#define CLK_SDRAM0		108
#define CLK_SDRAM1		109

/* All the DRAM gates are exported */

/* Some more module clocks are exported */

#define CLK_MBUS0		141
#define CLK_MBUS1		142

/* Some more module clocks and external clock outputs are exported */

#define CLK_NUMBER		(CLK_OUT_C + 1)

#endif /* _CCU_SUN6I_A31_H_ */
