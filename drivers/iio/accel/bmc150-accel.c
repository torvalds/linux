/*
 * 3-axis accelerometer driver supporting following Bosch-Sensortec chips:
 *  - BMC150
 *  - BMI055
 *  - BMA255
 *  - BMA250E
 *  - BMA222E
 *  - BMA280
 *
 * Copyright (c) 2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/gpio/consumer.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/events.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#define BMC150_ACCEL_DRV_NAME			"bmc150_accel"
#define BMC150_ACCEL_IRQ_NAME			"bmc150_accel_event"
#define BMC150_ACCEL_GPIO_NAME			"bmc150_accel_int"

#define BMC150_ACCEL_REG_CHIP_ID		0x00

#define BMC150_ACCEL_REG_INT_STATUS_2		0x0B
#define BMC150_ACCEL_ANY_MOTION_MASK		0x07
#define BMC150_ACCEL_ANY_MOTION_BIT_X		BIT(0)
#define BMC150_ACCEL_ANY_MOTION_BIT_Y		BIT(1)
#define BMC150_ACCEL_ANY_MOTION_BIT_Z		BIT(2)
#define BMC150_ACCEL_ANY_MOTION_BIT_SIGN	BIT(3)

#define BMC150_ACCEL_REG_PMU_LPW		0x11
#define BMC150_ACCEL_PMU_MODE_MASK		0xE0
#define BMC150_ACCEL_PMU_MODE_SHIFT		5
#define BMC150_ACCEL_PMU_BIT_SLEEP_DUR_MASK	0x17
#define BMC150_ACCEL_PMU_BIT_SLEEP_DUR_SHIFT	1

#define BMC150_ACCEL_REG_PMU_RANGE		0x0F

#define BMC150_ACCEL_DEF_RANGE_2G		0x03
#define BMC150_ACCEL_DEF_RANGE_4G		0x05
#define BMC150_ACCEL_DEF_RANGE_8G		0x08
#define BMC150_ACCEL_DEF_RANGE_16G		0x0C

/* Default BW: 125Hz */
#define BMC150_ACCEL_REG_PMU_BW		0x10
#define BMC150_ACCEL_DEF_BW			125

#define BMC150_ACCEL_REG_INT_MAP_0		0x19
#define BMC150_ACCEL_INT_MAP_0_BIT_SLOPE	BIT(2)

#define BMC150_ACCEL_REG_INT_MAP_1		0x1A
#define BMC150_ACCEL_INT_MAP_1_BIT_DATA	BIT(0)

#define BMC150_ACCEL_REG_INT_RST_LATCH		0x21
#define BMC150_ACCEL_INT_MODE_LATCH_RESET	0x80
#define BMC150_ACCEL_INT_MODE_LATCH_INT	0x0F
#define BMC150_ACCEL_INT_MODE_NON_LATCH_INT	0x00

#define BMC150_ACCEL_REG_INT_EN_0		0x16
#define BMC150_ACCEL_INT_EN_BIT_SLP_X		BIT(0)
#define BMC150_ACCEL_INT_EN_BIT_SLP_Y		BIT(1)
#define BMC150_ACCEL_INT_EN_BIT_SLP_Z		BIT(2)

#define BMC150_ACCEL_REG_INT_EN_1		0x17
#define BMC150_ACCEL_INT_EN_BIT_DATA_EN	BIT(4)

#define BMC150_ACCEL_REG_INT_OUT_CTRL		0x20
#define BMC150_ACCEL_INT_OUT_CTRL_INT1_LVL	BIT(0)

#define BMC150_ACCEL_REG_INT_5			0x27
#define BMC150_ACCEL_SLOPE_DUR_MASK		0x03

#define BMC150_ACCEL_REG_INT_6			0x28
#define BMC150_ACCEL_SLOPE_THRES_MASK		0xFF

/* Slope duration in terms of number of samples */
#define BMC150_ACCEL_DEF_SLOPE_DURATION		1
/* in terms of multiples of g's/LSB, based on range */
#define BMC150_ACCEL_DEF_SLOPE_THRESHOLD	1

#define BMC150_ACCEL_REG_XOUT_L		0x02

#define BMC150_ACCEL_MAX_STARTUP_TIME_MS	100

/* Sleep Duration values */
#define BMC150_ACCEL_SLEEP_500_MICRO		0x05
#define BMC150_ACCEL_SLEEP_1_MS		0x06
#define BMC150_ACCEL_SLEEP_2_MS		0x07
#define BMC150_ACCEL_SLEEP_4_MS		0x08
#define BMC150_ACCEL_SLEEP_6_MS		0x09
#define BMC150_ACCEL_SLEEP_10_MS		0x0A
#define BMC150_ACCEL_SLEEP_25_MS		0x0B
#define BMC150_ACCEL_SLEEP_50_MS		0x0C
#define BMC150_ACCEL_SLEEP_100_MS		0x0D
#define BMC150_ACCEL_SLEEP_500_MS		0x0E
#define BMC150_ACCEL_SLEEP_1_SEC		0x0F

#define BMC150_ACCEL_REG_TEMP			0x08
#define BMC150_ACCEL_TEMP_CENTER_VAL		24

#define BMC150_ACCEL_AXIS_TO_REG(axis)	(BMC150_ACCEL_REG_XOUT_L + (axis * 2))
#define BMC150_AUTO_SUSPEND_DELAY_MS		2000

enum bmc150_accel_axis {
	AXIS_X,
	AXIS_Y,
	AXIS_Z,
};

enum bmc150_power_modes {
	BMC150_ACCEL_SLEEP_MODE_NORMAL,
	BMC150_ACCEL_SLEEP_MODE_DEEP_SUSPEND,
	BMC150_ACCEL_SLEEP_MODE_LPM,
	BMC150_ACCEL_SLEEP_MODE_SUSPEND = 0x04,
};

