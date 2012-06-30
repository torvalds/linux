/*
 * adis16400.c	support Analog Devices ADIS16400/5
 *		3d 2g Linear Accelerometers,
 *		3d Gyroscopes,
 *		3d Magnetometers via SPI
 *
 * Copyright (c) 2009 Manuel Stahl <manuel.stahl@iis.fraunhofer.de>
 * Copyright (c) 2007 Jonathan Cameron <jic23@cam.ac.uk>
 * Copyright (c) 2011 Analog Devices Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/list.h>
#include <linux/module.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include "adis16400.h"

enum adis16400_chip_variant {
	ADIS16300,
	ADIS16334,
	ADIS16350,
	ADIS16360,
	ADIS16362,
	ADIS16364,
	ADIS16365,
	ADIS16400,
};

/**
 * adis16400_spi_write_reg_8() - write single byte to a register
 * @dev: device associated with child of actual device (iio_dev or iio_trig)
 * @reg_address: the address of the register to be written
 * @val: the value to write
 */
static int adis16400_spi_write_reg_8(struct iio_dev *indio_dev,
				     u8 reg_address,
				     u8 val)
{
	int ret;
	struct adis16400_state *st = iio_priv(indio_dev);

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
 *
 * At the moment the spi framework doesn't allow global setting of cs_change.
 * This means that use cannot be made of spi_write.
 */
static int adis16400_spi_write_reg_16(struct iio_dev *indio_dev,
		u8 lower_reg_address,
		u16 value)
{
	int ret;
	struct spi_message msg;
	struct adis16400_state *st = iio_priv(indio_dev);
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
 * @indio_dev: iio device
 * @reg_address: the address of the lower of the two registers. Second register
 *               is assumed to have address one greater.
 * @val: somewhere to pass back the value read
 *
 * At the moment the spi framework doesn't allow global setting of cs_change.
 * This means that use cannot be made of spi_read.
 **/
static int adis16400_spi_read_reg_16(struct iio_dev *indio_dev,
		u8 lower_reg_address,
		u16 *val)
{
	struct spi_message msg;
	struct adis16400_state *st = iio_priv(indio_dev);
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
		},
	};

	mutex_lock(&st->buf_lock);
	st->tx[0] = ADIS16400_READ_REG(lower_reg_address);
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

static int adis16400_get_freq(struct iio_dev *indio_dev)
{
	u16 t;
	int sps, ret;

	ret = adis16400_spi_read_reg_16(indio_dev, ADIS16400_SMPL_PRD, &t);
	if (ret < 0)
		return ret;
	sps =  (t & ADIS16400_SMPL_PRD_TIME_BASE) ? 53 : 1638;
	sps /= (t & ADIS16400_SMPL_PRD_DIV_MASK) + 1;

	return sps;
}

static ssize_t adis16400_read_frequency(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	int ret, len = 0;
	ret = adis16400_get_freq(indio_dev);
	if (ret < 0)
		return ret;
	len = sprintf(buf, "%d SPS\n", ret);
	return len;
}

static const unsigned adis16400_3db_divisors[] = {
	[0] = 2, /* Special case */
	[1] = 5,
	[2] = 10,
	[3] = 50,
	[4] = 200,
};

static int adis16400_set_filter(struct iio_dev *indio_dev, int sps, int val)
{
	int i, ret;
	u16 val16;
	for (i = ARRAY_SIZE(adis16400_3db_divisors) - 1; i >= 0; i--)
		if (sps/adis16400_3db_divisors[i] > val)
			break;
	if (i == -1)
		ret = -EINVAL;
	else {
		ret = adis16400_spi_read_reg_16(indio_dev,
						ADIS16400_SENS_AVG,
						&val16);
		if (ret < 0)
			goto error_ret;

		ret = adis16400_spi_write_reg_16(indio_dev,
						 ADIS16400_SENS_AVG,
						 (val16 & ~0x03) | i);
	}
error_ret:
	return ret;
}

static ssize_t adis16400_write_frequency(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct adis16400_state *st = iio_priv(indio_dev);
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

	ret = adis16400_spi_write_reg_8(indio_dev,
			ADIS16400_SMPL_PRD,
			t);

	/* Also update the filter */
	mutex_unlock(&indio_dev->mlock);

	return ret ? ret : len;
}

static int adis16400_reset(struct iio_dev *indio_dev)
{
	int ret;
	ret = adis16400_spi_write_reg_8(indio_dev,
			ADIS16400_GLOB_CMD,
			ADIS16400_GLOB_CMD_SW_RESET);
	if (ret)
		dev_err(&indio_dev->dev, "problem resetting device");

	return ret;
}

static ssize_t adis16400_write_reset(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t len)
{
	bool val;
	int ret;

	ret = strtobool(buf, &val);
	if (ret < 0)
		return ret;
	if (val) {
		ret = adis16400_reset(dev_to_iio_dev(dev));
		if (ret < 0)
			return ret;
	}

	return len;
}

int adis16400_set_irq(struct iio_dev *indio_dev, bool enable)
{
	int ret;
	u16 msc;

	ret = adis16400_spi_read_reg_16(indio_dev, ADIS16400_MSC_CTRL, &msc);
	if (ret)
		goto error_ret;

	msc |= ADIS16400_MSC_CTRL_DATA_RDY_POL_HIGH;
	if (enable)
		msc |= ADIS16400_MSC_CTRL_DATA_RDY_EN;
	else
		msc &= ~ADIS16400_MSC_CTRL_DATA_RDY_EN;

	ret = adis16400_spi_write_reg_16(indio_dev, ADIS16400_MSC_CTRL, msc);
	if (ret)
		goto error_ret;

error_ret:
	return ret;
}

/* Power down the device */
static int adis16400_stop_device(struct iio_dev *indio_dev)
{
	int ret;
	u16 val = ADIS16400_SLP_CNT_POWER_OFF;

	ret = adis16400_spi_write_reg_16(indio_dev, ADIS16400_SLP_CNT, val);
	if (ret)
		dev_err(&indio_dev->dev,
			"problem with turning device off: SLP_CNT");

	return ret;
}

static int adis16400_check_status(struct iio_dev *indio_dev)
{
	u16 status;
	int ret;
	struct device *dev = &indio_dev->dev;

	ret = adis16400_spi_read_reg_16(indio_dev,
					ADIS16400_DIAG_STAT, &status);

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

static int adis16400_self_test(struct iio_dev *indio_dev)
{
	int ret;
	ret = adis16400_spi_write_reg_16(indio_dev,
			ADIS16400_MSC_CTRL,
			ADIS16400_MSC_CTRL_MEM_TEST);
	if (ret) {
		dev_err(&indio_dev->dev, "problem starting self test");
		goto err_ret;
	}

	msleep(ADIS16400_MTEST_DELAY);
	adis16400_check_status(indio_dev);

err_ret:
	return ret;
}

static int adis16400_initial_setup(struct iio_dev *indio_dev)
{
	int ret;
	u16 prod_id, smp_prd;
	struct adis16400_state *st = iio_priv(indio_dev);

	/* use low spi speed for init */
	st->us->max_speed_hz = ADIS16400_SPI_SLOW;
	st->us->mode = SPI_MODE_3;
	spi_setup(st->us);

	ret = adis16400_set_irq(indio_dev, false);
	if (ret) {
		dev_err(&indio_dev->dev, "disable irq failed");
		goto err_ret;
	}

	ret = adis16400_self_test(indio_dev);
	if (ret) {
		dev_err(&indio_dev->dev, "self test failure");
		goto err_ret;
	}

	ret = adis16400_check_status(indio_dev);
	if (ret) {
		adis16400_reset(indio_dev);
		dev_err(&indio_dev->dev, "device not playing ball -> reset");
		msleep(ADIS16400_STARTUP_DELAY);
		ret = adis16400_check_status(indio_dev);
		if (ret) {
			dev_err(&indio_dev->dev, "giving up");
			goto err_ret;
		}
	}
	if (st->variant->flags & ADIS16400_HAS_PROD_ID) {
		ret = adis16400_spi_read_reg_16(indio_dev,
						ADIS16400_PRODUCT_ID, &prod_id);
		if (ret)
			goto err_ret;

		if ((prod_id & 0xF000) != st->variant->product_id)
			dev_warn(&indio_dev->dev, "incorrect id");

		dev_info(&indio_dev->dev, "%s: prod_id 0x%04x at CS%d (irq %d)\n",
		       indio_dev->name, prod_id,
		       st->us->chip_select, st->us->irq);
	}
	/* use high spi speed if possible */
	ret = adis16400_spi_read_reg_16(indio_dev,
					ADIS16400_SMPL_PRD, &smp_prd);
	if (!ret && (smp_prd & ADIS16400_SMPL_PRD_DIV_MASK) < 0x0A) {
		st->us->max_speed_hz = ADIS16400_SPI_SLOW;
		spi_setup(st->us);
	}

err_ret:
	return ret;
}

static IIO_DEV_ATTR_SAMP_FREQ(S_IWUSR | S_IRUGO,
			      adis16400_read_frequency,
			      adis16400_write_frequency);

static IIO_DEVICE_ATTR(reset, S_IWUSR, NULL, adis16400_write_reset, 0);

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("409 546 819 1638");

enum adis16400_chan {
	in_supply,
	gyro_x,
	gyro_y,
	gyro_z,
	accel_x,
	accel_y,
	accel_z,
	magn_x,
	magn_y,
	magn_z,
	temp,
	temp0, temp1, temp2,
	in1,
	incli_x,
	incli_y,
};

static u8 adis16400_addresses[17][2] = {
	[in_supply] = { ADIS16400_SUPPLY_OUT },
	[gyro_x] = { ADIS16400_XGYRO_OUT, ADIS16400_XGYRO_OFF },
	[gyro_y] = { ADIS16400_YGYRO_OUT, ADIS16400_YGYRO_OFF },
	[gyro_z] = { ADIS16400_ZGYRO_OUT, ADIS16400_ZGYRO_OFF },
	[accel_x] = { ADIS16400_XACCL_OUT, ADIS16400_XACCL_OFF },
	[accel_y] = { ADIS16400_YACCL_OUT, ADIS16400_YACCL_OFF },
	[accel_z] = { ADIS16400_ZACCL_OUT, ADIS16400_ZACCL_OFF },
	[magn_x] = { ADIS16400_XMAGN_OUT },
	[magn_y] = { ADIS16400_YMAGN_OUT },
	[magn_z] = { ADIS16400_ZMAGN_OUT },
	[temp] = { ADIS16400_TEMP_OUT },
	[temp0] = { ADIS16350_XTEMP_OUT },
	[temp1] = { ADIS16350_YTEMP_OUT },
	[temp2] = { ADIS16350_ZTEMP_OUT },
	[in1] = { ADIS16400_AUX_ADC },
	[incli_x] = { ADIS16300_PITCH_OUT },
	[incli_y] = { ADIS16300_ROLL_OUT }
};


static int adis16400_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val,
			       int val2,
			       long mask)
{
	struct adis16400_state *st = iio_priv(indio_dev);
	int ret, sps;

	switch (mask) {
	case IIO_CHAN_INFO_CALIBBIAS:
		mutex_lock(&indio_dev->mlock);
		ret = adis16400_spi_write_reg_16(indio_dev,
				adis16400_addresses[chan->address][1],
				val);
		mutex_unlock(&indio_dev->mlock);
		return ret;
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		/* Need to cache values so we can update if the frequency
		   changes */
		mutex_lock(&indio_dev->mlock);
		st->filt_int = val;
		/* Work out update to current value */
		sps = adis16400_get_freq(indio_dev);
		if (sps < 0) {
			mutex_unlock(&indio_dev->mlock);
			return sps;
		}

		ret = adis16400_set_filter(indio_dev, sps, val);
		mutex_unlock(&indio_dev->mlock);
		return ret;
	default:
		return -EINVAL;
	}
}

static int adis16400_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val,
			      int *val2,
			      long mask)
{
	struct adis16400_state *st = iio_priv(indio_dev);
	int ret, shift;
	s16 val16;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&indio_dev->mlock);
		ret = adis16400_spi_read_reg_16(indio_dev,
				adis16400_addresses[chan->address][0],
				&val16);
		if (ret) {
			mutex_unlock(&indio_dev->mlock);
			return ret;
		}
		val16 &= (1 << chan->scan_type.realbits) - 1;
		if (chan->scan_type.sign == 's') {
			shift = 16 - chan->scan_type.realbits;
			val16 = (s16)(val16 << shift) >> shift;
		}
		*val = val16;
		mutex_unlock(&indio_dev->mlock);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			*val = 0;
			*val2 = st->variant->gyro_scale_micro;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_VOLTAGE:
			*val = 0;
			if (chan->channel == 0)
				*val2 = 2418;
			else
				*val2 = 806;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_ACCEL:
			*val = 0;
			*val2 = st->variant->accel_scale_micro;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_MAGN:
			*val = 0;
			*val2 = 500;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_TEMP:
			*val = 0;
			*val2 = 140000;
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_CALIBBIAS:
		mutex_lock(&indio_dev->mlock);
		ret = adis16400_spi_read_reg_16(indio_dev,
				adis16400_addresses[chan->address][1],
				&val16);
		mutex_unlock(&indio_dev->mlock);
		if (ret)
			return ret;
		val16 = ((val16 & 0xFFF) << 4) >> 4;
		*val = val16;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_OFFSET:
		/* currently only temperature */
		*val = 198;
		*val2 = 160000;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		mutex_lock(&indio_dev->mlock);
		/* Need both the number of taps and the sampling frequency */
		ret = adis16400_spi_read_reg_16(indio_dev,
						ADIS16400_SENS_AVG,
						&val16);
		if (ret < 0) {
			mutex_unlock(&indio_dev->mlock);
			return ret;
		}
		ret = adis16400_get_freq(indio_dev);
		if (ret > 0)
			*val = ret/adis16400_3db_divisors[val16 & 0x03];
		*val2 = 0;
		mutex_unlock(&indio_dev->mlock);
		if (ret < 0)
			return ret;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static struct iio_chan_spec adis16400_channels[] = {
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 0,
		.extend_name = "supply",
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SEPARATE_BIT,
		.address = in_supply,
		.scan_index = ADIS16400_SCAN_SUPPLY,
		.scan_type = IIO_ST('u', 14, 16, 0)
	}, {
		.type = IIO_ANGL_VEL,
		.modified = 1,
		.channel2 = IIO_MOD_X,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SHARED_BIT |
		IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY_SHARED_BIT,
		.address = gyro_x,
		.scan_index = ADIS16400_SCAN_GYRO_X,
		.scan_type = IIO_ST('s', 14, 16, 0)
	}, {
		.type = IIO_ANGL_VEL,
		.modified = 1,
		.channel2 = IIO_MOD_Y,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SHARED_BIT |
		IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY_SHARED_BIT,
		.address = gyro_y,
		.scan_index = ADIS16400_SCAN_GYRO_Y,
		.scan_type = IIO_ST('s', 14, 16, 0),
	}, {
		.type = IIO_ANGL_VEL,
		.modified = 1,
		.channel2 = IIO_MOD_Z,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SHARED_BIT |
		IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY_SHARED_BIT,
		.address = gyro_z,
		.scan_index = ADIS16400_SCAN_GYRO_Z,
		.scan_type = IIO_ST('s', 14, 16, 0),
	}, {
		.type = IIO_ACCEL,
		.modified = 1,
		.channel2 = IIO_MOD_X,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SHARED_BIT |
		IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY_SHARED_BIT,
		.address = accel_x,
		.scan_index = ADIS16400_SCAN_ACC_X,
		.scan_type = IIO_ST('s', 14, 16, 0),
	}, {
		.type = IIO_ACCEL,
		.modified = 1,
		.channel2 = IIO_MOD_Y,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SHARED_BIT |
		IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY_SHARED_BIT,
		.address = accel_y,
		.scan_index = ADIS16400_SCAN_ACC_Y,
		.scan_type = IIO_ST('s', 14, 16, 0),
	}, {
		.type = IIO_ACCEL,
		.modified = 1,
		.channel2 = IIO_MOD_Z,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SHARED_BIT |
		IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY_SHARED_BIT,
		.address = accel_z,
		.scan_index = ADIS16400_SCAN_ACC_Z,
		.scan_type = IIO_ST('s', 14, 16, 0),
	}, {
		.type = IIO_MAGN,
		.modified = 1,
		.channel2 = IIO_MOD_X,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SHARED_BIT |
		IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY_SHARED_BIT,
		.address = magn_x,
		.scan_index = ADIS16400_SCAN_MAGN_X,
		.scan_type = IIO_ST('s', 14, 16, 0),
	}, {
		.type = IIO_MAGN,
		.modified = 1,
		.channel2 = IIO_MOD_Y,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SHARED_BIT |
		IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY_SHARED_BIT,
		.address = magn_y,
		.scan_index = ADIS16400_SCAN_MAGN_Y,
		.scan_type = IIO_ST('s', 14, 16, 0),
	}, {
		.type = IIO_MAGN,
		.modified = 1,
		.channel2 = IIO_MOD_Z,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SHARED_BIT |
		IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY_SHARED_BIT,
		.address = magn_z,
		.scan_index = ADIS16400_SCAN_MAGN_Z,
		.scan_type = IIO_ST('s', 14, 16, 0),
	}, {
		.type = IIO_TEMP,
		.indexed = 1,
		.channel = 0,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_OFFSET_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SEPARATE_BIT,
		.address = temp,
		.scan_index = ADIS16400_SCAN_TEMP,
		.scan_type = IIO_ST('s', 12, 16, 0),
	}, {
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 1,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SEPARATE_BIT,
		.address = in1,
		.scan_index = ADIS16400_SCAN_ADC_0,
		.scan_type = IIO_ST('s', 12, 16, 0),
	},
	IIO_CHAN_SOFT_TIMESTAMP(12)
};

