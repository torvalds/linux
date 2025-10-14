// SPDX-License-Identifier: GPL-2.0-only
/*
 * Bosch triaxial acceleration sensor
 *
 * Copyright (c) 2025 Petre Rodan <petre.rodan@subdimension.ro>
 *
 * Datasheet: https://media.digikey.com/pdf/Data%20Sheets/Bosch/BMA220.pdf
 * I2C address is either 0x0b or 0x0a depending on CSB (pin 10)
 */

#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/types.h>

#include "bma220.h"

static int bma220_i2c_probe(struct i2c_client *client)
{
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(client, &bma220_i2c_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(&client->dev, PTR_ERR(regmap),
				     "failed to create regmap\n");

	return bma220_common_probe(&client->dev, regmap, client->irq);
}

static const struct of_device_id bma220_i2c_match[] = {
	{ .compatible = "bosch,bma220" },
	{ }
};
MODULE_DEVICE_TABLE(of, bma220_i2c_match);

static const struct i2c_device_id bma220_i2c_id[] = {
	{ "bma220" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bma220_i2c_id);

static struct i2c_driver bma220_i2c_driver = {
	.driver = {
		.name = "bma220_i2c",
		.pm = pm_sleep_ptr(&bma220_pm_ops),
		.of_match_table = bma220_i2c_match,
	},
	.probe = bma220_i2c_probe,
	.id_table = bma220_i2c_id,
};
module_i2c_driver(bma220_i2c_driver);

MODULE_AUTHOR("Petre Rodan <petre.rodan@subdimension.ro>");
MODULE_DESCRIPTION("Bosch triaxial acceleration sensor i2c driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_BOSCH_BMA220");
