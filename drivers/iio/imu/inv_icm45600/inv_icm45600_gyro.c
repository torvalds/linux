// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025 Invensense, Inc.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/math64.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/types.h>

#include <linux/iio/buffer.h>
#include <linux/iio/common/inv_sensors_timestamp.h>
#include <linux/iio/iio.h>
#include <linux/iio/kfifo_buf.h>

#include "inv_icm45600_buffer.h"
#include "inv_icm45600.h"

enum inv_icm45600_gyro_scan {
	INV_ICM45600_GYRO_SCAN_X,
	INV_ICM45600_GYRO_SCAN_Y,
	INV_ICM45600_GYRO_SCAN_Z,
	INV_ICM45600_GYRO_SCAN_TEMP,
	INV_ICM45600_GYRO_SCAN_TIMESTAMP,
};

static const struct iio_chan_spec_ext_info inv_icm45600_gyro_ext_infos[] = {
	IIO_MOUNT_MATRIX(IIO_SHARED_BY_ALL, inv_icm45600_get_mount_matrix),
	{ }
};

#define INV_ICM45600_GYRO_CHAN(_modifier, _index, _ext_info)		\
	{								\
		.type = IIO_ANGL_VEL,					\
		.modified = 1,						\
		.channel2 = _modifier,					\
		.info_mask_separate =					\
			BIT(IIO_CHAN_INFO_RAW) |			\
			BIT(IIO_CHAN_INFO_CALIBBIAS),			\
		.info_mask_shared_by_type =				\
			BIT(IIO_CHAN_INFO_SCALE),			\
		.info_mask_shared_by_type_available =			\
			BIT(IIO_CHAN_INFO_SCALE) |			\
			BIT(IIO_CHAN_INFO_CALIBBIAS),			\
		.info_mask_shared_by_all =				\
			BIT(IIO_CHAN_INFO_SAMP_FREQ),			\
		.info_mask_shared_by_all_available =			\
			BIT(IIO_CHAN_INFO_SAMP_FREQ),			\
		.scan_index = _index,					\
		.scan_type = {						\
			.sign = 's',					\
			.realbits = 16,					\
			.storagebits = 16,				\
			.endianness = IIO_LE,				\
		},							\
		.ext_info = _ext_info,					\
	}

static const struct iio_chan_spec inv_icm45600_gyro_channels[] = {
	INV_ICM45600_GYRO_CHAN(IIO_MOD_X, INV_ICM45600_GYRO_SCAN_X,
			       inv_icm45600_gyro_ext_infos),
	INV_ICM45600_GYRO_CHAN(IIO_MOD_Y, INV_ICM45600_GYRO_SCAN_Y,
			       inv_icm45600_gyro_ext_infos),
	INV_ICM45600_GYRO_CHAN(IIO_MOD_Z, INV_ICM45600_GYRO_SCAN_Z,
			       inv_icm45600_gyro_ext_infos),
	INV_ICM45600_TEMP_CHAN(INV_ICM45600_GYRO_SCAN_TEMP),
	IIO_CHAN_SOFT_TIMESTAMP(INV_ICM45600_GYRO_SCAN_TIMESTAMP),
};

/*
 * IIO buffer data: size must be a power of 2 and timestamp aligned
 * 16 bytes: 6 bytes angular velocity, 2 bytes temperature, 8 bytes timestamp
 */
struct inv_icm45600_gyro_buffer {
	struct inv_icm45600_fifo_sensor_data gyro;
	s16 temp;
	aligned_s64 timestamp;
};

static const unsigned long inv_icm45600_gyro_scan_masks[] = {
	/* 3-axis gyro + temperature */
	BIT(INV_ICM45600_GYRO_SCAN_X) |
	BIT(INV_ICM45600_GYRO_SCAN_Y) |
	BIT(INV_ICM45600_GYRO_SCAN_Z) |
	BIT(INV_ICM45600_GYRO_SCAN_TEMP),
	0
};

