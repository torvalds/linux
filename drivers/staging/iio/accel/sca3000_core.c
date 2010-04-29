/*
 * sca3000_core.c -- support VTI sca3000 series accelerometers via SPI
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * Copyright (c) 2009 Jonathan Cameron <jic23@cam.ac.uk>
 *
 * See industrialio/accels/sca3000.h for comments.
 */

#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/sysfs.h>
#include "../iio.h"
#include "../sysfs.h"
#include "../ring_generic.h"

#include "accel.h"
#include "sca3000.h"

enum sca3000_variant {
	d01,
	d03,
	e02,
	e04,
	e05,
	l01,
};

/* Note where option modes are not defined, the chip simply does not
 * support any.
 * Other chips in the sca3000 series use i2c and are not included here.
 *
 * Some of these devices are only listed in the family data sheet and
 * do not actually appear to be available.
 */
static const struct sca3000_chip_info sca3000_spi_chip_info_tbl[] = {
	{
		.name = "sca3000-d01",
		.temp_output = true,
		.measurement_mode_freq = 250,
		.option_mode_1 = SCA3000_OP_MODE_BYPASS,
		.option_mode_1_freq = 250,
	}, {
		/* No data sheet available - may be the same as the 3100-d03?*/
		.name = "sca3000-d03",
		.temp_output = true,
	}, {
		.name = "sca3000-e02",
		.measurement_mode_freq = 125,
		.option_mode_1 = SCA3000_OP_MODE_NARROW,
		.option_mode_1_freq = 63,
	}, {
		.name = "sca3000-e04",
		.measurement_mode_freq = 100,
		.option_mode_1 = SCA3000_OP_MODE_NARROW,
		.option_mode_1_freq = 50,
		.option_mode_2 = SCA3000_OP_MODE_WIDE,
		.option_mode_2_freq = 400,
	}, {
		.name = "sca3000-e05",
		.measurement_mode_freq = 200,
		.option_mode_1 = SCA3000_OP_MODE_NARROW,
		.option_mode_1_freq = 50,
		.option_mode_2 = SCA3000_OP_MODE_WIDE,
		.option_mode_2_freq = 400,
	}, {
		/* No data sheet available.
		 * Frequencies are unknown.
		 */
		.name = "sca3000-l01",
		.temp_output = true,
		.option_mode_1 = SCA3000_OP_MODE_BYPASS,
	},
};


int sca3000_write_reg(struct sca3000_state *st, u8 address, u8 val)
{
	struct spi_transfer xfer = {
		.bits_per_word = 8,
		.len = 2,
		.cs_change = 1,
		.tx_buf = st->tx,
	};
	struct spi_message msg;

	st->tx[0] = SCA3000_WRITE_REG(address);
	st->tx[1] = val;
	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	return spi_sync(st->us, &msg);
}

