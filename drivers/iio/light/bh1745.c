// SPDX-License-Identifier: GPL-2.0
/*
 * ROHM BH1745 digital colour sensor driver
 *
 * Copyright (C) Mudit Sharma <muditsharma.info@gmail.com>
 *
 * 7-bit I2C slave addresses:
 *  0x38 (ADDR pin low)
 *  0x39 (ADDR pin high)
 */

#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/util_macros.h>
#include <linux/iio/events.h>
#include <linux/regmap.h>
#include <linux/bits.h>
#include <linux/bitfield.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/iio-gts-helper.h>

/* BH1745 configuration registers */

/* System control */
#define BH1745_SYS_CTRL 0x40
#define BH1745_SYS_CTRL_SW_RESET BIT(7)
#define BH1745_SYS_CTRL_INTR_RESET BIT(6)
#define BH1745_SYS_CTRL_PART_ID_MASK GENMASK(5, 0)
#define BH1745_PART_ID 0x0B

/* Mode control 1 */
#define BH1745_MODE_CTRL1 0x41
#define BH1745_CTRL1_MEASUREMENT_TIME_MASK GENMASK(2, 0)

/* Mode control 2 */
#define BH1745_MODE_CTRL2 0x42
#define BH1745_CTRL2_RGBC_EN BIT(4)
#define BH1745_CTRL2_ADC_GAIN_MASK GENMASK(1, 0)

/* Interrupt */
#define BH1745_INTR 0x60
#define BH1745_INTR_STATUS BIT(7)
#define BH1745_INTR_SOURCE_MASK GENMASK(3, 2)
#define BH1745_INTR_ENABLE BIT(0)

#define BH1745_PERSISTENCE 0x61

/* Threshold high */
#define BH1745_TH_LSB 0x62
#define BH1745_TH_MSB 0x63

/* Threshold low */
#define BH1745_TL_LSB 0x64
#define BH1745_TL_MSB 0x65

/* BH1745 data output regs */
#define BH1745_RED_LSB 0x50
#define BH1745_RED_MSB 0x51
#define BH1745_GREEN_LSB 0x52
#define BH1745_GREEN_MSB 0x53
#define BH1745_BLUE_LSB 0x54
#define BH1745_BLUE_MSB 0x55
#define BH1745_CLEAR_LSB 0x56
#define BH1745_CLEAR_MSB 0x57

#define BH1745_MANU_ID_REG 0x92

/* From 16x max HW gain and 32x max integration time */
#define BH1745_MAX_GAIN 512

enum bh1745_int_source {
	BH1745_INTR_SOURCE_RED,
	BH1745_INTR_SOURCE_GREEN,
	BH1745_INTR_SOURCE_BLUE,
	BH1745_INTR_SOURCE_CLEAR,
};

enum bh1745_gain {
	BH1745_ADC_GAIN_1X,
	BH1745_ADC_GAIN_2X,
	BH1745_ADC_GAIN_16X,
};

enum bh1745_measurement_time {
	BH1745_MEASUREMENT_TIME_160MS,
	BH1745_MEASUREMENT_TIME_320MS,
	BH1745_MEASUREMENT_TIME_640MS,
	BH1745_MEASUREMENT_TIME_1280MS,
	BH1745_MEASUREMENT_TIME_2560MS,
	BH1745_MEASUREMENT_TIME_5120MS,
};

enum bh1745_presistence_value {
	BH1745_PRESISTENCE_UPDATE_TOGGLE,
	BH1745_PRESISTENCE_UPDATE_EACH_MEASUREMENT,
	BH1745_PRESISTENCE_UPDATE_FOUR_MEASUREMENT,
	BH1745_PRESISTENCE_UPDATE_EIGHT_MEASUREMENT,
};

static const struct iio_gain_sel_pair bh1745_gain[] = {
	GAIN_SCALE_GAIN(1, BH1745_ADC_GAIN_1X),
	GAIN_SCALE_GAIN(2, BH1745_ADC_GAIN_2X),
	GAIN_SCALE_GAIN(16, BH1745_ADC_GAIN_16X),
};

