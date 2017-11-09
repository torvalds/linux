/*
 *  WMI methods for use with dell-smbios
 *
 *  Copyright (c) 2017 Dell Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/dmi.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/wmi.h>
#include "dell-smbios.h"
#include "dell-wmi-descriptor.h"

static DEFINE_MUTEX(call_mutex);
static DEFINE_MUTEX(list_mutex);
static int wmi_supported;

struct misc_bios_flags_structure {
	struct dmi_header header;
	u16 flags0;
} __packed;
#define FLAG_HAS_ACPI_WMI 0x02

#define DELL_WMI_SMBIOS_GUID "A80593CE-A997-11DA-B012-B622A1EF5492"

struct wmi_smbios_priv {
	struct dell_wmi_smbios_buffer *buf;
	struct list_head list;
	struct wmi_device *wdev;
	struct device *child;
	u32 req_buf_size;
};
static LIST_HEAD(wmi_list);

static inline struct wmi_smbios_priv *get_first_smbios_priv(void)
{
	return list_first_entry_or_null(&wmi_list,
					struct wmi_smbios_priv,
					list);
}

static int run_smbios_call(struct wmi_device *wdev)
{
	struct acpi_buffer output = {ACPI_ALLOCATE_BUFFER, NULL};
	struct wmi_smbios_priv *priv;
	struct acpi_buffer input;
	union acpi_object *obj;
	acpi_status status;

	priv = dev_get_drvdata(&wdev->dev);
	input.length = priv->req_buf_size - sizeof(u64);
	input.pointer = &priv->buf->std;

	dev_dbg(&wdev->dev, "evaluating: %u/%u [%x,%x,%x,%x]\n",
		priv->buf->std.cmd_class, priv->buf->std.cmd_select,
		priv->buf->std.input[0], priv->buf->std.input[1],
		priv->buf->std.input[2], priv->buf->std.input[3]);

	status = wmidev_evaluate_method(wdev, 0, 1, &input, &output);
	if (ACPI_FAILURE(status))
		return -EIO;
	obj = (union acpi_object *)output.pointer;
	if (obj->type != ACPI_TYPE_BUFFER) {
		dev_dbg(&wdev->dev, "received type: %d\n", obj->type);
		if (obj->type == ACPI_TYPE_INTEGER)
			dev_dbg(&wdev->dev, "SMBIOS call failed: %llu\n",
				obj->integer.value);
		return -EIO;
	}
	memcpy(&priv->buf->std, obj->buffer.pointer, obj->buffer.length);
	dev_dbg(&wdev->dev, "result: [%08x,%08x,%08x,%08x]\n",
		priv->buf->std.output[0], priv->buf->std.output[1],
		priv->buf->std.output[2], priv->buf->std.output[3]);

	return 0;
}

int dell_smbios_wmi_call(struct calling_interface_buffer *buffer)
{
	struct wmi_smbios_priv *priv;
	size_t difference;
	size_t size;
	int ret;

	mutex_lock(&call_mutex);
	priv = get_first_smbios_priv();
	if (!priv) {
		ret = -ENODEV;
		goto out_wmi_call;
	}

	size = sizeof(struct calling_interface_buffer);
	difference = priv->req_buf_size - sizeof(u64) - size;

	memset(&priv->buf->ext, 0, difference);
	memcpy(&priv->buf->std, buffer, size);
	ret = run_smbios_call(priv->wdev);
	memcpy(buffer, &priv->buf->std, size);
out_wmi_call:
	mutex_unlock(&call_mutex);

	return ret;
}

static long dell_smbios_wmi_filter(struct wmi_device *wdev, unsigned int cmd,
				   struct wmi_ioctl_buffer *arg)
{
	struct wmi_smbios_priv *priv;
	int ret = 0;

	switch (cmd) {
	case DELL_WMI_SMBIOS_CMD:
		mutex_lock(&call_mutex);
		priv = dev_get_drvdata(&wdev->dev);
		if (!priv) {
			ret = -ENODEV;
			goto fail_smbios_cmd;
		}
		memcpy(priv->buf, arg, priv->req_buf_size);
		if (dell_smbios_call_filter(&wdev->dev, &priv->buf->std)) {
			dev_err(&wdev->dev, "Invalid call %d/%d:%8x\n",
				priv->buf->std.cmd_class,
				priv->buf->std.cmd_select,
				priv->buf->std.input[0]);
			ret = -EFAULT;
			goto fail_smbios_cmd;
		}
		ret = run_smbios_call(priv->wdev);
		if (ret)
			goto fail_smbios_cmd;
		memcpy(arg, priv->buf, priv->req_buf_size);
fail_smbios_cmd:
		mutex_unlock(&call_mutex);
		break;
	default:
		ret = -ENOIOCTLCMD;
	}
	return ret;
}

static int dell_smbios_wmi_probe(struct wmi_device *wdev)
{
	struct wmi_smbios_priv *priv;
	int count;
	int ret;

	ret = dell_wmi_get_descriptor_valid();
	if (ret)
		return ret;

	priv = devm_kzalloc(&wdev->dev, sizeof(struct wmi_smbios_priv),
			    GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* WMI buffer size will be either 4k or 32k depending on machine */
	if (!dell_wmi_get_size(&priv->req_buf_size))
		return -EPROBE_DEFER;

	/* add in the length object we will use internally with ioctl */
	priv->req_buf_size += sizeof(u64);
	ret = set_required_buffer_size(wdev, priv->req_buf_size);
	if (ret)
		return ret;

	count = get_order(priv->req_buf_size);
	priv->buf = (void *)__get_free_pages(GFP_KERNEL, count);
	if (!priv->buf)
		return -ENOMEM;

	/* ID is used by dell-smbios to set priority of drivers */
	wdev->dev.id = 1;
	ret = dell_smbios_register_device(&wdev->dev, &dell_smbios_wmi_call);
	if (ret)
		goto fail_register;

	priv->wdev = wdev;
	dev_set_drvdata(&wdev->dev, priv);
	mutex_lock(&list_mutex);
	list_add_tail(&priv->list, &wmi_list);
	mutex_unlock(&list_mutex);

	return 0;

