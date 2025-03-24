// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * mcp4922.c
 *
 * Driver for Microchip Digital to Analog Converters.
 * Supports MCP4902, MCP4912, and MCP4922.
 *
 * Copyright (c) 2014 EMAC Inc.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/spi/spi.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/regulator/consumer.h>
#include <linux/bitops.h>

#define MCP4922_NUM_CHANNELS	2
#define MCP4921_NUM_CHANNELS	1

enum mcp4922_supported_device_ids {
	ID_MCP4902,
	ID_MCP4912,
	ID_MCP4921,
	ID_MCP4922,
};

struct mcp4922_state {
	struct spi_device *spi;
	unsigned int value[MCP4922_NUM_CHANNELS];
	unsigned int vref_mv;
	u8 mosi[2] __aligned(IIO_DMA_MINALIGN);
};

#define MCP4922_CHAN(chan, bits) {			\
	.type = IIO_VOLTAGE,				\
	.output = 1,					\
	.indexed = 1,					\
	.channel = chan,				\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
	.scan_type = {					\
		.sign = 'u',				\
		.realbits = (bits),			\
		.storagebits = 16,			\
		.shift = 12 - (bits),			\
	},						\
}

static int mcp4922_spi_write(struct mcp4922_state *state, u8 addr, u32 val)
{
	state->mosi[1] = val & 0xff;
	state->mosi[0] = (addr == 0) ? 0x00 : 0x80;
	state->mosi[0] |= 0x30 | ((val >> 8) & 0x0f);

	return spi_write(state->spi, state->mosi, 2);
}

static int mcp4922_read_raw(struct iio_dev *indio_dev,
		struct iio_chan_spec const *chan,
		int *val,
		int *val2,
		long mask)
{
	struct mcp4922_state *state = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		*val = state->value[chan->channel];
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = state->vref_mv;
		*val2 = chan->scan_type.realbits;
		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		return -EINVAL;
	}
}

static int mcp4922_write_raw(struct iio_dev *indio_dev,
		struct iio_chan_spec const *chan,
		int val,
		int val2,
		long mask)
{
	struct mcp4922_state *state = iio_priv(indio_dev);
	int ret;

	if (val2 != 0)
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (val < 0 || val > GENMASK(chan->scan_type.realbits - 1, 0))
			return -EINVAL;
		val <<= chan->scan_type.shift;

		ret = mcp4922_spi_write(state, chan->channel, val);
		if (!ret)
			state->value[chan->channel] = val;
		return ret;

	default:
		return -EINVAL;
	}
}

static const struct iio_chan_spec mcp4922_channels[4][MCP4922_NUM_CHANNELS] = {
	[ID_MCP4902] = { MCP4922_CHAN(0, 8),	MCP4922_CHAN(1, 8) },
	[ID_MCP4912] = { MCP4922_CHAN(0, 10),	MCP4922_CHAN(1, 10) },
	[ID_MCP4921] = { MCP4922_CHAN(0, 12),	{} },
	[ID_MCP4922] = { MCP4922_CHAN(0, 12),	MCP4922_CHAN(1, 12) },
};

static const struct iio_info mcp4922_info = {
	.read_raw = &mcp4922_read_raw,
	.write_raw = &mcp4922_write_raw,
};

static int mcp4922_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct mcp4922_state *state;
	const struct spi_device_id *id;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*state));
	if (indio_dev == NULL)
		return -ENOMEM;

	state = iio_priv(indio_dev);
	state->spi = spi;

	ret = devm_regulator_get_enable_read_voltage(&spi->dev, "vref");
	if (ret < 0)
		return dev_err_probe(&spi->dev, ret, "Failed to get vref voltage\n");

	state->vref_mv = ret / 1000;

	id = spi_get_device_id(spi);
	indio_dev->info = &mcp4922_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = mcp4922_channels[id->driver_data];
	if (id->driver_data == ID_MCP4921)
		indio_dev->num_channels = MCP4921_NUM_CHANNELS;
	else
		indio_dev->num_channels = MCP4922_NUM_CHANNELS;
	indio_dev->name = id->name;

	ret = devm_iio_device_register(&spi->dev, indio_dev);
	if (ret)
		return dev_err_probe(&spi->dev, ret, "Failed to register iio device\n");

	return 0;
}


static const struct spi_device_id mcp4922_id[] = {
	{"mcp4902", ID_MCP4902},
	{"mcp4912", ID_MCP4912},
	{"mcp4921", ID_MCP4921},
	{"mcp4922", ID_MCP4922},
	{}
};
MODULE_DEVICE_TABLE(spi, mcp4922_id);

static struct spi_driver mcp4922_driver = {
	.driver = {
		   .name = "mcp4922",
		   },
	.probe = mcp4922_probe,
	.id_table = mcp4922_id,
};
module_spi_driver(mcp4922_driver);

MODULE_AUTHOR("Michael Welling <mwelling@ieee.org>");
MODULE_DESCRIPTION("Microchip MCP4902, MCP4912, MCP4922 DAC");
MODULE_LICENSE("GPL v2");
