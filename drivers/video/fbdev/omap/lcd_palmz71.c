// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * LCD panel support for the Palm Zire71
 *
 * Original version : Romain Goyet
 * Current version : Laurent Gonzalez
 * Modified for zire71 : Marek Vasut
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include "omapfb.h"

static unsigned long palmz71_panel_get_caps(struct lcd_panel *panel)
{
	return OMAPFB_CAPS_SET_BACKLIGHT;
}

static struct lcd_panel palmz71_panel = {
	.name		= "palmz71",
	.config		= OMAP_LCDC_PANEL_TFT | OMAP_LCDC_INV_VSYNC |
			  OMAP_LCDC_INV_HSYNC | OMAP_LCDC_HSVS_RISING_EDGE |
			  OMAP_LCDC_HSVS_OPPOSITE,
	.data_lines	= 16,
	.bpp		= 16,
	.pixel_clock	= 24000,
	.x_res		= 320,
	.y_res		= 320,
	.hsw		= 4,
	.hfp		= 8,
	.hbp		= 28,
	.vsw		= 1,
	.vfp		= 8,
	.vbp		= 7,
	.pcd		= 0,

	.get_caps	= palmz71_panel_get_caps,
};

static int palmz71_panel_probe(struct platform_device *pdev)
{
	omapfb_register_panel(&palmz71_panel);
	return 0;
}

static struct platform_driver palmz71_panel_driver = {
	.probe		= palmz71_panel_probe,
	.driver		= {
		.name	= "lcd_palmz71",
	},
};

module_platform_driver(palmz71_panel_driver);

MODULE_AUTHOR("Romain Goyet, Laurent Gonzalez, Marek Vasut");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LCD panel support for the Palm Zire71");
