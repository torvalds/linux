// SPDX-License-Identifier: GPL-2.0-only
/* drivers/video/backlight/platform_lcd.c
 *
 * Copyright 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * Generic platform-device LCD power control interface.
*/

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/lcd.h>
#include <linux/slab.h>

#include <video/platform_lcd.h>

struct platform_lcd {
	struct device		*us;
	struct lcd_device	*lcd;
	struct plat_lcd_data	*pdata;

	unsigned int		 power;
	unsigned int		 suspended:1;
};

static inline struct platform_lcd *to_our_lcd(struct lcd_device *lcd)
{
	return lcd_get_data(lcd);
}

static int platform_lcd_get_power(struct lcd_device *lcd)
{
	struct platform_lcd *plcd = to_our_lcd(lcd);

	return plcd->power;
}

static int platform_lcd_set_power(struct lcd_device *lcd, int power)
{
	struct platform_lcd *plcd = to_our_lcd(lcd);
	int lcd_power = 1;

	if (power == FB_BLANK_POWERDOWN || plcd->suspended)
		lcd_power = 0;

	plcd->pdata->set_power(plcd->pdata, lcd_power);
	plcd->power = power;

	return 0;
}

static int platform_lcd_match(struct lcd_device *lcd, struct fb_info *info)
{
	struct platform_lcd *plcd = to_our_lcd(lcd);
	struct plat_lcd_data *pdata = plcd->pdata;

	if (pdata->match_fb)
		return pdata->match_fb(pdata, info);

	return plcd->us->parent == info->device;
}

static const struct lcd_ops platform_lcd_ops = {
	.get_power	= platform_lcd_get_power,
	.set_power	= platform_lcd_set_power,
	.check_fb	= platform_lcd_match,
};

static int platform_lcd_probe(struct platform_device *pdev)
{
	struct plat_lcd_data *pdata;
	struct platform_lcd *plcd;
	struct device *dev = &pdev->dev;
	int err;

	pdata = dev_get_platdata(&pdev->dev);
	if (!pdata) {
		dev_err(dev, "no platform data supplied\n");
		return -EINVAL;
	}

	if (pdata->probe) {
		err = pdata->probe(pdata);
		if (err)
			return err;
	}

	plcd = devm_kzalloc(&pdev->dev, sizeof(struct platform_lcd),
			    GFP_KERNEL);
	if (!plcd)
		return -ENOMEM;

	plcd->us = dev;
	plcd->pdata = pdata;
	plcd->lcd = devm_lcd_device_register(&pdev->dev, dev_name(dev), dev,
						plcd, &platform_lcd_ops);
	if (IS_ERR(plcd->lcd)) {
		dev_err(dev, "cannot register lcd device\n");
		return PTR_ERR(plcd->lcd);
	}

	platform_set_drvdata(pdev, plcd);
	platform_lcd_set_power(plcd->lcd, FB_BLANK_NORMAL);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int platform_lcd_suspend(struct device *dev)
{
	struct platform_lcd *plcd = dev_get_drvdata(dev);

	plcd->suspended = 1;
	platform_lcd_set_power(plcd->lcd, plcd->power);

	return 0;
}

static int platform_lcd_resume(struct device *dev)
{
	struct platform_lcd *plcd = dev_get_drvdata(dev);

	plcd->suspended = 0;
	platform_lcd_set_power(plcd->lcd, plcd->power);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(platform_lcd_pm_ops, platform_lcd_suspend,
			platform_lcd_resume);

static struct platform_driver platform_lcd_driver = {
	.driver		= {
		.name	= "platform-lcd",
		.pm	= &platform_lcd_pm_ops,
	},
	.probe		= platform_lcd_probe,
};

module_platform_driver(platform_lcd_driver);

MODULE_AUTHOR("Ben Dooks <ben-linux@fluff.org>");
MODULE_DESCRIPTION("Generic platform-device LCD power control interface");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:platform-lcd");
