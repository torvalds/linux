/*
 * Freescale i.MX28 LRADC driver
 *
 * Copyright (c) 2012 DENX Software Engineering, GmbH.
 * Marek Vasut <marex@denx.de>
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

#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/sysfs.h>
#include <linux/list.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/stmp_device.h>
#include <linux/bitops.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/input.h>

#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#define DRIVER_NAME		"mxs-lradc"

#define LRADC_MAX_DELAY_CHANS	4
#define LRADC_MAX_MAPPED_CHANS	8
#define LRADC_MAX_TOTAL_CHANS	16

#define LRADC_DELAY_TIMER_HZ	2000

/*
 * Make this runtime configurable if necessary. Currently, if the buffered mode
 * is enabled, the LRADC takes LRADC_DELAY_TIMER_LOOP samples of data before
 * triggering IRQ. The sampling happens every (LRADC_DELAY_TIMER_PER / 2000)
 * seconds. The result is that the samples arrive every 500mS.
 */
#define LRADC_DELAY_TIMER_PER	200
#define LRADC_DELAY_TIMER_LOOP	5

/*
 * Once the pen touches the touchscreen, the touchscreen switches from
 * IRQ-driven mode to polling mode to prevent interrupt storm. The polling
 * is realized by worker thread, which is called every 20 or so milliseconds.
 * This gives the touchscreen enough fluence and does not strain the system
 * too much.
 */
#define LRADC_TS_SAMPLE_DELAY_MS	5

/*
 * The LRADC reads the following amount of samples from each touchscreen
 * channel and the driver then computes avarage of these.
 */
#define LRADC_TS_SAMPLE_AMOUNT		4

enum mxs_lradc_id {
	IMX23_LRADC,
	IMX28_LRADC,
};

static const char * const mx23_lradc_irq_names[] = {
	"mxs-lradc-touchscreen",
	"mxs-lradc-channel0",
	"mxs-lradc-channel1",
	"mxs-lradc-channel2",
	"mxs-lradc-channel3",
	"mxs-lradc-channel4",
	"mxs-lradc-channel5",
	"mxs-lradc-channel6",
	"mxs-lradc-channel7",
};

static const char * const mx28_lradc_irq_names[] = {
	"mxs-lradc-touchscreen",
	"mxs-lradc-thresh0",
	"mxs-lradc-thresh1",
	"mxs-lradc-channel0",
	"mxs-lradc-channel1",
	"mxs-lradc-channel2",
	"mxs-lradc-channel3",
	"mxs-lradc-channel4",
	"mxs-lradc-channel5",
	"mxs-lradc-channel6",
	"mxs-lradc-channel7",
	"mxs-lradc-button0",
	"mxs-lradc-button1",
};

struct mxs_lradc_of_config {
	const int		irq_count;
	const char * const	*irq_name;
};

static const struct mxs_lradc_of_config mxs_lradc_of_config[] = {
	[IMX23_LRADC] = {
		.irq_count	= ARRAY_SIZE(mx23_lradc_irq_names),
		.irq_name	= mx23_lradc_irq_names,
	},
	[IMX28_LRADC] = {
		.irq_count	= ARRAY_SIZE(mx28_lradc_irq_names),
		.irq_name	= mx28_lradc_irq_names,
	},
};

enum mxs_lradc_ts {
	MXS_LRADC_TOUCHSCREEN_NONE = 0,
	MXS_LRADC_TOUCHSCREEN_4WIRE,
	MXS_LRADC_TOUCHSCREEN_5WIRE,
};

struct mxs_lradc {
	struct device		*dev;
	void __iomem		*base;
	int			irq[13];

	uint32_t		*buffer;
	struct iio_trigger	*trig;

	struct mutex		lock;

	struct completion	completion;

	/*
	 * Touchscreen LRADC channels receives a private slot in the CTRL4
	 * register, the slot #7. Therefore only 7 slots instead of 8 in the
	 * CTRL4 register can be mapped to LRADC channels when using the
	 * touchscreen.
	 *
	 * Furthermore, certain LRADC channels are shared between touchscreen
	 * and/or touch-buttons and generic LRADC block. Therefore when using
	 * either of these, these channels are not available for the regular
	 * sampling. The shared channels are as follows:
	 *
	 * CH0 -- Touch button #0
	 * CH1 -- Touch button #1
	 * CH2 -- Touch screen XPUL
	 * CH3 -- Touch screen YPLL
	 * CH4 -- Touch screen XNUL
	 * CH5 -- Touch screen YNLR
	 * CH6 -- Touch screen WIPER (5-wire only)
	 *
	 * The bitfields below represents which parts of the LRADC block are
	 * switched into special mode of operation. These channels can not
	 * be sampled as regular LRADC channels. The driver will refuse any
	 * attempt to sample these channels.
	 */
#define CHAN_MASK_TOUCHBUTTON		(0x3 << 0)
#define CHAN_MASK_TOUCHSCREEN_4WIRE	(0xf << 2)
#define CHAN_MASK_TOUCHSCREEN_5WIRE	(0x1f << 2)
	enum mxs_lradc_ts	use_touchscreen;
	bool			stop_touchscreen;
	bool			use_touchbutton;

