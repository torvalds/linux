/*
 * INT3400 thermal driver
 *
 * Copyright (C) 2014, Intel Corporation
 * Authors: Zhang Rui <rui.zhang@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/thermal.h>
#include "acpi_thermal_rel.h"

#define INT3400_THERMAL_TABLE_CHANGED 0x83

enum int3400_thermal_uuid {
	INT3400_THERMAL_PASSIVE_1,
	INT3400_THERMAL_ACTIVE,
	INT3400_THERMAL_CRITICAL,
	INT3400_THERMAL_MAXIMUM_UUID,
};

static char *int3400_thermal_uuids[INT3400_THERMAL_MAXIMUM_UUID] = {
	"42A441D6-AE6A-462b-A84B-4A8CE79027D3",
	"3A95C389-E4B8-4629-A526-C52C88626BAE",
	"97C68AE7-15FA-499c-B8C9-5DA81D606E0A",
};

struct int3400_thermal_priv {
	struct acpi_device *adev;
	struct thermal_zone_device *thermal;
	int mode;
	int art_count;
	struct art *arts;
	int trt_count;
	struct trt *trts;
	u8 uuid_bitmap;
	int rel_misc_dev_res;
	int current_uuid_index;
};

static ssize_t available_uuids_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct int3400_thermal_priv *priv = platform_get_drvdata(pdev);
	int i;
	int length = 0;

	for (i = 0; i < INT3400_THERMAL_MAXIMUM_UUID; i++) {
		if (priv->uuid_bitmap & (1 << i))
			if (PAGE_SIZE - length > 0)
				length += snprintf(&buf[length],
						   PAGE_SIZE - length,
						   "%s\n",
						   int3400_thermal_uuids[i]);
	}

	return length;
}

static ssize_t current_uuid_show(struct device *dev,
				 struct device_attribute *devattr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct int3400_thermal_priv *priv = platform_get_drvdata(pdev);

	if (priv->uuid_bitmap & (1 << priv->current_uuid_index))
		return sprintf(buf, "%s\n",
			       int3400_thermal_uuids[priv->current_uuid_index]);
	else
		return sprintf(buf, "INVALID\n");
}

static ssize_t current_uuid_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct int3400_thermal_priv *priv = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < INT3400_THERMAL_MAXIMUM_UUID; ++i) {
		if ((priv->uuid_bitmap & (1 << i)) &&
		    !(strncmp(buf, int3400_thermal_uuids[i],
			      sizeof(int3400_thermal_uuids[i]) - 1))) {
			priv->current_uuid_index = i;
			return count;
		}
	}

	return -EINVAL;
}

static DEVICE_ATTR_RW(current_uuid);
static DEVICE_ATTR_RO(available_uuids);
static struct attribute *uuid_attrs[] = {
	&dev_attr_available_uuids.attr,
	&dev_attr_current_uuid.attr,
	NULL
};

static const struct attribute_group uuid_attribute_group = {
	.attrs = uuid_attrs,
	.name = "uuids"
};

static int int3400_thermal_get_uuids(struct int3400_thermal_priv *priv)
{
	struct acpi_buffer buf = { ACPI_ALLOCATE_BUFFER, NULL};
	union acpi_object *obja, *objb;
	int i, j;
	int result = 0;
	acpi_status status;

	status = acpi_evaluate_object(priv->adev->handle, "IDSP", NULL, &buf);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	obja = (union acpi_object *)buf.pointer;
	if (obja->type != ACPI_TYPE_PACKAGE) {
		result = -EINVAL;
		goto end;
	}

	for (i = 0; i < obja->package.count; i++) {
		objb = &obja->package.elements[i];
		if (objb->type != ACPI_TYPE_BUFFER) {
			result = -EINVAL;
			goto end;
		}

		/* UUID must be 16 bytes */
		if (objb->buffer.length != 16) {
			result = -EINVAL;
			goto end;
		}

		for (j = 0; j < INT3400_THERMAL_MAXIMUM_UUID; j++) {
			guid_t guid;

			guid_parse(int3400_thermal_uuids[j], &guid);
			if (guid_equal((guid_t *)objb->buffer.pointer, &guid)) {
				priv->uuid_bitmap |= (1 << j);
				break;
			}
		}
	}

end:
	kfree(buf.pointer);
	return result;
}

static int int3400_thermal_run_osc(acpi_handle handle,
				enum int3400_thermal_uuid uuid, bool enable)
{
	u32 ret, buf[2];
	acpi_status status;
	int result = 0;
	struct acpi_osc_context context = {
		.uuid_str = int3400_thermal_uuids[uuid],
		.rev = 1,
		.cap.length = 8,
	};

	buf[OSC_QUERY_DWORD] = 0;
	buf[OSC_SUPPORT_DWORD] = enable;

	context.cap.pointer = buf;

	status = acpi_run_osc(handle, &context);
	if (ACPI_SUCCESS(status)) {
		ret = *((u32 *)(context.ret.pointer + 4));
		if (ret != enable)
			result = -EPERM;
	} else
		result = -EPERM;

	kfree(context.ret.pointer);
	return result;
}

