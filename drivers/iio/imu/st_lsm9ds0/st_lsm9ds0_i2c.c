// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics LSM9DS0 IMU driver
 *
 * Copyright (C) 2021, Intel Corporation
 *
 * Author: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 */

#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <linux/iio/common/st_sensors_i2c.h>

#include "st_lsm9ds0.h"

static const struct of_device_id st_lsm9ds0_of_match[] = {
	{
		.compatible = "st,lsm9ds0-imu",
		.data = LSM9DS0_IMU_DEV_NAME,
	},
	{}
};
MODULE_DEVICE_TABLE(of, st_lsm9ds0_of_match);

static const struct i2c_device_id st_lsm9ds0_id_table[] = {
	{ LSM9DS0_IMU_DEV_NAME },
	{}
};
MODULE_DEVICE_TABLE(i2c, st_lsm9ds0_id_table);

static const struct regmap_config st_lsm9ds0_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.read_flag_mask	= 0x80,
};

static int st_lsm9ds0_i2c_probe(struct i2c_client *client)
{
	const struct regmap_config *config = &st_lsm9ds0_regmap_config;
	struct device *dev = &client->dev;
	struct st_lsm9ds0 *lsm9ds0;
	struct regmap *regmap;

	st_sensors_dev_name_probe(dev, client->name, sizeof(client->name));

	lsm9ds0 = devm_kzalloc(dev, sizeof(*lsm9ds0), GFP_KERNEL);
	if (!lsm9ds0)
		return -ENOMEM;

	lsm9ds0->dev = dev;
	lsm9ds0->name = client->name;
	lsm9ds0->irq = client->irq;

	regmap = devm_regmap_init_i2c(client, config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	i2c_set_clientdata(client, lsm9ds0);

	return st_lsm9ds0_probe(lsm9ds0, regmap);
}

static int st_lsm9ds0_i2c_remove(struct i2c_client *client)
{
	return st_lsm9ds0_remove(i2c_get_clientdata(client));
}

static struct i2c_driver st_lsm9ds0_driver = {
	.driver = {
		.name = "st-lsm9ds0-i2c",
		.of_match_table = st_lsm9ds0_of_match,
	},
	.probe_new = st_lsm9ds0_i2c_probe,
	.remove = st_lsm9ds0_i2c_remove,
	.id_table = st_lsm9ds0_id_table,
};
module_i2c_driver(st_lsm9ds0_driver);

MODULE_AUTHOR("Andy Shevchenko <andriy.shevchenko@linux.intel.com>");
MODULE_DESCRIPTION("STMicroelectronics LSM9DS0 IMU I2C driver");
MODULE_LICENSE("GPL v2");
