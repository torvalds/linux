// SPDX-License-Identifier: GPL-2.0-only
/*
 * palmas-adc.c -- TI PALMAS GPADC.
 *
 * Copyright (c) 2013, NVIDIA Corporation. All rights reserved.
 *
 * Author: Pradeep Goudagunta <pgoudagunta@nvidia.com>
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/pm.h>
#include <linux/mfd/palmas.h>
#include <linux/completion.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/machine.h>
#include <linux/iio/driver.h>

#define MOD_NAME "palmas-gpadc"
#define PALMAS_ADC_CONVERSION_TIMEOUT	(msecs_to_jiffies(5000))
#define PALMAS_TO_BE_CALCULATED 0
#define PALMAS_GPADC_TRIMINVALID	-1

struct palmas_gpadc_info {
/* calibration codes and regs */
	int x1;	/* lower ideal code */
	int x2;	/* higher ideal code */
	int v1;	/* expected lower volt reading */
	int v2;	/* expected higher volt reading */
	u8 trim1_reg;	/* register number for lower trim */
	u8 trim2_reg;	/* register number for upper trim */
	int gain;	/* calculated from above (after reading trim regs) */
	int offset;	/* calculated from above (after reading trim regs) */
	int gain_error;	/* calculated from above (after reading trim regs) */
	bool is_uncalibrated;	/* if channel has calibration data */
};

#define PALMAS_ADC_INFO(_chan, _x1, _x2, _v1, _v2, _t1, _t2, _is_uncalibrated) \
	[PALMAS_ADC_CH_##_chan] = { \
		.x1 = _x1, \
		.x2 = _x2, \
		.v1 = _v1, \
		.v2 = _v2, \
		.gain = PALMAS_TO_BE_CALCULATED, \
		.offset = PALMAS_TO_BE_CALCULATED, \
		.gain_error = PALMAS_TO_BE_CALCULATED, \
		.trim1_reg = PALMAS_GPADC_TRIM##_t1, \
		.trim2_reg = PALMAS_GPADC_TRIM##_t2,  \
		.is_uncalibrated = _is_uncalibrated \
	}

static struct palmas_gpadc_info palmas_gpadc_info[] = {
	PALMAS_ADC_INFO(IN0, 2064, 3112, 630, 950, 1, 2, false),
	PALMAS_ADC_INFO(IN1, 2064, 3112, 630, 950, 1, 2, false),
	PALMAS_ADC_INFO(IN2, 2064, 3112, 1260, 1900, 3, 4, false),
	PALMAS_ADC_INFO(IN3, 2064, 3112, 630, 950, 1, 2, false),
	PALMAS_ADC_INFO(IN4, 2064, 3112, 630, 950, 1, 2, false),
	PALMAS_ADC_INFO(IN5, 2064, 3112, 630, 950, 1, 2, false),
	PALMAS_ADC_INFO(IN6, 2064, 3112, 2520, 3800, 5, 6, false),
	PALMAS_ADC_INFO(IN7, 2064, 3112, 2520, 3800, 7, 8, false),
	PALMAS_ADC_INFO(IN8, 2064, 3112, 3150, 4750, 9, 10, false),
	PALMAS_ADC_INFO(IN9, 2064, 3112, 5670, 8550, 11, 12, false),
	PALMAS_ADC_INFO(IN10, 2064, 3112, 3465, 5225, 13, 14, false),
	PALMAS_ADC_INFO(IN11, 0, 0, 0, 0, INVALID, INVALID, true),
	PALMAS_ADC_INFO(IN12, 0, 0, 0, 0, INVALID, INVALID, true),
	PALMAS_ADC_INFO(IN13, 0, 0, 0, 0, INVALID, INVALID, true),
	PALMAS_ADC_INFO(IN14, 2064, 3112, 3645, 5225, 15, 16, false),
	PALMAS_ADC_INFO(IN15, 0, 0, 0, 0, INVALID, INVALID, true),
};

struct palmas_adc_event {
	bool enabled;
	int channel;
	enum iio_event_direction direction;
};

struct palmas_gpadc_thresholds {
	int high;
	int low;
};

/*
 * struct palmas_gpadc - the palmas_gpadc structure
 * @ch0_current:	channel 0 current source setting
 *			0: 0 uA
 *			1: 5 uA
 *			2: 15 uA
 *			3: 20 uA
 * @ch3_current:	channel 0 current source setting
 *			0: 0 uA
 *			1: 10 uA
 *			2: 400 uA
 *			3: 800 uA
 * @extended_delay:	enable the gpadc extended delay mode
 * @auto_conversion_period:	define the auto_conversion_period
 * @lock:	Lock to protect the device state during a potential concurrent
 *		read access from userspace. Reading a raw value requires a sequence
 *		of register writes, then a wait for a completion callback,
 *		and finally a register read, during which userspace could issue
 *		another read request. This lock protects a read access from
 *		ocurring before another one has finished.
 *
 * This is the palmas_gpadc structure to store run-time information
 * and pointers for this driver instance.
 */
struct palmas_gpadc {
	struct device			*dev;
	struct palmas			*palmas;
	u8				ch0_current;
	u8				ch3_current;
	bool				extended_delay;
	int				irq;
	int				irq_auto_0;
	int				irq_auto_1;
	struct palmas_gpadc_info	*adc_info;
	struct completion		conv_completion;
	struct palmas_adc_event		event0;
	struct palmas_adc_event		event1;
	struct palmas_gpadc_thresholds	thresholds[PALMAS_ADC_CH_MAX];
	int				auto_conversion_period;
	struct mutex			lock;
};

