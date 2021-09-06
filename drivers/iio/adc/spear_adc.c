// SPDX-License-Identifier: GPL-2.0-only
/*
 * ST SPEAr ADC driver
 *
 * Copyright 2012 Stefan Roese <sr@denx.de>
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/completion.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

/* SPEAR registers definitions */
#define SPEAR600_ADC_SCAN_RATE_LO(x)	((x) & 0xFFFF)
#define SPEAR600_ADC_SCAN_RATE_HI(x)	(((x) >> 0x10) & 0xFFFF)
#define SPEAR_ADC_CLK_LOW(x)		(((x) & 0xf) << 0)
#define SPEAR_ADC_CLK_HIGH(x)		(((x) & 0xf) << 4)

/* Bit definitions for SPEAR_ADC_STATUS */
#define SPEAR_ADC_STATUS_START_CONVERSION	BIT(0)
#define SPEAR_ADC_STATUS_CHANNEL_NUM(x)		((x) << 1)
#define SPEAR_ADC_STATUS_ADC_ENABLE		BIT(4)
#define SPEAR_ADC_STATUS_AVG_SAMPLE(x)		((x) << 5)
#define SPEAR_ADC_STATUS_VREF_INTERNAL		BIT(9)

#define SPEAR_ADC_DATA_MASK		0x03ff
#define SPEAR_ADC_DATA_BITS		10

#define SPEAR_ADC_MOD_NAME "spear-adc"

#define SPEAR_ADC_CHANNEL_NUM		8

#define SPEAR_ADC_CLK_MIN			2500000
#define SPEAR_ADC_CLK_MAX			20000000

struct adc_regs_spear3xx {
	u32 status;
	u32 average;
	u32 scan_rate;
	u32 clk;	/* Not avail for 1340 & 1310 */
	u32 ch_ctrl[SPEAR_ADC_CHANNEL_NUM];
	u32 ch_data[SPEAR_ADC_CHANNEL_NUM];
};

struct chan_data {
	u32 lsb;
	u32 msb;
};

struct adc_regs_spear6xx {
	u32 status;
	u32 pad[2];
	u32 clk;
	u32 ch_ctrl[SPEAR_ADC_CHANNEL_NUM];
	struct chan_data ch_data[SPEAR_ADC_CHANNEL_NUM];
	u32 scan_rate_lo;
	u32 scan_rate_hi;
	struct chan_data average;
};

struct spear_adc_state {
	struct device_node *np;
	struct adc_regs_spear3xx __iomem *adc_base_spear3xx;
	struct adc_regs_spear6xx __iomem *adc_base_spear6xx;
	struct clk *clk;
	struct completion completion;
	/*
	 * Lock to protect the device state during a potential concurrent
	 * read access from userspace. Reading a raw value requires a sequence
	 * of register writes, then a wait for a completion callback,
	 * and finally a register read, during which userspace could issue
	 * another read request. This lock protects a read access from
	 * ocurring before another one has finished.
	 */
	struct mutex lock;
	u32 current_clk;
	u32 sampling_freq;
	u32 avg_samples;
	u32 vref_external;
	u32 value;
};

/*
 * Functions to access some SPEAr ADC register. Abstracted into
 * static inline functions, because of different register offsets
 * on different SoC variants (SPEAr300 vs SPEAr600 etc).
 */
static void spear_adc_set_status(struct spear_adc_state *st, u32 val)
{
	__raw_writel(val, &st->adc_base_spear6xx->status);
}

static void spear_adc_set_clk(struct spear_adc_state *st, u32 val)
{
	u32 clk_high, clk_low, count;
	u32 apb_clk = clk_get_rate(st->clk);

	count = DIV_ROUND_UP(apb_clk, val);
	clk_low = count / 2;
	clk_high = count - clk_low;
	st->current_clk = apb_clk / count;

	__raw_writel(SPEAR_ADC_CLK_LOW(clk_low) | SPEAR_ADC_CLK_HIGH(clk_high),
		     &st->adc_base_spear6xx->clk);
}

