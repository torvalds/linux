// SPDX-License-Identifier: GPL-2.0-only
/*
 * LED Class Core
 *
 * Copyright (C) 2005 John Lenz <lenz@cs.wisc.edu>
 * Copyright (C) 2005-2007 Richard Purdie <rpurdie@openedhand.com>
 */

#include <linux/ctype.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <uapi/linux/uleds.h>
#include <linux/of.h>
#include "leds.h"

static DEFINE_MUTEX(leds_lookup_lock);
static LIST_HEAD(leds_lookup_list);

static struct workqueue_struct *leds_wq;

static ssize_t brightness_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	unsigned int brightness;

	mutex_lock(&led_cdev->led_access);
	led_update_brightness(led_cdev);
	brightness = led_cdev->brightness;
	mutex_unlock(&led_cdev->led_access);

	return sprintf(buf, "%u\n", brightness);
}

static ssize_t brightness_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	unsigned long state;
	ssize_t ret;

	mutex_lock(&led_cdev->led_access);

	if (led_sysfs_is_disabled(led_cdev)) {
		ret = -EBUSY;
		goto unlock;
	}

	ret = kstrtoul(buf, 10, &state);
	if (ret)
		goto unlock;

	if (state == LED_OFF)
		led_trigger_remove(led_cdev);
	led_set_brightness(led_cdev, state);

	ret = size;
unlock:
	mutex_unlock(&led_cdev->led_access);
	return ret;
}
static DEVICE_ATTR_RW(brightness);

static ssize_t max_brightness_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	unsigned int max_brightness;

	mutex_lock(&led_cdev->led_access);
	max_brightness = led_cdev->max_brightness;
	mutex_unlock(&led_cdev->led_access);

	return sprintf(buf, "%u\n", max_brightness);
}
static DEVICE_ATTR_RO(max_brightness);

#ifdef CONFIG_LEDS_TRIGGERS
static const BIN_ATTR(trigger, 0644, led_trigger_read, led_trigger_write, 0);
static const struct bin_attribute *const led_trigger_bin_attrs[] = {
	&bin_attr_trigger,
	NULL,
};
static const struct attribute_group led_trigger_group = {
	.bin_attrs = led_trigger_bin_attrs,
};
#endif

static struct attribute *led_class_attrs[] = {
	&dev_attr_brightness.attr,
	&dev_attr_max_brightness.attr,
	NULL,
};

static const struct attribute_group led_group = {
	.attrs = led_class_attrs,
};

static const struct attribute_group *led_groups[] = {
	&led_group,
#ifdef CONFIG_LEDS_TRIGGERS
	&led_trigger_group,
#endif
	NULL,
};

#ifdef CONFIG_LEDS_BRIGHTNESS_HW_CHANGED
static ssize_t brightness_hw_changed_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	if (led_cdev->brightness_hw_changed == -1)
		return -ENODATA;

	return sprintf(buf, "%u\n", led_cdev->brightness_hw_changed);
}

static DEVICE_ATTR_RO(brightness_hw_changed);

static int led_add_brightness_hw_changed(struct led_classdev *led_cdev)
{
	struct device *dev = led_cdev->dev;
	int ret;

	ret = device_create_file(dev, &dev_attr_brightness_hw_changed);
	if (ret) {
		dev_err(dev, "Error creating brightness_hw_changed\n");
		return ret;
	}

	led_cdev->brightness_hw_changed_kn =
		sysfs_get_dirent(dev->kobj.sd, "brightness_hw_changed");
	if (!led_cdev->brightness_hw_changed_kn) {
		dev_err(dev, "Error getting brightness_hw_changed kn\n");
		device_remove_file(dev, &dev_attr_brightness_hw_changed);
		return -ENXIO;
	}

	return 0;
}

static void led_remove_brightness_hw_changed(struct led_classdev *led_cdev)
{
	sysfs_put(led_cdev->brightness_hw_changed_kn);
	device_remove_file(led_cdev->dev, &dev_attr_brightness_hw_changed);
}

