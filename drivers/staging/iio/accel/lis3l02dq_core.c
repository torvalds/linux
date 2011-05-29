/*
 * lis3l02dq.c	support STMicroelectronics LISD02DQ
 *		3d 2g Linear Accelerometers via SPI
 *
 * Copyright (c) 2007 Jonathan Cameron <jic23@cam.ac.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Settings:
 * 16 bit left justified mode used.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/workqueue.h>
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
#include "../ring_sw.h"

#include "accel.h"

#include "lis3l02dq.h"

/* At the moment the spi framework doesn't allow global setting of cs_change.
 * It's in the likely to be added comment at the top of spi.h.
 * This means that use cannot be made of spi_write etc.
 */

/**
 * lis3l02dq_spi_read_reg_8() - read single byte from a single register
 * @dev: device asosciated with child of actual device (iio_dev or iio_trig)
 * @reg_address: the address of the register to be read
 * @val: pass back the resulting value
 **/
int lis3l02dq_spi_read_reg_8(struct device *dev, u8 reg_address, u8 *val)
{
	int ret;
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct iio_sw_ring_helper_state *h = iio_dev_get_devdata(indio_dev);
	struct lis3l02dq_state *st = lis3l02dq_h_to_s(h);

	struct spi_transfer xfer = {
		.tx_buf = st->tx,
		.rx_buf = st->rx,
		.bits_per_word = 8,
		.len = 2,
		.cs_change = 1,
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = LIS3L02DQ_READ_REG(reg_address);
	st->tx[1] = 0;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->us, &msg);
	*val = st->rx[1];
	mutex_unlock(&st->buf_lock);

	return ret;
}

/**
 * lis3l02dq_spi_write_reg_8() - write single byte to a register
 * @dev: device associated with child of actual device (iio_dev or iio_trig)
 * @reg_address: the address of the register to be written
 * @val: the value to write
 **/
