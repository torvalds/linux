/*
 * ADIS16220 Programmable Digital Vibration Sensor driver
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
#include "accel.h"
#include "../adc/adc.h"

#include "adis16220.h"

#define DRIVER_NAME		"adis16220"

/**
 * adis16220_spi_write_reg_8() - write single byte to a register
 * @dev: device associated with child of actual device (iio_dev or iio_trig)
 * @reg_address: the address of the register to be written
 * @val: the value to write
 **/
static int adis16220_spi_write_reg_8(struct device *dev,
		u8 reg_address,
		u8 val)
{
	int ret;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adis16220_state *st = iio_dev_get_devdata(indio_dev);

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADIS16220_WRITE_REG(reg_address);
	st->tx[1] = val;

	ret = spi_write(st->us, st->tx, 2);
	mutex_unlock(&st->buf_lock);

	return ret;
}

/**
 * adis16220_spi_write_reg_16() - write 2 bytes to a pair of registers
 * @dev: device associated with child of actual device (iio_dev or iio_trig)
 * @reg_address: the address of the lower of the two registers. Second register
 *               is assumed to have address one greater.
 * @val: value to be written
 **/
static int adis16220_spi_write_reg_16(struct device *dev,
		u8 lower_reg_address,
		u16 value)
{
	int ret;
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adis16220_state *st = iio_dev_get_devdata(indio_dev);
	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.delay_usecs = 35,
		}, {
			.tx_buf = st->tx + 2,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.delay_usecs = 35,
		},
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADIS16220_WRITE_REG(lower_reg_address);
	st->tx[1] = value & 0xFF;
	st->tx[2] = ADIS16220_WRITE_REG(lower_reg_address + 1);
	st->tx[3] = (value >> 8) & 0xFF;

	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);
	ret = spi_sync(st->us, &msg);
	mutex_unlock(&st->buf_lock);

	return ret;
}

/**
 * adis16220_spi_read_reg_16() - read 2 bytes from a 16-bit register
 * @dev: device associated with child of actual device (iio_dev or iio_trig)
 * @reg_address: the address of the lower of the two registers. Second register
 *               is assumed to have address one greater.
 * @val: somewhere to pass back the value read
 **/
static int adis16220_spi_read_reg_16(struct device *dev,
		u8 lower_reg_address,
		u16 *val)
{
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adis16220_state *st = iio_dev_get_devdata(indio_dev);
	int ret;
	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.delay_usecs = 35,
		}, {
			.rx_buf = st->rx,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.delay_usecs = 35,
		},
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADIS16220_READ_REG(lower_reg_address);
	st->tx[1] = 0;

	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);
	ret = spi_sync(st->us, &msg);
	if (ret) {
		dev_err(&st->us->dev,
			"problem when reading 16 bit register 0x%02X",
			lower_reg_address);
		goto error_ret;
	}
	*val = (st->rx[0] << 8) | st->rx[1];

error_ret:
	mutex_unlock(&st->buf_lock);
	return ret;
}

static ssize_t adis16220_spi_read_signed(struct device *dev,
		struct device_attribute *attr,
		char *buf,
		unsigned bits)
{
	int ret;
	s16 val = 0;
	unsigned shift = 16 - bits;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = adis16220_spi_read_reg_16(dev, this_attr->address, (u16 *)&val);
	if (ret)
		return ret;

	val = ((s16)(val << shift) >> shift);
	return sprintf(buf, "%d\n", val);
}

static ssize_t adis16220_read_12bit_unsigned(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int ret;
	u16 val = 0;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = adis16220_spi_read_reg_16(dev, this_attr->address, &val);
	if (ret)
		return ret;

	return sprintf(buf, "%u\n", val & 0x0FFF);
}

static ssize_t adis16220_read_16bit(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	ssize_t ret;

	/* Take the iio_dev status lock */
	mutex_lock(&indio_dev->mlock);
	ret =  adis16220_spi_read_signed(dev, attr, buf, 16);
	mutex_unlock(&indio_dev->mlock);

	return ret;
}

static ssize_t adis16220_write_16bit(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int ret;
	long val;

	ret = strict_strtol(buf, 10, &val);
	if (ret)
		goto error_ret;
	ret = adis16220_spi_write_reg_16(dev, this_attr->address, val);

error_ret:
	return ret ? ret : len;
}