/* enable gyroscope sensor and FIFO write */
static int inv_icm45600_gyro_update_scan_mode(struct iio_dev *indio_dev,
					      const unsigned long *scan_mask)
{
	struct inv_icm45600_state *st = iio_device_get_drvdata(indio_dev);
	struct inv_icm45600_sensor_state *gyro_st = iio_priv(indio_dev);
	struct inv_icm45600_sensor_conf conf = INV_ICM45600_SENSOR_CONF_KEEP_VALUES;
	unsigned int fifo_en = 0;
	unsigned int sleep = 0;
	int ret;

	scoped_guard(mutex, &st->lock) {
		if (*scan_mask & BIT(INV_ICM45600_GYRO_SCAN_TEMP))
			fifo_en |= INV_ICM45600_SENSOR_TEMP;

		if (*scan_mask & (BIT(INV_ICM45600_GYRO_SCAN_X) |
				 BIT(INV_ICM45600_GYRO_SCAN_Y) |
				 BIT(INV_ICM45600_GYRO_SCAN_Z))) {
			/* enable gyro sensor */
			conf.mode = gyro_st->power_mode;
			ret = inv_icm45600_set_gyro_conf(st, &conf, &sleep);
			if (ret)
				return ret;
			fifo_en |= INV_ICM45600_SENSOR_GYRO;
		}
		ret = inv_icm45600_buffer_set_fifo_en(st, fifo_en | st->fifo.en);
	}
	if (sleep)
		msleep(sleep);

	return ret;
}

static int _inv_icm45600_gyro_read_sensor(struct inv_icm45600_state *st,
					  struct inv_icm45600_sensor_state *gyro_st,
					  unsigned int reg, int *val)
{
	struct inv_icm45600_sensor_conf conf = INV_ICM45600_SENSOR_CONF_KEEP_VALUES;
	int ret;

	/* enable gyro sensor */
	conf.mode = gyro_st->power_mode;
	ret = inv_icm45600_set_gyro_conf(st, &conf, NULL);
	if (ret)
		return ret;

	/* read gyro register data */
	ret = regmap_bulk_read(st->map, reg, &st->buffer.u16, sizeof(st->buffer.u16));
	if (ret)
		return ret;

	*val = sign_extend32(le16_to_cpup(&st->buffer.u16), 15);
	if (*val == INV_ICM45600_DATA_INVALID)
		return -ENODATA;

	return 0;
}

static int inv_icm45600_gyro_read_sensor(struct iio_dev *indio_dev,
					 struct iio_chan_spec const *chan,
					 int *val)
{
	struct inv_icm45600_state *st = iio_device_get_drvdata(indio_dev);
	struct inv_icm45600_sensor_state *gyro_st = iio_priv(indio_dev);
	struct device *dev = regmap_get_device(st->map);
	unsigned int reg;
	int ret;

	if (chan->type != IIO_ANGL_VEL)
		return -EINVAL;

	switch (chan->channel2) {
	case IIO_MOD_X:
		reg = INV_ICM45600_REG_GYRO_DATA_X;
		break;
	case IIO_MOD_Y:
		reg = INV_ICM45600_REG_GYRO_DATA_Y;
		break;
	case IIO_MOD_Z:
		reg = INV_ICM45600_REG_GYRO_DATA_Z;
		break;
	default:
		return -EINVAL;
	}

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return ret;

	scoped_guard(mutex, &st->lock)
		ret = _inv_icm45600_gyro_read_sensor(st, gyro_st, reg, val);

	pm_runtime_put_autosuspend(dev);

	return ret;
}

/* IIO format int + nano */
const int inv_icm45600_gyro_scale[][2] = {
	/* +/- 2000dps => 0.001065264 rad/s */
	[INV_ICM45600_GYRO_FS_2000DPS] = { 0, 1065264 },
	/* +/- 1000dps => 0.000532632 rad/s */
	[INV_ICM45600_GYRO_FS_1000DPS] = { 0, 532632 },
	/* +/- 500dps => 0.000266316 rad/s */
	[INV_ICM45600_GYRO_FS_500DPS] = { 0, 266316 },
	/* +/- 250dps => 0.000133158 rad/s */
	[INV_ICM45600_GYRO_FS_250DPS] = { 0, 133158 },
	/* +/- 125dps => 0.000066579 rad/s */
	[INV_ICM45600_GYRO_FS_125DPS] = { 0, 66579 },
	/* +/- 62.5dps => 0.000033290 rad/s */
	[INV_ICM45600_GYRO_FS_62_5DPS] = { 0, 33290 },
	/* +/- 31.25dps => 0.000016645 rad/s */
	[INV_ICM45600_GYRO_FS_31_25DPS] = { 0, 16645 },
	/* +/- 15.625dps => 0.000008322 rad/s */
	[INV_ICM45600_GYRO_FS_15_625DPS] = { 0, 8322 },
};

