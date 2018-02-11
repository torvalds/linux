/*
 * Freescale MXS LRADC ADC driver
 *
 * Copyright (c) 2012 DENX Software Engineering, GmbH.
 * Copyright (c) 2017 Ksenija Stanojevic <ksenija.stanojevic@gmail.com>
 *
 * Authors:
 *  Marek Vasut <marex@denx.de>
 *  Ksenija Stanojevic <ksenija.stanojevic@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/mfd/core.h>
#include <linux/mfd/mxs-lradc.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/sysfs.h>

#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/sysfs.h>

/*
 * Make this runtime configurable if necessary. Currently, if the buffered mode
 * is enabled, the LRADC takes LRADC_DELAY_TIMER_LOOP samples of data before
 * triggering IRQ. The sampling happens every (LRADC_DELAY_TIMER_PER / 2000)
 * seconds. The result is that the samples arrive every 500mS.
 */
#define LRADC_DELAY_TIMER_PER	200
#define LRADC_DELAY_TIMER_LOOP	5

#define VREF_MV_BASE 1850

static const char *mx23_lradc_adc_irq_names[] = {
	"mxs-lradc-channel0",
	"mxs-lradc-channel1",
	"mxs-lradc-channel2",
	"mxs-lradc-channel3",
	"mxs-lradc-channel4",
	"mxs-lradc-channel5",
};

static const char *mx28_lradc_adc_irq_names[] = {
	"mxs-lradc-thresh0",
	"mxs-lradc-thresh1",
	"mxs-lradc-channel0",
	"mxs-lradc-channel1",
	"mxs-lradc-channel2",
	"mxs-lradc-channel3",
	"mxs-lradc-channel4",
	"mxs-lradc-channel5",
	"mxs-lradc-button0",
	"mxs-lradc-button1",
};

static const u32 mxs_lradc_adc_vref_mv[][LRADC_MAX_TOTAL_CHANS] = {
	[IMX23_LRADC] = {
		VREF_MV_BASE,		/* CH0 */
		VREF_MV_BASE,		/* CH1 */
		VREF_MV_BASE,		/* CH2 */
		VREF_MV_BASE,		/* CH3 */
		VREF_MV_BASE,		/* CH4 */
		VREF_MV_BASE,		/* CH5 */
		VREF_MV_BASE * 2,	/* CH6 VDDIO */
		VREF_MV_BASE * 4,	/* CH7 VBATT */
		VREF_MV_BASE,		/* CH8 Temp sense 0 */
		VREF_MV_BASE,		/* CH9 Temp sense 1 */
		VREF_MV_BASE,		/* CH10 */
		VREF_MV_BASE,		/* CH11 */
		VREF_MV_BASE,		/* CH12 USB_DP */
		VREF_MV_BASE,		/* CH13 USB_DN */
		VREF_MV_BASE,		/* CH14 VBG */
		VREF_MV_BASE * 4,	/* CH15 VDD5V */
	},
	[IMX28_LRADC] = {
		VREF_MV_BASE,		/* CH0 */
		VREF_MV_BASE,		/* CH1 */
		VREF_MV_BASE,		/* CH2 */
		VREF_MV_BASE,		/* CH3 */
		VREF_MV_BASE,		/* CH4 */
		VREF_MV_BASE,		/* CH5 */
		VREF_MV_BASE,		/* CH6 */
		VREF_MV_BASE * 4,	/* CH7 VBATT */
		VREF_MV_BASE,		/* CH8 Temp sense 0 */
		VREF_MV_BASE,		/* CH9 Temp sense 1 */
		VREF_MV_BASE * 2,	/* CH10 VDDIO */
		VREF_MV_BASE,		/* CH11 VTH */
		VREF_MV_BASE * 2,	/* CH12 VDDA */
		VREF_MV_BASE,		/* CH13 VDDD */
		VREF_MV_BASE,		/* CH14 VBG */
		VREF_MV_BASE * 4,	/* CH15 VDD5V */
	},
};

enum mxs_lradc_divbytwo {
	MXS_LRADC_DIV_DISABLED = 0,
	MXS_LRADC_DIV_ENABLED,
};

struct mxs_lradc_scale {
	unsigned int		integer;
	unsigned int		nano;
};

