// SPDX-License-Identifier: GPL-2.0
/*
 * RZ/G2L A/D Converter driver
 *
 *  Copyright (c) 2021 Renesas Electronics Europe GmbH
 *
 * Author: Lad Prabhakar <prabhakar.mahadev-lad.rj@bp.renesas.com>
 */

#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/iio/iio.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/reset.h>

#define DRIVER_NAME		"rzg2l-adc"

#define RZG2L_ADM(n)			((n) * 0x4)
#define RZG2L_ADM0_ADCE			BIT(0)
#define RZG2L_ADM0_ADBSY		BIT(1)
#define RZG2L_ADM0_PWDWNB		BIT(2)
#define RZG2L_ADM0_SRESB		BIT(15)
#define RZG2L_ADM1_TRG			BIT(0)
#define RZG2L_ADM1_MS			BIT(2)
#define RZG2L_ADM1_BS			BIT(4)
#define RZG2L_ADM1_EGA_MASK		GENMASK(13, 12)
#define RZG2L_ADM3_ADIL_MASK		GENMASK(31, 24)
#define RZG2L_ADM3_ADCMP_MASK		GENMASK(23, 16)

#define RZG2L_ADINT			0x20
#define RZG2L_ADINT_CSEEN		BIT(16)
#define RZG2L_ADINT_INTS		BIT(31)

#define RZG2L_ADSTS			0x24
#define RZG2L_ADSTS_CSEST		BIT(16)

#define RZG2L_ADIVC			0x28
#define RZG2L_ADIVC_DIVADC_MASK		GENMASK(8, 0)
#define RZG2L_ADIVC_DIVADC_4		FIELD_PREP(RZG2L_ADIVC_DIVADC_MASK, 0x4)

#define RZG2L_ADFIL			0x2c

#define RZG2L_ADCR(n)			(0x30 + ((n) * 0x4))
#define RZG2L_ADCR_AD_MASK		GENMASK(11, 0)

#define RZG2L_ADC_MAX_CHANNELS		9
#define RZG2L_ADC_TIMEOUT		usecs_to_jiffies(1 * 4)

/**
 * struct rzg2l_adc_hw_params - ADC hardware specific parameters
 * @default_adsmp: default ADC sampling period (see ADM3 register); index 0 is
 * used for voltage channels, index 1 is used for temperature channel
 * @adsmp_mask: ADC sampling period mask (see ADM3 register)
 * @adint_inten_mask: conversion end interrupt mask (see ADINT register)
 * @default_adcmp: default ADC cmp (see ADM3 register)
 * @num_channels: number of supported channels
 * @adivc: specifies if ADVIC register is available
 */
struct rzg2l_adc_hw_params {
	u16 default_adsmp[2];
	u16 adsmp_mask;
	u16 adint_inten_mask;
	u8 default_adcmp;
	u8 num_channels;
	bool adivc;
};

struct rzg2l_adc_data {
	const struct iio_chan_spec *channels;
	u8 num_channels;
};

struct rzg2l_adc {
	void __iomem *base;
	struct reset_control *presetn;
	struct reset_control *adrstn;
	const struct rzg2l_adc_data *data;
	const struct rzg2l_adc_hw_params *hw_params;
	struct completion completion;
	struct mutex lock;
	u16 last_val[RZG2L_ADC_MAX_CHANNELS];
	bool was_rpm_active;
};

/**
 * struct rzg2l_adc_channel - ADC channel descriptor
 * @name: ADC channel name
 * @type: ADC channel type
 */
struct rzg2l_adc_channel {
	const char * const name;
	enum iio_chan_type type;
};

static const struct rzg2l_adc_channel rzg2l_adc_channels[] = {
	{ "adc0", IIO_VOLTAGE },
	{ "adc1", IIO_VOLTAGE },
	{ "adc2", IIO_VOLTAGE },
	{ "adc3", IIO_VOLTAGE },
	{ "adc4", IIO_VOLTAGE },
	{ "adc5", IIO_VOLTAGE },
	{ "adc6", IIO_VOLTAGE },
	{ "adc7", IIO_VOLTAGE },
	{ "adc8", IIO_TEMP },
};

