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
#include <linux/regmap.h>
#include "rmi-core.h"

static int sbrmi_enable_alert(struct sbrmi_data *data)
{
	int ctrl, ret;

	/*
	 * Enable the SB-RMI Software alert status
	 * by writing 0 to bit 4 of Control register(0x1)
	 */
	ret = regmap_read(data->regmap, SBRMI_CTRL, &ctrl);
	if (ret < 0)
		return ret;

	if (ctrl & 0x10) {
		ctrl &= ~0x10;
		return regmap_write(data->regmap, SBRMI_CTRL, ctrl);
	}

	return 0;
}

static int sbrmi_get_max_pwr_limit(struct sbrmi_data *data)
{
	struct apml_mbox_msg msg = { 0 };
	int ret;

	msg.cmd = SBRMI_READ_PKG_MAX_PWR_LIMIT;
	ret = rmi_mailbox_xfer(data, &msg);
	if (ret < 0)
		return ret;
	data->pwr_limit_max = msg.mb_in_out;

	return ret;
}

static int sbrmi_i2c_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct sbrmi_data *data;
	struct regmap_config sbrmi_i2c_regmap_config = {
		.reg_bits = 8,
		.val_bits = 8,
	};
	int ret;

	data = devm_kzalloc(dev, sizeof(struct sbrmi_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	mutex_init(&data->lock);

	data->regmap = devm_regmap_init_i2c(client, &sbrmi_i2c_regmap_config);
	if (IS_ERR(data->regmap))
		return PTR_ERR(data->regmap);

	/* Enable alert for SB-RMI sequence */
	ret = sbrmi_enable_alert(data);
	if (ret < 0)
		return ret;

	/* Cache maximum power limit */
	ret = sbrmi_get_max_pwr_limit(data);
	if (ret < 0)
		return ret;

	data->dev_static_addr = client->addr;
	dev_set_drvdata(dev, data);

	ret = create_hwmon_sensor_device(dev, data);
	if (ret < 0)
		return ret;
	return create_misc_rmi_device(data, dev);
}

static void sbrmi_i2c_remove(struct i2c_client *client)
{
	struct sbrmi_data *data = dev_get_drvdata(&client->dev);

	misc_deregister(&data->sbrmi_misc_dev);
	/* Assign fops and parent of misc dev to NULL */
	data->sbrmi_misc_dev.fops = NULL;
	data->sbrmi_misc_dev.parent = NULL;
	dev_info(&client->dev, "Removed sbrmi-i2c driver\n");
	return;
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
	.remove = sbrmi_i2c_remove,
	.id_table = sbrmi_id,
};

module_i2c_driver(sbrmi_driver);

MODULE_AUTHOR("Akshay Gupta <akshay.gupta@amd.com>");
MODULE_AUTHOR("Naveen Krishna Chatradhi <naveenkrishna.chatradhi@amd.com>");
MODULE_DESCRIPTION("Hwmon driver for AMD SB-RMI emulated sensor");
MODULE_LICENSE("GPL");