struct mxs_lradc_adc {
	struct mxs_lradc	*lradc;
	struct device		*dev;

	void __iomem		*base;
	u32			buffer[10];
	struct iio_trigger	*trig;
	struct completion	completion;
	spinlock_t		lock;

	const u32		*vref_mv;
	struct mxs_lradc_scale	scale_avail[LRADC_MAX_TOTAL_CHANS][2];
	unsigned long		is_divided;
};


/* Raw I/O operations */
static int mxs_lradc_adc_read_single(struct iio_dev *iio_dev, int chan,
				     int *val)
{
	struct mxs_lradc_adc *adc = iio_priv(iio_dev);
	struct mxs_lradc *lradc = adc->lradc;
	int ret;

	/*
	 * See if there is no buffered operation in progress. If there is simply
	 * bail out. This can be improved to support both buffered and raw IO at
	 * the same time, yet the code becomes horribly complicated. Therefore I
	 * applied KISS principle here.
	 */
	ret = iio_device_claim_direct_mode(iio_dev);
	if (ret)
		return ret;

	reinit_completion(&adc->completion);

	/*
	 * No buffered operation in progress, map the channel and trigger it.
	 * Virtual channel 0 is always used here as the others are always not
	 * used if doing raw sampling.
	 */
	if (lradc->soc == IMX28_LRADC)
		writel(LRADC_CTRL1_LRADC_IRQ_EN(0),
		       adc->base + LRADC_CTRL1 + STMP_OFFSET_REG_CLR);
	writel(0x1, adc->base + LRADC_CTRL0 + STMP_OFFSET_REG_CLR);

	/* Enable / disable the divider per requirement */
	if (test_bit(chan, &adc->is_divided))
		writel(1 << LRADC_CTRL2_DIVIDE_BY_TWO_OFFSET,
		       adc->base + LRADC_CTRL2 + STMP_OFFSET_REG_SET);
	else
		writel(1 << LRADC_CTRL2_DIVIDE_BY_TWO_OFFSET,
		       adc->base + LRADC_CTRL2 + STMP_OFFSET_REG_CLR);

	/* Clean the slot's previous content, then set new one. */
	writel(LRADC_CTRL4_LRADCSELECT_MASK(0),
	       adc->base + LRADC_CTRL4 + STMP_OFFSET_REG_CLR);
	writel(chan, adc->base + LRADC_CTRL4 + STMP_OFFSET_REG_SET);

	writel(0, adc->base + LRADC_CH(0));

	/* Enable the IRQ and start sampling the channel. */
	writel(LRADC_CTRL1_LRADC_IRQ_EN(0),
	       adc->base + LRADC_CTRL1 + STMP_OFFSET_REG_SET);
	writel(BIT(0), adc->base + LRADC_CTRL0 + STMP_OFFSET_REG_SET);

	/* Wait for completion on the channel, 1 second max. */
	ret = wait_for_completion_killable_timeout(&adc->completion, HZ);
	if (!ret)
		ret = -ETIMEDOUT;
	if (ret < 0)
		goto err;

	/* Read the data. */
	*val = readl(adc->base + LRADC_CH(0)) & LRADC_CH_VALUE_MASK;
	ret = IIO_VAL_INT;

err:
	writel(LRADC_CTRL1_LRADC_IRQ_EN(0),
	       adc->base + LRADC_CTRL1 + STMP_OFFSET_REG_CLR);

	iio_device_release_direct_mode(iio_dev);

	return ret;
}

static int mxs_lradc_adc_read_temp(struct iio_dev *iio_dev, int *val)
{
	int ret, min, max;

	ret = mxs_lradc_adc_read_single(iio_dev, 8, &min);
	if (ret != IIO_VAL_INT)
		return ret;

	ret = mxs_lradc_adc_read_single(iio_dev, 9, &max);
	if (ret != IIO_VAL_INT)
		return ret;

	*val = max - min;

	return IIO_VAL_INT;
}