static unsigned int rzg2l_adc_readl(struct rzg2l_adc *adc, u32 reg)
{
	return readl(adc->base + reg);
}

static void rzg2l_adc_writel(struct rzg2l_adc *adc, unsigned int reg, u32 val)
{
	writel(val, adc->base + reg);
}

static void rzg2l_adc_pwr(struct rzg2l_adc *adc, bool on)
{
	u32 reg;

	reg = rzg2l_adc_readl(adc, RZG2L_ADM(0));
	if (on)
		reg |= RZG2L_ADM0_PWDWNB;
	else
		reg &= ~RZG2L_ADM0_PWDWNB;
	rzg2l_adc_writel(adc, RZG2L_ADM(0), reg);
	udelay(2);
}

static void rzg2l_adc_start_stop(struct rzg2l_adc *adc, bool start)
{
	int ret;
	u32 reg;

	reg = rzg2l_adc_readl(adc, RZG2L_ADM(0));
	if (start)
		reg |= RZG2L_ADM0_ADCE;
	else
		reg &= ~RZG2L_ADM0_ADCE;
	rzg2l_adc_writel(adc, RZG2L_ADM(0), reg);

	if (start)
		return;

	ret = read_poll_timeout(rzg2l_adc_readl, reg, !(reg & (RZG2L_ADM0_ADBSY | RZG2L_ADM0_ADCE)),
				200, 1000, true, adc, RZG2L_ADM(0));
	if (ret)
		pr_err("%s stopping ADC timed out\n", __func__);
}

static void rzg2l_set_trigger(struct rzg2l_adc *adc)
{
	u32 reg;

	/*
	 * Setup ADM1 for SW trigger
	 * EGA[13:12] - Set 00 to indicate hardware trigger is invalid
	 * BS[4] - Enable 1-buffer mode
	 * MS[1] - Enable Select mode
	 * TRG[0] - Enable software trigger mode
	 */
	reg = rzg2l_adc_readl(adc, RZG2L_ADM(1));
	reg &= ~RZG2L_ADM1_EGA_MASK;
	reg &= ~RZG2L_ADM1_BS;
	reg &= ~RZG2L_ADM1_TRG;
	reg |= RZG2L_ADM1_MS;
	rzg2l_adc_writel(adc, RZG2L_ADM(1), reg);
}

static u8 rzg2l_adc_ch_to_adsmp_index(u8 ch)
{
	if (rzg2l_adc_channels[ch].type == IIO_VOLTAGE)
		return 0;

	return 1;
}

static int rzg2l_adc_conversion_setup(struct rzg2l_adc *adc, u8 ch)
{
	const struct rzg2l_adc_hw_params *hw_params = adc->hw_params;
	u8 index = rzg2l_adc_ch_to_adsmp_index(ch);
	u32 reg;

	if (rzg2l_adc_readl(adc, RZG2L_ADM(0)) & RZG2L_ADM0_ADBSY)
		return -EBUSY;

	rzg2l_set_trigger(adc);

	/* Select analog input channel subjected to conversion. */
	reg = rzg2l_adc_readl(adc, RZG2L_ADM(2));
	reg &= ~GENMASK(hw_params->num_channels - 1, 0);
	reg |= BIT(ch);
	rzg2l_adc_writel(adc, RZG2L_ADM(2), reg);

	reg = rzg2l_adc_readl(adc, RZG2L_ADM(3));
	reg &= ~hw_params->adsmp_mask;
	reg |= hw_params->default_adsmp[index];
	rzg2l_adc_writel(adc, RZG2L_ADM(3), reg);

	/*
	 * Setup ADINT
	 * INTS[31] - Select pulse signal
	 * CSEEN[16] - Enable channel select error interrupt
	 * INTEN[7:0] - Select channel interrupt
	 */
	reg = rzg2l_adc_readl(adc, RZG2L_ADINT);
	reg &= ~RZG2L_ADINT_INTS;
	reg &= ~hw_params->adint_inten_mask;
	reg |= (RZG2L_ADINT_CSEEN | BIT(ch));
	rzg2l_adc_writel(adc, RZG2L_ADINT, reg);

	return 0;
}

