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

#include <mach/mxs.h>
#include <mach/common.h>

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

static const char * const mxs_lradc_irq_name[] = {
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

struct mxs_lradc_chan {
	uint8_t				slot;
	uint8_t				flags;
};

struct mxs_lradc {
	struct device		*dev;
	void __iomem		*base;
	int			irq[13];

	uint32_t		*buffer;
	struct iio_trigger	*trig;

	struct mutex		lock;

	uint8_t			enable;

	struct completion	completion;
};

#define	LRADC_CTRL0				0x00
#define LRADC_CTRL0_TOUCH_DETECT_ENABLE		(1 << 23)
#define LRADC_CTRL0_TOUCH_SCREEN_TYPE		(1 << 22)

#define	LRADC_CTRL1				0x10
#define	LRADC_CTRL1_LRADC_IRQ(n)		(1 << (n))
#define	LRADC_CTRL1_LRADC_IRQ_MASK		0x1fff
#define	LRADC_CTRL1_LRADC_IRQ_EN(n)		(1 << ((n) + 16))
#define	LRADC_CTRL1_LRADC_IRQ_EN_MASK		(0x1fff << 16)

#define	LRADC_CTRL2				0x20
#define	LRADC_CTRL2_TEMPSENSE_PWD		(1 << 15)

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

	if (m != IIO_CHAN_INFO_RAW)
		return -EINVAL;

	/* Check for invalid channel */
	if (chan->channel > LRADC_MAX_TOTAL_CHANS)
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

	writel(chan->channel, lradc->base + LRADC_CTRL4);
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
 * IRQ Handling
 */
static irqreturn_t mxs_lradc_handle_irq(int irq, void *data)
{
	struct iio_dev *iio = data;
	struct mxs_lradc *lradc = iio_priv(iio);
	unsigned long reg = readl(lradc->base + LRADC_CTRL1);

	if (!(reg & LRADC_CTRL1_LRADC_IRQ_MASK))
		return IRQ_NONE;

	/*
	 * Touchscreen IRQ handling code shall probably have priority
	 * and therefore shall be placed here.
	 */

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
	int i, j = 0;

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
	struct iio_dev *iio = trig->private_data;
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
	trig->private_data = iio;
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
	struct iio_buffer *buffer = iio->buffer;
	int ret = 0, chan, ofs = 0, enable = 0;
	uint32_t ctrl4 = 0;
	uint32_t ctrl1_irq = 0;
	const uint32_t chan_value = LRADC_CH_ACCUMULATE |
		((LRADC_DELAY_TIMER_LOOP - 1) << LRADC_CH_NUM_SAMPLES_OFFSET);
	const int len = bitmap_weight(buffer->scan_mask, LRADC_MAX_TOTAL_CHANS);

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

	for_each_set_bit(chan, buffer->scan_mask, LRADC_MAX_TOTAL_CHANS) {
		ctrl4 |= chan << LRADC_CTRL4_LRADCSELECT_OFFSET(ofs);
		ctrl1_irq |= LRADC_CTRL1_LRADC_IRQ_EN(ofs);
		writel(chan_value, lradc->base + LRADC_CH(ofs));
		enable |= 1 << ofs;
		ofs++;
	}

	writel(LRADC_DELAY_TRIGGER_LRADCS_MASK | LRADC_DELAY_KICK,
		lradc->base + LRADC_DELAY(0) + STMP_OFFSET_REG_CLR);

	writel(ctrl4, lradc->base + LRADC_CTRL4);
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
	const int mw = bitmap_weight(mask, iio->masklength);

	return mw <= LRADC_MAX_MAPPED_CHANS;
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
	.info_mask = IIO_CHAN_INFO_RAW_SEPARATE_BIT,		\
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
	int i;
	const uint32_t cfg =
		(LRADC_DELAY_TIMER_PER << LRADC_DELAY_DELAY_OFFSET);

	stmp_reset_block(lradc->base);

	for (i = 0; i < LRADC_MAX_DELAY_CHANS; i++)
		writel(cfg | (1 << (LRADC_DELAY_TRIGGER_DELAYS_OFFSET + i)),
			lradc->base + LRADC_DELAY(i));

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

static int mxs_lradc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mxs_lradc *lradc;
	struct iio_dev *iio;
	struct resource *iores;
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
	lradc->base = devm_request_and_ioremap(dev, iores);
	if (!lradc->base) {
		ret = -EADDRNOTAVAIL;
		goto err_addr;
	}

	/* Grab all IRQ sources */
	for (i = 0; i < 13; i++) {
		lradc->irq[i] = platform_get_irq(pdev, i);
		if (lradc->irq[i] < 0) {
			ret = -EINVAL;
			goto err_addr;
		}

		ret = devm_request_irq(dev, lradc->irq[i],
					mxs_lradc_handle_irq, 0,
					mxs_lradc_irq_name[i], iio);
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

	/* Register IIO device. */
	ret = iio_device_register(iio);
	if (ret) {
		dev_err(dev, "Failed to register IIO device\n");
		goto err_dev;
	}

	/* Configure the hardware. */
	mxs_lradc_hw_init(lradc);

	return 0;

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

	mxs_lradc_hw_stop(lradc);

	iio_device_unregister(iio);
	iio_triggered_buffer_cleanup(iio);
	mxs_lradc_trigger_remove(iio);
	iio_device_free(iio);

	return 0;
}

static const struct of_device_id mxs_lradc_dt_ids[] = {
	{ .compatible = "fsl,imx28-lradc", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mxs_lradc_dt_ids);

static struct platform_driver mxs_lradc_driver = {
	.driver	= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = mxs_lradc_dt_ids,
	},
	.probe	= mxs_lradc_probe,
	.remove	= __devexit_p(mxs_lradc_remove),
};

module_platform_driver(mxs_lradc_driver);

MODULE_AUTHOR("Marek Vasut <marex@denx.de>");
MODULE_DESCRIPTION("Freescale i.MX28 LRADC driver");
MODULE_LICENSE("GPL v2");