static int mxs_lradc_adc_read_raw(struct iio_dev *iio_dev,
			      const struct iio_chan_spec *chan,
			      int *val, int *val2, long m)
{
	struct mxs_lradc_adc *adc = iio_priv(iio_dev);

	switch (m) {
	case IIO_CHAN_INFO_RAW:
		if (chan->type == IIO_TEMP)
			return mxs_lradc_adc_read_temp(iio_dev, val);

		return mxs_lradc_adc_read_single(iio_dev, chan->channel, val);

	case IIO_CHAN_INFO_SCALE:
		if (chan->type == IIO_TEMP) {
			/*
			 * From the datasheet, we have to multiply by 1.012 and
			 * divide by 4
			 */
			*val = 0;
			*val2 = 253000;
			return IIO_VAL_INT_PLUS_MICRO;
		}

		*val = adc->vref_mv[chan->channel];
		*val2 = chan->scan_type.realbits -
			test_bit(chan->channel, &adc->is_divided);
		return IIO_VAL_FRACTIONAL_LOG2;

	case IIO_CHAN_INFO_OFFSET:
		if (chan->type == IIO_TEMP) {
			/*
			 * The calculated value from the ADC is in Kelvin, we
			 * want Celsius for hwmon so the offset is -273.15
			 * The offset is applied before scaling so it is
			 * actually -213.15 * 4 / 1.012 = -1079.644268
			 */
			*val = -1079;
			*val2 = 644268;

			return IIO_VAL_INT_PLUS_MICRO;
		}

		return -EINVAL;

	default:
		break;
	}

	return -EINVAL;
}

static int mxs_lradc_adc_write_raw(struct iio_dev *iio_dev,
				   const struct iio_chan_spec *chan,
				   int val, int val2, long m)
{
	struct mxs_lradc_adc *adc = iio_priv(iio_dev);
	struct mxs_lradc_scale *scale_avail =
			adc->scale_avail[chan->channel];
	int ret;

	ret = iio_device_claim_direct_mode(iio_dev);
	if (ret)
		return ret;

	switch (m) {
	case IIO_CHAN_INFO_SCALE:
		ret = -EINVAL;
		if (val == scale_avail[MXS_LRADC_DIV_DISABLED].integer &&
		    val2 == scale_avail[MXS_LRADC_DIV_DISABLED].nano) {
			/* divider by two disabled */
			clear_bit(chan->channel, &adc->is_divided);
			ret = 0;
		} else if (val == scale_avail[MXS_LRADC_DIV_ENABLED].integer &&
			   val2 == scale_avail[MXS_LRADC_DIV_ENABLED].nano) {
			/* divider by two enabled */
			set_bit(chan->channel, &adc->is_divided);
			ret = 0;
		}

		break;
	default:
		ret = -EINVAL;
		break;
	}

	iio_device_release_direct_mode(iio_dev);

	return ret;
}

static int mxs_lradc_adc_write_raw_get_fmt(struct iio_dev *iio_dev,
					   const struct iio_chan_spec *chan,
					   long m)
{
	return IIO_VAL_INT_PLUS_NANO;
}

static ssize_t mxs_lradc_adc_show_scale_avail(struct device *dev,
						 struct device_attribute *attr,
						 char *buf)
{
	struct iio_dev *iio = dev_to_iio_dev(dev);
	struct mxs_lradc_adc *adc = iio_priv(iio);
	struct iio_dev_attr *iio_attr = to_iio_dev_attr(attr);
	int i, ch, len = 0;

	ch = iio_attr->address;
	for (i = 0; i < ARRAY_SIZE(adc->scale_avail[ch]); i++)
		len += sprintf(buf + len, "%u.%09u ",
			       adc->scale_avail[ch][i].integer,
			       adc->scale_avail[ch][i].nano);

	len += sprintf(buf + len, "\n");

	return len;
}

#define SHOW_SCALE_AVAILABLE_ATTR(ch)\
	IIO_DEVICE_ATTR(in_voltage##ch##_scale_available, 0444,\
			mxs_lradc_adc_show_scale_avail, NULL, ch)

