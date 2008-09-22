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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <asm/hardware/scoop.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <mach/pxa-regs.h>
#include <mach/spitz.h>

static void spitzled_amber_set(struct led_classdev *led_cdev,
			       enum led_brightness value)
{
	if (value)
		set_scoop_gpio(&spitzscoop_device.dev, SPITZ_SCP_LED_ORANGE);
	else
		reset_scoop_gpio(&spitzscoop_device.dev, SPITZ_SCP_LED_ORANGE);
}

static void spitzled_green_set(struct led_classdev *led_cdev,
			       enum led_brightness value)
{
	if (value)
		set_scoop_gpio(&spitzscoop_device.dev, SPITZ_SCP_LED_GREEN);
	else
		reset_scoop_gpio(&spitzscoop_device.dev, SPITZ_SCP_LED_GREEN);
}

static struct led_classdev spitz_amber_led = {
	.name			= "spitz:amber:charge",
	.default_trigger	= "sharpsl-charge",
	.brightness_set		= spitzled_amber_set,
};

static struct led_classdev spitz_green_led = {
	.name			= "spitz:green:hddactivity",
	.default_trigger	= "ide-disk",
	.brightness_set		= spitzled_green_set,
};

#ifdef CONFIG_PM
static int spitzled_suspend(struct platform_device *dev, pm_message_t state)
{
#ifdef CONFIG_LEDS_TRIGGERS
	if (spitz_amber_led.trigger &&
	    strcmp(spitz_amber_led.trigger->name, "sharpsl-charge"))
#endif
		led_classdev_suspend(&spitz_amber_led);
	led_classdev_suspend(&spitz_green_led);
	return 0;
}

static int spitzled_resume(struct platform_device *dev)
{
	led_classdev_resume(&spitz_amber_led);
	led_classdev_resume(&spitz_green_led);
	return 0;
}
#endif

static int spitzled_probe(struct platform_device *pdev)
{
	int ret;

	if (machine_is_akita()) {
		spitz_green_led.name = "spitz:green:mail";
		spitz_green_led.default_trigger = "nand-disk";
	}

	ret = led_classdev_register(&pdev->dev, &spitz_amber_led);
	if (ret < 0)
		return ret;

	ret = led_classdev_register(&pdev->dev, &spitz_green_led);
	if (ret < 0)
		led_classdev_unregister(&spitz_amber_led);

	return ret;
}

static int spitzled_remove(struct platform_device *pdev)
{
	led_classdev_unregister(&spitz_amber_led);
	led_classdev_unregister(&spitz_green_led);

	return 0;
}

static struct platform_driver spitzled_driver = {
	.probe		= spitzled_probe,
	.remove		= spitzled_remove,
#ifdef CONFIG_PM
	.suspend	= spitzled_suspend,
	.resume		= spitzled_resume,
#endif
	.driver		= {
		.name		= "spitz-led",
		.owner		= THIS_MODULE,
	},
};

static int __init spitzled_init(void)
{
	return platform_driver_register(&spitzled_driver);
}

static void __exit spitzled_exit(void)
{
	platform_driver_unregister(&spitzled_driver);
}

module_init(spitzled_init);
module_exit(spitzled_exit);

MODULE_AUTHOR("Richard Purdie <rpurdie@openedhand.com>");
MODULE_DESCRIPTION("Spitz LED driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:spitz-led");
