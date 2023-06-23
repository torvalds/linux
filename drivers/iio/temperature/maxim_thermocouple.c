// SPDX-License-Identifier: GPL-2.0+
/*
 * maxim_thermocouple.c  - Support for Maxim thermocouple chips
 *
 * Copyright (C) 2016-2018 Matt Ranostay
 * Author: <matt.ranostay@konsulko.com>
 */

#include <linux/init.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/spi/spi.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/buffer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>

#define MAXIM_THERMOCOUPLE_DRV_NAME	"maxim_thermocouple"

enum {
	MAX6675,
	MAX31855,
	MAX31855K,
	MAX31855J,
	MAX31855N,
	MAX31855S,
	MAX31855T,
	MAX31855E,
	MAX31855R,
};

static const char maxim_tc_types[] = {
	'K', '?', 'K', 'J', 'N', 'S', 'T', 'E', 'R'
};

static const struct iio_chan_spec max6675_channels[] = {
	{	/* thermocouple temperature */
		.type = IIO_TEMP,
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE) |
			BIT(IIO_CHAN_INFO_THERMOCOUPLE_TYPE),
		.scan_index = 0,
		.scan_type = {
			.sign = 's',
			.realbits = 13,
			.storagebits = 16,
			.shift = 3,
			.endianness = IIO_BE,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(1),
};

static const struct iio_chan_spec max31855_channels[] = {
	{	/* thermocouple temperature */
		.type = IIO_TEMP,
		.address = 2,
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE) |
			BIT(IIO_CHAN_INFO_THERMOCOUPLE_TYPE),
		.scan_index = 0,
		.scan_type = {
			.sign = 's',
			.realbits = 14,
			.storagebits = 16,
			.shift = 2,
			.endianness = IIO_BE,
		},
	},
	{	/* cold junction temperature */
		.type = IIO_TEMP,
		.address = 0,
		.channel2 = IIO_MOD_TEMP_AMBIENT,
		.modified = 1,
		.info_mask_separate =
			BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = 1,
		.scan_type = {
			.sign = 's',
			.realbits = 12,
			.storagebits = 16,
			.shift = 4,
			.endianness = IIO_BE,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(2),
};

static const unsigned long max31855_scan_masks[] = {0x3, 0};

struct maxim_thermocouple_chip {
	const struct iio_chan_spec *channels;
	const unsigned long *scan_masks;
	u8 num_channels;
	u8 read_size;

	/* bit-check for valid input */
	u32 status_bit;
};

static const struct maxim_thermocouple_chip maxim_thermocouple_chips[] = {
	[MAX6675] = {
			.channels = max6675_channels,
			.num_channels = ARRAY_SIZE(max6675_channels),
			.read_size = 2,
			.status_bit = BIT(2),
		},
	[MAX31855] = {
			.channels = max31855_channels,
			.num_channels = ARRAY_SIZE(max31855_channels),
			.read_size = 4,
			.scan_masks = max31855_scan_masks,
			.status_bit = BIT(16),
		},
};

struct maxim_thermocouple_data {
	struct spi_device *spi;
	const struct maxim_thermocouple_chip *chip;

	u8 buffer[16] __aligned(IIO_DMA_MINALIGN);
	char tc_type;
};

static int maxim_thermocouple_read(struct maxim_thermocouple_data *data,
				   struct iio_chan_spec const *chan, int *val)
{
	unsigned int storage_bytes = data->chip->read_size;
	unsigned int shift = chan->scan_type.shift + (chan->address * 8);
	__be16 buf16;
	__be32 buf32;
	int ret;

	switch (storage_bytes) {
	case 2:
		ret = spi_read(data->spi, (void *)&buf16, storage_bytes);
		*val = be16_to_cpu(buf16);
		break;
	case 4:
		ret = spi_read(data->spi, (void *)&buf32, storage_bytes);
		*val = be32_to_cpu(buf32);
		break;
	default:
		ret = -EINVAL;
	}

	if (ret)
		return ret;

	/* check to be sure this is a valid reading */
	if (*val & data->chip->status_bit)
		return -EINVAL;

	*val = sign_extend32(*val >> shift, chan->scan_type.realbits - 1);

	return 0;
}

static irqreturn_t maxim_thermocouple_trigger_handler(int irq, void *private)
{
	struct iio_poll_func *pf = private;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct maxim_thermocouple_data *data = iio_priv(indio_dev);
	int ret;

	ret = spi_read(data->spi, data->buffer, data->chip->read_size);
	if (!ret) {
		iio_push_to_buffers_with_timestamp(indio_dev, data->buffer,
						   iio_get_time_ns(indio_dev));
	}

	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int maxim_thermocouple_read_raw(struct iio_dev *indio_dev,
				       struct iio_chan_spec const *chan,
				       int *val, int *val2, long mask)
{
	struct maxim_thermocouple_data *data = iio_priv(indio_dev);
	int ret = -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;

		ret = maxim_thermocouple_read(data, chan, val);
		iio_device_release_direct_mode(indio_dev);

		if (!ret)
			return IIO_VAL_INT;

		break;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->channel2) {
		case IIO_MOD_TEMP_AMBIENT:
			*val = 62;
			*val2 = 500000; /* 1000 * 0.0625 */
			ret = IIO_VAL_INT_PLUS_MICRO;
			break;
		default:
			*val = 250; /* 1000 * 0.25 */
			ret = IIO_VAL_INT;
		}
		break;
	case IIO_CHAN_INFO_THERMOCOUPLE_TYPE:
		*val = data->tc_type;
		ret = IIO_VAL_CHAR;
		break;
	}

	return ret;
}

static const struct iio_info maxim_thermocouple_info = {
	.read_raw = maxim_thermocouple_read_raw,
};

static int maxim_thermocouple_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	struct iio_dev *indio_dev;
	struct maxim_thermocouple_data *data;
	const int chip_type = (id->driver_data == MAX6675) ? MAX6675 : MAX31855;
	const struct maxim_thermocouple_chip *chip =
		&maxim_thermocouple_chips[chip_type];
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	indio_dev->info = &maxim_thermocouple_info;
	indio_dev->name = MAXIM_THERMOCOUPLE_DRV_NAME;
	indio_dev->channels = chip->channels;
	indio_dev->available_scan_masks = chip->scan_masks;
	indio_dev->num_channels = chip->num_channels;
	indio_dev->modes = INDIO_DIRECT_MODE;

	data = iio_priv(indio_dev);
	data->spi = spi;
	data->chip = chip;
	data->tc_type = maxim_tc_types[id->driver_data];

	ret = devm_iio_triggered_buffer_setup(&spi->dev,
				indio_dev, NULL,
				maxim_thermocouple_trigger_handler, NULL);
	if (ret)
		return ret;

	if (id->driver_data == MAX31855)
		dev_warn(&spi->dev, "generic max31855 ID is deprecated\nplease use more specific part type");

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct spi_device_id maxim_thermocouple_id[] = {
	{"max6675", MAX6675},
	{"max31855", MAX31855},
	{"max31855k", MAX31855K},
	{"max31855j", MAX31855J},
	{"max31855n", MAX31855N},
	{"max31855s", MAX31855S},
	{"max31855t", MAX31855T},
	{"max31855e", MAX31855E},
	{"max31855r", MAX31855R},
	{},
};
MODULE_DEVICE_TABLE(spi, maxim_thermocouple_id);

static const struct of_device_id maxim_thermocouple_of_match[] = {
        { .compatible = "maxim,max6675" },
        { .compatible = "maxim,max31855" },
	{ .compatible = "maxim,max31855k" },
	{ .compatible = "maxim,max31855j" },
	{ .compatible = "maxim,max31855n" },
	{ .compatible = "maxim,max31855s" },
	{ .compatible = "maxim,max31855t" },
	{ .compatible = "maxim,max31855e" },
	{ .compatible = "maxim,max31855r" },
        { },
};
MODULE_DEVICE_TABLE(of, maxim_thermocouple_of_match);

static struct spi_driver maxim_thermocouple_driver = {
	.driver = {
		.name	= MAXIM_THERMOCOUPLE_DRV_NAME,
		.of_match_table = maxim_thermocouple_of_match,
	},
	.probe		= maxim_thermocouple_probe,
	.id_table	= maxim_thermocouple_id,
};
module_spi_driver(maxim_thermocouple_driver);

MODULE_AUTHOR("Matt Ranostay <matt.ranostay@konsulko.com>");
MODULE_DESCRIPTION("Maxim thermocouple sensors");
MODULE_LICENSE("GPL");