int sca3000_read_data(struct sca3000_state *st,
		      uint8_t reg_address_high,
		      u8 **rx_p,
		      int len)
{
	int ret;
	struct spi_message msg;
	struct spi_transfer xfer = {
		.bits_per_word = 8,
		.len = len + 1,
		.cs_change = 1,
		.tx_buf = st->tx,
	};

	*rx_p = kmalloc(len + 1, GFP_KERNEL);
	if (*rx_p == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	xfer.rx_buf = *rx_p;
	st->tx[0] = SCA3000_READ_REG(reg_address_high);
	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	ret = spi_sync(st->us, &msg);

	if (ret) {
		dev_err(get_device(&st->us->dev), "problem reading register");
		goto error_free_rx;
	}

	return 0;
error_free_rx:
	kfree(*rx_p);
error_ret:
	return ret;

}
/**
 * sca3000_reg_lock_on() test if the ctrl register lock is on
 *
 * Lock must be held.
 **/
static int sca3000_reg_lock_on(struct sca3000_state *st)
{
	u8 *rx;
	int ret;

	ret = sca3000_read_data(st, SCA3000_REG_ADDR_STATUS, &rx, 1);

	if (ret < 0)
		return ret;
	ret = !(rx[1] & SCA3000_LOCKED);
	kfree(rx);

	return ret;
}

/**
 * __sca3000_unlock_reg_lock() unlock the control registers
 *
 * Note the device does not appear to support doing this in a single transfer.
 * This should only ever be used as part of ctrl reg read.
 * Lock must be held before calling this
 **/
static int __sca3000_unlock_reg_lock(struct sca3000_state *st)
{
	struct spi_message msg;
	struct spi_transfer xfer[3] = {
		{
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.tx_buf = st->tx,
		}, {
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.tx_buf = st->tx + 2,
		}, {
			.bits_per_word = 8,
			.len = 2,
			.cs_change = 1,
			.tx_buf = st->tx + 4,
		},
	};
	st->tx[0] = SCA3000_WRITE_REG(SCA3000_REG_ADDR_UNLOCK);
	st->tx[1] = 0x00;
	st->tx[2] = SCA3000_WRITE_REG(SCA3000_REG_ADDR_UNLOCK);
	st->tx[3] = 0x50;
	st->tx[4] = SCA3000_WRITE_REG(SCA3000_REG_ADDR_UNLOCK);
	st->tx[5] = 0xA0;
	spi_message_init(&msg);
	spi_message_add_tail(&xfer[0], &msg);
	spi_message_add_tail(&xfer[1], &msg);
	spi_message_add_tail(&xfer[2], &msg);

	return spi_sync(st->us, &msg);
}

/**
 * sca3000_write_ctrl_reg() write to a lock protect ctrl register
 * @sel: selects which registers we wish to write to
 * @val: the value to be written
 *
 * Certain control registers are protected against overwriting by the lock
 * register and use a shared write address. This function allows writing of
 * these registers.
 * Lock must be held.
 **/
static int sca3000_write_ctrl_reg(struct sca3000_state *st,
				  uint8_t sel,
				  uint8_t val)
{

	int ret;

	ret = sca3000_reg_lock_on(st);
	if (ret < 0)
		goto error_ret;
	if (ret) {
		ret = __sca3000_unlock_reg_lock(st);
		if (ret)
			goto error_ret;
	}

	/* Set the control select register */
	ret = sca3000_write_reg(st, SCA3000_REG_ADDR_CTRL_SEL, sel);
	if (ret)
		goto error_ret;

	/* Write the actual value into the register */
	ret = sca3000_write_reg(st, SCA3000_REG_ADDR_CTRL_DATA, val);

error_ret:
	return ret;
}

/* Crucial that lock is called before calling this */
/**
 * sca3000_read_ctrl_reg() read from lock protected control register.
 *
 * Lock must be held.
 **/
static int sca3000_read_ctrl_reg(struct sca3000_state *st,
				 u8 ctrl_reg,
				 u8 **rx_p)
{
	int ret;

	ret = sca3000_reg_lock_on(st);
	if (ret < 0)
		goto error_ret;
	if (ret) {
		ret = __sca3000_unlock_reg_lock(st);
		if (ret)
			goto error_ret;
	}
	/* Set the control select register */
	ret = sca3000_write_reg(st, SCA3000_REG_ADDR_CTRL_SEL, ctrl_reg);
	if (ret)
		goto error_ret;
	ret = sca3000_read_data(st, SCA3000_REG_ADDR_CTRL_DATA, rx_p, 1);

error_ret:
	return ret;
}

#ifdef SCA3000_DEBUG
/**
 * sca3000_check_status() check the status register
 *
 * Only used for debugging purposes
 **/
static int sca3000_check_status(struct device *dev)
{
	u8 *rx;
	int ret;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct sca3000_state *st = indio_dev->dev_data;

	mutex_lock(&st->lock);
	ret = sca3000_read_data(st, SCA3000_REG_ADDR_STATUS, &rx, 1);
	if (ret < 0)
		goto error_ret;
	if (rx[1] & SCA3000_EEPROM_CS_ERROR)
		dev_err(dev, "eeprom error \n");
	if (rx[1] & SCA3000_SPI_FRAME_ERROR)
		dev_err(dev, "Previous SPI Frame was corrupt\n");
	kfree(rx);

error_ret:
	mutex_unlock(&st->lock);
	return ret;
}
#endif /* SCA3000_DEBUG */

/**
 * sca3000_read_13bit_signed() sysfs interface to read 13 bit signed registers
 *
 * These are described as signed 12 bit on the data sheet, which appears
 * to be a conventional 2's complement 13 bit.
 **/
static ssize_t sca3000_read_13bit_signed(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	int len = 0, ret;
	int val;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	u8 *rx;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct sca3000_state *st = indio_dev->dev_data;

	mutex_lock(&st->lock);
	ret = sca3000_read_data(st, this_attr->address, &rx, 2);
	if (ret < 0)
		goto error_ret;
	val = sca3000_13bit_convert(rx[1], rx[2]);
	len += sprintf(buf + len, "%d\n", val);
	kfree(rx);
error_ret:
	mutex_unlock(&st->lock);

	return ret ? ret : len;
}


static ssize_t sca3000_show_name(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct sca3000_state *st = dev_info->dev_data;
	return sprintf(buf, "%s\n", st->info->name);
}
/**
 * sca3000_show_reg() - sysfs interface to read the chip revision number
 **/
static ssize_t sca3000_show_rev(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	int len = 0, ret;
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct sca3000_state *st = dev_info->dev_data;

	u8 *rx;

	mutex_lock(&st->lock);
	ret = sca3000_read_data(st, SCA3000_REG_ADDR_REVID, &rx, 1);
	if (ret < 0)
		goto error_ret;
	len += sprintf(buf + len,
		       "major=%d, minor=%d\n",
		       rx[1] & SCA3000_REVID_MAJOR_MASK,
		       rx[1] & SCA3000_REVID_MINOR_MASK);
	kfree(rx);

error_ret:
	mutex_unlock(&st->lock);

	return ret ? ret : len;
}

/**
 * sca3000_show_available_measurement_modes() display available modes
 *
 * This is all read from chip specific data in the driver. Not all
 * of the sca3000 series support modes other than normal.
 **/
static ssize_t
sca3000_show_available_measurement_modes(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct sca3000_state *st = dev_info->dev_data;
	int len = 0;

	len += sprintf(buf + len, "0 - normal mode");
	switch (st->info->option_mode_1) {
	case SCA3000_OP_MODE_NARROW:
		len += sprintf(buf + len, ", 1 - narrow mode");
		break;
	case SCA3000_OP_MODE_BYPASS:
		len += sprintf(buf + len, ", 1 - bypass mode");
		break;
	};
	switch (st->info->option_mode_2) {
	case SCA3000_OP_MODE_WIDE:
		len += sprintf(buf + len, ", 2 - wide mode");
		break;
	}
	/* always supported */
	len += sprintf(buf + len, " 3 - motion detection \n");

	return len;
}

/**
 * sca3000_show_measurmenet_mode() sysfs read of current mode
 **/
static ssize_t
sca3000_show_measurement_mode(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct sca3000_state *st = dev_info->dev_data;
	int len = 0, ret;
	u8 *rx;

	mutex_lock(&st->lock);
	ret = sca3000_read_data(st, SCA3000_REG_ADDR_MODE, &rx, 1);
	if (ret)
		goto error_ret;
	/* mask bottom 2 bits - only ones that are relevant */
	rx[1] &= 0x03;
	switch (rx[1]) {
	case SCA3000_MEAS_MODE_NORMAL:
		len += sprintf(buf + len, "0 - normal mode\n");
		break;
	case SCA3000_MEAS_MODE_MOT_DET:
		len += sprintf(buf + len, "3 - motion detection\n");
		break;
	case SCA3000_MEAS_MODE_OP_1:
		switch (st->info->option_mode_1) {
		case SCA3000_OP_MODE_NARROW:
			len += sprintf(buf + len, "1 - narrow mode\n");
			break;
		case SCA3000_OP_MODE_BYPASS:
			len += sprintf(buf + len, "1 - bypass mode\n");
			break;
		};
		break;
	case SCA3000_MEAS_MODE_OP_2:
		switch (st->info->option_mode_2) {
		case SCA3000_OP_MODE_WIDE:
			len += sprintf(buf + len, "2 - wide mode\n");
			break;
		}
		break;
	};

error_ret:
	mutex_unlock(&st->lock);

	return ret ? ret : len;
}

/**
 * sca3000_store_measurement_mode() set the current mode
 **/
static ssize_t
sca3000_store_measurement_mode(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf,
			       size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct sca3000_state *st = dev_info->dev_data;
	int ret;
	u8 *rx;
	int mask = 0x03;
	long val;

	mutex_lock(&st->lock);
	ret = strict_strtol(buf, 10, &val);
	if (ret)
		goto error_ret;
	ret = sca3000_read_data(st, SCA3000_REG_ADDR_MODE, &rx, 1);
	if (ret)
		goto error_ret;
	rx[1] &= ~mask;
	rx[1] |= (val & mask);
	ret = sca3000_write_reg(st, SCA3000_REG_ADDR_MODE, rx[1]);
	if (ret)
		goto error_free_rx;
	mutex_unlock(&st->lock);

	return len;

error_free_rx:
	kfree(rx);
error_ret:
	mutex_unlock(&st->lock);

	return ret;
}


/* Not even vaguely standard attributes so defined here rather than
 * in the relevant IIO core headers
 */
static IIO_DEVICE_ATTR(available_measurement_modes, S_IRUGO,
		       sca3000_show_available_measurement_modes,
		       NULL, 0);

static IIO_DEVICE_ATTR(measurement_mode, S_IRUGO | S_IWUSR,
		       sca3000_show_measurement_mode,
		       sca3000_store_measurement_mode,
		       0);

/* More standard attributes */

static IIO_DEV_ATTR_NAME(sca3000_show_name);
static IIO_DEV_ATTR_REV(sca3000_show_rev);

static IIO_DEV_ATTR_ACCEL_X(sca3000_read_13bit_signed,
			    SCA3000_REG_ADDR_X_MSB);
static IIO_DEV_ATTR_ACCEL_Y(sca3000_read_13bit_signed,
			    SCA3000_REG_ADDR_Y_MSB);
static IIO_DEV_ATTR_ACCEL_Z(sca3000_read_13bit_signed,
			    SCA3000_REG_ADDR_Z_MSB);


/**
 * sca3000_read_av_freq() sysfs function to get available frequencies
 *
 * The later modes are only relevant to the ring buffer - and depend on current
 * mode. Note that data sheet gives rather wide tolerances for these so integer
 * division will give good enough answer and not all chips have them specified
 * at all.
 **/
static ssize_t sca3000_read_av_freq(struct device *dev,
			     struct device_attribute *attr,
			     char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct sca3000_state *st = indio_dev->dev_data;
	int len = 0, ret;
	u8 *rx;
	mutex_lock(&st->lock);
	ret = sca3000_read_data(st, SCA3000_REG_ADDR_MODE, &rx, 1);
	mutex_unlock(&st->lock);
	if (ret)
		goto error_ret;
	rx[1] &= 0x03;
	switch (rx[1]) {
	case SCA3000_MEAS_MODE_NORMAL:
		len += sprintf(buf + len, "%d %d %d\n",
			       st->info->measurement_mode_freq,
			       st->info->measurement_mode_freq/2,
			       st->info->measurement_mode_freq/4);
		break;
	case SCA3000_MEAS_MODE_OP_1:
		len += sprintf(buf + len, "%d %d %d\n",
			       st->info->option_mode_1_freq,
			       st->info->option_mode_1_freq/2,
			       st->info->option_mode_1_freq/4);
		break;
	case SCA3000_MEAS_MODE_OP_2:
		len += sprintf(buf + len, "%d %d %d\n",
			       st->info->option_mode_2_freq,
			       st->info->option_mode_2_freq/2,
			       st->info->option_mode_2_freq/4);
		break;
	};
	kfree(rx);
	return len;
error_ret:
	return ret;
}
/**
 * __sca3000_get_base_frequency() obtain mode specific base frequency
 *
 * lock must be held
 **/
static inline int __sca3000_get_base_freq(struct sca3000_state *st,
					  const struct sca3000_chip_info *info,
					  int *base_freq)
{
	int ret;
	u8 *rx;

	ret = sca3000_read_data(st, SCA3000_REG_ADDR_MODE, &rx, 1);
	if (ret)
		goto error_ret;
	switch (0x03 & rx[1]) {
	case SCA3000_MEAS_MODE_NORMAL:
		*base_freq = info->measurement_mode_freq;
		break;
	case SCA3000_MEAS_MODE_OP_1:
		*base_freq = info->option_mode_1_freq;
		break;
	case SCA3000_MEAS_MODE_OP_2:
		*base_freq = info->option_mode_2_freq;
		break;
	};
	kfree(rx);
error_ret:
	return ret;
}

/**
 * sca3000_read_frequency() sysfs interface to get the current frequency
 **/
static ssize_t sca3000_read_frequency(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct sca3000_state *st = indio_dev->dev_data;
	int ret, len = 0, base_freq = 0;
	u8 *rx;
	mutex_lock(&st->lock);
	ret = __sca3000_get_base_freq(st, st->info, &base_freq);
	if (ret)
		goto error_ret_mut;
	ret = sca3000_read_ctrl_reg(st, SCA3000_REG_CTRL_SEL_OUT_CTRL, &rx);
	mutex_unlock(&st->lock);
	if (ret)
		goto error_ret;
	if (base_freq > 0)
		switch (rx[1]&0x03) {
		case 0x00:
		case 0x03:
			len = sprintf(buf, "%d\n", base_freq);
			break;
		case 0x01:
			len = sprintf(buf, "%d\n", base_freq/2);
			break;
		case 0x02:
			len = sprintf(buf, "%d\n", base_freq/4);
			break;
	};
			kfree(rx);
	return len;
error_ret_mut:
	mutex_unlock(&st->lock);
error_ret:
	return ret;
}

/**
 * sca3000_set_frequency() sysfs interface to set the current frequency
 **/
static ssize_t sca3000_set_frequency(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf,
			      size_t len)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct sca3000_state *st = indio_dev->dev_data;
	int ret, base_freq = 0;
	u8 *rx;
	long val;

	ret = strict_strtol(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&st->lock);
	/* What mode are we in? */
	ret = __sca3000_get_base_freq(st, st->info, &base_freq);
	if (ret)
		goto error_free_lock;

	ret = sca3000_read_ctrl_reg(st, SCA3000_REG_CTRL_SEL_OUT_CTRL, &rx);
	if (ret)
		goto error_free_lock;
	/* clear the bits */
	rx[1] &= ~0x03;

	if (val == base_freq/2) {
		rx[1] |= SCA3000_OUT_CTRL_BUF_DIV_2;
	} else if (val == base_freq/4) {
		rx[1] |= SCA3000_OUT_CTRL_BUF_DIV_4;
	} else if (val != base_freq) {
		ret = -EINVAL;
		goto error_free_lock;
	}
	ret = sca3000_write_ctrl_reg(st, SCA3000_REG_CTRL_SEL_OUT_CTRL, rx[1]);
error_free_lock:
	mutex_unlock(&st->lock);

	return ret ? ret : len;
}

