// SPDX-License-Identifier: GPL-2.0
/*
 * GPADC driver for sunxi platforms (D1, T113-S3 and R329)
 * Copyright (c) 2023 Maksim Kiselev <bigunclemax@gmail.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/reset.h>

#include <linux/iio/adc-helpers.h>
#include <linux/iio/iio.h>

#define SUN20I_GPADC_DRIVER_NAME	"sun20i-gpadc"

/* Register map definition */
#define SUN20I_GPADC_SR			0x00
#define SUN20I_GPADC_CTRL		0x04
#define SUN20I_GPADC_CS_EN		0x08
#define SUN20I_GPADC_FIFO_INTC		0x0c
#define SUN20I_GPADC_FIFO_INTS		0x10
#define SUN20I_GPADC_FIFO_DATA		0X14
#define SUN20I_GPADC_CB_DATA		0X18
#define SUN20I_GPADC_DATAL_INTC		0x20
#define SUN20I_GPADC_DATAH_INTC		0x24
#define SUN20I_GPADC_DATA_INTC		0x28
#define SUN20I_GPADC_DATAL_INTS		0x30
#define SUN20I_GPADC_DATAH_INTS		0x34
#define SUN20I_GPADC_DATA_INTS		0x38
#define SUN20I_GPADC_CH_CMP_DATA(x)	(0x40 + (x) * 4)
#define SUN20I_GPADC_CH_DATA(x)		(0x80 + (x) * 4)

#define SUN20I_GPADC_CTRL_ADC_AUTOCALI_EN_MASK		BIT(23)
#define SUN20I_GPADC_CTRL_WORK_MODE_MASK		GENMASK(19, 18)
#define SUN20I_GPADC_CTRL_ADC_EN_MASK			BIT(16)
#define SUN20I_GPADC_CS_EN_ADC_CH(x)			BIT(x)
#define SUN20I_GPADC_DATA_INTC_CH_DATA_IRQ_EN(x)	BIT(x)

#define SUN20I_GPADC_WORK_MODE_SINGLE			0

struct sun20i_gpadc_iio {
	void __iomem		*regs;
	struct completion	completion;
	int			last_channel;
	/*
	 * Lock to protect the device state during a potential concurrent
	 * read access from userspace. Reading a raw value requires a sequence
	 * of register writes, then a wait for a completion callback,
	 * and finally a register read, during which userspace could issue
	 * another read request. This lock protects a read access from
	 * ocurring before another one has finished.
	 */
	struct mutex		lock;
};

static int sun20i_gpadc_adc_read(struct sun20i_gpadc_iio *info,
				 struct iio_chan_spec const *chan, int *val)
{
	u32 ctrl;
	int ret = IIO_VAL_INT;

	mutex_lock(&info->lock);

	reinit_completion(&info->completion);

	if (info->last_channel != chan->channel) {
		info->last_channel = chan->channel;

		/* enable the analog input channel */
		writel(SUN20I_GPADC_CS_EN_ADC_CH(chan->channel),
		       info->regs + SUN20I_GPADC_CS_EN);

		/* enable the data irq for input channel */
		writel(SUN20I_GPADC_DATA_INTC_CH_DATA_IRQ_EN(chan->channel),
		       info->regs + SUN20I_GPADC_DATA_INTC);
	}

	/* enable the ADC function */
	ctrl = readl(info->regs + SUN20I_GPADC_CTRL);
	ctrl |= FIELD_PREP(SUN20I_GPADC_CTRL_ADC_EN_MASK, 1);
	writel(ctrl, info->regs + SUN20I_GPADC_CTRL);

	/*
	 * According to the datasheet maximum acquire time(TACQ) can be
	 * (65535+1)/24Mhz and conversion time(CONV_TIME) is always constant
	 * and equal to 14/24Mhz, so (TACQ+CONV_TIME) <= 2.73125ms.
	 * A 10ms delay should be enough to make sure an interrupt occurs in
	 * normal conditions. If it doesn't occur, then there is a timeout.
	 */
	if (!wait_for_completion_timeout(&info->completion, msecs_to_jiffies(10))) {
		ret = -ETIMEDOUT;
		goto err_unlock;
	}

	/* read the ADC data */
	*val = readl(info->regs + SUN20I_GPADC_CH_DATA(chan->channel));

err_unlock:
	mutex_unlock(&info->lock);