static struct palmas_adc_event *palmas_gpadc_get_event(struct palmas_gpadc *adc,
						       int adc_chan,
						       enum iio_event_direction dir)
{
	if (adc_chan == adc->event0.channel && dir == adc->event0.direction)
		return &adc->event0;

	if (adc_chan == adc->event1.channel && dir == adc->event1.direction)
		return &adc->event1;

	return NULL;
}

static bool palmas_gpadc_channel_is_freerunning(struct palmas_gpadc *adc,
						int adc_chan)
{
	return palmas_gpadc_get_event(adc, adc_chan, IIO_EV_DIR_RISING) ||
		palmas_gpadc_get_event(adc, adc_chan, IIO_EV_DIR_FALLING);
}

/*
 * GPADC lock issue in AUTO mode.
 * Impact: In AUTO mode, GPADC conversion can be locked after disabling AUTO
 *	   mode feature.
 * Details:
 *	When the AUTO mode is the only conversion mode enabled, if the AUTO
 *	mode feature is disabled with bit GPADC_AUTO_CTRL.  AUTO_CONV1_EN = 0
 *	or bit GPADC_AUTO_CTRL.  AUTO_CONV0_EN = 0 during a conversion, the
 *	conversion mechanism can be seen as locked meaning that all following
 *	conversion will give 0 as a result.  Bit GPADC_STATUS.GPADC_AVAILABLE
 *	will stay at 0 meaning that GPADC is busy.  An RT conversion can unlock
 *	the GPADC.
 *
 * Workaround(s):
 *	To avoid the lock mechanism, the workaround to follow before any stop
 *	conversion request is:
 *	Force the GPADC state machine to be ON by using the GPADC_CTRL1.
 *		GPADC_FORCE bit = 1
 *	Shutdown the GPADC AUTO conversion using
 *		GPADC_AUTO_CTRL.SHUTDOWN_CONV[01] = 0.
 *	After 100us, force the GPADC state machine to be OFF by using the
 *		GPADC_CTRL1.  GPADC_FORCE bit = 0
 */

static int palmas_disable_auto_conversion(struct palmas_gpadc *adc)
{
	int ret;

	ret = palmas_update_bits(adc->palmas, PALMAS_GPADC_BASE,
			PALMAS_GPADC_CTRL1,
			PALMAS_GPADC_CTRL1_GPADC_FORCE,
			PALMAS_GPADC_CTRL1_GPADC_FORCE);
	if (ret < 0) {
		dev_err(adc->dev, "GPADC_CTRL1 update failed: %d\n", ret);
		return ret;
	}

	ret = palmas_update_bits(adc->palmas, PALMAS_GPADC_BASE,
			PALMAS_GPADC_AUTO_CTRL,
			PALMAS_GPADC_AUTO_CTRL_SHUTDOWN_CONV1 |
			PALMAS_GPADC_AUTO_CTRL_SHUTDOWN_CONV0,
			0);
	if (ret < 0) {
		dev_err(adc->dev, "AUTO_CTRL update failed: %d\n", ret);
		return ret;
	}

	udelay(100);

	ret = palmas_update_bits(adc->palmas, PALMAS_GPADC_BASE,
			PALMAS_GPADC_CTRL1,
			PALMAS_GPADC_CTRL1_GPADC_FORCE, 0);
	if (ret < 0)
		dev_err(adc->dev, "GPADC_CTRL1 update failed: %d\n", ret);

	return ret;
}

static irqreturn_t palmas_gpadc_irq(int irq, void *data)
{
	struct palmas_gpadc *adc = data;

	complete(&adc->conv_completion);

	return IRQ_HANDLED;
}

static irqreturn_t palmas_gpadc_irq_auto(int irq, void *data)
{
	struct iio_dev *indio_dev = data;
	struct palmas_gpadc *adc = iio_priv(indio_dev);
	struct palmas_adc_event *ev;

	dev_dbg(adc->dev, "Threshold interrupt %d occurs\n", irq);
	palmas_disable_auto_conversion(adc);

	ev = (irq == adc->irq_auto_0) ? &adc->event0 : &adc->event1;
	if (ev->channel != -1) {
		enum iio_event_direction dir;
		u64 code;

		dir = ev->direction;
		code = IIO_UNMOD_EVENT_CODE(IIO_VOLTAGE, ev->channel,
					    IIO_EV_TYPE_THRESH, dir);
		iio_push_event(indio_dev, code, iio_get_time_ns(indio_dev));
	}

	return IRQ_HANDLED;
}

static int palmas_gpadc_start_mask_interrupt(struct palmas_gpadc *adc,
						bool mask)
{
	int ret;

	if (!mask)
		ret = palmas_update_bits(adc->palmas, PALMAS_INTERRUPT_BASE,
					PALMAS_INT3_MASK,
					PALMAS_INT3_MASK_GPADC_EOC_SW, 0);
	else
		ret = palmas_update_bits(adc->palmas, PALMAS_INTERRUPT_BASE,
					PALMAS_INT3_MASK,
					PALMAS_INT3_MASK_GPADC_EOC_SW,
					PALMAS_INT3_MASK_GPADC_EOC_SW);
	if (ret < 0)
		dev_err(adc->dev, "GPADC INT MASK update failed: %d\n", ret);

	return ret;
}

