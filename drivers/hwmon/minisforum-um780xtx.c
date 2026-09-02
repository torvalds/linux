// SPDX-License-Identifier: GPL-2.0-only
/*
 * Minisforum UM780 XTX (F7BSD) embedded-controller hwmon driver.
 *
 * Copyright (C) 2026 Sebastián Peyrott <speyrott@gmail.com>
 */

#include <linux/acpi.h>
#include <linux/cleanup.h>
#include <linux/dmi.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>

#define UM780XTX_EC_TEMP_SYS	0x05
#define UM780XTX_EC_TEMP_CPU	0x09
#define UM780XTX_EC_CPU_PROFILE	0x2f
#define UM780XTX_EC_SYS_POINT1	0x31
#define UM780XTX_EC_SYS_POINT2	0x34
#define UM780XTX_EC_SYS_POINT3	0x37

#define UM780XTX_EC_PROFILE_B1	0xb1
#define UM780XTX_EC_PROFILE_B2	0xb2

#define UM780XTX_EC_FAN1_LO	0xb6
#define UM780XTX_EC_FAN1_HI	0xb7
#define UM780XTX_EC_FAN2_LO	0xb9
#define UM780XTX_EC_FAN2_HI	0xba

#define UM780XTX_EC_MAX_RPM	9000
#define UM780XTX_EC_RPM_RETRIES	3

static struct platform_device *um780xtx_pdev;

struct um780xtx_data {
	struct device *hwmon_dev;
	u8 saved_profile;
	u8 saved_sys_point1;
	u8 saved_sys_point2;
	u8 sys_point3;
};

static const struct dmi_system_id um780xtx_dmi_table[] = {
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR,
					"Micro Computer (HK) Tech Limited"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Venus series"),
			DMI_EXACT_MATCH(DMI_BOARD_VENDOR,
					"Shenzhen Meigao Electronic Equipment Co.,Ltd"),
			DMI_EXACT_MATCH(DMI_BOARD_NAME, "F7BSD"),
		},
	},
	{ }
};
MODULE_DEVICE_TABLE(dmi, um780xtx_dmi_table);

static bool um780xtx_firmware_match(void)
{
	const char *board_version = dmi_get_system_info(DMI_BOARD_VERSION);
	const char *bios_version = dmi_get_system_info(DMI_BIOS_VERSION);

	return board_version && bios_version &&
		!strcmp(board_version, "1.1") && !strcmp(bios_version, "1.06");
}

static int um780xtx_oem_read(u8 command, u8 *value)
{
	return ec_transaction(command, NULL, 0, value, 1);
}

static int um780xtx_read_rpm(u8 command_hi, u8 command_lo, long *rpm)
{
	u8 hi_before;
	u8 hi_after;
	u8 lo;
	unsigned int value;
	int attempt;
	int ret;

	for (attempt = 0; attempt < UM780XTX_EC_RPM_RETRIES; attempt++) {
		ret = um780xtx_oem_read(command_hi, &hi_before);
		if (ret)
			return ret;
		ret = um780xtx_oem_read(command_lo, &lo);
		if (ret)
			return ret;
		ret = um780xtx_oem_read(command_hi, &hi_after);
		if (ret)
			return ret;
		if (hi_before != hi_after)
			continue;

		value = (hi_after << 8) | lo;
		if (value > UM780XTX_EC_MAX_RPM)
			continue;

		*rpm = value;
		return 0;
	}

	return -EIO;
}

static int um780xtx_read_profile(long *mode)
{
	u8 profile;
	int ret;

	ret = ec_read(UM780XTX_EC_CPU_PROFILE, &profile);
	if (ret)
		return ret;
	if (profile == UM780XTX_EC_PROFILE_B1) {
		*mode = 2;
		return 0;
	}
	if (profile == UM780XTX_EC_PROFILE_B2) {
		*mode = 3;
		return 0;
	}

	return -ENODATA;
}

static int um780xtx_write_profile(struct um780xtx_data *data, long mode)
{
	u8 expected;
	u8 profile;
	int ret;

	if (mode != 2 && mode != 3)
		return -EINVAL;
	expected = mode == 2 ? UM780XTX_EC_PROFILE_B1 : UM780XTX_EC_PROFILE_B2;

	ret = ec_transaction(expected, NULL, 0, NULL, 0);
	if (ret)
		return ret;
	ret = ec_read(UM780XTX_EC_CPU_PROFILE, &profile);
	if (ret)
		return ret;
	if (profile != expected)
		return -EIO;

	data->saved_profile = profile;
	return 0;
}

static const u8 um780xtx_sys_point_offsets[] = {
	UM780XTX_EC_SYS_POINT1,
	UM780XTX_EC_SYS_POINT2,
};

static ssize_t um780xtx_sys_point_temp_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	u8 value;
	int ret;

	guard(hwmon_lock)(dev);
	ret = ec_read(um780xtx_sys_point_offsets[sattr->index], &value);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%u\n", value * 1000);
}

