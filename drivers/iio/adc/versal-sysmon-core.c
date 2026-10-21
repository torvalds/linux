// SPDX-License-Identifier: GPL-2.0
/*
 * AMD Versal SysMon core driver
 *
 * Copyright (C) 2019 - 2022, Xilinx, Inc.
 * Copyright (C) 2022 - 2026, Advanced Micro Devices, Inc.
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/limits.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/units.h>

#include <linux/iio/events.h>
#include <linux/iio/iio.h>

#include "versal-sysmon.h"

/*
 * Oversampling ratio values exposed to userspace via IIO.
 * Actual number of samples averaged: 1=none, 2=2x, 4=4x, 8=8x, 16=16x.
 */
static const int sysmon_oversampling_avail[] = { 1, 2, 4, 8, 16 };

/* TEMP hysteresis mode bit in SYSMON_TEMP_EV_CFG */
#define SYSMON_TEMP_HYST_MASK		BIT(1)

/* Compute alarm register offset from a channel address */
#define SYSMON_ALARM_OFFSET(addr) \
	(SYSMON_ALARM_REG + ((addr) / SYSMON_ALARM_BITS_PER_REG) * SYSMON_REG_STRIDE)

#define SYSMON_CHAN_TEMP(_chan, _address, _name)		\
{								\
	.type = IIO_TEMP,					\
	.indexed = 1,						\
	.address = _address,					\
	.channel = _chan,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type =				\
		BIT(IIO_CHAN_INFO_SCALE) |			\
		BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),		\
	.info_mask_shared_by_type_available =			\
		BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),		\
	.datasheet_name = _name,				\
}

enum sysmon_alarm_bit {
	SYSMON_BIT_ALARM0 = 0,
	SYSMON_BIT_ALARM1 = 1,
	SYSMON_BIT_ALARM2 = 2,
	SYSMON_BIT_ALARM3 = 3,
	SYSMON_BIT_ALARM4 = 4,
	SYSMON_BIT_TEMP = 9,
};

/* Temperature event specification: rising threshold + hysteresis only */
static const struct iio_event_spec sysmon_temp_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE) |
				 BIT(IIO_EV_INFO_VALUE) |
				 BIT(IIO_EV_INFO_HYSTERESIS),
	},
};

/* Supply event specifications */
static const struct iio_event_spec sysmon_supply_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE),
	},
};

/*
 * Static temperature channels (always present).
 *
 * These are hardware-computed aggregate registers across all active
 * temperature satellites:
 *   temp:     current max temperature across all active satellites
 *   min:      current min temperature across all active satellites
 *   max_max:  highest peak recorded since last hardware reset
 *   min_min:  lowest trough recorded since last hardware reset
 */
static const struct iio_chan_spec temp_channels[] = {
	SYSMON_CHAN_TEMP(0, SYSMON_TEMP_MAX, "temp"),
	SYSMON_CHAN_TEMP(1, SYSMON_TEMP_MIN, "min"),
	SYSMON_CHAN_TEMP(2, SYSMON_TEMP_MAX_MAX, "max_max"),
	SYSMON_CHAN_TEMP(3, SYSMON_TEMP_MIN_MIN, "min_min"),
};

static void sysmon_q8p7_to_millicelsius(s16 raw_data, int *val)
{
	*val = (raw_data * MILLIDEGREE_PER_DEGREE) >> SYSMON_FRACTIONAL_SHIFT;
}

static void sysmon_millicelsius_to_q8p7(u32 *raw_data, int val)
{
	*raw_data = (val << SYSMON_FRACTIONAL_SHIFT) / MILLIDEGREE_PER_DEGREE;
}

static void sysmon_supply_rawtoprocessed(int raw_data, int *val)
{
	int mantissa, format, exponent;

	mantissa = FIELD_GET(SYSMON_MANTISSA_MASK, raw_data);
	exponent = SYSMON_SUPPLY_MANTISSA_BITS - FIELD_GET(SYSMON_MODE_MASK, raw_data);
	format = FIELD_GET(SYSMON_FMT_MASK, raw_data);
	/*
	 * When format bit is set the mantissa is two's complement
	 * (per hardware spec); sign-extend to int for correct arithmetic.
	 */
	if (format)
		mantissa = sign_extend32(mantissa, 15);

	*val = (mantissa * (int)MILLI) >> exponent;
}