/* IIO format int + nano */
const int inv_icm45686_gyro_scale[][2] = {
	/* +/- 4000dps => 0.002130529 rad/s */
	[INV_ICM45686_GYRO_FS_4000DPS] = { 0, 2130529 },
	/* +/- 2000dps => 0.001065264 rad/s */
	[INV_ICM45686_GYRO_FS_2000DPS] = { 0, 1065264 },
	/* +/- 1000dps => 0.000532632 rad/s */
	[INV_ICM45686_GYRO_FS_1000DPS] = { 0, 532632 },
	/* +/- 500dps => 0.000266316 rad/s */
	[INV_ICM45686_GYRO_FS_500DPS] = { 0, 266316 },
	/* +/- 250dps => 0.000133158 rad/s */
	[INV_ICM45686_GYRO_FS_250DPS] = { 0, 133158 },
	/* +/- 125dps => 0.000066579 rad/s */
	[INV_ICM45686_GYRO_FS_125DPS] = { 0, 66579 },
	/* +/- 62.5dps => 0.000033290 rad/s */
	[INV_ICM45686_GYRO_FS_62_5DPS] = { 0, 33290 },
	/* +/- 31.25dps => 0.000016645 rad/s */
	[INV_ICM45686_GYRO_FS_31_25DPS] = { 0, 16645 },
	/* +/- 15.625dps => 0.000008322 rad/s */
	[INV_ICM45686_GYRO_FS_15_625DPS] = { 0, 8322 },
};

static int inv_icm45600_gyro_read_scale(struct iio_dev *indio_dev,
					int *val, int *val2)
{
	struct inv_icm45600_state *st = iio_device_get_drvdata(indio_dev);
	struct inv_icm45600_sensor_state *gyro_st = iio_priv(indio_dev);
	unsigned int idx;

	idx = st->conf.gyro.fs;

	/* Full scale register starts at 1 for not High FSR parts */
	if (gyro_st->scales ==  (const int *)&inv_icm45600_gyro_scale)
		idx--;

	*val = gyro_st->scales[2 * idx];
	*val2 = gyro_st->scales[2 * idx + 1];
	return IIO_VAL_INT_PLUS_NANO;
}

static int inv_icm45600_gyro_write_scale(struct iio_dev *indio_dev,
					 int val, int val2)
{
	struct inv_icm45600_state *st = iio_device_get_drvdata(indio_dev);
	struct inv_icm45600_sensor_state *gyro_st = iio_priv(indio_dev);
	struct device *dev = regmap_get_device(st->map);
	unsigned int idx;
	struct inv_icm45600_sensor_conf conf = INV_ICM45600_SENSOR_CONF_KEEP_VALUES;
	int ret;

	for (idx = 0; idx < gyro_st->scales_len; idx += 2) {
		if (val == gyro_st->scales[idx] &&
		    val2 == gyro_st->scales[idx + 1])
			break;
	}
	if (idx == gyro_st->scales_len)
		return -EINVAL;

	conf.fs = idx / 2;

	/* Full scale register starts at 1 for not High FSR parts */
	if (gyro_st->scales == (const int *)&inv_icm45600_gyro_scale)
		conf.fs++;

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return ret;

	scoped_guard(mutex, &st->lock)
		ret = inv_icm45600_set_gyro_conf(st, &conf, NULL);

	pm_runtime_put_autosuspend(dev);

	return ret;
}

/* IIO format int + micro */
static const int inv_icm45600_gyro_odr[] = {
	1, 562500,	/* 1.5625Hz */
	3, 125000,	/* 3.125Hz */
	6, 250000,	/* 6.25Hz */
	12, 500000,	/* 12.5Hz */
	25, 0,		/* 25Hz */
	50, 0,		/* 50Hz */
	100, 0,		/* 100Hz */
	200, 0,		/* 200Hz */
	400, 0,		/* 400Hz */
	800, 0,		/* 800Hz */
	1600, 0,	/* 1.6kHz */
	3200, 0,	/* 3.2kHz */
	6400, 0,	/* 6.4kHz */
};

