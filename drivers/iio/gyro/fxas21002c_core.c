// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for NXP FXAS21002C Gyroscope - Core
 *
 * Copyright (C) 2019 Linaro Ltd.
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#include "fxas21002c.h"

#define FXAS21002C_CHIP_ID_1	0xD6
#define FXAS21002C_CHIP_ID_2	0xD7

enum fxas21002c_mode_state {
	FXAS21002C_MODE_STANDBY,
	FXAS21002C_MODE_READY,
	FXAS21002C_MODE_ACTIVE,
};

#define FXAS21002C_STANDBY_ACTIVE_TIME_MS	62
#define FXAS21002C_READY_ACTIVE_TIME_MS		7

#define FXAS21002C_ODR_LIST_MAX		10

#define FXAS21002C_SCALE_FRACTIONAL	32
#define FXAS21002C_RANGE_LIMIT_DOUBLE	2000

#define FXAS21002C_AXIS_TO_REG(axis) (FXAS21002C_REG_OUT_X_MSB + ((axis) * 2))

static const struct reg_field fxas21002c_reg_fields[] = {
	[F_DR_STATUS]		= REG_FIELD(FXAS21002C_REG_STATUS, 0, 7),
	[F_OUT_X_MSB]		= REG_FIELD(FXAS21002C_REG_OUT_X_MSB, 0, 7),
	[F_OUT_X_LSB]		= REG_FIELD(FXAS21002C_REG_OUT_X_LSB, 0, 7),
	[F_OUT_Y_MSB]		= REG_FIELD(FXAS21002C_REG_OUT_Y_MSB, 0, 7),
	[F_OUT_Y_LSB]		= REG_FIELD(FXAS21002C_REG_OUT_Y_LSB, 0, 7),
	[F_OUT_Z_MSB]		= REG_FIELD(FXAS21002C_REG_OUT_Z_MSB, 0, 7),
	[F_OUT_Z_LSB]		= REG_FIELD(FXAS21002C_REG_OUT_Z_LSB, 0, 7),
	[F_ZYX_OW]		= REG_FIELD(FXAS21002C_REG_DR_STATUS, 7, 7),
	[F_Z_OW]		= REG_FIELD(FXAS21002C_REG_DR_STATUS, 6, 6),
	[F_Y_OW]		= REG_FIELD(FXAS21002C_REG_DR_STATUS, 5, 5),
	[F_X_OW]		= REG_FIELD(FXAS21002C_REG_DR_STATUS, 4, 4),
	[F_ZYX_DR]		= REG_FIELD(FXAS21002C_REG_DR_STATUS, 3, 3),
	[F_Z_DR]		= REG_FIELD(FXAS21002C_REG_DR_STATUS, 2, 2),
	[F_Y_DR]		= REG_FIELD(FXAS21002C_REG_DR_STATUS, 1, 1),
	[F_X_DR]		= REG_FIELD(FXAS21002C_REG_DR_STATUS, 0, 0),
	[F_OVF]			= REG_FIELD(FXAS21002C_REG_F_STATUS, 7, 7),
	[F_WMKF]		= REG_FIELD(FXAS21002C_REG_F_STATUS, 6, 6),
	[F_CNT]			= REG_FIELD(FXAS21002C_REG_F_STATUS, 0, 5),
	[F_MODE]		= REG_FIELD(FXAS21002C_REG_F_SETUP, 6, 7),
	[F_WMRK]		= REG_FIELD(FXAS21002C_REG_F_SETUP, 0, 5),
	[F_EVENT]		= REG_FIELD(FXAS21002C_REG_F_EVENT, 5, 5),
	[FE_TIME]		= REG_FIELD(FXAS21002C_REG_F_EVENT, 0, 4),
	[F_BOOTEND]		= REG_FIELD(FXAS21002C_REG_INT_SRC_FLAG, 3, 3),
	[F_SRC_FIFO]		= REG_FIELD(FXAS21002C_REG_INT_SRC_FLAG, 2, 2),
	[F_SRC_RT]		= REG_FIELD(FXAS21002C_REG_INT_SRC_FLAG, 1, 1),
	[F_SRC_DRDY]		= REG_FIELD(FXAS21002C_REG_INT_SRC_FLAG, 0, 0),
	[F_WHO_AM_I]		= REG_FIELD(FXAS21002C_REG_WHO_AM_I, 0, 7),
	[F_BW]			= REG_FIELD(FXAS21002C_REG_CTRL0, 6, 7),
	[F_SPIW]		= REG_FIELD(FXAS21002C_REG_CTRL0, 5, 5),
	[F_SEL]			= REG_FIELD(FXAS21002C_REG_CTRL0, 3, 4),
	[F_HPF_EN]		= REG_FIELD(FXAS21002C_REG_CTRL0, 2, 2),
	[F_FS]			= REG_FIELD(FXAS21002C_REG_CTRL0, 0, 1),
	[F_ELE]			= REG_FIELD(FXAS21002C_REG_RT_CFG, 3, 3),
	[F_ZTEFE]		= REG_FIELD(FXAS21002C_REG_RT_CFG, 2, 2),
	[F_YTEFE]		= REG_FIELD(FXAS21002C_REG_RT_CFG, 1, 1),
	[F_XTEFE]		= REG_FIELD(FXAS21002C_REG_RT_CFG, 0, 0),
	[F_EA]			= REG_FIELD(FXAS21002C_REG_RT_SRC, 6, 6),
	[F_ZRT]			= REG_FIELD(FXAS21002C_REG_RT_SRC, 5, 5),
	[F_ZRT_POL]		= REG_FIELD(FXAS21002C_REG_RT_SRC, 4, 4),
	[F_YRT]			= REG_FIELD(FXAS21002C_REG_RT_SRC, 3, 3),
	[F_YRT_POL]		= REG_FIELD(FXAS21002C_REG_RT_SRC, 2, 2),
	[F_XRT]			= REG_FIELD(FXAS21002C_REG_RT_SRC, 1, 1),
	[F_XRT_POL]		= REG_FIELD(FXAS21002C_REG_RT_SRC, 0, 0),
	[F_DBCNTM]		= REG_FIELD(FXAS21002C_REG_RT_THS, 7, 7),
	[F_THS]			= REG_FIELD(FXAS21002C_REG_RT_SRC, 0, 6),
	[F_RT_COUNT]		= REG_FIELD(FXAS21002C_REG_RT_COUNT, 0, 7),
	[F_TEMP]		= REG_FIELD(FXAS21002C_REG_TEMP, 0, 7),
	[F_RST]			= REG_FIELD(FXAS21002C_REG_CTRL1, 6, 6),
	[F_ST]			= REG_FIELD(FXAS21002C_REG_CTRL1, 5, 5),
	[F_DR]			= REG_FIELD(FXAS21002C_REG_CTRL1, 2, 4),
	[F_ACTIVE]		= REG_FIELD(FXAS21002C_REG_CTRL1, 1, 1),
	[F_READY]		= REG_FIELD(FXAS21002C_REG_CTRL1, 0, 0),
	[F_INT_CFG_FIFO]	= REG_FIELD(FXAS21002C_REG_CTRL2, 7, 7),
	[F_INT_EN_FIFO]		= REG_FIELD(FXAS21002C_REG_CTRL2, 6, 6),
	[F_INT_CFG_RT]		= REG_FIELD(FXAS21002C_REG_CTRL2, 5, 5),
	[F_INT_EN_RT]		= REG_FIELD(FXAS21002C_REG_CTRL2, 4, 4),
	[F_INT_CFG_DRDY]	= REG_FIELD(FXAS21002C_REG_CTRL2, 3, 3),
	[F_INT_EN_DRDY]		= REG_FIELD(FXAS21002C_REG_CTRL2, 2, 2),
	[F_IPOL]		= REG_FIELD(FXAS21002C_REG_CTRL2, 1, 1),
	[F_PP_OD]		= REG_FIELD(FXAS21002C_REG_CTRL2, 0, 0),
	[F_WRAPTOONE]		= REG_FIELD(FXAS21002C_REG_CTRL3, 3, 3),
	[F_EXTCTRLEN]		= REG_FIELD(FXAS21002C_REG_CTRL3, 2, 2),
	[F_FS_DOUBLE]		= REG_FIELD(FXAS21002C_REG_CTRL3, 0, 0),
};

