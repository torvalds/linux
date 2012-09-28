/*
 * kxsd9.c	simple support for the Kionix KXSD9 3D
 *		accelerometer.
 *
 * Copyright (c) 2008-2009 Jonathan Cameron <jic23@kernel.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * The i2c interface is very similar, so shouldn't be a problem once
 * I have a suitable wire made up.
 *
 * TODO:	Support the motion detector
 *		Uses register address incrementing so could have a
 *		heavily optimized ring buffer access function.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/module.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define KXSD9_REG_X		0x00
#define KXSD9_REG_Y		0x02
#define KXSD9_REG_Z		0x04
#define KXSD9_REG_AUX		0x06
#define KXSD9_REG_RESET		0x0a
#define KXSD9_REG_CTRL_C	0x0c

#define KXSD9_FS_MASK		0x03

#define KXSD9_REG_CTRL_B	0x0d
#define KXSD9_REG_CTRL_A	0x0e

#define KXSD9_READ(a) (0x80 | (a))
#define KXSD9_WRITE(a) (a)

#define KXSD9_STATE_RX_SIZE 2
#define KXSD9_STATE_TX_SIZE 2
/**
 * struct kxsd9_state - device related storage
 * @buf_lock:	protect the rx and tx buffers.
 * @us:		spi device
 * @rx:		single rx buffer storage
 * @tx:		single tx buffer storage
 **/
struct kxsd9_state {
	struct mutex buf_lock;
	struct spi_device *us;
	u8 rx[KXSD9_STATE_RX_SIZE] ____cacheline_aligned;
	u8 tx[KXSD9_STATE_TX_SIZE];
};

#define KXSD9_SCALE_2G "0.011978"
#define KXSD9_SCALE_4G "0.023927"
#define KXSD9_SCALE_6G "0.035934"
#define KXSD9_SCALE_8G "0.047853"

/* reverse order */
static const int kxsd9_micro_scales[4] = { 47853, 35934, 23927, 11978 };

static int kxsd9_write_scale(struct iio_dev *indio_dev, int micro)
{
	int ret, i;
	struct kxsd9_state *st = iio_priv(indio_dev);
	bool foundit = false;

	for (i = 0; i < 4; i++)
		if (micro == kxsd9_micro_scales[i]) {
			foundit = true;
			break;
		}
	if (!foundit)
		return -EINVAL;

	mutex_lock(&st->buf_lock);
	ret = spi_w8r8(st->us, KXSD9_READ(KXSD9_REG_CTRL_C));
	if (ret)
		goto error_ret;
	st->tx[0] = KXSD9_WRITE(KXSD9_REG_CTRL_C);
	st->tx[1] = (ret & ~KXSD9_FS_MASK) | i;

	ret = spi_write(st->us, st->tx, 2);
error_ret:
	mutex_unlock(&st->buf_lock);
	return ret;
}

static int kxsd9_read(struct iio_dev *indio_dev, u8 address)
{
	struct spi_message msg;
	int ret;
	struct kxsd9_state *st = iio_priv(indio_dev);
	struct spi_transfer xfers[] = {
		{
			.bits_per_word = 8,
			.len = 1,
			.delay_usecs = 200,
			.tx_buf = st->tx,
		}, {
			.bits_per_word = 8,
			.len = 2,
			.rx_buf = st->rx,
		},
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = KXSD9_READ(address);
	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);
	ret = spi_sync(st->us, &msg);
	if (ret)
		return ret;
	return (((u16)(st->rx[0])) << 8) | (st->rx[1] & 0xF0);
}

static IIO_CONST_ATTR(accel_scale_available,
		KXSD9_SCALE_2G " "
		KXSD9_SCALE_4G " "
		KXSD9_SCALE_6G " "
		KXSD9_SCALE_8G);

static struct attribute *kxsd9_attributes[] = {
	&iio_const_attr_accel_scale_available.dev_attr.attr,
	NULL,
};

