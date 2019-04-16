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
#include <linux/acpi.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "bme680.h"

static int bme680_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct regmap *regmap;
	const char *name = NULL;
	unsigned int val;
	int ret;

	regmap = devm_regmap_init_i2c(client, &bme680_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Failed to register i2c regmap %d\n",
				(int)PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	ret = regmap_write(regmap, BME680_REG_SOFT_RESET_I2C,
			   BME680_CMD_SOFTRESET);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to reset chip\n");
		return ret;
	}

	ret = regmap_read(regmap, BME680_REG_CHIP_I2C_ID, &val);
	if (ret < 0) {
		dev_err(&client->dev, "Error reading I2C chip ID\n");
		return ret;
	}

	if (val != BME680_CHIP_ID_VAL) {
		dev_err(&client->dev, "Wrong chip ID, got %x expected %x\n",
				val, BME680_CHIP_ID_VAL);
		return -ENODEV;
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

static const struct acpi_device_id bme680_acpi_match[] = {
	{"BME0680", 0},
	{},
};
MODULE_DEVICE_TABLE(acpi, bme680_acpi_match);

static const struct of_device_id bme680_of_i2c_match[] = {
	{ .compatible = "bosch,bme680", },
	{},
};
MODULE_DEVICE_TABLE(of, bme680_of_i2c_match);

static struct i2c_driver bme680_i2c_driver = {
	.driver = {
		.name			= "bme680_i2c",
		.acpi_match_table       = ACPI_PTR(bme680_acpi_match),
		.of_match_table		= bme680_of_i2c_match,
	},
	.probe = bme680_i2c_probe,
	.id_table = bme680_i2c_id,
};
module_i2c_driver(bme680_i2c_driver);

MODULE_AUTHOR("Himanshu Jha <himanshujha199640@gmail.com>");
MODULE_DESCRIPTION("BME680 I2C driver");
MODULE_LICENSE("GPL v2");