static const int fxas21002c_odr_values[] = {
	800, 400, 200, 100, 50, 25, 12, 12
};

/*
 * These values are taken from the low-pass filter cutoff frequency calculated
 * ODR * 0.lpf_values. So, for ODR = 800Hz with a lpf value = 0.32
 * => LPF cutoff frequency = 800 * 0.32 = 256 Hz
 */
static const int fxas21002c_lpf_values[] = {
	32, 16, 8
};

/*
 * These values are taken from the high-pass filter cutoff frequency calculated
 * ODR * 0.0hpf_values. So, for ODR = 800Hz with a hpf value = 0.018750
 * => HPF cutoff frequency = 800 * 0.018750 = 15 Hz
 */
static const int fxas21002c_hpf_values[] = {
	18750, 9625, 4875, 2475
};

static const int fxas21002c_range_values[] = {
	4000, 2000, 1000, 500, 250
};

struct fxas21002c_data {
	u8 chip_id;
	enum fxas21002c_mode_state mode;
	enum fxas21002c_mode_state prev_mode;

	struct mutex lock;		/* serialize data access */
	struct regmap *regmap;
	struct regmap_field *regmap_fields[F_MAX_FIELDS];
	struct iio_trigger *dready_trig;
	s64 timestamp;
	int irq;