/* Should only really be registered if ring buffer support is compiled in.
 * Does no harm however and doing it right would add a fair bit of complexity
 */
static IIO_DEV_ATTR_AVAIL_SAMP_FREQ(sca3000_read_av_freq);

static IIO_DEV_ATTR_SAMP_FREQ(S_IWUSR | S_IRUGO,
			      sca3000_read_frequency,
			      sca3000_set_frequency);


/**
 * sca3000_read_temp() sysfs interface to get the temperature when available
 *
* The alignment of data in here is downright odd. See data sheet.
* Converting this into a meaningful value is left to inline functions in
* userspace part of header.
**/
static ssize_t sca3000_read_temp(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct sca3000_state *st = indio_dev->dev_data;
	int len = 0, ret;
	int val;
	u8 *rx;
	ret = sca3000_read_data(st, SCA3000_REG_ADDR_TEMP_MSB, &rx, 2);
	if (ret < 0)
		goto error_ret;
	val = ((rx[1]&0x3F) << 3) | ((rx[2] & 0xE0) >> 5);
	len += sprintf(buf + len, "%d\n", val);
	kfree(rx);

	return len;

error_ret:
	return ret;
}
static IIO_DEV_ATTR_TEMP(sca3000_read_temp);

/**
 * sca3000_show_thresh() sysfs query of a threshold
 **/
