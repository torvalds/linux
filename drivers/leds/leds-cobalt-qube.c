/*
 * Copyright 2006 - Florian Fainelli <florian@openwrt.org>
 *
 * Control the Cobalt Qube/RaQ front LED
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/leds.h>
#include <asm/mach-cobalt/cobalt.h>

static void cobalt_led_set(struct led_classdev *led_cdev, enum led_brightness brightness)
{
	if (brightness)
		COBALT_LED_PORT = COBALT_LED_BAR_LEFT | COBALT_LED_BAR_RIGHT;
	else
		COBALT_LED_PORT = 0;
}

static struct led_classdev cobalt_led = {
       .name = "cobalt-front-led",
       .brightness_set = cobalt_led_set,
       .default_trigger = "ide-disk",
};

static int __init cobalt_led_init(void)
{
	return led_classdev_register(NULL, &cobalt_led);
}

static void __exit cobalt_led_exit(void)
{
	led_classdev_unregister(&cobalt_led);
}

module_init(cobalt_led_init);
module_exit(cobalt_led_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Front LED support for Cobalt Server");
MODULE_AUTHOR("Florian Fainelli <florian@openwrt.org>");