static void sysmon_supply_processedtoraw(int val, u32 reg_val, u32 *raw_data)
{
	int exponent = FIELD_GET(SYSMON_MODE_MASK, reg_val);
	int format = FIELD_GET(SYSMON_FMT_MASK, reg_val);
	int scale, tmp;

	scale = BIT(SYSMON_SUPPLY_MANTISSA_BITS - exponent);
	tmp = (val * scale) / (int)MILLI;

	if (format)
		tmp = clamp(tmp, S16_MIN, S16_MAX);
	else
		tmp = clamp(tmp, 0, U16_MAX);

	*raw_data = (u16)tmp;
}

static int sysmon_supply_thresh_offset(unsigned long address, enum iio_event_direction dir)
{
	if (dir == IIO_EV_DIR_RISING)
		return (address * SYSMON_REG_STRIDE) + SYSMON_SUPPLY_TH_UP;
	if (dir == IIO_EV_DIR_FALLING)
		return (address * SYSMON_REG_STRIDE) + SYSMON_SUPPLY_TH_LOW;

	return -EINVAL;
}

static int sysmon_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	struct sysmon *sysmon = iio_priv(indio_dev);
	unsigned int regval;
	int ret;

	guard(mutex)(&sysmon->lock);

	if (mask == IIO_CHAN_INFO_OVERSAMPLING_RATIO) {
		*val = (chan->type == IIO_TEMP) ? sysmon->temp_oversampling :
						 sysmon->supply_oversampling;
		return IIO_VAL_INT;
	}

	switch (chan->type) {
	case IIO_TEMP:
		if (mask == IIO_CHAN_INFO_SCALE) {
			/* Q8.7 to millicelsius: raw * 1000 / 128 */
			*val = MILLIDEGREE_PER_DEGREE;
			*val2 = BIT(SYSMON_FRACTIONAL_SHIFT);
			return IIO_VAL_FRACTIONAL;
		}
		if (mask != IIO_CHAN_INFO_RAW)
			return -EINVAL;

		ret = regmap_read(sysmon->regmap, chan->address, &regval);
		if (ret)
			return ret;

		*val = sign_extend32(regval, 15);
		return IIO_VAL_INT;

	case IIO_VOLTAGE:
		if (mask != IIO_CHAN_INFO_PROCESSED)
			return -EINVAL;

		ret = regmap_read(sysmon->regmap,
				  chan->address * SYSMON_REG_STRIDE +
				  SYSMON_SUPPLY_BASE, &regval);
		if (ret)
			return ret;

		sysmon_supply_rawtoprocessed(regval, val);
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static u32 sysmon_get_event_mask(const struct iio_chan_spec *chan)
{
	if (chan->type == IIO_TEMP)
		return BIT(SYSMON_BIT_TEMP);

	return BIT(chan->address / SYSMON_ALARM_BITS_PER_REG);
}

static int sysmon_read_alarm_config(struct sysmon *sysmon,
				    unsigned long address)
{
	u32 shift = address % SYSMON_ALARM_BITS_PER_REG;
	u32 offset = SYSMON_ALARM_OFFSET(address);

	return regmap_test_bits(sysmon->regmap, offset, BIT(shift));
}

static int sysmon_write_alarm_config(struct sysmon *sysmon,
				     unsigned long address, bool enable)
{
	u32 shift = address % SYSMON_ALARM_BITS_PER_REG;
	u32 offset = SYSMON_ALARM_OFFSET(address);

	return regmap_assign_bits(sysmon->regmap, offset, BIT(shift), enable);
}

static int sysmon_read_event_config(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir)
{
	struct sysmon *sysmon = iio_priv(indio_dev);
	u32 mask = sysmon_get_event_mask(chan);
	unsigned int imr;
	int config_value;
	int ret;

	ret = regmap_read(sysmon->regmap, SYSMON_IMR, &imr);
	if (ret)
		return ret;

	/* IMR bits are 1=masked, invert to get 1=enabled */
	imr = ~imr;

	switch (chan->type) {
	case IIO_VOLTAGE:
		config_value = sysmon_read_alarm_config(sysmon, chan->address);
		if (config_value < 0)
			return config_value;
		return config_value && (imr & mask);

	case IIO_TEMP:
		/*
		 * Return the administrative state, not the hardware IMR.
		 * The IRQ handler temporarily masks the interrupt during
		 * the polling window; reading IMR would show it as disabled.
		 * temp_mask bit is set when administratively disabled.
		 */
		return !(sysmon->temp_mask & mask);

	default:
		return -EINVAL;
	}
}

static int sysmon_write_event_config(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir,
				     bool state)
{
	u32 offset = SYSMON_ALARM_OFFSET(chan->address);
	struct sysmon *sysmon = iio_priv(indio_dev);
	u32 mask = sysmon_get_event_mask(chan);
	unsigned int alarm_config;
	int ret;

	guard(mutex)(&sysmon->lock);

	switch (chan->type) {
	case IIO_VOLTAGE:
		ret = sysmon_write_alarm_config(sysmon, chan->address, state);
		if (ret)
			return ret;

		ret = regmap_read(sysmon->regmap, offset, &alarm_config);
		if (ret)
			return ret;

		if (alarm_config)
			return regmap_write(sysmon->regmap, SYSMON_IER, mask);

		return regmap_write(sysmon->regmap, SYSMON_IDR, mask);

	case IIO_TEMP:
		if (state) {
			ret = regmap_write(sysmon->regmap, SYSMON_IER, mask);
			if (ret)
				return ret;

			scoped_guard(spinlock_irq, &sysmon->irq_lock)
				sysmon->temp_mask &= ~mask;
		} else {
			ret = regmap_write(sysmon->regmap, SYSMON_IDR, mask);
			if (ret)
				return ret;

			scoped_guard(spinlock_irq, &sysmon->irq_lock)
				sysmon->temp_mask |= mask;
		}
		return 0;

	default:
		return -EINVAL;
	}
}

/*
 * Recompute the lower threshold register from upper threshold and
 * cached hysteresis. Called when either upper threshold or hysteresis
 * is written.
 */
static int sysmon_update_temp_lower(struct sysmon *sysmon)
{
	unsigned int upper_reg;
	int upper_mc, lower_mc;
	u32 raw_val;
	int ret;

	ret = regmap_read(sysmon->regmap, SYSMON_TEMP_TH_UP, &upper_reg);
	if (ret)
		return ret;

	sysmon_q8p7_to_millicelsius(upper_reg, &upper_mc);
	lower_mc = clamp(upper_mc - sysmon->temp_hysteresis, -256000, 255992);
	sysmon_millicelsius_to_q8p7(&raw_val, lower_mc);

	return regmap_write(sysmon->regmap, SYSMON_TEMP_TH_LOW, raw_val);
}

static int sysmon_read_event_value(struct iio_dev *indio_dev,
				   const struct iio_chan_spec *chan,
				   enum iio_event_type type,
				   enum iio_event_direction dir,
				   enum iio_event_info info,
				   int *val, int *val2)
{
	struct sysmon *sysmon = iio_priv(indio_dev);
	unsigned int reg_val;
	int offset;
	int ret;

	guard(mutex)(&sysmon->lock);

	switch (chan->type) {
	case IIO_TEMP:
		switch (info) {
		case IIO_EV_INFO_VALUE:
			ret = regmap_read(sysmon->regmap, SYSMON_TEMP_TH_UP, &reg_val);
			if (ret)
				return ret;

			sysmon_q8p7_to_millicelsius(reg_val, val);

			return IIO_VAL_INT;

		case IIO_EV_INFO_HYSTERESIS:
			*val = sysmon->temp_hysteresis;
			return IIO_VAL_INT;

		default:
			return -EINVAL;
		}

	case IIO_VOLTAGE:
		offset = sysmon_supply_thresh_offset(chan->address, dir);
		if (offset < 0)
			return offset;

		ret = regmap_read(sysmon->regmap, offset, &reg_val);
		if (ret)
			return ret;

		sysmon_supply_rawtoprocessed(reg_val, val);

		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static int sysmon_write_event_value(struct iio_dev *indio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir,
				    enum iio_event_info info,
				    int val, int val2)
{
	struct sysmon *sysmon = iio_priv(indio_dev);
	unsigned int reg_val;
	u32 raw_val;
	int offset;
	int ret;

	guard(mutex)(&sysmon->lock);

	switch (chan->type) {
	case IIO_TEMP:
		switch (info) {
		case IIO_EV_INFO_VALUE:
			/* Q8.7 signed range: -256000 to +255992 mC */
			if (val < -256000 || val > 255992)
				return -EINVAL;

			sysmon_millicelsius_to_q8p7(&raw_val, val);

			ret = regmap_write(sysmon->regmap, SYSMON_TEMP_TH_UP, raw_val);
			if (ret)
				return ret;

			/* Recompute lower = upper - hysteresis */
			return sysmon_update_temp_lower(sysmon);

		case IIO_EV_INFO_HYSTERESIS:
			if (val < 0)
				return -EINVAL;

			sysmon->temp_hysteresis = val;

			return sysmon_update_temp_lower(sysmon);

		default:
			return -EINVAL;
		}

	case IIO_VOLTAGE:
		offset = sysmon_supply_thresh_offset(chan->address, dir);
		if (offset < 0)
			return offset;

		ret = regmap_read(sysmon->regmap, offset, &reg_val);
		if (ret)
			return ret;

		/* Clamp to prevent overflow in processedtoraw conversion */
		if (val < -32768 || val > 32767)
			return -EINVAL;

		sysmon_supply_processedtoraw(val, reg_val, &raw_val);

		/*
		 * The hardware threshold register returns FMT and MODE
		 * bits in the upper 16 bits on read, but only the lower
		 * 16-bit mantissa is used on write.
		 */
		return regmap_write(sysmon->regmap, offset, raw_val);

	default:
		return -EINVAL;
	}
}

static int sysmon_set_avg_enable(struct sysmon *sysmon,
				 u32 base, u32 count, u32 val)
{
	struct regmap *map = sysmon->regmap;
	int ret;

	for (unsigned int i = 0; i < count; i++) {
		ret = regmap_write(map, base + i * SYSMON_REG_STRIDE, val);
		if (ret)
			return ret;
	}

	return 0;
}

static int sysmon_osr_write_temp(struct sysmon *sysmon, unsigned int val)
{
	/*
	 * HW register encoding is sample_count / 2:
	 * 0=none, 1=2x, 2=4x, 4=8x, 8=16x (not log2-based).
	 */
	unsigned int hw_val = val >> 1;
	unsigned int readback;
	int ret;

	ret = regmap_update_bits(sysmon->regmap, SYSMON_CONFIG,
				SYSMON_CONFIG_TEMP_SAT_OSR,
				FIELD_PREP(SYSMON_CONFIG_TEMP_SAT_OSR, hw_val));
	if (ret)
		return ret;

	/*
	 * Readback fence: the SysMon CONFIG register resides in the
	 * PMC domain behind the NoC. A posted write may not reach the
	 * hardware before the next MMIO access. Reading the register
	 * back forces the interconnect to complete the write, preventing
	 * a bus hang on the subsequent access.
	 */
	regmap_read(sysmon->regmap, SYSMON_CONFIG, &readback);

	return sysmon_set_avg_enable(sysmon, SYSMON_TEMP_EN_AVG_BASE,
				     SYSMON_TEMP_EN_AVG_COUNT,
				     hw_val ? ~0 : 0);
}

static int sysmon_osr_write_supply(struct sysmon *sysmon, unsigned int val)
{
	/* HW encoding: sample_count / 2 (see sysmon_osr_write_temp) */
	unsigned int hw_val = val >> 1;
	unsigned int readback;
	int ret;

	ret = regmap_update_bits(sysmon->regmap, SYSMON_CONFIG,
				SYSMON_CONFIG_SUPPLY_OSR,
				FIELD_PREP(SYSMON_CONFIG_SUPPLY_OSR, hw_val));
	if (ret)
		return ret;

	/* Readback fence -- see sysmon_osr_write_temp for details */
	regmap_read(sysmon->regmap, SYSMON_CONFIG, &readback);

	return sysmon_set_avg_enable(sysmon, SYSMON_SUPPLY_EN_AVG_BASE,
				     SYSMON_SUPPLY_EN_AVG_COUNT,
				     hw_val ? ~0 : 0);
}

static int sysmon_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	struct sysmon *sysmon = iio_priv(indio_dev);
	unsigned int i;
	int ret;

	if (mask != IIO_CHAN_INFO_OVERSAMPLING_RATIO)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(sysmon_oversampling_avail); i++) {
		if (val == sysmon_oversampling_avail[i])
			break;
	}
	if (i == ARRAY_SIZE(sysmon_oversampling_avail))
		return -EINVAL;

	guard(mutex)(&sysmon->lock);

	if (chan->type == IIO_TEMP) {
		ret = sysmon_osr_write_temp(sysmon, val);
		if (ret)
			return ret;
		sysmon->temp_oversampling = val;
	} else {
		ret = sysmon_osr_write_supply(sysmon, val);
		if (ret)
			return ret;
		sysmon->supply_oversampling = val;
	}

	return 0;
}

static int sysmon_write_raw_get_fmt(struct iio_dev *indio_dev,
				    struct iio_chan_spec const *chan,
				    long mask)
{
	if (mask == IIO_CHAN_INFO_OVERSAMPLING_RATIO)
		return IIO_VAL_INT;

	return -EINVAL;
}

static int sysmon_read_avail(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     const int **vals, int *type,
			     int *length, long mask)
{
	if (mask != IIO_CHAN_INFO_OVERSAMPLING_RATIO)
		return -EINVAL;

	*vals = sysmon_oversampling_avail;
	*type = IIO_VAL_INT;
	*length = ARRAY_SIZE(sysmon_oversampling_avail);

	return IIO_AVAIL_LIST;
}

static int sysmon_read_label(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     char *label)
{
	if (chan->datasheet_name)
		return sysfs_emit(label, "%s\n", chan->datasheet_name);

	return -EINVAL;
}

static const struct iio_info sysmon_iio_info = {
	.read_raw = sysmon_read_raw,
	.write_raw = sysmon_write_raw,
	.write_raw_get_fmt = sysmon_write_raw_get_fmt,
	.read_avail = sysmon_read_avail,
	.read_label = sysmon_read_label,
	.read_event_config = sysmon_read_event_config,
	.write_event_config = sysmon_write_event_config,
	.read_event_value = sysmon_read_event_value,
	.write_event_value = sysmon_write_event_value,
};

static void sysmon_push_event(struct iio_dev *indio_dev, u32 address)
{
	const struct iio_chan_spec *chan;
	enum iio_event_direction dir;

	for (unsigned int i = 0; i < indio_dev->num_channels; i++) {
		if (indio_dev->channels[i].address != address)
			continue;

		chan = &indio_dev->channels[i];
		/* Temp uses hysteresis mode (rising only), voltage uses window */
		dir = (chan->type == IIO_TEMP) ? IIO_EV_DIR_RISING :
						 IIO_EV_DIR_EITHER;
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(chan->type,
						    chan->channel,
						    IIO_EV_TYPE_THRESH,
						    dir),
			       iio_get_time_ns(indio_dev));
	}
}

