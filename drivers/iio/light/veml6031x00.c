// SPDX-License-Identifier: GPL-2.0+
/*
 * VEML6031X00 Ambient Light Sensor
 *
 * Copyright (c) 2026, Javier Carrasco <javier.carrasco.cruz@gmail.com>
 */

#include <linux/array_size.h>
#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/limits.h>
#include <linux/lockdep.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>

#include <asm/byteorder.h>

#include <linux/iio/iio.h>
#include <linux/iio/events.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/iio-gts-helper.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>

/* Device registers */
#define VEML6031X00_REG_CONF0       0x00
#define VEML6031X00_REG_CONF1       0x01
#define VEML6031X00_REG_WH_L        0x04
#define VEML6031X00_REG_WH_H        0x05
#define VEML6031X00_REG_WL_L        0x06
#define VEML6031X00_REG_WL_H        0x07
#define VEML6031X00_REG_ALS_L       0x10
#define VEML6031X00_REG_ALS_H       0x11
#define VEML6031X00_REG_IR_L        0x12
#define VEML6031X00_REG_IR_H        0x13
#define VEML6031X00_REG_ID_L        0x14
#define VEML6031X00_REG_ID_H        0x15
#define VEML6031X00_REG_INT         0x17

/* Bit masks for specific functionality */
#define VEML6031X00_CONF0_SD        BIT(0)
#define VEML6031X00_CONF0_AF_TRIG   BIT(2)
#define VEML6031X00_CONF0_AF        BIT(3)
#define VEML6031X00_CONF1_ALS_CAL   BIT(0)
#define VEML6031X00_CONF1_IR_SD     BIT(7)
#define VEML6031X00_INT_TH_H        BIT(1)
#define VEML6031X00_INT_TH_L        BIT(2)
#define VEML6031X00_INT_DRDY        BIT(3)
#define VEML6031X00_INT_MASK \
	(VEML6031X00_INT_TH_L | VEML6031X00_INT_TH_H | VEML6031X00_INT_DRDY)

#define VEML6031X00_GAIN_SEL(pd_div4, gain) (((pd_div4) << 2) | (gain))

enum veml6031x00_scan {
	VEML6031X00_SCAN_ALS,
	VEML6031X00_SCAN_IR,
	VEML6031X00_SCAN_TIMESTAMP,
};

struct veml6031x00_rf {
	struct regmap_field *gain;
	struct regmap_field *int_en;
	struct regmap_field *it;
	struct regmap_field *pd_div4;
	struct regmap_field *pers;
};

struct veml6031x00_chip {
	const char *name;
	const unsigned int part_id;
};

struct veml6031x00_data {
	struct iio_gts gts;
	struct regmap *regmap;
	struct iio_trigger *trig;
	struct veml6031x00_rf rf;
	const struct veml6031x00_chip *chip;
	/*
	 * Serialize access to scale register fields scattered across multiple
	 * registers (rf.gain, rf.pd_div4, rf.it) to read and write them as a
	 * consistent set.
	 */
	struct mutex scale_lock;
	/*
	 * Serialize access to irq enable/disable by events and trigger
	 * (shared line).
	 */
	struct mutex irq_lock;
	unsigned int interrupt_users;
	bool ev_en;
	bool trig_en;
};

static const struct iio_itime_sel_mul veml6031x00_it_sel[] = {
	GAIN_SCALE_ITIME_US(3125, 0, 1),
	GAIN_SCALE_ITIME_US(6250, 1, 2),
	GAIN_SCALE_ITIME_US(12500, 2, 4),
	GAIN_SCALE_ITIME_US(25000, 3, 8),
	GAIN_SCALE_ITIME_US(50000, 4, 16),
	GAIN_SCALE_ITIME_US(100000, 5, 32),
	GAIN_SCALE_ITIME_US(200000, 6, 64),
	GAIN_SCALE_ITIME_US(400000, 7, 128),
};

/*
 * The gain selector encodes (PD_D4 << 2) | GAIN to identify each gain setting.
 * Gains are multiplied by 8 to work with integers. The values in the iio-gts
 * tables don't need corrections because the maximum value of the scale refers
 * to GAIN = x1, and the rest of the values are obtained from the resulting
 * linear function.
 * TODO: add support for GAIN_0_165 and GAIN_0_660
 */
#define VEML6031X00_SEL_GAIN_0_125 0x07
#define VEML6031X00_SEL_GAIN_0_250 0x04
#define VEML6031X00_SEL_GAIN_0_500 0x03
#define VEML6031X00_SEL_GAIN_1_000 0x00
#define VEML6031X00_SEL_GAIN_2_000 0x01
static const struct iio_gain_sel_pair veml6031x00_gain_sel[] = {
	GAIN_SCALE_GAIN(1, VEML6031X00_SEL_GAIN_0_125),
	GAIN_SCALE_GAIN(2, VEML6031X00_SEL_GAIN_0_250),
	GAIN_SCALE_GAIN(4, VEML6031X00_SEL_GAIN_0_500),
	GAIN_SCALE_GAIN(8, VEML6031X00_SEL_GAIN_1_000),
	GAIN_SCALE_GAIN(16, VEML6031X00_SEL_GAIN_2_000),
};

static IIO_CONST_ATTR(in_illuminance_thresh_either_period_available, "1 2 4 8");

static struct attribute *veml6031x00_event_attributes[] = {
	&iio_const_attr_in_illuminance_thresh_either_period_available.dev_attr.attr,
	NULL
};