static const int inv_icm45600_gyro_odr_conv[] = {
	INV_ICM45600_ODR_1_5625HZ_LP,
	INV_ICM45600_ODR_3_125HZ_LP,
	INV_ICM45600_ODR_6_25HZ_LP,
	INV_ICM45600_ODR_12_5HZ,
	INV_ICM45600_ODR_25HZ,
	INV_ICM45600_ODR_50HZ,
	INV_ICM45600_ODR_100HZ,
	INV_ICM45600_ODR_200HZ,
	INV_ICM45600_ODR_400HZ,
	INV_ICM45600_ODR_800HZ_LN,
	INV_ICM45600_ODR_1600HZ_LN,
	INV_ICM45600_ODR_3200HZ_LN,
	INV_ICM45600_ODR_6400HZ_LN,
};

static int inv_icm45600_gyro_read_odr(struct inv_icm45600_state *st,
				      int *val, int *val2)
{
	unsigned int odr;
	unsigned int i;

	odr = st->conf.gyro.odr;

	for (i = 0; i < ARRAY_SIZE(inv_icm45600_gyro_odr_conv); ++i) {
		if (inv_icm45600_gyro_odr_conv[i] == odr)
			break;
	}
	if (i >= ARRAY_SIZE(inv_icm45600_gyro_odr_conv))
		return -EINVAL;

	*val = inv_icm45600_gyro_odr[2 * i];
	*val2 = inv_icm45600_gyro_odr[2 * i + 1];

	return IIO_VAL_INT_PLUS_MICRO;
}

static int _inv_icm45600_gyro_write_odr(struct iio_dev *indio_dev, int odr)
{
	struct inv_icm45600_state *st = iio_device_get_drvdata(indio_dev);
	struct inv_icm45600_sensor_state *gyro_st = iio_priv(indio_dev);
	struct inv_sensors_timestamp *ts = &gyro_st->ts;
	struct inv_icm45600_sensor_conf conf = INV_ICM45600_SENSOR_CONF_KEEP_VALUES;
	int ret;

	conf.odr = odr;
	ret = inv_sensors_timestamp_update_odr(ts, inv_icm45600_odr_to_period(conf.odr),
						iio_buffer_enabled(indio_dev));
	if (ret)
		return ret;

	if (st->conf.gyro.mode != INV_ICM45600_SENSOR_MODE_OFF)
		conf.mode = gyro_st->power_mode;

	ret = inv_icm45600_set_gyro_conf(st, &conf, NULL);
	if (ret)
		return ret;

	inv_icm45600_buffer_update_fifo_period(st);
	inv_icm45600_buffer_update_watermark(st);

	return 0;
}

static int inv_icm45600_gyro_write_odr(struct iio_dev *indio_dev,
				       int val, int val2)
{
	struct inv_icm45600_state *st = iio_device_get_drvdata(indio_dev);
	struct device *dev = regmap_get_device(st->map);
	unsigned int idx;
	int odr;
	int ret;

	for (idx = 0; idx < ARRAY_SIZE(inv_icm45600_gyro_odr); idx += 2) {
		if (val == inv_icm45600_gyro_odr[idx] &&
		    val2 == inv_icm45600_gyro_odr[idx + 1])
			break;
	}
	if (idx >= ARRAY_SIZE(inv_icm45600_gyro_odr))
		return -EINVAL;

	odr = inv_icm45600_gyro_odr_conv[idx / 2];

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return ret;

	scoped_guard(mutex, &st->lock)
		ret = _inv_icm45600_gyro_write_odr(indio_dev, odr);

	pm_runtime_put_autosuspend(dev);

	return ret;
}

/*
 * Calibration bias values, IIO range format int + nano.
 * Value is limited to +/-62.5dps coded on 14 bits signed. Step is 7.5mdps.
 */
static int inv_icm45600_gyro_calibbias[] = {
	-1, 90830336,	/* min: -1.090830336 rad/s */
	0, 133158,	/* step: 0.000133158 rad/s */
	1, 90697178,	/* max: 1.090697178 rad/s */
};

