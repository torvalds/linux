// SPDX-License-Identifier: GPL-2.0-only
/*
 * IIO accel I2C driver for Freescale MMA7455L 3-axis 10-bit accelerometer
 * Copyright 2015 Joachim Eastwood <manabian@gmail.com>
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "mma7455.h"

static int mma7455_i2c_probe(struct i2c_client *i2c,
			     const struct i2c_device_id *id)
{
	struct regmap *regmap;
	const char *name = NULL;

	regmap = devm_regmap_init_i2c(i2c, &mma7455_core_regmap);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	if (id)
		name = id->name;

	return mma7455_core_probe(&i2c->dev, regmap, name);
}

static int mma7455_i2c_remove(struct i2c_client *i2c)
{
	mma7455_core_remove(&i2c->dev);

	return 0;
}

static const struct i2c_device_id mma7455_i2c_ids[] = {
	{ "mma7455", 0 },
	{ "mma7456", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mma7455_i2c_ids);

static const struct of_device_id mma7455_of_match[] = {
	{ .compatible = "fsl,mma7455" },
	{ .compatible = "fsl,mma7456" },
	{ }
};
MODULE_DEVICE_TABLE(of, mma7455_of_match);

static struct i2c_driver mma7455_i2c_driver = {
	.probe = mma7455_i2c_probe,
	.remove = mma7455_i2c_remove,
	.id_table = mma7455_i2c_ids,
	.driver = {
		.name	= "mma7455-i2c",
		.of_match_table = mma7455_of_match,
	},
};
module_i2c_driver(mma7455_i2c_driver);

MODULE_AUTHOR("Joachim Eastwood <manabian@gmail.com>");
MODULE_DESCRIPTION("Freescale MMA7455L I2C accelerometer driver");
MODULE_LICENSE("GPL v2");
