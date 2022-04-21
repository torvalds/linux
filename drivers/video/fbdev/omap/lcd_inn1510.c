// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * LCD panel support for the TI OMAP1510 Innovator board
 *
 * Copyright (C) 2004 Nokia Corporation
 * Author: Imre Deak <imre.deak@nokia.com>
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include <linux/soc/ti/omap1-soc.h>

#include "omapfb.h"

static void __iomem *omap1510_fpga_lcd_panel_control;

static int innovator1510_panel_enable(struct lcd_panel *panel)
{
	__raw_writeb(0x7, omap1510_fpga_lcd_panel_control);
	return 0;
}

static void innovator1510_panel_disable(struct lcd_panel *panel)
{
	__raw_writeb(0x0, omap1510_fpga_lcd_panel_control);
}

static struct lcd_panel innovator1510_panel = {
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

	.enable		= innovator1510_panel_enable,
	.disable	= innovator1510_panel_disable,
};

static int innovator1510_panel_probe(struct platform_device *pdev)
{
	omap1510_fpga_lcd_panel_control = (void __iomem *)pdev->dev.platform_data;
	omapfb_register_panel(&innovator1510_panel);
	return 0;
}

static struct platform_driver innovator1510_panel_driver = {
	.probe		= innovator1510_panel_probe,
	.driver		= {
		.name	= "lcd_inn1510",
	},
};

module_platform_driver(innovator1510_panel_driver);

MODULE_AUTHOR("Imre Deak");
MODULE_DESCRIPTION("LCD panel support for the TI OMAP1510 Innovator board");
MODULE_LICENSE("GPL");