static int inv_icm45600_gyro_read_offset(struct inv_icm45600_state *st,
					 struct iio_chan_spec const *chan,
					 int *val, int *val2)
{
	struct device *dev = regmap_get_device(st->map);
	s64 val64;
	s32 bias;
	unsigned int reg;
	s16 offset;
	int ret;

	if (chan->type != IIO_ANGL_VEL)
		return -EINVAL;

	switch (chan->channel2) {
	case IIO_MOD_X:
		reg = INV_ICM45600_IPREG_SYS1_REG_42;
		break;
	case IIO_MOD_Y:
		reg = INV_ICM45600_IPREG_SYS1_REG_56;
		break;
	case IIO_MOD_Z:
		reg = INV_ICM45600_IPREG_SYS1_REG_70;
		break;
	default:
		return -EINVAL;
	}

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return ret;

	scoped_guard(mutex, &st->lock)
		ret = regmap_bulk_read(st->map, reg, &st->buffer.u16, sizeof(st->buffer.u16));

	pm_runtime_put_autosuspend(dev);
	if (ret)
		return ret;

	offset = le16_to_cpup(&st->buffer.u16) & INV_ICM45600_GYRO_OFFUSER_MASK;
	/* 14 bits signed value */
	offset = sign_extend32(offset, 13);

	/*
	 * convert raw offset to dps then to rad/s
	 * 14 bits signed raw max 62.5 to dps: 625 / 81920
	 * dps to rad: Pi / 180
	 * result in nano (1000000000)
	 * (offset * 625 * Pi * 1000000000) / (81920 * 180)
	 */
	val64 = (s64)offset * 625LL * 3141592653LL;
	/* for rounding, add + or - divisor (81920 * 180) divided by 2 */
	if (val64 >= 0)
		val64 += 81920 * 180 / 2;
	else
		val64 -= 81920 * 180 / 2;
	bias = div_s64(val64, 81920 * 180);
	*val = bias / 1000000000L;
	*val2 = bias % 1000000000L;

	return IIO_VAL_INT_PLUS_NANO;
}

static int inv_icm45600_gyro_write_offset(struct inv_icm45600_state *st,
					  struct iio_chan_spec const *chan,
					  int val, int val2)
{
	struct device *dev = regmap_get_device(st->map);
	s64 val64, min, max;
	unsigned int reg;
	s16 offset;
	int ret;

	if (chan->type != IIO_ANGL_VEL)
		return -EINVAL;

	switch (chan->channel2) {
	case IIO_MOD_X:
		reg = INV_ICM45600_IPREG_SYS1_REG_42;
		break;
	case IIO_MOD_Y:
		reg = INV_ICM45600_IPREG_SYS1_REG_56;
		break;
	case IIO_MOD_Z:
		reg = INV_ICM45600_IPREG_SYS1_REG_70;
		break;
	default:
		return -EINVAL;
	}

	/* inv_icm45600_gyro_calibbias: min - step - max in nano */
	min = (s64)inv_icm45600_gyro_calibbias[0] * 1000000000LL -
	      (s64)inv_icm45600_gyro_calibbias[1];
	max = (s64)inv_icm45600_gyro_calibbias[4] * 1000000000LL +
	      (s64)inv_icm45600_gyro_calibbias[5];
	val64 = (s64)val * 1000000000LL;
	if (val >= 0)
		val64 += (s64)val2;
	else
		val64 -= (s64)val2;
	if (val64 < min || val64 > max)
		return -EINVAL;

	/*
	 * convert rad/s to dps then to raw value
	 * rad to dps: 180 / Pi
	 * dps to raw 14 bits signed, max 62.5: 8192 / 62.5
	 * val in nano (1000000000)
	 * val * 180 * 8192 / (Pi * 1000000000 * 62.5)
	 */
	val64 = val64 * 180LL * 8192;
	/* for rounding, add + or - divisor (314159265 * 625) divided by 2 */
	if (val64 >= 0)
		val64 += 314159265LL * 625LL / 2LL;
	else
		val64 -= 314159265LL * 625LL / 2LL;
	offset = div64_s64(val64, 314159265LL * 625LL);

	/* clamp value limited to 14 bits signed */
	offset = clamp(offset, -8192, 8191);

	st->buffer.u16 = cpu_to_le16(offset & INV_ICM45600_GYRO_OFFUSER_MASK);

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return ret;

	scoped_guard(mutex, &st->lock)
		ret = regmap_bulk_write(st->map, reg, &st->buffer.u16, sizeof(st->buffer.u16));

	pm_runtime_put_autosuspend(dev);
	return ret;
}

