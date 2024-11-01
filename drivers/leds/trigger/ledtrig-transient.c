// SPDX-License-Identifier: GPL-2.0
//
// LED Kernel Transient Trigger
//
// Transient trigger allows one shot timer activation. Please refer to
// Documentation/leds/ledtrig-transient.rst for details
// Copyright (C) 2012 Shuah Khan <shuahkhan@gmail.com>
//
// Based on Richard Purdie's ledtrig-timer.c and Atsushi Nemoto's
// ledtrig-heartbeat.c
// Design and use-case input from Jonas Bonn <jonas@southpole.se> and
// Neil Brown <neilb@suse.de>

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/leds.h>
#include "../leds.h"

struct transient_trig_data {
	int activate;
	int state;
	int restore_state;
	unsigned long duration;
	struct timer_list timer;
	struct led_classdev *led_cdev;
};

static void transient_timer_function(struct timer_list *t)
{
	struct transient_trig_data *transient_data =
		from_timer(transient_data, t, timer);
	struct led_classdev *led_cdev = transient_data->led_cdev;

	transient_data->activate = 0;
	led_set_brightness_nosleep(led_cdev, transient_data->restore_state);
}

static ssize_t transient_activate_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct transient_trig_data *transient_data =
		led_trigger_get_drvdata(dev);

	return sprintf(buf, "%d\n", transient_data->activate);
}

static ssize_t transient_activate_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct led_classdev *led_cdev = led_trigger_get_led(dev);
	struct transient_trig_data *transient_data =
		led_trigger_get_drvdata(dev);
	unsigned long state;
	ssize_t ret;

	ret = kstrtoul(buf, 10, &state);
	if (ret)
		return ret;

	if (state != 1 && state != 0)
		return -EINVAL;

	/* cancel the running timer */
	if (state == 0 && transient_data->activate == 1) {
		del_timer(&transient_data->timer);
		transient_data->activate = state;
		led_set_brightness_nosleep(led_cdev,
					transient_data->restore_state);
		return size;
	}

	/* start timer if there is no active timer */
	if (state == 1 && transient_data->activate == 0 &&
	    transient_data->duration != 0) {
		transient_data->activate = state;
		led_set_brightness_nosleep(led_cdev, transient_data->state);
		transient_data->restore_state =
		    (transient_data->state == LED_FULL) ? LED_OFF : LED_FULL;
		mod_timer(&transient_data->timer,
			  jiffies + msecs_to_jiffies(transient_data->duration));
	}

	/* state == 0 && transient_data->activate == 0
		timer is not active - just return */
	/* state == 1 && transient_data->activate == 1
		timer is already active - just return */

	return size;
}

static ssize_t transient_duration_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct transient_trig_data *transient_data = led_trigger_get_drvdata(dev);

	return sprintf(buf, "%lu\n", transient_data->duration);
}

static ssize_t transient_duration_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct transient_trig_data *transient_data =
		led_trigger_get_drvdata(dev);
	unsigned long state;
	ssize_t ret;

	ret = kstrtoul(buf, 10, &state);
	if (ret)
		return ret;

	transient_data->duration = state;
	return size;
}

static ssize_t transient_state_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct transient_trig_data *transient_data =
		led_trigger_get_drvdata(dev);
	int state;

	state = (transient_data->state == LED_FULL) ? 1 : 0;
	return sprintf(buf, "%d\n", state);
}

static ssize_t transient_state_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct transient_trig_data *transient_data =
		led_trigger_get_drvdata(dev);
	unsigned long state;
	ssize_t ret;

	ret = kstrtoul(buf, 10, &state);
	if (ret)
		return ret;

	if (state != 1 && state != 0)
		return -EINVAL;

	transient_data->state = (state == 1) ? LED_FULL : LED_OFF;
	return size;
}

static DEVICE_ATTR(activate, 0644, transient_activate_show,
		   transient_activate_store);
static DEVICE_ATTR(duration, 0644, transient_duration_show,
		   transient_duration_store);
static DEVICE_ATTR(state, 0644, transient_state_show, transient_state_store);

static struct attribute *transient_trig_attrs[] = {
	&dev_attr_activate.attr,
	&dev_attr_duration.attr,
	&dev_attr_state.attr,
	NULL
};
ATTRIBUTE_GROUPS(transient_trig);

static int transient_trig_activate(struct led_classdev *led_cdev)
{
	struct transient_trig_data *tdata;

	tdata = kzalloc(sizeof(struct transient_trig_data), GFP_KERNEL);
	if (!tdata)
		return -ENOMEM;

	led_set_trigger_data(led_cdev, tdata);
	tdata->led_cdev = led_cdev;

	timer_setup(&tdata->timer, transient_timer_function, 0);

	return 0;
}

static void transient_trig_deactivate(struct led_classdev *led_cdev)
{
	struct transient_trig_data *transient_data = led_get_trigger_data(led_cdev);

	del_timer_sync(&transient_data->timer);
	led_set_brightness_nosleep(led_cdev, transient_data->restore_state);
	kfree(transient_data);
}

static struct led_trigger transient_trigger = {
	.name     = "transient",
	.activate = transient_trig_activate,
	.deactivate = transient_trig_deactivate,
	.groups = transient_trig_groups,
};
module_led_trigger(transient_trigger);

MODULE_AUTHOR("Shuah Khan <shuahkhan@gmail.com>");
MODULE_DESCRIPTION("Transient LED trigger");
MODULE_LICENSE("GPL v2");
