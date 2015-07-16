/*
 * adis16400.c	support Analog Devices ADIS16400/5
 *		3d 2g Linear Accelerometers,
 *		3d Gyroscopes,
 *		3d Magnetometers via SPI
 *
 * Copyright (c) 2009 Manuel Stahl <manuel.stahl@iis.fraunhofer.de>
 * Copyright (c) 2007 Jonathan Cameron <jic23@kernel.org>
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
#include <linux/debugfs.h>
#include <linux/bitops.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>

#include "adis16400.h"

#ifdef CONFIG_DEBUG_FS

static ssize_t adis16400_show_serial_number(struct file *file,
		char __user *userbuf, size_t count, loff_t *ppos)
{
	struct adis16400_state *st = file->private_data;
	u16 lot1, lot2, serial_number;
	char buf[16];
	size_t len;
	int ret;

	ret = adis_read_reg_16(&st->adis, ADIS16334_LOT_ID1, &lot1);
	if (ret < 0)
		return ret;

	ret = adis_read_reg_16(&st->adis, ADIS16334_LOT_ID2, &lot2);
	if (ret < 0)
		return ret;

	ret = adis_read_reg_16(&st->adis, ADIS16334_SERIAL_NUMBER,
			&serial_number);
	if (ret < 0)
		return ret;

	len = snprintf(buf, sizeof(buf), "%.4x-%.4x-%.4x\n", lot1, lot2,
			serial_number);

	return simple_read_from_buffer(userbuf, count, ppos, buf, len);
}

static const struct file_operations adis16400_serial_number_fops = {
	.open = simple_open,
	.read = adis16400_show_serial_number,
	.llseek = default_llseek,
	.owner = THIS_MODULE,
};

static int adis16400_show_product_id(void *arg, u64 *val)
{
	struct adis16400_state *st = arg;
	uint16_t prod_id;
	int ret;

	ret = adis_read_reg_16(&st->adis, ADIS16400_PRODUCT_ID, &prod_id);
	if (ret < 0)
		return ret;

	*val = prod_id;

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(adis16400_product_id_fops,
	adis16400_show_product_id, NULL, "%lld\n");

static int adis16400_show_flash_count(void *arg, u64 *val)
{
	struct adis16400_state *st = arg;
	uint16_t flash_count;
	int ret;

	ret = adis_read_reg_16(&st->adis, ADIS16400_FLASH_CNT, &flash_count);
	if (ret < 0)
		return ret;

	*val = flash_count;

	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(adis16400_flash_count_fops,
	adis16400_show_flash_count, NULL, "%lld\n");

static int adis16400_debugfs_init(struct iio_dev *indio_dev)
{
	struct adis16400_state *st = iio_priv(indio_dev);

	if (st->variant->flags & ADIS16400_HAS_SERIAL_NUMBER)
		debugfs_create_file("serial_number", 0400,
			indio_dev->debugfs_dentry, st,
			&adis16400_serial_number_fops);
	if (st->variant->flags & ADIS16400_HAS_PROD_ID)
		debugfs_create_file("product_id", 0400,
			indio_dev->debugfs_dentry, st,
			&adis16400_product_id_fops);
	debugfs_create_file("flash_count", 0400, indio_dev->debugfs_dentry,
		st, &adis16400_flash_count_fops);

	return 0;
}

#else

static int adis16400_debugfs_init(struct iio_dev *indio_dev)
{
	return 0;
}

#endif

enum adis16400_chip_variant {
	ADIS16300,
	ADIS16334,
	ADIS16350,
	ADIS16360,
	ADIS16362,
	ADIS16364,
	ADIS16400,
	ADIS16448,
};

static int adis16334_get_freq(struct adis16400_state *st)
{
	int ret;
	uint16_t t;

	ret = adis_read_reg_16(&st->adis, ADIS16400_SMPL_PRD, &t);
	if (ret < 0)
		return ret;

	t >>= ADIS16334_RATE_DIV_SHIFT;

	return 819200 >> t;
}

static int adis16334_set_freq(struct adis16400_state *st, unsigned int freq)
{
	unsigned int t;

	if (freq < 819200)
		t = ilog2(819200 / freq);
	else
		t = 0;

	if (t > 0x31)
		t = 0x31;

	t <<= ADIS16334_RATE_DIV_SHIFT;
	t |= ADIS16334_RATE_INT_CLK;

	return adis_write_reg_16(&st->adis, ADIS16400_SMPL_PRD, t);
}

static int adis16400_get_freq(struct adis16400_state *st)
{
	int sps, ret;
	uint16_t t;

	ret = adis_read_reg_16(&st->adis, ADIS16400_SMPL_PRD, &t);
	if (ret < 0)
		return ret;

	sps = (t & ADIS16400_SMPL_PRD_TIME_BASE) ? 52851 : 1638404;
	sps /= (t & ADIS16400_SMPL_PRD_DIV_MASK) + 1;

	return sps;
}

static int adis16400_set_freq(struct adis16400_state *st, unsigned int freq)
{
	unsigned int t;
	uint8_t val = 0;

	t = 1638404 / freq;
	if (t >= 128) {
		val |= ADIS16400_SMPL_PRD_TIME_BASE;
		t = 52851 / freq;
		if (t >= 128)
			t = 127;
	} else if (t != 0) {
		t--;
	}

	val |= t;

	if (t >= 0x0A || (val & ADIS16400_SMPL_PRD_TIME_BASE))
		st->adis.spi->max_speed_hz = ADIS16400_SPI_SLOW;
	else
		st->adis.spi->max_speed_hz = ADIS16400_SPI_FAST;

	return adis_write_reg_8(&st->adis, ADIS16400_SMPL_PRD, val);
}

static const unsigned adis16400_3db_divisors[] = {
	[0] = 2, /* Special case */
	[1] = 6,
	[2] = 12,
	[3] = 25,
	[4] = 50,
	[5] = 100,
	[6] = 200,
	[7] = 200, /* Not a valid setting */
};