static SHOW_SCALE_AVAILABLE_ATTR(0);
static SHOW_SCALE_AVAILABLE_ATTR(1);
static SHOW_SCALE_AVAILABLE_ATTR(2);
static SHOW_SCALE_AVAILABLE_ATTR(3);
static SHOW_SCALE_AVAILABLE_ATTR(4);
static SHOW_SCALE_AVAILABLE_ATTR(5);
static SHOW_SCALE_AVAILABLE_ATTR(6);
static SHOW_SCALE_AVAILABLE_ATTR(7);
static SHOW_SCALE_AVAILABLE_ATTR(10);
static SHOW_SCALE_AVAILABLE_ATTR(11);
static SHOW_SCALE_AVAILABLE_ATTR(12);
static SHOW_SCALE_AVAILABLE_ATTR(13);
static SHOW_SCALE_AVAILABLE_ATTR(14);
static SHOW_SCALE_AVAILABLE_ATTR(15);

static struct attribute *mxs_lradc_adc_attributes[] = {
	&iio_dev_attr_in_voltage0_scale_available.dev_attr.attr,
	&iio_dev_attr_in_voltage1_scale_available.dev_attr.attr,
	&iio_dev_attr_in_voltage2_scale_available.dev_attr.attr,
	&iio_dev_attr_in_voltage3_scale_available.dev_attr.attr,
	&iio_dev_attr_in_voltage4_scale_available.dev_attr.attr,
	&iio_dev_attr_in_voltage5_scale_available.dev_attr.attr,
	&iio_dev_attr_in_voltage6_scale_available.dev_attr.attr,
	&iio_dev_attr_in_voltage7_scale_available.dev_attr.attr,
	&iio_dev_attr_in_voltage10_scale_available.dev_attr.attr,
	&iio_dev_attr_in_voltage11_scale_available.dev_attr.attr,
	&iio_dev_attr_in_voltage12_scale_available.dev_attr.attr,
	&iio_dev_attr_in_voltage13_scale_available.dev_attr.attr,
	&iio_dev_attr_in_voltage14_scale_available.dev_attr.attr,
	&iio_dev_attr_in_voltage15_scale_available.dev_attr.attr,
	NULL
};

static const struct attribute_group mxs_lradc_adc_attribute_group = {
	.attrs = mxs_lradc_adc_attributes,
};

static const struct iio_info mxs_lradc_adc_iio_info = {
	.read_raw		= mxs_lradc_adc_read_raw,
	.write_raw		= mxs_lradc_adc_write_raw,
	.write_raw_get_fmt	= mxs_lradc_adc_write_raw_get_fmt,
	.attrs			= &mxs_lradc_adc_attribute_group,
};

/* IRQ Handling */
static irqreturn_t mxs_lradc_adc_handle_irq(int irq, void *data)
{
	struct iio_dev *iio = data;
	struct mxs_lradc_adc *adc = iio_priv(iio);
	struct mxs_lradc *lradc = adc->lradc;
	unsigned long reg = readl(adc->base + LRADC_CTRL1);
	unsigned long flags;

	if (!(reg & mxs_lradc_irq_mask(lradc)))
		return IRQ_NONE;

	if (iio_buffer_enabled(iio)) {
		if (reg & lradc->buffer_vchans) {
			spin_lock_irqsave(&adc->lock, flags);
			iio_trigger_poll(iio->trig);
			spin_unlock_irqrestore(&adc->lock, flags);
		}
	} else if (reg & LRADC_CTRL1_LRADC_IRQ(0)) {
		complete(&adc->completion);
	}

	writel(reg & mxs_lradc_irq_mask(lradc),
	       adc->base + LRADC_CTRL1 + STMP_OFFSET_REG_CLR);

	return IRQ_HANDLED;
}


/* Trigger handling */
static irqreturn_t mxs_lradc_adc_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *iio = pf->indio_dev;
	struct mxs_lradc_adc *adc = iio_priv(iio);
	const u32 chan_value = LRADC_CH_ACCUMULATE |
		((LRADC_DELAY_TIMER_LOOP - 1) << LRADC_CH_NUM_SAMPLES_OFFSET);
	unsigned int i, j = 0;

	for_each_set_bit(i, iio->active_scan_mask, LRADC_MAX_TOTAL_CHANS) {
		adc->buffer[j] = readl(adc->base + LRADC_CH(j));
		writel(chan_value, adc->base + LRADC_CH(j));
		adc->buffer[j] &= LRADC_CH_VALUE_MASK;
		adc->buffer[j] /= LRADC_DELAY_TIMER_LOOP;
		j++;
	}

	iio_push_to_buffers_with_timestamp(iio, adc->buffer, pf->timestamp);

	iio_trigger_notify_done(iio->trig);

	return IRQ_HANDLED;
}

