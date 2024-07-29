// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 Google LLC.
 *
 * Driver for Semtech's SX9324 capacitive proximity/button solution.
 * Based on SX9324 driver and copy of datasheet at:
 * https://edit.wpgdadawant.com/uploads/news_file/program/2019/30184/tech_files/program_30184_suggest_other_file.pdf
 */

#include <linux/acpi.h>
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

/* Register definitions. */
#define SX9324_REG_IRQ_SRC		SX_COMMON_REG_IRQ_SRC
#define SX9324_REG_STAT0		0x01
#define SX9324_REG_STAT1		0x02
#define SX9324_REG_STAT2		0x03
#define SX9324_REG_STAT2_COMPSTAT_MASK	GENMASK(3, 0)
#define SX9324_REG_STAT3		0x04
#define SX9324_REG_IRQ_MSK		0x05
#define SX9324_CONVDONE_IRQ		BIT(3)
#define SX9324_FAR_IRQ			BIT(5)
#define SX9324_CLOSE_IRQ		BIT(6)
#define SX9324_REG_IRQ_CFG0		0x06
#define SX9324_REG_IRQ_CFG1		0x07
#define SX9324_REG_IRQ_CFG1_FAILCOND    0x80
#define SX9324_REG_IRQ_CFG2		0x08

#define SX9324_REG_GNRL_CTRL0		0x10
#define SX9324_REG_GNRL_CTRL0_SCANPERIOD_MASK GENMASK(4, 0)
#define SX9324_REG_GNRL_CTRL0_SCANPERIOD_100MS 0x16
#define SX9324_REG_GNRL_CTRL1		0x11
#define SX9324_REG_GNRL_CTRL1_PHEN_MASK GENMASK(3, 0)
#define SX9324_REG_GNRL_CTRL1_PAUSECTRL 0x20

#define SX9324_REG_I2C_ADDR		0x14
#define SX9324_REG_CLK_SPRD		0x15

#define SX9324_REG_AFE_CTRL0		0x20
#define SX9324_REG_AFE_CTRL0_RINT_SHIFT		6
#define SX9324_REG_AFE_CTRL0_RINT_MASK \
	GENMASK(SX9324_REG_AFE_CTRL0_RINT_SHIFT + 1, \
		SX9324_REG_AFE_CTRL0_RINT_SHIFT)
#define SX9324_REG_AFE_CTRL0_RINT_LOWEST	0x00
#define SX9324_REG_AFE_CTRL0_CSIDLE_SHIFT	4
#define SX9324_REG_AFE_CTRL0_CSIDLE_MASK \
	GENMASK(SX9324_REG_AFE_CTRL0_CSIDLE_SHIFT + 1, \
		SX9324_REG_AFE_CTRL0_CSIDLE_SHIFT)
#define SX9324_REG_AFE_CTRL0_RINT_LOWEST	0x00
#define SX9324_REG_AFE_CTRL1		0x21
#define SX9324_REG_AFE_CTRL2		0x22
#define SX9324_REG_AFE_CTRL3		0x23
#define SX9324_REG_AFE_CTRL4		0x24
#define SX9324_REG_AFE_CTRL4_FREQ_83_33HZ 0x40
#define SX9324_REG_AFE_CTRL4_RESOLUTION_MASK GENMASK(2, 0)
#define SX9324_REG_AFE_CTRL4_RES_100	0x04
#define SX9324_REG_AFE_CTRL5		0x25
#define SX9324_REG_AFE_CTRL6		0x26
#define SX9324_REG_AFE_CTRL7		0x27
#define SX9324_REG_AFE_PH0		0x28
#define SX9324_REG_AFE_PH0_PIN_MASK(_pin) \
	GENMASK(2 * (_pin) + 1, 2 * (_pin))

#define SX9324_REG_AFE_PH1		0x29
#define SX9324_REG_AFE_PH2		0x2a
#define SX9324_REG_AFE_PH3		0x2b
#define SX9324_REG_AFE_CTRL8		0x2c
#define SX9324_REG_AFE_CTRL8_RESERVED	0x10
#define SX9324_REG_AFE_CTRL8_RESFILTIN_4KOHM 0x02
#define SX9324_REG_AFE_CTRL8_RESFILTIN_MASK GENMASK(3, 0)
#define SX9324_REG_AFE_CTRL9		0x2d
#define SX9324_REG_AFE_CTRL9_AGAIN_MASK			GENMASK(3, 0)
#define SX9324_REG_AFE_CTRL9_AGAIN_1	0x08

#define SX9324_REG_PROX_CTRL0		0x30
#define SX9324_REG_PROX_CTRL0_GAIN_MASK	GENMASK(5, 3)
#define SX9324_REG_PROX_CTRL0_GAIN_SHIFT	3
#define SX9324_REG_PROX_CTRL0_GAIN_RSVD		0x0
#define SX9324_REG_PROX_CTRL0_GAIN_1		0x1
#define SX9324_REG_PROX_CTRL0_GAIN_8		0x4
#define SX9324_REG_PROX_CTRL0_RAWFILT_MASK	GENMASK(2, 0)
#define SX9324_REG_PROX_CTRL0_RAWFILT_1P50	0x01
#define SX9324_REG_PROX_CTRL1		0x31
#define SX9324_REG_PROX_CTRL2		0x32
#define SX9324_REG_PROX_CTRL2_AVGNEG_THRESH_16K 0x20
#define SX9324_REG_PROX_CTRL3		0x33
#define SX9324_REG_PROX_CTRL3_AVGDEB_2SAMPLES	0x40
#define SX9324_REG_PROX_CTRL3_AVGPOS_THRESH_16K 0x20
#define SX9324_REG_PROX_CTRL4		0x34
#define SX9324_REG_PROX_CTRL4_AVGNEGFILT_MASK	GENMASK(5, 3)
#define SX9324_REG_PROX_CTRL4_AVGNEG_FILT_2 0x08
#define SX9324_REG_PROX_CTRL4_AVGPOSFILT_MASK	GENMASK(2, 0)
#define SX9324_REG_PROX_CTRL4_AVGPOS_FILT_256 0x04
#define SX9324_REG_PROX_CTRL5		0x35
#define SX9324_REG_PROX_CTRL5_HYST_MASK			GENMASK(5, 4)
#define SX9324_REG_PROX_CTRL5_CLOSE_DEBOUNCE_MASK	GENMASK(3, 2)
#define SX9324_REG_PROX_CTRL5_FAR_DEBOUNCE_MASK		GENMASK(1, 0)
#define SX9324_REG_PROX_CTRL6		0x36
#define SX9324_REG_PROX_CTRL6_PROXTHRESH_32	0x08
#define SX9324_REG_PROX_CTRL7		0x37

