/* drivers/leds/flashlight.c
 * Flashlight Class Device Driver
 *
 * Copyright (C) 2013 Richtek Technology Corp.
 * Author: Patrick Chang <patrick_chang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include "flashlight.h"
#include <linux/ctype.h>
#include <linux/err.h>
#include <linux/slab.h>

static struct class *flashlight_class;

static int flashlight_suspend(struct device *dev, pm_message_t state)
{
	struct flashlight_device *flashlight_dev = to_flashlight_device(dev);

	if (flashlight_dev->ops)
		flashlight_dev->ops->suspend(flashlight_dev, state);
	return 0;
}

static int flashlight_resume(struct device *dev)
{
	struct flashlight_device *flashlight_dev = to_flashlight_device(dev);

	if (flashlight_dev->ops)
		flashlight_dev->ops->resume(flashlight_dev);
	return 0;
}

static void flashlight_device_release(struct device *dev)
{
	struct flashlight_device *flashlight_dev = to_flashlight_device(dev);

	kfree(flashlight_dev);
}

/**
 * flashlight_device_register - create and register a new object of
 *   flashlight_device class.
 * @name: the name of the new object(must be the same as the name of the
 *   respective framebuffer device).
 * @parent: a pointer to the parent device
 * @devdata: an optional pointer to be stored for private driver use. The
 *   methods may retrieve it by using bl_get_data(flashlight_dev).
 * @ops: the flashlight operations structure.
 *
 * Creates and registers new flashlight device. Returns either an
 * ERR_PTR() or a pointer to the newly allocated device.
 */
struct flashlight_device *flashlight_device_register(const char *name,
						     struct device *parent,
						     void *devdata,
						     const struct flashlight_ops
						     *ops,
						     const struct
						     flashlight_properties
						     *props)
{
	struct flashlight_device *flashlight_dev;
	int rc;

	pr_debug("flashlight_device_register: name=%s\n", name);
	flashlight_dev = kzalloc(sizeof(*flashlight_dev), GFP_KERNEL);
	if (!flashlight_dev)
		return ERR_PTR(-ENOMEM);

	mutex_init(&flashlight_dev->ops_lock);
	flashlight_dev->dev.class = flashlight_class;
	flashlight_dev->dev.parent = parent;
	flashlight_dev->dev.release = flashlight_device_release;
	dev_set_name(&flashlight_dev->dev, name);
	dev_set_drvdata(&flashlight_dev->dev, devdata);
	/* Copy properties */
	if (props) {
		memcpy(&flashlight_dev->props, props,
		       sizeof(struct flashlight_properties));
	}
	rc = device_register(&flashlight_dev->dev);
	if (rc) {
		kfree(flashlight_dev);
		return ERR_PTR(rc);
	}
	flashlight_dev->ops = ops;
	return flashlight_dev;
}
EXPORT_SYMBOL(flashlight_device_register);

/**
 * flashlight_device_unregister - unregisters a flashlight device object.
 * @flashlight_dev: the flashlight device object to be unregistered and freed.
 *
 * Unregisters a previously registered via flashlight_device_register object.
 */
void flashlight_device_unregister(struct flashlight_device *flashlight_dev)
{
	if (!flashlight_dev)
		return;

	mutex_lock(&flashlight_dev->ops_lock);
	flashlight_dev->ops = NULL;
	mutex_unlock(&flashlight_dev->ops_lock);
	device_unregister(&flashlight_dev->dev);
}
EXPORT_SYMBOL(flashlight_device_unregister);

int flashlight_list_color_temperature(struct flashlight_device *flashlight_dev,
				      int selector)
{
	if (flashlight_dev->ops && flashlight_dev->ops->list_color_temperature)
		return flashlight_dev->ops->
		    list_color_temperature(flashlight_dev, selector);
	return -EINVAL;
}
EXPORT_SYMBOL(flashlight_list_color_temperature);

int flashlight_set_color_temperature(struct flashlight_device *flashlight_dev,
				     int minK, int maxK)
{
	int selector = 0;
	int rc;

	if ((flashlight_dev->ops == NULL) ||
	    (flashlight_dev->ops->set_color_temperature == NULL))
		return -EINVAL;
	for (selector = 0;; selector++) {
		rc = flashlight_list_color_temperature(flashlight_dev,
						       selector);
		if (rc < 0)
			return rc;
		if (rc >= minK && rc <= maxK) {
			mutex_lock(&flashlight_dev->ops_lock);
			rc = flashlight_dev->ops->
			    set_color_temperature(flashlight_dev, rc);
			mutex_unlock(&flashlight_dev->ops_lock);
			if (rc == 0)
				flashlight_dev->props.color_temperature = rc;
			return rc;
		}

	}
	return -EINVAL;
}
EXPORT_SYMBOL(flashlight_set_color_temperature);

int flashlight_set_torch_brightness(struct flashlight_device *flashlight_dev,
				    int brightness_level)
{
	int rc;

	if ((flashlight_dev->ops == NULL) ||
	    (flashlight_dev->ops->set_torch_brightness == NULL))
		return -EINVAL;
	if (brightness_level > flashlight_dev->props.torch_max_brightness)
		return -EINVAL;
	mutex_lock(&flashlight_dev->ops_lock);
	rc = flashlight_dev->ops->set_torch_brightness(flashlight_dev,
						       brightness_level);
	mutex_unlock(&flashlight_dev->ops_lock);
	if (rc < 0)
		return rc;
	flashlight_dev->props.torch_brightness = brightness_level;
	return rc;

}
EXPORT_SYMBOL(flashlight_set_torch_brightness);

