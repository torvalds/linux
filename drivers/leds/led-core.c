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

static int __led_set_brightness(struct led_classdev *led_cdev,
				enum led_brightness value)
{
	if (!led_cdev->brightness_set)
		return -ENOTSUPP;

	led_cdev->brightness_set(led_cdev, value);

	return 0;
}

static int __led_set_brightness_blocking(struct led_classdev *led_cdev,
					 enum led_brightness value)
{
	if (!led_cdev->brightness_set_blocking)
		return -ENOTSUPP;

	return led_cdev->brightness_set_blocking(led_cdev, value);
}

static void led_timer_function(struct timer_list *t)
{
	struct led_classdev *led_cdev = from_timer(led_cdev, t, blink_timer);
	unsigned long brightness;
	unsigned long delay;

	if (!led_cdev->blink_delay_on || !led_cdev->blink_delay_off) {
		led_set_brightness_nosleep(led_cdev, LED_OFF);
		clear_bit(LED_BLINK_SW, &led_cdev->work_flags);
		return;
	}

	if (test_and_clear_bit(LED_BLINK_ONESHOT_STOP,
			       &led_cdev->work_flags)) {
		clear_bit(LED_BLINK_SW, &led_cdev->work_flags);
		return;
	}

	brightness = led_get_brightness(led_cdev);
	if (!brightness) {
		/* Time to switch the LED on. */
		if (test_and_clear_bit(LED_BLINK_BRIGHTNESS_CHANGE,
					&led_cdev->work_flags))
			brightness = led_cdev->new_blink_brightness;
		else
			brightness = led_cdev->blink_brightness;
		delay = led_cdev->blink_delay_on;
	} else {
		/* Store the current brightness value to be able
		 * to restore it when the delay_off period is over.
		 */
		led_cdev->blink_brightness = brightness;
		brightness = LED_OFF;
		delay = led_cdev->blink_delay_off;
	}

	led_set_brightness_nosleep(led_cdev, brightness);

	/* Return in next iteration if led is in one-shot mode and we are in
	 * the final blink state so that the led is toggled each delay_on +
	 * delay_off milliseconds in worst case.
	 */
	if (test_bit(LED_BLINK_ONESHOT, &led_cdev->work_flags)) {
		if (test_bit(LED_BLINK_INVERT, &led_cdev->work_flags)) {
			if (brightness)
				set_bit(LED_BLINK_ONESHOT_STOP,
					&led_cdev->work_flags);
		} else {
			if (!brightness)
				set_bit(LED_BLINK_ONESHOT_STOP,
					&led_cdev->work_flags);
		}
	}

	mod_timer(&led_cdev->blink_timer, jiffies + msecs_to_jiffies(delay));
}

static void set_brightness_delayed(struct work_struct *ws)
{
	struct led_classdev *led_cdev =
		container_of(ws, struct led_classdev, set_brightness_work);
	int ret = 0;

	if (test_and_clear_bit(LED_BLINK_DISABLE, &led_cdev->work_flags)) {
		led_cdev->delayed_set_value = LED_OFF;
		led_stop_software_blink(led_cdev);
	}

	ret = __led_set_brightness(led_cdev, led_cdev->delayed_set_value);
	if (ret == -ENOTSUPP)
		ret = __led_set_brightness_blocking(led_cdev,
					led_cdev->delayed_set_value);
	if (ret < 0 &&
	    /* LED HW might have been unplugged, therefore don't warn */
	    !(ret == -ENODEV && (led_cdev->flags & LED_UNREGISTERING) &&
	    (led_cdev->flags & LED_HW_PLUGGABLE)))
		dev_err(led_cdev->dev,
			"Setting an LED's brightness failed (%d)\n", ret);
}

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
		led_set_brightness_nosleep(led_cdev, LED_OFF);
		return;
	}

	/* never off - just set to brightness */
	if (!delay_off) {
		led_set_brightness_nosleep(led_cdev,
					   led_cdev->blink_brightness);
		return;
	}

	set_bit(LED_BLINK_SW, &led_cdev->work_flags);
	mod_timer(&led_cdev->blink_timer, jiffies + 1);
}


static void led_blink_setup(struct led_classdev *led_cdev,
		     unsigned long *delay_on,
		     unsigned long *delay_off)
{
	if (!test_bit(LED_BLINK_ONESHOT, &led_cdev->work_flags) &&
	    led_cdev->blink_set &&
	    !led_cdev->blink_set(led_cdev, delay_on, delay_off))
		return;

	/* blink with 1 Hz as default if nothing specified */
	if (!*delay_on && !*delay_off)
		*delay_on = *delay_off = 500;

	led_set_software_blink(led_cdev, *delay_on, *delay_off);
}