#define SX9324_REG_ADV_CTRL0		0x40
#define SX9324_REG_ADV_CTRL1		0x41
#define SX9324_REG_ADV_CTRL2		0x42
#define SX9324_REG_ADV_CTRL3		0x43
#define SX9324_REG_ADV_CTRL4		0x44
#define SX9324_REG_ADV_CTRL5		0x45
#define SX9324_REG_ADV_CTRL5_STARTUPSENS_MASK GENMASK(3, 2)
#define SX9324_REG_ADV_CTRL5_STARTUP_SENSOR_1	0x04
#define SX9324_REG_ADV_CTRL5_STARTUP_METHOD_1	0x01
#define SX9324_REG_ADV_CTRL6		0x46
#define SX9324_REG_ADV_CTRL7		0x47
#define SX9324_REG_ADV_CTRL8		0x48
#define SX9324_REG_ADV_CTRL9		0x49
#define SX9324_REG_ADV_CTRL10		0x4a
#define SX9324_REG_ADV_CTRL11		0x4b
#define SX9324_REG_ADV_CTRL12		0x4c
#define SX9324_REG_ADV_CTRL13		0x4d
#define SX9324_REG_ADV_CTRL14		0x4e
#define SX9324_REG_ADV_CTRL15		0x4f
#define SX9324_REG_ADV_CTRL16		0x50
#define SX9324_REG_ADV_CTRL17		0x51
#define SX9324_REG_ADV_CTRL18		0x52
#define SX9324_REG_ADV_CTRL19		0x53
#define SX9324_REG_ADV_CTRL20		0x54
#define SX9324_REG_ADV_CTRL19_HIGHT_FAILURE_THRESH_SATURATION 0xf0

#define SX9324_REG_PHASE_SEL		0x60

#define SX9324_REG_USEFUL_MSB		0x61
#define SX9324_REG_USEFUL_LSB		0x62

#define SX9324_REG_AVG_MSB		0x63
#define SX9324_REG_AVG_LSB		0x64

#define SX9324_REG_DIFF_MSB		0x65
#define SX9324_REG_DIFF_LSB		0x66

#define SX9324_REG_OFFSET_MSB		0x67
#define SX9324_REG_OFFSET_LSB		0x68

#define SX9324_REG_SAR_MSB		0x69
#define SX9324_REG_SAR_LSB		0x6a

#define SX9324_REG_RESET		0x9f
/* Write this to REG_RESET to do a soft reset. */
#define SX9324_SOFT_RESET		0xde

#define SX9324_REG_WHOAMI		0xfa
#define   SX9324_WHOAMI_VALUE		0x23

#define SX9324_REG_REVISION		0xfe

/* 4 channels, as defined in STAT0: PH0, PH1, PH2 and PH3. */
#define SX9324_NUM_CHANNELS		4
/* 3 CS pins: CS0, CS1, CS2. */
#define SX9324_NUM_PINS			3

static const char * const sx9324_cs_pin_usage[] = { "HZ", "MI", "DS", "GD" };

static ssize_t sx9324_phase_configuration_show(struct iio_dev *indio_dev,
					       uintptr_t private,
					       const struct iio_chan_spec *chan,
					       char *buf)
{
	struct sx_common_data *data = iio_priv(indio_dev);
	unsigned int val;
	int i, ret, pin_idx;
	size_t len = 0;

	ret = regmap_read(data->regmap, SX9324_REG_AFE_PH0 + chan->channel, &val);
	if (ret < 0)
		return ret;

	for (i = 0; i < SX9324_NUM_PINS; i++) {
		pin_idx = (val & SX9324_REG_AFE_PH0_PIN_MASK(i)) >> (2 * i);
		len += sysfs_emit_at(buf, len, "%s,",
				     sx9324_cs_pin_usage[pin_idx]);
	}
	buf[len - 1] = '\n';
	return len;
}

static const struct iio_chan_spec_ext_info sx9324_channel_ext_info[] = {
	{
		.name = "setup",
		.shared = IIO_SEPARATE,
		.read = sx9324_phase_configuration_show,
	},
	{}
};

#define SX9324_CHANNEL(idx)					 \
{								 \
	.type = IIO_PROXIMITY,					 \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		 \
			      BIT(IIO_CHAN_INFO_HARDWAREGAIN),	 \
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ), \
	.info_mask_separate_available =				 \
		BIT(IIO_CHAN_INFO_HARDWAREGAIN),		 \
	.info_mask_shared_by_all_available =			 \
		BIT(IIO_CHAN_INFO_SAMP_FREQ),			 \
	.indexed = 1,						 \
	.channel = idx,						 \
	.address = SX9324_REG_DIFF_MSB,				 \
	.event_spec = sx_common_events,				 \
	.num_event_specs = ARRAY_SIZE(sx_common_events),	 \
	.scan_index = idx,					 \
	.scan_type = {						 \
		.sign = 's',					 \
		.realbits = 12,					 \
		.storagebits = 16,				 \
		.endianness = IIO_BE,				 \
	},							 \
	.ext_info = sx9324_channel_ext_info,			 \
}

static const struct iio_chan_spec sx9324_channels[] = {
	SX9324_CHANNEL(0),			/* Phase 0 */
	SX9324_CHANNEL(1),			/* Phase 1 */
	SX9324_CHANNEL(2),			/* Phase 2 */
	SX9324_CHANNEL(3),			/* Phase 3 */
	IIO_CHAN_SOFT_TIMESTAMP(4),
};