void led_classdev_notify_brightness_hw_changed(struct led_classdev *led_cdev, unsigned int brightness)
{
	if (WARN_ON(!led_cdev->brightness_hw_changed_kn))
		return;

	led_cdev->brightness_hw_changed = brightness;
	sysfs_notify_dirent(led_cdev->brightness_hw_changed_kn);
}
EXPORT_SYMBOL_GPL(led_classdev_notify_brightness_hw_changed);
#else
static int led_add_brightness_hw_changed(struct led_classdev *led_cdev)
{
	return 0;
}
static void led_remove_brightness_hw_changed(struct led_classdev *led_cdev)
{
}
#endif

/**
 * led_classdev_suspend - suspend an led_classdev.
 * @led_cdev: the led_classdev to suspend.
 */
void led_classdev_suspend(struct led_classdev *led_cdev)
{
	led_cdev->flags |= LED_SUSPENDED;
	led_set_brightness_nopm(led_cdev, 0);
	flush_work(&led_cdev->set_brightness_work);
}
EXPORT_SYMBOL_GPL(led_classdev_suspend);

/**
 * led_classdev_resume - resume an led_classdev.
 * @led_cdev: the led_classdev to resume.
 */
void led_classdev_resume(struct led_classdev *led_cdev)
{
	led_set_brightness_nopm(led_cdev, led_cdev->brightness);

	if (led_cdev->flash_resume)
		led_cdev->flash_resume(led_cdev);

	led_cdev->flags &= ~LED_SUSPENDED;
}
EXPORT_SYMBOL_GPL(led_classdev_resume);

#ifdef CONFIG_PM_SLEEP
static int led_suspend(struct device *dev)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	if (led_cdev->flags & LED_CORE_SUSPENDRESUME)
		led_classdev_suspend(led_cdev);

	return 0;
}

static int led_resume(struct device *dev)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);

	if (led_cdev->flags & LED_CORE_SUSPENDRESUME)
		led_classdev_resume(led_cdev);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(leds_class_dev_pm_ops, led_suspend, led_resume);

static struct led_classdev *led_module_get(struct device *led_dev)
{
	struct led_classdev *led_cdev;

	if (!led_dev)
		return ERR_PTR(-EPROBE_DEFER);

	led_cdev = dev_get_drvdata(led_dev);

	if (!try_module_get(led_cdev->dev->parent->driver->owner)) {
		put_device(led_cdev->dev);
		return ERR_PTR(-ENODEV);
	}

	return led_cdev;
}

static const struct class leds_class = {
	.name = "leds",
	.dev_groups = led_groups,
	.pm = &leds_class_dev_pm_ops,
};

/**
 * of_led_get() - request a LED device via the LED framework
 * @np: device node to get the LED device from
 * @index: the index of the LED
 * @name: the name of the LED used to map it to its function, if present
 *
 * Returns the LED device parsed from the phandle specified in the "leds"
 * property of a device tree node or a negative error-code on failure.
 */
static struct led_classdev *of_led_get(struct device_node *np, int index,
				       const char *name)
{
	struct device *led_dev;
	struct device_node *led_node;

	/*
	 * For named LEDs, first look up the name in the "led-names" property.
	 * If it cannot be found, then of_parse_phandle() will propagate the error.
	 */
	if (name)
		index = of_property_match_string(np, "led-names", name);
	led_node = of_parse_phandle(np, "leds", index);
	if (!led_node)
		return ERR_PTR(-ENOENT);

	led_dev = class_find_device_by_of_node(&leds_class, led_node);
	of_node_put(led_node);

	return led_module_get(led_dev);
}

/**
 * led_put() - release a LED device
 * @led_cdev: LED device
 */
void led_put(struct led_classdev *led_cdev)
{
	module_put(led_cdev->dev->parent->driver->owner);
	put_device(led_cdev->dev);
}
EXPORT_SYMBOL_GPL(led_put);

static void devm_led_release(struct device *dev, void *res)
{
	struct led_classdev **p = res;

	led_put(*p);
}

static struct led_classdev *__devm_led_get(struct device *dev, struct led_classdev *led)
{
	struct led_classdev **dr;

	dr = devres_alloc(devm_led_release, sizeof(struct led_classdev *), GFP_KERNEL);
	if (!dr) {
		led_put(led);
		return ERR_PTR(-ENOMEM);
	}

