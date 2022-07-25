// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020 Invensense, Inc.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/delay.h>
#include <linux/math64.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/kfifo_buf.h>

#include "inv_icm42600.h"
#include "inv_icm42600_temp.h"
#include "inv_icm42600_buffer.h"
#include "inv_icm42600_timestamp.h"

#define INV_ICM42600_ACCEL_CHAN(_modifier, _index, _ext_info)		\
	{								\
		.type = IIO_ACCEL,					\
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
			.endianness = IIO_BE,				\
		},							\
		.ext_info = _ext_info,					\
	}

enum inv_icm42600_accel_scan {
	INV_ICM42600_ACCEL_SCAN_X,
	INV_ICM42600_ACCEL_SCAN_Y,
	INV_ICM42600_ACCEL_SCAN_Z,
	INV_ICM42600_ACCEL_SCAN_TEMP,
	INV_ICM42600_ACCEL_SCAN_TIMESTAMP,
};

static const struct iio_chan_spec_ext_info inv_icm42600_accel_ext_infos[] = {
	IIO_MOUNT_MATRIX(IIO_SHARED_BY_ALL, inv_icm42600_get_mount_matrix),
	{},
};

static const struct iio_chan_spec inv_icm42600_accel_channels[] = {
	INV_ICM42600_ACCEL_CHAN(IIO_MOD_X, INV_ICM42600_ACCEL_SCAN_X,
				inv_icm42600_accel_ext_infos),
	INV_ICM42600_ACCEL_CHAN(IIO_MOD_Y, INV_ICM42600_ACCEL_SCAN_Y,
				inv_icm42600_accel_ext_infos),
	INV_ICM42600_ACCEL_CHAN(IIO_MOD_Z, INV_ICM42600_ACCEL_SCAN_Z,
				inv_icm42600_accel_ext_infos),
	INV_ICM42600_TEMP_CHAN(INV_ICM42600_ACCEL_SCAN_TEMP),
	IIO_CHAN_SOFT_TIMESTAMP(INV_ICM42600_ACCEL_SCAN_TIMESTAMP),
};

/*
 * IIO buffer data: size must be a power of 2 and timestamp aligned
 * 16 bytes: 6 bytes acceleration, 2 bytes temperature, 8 bytes timestamp
 */
struct inv_icm42600_accel_buffer {
	struct inv_icm42600_fifo_sensor_data accel;
	int16_t temp;
	int64_t timestamp __aligned(8);
};

#define INV_ICM42600_SCAN_MASK_ACCEL_3AXIS				\
	(BIT(INV_ICM42600_ACCEL_SCAN_X) |				\
	BIT(INV_ICM42600_ACCEL_SCAN_Y) |				\
	BIT(INV_ICM42600_ACCEL_SCAN_Z))

#define INV_ICM42600_SCAN_MASK_TEMP	BIT(INV_ICM42600_ACCEL_SCAN_TEMP)

static const unsigned long inv_icm42600_accel_scan_masks[] = {
	/* 3-axis accel + temperature */
	INV_ICM42600_SCAN_MASK_ACCEL_3AXIS | INV_ICM42600_SCAN_MASK_TEMP,
	0,
};

/* enable accelerometer sensor and FIFO write */
static int inv_icm42600_accel_update_scan_mode(struct iio_dev *indio_dev,
					       const unsigned long *scan_mask)
{
	struct inv_icm42600_state *st = iio_device_get_drvdata(indio_dev);
	struct inv_icm42600_timestamp *ts = iio_priv(indio_dev);
	struct inv_icm42600_sensor_conf conf = INV_ICM42600_SENSOR_CONF_INIT;
	unsigned int fifo_en = 0;
	unsigned int sleep_temp = 0;
	unsigned int sleep_accel = 0;
	unsigned int sleep;
	int ret;

	mutex_lock(&st->lock);

	if (*scan_mask & INV_ICM42600_SCAN_MASK_TEMP) {
		/* enable temp sensor */
		ret = inv_icm42600_set_temp_conf(st, true, &sleep_temp);
		if (ret)
			goto out_unlock;
		fifo_en |= INV_ICM42600_SENSOR_TEMP;
	}

	if (*scan_mask & INV_ICM42600_SCAN_MASK_ACCEL_3AXIS) {
		/* enable accel sensor */
		conf.mode = INV_ICM42600_SENSOR_MODE_LOW_NOISE;
		ret = inv_icm42600_set_accel_conf(st, &conf, &sleep_accel);
		if (ret)
			goto out_unlock;
		fifo_en |= INV_ICM42600_SENSOR_ACCEL;
	}

	/* update data FIFO write */
	inv_icm42600_timestamp_apply_odr(ts, 0, 0, 0);
	ret = inv_icm42600_buffer_set_fifo_en(st, fifo_en | st->fifo.en);
	if (ret)
		goto out_unlock;

	ret = inv_icm42600_buffer_update_watermark(st);

out_unlock:
	mutex_unlock(&st->lock);
	/* sleep maximum required time */
	if (sleep_accel > sleep_temp)
		sleep = sleep_accel;
	else
		sleep = sleep_temp;
	if (sleep)
		msleep(sleep);
	return ret;
}

