/*
 * ADIS16204 Programmable High-g Digital Impact Sensor and Recorder
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
#include "../gyro/gyro.h"
#include "../adc/adc.h"

#include "adis16204.h"

#define DRIVER_NAME		"adis16204"

static int adis16204_check_status(struct device *dev);

/**
 * adis16204_spi_write_reg_8() - write single byte to a register
 * @dev: device associated with child of actual device (iio_dev or iio_trig)
 * @reg_address: the address of the register to be written
 * @val: the value to write
 **/
static int adis16204_spi_write_reg_8(struct device *dev,
		u8 reg_address,
		u8 val)
{
	int ret;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adis16204_state *st = iio_dev_get_devdata(indio_dev);

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADIS16204_WRITE_REG(reg_address);
	st->tx[1] = val;

	ret = spi_write(st->us, st->tx, 2);
	mutex_unlock(&st->buf_lock);

	return ret;
}

/**
 * adis16204_spi_write_reg_16() - write 2 bytes to a pair of registers
 * @dev: device associated with child of actual device (iio_dev or iio_trig)
 * @reg_address: the address of the lower of the two registers. Second register
 *               is assumed to have address one greater.
 * @val: value to be written
 **/
static int adis16204_spi_write_reg_16(struct device *dev,
		u8 lower_reg_address,
		u16 value)
{
	int ret;
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adis16204_state *st = iio_dev_get_devdata(indio_dev);
	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
		}, {
			.tx_buf = st->tx + 2,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
		},
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADIS16204_WRITE_REG(lower_reg_address);
	st->tx[1] = value & 0xFF;
	st->tx[2] = ADIS16204_WRITE_REG(lower_reg_address + 1);
	st->tx[3] = (value >> 8) & 0xFF;

	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);
	ret = spi_sync(st->us, &msg);
	mutex_unlock(&st->buf_lock);

	return ret;
}

/**
 * adis16204_spi_read_reg_16() - read 2 bytes from a 16-bit register
 * @dev: device associated with child of actual device (iio_dev or iio_trig)
 * @reg_address: the address of the lower of the two registers. Second register
 *               is assumed to have address one greater.
 * @val: somewhere to pass back the value read
 **/
static int adis16204_spi_read_reg_16(struct device *dev,
		u8 lower_reg_address,
		u16 *val)
{
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adis16204_state *st = iio_dev_get_devdata(indio_dev);
	int ret;
	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.delay_usecs = 20,
		}, {
			.rx_buf = st->rx,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.delay_usecs = 20,
		},
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADIS16204_READ_REG(lower_reg_address);
	st->tx[1] = 0;

	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);
	ret = spi_sync(st->us, &msg);
	if (ret) {
		dev_err(&st->us->dev, "problem when reading 16 bit register 0x%02X",
				lower_reg_address);
		goto error_ret;
	}
	*val = (st->rx[0] << 8) | st->rx[1];

error_ret:
	mutex_unlock(&st->buf_lock);
	return ret;
}

static ssize_t adis16204_read_12bit_unsigned(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int ret;
	u16 val = 0;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = adis16204_spi_read_reg_16(dev, this_attr->address, &val);
	if (ret)
		return ret;

	if (val & ADIS16204_ERROR_ACTIVE)
		adis16204_check_status(dev);

	return sprintf(buf, "%u\n", val & 0x0FFF);
}

static ssize_t adis16204_read_temp(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	ssize_t ret;
	u16 val;

	/* Take the iio_dev status lock */
	mutex_lock(&indio_dev->mlock);

	ret = adis16204_spi_read_reg_16(dev, ADIS16204_TEMP_OUT, (u16 *)&val);
	if (ret)
		goto error_ret;

	if (val & ADIS16204_ERROR_ACTIVE)
		adis16204_check_status(dev);

	val &= 0xFFF;
	ret = sprintf(buf, "%d\n", val);

error_ret:
	mutex_unlock(&indio_dev->mlock);
	return ret;
}

