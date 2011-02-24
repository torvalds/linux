/*
 * ADIS16130 Digital Output, High Precision Angular Rate Sensor driver
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/list.h>

#include "../iio.h"
#include "../sysfs.h"
#include "gyro.h"
#include "../adc/adc.h"

#include "adis16130.h"

#define DRIVER_NAME		"adis16130"

struct adis16130_state *adis16130_st;

int adis16130_spi_write(struct device *dev, u8 reg_addr,
		u8 val)
{
	int ret;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adis16130_state *st = iio_dev_get_devdata(indio_dev);

	mutex_lock(&st->buf_lock);
	st->tx[0] = reg_addr;
	st->tx[1] = val;

	ret = spi_write(st->us, st->tx, 2);
	mutex_unlock(&st->buf_lock);

	return ret;
}

int adis16130_spi_read(struct device *dev, u8 reg_addr,
		u32 *val)
{
	int ret;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adis16130_state *st = iio_dev_get_devdata(indio_dev);

	mutex_lock(&st->buf_lock);

	st->tx[0] = ADIS16130_CON_RD | reg_addr;
	if (st->mode)
		ret = spi_read(st->us, st->rx, 4);
	else
		ret = spi_read(st->us, st->rx, 3);

	if (ret == 0) {
		if (st->mode)
			*val = (st->rx[1] << 16) | (st->rx[2] << 8) | st->rx[3];
		else
			*val = (st->rx[1] << 8) | st->rx[2];
	}

	mutex_unlock(&st->buf_lock);

	return ret;
}

static ssize_t adis16130_gyro_read(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	u32 val;
	ssize_t ret;

	/* Take the iio_dev status lock */
	mutex_lock(&indio_dev->mlock);
	ret =  adis16130_spi_read(dev, ADIS16130_RATEDATA, &val);
	mutex_unlock(&indio_dev->mlock);

	if (ret == 0)
		return sprintf(buf, "%d\n", val);
	else
		return ret;
}

static ssize_t adis16130_temp_read(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	u32 val;
	ssize_t ret;

	/* Take the iio_dev status lock */
	mutex_lock(&indio_dev->mlock);
	ret =  adis16130_spi_read(dev, ADIS16130_TEMPDATA, &val);
	mutex_unlock(&indio_dev->mlock);

	if (ret == 0)
		return sprintf(buf, "%d\n", val);
	else
		return ret;
}

static ssize_t adis16130_bitsmode_read(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adis16130_state *st = iio_dev_get_devdata(indio_dev);

	return sprintf(buf, "%d\n", st->mode);
}

static ssize_t adis16130_bitsmode_write(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	int ret;
	long val;

	ret = strict_strtol(buf, 16, &val);
	if (ret)
		goto error_ret;
	ret = adis16130_spi_write(dev, ADIS16130_MODE, !!val);

error_ret:
	return ret ? ret : len;
}

static IIO_DEV_ATTR_TEMP_RAW(adis16130_temp_read);

static IIO_CONST_ATTR(name, "adis16130");

static IIO_DEV_ATTR_GYRO(adis16130_gyro_read,
		ADIS16130_RATEDATA);

#define IIO_DEV_ATTR_BITS_MODE(_mode, _show, _store, _addr)	\
	IIO_DEVICE_ATTR(bits_mode, _mode, _show, _store, _addr)

static IIO_DEV_ATTR_BITS_MODE(S_IWUSR | S_IRUGO, adis16130_bitsmode_read, adis16130_bitsmode_write,
			ADIS16130_MODE);

static struct attribute *adis16130_event_attributes[] = {
	NULL
};

static struct attribute_group adis16130_event_attribute_group = {
	.attrs = adis16130_event_attributes,
};

static struct attribute *adis16130_attributes[] = {
	&iio_dev_attr_temp_raw.dev_attr.attr,
	&iio_const_attr_name.dev_attr.attr,
	&iio_dev_attr_gyro_raw.dev_attr.attr,
	&iio_dev_attr_bits_mode.dev_attr.attr,
	NULL
};

static const struct attribute_group adis16130_attribute_group = {
	.attrs = adis16130_attributes,
};