static int palmas_gpadc_enable(struct palmas_gpadc *adc, int adc_chan,
			       int enable)
{
	unsigned int mask, val;
	int ret;

	if (enable) {
		val = (adc->extended_delay
			<< PALMAS_GPADC_RT_CTRL_EXTEND_DELAY_SHIFT);
		ret = palmas_update_bits(adc->palmas, PALMAS_GPADC_BASE,
					PALMAS_GPADC_RT_CTRL,
					PALMAS_GPADC_RT_CTRL_EXTEND_DELAY, val);
		if (ret < 0) {
			dev_err(adc->dev, "RT_CTRL update failed: %d\n", ret);
			return ret;
		}

		mask = (PALMAS_GPADC_CTRL1_CURRENT_SRC_CH0_MASK |
			PALMAS_GPADC_CTRL1_CURRENT_SRC_CH3_MASK |
			PALMAS_GPADC_CTRL1_GPADC_FORCE);
		val = (adc->ch0_current
			<< PALMAS_GPADC_CTRL1_CURRENT_SRC_CH0_SHIFT);
		val |= (adc->ch3_current
			<< PALMAS_GPADC_CTRL1_CURRENT_SRC_CH3_SHIFT);
		val |= PALMAS_GPADC_CTRL1_GPADC_FORCE;
		ret = palmas_update_bits(adc->palmas, PALMAS_GPADC_BASE,
				PALMAS_GPADC_CTRL1, mask, val);
		if (ret < 0) {
			dev_err(adc->dev,
				"Failed to update current setting: %d\n", ret);
			return ret;
		}

		mask = (PALMAS_GPADC_SW_SELECT_SW_CONV0_SEL_MASK |
			PALMAS_GPADC_SW_SELECT_SW_CONV_EN);
		val = (adc_chan | PALMAS_GPADC_SW_SELECT_SW_CONV_EN);
		ret = palmas_update_bits(adc->palmas, PALMAS_GPADC_BASE,
				PALMAS_GPADC_SW_SELECT, mask, val);
		if (ret < 0) {
			dev_err(adc->dev, "SW_SELECT update failed: %d\n", ret);
			return ret;
		}
	} else {
		ret = palmas_write(adc->palmas, PALMAS_GPADC_BASE,
				PALMAS_GPADC_SW_SELECT, 0);
		if (ret < 0)
			dev_err(adc->dev, "SW_SELECT write failed: %d\n", ret);

		ret = palmas_update_bits(adc->palmas, PALMAS_GPADC_BASE,
				PALMAS_GPADC_CTRL1,
				PALMAS_GPADC_CTRL1_GPADC_FORCE, 0);
		if (ret < 0) {
			dev_err(adc->dev, "CTRL1 update failed: %d\n", ret);
			return ret;
		}
	}

	return ret;
}

static int palmas_gpadc_read_prepare(struct palmas_gpadc *adc, int adc_chan)
{
	int ret;

	if (palmas_gpadc_channel_is_freerunning(adc, adc_chan))
		return 0; /* ADC already running */

	ret = palmas_gpadc_enable(adc, adc_chan, true);
	if (ret < 0)
		return ret;

	return palmas_gpadc_start_mask_interrupt(adc, 0);
}

static void palmas_gpadc_read_done(struct palmas_gpadc *adc, int adc_chan)
{
	palmas_gpadc_start_mask_interrupt(adc, 1);
	palmas_gpadc_enable(adc, adc_chan, false);
}

static int palmas_gpadc_calibrate(struct palmas_gpadc *adc, int adc_chan)
{
	int k;
	int d1;
	int d2;
	int ret;
	int gain;
	int x1 =  adc->adc_info[adc_chan].x1;
	int x2 =  adc->adc_info[adc_chan].x2;
	int v1 = adc->adc_info[adc_chan].v1;
	int v2 = adc->adc_info[adc_chan].v2;

	ret = palmas_read(adc->palmas, PALMAS_TRIM_GPADC_BASE,
				adc->adc_info[adc_chan].trim1_reg, &d1);
	if (ret < 0) {
		dev_err(adc->dev, "TRIM read failed: %d\n", ret);
		goto scrub;
	}

	ret = palmas_read(adc->palmas, PALMAS_TRIM_GPADC_BASE,
				adc->adc_info[adc_chan].trim2_reg, &d2);
	if (ret < 0) {
		dev_err(adc->dev, "TRIM read failed: %d\n", ret);
		goto scrub;
	}

	/* gain error calculation */
	k = (1000 + (1000 * (d2 - d1)) / (x2 - x1));

	/* gain calculation */
	gain = ((v2 - v1) * 1000) / (x2 - x1);

	adc->adc_info[adc_chan].gain_error = k;
	adc->adc_info[adc_chan].gain = gain;
	/* offset Calculation */
	adc->adc_info[adc_chan].offset = (d1 * 1000) - ((k - 1000) * x1);

scrub:
	return ret;
}

static int palmas_gpadc_start_conversion(struct palmas_gpadc *adc, int adc_chan)
{
	unsigned int val;
	int ret;

	if (palmas_gpadc_channel_is_freerunning(adc, adc_chan)) {
		int event = (adc_chan == adc->event0.channel) ? 0 : 1;
		unsigned int reg = (event == 0) ?
			PALMAS_GPADC_AUTO_CONV0_LSB :
			PALMAS_GPADC_AUTO_CONV1_LSB;

		ret = palmas_bulk_read(adc->palmas, PALMAS_GPADC_BASE,
					reg, &val, 2);
		if (ret < 0) {
			dev_err(adc->dev, "AUTO_CONV%x_LSB read failed: %d\n",
				event, ret);
			return ret;
		}
	} else {
		init_completion(&adc->conv_completion);
		ret = palmas_update_bits(adc->palmas, PALMAS_GPADC_BASE,
					PALMAS_GPADC_SW_SELECT,
					PALMAS_GPADC_SW_SELECT_SW_START_CONV0,
					PALMAS_GPADC_SW_SELECT_SW_START_CONV0);
		if (ret < 0) {
			dev_err(adc->dev, "SELECT_SW_START write failed: %d\n", ret);
			return ret;
		}

		ret = wait_for_completion_timeout(&adc->conv_completion,
					PALMAS_ADC_CONVERSION_TIMEOUT);
		if (ret == 0) {
			dev_err(adc->dev, "conversion not completed\n");
			return -ETIMEDOUT;
		}

		ret = palmas_bulk_read(adc->palmas, PALMAS_GPADC_BASE,
					PALMAS_GPADC_SW_CONV0_LSB, &val, 2);
		if (ret < 0) {
			dev_err(adc->dev, "SW_CONV0_LSB read failed: %d\n", ret);
			return ret;
		}
	}

	ret = val & 0xFFF;

	return ret;
}

