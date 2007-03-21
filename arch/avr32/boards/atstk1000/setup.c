/*
 * ATSTK1000 board-specific setup code.
 *
 * Copyright (C) 2005-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/bootmem.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/linkage.h>

#include <video/atmel_lcdc.h>

#include <asm/setup.h>
#include <asm/arch/board.h>

#include "atstk1000.h"

/* Initialized by bootloader-specific startup code. */
struct tag *bootloader_tags __initdata;

static struct fb_videomode __initdata ltv350qv_modes[] = {
	{
		.name		= "320x240 @ 75",
		.refresh	= 75,
		.xres		= 320,		.yres		= 240,
		.pixclock	= KHZ2PICOS(6891),

		.left_margin	= 17,		.right_margin	= 33,
		.upper_margin	= 10,		.lower_margin	= 10,
		.hsync_len	= 16,		.vsync_len	= 1,

		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
};

static struct fb_monspecs __initdata atstk1000_default_monspecs = {
	.manufacturer		= "SNG",
	.monitor		= "LTV350QV",
	.modedb			= ltv350qv_modes,
	.modedb_len		= ARRAY_SIZE(ltv350qv_modes),
	.hfmin			= 14820,
	.hfmax			= 22230,
	.vfmin			= 60,
	.vfmax			= 90,
	.dclkmax		= 30000000,
};

struct atmel_lcdfb_info __initdata atstk1000_lcdc_data = {
	.default_bpp		= 24,
	.default_dmacon		= ATMEL_LCDC_DMAEN | ATMEL_LCDC_DMA2DEN,
	.default_lcdcon2	= (ATMEL_LCDC_DISTYPE_TFT
				   | ATMEL_LCDC_INVCLK
				   | ATMEL_LCDC_CLKMOD_ALWAYSACTIVE
				   | ATMEL_LCDC_MEMOR_BIG),
	.default_monspecs	= &atstk1000_default_monspecs,
	.guard_time		= 2,
};
