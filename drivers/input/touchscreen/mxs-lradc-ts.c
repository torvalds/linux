/*
 * Freescale MXS LRADC touchscreen driver
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

#include <linux/device.h>
#include <linux/err.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mfd/core.h>
#include <linux/mfd/mxs-lradc.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>

const char *mxs_lradc_ts_irq_names[] = {
	"mxs-lradc-touchscreen",
	"mxs-lradc-channel6",
	"mxs-lradc-channel7",
};

/*
 * Touchscreen handling
 */
enum mxs_lradc_ts_plate {
	LRADC_TOUCH = 0,
	LRADC_SAMPLE_X,
	LRADC_SAMPLE_Y,
	LRADC_SAMPLE_PRESSURE,
	LRADC_SAMPLE_VALID,
};

struct mxs_lradc_ts {
	struct mxs_lradc	*lradc;
	struct device		*dev;

	void __iomem		*base;
	/*
	 * When the touchscreen is enabled, we give it two private virtual
	 * channels: #6 and #7. This means that only 6 virtual channels (instead
	 * of 8) will be available for buffered capture.
	 */
#define TOUCHSCREEN_VCHANNEL1		7
#define TOUCHSCREEN_VCHANNEL2		6

	struct input_dev	*ts_input;

	enum mxs_lradc_ts_plate	cur_plate; /* state machine */
	bool			ts_valid;
	unsigned int		ts_x_pos;
	unsigned int		ts_y_pos;
	unsigned int		ts_pressure;

	/* handle touchscreen's physical behaviour */
	/* samples per coordinate */
	unsigned int		over_sample_cnt;
	/* time clocks between samples */
	unsigned int		over_sample_delay;
	/* time in clocks to wait after the plates where switched */
	unsigned int		settling_delay;
	spinlock_t		lock;
};

struct state_info {
	u32		mask;
	u32		bit;
	u32		x_plate;
	u32		y_plate;
	u32		pressure;
};

static struct state_info info[] = {
	{LRADC_CTRL0_MX23_PLATE_MASK, LRADC_CTRL0_MX23_TOUCH_DETECT_ENABLE,
	 LRADC_CTRL0_MX23_XP | LRADC_CTRL0_MX23_XM,
	 LRADC_CTRL0_MX23_YP | LRADC_CTRL0_MX23_YM,
	 LRADC_CTRL0_MX23_YP | LRADC_CTRL0_MX23_XM},
	{LRADC_CTRL0_MX28_PLATE_MASK, LRADC_CTRL0_MX28_TOUCH_DETECT_ENABLE,
	 LRADC_CTRL0_MX28_XPPSW | LRADC_CTRL0_MX28_XNNSW,
	 LRADC_CTRL0_MX28_YPPSW | LRADC_CTRL0_MX28_YNNSW,
	 LRADC_CTRL0_MX28_YPPSW | LRADC_CTRL0_MX28_XNNSW}
};

static bool mxs_lradc_check_touch_event(struct mxs_lradc_ts *ts)
{
	return !!(readl(ts->base + LRADC_STATUS) &
					LRADC_STATUS_TOUCH_DETECT_RAW);
}

static void mxs_lradc_map_ts_channel(struct mxs_lradc_ts *ts, unsigned int vch,
				     unsigned int ch)
{
	writel(LRADC_CTRL4_LRADCSELECT_MASK(vch),
	       ts->base + LRADC_CTRL4 + STMP_OFFSET_REG_CLR);
	writel(LRADC_CTRL4_LRADCSELECT(vch, ch),
	       ts->base + LRADC_CTRL4 + STMP_OFFSET_REG_SET);
}