static struct iio_chan_spec adis16350_channels[] = {
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 0,
		.extend_name = "supply",
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SEPARATE_BIT,
		.address = in_supply,
		.scan_index = ADIS16400_SCAN_SUPPLY,
		.scan_type = IIO_ST('u', 12, 16, 0)
	}, {
		.type = IIO_ANGL_VEL,
		.modified = 1,
		.channel2 = IIO_MOD_X,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SHARED_BIT |
		IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY_SHARED_BIT,
		.address = gyro_x,
		.scan_index = ADIS16400_SCAN_GYRO_X,
		.scan_type = IIO_ST('s', 14, 16, 0)
	}, {
		.type = IIO_ANGL_VEL,
		.modified = 1,
		.channel2 = IIO_MOD_Y,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SHARED_BIT |
		IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY_SHARED_BIT,
		.address = gyro_y,
		.scan_index = ADIS16400_SCAN_GYRO_Y,
		.scan_type = IIO_ST('s', 14, 16, 0),
	}, {
		.type = IIO_ANGL_VEL,
		.modified = 1,
		.channel2 = IIO_MOD_Z,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SHARED_BIT |
		IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY_SHARED_BIT,
		.address = gyro_z,
		.scan_index = ADIS16400_SCAN_GYRO_Z,
		.scan_type = IIO_ST('s', 14, 16, 0),
	}, {
		.type = IIO_ACCEL,
		.modified = 1,
		.channel2 = IIO_MOD_X,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SHARED_BIT |
		IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY_SHARED_BIT,
		.address = accel_x,
		.scan_index = ADIS16400_SCAN_ACC_X,
		.scan_type = IIO_ST('s', 14, 16, 0),
	}, {
		.type = IIO_ACCEL,
		.modified = 1,
		.channel2 = IIO_MOD_Y,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SHARED_BIT |
		IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY_SHARED_BIT,
		.address = accel_y,
		.scan_index = ADIS16400_SCAN_ACC_Y,
		.scan_type = IIO_ST('s', 14, 16, 0),
	}, {
		.type = IIO_ACCEL,
		.modified = 1,
		.channel2 = IIO_MOD_Z,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SHARED_BIT |
		IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY_SHARED_BIT,
		.address = accel_z,
		.scan_index = ADIS16400_SCAN_ACC_Z,
		.scan_type = IIO_ST('s', 14, 16, 0),
	}, {
		.type = IIO_TEMP,
		.indexed = 1,
		.channel = 0,
		.extend_name = "x",
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_OFFSET_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SEPARATE_BIT |
		IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY_SHARED_BIT,
		.address = temp0,
		.scan_index = ADIS16350_SCAN_TEMP_X,
		.scan_type = IIO_ST('s', 12, 16, 0),
	}, {
		.type = IIO_TEMP,
		.indexed = 1,
		.channel = 1,
		.extend_name = "y",
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_OFFSET_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SEPARATE_BIT |
		IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY_SHARED_BIT,
		.address = temp1,
		.scan_index = ADIS16350_SCAN_TEMP_Y,
		.scan_type = IIO_ST('s', 12, 16, 0),
	}, {
		.type = IIO_TEMP,
		.indexed = 1,
		.channel = 2,
		.extend_name = "z",
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_OFFSET_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SEPARATE_BIT,
		.address = temp2,
		.scan_index = ADIS16350_SCAN_TEMP_Z,
		.scan_type = IIO_ST('s', 12, 16, 0),
	}, {
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 1,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SEPARATE_BIT,
		.address = in1,
		.scan_index = ADIS16350_SCAN_ADC_0,
		.scan_type = IIO_ST('s', 12, 16, 0),
	},
	IIO_CHAN_SOFT_TIMESTAMP(11)
};