static int adis16400_set_filter(struct iio_dev *indio_dev, int sps, int val)
{
	struct adis16400_state *st = iio_priv(indio_dev);
	uint16_t val16;
	int i, ret;

	for (i = ARRAY_SIZE(adis16400_3db_divisors) - 1; i >= 1; i--) {
		if (sps / adis16400_3db_divisors[i] >= val)
			break;
	}

	ret = adis_read_reg_16(&st->adis, ADIS16400_SENS_AVG, &val16);
	if (ret < 0)
		return ret;

	ret = adis_write_reg_16(&st->adis, ADIS16400_SENS_AVG,
					 (val16 & ~0x07) | i);
	return ret;
}

/* Power down the device */
static int adis16400_stop_device(struct iio_dev *indio_dev)
{
	struct adis16400_state *st = iio_priv(indio_dev);
	int ret;

	ret = adis_write_reg_16(&st->adis, ADIS16400_SLP_CNT,
			ADIS16400_SLP_CNT_POWER_OFF);
	if (ret)
		dev_err(&indio_dev->dev,
			"problem with turning device off: SLP_CNT");

	return ret;
}

static int adis16400_initial_setup(struct iio_dev *indio_dev)
{
	struct adis16400_state *st = iio_priv(indio_dev);
	uint16_t prod_id, smp_prd;
	unsigned int device_id;
	int ret;

	/* use low spi speed for init if the device has a slow mode */
	if (st->variant->flags & ADIS16400_HAS_SLOW_MODE)
		st->adis.spi->max_speed_hz = ADIS16400_SPI_SLOW;
	else
		st->adis.spi->max_speed_hz = ADIS16400_SPI_FAST;
	st->adis.spi->mode = SPI_MODE_3;
	spi_setup(st->adis.spi);

	ret = adis_initial_startup(&st->adis);
	if (ret)
		return ret;

	if (st->variant->flags & ADIS16400_HAS_PROD_ID) {
		ret = adis_read_reg_16(&st->adis,
						ADIS16400_PRODUCT_ID, &prod_id);
		if (ret)
			goto err_ret;

		sscanf(indio_dev->name, "adis%u\n", &device_id);

		if (prod_id != device_id)
			dev_warn(&indio_dev->dev, "Device ID(%u) and product ID(%u) do not match.",
					device_id, prod_id);

		dev_info(&indio_dev->dev, "%s: prod_id 0x%04x at CS%d (irq %d)\n",
			indio_dev->name, prod_id,
			st->adis.spi->chip_select, st->adis.spi->irq);
	}
	/* use high spi speed if possible */
	if (st->variant->flags & ADIS16400_HAS_SLOW_MODE) {
		ret = adis_read_reg_16(&st->adis, ADIS16400_SMPL_PRD, &smp_prd);
		if (ret)
			goto err_ret;

		if ((smp_prd & ADIS16400_SMPL_PRD_DIV_MASK) < 0x0A) {
			st->adis.spi->max_speed_hz = ADIS16400_SPI_FAST;
			spi_setup(st->adis.spi);
		}
	}

err_ret:
	return ret;
}