/*
 * Each entry contains the integer part (val) and the fractional part, in micro
 * seconds. It conforms to the IIO output IIO_VAL_INT_PLUS_MICRO.
 */
static const struct {
	int val;
	int val2;
} sx9324_samp_freq_table[] = {
	{ 1000, 0 },  /* 00000: Min (no idle time) */
	{ 500, 0 },  /* 00001: 2 ms */
	{ 250, 0 },  /* 00010: 4 ms */
	{ 166, 666666 },  /* 00011: 6 ms */
	{ 125, 0 },  /* 00100: 8 ms */
	{ 100, 0 },  /* 00101: 10 ms */
	{ 71, 428571 },  /* 00110: 14 ms */
	{ 55, 555556 },  /* 00111: 18 ms */
	{ 45, 454545 },  /* 01000: 22 ms */
	{ 38, 461538 },  /* 01001: 26 ms */
	{ 33, 333333 },  /* 01010: 30 ms */
	{ 29, 411765 },  /* 01011: 34 ms */
	{ 26, 315789 },  /* 01100: 38 ms */
	{ 23, 809524 },  /* 01101: 42 ms */
	{ 21, 739130 },  /* 01110: 46 ms */
	{ 20, 0 },  /* 01111: 50 ms */
	{ 17, 857143 },  /* 10000: 56 ms */
	{ 16, 129032 },  /* 10001: 62 ms */
	{ 14, 705882 },  /* 10010: 68 ms */
	{ 13, 513514 },  /* 10011: 74 ms */
	{ 12, 500000 },  /* 10100: 80 ms */
	{ 11, 111111 },  /* 10101: 90 ms */
	{ 10, 0 },  /* 10110: 100 ms (Typ.) */
	{ 5, 0 },  /* 10111: 200 ms */
	{ 3, 333333 },  /* 11000: 300 ms */
	{ 2, 500000 },  /* 11001: 400 ms */
	{ 1, 666667 },  /* 11010: 600 ms */
	{ 1, 250000 },  /* 11011: 800 ms */
	{ 1, 0 },  /* 11100: 1 s */
	{ 0, 500000 },  /* 11101: 2 s */
	{ 0, 333333 },  /* 11110: 3 s */
	{ 0, 250000 },  /* 11111: 4 s */
};

static const unsigned int sx9324_scan_period_table[] = {
	2,   15,  30,  45,   60,   90,	 120,  200,
	400, 600, 800, 1000, 2000, 3000, 4000, 5000,
};

static const struct regmap_range sx9324_writable_reg_ranges[] = {
	/*
	 * To set COMPSTAT for compensation, even if datasheet says register is
	 * RO.
	 */
	regmap_reg_range(SX9324_REG_STAT2, SX9324_REG_STAT2),
	regmap_reg_range(SX9324_REG_IRQ_MSK, SX9324_REG_IRQ_CFG2),
	regmap_reg_range(SX9324_REG_GNRL_CTRL0, SX9324_REG_GNRL_CTRL1),
	/* Leave i2c and clock spreading as unavailable */
	regmap_reg_range(SX9324_REG_AFE_CTRL0, SX9324_REG_AFE_CTRL9),
	regmap_reg_range(SX9324_REG_PROX_CTRL0, SX9324_REG_PROX_CTRL7),
	regmap_reg_range(SX9324_REG_ADV_CTRL0, SX9324_REG_ADV_CTRL20),
	regmap_reg_range(SX9324_REG_PHASE_SEL, SX9324_REG_PHASE_SEL),
	regmap_reg_range(SX9324_REG_OFFSET_MSB, SX9324_REG_OFFSET_LSB),
	regmap_reg_range(SX9324_REG_RESET, SX9324_REG_RESET),
};

static const struct regmap_access_table sx9324_writeable_regs = {
	.yes_ranges = sx9324_writable_reg_ranges,
	.n_yes_ranges = ARRAY_SIZE(sx9324_writable_reg_ranges),
};

/*
 * All allocated registers are readable, so we just list unallocated
 * ones.
 */
static const struct regmap_range sx9324_non_readable_reg_ranges[] = {
	regmap_reg_range(SX9324_REG_IRQ_CFG2 + 1, SX9324_REG_GNRL_CTRL0 - 1),
	regmap_reg_range(SX9324_REG_GNRL_CTRL1 + 1, SX9324_REG_AFE_CTRL0 - 1),
	regmap_reg_range(SX9324_REG_AFE_CTRL9 + 1, SX9324_REG_PROX_CTRL0 - 1),
	regmap_reg_range(SX9324_REG_PROX_CTRL7 + 1, SX9324_REG_ADV_CTRL0 - 1),
	regmap_reg_range(SX9324_REG_ADV_CTRL20 + 1, SX9324_REG_PHASE_SEL - 1),
	regmap_reg_range(SX9324_REG_SAR_LSB + 1, SX9324_REG_RESET - 1),
	regmap_reg_range(SX9324_REG_RESET + 1, SX9324_REG_WHOAMI - 1),
	regmap_reg_range(SX9324_REG_WHOAMI + 1, SX9324_REG_REVISION - 1),
};

static const struct regmap_access_table sx9324_readable_regs = {
	.no_ranges = sx9324_non_readable_reg_ranges,
	.n_no_ranges = ARRAY_SIZE(sx9324_non_readable_reg_ranges),
};

static const struct regmap_range sx9324_volatile_reg_ranges[] = {
	regmap_reg_range(SX9324_REG_IRQ_SRC, SX9324_REG_STAT3),
	regmap_reg_range(SX9324_REG_USEFUL_MSB, SX9324_REG_DIFF_LSB),
	regmap_reg_range(SX9324_REG_SAR_MSB, SX9324_REG_SAR_LSB),
	regmap_reg_range(SX9324_REG_WHOAMI, SX9324_REG_WHOAMI),
	regmap_reg_range(SX9324_REG_REVISION, SX9324_REG_REVISION),
};

