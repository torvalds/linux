/*
 * Copyright (C) 2012 Bachmann electronic GmbH
 *	Christian Gmeiner <christian.gmeiner@gmail.com>
 *
 * Backlight driver for ot200 visualisation device from
 * Bachmann electronic GmbH.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/gpio.h>
#include <linux/cs5535.h>

static struct cs5535_mfgpt_timer *pwm_timer;

/* this array defines the mapping of brightness in % to pwm frequency */
static const u8 dim_table[101] = {0, 0, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2,
				  2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4,
				  4, 5, 5, 5, 5, 6, 6, 6, 7, 7, 7, 8, 8, 9, 9,
				  10, 10, 11, 11, 12, 12, 13, 14, 15, 15, 16,
				  17, 18, 19, 20, 21, 22, 23, 24, 26, 27, 28,
				  30, 31, 33, 35, 37, 39, 41, 43, 45, 47, 50,
				  53, 55, 58, 61, 65, 68, 72, 75, 79, 84, 88,
				  93, 97, 103, 108, 114, 120, 126, 133, 140,
				  147, 155, 163};

struct ot200_backlight_data {
	int current_brightness;
};

#define GPIO_DIMM	27
#define SCALE		1
#define CMP1MODE	0x2	/* compare on GE; output high on compare
				 * greater than or equal */
#define PWM_SETUP	(SCALE | CMP1MODE << 6 | MFGPT_SETUP_CNTEN)
#define MAX_COMP2	163

static int ot200_backlight_update_status(struct backlight_device *bl)
{
	struct ot200_backlight_data *data = bl_get_data(bl);
	int brightness = bl->props.brightness;

	if (bl->props.state & BL_CORE_FBBLANK)
		brightness = 0;

	/* enable or disable PWM timer */
	if (brightness == 0)
		cs5535_mfgpt_write(pwm_timer, MFGPT_REG_SETUP, 0);
	else if (data->current_brightness == 0) {
		cs5535_mfgpt_write(pwm_timer, MFGPT_REG_COUNTER, 0);
		cs5535_mfgpt_write(pwm_timer, MFGPT_REG_SETUP,
			MFGPT_SETUP_CNTEN);
	}

	/* apply new brightness value */
	cs5535_mfgpt_write(pwm_timer, MFGPT_REG_CMP1,
		MAX_COMP2 - dim_table[brightness]);
	data->current_brightness = brightness;

	return 0;
}

static int ot200_backlight_get_brightness(struct backlight_device *bl)
{
	struct ot200_backlight_data *data = bl_get_data(bl);
	return data->current_brightness;
}

static const struct backlight_ops ot200_backlight_ops = {
	.update_status	= ot200_backlight_update_status,
	.get_brightness	= ot200_backlight_get_brightness,
};

static int ot200_backlight_probe(struct platform_device *pdev)
{
	struct backlight_device *bl;
	struct ot200_backlight_data *data;
	struct backlight_properties props;
	int retval = 0;

	/* request gpio */
	if (gpio_request(GPIO_DIMM, "ot200 backlight dimmer") < 0) {
		dev_err(&pdev->dev, "failed to request GPIO %d\n", GPIO_DIMM);
		return -ENODEV;
	}

	/* request timer */
	pwm_timer = cs5535_mfgpt_alloc_timer(7, MFGPT_DOMAIN_ANY);
	if (!pwm_timer) {
		dev_err(&pdev->dev, "MFGPT 7 not available\n");
		retval = -ENODEV;
		goto error_mfgpt_alloc;
	}

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data) {
		retval = -ENOMEM;
		goto error_kzalloc;
	}

	/* setup gpio */
	cs5535_gpio_set(GPIO_DIMM, GPIO_OUTPUT_ENABLE);
	cs5535_gpio_set(GPIO_DIMM, GPIO_OUTPUT_AUX1);

	/* setup timer */
	cs5535_mfgpt_write(pwm_timer, MFGPT_REG_CMP1, 0);
	cs5535_mfgpt_write(pwm_timer, MFGPT_REG_CMP2, MAX_COMP2);
	cs5535_mfgpt_write(pwm_timer, MFGPT_REG_SETUP, PWM_SETUP);

	data->current_brightness = 100;
	props.max_brightness = 100;
	props.brightness = 100;
	props.type = BACKLIGHT_RAW;

	bl = backlight_device_register(dev_name(&pdev->dev), &pdev->dev, data,
					&ot200_backlight_ops, &props);
	if (IS_ERR(bl)) {
		dev_err(&pdev->dev, "failed to register backlight\n");
		retval = PTR_ERR(bl);
		goto error_backlight_device_register;
	}

	platform_set_drvdata(pdev, bl);

	return 0;

error_backlight_device_register:
	kfree(data);
error_kzalloc:
	cs5535_mfgpt_free_timer(pwm_timer);
error_mfgpt_alloc:
	gpio_free(GPIO_DIMM);
	return retval;
}

static int ot200_backlight_remove(struct platform_device *pdev)
{
	struct backlight_device *bl = platform_get_drvdata(pdev);
	struct ot200_backlight_data *data = bl_get_data(bl);

	backlight_device_unregister(bl);

	/* on module unload set brightness to 100% */
	cs5535_mfgpt_write(pwm_timer, MFGPT_REG_COUNTER, 0);
	cs5535_mfgpt_write(pwm_timer, MFGPT_REG_SETUP, MFGPT_SETUP_CNTEN);
	cs5535_mfgpt_write(pwm_timer, MFGPT_REG_CMP1,
		MAX_COMP2 - dim_table[100]);

	cs5535_mfgpt_free_timer(pwm_timer);
	gpio_free(GPIO_DIMM);

	kfree(data);
	return 0;
}

static struct platform_driver ot200_backlight_driver = {
	.driver		= {
		.name	= "ot200-backlight",
		.owner	= THIS_MODULE,
	},
	.probe		= ot200_backlight_probe,
	.remove		= ot200_backlight_remove,
};

module_platform_driver(ot200_backlight_driver);

MODULE_DESCRIPTION("backlight driver for ot200 visualisation device");
MODULE_AUTHOR("Christian Gmeiner <christian.gmeiner@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ot200-backlight");
