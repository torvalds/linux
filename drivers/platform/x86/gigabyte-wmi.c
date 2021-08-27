// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2021 Thomas Weißschuh <thomas@weissschuh.net>
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/hwmon.h>
#include <linux/module.h>
#include <linux/wmi.h>

#define GIGABYTE_WMI_GUID	"DEADBEEF-2001-0000-00A0-C90629100000"
#define NUM_TEMPERATURE_SENSORS	6

static bool force_load;
module_param(force_load, bool, 0444);
MODULE_PARM_DESC(force_load, "Force loading on unknown platform");

static u8 usable_sensors_mask;

enum gigabyte_wmi_commandtype {
	GIGABYTE_WMI_BUILD_DATE_QUERY       =   0x1,
	GIGABYTE_WMI_MAINBOARD_TYPE_QUERY   =   0x2,
	GIGABYTE_WMI_FIRMWARE_VERSION_QUERY =   0x4,
	GIGABYTE_WMI_MAINBOARD_NAME_QUERY   =   0x5,
	GIGABYTE_WMI_TEMPERATURE_QUERY      = 0x125,
};

struct gigabyte_wmi_args {
	u32 arg1;
};

static int gigabyte_wmi_perform_query(struct wmi_device *wdev,
				      enum gigabyte_wmi_commandtype command,
				      struct gigabyte_wmi_args *args, struct acpi_buffer *out)
{
	const struct acpi_buffer in = {
		.length = sizeof(*args),
		.pointer = args,
	};

	acpi_status ret = wmidev_evaluate_method(wdev, 0x0, command, &in, out);

	if (ACPI_FAILURE(ret))
		return -EIO;

	return 0;
}

static int gigabyte_wmi_query_integer(struct wmi_device *wdev,
				      enum gigabyte_wmi_commandtype command,
				      struct gigabyte_wmi_args *args, u64 *res)
{
	union acpi_object *obj;
	struct acpi_buffer result = { ACPI_ALLOCATE_BUFFER, NULL };
	int ret;

	ret = gigabyte_wmi_perform_query(wdev, command, args, &result);
	if (ret)
		return ret;
	obj = result.pointer;
	if (obj && obj->type == ACPI_TYPE_INTEGER)
		*res = obj->integer.value;
	else
		ret = -EIO;
	kfree(result.pointer);
	return ret;
}

static int gigabyte_wmi_temperature(struct wmi_device *wdev, u8 sensor, long *res)
{
	struct gigabyte_wmi_args args = {
		.arg1 = sensor,
	};
	u64 temp;
	acpi_status ret;

	ret = gigabyte_wmi_query_integer(wdev, GIGABYTE_WMI_TEMPERATURE_QUERY, &args, &temp);
	if (ret == 0) {
		if (temp == 0)
			return -ENODEV;
		*res = (s8)temp * 1000; // value is a signed 8-bit integer
	}
	return ret;
}

static int gigabyte_wmi_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
				   u32 attr, int channel, long *val)
{
	struct wmi_device *wdev = dev_get_drvdata(dev);

	return gigabyte_wmi_temperature(wdev, channel, val);
}

static umode_t gigabyte_wmi_hwmon_is_visible(const void *data, enum hwmon_sensor_types type,
					     u32 attr, int channel)
{
	return usable_sensors_mask & BIT(channel) ? 0444  : 0;
}

static const struct hwmon_channel_info *gigabyte_wmi_hwmon_info[] = {
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT,
			   HWMON_T_INPUT,
			   HWMON_T_INPUT,
			   HWMON_T_INPUT,
			   HWMON_T_INPUT,
			   HWMON_T_INPUT),
	NULL
};

static const struct hwmon_ops gigabyte_wmi_hwmon_ops = {
	.read = gigabyte_wmi_hwmon_read,
	.is_visible = gigabyte_wmi_hwmon_is_visible,
};

static const struct hwmon_chip_info gigabyte_wmi_hwmon_chip_info = {
	.ops = &gigabyte_wmi_hwmon_ops,
	.info = gigabyte_wmi_hwmon_info,
};

static u8 gigabyte_wmi_detect_sensor_usability(struct wmi_device *wdev)
{
	int i;
	long temp;
	u8 r = 0;

	for (i = 0; i < NUM_TEMPERATURE_SENSORS; i++) {
		if (!gigabyte_wmi_temperature(wdev, i, &temp))
			r |= BIT(i);
	}
	return r;
}

#define DMI_EXACT_MATCH_GIGABYTE_BOARD_NAME(name) \
	{ .matches = { \
		DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "Gigabyte Technology Co., Ltd."), \
		DMI_EXACT_MATCH(DMI_BOARD_NAME, name), \
	}}

static const struct dmi_system_id gigabyte_wmi_known_working_platforms[] = {
	DMI_EXACT_MATCH_GIGABYTE_BOARD_NAME("B450M S2H V2"),
	DMI_EXACT_MATCH_GIGABYTE_BOARD_NAME("B550 AORUS ELITE"),
	DMI_EXACT_MATCH_GIGABYTE_BOARD_NAME("B550 AORUS ELITE V2"),
	DMI_EXACT_MATCH_GIGABYTE_BOARD_NAME("B550 GAMING X V2"),
	DMI_EXACT_MATCH_GIGABYTE_BOARD_NAME("B550M AORUS PRO-P"),
	DMI_EXACT_MATCH_GIGABYTE_BOARD_NAME("B550M DS3H"),
	DMI_EXACT_MATCH_GIGABYTE_BOARD_NAME("Z390 I AORUS PRO WIFI-CF"),
	DMI_EXACT_MATCH_GIGABYTE_BOARD_NAME("X570 AORUS ELITE"),
	DMI_EXACT_MATCH_GIGABYTE_BOARD_NAME("X570 GAMING X"),
	DMI_EXACT_MATCH_GIGABYTE_BOARD_NAME("X570 I AORUS PRO WIFI"),
	DMI_EXACT_MATCH_GIGABYTE_BOARD_NAME("X570 UD"),
	{ }
};

static int gigabyte_wmi_probe(struct wmi_device *wdev, const void *context)
{
	struct device *hwmon_dev;

	if (!dmi_check_system(gigabyte_wmi_known_working_platforms)) {
		if (!force_load)
			return -ENODEV;
		dev_warn(&wdev->dev, "Forcing load on unknown platform");
	}

	usable_sensors_mask = gigabyte_wmi_detect_sensor_usability(wdev);
	if (!usable_sensors_mask) {
		dev_info(&wdev->dev, "No temperature sensors usable");
		return -ENODEV;
	}

	hwmon_dev = devm_hwmon_device_register_with_info(&wdev->dev, "gigabyte_wmi", wdev,
							 &gigabyte_wmi_hwmon_chip_info, NULL);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct wmi_device_id gigabyte_wmi_id_table[] = {
	{ GIGABYTE_WMI_GUID, NULL },
	{ }
};

static struct wmi_driver gigabyte_wmi_driver = {
	.driver = {
		.name = "gigabyte-wmi",
	},
	.id_table = gigabyte_wmi_id_table,
	.probe = gigabyte_wmi_probe,
};
module_wmi_driver(gigabyte_wmi_driver);

MODULE_DEVICE_TABLE(wmi, gigabyte_wmi_id_table);
MODULE_AUTHOR("Thomas Weißschuh <thomas@weissschuh.net>");
MODULE_DESCRIPTION("Gigabyte WMI temperature driver");
MODULE_LICENSE("GPL");
