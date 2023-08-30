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
 * [0] https://dn.odroid.com/S805/Datasheet/S805_Datasheet%20V0.8%2020150126.pdf
 */
#define HHI_GP_PLL_CNTL			0x40  /* 0x10 offset in data sheet */
#define HHI_GP_PLL_CNTL2		0x44  /* 0x11 offset in data sheet */
#define HHI_GP_PLL_CNTL3		0x48  /* 0x12 offset in data sheet */
#define HHI_GP_PLL_CNTL4		0x4C  /* 0x13 offset in data sheet */
#define HHI_GP_PLL_CNTL5		0x50  /* 0x14 offset in data sheet */
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
#define HHI_AUD_CLK_CNTL		0x178 /* 0x5e offset in data sheet */
#define HHI_VID_CLK_CNTL		0x17c /* 0x5f offset in data sheet */
#define HHI_AUD_CLK_CNTL2		0x190 /* 0x64 offset in data sheet */
#define HHI_VID_CLK_CNTL2		0x194 /* 0x65 offset in data sheet */
#define HHI_VID_DIVIDER_CNTL		0x198 /* 0x66 offset in data sheet */
#define HHI_SYS_CPU_CLK_CNTL0		0x19c /* 0x67 offset in data sheet */
#define HHI_MALI_CLK_CNTL		0x1b0 /* 0x6c offset in data sheet */
#define HHI_VPU_CLK_CNTL		0x1bc /* 0x6f offset in data sheet */
#define HHI_HDMI_CLK_CNTL		0x1cc /* 0x73 offset in data sheet */
#define HHI_VDEC_CLK_CNTL		0x1e0 /* 0x78 offset in data sheet */
#define HHI_VDEC2_CLK_CNTL		0x1e4 /* 0x79 offset in data sheet */
#define HHI_VDEC3_CLK_CNTL		0x1e8 /* 0x7a offset in data sheet */
#define HHI_NAND_CLK_CNTL		0x25c /* 0x97 offset in data sheet */
#define HHI_MPLL_CNTL			0x280 /* 0xa0 offset in data sheet */
#define HHI_SYS_PLL_CNTL		0x300 /* 0xc0 offset in data sheet */
#define HHI_VID_PLL_CNTL		0x320 /* 0xc8 offset in data sheet */
#define HHI_VID_PLL_CNTL2		0x324 /* 0xc9 offset in data sheet */
#define HHI_VID_PLL_CNTL3		0x328 /* 0xca offset in data sheet */
#define HHI_VID_PLL_CNTL4		0x32c /* 0xcb offset in data sheet */
#define HHI_VID_PLL_CNTL5		0x330 /* 0xcc offset in data sheet */
#define HHI_VID_PLL_CNTL6		0x334 /* 0xcd offset in data sheet */
#define HHI_VID2_PLL_CNTL		0x380 /* 0xe0 offset in data sheet */
#define HHI_VID2_PLL_CNTL2		0x384 /* 0xe1 offset in data sheet */
#define HHI_VID2_PLL_CNTL3		0x388 /* 0xe2 offset in data sheet */
#define HHI_VID2_PLL_CNTL4		0x38c /* 0xe3 offset in data sheet */
#define HHI_VID2_PLL_CNTL5		0x390 /* 0xe4 offset in data sheet */
#define HHI_VID2_PLL_CNTL6		0x394 /* 0xe5 offset in data sheet */

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

#endif /* __MESON8B_H */