static int palmas_gpadc_get_calibrated_code(struct palmas_gpadc *adc,
						int adc_chan, int val)
{
	if (!adc->adc_info[adc_chan].is_uncalibrated)
		val  = (val*1000 - adc->adc_info[adc_chan].offset) /
					adc->adc_info[adc_chan].gain_error;

	if (val < 0) {
		if (val < -10)
			dev_err(adc->dev, "Mismatch with calibration var = %d\n", val);
		return 0;
	}

	val = (val * adc->adc_info[adc_chan].gain) / 1000;

	return val;
}

/*
 * The high and low threshold values are calculated based on the advice given
 * in TI Application Report SLIA087A, "Guide to Using the GPADC in PS65903x,
 * TPS65917-Q1, TPS65919-Q1, and TPS65916 Devices". This document recommend
 * taking ADC tolerances into account and is based on the device integral non-
 * linearity (INL), offset error and gain error:
 *
 *   raw high threshold = (ideal threshold + INL) * gain error + offset error
 *
 * The gain error include both gain error, as specified in the datasheet, and
 * the gain error drift. These paramenters vary depending on device and whether
 * the the channel is calibrated (trimmed) or not.
 */
static int palmas_gpadc_threshold_with_tolerance(int val, const int INL,
						 const int gain_error,
						 const int offset_error)
{
	val = ((val + INL) * (1000 + gain_error)) / 1000 + offset_error;

	return clamp(val, 0, 0xFFF);
}

/*
 * The values below are taken from the datasheet of TWL6035, TWL6037.
 * todo: get max INL, gain error, and offset error from OF.
 */
static int palmas_gpadc_get_high_threshold_raw(struct palmas_gpadc *adc,
					       struct palmas_adc_event *ev)
{
	const int adc_chan = ev->channel;
	int val = adc->thresholds[adc_chan].high;
	/* integral nonlinearity, measured in LSB */
	const int max_INL = 2;
	/* measured in LSB */
	int max_offset_error;
	/* 0.2% when calibrated */
	int max_gain_error = 2;

	val = (val * 1000) / adc->adc_info[adc_chan].gain;

	if (adc->adc_info[adc_chan].is_uncalibrated) {
		/* 2% worse */
		max_gain_error += 20;
		max_offset_error = 36;
	} else {
		val = (val * adc->adc_info[adc_chan].gain_error +
		       adc->adc_info[adc_chan].offset) /
			1000;
		max_offset_error = 2;
	}

	return palmas_gpadc_threshold_with_tolerance(val,
						     max_INL,
						     max_gain_error,
						     max_offset_error);
}

/*
 * The values below are taken from the datasheet of TWL6035, TWL6037.
 * todo: get min INL, gain error, and offset error from OF.
 */
static int palmas_gpadc_get_low_threshold_raw(struct palmas_gpadc *adc,
					      struct palmas_adc_event *ev)
{
	const int adc_chan = ev->channel;
	int val = adc->thresholds[adc_chan].low;
	/* integral nonlinearity, measured in LSB */
	const int min_INL = -2;
	/* measured in LSB */
	int min_offset_error;
	/* -0.6% when calibrated */
	int min_gain_error = -6;

	val = (val * 1000) / adc->adc_info[adc_chan].gain;

        if (adc->adc_info[adc_chan].is_uncalibrated) {
		/* 2% worse */
		min_gain_error -= 20;
		min_offset_error = -36;
        } else {
		val = (val * adc->adc_info[adc_chan].gain_error -
		       adc->adc_info[adc_chan].offset) /
			1000;
		min_offset_error = -2;
        }

	return palmas_gpadc_threshold_with_tolerance(val,
						     min_INL,
						     min_gain_error,
						     min_offset_error);
}

static int palmas_gpadc_read_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *chan, int *val, int *val2, long mask)
{
	struct  palmas_gpadc *adc = iio_priv(indio_dev);
	int adc_chan = chan->channel;
	int ret = 0;

	if (adc_chan >= PALMAS_ADC_CH_MAX)
		return -EINVAL;

	mutex_lock(&adc->lock);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
	case IIO_CHAN_INFO_PROCESSED:
		ret = palmas_gpadc_read_prepare(adc, adc_chan);
		if (ret < 0)
			goto out;

		ret = palmas_gpadc_start_conversion(adc, adc_chan);
		if (ret < 0) {
			dev_err(adc->dev,
			"ADC start conversion failed\n");
			goto out;
		}

		if (mask == IIO_CHAN_INFO_PROCESSED)
			ret = palmas_gpadc_get_calibrated_code(
							adc, adc_chan, ret);

		*val = ret;

		ret = IIO_VAL_INT;
		goto out;
	}

	mutex_unlock(&adc->lock);
	return ret;

out:
	palmas_gpadc_read_done(adc, adc_chan);
	mutex_unlock(&adc->lock);

	return ret;
}

