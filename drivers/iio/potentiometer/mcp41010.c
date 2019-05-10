// SPDX-License-Identifier: GPL-2.0
/*
 * Industrial I/O driver for Microchip digital potentiometers
 *
 * Copyright (c) 2018 Chris Coffey <cmc@babblebit.net>
 * Based on: Slawomir Stepien's code from mcp4131.c
 *
 * Datasheet: http://ww1.microchip.com/downloads/en/devicedoc/11195c.pdf
 *
 * DEVID	#Wipers	#Positions	Resistance (kOhm)
 * mcp41010	1	256		10
 * mcp41050	1	256		50
 * mcp41100	1	256		100
 * mcp42010	2	256		10
 * mcp42050	2	256		50
 * mcp42100	2	256		100
 */

#include <linux/cache.h>
#include <linux/err.h>
#include <linux/iio/iio.h>
#include <linux/iio/types.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/spi/spi.h>

#define MCP41010_MAX_WIPERS	2
#define MCP41010_WRITE		BIT(4)
#define MCP41010_WIPER_MAX	255
#define MCP41010_WIPER_CHANNEL	BIT(0)

struct mcp41010_cfg {
	char name[16];
	int wipers;
	int kohms;
};

enum mcp41010_type {
	MCP41010,
	MCP41050,
	MCP41100,
	MCP42010,
	MCP42050,
	MCP42100,
};

static const struct mcp41010_cfg mcp41010_cfg[] = {
	[MCP41010] = { .name = "mcp41010", .wipers = 1, .kohms =  10, },
	[MCP41050] = { .name = "mcp41050", .wipers = 1, .kohms =  50, },
	[MCP41100] = { .name = "mcp41100", .wipers = 1, .kohms = 100, },
	[MCP42010] = { .name = "mcp42010", .wipers = 2, .kohms =  10, },
	[MCP42050] = { .name = "mcp42050", .wipers = 2, .kohms =  50, },
	[MCP42100] = { .name = "mcp42100", .wipers = 2, .kohms = 100, },
};

struct mcp41010_data {
	struct spi_device *spi;
	const struct mcp41010_cfg *cfg;
	struct mutex lock; /* Protect write sequences */
	unsigned int value[MCP41010_MAX_WIPERS]; /* Cache wiper values */
	u8 buf[2] ____cacheline_aligned;
};

#define MCP41010_CHANNEL(ch) {					\
	.type = IIO_RESISTANCE,					\
	.indexed = 1,						\
	.output = 1,						\
	.channel = (ch),					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
}

static const struct iio_chan_spec mcp41010_channels[] = {
	MCP41010_CHANNEL(0),
	MCP41010_CHANNEL(1),
};

static int mcp41010_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long mask)
{
	struct mcp41010_data *data = iio_priv(indio_dev);
	int channel = chan->channel;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		*val = data->value[channel];
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		*val = 1000 * data->cfg->kohms;
		*val2 = MCP41010_WIPER_MAX;
		return IIO_VAL_FRACTIONAL;
	}

	return -EINVAL;
}

static int mcp41010_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long mask)
{
	int err;
	struct mcp41010_data *data = iio_priv(indio_dev);
	int channel = chan->channel;

	if (mask != IIO_CHAN_INFO_RAW)
		return -EINVAL;

	if (val > MCP41010_WIPER_MAX || val < 0)
		return -EINVAL;

	mutex_lock(&data->lock);

	data->buf[0] = MCP41010_WIPER_CHANNEL << channel;
	data->buf[0] |= MCP41010_WRITE;
	data->buf[1] = val & 0xff;

	err = spi_write(data->spi, data->buf, sizeof(data->buf));
	if (!err)
		data->value[channel] = val;

	mutex_unlock(&data->lock);

	return err;
}

static const struct iio_info mcp41010_info = {
	.read_raw = mcp41010_read_raw,
	.write_raw = mcp41010_write_raw,
};

static int mcp41010_probe(struct spi_device *spi)
{
	int err;
	struct device *dev = &spi->dev;
	struct mcp41010_data *data;
	struct iio_dev *indio_dev;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	spi_set_drvdata(spi, indio_dev);
	data->spi = spi;
	data->cfg = of_device_get_match_data(&spi->dev);
	if (!data->cfg)
		data->cfg = &mcp41010_cfg[spi_get_device_id(spi)->driver_data];

	mutex_init(&data->lock);

	indio_dev->dev.parent = dev;
	indio_dev->info = &mcp41010_info;
	indio_dev->channels = mcp41010_channels;
	indio_dev->num_channels = data->cfg->wipers;
	indio_dev->name = data->cfg->name;

	err = devm_iio_device_register(dev, indio_dev);
	if (err)
		dev_info(&spi->dev, "Unable to register %s\n", indio_dev->name);

	return err;
}

static const struct of_device_id mcp41010_match[] = {
	{ .compatible = "microchip,mcp41010", .data = &mcp41010_cfg[MCP41010] },
	{ .compatible = "microchip,mcp41050", .data = &mcp41010_cfg[MCP41050] },
	{ .compatible = "microchip,mcp41100", .data = &mcp41010_cfg[MCP41100] },
	{ .compatible = "microchip,mcp42010", .data = &mcp41010_cfg[MCP42010] },
	{ .compatible = "microchip,mcp42050", .data = &mcp41010_cfg[MCP42050] },
	{ .compatible = "microchip,mcp42100", .data = &mcp41010_cfg[MCP42100] },
	{}
};
MODULE_DEVICE_TABLE(of, mcp41010_match);

static const struct spi_device_id mcp41010_id[] = {
	{ "mcp41010", MCP41010 },
	{ "mcp41050", MCP41050 },
	{ "mcp41100", MCP41100 },
	{ "mcp42010", MCP42010 },
	{ "mcp42050", MCP42050 },
	{ "mcp42100", MCP42100 },
	{}
};
MODULE_DEVICE_TABLE(spi, mcp41010_id);

static struct spi_driver mcp41010_driver = {
	.driver = {
		.name	= "mcp41010",
		.of_match_table = mcp41010_match,
	},
	.probe		= mcp41010_probe,
	.id_table	= mcp41010_id,
};

module_spi_driver(mcp41010_driver);

MODULE_AUTHOR("Chris Coffey <cmc@babblebit.net>");
MODULE_DESCRIPTION("MCP41010 digital potentiometer");
MODULE_LICENSE("GPL v2");