static const struct attribute_group veml6031x00_event_attr_group = {
	.attrs = veml6031x00_event_attributes,
};

/*
 * The shutdown bits (SD and ALS_IR_SD) are in different registers, and both
 * must be updated when changing the device power state.
 */
static int veml6031x00_set_power(struct veml6031x00_data *data, bool state)
{
	int ret;

	ret = regmap_assign_bits(data->regmap, VEML6031X00_REG_CONF0,
				 VEML6031X00_CONF0_SD, !state);
	if (ret)
		return ret;

	return regmap_assign_bits(data->regmap, VEML6031X00_REG_CONF1,
				  VEML6031X00_CONF1_IR_SD, !state);
}

static void veml6031x00_als_shutdown_action(void *data)
{
	struct veml6031x00_data *priv = data;
	struct device *dev = regmap_get_device(priv->regmap);
	int ret;

	ret = veml6031x00_set_power(priv, false);
	if (ret)
		dev_warn(dev, "Failed to shut down device: %d\n", ret);
}

static const struct iio_event_spec veml6031x00_event_spec[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	}, {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	}, {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_PERIOD) |
				 BIT(IIO_EV_INFO_ENABLE),
	},
};

static const struct iio_chan_spec veml6031x00_channels[] = {
	{
		.type = IIO_LIGHT,
		.address = VEML6031X00_REG_ALS_L,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_INT_TIME) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_separate_available = BIT(IIO_CHAN_INFO_INT_TIME) |
						BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = VEML6031X00_SCAN_ALS,
		.scan_type = {
			.format = IIO_SCAN_FORMAT_UNSIGNED_INT,
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_LE,
		},
	},
	{
		.type = IIO_INTENSITY,
		.address = VEML6031X00_REG_IR_L,
		.modified = 1,
		.channel2 = IIO_MOD_LIGHT_IR,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.scan_index = VEML6031X00_SCAN_IR,
		.scan_type = {
			.format = IIO_SCAN_FORMAT_UNSIGNED_INT,
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_LE,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(VEML6031X00_SCAN_TIMESTAMP),
};

static const struct iio_chan_spec veml6031x00_channels_irq[] = {
	{
		.type = IIO_LIGHT,
		.address = VEML6031X00_REG_ALS_L,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_INT_TIME) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.info_mask_separate_available = BIT(IIO_CHAN_INFO_INT_TIME) |
						BIT(IIO_CHAN_INFO_SCALE),
		.event_spec = veml6031x00_event_spec,
		.num_event_specs = ARRAY_SIZE(veml6031x00_event_spec),
		.scan_index = VEML6031X00_SCAN_ALS,
		.scan_type = {
			.format = IIO_SCAN_FORMAT_UNSIGNED_INT,
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_LE,
		},
	},
	{
		.type = IIO_INTENSITY,
		.address = VEML6031X00_REG_IR_L,
		.modified = 1,
		.channel2 = IIO_MOD_LIGHT_IR,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.scan_index = VEML6031X00_SCAN_IR,
		.scan_type = {
			.format = IIO_SCAN_FORMAT_UNSIGNED_INT,
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_LE,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(VEML6031X00_SCAN_TIMESTAMP),
};

static const struct regmap_range veml6031x00_readable_ranges[] = {
	regmap_reg_range(VEML6031X00_REG_CONF0, VEML6031X00_REG_CONF1),
	regmap_reg_range(VEML6031X00_REG_WH_L, VEML6031X00_REG_WL_H),
	regmap_reg_range(VEML6031X00_REG_ALS_L, VEML6031X00_REG_ID_H),
	regmap_reg_range(VEML6031X00_REG_INT, VEML6031X00_REG_INT),
};

static const struct regmap_access_table veml6031x00_readable_table = {
	.yes_ranges = veml6031x00_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(veml6031x00_readable_ranges),
};

static const struct regmap_range veml6031x00_writable_ranges[] = {
	regmap_reg_range(VEML6031X00_REG_CONF0, VEML6031X00_REG_CONF1),
	regmap_reg_range(VEML6031X00_REG_WH_L, VEML6031X00_REG_WL_H),
};

static const struct regmap_access_table veml6031x00_writable_table = {
	.yes_ranges = veml6031x00_writable_ranges,
	.n_yes_ranges = ARRAY_SIZE(veml6031x00_writable_ranges),
};

static const struct regmap_range veml6031x00_volatile_ranges[] = {
	/* AF_TRIG in CONF0 is volatile */
	regmap_reg_range(VEML6031X00_REG_CONF0, VEML6031X00_REG_CONF0),
	regmap_reg_range(VEML6031X00_REG_ALS_L, VEML6031X00_REG_IR_H),
	regmap_reg_range(VEML6031X00_REG_INT, VEML6031X00_REG_INT),
};

static const struct regmap_access_table veml6031x00_volatile_table = {
	.yes_ranges = veml6031x00_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(veml6031x00_volatile_ranges),
};

static const struct regmap_range veml6031x00_precious_ranges[] = {
	regmap_reg_range(VEML6031X00_REG_INT, VEML6031X00_REG_INT),
};

static const struct regmap_access_table veml6031x00_precious_table = {
	.yes_ranges = veml6031x00_precious_ranges,
	.n_yes_ranges = ARRAY_SIZE(veml6031x00_precious_ranges),
};

static const struct regmap_config veml6031x00_regmap_config = {
	.name = "veml6031x00_regmap",
	.reg_bits = 8,
	.val_bits = 8,
	.rd_table = &veml6031x00_readable_table,
	.wr_table = &veml6031x00_writable_table,
	.volatile_table = &veml6031x00_volatile_table,
	.precious_table = &veml6031x00_precious_table,
	.max_register = VEML6031X00_REG_INT,
	.cache_type = REGCACHE_MAPLE,
};

static const struct reg_field veml6031x00_rf_int_en =
	REG_FIELD(VEML6031X00_REG_CONF0, 1, 1);

static const struct reg_field veml6031x00_rf_it =
	REG_FIELD(VEML6031X00_REG_CONF0, 4, 6);

static const struct reg_field veml6031x00_rf_pers =
	REG_FIELD(VEML6031X00_REG_CONF1, 1, 2);

static const struct reg_field veml6031x00_rf_gain =
	REG_FIELD(VEML6031X00_REG_CONF1, 3, 4);

static const struct reg_field veml6031x00_rf_pd_div4 =
	REG_FIELD(VEML6031X00_REG_CONF1, 6, 6);

static int veml6031x00_regfield_init(struct veml6031x00_data *data)
{
	struct regmap *map = data->regmap;
	struct device *dev = regmap_get_device(map);
	struct regmap_field *rm_field;
	struct veml6031x00_rf *rf = &data->rf;

	rm_field = devm_regmap_field_alloc(dev, map, veml6031x00_rf_gain);
	if (IS_ERR(rm_field))
		return PTR_ERR(rm_field);
	rf->gain = rm_field;

	rm_field = devm_regmap_field_alloc(dev, map, veml6031x00_rf_it);
	if (IS_ERR(rm_field))
		return PTR_ERR(rm_field);
	rf->it = rm_field;

	rm_field = devm_regmap_field_alloc(dev, map, veml6031x00_rf_int_en);
	if (IS_ERR(rm_field))
		return PTR_ERR(rm_field);
	rf->int_en = rm_field;

	rm_field = devm_regmap_field_alloc(dev, map, veml6031x00_rf_pd_div4);
	if (IS_ERR(rm_field))
		return PTR_ERR(rm_field);
	rf->pd_div4 = rm_field;

	rm_field = devm_regmap_field_alloc(dev, map, veml6031x00_rf_pers);
	if (IS_ERR(rm_field))
		return PTR_ERR(rm_field);
	rf->pers = rm_field;

	return 0;
}

static int __veml6031x00_get_it(struct veml6031x00_data *data, int *val2)
{
	unsigned int it_idx;
	int ret;

	ret = regmap_field_read(data->rf.it, &it_idx);
	if (ret)
		return ret;

	ret = iio_gts_find_int_time_by_sel(&data->gts, it_idx);
	if (ret < 0)
		return ret;

	*val2 = ret;

	return IIO_VAL_INT_PLUS_MICRO;
}

static int veml6031x00_get_it(struct veml6031x00_data *data, int *val2)
{
	guard(mutex)(&data->scale_lock);

	return __veml6031x00_get_it(data, val2);
}

static int veml6031x00_write_gain(struct veml6031x00_data *data, unsigned int gain)
{
	int ret;

	ret = regmap_field_write(data->rf.pd_div4, gain >> 2);
	if (ret)
		return ret;

	return regmap_field_write(data->rf.gain, gain & 0x03);
}

static int veml6031x00_set_it(struct iio_dev *iio, int val, int val2)
{
	struct veml6031x00_data *data = iio_priv(iio);
	struct device *dev = regmap_get_device(data->regmap);
	int ret, gain_sel, new_gain, prev_gain, prev_it;
	unsigned int gain_reg, it_idx, pd_div4;
	struct iio_gts *gts = &data->gts;
	bool gain_in_range;

	if (val || !iio_gts_valid_time(gts, val2))
		return -EINVAL;

	guard(mutex)(&data->scale_lock);

	ret = regmap_field_read(data->rf.it, &it_idx);
	if (ret)
		return ret;

	ret = regmap_field_read(data->rf.gain, &gain_reg);
	if (ret)
		return ret;

	ret = regmap_field_read(data->rf.pd_div4, &pd_div4);
	if (ret)
		return ret;

	prev_it = iio_gts_find_int_time_by_sel(gts, it_idx);
	if (prev_it < 0)
		return prev_it;

	if (prev_it == val2)
		return 0;

	gain_sel = VEML6031X00_GAIN_SEL(pd_div4, gain_reg);
	prev_gain = iio_gts_find_gain_by_sel(gts, gain_sel);
	if (prev_gain < 0)
		return prev_gain;

	ret = iio_gts_find_new_gain_by_gain_time_min(gts, prev_gain, prev_it, val2,
						     &new_gain, &gain_in_range);
	if (ret)
		return ret;

	if (!gain_in_range)
		dev_dbg(dev, "Optimal gain out of range\n");

	ret = iio_gts_find_sel_by_int_time(gts, val2);
	if (ret < 0)
		return ret;

	ret = regmap_field_write(data->rf.it, ret);
	if (ret)
		return ret;

	gain_sel = iio_gts_find_sel_by_gain(gts, new_gain);
	if (gain_sel < 0)
		return gain_sel;

	return veml6031x00_write_gain(data, gain_sel);
}

static int veml6031x00_read_period(struct iio_dev *iio, int *val)
{
	struct veml6031x00_data *data = iio_priv(iio);
	unsigned int regval;
	int ret;

	ret = regmap_field_read(data->rf.pers, &regval);
	if (ret)
		return ret;

	*val = BIT(regval);

	return IIO_VAL_INT;
}

static int veml6031x00_write_period(struct iio_dev *iio, int val)
{
	struct veml6031x00_data *data = iio_priv(iio);

	if (val < 0 || val > 8 || !is_power_of_2(val))
		return -EINVAL;

	return regmap_field_write(data->rf.pers, ilog2(val));
}

static int veml6031x00_set_scale(struct iio_dev *iio, int val, int val2)
{
	struct veml6031x00_data *data = iio_priv(iio);
	struct iio_gts *gts = &data->gts;
	int gain_sel, it_sel, ret;

	ret = iio_gts_find_gain_time_sel_for_scale(gts, val, val2, &gain_sel, &it_sel);
	if (ret)
		return ret;

	guard(mutex)(&data->scale_lock);

	ret = regmap_field_write(data->rf.it, it_sel);
	if (ret)
		return ret;

	return veml6031x00_write_gain(data, gain_sel);
}

static int veml6031x00_get_scale(struct veml6031x00_data *data, int *val, int *val2)
{
	unsigned int gain_reg, pd_div4, it_reg;
	int gain, it, ret, sel;

	scoped_guard(mutex, &data->scale_lock) {
		ret = regmap_field_read(data->rf.gain, &gain_reg);
		if (ret)
			return ret;

		ret = regmap_field_read(data->rf.pd_div4, &pd_div4);
		if (ret)
			return ret;

		sel = VEML6031X00_GAIN_SEL(pd_div4, gain_reg);
		gain = iio_gts_find_gain_by_sel(&data->gts, sel);
		if (gain < 0)
			return gain;

		ret = regmap_field_read(data->rf.it, &it_reg);
		if (ret)
			return ret;
	}

	it = iio_gts_find_int_time_by_sel(&data->gts, it_reg);
	if (it < 0)
		return it;

	ret = iio_gts_get_scale(&data->gts, gain, it, val, val2);
	if (ret)
		return ret;

	return IIO_VAL_INT_PLUS_NANO;
}

static int veml6031x00_read_th(struct iio_dev *iio, enum iio_event_direction dir,
			       int *val, int *val2)
{
	struct veml6031x00_data *data = iio_priv(iio);
	__le16 regval;
	int ret;

	if (dir == IIO_EV_DIR_RISING)
		ret = regmap_bulk_read(data->regmap, VEML6031X00_REG_WH_L,
				       &regval, sizeof(regval));
	else
		ret = regmap_bulk_read(data->regmap, VEML6031X00_REG_WL_L,
				       &regval, sizeof(regval));
	if (ret)
		return ret;

	*val = le16_to_cpu(regval);

	return IIO_VAL_INT;
}

static int veml6031x00_write_th(struct iio_dev *iio, enum iio_event_direction dir,
				int val, int val2)
{
	struct veml6031x00_data *data = iio_priv(iio);
	struct device *dev = regmap_get_device(data->regmap);
	__le16 regval;
	int ret;

	if (val < 0 || val > U16_MAX || val2)
		return -EINVAL;

	regval = cpu_to_le16(val);

	if (dir == IIO_EV_DIR_RISING) {
		ret = regmap_bulk_write(data->regmap, VEML6031X00_REG_WH_L,
					&regval, sizeof(regval));
		if (ret)
			dev_dbg(dev, "Failed to set high threshold %d\n", ret);
	} else {
		ret = regmap_bulk_write(data->regmap, VEML6031X00_REG_WL_L,
					&regval, sizeof(regval));
		if (ret)
			dev_dbg(dev, "Failed to set low threshold %d\n", ret);
	}

	return ret;
}

static int veml6031x00_single_read(struct iio_dev *iio, enum iio_chan_type type,
				   int *val)
{
	struct veml6031x00_data *data = iio_priv(iio);
	unsigned int addr;
	int it_usec, ret;
	__le16 regval;

	switch (type) {
	case IIO_LIGHT:
		addr = VEML6031X00_REG_ALS_L;
		break;
	case IIO_INTENSITY:
		addr = VEML6031X00_REG_IR_L;
		break;
	default:
		return -EINVAL;
	}

	IIO_DEV_ACQUIRE_DIRECT_MODE(iio, claim);
	if (IIO_DEV_ACQUIRE_FAILED(claim))
		return -EBUSY;

	guard(mutex)(&data->scale_lock);

	PM_RUNTIME_ACQUIRE_AUTOSUSPEND(regmap_get_device(data->regmap), pm);
	ret = PM_RUNTIME_ACQUIRE_ERR(&pm);
	if (ret)
		return ret;

	ret = __veml6031x00_get_it(data, &it_usec);
	if (ret < 0)
		return ret;

	/* integration time + 10% to ensure completion */
	fsleep(it_usec + (it_usec / 10));

	ret = regmap_bulk_read(data->regmap, addr, &regval, sizeof(regval));
	if (ret)
		return ret;

	*val = le16_to_cpu(regval);

	return IIO_VAL_INT;
}

static int veml6031x00_read_raw(struct iio_dev *iio,
				struct iio_chan_spec const *chan,
				int *val, int *val2, long mask)
{
	struct veml6031x00_data *data = iio_priv(iio);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		return veml6031x00_single_read(iio, chan->type, val);
	case IIO_CHAN_INFO_INT_TIME:
		*val = 0;
		return veml6031x00_get_it(data, val2);
	case IIO_CHAN_INFO_SCALE:
		return veml6031x00_get_scale(data, val, val2);
	default:
		return -EINVAL;
	}
}

static int veml6031x00_read_avail(struct iio_dev *iio,
				  struct iio_chan_spec const *chan,
				  const int **vals, int *type, int *length,
				  long mask)
{
	struct veml6031x00_data *data = iio_priv(iio);

	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		return iio_gts_avail_times(&data->gts, vals, type, length);
	case IIO_CHAN_INFO_SCALE:
		return iio_gts_all_avail_scales(&data->gts, vals, type, length);
	default:
		return -EINVAL;
	}
}