static const struct iio_itime_sel_mul bh1745_itimes[] = {
	GAIN_SCALE_ITIME_US(5120000, BH1745_MEASUREMENT_TIME_5120MS, 32),
	GAIN_SCALE_ITIME_US(2560000, BH1745_MEASUREMENT_TIME_2560MS, 16),
	GAIN_SCALE_ITIME_US(1280000, BH1745_MEASUREMENT_TIME_1280MS, 8),
	GAIN_SCALE_ITIME_US(640000, BH1745_MEASUREMENT_TIME_640MS, 4),
	GAIN_SCALE_ITIME_US(320000, BH1745_MEASUREMENT_TIME_320MS, 2),
	GAIN_SCALE_ITIME_US(160000, BH1745_MEASUREMENT_TIME_160MS, 1),
};

struct bh1745_data {
	/*
	 * Lock to prevent device setting update or read before
	 * related calculations are completed
	 */
	struct mutex lock;
	struct regmap *regmap;
	struct device *dev;
	struct iio_trigger *trig;
	struct iio_gts gts;
};

static const struct regmap_range bh1745_volatile_ranges[] = {
	regmap_reg_range(BH1745_MODE_CTRL2, BH1745_MODE_CTRL2), /* VALID */
	regmap_reg_range(BH1745_RED_LSB, BH1745_CLEAR_MSB), /* Data */
	regmap_reg_range(BH1745_INTR, BH1745_INTR), /* Interrupt */
};

static const struct regmap_access_table bh1745_volatile_regs = {
	.yes_ranges = bh1745_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(bh1745_volatile_ranges),
};

static const struct regmap_range bh1745_readable_ranges[] = {
	regmap_reg_range(BH1745_SYS_CTRL, BH1745_MODE_CTRL2),
	regmap_reg_range(BH1745_RED_LSB, BH1745_CLEAR_MSB),
	regmap_reg_range(BH1745_INTR, BH1745_INTR),
	regmap_reg_range(BH1745_PERSISTENCE, BH1745_TL_MSB),
	regmap_reg_range(BH1745_MANU_ID_REG, BH1745_MANU_ID_REG),
};

static const struct regmap_access_table bh1745_readable_regs = {
	.yes_ranges = bh1745_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(bh1745_readable_ranges),
};

static const struct regmap_range bh1745_writable_ranges[] = {
	regmap_reg_range(BH1745_SYS_CTRL, BH1745_MODE_CTRL2),
	regmap_reg_range(BH1745_INTR, BH1745_INTR),
	regmap_reg_range(BH1745_PERSISTENCE, BH1745_TL_MSB),
};

static const struct regmap_access_table bh1745_writable_regs = {
	.yes_ranges = bh1745_writable_ranges,
	.n_yes_ranges = ARRAY_SIZE(bh1745_writable_ranges),
};

static const struct regmap_config bh1745_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = BH1745_MANU_ID_REG,
	.cache_type = REGCACHE_RBTREE,
	.volatile_table = &bh1745_volatile_regs,
	.wr_table = &bh1745_writable_regs,
	.rd_table = &bh1745_readable_regs,
};

static const struct iio_event_spec bh1745_event_spec[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_shared_by_type = BIT(IIO_EV_INFO_VALUE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_shared_by_type = BIT(IIO_EV_INFO_VALUE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_shared_by_type = BIT(IIO_EV_INFO_PERIOD),
		.mask_separate = BIT(IIO_EV_INFO_ENABLE),
	},
};

#define BH1745_CHANNEL(_colour, _si, _addr)                             \
	{                                                               \
		.type = IIO_INTENSITY, .modified = 1,                   \
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),           \
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SCALE) |   \
					   BIT(IIO_CHAN_INFO_INT_TIME), \
		.info_mask_shared_by_all_available =                    \
			BIT(IIO_CHAN_INFO_SCALE) |                      \
			BIT(IIO_CHAN_INFO_INT_TIME),                    \
		.event_spec = bh1745_event_spec,                        \
		.num_event_specs = ARRAY_SIZE(bh1745_event_spec),       \
		.channel2 = IIO_MOD_LIGHT_##_colour, .address = _addr,  \
		.scan_index = _si,                                      \
		.scan_type = {                                          \
			.sign = 'u',                                    \
			.realbits = 16,                                 \
			.storagebits = 16,                              \
			.endianness = IIO_CPU,                          \
		},                                                      \
	}

