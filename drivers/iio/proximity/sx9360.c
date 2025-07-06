// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 Google LLC.
 *
 * Driver for Semtech's SX9360 capacitive proximity/button solution.
 * Based on SX9360 driver and copy of datasheet at:
 * https://edit.wpgdadawant.com/uploads/news_file/program/2019/30184/tech_files/program_30184_suggest_other_file.pdf
 */

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/property.h>
#include <linux/regmap.h>

#include <linux/iio/iio.h>

#include "sx_common.h"

/* Nominal Oscillator Frequency. */
#define SX9360_FOSC_MHZ			4
#define SX9360_FOSC_HZ			(SX9360_FOSC_MHZ * 1000000)

/* Register definitions. */
#define SX9360_REG_IRQ_SRC		SX_COMMON_REG_IRQ_SRC
#define SX9360_REG_STAT		0x01
#define SX9360_REG_STAT_COMPSTAT_MASK	GENMASK(2, 1)
#define SX9360_REG_IRQ_MSK		0x02
#define SX9360_CONVDONE_IRQ		BIT(0)
#define SX9360_FAR_IRQ			BIT(2)
#define SX9360_CLOSE_IRQ		BIT(3)
#define SX9360_REG_IRQ_CFG		0x03

#define SX9360_REG_GNRL_CTRL0		0x10
#define SX9360_REG_GNRL_CTRL0_PHEN_MASK GENMASK(1, 0)
#define SX9360_REG_GNRL_CTRL1		0x11
#define SX9360_REG_GNRL_CTRL1_SCANPERIOD_MASK GENMASK(2, 0)
#define SX9360_REG_GNRL_CTRL2		0x12
#define SX9360_REG_GNRL_CTRL2_PERIOD_102MS	0x32
#define SX9360_REG_GNRL_REG_2_PERIOD_MS(_r)	\
	(((_r) * 8192) / (SX9360_FOSC_HZ / 1000))
#define SX9360_REG_GNRL_FREQ_2_REG(_f)  (((_f) * 8192) / SX9360_FOSC_HZ)
#define SX9360_REG_GNRL_REG_2_FREQ(_r)  (SX9360_FOSC_HZ / ((_r) * 8192))

#define SX9360_REG_AFE_CTRL1		0x21
#define SX9360_REG_AFE_CTRL1_RESFILTIN_MASK GENMASK(3, 0)
#define SX9360_REG_AFE_CTRL1_RESFILTIN_0OHMS 0
#define SX9360_REG_AFE_PARAM0_PHR	0x22
#define SX9360_REG_AFE_PARAM1_PHR	0x23
#define SX9360_REG_AFE_PARAM0_PHM	0x24
#define SX9360_REG_AFE_PARAM0_RSVD		0x08
#define SX9360_REG_AFE_PARAM0_RESOLUTION_MASK	GENMASK(2, 0)
#define SX9360_REG_AFE_PARAM0_RESOLUTION_128	0x02
#define SX9360_REG_AFE_PARAM1_PHM	0x25
#define SX9360_REG_AFE_PARAM1_AGAIN_PHM_6PF	0x40
#define SX9360_REG_AFE_PARAM1_FREQ_83_33HZ	0x06

#define SX9360_REG_PROX_CTRL0_PHR	0x40
#define SX9360_REG_PROX_CTRL0_PHM	0x41
#define SX9360_REG_PROX_CTRL0_GAIN_MASK	GENMASK(5, 3)
#define SX9360_REG_PROX_CTRL0_GAIN_1		0x80
#define SX9360_REG_PROX_CTRL0_RAWFILT_MASK	GENMASK(2, 0)
#define SX9360_REG_PROX_CTRL0_RAWFILT_1P50	0x01
#define SX9360_REG_PROX_CTRL1		0x42
#define SX9360_REG_PROX_CTRL1_AVGNEG_THRESH_MASK	GENMASK(5, 3)
#define SX9360_REG_PROX_CTRL1_AVGNEG_THRESH_16K 0x20
#define SX9360_REG_PROX_CTRL2		0x43
#define SX9360_REG_PROX_CTRL2_AVGDEB_MASK	GENMASK(7, 6)
#define SX9360_REG_PROX_CTRL2_AVGDEB_2SAMPLES	0x40
#define SX9360_REG_PROX_CTRL2_AVGPOS_THRESH_16K	0x20
#define SX9360_REG_PROX_CTRL3		0x44
#define SX9360_REG_PROX_CTRL3_AVGNEG_FILT_MASK	GENMASK(5, 3)
#define SX9360_REG_PROX_CTRL3_AVGNEG_FILT_2	0x08
#define SX9360_REG_PROX_CTRL3_AVGPOS_FILT_MASK	GENMASK(2, 0)
#define SX9360_REG_PROX_CTRL3_AVGPOS_FILT_256	0x04
#define SX9360_REG_PROX_CTRL4		0x45
#define SX9360_REG_PROX_CTRL4_HYST_MASK			GENMASK(5, 4)
#define SX9360_REG_PROX_CTRL4_CLOSE_DEBOUNCE_MASK	GENMASK(3, 2)
#define SX9360_REG_PROX_CTRL4_FAR_DEBOUNCE_MASK		GENMASK(1, 0)
#define SX9360_REG_PROX_CTRL5		0x46
#define SX9360_REG_PROX_CTRL5_PROXTHRESH_32	0x08