static int palmas_gpadc_read_event_config(struct iio_dev *indio_dev,
					  const struct iio_chan_spec *chan,
					  enum iio_event_type type,
					  enum iio_event_direction dir)
{
	struct palmas_gpadc *adc = iio_priv(indio_dev);
	int adc_chan = chan->channel;
	int ret = 0;

	if (adc_chan >= PALMAS_ADC_CH_MAX || type != IIO_EV_TYPE_THRESH)
		return -EINVAL;

	mutex_lock(&adc->lock);

	if (palmas_gpadc_get_event(adc, adc_chan, dir))
		ret = 1;

	mutex_unlock(&adc->lock);

	return ret;
}

static int palmas_adc_configure_events(struct palmas_gpadc *adc);
static int palmas_adc_reset_events(struct palmas_gpadc *adc);

static int palmas_gpadc_reconfigure_event_channels(struct palmas_gpadc *adc)
{
	return (adc->event0.enabled || adc->event1.enabled) ?
		palmas_adc_configure_events(adc) :
		palmas_adc_reset_events(adc);
}

static int palmas_gpadc_enable_event_config(struct palmas_gpadc *adc,
					    const struct iio_chan_spec *chan,
					    enum iio_event_direction dir)
{
	struct palmas_adc_event *ev;
	int adc_chan = chan->channel;

	if (palmas_gpadc_get_event(adc, adc_chan, dir))
		/* already enabled */
		return 0;

	if (adc->event0.channel == -1) {
		ev = &adc->event0;
	} else if (adc->event1.channel == -1) {
		/* event0 has to be the lowest channel */
		if (adc_chan < adc->event0.channel) {
			adc->event1 = adc->event0;
			ev = &adc->event0;
		} else {
			ev = &adc->event1;
		}
	} else { /* both AUTO channels already in use */
		dev_warn(adc->dev, "event0 - %d, event1 - %d\n",
			 adc->event0.channel, adc->event1.channel);
		return -EBUSY;
	}

	ev->enabled = true;
	ev->channel = adc_chan;
	ev->direction = dir;

	return palmas_gpadc_reconfigure_event_channels(adc);
}

static int palmas_gpadc_disable_event_config(struct palmas_gpadc *adc,
					     const struct iio_chan_spec *chan,
					     enum iio_event_direction dir)
{
	int adc_chan = chan->channel;
	struct palmas_adc_event *ev = palmas_gpadc_get_event(adc, adc_chan, dir);

	if (!ev)
		return 0;

	if (ev == &adc->event0) {
		adc->event0 = adc->event1;
		ev = &adc->event1;
	}

	ev->enabled = false;
	ev->channel = -1;
	ev->direction = IIO_EV_DIR_NONE;

	return palmas_gpadc_reconfigure_event_channels(adc);
}

static int palmas_gpadc_write_event_config(struct iio_dev *indio_dev,
					   const struct iio_chan_spec *chan,
					   enum iio_event_type type,
					   enum iio_event_direction dir,
					   int state)
{
	struct palmas_gpadc *adc = iio_priv(indio_dev);
	int adc_chan = chan->channel;
	int ret;

	if (adc_chan >= PALMAS_ADC_CH_MAX || type != IIO_EV_TYPE_THRESH)
		return -EINVAL;

	mutex_lock(&adc->lock);

	if (state)
		ret = palmas_gpadc_enable_event_config(adc, chan, dir);
	else
		ret = palmas_gpadc_disable_event_config(adc, chan, dir);

	mutex_unlock(&adc->lock);

	return ret;
}

static int palmas_gpadc_read_event_value(struct iio_dev *indio_dev,
					 const struct iio_chan_spec *chan,
					 enum iio_event_type type,
					 enum iio_event_direction dir,
					 enum iio_event_info info,
					 int *val, int *val2)
{
	struct palmas_gpadc *adc = iio_priv(indio_dev);
	int adc_chan = chan->channel;
	int ret;

	if (adc_chan >= PALMAS_ADC_CH_MAX || type != IIO_EV_TYPE_THRESH)
		return -EINVAL;

	mutex_lock(&adc->lock);

	switch (info) {
	case IIO_EV_INFO_VALUE:
		*val = (dir == IIO_EV_DIR_RISING) ?
			adc->thresholds[adc_chan].high :
			adc->thresholds[adc_chan].low;
		ret = IIO_VAL_INT;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&adc->lock);

	return ret;
}

static int palmas_gpadc_write_event_value(struct iio_dev *indio_dev,
					  const struct iio_chan_spec *chan,
					  enum iio_event_type type,
					  enum iio_event_direction dir,
					  enum iio_event_info info,
					  int val, int val2)
{
	struct palmas_gpadc *adc = iio_priv(indio_dev);
	int adc_chan = chan->channel;
	int old;
	int ret;

	if (adc_chan >= PALMAS_ADC_CH_MAX || type != IIO_EV_TYPE_THRESH)
		return -EINVAL;

	mutex_lock(&adc->lock);
	switch (info) {
	case IIO_EV_INFO_VALUE:
		if (val < 0 || val > 0xFFF) {
			ret = -EINVAL;
			goto out_unlock;
		}
		if (dir == IIO_EV_DIR_RISING) {
			old = adc->thresholds[adc_chan].high;
			adc->thresholds[adc_chan].high = val;
		} else {
			old = adc->thresholds[adc_chan].low;
			adc->thresholds[adc_chan].low = val;
		}
		ret = 0;
		break;
	default:
		ret = -EINVAL;
		goto out_unlock;
	}

	if (val != old && palmas_gpadc_get_event(adc, adc_chan, dir))
		ret = palmas_gpadc_reconfigure_event_channels(adc);

out_unlock:
	mutex_unlock(&adc->lock);

	return ret;
}