	*dr = led;
	devres_add(dev, dr);

	return led;
}

/**
 * devm_of_led_get - Resource-managed request of a LED device
 * @dev:	LED consumer
 * @index:	index of the LED to obtain in the consumer
 *
 * The device node of the device is parse to find the request LED device.
 * The LED device returned from this function is automatically released
 * on driver detach.
 *
 * @return a pointer to a LED device or ERR_PTR(errno) on failure.
 */
struct led_classdev *__must_check devm_of_led_get(struct device *dev,
						  int index)
{
	struct led_classdev *led;

	if (!dev)
		return ERR_PTR(-EINVAL);

	led = of_led_get(dev->of_node, index, NULL);
	if (IS_ERR(led))
		return led;

	return __devm_led_get(dev, led);
}
EXPORT_SYMBOL_GPL(devm_of_led_get);

/**
 * led_get() - request a LED device via the LED framework
 * @dev: device for which to get the LED device
 * @con_id: name of the LED from the device's point of view
 *
 * @return a pointer to a LED device or ERR_PTR(errno) on failure.
 */
struct led_classdev *led_get(struct device *dev, char *con_id)
{
	struct led_lookup_data *lookup;
	struct led_classdev *led_cdev;
	const char *provider = NULL;
	struct device *led_dev;

	led_cdev = of_led_get(dev->of_node, -1, con_id);
	if (!IS_ERR(led_cdev) || PTR_ERR(led_cdev) != -ENOENT)
		return led_cdev;

	mutex_lock(&leds_lookup_lock);
	list_for_each_entry(lookup, &leds_lookup_list, list) {
		if (!strcmp(lookup->dev_id, dev_name(dev)) &&
		    !strcmp(lookup->con_id, con_id)) {
			provider = kstrdup_const(lookup->provider, GFP_KERNEL);
			break;
		}
	}
	mutex_unlock(&leds_lookup_lock);

	if (!provider)
		return ERR_PTR(-ENOENT);

	led_dev = class_find_device_by_name(&leds_class, provider);
	kfree_const(provider);

	return led_module_get(led_dev);
}
EXPORT_SYMBOL_GPL(led_get);

/**
 * devm_led_get() - request a LED device via the LED framework
 * @dev: device for which to get the LED device
 * @con_id: name of the LED from the device's point of view
 *
 * The LED device returned from this function is automatically released
 * on driver detach.
 *
 * @return a pointer to a LED device or ERR_PTR(errno) on failure.
 */
struct led_classdev *devm_led_get(struct device *dev, char *con_id)
{
	struct led_classdev *led;

	led = led_get(dev, con_id);
	if (IS_ERR(led))
		return led;

	return __devm_led_get(dev, led);
}
EXPORT_SYMBOL_GPL(devm_led_get);

/**
 * led_add_lookup() - Add a LED lookup table entry
 * @led_lookup: the lookup table entry to add
 *
 * Add a LED lookup table entry. On systems without devicetree the lookup table
 * is used by led_get() to find LEDs.
 */
void led_add_lookup(struct led_lookup_data *led_lookup)
{
	mutex_lock(&leds_lookup_lock);
	list_add_tail(&led_lookup->list, &leds_lookup_list);
	mutex_unlock(&leds_lookup_lock);
}
EXPORT_SYMBOL_GPL(led_add_lookup);

/**
 * led_remove_lookup() - Remove a LED lookup table entry
 * @led_lookup: the lookup table entry to remove
 */
void led_remove_lookup(struct led_lookup_data *led_lookup)
{
	mutex_lock(&leds_lookup_lock);
	list_del(&led_lookup->list);
	mutex_unlock(&leds_lookup_lock);
}
EXPORT_SYMBOL_GPL(led_remove_lookup);

/**
 * devm_of_led_get_optional - Resource-managed request of an optional LED device
 * @dev:	LED consumer
 * @index:	index of the LED to obtain in the consumer
 *
 * The device node of the device is parsed to find the requested LED device.
 * The LED device returned from this function is automatically released
 * on driver detach.
 *
 * @return a pointer to a LED device, ERR_PTR(errno) on failure and NULL if the
 * led was not found.
 */
