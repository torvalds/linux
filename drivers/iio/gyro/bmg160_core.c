/*
 * BMG160 Gyro Sensor driver
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
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/events.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/regmap.h>
#include <linux/delay.h>
#include "bmg160.h"

#define BMG160_IRQ_NAME		"bmg160_event"

#define BMG160_REG_CHIP_ID		0x00
#define BMG160_CHIP_ID_VAL		0x0F

#define BMG160_REG_PMU_LPW		0x11
#define BMG160_MODE_NORMAL		0x00
#define BMG160_MODE_DEEP_SUSPEND	0x20
#define BMG160_MODE_SUSPEND		0x80

#define BMG160_REG_RANGE		0x0F

#define BMG160_RANGE_2000DPS		0
#define BMG160_RANGE_1000DPS		1
#define BMG160_RANGE_500DPS		2
#define BMG160_RANGE_250DPS		3
#define BMG160_RANGE_125DPS		4

#define BMG160_REG_PMU_BW		0x10
#define BMG160_NO_FILTER		0
#define BMG160_DEF_BW			100
#define BMG160_REG_PMU_BW_RES		BIT(7)

#define BMG160_GYRO_REG_RESET		0x14
#define BMG160_GYRO_RESET_VAL		0xb6

#define BMG160_REG_INT_MAP_0		0x17
#define BMG160_INT_MAP_0_BIT_ANY	BIT(1)

#define BMG160_REG_INT_MAP_1		0x18
#define BMG160_INT_MAP_1_BIT_NEW_DATA	BIT(0)

#define BMG160_REG_INT_RST_LATCH	0x21
#define BMG160_INT_MODE_LATCH_RESET	0x80
#define BMG160_INT_MODE_LATCH_INT	0x0F
#define BMG160_INT_MODE_NON_LATCH_INT	0x00

#define BMG160_REG_INT_EN_0		0x15
#define BMG160_DATA_ENABLE_INT		BIT(7)

#define BMG160_REG_INT_EN_1		0x16
#define BMG160_INT1_BIT_OD		BIT(1)

#define BMG160_REG_XOUT_L		0x02
#define BMG160_AXIS_TO_REG(axis)	(BMG160_REG_XOUT_L + (axis * 2))

#define BMG160_REG_SLOPE_THRES		0x1B
#define BMG160_SLOPE_THRES_MASK	0x0F

#define BMG160_REG_MOTION_INTR		0x1C
#define BMG160_INT_MOTION_X		BIT(0)
#define BMG160_INT_MOTION_Y		BIT(1)
#define BMG160_INT_MOTION_Z		BIT(2)
#define BMG160_ANY_DUR_MASK		0x30
#define BMG160_ANY_DUR_SHIFT		4

#define BMG160_REG_INT_STATUS_2	0x0B
#define BMG160_ANY_MOTION_MASK		0x07
#define BMG160_ANY_MOTION_BIT_X		BIT(0)
#define BMG160_ANY_MOTION_BIT_Y		BIT(1)
#define BMG160_ANY_MOTION_BIT_Z		BIT(2)

#define BMG160_REG_TEMP		0x08
#define BMG160_TEMP_CENTER_VAL		23

#define BMG160_MAX_STARTUP_TIME_MS	80

#define BMG160_AUTO_SUSPEND_DELAY_MS	2000

struct bmg160_data {
	struct regmap *regmap;
	struct iio_trigger *dready_trig;
	struct iio_trigger *motion_trig;
	struct mutex mutex;
	s16 buffer[8];
	u32 dps_range;
	int ev_enable_state;
	int slope_thres;
	bool dready_trigger_on;
	bool motion_trigger_on;
	int irq;
};

enum bmg160_axis {
	AXIS_X,
	AXIS_Y,
	AXIS_Z,
	AXIS_MAX,
};

static const struct {
	int odr;
	int filter;
	int bw_bits;
} bmg160_samp_freq_table[] = { {100, 32, 0x07},
			       {200, 64, 0x06},
			       {100, 12, 0x05},
			       {200, 23, 0x04},
			       {400, 47, 0x03},
			       {1000, 116, 0x02},
			       {2000, 230, 0x01} };

static const struct {
	int scale;
	int dps_range;
} bmg160_scale_table[] = { { 1065, BMG160_RANGE_2000DPS},
			   { 532, BMG160_RANGE_1000DPS},
			   { 266, BMG160_RANGE_500DPS},
			   { 133, BMG160_RANGE_250DPS},
			   { 66, BMG160_RANGE_125DPS} };

static int bmg160_set_mode(struct bmg160_data *data, u8 mode)
{
	struct device *dev = regmap_get_device(data->regmap);
	int ret;

	ret = regmap_write(data->regmap, BMG160_REG_PMU_LPW, mode);
	if (ret < 0) {
		dev_err(dev, "Error writing reg_pmu_lpw\n");
		return ret;
	}

	return 0;
}

static int bmg160_convert_freq_to_bit(int val)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(bmg160_samp_freq_table); ++i) {
		if (bmg160_samp_freq_table[i].odr == val)
			return bmg160_samp_freq_table[i].bw_bits;
	}

	return -EINVAL;
}

static int bmg160_set_bw(struct bmg160_data *data, int val)
{
	struct device *dev = regmap_get_device(data->regmap);
	int ret;
	int bw_bits;

	bw_bits = bmg160_convert_freq_to_bit(val);
	if (bw_bits < 0)
		return bw_bits;

	ret = regmap_write(data->regmap, BMG160_REG_PMU_BW, bw_bits);
	if (ret < 0) {
		dev_err(dev, "Error writing reg_pmu_bw\n");
		return ret;
	}

	return 0;
}

static int bmg160_get_filter(struct bmg160_data *data, int *val)
{
	struct device *dev = regmap_get_device(data->regmap);
	int ret;
	int i;
	unsigned int bw_bits;

	ret = regmap_read(data->regmap, BMG160_REG_PMU_BW, &bw_bits);
	if (ret < 0) {
		dev_err(dev, "Error reading reg_pmu_bw\n");
		return ret;
	}

	/* Ignore the readonly reserved bit. */
	bw_bits &= ~BMG160_REG_PMU_BW_RES;

	for (i = 0; i < ARRAY_SIZE(bmg160_samp_freq_table); ++i) {
		if (bmg160_samp_freq_table[i].bw_bits == bw_bits)
			break;
	}

	*val = bmg160_samp_freq_table[i].filter;

	return ret ? ret : IIO_VAL_INT;
}


