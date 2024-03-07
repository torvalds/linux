// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_lsm6dsvx i2c driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2022 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>

#include "st_lsm6dsvx.h"

static const struct regmap_config st_lsm6dsvx_i2c_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int st_lsm6dsvx_i2c_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
{
	int hw_id = id->driver_data;
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(client, &st_lsm6dsvx_i2c_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev,
			"Failed to register i2c regmap %d\n",
			(int)PTR_ERR(regmap));

		return PTR_ERR(regmap);
	}

	return st_lsm6dsvx_probe(&client->dev, client->irq, hw_id, regmap);
}

#if KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE
static void st_lsm6dsvx_i2c_remove(struct i2c_client *client)
{
	st_lsm6dsvx_remove(&client->dev);
}
#else /* LINUX_VERSION_CODE */
static int st_lsm6dsvx_i2c_remove(struct i2c_client *client)
{
	return st_lsm6dsvx_remove(&client->dev);
}
#endif /* LINUX_VERSION_CODE */

static const struct of_device_id st_lsm6dsvx_i2c_of_match[] = {
	{
		.compatible = "st," ST_LSM6DSV_DEV_NAME,
		.data = (void *)ST_LSM6DSV_ID,
	},
	{
		.compatible = "st," ST_LSM6DSV16X_DEV_NAME,
		.data = (void *)ST_LSM6DSV16X_ID,
	},
	{},
};
MODULE_DEVICE_TABLE(of, st_lsm6dsvx_i2c_of_match);

static const struct i2c_device_id st_lsm6dsvx_i2c_id_table[] = {
	{ ST_LSM6DSV_DEV_NAME, ST_LSM6DSV_ID },
	{ ST_LSM6DSV16X_DEV_NAME, ST_LSM6DSV16X_ID },
	{},
};
MODULE_DEVICE_TABLE(i2c, st_lsm6dsvx_i2c_id_table);

static struct i2c_driver st_lsm6dsvx_driver = {
	.driver = {
		.name = "st_lsm6dsvx_i2c",
		.pm = &st_lsm6dsvx_pm_ops,
		.of_match_table = of_match_ptr(st_lsm6dsvx_i2c_of_match),
	},
	.probe = st_lsm6dsvx_i2c_probe,
	.remove = st_lsm6dsvx_i2c_remove,
	.id_table = st_lsm6dsvx_i2c_id_table,
};
module_i2c_driver(st_lsm6dsvx_driver);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics st_lsm6dsvx i2c driver");
MODULE_LICENSE("GPL v2");
