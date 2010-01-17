/*
 * Based on drivers/video/omap/lcd_inn1510.c
 *
 * LCD panel support for the Amstrad E3 (Delta) videophone.
 *
 * Copyright (C) 2006 Jonathan McDowell <noodles@earth.li>
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
#include <linux/io.h>
#include <linux/delay.h>

#include <plat/board-ams-delta.h>
#include <mach/hardware.h>

#include "omapfb.h"

#define AMS_DELTA_DEFAULT_CONTRAST	112

static int ams_delta_panel_init(struct lcd_panel *panel,
		struct omapfb_device *fbdev)
{
	return 0;
}

static void ams_delta_panel_cleanup(struct lcd_panel *panel)
{
}

static int ams_delta_panel_enable(struct lcd_panel *panel)
{
	ams_delta_latch2_write(AMS_DELTA_LATCH2_LCD_NDISP,
			AMS_DELTA_LATCH2_LCD_NDISP);
	ams_delta_latch2_write(AMS_DELTA_LATCH2_LCD_VBLEN,
			AMS_DELTA_LATCH2_LCD_VBLEN);

	omap_writeb(1, OMAP_PWL_CLK_ENABLE);
	omap_writeb(AMS_DELTA_DEFAULT_CONTRAST, OMAP_PWL_ENABLE);

	return 0;
}

static void ams_delta_panel_disable(struct lcd_panel *panel)
{
	ams_delta_latch2_write(AMS_DELTA_LATCH2_LCD_VBLEN, 0);
	ams_delta_latch2_write(AMS_DELTA_LATCH2_LCD_NDISP, 0);
}

static unsigned long ams_delta_panel_get_caps(struct lcd_panel *panel)
{
	return 0;
}

static struct lcd_panel ams_delta_panel = {
	.name		= "ams-delta",
	.config		= 0,

	.bpp		= 12,
	.data_lines	= 16,
	.x_res		= 480,
	.y_res		= 320,
	.pixel_clock	= 4687,
	.hsw		= 3,
	.hfp		= 1,
	.hbp		= 1,
	.vsw		= 1,
	.vfp		= 0,
	.vbp		= 0,
	.pcd		= 0,
	.acb		= 37,

	.init		= ams_delta_panel_init,
	.cleanup	= ams_delta_panel_cleanup,
	.enable		= ams_delta_panel_enable,
	.disable	= ams_delta_panel_disable,
	.get_caps	= ams_delta_panel_get_caps,
};

static int ams_delta_panel_probe(struct platform_device *pdev)
{
	omapfb_register_panel(&ams_delta_panel);
	return 0;
}

static int ams_delta_panel_remove(struct platform_device *pdev)
{
	return 0;
}

static int ams_delta_panel_suspend(struct platform_device *pdev,
		pm_message_t mesg)
{
	return 0;
}

static int ams_delta_panel_resume(struct platform_device *pdev)
{
	return 0;
}

struct platform_driver ams_delta_panel_driver = {
	.probe		= ams_delta_panel_probe,
	.remove		= ams_delta_panel_remove,
	.suspend	= ams_delta_panel_suspend,
	.resume		= ams_delta_panel_resume,
	.driver		= {
		.name	= "lcd_ams_delta",
		.owner	= THIS_MODULE,
	},
};

static int __init ams_delta_panel_drv_init(void)
{
	return platform_driver_register(&ams_delta_panel_driver);
}

static void __exit ams_delta_panel_drv_cleanup(void)
{
	platform_driver_unregister(&ams_delta_panel_driver);
}

module_init(ams_delta_panel_drv_init);
module_exit(ams_delta_panel_drv_cleanup);
