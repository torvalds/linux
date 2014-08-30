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
	struct greybus_device *gdev;
};

static const struct greybus_device_id id_table[] = {
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
	struct gb_i2c_device *i2c_gb_dev;
	struct greybus_device *gdev;

	i2c_gb_dev = i2c_get_adapdata(adap);
	gdev = i2c_gb_dev->gdev;

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
		dev_err(&gdev->dev, "Unsupported transaction %d\n", size);
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

int gb_i2c_probe(struct greybus_device *gdev,
		 const struct greybus_device_id *id)
{
	struct gb_i2c_device *i2c_gb_dev;
	struct i2c_adapter *adapter;
	int retval;

	i2c_gb_dev = kzalloc(sizeof(*i2c_gb_dev), GFP_KERNEL);
	if (!i2c_gb_dev)
		return -ENOMEM;
	adapter = kzalloc(sizeof(*adapter), GFP_KERNEL);
	if (!adapter) {
		kfree(i2c_gb_dev);
		return -ENOMEM;
	}

	i2c_set_adapdata(adapter, i2c_gb_dev);
	adapter->owner = THIS_MODULE;
	adapter->class = I2C_CLASS_HWMON | I2C_CLASS_SPD;
	adapter->algo = &smbus_algorithm;
	adapter->dev.parent = &gdev->dev;
	adapter->retries = 3;	/* we have to pick something... */
	snprintf(adapter->name, sizeof(adapter->name), "Greybus i2c adapter");
	retval = i2c_add_adapter(adapter);
	if (retval) {
		dev_err(&gdev->dev, "Can not add SMBus adapter\n");
		goto error;
	}

	i2c_gb_dev->gdev = gdev;
	i2c_gb_dev->adapter = adapter;

	greybus_set_drvdata(gdev, i2c_gb_dev);
	return 0;
error:
	kfree(adapter);
	kfree(i2c_gb_dev);
	return retval;
}

void gb_i2c_disconnect(struct greybus_device *gdev)
{
	struct gb_i2c_device *i2c_gb_dev;

	i2c_gb_dev = greybus_get_drvdata(gdev);
	i2c_del_adapter(i2c_gb_dev->adapter);
	kfree(i2c_gb_dev->adapter);
	kfree(i2c_gb_dev);
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
