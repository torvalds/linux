/*
 * ADXRS450 Digital Output Gyroscope Driver
 *
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
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

#include "adxrs450.h"

/**
 * adxrs450_spi_read_reg_16() - read 2 bytes from a register pair
 * @dev: device associated with child of actual iio_dev
 * @reg_address: the address of the lower of the two registers,which should be an even address,
 * Second register's address is reg_address + 1.
 * @val: somewhere to pass back the value read
 **/
static int adxrs450_spi_read_reg_16(struct device *dev,
		u8 reg_address,
		u16 *val)
{
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adxrs450_state *st = iio_dev_get_devdata(indio_dev);
	int ret;
	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.bits_per_word = 8,
			.len = 4,
			.cs_change = 1,
		}, {
			.rx_buf = st->rx,
			.bits_per_word = 8,
			.len = 4,
		},
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADXRS450_READ_DATA | (reg_address >> 7);
	st->tx[1] = reg_address << 1;
	st->tx[2] = 0;
	st->tx[3] = 0;

	if (!(hweight32(be32_to_cpu(*(u32 *)st->tx)) & 1))
		st->tx[3]  |= ADXRS450_P;

	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);
	ret = spi_sync(st->us, &msg);
	if (ret) {
		dev_err(&st->us->dev, "problem while reading 16 bit register 0x%02x\n",
				reg_address);
		goto error_ret;
	}

	*val = (be32_to_cpu(*(u32 *)st->rx) >> 5) & 0xFFFF;

error_ret:
	mutex_unlock(&st->buf_lock);
	return ret;
}

/**
 * adxrs450_spi_write_reg_16() - write 2 bytes data to a register pair
 * @dev: device associated with child of actual actual iio_dev
 * @reg_address: the address of the lower of the two registers,which should be an even address,
 * Second register's address is reg_address + 1.
 * @val: value to be written.
 **/
static int adxrs450_spi_write_reg_16(struct device *dev,
		u8 reg_address,
		u16 val)
{
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adxrs450_state *st = iio_dev_get_devdata(indio_dev);
	int ret;
	struct spi_transfer xfers = {
		.tx_buf = st->tx,
		.rx_buf = st->rx,
		.bits_per_word = 8,
		.len = 4,
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADXRS450_WRITE_DATA | reg_address >> 7;
	st->tx[1] = reg_address << 1 | val >> 15;
	st->tx[2] = val >> 7;
	st->tx[3] = val << 1;

	if (!(hweight32(be32_to_cpu(*(u32 *)st->tx)) & 1))
		st->tx[3]  |= ADXRS450_P;

	spi_message_init(&msg);
	spi_message_add_tail(&xfers, &msg);
	ret = spi_sync(st->us, &msg);
	if (ret)
		dev_err(&st->us->dev, "problem while writing 16 bit register 0x%02x\n",
				reg_address);
	msleep(1); /* enforce sequential transfer delay 0.1ms */
	mutex_unlock(&st->buf_lock);
	return ret;
}

/**
 * adxrs450_spi_sensor_data() - read 2 bytes sensor data
 * @dev: device associated with child of actual iio_dev
 * @val: somewhere to pass back the value read
 **/
static int adxrs450_spi_sensor_data(struct device *dev, s16 *val)
{
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adxrs450_state *st = iio_dev_get_devdata(indio_dev);
	int ret;
	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.bits_per_word = 8,
			.len = 4,
			.cs_change = 1,
		}, {
			.rx_buf = st->rx,
			.bits_per_word = 8,
			.len = 4,
		},
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADXRS450_SENSOR_DATA;
	st->tx[1] = 0;
	st->tx[2] = 0;
	st->tx[3] = 0;

	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);
	ret = spi_sync(st->us, &msg);
	if (ret) {
		dev_err(&st->us->dev, "Problem while reading sensor data\n");
		goto error_ret;
	}

	*val = (be32_to_cpu(*(u32 *)st->rx) >> 10) & 0xFFFF;

error_ret:
	mutex_unlock(&st->buf_lock);
	return ret;
}