static const struct regmap_access_table sx9324_volatile_regs = {
	.yes_ranges = sx9324_volatile_reg_ranges,
	.n_yes_ranges = ARRAY_SIZE(sx9324_volatile_reg_ranges),
};

static const struct regmap_config sx9324_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = SX9324_REG_REVISION,
	.cache_type = REGCACHE_RBTREE,

	.wr_table = &sx9324_writeable_regs,
	.rd_table = &sx9324_readable_regs,
	.volatile_table = &sx9324_volatile_regs,
};

static int sx9324_read_prox_data(struct sx_common_data *data,
				 const struct iio_chan_spec *chan,
				 __be16 *val)
{
	int ret;

	ret = regmap_write(data->regmap, SX9324_REG_PHASE_SEL, chan->channel);
	if (ret < 0)
		return ret;

	return regmap_bulk_read(data->regmap, chan->address, val, sizeof(*val));
}

/*
 * If we have no interrupt support, we have to wait for a scan period
 * after enabling a channel to get a result.
 */
static int sx9324_wait_for_sample(struct sx_common_data *data)
{
	int ret;
	unsigned int val;

	ret = regmap_read(data->regmap, SX9324_REG_GNRL_CTRL0, &val);
	if (ret < 0)
		return ret;
	val = FIELD_GET(SX9324_REG_GNRL_CTRL0_SCANPERIOD_MASK, val);

	msleep(sx9324_scan_period_table[val]);

	return 0;
}

static int sx9324_read_gain(struct sx_common_data *data,
			    const struct iio_chan_spec *chan, int *val)
{
	unsigned int reg, regval;
	int ret;

	reg = SX9324_REG_PROX_CTRL0 + chan->channel / 2;
	ret = regmap_read(data->regmap, reg, &regval);
	if (ret)
		return ret;

	regval = FIELD_GET(SX9324_REG_PROX_CTRL0_GAIN_MASK, regval);
	if (regval)
		regval--;
	else if (regval == SX9324_REG_PROX_CTRL0_GAIN_RSVD ||
		 regval > SX9324_REG_PROX_CTRL0_GAIN_8)
		return -EINVAL;

	*val = 1 << regval;

	return IIO_VAL_INT;
}

static int sx9324_read_samp_freq(struct sx_common_data *data,
				 int *val, int *val2)
{
	int ret;
	unsigned int regval;

	ret = regmap_read(data->regmap, SX9324_REG_GNRL_CTRL0, &regval);
	if (ret)
		return ret;

	regval = FIELD_GET(SX9324_REG_GNRL_CTRL0_SCANPERIOD_MASK, regval);
	*val = sx9324_samp_freq_table[regval].val;
	*val2 = sx9324_samp_freq_table[regval].val2;

	return IIO_VAL_INT_PLUS_MICRO;
}

static int sx9324_read_raw(struct iio_dev *indio_dev,
			   const struct iio_chan_spec *chan,
			   int *val, int *val2, long mask)
{
	struct sx_common_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		iio_device_claim_direct_scoped(return -EBUSY, indio_dev)
			return sx_common_read_proximity(data, chan, val);
		unreachable();
	case IIO_CHAN_INFO_HARDWAREGAIN:
		iio_device_claim_direct_scoped(return -EBUSY, indio_dev)
			return sx9324_read_gain(data, chan, val);
		unreachable();
	case IIO_CHAN_INFO_SAMP_FREQ:
		return sx9324_read_samp_freq(data, val, val2);
	default:
		return -EINVAL;
	}
}

static const int sx9324_gain_vals[] = { 1, 2, 4, 8 };

static int sx9324_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     const int **vals, int *type, int *length,
			     long mask)
{
	if (chan->type != IIO_PROXIMITY)
		return -EINVAL;

	switch (mask) {
	case IIO_CHAN_INFO_HARDWAREGAIN:
		*type = IIO_VAL_INT;
		*length = ARRAY_SIZE(sx9324_gain_vals);
		*vals = sx9324_gain_vals;
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*type = IIO_VAL_INT_PLUS_MICRO;
		*length = ARRAY_SIZE(sx9324_samp_freq_table) * 2;
		*vals = (int *)sx9324_samp_freq_table;
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static int sx9324_set_samp_freq(struct sx_common_data *data,
				int val, int val2)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sx9324_samp_freq_table); i++)
		if (val == sx9324_samp_freq_table[i].val &&
		    val2 == sx9324_samp_freq_table[i].val2)
			break;

	if (i == ARRAY_SIZE(sx9324_samp_freq_table))
		return -EINVAL;

	guard(mutex)(&data->mutex);

	return regmap_update_bits(data->regmap,
				  SX9324_REG_GNRL_CTRL0,
				  SX9324_REG_GNRL_CTRL0_SCANPERIOD_MASK, i);
}

static int sx9324_read_thresh(struct sx_common_data *data,
			      const struct iio_chan_spec *chan, int *val)
{
	unsigned int regval;
	unsigned int reg;
	int ret;

	/*
	 * TODO(gwendal): Depending on the phase function
	 * (proximity/table/body), retrieve the right threshold.
	 * For now, return the proximity threshold.
	 */
	reg = SX9324_REG_PROX_CTRL6 + chan->channel / 2;
	ret = regmap_read(data->regmap, reg, &regval);
	if (ret)
		return ret;

	if (regval <= 1)
		*val = regval;
	else
		*val = (regval * regval) / 2;

	return IIO_VAL_INT;
}

static int sx9324_read_hysteresis(struct sx_common_data *data,
				  const struct iio_chan_spec *chan, int *val)
{
	unsigned int regval, pthresh;
	int ret;

	ret = sx9324_read_thresh(data, chan, &pthresh);
	if (ret < 0)
		return ret;

	ret = regmap_read(data->regmap, SX9324_REG_PROX_CTRL5, &regval);
	if (ret)
		return ret;

	regval = FIELD_GET(SX9324_REG_PROX_CTRL5_HYST_MASK, regval);
	if (!regval)
		*val = 0;
	else
		*val = pthresh >> (5 - regval);

	return IIO_VAL_INT;
}