static void mxs_lradc_setup_ts_channel(struct mxs_lradc_ts *ts, unsigned int ch)
{
	/*
	 * prepare for oversampling conversion
	 *
	 * from the datasheet:
	 * "The ACCUMULATE bit in the appropriate channel register
	 * HW_LRADC_CHn must be set to 1 if NUM_SAMPLES is greater then 0;
	 * otherwise, the IRQs will not fire."
	 */
	writel(LRADC_CH_ACCUMULATE |
	       LRADC_CH_NUM_SAMPLES(ts->over_sample_cnt - 1),
	       ts->base + LRADC_CH(ch));

	/* from the datasheet:
	 * "Software must clear this register in preparation for a
	 * multi-cycle accumulation.
	 */
	writel(LRADC_CH_VALUE_MASK,
	       ts->base + LRADC_CH(ch) + STMP_OFFSET_REG_CLR);

	/*
	 * prepare the delay/loop unit according to the oversampling count
	 *
	 * from the datasheet:
	 * "The DELAY fields in HW_LRADC_DELAY0, HW_LRADC_DELAY1,
	 * HW_LRADC_DELAY2, and HW_LRADC_DELAY3 must be non-zero; otherwise,
	 * the LRADC will not trigger the delay group."
	 */
	writel(LRADC_DELAY_TRIGGER(1 << ch) | LRADC_DELAY_TRIGGER_DELAYS(0) |
	       LRADC_DELAY_LOOP(ts->over_sample_cnt - 1) |
	       LRADC_DELAY_DELAY(ts->over_sample_delay - 1),
	       ts->base + LRADC_DELAY(3));

	writel(LRADC_CTRL1_LRADC_IRQ(ch),
	       ts->base + LRADC_CTRL1 + STMP_OFFSET_REG_CLR);

	/*
	 * after changing the touchscreen plates setting
	 * the signals need some initial time to settle. Start the
	 * SoC's delay unit and start the conversion later
	 * and automatically.
	 */
	writel(LRADC_DELAY_TRIGGER(0) | LRADC_DELAY_TRIGGER_DELAYS(BIT(3)) |
	       LRADC_DELAY_KICK | LRADC_DELAY_DELAY(ts->settling_delay),
	       ts->base + LRADC_DELAY(2));
}

/*
 * Pressure detection is special:
 * We want to do both required measurements for the pressure detection in
 * one turn. Use the hardware features to chain both conversions and let the
 * hardware report one interrupt if both conversions are done
 */
static void mxs_lradc_setup_ts_pressure(struct mxs_lradc_ts *ts,
					unsigned int ch1, unsigned int ch2)
{
	u32 reg;

	/*
	 * prepare for oversampling conversion
	 *
	 * from the datasheet:
	 * "The ACCUMULATE bit in the appropriate channel register
	 * HW_LRADC_CHn must be set to 1 if NUM_SAMPLES is greater then 0;
	 * otherwise, the IRQs will not fire."
	 */
	reg = LRADC_CH_ACCUMULATE |
		LRADC_CH_NUM_SAMPLES(ts->over_sample_cnt - 1);
	writel(reg, ts->base + LRADC_CH(ch1));
	writel(reg, ts->base + LRADC_CH(ch2));

	/* from the datasheet:
	 * "Software must clear this register in preparation for a
	 * multi-cycle accumulation.
	 */
	writel(LRADC_CH_VALUE_MASK,
	       ts->base + LRADC_CH(ch1) + STMP_OFFSET_REG_CLR);
	writel(LRADC_CH_VALUE_MASK,
	       ts->base + LRADC_CH(ch2) + STMP_OFFSET_REG_CLR);

	/* prepare the delay/loop unit according to the oversampling count */
	writel(LRADC_DELAY_TRIGGER(1 << ch1) | LRADC_DELAY_TRIGGER(1 << ch2) |
	       LRADC_DELAY_TRIGGER_DELAYS(0) |
	       LRADC_DELAY_LOOP(ts->over_sample_cnt - 1) |
	       LRADC_DELAY_DELAY(ts->over_sample_delay - 1),
	       ts->base + LRADC_DELAY(3));

	writel(LRADC_CTRL1_LRADC_IRQ(ch2),
	       ts->base + LRADC_CTRL1 + STMP_OFFSET_REG_CLR);

	/*
	 * after changing the touchscreen plates setting
	 * the signals need some initial time to settle. Start the
	 * SoC's delay unit and start the conversion later
	 * and automatically.
	 */
	writel(LRADC_DELAY_TRIGGER(0) | LRADC_DELAY_TRIGGER_DELAYS(BIT(3)) |
	       LRADC_DELAY_KICK | LRADC_DELAY_DELAY(ts->settling_delay),
	       ts->base + LRADC_DELAY(2));
}