#define SX9360_REG_REF_CORR0		0x60
#define SX9360_REG_REF_CORR1		0x61

#define SX9360_REG_USEFUL_PHR_MSB		0x90
#define SX9360_REG_USEFUL_PHR_LSB		0x91

#define SX9360_REG_OFFSET_PMR_MSB		0x92
#define SX9360_REG_OFFSET_PMR_LSB		0x93

#define SX9360_REG_USEFUL_PHM_MSB		0x94
#define SX9360_REG_USEFUL_PHM_LSB		0x95

#define SX9360_REG_AVG_PHM_MSB		0x96
#define SX9360_REG_AVG_PHM_LSB		0x97

#define SX9360_REG_DIFF_PHM_MSB		0x98
#define SX9360_REG_DIFF_PHM_LSB		0x99

#define SX9360_REG_OFFSET_PHM_MSB		0x9a
#define SX9360_REG_OFFSET_PHM_LSB		0x9b

#define SX9360_REG_USE_FILTER_MSB		0x9a
#define SX9360_REG_USE_FILTER_LSB		0x9b

#define SX9360_REG_RESET		0xcf
/* Write this to REG_RESET to do a soft reset. */
#define SX9360_SOFT_RESET		0xde

#define SX9360_REG_WHOAMI		0xfa
#define   SX9360_WHOAMI_VALUE				0x60

#define SX9360_REG_REVISION		0xfe

/* 2 channels, Phase Reference and Measurement. */
#define SX9360_NUM_CHANNELS		2

static const struct iio_chan_spec sx9360_channels[] = {
	{
		.type = IIO_PROXIMITY,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_HARDWAREGAIN),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_separate_available =
			BIT(IIO_CHAN_INFO_HARDWAREGAIN),
		.info_mask_shared_by_all_available =
			BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.indexed = 1,
		.address = SX9360_REG_USEFUL_PHR_MSB,
		.channel = 0,
		.scan_index = 0,
		.scan_type = {
			.sign = 's',
			.realbits = 12,
			.storagebits = 16,
			.endianness = IIO_BE,
		},
	},
	{
		.type = IIO_PROXIMITY,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_HARDWAREGAIN),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.info_mask_separate_available =
			BIT(IIO_CHAN_INFO_HARDWAREGAIN),
		.info_mask_shared_by_all_available =
			BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.indexed = 1,
		.address = SX9360_REG_USEFUL_PHM_MSB,
		.event_spec = sx_common_events,
		.num_event_specs = ARRAY_SIZE(sx_common_events),
		.channel = 1,
		.scan_index = 1,
		.scan_type = {
			.sign = 's',
			.realbits = 12,
			.storagebits = 16,
			.endianness = IIO_BE,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(2),
};

/*
 * Each entry contains the integer part (val) and the fractional part, in micro
 * seconds. It conforms to the IIO output IIO_VAL_INT_PLUS_MICRO.
 *
 * The frequency control register holds the period, with a ~2ms increment.
 * Therefore the smallest frequency is 4MHz / (2047 * 8192),
 * The fastest is 4MHz / 8192.
 * The interval is not linear, but given there is 2047 possible value,
 * Returns the fake increment of (Max-Min)/2047
 */
static const struct {
	int val;
	int val2;
} sx9360_samp_freq_interval[] = {
	{ 0, 281250 },  /* 4MHz / (8192 * 2047) */
	{ 0, 281250 },
	{ 448, 281250 },  /* 4MHz / 8192 */
};