static int sx9324_read_far_debounce(struct sx_common_data *data, int *val)
{
	unsigned int regval;
	int ret;

	ret = regmap_read(data->regmap, SX9324_REG_PROX_CTRL5, &regval);
	if (ret)
		return ret;

	regval = FIELD_GET(SX9324_REG_PROX_CTRL5_FAR_DEBOUNCE_MASK, regval);
	if (regval)
		*val = 1 << regval;
	else
		*val = 0;

	return IIO_VAL_INT;
}

static int sx9324_read_close_debounce(struct sx_common_data *data, int *val)
{
	unsigned int regval;
	int ret;

	ret = regmap_read(data->regmap, SX9324_REG_PROX_CTRL5, &regval);
	if (ret)
		return ret;

	regval = FIELD_GET(SX9324_REG_PROX_CTRL5_CLOSE_DEBOUNCE_MASK, regval);
	if (regval)
		*val = 1 << regval;
	else
		*val = 0;

	return IIO_VAL_INT;
}

static int sx9324_read_event_val(struct iio_dev *indio_dev,
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
		return sx9324_read_thresh(data, chan, val);
	case IIO_EV_INFO_PERIOD:
		switch (dir) {
		case IIO_EV_DIR_RISING:
			return sx9324_read_far_debounce(data, val);
		case IIO_EV_DIR_FALLING:
			return sx9324_read_close_debounce(data, val);
		default:
			return -EINVAL;
		}
	case IIO_EV_INFO_HYSTERESIS:
		return sx9324_read_hysteresis(data, chan, val);
	default:
		return -EINVAL;
	}
}

static int sx9324_write_thresh(struct sx_common_data *data,
			       const struct iio_chan_spec *chan, int _val)
{
	unsigned int reg, val = _val;

	reg = SX9324_REG_PROX_CTRL6 + chan->channel / 2;

	if (val >= 1)
		val = int_sqrt(2 * val);

	if (val > 0xff)
		return -EINVAL;

	guard(mutex)(&data->mutex);

	return regmap_write(data->regmap, reg, val);
}

static int sx9324_write_hysteresis(struct sx_common_data *data,
				   const struct iio_chan_spec *chan, int _val)
{
	unsigned int hyst, val = _val;
	int ret, pthresh;

	ret = sx9324_read_thresh(data, chan, &pthresh);
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

	hyst = FIELD_PREP(SX9324_REG_PROX_CTRL5_HYST_MASK, hyst);
	guard(mutex)(&data->mutex);

	return regmap_update_bits(data->regmap, SX9324_REG_PROX_CTRL5,
				  SX9324_REG_PROX_CTRL5_HYST_MASK, hyst);
}

static int sx9324_write_far_debounce(struct sx_common_data *data, int _val)
{
	unsigned int regval, val = _val;

	if (val > 0)
		val = ilog2(val);
	if (!FIELD_FIT(SX9324_REG_PROX_CTRL5_FAR_DEBOUNCE_MASK, val))
		return -EINVAL;

	regval = FIELD_PREP(SX9324_REG_PROX_CTRL5_FAR_DEBOUNCE_MASK, val);

	guard(mutex)(&data->mutex);

	return regmap_update_bits(data->regmap, SX9324_REG_PROX_CTRL5,
				  SX9324_REG_PROX_CTRL5_FAR_DEBOUNCE_MASK,
				  regval);
}

static int sx9324_write_close_debounce(struct sx_common_data *data, int _val)
{
	unsigned int regval, val = _val;

	if (val > 0)
		val = ilog2(val);
	if (!FIELD_FIT(SX9324_REG_PROX_CTRL5_CLOSE_DEBOUNCE_MASK, val))
		return -EINVAL;

	regval = FIELD_PREP(SX9324_REG_PROX_CTRL5_CLOSE_DEBOUNCE_MASK, val);

	guard(mutex)(&data->mutex);

	return regmap_update_bits(data->regmap, SX9324_REG_PROX_CTRL5,
				  SX9324_REG_PROX_CTRL5_CLOSE_DEBOUNCE_MASK,
				  regval);
}

static int sx9324_write_event_val(struct iio_dev *indio_dev,
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
		return sx9324_write_thresh(data, chan, val);
	case IIO_EV_INFO_PERIOD:
		switch (dir) {
		case IIO_EV_DIR_RISING:
			return sx9324_write_far_debounce(data, val);
		case IIO_EV_DIR_FALLING:
			return sx9324_write_close_debounce(data, val);
		default:
			return -EINVAL;
		}
	case IIO_EV_INFO_HYSTERESIS:
		return sx9324_write_hysteresis(data, chan, val);
	default:
		return -EINVAL;
	}
}

static int sx9324_write_gain(struct sx_common_data *data,
			     const struct iio_chan_spec *chan, int val)
{
	unsigned int gain, reg;

	reg = SX9324_REG_PROX_CTRL0 + chan->channel / 2;

	gain = ilog2(val) + 1;
	if (val <= 0 || gain > SX9324_REG_PROX_CTRL0_GAIN_8)
		return -EINVAL;

	gain = FIELD_PREP(SX9324_REG_PROX_CTRL0_GAIN_MASK, gain);

	guard(mutex)(&data->mutex);

	return regmap_update_bits(data->regmap, reg,
				  SX9324_REG_PROX_CTRL0_GAIN_MASK,
				  gain);
}

static int sx9324_write_raw(struct iio_dev *indio_dev,
			    const struct iio_chan_spec *chan, int val, int val2,
			    long mask)
{
	struct sx_common_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		return sx9324_set_samp_freq(data, val, val2);
	case IIO_CHAN_INFO_HARDWAREGAIN:
		return sx9324_write_gain(data, chan, val);
	default:
		return -EINVAL;
	}
}

