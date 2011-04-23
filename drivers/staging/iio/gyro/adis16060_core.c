/*
 * ADIS16060 Wide Bandwidth Yaw Rate Gyroscope with SPI driver
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


#include "../iio.h"
#include "../sysfs.h"
#include "gyro.h"
#include "../adc/adc.h"

#define ADIS16060_GYRO		0x20 /* Measure Angular Rate (Gyro) */
#define ADIS16060_TEMP_OUT	0x10 /* Measure Temperature */
#define ADIS16060_AIN2		0x80 /* Measure AIN2 */
#define ADIS16060_AIN1		0x40 /* Measure AIN1 */

/**
 * struct adis16060_state - device instance specific data
 * @us_w:		actual spi_device to write config
 * @us_r:		actual spi_device to read back data
 * @indio_dev:		industrial I/O device structure
 * @buf:		transmit or receive buffer
 * @buf_lock:		mutex to protect tx and rx
 **/
struct adis16060_state {
	struct spi_device		*us_w;
	struct spi_device		*us_r;
	struct iio_dev			*indio_dev;
	struct mutex			buf_lock;

	u8 buf[3] ____cacheline_aligned;
};

static struct adis16060_state *adis16060_st;

static int adis16060_spi_write(struct device *dev,
		u8 val)
{
	int ret;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adis16060_state *st = iio_dev_get_devdata(indio_dev);

	mutex_lock(&st->buf_lock);
	st->buf[2] = val; /* The last 8 bits clocked in are latched */
	ret = spi_write(st->us_w, st->buf, 3);
	mutex_unlock(&st->buf_lock);

	return ret;
}

static int adis16060_spi_read(struct device *dev,
		u16 *val)
{
	int ret;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adis16060_state *st = iio_dev_get_devdata(indio_dev);

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

static ssize_t adis16060_read(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	u16 val = 0;
	ssize_t ret;

	/* Take the iio_dev status lock */
	mutex_lock(&indio_dev->mlock);

	ret = adis16060_spi_write(dev, this_attr->address);
	if (ret < 0)
		goto error_ret;
	ret = adis16060_spi_read(dev, &val);
error_ret:
	mutex_unlock(&indio_dev->mlock);

	if (ret == 0)
		return sprintf(buf, "%d\n", val);
	else
		return ret;
}

static IIO_DEV_ATTR_GYRO_Z(adis16060_read, ADIS16060_GYRO);
static IIO_DEVICE_ATTR(temp_raw, S_IRUGO, adis16060_read, NULL,
		       ADIS16060_TEMP_OUT);
static IIO_CONST_ATTR_TEMP_SCALE("34"); /* Milli degrees C */
static IIO_CONST_ATTR_TEMP_OFFSET("-7461.117"); /* Milli degrees C */
static IIO_DEV_ATTR_IN_RAW(0, adis16060_read, ADIS16060_AIN1);
static IIO_DEV_ATTR_IN_RAW(1, adis16060_read, ADIS16060_AIN2);
static IIO_CONST_ATTR(name, "adis16060");

static struct attribute *adis16060_attributes[] = {
	&iio_dev_attr_gyro_z_raw.dev_attr.attr,
	&iio_dev_attr_temp_raw.dev_attr.attr,
	&iio_const_attr_temp_scale.dev_attr.attr,
	&iio_const_attr_temp_offset.dev_attr.attr,
	&iio_dev_attr_in0_raw.dev_attr.attr,
	&iio_dev_attr_in1_raw.dev_attr.attr,
	&iio_const_attr_name.dev_attr.attr,
	NULL
};

static const struct attribute_group adis16060_attribute_group = {
	.attrs = adis16060_attributes,
};

static int __devinit adis16060_r_probe(struct spi_device *spi)
{
	int ret, regdone = 0;
	struct adis16060_state *st = kzalloc(sizeof *st, GFP_KERNEL);
	if (!st) {
		ret =  -ENOMEM;
		goto error_ret;
	}
	/* this is only used for removal purposes */
	spi_set_drvdata(spi, st);

	st->us_r = spi;
	mutex_init(&st->buf_lock);
	/* setup the industrialio driver allocated elements */
	st->indio_dev = iio_allocate_device();
	if (st->indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_free_st;
	}

	st->indio_dev->dev.parent = &spi->dev;
	st->indio_dev->attrs = &adis16060_attribute_group;
	st->indio_dev->dev_data = (void *)(st);
	st->indio_dev->driver_module = THIS_MODULE;
	st->indio_dev->modes = INDIO_DIRECT_MODE;

	ret = iio_device_register(st->indio_dev);
	if (ret)
		goto error_free_dev;
	regdone = 1;

	adis16060_st = st;
	return 0;

error_free_dev:
	if (regdone)
		iio_device_unregister(st->indio_dev);
	else
		iio_free_device(st->indio_dev);
error_free_st:
	kfree(st);
error_ret:
	return ret;
}

/* fixme, confirm ordering in this function */
static int adis16060_r_remove(struct spi_device *spi)
{
	struct adis16060_state *st = spi_get_drvdata(spi);
	struct iio_dev *indio_dev = st->indio_dev;

	iio_device_unregister(indio_dev);
	kfree(st);

	return 0;
}

static int __devinit adis16060_w_probe(struct spi_device *spi)
{
	int ret;
	struct adis16060_state *st = adis16060_st;
	if (!st) {
		ret =  -ENODEV;
		goto error_ret;
	}
	spi_set_drvdata(spi, st);
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