static int mxs_lradc_adc_configure_trigger(struct iio_trigger *trig, bool state)
{
	struct iio_dev *iio = iio_trigger_get_drvdata(trig);
	struct mxs_lradc_adc *adc = iio_priv(iio);
	const u32 st = state ? STMP_OFFSET_REG_SET : STMP_OFFSET_REG_CLR;

	writel(LRADC_DELAY_KICK, adc->base + (LRADC_DELAY(0) + st));

	return 0;
}

static const struct iio_trigger_ops mxs_lradc_adc_trigger_ops = {
	.set_trigger_state = &mxs_lradc_adc_configure_trigger,
};

static int mxs_lradc_adc_trigger_init(struct iio_dev *iio)
{
	int ret;
	struct iio_trigger *trig;
	struct mxs_lradc_adc *adc = iio_priv(iio);

	trig = devm_iio_trigger_alloc(&iio->dev, "%s-dev%i", iio->name,
				      iio->id);

	trig->dev.parent = adc->dev;
	iio_trigger_set_drvdata(trig, iio);
	trig->ops = &mxs_lradc_adc_trigger_ops;

	ret = iio_trigger_register(trig);
	if (ret)
		return ret;

	adc->trig = trig;

	return 0;
}

static void mxs_lradc_adc_trigger_remove(struct iio_dev *iio)
{
	struct mxs_lradc_adc *adc = iio_priv(iio);

	iio_trigger_unregister(adc->trig);
}

static int mxs_lradc_adc_buffer_preenable(struct iio_dev *iio)
{
	struct mxs_lradc_adc *adc = iio_priv(iio);
	struct mxs_lradc *lradc = adc->lradc;
	int chan, ofs = 0;
	unsigned long enable = 0;
	u32 ctrl4_set = 0;
	u32 ctrl4_clr = 0;
	u32 ctrl1_irq = 0;
	const u32 chan_value = LRADC_CH_ACCUMULATE |
		((LRADC_DELAY_TIMER_LOOP - 1) << LRADC_CH_NUM_SAMPLES_OFFSET);

	if (lradc->soc == IMX28_LRADC)
		writel(lradc->buffer_vchans << LRADC_CTRL1_LRADC_IRQ_EN_OFFSET,
		       adc->base + LRADC_CTRL1 + STMP_OFFSET_REG_CLR);
	writel(lradc->buffer_vchans,
	       adc->base + LRADC_CTRL0 + STMP_OFFSET_REG_CLR);

	for_each_set_bit(chan, iio->active_scan_mask, LRADC_MAX_TOTAL_CHANS) {
		ctrl4_set |= chan << LRADC_CTRL4_LRADCSELECT_OFFSET(ofs);
		ctrl4_clr |= LRADC_CTRL4_LRADCSELECT_MASK(ofs);
		ctrl1_irq |= LRADC_CTRL1_LRADC_IRQ_EN(ofs);
		writel(chan_value, adc->base + LRADC_CH(ofs));
		bitmap_set(&enable, ofs, 1);
		ofs++;
	}

	writel(LRADC_DELAY_TRIGGER_LRADCS_MASK | LRADC_DELAY_KICK,
	       adc->base + LRADC_DELAY(0) + STMP_OFFSET_REG_CLR);
	writel(ctrl4_clr, adc->base + LRADC_CTRL4 + STMP_OFFSET_REG_CLR);
	writel(ctrl4_set, adc->base + LRADC_CTRL4 + STMP_OFFSET_REG_SET);
	writel(ctrl1_irq, adc->base + LRADC_CTRL1 + STMP_OFFSET_REG_SET);
	writel(enable << LRADC_DELAY_TRIGGER_LRADCS_OFFSET,
	       adc->base + LRADC_DELAY(0) + STMP_OFFSET_REG_SET);

	return 0;
}