static int sysmon_handle_event(struct iio_dev *indio_dev, u32 event)
{
	u32 alarm_flag_offset = SYSMON_ALARM_FLAG + event * SYSMON_REG_STRIDE;
	u32 alarm_reg_offset = SYSMON_ALARM_REG + event * SYSMON_REG_STRIDE;
	struct sysmon *sysmon = iio_priv(indio_dev);
	unsigned long alarm_flag_reg;
	unsigned int reg_val;
	u32 address, bit;
	int ret;

	switch (event) {
	case SYSMON_BIT_TEMP:
		sysmon_push_event(indio_dev, SYSMON_TEMP_MAX);

		ret = regmap_write(sysmon->regmap, SYSMON_IDR, BIT(SYSMON_BIT_TEMP));
		if (ret)
			return ret;

		sysmon->masked_temp |= BIT(SYSMON_BIT_TEMP);
		return 0;

	case SYSMON_BIT_ALARM0:
	case SYSMON_BIT_ALARM1:
	case SYSMON_BIT_ALARM2:
	case SYSMON_BIT_ALARM3:
	case SYSMON_BIT_ALARM4:
		ret = regmap_read(sysmon->regmap, alarm_flag_offset, &reg_val);
		if (ret)
			return ret;

		alarm_flag_reg = reg_val;

		for_each_set_bit(bit, &alarm_flag_reg, SYSMON_ALARM_BITS_PER_REG) {
			address = bit + SYSMON_ALARM_BITS_PER_REG * event;
			sysmon_push_event(indio_dev, address);
			ret = regmap_clear_bits(sysmon->regmap, alarm_reg_offset, BIT(bit));
			if (ret)
				return ret;
		}

		return regmap_write(sysmon->regmap, alarm_flag_offset, alarm_flag_reg);

	default:
		return -EINVAL;
	}
}