static const struct regmap_range sx9360_writable_reg_ranges[] = {
	/*
	 * To set COMPSTAT for compensation, even if datasheet says register is
	 * RO.
	 */
	regmap_reg_range(SX9360_REG_STAT, SX9360_REG_IRQ_CFG),
	regmap_reg_range(SX9360_REG_GNRL_CTRL0, SX9360_REG_GNRL_CTRL2),
	regmap_reg_range(SX9360_REG_AFE_CTRL1, SX9360_REG_AFE_PARAM1_PHM),
	regmap_reg_range(SX9360_REG_PROX_CTRL0_PHR, SX9360_REG_PROX_CTRL5),
	regmap_reg_range(SX9360_REG_REF_CORR0, SX9360_REG_REF_CORR1),
	regmap_reg_range(SX9360_REG_OFFSET_PMR_MSB, SX9360_REG_OFFSET_PMR_LSB),
	regmap_reg_range(SX9360_REG_RESET, SX9360_REG_RESET),
};

static const struct regmap_access_table sx9360_writeable_regs = {
	.yes_ranges = sx9360_writable_reg_ranges,
	.n_yes_ranges = ARRAY_SIZE(sx9360_writable_reg_ranges),
};

/*
 * All allocated registers are readable, so we just list unallocated
 * ones.
 */
static const struct regmap_range sx9360_non_readable_reg_ranges[] = {
	regmap_reg_range(SX9360_REG_IRQ_CFG + 1, SX9360_REG_GNRL_CTRL0 - 1),
	regmap_reg_range(SX9360_REG_GNRL_CTRL2 + 1, SX9360_REG_AFE_CTRL1 - 1),
	regmap_reg_range(SX9360_REG_AFE_PARAM1_PHM + 1,
			 SX9360_REG_PROX_CTRL0_PHR - 1),
	regmap_reg_range(SX9360_REG_PROX_CTRL5 + 1, SX9360_REG_REF_CORR0 - 1),
	regmap_reg_range(SX9360_REG_REF_CORR1 + 1,
			 SX9360_REG_USEFUL_PHR_MSB - 1),
	regmap_reg_range(SX9360_REG_USE_FILTER_LSB + 1, SX9360_REG_RESET - 1),
	regmap_reg_range(SX9360_REG_RESET + 1, SX9360_REG_WHOAMI - 1),
	regmap_reg_range(SX9360_REG_WHOAMI + 1, SX9360_REG_REVISION - 1),
};

static const struct regmap_access_table sx9360_readable_regs = {
	.no_ranges = sx9360_non_readable_reg_ranges,
	.n_no_ranges = ARRAY_SIZE(sx9360_non_readable_reg_ranges),
};

static const struct regmap_range sx9360_volatile_reg_ranges[] = {
	regmap_reg_range(SX9360_REG_IRQ_SRC, SX9360_REG_STAT),
	regmap_reg_range(SX9360_REG_USEFUL_PHR_MSB, SX9360_REG_USE_FILTER_LSB),
	regmap_reg_range(SX9360_REG_WHOAMI, SX9360_REG_WHOAMI),
	regmap_reg_range(SX9360_REG_REVISION, SX9360_REG_REVISION),
};

static const struct regmap_access_table sx9360_volatile_regs = {
	.yes_ranges = sx9360_volatile_reg_ranges,
	.n_yes_ranges = ARRAY_SIZE(sx9360_volatile_reg_ranges),
};

static const struct regmap_config sx9360_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = SX9360_REG_REVISION,
	.cache_type = REGCACHE_RBTREE,

	.wr_table = &sx9360_writeable_regs,
	.rd_table = &sx9360_readable_regs,
	.volatile_table = &sx9360_volatile_regs,
};

static int sx9360_read_prox_data(struct sx_common_data *data,
				 const struct iio_chan_spec *chan,
				 __be16 *val)
{
	return regmap_bulk_read(data->regmap, chan->address, val, sizeof(*val));
}

/*
 * If we have no interrupt support, we have to wait for a scan period
 * after enabling a channel to get a result.
 */
static int sx9360_wait_for_sample(struct sx_common_data *data)
{
	int ret;
	__be16 buf;

	ret = regmap_bulk_read(data->regmap, SX9360_REG_GNRL_CTRL1,
			       &buf, sizeof(buf));
	if (ret < 0)
		return ret;
	msleep(SX9360_REG_GNRL_REG_2_PERIOD_MS(be16_to_cpu(buf)));

	return 0;
}

