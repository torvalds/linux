// SPDX-License-Identifier: GPL-2.0
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
 * mcp4541	1	129		5, 10, 50, 100          010111x
 * mcp4542	1	129		5, 10, 50, 100          01011xx
 * mcp4551	1	257		5, 10, 50, 100          010111x
 * mcp4552	1	257		5, 10, 50, 100          01011xx
 * mcp4561	1	257		5, 10, 50, 100          010111x
 * mcp4562	1	257		5, 10, 50, 100          01011xx
 * mcp4631	2	129		5, 10, 50, 100          0101xxx
 * mcp4632	2	129		5, 10, 50, 100          01011xx
 * mcp4641	2	129		5, 10, 50, 100          0101xxx
 * mcp4642	2	129		5, 10, 50, 100          01011xx
 * mcp4651	2	257		5, 10, 50, 100          0101xxx
 * mcp4652	2	257		5, 10, 50, 100          01011xx
 * mcp4661	2	257		5, 10, 50, 100          0101xxx
 * mcp4662	2	257		5, 10, 50, 100          01011xx
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <linux/iio/iio.h>

struct mcp4531_cfg {
	int wipers;
	int avail[3];
	int kohms;
};

enum mcp4531_type {
	MCP453x_502,
	MCP453x_103,
	MCP453x_503,
	MCP453x_104,
	MCP454x_502,
	MCP454x_103,
	MCP454x_503,
	MCP454x_104,
	MCP455x_502,
	MCP455x_103,
	MCP455x_503,
	MCP455x_104,
	MCP456x_502,
	MCP456x_103,
	MCP456x_503,
	MCP456x_104,
	MCP463x_502,
	MCP463x_103,
	MCP463x_503,
	MCP463x_104,
	MCP464x_502,
	MCP464x_103,
	MCP464x_503,
	MCP464x_104,
	MCP465x_502,
	MCP465x_103,
	MCP465x_503,
	MCP465x_104,
	MCP466x_502,
	MCP466x_103,
	MCP466x_503,
	MCP466x_104,
};

static const struct mcp4531_cfg mcp4531_cfg[] = {
	[MCP453x_502] = { .wipers = 1, .avail = { 0, 1, 128 }, .kohms =   5, },
	[MCP453x_103] = { .wipers = 1, .avail = { 0, 1, 128 }, .kohms =  10, },
	[MCP453x_503] = { .wipers = 1, .avail = { 0, 1, 128 }, .kohms =  50, },
	[MCP453x_104] = { .wipers = 1, .avail = { 0, 1, 128 }, .kohms = 100, },
	[MCP454x_502] = { .wipers = 1, .avail = { 0, 1, 128 }, .kohms =   5, },
	[MCP454x_103] = { .wipers = 1, .avail = { 0, 1, 128 }, .kohms =  10, },
	[MCP454x_503] = { .wipers = 1, .avail = { 0, 1, 128 }, .kohms =  50, },
	[MCP454x_104] = { .wipers = 1, .avail = { 0, 1, 128 }, .kohms = 100, },
	[MCP455x_502] = { .wipers = 1, .avail = { 0, 1, 256 }, .kohms =   5, },
	[MCP455x_103] = { .wipers = 1, .avail = { 0, 1, 256 }, .kohms =  10, },
	[MCP455x_503] = { .wipers = 1, .avail = { 0, 1, 256 }, .kohms =  50, },
	[MCP455x_104] = { .wipers = 1, .avail = { 0, 1, 256 }, .kohms = 100, },
	[MCP456x_502] = { .wipers = 1, .avail = { 0, 1, 256 }, .kohms =   5, },
	[MCP456x_103] = { .wipers = 1, .avail = { 0, 1, 256 }, .kohms =  10, },
	[MCP456x_503] = { .wipers = 1, .avail = { 0, 1, 256 }, .kohms =  50, },
	[MCP456x_104] = { .wipers = 1, .avail = { 0, 1, 256 }, .kohms = 100, },
	[MCP463x_502] = { .wipers = 2, .avail = { 0, 1, 128 }, .kohms =   5, },
	[MCP463x_103] = { .wipers = 2, .avail = { 0, 1, 128 }, .kohms =  10, },
	[MCP463x_503] = { .wipers = 2, .avail = { 0, 1, 128 }, .kohms =  50, },
	[MCP463x_104] = { .wipers = 2, .avail = { 0, 1, 128 }, .kohms = 100, },
	[MCP464x_502] = { .wipers = 2, .avail = { 0, 1, 128 }, .kohms =   5, },
	[MCP464x_103] = { .wipers = 2, .avail = { 0, 1, 128 }, .kohms =  10, },
	[MCP464x_503] = { .wipers = 2, .avail = { 0, 1, 128 }, .kohms =  50, },
	[MCP464x_104] = { .wipers = 2, .avail = { 0, 1, 128 }, .kohms = 100, },
	[MCP465x_502] = { .wipers = 2, .avail = { 0, 1, 256 }, .kohms =   5, },
	[MCP465x_103] = { .wipers = 2, .avail = { 0, 1, 256 }, .kohms =  10, },
	[MCP465x_503] = { .wipers = 2, .avail = { 0, 1, 256 }, .kohms =  50, },
	[MCP465x_104] = { .wipers = 2, .avail = { 0, 1, 256 }, .kohms = 100, },
	[MCP466x_502] = { .wipers = 2, .avail = { 0, 1, 256 }, .kohms =   5, },
	[MCP466x_103] = { .wipers = 2, .avail = { 0, 1, 256 }, .kohms =  10, },
	[MCP466x_503] = { .wipers = 2, .avail = { 0, 1, 256 }, .kohms =  50, },
	[MCP466x_104] = { .wipers = 2, .avail = { 0, 1, 256 }, .kohms = 100, },
};