static struct iio_chan_spec adis16300_channels[] = {
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 0,
		.extend_name = "supply",
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SEPARATE_BIT,
		.address = in_supply,
		.scan_index = ADIS16400_SCAN_SUPPLY,
		.scan_type = IIO_ST('u', 12, 16, 0)
	}, {
		.type = IIO_ANGL_VEL,
		.modified = 1,
		.channel2 = IIO_MOD_X,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SHARED_BIT |
		IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY_SHARED_BIT,
		.address = gyro_x,
		.scan_index = ADIS16400_SCAN_GYRO_X,
		.scan_type = IIO_ST('s', 14, 16, 0),
	}, {
		.type = IIO_ACCEL,
		.modified = 1,
		.channel2 = IIO_MOD_X,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SHARED_BIT |
		IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY_SHARED_BIT,
		.address = accel_x,
		.scan_index = ADIS16400_SCAN_ACC_X,
		.scan_type = IIO_ST('s', 14, 16, 0),
	}, {
		.type = IIO_ACCEL,
		.modified = 1,
		.channel2 = IIO_MOD_Y,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SHARED_BIT |
		IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY_SHARED_BIT,
		.address = accel_y,
		.scan_index = ADIS16400_SCAN_ACC_Y,
		.scan_type = IIO_ST('s', 14, 16, 0),
	}, {
		.type = IIO_ACCEL,
		.modified = 1,
		.channel2 = IIO_MOD_Z,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SHARED_BIT |
		IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY_SHARED_BIT,
		.address = accel_z,
		.scan_index = ADIS16400_SCAN_ACC_Z,
		.scan_type = IIO_ST('s', 14, 16, 0),
	}, {
		.type = IIO_TEMP,
		.indexed = 1,
		.channel = 0,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_OFFSET_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SEPARATE_BIT,
		.address = temp,
		.scan_index = ADIS16400_SCAN_TEMP,
		.scan_type = IIO_ST('s', 12, 16, 0),
	}, {
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 1,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SEPARATE_BIT,
		.address = in1,
		.scan_index = ADIS16350_SCAN_ADC_0,
		.scan_type = IIO_ST('s', 12, 16, 0),
	}, {
		.type = IIO_INCLI,
		.modified = 1,
		.channel2 = IIO_MOD_X,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SHARED_BIT,
		.address = incli_x,
		.scan_index = ADIS16300_SCAN_INCLI_X,
		.scan_type = IIO_ST('s', 13, 16, 0),
	}, {
		.type = IIO_INCLI,
		.modified = 1,
		.channel2 = IIO_MOD_Y,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SHARED_BIT,
		.address = incli_y,
		.scan_index = ADIS16300_SCAN_INCLI_Y,
		.scan_type = IIO_ST('s', 13, 16, 0),
	},
	IIO_CHAN_SOFT_TIMESTAMP(14)
};

