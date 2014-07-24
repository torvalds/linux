/*
 * LED Driver for Dialog DA9052 PMICs.
 *
 * Copyright(c) 2012 Dialog Semiconductor Ltd.
 *
 * Author: David Dajun Chen <dchen@diasemi.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/workqueue.h>
#include <linux/slab.h>

#include <linux/mfd/da9052/reg.h>
#include <linux/mfd/da9052/da9052.h>
#include <linux/mfd/da9052/pdata.h>

#define DA9052_OPENDRAIN_OUTPUT	2
#define DA9052_SET_HIGH_LVL_OUTPUT	(1 << 3)
#define DA9052_MASK_UPPER_NIBBLE	0xF0
#define DA9052_MASK_LOWER_NIBBLE	0x0F
#define DA9052_NIBBLE_SHIFT		4
#define DA9052_MAX_BRIGHTNESS		0x5f

struct da9052_led {
	struct led_classdev cdev;
	struct work_struct work;
	struct da9052 *da9052;
	unsigned char led_index;
	unsigned char id;
	int brightness;
};

static unsigned char led_reg[] = {
	DA9052_LED_CONT_4_REG,
	DA9052_LED_CONT_5_REG,
};

static int da9052_set_led_brightness(struct da9052_led *led)
{
	u8 val;
	int error;

	val = (led->brightness & 0x7f) | DA9052_LED_CONT_DIM;

	error = da9052_reg_write(led->da9052, led_reg[led->led_index], val);
	if (error < 0)
		dev_err(led->da9052->dev, "Failed to set led brightness, %d\n",
			error);
	return error;
}

static void da9052_led_work(struct work_struct *work)
{
	struct da9052_led *led = container_of(work, struct da9052_led, work);

	da9052_set_led_brightness(led);
}

static void da9052_led_set(struct led_classdev *led_cdev,
			   enum led_brightness value)
{
	struct da9052_led *led;

	led = container_of(led_cdev, struct da9052_led, cdev);
	led->brightness = value;
	schedule_work(&led->work);
}

static int da9052_configure_leds(struct da9052 *da9052)
{
	int error;
	unsigned char register_value = DA9052_OPENDRAIN_OUTPUT
				       | DA9052_SET_HIGH_LVL_OUTPUT;

	error = da9052_reg_update(da9052, DA9052_GPIO_14_15_REG,
				  DA9052_MASK_LOWER_NIBBLE,
				  register_value);

	if (error < 0) {
		dev_err(da9052->dev, "Failed to write GPIO 14-15 reg, %d\n",
			error);
		return error;
	}

	error = da9052_reg_update(da9052, DA9052_GPIO_14_15_REG,
				  DA9052_MASK_UPPER_NIBBLE,
				  register_value << DA9052_NIBBLE_SHIFT);
	if (error < 0)
		dev_err(da9052->dev, "Failed to write GPIO 14-15 reg, %d\n",
			error);

	return error;
}

static int da9052_led_probe(struct platform_device *pdev)
{
	struct da9052_pdata *pdata;
	struct da9052 *da9052;
	struct led_platform_data *pled;
	struct da9052_led *led = NULL;
	int error = -ENODEV;
	int i;

	da9052 = dev_get_drvdata(pdev->dev.parent);
	pdata = dev_get_platdata(da9052->dev);
	if (pdata == NULL) {
		dev_err(&pdev->dev, "No platform data\n");
		goto err;
	}

	pled = pdata->pled;
	if (pled == NULL) {
		dev_err(&pdev->dev, "No platform data for LED\n");
		goto err;
	}

	led = devm_kzalloc(&pdev->dev,
			   sizeof(struct da9052_led) * pled->num_leds,
			   GFP_KERNEL);
	if (!led) {
		error = -ENOMEM;
		goto err;
	}

	for (i = 0; i < pled->num_leds; i++) {
		led[i].cdev.name = pled->leds[i].name;
		led[i].cdev.brightness_set = da9052_led_set;
		led[i].cdev.brightness = LED_OFF;
		led[i].cdev.max_brightness = DA9052_MAX_BRIGHTNESS;
		led[i].brightness = LED_OFF;
		led[i].led_index = pled->leds[i].flags;
		led[i].da9052 = dev_get_drvdata(pdev->dev.parent);
		INIT_WORK(&led[i].work, da9052_led_work);

		error = led_classdev_register(pdev->dev.parent, &led[i].cdev);
		if (error) {
			dev_err(&pdev->dev, "Failed to register led %d\n",
				led[i].led_index);
			goto err_register;
		}

		error = da9052_set_led_brightness(&led[i]);
		if (error) {
			dev_err(&pdev->dev, "Unable to init led %d\n",
				led[i].led_index);
			continue;
		}
	}
	error = da9052_configure_leds(led->da9052);
	if (error) {
		dev_err(&pdev->dev, "Failed to configure GPIO LED%d\n", error);
		goto err_register;
	}

	platform_set_drvdata(pdev, led);

	return 0;

err_register:
	for (i = i - 1; i >= 0; i--) {
		led_classdev_unregister(&led[i].cdev);
		cancel_work_sync(&led[i].work);
	}
err:
	return error;
}

static int da9052_led_remove(struct platform_device *pdev)
{
	struct da9052_led *led = platform_get_drvdata(pdev);
	struct da9052_pdata *pdata;
	struct da9052 *da9052;
	struct led_platform_data *pled;
	int i;

	da9052 = dev_get_drvdata(pdev->dev.parent);
	pdata = dev_get_platdata(da9052->dev);
	pled = pdata->pled;

	for (i = 0; i < pled->num_leds; i++) {
		led[i].brightness = 0;
		da9052_set_led_brightness(&led[i]);
		led_classdev_unregister(&led[i].cdev);
		cancel_work_sync(&led[i].work);
	}

	return 0;
}

static struct platform_driver da9052_led_driver = {
	.driver		= {
		.name	= "da9052-leds",
		.owner	= THIS_MODULE,
	},
	.probe		= da9052_led_probe,
	.remove		= da9052_led_remove,
};

module_platform_driver(da9052_led_driver);

MODULE_AUTHOR("Dialog Semiconductor Ltd <dchen@diasemi.com>");
MODULE_DESCRIPTION("LED driver for Dialog DA9052 PMIC");
MODULE_LICENSE("GPL");
