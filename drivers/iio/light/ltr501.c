// SPDX-License-Identifier: GPL-2.0-only
/*
 * Support for Lite-On LTR501 and similar ambient light and proximity sensors.
 *
 * Copyright 2014 Peter Meerwald <pmeerw@pmeerw.net>
 *
 * 7-bit I2C slave address 0x23
 *
 * TODO: IR LED characteristics
 */

#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <linux/iio/iio.h>
#include <linux/iio/events.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/buffer.h>
#include <linux/iio/triggered_buffer.h>

#define LTR501_DRV_NAME "ltr501"

#define LTR501_ALS_CONTR 0x80 /* ALS operation mode, SW reset */
#define LTR501_PS_CONTR 0x81 /* PS operation mode */
#define LTR501_PS_MEAS_RATE 0x84 /* measurement rate*/
#define LTR501_ALS_MEAS_RATE 0x85 /* ALS integ time, measurement rate*/
#define LTR501_PART_ID 0x86
#define LTR501_MANUFAC_ID 0x87
#define LTR501_ALS_DATA1 0x88 /* 16-bit, little endian */
#define LTR501_ALS_DATA1_UPPER 0x89 /* upper 8 bits of LTR501_ALS_DATA1 */
#define LTR501_ALS_DATA0 0x8a /* 16-bit, little endian */
#define LTR501_ALS_DATA0_UPPER 0x8b /* upper 8 bits of LTR501_ALS_DATA0 */
#define LTR501_ALS_PS_STATUS 0x8c
#define LTR501_PS_DATA 0x8d /* 16-bit, little endian */
#define LTR501_PS_DATA_UPPER 0x8e /* upper 8 bits of LTR501_PS_DATA */
#define LTR501_INTR 0x8f /* output mode, polarity, mode */
#define LTR501_PS_THRESH_UP 0x90 /* 11 bit, ps upper threshold */
#define LTR501_PS_THRESH_LOW 0x92 /* 11 bit, ps lower threshold */
#define LTR501_ALS_THRESH_UP 0x97 /* 16 bit, ALS upper threshold */
#define LTR501_ALS_THRESH_LOW 0x99 /* 16 bit, ALS lower threshold */
#define LTR501_INTR_PRST 0x9e /* ps thresh, als thresh */
#define LTR501_MAX_REG 0x9f

#define LTR501_ALS_CONTR_SW_RESET BIT(2)
#define LTR501_CONTR_PS_GAIN_MASK (BIT(3) | BIT(2))
#define LTR501_CONTR_PS_GAIN_SHIFT 2
#define LTR501_CONTR_ALS_GAIN_MASK BIT(3)
#define LTR501_CONTR_ACTIVE BIT(1)

#define LTR501_STATUS_ALS_INTR BIT(3)
#define LTR501_STATUS_ALS_RDY BIT(2)
#define LTR501_STATUS_PS_INTR BIT(1)
#define LTR501_STATUS_PS_RDY BIT(0)

#define LTR501_PS_DATA_MASK 0x7ff
#define LTR501_PS_THRESH_MASK 0x7ff
#define LTR501_ALS_THRESH_MASK 0xffff

#define LTR501_ALS_DEF_PERIOD 500000
#define LTR501_PS_DEF_PERIOD 100000

#define LTR501_REGMAP_NAME "ltr501_regmap"

#define LTR501_LUX_CONV(vis_coeff, vis_data, ir_coeff, ir_data) \
			((vis_coeff * vis_data) - (ir_coeff * ir_data))

static const int int_time_mapping[] = {100000, 50000, 200000, 400000};

static const struct reg_field reg_field_it =
				REG_FIELD(LTR501_ALS_MEAS_RATE, 3, 4);
static const struct reg_field reg_field_als_intr =
				REG_FIELD(LTR501_INTR, 1, 1);
static const struct reg_field reg_field_ps_intr =
				REG_FIELD(LTR501_INTR, 0, 0);
static const struct reg_field reg_field_als_rate =
				REG_FIELD(LTR501_ALS_MEAS_RATE, 0, 2);
static const struct reg_field reg_field_ps_rate =
				REG_FIELD(LTR501_PS_MEAS_RATE, 0, 3);
static const struct reg_field reg_field_als_prst =
				REG_FIELD(LTR501_INTR_PRST, 0, 3);
static const struct reg_field reg_field_ps_prst =
				REG_FIELD(LTR501_INTR_PRST, 4, 7);

struct ltr501_samp_table {
	int freq_val;  /* repetition frequency in micro HZ*/
	int time_val; /* repetition rate in micro seconds */
};

#define LTR501_RESERVED_GAIN -1

enum {
	ltr501 = 0,
	ltr559,
	ltr301,
	ltr303,
};

struct ltr501_gain {
	int scale;
	int uscale;
};

static const struct ltr501_gain ltr501_als_gain_tbl[] = {
	{1, 0},
	{0, 5000},
};

static const struct ltr501_gain ltr559_als_gain_tbl[] = {
	{1, 0},
	{0, 500000},
	{0, 250000},
	{0, 125000},
	{LTR501_RESERVED_GAIN, LTR501_RESERVED_GAIN},
	{LTR501_RESERVED_GAIN, LTR501_RESERVED_GAIN},
	{0, 20000},
	{0, 10000},
};

static const struct ltr501_gain ltr501_ps_gain_tbl[] = {
	{1, 0},
	{0, 250000},
	{0, 125000},
	{0, 62500},
};

static const struct ltr501_gain ltr559_ps_gain_tbl[] = {
	{0, 62500}, /* x16 gain */
	{0, 31250}, /* x32 gain */
	{0, 15625}, /* bits X1 are for x64 gain */
	{0, 15624},
};

struct ltr501_chip_info {
	u8 partid;
	const struct ltr501_gain *als_gain;
	int als_gain_tbl_size;
	const struct ltr501_gain *ps_gain;
	int ps_gain_tbl_size;
	u8 als_mode_active;
	u8 als_gain_mask;
	u8 als_gain_shift;
	struct iio_chan_spec const *channels;
	const int no_channels;
	const struct iio_info *info;
	const struct iio_info *info_no_irq;
};