static int bmg160_set_filter(struct bmg160_data *data, int val)
{
	struct device *dev = regmap_get_device(data->regmap);
	int ret;
	int i;

	for (i = 0; i < ARRAY_SIZE(bmg160_samp_freq_table); ++i) {
		if (bmg160_samp_freq_table[i].filter == val)
			break;
	}

	ret = regmap_write(data->regmap, BMG160_REG_PMU_BW,
			   bmg160_samp_freq_table[i].bw_bits);
	if (ret < 0) {
		dev_err(dev, "Error writing reg_pmu_bw\n");
		return ret;
	}

	return 0;
}

static int bmg160_chip_init(struct bmg160_data *data)
{
	struct device *dev = regmap_get_device(data->regmap);
	int ret;
	unsigned int val;

	/*
	 * Reset chip to get it in a known good state. A delay of 30ms after
	 * reset is required according to the datasheet.
	 */
	regmap_write(data->regmap, BMG160_GYRO_REG_RESET,
		     BMG160_GYRO_RESET_VAL);
	usleep_range(30000, 30700);

	ret = regmap_read(data->regmap, BMG160_REG_CHIP_ID, &val);
	if (ret < 0) {
		dev_err(dev, "Error reading reg_chip_id\n");
		return ret;
	}

	dev_dbg(dev, "Chip Id %x\n", val);
	if (val != BMG160_CHIP_ID_VAL) {
		dev_err(dev, "invalid chip %x\n", val);
		return -ENODEV;
	}

	ret = bmg160_set_mode(data, BMG160_MODE_NORMAL);
	if (ret < 0)
		return ret;

	/* Wait upto 500 ms to be ready after changing mode */
	usleep_range(500, 1000);

	/* Set Bandwidth */
	ret = bmg160_set_bw(data, BMG160_DEF_BW);
	if (ret < 0)
		return ret;

	/* Set Default Range */
	ret = regmap_write(data->regmap, BMG160_REG_RANGE, BMG160_RANGE_500DPS);
	if (ret < 0) {
		dev_err(dev, "Error writing reg_range\n");
		return ret;
	}
	data->dps_range = BMG160_RANGE_500DPS;

	ret = regmap_read(data->regmap, BMG160_REG_SLOPE_THRES, &val);
	if (ret < 0) {
		dev_err(dev, "Error reading reg_slope_thres\n");
		return ret;
	}
	data->slope_thres = val;

	/* Set default interrupt mode */
	ret = regmap_update_bits(data->regmap, BMG160_REG_INT_EN_1,
				 BMG160_INT1_BIT_OD, 0);
	if (ret < 0) {
		dev_err(dev, "Error updating bits in reg_int_en_1\n");
		return ret;
	}

	ret = regmap_write(data->regmap, BMG160_REG_INT_RST_LATCH,
			   BMG160_INT_MODE_LATCH_INT |
			   BMG160_INT_MODE_LATCH_RESET);
	if (ret < 0) {
		dev_err(dev,
			"Error writing reg_motion_intr\n");
		return ret;
	}

	return 0;
}

