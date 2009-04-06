/*
 * Display setup code for the Merisc board
 *
 * Copyright (C) 2008 Martinsson Elektronik AB
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <video/atmel_lcdc.h>
#include <asm/setup.h>
#include <mach/board.h>
#include "merisc.h"

static struct fb_videomode merisc_fb_videomode[] = {
	{
		.refresh	= 44,
		.xres		= 640,
		.yres		= 480,
		.left_margin	= 96,
		.right_margin	= 96,
		.upper_margin	= 34,
		.lower_margin	= 8,
		.hsync_len	= 64,
		.vsync_len	= 64,
		.name		= "640x480 @ 44",
		.pixclock	= KHZ2PICOS(25180),
		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
};

static struct fb_monspecs merisc_fb_monspecs = {
	.manufacturer	= "Kyo",
	.monitor	= "TCG075VG2AD",
	.modedb		= merisc_fb_videomode,
	.modedb_len	= ARRAY_SIZE(merisc_fb_videomode),
	.hfmin		= 30000,
	.hfmax		= 33333,
	.vfmin		= 60,
	.vfmax		= 90,
	.dclkmax	= 30000000,
};

struct atmel_lcdfb_info merisc_lcdc_data = {
	.default_bpp		= 24,
	.default_dmacon		= ATMEL_LCDC_DMAEN | ATMEL_LCDC_DMA2DEN,
	.default_lcdcon2	= (ATMEL_LCDC_DISTYPE_TFT
				   | ATMEL_LCDC_CLKMOD_ALWAYSACTIVE
				   | ATMEL_LCDC_MEMOR_BIG),
	.default_monspecs	= &merisc_fb_monspecs,
	.guard_time		= 2,
};

static int __init merisc_display_init(void)
{
	at32_add_device_lcdc(0, &merisc_lcdc_data, fbmem_start,
			     fbmem_size, 0);

	return 0;
}
device_initcall(merisc_display_init);