struct ltr501_data {
	struct i2c_client *client;
	struct mutex lock_als, lock_ps;
	const struct ltr501_chip_info *chip_info;
	u8 als_contr, ps_contr;
	int als_period, ps_period; /* period in micro seconds */
	struct regmap *regmap;
	struct regmap_field *reg_it;
	struct regmap_field *reg_als_intr;
	struct regmap_field *reg_ps_intr;
	struct regmap_field *reg_als_rate;
	struct regmap_field *reg_ps_rate;
	struct regmap_field *reg_als_prst;
	struct regmap_field *reg_ps_prst;
	uint32_t near_level;
};

static const struct ltr501_samp_table ltr501_als_samp_table[] = {
			{20000000, 50000}, {10000000, 100000},
			{5000000, 200000}, {2000000, 500000},
			{1000000, 1000000}, {500000, 2000000},
			{500000, 2000000}, {500000, 2000000}
};

static const struct ltr501_samp_table ltr501_ps_samp_table[] = {
			{20000000, 50000}, {14285714, 70000},
			{10000000, 100000}, {5000000, 200000},
			{2000000, 500000}, {1000000, 1000000},
			{500000, 2000000}, {500000, 2000000},
			{500000, 2000000}
};

static int ltr501_match_samp_freq(const struct ltr501_samp_table *tab,
					   int len, int val, int val2)
{
	int i, freq;

	freq = val * 1000000 + val2;

	for (i = 0; i < len; i++) {
		if (tab[i].freq_val == freq)
			return i;
	}

	return -EINVAL;
}

static int ltr501_als_read_samp_freq(const struct ltr501_data *data,
				     int *val, int *val2)
{
	int ret, i;

	ret = regmap_field_read(data->reg_als_rate, &i);
	if (ret < 0)
		return ret;

	if (i < 0 || i >= ARRAY_SIZE(ltr501_als_samp_table))
		return -EINVAL;

	*val = ltr501_als_samp_table[i].freq_val / 1000000;
	*val2 = ltr501_als_samp_table[i].freq_val % 1000000;

	return IIO_VAL_INT_PLUS_MICRO;
}

static int ltr501_ps_read_samp_freq(const struct ltr501_data *data,
				    int *val, int *val2)
{
	int ret, i;

	ret = regmap_field_read(data->reg_ps_rate, &i);
	if (ret < 0)
		return ret;

	if (i < 0 || i >= ARRAY_SIZE(ltr501_ps_samp_table))
		return -EINVAL;

	*val = ltr501_ps_samp_table[i].freq_val / 1000000;
	*val2 = ltr501_ps_samp_table[i].freq_val % 1000000;

	return IIO_VAL_INT_PLUS_MICRO;
}

static int ltr501_als_write_samp_freq(struct ltr501_data *data,
				      int val, int val2)
{
	int i, ret;

	i = ltr501_match_samp_freq(ltr501_als_samp_table,
				   ARRAY_SIZE(ltr501_als_samp_table),
				   val, val2);

	if (i < 0)
		return i;

	mutex_lock(&data->lock_als);
	ret = regmap_field_write(data->reg_als_rate, i);
	mutex_unlock(&data->lock_als);

	return ret;
}

static int ltr501_ps_write_samp_freq(struct ltr501_data *data,
				     int val, int val2)
{
	int i, ret;

	i = ltr501_match_samp_freq(ltr501_ps_samp_table,
				   ARRAY_SIZE(ltr501_ps_samp_table),
				   val, val2);

	if (i < 0)
		return i;

	mutex_lock(&data->lock_ps);
	ret = regmap_field_write(data->reg_ps_rate, i);
	mutex_unlock(&data->lock_ps);

	return ret;
}

static int ltr501_als_read_samp_period(const struct ltr501_data *data, int *val)
{
	int ret, i;

	ret = regmap_field_read(data->reg_als_rate, &i);
	if (ret < 0)
		return ret;

	if (i < 0 || i >= ARRAY_SIZE(ltr501_als_samp_table))
		return -EINVAL;

	*val = ltr501_als_samp_table[i].time_val;

	return IIO_VAL_INT;
}

static int ltr501_ps_read_samp_period(const struct ltr501_data *data, int *val)
{
	int ret, i;

	ret = regmap_field_read(data->reg_ps_rate, &i);
	if (ret < 0)
		return ret;

	if (i < 0 || i >= ARRAY_SIZE(ltr501_ps_samp_table))
		return -EINVAL;

	*val = ltr501_ps_samp_table[i].time_val;

	return IIO_VAL_INT;
}

/* IR and visible spectrum coeff's are given in data sheet */
static unsigned long ltr501_calculate_lux(u16 vis_data, u16 ir_data)
{
	unsigned long ratio, lux;

	if (vis_data == 0)
		return 0;

	/* multiply numerator by 100 to avoid handling ratio < 1 */
	ratio = DIV_ROUND_UP(ir_data * 100, ir_data + vis_data);

	if (ratio < 45)
		lux = LTR501_LUX_CONV(1774, vis_data, -1105, ir_data);
	else if (ratio >= 45 && ratio < 64)
		lux = LTR501_LUX_CONV(3772, vis_data, 1336, ir_data);
	else if (ratio >= 64 && ratio < 85)
		lux = LTR501_LUX_CONV(1690, vis_data, 169, ir_data);
	else
		lux = 0;

	return lux / 1000;
}

static int ltr501_drdy(const struct ltr501_data *data, u8 drdy_mask)
{
	int tries = 100;
	int ret, status;

	while (tries--) {
		ret = regmap_read(data->regmap, LTR501_ALS_PS_STATUS, &status);
		if (ret < 0)
			return ret;
		if ((status & drdy_mask) == drdy_mask)
			return 0;
		msleep(25);
	}

	dev_err(&data->client->dev, "ltr501_drdy() failed, data not ready\n");
	return -EIO;
}

static int ltr501_set_it_time(struct ltr501_data *data, int it)
{
	int ret, i, index = -1, status;

	for (i = 0; i < ARRAY_SIZE(int_time_mapping); i++) {
		if (int_time_mapping[i] == it) {
			index = i;
			break;
		}
	}
	/* Make sure integ time index is valid */
	if (index < 0)
		return -EINVAL;

	ret = regmap_read(data->regmap, LTR501_ALS_CONTR, &status);
	if (ret < 0)
		return ret;

	if (status & LTR501_CONTR_ALS_GAIN_MASK) {
		/*
		 * 200 ms and 400 ms integ time can only be
		 * used in dynamic range 1
		 */
		if (index > 1)
			return -EINVAL;
	} else
		/* 50 ms integ time can only be used in dynamic range 2 */
		if (index == 1)
			return -EINVAL;

	return regmap_field_write(data->reg_it, index);
}

