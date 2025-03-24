// SPDX-License-Identifier: GPL-2.0+
/*
 * Surface Fan driver for Surface System Aggregator Module. It provides access
 * to the fan's rpm through the hwmon system.
 *
 * Copyright (C) 2023 Ivor Wanders <ivor@iwanders.net>
 */

#include <linux/hwmon.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/surface_aggregator/device.h>
#include <linux/types.h>

// SSAM
SSAM_DEFINE_SYNC_REQUEST_CL_R(__ssam_fan_rpm_get, __le16, {
	.target_category = SSAM_SSH_TC_FAN,
	.command_id      = 0x01,
});

static int surface_fan_hwmon_read(struct device *dev,
				  enum hwmon_sensor_types type, u32 attr,
				  int channel, long *val)
{
	struct ssam_device *sdev = dev_get_drvdata(dev);
	int ret;
	__le16 value;

	ret = __ssam_fan_rpm_get(sdev, &value);
	if (ret)
		return ret;

	*val = le16_to_cpu(value);

	return 0;
}

static const struct hwmon_channel_info *const surface_fan_info[] = {
	HWMON_CHANNEL_INFO(fan, HWMON_F_INPUT),
	NULL
};

static const struct hwmon_ops surface_fan_hwmon_ops = {
	.visible = 0444,
	.read = surface_fan_hwmon_read,
};

static const struct hwmon_chip_info surface_fan_chip_info = {
	.ops = &surface_fan_hwmon_ops,
	.info = surface_fan_info,
};

static int surface_fan_probe(struct ssam_device *sdev)
{
	struct device *hdev;

	hdev = devm_hwmon_device_register_with_info(&sdev->dev,
						    "surface_fan", sdev,
						    &surface_fan_chip_info,
						    NULL);

	return PTR_ERR_OR_ZERO(hdev);
}

static const struct ssam_device_id ssam_fan_match[] = {
	{ SSAM_SDEV(FAN, SAM, 0x01, 0x01) },
	{},
};
MODULE_DEVICE_TABLE(ssam, ssam_fan_match);

static struct ssam_device_driver surface_fan = {
	.probe = surface_fan_probe,
	.match_table = ssam_fan_match,
	.driver = {
		.name = "surface_fan",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};
module_ssam_device_driver(surface_fan);

MODULE_AUTHOR("Ivor Wanders <ivor@iwanders.net>");
MODULE_DESCRIPTION("Fan Driver for Surface System Aggregator Module");
MODULE_LICENSE("GPL");
