// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_ism330is i2c driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2023 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/of.h>

#include "st_ism330is.h"

static const struct regmap_config st_ism330is_i2c_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int st_ism330is_i2c_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
{
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(client, &st_ism330is_i2c_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Failed to register i2c regmap %d\n",
			(int)PTR_ERR(regmap));

		return PTR_ERR(regmap);
	}

	return st_ism330is_probe(&client->dev, client->irq, regmap);
}

static const struct of_device_id st_ism330is_i2c_of_match[] = {
	{ .compatible = "st,ism330is" },
	{},
};
MODULE_DEVICE_TABLE(of, st_ism330is_i2c_of_match);

static const struct i2c_device_id st_ism330is_i2c_id_table[] = {
	{ ST_ISM330IS_DEV_NAME },
	{},
};
MODULE_DEVICE_TABLE(i2c, st_ism330is_i2c_id_table);

static struct i2c_driver st_ism330is_driver = {
	.driver = {
		.name = "st_" ST_ISM330IS_DEV_NAME "_i2c",
		.pm = &st_ism330is_pm_ops,
		.of_match_table = of_match_ptr(st_ism330is_i2c_of_match),
	},
	.probe = st_ism330is_i2c_probe,
	.id_table = st_ism330is_i2c_id_table,
};
module_i2c_driver(st_ism330is_driver);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics st_ism330is i2c driver");
MODULE_LICENSE("GPL v2");
