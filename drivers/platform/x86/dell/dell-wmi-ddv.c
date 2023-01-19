// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Linux driver for WMI sensor information on Dell notebooks.
 *
 * Copyright (C) 2022 Armin Wolf <W_Armin@gmx.de>
 */

#define pr_format(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/dev_printk.h>
#include <linux/kernel.h>
#include <linux/kstrtox.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/limits.h>
#include <linux/power_supply.h>
#include <linux/printk.h>
#include <linux/seq_file.h>
#include <linux/sysfs.h>
#include <linux/wmi.h>

#include <acpi/battery.h>

#define DRIVER_NAME	"dell-wmi-ddv"

#define DELL_DDV_SUPPORTED_INTERFACE 2
#define DELL_DDV_GUID	"8A42EA14-4F2A-FD45-6422-0087F7A7E608"

#define DELL_EPPID_LENGTH	20
#define DELL_EPPID_EXT_LENGTH	23

enum dell_ddv_method {
	DELL_DDV_BATTERY_DESIGN_CAPACITY	= 0x01,
	DELL_DDV_BATTERY_FULL_CHARGE_CAPACITY	= 0x02,
	DELL_DDV_BATTERY_MANUFACTURE_NAME	= 0x03,
	DELL_DDV_BATTERY_MANUFACTURE_DATE	= 0x04,
	DELL_DDV_BATTERY_SERIAL_NUMBER		= 0x05,
	DELL_DDV_BATTERY_CHEMISTRY_VALUE	= 0x06,
	DELL_DDV_BATTERY_TEMPERATURE		= 0x07,
	DELL_DDV_BATTERY_CURRENT		= 0x08,
	DELL_DDV_BATTERY_VOLTAGE		= 0x09,
	DELL_DDV_BATTERY_MANUFACTURER_ACCESS	= 0x0A,
	DELL_DDV_BATTERY_RELATIVE_CHARGE_STATE	= 0x0B,
	DELL_DDV_BATTERY_CYCLE_COUNT		= 0x0C,
	DELL_DDV_BATTERY_EPPID			= 0x0D,
	DELL_DDV_BATTERY_RAW_ANALYTICS_START	= 0x0E,
	DELL_DDV_BATTERY_RAW_ANALYTICS		= 0x0F,
	DELL_DDV_BATTERY_DESIGN_VOLTAGE		= 0x10,

	DELL_DDV_INTERFACE_VERSION		= 0x12,

	DELL_DDV_FAN_SENSOR_INFORMATION		= 0x20,
	DELL_DDV_THERMAL_SENSOR_INFORMATION	= 0x22,
};

struct dell_wmi_ddv_data {
	struct acpi_battery_hook hook;
	struct device_attribute temp_attr;
	struct device_attribute eppid_attr;
	struct wmi_device *wdev;
};

static int dell_wmi_ddv_query_type(struct wmi_device *wdev, enum dell_ddv_method method, u32 arg,
				   union acpi_object **result, acpi_object_type type)
{
	struct acpi_buffer out = { ACPI_ALLOCATE_BUFFER, NULL };
	const struct acpi_buffer in = {
		.length = sizeof(arg),
		.pointer = &arg,
	};
	union acpi_object *obj;
	acpi_status ret;

	ret = wmidev_evaluate_method(wdev, 0x0, method, &in, &out);
	if (ACPI_FAILURE(ret))
		return -EIO;

	obj = out.pointer;
	if (!obj)
		return -ENODATA;

	if (obj->type != type) {
		kfree(obj);
		return -EIO;
	}

	*result = obj;

	return 0;
}

static int dell_wmi_ddv_query_integer(struct wmi_device *wdev, enum dell_ddv_method method,
				      u32 arg, u32 *res)
{
	union acpi_object *obj;
	int ret;

	ret = dell_wmi_ddv_query_type(wdev, method, arg, &obj, ACPI_TYPE_INTEGER);
	if (ret < 0)
		return ret;

	if (obj->integer.value <= U32_MAX)
		*res = (u32)obj->integer.value;
	else
		ret = -ERANGE;

	kfree(obj);

	return ret;
}

