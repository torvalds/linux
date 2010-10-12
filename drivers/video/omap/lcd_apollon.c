/*
 * LCD panel support for the Samsung OMAP2 Apollon board
 *
 * Copyright (C) 2005,2006 Samsung Electronics
 * Author: Kyungmin Park <kyungmin.park@samsung.com>
 *
 * Derived from drivers/video/omap/lcd-h4.c
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/module.h>
#include <linux/platform_device.h>

#include <mach/gpio.h>

#include "omapfb.h"

/* #define USE_35INCH_LCD 1 */

static int apollon_panel_init(struct lcd_panel *panel,
				struct omapfb_device *fbdev)
{
	return 0;
}

static void apollon_panel_cleanup(struct lcd_panel *panel)
{
}

static int apollon_panel_enable(struct lcd_panel *panel)
{
	return 0;
}

static void apollon_panel_disable(struct lcd_panel *panel)
{
}

static unsigned long apollon_panel_get_caps(struct lcd_panel *panel)
{
	return 0;
}

struct lcd_panel apollon_panel = {
	.name		= "apollon",
	.config		= OMAP_LCDC_PANEL_TFT | OMAP_LCDC_INV_VSYNC |
			  OMAP_LCDC_INV_HSYNC,

	.bpp		= 16,
	.data_lines	= 18,
#ifdef USE_35INCH_LCD
	.x_res		= 240,
	.y_res		= 320,
	.hsw		= 2,
	.hfp		= 3,
	.hbp		= 9,
	.vsw		= 4,
	.vfp		= 3,
	.vbp		= 5,
#else
	.x_res		= 480,
	.y_res		= 272,
	.hsw		= 41,
	.hfp		= 2,
	.hbp		= 2,
	.vsw		= 10,
	.vfp		= 2,
	.vbp		= 2,
#endif
	.pixel_clock	= 6250,

	.init		= apollon_panel_init,
	.cleanup	= apollon_panel_cleanup,
	.enable		= apollon_panel_enable,
	.disable	= apollon_panel_disable,
	.get_caps	= apollon_panel_get_caps,
};

static int apollon_panel_probe(struct platform_device *pdev)
{
	omapfb_register_panel(&apollon_panel);
	return 0;
}

static int apollon_panel_remove(struct platform_device *pdev)
{
	return 0;
}

static int apollon_panel_suspend(struct platform_device *pdev,
				  pm_message_t mesg)
{
	return 0;
}

static int apollon_panel_resume(struct platform_device *pdev)
{
	return 0;
}

struct platform_driver apollon_panel_driver = {
	.probe		= apollon_panel_probe,
	.remove		= apollon_panel_remove,
	.suspend	= apollon_panel_suspend,
	.resume		= apollon_panel_resume,
	.driver		= {
		.name	= "apollon_lcd",
		.owner	= THIS_MODULE,
	},
};

static int __init apollon_panel_drv_init(void)
{
	return platform_driver_register(&apollon_panel_driver);
}

static void __exit apollon_panel_drv_exit(void)
{
	platform_driver_unregister(&apollon_panel_driver);
}

module_init(apollon_panel_drv_init);
module_exit(apollon_panel_drv_exit);