struct bmc150_scale_info {
	int scale;
	u8 reg_range;
};

struct bmc150_accel_chip_info {
	u8 chip_id;
	const struct iio_chan_spec *channels;
	int num_channels;
	const struct bmc150_scale_info scale_table[4];
};

struct bmc150_accel_data {
	struct i2c_client *client;
	struct iio_trigger *dready_trig;
	struct iio_trigger *motion_trig;
	struct mutex mutex;
	s16 buffer[8];
	u8 bw_bits;
	u32 slope_dur;
	u32 slope_thres;
	u32 range;
	int ev_enable_state;
	bool dready_trigger_on;
	bool motion_trigger_on;
	int64_t timestamp;
	const struct bmc150_accel_chip_info *chip_info;
};

static const struct {
	int val;
	int val2;
	u8 bw_bits;
} bmc150_accel_samp_freq_table[] = { {7, 810000, 0x08},
				     {15, 630000, 0x09},
				     {31, 250000, 0x0A},
				     {62, 500000, 0x0B},
				     {125, 0, 0x0C},
				     {250, 0, 0x0D},
				     {500, 0, 0x0E},
				     {1000, 0, 0x0F} };

static const struct {
	int bw_bits;
	int msec;
} bmc150_accel_sample_upd_time[] = { {0x08, 64},
				     {0x09, 32},
				     {0x0A, 16},
				     {0x0B, 8},
				     {0x0C, 4},
				     {0x0D, 2},
				     {0x0E, 1},
				     {0x0F, 1} };

static const struct {
	int sleep_dur;
	u8 reg_value;
} bmc150_accel_sleep_value_table[] = { {0, 0},
				       {500, BMC150_ACCEL_SLEEP_500_MICRO},
				       {1000, BMC150_ACCEL_SLEEP_1_MS},
				       {2000, BMC150_ACCEL_SLEEP_2_MS},
				       {4000, BMC150_ACCEL_SLEEP_4_MS},
				       {6000, BMC150_ACCEL_SLEEP_6_MS},
				       {10000, BMC150_ACCEL_SLEEP_10_MS},
				       {25000, BMC150_ACCEL_SLEEP_25_MS},
				       {50000, BMC150_ACCEL_SLEEP_50_MS},
				       {100000, BMC150_ACCEL_SLEEP_100_MS},
				       {500000, BMC150_ACCEL_SLEEP_500_MS},
				       {1000000, BMC150_ACCEL_SLEEP_1_SEC} };


static int bmc150_accel_set_mode(struct bmc150_accel_data *data,
				 enum bmc150_power_modes mode,
				 int dur_us)
{
	int i;
	int ret;
	u8 lpw_bits;
	int dur_val = -1;

	if (dur_us > 0) {
		for (i = 0; i < ARRAY_SIZE(bmc150_accel_sleep_value_table);
									 ++i) {
			if (bmc150_accel_sleep_value_table[i].sleep_dur ==
									dur_us)
				dur_val =
				bmc150_accel_sleep_value_table[i].reg_value;
		}
	} else
		dur_val = 0;

	if (dur_val < 0)
		return -EINVAL;

	lpw_bits = mode << BMC150_ACCEL_PMU_MODE_SHIFT;
	lpw_bits |= (dur_val << BMC150_ACCEL_PMU_BIT_SLEEP_DUR_SHIFT);

	dev_dbg(&data->client->dev, "Set Mode bits %x\n", lpw_bits);

	ret = i2c_smbus_write_byte_data(data->client,
					BMC150_ACCEL_REG_PMU_LPW, lpw_bits);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error writing reg_pmu_lpw\n");
		return ret;
	}

	return 0;
}

static int bmc150_accel_set_bw(struct bmc150_accel_data *data, int val,
			       int val2)
{
	int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(bmc150_accel_samp_freq_table); ++i) {
		if (bmc150_accel_samp_freq_table[i].val == val &&
				bmc150_accel_samp_freq_table[i].val2 == val2) {
			ret = i2c_smbus_write_byte_data(
				data->client,
				BMC150_ACCEL_REG_PMU_BW,
				bmc150_accel_samp_freq_table[i].bw_bits);
			if (ret < 0)
				return ret;

			data->bw_bits =
				bmc150_accel_samp_freq_table[i].bw_bits;
			return 0;
		}
	}

	return -EINVAL;
}

static int bmc150_accel_chip_init(struct bmc150_accel_data *data)
{
	int ret;

	ret = i2c_smbus_read_byte_data(data->client, BMC150_ACCEL_REG_CHIP_ID);
	if (ret < 0) {
		dev_err(&data->client->dev,
			"Error: Reading chip id\n");
		return ret;
	}

	dev_dbg(&data->client->dev, "Chip Id %x\n", ret);
	if (ret != data->chip_info->chip_id) {
		dev_err(&data->client->dev, "Invalid chip %x\n", ret);
		return -ENODEV;
	}

	ret = bmc150_accel_set_mode(data, BMC150_ACCEL_SLEEP_MODE_NORMAL, 0);
	if (ret < 0)
		return ret;

	/* Set Bandwidth */
	ret = bmc150_accel_set_bw(data, BMC150_ACCEL_DEF_BW, 0);
	if (ret < 0)
		return ret;

	/* Set Default Range */
	ret = i2c_smbus_write_byte_data(data->client,
					BMC150_ACCEL_REG_PMU_RANGE,
					BMC150_ACCEL_DEF_RANGE_4G);
	if (ret < 0) {
		dev_err(&data->client->dev,
					"Error writing reg_pmu_range\n");
		return ret;
	}

	data->range = BMC150_ACCEL_DEF_RANGE_4G;

	/* Set default slope duration */
	ret = i2c_smbus_read_byte_data(data->client, BMC150_ACCEL_REG_INT_5);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error reading reg_int_5\n");
		return ret;
	}
	data->slope_dur |= BMC150_ACCEL_DEF_SLOPE_DURATION;
	ret = i2c_smbus_write_byte_data(data->client,
					BMC150_ACCEL_REG_INT_5,
					data->slope_dur);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error writing reg_int_5\n");
		return ret;
	}
	dev_dbg(&data->client->dev, "slope_dur %x\n", data->slope_dur);

	/* Set default slope thresholds */
	ret = i2c_smbus_write_byte_data(data->client,
					BMC150_ACCEL_REG_INT_6,
					BMC150_ACCEL_DEF_SLOPE_THRESHOLD);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error writing reg_int_6\n");
		return ret;
	}
	data->slope_thres = BMC150_ACCEL_DEF_SLOPE_THRESHOLD;
	dev_dbg(&data->client->dev, "slope_thres %x\n", data->slope_thres);

	/* Set default as latched interrupts */
	ret = i2c_smbus_write_byte_data(data->client,
					BMC150_ACCEL_REG_INT_RST_LATCH,
					BMC150_ACCEL_INT_MODE_LATCH_INT |
					BMC150_ACCEL_INT_MODE_LATCH_RESET);
	if (ret < 0) {
		dev_err(&data->client->dev,
			"Error writing reg_int_rst_latch\n");
		return ret;
	}

	return 0;
}