static int dell_wmi_ddv_query_buffer(struct wmi_device *wdev, enum dell_ddv_method method,
				     u32 arg, union acpi_object **result)
{
	union acpi_object *obj;
	u64 buffer_size;
	int ret;

	ret = dell_wmi_ddv_query_type(wdev, method, arg, &obj, ACPI_TYPE_PACKAGE);
	if (ret < 0)
		return ret;

	if (obj->package.count != 2)
		goto err_free;

	if (obj->package.elements[0].type != ACPI_TYPE_INTEGER)
		goto err_free;

	buffer_size = obj->package.elements[0].integer.value;

	if (obj->package.elements[1].type != ACPI_TYPE_BUFFER)
		goto err_free;

	if (buffer_size > obj->package.elements[1].buffer.length) {
		dev_warn(&wdev->dev,
			 FW_WARN "WMI buffer size (%llu) exceeds ACPI buffer size (%d)\n",
			 buffer_size, obj->package.elements[1].buffer.length);

		goto err_free;
	}

	*result = obj;

	return 0;

err_free:
	kfree(obj);

	return -EIO;
}

static int dell_wmi_ddv_query_string(struct wmi_device *wdev, enum dell_ddv_method method,
				     u32 arg, union acpi_object **result)
{
	return dell_wmi_ddv_query_type(wdev, method, arg, result, ACPI_TYPE_STRING);
}

static int dell_wmi_ddv_battery_index(struct acpi_device *acpi_dev, u32 *index)
{
	const char *uid_str;

	uid_str = acpi_device_uid(acpi_dev);
	if (!uid_str)
		return -ENODEV;

	return kstrtou32(uid_str, 10, index);
}

static ssize_t temp_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dell_wmi_ddv_data *data = container_of(attr, struct dell_wmi_ddv_data, temp_attr);
	u32 index, value;
	int ret;

	ret = dell_wmi_ddv_battery_index(to_acpi_device(dev->parent), &index);
	if (ret < 0)
		return ret;

	ret = dell_wmi_ddv_query_integer(data->wdev, DELL_DDV_BATTERY_TEMPERATURE, index, &value);
	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%d\n", DIV_ROUND_CLOSEST(value, 10));
}

static ssize_t eppid_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dell_wmi_ddv_data *data = container_of(attr, struct dell_wmi_ddv_data, eppid_attr);
	union acpi_object *obj;
	u32 index;
	int ret;

	ret = dell_wmi_ddv_battery_index(to_acpi_device(dev->parent), &index);
	if (ret < 0)
		return ret;

	ret = dell_wmi_ddv_query_string(data->wdev, DELL_DDV_BATTERY_EPPID, index, &obj);
	if (ret < 0)
		return ret;

	if (obj->string.length != DELL_EPPID_LENGTH && obj->string.length != DELL_EPPID_EXT_LENGTH)
		dev_info_once(&data->wdev->dev, FW_INFO "Suspicious ePPID length (%d)\n",
			      obj->string.length);

	ret = sysfs_emit(buf, "%s\n", obj->string.pointer);

	kfree(obj);

	return ret;
}

static int dell_wmi_ddv_add_battery(struct power_supply *battery, struct acpi_battery_hook *hook)
{
	struct dell_wmi_ddv_data *data = container_of(hook, struct dell_wmi_ddv_data, hook);
	u32 index;
	int ret;

	/* Return 0 instead of error to avoid being unloaded */
	ret = dell_wmi_ddv_battery_index(to_acpi_device(battery->dev.parent), &index);
	if (ret < 0)
		return 0;

	ret = device_create_file(&battery->dev, &data->temp_attr);
	if (ret < 0)
		return ret;

	ret = device_create_file(&battery->dev, &data->eppid_attr);
	if (ret < 0) {
		device_remove_file(&battery->dev, &data->temp_attr);

		return ret;
	}

	return 0;
}

static int dell_wmi_ddv_remove_battery(struct power_supply *battery, struct acpi_battery_hook *hook)
{
	struct dell_wmi_ddv_data *data = container_of(hook, struct dell_wmi_ddv_data, hook);

	device_remove_file(&battery->dev, &data->temp_attr);
	device_remove_file(&battery->dev, &data->eppid_attr);

	return 0;
}

static void dell_wmi_ddv_battery_remove(void *data)
{
	struct acpi_battery_hook *hook = data;

	battery_hook_unregister(hook);
}