static ssize_t sca3000_show_thresh(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct sca3000_state *st = indio_dev->dev_data;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int len = 0, ret;
	u8 *rx;

	mutex_lock(&st->lock);
	ret = sca3000_read_ctrl_reg(st,
				    this_attr->address,
				    &rx);
	mutex_unlock(&st->lock);
	if (ret)
		return ret;
	len += sprintf(buf + len, "%d\n", rx[1]);
	kfree(rx);

	return len;
}

/**
 * sca3000_write_thresh() sysfs control of threshold
 **/
static ssize_t sca3000_write_thresh(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf,
				    size_t len)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct sca3000_state *st = indio_dev->dev_data;
	struct iio_dev_attr *this_attr = to_iio_dev_attr(attr);
	int ret;
	long val;

	ret = strict_strtol(buf, 10, &val);
	if (ret)
		return ret;
	mutex_lock(&st->lock);
	ret = sca3000_write_ctrl_reg(st, this_attr->address, val);
	mutex_unlock(&st->lock);

	return ret ? ret : len;
}

static IIO_DEV_ATTR_ACCEL_THRESH_X(S_IRUGO | S_IWUSR,
				   sca3000_show_thresh,
				   sca3000_write_thresh,
				   SCA3000_REG_CTRL_SEL_MD_X_TH);
static IIO_DEV_ATTR_ACCEL_THRESH_Y(S_IRUGO | S_IWUSR,
				   sca3000_show_thresh,
				   sca3000_write_thresh,
				   SCA3000_REG_CTRL_SEL_MD_Y_TH);
static IIO_DEV_ATTR_ACCEL_THRESH_Z(S_IRUGO | S_IWUSR,
				   sca3000_show_thresh,
				   sca3000_write_thresh,
				   SCA3000_REG_CTRL_SEL_MD_Z_TH);

static struct attribute *sca3000_attributes[] = {
	&iio_dev_attr_name.dev_attr.attr,
	&iio_dev_attr_revision.dev_attr.attr,
	&iio_dev_attr_accel_x.dev_attr.attr,
	&iio_dev_attr_accel_y.dev_attr.attr,
	&iio_dev_attr_accel_z.dev_attr.attr,
	&iio_dev_attr_thresh_accel_x.dev_attr.attr,
	&iio_dev_attr_thresh_accel_y.dev_attr.attr,
	&iio_dev_attr_thresh_accel_z.dev_attr.attr,
	&iio_dev_attr_available_measurement_modes.dev_attr.attr,
	&iio_dev_attr_measurement_mode.dev_attr.attr,
	&iio_dev_attr_available_sampling_frequency.dev_attr.attr,
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	NULL,
};

static struct attribute *sca3000_attributes_with_temp[] = {
	&iio_dev_attr_name.dev_attr.attr,
	&iio_dev_attr_revision.dev_attr.attr,
	&iio_dev_attr_accel_x.dev_attr.attr,
	&iio_dev_attr_accel_y.dev_attr.attr,
	&iio_dev_attr_accel_z.dev_attr.attr,
	&iio_dev_attr_thresh_accel_x.dev_attr.attr,
	&iio_dev_attr_thresh_accel_y.dev_attr.attr,
	&iio_dev_attr_thresh_accel_z.dev_attr.attr,
	&iio_dev_attr_available_measurement_modes.dev_attr.attr,
	&iio_dev_attr_measurement_mode.dev_attr.attr,
	&iio_dev_attr_available_sampling_frequency.dev_attr.attr,
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	/* Only present if temp sensor is */
	&iio_dev_attr_temp.dev_attr.attr,
	NULL,
};

