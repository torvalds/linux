/*
 * adis16400.c	support Analog Devices ADIS16400/5
 *		3d 2g Linear Accelerometers,
 *		3d Gyroscopes,
 *		3d Magnetometers via SPI
 *
 * Copyright (c) 2009 Manuel Stahl <manuel.stahl@iis.fraunhofer.de>
 * Copyright (c) 2007 Jonathan Cameron <jic23@cam.ac.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
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
#include "../ring_generic.h"
#include "../accel/accel.h"
#include "../adc/adc.h"
#include "../gyro/gyro.h"
#include "../magnetometer/magnet.h"

#include "adis16400.h"

#define DRIVER_NAME		"adis16400"

static int adis16400_check_status(struct device *dev);

/* At the moment the spi framework doesn't allow global setting of cs_change.
 * It's in the likely to be added comment at the top of spi.h.
 * This means that use cannot be made of spi_write etc.
 */

/**
 * adis16400_spi_write_reg_8() - write single byte to a register
 * @dev: device associated with child of actual device (iio_dev or iio_trig)
 * @reg_address: the address of the register to be written
 * @val: the value to write
 **/
static int adis16400_spi_write_reg_8(struct device *dev,
		u8 reg_address,
		u8 val)
{
	int ret;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adis16400_state *st = iio_dev_get_devdata(indio_dev);

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADIS16400_WRITE_REG(reg_address);
	st->tx[1] = val;

	ret = spi_write(st->us, st->tx, 2);
	mutex_unlock(&st->buf_lock);

	return ret;
}

/**
 * adis16400_spi_write_reg_16() - write 2 bytes to a pair of registers
 * @dev: device associated with child of actual device (iio_dev or iio_trig)
 * @reg_address: the address of the lower of the two registers. Second register
 *               is assumed to have address one greater.
 * @val: value to be written
 **/
static int adis16400_spi_write_reg_16(struct device *dev,
		u8 lower_reg_address,
		u16 value)
{
	int ret;
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adis16400_state *st = iio_dev_get_devdata(indio_dev);
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
	st->tx[0] = ADIS16400_WRITE_REG(lower_reg_address);
	st->tx[1] = value & 0xFF;
	st->tx[2] = ADIS16400_WRITE_REG(lower_reg_address + 1);
	st->tx[3] = (value >> 8) & 0xFF;

	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);
	ret = spi_sync(st->us, &msg);
	mutex_unlock(&st->buf_lock);

	return ret;
}

/**
 * adis16400_spi_read_reg_16() - read 2 bytes from a 16-bit register
 * @dev: device associated with child of actual device (iio_dev or iio_trig)
 * @reg_address: the address of the lower of the two registers. Second register
 *               is assumed to have address one greater.
 * @val: somewhere to pass back the value read
 **/