static int inv_icm45600_gyro_read_raw(struct iio_dev *indio_dev,
				      struct iio_chan_spec const *chan,
				      int *val, int *val2, long mask)
{
	struct inv_icm45600_state *st = iio_device_get_drvdata(indio_dev);
	int ret;

	switch (chan->type) {
	case IIO_ANGL_VEL:
		break;
	case IIO_TEMP:
		return inv_icm45600_temp_read_raw(indio_dev, chan, val, val2, mask);
	default:
		return -EINVAL;
	}

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;
		ret = inv_icm45600_gyro_read_sensor(indio_dev, chan, val);
		iio_device_release_direct(indio_dev);
		if (ret)
			return ret;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		return inv_icm45600_gyro_read_scale(indio_dev, val, val2);
	case IIO_CHAN_INFO_SAMP_FREQ:
		return inv_icm45600_gyro_read_odr(st, val, val2);
	case IIO_CHAN_INFO_CALIBBIAS:
		return inv_icm45600_gyro_read_offset(st, chan, val, val2);
	default:
		return -EINVAL;
	}
}

static int inv_icm45600_gyro_read_avail(struct iio_dev *indio_dev,
					struct iio_chan_spec const *chan,
					const int **vals,
					int *type, int *length, long mask)
{
	struct inv_icm45600_sensor_state *gyro_st = iio_priv(indio_dev);

	if (chan->type != IIO_ANGL_VEL)
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		*vals = gyro_st->scales;
		*type = IIO_VAL_INT_PLUS_NANO;
		*length = gyro_st->scales_len;
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*vals = inv_icm45600_gyro_odr;
		*type = IIO_VAL_INT_PLUS_MICRO;
		*length = ARRAY_SIZE(inv_icm45600_gyro_odr);
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_CALIBBIAS:
		*vals = inv_icm45600_gyro_calibbias;
		*type = IIO_VAL_INT_PLUS_NANO;
		return IIO_AVAIL_RANGE;
	default:
		return -EINVAL;
	}
}

static int inv_icm45600_gyro_write_raw(struct iio_dev *indio_dev,
				       struct iio_chan_spec const *chan,
				       int val, int val2, long mask)
{
	struct inv_icm45600_state *st = iio_device_get_drvdata(indio_dev);
	int ret;

	if (chan->type != IIO_ANGL_VEL)
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;
		ret = inv_icm45600_gyro_write_scale(indio_dev, val, val2);
		iio_device_release_direct(indio_dev);
		return ret;
	case IIO_CHAN_INFO_SAMP_FREQ:
		return inv_icm45600_gyro_write_odr(indio_dev, val, val2);
	case IIO_CHAN_INFO_CALIBBIAS:
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;
		ret = inv_icm45600_gyro_write_offset(st, chan, val, val2);
		iio_device_release_direct(indio_dev);
		return ret;
	default:
		return -EINVAL;
	}
}

static int inv_icm45600_gyro_write_raw_get_fmt(struct iio_dev *indio_dev,
					       struct iio_chan_spec const *chan,
					       long mask)
{
	if (chan->type != IIO_ANGL_VEL)
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		return IIO_VAL_INT_PLUS_NANO;
	case IIO_CHAN_INFO_SAMP_FREQ:
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_CALIBBIAS:
		return IIO_VAL_INT_PLUS_NANO;
	default:
		return -EINVAL;
	}
}

static int inv_icm45600_gyro_hwfifo_set_watermark(struct iio_dev *indio_dev,
						  unsigned int val)
{
	struct inv_icm45600_state *st = iio_device_get_drvdata(indio_dev);

	guard(mutex)(&st->lock);

	st->fifo.watermark.gyro = val;
	return inv_icm45600_buffer_update_watermark(st);
}

static int inv_icm45600_gyro_hwfifo_flush(struct iio_dev *indio_dev,
					  unsigned int count)
{
	struct inv_icm45600_state *st = iio_device_get_drvdata(indio_dev);
	int ret;

	if (count == 0)
		return 0;

	guard(mutex)(&st->lock);

	ret = inv_icm45600_buffer_hwfifo_flush(st, count);
	if (ret)
		return ret;

	return st->fifo.nb.gyro;
}

