/*
 * AD5415, AD5426, AD5429, AD5432, AD5439, AD5443, AD5449 Digital to Analog
 * Converter driver.
 *
 * Copyright 2012 Analog Devices Inc.
 *  Author: Lars-Peter Clausen <lars@metafoo.de>
 *
 * Licensed under the GPL-2.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/regulator/consumer.h>
#include <asm/unaligned.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#include <linux/platform_data/ad5449.h>

#define AD5449_MAX_CHANNELS		2
#define AD5449_MAX_VREFS		2

#define AD5449_CMD_NOOP			0x0
#define AD5449_CMD_LOAD_AND_UPDATE(x)	(0x1 + (x) * 3)
#define AD5449_CMD_READ(x)		(0x2 + (x) * 3)
#define AD5449_CMD_LOAD(x)		(0x3 + (x) * 3)
#define AD5449_CMD_CTRL			13

#define AD5449_CTRL_SDO_OFFSET		10
#define AD5449_CTRL_DAISY_CHAIN		BIT(9)
#define AD5449_CTRL_HCLR_TO_MIDSCALE	BIT(8)
#define AD5449_CTRL_SAMPLE_RISING	BIT(7)

/**
 * struct ad5449_chip_info - chip specific information
 * @channels:		Channel specification
 * @num_channels:	Number of channels
 * @has_ctrl:		Chip has a control register
 */
struct ad5449_chip_info {
	const struct iio_chan_spec *channels;
	unsigned int num_channels;
	bool has_ctrl;
};

/**
 * struct ad5449 - driver instance specific data
 * @spi:		the SPI device for this driver instance
 * @chip_info:		chip model specific constants, available modes etc
 * @vref_reg:		vref supply regulators
 * @has_sdo:		whether the SDO line is connected
 * @dac_cache:		Cache for the DAC values
 * @data:		spi transfer buffers
 */
struct ad5449 {
	struct spi_device		*spi;
	const struct ad5449_chip_info	*chip_info;
	struct regulator_bulk_data	vref_reg[AD5449_MAX_VREFS];

	bool has_sdo;
	uint16_t dac_cache[AD5449_MAX_CHANNELS];

	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	__be16 data[2] ____cacheline_aligned;
};

enum ad5449_type {
	ID_AD5426,
	ID_AD5429,
	ID_AD5432,
	ID_AD5439,
	ID_AD5443,
	ID_AD5449,
};

static int ad5449_write(struct iio_dev *indio_dev, unsigned int addr,
	unsigned int val)
{
	struct ad5449 *st = iio_priv(indio_dev);
	int ret;

	mutex_lock(&indio_dev->mlock);
	st->data[0] = cpu_to_be16((addr << 12) | val);
	ret = spi_write(st->spi, st->data, 2);
	mutex_unlock(&indio_dev->mlock);

	return ret;
}

static int ad5449_read(struct iio_dev *indio_dev, unsigned int addr,
	unsigned int *val)
{
	struct ad5449 *st = iio_priv(indio_dev);
	int ret;
	struct spi_message msg;
	struct spi_transfer t[] = {
		{
			.tx_buf = &st->data[0],
			.len = 2,
			.cs_change = 1,
		}, {
			.tx_buf = &st->data[1],
			.rx_buf = &st->data[1],
			.len = 2,
		},
	};

	spi_message_init(&msg);
	spi_message_add_tail(&t[0], &msg);
	spi_message_add_tail(&t[1], &msg);

	mutex_lock(&indio_dev->mlock);
	st->data[0] = cpu_to_be16(addr << 12);
	st->data[1] = cpu_to_be16(AD5449_CMD_NOOP);

	ret = spi_sync(st->spi, &msg);
	if (ret < 0)
		goto out_unlock;

	*val = be16_to_cpu(st->data[1]);

out_unlock:
	mutex_unlock(&indio_dev->mlock);
	return ret;
}

static int ad5449_read_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *chan, int *val, int *val2, long info)
{
	struct ad5449 *st = iio_priv(indio_dev);
	struct regulator_bulk_data *reg;
	int scale_uv;
	int ret;

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		if (st->has_sdo) {
			ret = ad5449_read(indio_dev,
				AD5449_CMD_READ(chan->address), val);
			if (ret)
				return ret;
			*val &= 0xfff;
		} else {
			*val = st->dac_cache[chan->address];
		}

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		reg = &st->vref_reg[chan->channel];
		scale_uv = regulator_get_voltage(reg->consumer);
		if (scale_uv < 0)
			return scale_uv;

		*val = scale_uv / 1000;
		*val2 = chan->scan_type.realbits;

		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		break;
	}

	return -EINVAL;
}

static int ad5449_write_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *chan, int val, int val2, long info)
{
	struct ad5449 *st = iio_priv(indio_dev);
	int ret;

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		if (val < 0 || val >= (1 << chan->scan_type.realbits))
			return -EINVAL;

		ret = ad5449_write(indio_dev,
			AD5449_CMD_LOAD_AND_UPDATE(chan->address),
			val << chan->scan_type.shift);
		if (ret == 0)
			st->dac_cache[chan->address] = val;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct iio_info ad5449_info = {
	.read_raw = ad5449_read_raw,
	.write_raw = ad5449_write_raw,
	.driver_module = THIS_MODULE,
};

#define AD5449_CHANNEL(chan, bits) {				\
	.type = IIO_VOLTAGE,					\
	.indexed = 1,						\
	.output = 1,						\
	.channel = (chan),					\
	.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |		\
		IIO_CHAN_INFO_SCALE_SEPARATE_BIT,		\
	.address = (chan),					\
	.scan_type = IIO_ST('u', (bits), 16, 12 - (bits)),	\
}

