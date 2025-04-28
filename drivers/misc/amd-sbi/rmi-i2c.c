// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * rmi-i2c.c - Side band RMI over I2C support for AMD out
 *             of band management
 *
 * Copyright (C) 2024 Advanced Micro Devices, Inc.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include "rmi-core.h"

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

	dev_set_drvdata(dev, data);
	return create_hwmon_sensor_device(dev, data);
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
