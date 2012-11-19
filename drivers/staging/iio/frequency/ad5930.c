/*
 * Driver for ADI Direct Digital Synthesis ad5930
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

#define DRV_NAME "ad5930"

#define value_mask (u16)0xf000
#define addr_shift 12

/* Register format: 4 bits addr + 12 bits value */
struct ad5903_config {
	u16 control;
	u16 incnum;
	u16 frqdelt[2];
	u16 incitvl;
	u16 buritvl;
	u16 strtfrq[2];
};

struct ad5930_state {
	struct mutex lock;
	struct spi_device *sdev;
};

static ssize_t ad5930_set_parameter(struct device *dev,
					struct device_attribute *attr,
					const char *buf,
					size_t len)
{
	struct spi_message msg;
	struct spi_transfer xfer;
	int ret;
	struct ad5903_config *config = (struct ad5903_config *)buf;
	struct iio_dev *idev = dev_to_iio_dev(dev);
	struct ad5930_state *st = iio_priv(idev);

	config->control = (config->control & ~value_mask);
	config->incnum = (config->control & ~value_mask) | (1 << addr_shift);
	config->frqdelt[0] = (config->control & ~value_mask) | (2 << addr_shift);
	config->frqdelt[1] = (config->control & ~value_mask) | 3 << addr_shift;
	config->incitvl = (config->control & ~value_mask) | 4 << addr_shift;
	config->buritvl = (config->control & ~value_mask) | 8 << addr_shift;
	config->strtfrq[0] = (config->control & ~value_mask) | 0xc << addr_shift;
	config->strtfrq[1] = (config->control & ~value_mask) | 0xd << addr_shift;

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

static IIO_DEVICE_ATTR(dds, S_IWUSR, NULL, ad5930_set_parameter, 0);

static struct attribute *ad5930_attributes[] = {
	&iio_dev_attr_dds.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad5930_attribute_group = {
	.attrs = ad5930_attributes,
};

static const struct iio_info ad5930_info = {
	.attrs = &ad5930_attribute_group,
	.driver_module = THIS_MODULE,
};

static int ad5930_probe(struct spi_device *spi)
{
	struct ad5930_state *st;
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
	idev->info = &ad5930_info;
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

static int ad5930_remove(struct spi_device *spi)
{
	iio_device_unregister(spi_get_drvdata(spi));
	iio_device_free(spi_get_drvdata(spi));

	return 0;
}

static struct spi_driver ad5930_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
	},
	.probe = ad5930_probe,
	.remove = __devexit_p(ad5930_remove),
};
module_spi_driver(ad5930_driver);

MODULE_AUTHOR("Cliff Cai");
MODULE_DESCRIPTION("Analog Devices ad5930 driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("spi:" DRV_NAME);
