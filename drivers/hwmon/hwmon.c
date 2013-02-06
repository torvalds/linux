/*
    hwmon.c - part of lm_sensors, Linux kernel modules for hardware monitoring

    This file defines the sysfs class "hwmon", for use by sensors drivers.

    Copyright (C) 2005 Mark M. Hoffman <mhoffman@lightlink.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; version 2 of the License.
*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kdev_t.h>
#include <linux/idr.h>
#include <linux/hwmon.h>
#include <linux/gfp.h>
#include <linux/spinlock.h>
#include <linux/pci.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#define HWMON_ID_PREFIX "hwmon"
#define HWMON_ID_FORMAT HWMON_ID_PREFIX "%d"

static struct class *hwmon_class;

static DEFINE_IDR(hwmon_idr);
static DEFINE_SPINLOCK(idr_lock);

struct hwmon_property {
	struct list_head node;
	const struct attribute *attr;
	struct hwmon_property_head *head;
};

struct hwmon_property_head {
	struct mutex lock;
	struct list_head head;
};

/**
 * hwmon_device_register - register w/ hwmon
 * @dev: the device to register
 *
 * hwmon_device_unregister() must be called when the device is no
 * longer needed.
 *
 * Returns the pointer to the new device.
 */
struct device *hwmon_device_register(struct device *dev)
{
	struct device *hwdev;
	int id, err;
	struct hwmon_property_head *data;

again:
	if (unlikely(idr_pre_get(&hwmon_idr, GFP_KERNEL) == 0))
		return ERR_PTR(-ENOMEM);

	spin_lock(&idr_lock);
	err = idr_get_new(&hwmon_idr, NULL, &id);
	spin_unlock(&idr_lock);

	if (unlikely(err == -EAGAIN))
		goto again;
	else if (unlikely(err))
		return ERR_PTR(err);

	id = id & MAX_ID_MASK;
	hwdev = device_create(hwmon_class, dev, MKDEV(0, 0), NULL,
			      HWMON_ID_FORMAT, id);

	if (IS_ERR(hwdev)) {
		spin_lock(&idr_lock);
		idr_remove(&hwmon_idr, id);
		spin_unlock(&idr_lock);
		goto out;
	}

	data = kzalloc(sizeof(struct hwmon_property_head), GFP_KERNEL);
	if (data == NULL)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&data->head);
	mutex_init(&data->lock);
	dev_set_drvdata(hwdev, data);
out:
	return hwdev;
}

static inline void _hwmon_free_prop(struct hwmon_property *prop)
{
	list_del(&prop->node);
	prop->attr = NULL;
	prop->head = NULL;
	kfree(prop);
}

/**
 * hwmon_unregister_all_properties - removes every property registered
 *
 * @hwmon: the class device to unregister sysfs properties.
 */
void hwmon_unregister_all_properties(struct device *hwmon)
{
	struct hwmon_property_head *data = dev_get_drvdata(hwmon);
	struct hwmon_property *pos, *tmp;

	mutex_lock(&data->lock);
	list_for_each_entry_safe(pos, tmp, &data->head, node) {
		_hwmon_free_prop(pos);
	}
	mutex_unlock(&data->lock);
}

/**
 * hwmon_device_unregister - removes the previously registered class device
 *
 * @dev: the class device to destroy
 */
void hwmon_device_unregister(struct device *dev)
{
	int id;
	struct hwmon_property_head *data = dev_get_drvdata(dev);

	hwmon_unregister_all_properties(dev);
	mutex_destroy(&data->lock);
	kfree(data);

	if (likely(sscanf(dev_name(dev), HWMON_ID_FORMAT, &id) == 1)) {
		device_unregister(dev);
		spin_lock(&idr_lock);
		idr_remove(&hwmon_idr, id);
		spin_unlock(&idr_lock);
	} else
		dev_dbg(dev->parent,
			"hwmon_device_unregister() failed: bad class ID!\n");
}

/**
 * hwmon_register_property - register one sysfs entry for hwmon framework
 *
 * @hwmon: the class device
 * @attr: a sysfs entry to be registered.
 */
struct hwmon_property *hwmon_register_property(struct device *hwmon,
					const struct device_attribute *attr)
{
	struct hwmon_property_head *data = dev_get_drvdata(hwmon);
	struct hwmon_property *entry;

	entry = kzalloc(sizeof(struct hwmon_property), GFP_KERNEL);
	if (!entry)
		return ERR_PTR(-ENOMEM);

	mutex_lock(&data->lock);
	entry->head = data;
	entry->attr = &attr->attr;
	list_add_tail(&entry->node, &data->head);
	mutex_unlock(&data->lock);

	return entry;
}