static int veml6031x00_write_raw(struct iio_dev *iio,
				 struct iio_chan_spec const *chan,
				 int val, int val2, long mask)
{
	IIO_DEV_ACQUIRE_DIRECT_MODE(iio, claim);
	if (IIO_DEV_ACQUIRE_FAILED(claim))
		return -EBUSY;

	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		return veml6031x00_set_it(iio, val, val2);
	case IIO_CHAN_INFO_SCALE:
		return veml6031x00_set_scale(iio, val, val2);
	default:
		return -EINVAL;
	}
}

static int veml6031x00_write_raw_get_fmt(struct iio_dev *indio_dev,
					 struct iio_chan_spec const *chan,
					 long mask)
{
	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		return IIO_VAL_INT_PLUS_NANO;
	case IIO_CHAN_INFO_INT_TIME:
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int veml6031x00_set_interrupt(struct veml6031x00_data *data, bool state)
	__must_hold(&data->irq_lock)
{
	int ret;

	lockdep_assert_held(&data->irq_lock);

	if (state) {
		data->interrupt_users++;
		if (data->interrupt_users > 1)
			return 0;
	} else {
		if (!data->interrupt_users)
			return 0;

		data->interrupt_users--;
		if (data->interrupt_users > 0)
			return 0;
	}

	ret = regmap_field_write(data->rf.int_en, state);
	if (ret) {
		if (state)
			data->interrupt_users--;
		else
			data->interrupt_users++;
	}

	return ret;
}

static int veml6031x00_read_event_val(struct iio_dev *iio,
				      const struct iio_chan_spec *chan,
				      enum iio_event_type type,
				      enum iio_event_direction dir,
				      enum iio_event_info info,
				      int *val, int *val2)
{
	switch (type) {
	case IIO_EV_TYPE_THRESH:
		if (dir == IIO_EV_DIR_EITHER && info == IIO_EV_INFO_PERIOD)
			return veml6031x00_read_period(iio, val);

		return veml6031x00_read_th(iio, dir, val, val2);
	default:
		return -EINVAL;
	}
}

static int veml6031x00_write_event_val(struct iio_dev *iio,
				       const struct iio_chan_spec *chan,
				       enum iio_event_type type,
				       enum iio_event_direction dir,
				       enum iio_event_info info,
				       int val, int val2)
{
	switch (info) {
	case IIO_EV_INFO_VALUE:
		return veml6031x00_write_th(iio, dir, val, val2);
	case IIO_EV_INFO_PERIOD:
		return veml6031x00_write_period(iio, val);
	default:
		return -EINVAL;
	}
}

static int veml6031x00_read_event_config(struct iio_dev *iio,
					 const struct iio_chan_spec *chan,
					 enum iio_event_type type,
					 enum iio_event_direction dir)
{
	struct veml6031x00_data *data = iio_priv(iio);

	guard(mutex)(&data->irq_lock);

	return data->ev_en;
}

static int veml6031x00_event_enable(struct veml6031x00_data *data)
{
	struct device *dev = regmap_get_device(data->regmap);
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return ret;

	ret = veml6031x00_set_interrupt(data, true);
	if (ret) {
		pm_runtime_put_autosuspend(dev);
		return ret;
	}

	data->ev_en = true;

	return 0;
}

static int veml6031x00_event_disable(struct veml6031x00_data *data)
{
	struct device *dev = regmap_get_device(data->regmap);
	int ret;

	ret = veml6031x00_set_interrupt(data, false);
	if (ret)
		return ret;

	data->ev_en = false;
	pm_runtime_put_autosuspend(dev);

	return 0;
}

static int veml6031x00_write_event_config(struct iio_dev *iio,
					  const struct iio_chan_spec *chan,
					  enum iio_event_type type,
					  enum iio_event_direction dir,
					  bool state)
{
	struct veml6031x00_data *data = iio_priv(iio);

	guard(mutex)(&data->irq_lock);

	/* avoid multiple increments/decrements from one source */
	if (state == data->ev_en)
		return 0;

	if (state)
		return veml6031x00_event_enable(data);

	return veml6031x00_event_disable(data);
}

static void veml6031x00_disable_event_action(void *arg)
{
	struct veml6031x00_data *data = arg;
	struct device *dev = regmap_get_device(data->regmap);
	int ret;

	guard(mutex)(&data->irq_lock);

	if (!data->ev_en)
		return;

	ret = veml6031x00_set_interrupt(data, false);
	if (ret)
		dev_err(dev, "Failed to disable events: %d\n", ret);
	else
		data->ev_en = false;

	pm_runtime_put_sync(dev);
}

static const struct iio_info veml6031x00_info = {
	.read_raw = veml6031x00_read_raw,
	.read_avail = veml6031x00_read_avail,
	.write_raw = veml6031x00_write_raw,
	.write_raw_get_fmt = veml6031x00_write_raw_get_fmt,
	.read_event_value = veml6031x00_read_event_val,
	.write_event_value = veml6031x00_write_event_val,
	.read_event_config = veml6031x00_read_event_config,
	.write_event_config = veml6031x00_write_event_config,
	.event_attrs = &veml6031x00_event_attr_group,
};

static const struct iio_info veml6031x00_info_no_irq = {
	.read_raw = veml6031x00_read_raw,
	.read_avail = veml6031x00_read_avail,
	.write_raw = veml6031x00_write_raw,
	.write_raw_get_fmt = veml6031x00_write_raw_get_fmt,
};

static irqreturn_t veml6031x00_irq(int irq, void *private)
{
	struct iio_dev *iio = private;
	struct veml6031x00_data *data = iio_priv(iio);
	struct device *dev = regmap_get_device(data->regmap);
	s64 timestamp;
	unsigned int regval;
	int ret;
	bool trigger_poll;

	ret = pm_runtime_get_if_active(dev);
	if (ret <= 0)
		return IRQ_NONE;

	scoped_guard(mutex, &data->irq_lock) {
		ret = regmap_read(data->regmap, VEML6031X00_REG_INT, &regval);
		if (ret) {
			dev_dbg(dev, "Failed to read interrupt register %d\n", ret);
			pm_runtime_put(dev);
			return IRQ_NONE;
		}

		if (!(regval & VEML6031X00_INT_MASK)) {
			pm_runtime_put(dev);
			return IRQ_NONE;
		}

		timestamp = iio_get_time_ns(iio);

		if (data->ev_en &&
		    (regval & (VEML6031X00_INT_TH_H | VEML6031X00_INT_TH_L))) {
			if (regval & VEML6031X00_INT_TH_H)
				iio_push_event(iio,
					       IIO_UNMOD_EVENT_CODE(IIO_LIGHT, 0,
								    IIO_EV_TYPE_THRESH,
								    IIO_EV_DIR_RISING),
					       timestamp);
			if (regval & VEML6031X00_INT_TH_L)
				iio_push_event(iio,
					       IIO_UNMOD_EVENT_CODE(IIO_LIGHT, 0,
								    IIO_EV_TYPE_THRESH,
								    IIO_EV_DIR_FALLING),
					       timestamp);
		}

		trigger_poll = data->trig_en && (regval & VEML6031X00_INT_DRDY);
	}

	/*
	 * iio_trigger_poll_nested() must be called with irq_lock released:
	 * it runs trig_handler() synchronously in this thread, which calls
	 * reenable() on completion, and that callback also takes irq_lock.
	 * iio_pollfunc_store_time() is not called for our own trigger, so the
	 * timestamp is stored here.
	 */
	if (trigger_poll) {
		iio->pollfunc->timestamp = timestamp;
		iio_trigger_poll_nested(data->trig);
	}

	pm_runtime_put(dev);

	return IRQ_HANDLED;
}

static int veml6031x00_buffer_preenable(struct iio_dev *iio)
{
	struct veml6031x00_data *data = iio_priv(iio);
	struct device *dev = regmap_get_device(data->regmap);
	int ret, it_usec;

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return ret;

	ret = veml6031x00_get_it(data, &it_usec);
	if (ret < 0) {
		pm_runtime_put_autosuspend(dev);
		return ret;
	}

	/*
	 * Wait one integration period + 10% margin so the first triggered
	 * read does not race with the sensor completing its first conversion
	 * after power-on.
	 */
	fsleep(it_usec + (it_usec / 10));

	return 0;
}

static int veml6031x00_buffer_postdisable(struct iio_dev *iio)
{
	struct veml6031x00_data *data = iio_priv(iio);

	pm_runtime_put_autosuspend(regmap_get_device(data->regmap));

	return 0;
}

static int __veml6031x00_set_trigger_state(struct veml6031x00_data *data,
					   bool state)
	__must_hold(&data->irq_lock)
{
	int ret;

	lockdep_assert_held(&data->irq_lock);

	ret = veml6031x00_set_interrupt(data, state);
	if (ret)
		return ret;

	/* The AF bit must be updated before updating AF_TRIG */
	ret = regmap_assign_bits(data->regmap, VEML6031X00_REG_CONF0,
				 VEML6031X00_CONF0_AF, state);
	if (ret)
		goto disable_trigger;

	ret = regmap_assign_bits(data->regmap, VEML6031X00_REG_CONF0,
				 VEML6031X00_CONF0_AF_TRIG, state);
	if (ret) {
		regmap_assign_bits(data->regmap, VEML6031X00_REG_CONF0,
				   VEML6031X00_CONF0_AF, false);
		goto disable_trigger;
	}

	data->trig_en = state;

	return 0;

disable_trigger:
	data->trig_en = false;
	if (state)
		veml6031x00_set_interrupt(data, false);
	return ret;
}

static int veml6031x00_set_trigger_state(struct iio_trigger *trig, bool state)
{
	struct iio_dev *iio = iio_trigger_get_drvdata(trig);
	struct veml6031x00_data *data = iio_priv(iio);

	guard(mutex)(&data->irq_lock);

	if (state == data->trig_en)
		return 0;

	return __veml6031x00_set_trigger_state(data, state);
}

static const struct iio_buffer_setup_ops veml6031x00_buffer_setup_ops = {
	.preenable = veml6031x00_buffer_preenable,
	.postdisable = veml6031x00_buffer_postdisable,
};

static void veml6031x00_trigger_reenable(struct iio_trigger *trig)
{
	struct iio_dev *iio = iio_trigger_get_drvdata(trig);
	struct veml6031x00_data *data = iio_priv(iio);
	int ret;

	guard(mutex)(&data->irq_lock);

	if (!data->trig_en)
		return;

	ret = regmap_assign_bits(data->regmap, VEML6031X00_REG_CONF0,
				 VEML6031X00_CONF0_AF_TRIG, true);
	if (ret)
		dev_err(regmap_get_device(data->regmap),
			"Failed to reenable trigger: %d\n", ret);
}

static const struct iio_trigger_ops veml6031x00_trigger_ops = {
	.validate_device = iio_trigger_validate_own_device,
	.set_trigger_state = veml6031x00_set_trigger_state,
	.reenable = veml6031x00_trigger_reenable,
};

static irqreturn_t veml6031x00_trig_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *iio = pf->indio_dev;
	struct veml6031x00_data *data = iio_priv(iio);
	struct regmap *map = data->regmap;
	IIO_DECLARE_BUFFER_WITH_TS(__le16, scan, 2) = { };
	unsigned int i;
	int ch, ret;

	if (test_bit(VEML6031X00_SCAN_ALS, iio->active_scan_mask) &&
	    test_bit(VEML6031X00_SCAN_IR, iio->active_scan_mask)) {
		ret = regmap_bulk_read(map, VEML6031X00_REG_ALS_L,
				       scan, 2 * sizeof(*scan));
		if (ret)
			goto done;
	} else {
		i = 0;
		iio_for_each_active_channel(iio, ch) {
			ret = regmap_bulk_read(map, iio->channels[ch].address,
					       &scan[i++], sizeof(*scan));
			if (ret)
				goto done;
		}
	}

	iio_push_to_buffers_with_ts(iio, scan, sizeof(scan), pf->timestamp);

done:
	iio_trigger_notify_done(iio->trig);

	return IRQ_HANDLED;
}

