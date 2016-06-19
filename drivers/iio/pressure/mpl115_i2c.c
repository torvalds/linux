/*
 * Freescale MPL115A2 pressure/temperature sensor
 *
 * Copyright (c) 2014 Peter Meerwald <pmeerw@pmeerw.net>
 *
 * This file is subject to the terms and conditions of version 2 of
 * the GNU General Public License.  See the file COPYING in the main
 * directory of this archive for more details.
 *
 * (7-bit I2C slave address 0x60)
 *
 * Datasheet: http://www.nxp.com/files/sensors/doc/data_sheet/MPL115A2.pdf
 */

#include <linux/module.h>
#include <linux/i2c.h>

#include "mpl115.h"

static int mpl115_i2c_init(struct device *dev)
{
	return 0;
}

static int mpl115_i2c_read(struct device *dev, u8 address)
{
	return i2c_smbus_read_word_swapped(to_i2c_client(dev), address);
}

static int mpl115_i2c_write(struct device *dev, u8 address, u8 value)
{
	return i2c_smbus_write_byte_data(to_i2c_client(dev), address, value);
}

static const struct mpl115_ops mpl115_i2c_ops = {
	.init = mpl115_i2c_init,
	.read = mpl115_i2c_read,
	.write = mpl115_i2c_write,
};

static int mpl115_i2c_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_WORD_DATA))
		return -EOPNOTSUPP;

	return mpl115_probe(&client->dev, id->name, &mpl115_i2c_ops);
}

static const struct i2c_device_id mpl115_i2c_id[] = {
	{ "mpl115", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mpl115_i2c_id);

static struct i2c_driver mpl115_i2c_driver = {
	.driver = {
		.name	= "mpl115",
	},
	.probe = mpl115_i2c_probe,
	.id_table = mpl115_i2c_id,
};
module_i2c_driver(mpl115_i2c_driver);

MODULE_AUTHOR("Peter Meerwald <pmeerw@pmeerw.net>");
MODULE_DESCRIPTION("Freescale MPL115A2 pressure/temperature driver");
MODULE_LICENSE("GPL");
