/*
 * i2c driver for hmc5843/5843/5883/5883l/5983
 *
 * Split from hmc5843.c
 * Copyright (C) Josef Gajdusek <atx@atx.name>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/iio/iio.h>
#include <linux/iio/triggered_buffer.h>

#include "hmc5843.h"

static const struct regmap_range hmc5843_readable_ranges[] = {
		regmap_reg_range(0, HMC5843_ID_END),
};

static struct regmap_access_table hmc5843_readable_table = {
		.yes_ranges = hmc5843_readable_ranges,
		.n_yes_ranges = ARRAY_SIZE(hmc5843_readable_ranges),
};

static const struct regmap_range hmc5843_writable_ranges[] = {
		regmap_reg_range(0, HMC5843_MODE_REG),
};

static struct regmap_access_table hmc5843_writable_table = {
		.yes_ranges = hmc5843_writable_ranges,
		.n_yes_ranges = ARRAY_SIZE(hmc5843_writable_ranges),
};

static const struct regmap_range hmc5843_volatile_ranges[] = {
		regmap_reg_range(HMC5843_DATA_OUT_MSB_REGS, HMC5843_STATUS_REG),
};

static struct regmap_access_table hmc5843_volatile_table = {
		.yes_ranges = hmc5843_volatile_ranges,
		.n_yes_ranges = ARRAY_SIZE(hmc5843_volatile_ranges),
};

static struct regmap_config hmc5843_i2c_regmap_config = {
		.reg_bits = 8,
		.val_bits = 8,

		.rd_table = &hmc5843_readable_table,
		.wr_table = &hmc5843_writable_table,
		.volatile_table = &hmc5843_volatile_table,

		.cache_type = REGCACHE_RBTREE,
};

static int hmc5843_i2c_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	return hmc5843_common_probe(&client->dev,
			devm_regmap_init_i2c(client, &hmc5843_i2c_regmap_config),
			id->driver_data);
}

static int hmc5843_i2c_remove(struct i2c_client *client)
{
	return hmc5843_common_remove(&client->dev);
}

static const struct i2c_device_id hmc5843_id[] = {
	{ "hmc5843", HMC5843_ID },
	{ "hmc5883", HMC5883_ID },
	{ "hmc5883l", HMC5883L_ID },
	{ "hmc5983", HMC5983_ID },
	{ }
};
MODULE_DEVICE_TABLE(i2c, hmc5843_id);

static const struct of_device_id hmc5843_of_match[] = {
	{ .compatible = "honeywell,hmc5843", .data = (void *)HMC5843_ID },
	{ .compatible = "honeywell,hmc5883", .data = (void *)HMC5883_ID },
	{ .compatible = "honeywell,hmc5883l", .data = (void *)HMC5883L_ID },
	{ .compatible = "honeywell,hmc5983", .data = (void *)HMC5983_ID },
	{}
};
MODULE_DEVICE_TABLE(of, hmc5843_of_match);

static struct i2c_driver hmc5843_driver = {
	.driver = {
		.name	= "hmc5843",
		.pm	= HMC5843_PM_OPS,
		.of_match_table = hmc5843_of_match,
	},
	.id_table	= hmc5843_id,
	.probe		= hmc5843_i2c_probe,
	.remove		= hmc5843_i2c_remove,
};
module_i2c_driver(hmc5843_driver);

MODULE_AUTHOR("Josef Gajdusek <atx@atx.name>");
MODULE_DESCRIPTION("HMC5843/5883/5883L/5983 i2c driver");
MODULE_LICENSE("GPL");
