// SPDX-License-Identifier: GPL-2.0-only
/*
 * ad2s1200.c simple support for the ADI Resolver to Digital Converters:
 * AD2S1200/1205
 *
 * Copyright (c) 2018-2018 David Veenstra <davidjulianveenstra@gmail.com>
 * Copyright (c) 2010-2010 Analog Devices Inc.
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/spi/spi.h>
#include <linux/sysfs.h>
#include <linux/types.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>


/* input clock on serial interface */
#define AD2S1200_HZ	8192000
/* clock period in nano second */
#define AD2S1200_TSCLK	(1000000000 / AD2S1200_HZ)

/**
 * struct ad2s1200_state - driver instance specific data.
 * @lock:	protects both the GPIO pins and the rx buffer.
 * @sdev:	spi device.
 * @sample:	GPIO pin SAMPLE.
 * @rdvel:	GPIO pin RDVEL.
 * @rx:		buffer for spi transfers.
 */
struct ad2s1200_state {
	struct mutex lock;
	struct spi_device *sdev;
	struct gpio_desc *sample;
	struct gpio_desc *rdvel;
	__be16 rx __aligned(IIO_DMA_MINALIGN);
};

static int ad2s1200_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val,
			     int *val2,
			     long m)
{
	struct ad2s1200_state *st = iio_priv(indio_dev);
	int ret;

	switch (m) {
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ANGL:
			/* 2 * Pi / (2^12 - 1) ~= 0.001534355 */
			*val = 0;
			*val2 = 1534355;
			return IIO_VAL_INT_PLUS_NANO;
		case IIO_ANGL_VEL:
			/* 2 * Pi ~= 6.283185 */
			*val = 6;
			*val2 = 283185;
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
		break;
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&st->lock);
		gpiod_set_value(st->sample, 0);

		/* delay (6 * AD2S1200_TSCLK + 20) nano seconds */
		udelay(1);
		gpiod_set_value(st->sample, 1);
		gpiod_set_value(st->rdvel, !!(chan->type == IIO_ANGL));

		ret = spi_read(st->sdev, &st->rx, 2);
		if (ret < 0) {
			mutex_unlock(&st->lock);
			return ret;
		}

		switch (chan->type) {
		case IIO_ANGL:
			*val = be16_to_cpup(&st->rx) >> 4;
			break;
		case IIO_ANGL_VEL:
			*val = sign_extend32(be16_to_cpup(&st->rx) >> 4, 11);
			break;
		default:
			mutex_unlock(&st->lock);
			return -EINVAL;
		}

		/* delay (2 * AD2S1200_TSCLK + 20) ns for sample pulse */
		udelay(1);
		mutex_unlock(&st->lock);

		return IIO_VAL_INT;
	default:
		break;
	}

	return -EINVAL;
}

static const struct iio_chan_spec ad2s1200_channels[] = {
	{
		.type = IIO_ANGL,
		.indexed = 1,
		.channel = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
	}, {
		.type = IIO_ANGL_VEL,
		.indexed = 1,
		.channel = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
	}
};

static const struct iio_info ad2s1200_info = {
	.read_raw = ad2s1200_read_raw,
};

static int ad2s1200_probe(struct spi_device *spi)
{
	struct ad2s1200_state *st;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	spi_set_drvdata(spi, indio_dev);
	st = iio_priv(indio_dev);
	mutex_init(&st->lock);
	st->sdev = spi;

	st->sample = devm_gpiod_get(&spi->dev, "adi,sample", GPIOD_OUT_LOW);
	if (IS_ERR(st->sample)) {
		dev_err(&spi->dev, "Failed to claim SAMPLE gpio: err=%ld\n",
			PTR_ERR(st->sample));
		return PTR_ERR(st->sample);
	}

	st->rdvel = devm_gpiod_get(&spi->dev, "adi,rdvel", GPIOD_OUT_LOW);
	if (IS_ERR(st->rdvel)) {
		dev_err(&spi->dev, "Failed to claim RDVEL gpio: err=%ld\n",
			PTR_ERR(st->rdvel));
		return PTR_ERR(st->rdvel);
	}

	indio_dev->info = &ad2s1200_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = ad2s1200_channels;
	indio_dev->num_channels = ARRAY_SIZE(ad2s1200_channels);
	indio_dev->name = spi_get_device_id(spi)->name;

	spi->max_speed_hz = AD2S1200_HZ;
	spi->mode = SPI_MODE_3;
	ret = spi_setup(spi);

	if (ret < 0) {
		dev_err(&spi->dev, "spi_setup failed!\n");
		return ret;
	}

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct of_device_id ad2s1200_of_match[] = {
	{ .compatible = "adi,ad2s1200", },
	{ .compatible = "adi,ad2s1205", },
	{ }
};
MODULE_DEVICE_TABLE(of, ad2s1200_of_match);

static const struct spi_device_id ad2s1200_id[] = {
	{ "ad2s1200" },
	{ "ad2s1205" },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad2s1200_id);

static struct spi_driver ad2s1200_driver = {
	.driver = {
		.name = "ad2s1200",
		.of_match_table = ad2s1200_of_match,
	},
	.probe = ad2s1200_probe,
	.id_table = ad2s1200_id,
};
module_spi_driver(ad2s1200_driver);

MODULE_AUTHOR("David Veenstra <davidjulianveenstra@gmail.com>");
MODULE_AUTHOR("Graff Yang <graff.yang@gmail.com>");
MODULE_DESCRIPTION("Analog Devices AD2S1200/1205 Resolver to Digital SPI driver");
MODULE_LICENSE("GPL v2");