static int kxsd9_write_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int val,
			   int val2,
			   long mask)
{
	int ret = -EINVAL;

	if (mask == IIO_CHAN_INFO_SCALE) {
		/* Check no integer component */
		if (val)
			return -EINVAL;
		ret = kxsd9_write_scale(indio_dev, val2);
	}

	return ret;
}

static int kxsd9_read_raw(struct iio_dev *indio_dev,
			  struct iio_chan_spec const *chan,
			  int *val, int *val2, long mask)
{
	int ret = -EINVAL;
	struct kxsd9_state *st = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = kxsd9_read(indio_dev, chan->address);
		if (ret < 0)
			goto error_ret;
		*val = ret;
		break;
	case IIO_CHAN_INFO_SCALE:
		ret = spi_w8r8(st->us, KXSD9_READ(KXSD9_REG_CTRL_C));
		if (ret)
			goto error_ret;
		*val2 = kxsd9_micro_scales[ret & KXSD9_FS_MASK];
		ret = IIO_VAL_INT_PLUS_MICRO;
		break;
	}

error_ret:
	return ret;
};
#define KXSD9_ACCEL_CHAN(axis)						\
	{								\
		.type = IIO_ACCEL,					\
		.modified = 1,						\
		.channel2 = IIO_MOD_##axis,				\
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |		\
			IIO_CHAN_INFO_SCALE_SHARED_BIT,			\
		.address = KXSD9_REG_##axis,				\
	}

static const struct iio_chan_spec kxsd9_channels[] = {
	KXSD9_ACCEL_CHAN(X), KXSD9_ACCEL_CHAN(Y), KXSD9_ACCEL_CHAN(Z),
	{
		.type = IIO_VOLTAGE,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
		.indexed = 1,
		.address = KXSD9_REG_AUX,
	}
};

static const struct attribute_group kxsd9_attribute_group = {
	.attrs = kxsd9_attributes,
};

static int __devinit kxsd9_power_up(struct kxsd9_state *st)
{
	int ret;

	st->tx[0] = 0x0d;
	st->tx[1] = 0x40;
	ret = spi_write(st->us, st->tx, 2);
	if (ret)
		return ret;

	st->tx[0] = 0x0c;
	st->tx[1] = 0x9b;
	return spi_write(st->us, st->tx, 2);
};

static const struct iio_info kxsd9_info = {
	.read_raw = &kxsd9_read_raw,
	.write_raw = &kxsd9_write_raw,
	.attrs = &kxsd9_attribute_group,
	.driver_module = THIS_MODULE,
};

static int __devinit kxsd9_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct kxsd9_state *st;
	int ret = 0;

	indio_dev = iio_device_alloc(sizeof(*st));
	if (indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	st = iio_priv(indio_dev);
	spi_set_drvdata(spi, indio_dev);

	st->us = spi;
	mutex_init(&st->buf_lock);
	indio_dev->channels = kxsd9_channels;
	indio_dev->num_channels = ARRAY_SIZE(kxsd9_channels);
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->dev.parent = &spi->dev;
	indio_dev->info = &kxsd9_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_free_dev;

	spi->mode = SPI_MODE_0;
	spi_setup(spi);
	kxsd9_power_up(st);

	return 0;

error_free_dev:
	iio_device_free(indio_dev);
error_ret:
	return ret;
}

static int __devexit kxsd9_remove(struct spi_device *spi)
{
	iio_device_unregister(spi_get_drvdata(spi));
	iio_device_free(spi_get_drvdata(spi));

	return 0;
}

static const struct spi_device_id kxsd9_id[] = {
	{"kxsd9", 0},
	{ },
};
MODULE_DEVICE_TABLE(spi, kxsd9_id);

static struct spi_driver kxsd9_driver = {
	.driver = {
		.name = "kxsd9",
		.owner = THIS_MODULE,
	},
	.probe = kxsd9_probe,
	.remove = __devexit_p(kxsd9_remove),
	.id_table = kxsd9_id,
};
module_spi_driver(kxsd9_driver);

MODULE_AUTHOR("Jonathan Cameron <jic23@kernel.org>");
MODULE_DESCRIPTION("Kionix KXSD9 SPI driver");
MODULE_LICENSE("GPL v2");
