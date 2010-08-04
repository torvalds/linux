/*
 * LCD panel driver for Toppoly TDO35S
 *
 * Copyright (C) 2009 CompuLab, Ltd.
 * Author: Mike Rapoport <mike@compulab.co.il>
 *
 * Based on generic panel support
 * Copyright (C) 2008 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/delay.h>

#include <plat/display.h>

static struct omap_video_timings toppoly_tdo_panel_timings = {
	/* 640 x 480 @ 60 Hz  Reduced blanking VESA CVT 0.31M3-R */
	.x_res		= 480,
	.y_res		= 640,

	.pixel_clock	= 26000,

	.hfp		= 104,
	.hsw		= 8,
	.hbp		= 8,

	.vfp		= 4,
	.vsw		= 2,
	.vbp		= 2,
};

static int toppoly_tdo_panel_power_on(struct omap_dss_device *dssdev)
{
	int r;

	r = omapdss_dpi_display_enable(dssdev);
	if (r)
		goto err0;

	if (dssdev->platform_enable) {
		r = dssdev->platform_enable(dssdev);
		if (r)
			goto err1;
	}

	return 0;
err1:
	omapdss_dpi_display_disable(dssdev);
err0:
	return r;
}

static void toppoly_tdo_panel_power_off(struct omap_dss_device *dssdev)
{
	if (dssdev->platform_disable)
		dssdev->platform_disable(dssdev);

	omapdss_dpi_display_disable(dssdev);
}

static int toppoly_tdo_panel_probe(struct omap_dss_device *dssdev)
{
	dssdev->panel.config = OMAP_DSS_LCD_TFT | OMAP_DSS_LCD_IVS |
		OMAP_DSS_LCD_IHS;
	dssdev->panel.timings = toppoly_tdo_panel_timings;

	return 0;
}

static void toppoly_tdo_panel_remove(struct omap_dss_device *dssdev)
{
}

static int toppoly_tdo_panel_enable(struct omap_dss_device *dssdev)
{
	int r = 0;

	r = toppoly_tdo_panel_power_on(dssdev);
	if (r)
		return r;

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	return 0;
}

static void toppoly_tdo_panel_disable(struct omap_dss_device *dssdev)
{
	toppoly_tdo_panel_power_off(dssdev);

	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;
}

static int toppoly_tdo_panel_suspend(struct omap_dss_device *dssdev)
{
	toppoly_tdo_panel_power_off(dssdev);
	dssdev->state = OMAP_DSS_DISPLAY_SUSPENDED;
	return 0;
}

static int toppoly_tdo_panel_resume(struct omap_dss_device *dssdev)
{
	int r = 0;

	r = toppoly_tdo_panel_power_on(dssdev);
	if (r)
		return r;

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	return 0;
}

static struct omap_dss_driver generic_driver = {
	.probe		= toppoly_tdo_panel_probe,
	.remove		= toppoly_tdo_panel_remove,

	.enable		= toppoly_tdo_panel_enable,
	.disable	= toppoly_tdo_panel_disable,
	.suspend	= toppoly_tdo_panel_suspend,
	.resume		= toppoly_tdo_panel_resume,

	.driver         = {
		.name   = "toppoly_tdo35s_panel",
		.owner  = THIS_MODULE,
	},
};

static int __init toppoly_tdo_panel_drv_init(void)
{
	return omap_dss_register_driver(&generic_driver);
}

static void __exit toppoly_tdo_panel_drv_exit(void)
{
	omap_dss_unregister_driver(&generic_driver);
}

module_init(toppoly_tdo_panel_drv_init);
module_exit(toppoly_tdo_panel_drv_exit);
MODULE_LICENSE("GPL");