static const uint8_t adis16400_addresses[] = {
	[ADIS16400_SCAN_GYRO_X] = ADIS16400_XGYRO_OFF,
	[ADIS16400_SCAN_GYRO_Y] = ADIS16400_YGYRO_OFF,
	[ADIS16400_SCAN_GYRO_Z] = ADIS16400_ZGYRO_OFF,
	[ADIS16400_SCAN_ACC_X] = ADIS16400_XACCL_OFF,
	[ADIS16400_SCAN_ACC_Y] = ADIS16400_YACCL_OFF,
	[ADIS16400_SCAN_ACC_Z] = ADIS16400_ZACCL_OFF,
};

static int adis16400_write_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *chan, int val, int val2, long info)
{
	struct adis16400_state *st = iio_priv(indio_dev);
	int ret, sps;

	switch (info) {
	case IIO_CHAN_INFO_CALIBBIAS:
		mutex_lock(&indio_dev->mlock);
		ret = adis_write_reg_16(&st->adis,
				adis16400_addresses[chan->scan_index], val);
		mutex_unlock(&indio_dev->mlock);
		return ret;
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		/*
		 * Need to cache values so we can update if the frequency
		 * changes.
		 */
		mutex_lock(&indio_dev->mlock);
		st->filt_int = val;
		/* Work out update to current value */
		sps = st->variant->get_freq(st);
		if (sps < 0) {
			mutex_unlock(&indio_dev->mlock);
			return sps;
		}

		ret = adis16400_set_filter(indio_dev, sps,
			val * 1000 + val2 / 1000);
		mutex_unlock(&indio_dev->mlock);
		return ret;
	case IIO_CHAN_INFO_SAMP_FREQ:
		sps = val * 1000 + val2 / 1000;

		if (sps <= 0)
			return -EINVAL;

		mutex_lock(&indio_dev->mlock);
		ret = st->variant->set_freq(st, sps);
		mutex_unlock(&indio_dev->mlock);
		return ret;
	default:
		return -EINVAL;
	}
}

static int adis16400_read_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *chan, int *val, int *val2, long info)
{
	struct adis16400_state *st = iio_priv(indio_dev);
	int16_t val16;
	int ret;

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		return adis_single_conversion(indio_dev, chan, 0, val);
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			*val = 0;
			*val2 = st->variant->gyro_scale_micro;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_VOLTAGE:
			*val = 0;
			if (chan->channel == 0) {
				*val = 2;
				*val2 = 418000; /* 2.418 mV */
			} else {
				*val = 0;
				*val2 = 805800; /* 805.8 uV */
			}
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_ACCEL:
			*val = 0;
			*val2 = st->variant->accel_scale_micro;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_MAGN:
			*val = 0;
			*val2 = 500; /* 0.5 mgauss */
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_TEMP:
			*val = st->variant->temp_scale_nano / 1000000;
			*val2 = (st->variant->temp_scale_nano % 1000000);
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_PRESSURE:
			/* 20 uBar = 0.002kPascal */
			*val = 0;
			*val2 = 2000;
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_CALIBBIAS:
		mutex_lock(&indio_dev->mlock);
		ret = adis_read_reg_16(&st->adis,
				adis16400_addresses[chan->scan_index], &val16);
		mutex_unlock(&indio_dev->mlock);
		if (ret)
			return ret;
		val16 = sign_extend32(val16, 11);
		*val = val16;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_OFFSET:
		/* currently only temperature */
		*val = st->variant->temp_offset;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		mutex_lock(&indio_dev->mlock);
		/* Need both the number of taps and the sampling frequency */
		ret = adis_read_reg_16(&st->adis,
						ADIS16400_SENS_AVG,
						&val16);
		if (ret < 0) {
			mutex_unlock(&indio_dev->mlock);
			return ret;
		}
		ret = st->variant->get_freq(st);
		if (ret >= 0) {
			ret /= adis16400_3db_divisors[val16 & 0x07];
			*val = ret / 1000;
			*val2 = (ret % 1000) * 1000;
		}
		mutex_unlock(&indio_dev->mlock);
		if (ret < 0)
			return ret;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_SAMP_FREQ:
		ret = st->variant->get_freq(st);
		if (ret < 0)
			return ret;
		*val = ret / 1000;
		*val2 = (ret % 1000) * 1000;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

#define ADIS16400_VOLTAGE_CHAN(addr, bits, name, si, chn) { \
	.type = IIO_VOLTAGE, \
	.indexed = 1, \
	.channel = chn, \
	.extend_name = name, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
		BIT(IIO_CHAN_INFO_SCALE), \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.address = (addr), \
	.scan_index = (si), \
	.scan_type = { \
		.sign = 'u', \
		.realbits = (bits), \
		.storagebits = 16, \
		.shift = 0, \
		.endianness = IIO_BE, \
	}, \
}

