/*
 * LED Class Core
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
#include <linux/list.h>
#include <linux/module.h>
#include <linux/rwsem.h>
#include <linux/leds.h>
#include "leds.h"

DECLARE_RWSEM(leds_list_lock);
EXPORT_SYMBOL_GPL(leds_list_lock);

LIST_HEAD(leds_list);
EXPORT_SYMBOL_GPL(leds_list);

static void led_set_software_blink(struct led_classdev *led_cdev,
				   unsigned long delay_on,
				   unsigned long delay_off)
{
	int current_brightness;

	current_brightness = led_get_brightness(led_cdev);
	if (current_brightness)
		led_cdev->blink_brightness = current_brightness;
	if (!led_cdev->blink_brightness)
		led_cdev->blink_brightness = led_cdev->max_brightness;

	led_cdev->blink_delay_on = delay_on;
	led_cdev->blink_delay_off = delay_off;

	/* never on - don't blink */
	if (!delay_on)
		return;

	/* never off - just set to brightness */
	if (!delay_off) {
		led_set_brightness(led_cdev, led_cdev->blink_brightness);
		return;
	}

	mod_timer(&led_cdev->blink_timer, jiffies + 1);
}


void led_blink_setup(struct led_classdev *led_cdev,
		     unsigned long *delay_on,
		     unsigned long *delay_off)
{
	if (!(led_cdev->flags & LED_BLINK_ONESHOT) &&
	    led_cdev->blink_set &&
	    !led_cdev->blink_set(led_cdev, delay_on, delay_off))
		return;

	/* blink with 1 Hz as default if nothing specified */
	if (!*delay_on && !*delay_off)
		*delay_on = *delay_off = 500;

	led_set_software_blink(led_cdev, *delay_on, *delay_off);
}

void led_blink_set(struct led_classdev *led_cdev,
		   unsigned long *delay_on,
		   unsigned long *delay_off)
{
	del_timer_sync(&led_cdev->blink_timer);

	led_cdev->flags &= ~LED_BLINK_ONESHOT;
	led_cdev->flags &= ~LED_BLINK_ONESHOT_STOP;

	led_blink_setup(led_cdev, delay_on, delay_off);
}
EXPORT_SYMBOL(led_blink_set);

void led_blink_set_oneshot(struct led_classdev *led_cdev,
			   unsigned long *delay_on,
			   unsigned long *delay_off,
			   int invert)
{
	if ((led_cdev->flags & LED_BLINK_ONESHOT) &&
	     timer_pending(&led_cdev->blink_timer))
		return;

	led_cdev->flags |= LED_BLINK_ONESHOT;
	led_cdev->flags &= ~LED_BLINK_ONESHOT_STOP;

	if (invert)
		led_cdev->flags |= LED_BLINK_INVERT;
	else
		led_cdev->flags &= ~LED_BLINK_INVERT;

	led_blink_setup(led_cdev, delay_on, delay_off);
}
EXPORT_SYMBOL(led_blink_set_oneshot);

void led_brightness_set(struct led_classdev *led_cdev,
			enum led_brightness brightness)
{
	/* stop and clear soft-blink timer */
	del_timer_sync(&led_cdev->blink_timer);
	led_cdev->blink_delay_on = 0;
	led_cdev->blink_delay_off = 0;

	led_set_brightness(led_cdev, brightness);
}
EXPORT_SYMBOL(led_brightness_set);
