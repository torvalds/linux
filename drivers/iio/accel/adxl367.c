// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2021 Analog Devices, Inc.
 * Author: Cosmin Tanislav <cosmin.tanislav@analog.com>
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/iio/buffer.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/sysfs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mod_devicetable.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <asm/unaligned.h>

#include "adxl367.h"

#define ADXL367_REG_DEVID		0x00
#define ADXL367_DEVID_AD		0xAD

#define ADXL367_REG_STATUS		0x0B
#define ADXL367_STATUS_INACT_MASK	BIT(5)
#define ADXL367_STATUS_ACT_MASK		BIT(4)
#define ADXL367_STATUS_FIFO_FULL_MASK	BIT(2)

#define ADXL367_FIFO_ENT_H_MASK		GENMASK(1, 0)

#define ADXL367_REG_X_DATA_H		0x0E
#define ADXL367_REG_Y_DATA_H		0x10
#define ADXL367_REG_Z_DATA_H		0x12
#define ADXL367_REG_TEMP_DATA_H		0x14
#define ADXL367_REG_EX_ADC_DATA_H	0x16
#define ADXL367_DATA_MASK		GENMASK(15, 2)

#define ADXL367_TEMP_25C		165
#define ADXL367_TEMP_PER_C		54

#define ADXL367_VOLTAGE_OFFSET		8192
#define ADXL367_VOLTAGE_MAX_MV		1000
#define ADXL367_VOLTAGE_MAX_RAW		GENMASK(13, 0)

#define ADXL367_REG_RESET		0x1F
#define ADXL367_RESET_CODE		0x52

#define ADXL367_REG_THRESH_ACT_H	0x20
#define ADXL367_REG_THRESH_INACT_H	0x23
#define ADXL367_THRESH_MAX		GENMASK(12, 0)
#define ADXL367_THRESH_VAL_H_MASK	GENMASK(12, 6)
#define ADXL367_THRESH_H_MASK		GENMASK(6, 0)
#define ADXL367_THRESH_VAL_L_MASK	GENMASK(5, 0)
#define ADXL367_THRESH_L_MASK		GENMASK(7, 2)

#define ADXL367_REG_TIME_ACT		0x22
#define ADXL367_REG_TIME_INACT_H	0x25
#define ADXL367_TIME_ACT_MAX		GENMASK(7, 0)
#define ADXL367_TIME_INACT_MAX		GENMASK(15, 0)
#define ADXL367_TIME_INACT_VAL_H_MASK	GENMASK(15, 8)
#define ADXL367_TIME_INACT_H_MASK	GENMASK(7, 0)
#define ADXL367_TIME_INACT_VAL_L_MASK	GENMASK(7, 0)
#define ADXL367_TIME_INACT_L_MASK	GENMASK(7, 0)

#define ADXL367_REG_ACT_INACT_CTL	0x27
#define ADXL367_ACT_EN_MASK		GENMASK(1, 0)
#define ADXL367_ACT_LINKLOOP_MASK	GENMASK(5, 4)

#define ADXL367_REG_FIFO_CTL		0x28
#define ADXL367_FIFO_CTL_FORMAT_MASK	GENMASK(6, 3)
#define ADXL367_FIFO_CTL_MODE_MASK	GENMASK(1, 0)

#define ADXL367_REG_FIFO_SAMPLES	0x29
#define ADXL367_FIFO_SIZE		512
#define ADXL367_FIFO_MAX_WATERMARK	511

#define ADXL367_SAMPLES_VAL_H_MASK	BIT(8)
#define ADXL367_SAMPLES_H_MASK		BIT(2)
#define ADXL367_SAMPLES_VAL_L_MASK	GENMASK(7, 0)
#define ADXL367_SAMPLES_L_MASK		GENMASK(7, 0)

#define ADXL367_REG_INT1_MAP		0x2A
#define ADXL367_INT_INACT_MASK		BIT(5)
#define ADXL367_INT_ACT_MASK		BIT(4)
#define ADXL367_INT_FIFO_WATERMARK_MASK	BIT(2)

#define ADXL367_REG_FILTER_CTL		0x2C
#define ADXL367_FILTER_CTL_RANGE_MASK	GENMASK(7, 6)
#define ADXL367_2G_RANGE_1G		4095
#define ADXL367_2G_RANGE_100MG		409
#define ADXL367_FILTER_CTL_ODR_MASK	GENMASK(2, 0)

#define ADXL367_REG_POWER_CTL		0x2D
#define ADXL367_POWER_CTL_MODE_MASK	GENMASK(1, 0)

#define ADXL367_REG_ADC_CTL		0x3C
#define ADXL367_REG_TEMP_CTL		0x3D
#define ADXL367_ADC_EN_MASK		BIT(0)

enum adxl367_range {
	ADXL367_2G_RANGE,
	ADXL367_4G_RANGE,
	ADXL367_8G_RANGE,
};

enum adxl367_fifo_mode {
	ADXL367_FIFO_MODE_DISABLED = 0b00,
	ADXL367_FIFO_MODE_STREAM = 0b10,
};

enum adxl367_fifo_format {
	ADXL367_FIFO_FORMAT_XYZ,
	ADXL367_FIFO_FORMAT_X,
	ADXL367_FIFO_FORMAT_Y,
	ADXL367_FIFO_FORMAT_Z,
	ADXL367_FIFO_FORMAT_XYZT,
	ADXL367_FIFO_FORMAT_XT,
	ADXL367_FIFO_FORMAT_YT,
	ADXL367_FIFO_FORMAT_ZT,
	ADXL367_FIFO_FORMAT_XYZA,
	ADXL367_FIFO_FORMAT_XA,
	ADXL367_FIFO_FORMAT_YA,
	ADXL367_FIFO_FORMAT_ZA,
};

enum adxl367_op_mode {
	ADXL367_OP_STANDBY = 0b00,
	ADXL367_OP_MEASURE = 0b10,
};

enum adxl367_act_proc_mode {
	ADXL367_LOOPED = 0b11,
};

enum adxl367_act_en_mode {
	ADXL367_ACT_DISABLED = 0b00,
	ADCL367_ACT_REF_ENABLED = 0b11,
};

enum adxl367_activity_type {
	ADXL367_ACTIVITY,
	ADXL367_INACTIVITY,
};

enum adxl367_odr {
	ADXL367_ODR_12P5HZ,
	ADXL367_ODR_25HZ,
	ADXL367_ODR_50HZ,
	ADXL367_ODR_100HZ,
	ADXL367_ODR_200HZ,
	ADXL367_ODR_400HZ,
};