static int bmg160_set_power_state(struct bmg160_data *data, bool on)
{
#ifdef CONFIG_PM
	struct device *dev = regmap_get_device(data->regmap);
	int ret;

	if (on)
		ret = pm_runtime_get_sync(dev);
	else {
		pm_runtime_mark_last_busy(dev);
		ret = pm_runtime_put_autosuspend(dev);
	}

	if (ret < 0) {
		dev_err(dev, "Failed: bmg160_set_power_state for %d\n", on);

		if (on)
			pm_runtime_put_noidle(dev);

		return ret;
	}
#endif

	return 0;
}

static int bmg160_setup_any_motion_interrupt(struct bmg160_data *data,
					     bool status)
{
	struct device *dev = regmap_get_device(data->regmap);
	int ret;

	/* Enable/Disable INT_MAP0 mapping */
	ret = regmap_update_bits(data->regmap, BMG160_REG_INT_MAP_0,
				 BMG160_INT_MAP_0_BIT_ANY,
				 (status ? BMG160_INT_MAP_0_BIT_ANY : 0));
	if (ret < 0) {
		dev_err(dev, "Error updating bits reg_int_map0\n");
		return ret;
	}

	/* Enable/Disable slope interrupts */
	if (status) {
		/* Update slope thres */
		ret = regmap_write(data->regmap, BMG160_REG_SLOPE_THRES,
				   data->slope_thres);
		if (ret < 0) {
			dev_err(dev, "Error writing reg_slope_thres\n");
			return ret;
		}

		ret = regmap_write(data->regmap, BMG160_REG_MOTION_INTR,
				   BMG160_INT_MOTION_X | BMG160_INT_MOTION_Y |
				   BMG160_INT_MOTION_Z);
		if (ret < 0) {
			dev_err(dev, "Error writing reg_motion_intr\n");
			return ret;
		}

		/*
		 * New data interrupt is always non-latched,
		 * which will have higher priority, so no need
		 * to set latched mode, we will be flooded anyway with INTR
		 */
		if (!data->dready_trigger_on) {
			ret = regmap_write(data->regmap,
					   BMG160_REG_INT_RST_LATCH,
					   BMG160_INT_MODE_LATCH_INT |
					   BMG160_INT_MODE_LATCH_RESET);
			if (ret < 0) {
				dev_err(dev, "Error writing reg_rst_latch\n");
				return ret;
			}
		}

		ret = regmap_write(data->regmap, BMG160_REG_INT_EN_0,
				   BMG160_DATA_ENABLE_INT);

	} else {
		ret = regmap_write(data->regmap, BMG160_REG_INT_EN_0, 0);
	}

	if (ret < 0) {
		dev_err(dev, "Error writing reg_int_en0\n");
		return ret;
	}

	return 0;
}

static int bmg160_setup_new_data_interrupt(struct bmg160_data *data,
					   bool status)
{
	struct device *dev = regmap_get_device(data->regmap);
	int ret;

	/* Enable/Disable INT_MAP1 mapping */
	ret = regmap_update_bits(data->regmap, BMG160_REG_INT_MAP_1,
				 BMG160_INT_MAP_1_BIT_NEW_DATA,
				 (status ? BMG160_INT_MAP_1_BIT_NEW_DATA : 0));
	if (ret < 0) {
		dev_err(dev, "Error updating bits in reg_int_map1\n");
		return ret;
	}

	if (status) {
		ret = regmap_write(data->regmap, BMG160_REG_INT_RST_LATCH,
				   BMG160_INT_MODE_NON_LATCH_INT |
				   BMG160_INT_MODE_LATCH_RESET);
		if (ret < 0) {
			dev_err(dev, "Error writing reg_rst_latch\n");
			return ret;
		}

		ret = regmap_write(data->regmap, BMG160_REG_INT_EN_0,
				   BMG160_DATA_ENABLE_INT);

	} else {
		/* Restore interrupt mode */
		ret = regmap_write(data->regmap, BMG160_REG_INT_RST_LATCH,
				   BMG160_INT_MODE_LATCH_INT |
				   BMG160_INT_MODE_LATCH_RESET);
		if (ret < 0) {
			dev_err(dev, "Error writing reg_rst_latch\n");
			return ret;
		}

		ret = regmap_write(data->regmap, BMG160_REG_INT_EN_0, 0);
	}

	if (ret < 0) {
		dev_err(dev, "Error writing reg_int_en0\n");
		return ret;
	}

	return 0;
}

