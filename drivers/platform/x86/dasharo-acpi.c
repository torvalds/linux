// SPDX-License-Identifier: GPL-2.0+
/*
 * Dasharo ACPI Driver
 */

#include <linux/acpi.h>
#include <linux/array_size.h>
#include <linux/hwmon.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/units.h>

enum dasharo_feature {
	DASHARO_FEATURE_TEMPERATURE = 0,
	DASHARO_FEATURE_FAN_PWM,
	DASHARO_FEATURE_FAN_TACH,
	DASHARO_FEATURE_MAX
};

enum dasharo_temperature {
	DASHARO_TEMPERATURE_CPU_PACKAGE = 0,
	DASHARO_TEMPERATURE_CPU_CORE,
	DASHARO_TEMPERATURE_GPU,
	DASHARO_TEMPERATURE_BOARD,
	DASHARO_TEMPERATURE_CHASSIS,
	DASHARO_TEMPERATURE_MAX
};

enum dasharo_fan {
	DASHARO_FAN_CPU = 0,
	DASHARO_FAN_GPU,
	DASHARO_FAN_CHASSIS,
	DASHARO_FAN_MAX
};

#define MAX_GROUPS_PER_FEAT 8

static const char * const dasharo_group_names[DASHARO_FEATURE_MAX][MAX_GROUPS_PER_FEAT] = {
	[DASHARO_FEATURE_TEMPERATURE] = {
		[DASHARO_TEMPERATURE_CPU_PACKAGE] = "CPU Package",
		[DASHARO_TEMPERATURE_CPU_CORE] = "CPU Core",
		[DASHARO_TEMPERATURE_GPU] = "GPU",
		[DASHARO_TEMPERATURE_BOARD] = "Board",
		[DASHARO_TEMPERATURE_CHASSIS] = "Chassis",
	},
	[DASHARO_FEATURE_FAN_PWM] = {
		[DASHARO_FAN_CPU] = "CPU",
		[DASHARO_FAN_GPU] = "GPU",
		[DASHARO_FAN_CHASSIS] = "Chassis",
	},
	[DASHARO_FEATURE_FAN_TACH] = {
		[DASHARO_FAN_CPU] = "CPU",
		[DASHARO_FAN_GPU] = "GPU",
		[DASHARO_FAN_CHASSIS] = "Chassis",
	},
};

struct dasharo_capability {
	unsigned int group;
	unsigned int index;
	char name[16];
};

#define MAX_CAPS_PER_FEAT 24

struct dasharo_data {
	struct platform_device *pdev;
	int caps_found[DASHARO_FEATURE_MAX];
	struct dasharo_capability capabilities[DASHARO_FEATURE_MAX][MAX_CAPS_PER_FEAT];
};

static int dasharo_get_feature_cap_count(struct dasharo_data *data, enum dasharo_feature feat, int cap)
{
	struct acpi_object_list obj_list;
	union acpi_object obj[2];
	acpi_handle handle;
	acpi_status status;
	u64 count;

	obj[0].type = ACPI_TYPE_INTEGER;
	obj[0].integer.value = feat;
	obj[1].type = ACPI_TYPE_INTEGER;
	obj[1].integer.value = cap;
	obj_list.count = 2;
	obj_list.pointer = &obj[0];

	handle = ACPI_HANDLE(&data->pdev->dev);
	status = acpi_evaluate_integer(handle, "GFCP", &obj_list, &count);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	return count;
}

static int dasharo_read_channel(struct dasharo_data *data, char *method, enum dasharo_feature feat, int channel, long *value)
{
	struct acpi_object_list obj_list;
	union acpi_object obj[2];
	acpi_handle handle;
	acpi_status status;
	u64 val;

	if (feat >= ARRAY_SIZE(data->capabilities))
		return -EINVAL;

	if (channel >= data->caps_found[feat])
		return -EINVAL;

	obj[0].type = ACPI_TYPE_INTEGER;
	obj[0].integer.value = data->capabilities[feat][channel].group;
	obj[1].type = ACPI_TYPE_INTEGER;
	obj[1].integer.value = data->capabilities[feat][channel].index;
	obj_list.count = 2;
	obj_list.pointer = &obj[0];

	handle = ACPI_HANDLE(&data->pdev->dev);
	status = acpi_evaluate_integer(handle, method, &obj_list, &val);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	*value = val;
	return 0;
}

