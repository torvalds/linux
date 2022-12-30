// SPDX-License-Identifier: GPL-2.0-only
/*
 * ST Microelectronics MFD: stmpe's i2c client specific driver
 *
 * Copyright (C) ST-Ericsson SA 2010
 * Copyright (C) ST Microelectronics SA 2011
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 * Author: Viresh Kumar <vireshk@kernel.org> for ST Microelectronics
 */

#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/of_device.h>
#include "stmpe.h"

static int i2c_reg_read(struct stmpe *stmpe, u8 reg)
{
	struct i2c_client *i2c = stmpe->client;

	return i2c_smbus_read_byte_data(i2c, reg);
}

static int i2c_reg_write(struct stmpe *stmpe, u8 reg, u8 val)
{
	struct i2c_client *i2c = stmpe->client;

	return i2c_smbus_write_byte_data(i2c, reg, val);
}

static int i2c_block_read(struct stmpe *stmpe, u8 reg, u8 length, u8 *values)
{
	struct i2c_client *i2c = stmpe->client;

	return i2c_smbus_read_i2c_block_data(i2c, reg, length, values);
}

static int i2c_block_write(struct stmpe *stmpe, u8 reg, u8 length,
		const u8 *values)
{
	struct i2c_client *i2c = stmpe->client;

	return i2c_smbus_write_i2c_block_data(i2c, reg, length, values);
}

static struct stmpe_client_info i2c_ci = {
	.read_byte = i2c_reg_read,
	.write_byte = i2c_reg_write,
	.read_block = i2c_block_read,
	.write_block = i2c_block_write,
};

static const struct of_device_id stmpe_of_match[] = {
	{ .compatible = "st,stmpe610", .data = (void *)STMPE610, },
	{ .compatible = "st,stmpe801", .data = (void *)STMPE801, },
	{ .compatible = "st,stmpe811", .data = (void *)STMPE811, },
	{ .compatible = "st,stmpe1600", .data = (void *)STMPE1600, },
	{ .compatible = "st,stmpe1601", .data = (void *)STMPE1601, },
	{ .compatible = "st,stmpe1801", .data = (void *)STMPE1801, },
	{ .compatible = "st,stmpe2401", .data = (void *)STMPE2401, },
	{ .compatible = "st,stmpe2403", .data = (void *)STMPE2403, },
	{},
};
MODULE_DEVICE_TABLE(of, stmpe_of_match);

static int
stmpe_i2c_probe(struct i2c_client *i2c)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(i2c);
	enum stmpe_partnum partnum;
	const struct of_device_id *of_id;

	i2c_ci.data = (void *)id;
	i2c_ci.irq = i2c->irq;
	i2c_ci.client = i2c;
	i2c_ci.dev = &i2c->dev;

	of_id = of_match_device(stmpe_of_match, &i2c->dev);
	if (!of_id) {
		/*
		 * This happens when the I2C ID matches the node name
		 * but no real compatible string has been given.
		 */
		dev_info(&i2c->dev, "matching on node name, compatible is preferred\n");
		partnum = id->driver_data;
	} else
		partnum = (enum stmpe_partnum)of_id->data;

	return stmpe_probe(&i2c_ci, partnum);
}

static void stmpe_i2c_remove(struct i2c_client *i2c)
{
	struct stmpe *stmpe = dev_get_drvdata(&i2c->dev);

	stmpe_remove(stmpe);
}

static const struct i2c_device_id stmpe_i2c_id[] = {
	{ "stmpe610", STMPE610 },
	{ "stmpe801", STMPE801 },
	{ "stmpe811", STMPE811 },
	{ "stmpe1600", STMPE1600 },
	{ "stmpe1601", STMPE1601 },
	{ "stmpe1801", STMPE1801 },
	{ "stmpe2401", STMPE2401 },
	{ "stmpe2403", STMPE2403 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, stmpe_i2c_id);

static struct i2c_driver stmpe_i2c_driver = {
	.driver = {
		.name = "stmpe-i2c",
		.pm = pm_sleep_ptr(&stmpe_dev_pm_ops),
		.of_match_table = stmpe_of_match,
	},
	.probe_new	= stmpe_i2c_probe,
	.remove		= stmpe_i2c_remove,
	.id_table	= stmpe_i2c_id,
};

static int __init stmpe_init(void)
{
	return i2c_add_driver(&stmpe_i2c_driver);
}
subsys_initcall(stmpe_init);

static void __exit stmpe_exit(void)
{
	i2c_del_driver(&stmpe_i2c_driver);
}
module_exit(stmpe_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("STMPE MFD I2C Interface Driver");
MODULE_AUTHOR("Rabin Vincent <rabin.vincent@stericsson.com>");