static int adis16220_capture(struct device *dev)
{
	int ret;
	ret = adis16220_spi_write_reg_16(dev,
			ADIS16220_GLOB_CMD,
			0xBF08); /* initiates a manual data capture */
	if (ret)
		dev_err(dev, "problem beginning capture");

	msleep(10); /* delay for capture to finish */

	return ret;
}

static int adis16220_reset(struct device *dev)
{
	int ret;
	ret = adis16220_spi_write_reg_8(dev,
			ADIS16220_GLOB_CMD,
			ADIS16220_GLOB_CMD_SW_RESET);
	if (ret)
		dev_err(dev, "problem resetting device");

	return ret;
}

static ssize_t adis16220_write_reset(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t len)
{
	if (len < 1)
		return -1;
	switch (buf[0]) {
	case '1':
	case 'y':
	case 'Y':
		return adis16220_reset(dev) == 0 ? len : -EIO;
	}
	return -1;
}

static ssize_t adis16220_write_capture(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t len)
{
	if (len < 1)
		return -1;
	switch (buf[0]) {
	case '1':
	case 'y':
	case 'Y':
		return adis16220_capture(dev) == 0 ? len : -EIO;
	}
	return -1;
}

static int adis16220_check_status(struct device *dev)
{
	u16 status;
	int ret;

	ret = adis16220_spi_read_reg_16(dev, ADIS16220_DIAG_STAT, &status);

	if (ret < 0) {
		dev_err(dev, "Reading status failed\n");
		goto error_ret;
	}
	ret = status & 0x7F;

	if (status & ADIS16220_DIAG_STAT_VIOLATION)
		dev_err(dev, "Capture period violation/interruption\n");
	if (status & ADIS16220_DIAG_STAT_SPI_FAIL)
		dev_err(dev, "SPI failure\n");
	if (status & ADIS16220_DIAG_STAT_FLASH_UPT)
		dev_err(dev, "Flash update failed\n");
	if (status & ADIS16220_DIAG_STAT_POWER_HIGH)
		dev_err(dev, "Power supply above 3.625V\n");
	if (status & ADIS16220_DIAG_STAT_POWER_LOW)
		dev_err(dev, "Power supply below 3.15V\n");

error_ret:
	return ret;
}

static int adis16220_self_test(struct device *dev)
{
	int ret;
	ret = adis16220_spi_write_reg_16(dev,
			ADIS16220_MSC_CTRL,
			ADIS16220_MSC_CTRL_SELF_TEST_EN);
	if (ret) {
		dev_err(dev, "problem starting self test");
		goto err_ret;
	}

	adis16220_check_status(dev);

err_ret:
	return ret;
}

static int adis16220_initial_setup(struct adis16220_state *st)
{
	int ret;
	struct device *dev = &st->indio_dev->dev;

	/* Do self test */
	ret = adis16220_self_test(dev);
	if (ret) {
		dev_err(dev, "self test failure");
		goto err_ret;
	}

	/* Read status register to check the result */
	ret = adis16220_check_status(dev);
	if (ret) {
		adis16220_reset(dev);
		dev_err(dev, "device not playing ball -> reset");
		msleep(ADIS16220_STARTUP_DELAY);
		ret = adis16220_check_status(dev);
		if (ret) {
			dev_err(dev, "giving up");
			goto err_ret;
		}
	}

	printk(KERN_INFO DRIVER_NAME ": at CS%d (irq %d)\n",
			st->us->chip_select, st->us->irq);

err_ret:
	return ret;
}

static ssize_t adis16220_capture_buffer_read(struct adis16220_state *st,
					char *buf,
					loff_t off,
					size_t count,
					int addr)
{
	struct spi_message msg;
	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.delay_usecs = 25,
		}, {
			.tx_buf = st->tx,
			.rx_buf = st->rx,
			.bits_per_word = 8,
			.cs_change = 1,
			.delay_usecs = 25,
		},
	};
	int ret;
	int i;

	if (unlikely(!count))
		return count;

	if ((off >= ADIS16220_CAPTURE_SIZE) || (count & 1) || (off & 1))
		return -EINVAL;

	if (off + count > ADIS16220_CAPTURE_SIZE)
		count = ADIS16220_CAPTURE_SIZE - off;

	/* write the begin position of capture buffer */
	ret = adis16220_spi_write_reg_16(&st->indio_dev->dev,
					ADIS16220_CAPT_PNTR,
					off > 1);
	if (ret)
		return -EIO;

	/* read count/2 values from capture buffer */
	mutex_lock(&st->buf_lock);

	for (i = 0; i < count; i += 2) {
		st->tx[i] = ADIS16220_READ_REG(addr);
		st->tx[i + 1] = 0;
	}
	xfers[1].len = count;

	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);
	ret = spi_sync(st->us, &msg);
	if (ret) {

		mutex_unlock(&st->buf_lock);
		return -EIO;
	}

	memcpy(buf, st->rx, count);

	mutex_unlock(&st->buf_lock);
	return count;
}