	struct input_dev	*ts_input;
	struct work_struct	ts_work;
};

#define	LRADC_CTRL0				0x00
#define	LRADC_CTRL0_TOUCH_DETECT_ENABLE		(1 << 23)
#define	LRADC_CTRL0_TOUCH_SCREEN_TYPE		(1 << 22)
#define	LRADC_CTRL0_YNNSW	/* YM */	(1 << 21)
#define	LRADC_CTRL0_YPNSW	/* YP */	(1 << 20)
#define	LRADC_CTRL0_YPPSW	/* YP */	(1 << 19)
#define	LRADC_CTRL0_XNNSW	/* XM */	(1 << 18)
#define	LRADC_CTRL0_XNPSW	/* XM */	(1 << 17)
#define	LRADC_CTRL0_XPPSW	/* XP */	(1 << 16)
#define	LRADC_CTRL0_PLATE_MASK			(0x3f << 16)

#define	LRADC_CTRL1				0x10
#define	LRADC_CTRL1_TOUCH_DETECT_IRQ_EN		(1 << 24)
#define	LRADC_CTRL1_LRADC_IRQ_EN(n)		(1 << ((n) + 16))
#define	LRADC_CTRL1_LRADC_IRQ_EN_MASK		(0x1fff << 16)
#define	LRADC_CTRL1_LRADC_IRQ_EN_OFFSET		16
#define	LRADC_CTRL1_TOUCH_DETECT_IRQ		(1 << 8)
#define	LRADC_CTRL1_LRADC_IRQ(n)		(1 << (n))
#define	LRADC_CTRL1_LRADC_IRQ_MASK		0x1fff
#define	LRADC_CTRL1_LRADC_IRQ_OFFSET		0

#define	LRADC_CTRL2				0x20
#define	LRADC_CTRL2_TEMPSENSE_PWD		(1 << 15)

#define	LRADC_STATUS				0x40
#define	LRADC_STATUS_TOUCH_DETECT_RAW		(1 << 0)

#define	LRADC_CH(n)				(0x50 + (0x10 * (n)))
#define	LRADC_CH_ACCUMULATE			(1 << 29)
#define	LRADC_CH_NUM_SAMPLES_MASK		(0x1f << 24)
#define	LRADC_CH_NUM_SAMPLES_OFFSET		24
#define	LRADC_CH_VALUE_MASK			0x3ffff
#define	LRADC_CH_VALUE_OFFSET			0

#define	LRADC_DELAY(n)				(0xd0 + (0x10 * (n)))
#define	LRADC_DELAY_TRIGGER_LRADCS_MASK		(0xff << 24)
#define	LRADC_DELAY_TRIGGER_LRADCS_OFFSET	24
#define	LRADC_DELAY_KICK			(1 << 20)
#define	LRADC_DELAY_TRIGGER_DELAYS_MASK		(0xf << 16)
#define	LRADC_DELAY_TRIGGER_DELAYS_OFFSET	16
#define	LRADC_DELAY_LOOP_COUNT_MASK		(0x1f << 11)
#define	LRADC_DELAY_LOOP_COUNT_OFFSET		11
#define	LRADC_DELAY_DELAY_MASK			0x7ff
#define	LRADC_DELAY_DELAY_OFFSET		0

#define	LRADC_CTRL4				0x140
#define	LRADC_CTRL4_LRADCSELECT_MASK(n)		(0xf << ((n) * 4))
#define	LRADC_CTRL4_LRADCSELECT_OFFSET(n)	((n) * 4)

/*
 * Raw I/O operations
 */
