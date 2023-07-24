// SPDX-License-Identifier: GPL-2.0-only
/*
 * Core IIO driver for Bosch BMA400 triaxial acceleration sensor.
 *
 * Copyright 2019 Dan Robertson <dan@dlrobertson.com>
 *
 * TODO:
 *  - Support for power management
 *  - Support events and interrupts
 *  - Create channel for step count
 *  - Create channel for sensor time
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#include <asm/unaligned.h>

#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/events.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#include "bma400.h"

/*
 * The G-range selection may be one of 2g, 4g, 8, or 16g. The scale may
 * be selected with the acc_range bits of the ACC_CONFIG1 register.
 * NB: This buffer is populated in the device init.
 */
static int bma400_scales[8];

/*
 * See the ACC_CONFIG1 section of the datasheet.
 * NB: This buffer is populated in the device init.
 */
static int bma400_sample_freqs[14];

static const int bma400_osr_range[] = { 0, 1, 3 };

static int tap_reset_timeout[BMA400_TAP_TIM_LIST_LEN] = {
	300000,
	400000,
	500000,
	600000
};

static int tap_max2min_time[BMA400_TAP_TIM_LIST_LEN] = {
	30000,
	45000,
	60000,
	90000
};

static int double_tap2_min_delay[BMA400_TAP_TIM_LIST_LEN] = {
	20000,
	40000,
	60000,
	80000
};

/* See the ACC_CONFIG0 section of the datasheet */
enum bma400_power_mode {
	POWER_MODE_SLEEP   = 0x00,
	POWER_MODE_LOW     = 0x01,
	POWER_MODE_NORMAL  = 0x02,
	POWER_MODE_INVALID = 0x03,
};

enum bma400_scan {
	BMA400_ACCL_X,
	BMA400_ACCL_Y,
	BMA400_ACCL_Z,
	BMA400_TEMP,
};

struct bma400_sample_freq {
	int hz;
	int uhz;
};

enum bma400_activity {
	BMA400_STILL,
	BMA400_WALKING,
	BMA400_RUNNING,
};

struct bma400_data {
	struct device *dev;
	struct regmap *regmap;
	struct mutex mutex; /* data register lock */
	struct iio_mount_matrix orientation;
	enum bma400_power_mode power_mode;
	struct bma400_sample_freq sample_freq;
	int oversampling_ratio;
	int scale;
	struct iio_trigger *trig;
	int steps_enabled;
	bool step_event_en;
	bool activity_event_en;
	unsigned int generic_event_en;
	unsigned int tap_event_en_bitmask;
	/* Correct time stamp alignment */
	struct {
		__le16 buff[3];
		u8 temperature;
		s64 ts __aligned(8);
	} buffer __aligned(IIO_DMA_MINALIGN);
	__le16 status;
	__be16 duration;
};

static bool bma400_is_writable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case BMA400_CHIP_ID_REG:
	case BMA400_ERR_REG:
	case BMA400_STATUS_REG:
	case BMA400_X_AXIS_LSB_REG:
	case BMA400_X_AXIS_MSB_REG:
	case BMA400_Y_AXIS_LSB_REG:
	case BMA400_Y_AXIS_MSB_REG:
	case BMA400_Z_AXIS_LSB_REG:
	case BMA400_Z_AXIS_MSB_REG:
	case BMA400_SENSOR_TIME0:
	case BMA400_SENSOR_TIME1:
	case BMA400_SENSOR_TIME2:
	case BMA400_EVENT_REG:
	case BMA400_INT_STAT0_REG:
	case BMA400_INT_STAT1_REG:
	case BMA400_INT_STAT2_REG:
	case BMA400_TEMP_DATA_REG:
	case BMA400_FIFO_LENGTH0_REG:
	case BMA400_FIFO_LENGTH1_REG:
	case BMA400_FIFO_DATA_REG:
	case BMA400_STEP_CNT0_REG:
	case BMA400_STEP_CNT1_REG:
	case BMA400_STEP_CNT3_REG:
	case BMA400_STEP_STAT_REG:
		return false;
	default:
		return true;
	}
}

static bool bma400_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case BMA400_ERR_REG:
	case BMA400_STATUS_REG:
	case BMA400_X_AXIS_LSB_REG:
	case BMA400_X_AXIS_MSB_REG:
	case BMA400_Y_AXIS_LSB_REG:
	case BMA400_Y_AXIS_MSB_REG:
	case BMA400_Z_AXIS_LSB_REG:
	case BMA400_Z_AXIS_MSB_REG:
	case BMA400_SENSOR_TIME0:
	case BMA400_SENSOR_TIME1:
	case BMA400_SENSOR_TIME2:
	case BMA400_EVENT_REG:
	case BMA400_INT_STAT0_REG:
	case BMA400_INT_STAT1_REG:
	case BMA400_INT_STAT2_REG:
	case BMA400_TEMP_DATA_REG:
	case BMA400_FIFO_LENGTH0_REG:
	case BMA400_FIFO_LENGTH1_REG:
	case BMA400_FIFO_DATA_REG:
	case BMA400_STEP_CNT0_REG:
	case BMA400_STEP_CNT1_REG:
	case BMA400_STEP_CNT3_REG:
	case BMA400_STEP_STAT_REG:
		return true;
	default:
		return false;
	}
}

const struct regmap_config bma400_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = BMA400_CMD_REG,
	.cache_type = REGCACHE_RBTREE,
	.writeable_reg = bma400_is_writable_reg,
	.volatile_reg = bma400_is_volatile_reg,
};
EXPORT_SYMBOL_NS(bma400_regmap_config, IIO_BMA400);

static const struct iio_mount_matrix *
bma400_accel_get_mount_matrix(const struct iio_dev *indio_dev,
			      const struct iio_chan_spec *chan)
{
	struct bma400_data *data = iio_priv(indio_dev);

	return &data->orientation;
}

static const struct iio_chan_spec_ext_info bma400_ext_info[] = {
	IIO_MOUNT_MATRIX(IIO_SHARED_BY_DIR, bma400_accel_get_mount_matrix),
	{ }
};

static const struct iio_event_spec bma400_step_detect_event = {
	.type = IIO_EV_TYPE_CHANGE,
	.dir = IIO_EV_DIR_NONE,
	.mask_separate = BIT(IIO_EV_INFO_ENABLE),
};

static const struct iio_event_spec bma400_activity_event = {
	.type = IIO_EV_TYPE_CHANGE,
	.dir = IIO_EV_DIR_NONE,
	.mask_shared_by_type = BIT(IIO_EV_INFO_ENABLE),
};

