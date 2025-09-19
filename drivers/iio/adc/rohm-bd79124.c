// SPDX-License-Identifier: GPL-2.0-only
/*
 * ROHM ADC driver for BD79124 ADC/GPO device
 * https://fscdn.rohm.com/en/products/databook/datasheet/ic/data_converter/dac/bd79124muf-c-e.pdf
 *
 * Copyright (c) 2025, ROHM Semiconductor.
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/devm-helpers.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/regmap.h>
#include <linux/types.h>

#include <asm/byteorder.h>

#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/adc-helpers.h>

#define BD79124_I2C_MULTI_READ		0x30
#define BD79124_I2C_MULTI_WRITE		0x28
#define BD79124_REG_MAX			0xaf

#define BD79124_REG_SYSTEM_STATUS	0x00
#define BD79124_REG_GEN_CFG		0x01
#define BD79124_REG_OPMODE_CFG		0x04
#define BD79124_REG_PINCFG		0x05
#define BD79124_REG_GPO_VAL		0x0B
#define BD79124_REG_SEQ_CFG		0x10
#define BD79124_REG_MANUAL_CHANNELS	0x11
#define BD79124_REG_AUTO_CHANNELS	0x12
#define BD79124_REG_ALERT_CH_SEL	0x14
#define BD79124_REG_EVENT_FLAG		0x18
#define BD79124_REG_EVENT_FLAG_HI	0x1a
#define BD79124_REG_EVENT_FLAG_LO	0x1c
#define BD79124_REG_HYSTERESIS_CH0	0x20
#define BD79124_REG_EVENTCOUNT_CH0	0x22
#define BD79124_REG_RECENT_CH0_LSB	0xa0
#define BD79124_REG_RECENT_CH7_MSB	0xaf

#define BD79124_ADC_BITS 12

/* Masks for the BD79124_REG_OPMODE_CFG */
#define BD79124_MSK_CONV_MODE GENMASK(6, 5)
#define BD79124_CONV_MODE_MANSEQ 0
#define BD79124_CONV_MODE_AUTO 1
#define BD79124_MSK_AUTO_INTERVAL GENMASK(1, 0)
#define BD79124_INTERVAL_750_US 0

/* Masks for the BD79124_REG_GEN_CFG */
#define BD79124_MSK_DWC_EN BIT(4)
#define BD79124_MSK_STATS_EN BIT(5)

/* Masks for the BD79124_REG_SEQ_CFG */
#define BD79124_MSK_SEQ_START BIT(4)
#define BD79124_MSK_SEQ_MODE GENMASK(1, 0)
#define BD79124_MSK_SEQ_MANUAL 0
#define BD79124_MSK_SEQ_SEQ 1

#define BD79124_MSK_HYSTERESIS GENMASK(3, 0)
#define BD79124_LOW_LIMIT_MIN 0
#define BD79124_HIGH_LIMIT_MAX GENMASK(11, 0)

/*
 * The high limit, low limit and last measurement result are each stored in
 * 2 consequtive registers. 4 bits are in the high bits of the first register
 * and 8 bits in the next register.
 *
 * These macros return the address of the first reg for the given channel.
 */
#define BD79124_GET_HIGH_LIMIT_REG(ch) (BD79124_REG_HYSTERESIS_CH0 + (ch) * 4)
#define BD79124_GET_LOW_LIMIT_REG(ch) (BD79124_REG_EVENTCOUNT_CH0 + (ch) * 4)
#define BD79124_GET_LIMIT_REG(ch, dir) ((dir) == IIO_EV_DIR_RISING ?		\
		BD79124_GET_HIGH_LIMIT_REG(ch) : BD79124_GET_LOW_LIMIT_REG(ch))
#define BD79124_GET_RECENT_RES_REG(ch) (BD79124_REG_RECENT_CH0_LSB + (ch) * 2)

/*
 * The hysteresis for a channel is stored in the same register where the
 * 4 bits of high limit reside.
 */
#define BD79124_GET_HYSTERESIS_REG(ch) BD79124_GET_HIGH_LIMIT_REG(ch)

#define BD79124_MAX_NUM_CHANNELS 8

struct bd79124_data {
	s64 timestamp;
	struct regmap *map;
	struct device *dev;
	int vmax;
	/*
	 * Keep measurement status so read_raw() knows if the measurement needs
	 * to be started.
	 */
	int alarm_monitored[BD79124_MAX_NUM_CHANNELS];
	/*
	 * The BD79124 does not allow disabling/enabling limit separately for
	 * one direction only. Hence, we do the disabling by changing the limit
	 * to maximum/minimum measurable value. This means we need to cache
	 * the limit in order to maintain it over the time limit is disabled.
	 */
	u16 alarm_r_limit[BD79124_MAX_NUM_CHANNELS];
	u16 alarm_f_limit[BD79124_MAX_NUM_CHANNELS];
	/* Bitmask of disabled events (for rate limiting) for each channel. */
	int alarm_suppressed[BD79124_MAX_NUM_CHANNELS];
	/*
	 * The BD79124 is configured to run the measurements in the background.
	 * This is done for the event monitoring as well as for the read_raw().
	 * Protect the measurement starting/stopping using a mutex.
	 */
	struct mutex mutex;
	struct delayed_work alm_enable_work;
	struct gpio_chip gc;
	u8 gpio_valid_mask;
};

