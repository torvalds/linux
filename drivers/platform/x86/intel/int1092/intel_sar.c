// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, Intel Corporation.
 */

#include <linux/acpi.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <linux/sysfs.h>
#include "intel_sar.h"

/**
 * get_int_value: Retrieve integer values from ACPI Object
 * @obj: acpi_object pointer which has the integer value
 * @out: output pointer will get integer value
 *
 * Function is used to retrieve integer value from acpi object.
 *
 * Return:
 * * 0 on success
 * * -EIO if there is an issue in acpi_object passed.
 */
static int get_int_value(union acpi_object *obj, int *out)
{
	if (!obj || obj->type != ACPI_TYPE_INTEGER)
		return -EIO;
	*out = (int)obj->integer.value;
	return 0;
}

/**
 * update_sar_data: sar data is updated based on regulatory mode
 * @context: pointer to driver context structure
 *
 * sar_data is updated based on regulatory value
 * context->reg_value will never exceed MAX_REGULATORY
 */
static void update_sar_data(struct wwan_sar_context *context)
{
	struct wwan_device_mode_configuration *config =
		&context->config_data[context->reg_value];

	if (config->device_mode_info &&
	    context->sar_data.device_mode < config->total_dev_mode) {
		int itr = 0;

		for (itr = 0; itr < config->total_dev_mode; itr++) {
			if (context->sar_data.device_mode ==
				config->device_mode_info[itr].device_mode) {
				struct wwan_device_mode_info *dev_mode =
				&config->device_mode_info[itr];

				context->sar_data.antennatable_index = dev_mode->antennatable_index;
				context->sar_data.bandtable_index = dev_mode->bandtable_index;
				context->sar_data.sartable_index = dev_mode->sartable_index;
				break;
			}
		}
	}
}

/**
 * parse_package: parse acpi package for retrieving SAR information
 * @context: pointer to driver context structure
 * @item : acpi_object pointer
 *
 * Given acpi_object is iterated to retrieve information for each device mode.
 * If a given package corresponding to a specific device mode is faulty, it is
 * skipped and the specific entry in context structure will have the default value
 * of zero. Decoding of subsequent device modes is realized by having "continue"
 * statements in the for loop on encountering error in parsing given device mode.
 *
 * Return:
 * AE_OK if success
 * AE_ERROR on error
 */
static acpi_status parse_package(struct wwan_sar_context *context, union acpi_object *item)
{
	struct wwan_device_mode_configuration *data;
	int value, itr, reg;
	union acpi_object *num;

	num = &item->package.elements[0];
	if (get_int_value(num, &value) || value < 0 || value >= MAX_REGULATORY)
		return AE_ERROR;

	reg = value;

	data = &context->config_data[reg];
	if (data->total_dev_mode > MAX_DEV_MODES ||	data->total_dev_mode == 0 ||
	    item->package.count <= data->total_dev_mode)
		return AE_ERROR;

	data->device_mode_info = kmalloc_array(data->total_dev_mode,
					       sizeof(struct wwan_device_mode_info), GFP_KERNEL);
	if (!data->device_mode_info)
		return AE_ERROR;

	for (itr = 0; itr < data->total_dev_mode; itr++) {
		struct wwan_device_mode_info temp = { 0 };

		num = &item->package.elements[itr + 1];
		if (num->type != ACPI_TYPE_PACKAGE || num->package.count < TOTAL_DATA)
			continue;
		if (get_int_value(&num->package.elements[0], &temp.device_mode))
			continue;
		if (get_int_value(&num->package.elements[1], &temp.bandtable_index))
			continue;
		if (get_int_value(&num->package.elements[2], &temp.antennatable_index))
			continue;
		if (get_int_value(&num->package.elements[3], &temp.sartable_index))
			continue;
		data->device_mode_info[itr] = temp;
	}
	return AE_OK;
}

/**
 * sar_get_device_mode: Extraction of information from BIOS via DSM calls
 * @device: ACPI device for which to retrieve the data
 *
 * Retrieve the current device mode information from the BIOS.
 *
 * Return:
 * AE_OK on success
 * AE_ERROR on error
 */
static acpi_status sar_get_device_mode(struct platform_device *device)
{
	struct wwan_sar_context *context = dev_get_drvdata(&device->dev);
	acpi_status status = AE_OK;
	union acpi_object *out;
	u32 rev = 0;
	int value;

	out = acpi_evaluate_dsm(context->handle, &context->guid, rev,
				COMMAND_ID_DEV_MODE, NULL);
	if (get_int_value(out, &value)) {
		dev_err(&device->dev, "DSM cmd:%d Failed to retrieve value\n", COMMAND_ID_DEV_MODE);
		status = AE_ERROR;
		goto dev_mode_error;
	}
	context->sar_data.device_mode = value;
	update_sar_data(context);
	sysfs_notify(&device->dev.kobj, NULL, SYSFS_DATANAME);

dev_mode_error:
	ACPI_FREE(out);
	return status;
}

static const struct acpi_device_id sar_device_ids[] = {
	{ "INTC1092", 0},
	{}
};
MODULE_DEVICE_TABLE(acpi, sar_device_ids);

