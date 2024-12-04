// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for NXP FXAS21002C Gyroscope - I2C
 *
 * Copyright (C) 2018 Linaro Ltd.
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "fxas21002c.h"

static const struct regmap_config fxas21002c_regmap_i2c_conf = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = FXAS21002C_REG_CTRL3,
};

static int fxas21002c_i2c_probe(struct i2c_client *i2c)
{
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(i2c, &fxas21002c_regmap_i2c_conf);
	if (IS_ERR(regmap)) {
		dev_err(&i2c->dev, "Failed to register i2c regmap: %ld\n",
			PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	return fxas21002c_core_probe(&i2c->dev, regmap, i2c->irq, i2c->name);
}

static void fxas21002c_i2c_remove(struct i2c_client *i2c)
{
	fxas21002c_core_remove(&i2c->dev);
}

static const struct i2c_device_id fxas21002c_i2c_id[] = {
	{ "fxas21002c" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, fxas21002c_i2c_id);

static const struct of_device_id fxas21002c_i2c_of_match[] = {
	{ .compatible = "nxp,fxas21002c", },
	{ }
};
MODULE_DEVICE_TABLE(of, fxas21002c_i2c_of_match);

static struct i2c_driver fxas21002c_i2c_driver = {
	.driver = {
		.name = "fxas21002c_i2c",
		.pm = pm_ptr(&fxas21002c_pm_ops),
		.of_match_table = fxas21002c_i2c_of_match,
	},
	.probe		= fxas21002c_i2c_probe,
	.remove		= fxas21002c_i2c_remove,
	.id_table	= fxas21002c_i2c_id,
};
module_i2c_driver(fxas21002c_i2c_driver);

MODULE_AUTHOR("Rui Miguel Silva <rui.silva@linaro.org>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("FXAS21002C I2C Gyro driver");
MODULE_IMPORT_NS("IIO_FXAS21002C");