static void sysmon_handle_events(struct iio_dev *indio_dev,
				 unsigned long events)
{
	unsigned int bit;

	for_each_set_bit(bit, &events, SYSMON_NO_OF_EVENTS)
		sysmon_handle_event(indio_dev, bit);
}

static void sysmon_unmask_temp(struct sysmon *sysmon, unsigned int isr)
{
	unsigned int status;
	u32 ier;

	status = isr & SYSMON_TEMP_INTR_MASK;

	ier = ~status & sysmon->masked_temp;
	sysmon->masked_temp &= status;

	/* Only unmask if not administratively disabled by userspace */
	ier &= ~sysmon->temp_mask;

	regmap_write(sysmon->regmap, SYSMON_IER, ier);
}

/*
 * Versal threshold interrupts are level-sensitive. Active threshold
 * interrupts are masked in the handler and polled via delayed work
 * until the condition clears, then unmasked.
 */
static void sysmon_unmask_worker(struct work_struct *work)
{
	struct sysmon *sysmon =
		container_of(work, struct sysmon, sysmon_unmask_work.work);
	unsigned int isr;

	/*
	 * If the ISR read fails, skip processing to avoid acting
	 * on undefined data.
	 */
	scoped_guard(spinlock_irq, &sysmon->irq_lock) {
		if (regmap_read(sysmon->regmap, SYSMON_ISR, &isr))
			break;
		regmap_write(sysmon->regmap, SYSMON_ISR, isr);
		sysmon_unmask_temp(sysmon, isr);
	}

	if (sysmon->masked_temp)
		schedule_delayed_work(&sysmon->sysmon_unmask_work,
				      msecs_to_jiffies(SYSMON_UNMASK_WORK_DELAY_MS));
	else
		regmap_write(sysmon->regmap, SYSMON_STATUS_RESET, 1);
}

