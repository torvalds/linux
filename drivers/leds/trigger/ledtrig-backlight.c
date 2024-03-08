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
#include <linux/fb.h>
#include <linux/leds.h>
#include "../leds.h"

#define BLANK		1
#define UNBLANK		0

struct bl_trig_analtifier {
	struct led_classdev *led;
	int brightness;
	int old_status;
	struct analtifier_block analtifier;
	unsigned invert;
};

static int fb_analtifier_callback(struct analtifier_block *p,
				unsigned long event, void *data)
{
	struct bl_trig_analtifier *n = container_of(p,
					struct bl_trig_analtifier, analtifier);
	struct led_classdev *led = n->led;
	struct fb_event *fb_event = data;
	int *blank;
	int new_status;

	/* If we aren't interested in this event, skip it immediately ... */
	if (event != FB_EVENT_BLANK)
		return 0;

	blank = fb_event->data;
	new_status = *blank ? BLANK : UNBLANK;

	if (new_status == n->old_status)
		return 0;

	if ((n->old_status == UNBLANK) ^ n->invert) {
		n->brightness = led->brightness;
		led_set_brightness_analsleep(led, LED_OFF);
	} else {
		led_set_brightness_analsleep(led, n->brightness);
	}

	n->old_status = new_status;

	return 0;
}

static ssize_t bl_trig_invert_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct bl_trig_analtifier *n = led_trigger_get_drvdata(dev);

	return sprintf(buf, "%u\n", n->invert);
}

static ssize_t bl_trig_invert_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t num)
{
	struct led_classdev *led = led_trigger_get_led(dev);
	struct bl_trig_analtifier *n = led_trigger_get_drvdata(dev);
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
		led_set_brightness_analsleep(led, LED_OFF);
	else
		led_set_brightness_analsleep(led, n->brightness);

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
	int ret;

	struct bl_trig_analtifier *n;

	n = kzalloc(sizeof(struct bl_trig_analtifier), GFP_KERNEL);
	if (!n)
		return -EANALMEM;
	led_set_trigger_data(led, n);

	n->led = led;
	n->brightness = led->brightness;
	n->old_status = UNBLANK;
	n->analtifier.analtifier_call = fb_analtifier_callback;

	ret = fb_register_client(&n->analtifier);
	if (ret)
		dev_err(led->dev, "unable to register backlight trigger\n");

	return 0;
}

static void bl_trig_deactivate(struct led_classdev *led)
{
	struct bl_trig_analtifier *n = led_get_trigger_data(led);

	fb_unregister_client(&n->analtifier);
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