static const struct iio_event_spec bma400_accel_event[] = {
	{
		.type = IIO_EV_TYPE_MAG,
		.dir = IIO_EV_DIR_FALLING,
		.mask_shared_by_type = BIT(IIO_EV_INFO_VALUE) |
				       BIT(IIO_EV_INFO_PERIOD) |
				       BIT(IIO_EV_INFO_HYSTERESIS) |
				       BIT(IIO_EV_INFO_ENABLE),
	},
	{
		.type = IIO_EV_TYPE_MAG,
		.dir = IIO_EV_DIR_RISING,
		.mask_shared_by_type = BIT(IIO_EV_INFO_VALUE) |
				       BIT(IIO_EV_INFO_PERIOD) |
				       BIT(IIO_EV_INFO_HYSTERESIS) |
				       BIT(IIO_EV_INFO_ENABLE),
	},
	{
		.type = IIO_EV_TYPE_GESTURE,
		.dir = IIO_EV_DIR_SINGLETAP,
		.mask_shared_by_type = BIT(IIO_EV_INFO_VALUE) |
				       BIT(IIO_EV_INFO_ENABLE) |
				       BIT(IIO_EV_INFO_RESET_TIMEOUT),
	},
	{
		.type = IIO_EV_TYPE_GESTURE,
		.dir = IIO_EV_DIR_DOUBLETAP,
		.mask_shared_by_type = BIT(IIO_EV_INFO_VALUE) |
				       BIT(IIO_EV_INFO_ENABLE) |
				       BIT(IIO_EV_INFO_RESET_TIMEOUT) |
				       BIT(IIO_EV_INFO_TAP2_MIN_DELAY),
	},
};

static int usec_to_tapreg_raw(int usec, const int *time_list)
{
	int index;

	for (index = 0; index < BMA400_TAP_TIM_LIST_LEN; index++) {
		if (usec == time_list[index])
			return index;
	}
	return -EINVAL;
}

static ssize_t in_accel_gesture_tap_maxtomin_time_show(struct device *dev,
						       struct device_attribute *attr,
						       char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct bma400_data *data = iio_priv(indio_dev);
	int ret, reg_val, raw, vals[2];

	ret = regmap_read(data->regmap, BMA400_TAP_CONFIG1, &reg_val);
	if (ret)
		return ret;

	raw = FIELD_GET(BMA400_TAP_TICSTH_MSK, reg_val);
	vals[0] = 0;
	vals[1] = tap_max2min_time[raw];

	return iio_format_value(buf, IIO_VAL_INT_PLUS_MICRO, 2, vals);
}

static ssize_t in_accel_gesture_tap_maxtomin_time_store(struct device *dev,
							struct device_attribute *attr,
							const char *buf, size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct bma400_data *data = iio_priv(indio_dev);
	int ret, val_int, val_fract, raw;

	ret = iio_str_to_fixpoint(buf, 100000, &val_int, &val_fract);
	if (ret)
		return ret;

	raw = usec_to_tapreg_raw(val_fract, tap_max2min_time);
	if (raw < 0)
		return -EINVAL;

	ret = regmap_update_bits(data->regmap, BMA400_TAP_CONFIG1,
				 BMA400_TAP_TICSTH_MSK,
				 FIELD_PREP(BMA400_TAP_TICSTH_MSK, raw));
	if (ret)
		return ret;

	return len;
}

static IIO_DEVICE_ATTR_RW(in_accel_gesture_tap_maxtomin_time, 0);

/*
 * Tap interrupts works with 200 Hz input data rate and the time based tap
 * controls are in the terms of data samples so the below calculation is
 * used to convert the configuration values into seconds.
 * e.g.:
 * 60 data samples * 0.005 ms = 0.3 seconds.
 * 80 data samples * 0.005 ms = 0.4 seconds.
 */

/* quiet configuration values in seconds */
static IIO_CONST_ATTR(in_accel_gesture_tap_reset_timeout_available,
		      "0.3 0.4 0.5 0.6");

/* tics_th configuration values in seconds */
static IIO_CONST_ATTR(in_accel_gesture_tap_maxtomin_time_available,
		      "0.03 0.045 0.06 0.09");

/* quiet_dt configuration values in seconds */
static IIO_CONST_ATTR(in_accel_gesture_doubletap_tap2_min_delay_available,
		      "0.02 0.04 0.06 0.08");

/* List of sensitivity values available to configure tap interrupts */
static IIO_CONST_ATTR(in_accel_gesture_tap_value_available, "0 1 2 3 4 5 6 7");

static struct attribute *bma400_event_attributes[] = {
	&iio_const_attr_in_accel_gesture_tap_value_available.dev_attr.attr,
	&iio_const_attr_in_accel_gesture_tap_reset_timeout_available.dev_attr.attr,
	&iio_const_attr_in_accel_gesture_tap_maxtomin_time_available.dev_attr.attr,
	&iio_const_attr_in_accel_gesture_doubletap_tap2_min_delay_available.dev_attr.attr,
	&iio_dev_attr_in_accel_gesture_tap_maxtomin_time.dev_attr.attr,
	NULL
};

static const struct attribute_group bma400_event_attribute_group = {
	.attrs = bma400_event_attributes,
};

#define BMA400_ACC_CHANNEL(_index, _axis) { \
	.type = IIO_ACCEL, \
	.modified = 1, \
	.channel2 = IIO_MOD_##_axis, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SAMP_FREQ) | \
		BIT(IIO_CHAN_INFO_SCALE) | \
		BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO), \
	.info_mask_shared_by_type_available = BIT(IIO_CHAN_INFO_SAMP_FREQ) | \
		BIT(IIO_CHAN_INFO_SCALE) | \
		BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO), \
	.ext_info = bma400_ext_info, \
	.scan_index = _index,	\
	.scan_type = {		\
		.sign = 's',	\
		.realbits = 12,		\
		.storagebits = 16,	\
		.endianness = IIO_LE,	\
	},				\
	.event_spec = bma400_accel_event,			\
	.num_event_specs = ARRAY_SIZE(bma400_accel_event)	\
}

#define BMA400_ACTIVITY_CHANNEL(_chan2) {	\
	.type = IIO_ACTIVITY,			\
	.modified = 1,				\
	.channel2 = _chan2,			\
	.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),	\
	.scan_index = -1, /* No buffer support */		\
	.event_spec = &bma400_activity_event,			\
	.num_event_specs = 1,					\
}

static const struct iio_chan_spec bma400_channels[] = {
	BMA400_ACC_CHANNEL(0, X),
	BMA400_ACC_CHANNEL(1, Y),
	BMA400_ACC_CHANNEL(2, Z),
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.scan_index = 3,
		.scan_type = {
			.sign = 's',
			.realbits = 8,
			.storagebits = 8,
			.endianness = IIO_LE,
		},
	},
	{
		.type = IIO_STEPS,
		.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED) |
				      BIT(IIO_CHAN_INFO_ENABLE),
		.scan_index = -1, /* No buffer support */
		.event_spec = &bma400_step_detect_event,
		.num_event_specs = 1,
	},
	BMA400_ACTIVITY_CHANNEL(IIO_MOD_STILL),
	BMA400_ACTIVITY_CHANNEL(IIO_MOD_WALKING),
	BMA400_ACTIVITY_CHANNEL(IIO_MOD_RUNNING),
	IIO_CHAN_SOFT_TIMESTAMP(4),
};

