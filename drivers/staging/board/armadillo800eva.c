// SPDX-License-Identifier: GPL-2.0
/*
 * Staging board support for Armadillo 800 eva.
 * Enable not-yet-DT-capable devices here.
 *
 * Based on board-armadillo800eva.c
 *
 * Copyright (C) 2012 Renesas Solutions Corp.
 * Copyright (C) 2012 Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 */

#include <linux/dma-mapping.h>
#include <linux/fb.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/videodev2.h>

#include <video/sh_mobile_lcdc.h>

#include "board.h"

static struct fb_videomode lcdc0_mode = {
	.name		= "AMPIER/AM-800480",
	.xres		= 800,
	.yres		= 480,
	.left_margin	= 88,
	.right_margin	= 40,
	.hsync_len	= 128,
	.upper_margin	= 20,
	.lower_margin	= 5,
	.vsync_len	= 5,
	.sync		= 0,
};

static struct sh_mobile_lcdc_info lcdc0_info = {
	.clock_source	= LCDC_CLK_BUS,
	.ch[0] = {
		.chan		= LCDC_CHAN_MAINLCD,
		.fourcc		= V4L2_PIX_FMT_RGB565,
		.interface_type	= RGB24,
		.clock_divider	= 5,
		.flags		= 0,
		.lcd_modes	= &lcdc0_mode,
		.num_modes	= 1,
		.panel_cfg = {
			.width	= 111,
			.height = 68,
		},
	},
};

static struct resource lcdc0_resources[] = {
	DEFINE_RES_MEM_NAMED(0xfe940000, 0x4000, "LCD0"),
	DEFINE_RES_IRQ(177 + 32),
};

static struct platform_device lcdc0_device = {
	.name		= "sh_mobile_lcdc_fb",
	.num_resources	= ARRAY_SIZE(lcdc0_resources),
	.resource	= lcdc0_resources,
	.id		= 0,
	.dev	= {
		.platform_data	= &lcdc0_info,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

static const struct board_staging_clk lcdc0_clocks[] __initconst = {
	{ "lcdc0", NULL, "sh_mobile_lcdc_fb.0" },
};

static const struct board_staging_dev armadillo800eva_devices[] __initconst = {
	{
		.pdev	 = &lcdc0_device,
		.clocks	 = lcdc0_clocks,
		.nclocks = ARRAY_SIZE(lcdc0_clocks),
		.domain	 = "/system-controller@e6180000/pm-domains/c5/a4lc@1"
	},
};

static void __init armadillo800eva_init(void)
{
	board_staging_gic_setup_xlate("arm,pl390", 32);
	board_staging_register_devices(armadillo800eva_devices,
				       ARRAY_SIZE(armadillo800eva_devices));
}

board_staging("renesas,armadillo800eva", armadillo800eva_init);
