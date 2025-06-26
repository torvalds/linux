// SPDX-License-Identifier: GPL-2.0-only
/*
 * Maxim Integrated DS1803 and similar digital potentiometer driver
 * Copyright (c) 2016 Slawomir Stepien
 * Copyright (c) 2022 Jagath Jog J
 *
 * Datasheet: https://datasheets.maximintegrated.com/en/ds/DS1803.pdf
 * Datasheet: https://datasheets.maximintegrated.com/en/ds/DS3502.pdf
 *
 * DEVID	#Wipers	#Positions	Resistor Opts (kOhm)	i2c address
 * ds1803	2	256		10, 50, 100		0101xxx
 * ds3502	1	128		10			01010xx
 */

#include <linux/err.h>
#include <linux/export.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>

#define DS1803_WIPER_0         0xA9
#define DS1803_WIPER_1         0xAA
#define DS3502_WR_IVR          0x00

enum ds1803_type {
	DS1803_010,
	DS1803_050,
	DS1803_100,
	DS3502,
};

struct ds1803_cfg {
	int wipers;
	int avail[3];
	int kohms;
	const struct iio_chan_spec *channels;
	u8 num_channels;
	int (*read)(struct iio_dev *indio_dev,
		    struct iio_chan_spec const *chan, int *val);
};

struct ds1803_data {
	struct i2c_client *client;
	const struct ds1803_cfg *cfg;
};

#define DS1803_CHANNEL(ch, addr) {					\
	.type = IIO_RESISTANCE,						\
	.indexed = 1,							\
	.output = 1,							\
	.channel = (ch),						\
	.address = (addr),						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),			\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),		\
	.info_mask_shared_by_type_available = BIT(IIO_CHAN_INFO_RAW),   \
}

static const struct iio_chan_spec ds1803_channels[] = {
	DS1803_CHANNEL(0, DS1803_WIPER_0),
	DS1803_CHANNEL(1, DS1803_WIPER_1),
};

static const struct iio_chan_spec ds3502_channels[] = {
	DS1803_CHANNEL(0, DS3502_WR_IVR),
};

static int ds1803_read(struct iio_dev *indio_dev,
		       struct iio_chan_spec const *chan,
		       int *val)
{
	struct ds1803_data *data = iio_priv(indio_dev);
	int ret;
	u8 result[ARRAY_SIZE(ds1803_channels)];

	ret = i2c_master_recv(data->client, result, indio_dev->num_channels);
	if (ret < 0)
		return ret;

	*val = result[chan->channel];
	return ret;
}

static int ds3502_read(struct iio_dev *indio_dev,
		       struct iio_chan_spec const *chan,
		       int *val)
{
	struct ds1803_data *data = iio_priv(indio_dev);
	int ret;

	ret = i2c_smbus_read_byte_data(data->client, chan->address);
	if (ret < 0)
		return ret;

	*val = ret;
	return ret;
}

static const struct ds1803_cfg ds1803_cfg[] = {
	[DS1803_010] = {
		.wipers = 2,
		.avail = { 0, 1, 255 },
		.kohms =  10,
		.channels = ds1803_channels,
		.num_channels = ARRAY_SIZE(ds1803_channels),
		.read = ds1803_read,
	},
	[DS1803_050] = {
		.wipers = 2,
		.avail = { 0, 1, 255 },
		.kohms =  50,
		.channels = ds1803_channels,
		.num_channels = ARRAY_SIZE(ds1803_channels),
		.read = ds1803_read,
	},
	[DS1803_100] = {
		.wipers = 2,
		.avail = { 0, 1, 255 },
		.kohms = 100,
		.channels = ds1803_channels,
		.num_channels = ARRAY_SIZE(ds1803_channels),
		.read = ds1803_read,
	},
	[DS3502] = {
	  .wipers = 1,
	  .avail = { 0, 1, 127 },
	  .kohms =  10,
	  .channels = ds3502_channels,
	  .num_channels = ARRAY_SIZE(ds3502_channels),
	  .read = ds3502_read,
	},
};

static int ds1803_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct ds1803_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = data->cfg->read(indio_dev, chan, val);
		if (ret < 0)
			return ret;

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		*val = 1000 * data->cfg->kohms;
		*val2 = data->cfg->avail[2]; /* Max wiper position */
		return IIO_VAL_FRACTIONAL;
	}

	return -EINVAL;
}

static int ds1803_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	struct ds1803_data *data = iio_priv(indio_dev);
	u8 addr = chan->address;
	int max_pos = data->cfg->avail[2];

	if (val2 != 0)
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (val > max_pos || val < 0)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	return i2c_smbus_write_byte_data(data->client, addr, val);
}

static int ds1803_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     const int **vals, int *type,
			     int *length, long mask)
{
	struct ds1803_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		*vals = data->cfg->avail;
		*length = ARRAY_SIZE(data->cfg->avail);
		*type = IIO_VAL_INT;
		return IIO_AVAIL_RANGE;
	}
	return -EINVAL;
}

static const struct iio_info ds1803_info = {
	.read_raw = ds1803_read_raw,
	.write_raw = ds1803_write_raw,
	.read_avail = ds1803_read_avail,
};

static int ds1803_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ds1803_data *data;
	struct iio_dev *indio_dev;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	i2c_set_clientdata(client, indio_dev);

	data = iio_priv(indio_dev);
	data->client = client;
	data->cfg = i2c_get_match_data(client);

	indio_dev->info = &ds1803_info;
	indio_dev->channels = data->cfg->channels;
	indio_dev->num_channels = data->cfg->num_channels;
	indio_dev->name = client->name;

	return devm_iio_device_register(dev, indio_dev);
}

static const struct of_device_id ds1803_dt_ids[] = {
	{ .compatible = "maxim,ds1803-010", .data = &ds1803_cfg[DS1803_010] },
	{ .compatible = "maxim,ds1803-050", .data = &ds1803_cfg[DS1803_050] },
	{ .compatible = "maxim,ds1803-100", .data = &ds1803_cfg[DS1803_100] },
	{ .compatible = "maxim,ds3502", .data = &ds1803_cfg[DS3502] },
	{ }
};
MODULE_DEVICE_TABLE(of, ds1803_dt_ids);

static const struct i2c_device_id ds1803_id[] = {
	{ "ds1803-010", (kernel_ulong_t)&ds1803_cfg[DS1803_010] },
	{ "ds1803-050", (kernel_ulong_t)&ds1803_cfg[DS1803_050] },
	{ "ds1803-100", (kernel_ulong_t)&ds1803_cfg[DS1803_100] },
	{ "ds3502", (kernel_ulong_t)&ds1803_cfg[DS3502] },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ds1803_id);

static struct i2c_driver ds1803_driver = {
	.driver = {
		.name	= "ds1803",
		.of_match_table = ds1803_dt_ids,
	},
	.probe		= ds1803_probe,
	.id_table	= ds1803_id,
};

module_i2c_driver(ds1803_driver);

MODULE_AUTHOR("Slawomir Stepien <sst@poczta.fm>");
MODULE_AUTHOR("Jagath Jog J <jagathjog1996@gmail.com>");
MODULE_DESCRIPTION("DS1803 digital potentiometer");
MODULE_LICENSE("GPL v2");