static int bma400_get_temp_reg(struct bma400_data *data, int *val, int *val2)
{
	unsigned int raw_temp;
	int host_temp;
	int ret;

	if (data->power_mode == POWER_MODE_SLEEP)
		return -EBUSY;

	ret = regmap_read(data->regmap, BMA400_TEMP_DATA_REG, &raw_temp);
	if (ret)
		return ret;

	host_temp = sign_extend32(raw_temp, 7);
	/*
	 * The formula for the TEMP_DATA register in the datasheet
	 * is: x * 0.5 + 23
	 */
	*val = (host_temp >> 1) + 23;
	*val2 = (host_temp & 0x1) * 500000;
	return IIO_VAL_INT_PLUS_MICRO;
}

static int bma400_get_accel_reg(struct bma400_data *data,
				const struct iio_chan_spec *chan,
				int *val)
{
	__le16 raw_accel;
	int lsb_reg;
	int ret;

	if (data->power_mode == POWER_MODE_SLEEP)
		return -EBUSY;

	switch (chan->channel2) {
	case IIO_MOD_X:
		lsb_reg = BMA400_X_AXIS_LSB_REG;
		break;
	case IIO_MOD_Y:
		lsb_reg = BMA400_Y_AXIS_LSB_REG;
		break;
	case IIO_MOD_Z:
		lsb_reg = BMA400_Z_AXIS_LSB_REG;
		break;
	default:
		dev_err(data->dev, "invalid axis channel modifier\n");
		return -EINVAL;
	}

	/* bulk read two registers, with the base being the LSB register */
	ret = regmap_bulk_read(data->regmap, lsb_reg, &raw_accel,
			       sizeof(raw_accel));
	if (ret)
		return ret;

	*val = sign_extend32(le16_to_cpu(raw_accel), 11);
	return IIO_VAL_INT;
}

static void bma400_output_data_rate_from_raw(int raw, unsigned int *val,
					     unsigned int *val2)
{
	*val = BMA400_ACC_ODR_MAX_HZ >> (BMA400_ACC_ODR_MAX_RAW - raw);
	if (raw > BMA400_ACC_ODR_MIN_RAW)
		*val2 = 0;
	else
		*val2 = 500000;
}

static int bma400_get_accel_output_data_rate(struct bma400_data *data)
{
	unsigned int val;
	unsigned int odr;
	int ret;

	switch (data->power_mode) {
	case POWER_MODE_LOW:
		/*
		 * Runs at a fixed rate in low-power mode. See section 4.3
		 * in the datasheet.
		 */
		bma400_output_data_rate_from_raw(BMA400_ACC_ODR_LP_RAW,
						 &data->sample_freq.hz,
						 &data->sample_freq.uhz);
		return 0;
	case POWER_MODE_NORMAL:
		/*
		 * In normal mode the ODR can be found in the ACC_CONFIG1
		 * register.
		 */
		ret = regmap_read(data->regmap, BMA400_ACC_CONFIG1_REG, &val);
		if (ret)
			goto error;

		odr = val & BMA400_ACC_ODR_MASK;
		if (odr < BMA400_ACC_ODR_MIN_RAW ||
		    odr > BMA400_ACC_ODR_MAX_RAW) {
			ret = -EINVAL;
			goto error;
		}

		bma400_output_data_rate_from_raw(odr, &data->sample_freq.hz,
						 &data->sample_freq.uhz);
		return 0;
	case POWER_MODE_SLEEP:
		data->sample_freq.hz = 0;
		data->sample_freq.uhz = 0;
		return 0;
	default:
		ret = 0;
		goto error;
	}
error:
	data->sample_freq.hz = -1;
	data->sample_freq.uhz = -1;
	return ret;
}

static int bma400_set_accel_output_data_rate(struct bma400_data *data,
					     int hz, int uhz)
{
	unsigned int idx;
	unsigned int odr;
	unsigned int val;
	int ret;

	if (hz >= BMA400_ACC_ODR_MIN_WHOLE_HZ) {
		if (uhz || hz > BMA400_ACC_ODR_MAX_HZ)
			return -EINVAL;

		/* Note this works because MIN_WHOLE_HZ is odd */
		idx = __ffs(hz);

		if (hz >> idx != BMA400_ACC_ODR_MIN_WHOLE_HZ)
			return -EINVAL;

		idx += BMA400_ACC_ODR_MIN_RAW + 1;
	} else if (hz == BMA400_ACC_ODR_MIN_HZ && uhz == 500000) {
		idx = BMA400_ACC_ODR_MIN_RAW;
	} else {
		return -EINVAL;
	}

	ret = regmap_read(data->regmap, BMA400_ACC_CONFIG1_REG, &val);
	if (ret)
		return ret;

	/* preserve the range and normal mode osr */
	odr = (~BMA400_ACC_ODR_MASK & val) | idx;

	ret = regmap_write(data->regmap, BMA400_ACC_CONFIG1_REG, odr);
	if (ret)
		return ret;

	bma400_output_data_rate_from_raw(idx, &data->sample_freq.hz,
					 &data->sample_freq.uhz);
	return 0;
}

static int bma400_get_accel_oversampling_ratio(struct bma400_data *data)
{
	unsigned int val;
	unsigned int osr;
	int ret;

	/*
	 * The oversampling ratio is stored in a different register
	 * based on the power-mode. In normal mode the OSR is stored
	 * in ACC_CONFIG1. In low-power mode it is stored in
	 * ACC_CONFIG0.
	 */
	switch (data->power_mode) {
	case POWER_MODE_LOW:
		ret = regmap_read(data->regmap, BMA400_ACC_CONFIG0_REG, &val);
		if (ret) {
			data->oversampling_ratio = -1;
			return ret;
		}

		osr = (val & BMA400_LP_OSR_MASK) >> BMA400_LP_OSR_SHIFT;

		data->oversampling_ratio = osr;
		return 0;
	case POWER_MODE_NORMAL:
		ret = regmap_read(data->regmap, BMA400_ACC_CONFIG1_REG, &val);
		if (ret) {
			data->oversampling_ratio = -1;
			return ret;
		}

		osr = (val & BMA400_NP_OSR_MASK) >> BMA400_NP_OSR_SHIFT;

		data->oversampling_ratio = osr;
		return 0;
	case POWER_MODE_SLEEP:
		data->oversampling_ratio = 0;
		return 0;
	default:
		data->oversampling_ratio = -1;
		return -EINVAL;
	}
}