static int bmc150_accel_setup_any_motion_interrupt(
					struct bmc150_accel_data *data,
					bool status)
{
	int ret;

	/* Enable/Disable INT1 mapping */
	ret = i2c_smbus_read_byte_data(data->client,
				       BMC150_ACCEL_REG_INT_MAP_0);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error reading reg_int_map_0\n");
		return ret;
	}
	if (status)
		ret |= BMC150_ACCEL_INT_MAP_0_BIT_SLOPE;
	else
		ret &= ~BMC150_ACCEL_INT_MAP_0_BIT_SLOPE;

	ret = i2c_smbus_write_byte_data(data->client,
					BMC150_ACCEL_REG_INT_MAP_0,
					ret);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error writing reg_int_map_0\n");
		return ret;
	}

	if (status) {
		/* Set slope duration (no of samples) */
		ret = i2c_smbus_write_byte_data(data->client,
						BMC150_ACCEL_REG_INT_5,
						data->slope_dur);
		if (ret < 0) {
			dev_err(&data->client->dev, "Error write reg_int_5\n");
			return ret;
		}

		/* Set slope thresholds */
		ret = i2c_smbus_write_byte_data(data->client,
						BMC150_ACCEL_REG_INT_6,
						data->slope_thres);
		if (ret < 0) {
			dev_err(&data->client->dev, "Error write reg_int_6\n");
			return ret;
		}

		/*
		 * New data interrupt is always non-latched,
		 * which will have higher priority, so no need
		 * to set latched mode, we will be flooded anyway with INTR
		 */
		if (!data->dready_trigger_on) {
			ret = i2c_smbus_write_byte_data(data->client,
					BMC150_ACCEL_REG_INT_RST_LATCH,
					BMC150_ACCEL_INT_MODE_LATCH_INT |
					BMC150_ACCEL_INT_MODE_LATCH_RESET);
			if (ret < 0) {
				dev_err(&data->client->dev,
					"Error writing reg_int_rst_latch\n");
				return ret;
			}
		}

		ret = i2c_smbus_write_byte_data(data->client,
						BMC150_ACCEL_REG_INT_EN_0,
						BMC150_ACCEL_INT_EN_BIT_SLP_X |
						BMC150_ACCEL_INT_EN_BIT_SLP_Y |
						BMC150_ACCEL_INT_EN_BIT_SLP_Z);
	} else
		ret = i2c_smbus_write_byte_data(data->client,
						BMC150_ACCEL_REG_INT_EN_0,
						0);

	if (ret < 0) {
		dev_err(&data->client->dev, "Error writing reg_int_en_0\n");
		return ret;
	}

	return 0;
}

static int bmc150_accel_setup_new_data_interrupt(struct bmc150_accel_data *data,
					   bool status)
{
	int ret;

	/* Enable/Disable INT1 mapping */
	ret = i2c_smbus_read_byte_data(data->client,
				       BMC150_ACCEL_REG_INT_MAP_1);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error reading reg_int_map_1\n");
		return ret;
	}
	if (status)
		ret |= BMC150_ACCEL_INT_MAP_1_BIT_DATA;
	else
		ret &= ~BMC150_ACCEL_INT_MAP_1_BIT_DATA;

	ret = i2c_smbus_write_byte_data(data->client,
					BMC150_ACCEL_REG_INT_MAP_1,
					ret);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error writing reg_int_map_1\n");
		return ret;
	}

	if (status) {
		/*
		 * Set non latched mode interrupt and clear any latched
		 * interrupt
		 */
		ret = i2c_smbus_write_byte_data(data->client,
					BMC150_ACCEL_REG_INT_RST_LATCH,
					BMC150_ACCEL_INT_MODE_NON_LATCH_INT |
					BMC150_ACCEL_INT_MODE_LATCH_RESET);
		if (ret < 0) {
			dev_err(&data->client->dev,
				"Error writing reg_int_rst_latch\n");
			return ret;
		}

		ret = i2c_smbus_write_byte_data(data->client,
					BMC150_ACCEL_REG_INT_EN_1,
					BMC150_ACCEL_INT_EN_BIT_DATA_EN);

	} else {
		/* Restore default interrupt mode */
		ret = i2c_smbus_write_byte_data(data->client,
					BMC150_ACCEL_REG_INT_RST_LATCH,
					BMC150_ACCEL_INT_MODE_LATCH_INT |
					BMC150_ACCEL_INT_MODE_LATCH_RESET);
		if (ret < 0) {
			dev_err(&data->client->dev,
				"Error writing reg_int_rst_latch\n");
			return ret;
		}

		ret = i2c_smbus_write_byte_data(data->client,
						BMC150_ACCEL_REG_INT_EN_1,
						0);
	}

	if (ret < 0) {
		dev_err(&data->client->dev, "Error writing reg_int_en_1\n");
		return ret;
	}

	return 0;
}

