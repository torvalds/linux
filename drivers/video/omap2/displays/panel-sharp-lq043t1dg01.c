/*
 * LCD panel driver for Sharp LQ043T1DG01
 *
 * Copyright (C) 2009 Texas Instruments Inc
 * Author: Vaibhav Hiremath <hvaibhav@ti.com>
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
#include <linux/device.h>
#include <linux/err.h>

#include <plat/display.h>

static struct omap_video_timings sharp_lq_timings = {
	.x_res = 480,
	.y_res = 272,

	.pixel_clock	= 9000,

	.hsw		= 42,
	.hfp		= 3,
	.hbp		= 2,

	.vsw		= 11,
	.vfp		= 3,
	.vbp		= 2,
};

static int sharp_lq_panel_power_on(struct omap_dss_device *dssdev)
{
	int r;

	r = omapdss_dpi_display_enable(dssdev);
	if (r)
		goto err0;

	/* wait couple of vsyncs until enabling the LCD */
	msleep(50);

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

static void sharp_lq_panel_power_off(struct omap_dss_device *dssdev)
{
	if (dssdev->platform_disable)
		dssdev->platform_disable(dssdev);

	/* wait at least 5 vsyncs after disabling the LCD */
	msleep(100);

	omapdss_dpi_display_disable(dssdev);
}

static int sharp_lq_panel_probe(struct omap_dss_device *dssdev)
{

	dssdev->panel.config = OMAP_DSS_LCD_TFT | OMAP_DSS_LCD_IVS |
		OMAP_DSS_LCD_IHS | OMAP_DSS_LCD_IEO;
	dssdev->panel.acb = 0x0;
	dssdev->panel.timings = sharp_lq_timings;

	return 0;
}

static void sharp_lq_panel_remove(struct omap_dss_device *dssdev)
{
}

static int sharp_lq_panel_enable(struct omap_dss_device *dssdev)
{
	int r = 0;

	r = sharp_lq_panel_power_on(dssdev);
	if (r)
		return r;

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	return 0;
}

static void sharp_lq_panel_disable(struct omap_dss_device *dssdev)
{
	sharp_lq_panel_power_off(dssdev);

	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;
}

static int sharp_lq_panel_suspend(struct omap_dss_device *dssdev)
{
	sharp_lq_panel_power_off(dssdev);
	dssdev->state = OMAP_DSS_DISPLAY_SUSPENDED;
	return 0;
}

static int sharp_lq_panel_resume(struct omap_dss_device *dssdev)
{
	int r = 0;

	r = sharp_lq_panel_power_on(dssdev);
	if (r)
		return r;

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	return 0;
}

static struct omap_dss_driver sharp_lq_driver = {
	.probe		= sharp_lq_panel_probe,
	.remove		= sharp_lq_panel_remove,

	.enable		= sharp_lq_panel_enable,
	.disable	= sharp_lq_panel_disable,
	.suspend	= sharp_lq_panel_suspend,
	.resume		= sharp_lq_panel_resume,

	.driver         = {
		.name   = "sharp_lq_panel",
		.owner  = THIS_MODULE,
	},
};

static int __init sharp_lq_panel_drv_init(void)
{
	return omap_dss_register_driver(&sharp_lq_driver);
}

static void __exit sharp_lq_panel_drv_exit(void)
{
	omap_dss_unregister_driver(&sharp_lq_driver);
}

module_init(sharp_lq_panel_drv_init);
module_exit(sharp_lq_panel_drv_exit);
MODULE_LICENSE("GPL");
