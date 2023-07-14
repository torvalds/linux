// SPDX-License-Identifier: GPL-2.0-only
/*
 * I2C IIO driver for Bosch BMA400 triaxial acceleration sensor.
 *
 * Copyright 2019 Dan Robertson <dan@dlrobertson.com>
 *
 * I2C address is either 0x14 or 0x15 depending on SDO
 */
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "bma400.h"

static int bma400_i2c_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(client, &bma400_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "failed to create regmap\n");
		return PTR_ERR(regmap);
	}

	return bma400_probe(&client->dev, regmap, client->irq, id->name);
}

static const struct i2c_device_id bma400_i2c_ids[] = {
	{ "bma400", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bma400_i2c_ids);

static const struct of_device_id bma400_of_i2c_match[] = {
	{ .compatible = "bosch,bma400" },
	{ }
};
MODULE_DEVICE_TABLE(of, bma400_of_i2c_match);

static struct i2c_driver bma400_i2c_driver = {
	.driver = {
		.name = "bma400",
		.of_match_table = bma400_of_i2c_match,
	},
	.probe = bma400_i2c_probe,
	.id_table = bma400_i2c_ids,
};

module_i2c_driver(bma400_i2c_driver);

MODULE_AUTHOR("Dan Robertson <dan@dlrobertson.com>");
MODULE_DESCRIPTION("Bosch BMA400 triaxial acceleration sensor (I2C)");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(IIO_BMA400);