static int bmc150_accel_get_bw(struct bmc150_accel_data *data, int *val,
			       int *val2)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(bmc150_accel_samp_freq_table); ++i) {
		if (bmc150_accel_samp_freq_table[i].bw_bits == data->bw_bits) {
			*val = bmc150_accel_samp_freq_table[i].val;
			*val2 = bmc150_accel_samp_freq_table[i].val2;
			return IIO_VAL_INT_PLUS_MICRO;
		}
	}

	return -EINVAL;
}

#ifdef CONFIG_PM_RUNTIME
static int bmc150_accel_get_startup_times(struct bmc150_accel_data *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(bmc150_accel_sample_upd_time); ++i) {
		if (bmc150_accel_sample_upd_time[i].bw_bits == data->bw_bits)
			return bmc150_accel_sample_upd_time[i].msec;
	}

	return BMC150_ACCEL_MAX_STARTUP_TIME_MS;
}

static int bmc150_accel_set_power_state(struct bmc150_accel_data *data, bool on)
{
	int ret;

	if (on)
		ret = pm_runtime_get_sync(&data->client->dev);
	else {
		pm_runtime_mark_last_busy(&data->client->dev);
		ret = pm_runtime_put_autosuspend(&data->client->dev);
	}
	if (ret < 0) {
		dev_err(&data->client->dev,
			"Failed: bmc150_accel_set_power_state for %d\n", on);
		if (on)
			pm_runtime_put_noidle(&data->client->dev);

		return ret;
	}

	return 0;
}
#else
static int bmc150_accel_set_power_state(struct bmc150_accel_data *data, bool on)
{
	return 0;
}
#endif

static int bmc150_accel_set_scale(struct bmc150_accel_data *data, int val)
{
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(data->chip_info->scale_table); ++i) {
		if (data->chip_info->scale_table[i].scale == val) {
			ret = i2c_smbus_write_byte_data(
				     data->client,
				     BMC150_ACCEL_REG_PMU_RANGE,
				     data->chip_info->scale_table[i].reg_range);
			if (ret < 0) {
				dev_err(&data->client->dev,
					"Error writing pmu_range\n");
				return ret;
			}

			data->range = data->chip_info->scale_table[i].reg_range;
			return 0;
		}
	}

	return -EINVAL;
}

static int bmc150_accel_get_temp(struct bmc150_accel_data *data, int *val)
{
	int ret;

	mutex_lock(&data->mutex);

	ret = i2c_smbus_read_byte_data(data->client, BMC150_ACCEL_REG_TEMP);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error reading reg_temp\n");
		mutex_unlock(&data->mutex);
		return ret;
	}
	*val = sign_extend32(ret, 7);

	mutex_unlock(&data->mutex);

	return IIO_VAL_INT;
}

static int bmc150_accel_get_axis(struct bmc150_accel_data *data,
				 struct iio_chan_spec const *chan,
				 int *val)
{
	int ret;
	int axis = chan->scan_index;

	mutex_lock(&data->mutex);
	ret = bmc150_accel_set_power_state(data, true);
	if (ret < 0) {
		mutex_unlock(&data->mutex);
		return ret;
	}

	ret = i2c_smbus_read_word_data(data->client,
				       BMC150_ACCEL_AXIS_TO_REG(axis));
	if (ret < 0) {
		dev_err(&data->client->dev, "Error reading axis %d\n", axis);
		bmc150_accel_set_power_state(data, false);
		mutex_unlock(&data->mutex);
		return ret;
	}
	*val = sign_extend32(ret >> chan->scan_type.shift,
			     chan->scan_type.realbits - 1);
	ret = bmc150_accel_set_power_state(data, false);
	mutex_unlock(&data->mutex);
	if (ret < 0)
		return ret;

	return IIO_VAL_INT;
}

static int bmc150_accel_read_raw(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan,
				 int *val, int *val2, long mask)
{
	struct bmc150_accel_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_TEMP:
			return bmc150_accel_get_temp(data, val);
		case IIO_ACCEL:
			if (iio_buffer_enabled(indio_dev))
				return -EBUSY;
			else
				return bmc150_accel_get_axis(data, chan, val);
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_OFFSET:
		if (chan->type == IIO_TEMP) {
			*val = BMC150_ACCEL_TEMP_CENTER_VAL;
			return IIO_VAL_INT;
		} else
			return -EINVAL;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		switch (chan->type) {
		case IIO_TEMP:
			*val2 = 500000;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_ACCEL:
		{
			int i;
			const struct bmc150_scale_info *si;
			int st_size = ARRAY_SIZE(data->chip_info->scale_table);

			for (i = 0; i < st_size; ++i) {
				si = &data->chip_info->scale_table[i];
				if (si->reg_range == data->range) {
					*val2 = si->scale;
					return IIO_VAL_INT_PLUS_MICRO;
				}
			}
			return -EINVAL;
		}
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SAMP_FREQ:
		mutex_lock(&data->mutex);
		ret = bmc150_accel_get_bw(data, val, val2);
		mutex_unlock(&data->mutex);
		return ret;
	default:
		return -EINVAL;
	}
}