/* read int time in micro seconds */
static int ltr501_read_it_time(const struct ltr501_data *data,
			       int *val, int *val2)
{
	int ret, index;

	ret = regmap_field_read(data->reg_it, &index);
	if (ret < 0)
		return ret;

	/* Make sure integ time index is valid */
	if (index < 0 || index >= ARRAY_SIZE(int_time_mapping))
		return -EINVAL;

	*val2 = int_time_mapping[index];
	*val = 0;

	return IIO_VAL_INT_PLUS_MICRO;
}

static int ltr501_read_als(const struct ltr501_data *data, __le16 buf[2])
{
	int ret;

	ret = ltr501_drdy(data, LTR501_STATUS_ALS_RDY);
	if (ret < 0)
		return ret;
	/* always read both ALS channels in given order */
	return regmap_bulk_read(data->regmap, LTR501_ALS_DATA1,
				buf, 2 * sizeof(__le16));
}

static int ltr501_read_ps(const struct ltr501_data *data)
{
	__le16 status;
	int ret;

	ret = ltr501_drdy(data, LTR501_STATUS_PS_RDY);
	if (ret < 0)
		return ret;

	ret = regmap_bulk_read(data->regmap, LTR501_PS_DATA,
			       &status, sizeof(status));
	if (ret < 0)
		return ret;

	return le16_to_cpu(status);
}

static int ltr501_read_intr_prst(const struct ltr501_data *data,
				 enum iio_chan_type type,
				 int *val2)
{
	int ret, samp_period, prst;

	switch (type) {
	case IIO_INTENSITY:
		ret = regmap_field_read(data->reg_als_prst, &prst);
		if (ret < 0)
			return ret;

		ret = ltr501_als_read_samp_period(data, &samp_period);

		if (ret < 0)
			return ret;
		*val2 = samp_period * prst;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_PROXIMITY:
		ret = regmap_field_read(data->reg_ps_prst, &prst);
		if (ret < 0)
			return ret;

		ret = ltr501_ps_read_samp_period(data, &samp_period);

		if (ret < 0)
			return ret;

		*val2 = samp_period * prst;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}

	return -EINVAL;
}

static int ltr501_write_intr_prst(struct ltr501_data *data,
				  enum iio_chan_type type,
				  int val, int val2)
{
	int ret, samp_period, new_val;
	unsigned long period;

	if (val < 0 || val2 < 0)
		return -EINVAL;

	/* period in microseconds */
	period = ((val * 1000000) + val2);

	switch (type) {
	case IIO_INTENSITY:
		ret = ltr501_als_read_samp_period(data, &samp_period);
		if (ret < 0)
			return ret;

		/* period should be atleast equal to sampling period */
		if (period < samp_period)
			return -EINVAL;

		new_val = DIV_ROUND_UP(period, samp_period);
		if (new_val < 0 || new_val > 0x0f)
			return -EINVAL;

		mutex_lock(&data->lock_als);
		ret = regmap_field_write(data->reg_als_prst, new_val);
		mutex_unlock(&data->lock_als);
		if (ret >= 0)
			data->als_period = period;

		return ret;
	case IIO_PROXIMITY:
		ret = ltr501_ps_read_samp_period(data, &samp_period);
		if (ret < 0)
			return ret;

		/* period should be atleast equal to rate */
		if (period < samp_period)
			return -EINVAL;

		new_val = DIV_ROUND_UP(period, samp_period);
		if (new_val < 0 || new_val > 0x0f)
			return -EINVAL;

		mutex_lock(&data->lock_ps);
		ret = regmap_field_write(data->reg_ps_prst, new_val);
		mutex_unlock(&data->lock_ps);
		if (ret >= 0)
			data->ps_period = period;

		return ret;
	default:
		return -EINVAL;
	}

	return -EINVAL;
}

static ssize_t ltr501_read_near_level(struct iio_dev *indio_dev,
				      uintptr_t priv,
				      const struct iio_chan_spec *chan,
				      char *buf)
{
	struct ltr501_data *data = iio_priv(indio_dev);

	return sprintf(buf, "%u\n", data->near_level);
}

static const struct iio_chan_spec_ext_info ltr501_ext_info[] = {
	{
		.name = "nearlevel",
		.shared = IIO_SEPARATE,
		.read = ltr501_read_near_level,
	},
	{ }
};

static const struct iio_event_spec ltr501_als_event_spec[] = {
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
		.mask_separate = BIT(IIO_EV_INFO_ENABLE) |
				 BIT(IIO_EV_INFO_PERIOD),
	},

};

static const struct iio_event_spec ltr501_pxs_event_spec[] = {
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
		.mask_separate = BIT(IIO_EV_INFO_ENABLE) |
				 BIT(IIO_EV_INFO_PERIOD),
	},
};

#define LTR501_INTENSITY_CHANNEL(_idx, _addr, _mod, _shared, \
				 _evspec, _evsize) { \
	.type = IIO_INTENSITY, \
	.modified = 1, \
	.address = (_addr), \
	.channel2 = (_mod), \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = (_shared), \
	.scan_index = (_idx), \
	.scan_type = { \
		.sign = 'u', \
		.realbits = 16, \
		.storagebits = 16, \
		.endianness = IIO_CPU, \
	}, \
	.event_spec = _evspec,\
	.num_event_specs = _evsize,\
}

#define LTR501_LIGHT_CHANNEL() { \
	.type = IIO_LIGHT, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED), \
	.scan_index = -1, \
}