static irqreturn_t sysmon_iio_irq(int irq, void *data)
{
	struct iio_dev *indio_dev = data;
	struct sysmon *sysmon = iio_priv(indio_dev);
	unsigned int isr, imr;

	guard(spinlock)(&sysmon->irq_lock);

	if (regmap_read(sysmon->regmap, SYSMON_ISR, &isr) ||
	    regmap_read(sysmon->regmap, SYSMON_IMR, &imr))
		return IRQ_NONE;

	isr &= ~imr;
	if (!isr)
		return IRQ_NONE;

	regmap_write(sysmon->regmap, SYSMON_ISR, isr);

	sysmon_handle_events(indio_dev, isr);
	schedule_delayed_work(&sysmon->sysmon_unmask_work,
			      msecs_to_jiffies(SYSMON_UNMASK_WORK_DELAY_MS));

	return IRQ_HANDLED;
}

static void sysmon_disable_interrupts(void *data)
{
	struct sysmon *sysmon = data;

	regmap_write(sysmon->regmap, SYSMON_IDR, SYSMON_INTR_ALL_MASK);

	scoped_guard(spinlock_irq, &sysmon->irq_lock)
		sysmon->masked_temp = 0;

	cancel_delayed_work_sync(&sysmon->sysmon_unmask_work);
}