static const struct iio_info inv_icm45600_gyro_info = {
	.read_raw = inv_icm45600_gyro_read_raw,
	.read_avail = inv_icm45600_gyro_read_avail,
	.write_raw = inv_icm45600_gyro_write_raw,
	.write_raw_get_fmt = inv_icm45600_gyro_write_raw_get_fmt,
	.debugfs_reg_access = inv_icm45600_debugfs_reg,
	.update_scan_mode = inv_icm45600_gyro_update_scan_mode,
	.hwfifo_set_watermark = inv_icm45600_gyro_hwfifo_set_watermark,
	.hwfifo_flush_to_buffer = inv_icm45600_gyro_hwfifo_flush,
};

struct iio_dev *inv_icm45600_gyro_init(struct inv_icm45600_state *st)
{
	struct device *dev = regmap_get_device(st->map);
	struct inv_icm45600_sensor_state *gyro_st;
	struct inv_sensors_timestamp_chip ts_chip;
	struct iio_dev *indio_dev;
	const char *name;
	int ret;

	name = devm_kasprintf(dev, GFP_KERNEL, "%s-gyro", st->chip_info->name);
	if (!name)
		return ERR_PTR(-ENOMEM);

	indio_dev = devm_iio_device_alloc(dev, sizeof(*gyro_st));
	if (!indio_dev)
		return ERR_PTR(-ENOMEM);
	gyro_st = iio_priv(indio_dev);

	gyro_st->scales = st->chip_info->gyro_scales;
	gyro_st->scales_len = st->chip_info->gyro_scales_len * 2;

	/* low-noise by default at init */
	gyro_st->power_mode = INV_ICM45600_SENSOR_MODE_LOW_NOISE;

	/*
	 * clock period is 32kHz (31250ns)
	 * jitter is +/- 2% (20 per mille)
	 */
	ts_chip.clock_period = 31250;
	ts_chip.jitter = 20;
	ts_chip.init_period = inv_icm45600_odr_to_period(st->conf.gyro.odr);
	inv_sensors_timestamp_init(&gyro_st->ts, &ts_chip);

	iio_device_set_drvdata(indio_dev, st);
	indio_dev->name = name;
	indio_dev->info = &inv_icm45600_gyro_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = inv_icm45600_gyro_channels;
	indio_dev->num_channels = ARRAY_SIZE(inv_icm45600_gyro_channels);
	indio_dev->available_scan_masks = inv_icm45600_gyro_scan_masks;
	indio_dev->setup_ops = &inv_icm45600_buffer_ops;

	ret = devm_iio_kfifo_buffer_setup(dev, indio_dev,
					  &inv_icm45600_buffer_ops);
	if (ret)
		return ERR_PTR(ret);

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret)
		return ERR_PTR(ret);

	return indio_dev;
}

int inv_icm45600_gyro_parse_fifo(struct iio_dev *indio_dev)
{
	struct inv_icm45600_state *st = iio_device_get_drvdata(indio_dev);
	struct inv_icm45600_sensor_state *gyro_st = iio_priv(indio_dev);
	struct inv_sensors_timestamp *ts = &gyro_st->ts;
	ssize_t i, size;
	unsigned int no;

	/* parse all fifo packets */
	for (i = 0, no = 0; i < st->fifo.count; i += size, ++no) {
		struct inv_icm45600_gyro_buffer buffer = { };
		const struct inv_icm45600_fifo_sensor_data *accel, *gyro;
		const __le16 *timestamp;
		const s8 *temp;
		unsigned int odr;
		s64 ts_val;

		size = inv_icm45600_fifo_decode_packet(&st->fifo.data[i],
				&accel, &gyro, &temp, &timestamp, &odr);
		/* quit if error or FIFO is empty */
		if (size <= 0)
			return size;

		/* skip packet if no gyro data or data is invalid */
		if (gyro == NULL || !inv_icm45600_fifo_is_data_valid(gyro))
			continue;

		/* update odr */
		if (odr & INV_ICM45600_SENSOR_GYRO)
			inv_sensors_timestamp_apply_odr(ts, st->fifo.period,
							st->fifo.nb.total, no);

		memcpy(&buffer.gyro, gyro, sizeof(buffer.gyro));
		/* convert 8 bits FIFO temperature in high resolution format */
		buffer.temp = temp ? (*temp * 64) : 0;
		ts_val = inv_sensors_timestamp_pop(ts);
		iio_push_to_buffers_with_ts(indio_dev, &buffer, sizeof(buffer), ts_val);
	}

	return 0;
}