static int mxs_lradc_adc_buffer_postdisable(struct iio_dev *iio)
{
	struct mxs_lradc_adc *adc = iio_priv(iio);
	struct mxs_lradc *lradc = adc->lradc;

	writel(LRADC_DELAY_TRIGGER_LRADCS_MASK | LRADC_DELAY_KICK,
	       adc->base + LRADC_DELAY(0) + STMP_OFFSET_REG_CLR);

	writel(lradc->buffer_vchans,
	       adc->base + LRADC_CTRL0 + STMP_OFFSET_REG_CLR);
	if (lradc->soc == IMX28_LRADC)
		writel(lradc->buffer_vchans << LRADC_CTRL1_LRADC_IRQ_EN_OFFSET,
		       adc->base + LRADC_CTRL1 + STMP_OFFSET_REG_CLR);

	return 0;
}

static bool mxs_lradc_adc_validate_scan_mask(struct iio_dev *iio,
					     const unsigned long *mask)
{
	struct mxs_lradc_adc *adc = iio_priv(iio);
	struct mxs_lradc *lradc = adc->lradc;
	const int map_chans = bitmap_weight(mask, LRADC_MAX_TOTAL_CHANS);
	int rsvd_chans = 0;
	unsigned long rsvd_mask = 0;

	if (lradc->use_touchbutton)
		rsvd_mask |= CHAN_MASK_TOUCHBUTTON;
	if (lradc->touchscreen_wire == MXS_LRADC_TOUCHSCREEN_4WIRE)
		rsvd_mask |= CHAN_MASK_TOUCHSCREEN_4WIRE;
	if (lradc->touchscreen_wire == MXS_LRADC_TOUCHSCREEN_5WIRE)
		rsvd_mask |= CHAN_MASK_TOUCHSCREEN_5WIRE;

	if (lradc->use_touchbutton)
		rsvd_chans++;
	if (lradc->touchscreen_wire)
		rsvd_chans += 2;

	/* Test for attempts to map channels with special mode of operation. */
	if (bitmap_intersects(mask, &rsvd_mask, LRADC_MAX_TOTAL_CHANS))
		return false;

	/* Test for attempts to map more channels then available slots. */
	if (map_chans + rsvd_chans > LRADC_MAX_MAPPED_CHANS)
		return false;

	return true;
}

static const struct iio_buffer_setup_ops mxs_lradc_adc_buffer_ops = {
	.preenable = &mxs_lradc_adc_buffer_preenable,
	.postenable = &iio_triggered_buffer_postenable,
	.predisable = &iio_triggered_buffer_predisable,
	.postdisable = &mxs_lradc_adc_buffer_postdisable,
	.validate_scan_mask = &mxs_lradc_adc_validate_scan_mask,
};

/* Driver initialization */
#define MXS_ADC_CHAN(idx, chan_type, name) {			\
	.type = (chan_type),					\
	.indexed = 1,						\
	.scan_index = (idx),					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
			      BIT(IIO_CHAN_INFO_SCALE),		\
	.channel = (idx),					\
	.address = (idx),					\
	.scan_type = {						\
		.sign = 'u',					\
		.realbits = LRADC_RESOLUTION,			\
		.storagebits = 32,				\
	},							\
	.datasheet_name = (name),				\
}

static const struct iio_chan_spec mx23_lradc_chan_spec[] = {
	MXS_ADC_CHAN(0, IIO_VOLTAGE, "LRADC0"),
	MXS_ADC_CHAN(1, IIO_VOLTAGE, "LRADC1"),
	MXS_ADC_CHAN(2, IIO_VOLTAGE, "LRADC2"),
	MXS_ADC_CHAN(3, IIO_VOLTAGE, "LRADC3"),
	MXS_ADC_CHAN(4, IIO_VOLTAGE, "LRADC4"),
	MXS_ADC_CHAN(5, IIO_VOLTAGE, "LRADC5"),
	MXS_ADC_CHAN(6, IIO_VOLTAGE, "VDDIO"),
	MXS_ADC_CHAN(7, IIO_VOLTAGE, "VBATT"),
	/* Combined Temperature sensors */
	{
		.type = IIO_TEMP,
		.indexed = 1,
		.scan_index = 8,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_OFFSET) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.channel = 8,
		.scan_type = {.sign = 'u', .realbits = 18, .storagebits = 32,},
		.datasheet_name = "TEMP_DIE",
	},
	/* Hidden channel to keep indexes */
	{
		.type = IIO_TEMP,
		.indexed = 1,
		.scan_index = -1,
		.channel = 9,
	},
	MXS_ADC_CHAN(10, IIO_VOLTAGE, NULL),
	MXS_ADC_CHAN(11, IIO_VOLTAGE, NULL),
	MXS_ADC_CHAN(12, IIO_VOLTAGE, "USB_DP"),
	MXS_ADC_CHAN(13, IIO_VOLTAGE, "USB_DN"),
	MXS_ADC_CHAN(14, IIO_VOLTAGE, "VBG"),
	MXS_ADC_CHAN(15, IIO_VOLTAGE, "VDD5V"),
};

