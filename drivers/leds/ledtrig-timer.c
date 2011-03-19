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
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/ctype.h>
#include <linux/leds.h>
#include "leds.h"

static ssize_t led_delay_on_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	return sprintf(buf, "%lu\n", led_cdev->blink_delay_on);
}

static ssize_t led_delay_on_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	int ret = -EINVAL;
	char *after;
	unsigned long state = simple_strtoul(buf, &after, 10);
	size_t count = after - buf;

	if (isspace(*after))
		count++;

	if (count == size) {
		led_blink_set(led_cdev, &state, &led_cdev->blink_delay_off);
		ret = count;
	}

	return ret;
}

static ssize_t led_delay_off_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	return sprintf(buf, "%lu\n", led_cdev->blink_delay_off);
}

static ssize_t led_delay_off_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	int ret = -EINVAL;
	char *after;
	unsigned long state = simple_strtoul(buf, &after, 10);
	size_t count = after - buf;

	if (isspace(*after))
		count++;

	if (count == size) {
		led_blink_set(led_cdev, &led_cdev->blink_delay_on, &state);
		ret = count;
	}

	return ret;
}

static DEVICE_ATTR(delay_on, 0644, led_delay_on_show, led_delay_on_store);
static DEVICE_ATTR(delay_off, 0644, led_delay_off_show, led_delay_off_store);

static void timer_trig_activate(struct led_classdev *led_cdev)
{
	int rc;

	led_cdev->trigger_data = NULL;

	rc = device_create_file(led_cdev->dev, &dev_attr_delay_on);
	if (rc)
		return;
	rc = device_create_file(led_cdev->dev, &dev_attr_delay_off);
	if (rc)
		goto err_out_delayon;

	led_cdev->trigger_data = (void *)1;

	return;

err_out_delayon:
	device_remove_file(led_cdev->dev, &dev_attr_delay_on);
}

static void timer_trig_deactivate(struct led_classdev *led_cdev)
{
	if (led_cdev->trigger_data) {
		device_remove_file(led_cdev->dev, &dev_attr_delay_on);
		device_remove_file(led_cdev->dev, &dev_attr_delay_off);
	}

	/* Stop blinking */
	led_brightness_set(led_cdev, LED_OFF);
}

static struct led_trigger timer_led_trigger = {
	.name     = "timer",
	.activate = timer_trig_activate,
	.deactivate = timer_trig_deactivate,
};

static int __init timer_trig_init(void)
{
	return led_trigger_register(&timer_led_trigger);
}

static void __exit timer_trig_exit(void)
{
	led_trigger_unregister(&timer_led_trigger);
}

module_init(timer_trig_init);
module_exit(timer_trig_exit);

MODULE_AUTHOR("Richard Purdie <rpurdie@openedhand.com>");
MODULE_DESCRIPTION("Timer LED trigger");
MODULE_LICENSE("GPL");