static const struct attribute_group sca3000_attribute_group = {
	.attrs = sca3000_attributes,
};

static const struct attribute_group sca3000_attribute_group_with_temp = {
	.attrs = sca3000_attributes_with_temp,
};

/* RING RELATED interrupt handler */
/* depending on event, push to the ring buffer event chrdev or the event one */

/**
 * sca3000_interrupt_handler_bh() - handling ring and non ring events
 *
 * This function is complicated by the fact that the devices can signify ring
 * and non ring events via the same interrupt line and they can only
 * be distinguished via a read of the relevant status register.
 **/
static void sca3000_interrupt_handler_bh(struct work_struct *work_s)
{
	struct sca3000_state *st
		= container_of(work_s, struct sca3000_state,
			       interrupt_handler_ws);
	u8 *rx;
	int ret;

	/* Could lead if badly timed to an extra read of status reg,
	 * but ensures no interrupt is missed.
	 */
	enable_irq(st->us->irq);
	mutex_lock(&st->lock);
	ret = sca3000_read_data(st, SCA3000_REG_ADDR_INT_STATUS,
				&rx, 1);
	mutex_unlock(&st->lock);
	if (ret)
		goto done;

	sca3000_ring_int_process(rx[1], st->indio_dev->ring);

	if (rx[1] & SCA3000_INT_STATUS_FREE_FALL)
		iio_push_event(st->indio_dev, 0,
			       IIO_EVENT_CODE_FREE_FALL,
			       st->last_timestamp);

	if (rx[1] & SCA3000_INT_STATUS_Y_TRIGGER)
		iio_push_event(st->indio_dev, 0,
			       IIO_EVENT_CODE_ACCEL_Y_HIGH,
			       st->last_timestamp);

	if (rx[1] & SCA3000_INT_STATUS_X_TRIGGER)
		iio_push_event(st->indio_dev, 0,
			       IIO_EVENT_CODE_ACCEL_X_HIGH,
			       st->last_timestamp);

	if (rx[1] & SCA3000_INT_STATUS_Z_TRIGGER)
		iio_push_event(st->indio_dev, 0,
			       IIO_EVENT_CODE_ACCEL_Z_HIGH,
			       st->last_timestamp);

done:
	kfree(rx);
	return;
}

/**
 * sca3000_handler_th() handles all interrupt events from device
 *
 * These devices deploy unified interrupt status registers meaning
 * all interrupts must be handled together
 **/
static int sca3000_handler_th(struct iio_dev *dev_info,
			      int index,
			      s64 timestamp,
			      int no_test)
{
	struct sca3000_state *st = dev_info->dev_data;

	st->last_timestamp = timestamp;
	schedule_work(&st->interrupt_handler_ws);

	return 0;
}

/**
 * sca3000_query_mo_det() is motion detection enabled for this axis
 *
 * First queries if motion detection is enabled and then if this axis is
 * on.
 **/
static ssize_t sca3000_query_mo_det(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct sca3000_state *st = indio_dev->dev_data;
	struct iio_event_attr *this_attr = to_iio_event_attr(attr);
	int ret, len = 0;
	u8 *rx;
	u8 protect_mask = 0x03;

	/* read current value of mode register */
	mutex_lock(&st->lock);
	ret = sca3000_read_data(st, SCA3000_REG_ADDR_MODE, &rx, 1);
	if (ret)
		goto error_ret;

	if ((rx[1]&protect_mask) != SCA3000_MEAS_MODE_MOT_DET)
		len += sprintf(buf + len, "0\n");
	else {
		kfree(rx);
		ret = sca3000_read_ctrl_reg(st,
					    SCA3000_REG_CTRL_SEL_MD_CTRL,
					    &rx);
		if (ret)
			goto error_ret;
		/* only supporting logical or's for now */
		len += sprintf(buf + len, "%d\n",
			       (rx[1] & this_attr->mask) ? 1 : 0);
	}
	kfree(rx);
error_ret:
	mutex_unlock(&st->lock);

	return ret ? ret : len;
}
/**
 * sca3000_query_free_fall_mode() is free fall mode enabled
 **/
static ssize_t sca3000_query_free_fall_mode(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	int ret, len;
	u8 *rx;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct sca3000_state *st = indio_dev->dev_data;

	mutex_lock(&st->lock);
	ret = sca3000_read_data(st, SCA3000_REG_ADDR_MODE, &rx, 1);
	mutex_unlock(&st->lock);
	if (ret)
		return ret;
	len = sprintf(buf, "%d\n",
		      !!(rx[1] & SCA3000_FREE_FALL_DETECT));
	kfree(rx);

	return len;
}
/**
 * sca3000_query_ring_int() is the hardware ring status interrupt enabled
 **/
static ssize_t sca3000_query_ring_int(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct iio_event_attr *this_attr = to_iio_event_attr(attr);
	int ret, len;
	u8 *rx;
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct sca3000_state *st = indio_dev->dev_data;
	mutex_lock(&st->lock);
	ret = sca3000_read_data(st, SCA3000_REG_ADDR_INT_MASK, &rx, 1);
	mutex_unlock(&st->lock);
	if (ret)
		return ret;
	len = sprintf(buf, "%d\n", (rx[1] & this_attr->mask) ? 1 : 0);
	kfree(rx);

	return len;
}
/**
 * sca3000_set_ring_int() set state of ring status interrupt
 **/
