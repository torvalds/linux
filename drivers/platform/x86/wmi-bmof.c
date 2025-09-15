// SPDX-License-Identifier: GPL-2.0-only
/*
 * WMI embedded Binary MOF driver
 *
 * Copyright (c) 2015 Andrew Lutomirski
 * Copyright (C) 2017 VMware, Inc. All Rights Reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/wmi.h>

#define WMI_BMOF_GUID "05901221-D566-11D1-B2F0-00A0C9062910"

static ssize_t bmof_read(struct file *filp, struct kobject *kobj, const struct bin_attribute *attr,
			 char *buf, loff_t off, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	union acpi_object *obj = dev_get_drvdata(dev);

	return memory_read_from_buffer(buf, count, &off, obj->buffer.pointer, obj->buffer.length);
}

static const BIN_ATTR_ADMIN_RO(bmof, 0);

static const struct bin_attribute * const bmof_attrs[] = {
	&bin_attr_bmof,
	NULL
};

static size_t bmof_bin_size(struct kobject *kobj, const struct bin_attribute *attr, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	union acpi_object *obj = dev_get_drvdata(dev);

	return obj->buffer.length;
}

static const struct attribute_group bmof_group = {
	.bin_size = bmof_bin_size,
	.bin_attrs = bmof_attrs,
};

static const struct attribute_group *bmof_groups[] = {
	&bmof_group,
	NULL
};

static int wmi_bmof_probe(struct wmi_device *wdev, const void *context)
{
	union acpi_object *obj;

	obj = wmidev_block_query(wdev, 0);
	if (!obj) {
		dev_err(&wdev->dev, "failed to read Binary MOF\n");
		return -EIO;
	}

	if (obj->type != ACPI_TYPE_BUFFER) {
		dev_err(&wdev->dev, "Binary MOF is not a buffer\n");
		kfree(obj);
		return -EIO;
	}

	dev_set_drvdata(&wdev->dev, obj);

	return 0;
}

static void wmi_bmof_remove(struct wmi_device *wdev)
{
	union acpi_object *obj = dev_get_drvdata(&wdev->dev);

	kfree(obj);
}

static const struct wmi_device_id wmi_bmof_id_table[] = {
	{ .guid_string = WMI_BMOF_GUID },
	{ },
};

static struct wmi_driver wmi_bmof_driver = {
	.driver = {
		.name = "wmi-bmof",
		.dev_groups = bmof_groups,
	},
	.probe = wmi_bmof_probe,
	.remove = wmi_bmof_remove,
	.id_table = wmi_bmof_id_table,
	.no_singleton = true,
};

module_wmi_driver(wmi_bmof_driver);

MODULE_DEVICE_TABLE(wmi, wmi_bmof_id_table);
MODULE_AUTHOR("Andrew Lutomirski <luto@kernel.org>");
MODULE_DESCRIPTION("WMI embedded Binary MOF driver");
MODULE_LICENSE("GPL");
