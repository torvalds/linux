/*
 * hwmon.c - part of lm_sensors, Linux kernel modules for hardware monitoring
 *
 * This file defines the sysfs class "hwmon", for use by sensors drivers.
 *
 * Copyright (C) 2005 Mark M. Hoffman <mhoffman@lightlink.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/kdev_t.h>
#include <linux/idr.h>
#include <linux/hwmon.h>
#include <linux/gfp.h>
#include <linux/spinlock.h>
#include <linux/pci.h>

#define HWMON_ID_PREFIX "hwmon"
#define HWMON_ID_FORMAT HWMON_ID_PREFIX "%d"

struct hwmon_device {
	const char *name;
	struct device dev;
};
#define to_hwmon_device(d) container_of(d, struct hwmon_device, dev)

static ssize_t
show_name(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", to_hwmon_device(dev)->name);
}
static DEVICE_ATTR(name, S_IRUGO, show_name, NULL);

static struct attribute *hwmon_dev_attrs[] = {
	&dev_attr_name.attr,
	NULL
};

static umode_t hwmon_dev_name_is_visible(struct kobject *kobj,
					 struct attribute *attr, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);

	if (to_hwmon_device(dev)->name == NULL)
		return 0;

	return attr->mode;
}

static struct attribute_group hwmon_dev_attr_group = {
	.attrs		= hwmon_dev_attrs,
	.is_visible	= hwmon_dev_name_is_visible,
};

static const struct attribute_group *hwmon_dev_attr_groups[] = {
	&hwmon_dev_attr_group,
	NULL
};

static void hwmon_dev_release(struct device *dev)
{
	kfree(to_hwmon_device(dev));
}

static struct class hwmon_class = {
	.name = "hwmon",
	.owner = THIS_MODULE,
	.dev_groups = hwmon_dev_attr_groups,
	.dev_release = hwmon_dev_release,
};

static DEFINE_IDA(hwmon_ida);

/**
 * hwmon_device_register_with_groups - register w/ hwmon
 * @dev: the parent device
 * @name: hwmon name attribute
 * @drvdata: driver data to attach to created device
 * @groups: List of attribute groups to create
 *
 * hwmon_device_unregister() must be called when the device is no
 * longer needed.
 *
 * Returns the pointer to the new device.
 */
struct device *
hwmon_device_register_with_groups(struct device *dev, const char *name,
				  void *drvdata,
				  const struct attribute_group **groups)
{
	struct hwmon_device *hwdev;
	int err, id;

	id = ida_simple_get(&hwmon_ida, 0, 0, GFP_KERNEL);
	if (id < 0)
		return ERR_PTR(id);

	hwdev = kzalloc(sizeof(*hwdev), GFP_KERNEL);
	if (hwdev == NULL) {
		err = -ENOMEM;
		goto ida_remove;
	}

	hwdev->name = name;
	hwdev->dev.class = &hwmon_class;
	hwdev->dev.parent = dev;
	hwdev->dev.groups = groups;
	hwdev->dev.of_node = dev ? dev->of_node : NULL;
	dev_set_drvdata(&hwdev->dev, drvdata);
	dev_set_name(&hwdev->dev, HWMON_ID_FORMAT, id);
	err = device_register(&hwdev->dev);
	if (err)
		goto free;

	return &hwdev->dev;

free:
	kfree(hwdev);
ida_remove:
	ida_simple_remove(&hwmon_ida, id);
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(hwmon_device_register_with_groups);

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
	return hwmon_device_register_with_groups(dev, NULL, NULL, NULL);
}
EXPORT_SYMBOL_GPL(hwmon_device_register);

/**
 * hwmon_device_unregister - removes the previously registered class device
 *
 * @dev: the class device to destroy
 */
void hwmon_device_unregister(struct device *dev)
{
	int id;

	if (likely(sscanf(dev_name(dev), HWMON_ID_FORMAT, &id) == 1)) {
		device_unregister(dev);
		ida_simple_remove(&hwmon_ida, id);
	} else
		dev_dbg(dev->parent,
			"hwmon_device_unregister() failed: bad class ID!\n");
}
EXPORT_SYMBOL_GPL(hwmon_device_unregister);

static void devm_hwmon_release(struct device *dev, void *res)
{
	struct device *hwdev = *(struct device **)res;

	hwmon_device_unregister(hwdev);
}

/**
 * devm_hwmon_device_register_with_groups - register w/ hwmon
 * @dev: the parent device
 * @name: hwmon name attribute
 * @drvdata: driver data to attach to created device
 * @groups: List of attribute groups to create
 *
 * Returns the pointer to the new device. The new device is automatically
 * unregistered with the parent device.
 */
struct device *
devm_hwmon_device_register_with_groups(struct device *dev, const char *name,
				       void *drvdata,
				       const struct attribute_group **groups)
{
	struct device **ptr, *hwdev;

	if (!dev)
		return ERR_PTR(-EINVAL);

	ptr = devres_alloc(devm_hwmon_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	hwdev = hwmon_device_register_with_groups(dev, name, drvdata, groups);
	if (IS_ERR(hwdev))
		goto error;

	*ptr = hwdev;
	devres_add(dev, ptr);
	return hwdev;

error:
	devres_free(ptr);
	return hwdev;
}
EXPORT_SYMBOL_GPL(devm_hwmon_device_register_with_groups);

static int devm_hwmon_match(struct device *dev, void *res, void *data)
{
	struct device **hwdev = res;

	return *hwdev == data;
}

/**
 * devm_hwmon_device_unregister - removes a previously registered hwmon device
 *
 * @dev: the parent device of the device to unregister
 */
void devm_hwmon_device_unregister(struct device *dev)
{
	WARN_ON(devres_release(dev, devm_hwmon_release, devm_hwmon_match, dev));
}
EXPORT_SYMBOL_GPL(devm_hwmon_device_unregister);

static void __init hwmon_pci_quirks(void)
{
#if defined CONFIG_X86 && defined CONFIG_PCI
	struct pci_dev *sb;
	u16 base;
	u8 enable;

	/* Open access to 0x295-0x296 on MSI MS-7031 */
	sb = pci_get_device(PCI_VENDOR_ID_ATI, 0x436c, NULL);
	if (sb) {
		if (sb->subsystem_vendor == 0x1462 &&	/* MSI */
		    sb->subsystem_device == 0x0031) {	/* MS-7031 */
			pci_read_config_byte(sb, 0x48, &enable);
			pci_read_config_word(sb, 0x64, &base);

			if (base == 0 && !(enable & BIT(2))) {
				dev_info(&sb->dev,
					 "Opening wide generic port at 0x295\n");
				pci_write_config_word(sb, 0x64, 0x295);
				pci_write_config_byte(sb, 0x48,
						      enable | BIT(2));
			}
		}
		pci_dev_put(sb);
	}
#endif
}

static int __init hwmon_init(void)
{
	int err;

	hwmon_pci_quirks();

	err = class_register(&hwmon_class);
	if (err) {
		pr_err("couldn't register hwmon sysfs class\n");
		return err;
	}
	return 0;
}

static void __exit hwmon_exit(void)
{
	class_unregister(&hwmon_class);
}

subsys_initcall(hwmon_init);
module_exit(hwmon_exit);

MODULE_AUTHOR("Mark M. Hoffman <mhoffman@lightlink.com>");
MODULE_DESCRIPTION("hardware monitoring sysfs/class support");
MODULE_LICENSE("GPL");