static const struct iio_chan_spec adis16334_channels[] = {
	{
		.type = IIO_ANGL_VEL,
		.modified = 1,
		.channel2 = IIO_MOD_X,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SHARED_BIT |
		IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY_SHARED_BIT,
		.address = gyro_x,
		.scan_index = ADIS16400_SCAN_GYRO_X,
		.scan_type = IIO_ST('s', 14, 16, 0),
	}, {
		.type = IIO_ANGL_VEL,
		.modified = 1,
		.channel2 = IIO_MOD_Y,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SHARED_BIT |
		IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY_SHARED_BIT,
		.address = gyro_y,
		.scan_index = ADIS16400_SCAN_GYRO_Y,
		.scan_type = IIO_ST('s', 14, 16, 0),
	}, {
		.type = IIO_ANGL_VEL,
		.modified = 1,
		.channel2 = IIO_MOD_Z,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SHARED_BIT |
		IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY_SHARED_BIT,
		.address = gyro_z,
		.scan_index = ADIS16400_SCAN_GYRO_Z,
		.scan_type = IIO_ST('s', 14, 16, 0),
	}, {
		.type = IIO_ACCEL,
		.modified = 1,
		.channel2 = IIO_MOD_X,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SHARED_BIT |
		IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY_SHARED_BIT,
		.address = accel_x,
		.scan_index = ADIS16400_SCAN_ACC_X,
		.scan_type = IIO_ST('s', 14, 16, 0),
	}, {
		.type = IIO_ACCEL,
		.modified = 1,
		.channel2 = IIO_MOD_Y,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SHARED_BIT |
		IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY_SHARED_BIT,
		.address = accel_y,
		.scan_index = ADIS16400_SCAN_ACC_Y,
		.scan_type = IIO_ST('s', 14, 16, 0),
	}, {
		.type = IIO_ACCEL,
		.modified = 1,
		.channel2 = IIO_MOD_Z,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SHARED_BIT |
		IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY_SHARED_BIT,
		.address = accel_z,
		.scan_index = ADIS16400_SCAN_ACC_Z,
		.scan_type = IIO_ST('s', 14, 16, 0),
	}, {
		.type = IIO_TEMP,
		.indexed = 1,
		.channel = 0,
		.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT |
		IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT |
		IIO_CHAN_INFO_SCALE_SHARED_BIT,
		.address = accel_z,
		.scan_index = ADIS16400_SCAN_ACC_Z,
		.scan_type = IIO_ST('s', 14, 16, 0),
	},
	IIO_CHAN_SOFT_TIMESTAMP(12)
};

