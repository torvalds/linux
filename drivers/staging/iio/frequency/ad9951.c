/*
 * Driver for ADI Direct Digital Synthesis ad9951
 *
 * Copyright (c) 2010 Analog Devices Inc.
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

#define DRV_NAME "ad9951"

#define CFR1 0x0
#define CFR2 0x1

#define AUTO_OSK	(1)
#define OSKEN		(1 << 1)
#define LOAD_ARR	(1 << 2)

#define AUTO_SYNC	(1 << 7)

#define LSB_FST		(1)
#define SDIO_IPT	(1 << 1)
#define CLR_PHA		(1 << 2)
#define SINE_OPT	(1 << 4)
#define ACLR_PHA	(1 << 5)

#define VCO_RANGE	(1 << 2)

#define CRS_OPT		(1 << 1)
#define HMANU_SYNC	(1 << 2)
#define HSPD_SYNC	(1 << 3)

/* Register format: 1 byte addr + value */
struct ad9951_config {
	u8 asf[3];
	u8 arr[2];
	u8 ftw0[5];
	u8 ftw1[3];
};

struct ad9951_state {
	struct mutex lock;
	struct spi_device *sdev;
};

static ssize_t ad9951_set_parameter(struct device *dev,
					struct device_attribute *attr,
					const char *buf,
					size_t len)
{
	struct spi_message msg;
	struct spi_transfer xfer;
	int ret;
	struct ad9951_config *config = (struct ad9951_config *)buf;
	struct iio_dev *idev = dev_to_iio_dev(dev);
	struct ad9951_state *st = iio_priv(idev);

	xfer.len = 3;
	xfer.tx_buf = &config->asf[0];
	mutex_lock(&st->lock);

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->sdev, &msg);
	if (ret)
		goto error_ret;

	xfer.len = 2;
	xfer.tx_buf = &config->arr[0];

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->sdev, &msg);
	if (ret)
		goto error_ret;

	xfer.len = 5;
	xfer.tx_buf = &config->ftw0[0];

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->sdev, &msg);
	if (ret)
		goto error_ret;

	xfer.len = 3;
	xfer.tx_buf = &config->ftw1[0];

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->sdev, &msg);
	if (ret)
		goto error_ret;
error_ret:
	mutex_unlock(&st->lock);

	return ret ? ret : len;
}

static IIO_DEVICE_ATTR(dds, S_IWUSR, NULL, ad9951_set_parameter, 0);

static void ad9951_init(struct ad9951_state *st)
{
	struct spi_message msg;
	struct spi_transfer xfer;
	int ret;
	u8 cfr[5];

	cfr[0] = CFR1;
	cfr[1] = 0;
	cfr[2] = LSB_FST | CLR_PHA | SINE_OPT | ACLR_PHA;
	cfr[3] = AUTO_OSK | OSKEN | LOAD_ARR;
	cfr[4] = 0;

	mutex_lock(&st->lock);

	xfer.len = 5;
	xfer.tx_buf = &cfr;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->sdev, &msg);
	if (ret)
		goto error_ret;

	cfr[0] = CFR2;
	cfr[1] = VCO_RANGE;
	cfr[2] = HSPD_SYNC;
	cfr[3] = 0;

	xfer.len = 4;
	xfer.tx_buf = &cfr;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->sdev, &msg);
	if (ret)
		goto error_ret;

error_ret:
	mutex_unlock(&st->lock);



}

static struct attribute *ad9951_attributes[] = {
	&iio_dev_attr_dds.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad9951_attribute_group = {
	.attrs = ad9951_attributes,
};

static const struct iio_info ad9951_info = {
	.attrs = &ad9951_attribute_group,
	.driver_module = THIS_MODULE,
};

static int ad9951_probe(struct spi_device *spi)
{
	struct ad9951_state *st;
	struct iio_dev *idev;
	int ret = 0;

	idev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!idev)
		return -ENOMEM;
	spi_set_drvdata(spi, idev);
	st = iio_priv(idev);
	mutex_init(&st->lock);
	st->sdev = spi;

	idev->dev.parent = &spi->dev;

	idev->info = &ad9951_info;
	idev->modes = INDIO_DIRECT_MODE;

	ret = iio_device_register(idev);
	if (ret)
		return ret;
	spi->max_speed_hz = 2000000;
	spi->mode = SPI_MODE_3;
	spi->bits_per_word = 8;
	spi_setup(spi);
	ad9951_init(st);
	return 0;
}

static int ad9951_remove(struct spi_device *spi)
{
	iio_device_unregister(spi_get_drvdata(spi));

	return 0;
}

static struct spi_driver ad9951_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
	},
	.probe = ad9951_probe,
	.remove = ad9951_remove,
};
module_spi_driver(ad9951_driver);

MODULE_AUTHOR("Cliff Cai");
MODULE_DESCRIPTION("Analog Devices ad9951 driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("spi:" DRV_NAME);