struct adxl367_state {
	const struct adxl367_ops	*ops;
	void				*context;

	struct device			*dev;
	struct regmap			*regmap;

	struct regulator_bulk_data	regulators[2];

	/*
	 * Synchronize access to members of driver state, and ensure atomicity
	 * of consecutive regmap operations.
	 */
	struct mutex		lock;

	enum adxl367_odr	odr;
	enum adxl367_range	range;

	unsigned int	act_threshold;
	unsigned int	act_time_ms;
	unsigned int	inact_threshold;
	unsigned int	inact_time_ms;

	unsigned int	fifo_set_size;
	unsigned int	fifo_watermark;

	__be16		fifo_buf[ADXL367_FIFO_SIZE] ____cacheline_aligned;
	__be16		sample_buf;
	u8		act_threshold_buf[2];
	u8		inact_time_buf[2];
	u8		status_buf[3];
};

static const unsigned int adxl367_threshold_h_reg_tbl[] = {
	[ADXL367_ACTIVITY]   = ADXL367_REG_THRESH_ACT_H,
	[ADXL367_INACTIVITY] = ADXL367_REG_THRESH_INACT_H,
};

static const unsigned int adxl367_act_en_shift_tbl[] = {
	[ADXL367_ACTIVITY]   = 0,
	[ADXL367_INACTIVITY] = 2,
};

static const unsigned int adxl367_act_int_mask_tbl[] = {
	[ADXL367_ACTIVITY]   = ADXL367_INT_ACT_MASK,
	[ADXL367_INACTIVITY] = ADXL367_INT_INACT_MASK,
};

static const int adxl367_samp_freq_tbl[][2] = {
	[ADXL367_ODR_12P5HZ] = {12, 500000},
	[ADXL367_ODR_25HZ]   = {25, 0},
	[ADXL367_ODR_50HZ]   = {50, 0},
	[ADXL367_ODR_100HZ]  = {100, 0},
	[ADXL367_ODR_200HZ]  = {200, 0},
	[ADXL367_ODR_400HZ]  = {400, 0},
};

/* (g * 2) * 9.80665 * 1000000 / (2^14 - 1) */
static const int adxl367_range_scale_tbl[][2] = {
	[ADXL367_2G_RANGE] = {0, 2394347},
	[ADXL367_4G_RANGE] = {0, 4788695},
	[ADXL367_8G_RANGE] = {0, 9577391},
};

static const int adxl367_range_scale_factor_tbl[] = {
	[ADXL367_2G_RANGE] = 1,
	[ADXL367_4G_RANGE] = 2,
	[ADXL367_8G_RANGE] = 4,
};

enum {
	ADXL367_X_CHANNEL_INDEX,
	ADXL367_Y_CHANNEL_INDEX,
	ADXL367_Z_CHANNEL_INDEX,
	ADXL367_TEMP_CHANNEL_INDEX,
	ADXL367_EX_ADC_CHANNEL_INDEX
};

#define ADXL367_X_CHANNEL_MASK		BIT(ADXL367_X_CHANNEL_INDEX)
#define ADXL367_Y_CHANNEL_MASK		BIT(ADXL367_Y_CHANNEL_INDEX)
#define ADXL367_Z_CHANNEL_MASK		BIT(ADXL367_Z_CHANNEL_INDEX)
#define ADXL367_TEMP_CHANNEL_MASK	BIT(ADXL367_TEMP_CHANNEL_INDEX)
#define ADXL367_EX_ADC_CHANNEL_MASK	BIT(ADXL367_EX_ADC_CHANNEL_INDEX)

static const enum adxl367_fifo_format adxl367_fifo_formats[] = {
	ADXL367_FIFO_FORMAT_X,
	ADXL367_FIFO_FORMAT_Y,
	ADXL367_FIFO_FORMAT_Z,
	ADXL367_FIFO_FORMAT_XT,
	ADXL367_FIFO_FORMAT_YT,
	ADXL367_FIFO_FORMAT_ZT,
	ADXL367_FIFO_FORMAT_XA,
	ADXL367_FIFO_FORMAT_YA,
	ADXL367_FIFO_FORMAT_ZA,
	ADXL367_FIFO_FORMAT_XYZ,
	ADXL367_FIFO_FORMAT_XYZT,
	ADXL367_FIFO_FORMAT_XYZA,
};

static const unsigned long adxl367_channel_masks[] = {
	ADXL367_X_CHANNEL_MASK,
	ADXL367_Y_CHANNEL_MASK,
	ADXL367_Z_CHANNEL_MASK,
	ADXL367_X_CHANNEL_MASK | ADXL367_TEMP_CHANNEL_MASK,
	ADXL367_Y_CHANNEL_MASK | ADXL367_TEMP_CHANNEL_MASK,
	ADXL367_Z_CHANNEL_MASK | ADXL367_TEMP_CHANNEL_MASK,
	ADXL367_X_CHANNEL_MASK | ADXL367_EX_ADC_CHANNEL_MASK,
	ADXL367_Y_CHANNEL_MASK | ADXL367_EX_ADC_CHANNEL_MASK,
	ADXL367_Z_CHANNEL_MASK | ADXL367_EX_ADC_CHANNEL_MASK,
	ADXL367_X_CHANNEL_MASK | ADXL367_Y_CHANNEL_MASK | ADXL367_Z_CHANNEL_MASK,
	ADXL367_X_CHANNEL_MASK | ADXL367_Y_CHANNEL_MASK | ADXL367_Z_CHANNEL_MASK |
		ADXL367_TEMP_CHANNEL_MASK,
	ADXL367_X_CHANNEL_MASK | ADXL367_Y_CHANNEL_MASK | ADXL367_Z_CHANNEL_MASK |
		ADXL367_EX_ADC_CHANNEL_MASK,
	0,
};

static int adxl367_set_measure_en(struct adxl367_state *st, bool en)
{
	enum adxl367_op_mode op_mode = en ? ADXL367_OP_MEASURE
					  : ADXL367_OP_STANDBY;
	int ret;

	ret = regmap_update_bits(st->regmap, ADXL367_REG_POWER_CTL,
				 ADXL367_POWER_CTL_MODE_MASK,
				 FIELD_PREP(ADXL367_POWER_CTL_MODE_MASK,
					    op_mode));
	if (ret)
		return ret;

	/*
	 * Wait for acceleration output to settle after entering
	 * measure mode.
	 */
	if (en)
		msleep(100);

	return 0;
}

