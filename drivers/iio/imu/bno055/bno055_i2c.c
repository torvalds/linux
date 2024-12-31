// SPDX-License-Identifier: GPL-2.0
/*
 * Support for I2C-interfaced Bosch BNO055 IMU.
 *
 * Copyright (C) 2021-2022 Istituto Italiano di Tecnologia
 * Electronic Design Laboratory
 * Written by Andrea Merello <andrea.merello@iit.it>
 */

#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "bno055.h"

#define BNO055_I2C_XFER_BURST_BREAK_THRESHOLD 3

static int bno055_i2c_probe(struct i2c_client *client)
{
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(client, &bno055_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(&client->dev, PTR_ERR(regmap),
				     "Unable to init register map");

	return bno055_probe(&client->dev, regmap,
			    BNO055_I2C_XFER_BURST_BREAK_THRESHOLD, true);
}

static const struct i2c_device_id bno055_i2c_id[] = {
	{ "bno055" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bno055_i2c_id);

static const struct of_device_id __maybe_unused bno055_i2c_of_match[] = {
	{ .compatible = "bosch,bno055" },
	{ }
};
MODULE_DEVICE_TABLE(of, bno055_i2c_of_match);

static struct i2c_driver bno055_driver = {
	.driver = {
		.name = "bno055-i2c",
		.of_match_table = bno055_i2c_of_match,
	},
	.probe = bno055_i2c_probe,
	.id_table = bno055_i2c_id,
};
module_i2c_driver(bno055_driver);

MODULE_AUTHOR("Andrea Merello");
MODULE_DESCRIPTION("Bosch BNO055 I2C interface");
MODULE_IMPORT_NS("IIO_BNO055");
MODULE_LICENSE("GPL");