static int bmg160_get_bw(struct bmg160_data *data, int *val)
{
	struct device *dev = regmap_get_device(data->regmap);	
	int i;
	unsigned int bw_bits;
	int ret;

	ret = regmap_read(data->regmap, BMG160_REG_PMU_BW, &bw_bits);
	if (ret < 0) {
		dev_err(dev, "Error reading reg_pmu_bw\n");
		return ret;
	}

	/* Ignore the readonly reserved bit. */
	bw_bits &= ~BMG160_REG_PMU_BW_RES;

	for (i = 0; i < ARRAY_SIZE(bmg160_samp_freq_table); ++i) {
		if (bmg160_samp_freq_table[i].bw_bits == bw_bits) {
			*val = bmg160_samp_freq_table[i].odr;
			return IIO_VAL_INT;
		}
	}

	return -EINVAL;
}

static int bmg160_set_scale(struct bmg160_data *data, int val)
{
	struct device *dev = regmap_get_device(data->regmap);
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(bmg160_scale_table); ++i) {
		if (bmg160_scale_table[i].scale == val) {
			ret = regmap_write(data->regmap, BMG160_REG_RANGE,
					   bmg160_scale_table[i].dps_range);
			if (ret < 0) {
				dev_err(dev, "Error writing reg_range\n");
				return ret;
			}
			data->dps_range = bmg160_scale_table[i].dps_range;
			return 0;
		}
	}

	return -EINVAL;
}

static int bmg160_get_temp(struct bmg160_data *data, int *val)
{
	struct device *dev = regmap_get_device(data->regmap);
	int ret;
	unsigned int raw_val;

	mutex_lock(&data->mutex);
	ret = bmg160_set_power_state(data, true);
	if (ret < 0) {
		mutex_unlock(&data->mutex);
		return ret;
	}

	ret = regmap_read(data->regmap, BMG160_REG_TEMP, &raw_val);
	if (ret < 0) {
		dev_err(dev, "Error reading reg_temp\n");
		bmg160_set_power_state(data, false);
		mutex_unlock(&data->mutex);
		return ret;
	}

	*val = sign_extend32(raw_val, 7);
	ret = bmg160_set_power_state(data, false);
	mutex_unlock(&data->mutex);
	if (ret < 0)
		return ret;

	return IIO_VAL_INT;
}

static int bmg160_get_axis(struct bmg160_data *data, int axis, int *val)
{
	struct device *dev = regmap_get_device(data->regmap);
	int ret;
	__le16 raw_val;

	mutex_lock(&data->mutex);
	ret = bmg160_set_power_state(data, true);
	if (ret < 0) {
		mutex_unlock(&data->mutex);
		return ret;
	}

	ret = regmap_bulk_read(data->regmap, BMG160_AXIS_TO_REG(axis), &raw_val,
			       sizeof(raw_val));
	if (ret < 0) {
		dev_err(dev, "Error reading axis %d\n", axis);
		bmg160_set_power_state(data, false);
		mutex_unlock(&data->mutex);
		return ret;
	}

	*val = sign_extend32(le16_to_cpu(raw_val), 15);
	ret = bmg160_set_power_state(data, false);
	mutex_unlock(&data->mutex);
	if (ret < 0)
		return ret;

	return IIO_VAL_INT;
}

static int bmg160_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct bmg160_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_TEMP:
			return bmg160_get_temp(data, val);
		case IIO_ANGL_VEL:
			if (iio_buffer_enabled(indio_dev))
				return -EBUSY;
			else
				return bmg160_get_axis(data, chan->scan_index,
						       val);
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_OFFSET:
		if (chan->type == IIO_TEMP) {
			*val = BMG160_TEMP_CENTER_VAL;
			return IIO_VAL_INT;
		} else
			return -EINVAL;
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		return bmg160_get_filter(data, val);
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		switch (chan->type) {
		case IIO_TEMP:
			*val2 = 500000;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_ANGL_VEL:
		{
			int i;

			for (i = 0; i < ARRAY_SIZE(bmg160_scale_table); ++i) {
				if (bmg160_scale_table[i].dps_range ==
							data->dps_range) {
					*val2 = bmg160_scale_table[i].scale;
					return IIO_VAL_INT_PLUS_MICRO;
				}
			}
			return -EINVAL;
		}
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val2 = 0;
		mutex_lock(&data->mutex);
		ret = bmg160_get_bw(data, val);
		mutex_unlock(&data->mutex);
		return ret;
	default:
		return -EINVAL;
	}
}

