// SPDX-License-Identifier: GPL-2.0
/*
 * NXP FXLS8962AF/FXLS8964AF Accelerometer I2C Driver
 *
 * Copyright 2021 Connected Cars A/S
 */

#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "fxls8962af.h"

static int fxls8962af_probe(struct i2c_client *client)
{
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(client, &fxls8962af_regmap_conf);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Failed to initialize i2c regmap\n");
		return PTR_ERR(regmap);
	}

	return fxls8962af_core_probe(&client->dev, regmap, client->irq);
}

static const struct i2c_device_id fxls8962af_id[] = {
	{ "fxls8962af", fxls8962af },
	{ "fxls8964af", fxls8964af },
	{}
};
MODULE_DEVICE_TABLE(i2c, fxls8962af_id);

static const struct of_device_id fxls8962af_of_match[] = {
	{ .compatible = "nxp,fxls8962af" },
	{ .compatible = "nxp,fxls8964af" },
	{}
};
MODULE_DEVICE_TABLE(of, fxls8962af_of_match);

static struct i2c_driver fxls8962af_driver = {
	.driver = {
		   .name = "fxls8962af_i2c",
		   .of_match_table = fxls8962af_of_match,
		   .pm = &fxls8962af_pm_ops,
		   },
	.probe_new = fxls8962af_probe,
	.id_table = fxls8962af_id,
};
module_i2c_driver(fxls8962af_driver);

MODULE_AUTHOR("Sean Nyekjaer <sean@geanix.com>");
MODULE_DESCRIPTION("NXP FXLS8962AF/FXLS8964AF accelerometer i2c driver");
MODULE_LICENSE("GPL v2");
