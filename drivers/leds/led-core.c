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
#include <linux/leds.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
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

	/* never on - just set to off */
	if (!delay_on) {
		led_set_brightness_async(led_cdev, LED_OFF);
		return;
	}

	/* never off - just set to brightness */
	if (!delay_off) {
		led_set_brightness_async(led_cdev, led_cdev->blink_brightness);
		return;
	}

	mod_timer(&led_cdev->blink_timer, jiffies + 1);
}


static void led_blink_setup(struct led_classdev *led_cdev,
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

void led_stop_software_blink(struct led_classdev *led_cdev)
{
	del_timer_sync(&led_cdev->blink_timer);
	led_cdev->blink_delay_on = 0;
	led_cdev->blink_delay_off = 0;
}
EXPORT_SYMBOL_GPL(led_stop_software_blink);

void led_set_brightness(struct led_classdev *led_cdev,
			enum led_brightness brightness)
{
	int ret = 0;

	/* delay brightness if soft-blink is active */
	if (led_cdev->blink_delay_on || led_cdev->blink_delay_off) {
		led_cdev->delayed_set_value = brightness;
		if (brightness == LED_OFF)
			schedule_work(&led_cdev->set_brightness_work);
		return;
	}

	if (led_cdev->flags & SET_BRIGHTNESS_ASYNC) {
		led_set_brightness_async(led_cdev, brightness);
		return;
	} else if (led_cdev->flags & SET_BRIGHTNESS_SYNC)
		ret = led_set_brightness_sync(led_cdev, brightness);
	else
		ret = -EINVAL;

	if (ret < 0)
		dev_dbg(led_cdev->dev, "Setting LED brightness failed (%d)\n",
			ret);
}
EXPORT_SYMBOL(led_set_brightness);

int led_update_brightness(struct led_classdev *led_cdev)
{
	int ret = 0;

	if (led_cdev->brightness_get) {
		ret = led_cdev->brightness_get(led_cdev);
		if (ret >= 0) {
			led_cdev->brightness = ret;
			return 0;
		}
	}

	return ret;
}
EXPORT_SYMBOL(led_update_brightness);

/* Caller must ensure led_cdev->led_access held */
void led_sysfs_disable(struct led_classdev *led_cdev)
{
	lockdep_assert_held(&led_cdev->led_access);

	led_cdev->flags |= LED_SYSFS_DISABLE;
}
EXPORT_SYMBOL_GPL(led_sysfs_disable);

/* Caller must ensure led_cdev->led_access held */
void led_sysfs_enable(struct led_classdev *led_cdev)
{
	lockdep_assert_held(&led_cdev->led_access);

	led_cdev->flags &= ~LED_SYSFS_DISABLE;
}
EXPORT_SYMBOL_GPL(led_sysfs_enable);
