// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics ilps22qs i2c driver
 *
 * Copyright 2023 STMicroelectronics Inc.
 *
 * MEMS Software Solutions Team
 */

#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/version.h>

#include "st_ilps22qs.h"

static const struct regmap_config st_ilps22qs_i2c_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int st_ilps22qs_i2c_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
{
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(client, &st_ilps22qs_i2c_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev,
			"Failed to register i2c regmap %d\n",
			(int)PTR_ERR(regmap));

		return PTR_ERR(regmap);
	}

	return st_ilps22qs_probe(&client->dev, regmap);
}

#if KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE
static void st_ilps22qs_i2c_remove(struct i2c_client *client)
{
	st_ilps22qs_remove(&client->dev);
}
#else /* LINUX_VERSION_CODE */
static int st_ilps22qs_i2c_remove(struct i2c_client *client)
{
	return st_ilps22qs_remove(&client->dev);
}
#endif /* LINUX_VERSION_CODE */

static const struct i2c_device_id st_ilps22qs_ids[] = {
	{ ST_ILPS22QS_DEV_NAME },
	{ ST_ILPS28QWS_DEV_NAME },
	{}
};
MODULE_DEVICE_TABLE(i2c, st_ilps22qs_ids);

static const struct of_device_id st_ilps22qs_id_table[] = {
	{ .compatible = "st," ST_ILPS22QS_DEV_NAME },
	{ .compatible = "st," ST_ILPS28QWS_DEV_NAME },
	{},
};
MODULE_DEVICE_TABLE(of, st_ilps22qs_id_table);

static struct i2c_driver st_ilps22qs_i2c_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "st_" ST_ILPS22QS_DEV_NAME "_i2c",
		.pm = &st_ilps22qs_pm_ops,
		.of_match_table = of_match_ptr(st_ilps22qs_id_table),
	},
	.probe = st_ilps22qs_i2c_probe,
	.remove = st_ilps22qs_i2c_remove,
	.id_table = st_ilps22qs_ids,
};
module_i2c_driver(st_ilps22qs_i2c_driver);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics ilps22qs i2c driver");
MODULE_LICENSE("GPL v2");
