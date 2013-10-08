/*
 * lis3l02dq.c	support STMicroelectronics LISD02DQ
 *		3d 2g Linear Accelerometers via SPI
 *
 * Copyright (c) 2007 Jonathan Cameron <jic23@kernel.org>
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
#include <linux/of_gpio.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/module.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/events.h>
#include <linux/iio/buffer.h>

#include "lis3l02dq.h"

/* At the moment the spi framework doesn't allow global setting of cs_change.
 * It's in the likely to be added comment at the top of spi.h.
 * This means that use cannot be made of spi_write etc.
 */
/* direct copy of the irq_default_primary_handler */
#ifndef CONFIG_IIO_BUFFER
static irqreturn_t lis3l02dq_nobuffer(int irq, void *private)
{
	return IRQ_WAKE_THREAD;
}
#endif

/**
 * lis3l02dq_spi_read_reg_8() - read single byte from a single register
 * @indio_dev: iio_dev for this actual device
 * @reg_address: the address of the register to be read
 * @val: pass back the resulting value
 **/
int lis3l02dq_spi_read_reg_8(struct iio_dev *indio_dev,
			     u8 reg_address, u8 *val)
{
	struct lis3l02dq_state *st = iio_priv(indio_dev);
	int ret;
	struct spi_transfer xfer = {
		.tx_buf = st->tx,
		.rx_buf = st->rx,
		.bits_per_word = 8,
		.len = 2,
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = LIS3L02DQ_READ_REG(reg_address);
	st->tx[1] = 0;

	ret = spi_sync_transfer(st->us, &xfer, 1);
	*val = st->rx[1];
	mutex_unlock(&st->buf_lock);

	return ret;
}

/**
 * lis3l02dq_spi_write_reg_8() - write single byte to a register
 * @indio_dev: iio_dev for this device
 * @reg_address: the address of the register to be written
 * @val: the value to write
 **/
int lis3l02dq_spi_write_reg_8(struct iio_dev *indio_dev,
			      u8 reg_address,
			      u8 val)
{
	int ret;
	struct lis3l02dq_state *st = iio_priv(indio_dev);

	mutex_lock(&st->buf_lock);
	st->tx[0] = LIS3L02DQ_WRITE_REG(reg_address);
	st->tx[1] = val;
	ret = spi_write(st->us, st->tx, 2);
	mutex_unlock(&st->buf_lock);

	return ret;
}

/**
 * lisl302dq_spi_write_reg_s16() - write 2 bytes to a pair of registers
 * @indio_dev: iio_dev for this device
 * @lower_reg_address: the address of the lower of the two registers.
 *               Second register is assumed to have address one greater.
 * @value: value to be written
 **/
static int lis3l02dq_spi_write_reg_s16(struct iio_dev *indio_dev,
				       u8 lower_reg_address,
				       s16 value)
{
	int ret;
	struct lis3l02dq_state *st = iio_priv(indio_dev);
	struct spi_transfer xfers[] = { {
			.tx_buf = st->tx,
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
		}, {
			.tx_buf = st->tx + 2,
			.bits_per_word = 8,
			.len = 2,
		},
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = LIS3L02DQ_WRITE_REG(lower_reg_address);
	st->tx[1] = value & 0xFF;
	st->tx[2] = LIS3L02DQ_WRITE_REG(lower_reg_address + 1);
	st->tx[3] = (value >> 8) & 0xFF;

	ret = spi_sync_transfer(st->us, xfers, ARRAY_SIZE(xfers));
	mutex_unlock(&st->buf_lock);

	return ret;
}

static int lis3l02dq_read_reg_s16(struct iio_dev *indio_dev,
				  u8 lower_reg_address,
				  int *val)
{
	struct lis3l02dq_state *st = iio_priv(indio_dev);
	int ret;
	s16 tempval;
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
		},
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = LIS3L02DQ_READ_REG(lower_reg_address);
	st->tx[1] = 0;
	st->tx[2] = LIS3L02DQ_READ_REG(lower_reg_address + 1);
	st->tx[3] = 0;

	ret = spi_sync_transfer(st->us, xfers, ARRAY_SIZE(xfers));
	if (ret) {
		dev_err(&st->us->dev, "problem when reading 16 bit register");
		goto error_ret;
	}
	tempval = (s16)(st->rx[1]) | ((s16)(st->rx[3]) << 8);

	*val = tempval;
error_ret:
	mutex_unlock(&st->buf_lock);
	return ret;
}

enum lis3l02dq_rm_ind {
	LIS3L02DQ_ACCEL,
	LIS3L02DQ_GAIN,
	LIS3L02DQ_BIAS,
};

static u8 lis3l02dq_axis_map[3][3] = {
	[LIS3L02DQ_ACCEL] = { LIS3L02DQ_REG_OUT_X_L_ADDR,
			      LIS3L02DQ_REG_OUT_Y_L_ADDR,
			      LIS3L02DQ_REG_OUT_Z_L_ADDR },
	[LIS3L02DQ_GAIN] = { LIS3L02DQ_REG_GAIN_X_ADDR,
			     LIS3L02DQ_REG_GAIN_Y_ADDR,
			     LIS3L02DQ_REG_GAIN_Z_ADDR },
	[LIS3L02DQ_BIAS] = { LIS3L02DQ_REG_OFFSET_X_ADDR,
			     LIS3L02DQ_REG_OFFSET_Y_ADDR,
			     LIS3L02DQ_REG_OFFSET_Z_ADDR }
};

static int lis3l02dq_read_thresh(struct iio_dev *indio_dev,
				 u64 e,
				 int *val)
{
	return lis3l02dq_read_reg_s16(indio_dev, LIS3L02DQ_REG_THS_L_ADDR, val);
}

static int lis3l02dq_write_thresh(struct iio_dev *indio_dev,
				  u64 event_code,
				  int val)
{
	u16 value = val;
	return lis3l02dq_spi_write_reg_s16(indio_dev,
					   LIS3L02DQ_REG_THS_L_ADDR,
					   value);
}

static int lis3l02dq_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val,
			       int val2,
			       long mask)
{
	int ret = -EINVAL, reg;
	u8 uval;
	s8 sval;
	switch (mask) {
	case IIO_CHAN_INFO_CALIBBIAS:
		if (val > 255 || val < -256)
			return -EINVAL;
		sval = val;
		reg = lis3l02dq_axis_map[LIS3L02DQ_BIAS][chan->address];
		ret = lis3l02dq_spi_write_reg_8(indio_dev, reg, sval);
		break;
	case IIO_CHAN_INFO_CALIBSCALE:
		if (val & ~0xFF)
			return -EINVAL;
		uval = val;
		reg = lis3l02dq_axis_map[LIS3L02DQ_GAIN][chan->address];
		ret = lis3l02dq_spi_write_reg_8(indio_dev, reg, uval);
		break;
	}
	return ret;
}

