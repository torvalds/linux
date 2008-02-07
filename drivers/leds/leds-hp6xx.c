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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <asm/hd64461.h>
#include <asm/hp6xx.h>

static void hp6xxled_green_set(struct led_classdev *led_cdev, enum led_brightness value)
{
	u8 v8;

	v8 = inb(PKDR);
	if (value)
		outb(v8 & (~PKDR_LED_GREEN), PKDR);
	else
		outb(v8 | PKDR_LED_GREEN, PKDR);
}

static void hp6xxled_red_set(struct led_classdev *led_cdev, enum led_brightness value)
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
};

static struct led_classdev hp6xx_green_led = {
	.name			= "hp6xx:green",
	.default_trigger	= "ide-disk",
	.brightness_set		= hp6xxled_green_set,
};

#ifdef CONFIG_PM
static int hp6xxled_suspend(struct platform_device *dev, pm_message_t state)
{
	led_classdev_suspend(&hp6xx_red_led);
	led_classdev_suspend(&hp6xx_green_led);
	return 0;
}

static int hp6xxled_resume(struct platform_device *dev)
{
	led_classdev_resume(&hp6xx_red_led);
	led_classdev_resume(&hp6xx_green_led);
	return 0;
}
#endif

static int hp6xxled_probe(struct platform_device *pdev)
{
	int ret;

	ret = led_classdev_register(&pdev->dev, &hp6xx_red_led);
	if (ret < 0)
		return ret;

	ret = led_classdev_register(&pdev->dev, &hp6xx_green_led);
	if (ret < 0)
		led_classdev_unregister(&hp6xx_red_led);

	return ret;
}

static int hp6xxled_remove(struct platform_device *pdev)
{
	led_classdev_unregister(&hp6xx_red_led);
	led_classdev_unregister(&hp6xx_green_led);

	return 0;
}

static struct platform_driver hp6xxled_driver = {
	.probe		= hp6xxled_probe,
	.remove		= hp6xxled_remove,
#ifdef CONFIG_PM
	.suspend	= hp6xxled_suspend,
	.resume		= hp6xxled_resume,
#endif
	.driver		= {
		.name		= "hp6xx-led",
	},
};

static int __init hp6xxled_init(void)
{
	return platform_driver_register(&hp6xxled_driver);
}

static void __exit hp6xxled_exit(void)
{
	platform_driver_unregister(&hp6xxled_driver);
}

module_init(hp6xxled_init);
module_exit(hp6xxled_exit);

MODULE_AUTHOR("Kristoffer Ericson <kristoffer.ericson@gmail.com>");
MODULE_DESCRIPTION("HP Jornada 6xx LED driver");
MODULE_LICENSE("GPL");
