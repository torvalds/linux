// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_sths34pf80 i2c driver
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
#include <linux/version.h>

#include "st_sths34pf80.h"

static const struct regmap_config st_sths34pf80_i2c_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int st_sths34pf80_i2c_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(client,
				      &st_sths34pf80_i2c_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev,
			"Failed to register i2c regmap %d\n",
			(int)PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	return st_sths34pf80_probe(&client->dev, client->irq, regmap);
}

#if KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE
static void st_sths34pf80_i2c_remove(struct i2c_client *client)
{
	st_sths34pf80_remove(&client->dev);
}
#else /* LINUX_VERSION_CODE */
static int st_sths34pf80_i2c_remove(struct i2c_client *client)
{
	return st_sths34pf80_remove(&client->dev);
}
#endif /* LINUX_VERSION_CODE */

static const struct of_device_id st_sths34pf80_i2c_of_match[] = {
	{ .compatible = "st," ST_STHS34PF80_DEV_NAME },
	{},
};
MODULE_DEVICE_TABLE(of, st_sths34pf80_i2c_of_match);

static const struct i2c_device_id st_sths34pf80_i2c_id_table[] = {
	{ ST_STHS34PF80_DEV_NAME },
	{},
};
MODULE_DEVICE_TABLE(i2c, st_sths34pf80_i2c_id_table);

static struct i2c_driver st_sths34pf80_driver = {
	.driver = {
		.name = "st_" ST_STHS34PF80_DEV_NAME "_i2c",
#ifdef CONFIG_PM_SLEEP
		.pm = &st_sths34pf80_pm_ops,
#endif /* CONFIG_PM_SLEEP */
		.of_match_table =
			       of_match_ptr(st_sths34pf80_i2c_of_match),
	},
	.probe = st_sths34pf80_i2c_probe,
	.remove = st_sths34pf80_i2c_remove,
	.id_table = st_sths34pf80_i2c_id_table,
};
module_i2c_driver(st_sths34pf80_driver);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics st_sths34pf80 i2c driver");
MODULE_LICENSE("GPL v2");