static int lis3l02dq_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val,
			      int *val2,
			      long mask)
{
	u8 utemp;
	s8 stemp;
	ssize_t ret = 0;
	u8 reg;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		/* Take the iio_dev status lock */
		mutex_lock(&indio_dev->mlock);
		if (indio_dev->currentmode == INDIO_BUFFER_TRIGGERED) {
			ret = -EBUSY;
		} else {
			reg = lis3l02dq_axis_map
				[LIS3L02DQ_ACCEL][chan->address];
			ret = lis3l02dq_read_reg_s16(indio_dev, reg, val);
		}
		mutex_unlock(&indio_dev->mlock);
		if (ret < 0)
			goto error_ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = 9580;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_CALIBSCALE:
		reg = lis3l02dq_axis_map[LIS3L02DQ_GAIN][chan->address];
		ret = lis3l02dq_spi_read_reg_8(indio_dev, reg, &utemp);
		if (ret)
			goto error_ret;
		/* to match with what previous code does */
		*val = utemp;
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_CALIBBIAS:
		reg = lis3l02dq_axis_map[LIS3L02DQ_BIAS][chan->address];
		ret = lis3l02dq_spi_read_reg_8(indio_dev, reg, (u8 *)&stemp);
		/* to match with what previous code does */
		*val = stemp;
		return IIO_VAL_INT;
	}