static unsigned int mxs_lradc_ts_read_raw_channel(struct mxs_lradc_ts *ts,
						  unsigned int channel)
{
	u32 reg;
	unsigned int num_samples, val;

	reg = readl(ts->base + LRADC_CH(channel));
	if (reg & LRADC_CH_ACCUMULATE)
		num_samples = ts->over_sample_cnt;
	else
		num_samples = 1;

	val = (reg & LRADC_CH_VALUE_MASK) >> LRADC_CH_VALUE_OFFSET;
	return val / num_samples;
}

static unsigned int mxs_lradc_read_ts_pressure(struct mxs_lradc_ts *ts,
					unsigned int ch1, unsigned int ch2)
{
	u32 reg, mask;
	unsigned int pressure, m1, m2;

	mask = LRADC_CTRL1_LRADC_IRQ(ch1) | LRADC_CTRL1_LRADC_IRQ(ch2);
	reg = readl(ts->base + LRADC_CTRL1) & mask;

	while (reg != mask) {
		reg = readl(ts->base + LRADC_CTRL1) & mask;
		dev_dbg(ts->dev, "One channel is still busy: %X\n", reg);
	}

	m1 = mxs_lradc_ts_read_raw_channel(ts, ch1);
	m2 = mxs_lradc_ts_read_raw_channel(ts, ch2);

	if (m2 == 0) {
		dev_warn(ts->dev, "Cannot calculate pressure\n");
		return 1 << (LRADC_RESOLUTION - 1);
	}

	/* simply scale the value from 0 ... max ADC resolution */
	pressure = m1;
	pressure *= (1 << LRADC_RESOLUTION);
	pressure /= m2;

	dev_dbg(ts->dev, "Pressure = %u\n", pressure);
	return pressure;
}

#define TS_CH_XP 2
#define TS_CH_YP 3
#define TS_CH_XM 4
#define TS_CH_YM 5

/*
 * YP(open)--+-------------+
 *	     |		   |--+
 *	     |		   |  |
 *    YM(-)--+-------------+  |
 *	       +--------------+
 *	       |	      |
 *	   XP(weak+)	    XM(open)
 *
 * "weak+" means 200k Ohm VDDIO
 * (-) means GND
 */
static void mxs_lradc_setup_touch_detection(struct mxs_lradc_ts *ts)
{
	struct mxs_lradc *lradc = ts->lradc;

	/*
	 * In order to detect a touch event the 'touch detect enable' bit
	 * enables:
	 *  - a weak pullup to the X+ connector
	 *  - a strong ground at the Y- connector
	 */
	writel(info[lradc->soc].mask,
	       ts->base + LRADC_CTRL0 + STMP_OFFSET_REG_CLR);
	writel(info[lradc->soc].bit,
	       ts->base + LRADC_CTRL0 + STMP_OFFSET_REG_SET);
}

/*
 * YP(meas)--+-------------+
 *	     |		   |--+
 *	     |		   |  |
 * YM(open)--+-------------+  |
 *	       +--------------+
 *	       |	      |
 *	     XP(+)	    XM(-)
 *
 * (+) means here 1.85 V
 * (-) means here GND
 */
static void mxs_lradc_prepare_x_pos(struct mxs_lradc_ts *ts)
{
	struct mxs_lradc *lradc = ts->lradc;

	writel(info[lradc->soc].mask,
	       ts->base + LRADC_CTRL0 + STMP_OFFSET_REG_CLR);
	writel(info[lradc->soc].x_plate,
	       ts->base + LRADC_CTRL0 + STMP_OFFSET_REG_SET);

	ts->cur_plate = LRADC_SAMPLE_X;
	mxs_lradc_map_ts_channel(ts, TOUCHSCREEN_VCHANNEL1, TS_CH_YP);
	mxs_lradc_setup_ts_channel(ts, TOUCHSCREEN_VCHANNEL1);
}