static const struct iio_chan_spec bh1745_channels[] = {
	BH1745_CHANNEL(RED, 0, BH1745_RED_LSB),
	BH1745_CHANNEL(GREEN, 1, BH1745_GREEN_LSB),
	BH1745_CHANNEL(BLUE, 2, BH1745_BLUE_LSB),
	BH1745_CHANNEL(CLEAR, 3, BH1745_CLEAR_LSB),
	IIO_CHAN_SOFT_TIMESTAMP(4),
};

static int bh1745_reset(struct bh1745_data *data)
{
	return regmap_set_bits(data->regmap, BH1745_SYS_CTRL,
			       BH1745_SYS_CTRL_SW_RESET |
			       BH1745_SYS_CTRL_INTR_RESET);
}

static int bh1745_power_on(struct bh1745_data *data)
{
	return regmap_set_bits(data->regmap, BH1745_MODE_CTRL2,
			       BH1745_CTRL2_RGBC_EN);
}

static void bh1745_power_off(void *data_ptr)
{
	struct bh1745_data *data = data_ptr;
	struct device *dev = data->dev;
	int ret;

	ret = regmap_clear_bits(data->regmap, BH1745_MODE_CTRL2,
				BH1745_CTRL2_RGBC_EN);
	if (ret)
		dev_err(dev, "Failed to turn off device\n");
}

static int bh1745_get_scale(struct bh1745_data *data, int *val, int *val2)
{
	int ret;
	int value;
	int gain_sel, int_time_sel;
	int gain;
	const struct iio_itime_sel_mul *int_time;

	ret = regmap_read(data->regmap, BH1745_MODE_CTRL2, &value);
	if (ret)
		return ret;

	gain_sel = FIELD_GET(BH1745_CTRL2_ADC_GAIN_MASK, value);
	gain = iio_gts_find_gain_by_sel(&data->gts, gain_sel);

	ret = regmap_read(data->regmap, BH1745_MODE_CTRL1, &value);
	if (ret)
		return ret;

	int_time_sel = FIELD_GET(BH1745_CTRL1_MEASUREMENT_TIME_MASK, value);
	int_time = iio_gts_find_itime_by_sel(&data->gts, int_time_sel);

	return iio_gts_get_scale(&data->gts, gain, int_time->time_us, val,
				 val2);
}

static int bh1745_set_scale(struct bh1745_data *data, int val)
{
	struct device *dev = data->dev;
	int ret;
	int value;
	int hw_gain_sel, current_int_time_sel, new_int_time_sel;

	ret = regmap_read(data->regmap, BH1745_MODE_CTRL1, &value);
	if (ret)
		return ret;

	current_int_time_sel = FIELD_GET(BH1745_CTRL1_MEASUREMENT_TIME_MASK,
					 value);
	ret = iio_gts_find_gain_sel_for_scale_using_time(&data->gts,
							 current_int_time_sel,
							 val, 0, &hw_gain_sel);
	if (ret) {
		for (int i = 0; i < data->gts.num_itime; i++) {
			new_int_time_sel = data->gts.itime_table[i].sel;

			if (new_int_time_sel == current_int_time_sel)
				continue;

			ret = iio_gts_find_gain_sel_for_scale_using_time(&data->gts,
									 new_int_time_sel,
									 val, 0,
									 &hw_gain_sel);
			if (!ret)
				break;
		}

		if (ret) {
			dev_dbg(dev, "Unsupported scale value requested: %d\n",
				val);
			return -EINVAL;
		}

		ret = regmap_write_bits(data->regmap, BH1745_MODE_CTRL1,
					BH1745_CTRL1_MEASUREMENT_TIME_MASK,
					new_int_time_sel);
		if (ret)
			return ret;
	}

	return regmap_write_bits(data->regmap, BH1745_MODE_CTRL2,
				 BH1745_CTRL2_ADC_GAIN_MASK, hw_gain_sel);
}

