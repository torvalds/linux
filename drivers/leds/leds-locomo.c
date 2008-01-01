/*
 * linux/drivers/leds/leds-locomo.c
 *
 * Copyright (C) 2005 John Lenz <lenz@cs.wisc.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/leds.h>

#include <asm/hardware.h>
#include <asm/hardware/locomo.h>

static void locomoled_brightness_set(struct led_classdev *led_cdev,
				enum led_brightness value, int offset)
{
	struct locomo_dev *locomo_dev = LOCOMO_DEV(led_cdev->dev->parent);
	unsigned long flags;

	local_irq_save(flags);
	if (value)
		locomo_writel(LOCOMO_LPT_TOFH, locomo_dev->mapbase + offset);
	else
		locomo_writel(LOCOMO_LPT_TOFL, locomo_dev->mapbase + offset);
	local_irq_restore(flags);
}

static void locomoled_brightness_set0(struct led_classdev *led_cdev,
				enum led_brightness value)
{
	locomoled_brightness_set(led_cdev, value, LOCOMO_LPT0);
}

static void locomoled_brightness_set1(struct led_classdev *led_cdev,
				enum led_brightness value)
{
	locomoled_brightness_set(led_cdev, value, LOCOMO_LPT1);
}

static struct led_classdev locomo_led0 = {
	.name			= "locomo:amber",
	.default_trigger	= "sharpsl-charge",
	.brightness_set		= locomoled_brightness_set0,
};

static struct led_classdev locomo_led1 = {
	.name			= "locomo:green",
	.default_trigger	= "nand-disk",
	.brightness_set		= locomoled_brightness_set1,
};

static int locomoled_probe(struct locomo_dev *ldev)
{
	int ret;

	ret = led_classdev_register(&ldev->dev, &locomo_led0);
	if (ret < 0)
		return ret;

	ret = led_classdev_register(&ldev->dev, &locomo_led1);
	if (ret < 0)
		led_classdev_unregister(&locomo_led0);

	return ret;
}

static int locomoled_remove(struct locomo_dev *dev)
{
	led_classdev_unregister(&locomo_led0);
	led_classdev_unregister(&locomo_led1);
	return 0;
}

static struct locomo_driver locomoled_driver = {
	.drv = {
		.name = "locomoled"
	},
	.devid	= LOCOMO_DEVID_LED,
	.probe	= locomoled_probe,
	.remove	= locomoled_remove,
};

static int __init locomoled_init(void)
{
	return locomo_driver_register(&locomoled_driver);
}
module_init(locomoled_init);

MODULE_AUTHOR("John Lenz <lenz@cs.wisc.edu>");
MODULE_DESCRIPTION("Locomo LED driver");
MODULE_LICENSE("GPL");
