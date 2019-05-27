/*
 * Dell WMI descriptor driver
 *
 * Copyright (C) 2017 Dell Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/wmi.h>
#include "dell-wmi-descriptor.h"

#define DELL_WMI_DESCRIPTOR_GUID "8D9DDCBC-A997-11DA-B012-B622A1EF5492"

struct descriptor_priv {
	struct list_head list;
	u32 interface_version;
	u32 size;
	u32 hotfix;
};
static int descriptor_valid = -EPROBE_DEFER;
static LIST_HEAD(wmi_list);
static DEFINE_MUTEX(list_mutex);

int dell_wmi_get_descriptor_valid(void)
{
	if (!wmi_has_guid(DELL_WMI_DESCRIPTOR_GUID))
		return -ENODEV;

	return descriptor_valid;
}
EXPORT_SYMBOL_GPL(dell_wmi_get_descriptor_valid);

bool dell_wmi_get_interface_version(u32 *version)
{
	struct descriptor_priv *priv;
	bool ret = false;

	mutex_lock(&list_mutex);
	priv = list_first_entry_or_null(&wmi_list,
					struct descriptor_priv,
					list);
	if (priv) {
		*version = priv->interface_version;
		ret = true;
	}
	mutex_unlock(&list_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(dell_wmi_get_interface_version);

bool dell_wmi_get_size(u32 *size)
{
	struct descriptor_priv *priv;
	bool ret = false;

	mutex_lock(&list_mutex);
	priv = list_first_entry_or_null(&wmi_list,
					struct descriptor_priv,
					list);
	if (priv) {
		*size = priv->size;
		ret = true;
	}
	mutex_unlock(&list_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(dell_wmi_get_size);

bool dell_wmi_get_hotfix(u32 *hotfix)
{
	struct descriptor_priv *priv;
	bool ret = false;

	mutex_lock(&list_mutex);
	priv = list_first_entry_or_null(&wmi_list,
					struct descriptor_priv,
					list);
	if (priv) {
		*hotfix = priv->hotfix;
		ret = true;
	}
	mutex_unlock(&list_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(dell_wmi_get_hotfix);

/*
 * Descriptor buffer is 128 byte long and contains:
 *
 *       Name             Offset  Length  Value
 * Vendor Signature          0       4    "DELL"
 * Object Signature          4       4    " WMI"
 * WMI Interface Version     8       4    <version>
 * WMI buffer length        12       4    <length>
 * WMI hotfix number        16       4    <hotfix>
 */
static int dell_wmi_descriptor_probe(struct wmi_device *wdev,
				     const void *context)
{
	union acpi_object *obj = NULL;
	struct descriptor_priv *priv;
	u32 *buffer;
	int ret;

	obj = wmidev_block_query(wdev, 0);
	if (!obj) {
		dev_err(&wdev->dev, "failed to read Dell WMI descriptor\n");
		ret = -EIO;
		goto out;
	}

	if (obj->type != ACPI_TYPE_BUFFER) {
		dev_err(&wdev->dev, "Dell descriptor has wrong type\n");
		ret = -EINVAL;
		descriptor_valid = ret;
		goto out;
	}

	/* Although it's not technically a failure, this would lead to
	 * unexpected behavior
	 */
	if (obj->buffer.length != 128) {
		dev_err(&wdev->dev,
			"Dell descriptor buffer has unexpected length (%d)\n",
			obj->buffer.length);
		ret = -EINVAL;
		descriptor_valid = ret;
		goto out;
	}

	buffer = (u32 *)obj->buffer.pointer;

	if (strncmp(obj->string.pointer, "DELL WMI", 8) != 0) {
		dev_err(&wdev->dev, "Dell descriptor buffer has invalid signature (%8ph)\n",
			buffer);
		ret = -EINVAL;
		descriptor_valid = ret;
		goto out;
	}
	descriptor_valid = 0;

	if (buffer[2] != 0 && buffer[2] != 1)
		dev_warn(&wdev->dev, "Dell descriptor buffer has unknown version (%lu)\n",
			(unsigned long) buffer[2]);

	priv = devm_kzalloc(&wdev->dev, sizeof(struct descriptor_priv),
	GFP_KERNEL);

	if (!priv) {
		ret = -ENOMEM;
		goto out;
	}

	priv->interface_version = buffer[2];
	priv->size = buffer[3];
	priv->hotfix = buffer[4];
	ret = 0;
	dev_set_drvdata(&wdev->dev, priv);
	mutex_lock(&list_mutex);
	list_add_tail(&priv->list, &wmi_list);
	mutex_unlock(&list_mutex);

	dev_dbg(&wdev->dev, "Detected Dell WMI interface version %lu, buffer size %lu, hotfix %lu\n",
		(unsigned long) priv->interface_version,
		(unsigned long) priv->size,
		(unsigned long) priv->hotfix);

out:
	kfree(obj);
	return ret;
}

static int dell_wmi_descriptor_remove(struct wmi_device *wdev)
{
	struct descriptor_priv *priv = dev_get_drvdata(&wdev->dev);

	mutex_lock(&list_mutex);
	list_del(&priv->list);
	mutex_unlock(&list_mutex);
	return 0;
}

static const struct wmi_device_id dell_wmi_descriptor_id_table[] = {
	{ .guid_string = DELL_WMI_DESCRIPTOR_GUID },
	{ },
};

static struct wmi_driver dell_wmi_descriptor_driver = {
	.driver = {
		.name = "dell-wmi-descriptor",
	},
	.probe = dell_wmi_descriptor_probe,
	.remove = dell_wmi_descriptor_remove,
	.id_table = dell_wmi_descriptor_id_table,
};

module_wmi_driver(dell_wmi_descriptor_driver);

MODULE_DEVICE_TABLE(wmi, dell_wmi_descriptor_id_table);
MODULE_AUTHOR("Mario Limonciello <mario.limonciello@dell.com>");
MODULE_DESCRIPTION("Dell WMI descriptor driver");
MODULE_LICENSE("GPL");