static int sx9360_read_gain(struct sx_common_data *data,
			    const struct iio_chan_spec *chan, int *val)
{
	unsigned int reg, regval;
	int ret;

	reg = SX9360_REG_PROX_CTRL0_PHR + chan->channel;
	ret = regmap_read(data->regmap, reg, &regval);
	if (ret)
		return ret;

	*val = 1 << FIELD_GET(SX9360_REG_PROX_CTRL0_GAIN_MASK, regval);

	return IIO_VAL_INT;
}

static int sx9360_read_samp_freq(struct sx_common_data *data,
				 int *val, int *val2)
{
	int ret, divisor;
	__be16 buf;

	ret = regmap_bulk_read(data->regmap, SX9360_REG_GNRL_CTRL1,
			       &buf, sizeof(buf));
	if (ret < 0)
		return ret;
	divisor = be16_to_cpu(buf);
	if (divisor == 0) {
		*val = 0;
		return IIO_VAL_INT;
	}

	*val = SX9360_FOSC_HZ;
	*val2 = divisor * 8192;

	return IIO_VAL_FRACTIONAL;
}

static int sx9360_read_raw(struct iio_dev *indio_dev,
			   const struct iio_chan_spec *chan,
			   int *val, int *val2, long mask)
{
	struct sx_common_data *data = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;

		ret = sx_common_read_proximity(data, chan, val);
		iio_device_release_direct(indio_dev);
		return ret;
	case IIO_CHAN_INFO_HARDWAREGAIN:
		if (!iio_device_claim_direct(indio_dev))
			return -EBUSY;

		ret = sx9360_read_gain(data, chan, val);
		iio_device_release_direct(indio_dev);
		return ret;
	case IIO_CHAN_INFO_SAMP_FREQ:
		return sx9360_read_samp_freq(data, val, val2);
	default:
		return -EINVAL;
	}
}

static const char *sx9360_channel_labels[SX9360_NUM_CHANNELS] = {
	"reference", "main",
};

static int sx9360_read_label(struct iio_dev *iio_dev, const struct iio_chan_spec *chan,
			     char *label)
{
	return sysfs_emit(label, "%s\n", sx9360_channel_labels[chan->channel]);
}

static const int sx9360_gain_vals[] = { 1, 2, 4, 8 };

static int sx9360_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     const int **vals, int *type, int *length,
			     long mask)
{
	if (chan->type != IIO_PROXIMITY)
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_HARDWAREGAIN:
		*type = IIO_VAL_INT;
		*length = ARRAY_SIZE(sx9360_gain_vals);
		*vals = sx9360_gain_vals;
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*type = IIO_VAL_INT_PLUS_MICRO;
		*length = ARRAY_SIZE(sx9360_samp_freq_interval) * 2;
		*vals = (int *)sx9360_samp_freq_interval;
		return IIO_AVAIL_RANGE;
	default:
		return -EINVAL;
	}
}

static int sx9360_set_samp_freq(struct sx_common_data *data,
				int val, int val2)
{
	int reg;
	__be16 buf;

	reg = val * 8192 / SX9360_FOSC_HZ + val2 * 8192 / (SX9360_FOSC_MHZ);
	buf = cpu_to_be16(reg);
	guard(mutex)(&data->mutex);

	return regmap_bulk_write(data->regmap, SX9360_REG_GNRL_CTRL1, &buf,
				 sizeof(buf));
}

static int sx9360_read_thresh(struct sx_common_data *data, int *val)
{
	unsigned int regval;
	int ret;

	ret = regmap_read(data->regmap, SX9360_REG_PROX_CTRL5, &regval);
	if (ret)
		return ret;

	if (regval <= 1)
		*val = regval;
	else
		*val = (regval * regval) / 2;

	return IIO_VAL_INT;
}

static int sx9360_read_hysteresis(struct sx_common_data *data, int *val)
{
	unsigned int regval, pthresh;
	int ret;

	ret = sx9360_read_thresh(data, &pthresh);
	if (ret < 0)
		return ret;

	ret = regmap_read(data->regmap, SX9360_REG_PROX_CTRL4, &regval);
	if (ret)
		return ret;

	regval = FIELD_GET(SX9360_REG_PROX_CTRL4_HYST_MASK, regval);
	if (!regval)
		*val = 0;
	else
		*val = pthresh >> (5 - regval);

	return IIO_VAL_INT;
}

