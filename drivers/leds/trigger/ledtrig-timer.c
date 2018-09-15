/*
 * LED Kernel Timer Trigger
 *
 * Copyright 2005-2006 Openedhand Ltd.
 *
 * Author: Richard Purdie <rpurdie@openedhand.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/ctype.h>
#include <linux/leds.h>

static ssize_t led_delay_on_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = led_trigger_get_led(dev);

	return sprintf(buf, "%lu\n", led_cdev->blink_delay_on);
}

static ssize_t led_delay_on_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct led_classdev *led_cdev = led_trigger_get_led(dev);
	unsigned long state;
	ssize_t ret = -EINVAL;

	ret = kstrtoul(buf, 10, &state);
	if (ret)
		return ret;

	led_blink_set(led_cdev, &state, &led_cdev->blink_delay_off);
	led_cdev->blink_delay_on = state;

	return size;
}

static ssize_t led_delay_off_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = led_trigger_get_led(dev);

	return sprintf(buf, "%lu\n", led_cdev->blink_delay_off);
}

static ssize_t led_delay_off_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct led_classdev *led_cdev = led_trigger_get_led(dev);
	unsigned long state;
	ssize_t ret = -EINVAL;

	ret = kstrtoul(buf, 10, &state);
	if (ret)
		return ret;

	led_blink_set(led_cdev, &led_cdev->blink_delay_on, &state);
	led_cdev->blink_delay_off = state;

	return size;
}

static DEVICE_ATTR(delay_on, 0644, led_delay_on_show, led_delay_on_store);
static DEVICE_ATTR(delay_off, 0644, led_delay_off_show, led_delay_off_store);

static struct attribute *timer_trig_attrs[] = {
	&dev_attr_delay_on.attr,
	&dev_attr_delay_off.attr,
	NULL
};
ATTRIBUTE_GROUPS(timer_trig);

static int timer_trig_activate(struct led_classdev *led_cdev)
{
	led_blink_set(led_cdev, &led_cdev->blink_delay_on,
		      &led_cdev->blink_delay_off);

	return 0;
}

static void timer_trig_deactivate(struct led_classdev *led_cdev)
{
	/* Stop blinking */
	led_set_brightness(led_cdev, LED_OFF);
}

static struct led_trigger timer_led_trigger = {
	.name     = "timer",
	.activate = timer_trig_activate,
	.deactivate = timer_trig_deactivate,
	.groups = timer_trig_groups,
};
module_led_trigger(timer_led_trigger);

MODULE_AUTHOR("Richard Purdie <rpurdie@openedhand.com>");
MODULE_DESCRIPTION("Timer LED trigger");
MODULE_LICENSE("GPL v2");