static int bmc150_accel_write_raw(struct iio_dev *indio_dev,
				  struct iio_chan_spec const *chan,
				  int val, int val2, long mask)
{
	struct bmc150_accel_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		mutex_lock(&data->mutex);
		ret = bmc150_accel_set_bw(data, val, val2);
		mutex_unlock(&data->mutex);
		break;
	case IIO_CHAN_INFO_SCALE:
		if (val)
			return -EINVAL;

		mutex_lock(&data->mutex);
		ret = bmc150_accel_set_scale(data, val2);
		mutex_unlock(&data->mutex);
		return ret;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int bmc150_accel_read_event(struct iio_dev *indio_dev,
				   const struct iio_chan_spec *chan,
				   enum iio_event_type type,
				   enum iio_event_direction dir,
				   enum iio_event_info info,
				   int *val, int *val2)
{
	struct bmc150_accel_data *data = iio_priv(indio_dev);

	*val2 = 0;
	switch (info) {
	case IIO_EV_INFO_VALUE:
		*val = data->slope_thres;
		break;
	case IIO_EV_INFO_PERIOD:
		*val = data->slope_dur & BMC150_ACCEL_SLOPE_DUR_MASK;
		break;
	default:
		return -EINVAL;
	}

	return IIO_VAL_INT;
}

static int bmc150_accel_write_event(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir,
				    enum iio_event_info info,
				    int val, int val2)
{
	struct bmc150_accel_data *data = iio_priv(indio_dev);

	if (data->ev_enable_state)
		return -EBUSY;

	switch (info) {
	case IIO_EV_INFO_VALUE:
		data->slope_thres = val;
		break;
	case IIO_EV_INFO_PERIOD:
		data->slope_dur &= ~BMC150_ACCEL_SLOPE_DUR_MASK;
		data->slope_dur |= val & BMC150_ACCEL_SLOPE_DUR_MASK;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int bmc150_accel_read_event_config(struct iio_dev *indio_dev,
					  const struct iio_chan_spec *chan,
					  enum iio_event_type type,
					  enum iio_event_direction dir)
{

	struct bmc150_accel_data *data = iio_priv(indio_dev);

	return data->ev_enable_state;
}

static int bmc150_accel_write_event_config(struct iio_dev *indio_dev,
					   const struct iio_chan_spec *chan,
					   enum iio_event_type type,
					   enum iio_event_direction dir,
					   int state)
{
	struct bmc150_accel_data *data = iio_priv(indio_dev);
	int ret;

	if (state && data->ev_enable_state)
		return 0;

	mutex_lock(&data->mutex);

	if (!state && data->motion_trigger_on) {
		data->ev_enable_state = 0;
		mutex_unlock(&data->mutex);
		return 0;
	}

	/*
	 * We will expect the enable and disable to do operation in
	 * in reverse order. This will happen here anyway as our
	 * resume operation uses sync mode runtime pm calls, the
	 * suspend operation will be delayed by autosuspend delay
	 * So the disable operation will still happen in reverse of
	 * enable operation. When runtime pm is disabled the mode
	 * is always on so sequence doesn't matter
	 */

	ret = bmc150_accel_set_power_state(data, state);
	if (ret < 0) {
		mutex_unlock(&data->mutex);
		return ret;
	}

	ret =  bmc150_accel_setup_any_motion_interrupt(data, state);
	if (ret < 0) {
		bmc150_accel_set_power_state(data, false);
		mutex_unlock(&data->mutex);
		return ret;
	}

	data->ev_enable_state = state;
	mutex_unlock(&data->mutex);

	return 0;
}

static int bmc150_accel_validate_trigger(struct iio_dev *indio_dev,
				   struct iio_trigger *trig)
{
	struct bmc150_accel_data *data = iio_priv(indio_dev);

	if (data->dready_trig != trig && data->motion_trig != trig)
		return -EINVAL;

	return 0;
}

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL(
		"7.810000 15.630000 31.250000 62.500000 125 250 500 1000");

static struct attribute *bmc150_accel_attributes[] = {
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group bmc150_accel_attrs_group = {
	.attrs = bmc150_accel_attributes,
};

static const struct iio_event_spec bmc150_accel_event = {
		.type = IIO_EV_TYPE_ROC,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
				 BIT(IIO_EV_INFO_ENABLE) |
				 BIT(IIO_EV_INFO_PERIOD)
};

#define BMC150_ACCEL_CHANNEL(_axis, bits) {				\
	.type = IIO_ACCEL,						\
	.modified = 1,							\
	.channel2 = IIO_MOD_##_axis,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),			\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |		\
				BIT(IIO_CHAN_INFO_SAMP_FREQ),		\
	.scan_index = AXIS_##_axis,					\
	.scan_type = {							\
		.sign = 's',						\
		.realbits = (bits),					\
		.storagebits = 16,					\
		.shift = 16 - (bits),					\
	},								\
	.event_spec = &bmc150_accel_event,				\
	.num_event_specs = 1						\
}

#define BMC150_ACCEL_CHANNELS(bits) {					\
	{								\
		.type = IIO_TEMP,					\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
				      BIT(IIO_CHAN_INFO_SCALE) |	\
				      BIT(IIO_CHAN_INFO_OFFSET),	\
		.scan_index = -1,					\
	},								\
	BMC150_ACCEL_CHANNEL(X, bits),					\
	BMC150_ACCEL_CHANNEL(Y, bits),					\
	BMC150_ACCEL_CHANNEL(Z, bits),					\
	IIO_CHAN_SOFT_TIMESTAMP(3),					\
}

static const struct iio_chan_spec bma222e_accel_channels[] =
	BMC150_ACCEL_CHANNELS(8);
static const struct iio_chan_spec bma250e_accel_channels[] =
	BMC150_ACCEL_CHANNELS(10);
static const struct iio_chan_spec bmc150_accel_channels[] =
	BMC150_ACCEL_CHANNELS(12);
static const struct iio_chan_spec bma280_accel_channels[] =
	BMC150_ACCEL_CHANNELS(14);

enum {
	bmc150,
	bmi055,
	bma255,
	bma250e,
	bma222e,
	bma280,
};

static const struct bmc150_accel_chip_info bmc150_accel_chip_info_tbl[] = {
	[bmc150] = {
		.chip_id = 0xFA,
		.channels = bmc150_accel_channels,
		.num_channels = ARRAY_SIZE(bmc150_accel_channels),
		.scale_table = { {9610, BMC150_ACCEL_DEF_RANGE_2G},
				 {19122, BMC150_ACCEL_DEF_RANGE_4G},
				 {38344, BMC150_ACCEL_DEF_RANGE_8G},
				 {76590, BMC150_ACCEL_DEF_RANGE_16G} },
	},
	[bmi055] = {
		.chip_id = 0xFA,
		.channels = bmc150_accel_channels,
		.num_channels = ARRAY_SIZE(bmc150_accel_channels),
		.scale_table = { {9610, BMC150_ACCEL_DEF_RANGE_2G},
				 {19122, BMC150_ACCEL_DEF_RANGE_4G},
				 {38344, BMC150_ACCEL_DEF_RANGE_8G},
				 {76590, BMC150_ACCEL_DEF_RANGE_16G} },
	},
	[bma255] = {
		.chip_id = 0xFA,
		.channels = bmc150_accel_channels,
		.num_channels = ARRAY_SIZE(bmc150_accel_channels),
		.scale_table = { {9610, BMC150_ACCEL_DEF_RANGE_2G},
				 {19122, BMC150_ACCEL_DEF_RANGE_4G},
				 {38344, BMC150_ACCEL_DEF_RANGE_8G},
				 {76590, BMC150_ACCEL_DEF_RANGE_16G} },
	},
	[bma250e] = {
		.chip_id = 0xF9,
		.channels = bma250e_accel_channels,
		.num_channels = ARRAY_SIZE(bma250e_accel_channels),
		.scale_table = { {38344, BMC150_ACCEL_DEF_RANGE_2G},
				 {76590, BMC150_ACCEL_DEF_RANGE_4G},
				 {153277, BMC150_ACCEL_DEF_RANGE_8G},
				 {306457, BMC150_ACCEL_DEF_RANGE_16G} },
	},
	[bma222e] = {
		.chip_id = 0xF8,
		.channels = bma222e_accel_channels,
		.num_channels = ARRAY_SIZE(bma222e_accel_channels),
		.scale_table = { {153277, BMC150_ACCEL_DEF_RANGE_2G},
				 {306457, BMC150_ACCEL_DEF_RANGE_4G},
				 {612915, BMC150_ACCEL_DEF_RANGE_8G},
				 {1225831, BMC150_ACCEL_DEF_RANGE_16G} },
	},
	[bma280] = {
		.chip_id = 0xFB,
		.channels = bma280_accel_channels,
		.num_channels = ARRAY_SIZE(bma280_accel_channels),
		.scale_table = { {2392, BMC150_ACCEL_DEF_RANGE_2G},
				 {4785, BMC150_ACCEL_DEF_RANGE_4G},
				 {9581, BMC150_ACCEL_DEF_RANGE_8G},
				 {19152, BMC150_ACCEL_DEF_RANGE_16G} },
	},
};

static const struct iio_info bmc150_accel_info = {
	.attrs			= &bmc150_accel_attrs_group,
	.read_raw		= bmc150_accel_read_raw,
	.write_raw		= bmc150_accel_write_raw,
	.read_event_value	= bmc150_accel_read_event,
	.write_event_value	= bmc150_accel_write_event,
	.write_event_config	= bmc150_accel_write_event_config,
	.read_event_config	= bmc150_accel_read_event_config,
	.validate_trigger	= bmc150_accel_validate_trigger,
	.driver_module		= THIS_MODULE,
};

static irqreturn_t bmc150_accel_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct bmc150_accel_data *data = iio_priv(indio_dev);
	int bit, ret, i = 0;

	mutex_lock(&data->mutex);
	for_each_set_bit(bit, indio_dev->buffer->scan_mask,
			 indio_dev->masklength) {
		ret = i2c_smbus_read_word_data(data->client,
					       BMC150_ACCEL_AXIS_TO_REG(bit));
		if (ret < 0) {
			mutex_unlock(&data->mutex);
			goto err_read;
		}
		data->buffer[i++] = ret;
	}
	mutex_unlock(&data->mutex);

	iio_push_to_buffers_with_timestamp(indio_dev, data->buffer,
					   data->timestamp);
err_read:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int bmc150_accel_trig_try_reen(struct iio_trigger *trig)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct bmc150_accel_data *data = iio_priv(indio_dev);
	int ret;

	/* new data interrupts don't need ack */
	if (data->dready_trigger_on)
		return 0;

	mutex_lock(&data->mutex);
	/* clear any latched interrupt */
	ret = i2c_smbus_write_byte_data(data->client,
					BMC150_ACCEL_REG_INT_RST_LATCH,
					BMC150_ACCEL_INT_MODE_LATCH_INT |
					BMC150_ACCEL_INT_MODE_LATCH_RESET);
	mutex_unlock(&data->mutex);
	if (ret < 0) {
		dev_err(&data->client->dev,
			"Error writing reg_int_rst_latch\n");
		return ret;
	}

	return 0;
}

static int bmc150_accel_data_rdy_trigger_set_state(struct iio_trigger *trig,
						   bool state)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct bmc150_accel_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->mutex);

	if (!state && data->ev_enable_state && data->motion_trigger_on) {
		data->motion_trigger_on = false;
		mutex_unlock(&data->mutex);
		return 0;
	}

	/*
	 * Refer to comment in bmc150_accel_write_event_config for
	 * enable/disable operation order
	 */
	ret = bmc150_accel_set_power_state(data, state);
	if (ret < 0) {
		mutex_unlock(&data->mutex);
		return ret;
	}
	if (data->motion_trig == trig)
		ret =  bmc150_accel_setup_any_motion_interrupt(data, state);
	else
		ret = bmc150_accel_setup_new_data_interrupt(data, state);
	if (ret < 0) {
		bmc150_accel_set_power_state(data, false);
		mutex_unlock(&data->mutex);
		return ret;
	}
	if (data->motion_trig == trig)
		data->motion_trigger_on = state;
	else
		data->dready_trigger_on = state;

	mutex_unlock(&data->mutex);

	return ret;
}