static int sx9360_read_far_debounce(struct sx_common_data *data, int *val)
{
	unsigned int regval;
	int ret;

	ret = regmap_read(data->regmap, SX9360_REG_PROX_CTRL4, &regval);
	if (ret)
		return ret;

	regval = FIELD_GET(SX9360_REG_PROX_CTRL4_FAR_DEBOUNCE_MASK, regval);
	if (regval)
		*val = 1 << regval;
	else
		*val = 0;

	return IIO_VAL_INT;
}

static int sx9360_read_close_debounce(struct sx_common_data *data, int *val)
{
	unsigned int regval;
	int ret;

	ret = regmap_read(data->regmap, SX9360_REG_PROX_CTRL4, &regval);
	if (ret)
		return ret;

	regval = FIELD_GET(SX9360_REG_PROX_CTRL4_CLOSE_DEBOUNCE_MASK, regval);
	if (regval)
		*val = 1 << regval;
	else
		*val = 0;

	return IIO_VAL_INT;
}

static int sx9360_read_event_val(struct iio_dev *indio_dev,
				 const struct iio_chan_spec *chan,
				 enum iio_event_type type,
				 enum iio_event_direction dir,
				 enum iio_event_info info, int *val, int *val2)
{
	struct sx_common_data *data = iio_priv(indio_dev);

	if (chan->type != IIO_PROXIMITY)
		return -EINVAL;

	switch (info) {
	case IIO_EV_INFO_VALUE:
		return sx9360_read_thresh(data, val);
	case IIO_EV_INFO_PERIOD:
		switch (dir) {
		case IIO_EV_DIR_RISING:
			return sx9360_read_far_debounce(data, val);
		case IIO_EV_DIR_FALLING:
			return sx9360_read_close_debounce(data, val);
		default:
			return -EINVAL;
		}
	case IIO_EV_INFO_HYSTERESIS:
		return sx9360_read_hysteresis(data, val);
	default:
		return -EINVAL;
	}
}

static int sx9360_write_thresh(struct sx_common_data *data, int _val)
{
	unsigned int val = _val;

	if (val >= 1)
		val = int_sqrt(2 * val);

	if (val > 0xff)
		return -EINVAL;

	guard(mutex)(&data->mutex);
	return regmap_write(data->regmap, SX9360_REG_PROX_CTRL5, val);
}

static int sx9360_write_hysteresis(struct sx_common_data *data, int _val)
{
	unsigned int hyst, val = _val;
	int ret, pthresh;

	ret = sx9360_read_thresh(data, &pthresh);
	if (ret < 0)
		return ret;

	if (val == 0)
		hyst = 0;
	else if (val >= pthresh >> 2)
		hyst = 3;
	else if (val >= pthresh >> 3)
		hyst = 2;
	else if (val >= pthresh >> 4)
		hyst = 1;
	else
		return -EINVAL;

	hyst = FIELD_PREP(SX9360_REG_PROX_CTRL4_HYST_MASK, hyst);
	guard(mutex)(&data->mutex);
	return regmap_update_bits(data->regmap, SX9360_REG_PROX_CTRL4,
				  SX9360_REG_PROX_CTRL4_HYST_MASK, hyst);
}

static int sx9360_write_far_debounce(struct sx_common_data *data, int _val)
{
	unsigned int regval, val = _val;

	if (val > 0)
		val = ilog2(val);
	if (!FIELD_FIT(SX9360_REG_PROX_CTRL4_FAR_DEBOUNCE_MASK, val))
		return -EINVAL;

	regval = FIELD_PREP(SX9360_REG_PROX_CTRL4_FAR_DEBOUNCE_MASK, val);

	guard(mutex)(&data->mutex);
	return regmap_update_bits(data->regmap, SX9360_REG_PROX_CTRL4,
				  SX9360_REG_PROX_CTRL4_FAR_DEBOUNCE_MASK,
				  regval);
}

static int sx9360_write_close_debounce(struct sx_common_data *data, int _val)
{
	unsigned int regval, val = _val;

	if (val > 0)
		val = ilog2(val);
	if (!FIELD_FIT(SX9360_REG_PROX_CTRL4_CLOSE_DEBOUNCE_MASK, val))
		return -EINVAL;

	regval = FIELD_PREP(SX9360_REG_PROX_CTRL4_CLOSE_DEBOUNCE_MASK, val);

	guard(mutex)(&data->mutex);
	return regmap_update_bits(data->regmap, SX9360_REG_PROX_CTRL4,
				  SX9360_REG_PROX_CTRL4_CLOSE_DEBOUNCE_MASK,
				  regval);
}

