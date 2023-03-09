// SPDX-License-Identifier: GPL-2.0-only
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/acpi.h>

#include "bmg160.h"

static const struct regmap_config bmg160_regmap_i2c_conf = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x3f
};

static int bmg160_i2c_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct regmap *regmap;
	const char *name = NULL;

	regmap = devm_regmap_init_i2c(client, &bmg160_regmap_i2c_conf);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Failed to register i2c regmap: %pe\n",
			regmap);
		return PTR_ERR(regmap);
	}

	if (id)
		name = id->name;

	return bmg160_core_probe(&client->dev, regmap, client->irq, name);
}

static void bmg160_i2c_remove(struct i2c_client *client)
{
	bmg160_core_remove(&client->dev);
}

static const struct acpi_device_id bmg160_acpi_match[] = {
	{"BMG0160", 0},
	{"BMI055B", 0},
	{"BMI088B", 0},
	{},
};

MODULE_DEVICE_TABLE(acpi, bmg160_acpi_match);

static const struct i2c_device_id bmg160_i2c_id[] = {
	{"bmg160", 0},
	{"bmi055_gyro", 0},
	{"bmi088_gyro", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, bmg160_i2c_id);

static const struct of_device_id bmg160_of_match[] = {
	{ .compatible = "bosch,bmg160" },
	{ .compatible = "bosch,bmi055_gyro" },
	{ }
};

MODULE_DEVICE_TABLE(of, bmg160_of_match);

static struct i2c_driver bmg160_i2c_driver = {
	.driver = {
		.name	= "bmg160_i2c",
		.acpi_match_table = ACPI_PTR(bmg160_acpi_match),
		.of_match_table = bmg160_of_match,
		.pm	= &bmg160_pm_ops,
	},
	.probe_new	= bmg160_i2c_probe,
	.remove		= bmg160_i2c_remove,
	.id_table	= bmg160_i2c_id,
};
module_i2c_driver(bmg160_i2c_driver);

MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("BMG160 I2C Gyro driver");