#define ADIS16400_SUPPLY_CHAN(addr, bits) \
	ADIS16400_VOLTAGE_CHAN(addr, bits, "supply", ADIS16400_SCAN_SUPPLY, 0)

#define ADIS16400_AUX_ADC_CHAN(addr, bits) \
	ADIS16400_VOLTAGE_CHAN(addr, bits, NULL, ADIS16400_SCAN_ADC, 1)

#define ADIS16400_GYRO_CHAN(mod, addr, bits) { \
	.type = IIO_ANGL_VEL, \
	.modified = 1, \
	.channel2 = IIO_MOD_ ## mod, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
		BIT(IIO_CHAN_INFO_CALIBBIAS),		  \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) | \
		BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY), \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.address = addr, \
	.scan_index = ADIS16400_SCAN_GYRO_ ## mod, \
	.scan_type = { \
		.sign = 's', \
		.realbits = (bits), \
		.storagebits = 16, \
		.shift = 0, \
		.endianness = IIO_BE, \
	}, \
}

#define ADIS16400_ACCEL_CHAN(mod, addr, bits) { \
	.type = IIO_ACCEL, \
	.modified = 1, \
	.channel2 = IIO_MOD_ ## mod, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
		BIT(IIO_CHAN_INFO_CALIBBIAS), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) | \
		BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY), \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.address = (addr), \
	.scan_index = ADIS16400_SCAN_ACC_ ## mod, \
	.scan_type = { \
		.sign = 's', \
		.realbits = (bits), \
		.storagebits = 16, \
		.shift = 0, \
		.endianness = IIO_BE, \
	}, \
}

#define ADIS16400_MAGN_CHAN(mod, addr, bits) { \
	.type = IIO_MAGN, \
	.modified = 1, \
	.channel2 = IIO_MOD_ ## mod, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) | \
		BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY), \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.address = (addr), \
	.scan_index = ADIS16400_SCAN_MAGN_ ## mod, \
	.scan_type = { \
		.sign = 's', \
		.realbits = (bits), \
		.storagebits = 16, \
		.shift = 0, \
		.endianness = IIO_BE, \
	}, \
}

#define ADIS16400_MOD_TEMP_NAME_X "x"
#define ADIS16400_MOD_TEMP_NAME_Y "y"
#define ADIS16400_MOD_TEMP_NAME_Z "z"

#define ADIS16400_MOD_TEMP_CHAN(mod, addr, bits) { \
	.type = IIO_TEMP, \
	.indexed = 1, \
	.channel = 0, \
	.extend_name = ADIS16400_MOD_TEMP_NAME_ ## mod, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
		BIT(IIO_CHAN_INFO_OFFSET) | \
		BIT(IIO_CHAN_INFO_SCALE), \
	.info_mask_shared_by_type = \
		BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY), \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.address = (addr), \
	.scan_index = ADIS16350_SCAN_TEMP_ ## mod, \
	.scan_type = { \
		.sign = 's', \
		.realbits = (bits), \
		.storagebits = 16, \
		.shift = 0, \
		.endianness = IIO_BE, \
	}, \
}

#define ADIS16400_TEMP_CHAN(addr, bits) { \
	.type = IIO_TEMP, \
	.indexed = 1, \
	.channel = 0, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
		BIT(IIO_CHAN_INFO_OFFSET) | \
		BIT(IIO_CHAN_INFO_SCALE), \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.address = (addr), \
	.scan_index = ADIS16350_SCAN_TEMP_X, \
	.scan_type = { \
		.sign = 's', \
		.realbits = (bits), \
		.storagebits = 16, \
		.shift = 0, \
		.endianness = IIO_BE, \
	}, \
}

#define ADIS16400_INCLI_CHAN(mod, addr, bits) { \
	.type = IIO_INCLI, \
	.modified = 1, \
	.channel2 = IIO_MOD_ ## mod, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.address = (addr), \
	.scan_index = ADIS16300_SCAN_INCLI_ ## mod, \
	.scan_type = { \
		.sign = 's', \
		.realbits = (bits), \
		.storagebits = 16, \
		.shift = 0, \
		.endianness = IIO_BE, \
	}, \
}