static int veml6031x00_validate_part_id(struct veml6031x00_data *data)
{
	struct regmap *map = data->regmap;
	struct device *dev = regmap_get_device(map);
	unsigned int part_id;
	int ret;
	__le16 regval;

	ret = regmap_bulk_read(map, VEML6031X00_REG_ID_L, &regval, sizeof(regval));
	if (ret)
		return dev_err_probe(dev, ret, "Failed to read ID\n");

	part_id = le16_to_cpu(regval);
	if (part_id != data->chip->part_id)
		dev_info(dev, "Unknown or unexpected ID %04x\n", part_id);

	return 0;
}

static int veml6031x00_hw_init(struct veml6031x00_data *data)
{
	struct regmap *map = data->regmap;
	struct device *dev = regmap_get_device(map);
	unsigned int val;
	__le16 regval;
	int ret;

	ret = regmap_assign_bits(data->regmap, VEML6031X00_REG_CONF0,
				 VEML6031X00_CONF0_AF | VEML6031X00_CONF0_AF_TRIG, 0);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to disable trigger\n");

	ret = regmap_set_bits(data->regmap, VEML6031X00_REG_CONF1,
			      VEML6031X00_CONF1_ALS_CAL);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable internal calibration\n");

	regval = 0;
	ret = regmap_bulk_write(map, VEML6031X00_REG_WL_L, &regval, sizeof(regval));
	if (ret)
		return dev_err_probe(dev, ret, "Failed to set low threshold\n");

	regval = cpu_to_le16(U16_MAX);
	ret = regmap_bulk_write(map, VEML6031X00_REG_WH_L, &regval, sizeof(regval));
	if (ret)
		return dev_err_probe(dev, ret, "Failed to set high threshold\n");

	ret = regmap_field_write(data->rf.int_en, 0);
	if (ret)
		return ret;

	ret = regmap_read(map, VEML6031X00_REG_INT, &val);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to clear interrupts\n");

	return 0;
}