static int mxs_lradc_read_raw(struct iio_dev *iio_dev,
			const struct iio_chan_spec *chan,
			int *val, int *val2, long m)
{
	struct mxs_lradc *lradc = iio_priv(iio_dev);
	int ret;
	unsigned long mask;

	if (m != IIO_CHAN_INFO_RAW)
		return -EINVAL;

	/* Check for invalid channel */
	if (chan->channel > LRADC_MAX_TOTAL_CHANS)
		return -EINVAL;

	/* Validate the channel if it doesn't intersect with reserved chans. */
	bitmap_set(&mask, chan->channel, 1);
	ret = iio_validate_scan_mask_onehot(iio_dev, &mask);
	if (ret)
		return -EINVAL;

	/*
	 * See if there is no buffered operation in progess. If there is, simply
	 * bail out. This can be improved to support both buffered and raw IO at
	 * the same time, yet the code becomes horribly complicated. Therefore I
	 * applied KISS principle here.
	 */
	ret = mutex_trylock(&lradc->lock);
	if (!ret)
		return -EBUSY;

	INIT_COMPLETION(lradc->completion);

	/*
	 * No buffered operation in progress, map the channel and trigger it.
	 * Virtual channel 0 is always used here as the others are always not
	 * used if doing raw sampling.
	 */
	writel(LRADC_CTRL1_LRADC_IRQ_EN_MASK,
		lradc->base + LRADC_CTRL1 + STMP_OFFSET_REG_CLR);
	writel(0xff, lradc->base + LRADC_CTRL0 + STMP_OFFSET_REG_CLR);

	/* Clean the slot's previous content, then set new one. */
	writel(LRADC_CTRL4_LRADCSELECT_MASK(0),
		lradc->base + LRADC_CTRL4 + STMP_OFFSET_REG_CLR);
	writel(chan->channel, lradc->base + LRADC_CTRL4 + STMP_OFFSET_REG_SET);

	writel(0, lradc->base + LRADC_CH(0));

	/* Enable the IRQ and start sampling the channel. */
	writel(LRADC_CTRL1_LRADC_IRQ_EN(0),
		lradc->base + LRADC_CTRL1 + STMP_OFFSET_REG_SET);
	writel(1 << 0, lradc->base + LRADC_CTRL0 + STMP_OFFSET_REG_SET);

	/* Wait for completion on the channel, 1 second max. */
	ret = wait_for_completion_killable_timeout(&lradc->completion, HZ);
	if (!ret)
		ret = -ETIMEDOUT;
	if (ret < 0)
		goto err;

	/* Read the data. */
	*val = readl(lradc->base + LRADC_CH(0)) & LRADC_CH_VALUE_MASK;
	ret = IIO_VAL_INT;

err:
	writel(LRADC_CTRL1_LRADC_IRQ_EN(0),
		lradc->base + LRADC_CTRL1 + STMP_OFFSET_REG_CLR);

	mutex_unlock(&lradc->lock);

	return ret;
}

static const struct iio_info mxs_lradc_iio_info = {
	.driver_module		= THIS_MODULE,
	.read_raw		= mxs_lradc_read_raw,
};

/*
 * Touchscreen handling
 */
enum lradc_ts_plate {
	LRADC_SAMPLE_X,
	LRADC_SAMPLE_Y,
	LRADC_SAMPLE_PRESSURE,
};

static int mxs_lradc_ts_touched(struct mxs_lradc *lradc)
{
	uint32_t reg;

	/* Enable touch detection. */
	writel(LRADC_CTRL0_PLATE_MASK,
		lradc->base + LRADC_CTRL0 + STMP_OFFSET_REG_CLR);
	writel(LRADC_CTRL0_TOUCH_DETECT_ENABLE,
		lradc->base + LRADC_CTRL0 + STMP_OFFSET_REG_SET);

	msleep(LRADC_TS_SAMPLE_DELAY_MS);

	reg = readl(lradc->base + LRADC_STATUS);

	return reg & LRADC_STATUS_TOUCH_DETECT_RAW;
}

static int32_t mxs_lradc_ts_sample(struct mxs_lradc *lradc,
				enum lradc_ts_plate plate, int change)
{
	unsigned long delay, jiff;
	uint32_t reg, ctrl0 = 0, chan = 0;
	/* The touchscreen always uses CTRL4 slot #7. */
	const uint8_t slot = 7;
	uint32_t val;

	/*
	 * There are three correct configurations of the controller sampling
	 * the touchscreen, each of these configuration provides different
	 * information from the touchscreen.
	 *
	 * The following table describes the sampling configurations:
	 * +-------------+-------+-------+-------+
	 * | Wire \ Axis |   X   |   Y   |   Z   |
	 * +---------------------+-------+-------+
	 * |   X+ (CH2)  |   HI  |   TS  |   TS  |
	 * +-------------+-------+-------+-------+
	 * |   X- (CH4)  |   LO  |   SH  |   HI  |
	 * +-------------+-------+-------+-------+
	 * |   Y+ (CH3)  |   SH  |   HI  |   HI  |
	 * +-------------+-------+-------+-------+
	 * |   Y- (CH5)  |   TS  |   LO  |   SH  |
	 * +-------------+-------+-------+-------+
	 *
	 * HI ... strong '1'  ; LO ... strong '0'
	 * SH ... sample here ; TS ... tri-state
	 *
	 * There are a few other ways of obtaining the Z coordinate
	 * (aka. pressure), but the one in the table seems to be the
	 * most reliable one.
	 */
	switch (plate) {
	case LRADC_SAMPLE_X:
		ctrl0 = LRADC_CTRL0_XPPSW | LRADC_CTRL0_XNNSW;
		chan = 3;
		break;
	case LRADC_SAMPLE_Y:
		ctrl0 = LRADC_CTRL0_YPPSW | LRADC_CTRL0_YNNSW;
		chan = 4;
		break;
	case LRADC_SAMPLE_PRESSURE:
		ctrl0 = LRADC_CTRL0_YPPSW | LRADC_CTRL0_XNNSW;
		chan = 5;
		break;
	}