static int sx9360_write_event_val(struct iio_dev *indio_dev,
				  const struct iio_chan_spec *chan,
				  enum iio_event_type type,
				  enum iio_event_direction dir,
				  enum iio_event_info info, int val, int val2)
{
	struct sx_common_data *data = iio_priv(indio_dev);

	if (chan->type != IIO_PROXIMITY)
		return -EINVAL;

	switch (info) {
	case IIO_EV_INFO_VALUE:
		return sx9360_write_thresh(data, val);
	case IIO_EV_INFO_PERIOD:
		switch (dir) {
		case IIO_EV_DIR_RISING:
			return sx9360_write_far_debounce(data, val);
		case IIO_EV_DIR_FALLING:
			return sx9360_write_close_debounce(data, val);
		default:
			return -EINVAL;
		}
	case IIO_EV_INFO_HYSTERESIS:
		return sx9360_write_hysteresis(data, val);
	default:
		return -EINVAL;
	}
}

static int sx9360_write_gain(struct sx_common_data *data,
			     const struct iio_chan_spec *chan, int val)
{
	unsigned int gain, reg;

	gain = ilog2(val);
	reg = SX9360_REG_PROX_CTRL0_PHR + chan->channel;
	gain = FIELD_PREP(SX9360_REG_PROX_CTRL0_GAIN_MASK, gain);

	guard(mutex)(&data->mutex);
	return regmap_update_bits(data->regmap, reg,
				  SX9360_REG_PROX_CTRL0_GAIN_MASK,
				  gain);
}

static int sx9360_write_raw(struct iio_dev *indio_dev,
			    const struct iio_chan_spec *chan, int val, int val2,
			    long mask)
{
	struct sx_common_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		return sx9360_set_samp_freq(data, val, val2);
	case IIO_CHAN_INFO_HARDWAREGAIN:
		return sx9360_write_gain(data, chan, val);
	default:
		return -EINVAL;
	}
}

static const struct sx_common_reg_default sx9360_default_regs[] = {
	{ SX9360_REG_IRQ_MSK, 0x00 },
	{ SX9360_REG_IRQ_CFG, 0x00, "irq_cfg" },
	/*
	 * The lower 2 bits should not be set as it enable sensors measurements.
	 * Turning the detection on before the configuration values are set to
	 * good values can cause the device to return erroneous readings.
	 */
	{ SX9360_REG_GNRL_CTRL0, 0x00, "gnrl_ctrl0" },
	{ SX9360_REG_GNRL_CTRL1, 0x00, "gnrl_ctrl1" },
	{ SX9360_REG_GNRL_CTRL2, SX9360_REG_GNRL_CTRL2_PERIOD_102MS, "gnrl_ctrl2" },

	{ SX9360_REG_AFE_CTRL1, SX9360_REG_AFE_CTRL1_RESFILTIN_0OHMS, "afe_ctrl0" },
	{ SX9360_REG_AFE_PARAM0_PHR, SX9360_REG_AFE_PARAM0_RSVD |
		SX9360_REG_AFE_PARAM0_RESOLUTION_128, "afe_param0_phr" },
	{ SX9360_REG_AFE_PARAM1_PHR, SX9360_REG_AFE_PARAM1_AGAIN_PHM_6PF |
		SX9360_REG_AFE_PARAM1_FREQ_83_33HZ, "afe_param1_phr" },
	{ SX9360_REG_AFE_PARAM0_PHM, SX9360_REG_AFE_PARAM0_RSVD |
		SX9360_REG_AFE_PARAM0_RESOLUTION_128, "afe_param0_phm" },
	{ SX9360_REG_AFE_PARAM1_PHM, SX9360_REG_AFE_PARAM1_AGAIN_PHM_6PF |
		SX9360_REG_AFE_PARAM1_FREQ_83_33HZ, "afe_param1_phm" },

	{ SX9360_REG_PROX_CTRL0_PHR, SX9360_REG_PROX_CTRL0_GAIN_1 |
		SX9360_REG_PROX_CTRL0_RAWFILT_1P50, "prox_ctrl0_phr" },
	{ SX9360_REG_PROX_CTRL0_PHM, SX9360_REG_PROX_CTRL0_GAIN_1 |
		SX9360_REG_PROX_CTRL0_RAWFILT_1P50, "prox_ctrl0_phm" },
	{ SX9360_REG_PROX_CTRL1, SX9360_REG_PROX_CTRL1_AVGNEG_THRESH_16K, "prox_ctrl1" },
	{ SX9360_REG_PROX_CTRL2, SX9360_REG_PROX_CTRL2_AVGDEB_2SAMPLES |
		SX9360_REG_PROX_CTRL2_AVGPOS_THRESH_16K, "prox_ctrl2" },
	{ SX9360_REG_PROX_CTRL3, SX9360_REG_PROX_CTRL3_AVGNEG_FILT_2 |
		SX9360_REG_PROX_CTRL3_AVGPOS_FILT_256, "prox_ctrl3" },
	{ SX9360_REG_PROX_CTRL4, 0x00, "prox_ctrl4" },
	{ SX9360_REG_PROX_CTRL5, SX9360_REG_PROX_CTRL5_PROXTHRESH_32, "prox_ctrl5" },
};