static int rzg2l_adc_conversion(struct iio_dev *indio_dev, struct rzg2l_adc *adc, u8 ch)
{
	const struct rzg2l_adc_hw_params *hw_params = adc->hw_params;
	struct device *dev = indio_dev->dev.parent;
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return ret;

	ret = rzg2l_adc_conversion_setup(adc, ch);
	if (ret)
		goto rpm_put;

	reinit_completion(&adc->completion);

	rzg2l_adc_start_stop(adc, true);

	if (!wait_for_completion_timeout(&adc->completion, RZG2L_ADC_TIMEOUT)) {
		rzg2l_adc_writel(adc, RZG2L_ADINT,
				 rzg2l_adc_readl(adc, RZG2L_ADINT) & ~hw_params->adint_inten_mask);
		ret = -ETIMEDOUT;
	}

	rzg2l_adc_start_stop(adc, false);

rpm_put:
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
	return ret;
}

static int rzg2l_adc_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val, int *val2, long mask)
{
	struct rzg2l_adc *adc = iio_priv(indio_dev);
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW: {
		if (chan->type != IIO_VOLTAGE && chan->type != IIO_TEMP)
			return -EINVAL;

		guard(mutex)(&adc->lock);

		ret = rzg2l_adc_conversion(indio_dev, adc, chan->channel);
		if (ret)
			return ret;

		*val = adc->last_val[chan->channel];

		return IIO_VAL_INT;
	}

	default:
		return -EINVAL;
	}
}

static int rzg2l_adc_read_label(struct iio_dev *iio_dev,
				const struct iio_chan_spec *chan,
				char *label)
{
	return sysfs_emit(label, "%s\n", rzg2l_adc_channels[chan->channel].name);
}

static const struct iio_info rzg2l_adc_iio_info = {
	.read_raw = rzg2l_adc_read_raw,
	.read_label = rzg2l_adc_read_label,
};

static irqreturn_t rzg2l_adc_isr(int irq, void *dev_id)
{
	struct rzg2l_adc *adc = dev_id;
	const struct rzg2l_adc_hw_params *hw_params = adc->hw_params;
	unsigned long intst;
	u32 reg;
	int ch;

	reg = rzg2l_adc_readl(adc, RZG2L_ADSTS);

	/* A/D conversion channel select error interrupt */
	if (reg & RZG2L_ADSTS_CSEST) {
		rzg2l_adc_writel(adc, RZG2L_ADSTS, reg);
		return IRQ_HANDLED;
	}

	intst = reg & GENMASK(hw_params->num_channels - 1, 0);
	if (!intst)
		return IRQ_NONE;

	for_each_set_bit(ch, &intst, hw_params->num_channels)
		adc->last_val[ch] = rzg2l_adc_readl(adc, RZG2L_ADCR(ch)) & RZG2L_ADCR_AD_MASK;

	/* clear the channel interrupt */
	rzg2l_adc_writel(adc, RZG2L_ADSTS, reg);

	complete(&adc->completion);

	return IRQ_HANDLED;
}

static int rzg2l_adc_parse_properties(struct platform_device *pdev, struct rzg2l_adc *adc)
{
	const struct rzg2l_adc_hw_params *hw_params = adc->hw_params;
	struct iio_chan_spec *chan_array;
	struct rzg2l_adc_data *data;
	unsigned int channel;
	int num_channels;
	int ret;
	u8 i;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	num_channels = device_get_child_node_count(&pdev->dev);
	if (!num_channels)
		return dev_err_probe(&pdev->dev, -ENODEV, "no channel children\n");

	if (num_channels > hw_params->num_channels)
		return dev_err_probe(&pdev->dev, -EINVAL,
				     "num of channel children out of range\n");

	chan_array = devm_kcalloc(&pdev->dev, num_channels, sizeof(*chan_array),
				  GFP_KERNEL);
	if (!chan_array)
		return -ENOMEM;

	i = 0;
	device_for_each_child_node_scoped(&pdev->dev, fwnode) {
		ret = fwnode_property_read_u32(fwnode, "reg", &channel);
		if (ret)
			return ret;

		if (channel >= hw_params->num_channels)
			return -EINVAL;

		chan_array[i].type = rzg2l_adc_channels[channel].type;
		chan_array[i].indexed = 1;
		chan_array[i].channel = channel;
		chan_array[i].info_mask_separate = BIT(IIO_CHAN_INFO_RAW);
		chan_array[i].datasheet_name = rzg2l_adc_channels[channel].name;
		i++;
	}

	data->num_channels = num_channels;
	data->channels = chan_array;
	adc->data = data;

	return 0;
}