static void adxl367_scale_act_thresholds(struct adxl367_state *st,
					 enum adxl367_range old_range,
					 enum adxl367_range new_range)
{
	st->act_threshold = st->act_threshold
			    * adxl367_range_scale_factor_tbl[old_range]
			    / adxl367_range_scale_factor_tbl[new_range];
	st->inact_threshold = st->inact_threshold
			      * adxl367_range_scale_factor_tbl[old_range]
			      / adxl367_range_scale_factor_tbl[new_range];
}

static int _adxl367_set_act_threshold(struct adxl367_state *st,
				      enum adxl367_activity_type act,
				      unsigned int threshold)
{
	u8 reg = adxl367_threshold_h_reg_tbl[act];
	int ret;

	if (threshold > ADXL367_THRESH_MAX)
		return -EINVAL;

	st->act_threshold_buf[0] = FIELD_PREP(ADXL367_THRESH_H_MASK,
					      FIELD_GET(ADXL367_THRESH_VAL_H_MASK,
							threshold));
	st->act_threshold_buf[1] = FIELD_PREP(ADXL367_THRESH_L_MASK,
					      FIELD_GET(ADXL367_THRESH_VAL_L_MASK,
							threshold));

	ret = regmap_bulk_write(st->regmap, reg, st->act_threshold_buf,
				sizeof(st->act_threshold_buf));
	if (ret)
		return ret;

	if (act == ADXL367_ACTIVITY)
		st->act_threshold = threshold;
	else
		st->inact_threshold = threshold;

	return 0;
}

static int adxl367_set_act_threshold(struct adxl367_state *st,
				     enum adxl367_activity_type act,
				     unsigned int threshold)
{
	int ret;

	mutex_lock(&st->lock);

	ret = adxl367_set_measure_en(st, false);
	if (ret)
		goto out;

	ret = _adxl367_set_act_threshold(st, act, threshold);
	if (ret)
		goto out;

	ret = adxl367_set_measure_en(st, true);

out:
	mutex_unlock(&st->lock);

	return ret;
}

static int adxl367_set_act_proc_mode(struct adxl367_state *st,
				     enum adxl367_act_proc_mode mode)
{
	return regmap_update_bits(st->regmap, ADXL367_REG_ACT_INACT_CTL,
				  ADXL367_ACT_LINKLOOP_MASK,
				  FIELD_PREP(ADXL367_ACT_LINKLOOP_MASK,
					     mode));
}

static int adxl367_set_act_interrupt_en(struct adxl367_state *st,
					enum adxl367_activity_type act,
					bool en)
{
	unsigned int mask = adxl367_act_int_mask_tbl[act];

	return regmap_update_bits(st->regmap, ADXL367_REG_INT1_MAP,
				  mask, en ? mask : 0);
}

static int adxl367_get_act_interrupt_en(struct adxl367_state *st,
					enum adxl367_activity_type act,
					bool *en)
{
	unsigned int mask = adxl367_act_int_mask_tbl[act];
	unsigned int val;
	int ret;

	ret = regmap_read(st->regmap, ADXL367_REG_INT1_MAP, &val);
	if (ret)
		return ret;

	*en = !!(val & mask);

	return 0;
}

static int adxl367_set_act_en(struct adxl367_state *st,
			      enum adxl367_activity_type act,
			      enum adxl367_act_en_mode en)
{
	unsigned int ctl_shift = adxl367_act_en_shift_tbl[act];

	return regmap_update_bits(st->regmap, ADXL367_REG_ACT_INACT_CTL,
				  ADXL367_ACT_EN_MASK << ctl_shift,
				  en << ctl_shift);
}

static int adxl367_set_fifo_watermark_interrupt_en(struct adxl367_state *st,
						   bool en)
{
	return regmap_update_bits(st->regmap, ADXL367_REG_INT1_MAP,
				  ADXL367_INT_FIFO_WATERMARK_MASK,
				  en ? ADXL367_INT_FIFO_WATERMARK_MASK : 0);
}

static int adxl367_get_fifo_mode(struct adxl367_state *st,
				 enum adxl367_fifo_mode *fifo_mode)
{
	unsigned int val;
	int ret;

	ret = regmap_read(st->regmap, ADXL367_REG_FIFO_CTL, &val);
	if (ret)
		return ret;

	*fifo_mode = FIELD_GET(ADXL367_FIFO_CTL_MODE_MASK, val);

	return 0;
}

static int adxl367_set_fifo_mode(struct adxl367_state *st,
				 enum adxl367_fifo_mode fifo_mode)
{
	return regmap_update_bits(st->regmap, ADXL367_REG_FIFO_CTL,
				  ADXL367_FIFO_CTL_MODE_MASK,
				  FIELD_PREP(ADXL367_FIFO_CTL_MODE_MASK,
					     fifo_mode));
}

static int adxl367_set_fifo_format(struct adxl367_state *st,
				   enum adxl367_fifo_format fifo_format)
{
	return regmap_update_bits(st->regmap, ADXL367_REG_FIFO_CTL,
				  ADXL367_FIFO_CTL_FORMAT_MASK,
				  FIELD_PREP(ADXL367_FIFO_CTL_FORMAT_MASK,
					     fifo_format));
}

static int adxl367_set_fifo_samples(struct adxl367_state *st,
				    unsigned int fifo_watermark,
				    unsigned int fifo_set_size)
{
	unsigned int fifo_samples = fifo_watermark * fifo_set_size;
	unsigned int fifo_samples_h, fifo_samples_l;
	int ret;

	if (fifo_samples > ADXL367_FIFO_MAX_WATERMARK)
		fifo_samples = ADXL367_FIFO_MAX_WATERMARK;

	if (fifo_set_size == 0)
		return 0;

	fifo_samples /= fifo_set_size;

	fifo_samples_h = FIELD_PREP(ADXL367_SAMPLES_H_MASK,
				    FIELD_GET(ADXL367_SAMPLES_VAL_H_MASK,
					      fifo_samples));
	fifo_samples_l = FIELD_PREP(ADXL367_SAMPLES_L_MASK,
				    FIELD_GET(ADXL367_SAMPLES_VAL_L_MASK,
					      fifo_samples));

	ret = regmap_update_bits(st->regmap, ADXL367_REG_FIFO_CTL,
				 ADXL367_SAMPLES_H_MASK, fifo_samples_h);
	if (ret)
		return ret;

	return regmap_update_bits(st->regmap, ADXL367_REG_FIFO_SAMPLES,
				  ADXL367_SAMPLES_L_MASK, fifo_samples_l);
}

