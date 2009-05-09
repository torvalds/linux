/*
 * LEDs driver for the "User LED" on Routerboard532
 *
 * Copyright (C) 2009 Phil Sutter <n0-1@freewrt.org>
 *
 * Based on leds-cobalt-qube.c by Florian Fainelly and
 * rb-diag.c (my own standalone driver for both LED and
 * button of Routerboard532).
 */

#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <asm/mach-rc32434/gpio.h>
#include <asm/mach-rc32434/rb.h>

static void rb532_led_set(struct led_classdev *cdev,
                          enum led_brightness brightness)
{
	if (brightness)
		set_latch_u5(LO_ULED, 0);

	else
		set_latch_u5(0, LO_ULED);
}

static enum led_brightness rb532_led_get(struct led_classdev *cdev)
{
	return (get_latch_u5() & LO_ULED) ? LED_FULL : LED_OFF;
}

static struct led_classdev rb532_uled = {
	.name = "uled",
	.brightness_set = rb532_led_set,
	.brightness_get = rb532_led_get,
	.default_trigger = "nand-disk",
};

static int __devinit rb532_led_probe(struct platform_device *pdev)
{
	return led_classdev_register(&pdev->dev, &rb532_uled);
}

static int __devexit rb532_led_remove(struct platform_device *pdev)
{
	led_classdev_unregister(&rb532_uled);
	return 0;
}

static struct platform_driver rb532_led_driver = {
	.probe = rb532_led_probe,
	.remove = __devexit_p(rb532_led_remove),
	.driver = {
		.name = "rb532-led",
		.owner = THIS_MODULE,
	},
};

static int __init rb532_led_init(void)
{
	return platform_driver_register(&rb532_led_driver);
}

static void __exit rb532_led_exit(void)
{
	platform_driver_unregister(&rb532_led_driver);
}

module_init(rb532_led_init);
module_exit(rb532_led_exit);

MODULE_ALIAS("platform:rb532-led");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("User LED support for Routerboard532");
MODULE_AUTHOR("Phil Sutter <n0-1@freewrt.org>");