static int rzg2l_adc_hw_init(struct device *dev, struct rzg2l_adc *adc)
{
	const struct rzg2l_adc_hw_params *hw_params = adc->hw_params;
	u32 reg;
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret)
		return ret;

	/* SW reset */
	reg = rzg2l_adc_readl(adc, RZG2L_ADM(0));
	reg |= RZG2L_ADM0_SRESB;
	rzg2l_adc_writel(adc, RZG2L_ADM(0), reg);

	ret = read_poll_timeout(rzg2l_adc_readl, reg, reg & RZG2L_ADM0_SRESB,
				200, 1000, false, adc, RZG2L_ADM(0));
	if (ret)
		goto exit_hw_init;

	if (hw_params->adivc) {
		/* Only division by 4 can be set */
		reg = rzg2l_adc_readl(adc, RZG2L_ADIVC);
		reg &= ~RZG2L_ADIVC_DIVADC_MASK;
		reg |= RZG2L_ADIVC_DIVADC_4;
		rzg2l_adc_writel(adc, RZG2L_ADIVC, reg);
	}

	/*
	 * Setup AMD3
	 * ADIL[31:24] - Should be always set to 0
	 * ADCMP[23:16] - Should be always set to 0xe
	 * ADSMP[15:0] - Set default (0x578) sampling period
	 */
	reg = rzg2l_adc_readl(adc, RZG2L_ADM(3));
	reg &= ~RZG2L_ADM3_ADIL_MASK;
	reg &= ~RZG2L_ADM3_ADCMP_MASK;
	reg &= ~hw_params->adsmp_mask;
	reg |= FIELD_PREP(RZG2L_ADM3_ADCMP_MASK, hw_params->default_adcmp) |
	       hw_params->default_adsmp[0];

	rzg2l_adc_writel(adc, RZG2L_ADM(3), reg);

exit_hw_init:
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
	return ret;
}

static int rzg2l_adc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct iio_dev *indio_dev;
	struct rzg2l_adc *adc;
	int ret;
	int irq;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*adc));
	if (!indio_dev)
		return -ENOMEM;

	adc = iio_priv(indio_dev);

	adc->hw_params = device_get_match_data(dev);
	if (!adc->hw_params || adc->hw_params->num_channels > RZG2L_ADC_MAX_CHANNELS)
		return -EINVAL;

	ret = rzg2l_adc_parse_properties(pdev, adc);
	if (ret)
		return ret;

	mutex_init(&adc->lock);

	adc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(adc->base))
		return PTR_ERR(adc->base);

	adc->adrstn = devm_reset_control_get_exclusive_deasserted(dev, "adrst-n");
	if (IS_ERR(adc->adrstn))
		return dev_err_probe(dev, PTR_ERR(adc->adrstn),
				     "failed to get/deassert adrst-n\n");

	adc->presetn = devm_reset_control_get_exclusive_deasserted(dev, "presetn");
	if (IS_ERR(adc->presetn))
		return dev_err_probe(dev, PTR_ERR(adc->presetn),
				     "failed to get/deassert presetn\n");

	pm_runtime_set_autosuspend_delay(dev, 300);
	pm_runtime_use_autosuspend(dev);
	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, indio_dev);

	ret = rzg2l_adc_hw_init(dev, adc);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to initialize ADC HW\n");

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(dev, irq, rzg2l_adc_isr,
			       0, dev_name(dev), adc);
	if (ret < 0)
		return ret;

	init_completion(&adc->completion);

	indio_dev->name = DRIVER_NAME;
	indio_dev->info = &rzg2l_adc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = adc->data->channels;
	indio_dev->num_channels = adc->data->num_channels;

	return devm_iio_device_register(dev, indio_dev);
}