static const struct sx_common_reg_default sx9324_default_regs[] = {
	{ SX9324_REG_IRQ_MSK, 0x00 },
	{ SX9324_REG_IRQ_CFG0, 0x00, "irq_cfg0" },
	{ SX9324_REG_IRQ_CFG1, SX9324_REG_IRQ_CFG1_FAILCOND, "irq_cfg1" },
	{ SX9324_REG_IRQ_CFG2, 0x00, "irq_cfg2" },
	{ SX9324_REG_GNRL_CTRL0, SX9324_REG_GNRL_CTRL0_SCANPERIOD_100MS, "gnrl_ctrl0" },
	/*
	 * The lower 4 bits should not be set as it enable sensors measurements.
	 * Turning the detection on before the configuration values are set to
	 * good values can cause the device to return erroneous readings.
	 */
	{ SX9324_REG_GNRL_CTRL1, SX9324_REG_GNRL_CTRL1_PAUSECTRL, "gnrl_ctrl1" },

	{ SX9324_REG_AFE_CTRL0, SX9324_REG_AFE_CTRL0_RINT_LOWEST, "afe_ctrl0" },
	{ SX9324_REG_AFE_CTRL3, 0x00, "afe_ctrl3" },
	{ SX9324_REG_AFE_CTRL4, SX9324_REG_AFE_CTRL4_FREQ_83_33HZ |
		SX9324_REG_AFE_CTRL4_RES_100, "afe_ctrl4" },
	{ SX9324_REG_AFE_CTRL6, 0x00, "afe_ctrl6" },
	{ SX9324_REG_AFE_CTRL7, SX9324_REG_AFE_CTRL4_FREQ_83_33HZ |
		SX9324_REG_AFE_CTRL4_RES_100, "afe_ctrl7" },

	/* TODO(gwendal): PHx use chip default or all grounded? */
	{ SX9324_REG_AFE_PH0, 0x29, "afe_ph0" },
	{ SX9324_REG_AFE_PH1, 0x26, "afe_ph1" },
	{ SX9324_REG_AFE_PH2, 0x1a, "afe_ph2" },
	{ SX9324_REG_AFE_PH3, 0x16, "afe_ph3" },

	{ SX9324_REG_AFE_CTRL8, SX9324_REG_AFE_CTRL8_RESERVED |
		SX9324_REG_AFE_CTRL8_RESFILTIN_4KOHM, "afe_ctrl8" },
	{ SX9324_REG_AFE_CTRL9, SX9324_REG_AFE_CTRL9_AGAIN_1, "afe_ctrl9" },

	{ SX9324_REG_PROX_CTRL0,
		SX9324_REG_PROX_CTRL0_GAIN_1 << SX9324_REG_PROX_CTRL0_GAIN_SHIFT |
		SX9324_REG_PROX_CTRL0_RAWFILT_1P50, "prox_ctrl0" },
	{ SX9324_REG_PROX_CTRL1,
		SX9324_REG_PROX_CTRL0_GAIN_1 << SX9324_REG_PROX_CTRL0_GAIN_SHIFT |
		SX9324_REG_PROX_CTRL0_RAWFILT_1P50, "prox_ctrl1" },
	{ SX9324_REG_PROX_CTRL2, SX9324_REG_PROX_CTRL2_AVGNEG_THRESH_16K, "prox_ctrl2" },
	{ SX9324_REG_PROX_CTRL3, SX9324_REG_PROX_CTRL3_AVGDEB_2SAMPLES |
		SX9324_REG_PROX_CTRL3_AVGPOS_THRESH_16K, "prox_ctrl3" },
	{ SX9324_REG_PROX_CTRL4, SX9324_REG_PROX_CTRL4_AVGNEG_FILT_2 |
		SX9324_REG_PROX_CTRL4_AVGPOS_FILT_256, "prox_ctrl4" },
	{ SX9324_REG_PROX_CTRL5, 0x00, "prox_ctrl5" },
	{ SX9324_REG_PROX_CTRL6, SX9324_REG_PROX_CTRL6_PROXTHRESH_32, "prox_ctrl6" },
	{ SX9324_REG_PROX_CTRL7, SX9324_REG_PROX_CTRL6_PROXTHRESH_32, "prox_ctrl7" },
	{ SX9324_REG_ADV_CTRL0, 0x00, "adv_ctrl0" },
	{ SX9324_REG_ADV_CTRL1, 0x00, "adv_ctrl1" },
	{ SX9324_REG_ADV_CTRL2, 0x00, "adv_ctrl2" },
	{ SX9324_REG_ADV_CTRL3, 0x00, "adv_ctrl3" },
	{ SX9324_REG_ADV_CTRL4, 0x00, "adv_ctrl4" },
	{ SX9324_REG_ADV_CTRL5, SX9324_REG_ADV_CTRL5_STARTUP_SENSOR_1 |
		SX9324_REG_ADV_CTRL5_STARTUP_METHOD_1, "adv_ctrl5" },
	{ SX9324_REG_ADV_CTRL6, 0x00, "adv_ctrl6" },
	{ SX9324_REG_ADV_CTRL7, 0x00, "adv_ctrl7" },
	{ SX9324_REG_ADV_CTRL8, 0x00, "adv_ctrl8" },
	{ SX9324_REG_ADV_CTRL9, 0x00, "adv_ctrl9" },
	/* Body/Table threshold */
	{ SX9324_REG_ADV_CTRL10, 0x00, "adv_ctrl10" },
	{ SX9324_REG_ADV_CTRL11, 0x00, "adv_ctrl11" },
	{ SX9324_REG_ADV_CTRL12, 0x00, "adv_ctrl12" },
	/* TODO(gwendal): SAR currenly disabled */
	{ SX9324_REG_ADV_CTRL13, 0x00, "adv_ctrl13" },
	{ SX9324_REG_ADV_CTRL14, 0x00, "adv_ctrl14" },
	{ SX9324_REG_ADV_CTRL15, 0x00, "adv_ctrl15" },
	{ SX9324_REG_ADV_CTRL16, 0x00, "adv_ctrl16" },
	{ SX9324_REG_ADV_CTRL17, 0x00, "adv_ctrl17" },
	{ SX9324_REG_ADV_CTRL18, 0x00, "adv_ctrl18" },
	{ SX9324_REG_ADV_CTRL19,
		SX9324_REG_ADV_CTRL19_HIGHT_FAILURE_THRESH_SATURATION, "adv_ctrl19" },
	{ SX9324_REG_ADV_CTRL20,
		SX9324_REG_ADV_CTRL19_HIGHT_FAILURE_THRESH_SATURATION, "adv_ctrl20" },
};

