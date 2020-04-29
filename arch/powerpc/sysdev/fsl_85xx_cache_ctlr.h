/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2009-2010, 2012 Freescale Semiconductor, Inc
 *
 * QorIQ based Cache Controller Memory Mapped Registers
 *
 * Author: Vivek Mahajan <vivek.mahajan@freescale.com>
 */

#ifndef __FSL_85XX_CACHE_CTLR_H__
#define __FSL_85XX_CACHE_CTLR_H__

#define L2CR_L2FI		0x40000000	/* L2 flash invalidate */
#define L2CR_L2IO		0x00200000	/* L2 instruction only */
#define L2CR_SRAM_ZERO		0x00000000	/* L2SRAM zero size */
#define L2CR_SRAM_FULL		0x00010000	/* L2SRAM full size */
#define L2CR_SRAM_HALF		0x00020000	/* L2SRAM half size */
#define L2CR_SRAM_TWO_HALFS	0x00030000	/* L2SRAM two half sizes */
#define L2CR_SRAM_QUART		0x00040000	/* L2SRAM one quarter size */
#define L2CR_SRAM_TWO_QUARTS	0x00050000	/* L2SRAM two quarter size */
#define L2CR_SRAM_EIGHTH	0x00060000	/* L2SRAM one eighth size */
#define L2CR_SRAM_TWO_EIGHTH	0x00070000	/* L2SRAM two eighth size */

#define L2SRAM_OPTIMAL_SZ_SHIFT	0x00000003	/* Optimum size for L2SRAM */

#define L2SRAM_BAR_MSK_LO18	0xFFFFC000	/* Lower 18 bits */
#define L2SRAM_BARE_MSK_HI4	0x0000000F	/* Upper 4 bits */

enum cache_sram_lock_ways {
	LOCK_WAYS_ZERO,
	LOCK_WAYS_EIGHTH,
	LOCK_WAYS_TWO_EIGHTH,
	LOCK_WAYS_HALF = 4,
	LOCK_WAYS_FULL = 8,
};

struct mpc85xx_l2ctlr {
	u32	ctl;		/* 0x000 - L2 control */
	u8	res1[0xC];
	u32	ewar0;		/* 0x010 - External write address 0 */
	u32	ewarea0;	/* 0x014 - External write address extended 0 */
	u32	ewcr0;		/* 0x018 - External write ctrl */
	u8	res2[4];
	u32	ewar1;		/* 0x020 - External write address 1 */
	u32	ewarea1;	/* 0x024 - External write address extended 1 */
	u32	ewcr1;		/* 0x028 - External write ctrl 1 */
	u8	res3[4];
	u32	ewar2;		/* 0x030 - External write address 2 */
	u32	ewarea2;	/* 0x034 - External write address extended 2 */
	u32	ewcr2;		/* 0x038 - External write ctrl 2 */
	u8	res4[4];
	u32	ewar3;		/* 0x040 - External write address 3 */
	u32	ewarea3;	/* 0x044 - External write address extended 3 */
	u32	ewcr3;		/* 0x048 - External write ctrl 3 */
	u8	res5[0xB4];
	u32	srbar0;		/* 0x100 - SRAM base address 0 */
	u32	srbarea0;	/* 0x104 - SRAM base addr reg ext address 0 */
	u32	srbar1;		/* 0x108 - SRAM base address 1 */
	u32	srbarea1;	/* 0x10C - SRAM base addr reg ext address 1 */
	u8	res6[0xCF0];
	u32	errinjhi;	/* 0xE00 - Error injection mask high */
	u32	errinjlo;	/* 0xE04 - Error injection mask low */
	u32	errinjctl;	/* 0xE08 - Error injection tag/ecc control */
	u8	res7[0x14];
	u32	captdatahi;	/* 0xE20 - Error data high capture */
	u32	captdatalo;	/* 0xE24 - Error data low capture */
	u32	captecc;	/* 0xE28 - Error syndrome */
	u8	res8[0x14];
	u32	errdet;		/* 0xE40 - Error detect */
	u32	errdis;		/* 0xE44 - Error disable */
	u32	errinten;	/* 0xE48 - Error interrupt enable */
	u32	errattr;	/* 0xE4c - Error attribute capture */
	u32	erradrrl;	/* 0xE50 - Error address capture low */
	u32	erradrrh;	/* 0xE54 - Error address capture high */
	u32	errctl;		/* 0xE58 - Error control */
	u8	res9[0x1A4];
};

struct sram_parameters {
	unsigned int sram_size;
	phys_addr_t sram_offset;
};

extern int instantiate_cache_sram(struct platform_device *dev,
		struct sram_parameters sram_params);
extern void remove_cache_sram(struct platform_device *dev);

#endif /* __FSL_85XX_CACHE_CTLR_H__ */
