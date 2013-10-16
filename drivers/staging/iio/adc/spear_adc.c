/*
 * ST SPEAr ADC driver
 *
 * Copyright 2012 Stefan Roese <sr@denx.de>
 *
 * Licensed under the GPL-2.
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

/*
 * SPEAR registers definitions
 */

#define SCAN_RATE_LO(x)		((x) & 0xFFFF)
#define SCAN_RATE_HI(x)		(((x) >> 0x10) & 0xFFFF)
#define CLK_LOW(x)		(((x) & 0xf) << 0)
#define CLK_HIGH(x)		(((x) & 0xf) << 4)

/* Bit definitions for SPEAR_ADC_STATUS */
#define START_CONVERSION	(1 << 0)
#define CHANNEL_NUM(x)		((x) << 1)
#define ADC_ENABLE		(1 << 4)
#define AVG_SAMPLE(x)		((x) << 5)
#define VREF_INTERNAL		(1 << 9)

#define DATA_MASK		0x03ff
#define DATA_BITS		10

#define MOD_NAME "spear-adc"

#define ADC_CHANNEL_NUM		8

#define CLK_MIN			2500000
#define CLK_MAX			20000000

struct adc_regs_spear3xx {
	u32 status;
	u32 average;
	u32 scan_rate;
	u32 clk;	/* Not avail for 1340 & 1310 */
	u32 ch_ctrl[ADC_CHANNEL_NUM];
	u32 ch_data[ADC_CHANNEL_NUM];
};

struct chan_data {
	u32 lsb;
	u32 msb;
};

struct adc_regs_spear6xx {
	u32 status;
	u32 pad[2];
	u32 clk;
	u32 ch_ctrl[ADC_CHANNEL_NUM];
	struct chan_data ch_data[ADC_CHANNEL_NUM];
	u32 scan_rate_lo;
	u32 scan_rate_hi;
	struct chan_data average;
};

struct spear_adc_info {
	struct device_node *np;
	struct adc_regs_spear3xx __iomem *adc_base_spear3xx;
	struct adc_regs_spear6xx __iomem *adc_base_spear6xx;
	struct clk *clk;
	struct completion completion;
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
static void spear_adc_set_status(struct spear_adc_info *info, u32 val)
{
	__raw_writel(val, &info->adc_base_spear6xx->status);
}

static void spear_adc_set_clk(struct spear_adc_info *info, u32 val)
{
	u32 clk_high, clk_low, count;
	u32 apb_clk = clk_get_rate(info->clk);

	count = (apb_clk + val - 1) / val;
	clk_low = count / 2;
	clk_high = count - clk_low;
	info->current_clk = apb_clk / count;

	__raw_writel(CLK_LOW(clk_low) | CLK_HIGH(clk_high),
		     &info->adc_base_spear6xx->clk);
}

static void spear_adc_set_ctrl(struct spear_adc_info *info, int n,
			       u32 val)
{
	__raw_writel(val, &info->adc_base_spear6xx->ch_ctrl[n]);
}

static u32 spear_adc_get_average(struct spear_adc_info *info)
{
	if (of_device_is_compatible(info->np, "st,spear600-adc")) {
		return __raw_readl(&info->adc_base_spear6xx->average.msb) &
			DATA_MASK;
	} else {
		return __raw_readl(&info->adc_base_spear3xx->average) &
			DATA_MASK;
	}
}

static void spear_adc_set_scanrate(struct spear_adc_info *info, u32 rate)
{
	if (of_device_is_compatible(info->np, "st,spear600-adc")) {
		__raw_writel(SCAN_RATE_LO(rate),
			     &info->adc_base_spear6xx->scan_rate_lo);
		__raw_writel(SCAN_RATE_HI(rate),
			     &info->adc_base_spear6xx->scan_rate_hi);
	} else {
		__raw_writel(rate, &info->adc_base_spear3xx->scan_rate);
	}
}

static int spear_read_raw(struct iio_dev *indio_dev,
			  struct iio_chan_spec const *chan,
			  int *val,
			  int *val2,
			  long mask)
{
	struct spear_adc_info *info = iio_priv(indio_dev);
	u32 status;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&indio_dev->mlock);

		status = CHANNEL_NUM(chan->channel) |
			AVG_SAMPLE(info->avg_samples) |
			START_CONVERSION | ADC_ENABLE;
		if (info->vref_external == 0)
			status |= VREF_INTERNAL;

		spear_adc_set_status(info, status);
		wait_for_completion(&info->completion); /* set by ISR */
		*val = info->value;

		mutex_unlock(&indio_dev->mlock);

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		*val = info->vref_external;
		*val2 = DATA_BITS;
		return IIO_VAL_FRACTIONAL_LOG2;
	}

	return -EINVAL;
}

#define SPEAR_ADC_CHAN(idx) {				\
	.type = IIO_VOLTAGE,				\
	.indexed = 1,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
	.channel = idx,					\
	.scan_type = {					\
		.sign = 'u',				\
		.storagebits = 16,			\
	},						\
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
	struct spear_adc_info *info = (struct spear_adc_info *)dev_id;

	/* Read value to clear IRQ */
	info->value = spear_adc_get_average(info);
	complete(&info->completion);

	return IRQ_HANDLED;
}

