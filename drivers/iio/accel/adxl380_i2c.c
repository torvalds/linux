// SPDX-License-Identifier: GPL-2.0+
/*
 * ADXL380 3-Axis Digital Accelerometer I2C driver
 *
 * Copyright 2024 Analog Devices Inc.
 */

#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "adxl380.h"

static const struct regmap_config adxl380_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.readable_noinc_reg = adxl380_readable_noinc_reg,
};

static int adxl380_i2c_probe(struct i2c_client *client)
{
	struct regmap *regmap;
	const struct adxl380_chip_info *chip_data;

	chip_data = i2c_get_match_data(client);

	regmap = devm_regmap_init_i2c(client, &adxl380_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return adxl380_probe(&client->dev, regmap, chip_data);
}

static const struct i2c_device_id adxl380_i2c_id[] = {
	{ "adxl380", (kernel_ulong_t)&adxl380_chip_info },
	{ "adxl382", (kernel_ulong_t)&adxl382_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adxl380_i2c_id);

static const struct of_device_id adxl380_of_match[] = {
	{ .compatible = "adi,adxl380", .data = &adxl380_chip_info },
	{ .compatible = "adi,adxl382", .data = &adxl382_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(of, adxl380_of_match);

static struct i2c_driver adxl380_i2c_driver = {
	.driver = {
		.name = "adxl380_i2c",
		.of_match_table = adxl380_of_match,
	},
	.probe = adxl380_i2c_probe,
	.id_table = adxl380_i2c_id,
};

module_i2c_driver(adxl380_i2c_driver);

MODULE_AUTHOR("Ramona Gradinariu <ramona.gradinariu@analog.com>");
MODULE_AUTHOR("Antoniu Miclaus <antoniu.miclaus@analog.com>");
MODULE_DESCRIPTION("Analog Devices ADXL380 3-axis accelerometer I2C driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(IIO_ADXL380);
