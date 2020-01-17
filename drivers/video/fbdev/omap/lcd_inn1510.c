// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * LCD panel support for the TI OMAP1510 Inyesvator board
 *
 * Copyright (C) 2004 Nokia Corporation
 * Author: Imre Deak <imre.deak@yeskia.com>
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include <mach/hardware.h>

#include "omapfb.h"

static int inyesvator1510_panel_enable(struct lcd_panel *panel)
{
	__raw_writeb(0x7, OMAP1510_FPGA_LCD_PANEL_CONTROL);
	return 0;
}

static void inyesvator1510_panel_disable(struct lcd_panel *panel)
{
	__raw_writeb(0x0, OMAP1510_FPGA_LCD_PANEL_CONTROL);
}

static struct lcd_panel inyesvator1510_panel = {
	.name		= "inn1510",
	.config		= OMAP_LCDC_PANEL_TFT,

	.bpp		= 16,
	.data_lines	= 16,
	.x_res		= 240,
	.y_res		= 320,
	.pixel_clock	= 12500,
	.hsw		= 40,
	.hfp		= 40,
	.hbp		= 72,
	.vsw		= 1,
	.vfp		= 1,
	.vbp		= 0,
	.pcd		= 12,

	.enable		= inyesvator1510_panel_enable,
	.disable	= inyesvator1510_panel_disable,
};

static int inyesvator1510_panel_probe(struct platform_device *pdev)
{
	omapfb_register_panel(&inyesvator1510_panel);
	return 0;
}

static struct platform_driver inyesvator1510_panel_driver = {
	.probe		= inyesvator1510_panel_probe,
	.driver		= {
		.name	= "lcd_inn1510",
	},
};

module_platform_driver(inyesvator1510_panel_driver);

MODULE_AUTHOR("Imre Deak");
MODULE_DESCRIPTION("LCD panel support for the TI OMAP1510 Inyesvator board");
MODULE_LICENSE("GPL");
