// SPDX-License-Identifier: GPL-2.0-only
/*
 * Murata ZPA2326 I2C pressure and temperature sensor driver
 *
 * Copyright (c) 2016 Parrot S.A.
 *
 * Author: Gregor Boirie <gregor.boirie@parrot.com>
 */

#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include "zpa2326.h"

/*
 * read_flag_mask:
 *   - address bit 7 must be set to request a register read operation
 */
static const struct regmap_config zpa2326_regmap_i2c_config = {
	.reg_bits       = 8,
	.val_bits       = 8,
	.writeable_reg  = zpa2326_isreg_writeable,
	.readable_reg   = zpa2326_isreg_readable,
	.precious_reg   = zpa2326_isreg_precious,
	.max_register   = ZPA2326_TEMP_OUT_H_REG,
	.read_flag_mask = BIT(7),
	.cache_type     = REGCACHE_NONE,
};

static unsigned int zpa2326_i2c_hwid(const struct i2c_client *client)
{
#define ZPA2326_SA0(_addr)          (_addr & BIT(0))
#define ZPA2326_DEVICE_ID_SA0_SHIFT (1)

	/* Identification register bit 1 mirrors device address bit 0. */
	return (ZPA2326_DEVICE_ID |
		(ZPA2326_SA0(client->addr) << ZPA2326_DEVICE_ID_SA0_SHIFT));
}

static int zpa2326_probe_i2c(struct i2c_client          *client)
{
	const struct i2c_device_id *i2c_id = i2c_client_get_device_id(client);
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(client, &zpa2326_regmap_i2c_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "failed to init registers map");
		return PTR_ERR(regmap);
	}

	return zpa2326_probe(&client->dev, i2c_id->name, client->irq,
			     zpa2326_i2c_hwid(client), regmap);
}

static void zpa2326_remove_i2c(struct i2c_client *client)
{
	zpa2326_remove(&client->dev);
}

static const struct i2c_device_id zpa2326_i2c_ids[] = {
	{ "zpa2326", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, zpa2326_i2c_ids);

static const struct of_device_id zpa2326_i2c_matches[] = {
	{ .compatible = "murata,zpa2326" },
	{ }
};
MODULE_DEVICE_TABLE(of, zpa2326_i2c_matches);

static struct i2c_driver zpa2326_i2c_driver = {
	.driver = {
		.name           = "zpa2326-i2c",
		.of_match_table = zpa2326_i2c_matches,
		.pm             = ZPA2326_PM_OPS,
	},
	.probe_new = zpa2326_probe_i2c,
	.remove   = zpa2326_remove_i2c,
	.id_table = zpa2326_i2c_ids,
};
module_i2c_driver(zpa2326_i2c_driver);

MODULE_AUTHOR("Gregor Boirie <gregor.boirie@parrot.com>");
MODULE_DESCRIPTION("I2C driver for Murata ZPA2326 pressure sensor");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(IIO_ZPA2326);