	struct regulator *vdd;
	struct regulator *vddio;

	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	s16 buffer[8] ____cacheline_aligned;
};

enum fxas21002c_channel_index {
	CHANNEL_SCAN_INDEX_X,
	CHANNEL_SCAN_INDEX_Y,
	CHANNEL_SCAN_INDEX_Z,
	CHANNEL_SCAN_MAX,
};

static int fxas21002c_odr_hz_from_value(struct fxas21002c_data *data, u8 value)
{
	int odr_value_max = ARRAY_SIZE(fxas21002c_odr_values) - 1;

	value = min_t(u8, value, odr_value_max);

	return fxas21002c_odr_values[value];
}

static int fxas21002c_odr_value_from_hz(struct fxas21002c_data *data,
					unsigned int hz)
{
	int odr_table_size = ARRAY_SIZE(fxas21002c_odr_values);
	int i;

	for (i = 0; i < odr_table_size; i++)
		if (fxas21002c_odr_values[i] == hz)
			return i;

	return -EINVAL;
}

static int fxas21002c_lpf_bw_from_value(struct fxas21002c_data *data, u8 value)
{
	int lpf_value_max = ARRAY_SIZE(fxas21002c_lpf_values) - 1;

	value = min_t(u8, value, lpf_value_max);

	return fxas21002c_lpf_values[value];
}

static int fxas21002c_lpf_value_from_bw(struct fxas21002c_data *data,
					unsigned int hz)
{
	int lpf_table_size = ARRAY_SIZE(fxas21002c_lpf_values);
	int i;

	for (i = 0; i < lpf_table_size; i++)
		if (fxas21002c_lpf_values[i] == hz)
			return i;

	return -EINVAL;
}

static int fxas21002c_hpf_sel_from_value(struct fxas21002c_data *data, u8 value)
{
	int hpf_value_max = ARRAY_SIZE(fxas21002c_hpf_values) - 1;

	value = min_t(u8, value, hpf_value_max);

	return fxas21002c_hpf_values[value];
}

static int fxas21002c_hpf_value_from_sel(struct fxas21002c_data *data,
					 unsigned int hz)
{
	int hpf_table_size = ARRAY_SIZE(fxas21002c_hpf_values);
	int i;

	for (i = 0; i < hpf_table_size; i++)
		if (fxas21002c_hpf_values[i] == hz)
			return i;

	return -EINVAL;
}

static int fxas21002c_range_fs_from_value(struct fxas21002c_data *data,
					  u8 value)
{
	int range_value_max = ARRAY_SIZE(fxas21002c_range_values) - 1;
	unsigned int fs_double;
	int ret;

	/* We need to check if FS_DOUBLE is enabled to offset the value */
	ret = regmap_field_read(data->regmap_fields[F_FS_DOUBLE], &fs_double);
	if (ret < 0)
		return ret;

	if (!fs_double)
		value += 1;

	value = min_t(u8, value, range_value_max);

	return fxas21002c_range_values[value];
}