static int adis16400_spi_read_reg_16(struct device *dev,
		u8 lower_reg_address,
		u16 *val)
{
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adis16400_state *st = iio_dev_get_devdata(indio_dev);
	int ret;
	struct spi_transfer xfers[] = {
		{
			.tx_buf = st->tx,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
		}, {
			.rx_buf = st->rx,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
		},
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADIS16400_READ_REG(lower_reg_address);
	st->tx[1] = 0;
	st->tx[2] = 0;
	st->tx[3] = 0;

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

static ssize_t adis16400_spi_read_signed(struct device *dev,
		struct device_attribute *attr,
		char *buf,
		unsigned bits)
{
	int ret;
	s16 val = 0;
	unsigned shift = 16 - bits;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = adis16400_spi_read_reg_16(dev, this_attr->address, (u16 *)&val);
	if (ret)
		return ret;

	if (val & ADIS16400_ERROR_ACTIVE)
		adis16400_check_status(dev);
	val = ((s16)(val << shift) >> shift);
	return sprintf(buf, "%d\n", val);
}

static ssize_t adis16400_read_12bit_unsigned(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int ret;
	u16 val = 0;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = adis16400_spi_read_reg_16(dev, this_attr->address, &val);
	if (ret)
		return ret;

	if (val & ADIS16400_ERROR_ACTIVE)
		adis16400_check_status(dev);

	return sprintf(buf, "%u\n", val & 0x0FFF);
}

static ssize_t adis16400_read_14bit_signed(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	ssize_t ret;

	/* Take the iio_dev status lock */
	mutex_lock(&indio_dev->mlock);
	ret =  adis16400_spi_read_signed(dev, attr, buf, 14);
	mutex_unlock(&indio_dev->mlock);

	return ret;
}

static ssize_t adis16400_read_12bit_signed(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	ssize_t ret;

	/* Take the iio_dev status lock */
	mutex_lock(&indio_dev->mlock);
	ret =  adis16400_spi_read_signed(dev, attr, buf, 12);
	mutex_unlock(&indio_dev->mlock);

	return ret;
}

static ssize_t adis16400_write_16bit(struct device *dev,
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
	ret = adis16400_spi_write_reg_16(dev, this_attr->address, val);

error_ret:
	return ret ? ret : len;
}

static ssize_t adis16400_read_frequency(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int ret, len = 0;
	u16 t;
	int sps;
	ret = adis16400_spi_read_reg_16(dev,
			ADIS16400_SMPL_PRD,
			&t);
	if (ret)
		return ret;
	sps =  (t & ADIS16400_SMPL_PRD_TIME_BASE) ? 53 : 1638;
	sps /= (t & ADIS16400_SMPL_PRD_DIV_MASK) + 1;
	len = sprintf(buf, "%d SPS\n", sps);
	return len;
}

static ssize_t adis16400_write_frequency(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct adis16400_state *st = iio_dev_get_devdata(indio_dev);
	long val;
	int ret;
	u8 t;

	ret = strict_strtol(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&indio_dev->mlock);

	t = (1638 / val);
	if (t > 0)
		t--;
	t &= ADIS16400_SMPL_PRD_DIV_MASK;
	if ((t & ADIS16400_SMPL_PRD_DIV_MASK) >= 0x0A)
		st->us->max_speed_hz = ADIS16400_SPI_SLOW;
	else
		st->us->max_speed_hz = ADIS16400_SPI_FAST;

	ret = adis16400_spi_write_reg_8(dev,
			ADIS16400_SMPL_PRD,
			t);

	mutex_unlock(&indio_dev->mlock);

	return ret ? ret : len;
}

static int adis16400_reset(struct device *dev)
{
	int ret;
	ret = adis16400_spi_write_reg_8(dev,
			ADIS16400_GLOB_CMD,
			ADIS16400_GLOB_CMD_SW_RESET);
	if (ret)
		dev_err(dev, "problem resetting device");

	return ret;
}

static ssize_t adis16400_write_reset(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t len)
{
	if (len < 1)
		return -1;
	switch (buf[0]) {
	case '1':
	case 'y':
	case 'Y':
		return adis16400_reset(dev);
	}
	return -1;
}

int adis16400_set_irq(struct device *dev, bool enable)
{
	int ret;
	u16 msc;
	ret = adis16400_spi_read_reg_16(dev, ADIS16400_MSC_CTRL, &msc);
	if (ret)
		goto error_ret;

	msc |= ADIS16400_MSC_CTRL_DATA_RDY_POL_HIGH;
	if (enable)
		msc |= ADIS16400_MSC_CTRL_DATA_RDY_EN;
	else
		msc &= ~ADIS16400_MSC_CTRL_DATA_RDY_EN;

	ret = adis16400_spi_write_reg_16(dev, ADIS16400_MSC_CTRL, msc);
	if (ret)
		goto error_ret;

error_ret:
	return ret;
}

/* Power down the device */
static int adis16400_stop_device(struct device *dev)
{
	int ret;
	u16 val = ADIS16400_SLP_CNT_POWER_OFF;

	ret = adis16400_spi_write_reg_16(dev, ADIS16400_SLP_CNT, val);
	if (ret)
		dev_err(dev, "problem with turning device off: SLP_CNT");

	return ret;
}

static int adis16400_self_test(struct device *dev)
{
	int ret;
	ret = adis16400_spi_write_reg_16(dev,
			ADIS16400_MSC_CTRL,
			ADIS16400_MSC_CTRL_MEM_TEST);
	if (ret) {
		dev_err(dev, "problem starting self test");
		goto err_ret;
	}

	adis16400_check_status(dev);

err_ret:
	return ret;
}

static int adis16400_check_status(struct device *dev)
{
	u16 status;
	int ret;

	ret = adis16400_spi_read_reg_16(dev, ADIS16400_DIAG_STAT, &status);

	if (ret < 0) {
		dev_err(dev, "Reading status failed\n");
		goto error_ret;
	}
	ret = status;
	if (status & ADIS16400_DIAG_STAT_ZACCL_FAIL)
		dev_err(dev, "Z-axis accelerometer self-test failure\n");
	if (status & ADIS16400_DIAG_STAT_YACCL_FAIL)
		dev_err(dev, "Y-axis accelerometer self-test failure\n");
	if (status & ADIS16400_DIAG_STAT_XACCL_FAIL)
		dev_err(dev, "X-axis accelerometer self-test failure\n");
	if (status & ADIS16400_DIAG_STAT_XGYRO_FAIL)
		dev_err(dev, "X-axis gyroscope self-test failure\n");
	if (status & ADIS16400_DIAG_STAT_YGYRO_FAIL)
		dev_err(dev, "Y-axis gyroscope self-test failure\n");
	if (status & ADIS16400_DIAG_STAT_ZGYRO_FAIL)
		dev_err(dev, "Z-axis gyroscope self-test failure\n");
	if (status & ADIS16400_DIAG_STAT_ALARM2)
		dev_err(dev, "Alarm 2 active\n");
	if (status & ADIS16400_DIAG_STAT_ALARM1)
		dev_err(dev, "Alarm 1 active\n");
	if (status & ADIS16400_DIAG_STAT_FLASH_CHK)
		dev_err(dev, "Flash checksum error\n");
	if (status & ADIS16400_DIAG_STAT_SELF_TEST)
		dev_err(dev, "Self test error\n");
	if (status & ADIS16400_DIAG_STAT_OVERFLOW)
		dev_err(dev, "Sensor overrange\n");
	if (status & ADIS16400_DIAG_STAT_SPI_FAIL)
		dev_err(dev, "SPI failure\n");
	if (status & ADIS16400_DIAG_STAT_FLASH_UPT)
		dev_err(dev, "Flash update failed\n");
	if (status & ADIS16400_DIAG_STAT_POWER_HIGH)
		dev_err(dev, "Power supply above 5.25V\n");
	if (status & ADIS16400_DIAG_STAT_POWER_LOW)
		dev_err(dev, "Power supply below 4.75V\n");

error_ret:
	return ret;
}

static int adis16400_initial_setup(struct adis16400_state *st)
{
	int ret;
	u16 prod_id, smp_prd;
	struct device *dev = &st->indio_dev->dev;

	/* use low spi speed for init */
	st->us->max_speed_hz = ADIS16400_SPI_SLOW;
	st->us->mode = SPI_MODE_3;
	spi_setup(st->us);

	/* Disable IRQ */
	ret = adis16400_set_irq(dev, false);
	if (ret) {
		dev_err(dev, "disable irq failed");
		goto err_ret;
	}

	/* Do self test */
	ret = adis16400_self_test(dev);
	if (ret) {
		dev_err(dev, "self test failure");
		goto err_ret;
	}

	/* Read status register to check the result */
	ret = adis16400_check_status(dev);
	if (ret) {
		adis16400_reset(dev);
		dev_err(dev, "device not playing ball -> reset");
		msleep(ADIS16400_STARTUP_DELAY);
		ret = adis16400_check_status(dev);
		if (ret) {
			dev_err(dev, "giving up");
			goto err_ret;
		}
	}

	ret = adis16400_spi_read_reg_16(dev, ADIS16400_PRODUCT_ID, &prod_id);
	if (ret)
		goto err_ret;

	if (prod_id != ADIS16400_PRODUCT_ID_DEFAULT)
		dev_warn(dev, "unknown product id");

	printk(KERN_INFO DRIVER_NAME ": prod_id 0x%04x at CS%d (irq %d)\n",
			prod_id, st->us->chip_select, st->us->irq);

	/* use high spi speed if possible */
	ret = adis16400_spi_read_reg_16(dev, ADIS16400_SMPL_PRD, &smp_prd);
	if (!ret && (smp_prd & ADIS16400_SMPL_PRD_DIV_MASK) < 0x0A) {
		st->us->max_speed_hz = ADIS16400_SPI_SLOW;
		spi_setup(st->us);
	}


err_ret:

	return ret;
}

static IIO_DEV_ATTR_ACCEL_X_OFFSET(S_IWUSR | S_IRUGO,
		adis16400_read_12bit_signed,
		adis16400_write_16bit,
		ADIS16400_XACCL_OFF);

static IIO_DEV_ATTR_ACCEL_Y_OFFSET(S_IWUSR | S_IRUGO,
		adis16400_read_12bit_signed,
		adis16400_write_16bit,
		ADIS16400_YACCL_OFF);

static IIO_DEV_ATTR_ACCEL_Z_OFFSET(S_IWUSR | S_IRUGO,
		adis16400_read_12bit_signed,
		adis16400_write_16bit,
		ADIS16400_ZACCL_OFF);

static IIO_DEV_ATTR_IN_NAMED_RAW(supply, adis16400_read_14bit_signed,
		ADIS16400_SUPPLY_OUT);
static IIO_CONST_ATTR(in_supply_scale, "0.002418");

static IIO_DEV_ATTR_GYRO_X(adis16400_read_14bit_signed,
		ADIS16400_XGYRO_OUT);
static IIO_DEV_ATTR_GYRO_Y(adis16400_read_14bit_signed,
		ADIS16400_YGYRO_OUT);
static IIO_DEV_ATTR_GYRO_Z(adis16400_read_14bit_signed,
		ADIS16400_ZGYRO_OUT);
static IIO_CONST_ATTR(gyro_scale, "0.05 deg/s");

static IIO_DEV_ATTR_ACCEL_X(adis16400_read_14bit_signed,
		ADIS16400_XACCL_OUT);
static IIO_DEV_ATTR_ACCEL_Y(adis16400_read_14bit_signed,
		ADIS16400_YACCL_OUT);
static IIO_DEV_ATTR_ACCEL_Z(adis16400_read_14bit_signed,
		ADIS16400_ZACCL_OUT);
static IIO_CONST_ATTR(accel_scale, "0.00333 g");

static IIO_DEV_ATTR_MAGN_X(adis16400_read_14bit_signed,
		ADIS16400_XMAGN_OUT);
static IIO_DEV_ATTR_MAGN_Y(adis16400_read_14bit_signed,
		ADIS16400_YMAGN_OUT);
static IIO_DEV_ATTR_MAGN_Z(adis16400_read_14bit_signed,
		ADIS16400_ZMAGN_OUT);
static IIO_CONST_ATTR(magn_scale, "0.0005 Gs");


static IIO_DEV_ATTR_TEMP_RAW(adis16400_read_12bit_signed);
static IIO_CONST_ATTR(temp_offset, "198.16 K");
static IIO_CONST_ATTR(temp_scale, "0.14 K");

static IIO_DEV_ATTR_IN_RAW(0, adis16400_read_12bit_unsigned,
		ADIS16400_AUX_ADC);
static IIO_CONST_ATTR(in0_scale, "0.000806");

static IIO_DEV_ATTR_SAMP_FREQ(S_IWUSR | S_IRUGO,
		adis16400_read_frequency,
		adis16400_write_frequency);

static IIO_DEVICE_ATTR(reset, S_IWUSR, NULL, adis16400_write_reset, 0);

static IIO_CONST_ATTR_AVAIL_SAMP_FREQ("409 546 819 1638");

static IIO_CONST_ATTR(name, "adis16400");

static struct attribute *adis16400_event_attributes[] = {
	NULL
};

static struct attribute_group adis16400_event_attribute_group = {
	.attrs = adis16400_event_attributes,
};

static struct attribute *adis16400_attributes[] = {
	&iio_dev_attr_accel_x_offset.dev_attr.attr,
	&iio_dev_attr_accel_y_offset.dev_attr.attr,
	&iio_dev_attr_accel_z_offset.dev_attr.attr,
	&iio_dev_attr_in_supply_raw.dev_attr.attr,
	&iio_const_attr_in_supply_scale.dev_attr.attr,
	&iio_dev_attr_gyro_x_raw.dev_attr.attr,
	&iio_dev_attr_gyro_y_raw.dev_attr.attr,
	&iio_dev_attr_gyro_z_raw.dev_attr.attr,
	&iio_const_attr_gyro_scale.dev_attr.attr,
	&iio_dev_attr_accel_x_raw.dev_attr.attr,
	&iio_dev_attr_accel_y_raw.dev_attr.attr,
	&iio_dev_attr_accel_z_raw.dev_attr.attr,
	&iio_const_attr_accel_scale.dev_attr.attr,
	&iio_dev_attr_magn_x_raw.dev_attr.attr,
	&iio_dev_attr_magn_y_raw.dev_attr.attr,
	&iio_dev_attr_magn_z_raw.dev_attr.attr,
	&iio_const_attr_magn_scale.dev_attr.attr,
	&iio_dev_attr_temp_raw.dev_attr.attr,
	&iio_const_attr_temp_offset.dev_attr.attr,
	&iio_const_attr_temp_scale.dev_attr.attr,
	&iio_dev_attr_in0_raw.dev_attr.attr,
	&iio_const_attr_in0_scale.dev_attr.attr,
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	&iio_const_attr_available_sampling_frequency.dev_attr.attr,
	&iio_dev_attr_reset.dev_attr.attr,
	&iio_const_attr_name.dev_attr.attr,
	NULL
};

static const struct attribute_group adis16400_attribute_group = {
	.attrs = adis16400_attributes,
};

static int __devinit adis16400_probe(struct spi_device *spi)
{
	int ret, regdone = 0;
	struct adis16400_state *st = kzalloc(sizeof *st, GFP_KERNEL);
	if (!st) {
		ret =  -ENOMEM;
		goto error_ret;
	}
	/* this is only used for removal purposes */
	spi_set_drvdata(spi, st);

	/* Allocate the comms buffers */
	st->rx = kzalloc(sizeof(*st->rx)*ADIS16400_MAX_RX, GFP_KERNEL);
	if (st->rx == NULL) {
		ret = -ENOMEM;
		goto error_free_st;
	}
	st->tx = kzalloc(sizeof(*st->tx)*ADIS16400_MAX_TX, GFP_KERNEL);
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
	st->indio_dev->event_attrs = &adis16400_event_attribute_group;
	st->indio_dev->attrs = &adis16400_attribute_group;
	st->indio_dev->dev_data = (void *)(st);
	st->indio_dev->driver_module = THIS_MODULE;
	st->indio_dev->modes = INDIO_DIRECT_MODE;

	ret = adis16400_configure_ring(st->indio_dev);
	if (ret)
		goto error_free_dev;

	ret = iio_device_register(st->indio_dev);
	if (ret)
		goto error_unreg_ring_funcs;
	regdone = 1;

	ret = iio_ring_buffer_register(st->indio_dev->ring, 0);
	if (ret) {
		printk(KERN_ERR "failed to initialize the ring\n");
		goto error_unreg_ring_funcs;
	}

	if (spi->irq && gpio_is_valid(irq_to_gpio(spi->irq)) > 0) {
		ret = iio_register_interrupt_line(spi->irq,
				st->indio_dev,
				0,
				IRQF_TRIGGER_RISING,
				"adis16400");
		if (ret)
			goto error_uninitialize_ring;

		ret = adis16400_probe_trigger(st->indio_dev);
		if (ret)
			goto error_unregister_line;
	}

	/* Get the device into a sane initial state */
	ret = adis16400_initial_setup(st);
	if (ret)
		goto error_remove_trigger;
	return 0;

error_remove_trigger:
	if (st->indio_dev->modes & INDIO_RING_TRIGGERED)
		adis16400_remove_trigger(st->indio_dev);
error_unregister_line:
	if (st->indio_dev->modes & INDIO_RING_TRIGGERED)
		iio_unregister_interrupt_line(st->indio_dev, 0);
error_uninitialize_ring:
	iio_ring_buffer_unregister(st->indio_dev->ring);
error_unreg_ring_funcs:
	adis16400_unconfigure_ring(st->indio_dev);
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
static int adis16400_remove(struct spi_device *spi)
{
	int ret;
	struct adis16400_state *st = spi_get_drvdata(spi);
	struct iio_dev *indio_dev = st->indio_dev;

	ret = adis16400_stop_device(&(indio_dev->dev));
	if (ret)
		goto err_ret;

	flush_scheduled_work();

	adis16400_remove_trigger(indio_dev);
	if (spi->irq && gpio_is_valid(irq_to_gpio(spi->irq)) > 0)
		iio_unregister_interrupt_line(indio_dev, 0);

	iio_ring_buffer_unregister(st->indio_dev->ring);
	adis16400_unconfigure_ring(indio_dev);
	iio_device_unregister(indio_dev);
	kfree(st->tx);
	kfree(st->rx);
	kfree(st);

	return 0;

err_ret:
	return ret;
}

static struct spi_driver adis16400_driver = {
	.driver = {
		.name = "adis16400",
		.owner = THIS_MODULE,
	},
	.probe = adis16400_probe,
	.remove = __devexit_p(adis16400_remove),
};

static __init int adis16400_init(void)
{
	return spi_register_driver(&adis16400_driver);
}
module_init(adis16400_init);

static __exit void adis16400_exit(void)
{
	spi_unregister_driver(&adis16400_driver);
}
module_exit(adis16400_exit);

MODULE_AUTHOR("Manuel Stahl <manuel.stahl@iis.fraunhofer.de>");
MODULE_DESCRIPTION("Analog Devices ADIS16400/5 IMU SPI driver");
MODULE_LICENSE("GPL v2");