static int bmg160_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	struct bmg160_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		mutex_lock(&data->mutex);
		/*
		 * Section 4.2 of spec
		 * In suspend mode, the only supported operations are reading
		 * registers as well as writing to the (0x14) softreset
		 * register. Since we will be in suspend mode by default, change
		 * mode to power on for other writes.
		 */
		ret = bmg160_set_power_state(data, true);
		if (ret < 0) {
			mutex_unlock(&data->mutex);
			return ret;
		}
		ret = bmg160_set_bw(data, val);
		if (ret < 0) {
			bmg160_set_power_state(data, false);
			mutex_unlock(&data->mutex);
			return ret;
		}
		ret = bmg160_set_power_state(data, false);
		mutex_unlock(&data->mutex);
		return ret;
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		if (val2)
			return -EINVAL;

		mutex_lock(&data->mutex);
		ret = bmg160_set_power_state(data, true);
		if (ret < 0) {
			bmg160_set_power_state(data, false);
			mutex_unlock(&data->mutex);
			return ret;
		}
		ret = bmg160_set_filter(data, val);
		if (ret < 0) {
			bmg160_set_power_state(data, false);
			mutex_unlock(&data->mutex);
			return ret;
		}
		ret = bmg160_set_power_state(data, false);
		mutex_unlock(&data->mutex);
		return ret;
	case IIO_CHAN_INFO_SCALE:
		if (val)
			return -EINVAL;

		mutex_lock(&data->mutex);
		/* Refer to comments above for the suspend mode ops */
		ret = bmg160_set_power_state(data, true);
		if (ret < 0) {
			mutex_unlock(&data->mutex);
			return ret;
		}
		ret = bmg160_set_scale(data, val2);
		if (ret < 0) {
			bmg160_set_power_state(data, false);
			mutex_unlock(&data->mutex);
			return ret;
		}
		ret = bmg160_set_power_state(data, false);
		mutex_unlock(&data->mutex);
		return ret;
	default:
		return -EINVAL;
	}

	return -EINVAL;
}

static int bmg160_read_event(struct iio_dev *indio_dev,
			     const struct iio_chan_spec *chan,
			     enum iio_event_type type,
			     enum iio_event_direction dir,
			     enum iio_event_info info,
			     int *val, int *val2)
{
	struct bmg160_data *data = iio_priv(indio_dev);

	*val2 = 0;
	switch (info) {
	case IIO_EV_INFO_VALUE:
		*val = data->slope_thres & BMG160_SLOPE_THRES_MASK;
		break;
	default:
		return -EINVAL;
	}

	return IIO_VAL_INT;
}

