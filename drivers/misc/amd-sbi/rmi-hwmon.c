// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * rmi-hwmon.c - hwmon sensor support for side band RMI
 *
 * Copyright (C) 2025 Advanced Micro Devices, Inc.
 */
#include <linux/err.h>
#include <linux/hwmon.h>
#include <uapi/misc/amd-apml.h>
#include "rmi-core.h"

/* Do not allow setting negative power limit */
#define SBRMI_PWR_MIN  0

static int sbrmi_read(struct device *dev, enum hwmon_sensor_types type,
		      u32 attr, int channel, long *val)
{
	struct sbrmi_data *data = dev_get_drvdata(dev);
	struct apml_mbox_msg msg = { 0 };
	int ret;

	if (!data)
		return -ENODEV;

	if (type != hwmon_power)
		return -EINVAL;

	switch (attr) {
	case hwmon_power_input:
		msg.cmd = SBRMI_READ_PKG_PWR_CONSUMPTION;
		ret = rmi_mailbox_xfer(data, &msg);
		break;
	case hwmon_power_cap:
		msg.cmd = SBRMI_READ_PKG_PWR_LIMIT;
		ret = rmi_mailbox_xfer(data, &msg);
		break;
	case hwmon_power_cap_max:
		msg.mb_in_out = data->pwr_limit_max;
		ret = 0;
		break;
	default:
		return -EINVAL;
	}
	if (ret < 0)
		return ret;
	/* hwmon power attributes are in microWatt */
	*val = (long)msg.mb_in_out * 1000;
	return ret;
}

static int sbrmi_write(struct device *dev, enum hwmon_sensor_types type,
		       u32 attr, int channel, long val)
{
	struct sbrmi_data *data = dev_get_drvdata(dev);
	struct apml_mbox_msg msg = { 0 };

	if (!data)
		return -ENODEV;

	if (type != hwmon_power && attr != hwmon_power_cap)
		return -EINVAL;
	/*
	 * hwmon power attributes are in microWatt
	 * mailbox read/write is in mWatt
	 */
	val /= 1000;

	val = clamp_val(val, SBRMI_PWR_MIN, data->pwr_limit_max);

	msg.cmd = SBRMI_WRITE_PKG_PWR_LIMIT;
	msg.mb_in_out = val;

	return rmi_mailbox_xfer(data, &msg);
}

static umode_t sbrmi_is_visible(const void *data,
				enum hwmon_sensor_types type,
				u32 attr, int channel)
{
	switch (type) {
	case hwmon_power:
		switch (attr) {
		case hwmon_power_input:
		case hwmon_power_cap_max:
			return 0444;
		case hwmon_power_cap:
			return 0644;
		}
		break;
	default:
		break;
	}
	return 0;
}

static const struct hwmon_channel_info * const sbrmi_info[] = {
	HWMON_CHANNEL_INFO(power,
			   HWMON_P_INPUT | HWMON_P_CAP | HWMON_P_CAP_MAX),
	NULL
};

static const struct hwmon_ops sbrmi_hwmon_ops = {
	.is_visible = sbrmi_is_visible,
	.read = sbrmi_read,
	.write = sbrmi_write,
};

static const struct hwmon_chip_info sbrmi_chip_info = {
	.ops = &sbrmi_hwmon_ops,
	.info = sbrmi_info,
};

int create_hwmon_sensor_device(struct device *dev, struct sbrmi_data *data)
{
	struct device *hwmon_dev;

	hwmon_dev = devm_hwmon_device_register_with_info(dev, "sbrmi", data,
							 &sbrmi_chip_info, NULL);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}
