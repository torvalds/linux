// SPDX-License-Identifier: GPL-2.0+
/*
 * tpl0102.c - Support for Texas Instruments digital potentiometers
 *
 * Copyright (C) 2016, 2018
 * Author: Matt Ranostay <matt.ranostay@konsulko.com>
 *
 * TODO: enable/disable hi-z output control
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/iio/iio.h>

struct tpl0102_cfg {
	int wipers;
	int avail[3];
	int kohms;
};

enum tpl0102_type {
	CAT5140_503,
	CAT5140_104,
	TPL0102_104,
	TPL0401_103,
};

static const struct tpl0102_cfg tpl0102_cfg[] = {
	/* on-semiconductor parts */
	[CAT5140_503] = { .wipers = 1, .avail = { 0, 1, 255 }, .kohms = 50, },
	[CAT5140_104] = { .wipers = 1, .avail = { 0, 1, 255 }, .kohms = 100, },
	/* ti parts */
	[TPL0102_104] = { .wipers = 2, .avail = { 0, 1, 255 }, .kohms = 100 },
	[TPL0401_103] = { .wipers = 1, .avail = { 0, 1, 127 }, .kohms = 10, },
};

struct tpl0102_data {
	struct regmap *regmap;
	const struct tpl0102_cfg *cfg;
};

static const struct regmap_config tpl0102_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

#define TPL0102_CHANNEL(ch) {					\
	.type = IIO_RESISTANCE,					\
	.indexed = 1,						\
	.output = 1,						\
	.channel = (ch),					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
	.info_mask_separate_available = BIT(IIO_CHAN_INFO_RAW),	\
}

static const struct iio_chan_spec tpl0102_channels[] = {
	TPL0102_CHANNEL(0),
	TPL0102_CHANNEL(1),
};

static int tpl0102_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct tpl0102_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW: {
		int ret = regmap_read(data->regmap, chan->channel, val);

		return ret ? ret : IIO_VAL_INT;
	}
	case IIO_CHAN_INFO_SCALE:
		*val = 1000 * data->cfg->kohms;
		*val2 = data->cfg->avail[2] + 1;
		return IIO_VAL_FRACTIONAL;
	}

	return -EINVAL;
}

static int tpl0102_read_avail(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      const int **vals, int *type, int *length,
			      long mask)
{
	struct tpl0102_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		*length = ARRAY_SIZE(data->cfg->avail);
		*vals = data->cfg->avail;
		*type = IIO_VAL_INT;
		return IIO_AVAIL_RANGE;
	}

	return -EINVAL;
}

static int tpl0102_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	struct tpl0102_data *data = iio_priv(indio_dev);

	if (mask != IIO_CHAN_INFO_RAW)
		return -EINVAL;

	if (val > data->cfg->avail[2] || val < 0)
		return -EINVAL;

	return regmap_write(data->regmap, chan->channel, val);
}

static const struct iio_info tpl0102_info = {
	.read_raw = tpl0102_read_raw,
	.read_avail = tpl0102_read_avail,
	.write_raw = tpl0102_write_raw,
};

static int tpl0102_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct tpl0102_data *data;
	struct iio_dev *indio_dev;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;
	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);

	data->cfg = &tpl0102_cfg[id->driver_data];
	data->regmap = devm_regmap_init_i2c(client, &tpl0102_regmap_config);
	if (IS_ERR(data->regmap)) {
		dev_err(dev, "regmap initialization failed\n");
		return PTR_ERR(data->regmap);
	}

	indio_dev->dev.parent = dev;
	indio_dev->info = &tpl0102_info;
	indio_dev->channels = tpl0102_channels;
	indio_dev->num_channels = data->cfg->wipers;
	indio_dev->name = client->name;

	return devm_iio_device_register(dev, indio_dev);
}

static const struct i2c_device_id tpl0102_id[] = {
	{ "cat5140-503", CAT5140_503 },
	{ "cat5140-104", CAT5140_104 },
	{ "tpl0102-104", TPL0102_104 },
	{ "tpl0401-103", TPL0401_103 },
	{}
};
MODULE_DEVICE_TABLE(i2c, tpl0102_id);

static struct i2c_driver tpl0102_driver = {
	.driver = {
		.name = "tpl0102",
	},
	.probe = tpl0102_probe,
	.id_table = tpl0102_id,
};

module_i2c_driver(tpl0102_driver);

MODULE_AUTHOR("Matt Ranostay <matt.ranostay@konsulko.com>");
MODULE_DESCRIPTION("TPL0102 digital potentiometer");
MODULE_LICENSE("GPL");
