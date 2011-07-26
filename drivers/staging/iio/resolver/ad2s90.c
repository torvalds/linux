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

#include "../iio.h"
#include "../sysfs.h"

#define DRV_NAME "ad2s90"

struct ad2s90_state {
	struct mutex lock;
	struct iio_dev *idev;
	struct spi_device *sdev;
	u8 rx[2] ____cacheline_aligned;
};

static ssize_t ad2s90_show_angular(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int ret;
	ssize_t len = 0;
	u16 val;
	struct ad2s90_state *st = iio_priv(dev_get_drvdata(dev));

	mutex_lock(&st->lock);
	ret = spi_read(st->sdev, st->rx, 2);
	if (ret)
		goto error_ret;
	val = (((u16)(st->rx[0])) << 4) | ((st->rx[1] & 0xF0) >> 4);
	len = sprintf(buf, "%d\n", val);
error_ret:
	mutex_unlock(&st->lock);

	return ret ? ret : len;
}

#define IIO_DEV_ATTR_SIMPLE_RESOLVER(_show) \
	IIO_DEVICE_ATTR(angular, S_IRUGO, _show, NULL, 0)

static IIO_DEV_ATTR_SIMPLE_RESOLVER(ad2s90_show_angular);

static struct attribute *ad2s90_attributes[] = {
	&iio_dev_attr_angular.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad2s90_attribute_group = {
	.name = DRV_NAME,
	.attrs = ad2s90_attributes,
};

static const struct iio_info ad2s90_info = {
	.attrs = &ad2s90_attribute_group,
	.driver_module = THIS_MODULE,
};

static int __devinit ad2s90_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct ad2s90_state *st;
	int ret = 0;

	indio_dev = iio_allocate_device(sizeof(*st));
	if (indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	st = iio_priv(indio_dev);
	spi_set_drvdata(spi, indio_dev);

	mutex_init(&st->lock);
	st->sdev = spi;
	indio_dev->dev.parent = &spi->dev;
	indio_dev->info = &ad2s90_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = iio_device_register(st->idev);
	if (ret)
		goto error_free_dev;

	/* need 600ns between CS and the first falling edge of SCLK */
	spi->max_speed_hz = 830000;
	spi->mode = SPI_MODE_3;
	spi_setup(spi);

	return 0;

error_free_dev:
	iio_free_device(st->idev);
error_ret:
	return ret;
}

static int __devexit ad2s90_remove(struct spi_device *spi)
{
	iio_device_unregister(spi_get_drvdata(spi));

	return 0;
}

static struct spi_driver ad2s90_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
	},
	.probe = ad2s90_probe,
	.remove = __devexit_p(ad2s90_remove),
};

static __init int ad2s90_spi_init(void)
{
	return spi_register_driver(&ad2s90_driver);
}
module_init(ad2s90_spi_init);

static __exit void ad2s90_spi_exit(void)
{
	spi_unregister_driver(&ad2s90_driver);
}
module_exit(ad2s90_spi_exit);

MODULE_AUTHOR("Graff Yang <graff.yang@gmail.com>");
MODULE_DESCRIPTION("Analog Devices AD2S90 Resolver to Digital SPI driver");
MODULE_LICENSE("GPL v2");