static int fxas21002c_range_value_from_fs(struct fxas21002c_data *data,
					  unsigned int range)
{
	int range_table_size = ARRAY_SIZE(fxas21002c_range_values);
	bool found = false;
	int fs_double = 0;
	int ret;
	int i;

	for (i = 0; i < range_table_size; i++)
		if (fxas21002c_range_values[i] == range) {
			found = true;
			break;
		}

	if (!found)
		return -EINVAL;

	if (range > FXAS21002C_RANGE_LIMIT_DOUBLE)
		fs_double = 1;

	ret = regmap_field_write(data->regmap_fields[F_FS_DOUBLE], fs_double);
	if (ret < 0)
		return ret;

	return i;
}

static int fxas21002c_mode_get(struct fxas21002c_data *data)
{
	unsigned int active;
	unsigned int ready;
	int ret;

	ret = regmap_field_read(data->regmap_fields[F_ACTIVE], &active);
	if (ret < 0)
		return ret;
	if (active)
		return FXAS21002C_MODE_ACTIVE;

	ret = regmap_field_read(data->regmap_fields[F_READY], &ready);
	if (ret < 0)
		return ret;
	if (ready)
		return FXAS21002C_MODE_READY;

	return FXAS21002C_MODE_STANDBY;
}

static int fxas21002c_mode_set(struct fxas21002c_data *data,
			       enum fxas21002c_mode_state mode)
{
	int ret;

	if (mode == data->mode)
		return 0;

	if (mode == FXAS21002C_MODE_READY)
		ret = regmap_field_write(data->regmap_fields[F_READY], 1);
	else
		ret = regmap_field_write(data->regmap_fields[F_READY], 0);
	if (ret < 0)
		return ret;

	if (mode == FXAS21002C_MODE_ACTIVE)
		ret = regmap_field_write(data->regmap_fields[F_ACTIVE], 1);
	else
		ret = regmap_field_write(data->regmap_fields[F_ACTIVE], 0);
	if (ret < 0)
		return ret;

	/* if going to active wait the setup times */
	if (mode == FXAS21002C_MODE_ACTIVE &&
	    data->mode == FXAS21002C_MODE_STANDBY)
		msleep_interruptible(FXAS21002C_STANDBY_ACTIVE_TIME_MS);

	if (data->mode == FXAS21002C_MODE_READY)
		msleep_interruptible(FXAS21002C_READY_ACTIVE_TIME_MS);

	data->prev_mode = data->mode;
	data->mode = mode;

	return ret;
}

static int fxas21002c_write(struct fxas21002c_data *data,
			    enum fxas21002c_fields field, int bits)
{
	int actual_mode;
	int ret;

	mutex_lock(&data->lock);

	actual_mode = fxas21002c_mode_get(data);
	if (actual_mode < 0) {
		ret = actual_mode;
		goto out_unlock;
	}

	ret = fxas21002c_mode_set(data, FXAS21002C_MODE_READY);
	if (ret < 0)
		goto out_unlock;

	ret = regmap_field_write(data->regmap_fields[field], bits);
	if (ret < 0)
		goto out_unlock;

	ret = fxas21002c_mode_set(data, data->prev_mode);

out_unlock:
	mutex_unlock(&data->lock);

	return ret;
}

static int  fxas21002c_pm_get(struct fxas21002c_data *data)
{
	return pm_runtime_resume_and_get(regmap_get_device(data->regmap));
}

static int  fxas21002c_pm_put(struct fxas21002c_data *data)
{
	struct device *dev = regmap_get_device(data->regmap);

	pm_runtime_mark_last_busy(dev);

	return pm_runtime_put_autosuspend(dev);
}

static int fxas21002c_temp_get(struct fxas21002c_data *data, int *val)
{
	struct device *dev = regmap_get_device(data->regmap);
	unsigned int temp;
	int ret;

	mutex_lock(&data->lock);
	ret = fxas21002c_pm_get(data);
	if (ret < 0)
		goto data_unlock;

	ret = regmap_field_read(data->regmap_fields[F_TEMP], &temp);
	if (ret < 0) {
		dev_err(dev, "failed to read temp: %d\n", ret);
		fxas21002c_pm_put(data);
		goto data_unlock;
	}

	*val = sign_extend32(temp, 7);

	ret = fxas21002c_pm_put(data);
	if (ret < 0)
		goto data_unlock;

	ret = IIO_VAL_INT;

data_unlock:
	mutex_unlock(&data->lock);

	return ret;
}