static const struct iio_trigger_ops bmc150_accel_trigger_ops = {
	.set_trigger_state = bmc150_accel_data_rdy_trigger_set_state,
	.try_reenable = bmc150_accel_trig_try_reen,
	.owner = THIS_MODULE,
};

static irqreturn_t bmc150_accel_event_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct bmc150_accel_data *data = iio_priv(indio_dev);
	int ret;
	int dir;

	ret = i2c_smbus_read_byte_data(data->client,
				       BMC150_ACCEL_REG_INT_STATUS_2);
	if (ret < 0) {
		dev_err(&data->client->dev, "Error reading reg_int_status_2\n");
		goto ack_intr_status;
	}

	if (ret & BMC150_ACCEL_ANY_MOTION_BIT_SIGN)
		dir = IIO_EV_DIR_FALLING;
	else
		dir = IIO_EV_DIR_RISING;

	if (ret & BMC150_ACCEL_ANY_MOTION_BIT_X)
		iio_push_event(indio_dev, IIO_MOD_EVENT_CODE(IIO_ACCEL,
							0,
							IIO_MOD_X,
							IIO_EV_TYPE_ROC,
							dir),
							data->timestamp);
	if (ret & BMC150_ACCEL_ANY_MOTION_BIT_Y)
		iio_push_event(indio_dev, IIO_MOD_EVENT_CODE(IIO_ACCEL,
							0,
							IIO_MOD_Y,
							IIO_EV_TYPE_ROC,
							dir),
							data->timestamp);
	if (ret & BMC150_ACCEL_ANY_MOTION_BIT_Z)
		iio_push_event(indio_dev, IIO_MOD_EVENT_CODE(IIO_ACCEL,
							0,
							IIO_MOD_Z,
							IIO_EV_TYPE_ROC,
							dir),
							data->timestamp);