int flashlight_set_strobe_brightness(struct flashlight_device *flashlight_dev,
				     int brightness_level)
{
	int rc;

	if ((flashlight_dev->ops == NULL) ||
	    (flashlight_dev->ops->set_strobe_brightness == NULL))
		return -EINVAL;
	if (brightness_level > flashlight_dev->props.strobe_max_brightness)
		return -EINVAL;
	mutex_lock(&flashlight_dev->ops_lock);
	rc = flashlight_dev->ops->set_strobe_brightness(flashlight_dev,
							brightness_level);
	mutex_unlock(&flashlight_dev->ops_lock);
	if (rc < 0)
		return rc;
	flashlight_dev->props.strobe_brightness = brightness_level;
	return rc;
}
EXPORT_SYMBOL(flashlight_set_strobe_brightness);

int flashlight_list_strobe_timeout(struct flashlight_device *flashlight_dev,
				   int selector)
{
	if (flashlight_dev->ops && flashlight_dev->ops->list_strobe_timeout) {
		return flashlight_dev->ops->list_strobe_timeout(flashlight_dev,
								selector);
	}
	return -EINVAL;
}
EXPORT_SYMBOL(flashlight_list_strobe_timeout);

int flashlight_set_strobe_timeout(struct flashlight_device *flashlight_dev,
				  int min_ms, int max_ms)
{
	int selector = 0;
	int rc = -EINVAL;
	int timeout;

	if ((flashlight_dev->ops == NULL) ||
	    (flashlight_dev->ops->set_strobe_timeout == NULL))
		return -EINVAL;
	for (selector = 0;; selector++) {
		timeout =
		    flashlight_list_strobe_timeout(flashlight_dev, selector);
		if (timeout < 0)
			return timeout;
		if (timeout >= min_ms && timeout <= max_ms) {
			mutex_lock(&flashlight_dev->ops_lock);
			rc = flashlight_dev->ops->
			    set_strobe_timeout(flashlight_dev, timeout);
			mutex_unlock(&flashlight_dev->ops_lock);
			if (rc == 0)
				flashlight_dev->props.strobe_timeout = timeout;
			return rc;
		}
	}
	return -EINVAL;
}
EXPORT_SYMBOL(flashlight_set_strobe_timeout);

int flashlight_set_mode(struct flashlight_device *flashlight_dev, int mode)
{
	int rc;

	if (mode >= FLASHLIGHT_MODE_MAX || mode < 0)
		return -EINVAL;
	if ((flashlight_dev->ops == NULL) ||
	    (flashlight_dev->ops->set_mode == NULL)) {
		flashlight_dev->props.mode = mode;
		return 0;
	}
	mutex_lock(&flashlight_dev->ops_lock);
	rc = flashlight_dev->ops->set_mode(flashlight_dev, mode);
	mutex_unlock(&flashlight_dev->ops_lock);
	if (rc < 0)
		return rc;
	flashlight_dev->props.mode = mode;
	return rc;
}
EXPORT_SYMBOL(flashlight_set_mode);

int flashlight_strobe(struct flashlight_device *flashlight_dev)
{
	if (flashlight_dev->props.mode == FLASHLIGHT_MODE_FLASH
	    || flashlight_dev->props.mode == FLASHLIGHT_MODE_MIXED) {
		if (flashlight_dev->ops == NULL ||
		    flashlight_dev->ops->strobe == NULL)
			return -EINVAL;
		return flashlight_dev->ops->strobe(flashlight_dev);
	}
	return -EINVAL;
}
EXPORT_SYMBOL(flashlight_strobe);

static int flashlight_match_device_by_name(struct device *dev, const void *data)
{
	const char *name = data;

	return strcmp(dev_name(dev), name) == 0;
}

struct flashlight_device *find_flashlight_by_name(char *name)
{
	struct device *dev;

	if (!name)
		return (struct flashlight_device *)NULL;
	dev = class_find_device(flashlight_class, NULL, (void*)name,
				flashlight_match_device_by_name);

	return dev ? to_flashlight_device(dev) : NULL;

}
EXPORT_SYMBOL(find_flashlight_by_name);

int flashlight_strobe_charge(struct flashlight_device *flashlight_dev,
			     flashlight_charge_event_cb cb, void *data,
			     int start)
{

	if (flashlight_dev->ops->strobe_charge)
		return flashlight_dev->ops->strobe_charge(flashlight_dev,
							  cb, data, start);
	if (flashlight_dev->props.type == FLASHLIGHT_TYPE_LED) {
		if (cb)
			cb(data, 0);
		return 0;
	}
	return -EINVAL;
}
EXPORT_SYMBOL(flashlight_strobe_charge);

static void __exit flashlight_class_exit(void)
{
	class_destroy(flashlight_class);
}

static int __init flashlight_class_init(void)
{
	flashlight_class = class_create(THIS_MODULE, "flashlight");
	if (IS_ERR(flashlight_class)) {
		pr_err(
		       "Unable to create flashlight class; errno = %ld\n",
		       PTR_ERR(flashlight_class));
		return PTR_ERR(flashlight_class);
	}

	flashlight_class->suspend = flashlight_suspend;
	flashlight_class->resume = flashlight_resume;
	return 0;
}
subsys_initcall(flashlight_class_init);
module_exit(flashlight_class_exit);

MODULE_DESCRIPTION("Flashlight Class Device");
MODULE_AUTHOR("Patrick Chang <patrick_chang@richtek.com>");
MODULE_VERSION("1.0.0_G");
MODULE_LICENSE("GPL");
