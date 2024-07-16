// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ADLX345/346 Three-Axis Digital Accelerometers (I2C Interface)
 *
 * Enter bugs at http://blackfin.uclinux.org/
 *
 * Copyright (C) 2009 Michael Hennerich, Analog Devices Inc.
 */

#include <linux/input.h>	/* BUS_I2C */
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/types.h>
#include <linux/pm.h>
#include "adxl34x.h"

static int adxl34x_smbus_read(struct device *dev, unsigned char reg)
{
	struct i2c_client *client = to_i2c_client(dev);

	return i2c_smbus_read_byte_data(client, reg);
}

static int adxl34x_smbus_write(struct device *dev,
			       unsigned char reg, unsigned char val)
{
	struct i2c_client *client = to_i2c_client(dev);

	return i2c_smbus_write_byte_data(client, reg, val);
}

static int adxl34x_smbus_read_block(struct device *dev,
				    unsigned char reg, int count,
				    void *buf)
{
	struct i2c_client *client = to_i2c_client(dev);

	return i2c_smbus_read_i2c_block_data(client, reg, count, buf);
}

static int adxl34x_i2c_read_block(struct device *dev,
				  unsigned char reg, int count,
				  void *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	int ret;

	ret = i2c_master_send(client, &reg, 1);
	if (ret < 0)
		return ret;

	ret = i2c_master_recv(client, buf, count);
	if (ret < 0)
		return ret;

	if (ret != count)
		return -EIO;

	return 0;
}

static const struct adxl34x_bus_ops adxl34x_smbus_bops = {
	.bustype	= BUS_I2C,
	.write		= adxl34x_smbus_write,
	.read		= adxl34x_smbus_read,
	.read_block	= adxl34x_smbus_read_block,
};

static const struct adxl34x_bus_ops adxl34x_i2c_bops = {
	.bustype	= BUS_I2C,
	.write		= adxl34x_smbus_write,
	.read		= adxl34x_smbus_read,
	.read_block	= adxl34x_i2c_read_block,
};

static int adxl34x_i2c_probe(struct i2c_client *client,
				       const struct i2c_device_id *id)
{
	struct adxl34x *ac;
	int error;

	error = i2c_check_functionality(client->adapter,
			I2C_FUNC_SMBUS_BYTE_DATA);
	if (!error) {
		dev_err(&client->dev, "SMBUS Byte Data not Supported\n");
		return -EIO;
	}

	ac = adxl34x_probe(&client->dev, client->irq, false,
			   i2c_check_functionality(client->adapter,
						   I2C_FUNC_SMBUS_READ_I2C_BLOCK) ?
				&adxl34x_smbus_bops : &adxl34x_i2c_bops);
	if (IS_ERR(ac))
		return PTR_ERR(ac);

	i2c_set_clientdata(client, ac);

	return 0;
}

static void adxl34x_i2c_remove(struct i2c_client *client)
{
	struct adxl34x *ac = i2c_get_clientdata(client);

	adxl34x_remove(ac);
}

static int __maybe_unused adxl34x_i2c_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct adxl34x *ac = i2c_get_clientdata(client);

	adxl34x_suspend(ac);

	return 0;
}

static int __maybe_unused adxl34x_i2c_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct adxl34x *ac = i2c_get_clientdata(client);

	adxl34x_resume(ac);

	return 0;
}

static SIMPLE_DEV_PM_OPS(adxl34x_i2c_pm, adxl34x_i2c_suspend,
			 adxl34x_i2c_resume);

static const struct i2c_device_id adxl34x_id[] = {
	{ "adxl34x", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, adxl34x_id);

static const struct of_device_id adxl34x_of_id[] = {
	/*
	 * The ADXL346 is backward-compatible with the ADXL345. Differences are
	 * handled by runtime detection of the device model, there's thus no
	 * need for listing the "adi,adxl346" compatible value explicitly.
	 */
	{ .compatible = "adi,adxl345", },
	/*
	 * Deprecated, DT nodes should use one or more of the device-specific
	 * compatible values "adi,adxl345" and "adi,adxl346".
	 */
	{ .compatible = "adi,adxl34x", },
	{ }
};

MODULE_DEVICE_TABLE(of, adxl34x_of_id);

static struct i2c_driver adxl34x_driver = {
	.driver = {
		.name = "adxl34x",
		.pm = &adxl34x_i2c_pm,
		.of_match_table = adxl34x_of_id,
	},
	.probe    = adxl34x_i2c_probe,
	.remove   = adxl34x_i2c_remove,
	.id_table = adxl34x_id,
};

module_i2c_driver(adxl34x_driver);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("ADXL345/346 Three-Axis Digital Accelerometer I2C Bus Driver");
MODULE_LICENSE("GPL");