static const struct regmap_range bd79124_ro_ranges[] = {
	{
		.range_min = BD79124_REG_EVENT_FLAG,
		.range_max = BD79124_REG_EVENT_FLAG,
	}, {
		.range_min = BD79124_REG_RECENT_CH0_LSB,
		.range_max = BD79124_REG_RECENT_CH7_MSB,
	},
};

static const struct regmap_access_table bd79124_ro_regs = {
	.no_ranges	= &bd79124_ro_ranges[0],
	.n_no_ranges	= ARRAY_SIZE(bd79124_ro_ranges),
};

static const struct regmap_range bd79124_volatile_ranges[] = {
	{
		.range_min = BD79124_REG_RECENT_CH0_LSB,
		.range_max = BD79124_REG_RECENT_CH7_MSB,
	}, {
		.range_min = BD79124_REG_EVENT_FLAG,
		.range_max = BD79124_REG_EVENT_FLAG,
	}, {
		.range_min = BD79124_REG_EVENT_FLAG_HI,
		.range_max = BD79124_REG_EVENT_FLAG_HI,
	}, {
		.range_min = BD79124_REG_EVENT_FLAG_LO,
		.range_max = BD79124_REG_EVENT_FLAG_LO,
	}, {
		.range_min = BD79124_REG_SYSTEM_STATUS,
		.range_max = BD79124_REG_SYSTEM_STATUS,
	},
};

static const struct regmap_access_table bd79124_volatile_regs = {
	.yes_ranges	= &bd79124_volatile_ranges[0],
	.n_yes_ranges	= ARRAY_SIZE(bd79124_volatile_ranges),
};

static const struct regmap_range bd79124_precious_ranges[] = {
	{
		.range_min = BD79124_REG_EVENT_FLAG_HI,
		.range_max = BD79124_REG_EVENT_FLAG_HI,
	}, {
		.range_min = BD79124_REG_EVENT_FLAG_LO,
		.range_max = BD79124_REG_EVENT_FLAG_LO,
	},
};

static const struct regmap_access_table bd79124_precious_regs = {
	.yes_ranges	= &bd79124_precious_ranges[0],
	.n_yes_ranges	= ARRAY_SIZE(bd79124_precious_ranges),
};

static const struct regmap_config bd79124_regmap = {
	.reg_bits		= 16,
	.val_bits		= 8,
	.read_flag_mask		= BD79124_I2C_MULTI_READ,
	.write_flag_mask	= BD79124_I2C_MULTI_WRITE,
	.max_register		= BD79124_REG_MAX,
	.cache_type		= REGCACHE_MAPLE,
	.volatile_table		= &bd79124_volatile_regs,
	.wr_table		= &bd79124_ro_regs,
	.precious_table		= &bd79124_precious_regs,
};

static int bd79124gpo_direction_get(struct gpio_chip *gc, unsigned int offset)
{
	return GPIO_LINE_DIRECTION_OUT;
}

static int bd79124gpo_set(struct gpio_chip *gc, unsigned int offset, int value)
{
	struct bd79124_data *data = gpiochip_get_data(gc);

	return regmap_assign_bits(data->map, BD79124_REG_GPO_VAL, BIT(offset),
				  value);
}

static int bd79124gpo_set_multiple(struct gpio_chip *gc, unsigned long *mask,
				    unsigned long *bits)
{
	unsigned int all_gpos;
	int ret;
	struct bd79124_data *data = gpiochip_get_data(gc);

	/*
	 * Ensure all GPIOs in 'mask' are set to be GPIOs
	 * The valid_mask was not obeyed by the gpiolib in all cases prior the
	 * https://lore.kernel.org/all/cd5e067b80e1bb590027bc3bfa817e7f794f21c3.1741180097.git.mazziesaccount@gmail.com/
	 *
	 * Keep this check here for a couple of cycles.
	 */
	ret = regmap_read(data->map, BD79124_REG_PINCFG, &all_gpos);
	if (ret)
		return ret;

	if (all_gpos ^ *mask) {
		dev_dbg(data->dev, "Invalid mux config. Can't set value.\n");

		return -EINVAL;
	}

	return regmap_update_bits(data->map, BD79124_REG_GPO_VAL, *mask, *bits);
}