/* Activate all channels and perform an initial compensation. */
static int sx9360_init_compensation(struct iio_dev *indio_dev)
{
	struct sx_common_data *data = iio_priv(indio_dev);
	unsigned int val;
	int ret;

	/* run the compensation phase on all channels */
	ret = regmap_set_bits(data->regmap, SX9360_REG_STAT,
			      SX9360_REG_STAT_COMPSTAT_MASK);
	if (ret)
		return ret;

	return regmap_read_poll_timeout(data->regmap, SX9360_REG_STAT, val,
				       !(val & SX9360_REG_STAT_COMPSTAT_MASK),
				       20000, 2000000);
}

static const struct sx_common_reg_default *
sx9360_get_default_reg(struct device *dev, int idx,
		       struct sx_common_reg_default *reg_def)
{
	u32 raw = 0, pos = 0;
	int ret;

	memcpy(reg_def, &sx9360_default_regs[idx], sizeof(*reg_def));
	switch (reg_def->reg) {
	case SX9360_REG_AFE_CTRL1:
		ret = device_property_read_u32(dev,
				"semtech,input-precharge-resistor-ohms",
				&raw);
		if (ret)
			break;

		reg_def->def &= ~SX9360_REG_AFE_CTRL1_RESFILTIN_MASK;
		reg_def->def |= FIELD_PREP(SX9360_REG_AFE_CTRL1_RESFILTIN_MASK,
					   raw / 2000);
		break;
	case SX9360_REG_AFE_PARAM0_PHR:
	case SX9360_REG_AFE_PARAM0_PHM:
		ret = device_property_read_u32(dev, "semtech,resolution", &raw);
		if (ret)
			break;

		raw = ilog2(raw) - 3;

		reg_def->def &= ~SX9360_REG_AFE_PARAM0_RESOLUTION_MASK;
		reg_def->def |= FIELD_PREP(SX9360_REG_AFE_PARAM0_RESOLUTION_MASK, raw);
		break;
	case SX9360_REG_PROX_CTRL0_PHR:
	case SX9360_REG_PROX_CTRL0_PHM:
		ret = device_property_read_u32(dev, "semtech,proxraw-strength", &raw);
		if (ret)
			break;

		reg_def->def &= ~SX9360_REG_PROX_CTRL0_RAWFILT_MASK;
		reg_def->def |= FIELD_PREP(SX9360_REG_PROX_CTRL0_RAWFILT_MASK, raw);
		break;
	case SX9360_REG_PROX_CTRL3:
		ret = device_property_read_u32(dev, "semtech,avg-pos-strength",
					       &pos);
		if (ret)
			break;

		/* Powers of 2, except for a gap between 16 and 64 */
		raw = clamp(ilog2(pos), 3, 11) - (pos >= 32 ? 4 : 3);
		reg_def->def &= ~SX9360_REG_PROX_CTRL3_AVGPOS_FILT_MASK;
		reg_def->def |= FIELD_PREP(SX9360_REG_PROX_CTRL3_AVGPOS_FILT_MASK, raw);
		break;
	}

	return reg_def;
}

static int sx9360_check_whoami(struct device *dev, struct iio_dev *indio_dev)
{
	/*
	 * Only one sensor for this driver. Assuming the device tree
	 * is correct, just set the sensor name.
	 */
	indio_dev->name = "sx9360";
	return 0;
}

