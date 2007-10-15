/*
 * drivers/leds/leds-cm-x270.c
 *
 * Copyright 2007 CompuLab Ltd.
 * Author: Mike Rapoport <mike@compulab.co.il>
 *
 * Based on leds-corgi.c
 * Author: Richard Purdie <rpurdie@openedhand.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/leds.h>

#include <asm/arch/hardware.h>
#include <asm/arch/pxa-regs.h>

#define GPIO_RED_LED			(93)
#define GPIO_GREEN_LED			(94)

static void cmx270_red_set(struct led_classdev *led_cdev,
			   enum led_brightness value)
{
	if (value)
		GPCR(GPIO_RED_LED) = GPIO_bit(GPIO_RED_LED);
	else
		GPSR(GPIO_RED_LED) = GPIO_bit(GPIO_RED_LED);
}

static void cmx270_green_set(struct led_classdev *led_cdev,
			     enum led_brightness value)
{
	if (value)
		GPCR(GPIO_GREEN_LED) = GPIO_bit(GPIO_GREEN_LED);
	else
		GPSR(GPIO_GREEN_LED) = GPIO_bit(GPIO_GREEN_LED);
}

static struct led_classdev cmx270_red_led = {
	.name			= "cm-x270:red",
	.default_trigger	= "nand-disk",
	.brightness_set		= cmx270_red_set,
};

static struct led_classdev cmx270_green_led = {
	.name			= "cm-x270:green",
	.default_trigger	= "heartbeat",
	.brightness_set		= cmx270_green_set,
};

#ifdef CONFIG_PM
static int cmx270led_suspend(struct platform_device *dev, pm_message_t state)
{
	led_classdev_suspend(&cmx270_red_led);
	led_classdev_suspend(&cmx270_green_led);
	return 0;
}

static int cmx270led_resume(struct platform_device *dev)
{
	led_classdev_resume(&cmx270_red_led);
	led_classdev_resume(&cmx270_green_led);
	return 0;
}
#endif

static int cmx270led_probe(struct platform_device *pdev)
{
	int ret;

	ret = led_classdev_register(&pdev->dev, &cmx270_red_led);
	if (ret < 0)
		return ret;

	ret = led_classdev_register(&pdev->dev, &cmx270_green_led);
	if (ret < 0)
		led_classdev_unregister(&cmx270_red_led);

	return ret;
}

static int cmx270led_remove(struct platform_device *pdev)
{
	led_classdev_unregister(&cmx270_red_led);
	led_classdev_unregister(&cmx270_green_led);
	return 0;
}

static struct platform_driver cmx270led_driver = {
	.probe		= cmx270led_probe,
	.remove		= cmx270led_remove,
#ifdef CONFIG_PM
	.suspend	= cmx270led_suspend,
	.resume		= cmx270led_resume,
#endif
	.driver		= {
		.name		= "cm-x270-led",
	},
};

static int __init cmx270led_init(void)
{
	return platform_driver_register(&cmx270led_driver);
}

static void __exit cmx270led_exit(void)
{
	platform_driver_unregister(&cmx270led_driver);
}

module_init(cmx270led_init);
module_exit(cmx270led_exit);

MODULE_AUTHOR("Mike Rapoport <mike@compulab.co.il>");
MODULE_DESCRIPTION("CM-x270 LED driver");
MODULE_LICENSE("GPL");