static const struct iio_chan_spec adis16400_channels[] = {
	ADIS16400_SUPPLY_CHAN(ADIS16400_SUPPLY_OUT, 14),
	ADIS16400_GYRO_CHAN(X, ADIS16400_XGYRO_OUT, 14),
	ADIS16400_GYRO_CHAN(Y, ADIS16400_YGYRO_OUT, 14),
	ADIS16400_GYRO_CHAN(Z, ADIS16400_ZGYRO_OUT, 14),
	ADIS16400_ACCEL_CHAN(X, ADIS16400_XACCL_OUT, 14),
	ADIS16400_ACCEL_CHAN(Y, ADIS16400_YACCL_OUT, 14),
	ADIS16400_ACCEL_CHAN(Z, ADIS16400_ZACCL_OUT, 14),
	ADIS16400_MAGN_CHAN(X, ADIS16400_XMAGN_OUT, 14),
	ADIS16400_MAGN_CHAN(Y, ADIS16400_YMAGN_OUT, 14),
	ADIS16400_MAGN_CHAN(Z, ADIS16400_ZMAGN_OUT, 14),
	ADIS16400_TEMP_CHAN(ADIS16400_TEMP_OUT, 12),
	ADIS16400_AUX_ADC_CHAN(ADIS16400_AUX_ADC, 12),
	IIO_CHAN_SOFT_TIMESTAMP(ADIS16400_SCAN_TIMESTAMP),
};

static const struct iio_chan_spec adis16448_channels[] = {
	ADIS16400_GYRO_CHAN(X, ADIS16400_XGYRO_OUT, 16),
	ADIS16400_GYRO_CHAN(Y, ADIS16400_YGYRO_OUT, 16),
	ADIS16400_GYRO_CHAN(Z, ADIS16400_ZGYRO_OUT, 16),
	ADIS16400_ACCEL_CHAN(X, ADIS16400_XACCL_OUT, 16),
	ADIS16400_ACCEL_CHAN(Y, ADIS16400_YACCL_OUT, 16),
	ADIS16400_ACCEL_CHAN(Z, ADIS16400_ZACCL_OUT, 16),
	ADIS16400_MAGN_CHAN(X, ADIS16400_XMAGN_OUT, 16),
	ADIS16400_MAGN_CHAN(Y, ADIS16400_YMAGN_OUT, 16),
	ADIS16400_MAGN_CHAN(Z, ADIS16400_ZMAGN_OUT, 16),
	{
		.type = IIO_PRESSURE,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.address = ADIS16448_BARO_OUT,
		.scan_index = ADIS16400_SCAN_BARO,
		.scan_type = {
			.sign = 's',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_BE,
		},
	},
	ADIS16400_TEMP_CHAN(ADIS16448_TEMP_OUT, 12),
	IIO_CHAN_SOFT_TIMESTAMP(ADIS16400_SCAN_TIMESTAMP),
};

static const struct iio_chan_spec adis16350_channels[] = {
	ADIS16400_SUPPLY_CHAN(ADIS16400_SUPPLY_OUT, 12),
	ADIS16400_GYRO_CHAN(X, ADIS16400_XGYRO_OUT, 14),
	ADIS16400_GYRO_CHAN(Y, ADIS16400_YGYRO_OUT, 14),
	ADIS16400_GYRO_CHAN(Z, ADIS16400_ZGYRO_OUT, 14),
	ADIS16400_ACCEL_CHAN(X, ADIS16400_XACCL_OUT, 14),
	ADIS16400_ACCEL_CHAN(Y, ADIS16400_YACCL_OUT, 14),
	ADIS16400_ACCEL_CHAN(Z, ADIS16400_ZACCL_OUT, 14),
	ADIS16400_MAGN_CHAN(X, ADIS16400_XMAGN_OUT, 14),
	ADIS16400_MAGN_CHAN(Y, ADIS16400_YMAGN_OUT, 14),
	ADIS16400_MAGN_CHAN(Z, ADIS16400_ZMAGN_OUT, 14),
	ADIS16400_AUX_ADC_CHAN(ADIS16300_AUX_ADC, 12),
	ADIS16400_MOD_TEMP_CHAN(X, ADIS16350_XTEMP_OUT, 12),
	ADIS16400_MOD_TEMP_CHAN(Y, ADIS16350_YTEMP_OUT, 12),
	ADIS16400_MOD_TEMP_CHAN(Z, ADIS16350_ZTEMP_OUT, 12),
	IIO_CHAN_SOFT_TIMESTAMP(ADIS16400_SCAN_TIMESTAMP),
};

