/*
 * LED Triggers Core
 *
 * Copyright 2005-2006 Openedhand Ltd.
 *
 * Author: Richard Purdie <rpurdie@openedhand.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <asm/mach-types.h>
#include <asm/arch/corgi.h>
#include <asm/arch/hardware.h>
#include <asm/arch/pxa-regs.h>
#include <asm/hardware/scoop.h>

static void corgiled_amber_set(struct led_classdev *led_cdev, enum led_brightness value)
{
	if (value)
		GPSR0 = GPIO_bit(CORGI_GPIO_LED_ORANGE);
	else
		GPCR0 = GPIO_bit(CORGI_GPIO_LED_ORANGE);
}

static void corgiled_green_set(struct led_classdev *led_cdev, enum led_brightness value)
{
	if (value)
		set_scoop_gpio(&corgiscoop_device.dev, CORGI_SCP_LED_GREEN);
	else
		reset_scoop_gpio(&corgiscoop_device.dev, CORGI_SCP_LED_GREEN);
}

static struct led_classdev corgi_amber_led = {
	.name			= "corgi:amber",
	.default_trigger	= "sharpsl-charge",
	.brightness_set		= corgiled_amber_set,
};

static struct led_classdev corgi_green_led = {
	.name			= "corgi:green",
	.default_trigger	= "nand-disk",
	.brightness_set		= corgiled_green_set,
};

#ifdef CONFIG_PM
static int corgiled_suspend(struct platform_device *dev, pm_message_t state)
{
#ifdef CONFIG_LEDS_TRIGGERS
	if (corgi_amber_led.trigger && strcmp(corgi_amber_led.trigger->name, "sharpsl-charge"))
#endif
		led_classdev_suspend(&corgi_amber_led);
	led_classdev_suspend(&corgi_green_led);
	return 0;
}

static int corgiled_resume(struct platform_device *dev)
{
	led_classdev_resume(&corgi_amber_led);
	led_classdev_resume(&corgi_green_led);
	return 0;
}
#endif

static int corgiled_probe(struct platform_device *pdev)
{
	int ret;

	ret = led_classdev_register(&pdev->dev, &corgi_amber_led);
	if (ret < 0)
		return ret;

	ret = led_classdev_register(&pdev->dev, &corgi_green_led);
	if (ret < 0)
		led_classdev_unregister(&corgi_amber_led);

	return ret;
}

static int corgiled_remove(struct platform_device *pdev)
{
	led_classdev_unregister(&corgi_amber_led);
	led_classdev_unregister(&corgi_green_led);
	return 0;
}

static struct platform_driver corgiled_driver = {
	.probe		= corgiled_probe,
	.remove		= corgiled_remove,
#ifdef CONFIG_PM
	.suspend	= corgiled_suspend,
	.resume		= corgiled_resume,
#endif
	.driver		= {
		.name		= "corgi-led",
	},
};

static int __init corgiled_init(void)
{
	return platform_driver_register(&corgiled_driver);
}

static void __exit corgiled_exit(void)
{
 	platform_driver_unregister(&corgiled_driver);
}

module_init(corgiled_init);
module_exit(corgiled_exit);

MODULE_AUTHOR("Richard Purdie <rpurdie@openedhand.com>");
MODULE_DESCRIPTION("Corgi LED driver");
MODULE_LICENSE("GPL");
