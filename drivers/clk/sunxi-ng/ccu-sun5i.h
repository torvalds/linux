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

#ifndef _CCU_SUN5I_H_
#define _CCU_SUN5I_H_

#include <dt-bindings/clock/sun5i-ccu.h>
#include <dt-bindings/reset/sun5i-ccu.h>

/* The HOSC is exported */
#define CLK_PLL_CORE		2
#define CLK_PLL_AUDIO_BASE	3
#define CLK_PLL_AUDIO		4
#define CLK_PLL_AUDIO_2X	5
#define CLK_PLL_AUDIO_4X	6
#define CLK_PLL_AUDIO_8X	7
#define CLK_PLL_VIDEO0		8

/* The PLL_VIDEO0_2X is exported for HDMI */

#define CLK_PLL_VE		10
#define CLK_PLL_DDR_BASE	11
#define CLK_PLL_DDR		12
#define CLK_PLL_DDR_OTHER	13
#define CLK_PLL_PERIPH		14
#define CLK_PLL_VIDEO1		15

/* The PLL_VIDEO1_2X is exported for HDMI */
/* The CPU clock is exported */

#define CLK_AXI			18
#define CLK_AHB			19
#define CLK_APB0		20
#define CLK_APB1		21
#define CLK_DRAM_AXI		22

/* AHB gates are exported */
/* APB0 gates are exported */
/* APB1 gates are exported */
/* Modules clocks are exported */
/* USB clocks are exported */
/* GPS clock is exported */
/* DRAM gates are exported */
/* More display modules clocks are exported */

#define CLK_TCON_CH1_SCLK	91

/* The rest of the module clocks are exported */

#define CLK_MBUS		99

/* And finally the IEP clock */

#define CLK_NUMBER		(CLK_IEP + 1)

#endif /* _CCU_SUN5I_H_ */
