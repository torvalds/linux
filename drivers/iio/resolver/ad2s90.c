// SPDX-License-Identifier: GPL-2.0
/*
 * ad2s90.c simple support for the ADI Resolver to Digital Converters: AD2S90
 *
 * Copyright (c) 2010-2010 Analog Devices Inc.
 */
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/module.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

/*
 * Although chip's max frequency is 2Mhz, it needs 600ns between CS and the
 * first falling edge of SCLK, so frequency should be at most 1 / (2 * 6e-7)
 */
#define AD2S90_MAX_SPI_FREQ_HZ  830000

struct ad2s90_state {
	struct mutex lock; /* lock to protect rx buffer */
	struct spi_device *sdev;
	u8 rx[2] __aligned(IIO_DMA_MINALIGN);
};

static int ad2s90_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	int ret;
	struct ad2s90_state *st = iio_priv(indio_dev);

	if (chan->type != IIO_ANGL)
		return -EINVAL;

	switch (m) {
	case IIO_CHAN_INFO_SCALE:
		/* 2 * Pi / 2^12 */
		*val = 6283; /* mV */
		*val2 = 12;
		return IIO_VAL_FRACTIONAL_LOG2;
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&st->lock);
		ret = spi_read(st->sdev, st->rx, 2);
		if (ret < 0) {
			mutex_unlock(&st->lock);
			return ret;
		}
		*val = (((u16)(st->rx[0])) << 4) | ((st->rx[1] & 0xF0) >> 4);

		mutex_unlock(&st->lock);

		return IIO_VAL_INT;
	default:
		break;
	}

	return -EINVAL;
}

static const struct iio_info ad2s90_info = {
	.read_raw = ad2s90_read_raw,
};

static const struct iio_chan_spec ad2s90_chan = {
	.type = IIO_ANGL,
	.indexed = 1,
	.channel = 0,
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | BIT(IIO_CHAN_INFO_SCALE),
};

static int ad2s90_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct ad2s90_state *st;

	if (spi->max_speed_hz > AD2S90_MAX_SPI_FREQ_HZ) {
		dev_err(&spi->dev, "SPI CLK, %d Hz exceeds %d Hz\n",
			spi->max_speed_hz, AD2S90_MAX_SPI_FREQ_HZ);
		return -EINVAL;
	}

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;
	st = iio_priv(indio_dev);
	spi_set_drvdata(spi, indio_dev);

	mutex_init(&st->lock);
	st->sdev = spi;
	indio_dev->info = &ad2s90_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = &ad2s90_chan;
	indio_dev->num_channels = 1;
	indio_dev->name = spi_get_device_id(spi)->name;

	return devm_iio_device_register(indio_dev->dev.parent, indio_dev);
}

static const struct of_device_id ad2s90_of_match[] = {
	{ .compatible = "adi,ad2s90", },
	{ }
};
MODULE_DEVICE_TABLE(of, ad2s90_of_match);

static const struct spi_device_id ad2s90_id[] = {
	{ "ad2s90" },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad2s90_id);

static struct spi_driver ad2s90_driver = {
	.driver = {
		.name = "ad2s90",
		.of_match_table = ad2s90_of_match,
	},
	.probe = ad2s90_probe,
	.id_table = ad2s90_id,
};
module_spi_driver(ad2s90_driver);

MODULE_AUTHOR("Graff Yang <graff.yang@gmail.com>");
MODULE_DESCRIPTION("Analog Devices AD2S90 Resolver to Digital SPI driver");
MODULE_LICENSE("GPL v2");