error_ret:
	return ret;
}

static ssize_t lis3l02dq_read_frequency(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	int ret, len = 0;
	s8 t;
	ret = lis3l02dq_spi_read_reg_8(indio_dev,
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
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	unsigned long val;
	int ret;
	u8 t;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&indio_dev->mlock);
	ret = lis3l02dq_spi_read_reg_8(indio_dev,
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
	}

	ret = lis3l02dq_spi_write_reg_8(indio_dev,
					LIS3L02DQ_REG_CTRL_1_ADDR,
					t);

error_ret_mutex:
	mutex_unlock(&indio_dev->mlock);

	return ret ? ret : len;
}

static int lis3l02dq_initial_setup(struct iio_dev *indio_dev)
{
	struct lis3l02dq_state *st = iio_priv(indio_dev);
	int ret;
	u8 val, valtest;

	st->us->mode = SPI_MODE_3;

	spi_setup(st->us);

	val = LIS3L02DQ_DEFAULT_CTRL1;
	/* Write suitable defaults to ctrl1 */
	ret = lis3l02dq_spi_write_reg_8(indio_dev,
					LIS3L02DQ_REG_CTRL_1_ADDR,
					val);
	if (ret) {
		dev_err(&st->us->dev, "problem with setup control register 1");
		goto err_ret;
	}
	/* Repeat as sometimes doesn't work first time? */
	ret = lis3l02dq_spi_write_reg_8(indio_dev,
					LIS3L02DQ_REG_CTRL_1_ADDR,
					val);
	if (ret) {
		dev_err(&st->us->dev, "problem with setup control register 1");
		goto err_ret;
	}

	/* Read back to check this has worked acts as loose test of correct
	 * chip */
	ret = lis3l02dq_spi_read_reg_8(indio_dev,
				       LIS3L02DQ_REG_CTRL_1_ADDR,
				       &valtest);
	if (ret || (valtest != val)) {
		dev_err(&indio_dev->dev,
			"device not playing ball %d %d\n", valtest, val);
		ret = -EINVAL;
		goto err_ret;
	}

	val = LIS3L02DQ_DEFAULT_CTRL2;
	ret = lis3l02dq_spi_write_reg_8(indio_dev,
					LIS3L02DQ_REG_CTRL_2_ADDR,
					val);
	if (ret) {
		dev_err(&st->us->dev, "problem with setup control register 2");
		goto err_ret;
	}

	val = LIS3L02DQ_REG_WAKE_UP_CFG_LATCH_SRC;
	ret = lis3l02dq_spi_write_reg_8(indio_dev,
					LIS3L02DQ_REG_WAKE_UP_CFG_ADDR,
					val);
	if (ret)
		dev_err(&st->us->dev, "problem with interrupt cfg register");
err_ret:

	return ret;
}

static IIO_DEV_ATTR_SAMP_FREQ(S_IWUSR | S_IRUGO,
			      lis3l02dq_read_frequency,
			      lis3l02dq_write_frequency);

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("280 560 1120 4480");