static const struct iio_chan_spec mx28_lradc_chan_spec[] = {
	MXS_ADC_CHAN(0, IIO_VOLTAGE, "LRADC0"),
	MXS_ADC_CHAN(1, IIO_VOLTAGE, "LRADC1"),
	MXS_ADC_CHAN(2, IIO_VOLTAGE, "LRADC2"),
	MXS_ADC_CHAN(3, IIO_VOLTAGE, "LRADC3"),
	MXS_ADC_CHAN(4, IIO_VOLTAGE, "LRADC4"),
	MXS_ADC_CHAN(5, IIO_VOLTAGE, "LRADC5"),
	MXS_ADC_CHAN(6, IIO_VOLTAGE, "LRADC6"),
	MXS_ADC_CHAN(7, IIO_VOLTAGE, "VBATT"),
	/* Combined Temperature sensors */
	{
		.type = IIO_TEMP,
		.indexed = 1,
		.scan_index = 8,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_OFFSET) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.channel = 8,
		.scan_type = {.sign = 'u', .realbits = 18, .storagebits = 32,},
		.datasheet_name = "TEMP_DIE",
	},
	/* Hidden channel to keep indexes */
	{
		.type = IIO_TEMP,
		.indexed = 1,
		.scan_index = -1,
		.channel = 9,
	},
	MXS_ADC_CHAN(10, IIO_VOLTAGE, "VDDIO"),
	MXS_ADC_CHAN(11, IIO_VOLTAGE, "VTH"),
	MXS_ADC_CHAN(12, IIO_VOLTAGE, "VDDA"),
	MXS_ADC_CHAN(13, IIO_VOLTAGE, "VDDD"),
	MXS_ADC_CHAN(14, IIO_VOLTAGE, "VBG"),
	MXS_ADC_CHAN(15, IIO_VOLTAGE, "VDD5V"),
};

static void mxs_lradc_adc_hw_init(struct mxs_lradc_adc *adc)
{
	/* The ADC always uses DELAY CHANNEL 0. */
	const u32 adc_cfg =
		(1 << (LRADC_DELAY_TRIGGER_DELAYS_OFFSET + 0)) |
		(LRADC_DELAY_TIMER_PER << LRADC_DELAY_DELAY_OFFSET);

	/* Configure DELAY CHANNEL 0 for generic ADC sampling. */
	writel(adc_cfg, adc->base + LRADC_DELAY(0));

	/*
	 * Start internal temperature sensing by clearing bit
	 * HW_LRADC_CTRL2_TEMPSENSE_PWD. This bit can be left cleared
	 * after power up.
	 */
	writel(0, adc->base + LRADC_CTRL2);
}

static void mxs_lradc_adc_hw_stop(struct mxs_lradc_adc *adc)
{
	writel(0, adc->base + LRADC_DELAY(0));
}