/* Activate all channels and perform an initial compensation. */
static int sx9324_init_compensation(struct iio_dev *indio_dev)
{
	struct sx_common_data *data = iio_priv(indio_dev);
	unsigned int val;
	int ret;

	/* run the compensation phase on all channels */
	ret = regmap_set_bits(data->regmap, SX9324_REG_STAT2,
			      SX9324_REG_STAT2_COMPSTAT_MASK);
	if (ret)
		return ret;

	return regmap_read_poll_timeout(data->regmap, SX9324_REG_STAT2, val,
					!(val & SX9324_REG_STAT2_COMPSTAT_MASK),
					20000, 2000000);
}

static u8 sx9324_parse_phase_prop(struct device *dev,
				  struct sx_common_reg_default *reg_def,
				  const char *prop)
{
	unsigned int pin_defs[SX9324_NUM_PINS];
	int count, ret, pin;
	u32 raw = 0;

	count = device_property_count_u32(dev, prop);
	if (count != ARRAY_SIZE(pin_defs))
		return reg_def->def;
	ret = device_property_read_u32_array(dev, prop, pin_defs,
					     ARRAY_SIZE(pin_defs));
	if (ret)
		return reg_def->def;

	for (pin = 0; pin < SX9324_NUM_PINS; pin++)
		raw |= (pin_defs[pin] << (2 * pin)) &
		       SX9324_REG_AFE_PH0_PIN_MASK(pin);

	return raw;
}

static const struct sx_common_reg_default *
sx9324_get_default_reg(struct device *dev, int idx,
		       struct sx_common_reg_default *reg_def)
{
	static const char * const sx9324_rints[] = { "lowest", "low", "high",
		"highest" };
	static const char * const sx9324_csidle[] = { "hi-z", "hi-z", "gnd",
		"vdd" };
	u32 start = 0, raw = 0, pos = 0;
	const char *prop;
	int ret;

	memcpy(reg_def, &sx9324_default_regs[idx], sizeof(*reg_def));

	sx_common_get_raw_register_config(dev, reg_def);
	switch (reg_def->reg) {
	case SX9324_REG_AFE_PH0:
		reg_def->def = sx9324_parse_phase_prop(dev, reg_def,
						       "semtech,ph0-pin");
		break;
	case SX9324_REG_AFE_PH1:
		reg_def->def = sx9324_parse_phase_prop(dev, reg_def,
						       "semtech,ph1-pin");
		break;
	case SX9324_REG_AFE_PH2:
		reg_def->def = sx9324_parse_phase_prop(dev, reg_def,
						       "semtech,ph2-pin");
		break;
	case SX9324_REG_AFE_PH3:
		reg_def->def = sx9324_parse_phase_prop(dev, reg_def,
						       "semtech,ph3-pin");
		break;
	case SX9324_REG_AFE_CTRL0:
		ret = device_property_match_property_string(dev, "semtech,cs-idle-sleep",
							    sx9324_csidle,
							    ARRAY_SIZE(sx9324_csidle));
		if (ret >= 0) {
			reg_def->def &= ~SX9324_REG_AFE_CTRL0_CSIDLE_MASK;
			reg_def->def |= ret << SX9324_REG_AFE_CTRL0_CSIDLE_SHIFT;
		}

		ret = device_property_match_property_string(dev, "semtech,int-comp-resistor",
							    sx9324_rints,
							    ARRAY_SIZE(sx9324_rints));
		if (ret >= 0) {
			reg_def->def &= ~SX9324_REG_AFE_CTRL0_RINT_MASK;
			reg_def->def |= ret << SX9324_REG_AFE_CTRL0_RINT_SHIFT;
		}
		break;
	case SX9324_REG_AFE_CTRL4:
	case SX9324_REG_AFE_CTRL7:
		if (reg_def->reg == SX9324_REG_AFE_CTRL4)
			prop = "semtech,ph01-resolution";
		else
			prop = "semtech,ph23-resolution";

		ret = device_property_read_u32(dev, prop, &raw);
		if (ret)
			break;

		raw = ilog2(raw) - 3;

		reg_def->def &= ~SX9324_REG_AFE_CTRL4_RESOLUTION_MASK;
		reg_def->def |= FIELD_PREP(SX9324_REG_AFE_CTRL4_RESOLUTION_MASK,
					   raw);
		break;
	case SX9324_REG_AFE_CTRL8:
		ret = device_property_read_u32(dev,
				"semtech,input-precharge-resistor-ohms",
				&raw);
		if (ret)
			break;

		reg_def->def &= ~SX9324_REG_AFE_CTRL8_RESFILTIN_MASK;
		reg_def->def |= FIELD_PREP(SX9324_REG_AFE_CTRL8_RESFILTIN_MASK,
					   raw / 2000);
		break;

	case SX9324_REG_AFE_CTRL9:
		ret = device_property_read_u32(dev,
				"semtech,input-analog-gain", &raw);
		if (ret)
			break;
		/*
		 * The analog gain has the following setting:
		 * +---------+----------------+----------------+
		 * | dt(raw) | physical value | register value |
		 * +---------+----------------+----------------+
		 * |  0      |      x1.247    |      6         |
		 * |  1      |      x1        |      8         |
		 * |  2      |      x0.768    |     11         |
		 * |  3      |      x0.552    |     15         |
		 * +---------+----------------+----------------+
		 */
		reg_def->def &= ~SX9324_REG_AFE_CTRL9_AGAIN_MASK;
		reg_def->def |= FIELD_PREP(SX9324_REG_AFE_CTRL9_AGAIN_MASK,
					   6 + raw * (raw + 3) / 2);
		break;

	case SX9324_REG_ADV_CTRL5:
		ret = device_property_read_u32(dev, "semtech,startup-sensor",
					       &start);
		if (ret)
			break;

		reg_def->def &= ~SX9324_REG_ADV_CTRL5_STARTUPSENS_MASK;
		reg_def->def |= FIELD_PREP(SX9324_REG_ADV_CTRL5_STARTUPSENS_MASK,
					   start);
		break;
	case SX9324_REG_PROX_CTRL4:
		ret = device_property_read_u32(dev, "semtech,avg-pos-strength",
					       &pos);
		if (ret)
			break;

		/* Powers of 2, except for a gap between 16 and 64 */
		raw = clamp(ilog2(pos), 3, 11) - (pos >= 32 ? 4 : 3);

		reg_def->def &= ~SX9324_REG_PROX_CTRL4_AVGPOSFILT_MASK;
		reg_def->def |= FIELD_PREP(SX9324_REG_PROX_CTRL4_AVGPOSFILT_MASK,
					   raw);
		break;
	case SX9324_REG_PROX_CTRL0:
	case SX9324_REG_PROX_CTRL1:
		if (reg_def->reg == SX9324_REG_PROX_CTRL0)
			prop = "semtech,ph01-proxraw-strength";
		else
			prop = "semtech,ph23-proxraw-strength";
		ret = device_property_read_u32(dev, prop, &raw);
		if (ret)
			break;

		reg_def->def &= ~SX9324_REG_PROX_CTRL0_RAWFILT_MASK;
		reg_def->def |= FIELD_PREP(SX9324_REG_PROX_CTRL0_RAWFILT_MASK,
					   raw);
		break;
	}
	return reg_def;
}