	if (change) {
		writel(LRADC_CTRL0_PLATE_MASK,
			lradc->base + LRADC_CTRL0 + STMP_OFFSET_REG_CLR);
		writel(ctrl0, lradc->base + LRADC_CTRL0 + STMP_OFFSET_REG_SET);

		writel(LRADC_CTRL4_LRADCSELECT_MASK(slot),
			lradc->base + LRADC_CTRL4 + STMP_OFFSET_REG_CLR);
		writel(chan << LRADC_CTRL4_LRADCSELECT_OFFSET(slot),
			lradc->base + LRADC_CTRL4 + STMP_OFFSET_REG_SET);
	}

	writel(0xffffffff, lradc->base + LRADC_CH(slot) + STMP_OFFSET_REG_CLR);
	writel(1 << slot, lradc->base + LRADC_CTRL0 + STMP_OFFSET_REG_SET);

	delay = jiffies + msecs_to_jiffies(LRADC_TS_SAMPLE_DELAY_MS);
	do {
		jiff = jiffies;
		reg = readl_relaxed(lradc->base + LRADC_CTRL1);
		if (reg & LRADC_CTRL1_LRADC_IRQ(slot))
			break;
	} while (time_before(jiff, delay));

	writel(LRADC_CTRL1_LRADC_IRQ(slot),
		lradc->base + LRADC_CTRL1 + STMP_OFFSET_REG_CLR);

	if (time_after_eq(jiff, delay))
		return -ETIMEDOUT;

	val = readl(lradc->base + LRADC_CH(slot));
	val &= LRADC_CH_VALUE_MASK;

	return val;
}

static int32_t mxs_lradc_ts_sample_filter(struct mxs_lradc *lradc,
				enum lradc_ts_plate plate)
{
	int32_t val, tot = 0;
	int i;

	val = mxs_lradc_ts_sample(lradc, plate, 1);

	/* Delay a bit so the touchscreen is stable. */
	mdelay(2);

	for (i = 0; i < LRADC_TS_SAMPLE_AMOUNT; i++) {
		val = mxs_lradc_ts_sample(lradc, plate, 0);
		tot += val;
	}

	return tot / LRADC_TS_SAMPLE_AMOUNT;
}

static void mxs_lradc_ts_work(struct work_struct *ts_work)
{
	struct mxs_lradc *lradc = container_of(ts_work,
				struct mxs_lradc, ts_work);
	int val_x, val_y, val_p;
	bool valid = false;

	while (mxs_lradc_ts_touched(lradc)) {
		/* Disable touch detector so we can sample the touchscreen. */
		writel(LRADC_CTRL0_TOUCH_DETECT_ENABLE,
			lradc->base + LRADC_CTRL0 + STMP_OFFSET_REG_CLR);

		if (likely(valid)) {
			input_report_abs(lradc->ts_input, ABS_X, val_x);
			input_report_abs(lradc->ts_input, ABS_Y, val_y);
			input_report_abs(lradc->ts_input, ABS_PRESSURE, val_p);
			input_report_key(lradc->ts_input, BTN_TOUCH, 1);
			input_sync(lradc->ts_input);
		}

		valid = false;

		val_x = mxs_lradc_ts_sample_filter(lradc, LRADC_SAMPLE_X);
		if (val_x < 0)
			continue;
		val_y = mxs_lradc_ts_sample_filter(lradc, LRADC_SAMPLE_Y);
		if (val_y < 0)
			continue;
		val_p = mxs_lradc_ts_sample_filter(lradc, LRADC_SAMPLE_PRESSURE);
		if (val_p < 0)
			continue;

		valid = true;
	}

	input_report_abs(lradc->ts_input, ABS_PRESSURE, 0);
	input_report_key(lradc->ts_input, BTN_TOUCH, 0);
	input_sync(lradc->ts_input);

	/* Do not restart the TS IRQ if the driver is shutting down. */
	if (lradc->stop_touchscreen)
		return;

	/* Restart the touchscreen interrupts. */
	writel(LRADC_CTRL1_TOUCH_DETECT_IRQ,
		lradc->base + LRADC_CTRL1 + STMP_OFFSET_REG_CLR);
	writel(LRADC_CTRL1_TOUCH_DETECT_IRQ_EN,
		lradc->base + LRADC_CTRL1 + STMP_OFFSET_REG_SET);
}

static int mxs_lradc_ts_open(struct input_dev *dev)
{
	struct mxs_lradc *lradc = input_get_drvdata(dev);

	/* The touchscreen is starting. */
	lradc->stop_touchscreen = false;

	/* Enable the touch-detect circuitry. */
	writel(LRADC_CTRL0_TOUCH_DETECT_ENABLE,
		lradc->base + LRADC_CTRL0 + STMP_OFFSET_REG_SET);

	/* Enable the touch-detect IRQ. */
	writel(LRADC_CTRL1_TOUCH_DETECT_IRQ_EN,
		lradc->base + LRADC_CTRL1 + STMP_OFFSET_REG_SET);

	return 0;
}