fail_register:
	free_pages((unsigned long)priv->buf, count);
	return ret;
}

static int dell_smbios_wmi_remove(struct wmi_device *wdev)
{
	struct wmi_smbios_priv *priv = dev_get_drvdata(&wdev->dev);
	int count;

	mutex_lock(&call_mutex);
	mutex_lock(&list_mutex);
	list_del(&priv->list);
	mutex_unlock(&list_mutex);
	dell_smbios_unregister_device(&wdev->dev);
	count = get_order(priv->req_buf_size);
	free_pages((unsigned long)priv->buf, count);
	mutex_unlock(&call_mutex);
	return 0;
}

static const struct wmi_device_id dell_smbios_wmi_id_table[] = {
	{ .guid_string = DELL_WMI_SMBIOS_GUID },
	{ },
};

static void __init parse_b1_table(const struct dmi_header *dm)
{
	struct misc_bios_flags_structure *flags =
	container_of(dm, struct misc_bios_flags_structure, header);

	/* 4 bytes header, 8 bytes flags */
	if (dm->length < 12)
		return;
	if (dm->handle != 0xb100)
		return;
	if ((flags->flags0 & FLAG_HAS_ACPI_WMI))
		wmi_supported = 1;
}

static void __init find_b1(const struct dmi_header *dm, void *dummy)
{
	switch (dm->type) {
	case 0xb1: /* misc bios flags */
		parse_b1_table(dm);
		break;
	}
}

static struct wmi_driver dell_smbios_wmi_driver = {
	.driver = {
		.name = "dell-smbios",
	},
	.probe = dell_smbios_wmi_probe,
	.remove = dell_smbios_wmi_remove,
	.id_table = dell_smbios_wmi_id_table,
	.filter_callback = dell_smbios_wmi_filter,
};

static int __init init_dell_smbios_wmi(void)
{
	dmi_walk(find_b1, NULL);

	if (!wmi_supported)
		return -ENODEV;

	return wmi_driver_register(&dell_smbios_wmi_driver);
}

static void __exit exit_dell_smbios_wmi(void)
{
	wmi_driver_unregister(&dell_smbios_wmi_driver);
}

module_init(init_dell_smbios_wmi);
module_exit(exit_dell_smbios_wmi);

MODULE_ALIAS("wmi:" DELL_WMI_SMBIOS_GUID);
MODULE_AUTHOR("Mario Limonciello <mario.limonciello@dell.com>");
MODULE_DESCRIPTION("Dell SMBIOS communications over WMI");
MODULE_LICENSE("GPL");
