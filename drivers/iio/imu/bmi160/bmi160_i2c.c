/*
 * BMI160 - Bosch IMU, I2C bits
 *
 * Copyright (c) 2016, Intel Corporation.
 *
 * This file is subject to the terms and conditions of version 2 of
 * the GNU General Public License.  See the file COPYING in the main
 * directory of this archive for more details.
 *
 * 7-bit I2C slave address is:
 *      - 0x68 if SDO is pulled to GND
 *      - 0x69 if SDO is pulled to VDDIO
 */
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/acpi.h>

#include "bmi160.h"

static int bmi160_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct regmap *regmap;
	const char *name = NULL;

	regmap = devm_regmap_init_i2c(client, &bmi160_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Failed to register i2c regmap %d\n",
			(int)PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	if (id)
		name = id->name;

	return bmi160_core_probe(&client->dev, regmap, name, false);
}

static int bmi160_i2c_remove(struct i2c_client *client)
{
	bmi160_core_remove(&client->dev);

	return 0;
}

static const struct i2c_device_id bmi160_i2c_id[] = {
	{"bmi160", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, bmi160_i2c_id);

static const struct acpi_device_id bmi160_acpi_match[] = {
	{"BMI0160", 0},
	{ },
};
MODULE_DEVICE_TABLE(acpi, bmi160_acpi_match);

static struct i2c_driver bmi160_i2c_driver = {
	.driver = {
		.name			= "bmi160_i2c",
		.acpi_match_table	= ACPI_PTR(bmi160_acpi_match),
	},
	.probe		= bmi160_i2c_probe,
	.remove		= bmi160_i2c_remove,
	.id_table	= bmi160_i2c_id,
};
module_i2c_driver(bmi160_i2c_driver);

MODULE_AUTHOR("Daniel Baluta <daniel.baluta@intel.com>");
MODULE_DESCRIPTION("BMI160 I2C driver");
MODULE_LICENSE("GPL v2");