static ssize_t um780xtx_sys_point_temp_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t count)
{
	struct um780xtx_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *sattr = to_sensor_dev_attr(attr);
	u8 points[2];
	u8 readback;
	long value, lo, hi;
	int ret;

	ret = kstrtol(buf, 10, &value);
	if (ret)
		return ret;

	guard(hwmon_lock)(dev);
	/* Validate and clamp against the live peer threshold. */
	ret = ec_read(UM780XTX_EC_SYS_POINT1, &points[0]);
	if (ret)
		return ret;
	ret = ec_read(UM780XTX_EC_SYS_POINT2, &points[1]);
	if (ret)
		return ret;
	if (points[0] >= points[1] || points[1] >= data->sys_point3)
		return -EIO;

	if (!sattr->index) {
		lo = 0;
		hi = points[1] - 1;
	} else {
		lo = points[0] + 1;
		hi = data->sys_point3 - 1;
	}
	value = DIV_ROUND_CLOSEST(clamp_val(value, 0, 255000), 1000);
	points[sattr->index] = clamp_val(value, lo, hi);

	ret = ec_write(um780xtx_sys_point_offsets[sattr->index],
		       points[sattr->index]);
	if (ret)
		return ret;
	ret = ec_read(um780xtx_sys_point_offsets[sattr->index], &readback);
	if (ret)
		return ret;
	if (readback != points[sattr->index])
		return -EIO;

	data->saved_sys_point1 = points[0];
	data->saved_sys_point2 = points[1];
	return count;
}

static SENSOR_DEVICE_ATTR_RW(pwm2_auto_point1_temp,
			     um780xtx_sys_point_temp, 0);
static SENSOR_DEVICE_ATTR_RW(pwm2_auto_point2_temp,
			     um780xtx_sys_point_temp, 1);

static struct attribute *um780xtx_extra_attrs[] = {
	&sensor_dev_attr_pwm2_auto_point1_temp.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point2_temp.dev_attr.attr,
	NULL
};

static const struct attribute_group um780xtx_extra_group = {
	.attrs = um780xtx_extra_attrs,
};

static const struct attribute_group *um780xtx_extra_groups[] = {
	&um780xtx_extra_group,
	NULL
};

static umode_t um780xtx_is_visible(const void *data,
				   enum hwmon_sensor_types type,
				   u32 attr, int channel)
{
	if (type == hwmon_temp || type == hwmon_fan)
		return 0444;
	if (type == hwmon_pwm && attr == hwmon_pwm_enable)
		return 0644;
	return 0;
}

static int um780xtx_read(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long *value)
{
	u8 raw;
	int ret;

	if (type == hwmon_temp) {
		ret = ec_read(channel ? UM780XTX_EC_TEMP_SYS :
			      UM780XTX_EC_TEMP_CPU, &raw);
		if (ret)
			return ret;
		*value = raw * 1000L;
		return 0;
	}
	if (type == hwmon_fan) {
		if (!channel)
			ret = um780xtx_read_rpm(UM780XTX_EC_FAN1_HI,
						UM780XTX_EC_FAN1_LO, value);
		else
			ret = um780xtx_read_rpm(UM780XTX_EC_FAN2_HI,
						UM780XTX_EC_FAN2_LO, value);
		return ret;
	}
	if (type == hwmon_pwm)
		return um780xtx_read_profile(value);

	return -EOPNOTSUPP;
}

static int um780xtx_write(struct device *dev, enum hwmon_sensor_types type,
			  u32 attr, int channel, long value)
{
	struct um780xtx_data *data = dev_get_drvdata(dev);

	if (type == hwmon_pwm && attr == hwmon_pwm_enable)
		return um780xtx_write_profile(data, value);

	return -EOPNOTSUPP;
}

static int um780xtx_read_string(struct device *dev,
				enum hwmon_sensor_types type,
				u32 attr, int channel, const char **str)
{
	static const char * const labels[] = { "CPU", "SYS" };

	if (type == hwmon_temp && attr == hwmon_temp_label)
		*str = labels[channel];
	else if (type == hwmon_fan && attr == hwmon_fan_label)
		*str = labels[channel];
	else
		return -EOPNOTSUPP;
	return 0;
}

static const struct hwmon_ops um780xtx_hwmon_ops = {
	.is_visible = um780xtx_is_visible,
	.read = um780xtx_read,
	.write = um780xtx_write,
	.read_string = um780xtx_read_string,
};

static const struct hwmon_channel_info * const um780xtx_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL),
	HWMON_CHANNEL_INFO(fan, HWMON_F_INPUT | HWMON_F_LABEL,
			   HWMON_F_INPUT | HWMON_F_LABEL),
	HWMON_CHANNEL_INFO(pwm, HWMON_PWM_ENABLE),
	NULL
};

static const struct hwmon_chip_info um780xtx_chip_info = {
	.ops = &um780xtx_hwmon_ops,
	.info = um780xtx_info,
};

