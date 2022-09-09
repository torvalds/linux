// SPDX-License-Identifier: GPL-2.0-only
/*
 * ADXL313 3-Axis Digital Accelerometer
 *
 * Copyright (c) 2021 Lucas Stankus <lucas.p.stankus@gmail.com>
 *
 * Datasheet: https://www.analog.com/media/en/technical-documentation/data-sheets/ADXL313.pdf
 */

#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "adxl313.h"

static const struct regmap_config adxl313_i2c_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.rd_table	= &adxl313_readable_regs_table,
	.wr_table	= &adxl313_writable_regs_table,
	.max_register	= 0x39,
};

static int adxl313_i2c_probe(struct i2c_client *client)
{
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(client, &adxl313_i2c_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Error initializing i2c regmap: %ld\n",
			PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	return adxl313_core_probe(&client->dev, regmap, client->name, NULL);
}

static const struct i2c_device_id adxl313_i2c_id[] = {
	{ "adxl313" },
	{ }
};

MODULE_DEVICE_TABLE(i2c, adxl313_i2c_id);

static const struct of_device_id adxl313_of_match[] = {
	{ .compatible = "adi,adxl313" },
	{ }
};

MODULE_DEVICE_TABLE(of, adxl313_of_match);

static struct i2c_driver adxl313_i2c_driver = {
	.driver = {
		.name	= "adxl313_i2c",
		.of_match_table = adxl313_of_match,
	},
	.probe_new	= adxl313_i2c_probe,
	.id_table	= adxl313_i2c_id,
};

module_i2c_driver(adxl313_i2c_driver);

MODULE_AUTHOR("Lucas Stankus <lucas.p.stankus@gmail.com>");
MODULE_DESCRIPTION("ADXL313 3-Axis Digital Accelerometer I2C driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(IIO_ADXL313);
