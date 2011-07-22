/*
 * LCD panel driver for Sharp LS037V7DW01
 *
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
#include <linux/device.h>
#include <linux/backlight.h>
#include <linux/fb.h>
#include <linux/err.h>
#include <linux/slab.h>

#include <video/omapdss.h>

struct sharp_data {
	struct backlight_device *bl;
};

static struct omap_video_timings sharp_ls_timings = {
	.x_res = 480,
	.y_res = 640,

	.pixel_clock	= 19200,

	.hsw		= 2,
	.hfp		= 1,
	.hbp		= 28,

	.vsw		= 1,
	.vfp		= 1,
	.vbp		= 1,
};

static int sharp_ls_bl_update_status(struct backlight_device *bl)
{
	struct omap_dss_device *dssdev = dev_get_drvdata(&bl->dev);
	int level;

	if (!dssdev->set_backlight)
		return -EINVAL;

	if (bl->props.fb_blank == FB_BLANK_UNBLANK &&
			bl->props.power == FB_BLANK_UNBLANK)
		level = bl->props.brightness;
	else
		level = 0;

	return dssdev->set_backlight(dssdev, level);
}

static int sharp_ls_bl_get_brightness(struct backlight_device *bl)
{
	if (bl->props.fb_blank == FB_BLANK_UNBLANK &&
			bl->props.power == FB_BLANK_UNBLANK)
		return bl->props.brightness;

	return 0;
}

static const struct backlight_ops sharp_ls_bl_ops = {
	.get_brightness = sharp_ls_bl_get_brightness,
	.update_status  = sharp_ls_bl_update_status,
};



static int sharp_ls_panel_probe(struct omap_dss_device *dssdev)
{
	struct backlight_properties props;
	struct backlight_device *bl;
	struct sharp_data *sd;
	int r;

	dssdev->panel.config = OMAP_DSS_LCD_TFT | OMAP_DSS_LCD_IVS |
		OMAP_DSS_LCD_IHS;
	dssdev->panel.acb = 0x28;
	dssdev->panel.timings = sharp_ls_timings;

	sd = kzalloc(sizeof(*sd), GFP_KERNEL);
	if (!sd)
		return -ENOMEM;

	dev_set_drvdata(&dssdev->dev, sd);

	memset(&props, 0, sizeof(struct backlight_properties));
	props.max_brightness = dssdev->max_backlight_level;
	props.type = BACKLIGHT_RAW;

	bl = backlight_device_register("sharp-ls", &dssdev->dev, dssdev,
			&sharp_ls_bl_ops, &props);
	if (IS_ERR(bl)) {
		r = PTR_ERR(bl);
		kfree(sd);
		return r;
	}
	sd->bl = bl;

	bl->props.fb_blank = FB_BLANK_UNBLANK;
	bl->props.power = FB_BLANK_UNBLANK;
	bl->props.brightness = dssdev->max_backlight_level;
	r = sharp_ls_bl_update_status(bl);
	if (r < 0)
		dev_err(&dssdev->dev, "failed to set lcd brightness\n");

	return 0;
}

static void __exit sharp_ls_panel_remove(struct omap_dss_device *dssdev)
{
	struct sharp_data *sd = dev_get_drvdata(&dssdev->dev);
	struct backlight_device *bl = sd->bl;

	bl->props.power = FB_BLANK_POWERDOWN;
	sharp_ls_bl_update_status(bl);
	backlight_device_unregister(bl);

	kfree(sd);
}

static int sharp_ls_power_on(struct omap_dss_device *dssdev)
{
	int r = 0;

	if (dssdev->state == OMAP_DSS_DISPLAY_ACTIVE)
		return 0;

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

static void sharp_ls_power_off(struct omap_dss_device *dssdev)
{
	if (dssdev->state != OMAP_DSS_DISPLAY_ACTIVE)
		return;

	if (dssdev->platform_disable)
		dssdev->platform_disable(dssdev);

	/* wait at least 5 vsyncs after disabling the LCD */

	msleep(100);

	omapdss_dpi_display_disable(dssdev);
}

static int sharp_ls_panel_enable(struct omap_dss_device *dssdev)
{
	int r;
	r = sharp_ls_power_on(dssdev);
	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;
	return r;
}

static void sharp_ls_panel_disable(struct omap_dss_device *dssdev)
{
	sharp_ls_power_off(dssdev);
	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;
}

static int sharp_ls_panel_suspend(struct omap_dss_device *dssdev)
{
	sharp_ls_power_off(dssdev);
	dssdev->state = OMAP_DSS_DISPLAY_SUSPENDED;
	return 0;
}

static int sharp_ls_panel_resume(struct omap_dss_device *dssdev)
{
	int r;
	r = sharp_ls_power_on(dssdev);
	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;
	return r;
}

static struct omap_dss_driver sharp_ls_driver = {
	.probe		= sharp_ls_panel_probe,
	.remove		= __exit_p(sharp_ls_panel_remove),

	.enable		= sharp_ls_panel_enable,
	.disable	= sharp_ls_panel_disable,
	.suspend	= sharp_ls_panel_suspend,
	.resume		= sharp_ls_panel_resume,

	.driver         = {
		.name   = "sharp_ls_panel",
		.owner  = THIS_MODULE,
	},
};

static int __init sharp_ls_panel_drv_init(void)
{
	return omap_dss_register_driver(&sharp_ls_driver);
}

static void __exit sharp_ls_panel_drv_exit(void)
{
	omap_dss_unregister_driver(&sharp_ls_driver);
}

module_init(sharp_ls_panel_drv_init);
module_exit(sharp_ls_panel_drv_exit);
MODULE_LICENSE("GPL");