static int bh1745_get_int_time(struct bh1745_data *data, int *val)
{
	int ret;
	int value;
	int int_time, int_time_sel;

	ret = regmap_read(data->regmap, BH1745_MODE_CTRL1, &value);
	if (ret)
		return ret;

	int_time_sel = FIELD_GET(BH1745_CTRL1_MEASUREMENT_TIME_MASK, value);
	int_time = iio_gts_find_int_time_by_sel(&data->gts, int_time_sel);
	if (int_time < 0)
		return int_time;

	*val = int_time;

	return 0;
}

static int bh1745_set_int_time(struct bh1745_data *data, int val, int val2)
{
	struct device *dev = data->dev;
	int ret;
	int value;
	int current_int_time, current_hwgain_sel, current_hwgain;
	int new_hwgain, new_hwgain_sel, new_int_time_sel;
	int req_int_time = (1000000 * val) + val2;

	if (!iio_gts_valid_time(&data->gts, req_int_time)) {
		dev_dbg(dev, "Unsupported integration time requested: %d\n",
			req_int_time);
		return -EINVAL;
	}

	ret = bh1745_get_int_time(data, &current_int_time);
	if (ret)
		return ret;

	if (current_int_time == req_int_time)
		return 0;

	ret = regmap_read(data->regmap, BH1745_MODE_CTRL2, &value);
	if (ret)
		return ret;

	current_hwgain_sel = FIELD_GET(BH1745_CTRL2_ADC_GAIN_MASK, value);
	current_hwgain = iio_gts_find_gain_by_sel(&data->gts,
						  current_hwgain_sel);
	ret = iio_gts_find_new_gain_by_old_gain_time(&data->gts, current_hwgain,
						     current_int_time,
						     req_int_time,
						     &new_hwgain);
	if (new_hwgain < 0) {
		dev_dbg(dev, "No corresponding gain for requested integration time\n");
		return ret;
	}

	if (ret) {
		bool in_range;

		new_hwgain = iio_find_closest_gain_low(&data->gts, new_hwgain,
						       &in_range);
		if (new_hwgain < 0) {
			new_hwgain = iio_gts_get_min_gain(&data->gts);
			if (new_hwgain < 0)
				return ret;
		}

		if (!in_range)
			dev_dbg(dev, "Optimal gain out of range\n");

		dev_dbg(dev, "Scale changed, new hw_gain %d\n", new_hwgain);
	}

	new_hwgain_sel = iio_gts_find_sel_by_gain(&data->gts, new_hwgain);
	if (new_hwgain_sel < 0)
		return new_hwgain_sel;

	ret = regmap_write_bits(data->regmap, BH1745_MODE_CTRL2,
				BH1745_CTRL2_ADC_GAIN_MASK,
				new_hwgain_sel);
	if (ret)
		return ret;

	new_int_time_sel = iio_gts_find_sel_by_int_time(&data->gts,
							req_int_time);
	if (new_int_time_sel < 0)
		return new_int_time_sel;

	return regmap_write_bits(data->regmap, BH1745_MODE_CTRL1,
				 BH1745_CTRL1_MEASUREMENT_TIME_MASK,
				 new_int_time_sel);
}