static int sysmon_init_interrupt(struct sysmon *sysmon,
				 struct device *dev,
				 struct iio_dev *indio_dev,
				 int irq)
{
	unsigned int imr;
	int ret;

	/* Events not supported without IRQ (e.g. I2C path) */
	if (!irq)
		return 0;

	INIT_DELAYED_WORK(&sysmon->sysmon_unmask_work, sysmon_unmask_worker);

	ret = regmap_read(sysmon->regmap, SYSMON_IMR, &imr);
	if (ret)
		return ret;
	sysmon->temp_mask = imr & SYSMON_TEMP_INTR_MASK;

	ret = devm_request_irq(dev, irq, sysmon_iio_irq, 0, "sysmon-irq", indio_dev);
	if (ret)
		return ret;

	return devm_add_action_or_reset(dev, sysmon_disable_interrupts, sysmon);
}

/*
 * Initialize the cached hysteresis for a temperature channel from the
 * current hardware threshold registers: hysteresis = upper - lower.
 */
static int sysmon_init_hysteresis(struct sysmon *sysmon, int *hysteresis)
{
	unsigned int upper_reg, lower_reg;
	int upper_mc, lower_mc;
	int ret;

	ret = regmap_read(sysmon->regmap, SYSMON_TEMP_TH_UP, &upper_reg);
	if (ret)
		return ret;

	ret = regmap_read(sysmon->regmap, SYSMON_TEMP_TH_LOW, &lower_reg);
	if (ret)
		return ret;

	sysmon_q8p7_to_millicelsius(upper_reg, &upper_mc);
	sysmon_q8p7_to_millicelsius(lower_reg, &lower_mc);
	*hysteresis = upper_mc - lower_mc;

	return 0;
}