/**
 * hwmon_unregister_property - unregister the sysfs entry registered to hwmon
 *
 * @hwmon: the class device
 * @prop: hwmon_property entry to be unregistered
 *
 * Note that hwmon_device_unregister automatically unregister every property.
 */
int hwmon_unregister_property(struct device *hwmon,
			      struct hwmon_property *prop)
{
	struct hwmon_property_head *data = dev_get_drvdata(hwmon);
	struct hwmon_property *pos, *tmp;
	int err = -EINVAL;

	if (prop == NULL)
		return -EINVAL;
	if (!prop->attr || !prop->node.next || !prop->node.prev)
		return -EINVAL;

	mutex_lock(&data->lock);
	list_for_each_entry_safe(pos, tmp, &data->head, node) {
		if (prop == pos) {
			_hwmon_free_prop(pos);
			err = 0;
			break;
		}
	}
	mutex_unlock(&data->lock);

	return err;
}

/**
 * hwmon_register_properties - register a group of sysfs attributes
 *
 * @hwmon: hwmon class device to register sysfs entries.
 * @attrs: a sysfs attribute group to be registered.
 */
int hwmon_register_properties(struct device *hwmon,
			      const struct attribute_group *attrs)
{
	int i = 0, j, err = 0;
	struct attribute **_attrs;
	struct hwmon_property *prop;
	struct hwmon_property_head *data = dev_get_drvdata(hwmon);

	if (!attrs)
		return -EINVAL;
	_attrs = attrs->attrs;
	if (!_attrs)
		return -EINVAL;

	mutex_lock(&data->lock);

	while (_attrs[i]) {
		prop = kzalloc(sizeof(struct hwmon_property), GFP_KERNEL);
		if (!prop) {
			err = -ENOMEM;
			break;
		}
		prop->head = data;
		prop->attr = _attrs[i];
		list_add_tail(&prop->node, &data->head);
		i++;
	}
	if (err && i > 0) {
		struct hwmon_property *pos, *tmp;

		/* nodes are added to tail. remove from head */
		j = 0;
		list_for_each_entry_safe(pos, tmp, &data->head, node) {
			if (pos->attr == _attrs[j]) {
				_hwmon_free_prop(pos);

				j++;
				if (j >= i)
					break;
			}
		}
	}

	mutex_unlock(&data->lock);

	return err;
}

/**
 * hwmon_unregister_properties - unregister a group of attributes registered
 *
 * @hwmon: hwmon class device to register sysfs entries.
 * @attrs: a sysfs attribute group to be unregistered.
 *
 * Note that hwmon_device_unregister automatically unregister every property.
 */
int hwmon_unregister_properties(struct device *hwmon,
				const struct attribute_group *attrs)
{
	struct hwmon_property_head *data = dev_get_drvdata(hwmon);
	struct attribute **_attrs;
	struct hwmon_property *pos, *tmp;
	int i = 0;

	if (!attrs)
		return -EINVAL;
	_attrs = attrs->attrs;
	if (!_attrs)
		return -EINVAL;

	mutex_lock(&data->lock);

	/*
	 * Assuming that hwmon_register_properties was used, try to
	 * remove in the inserted order first.
	 */
	list_for_each_entry_safe(pos, tmp, &data->head, node) {
		if (_attrs[i] == NULL)
			break;
		if (pos->attr == _attrs[i]) {
			_hwmon_free_prop(pos);
			i++;
		}
	}

	/* If it wasn't inserted in the order of attrs */
	while (_attrs[i]) {
		list_for_each_entry_safe(pos, tmp, &data->head, node) {
			if (pos->attr == _attrs[i]) {
				_hwmon_free_prop(pos);
				break;
			}
		}
		i++;
	}

	mutex_unlock(&data->lock);
	return 0;
}

/**
 * hwmon_get_property - get hwmon property based on an LMSENSOR sysfs name.
 *
 * @hwmon - hwmon class device
 * @name - LMSENSOR sysfs name (e.g., "temp1_input")
 */
struct hwmon_property *hwmon_get_property(struct device *hwmon,
					  const char *name)
{
	struct hwmon_property_head *data = dev_get_drvdata(hwmon);
	struct hwmon_property *pos;

	mutex_lock(&data->lock);
	list_for_each_entry(pos, &data->head, node) {
		if (!strcmp(name, pos->attr->name))
			goto out;
	}
	pos = ERR_PTR(-EINVAL);
out:
	mutex_unlock(&data->lock);
	return pos;
}

