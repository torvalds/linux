// SPDX-License-Identifier: GPL-2.0
/*
 * BME680 - I2C Driver
 *
 * Copyright (C) 2018 Himanshu Jha <himanshujha199640@gmail.com>
 *
 * 7-Bit I2C slave address is:
 *	- 0x76 if SDO is pulled to GND
 *	- 0x77 if SDO is pulled to VDDIO
 *
 * Note: SDO pin cannot be left floating otherwise I2C address
 *	 will be undefined.
 */
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "bme680.h"

static int bme680_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct regmap *regmap;
	const char *name = NULL;

	regmap = devm_regmap_init_i2c(client, &bme680_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Failed to register i2c regmap %ld\n", PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	if (id)
		name = id->name;

	return bme680_core_probe(&client->dev, regmap, name);
}

static const struct i2c_device_id bme680_i2c_id[] = {
	{"bme680", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, bme680_i2c_id);

static const struct of_device_id bme680_of_i2c_match[] = {
	{ .compatible = "bosch,bme680", },
	{},
};
MODULE_DEVICE_TABLE(of, bme680_of_i2c_match);

static struct i2c_driver bme680_i2c_driver = {
	.driver = {
		.name			= "bme680_i2c",
		.of_match_table		= bme680_of_i2c_match,
	},
	.probe = bme680_i2c_probe,
	.id_table = bme680_i2c_id,
};
module_i2c_driver(bme680_i2c_driver);

MODULE_AUTHOR("Himanshu Jha <himanshujha199640@gmail.com>");
MODULE_DESCRIPTION("BME680 I2C driver");
MODULE_LICENSE("GPL v2");