static int mxs_lradc_adc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mxs_lradc *lradc = dev_get_drvdata(dev->parent);
	struct mxs_lradc_adc *adc;
	struct iio_dev *iio;
	struct resource *iores;
	int ret, irq, virq, i, s, n;
	u64 scale_uv;
	const char **irq_name;

	/* Allocate the IIO device. */
	iio = devm_iio_device_alloc(dev, sizeof(*adc));
	if (!iio) {
		dev_err(dev, "Failed to allocate IIO device\n");
		return -ENOMEM;
	}

	adc = iio_priv(iio);
	adc->lradc = lradc;
	adc->dev = dev;

	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!iores)
		return -EINVAL;

	adc->base = devm_ioremap(dev, iores->start, resource_size(iores));
	if (!adc->base)
		return -ENOMEM;

	init_completion(&adc->completion);
	spin_lock_init(&adc->lock);

	platform_set_drvdata(pdev, iio);

	iio->name = pdev->name;
	iio->dev.parent = dev;
	iio->dev.of_node = dev->parent->of_node;
	iio->info = &mxs_lradc_adc_iio_info;
	iio->modes = INDIO_DIRECT_MODE;
	iio->masklength = LRADC_MAX_TOTAL_CHANS;

	if (lradc->soc == IMX23_LRADC) {
		iio->channels = mx23_lradc_chan_spec;
		iio->num_channels = ARRAY_SIZE(mx23_lradc_chan_spec);
		irq_name = mx23_lradc_adc_irq_names;
		n = ARRAY_SIZE(mx23_lradc_adc_irq_names);
	} else {
		iio->channels = mx28_lradc_chan_spec;
		iio->num_channels = ARRAY_SIZE(mx28_lradc_chan_spec);
		irq_name = mx28_lradc_adc_irq_names;
		n = ARRAY_SIZE(mx28_lradc_adc_irq_names);
	}

	ret = stmp_reset_block(adc->base);
	if (ret)
		return ret;

	for (i = 0; i < n; i++) {
		irq = platform_get_irq_byname(pdev, irq_name[i]);
		if (irq < 0)
			return irq;

		virq = irq_of_parse_and_map(dev->parent->of_node, irq);

		ret = devm_request_irq(dev, virq, mxs_lradc_adc_handle_irq,
				       0, irq_name[i], iio);
		if (ret)
			return ret;
	}

	ret = mxs_lradc_adc_trigger_init(iio);
	if (ret)
		goto err_trig;

	ret = iio_triggered_buffer_setup(iio, &iio_pollfunc_store_time,
					 &mxs_lradc_adc_trigger_handler,
					 &mxs_lradc_adc_buffer_ops);
	if (ret)
		return ret;

	adc->vref_mv = mxs_lradc_adc_vref_mv[lradc->soc];

	/* Populate available ADC input ranges */
	for (i = 0; i < LRADC_MAX_TOTAL_CHANS; i++) {
		for (s = 0; s < ARRAY_SIZE(adc->scale_avail[i]); s++) {
			/*
			 * [s=0] = optional divider by two disabled (default)
			 * [s=1] = optional divider by two enabled
			 *
			 * The scale is calculated by doing:
			 *   Vref >> (realbits - s)
			 * which multiplies by two on the second component
			 * of the array.
			 */
			scale_uv = ((u64)adc->vref_mv[i] * 100000000) >>
				   (LRADC_RESOLUTION - s);
			adc->scale_avail[i][s].nano =
					do_div(scale_uv, 100000000) * 10;
			adc->scale_avail[i][s].integer = scale_uv;
		}
	}

	/* Configure the hardware. */
	mxs_lradc_adc_hw_init(adc);

	/* Register IIO device. */
	ret = iio_device_register(iio);
	if (ret) {
		dev_err(dev, "Failed to register IIO device\n");
		goto err_dev;
	}

	return 0;

err_dev:
	mxs_lradc_adc_hw_stop(adc);
	mxs_lradc_adc_trigger_remove(iio);
err_trig:
	iio_triggered_buffer_cleanup(iio);
	return ret;
}

static int mxs_lradc_adc_remove(struct platform_device *pdev)
{
	struct iio_dev *iio = platform_get_drvdata(pdev);
	struct mxs_lradc_adc *adc = iio_priv(iio);

	iio_device_unregister(iio);
	mxs_lradc_adc_hw_stop(adc);
	mxs_lradc_adc_trigger_remove(iio);
	iio_triggered_buffer_cleanup(iio);

	return 0;
}

static struct platform_driver mxs_lradc_adc_driver = {
	.driver = {
		.name	= "mxs-lradc-adc",
	},
	.probe	= mxs_lradc_adc_probe,
	.remove = mxs_lradc_adc_remove,
};
module_platform_driver(mxs_lradc_adc_driver);

MODULE_AUTHOR("Marek Vasut <marex@denx.de>");
MODULE_DESCRIPTION("Freescale MXS LRADC driver general purpose ADC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:mxs-lradc-adc");