struct led_classdev *__must_check devm_of_led_get_optional(struct device *dev,
							int index)
{
	struct led_classdev *led;

	led = devm_of_led_get(dev, index);
	if (IS_ERR(led) && PTR_ERR(led) == -ENOENT)
		return NULL;

	return led;
}
EXPORT_SYMBOL_GPL(devm_of_led_get_optional);

static int led_classdev_next_name(const char *init_name, char *name,
				  size_t len)
{
	unsigned int i = 0;
	int ret = 0;
	struct device *dev;

	strscpy(name, init_name, len);

	while ((ret < len) &&
	       (dev = class_find_device_by_name(&leds_class, name))) {
		put_device(dev);
		ret = snprintf(name, len, "%s_%u", init_name, ++i);
	}

	if (ret >= len)
		return -ENOMEM;

	return i;
}

/**
 * led_classdev_register_ext - register a new object of led_classdev class
 *			       with init data.
 *
 * @parent: parent of LED device
 * @led_cdev: the led_classdev structure for this device.
 * @init_data: LED class device initialization data
 */
int led_classdev_register_ext(struct device *parent,
			      struct led_classdev *led_cdev,
			      struct led_init_data *init_data)
{
	char composed_name[LED_MAX_NAME_SIZE];
	char final_name[LED_MAX_NAME_SIZE];
	const char *proposed_name = composed_name;
	int ret;

	if (init_data) {
		if (init_data->devname_mandatory && !init_data->devicename) {
			dev_err(parent, "Mandatory device name is missing");
			return -EINVAL;
		}
		ret = led_compose_name(parent, init_data, composed_name);
		if (ret < 0)
			return ret;

		if (init_data->fwnode) {
			fwnode_property_read_string(init_data->fwnode,
				"linux,default-trigger",
				&led_cdev->default_trigger);

			if (fwnode_property_present(init_data->fwnode,
						    "retain-state-shutdown"))
				led_cdev->flags |= LED_RETAIN_AT_SHUTDOWN;

			fwnode_property_read_u32(init_data->fwnode,
				"max-brightness",
				&led_cdev->max_brightness);

			if (fwnode_property_present(init_data->fwnode, "color"))
				fwnode_property_read_u32(init_data->fwnode, "color",
							 &led_cdev->color);
		}
	} else {
		proposed_name = led_cdev->name;
	}

	ret = led_classdev_next_name(proposed_name, final_name, sizeof(final_name));
	if (ret < 0)
		return ret;
	else if (ret && led_cdev->flags & LED_REJECT_NAME_CONFLICT)
		return -EEXIST;
	else if (ret)
		dev_warn(parent, "Led %s renamed to %s due to name collision\n",
			 proposed_name, final_name);

	if (led_cdev->color >= LED_COLOR_ID_MAX)
		dev_warn(parent, "LED %s color identifier out of range\n", final_name);

	mutex_init(&led_cdev->led_access);
	mutex_lock(&led_cdev->led_access);
	led_cdev->dev = device_create_with_groups(&leds_class, parent, 0,
						  led_cdev, led_cdev->groups, "%s", final_name);
	if (IS_ERR(led_cdev->dev)) {
		mutex_unlock(&led_cdev->led_access);
		return PTR_ERR(led_cdev->dev);
	}
	if (init_data && init_data->fwnode)
		device_set_node(led_cdev->dev, init_data->fwnode);

	if (led_cdev->flags & LED_BRIGHT_HW_CHANGED) {
		ret = led_add_brightness_hw_changed(led_cdev);
		if (ret) {
			device_unregister(led_cdev->dev);
			led_cdev->dev = NULL;
			mutex_unlock(&led_cdev->led_access);
			return ret;
		}
	}

	led_cdev->work_flags = 0;
#ifdef CONFIG_LEDS_TRIGGERS
	init_rwsem(&led_cdev->trigger_lock);
#endif
#ifdef CONFIG_LEDS_BRIGHTNESS_HW_CHANGED
	led_cdev->brightness_hw_changed = -1;
#endif
	/* add to the list of leds */
	down_write(&leds_list_lock);
	list_add_tail(&led_cdev->node, &leds_list);
	up_write(&leds_list_lock);