static const struct iio_chan_spec ltr501_channels[] = {
	LTR501_LIGHT_CHANNEL(),
	LTR501_INTENSITY_CHANNEL(0, LTR501_ALS_DATA0, IIO_MOD_LIGHT_BOTH, 0,
				 ltr501_als_event_spec,
				 ARRAY_SIZE(ltr501_als_event_spec)),
	LTR501_INTENSITY_CHANNEL(1, LTR501_ALS_DATA1, IIO_MOD_LIGHT_IR,
				 BIT(IIO_CHAN_INFO_SCALE) |
				 BIT(IIO_CHAN_INFO_INT_TIME) |
				 BIT(IIO_CHAN_INFO_SAMP_FREQ),
				 NULL, 0),
	{
		.type = IIO_PROXIMITY,
		.address = LTR501_PS_DATA,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
			BIT(IIO_CHAN_INFO_SCALE),
		.scan_index = 2,
		.scan_type = {
			.sign = 'u',
			.realbits = 11,
			.storagebits = 16,
			.endianness = IIO_CPU,
		},
		.event_spec = ltr501_pxs_event_spec,
		.num_event_specs = ARRAY_SIZE(ltr501_pxs_event_spec),
		.ext_info = ltr501_ext_info,
	},
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static const struct iio_chan_spec ltr301_channels[] = {
	LTR501_LIGHT_CHANNEL(),
	LTR501_INTENSITY_CHANNEL(0, LTR501_ALS_DATA0, IIO_MOD_LIGHT_BOTH, 0,
				 ltr501_als_event_spec,
				 ARRAY_SIZE(ltr501_als_event_spec)),
	LTR501_INTENSITY_CHANNEL(1, LTR501_ALS_DATA1, IIO_MOD_LIGHT_IR,
				 BIT(IIO_CHAN_INFO_SCALE) |
				 BIT(IIO_CHAN_INFO_INT_TIME) |
				 BIT(IIO_CHAN_INFO_SAMP_FREQ),
				 NULL, 0),
	IIO_CHAN_SOFT_TIMESTAMP(2),
};

static int ltr501_read_info_raw(struct ltr501_data *data,
				struct iio_chan_spec const *chan,
				int *val)
{
	__le16 buf[2];
	int ret;

	switch (chan->type) {
	case IIO_INTENSITY:
		mutex_lock(&data->lock_als);
		ret = ltr501_read_als(data, buf);
		mutex_unlock(&data->lock_als);
		if (ret < 0)
			return ret;
		*val = le16_to_cpu(chan->address == LTR501_ALS_DATA1 ?
				   buf[0] : buf[1]);
		return IIO_VAL_INT;
	case IIO_PROXIMITY:
		mutex_lock(&data->lock_ps);
		ret = ltr501_read_ps(data);
		mutex_unlock(&data->lock_ps);
		if (ret < 0)
			return ret;
		*val = ret & LTR501_PS_DATA_MASK;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static int ltr501_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct ltr501_data *data = iio_priv(indio_dev);
	__le16 buf[2];
	int ret, i;

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		switch (chan->type) {
		case IIO_LIGHT:
			if (!iio_device_claim_direct(indio_dev))
				return -EBUSY;

			mutex_lock(&data->lock_als);
			ret = ltr501_read_als(data, buf);
			mutex_unlock(&data->lock_als);
			iio_device_release_direct(indio_dev);
			if (ret < 0)
				return ret;
			*val = ltr501_calculate_lux(le16_to_cpu(buf[1]),
						    le16_to_cpu(buf[0]));
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_RAW:
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;

		ret = ltr501_read_info_raw(data, chan, val);

		iio_device_release_direct(indio_dev);
		return ret;

	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_INTENSITY:
			i = (data->als_contr & data->chip_info->als_gain_mask)
			     >> data->chip_info->als_gain_shift;
			*val = data->chip_info->als_gain[i].scale;
			*val2 = data->chip_info->als_gain[i].uscale;
			return IIO_VAL_INT_PLUS_MICRO;
		case IIO_PROXIMITY:
			i = (data->ps_contr & LTR501_CONTR_PS_GAIN_MASK) >>
				LTR501_CONTR_PS_GAIN_SHIFT;
			*val = data->chip_info->ps_gain[i].scale;
			*val2 = data->chip_info->ps_gain[i].uscale;
			return IIO_VAL_INT_PLUS_MICRO;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_INT_TIME:
		switch (chan->type) {
		case IIO_INTENSITY:
			return ltr501_read_it_time(data, val, val2);
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SAMP_FREQ:
		switch (chan->type) {
		case IIO_INTENSITY:
			return ltr501_als_read_samp_freq(data, val, val2);
		case IIO_PROXIMITY:
			return ltr501_ps_read_samp_freq(data, val, val2);
		default:
			return -EINVAL;
		}
	}
	return -EINVAL;
}

static int ltr501_get_gain_index(const struct ltr501_gain *gain, int size,
				 int val, int val2)
{
	int i;

	for (i = 0; i < size; i++)
		if (val == gain[i].scale && val2 == gain[i].uscale)
			return i;

	return -1;
}

static int __ltr501_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int val, int val2, long mask)
{
	struct ltr501_data *data = iio_priv(indio_dev);
	int i, ret, freq_val, freq_val2;
	const struct ltr501_chip_info *info = data->chip_info;

	switch (mask) {
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_INTENSITY:
			i = ltr501_get_gain_index(info->als_gain,
						  info->als_gain_tbl_size,
						  val, val2);
			if (i < 0)
				return -EINVAL;

			data->als_contr &= ~info->als_gain_mask;
			data->als_contr |= i << info->als_gain_shift;

			return regmap_write(data->regmap, LTR501_ALS_CONTR,
					    data->als_contr);
		case IIO_PROXIMITY:
			i = ltr501_get_gain_index(info->ps_gain,
						  info->ps_gain_tbl_size,
						  val, val2);
			if (i < 0)
				return -EINVAL;

			data->ps_contr &= ~LTR501_CONTR_PS_GAIN_MASK;
			data->ps_contr |= i << LTR501_CONTR_PS_GAIN_SHIFT;

			return regmap_write(data->regmap, LTR501_PS_CONTR,
					    data->ps_contr);
		default:
			return -EINVAL;
		}

	case IIO_CHAN_INFO_INT_TIME:
		switch (chan->type) {
		case IIO_INTENSITY:
			if (val != 0)
				return -EINVAL;

			mutex_lock(&data->lock_als);
			ret = ltr501_set_it_time(data, val2);
			mutex_unlock(&data->lock_als);
			return ret;
		default:
			return -EINVAL;
		}

	case IIO_CHAN_INFO_SAMP_FREQ:
		switch (chan->type) {
		case IIO_INTENSITY:
			ret = ltr501_als_read_samp_freq(data, &freq_val,
							&freq_val2);
			if (ret < 0)
				return ret;

			ret = ltr501_als_write_samp_freq(data, val, val2);
			if (ret < 0)
				return ret;

			/* update persistence count when changing frequency */
			ret = ltr501_write_intr_prst(data, chan->type,
						     0, data->als_period);

			if (ret < 0)
				/* Do not ovewrite error */
				ltr501_als_write_samp_freq(data, freq_val,
							   freq_val2);
			return ret;
		case IIO_PROXIMITY:
			ret = ltr501_ps_read_samp_freq(data, &freq_val,
						       &freq_val2);
			if (ret < 0)
				return ret;

			ret = ltr501_ps_write_samp_freq(data, val, val2);
			if (ret < 0)
				return ret;

			/* update persistence count when changing frequency */
			ret = ltr501_write_intr_prst(data, chan->type,
						     0, data->ps_period);

			if (ret < 0)
				/* Do not overwrite error */
				ltr501_ps_write_samp_freq(data, freq_val,
							  freq_val2);
			return ret;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int ltr501_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	int ret;

	if (!iio_device_claim_direct(indio_dev))
		return -EBUSY;

	ret = __ltr501_write_raw(indio_dev, chan, val, val2, mask);

	iio_device_release_direct(indio_dev);

	return ret;
}

static int ltr501_read_thresh(const struct iio_dev *indio_dev,
			      const struct iio_chan_spec *chan,
			      enum iio_event_type type,
			      enum iio_event_direction dir,
			      enum iio_event_info info,
			      int *val, int *val2)
{
	const struct ltr501_data *data = iio_priv(indio_dev);
	int ret, thresh_data;

	switch (chan->type) {
	case IIO_INTENSITY:
		switch (dir) {
		case IIO_EV_DIR_RISING:
			ret = regmap_bulk_read(data->regmap,
					       LTR501_ALS_THRESH_UP,
					       &thresh_data, 2);
			if (ret < 0)
				return ret;
			*val = thresh_data & LTR501_ALS_THRESH_MASK;
			return IIO_VAL_INT;
		case IIO_EV_DIR_FALLING:
			ret = regmap_bulk_read(data->regmap,
					       LTR501_ALS_THRESH_LOW,
					       &thresh_data, 2);
			if (ret < 0)
				return ret;
			*val = thresh_data & LTR501_ALS_THRESH_MASK;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_PROXIMITY:
		switch (dir) {
		case IIO_EV_DIR_RISING:
			ret = regmap_bulk_read(data->regmap,
					       LTR501_PS_THRESH_UP,
					       &thresh_data, 2);
			if (ret < 0)
				return ret;
			*val = thresh_data & LTR501_PS_THRESH_MASK;
			return IIO_VAL_INT;
		case IIO_EV_DIR_FALLING:
			ret = regmap_bulk_read(data->regmap,
					       LTR501_PS_THRESH_LOW,
					       &thresh_data, 2);
			if (ret < 0)
				return ret;
			*val = thresh_data & LTR501_PS_THRESH_MASK;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}

	return -EINVAL;
}

static int ltr501_write_thresh(struct iio_dev *indio_dev,
			       const struct iio_chan_spec *chan,
			       enum iio_event_type type,
			       enum iio_event_direction dir,
			       enum iio_event_info info,
			       int val, int val2)
{
	struct ltr501_data *data = iio_priv(indio_dev);
	int ret;

	if (val < 0)
		return -EINVAL;

	switch (chan->type) {
	case IIO_INTENSITY:
		if (val > LTR501_ALS_THRESH_MASK)
			return -EINVAL;
		switch (dir) {
		case IIO_EV_DIR_RISING:
			mutex_lock(&data->lock_als);
			ret = regmap_bulk_write(data->regmap,
						LTR501_ALS_THRESH_UP,
						&val, 2);
			mutex_unlock(&data->lock_als);
			return ret;
		case IIO_EV_DIR_FALLING:
			mutex_lock(&data->lock_als);
			ret = regmap_bulk_write(data->regmap,
						LTR501_ALS_THRESH_LOW,
						&val, 2);
			mutex_unlock(&data->lock_als);
			return ret;
		default:
			return -EINVAL;
		}
	case IIO_PROXIMITY:
		if (val > LTR501_PS_THRESH_MASK)
			return -EINVAL;
		switch (dir) {
		case IIO_EV_DIR_RISING:
			mutex_lock(&data->lock_ps);
			ret = regmap_bulk_write(data->regmap,
						LTR501_PS_THRESH_UP,
						&val, 2);
			mutex_unlock(&data->lock_ps);
			return ret;
		case IIO_EV_DIR_FALLING:
			mutex_lock(&data->lock_ps);
			ret = regmap_bulk_write(data->regmap,
						LTR501_PS_THRESH_LOW,
						&val, 2);
			mutex_unlock(&data->lock_ps);
			return ret;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}

	return -EINVAL;
}

static int ltr501_read_event(struct iio_dev *indio_dev,
			     const struct iio_chan_spec *chan,
			     enum iio_event_type type,
			     enum iio_event_direction dir,
			     enum iio_event_info info,
			     int *val, int *val2)
{
	int ret;

	switch (info) {
	case IIO_EV_INFO_VALUE:
		return ltr501_read_thresh(indio_dev, chan, type, dir,
					  info, val, val2);
	case IIO_EV_INFO_PERIOD:
		ret = ltr501_read_intr_prst(iio_priv(indio_dev),
					    chan->type, val2);
		*val = *val2 / 1000000;
		*val2 = *val2 % 1000000;
		return ret;
	default:
		return -EINVAL;
	}

	return -EINVAL;
}

static int ltr501_write_event(struct iio_dev *indio_dev,
			      const struct iio_chan_spec *chan,
			      enum iio_event_type type,
			      enum iio_event_direction dir,
			      enum iio_event_info info,
			      int val, int val2)
{
	switch (info) {
	case IIO_EV_INFO_VALUE:
		if (val2 != 0)
			return -EINVAL;
		return ltr501_write_thresh(indio_dev, chan, type, dir,
					   info, val, val2);
	case IIO_EV_INFO_PERIOD:
		return ltr501_write_intr_prst(iio_priv(indio_dev), chan->type,
					      val, val2);
	default:
		return -EINVAL;
	}

	return -EINVAL;
}

static int ltr501_read_event_config(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir)
{
	struct ltr501_data *data = iio_priv(indio_dev);
	int ret, status;

	switch (chan->type) {
	case IIO_INTENSITY:
		ret = regmap_field_read(data->reg_als_intr, &status);
		if (ret < 0)
			return ret;
		return status;
	case IIO_PROXIMITY:
		ret = regmap_field_read(data->reg_ps_intr, &status);
		if (ret < 0)
			return ret;
		return status;
	default:
		return -EINVAL;
	}

	return -EINVAL;
}

static int ltr501_write_event_config(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir, bool state)
{
	struct ltr501_data *data = iio_priv(indio_dev);
	int ret;

	switch (chan->type) {
	case IIO_INTENSITY:
		mutex_lock(&data->lock_als);
		ret = regmap_field_write(data->reg_als_intr, state);
		mutex_unlock(&data->lock_als);
		return ret;
	case IIO_PROXIMITY:
		mutex_lock(&data->lock_ps);
		ret = regmap_field_write(data->reg_ps_intr, state);
		mutex_unlock(&data->lock_ps);
		return ret;
	default:
		return -EINVAL;
	}

	return -EINVAL;
}

static ssize_t ltr501_show_proximity_scale_avail(struct device *dev,
						 struct device_attribute *attr,
						 char *buf)
{
	struct ltr501_data *data = iio_priv(dev_to_iio_dev(dev));
	const struct ltr501_chip_info *info = data->chip_info;
	ssize_t len = 0;
	int i;

	for (i = 0; i < info->ps_gain_tbl_size; i++) {
		if (info->ps_gain[i].scale == LTR501_RESERVED_GAIN)
			continue;
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d.%06d ",
				 info->ps_gain[i].scale,
				 info->ps_gain[i].uscale);
	}

	buf[len - 1] = '\n';

	return len;
}

static ssize_t ltr501_show_intensity_scale_avail(struct device *dev,
						 struct device_attribute *attr,
						 char *buf)
{
	struct ltr501_data *data = iio_priv(dev_to_iio_dev(dev));
	const struct ltr501_chip_info *info = data->chip_info;
	ssize_t len = 0;
	int i;

	for (i = 0; i < info->als_gain_tbl_size; i++) {
		if (info->als_gain[i].scale == LTR501_RESERVED_GAIN)
			continue;
		len += scnprintf(buf + len, PAGE_SIZE - len, "%d.%06d ",
				 info->als_gain[i].scale,
				 info->als_gain[i].uscale);
	}

	buf[len - 1] = '\n';

	return len;
}

static IIO_CONST_ATTR_INT_TIME_AVAIL("0.05 0.1 0.2 0.4");
static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("20 10 5 2 1 0.5");

static IIO_DEVICE_ATTR(in_proximity_scale_available, S_IRUGO,
		       ltr501_show_proximity_scale_avail, NULL, 0);
static IIO_DEVICE_ATTR(in_intensity_scale_available, S_IRUGO,
		       ltr501_show_intensity_scale_avail, NULL, 0);

static struct attribute *ltr501_attributes[] = {
	&iio_dev_attr_in_proximity_scale_available.dev_attr.attr,
	&iio_dev_attr_in_intensity_scale_available.dev_attr.attr,
	&iio_const_attr_integration_time_available.dev_attr.attr,
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	NULL
};

static struct attribute *ltr301_attributes[] = {
	&iio_dev_attr_in_intensity_scale_available.dev_attr.attr,
	&iio_const_attr_integration_time_available.dev_attr.attr,
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	NULL
};

static const struct attribute_group ltr501_attribute_group = {
	.attrs = ltr501_attributes,
};

static const struct attribute_group ltr301_attribute_group = {
	.attrs = ltr301_attributes,
};

static const struct iio_info ltr501_info_no_irq = {
	.read_raw = ltr501_read_raw,
	.write_raw = ltr501_write_raw,
	.attrs = &ltr501_attribute_group,
};

static const struct iio_info ltr501_info = {
	.read_raw = ltr501_read_raw,
	.write_raw = ltr501_write_raw,
	.attrs = &ltr501_attribute_group,
	.read_event_value	= &ltr501_read_event,
	.write_event_value	= &ltr501_write_event,
	.read_event_config	= &ltr501_read_event_config,
	.write_event_config	= &ltr501_write_event_config,
};

static const struct iio_info ltr301_info_no_irq = {
	.read_raw = ltr501_read_raw,
	.write_raw = ltr501_write_raw,
	.attrs = &ltr301_attribute_group,
};

static const struct iio_info ltr301_info = {
	.read_raw = ltr501_read_raw,
	.write_raw = ltr501_write_raw,
	.attrs = &ltr301_attribute_group,
	.read_event_value	= &ltr501_read_event,
	.write_event_value	= &ltr501_write_event,
	.read_event_config	= &ltr501_read_event_config,
	.write_event_config	= &ltr501_write_event_config,
};

static const struct ltr501_chip_info ltr501_chip_info_tbl[] = {
	[ltr501] = {
		.partid = 0x08,
		.als_gain = ltr501_als_gain_tbl,
		.als_gain_tbl_size = ARRAY_SIZE(ltr501_als_gain_tbl),
		.ps_gain = ltr501_ps_gain_tbl,
		.ps_gain_tbl_size = ARRAY_SIZE(ltr501_ps_gain_tbl),
		.als_mode_active = BIT(0) | BIT(1),
		.als_gain_mask = BIT(3),
		.als_gain_shift = 3,
		.info = &ltr501_info,
		.info_no_irq = &ltr501_info_no_irq,
		.channels = ltr501_channels,
		.no_channels = ARRAY_SIZE(ltr501_channels),
	},
	[ltr559] = {
		.partid = 0x09,
		.als_gain = ltr559_als_gain_tbl,
		.als_gain_tbl_size = ARRAY_SIZE(ltr559_als_gain_tbl),
		.ps_gain = ltr559_ps_gain_tbl,
		.ps_gain_tbl_size = ARRAY_SIZE(ltr559_ps_gain_tbl),
		.als_mode_active = BIT(0),
		.als_gain_mask = BIT(2) | BIT(3) | BIT(4),
		.als_gain_shift = 2,
		.info = &ltr501_info,
		.info_no_irq = &ltr501_info_no_irq,
		.channels = ltr501_channels,
		.no_channels = ARRAY_SIZE(ltr501_channels),
	},
	[ltr301] = {
		.partid = 0x08,
		.als_gain = ltr501_als_gain_tbl,
		.als_gain_tbl_size = ARRAY_SIZE(ltr501_als_gain_tbl),
		.als_mode_active = BIT(0) | BIT(1),
		.als_gain_mask = BIT(3),
		.als_gain_shift = 3,
		.info = &ltr301_info,
		.info_no_irq = &ltr301_info_no_irq,
		.channels = ltr301_channels,
		.no_channels = ARRAY_SIZE(ltr301_channels),
	},
	[ltr303] = {
		.partid = 0x0A,
		.als_gain = ltr559_als_gain_tbl,
		.als_gain_tbl_size = ARRAY_SIZE(ltr559_als_gain_tbl),
		.als_mode_active = BIT(0),
		.als_gain_mask = BIT(2) | BIT(3) | BIT(4),
		.als_gain_shift = 2,
		.info = &ltr301_info,
		.info_no_irq = &ltr301_info_no_irq,
		.channels = ltr301_channels,
		.no_channels = ARRAY_SIZE(ltr301_channels),
	},
};

static int ltr501_write_contr(struct ltr501_data *data, u8 als_val, u8 ps_val)
{
	int ret;

	ret = regmap_write(data->regmap, LTR501_ALS_CONTR, als_val);
	if (ret < 0)
		return ret;

	return regmap_write(data->regmap, LTR501_PS_CONTR, ps_val);
}

static irqreturn_t ltr501_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ltr501_data *data = iio_priv(indio_dev);
	struct {
		u16 channels[3];
		aligned_s64 ts;
	} scan;
	__le16 als_buf[2];
	u8 mask = 0;
	int j = 0;
	int ret, psdata;

	memset(&scan, 0, sizeof(scan));

	/* figure out which data needs to be ready */
	if (test_bit(0, indio_dev->active_scan_mask) ||
	    test_bit(1, indio_dev->active_scan_mask))
		mask |= LTR501_STATUS_ALS_RDY;
	if (test_bit(2, indio_dev->active_scan_mask))
		mask |= LTR501_STATUS_PS_RDY;

	ret = ltr501_drdy(data, mask);
	if (ret < 0)
		goto done;

	if (mask & LTR501_STATUS_ALS_RDY) {
		ret = regmap_bulk_read(data->regmap, LTR501_ALS_DATA1,
				       als_buf, sizeof(als_buf));
		if (ret < 0)
			goto done;
		if (test_bit(0, indio_dev->active_scan_mask))
			scan.channels[j++] = le16_to_cpu(als_buf[1]);
		if (test_bit(1, indio_dev->active_scan_mask))
			scan.channels[j++] = le16_to_cpu(als_buf[0]);
	}

	if (mask & LTR501_STATUS_PS_RDY) {
		ret = regmap_bulk_read(data->regmap, LTR501_PS_DATA,
				       &psdata, 2);
		if (ret < 0)
			goto done;
		scan.channels[j++] = psdata & LTR501_PS_DATA_MASK;
	}

	iio_push_to_buffers_with_timestamp(indio_dev, &scan,
					   iio_get_time_ns(indio_dev));

done:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static irqreturn_t ltr501_interrupt_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct ltr501_data *data = iio_priv(indio_dev);
	int ret, status;

	ret = regmap_read(data->regmap, LTR501_ALS_PS_STATUS, &status);
	if (ret < 0) {
		dev_err(&data->client->dev,
			"irq read int reg failed\n");
		return IRQ_HANDLED;
	}

	if (status & LTR501_STATUS_ALS_INTR)
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_INTENSITY, 0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_EITHER),
			       iio_get_time_ns(indio_dev));

	if (status & LTR501_STATUS_PS_INTR)
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_PROXIMITY, 0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_EITHER),
			       iio_get_time_ns(indio_dev));

	return IRQ_HANDLED;
}