static void mxs_lradc_ts_close(struct input_dev *dev)
{
	struct mxs_lradc *lradc = input_get_drvdata(dev);

	/* Indicate the touchscreen is stopping. */
	lradc->stop_touchscreen = true;
	mb();

	/* Wait until touchscreen thread finishes any possible remnants. */
	cancel_work_sync(&lradc->ts_work);

	/* Disable touchscreen touch-detect IRQ. */
	writel(LRADC_CTRL1_TOUCH_DETECT_IRQ_EN,
		lradc->base + LRADC_CTRL1 + STMP_OFFSET_REG_CLR);

	/* Power-down touchscreen touch-detect circuitry. */
	writel(LRADC_CTRL0_TOUCH_DETECT_ENABLE,
		lradc->base + LRADC_CTRL0 + STMP_OFFSET_REG_CLR);
}

static int mxs_lradc_ts_register(struct mxs_lradc *lradc)
{
	struct input_dev *input;
	struct device *dev = lradc->dev;
	int ret;

	if (!lradc->use_touchscreen)
		return 0;

	input = input_allocate_device();
	if (!input) {
		dev_err(dev, "Failed to allocate TS device!\n");
		return -ENOMEM;
	}

	input->name = DRIVER_NAME;
	input->id.bustype = BUS_HOST;
	input->dev.parent = dev;
	input->open = mxs_lradc_ts_open;
	input->close = mxs_lradc_ts_close;

	__set_bit(EV_ABS, input->evbit);
	__set_bit(EV_KEY, input->evbit);
	__set_bit(BTN_TOUCH, input->keybit);
	input_set_abs_params(input, ABS_X, 0, LRADC_CH_VALUE_MASK, 0, 0);
	input_set_abs_params(input, ABS_Y, 0, LRADC_CH_VALUE_MASK, 0, 0);
	input_set_abs_params(input, ABS_PRESSURE, 0, LRADC_CH_VALUE_MASK, 0, 0);

	lradc->ts_input = input;
	input_set_drvdata(input, lradc);
	ret = input_register_device(input);
	if (ret)
		input_free_device(lradc->ts_input);

	return ret;
}

static void mxs_lradc_ts_unregister(struct mxs_lradc *lradc)
{
	if (!lradc->use_touchscreen)
		return;

	cancel_work_sync(&lradc->ts_work);

	input_unregister_device(lradc->ts_input);
}

/*
 * IRQ Handling
 */
static irqreturn_t mxs_lradc_handle_irq(int irq, void *data)
{
	struct iio_dev *iio = data;
	struct mxs_lradc *lradc = iio_priv(iio);
	unsigned long reg = readl(lradc->base + LRADC_CTRL1);
	const uint32_t ts_irq_mask =
		LRADC_CTRL1_TOUCH_DETECT_IRQ_EN |
		LRADC_CTRL1_TOUCH_DETECT_IRQ;

	if (!(reg & LRADC_CTRL1_LRADC_IRQ_MASK))
		return IRQ_NONE;

	/*
	 * Touchscreen IRQ handling code has priority and therefore
	 * is placed here. In case touchscreen IRQ arrives, disable
	 * it ASAP
	 */
	if (reg & LRADC_CTRL1_TOUCH_DETECT_IRQ) {
		writel(ts_irq_mask,
			lradc->base + LRADC_CTRL1 + STMP_OFFSET_REG_CLR);
		if (!lradc->stop_touchscreen)
			schedule_work(&lradc->ts_work);
	}

	if (iio_buffer_enabled(iio))
		iio_trigger_poll(iio->trig, iio_get_time_ns());
	else if (reg & LRADC_CTRL1_LRADC_IRQ(0))
		complete(&lradc->completion);

	writel(reg & LRADC_CTRL1_LRADC_IRQ_MASK,
		lradc->base + LRADC_CTRL1 + STMP_OFFSET_REG_CLR);

	return IRQ_HANDLED;
}

/*
 * Trigger handling
 */
static irqreturn_t mxs_lradc_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *iio = pf->indio_dev;
	struct mxs_lradc *lradc = iio_priv(iio);
	const uint32_t chan_value = LRADC_CH_ACCUMULATE |
		((LRADC_DELAY_TIMER_LOOP - 1) << LRADC_CH_NUM_SAMPLES_OFFSET);
	unsigned int i, j = 0;

	for_each_set_bit(i, iio->active_scan_mask, iio->masklength) {
		lradc->buffer[j] = readl(lradc->base + LRADC_CH(j));
		writel(chan_value, lradc->base + LRADC_CH(j));
		lradc->buffer[j] &= LRADC_CH_VALUE_MASK;
		lradc->buffer[j] /= LRADC_DELAY_TIMER_LOOP;
		j++;
	}

	if (iio->scan_timestamp) {
		s64 *timestamp = (s64 *)((u8 *)lradc->buffer +
					ALIGN(j, sizeof(s64)));
		*timestamp = pf->timestamp;
	}

	iio_push_to_buffers(iio, (u8 *)lradc->buffer);

	iio_trigger_notify_done(iio->trig);

	return IRQ_HANDLED;
}

static int mxs_lradc_configure_trigger(struct iio_trigger *trig, bool state)
{
	struct iio_dev *iio = iio_trigger_get_drvdata(trig);
	struct mxs_lradc *lradc = iio_priv(iio);
	const uint32_t st = state ? STMP_OFFSET_REG_SET : STMP_OFFSET_REG_CLR;

	writel(LRADC_DELAY_KICK, lradc->base + LRADC_DELAY(0) + st);

	return 0;
}