static ssize_t adis16204_read_12bit_signed(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	s16 val = 0;
	ssize_t ret;

	mutex_lock(&indio_dev->mlock);

	ret = adis16204_spi_read_reg_16(dev, this_attr->address, (u16 *)&val);
	if (!ret) {
		if (val & ADIS16204_ERROR_ACTIVE)
			adis16204_check_status(dev);

		val = ((s16)(val << 4) >> 4);
		ret = sprintf(buf, "%d\n", val);
	}

	mutex_unlock(&indio_dev->mlock);

	return ret;
}

static ssize_t adis16204_read_14bit_signed(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	s16 val = 0;
	ssize_t ret;

	mutex_lock(&indio_dev->mlock);

	ret = adis16204_spi_read_reg_16(dev, this_attr->address, (u16 *)&val);
	if (!ret) {
		if (val & ADIS16204_ERROR_ACTIVE)
			adis16204_check_status(dev);

		val = ((s16)(val << 2) >> 2);
		ret = sprintf(buf, "%d\n", val);
	}

	mutex_unlock(&indio_dev->mlock);

	return ret;
}

static ssize_t adis16204_write_16bit(struct device *dev,
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
	ret = adis16204_spi_write_reg_16(dev, this_attr->address, val);

error_ret:
	return ret ? ret : len;
}

static int adis16204_reset(struct device *dev)
{
	int ret;
	ret = adis16204_spi_write_reg_8(dev,
			ADIS16204_GLOB_CMD,
			ADIS16204_GLOB_CMD_SW_RESET);
	if (ret)
		dev_err(dev, "problem resetting device");

	return ret;
}

static ssize_t adis16204_write_reset(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t len)
{
	if (len < 1)
		return -EINVAL;
	switch (buf[0]) {
	case '1':
	case 'y':
	case 'Y':
		return adis16204_reset(dev);
	}
	return -EINVAL;
}

int adis16204_set_irq(struct device *dev, bool enable)
{
	int ret = 0;
	u16 msc;

	ret = adis16204_spi_read_reg_16(dev, ADIS16204_MSC_CTRL, &msc);
	if (ret)
		goto error_ret;

	msc |= ADIS16204_MSC_CTRL_ACTIVE_HIGH;
	msc &= ~ADIS16204_MSC_CTRL_DATA_RDY_DIO2;
	if (enable)
		msc |= ADIS16204_MSC_CTRL_DATA_RDY_EN;
	else
		msc &= ~ADIS16204_MSC_CTRL_DATA_RDY_EN;

	ret = adis16204_spi_write_reg_16(dev, ADIS16204_MSC_CTRL, msc);

error_ret:
	return ret;
}

static int adis16204_check_status(struct device *dev)
{
	u16 status;
	int ret;

	ret = adis16204_spi_read_reg_16(dev, ADIS16204_DIAG_STAT, &status);
	if (ret < 0) {
		dev_err(dev, "Reading status failed\n");
		goto error_ret;
	}
	ret = status & 0x1F;

	if (status & ADIS16204_DIAG_STAT_SELFTEST_FAIL)
		dev_err(dev, "Self test failure\n");
	if (status & ADIS16204_DIAG_STAT_SPI_FAIL)
		dev_err(dev, "SPI failure\n");
	if (status & ADIS16204_DIAG_STAT_FLASH_UPT)
		dev_err(dev, "Flash update failed\n");
	if (status & ADIS16204_DIAG_STAT_POWER_HIGH)
		dev_err(dev, "Power supply above 3.625V\n");
	if (status & ADIS16204_DIAG_STAT_POWER_LOW)
		dev_err(dev, "Power supply below 2.975V\n");

error_ret:
	return ret;
}

static int adis16204_self_test(struct device *dev)
{
	int ret;
	ret = adis16204_spi_write_reg_16(dev,
			ADIS16204_MSC_CTRL,
			ADIS16204_MSC_CTRL_SELF_TEST_EN);
	if (ret) {
		dev_err(dev, "problem starting self test");
		goto err_ret;
	}

	adis16204_check_status(dev);

err_ret:
	return ret;
}