static int inv_icm42600_accel_read_sensor(struct inv_icm42600_state *st,
					  struct iio_chan_spec const *chan,
					  int16_t *val)
{
	struct device *dev = regmap_get_device(st->map);
	struct inv_icm42600_sensor_conf conf = INV_ICM42600_SENSOR_CONF_INIT;
	unsigned int reg;
	__be16 *data;
	int ret;

	if (chan->type != IIO_ACCEL)
		return -EINVAL;

	switch (chan->channel2) {
	case IIO_MOD_X:
		reg = INV_ICM42600_REG_ACCEL_DATA_X;
		break;
	case IIO_MOD_Y:
		reg = INV_ICM42600_REG_ACCEL_DATA_Y;
		break;
	case IIO_MOD_Z:
		reg = INV_ICM42600_REG_ACCEL_DATA_Z;
		break;
	default:
		return -EINVAL;
	}

	pm_runtime_get_sync(dev);
	mutex_lock(&st->lock);

	/* enable accel sensor */
	conf.mode = INV_ICM42600_SENSOR_MODE_LOW_NOISE;
	ret = inv_icm42600_set_accel_conf(st, &conf, NULL);
	if (ret)
		goto exit;

	/* read accel register data */
	data = (__be16 *)&st->buffer[0];
	ret = regmap_bulk_read(st->map, reg, data, sizeof(*data));
	if (ret)
		goto exit;

	*val = (int16_t)be16_to_cpup(data);
	if (*val == INV_ICM42600_DATA_INVALID)
		ret = -EINVAL;
exit:
	mutex_unlock(&st->lock);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
	return ret;
}

/* IIO format int + nano */
static const int inv_icm42600_accel_scale[] = {
	/* +/- 16G => 0.004788403 m/s-2 */
	[2 * INV_ICM42600_ACCEL_FS_16G] = 0,
	[2 * INV_ICM42600_ACCEL_FS_16G + 1] = 4788403,
	/* +/- 8G => 0.002394202 m/s-2 */
	[2 * INV_ICM42600_ACCEL_FS_8G] = 0,
	[2 * INV_ICM42600_ACCEL_FS_8G + 1] = 2394202,
	/* +/- 4G => 0.001197101 m/s-2 */
	[2 * INV_ICM42600_ACCEL_FS_4G] = 0,
	[2 * INV_ICM42600_ACCEL_FS_4G + 1] = 1197101,
	/* +/- 2G => 0.000598550 m/s-2 */
	[2 * INV_ICM42600_ACCEL_FS_2G] = 0,
	[2 * INV_ICM42600_ACCEL_FS_2G + 1] = 598550,
};

static int inv_icm42600_accel_read_scale(struct inv_icm42600_state *st,
					 int *val, int *val2)
{
	unsigned int idx;

	idx = st->conf.accel.fs;

	*val = inv_icm42600_accel_scale[2 * idx];
	*val2 = inv_icm42600_accel_scale[2 * idx + 1];
	return IIO_VAL_INT_PLUS_NANO;
}

static int inv_icm42600_accel_write_scale(struct inv_icm42600_state *st,
					  int val, int val2)
{
	struct device *dev = regmap_get_device(st->map);
	unsigned int idx;
	struct inv_icm42600_sensor_conf conf = INV_ICM42600_SENSOR_CONF_INIT;
	int ret;

	for (idx = 0; idx < ARRAY_SIZE(inv_icm42600_accel_scale); idx += 2) {
		if (val == inv_icm42600_accel_scale[idx] &&
		    val2 == inv_icm42600_accel_scale[idx + 1])
			break;
	}
	if (idx >= ARRAY_SIZE(inv_icm42600_accel_scale))
		return -EINVAL;

	conf.fs = idx / 2;

	pm_runtime_get_sync(dev);
	mutex_lock(&st->lock);

	ret = inv_icm42600_set_accel_conf(st, &conf, NULL);

	mutex_unlock(&st->lock);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return ret;
}