static int dell_wmi_ddv_battery_add(struct dell_wmi_ddv_data *data)
{
	data->hook.name = "Dell DDV Battery Extension";
	data->hook.add_battery = dell_wmi_ddv_add_battery;
	data->hook.remove_battery = dell_wmi_ddv_remove_battery;

	sysfs_attr_init(&data->temp_attr.attr);
	data->temp_attr.attr.name = "temp";
	data->temp_attr.attr.mode = 0444;
	data->temp_attr.show = temp_show;

	sysfs_attr_init(&data->eppid_attr.attr);
	data->eppid_attr.attr.name = "eppid";
	data->eppid_attr.attr.mode = 0444;
	data->eppid_attr.show = eppid_show;

	battery_hook_register(&data->hook);

	return devm_add_action_or_reset(&data->wdev->dev, dell_wmi_ddv_battery_remove, &data->hook);
}

static int dell_wmi_ddv_buffer_read(struct seq_file *seq, enum dell_ddv_method method)
{
	struct device *dev = seq->private;
	struct dell_wmi_ddv_data *data = dev_get_drvdata(dev);
	union acpi_object *obj;
	u64 size;
	u8 *buf;
	int ret;

	ret = dell_wmi_ddv_query_buffer(data->wdev, method, 0, &obj);
	if (ret < 0)
		return ret;

	size = obj->package.elements[0].integer.value;
	buf = obj->package.elements[1].buffer.pointer;
	ret = seq_write(seq, buf, size);
	kfree(obj);

	return ret;
}

static int dell_wmi_ddv_fan_read(struct seq_file *seq, void *offset)
{
	return dell_wmi_ddv_buffer_read(seq, DELL_DDV_FAN_SENSOR_INFORMATION);
}

static int dell_wmi_ddv_temp_read(struct seq_file *seq, void *offset)
{
	return dell_wmi_ddv_buffer_read(seq, DELL_DDV_THERMAL_SENSOR_INFORMATION);
}

static void dell_wmi_ddv_debugfs_remove(void *data)
{
	struct dentry *entry = data;

	debugfs_remove(entry);
}

static void dell_wmi_ddv_debugfs_init(struct wmi_device *wdev)
{
	struct dentry *entry;
	char name[64];

	scnprintf(name, ARRAY_SIZE(name), "%s-%s", DRIVER_NAME, dev_name(&wdev->dev));
	entry = debugfs_create_dir(name, NULL);

	debugfs_create_devm_seqfile(&wdev->dev, "fan_sensor_information", entry,
				    dell_wmi_ddv_fan_read);
	debugfs_create_devm_seqfile(&wdev->dev, "thermal_sensor_information", entry,
				    dell_wmi_ddv_temp_read);

	devm_add_action_or_reset(&wdev->dev, dell_wmi_ddv_debugfs_remove, entry);
}

static int dell_wmi_ddv_probe(struct wmi_device *wdev, const void *context)
{
	struct dell_wmi_ddv_data *data;
	u32 version;
	int ret;

	ret = dell_wmi_ddv_query_integer(wdev, DELL_DDV_INTERFACE_VERSION, 0, &version);
	if (ret < 0)
		return ret;

	dev_dbg(&wdev->dev, "WMI interface version: %d\n", version);
	if (version != DELL_DDV_SUPPORTED_INTERFACE)
		return -ENODEV;

	data = devm_kzalloc(&wdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	dev_set_drvdata(&wdev->dev, data);
	data->wdev = wdev;

	dell_wmi_ddv_debugfs_init(wdev);

	return dell_wmi_ddv_battery_add(data);
}

static const struct wmi_device_id dell_wmi_ddv_id_table[] = {
	{ DELL_DDV_GUID, NULL },
	{ }
};
MODULE_DEVICE_TABLE(wmi, dell_wmi_ddv_id_table);

static struct wmi_driver dell_wmi_ddv_driver = {
	.driver = {
		.name = DRIVER_NAME,
	},
	.id_table = dell_wmi_ddv_id_table,
	.probe = dell_wmi_ddv_probe,
};
module_wmi_driver(dell_wmi_ddv_driver);

MODULE_AUTHOR("Armin Wolf <W_Armin@gmx.de>");
MODULE_DESCRIPTION("Dell WMI sensor driver");
MODULE_LICENSE("GPL");