static ssize_t adis16220_accel_bin_read(struct file *filp, struct kobject *kobj,
					struct bin_attribute *attr,
					char *buf,
					loff_t off,
					size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adis16220_state *st = iio_dev_get_devdata(indio_dev);

	return adis16220_capture_buffer_read(st, buf,
					off, count,
					ADIS16220_CAPT_BUFA);
}

static struct bin_attribute accel_bin = {
	.attr = {
		.name = "accel_bin",
		.mode = S_IRUGO,
	},
	.read = adis16220_accel_bin_read,
	.size = ADIS16220_CAPTURE_SIZE,
};

static ssize_t adis16220_adc1_bin_read(struct file *filp, struct kobject *kobj,
				struct bin_attribute *attr,
				char *buf, loff_t off,
				size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adis16220_state *st = iio_dev_get_devdata(indio_dev);

	return adis16220_capture_buffer_read(st, buf,
					off, count,
					ADIS16220_CAPT_BUF1);
}

static struct bin_attribute adc1_bin = {
	.attr = {
		.name = "in0_bin",
		.mode = S_IRUGO,
	},
	.read =  adis16220_adc1_bin_read,
	.size = ADIS16220_CAPTURE_SIZE,
};

static ssize_t adis16220_adc2_bin_read(struct file *filp, struct kobject *kobj,
				struct bin_attribute *attr,
				char *buf, loff_t off,
				size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adis16220_state *st = iio_dev_get_devdata(indio_dev);

	return adis16220_capture_buffer_read(st, buf,
					off, count,
					ADIS16220_CAPT_BUF2);
}


static struct bin_attribute adc2_bin = {
	.attr = {
		.name = "in1_bin",
		.mode = S_IRUGO,
	},
	.read =  adis16220_adc2_bin_read,
	.size = ADIS16220_CAPTURE_SIZE,
};

static IIO_DEV_ATTR_IN_NAMED_RAW(supply, adis16220_read_12bit_unsigned,
		ADIS16220_CAPT_SUPPLY);
static IIO_CONST_ATTR(in_supply_scale, "0.0012207");
static IIO_DEV_ATTR_ACCEL(adis16220_read_16bit, ADIS16220_CAPT_BUFA);
static IIO_DEVICE_ATTR(accel_peak_raw, S_IRUGO, adis16220_read_16bit,
		NULL, ADIS16220_CAPT_PEAKA);
static IIO_DEV_ATTR_ACCEL_OFFSET(S_IWUSR | S_IRUGO,
		adis16220_read_16bit,
		adis16220_write_16bit,
		ADIS16220_ACCL_NULL);
static IIO_DEV_ATTR_TEMP_RAW(adis16220_read_12bit_unsigned);
static IIO_CONST_ATTR(temp_offset, "25");
static IIO_CONST_ATTR(temp_scale, "-0.47");

static IIO_DEV_ATTR_IN_RAW(0, adis16220_read_16bit, ADIS16220_CAPT_BUF1);
static IIO_DEV_ATTR_IN_RAW(1, adis16220_read_16bit, ADIS16220_CAPT_BUF2);

static IIO_DEVICE_ATTR(reset, S_IWUSR, NULL,
		adis16220_write_reset, 0);

#define IIO_DEV_ATTR_CAPTURE(_store)				\
	IIO_DEVICE_ATTR(capture, S_IWUSR, NULL, _store, 0)

static IIO_DEV_ATTR_CAPTURE(adis16220_write_capture);

#define IIO_DEV_ATTR_CAPTURE_COUNT(_mode, _show, _store, _addr)		\
	IIO_DEVICE_ATTR(capture_count, _mode, _show, _store, _addr)

static IIO_DEV_ATTR_CAPTURE_COUNT(S_IWUSR | S_IRUGO,
		adis16220_read_16bit,
		adis16220_write_16bit,
		ADIS16220_CAPT_PNTR);

static IIO_CONST_ATTR_AVAIL_SAMP_FREQ("100200");

static IIO_CONST_ATTR(name, "adis16220");