static int fxas21002c_axis_get(struct fxas21002c_data *data,
			       int index, int *val)
{
	struct device *dev = regmap_get_device(data->regmap);
	__be16 axis_be;
	int ret;

	mutex_lock(&data->lock);
	ret = fxas21002c_pm_get(data);
	if (ret < 0)
		goto data_unlock;

	ret = regmap_bulk_read(data->regmap, FXAS21002C_AXIS_TO_REG(index),
			       &axis_be, sizeof(axis_be));
	if (ret < 0) {
		dev_err(dev, "failed to read axis: %d: %d\n", index, ret);
		fxas21002c_pm_put(data);
		goto data_unlock;
	}

	*val = sign_extend32(be16_to_cpu(axis_be), 15);

	ret = fxas21002c_pm_put(data);
	if (ret < 0)
		goto data_unlock;

	ret = IIO_VAL_INT;

data_unlock:
	mutex_unlock(&data->lock);

	return ret;
}

static int fxas21002c_odr_get(struct fxas21002c_data *data, int *odr)
{
	unsigned int odr_bits;
	int ret;

	mutex_lock(&data->lock);
	ret = regmap_field_read(data->regmap_fields[F_DR], &odr_bits);
	if (ret < 0)
		goto data_unlock;

	*odr = fxas21002c_odr_hz_from_value(data, odr_bits);

	ret = IIO_VAL_INT;

data_unlock:
	mutex_unlock(&data->lock);

	return ret;
}

static int fxas21002c_odr_set(struct fxas21002c_data *data, int odr)
{
	int odr_bits;

	odr_bits = fxas21002c_odr_value_from_hz(data, odr);
	if (odr_bits < 0)
		return odr_bits;

	return fxas21002c_write(data, F_DR, odr_bits);
}

static int fxas21002c_lpf_get(struct fxas21002c_data *data, int *val2)
{
	unsigned int bw_bits;
	int ret;

	mutex_lock(&data->lock);
	ret = regmap_field_read(data->regmap_fields[F_BW], &bw_bits);
	if (ret < 0)
		goto data_unlock;

	*val2 = fxas21002c_lpf_bw_from_value(data, bw_bits) * 10000;

	ret = IIO_VAL_INT_PLUS_MICRO;

data_unlock:
	mutex_unlock(&data->lock);

	return ret;
}

static int fxas21002c_lpf_set(struct fxas21002c_data *data, int bw)
{
	int bw_bits;
	int odr;
	int ret;

	bw_bits = fxas21002c_lpf_value_from_bw(data, bw);
	if (bw_bits < 0)
		return bw_bits;

	/*
	 * From table 33 of the device spec, for ODR = 25Hz and 12.5 value 0.08
	 * is not allowed and for ODR = 12.5 value 0.16 is also not allowed
	 */
	ret = fxas21002c_odr_get(data, &odr);
	if (ret < 0)
		return -EINVAL;

	if ((odr == 25 && bw_bits > 0x01) || (odr == 12 && bw_bits > 0))
		return -EINVAL;

	return fxas21002c_write(data, F_BW, bw_bits);
}

static int fxas21002c_hpf_get(struct fxas21002c_data *data, int *val2)
{
	unsigned int sel_bits;
	int ret;

	mutex_lock(&data->lock);
	ret = regmap_field_read(data->regmap_fields[F_SEL], &sel_bits);
	if (ret < 0)
		goto data_unlock;

	*val2 = fxas21002c_hpf_sel_from_value(data, sel_bits);

	ret = IIO_VAL_INT_PLUS_MICRO;

data_unlock:
	mutex_unlock(&data->lock);

	return ret;
}

static int fxas21002c_hpf_set(struct fxas21002c_data *data, int sel)
{
	int sel_bits;

	sel_bits = fxas21002c_hpf_value_from_sel(data, sel);
	if (sel_bits < 0)
		return sel_bits;

	return fxas21002c_write(data, F_SEL, sel_bits);
}

static int fxas21002c_scale_get(struct fxas21002c_data *data, int *val)
{
	int fs_bits;
	int scale;
	int ret;

	mutex_lock(&data->lock);
	ret = regmap_field_read(data->regmap_fields[F_FS], &fs_bits);
	if (ret < 0)
		goto data_unlock;

	scale = fxas21002c_range_fs_from_value(data, fs_bits);
	if (scale < 0) {
		ret = scale;
		goto data_unlock;
	}

	*val = scale;

data_unlock:
	mutex_unlock(&data->lock);

	return ret;
}