static struct attribute *adis16400_attributes[] = {
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_reset.dev_attr.attr,
	NULL
};

static const struct attribute_group adis16400_attribute_group = {
	.attrs = adis16400_attributes,
};

static struct adis16400_chip_info adis16400_chips[] = {
	[ADIS16300] = {
		.channels = adis16300_channels,
		.num_channels = ARRAY_SIZE(adis16300_channels),
		.gyro_scale_micro = 873,
		.accel_scale_micro = 5884,
		.default_scan_mask = (1 << ADIS16400_SCAN_SUPPLY) |
		(1 << ADIS16400_SCAN_GYRO_X) | (1 << ADIS16400_SCAN_ACC_X) |
		(1 << ADIS16400_SCAN_ACC_Y) | (1 << ADIS16400_SCAN_ACC_Z) |
		(1 << ADIS16400_SCAN_TEMP) | (1 << ADIS16400_SCAN_ADC_0) |
		(1 << ADIS16300_SCAN_INCLI_X) | (1 << ADIS16300_SCAN_INCLI_Y) |
		(1 << 14),
	},
	[ADIS16334] = {
		.channels = adis16334_channels,
		.num_channels = ARRAY_SIZE(adis16334_channels),
		.gyro_scale_micro = 873,
		.accel_scale_micro = 981,
		.default_scan_mask = (1 << ADIS16400_SCAN_GYRO_X) |
		(1 << ADIS16400_SCAN_GYRO_Y) | (1 << ADIS16400_SCAN_GYRO_Z) |
		(1 << ADIS16400_SCAN_ACC_X) | (1 << ADIS16400_SCAN_ACC_Y) |
		(1 << ADIS16400_SCAN_ACC_Z),
	},
	[ADIS16350] = {
		.channels = adis16350_channels,
		.num_channels = ARRAY_SIZE(adis16350_channels),
		.gyro_scale_micro = 872664,
		.accel_scale_micro = 24732,
		.default_scan_mask = 0x7FF,
		.flags = ADIS16400_NO_BURST,
	},
	[ADIS16360] = {
		.channels = adis16350_channels,
		.num_channels = ARRAY_SIZE(adis16350_channels),
		.flags = ADIS16400_HAS_PROD_ID,
		.product_id = 0x3FE8,
		.gyro_scale_micro = 1279,
		.accel_scale_micro = 24732,
		.default_scan_mask = 0x7FF,
	},
	[ADIS16362] = {
		.channels = adis16350_channels,
		.num_channels = ARRAY_SIZE(adis16350_channels),
		.flags = ADIS16400_HAS_PROD_ID,
		.product_id = 0x3FEA,
		.gyro_scale_micro = 1279,
		.accel_scale_micro = 24732,
		.default_scan_mask = 0x7FF,
	},
	[ADIS16364] = {
		.channels = adis16350_channels,
		.num_channels = ARRAY_SIZE(adis16350_channels),
		.flags = ADIS16400_HAS_PROD_ID,
		.product_id = 0x3FEC,
		.gyro_scale_micro = 1279,
		.accel_scale_micro = 24732,
		.default_scan_mask = 0x7FF,
	},
	[ADIS16365] = {
		.channels = adis16350_channels,
		.num_channels = ARRAY_SIZE(adis16350_channels),
		.flags = ADIS16400_HAS_PROD_ID,
		.product_id = 0x3FED,
		.gyro_scale_micro = 1279,
		.accel_scale_micro = 24732,
		.default_scan_mask = 0x7FF,
	},
	[ADIS16400] = {
		.channels = adis16400_channels,
		.num_channels = ARRAY_SIZE(adis16400_channels),
		.flags = ADIS16400_HAS_PROD_ID,
		.product_id = 0x4015,
		.gyro_scale_micro = 873,
		.accel_scale_micro = 32656,
		.default_scan_mask = 0xFFF,
	}
};