static const struct iio_trigger_ops mxs_lradc_trigger_ops = {
	.owner = THIS_MODULE,
	.set_trigger_state = &mxs_lradc_configure_trigger,
};

static int mxs_lradc_trigger_init(struct iio_dev *iio)
{
	int ret;
	struct iio_trigger *trig;

	trig = iio_trigger_alloc("%s-dev%i", iio->name, iio->id);
	if (trig == NULL)
		return -ENOMEM;

	trig->dev.parent = iio->dev.parent;
	iio_trigger_set_drvdata(trig, iio);
	trig->ops = &mxs_lradc_trigger_ops;

	ret = iio_trigger_register(trig);
	if (ret) {
		iio_trigger_free(trig);
		return ret;
	}

	iio->trig = trig;

	return 0;
}

static void mxs_lradc_trigger_remove(struct iio_dev *iio)
{
	iio_trigger_unregister(iio->trig);
	iio_trigger_free(iio->trig);
}

static int mxs_lradc_buffer_preenable(struct iio_dev *iio)
{
	struct mxs_lradc *lradc = iio_priv(iio);
	int ret = 0, chan, ofs = 0;
	unsigned long enable = 0;
	uint32_t ctrl4_set = 0;
	uint32_t ctrl4_clr = 0;
	uint32_t ctrl1_irq = 0;
	const uint32_t chan_value = LRADC_CH_ACCUMULATE |
		((LRADC_DELAY_TIMER_LOOP - 1) << LRADC_CH_NUM_SAMPLES_OFFSET);
	const int len = bitmap_weight(iio->active_scan_mask, LRADC_MAX_TOTAL_CHANS);

	if (!len)
		return -EINVAL;

	/*
	 * Lock the driver so raw access can not be done during buffered
	 * operation. This simplifies the code a lot.
	 */
	ret = mutex_trylock(&lradc->lock);
	if (!ret)
		return -EBUSY;

	lradc->buffer = kmalloc(len * sizeof(*lradc->buffer), GFP_KERNEL);
	if (!lradc->buffer) {
		ret = -ENOMEM;
		goto err_mem;
	}

	ret = iio_sw_buffer_preenable(iio);
	if (ret < 0)
		goto err_buf;

	writel(LRADC_CTRL1_LRADC_IRQ_EN_MASK,
		lradc->base + LRADC_CTRL1 + STMP_OFFSET_REG_CLR);
	writel(0xff, lradc->base + LRADC_CTRL0 + STMP_OFFSET_REG_CLR);

	for_each_set_bit(chan, iio->active_scan_mask, LRADC_MAX_TOTAL_CHANS) {
		ctrl4_set |= chan << LRADC_CTRL4_LRADCSELECT_OFFSET(ofs);
		ctrl4_clr |= LRADC_CTRL4_LRADCSELECT_MASK(ofs);
		ctrl1_irq |= LRADC_CTRL1_LRADC_IRQ_EN(ofs);
		writel(chan_value, lradc->base + LRADC_CH(ofs));
		bitmap_set(&enable, ofs, 1);
		ofs++;
	}

	writel(LRADC_DELAY_TRIGGER_LRADCS_MASK | LRADC_DELAY_KICK,
		lradc->base + LRADC_DELAY(0) + STMP_OFFSET_REG_CLR);

	writel(ctrl4_clr, lradc->base + LRADC_CTRL4 + STMP_OFFSET_REG_CLR);
	writel(ctrl4_set, lradc->base + LRADC_CTRL4 + STMP_OFFSET_REG_SET);

	writel(ctrl1_irq, lradc->base + LRADC_CTRL1 + STMP_OFFSET_REG_SET);

	writel(enable << LRADC_DELAY_TRIGGER_LRADCS_OFFSET,
		lradc->base + LRADC_DELAY(0) + STMP_OFFSET_REG_SET);

	return 0;

err_buf:
	kfree(lradc->buffer);
err_mem:
	mutex_unlock(&lradc->lock);
	return ret;
}

static int mxs_lradc_buffer_postdisable(struct iio_dev *iio)
{
	struct mxs_lradc *lradc = iio_priv(iio);

	writel(LRADC_DELAY_TRIGGER_LRADCS_MASK | LRADC_DELAY_KICK,
		lradc->base + LRADC_DELAY(0) + STMP_OFFSET_REG_CLR);

	writel(0xff, lradc->base + LRADC_CTRL0 + STMP_OFFSET_REG_CLR);
	writel(LRADC_CTRL1_LRADC_IRQ_EN_MASK,
		lradc->base + LRADC_CTRL1 + STMP_OFFSET_REG_CLR);

	kfree(lradc->buffer);
	mutex_unlock(&lradc->lock);

	return 0;
}