static ssize_t sca3000_set_ring_int(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf,
				      size_t len)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct sca3000_state *st = indio_dev->dev_data;
	struct iio_event_attr *this_attr = to_iio_event_attr(attr);

	long val;
	int ret;
	u8 *rx;

	mutex_lock(&st->lock);
	ret = strict_strtol(buf, 10, &val);
	if (ret)
		goto error_ret;
	ret = sca3000_read_data(st, SCA3000_REG_ADDR_INT_MASK, &rx, 1);
	if (ret)
		goto error_ret;
	if (val)
		ret = sca3000_write_reg(st,
					SCA3000_REG_ADDR_INT_MASK,
					rx[1] | this_attr->mask);
	else
		ret = sca3000_write_reg(st,
					SCA3000_REG_ADDR_INT_MASK,
					rx[1] & ~this_attr->mask);
	kfree(rx);
error_ret:
	mutex_unlock(&st->lock);

	return ret ? ret : len;
}

/**
 * sca3000_set_free_fall_mode() simple on off control for free fall int
 *
 * In these chips the free fall detector should send an interrupt if
 * the device falls more than 25cm.  This has not been tested due
 * to fragile wiring.
 **/

static ssize_t sca3000_set_free_fall_mode(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf,
					  size_t len)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct sca3000_state *st = indio_dev->dev_data;
	long val;
	int ret;
	u8 *rx;
	u8 protect_mask = SCA3000_FREE_FALL_DETECT;

	mutex_lock(&st->lock);
	ret = strict_strtol(buf, 10, &val);
	if (ret)
		goto error_ret;

	/* read current value of mode register */
	ret = sca3000_read_data(st, SCA3000_REG_ADDR_MODE, &rx, 1);
	if (ret)
		goto error_ret;

	/*if off and should be on*/
	if (val && !(rx[1] & protect_mask))
		ret = sca3000_write_reg(st, SCA3000_REG_ADDR_MODE,
					(rx[1] | SCA3000_FREE_FALL_DETECT));
	/* if on and should be off */
	else if (!val && (rx[1]&protect_mask))
		ret = sca3000_write_reg(st, SCA3000_REG_ADDR_MODE,
					(rx[1] & ~protect_mask));

	kfree(rx);
error_ret:
	mutex_unlock(&st->lock);

	return ret ? ret : len;
}

/**
 * sca3000_set_mo_det() simple on off control for motion detector
 *
 * This is a per axis control, but enabling any will result in the
 * motion detector unit being enabled.
 * N.B. enabling motion detector stops normal data acquisition.
 * There is a complexity in knowing which mode to return to when
 * this mode is disabled.  Currently normal mode is assumed.
 **/
static ssize_t sca3000_set_mo_det(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf,
				  size_t len)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct sca3000_state *st = indio_dev->dev_data;
	struct iio_event_attr *this_attr = to_iio_event_attr(attr);
	long val;
	int ret;
	u8 *rx;
	u8 protect_mask = 0x03;
	ret = strict_strtol(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&st->lock);
	/* First read the motion detector config to find out if
	 * this axis is on*/
	ret = sca3000_read_ctrl_reg(st,
				    SCA3000_REG_CTRL_SEL_MD_CTRL,
				    &rx);
	if (ret)
		goto exit_point;
	/* Off and should be on */
	if (val && !(rx[1] & this_attr->mask)) {
		ret = sca3000_write_ctrl_reg(st,
					     SCA3000_REG_CTRL_SEL_MD_CTRL,
					     rx[1] | this_attr->mask);
		if (ret)
			goto exit_point_free_rx;
		st->mo_det_use_count++;
	} else if (!val && (rx[1]&this_attr->mask)) {
		ret = sca3000_write_ctrl_reg(st,
					     SCA3000_REG_CTRL_SEL_MD_CTRL,
					     rx[1] & ~(this_attr->mask));
		if (ret)
			goto exit_point_free_rx;
		st->mo_det_use_count--;
	} else /* relies on clean state for device on boot */
		goto exit_point_free_rx;
	kfree(rx);
	/* read current value of mode register */
	ret = sca3000_read_data(st, SCA3000_REG_ADDR_MODE, &rx, 1);
	if (ret)
		goto exit_point;
	/*if off and should be on*/
	if ((st->mo_det_use_count)
	    && ((rx[1]&protect_mask) != SCA3000_MEAS_MODE_MOT_DET))
		ret = sca3000_write_reg(st, SCA3000_REG_ADDR_MODE,
					(rx[1] & ~protect_mask)
					| SCA3000_MEAS_MODE_MOT_DET);
	/* if on and should be off */
	else if (!(st->mo_det_use_count)
		 && ((rx[1]&protect_mask) == SCA3000_MEAS_MODE_MOT_DET))
		ret = sca3000_write_reg(st, SCA3000_REG_ADDR_MODE,
					(rx[1] & ~protect_mask));
exit_point_free_rx:
	kfree(rx);
exit_point:
	mutex_unlock(&st->lock);

	return ret ? ret : len;
}

/* Shared event handler for all events as single event status register */
IIO_EVENT_SH(all, &sca3000_handler_th);

/* Free fall detector related event attribute */
IIO_EVENT_ATTR_FREE_FALL_DETECT_SH(iio_event_all,
				   sca3000_query_free_fall_mode,
				   sca3000_set_free_fall_mode,
				   0)

/* Motion detector related event attributes */
IIO_EVENT_ATTR_ACCEL_X_HIGH_SH(iio_event_all,
			       sca3000_query_mo_det,
			       sca3000_set_mo_det,
			       SCA3000_MD_CTRL_OR_X);

IIO_EVENT_ATTR_ACCEL_Y_HIGH_SH(iio_event_all,
			       sca3000_query_mo_det,
			       sca3000_set_mo_det,
			       SCA3000_MD_CTRL_OR_Y);

IIO_EVENT_ATTR_ACCEL_Z_HIGH_SH(iio_event_all,
			       sca3000_query_mo_det,
			       sca3000_set_mo_det,
			       SCA3000_MD_CTRL_OR_Z);

/* Hardware ring buffer related event attributes */
IIO_EVENT_ATTR_RING_50_FULL_SH(iio_event_all,
			       sca3000_query_ring_int,
			       sca3000_set_ring_int,
			       SCA3000_INT_MASK_RING_HALF);

