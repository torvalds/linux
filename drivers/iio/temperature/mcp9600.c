// SPDX-License-Identifier: GPL-2.0+
/*
 * mcp9600.c - Support for Microchip MCP9600 thermocouple EMF converter
 *
 * Copyright (c) 2022 Andrew Hepp
 * Author: <andrew.hepp@ahepp.dev>
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>

#include <linux/iio/iio.h>

/* MCP9600 registers */
#define MCP9600_HOT_JUNCTION 0x0
#define MCP9600_COLD_JUNCTION 0x2
#define MCP9600_DEVICE_ID 0x20

/* MCP9600 device id value */
#define MCP9600_DEVICE_ID_MCP9600 0x40

static const struct iio_chan_spec mcp9600_channels[] = {
	{
		.type = IIO_TEMP,
		.address = MCP9600_HOT_JUNCTION,
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
	},
	{
		.type = IIO_TEMP,
		.address = MCP9600_COLD_JUNCTION,
		.channel2 = IIO_MOD_TEMP_AMBIENT,
		.modified = 1,
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
	},
};

struct mcp9600_data {
	struct i2c_client *client;
};

static int mcp9600_read(struct mcp9600_data *data,
			struct iio_chan_spec const *chan, int *val)
{
	int ret;

	ret = i2c_smbus_read_word_swapped(data->client, chan->address);

	if (ret < 0)
		return ret;
	*val = ret;

	return 0;
}

static int mcp9600_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int *val,
			    int *val2, long mask)
{
	struct mcp9600_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = mcp9600_read(data, chan, val);
		if (ret)
			return ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 62;
		*val2 = 500000;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static const struct iio_info mcp9600_info = {
	.read_raw = mcp9600_read_raw,
};

static int mcp9600_probe(struct i2c_client *client)
{
	struct iio_dev *indio_dev;
	struct mcp9600_data *data;
	int ret;

	ret = i2c_smbus_read_byte_data(client, MCP9600_DEVICE_ID);
	if (ret < 0)
		return dev_err_probe(&client->dev, ret, "Failed to read device ID\n");
	if (ret != MCP9600_DEVICE_ID_MCP9600)
		dev_warn(&client->dev, "Expected ID %x, got %x\n",
				MCP9600_DEVICE_ID_MCP9600, ret);

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->client = client;

	indio_dev->info = &mcp9600_info;
	indio_dev->name = "mcp9600";
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = mcp9600_channels;
	indio_dev->num_channels = ARRAY_SIZE(mcp9600_channels);

	return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct i2c_device_id mcp9600_id[] = {
	{ "mcp9600" },
	{}
};
MODULE_DEVICE_TABLE(i2c, mcp9600_id);

static const struct of_device_id mcp9600_of_match[] = {
	{ .compatible = "microchip,mcp9600" },
	{}
};
MODULE_DEVICE_TABLE(of, mcp9600_of_match);

static struct i2c_driver mcp9600_driver = {
	.driver = {
		.name = "mcp9600",
		.of_match_table = mcp9600_of_match,
	},
	.probe = mcp9600_probe,
	.id_table = mcp9600_id
};
module_i2c_driver(mcp9600_driver);

MODULE_AUTHOR("Andrew Hepp <andrew.hepp@ahepp.dev>");
MODULE_DESCRIPTION("Microchip MCP9600 thermocouple EMF converter driver");
MODULE_LICENSE("GPL");
