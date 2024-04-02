// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics hts221 i2c driver
 *
 * Copyright 2016 STMicroelectronics Inc.
 *
 * Lorenzo Bianconi <lorenzo.bianconi@st.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/regmap.h>

#include "hts221.h"

#define HTS221_I2C_AUTO_INCREMENT	BIT(7)

static const struct regmap_config hts221_i2c_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.write_flag_mask = HTS221_I2C_AUTO_INCREMENT,
	.read_flag_mask = HTS221_I2C_AUTO_INCREMENT,
};

static int hts221_i2c_probe(struct i2c_client *client)
{
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(client, &hts221_i2c_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Failed to register i2c regmap %ld\n",
			PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	return hts221_probe(&client->dev, client->irq,
			    client->name, regmap);
}

static const struct acpi_device_id hts221_acpi_match[] = {
	{"SMO9100", 0},
	{ },
};
MODULE_DEVICE_TABLE(acpi, hts221_acpi_match);

static const struct of_device_id hts221_i2c_of_match[] = {
	{ .compatible = "st,hts221", },
	{},
};
MODULE_DEVICE_TABLE(of, hts221_i2c_of_match);

static const struct i2c_device_id hts221_i2c_id_table[] = {
	{ HTS221_DEV_NAME },
	{},
};
MODULE_DEVICE_TABLE(i2c, hts221_i2c_id_table);

static struct i2c_driver hts221_driver = {
	.driver = {
		.name = "hts221_i2c",
		.pm = pm_sleep_ptr(&hts221_pm_ops),
		.of_match_table = hts221_i2c_of_match,
		.acpi_match_table = hts221_acpi_match,
	},
	.probe = hts221_i2c_probe,
	.id_table = hts221_i2c_id_table,
};
module_i2c_driver(hts221_driver);

MODULE_AUTHOR("Lorenzo Bianconi <lorenzo.bianconi@st.com>");
MODULE_DESCRIPTION("STMicroelectronics hts221 i2c driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(IIO_HTS221);