static const struct iio_chan_spec adis16300_channels[] = {
	ADIS16400_SUPPLY_CHAN(ADIS16400_SUPPLY_OUT, 12),
	ADIS16400_GYRO_CHAN(X, ADIS16400_XGYRO_OUT, 14),
	ADIS16400_ACCEL_CHAN(X, ADIS16400_XACCL_OUT, 14),
	ADIS16400_ACCEL_CHAN(Y, ADIS16400_YACCL_OUT, 14),
	ADIS16400_ACCEL_CHAN(Z, ADIS16400_ZACCL_OUT, 14),
	ADIS16400_TEMP_CHAN(ADIS16350_XTEMP_OUT, 12),
	ADIS16400_AUX_ADC_CHAN(ADIS16300_AUX_ADC, 12),
	ADIS16400_INCLI_CHAN(X, ADIS16300_PITCH_OUT, 13),
	ADIS16400_INCLI_CHAN(Y, ADIS16300_ROLL_OUT, 13),
	IIO_CHAN_SOFT_TIMESTAMP(ADIS16400_SCAN_TIMESTAMP),
};

static const struct iio_chan_spec adis16334_channels[] = {
	ADIS16400_GYRO_CHAN(X, ADIS16400_XGYRO_OUT, 14),
	ADIS16400_GYRO_CHAN(Y, ADIS16400_YGYRO_OUT, 14),
	ADIS16400_GYRO_CHAN(Z, ADIS16400_ZGYRO_OUT, 14),
	ADIS16400_ACCEL_CHAN(X, ADIS16400_XACCL_OUT, 14),
	ADIS16400_ACCEL_CHAN(Y, ADIS16400_YACCL_OUT, 14),
	ADIS16400_ACCEL_CHAN(Z, ADIS16400_ZACCL_OUT, 14),
	ADIS16400_TEMP_CHAN(ADIS16350_XTEMP_OUT, 12),
	IIO_CHAN_SOFT_TIMESTAMP(ADIS16400_SCAN_TIMESTAMP),
};