static int adxl367_set_fifo_set_size(struct adxl367_state *st,
				     unsigned int fifo_set_size)
{
	int ret;

	ret = adxl367_set_fifo_samples(st, st->fifo_watermark, fifo_set_size);
	if (ret)
		return ret;

	st->fifo_set_size = fifo_set_size;

	return 0;
}

static int adxl367_set_fifo_watermark(struct adxl367_state *st,
				      unsigned int fifo_watermark)
{
	int ret;

	ret = adxl367_set_fifo_samples(st, fifo_watermark, st->fifo_set_size);
	if (ret)
		return ret;

	st->fifo_watermark = fifo_watermark;

	return 0;
}

static int adxl367_set_range(struct iio_dev *indio_dev,
			     enum adxl367_range range)
{
	struct adxl367_state *st = iio_priv(indio_dev);
	int ret;

	ret = iio_device_claim_direct_mode(indio_dev);
	if (ret)
		return ret;

	mutex_lock(&st->lock);

	ret = adxl367_set_measure_en(st, false);
	if (ret)
		goto out;

	ret = regmap_update_bits(st->regmap, ADXL367_REG_FILTER_CTL,
				 ADXL367_FILTER_CTL_RANGE_MASK,
				 FIELD_PREP(ADXL367_FILTER_CTL_RANGE_MASK,
					    range));
	if (ret)
		goto out;

	adxl367_scale_act_thresholds(st, st->range, range);

	/* Activity thresholds depend on range */
	ret = _adxl367_set_act_threshold(st, ADXL367_ACTIVITY,
					 st->act_threshold);
	if (ret)
		goto out;

	ret = _adxl367_set_act_threshold(st, ADXL367_INACTIVITY,
					 st->inact_threshold);
	if (ret)
		goto out;

	ret = adxl367_set_measure_en(st, true);
	if (ret)
		goto out;

	st->range = range;

out:
	mutex_unlock(&st->lock);

	iio_device_release_direct_mode(indio_dev);

	return ret;
}

static int adxl367_time_ms_to_samples(struct adxl367_state *st, unsigned int ms)
{
	int freq_hz = adxl367_samp_freq_tbl[st->odr][0];
	int freq_microhz = adxl367_samp_freq_tbl[st->odr][1];
	/* Scale to decihertz to prevent precision loss in 12.5Hz case. */
	int freq_dhz = freq_hz * 10 + freq_microhz / 100000;

	return DIV_ROUND_CLOSEST(ms * freq_dhz, 10000);
}

static int _adxl367_set_act_time_ms(struct adxl367_state *st, unsigned int ms)
{
	unsigned int val = adxl367_time_ms_to_samples(st, ms);
	int ret;

	if (val > ADXL367_TIME_ACT_MAX)
		val = ADXL367_TIME_ACT_MAX;

	ret = regmap_write(st->regmap, ADXL367_REG_TIME_ACT, val);
	if (ret)
		return ret;

	st->act_time_ms = ms;

	return 0;
}

static int _adxl367_set_inact_time_ms(struct adxl367_state *st, unsigned int ms)
{
	unsigned int val = adxl367_time_ms_to_samples(st, ms);
	int ret;

	if (val > ADXL367_TIME_INACT_MAX)
		val = ADXL367_TIME_INACT_MAX;

	st->inact_time_buf[0] = FIELD_PREP(ADXL367_TIME_INACT_H_MASK,
					   FIELD_GET(ADXL367_TIME_INACT_VAL_H_MASK,
						     val));
	st->inact_time_buf[1] = FIELD_PREP(ADXL367_TIME_INACT_L_MASK,
					   FIELD_GET(ADXL367_TIME_INACT_VAL_L_MASK,
						     val));

	ret = regmap_bulk_write(st->regmap, ADXL367_REG_TIME_INACT_H,
				st->inact_time_buf, sizeof(st->inact_time_buf));
	if (ret)
		return ret;

	st->inact_time_ms = ms;

	return 0;
}

static int adxl367_set_act_time_ms(struct adxl367_state *st,
				   enum adxl367_activity_type act,
				   unsigned int ms)
{
	int ret;

	mutex_lock(&st->lock);

	ret = adxl367_set_measure_en(st, false);
	if (ret)
		goto out;

	if (act == ADXL367_ACTIVITY)
		ret = _adxl367_set_act_time_ms(st, ms);
	else
		ret = _adxl367_set_inact_time_ms(st, ms);

	if (ret)
		goto out;

	ret = adxl367_set_measure_en(st, true);

out:
	mutex_unlock(&st->lock);

	return ret;
}

static int _adxl367_set_odr(struct adxl367_state *st, enum adxl367_odr odr)
{
	int ret;

	ret = regmap_update_bits(st->regmap, ADXL367_REG_FILTER_CTL,
				 ADXL367_FILTER_CTL_ODR_MASK,
				 FIELD_PREP(ADXL367_FILTER_CTL_ODR_MASK,
					    odr));
	if (ret)
		return ret;

	/* Activity timers depend on ODR */
	ret = _adxl367_set_act_time_ms(st, st->act_time_ms);
	if (ret)
		return ret;

	ret = _adxl367_set_inact_time_ms(st, st->inact_time_ms);
	if (ret)
		return ret;

	st->odr = odr;

	return 0;
}

static int adxl367_set_odr(struct iio_dev *indio_dev, enum adxl367_odr odr)
{
	struct adxl367_state *st = iio_priv(indio_dev);
	int ret;

	ret = iio_device_claim_direct_mode(indio_dev);
	if (ret)
		return ret;

	mutex_lock(&st->lock);

	ret = adxl367_set_measure_en(st, false);
	if (ret)
		goto out;

	ret = _adxl367_set_odr(st, odr);
	if (ret)
		goto out;

	ret = adxl367_set_measure_en(st, true);

out:
	mutex_unlock(&st->lock);

	iio_device_release_direct_mode(indio_dev);

	return ret;
}

static int adxl367_set_temp_adc_en(struct adxl367_state *st, unsigned int reg,
				   bool en)
{
	return regmap_update_bits(st->regmap, reg, ADXL367_ADC_EN_MASK,
				  en ? ADXL367_ADC_EN_MASK : 0);
}

static int adxl367_set_temp_adc_reg_en(struct adxl367_state *st,
				       unsigned int reg, bool en)
{
	int ret;

	switch (reg) {
	case ADXL367_REG_TEMP_DATA_H:
		ret = adxl367_set_temp_adc_en(st, ADXL367_REG_TEMP_CTL, en);
		break;
	case ADXL367_REG_EX_ADC_DATA_H:
		ret = adxl367_set_temp_adc_en(st, ADXL367_REG_ADC_CTL, en);
		break;
	default:
		return 0;
	}

	if (ret)
		return ret;

	if (en)
		msleep(100);

	return 0;
}