static int veml6031x00_setup_trigger(struct iio_dev *iio)
{
	struct veml6031x00_data *data = iio_priv(iio);
	struct device *dev = regmap_get_device(data->regmap);

	data->trig = devm_iio_trigger_alloc(dev, "%s-drdy%d",
					    iio->name, iio_device_id(iio));
	if (!data->trig)
		return -ENOMEM;

	data->trig->ops = &veml6031x00_trigger_ops;
	iio_trigger_set_drvdata(data->trig, iio);

	return devm_iio_trigger_register(dev, data->trig);
}

static int veml6031x00_init_iiodev(struct i2c_client *i2c, struct iio_dev *iio)
{
	struct veml6031x00_data *data = iio_priv(iio);
	struct device *dev = regmap_get_device(data->regmap);
	int ret;

	iio->name = data->chip->name;
	iio->modes = INDIO_DIRECT_MODE;

	if (!i2c->irq) {
		iio->channels = veml6031x00_channels;
		iio->num_channels = ARRAY_SIZE(veml6031x00_channels);
		iio->info = &veml6031x00_info_no_irq;

		return 0;
	}

	iio->channels = veml6031x00_channels_irq;
	iio->num_channels = ARRAY_SIZE(veml6031x00_channels_irq);
	iio->info = &veml6031x00_info;

	ret = veml6031x00_setup_trigger(iio);
	if (ret)
		return ret;

	return devm_add_action_or_reset(dev, veml6031x00_disable_event_action, data);
}

