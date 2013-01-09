/*
 * ADIS16080/100 Yaw Rate Gyroscope with SPI driver
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/module.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define ADIS16080_DIN_GYRO   (0 << 10) /* Gyroscope output */
#define ADIS16080_DIN_TEMP   (1 << 10) /* Temperature output */
#define ADIS16080_DIN_AIN1   (2 << 10)
#define ADIS16080_DIN_AIN2   (3 << 10)

/*
 * 1: Write contents on DIN to control register.
 * 0: No changes to control register.
 */

#define ADIS16080_DIN_WRITE  (1 << 15)

/**
 * struct adis16080_state - device instance specific data
 * @us:			actual spi_device to write data
 * @buf:		transmit or receive buffer
 * @buf_lock:		mutex to protect tx and rx
 **/
struct adis16080_state {
	struct spi_device		*us;
	struct mutex			buf_lock;

	__be16 buf ____cacheline_aligned;
};

static int adis16080_read_sample(struct iio_dev *indio_dev,
		u16 addr, int *val)
{
	struct adis16080_state *st = iio_priv(indio_dev);
	struct spi_message m;
	int ret;
	struct spi_transfer	t[] = {
		{
			.tx_buf		= &st->buf,
			.len		= 2,
			.cs_change	= 1,
		}, {
			.rx_buf		= &st->buf,
			.len		= 2,
		},
	};

	mutex_lock(&st->buf_lock);
	st->buf = cpu_to_be16(addr | ADIS16080_DIN_WRITE);

	spi_message_init(&m);
	spi_message_add_tail(&t[0], &m);
	spi_message_add_tail(&t[1], &m);

	ret = spi_sync(st->us, &m);
	if (ret == 0)
		*val = sign_extend32(be16_to_cpu(st->buf), 11);
	mutex_unlock(&st->buf_lock);

	return ret;
}

static int adis16080_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val,
			     int *val2,
			     long mask)
{
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&indio_dev->mlock);
		ret = adis16080_read_sample(indio_dev, chan->address, val);
		mutex_unlock(&indio_dev->mlock);
		return ret ? ret : IIO_VAL_INT;
	}

	return -EINVAL;
}

static const struct iio_chan_spec adis16080_channels[] = {
	{
		.type = IIO_ANGL_VEL,
		.modified = 1,
		.channel2 = IIO_MOD_Z,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
		.address = ADIS16080_DIN_GYRO,
	}, {
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 0,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
		.address = ADIS16080_DIN_AIN1,
	}, {
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 1,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
		.address = ADIS16080_DIN_AIN2,
	}, {
		.type = IIO_TEMP,
		.indexed = 1,
		.channel = 0,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
		.address = ADIS16080_DIN_TEMP,
	}
};

static const struct iio_info adis16080_info = {
	.read_raw = &adis16080_read_raw,
	.driver_module = THIS_MODULE,
};

static int adis16080_probe(struct spi_device *spi)
{
	int ret;
	struct adis16080_state *st;
	struct iio_dev *indio_dev;

	/* setup the industrialio driver allocated elements */
	indio_dev = iio_device_alloc(sizeof(*st));
	if (indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	st = iio_priv(indio_dev);
	/* this is only used for removal purposes */
	spi_set_drvdata(spi, indio_dev);

	/* Allocate the comms buffers */
	st->us = spi;
	mutex_init(&st->buf_lock);

	indio_dev->name = spi->dev.driver->name;
	indio_dev->channels = adis16080_channels;
	indio_dev->num_channels = ARRAY_SIZE(adis16080_channels);
	indio_dev->dev.parent = &spi->dev;
	indio_dev->info = &adis16080_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_free_dev;
	return 0;

error_free_dev:
	iio_device_free(indio_dev);
error_ret:
	return ret;
}

/* fixme, confirm ordering in this function */
static int adis16080_remove(struct spi_device *spi)
{
	iio_device_unregister(spi_get_drvdata(spi));
	iio_device_free(spi_get_drvdata(spi));

	return 0;
}

static const struct spi_device_id adis16080_ids[] = {
	{ "adis16080", 0 },
	{ "adis16100", 0 },
	{},
};
MODULE_DEVICE_TABLE(spi, adis16080_ids);

static struct spi_driver adis16080_driver = {
	.driver = {
		.name = "adis16080",
		.owner = THIS_MODULE,
	},
	.probe = adis16080_probe,
	.remove = adis16080_remove,
	.id_table = adis16080_ids,
};
module_spi_driver(adis16080_driver);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices ADIS16080/100 Yaw Rate Gyroscope Driver");
MODULE_LICENSE("GPL v2");