static int adxl367_set_temp_adc_mask_en(struct adxl367_state *st,
					const unsigned long *active_scan_mask,
					bool en)
{
	if (*active_scan_mask & ADXL367_TEMP_CHANNEL_MASK)
		return adxl367_set_temp_adc_en(st, ADXL367_REG_TEMP_CTL, en);
	else if (*active_scan_mask & ADXL367_EX_ADC_CHANNEL_MASK)
		return adxl367_set_temp_adc_en(st, ADXL367_REG_ADC_CTL, en);

	return 0;
}

static int adxl367_find_odr(struct adxl367_state *st, int val, int val2,
			    enum adxl367_odr *odr)
{
	size_t size = ARRAY_SIZE(adxl367_samp_freq_tbl);
	int i;

	for (i = 0; i < size; i++)
		if (val == adxl367_samp_freq_tbl[i][0] &&
		    val2 == adxl367_samp_freq_tbl[i][1])
			break;

	if (i == size)
		return -EINVAL;

	*odr = i;

	return 0;
}

static int adxl367_find_range(struct adxl367_state *st, int val, int val2,
			      enum adxl367_range *range)
{
	size_t size = ARRAY_SIZE(adxl367_range_scale_tbl);
	int i;

	for (i = 0; i < size; i++)
		if (val == adxl367_range_scale_tbl[i][0] &&
		    val2 == adxl367_range_scale_tbl[i][1])
			break;

	if (i == size)
		return -EINVAL;

	*range = i;

	return 0;
}

static int adxl367_read_sample(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int *val)
{
	struct adxl367_state *st = iio_priv(indio_dev);
	u16 sample;
	int ret;

	ret = iio_device_claim_direct_mode(indio_dev);
	if (ret)
		return ret;

	mutex_lock(&st->lock);

	ret = adxl367_set_temp_adc_reg_en(st, chan->address, true);
	if (ret)
		goto out;

	ret = regmap_bulk_read(st->regmap, chan->address, &st->sample_buf,
			       sizeof(st->sample_buf));
	if (ret)
		goto out;

	sample = FIELD_GET(ADXL367_DATA_MASK, be16_to_cpu(st->sample_buf));
	*val = sign_extend32(sample, chan->scan_type.realbits - 1);

	ret = adxl367_set_temp_adc_reg_en(st, chan->address, false);

out:
	mutex_unlock(&st->lock);

	iio_device_release_direct_mode(indio_dev);

	return ret ?: IIO_VAL_INT;
}

static int adxl367_get_status(struct adxl367_state *st, u8 *status,
			      u16 *fifo_entries)
{
	int ret;

	/* Read STATUS, FIFO_ENT_L and FIFO_ENT_H */
	ret = regmap_bulk_read(st->regmap, ADXL367_REG_STATUS,
			       st->status_buf, sizeof(st->status_buf));
	if (ret)
		return ret;

	st->status_buf[2] &= ADXL367_FIFO_ENT_H_MASK;

	*status = st->status_buf[0];
	*fifo_entries = get_unaligned_le16(&st->status_buf[1]);

	return 0;
}

static bool adxl367_push_event(struct iio_dev *indio_dev, u8 status)
{
	unsigned int ev_dir;

	if (FIELD_GET(ADXL367_STATUS_ACT_MASK, status))
		ev_dir = IIO_EV_DIR_RISING;
	else if (FIELD_GET(ADXL367_STATUS_INACT_MASK, status))
		ev_dir = IIO_EV_DIR_FALLING;
	else
		return false;

	iio_push_event(indio_dev,
		       IIO_MOD_EVENT_CODE(IIO_ACCEL, 0, IIO_MOD_X_OR_Y_OR_Z,
					  IIO_EV_TYPE_THRESH, ev_dir),
		       iio_get_time_ns(indio_dev));

	return true;
}

static bool adxl367_push_fifo_data(struct iio_dev *indio_dev, u8 status,
				   u16 fifo_entries)
{
	struct adxl367_state *st = iio_priv(indio_dev);
	int ret;
	int i;

	if (!FIELD_GET(ADXL367_STATUS_FIFO_FULL_MASK, status))
		return false;

	fifo_entries -= fifo_entries % st->fifo_set_size;

	ret = st->ops->read_fifo(st->context, st->fifo_buf, fifo_entries);
	if (ret) {
		dev_err(st->dev, "Failed to read FIFO: %d\n", ret);
		return true;
	}

	for (i = 0; i < fifo_entries; i += st->fifo_set_size)
		iio_push_to_buffers(indio_dev, &st->fifo_buf[i]);

	return true;
}

static irqreturn_t adxl367_irq_handler(int irq, void *private)
{
	struct iio_dev *indio_dev = private;
	struct adxl367_state *st = iio_priv(indio_dev);
	u16 fifo_entries;
	bool handled;
	u8 status;
	int ret;

	ret = adxl367_get_status(st, &status, &fifo_entries);
	if (ret)
		return IRQ_NONE;

	handled = adxl367_push_event(indio_dev, status);
	handled |= adxl367_push_fifo_data(indio_dev, status, fifo_entries);

	return handled ? IRQ_HANDLED : IRQ_NONE;
}

static int adxl367_reg_access(struct iio_dev *indio_dev,
			      unsigned int reg,
			      unsigned int writeval,
			      unsigned int *readval)
{
	struct adxl367_state *st = iio_priv(indio_dev);

	if (readval)
		return regmap_read(st->regmap, reg, readval);
	else
		return regmap_write(st->regmap, reg, writeval);
}

