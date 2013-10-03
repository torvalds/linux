/*
 * Backlight driver for OMAP based boards.
 *
 * Copyright (c) 2006 Andrzej Zaborowski  <balrog@zabor.org>
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this package; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/slab.h>
#include <linux/platform_data/omap1_bl.h>

#include <mach/hardware.h>
#include <mach/mux.h>

#define OMAPBL_MAX_INTENSITY		0xff

struct omap_backlight {
	int powermode;
	int current_intensity;

	struct device *dev;
	struct omap_backlight_config *pdata;
};

static inline void omapbl_send_intensity(int intensity)
{
	omap_writeb(intensity, OMAP_PWL_ENABLE);
}

static inline void omapbl_send_enable(int enable)
{
	omap_writeb(enable, OMAP_PWL_CLK_ENABLE);
}

static void omapbl_blank(struct omap_backlight *bl, int mode)
{
	if (bl->pdata->set_power)
		bl->pdata->set_power(bl->dev, mode);

	switch (mode) {
	case FB_BLANK_NORMAL:
	case FB_BLANK_VSYNC_SUSPEND:
	case FB_BLANK_HSYNC_SUSPEND:
	case FB_BLANK_POWERDOWN:
		omapbl_send_intensity(0);
		omapbl_send_enable(0);
		break;

	case FB_BLANK_UNBLANK:
		omapbl_send_intensity(bl->current_intensity);
		omapbl_send_enable(1);
		break;
	}
}

#ifdef CONFIG_PM_SLEEP
static int omapbl_suspend(struct device *dev)
{
	struct backlight_device *bl_dev = dev_get_drvdata(dev);
	struct omap_backlight *bl = bl_get_data(bl_dev);

	omapbl_blank(bl, FB_BLANK_POWERDOWN);
	return 0;
}

static int omapbl_resume(struct device *dev)
{
	struct backlight_device *bl_dev = dev_get_drvdata(dev);
	struct omap_backlight *bl = bl_get_data(bl_dev);

	omapbl_blank(bl, bl->powermode);
	return 0;
}
#endif

static int omapbl_set_power(struct backlight_device *dev, int state)
{
	struct omap_backlight *bl = bl_get_data(dev);

	omapbl_blank(bl, state);
	bl->powermode = state;

	return 0;
}

static int omapbl_update_status(struct backlight_device *dev)
{
	struct omap_backlight *bl = bl_get_data(dev);

	if (bl->current_intensity != dev->props.brightness) {
		if (bl->powermode == FB_BLANK_UNBLANK)
			omapbl_send_intensity(dev->props.brightness);
		bl->current_intensity = dev->props.brightness;
	}

	if (dev->props.fb_blank != bl->powermode)
		omapbl_set_power(dev, dev->props.fb_blank);

	return 0;
}

static int omapbl_get_intensity(struct backlight_device *dev)
{
	struct omap_backlight *bl = bl_get_data(dev);
	return bl->current_intensity;
}

static const struct backlight_ops omapbl_ops = {
	.get_brightness = omapbl_get_intensity,
	.update_status  = omapbl_update_status,
};

static int omapbl_probe(struct platform_device *pdev)
{
	struct backlight_properties props;
	struct backlight_device *dev;
	struct omap_backlight *bl;
	struct omap_backlight_config *pdata = pdev->dev.platform_data;

	if (!pdata)
		return -ENXIO;

	bl = devm_kzalloc(&pdev->dev, sizeof(struct omap_backlight),
			  GFP_KERNEL);
	if (unlikely(!bl))
		return -ENOMEM;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = OMAPBL_MAX_INTENSITY;
	dev = backlight_device_register("omap-bl", &pdev->dev, bl, &omapbl_ops,
					&props);
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	bl->powermode = FB_BLANK_POWERDOWN;
	bl->current_intensity = 0;

	bl->pdata = pdata;
	bl->dev = &pdev->dev;

	platform_set_drvdata(pdev, dev);

	omap_cfg_reg(PWL);	/* Conflicts with UART3 */

	dev->props.fb_blank = FB_BLANK_UNBLANK;
	dev->props.brightness = pdata->default_intensity;
	omapbl_update_status(dev);

	dev_info(&pdev->dev, "OMAP LCD backlight initialised\n");

	return 0;
}

static int omapbl_remove(struct platform_device *pdev)
{
	struct backlight_device *dev = platform_get_drvdata(pdev);

	backlight_device_unregister(dev);

	return 0;
}

static SIMPLE_DEV_PM_OPS(omapbl_pm_ops, omapbl_suspend, omapbl_resume);

static struct platform_driver omapbl_driver = {
	.probe		= omapbl_probe,
	.remove		= omapbl_remove,
	.driver		= {
		.name	= "omap-bl",
		.pm	= &omapbl_pm_ops,
	},
};

module_platform_driver(omapbl_driver);

MODULE_AUTHOR("Andrzej Zaborowski <balrog@zabor.org>");
MODULE_DESCRIPTION("OMAP LCD Backlight driver");
MODULE_LICENSE("GPL");