static const struct iio_info adis16400_info = {
	.driver_module = THIS_MODULE,
	.read_raw = &adis16400_read_raw,
	.write_raw = &adis16400_write_raw,
	.attrs = &adis16400_attribute_group,
};

static int __devinit adis16400_probe(struct spi_device *spi)
{
	int ret;
	struct adis16400_state *st;
	struct iio_dev *indio_dev = iio_device_alloc(sizeof(*st));
	if (indio_dev == NULL) {
		ret =  -ENOMEM;
		goto error_ret;
	}
	st = iio_priv(indio_dev);
	/* this is only used for removal purposes */
	spi_set_drvdata(spi, indio_dev);

	st->us = spi;
	mutex_init(&st->buf_lock);

	/* setup the industrialio driver allocated elements */
	st->variant = &adis16400_chips[spi_get_device_id(spi)->driver_data];
	indio_dev->dev.parent = &spi->dev;
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->channels = st->variant->channels;
	indio_dev->num_channels = st->variant->num_channels;
	indio_dev->info = &adis16400_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = adis16400_configure_ring(indio_dev);
	if (ret)
		goto error_free_dev;

	ret = iio_buffer_register(indio_dev,
				  st->variant->channels,
				  st->variant->num_channels);
	if (ret) {
		dev_err(&spi->dev, "failed to initialize the ring\n");
		goto error_unreg_ring_funcs;
	}

	if (spi->irq) {
		ret = adis16400_probe_trigger(indio_dev);
		if (ret)
			goto error_uninitialize_ring;
	}

	/* Get the device into a sane initial state */
	ret = adis16400_initial_setup(indio_dev);
	if (ret)
		goto error_remove_trigger;
	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_remove_trigger;

	return 0;

error_remove_trigger:
	if (spi->irq)
		adis16400_remove_trigger(indio_dev);
error_uninitialize_ring:
	iio_buffer_unregister(indio_dev);
error_unreg_ring_funcs:
	adis16400_unconfigure_ring(indio_dev);
error_free_dev:
	iio_device_free(indio_dev);
error_ret:
	return ret;
}