static const struct rzg2l_adc_hw_params rzg2l_hw_params = {
	.num_channels = 8,
	.default_adcmp = 0xe,
	.default_adsmp = { 0x578 },
	.adsmp_mask = GENMASK(15, 0),
	.adint_inten_mask = GENMASK(7, 0),
	.adivc = true
};

static const struct rzg2l_adc_hw_params rzg3s_hw_params = {
	.num_channels = 9,
	.default_adcmp = 0x1d,
	.default_adsmp = { 0x7f, 0xff },
	.adsmp_mask = GENMASK(7, 0),
	.adint_inten_mask = GENMASK(11, 0),
};

static const struct of_device_id rzg2l_adc_match[] = {
	{ .compatible = "renesas,r9a08g045-adc", .data = &rzg3s_hw_params },
	{ .compatible = "renesas,rzg2l-adc", .data = &rzg2l_hw_params },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rzg2l_adc_match);

static int rzg2l_adc_pm_runtime_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct rzg2l_adc *adc = iio_priv(indio_dev);

	rzg2l_adc_pwr(adc, false);

	return 0;
}

static int rzg2l_adc_pm_runtime_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct rzg2l_adc *adc = iio_priv(indio_dev);

	rzg2l_adc_pwr(adc, true);

	return 0;
}

static int rzg2l_adc_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct rzg2l_adc *adc = iio_priv(indio_dev);
	struct reset_control_bulk_data resets[] = {
		{ .rstc = adc->presetn },
		{ .rstc = adc->adrstn },
	};
	int ret;

	if (pm_runtime_suspended(dev)) {
		adc->was_rpm_active = false;
	} else {
		ret = pm_runtime_force_suspend(dev);
		if (ret)
			return ret;
		adc->was_rpm_active = true;
	}

	ret = reset_control_bulk_assert(ARRAY_SIZE(resets), resets);
	if (ret)
		goto rpm_restore;

	return 0;

rpm_restore:
	if (adc->was_rpm_active)
		pm_runtime_force_resume(dev);

	return ret;
}

static int rzg2l_adc_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct rzg2l_adc *adc = iio_priv(indio_dev);
	struct reset_control_bulk_data resets[] = {
		{ .rstc = adc->adrstn },
		{ .rstc = adc->presetn },
	};
	int ret;

	ret = reset_control_bulk_deassert(ARRAY_SIZE(resets), resets);
	if (ret)
		return ret;

	if (adc->was_rpm_active) {
		ret = pm_runtime_force_resume(dev);
		if (ret)
			goto resets_restore;
	}

	ret = rzg2l_adc_hw_init(dev, adc);
	if (ret)
		goto rpm_restore;

	return 0;

rpm_restore:
	if (adc->was_rpm_active) {
		pm_runtime_mark_last_busy(dev);
		pm_runtime_put_autosuspend(dev);
	}
resets_restore:
	reset_control_bulk_assert(ARRAY_SIZE(resets), resets);
	return ret;
}

static const struct dev_pm_ops rzg2l_adc_pm_ops = {
	RUNTIME_PM_OPS(rzg2l_adc_pm_runtime_suspend, rzg2l_adc_pm_runtime_resume, NULL)
	SYSTEM_SLEEP_PM_OPS(rzg2l_adc_suspend, rzg2l_adc_resume)
};

static struct platform_driver rzg2l_adc_driver = {
	.probe		= rzg2l_adc_probe,
	.driver		= {
		.name		= DRIVER_NAME,
		.of_match_table = rzg2l_adc_match,
		.pm		= pm_ptr(&rzg2l_adc_pm_ops),
	},
};

module_platform_driver(rzg2l_adc_driver);

MODULE_AUTHOR("Lad Prabhakar <prabhakar.mahadev-lad.rj@bp.renesas.com>");
MODULE_DESCRIPTION("Renesas RZ/G2L ADC driver");
MODULE_LICENSE("GPL v2");