static int um780xtx_write_sys_point(u8 offset, u8 value)
{
	u8 readback;
	int ret;

	ret = ec_write(offset, value);
	if (ret)
		return ret;
	ret = ec_read(offset, &readback);
	if (ret)
		return ret;

	return readback == value ? 0 : -EIO;
}

static int um780xtx_restore_sys_points(struct um780xtx_data *data,
				       u8 current_point2)
{
	u8 point1 = data->saved_sys_point1;
	u8 point2 = data->saved_sys_point2;
	int ret;

	/* Keep strict ordering valid after each individual EC write. */
	if (point1 >= current_point2) {
		ret = um780xtx_write_sys_point(UM780XTX_EC_SYS_POINT2, point2);
		if (ret)
			return ret;
		return um780xtx_write_sys_point(UM780XTX_EC_SYS_POINT1,
						 point1);
	}

	ret = um780xtx_write_sys_point(UM780XTX_EC_SYS_POINT1, point1);
	if (ret)
		return ret;
	return um780xtx_write_sys_point(UM780XTX_EC_SYS_POINT2, point2);
}

static int um780xtx_read_initial_state(struct um780xtx_data *data)
{
	u8 profile;
	u8 point1;
	u8 point2;
	int ret;

	ret = ec_read(UM780XTX_EC_CPU_PROFILE, &profile);
	if (ret)
		return ret;
	if (profile != UM780XTX_EC_PROFILE_B1 &&
	    profile != UM780XTX_EC_PROFILE_B2)
		return -EINVAL;

	ret = ec_read(UM780XTX_EC_SYS_POINT1, &point1);
	if (ret)
		return ret;
	ret = ec_read(UM780XTX_EC_SYS_POINT2, &point2);
	if (ret)
		return ret;
	if (point1 >= point2 || point2 >= data->sys_point3)
		return -EINVAL;

	data->saved_profile = profile;
	data->saved_sys_point1 = point1;
	data->saved_sys_point2 = point2;
	return 0;
}

static int um780xtx_restore_state(struct um780xtx_data *data)
{
	u8 readback;
	u8 point2;
	int ret;

	ret = ec_transaction(data->saved_profile, NULL, 0, NULL, 0);
	if (ret)
		return ret;
	ret = ec_read(UM780XTX_EC_CPU_PROFILE, &readback);
	if (ret)
		return ret;
	if (readback != data->saved_profile)
		return -EIO;

	ret = ec_read(UM780XTX_EC_SYS_POINT2, &point2);
	if (ret)
		return ret;

	return um780xtx_restore_sys_points(data, point2);
}

static int um780xtx_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct um780xtx_data *data;
	struct device *hwmon;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	platform_set_drvdata(pdev, data);

	ret = ec_read(UM780XTX_EC_SYS_POINT3, &data->sys_point3);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to read system fan limit\n");
	ret = um780xtx_read_initial_state(data);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to read initial fan settings\n");

	hwmon = devm_hwmon_device_register_with_info(dev, "um780xtx_ec", data,
						     &um780xtx_chip_info,
						     um780xtx_extra_groups);
	if (IS_ERR(hwmon))
		return PTR_ERR(hwmon);
	data->hwmon_dev = hwmon;

	return 0;
}

static int um780xtx_resume(struct device *dev)
{
	struct um780xtx_data *data = dev_get_drvdata(dev);

	guard(hwmon_lock)(data->hwmon_dev);
	return um780xtx_restore_state(data);
}

static DEFINE_SIMPLE_DEV_PM_OPS(um780xtx_pm_ops, NULL, um780xtx_resume);

static struct platform_driver um780xtx_driver = {
	.driver = {
		.name = "um780xtx-ec-hwmon",
		.pm = pm_sleep_ptr(&um780xtx_pm_ops),
	},
	.probe = um780xtx_probe,
};

static int __init um780xtx_init(void)
{
	int ret;

	if (!dmi_check_system(um780xtx_dmi_table) ||
	    !um780xtx_firmware_match() || !ec_get_handle())
		return -ENODEV;
	ret = platform_driver_register(&um780xtx_driver);
	if (ret)
		return ret;

	um780xtx_pdev = platform_device_register_simple("um780xtx-ec-hwmon",
							PLATFORM_DEVID_NONE,
						       NULL, 0);
	if (IS_ERR(um780xtx_pdev)) {
		ret = PTR_ERR(um780xtx_pdev);
		platform_driver_unregister(&um780xtx_driver);
		return ret;
	}
	return 0;
}

static void __exit um780xtx_exit(void)
{
	platform_device_unregister(um780xtx_pdev);
	platform_driver_unregister(&um780xtx_driver);
}

module_init(um780xtx_init);
module_exit(um780xtx_exit);

MODULE_AUTHOR("Sebastián Peyrott <speyrott@gmail.com>");
MODULE_DESCRIPTION("Minisforum UM780 XTX EC hwmon driver");
MODULE_LICENSE("GPL");