static int fxas21002c_scale_set(struct fxas21002c_data *data, int range)
{
	int fs_bits;

	fs_bits = fxas21002c_range_value_from_fs(data, range);
	if (fs_bits < 0)
		return fs_bits;

	return fxas21002c_write(data, F_FS, fs_bits);
}

static int fxas21002c_read_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan, int *val,
			       int *val2, long mask)
{
	struct fxas21002c_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_TEMP:
			return fxas21002c_temp_get(data, val);
		case IIO_ANGL_VEL:
			return fxas21002c_axis_get(data, chan->scan_index, val);
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			*val2 = FXAS21002C_SCALE_FRACTIONAL;
			ret = fxas21002c_scale_get(data, val);
			if (ret < 0)
				return ret;

			return IIO_VAL_FRACTIONAL;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		*val = 0;
		return fxas21002c_lpf_get(data, val2);
	case IIO_CHAN_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY:
		*val = 0;
		return fxas21002c_hpf_get(data, val2);
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val2 = 0;
		return fxas21002c_odr_get(data, val);
	default:
		return -EINVAL;
	}
}

static int fxas21002c_write_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan, int val,
				int val2, long mask)
{
	struct fxas21002c_data *data = iio_priv(indio_dev);
	int range;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		if (val2)
			return -EINVAL;

		return fxas21002c_odr_set(data, val);
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		if (val)
			return -EINVAL;

		val2 = val2 / 10000;
		return fxas21002c_lpf_set(data, val2);
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			range = (((val * 1000 + val2 / 1000) *
				  FXAS21002C_SCALE_FRACTIONAL) / 1000);
			return fxas21002c_scale_set(data, range);
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY:
		return fxas21002c_hpf_set(data, val2);
	default:
		return -EINVAL;
	}
}

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("12.5 25 50 100 200 400 800");

static IIO_CONST_ATTR(in_anglvel_filter_low_pass_3db_frequency_available,
		      "0.32 0.16 0.08");

static IIO_CONST_ATTR(in_anglvel_filter_high_pass_3db_frequency_available,
		      "0.018750 0.009625 0.004875 0.002475");

static IIO_CONST_ATTR(in_anglvel_scale_available,
		      "125.0 62.5 31.25 15.625 7.8125");

static struct attribute *fxas21002c_attributes[] = {
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	&iio_const_attr_in_anglvel_filter_low_pass_3db_frequency_available.dev_attr.attr,
	&iio_const_attr_in_anglvel_filter_high_pass_3db_frequency_available.dev_attr.attr,
	&iio_const_attr_in_anglvel_scale_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group fxas21002c_attrs_group = {
	.attrs = fxas21002c_attributes,
};

#define FXAS21002C_CHANNEL(_axis) {					\
	.type = IIO_ANGL_VEL,						\
	.modified = 1,							\
	.channel2 = IIO_MOD_##_axis,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),			\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |		\
		BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY) |	\
		BIT(IIO_CHAN_INFO_HIGH_PASS_FILTER_3DB_FREQUENCY) |	\
		BIT(IIO_CHAN_INFO_SAMP_FREQ),				\
	.scan_index = CHANNEL_SCAN_INDEX_##_axis,			\
	.scan_type = {							\
		.sign = 's',						\
		.realbits = 16,						\
		.storagebits = 16,					\
		.endianness = IIO_BE,					\
	},								\
}

static const struct iio_chan_spec fxas21002c_channels[] = {
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.scan_index = -1,
	},
	FXAS21002C_CHANNEL(X),
	FXAS21002C_CHANNEL(Y),
	FXAS21002C_CHANNEL(Z),
};

static const struct iio_info fxas21002c_info = {
	.attrs			= &fxas21002c_attrs_group,
	.read_raw		= &fxas21002c_read_raw,
	.write_raw		= &fxas21002c_write_raw,
};

static irqreturn_t fxas21002c_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct fxas21002c_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->lock);
	ret = regmap_bulk_read(data->regmap, FXAS21002C_REG_OUT_X_MSB,
			       data->buffer, CHANNEL_SCAN_MAX * sizeof(s16));
	if (ret < 0)
		goto out_unlock;

	iio_push_to_buffers_with_timestamp(indio_dev, data->buffer,
					   data->timestamp);