static struct attribute *adis16220_attributes[] = {
	&iio_dev_attr_in_supply_raw.dev_attr.attr,
	&iio_const_attr_in_supply_scale.dev_attr.attr,
	&iio_dev_attr_accel_raw.dev_attr.attr,
	&iio_dev_attr_accel_offset.dev_attr.attr,
	&iio_dev_attr_accel_peak_raw.dev_attr.attr,
	&iio_dev_attr_temp_raw.dev_attr.attr,
	&iio_dev_attr_in0_raw.dev_attr.attr,
	&iio_dev_attr_in1_raw.dev_attr.attr,
	&iio_const_attr_temp_offset.dev_attr.attr,
	&iio_const_attr_temp_scale.dev_attr.attr,
	&iio_const_attr_available_sampling_frequency.dev_attr.attr,
	&iio_dev_attr_reset.dev_attr.attr,
	&iio_dev_attr_capture.dev_attr.attr,
	&iio_dev_attr_capture_count.dev_attr.attr,
	&iio_const_attr_name.dev_attr.attr,
	NULL
};

static const struct attribute_group adis16220_attribute_group = {
	.attrs = adis16220_attributes,
};

static int __devinit adis16220_probe(struct spi_device *spi)
{
	int ret, regdone = 0;
	struct adis16220_state *st = kzalloc(sizeof *st, GFP_KERNEL);
	if (!st) {
		ret =  -ENOMEM;
		goto error_ret;
	}
	/* this is only used for removal purposes */
	spi_set_drvdata(spi, st);

	/* Allocate the comms buffers */
	st->rx = kzalloc(sizeof(*st->rx)*ADIS16220_MAX_RX, GFP_KERNEL);
	if (st->rx == NULL) {
		ret = -ENOMEM;
		goto error_free_st;
	}
	st->tx = kzalloc(sizeof(*st->tx)*ADIS16220_MAX_TX, GFP_KERNEL);
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
	st->indio_dev->attrs = &adis16220_attribute_group;
	st->indio_dev->dev_data = (void *)(st);
	st->indio_dev->driver_module = THIS_MODULE;
	st->indio_dev->modes = INDIO_DIRECT_MODE;

	ret = iio_device_register(st->indio_dev);
	if (ret)
		goto error_free_dev;
	regdone = 1;

	ret = sysfs_create_bin_file(&st->indio_dev->dev.kobj, &accel_bin);
	if (ret)
		goto error_free_dev;

	ret = sysfs_create_bin_file(&st->indio_dev->dev.kobj, &adc1_bin);
	if (ret)
		goto error_rm_accel_bin;

	ret = sysfs_create_bin_file(&st->indio_dev->dev.kobj, &adc2_bin);
	if (ret)
		goto error_rm_adc1_bin;

	/* Get the device into a sane initial state */
	ret = adis16220_initial_setup(st);
	if (ret)
		goto error_rm_adc2_bin;
	return 0;

error_rm_adc2_bin:
	sysfs_remove_bin_file(&st->indio_dev->dev.kobj, &adc2_bin);
error_rm_adc1_bin:
	sysfs_remove_bin_file(&st->indio_dev->dev.kobj, &adc1_bin);
error_rm_accel_bin:
	sysfs_remove_bin_file(&st->indio_dev->dev.kobj, &accel_bin);
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

static int adis16220_remove(struct spi_device *spi)
{
	struct adis16220_state *st = spi_get_drvdata(spi);
	struct iio_dev *indio_dev = st->indio_dev;

	flush_scheduled_work();

	sysfs_remove_bin_file(&st->indio_dev->dev.kobj, &adc2_bin);
	sysfs_remove_bin_file(&st->indio_dev->dev.kobj, &adc1_bin);
	sysfs_remove_bin_file(&st->indio_dev->dev.kobj, &accel_bin);
	iio_device_unregister(indio_dev);
	kfree(st->tx);
	kfree(st->rx);
	kfree(st);

	return 0;
}

static struct spi_driver adis16220_driver = {
	.driver = {
		.name = "adis16220",
		.owner = THIS_MODULE,
	},
	.probe = adis16220_probe,
	.remove = __devexit_p(adis16220_remove),
};

static __init int adis16220_init(void)
{
	return spi_register_driver(&adis16220_driver);
}
module_init(adis16220_init);

static __exit void adis16220_exit(void)
{
	spi_unregister_driver(&adis16220_driver);
}
module_exit(adis16220_exit);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices ADIS16220 Digital Vibration Sensor");
MODULE_LICENSE("GPL v2");