static int ltr501_init(struct ltr501_data *data)
{
	int ret, status;

	ret = regmap_read(data->regmap, LTR501_ALS_CONTR, &status);
	if (ret < 0)
		return ret;

	data->als_contr = status | data->chip_info->als_mode_active;

	ret = regmap_read(data->regmap, LTR501_PS_CONTR, &status);
	if (ret < 0)
		return ret;

	data->ps_contr = status | LTR501_CONTR_ACTIVE;

	ret = ltr501_read_intr_prst(data, IIO_INTENSITY, &data->als_period);
	if (ret < 0)
		return ret;

	ret = ltr501_read_intr_prst(data, IIO_PROXIMITY, &data->ps_period);
	if (ret < 0)
		return ret;

	return ltr501_write_contr(data, data->als_contr, data->ps_contr);
}

static bool ltr501_is_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case LTR501_ALS_DATA1:
	case LTR501_ALS_DATA1_UPPER:
	case LTR501_ALS_DATA0:
	case LTR501_ALS_DATA0_UPPER:
	case LTR501_ALS_PS_STATUS:
	case LTR501_PS_DATA:
	case LTR501_PS_DATA_UPPER:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config ltr501_regmap_config = {
	.name =  LTR501_REGMAP_NAME,
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = LTR501_MAX_REG,
	.cache_type = REGCACHE_RBTREE,
	.volatile_reg = ltr501_is_volatile_reg,
};

