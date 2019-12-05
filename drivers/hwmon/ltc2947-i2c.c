// SPDX-License-Identifier: GPL-2.0
/*
 * Analog Devices LTC2947 high precision power and energy monitor over I2C
 *
 * Copyright 2019 Analog Devices Inc.
 */
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "ltc2947.h"

static const struct regmap_config ltc2947_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int ltc2947_probe(struct i2c_client *i2c,
			 const struct i2c_device_id *id)
{
	struct regmap *map;

	map = devm_regmap_init_i2c(i2c, &ltc2947_regmap_config);
	if (IS_ERR(map))
		return PTR_ERR(map);

	return ltc2947_core_probe(map, i2c->name);
}

static const struct i2c_device_id ltc2947_id[] = {
	{"ltc2947", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, ltc2947_id);

static struct i2c_driver ltc2947_driver = {
	.driver = {
		.name = "ltc2947",
		.of_match_table = ltc2947_of_match,
		.pm = &ltc2947_pm_ops,
	},
	.probe = ltc2947_probe,
	.id_table = ltc2947_id,
};
module_i2c_driver(ltc2947_driver);

MODULE_AUTHOR("Nuno Sa <nuno.sa@analog.com>");
MODULE_DESCRIPTION("LTC2947 I2C power and energy monitor driver");
MODULE_LICENSE("GPL");
