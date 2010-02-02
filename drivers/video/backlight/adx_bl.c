/*
 * linux/drivers/video/backlight/adx.c
 *
 * Copyright (C) 2009 Avionic Design GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Written by Thierry Reding <thierry.reding@avionic-design.de>
 */

#include <linux/backlight.h>
#include <linux/fb.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>

/* register definitions */
#define ADX_BACKLIGHT_CONTROL		0x00
#define ADX_BACKLIGHT_CONTROL_ENABLE	(1 << 0)
#define ADX_BACKLIGHT_BRIGHTNESS	0x08
#define ADX_BACKLIGHT_STATUS		0x10
#define ADX_BACKLIGHT_ERROR		0x18

struct adxbl {
	void __iomem *base;
};

static int adx_backlight_update_status(struct backlight_device *bldev)
{
	struct adxbl *bl = bl_get_data(bldev);
	u32 value;

	value = bldev->props.brightness;
	writel(value, bl->base + ADX_BACKLIGHT_BRIGHTNESS);

	value = readl(bl->base + ADX_BACKLIGHT_CONTROL);

	if (bldev->props.state & BL_CORE_FBBLANK)
		value &= ~ADX_BACKLIGHT_CONTROL_ENABLE;
	else
		value |= ADX_BACKLIGHT_CONTROL_ENABLE;

	writel(value, bl->base + ADX_BACKLIGHT_CONTROL);

	return 0;
}

static int adx_backlight_get_brightness(struct backlight_device *bldev)
{
	struct adxbl *bl = bl_get_data(bldev);
	u32 brightness;

	brightness = readl(bl->base + ADX_BACKLIGHT_BRIGHTNESS);
	return brightness & 0xff;
}

static int adx_backlight_check_fb(struct fb_info *fb)
{
	return 1;
}

static const struct backlight_ops adx_backlight_ops = {
	.options = 0,
	.update_status = adx_backlight_update_status,
	.get_brightness = adx_backlight_get_brightness,
	.check_fb = adx_backlight_check_fb,
};

static int __devinit adx_backlight_probe(struct platform_device *pdev)
{
	struct backlight_device *bldev;
	struct resource *res;
	struct adxbl *bl;
	int ret = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -ENXIO;
		goto out;
	}

	res = devm_request_mem_region(&pdev->dev, res->start,
			resource_size(res), res->name);
	if (!res) {
		ret = -ENXIO;
		goto out;
	}

	bl = devm_kzalloc(&pdev->dev, sizeof(*bl), GFP_KERNEL);
	if (!bl) {
		ret = -ENOMEM;
		goto out;
	}

	bl->base = devm_ioremap_nocache(&pdev->dev, res->start,
			resource_size(res));
	if (!bl->base) {
		ret = -ENXIO;
		goto out;
	}

	bldev = backlight_device_register(dev_name(&pdev->dev), &pdev->dev, bl,
			&adx_backlight_ops);
	if (!bldev) {
		ret = -ENOMEM;
		goto out;
	}

	bldev->props.max_brightness = 0xff;
	bldev->props.brightness = 0xff;
	bldev->props.power = FB_BLANK_UNBLANK;

	platform_set_drvdata(pdev, bldev);

out:
	return ret;
}

static int __devexit adx_backlight_remove(struct platform_device *pdev)
{
	struct backlight_device *bldev;
	int ret = 0;

	bldev = platform_get_drvdata(pdev);
	bldev->props.power = FB_BLANK_UNBLANK;
	bldev->props.brightness = 0xff;
	backlight_update_status(bldev);
	backlight_device_unregister(bldev);
	platform_set_drvdata(pdev, NULL);

	return ret;
}

#ifdef CONFIG_PM
static int adx_backlight_suspend(struct platform_device *pdev,
		pm_message_t state)
{
	return 0;
}

static int adx_backlight_resume(struct platform_device *pdev)
{
	return 0;
}
#else
#define adx_backlight_suspend NULL
#define adx_backlight_resume NULL
#endif

static struct platform_driver adx_backlight_driver = {
	.probe = adx_backlight_probe,
	.remove = __devexit_p(adx_backlight_remove),
	.suspend = adx_backlight_suspend,
	.resume = adx_backlight_resume,
	.driver = {
		.name = "adx-backlight",
		.owner = THIS_MODULE,
	},
};

static int __init adx_backlight_init(void)
{
	return platform_driver_register(&adx_backlight_driver);
}

static void __exit adx_backlight_exit(void)
{
	platform_driver_unregister(&adx_backlight_driver);
}

module_init(adx_backlight_init);
module_exit(adx_backlight_exit);

MODULE_AUTHOR("Thierry Reding <thierry.reding@avionic-design.de>");
MODULE_DESCRIPTION("Avionic Design Xanthos Backlight Driver");
MODULE_LICENSE("GPL v2");