static void spear_adc_set_ctrl(struct spear_adc_state *st, int n,
			       u32 val)
{
	__raw_writel(val, &st->adc_base_spear6xx->ch_ctrl[n]);
}

static u32 spear_adc_get_average(struct spear_adc_state *st)
{
	if (of_device_is_compatible(st->np, "st,spear600-adc")) {
		return __raw_readl(&st->adc_base_spear6xx->average.msb) &
			SPEAR_ADC_DATA_MASK;
	} else {
		return __raw_readl(&st->adc_base_spear3xx->average) &
			SPEAR_ADC_DATA_MASK;
	}
}

static void spear_adc_set_scanrate(struct spear_adc_state *st, u32 rate)
{
	if (of_device_is_compatible(st->np, "st,spear600-adc")) {
		__raw_writel(SPEAR600_ADC_SCAN_RATE_LO(rate),
			     &st->adc_base_spear6xx->scan_rate_lo);
		__raw_writel(SPEAR600_ADC_SCAN_RATE_HI(rate),
			     &st->adc_base_spear6xx->scan_rate_hi);
	} else {
		__raw_writel(rate, &st->adc_base_spear3xx->scan_rate);
	}
}

static int spear_adc_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val,
			      int *val2,
			      long mask)
{
	struct spear_adc_state *st = iio_priv(indio_dev);
	u32 status;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&st->lock);

		status = SPEAR_ADC_STATUS_CHANNEL_NUM(chan->channel) |
			SPEAR_ADC_STATUS_AVG_SAMPLE(st->avg_samples) |
			SPEAR_ADC_STATUS_START_CONVERSION |
			SPEAR_ADC_STATUS_ADC_ENABLE;
		if (st->vref_external == 0)
			status |= SPEAR_ADC_STATUS_VREF_INTERNAL;

		spear_adc_set_status(st, status);
		wait_for_completion(&st->completion); /* set by ISR */
		*val = st->value;

		mutex_unlock(&st->lock);

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		*val = st->vref_external;
		*val2 = SPEAR_ADC_DATA_BITS;
		return IIO_VAL_FRACTIONAL_LOG2;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = st->current_clk;
		return IIO_VAL_INT;
	}

	return -EINVAL;
}

static int spear_adc_write_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int val,
			       int val2,
			       long mask)
{
	struct spear_adc_state *st = iio_priv(indio_dev);
	int ret = 0;

	if (mask != IIO_CHAN_INFO_SAMP_FREQ)
		return -EINVAL;

	mutex_lock(&st->lock);

	if ((val < SPEAR_ADC_CLK_MIN) ||
	    (val > SPEAR_ADC_CLK_MAX) ||
	    (val2 != 0)) {
		ret = -EINVAL;
		goto out;
	}

	spear_adc_set_clk(st, val);

out:
	mutex_unlock(&st->lock);
	return ret;
}

#define SPEAR_ADC_CHAN(idx) {				\
	.type = IIO_VOLTAGE,				\
	.indexed = 1,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
	.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),\
	.channel = idx,					\
}

static const struct iio_chan_spec spear_adc_iio_channels[] = {
	SPEAR_ADC_CHAN(0),
	SPEAR_ADC_CHAN(1),
	SPEAR_ADC_CHAN(2),
	SPEAR_ADC_CHAN(3),
	SPEAR_ADC_CHAN(4),
	SPEAR_ADC_CHAN(5),
	SPEAR_ADC_CHAN(6),
	SPEAR_ADC_CHAN(7),
};

static irqreturn_t spear_adc_isr(int irq, void *dev_id)
{
	struct spear_adc_state *st = dev_id;

	/* Read value to clear IRQ */
	st->value = spear_adc_get_average(st);
	complete(&st->completion);

	return IRQ_HANDLED;
}