ack_intr_status:
	if (!data->dready_trigger_on)
		ret = i2c_smbus_write_byte_data(data->client,
					BMC150_ACCEL_REG_INT_RST_LATCH,
					BMC150_ACCEL_INT_MODE_LATCH_INT |
					BMC150_ACCEL_INT_MODE_LATCH_RESET);

	return IRQ_HANDLED;
}

static irqreturn_t bmc150_accel_data_rdy_trig_poll(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct bmc150_accel_data *data = iio_priv(indio_dev);

	data->timestamp = iio_get_time_ns();

	if (data->dready_trigger_on)
		iio_trigger_poll(data->dready_trig);
	else if (data->motion_trigger_on)
		iio_trigger_poll(data->motion_trig);

	if (data->ev_enable_state)
		return IRQ_WAKE_THREAD;
	else
		return IRQ_HANDLED;
}

static const char *bmc150_accel_match_acpi_device(struct device *dev, int *data)
{
	const struct acpi_device_id *id;

	id = acpi_match_device(dev->driver->acpi_match_table, dev);

	if (!id)
		return NULL;

	*data = (int) id->driver_data;

	return dev_name(dev);
}

static int bmc150_accel_gpio_probe(struct i2c_client *client,
					struct bmc150_accel_data *data)
{
	struct device *dev;
	struct gpio_desc *gpio;
	int ret;

	if (!client)
		return -EINVAL;

	dev = &client->dev;

	/* data ready gpio interrupt pin */
	gpio = devm_gpiod_get_index(dev, BMC150_ACCEL_GPIO_NAME, 0);
	if (IS_ERR(gpio)) {
		dev_err(dev, "Failed: gpio get index\n");
		return PTR_ERR(gpio);
	}

	ret = gpiod_direction_input(gpio);
	if (ret)
		return ret;

	ret = gpiod_to_irq(gpio);

	dev_dbg(dev, "GPIO resource, no:%d irq:%d\n", desc_to_gpio(gpio), ret);

	return ret;
}