static int veml6031x00_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	struct veml6031x00_data *data;
	struct iio_dev *iio;
	int ret;

	iio = devm_iio_device_alloc(dev, sizeof(*data));
	if (!iio)
		return -ENOMEM;

	data = iio_priv(iio);
	i2c_set_clientdata(i2c, iio);

	data->chip = i2c_get_match_data(i2c);
	if (!data->chip)
		return dev_err_probe(dev, -ENODATA, "Failed to get chip data\n");

	ret = devm_mutex_init(dev, &data->scale_lock);
	if (ret)
		return ret;

	ret = devm_mutex_init(dev, &data->irq_lock);
	if (ret)
		return ret;

	data->regmap = devm_regmap_init_i2c(i2c, &veml6031x00_regmap_config);
	if (IS_ERR(data->regmap))
		return dev_err_probe(dev, PTR_ERR(data->regmap),
				     "Failed to set regmap\n");

	ret = veml6031x00_regfield_init(data);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to init regfield\n");

	ret = devm_regulator_get_enable(dev, "vdd");
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable regulator\n");

	/* The device starts in power down mode by default */
	ret = veml6031x00_set_power(data, true);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to power on the device\n");

	ret = devm_add_action_or_reset(dev, veml6031x00_als_shutdown_action, data);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to add shutdown action\n");

	pm_runtime_set_autosuspend_delay(dev, 2000);
	pm_runtime_use_autosuspend(dev);
	ret = devm_pm_runtime_set_active_enabled(dev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to enable runtime PM\n");

	ret = veml6031x00_validate_part_id(data);
	if (ret)
		return ret;

	ret = veml6031x00_hw_init(data);
	if (ret)
		return ret;

	/* Max resolution = 6.9632 lx/cnt for gain = 0.125 and IT = 3.125ms */
	ret = devm_iio_init_iio_gts(dev, 6, 963200000,
				    veml6031x00_gain_sel,
				    ARRAY_SIZE(veml6031x00_gain_sel),
				    veml6031x00_it_sel,
				    ARRAY_SIZE(veml6031x00_it_sel),
				    &data->gts);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to init IIO GTS\n");

	ret = veml6031x00_init_iiodev(i2c, iio);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to initialize IIO device\n");

	ret = devm_iio_triggered_buffer_setup(dev, iio,
					      iio_pollfunc_store_time,
					      veml6031x00_trig_handler,
					      &veml6031x00_buffer_setup_ops);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register triggered buffer\n");

	if (i2c->irq) {
		ret = devm_request_threaded_irq(dev, i2c->irq, NULL, veml6031x00_irq,
						IRQF_ONESHOT, iio->name, iio);
		if (ret)
			return dev_err_probe(dev, ret, "Failed to request IRQ\n");
	}

	ret = devm_iio_device_register(dev, iio);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register IIO device\n");

	return 0;
}