	return ret;
}

static int sun20i_gpadc_read_raw(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan, int *val,
				 int *val2, long mask)
{
	struct sun20i_gpadc_iio *info = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		return sun20i_gpadc_adc_read(info, chan, val);
	case IIO_CHAN_INFO_SCALE:
		/* value in mv = 1800mV / 4096 raw */
		*val = 1800;
		*val2 = 12;
		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		return -EINVAL;
	}
}

static irqreturn_t sun20i_gpadc_irq_handler(int irq, void *data)
{
	struct sun20i_gpadc_iio *info = data;

	/* clear data interrupt status register */
	writel(GENMASK(31, 0), info->regs + SUN20I_GPADC_DATA_INTS);

	complete(&info->completion);

	return IRQ_HANDLED;
}

static const struct iio_info sun20i_gpadc_iio_info = {
	.read_raw = sun20i_gpadc_read_raw,
};

static void sun20i_gpadc_reset_assert(void *data)
{
	struct reset_control *rst = data;

	reset_control_assert(rst);
}

static const struct iio_chan_spec sun20i_gpadc_chan_template = {
	.type = IIO_VOLTAGE,
	.indexed = 1,
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
};

static int sun20i_gpadc_alloc_channels(struct iio_dev *indio_dev,
				       struct device *dev)
{
	int num_channels;
	struct iio_chan_spec *channels;

	num_channels = devm_iio_adc_device_alloc_chaninfo_se(dev,
				&sun20i_gpadc_chan_template, -1, &channels);
	if (num_channels < 0)
		return num_channels;

	indio_dev->channels = channels;
	indio_dev->num_channels = num_channels;

	return 0;
}

static int sun20i_gpadc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct iio_dev *indio_dev;
	struct sun20i_gpadc_iio *info;
	struct reset_control *rst;
	struct clk *clk;
	int irq;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*info));
	if (!indio_dev)
		return -ENOMEM;

	info = iio_priv(indio_dev);
	info->last_channel = -1;

	mutex_init(&info->lock);
	init_completion(&info->completion);

	ret = sun20i_gpadc_alloc_channels(indio_dev, dev);
	if (ret)
		return ret;

	indio_dev->info = &sun20i_gpadc_iio_info;
	indio_dev->name = SUN20I_GPADC_DRIVER_NAME;

	info->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(info->regs))
		return PTR_ERR(info->regs);

	clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "failed to enable bus clock\n");

	rst = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(rst))
		return dev_err_probe(dev, PTR_ERR(rst), "failed to get reset control\n");

	ret = reset_control_deassert(rst);
	if (ret)
		return dev_err_probe(dev, ret, "failed to deassert reset\n");

	ret = devm_add_action_or_reset(dev, sun20i_gpadc_reset_assert, rst);
	if (ret)
		return ret;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(dev, irq, sun20i_gpadc_irq_handler, 0,
			       dev_name(dev), info);
	if (ret)
		return dev_err_probe(dev, ret, "failed requesting irq %d\n", irq);

	writel(FIELD_PREP(SUN20I_GPADC_CTRL_ADC_AUTOCALI_EN_MASK, 1) |
	       FIELD_PREP(SUN20I_GPADC_CTRL_WORK_MODE_MASK, SUN20I_GPADC_WORK_MODE_SINGLE),
	       info->regs + SUN20I_GPADC_CTRL);

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret)
		return dev_err_probe(dev, ret, "could not register the device\n");

	return 0;
}

static const struct of_device_id sun20i_gpadc_of_id[] = {
	{ .compatible = "allwinner,sun20i-d1-gpadc" },
	{ }
};
MODULE_DEVICE_TABLE(of, sun20i_gpadc_of_id);

static struct platform_driver sun20i_gpadc_driver = {
	.driver = {
		.name = SUN20I_GPADC_DRIVER_NAME,
		.of_match_table = sun20i_gpadc_of_id,
	},
	.probe = sun20i_gpadc_probe,
};
module_platform_driver(sun20i_gpadc_driver);

MODULE_DESCRIPTION("ADC driver for sunxi platforms");
MODULE_AUTHOR("Maksim Kiselev <bigunclemax@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_DRIVER");
