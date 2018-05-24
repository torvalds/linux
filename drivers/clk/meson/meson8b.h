/*
 * Copyright (c) 2015 Endless Mobile, Inc.
 * Author: Carlo Caione <carlo@endlessm.com>
 *
 * Copyright (c) 2016 BayLibre, Inc.
 * Michael Turquette <mturquette@baylibre.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __MESON8B_H
#define __MESON8B_H

/*
 * Clock controller register offsets
 *
 * Register offsets from the HardKernel[0] data sheet are listed in comment
 * blocks below. Those offsets must be multiplied by 4 before adding them to
 * the base address to get the right value
 *
 * [0] http://dn.odroid.com/S805/Datasheet/S805_Datasheet%20V0.8%2020150126.pdf
 */
#define HHI_GCLK_MPEG0			0x140 /* 0x50 offset in data sheet */
#define HHI_GCLK_MPEG1			0x144 /* 0x51 offset in data sheet */
#define HHI_GCLK_MPEG2			0x148 /* 0x52 offset in data sheet */
#define HHI_GCLK_OTHER			0x150 /* 0x54 offset in data sheet */
#define HHI_GCLK_AO			0x154 /* 0x55 offset in data sheet */
#define HHI_SYS_CPU_CLK_CNTL1		0x15c /* 0x57 offset in data sheet */
#define HHI_MPEG_CLK_CNTL		0x174 /* 0x5d offset in data sheet */
#define HHI_VID_CLK_CNTL		0x17c /* 0x5f offset in data sheet */
#define HHI_VID_DIVIDER_CNTL		0x198 /* 0x66 offset in data sheet */
#define HHI_SYS_CPU_CLK_CNTL0		0x19c /* 0x67 offset in data sheet */
#define HHI_MPLL_CNTL			0x280 /* 0xa0 offset in data sheet */
#define HHI_SYS_PLL_CNTL		0x300 /* 0xc0 offset in data sheet */
#define HHI_VID_PLL_CNTL		0x320 /* 0xc8 offset in data sheet */

/*
 * MPLL register offeset taken from the S905 datasheet. Vendor kernel source
 * confirm these are the same for the S805.
 */
#define HHI_MPLL_CNTL			0x280 /* 0xa0 offset in data sheet */
#define HHI_MPLL_CNTL2			0x284 /* 0xa1 offset in data sheet */
#define HHI_MPLL_CNTL3			0x288 /* 0xa2 offset in data sheet */
#define HHI_MPLL_CNTL4			0x28C /* 0xa3 offset in data sheet */
#define HHI_MPLL_CNTL5			0x290 /* 0xa4 offset in data sheet */
#define HHI_MPLL_CNTL6			0x294 /* 0xa5 offset in data sheet */
#define HHI_MPLL_CNTL7			0x298 /* 0xa6 offset in data sheet */
#define HHI_MPLL_CNTL8			0x29C /* 0xa7 offset in data sheet */
#define HHI_MPLL_CNTL9			0x2A0 /* 0xa8 offset in data sheet */
#define HHI_MPLL_CNTL10			0x2A4 /* 0xa9 offset in data sheet */

/*
 * CLKID index values
 *
 * These indices are entirely contrived and do not map onto the hardware.
 * It has now been decided to expose everything by default in the DT header:
 * include/dt-bindings/clock/gxbb-clkc.h. Only the clocks ids we don't want
 * to expose, such as the internal muxes and dividers of composite clocks,
 * will remain defined here.
 */

#define CLKID_MPLL0_DIV		96
#define CLKID_MPLL1_DIV		97
#define CLKID_MPLL2_DIV		98
#define CLKID_CPU_IN_SEL	99
#define CLKID_CPU_DIV2		100
#define CLKID_CPU_DIV3		101
#define CLKID_CPU_SCALE_DIV	102
#define CLKID_CPU_SCALE_OUT_SEL	103
#define CLKID_MPLL_PREDIV	104
#define CLKID_FCLK_DIV2_DIV	105
#define CLKID_FCLK_DIV3_DIV	106
#define CLKID_FCLK_DIV4_DIV	107
#define CLKID_FCLK_DIV5_DIV	108
#define CLKID_FCLK_DIV7_DIV	109

#define CLK_NR_CLKS		110

/*
 * include the CLKID and RESETID that have
 * been made part of the stable DT binding
 */
#include <dt-bindings/clock/meson8b-clkc.h>
#include <dt-bindings/reset/amlogic,meson8b-clkc-reset.h>

#endif /* __MESON8B_H */