static bool mxs_lradc_validate_scan_mask(struct iio_dev *iio,
					const unsigned long *mask)
{
	struct mxs_lradc *lradc = iio_priv(iio);
	const int len = iio->masklength;
	const int map_chans = bitmap_weight(mask, len);
	int rsvd_chans = 0;
	unsigned long rsvd_mask = 0;

	if (lradc->use_touchbutton)
		rsvd_mask |= CHAN_MASK_TOUCHBUTTON;
	if (lradc->use_touchscreen == MXS_LRADC_TOUCHSCREEN_4WIRE)
		rsvd_mask |= CHAN_MASK_TOUCHSCREEN_4WIRE;
	if (lradc->use_touchscreen == MXS_LRADC_TOUCHSCREEN_5WIRE)
		rsvd_mask |= CHAN_MASK_TOUCHSCREEN_5WIRE;

	if (lradc->use_touchbutton)
		rsvd_chans++;
	if (lradc->use_touchscreen)
		rsvd_chans++;

	/* Test for attempts to map channels with special mode of operation. */
	if (bitmap_intersects(mask, &rsvd_mask, len))
		return false;

	/* Test for attempts to map more channels then available slots. */
	if (map_chans + rsvd_chans > LRADC_MAX_MAPPED_CHANS)
		return false;

	return true;
}

static const struct iio_buffer_setup_ops mxs_lradc_buffer_ops = {
	.preenable = &mxs_lradc_buffer_preenable,
	.postenable = &iio_triggered_buffer_postenable,
	.predisable = &iio_triggered_buffer_predisable,
	.postdisable = &mxs_lradc_buffer_postdisable,
	.validate_scan_mask = &mxs_lradc_validate_scan_mask,
};

/*
 * Driver initialization
 */

#define MXS_ADC_CHAN(idx, chan_type) {				\
	.type = (chan_type),					\
	.indexed = 1,						\
	.scan_index = (idx),					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.channel = (idx),					\
	.scan_type = {						\
		.sign = 'u',					\
		.realbits = 18,					\
		.storagebits = 32,				\
	},							\
}

static const struct iio_chan_spec mxs_lradc_chan_spec[] = {
	MXS_ADC_CHAN(0, IIO_VOLTAGE),
	MXS_ADC_CHAN(1, IIO_VOLTAGE),
	MXS_ADC_CHAN(2, IIO_VOLTAGE),
	MXS_ADC_CHAN(3, IIO_VOLTAGE),
	MXS_ADC_CHAN(4, IIO_VOLTAGE),
	MXS_ADC_CHAN(5, IIO_VOLTAGE),
	MXS_ADC_CHAN(6, IIO_VOLTAGE),
	MXS_ADC_CHAN(7, IIO_VOLTAGE),	/* VBATT */
	MXS_ADC_CHAN(8, IIO_TEMP),	/* Temp sense 0 */
	MXS_ADC_CHAN(9, IIO_TEMP),	/* Temp sense 1 */
	MXS_ADC_CHAN(10, IIO_VOLTAGE),	/* VDDIO */
	MXS_ADC_CHAN(11, IIO_VOLTAGE),	/* VTH */
	MXS_ADC_CHAN(12, IIO_VOLTAGE),	/* VDDA */
	MXS_ADC_CHAN(13, IIO_VOLTAGE),	/* VDDD */
	MXS_ADC_CHAN(14, IIO_VOLTAGE),	/* VBG */
	MXS_ADC_CHAN(15, IIO_VOLTAGE),	/* VDD5V */
};

static void mxs_lradc_hw_init(struct mxs_lradc *lradc)
{
	/* The ADC always uses DELAY CHANNEL 0. */
	const uint32_t adc_cfg =
		(1 << (LRADC_DELAY_TRIGGER_DELAYS_OFFSET + 0)) |
		(LRADC_DELAY_TIMER_PER << LRADC_DELAY_DELAY_OFFSET);

	stmp_reset_block(lradc->base);

	/* Configure DELAY CHANNEL 0 for generic ADC sampling. */
	writel(adc_cfg, lradc->base + LRADC_DELAY(0));

	/* Disable remaining DELAY CHANNELs */
	writel(0, lradc->base + LRADC_DELAY(1));
	writel(0, lradc->base + LRADC_DELAY(2));
	writel(0, lradc->base + LRADC_DELAY(3));

	/* Configure the touchscreen type */
	writel(LRADC_CTRL0_TOUCH_SCREEN_TYPE,
		lradc->base + LRADC_CTRL0 + STMP_OFFSET_REG_CLR);

	if (lradc->use_touchscreen == MXS_LRADC_TOUCHSCREEN_5WIRE) {
		writel(LRADC_CTRL0_TOUCH_SCREEN_TYPE,
			lradc->base + LRADC_CTRL0 + STMP_OFFSET_REG_SET);
	}

	/* Start internal temperature sensing. */
	writel(0, lradc->base + LRADC_CTRL2);
}

static void mxs_lradc_hw_stop(struct mxs_lradc *lradc)
{
	int i;

	writel(LRADC_CTRL1_LRADC_IRQ_EN_MASK,
		lradc->base + LRADC_CTRL1 + STMP_OFFSET_REG_CLR);

	for (i = 0; i < LRADC_MAX_DELAY_CHANS; i++)
		writel(0, lradc->base + LRADC_DELAY(i));
}

