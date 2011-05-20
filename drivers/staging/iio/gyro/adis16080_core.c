/*
 * ADIS16080/100 Yaw Rate Gyroscope with SPI driver
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */
#include <linux/gpio.h>
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
 * @indio_dev:		industrial I/O device structure
 * @buf:		transmit or receive buffer
 * @buf_lock:		mutex to protect tx and rx
 **/
struct adis16080_state {
	struct spi_device		*us;
	struct iio_dev			*indio_dev;
	struct mutex			buf_lock;

	u8 buf[2] ____cacheline_aligned;
};

static int adis16080_spi_write(struct device *dev,
		u16 val)
{
	int ret;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adis16080_state *st = iio_dev_get_devdata(indio_dev);

	mutex_lock(&st->buf_lock);
	st->buf[0] = val >> 8;
	st->buf[1] = val;

	ret = spi_write(st->us, st->buf, 2);
	mutex_unlock(&st->buf_lock);

	return ret;
}

static int adis16080_spi_read(struct device *dev,
		u16 *val)
{
	int ret;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adis16080_state *st = iio_dev_get_devdata(indio_dev);

	mutex_lock(&st->buf_lock);

	ret = spi_read(st->us, st->buf, 2);

	if (ret == 0)
		*val = ((st->buf[0] & 0xF) << 8) | st->buf[1];
	mutex_unlock(&st->buf_lock);

	return ret;
}

static ssize_t adis16080_read(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	u16 val = 0;
	ssize_t ret;

	/* Take the iio_dev status lock */
	mutex_lock(&indio_dev->mlock);
	ret = adis16080_spi_write(dev,
				  this_attr->address | ADIS16080_DIN_WRITE);
	if (ret < 0)
		goto error_ret;
	ret =  adis16080_spi_read(dev, &val);
error_ret:
	mutex_unlock(&indio_dev->mlock);

	if (ret == 0)
		return sprintf(buf, "%d\n", val);
	else
		return ret;
}
static IIO_DEV_ATTR_GYRO_Z(adis16080_read, ADIS16080_DIN_GYRO);
static IIO_DEVICE_ATTR(temp_raw, S_IRUGO, adis16080_read, NULL,
		       ADIS16080_DIN_TEMP);
static IIO_DEV_ATTR_IN_RAW(0, adis16080_read, ADIS16080_DIN_AIN1);
static IIO_DEV_ATTR_IN_RAW(1, adis16080_read, ADIS16080_DIN_AIN2);
static IIO_CONST_ATTR(name, "adis16080");

static struct attribute *adis16080_attributes[] = {
	&iio_dev_attr_gyro_z_raw.dev_attr.attr,
	&iio_dev_attr_temp_raw.dev_attr.attr,
	&iio_dev_attr_in0_raw.dev_attr.attr,
	&iio_dev_attr_in1_raw.dev_attr.attr,
	&iio_const_attr_name.dev_attr.attr,
	NULL
};

static const struct attribute_group adis16080_attribute_group = {
	.attrs = adis16080_attributes,
};

static int __devinit adis16080_probe(struct spi_device *spi)
{
	int ret, regdone = 0;
	struct adis16080_state *st = kzalloc(sizeof *st, GFP_KERNEL);
	if (!st) {
		ret =  -ENOMEM;
		goto error_ret;
	}
	/* this is only used for removal purposes */
	spi_set_drvdata(spi, st);

	/* Allocate the comms buffers */
	st->us = spi;
	mutex_init(&st->buf_lock);
	/* setup the industrialio driver allocated elements */
	st->indio_dev = iio_allocate_device();
	if (st->indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_free_st;
	}

	st->indio_dev->dev.parent = &spi->dev;
	st->indio_dev->attrs = &adis16080_attribute_group;
	st->indio_dev->dev_data = (void *)(st);
	st->indio_dev->driver_module = THIS_MODULE;
	st->indio_dev->modes = INDIO_DIRECT_MODE;

	ret = iio_device_register(st->indio_dev);
	if (ret)
		goto error_free_dev;
	regdone = 1;

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
static int adis16080_remove(struct spi_device *spi)
{
	struct adis16080_state *st = spi_get_drvdata(spi);
	struct iio_dev *indio_dev = st->indio_dev;

	iio_device_unregister(indio_dev);
	kfree(st);

	return 0;
}

static struct spi_driver adis16080_driver = {
	.driver = {
		.name = "adis16080",
		.owner = THIS_MODULE,
	},
	.probe = adis16080_probe,
	.remove = __devexit_p(adis16080_remove),
};

static __init int adis16080_init(void)
{
	return spi_register_driver(&adis16080_driver);
}
module_init(adis16080_init);

static __exit void adis16080_exit(void)
{
	spi_unregister_driver(&adis16080_driver);
}
module_exit(adis16080_exit);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices ADIS16080/100 Yaw Rate Gyroscope Driver");
MODULE_LICENSE("GPL v2");