static int bma400_set_accel_oversampling_ratio(struct bma400_data *data,
					       int val)
{
	unsigned int acc_config;
	int ret;

	if (val & ~BMA400_TWO_BITS_MASK)
		return -EINVAL;

	/*
	 * The oversampling ratio is stored in a different register
	 * based on the power-mode.
	 */
	switch (data->power_mode) {
	case POWER_MODE_LOW:
		ret = regmap_read(data->regmap, BMA400_ACC_CONFIG0_REG,
				  &acc_config);
		if (ret)
			return ret;

		ret = regmap_write(data->regmap, BMA400_ACC_CONFIG0_REG,
				   (acc_config & ~BMA400_LP_OSR_MASK) |
				   (val << BMA400_LP_OSR_SHIFT));
		if (ret) {
			dev_err(data->dev, "Failed to write out OSR\n");
			return ret;
		}

		data->oversampling_ratio = val;
		return 0;
	case POWER_MODE_NORMAL:
		ret = regmap_read(data->regmap, BMA400_ACC_CONFIG1_REG,
				  &acc_config);
		if (ret)
			return ret;

		ret = regmap_write(data->regmap, BMA400_ACC_CONFIG1_REG,
				   (acc_config & ~BMA400_NP_OSR_MASK) |
				   (val << BMA400_NP_OSR_SHIFT));
		if (ret) {
			dev_err(data->dev, "Failed to write out OSR\n");
			return ret;
		}

		data->oversampling_ratio = val;
		return 0;
	default:
		return -EINVAL;
	}
	return ret;
}

static int bma400_accel_scale_to_raw(struct bma400_data *data,
				     unsigned int val)
{
	int raw;

	if (val == 0)
		return -EINVAL;

	/* Note this works because BMA400_SCALE_MIN is odd */
	raw = __ffs(val);

	if (val >> raw != BMA400_SCALE_MIN)
		return -EINVAL;

	return raw;
}

static int bma400_get_accel_scale(struct bma400_data *data)
{
	unsigned int raw_scale;
	unsigned int val;
	int ret;

	ret = regmap_read(data->regmap, BMA400_ACC_CONFIG1_REG, &val);
	if (ret)
		return ret;

	raw_scale = (val & BMA400_ACC_SCALE_MASK) >> BMA400_SCALE_SHIFT;
	if (raw_scale > BMA400_TWO_BITS_MASK)
		return -EINVAL;

	data->scale = BMA400_SCALE_MIN << raw_scale;

	return 0;
}

static int bma400_set_accel_scale(struct bma400_data *data, unsigned int val)
{
	unsigned int acc_config;
	int raw;
	int ret;

	ret = regmap_read(data->regmap, BMA400_ACC_CONFIG1_REG, &acc_config);
	if (ret)
		return ret;

	raw = bma400_accel_scale_to_raw(data, val);
	if (raw < 0)
		return raw;

	ret = regmap_write(data->regmap, BMA400_ACC_CONFIG1_REG,
			   (acc_config & ~BMA400_ACC_SCALE_MASK) |
			   (raw << BMA400_SCALE_SHIFT));
	if (ret)
		return ret;

	data->scale = val;
	return 0;
}

static int bma400_get_power_mode(struct bma400_data *data)
{
	unsigned int val;
	int ret;

	ret = regmap_read(data->regmap, BMA400_STATUS_REG, &val);
	if (ret) {
		dev_err(data->dev, "Failed to read status register\n");
		return ret;
	}

	data->power_mode = (val >> 1) & BMA400_TWO_BITS_MASK;
	return 0;
}

static int bma400_set_power_mode(struct bma400_data *data,
				 enum bma400_power_mode mode)
{
	unsigned int val;
	int ret;

	ret = regmap_read(data->regmap, BMA400_ACC_CONFIG0_REG, &val);
	if (ret)
		return ret;

	if (data->power_mode == mode)
		return 0;

	if (mode == POWER_MODE_INVALID)
		return -EINVAL;

	/* Preserve the low-power oversample ratio etc */
	ret = regmap_write(data->regmap, BMA400_ACC_CONFIG0_REG,
			   mode | (val & ~BMA400_TWO_BITS_MASK));
	if (ret) {
		dev_err(data->dev, "Failed to write to power-mode\n");
		return ret;
	}

	data->power_mode = mode;

	/*
	 * Update our cached osr and odr based on the new
	 * power-mode.
	 */
	bma400_get_accel_output_data_rate(data);
	bma400_get_accel_oversampling_ratio(data);
	return 0;
}

static int bma400_enable_steps(struct bma400_data *data, int val)
{
	int ret;

	if (data->steps_enabled == val)
		return 0;

	ret = regmap_update_bits(data->regmap, BMA400_INT_CONFIG1_REG,
				 BMA400_STEP_INT_MSK,
				 FIELD_PREP(BMA400_STEP_INT_MSK, val ? 1 : 0));
	if (ret)
		return ret;
	data->steps_enabled = val;
	return ret;
}

static int bma400_get_steps_reg(struct bma400_data *data, int *val)
{
	u8 *steps_raw;
	int ret;

	steps_raw = kmalloc(BMA400_STEP_RAW_LEN, GFP_KERNEL);
	if (!steps_raw)
		return -ENOMEM;

	ret = regmap_bulk_read(data->regmap, BMA400_STEP_CNT0_REG,
			       steps_raw, BMA400_STEP_RAW_LEN);
	if (ret) {
		kfree(steps_raw);
		return ret;
	}
	*val = get_unaligned_le24(steps_raw);
	kfree(steps_raw);
	return IIO_VAL_INT;
}

static void bma400_init_tables(void)
{
	int raw;
	int i;

	for (i = 0; i + 1 < ARRAY_SIZE(bma400_sample_freqs); i += 2) {
		raw = (i / 2) + 5;
		bma400_output_data_rate_from_raw(raw, &bma400_sample_freqs[i],
						 &bma400_sample_freqs[i + 1]);
	}

	for (i = 0; i + 1 < ARRAY_SIZE(bma400_scales); i += 2) {
		raw = i / 2;
		bma400_scales[i] = 0;
		bma400_scales[i + 1] = BMA400_SCALE_MIN << raw;
	}
}

static void bma400_power_disable(void *data_ptr)
{
	struct bma400_data *data = data_ptr;
	int ret;

	mutex_lock(&data->mutex);
	ret = bma400_set_power_mode(data, POWER_MODE_SLEEP);
	mutex_unlock(&data->mutex);
	if (ret)
		dev_warn(data->dev, "Failed to put device into sleep mode (%pe)\n",
			 ERR_PTR(ret));
}

static enum iio_modifier bma400_act_to_mod(enum bma400_activity activity)
{
	switch (activity) {
	case BMA400_STILL:
		return IIO_MOD_STILL;
	case BMA400_WALKING:
		return IIO_MOD_WALKING;
	case BMA400_RUNNING:
		return IIO_MOD_RUNNING;
	default:
		return IIO_NO_MOD;
	}
}