IIO_EVENT_ATTR_RING_75_FULL_SH(iio_event_all,
			       sca3000_query_ring_int,
			       sca3000_set_ring_int,
			       SCA3000_INT_MASK_RING_THREE_QUARTER);

static struct attribute *sca3000_event_attributes[] = {
	&iio_event_attr_free_fall.dev_attr.attr,
	&iio_event_attr_accel_x_high.dev_attr.attr,
	&iio_event_attr_accel_y_high.dev_attr.attr,
	&iio_event_attr_accel_z_high.dev_attr.attr,
	&iio_event_attr_ring_50_full.dev_attr.attr,
	&iio_event_attr_ring_75_full.dev_attr.attr,
	NULL,
};

static struct attribute_group sca3000_event_attribute_group = {
	.attrs = sca3000_event_attributes,
};

/**
 * sca3000_clean_setup() get the device into a predictable state
 *
 * Devices use flash memory to store many of the register values
 * and hence can come up in somewhat unpredictable states.
 * Hence reset everything on driver load.
  **/
static int sca3000_clean_setup(struct sca3000_state *st)
{
	int ret;
	u8 *rx;

	mutex_lock(&st->lock);
	/* Ensure all interrupts have been acknowledged */
	ret = sca3000_read_data(st, SCA3000_REG_ADDR_INT_STATUS, &rx, 1);
	if (ret)
		goto error_ret;
	kfree(rx);

	/* Turn off all motion detection channels */
	ret = sca3000_read_ctrl_reg(st,
				    SCA3000_REG_CTRL_SEL_MD_CTRL,
				    &rx);
	if (ret)
		goto error_ret;
	ret = sca3000_write_ctrl_reg(st,
				     SCA3000_REG_CTRL_SEL_MD_CTRL,
				     rx[1] & SCA3000_MD_CTRL_PROT_MASK);
	kfree(rx);
	if (ret)
		goto error_ret;

	/* Disable ring buffer */
	sca3000_read_ctrl_reg(st,
			      SCA3000_REG_CTRL_SEL_OUT_CTRL,
			      &rx);
	/* Frequency of ring buffer sampling deliberately restricted to make
	 * debugging easier - add control of this later */
	ret = sca3000_write_ctrl_reg(st,
				     SCA3000_REG_CTRL_SEL_OUT_CTRL,
				     (rx[1] & SCA3000_OUT_CTRL_PROT_MASK)
				     | SCA3000_OUT_CTRL_BUF_X_EN
				     | SCA3000_OUT_CTRL_BUF_Y_EN
				     | SCA3000_OUT_CTRL_BUF_Z_EN
				     | SCA3000_OUT_CTRL_BUF_DIV_4);
	kfree(rx);

	if (ret)
		goto error_ret;
	/* Enable interrupts, relevant to mode and set up as active low */
	ret = sca3000_read_data(st,
			  SCA3000_REG_ADDR_INT_MASK,
			  &rx, 1);
	if (ret)
		goto error_ret;
	ret = sca3000_write_reg(st,
				SCA3000_REG_ADDR_INT_MASK,
				(rx[1] & SCA3000_INT_MASK_PROT_MASK)
				| SCA3000_INT_MASK_ACTIVE_LOW);
	kfree(rx);
	if (ret)
		goto error_ret;
	/* Select normal measurement mode, free fall off, ring off */
	/* Ring in 12 bit mode - it is fine to overwrite reserved bits 3,5
	 * as that occurs in one of the example on the datasheet */
	ret = sca3000_read_data(st,
			  SCA3000_REG_ADDR_MODE,
			  &rx, 1);
	if (ret)
		goto error_ret;
	ret = sca3000_write_reg(st,
				SCA3000_REG_ADDR_MODE,
				(rx[1] & SCA3000_MODE_PROT_MASK));
	kfree(rx);
	st->bpse = 11;

error_ret:
	mutex_unlock(&st->lock);
	return ret;
}

static int __devinit __sca3000_probe(struct spi_device *spi,
				     enum sca3000_variant variant)
{
	int ret, regdone = 0;
	struct sca3000_state *st;

