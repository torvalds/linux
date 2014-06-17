/*
 * Backlight Driver for Dialog DA9052 PMICs
 *
 * Copyright(c) 2012 Dialog Semiconductor Ltd.
 *
 * Author: David Dajun Chen <dchen@diasemi.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <linux/mfd/da9052/da9052.h>
#include <linux/mfd/da9052/reg.h>

#define DA9052_MAX_BRIGHTNESS		0xFF

enum {
	DA9052_WLEDS_OFF,
	DA9052_WLEDS_ON,
};

enum {
	DA9052_TYPE_WLED1,
	DA9052_TYPE_WLED2,
	DA9052_TYPE_WLED3,
};

static const unsigned char wled_bank[] = {
	DA9052_LED1_CONF_REG,
	DA9052_LED2_CONF_REG,
	DA9052_LED3_CONF_REG,
};

struct da9052_bl {
	struct da9052 *da9052;
	uint brightness;
	uint state;
	uint led_reg;
};

static int da9052_adjust_wled_brightness(struct da9052_bl *wleds)
{
	unsigned char boost_en;
	unsigned char i_sink;
	int ret;

	boost_en = 0x3F;
	i_sink = 0xFF;
	if (wleds->state == DA9052_WLEDS_OFF) {
		boost_en = 0x00;
		i_sink = 0x00;
	}

	ret = da9052_reg_write(wleds->da9052, DA9052_BOOST_REG, boost_en);
	if (ret < 0)
		return ret;

	ret = da9052_reg_write(wleds->da9052, DA9052_LED_CONT_REG, i_sink);
	if (ret < 0)
		return ret;

	ret = da9052_reg_write(wleds->da9052, wled_bank[wleds->led_reg], 0x0);
	if (ret < 0)
		return ret;

	usleep_range(10000, 11000);

	if (wleds->brightness) {
		ret = da9052_reg_write(wleds->da9052, wled_bank[wleds->led_reg],
				       wleds->brightness);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int da9052_backlight_update_status(struct backlight_device *bl)
{
	int brightness = bl->props.brightness;
	struct da9052_bl *wleds = bl_get_data(bl);

	wleds->brightness = brightness;
	wleds->state = DA9052_WLEDS_ON;

	return da9052_adjust_wled_brightness(wleds);
}

static int da9052_backlight_get_brightness(struct backlight_device *bl)
{
	struct da9052_bl *wleds = bl_get_data(bl);

	return wleds->brightness;
}

static const struct backlight_ops da9052_backlight_ops = {
	.update_status = da9052_backlight_update_status,
	.get_brightness = da9052_backlight_get_brightness,
};

static int da9052_backlight_probe(struct platform_device *pdev)
{
	struct backlight_device *bl;
	struct backlight_properties props;
	struct da9052_bl *wleds;

	wleds = devm_kzalloc(&pdev->dev, sizeof(struct da9052_bl), GFP_KERNEL);
	if (!wleds)
		return -ENOMEM;

	wleds->da9052 = dev_get_drvdata(pdev->dev.parent);
	wleds->brightness = 0;
	wleds->led_reg = platform_get_device_id(pdev)->driver_data;
	wleds->state = DA9052_WLEDS_OFF;

	props.type = BACKLIGHT_RAW;
	props.max_brightness = DA9052_MAX_BRIGHTNESS;

	bl = devm_backlight_device_register(&pdev->dev, pdev->name,
					wleds->da9052->dev, wleds,
					&da9052_backlight_ops, &props);
	if (IS_ERR(bl)) {
		dev_err(&pdev->dev, "Failed to register backlight\n");
		return PTR_ERR(bl);
	}

	bl->props.max_brightness = DA9052_MAX_BRIGHTNESS;
	bl->props.brightness = 0;
	platform_set_drvdata(pdev, bl);

	return da9052_adjust_wled_brightness(wleds);
}

static int da9052_backlight_remove(struct platform_device *pdev)
{
	struct backlight_device *bl = platform_get_drvdata(pdev);
	struct da9052_bl *wleds = bl_get_data(bl);

	wleds->brightness = 0;
	wleds->state = DA9052_WLEDS_OFF;
	da9052_adjust_wled_brightness(wleds);

	return 0;
}

static struct platform_device_id da9052_wled_ids[] = {
	{
		.name		= "da9052-wled1",
		.driver_data	= DA9052_TYPE_WLED1,
	},
	{
		.name		= "da9052-wled2",
		.driver_data	= DA9052_TYPE_WLED2,
	},
	{
		.name		= "da9052-wled3",
		.driver_data	= DA9052_TYPE_WLED3,
	},
};

static struct platform_driver da9052_wled_driver = {
	.probe		= da9052_backlight_probe,
	.remove		= da9052_backlight_remove,
	.id_table	= da9052_wled_ids,
	.driver	= {
		.name	= "da9052-wled",
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(da9052_wled_driver);

MODULE_AUTHOR("David Dajun Chen <dchen@diasemi.com>");
MODULE_DESCRIPTION("Backlight driver for DA9052 PMIC");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:da9052-backlight");
