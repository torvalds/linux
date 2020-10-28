// SPDX-License-Identifier: GPL-2.0+
/*
 * ADXL372 3-Axis Digital Accelerometer I2C driver
 *
 * Copyright 2018 Analog Devices Inc.
 */

#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "adxl372.h"

static const struct regmap_config adxl372_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.readable_noinc_reg = adxl372_readable_noinc_reg,
};

static int adxl372_i2c_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	struct regmap *regmap;
	unsigned int regval;
	int ret;

	regmap = devm_regmap_init_i2c(client, &adxl372_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	ret = regmap_read(regmap, ADXL372_REVID, &regval);
	if (ret < 0)
		return ret;

	/* Starting with the 3rd revision an I2C chip bug was fixed */
	if (regval < 3)
		dev_warn(&client->dev,
		"I2C might not work properly with other devices on the bus");

	return adxl372_probe(&client->dev, regmap, client->irq, id->name);
}

static const struct i2c_device_id adxl372_i2c_id[] = {
	{ "adxl372", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, adxl372_i2c_id);

static const struct of_device_id adxl372_of_match[] = {
	{ .compatible = "adi,adxl372" },
	{ }
};
MODULE_DEVICE_TABLE(of, adxl372_of_match);

static struct i2c_driver adxl372_i2c_driver = {
	.driver = {
		.name = "adxl372_i2c",
		.of_match_table = adxl372_of_match,
	},
	.probe = adxl372_i2c_probe,
	.id_table = adxl372_i2c_id,
};

module_i2c_driver(adxl372_i2c_driver);

MODULE_AUTHOR("Stefan Popa <stefan.popa@analog.com>");
MODULE_DESCRIPTION("Analog Devices ADXL372 3-axis accelerometer I2C driver");
MODULE_LICENSE("GPL");