static ssize_t intc_data_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct wwan_sar_context *context = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d %d %d %d\n", context->sar_data.device_mode,
		      context->sar_data.bandtable_index,
		      context->sar_data.antennatable_index,
		      context->sar_data.sartable_index);
}
static DEVICE_ATTR_RO(intc_data);

static ssize_t intc_reg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct wwan_sar_context *context = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", context->reg_value);
}

static ssize_t intc_reg_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct wwan_sar_context *context = dev_get_drvdata(dev);
	unsigned int value;
	int read;

	if (!count)
		return -EINVAL;
	read = kstrtouint(buf, 10, &value);
	if (read < 0)
		return read;
	if (value >= MAX_REGULATORY)
		return -EOVERFLOW;
	context->reg_value = value;
	update_sar_data(context);
	sysfs_notify(&dev->kobj, NULL, SYSFS_DATANAME);
	return count;
}
static DEVICE_ATTR_RW(intc_reg);

static struct attribute *intcsar_attrs[] = {
	&dev_attr_intc_data.attr,
	&dev_attr_intc_reg.attr,
	NULL
};

static struct attribute_group intcsar_group = {
	.attrs = intcsar_attrs,
};

static void sar_notify(acpi_handle handle, u32 event, void *data)
{
	struct platform_device *device = data;

	if (event == SAR_EVENT) {
		if (sar_get_device_mode(device) != AE_OK)
			dev_err(&device->dev, "sar_get_device_mode error");
	}
}

static void sar_get_data(int reg, struct wwan_sar_context *context)
{
	union acpi_object *out, req;
	u32 rev = 0;

	req.type = ACPI_TYPE_INTEGER;
	req.integer.value = reg;
	out = acpi_evaluate_dsm(context->handle, &context->guid, rev,
				COMMAND_ID_CONFIG_TABLE, &req);
	if (!out)
		return;
	if (out->type == ACPI_TYPE_PACKAGE && out->package.count >= 3 &&
	    out->package.elements[0].type == ACPI_TYPE_INTEGER &&
	    out->package.elements[1].type == ACPI_TYPE_INTEGER &&
	    out->package.elements[2].type == ACPI_TYPE_PACKAGE &&
	    out->package.elements[2].package.count > 0) {
		context->config_data[reg].version = out->package.elements[0].integer.value;
		context->config_data[reg].total_dev_mode =
			out->package.elements[1].integer.value;
		if (context->config_data[reg].total_dev_mode <= 0 ||
		    context->config_data[reg].total_dev_mode > MAX_DEV_MODES) {
			ACPI_FREE(out);
			return;
		}
		parse_package(context, &out->package.elements[2]);
	}
	ACPI_FREE(out);
}

static int sar_probe(struct platform_device *device)
{
	struct wwan_sar_context *context;
	int reg;
	int result;

	context = kzalloc(sizeof(*context), GFP_KERNEL);
	if (!context)
		return -ENOMEM;

	context->sar_device = device;
	context->handle = ACPI_HANDLE(&device->dev);
	dev_set_drvdata(&device->dev, context);

	result = guid_parse(SAR_DSM_UUID, &context->guid);
	if (result) {
		dev_err(&device->dev, "SAR UUID parse error: %d\n", result);
		goto r_free;
	}

	for (reg = 0; reg < MAX_REGULATORY; reg++)
		sar_get_data(reg, context);

	if (sar_get_device_mode(device) != AE_OK) {
		dev_err(&device->dev, "Failed to get device mode\n");
		result = -EIO;
		goto r_free;
	}

	result = sysfs_create_group(&device->dev.kobj, &intcsar_group);
	if (result) {
		dev_err(&device->dev, "sysfs creation failed\n");
		goto r_free;
	}

	if (acpi_install_notify_handler(ACPI_HANDLE(&device->dev), ACPI_DEVICE_NOTIFY,
					sar_notify, (void *)device) != AE_OK) {
		dev_err(&device->dev, "Failed acpi_install_notify_handler\n");
		result = -EIO;
		goto r_sys;
	}
	return 0;

r_sys:
	sysfs_remove_group(&device->dev.kobj, &intcsar_group);
r_free:
	kfree(context);
	return result;
}

static int sar_remove(struct platform_device *device)
{
	struct wwan_sar_context *context = dev_get_drvdata(&device->dev);
	int reg;

	acpi_remove_notify_handler(ACPI_HANDLE(&device->dev),
				   ACPI_DEVICE_NOTIFY, sar_notify);
	sysfs_remove_group(&device->dev.kobj, &intcsar_group);
	for (reg = 0; reg < MAX_REGULATORY; reg++)
		kfree(context->config_data[reg].device_mode_info);

	kfree(context);
	return 0;
}

static struct platform_driver sar_driver = {
	.probe = sar_probe,
	.remove = sar_remove,
	.driver = {
		.name = DRVNAME,
		.acpi_match_table = ACPI_PTR(sar_device_ids)
	}
};
module_platform_driver(sar_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Platform device driver for INTEL MODEM BIOS SAR");
MODULE_AUTHOR("Shravan Sudhakar <s.shravan@intel.com>");