/*
 *   YP(+)--+-------------+
 *	    |		  |--+
 *	    |		  |  |
 *   YM(-)--+-------------+  |
 *	      +--------------+
 *	      |		     |
 *	   XP(open)	   XM(meas)
 *
 * (+) means here 1.85 V
 * (-) means here GND
 */
static void mxs_lradc_prepare_y_pos(struct mxs_lradc_ts *ts)
{
	struct mxs_lradc *lradc = ts->lradc;

	writel(info[lradc->soc].mask,
	       ts->base + LRADC_CTRL0 + STMP_OFFSET_REG_CLR);
	writel(info[lradc->soc].y_plate,
	       ts->base + LRADC_CTRL0 + STMP_OFFSET_REG_SET);

	ts->cur_plate = LRADC_SAMPLE_Y;
	mxs_lradc_map_ts_channel(ts, TOUCHSCREEN_VCHANNEL1, TS_CH_XM);
	mxs_lradc_setup_ts_channel(ts, TOUCHSCREEN_VCHANNEL1);
}

/*
 *    YP(+)--+-------------+
 *	     |		   |--+
 *	     |		   |  |
 * YM(meas)--+-------------+  |
 *	       +--------------+
 *	       |	      |
 *	    XP(meas)	    XM(-)
 *
 * (+) means here 1.85 V
 * (-) means here GND
 */
static void mxs_lradc_prepare_pressure(struct mxs_lradc_ts *ts)
{
	struct mxs_lradc *lradc = ts->lradc;

	writel(info[lradc->soc].mask,
	       ts->base + LRADC_CTRL0 + STMP_OFFSET_REG_CLR);
	writel(info[lradc->soc].pressure,
	       ts->base + LRADC_CTRL0 + STMP_OFFSET_REG_SET);

	ts->cur_plate = LRADC_SAMPLE_PRESSURE;
	mxs_lradc_map_ts_channel(ts, TOUCHSCREEN_VCHANNEL1, TS_CH_YM);
	mxs_lradc_map_ts_channel(ts, TOUCHSCREEN_VCHANNEL2, TS_CH_XP);
	mxs_lradc_setup_ts_pressure(ts, TOUCHSCREEN_VCHANNEL2,
				    TOUCHSCREEN_VCHANNEL1);
}

static void mxs_lradc_enable_touch_detection(struct mxs_lradc_ts *ts)
{
	mxs_lradc_setup_touch_detection(ts);

	ts->cur_plate = LRADC_TOUCH;
	writel(LRADC_CTRL1_TOUCH_DETECT_IRQ | LRADC_CTRL1_TOUCH_DETECT_IRQ_EN,
	       ts->base + LRADC_CTRL1 + STMP_OFFSET_REG_CLR);
	writel(LRADC_CTRL1_TOUCH_DETECT_IRQ_EN,
	       ts->base + LRADC_CTRL1 + STMP_OFFSET_REG_SET);
}

static void mxs_lradc_start_touch_event(struct mxs_lradc_ts *ts)
{
	writel(LRADC_CTRL1_TOUCH_DETECT_IRQ_EN,
	       ts->base + LRADC_CTRL1 + STMP_OFFSET_REG_CLR);
	writel(LRADC_CTRL1_LRADC_IRQ_EN(TOUCHSCREEN_VCHANNEL1),
	       ts->base + LRADC_CTRL1 + STMP_OFFSET_REG_SET);
	/*
	 * start with the Y-pos, because it uses nearly the same plate
	 * settings like the touch detection
	 */
	mxs_lradc_prepare_y_pos(ts);
}

static void mxs_lradc_report_ts_event(struct mxs_lradc_ts *ts)
{
	input_report_abs(ts->ts_input, ABS_X, ts->ts_x_pos);
	input_report_abs(ts->ts_input, ABS_Y, ts->ts_y_pos);
	input_report_abs(ts->ts_input, ABS_PRESSURE, ts->ts_pressure);
	input_report_key(ts->ts_input, BTN_TOUCH, 1);
	input_sync(ts->ts_input);
}

