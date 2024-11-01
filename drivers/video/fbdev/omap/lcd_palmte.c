// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * LCD panel support for the Palm Tungsten E
 *
 * Original version : Romain Goyet <r.goyet@gmail.com>
 * Current version : Laurent Gonzalez <palmte.linux@free.fr>
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include "omapfb.h"

static struct lcd_panel palmte_panel = {
	.name		= "palmte",
	.config		= OMAP_LCDC_PANEL_TFT | OMAP_LCDC_INV_VSYNC |
			  OMAP_LCDC_INV_HSYNC | OMAP_LCDC_HSVS_RISING_EDGE |
			  OMAP_LCDC_HSVS_OPPOSITE,

	.data_lines	= 16,
	.bpp		= 8,
	.pixel_clock	= 12000,
	.x_res		= 320,
	.y_res		= 320,
	.hsw		= 4,
	.hfp		= 8,
	.hbp		= 28,
	.vsw		= 1,
	.vfp		= 8,
	.vbp		= 7,
	.pcd		= 0,
};

static int palmte_panel_probe(struct platform_device *pdev)
{
	omapfb_register_panel(&palmte_panel);
	return 0;
}

static struct platform_driver palmte_panel_driver = {
	.probe		= palmte_panel_probe,
	.driver		= {
		.name	= "lcd_palmte",
	},
};

module_platform_driver(palmte_panel_driver);

MODULE_AUTHOR("Romain Goyet <r.goyet@gmail.com>, Laurent Gonzalez <palmte.linux@free.fr>");
MODULE_DESCRIPTION("LCD panel support for the Palm Tungsten E");
MODULE_LICENSE("GPL");
