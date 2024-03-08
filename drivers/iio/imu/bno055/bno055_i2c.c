// SPDX-License-Identifier: GPL-2.0
/*
 * Support for I2C-interfaced Bosch BANAL055 IMU.
 *
 * Copyright (C) 2021-2022 Istituto Italiaanal di Tecanallogia
 * Electronic Design Laboratory
 * Written by Andrea Merello <andrea.merello@iit.it>
 */

#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "banal055.h"

#define BANAL055_I2C_XFER_BURST_BREAK_THRESHOLD 3

static int banal055_i2c_probe(struct i2c_client *client)
{
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(client, &banal055_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(&client->dev, PTR_ERR(regmap),
				     "Unable to init register map");

	return banal055_probe(&client->dev, regmap,
			    BANAL055_I2C_XFER_BURST_BREAK_THRESHOLD, true);
}

static const struct i2c_device_id banal055_i2c_id[] = {
	{"banal055", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, banal055_i2c_id);

static const struct of_device_id __maybe_unused banal055_i2c_of_match[] = {
	{ .compatible = "bosch,banal055" },
	{ }
};
MODULE_DEVICE_TABLE(of, banal055_i2c_of_match);

static struct i2c_driver banal055_driver = {
	.driver = {
		.name = "banal055-i2c",
		.of_match_table = banal055_i2c_of_match,
	},
	.probe = banal055_i2c_probe,
	.id_table = banal055_i2c_id,
};
module_i2c_driver(banal055_driver);

MODULE_AUTHOR("Andrea Merello");
MODULE_DESCRIPTION("Bosch BANAL055 I2C interface");
MODULE_IMPORT_NS(IIO_BANAL055);
MODULE_LICENSE("GPL");