static int sx9324_check_whoami(struct device *dev,
			       struct iio_dev *indio_dev)
{
	/*
	 * Only one sensor for this driver. Assuming the device tree
	 * is correct, just set the sensor name.
	 */
	indio_dev->name = "sx9324";
	return 0;
}

static const struct sx_common_chip_info sx9324_chip_info = {
	.reg_stat = SX9324_REG_STAT0,
	.reg_irq_msk = SX9324_REG_IRQ_MSK,
	.reg_enable_chan = SX9324_REG_GNRL_CTRL1,
	.reg_reset = SX9324_REG_RESET,

	.mask_enable_chan = SX9324_REG_GNRL_CTRL1_PHEN_MASK,
	.irq_msk_offset = 3,
	.num_channels = SX9324_NUM_CHANNELS,
	.num_default_regs = ARRAY_SIZE(sx9324_default_regs),

	.ops = {
		.read_prox_data = sx9324_read_prox_data,
		.check_whoami = sx9324_check_whoami,
		.init_compensation = sx9324_init_compensation,
		.wait_for_sample = sx9324_wait_for_sample,
		.get_default_reg = sx9324_get_default_reg,
	},

	.iio_channels = sx9324_channels,
	.num_iio_channels = ARRAY_SIZE(sx9324_channels),
	.iio_info =  {
		.read_raw = sx9324_read_raw,
		.read_avail = sx9324_read_avail,
		.read_event_value = sx9324_read_event_val,
		.write_event_value = sx9324_write_event_val,
		.write_raw = sx9324_write_raw,
		.read_event_config = sx_common_read_event_config,
		.write_event_config = sx_common_write_event_config,
	},
};

static int sx9324_probe(struct i2c_client *client)
{
	return sx_common_probe(client, &sx9324_chip_info, &sx9324_regmap_config);
}

static int sx9324_suspend(struct device *dev)
{
	struct sx_common_data *data = iio_priv(dev_get_drvdata(dev));
	unsigned int regval;
	int ret;

	disable_irq_nosync(data->client->irq);

	guard(mutex)(&data->mutex);
	ret = regmap_read(data->regmap, SX9324_REG_GNRL_CTRL1, &regval);
	if (ret < 0)
		return ret;

	data->suspend_ctrl =
		FIELD_GET(SX9324_REG_GNRL_CTRL1_PHEN_MASK, regval);


	/* Disable all phases, send the device to sleep. */
	return regmap_write(data->regmap, SX9324_REG_GNRL_CTRL1, 0);
}

static int sx9324_resume(struct device *dev)
{
	struct sx_common_data *data = iio_priv(dev_get_drvdata(dev));

	scoped_guard(mutex, &data->mutex) {
		int ret = regmap_write(data->regmap, SX9324_REG_GNRL_CTRL1,
				       data->suspend_ctrl |
				       SX9324_REG_GNRL_CTRL1_PAUSECTRL);
		if (ret)
			return ret;
	}

	enable_irq(data->client->irq);
	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(sx9324_pm_ops, sx9324_suspend, sx9324_resume);

static const struct acpi_device_id sx9324_acpi_match[] = {
	{ "STH9324", SX9324_WHOAMI_VALUE },
	{ }
};
MODULE_DEVICE_TABLE(acpi, sx9324_acpi_match);

static const struct of_device_id sx9324_of_match[] = {
	{ .compatible = "semtech,sx9324", (void *)SX9324_WHOAMI_VALUE },
	{ }
};
MODULE_DEVICE_TABLE(of, sx9324_of_match);

static const struct i2c_device_id sx9324_id[] = {
	{ "sx9324", SX9324_WHOAMI_VALUE },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sx9324_id);

static struct i2c_driver sx9324_driver = {
	.driver = {
		.name	= "sx9324",
		.acpi_match_table = sx9324_acpi_match,
		.of_match_table = sx9324_of_match,
		.pm = pm_sleep_ptr(&sx9324_pm_ops),

		/*
		 * Lots of i2c transfers in probe + over 200 ms waiting in
		 * sx9324_init_compensation() mean a slow probe; prefer async
		 * so we don't delay boot if we're builtin to the kernel.
		 */
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe		= sx9324_probe,
	.id_table	= sx9324_id,
};
module_i2c_driver(sx9324_driver);

MODULE_AUTHOR("Gwendal Grignou <gwendal@chromium.org>");
MODULE_DESCRIPTION("Driver for Semtech SX9324 proximity sensor");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(SEMTECH_PROX);
