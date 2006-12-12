/*
 * LED Triggers Core
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
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/sysdev.h>
#include <linux/timer.h>
#include <linux/leds.h>
#include "leds.h"

/*
 * Nests outside led_cdev->trigger_lock
 */
static DEFINE_RWLOCK(triggers_list_lock);
static LIST_HEAD(trigger_list);

ssize_t led_trigger_store(struct class_device *dev, const char *buf,
			size_t count)
{
	struct led_classdev *led_cdev = class_get_devdata(dev);
	char trigger_name[TRIG_NAME_MAX];
	struct led_trigger *trig;
	size_t len;

	trigger_name[sizeof(trigger_name) - 1] = '\0';
	strncpy(trigger_name, buf, sizeof(trigger_name) - 1);
	len = strlen(trigger_name);

	if (len && trigger_name[len - 1] == '\n')
		trigger_name[len - 1] = '\0';

	if (!strcmp(trigger_name, "none")) {
		write_lock(&led_cdev->trigger_lock);
		led_trigger_set(led_cdev, NULL);
		write_unlock(&led_cdev->trigger_lock);
		return count;
	}

	read_lock(&triggers_list_lock);
	list_for_each_entry(trig, &trigger_list, next_trig) {
		if (!strcmp(trigger_name, trig->name)) {
			write_lock(&led_cdev->trigger_lock);
			led_trigger_set(led_cdev, trig);
			write_unlock(&led_cdev->trigger_lock);

			read_unlock(&triggers_list_lock);
			return count;
		}
	}
	read_unlock(&triggers_list_lock);

	return -EINVAL;
}


ssize_t led_trigger_show(struct class_device *dev, char *buf)
{
	struct led_classdev *led_cdev = class_get_devdata(dev);
	struct led_trigger *trig;
	int len = 0;

	read_lock(&triggers_list_lock);
	read_lock(&led_cdev->trigger_lock);

	if (!led_cdev->trigger)
		len += sprintf(buf+len, "[none] ");
	else
		len += sprintf(buf+len, "none ");

	list_for_each_entry(trig, &trigger_list, next_trig) {
		if (led_cdev->trigger && !strcmp(led_cdev->trigger->name,
							trig->name))
			len += sprintf(buf+len, "[%s] ", trig->name);
		else
			len += sprintf(buf+len, "%s ", trig->name);
	}
	read_unlock(&led_cdev->trigger_lock);
	read_unlock(&triggers_list_lock);

	len += sprintf(len+buf, "\n");
	return len;
}

void led_trigger_event(struct led_trigger *trigger,
			enum led_brightness brightness)
{
	struct list_head *entry;

	if (!trigger)
		return;

	read_lock(&trigger->leddev_list_lock);
	list_for_each(entry, &trigger->led_cdevs) {
		struct led_classdev *led_cdev;

		led_cdev = list_entry(entry, struct led_classdev, trig_list);
		led_set_brightness(led_cdev, brightness);
	}
	read_unlock(&trigger->leddev_list_lock);
}

/* Caller must ensure led_cdev->trigger_lock held */
void led_trigger_set(struct led_classdev *led_cdev, struct led_trigger *trigger)
{
	unsigned long flags;

	/* Remove any existing trigger */
	if (led_cdev->trigger) {
		write_lock_irqsave(&led_cdev->trigger->leddev_list_lock, flags);
		list_del(&led_cdev->trig_list);
		write_unlock_irqrestore(&led_cdev->trigger->leddev_list_lock, flags);
		if (led_cdev->trigger->deactivate)
			led_cdev->trigger->deactivate(led_cdev);
		led_set_brightness(led_cdev, LED_OFF);
	}
	if (trigger) {
		write_lock_irqsave(&trigger->leddev_list_lock, flags);
		list_add_tail(&led_cdev->trig_list, &trigger->led_cdevs);
		write_unlock_irqrestore(&trigger->leddev_list_lock, flags);
		if (trigger->activate)
			trigger->activate(led_cdev);
	}
	led_cdev->trigger = trigger;
}

void led_trigger_set_default(struct led_classdev *led_cdev)
{
	struct led_trigger *trig;

	if (!led_cdev->default_trigger)
		return;

	read_lock(&triggers_list_lock);
	write_lock(&led_cdev->trigger_lock);
	list_for_each_entry(trig, &trigger_list, next_trig) {
		if (!strcmp(led_cdev->default_trigger, trig->name))
			led_trigger_set(led_cdev, trig);
	}
	write_unlock(&led_cdev->trigger_lock);
	read_unlock(&triggers_list_lock);
}

int led_trigger_register(struct led_trigger *trigger)
{
	struct led_classdev *led_cdev;

	rwlock_init(&trigger->leddev_list_lock);
	INIT_LIST_HEAD(&trigger->led_cdevs);

	/* Add to the list of led triggers */
	write_lock(&triggers_list_lock);
	list_add_tail(&trigger->next_trig, &trigger_list);
	write_unlock(&triggers_list_lock);

	/* Register with any LEDs that have this as a default trigger */
	read_lock(&leds_list_lock);
	list_for_each_entry(led_cdev, &leds_list, node) {
		write_lock(&led_cdev->trigger_lock);
		if (!led_cdev->trigger && led_cdev->default_trigger &&
			    !strcmp(led_cdev->default_trigger, trigger->name))
			led_trigger_set(led_cdev, trigger);
		write_unlock(&led_cdev->trigger_lock);
	}
	read_unlock(&leds_list_lock);

	return 0;
}

void led_trigger_register_simple(const char *name, struct led_trigger **tp)
{
	struct led_trigger *trigger;

	trigger = kzalloc(sizeof(struct led_trigger), GFP_KERNEL);

	if (trigger) {
		trigger->name = name;
		led_trigger_register(trigger);
	}
	*tp = trigger;
}

void led_trigger_unregister(struct led_trigger *trigger)
{
	struct led_classdev *led_cdev;

	/* Remove from the list of led triggers */
	write_lock(&triggers_list_lock);
	list_del(&trigger->next_trig);
	write_unlock(&triggers_list_lock);

	/* Remove anyone actively using this trigger */
	read_lock(&leds_list_lock);
	list_for_each_entry(led_cdev, &leds_list, node) {
		write_lock(&led_cdev->trigger_lock);
		if (led_cdev->trigger == trigger)
			led_trigger_set(led_cdev, NULL);
		write_unlock(&led_cdev->trigger_lock);
	}
	read_unlock(&leds_list_lock);
}

void led_trigger_unregister_simple(struct led_trigger *trigger)
{
	led_trigger_unregister(trigger);
	kfree(trigger);
}

/* Used by LED Class */
EXPORT_SYMBOL_GPL(led_trigger_set);
EXPORT_SYMBOL_GPL(led_trigger_set_default);
EXPORT_SYMBOL_GPL(led_trigger_show);
EXPORT_SYMBOL_GPL(led_trigger_store);

/* LED Trigger Interface */
EXPORT_SYMBOL_GPL(led_trigger_register);
EXPORT_SYMBOL_GPL(led_trigger_unregister);

/* Simple LED Tigger Interface */
EXPORT_SYMBOL_GPL(led_trigger_register_simple);
EXPORT_SYMBOL_GPL(led_trigger_unregister_simple);
EXPORT_SYMBOL_GPL(led_trigger_event);

MODULE_AUTHOR("Richard Purdie");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LED Triggers Core");
