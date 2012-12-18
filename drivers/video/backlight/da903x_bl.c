/*
 * Backlight driver for Dialog Semiconductor DA9030/DA9034
 *
 * Copyright (C) 2008 Compulab, Ltd.
 *	Mike Rapoport <mike@compulab.co.il>
 *
 * Copyright (C) 2006-2008 Marvell International Ltd.
 *	Eric Miao <eric.miao@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/mfd/da903x.h>
#include <linux/slab.h>
#include <linux/module.h>

#define DA9030_WLED_CONTROL	0x25
#define DA9030_WLED_CP_EN	(1 << 6)
#define DA9030_WLED_TRIM(x)	((x) & 0x7)

#define DA9034_WLED_CONTROL1	0x3C
#define DA9034_WLED_CONTROL2	0x3D
#define DA9034_WLED_ISET(x)	((x) & 0x1f)

#define DA9034_WLED_BOOST_EN	(1 << 5)

#define DA9030_MAX_BRIGHTNESS	7
#define DA9034_MAX_BRIGHTNESS	0x7f

struct da903x_backlight_data {
	struct device *da903x_dev;
	int id;
	int current_brightness;
};

static int da903x_backlight_set(struct backlight_device *bl, int brightness)
{
	struct da903x_backlight_data *data = bl_get_data(bl);
	struct device *dev = data->da903x_dev;
	uint8_t val;
	int ret = 0;

	switch (data->id) {
	case DA9034_ID_WLED:
		ret = da903x_update(dev, DA9034_WLED_CONTROL1,
				brightness, 0x7f);
		if (ret)
			return ret;

		if (data->current_brightness && brightness == 0)
			ret = da903x_clr_bits(dev,
					DA9034_WLED_CONTROL2,
					DA9034_WLED_BOOST_EN);

		if (data->current_brightness == 0 && brightness)
			ret = da903x_set_bits(dev,
					DA9034_WLED_CONTROL2,
					DA9034_WLED_BOOST_EN);
		break;
	case DA9030_ID_WLED:
		val = DA9030_WLED_TRIM(brightness);
		val |= brightness ? DA9030_WLED_CP_EN : 0;
		ret = da903x_write(dev, DA9030_WLED_CONTROL, val);
		break;
	}

	if (ret)
		return ret;

	data->current_brightness = brightness;
	return 0;
}

static int da903x_backlight_update_status(struct backlight_device *bl)
{
	int brightness = bl->props.brightness;

	if (bl->props.power != FB_BLANK_UNBLANK)
		brightness = 0;

	if (bl->props.fb_blank != FB_BLANK_UNBLANK)
		brightness = 0;

	return da903x_backlight_set(bl, brightness);
}

static int da903x_backlight_get_brightness(struct backlight_device *bl)
{
	struct da903x_backlight_data *data = bl_get_data(bl);
	return data->current_brightness;
}

static const struct backlight_ops da903x_backlight_ops = {
	.update_status	= da903x_backlight_update_status,
	.get_brightness	= da903x_backlight_get_brightness,
};

static int da903x_backlight_probe(struct platform_device *pdev)
{
	struct da9034_backlight_pdata *pdata = pdev->dev.platform_data;
	struct da903x_backlight_data *data;
	struct backlight_device *bl;
	struct backlight_properties props;
	int max_brightness;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	switch (pdev->id) {
	case DA9030_ID_WLED:
		max_brightness = DA9030_MAX_BRIGHTNESS;
		break;
	case DA9034_ID_WLED:
		max_brightness = DA9034_MAX_BRIGHTNESS;
		break;
	default:
		dev_err(&pdev->dev, "invalid backlight device ID(%d)\n",
				pdev->id);
		return -EINVAL;
	}

	data->id = pdev->id;
	data->da903x_dev = pdev->dev.parent;
	data->current_brightness = 0;

	/* adjust the WLED output current */
	if (pdata)
		da903x_write(data->da903x_dev, DA9034_WLED_CONTROL2,
				DA9034_WLED_ISET(pdata->output_current));

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = max_brightness;
	bl = backlight_device_register(pdev->name, data->da903x_dev, data,
				       &da903x_backlight_ops, &props);
	if (IS_ERR(bl)) {
		dev_err(&pdev->dev, "failed to register backlight\n");
		return PTR_ERR(bl);
	}

	bl->props.brightness = max_brightness;

	platform_set_drvdata(pdev, bl);
	backlight_update_status(bl);
	return 0;
}

static int da903x_backlight_remove(struct platform_device *pdev)
{
	struct backlight_device *bl = platform_get_drvdata(pdev);

	backlight_device_unregister(bl);
	return 0;
}

#ifdef CONFIG_PM
static int da903x_backlight_suspend(struct device *dev)
{
	struct backlight_device *bl = dev_get_drvdata(dev);

	return da903x_backlight_set(bl, 0);
}

static int da903x_backlight_resume(struct device *dev)
{
	struct backlight_device *bl = dev_get_drvdata(dev);

	backlight_update_status(bl);
	return 0;
}

static const struct dev_pm_ops da903x_backlight_pm_ops = {
	.suspend	= da903x_backlight_suspend,
	.resume		= da903x_backlight_resume,
};
#endif

static struct platform_driver da903x_backlight_driver = {
	.driver		= {
		.name	= "da903x-backlight",
		.owner	= THIS_MODULE,
#ifdef CONFIG_PM
		.pm	= &da903x_backlight_pm_ops,
#endif
	},
	.probe		= da903x_backlight_probe,
	.remove		= da903x_backlight_remove,
};

module_platform_driver(da903x_backlight_driver);

MODULE_DESCRIPTION("Backlight Driver for Dialog Semiconductor DA9030/DA9034");
MODULE_AUTHOR("Eric Miao <eric.miao@marvell.com>");
MODULE_AUTHOR("Mike Rapoport <mike@compulab.co.il>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:da903x-backlight");