/**
 * sysmon_parse_fw() - Parse firmware nodes and configure IIO channels.
 * @indio_dev: IIO device instance
 * @dev: Parent device
 * @irq: IRQ number (positive enables event channels, 0 disables)
 *
 * Reads voltage-channels and temperature-channels container nodes from
 * firmware and builds the IIO channel array. Static temperature channels
 * and event channels are prepended, followed by supply and satellite
 * channels from DT.
 *
 * Event channels and per-channel event specs are only added when the
 * device has an IRQ. I2C devices have no interrupt line, and the I2C
 * regmap cannot be called from atomic context, so events are not
 * supported on that path.
 *
 * Return: 0 on success, negative errno on failure.
 */
static int sysmon_parse_fw(struct iio_dev *indio_dev, struct device *dev, int irq)
{
	unsigned int num_chan, num_static, num_supply, num_temp;
	unsigned int idx, temp_chan_idx, volt_chan_idx;
	struct iio_chan_spec *sysmon_channels;
	const char *label;
	u32 reg;
	int ret;

	struct fwnode_handle *supply_node __free(fwnode_handle) =
		device_get_named_child_node(dev, "voltage-channels");
	num_supply = fwnode_get_child_node_count(supply_node);

	struct fwnode_handle *temp_node __free(fwnode_handle) =
		device_get_named_child_node(dev, "temperature-channels");
	num_temp = fwnode_get_child_node_count(temp_node);

	num_static = ARRAY_SIZE(temp_channels);
	num_chan = size_add(num_temp, size_add(num_static, num_supply));
	sysmon_channels = devm_kcalloc(dev, num_chan, sizeof(*sysmon_channels), GFP_KERNEL);
	if (!sysmon_channels)
		return -ENOMEM;

	memcpy(sysmon_channels, temp_channels, sizeof(temp_channels));

	/* Attach event spec to channel 0 when IRQ is available */
	if (irq > 0) {
		sysmon_channels[0].event_spec = sysmon_temp_events;
		sysmon_channels[0].num_event_specs = ARRAY_SIZE(sysmon_temp_events);
	}

	idx = num_static;

	/* Supply channels from DT */
	fwnode_for_each_child_node_scoped(supply_node, child) {
		ret = fwnode_property_read_u32(child, "reg", &reg);
		if (ret)
			return dev_err_probe(dev, ret,
					     "missing reg for supply channel\n");

		if (reg > SYSMON_SUPPLY_IDX_MAX)
			return dev_err_probe(dev, -EINVAL,
					     "supply reg %u exceeds max %u\n",
					     reg, SYSMON_SUPPLY_IDX_MAX);

		ret = fwnode_property_read_string(child, "label", &label);
		if (ret)
			return dev_err_probe(dev, ret,
					     "missing label for supply channel\n");

		sysmon_channels[idx++] = (struct iio_chan_spec) {
			.type = IIO_VOLTAGE,
			.indexed = 1,
			.address = reg,
			.info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
			.info_mask_shared_by_type =
				BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
			.info_mask_shared_by_type_available =
				BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
			.event_spec = irq > 0 ?
				sysmon_supply_events : NULL,
			.num_event_specs = irq > 0 ?
				ARRAY_SIZE(sysmon_supply_events) : 0,
			.datasheet_name = label,
		};
	}

	/* Temperature satellite channels from DT */
	fwnode_for_each_child_node_scoped(temp_node, child) {
		ret = fwnode_property_read_u32(child, "reg", &reg);
		if (ret)
			return dev_err_probe(dev, ret,
					     "missing reg for temp channel\n");

		if (reg < 1 || reg > SYSMON_TEMP_SAT_MAX)
			return dev_err_probe(dev, -EINVAL,
					     "temp reg %u out of range [1..%u]\n",
					     reg, SYSMON_TEMP_SAT_MAX);

		ret = fwnode_property_read_string(child, "label", &label);
		if (ret)
			return dev_err_probe(dev, ret,
					     "missing label for temp channel\n");

		sysmon_channels[idx++] = (struct iio_chan_spec) {
			.type = IIO_TEMP,
			.indexed = 1,
			.address = SYSMON_TEMP_SAT_BASE +
				   (reg - 1) * SYSMON_REG_STRIDE,
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
			.info_mask_shared_by_type =
				BIT(IIO_CHAN_INFO_SCALE) |
				BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
			.info_mask_shared_by_type_available =
				BIT(IIO_CHAN_INFO_OVERSAMPLING_RATIO),
			.datasheet_name = label,
		};
	}

	indio_dev->num_channels = idx;
	indio_dev->info = &sysmon_iio_info;

	/*
	 * Assign per-type sequential channel numbers.
	 * IIO sysfs uses type prefix (in_tempN, in_voltageN)
	 * so numbers only need to be unique within each type.
	 */
	temp_chan_idx = 0;
	volt_chan_idx = 0;
	for (unsigned int idx = 0; idx < indio_dev->num_channels; idx++) {
		if (sysmon_channels[idx].type == IIO_TEMP)
			sysmon_channels[idx].channel = temp_chan_idx++;
		else
			sysmon_channels[idx].channel = volt_chan_idx++;
	}

	indio_dev->channels = sysmon_channels;

	return 0;
}

