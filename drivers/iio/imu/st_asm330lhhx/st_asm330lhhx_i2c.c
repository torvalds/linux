// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_asm330lhhx i2c driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2022 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/of.h>

#include "st_asm330lhhx.h"

static const struct regmap_config st_asm330lhhx_i2c_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int st_asm330lhhx_i2c_probe(struct i2c_client *client,
			       const struct i2c_device_id *id)
{
	int hw_id = id->driver_data;
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(client,
				      &st_asm330lhhx_i2c_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev,
			"Failed to register i2c regmap %d\n",
			(int)PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	return st_asm330lhhx_probe(&client->dev, client->irq,
			       hw_id, regmap);
}

static const struct of_device_id st_asm330lhhx_i2c_of_match[] = {
	{
		.compatible = "st,asm330lhhx",
		.data = (void *)ST_ASM330LHHX_ID,
	},
	{
		.compatible = "st,asm330lhh",
		.data = (void *)ST_ASM330LHH_ID,
	},
	{
		.compatible = "st,asm330lhhxg1",
		.data = (void *)ST_ASM330LHHXG1_ID,
	},
	{
		.compatible = "st,asm330lhb",
		.data = (void *)ST_ASM330LHB_ID,
	},
	{},
};
MODULE_DEVICE_TABLE(of, st_asm330lhhx_i2c_of_match);

static const struct i2c_device_id st_asm330lhhx_i2c_id_table[] = {
	{ ST_ASM330LHHX_DEV_NAME, ST_ASM330LHHX_ID },
	{ ST_ASM330LHH_DEV_NAME , ST_ASM330LHH_ID },
	{ ST_ASM330LHHXG1_DEV_NAME, ST_ASM330LHHXG1_ID },
	{ ST_ASM330LHB_DEV_NAME, ST_ASM330LHB_ID },
	{},
};
MODULE_DEVICE_TABLE(i2c, st_asm330lhhx_i2c_id_table);

static struct i2c_driver st_asm330lhhx_driver = {
	.driver = {
		.name = "st_asm330lhhx_i2c",
		.pm = &st_asm330lhhx_pm_ops,
		.of_match_table = st_asm330lhhx_i2c_of_match,
	},
	.probe = st_asm330lhhx_i2c_probe,
	.id_table = st_asm330lhhx_i2c_id_table,
};
module_i2c_driver(st_asm330lhhx_driver);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics st_asm330lhhx i2c driver");
MODULE_LICENSE("GPL v2");