static int bmg160_write_event(struct iio_dev *indio_dev,
			      const struct iio_chan_spec *chan,
			      enum iio_event_type type,
			      enum iio_event_direction dir,
			      enum iio_event_info info,
			      int val, int val2)
{
	struct bmg160_data *data = iio_priv(indio_dev);

	switch (info) {
	case IIO_EV_INFO_VALUE:
		if (data->ev_enable_state)
			return -EBUSY;
		data->slope_thres &= ~BMG160_SLOPE_THRES_MASK;
		data->slope_thres |= (val & BMG160_SLOPE_THRES_MASK);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int bmg160_read_event_config(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir)
{

	struct bmg160_data *data = iio_priv(indio_dev);

	return data->ev_enable_state;
}

static int bmg160_write_event_config(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir,
				     int state)
{
	struct bmg160_data *data = iio_priv(indio_dev);
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
	ret = bmg160_set_power_state(data, state);
	if (ret < 0) {
		mutex_unlock(&data->mutex);
		return ret;
	}

	ret =  bmg160_setup_any_motion_interrupt(data, state);
	if (ret < 0) {
		bmg160_set_power_state(data, false);
		mutex_unlock(&data->mutex);
		return ret;
	}

	data->ev_enable_state = state;
	mutex_unlock(&data->mutex);

	return 0;
}

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("100 200 400 1000 2000");

static IIO_CONST_ATTR(in_anglvel_scale_available,
		      "0.001065 0.000532 0.000266 0.000133 0.000066");

static struct attribute *bmg160_attributes[] = {
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	&iio_const_attr_in_anglvel_scale_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group bmg160_attrs_group = {
	.attrs = bmg160_attributes,
};

static const struct iio_event_spec bmg160_event = {
		.type = IIO_EV_TYPE_ROC,
		.dir = IIO_EV_DIR_EITHER,
		.mask_shared_by_type = BIT(IIO_EV_INFO_VALUE) |
				       BIT(IIO_EV_INFO_ENABLE)
};

#define BMG160_CHANNEL(_axis) {					\
	.type = IIO_ANGL_VEL,						\
	.modified = 1,							\
	.channel2 = IIO_MOD_##_axis,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),			\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |		\
		BIT(IIO_CHAN_INFO_SAMP_FREQ) |				\
		BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY),	\
	.scan_index = AXIS_##_axis,					\
	.scan_type = {							\
		.sign = 's',						\
		.realbits = 16,					\
		.storagebits = 16,					\
		.endianness = IIO_LE,					\
	},								\
	.event_spec = &bmg160_event,					\
	.num_event_specs = 1						\
}

static const struct iio_chan_spec bmg160_channels[] = {
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_OFFSET),
		.scan_index = -1,
	},
	BMG160_CHANNEL(X),
	BMG160_CHANNEL(Y),
	BMG160_CHANNEL(Z),
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static const struct iio_info bmg160_info = {
	.attrs			= &bmg160_attrs_group,
	.read_raw		= bmg160_read_raw,
	.write_raw		= bmg160_write_raw,
	.read_event_value	= bmg160_read_event,
	.write_event_value	= bmg160_write_event,
	.write_event_config	= bmg160_write_event_config,
	.read_event_config	= bmg160_read_event_config,
	.driver_module		= THIS_MODULE,
};

static const unsigned long bmg160_accel_scan_masks[] = {
					BIT(AXIS_X) | BIT(AXIS_Y) | BIT(AXIS_Z),
					0};

static irqreturn_t bmg160_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct bmg160_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->mutex);
	ret = regmap_bulk_read(data->regmap, BMG160_REG_XOUT_L,
			       data->buffer, AXIS_MAX * 2);
	mutex_unlock(&data->mutex);
	if (ret < 0)
		goto err;

	iio_push_to_buffers_with_timestamp(indio_dev, data->buffer,
					   pf->timestamp);
err:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int bmg160_trig_try_reen(struct iio_trigger *trig)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct bmg160_data *data = iio_priv(indio_dev);
	struct device *dev = regmap_get_device(data->regmap);
	int ret;

	/* new data interrupts don't need ack */
	if (data->dready_trigger_on)
		return 0;

	/* Set latched mode interrupt and clear any latched interrupt */
	ret = regmap_write(data->regmap, BMG160_REG_INT_RST_LATCH,
			   BMG160_INT_MODE_LATCH_INT |
			   BMG160_INT_MODE_LATCH_RESET);
	if (ret < 0) {
		dev_err(dev, "Error writing reg_rst_latch\n");
		return ret;
	}

	return 0;
}

static int bmg160_data_rdy_trigger_set_state(struct iio_trigger *trig,
					     bool state)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct bmg160_data *data = iio_priv(indio_dev);
	int ret;

	mutex_lock(&data->mutex);

	if (!state && data->ev_enable_state && data->motion_trigger_on) {
		data->motion_trigger_on = false;
		mutex_unlock(&data->mutex);
		return 0;
	}

	/*
	 * Refer to comment in bmg160_write_event_config for
	 * enable/disable operation order
	 */
	ret = bmg160_set_power_state(data, state);
	if (ret < 0) {
		mutex_unlock(&data->mutex);
		return ret;
	}
	if (data->motion_trig == trig)
		ret =  bmg160_setup_any_motion_interrupt(data, state);
	else
		ret = bmg160_setup_new_data_interrupt(data, state);
	if (ret < 0) {
		bmg160_set_power_state(data, false);
		mutex_unlock(&data->mutex);
		return ret;
	}
	if (data->motion_trig == trig)
		data->motion_trigger_on = state;
	else
		data->dready_trigger_on = state;

	mutex_unlock(&data->mutex);

	return 0;
}

static const struct iio_trigger_ops bmg160_trigger_ops = {
	.set_trigger_state = bmg160_data_rdy_trigger_set_state,
	.try_reenable = bmg160_trig_try_reen,
	.owner = THIS_MODULE,
};

