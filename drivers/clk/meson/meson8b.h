/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2015 Endless Mobile, Inc.
 * Author: Carlo Caione <carlo@endlessm.com>
 *
 * Copyright (c) 2016 BayLibre, Inc.
 * Michael Turquette <mturquette@baylibre.com>
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
#define HHI_VIID_CLK_DIV		0x128 /* 0x4a offset in data sheet */
#define HHI_VIID_CLK_CNTL		0x12c /* 0x4b offset in data sheet */
#define HHI_GCLK_MPEG0			0x140 /* 0x50 offset in data sheet */
#define HHI_GCLK_MPEG1			0x144 /* 0x51 offset in data sheet */
#define HHI_GCLK_MPEG2			0x148 /* 0x52 offset in data sheet */
#define HHI_GCLK_OTHER			0x150 /* 0x54 offset in data sheet */
#define HHI_GCLK_AO			0x154 /* 0x55 offset in data sheet */
#define HHI_SYS_CPU_CLK_CNTL1		0x15c /* 0x57 offset in data sheet */
#define HHI_VID_CLK_DIV			0x164 /* 0x59 offset in data sheet */
#define HHI_MPEG_CLK_CNTL		0x174 /* 0x5d offset in data sheet */
#define HHI_VID_CLK_CNTL		0x17c /* 0x5f offset in data sheet */
#define HHI_VID_CLK_CNTL2		0x194 /* 0x65 offset in data sheet */
#define HHI_VID_DIVIDER_CNTL		0x198 /* 0x66 offset in data sheet */
#define HHI_SYS_CPU_CLK_CNTL0		0x19c /* 0x67 offset in data sheet */
#define HHI_HDMI_CLK_CNTL		0x1cc /* 0x73 offset in data sheet */
#define HHI_NAND_CLK_CNTL		0x25c /* 0x97 offset in data sheet */
#define HHI_MPLL_CNTL			0x280 /* 0xa0 offset in data sheet */
#define HHI_SYS_PLL_CNTL		0x300 /* 0xc0 offset in data sheet */
#define HHI_VID_PLL_CNTL		0x320 /* 0xc8 offset in data sheet */
#define HHI_VID_PLL_CNTL2		0x324 /* 0xc9 offset in data sheet */

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
#define CLKID_CPU_IN_DIV2	100
#define CLKID_CPU_IN_DIV3	101
#define CLKID_CPU_SCALE_DIV	102
#define CLKID_CPU_SCALE_OUT_SEL	103
#define CLKID_MPLL_PREDIV	104
#define CLKID_FCLK_DIV2_DIV	105
#define CLKID_FCLK_DIV3_DIV	106
#define CLKID_FCLK_DIV4_DIV	107
#define CLKID_FCLK_DIV5_DIV	108
#define CLKID_FCLK_DIV7_DIV	109
#define CLKID_NAND_SEL		110
#define CLKID_NAND_DIV		111
#define CLKID_PLL_FIXED_DCO	113
#define CLKID_HDMI_PLL_DCO	114
#define CLKID_PLL_SYS_DCO	115
#define CLKID_CPU_CLK_DIV2	116
#define CLKID_CPU_CLK_DIV3	117
#define CLKID_CPU_CLK_DIV4	118
#define CLKID_CPU_CLK_DIV5	119
#define CLKID_CPU_CLK_DIV6	120
#define CLKID_CPU_CLK_DIV7	121
#define CLKID_CPU_CLK_DIV8	122
#define CLKID_ABP_SEL		123
#define CLKID_PERIPH_SEL	125
#define CLKID_AXI_SEL		127
#define CLKID_L2_DRAM_SEL	129
#define CLKID_HDMI_PLL_LVDS_OUT	131
#define CLKID_HDMI_PLL_HDMI_OUT	132
#define CLKID_VID_PLL_IN_SEL	133
#define CLKID_VID_PLL_IN_EN	134
#define CLKID_VID_PLL_PRE_DIV	135
#define CLKID_VID_PLL_POST_DIV	136
#define CLKID_VID_PLL_FINAL_DIV	137
#define CLKID_VCLK_IN_SEL	138
#define CLKID_VCLK_IN_EN	139
#define CLKID_VCLK_DIV1		140
#define CLKID_VCLK_DIV2_DIV	141
#define CLKID_VCLK_DIV2		142
#define CLKID_VCLK_DIV4_DIV	143
#define CLKID_VCLK_DIV4		144
#define CLKID_VCLK_DIV6_DIV	145
#define CLKID_VCLK_DIV6		146
#define CLKID_VCLK_DIV12_DIV	147
#define CLKID_VCLK_DIV12	148
#define CLKID_VCLK2_IN_SEL	149
#define CLKID_VCLK2_IN_EN	150
#define CLKID_VCLK2_DIV1	151
#define CLKID_VCLK2_DIV2_DIV	152
#define CLKID_VCLK2_DIV2	153
#define CLKID_VCLK2_DIV4_DIV	154
#define CLKID_VCLK2_DIV4	155
#define CLKID_VCLK2_DIV6_DIV	156
#define CLKID_VCLK2_DIV6	157
#define CLKID_VCLK2_DIV12_DIV	158
#define CLKID_VCLK2_DIV12	159
#define CLKID_CTS_ENCT_SEL	160
#define CLKID_CTS_ENCT		161
#define CLKID_CTS_ENCP_SEL	162
#define CLKID_CTS_ENCP		163
#define CLKID_CTS_ENCI_SEL	164
#define CLKID_CTS_ENCI		165
#define CLKID_HDMI_TX_PIXEL_SEL	166
#define CLKID_HDMI_TX_PIXEL	167
#define CLKID_CTS_ENCL_SEL	168
#define CLKID_CTS_ENCL		169
#define CLKID_CTS_VDAC0_SEL	170
#define CLKID_CTS_VDAC0		171
#define CLKID_HDMI_SYS_SEL	172
#define CLKID_HDMI_SYS_DIV	173
#define CLKID_HDMI_SYS		174

#define CLK_NR_CLKS		175

/*
 * include the CLKID and RESETID that have
 * been made part of the stable DT binding
 */
#include <dt-bindings/clock/meson8b-clkc.h>
#include <dt-bindings/reset/amlogic,meson8b-clkc-reset.h>

#endif /* __MESON8B_H */