static int ltr501_powerdown(struct ltr501_data *data)
{
	return ltr501_write_contr(data, data->als_contr &
				  ~data->chip_info->als_mode_active,
				  data->ps_contr & ~LTR501_CONTR_ACTIVE);
}

static int ltr501_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	static const char * const regulator_names[] = { "vdd", "vddio" };
	struct ltr501_data *data;
	struct iio_dev *indio_dev;
	struct regmap *regmap;
	const void *ddata = NULL;
	int partid, chip_idx;
	const char *name;
	int ret;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	regmap = devm_regmap_init_i2c(client, &ltr501_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Regmap initialization failed.\n");
		return PTR_ERR(regmap);
	}

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	data->regmap = regmap;
	mutex_init(&data->lock_als);
	mutex_init(&data->lock_ps);

	ret = devm_regulator_bulk_get_enable(&client->dev,
					     ARRAY_SIZE(regulator_names),
					     regulator_names);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "Failed to get regulators\n");

	data->reg_it = devm_regmap_field_alloc(&client->dev, regmap,
					       reg_field_it);
	if (IS_ERR(data->reg_it)) {
		dev_err(&client->dev, "Integ time reg field init failed.\n");
		return PTR_ERR(data->reg_it);
	}

	data->reg_als_intr = devm_regmap_field_alloc(&client->dev, regmap,
						     reg_field_als_intr);
	if (IS_ERR(data->reg_als_intr)) {
		dev_err(&client->dev, "ALS intr mode reg field init failed\n");
		return PTR_ERR(data->reg_als_intr);
	}

	data->reg_ps_intr = devm_regmap_field_alloc(&client->dev, regmap,
						    reg_field_ps_intr);
	if (IS_ERR(data->reg_ps_intr)) {
		dev_err(&client->dev, "PS intr mode reg field init failed.\n");
		return PTR_ERR(data->reg_ps_intr);
	}

	data->reg_als_rate = devm_regmap_field_alloc(&client->dev, regmap,
						     reg_field_als_rate);
	if (IS_ERR(data->reg_als_rate)) {
		dev_err(&client->dev, "ALS samp rate field init failed.\n");
		return PTR_ERR(data->reg_als_rate);
	}

	data->reg_ps_rate = devm_regmap_field_alloc(&client->dev, regmap,
						    reg_field_ps_rate);
	if (IS_ERR(data->reg_ps_rate)) {
		dev_err(&client->dev, "PS samp rate field init failed.\n");
		return PTR_ERR(data->reg_ps_rate);
	}

	data->reg_als_prst = devm_regmap_field_alloc(&client->dev, regmap,
						     reg_field_als_prst);
	if (IS_ERR(data->reg_als_prst)) {
		dev_err(&client->dev, "ALS prst reg field init failed\n");
		return PTR_ERR(data->reg_als_prst);
	}

	data->reg_ps_prst = devm_regmap_field_alloc(&client->dev, regmap,
						    reg_field_ps_prst);
	if (IS_ERR(data->reg_ps_prst)) {
		dev_err(&client->dev, "PS prst reg field init failed.\n");
		return PTR_ERR(data->reg_ps_prst);
	}

	ret = regmap_read(data->regmap, LTR501_PART_ID, &partid);
	if (ret < 0)
		return ret;

	if (id) {
		name = id->name;
		chip_idx = id->driver_data;
	} else {
		name = iio_get_acpi_device_name_and_data(&client->dev, &ddata);
		chip_idx = (intptr_t)ddata;
	}
	if (!name)
		return -ENODEV;

	data->chip_info = &ltr501_chip_info_tbl[chip_idx];

	if ((partid >> 4) != data->chip_info->partid)
		return -ENODEV;

	if (device_property_read_u32(&client->dev, "proximity-near-level",
				     &data->near_level))
		data->near_level = 0;

	indio_dev->info = data->chip_info->info;
	indio_dev->channels = data->chip_info->channels;
	indio_dev->num_channels = data->chip_info->no_channels;
	indio_dev->name = name;
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = ltr501_init(data);
	if (ret < 0)
		return ret;

	if (client->irq > 0) {
		ret = devm_request_threaded_irq(&client->dev, client->irq,
						NULL, ltr501_interrupt_handler,
						IRQF_TRIGGER_FALLING |
						IRQF_ONESHOT,
						"ltr501_thresh_event",
						indio_dev);
		if (ret) {
			dev_err(&client->dev, "request irq (%d) failed\n",
				client->irq);
			return ret;
		}
	} else {
		indio_dev->info = data->chip_info->info_no_irq;
	}

	ret = iio_triggered_buffer_setup(indio_dev, NULL,
					 ltr501_trigger_handler, NULL);
	if (ret)
		goto powerdown_on_error;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_unreg_buffer;

	return 0;