/* IIO format int + micro */
static const int inv_icm42600_accel_odr[] = {
	/* 12.5Hz */
	12, 500000,
	/* 25Hz */
	25, 0,
	/* 50Hz */
	50, 0,
	/* 100Hz */
	100, 0,
	/* 200Hz */
	200, 0,
	/* 1kHz */
	1000, 0,
	/* 2kHz */
	2000, 0,
	/* 4kHz */
	4000, 0,
};

static const int inv_icm42600_accel_odr_conv[] = {
	INV_ICM42600_ODR_12_5HZ,
	INV_ICM42600_ODR_25HZ,
	INV_ICM42600_ODR_50HZ,
	INV_ICM42600_ODR_100HZ,
	INV_ICM42600_ODR_200HZ,
	INV_ICM42600_ODR_1KHZ_LN,
	INV_ICM42600_ODR_2KHZ_LN,
	INV_ICM42600_ODR_4KHZ_LN,
};

static int inv_icm42600_accel_read_odr(struct inv_icm42600_state *st,
				       int *val, int *val2)
{
	unsigned int odr;
	unsigned int i;

	odr = st->conf.accel.odr;

	for (i = 0; i < ARRAY_SIZE(inv_icm42600_accel_odr_conv); ++i) {
		if (inv_icm42600_accel_odr_conv[i] == odr)
			break;
	}
	if (i >= ARRAY_SIZE(inv_icm42600_accel_odr_conv))
		return -EINVAL;

	*val = inv_icm42600_accel_odr[2 * i];
	*val2 = inv_icm42600_accel_odr[2 * i + 1];

	return IIO_VAL_INT_PLUS_MICRO;
}

static int inv_icm42600_accel_write_odr(struct iio_dev *indio_dev,
					int val, int val2)
{
	struct inv_icm42600_state *st = iio_device_get_drvdata(indio_dev);
	struct inv_icm42600_timestamp *ts = iio_priv(indio_dev);
	struct device *dev = regmap_get_device(st->map);
	unsigned int idx;
	struct inv_icm42600_sensor_conf conf = INV_ICM42600_SENSOR_CONF_INIT;
	int ret;

	for (idx = 0; idx < ARRAY_SIZE(inv_icm42600_accel_odr); idx += 2) {
		if (val == inv_icm42600_accel_odr[idx] &&
		    val2 == inv_icm42600_accel_odr[idx + 1])
			break;
	}
	if (idx >= ARRAY_SIZE(inv_icm42600_accel_odr))
		return -EINVAL;

	conf.odr = inv_icm42600_accel_odr_conv[idx / 2];

	pm_runtime_get_sync(dev);
	mutex_lock(&st->lock);

	ret = inv_icm42600_timestamp_update_odr(ts, inv_icm42600_odr_to_period(conf.odr),
						iio_buffer_enabled(indio_dev));
	if (ret)
		goto out_unlock;

	ret = inv_icm42600_set_accel_conf(st, &conf, NULL);
	if (ret)
		goto out_unlock;
	inv_icm42600_buffer_update_fifo_period(st);
	inv_icm42600_buffer_update_watermark(st);

out_unlock:
	mutex_unlock(&st->lock);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return ret;
}

/*
 * Calibration bias values, IIO range format int + micro.
 * Value is limited to +/-1g coded on 12 bits signed. Step is 0.5mg.
 */
static int inv_icm42600_accel_calibbias[] = {
	-10, 42010,		/* min: -10.042010 m/s² */
	0, 4903,		/* step: 0.004903 m/s² */
	10, 37106,		/* max: 10.037106 m/s² */
};