#define MCP4531_WRITE (0 << 2)
#define MCP4531_INCR  (1 << 2)
#define MCP4531_DECR  (2 << 2)
#define MCP4531_READ  (3 << 2)

#define MCP4531_WIPER_SHIFT (4)

struct mcp4531_data {
	struct i2c_client *client;
	const struct mcp4531_cfg *cfg;
};

#define MCP4531_CHANNEL(ch) {						\
	.type = IIO_RESISTANCE,						\
	.indexed = 1,							\
	.output = 1,							\
	.channel = (ch),						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),			\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),		\
	.info_mask_shared_by_type_available = BIT(IIO_CHAN_INFO_RAW),	\
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
		*val = 1000 * data->cfg->kohms;
		*val2 = data->cfg->avail[2];
		return IIO_VAL_FRACTIONAL;
	}

	return -EINVAL;
}

static int mcp4531_read_avail(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      const int **vals, int *type, int *length,
			      long mask)
{
	struct mcp4531_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		*length = ARRAY_SIZE(data->cfg->avail);
		*vals = data->cfg->avail;
		*type = IIO_VAL_INT;
		return IIO_AVAIL_RANGE;
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
		if (val > data->cfg->avail[2] || val < 0)
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
	.read_avail = mcp4531_read_avail,
	.write_raw = mcp4531_write_raw,
};

static const struct i2c_device_id mcp4531_id[] = {
	{ "mcp4531-502", MCP453x_502 },
	{ "mcp4531-103", MCP453x_103 },
	{ "mcp4531-503", MCP453x_503 },
	{ "mcp4531-104", MCP453x_104 },
	{ "mcp4532-502", MCP453x_502 },
	{ "mcp4532-103", MCP453x_103 },
	{ "mcp4532-503", MCP453x_503 },
	{ "mcp4532-104", MCP453x_104 },
	{ "mcp4541-502", MCP454x_502 },
	{ "mcp4541-103", MCP454x_103 },
	{ "mcp4541-503", MCP454x_503 },
	{ "mcp4541-104", MCP454x_104 },
	{ "mcp4542-502", MCP454x_502 },
	{ "mcp4542-103", MCP454x_103 },
	{ "mcp4542-503", MCP454x_503 },
	{ "mcp4542-104", MCP454x_104 },
	{ "mcp4551-502", MCP455x_502 },
	{ "mcp4551-103", MCP455x_103 },
	{ "mcp4551-503", MCP455x_503 },
	{ "mcp4551-104", MCP455x_104 },
	{ "mcp4552-502", MCP455x_502 },
	{ "mcp4552-103", MCP455x_103 },
	{ "mcp4552-503", MCP455x_503 },
	{ "mcp4552-104", MCP455x_104 },
	{ "mcp4561-502", MCP456x_502 },
	{ "mcp4561-103", MCP456x_103 },
	{ "mcp4561-503", MCP456x_503 },
	{ "mcp4561-104", MCP456x_104 },
	{ "mcp4562-502", MCP456x_502 },
	{ "mcp4562-103", MCP456x_103 },
	{ "mcp4562-503", MCP456x_503 },
	{ "mcp4562-104", MCP456x_104 },
	{ "mcp4631-502", MCP463x_502 },
	{ "mcp4631-103", MCP463x_103 },
	{ "mcp4631-503", MCP463x_503 },
	{ "mcp4631-104", MCP463x_104 },
	{ "mcp4632-502", MCP463x_502 },
	{ "mcp4632-103", MCP463x_103 },
	{ "mcp4632-503", MCP463x_503 },
	{ "mcp4632-104", MCP463x_104 },
	{ "mcp4641-502", MCP464x_502 },
	{ "mcp4641-103", MCP464x_103 },
	{ "mcp4641-503", MCP464x_503 },
	{ "mcp4641-104", MCP464x_104 },
	{ "mcp4642-502", MCP464x_502 },
	{ "mcp4642-103", MCP464x_103 },
	{ "mcp4642-503", MCP464x_503 },
	{ "mcp4642-104", MCP464x_104 },
	{ "mcp4651-502", MCP465x_502 },
	{ "mcp4651-103", MCP465x_103 },
	{ "mcp4651-503", MCP465x_503 },
	{ "mcp4651-104", MCP465x_104 },
	{ "mcp4652-502", MCP465x_502 },
	{ "mcp4652-103", MCP465x_103 },
	{ "mcp4652-503", MCP465x_503 },
	{ "mcp4652-104", MCP465x_104 },
	{ "mcp4661-502", MCP466x_502 },
	{ "mcp4661-103", MCP466x_103 },
	{ "mcp4661-503", MCP466x_503 },
	{ "mcp4661-104", MCP466x_104 },
	{ "mcp4662-502", MCP466x_502 },
	{ "mcp4662-103", MCP466x_103 },
	{ "mcp4662-503", MCP466x_503 },
	{ "mcp4662-104", MCP466x_104 },
	{}
};
MODULE_DEVICE_TABLE(i2c, mcp4531_id);

