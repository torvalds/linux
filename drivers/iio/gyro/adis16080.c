// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ADIS16080/100 Yaw Rate Gyroscope with SPI driver
 *
 * Copyright 2010 Analog Devices Inc.
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

struct adis16080_chip_info {
	int scale_val;
	int scale_val2;
};

/**
 * struct adis16080_state - device instance specific data
 * @us:			actual spi_device to write data
 * @info:		chip specific parameters
 * @buf:		transmit or receive buffer
 * @lock:		lock to protect buffer during reads
 **/
struct adis16080_state {
	struct spi_device		*us;
	const struct adis16080_chip_info *info;
	struct mutex			lock;

	__be16 buf ____cacheline_aligned;
};

static int adis16080_read_sample(struct iio_dev *indio_dev,
		u16 addr, int *val)
{
	struct adis16080_state *st = iio_priv(indio_dev);
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

	st->buf = cpu_to_be16(addr | ADIS16080_DIN_WRITE);

	ret = spi_sync_transfer(st->us, t, ARRAY_SIZE(t));
	if (ret == 0)
		*val = sign_extend32(be16_to_cpu(st->buf), 11);

	return ret;
}

static int adis16080_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val,
			     int *val2,
			     long mask)
{
	struct adis16080_state *st = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&st->lock);
		ret = adis16080_read_sample(indio_dev, chan->address, val);
		mutex_unlock(&st->lock);
		return ret ? ret : IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			*val = st->info->scale_val;
			*val2 = st->info->scale_val2;
			return IIO_VAL_FRACTIONAL;
		case IIO_VOLTAGE:
			/* VREF = 5V, 12 bits */
			*val = 5000;
			*val2 = 12;
			return IIO_VAL_FRACTIONAL_LOG2;
		case IIO_TEMP:
			/* 85 C = 585, 25 C = 0 */
			*val = 85000 - 25000;
			*val2 = 585;
			return IIO_VAL_FRACTIONAL;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_OFFSET:
		switch (chan->type) {
		case IIO_VOLTAGE:
			/* 2.5 V = 0 */
			*val = 2048;
			return IIO_VAL_INT;
		case IIO_TEMP:
			/* 85 C = 585, 25 C = 0 */
			*val = DIV_ROUND_CLOSEST(25 * 585, 85 - 25);
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	default:
		break;
	}

	return -EINVAL;
}

static const struct iio_chan_spec adis16080_channels[] = {
	{
		.type = IIO_ANGL_VEL,
		.modified = 1,
		.channel2 = IIO_MOD_Z,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE),
		.address = ADIS16080_DIN_GYRO,
	}, {
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE) |
			BIT(IIO_CHAN_INFO_OFFSET),
		.address = ADIS16080_DIN_AIN1,
	}, {
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE) |
			BIT(IIO_CHAN_INFO_OFFSET),
		.address = ADIS16080_DIN_AIN2,
	}, {
		.type = IIO_TEMP,
		.indexed = 1,
		.channel = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE) |
			BIT(IIO_CHAN_INFO_OFFSET),
		.address = ADIS16080_DIN_TEMP,
	}
};

static const struct iio_info adis16080_info = {
	.read_raw = &adis16080_read_raw,
};

enum {
	ID_ADIS16080,
	ID_ADIS16100,
};

static const struct adis16080_chip_info adis16080_chip_info[] = {
	[ID_ADIS16080] = {
		/* 80 degree = 819, 819 rad = 46925 degree */
		.scale_val = 80,
		.scale_val2 = 46925,
	},
	[ID_ADIS16100] = {
		/* 300 degree = 1230, 1230 rad = 70474 degree */
		.scale_val = 300,
		.scale_val2 = 70474,
	},
};

static int adis16080_probe(struct spi_device *spi)
{
	const struct spi_device_id *id = spi_get_device_id(spi);
	struct adis16080_state *st;
	struct iio_dev *indio_dev;

	/* setup the industrialio driver allocated elements */
	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;
	st = iio_priv(indio_dev);

	mutex_init(&st->lock);

	/* Allocate the comms buffers */
	st->us = spi;
	st->info = &adis16080_chip_info[id->driver_data];

	indio_dev->name = spi->dev.driver->name;
	indio_dev->channels = adis16080_channels;
	indio_dev->num_channels = ARRAY_SIZE(adis16080_channels);
	indio_dev->info = &adis16080_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct spi_device_id adis16080_ids[] = {
	{ "adis16080", ID_ADIS16080 },
	{ "adis16100", ID_ADIS16100 },
	{},
};
MODULE_DEVICE_TABLE(spi, adis16080_ids);

static struct spi_driver adis16080_driver = {
	.driver = {
		.name = "adis16080",
	},
	.probe = adis16080_probe,
	.id_table = adis16080_ids,
};
module_spi_driver(adis16080_driver);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices ADIS16080/100 Yaw Rate Gyroscope Driver");
MODULE_LICENSE("GPL v2");