static const struct iio_info palmas_gpadc_iio_info = {
	.read_raw = palmas_gpadc_read_raw,
	.read_event_config = palmas_gpadc_read_event_config,
	.write_event_config = palmas_gpadc_write_event_config,
	.read_event_value = palmas_gpadc_read_event_value,
	.write_event_value = palmas_gpadc_write_event_value,
};

static const struct iio_event_spec palmas_gpadc_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
				BIT(IIO_EV_INFO_ENABLE),
	}, {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE) |
				BIT(IIO_EV_INFO_ENABLE),
	},
};

#define PALMAS_ADC_CHAN_IIO(chan, _type, chan_info)	\
{							\
	.datasheet_name = PALMAS_DATASHEET_NAME(chan),	\
	.type = _type,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |	\
			BIT(chan_info),			\
	.indexed = 1,					\
	.channel = PALMAS_ADC_CH_##chan,		\
	.event_spec = palmas_gpadc_events,		\
	.num_event_specs = ARRAY_SIZE(palmas_gpadc_events)	\
}

static const struct iio_chan_spec palmas_gpadc_iio_channel[] = {
	PALMAS_ADC_CHAN_IIO(IN0, IIO_VOLTAGE, IIO_CHAN_INFO_PROCESSED),
	PALMAS_ADC_CHAN_IIO(IN1, IIO_TEMP, IIO_CHAN_INFO_RAW),
	PALMAS_ADC_CHAN_IIO(IN2, IIO_VOLTAGE, IIO_CHAN_INFO_PROCESSED),
	PALMAS_ADC_CHAN_IIO(IN3, IIO_TEMP, IIO_CHAN_INFO_RAW),
	PALMAS_ADC_CHAN_IIO(IN4, IIO_VOLTAGE, IIO_CHAN_INFO_PROCESSED),
	PALMAS_ADC_CHAN_IIO(IN5, IIO_VOLTAGE, IIO_CHAN_INFO_PROCESSED),
	PALMAS_ADC_CHAN_IIO(IN6, IIO_VOLTAGE, IIO_CHAN_INFO_PROCESSED),
	PALMAS_ADC_CHAN_IIO(IN7, IIO_VOLTAGE, IIO_CHAN_INFO_PROCESSED),
	PALMAS_ADC_CHAN_IIO(IN8, IIO_VOLTAGE, IIO_CHAN_INFO_PROCESSED),
	PALMAS_ADC_CHAN_IIO(IN9, IIO_VOLTAGE, IIO_CHAN_INFO_PROCESSED),
	PALMAS_ADC_CHAN_IIO(IN10, IIO_VOLTAGE, IIO_CHAN_INFO_PROCESSED),
	PALMAS_ADC_CHAN_IIO(IN11, IIO_VOLTAGE, IIO_CHAN_INFO_PROCESSED),
	PALMAS_ADC_CHAN_IIO(IN12, IIO_TEMP, IIO_CHAN_INFO_RAW),
	PALMAS_ADC_CHAN_IIO(IN13, IIO_TEMP, IIO_CHAN_INFO_RAW),
	PALMAS_ADC_CHAN_IIO(IN14, IIO_VOLTAGE, IIO_CHAN_INFO_PROCESSED),
	PALMAS_ADC_CHAN_IIO(IN15, IIO_VOLTAGE, IIO_CHAN_INFO_PROCESSED),
};

static int palmas_gpadc_get_adc_dt_data(struct platform_device *pdev,
	struct palmas_gpadc_platform_data **gpadc_pdata)
{
	struct device_node *np = pdev->dev.of_node;
	struct palmas_gpadc_platform_data *gp_data;
	int ret;
	u32 pval;

	gp_data = devm_kzalloc(&pdev->dev, sizeof(*gp_data), GFP_KERNEL);
	if (!gp_data)
		return -ENOMEM;

	ret = of_property_read_u32(np, "ti,channel0-current-microamp", &pval);
	if (!ret)
		gp_data->ch0_current = pval;

	ret = of_property_read_u32(np, "ti,channel3-current-microamp", &pval);
	if (!ret)
		gp_data->ch3_current = pval;

	gp_data->extended_delay = of_property_read_bool(np,
					"ti,enable-extended-delay");

	*gpadc_pdata = gp_data;

	return 0;
}

static void palmas_gpadc_reset(void *data)
{
	struct palmas_gpadc *adc = data;
	if (adc->event0.enabled || adc->event1.enabled)
		palmas_adc_reset_events(adc);
}

