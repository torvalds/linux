// SPDX-License-Identifier: GPL-2.0
/*
 * BMI160 - Bosch IMU, I2C bits
 *
 * Copyright (c) 2016, Intel Corporation.
 *
 * 7-bit I2C slave address is:
 *      - 0x68 if SDO is pulled to GND
 *      - 0x69 if SDO is pulled to VDDIO
 */
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/regmap.h>

#include "bmi160.h"

static int bmi160_i2c_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct regmap *regmap;
	const char *name;

	regmap = devm_regmap_init_i2c(client, &bmi160_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Failed to register i2c regmap: %pe\n",
			regmap);
		return PTR_ERR(regmap);
	}

	if (id)
		name = id->name;
	else
		name = dev_name(&client->dev);

	return bmi160_core_probe(&client->dev, regmap, name, false);
}

static const struct i2c_device_id bmi160_i2c_id[] = {
	{ "bmi120" },
	{ "bmi160" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bmi160_i2c_id);

static const struct acpi_device_id bmi160_acpi_match[] = {
	/*
	 * FIRMWARE BUG WORKAROUND
	 * Some manufacturers like GPD, Lenovo or Aya used the incorrect
	 * ID "10EC5280" for bmi160 in their DSDT. A fixed firmware is not
	 * available as of Feb 2024 after trying to work with OEMs, and
	 * this is not expected to change anymore since at least some of
	 * the affected devices are from 2021/2022.
	 */
	{"10EC5280", 0},
	{"BMI0120", 0},
	{"BMI0160", 0},
	{ }
};
MODULE_DEVICE_TABLE(acpi, bmi160_acpi_match);

static const struct of_device_id bmi160_of_match[] = {
	{ .compatible = "bosch,bmi120" },
	{ .compatible = "bosch,bmi160" },
	{ }
};
MODULE_DEVICE_TABLE(of, bmi160_of_match);

static struct i2c_driver bmi160_i2c_driver = {
	.driver = {
		.name			= "bmi160_i2c",
		.pm			= pm_ptr(&bmi160_core_pm_ops),
		.acpi_match_table	= bmi160_acpi_match,
		.of_match_table		= bmi160_of_match,
	},
	.probe		= bmi160_i2c_probe,
	.id_table	= bmi160_i2c_id,
};
module_i2c_driver(bmi160_i2c_driver);

MODULE_AUTHOR("Daniel Baluta <daniel.baluta@intel.com>");
MODULE_DESCRIPTION("BMI160 I2C driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("IIO_BMI160");