static int bh1745_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct bh1745_data *data = iio_priv(indio_dev);
	int ret;
	int value;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;

		ret = regmap_bulk_read(data->regmap, chan->address, &value, 2);
		iio_device_release_direct(indio_dev);
		if (ret)
			return ret;
		*val = value;

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE: {
			guard(mutex)(&data->lock);
			ret = bh1745_get_scale(data, val, val2);
			if (ret)
				return ret;

			return IIO_VAL_INT;
		}

	case IIO_CHAN_INFO_INT_TIME: {
			guard(mutex)(&data->lock);
			*val = 0;
			ret = bh1745_get_int_time(data, val2);
			if (ret)
				return 0;

			return IIO_VAL_INT_PLUS_MICRO;
		}

	default:
		return -EINVAL;
	}
}

static int bh1745_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	struct bh1745_data *data = iio_priv(indio_dev);

	guard(mutex)(&data->lock);
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		return bh1745_set_scale(data, val);

	case IIO_CHAN_INFO_INT_TIME:
		return bh1745_set_int_time(data, val, val2);

	default:
		return -EINVAL;
	}
}

static int bh1745_write_raw_get_fmt(struct iio_dev *indio_dev,
				    struct iio_chan_spec const *chan,
				    long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_INT_TIME:
		return IIO_VAL_INT_PLUS_MICRO;

	default:
		return -EINVAL;
	}
}

static int bh1745_read_thresh(struct iio_dev *indio_dev,
			      const struct iio_chan_spec *chan,
			      enum iio_event_type type,
			      enum iio_event_direction dir,
			      enum iio_event_info info, int *val, int *val2)
{
	struct bh1745_data *data = iio_priv(indio_dev);
	int ret;