static int palmas_gpadc_probe(struct platform_device *pdev)
{
	struct palmas_gpadc *adc;
	struct palmas_platform_data *pdata;
	struct palmas_gpadc_platform_data *gpadc_pdata = NULL;
	struct iio_dev *indio_dev;
	int ret, i;

	pdata = dev_get_platdata(pdev->dev.parent);

	if (pdata && pdata->gpadc_pdata)
		gpadc_pdata = pdata->gpadc_pdata;

	if (!gpadc_pdata && pdev->dev.of_node) {
		ret = palmas_gpadc_get_adc_dt_data(pdev, &gpadc_pdata);
		if (ret < 0)
			return ret;
	}
	if (!gpadc_pdata)
		return -EINVAL;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*adc));
	if (!indio_dev) {
		dev_err(&pdev->dev, "iio_device_alloc failed\n");
		return -ENOMEM;
	}

	adc = iio_priv(indio_dev);
	adc->dev = &pdev->dev;
	adc->palmas = dev_get_drvdata(pdev->dev.parent);
	adc->adc_info = palmas_gpadc_info;

	mutex_init(&adc->lock);

	init_completion(&adc->conv_completion);
	platform_set_drvdata(pdev, indio_dev);

	adc->auto_conversion_period = gpadc_pdata->auto_conversion_period_ms;
	adc->irq = palmas_irq_get_virq(adc->palmas, PALMAS_GPADC_EOC_SW_IRQ);
	if (adc->irq < 0)
		return dev_err_probe(adc->dev, adc->irq, "get virq failed\n");

	ret = devm_request_threaded_irq(&pdev->dev, adc->irq, NULL,
					palmas_gpadc_irq,
					IRQF_ONESHOT, dev_name(adc->dev),
					adc);
	if (ret < 0)
		return dev_err_probe(adc->dev, ret,
				     "request irq %d failed\n", adc->irq);

	adc->irq_auto_0 = platform_get_irq(pdev, 1);
	if (adc->irq_auto_0 < 0)
		return dev_err_probe(adc->dev, adc->irq_auto_0,
				     "get auto0 irq failed\n");

	ret = devm_request_threaded_irq(&pdev->dev, adc->irq_auto_0, NULL,
					palmas_gpadc_irq_auto, IRQF_ONESHOT,
					"palmas-adc-auto-0", indio_dev);
	if (ret < 0)
		return dev_err_probe(adc->dev, ret,
				     "request auto0 irq %d failed\n",
				     adc->irq_auto_0);

	adc->irq_auto_1 = platform_get_irq(pdev, 2);
	if (adc->irq_auto_1 < 0)
		return dev_err_probe(adc->dev, adc->irq_auto_1,
				     "get auto1 irq failed\n");

	ret = devm_request_threaded_irq(&pdev->dev, adc->irq_auto_1, NULL,
					palmas_gpadc_irq_auto, IRQF_ONESHOT,
					"palmas-adc-auto-1", indio_dev);
	if (ret < 0)
		return dev_err_probe(adc->dev, ret,
				     "request auto1 irq %d failed\n",
				     adc->irq_auto_1);

	adc->event0.enabled = false;
	adc->event0.channel = -1;
	adc->event0.direction = IIO_EV_DIR_NONE;
	adc->event1.enabled = false;
	adc->event1.channel = -1;
	adc->event1.direction = IIO_EV_DIR_NONE;

	/* set the current source 0 (value 0/5/15/20 uA => 0..3) */
	if (gpadc_pdata->ch0_current <= 1)
		adc->ch0_current = PALMAS_ADC_CH0_CURRENT_SRC_0;
	else if (gpadc_pdata->ch0_current <= 5)
		adc->ch0_current = PALMAS_ADC_CH0_CURRENT_SRC_5;
	else if (gpadc_pdata->ch0_current <= 15)
		adc->ch0_current = PALMAS_ADC_CH0_CURRENT_SRC_15;
	else
		adc->ch0_current = PALMAS_ADC_CH0_CURRENT_SRC_20;

	/* set the current source 3 (value 0/10/400/800 uA => 0..3) */
	if (gpadc_pdata->ch3_current <= 1)
		adc->ch3_current = PALMAS_ADC_CH3_CURRENT_SRC_0;
	else if (gpadc_pdata->ch3_current <= 10)
		adc->ch3_current = PALMAS_ADC_CH3_CURRENT_SRC_10;
	else if (gpadc_pdata->ch3_current <= 400)
		adc->ch3_current = PALMAS_ADC_CH3_CURRENT_SRC_400;
	else
		adc->ch3_current = PALMAS_ADC_CH3_CURRENT_SRC_800;

	adc->extended_delay = gpadc_pdata->extended_delay;

	indio_dev->name = MOD_NAME;
	indio_dev->info = &palmas_gpadc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = palmas_gpadc_iio_channel;
	indio_dev->num_channels = ARRAY_SIZE(palmas_gpadc_iio_channel);

	ret = devm_iio_device_register(&pdev->dev, indio_dev);
	if (ret < 0)
		return dev_err_probe(adc->dev, ret,
				     "iio_device_register() failed\n");

	device_set_wakeup_capable(&pdev->dev, 1);
	for (i = 0; i < PALMAS_ADC_CH_MAX; i++) {
		if (!(adc->adc_info[i].is_uncalibrated))
			palmas_gpadc_calibrate(adc, i);
	}

	ret = devm_add_action(&pdev->dev, palmas_gpadc_reset, adc);
	if (ret)
		return ret;

	return 0;
}

