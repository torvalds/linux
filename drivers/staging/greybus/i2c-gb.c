/*
 * I2C bridge driver for the Greybus "generic" I2C module.
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include "greybus.h"

struct gb_i2c_device {
	struct i2c_adapter *adapter;
	struct greybus_module *gmod;
};

static const struct greybus_module_id id_table[] = {
	{ GREYBUS_DEVICE(0x42, 0x42) },	/* make shit up */
	{ },	/* terminating NULL entry */
};

/* We BETTER be able to do SMBUS protocl calls, otherwise we are bit-banging the
 * slowest thing possible over the fastest bus possible, crazy...
 * FIXME - research this, for now just assume we can
 */


static s32 i2c_gb_access(struct i2c_adapter *adap, u16 addr,
			 unsigned short flags, char read_write, u8 command,
			 int size, union i2c_smbus_data *data)
{
	struct gb_i2c_device *gb_i2c_dev;
	struct greybus_module *gmod;

	gb_i2c_dev = i2c_get_adapdata(adap);
	gmod = gb_i2c_dev->gmod;

	// FIXME - do the actual work of sending a i2c message here...
	switch (size) {
	case I2C_SMBUS_QUICK:
	case I2C_SMBUS_BYTE:
	case I2C_SMBUS_BYTE_DATA:
	case I2C_SMBUS_WORD_DATA:
	case I2C_SMBUS_PROC_CALL:
	case I2C_SMBUS_BLOCK_DATA:
	case I2C_SMBUS_I2C_BLOCK_BROKEN:
	case I2C_SMBUS_BLOCK_PROC_CALL:
	case I2C_SMBUS_I2C_BLOCK_DATA:
	default:
		dev_err(&gmod->dev, "Unsupported transaction %d\n", size);
		return -EOPNOTSUPP;
	}

	return 0;
}

static u32 i2c_gb_func(struct i2c_adapter *adapter)
{
	// FIXME - someone figure out what we really can support, for now just guess...
	return I2C_FUNC_SMBUS_QUICK |
		I2C_FUNC_SMBUS_BYTE |
		I2C_FUNC_SMBUS_BYTE_DATA |
		I2C_FUNC_SMBUS_WORD_DATA |
		I2C_FUNC_SMBUS_BLOCK_DATA |
		I2C_FUNC_SMBUS_WRITE_I2C_BLOCK |
		I2C_FUNC_SMBUS_PEC |
		I2C_FUNC_SMBUS_READ_I2C_BLOCK;
}

static const struct i2c_algorithm smbus_algorithm = {
	.smbus_xfer	= i2c_gb_access,
	.functionality	= i2c_gb_func,
};

int gb_i2c_probe(struct greybus_module *gmod,
		 const struct greybus_module_id *id)
{
	struct gb_i2c_device *gb_i2c_dev;
	struct i2c_adapter *adapter;
	int retval;

	gb_i2c_dev = kzalloc(sizeof(*gb_i2c_dev), GFP_KERNEL);
	if (!gb_i2c_dev)
		return -ENOMEM;
	adapter = kzalloc(sizeof(*adapter), GFP_KERNEL);
	if (!adapter) {
		kfree(gb_i2c_dev);
		return -ENOMEM;
	}

	i2c_set_adapdata(adapter, gb_i2c_dev);
	adapter->owner = THIS_MODULE;
	adapter->class = I2C_CLASS_HWMON | I2C_CLASS_SPD;
	adapter->algo = &smbus_algorithm;
	adapter->dev.parent = &gmod->dev;
	adapter->retries = 3;	/* we have to pick something... */
	snprintf(adapter->name, sizeof(adapter->name), "Greybus i2c adapter");
	retval = i2c_add_adapter(adapter);
	if (retval) {
		dev_err(&gmod->dev, "Can not add SMBus adapter\n");
		goto error;
	}

	gb_i2c_dev->gmod = gmod;
	gb_i2c_dev->adapter = adapter;

	gmod->gb_i2c_dev = gb_i2c_dev;
	return 0;
error:
	kfree(adapter);
	kfree(gb_i2c_dev);
	return retval;
}

void gb_i2c_disconnect(struct greybus_module *gmod)
{
	struct gb_i2c_device *gb_i2c_dev;

	gb_i2c_dev = gmod->gb_i2c_dev;
	i2c_del_adapter(gb_i2c_dev->adapter);
	kfree(gb_i2c_dev->adapter);
	kfree(gb_i2c_dev);
}

#if 0
static struct greybus_driver i2c_gb_driver = {
	.probe =	gb_i2c_probe,
	.disconnect =	gb_i2c_disconnect,
	.id_table =	id_table,
};

module_greybus_driver(i2c_gb_driver);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Greg Kroah-Hartman <gregkh@linuxfoundation.org>");
#endif
