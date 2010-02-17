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

#include "omapfb.h"

static int htcherald_panel_init(struct lcd_panel *panel,
					struct omapfb_device *fbdev)
{
	return 0;
}

static void htcherald_panel_cleanup(struct lcd_panel *panel)
{
}

static int htcherald_panel_enable(struct lcd_panel *panel)
{
	return 0;
}

static void htcherald_panel_disable(struct lcd_panel *panel)
{
}

static unsigned long htcherald_panel_get_caps(struct lcd_panel *panel)
{
	return 0;
}

/* Found on WIZ200 (miknix) and some HERA110 models (darkstar62) */
struct lcd_panel htcherald_panel_1 = {
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

	.init		= htcherald_panel_init,
	.cleanup	= htcherald_panel_cleanup,
	.enable		= htcherald_panel_enable,
	.disable	= htcherald_panel_disable,
	.get_caps	= htcherald_panel_get_caps,
};

static int htcherald_panel_probe(struct platform_device *pdev)
{
	omapfb_register_panel(&htcherald_panel_1);
	return 0;
}

static int htcherald_panel_remove(struct platform_device *pdev)
{
	return 0;
}

static int htcherald_panel_suspend(struct platform_device *pdev,
						pm_message_t mesg)
{
	return 0;
}

static int htcherald_panel_resume(struct platform_device *pdev)
{
	return 0;
}

struct platform_driver htcherald_panel_driver = {
	.probe		= htcherald_panel_probe,
	.remove		= htcherald_panel_remove,
	.suspend	= htcherald_panel_suspend,
	.resume		= htcherald_panel_resume,
	.driver		= {
		.name	= "lcd_htcherald",
		.owner	= THIS_MODULE,
	},
};

static int __init htcherald_panel_drv_init(void)
{
	return platform_driver_register(&htcherald_panel_driver);
}

static void __exit htcherald_panel_drv_cleanup(void)
{
	platform_driver_unregister(&htcherald_panel_driver);
}

module_init(htcherald_panel_drv_init);
module_exit(htcherald_panel_drv_cleanup);