int lis3l02dq_spi_write_reg_8(struct device *dev,
			      u8 reg_address,
			      u8 *val)
{
	int ret;
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct iio_sw_ring_helper_state *h
		= iio_dev_get_devdata(indio_dev);
	struct lis3l02dq_state *st = lis3l02dq_h_to_s(h);
	struct spi_transfer xfer = {
		.tx_buf = st->tx,
		.bits_per_word = 8,
		.len = 2,
		.cs_change = 1,
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = LIS3L02DQ_WRITE_REG(reg_address);
	st->tx[1] = *val;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(st->us, &msg);
	mutex_unlock(&st->buf_lock);

	return ret;
}

/**
 * lisl302dq_spi_write_reg_s16() - write 2 bytes to a pair of registers
 * @dev: device associated with child of actual device (iio_dev or iio_trig)
 * @reg_address: the address of the lower of the two registers. Second register
 *               is assumed to have address one greater.
 * @val: value to be written
 **/
static int lis3l02dq_spi_write_reg_s16(struct device *dev,
				       u8 lower_reg_address,
				       s16 value)
{
	int ret;
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct iio_sw_ring_helper_state *h
		= iio_dev_get_devdata(indio_dev);
	struct lis3l02dq_state *st = lis3l02dq_h_to_s(h);
	struct spi_transfer xfers[] = { {
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
	st->tx[0] = LIS3L02DQ_WRITE_REG(lower_reg_address);
	st->tx[1] = value & 0xFF;
	st->tx[2] = LIS3L02DQ_WRITE_REG(lower_reg_address + 1);
	st->tx[3] = (value >> 8) & 0xFF;

	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);
	ret = spi_sync(st->us, &msg);
	mutex_unlock(&st->buf_lock);

	return ret;
}

/**
 * lisl302dq_spi_read_reg_s16() - write 2 bytes to a pair of registers
 * @dev: device associated with child of actual device (iio_dev or iio_trig)
 * @reg_address: the address of the lower of the two registers. Second register
 *               is assumed to have address one greater.
 * @val: somewhere to pass back the value read
 **/
static int lis3l02dq_spi_read_reg_s16(struct device *dev,
				      u8 lower_reg_address,
				      s16 *val)
{
	struct spi_message msg;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct iio_sw_ring_helper_state *h
		= iio_dev_get_devdata(indio_dev);
	struct lis3l02dq_state *st = lis3l02dq_h_to_s(h);
	int ret;
	struct spi_transfer xfers[] = { {
			.tx_buf = st->tx,
			.rx_buf = st->rx,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
		}, {
			.tx_buf = st->tx + 2,
			.rx_buf = st->rx + 2,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,

		},
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = LIS3L02DQ_READ_REG(lower_reg_address);
	st->tx[1] = 0;
	st->tx[2] = LIS3L02DQ_READ_REG(lower_reg_address+1);
	st->tx[3] = 0;

	spi_message_init(&msg);
	spi_message_add_tail(&xfers[0], &msg);
	spi_message_add_tail(&xfers[1], &msg);
	ret = spi_sync(st->us, &msg);
	if (ret) {
		dev_err(&st->us->dev, "problem when reading 16 bit register");
		goto error_ret;
	}
	*val = (s16)(st->rx[1]) | ((s16)(st->rx[3]) << 8);

error_ret:
	mutex_unlock(&st->buf_lock);
	return ret;
}

/**
 * lis3l02dq_read_signed() - attribute function used for 8 bit signed values
 * @dev: the child device associated with the iio_dev or iio_trigger
 * @attr: the attribute being processed
 * @buf: buffer into which put the output string
 **/
static ssize_t lis3l02dq_read_signed(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	int ret;
	s8 val;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = lis3l02dq_spi_read_reg_8(dev, this_attr->address, (u8 *)&val);

	return ret ? ret : sprintf(buf, "%d\n", val);
}

static ssize_t lis3l02dq_read_unsigned(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	int ret;
	u8 val;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = lis3l02dq_spi_read_reg_8(dev, this_attr->address, &val);

	return ret ? ret : sprintf(buf, "%d\n", val);
}

static ssize_t lis3l02dq_write_signed(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t len)
{
	long valin;
	s8 val;
	int ret;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = strict_strtol(buf, 10, &valin);
	if (ret)
		goto error_ret;
	val = valin;
	ret = lis3l02dq_spi_write_reg_8(dev, this_attr->address, (u8 *)&val);

error_ret:
	return ret ? ret : len;
}

static ssize_t lis3l02dq_write_unsigned(struct device *dev,
					struct device_attribute *attr,
					const char *buf,
					size_t len)
{
	int ret;
	ulong valin;
	u8 val;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = strict_strtoul(buf, 10, &valin);
	if (ret)
		goto err_ret;
	val = valin;
	ret = lis3l02dq_spi_write_reg_8(dev, this_attr->address, &val);

err_ret:
	return ret ? ret : len;
}

static ssize_t lis3l02dq_read_16bit_signed(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	int ret;
	s16 val = 0;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);

	ret = lis3l02dq_spi_read_reg_s16(dev, this_attr->address, &val);

	if (ret)
		return ret;

	return sprintf(buf, "%d\n", val);
}

static ssize_t lis3l02dq_read_accel(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	ssize_t ret;

	/* Take the iio_dev status lock */
	mutex_lock(&indio_dev->mlock);
	if (indio_dev->currentmode == INDIO_RING_TRIGGERED)
		ret = lis3l02dq_read_accel_from_ring(dev, attr, buf);
	else
		ret =  lis3l02dq_read_16bit_signed(dev, attr, buf);
	mutex_unlock(&indio_dev->mlock);

	return ret;
}

static ssize_t lis3l02dq_write_16bit_signed(struct device *dev,
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
	ret = lis3l02dq_spi_write_reg_s16(dev, this_attr->address, val);

error_ret:
	return ret ? ret : len;
}

static ssize_t lis3l02dq_read_frequency(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int ret, len = 0;
	s8 t;
	ret = lis3l02dq_spi_read_reg_8(dev,
				       LIS3L02DQ_REG_CTRL_1_ADDR,
				       (u8 *)&t);
	if (ret)
		return ret;
	t &= LIS3L02DQ_DEC_MASK;
	switch (t) {
	case LIS3L02DQ_REG_CTRL_1_DF_128:
		len = sprintf(buf, "280\n");
		break;
	case LIS3L02DQ_REG_CTRL_1_DF_64:
		len = sprintf(buf, "560\n");
		break;
	case LIS3L02DQ_REG_CTRL_1_DF_32:
		len = sprintf(buf, "1120\n");
		break;
	case LIS3L02DQ_REG_CTRL_1_DF_8:
		len = sprintf(buf, "4480\n");
		break;
	}
	return len;
}

static ssize_t lis3l02dq_write_frequency(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf,
					 size_t len)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	long val;
	int ret;
	u8 t;

	ret = strict_strtol(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&indio_dev->mlock);
	ret = lis3l02dq_spi_read_reg_8(dev,
				       LIS3L02DQ_REG_CTRL_1_ADDR,
				       &t);
	if (ret)
		goto error_ret_mutex;
	/* Wipe the bits clean */
	t &= ~LIS3L02DQ_DEC_MASK;
	switch (val) {
	case 280:
		t |= LIS3L02DQ_REG_CTRL_1_DF_128;
		break;
	case 560:
		t |= LIS3L02DQ_REG_CTRL_1_DF_64;
		break;
	case 1120:
		t |= LIS3L02DQ_REG_CTRL_1_DF_32;
		break;
	case 4480:
		t |= LIS3L02DQ_REG_CTRL_1_DF_8;
		break;
	default:
		ret = -EINVAL;
		goto error_ret_mutex;
	};

	ret = lis3l02dq_spi_write_reg_8(dev,
					LIS3L02DQ_REG_CTRL_1_ADDR,
					&t);

error_ret_mutex:
	mutex_unlock(&indio_dev->mlock);

	return ret ? ret : len;
}

static int lis3l02dq_initial_setup(struct lis3l02dq_state *st)
{
	int ret;
	u8 val, valtest;

	st->us->mode = SPI_MODE_3;

	spi_setup(st->us);

	val = LIS3L02DQ_DEFAULT_CTRL1;
	/* Write suitable defaults to ctrl1 */
	ret = lis3l02dq_spi_write_reg_8(&st->help.indio_dev->dev,
					LIS3L02DQ_REG_CTRL_1_ADDR,
					&val);
	if (ret) {
		dev_err(&st->us->dev, "problem with setup control register 1");
		goto err_ret;
	}
	/* Repeat as sometimes doesn't work first time?*/
	ret = lis3l02dq_spi_write_reg_8(&st->help.indio_dev->dev,
					LIS3L02DQ_REG_CTRL_1_ADDR,
					&val);
	if (ret) {
		dev_err(&st->us->dev, "problem with setup control register 1");
		goto err_ret;
	}

	/* Read back to check this has worked acts as loose test of correct
	 * chip */
	ret = lis3l02dq_spi_read_reg_8(&st->help.indio_dev->dev,
				       LIS3L02DQ_REG_CTRL_1_ADDR,
				       &valtest);
	if (ret || (valtest != val)) {
		dev_err(&st->help.indio_dev->dev, "device not playing ball");
		ret = -EINVAL;
		goto err_ret;
	}

	val = LIS3L02DQ_DEFAULT_CTRL2;
	ret = lis3l02dq_spi_write_reg_8(&st->help.indio_dev->dev,
					LIS3L02DQ_REG_CTRL_2_ADDR,
					&val);
	if (ret) {
		dev_err(&st->us->dev, "problem with setup control register 2");
		goto err_ret;
	}

	val = LIS3L02DQ_REG_WAKE_UP_CFG_LATCH_SRC;
	ret = lis3l02dq_spi_write_reg_8(&st->help.indio_dev->dev,
					LIS3L02DQ_REG_WAKE_UP_CFG_ADDR,
					&val);
	if (ret)
		dev_err(&st->us->dev, "problem with interrupt cfg register");
err_ret:

	return ret;
}

#define LIS3L02DQ_SIGNED_ATTR(name, reg)	\
	IIO_DEVICE_ATTR(name,			\
			S_IWUSR | S_IRUGO,	\
			lis3l02dq_read_signed,	\
			lis3l02dq_write_signed, \
			reg);

#define LIS3L02DQ_UNSIGNED_ATTR(name, reg)		\
	IIO_DEVICE_ATTR(name,				\
			S_IWUSR | S_IRUGO,		\
			lis3l02dq_read_unsigned,	\
			lis3l02dq_write_unsigned,	\
			reg);

static LIS3L02DQ_SIGNED_ATTR(accel_x_calibbias,
			     LIS3L02DQ_REG_OFFSET_X_ADDR);
static LIS3L02DQ_SIGNED_ATTR(accel_y_calibbias,
			     LIS3L02DQ_REG_OFFSET_Y_ADDR);
static LIS3L02DQ_SIGNED_ATTR(accel_z_calibbias,
			     LIS3L02DQ_REG_OFFSET_Z_ADDR);

static LIS3L02DQ_UNSIGNED_ATTR(accel_x_calibscale,
			       LIS3L02DQ_REG_GAIN_X_ADDR);
static LIS3L02DQ_UNSIGNED_ATTR(accel_y_calibscale,
			       LIS3L02DQ_REG_GAIN_Y_ADDR);
static LIS3L02DQ_UNSIGNED_ATTR(accel_z_calibscale,
			       LIS3L02DQ_REG_GAIN_Z_ADDR);

static IIO_DEVICE_ATTR(accel_raw_mag_value,
		       S_IWUSR | S_IRUGO,
		       lis3l02dq_read_16bit_signed,
		       lis3l02dq_write_16bit_signed,
		       LIS3L02DQ_REG_THS_L_ADDR);
/* RFC The reading method for these will change depending on whether
 * ring buffer capture is in use. Is it worth making these take two
 * functions and let the core handle which to call, or leave as in this
 * driver where it is the drivers problem to manage this?
 */

static IIO_DEV_ATTR_ACCEL_X(lis3l02dq_read_accel,
			    LIS3L02DQ_REG_OUT_X_L_ADDR);

static IIO_DEV_ATTR_ACCEL_Y(lis3l02dq_read_accel,
			    LIS3L02DQ_REG_OUT_Y_L_ADDR);

static IIO_DEV_ATTR_ACCEL_Z(lis3l02dq_read_accel,
			    LIS3L02DQ_REG_OUT_Z_L_ADDR);

static IIO_DEV_ATTR_SAMP_FREQ(S_IWUSR | S_IRUGO,
			      lis3l02dq_read_frequency,
			      lis3l02dq_write_frequency);

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("280 560 1120 4480");

static ssize_t lis3l02dq_read_interrupt_config(struct device *dev,
					       struct device_attribute *attr,
					       char *buf)
{
	int ret;
	s8 val;
	struct iio_event_attr *this_attr = to_iio_event_attr(attr);

	ret = lis3l02dq_spi_read_reg_8(dev->parent,
				       LIS3L02DQ_REG_WAKE_UP_CFG_ADDR,
				       (u8 *)&val);

	return ret ? ret : sprintf(buf, "%d\n", !!(val & this_attr->mask));
}

static ssize_t lis3l02dq_write_interrupt_config(struct device *dev,
						struct device_attribute *attr,
						const char *buf,
						size_t len)
{
	struct iio_event_attr *this_attr = to_iio_event_attr(attr);
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	int ret, currentlyset, changed = 0;
	u8 valold, controlold;
	bool val;

	val = !(buf[0] == '0');

	mutex_lock(&indio_dev->mlock);
	/* read current value */
	ret = lis3l02dq_spi_read_reg_8(dev->parent,
				       LIS3L02DQ_REG_WAKE_UP_CFG_ADDR,
				       &valold);
	if (ret)
		goto error_mutex_unlock;

	/* read current control */
	ret = lis3l02dq_spi_read_reg_8(dev,
				       LIS3L02DQ_REG_CTRL_2_ADDR,
				       &controlold);
	if (ret)
		goto error_mutex_unlock;
	currentlyset = !!(valold & this_attr->mask);
	if (val == false && currentlyset) {
		valold &= ~this_attr->mask;
		changed = 1;
		iio_remove_event_from_list(this_attr->listel,
						 &indio_dev->interrupts[0]
						 ->ev_list);
	} else if (val == true && !currentlyset) {
		changed = 1;
		valold |= this_attr->mask;
		iio_add_event_to_list(this_attr->listel,
					    &indio_dev->interrupts[0]->ev_list);
	}

	if (changed) {
		ret = lis3l02dq_spi_write_reg_8(dev,
						LIS3L02DQ_REG_WAKE_UP_CFG_ADDR,
						&valold);
		if (ret)
			goto error_mutex_unlock;
		/* This always enables the interrupt, even if we've remove the
		 * last thing using it. For this device we can use the reference
		 * count on the handler to tell us if anyone wants the interrupt
		 */
		controlold = this_attr->listel->refcount ?
			(controlold | LIS3L02DQ_REG_CTRL_2_ENABLE_INTERRUPT) :
			(controlold & ~LIS3L02DQ_REG_CTRL_2_ENABLE_INTERRUPT);
		ret = lis3l02dq_spi_write_reg_8(dev,
						LIS3L02DQ_REG_CTRL_2_ADDR,
						&controlold);
		if (ret)
			goto error_mutex_unlock;
	}
error_mutex_unlock:
	mutex_unlock(&indio_dev->mlock);

	return ret ? ret : len;
}


static int lis3l02dq_thresh_handler_th(struct iio_dev *indio_dev,
				       int index,
				       s64 timestamp,
				       int no_test)
{
	struct iio_sw_ring_helper_state *h
		= iio_dev_get_devdata(indio_dev);
	struct lis3l02dq_state *st = lis3l02dq_h_to_s(h);

	/* Stash the timestamp somewhere convenient for the bh */
	st->thresh_timestamp = timestamp;
	schedule_work(&st->work_thresh);

	return 0;
}


/* Unforunately it appears the interrupt won't clear unless you read from the
 * src register.
 */
static void lis3l02dq_thresh_handler_bh_no_check(struct work_struct *work_s)
{
       struct lis3l02dq_state *st
	       = container_of(work_s,
		       struct lis3l02dq_state, work_thresh);

	u8 t;

	lis3l02dq_spi_read_reg_8(&st->help.indio_dev->dev,
				 LIS3L02DQ_REG_WAKE_UP_SRC_ADDR,
				 &t);

	if (t & LIS3L02DQ_REG_WAKE_UP_SRC_INTERRUPT_Z_HIGH)
		iio_push_event(st->help.indio_dev, 0,
			       IIO_MOD_EVENT_CODE(IIO_EV_CLASS_ACCEL,
						  0,
						  IIO_EV_MOD_Z,
						  IIO_EV_TYPE_THRESH,
						  IIO_EV_DIR_RISING),
			       st->thresh_timestamp);

	if (t & LIS3L02DQ_REG_WAKE_UP_SRC_INTERRUPT_Z_LOW)
		iio_push_event(st->help.indio_dev, 0,
			       IIO_MOD_EVENT_CODE(IIO_EV_CLASS_ACCEL,
						  0,
						  IIO_EV_MOD_Z,
						  IIO_EV_TYPE_THRESH,
						  IIO_EV_DIR_FALLING),
			       st->thresh_timestamp);

	if (t & LIS3L02DQ_REG_WAKE_UP_SRC_INTERRUPT_Y_HIGH)
		iio_push_event(st->help.indio_dev, 0,
			       IIO_MOD_EVENT_CODE(IIO_EV_CLASS_ACCEL,
						  0,
						  IIO_EV_MOD_Y,
						  IIO_EV_TYPE_THRESH,
						  IIO_EV_DIR_RISING),
			       st->thresh_timestamp);

	if (t & LIS3L02DQ_REG_WAKE_UP_SRC_INTERRUPT_Y_LOW)
		iio_push_event(st->help.indio_dev, 0,
			       IIO_MOD_EVENT_CODE(IIO_EV_CLASS_ACCEL,
						  0,
						  IIO_EV_MOD_Y,
						  IIO_EV_TYPE_THRESH,
						  IIO_EV_DIR_FALLING),
			       st->thresh_timestamp);

	if (t & LIS3L02DQ_REG_WAKE_UP_SRC_INTERRUPT_X_HIGH)
		iio_push_event(st->help.indio_dev, 0,
			       IIO_MOD_EVENT_CODE(IIO_EV_CLASS_ACCEL,
						  0,
						  IIO_EV_MOD_X,
						  IIO_EV_TYPE_THRESH,
						  IIO_EV_DIR_RISING),
			       st->thresh_timestamp);

	if (t & LIS3L02DQ_REG_WAKE_UP_SRC_INTERRUPT_X_LOW)
		iio_push_event(st->help.indio_dev, 0,
			       IIO_MOD_EVENT_CODE(IIO_EV_CLASS_ACCEL,
						  0,
						  IIO_EV_MOD_X,
						  IIO_EV_TYPE_THRESH,
						  IIO_EV_DIR_FALLING),
			       st->thresh_timestamp);
	/* reenable the irq */
	enable_irq(st->us->irq);
	/* Ack and allow for new interrupts */
	lis3l02dq_spi_read_reg_8(&st->help.indio_dev->dev,
				 LIS3L02DQ_REG_WAKE_UP_ACK_ADDR,
				 &t);

	return;
}

/* A shared handler for a number of threshold types */
IIO_EVENT_SH(threshold, &lis3l02dq_thresh_handler_th);

IIO_EVENT_ATTR_SH(accel_x_thresh_rising_en,
		  iio_event_threshold,
		  lis3l02dq_read_interrupt_config,
		  lis3l02dq_write_interrupt_config,
		  LIS3L02DQ_REG_WAKE_UP_CFG_INTERRUPT_X_HIGH);

IIO_EVENT_ATTR_SH(accel_y_thresh_rising_en,
		  iio_event_threshold,
		  lis3l02dq_read_interrupt_config,
		  lis3l02dq_write_interrupt_config,
		  LIS3L02DQ_REG_WAKE_UP_CFG_INTERRUPT_Y_HIGH);

IIO_EVENT_ATTR_SH(accel_z_thresh_rising_en,
		  iio_event_threshold,
		  lis3l02dq_read_interrupt_config,
		  lis3l02dq_write_interrupt_config,
		  LIS3L02DQ_REG_WAKE_UP_CFG_INTERRUPT_Z_HIGH);

IIO_EVENT_ATTR_SH(accel_x_thresh_falling_en,
		  iio_event_threshold,
		  lis3l02dq_read_interrupt_config,
		  lis3l02dq_write_interrupt_config,
		  LIS3L02DQ_REG_WAKE_UP_CFG_INTERRUPT_X_LOW);

IIO_EVENT_ATTR_SH(accel_y_thresh_falling_en,
		  iio_event_threshold,
		  lis3l02dq_read_interrupt_config,
		  lis3l02dq_write_interrupt_config,
		  LIS3L02DQ_REG_WAKE_UP_CFG_INTERRUPT_Y_LOW);

IIO_EVENT_ATTR_SH(accel_z_thresh_falling_en,
		  iio_event_threshold,
		  lis3l02dq_read_interrupt_config,
		  lis3l02dq_write_interrupt_config,
		  LIS3L02DQ_REG_WAKE_UP_CFG_INTERRUPT_Z_LOW);


static struct attribute *lis3l02dq_event_attributes[] = {
	&iio_event_attr_accel_x_thresh_rising_en.dev_attr.attr,
	&iio_event_attr_accel_y_thresh_rising_en.dev_attr.attr,
	&iio_event_attr_accel_z_thresh_rising_en.dev_attr.attr,
	&iio_event_attr_accel_x_thresh_falling_en.dev_attr.attr,
	&iio_event_attr_accel_y_thresh_falling_en.dev_attr.attr,
	&iio_event_attr_accel_z_thresh_falling_en.dev_attr.attr,
	&iio_dev_attr_accel_raw_mag_value.dev_attr.attr,
	NULL
};

static struct attribute_group lis3l02dq_event_attribute_group = {
	.attrs = lis3l02dq_event_attributes,
};

static IIO_CONST_ATTR_NAME("lis3l02dq");
static IIO_CONST_ATTR(accel_scale, "0.00958");

static struct attribute *lis3l02dq_attributes[] = {
	&iio_dev_attr_accel_x_calibbias.dev_attr.attr,
	&iio_dev_attr_accel_y_calibbias.dev_attr.attr,
	&iio_dev_attr_accel_z_calibbias.dev_attr.attr,
	&iio_dev_attr_accel_x_calibscale.dev_attr.attr,
	&iio_dev_attr_accel_y_calibscale.dev_attr.attr,
	&iio_dev_attr_accel_z_calibscale.dev_attr.attr,
	&iio_const_attr_accel_scale.dev_attr.attr,
	&iio_dev_attr_accel_x_raw.dev_attr.attr,
	&iio_dev_attr_accel_y_raw.dev_attr.attr,
	&iio_dev_attr_accel_z_raw.dev_attr.attr,
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	&iio_const_attr_name.dev_attr.attr,
	NULL
};

static const struct attribute_group lis3l02dq_attribute_group = {
	.attrs = lis3l02dq_attributes,
};

static int __devinit lis3l02dq_probe(struct spi_device *spi)
{
	int ret, regdone = 0;
	struct lis3l02dq_state *st = kzalloc(sizeof *st, GFP_KERNEL);
	if (!st) {
		ret =  -ENOMEM;
		goto error_ret;
	}
	INIT_WORK(&st->work_thresh, lis3l02dq_thresh_handler_bh_no_check);
	/* this is only used tor removal purposes */
	spi_set_drvdata(spi, st);

	/* Allocate the comms buffers */
	st->rx = kzalloc(sizeof(*st->rx)*LIS3L02DQ_MAX_RX, GFP_KERNEL);
	if (st->rx == NULL) {
		ret = -ENOMEM;
		goto error_free_st;
	}
	st->tx = kzalloc(sizeof(*st->tx)*LIS3L02DQ_MAX_TX, GFP_KERNEL);
	if (st->tx == NULL) {
		ret = -ENOMEM;
		goto error_free_rx;
	}
	st->us = spi;
	mutex_init(&st->buf_lock);
	/* setup the industrialio driver allocated elements */
	st->help.indio_dev = iio_allocate_device();
	if (st->help.indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_free_tx;
	}

	st->help.indio_dev->dev.parent = &spi->dev;
	st->help.indio_dev->num_interrupt_lines = 1;
	st->help.indio_dev->event_attrs = &lis3l02dq_event_attribute_group;
	st->help.indio_dev->attrs = &lis3l02dq_attribute_group;
	st->help.indio_dev->dev_data = (void *)(&st->help);
	st->help.indio_dev->driver_module = THIS_MODULE;
	st->help.indio_dev->modes = INDIO_DIRECT_MODE;

	ret = lis3l02dq_configure_ring(st->help.indio_dev);
	if (ret)
		goto error_free_dev;

	ret = iio_device_register(st->help.indio_dev);
	if (ret)
		goto error_unreg_ring_funcs;
	regdone = 1;

	ret = iio_ring_buffer_register(st->help.indio_dev->ring, 0);
	if (ret) {
		printk(KERN_ERR "failed to initialize the ring\n");
		goto error_unreg_ring_funcs;
	}

	if (spi->irq && gpio_is_valid(irq_to_gpio(spi->irq)) > 0) {
		st->inter = 0;
		ret = iio_register_interrupt_line(spi->irq,
						  st->help.indio_dev,
						  0,
						  IRQF_TRIGGER_RISING,
						  "lis3l02dq");
		if (ret)
			goto error_uninitialize_ring;

		ret = lis3l02dq_probe_trigger(st->help.indio_dev);
		if (ret)
			goto error_unregister_line;
	}

	/* Get the device into a sane initial state */
	ret = lis3l02dq_initial_setup(st);
	if (ret)
		goto error_remove_trigger;
	return 0;

error_remove_trigger:
	if (st->help.indio_dev->modes & INDIO_RING_TRIGGERED)
		lis3l02dq_remove_trigger(st->help.indio_dev);
error_unregister_line:
	if (st->help.indio_dev->modes & INDIO_RING_TRIGGERED)
		iio_unregister_interrupt_line(st->help.indio_dev, 0);
error_uninitialize_ring:
	iio_ring_buffer_unregister(st->help.indio_dev->ring);
error_unreg_ring_funcs:
	lis3l02dq_unconfigure_ring(st->help.indio_dev);
error_free_dev:
	if (regdone)
		iio_device_unregister(st->help.indio_dev);
	else
		iio_free_device(st->help.indio_dev);
error_free_tx:
	kfree(st->tx);
error_free_rx:
	kfree(st->rx);
error_free_st:
	kfree(st);
error_ret:
	return ret;
}

/* Power down the device */
static int lis3l02dq_stop_device(struct iio_dev *indio_dev)
{
	int ret;
	struct iio_sw_ring_helper_state *h
		= iio_dev_get_devdata(indio_dev);
	struct lis3l02dq_state *st = lis3l02dq_h_to_s(h);
	u8 val = 0;

	mutex_lock(&indio_dev->mlock);
	ret = lis3l02dq_spi_write_reg_8(&indio_dev->dev,
					LIS3L02DQ_REG_CTRL_1_ADDR,
					&val);
	if (ret) {
		dev_err(&st->us->dev, "problem with turning device off: ctrl1");
		goto err_ret;
	}

	ret = lis3l02dq_spi_write_reg_8(&indio_dev->dev,
					LIS3L02DQ_REG_CTRL_2_ADDR,
					&val);
	if (ret)
		dev_err(&st->us->dev, "problem with turning device off: ctrl2");
err_ret:
	mutex_unlock(&indio_dev->mlock);
	return ret;
}

/* fixme, confirm ordering in this function */
static int lis3l02dq_remove(struct spi_device *spi)
{
	int ret;
	struct lis3l02dq_state *st = spi_get_drvdata(spi);
	struct iio_dev *indio_dev = st->help.indio_dev;

	ret = lis3l02dq_stop_device(indio_dev);
	if (ret)
		goto err_ret;

	flush_scheduled_work();

	lis3l02dq_remove_trigger(indio_dev);
	if (spi->irq && gpio_is_valid(irq_to_gpio(spi->irq)) > 0)
		iio_unregister_interrupt_line(indio_dev, 0);

	iio_ring_buffer_unregister(indio_dev->ring);
	lis3l02dq_unconfigure_ring(indio_dev);
	iio_device_unregister(indio_dev);
	kfree(st->tx);
	kfree(st->rx);
	kfree(st);

	return 0;

err_ret:
	return ret;
}

static struct spi_driver lis3l02dq_driver = {
	.driver = {
		.name = "lis3l02dq",
		.owner = THIS_MODULE,
	},
	.probe = lis3l02dq_probe,
	.remove = __devexit_p(lis3l02dq_remove),
};

static __init int lis3l02dq_init(void)
{
	return spi_register_driver(&lis3l02dq_driver);
}
module_init(lis3l02dq_init);

static __exit void lis3l02dq_exit(void)
{
	spi_unregister_driver(&lis3l02dq_driver);
}
module_exit(lis3l02dq_exit);

MODULE_AUTHOR("Jonathan Cameron <jic23@cam.ac.uk>");
MODULE_DESCRIPTION("ST LIS3L02DQ Accelerometer SPI driver");
MODULE_LICENSE("GPL v2");
