/*
 * ADIS16060 Wide Bandwidth Yaw Rate Gyroscope with SPI driver
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define ADIS16060_GYRO		0x20 /* Measure Angular Rate (Gyro) */
#define ADIS16060_TEMP_OUT	0x10 /* Measure Temperature */
#define ADIS16060_AIN2		0x80 /* Measure AIN2 */
#define ADIS16060_AIN1		0x40 /* Measure AIN1 */

/**
 * struct adis16060_state - device instance specific data
 * @us_w:		actual spi_device to write config
 * @us_r:		actual spi_device to read back data
 * @buf:		transmit or receive buffer
 * @buf_lock:		mutex to protect tx and rx
 **/
struct adis16060_state {
	struct spi_device		*us_w;
	struct spi_device		*us_r;
	struct mutex			buf_lock;

	u8 buf[3] ____cacheline_aligned;
};

static struct iio_dev *adis16060_iio_dev;

static int adis16060_spi_write(struct iio_dev *indio_dev, u8 val)
{
	int ret;
	struct adis16060_state *st = iio_priv(indio_dev);

	mutex_lock(&st->buf_lock);
	st->buf[2] = val; /* The last 8 bits clocked in are latched */
	ret = spi_write(st->us_w, st->buf, 3);
	mutex_unlock(&st->buf_lock);

	return ret;
}

static int adis16060_spi_read(struct iio_dev *indio_dev, u16 *val)
{
	int ret;
	struct adis16060_state *st = iio_priv(indio_dev);

	mutex_lock(&st->buf_lock);

	ret = spi_read(st->us_r, st->buf, 3);

	/* The internal successive approximation ADC begins the
	 * conversion process on the falling edge of MSEL1 and
	 * starts to place data MSB first on the DOUT line at
	 * the 6th falling edge of SCLK
	 */
	if (ret == 0)
		*val = ((st->buf[0] & 0x3) << 12) |
			(st->buf[1] << 4) |
			((st->buf[2] >> 4) & 0xF);
	mutex_unlock(&st->buf_lock);

	return ret;
}

static int adis16060_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val, int *val2,
			      long mask)
{
	u16 tval = 0;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		/* Take the iio_dev status lock */
		mutex_lock(&indio_dev->mlock);
		ret = adis16060_spi_write(indio_dev, chan->address);
		if (ret < 0) {
			mutex_unlock(&indio_dev->mlock);
			return ret;
		}
		ret = adis16060_spi_read(indio_dev, &tval);
		mutex_unlock(&indio_dev->mlock);
		*val = tval;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_OFFSET:
		*val = -7;
		*val2 = 461117;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = 34000;
		return IIO_VAL_INT_PLUS_MICRO;
	}

	return -EINVAL;
}

static const struct iio_info adis16060_info = {
	.read_raw = &adis16060_read_raw,
	.driver_module = THIS_MODULE,
};

static const struct iio_chan_spec adis16060_channels[] = {
	{
		.type = IIO_ANGL_VEL,
		.modified = 1,
		.channel2 = IIO_MOD_Z,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
		.address = ADIS16060_GYRO,
	}, {
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 0,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
		.address = ADIS16060_AIN1,
	}, {
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 1,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,
		.address = ADIS16060_AIN2,
	}, {
		.type = IIO_TEMP,
		.indexed = 1,
		.channel = 0,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_OFFSET_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SEPARATE_BIT,
		.address = ADIS16060_TEMP_OUT,
	}
};

static int __devinit adis16060_r_probe(struct spi_device *spi)
{
	int ret;
	struct adis16060_state *st;
	struct iio_dev *indio_dev;

	/* setup the industrialio driver allocated elements */
	indio_dev = iio_device_alloc(sizeof(*st));
	if (indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	/* this is only used for removal purposes */
	spi_set_drvdata(spi, indio_dev);
	st = iio_priv(indio_dev);
	st->us_r = spi;
	mutex_init(&st->buf_lock);

	indio_dev->name = spi->dev.driver->name;
	indio_dev->dev.parent = &spi->dev;
	indio_dev->info = &adis16060_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = adis16060_channels;
	indio_dev->num_channels = ARRAY_SIZE(adis16060_channels);

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_free_dev;

	adis16060_iio_dev = indio_dev;
	return 0;

error_free_dev:
	iio_device_free(indio_dev);
error_ret:
	return ret;
}

/* fixme, confirm ordering in this function */
static int adis16060_r_remove(struct spi_device *spi)
{
	iio_device_unregister(spi_get_drvdata(spi));
	iio_device_free(spi_get_drvdata(spi));

	return 0;
}

static int __devinit adis16060_w_probe(struct spi_device *spi)
{
	int ret;
	struct iio_dev *indio_dev = adis16060_iio_dev;
	struct adis16060_state *st;
	if (!indio_dev) {
		ret =  -ENODEV;
		goto error_ret;
	}
	st = iio_priv(indio_dev);
	spi_set_drvdata(spi, indio_dev);
	st->us_w = spi;
	return 0;

error_ret:
	return ret;
}

static int adis16060_w_remove(struct spi_device *spi)
{
	return 0;
}

static struct spi_driver adis16060_r_driver = {
	.driver = {
		.name = "adis16060_r",
		.owner = THIS_MODULE,
	},
	.probe = adis16060_r_probe,
	.remove = __devexit_p(adis16060_r_remove),
};

static struct spi_driver adis16060_w_driver = {
	.driver = {
		.name = "adis16060_w",
		.owner = THIS_MODULE,
	},
	.probe = adis16060_w_probe,
	.remove = __devexit_p(adis16060_w_remove),
};

static __init int adis16060_init(void)
{
	int ret;

	ret = spi_register_driver(&adis16060_r_driver);
	if (ret < 0)
		return ret;

	ret = spi_register_driver(&adis16060_w_driver);
	if (ret < 0) {
		spi_unregister_driver(&adis16060_r_driver);
		return ret;
	}

	return 0;
}
module_init(adis16060_init);

static __exit void adis16060_exit(void)
{
	spi_unregister_driver(&adis16060_w_driver);
	spi_unregister_driver(&adis16060_r_driver);
}
module_exit(adis16060_exit);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices ADIS16060 Yaw Rate Gyroscope Driver");
MODULE_LICENSE("GPL v2");
