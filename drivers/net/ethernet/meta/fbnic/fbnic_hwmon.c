// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/hwmon.h>

#include "fbnic.h"
#include "fbnic_mac.h"

static int fbnic_hwmon_sensor_id(enum hwmon_sensor_types type)
{
	if (type == hwmon_temp)
		return FBNIC_SENSOR_TEMP;
	if (type == hwmon_in)
		return FBNIC_SENSOR_VOLTAGE;

	return -EOPNOTSUPP;
}

static umode_t fbnic_hwmon_is_visible(const void *drvdata,
				      enum hwmon_sensor_types type,
				      u32 attr, int channel)
{
	if (type == hwmon_temp && attr == hwmon_temp_input)
		return 0444;
	if (type == hwmon_in && attr == hwmon_in_input)
		return 0444;

	return 0;
}

static int fbnic_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			    u32 attr, int channel, long *val)
{
	struct fbnic_dev *fbd = dev_get_drvdata(dev);
	const struct fbnic_mac *mac = fbd->mac;
	int id;

	id = fbnic_hwmon_sensor_id(type);
	return id < 0 ? id : mac->get_sensor(fbd, id, val);
}

static const struct hwmon_ops fbnic_hwmon_ops = {
	.is_visible = fbnic_hwmon_is_visible,
	.read = fbnic_hwmon_read,
};

static const struct hwmon_channel_info *fbnic_hwmon_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT),
	HWMON_CHANNEL_INFO(in, HWMON_I_INPUT),
	NULL
};

static const struct hwmon_chip_info fbnic_chip_info = {
	.ops = &fbnic_hwmon_ops,
	.info = fbnic_hwmon_info,
};

void fbnic_hwmon_register(struct fbnic_dev *fbd)
{
	if (!IS_REACHABLE(CONFIG_HWMON))
		return;

	fbd->hwmon = hwmon_device_register_with_info(fbd->dev, "fbnic",
						     fbd, &fbnic_chip_info,
						     NULL);
	if (IS_ERR(fbd->hwmon)) {
		dev_notice(fbd->dev,
			   "Failed to register hwmon device %pe\n",
			fbd->hwmon);
		fbd->hwmon = NULL;
	}
}

void fbnic_hwmon_unregister(struct fbnic_dev *fbd)
{
	if (!IS_REACHABLE(CONFIG_HWMON) || !fbd->hwmon)
		return;

	hwmon_device_unregister(fbd->hwmon);
	fbd->hwmon = NULL;
}