static const struct sx_common_chip_info sx9360_chip_info = {
	.reg_stat = SX9360_REG_STAT,
	.reg_irq_msk = SX9360_REG_IRQ_MSK,
	.reg_enable_chan = SX9360_REG_GNRL_CTRL0,
	.reg_reset = SX9360_REG_RESET,

	.mask_enable_chan = SX9360_REG_GNRL_CTRL0_PHEN_MASK,
	.stat_offset = 2,
	.num_channels = SX9360_NUM_CHANNELS,
	.num_default_regs = ARRAY_SIZE(sx9360_default_regs),

	.ops = {
		.read_prox_data = sx9360_read_prox_data,
		.check_whoami = sx9360_check_whoami,
		.init_compensation = sx9360_init_compensation,
		.wait_for_sample = sx9360_wait_for_sample,
		.get_default_reg = sx9360_get_default_reg,
	},

	.iio_channels = sx9360_channels,
	.num_iio_channels = ARRAY_SIZE(sx9360_channels),
	.iio_info =  {
		.read_raw = sx9360_read_raw,
		.read_avail = sx9360_read_avail,
		.read_label = sx9360_read_label,
		.read_event_value = sx9360_read_event_val,
		.write_event_value = sx9360_write_event_val,
		.write_raw = sx9360_write_raw,
		.read_event_config = sx_common_read_event_config,
		.write_event_config = sx_common_write_event_config,
	},
};

static int sx9360_probe(struct i2c_client *client)
{
	return sx_common_probe(client, &sx9360_chip_info, &sx9360_regmap_config);
}

static int sx9360_suspend(struct device *dev)
{
	struct sx_common_data *data = iio_priv(dev_get_drvdata(dev));
	unsigned int regval;
	int ret;

	disable_irq_nosync(data->client->irq);

	guard(mutex)(&data->mutex);
	ret = regmap_read(data->regmap, SX9360_REG_GNRL_CTRL0, &regval);
	if (ret < 0)
		return ret;

	data->suspend_ctrl =
		FIELD_GET(SX9360_REG_GNRL_CTRL0_PHEN_MASK, regval);


	/* Disable all phases, send the device to sleep. */
	return regmap_write(data->regmap, SX9360_REG_GNRL_CTRL0, 0);
}

static int sx9360_resume(struct device *dev)
{
	struct sx_common_data *data = iio_priv(dev_get_drvdata(dev));

	scoped_guard(mutex, &data->mutex) {
		int ret = regmap_update_bits(data->regmap,
					     SX9360_REG_GNRL_CTRL0,
					     SX9360_REG_GNRL_CTRL0_PHEN_MASK,
					     data->suspend_ctrl);
		if (ret)
			return ret;
	}
	enable_irq(data->client->irq);
	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(sx9360_pm_ops, sx9360_suspend, sx9360_resume);

static const struct acpi_device_id sx9360_acpi_match[] = {
	{ "STH9360", SX9360_WHOAMI_VALUE },
	{ "SAMM0208", SX9360_WHOAMI_VALUE },
	{ }
};
MODULE_DEVICE_TABLE(acpi, sx9360_acpi_match);

static const struct of_device_id sx9360_of_match[] = {
	{ .compatible = "semtech,sx9360", (void *)SX9360_WHOAMI_VALUE },
	{ }
};
MODULE_DEVICE_TABLE(of, sx9360_of_match);

static const struct i2c_device_id sx9360_id[] = {
	{"sx9360", SX9360_WHOAMI_VALUE },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sx9360_id);

static struct i2c_driver sx9360_driver = {
	.driver = {
		.name	= "sx9360",
		.acpi_match_table = sx9360_acpi_match,
		.of_match_table = sx9360_of_match,
		.pm = pm_sleep_ptr(&sx9360_pm_ops),

		/*
		 * Lots of i2c transfers in probe + over 200 ms waiting in
		 * sx9360_init_compensation() mean a slow probe; prefer async
		 * so we don't delay boot if we're builtin to the kernel.
		 */
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe		= sx9360_probe,
	.id_table	= sx9360_id,
};
module_i2c_driver(sx9360_driver);

MODULE_AUTHOR("Gwendal Grignou <gwendal@chromium.org>");
MODULE_DESCRIPTION("Driver for Semtech SX9360 proximity sensor");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("SEMTECH_PROX");
