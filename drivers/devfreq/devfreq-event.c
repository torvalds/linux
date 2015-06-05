/*
 * devfreq-event: a framework to provide raw data and events of devfreq devices
 *
 * Copyright (C) 2015 Samsung Electronics
 * Author: Chanwoo Choi <cw00.choi@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This driver is based on drivers/devfreq/devfreq.c.
 */

#include <linux/devfreq-event.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/of.h>

static struct class *devfreq_event_class;

/* The list of all devfreq event list */
static LIST_HEAD(devfreq_event_list);
static DEFINE_MUTEX(devfreq_event_list_lock);

#define to_devfreq_event(DEV) container_of(DEV, struct devfreq_event_dev, dev)

/**
 * devfreq_event_enable_edev() - Enable the devfreq-event dev and increase
 *				 the enable_count of devfreq-event dev.
 * @edev	: the devfreq-event device
 *
 * Note that this function increase the enable_count and enable the
 * devfreq-event device. The devfreq-event device should be enabled before
 * using it by devfreq device.
 */
int devfreq_event_enable_edev(struct devfreq_event_dev *edev)
{
	int ret = 0;

	if (!edev || !edev->desc)
		return -EINVAL;

	mutex_lock(&edev->lock);
	if (edev->desc->ops && edev->desc->ops->enable
			&& edev->enable_count == 0) {
		ret = edev->desc->ops->enable(edev);
		if (ret < 0)
			goto err;
	}
	edev->enable_count++;
err:
	mutex_unlock(&edev->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(devfreq_event_enable_edev);

/**
 * devfreq_event_disable_edev() - Disable the devfreq-event dev and decrease
 *				  the enable_count of the devfreq-event dev.
 * @edev	: the devfreq-event device
 *
 * Note that this function decrease the enable_count and disable the
 * devfreq-event device. After the devfreq-event device is disabled,
 * devfreq device can't use the devfreq-event device for get/set/reset
 * operations.
 */
int devfreq_event_disable_edev(struct devfreq_event_dev *edev)
{
	int ret = 0;

	if (!edev || !edev->desc)
		return -EINVAL;

	mutex_lock(&edev->lock);
	if (edev->enable_count <= 0) {
		dev_warn(&edev->dev, "unbalanced enable_count\n");
		ret = -EIO;
		goto err;
	}

	if (edev->desc->ops && edev->desc->ops->disable
			&& edev->enable_count == 1) {
		ret = edev->desc->ops->disable(edev);
		if (ret < 0)
			goto err;
	}
	edev->enable_count--;
err:
	mutex_unlock(&edev->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(devfreq_event_disable_edev);

/**
 * devfreq_event_is_enabled() - Check whether devfreq-event dev is enabled or
 *				not.
 * @edev	: the devfreq-event device
 *
 * Note that this function check whether devfreq-event dev is enabled or not.
 * If return true, the devfreq-event dev is enabeld. If return false, the
 * devfreq-event dev is disabled.
 */
bool devfreq_event_is_enabled(struct devfreq_event_dev *edev)
{
	bool enabled = false;

	if (!edev || !edev->desc)
		return enabled;

	mutex_lock(&edev->lock);

	if (edev->enable_count > 0)
		enabled = true;

	mutex_unlock(&edev->lock);

	return enabled;
}
EXPORT_SYMBOL_GPL(devfreq_event_is_enabled);

/**
 * devfreq_event_set_event() - Set event to devfreq-event dev to start.
 * @edev	: the devfreq-event device
 *
 * Note that this function set the event to the devfreq-event device to start
 * for getting the event data which could be various event type.
 */
int devfreq_event_set_event(struct devfreq_event_dev *edev)
{
	int ret;

	if (!edev || !edev->desc)
		return -EINVAL;

	if (!edev->desc->ops || !edev->desc->ops->set_event)
		return -EINVAL;

	if (!devfreq_event_is_enabled(edev))
		return -EPERM;

	mutex_lock(&edev->lock);
	ret = edev->desc->ops->set_event(edev);
	mutex_unlock(&edev->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(devfreq_event_set_event);

/**
 * devfreq_event_get_event() - Get {load|total}_count from devfreq-event dev.
 * @edev	: the devfreq-event device
 * @edata	: the calculated data of devfreq-event device
 *
 * Note that this function get the calculated event data from devfreq-event dev
 * after stoping the progress of whole sequence of devfreq-event dev.
 */
int devfreq_event_get_event(struct devfreq_event_dev *edev,
			    struct devfreq_event_data *edata)
{
	int ret;

	if (!edev || !edev->desc)
		return -EINVAL;

	if (!edev->desc->ops || !edev->desc->ops->get_event)
		return -EINVAL;

	if (!devfreq_event_is_enabled(edev))
		return -EINVAL;

	edata->total_count = edata->load_count = 0;

	mutex_lock(&edev->lock);
	ret = edev->desc->ops->get_event(edev, edata);
	if (ret < 0)
		edata->total_count = edata->load_count = 0;
	mutex_unlock(&edev->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(devfreq_event_get_event);

/**
 * devfreq_event_reset_event() - Reset all opeations of devfreq-event dev.
 * @edev	: the devfreq-event device
 *
 * Note that this function stop all operations of devfreq-event dev and reset
 * the current event data to make the devfreq-event device into initial state.
 */
int devfreq_event_reset_event(struct devfreq_event_dev *edev)
{
	int ret = 0;

	if (!edev || !edev->desc)
		return -EINVAL;

	if (!devfreq_event_is_enabled(edev))
		return -EPERM;

	mutex_lock(&edev->lock);
	if (edev->desc->ops && edev->desc->ops->reset)
		ret = edev->desc->ops->reset(edev);
	mutex_unlock(&edev->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(devfreq_event_reset_event);

/**
 * devfreq_event_get_edev_by_phandle() - Get the devfreq-event dev from
 *					 devicetree.
 * @dev		: the pointer to the given device
 * @index	: the index into list of devfreq-event device
 *
 * Note that this function return the pointer of devfreq-event device.
 */
struct devfreq_event_dev *devfreq_event_get_edev_by_phandle(struct device *dev,
						      int index)
{
	struct device_node *node;
	struct devfreq_event_dev *edev;

	if (!dev->of_node) {
		dev_err(dev, "device does not have a device node entry\n");
		return ERR_PTR(-EINVAL);
	}

	node = of_parse_phandle(dev->of_node, "devfreq-events", index);
	if (!node) {
		dev_err(dev, "failed to get phandle in %s node\n",
			dev->of_node->full_name);
		return ERR_PTR(-ENODEV);
	}

	mutex_lock(&devfreq_event_list_lock);
	list_for_each_entry(edev, &devfreq_event_list, node) {
		if (!strcmp(edev->desc->name, node->name))
			goto out;
	}
	edev = NULL;
out:
	mutex_unlock(&devfreq_event_list_lock);

	if (!edev) {
		dev_err(dev, "unable to get devfreq-event device : %s\n",
			node->name);
		of_node_put(node);
		return ERR_PTR(-ENODEV);
	}

	of_node_put(node);

	return edev;
}
EXPORT_SYMBOL_GPL(devfreq_event_get_edev_by_phandle);

/**
 * devfreq_event_get_edev_count() - Get the count of devfreq-event dev
 * @dev		: the pointer to the given device
 *
 * Note that this function return the count of devfreq-event devices.
 */
int devfreq_event_get_edev_count(struct device *dev)
{
	int count;

	if (!dev->of_node) {
		dev_err(dev, "device does not have a device node entry\n");
		return -EINVAL;
	}

	count = of_property_count_elems_of_size(dev->of_node, "devfreq-events",
						sizeof(u32));
	if (count < 0 ) {
		dev_err(dev,
			"failed to get the count of devfreq-event in %s node\n",
			dev->of_node->full_name);
		return count;
	}

	return count;
}
EXPORT_SYMBOL_GPL(devfreq_event_get_edev_count);

static void devfreq_event_release_edev(struct device *dev)
{
	struct devfreq_event_dev *edev = to_devfreq_event(dev);

	kfree(edev);
}

/**
 * devfreq_event_add_edev() - Add new devfreq-event device.
 * @dev		: the device owning the devfreq-event device being created
 * @desc	: the devfreq-event device's decriptor which include essential
 *		  data for devfreq-event device.
 *
 * Note that this function add new devfreq-event device to devfreq-event class
 * list and register the device of the devfreq-event device.
 */
struct devfreq_event_dev *devfreq_event_add_edev(struct device *dev,
						struct devfreq_event_desc *desc)
{
	struct devfreq_event_dev *edev;
	static atomic_t event_no = ATOMIC_INIT(0);
	int ret;

	if (!dev || !desc)
		return ERR_PTR(-EINVAL);

	if (!desc->name || !desc->ops)
		return ERR_PTR(-EINVAL);

	if (!desc->ops->set_event || !desc->ops->get_event)
		return ERR_PTR(-EINVAL);

	edev = kzalloc(sizeof(struct devfreq_event_dev), GFP_KERNEL);
	if (!edev)
		return ERR_PTR(-ENOMEM);

	mutex_init(&edev->lock);
	edev->desc = desc;
	edev->enable_count = 0;
	edev->dev.parent = dev;
	edev->dev.class = devfreq_event_class;
	edev->dev.release = devfreq_event_release_edev;

	dev_set_name(&edev->dev, "event.%d", atomic_inc_return(&event_no) - 1);
	ret = device_register(&edev->dev);
	if (ret < 0) {
		put_device(&edev->dev);
		return ERR_PTR(ret);
	}
	dev_set_drvdata(&edev->dev, edev);

	INIT_LIST_HEAD(&edev->node);

	mutex_lock(&devfreq_event_list_lock);
	list_add(&edev->node, &devfreq_event_list);
	mutex_unlock(&devfreq_event_list_lock);

	return edev;
}
EXPORT_SYMBOL_GPL(devfreq_event_add_edev);

/**
 * devfreq_event_remove_edev() - Remove the devfreq-event device registered.
 * @dev		: the devfreq-event device
 *
 * Note that this function remove the registered devfreq-event device.
 */
int devfreq_event_remove_edev(struct devfreq_event_dev *edev)
{
	if (!edev)
		return -EINVAL;

	WARN_ON(edev->enable_count);

	mutex_lock(&devfreq_event_list_lock);
	list_del(&edev->node);
	mutex_unlock(&devfreq_event_list_lock);

	device_unregister(&edev->dev);

	return 0;
}
EXPORT_SYMBOL_GPL(devfreq_event_remove_edev);

static int devm_devfreq_event_match(struct device *dev, void *res, void *data)
{
	struct devfreq_event_dev **r = res;

	if (WARN_ON(!r || !*r))
		return 0;

	return *r == data;
}

static void devm_devfreq_event_release(struct device *dev, void *res)
{
	devfreq_event_remove_edev(*(struct devfreq_event_dev **)res);
}

/**
 * devm_devfreq_event_add_edev() - Resource-managed devfreq_event_add_edev()
 * @dev		: the device owning the devfreq-event device being created
 * @desc	: the devfreq-event device's decriptor which include essential
 *		  data for devfreq-event device.
 *
 * Note that this function manages automatically the memory of devfreq-event
 * device using device resource management and simplify the free operation
 * for memory of devfreq-event device.
 */
struct devfreq_event_dev *devm_devfreq_event_add_edev(struct device *dev,
						struct devfreq_event_desc *desc)
{
	struct devfreq_event_dev **ptr, *edev;

	ptr = devres_alloc(devm_devfreq_event_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	edev = devfreq_event_add_edev(dev, desc);
	if (IS_ERR(edev)) {
		devres_free(ptr);
		return ERR_PTR(-ENOMEM);
	}

	*ptr = edev;
	devres_add(dev, ptr);

	return edev;
}
EXPORT_SYMBOL_GPL(devm_devfreq_event_add_edev);

/**
 * devm_devfreq_event_remove_edev()- Resource-managed devfreq_event_remove_edev()
 * @dev		: the device owning the devfreq-event device being created
 * @edev	: the devfreq-event device
 *
 * Note that this function manages automatically the memory of devfreq-event
 * device using device resource management.
 */
void devm_devfreq_event_remove_edev(struct device *dev,
				struct devfreq_event_dev *edev)
{
	WARN_ON(devres_release(dev, devm_devfreq_event_release,
			       devm_devfreq_event_match, edev));
}
EXPORT_SYMBOL_GPL(devm_devfreq_event_remove_edev);

/*
 * Device attributes for devfreq-event class.
 */
static ssize_t name_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct devfreq_event_dev *edev = to_devfreq_event(dev);

	if (!edev || !edev->desc)
		return -EINVAL;

	return sprintf(buf, "%s\n", edev->desc->name);
}
static DEVICE_ATTR_RO(name);

static ssize_t enable_count_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct devfreq_event_dev *edev = to_devfreq_event(dev);

	if (!edev || !edev->desc)
		return -EINVAL;

	return sprintf(buf, "%d\n", edev->enable_count);
}
static DEVICE_ATTR_RO(enable_count);

static struct attribute *devfreq_event_attrs[] = {
	&dev_attr_name.attr,
	&dev_attr_enable_count.attr,
	NULL,
};
ATTRIBUTE_GROUPS(devfreq_event);

static int __init devfreq_event_init(void)
{
	devfreq_event_class = class_create(THIS_MODULE, "devfreq-event");
	if (IS_ERR(devfreq_event_class)) {
		pr_err("%s: couldn't create class\n", __FILE__);
		return PTR_ERR(devfreq_event_class);
	}

	devfreq_event_class->dev_groups = devfreq_event_groups;

	return 0;
}
subsys_initcall(devfreq_event_init);

static void __exit devfreq_event_exit(void)
{
	class_destroy(devfreq_event_class);
}
module_exit(devfreq_event_exit);

MODULE_AUTHOR("Chanwoo Choi <cw00.choi@samsung.com>");
MODULE_DESCRIPTION("DEVFREQ-Event class support");
MODULE_LICENSE("GPL");
