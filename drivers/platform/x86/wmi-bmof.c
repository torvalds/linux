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

struct bmof_priv {
	union acpi_object *bmofdata;
	struct bin_attribute bmof_bin_attr;
};

static ssize_t
read_bmof(struct file *filp, struct kobject *kobj,
	 struct bin_attribute *attr,
	 char *buf, loff_t off, size_t count)
{
	struct bmof_priv *priv =
		container_of(attr, struct bmof_priv, bmof_bin_attr);

	if (off < 0)
		return -EINVAL;

	if (off >= priv->bmofdata->buffer.length)
		return 0;

	if (count > priv->bmofdata->buffer.length - off)
		count = priv->bmofdata->buffer.length - off;

	memcpy(buf, priv->bmofdata->buffer.pointer + off, count);
	return count;
}

static int wmi_bmof_probe(struct wmi_device *wdev)
{
	struct bmof_priv *priv;
	int ret;

	priv = devm_kzalloc(&wdev->dev, sizeof(struct bmof_priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(&wdev->dev, priv);

	priv->bmofdata = wmidev_block_query(wdev, 0);
	if (!priv->bmofdata) {
		dev_err(&wdev->dev, "failed to read Binary MOF\n");
		return -EIO;
	}

	if (priv->bmofdata->type != ACPI_TYPE_BUFFER) {
		dev_err(&wdev->dev, "Binary MOF is not a buffer\n");
		ret = -EIO;
		goto err_free;
	}

	sysfs_bin_attr_init(&priv->bmof_bin_attr);
	priv->bmof_bin_attr.attr.name = "bmof";
	priv->bmof_bin_attr.attr.mode = 0400;
	priv->bmof_bin_attr.read = read_bmof;
	priv->bmof_bin_attr.size = priv->bmofdata->buffer.length;

	ret = sysfs_create_bin_file(&wdev->dev.kobj, &priv->bmof_bin_attr);
	if (ret)
		goto err_free;

	return 0;

 err_free:
	kfree(priv->bmofdata);
	return ret;
}

static int wmi_bmof_remove(struct wmi_device *wdev)
{
	struct bmof_priv *priv = dev_get_drvdata(&wdev->dev);

	sysfs_remove_bin_file(&wdev->dev.kobj, &priv->bmof_bin_attr);
	kfree(priv->bmofdata);
	return 0;
}

static const struct wmi_device_id wmi_bmof_id_table[] = {
	{ .guid_string = WMI_BMOF_GUID },
	{ },
};

static struct wmi_driver wmi_bmof_driver = {
	.driver = {
		.name = "wmi-bmof",
	},
	.probe = wmi_bmof_probe,
	.remove = wmi_bmof_remove,
	.id_table = wmi_bmof_id_table,
};

module_wmi_driver(wmi_bmof_driver);

MODULE_DEVICE_TABLE(wmi, wmi_bmof_id_table);
MODULE_AUTHOR("Andrew Lutomirski <luto@kernel.org>");
MODULE_DESCRIPTION("WMI embedded Binary MOF driver");
MODULE_LICENSE("GPL");