static irqreturn_t lis3l02dq_event_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	u8 t;

	s64 timestamp = iio_get_time_ns();

	lis3l02dq_spi_read_reg_8(indio_dev,
				 LIS3L02DQ_REG_WAKE_UP_SRC_ADDR,
				 &t);

	if (t & LIS3L02DQ_REG_WAKE_UP_SRC_INTERRUPT_Z_HIGH)
		iio_push_event(indio_dev,
			       IIO_MOD_EVENT_CODE(IIO_ACCEL,
						  0,
						  IIO_MOD_Z,
						  IIO_EV_TYPE_THRESH,
						  IIO_EV_DIR_RISING),
			       timestamp);

	if (t & LIS3L02DQ_REG_WAKE_UP_SRC_INTERRUPT_Z_LOW)
		iio_push_event(indio_dev,
			       IIO_MOD_EVENT_CODE(IIO_ACCEL,
						  0,
						  IIO_MOD_Z,
						  IIO_EV_TYPE_THRESH,
						  IIO_EV_DIR_FALLING),
			       timestamp);

	if (t & LIS3L02DQ_REG_WAKE_UP_SRC_INTERRUPT_Y_HIGH)
		iio_push_event(indio_dev,
			       IIO_MOD_EVENT_CODE(IIO_ACCEL,
						  0,
						  IIO_MOD_Y,
						  IIO_EV_TYPE_THRESH,
						  IIO_EV_DIR_RISING),
			       timestamp);

	if (t & LIS3L02DQ_REG_WAKE_UP_SRC_INTERRUPT_Y_LOW)
		iio_push_event(indio_dev,
			       IIO_MOD_EVENT_CODE(IIO_ACCEL,
						  0,
						  IIO_MOD_Y,
						  IIO_EV_TYPE_THRESH,
						  IIO_EV_DIR_FALLING),
			       timestamp);

	if (t & LIS3L02DQ_REG_WAKE_UP_SRC_INTERRUPT_X_HIGH)
		iio_push_event(indio_dev,
			       IIO_MOD_EVENT_CODE(IIO_ACCEL,
						  0,
						  IIO_MOD_X,
						  IIO_EV_TYPE_THRESH,
						  IIO_EV_DIR_RISING),
			       timestamp);

	if (t & LIS3L02DQ_REG_WAKE_UP_SRC_INTERRUPT_X_LOW)
		iio_push_event(indio_dev,
			       IIO_MOD_EVENT_CODE(IIO_ACCEL,
						  0,
						  IIO_MOD_X,
						  IIO_EV_TYPE_THRESH,
						  IIO_EV_DIR_FALLING),
			       timestamp);

	/* Ack and allow for new interrupts */
	lis3l02dq_spi_read_reg_8(indio_dev,
				 LIS3L02DQ_REG_WAKE_UP_ACK_ADDR,
				 &t);

	return IRQ_HANDLED;
}

#define LIS3L02DQ_EVENT_MASK					\
	(IIO_EV_BIT(IIO_EV_TYPE_THRESH, IIO_EV_DIR_RISING) |	\
	 IIO_EV_BIT(IIO_EV_TYPE_THRESH, IIO_EV_DIR_FALLING))

#define LIS3L02DQ_CHAN(index, mod)				\
	{							\
		.type = IIO_ACCEL,				\
		.modified = 1,					\
		.channel2 = mod,				\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |	\
			BIT(IIO_CHAN_INFO_CALIBSCALE) |		\
			BIT(IIO_CHAN_INFO_CALIBBIAS),		\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
		.address = index,				\
		.scan_index = index,				\
		.scan_type = {					\
			.sign = 's',				\
			.realbits = 12,				\
			.storagebits = 16,			\
		},						\
		.event_mask = LIS3L02DQ_EVENT_MASK,		\
	 }

static const struct iio_chan_spec lis3l02dq_channels[] = {
	LIS3L02DQ_CHAN(0, IIO_MOD_X),
	LIS3L02DQ_CHAN(1, IIO_MOD_Y),
	LIS3L02DQ_CHAN(2, IIO_MOD_Z),
	IIO_CHAN_SOFT_TIMESTAMP(3)
};


static int lis3l02dq_read_event_config(struct iio_dev *indio_dev,
					   u64 event_code)
{

	u8 val;
	int ret;
	u8 mask = (1 << (IIO_EVENT_CODE_EXTRACT_MODIFIER(event_code)*2 +
			 (IIO_EVENT_CODE_EXTRACT_DIR(event_code) ==
			  IIO_EV_DIR_RISING)));
	ret = lis3l02dq_spi_read_reg_8(indio_dev,
				       LIS3L02DQ_REG_WAKE_UP_CFG_ADDR,
				       &val);
	if (ret < 0)
		return ret;

	return !!(val & mask);
}

