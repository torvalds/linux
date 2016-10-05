/*
 * LED Triggers Core
 * For the HP Jornada 620/660/680/690 handhelds
 *
 * Copyright 2008 Kristoffer Ericson <kristoffer.ericson@gmail.com>
 *     this driver is based on leds-spitz.c by Richard Purdie.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <asm/hd64461.h>
#include <mach/hp6xx.h>

static void hp6xxled_green_set(struct led_classdev *led_cdev,
			       enum led_brightness value)
{
	u8 v8;

	v8 = inb(PKDR);
	if (value)
		outb(v8 & (~PKDR_LED_GREEN), PKDR);
	else
		outb(v8 | PKDR_LED_GREEN, PKDR);
}

static void hp6xxled_red_set(struct led_classdev *led_cdev,
			     enum led_brightness value)
{
	u16 v16;

	v16 = inw(HD64461_GPBDR);
	if (value)
		outw(v16 & (~HD64461_GPBDR_LED_RED), HD64461_GPBDR);
	else
		outw(v16 | HD64461_GPBDR_LED_RED, HD64461_GPBDR);
}

static struct led_classdev hp6xx_red_led = {
	.name			= "hp6xx:red",
	.default_trigger	= "hp6xx-charge",
	.brightness_set		= hp6xxled_red_set,
	.flags			= LED_CORE_SUSPENDRESUME,
};

static struct led_classdev hp6xx_green_led = {
	.name			= "hp6xx:green",
	.default_trigger	= "disk-activity",
	.brightness_set		= hp6xxled_green_set,
	.flags			= LED_CORE_SUSPENDRESUME,
};

static int hp6xxled_probe(struct platform_device *pdev)
{
	int ret;

	ret = devm_led_classdev_register(&pdev->dev, &hp6xx_red_led);
	if (ret < 0)
		return ret;

	return devm_led_classdev_register(&pdev->dev, &hp6xx_green_led);
}

static struct platform_driver hp6xxled_driver = {
	.probe		= hp6xxled_probe,
	.driver		= {
		.name		= "hp6xx-led",
	},
};

module_platform_driver(hp6xxled_driver);

MODULE_AUTHOR("Kristoffer Ericson <kristoffer.ericson@gmail.com>");
MODULE_DESCRIPTION("HP Jornada 6xx LED driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:hp6xx-led");
