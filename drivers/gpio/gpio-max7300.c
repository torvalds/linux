// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2009 Wolfram Sang, Pengutronix
 *
 * Check max730x.c for further details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/i2c.h>
#include <linux/spi/max7301.h>
#include <linux/slab.h>

static int max7300_i2c_write(struct device *dev, unsigned int reg,
				unsigned int val)
{
	struct i2c_client *client = to_i2c_client(dev);

	return i2c_smbus_write_byte_data(client, reg, val);
}

static int max7300_i2c_read(struct device *dev, unsigned int reg)
{
	struct i2c_client *client = to_i2c_client(dev);

	return i2c_smbus_read_byte_data(client, reg);
}

static int max7300_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct max7301 *ts;

	if (!i2c_check_functionality(client->adapter,
			I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	ts = devm_kzalloc(&client->dev, sizeof(struct max7301), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	ts->read = max7300_i2c_read;
	ts->write = max7300_i2c_write;
	ts->dev = &client->dev;

	return __max730x_probe(ts);
}

static void max7300_remove(struct i2c_client *client)
{
	__max730x_remove(&client->dev);
}

static const struct i2c_device_id max7300_id[] = {
	{ "max7300", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max7300_id);

static struct i2c_driver max7300_driver = {
	.driver = {
		.name = "max7300",
	},
	.probe = max7300_probe,
	.remove = max7300_remove,
	.id_table = max7300_id,
};

static int __init max7300_init(void)
{
	return i2c_add_driver(&max7300_driver);
}
subsys_initcall(max7300_init);

static void __exit max7300_exit(void)
{
	i2c_del_driver(&max7300_driver);
}
module_exit(max7300_exit);

MODULE_AUTHOR("Wolfram Sang");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MAX7300 GPIO-Expander");
