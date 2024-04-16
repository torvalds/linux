// SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-or-later
/*
 * Dell Wyse 3020 a.k.a. "Ariel" Embedded Controller LED Driver
 *
 * Copyright (C) 2020 Lubomir Rintel
 */

#include <linux/module.h>
#include <linux/leds.h>
#include <linux/regmap.h>
#include <linux/of_platform.h>

enum ec_index {
	EC_BLUE_LED	= 0x01,
	EC_AMBER_LED	= 0x02,
	EC_GREEN_LED	= 0x03,
};

enum {
	EC_LED_OFF	= 0x00,
	EC_LED_STILL	= 0x01,
	EC_LED_FADE	= 0x02,
	EC_LED_BLINK	= 0x03,
};

struct ariel_led {
	struct regmap *ec_ram;
	enum ec_index ec_index;
	struct led_classdev led_cdev;
};

#define led_cdev_to_ariel_led(c) container_of(c, struct ariel_led, led_cdev)

static enum led_brightness ariel_led_get(struct led_classdev *led_cdev)
{
	struct ariel_led *led = led_cdev_to_ariel_led(led_cdev);
	unsigned int led_status = 0;

	if (regmap_read(led->ec_ram, led->ec_index, &led_status))
		return LED_OFF;

	if (led_status == EC_LED_STILL)
		return LED_FULL;
	else
		return LED_OFF;
}

static void ariel_led_set(struct led_classdev *led_cdev,
			  enum led_brightness brightness)
{
	struct ariel_led *led = led_cdev_to_ariel_led(led_cdev);

	if (brightness == LED_OFF)
		regmap_write(led->ec_ram, led->ec_index, EC_LED_OFF);
	else
		regmap_write(led->ec_ram, led->ec_index, EC_LED_STILL);
}

static int ariel_blink_set(struct led_classdev *led_cdev,
			   unsigned long *delay_on, unsigned long *delay_off)
{
	struct ariel_led *led = led_cdev_to_ariel_led(led_cdev);

	if (*delay_on == 0 && *delay_off == 0)
		return -EINVAL;

	if (*delay_on == 0) {
		regmap_write(led->ec_ram, led->ec_index, EC_LED_OFF);
	} else if (*delay_off == 0) {
		regmap_write(led->ec_ram, led->ec_index, EC_LED_STILL);
	} else {
		*delay_on = 500;
		*delay_off = 500;
		regmap_write(led->ec_ram, led->ec_index, EC_LED_BLINK);
	}

	return 0;
}

#define NLEDS 3

static int ariel_led_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ariel_led *leds;
	struct regmap *ec_ram;
	int ret;
	int i;

	ec_ram = dev_get_regmap(dev->parent, "ec_ram");
	if (!ec_ram)
		return -ENODEV;

	leds = devm_kcalloc(dev, NLEDS, sizeof(*leds), GFP_KERNEL);
	if (!leds)
		return -ENOMEM;

	leds[0].ec_index = EC_BLUE_LED;
	leds[0].led_cdev.name = "blue:power";
	leds[0].led_cdev.default_trigger = "default-on";

	leds[1].ec_index = EC_AMBER_LED;
	leds[1].led_cdev.name = "amber:status";

	leds[2].ec_index = EC_GREEN_LED;
	leds[2].led_cdev.name = "green:status";
	leds[2].led_cdev.default_trigger = "default-on";

	for (i = 0; i < NLEDS; i++) {
		leds[i].ec_ram = ec_ram;
		leds[i].led_cdev.brightness_get = ariel_led_get;
		leds[i].led_cdev.brightness_set = ariel_led_set;
		leds[i].led_cdev.blink_set = ariel_blink_set;

		ret = devm_led_classdev_register(dev, &leds[i].led_cdev);
		if (ret)
			return ret;
	}

	return 0;
}

static struct platform_driver ariel_led_driver = {
	.probe = ariel_led_probe,
	.driver = {
		.name = "dell-wyse-ariel-led",
	},
};
module_platform_driver(ariel_led_driver);

MODULE_AUTHOR("Lubomir Rintel <lkundrak@v3.sk>");
MODULE_DESCRIPTION("Dell Wyse 3020 Status LEDs Driver");
MODULE_LICENSE("Dual BSD/GPL");