/**
 * adxrs450_spi_initial() - use for initializing procedure.
 * @st: device instance specific data
 * @val: somewhere to pass back the value read
 **/
static int adxrs450_spi_initial(struct adxrs450_state *st,
		u32 *val, char chk)
{
	struct spi_message msg;
	int ret;
	struct spi_transfer xfers = {
		.tx_buf = st->tx,
		.rx_buf = st->rx,
		.bits_per_word = 8,
		.len = 4,
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADXRS450_SENSOR_DATA;
	st->tx[1] = 0;
	st->tx[2] = 0;
	st->tx[3] = 0;
	if (chk)
		st->tx[3] |= (ADXRS450_CHK | ADXRS450_P);
	spi_message_init(&msg);
	spi_message_add_tail(&xfers, &msg);
	ret = spi_sync(st->us, &msg);
	if (ret) {
		dev_err(&st->us->dev, "Problem while reading initializing data\n");
		goto error_ret;
	}

	*val = be32_to_cpu(*(u32 *)st->rx);

error_ret:
	mutex_unlock(&st->buf_lock);
	return ret;
}

static ssize_t adxrs450_read_temp(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int ret;
	u16 t;
	ret = adxrs450_spi_read_reg_16(dev,
			ADXRS450_TEMP1,
			&t);
	if (ret)
		return ret;
	return sprintf(buf, "%d\n", t >> 7);
}

static ssize_t adxrs450_read_quad(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int ret;
	s16 t;
	ret = adxrs450_spi_read_reg_16(dev,
			ADXRS450_QUAD1,
			&t);
	if (ret)
		return ret;
	return sprintf(buf, "%d\n", t);
}

static ssize_t adxrs450_write_dnc(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	int ret;
	long val;

	ret = strict_strtol(buf, 10, &val);
	if (ret)
		goto error_ret;
	ret = adxrs450_spi_write_reg_16(dev,
			ADXRS450_DNC1,
			val & 0x3FF);
error_ret:
	return ret ? ret : len;
}

static ssize_t adxrs450_read_sensor_data(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int ret;
	s16 t;

	ret = adxrs450_spi_sensor_data(dev, &t);
	if (ret)
		return ret;

	return sprintf(buf, "%d\n", t);
}

/* Recommended Startup Sequence by spec */
static int adxrs450_initial_setup(struct adxrs450_state *st)
{
	u32 t;
	u16 data;
	int ret;
	struct device *dev = &st->indio_dev->dev;

	msleep(ADXRS450_STARTUP_DELAY*2);
	ret = adxrs450_spi_initial(st, &t, 1);
	if (ret)
		return ret;
	if (t != 0x01)
		dev_warn(&st->us->dev, "The initial power on response "
			 "is not correct! Restart without reset?\n");

	msleep(ADXRS450_STARTUP_DELAY);
	ret = adxrs450_spi_initial(st, &t, 0);
	if (ret)
		return ret;

	msleep(ADXRS450_STARTUP_DELAY);
	ret = adxrs450_spi_initial(st, &t, 0);
	if (ret)
		return ret;
	if (((t & 0xff) | 0x01) != 0xff || ADXRS450_GET_ST(t) != 2) {
		dev_err(&st->us->dev, "The second response is not correct!\n");
		return -EIO;

	}
	ret = adxrs450_spi_initial(st, &t, 0);
	if (ret)
		return ret;
	if (((t & 0xff) | 0x01) != 0xff || ADXRS450_GET_ST(t) != 2) {
		dev_err(&st->us->dev, "The third response is not correct!\n");
		return -EIO;

	}
	ret = adxrs450_spi_read_reg_16(dev, ADXRS450_FAULT1, &data);
	if (ret)
		return ret;
	if (data & 0x0fff) {
		dev_err(&st->us->dev, "The device is not in normal status!\n");
		return -EINVAL;
	}
	ret = adxrs450_spi_read_reg_16(dev, ADXRS450_PID1, &data);
	if (ret)
		return ret;
	dev_info(&st->us->dev, "The Part ID is 0x%x\n", data);

	ret = adxrs450_spi_read_reg_16(dev, ADXRS450_SNL, &data);
	if (ret)
		return ret;
	t = data;
	ret = adxrs450_spi_read_reg_16(dev, ADXRS450_SNH, &data);
	if (ret)
		return ret;
	t |= data << 16;
	dev_info(&st->us->dev, "The Serial Number is 0x%x\n", t);

	return 0;
}

static IIO_DEV_ATTR_GYRO_Z(adxrs450_read_sensor_data, 0);
static IIO_DEV_ATTR_TEMP_RAW(adxrs450_read_temp);
static IIO_DEV_ATTR_GYRO_Z_QUADRATURE_CORRECTION(adxrs450_read_quad, 0);
static IIO_DEV_ATTR_GYRO_Z_CALIBBIAS(S_IWUSR,
		NULL, adxrs450_write_dnc, 0);
static IIO_CONST_ATTR(name, "adxrs450");

static struct attribute *adxrs450_attributes[] = {
	&iio_dev_attr_gyro_z_raw.dev_attr.attr,
	&iio_dev_attr_temp_raw.dev_attr.attr,
	&iio_dev_attr_gyro_z_quadrature_correction_raw.dev_attr.attr,
	&iio_dev_attr_gyro_z_calibbias.dev_attr.attr,
	&iio_const_attr_name.dev_attr.attr,
	NULL
};

static const struct attribute_group adxrs450_attribute_group = {
	.attrs = adxrs450_attributes,
};

static const struct iio_info adxrs450_info = {
	.attrs = &adxrs450_attribute_group,
	.driver_module = THIS_MODULE,
};

static int __devinit adxrs450_probe(struct spi_device *spi)
{
	int ret, regdone = 0;
	struct adxrs450_state *st = kzalloc(sizeof *st, GFP_KERNEL);
	if (!st) {
		ret =  -ENOMEM;
		goto error_ret;
	}
	/* This is only used for removal purposes */
	spi_set_drvdata(spi, st);

	/* Allocate the comms buffers */
	st->rx = kzalloc(sizeof(*st->rx)*ADXRS450_MAX_RX, GFP_KERNEL);
	if (st->rx == NULL) {
		ret = -ENOMEM;
		goto error_free_st;
	}
	st->tx = kzalloc(sizeof(*st->tx)*ADXRS450_MAX_TX, GFP_KERNEL);
	if (st->tx == NULL) {
		ret = -ENOMEM;
		goto error_free_rx;
	}
	st->us = spi;
	mutex_init(&st->buf_lock);
	/* setup the industrialio driver allocated elements */
	st->indio_dev = iio_allocate_device(0);
	if (st->indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_free_tx;
	}

	st->indio_dev->dev.parent = &spi->dev;
	st->indio_dev->info = &adxrs450_info;
	st->indio_dev->dev_data = (void *)(st);
	st->indio_dev->modes = INDIO_DIRECT_MODE;

	ret = iio_device_register(st->indio_dev);
	if (ret)
		goto error_free_dev;
	regdone = 1;

	/* Get the device into a sane initial state */
	ret = adxrs450_initial_setup(st);
	if (ret)
		goto error_initial;
	return 0;

error_initial:
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

static int adxrs450_remove(struct spi_device *spi)
{
	struct adxrs450_state *st = spi_get_drvdata(spi);

	iio_device_unregister(st->indio_dev);
	kfree(st->tx);
	kfree(st->rx);
	kfree(st);

	return 0;
}

static struct spi_driver adxrs450_driver = {
	.driver = {
		.name = "adxrs450",
		.owner = THIS_MODULE,
	},
	.probe = adxrs450_probe,
	.remove = __devexit_p(adxrs450_remove),
};

static __init int adxrs450_init(void)
{
	return spi_register_driver(&adxrs450_driver);
}
module_init(adxrs450_init);

static __exit void adxrs450_exit(void)
{
	spi_unregister_driver(&adxrs450_driver);
}
module_exit(adxrs450_exit);

MODULE_AUTHOR("Cliff Cai <cliff.cai@xxxxxxxxxx>");
MODULE_DESCRIPTION("Analog Devices ADXRS450 Gyroscope SPI driver");
MODULE_LICENSE("GPL v2");