static int __devinit adis16130_probe(struct spi_device *spi)
{
	int ret, regdone = 0;
	struct adis16130_state *st = kzalloc(sizeof *st, GFP_KERNEL);
	if (!st) {
		ret =  -ENOMEM;
		goto error_ret;
	}
	/* this is only used for removal purposes */
	spi_set_drvdata(spi, st);

	/* Allocate the comms buffers */
	st->rx = kzalloc(sizeof(*st->rx)*ADIS16130_MAX_RX, GFP_KERNEL);
	if (st->rx == NULL) {
		ret = -ENOMEM;
		goto error_free_st;
	}
	st->tx = kzalloc(sizeof(*st->tx)*ADIS16130_MAX_TX, GFP_KERNEL);
	if (st->tx == NULL) {
		ret = -ENOMEM;
		goto error_free_rx;
	}
	st->us = spi;
	mutex_init(&st->buf_lock);
	/* setup the industrialio driver allocated elements */
	st->indio_dev = iio_allocate_device();
	if (st->indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_free_tx;
	}

	st->indio_dev->dev.parent = &spi->dev;
	st->indio_dev->num_interrupt_lines = 1;
	st->indio_dev->event_attrs = &adis16130_event_attribute_group;
	st->indio_dev->attrs = &adis16130_attribute_group;
	st->indio_dev->dev_data = (void *)(st);
	st->indio_dev->driver_module = THIS_MODULE;
	st->indio_dev->modes = INDIO_DIRECT_MODE;
	st->mode = 1;

	ret = adis16130_configure_ring(st->indio_dev);
	if (ret)
		goto error_free_dev;

	ret = iio_device_register(st->indio_dev);
	if (ret)
		goto error_unreg_ring_funcs;
	regdone = 1;

	ret = adis16130_initialize_ring(st->indio_dev->ring);
	if (ret) {
		printk(KERN_ERR "failed to initialize the ring\n");
		goto error_unreg_ring_funcs;
	}

	if (spi->irq && gpio_is_valid(irq_to_gpio(spi->irq)) > 0) {
		ret = iio_register_interrupt_line(spi->irq,
				st->indio_dev,
				0,
				IRQF_TRIGGER_RISING,
				"adis16130");
		if (ret)
			goto error_uninitialize_ring;

		ret = adis16130_probe_trigger(st->indio_dev);
		if (ret)
			goto error_unregister_line;
	}

	adis16130_st = st;
	return 0;

error_unregister_line:
	if (st->indio_dev->modes & INDIO_RING_TRIGGERED)
		iio_unregister_interrupt_line(st->indio_dev, 0);
error_uninitialize_ring:
	adis16130_uninitialize_ring(st->indio_dev->ring);
error_unreg_ring_funcs:
	adis16130_unconfigure_ring(st->indio_dev);
error_free_dev:
	if (regdone)
		iio_device_unregister(st->indio_dev);
	else
		iio_free_device(st->indio_dev);
error_free_tx:
	kfree(st->tx);
error_free_rx:
	kfree(st->rx);
error_free_st:
	kfree(st);
error_ret:
	return ret;
}

/* fixme, confirm ordering in this function */
static int adis16130_remove(struct spi_device *spi)
{
	struct adis16130_state *st = spi_get_drvdata(spi);
	struct iio_dev *indio_dev = st->indio_dev;

	flush_scheduled_work();

	adis16130_remove_trigger(indio_dev);
	if (spi->irq && gpio_is_valid(irq_to_gpio(spi->irq)) > 0)
		iio_unregister_interrupt_line(indio_dev, 0);

	adis16130_uninitialize_ring(indio_dev->ring);
	adis16130_unconfigure_ring(indio_dev);
	iio_device_unregister(indio_dev);
	kfree(st->tx);
	kfree(st->rx);
	kfree(st);

	return 0;
}

static struct spi_driver adis16130_driver = {
	.driver = {
		.name = "adis16130",
		.owner = THIS_MODULE,
	},
	.probe = adis16130_probe,
	.remove = __devexit_p(adis16130_remove),
};

static __init int adis16130_init(void)
{
	return spi_register_driver(&adis16130_driver);
}
module_init(adis16130_init);

static __exit void adis16130_exit(void)
{
	spi_unregister_driver(&adis16130_driver);
}
module_exit(adis16130_exit);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices ADIS16130 High Precision Angular Rate Sensor driver");
MODULE_LICENSE("GPL v2");