static int adxl367_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long info)
{
	struct adxl367_state *st = iio_priv(indio_dev);

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		return adxl367_read_sample(indio_dev, chan, val);
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ACCEL:
			mutex_lock(&st->lock);
			*val = adxl367_range_scale_tbl[st->range][0];
			*val2 = adxl367_range_scale_tbl[st->range][1];
			mutex_unlock(&st->lock);
			return IIO_VAL_INT_PLUS_NANO;
		case IIO_TEMP:
			*val = 1000;
			*val2 = ADXL367_TEMP_PER_C;
			return IIO_VAL_FRACTIONAL;
		case IIO_VOLTAGE:
			*val = ADXL367_VOLTAGE_MAX_MV;
			*val2 = ADXL367_VOLTAGE_MAX_RAW;
			return IIO_VAL_FRACTIONAL;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_OFFSET:
		switch (chan->type) {
		case IIO_TEMP:
			*val = 25 * ADXL367_TEMP_PER_C - ADXL367_TEMP_25C;
			return IIO_VAL_INT;
		case IIO_VOLTAGE:
			*val = ADXL367_VOLTAGE_OFFSET;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SAMP_FREQ:
		mutex_lock(&st->lock);
		*val = adxl367_samp_freq_tbl[st->odr][0];
		*val2 = adxl367_samp_freq_tbl[st->odr][1];
		mutex_unlock(&st->lock);
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int adxl367_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long info)
{
	struct adxl367_state *st = iio_priv(indio_dev);
	int ret;

	switch (info) {
	case IIO_CHAN_INFO_SAMP_FREQ: {
		enum adxl367_odr odr;

		ret = adxl367_find_odr(st, val, val2, &odr);
		if (ret)
			return ret;

		return adxl367_set_odr(indio_dev, odr);
	}
	case IIO_CHAN_INFO_SCALE: {
		enum adxl367_range range;

		ret = adxl367_find_range(st, val, val2, &range);
		if (ret)
			return ret;

		return adxl367_set_range(indio_dev, range);
	}
	default:
		return -EINVAL;
	}
}

static int adxl367_write_raw_get_fmt(struct iio_dev *indio_dev,
				     struct iio_chan_spec const *chan,
				     long info)
{
	switch (info) {
	case IIO_CHAN_INFO_SCALE:
		if (chan->type != IIO_ACCEL)
			return -EINVAL;

		return IIO_VAL_INT_PLUS_NANO;
	default:
		return IIO_VAL_INT_PLUS_MICRO;
	}
}

static int adxl367_read_avail(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      const int **vals, int *type, int *length,
			      long info)
{
	switch (info) {
	case IIO_CHAN_INFO_SCALE:
		if (chan->type != IIO_ACCEL)
			return -EINVAL;

		*vals = (int *)adxl367_range_scale_tbl;
		*type = IIO_VAL_INT_PLUS_NANO;
		*length = ARRAY_SIZE(adxl367_range_scale_tbl) * 2;
		return IIO_AVAIL_LIST;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*vals = (int *)adxl367_samp_freq_tbl;
		*type = IIO_VAL_INT_PLUS_MICRO;
		*length = ARRAY_SIZE(adxl367_samp_freq_tbl) * 2;
		return IIO_AVAIL_LIST;
	default:
		return -EINVAL;
	}
}

static int adxl367_read_event_value(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir,
				    enum iio_event_info info,
				    int *val, int *val2)
{
	struct adxl367_state *st = iio_priv(indio_dev);

	switch (info) {
	case IIO_EV_INFO_VALUE: {
		switch (dir) {
		case IIO_EV_DIR_RISING:
			mutex_lock(&st->lock);
			*val = st->act_threshold;
			mutex_unlock(&st->lock);
			return IIO_VAL_INT;
		case IIO_EV_DIR_FALLING:
			mutex_lock(&st->lock);
			*val = st->inact_threshold;
			mutex_unlock(&st->lock);
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	}
	case IIO_EV_INFO_PERIOD:
		switch (dir) {
		case IIO_EV_DIR_RISING:
			mutex_lock(&st->lock);
			*val = st->act_time_ms;
			mutex_unlock(&st->lock);
			*val2 = 1000;
			return IIO_VAL_FRACTIONAL;
		case IIO_EV_DIR_FALLING:
			mutex_lock(&st->lock);
			*val = st->inact_time_ms;
			mutex_unlock(&st->lock);
			*val2 = 1000;
			return IIO_VAL_FRACTIONAL;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int adxl367_write_event_value(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir,
				     enum iio_event_info info,
				     int val, int val2)
{
	struct adxl367_state *st = iio_priv(indio_dev);

	switch (info) {
	case IIO_EV_INFO_VALUE:
		if (val < 0)
			return -EINVAL;

		switch (dir) {
		case IIO_EV_DIR_RISING:
			return adxl367_set_act_threshold(st, ADXL367_ACTIVITY, val);
		case IIO_EV_DIR_FALLING:
			return adxl367_set_act_threshold(st, ADXL367_INACTIVITY, val);
		default:
			return -EINVAL;
		}
	case IIO_EV_INFO_PERIOD:
		if (val < 0)
			return -EINVAL;

		val = val * 1000 + DIV_ROUND_UP(val2, 1000);
		switch (dir) {
		case IIO_EV_DIR_RISING:
			return adxl367_set_act_time_ms(st, ADXL367_ACTIVITY, val);
		case IIO_EV_DIR_FALLING:
			return adxl367_set_act_time_ms(st, ADXL367_INACTIVITY, val);
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int adxl367_read_event_config(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir)
{
	struct adxl367_state *st = iio_priv(indio_dev);
	bool en;
	int ret;

	switch (dir) {
	case IIO_EV_DIR_RISING:
		ret = adxl367_get_act_interrupt_en(st, ADXL367_ACTIVITY, &en);
		return ret ?: en;
	case IIO_EV_DIR_FALLING:
		ret = adxl367_get_act_interrupt_en(st, ADXL367_INACTIVITY, &en);
		return ret ?: en;
	default:
		return -EINVAL;
	}
}

static int adxl367_write_event_config(struct iio_dev *indio_dev,
				      const struct iio_chan_spec *chan,
				      enum iio_event_type type,
				      enum iio_event_direction dir,
				      int state)
{
	struct adxl367_state *st = iio_priv(indio_dev);
	enum adxl367_activity_type act;
	int ret;

	switch (dir) {
	case IIO_EV_DIR_RISING:
		act = ADXL367_ACTIVITY;
		break;
	case IIO_EV_DIR_FALLING:
		act = ADXL367_INACTIVITY;
		break;
	default:
		return -EINVAL;
	}

	ret = iio_device_claim_direct_mode(indio_dev);
	if (ret)
		return ret;

	mutex_lock(&st->lock);

	ret = adxl367_set_measure_en(st, false);
	if (ret)
		goto out;

	ret = adxl367_set_act_interrupt_en(st, act, state);
	if (ret)
		goto out;

	ret = adxl367_set_act_en(st, act, state ? ADCL367_ACT_REF_ENABLED
						: ADXL367_ACT_DISABLED);
	if (ret)
		goto out;

	ret = adxl367_set_measure_en(st, true);

out:
	mutex_unlock(&st->lock);

	iio_device_release_direct_mode(indio_dev);

	return ret;
}

static ssize_t adxl367_get_fifo_enabled(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct adxl367_state *st = iio_priv(dev_to_iio_dev(dev));
	enum adxl367_fifo_mode fifo_mode;
	int ret;

	ret = adxl367_get_fifo_mode(st, &fifo_mode);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%d\n", fifo_mode != ADXL367_FIFO_MODE_DISABLED);
}

static ssize_t adxl367_get_fifo_watermark(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct adxl367_state *st = iio_priv(dev_to_iio_dev(dev));
	unsigned int fifo_watermark;

	mutex_lock(&st->lock);
	fifo_watermark = st->fifo_watermark;
	mutex_unlock(&st->lock);

	return sysfs_emit(buf, "%d\n", fifo_watermark);
}

static IIO_CONST_ATTR(hwfifo_watermark_min, "1");
static IIO_CONST_ATTR(hwfifo_watermark_max,
		      __stringify(ADXL367_FIFO_MAX_WATERMARK));
static IIO_DEVICE_ATTR(hwfifo_watermark, 0444,
		       adxl367_get_fifo_watermark, NULL, 0);
static IIO_DEVICE_ATTR(hwfifo_enabled, 0444,
		       adxl367_get_fifo_enabled, NULL, 0);

static const struct attribute *adxl367_fifo_attributes[] = {
	&iio_const_attr_hwfifo_watermark_min.dev_attr.attr,
	&iio_const_attr_hwfifo_watermark_max.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark.dev_attr.attr,
	&iio_dev_attr_hwfifo_enabled.dev_attr.attr,
	NULL,
};

static int adxl367_set_watermark(struct iio_dev *indio_dev, unsigned int val)
{
	struct adxl367_state *st  = iio_priv(indio_dev);
	int ret;

	if (val > ADXL367_FIFO_MAX_WATERMARK)
		return -EINVAL;

	mutex_lock(&st->lock);

	ret = adxl367_set_measure_en(st, false);
	if (ret)
		goto out;

	ret = adxl367_set_fifo_watermark(st, val);
	if (ret)
		goto out;

	ret = adxl367_set_measure_en(st, true);

out:
	mutex_unlock(&st->lock);

	return ret;
}

static bool adxl367_find_mask_fifo_format(const unsigned long *scan_mask,
					  enum adxl367_fifo_format *fifo_format)
{
	size_t size = ARRAY_SIZE(adxl367_fifo_formats);
	int i;

	for (i = 0; i < size; i++)
		if (*scan_mask == adxl367_channel_masks[i])
			break;

	if (i == size)
		return false;

	*fifo_format = adxl367_fifo_formats[i];

	return true;
}

static int adxl367_update_scan_mode(struct iio_dev *indio_dev,
				    const unsigned long *active_scan_mask)
{
	struct adxl367_state *st  = iio_priv(indio_dev);
	enum adxl367_fifo_format fifo_format;
	unsigned int fifo_set_size;
	int ret;

	if (!adxl367_find_mask_fifo_format(active_scan_mask, &fifo_format))
		return -EINVAL;

	fifo_set_size = bitmap_weight(active_scan_mask, indio_dev->masklength);

	mutex_lock(&st->lock);

	ret = adxl367_set_measure_en(st, false);
	if (ret)
		goto out;

	ret = adxl367_set_fifo_format(st, fifo_format);
	if (ret)
		goto out;

	ret = adxl367_set_fifo_set_size(st, fifo_set_size);
	if (ret)
		goto out;

	ret = adxl367_set_measure_en(st, true);

out:
	mutex_unlock(&st->lock);

	return ret;
}

static int adxl367_buffer_postenable(struct iio_dev *indio_dev)
{
	struct adxl367_state *st = iio_priv(indio_dev);
	int ret;

	mutex_lock(&st->lock);

	ret = adxl367_set_temp_adc_mask_en(st, indio_dev->active_scan_mask,
					   true);
	if (ret)
		goto out;

	ret = adxl367_set_measure_en(st, false);
	if (ret)
		goto out;

	ret = adxl367_set_fifo_watermark_interrupt_en(st, true);
	if (ret)
		goto out;

	ret = adxl367_set_fifo_mode(st, ADXL367_FIFO_MODE_STREAM);
	if (ret)
		goto out;

	ret = adxl367_set_measure_en(st, true);

out:
	mutex_unlock(&st->lock);

	return ret;
}

static int adxl367_buffer_predisable(struct iio_dev *indio_dev)
{
	struct adxl367_state *st = iio_priv(indio_dev);
	int ret;

	mutex_lock(&st->lock);

	ret = adxl367_set_measure_en(st, false);
	if (ret)
		goto out;

	ret = adxl367_set_fifo_mode(st, ADXL367_FIFO_MODE_DISABLED);
	if (ret)
		goto out;

	ret = adxl367_set_fifo_watermark_interrupt_en(st, false);
	if (ret)
		goto out;

	ret = adxl367_set_measure_en(st, true);
	if (ret)
		goto out;

	ret = adxl367_set_temp_adc_mask_en(st, indio_dev->active_scan_mask,
					   false);

out:
	mutex_unlock(&st->lock);

	return ret;
}

static const struct iio_buffer_setup_ops adxl367_buffer_ops = {
	.postenable = adxl367_buffer_postenable,
	.predisable = adxl367_buffer_predisable,
};

static const struct iio_info adxl367_info = {
	.read_raw = adxl367_read_raw,
	.write_raw = adxl367_write_raw,
	.write_raw_get_fmt = adxl367_write_raw_get_fmt,
	.read_avail = adxl367_read_avail,
	.read_event_config = adxl367_read_event_config,
	.write_event_config = adxl367_write_event_config,
	.read_event_value = adxl367_read_event_value,
	.write_event_value = adxl367_write_event_value,
	.debugfs_reg_access = adxl367_reg_access,
	.hwfifo_set_watermark = adxl367_set_watermark,
	.update_scan_mode = adxl367_update_scan_mode,
};

static const struct iio_event_spec adxl367_events[] = {
	{
		.type = IIO_EV_TYPE_MAG_REFERENCED,
		.dir = IIO_EV_DIR_RISING,
		.mask_shared_by_type = BIT(IIO_EV_INFO_ENABLE) |
				       BIT(IIO_EV_INFO_PERIOD) |
				       BIT(IIO_EV_INFO_VALUE),
	},
	{
		.type = IIO_EV_TYPE_MAG_REFERENCED,
		.dir = IIO_EV_DIR_FALLING,
		.mask_shared_by_type = BIT(IIO_EV_INFO_ENABLE) |
				       BIT(IIO_EV_INFO_PERIOD) |
				       BIT(IIO_EV_INFO_VALUE),
	},
};

#define ADXL367_ACCEL_CHANNEL(index, reg, axis) {			\
	.type = IIO_ACCEL,						\
	.address = (reg),						\
	.modified = 1,							\
	.channel2 = IIO_MOD_##axis,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),			\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),		\
	.info_mask_shared_by_type_available = BIT(IIO_CHAN_INFO_SCALE),	\
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
	.info_mask_shared_by_all_available =				\
			BIT(IIO_CHAN_INFO_SAMP_FREQ),			\
	.event_spec = adxl367_events,					\
	.num_event_specs = ARRAY_SIZE(adxl367_events),			\
	.scan_index = (index),						\
	.scan_type = {							\
		.sign = 's',						\
		.realbits = 14,						\
		.storagebits = 16,					\
		.endianness = IIO_BE,					\
	},								\
}

#define ADXL367_CHANNEL(index, reg, _type) {				\
	.type = (_type),						\
	.address = (reg),						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |			\
			      BIT(IIO_CHAN_INFO_OFFSET) |		\
			      BIT(IIO_CHAN_INFO_SCALE),			\
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
	.scan_index = (index),						\
	.scan_type = {							\
		.sign = 's',						\
		.realbits = 14,						\
		.storagebits = 16,					\
		.endianness = IIO_BE,					\
	},								\
}

static const struct iio_chan_spec adxl367_channels[] = {
	ADXL367_ACCEL_CHANNEL(ADXL367_X_CHANNEL_INDEX, ADXL367_REG_X_DATA_H, X),
	ADXL367_ACCEL_CHANNEL(ADXL367_Y_CHANNEL_INDEX, ADXL367_REG_Y_DATA_H, Y),
	ADXL367_ACCEL_CHANNEL(ADXL367_Z_CHANNEL_INDEX, ADXL367_REG_Z_DATA_H, Z),
	ADXL367_CHANNEL(ADXL367_TEMP_CHANNEL_INDEX, ADXL367_REG_TEMP_DATA_H,
			IIO_TEMP),
	ADXL367_CHANNEL(ADXL367_EX_ADC_CHANNEL_INDEX, ADXL367_REG_EX_ADC_DATA_H,
			IIO_VOLTAGE),
};

static int adxl367_verify_devid(struct adxl367_state *st)
{
	unsigned int val;
	int ret;

	ret = regmap_read_poll_timeout(st->regmap, ADXL367_REG_DEVID, val,
				       val == ADXL367_DEVID_AD, 1000, 10000);
	if (ret)
		return dev_err_probe(st->dev, -ENODEV,
				     "Invalid dev id 0x%02X, expected 0x%02X\n",
				     val, ADXL367_DEVID_AD);

	return 0;
}

static int adxl367_setup(struct adxl367_state *st)
{
	int ret;

	ret = _adxl367_set_act_threshold(st, ADXL367_ACTIVITY,
					 ADXL367_2G_RANGE_1G);
	if (ret)
		return ret;

	ret = _adxl367_set_act_threshold(st, ADXL367_INACTIVITY,
					 ADXL367_2G_RANGE_100MG);
	if (ret)
		return ret;

	ret = adxl367_set_act_proc_mode(st, ADXL367_LOOPED);
	if (ret)
		return ret;

	ret = _adxl367_set_odr(st, ADXL367_ODR_400HZ);
	if (ret)
		return ret;

	ret = _adxl367_set_act_time_ms(st, 10);
	if (ret)
		return ret;

	ret = _adxl367_set_inact_time_ms(st, 10000);
	if (ret)
		return ret;

	return adxl367_set_measure_en(st, true);
}

static void adxl367_disable_regulators(void *data)
{
	struct adxl367_state *st = data;

	regulator_bulk_disable(ARRAY_SIZE(st->regulators), st->regulators);
}

int adxl367_probe(struct device *dev, const struct adxl367_ops *ops,
		  void *context, struct regmap *regmap, int irq)
{
	struct iio_dev *indio_dev;
	struct adxl367_state *st;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	st->dev = dev;
	st->regmap = regmap;
	st->context = context;
	st->ops = ops;

	mutex_init(&st->lock);

	indio_dev->channels = adxl367_channels;
	indio_dev->num_channels = ARRAY_SIZE(adxl367_channels);
	indio_dev->available_scan_masks = adxl367_channel_masks;
	indio_dev->name = "adxl367";
	indio_dev->info = &adxl367_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	st->regulators[0].supply = "vdd";
	st->regulators[1].supply = "vddio";

	ret = devm_regulator_bulk_get(st->dev, ARRAY_SIZE(st->regulators),
				      st->regulators);
	if (ret)
		return dev_err_probe(st->dev, ret,
				     "Failed to get regulators\n");

	ret = regulator_bulk_enable(ARRAY_SIZE(st->regulators), st->regulators);
	if (ret)
		return dev_err_probe(st->dev, ret,
				     "Failed to enable regulators\n");

	ret = devm_add_action_or_reset(st->dev, adxl367_disable_regulators, st);
	if (ret)
		return dev_err_probe(st->dev, ret,
				     "Failed to add regulators disable action\n");

	ret = regmap_write(st->regmap, ADXL367_REG_RESET, ADXL367_RESET_CODE);
	if (ret)
		return ret;

	ret = adxl367_verify_devid(st);
	if (ret)
		return ret;

	ret = adxl367_setup(st);
	if (ret)
		return ret;

	ret = devm_iio_kfifo_buffer_setup_ext(st->dev, indio_dev,
					      &adxl367_buffer_ops,
					      adxl367_fifo_attributes);
	if (ret)
		return ret;

	ret = devm_request_threaded_irq(st->dev, irq, NULL,
					adxl367_irq_handler, IRQF_ONESHOT,
					indio_dev->name, indio_dev);
	if (ret)
		return dev_err_probe(st->dev, ret, "Failed to request irq\n");

	return devm_iio_device_register(dev, indio_dev);
}
EXPORT_SYMBOL_NS_GPL(adxl367_probe, IIO_ADXL367);

MODULE_AUTHOR("Cosmin Tanislav <cosmin.tanislav@analog.com>");
MODULE_DESCRIPTION("Analog Devices ADXL367 3-axis accelerometer driver");
MODULE_LICENSE("GPL");