static void mxs_lradc_complete_touch_event(struct mxs_lradc_ts *ts)
{
	mxs_lradc_setup_touch_detection(ts);
	ts->cur_plate = LRADC_SAMPLE_VALID;
	/*
	 * start a dummy conversion to burn time to settle the signals
	 * note: we are not interested in the conversion's value
	 */
	writel(0, ts->base + LRADC_CH(TOUCHSCREEN_VCHANNEL1));
	writel(LRADC_CTRL1_LRADC_IRQ(TOUCHSCREEN_VCHANNEL1) |
	       LRADC_CTRL1_LRADC_IRQ(TOUCHSCREEN_VCHANNEL2),
	       ts->base + LRADC_CTRL1 + STMP_OFFSET_REG_CLR);
	writel(LRADC_DELAY_TRIGGER(1 << TOUCHSCREEN_VCHANNEL1) |
	       LRADC_DELAY_KICK | LRADC_DELAY_DELAY(10),
	       ts->base + LRADC_DELAY(2));
}

/*
 * in order to avoid false measurements, report only samples where
 * the surface is still touched after the position measurement
 */
static void mxs_lradc_finish_touch_event(struct mxs_lradc_ts *ts, bool valid)
{
	/* if it is still touched, report the sample */
	if (valid && mxs_lradc_check_touch_event(ts)) {
		ts->ts_valid = true;
		mxs_lradc_report_ts_event(ts);
	}

	/* if it is even still touched, continue with the next measurement */
	if (mxs_lradc_check_touch_event(ts)) {
		mxs_lradc_prepare_y_pos(ts);
		return;
	}

	if (ts->ts_valid) {
		/* signal the release */
		ts->ts_valid = false;
		input_report_key(ts->ts_input, BTN_TOUCH, 0);
		input_sync(ts->ts_input);
	}

	/* if it is released, wait for the next touch via IRQ */
	ts->cur_plate = LRADC_TOUCH;
	writel(0, ts->base + LRADC_DELAY(2));
	writel(0, ts->base + LRADC_DELAY(3));
	writel(LRADC_CTRL1_TOUCH_DETECT_IRQ |
	       LRADC_CTRL1_LRADC_IRQ_EN(TOUCHSCREEN_VCHANNEL1) |
	       LRADC_CTRL1_LRADC_IRQ(TOUCHSCREEN_VCHANNEL1),
	       ts->base + LRADC_CTRL1 + STMP_OFFSET_REG_CLR);
	writel(LRADC_CTRL1_TOUCH_DETECT_IRQ_EN,
	       ts->base + LRADC_CTRL1 + STMP_OFFSET_REG_SET);
}

/* touchscreen's state machine */
static void mxs_lradc_handle_touch(struct mxs_lradc_ts *ts)
{
	switch (ts->cur_plate) {
	case LRADC_TOUCH:
		if (mxs_lradc_check_touch_event(ts))
			mxs_lradc_start_touch_event(ts);
		writel(LRADC_CTRL1_TOUCH_DETECT_IRQ,
		       ts->base + LRADC_CTRL1 + STMP_OFFSET_REG_CLR);
		return;

	case LRADC_SAMPLE_Y:
		ts->ts_y_pos =
		    mxs_lradc_ts_read_raw_channel(ts, TOUCHSCREEN_VCHANNEL1);
		mxs_lradc_prepare_x_pos(ts);
		return;

	case LRADC_SAMPLE_X:
		ts->ts_x_pos =
		    mxs_lradc_ts_read_raw_channel(ts, TOUCHSCREEN_VCHANNEL1);
		mxs_lradc_prepare_pressure(ts);
		return;

	case LRADC_SAMPLE_PRESSURE:
		ts->ts_pressure =
		    mxs_lradc_read_ts_pressure(ts,
					       TOUCHSCREEN_VCHANNEL2,
					       TOUCHSCREEN_VCHANNEL1);
		mxs_lradc_complete_touch_event(ts);
		return;

	case LRADC_SAMPLE_VALID:
		mxs_lradc_finish_touch_event(ts, 1);
		break;
	}
}