static int spear_adc_configure(struct spear_adc_state *st)
{
	int i;

	/* Reset ADC core */
	spear_adc_set_status(st, 0);
	__raw_writel(0, &st->adc_base_spear6xx->clk);
	for (i = 0; i < 8; i++)
		spear_adc_set_ctrl(st, i, 0);
	spear_adc_set_scanrate(st, 0);

	spear_adc_set_clk(st, st->sampling_freq);

	return 0;
}

static const struct iio_info spear_adc_info = {
	.read_raw = &spear_adc_read_raw,
	.write_raw = &spear_adc_write_raw,
};

static int spear_adc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct spear_adc_state *st;
	struct iio_dev *indio_dev = NULL;
	int ret = -ENODEV;
	int irq;

	indio_dev = devm_iio_device_alloc(dev, sizeof(struct spear_adc_state));
	if (!indio_dev) {
		dev_err(dev, "failed allocating iio device\n");
		return -ENOMEM;
	}

	st = iio_priv(indio_dev);

	mutex_init(&st->lock);

	st->np = np;

	/*
	 * SPEAr600 has a different register layout than other SPEAr SoC's
	 * (e.g. SPEAr3xx). Let's provide two register base addresses
	 * to support multi-arch kernels.
	 */
	st->adc_base_spear6xx = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(st->adc_base_spear6xx))
		return PTR_ERR(st->adc_base_spear6xx);

	st->adc_base_spear3xx =
		(struct adc_regs_spear3xx __iomem *)st->adc_base_spear6xx;

	st->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(st->clk)) {
		dev_err(dev, "failed getting clock\n");
		return PTR_ERR(st->clk);
	}

	ret = clk_prepare_enable(st->clk);
	if (ret) {
		dev_err(dev, "failed enabling clock\n");
		return ret;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		ret = -EINVAL;
		goto errout2;
	}

	ret = devm_request_irq(dev, irq, spear_adc_isr, 0, SPEAR_ADC_MOD_NAME,
			       st);
	if (ret < 0) {
		dev_err(dev, "failed requesting interrupt\n");
		goto errout2;
	}

	if (of_property_read_u32(np, "sampling-frequency",
				 &st->sampling_freq)) {
		dev_err(dev, "sampling-frequency missing in DT\n");
		ret = -EINVAL;
		goto errout2;
	}

	/*
	 * Optional avg_samples defaults to 0, resulting in single data
	 * conversion
	 */
	of_property_read_u32(np, "average-samples", &st->avg_samples);

	/*
	 * Optional vref_external defaults to 0, resulting in internal vref
	 * selection
	 */
	of_property_read_u32(np, "vref-external", &st->vref_external);

	spear_adc_configure(st);

	platform_set_drvdata(pdev, indio_dev);

	init_completion(&st->completion);

	indio_dev->name = SPEAR_ADC_MOD_NAME;
	indio_dev->info = &spear_adc_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = spear_adc_iio_channels;
	indio_dev->num_channels = ARRAY_SIZE(spear_adc_iio_channels);

	ret = iio_device_register(indio_dev);
	if (ret)
		goto errout2;

	dev_info(dev, "SPEAR ADC driver loaded, IRQ %d\n", irq);

	return 0;

errout2:
	clk_disable_unprepare(st->clk);
	return ret;
}

static int spear_adc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct spear_adc_state *st = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	clk_disable_unprepare(st->clk);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id spear_adc_dt_ids[] = {
	{ .compatible = "st,spear600-adc", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, spear_adc_dt_ids);
#endif

static struct platform_driver spear_adc_driver = {
	.probe		= spear_adc_probe,
	.remove		= spear_adc_remove,
	.driver		= {
		.name	= SPEAR_ADC_MOD_NAME,
		.of_match_table = of_match_ptr(spear_adc_dt_ids),
	},
};

module_platform_driver(spear_adc_driver);

MODULE_AUTHOR("Stefan Roese <sr@denx.de>");
MODULE_DESCRIPTION("SPEAr ADC driver");
MODULE_LICENSE("GPL");