static int adis16204_initial_setup(struct adis16204_state *st)
{
	int ret;
	struct device *dev = &st->indio_dev->dev;

	/* Disable IRQ */
	ret = adis16204_set_irq(dev, false);
	if (ret) {
		dev_err(dev, "disable irq failed");
		goto err_ret;
	}

	/* Do self test */
	ret = adis16204_self_test(dev);
	if (ret) {
		dev_err(dev, "self test failure");
		goto err_ret;
	}

	/* Read status register to check the result */
	ret = adis16204_check_status(dev);
	if (ret) {
		adis16204_reset(dev);
		dev_err(dev, "device not playing ball -> reset");
		msleep(ADIS16204_STARTUP_DELAY);
		ret = adis16204_check_status(dev);
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

static IIO_DEV_ATTR_IN_NAMED_RAW(0, supply, adis16204_read_12bit_unsigned,
		ADIS16204_SUPPLY_OUT);
static IIO_CONST_ATTR(in0_supply_scale, "0.00122");
static IIO_DEV_ATTR_IN_RAW(1, adis16204_read_12bit_unsigned,
		ADIS16204_AUX_ADC);
static IIO_CONST_ATTR(in1_scale, "0.00061");

static IIO_DEV_ATTR_ACCEL_X(adis16204_read_14bit_signed,
		ADIS16204_XACCL_OUT);
static IIO_DEV_ATTR_ACCEL_Y(adis16204_read_14bit_signed,
		ADIS16204_YACCL_OUT);
static IIO_DEV_ATTR_ACCEL_XY(adis16204_read_14bit_signed,
		ADIS16204_XY_RSS_OUT);
static IIO_DEV_ATTR_ACCEL_XPEAK(adis16204_read_14bit_signed,
		ADIS16204_X_PEAK_OUT);
static IIO_DEV_ATTR_ACCEL_YPEAK(adis16204_read_14bit_signed,
		ADIS16204_Y_PEAK_OUT);
static IIO_DEV_ATTR_ACCEL_XYPEAK(adis16204_read_14bit_signed,
		ADIS16204_XY_PEAK_OUT);
static IIO_DEV_ATTR_ACCEL_X_OFFSET(S_IWUSR | S_IRUGO,
		adis16204_read_12bit_signed,
		adis16204_write_16bit,
		ADIS16204_XACCL_NULL);
static IIO_DEV_ATTR_ACCEL_Y_OFFSET(S_IWUSR | S_IRUGO,
		adis16204_read_12bit_signed,
		adis16204_write_16bit,
		ADIS16204_YACCL_NULL);
static IIO_CONST_ATTR(accel_x_scale, "0.017125");
static IIO_CONST_ATTR(accel_y_scale, "0.008407");
static IIO_CONST_ATTR(accel_xy_scale, "0.017125");

static IIO_DEV_ATTR_TEMP_RAW(adis16204_read_temp);
static IIO_CONST_ATTR(temp_offset, "25");
static IIO_CONST_ATTR(temp_scale, "-0.47");

static IIO_DEVICE_ATTR(reset, S_IWUSR, NULL, adis16204_write_reset, 0);

static IIO_CONST_ATTR(name, "adis16204");

static struct attribute *adis16204_event_attributes[] = {
	NULL
};

static struct attribute_group adis16204_event_attribute_group = {
	.attrs = adis16204_event_attributes,
};

static struct attribute *adis16204_attributes[] = {
	&iio_dev_attr_in0_supply_raw.dev_attr.attr,
	&iio_const_attr_in0_supply_scale.dev_attr.attr,
	&iio_dev_attr_temp_raw.dev_attr.attr,
	&iio_const_attr_temp_offset.dev_attr.attr,
	&iio_const_attr_temp_scale.dev_attr.attr,
	&iio_dev_attr_reset.dev_attr.attr,
	&iio_const_attr_name.dev_attr.attr,
	&iio_dev_attr_in1_raw.dev_attr.attr,
	&iio_const_attr_in1_scale.dev_attr.attr,
	&iio_dev_attr_accel_x_raw.dev_attr.attr,
	&iio_dev_attr_accel_y_raw.dev_attr.attr,
	&iio_dev_attr_accel_xy.dev_attr.attr,
	&iio_dev_attr_accel_xpeak.dev_attr.attr,
	&iio_dev_attr_accel_ypeak.dev_attr.attr,
	&iio_dev_attr_accel_xypeak.dev_attr.attr,
	&iio_dev_attr_accel_x_offset.dev_attr.attr,
	&iio_dev_attr_accel_y_offset.dev_attr.attr,
	&iio_const_attr_accel_x_scale.dev_attr.attr,
	&iio_const_attr_accel_y_scale.dev_attr.attr,
	&iio_const_attr_accel_xy_scale.dev_attr.attr,
	NULL
};

static const struct attribute_group adis16204_attribute_group = {
	.attrs = adis16204_attributes,
};

static int __devinit adis16204_probe(struct spi_device *spi)
{
	int ret, regdone = 0;
	struct adis16204_state *st = kzalloc(sizeof *st, GFP_KERNEL);
	if (!st) {
		ret =  -ENOMEM;
		goto error_ret;
	}
	/* this is only used for removal purposes */
	spi_set_drvdata(spi, st);

	/* Allocate the comms buffers */
	st->rx = kzalloc(sizeof(*st->rx)*ADIS16204_MAX_RX, GFP_KERNEL);
	if (st->rx == NULL) {
		ret = -ENOMEM;
		goto error_free_st;
	}
	st->tx = kzalloc(sizeof(*st->tx)*ADIS16204_MAX_TX, GFP_KERNEL);
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
	st->indio_dev->event_attrs = &adis16204_event_attribute_group;
	st->indio_dev->attrs = &adis16204_attribute_group;
	st->indio_dev->dev_data = (void *)(st);
	st->indio_dev->driver_module = THIS_MODULE;
	st->indio_dev->modes = INDIO_DIRECT_MODE;

	ret = adis16204_configure_ring(st->indio_dev);
	if (ret)
		goto error_free_dev;

	ret = iio_device_register(st->indio_dev);
	if (ret)
		goto error_unreg_ring_funcs;
	regdone = 1;

	ret = adis16204_initialize_ring(st->indio_dev->ring);
	if (ret) {
		printk(KERN_ERR "failed to initialize the ring\n");
		goto error_unreg_ring_funcs;
	}

	if (spi->irq) {
		ret = iio_register_interrupt_line(spi->irq,
				st->indio_dev,
				0,
				IRQF_TRIGGER_RISING,
				"adis16204");
		if (ret)
			goto error_uninitialize_ring;

		ret = adis16204_probe_trigger(st->indio_dev);
		if (ret)
			goto error_unregister_line;
	}

	/* Get the device into a sane initial state */
	ret = adis16204_initial_setup(st);
	if (ret)
		goto error_remove_trigger;
	return 0;

error_remove_trigger:
	adis16204_remove_trigger(st->indio_dev);
error_unregister_line:
	if (spi->irq)
		iio_unregister_interrupt_line(st->indio_dev, 0);
error_uninitialize_ring:
	adis16204_uninitialize_ring(st->indio_dev->ring);
error_unreg_ring_funcs:
	adis16204_unconfigure_ring(st->indio_dev);
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

static int adis16204_remove(struct spi_device *spi)
{
	struct adis16204_state *st = spi_get_drvdata(spi);
	struct iio_dev *indio_dev = st->indio_dev;

	flush_scheduled_work();

	adis16204_remove_trigger(indio_dev);
	if (spi->irq)
		iio_unregister_interrupt_line(indio_dev, 0);

	adis16204_uninitialize_ring(indio_dev->ring);
	iio_device_unregister(indio_dev);
	adis16204_unconfigure_ring(indio_dev);
	kfree(st->tx);
	kfree(st->rx);
	kfree(st);

	return 0;
}

static struct spi_driver adis16204_driver = {
	.driver = {
		.name = "adis16204",
		.owner = THIS_MODULE,
	},
	.probe = adis16204_probe,
	.remove = __devexit_p(adis16204_remove),
};

static __init int adis16204_init(void)
{
	return spi_register_driver(&adis16204_driver);
}
module_init(adis16204_init);

static __exit void adis16204_exit(void)
{
	spi_unregister_driver(&adis16204_driver);
}
module_exit(adis16204_exit);

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices ADIS16204 Programmable High-g Digital Impact Sensor and Recorder");
MODULE_LICENSE("GPL v2");
