/*
 * LED Triggers Core
 *
 * Copyright 2005 Dirk Opfer
 *
 * Author: Dirk Opfer <Dirk@Opfer-Online.de>
 *	based on spitz.c
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
#include <asm/hardware/scoop.h>
#include <asm/mach-types.h>
#include <asm/arch/hardware.h>
#include <asm/arch/pxa-regs.h>
#include <asm/arch/tosa.h>

static void tosaled_amber_set(struct led_classdev *led_cdev,
				enum led_brightness value)
{
	if (value)
		set_scoop_gpio(&tosascoop_jc_device.dev,
				TOSA_SCOOP_JC_CHRG_ERR_LED);
	else
		reset_scoop_gpio(&tosascoop_jc_device.dev,
				TOSA_SCOOP_JC_CHRG_ERR_LED);
}

static void tosaled_green_set(struct led_classdev *led_cdev,
				enum led_brightness value)
{
	if (value)
		set_scoop_gpio(&tosascoop_jc_device.dev,
				TOSA_SCOOP_JC_NOTE_LED);
	else
		reset_scoop_gpio(&tosascoop_jc_device.dev,
				TOSA_SCOOP_JC_NOTE_LED);
}

static struct led_classdev tosa_amber_led = {
	.name			= "tosa:amber",
	.default_trigger	= "sharpsl-charge",
	.brightness_set		= tosaled_amber_set,
};

static struct led_classdev tosa_green_led = {
	.name			= "tosa:green",
	.default_trigger	= "nand-disk",
	.brightness_set		= tosaled_green_set,
};

#ifdef CONFIG_PM
static int tosaled_suspend(struct platform_device *dev, pm_message_t state)
{
#ifdef CONFIG_LEDS_TRIGGERS
	if (tosa_amber_led.trigger && strcmp(tosa_amber_led.trigger->name,
						"sharpsl-charge"))
#endif
		led_classdev_suspend(&tosa_amber_led);
	led_classdev_suspend(&tosa_green_led);
	return 0;
}

static int tosaled_resume(struct platform_device *dev)
{
	led_classdev_resume(&tosa_amber_led);
	led_classdev_resume(&tosa_green_led);
	return 0;
}
#else
#define tosaled_suspend NULL
#define tosaled_resume NULL
#endif

static int tosaled_probe(struct platform_device *pdev)
{
	int ret;

	ret = led_classdev_register(&pdev->dev, &tosa_amber_led);
	if (ret < 0)
		return ret;

	ret = led_classdev_register(&pdev->dev, &tosa_green_led);
	if (ret < 0)
		led_classdev_unregister(&tosa_amber_led);

	return ret;
}

static int tosaled_remove(struct platform_device *pdev)
{
	led_classdev_unregister(&tosa_amber_led);
	led_classdev_unregister(&tosa_green_led);

	return 0;
}

static struct platform_driver tosaled_driver = {
	.probe		= tosaled_probe,
	.remove		= tosaled_remove,
	.suspend	= tosaled_suspend,
	.resume		= tosaled_resume,
	.driver		= {
		.name		= "tosa-led",
	},
};

static int __init tosaled_init(void)
{
	return platform_driver_register(&tosaled_driver);
}

static void __exit tosaled_exit(void)
{
 	platform_driver_unregister(&tosaled_driver);
}

module_init(tosaled_init);
module_exit(tosaled_exit);

MODULE_AUTHOR("Dirk Opfer <Dirk@Opfer-Online.de>");
MODULE_DESCRIPTION("Tosa LED driver");
MODULE_LICENSE("GPL");
