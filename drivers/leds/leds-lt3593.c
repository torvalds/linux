/*
 * LEDs driver for LT3593 controllers
 *
 * See the datasheet at http://cds.linear.com/docs/Datasheet/3593f.pdf
 *
 * Copyright (c) 2009 Daniel Mack <daniel@caiaq.de>
 *
 * Based on leds-gpio.c,
 *
 *   Copyright (C) 2007 8D Technologies inc.
 *   Raphael Assenat <raph@8d.com>
 *   Copyright (C) 2008 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/module.h>

struct lt3593_led_data {
	struct led_classdev cdev;
	unsigned gpio;
};

static int lt3593_led_set(struct led_classdev *led_cdev,
			   enum led_brightness value)
{
	struct lt3593_led_data *led_dat =
		container_of(led_cdev, struct lt3593_led_data, cdev);
	int pulses;

	/*
	 * The LT3593 resets its internal current level register to the maximum
	 * level on the first falling edge on the control pin. Each following
	 * falling edge decreases the current level by 625uA. Up to 32 pulses
	 * can be sent, so the maximum power reduction is 20mA.
	 * After a timeout of 128us, the value is taken from the register and
	 * applied is to the output driver.
	 */

	if (value == 0) {
		gpio_set_value_cansleep(led_dat->gpio, 0);
		return 0;
	}

	pulses = 32 - (value * 32) / 255;

	if (pulses == 0) {
		gpio_set_value_cansleep(led_dat->gpio, 0);
		mdelay(1);
		gpio_set_value_cansleep(led_dat->gpio, 1);
		return 0;
	}

	gpio_set_value_cansleep(led_dat->gpio, 1);

	while (pulses--) {
		gpio_set_value_cansleep(led_dat->gpio, 0);
		udelay(1);
		gpio_set_value_cansleep(led_dat->gpio, 1);
		udelay(1);
	}

	return 0;
}

static struct lt3593_led_data *lt3593_led_probe_pdata(struct device *dev)
{
	struct gpio_led_platform_data *pdata = dev_get_platdata(dev);
	const struct gpio_led *template = &pdata->leds[0];
	struct lt3593_led_data *led_data;
	int ret, state;

	if (pdata->num_leds != 1)
		return ERR_PTR(-EINVAL);

	led_data = devm_kzalloc(dev, sizeof(*led_data), GFP_KERNEL);
	if (!led_data)
		return ERR_PTR(-ENOMEM);

	if (!gpio_is_valid(template->gpio)) {
		dev_info(dev, "skipping unavailable LT3593 LED at gpio "
			 "%d (%s)\n", template->gpio, template->name);
		return ERR_PTR(-EINVAL);
	}

	led_data->cdev.name = template->name;
	led_data->cdev.default_trigger = template->default_trigger;
	led_data->gpio = template->gpio;
	led_data->cdev.brightness_set_blocking = lt3593_led_set;

	state = (template->default_state == LEDS_GPIO_DEFSTATE_ON);
	led_data->cdev.brightness = state ? LED_FULL : LED_OFF;

	if (!template->retain_state_suspended)
		led_data->cdev.flags |= LED_CORE_SUSPENDRESUME;

	ret = devm_gpio_request_one(dev, template->gpio, state ?
				    GPIOF_OUT_INIT_HIGH : GPIOF_OUT_INIT_LOW,
				    template->name);
	if (ret < 0)
		return ERR_PTR(ret);

	ret = devm_led_classdev_register(dev, &led_data->cdev);
	if (ret < 0)
		return ERR_PTR(ret);

	dev_info(dev, "registered LT3593 LED '%s' at GPIO %d\n",
		 template->name, template->gpio);

	return led_data;
}

static int lt3593_led_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct lt3593_led_data *led_data;

	if (dev_get_platdata(dev)) {
		led_data = lt3593_led_probe_pdata(dev);
		if (IS_ERR(led_data))
			return PTR_ERR(led_data);
	}

	platform_set_drvdata(pdev, led_data);

	return 0;
}

static struct platform_driver lt3593_led_driver = {
	.probe		= lt3593_led_probe,
	.driver		= {
		.name	= "leds-lt3593",
	},
};

module_platform_driver(lt3593_led_driver);

MODULE_AUTHOR("Daniel Mack <daniel@caiaq.de>");
MODULE_DESCRIPTION("LED driver for LT3593 controllers");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:leds-lt3593");
