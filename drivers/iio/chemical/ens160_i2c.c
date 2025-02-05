// SPDX-License-Identifier: GPL-2.0
/*
 * ScioSense ENS160 multi-gas sensor I2C driver
 *
 * Copyright (c) 2024 Gustavo Silva <gustavograzs@gmail.com>
 *
 * 7-Bit I2C slave address is:
 *	- 0x52 if ADDR pin LOW
 *	- 0x53 if ADDR pin HIGH
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "ens160.h"

static const struct regmap_config ens160_regmap_i2c_conf = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int ens160_i2c_probe(struct i2c_client *client)
{
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(client, &ens160_regmap_i2c_conf);
	if (IS_ERR(regmap))
		return dev_err_probe(&client->dev, PTR_ERR(regmap),
				     "Failed to register i2c regmap\n");

	return devm_ens160_core_probe(&client->dev, regmap, client->irq,
				      "ens160");
}

static const struct i2c_device_id ens160_i2c_id[] = {
	{ "ens160" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ens160_i2c_id);

static const struct of_device_id ens160_of_i2c_match[] = {
	{ .compatible = "sciosense,ens160" },
	{ }
};
MODULE_DEVICE_TABLE(of, ens160_of_i2c_match);

static struct i2c_driver ens160_i2c_driver = {
	.driver = {
		.name		= "ens160",
		.of_match_table	= ens160_of_i2c_match,
		.pm		= pm_sleep_ptr(&ens160_pm_ops),
	},
	.probe = ens160_i2c_probe,
	.id_table = ens160_i2c_id,
};
module_i2c_driver(ens160_i2c_driver);

MODULE_AUTHOR("Gustavo Silva <gustavograzs@gmail.com>");
MODULE_DESCRIPTION("ScioSense ENS160 I2C driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("IIO_ENS160");
