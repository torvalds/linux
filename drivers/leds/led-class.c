/*
 * LED Class Core
 *
 * Copyright (C) 2005 John Lenz <lenz@cs.wisc.edu>
 * Copyright (C) 2005-2006 Richard Purdie <rpurdie@openedhand.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/sysdev.h>
#include <linux/timer.h>
#include <linux/err.h>
#include <linux/ctype.h>
#include <linux/leds.h>
#include "leds.h"

static struct class *leds_class;

static ssize_t led_brightness_show(struct class_device *dev, char *buf)
{
	struct led_classdev *led_cdev = class_get_devdata(dev);
	ssize_t ret = 0;

	/* no lock needed for this */
	sprintf(buf, "%u\n", led_cdev->brightness);
	ret = strlen(buf) + 1;

	return ret;
}

static ssize_t led_brightness_store(struct class_device *dev,
				const char *buf, size_t size)
{
	struct led_classdev *led_cdev = class_get_devdata(dev);
	ssize_t ret = -EINVAL;
	char *after;
	unsigned long state = simple_strtoul(buf, &after, 10);
	size_t count = after - buf;

	if (*after && isspace(*after))
		count++;

	if (count == size) {
		ret = count;
		led_set_brightness(led_cdev, state);
	}

	return ret;
}

static CLASS_DEVICE_ATTR(brightness, 0644, led_brightness_show,
			led_brightness_store);
#ifdef CONFIG_LEDS_TRIGGERS
static CLASS_DEVICE_ATTR(trigger, 0644, led_trigger_show, led_trigger_store);
#endif

/**
 * led_classdev_suspend - suspend an led_classdev.
 * @led_cdev: the led_classdev to suspend.
 */
void led_classdev_suspend(struct led_classdev *led_cdev)
{
	led_cdev->flags |= LED_SUSPENDED;
	led_cdev->brightness_set(led_cdev, 0);
}
EXPORT_SYMBOL_GPL(led_classdev_suspend);

/**
 * led_classdev_resume - resume an led_classdev.
 * @led_cdev: the led_classdev to resume.
 */
void led_classdev_resume(struct led_classdev *led_cdev)
{
	led_cdev->brightness_set(led_cdev, led_cdev->brightness);
	led_cdev->flags &= ~LED_SUSPENDED;
}
EXPORT_SYMBOL_GPL(led_classdev_resume);

/**
 * led_classdev_register - register a new object of led_classdev class.
 * @dev: The device to register.
 * @led_cdev: the led_classdev structure for this device.
 */
int led_classdev_register(struct device *parent, struct led_classdev *led_cdev)
{
	led_cdev->class_dev = class_device_create(leds_class, NULL, 0,
						parent, "%s", led_cdev->name);
	if (unlikely(IS_ERR(led_cdev->class_dev)))
		return PTR_ERR(led_cdev->class_dev);

	class_set_devdata(led_cdev->class_dev, led_cdev);

	/* register the attributes */
	class_device_create_file(led_cdev->class_dev,
				&class_device_attr_brightness);

	/* add to the list of leds */
	write_lock(&leds_list_lock);
	list_add_tail(&led_cdev->node, &leds_list);
	write_unlock(&leds_list_lock);

#ifdef CONFIG_LEDS_TRIGGERS
	rwlock_init(&led_cdev->trigger_lock);

	led_trigger_set_default(led_cdev);

	class_device_create_file(led_cdev->class_dev,
				&class_device_attr_trigger);
#endif

	printk(KERN_INFO "Registered led device: %s\n",
			led_cdev->class_dev->class_id);

	return 0;
}
EXPORT_SYMBOL_GPL(led_classdev_register);

/**
 * led_classdev_unregister - unregisters a object of led_properties class.
 * @led_cdev: the led device to unreigister
 *
 * Unregisters a previously registered via led_classdev_register object.
 */
void led_classdev_unregister(struct led_classdev *led_cdev)
{
	class_device_remove_file(led_cdev->class_dev,
				&class_device_attr_brightness);
#ifdef CONFIG_LEDS_TRIGGERS
	class_device_remove_file(led_cdev->class_dev,
				&class_device_attr_trigger);
	write_lock(&led_cdev->trigger_lock);
	if (led_cdev->trigger)
		led_trigger_set(led_cdev, NULL);
	write_unlock(&led_cdev->trigger_lock);
#endif

	class_device_unregister(led_cdev->class_dev);

	write_lock(&leds_list_lock);
	list_del(&led_cdev->node);
	write_unlock(&leds_list_lock);
}
EXPORT_SYMBOL_GPL(led_classdev_unregister);

static int __init leds_init(void)
{
	leds_class = class_create(THIS_MODULE, "leds");
	if (IS_ERR(leds_class))
		return PTR_ERR(leds_class);
	return 0;
}

static void __exit leds_exit(void)
{
	class_destroy(leds_class);
}

subsys_initcall(leds_init);
module_exit(leds_exit);

MODULE_AUTHOR("John Lenz, Richard Purdie");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LED Class Interface");