static void int3400_notify(acpi_handle handle,
			u32 event,
			void *data)
{
	struct int3400_thermal_priv *priv = data;
	char *thermal_prop[5];

	if (!priv)
		return;

	switch (event) {
	case INT3400_THERMAL_TABLE_CHANGED:
		thermal_prop[0] = kasprintf(GFP_KERNEL, "NAME=%s",
				priv->thermal->type);
		thermal_prop[1] = kasprintf(GFP_KERNEL, "TEMP=%d",
				priv->thermal->temperature);
		thermal_prop[2] = kasprintf(GFP_KERNEL, "TRIP=");
		thermal_prop[3] = kasprintf(GFP_KERNEL, "EVENT=%d",
				THERMAL_TABLE_CHANGED);
		thermal_prop[4] = NULL;
		kobject_uevent_env(&priv->thermal->device.kobj, KOBJ_CHANGE,
				thermal_prop);
		break;
	default:
		/* Ignore unknown notification codes sent to INT3400 device */
		break;
	}
}

static int int3400_thermal_get_temp(struct thermal_zone_device *thermal,
			int *temp)
{
	*temp = 20 * 1000; /* faked temp sensor with 20C */
	return 0;
}

static int int3400_thermal_get_mode(struct thermal_zone_device *thermal,
				enum thermal_device_mode *mode)
{
	struct int3400_thermal_priv *priv = thermal->devdata;

	if (!priv)
		return -EINVAL;

	*mode = priv->mode;

	return 0;
}

static int int3400_thermal_set_mode(struct thermal_zone_device *thermal,
				enum thermal_device_mode mode)
{
	struct int3400_thermal_priv *priv = thermal->devdata;
	bool enable;
	int result = 0;

	if (!priv)
		return -EINVAL;

	if (mode == THERMAL_DEVICE_ENABLED)
		enable = true;
	else if (mode == THERMAL_DEVICE_DISABLED)
		enable = false;
	else
		return -EINVAL;

	if (enable != priv->mode) {
		priv->mode = enable;
		result = int3400_thermal_run_osc(priv->adev->handle,
						 priv->current_uuid_index,
						 enable);
	}
	return result;
}

static struct thermal_zone_device_ops int3400_thermal_ops = {
	.get_temp = int3400_thermal_get_temp,
};

static struct thermal_zone_params int3400_thermal_params = {
	.governor_name = "user_space",
	.no_hwmon = true,
};

static int int3400_thermal_probe(struct platform_device *pdev)
{
	struct acpi_device *adev = ACPI_COMPANION(&pdev->dev);
	struct int3400_thermal_priv *priv;
	int result;

	if (!adev)
		return -ENODEV;

	priv = kzalloc(sizeof(struct int3400_thermal_priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->adev = adev;

	result = int3400_thermal_get_uuids(priv);
	if (result)
		goto free_priv;

	result = acpi_parse_art(priv->adev->handle, &priv->art_count,
				&priv->arts, true);
	if (result)
		dev_dbg(&pdev->dev, "_ART table parsing error\n");

	result = acpi_parse_trt(priv->adev->handle, &priv->trt_count,
				&priv->trts, true);
	if (result)
		dev_dbg(&pdev->dev, "_TRT table parsing error\n");

	platform_set_drvdata(pdev, priv);

	if (priv->uuid_bitmap & 1 << INT3400_THERMAL_PASSIVE_1) {
		int3400_thermal_ops.get_mode = int3400_thermal_get_mode;
		int3400_thermal_ops.set_mode = int3400_thermal_set_mode;
	}
	priv->thermal = thermal_zone_device_register("INT3400 Thermal", 0, 0,
						priv, &int3400_thermal_ops,
						&int3400_thermal_params, 0, 0);
	if (IS_ERR(priv->thermal)) {
		result = PTR_ERR(priv->thermal);
		goto free_art_trt;
	}

	priv->rel_misc_dev_res = acpi_thermal_rel_misc_device_add(
							priv->adev->handle);

	result = sysfs_create_group(&pdev->dev.kobj, &uuid_attribute_group);
	if (result)
		goto free_rel_misc;

	result = acpi_install_notify_handler(
			priv->adev->handle, ACPI_DEVICE_NOTIFY, int3400_notify,
			(void *)priv);
	if (result)
		goto free_sysfs;

	return 0;

free_sysfs:
	sysfs_remove_group(&pdev->dev.kobj, &uuid_attribute_group);
free_rel_misc:
	if (!priv->rel_misc_dev_res)
		acpi_thermal_rel_misc_device_remove(priv->adev->handle);
	thermal_zone_device_unregister(priv->thermal);
free_art_trt:
	kfree(priv->trts);
	kfree(priv->arts);
free_priv:
	kfree(priv);
	return result;
}

static int int3400_thermal_remove(struct platform_device *pdev)
{
	struct int3400_thermal_priv *priv = platform_get_drvdata(pdev);

	acpi_remove_notify_handler(
			priv->adev->handle, ACPI_DEVICE_NOTIFY,
			int3400_notify);

	if (!priv->rel_misc_dev_res)
		acpi_thermal_rel_misc_device_remove(priv->adev->handle);

	sysfs_remove_group(&pdev->dev.kobj, &uuid_attribute_group);
	thermal_zone_device_unregister(priv->thermal);
	kfree(priv->trts);
	kfree(priv->arts);
	kfree(priv);
	return 0;
}

static const struct acpi_device_id int3400_thermal_match[] = {
	{"INT3400", 0},
	{}
};

MODULE_DEVICE_TABLE(acpi, int3400_thermal_match);

static struct platform_driver int3400_thermal_driver = {
	.probe = int3400_thermal_probe,
	.remove = int3400_thermal_remove,
	.driver = {
		   .name = "int3400 thermal",
		   .acpi_match_table = ACPI_PTR(int3400_thermal_match),
		   },
};

module_platform_driver(int3400_thermal_driver);

MODULE_DESCRIPTION("INT3400 Thermal driver");
MODULE_AUTHOR("Zhang Rui <rui.zhang@intel.com>");
MODULE_LICENSE("GPL");