	switch (info) {
	case IIO_EV_INFO_VALUE:
		switch (dir) {
		case IIO_EV_DIR_RISING:
			ret = regmap_bulk_read(data->regmap, BH1745_TH_LSB,
					       val, 2);
			if (ret)
				return ret;

			return IIO_VAL_INT;

		case IIO_EV_DIR_FALLING:
			ret = regmap_bulk_read(data->regmap, BH1745_TL_LSB,
					       val, 2);
			if (ret)
				return ret;

			return IIO_VAL_INT;

		default:
			return -EINVAL;
		}

	case IIO_EV_INFO_PERIOD:
		ret = regmap_read(data->regmap, BH1745_PERSISTENCE, val);
		if (ret)
			return ret;

		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static int bh1745_write_thresh(struct iio_dev *indio_dev,
			       const struct iio_chan_spec *chan,
			       enum iio_event_type type,
			       enum iio_event_direction dir,
			       enum iio_event_info info, int val, int val2)
{
	struct bh1745_data *data = iio_priv(indio_dev);
	int ret;

	switch (info) {
	case IIO_EV_INFO_VALUE:
		if (val < 0x0 || val > 0xFFFF)
			return -EINVAL;

		switch (dir) {
		case IIO_EV_DIR_RISING:
			ret = regmap_bulk_write(data->regmap, BH1745_TH_LSB,
						&val, 2);
			if (ret)
				return ret;

			return IIO_VAL_INT;

		case IIO_EV_DIR_FALLING:
			ret = regmap_bulk_write(data->regmap, BH1745_TL_LSB,
						&val, 2);
			if (ret)
				return ret;

			return IIO_VAL_INT;

		default:
			return -EINVAL;
		}

	case IIO_EV_INFO_PERIOD:
		if (val < BH1745_PRESISTENCE_UPDATE_TOGGLE ||
		    val > BH1745_PRESISTENCE_UPDATE_EIGHT_MEASUREMENT)
			return -EINVAL;
		ret = regmap_write(data->regmap, BH1745_PERSISTENCE, val);
		if (ret)
			return ret;

		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static int bh1745_read_event_config(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir)
{
	struct bh1745_data *data = iio_priv(indio_dev);
	int ret;
	int value;
	int int_src;

	ret = regmap_read(data->regmap, BH1745_INTR, &value);
	if (ret)
		return ret;

	if (!FIELD_GET(BH1745_INTR_ENABLE, value))
		return 0;

	int_src = FIELD_GET(BH1745_INTR_SOURCE_MASK, value);

	switch (chan->channel2) {
	case IIO_MOD_LIGHT_RED:
		if (int_src == BH1745_INTR_SOURCE_RED)
			return 1;
		return 0;

	case IIO_MOD_LIGHT_GREEN:
		if (int_src == BH1745_INTR_SOURCE_GREEN)
			return 1;
		return 0;

	case IIO_MOD_LIGHT_BLUE:
		if (int_src == BH1745_INTR_SOURCE_BLUE)
			return 1;
		return 0;

	case IIO_MOD_LIGHT_CLEAR:
		if (int_src == BH1745_INTR_SOURCE_CLEAR)
			return 1;
		return 0;

	default:
		return -EINVAL;
	}
}

static int bh1745_write_event_config(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir, bool state)
{
	struct bh1745_data *data = iio_priv(indio_dev);
	int value;

	if (!state)
		return regmap_clear_bits(data->regmap,
					 BH1745_INTR, BH1745_INTR_ENABLE);

	/* Latch is always enabled when enabling interrupt */
	value = BH1745_INTR_ENABLE;

	switch (chan->channel2) {
	case IIO_MOD_LIGHT_RED:
		return regmap_write(data->regmap, BH1745_INTR,
				    value | FIELD_PREP(BH1745_INTR_SOURCE_MASK,
						       BH1745_INTR_SOURCE_RED));

	case IIO_MOD_LIGHT_GREEN:
		return regmap_write(data->regmap, BH1745_INTR,
				    value | FIELD_PREP(BH1745_INTR_SOURCE_MASK,
						       BH1745_INTR_SOURCE_GREEN));

	case IIO_MOD_LIGHT_BLUE:
		return regmap_write(data->regmap, BH1745_INTR,
				    value | FIELD_PREP(BH1745_INTR_SOURCE_MASK,
						       BH1745_INTR_SOURCE_BLUE));

	case IIO_MOD_LIGHT_CLEAR:
		return regmap_write(data->regmap, BH1745_INTR,
				    value | FIELD_PREP(BH1745_INTR_SOURCE_MASK,
						       BH1745_INTR_SOURCE_CLEAR));

	default:
		return -EINVAL;
	}
}

static int bh1745_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, const int **vals,
			     int *type, int *length, long mask)
{
	struct bh1745_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		return iio_gts_avail_times(&data->gts, vals, type, length);

	case IIO_CHAN_INFO_SCALE:
		return iio_gts_all_avail_scales(&data->gts, vals, type, length);

	default:
		return -EINVAL;
	}
}

static const struct iio_info bh1745_info = {
	.read_raw = bh1745_read_raw,
	.write_raw = bh1745_write_raw,
	.write_raw_get_fmt = bh1745_write_raw_get_fmt,
	.read_event_value = bh1745_read_thresh,
	.write_event_value = bh1745_write_thresh,
	.read_event_config = bh1745_read_event_config,
	.write_event_config = bh1745_write_event_config,
	.read_avail = bh1745_read_avail,
};

static irqreturn_t bh1745_interrupt_handler(int interrupt, void *p)
{
	struct iio_dev *indio_dev = p;
	struct bh1745_data *data = iio_priv(indio_dev);
	int ret;
	int value;
	int int_src;

	ret = regmap_read(data->regmap, BH1745_INTR, &value);
	if (ret)
		return IRQ_NONE;

	int_src = FIELD_GET(BH1745_INTR_SOURCE_MASK, value);

	if (value & BH1745_INTR_STATUS) {
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_INTENSITY, int_src,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_EITHER),
			       iio_get_time_ns(indio_dev));

		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static irqreturn_t bh1745_trigger_handler(int interrupt, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct bh1745_data *data = iio_priv(indio_dev);
	struct {
		u16 chans[4];
		aligned_s64 timestamp;
	} scan = { };
	u16 value;
	int ret;
	int i;
	int j = 0;

	iio_for_each_active_channel(indio_dev, i) {
		ret = regmap_bulk_read(data->regmap, BH1745_RED_LSB + 2 * i,
				       &value, 2);
		if (ret)
			goto err;

		scan.chans[j++] = value;
	}

	iio_push_to_buffers_with_timestamp(indio_dev, &scan,
					   iio_get_time_ns(indio_dev));

err:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int bh1745_setup_triggered_buffer(struct iio_dev *indio_dev,
					 struct device *parent,
					 int irq)
{
	struct bh1745_data *data = iio_priv(indio_dev);
	struct device *dev = data->dev;
	int ret;

	ret = devm_iio_triggered_buffer_setup(parent, indio_dev, NULL,
					      bh1745_trigger_handler, NULL);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Triggered buffer setup failed\n");

	if (irq) {
		ret = devm_request_threaded_irq(dev, irq, NULL,
						bh1745_interrupt_handler,
						IRQF_ONESHOT,
						"bh1745_interrupt", indio_dev);
		if (ret)
			return dev_err_probe(dev, ret,
					     "Request for IRQ failed\n");
	}

	return 0;
}

static int bh1745_init(struct bh1745_data *data)
{
	int ret;
	struct device *dev = data->dev;

	mutex_init(&data->lock);

	ret = devm_iio_init_iio_gts(dev, BH1745_MAX_GAIN, 0, bh1745_gain,
				    ARRAY_SIZE(bh1745_gain), bh1745_itimes,
				    ARRAY_SIZE(bh1745_itimes), &data->gts);
	if (ret)
		return ret;

	ret = bh1745_reset(data);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to reset sensor\n");

	ret = bh1745_power_on(data);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to turn on sensor\n");

	ret = devm_add_action_or_reset(dev, bh1745_power_off, data);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to add action or reset\n");

	return 0;
}

static int bh1745_probe(struct i2c_client *client)
{
	int ret;
	int value;
	int part_id;
	struct bh1745_data *data;
	struct iio_dev *indio_dev;
	struct device *dev = &client->dev;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	indio_dev->info = &bh1745_info;
	indio_dev->name = "bh1745";
	indio_dev->channels = bh1745_channels;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->num_channels = ARRAY_SIZE(bh1745_channels);
	data = iio_priv(indio_dev);
	data->dev = &client->dev;
	data->regmap = devm_regmap_init_i2c(client, &bh1745_regmap);
	if (IS_ERR(data->regmap))
		return dev_err_probe(dev, PTR_ERR(data->regmap),
				     "Failed to initialize Regmap\n");

	ret = regmap_read(data->regmap, BH1745_SYS_CTRL, &value);
	if (ret)
		return ret;

	part_id = FIELD_GET(BH1745_SYS_CTRL_PART_ID_MASK, value);
	if (part_id != BH1745_PART_ID)
		dev_warn(dev, "Unknown part ID 0x%x\n", part_id);

	ret = devm_regulator_get_enable(dev, "vdd");
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to get and enable regulator\n");

	ret = bh1745_init(data);
	if (ret)
		return ret;

	ret = bh1745_setup_triggered_buffer(indio_dev, indio_dev->dev.parent,
					    client->irq);
	if (ret)
		return ret;

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register device\n");

	return 0;
}

static const struct i2c_device_id bh1745_idtable[] = {
	{ "bh1745" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bh1745_idtable);

static const struct of_device_id bh1745_of_match[] = {
	{ .compatible = "rohm,bh1745" },
	{ }
};
MODULE_DEVICE_TABLE(of, bh1745_of_match);

static struct i2c_driver bh1745_driver = {
	.driver = {
		.name = "bh1745",
		.of_match_table = bh1745_of_match,
	},
	.probe = bh1745_probe,
	.id_table = bh1745_idtable,
};
module_i2c_driver(bh1745_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mudit Sharma <muditsharma.info@gmail.com>");
MODULE_DESCRIPTION("BH1745 colour sensor driver");
MODULE_IMPORT_NS("IIO_GTS_HELPER");
