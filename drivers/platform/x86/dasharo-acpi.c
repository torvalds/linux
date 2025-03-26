// SPDX-License-Identifier: GPL-2.0+
/*
 * Dasharo ACPI Driver
 *
 * Copyright (C) 2025 3mdeb Sp. z o.o.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/acpi.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sysfs.h>
#include <linux/types.h>

enum dasharo_feature {
	DASHARO_FEATURE_TEMPERATURE = 0,
	DASHARO_FEATURE_FAN_PWM,
	DASHARO_FEATURE_FAN_TACH,
	DASHARO_FEATURE_FAN_POINTS,
	DASHARO_FEATURE_MAX,
};

enum dasharo_temperature {
	DASHARO_TEMPERATURE_CPU_PACKAGE = 0,
	DASHARO_TEMPERATURE_CPU_CORE,
	DASHARO_TEMPERATURE_GPU,
	DASHARO_TEMPERATURE_BOARD,
	DASHARO_TEMPERATURE_CHASSIS,
	DASHARO_TEMPERATURE_MAX,
};

enum dasharo_fan {
	DASHARO_FAN_CPU = 0,
	DASHARO_FAN_GPU,
	DASHARO_FAN_CHASSIS,
	DASHARO_FAN_MAX,
};

static char * dasharo_temp_group_name[DASHARO_TEMPERATURE_MAX] = {
	[DASHARO_TEMPERATURE_CPU_PACKAGE] = "CPU Package",
	[DASHARO_TEMPERATURE_CPU_CORE] = "CPU Core",
	[DASHARO_TEMPERATURE_GPU] = "GPU",
	[DASHARO_TEMPERATURE_BOARD] = "Board",
	[DASHARO_TEMPERATURE_CHASSIS] = "Chassis",
};

static char * dasharo_fan_group_name[DASHARO_FAN_MAX] = {
	[DASHARO_FAN_CPU] = "CPU",
	[DASHARO_FAN_GPU] = "GPU",
	[DASHARO_FAN_CHASSIS] = "Chassis",
};

#define MAX_CAP_NAME_LEN 16

struct dasharo_capability {
	int cap;
	int index;
	char name[MAX_CAP_NAME_LEN];
};

#define MAX_CAPS_PER_FEAT 24

struct dasharo_data {
	struct acpi_device *acpi_dev;
	int sensors_count;
	int fan_tachs_count;
	int fan_pwms_count;
	struct dasharo_capability sensors[MAX_CAPS_PER_FEAT];
	struct dasharo_capability fan_tachs[MAX_CAPS_PER_FEAT];
	struct dasharo_capability fan_pwms[MAX_CAPS_PER_FEAT];
	struct device *hwmon;
};

static int dasharo_get_feature_cap_count(struct dasharo_data *data, int feat, int cap)
{
	union acpi_object obj[2];
	struct acpi_object_list obj_list;
	acpi_handle handle;
	acpi_status status;
	unsigned long long ret = 0;

	obj[0].type = ACPI_TYPE_INTEGER;
	obj[0].integer.value = feat;
	obj[1].type = ACPI_TYPE_INTEGER;
	obj[1].integer.value = cap;
	obj_list.count = 2;
	obj_list.pointer = &obj[0];

	handle = acpi_device_handle(data->acpi_dev);
	status = acpi_evaluate_integer(handle, "GFCP", &obj_list, &ret);
	if (ACPI_SUCCESS(status))
		return ret;
	return -ENODEV;
}

static int dasharo_read_value_by_cap_idx(struct dasharo_data *data, const char *method, int cap, int index, long *value)
{
	union acpi_object obj[2];
	struct acpi_object_list obj_list;
	acpi_handle handle;
	acpi_status status;
	unsigned long long ret = 0;

	obj[0].type = ACPI_TYPE_INTEGER;
	obj[0].integer.value = cap;
	obj[1].type = ACPI_TYPE_INTEGER;
	obj[1].integer.value = index;
	obj_list.count = 2;
	obj_list.pointer = &obj[0];

	handle = acpi_device_handle(data->acpi_dev);
	status = acpi_evaluate_integer(handle, method, &obj_list, &ret);
	if (ACPI_SUCCESS(status)) {
		*value = ret;
		return ret;
	}
	return -ENODEV;
}

static int dasharo_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			      u32 attr, int channel, long *val)
{
	struct dasharo_data *data = dev_get_drvdata(dev);
	int ret = 0;
	long value;

	if (type == hwmon_temp) {
		if (attr == hwmon_temp_input) {
			ret = dasharo_read_value_by_cap_idx(data,
						      "GTMP",
						      data->sensors[channel].cap,
						      data->sensors[channel].index,
						      &value);

			if (ret > 0) {
				*val = value * 1000;
			}
		}
	} else if (type == hwmon_fan) {
		if (attr == hwmon_fan_input) {
			ret = dasharo_read_value_by_cap_idx(data,
						      "GFTH",
						      data->fan_tachs[channel].cap,
						      data->fan_tachs[channel].index,
						      &value);

			if (ret > 0) {
				*val = value;
			}
		}
	} else if (type == hwmon_pwm) {
		if (attr == hwmon_pwm_input) {
			ret = dasharo_read_value_by_cap_idx(data,
						      "GFDC",
						      data->fan_tachs[channel].cap,
						      data->fan_tachs[channel].index,
						      &value);

			if (ret > 0) {
				*val = value;
			}
		}
	}

	return ret;
}

static int dasharo_hwmon_read_string(struct device *dev, enum hwmon_sensor_types type,
				     u32 attr, int channel, const char **str)
{
	struct dasharo_data *data = dev_get_drvdata(dev);

	if (channel < data->sensors_count && type == hwmon_temp && attr == hwmon_temp_label) {
		*str = data->sensors[channel].name;
		return 0;
	} else if (channel < data->fan_tachs_count && type == hwmon_fan && attr == hwmon_fan_label) {
		*str = data->fan_tachs[channel].name;
		return 0;
	}

	return -EOPNOTSUPP;
}

static umode_t dasharo_hwmon_is_visible(const void *drvdata, enum hwmon_sensor_types type,
					u32 attr, int channel)
{
	const struct dasharo_data *data = drvdata;

	if (channel < data->sensors_count && type == hwmon_temp) {
		return 0444;
	}

	if (channel < data->fan_pwms_count && type == hwmon_pwm) {
		return 0444;
	}

	if (channel < data->fan_tachs_count && type == hwmon_fan) {
		return 0444;
	}

	return 0;
}
static const struct hwmon_ops dasharo_hwmon_ops = {
	.is_visible = dasharo_hwmon_is_visible,
	.read_string = dasharo_hwmon_read_string,
	.read = dasharo_hwmon_read,
};

// Max 24 capabilities per feature
static const struct hwmon_channel_info * const dasharo_hwmon_info[] = {
	HWMON_CHANNEL_INFO(fan,
		HWMON_F_INPUT | HWMON_F_LABEL,
		HWMON_F_INPUT | HWMON_F_LABEL,
		HWMON_F_INPUT | HWMON_F_LABEL,
		HWMON_F_INPUT | HWMON_F_LABEL,
		HWMON_F_INPUT | HWMON_F_LABEL,
		HWMON_F_INPUT | HWMON_F_LABEL,
		HWMON_F_INPUT | HWMON_F_LABEL,
		HWMON_F_INPUT | HWMON_F_LABEL,
		HWMON_F_INPUT | HWMON_F_LABEL,
		HWMON_F_INPUT | HWMON_F_LABEL,
		HWMON_F_INPUT | HWMON_F_LABEL,
		HWMON_F_INPUT | HWMON_F_LABEL,
		HWMON_F_INPUT | HWMON_F_LABEL,
		HWMON_F_INPUT | HWMON_F_LABEL,
		HWMON_F_INPUT | HWMON_F_LABEL,
		HWMON_F_INPUT | HWMON_F_LABEL,
		HWMON_F_INPUT | HWMON_F_LABEL,
		HWMON_F_INPUT | HWMON_F_LABEL,
		HWMON_F_INPUT | HWMON_F_LABEL,
		HWMON_F_INPUT | HWMON_F_LABEL,
		HWMON_F_INPUT | HWMON_F_LABEL,
		HWMON_F_INPUT | HWMON_F_LABEL,
		HWMON_F_INPUT | HWMON_F_LABEL,
		HWMON_F_INPUT | HWMON_F_LABEL),
	HWMON_CHANNEL_INFO(temp,
		HWMON_T_INPUT | HWMON_T_LABEL,
		HWMON_T_INPUT | HWMON_T_LABEL,
		HWMON_T_INPUT | HWMON_T_LABEL,
		HWMON_T_INPUT | HWMON_T_LABEL,
		HWMON_T_INPUT | HWMON_T_LABEL,
		HWMON_T_INPUT | HWMON_T_LABEL,
		HWMON_T_INPUT | HWMON_T_LABEL,
		HWMON_T_INPUT | HWMON_T_LABEL,
		HWMON_T_INPUT | HWMON_T_LABEL,
		HWMON_T_INPUT | HWMON_T_LABEL,
		HWMON_T_INPUT | HWMON_T_LABEL,
		HWMON_T_INPUT | HWMON_T_LABEL,
		HWMON_T_INPUT | HWMON_T_LABEL,
		HWMON_T_INPUT | HWMON_T_LABEL,
		HWMON_T_INPUT | HWMON_T_LABEL,
		HWMON_T_INPUT | HWMON_T_LABEL,
		HWMON_T_INPUT | HWMON_T_LABEL,
		HWMON_T_INPUT | HWMON_T_LABEL,
		HWMON_T_INPUT | HWMON_T_LABEL,
		HWMON_T_INPUT | HWMON_T_LABEL,
		HWMON_T_INPUT | HWMON_T_LABEL,
		HWMON_T_INPUT | HWMON_T_LABEL,
		HWMON_T_INPUT | HWMON_T_LABEL,
		HWMON_T_INPUT | HWMON_T_LABEL),
	HWMON_CHANNEL_INFO(pwm,
		HWMON_PWM_INPUT,
		HWMON_PWM_INPUT,
		HWMON_PWM_INPUT,
		HWMON_PWM_INPUT,
		HWMON_PWM_INPUT,
		HWMON_PWM_INPUT,
		HWMON_PWM_INPUT,
		HWMON_PWM_INPUT,
		HWMON_PWM_INPUT,
		HWMON_PWM_INPUT,
		HWMON_PWM_INPUT,
		HWMON_PWM_INPUT,
		HWMON_PWM_INPUT,
		HWMON_PWM_INPUT,
		HWMON_PWM_INPUT,
		HWMON_PWM_INPUT,
		HWMON_PWM_INPUT,
		HWMON_PWM_INPUT,
		HWMON_PWM_INPUT,
		HWMON_PWM_INPUT,
		HWMON_PWM_INPUT,
		HWMON_PWM_INPUT,
		HWMON_PWM_INPUT,
		HWMON_PWM_INPUT),
	NULL
};

static const struct hwmon_chip_info dasharo_hwmon_chip_info = {
	.ops = &dasharo_hwmon_ops,
	.info = dasharo_hwmon_info,
};

static void dasharo_fill_sensors(struct dasharo_data *data) {
	int group = 0;
	int count = 0;

	while (group < DASHARO_TEMPERATURE_MAX) {
		count = dasharo_get_feature_cap_count(data, DASHARO_FEATURE_TEMPERATURE, group);

		for (int i = 0; i < count && data->sensors_count < MAX_CAPS_PER_FEAT; ++i) {
			data->sensors[data->sensors_count].cap = group;
			data->sensors[data->sensors_count].index = i;
			snprintf(data->sensors[data->sensors_count].name, MAX_CAP_NAME_LEN, "%s %d", dasharo_temp_group_name[group], i);
			data->sensors_count++;
		}

		group++;
	}
}

static void dasharo_fill_fan_tachs(struct dasharo_data *data) {
	int group = 0;
	int count = 0;

	while (group < DASHARO_FAN_MAX) {
		count = dasharo_get_feature_cap_count(data, DASHARO_FEATURE_FAN_TACH, group);

		for (int i = 0; i < count && data->fan_tachs_count < MAX_CAPS_PER_FEAT; ++i) {
			data->fan_tachs[data->fan_tachs_count].cap = group;
			data->fan_tachs[data->fan_tachs_count].index = i;
			snprintf(data->fan_tachs[data->fan_tachs_count].name, MAX_CAP_NAME_LEN, "%s %d", dasharo_fan_group_name[group], i);
			data->fan_tachs_count++;
		}

		group++;
	}
}

static void dasharo_fill_fan_pwms(struct dasharo_data *data) {
	int group = 0;
	int count = 0;

	while (group < DASHARO_FAN_MAX) {
		count = dasharo_get_feature_cap_count(data, DASHARO_FEATURE_FAN_PWM, group);

		for (int i = 0; i < count && data->fan_pwms_count < MAX_CAPS_PER_FEAT; ++i) {
			data->fan_pwms[data->fan_pwms_count].cap = group;
			data->fan_pwms[data->fan_pwms_count].index = i;
			snprintf(data->fan_pwms[data->fan_pwms_count].name, MAX_CAP_NAME_LEN, "%s %d", dasharo_fan_group_name[group], i);
			data->fan_pwms_count++;
		}

		group++;
	}
}

static int dasharo_add(struct acpi_device *acpi_dev)
{
	struct dasharo_data *data;

	data = devm_kzalloc(&acpi_dev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	acpi_dev->driver_data = data;
	data->acpi_dev = acpi_dev;

	pr_info("Dasharo driver loading\n");

	dasharo_fill_sensors(data);
	dasharo_fill_fan_tachs(data);
	dasharo_fill_fan_pwms(data);

	data->hwmon = devm_hwmon_device_register_with_info(&acpi_dev->dev,
		"dasharo_acpi", data, &dasharo_hwmon_chip_info, NULL);

	return 0;
}

static void dasharo_remove(struct acpi_device *acpi_dev)
{
	struct dasharo_data *data = acpi_driver_data(acpi_dev);
}

static const struct acpi_device_id device_ids[] = {
	{"DSHR0001", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, device_ids);

static struct acpi_driver dasharo_driver = {
	.name = "Dasharo ACPI Driver",
	.class = "Dasharo",
	.ids = device_ids,
	.ops = {
		.add = dasharo_add,
		.remove = dasharo_remove,
	},
};
module_acpi_driver(dasharo_driver);

MODULE_DESCRIPTION("Dasharo ACPI Driver");
MODULE_AUTHOR("Michał Kopeć <michal.kopec@3mdeb.com>");
MODULE_LICENSE("GPL");