/**
 * hwmon_get_value - get the sysfs entry value of the hwmon device.
 *
 * @hwmon - hwmon class device
 * @prop - hwmon property (use hwmon_get_property() to get one)
 * @value - integer value from prop.
 */
int hwmon_get_value(struct device *hwmon, struct hwmon_property *prop,
		    int *value)
{
	int err = -EINVAL;
	struct device_attribute *devattr;
	char buf[13]; /* 32b int max string length = 12 */

	if (!prop || !prop->attr || !prop->head)
		return -EINVAL;

	mutex_lock(&prop->head->lock);

	devattr = container_of(prop->attr, struct device_attribute, attr);

	if (!devattr->show)
		goto out;

	err = devattr->show(hwmon->parent, devattr, buf);
	if (strnlen(buf, 13) >= 13) {
		err = -EINVAL;
		goto out;
	}

	err = sscanf(buf, "%d", value);
	if (err >= 0)
		err = 0;
out:
	mutex_unlock(&prop->head->lock);
	return err;
}
EXPORT_SYMBOL_GPL(hwmon_get_value);

/**
 * hwmon_set_value - set the sysfs entry value of the hwmon device.
 *
 * @hwmon - hwmon class device
 * @prop - hwmon property (use hwmon_get_property() to get one)
 * @value - integer value to set prop
 */
int hwmon_set_value(struct device *hwmon, struct hwmon_property *prop,
		    int value)
{
	int err = -EINVAL, count;
	struct device_attribute *devattr;
	char buf[13]; /* 32b int max string length = 12 */

	if (!prop || !prop->attr || !prop->head)
		return -EINVAL;

	mutex_lock(&prop->head->lock);

	devattr = container_of(prop->attr, struct device_attribute, attr);

	if (!devattr->store)
		goto out;

	count = snprintf(buf, 13, "%d\n", value);

	err = devattr->store(hwmon->parent, devattr, buf, count);
out:
	mutex_unlock(&prop->head->lock);
	return err;
}
EXPORT_SYMBOL_GPL(hwmon_set_value);

static int hwmon_dev_match(struct device *dev, void *data)
{
	if (dev->class == hwmon_class)
		return 1;
	return 0;
}

/**
 * hwmon_find_device - find the hwmon device of the given device
 *
 * @dev - a parent device of a hwmon class device
 */
struct device *hwmon_find_device(struct device *dev)
{
	return device_find_child(dev, NULL, hwmon_dev_match);
}

static int hwmon_parent_name_match(struct device *dev, void *data)
{
	char *devname = data;

	if (!strcmp(dev_name(dev->parent), devname))
		return 1;
	return 0;
}

/**
 * hwmon_find_device_name - find the hwmon device with device name
 *
 * @name - device name of the parent device of a hwmon class device
 */
struct device *hwmon_find_device_name(char *devname)
{
	return class_find_device(hwmon_class, NULL, devname,
				 hwmon_parent_name_match);
}

static void __init hwmon_pci_quirks(void)
{
#if defined CONFIG_X86 && defined CONFIG_PCI
	struct pci_dev *sb;
	u16 base;
	u8 enable;

	/* Open access to 0x295-0x296 on MSI MS-7031 */
	sb = pci_get_device(PCI_VENDOR_ID_ATI, 0x436c, NULL);
	if (sb &&
	    (sb->subsystem_vendor == 0x1462 &&	/* MSI */
	     sb->subsystem_device == 0x0031)) {	/* MS-7031 */

		pci_read_config_byte(sb, 0x48, &enable);
		pci_read_config_word(sb, 0x64, &base);

		if (base == 0 && !(enable & BIT(2))) {
			dev_info(&sb->dev,
				 "Opening wide generic port at 0x295\n");
			pci_write_config_word(sb, 0x64, 0x295);
			pci_write_config_byte(sb, 0x48, enable | BIT(2));
		}
	}
#endif
}

static int __init hwmon_init(void)
{
	hwmon_pci_quirks();

	hwmon_class = class_create(THIS_MODULE, "hwmon");
	if (IS_ERR(hwmon_class)) {
		pr_err("couldn't create sysfs class\n");
		return PTR_ERR(hwmon_class);
	}
	return 0;
}

static void __exit hwmon_exit(void)
{
	class_destroy(hwmon_class);
}

subsys_initcall(hwmon_init);
module_exit(hwmon_exit);

EXPORT_SYMBOL_GPL(hwmon_device_register);
EXPORT_SYMBOL_GPL(hwmon_device_unregister);

MODULE_AUTHOR("Mark M. Hoffman <mhoffman@lightlink.com>");
MODULE_DESCRIPTION("hardware monitoring sysfs/class support");
MODULE_LICENSE("GPL");

