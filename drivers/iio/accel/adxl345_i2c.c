// SPDX-License-Identifier: GPL-2.0-only
/*
 * ADXL345 3-Axis Digital Accelerometer I2C driver
 *
 * Copyright (c) 2017 Eva Rachel Retuya <eraretuya@gmail.com>
 *
 * 7-bit I2C slave address: 0x1D (ALT ADDRESS pin tied to VDDIO) or
 * 0x53 (ALT ADDRESS pin grounded)
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "adxl345.h"

static const struct regmap_config adxl345_i2c_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int adxl345_i2c_probe(struct i2c_client *client)
{
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(client, &adxl345_i2c_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(&client->dev, PTR_ERR(regmap), "Error initializing regmap\n");

	return adxl345_core_probe(&client->dev, regmap);
}

static const struct i2c_device_id adxl345_i2c_id[] = {
	{ "adxl345", ADXL345 },
	{ "adxl375", ADXL375 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adxl345_i2c_id);

static const struct of_device_id adxl345_of_match[] = {
	{ .compatible = "adi,adxl345", .data = (const void *)ADXL345 },
	{ .compatible = "adi,adxl375", .data = (const void *)ADXL375 },
	{ }
};
MODULE_DEVICE_TABLE(of, adxl345_of_match);

static const struct acpi_device_id adxl345_acpi_match[] = {
	{ "ADS0345", ADXL345 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, adxl345_acpi_match);

static struct i2c_driver adxl345_i2c_driver = {
	.driver = {
		.name	= "adxl345_i2c",
		.of_match_table = adxl345_of_match,
		.acpi_match_table = adxl345_acpi_match,
	},
	.probe_new	= adxl345_i2c_probe,
	.id_table	= adxl345_i2c_id,
};
module_i2c_driver(adxl345_i2c_driver);

MODULE_AUTHOR("Eva Rachel Retuya <eraretuya@gmail.com>");
MODULE_DESCRIPTION("ADXL345 3-Axis Digital Accelerometer I2C driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(IIO_ADXL345);