static int spear_adc_configure(struct spear_adc_info *info)
{
	int i;

	/* Reset ADC core */
	spear_adc_set_status(info, 0);
	__raw_writel(0, &info->adc_base_spear6xx->clk);
	for (i = 0; i < 8; i++)
		spear_adc_set_ctrl(info, i, 0);
	spear_adc_set_scanrate(info, 0);

	spear_adc_set_clk(info, info->sampling_freq);

	return 0;
}

static ssize_t spear_adc_read_frequency(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct spear_adc_info *info = iio_priv(indio_dev);

	return sprintf(buf, "%d\n", info->current_clk);
}

static ssize_t spear_adc_write_frequency(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf,
					 size_t len)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct spear_adc_info *info = iio_priv(indio_dev);
	u32 clk_high, clk_low, count;
	u32 apb_clk = clk_get_rate(info->clk);
	unsigned long lval;
	int ret;

	ret = kstrtoul(buf, 10, &lval);
	if (ret)
		return ret;

	mutex_lock(&indio_dev->mlock);

	if ((lval < CLK_MIN) || (lval > CLK_MAX)) {
		ret = -EINVAL;
		goto out;
	}

	count = (apb_clk + lval - 1) / lval;
	clk_low = count / 2;
	clk_high = count - clk_low;
	info->current_clk = apb_clk / count;
	spear_adc_set_clk(info, lval);

out:
	mutex_unlock(&indio_dev->mlock);

	return ret ? ret : len;
}

static IIO_DEV_ATTR_SAMP_FREQ(S_IWUSR | S_IRUGO,
			      spear_adc_read_frequency,
			      spear_adc_write_frequency);

static struct attribute *spear_attributes[] = {
	&iio_dev_attr_sampling_frequency.dev_attr.attr,
	NULL
};

static const struct attribute_group spear_attribute_group = {
	.attrs = spear_attributes,
};

static const struct iio_info spear_adc_iio_info = {
	.read_raw = &spear_read_raw,
	.attrs = &spear_attribute_group,
	.driver_module = THIS_MODULE,
};

static int spear_adc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct spear_adc_info *info;
	struct iio_dev *iodev = NULL;
	int ret = -ENODEV;
	int irq;

	iodev = devm_iio_device_alloc(dev, sizeof(struct spear_adc_info));
	if (!iodev) {
		dev_err(dev, "failed allocating iio device\n");
		return -ENOMEM;
	}

	info = iio_priv(iodev);
	info->np = np;

	/*
	 * SPEAr600 has a different register layout than other SPEAr SoC's
	 * (e.g. SPEAr3xx). Let's provide two register base addresses
	 * to support multi-arch kernels.
	 */
	info->adc_base_spear6xx = of_iomap(np, 0);
	if (!info->adc_base_spear6xx) {
		dev_err(dev, "failed mapping memory\n");
		return -ENOMEM;
	}
	info->adc_base_spear3xx =
		(struct adc_regs_spear3xx __iomem *)info->adc_base_spear6xx;

	info->clk = clk_get(dev, NULL);
	if (IS_ERR(info->clk)) {
		dev_err(dev, "failed getting clock\n");
		goto errout1;
	}

	ret = clk_prepare_enable(info->clk);
	if (ret) {
		dev_err(dev, "failed enabling clock\n");
		goto errout2;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_err(dev, "failed getting interrupt resource\n");
		ret = -EINVAL;
		goto errout3;
	}

	ret = devm_request_irq(dev, irq, spear_adc_isr, 0, MOD_NAME, info);
	if (ret < 0) {
		dev_err(dev, "failed requesting interrupt\n");
		goto errout3;
	}

	if (of_property_read_u32(np, "sampling-frequency",
				 &info->sampling_freq)) {
		dev_err(dev, "sampling-frequency missing in DT\n");
		ret = -EINVAL;
		goto errout3;
	}

	/*
	 * Optional avg_samples defaults to 0, resulting in single data
	 * conversion
	 */
	of_property_read_u32(np, "average-samples", &info->avg_samples);

	/*
	 * Optional vref_external defaults to 0, resulting in internal vref
	 * selection
	 */
	of_property_read_u32(np, "vref-external", &info->vref_external);

	spear_adc_configure(info);

	platform_set_drvdata(pdev, iodev);

	init_completion(&info->completion);

	iodev->name = MOD_NAME;
	iodev->dev.parent = dev;
	iodev->info = &spear_adc_iio_info;
	iodev->modes = INDIO_DIRECT_MODE;
	iodev->channels = spear_adc_iio_channels;
	iodev->num_channels = ARRAY_SIZE(spear_adc_iio_channels);

	ret = iio_device_register(iodev);
	if (ret)
		goto errout3;

	dev_info(dev, "SPEAR ADC driver loaded, IRQ %d\n", irq);

	return 0;

errout3:
	clk_disable_unprepare(info->clk);
errout2:
	clk_put(info->clk);
errout1:
	iounmap(info->adc_base_spear6xx);
	return ret;
}

static int spear_adc_remove(struct platform_device *pdev)
{
	struct iio_dev *iodev = platform_get_drvdata(pdev);
	struct spear_adc_info *info = iio_priv(iodev);

	iio_device_unregister(iodev);
	clk_disable_unprepare(info->clk);
	clk_put(info->clk);
	iounmap(info->adc_base_spear6xx);

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
		.name	= MOD_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(spear_adc_dt_ids),
	},
};

module_platform_driver(spear_adc_driver);

MODULE_AUTHOR("Stefan Roese <sr@denx.de>");
MODULE_DESCRIPTION("SPEAr ADC driver");
MODULE_LICENSE("GPL");