void led_init_core(struct led_classdev *led_cdev)
{
	INIT_WORK(&led_cdev->set_brightness_work, set_brightness_delayed);

	timer_setup(&led_cdev->blink_timer, led_timer_function, 0);
}
EXPORT_SYMBOL_GPL(led_init_core);

void led_blink_set(struct led_classdev *led_cdev,
		   unsigned long *delay_on,
		   unsigned long *delay_off)
{
	del_timer_sync(&led_cdev->blink_timer);

	clear_bit(LED_BLINK_SW, &led_cdev->work_flags);
	clear_bit(LED_BLINK_ONESHOT, &led_cdev->work_flags);
	clear_bit(LED_BLINK_ONESHOT_STOP, &led_cdev->work_flags);

	led_blink_setup(led_cdev, delay_on, delay_off);
}
EXPORT_SYMBOL_GPL(led_blink_set);

void led_blink_set_oneshot(struct led_classdev *led_cdev,
			   unsigned long *delay_on,
			   unsigned long *delay_off,
			   int invert)
{
	if (test_bit(LED_BLINK_ONESHOT, &led_cdev->work_flags) &&
	     timer_pending(&led_cdev->blink_timer))
		return;

	set_bit(LED_BLINK_ONESHOT, &led_cdev->work_flags);
	clear_bit(LED_BLINK_ONESHOT_STOP, &led_cdev->work_flags);

	if (invert)
		set_bit(LED_BLINK_INVERT, &led_cdev->work_flags);
	else
		clear_bit(LED_BLINK_INVERT, &led_cdev->work_flags);

	led_blink_setup(led_cdev, delay_on, delay_off);
}
EXPORT_SYMBOL_GPL(led_blink_set_oneshot);

void led_stop_software_blink(struct led_classdev *led_cdev)
{
	del_timer_sync(&led_cdev->blink_timer);
	led_cdev->blink_delay_on = 0;
	led_cdev->blink_delay_off = 0;
	clear_bit(LED_BLINK_SW, &led_cdev->work_flags);
}
EXPORT_SYMBOL_GPL(led_stop_software_blink);

void led_set_brightness(struct led_classdev *led_cdev,
			enum led_brightness brightness)
{
	/*
	 * If software blink is active, delay brightness setting
	 * until the next timer tick.
	 */
	if (test_bit(LED_BLINK_SW, &led_cdev->work_flags)) {
		/*
		 * If we need to disable soft blinking delegate this to the
		 * work queue task to avoid problems in case we are called
		 * from hard irq context.
		 */
		if (brightness == LED_OFF) {
			set_bit(LED_BLINK_DISABLE, &led_cdev->work_flags);
			schedule_work(&led_cdev->set_brightness_work);
		} else {
			set_bit(LED_BLINK_BRIGHTNESS_CHANGE,
				&led_cdev->work_flags);
			led_cdev->new_blink_brightness = brightness;
		}
		return;
	}

	led_set_brightness_nosleep(led_cdev, brightness);
}
EXPORT_SYMBOL_GPL(led_set_brightness);

void led_set_brightness_nopm(struct led_classdev *led_cdev,
			      enum led_brightness value)
{
	/* Use brightness_set op if available, it is guaranteed not to sleep */
	if (!__led_set_brightness(led_cdev, value))
		return;

	/* If brightness setting can sleep, delegate it to a work queue task */
	led_cdev->delayed_set_value = value;
	schedule_work(&led_cdev->set_brightness_work);
}
EXPORT_SYMBOL_GPL(led_set_brightness_nopm);

void led_set_brightness_nosleep(struct led_classdev *led_cdev,
				enum led_brightness value)
{
	led_cdev->brightness = min(value, led_cdev->max_brightness);

	if (led_cdev->flags & LED_SUSPENDED)
		return;

	led_set_brightness_nopm(led_cdev, led_cdev->brightness);
}
EXPORT_SYMBOL_GPL(led_set_brightness_nosleep);

int led_set_brightness_sync(struct led_classdev *led_cdev,
			    enum led_brightness value)
{
	if (led_cdev->blink_delay_on || led_cdev->blink_delay_off)
		return -EBUSY;

	led_cdev->brightness = min(value, led_cdev->max_brightness);

	if (led_cdev->flags & LED_SUSPENDED)
		return 0;

	return __led_set_brightness_blocking(led_cdev, led_cdev->brightness);
}
EXPORT_SYMBOL_GPL(led_set_brightness_sync);

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
EXPORT_SYMBOL_GPL(led_update_brightness);

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