out_unlock:
	mutex_unlock(&data->lock);

	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int fxas21002c_chip_init(struct fxas21002c_data *data)
{
	struct device *dev = regmap_get_device(data->regmap);
	unsigned int chip_id;
	int ret;

	ret = regmap_field_read(data->regmap_fields[F_WHO_AM_I], &chip_id);
	if (ret < 0)
		return ret;

	if (chip_id != FXAS21002C_CHIP_ID_1 &&
	    chip_id != FXAS21002C_CHIP_ID_2) {
		dev_err(dev, "chip id 0x%02x is not supported\n", chip_id);
		return -EINVAL;
	}

	data->chip_id = chip_id;

	ret = fxas21002c_mode_set(data, FXAS21002C_MODE_STANDBY);
	if (ret < 0)
		return ret;

	/* Set ODR to 200HZ as default */
	ret = fxas21002c_odr_set(data, 200);
	if (ret < 0)
		dev_err(dev, "failed to set ODR: %d\n", ret);

	return ret;
}

static int fxas21002c_data_rdy_trigger_set_state(struct iio_trigger *trig,
						 bool state)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct fxas21002c_data *data = iio_priv(indio_dev);

	return regmap_field_write(data->regmap_fields[F_INT_EN_DRDY], state);
}

static const struct iio_trigger_ops fxas21002c_trigger_ops = {
	.set_trigger_state = &fxas21002c_data_rdy_trigger_set_state,
};

static irqreturn_t fxas21002c_data_rdy_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct fxas21002c_data *data = iio_priv(indio_dev);

	data->timestamp = iio_get_time_ns(indio_dev);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t fxas21002c_data_rdy_thread(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct fxas21002c_data *data = iio_priv(indio_dev);
	unsigned int data_ready;
	int ret;

	ret = regmap_field_read(data->regmap_fields[F_SRC_DRDY], &data_ready);
	if (ret < 0)
		return IRQ_NONE;

	if (!data_ready)
		return IRQ_NONE;

	iio_trigger_poll_chained(data->dready_trig);

	return IRQ_HANDLED;
}

static int fxas21002c_trigger_probe(struct fxas21002c_data *data)
{
	struct device *dev = regmap_get_device(data->regmap);
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	unsigned long irq_trig;
	bool irq_open_drain;
	int irq1;
	int ret;

	if (!data->irq)
		return 0;

	irq1 = fwnode_irq_get_byname(dev_fwnode(dev), "INT1");
	if (irq1 == data->irq) {
		dev_info(dev, "using interrupt line INT1\n");
		ret = regmap_field_write(data->regmap_fields[F_INT_CFG_DRDY],
					 1);
		if (ret < 0)
			return ret;
	}

	dev_info(dev, "using interrupt line INT2\n");

	irq_open_drain = device_property_read_bool(dev, "drive-open-drain");

	data->dready_trig = devm_iio_trigger_alloc(dev, "%s-dev%d",
						   indio_dev->name,
						   iio_device_id(indio_dev));
	if (!data->dready_trig)
		return -ENOMEM;

	irq_trig = irqd_get_trigger_type(irq_get_irq_data(data->irq));

	if (irq_trig == IRQF_TRIGGER_RISING) {
		ret = regmap_field_write(data->regmap_fields[F_IPOL], 1);
		if (ret < 0)
			return ret;
	}

	if (irq_open_drain)
		irq_trig |= IRQF_SHARED;

	ret = devm_request_threaded_irq(dev, data->irq,
					fxas21002c_data_rdy_handler,
					fxas21002c_data_rdy_thread,
					irq_trig, "fxas21002c_data_ready",
					indio_dev);
	if (ret < 0)
		return ret;

	data->dready_trig->ops = &fxas21002c_trigger_ops;
	iio_trigger_set_drvdata(data->dready_trig, indio_dev);

	return devm_iio_trigger_register(dev, data->dready_trig);
}

static int fxas21002c_power_enable(struct fxas21002c_data *data)
{
	int ret;

	ret = regulator_enable(data->vdd);
	if (ret < 0)
		return ret;

	ret = regulator_enable(data->vddio);
	if (ret < 0) {
		regulator_disable(data->vdd);
		return ret;
	}

	return 0;
}

static void fxas21002c_power_disable(struct fxas21002c_data *data)
{
	regulator_disable(data->vdd);
	regulator_disable(data->vddio);
}

