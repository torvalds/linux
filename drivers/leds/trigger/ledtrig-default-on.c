// SPDX-License-Identifier: GPL-2.0-only
/*
 * LED Kernel Default ON Trigger
 *
 * Copyright 2008 Nick Forbes <nick.forbes@incepta.com>
 *
 * Based on Richard Purdie's ledtrig-timer.c.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/leds.h>
#include "../leds.h"

static int defon_trig_activate(struct led_classdev *led_cdev)
{
	led_set_brightness_nosleep(led_cdev, led_cdev->max_brightness);
	return 0;
}

static struct led_trigger defon_led_trigger = {
	.name     = "default-on",
	.activate = defon_trig_activate,
};
module_led_trigger(defon_led_trigger);

MODULE_AUTHOR("Nick Forbes <nick.forbes@incepta.com>");
MODULE_DESCRIPTION("Default-ON LED trigger");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("ledtrig:default-on");