static int bd79124_init_valid_mask(struct gpio_chip *gc,
				   unsigned long *valid_mask,
				   unsigned int ngpios)
{
	struct bd79124_data *data = gpiochip_get_data(gc);

	*valid_mask = data->gpio_valid_mask;

	return 0;
}

/* Template for GPIO chip */
static const struct gpio_chip bd79124gpo_chip = {
	.label			= "bd79124-gpo",
	.get_direction		= bd79124gpo_direction_get,
	.set			= bd79124gpo_set,
	.set_multiple		= bd79124gpo_set_multiple,
	.init_valid_mask	= bd79124_init_valid_mask,
	.can_sleep		= true,
	.ngpio			= 8,
	.base			= -1,
};

struct bd79124_raw {
	u8 val_bit3_0; /* Is set in high bits of the byte */
	u8 val_bit11_4;
};
#define BD79124_RAW_TO_INT(r) ((r.val_bit11_4 << 4) | (r.val_bit3_0 >> 4))
#define BD79124_INT_TO_RAW(val) {					\
	.val_bit11_4 = (val) >> 4,					\
	.val_bit3_0 = (val) << 4,					\
}

/*
 * The high and low limits as well as the recent result values are stored in
 * the same way in 2 consequent registers. The first register contains 4 bits
 * of the value. These bits are stored in the high bits [7:4] of register, but
 * they represent the low bits [3:0] of the value.
 * The value bits [11:4] are stored in the next register.
 *
 * Read data from register and convert to integer.
 */
static int bd79124_read_reg_to_int(struct bd79124_data *data, int reg,
				   unsigned int *val)
{
	int ret;
	struct bd79124_raw raw;

	ret = regmap_bulk_read(data->map, reg, &raw, sizeof(raw));
	if (ret) {
		dev_dbg(data->dev, "bulk_read failed %d\n", ret);

		return ret;
	}

	*val = BD79124_RAW_TO_INT(raw);

	return 0;
}

/*
 * The high and low limits as well as the recent result values are stored in
 * the same way in 2 consequent registers. The first register contains 4 bits
 * of the value. These bits are stored in the high bits [7:4] of register, but
 * they represent the low bits [3:0] of the value.
 * The value bits [11:4] are stored in the next register.
 *
 * Convert the integer to register format and write it using rmw cycle.
 */
static int bd79124_write_int_to_reg(struct bd79124_data *data, int reg,
				    unsigned int val)
{
	struct bd79124_raw raw = BD79124_INT_TO_RAW(val);
	unsigned int tmp;
	int ret;

	ret = regmap_read(data->map, reg, &tmp);
	if (ret)
		return ret;

	raw.val_bit3_0 |= (tmp & 0xf);

	return regmap_bulk_write(data->map, reg, &raw, sizeof(raw));
}

static const struct iio_event_spec bd79124_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
				 BIT(IIO_EV_INFO_ENABLE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
				 BIT(IIO_EV_INFO_ENABLE),
	},
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_HYSTERESIS),
	},
};

static const struct iio_chan_spec bd79124_chan_template_noirq = {
	.type = IIO_VOLTAGE,
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
	.indexed = 1,
};

static const struct iio_chan_spec bd79124_chan_template = {
	.type = IIO_VOLTAGE,
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
	.indexed = 1,
	.event_spec = bd79124_events,
	.num_event_specs = ARRAY_SIZE(bd79124_events),
};

