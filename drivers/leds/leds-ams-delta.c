/*
 * LEDs driver for Amstrad Delta (E3)
 *
 * Copyright (C) 2006 Jonathan McDowell <noodles@earth.li>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <plat/board-ams-delta.h>

/*
 * Our context
 */
struct ams_delta_led {
	struct led_classdev	cdev;
	u8			bitmask;
};

static void ams_delta_led_set(struct led_classdev *led_cdev,
		enum led_brightness value)
{
	struct ams_delta_led *led_dev =
		container_of(led_cdev, struct ams_delta_led, cdev);

	if (value)
		ams_delta_latch1_write(led_dev->bitmask, led_dev->bitmask);
	else
		ams_delta_latch1_write(led_dev->bitmask, 0);
}

static struct ams_delta_led ams_delta_leds[] = {
	{
		.cdev		= {
			.name		= "ams-delta::camera",
			.brightness_set = ams_delta_led_set,
		},
		.bitmask	= AMS_DELTA_LATCH1_LED_CAMERA,
	},
	{
		.cdev		= {
			.name		= "ams-delta::advert",
			.brightness_set = ams_delta_led_set,
		},
		.bitmask	= AMS_DELTA_LATCH1_LED_ADVERT,
	},
	{
		.cdev		= {
			.name		= "ams-delta::email",
			.brightness_set = ams_delta_led_set,
		},
		.bitmask	= AMS_DELTA_LATCH1_LED_EMAIL,
	},
	{
		.cdev		= {
			.name		= "ams-delta::handsfree",
			.brightness_set = ams_delta_led_set,
		},
		.bitmask	= AMS_DELTA_LATCH1_LED_HANDSFREE,
	},
	{
		.cdev		= {
			.name		= "ams-delta::voicemail",
			.brightness_set = ams_delta_led_set,
		},
		.bitmask	= AMS_DELTA_LATCH1_LED_VOICEMAIL,
	},
	{
		.cdev		= {
			.name		= "ams-delta::voice",
			.brightness_set = ams_delta_led_set,
		},
		.bitmask	= AMS_DELTA_LATCH1_LED_VOICE,
	},
};

static int ams_delta_led_probe(struct platform_device *pdev)
{
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(ams_delta_leds); i++) {
		ams_delta_leds[i].cdev.flags |= LED_CORE_SUSPENDRESUME;
		ret = led_classdev_register(&pdev->dev,
				&ams_delta_leds[i].cdev);
		if (ret < 0)
			goto fail;
	}

	return 0;
fail:
	while (--i >= 0)
		led_classdev_unregister(&ams_delta_leds[i].cdev);
	return ret;	
}

static int ams_delta_led_remove(struct platform_device *pdev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ams_delta_leds); i++)
		led_classdev_unregister(&ams_delta_leds[i].cdev);

	return 0;
}

static struct platform_driver ams_delta_led_driver = {
	.probe		= ams_delta_led_probe,
	.remove		= ams_delta_led_remove,
	.driver		= {
		.name = "ams-delta-led",
		.owner = THIS_MODULE,
	},
};

static int __init ams_delta_led_init(void)
{
	return platform_driver_register(&ams_delta_led_driver);
}

static void __exit ams_delta_led_exit(void)
{
	platform_driver_unregister(&ams_delta_led_driver);
}

module_init(ams_delta_led_init);
module_exit(ams_delta_led_exit);

MODULE_AUTHOR("Jonathan McDowell <noodles@earth.li>");
MODULE_DESCRIPTION("Amstrad Delta LED driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ams-delta-led");
