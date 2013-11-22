/*
 * ADIS16130 Digital Output, High Precision Angular Rate Sensor driver
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/mutex.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/module.h>

#include <linux/iio/iio.h>

#define ADIS16130_CON         0x0
#define ADIS16130_CON_RD      (1 << 6)
#define ADIS16130_IOP         0x1

/* 1 = data-ready signal low when unread data on all channels; */
#define ADIS16130_IOP_ALL_RDY (1 << 3)
#define ADIS16130_IOP_SYNC    (1 << 0) /* 1 = synchronization enabled */
#define ADIS16130_RATEDATA    0x8 /* Gyroscope output, rate of rotation */
#define ADIS16130_TEMPDATA    0xA /* Temperature output */
#define ADIS16130_RATECS      0x28 /* Gyroscope channel setup */
#define ADIS16130_RATECS_EN   (1 << 3) /* 1 = channel enable; */
#define ADIS16130_TEMPCS      0x2A /* Temperature channel setup */
#define ADIS16130_TEMPCS_EN   (1 << 3)
#define ADIS16130_RATECONV    0x30
#define ADIS16130_TEMPCONV    0x32
#define ADIS16130_MODE        0x38
#define ADIS16130_MODE_24BIT  (1 << 1) /* 1 = 24-bit resolution; */

/**
 * struct adis16130_state - device instance specific data
 * @us:			actual spi_device to write data
 * @buf_lock:		mutex to protect tx and rx
 * @buf:		unified tx/rx buffer
 **/
struct adis16130_state {
	struct spi_device		*us;
	struct mutex			buf_lock;
	u8				buf[4] ____cacheline_aligned;
};

static int adis16130_spi_read(struct iio_dev *indio_dev, u8 reg_addr, u32 *val)
{
	int ret;
	struct adis16130_state *st = iio_priv(indio_dev);
	struct spi_message msg;
	struct spi_transfer xfer = {
		.tx_buf = st->buf,
		.rx_buf = st->buf,
		.len = 4,
	};

	mutex_lock(&st->buf_lock);

	st->buf[0] = ADIS16130_CON_RD | reg_addr;
	st->buf[1] = st->buf[2] = st->buf[3] = 0;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->us, &msg);

	if (ret == 0)
		*val = (st->buf[1] << 16) | (st->buf[2] << 8) | st->buf[3];
	mutex_unlock(&st->buf_lock);

	return ret;
}

static int adis16130_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val, int *val2,
			      long mask)
{
	int ret;
	u32 temp;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		/* Take the iio_dev status lock */
		mutex_lock(&indio_dev->mlock);
		ret = adis16130_spi_read(indio_dev, chan->address, &temp);
		mutex_unlock(&indio_dev->mlock);
		if (ret)
			return ret;
		*val = temp;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			/* 0 degree = 838860, 250 degree = 14260608 */
			*val = 250;
			*val2 = 336440817; /* RAD_TO_DEGREE(14260608 - 8388608) */
			return IIO_VAL_FRACTIONAL;
		case IIO_TEMP:
			/* 0C = 8036283, 105C = 9516048 */
			*val = 105000;
			*val2 = 9516048 - 8036283;
			return IIO_VAL_FRACTIONAL;
		default:
			return -EINVAL;
		}
		break;
	case IIO_CHAN_INFO_OFFSET:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			*val = -8388608;
			return IIO_VAL_INT;
		case IIO_TEMP:
			*val = -8036283;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
		break;
	}

	return -EINVAL;
}

static const struct iio_chan_spec adis16130_channels[] = {
	{
		.type = IIO_ANGL_VEL,
		.modified = 1,
		.channel2 = IIO_MOD_Z,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE) |
			BIT(IIO_CHAN_INFO_OFFSET),
		.address = ADIS16130_RATEDATA,
	}, {
		.type = IIO_TEMP,
		.indexed = 1,
		.channel = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE) |
			BIT(IIO_CHAN_INFO_OFFSET),
		.address = ADIS16130_TEMPDATA,
	}
};

static const struct iio_info adis16130_info = {
	.read_raw = &adis16130_read_raw,
	.driver_module = THIS_MODULE,
};

static int adis16130_probe(struct spi_device *spi)
{
	struct adis16130_state *st;
	struct iio_dev *indio_dev;

	/* setup the industrialio driver allocated elements */
	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;
	st = iio_priv(indio_dev);
	/* this is only used for removal purposes */
	spi_set_drvdata(spi, indio_dev);
	st->us = spi;
	mutex_init(&st->buf_lock);
	indio_dev->name = spi->dev.driver->name;
	indio_dev->channels = adis16130_channels;
	indio_dev->num_channels = ARRAY_SIZE(adis16130_channels);
	indio_dev->dev.parent = &spi->dev;
	indio_dev->info = &adis16130_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	return iio_device_register(indio_dev);
}

static int adis16130_remove(struct spi_device *spi)
{
	iio_device_unregister(spi_get_drvdata(spi));
	return 0;
}

static struct spi_driver adis16130_driver = {
	.driver = {
		.name = "adis16130",
		.owner = THIS_MODULE,
	},
	.probe = adis16130_probe,
	.remove = adis16130_remove,
};
module_spi_driver(adis16130_driver);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices ADIS16130 High Precision Angular Rate");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("spi:adis16130");