static int bd79124_read_event_value(struct iio_dev *iio_dev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir,
				    enum iio_event_info info, int *val,
				    int *val2)
{
	struct bd79124_data *data = iio_priv(iio_dev);
	int ret, reg;

	if (chan->channel >= BD79124_MAX_NUM_CHANNELS)
		return -EINVAL;

	switch (info) {
	case IIO_EV_INFO_VALUE:
		if (dir == IIO_EV_DIR_RISING)
			*val = data->alarm_r_limit[chan->channel];
		else if (dir == IIO_EV_DIR_FALLING)
			*val = data->alarm_f_limit[chan->channel];
		else
			return -EINVAL;

		return IIO_VAL_INT;

	case IIO_EV_INFO_HYSTERESIS:
		reg = BD79124_GET_HYSTERESIS_REG(chan->channel);
		ret = regmap_read(data->map, reg, val);
		if (ret)
			return ret;

		*val &= BD79124_MSK_HYSTERESIS;
		/*
		 * The data-sheet says the hysteresis register value needs to be
		 * shifted left by 3.
		 */
		*val <<= 3;

		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static int bd79124_start_measurement(struct bd79124_data *data, int chan)
{
	unsigned int val, regval;
	int ret;

	/* See if already started */
	ret = regmap_read(data->map, BD79124_REG_AUTO_CHANNELS, &val);
	if (val & BIT(chan))
		return 0;

	/*
	 * The sequencer must be stopped when channels are added/removed from
	 * the list of the measured channels to ensure the new channel
	 * configuration is used.
	 */
	ret = regmap_clear_bits(data->map, BD79124_REG_SEQ_CFG,
				BD79124_MSK_SEQ_START);
	if (ret)
		return ret;

	ret = regmap_write(data->map, BD79124_REG_AUTO_CHANNELS, val | BIT(chan));
	if (ret)
		return ret;

	ret = regmap_set_bits(data->map, BD79124_REG_SEQ_CFG,
			      BD79124_MSK_SEQ_START);
	if (ret)
		return ret;

	/*
	 * Start the measurement at the background. Don't bother checking if
	 * it was started, regmap has cache.
	 */
	regval = FIELD_PREP(BD79124_MSK_CONV_MODE, BD79124_CONV_MODE_AUTO);

	return regmap_update_bits(data->map, BD79124_REG_OPMODE_CFG,
				BD79124_MSK_CONV_MODE, regval);
}

static int bd79124_stop_measurement(struct bd79124_data *data, int chan)
{
	unsigned int enabled_chans;
	int ret;

	/* See if already stopped */
	ret = regmap_read(data->map, BD79124_REG_AUTO_CHANNELS, &enabled_chans);
	if (!(enabled_chans & BIT(chan)))
		return 0;

	ret = regmap_clear_bits(data->map, BD79124_REG_SEQ_CFG,
				BD79124_MSK_SEQ_START);

	/* Clear the channel from the measured channels */
	enabled_chans &= ~BIT(chan);
	ret = regmap_write(data->map, BD79124_REG_AUTO_CHANNELS,
			   enabled_chans);
	if (ret)
		return ret;

	/*
	 * Stop background conversion for power saving if it was the last
	 * channel.
	 */
	if (!enabled_chans) {
		int regval = FIELD_PREP(BD79124_MSK_CONV_MODE,
					BD79124_CONV_MODE_MANSEQ);

		ret = regmap_update_bits(data->map, BD79124_REG_OPMODE_CFG,
					 BD79124_MSK_CONV_MODE, regval);
		if (ret)
			return ret;
	}

	return regmap_set_bits(data->map, BD79124_REG_SEQ_CFG,
			       BD79124_MSK_SEQ_START);
}

static int bd79124_read_event_config(struct iio_dev *iio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir)
{
	struct bd79124_data *data = iio_priv(iio_dev);

	if (chan->channel >= BD79124_MAX_NUM_CHANNELS)
		return -EINVAL;

	return !!(data->alarm_monitored[chan->channel] & BIT(dir));
}

static int bd79124_disable_event(struct bd79124_data *data,
				 enum iio_event_direction dir, int channel)
{
	int dir_bit = BIT(dir);
	int reg;
	unsigned int limit;

	guard(mutex)(&data->mutex);

	/*
	 * Set thresholds either to 0 or to 2^12 - 1 as appropriate to prevent
	 * alerts and thus disable event generation.
	 */
	if (dir == IIO_EV_DIR_RISING) {
		reg = BD79124_GET_HIGH_LIMIT_REG(channel);
		limit = BD79124_HIGH_LIMIT_MAX;
	} else if (dir == IIO_EV_DIR_FALLING) {
		reg = BD79124_GET_LOW_LIMIT_REG(channel);
		limit = BD79124_LOW_LIMIT_MIN;
	} else {
		return -EINVAL;
	}

	data->alarm_monitored[channel] &= ~dir_bit;

	/*
	 * Stop measurement if there is no more events to monitor.
	 * We don't bother checking the retval because the limit
	 * setting should in any case effectively disable the alarm.
	 */
	if (!data->alarm_monitored[channel]) {
		bd79124_stop_measurement(data, channel);
		regmap_clear_bits(data->map, BD79124_REG_ALERT_CH_SEL,
				  BIT(channel));
	}

	return bd79124_write_int_to_reg(data, reg, limit);
}

static int bd79124_enable_event(struct bd79124_data *data,
				enum iio_event_direction dir,
				unsigned int channel)
{
	int dir_bit = BIT(dir);
	int reg, ret;
	u16 *limit;

	guard(mutex)(&data->mutex);
	ret = bd79124_start_measurement(data, channel);
	if (ret)
		return ret;

	data->alarm_monitored[channel] |= dir_bit;

	/* Add the channel to the list of monitored channels */
	ret = regmap_set_bits(data->map, BD79124_REG_ALERT_CH_SEL, BIT(channel));
	if (ret)
		return ret;

	if (dir == IIO_EV_DIR_RISING) {
		limit = &data->alarm_f_limit[channel];
		reg = BD79124_GET_HIGH_LIMIT_REG(channel);
	} else {
		limit = &data->alarm_f_limit[channel];
		reg = BD79124_GET_LOW_LIMIT_REG(channel);
	}
	/*
	 * Don't write the new limit to the hardware if we are in the
	 * rate-limit period. The timer which re-enables the event will set
	 * the limit.
	 */
	if (!(data->alarm_suppressed[channel] & dir_bit)) {
		ret = bd79124_write_int_to_reg(data, reg, *limit);
		if (ret)
			return ret;
	}

	/*
	 * Enable comparator. Trust the regmap cache, no need to check
	 * if it was already enabled.
	 *
	 * We could do this in the hw-init, but there may be users who
	 * never enable alarms and for them it makes sense to not
	 * enable the comparator at probe.
	 */
	return regmap_set_bits(data->map, BD79124_REG_GEN_CFG,
				      BD79124_MSK_DWC_EN);
}

static int bd79124_write_event_config(struct iio_dev *iio_dev,
				      const struct iio_chan_spec *chan,
				      enum iio_event_type type,
				      enum iio_event_direction dir, bool state)
{
	struct bd79124_data *data = iio_priv(iio_dev);

	if (chan->channel >= BD79124_MAX_NUM_CHANNELS)
		return -EINVAL;

	if (state)
		return bd79124_enable_event(data, dir, chan->channel);

	return bd79124_disable_event(data, dir, chan->channel);
}

static int bd79124_write_event_value(struct iio_dev *iio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir,
				     enum iio_event_info info, int val,
				     int val2)
{
	struct bd79124_data *data = iio_priv(iio_dev);
	int reg;

	if (chan->channel >= BD79124_MAX_NUM_CHANNELS)
		return -EINVAL;

	switch (info) {
	case IIO_EV_INFO_VALUE:
	{
		guard(mutex)(&data->mutex);

		if (dir == IIO_EV_DIR_RISING) {
			data->alarm_r_limit[chan->channel] = val;
			reg = BD79124_GET_HIGH_LIMIT_REG(chan->channel);
		} else if (dir == IIO_EV_DIR_FALLING) {
			data->alarm_f_limit[chan->channel] = val;
			reg = BD79124_GET_LOW_LIMIT_REG(chan->channel);
		} else {
			return -EINVAL;
		}
		/*
		 * We don't want to enable the alarm if it is not enabled or
		 * if it is suppressed. In that case skip writing to the
		 * register.
		 */
		if (!(data->alarm_monitored[chan->channel] & BIT(dir)) ||
		    data->alarm_suppressed[chan->channel] & BIT(dir))
			return 0;

		return bd79124_write_int_to_reg(data, reg, val);
	}
	case IIO_EV_INFO_HYSTERESIS:
		reg = BD79124_GET_HYSTERESIS_REG(chan->channel);
		val >>= 3;

		return regmap_update_bits(data->map, reg, BD79124_MSK_HYSTERESIS,
					  val);
	default:
		return -EINVAL;
	}
}

static int bd79124_single_chan_seq(struct bd79124_data *data, int chan, unsigned int *old)
{
	int ret;

	ret = regmap_clear_bits(data->map, BD79124_REG_SEQ_CFG,
				BD79124_MSK_SEQ_START);
	if (ret)
		return ret;

	/*
	 * It may be we have some channels monitored for alarms so we want to
	 * cache the old config and return it when the single channel
	 * measurement has been completed.
	 */
	ret = regmap_read(data->map, BD79124_REG_AUTO_CHANNELS, old);
	if (ret)
		return ret;

	ret = regmap_write(data->map, BD79124_REG_AUTO_CHANNELS, BIT(chan));
	if (ret)
		return ret;

	/* Restart the sequencer */
	return regmap_set_bits(data->map, BD79124_REG_SEQ_CFG,
			       BD79124_MSK_SEQ_START);
}

static int bd79124_single_chan_seq_end(struct bd79124_data *data, unsigned int old)
{
	int ret;

	ret = regmap_clear_bits(data->map, BD79124_REG_SEQ_CFG,
				BD79124_MSK_SEQ_START);
	if (ret)
		return ret;

	ret = regmap_write(data->map, BD79124_REG_AUTO_CHANNELS, old);
	if (ret)
		return ret;

	return regmap_set_bits(data->map, BD79124_REG_SEQ_CFG,
			       BD79124_MSK_SEQ_START);
}

static int bd79124_read_raw(struct iio_dev *iio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long m)
{
	struct bd79124_data *data = iio_priv(iio_dev);
	int ret;

	if (chan->channel >= BD79124_MAX_NUM_CHANNELS)
		return -EINVAL;

	switch (m) {
	case IIO_CHAN_INFO_RAW:
	{
		unsigned int old_chan_cfg, regval;
		int tmp;

		guard(mutex)(&data->mutex);

		/*
		 * Start the automatic conversion. This is needed here if no
		 * events have been enabled.
		 */
		regval = FIELD_PREP(BD79124_MSK_CONV_MODE,
				    BD79124_CONV_MODE_AUTO);
		ret = regmap_update_bits(data->map, BD79124_REG_OPMODE_CFG,
					 BD79124_MSK_CONV_MODE, regval);
		if (ret)
			return ret;

		ret = bd79124_single_chan_seq(data, chan->channel, &old_chan_cfg);
		if (ret)
			return ret;

		/* The maximum conversion time is 6 uS. */
		udelay(6);

		ret = bd79124_read_reg_to_int(data,
			BD79124_GET_RECENT_RES_REG(chan->channel), val);
		/*
		 * Return the old chan config even if data reading failed in
		 * order to re-enable the event monitoring.
		 */
		tmp = bd79124_single_chan_seq_end(data, old_chan_cfg);
		if (tmp)
			dev_err(data->dev,
				"Failed to return config. Alarms may be disabled\n");

		if (ret)
			return ret;

		return IIO_VAL_INT;
	}
	case IIO_CHAN_INFO_SCALE:
		*val = data->vmax / 1000;
		*val2 = BD79124_ADC_BITS;
		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		return -EINVAL;
	}
}

static const struct iio_info bd79124_info = {
	.read_raw = bd79124_read_raw,
	.read_event_config = &bd79124_read_event_config,
	.write_event_config = &bd79124_write_event_config,
	.read_event_value = &bd79124_read_event_value,
	.write_event_value = &bd79124_write_event_value,
};

static void bd79124_re_enable_lo(struct bd79124_data *data, unsigned int channel)
{
	int ret, evbit = BIT(IIO_EV_DIR_FALLING);

	/*
	 * We should not re-enable the event if user has disabled it while
	 * rate-limiting was enabled.
	 */
	if (!(data->alarm_suppressed[channel] & evbit))
		return;

	data->alarm_suppressed[channel] &= ~evbit;

	if (!(data->alarm_monitored[channel] & evbit))
		return;

	ret = bd79124_write_int_to_reg(data, BD79124_GET_LOW_LIMIT_REG(channel),
				       data->alarm_f_limit[channel]);
	if (ret)
		dev_warn(data->dev, "Low limit enabling failed for channel%d\n",
			 channel);
}

static void bd79124_re_enable_hi(struct bd79124_data *data, unsigned int channel)
{
	int ret, evbit = BIT(IIO_EV_DIR_RISING);

	/*
	 * We should not re-enable the event if user has disabled it while
	 * rate-limiting was enabled.
	 */
	if (!(data->alarm_suppressed[channel] & evbit))
		return;

	data->alarm_suppressed[channel] &= ~evbit;

	if (!(data->alarm_monitored[channel] & evbit))
		return;

	ret = bd79124_write_int_to_reg(data, BD79124_GET_HIGH_LIMIT_REG(channel),
				       data->alarm_r_limit[channel]);
	if (ret)
		dev_warn(data->dev, "High limit enabling failed for channel%d\n",
			 channel);
}

static void bd79124_alm_enable_worker(struct work_struct *work)
{
	int i;
	struct bd79124_data *data = container_of(work, struct bd79124_data,
						 alm_enable_work.work);

	/* Take the mutex so there is no race with user disabling the alarm */
	guard(mutex)(&data->mutex);
	for (i = 0; i < BD79124_MAX_NUM_CHANNELS; i++) {
		bd79124_re_enable_hi(data, i);
		bd79124_re_enable_lo(data, i);
	}
}

static int __bd79124_event_ratelimit(struct bd79124_data *data, int reg,
				     unsigned int limit)
{
	int ret;

	if (limit > BD79124_HIGH_LIMIT_MAX)
		return -EINVAL;

	ret = bd79124_write_int_to_reg(data, reg, limit);
	if (ret)
		return ret;

	/*
	 * We use 1 sec 'grace period'. At the moment I see no reason to make
	 * this user configurable. We need an ABI for this if configuration is
	 * needed.
	 */
	schedule_delayed_work(&data->alm_enable_work, msecs_to_jiffies(1000));

	return 0;
}

static int bd79124_event_ratelimit_hi(struct bd79124_data *data,
				      unsigned int channel)
{
	guard(mutex)(&data->mutex);
	data->alarm_suppressed[channel] |= BIT(IIO_EV_DIR_RISING);

	return __bd79124_event_ratelimit(data,
					 BD79124_GET_HIGH_LIMIT_REG(channel),
					 BD79124_HIGH_LIMIT_MAX);
}

static int bd79124_event_ratelimit_lo(struct bd79124_data *data,
				      unsigned int channel)
{
	guard(mutex)(&data->mutex);
	data->alarm_suppressed[channel] |= BIT(IIO_EV_DIR_FALLING);

	return __bd79124_event_ratelimit(data,
					 BD79124_GET_LOW_LIMIT_REG(channel),
					 BD79124_LOW_LIMIT_MIN);
}

static irqreturn_t bd79124_event_handler(int irq, void *priv)
{
	unsigned int i_hi, i_lo;
	int i, ret;
	struct iio_dev *iio_dev = priv;
	struct bd79124_data *data = iio_priv(iio_dev);

	/*
	 * Return IRQ_NONE if bailing-out without acking. This allows the IRQ
	 * subsystem to disable the offending IRQ line if we get a hardware
	 * problem. This behaviour has saved my poor bottom a few times in the
	 * past as, instead of getting unusably unresponsive, the system has
	 * spilled out the magic words "...nobody cared".
	 */
	ret = regmap_read(data->map, BD79124_REG_EVENT_FLAG_HI, &i_hi);
	if (ret)
		return IRQ_NONE;

	ret = regmap_read(data->map, BD79124_REG_EVENT_FLAG_LO, &i_lo);
	if (ret)
		return IRQ_NONE;

	if (!i_lo && !i_hi)
		return IRQ_NONE;

	for (i = 0; i < BD79124_MAX_NUM_CHANNELS; i++) {
		u64 ecode;

		if (BIT(i) & i_hi) {
			ecode = IIO_UNMOD_EVENT_CODE(IIO_VOLTAGE, i,
						     IIO_EV_TYPE_THRESH,
						     IIO_EV_DIR_RISING);

			iio_push_event(iio_dev, ecode, data->timestamp);
			/*
			 * The BD79124 keeps the IRQ asserted for as long as
			 * the voltage exceeds the threshold. It causes the IRQ
			 * to keep firing.
			 *
			 * Disable the event for the channel and schedule the
			 * re-enabling the event later to prevent storm of
			 * events.
			 */
			ret = bd79124_event_ratelimit_hi(data, i);
			if (ret)
				return IRQ_NONE;
		}
		if (BIT(i) & i_lo) {
			ecode = IIO_UNMOD_EVENT_CODE(IIO_VOLTAGE, i,
						     IIO_EV_TYPE_THRESH,
						     IIO_EV_DIR_FALLING);

			iio_push_event(iio_dev, ecode, data->timestamp);
			ret = bd79124_event_ratelimit_lo(data, i);
			if (ret)
				return IRQ_NONE;
		}
	}

	ret = regmap_write(data->map, BD79124_REG_EVENT_FLAG_HI, i_hi);
	if (ret)
		return IRQ_NONE;

	ret = regmap_write(data->map, BD79124_REG_EVENT_FLAG_LO, i_lo);
	if (ret)
		return IRQ_NONE;

	return IRQ_HANDLED;
}

static irqreturn_t bd79124_irq_handler(int irq, void *priv)
{
	struct iio_dev *iio_dev = priv;
	struct bd79124_data *data = iio_priv(iio_dev);

	data->timestamp = iio_get_time_ns(iio_dev);

	return IRQ_WAKE_THREAD;
}

static int bd79124_chan_init(struct bd79124_data *data, int channel)
{
	int ret;

	ret = regmap_write(data->map, BD79124_GET_HIGH_LIMIT_REG(channel),
			   BD79124_HIGH_LIMIT_MAX);
	if (ret)
		return ret;

	return regmap_write(data->map, BD79124_GET_LOW_LIMIT_REG(channel),
			    BD79124_LOW_LIMIT_MIN);
}

static int bd79124_get_gpio_pins(const struct iio_chan_spec *cs, int num_channels)
{
	int i, gpio_channels;

	/*
	 * Let's initialize the mux config to say that all 8 channels are
	 * GPIOs. Then we can just loop through the iio_chan_spec and clear the
	 * bits for found ADC channels.
	 */
	gpio_channels = GENMASK(7, 0);
	for (i = 0; i < num_channels; i++)
		gpio_channels &= ~BIT(cs[i].channel);

	return gpio_channels;
}

static int bd79124_hw_init(struct bd79124_data *data)
{
	unsigned int regval;
	int ret, i;

	for (i = 0; i < BD79124_MAX_NUM_CHANNELS; i++) {
		ret = bd79124_chan_init(data, i);
		if (ret)
			return ret;
		data->alarm_r_limit[i] = BD79124_HIGH_LIMIT_MAX;
	}
	/* Stop auto sequencer */
	ret = regmap_clear_bits(data->map, BD79124_REG_SEQ_CFG,
				BD79124_MSK_SEQ_START);
	if (ret)
		return ret;

	/* Enable writing the measured values to the regsters */
	ret = regmap_set_bits(data->map, BD79124_REG_GEN_CFG,
			      BD79124_MSK_STATS_EN);
	if (ret)
		return ret;

	/* Set no channels to be auto-measured */
	ret = regmap_write(data->map, BD79124_REG_AUTO_CHANNELS, 0x0);
	if (ret)
		return ret;

	/* Set no channels to be manually measured */
	ret = regmap_write(data->map, BD79124_REG_MANUAL_CHANNELS, 0x0);
	if (ret)
		return ret;

	regval = FIELD_PREP(BD79124_MSK_AUTO_INTERVAL, BD79124_INTERVAL_750_US);
	ret = regmap_update_bits(data->map, BD79124_REG_OPMODE_CFG,
				 BD79124_MSK_AUTO_INTERVAL, regval);
	if (ret)
		return ret;

	/* Sequencer mode to auto */
	ret = regmap_set_bits(data->map, BD79124_REG_SEQ_CFG,
			      BD79124_MSK_SEQ_SEQ);
	if (ret)
		return ret;

	/* Don't start the measurement */
	regval = FIELD_PREP(BD79124_MSK_CONV_MODE, BD79124_CONV_MODE_MANSEQ);
	return regmap_update_bits(data->map, BD79124_REG_OPMODE_CFG,
				  BD79124_MSK_CONV_MODE, regval);
}

static int bd79124_probe(struct i2c_client *i2c)
{
	struct bd79124_data *data;
	struct iio_dev *iio_dev;
	const struct iio_chan_spec *template;
	struct iio_chan_spec *cs;
	struct device *dev = &i2c->dev;
	unsigned int gpio_pins;
	int ret;

	iio_dev = devm_iio_device_alloc(dev, sizeof(*data));
	if (!iio_dev)
		return -ENOMEM;

	data = iio_priv(iio_dev);
	data->dev = dev;
	data->map = devm_regmap_init_i2c(i2c, &bd79124_regmap);
	if (IS_ERR(data->map))
		return dev_err_probe(dev, PTR_ERR(data->map),
				     "Failed to initialize Regmap\n");

	ret = devm_regulator_get_enable_read_voltage(dev, "vdd");
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get the Vdd\n");

	data->vmax = ret;

	ret = devm_regulator_get_enable(dev, "iovdd");
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to enable I/O voltage\n");

	ret = devm_delayed_work_autocancel(dev, &data->alm_enable_work,
					   bd79124_alm_enable_worker);
	if (ret)
		return ret;

	if (i2c->irq) {
		template = &bd79124_chan_template;
	} else {
		template = &bd79124_chan_template_noirq;
		dev_dbg(dev, "No IRQ found, events disabled\n");
	}

	ret = devm_mutex_init(dev, &data->mutex);
	if (ret)
		return ret;

	ret = devm_iio_adc_device_alloc_chaninfo_se(dev, template,
		BD79124_MAX_NUM_CHANNELS - 1, &cs);
	if (ret < 0) {
		/* Register all pins as GPOs if there are no ADC channels */
		if (ret == -ENOENT)
			goto register_gpios;
		return ret;
	}
	iio_dev->channels = cs;
	iio_dev->num_channels = ret;
	iio_dev->info = &bd79124_info;
	iio_dev->name = "bd79124";
	iio_dev->modes = INDIO_DIRECT_MODE;

	ret = bd79124_hw_init(data);
	if (ret)
		return ret;

	if (i2c->irq > 0) {
		ret = devm_request_threaded_irq(dev, i2c->irq,
			bd79124_irq_handler, &bd79124_event_handler,
			IRQF_ONESHOT, "adc-thresh-alert", iio_dev);
		if (ret)
			return dev_err_probe(data->dev, ret,
					     "Failed to register IRQ\n");
	}

	ret = devm_iio_device_register(data->dev, iio_dev);
	if (ret)
		return dev_err_probe(data->dev, ret, "Failed to register ADC\n");

register_gpios:
	gpio_pins = bd79124_get_gpio_pins(iio_dev->channels,
					  iio_dev->num_channels);

	/*
	 * The mux should default to "all ADCs", but better to not trust it.
	 * Thus we do set the mux even when we have only ADCs and no GPOs.
	 */
	ret = regmap_write(data->map, BD79124_REG_PINCFG, gpio_pins);
	if (ret)
		return ret;

	/* No GPOs if all channels are reserved for ADC, so we're done. */
	if (!gpio_pins)
		return 0;

	data->gpio_valid_mask = gpio_pins;
	data->gc = bd79124gpo_chip;
	data->gc.parent = dev;

	return devm_gpiochip_add_data(dev, &data->gc, data);
}

static const struct of_device_id bd79124_of_match[] = {
	{ .compatible = "rohm,bd79124" },
	{ }
};
MODULE_DEVICE_TABLE(of, bd79124_of_match);

static const struct i2c_device_id bd79124_id[] = {
	{ "bd79124" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bd79124_id);

static struct i2c_driver bd79124_driver = {
	.driver = {
		.name = "bd79124",
		.of_match_table = bd79124_of_match,
	},
	.probe = bd79124_probe,
	.id_table = bd79124_id,
};
module_i2c_driver(bd79124_driver);

MODULE_AUTHOR("Matti Vaittinen <mazziesaccount@gmail.com>");
MODULE_DESCRIPTION("Driver for ROHM BD79124 ADC");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_DRIVER");