int lis3l02dq_disable_all_events(struct iio_dev *indio_dev)
{
	int ret;
	u8 control, val;

	ret = lis3l02dq_spi_read_reg_8(indio_dev,
				       LIS3L02DQ_REG_CTRL_2_ADDR,
				       &control);

	control &= ~LIS3L02DQ_REG_CTRL_2_ENABLE_INTERRUPT;
	ret = lis3l02dq_spi_write_reg_8(indio_dev,
					LIS3L02DQ_REG_CTRL_2_ADDR,
					control);
	if (ret)
		goto error_ret;
	/* Also for consistency clear the mask */
	ret = lis3l02dq_spi_read_reg_8(indio_dev,
				       LIS3L02DQ_REG_WAKE_UP_CFG_ADDR,
				       &val);
	if (ret)
		goto error_ret;
	val &= ~0x3f;

	ret = lis3l02dq_spi_write_reg_8(indio_dev,
					LIS3L02DQ_REG_WAKE_UP_CFG_ADDR,
					val);
	if (ret)
		goto error_ret;

	ret = control;
error_ret:
	return ret;
}

static int lis3l02dq_write_event_config(struct iio_dev *indio_dev,
					u64 event_code,
					int state)
{
	int ret = 0;
	u8 val, control;
	u8 currentlyset;
	bool changed = false;
	u8 mask = (1 << (IIO_EVENT_CODE_EXTRACT_MODIFIER(event_code)*2 +
			 (IIO_EVENT_CODE_EXTRACT_DIR(event_code) ==
			  IIO_EV_DIR_RISING)));

	mutex_lock(&indio_dev->mlock);
	/* read current control */
	ret = lis3l02dq_spi_read_reg_8(indio_dev,
				       LIS3L02DQ_REG_CTRL_2_ADDR,
				       &control);
	if (ret)
		goto error_ret;
	ret = lis3l02dq_spi_read_reg_8(indio_dev,
				       LIS3L02DQ_REG_WAKE_UP_CFG_ADDR,
				       &val);
	if (ret < 0)
		goto error_ret;
	currentlyset = val & mask;

	if (!currentlyset && state) {
		changed = true;
		val |= mask;
	} else if (currentlyset && !state) {
		changed = true;
		val &= ~mask;
	}

	if (changed) {
		ret = lis3l02dq_spi_write_reg_8(indio_dev,
						LIS3L02DQ_REG_WAKE_UP_CFG_ADDR,
						val);
		if (ret)
			goto error_ret;
		control = val & 0x3f ?
			(control | LIS3L02DQ_REG_CTRL_2_ENABLE_INTERRUPT) :
			(control & ~LIS3L02DQ_REG_CTRL_2_ENABLE_INTERRUPT);
		ret = lis3l02dq_spi_write_reg_8(indio_dev,
					       LIS3L02DQ_REG_CTRL_2_ADDR,
					       control);
		if (ret)
			goto error_ret;
	}

error_ret:
	mutex_unlock(&indio_dev->mlock);
	return ret;
}

static struct attribute *lis3l02dq_attributes[] = {
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	NULL
};

static const struct attribute_group lis3l02dq_attribute_group = {
	.attrs = lis3l02dq_attributes,
};

static const struct iio_info lis3l02dq_info = {
	.read_raw = &lis3l02dq_read_raw,
	.write_raw = &lis3l02dq_write_raw,
	.read_event_value = &lis3l02dq_read_thresh,
	.write_event_value = &lis3l02dq_write_thresh,
	.write_event_config = &lis3l02dq_write_event_config,
	.read_event_config = &lis3l02dq_read_event_config,
	.driver_module = THIS_MODULE,
	.attrs = &lis3l02dq_attribute_group,
};

