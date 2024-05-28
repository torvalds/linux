// SPDX-License-Identifier: GPL-2.0
/*
 * FXOS8700 - NXP IMU, I2C bits
 *
 * 7-bit I2C slave address determined by SA1 and SA0 logic level
 * inputs represented in the following table:
 *      SA1  |  SA0  |  Slave Address
 *      0    |  0    |  0x1E
 *      0    |  1    |  0x1D
 *      1    |  0    |  0x1C
 *      1    |  1    |  0x1F
 */
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/regmap.h>

#include "fxos8700.h"

static int fxos8700_i2c_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct regmap *regmap;
	const char *name = NULL;

	regmap = devm_regmap_init_i2c(client, &fxos8700_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Failed to register i2c regmap %ld\n", PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	if (id)
		name = id->name;

	return fxos8700_core_probe(&client->dev, regmap, name, false);
}

static const struct i2c_device_id fxos8700_i2c_id[] = {
	{"fxos8700", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, fxos8700_i2c_id);

static const struct acpi_device_id fxos8700_acpi_match[] = {
	{"FXOS8700", 0},
	{ }
};
MODULE_DEVICE_TABLE(acpi, fxos8700_acpi_match);

static const struct of_device_id fxos8700_of_match[] = {
	{ .compatible = "nxp,fxos8700" },
	{ }
};
MODULE_DEVICE_TABLE(of, fxos8700_of_match);

static struct i2c_driver fxos8700_i2c_driver = {
	.driver = {
		.name                   = "fxos8700_i2c",
		.acpi_match_table       = fxos8700_acpi_match,
		.of_match_table         = fxos8700_of_match,
	},
	.probe          = fxos8700_i2c_probe,
	.id_table       = fxos8700_i2c_id,
};
module_i2c_driver(fxos8700_i2c_driver);

MODULE_AUTHOR("Robert Jones <rjones@gateworks.com>");
MODULE_DESCRIPTION("FXOS8700 I2C driver");
MODULE_LICENSE("GPL v2");