	if (!led_cdev->max_brightness)
		led_cdev->max_brightness = LED_FULL;

	led_update_brightness(led_cdev);

	led_cdev->wq = leds_wq;

	led_init_core(led_cdev);

#ifdef CONFIG_LEDS_TRIGGERS
	led_trigger_set_default(led_cdev);
#endif

	mutex_unlock(&led_cdev->led_access);

	dev_dbg(parent, "Registered led device: %s\n",
			led_cdev->name);

	return 0;
}
EXPORT_SYMBOL_GPL(led_classdev_register_ext);

/**
 * led_classdev_unregister - unregisters a object of led_properties class.
 * @led_cdev: the led device to unregister
 *
 * Unregisters a previously registered via led_classdev_register object.
 */
void led_classdev_unregister(struct led_classdev *led_cdev)
{
	if (IS_ERR_OR_NULL(led_cdev->dev))
		return;

#ifdef CONFIG_LEDS_TRIGGERS
	down_write(&led_cdev->trigger_lock);
	if (led_cdev->trigger)
		led_trigger_set(led_cdev, NULL);
	up_write(&led_cdev->trigger_lock);
#endif

	led_cdev->flags |= LED_UNREGISTERING;

	/* Stop blinking */
	led_stop_software_blink(led_cdev);

	if (!(led_cdev->flags & LED_RETAIN_AT_SHUTDOWN))
		led_set_brightness(led_cdev, LED_OFF);

	flush_work(&led_cdev->set_brightness_work);

	if (led_cdev->flags & LED_BRIGHT_HW_CHANGED)
		led_remove_brightness_hw_changed(led_cdev);

	device_unregister(led_cdev->dev);

	down_write(&leds_list_lock);
	list_del(&led_cdev->node);
	up_write(&leds_list_lock);

	mutex_destroy(&led_cdev->led_access);
}
EXPORT_SYMBOL_GPL(led_classdev_unregister);

static void devm_led_classdev_release(struct device *dev, void *res)
{
	led_classdev_unregister(*(struct led_classdev **)res);
}

/**
 * devm_led_classdev_register_ext - resource managed led_classdev_register_ext()
 *
 * @parent: parent of LED device
 * @led_cdev: the led_classdev structure for this device.
 * @init_data: LED class device initialization data
 */
int devm_led_classdev_register_ext(struct device *parent,
				   struct led_classdev *led_cdev,
				   struct led_init_data *init_data)
{
	struct led_classdev **dr;
	int rc;

	dr = devres_alloc(devm_led_classdev_release, sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return -ENOMEM;

	rc = led_classdev_register_ext(parent, led_cdev, init_data);
	if (rc) {
		devres_free(dr);
		return rc;
	}

	*dr = led_cdev;
	devres_add(parent, dr);

	return 0;
}
EXPORT_SYMBOL_GPL(devm_led_classdev_register_ext);

static int devm_led_classdev_match(struct device *dev, void *res, void *data)
{
	struct led_classdev **p = res;

	if (WARN_ON(!p || !*p))
		return 0;

	return *p == data;
}

/**
 * devm_led_classdev_unregister() - resource managed led_classdev_unregister()
 * @dev: The device to unregister.
 * @led_cdev: the led_classdev structure for this device.
 */
void devm_led_classdev_unregister(struct device *dev,
				  struct led_classdev *led_cdev)
{
	WARN_ON(devres_release(dev,
			       devm_led_classdev_release,
			       devm_led_classdev_match, led_cdev));
}
EXPORT_SYMBOL_GPL(devm_led_classdev_unregister);

static int __init leds_init(void)
{
	leds_wq = alloc_ordered_workqueue("leds", 0);
	if (!leds_wq) {
		pr_err("Failed to create LEDs ordered workqueue\n");
		return -ENOMEM;
	}

	return class_register(&leds_class);
}

static void __exit leds_exit(void)
{
	class_unregister(&leds_class);
	destroy_workqueue(leds_wq);
}

subsys_initcall(leds_init);
module_exit(leds_exit);

MODULE_AUTHOR("John Lenz, Richard Purdie");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LED Class Interface");