static irqreturn_t bmg160_event_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct bmg160_data *data = iio_priv(indio_dev);
	struct device *dev = regmap_get_device(data->regmap);
	int ret;
	int dir;
	unsigned int val;

	ret = regmap_read(data->regmap, BMG160_REG_INT_STATUS_2, &val);
	if (ret < 0) {
		dev_err(dev, "Error reading reg_int_status2\n");
		goto ack_intr_status;
	}

	if (val & 0x08)
		dir = IIO_EV_DIR_RISING;
	else
		dir = IIO_EV_DIR_FALLING;

	if (val & BMG160_ANY_MOTION_BIT_X)
		iio_push_event(indio_dev, IIO_MOD_EVENT_CODE(IIO_ANGL_VEL,
							     0,
							     IIO_MOD_X,
							     IIO_EV_TYPE_ROC,
							     dir),
			       iio_get_time_ns(indio_dev));
	if (val & BMG160_ANY_MOTION_BIT_Y)
		iio_push_event(indio_dev, IIO_MOD_EVENT_CODE(IIO_ANGL_VEL,
							     0,
							     IIO_MOD_Y,
							     IIO_EV_TYPE_ROC,
							     dir),
			       iio_get_time_ns(indio_dev));
	if (val & BMG160_ANY_MOTION_BIT_Z)
		iio_push_event(indio_dev, IIO_MOD_EVENT_CODE(IIO_ANGL_VEL,
							     0,
							     IIO_MOD_Z,
							     IIO_EV_TYPE_ROC,
							     dir),
			       iio_get_time_ns(indio_dev));

ack_intr_status:
	if (!data->dready_trigger_on) {
		ret = regmap_write(data->regmap, BMG160_REG_INT_RST_LATCH,
				   BMG160_INT_MODE_LATCH_INT |
				   BMG160_INT_MODE_LATCH_RESET);
		if (ret < 0)
			dev_err(dev, "Error writing reg_rst_latch\n");
	}

	return IRQ_HANDLED;
}

static irqreturn_t bmg160_data_rdy_trig_poll(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct bmg160_data *data = iio_priv(indio_dev);

	if (data->dready_trigger_on)
		iio_trigger_poll(data->dready_trig);
	else if (data->motion_trigger_on)
		iio_trigger_poll(data->motion_trig);

	if (data->ev_enable_state)
		return IRQ_WAKE_THREAD;
	else
		return IRQ_HANDLED;

}

static int bmg160_buffer_preenable(struct iio_dev *indio_dev)
{
	struct bmg160_data *data = iio_priv(indio_dev);

	return bmg160_set_power_state(data, true);
}

static int bmg160_buffer_postdisable(struct iio_dev *indio_dev)
{
	struct bmg160_data *data = iio_priv(indio_dev);

	return bmg160_set_power_state(data, false);
}

static const struct iio_buffer_setup_ops bmg160_buffer_setup_ops = {
	.preenable = bmg160_buffer_preenable,
	.postenable = iio_triggered_buffer_postenable,
	.predisable = iio_triggered_buffer_predisable,
	.postdisable = bmg160_buffer_postdisable,
};

static const char *bmg160_match_acpi_device(struct device *dev)
{
	const struct acpi_device_id *id;

	id = acpi_match_device(dev->driver->acpi_match_table, dev);
	if (!id)
		return NULL;

	return dev_name(dev);
}

