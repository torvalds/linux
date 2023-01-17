// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * TSC2004 touchscreen driver
 *
 * Copyright (C) 2015 QWERTY Embedded Design
 * Copyright (C) 2015 EMAC Inc.
 */

#include <linux/module.h>
#include <linux/input.h>
#include <linux/of.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include "tsc200x-core.h"

static const struct input_id tsc2004_input_id = {
	.bustype = BUS_I2C,
	.product = 2004,
};

static int tsc2004_cmd(struct device *dev, u8 cmd)
{
	u8 tx = TSC200X_CMD | TSC200X_CMD_12BIT | cmd;
	s32 data;
	struct i2c_client *i2c = to_i2c_client(dev);

	data = i2c_smbus_write_byte(i2c, tx);
	if (data < 0) {
		dev_err(dev, "%s: failed, command: %x i2c error: %d\n",
			__func__, cmd, data);
		return data;
	}

	return 0;
}

static int tsc2004_probe(struct i2c_client *i2c)

{
	return tsc200x_probe(&i2c->dev, i2c->irq, &tsc2004_input_id,
			     devm_regmap_init_i2c(i2c, &tsc200x_regmap_config),
			     tsc2004_cmd);
}

static void tsc2004_remove(struct i2c_client *i2c)
{
	tsc200x_remove(&i2c->dev);
}

static const struct i2c_device_id tsc2004_idtable[] = {
	{ "tsc2004", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tsc2004_idtable);

#ifdef CONFIG_OF
static const struct of_device_id tsc2004_of_match[] = {
	{ .compatible = "ti,tsc2004" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, tsc2004_of_match);
#endif

static struct i2c_driver tsc2004_driver = {
	.driver = {
		.name   = "tsc2004",
		.of_match_table = of_match_ptr(tsc2004_of_match),
		.pm     = &tsc200x_pm_ops,
	},
	.id_table       = tsc2004_idtable,
	.probe_new      = tsc2004_probe,
	.remove         = tsc2004_remove,
};
module_i2c_driver(tsc2004_driver);

MODULE_AUTHOR("Michael Welling <mwelling@ieee.org>");
MODULE_DESCRIPTION("TSC2004 Touchscreen Driver");
MODULE_LICENSE("GPL");