static int bma400_init(struct bma400_data *data)
{
	static const char * const regulator_names[] = { "vdd", "vddio" };
	unsigned int val;
	int ret;

	ret = devm_regulator_bulk_get_enable(data->dev,
					     ARRAY_SIZE(regulator_names),
					     regulator_names);
	if (ret)
		return dev_err_probe(data->dev, ret, "Failed to get regulators\n");

	/* Try to read chip_id register. It must return 0x90. */
	ret = regmap_read(data->regmap, BMA400_CHIP_ID_REG, &val);
	if (ret) {
		dev_err(data->dev, "Failed to read chip id register\n");
		return ret;
	}

	if (val != BMA400_ID_REG_VAL) {
		dev_err(data->dev, "Chip ID mismatch\n");
		return -ENODEV;
	}

	ret = bma400_get_power_mode(data);
	if (ret) {
		dev_err(data->dev, "Failed to get the initial power-mode\n");
		return ret;
	}

	if (data->power_mode != POWER_MODE_NORMAL) {
		ret = bma400_set_power_mode(data, POWER_MODE_NORMAL);
		if (ret) {
			dev_err(data->dev, "Failed to wake up the device\n");
			return ret;
		}
		/*
		 * TODO: The datasheet waits 1500us here in the example, but
		 * lists 2/ODR as the wakeup time.
		 */
		usleep_range(1500, 2000);
	}

	ret = devm_add_action_or_reset(data->dev, bma400_power_disable, data);
	if (ret)
		return ret;

	bma400_init_tables();

	ret = bma400_get_accel_output_data_rate(data);
	if (ret)
		return ret;

	ret = bma400_get_accel_oversampling_ratio(data);
	if (ret)
		return ret;

	ret = bma400_get_accel_scale(data);
	if (ret)
		return ret;

	/* Configure INT1 pin to open drain */
	ret = regmap_write(data->regmap, BMA400_INT_IO_CTRL_REG, 0x06);
	if (ret)
		return ret;
	/*
	 * Once the interrupt engine is supported we might use the
	 * data_src_reg, but for now ensure this is set to the
	 * variable ODR filter selectable by the sample frequency
	 * channel.
	 */
	return regmap_write(data->regmap, BMA400_ACC_CONFIG2_REG, 0x00);
}

static int bma400_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan, int *val,
			   int *val2, long mask)
{
	struct bma400_data *data = iio_priv(indio_dev);
	unsigned int activity;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		switch (chan->type) {
		case IIO_TEMP:
			mutex_lock(&data->mutex);
			ret = bma400_get_temp_reg(data, val, val2);
			mutex_unlock(&data->mutex);
			return ret;
		case IIO_STEPS:
			return bma400_get_steps_reg(data, val);
		case IIO_ACTIVITY:
			ret = regmap_read(data->regmap, BMA400_STEP_STAT_REG,
					  &activity);
			if (ret)
				return ret;
			/*
			 * The device does not support confidence value levels,
			 * so we will always have 100% for current activity and
			 * 0% for the others.
			 */
			if (chan->channel2 == bma400_act_to_mod(activity))
				*val = 100;
			else
				*val = 0;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&data->mutex);
		ret = bma400_get_accel_reg(data, chan, val);
		mutex_unlock(&data->mutex);
		return ret;
	case IIO_CHAN_INFO_SAMP_FREQ:
		switch (chan->type) {
		case IIO_ACCEL:
			if (data->sample_freq.hz < 0)
				return -EINVAL;

			*val = data->sample_freq.hz;
			*val2 = data->sample_freq.uhz;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_TEMP:
			/*
			 * Runs at a fixed sampling frequency. See Section 4.4
			 * of the datasheet.
			 */
			*val = 6;
			*val2 = 250000;
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = data->scale;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		/*
		 * TODO: We could avoid this logic and returning -EINVAL here if
		 * we set both the low-power and normal mode OSR registers when
		 * we configure the device.
		 */
		if (data->oversampling_ratio < 0)
			return -EINVAL;

		*val = data->oversampling_ratio;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_ENABLE:
		*val = data->steps_enabled;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int bma400_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     const int **vals, int *type, int *length,
			     long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		*type = IIO_VAL_INT_PLUS_MICRO;
		*vals = bma400_scales;
		*length = ARRAY_SIZE(bma400_scales);
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		*type = IIO_VAL_INT;
		*vals = bma400_osr_range;
		*length = ARRAY_SIZE(bma400_osr_range);
		return IIO_AVAIL_RANGE;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*type = IIO_VAL_INT_PLUS_MICRO;
		*vals = bma400_sample_freqs;
		*length = ARRAY_SIZE(bma400_sample_freqs);
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static int bma400_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan, int val, int val2,
			    long mask)
{
	struct bma400_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		/*
		 * The sample frequency is readonly for the temperature
		 * register and a fixed value in low-power mode.
		 */
		if (chan->type != IIO_ACCEL)
			return -EINVAL;

		mutex_lock(&data->mutex);
		ret = bma400_set_accel_output_data_rate(data, val, val2);
		mutex_unlock(&data->mutex);
		return ret;
	case IIO_CHAN_INFO_SCALE:
		if (val != 0 ||
		    val2 < BMA400_SCALE_MIN || val2 > BMA400_SCALE_MAX)
			return -EINVAL;

		mutex_lock(&data->mutex);
		ret = bma400_set_accel_scale(data, val2);
		mutex_unlock(&data->mutex);
		return ret;
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		mutex_lock(&data->mutex);
		ret = bma400_set_accel_oversampling_ratio(data, val);
		mutex_unlock(&data->mutex);
		return ret;
	case IIO_CHAN_INFO_ENABLE:
		mutex_lock(&data->mutex);
		ret = bma400_enable_steps(data, val);
		mutex_unlock(&data->mutex);
		return ret;
	default:
		return -EINVAL;
	}
}

