// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_lis2duxs12 i2c driver
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

#include "st_lis2duxs12.h"

static const struct regmap_config st_lis2duxs12_i2c_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int st_lis2duxs12_i2c_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
	enum st_lis2duxs12_hw_id hw_id = id->driver_data;
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(client, &st_lis2duxs12_i2c_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev,
			"Failed to register i2c regmap %d\n",
			(int)PTR_ERR(regmap));

		return PTR_ERR(regmap);
	}

	return st_lis2duxs12_probe(&client->dev, client->irq, hw_id, regmap);
}

#if KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE
static void st_lis2duxs12_i2c_remove(struct i2c_client *client)
{
	st_lis2duxs12_remove(&client->dev);
}
#else /* LINUX_VERSION_CODE */
static int st_lis2duxs12_i2c_remove(struct i2c_client *client)
{
	return st_lis2duxs12_remove(&client->dev);
}
#endif /* LINUX_VERSION_CODE */

static const struct of_device_id st_lis2duxs12_i2c_of_match[] = {
	{
		.compatible = "st," ST_LIS2DUX12_DEV_NAME,
		.data = (void *)ST_LIS2DUX12_ID,
	},
	{
		.compatible = "st," ST_LIS2DUXS12_DEV_NAME,
		.data = (void *)ST_LIS2DUXS12_ID,
	},
	{},
};
MODULE_DEVICE_TABLE(of, st_lis2duxs12_i2c_of_match);

static const struct i2c_device_id st_lis2duxs12_i2c_id_table[] = {
	{ ST_LIS2DUX12_DEV_NAME, ST_LIS2DUX12_ID },
	{ ST_LIS2DUXS12_DEV_NAME, ST_LIS2DUXS12_ID },
	{},
};
MODULE_DEVICE_TABLE(i2c, st_lis2duxs12_i2c_id_table);

static struct i2c_driver st_lis2duxs12_driver = {
	.driver = {
		.name = "st_" ST_LIS2DUXS12_DEV_NAME "_i2c",
		.pm = &st_lis2duxs12_pm_ops,
		.of_match_table = of_match_ptr(st_lis2duxs12_i2c_of_match),
	},
	.probe = st_lis2duxs12_i2c_probe,
	.remove = st_lis2duxs12_i2c_remove,
	.id_table = st_lis2duxs12_i2c_id_table,
};
module_i2c_driver(st_lis2duxs12_driver);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics st_lis2duxs12 i2c driver");
MODULE_LICENSE("GPL v2");