/* fixme, confirm ordering in this function */
static int adis16400_remove(struct spi_device *spi)
{
	int ret;
	struct iio_dev *indio_dev =  spi_get_drvdata(spi);

	iio_device_unregister(indio_dev);
	ret = adis16400_stop_device(indio_dev);
	if (ret)
		goto err_ret;

	adis16400_remove_trigger(indio_dev);
	iio_buffer_unregister(indio_dev);
	adis16400_unconfigure_ring(indio_dev);
	iio_device_free(indio_dev);

	return 0;

err_ret:
	return ret;
}

static const struct spi_device_id adis16400_id[] = {
	{"adis16300", ADIS16300},
	{"adis16334", ADIS16334},
	{"adis16350", ADIS16350},
	{"adis16354", ADIS16350},
	{"adis16355", ADIS16350},
	{"adis16360", ADIS16360},
	{"adis16362", ADIS16362},
	{"adis16364", ADIS16364},
	{"adis16365", ADIS16365},
	{"adis16400", ADIS16400},
	{"adis16405", ADIS16400},
	{}
};
MODULE_DEVICE_TABLE(spi, adis16400_id);

static struct spi_driver adis16400_driver = {
	.driver = {
		.name = "adis16400",
		.owner = THIS_MODULE,
	},
	.id_table = adis16400_id,
	.probe = adis16400_probe,
	.remove = __devexit_p(adis16400_remove),
};
module_spi_driver(adis16400_driver);

MODULE_AUTHOR("Manuel Stahl <manuel.stahl@iis.fraunhofer.de>");
MODULE_DESCRIPTION("Analog Devices ADIS16400/5 IMU SPI driver");
MODULE_LICENSE("GPL v2");