static int bmc150_accel_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	struct bmc150_accel_data *data;
	struct iio_dev *indio_dev;
	int ret;
	const char *name = NULL;
	int chip_id = 0;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;

	if (id) {
		name = id->name;
		chip_id = id->driver_data;
	}

	if (ACPI_HANDLE(&client->dev))
		name = bmc150_accel_match_acpi_device(&client->dev, &chip_id);

	data->chip_info = &bmc150_accel_chip_info_tbl[chip_id];

	ret = bmc150_accel_chip_init(data);
	if (ret < 0)
		return ret;

	mutex_init(&data->mutex);

	indio_dev->dev.parent = &client->dev;
	indio_dev->channels = data->chip_info->channels;
	indio_dev->num_channels = data->chip_info->num_channels;
	indio_dev->name = name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &bmc150_accel_info;

	if (client->irq < 0)
		client->irq = bmc150_accel_gpio_probe(client, data);

	if (client->irq >= 0) {
		ret = devm_request_threaded_irq(
						&client->dev, client->irq,
						bmc150_accel_data_rdy_trig_poll,
						bmc150_accel_event_handler,
						IRQF_TRIGGER_RISING,
						BMC150_ACCEL_IRQ_NAME,
						indio_dev);
		if (ret)
			return ret;

		data->dready_trig = devm_iio_trigger_alloc(&client->dev,
							   "%s-dev%d",
							   indio_dev->name,
							   indio_dev->id);
		if (!data->dready_trig)
			return -ENOMEM;

		data->motion_trig = devm_iio_trigger_alloc(&client->dev,
							  "%s-any-motion-dev%d",
							  indio_dev->name,
							  indio_dev->id);
		if (!data->motion_trig)
			return -ENOMEM;

		data->dready_trig->dev.parent = &client->dev;
		data->dready_trig->ops = &bmc150_accel_trigger_ops;
		iio_trigger_set_drvdata(data->dready_trig, indio_dev);
		ret = iio_trigger_register(data->dready_trig);
		if (ret)
			return ret;

		data->motion_trig->dev.parent = &client->dev;
		data->motion_trig->ops = &bmc150_accel_trigger_ops;
		iio_trigger_set_drvdata(data->motion_trig, indio_dev);
		ret = iio_trigger_register(data->motion_trig);
		if (ret) {
			data->motion_trig = NULL;
			goto err_trigger_unregister;
		}

		ret = iio_triggered_buffer_setup(indio_dev,
						 &iio_pollfunc_store_time,
						 bmc150_accel_trigger_handler,
						 NULL);
		if (ret < 0) {
			dev_err(&client->dev,
				"Failed: iio triggered buffer setup\n");
			goto err_trigger_unregister;
		}
	}

	ret = iio_device_register(indio_dev);
	if (ret < 0) {
		dev_err(&client->dev, "Unable to register iio device\n");
		goto err_buffer_cleanup;
	}

	ret = pm_runtime_set_active(&client->dev);
	if (ret)
		goto err_iio_unregister;

	pm_runtime_enable(&client->dev);
	pm_runtime_set_autosuspend_delay(&client->dev,
					 BMC150_AUTO_SUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(&client->dev);

	return 0;

err_iio_unregister:
	iio_device_unregister(indio_dev);
err_buffer_cleanup:
	if (data->dready_trig)
		iio_triggered_buffer_cleanup(indio_dev);
err_trigger_unregister:
	if (data->dready_trig)
		iio_trigger_unregister(data->dready_trig);
	if (data->motion_trig)
		iio_trigger_unregister(data->motion_trig);

	return ret;
}

static int bmc150_accel_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct bmc150_accel_data *data = iio_priv(indio_dev);

	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);
	pm_runtime_put_noidle(&client->dev);

	iio_device_unregister(indio_dev);

	if (data->dready_trig) {
		iio_triggered_buffer_cleanup(indio_dev);
		iio_trigger_unregister(data->dready_trig);
		iio_trigger_unregister(data->motion_trig);
	}

	mutex_lock(&data->mutex);
	bmc150_accel_set_mode(data, BMC150_ACCEL_SLEEP_MODE_DEEP_SUSPEND, 0);
	mutex_unlock(&data->mutex);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int bmc150_accel_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct bmc150_accel_data *data = iio_priv(indio_dev);

	mutex_lock(&data->mutex);
	bmc150_accel_set_mode(data, BMC150_ACCEL_SLEEP_MODE_SUSPEND, 0);
	mutex_unlock(&data->mutex);

	return 0;
}

static int bmc150_accel_resume(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct bmc150_accel_data *data = iio_priv(indio_dev);

	mutex_lock(&data->mutex);
	if (data->dready_trigger_on || data->motion_trigger_on ||
							data->ev_enable_state)
		bmc150_accel_set_mode(data, BMC150_ACCEL_SLEEP_MODE_NORMAL, 0);
	mutex_unlock(&data->mutex);

	return 0;
}
#endif

#ifdef CONFIG_PM_RUNTIME
static int bmc150_accel_runtime_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct bmc150_accel_data *data = iio_priv(indio_dev);
	int ret;

	dev_dbg(&data->client->dev,  __func__);
	ret = bmc150_accel_set_mode(data, BMC150_ACCEL_SLEEP_MODE_SUSPEND, 0);
	if (ret < 0)
		return -EAGAIN;

	return 0;
}

static int bmc150_accel_runtime_resume(struct device *dev)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(to_i2c_client(dev));
	struct bmc150_accel_data *data = iio_priv(indio_dev);
	int ret;
	int sleep_val;

	dev_dbg(&data->client->dev,  __func__);

	ret = bmc150_accel_set_mode(data, BMC150_ACCEL_SLEEP_MODE_NORMAL, 0);
	if (ret < 0)
		return ret;

	sleep_val = bmc150_accel_get_startup_times(data);
	if (sleep_val < 20)
		usleep_range(sleep_val * 1000, 20000);
	else
		msleep_interruptible(sleep_val);

	return 0;
}
#endif

static const struct dev_pm_ops bmc150_accel_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(bmc150_accel_suspend, bmc150_accel_resume)
	SET_RUNTIME_PM_OPS(bmc150_accel_runtime_suspend,
			   bmc150_accel_runtime_resume, NULL)
};

static const struct acpi_device_id bmc150_accel_acpi_match[] = {
	{"BSBA0150",	bmc150},
	{"BMC150A",	bmc150},
	{"BMI055A",	bmi055},
	{"BMA0255",	bma255},
	{"BMA250E",	bma250e},
	{"BMA222E",	bma222e},
	{"BMA0280",	bma280},
	{ },
};
MODULE_DEVICE_TABLE(acpi, bmc150_accel_acpi_match);

static const struct i2c_device_id bmc150_accel_id[] = {
	{"bmc150_accel",	bmc150},
	{"bmi055_accel",	bmi055},
	{"bma255",		bma255},
	{"bma250e",		bma250e},
	{"bma222e",		bma222e},
	{"bma280",		bma280},
	{}
};

MODULE_DEVICE_TABLE(i2c, bmc150_accel_id);

static struct i2c_driver bmc150_accel_driver = {
	.driver = {
		.name	= BMC150_ACCEL_DRV_NAME,
		.acpi_match_table = ACPI_PTR(bmc150_accel_acpi_match),
		.pm	= &bmc150_accel_pm_ops,
	},
	.probe		= bmc150_accel_probe,
	.remove		= bmc150_accel_remove,
	.id_table	= bmc150_accel_id,
};
module_i2c_driver(bmc150_accel_driver);

MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("BMC150 accelerometer driver");