	st = kzalloc(sizeof(struct sca3000_state), GFP_KERNEL);
	if (st == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	spi_set_drvdata(spi, st);

	st->tx = kmalloc(sizeof(*st->tx)*6, GFP_KERNEL);
	if (st->tx == NULL) {
		ret = -ENOMEM;
		goto error_clear_st;
	}
	st->rx = kmalloc(sizeof(*st->rx)*3, GFP_KERNEL);
	if (st->rx == NULL) {
		ret = -ENOMEM;
		goto error_free_tx;
	}
	st->us = spi;
	mutex_init(&st->lock);
	st->info = &sca3000_spi_chip_info_tbl[variant];

	st->indio_dev = iio_allocate_device();
	if (st->indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_free_rx;
	}

	st->indio_dev->dev.parent = &spi->dev;
	st->indio_dev->num_interrupt_lines = 1;
	st->indio_dev->event_attrs = &sca3000_event_attribute_group;
	if (st->info->temp_output)
		st->indio_dev->attrs = &sca3000_attribute_group_with_temp;
	else
		st->indio_dev->attrs = &sca3000_attribute_group;
	st->indio_dev->dev_data = (void *)(st);
	st->indio_dev->modes = INDIO_DIRECT_MODE;

	sca3000_configure_ring(st->indio_dev);

	ret = iio_device_register(st->indio_dev);
	if (ret < 0)
		goto error_free_dev;
	regdone = 1;
	ret = iio_ring_buffer_register(st->indio_dev->ring);
	if (ret < 0)
		goto error_unregister_dev;
	if (spi->irq && gpio_is_valid(irq_to_gpio(spi->irq)) > 0) {
		INIT_WORK(&st->interrupt_handler_ws,
			  sca3000_interrupt_handler_bh);
		ret = iio_register_interrupt_line(spi->irq,
						  st->indio_dev,
						  0,
						  IRQF_TRIGGER_FALLING,
						  "sca3000");
		if (ret)
			goto error_unregister_ring;
		/* RFC
		 * Probably a common situation.  All interrupts need an ack
		 * and there is only one handler so the complicated list system
		 * is overkill.  At very least a simpler registration method
		 * might be worthwhile.
		 */
		iio_add_event_to_list(iio_event_attr_accel_z_high.listel,
					    &st->indio_dev
					    ->interrupts[0]->ev_list);
	}
	sca3000_register_ring_funcs(st->indio_dev);
	ret = sca3000_clean_setup(st);
	if (ret)
		goto error_unregister_interrupt_line;
	return 0;

error_unregister_interrupt_line:
	if (spi->irq && gpio_is_valid(irq_to_gpio(spi->irq)) > 0)
		iio_unregister_interrupt_line(st->indio_dev, 0);
error_unregister_ring:
	iio_ring_buffer_unregister(st->indio_dev->ring);
error_unregister_dev:
error_free_dev:
	if (regdone)
		iio_device_unregister(st->indio_dev);
	else
		iio_free_device(st->indio_dev);
error_free_rx:
	kfree(st->rx);
error_free_tx:
	kfree(st->tx);
error_clear_st:
	kfree(st);
error_ret:
	return ret;
}

static int sca3000_stop_all_interrupts(struct sca3000_state *st)
{
	int ret;
	u8 *rx;

	mutex_lock(&st->lock);
	ret = sca3000_read_data(st, SCA3000_REG_ADDR_INT_MASK, &rx, 1);
	if (ret)
		goto error_ret;
	ret = sca3000_write_reg(st, SCA3000_REG_ADDR_INT_MASK,
				(rx[1] & ~(SCA3000_INT_MASK_RING_THREE_QUARTER
					   | SCA3000_INT_MASK_RING_HALF
					   | SCA3000_INT_MASK_ALL_INTS)));
error_ret:
	kfree(rx);
	return ret;

}

static int sca3000_remove(struct spi_device *spi)
{
	struct sca3000_state *st =  spi_get_drvdata(spi);
	struct iio_dev *indio_dev = st->indio_dev;
	int ret;
	/* Must ensure no interrupts can be generated after this!*/
	ret = sca3000_stop_all_interrupts(st);
	if (ret)
		return ret;
	if (spi->irq && gpio_is_valid(irq_to_gpio(spi->irq)) > 0)
		iio_unregister_interrupt_line(indio_dev, 0);
	iio_ring_buffer_unregister(indio_dev->ring);
	sca3000_unconfigure_ring(indio_dev);
	iio_device_unregister(indio_dev);

	kfree(st->tx);
	kfree(st->rx);
	kfree(st);

	return 0;
}

/* These macros save on an awful lot of repeated code */
#define SCA3000_VARIANT_PROBE(_name)				\
	static int __devinit					\
	sca3000_##_name##_probe(struct spi_device *spi)		\
	{							\
		return __sca3000_probe(spi, _name);		\
	}

#define SCA3000_VARIANT_SPI_DRIVER(_name)			\
	struct spi_driver sca3000_##_name##_driver = {		\
		.driver = {					\
			.name = "sca3000_" #_name,		\
			.owner = THIS_MODULE,			\
		},						\
		.probe = sca3000_##_name##_probe,		\
		.remove = __devexit_p(sca3000_remove),		\
	}

SCA3000_VARIANT_PROBE(d01);
static SCA3000_VARIANT_SPI_DRIVER(d01);

SCA3000_VARIANT_PROBE(d03);
static SCA3000_VARIANT_SPI_DRIVER(d03);

SCA3000_VARIANT_PROBE(e02);
static SCA3000_VARIANT_SPI_DRIVER(e02);

SCA3000_VARIANT_PROBE(e04);
static SCA3000_VARIANT_SPI_DRIVER(e04);

SCA3000_VARIANT_PROBE(e05);
static SCA3000_VARIANT_SPI_DRIVER(e05);

SCA3000_VARIANT_PROBE(l01);
static SCA3000_VARIANT_SPI_DRIVER(l01);

static __init int sca3000_init(void)
{
	int ret;

	ret = spi_register_driver(&sca3000_d01_driver);
	if (ret)
		goto error_ret;
	ret = spi_register_driver(&sca3000_d03_driver);
	if (ret)
		goto error_unreg_d01;
	ret = spi_register_driver(&sca3000_e02_driver);
	if (ret)
		goto error_unreg_d03;
	ret = spi_register_driver(&sca3000_e04_driver);
	if (ret)
		goto error_unreg_e02;
	ret = spi_register_driver(&sca3000_e05_driver);
	if (ret)
		goto error_unreg_e04;
	ret = spi_register_driver(&sca3000_l01_driver);
	if (ret)
		goto error_unreg_e05;

	return 0;

error_unreg_e05:
	spi_unregister_driver(&sca3000_e05_driver);
error_unreg_e04:
	spi_unregister_driver(&sca3000_e04_driver);
error_unreg_e02:
	spi_unregister_driver(&sca3000_e02_driver);
error_unreg_d03:
	spi_unregister_driver(&sca3000_d03_driver);
error_unreg_d01:
	spi_unregister_driver(&sca3000_d01_driver);
error_ret:

	return ret;
}

static __exit void sca3000_exit(void)
{
	spi_unregister_driver(&sca3000_l01_driver);
	spi_unregister_driver(&sca3000_e05_driver);
	spi_unregister_driver(&sca3000_e04_driver);
	spi_unregister_driver(&sca3000_e02_driver);
	spi_unregister_driver(&sca3000_d03_driver);
	spi_unregister_driver(&sca3000_d01_driver);
}

module_init(sca3000_init);
module_exit(sca3000_exit);

MODULE_AUTHOR("Jonathan Cameron <jic23@cam.ac.uk>");
MODULE_DESCRIPTION("VTI SCA3000 Series Accelerometers SPI driver");
MODULE_LICENSE("GPL v2");
