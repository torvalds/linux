/*
 * ST Microelectronics MFD: stmpe's i2c client specific driver
 *
 * Copyright (C) ST-Ericsson SA 2010
 * Copyright (C) ST Microelectronics SA 2011
 *
 * License Terms: GNU General Public License, version 2
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 * Author: Viresh Kumar <viresh.linux@gmail.com> for ST Microelectronics
 */

#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
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

static int __devinit
stmpe_i2c_probe(struct i2c_client *i2c, const struct i2c_device_id *id)
{
	i2c_ci.data = (void *)id;
	i2c_ci.irq = i2c->irq;
	i2c_ci.client = i2c;
	i2c_ci.dev = &i2c->dev;

	return stmpe_probe(&i2c_ci, id->driver_data);
}

static int __devexit stmpe_i2c_remove(struct i2c_client *i2c)
{
	struct stmpe *stmpe = dev_get_drvdata(&i2c->dev);

	return stmpe_remove(stmpe);
}

static const struct i2c_device_id stmpe_i2c_id[] = {
	{ "stmpe610", STMPE610 },
	{ "stmpe801", STMPE801 },
	{ "stmpe811", STMPE811 },
	{ "stmpe1601", STMPE1601 },
	{ "stmpe2401", STMPE2401 },
	{ "stmpe2403", STMPE2403 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, stmpe_id);

static struct i2c_driver stmpe_i2c_driver = {
	.driver.name	= "stmpe-i2c",
	.driver.owner	= THIS_MODULE,
#ifdef CONFIG_PM
	.driver.pm	= &stmpe_dev_pm_ops,
#endif
	.probe		= stmpe_i2c_probe,
	.remove		= __devexit_p(stmpe_i2c_remove),
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