static void fxas21002c_power_disable_action(void *_data)
{
	struct fxas21002c_data *data = _data;

	fxas21002c_power_disable(data);
}

static int fxas21002c_regulators_get(struct fxas21002c_data *data)
{
	struct device *dev = regmap_get_device(data->regmap);

	data->vdd = devm_regulator_get(dev->parent, "vdd");
	if (IS_ERR(data->vdd))
		return PTR_ERR(data->vdd);

	data->vddio = devm_regulator_get(dev->parent, "vddio");

	return PTR_ERR_OR_ZERO(data->vddio);
}

int fxas21002c_core_probe(struct device *dev, struct regmap *regmap, int irq,
			  const char *name)
{
	struct fxas21002c_data *data;
	struct iio_dev *indio_dev;
	struct regmap_field *f;
	int i;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	dev_set_drvdata(dev, indio_dev);
	data->irq = irq;
	data->regmap = regmap;

	for (i = 0; i < F_MAX_FIELDS; i++) {
		f = devm_regmap_field_alloc(dev, data->regmap,
					    fxas21002c_reg_fields[i]);
		if (IS_ERR(f))
			return PTR_ERR(f);

		data->regmap_fields[i] = f;
	}

	mutex_init(&data->lock);

	ret = fxas21002c_regulators_get(data);
	if (ret < 0)
		return ret;

	ret = fxas21002c_power_enable(data);
	if (ret < 0)
		return ret;

	ret = devm_add_action_or_reset(dev, fxas21002c_power_disable_action,
				       data);
	if (ret < 0)
		return ret;

	ret = fxas21002c_chip_init(data);
	if (ret < 0)
		return ret;

	indio_dev->channels = fxas21002c_channels;
	indio_dev->num_channels = ARRAY_SIZE(fxas21002c_channels);
	indio_dev->name = name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &fxas21002c_info;

	ret = fxas21002c_trigger_probe(data);
	if (ret < 0)
		return ret;

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev, NULL,
					      fxas21002c_trigger_handler, NULL);
	if (ret < 0)
		return ret;

	ret = pm_runtime_set_active(dev);
	if (ret)
		return ret;

	pm_runtime_enable(dev);
	pm_runtime_set_autosuspend_delay(dev, 2000);
	pm_runtime_use_autosuspend(dev);

	ret = iio_device_register(indio_dev);
	if (ret < 0)
		goto pm_disable;

	return 0;

pm_disable:
	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);

	return ret;
}
EXPORT_SYMBOL_GPL(fxas21002c_core_probe);

void fxas21002c_core_remove(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);

	iio_device_unregister(indio_dev);

	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
}
EXPORT_SYMBOL_GPL(fxas21002c_core_remove);

static int __maybe_unused fxas21002c_suspend(struct device *dev)
{
	struct fxas21002c_data *data = iio_priv(dev_get_drvdata(dev));

	fxas21002c_mode_set(data, FXAS21002C_MODE_STANDBY);
	fxas21002c_power_disable(data);

	return 0;
}

static int __maybe_unused fxas21002c_resume(struct device *dev)
{
	struct fxas21002c_data *data = iio_priv(dev_get_drvdata(dev));
	int ret;

	ret = fxas21002c_power_enable(data);
	if (ret < 0)
		return ret;

	return fxas21002c_mode_set(data, data->prev_mode);
}

static int __maybe_unused fxas21002c_runtime_suspend(struct device *dev)
{
	struct fxas21002c_data *data = iio_priv(dev_get_drvdata(dev));

	return fxas21002c_mode_set(data, FXAS21002C_MODE_READY);
}

static int __maybe_unused fxas21002c_runtime_resume(struct device *dev)
{
	struct fxas21002c_data *data = iio_priv(dev_get_drvdata(dev));

	return fxas21002c_mode_set(data, FXAS21002C_MODE_ACTIVE);
}

const struct dev_pm_ops fxas21002c_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(fxas21002c_suspend, fxas21002c_resume)
	SET_RUNTIME_PM_OPS(fxas21002c_runtime_suspend,
			   fxas21002c_runtime_resume, NULL)
};
EXPORT_SYMBOL_GPL(fxas21002c_pm_ops);

MODULE_AUTHOR("Rui Miguel Silva <rui.silva@linaro.org>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("FXAS21002C Gyro driver");