#ifdef CONFIG_OF

#define MCP4531_COMPATIBLE(of_compatible, cfg) {	\
			.compatible = of_compatible,	\
			.data = &mcp4531_cfg[cfg],	\
}

static const struct of_device_id mcp4531_of_match[] = {
	MCP4531_COMPATIBLE("microchip,mcp4531-502", MCP453x_502),
	MCP4531_COMPATIBLE("microchip,mcp4531-103", MCP453x_103),
	MCP4531_COMPATIBLE("microchip,mcp4531-503", MCP453x_503),
	MCP4531_COMPATIBLE("microchip,mcp4531-104", MCP453x_104),
	MCP4531_COMPATIBLE("microchip,mcp4532-502", MCP453x_502),
	MCP4531_COMPATIBLE("microchip,mcp4532-103", MCP453x_103),
	MCP4531_COMPATIBLE("microchip,mcp4532-503", MCP453x_503),
	MCP4531_COMPATIBLE("microchip,mcp4532-104", MCP453x_104),
	MCP4531_COMPATIBLE("microchip,mcp4541-502", MCP454x_502),
	MCP4531_COMPATIBLE("microchip,mcp4541-103", MCP454x_103),
	MCP4531_COMPATIBLE("microchip,mcp4541-503", MCP454x_503),
	MCP4531_COMPATIBLE("microchip,mcp4541-104", MCP454x_104),
	MCP4531_COMPATIBLE("microchip,mcp4542-502", MCP454x_502),
	MCP4531_COMPATIBLE("microchip,mcp4542-103", MCP454x_103),
	MCP4531_COMPATIBLE("microchip,mcp4542-503", MCP454x_503),
	MCP4531_COMPATIBLE("microchip,mcp4542-104", MCP454x_104),
	MCP4531_COMPATIBLE("microchip,mcp4551-502", MCP455x_502),
	MCP4531_COMPATIBLE("microchip,mcp4551-103", MCP455x_103),
	MCP4531_COMPATIBLE("microchip,mcp4551-503", MCP455x_503),
	MCP4531_COMPATIBLE("microchip,mcp4551-104", MCP455x_104),
	MCP4531_COMPATIBLE("microchip,mcp4552-502", MCP455x_502),
	MCP4531_COMPATIBLE("microchip,mcp4552-103", MCP455x_103),
	MCP4531_COMPATIBLE("microchip,mcp4552-503", MCP455x_503),
	MCP4531_COMPATIBLE("microchip,mcp4552-104", MCP455x_104),
	MCP4531_COMPATIBLE("microchip,mcp4561-502", MCP456x_502),
	MCP4531_COMPATIBLE("microchip,mcp4561-103", MCP456x_103),
	MCP4531_COMPATIBLE("microchip,mcp4561-503", MCP456x_503),
	MCP4531_COMPATIBLE("microchip,mcp4561-104", MCP456x_104),
	MCP4531_COMPATIBLE("microchip,mcp4562-502", MCP456x_502),
	MCP4531_COMPATIBLE("microchip,mcp4562-103", MCP456x_103),
	MCP4531_COMPATIBLE("microchip,mcp4562-503", MCP456x_503),
	MCP4531_COMPATIBLE("microchip,mcp4562-104", MCP456x_104),
	MCP4531_COMPATIBLE("microchip,mcp4631-502", MCP463x_502),
	MCP4531_COMPATIBLE("microchip,mcp4631-103", MCP463x_103),
	MCP4531_COMPATIBLE("microchip,mcp4631-503", MCP463x_503),
	MCP4531_COMPATIBLE("microchip,mcp4631-104", MCP463x_104),
	MCP4531_COMPATIBLE("microchip,mcp4632-502", MCP463x_502),
	MCP4531_COMPATIBLE("microchip,mcp4632-103", MCP463x_103),
	MCP4531_COMPATIBLE("microchip,mcp4632-503", MCP463x_503),
	MCP4531_COMPATIBLE("microchip,mcp4632-104", MCP463x_104),
	MCP4531_COMPATIBLE("microchip,mcp4641-502", MCP464x_502),
	MCP4531_COMPATIBLE("microchip,mcp4641-103", MCP464x_103),
	MCP4531_COMPATIBLE("microchip,mcp4641-503", MCP464x_503),
	MCP4531_COMPATIBLE("microchip,mcp4641-104", MCP464x_104),
	MCP4531_COMPATIBLE("microchip,mcp4642-502", MCP464x_502),
	MCP4531_COMPATIBLE("microchip,mcp4642-103", MCP464x_103),
	MCP4531_COMPATIBLE("microchip,mcp4642-503", MCP464x_503),
	MCP4531_COMPATIBLE("microchip,mcp4642-104", MCP464x_104),
	MCP4531_COMPATIBLE("microchip,mcp4651-502", MCP465x_502),
	MCP4531_COMPATIBLE("microchip,mcp4651-103", MCP465x_103),
	MCP4531_COMPATIBLE("microchip,mcp4651-503", MCP465x_503),
	MCP4531_COMPATIBLE("microchip,mcp4651-104", MCP465x_104),
	MCP4531_COMPATIBLE("microchip,mcp4652-502", MCP465x_502),
	MCP4531_COMPATIBLE("microchip,mcp4652-103", MCP465x_103),
	MCP4531_COMPATIBLE("microchip,mcp4652-503", MCP465x_503),
	MCP4531_COMPATIBLE("microchip,mcp4652-104", MCP465x_104),
	MCP4531_COMPATIBLE("microchip,mcp4661-502", MCP466x_502),
	MCP4531_COMPATIBLE("microchip,mcp4661-103", MCP466x_103),
	MCP4531_COMPATIBLE("microchip,mcp4661-503", MCP466x_503),
	MCP4531_COMPATIBLE("microchip,mcp4661-104", MCP466x_104),
	MCP4531_COMPATIBLE("microchip,mcp4662-502", MCP466x_502),
	MCP4531_COMPATIBLE("microchip,mcp4662-103", MCP466x_103),
	MCP4531_COMPATIBLE("microchip,mcp4662-503", MCP466x_503),
	MCP4531_COMPATIBLE("microchip,mcp4662-104", MCP466x_104),
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mcp4531_of_match);
#endif

static int mcp4531_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct mcp4531_data *data;
	struct iio_dev *indio_dev;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_WORD_DATA)) {
		dev_err(dev, "SMBUS Word Data not supported\n");
		return -EOPNOTSUPP;
	}

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;
	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;

	data->cfg = of_device_get_match_data(dev);
	if (!data->cfg)
		data->cfg = &mcp4531_cfg[i2c_match_id(mcp4531_id, client)->driver_data];

	indio_dev->dev.parent = dev;
	indio_dev->info = &mcp4531_info;
	indio_dev->channels = mcp4531_channels;
	indio_dev->num_channels = data->cfg->wipers;
	indio_dev->name = client->name;

	return devm_iio_device_register(dev, indio_dev);
}

static struct i2c_driver mcp4531_driver = {
	.driver = {
		.name	= "mcp4531",
		.of_match_table = of_match_ptr(mcp4531_of_match),
	},
	.probe_new	= mcp4531_probe,
	.id_table	= mcp4531_id,
};

module_i2c_driver(mcp4531_driver);

MODULE_AUTHOR("Peter Rosin <peda@axentia.se>");
MODULE_DESCRIPTION("MCP4531 digital potentiometer");
MODULE_LICENSE("GPL v2");