/* IRQ Handling */
static irqreturn_t mxs_lradc_ts_handle_irq(int irq, void *data)
{
	struct mxs_lradc_ts *ts = data;
	struct mxs_lradc *lradc = ts->lradc;
	unsigned long reg = readl(ts->base + LRADC_CTRL1);
	u32 clr_irq = mxs_lradc_irq_mask(lradc);
	const u32 ts_irq_mask =
		LRADC_CTRL1_TOUCH_DETECT_IRQ |
		LRADC_CTRL1_LRADC_IRQ(TOUCHSCREEN_VCHANNEL1) |
		LRADC_CTRL1_LRADC_IRQ(TOUCHSCREEN_VCHANNEL2);
	unsigned long flags;

	if (!(reg & mxs_lradc_irq_mask(lradc)))
		return IRQ_NONE;

	if (reg & ts_irq_mask) {
		spin_lock_irqsave(&ts->lock, flags);
		mxs_lradc_handle_touch(ts);
		spin_unlock_irqrestore(&ts->lock, flags);
		/* Make sure we don't clear the next conversion's interrupt. */
		clr_irq &= ~(LRADC_CTRL1_LRADC_IRQ(TOUCHSCREEN_VCHANNEL1) |
				LRADC_CTRL1_LRADC_IRQ(TOUCHSCREEN_VCHANNEL2));
		writel(reg & clr_irq,
		       ts->base + LRADC_CTRL1 + STMP_OFFSET_REG_CLR);
	}

	return IRQ_HANDLED;
}

static int mxs_lradc_ts_open(struct input_dev *dev)
{
	struct mxs_lradc_ts *ts = input_get_drvdata(dev);

	/* Enable the touch-detect circuitry. */
	mxs_lradc_enable_touch_detection(ts);

	return 0;
}

static void mxs_lradc_ts_stop(struct mxs_lradc_ts *ts)
{
	int i;
	struct mxs_lradc *lradc = ts->lradc;

	/* stop all interrupts from firing */
	writel(LRADC_CTRL1_TOUCH_DETECT_IRQ_EN |
	       LRADC_CTRL1_LRADC_IRQ_EN(TOUCHSCREEN_VCHANNEL1) |
	       LRADC_CTRL1_LRADC_IRQ_EN(TOUCHSCREEN_VCHANNEL2),
	       ts->base + LRADC_CTRL1 + STMP_OFFSET_REG_CLR);

	/* Power-down touchscreen touch-detect circuitry. */
	writel(info[lradc->soc].mask,
	       ts->base + LRADC_CTRL0 + STMP_OFFSET_REG_CLR);

	writel(lradc->buffer_vchans << LRADC_CTRL1_LRADC_IRQ_EN_OFFSET,
	       ts->base + LRADC_CTRL1 + STMP_OFFSET_REG_CLR);

	for (i = 1; i < LRADC_MAX_DELAY_CHANS; i++)
		writel(0, ts->base + LRADC_DELAY(i));
}

static void mxs_lradc_ts_close(struct input_dev *dev)
{
	struct mxs_lradc_ts *ts = input_get_drvdata(dev);

	mxs_lradc_ts_stop(ts);
}

static void mxs_lradc_ts_hw_init(struct mxs_lradc_ts *ts)
{
	struct mxs_lradc *lradc = ts->lradc;

	/* Configure the touchscreen type */
	if (lradc->soc == IMX28_LRADC) {
		writel(LRADC_CTRL0_MX28_TOUCH_SCREEN_TYPE,
		       ts->base + LRADC_CTRL0 + STMP_OFFSET_REG_CLR);

		if (lradc->touchscreen_wire == MXS_LRADC_TOUCHSCREEN_5WIRE)
			writel(LRADC_CTRL0_MX28_TOUCH_SCREEN_TYPE,
			       ts->base + LRADC_CTRL0 + STMP_OFFSET_REG_SET);
	}
}