static struct adis16400_chip_info adis16400_chips[] = {
	[ADIS16300] = {
		.channels = adis16300_channels,
		.num_channels = ARRAY_SIZE(adis16300_channels),
		.flags = ADIS16400_HAS_SLOW_MODE,
		.gyro_scale_micro = IIO_DEGREE_TO_RAD(50000), /* 0.05 deg/s */
		.accel_scale_micro = 5884,
		.temp_scale_nano = 140000000, /* 0.14 C */
		.temp_offset = 25000000 / 140000, /* 25 C = 0x00 */
		.set_freq = adis16400_set_freq,
		.get_freq = adis16400_get_freq,
	},
	[ADIS16334] = {
		.channels = adis16334_channels,
		.num_channels = ARRAY_SIZE(adis16334_channels),
		.flags = ADIS16400_HAS_PROD_ID | ADIS16400_NO_BURST |
				ADIS16400_HAS_SERIAL_NUMBER,
		.gyro_scale_micro = IIO_DEGREE_TO_RAD(50000), /* 0.05 deg/s */
		.accel_scale_micro = IIO_G_TO_M_S_2(1000), /* 1 mg */
		.temp_scale_nano = 67850000, /* 0.06785 C */
		.temp_offset = 25000000 / 67850, /* 25 C = 0x00 */
		.set_freq = adis16334_set_freq,
		.get_freq = adis16334_get_freq,
	},
	[ADIS16350] = {
		.channels = adis16350_channels,
		.num_channels = ARRAY_SIZE(adis16350_channels),
		.gyro_scale_micro = IIO_DEGREE_TO_RAD(73260), /* 0.07326 deg/s */
		.accel_scale_micro = IIO_G_TO_M_S_2(2522), /* 0.002522 g */
		.temp_scale_nano = 145300000, /* 0.1453 C */
		.temp_offset = 25000000 / 145300, /* 25 C = 0x00 */
		.flags = ADIS16400_NO_BURST | ADIS16400_HAS_SLOW_MODE,
		.set_freq = adis16400_set_freq,
		.get_freq = adis16400_get_freq,
	},
	[ADIS16360] = {
		.channels = adis16350_channels,
		.num_channels = ARRAY_SIZE(adis16350_channels),
		.flags = ADIS16400_HAS_PROD_ID | ADIS16400_HAS_SLOW_MODE |
				ADIS16400_HAS_SERIAL_NUMBER,
		.gyro_scale_micro = IIO_DEGREE_TO_RAD(50000), /* 0.05 deg/s */
		.accel_scale_micro = IIO_G_TO_M_S_2(3333), /* 3.333 mg */
		.temp_scale_nano = 136000000, /* 0.136 C */
		.temp_offset = 25000000 / 136000, /* 25 C = 0x00 */
		.set_freq = adis16400_set_freq,
		.get_freq = adis16400_get_freq,
	},
	[ADIS16362] = {
		.channels = adis16350_channels,
		.num_channels = ARRAY_SIZE(adis16350_channels),
		.flags = ADIS16400_HAS_PROD_ID | ADIS16400_HAS_SLOW_MODE |
				ADIS16400_HAS_SERIAL_NUMBER,
		.gyro_scale_micro = IIO_DEGREE_TO_RAD(50000), /* 0.05 deg/s */
		.accel_scale_micro = IIO_G_TO_M_S_2(333), /* 0.333 mg */
		.temp_scale_nano = 136000000, /* 0.136 C */
		.temp_offset = 25000000 / 136000, /* 25 C = 0x00 */
		.set_freq = adis16400_set_freq,
		.get_freq = adis16400_get_freq,
	},
	[ADIS16364] = {
		.channels = adis16350_channels,
		.num_channels = ARRAY_SIZE(adis16350_channels),
		.flags = ADIS16400_HAS_PROD_ID | ADIS16400_HAS_SLOW_MODE |
				ADIS16400_HAS_SERIAL_NUMBER,
		.gyro_scale_micro = IIO_DEGREE_TO_RAD(50000), /* 0.05 deg/s */
		.accel_scale_micro = IIO_G_TO_M_S_2(1000), /* 1 mg */
		.temp_scale_nano = 136000000, /* 0.136 C */
		.temp_offset = 25000000 / 136000, /* 25 C = 0x00 */
		.set_freq = adis16400_set_freq,
		.get_freq = adis16400_get_freq,
	},
	[ADIS16400] = {
		.channels = adis16400_channels,
		.num_channels = ARRAY_SIZE(adis16400_channels),
		.flags = ADIS16400_HAS_PROD_ID | ADIS16400_HAS_SLOW_MODE,
		.gyro_scale_micro = IIO_DEGREE_TO_RAD(50000), /* 0.05 deg/s */
		.accel_scale_micro = IIO_G_TO_M_S_2(3333), /* 3.333 mg */
		.temp_scale_nano = 140000000, /* 0.14 C */
		.temp_offset = 25000000 / 140000, /* 25 C = 0x00 */
		.set_freq = adis16400_set_freq,
		.get_freq = adis16400_get_freq,
	},
	[ADIS16448] = {
		.channels = adis16448_channels,
		.num_channels = ARRAY_SIZE(adis16448_channels),
		.flags = ADIS16400_HAS_PROD_ID |
				ADIS16400_HAS_SERIAL_NUMBER |
				ADIS16400_BURST_DIAG_STAT,
		.gyro_scale_micro = IIO_DEGREE_TO_RAD(10000), /* 0.01 deg/s */
		.accel_scale_micro = IIO_G_TO_M_S_2(833), /* 1/1200 g */
		.temp_scale_nano = 73860000, /* 0.07386 C */
		.temp_offset = 31000000 / 73860, /* 31 C = 0x00 */
		.set_freq = adis16334_set_freq,
		.get_freq = adis16334_get_freq,
	}
};

static const struct iio_info adis16400_info = {
	.driver_module = THIS_MODULE,
	.read_raw = &adis16400_read_raw,
	.write_raw = &adis16400_write_raw,
	.update_scan_mode = adis16400_update_scan_mode,
	.debugfs_reg_access = adis_debugfs_reg_access,
};

static const char * const adis16400_status_error_msgs[] = {
	[ADIS16400_DIAG_STAT_ZACCL_FAIL] = "Z-axis accelerometer self-test failure",
	[ADIS16400_DIAG_STAT_YACCL_FAIL] = "Y-axis accelerometer self-test failure",
	[ADIS16400_DIAG_STAT_XACCL_FAIL] = "X-axis accelerometer self-test failure",
	[ADIS16400_DIAG_STAT_XGYRO_FAIL] = "X-axis gyroscope self-test failure",
	[ADIS16400_DIAG_STAT_YGYRO_FAIL] = "Y-axis gyroscope self-test failure",
	[ADIS16400_DIAG_STAT_ZGYRO_FAIL] = "Z-axis gyroscope self-test failure",
	[ADIS16400_DIAG_STAT_ALARM2] = "Alarm 2 active",
	[ADIS16400_DIAG_STAT_ALARM1] = "Alarm 1 active",
	[ADIS16400_DIAG_STAT_FLASH_CHK] = "Flash checksum error",
	[ADIS16400_DIAG_STAT_SELF_TEST] = "Self test error",
	[ADIS16400_DIAG_STAT_OVERFLOW] = "Sensor overrange",
	[ADIS16400_DIAG_STAT_SPI_FAIL] = "SPI failure",
	[ADIS16400_DIAG_STAT_FLASH_UPT] = "Flash update failed",
	[ADIS16400_DIAG_STAT_POWER_HIGH] = "Power supply above 5.25V",
	[ADIS16400_DIAG_STAT_POWER_LOW] = "Power supply below 4.75V",
};

