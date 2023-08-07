// SPDX-License-Identifier: GPL-2.0
/*
 * Support for PNI RM3100 3-axis geomagnetic sensor on a i2c bus.
 *
 * Copyright (C) 2018 Song Qiang <songqiang1304521@gmail.com>
 *
 * i2c slave address: 0x20 + SA1 << 1 + SA0.
 */

#include <linux/i2c.h>
#include <linux/module.h>

#include "rm3100.h"

static const struct regmap_config rm3100_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.rd_table = &rm3100_readable_table,
	.wr_table = &rm3100_writable_table,
	.volatile_table = &rm3100_volatile_table,

	.cache_type = REGCACHE_RBTREE,
};

static int rm3100_probe(struct i2c_client *client)
{
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(client, &rm3100_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return rm3100_common_probe(&client->dev, regmap, client->irq);
}

static const struct of_device_id rm3100_dt_match[] = {
	{ .compatible = "pni,rm3100", },
	{ }
};
MODULE_DEVICE_TABLE(of, rm3100_dt_match);

static struct i2c_driver rm3100_driver = {
	.driver = {
		.name = "rm3100-i2c",
		.of_match_table = rm3100_dt_match,
	},
	.probe = rm3100_probe,
};
module_i2c_driver(rm3100_driver);

MODULE_AUTHOR("Song Qiang <songqiang1304521@gmail.com>");
MODULE_DESCRIPTION("PNI RM3100 3-axis magnetometer i2c driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(IIO_RM3100);
