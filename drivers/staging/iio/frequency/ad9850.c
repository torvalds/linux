/*
 * Driver for ADI Direct Digital Synthesis ad9850
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

#define DRV_NAME "ad9850"

#define value_mask (u16)0xf000
#define addr_shift 12

/* Register format: 4 bits addr + 12 bits value */
struct ad9850_config {
	u8 control[5];
};

struct ad9850_state {
	struct mutex lock;
	struct spi_device *sdev;
};

static ssize_t ad9850_set_parameter(struct device *dev,
					struct device_attribute *attr,
					const char *buf,
					size_t len)
{
	struct spi_message msg;
	struct spi_transfer xfer;
	int ret;
	struct ad9850_config *config = (struct ad9850_config *)buf;
	struct iio_dev *idev = dev_to_iio_dev(dev);
	struct ad9850_state *st = iio_priv(idev);

	xfer.len = len;
	xfer.tx_buf = config;
	mutex_lock(&st->lock);

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->sdev, &msg);
	if (ret)
		goto error_ret;
error_ret:
	mutex_unlock(&st->lock);

	return ret ? ret : len;
}

static IIO_DEVICE_ATTR(dds, S_IWUSR, NULL, ad9850_set_parameter, 0);

static struct attribute *ad9850_attributes[] = {
	&iio_dev_attr_dds.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad9850_attribute_group = {
	.attrs = ad9850_attributes,
};

static const struct iio_info ad9850_info = {
	.attrs = &ad9850_attribute_group,
	.driver_module = THIS_MODULE,
};

static int ad9850_probe(struct spi_device *spi)
{
	struct ad9850_state *st;
	struct iio_dev *idev;
	int ret = 0;

	idev = iio_device_alloc(sizeof(*st));
	if (idev == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	spi_set_drvdata(spi, idev);
	st = iio_priv(idev);
	mutex_init(&st->lock);
	st->sdev = spi;

	idev->dev.parent = &spi->dev;
	idev->info = &ad9850_info;
	idev->modes = INDIO_DIRECT_MODE;

	ret = iio_device_register(idev);
	if (ret)
		goto error_free_dev;
	spi->max_speed_hz = 2000000;
	spi->mode = SPI_MODE_3;
	spi->bits_per_word = 16;
	spi_setup(spi);

	return 0;

error_free_dev:
	iio_device_free(idev);
error_ret:
	return ret;
}

static int ad9850_remove(struct spi_device *spi)
{
	iio_device_unregister(spi_get_drvdata(spi));
	iio_device_free(spi_get_drvdata(spi));

	return 0;
}

static struct spi_driver ad9850_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
	},
	.probe = ad9850_probe,
	.remove = ad9850_remove,
};
module_spi_driver(ad9850_driver);

MODULE_AUTHOR("Cliff Cai");
MODULE_DESCRIPTION("Analog Devices ad9850 driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("spi:" DRV_NAME);