static int inv_icm42600_accel_read_offset(struct inv_icm42600_state *st,
					  struct iio_chan_spec const *chan,
					  int *val, int *val2)
{
	struct device *dev = regmap_get_device(st->map);
	int64_t val64;
	int32_t bias;
	unsigned int reg;
	int16_t offset;
	uint8_t data[2];
	int ret;

	if (chan->type != IIO_ACCEL)
		return -EINVAL;

	switch (chan->channel2) {
	case IIO_MOD_X:
		reg = INV_ICM42600_REG_OFFSET_USER4;
		break;
	case IIO_MOD_Y:
		reg = INV_ICM42600_REG_OFFSET_USER6;
		break;
	case IIO_MOD_Z:
		reg = INV_ICM42600_REG_OFFSET_USER7;
		break;
	default:
		return -EINVAL;
	}

	pm_runtime_get_sync(dev);
	mutex_lock(&st->lock);

	ret = regmap_bulk_read(st->map, reg, st->buffer, sizeof(data));
	memcpy(data, st->buffer, sizeof(data));

	mutex_unlock(&st->lock);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
	if (ret)
		return ret;

	/* 12 bits signed value */
	switch (chan->channel2) {
	case IIO_MOD_X:
		offset = sign_extend32(((data[0] & 0xF0) << 4) | data[1], 11);
		break;
	case IIO_MOD_Y:
		offset = sign_extend32(((data[1] & 0x0F) << 8) | data[0], 11);
		break;
	case IIO_MOD_Z:
		offset = sign_extend32(((data[0] & 0xF0) << 4) | data[1], 11);
		break;
	default:
		return -EINVAL;
	}

	/*
	 * convert raw offset to g then to m/s²
	 * 12 bits signed raw step 0.5mg to g: 5 / 10000
	 * g to m/s²: 9.806650
	 * result in micro (1000000)
	 * (offset * 5 * 9.806650 * 1000000) / 10000
	 */
	val64 = (int64_t)offset * 5LL * 9806650LL;
	/* for rounding, add + or - divisor (10000) divided by 2 */
	if (val64 >= 0)
		val64 += 10000LL / 2LL;
	else
		val64 -= 10000LL / 2LL;
	bias = div_s64(val64, 10000L);
	*val = bias / 1000000L;
	*val2 = bias % 1000000L;

	return IIO_VAL_INT_PLUS_MICRO;
}

static int inv_icm42600_accel_write_offset(struct inv_icm42600_state *st,
					   struct iio_chan_spec const *chan,
					   int val, int val2)
{
	struct device *dev = regmap_get_device(st->map);
	int64_t val64;
	int32_t min, max;
	unsigned int reg, regval;
	int16_t offset;
	int ret;

	if (chan->type != IIO_ACCEL)
		return -EINVAL;

	switch (chan->channel2) {
	case IIO_MOD_X:
		reg = INV_ICM42600_REG_OFFSET_USER4;
		break;
	case IIO_MOD_Y:
		reg = INV_ICM42600_REG_OFFSET_USER6;
		break;
	case IIO_MOD_Z:
		reg = INV_ICM42600_REG_OFFSET_USER7;
		break;
	default:
		return -EINVAL;
	}

	/* inv_icm42600_accel_calibbias: min - step - max in micro */
	min = inv_icm42600_accel_calibbias[0] * 1000000L +
	      inv_icm42600_accel_calibbias[1];
	max = inv_icm42600_accel_calibbias[4] * 1000000L +
	      inv_icm42600_accel_calibbias[5];
	val64 = (int64_t)val * 1000000LL + (int64_t)val2;
	if (val64 < min || val64 > max)
		return -EINVAL;

	/*
	 * convert m/s² to g then to raw value
	 * m/s² to g: 1 / 9.806650
	 * g to raw 12 bits signed, step 0.5mg: 10000 / 5
	 * val in micro (1000000)
	 * val * 10000 / (9.806650 * 1000000 * 5)
	 */
	val64 = val64 * 10000LL;
	/* for rounding, add + or - divisor (9806650 * 5) divided by 2 */
	if (val64 >= 0)
		val64 += 9806650 * 5 / 2;
	else
		val64 -= 9806650 * 5 / 2;
	offset = div_s64(val64, 9806650 * 5);

	/* clamp value limited to 12 bits signed */
	if (offset < -2048)
		offset = -2048;
	else if (offset > 2047)
		offset = 2047;

	pm_runtime_get_sync(dev);
	mutex_lock(&st->lock);

	switch (chan->channel2) {
	case IIO_MOD_X:
		/* OFFSET_USER4 register is shared */
		ret = regmap_read(st->map, INV_ICM42600_REG_OFFSET_USER4,
				  &regval);
		if (ret)
			goto out_unlock;
		st->buffer[0] = ((offset & 0xF00) >> 4) | (regval & 0x0F);
		st->buffer[1] = offset & 0xFF;
		break;
	case IIO_MOD_Y:
		/* OFFSET_USER7 register is shared */
		ret = regmap_read(st->map, INV_ICM42600_REG_OFFSET_USER7,
				  &regval);
		if (ret)
			goto out_unlock;
		st->buffer[0] = offset & 0xFF;
		st->buffer[1] = ((offset & 0xF00) >> 8) | (regval & 0xF0);
		break;
	case IIO_MOD_Z:
		/* OFFSET_USER7 register is shared */
		ret = regmap_read(st->map, INV_ICM42600_REG_OFFSET_USER7,
				  &regval);
		if (ret)
			goto out_unlock;
		st->buffer[0] = ((offset & 0xF00) >> 4) | (regval & 0x0F);
		st->buffer[1] = offset & 0xFF;
		break;
	default:
		ret = -EINVAL;
		goto out_unlock;
	}

	ret = regmap_bulk_write(st->map, reg, st->buffer, 2);

out_unlock:
	mutex_unlock(&st->lock);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
	return ret;
}