static const struct adis_data adis16400_data = {
	.msc_ctrl_reg = ADIS16400_MSC_CTRL,
	.glob_cmd_reg = ADIS16400_GLOB_CMD,
	.diag_stat_reg = ADIS16400_DIAG_STAT,

	.read_delay = 50,
	.write_delay = 50,

	.self_test_mask = ADIS16400_MSC_CTRL_MEM_TEST,
	.startup_delay = ADIS16400_STARTUP_DELAY,

	.status_error_msgs = adis16400_status_error_msgs,
	.status_error_mask = BIT(ADIS16400_DIAG_STAT_ZACCL_FAIL) |
		BIT(ADIS16400_DIAG_STAT_YACCL_FAIL) |
		BIT(ADIS16400_DIAG_STAT_XACCL_FAIL) |
		BIT(ADIS16400_DIAG_STAT_XGYRO_FAIL) |
		BIT(ADIS16400_DIAG_STAT_YGYRO_FAIL) |
		BIT(ADIS16400_DIAG_STAT_ZGYRO_FAIL) |
		BIT(ADIS16400_DIAG_STAT_ALARM2) |
		BIT(ADIS16400_DIAG_STAT_ALARM1) |
		BIT(ADIS16400_DIAG_STAT_FLASH_CHK) |
		BIT(ADIS16400_DIAG_STAT_SELF_TEST) |
		BIT(ADIS16400_DIAG_STAT_OVERFLOW) |
		BIT(ADIS16400_DIAG_STAT_SPI_FAIL) |
		BIT(ADIS16400_DIAG_STAT_FLASH_UPT) |
		BIT(ADIS16400_DIAG_STAT_POWER_HIGH) |
		BIT(ADIS16400_DIAG_STAT_POWER_LOW),
};

static void adis16400_setup_chan_mask(struct adis16400_state *st)
{
	const struct adis16400_chip_info *chip_info = st->variant;
	unsigned i;

	for (i = 0; i < chip_info->num_channels; i++) {
		const struct iio_chan_spec *ch = &chip_info->channels[i];

		if (ch->scan_index >= 0 &&
		    ch->scan_index != ADIS16400_SCAN_TIMESTAMP)
			st->avail_scan_mask[0] |= BIT(ch->scan_index);
	}
}

static int adis16400_probe(struct spi_device *spi)
{
	struct adis16400_state *st;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (indio_dev == NULL)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	/* this is only used for removal purposes */
	spi_set_drvdata(spi, indio_dev);

	/* setup the industrialio driver allocated elements */
	st->variant = &adis16400_chips[spi_get_device_id(spi)->driver_data];
	indio_dev->dev.parent = &spi->dev;
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->channels = st->variant->channels;
	indio_dev->num_channels = st->variant->num_channels;
	indio_dev->info = &adis16400_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	if (!(st->variant->flags & ADIS16400_NO_BURST)) {
		adis16400_setup_chan_mask(st);
		indio_dev->available_scan_masks = st->avail_scan_mask;
	}

	ret = adis_init(&st->adis, indio_dev, spi, &adis16400_data);
	if (ret)
		return ret;

	ret = adis_setup_buffer_and_trigger(&st->adis, indio_dev,
			adis16400_trigger_handler);
	if (ret)
		return ret;

	/* Get the device into a sane initial state */
	ret = adis16400_initial_setup(indio_dev);
	if (ret)
		goto error_cleanup_buffer;
	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_cleanup_buffer;

	adis16400_debugfs_init(indio_dev);
	return 0;

error_cleanup_buffer:
	adis_cleanup_buffer_and_trigger(&st->adis, indio_dev);
	return ret;
}

static int adis16400_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);
	struct adis16400_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	adis16400_stop_device(indio_dev);

	adis_cleanup_buffer_and_trigger(&st->adis, indio_dev);

	return 0;
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
	{"adis16365", ADIS16360},
	{"adis16400", ADIS16400},
	{"adis16405", ADIS16400},
	{"adis16448", ADIS16448},
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
	.remove = adis16400_remove,
};
module_spi_driver(adis16400_driver);

MODULE_AUTHOR("Manuel Stahl <manuel.stahl@iis.fraunhofer.de>");
MODULE_DESCRIPTION("Analog Devices ADIS16400/5 IMU SPI driver");
MODULE_LICENSE("GPL v2");
