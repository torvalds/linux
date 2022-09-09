// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * File: drivers/video/omap/lcd-htcherald.c
 *
 * LCD panel support for the HTC Herald
 *
 * Copyright (C) 2009 Cory Maccarrone <darkstar6262@gmail.com>
 * Copyright (C) 2009 Wing Linux
 *
 * Based on the lcd_htcwizard.c file from the linwizard project:
 * Copyright (C) linwizard.sourceforge.net
 * Author: Angelo Arrifano <miknix@gmail.com>
 * Based on lcd_h4 by Imre Deak <imre.deak@nokia.com>
 */

#include <linux/module.h>
#include <linux/platform_device.h>

#include "omapfb.h"

/* Found on WIZ200 (miknix) and some HERA110 models (darkstar62) */
static struct lcd_panel htcherald_panel_1 = {
	.name		= "lcd_herald",
	.config		= OMAP_LCDC_PANEL_TFT |
			  OMAP_LCDC_INV_HSYNC |
			  OMAP_LCDC_INV_VSYNC |
			  OMAP_LCDC_INV_PIX_CLOCK,
	.bpp		= 16,
	.data_lines	= 16,
	.x_res		= 240,
	.y_res		= 320,
	.pixel_clock	= 6093,
	.pcd		= 0, /* 15 */
	.hsw		= 10,
	.hfp		= 10,
	.hbp		= 20,
	.vsw		= 3,
	.vfp		= 2,
	.vbp		= 2,
};

static int htcherald_panel_probe(struct platform_device *pdev)
{
	omapfb_register_panel(&htcherald_panel_1);
	return 0;
}

static struct platform_driver htcherald_panel_driver = {
	.probe		= htcherald_panel_probe,
	.driver		= {
		.name	= "lcd_htcherald",
	},
};

module_platform_driver(htcherald_panel_driver);

MODULE_AUTHOR("Cory Maccarrone");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LCD panel support for the HTC Herald");