/**
 * devm_versal_sysmon_core_probe() - Initialize Versal SysMon core
 * @dev: Parent device
 * @regmap: Register map for hardware access
 *
 * Return: 0 on success, negative errno on failure.
 */
int devm_versal_sysmon_core_probe(struct device *dev, struct regmap *regmap)
{
	struct iio_dev *indio_dev;
	struct sysmon *sysmon;
	int irq;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*sysmon));
	if (!indio_dev)
		return -ENOMEM;

	sysmon = iio_priv(indio_dev);
	sysmon->regmap = regmap;
	sysmon->temp_oversampling = 1;
	sysmon->supply_oversampling = 1;

	ret = devm_mutex_init(dev, &sysmon->lock);
	if (ret)
		return ret;
	spin_lock_init(&sysmon->irq_lock);

	/* Disable all interrupts and clear pending status */
	ret = regmap_write(sysmon->regmap, SYSMON_IDR, SYSMON_INTR_ALL_MASK);
	if (ret)
		return ret;
	ret = regmap_write(sysmon->regmap, SYSMON_ISR, SYSMON_INTR_ALL_MASK);
	if (ret)
		return ret;

	irq = fwnode_irq_get(dev_fwnode(dev), 0);
	if (irq == -EPROBE_DEFER)
		return dev_err_probe(dev, irq, "failed to get IRQ\n");

	indio_dev->name = "versal-sysmon";
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = sysmon_parse_fw(indio_dev, dev, irq);
	if (ret)
		return ret;

	if (irq > 0) {
		/* Set hysteresis mode for temperature threshold */
		ret = regmap_set_bits(sysmon->regmap, SYSMON_TEMP_EV_CFG,
				      SYSMON_TEMP_HYST_MASK);
		if (ret)
			return ret;

		/* Initialize cached hysteresis from hardware registers */
		ret = sysmon_init_hysteresis(sysmon, &sysmon->temp_hysteresis);
		if (ret)
			return ret;

		ret = sysmon_init_interrupt(sysmon, dev, indio_dev, irq);
		if (ret)
			return ret;
	}

	return devm_iio_device_register(dev, indio_dev);
}
EXPORT_SYMBOL_NS_GPL(devm_versal_sysmon_core_probe, "VERSAL_SYSMON");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AMD Versal SysMon Core Driver");
MODULE_AUTHOR("Salih Erim <salih.erim@amd.com>");
