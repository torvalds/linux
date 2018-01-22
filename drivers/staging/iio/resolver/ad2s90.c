/*
 * ad2s90.c simple support for the ADI Resolver to Digital Converters: AD2S90
 *
 * Copyright (c) 2010-2010 Analog Devices Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
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

struct ad2s90_state {
	struct mutex lock;
	struct spi_device *sdev;
	u8 rx[2] ____cacheline_aligned;
};

static int ad2s90_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val,
			   int *val2,
			   long m)
{
	int ret;
	struct ad2s90_state *st = iio_priv(indio_dev);

	mutex_lock(&st->lock);
	ret = spi_read(st->sdev, st->rx, 2);
	if (ret)
		goto error_ret;
	*val = (((u16)(st->rx[0])) << 4) | ((st->rx[1] & 0xF0) >> 4);

error_ret:
	mutex_unlock(&st->lock);

	return IIO_VAL_INT;
}

static const struct iio_info ad2s90_info = {
	.read_raw = ad2s90_read_raw,
};

static const struct iio_chan_spec ad2s90_chan = {
	.type = IIO_ANGL,
	.indexed = 1,
	.channel = 0,
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
};

static int ad2s90_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct ad2s90_state *st;
	int ret = 0;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;
	st = iio_priv(indio_dev);
	spi_set_drvdata(spi, indio_dev);

	mutex_init(&st->lock);
	st->sdev = spi;
	indio_dev->dev.parent = &spi->dev;
	indio_dev->info = &ad2s90_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = &ad2s90_chan;
	indio_dev->num_channels = 1;
	indio_dev->name = spi_get_device_id(spi)->name;

	ret = devm_iio_device_register(indio_dev->dev.parent, indio_dev);
	if (ret)
		return ret;

	/* need 600ns between CS and the first falling edge of SCLK */
	spi->max_speed_hz = 830000;
	spi->mode = SPI_MODE_3;
	spi_setup(spi);

	return 0;
}

static const struct spi_device_id ad2s90_id[] = {
	{ "ad2s90" },
	{}
};
MODULE_DEVICE_TABLE(spi, ad2s90_id);

static struct spi_driver ad2s90_driver = {
	.driver = {
		.name = "ad2s90",
	},
	.probe = ad2s90_probe,
	.id_table = ad2s90_id,
};
module_spi_driver(ad2s90_driver);

MODULE_AUTHOR("Graff Yang <graff.yang@gmail.com>");
MODULE_DESCRIPTION("Analog Devices AD2S90 Resolver to Digital SPI driver");
MODULE_LICENSE("GPL v2");