error_unreg_buffer:
	iio_triggered_buffer_cleanup(indio_dev);
powerdown_on_error:
	ltr501_powerdown(data);
	return ret;
}

static void ltr501_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);

	iio_device_unregister(indio_dev);
	iio_triggered_buffer_cleanup(indio_dev);
	ltr501_powerdown(iio_priv(indio_dev));
}

static int ltr501_suspend(struct device *dev)
{
	struct ltr501_data *data = iio_priv(i2c_get_clientdata(
					    to_i2c_client(dev)));
	return ltr501_powerdown(data);
}

static int ltr501_resume(struct device *dev)
{
	struct ltr501_data *data = iio_priv(i2c_get_clientdata(
					    to_i2c_client(dev)));

	return ltr501_write_contr(data, data->als_contr,
		data->ps_contr);
}

static DEFINE_SIMPLE_DEV_PM_OPS(ltr501_pm_ops, ltr501_suspend, ltr501_resume);

static const struct acpi_device_id ltr_acpi_match[] = {
	{ "LTER0301", ltr301 },
	/* https://www.catalog.update.microsoft.com/Search.aspx?q=lter0303 */
	{ "LTER0303", ltr303 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, ltr_acpi_match);

static const struct i2c_device_id ltr501_id[] = {
	{ "ltr501", ltr501 },
	{ "ltr559", ltr559 },
	{ "ltr301", ltr301 },
	{ "ltr303", ltr303 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ltr501_id);

static const struct of_device_id ltr501_of_match[] = {
	{ .compatible = "liteon,ltr501", },
	{ .compatible = "liteon,ltr559", },
	{ .compatible = "liteon,ltr301", },
	{ .compatible = "liteon,ltr303", },
	{ }
};
MODULE_DEVICE_TABLE(of, ltr501_of_match);

static struct i2c_driver ltr501_driver = {
	.driver = {
		.name   = LTR501_DRV_NAME,
		.of_match_table = ltr501_of_match,
		.pm	= pm_sleep_ptr(&ltr501_pm_ops),
		.acpi_match_table = ltr_acpi_match,
	},
	.probe = ltr501_probe,
	.remove	= ltr501_remove,
	.id_table = ltr501_id,
};

module_i2c_driver(ltr501_driver);

MODULE_AUTHOR("Peter Meerwald <pmeerw@pmeerw.net>");
MODULE_DESCRIPTION("Lite-On LTR501 ambient light and proximity sensor driver");
MODULE_LICENSE("GPL");