static int inv_icm42600_accel_read_raw(struct iio_dev *indio_dev,
				       struct iio_chan_spec const *chan,
				       int *val, int *val2, long mask)
{
	struct inv_icm42600_state *st = iio_device_get_drvdata(indio_dev);
	int16_t data;
	int ret;

	switch (chan->type) {
	case IIO_ACCEL:
		break;
	case IIO_TEMP:
		return inv_icm42600_temp_read_raw(indio_dev, chan, val, val2, mask);
	default:
		return -EINVAL;
	}

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;
		ret = inv_icm42600_accel_read_sensor(st, chan, &data);
		iio_device_release_direct_mode(indio_dev);
		if (ret)
			return ret;
		*val = data;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		return inv_icm42600_accel_read_scale(st, val, val2);
	case IIO_CHAN_INFO_SAMP_FREQ:
		return inv_icm42600_accel_read_odr(st, val, val2);
	case IIO_CHAN_INFO_CALIBBIAS:
		return inv_icm42600_accel_read_offset(st, chan, val, val2);
	default:
		return -EINVAL;
	}
}

static int inv_icm42600_accel_read_avail(struct iio_dev *indio_dev,
					 struct iio_chan_spec const *chan,
					 const int **vals,
					 int *type, int *length, long mask)
{
	if (chan->type != IIO_ACCEL)
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		*vals = inv_icm42600_accel_scale;
		*type = IIO_VAL_INT_PLUS_NANO;
		*length = ARRAY_SIZE(inv_icm42600_accel_scale);
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*vals = inv_icm42600_accel_odr;
		*type = IIO_VAL_INT_PLUS_MICRO;
		*length = ARRAY_SIZE(inv_icm42600_accel_odr);
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_CALIBBIAS:
		*vals = inv_icm42600_accel_calibbias;
		*type = IIO_VAL_INT_PLUS_MICRO;
		return IIO_AVAIL_RANGE;
	default:
		return -EINVAL;
	}
}

static int inv_icm42600_accel_write_raw(struct iio_dev *indio_dev,
					struct iio_chan_spec const *chan,
					int val, int val2, long mask)
{
	struct inv_icm42600_state *st = iio_device_get_drvdata(indio_dev);
	int ret;

	if (chan->type != IIO_ACCEL)
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;
		ret = inv_icm42600_accel_write_scale(st, val, val2);
		iio_device_release_direct_mode(indio_dev);
		return ret;
	case IIO_CHAN_INFO_SAMP_FREQ:
		return inv_icm42600_accel_write_odr(indio_dev, val, val2);
	case IIO_CHAN_INFO_CALIBBIAS:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;
		ret = inv_icm42600_accel_write_offset(st, chan, val, val2);
		iio_device_release_direct_mode(indio_dev);
		return ret;
	default:
		return -EINVAL;
	}
}