#define DECLARE_AD5449_CHANNELS(name, bits) \
const struct iio_chan_spec name[] = { \
	AD5449_CHANNEL(0, bits), \
	AD5449_CHANNEL(1, bits), \
}

static DECLARE_AD5449_CHANNELS(ad5429_channels, 8);
static DECLARE_AD5449_CHANNELS(ad5439_channels, 10);
static DECLARE_AD5449_CHANNELS(ad5449_channels, 12);

static const struct ad5449_chip_info ad5449_chip_info[] = {
	[ID_AD5426] = {
		.channels = ad5429_channels,
		.num_channels = 1,
		.has_ctrl = false,
	},
	[ID_AD5429] = {
		.channels = ad5429_channels,
		.num_channels = 2,
		.has_ctrl = true,
	},
	[ID_AD5432] = {
		.channels = ad5439_channels,
		.num_channels = 1,
		.has_ctrl = false,
	},
	[ID_AD5439] = {
		.channels = ad5439_channels,
		.num_channels = 2,
		.has_ctrl = true,
	},
	[ID_AD5443] = {
		.channels = ad5449_channels,
		.num_channels = 1,
		.has_ctrl = false,
	},
	[ID_AD5449] = {
		.channels = ad5449_channels,
		.num_channels = 2,
		.has_ctrl = true,
	},
};

static const char *ad5449_vref_name(struct ad5449 *st, int n)
{
	if (st->chip_info->num_channels == 1)
		return "VREF";

	if (n == 0)
		return "VREFA";
	else
		return "VREFB";
}

static int ad5449_spi_probe(struct spi_device *spi)
{
	struct ad5449_platform_data *pdata = spi->dev.platform_data;
	const struct spi_device_id *id = spi_get_device_id(spi);
	struct iio_dev *indio_dev;
	struct ad5449 *st;
	unsigned int i;
	int ret;

	indio_dev = iio_device_alloc(sizeof(*st));
	if (indio_dev == NULL)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	spi_set_drvdata(spi, indio_dev);

	st->chip_info = &ad5449_chip_info[id->driver_data];
	st->spi = spi;

	for (i = 0; i < st->chip_info->num_channels; ++i)
		st->vref_reg[i].supply = ad5449_vref_name(st, i);

	ret = regulator_bulk_get(&spi->dev, st->chip_info->num_channels,
				st->vref_reg);
	if (ret)
		goto error_free;

	ret = regulator_bulk_enable(st->chip_info->num_channels, st->vref_reg);
	if (ret)
		goto error_free_reg;

	indio_dev->dev.parent = &spi->dev;
	indio_dev->name = id->name;
	indio_dev->info = &ad5449_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = st->chip_info->channels;
	indio_dev->num_channels = st->chip_info->num_channels;

	if (st->chip_info->has_ctrl) {
		unsigned int ctrl = 0x00;
		if (pdata) {
			if (pdata->hardware_clear_to_midscale)
				ctrl |= AD5449_CTRL_HCLR_TO_MIDSCALE;
			ctrl |= pdata->sdo_mode << AD5449_CTRL_SDO_OFFSET;
			st->has_sdo = pdata->sdo_mode != AD5449_SDO_DISABLED;
		} else {
			st->has_sdo = true;
		}
		ad5449_write(indio_dev, AD5449_CMD_CTRL, ctrl);
	}

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_disable_reg;

	return 0;

error_disable_reg:
	regulator_bulk_disable(st->chip_info->num_channels, st->vref_reg);
error_free_reg:
	regulator_bulk_free(st->chip_info->num_channels, st->vref_reg);
error_free:
	iio_device_free(indio_dev);

	return ret;
}

static int ad5449_spi_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct ad5449 *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	regulator_bulk_disable(st->chip_info->num_channels, st->vref_reg);
	regulator_bulk_free(st->chip_info->num_channels, st->vref_reg);

	iio_device_free(indio_dev);

	return 0;
}

static const struct spi_device_id ad5449_spi_ids[] = {
	{ "ad5415", ID_AD5449 },
	{ "ad5426", ID_AD5426 },
	{ "ad5429", ID_AD5429 },
	{ "ad5432", ID_AD5432 },
	{ "ad5439", ID_AD5439 },
	{ "ad5443", ID_AD5443 },
	{ "ad5449", ID_AD5449 },
	{}
};
MODULE_DEVICE_TABLE(spi, ad5449_spi_ids);

static struct spi_driver ad5449_spi_driver = {
	.driver = {
		.name = "ad5449",
		.owner = THIS_MODULE,
	},
	.probe = ad5449_spi_probe,
	.remove = ad5449_spi_remove,
	.id_table = ad5449_spi_ids,
};
module_spi_driver(ad5449_spi_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("Analog Devices AD5449 and similar DACs");
MODULE_LICENSE("GPL v2");