static int palmas_adc_configure_events(struct palmas_gpadc *adc)
{
	int adc_period, conv;
	int i;
	int ch0 = 0, ch1 = 0;
	int thres;
	int ret;

	adc_period = adc->auto_conversion_period;
	for (i = 0; i < 16; ++i) {
		if (((1000 * (1 << i)) / 32) >= adc_period)
			break;
	}
	if (i > 0)
		i--;
	adc_period = i;
	ret = palmas_update_bits(adc->palmas, PALMAS_GPADC_BASE,
			PALMAS_GPADC_AUTO_CTRL,
			PALMAS_GPADC_AUTO_CTRL_COUNTER_CONV_MASK,
			adc_period);
	if (ret < 0) {
		dev_err(adc->dev, "AUTO_CTRL write failed: %d\n", ret);
		return ret;
	}

	conv = 0;
	if (adc->event0.enabled) {
		struct palmas_adc_event *ev = &adc->event0;
		int polarity;

		ch0 = ev->channel;
		conv |= PALMAS_GPADC_AUTO_CTRL_AUTO_CONV0_EN;
		switch (ev->direction) {
		case IIO_EV_DIR_RISING:
			thres = palmas_gpadc_get_high_threshold_raw(adc, ev);
			polarity = 0;
			break;
		case IIO_EV_DIR_FALLING:
			thres = palmas_gpadc_get_low_threshold_raw(adc, ev);
			polarity = PALMAS_GPADC_THRES_CONV0_MSB_THRES_CONV0_POL;
			break;
		default:
			return -EINVAL;
		}

		ret = palmas_write(adc->palmas, PALMAS_GPADC_BASE,
				PALMAS_GPADC_THRES_CONV0_LSB, thres & 0xFF);
		if (ret < 0) {
			dev_err(adc->dev,
				"THRES_CONV0_LSB write failed: %d\n", ret);
			return ret;
		}

		ret = palmas_write(adc->palmas, PALMAS_GPADC_BASE,
				PALMAS_GPADC_THRES_CONV0_MSB,
				((thres >> 8) & 0xF) | polarity);
		if (ret < 0) {
			dev_err(adc->dev,
				"THRES_CONV0_MSB write failed: %d\n", ret);
			return ret;
		}
	}

	if (adc->event1.enabled) {
		struct palmas_adc_event *ev = &adc->event1;
		int polarity;

		ch1 = ev->channel;
		conv |= PALMAS_GPADC_AUTO_CTRL_AUTO_CONV1_EN;
		switch (ev->direction) {
		case IIO_EV_DIR_RISING:
			thres = palmas_gpadc_get_high_threshold_raw(adc, ev);
			polarity = 0;
			break;
		case IIO_EV_DIR_FALLING:
			thres = palmas_gpadc_get_low_threshold_raw(adc, ev);
			polarity = PALMAS_GPADC_THRES_CONV1_MSB_THRES_CONV1_POL;
			break;
		default:
			return -EINVAL;
		}

		ret = palmas_write(adc->palmas, PALMAS_GPADC_BASE,
				PALMAS_GPADC_THRES_CONV1_LSB, thres & 0xFF);
		if (ret < 0) {
			dev_err(adc->dev,
				"THRES_CONV1_LSB write failed: %d\n", ret);
			return ret;
		}

		ret = palmas_write(adc->palmas, PALMAS_GPADC_BASE,
				PALMAS_GPADC_THRES_CONV1_MSB,
				((thres >> 8) & 0xF) | polarity);
		if (ret < 0) {
			dev_err(adc->dev,
				"THRES_CONV1_MSB write failed: %d\n", ret);
			return ret;
		}
	}

	ret = palmas_write(adc->palmas, PALMAS_GPADC_BASE,
			PALMAS_GPADC_AUTO_SELECT, (ch1 << 4) | ch0);
	if (ret < 0) {
		dev_err(adc->dev, "AUTO_SELECT write failed: %d\n", ret);
		return ret;
	}

	ret = palmas_update_bits(adc->palmas, PALMAS_GPADC_BASE,
			PALMAS_GPADC_AUTO_CTRL,
			PALMAS_GPADC_AUTO_CTRL_AUTO_CONV1_EN |
			PALMAS_GPADC_AUTO_CTRL_AUTO_CONV0_EN, conv);
	if (ret < 0)
		dev_err(adc->dev, "AUTO_CTRL write failed: %d\n", ret);

	return ret;
}

static int palmas_adc_reset_events(struct palmas_gpadc *adc)
{
	int ret;

	ret = palmas_write(adc->palmas, PALMAS_GPADC_BASE,
			PALMAS_GPADC_AUTO_SELECT, 0);
	if (ret < 0) {
		dev_err(adc->dev, "AUTO_SELECT write failed: %d\n", ret);
		return ret;
	}

	ret = palmas_disable_auto_conversion(adc);
	if (ret < 0)
		dev_err(adc->dev, "Disable auto conversion failed: %d\n", ret);

	return ret;
}

static int palmas_gpadc_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct palmas_gpadc *adc = iio_priv(indio_dev);

	if (!device_may_wakeup(dev))
		return 0;

	if (adc->event0.enabled)
		enable_irq_wake(adc->irq_auto_0);

	if (adc->event1.enabled)
		enable_irq_wake(adc->irq_auto_1);

	return 0;
}

static int palmas_gpadc_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct palmas_gpadc *adc = iio_priv(indio_dev);

	if (!device_may_wakeup(dev))
		return 0;

	if (adc->event0.enabled)
		disable_irq_wake(adc->irq_auto_0);

	if (adc->event1.enabled)
		disable_irq_wake(adc->irq_auto_1);

	return 0;
};

static DEFINE_SIMPLE_DEV_PM_OPS(palmas_pm_ops, palmas_gpadc_suspend,
				palmas_gpadc_resume);

static const struct of_device_id of_palmas_gpadc_match_tbl[] = {
	{ .compatible = "ti,palmas-gpadc", },
	{ /* end */ }
};
MODULE_DEVICE_TABLE(of, of_palmas_gpadc_match_tbl);

static struct platform_driver palmas_gpadc_driver = {
	.probe = palmas_gpadc_probe,
	.driver = {
		.name = MOD_NAME,
		.pm = pm_sleep_ptr(&palmas_pm_ops),
		.of_match_table = of_palmas_gpadc_match_tbl,
	},
};
module_platform_driver(palmas_gpadc_driver);

MODULE_DESCRIPTION("palmas GPADC driver");
MODULE_AUTHOR("Pradeep Goudagunta<pgoudagunta@nvidia.com>");
MODULE_ALIAS("platform:palmas-gpadc");
MODULE_LICENSE("GPL v2");
