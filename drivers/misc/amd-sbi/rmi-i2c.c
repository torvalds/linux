// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * rmi-i2c.c - Side band RMI over I2C support for AMD out
 *             of band management
 *
 * Copyright (C) 2024 Advanced Micro Devices, Inc.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include "rmi-core.h"

/* Do not allow setting negative power limit */
#define SBRMI_PWR_MIN	0

static int sbrmi_enable_alert(struct i2c_client *client)
{
	int ctrl;

	/*
	 * Enable the SB-RMI Software alert status
	 * by writing 0 to bit 4 of Control register(0x1)
	 */
	ctrl = i2c_smbus_read_byte_data(client, SBRMI_CTRL);
	if (ctrl < 0)
		return ctrl;

	if (ctrl & 0x10) {
		ctrl &= ~0x10;
		return i2c_smbus_write_byte_data(client,
						 SBRMI_CTRL, ctrl);
	}

	return 0;
}

static int sbrmi_read(struct device *dev, enum hwmon_sensor_types type,
		      u32 attr, int channel, long *val)
{
	struct sbrmi_data *data = dev_get_drvdata(dev);
	struct sbrmi_mailbox_msg msg = { 0 };
	int ret;

	if (type != hwmon_power)
		return -EINVAL;

	msg.read = true;
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
		msg.data_out = data->pwr_limit_max;
		ret = 0;
		break;
	default:
		return -EINVAL;
	}
	if (ret < 0)
		return ret;
	/* hwmon power attributes are in microWatt */
	*val = (long)msg.data_out * 1000;
	return ret;
}

static int sbrmi_write(struct device *dev, enum hwmon_sensor_types type,
		       u32 attr, int channel, long val)
{
	struct sbrmi_data *data = dev_get_drvdata(dev);
	struct sbrmi_mailbox_msg msg = { 0 };

	if (type != hwmon_power && attr != hwmon_power_cap)
		return -EINVAL;
	/*
	 * hwmon power attributes are in microWatt
	 * mailbox read/write is in mWatt
	 */
	val /= 1000;

	val = clamp_val(val, SBRMI_PWR_MIN, data->pwr_limit_max);

	msg.cmd = SBRMI_WRITE_PKG_PWR_LIMIT;
	msg.data_in = val;
	msg.read = false;

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

static int sbrmi_get_max_pwr_limit(struct sbrmi_data *data)
{
	struct sbrmi_mailbox_msg msg = { 0 };
	int ret;

	msg.cmd = SBRMI_READ_PKG_MAX_PWR_LIMIT;
	msg.read = true;
	ret = rmi_mailbox_xfer(data, &msg);
	if (ret < 0)
		return ret;
	data->pwr_limit_max = msg.data_out;

	return ret;
}

static int sbrmi_i2c_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct sbrmi_data *data;
	int ret;

	data = devm_kzalloc(dev, sizeof(struct sbrmi_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	mutex_init(&data->lock);

	/* Enable alert for SB-RMI sequence */
	ret = sbrmi_enable_alert(client);
	if (ret < 0)
		return ret;

	/* Cache maximum power limit */
	ret = sbrmi_get_max_pwr_limit(data);
	if (ret < 0)
		return ret;

	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name, data,
							 &sbrmi_chip_info, NULL);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id sbrmi_id[] = {
	{"sbrmi-i2c"},
	{}
};
MODULE_DEVICE_TABLE(i2c, sbrmi_id);

static const struct of_device_id __maybe_unused sbrmi_of_match[] = {
	{
		.compatible = "amd,sbrmi",
	},
	{ },
};
MODULE_DEVICE_TABLE(of, sbrmi_of_match);

static struct i2c_driver sbrmi_driver = {
	.driver = {
		.name = "sbrmi-i2c",
		.of_match_table = of_match_ptr(sbrmi_of_match),
	},
	.probe = sbrmi_i2c_probe,
	.id_table = sbrmi_id,
};

module_i2c_driver(sbrmi_driver);

MODULE_AUTHOR("Akshay Gupta <akshay.gupta@amd.com>");
MODULE_AUTHOR("Naveen Krishna Chatradhi <naveenkrishna.chatradhi@amd.com>");
MODULE_DESCRIPTION("Hwmon driver for AMD SB-RMI emulated sensor");
MODULE_LICENSE("GPL");