int bmg160_core_probe(struct device *dev, struct regmap *regmap, int irq,
		      const char *name)
{
	struct bmg160_data *data;
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	dev_set_drvdata(dev, indio_dev);
	data->irq = irq;
	data->regmap = regmap;

	ret = bmg160_chip_init(data);
	if (ret < 0)
		return ret;

	mutex_init(&data->mutex);

	if (ACPI_HANDLE(dev))
		name = bmg160_match_acpi_device(dev);

	indio_dev->dev.parent = dev;
	indio_dev->channels = bmg160_channels;
	indio_dev->num_channels = ARRAY_SIZE(bmg160_channels);
	indio_dev->name = name;
	indio_dev->available_scan_masks = bmg160_accel_scan_masks;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &bmg160_info;

	if (data->irq > 0) {
		ret = devm_request_threaded_irq(dev,
						data->irq,
						bmg160_data_rdy_trig_poll,
						bmg160_event_handler,
						IRQF_TRIGGER_RISING,
						BMG160_IRQ_NAME,
						indio_dev);
		if (ret)
			return ret;

		data->dready_trig = devm_iio_trigger_alloc(dev,
							   "%s-dev%d",
							   indio_dev->name,
							   indio_dev->id);
		if (!data->dready_trig)
			return -ENOMEM;

		data->motion_trig = devm_iio_trigger_alloc(dev,
							  "%s-any-motion-dev%d",
							  indio_dev->name,
							  indio_dev->id);
		if (!data->motion_trig)
			return -ENOMEM;

		data->dready_trig->dev.parent = dev;
		data->dready_trig->ops = &bmg160_trigger_ops;
		iio_trigger_set_drvdata(data->dready_trig, indio_dev);
		ret = iio_trigger_register(data->dready_trig);
		if (ret)
			return ret;

		data->motion_trig->dev.parent = dev;
		data->motion_trig->ops = &bmg160_trigger_ops;
		iio_trigger_set_drvdata(data->motion_trig, indio_dev);
		ret = iio_trigger_register(data->motion_trig);
		if (ret) {
			data->motion_trig = NULL;
			goto err_trigger_unregister;
		}
	}

	ret = iio_triggered_buffer_setup(indio_dev,
					 iio_pollfunc_store_time,
					 bmg160_trigger_handler,
					 &bmg160_buffer_setup_ops);
	if (ret < 0) {
		dev_err(dev,
			"iio triggered buffer setup failed\n");
		goto err_trigger_unregister;
	}

	ret = pm_runtime_set_active(dev);
	if (ret)
		goto err_buffer_cleanup;

	pm_runtime_enable(dev);
	pm_runtime_set_autosuspend_delay(dev,
					 BMG160_AUTO_SUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(dev);

	ret = iio_device_register(indio_dev);
	if (ret < 0) {
		dev_err(dev, "unable to register iio device\n");
		goto err_buffer_cleanup;
	}

	return 0;

err_buffer_cleanup:
	iio_triggered_buffer_cleanup(indio_dev);
err_trigger_unregister:
	if (data->dready_trig)
		iio_trigger_unregister(data->dready_trig);
	if (data->motion_trig)
		iio_trigger_unregister(data->motion_trig);

	return ret;
}
EXPORT_SYMBOL_GPL(bmg160_core_probe);

void bmg160_core_remove(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmg160_data *data = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	pm_runtime_put_noidle(dev);

	iio_triggered_buffer_cleanup(indio_dev);

	if (data->dready_trig) {
		iio_trigger_unregister(data->dready_trig);
		iio_trigger_unregister(data->motion_trig);
	}

	mutex_lock(&data->mutex);
	bmg160_set_mode(data, BMG160_MODE_DEEP_SUSPEND);
	mutex_unlock(&data->mutex);
}
EXPORT_SYMBOL_GPL(bmg160_core_remove);

#ifdef CONFIG_PM_SLEEP
static int bmg160_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmg160_data *data = iio_priv(indio_dev);

	mutex_lock(&data->mutex);
	bmg160_set_mode(data, BMG160_MODE_SUSPEND);
	mutex_unlock(&data->mutex);

	return 0;
}

static int bmg160_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmg160_data *data = iio_priv(indio_dev);

	mutex_lock(&data->mutex);
	if (data->dready_trigger_on || data->motion_trigger_on ||
							data->ev_enable_state)
		bmg160_set_mode(data, BMG160_MODE_NORMAL);
	mutex_unlock(&data->mutex);

	return 0;
}
#endif

#ifdef CONFIG_PM
static int bmg160_runtime_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmg160_data *data = iio_priv(indio_dev);
	int ret;

	ret = bmg160_set_mode(data, BMG160_MODE_SUSPEND);
	if (ret < 0) {
		dev_err(dev, "set mode failed\n");
		return -EAGAIN;
	}

	return 0;
}

static int bmg160_runtime_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct bmg160_data *data = iio_priv(indio_dev);
	int ret;

	ret = bmg160_set_mode(data, BMG160_MODE_NORMAL);
	if (ret < 0)
		return ret;

	msleep_interruptible(BMG160_MAX_STARTUP_TIME_MS);

	return 0;
}
#endif

const struct dev_pm_ops bmg160_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(bmg160_suspend, bmg160_resume)
	SET_RUNTIME_PM_OPS(bmg160_runtime_suspend,
			   bmg160_runtime_resume, NULL)
};
EXPORT_SYMBOL_GPL(bmg160_pm_ops);

MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("BMG160 Gyro driver");