static const struct of_device_id mxs_lradc_dt_ids[] = {
	{ .compatible = "fsl,imx23-lradc", .data = (void *)IMX23_LRADC, },
	{ .compatible = "fsl,imx28-lradc", .data = (void *)IMX28_LRADC, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mxs_lradc_dt_ids);

static int mxs_lradc_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id =
		of_match_device(mxs_lradc_dt_ids, &pdev->dev);
	const struct mxs_lradc_of_config *of_cfg =
		&mxs_lradc_of_config[(enum mxs_lradc_id)of_id->data];
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct mxs_lradc *lradc;
	struct iio_dev *iio;
	struct resource *iores;
	uint32_t ts_wires = 0;
	int ret = 0;
	int i;

	/* Allocate the IIO device. */
	iio = iio_device_alloc(sizeof(*lradc));
	if (!iio) {
		dev_err(dev, "Failed to allocate IIO device\n");
		return -ENOMEM;
	}

	lradc = iio_priv(iio);

	/* Grab the memory area */
	iores = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	lradc->dev = &pdev->dev;
	lradc->base = devm_ioremap_resource(dev, iores);
	if (IS_ERR(lradc->base)) {
		ret = PTR_ERR(lradc->base);
		goto err_addr;
	}

	INIT_WORK(&lradc->ts_work, mxs_lradc_ts_work);

	/* Check if touchscreen is enabled in DT. */
	ret = of_property_read_u32(node, "fsl,lradc-touchscreen-wires",
				&ts_wires);
	if (ret)
		dev_info(dev, "Touchscreen not enabled.\n");
	else if (ts_wires == 4)
		lradc->use_touchscreen = MXS_LRADC_TOUCHSCREEN_4WIRE;
	else if (ts_wires == 5)
		lradc->use_touchscreen = MXS_LRADC_TOUCHSCREEN_5WIRE;
	else
		dev_warn(dev, "Unsupported number of touchscreen wires (%d)\n",
				ts_wires);

	/* Grab all IRQ sources */
	for (i = 0; i < of_cfg->irq_count; i++) {
		lradc->irq[i] = platform_get_irq(pdev, i);
		if (lradc->irq[i] < 0) {
			ret = -EINVAL;
			goto err_addr;
		}

		ret = devm_request_irq(dev, lradc->irq[i],
					mxs_lradc_handle_irq, 0,
					of_cfg->irq_name[i], iio);
		if (ret)
			goto err_addr;
	}

	platform_set_drvdata(pdev, iio);

	init_completion(&lradc->completion);
	mutex_init(&lradc->lock);

	iio->name = pdev->name;
	iio->dev.parent = &pdev->dev;
	iio->info = &mxs_lradc_iio_info;
	iio->modes = INDIO_DIRECT_MODE;
	iio->channels = mxs_lradc_chan_spec;
	iio->num_channels = ARRAY_SIZE(mxs_lradc_chan_spec);

	ret = iio_triggered_buffer_setup(iio, &iio_pollfunc_store_time,
				&mxs_lradc_trigger_handler,
				&mxs_lradc_buffer_ops);
	if (ret)
		goto err_addr;

	ret = mxs_lradc_trigger_init(iio);
	if (ret)
		goto err_trig;

	/* Configure the hardware. */
	mxs_lradc_hw_init(lradc);

	/* Register the touchscreen input device. */
	ret = mxs_lradc_ts_register(lradc);
	if (ret)
		goto err_dev;

	/* Register IIO device. */
	ret = iio_device_register(iio);
	if (ret) {
		dev_err(dev, "Failed to register IIO device\n");
		goto err_ts;
	}

	return 0;

err_ts:
	mxs_lradc_ts_unregister(lradc);
err_dev:
	mxs_lradc_trigger_remove(iio);
err_trig:
	iio_triggered_buffer_cleanup(iio);
err_addr:
	iio_device_free(iio);
	return ret;
}

static int mxs_lradc_remove(struct platform_device *pdev)
{
	struct iio_dev *iio = platform_get_drvdata(pdev);
	struct mxs_lradc *lradc = iio_priv(iio);

	mxs_lradc_ts_unregister(lradc);

	mxs_lradc_hw_stop(lradc);

	iio_device_unregister(iio);
	iio_triggered_buffer_cleanup(iio);
	mxs_lradc_trigger_remove(iio);
	iio_device_free(iio);

	return 0;
}

static struct platform_driver mxs_lradc_driver = {
	.driver	= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = mxs_lradc_dt_ids,
	},
	.probe	= mxs_lradc_probe,
	.remove	= mxs_lradc_remove,
};

module_platform_driver(mxs_lradc_driver);

MODULE_AUTHOR("Marek Vasut <marex@denx.de>");
MODULE_DESCRIPTION("Freescale i.MX28 LRADC driver");
MODULE_LICENSE("GPL v2");