static int mxs_lradc_ts_register(struct mxs_lradc_ts *ts)
{
	struct input_dev *input = ts->ts_input;
	struct device *dev = ts->dev;

	input = devm_input_allocate_device(dev);
	if (!input)
		return -ENOMEM;

	input->name = "mxs-lradc-ts";
	input->id.bustype = BUS_HOST;
	input->open = mxs_lradc_ts_open;
	input->close = mxs_lradc_ts_close;

	__set_bit(INPUT_PROP_DIRECT, input->propbit);
	input_set_capability(input, EV_KEY, BTN_TOUCH);
	input_set_abs_params(input, ABS_X, 0, LRADC_SINGLE_SAMPLE_MASK, 0, 0);
	input_set_abs_params(input, ABS_Y, 0, LRADC_SINGLE_SAMPLE_MASK, 0, 0);
	input_set_abs_params(input, ABS_PRESSURE, 0, LRADC_SINGLE_SAMPLE_MASK,
			     0, 0);

	ts->ts_input = input;
	input_set_drvdata(input, ts);

	return input_register_device(input);
}

static int mxs_lradc_ts_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->parent->of_node;
	struct mxs_lradc *lradc = dev_get_drvdata(dev->parent);
	struct mxs_lradc_ts *ts;
	struct resource *iores;
	int ret, irq, virq, i;
	u32 ts_wires = 0, adapt;

	ts = devm_kzalloc(dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	platform_set_drvdata(pdev, ts);

	ts->lradc = lradc;
	ts->dev = dev;
	spin_lock_init(&ts->lock);

	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ts->base = devm_ioremap(dev, iores->start, resource_size(iores));
	if (IS_ERR(ts->base))
		return PTR_ERR(ts->base);

	ret = of_property_read_u32(node, "fsl,lradc-touchscreen-wires",
				   &ts_wires);
	if (ret)
		return ret;

	if (of_property_read_u32(node, "fsl,ave-ctrl", &adapt)) {
		ts->over_sample_cnt = 4;
	} else {
		if (adapt >= 1 && adapt <= 32) {
			ts->over_sample_cnt = adapt;
		} else {
			dev_err(ts->dev, "Invalid sample count (%u)\n",
				adapt);
			return -EINVAL;
		}
	}

	if (of_property_read_u32(node, "fsl,ave-delay", &adapt)) {
		ts->over_sample_delay = 2;
	} else {
		if (adapt >= 2 && adapt <= LRADC_DELAY_DELAY_MASK + 1) {
			ts->over_sample_delay = adapt;
		} else {
			dev_err(ts->dev, "Invalid sample delay (%u)\n",
				adapt);
			return -EINVAL;
		}
	}

	if (of_property_read_u32(node, "fsl,settling", &adapt)) {
		ts->settling_delay = 10;
	} else {
		if (adapt >= 1 && adapt <= LRADC_DELAY_DELAY_MASK) {
			ts->settling_delay = adapt;
		} else {
			dev_err(ts->dev, "Invalid settling delay (%u)\n",
				adapt);
			return -EINVAL;
		}
	}

	ret = stmp_reset_block(ts->base);
	if (ret)
		return ret;

	mxs_lradc_ts_hw_init(ts);

	for (i = 0; i < 3; i++) {
		irq = platform_get_irq_byname(pdev, mxs_lradc_ts_irq_names[i]);
		if (irq < 0)
			return irq;

		virq = irq_of_parse_and_map(node, irq);

		mxs_lradc_ts_stop(ts);

		ret = devm_request_irq(dev, virq,
				       mxs_lradc_ts_handle_irq,
				       0, mxs_lradc_ts_irq_names[i], ts);
		if (ret)
			return ret;
	}

	return mxs_lradc_ts_register(ts);
}

static struct platform_driver mxs_lradc_ts_driver = {
	.driver	= {
		.name = "mxs-lradc-ts",
	},
	.probe	= mxs_lradc_ts_probe,
};
module_platform_driver(mxs_lradc_ts_driver);

MODULE_AUTHOR("Marek Vasut <marex@denx.de>");
MODULE_DESCRIPTION("Freescale MXS LRADC touchscreen driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:mxs-lradc-ts");