static int dasharo_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			      u32 attr, int channel, long *val)
{
	struct dasharo_data *data = dev_get_drvdata(dev);
	long value;
	int ret;

	switch (type) {
	case hwmon_temp:
		ret = dasharo_read_channel(data, "GTMP", DASHARO_FEATURE_TEMPERATURE, channel, &value);
		if (!ret)
			*val = value * MILLIDEGREE_PER_DEGREE;
		break;
	case hwmon_fan:
		ret = dasharo_read_channel(data, "GFTH", DASHARO_FEATURE_FAN_TACH, channel, &value);
		if (!ret)
			*val = value;
		break;
	case hwmon_pwm:
		ret = dasharo_read_channel(data, "GFDC", DASHARO_FEATURE_FAN_PWM, channel, &value);
		if (!ret)
			*val = value;
		break;
	default:
		return -ENODEV;
		break;
	}

	return ret;
}

static int dasharo_hwmon_read_string(struct device *dev, enum hwmon_sensor_types type,
				     u32 attr, int channel, const char **str)
{
	struct dasharo_data *data = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_temp:
		if (channel >= data->caps_found[DASHARO_FEATURE_TEMPERATURE])
			return -EINVAL;

		*str = data->capabilities[DASHARO_FEATURE_TEMPERATURE][channel].name;
		break;
	case hwmon_fan:
		if (channel >= data->caps_found[DASHARO_FEATURE_FAN_TACH])
			return -EINVAL;

		*str = data->capabilities[DASHARO_FEATURE_FAN_TACH][channel].name;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static umode_t dasharo_hwmon_is_visible(const void *drvdata, enum hwmon_sensor_types type,
					u32 attr, int channel)
{
	const struct dasharo_data *data = drvdata;

	switch (type) {
	case hwmon_temp:
		if (channel < data->caps_found[DASHARO_FEATURE_TEMPERATURE])
			return 0444;
		break;
	case hwmon_pwm:
		if (channel < data->caps_found[DASHARO_FEATURE_FAN_PWM])
			return 0444;
		break;
	case hwmon_fan:
		if (channel < data->caps_found[DASHARO_FEATURE_FAN_TACH])
			return 0444;
		break;
	default:
		break;
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

static void dasharo_fill_feature_caps(struct dasharo_data *data, enum dasharo_feature feat)
{
	struct dasharo_capability *cap;
	int cap_count = 0;
	int count;

	for (int group = 0; group < MAX_GROUPS_PER_FEAT; ++group) {
		count = dasharo_get_feature_cap_count(data, feat, group);
		if (count <= 0)
			continue;

		for (unsigned int i = 0; i < count; ++i) {
			if (cap_count >= ARRAY_SIZE(data->capabilities[feat]))
				break;

			cap = &data->capabilities[feat][cap_count];
			cap->group = group;
			cap->index = i;
			scnprintf(cap->name, sizeof(cap->name), "%s %d",
				  dasharo_group_names[feat][group], i);
			cap_count++;
		}
	}
	data->caps_found[feat] = cap_count;
}

static int dasharo_probe(struct platform_device *pdev)
{
	struct dasharo_data *data;
	struct device *hwmon;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data->pdev = pdev;

	for (unsigned int i = 0; i < DASHARO_FEATURE_MAX; ++i)
		dasharo_fill_feature_caps(data, i);

	hwmon = devm_hwmon_device_register_with_info(&pdev->dev, "dasharo_acpi", data,
						     &dasharo_hwmon_chip_info, NULL);

	return PTR_ERR_OR_ZERO(hwmon);
}

static const struct acpi_device_id dasharo_device_ids[] = {
	{"DSHR0001", 0},
	{}
};
MODULE_DEVICE_TABLE(acpi, dasharo_device_ids);

static struct platform_driver dasharo_driver = {
	.driver = {
		.name = "dasharo-acpi",
		.acpi_match_table = dasharo_device_ids,
	},
	.probe = dasharo_probe,
};
module_platform_driver(dasharo_driver);

MODULE_DESCRIPTION("Dasharo ACPI Driver");
MODULE_AUTHOR("Michał Kopeć <michal.kopec@3mdeb.com>");
MODULE_LICENSE("GPL");
