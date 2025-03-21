// SPDX-License-Identifier: GPL-2.0-only
/*
 * Backlight emulation LED trigger
 *
 * Copyright 2008 (C) Rodolfo Giometti <giometti@linux.it>
 * Copyright 2008 (C) Eurotech S.p.A. <info@eurotech.it>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/leds.h>
#include "../leds.h"

#define BLANK		1
#define UNBLANK		0

struct bl_trig_notifier {
	struct led_classdev *led;
	int brightness;
	int old_status;
	unsigned invert;

	struct list_head entry;
};

static DEFINE_MUTEX(ledtrig_backlight_list_mutex);
static LIST_HEAD(ledtrig_backlight_list);

static void ledtrig_backlight_notify_blank(struct bl_trig_notifier *n, int new_status)
{
	struct led_classdev *led = n->led;

	if (new_status == n->old_status)
		return;

	if ((n->old_status == UNBLANK) ^ n->invert) {
		n->brightness = led->brightness;
		led_set_brightness_nosleep(led, LED_OFF);
	} else {
		led_set_brightness_nosleep(led, n->brightness);
	}

	n->old_status = new_status;
}

void ledtrig_backlight_blank(bool blank)
{
	struct bl_trig_notifier *n;
	int new_status = blank ? BLANK : UNBLANK;

	guard(mutex)(&ledtrig_backlight_list_mutex);

	list_for_each_entry(n, &ledtrig_backlight_list, entry)
		ledtrig_backlight_notify_blank(n, new_status);
}
EXPORT_SYMBOL(ledtrig_backlight_blank);

static ssize_t bl_trig_invert_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct bl_trig_notifier *n = led_trigger_get_drvdata(dev);

	return sprintf(buf, "%u\n", n->invert);
}

static ssize_t bl_trig_invert_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t num)
{
	struct led_classdev *led = led_trigger_get_led(dev);
	struct bl_trig_notifier *n = led_trigger_get_drvdata(dev);
	unsigned long invert;
	int ret;

	ret = kstrtoul(buf, 10, &invert);
	if (ret < 0)
		return ret;

	if (invert > 1)
		return -EINVAL;

	n->invert = invert;

	/* After inverting, we need to update the LED. */
	if ((n->old_status == BLANK) ^ n->invert)
		led_set_brightness_nosleep(led, LED_OFF);
	else
		led_set_brightness_nosleep(led, n->brightness);

	return num;
}
static DEVICE_ATTR(inverted, 0644, bl_trig_invert_show, bl_trig_invert_store);

static struct attribute *bl_trig_attrs[] = {
	&dev_attr_inverted.attr,
	NULL,
};
ATTRIBUTE_GROUPS(bl_trig);

static int bl_trig_activate(struct led_classdev *led)
{
	struct bl_trig_notifier *n;

	n = kzalloc(sizeof(struct bl_trig_notifier), GFP_KERNEL);
	if (!n)
		return -ENOMEM;
	led_set_trigger_data(led, n);

	n->led = led;
	n->brightness = led->brightness;
	n->old_status = UNBLANK;

	guard(mutex)(&ledtrig_backlight_list_mutex);
	list_add(&n->entry, &ledtrig_backlight_list);

	return 0;
}

static void bl_trig_deactivate(struct led_classdev *led)
{
	struct bl_trig_notifier *n = led_get_trigger_data(led);

	guard(mutex)(&ledtrig_backlight_list_mutex);
	list_del(&n->entry);

	kfree(n);
}

static struct led_trigger bl_led_trigger = {
	.name		= "backlight",
	.activate	= bl_trig_activate,
	.deactivate	= bl_trig_deactivate,
	.groups		= bl_trig_groups,
};
module_led_trigger(bl_led_trigger);

MODULE_AUTHOR("Rodolfo Giometti <giometti@linux.it>");
MODULE_DESCRIPTION("Backlight emulation LED trigger");
MODULE_LICENSE("GPL v2");