static int lis3l02dq_probe(struct spi_device *spi)
{
	int ret;
	struct lis3l02dq_state *st;
	struct iio_dev *indio_dev;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;
	st = iio_priv(indio_dev);
	/* this is only used for removal purposes */
	spi_set_drvdata(spi, indio_dev);

	st->us = spi;
	st->gpio = of_get_gpio(spi->dev.of_node, 0);
	mutex_init(&st->buf_lock);
	indio_dev->name = spi->dev.driver->name;
	indio_dev->dev.parent = &spi->dev;
	indio_dev->info = &lis3l02dq_info;
	indio_dev->channels = lis3l02dq_channels;
	indio_dev->num_channels = ARRAY_SIZE(lis3l02dq_channels);

	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = lis3l02dq_configure_buffer(indio_dev);
	if (ret)
		return ret;

	ret = iio_buffer_register(indio_dev,
				  lis3l02dq_channels,
				  ARRAY_SIZE(lis3l02dq_channels));
	if (ret) {
		printk(KERN_ERR "failed to initialize the buffer\n");
		goto error_unreg_buffer_funcs;
	}

	if (spi->irq) {
		ret = request_threaded_irq(st->us->irq,
					   &lis3l02dq_th,
					   &lis3l02dq_event_handler,
					   IRQF_TRIGGER_RISING,
					   "lis3l02dq",
					   indio_dev);
		if (ret)
			goto error_uninitialize_buffer;

		ret = lis3l02dq_probe_trigger(indio_dev);
		if (ret)
			goto error_free_interrupt;
	}

	/* Get the device into a sane initial state */
	ret = lis3l02dq_initial_setup(indio_dev);
	if (ret)
		goto error_remove_trigger;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_remove_trigger;

	return 0;

error_remove_trigger:
	if (spi->irq)
		lis3l02dq_remove_trigger(indio_dev);
error_free_interrupt:
	if (spi->irq)
		free_irq(st->us->irq, indio_dev);
error_uninitialize_buffer:
	iio_buffer_unregister(indio_dev);
error_unreg_buffer_funcs:
	lis3l02dq_unconfigure_buffer(indio_dev);
	return ret;
}

/* Power down the device */
static int lis3l02dq_stop_device(struct iio_dev *indio_dev)
{
	int ret;
	struct lis3l02dq_state *st = iio_priv(indio_dev);
	u8 val = 0;

	mutex_lock(&indio_dev->mlock);
	ret = lis3l02dq_spi_write_reg_8(indio_dev,
					LIS3L02DQ_REG_CTRL_1_ADDR,
					val);
	if (ret) {
		dev_err(&st->us->dev, "problem with turning device off: ctrl1");
		goto err_ret;
	}

	ret = lis3l02dq_spi_write_reg_8(indio_dev,
					LIS3L02DQ_REG_CTRL_2_ADDR,
					val);
	if (ret)
		dev_err(&st->us->dev, "problem with turning device off: ctrl2");
err_ret:
	mutex_unlock(&indio_dev->mlock);
	return ret;
}

/* fixme, confirm ordering in this function */
static int lis3l02dq_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct lis3l02dq_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	lis3l02dq_disable_all_events(indio_dev);
	lis3l02dq_stop_device(indio_dev);

	if (spi->irq)
		free_irq(st->us->irq, indio_dev);

	lis3l02dq_remove_trigger(indio_dev);
	iio_buffer_unregister(indio_dev);
	lis3l02dq_unconfigure_buffer(indio_dev);

	return 0;
}

static struct spi_driver lis3l02dq_driver = {
	.driver = {
		.name = "lis3l02dq",
		.owner = THIS_MODULE,
	},
	.probe = lis3l02dq_probe,
	.remove = lis3l02dq_remove,
};
module_spi_driver(lis3l02dq_driver);

MODULE_AUTHOR("Jonathan Cameron <jic23@kernel.org>");
MODULE_DESCRIPTION("ST LIS3L02DQ Accelerometer SPI driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("spi:lis3l02dq");
