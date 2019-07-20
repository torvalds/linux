/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2017 Priit Laes
 *
 * Priit Laes <plaes@plaes.org>
 */

#ifndef _CCU_SUN4I_A10_H_
#define _CCU_SUN4I_A10_H_

#include <dt-bindings/clock/sun4i-a10-ccu.h>
#include <dt-bindings/clock/sun7i-a20-ccu.h>
#include <dt-bindings/reset/sun4i-a10-ccu.h>

/* The HOSC is exported */
#define CLK_PLL_CORE		2
#define CLK_PLL_AUDIO_BASE	3
#define CLK_PLL_AUDIO		4
#define CLK_PLL_AUDIO_2X	5
#define CLK_PLL_AUDIO_4X	6
#define CLK_PLL_AUDIO_8X	7
#define CLK_PLL_VIDEO0		8
/* The PLL_VIDEO0_2X clock is exported */
#define CLK_PLL_VE		10
#define CLK_PLL_DDR_BASE	11
#define CLK_PLL_DDR		12
#define CLK_PLL_DDR_OTHER	13
#define CLK_PLL_PERIPH_BASE	14
#define CLK_PLL_PERIPH		15
#define CLK_PLL_PERIPH_SATA	16
#define CLK_PLL_VIDEO1		17
/* The PLL_VIDEO1_2X clock is exported */
#define CLK_PLL_GPU		19

/* The CPU clock is exported */
#define CLK_AXI			21
#define CLK_AXI_DRAM		22
#define CLK_AHB			23
#define CLK_APB0		24
#define CLK_APB1		25

/* AHB gates are exported (23..68) */
/* APB0 gates are exported (69..78) */
/* APB1 gates are exported (79..95) */
/* IP module clocks are exported (96..128) */
/* DRAM gates are exported (129..142)*/
/* Media (display engine clocks & etc) are exported (143..169) */

#define CLK_NUMBER_SUN4I	(CLK_MBUS + 1)
#define CLK_NUMBER_SUN7I	(CLK_OUT_B + 1)

#endif /* _CCU_SUN4I_A10_H_ */
