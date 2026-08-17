// SPDX-License-Identifier: GPL-2.0-only
/*
 * Dell WMI descriptor driver
 *
 * Copyright (C) 2017 Dell Inc. All Rights Reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cleanup.h>
#include <linux/compiler_attributes.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/wmi.h>
#include "dell-wmi-descriptor.h"

#define DELL_WMI_DESCRIPTOR_GUID "8D9DDCBC-A997-11DA-B012-B622A1EF5492"

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
struct descriptor {
	/* Both fields are NOT null-terminated */
	char vendor_signature[4] __nonstring;
	char object_signature[4] __nonstring;
	__le32 interface_version;
	__le32 buffer_length;
	__le32 hotfix_number;
} __packed;

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

static int dell_wmi_descriptor_probe(struct wmi_device *wdev, const void *context)
{
	struct descriptor_priv *priv;
	struct wmi_buffer buffer;
	int ret;

	ret = wmidev_query_block(wdev, 0, &buffer, sizeof(struct descriptor));
	if (ret < 0) {
		descriptor_valid = ret;
		return ret;
	}

	struct descriptor *desc __free(kfree) = buffer.data;

	if (strncmp(desc->vendor_signature, "DELL", sizeof(desc->vendor_signature))) {
		dev_err(&wdev->dev, "Dell descriptor buffer has invalid vendor signature (%4ph)\n",
			desc->vendor_signature);
		descriptor_valid = -ENOMSG;
		return -ENOMSG;
	}

	if (strncmp(desc->object_signature, " WMI", sizeof(desc->object_signature))) {
		dev_err(&wdev->dev, "Dell descriptor buffer has invalid object signature (%4ph)\n",
			desc->object_signature);
		descriptor_valid = -ENOMSG;
		return -ENOMSG;
	}
	descriptor_valid = 0;

	if (le32_to_cpu(desc->interface_version) > 1)
		dev_warn(&wdev->dev, "Dell descriptor buffer has unknown version (%u)\n",
			 le32_to_cpu(desc->interface_version));

	priv = devm_kzalloc(&wdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->interface_version = le32_to_cpu(desc->interface_version);
	priv->size = le32_to_cpu(desc->buffer_length);
	priv->hotfix = le32_to_cpu(desc->hotfix_number);
	dev_set_drvdata(&wdev->dev, priv);
	mutex_lock(&list_mutex);
	list_add_tail(&priv->list, &wmi_list);
	mutex_unlock(&list_mutex);

	dev_dbg(&wdev->dev, "Detected Dell WMI interface version %lu, buffer size %lu, hotfix %lu\n",
		(unsigned long) priv->interface_version,
		(unsigned long) priv->size,
		(unsigned long) priv->hotfix);

	return ret;
}

static void dell_wmi_descriptor_remove(struct wmi_device *wdev)
{
	struct descriptor_priv *priv = dev_get_drvdata(&wdev->dev);

	mutex_lock(&list_mutex);
	list_del(&priv->list);
	mutex_unlock(&list_mutex);
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
MODULE_AUTHOR("Mario Limonciello <mario.limonciello@outlook.com>");
MODULE_DESCRIPTION("Dell WMI descriptor driver");
MODULE_LICENSE("GPL");