static int inv_icm42600_accel_write_raw_get_fmt(struct iio_dev *indio_dev,
						struct iio_chan_spec const *chan,
						long mask)
{
	if (chan->type != IIO_ACCEL)
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		return IIO_VAL_INT_PLUS_NANO;
	case IIO_CHAN_INFO_SAMP_FREQ:
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_CALIBBIAS:
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int inv_icm42600_accel_hwfifo_set_watermark(struct iio_dev *indio_dev,
						   unsigned int val)
{
	struct inv_icm42600_state *st = iio_device_get_drvdata(indio_dev);
	int ret;

	mutex_lock(&st->lock);

	st->fifo.watermark.accel = val;
	ret = inv_icm42600_buffer_update_watermark(st);

	mutex_unlock(&st->lock);

	return ret;
}

static int inv_icm42600_accel_hwfifo_flush(struct iio_dev *indio_dev,
					   unsigned int count)
{
	struct inv_icm42600_state *st = iio_device_get_drvdata(indio_dev);
	int ret;

	if (count == 0)
		return 0;

	mutex_lock(&st->lock);

	ret = inv_icm42600_buffer_hwfifo_flush(st, count);
	if (!ret)
		ret = st->fifo.nb.accel;

	mutex_unlock(&st->lock);

	return ret;
}

static const struct iio_info inv_icm42600_accel_info = {
	.read_raw = inv_icm42600_accel_read_raw,
	.read_avail = inv_icm42600_accel_read_avail,
	.write_raw = inv_icm42600_accel_write_raw,
	.write_raw_get_fmt = inv_icm42600_accel_write_raw_get_fmt,
	.debugfs_reg_access = inv_icm42600_debugfs_reg,
	.update_scan_mode = inv_icm42600_accel_update_scan_mode,
	.hwfifo_set_watermark = inv_icm42600_accel_hwfifo_set_watermark,
	.hwfifo_flush_to_buffer = inv_icm42600_accel_hwfifo_flush,
};

struct iio_dev *inv_icm42600_accel_init(struct inv_icm42600_state *st)
{
	struct device *dev = regmap_get_device(st->map);
	const char *name;
	struct inv_icm42600_timestamp *ts;
	struct iio_dev *indio_dev;
	int ret;

	name = devm_kasprintf(dev, GFP_KERNEL, "%s-accel", st->name);
	if (!name)
		return ERR_PTR(-ENOMEM);

	indio_dev = devm_iio_device_alloc(dev, sizeof(*ts));
	if (!indio_dev)
		return ERR_PTR(-ENOMEM);

	ts = iio_priv(indio_dev);
	inv_icm42600_timestamp_init(ts, inv_icm42600_odr_to_period(st->conf.accel.odr));

	iio_device_set_drvdata(indio_dev, st);
	indio_dev->name = name;
	indio_dev->info = &inv_icm42600_accel_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = inv_icm42600_accel_channels;
	indio_dev->num_channels = ARRAY_SIZE(inv_icm42600_accel_channels);
	indio_dev->available_scan_masks = inv_icm42600_accel_scan_masks;

	ret = devm_iio_kfifo_buffer_setup(dev, indio_dev,
					  &inv_icm42600_buffer_ops);
	if (ret)
		return ERR_PTR(ret);

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret)
		return ERR_PTR(ret);

	return indio_dev;
}

int inv_icm42600_accel_parse_fifo(struct iio_dev *indio_dev)
{
	struct inv_icm42600_state *st = iio_device_get_drvdata(indio_dev);
	struct inv_icm42600_timestamp *ts = iio_priv(indio_dev);
	ssize_t i, size;
	unsigned int no;
	const void *accel, *gyro, *timestamp;
	const int8_t *temp;
	unsigned int odr;
	int64_t ts_val;
	struct inv_icm42600_accel_buffer buffer;

	/* parse all fifo packets */
	for (i = 0, no = 0; i < st->fifo.count; i += size, ++no) {
		size = inv_icm42600_fifo_decode_packet(&st->fifo.data[i],
				&accel, &gyro, &temp, &timestamp, &odr);
		/* quit if error or FIFO is empty */
		if (size <= 0)
			return size;

		/* skip packet if no accel data or data is invalid */
		if (accel == NULL || !inv_icm42600_fifo_is_data_valid(accel))
			continue;

		/* update odr */
		if (odr & INV_ICM42600_SENSOR_ACCEL)
			inv_icm42600_timestamp_apply_odr(ts, st->fifo.period,
							 st->fifo.nb.total, no);

		/* buffer is copied to userspace, zeroing it to avoid any data leak */
		memset(&buffer, 0, sizeof(buffer));
		memcpy(&buffer.accel, accel, sizeof(buffer.accel));
		/* convert 8 bits FIFO temperature in high resolution format */
		buffer.temp = temp ? (*temp * 64) : 0;
		ts_val = inv_icm42600_timestamp_pop(ts);
		iio_push_to_buffers_with_timestamp(indio_dev, &buffer, ts_val);
	}

	return 0;
}
