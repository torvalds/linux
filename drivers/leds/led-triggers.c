/*
 * LED Triggers Core
 *
 * Copyright 2005-2007 Openedhand Ltd.
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
#include <linux/rwsem.h>
#include <linux/leds.h>
#include <linux/slab.h>
#include "leds.h"

/*
 * Nests outside led_cdev->trigger_lock
 */
static DECLARE_RWSEM(triggers_list_lock);
static LIST_HEAD(trigger_list);

 /* Used by LED Class */

ssize_t led_trigger_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	char trigger_name[TRIG_NAME_MAX];
	struct led_trigger *trig;
	size_t len;

	trigger_name[sizeof(trigger_name) - 1] = '\0';
	strncpy(trigger_name, buf, sizeof(trigger_name) - 1);
	len = strlen(trigger_name);

	if (len && trigger_name[len - 1] == '\n')
		trigger_name[len - 1] = '\0';

	if (!strcmp(trigger_name, "none")) {
		led_trigger_remove(led_cdev);
		return count;
	}

	down_read(&triggers_list_lock);
	list_for_each_entry(trig, &trigger_list, next_trig) {
		if (!strcmp(trigger_name, trig->name)) {
			down_write(&led_cdev->trigger_lock);
			led_trigger_set(led_cdev, trig);
			up_write(&led_cdev->trigger_lock);

			up_read(&triggers_list_lock);
			return count;
		}
	}
	up_read(&triggers_list_lock);

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(led_trigger_store);

ssize_t led_trigger_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_trigger *trig;
	int len = 0;

	down_read(&triggers_list_lock);
	down_read(&led_cdev->trigger_lock);

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
	up_read(&led_cdev->trigger_lock);
	up_read(&triggers_list_lock);

	len += sprintf(len+buf, "\n");
	return len;
}
EXPORT_SYMBOL_GPL(led_trigger_show);

/* Caller must ensure led_cdev->trigger_lock held */
void led_trigger_set(struct led_classdev *led_cdev, struct led_trigger *trigger)
{
	unsigned long flags;

	/* Remove any existing trigger */
	if (led_cdev->trigger) {
		write_lock_irqsave(&led_cdev->trigger->leddev_list_lock, flags);
		list_del(&led_cdev->trig_list);
		write_unlock_irqrestore(&led_cdev->trigger->leddev_list_lock,
			flags);
		if (led_cdev->trigger->deactivate)
			led_cdev->trigger->deactivate(led_cdev);
		led_cdev->trigger = NULL;
		led_set_brightness(led_cdev, LED_OFF);
	}
	if (trigger) {
		write_lock_irqsave(&trigger->leddev_list_lock, flags);
		list_add_tail(&led_cdev->trig_list, &trigger->led_cdevs);
		write_unlock_irqrestore(&trigger->leddev_list_lock, flags);
		led_cdev->trigger = trigger;
		if (trigger->activate)
			trigger->activate(led_cdev);
	}
}
EXPORT_SYMBOL_GPL(led_trigger_set);

void led_trigger_remove(struct led_classdev *led_cdev)
{
	down_write(&led_cdev->trigger_lock);
	led_trigger_set(led_cdev, NULL);
	up_write(&led_cdev->trigger_lock);
}
EXPORT_SYMBOL_GPL(led_trigger_remove);

void led_trigger_set_default(struct led_classdev *led_cdev)
{
	struct led_trigger *trig;

	if (!led_cdev->default_trigger)
		return;

	down_read(&triggers_list_lock);
	down_write(&led_cdev->trigger_lock);
	list_for_each_entry(trig, &trigger_list, next_trig) {
		if (!strcmp(led_cdev->default_trigger, trig->name))
			led_trigger_set(led_cdev, trig);
	}
	up_write(&led_cdev->trigger_lock);
	up_read(&triggers_list_lock);
}
EXPORT_SYMBOL_GPL(led_trigger_set_default);

/* LED Trigger Interface */

int led_trigger_register(struct led_trigger *trigger)
{
	struct led_classdev *led_cdev;
	struct led_trigger *trig;

	rwlock_init(&trigger->leddev_list_lock);
	INIT_LIST_HEAD(&trigger->led_cdevs);

	down_write(&triggers_list_lock);
	/* Make sure the trigger's name isn't already in use */
	list_for_each_entry(trig, &trigger_list, next_trig) {
		if (!strcmp(trig->name, trigger->name)) {
			up_write(&triggers_list_lock);
			return -EEXIST;
		}
	}
	/* Add to the list of led triggers */
	list_add_tail(&trigger->next_trig, &trigger_list);
	up_write(&triggers_list_lock);

	/* Register with any LEDs that have this as a default trigger */
	down_read(&leds_list_lock);
	list_for_each_entry(led_cdev, &leds_list, node) {
		down_write(&led_cdev->trigger_lock);
		if (!led_cdev->trigger && led_cdev->default_trigger &&
			    !strcmp(led_cdev->default_trigger, trigger->name))
			led_trigger_set(led_cdev, trigger);
		up_write(&led_cdev->trigger_lock);
	}
	up_read(&leds_list_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(led_trigger_register);

void led_trigger_unregister(struct led_trigger *trigger)
{
	struct led_classdev *led_cdev;

	/* Remove from the list of led triggers */
	down_write(&triggers_list_lock);
	list_del(&trigger->next_trig);
	up_write(&triggers_list_lock);

	/* Remove anyone actively using this trigger */
	down_read(&leds_list_lock);
	list_for_each_entry(led_cdev, &leds_list, node) {
		down_write(&led_cdev->trigger_lock);
		if (led_cdev->trigger == trigger)
			led_trigger_set(led_cdev, NULL);
		up_write(&led_cdev->trigger_lock);
	}
	up_read(&leds_list_lock);
}
EXPORT_SYMBOL_GPL(led_trigger_unregister);

/* Simple LED Tigger Interface */

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
EXPORT_SYMBOL_GPL(led_trigger_event);

void led_trigger_register_simple(const char *name, struct led_trigger **tp)
{
	struct led_trigger *trigger;
	int err;

	trigger = kzalloc(sizeof(struct led_trigger), GFP_KERNEL);

	if (trigger) {
		trigger->name = name;
		err = led_trigger_register(trigger);
		if (err < 0)
			printk(KERN_WARNING "LED trigger %s failed to register"
				" (%d)\n", name, err);
	} else
		printk(KERN_WARNING "LED trigger %s failed to register"
			" (no memory)\n", name);

	*tp = trigger;
}
EXPORT_SYMBOL_GPL(led_trigger_register_simple);

void led_trigger_unregister_simple(struct led_trigger *trigger)
{
	if (trigger)
		led_trigger_unregister(trigger);
	kfree(trigger);
}
EXPORT_SYMBOL_GPL(led_trigger_unregister_simple);

MODULE_AUTHOR("Richard Purdie");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LED Triggers Core");