static int veml6031x00_runtime_suspend(struct device *dev)
{
	struct veml6031x00_data *data = iio_priv(dev_get_drvdata(dev));

	return veml6031x00_set_power(data, false);
}

static int veml6031x00_runtime_resume(struct device *dev)
{
	struct veml6031x00_data *data = iio_priv(dev_get_drvdata(dev));

	return veml6031x00_set_power(data, true);
}

static DEFINE_RUNTIME_DEV_PM_OPS(veml6031x00_pm_ops,
				 veml6031x00_runtime_suspend,
				 veml6031x00_runtime_resume,
				 NULL);

static const struct veml6031x00_chip veml6031x00_chip = {
	.name = "veml6031x00",
	.part_id = 0x0001,
};

static const struct veml6031x00_chip veml6031x01_chip = {
	.name = "veml6031x01",
	.part_id = 0x0001,
};

static const struct veml6031x00_chip veml60311x00_chip = {
	.name = "veml60311x00",
	.part_id = 0x1001,
};

static const struct veml6031x00_chip veml60311x01_chip = {
	.name = "veml60311x01",
	.part_id = 0x1001,
};

static const struct of_device_id veml6031x00_of_match[] = {
	{
		.compatible = "vishay,veml6031x00",
		.data = &veml6031x00_chip,
	},
	{
		.compatible = "vishay,veml6031x01",
		.data = &veml6031x01_chip,
	},
	{
		.compatible = "vishay,veml60311x00",
		.data = &veml60311x00_chip,
	},
	{
		.compatible = "vishay,veml60311x01",
		.data = &veml60311x01_chip,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, veml6031x00_of_match);

static const struct i2c_device_id veml6031x00_id[] = {
	{
		.name = "veml6031x00",
		.driver_data = (kernel_ulong_t)&veml6031x00_chip,
	},
	{
		.name = "veml6031x01",
		.driver_data = (kernel_ulong_t)&veml6031x01_chip,
	},
	{
		.name = "veml60311x00",
		.driver_data = (kernel_ulong_t)&veml60311x00_chip,
	},
	{
		.name = "veml60311x01",
		.driver_data = (kernel_ulong_t)&veml60311x01_chip,
	},
	{ }
};
MODULE_DEVICE_TABLE(i2c, veml6031x00_id);

static struct i2c_driver veml6031x00_driver = {
	.driver = {
		.name = "veml6031x00",
		.of_match_table = veml6031x00_of_match,
		.pm = pm_ptr(&veml6031x00_pm_ops),
	},
	.probe = veml6031x00_probe,
	.id_table = veml6031x00_id,
};
module_i2c_driver(veml6031x00_driver);

MODULE_AUTHOR("Javier Carrasco <javier.carrasco.cruz@gmail.com>");
MODULE_DESCRIPTION("VEML6031X00 Ambient Light Sensor");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_GTS_HELPER");