static int bma400_write_raw_get_fmt(struct iio_dev *indio_dev,
				    struct iio_chan_spec const *chan,
				    long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_SCALE:
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_OVERSAMPLING_RATIO:
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_ENABLE:
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int bma400_read_event_config(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir)
{
	struct bma400_data *data = iio_priv(indio_dev);

	switch (chan->type) {
	case IIO_ACCEL:
		switch (dir) {
		case IIO_EV_DIR_RISING:
			return FIELD_GET(BMA400_INT_GEN1_MSK,
					 data->generic_event_en);
		case IIO_EV_DIR_FALLING:
			return FIELD_GET(BMA400_INT_GEN2_MSK,
					 data->generic_event_en);
		case IIO_EV_DIR_SINGLETAP:
			return FIELD_GET(BMA400_S_TAP_MSK,
					 data->tap_event_en_bitmask);
		case IIO_EV_DIR_DOUBLETAP:
			return FIELD_GET(BMA400_D_TAP_MSK,
					 data->tap_event_en_bitmask);
		default:
			return -EINVAL;
		}
	case IIO_STEPS:
		return data->step_event_en;
	case IIO_ACTIVITY:
		return data->activity_event_en;
	default:
		return -EINVAL;
	}
}

static int bma400_steps_event_enable(struct bma400_data *data, int state)
{
	int ret;

	ret = bma400_enable_steps(data, 1);
	if (ret)
		return ret;

	ret = regmap_update_bits(data->regmap, BMA400_INT12_MAP_REG,
				 BMA400_STEP_INT_MSK,
				 FIELD_PREP(BMA400_STEP_INT_MSK,
					    state));
	if (ret)
		return ret;
	data->step_event_en = state;
	return 0;
}

static int bma400_activity_event_en(struct bma400_data *data,
				    enum iio_event_direction dir,
				    int state)
{
	int ret, reg, msk, value;
	int field_value = 0;

	switch (dir) {
	case IIO_EV_DIR_RISING:
		reg = BMA400_GEN1INT_CONFIG0;
		msk = BMA400_INT_GEN1_MSK;
		value = 2;
		set_mask_bits(&field_value, BMA400_INT_GEN1_MSK,
			      FIELD_PREP(BMA400_INT_GEN1_MSK, state));
		break;
	case IIO_EV_DIR_FALLING:
		reg = BMA400_GEN2INT_CONFIG0;
		msk = BMA400_INT_GEN2_MSK;
		value = 0;
		set_mask_bits(&field_value, BMA400_INT_GEN2_MSK,
			      FIELD_PREP(BMA400_INT_GEN2_MSK, state));
		break;
	default:
		return -EINVAL;
	}

	/* Enabling all axis for interrupt evaluation */
	ret = regmap_write(data->regmap, reg, 0xF8);
	if (ret)
		return ret;

	/* OR combination of all axis for interrupt evaluation */
	ret = regmap_write(data->regmap, reg + BMA400_GEN_CONFIG1_OFF, value);
	if (ret)
		return ret;

	/* Initial value to avoid interrupts while enabling*/
	ret = regmap_write(data->regmap, reg + BMA400_GEN_CONFIG2_OFF, 0x0A);
	if (ret)
		return ret;

	/* Initial duration value to avoid interrupts while enabling*/
	ret = regmap_write(data->regmap, reg + BMA400_GEN_CONFIG31_OFF, 0x0F);
	if (ret)
		return ret;

	ret = regmap_update_bits(data->regmap, BMA400_INT1_MAP_REG, msk,
				 field_value);
	if (ret)
		return ret;

	ret = regmap_update_bits(data->regmap, BMA400_INT_CONFIG0_REG, msk,
				 field_value);
	if (ret)
		return ret;

	set_mask_bits(&data->generic_event_en, msk, field_value);
	return 0;
}

static int bma400_tap_event_en(struct bma400_data *data,
			       enum iio_event_direction dir, int state)
{
	unsigned int mask, field_value;
	int ret;

	/*
	 * Tap interrupts can be configured only in normal mode.
	 * See table in section 4.3 "Power modes - performance modes" of
	 * datasheet v1.2.
	 */
	if (data->power_mode != POWER_MODE_NORMAL)
		return -EINVAL;

	/*
	 * Tap interrupts are operating with a data rate of 200Hz.
	 * See section 4.7 "Tap sensing interrupt" in datasheet v1.2.
	 */
	if (data->sample_freq.hz != 200 && state) {
		dev_err(data->dev, "Invalid data rate for tap interrupts.\n");
		return -EINVAL;
	}

	ret = regmap_update_bits(data->regmap, BMA400_INT12_MAP_REG,
				 BMA400_S_TAP_MSK,
				 FIELD_PREP(BMA400_S_TAP_MSK, state));
	if (ret)
		return ret;

	switch (dir) {
	case IIO_EV_DIR_SINGLETAP:
		mask = BMA400_S_TAP_MSK;
		set_mask_bits(&field_value, BMA400_S_TAP_MSK,
			      FIELD_PREP(BMA400_S_TAP_MSK, state));
		break;
	case IIO_EV_DIR_DOUBLETAP:
		mask = BMA400_D_TAP_MSK;
		set_mask_bits(&field_value, BMA400_D_TAP_MSK,
			      FIELD_PREP(BMA400_D_TAP_MSK, state));
		break;
	default:
		return -EINVAL;
	}

	ret = regmap_update_bits(data->regmap, BMA400_INT_CONFIG1_REG, mask,
				 field_value);
	if (ret)
		return ret;

	set_mask_bits(&data->tap_event_en_bitmask, mask, field_value);

	return 0;
}

static int bma400_disable_adv_interrupt(struct bma400_data *data)
{
	int ret;

	ret = regmap_write(data->regmap, BMA400_INT_CONFIG0_REG, 0);
	if (ret)
		return ret;

	ret = regmap_write(data->regmap, BMA400_INT_CONFIG1_REG, 0);
	if (ret)
		return ret;

	data->tap_event_en_bitmask = 0;
	data->generic_event_en = 0;
	data->step_event_en = false;
	data->activity_event_en = false;

	return 0;
}

static int bma400_write_event_config(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir, int state)
{
	struct bma400_data *data = iio_priv(indio_dev);
	int ret;

	switch (chan->type) {
	case IIO_ACCEL:
		switch (type) {
		case IIO_EV_TYPE_MAG:
			mutex_lock(&data->mutex);
			ret = bma400_activity_event_en(data, dir, state);
			mutex_unlock(&data->mutex);
			return ret;
		case IIO_EV_TYPE_GESTURE:
			mutex_lock(&data->mutex);
			ret = bma400_tap_event_en(data, dir, state);
			mutex_unlock(&data->mutex);
			return ret;
		default:
			return -EINVAL;
		}
	case IIO_STEPS:
		mutex_lock(&data->mutex);
		ret = bma400_steps_event_enable(data, state);
		mutex_unlock(&data->mutex);
		return ret;
	case IIO_ACTIVITY:
		mutex_lock(&data->mutex);
		if (!data->step_event_en) {
			ret = bma400_steps_event_enable(data, true);
			if (ret) {
				mutex_unlock(&data->mutex);
				return ret;
			}
		}
		data->activity_event_en = state;
		mutex_unlock(&data->mutex);
		return 0;
	default:
		return -EINVAL;
	}
}

static int get_gen_config_reg(enum iio_event_direction dir)
{
	switch (dir) {
	case IIO_EV_DIR_FALLING:
		return BMA400_GEN2INT_CONFIG0;
	case IIO_EV_DIR_RISING:
		return BMA400_GEN1INT_CONFIG0;
	default:
		return -EINVAL;
	}
}

static int bma400_read_event_value(struct iio_dev *indio_dev,
				   const struct iio_chan_spec *chan,
				   enum iio_event_type type,
				   enum iio_event_direction dir,
				   enum iio_event_info info,
				   int *val, int *val2)
{
	struct bma400_data *data = iio_priv(indio_dev);
	int ret, reg, reg_val, raw;

	if (chan->type != IIO_ACCEL)
		return -EINVAL;

	switch (type) {
	case IIO_EV_TYPE_MAG:
		reg = get_gen_config_reg(dir);
		if (reg < 0)
			return -EINVAL;

		*val2 = 0;
		switch (info) {
		case IIO_EV_INFO_VALUE:
			ret = regmap_read(data->regmap,
					  reg + BMA400_GEN_CONFIG2_OFF,
					  val);
			if (ret)
				return ret;
			return IIO_VAL_INT;
		case IIO_EV_INFO_PERIOD:
			mutex_lock(&data->mutex);
			ret = regmap_bulk_read(data->regmap,
					       reg + BMA400_GEN_CONFIG3_OFF,
					       &data->duration,
					       sizeof(data->duration));
			if (ret) {
				mutex_unlock(&data->mutex);
				return ret;
			}
			*val = be16_to_cpu(data->duration);
			mutex_unlock(&data->mutex);
			return IIO_VAL_INT;
		case IIO_EV_INFO_HYSTERESIS:
			ret = regmap_read(data->regmap, reg, val);
			if (ret)
				return ret;
			*val = FIELD_GET(BMA400_GEN_HYST_MSK, *val);
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_EV_TYPE_GESTURE:
		switch (info) {
		case IIO_EV_INFO_VALUE:
			ret = regmap_read(data->regmap, BMA400_TAP_CONFIG,
					  &reg_val);
			if (ret)
				return ret;

			*val = FIELD_GET(BMA400_TAP_SEN_MSK, reg_val);
			return IIO_VAL_INT;
		case IIO_EV_INFO_RESET_TIMEOUT:
			ret = regmap_read(data->regmap, BMA400_TAP_CONFIG1,
					  &reg_val);
			if (ret)
				return ret;

			raw = FIELD_GET(BMA400_TAP_QUIET_MSK, reg_val);
			*val = 0;
			*val2 = tap_reset_timeout[raw];
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_EV_INFO_TAP2_MIN_DELAY:
			ret = regmap_read(data->regmap, BMA400_TAP_CONFIG1,
					  &reg_val);
			if (ret)
				return ret;

			raw = FIELD_GET(BMA400_TAP_QUIETDT_MSK, reg_val);
			*val = 0;
			*val2 = double_tap2_min_delay[raw];
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int bma400_write_event_value(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir,
				    enum iio_event_info info,
				    int val, int val2)
{
	struct bma400_data *data = iio_priv(indio_dev);
	int reg, ret, raw;

	if (chan->type != IIO_ACCEL)
		return -EINVAL;

	switch (type) {
	case IIO_EV_TYPE_MAG:
		reg = get_gen_config_reg(dir);
		if (reg < 0)
			return -EINVAL;

		switch (info) {
		case IIO_EV_INFO_VALUE:
			if (val < 1 || val > 255)
				return -EINVAL;

			return regmap_write(data->regmap,
					    reg + BMA400_GEN_CONFIG2_OFF,
					    val);
		case IIO_EV_INFO_PERIOD:
			if (val < 1 || val > 65535)
				return -EINVAL;

			mutex_lock(&data->mutex);
			put_unaligned_be16(val, &data->duration);
			ret = regmap_bulk_write(data->regmap,
						reg + BMA400_GEN_CONFIG3_OFF,
						&data->duration,
						sizeof(data->duration));
			mutex_unlock(&data->mutex);
			return ret;
		case IIO_EV_INFO_HYSTERESIS:
			if (val < 0 || val > 3)
				return -EINVAL;

			return regmap_update_bits(data->regmap, reg,
						  BMA400_GEN_HYST_MSK,
						  FIELD_PREP(BMA400_GEN_HYST_MSK,
							     val));
		default:
			return -EINVAL;
		}
	case IIO_EV_TYPE_GESTURE:
		switch (info) {
		case IIO_EV_INFO_VALUE:
			if (val < 0 || val > 7)
				return -EINVAL;

			return regmap_update_bits(data->regmap,
						  BMA400_TAP_CONFIG,
						  BMA400_TAP_SEN_MSK,
						  FIELD_PREP(BMA400_TAP_SEN_MSK,
							     val));
		case IIO_EV_INFO_RESET_TIMEOUT:
			raw = usec_to_tapreg_raw(val2, tap_reset_timeout);
			if (raw < 0)
				return -EINVAL;

			return regmap_update_bits(data->regmap,
						  BMA400_TAP_CONFIG1,
						  BMA400_TAP_QUIET_MSK,
						  FIELD_PREP(BMA400_TAP_QUIET_MSK,
							     raw));
		case IIO_EV_INFO_TAP2_MIN_DELAY:
			raw = usec_to_tapreg_raw(val2, double_tap2_min_delay);
			if (raw < 0)
				return -EINVAL;

			return regmap_update_bits(data->regmap,
						  BMA400_TAP_CONFIG1,
						  BMA400_TAP_QUIETDT_MSK,
						  FIELD_PREP(BMA400_TAP_QUIETDT_MSK,
							     raw));
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int bma400_data_rdy_trigger_set_state(struct iio_trigger *trig,
					     bool state)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct bma400_data *data = iio_priv(indio_dev);
	int ret;

	ret = regmap_update_bits(data->regmap, BMA400_INT_CONFIG0_REG,
				 BMA400_INT_DRDY_MSK,
				 FIELD_PREP(BMA400_INT_DRDY_MSK, state));
	if (ret)
		return ret;

	return regmap_update_bits(data->regmap, BMA400_INT1_MAP_REG,
				  BMA400_INT_DRDY_MSK,
				  FIELD_PREP(BMA400_INT_DRDY_MSK, state));
}

static const unsigned long bma400_avail_scan_masks[] = {
	BIT(BMA400_ACCL_X) | BIT(BMA400_ACCL_Y) | BIT(BMA400_ACCL_Z),
	BIT(BMA400_ACCL_X) | BIT(BMA400_ACCL_Y) | BIT(BMA400_ACCL_Z)
	| BIT(BMA400_TEMP),
	0
};

static const struct iio_info bma400_info = {
	.read_raw          = bma400_read_raw,
	.read_avail        = bma400_read_avail,
	.write_raw         = bma400_write_raw,
	.write_raw_get_fmt = bma400_write_raw_get_fmt,
	.read_event_config = bma400_read_event_config,
	.write_event_config = bma400_write_event_config,
	.write_event_value = bma400_write_event_value,
	.read_event_value = bma400_read_event_value,
	.event_attrs = &bma400_event_attribute_group,
};

static const struct iio_trigger_ops bma400_trigger_ops = {
	.set_trigger_state = &bma400_data_rdy_trigger_set_state,
	.validate_device = &iio_trigger_validate_own_device,
};

static irqreturn_t bma400_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct bma400_data *data = iio_priv(indio_dev);
	int ret, temp;

	/* Lock to protect the data->buffer */
	mutex_lock(&data->mutex);

	/* bulk read six registers, with the base being the LSB register */
	ret = regmap_bulk_read(data->regmap, BMA400_X_AXIS_LSB_REG,
			       &data->buffer.buff, sizeof(data->buffer.buff));
	if (ret)
		goto unlock_err;

	if (test_bit(BMA400_TEMP, indio_dev->active_scan_mask)) {
		ret = regmap_read(data->regmap, BMA400_TEMP_DATA_REG, &temp);
		if (ret)
			goto unlock_err;

		data->buffer.temperature = temp;
	}

	iio_push_to_buffers_with_timestamp(indio_dev, &data->buffer,
					   iio_get_time_ns(indio_dev));

	mutex_unlock(&data->mutex);
	iio_trigger_notify_done(indio_dev->trig);
	return IRQ_HANDLED;

unlock_err:
	mutex_unlock(&data->mutex);
	return IRQ_NONE;
}

static irqreturn_t bma400_interrupt(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct bma400_data *data = iio_priv(indio_dev);
	s64 timestamp = iio_get_time_ns(indio_dev);
	unsigned int act, ev_dir = IIO_EV_DIR_NONE;
	int ret;

	/* Lock to protect the data->status */
	mutex_lock(&data->mutex);
	ret = regmap_bulk_read(data->regmap, BMA400_INT_STAT0_REG,
			       &data->status,
			       sizeof(data->status));
	/*
	 * if none of the bit is set in the status register then it is
	 * spurious interrupt.
	 */
	if (ret || !data->status)
		goto unlock_err;

	/*
	 * Disable all advance interrupts if interrupt engine overrun occurs.
	 * See section 4.7 "Interrupt engine overrun" in datasheet v1.2.
	 */
	if (FIELD_GET(BMA400_INT_ENG_OVRUN_MSK, le16_to_cpu(data->status))) {
		bma400_disable_adv_interrupt(data);
		dev_err(data->dev, "Interrupt engine overrun\n");
		goto unlock_err;
	}

	if (FIELD_GET(BMA400_INT_S_TAP_MSK, le16_to_cpu(data->status)))
		iio_push_event(indio_dev,
			       IIO_MOD_EVENT_CODE(IIO_ACCEL, 0,
						  IIO_MOD_X_OR_Y_OR_Z,
						  IIO_EV_TYPE_GESTURE,
						  IIO_EV_DIR_SINGLETAP),
			       timestamp);

	if (FIELD_GET(BMA400_INT_D_TAP_MSK, le16_to_cpu(data->status)))
		iio_push_event(indio_dev,
			       IIO_MOD_EVENT_CODE(IIO_ACCEL, 0,
						  IIO_MOD_X_OR_Y_OR_Z,
						  IIO_EV_TYPE_GESTURE,
						  IIO_EV_DIR_DOUBLETAP),
			       timestamp);

	if (FIELD_GET(BMA400_INT_GEN1_MSK, le16_to_cpu(data->status)))
		ev_dir = IIO_EV_DIR_RISING;

	if (FIELD_GET(BMA400_INT_GEN2_MSK, le16_to_cpu(data->status)))
		ev_dir = IIO_EV_DIR_FALLING;

	if (ev_dir != IIO_EV_DIR_NONE) {
		iio_push_event(indio_dev,
			       IIO_MOD_EVENT_CODE(IIO_ACCEL, 0,
						  IIO_MOD_X_OR_Y_OR_Z,
						  IIO_EV_TYPE_MAG, ev_dir),
			       timestamp);
	}

	if (FIELD_GET(BMA400_STEP_STAT_MASK, le16_to_cpu(data->status))) {
		iio_push_event(indio_dev,
			       IIO_MOD_EVENT_CODE(IIO_STEPS, 0, IIO_NO_MOD,
						  IIO_EV_TYPE_CHANGE,
						  IIO_EV_DIR_NONE),
			       timestamp);

		if (data->activity_event_en) {
			ret = regmap_read(data->regmap, BMA400_STEP_STAT_REG,
					  &act);
			if (ret)
				goto unlock_err;

			iio_push_event(indio_dev,
				       IIO_MOD_EVENT_CODE(IIO_ACTIVITY, 0,
							  bma400_act_to_mod(act),
							  IIO_EV_TYPE_CHANGE,
							  IIO_EV_DIR_NONE),
				       timestamp);
		}
	}

	if (FIELD_GET(BMA400_INT_DRDY_MSK, le16_to_cpu(data->status))) {
		mutex_unlock(&data->mutex);
		iio_trigger_poll_nested(data->trig);
		return IRQ_HANDLED;
	}

	mutex_unlock(&data->mutex);
	return IRQ_HANDLED;

unlock_err:
	mutex_unlock(&data->mutex);
	return IRQ_NONE;
}

int bma400_probe(struct device *dev, struct regmap *regmap, int irq,
		 const char *name)
{
	struct iio_dev *indio_dev;
	struct bma400_data *data;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->regmap = regmap;
	data->dev = dev;

	ret = bma400_init(data);
	if (ret)
		return ret;

	ret = iio_read_mount_matrix(dev, &data->orientation);
	if (ret)
		return ret;

	mutex_init(&data->mutex);
	indio_dev->name = name;
	indio_dev->info = &bma400_info;
	indio_dev->channels = bma400_channels;
	indio_dev->num_channels = ARRAY_SIZE(bma400_channels);
	indio_dev->available_scan_masks = bma400_avail_scan_masks;
	indio_dev->modes = INDIO_DIRECT_MODE;

	if (irq > 0) {
		data->trig = devm_iio_trigger_alloc(dev, "%s-dev%d",
						    indio_dev->name,
						    iio_device_id(indio_dev));
		if (!data->trig)
			return -ENOMEM;

		data->trig->ops = &bma400_trigger_ops;
		iio_trigger_set_drvdata(data->trig, indio_dev);

		ret = devm_iio_trigger_register(data->dev, data->trig);
		if (ret)
			return dev_err_probe(data->dev, ret,
					     "iio trigger register fail\n");

		indio_dev->trig = iio_trigger_get(data->trig);
		ret = devm_request_threaded_irq(dev, irq, NULL,
						&bma400_interrupt,
						IRQF_TRIGGER_RISING | IRQF_ONESHOT,
						indio_dev->name, indio_dev);
		if (ret)
			return dev_err_probe(data->dev, ret,
					     "request irq %d failed\n", irq);
	}

	ret = devm_iio_triggered_buffer_setup(dev, indio_dev, NULL,
					      &bma400_trigger_handler, NULL);
	if (ret)
		return dev_err_probe(data->dev, ret,
				     "iio triggered buffer setup failed\n");

	return devm_iio_device_register(dev, indio_dev);
}
EXPORT_SYMBOL_NS(bma400_probe, IIO_BMA400);

MODULE_AUTHOR("Dan Robertson <dan@dlrobertson.com>");
MODULE_AUTHOR("Jagath Jog J <jagathjog1996@gmail.com>");
MODULE_DESCRIPTION("Bosch BMA400 triaxial acceleration sensor core");
MODULE_LICENSE("GPL");
