// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/drivers/video/backlight/aat2870_bl.c
 *
 * Copyright (c) 2011, NVIDIA Corporation.
 * Author: Jin Park <jinyoungp@nvidia.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/mfd/aat2870.h>

struct aat2870_bl_driver_data {
	struct platform_device *pdev;
	struct backlight_device *bd;

	int channels;
	int max_current;
	int brightness; /* current brightness */
};

static inline int aat2870_brightness(struct aat2870_bl_driver_data *aat2870_bl,
				     int brightness)
{
	struct backlight_device *bd = aat2870_bl->bd;
	int val;

	val = brightness * (aat2870_bl->max_current - 1);
	val /= bd->props.max_brightness;

	return val;
}

static inline int aat2870_bl_enable(struct aat2870_bl_driver_data *aat2870_bl)
{
	struct aat2870_data *aat2870
			= dev_get_drvdata(aat2870_bl->pdev->dev.parent);

	return aat2870->write(aat2870, AAT2870_BL_CH_EN,
			      (u8)aat2870_bl->channels);
}

static inline int aat2870_bl_disable(struct aat2870_bl_driver_data *aat2870_bl)
{
	struct aat2870_data *aat2870
			= dev_get_drvdata(aat2870_bl->pdev->dev.parent);

	return aat2870->write(aat2870, AAT2870_BL_CH_EN, 0x0);
}

static int aat2870_bl_update_status(struct backlight_device *bd)
{
	struct aat2870_bl_driver_data *aat2870_bl = bl_get_data(bd);
	struct aat2870_data *aat2870 =
			dev_get_drvdata(aat2870_bl->pdev->dev.parent);
	int brightness = bd->props.brightness;
	int ret;

	if ((brightness < 0) || (bd->props.max_brightness < brightness)) {
		dev_err(&bd->dev, "invalid brightness, %d\n", brightness);
		return -EINVAL;
	}

	dev_dbg(&bd->dev, "brightness=%d, power=%d, state=%d\n",
		 bd->props.brightness, bd->props.power, bd->props.state);

	if ((bd->props.power != FB_BLANK_UNBLANK) ||
			(bd->props.state & BL_CORE_FBBLANK) ||
			(bd->props.state & BL_CORE_SUSPENDED))
		brightness = 0;

	ret = aat2870->write(aat2870, AAT2870_BLM,
			     (u8)aat2870_brightness(aat2870_bl, brightness));
	if (ret < 0)
		return ret;

	if (brightness == 0) {
		ret = aat2870_bl_disable(aat2870_bl);
		if (ret < 0)
			return ret;
	} else if (aat2870_bl->brightness == 0) {
		ret = aat2870_bl_enable(aat2870_bl);
		if (ret < 0)
			return ret;
	}

	aat2870_bl->brightness = brightness;

	return 0;
}

static int aat2870_bl_check_fb(struct backlight_device *bd, struct fb_info *fi)
{
	return 1;
}

static const struct backlight_ops aat2870_bl_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.update_status = aat2870_bl_update_status,
	.check_fb = aat2870_bl_check_fb,
};

static int aat2870_bl_probe(struct platform_device *pdev)
{
	struct aat2870_bl_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct aat2870_bl_driver_data *aat2870_bl;
	struct backlight_device *bd;
	struct backlight_properties props;
	int ret = 0;

	if (!pdata) {
		dev_err(&pdev->dev, "No platform data\n");
		ret = -ENXIO;
		goto out;
	}

	if (pdev->id != AAT2870_ID_BL) {
		dev_err(&pdev->dev, "Invalid device ID, %d\n", pdev->id);
		ret = -EINVAL;
		goto out;
	}

	aat2870_bl = devm_kzalloc(&pdev->dev,
				  sizeof(struct aat2870_bl_driver_data),
				  GFP_KERNEL);
	if (!aat2870_bl) {
		ret = -ENOMEM;
		goto out;
	}

	memset(&props, 0, sizeof(struct backlight_properties));

	props.type = BACKLIGHT_RAW;
	bd = devm_backlight_device_register(&pdev->dev, "aat2870-backlight",
					&pdev->dev, aat2870_bl, &aat2870_bl_ops,
					&props);
	if (IS_ERR(bd)) {
		dev_err(&pdev->dev,
			"Failed allocate memory for backlight device\n");
		ret = PTR_ERR(bd);
		goto out;
	}

	aat2870_bl->pdev = pdev;
	platform_set_drvdata(pdev, aat2870_bl);

	aat2870_bl->bd = bd;

	if (pdata->channels > 0)
		aat2870_bl->channels = pdata->channels;
	else
		aat2870_bl->channels = AAT2870_BL_CH_ALL;

	if (pdata->max_current > 0)
		aat2870_bl->max_current = pdata->max_current;
	else
		aat2870_bl->max_current = AAT2870_CURRENT_27_9;

	if (pdata->max_brightness > 0)
		bd->props.max_brightness = pdata->max_brightness;
	else
		bd->props.max_brightness = 255;

	aat2870_bl->brightness = 0;
	bd->props.power = FB_BLANK_UNBLANK;
	bd->props.brightness = bd->props.max_brightness;

	ret = aat2870_bl_update_status(bd);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to initialize\n");
		return ret;
	}

	return 0;

out:
	return ret;
}

static int aat2870_bl_remove(struct platform_device *pdev)
{
	struct aat2870_bl_driver_data *aat2870_bl = platform_get_drvdata(pdev);
	struct backlight_device *bd = aat2870_bl->bd;

	bd->props.power = FB_BLANK_POWERDOWN;
	bd->props.brightness = 0;
	backlight_update_status(bd);

	return 0;
}

static struct platform_driver aat2870_bl_driver = {
	.driver = {
		.name	= "aat2870-backlight",
	},
	.probe		= aat2870_bl_probe,
	.remove		= aat2870_bl_remove,
};

static int __init aat2870_bl_init(void)
{
	return platform_driver_register(&aat2870_bl_driver);
}
subsys_initcall(aat2870_bl_init);

static void __exit aat2870_bl_exit(void)
{
	platform_driver_unregister(&aat2870_bl_driver);
}
module_exit(aat2870_bl_exit);

MODULE_DESCRIPTION("AnalogicTech AAT2870 Backlight");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jin Park <jinyoungp@nvidia.com>");
