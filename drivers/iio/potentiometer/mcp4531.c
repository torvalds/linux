/*
 * Industrial I/O driver for Microchip digital potentiometers
 * Copyright (c) 2015  Axentia Technologies AB
 * Author: Peter Rosin <peda@axentia.se>
 *
 * Datasheet: http://www.microchip.com/downloads/en/DeviceDoc/22096b.pdf
 *
 * DEVID	#Wipers	#Positions	Resistor Opts (kOhm)	i2c address
 * mcp4531	1	129		5, 10, 50, 100          010111x
 * mcp4532	1	129		5, 10, 50, 100          01011xx
 * mcp4551	1	257		5, 10, 50, 100          010111x
 * mcp4552	1	257		5, 10, 50, 100          01011xx
 * mcp4631	2	129		5, 10, 50, 100          0101xxx
 * mcp4632	2	129		5, 10, 50, 100          01011xx
 * mcp4651	2	257		5, 10, 50, 100          0101xxx
 * mcp4652	2	257		5, 10, 50, 100          01011xx
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/err.h>

#include <linux/iio/iio.h>

struct mcp4531_cfg {
	int wipers;
	int max_pos;
	int kohms;
};

enum mcp4531_type {
	MCP453x_502,
	MCP453x_103,
	MCP453x_503,
	MCP453x_104,
	MCP455x_502,
	MCP455x_103,
	MCP455x_503,
	MCP455x_104,
	MCP463x_502,
	MCP463x_103,
	MCP463x_503,
	MCP463x_104,
	MCP465x_502,
	MCP465x_103,
	MCP465x_503,
	MCP465x_104,
};

static const struct mcp4531_cfg mcp4531_cfg[] = {
	[MCP453x_502] = { .wipers = 1, .max_pos = 128, .kohms =   5, },
	[MCP453x_103] = { .wipers = 1, .max_pos = 128, .kohms =  10, },
	[MCP453x_503] = { .wipers = 1, .max_pos = 128, .kohms =  50, },
	[MCP453x_104] = { .wipers = 1, .max_pos = 128, .kohms = 100, },
	[MCP455x_502] = { .wipers = 1, .max_pos = 256, .kohms =   5, },
	[MCP455x_103] = { .wipers = 1, .max_pos = 256, .kohms =  10, },
	[MCP455x_503] = { .wipers = 1, .max_pos = 256, .kohms =  50, },
	[MCP455x_104] = { .wipers = 1, .max_pos = 256, .kohms = 100, },
	[MCP463x_502] = { .wipers = 2, .max_pos = 128, .kohms =   5, },
	[MCP463x_103] = { .wipers = 2, .max_pos = 128, .kohms =  10, },
	[MCP463x_503] = { .wipers = 2, .max_pos = 128, .kohms =  50, },
	[MCP463x_104] = { .wipers = 2, .max_pos = 128, .kohms = 100, },
	[MCP465x_502] = { .wipers = 2, .max_pos = 256, .kohms =   5, },
	[MCP465x_103] = { .wipers = 2, .max_pos = 256, .kohms =  10, },
	[MCP465x_503] = { .wipers = 2, .max_pos = 256, .kohms =  50, },
	[MCP465x_104] = { .wipers = 2, .max_pos = 256, .kohms = 100, },
};

#define MCP4531_WRITE (0 << 2)
#define MCP4531_INCR  (1 << 2)
#define MCP4531_DECR  (2 << 2)
#define MCP4531_READ  (3 << 2)

#define MCP4531_WIPER_SHIFT (4)

struct mcp4531_data {
	struct i2c_client *client;
	unsigned long devid;
};

#define MCP4531_CHANNEL(ch) {					\
	.type = IIO_RESISTANCE,					\
	.indexed = 1,						\
	.output = 1,						\
	.channel = (ch),					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
}

static const struct iio_chan_spec mcp4531_channels[] = {
	MCP4531_CHANNEL(0),
	MCP4531_CHANNEL(1),
};

static int mcp4531_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct mcp4531_data *data = iio_priv(indio_dev);
	int address = chan->channel << MCP4531_WIPER_SHIFT;
	s32 ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = i2c_smbus_read_word_swapped(data->client,
						  MCP4531_READ | address);
		if (ret < 0)
			return ret;
		*val = ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 1000 * mcp4531_cfg[data->devid].kohms;
		*val2 = mcp4531_cfg[data->devid].max_pos;
		return IIO_VAL_FRACTIONAL;
	}

	return -EINVAL;
}

static int mcp4531_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct mcp4531_data *data = iio_priv(indio_dev);
	int address = chan->channel << MCP4531_WIPER_SHIFT;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (val > mcp4531_cfg[data->devid].max_pos || val < 0)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	return i2c_smbus_write_byte_data(data->client,
					 MCP4531_WRITE | address | (val >> 8),
					 val & 0xff);
}

static const struct iio_info mcp4531_info = {
	.read_raw = mcp4531_read_raw,
	.write_raw = mcp4531_write_raw,
	.driver_module = THIS_MODULE,
};

static int mcp4531_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	unsigned long devid = id->driver_data;
	struct mcp4531_data *data;
	struct iio_dev *indio_dev;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_WORD_DATA)) {
		dev_err(dev, "SMBUS Word Data not supported\n");
		return -EIO;
	}

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;
	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	data->devid = devid;

	indio_dev->dev.parent = dev;
	indio_dev->info = &mcp4531_info;
	indio_dev->channels = mcp4531_channels;
	indio_dev->num_channels = mcp4531_cfg[devid].wipers;
	indio_dev->name = client->name;

	return devm_iio_device_register(dev, indio_dev);
}

static const struct i2c_device_id mcp4531_id[] = {
	{ "mcp4531-502", MCP453x_502 },
	{ "mcp4531-103", MCP453x_103 },
	{ "mcp4531-503", MCP453x_503 },
	{ "mcp4531-104", MCP453x_104 },
	{ "mcp4532-502", MCP453x_502 },
	{ "mcp4532-103", MCP453x_103 },
	{ "mcp4532-503", MCP453x_503 },
	{ "mcp4532-104", MCP453x_104 },
	{ "mcp4551-502", MCP455x_502 },
	{ "mcp4551-103", MCP455x_103 },
	{ "mcp4551-503", MCP455x_503 },
	{ "mcp4551-104", MCP455x_104 },
	{ "mcp4552-502", MCP455x_502 },
	{ "mcp4552-103", MCP455x_103 },
	{ "mcp4552-503", MCP455x_503 },
	{ "mcp4552-104", MCP455x_104 },
	{ "mcp4631-502", MCP463x_502 },
	{ "mcp4631-103", MCP463x_103 },
	{ "mcp4631-503", MCP463x_503 },
	{ "mcp4631-104", MCP463x_104 },
	{ "mcp4632-502", MCP463x_502 },
	{ "mcp4632-103", MCP463x_103 },
	{ "mcp4632-503", MCP463x_503 },
	{ "mcp4632-104", MCP463x_104 },
	{ "mcp4651-502", MCP465x_502 },
	{ "mcp4651-103", MCP465x_103 },
	{ "mcp4651-503", MCP465x_503 },
	{ "mcp4651-104", MCP465x_104 },
	{ "mcp4652-502", MCP465x_502 },
	{ "mcp4652-103", MCP465x_103 },
	{ "mcp4652-503", MCP465x_503 },
	{ "mcp4652-104", MCP465x_104 },
	{}
};
MODULE_DEVICE_TABLE(i2c, mcp4531_id);

static struct i2c_driver mcp4531_driver = {
	.driver = {
		.name	= "mcp4531",
	},
	.probe		= mcp4531_probe,
	.id_table	= mcp4531_id,
};

module_i2c_driver(mcp4531_driver);

MODULE_AUTHOR("Peter Rosin <peda@axentia.se>");
MODULE_DESCRIPTION("MCP4531 digital potentiometer");
MODULE_LICENSE("GPL");
