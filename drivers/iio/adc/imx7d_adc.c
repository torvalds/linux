/*
 * Freescale i.MX7D ADC driver
 *
 * Copyright (C) 2015 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#include <linux/iio/iio.h>
#include <linux/iio/driver.h>
#include <linux/iio/sysfs.h>

/* ADC register */
#define IMX7D_REG_ADC_CH_A_CFG1			0x00
#define IMX7D_REG_ADC_CH_A_CFG2			0x10
#define IMX7D_REG_ADC_CH_B_CFG1			0x20
#define IMX7D_REG_ADC_CH_B_CFG2			0x30
#define IMX7D_REG_ADC_CH_C_CFG1			0x40
#define IMX7D_REG_ADC_CH_C_CFG2			0x50
#define IMX7D_REG_ADC_CH_D_CFG1			0x60
#define IMX7D_REG_ADC_CH_D_CFG2			0x70
#define IMX7D_REG_ADC_CH_SW_CFG			0x80
#define IMX7D_REG_ADC_TIMER_UNIT		0x90
#define IMX7D_REG_ADC_DMA_FIFO			0xa0
#define IMX7D_REG_ADC_FIFO_STATUS		0xb0
#define IMX7D_REG_ADC_INT_SIG_EN		0xc0
#define IMX7D_REG_ADC_INT_EN			0xd0
#define IMX7D_REG_ADC_INT_STATUS		0xe0
#define IMX7D_REG_ADC_CHA_B_CNV_RSLT		0xf0
#define IMX7D_REG_ADC_CHC_D_CNV_RSLT		0x100
#define IMX7D_REG_ADC_CH_SW_CNV_RSLT		0x110
#define IMX7D_REG_ADC_DMA_FIFO_DAT		0x120
#define IMX7D_REG_ADC_ADC_CFG			0x130

#define IMX7D_REG_ADC_CHANNEL_CFG2_BASE		0x10
#define IMX7D_EACH_CHANNEL_REG_OFFSET		0x20

#define IMX7D_REG_ADC_CH_CFG1_CHANNEL_EN			(0x1 << 31)
#define IMX7D_REG_ADC_CH_CFG1_CHANNEL_SINGLE			BIT(30)
#define IMX7D_REG_ADC_CH_CFG1_CHANNEL_AVG_EN			BIT(29)
#define IMX7D_REG_ADC_CH_CFG1_CHANNEL_SEL(x)			((x) << 24)

#define IMX7D_REG_ADC_CH_CFG2_AVG_NUM_4				(0x0 << 12)
#define IMX7D_REG_ADC_CH_CFG2_AVG_NUM_8				(0x1 << 12)
#define IMX7D_REG_ADC_CH_CFG2_AVG_NUM_16			(0x2 << 12)
#define IMX7D_REG_ADC_CH_CFG2_AVG_NUM_32			(0x3 << 12)

#define IMX7D_REG_ADC_TIMER_UNIT_PRE_DIV_4			(0x0 << 29)
#define IMX7D_REG_ADC_TIMER_UNIT_PRE_DIV_8			(0x1 << 29)
#define IMX7D_REG_ADC_TIMER_UNIT_PRE_DIV_16			(0x2 << 29)
#define IMX7D_REG_ADC_TIMER_UNIT_PRE_DIV_32			(0x3 << 29)
#define IMX7D_REG_ADC_TIMER_UNIT_PRE_DIV_64			(0x4 << 29)
#define IMX7D_REG_ADC_TIMER_UNIT_PRE_DIV_128			(0x5 << 29)

#define IMX7D_REG_ADC_ADC_CFG_ADC_CLK_DOWN			BIT(31)
#define IMX7D_REG_ADC_ADC_CFG_ADC_POWER_DOWN			BIT(1)
#define IMX7D_REG_ADC_ADC_CFG_ADC_EN				BIT(0)

#define IMX7D_REG_ADC_INT_CHA_COV_INT_EN			BIT(8)
#define IMX7D_REG_ADC_INT_CHB_COV_INT_EN			BIT(9)
#define IMX7D_REG_ADC_INT_CHC_COV_INT_EN			BIT(10)
#define IMX7D_REG_ADC_INT_CHD_COV_INT_EN			BIT(11)
#define IMX7D_REG_ADC_INT_CHANNEL_INT_EN \
	(IMX7D_REG_ADC_INT_CHA_COV_INT_EN | \
	 IMX7D_REG_ADC_INT_CHB_COV_INT_EN | \
	 IMX7D_REG_ADC_INT_CHC_COV_INT_EN | \
	 IMX7D_REG_ADC_INT_CHD_COV_INT_EN)
#define IMX7D_REG_ADC_INT_STATUS_CHANNEL_INT_STATUS		0xf00
#define IMX7D_REG_ADC_INT_STATUS_CHANNEL_CONV_TIME_OUT		0xf0000

#define IMX7D_ADC_TIMEOUT		msecs_to_jiffies(100)

enum imx7d_adc_clk_pre_div {
	IMX7D_ADC_ANALOG_CLK_PRE_DIV_4,
	IMX7D_ADC_ANALOG_CLK_PRE_DIV_8,
	IMX7D_ADC_ANALOG_CLK_PRE_DIV_16,
	IMX7D_ADC_ANALOG_CLK_PRE_DIV_32,
	IMX7D_ADC_ANALOG_CLK_PRE_DIV_64,
	IMX7D_ADC_ANALOG_CLK_PRE_DIV_128,
};

enum imx7d_adc_average_num {
	IMX7D_ADC_AVERAGE_NUM_4,
	IMX7D_ADC_AVERAGE_NUM_8,
	IMX7D_ADC_AVERAGE_NUM_16,
	IMX7D_ADC_AVERAGE_NUM_32,
};

struct imx7d_adc_feature {
	enum imx7d_adc_clk_pre_div clk_pre_div;
	enum imx7d_adc_average_num avg_num;

	u32 core_time_unit;	/* impact the sample rate */

	bool average_en;
};

struct imx7d_adc {
	struct device *dev;
	void __iomem *regs;
	struct clk *clk;

	u32 vref_uv;
	u32 value;
	u32 channel;
	u32 pre_div_num;

	struct regulator *vref;
	struct imx7d_adc_feature adc_feature;

	struct completion completion;
};

struct imx7d_adc_analogue_core_clk {
	u32 pre_div;
	u32 reg_config;
};

#define IMX7D_ADC_ANALOGUE_CLK_CONFIG(_pre_div, _reg_conf) {	\
	.pre_div = (_pre_div),					\
	.reg_config = (_reg_conf),				\
}

static const struct imx7d_adc_analogue_core_clk imx7d_adc_analogue_clk[] = {
	IMX7D_ADC_ANALOGUE_CLK_CONFIG(4, IMX7D_REG_ADC_TIMER_UNIT_PRE_DIV_4),
	IMX7D_ADC_ANALOGUE_CLK_CONFIG(8, IMX7D_REG_ADC_TIMER_UNIT_PRE_DIV_8),
	IMX7D_ADC_ANALOGUE_CLK_CONFIG(16, IMX7D_REG_ADC_TIMER_UNIT_PRE_DIV_16),
	IMX7D_ADC_ANALOGUE_CLK_CONFIG(32, IMX7D_REG_ADC_TIMER_UNIT_PRE_DIV_32),
	IMX7D_ADC_ANALOGUE_CLK_CONFIG(64, IMX7D_REG_ADC_TIMER_UNIT_PRE_DIV_64),
	IMX7D_ADC_ANALOGUE_CLK_CONFIG(128, IMX7D_REG_ADC_TIMER_UNIT_PRE_DIV_128),
};

#define IMX7D_ADC_CHAN(_idx) {					\
	.type = IIO_VOLTAGE,					\
	.indexed = 1,						\
	.channel = (_idx),					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |	\
				BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
}

static const struct iio_chan_spec imx7d_adc_iio_channels[] = {
	IMX7D_ADC_CHAN(0),
	IMX7D_ADC_CHAN(1),
	IMX7D_ADC_CHAN(2),
	IMX7D_ADC_CHAN(3),
	IMX7D_ADC_CHAN(4),
	IMX7D_ADC_CHAN(5),
	IMX7D_ADC_CHAN(6),
	IMX7D_ADC_CHAN(7),
	IMX7D_ADC_CHAN(8),
	IMX7D_ADC_CHAN(9),
	IMX7D_ADC_CHAN(10),
	IMX7D_ADC_CHAN(11),
	IMX7D_ADC_CHAN(12),
	IMX7D_ADC_CHAN(13),
	IMX7D_ADC_CHAN(14),
	IMX7D_ADC_CHAN(15),
};

static const u32 imx7d_adc_average_num[] = {
	IMX7D_REG_ADC_CH_CFG2_AVG_NUM_4,
	IMX7D_REG_ADC_CH_CFG2_AVG_NUM_8,
	IMX7D_REG_ADC_CH_CFG2_AVG_NUM_16,
	IMX7D_REG_ADC_CH_CFG2_AVG_NUM_32,
};

static void imx7d_adc_feature_config(struct imx7d_adc *info)
{
	info->adc_feature.clk_pre_div = IMX7D_ADC_ANALOG_CLK_PRE_DIV_4;
	info->adc_feature.avg_num = IMX7D_ADC_AVERAGE_NUM_32;
	info->adc_feature.core_time_unit = 1;
	info->adc_feature.average_en = true;
}

static void imx7d_adc_sample_rate_set(struct imx7d_adc *info)
{
	struct imx7d_adc_feature *adc_feature = &info->adc_feature;
	struct imx7d_adc_analogue_core_clk adc_analogure_clk;
	u32 i;
	u32 tmp_cfg1;
	u32 sample_rate = 0;

	/*
	 * Before sample set, disable channel A,B,C,D. Here we
	 * clear the bit 31 of register REG_ADC_CH_A\B\C\D_CFG1.
	 */
	for (i = 0; i < 4; i++) {
		tmp_cfg1 =
			readl(info->regs + i * IMX7D_EACH_CHANNEL_REG_OFFSET);
		tmp_cfg1 &= ~IMX7D_REG_ADC_CH_CFG1_CHANNEL_EN;
		writel(tmp_cfg1,
		       info->regs + i * IMX7D_EACH_CHANNEL_REG_OFFSET);
	}

	adc_analogure_clk = imx7d_adc_analogue_clk[adc_feature->clk_pre_div];
	sample_rate |= adc_analogure_clk.reg_config;
	info->pre_div_num = adc_analogure_clk.pre_div;

	sample_rate |= adc_feature->core_time_unit;
	writel(sample_rate, info->regs + IMX7D_REG_ADC_TIMER_UNIT);
}

static void imx7d_adc_hw_init(struct imx7d_adc *info)
{
	u32 cfg;

	/* power up and enable adc analogue core */
	cfg = readl(info->regs + IMX7D_REG_ADC_ADC_CFG);
	cfg &= ~(IMX7D_REG_ADC_ADC_CFG_ADC_CLK_DOWN |
		 IMX7D_REG_ADC_ADC_CFG_ADC_POWER_DOWN);
	cfg |= IMX7D_REG_ADC_ADC_CFG_ADC_EN;
	writel(cfg, info->regs + IMX7D_REG_ADC_ADC_CFG);

	/* enable channel A,B,C,D interrupt */
	writel(IMX7D_REG_ADC_INT_CHANNEL_INT_EN,
	       info->regs + IMX7D_REG_ADC_INT_SIG_EN);
	writel(IMX7D_REG_ADC_INT_CHANNEL_INT_EN,
	       info->regs + IMX7D_REG_ADC_INT_EN);

	imx7d_adc_sample_rate_set(info);
}

static void imx7d_adc_channel_set(struct imx7d_adc *info)
{
	u32 cfg1 = 0;
	u32 cfg2;
	u32 channel;

	channel = info->channel;

	/* the channel choose single conversion, and enable average mode */
	cfg1 |= (IMX7D_REG_ADC_CH_CFG1_CHANNEL_EN |
		 IMX7D_REG_ADC_CH_CFG1_CHANNEL_SINGLE);
	if (info->adc_feature.average_en)
		cfg1 |= IMX7D_REG_ADC_CH_CFG1_CHANNEL_AVG_EN;

	/*
	 * physical channel 0 chose logical channel A
	 * physical channel 1 chose logical channel B
	 * physical channel 2 chose logical channel C
	 * physical channel 3 chose logical channel D
	 */
	cfg1 |= IMX7D_REG_ADC_CH_CFG1_CHANNEL_SEL(channel);

	/*
	 * read register REG_ADC_CH_A\B\C\D_CFG2, according to the
	 * channel chosen
	 */
	cfg2 = readl(info->regs + IMX7D_EACH_CHANNEL_REG_OFFSET * channel +
		     IMX7D_REG_ADC_CHANNEL_CFG2_BASE);

	cfg2 |= imx7d_adc_average_num[info->adc_feature.avg_num];

	/*
	 * write the register REG_ADC_CH_A\B\C\D_CFG2, according to
	 * the channel chosen
	 */
	writel(cfg2, info->regs + IMX7D_EACH_CHANNEL_REG_OFFSET * channel +
	       IMX7D_REG_ADC_CHANNEL_CFG2_BASE);
	writel(cfg1, info->regs + IMX7D_EACH_CHANNEL_REG_OFFSET * channel);
}

static u32 imx7d_adc_get_sample_rate(struct imx7d_adc *info)
{
	/* input clock is always 24MHz */
	u32 input_clk = 24000000;
	u32 analogue_core_clk;
	u32 core_time_unit = info->adc_feature.core_time_unit;
	u32 tmp;

	analogue_core_clk = input_clk / info->pre_div_num;
	tmp = (core_time_unit + 1) * 6;

	return analogue_core_clk / tmp;
}

static int imx7d_adc_read_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan,
			int *val,
			int *val2,
			long mask)
{
	struct imx7d_adc *info = iio_priv(indio_dev);

	u32 channel;
	long ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&indio_dev->mlock);
		reinit_completion(&info->completion);

		channel = chan->channel & 0x03;
		info->channel = channel;
		imx7d_adc_channel_set(info);

		ret = wait_for_completion_interruptible_timeout
				(&info->completion, IMX7D_ADC_TIMEOUT);
		if (ret == 0) {
			mutex_unlock(&indio_dev->mlock);
			return -ETIMEDOUT;
		}
		if (ret < 0) {
			mutex_unlock(&indio_dev->mlock);
			return ret;
		}

		*val = info->value;
		mutex_unlock(&indio_dev->mlock);
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		info->vref_uv = regulator_get_voltage(info->vref);
		*val = info->vref_uv / 1000;
		*val2 = 12;
		return IIO_VAL_FRACTIONAL_LOG2;

	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = imx7d_adc_get_sample_rate(info);
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static int imx7d_adc_read_data(struct imx7d_adc *info)
{
	u32 channel;
	u32 value;

	channel = info->channel & 0x03;

	/*
	 * channel A and B conversion result share one register,
	 * bit[27~16] is the channel B conversion result,
	 * bit[11~0] is the channel A conversion result.
	 * channel C and D is the same.
	 */
	if (channel < 2)
		value = readl(info->regs + IMX7D_REG_ADC_CHA_B_CNV_RSLT);
	else
		value = readl(info->regs + IMX7D_REG_ADC_CHC_D_CNV_RSLT);
	if (channel & 0x1)	/* channel B or D */
		value = (value >> 16) & 0xFFF;
	else			/* channel A or C */
		value &= 0xFFF;

	return value;
}

static irqreturn_t imx7d_adc_isr(int irq, void *dev_id)
{
	struct imx7d_adc *info = dev_id;
	int status;

	status = readl(info->regs + IMX7D_REG_ADC_INT_STATUS);
	if (status & IMX7D_REG_ADC_INT_STATUS_CHANNEL_INT_STATUS) {
		info->value = imx7d_adc_read_data(info);
		complete(&info->completion);

		/*
		 * The register IMX7D_REG_ADC_INT_STATUS can't clear
		 * itself after read operation, need software to write
		 * 0 to the related bit. Here we clear the channel A/B/C/D
		 * conversion finished flag.
		 */
		status &= ~IMX7D_REG_ADC_INT_STATUS_CHANNEL_INT_STATUS;
		writel(status, info->regs + IMX7D_REG_ADC_INT_STATUS);
	}

	/*
	 * If the channel A/B/C/D conversion timeout, report it and clear these
	 * timeout flags.
	 */
	if (status & IMX7D_REG_ADC_INT_STATUS_CHANNEL_CONV_TIME_OUT) {
		dev_err(info->dev,
			"ADC got conversion time out interrupt: 0x%08x\n",
			status);
		status &= ~IMX7D_REG_ADC_INT_STATUS_CHANNEL_CONV_TIME_OUT;
		writel(status, info->regs + IMX7D_REG_ADC_INT_STATUS);
	}

	return IRQ_HANDLED;
}

static int imx7d_adc_reg_access(struct iio_dev *indio_dev,
			unsigned reg, unsigned writeval,
			unsigned *readval)
{
	struct imx7d_adc *info = iio_priv(indio_dev);

	if (!readval || reg % 4 || reg > IMX7D_REG_ADC_ADC_CFG)
		return -EINVAL;

	*readval = readl(info->regs + reg);

	return 0;
}

static const struct iio_info imx7d_adc_iio_info = {
	.read_raw = &imx7d_adc_read_raw,
	.debugfs_reg_access = &imx7d_adc_reg_access,
};

static const struct of_device_id imx7d_adc_match[] = {
	{ .compatible = "fsl,imx7d-adc", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx7d_adc_match);

static void imx7d_adc_power_down(struct imx7d_adc *info)
{
	u32 adc_cfg;

	adc_cfg = readl(info->regs + IMX7D_REG_ADC_ADC_CFG);
	adc_cfg |= IMX7D_REG_ADC_ADC_CFG_ADC_CLK_DOWN |
		   IMX7D_REG_ADC_ADC_CFG_ADC_POWER_DOWN;
	adc_cfg &= ~IMX7D_REG_ADC_ADC_CFG_ADC_EN;
	writel(adc_cfg, info->regs + IMX7D_REG_ADC_ADC_CFG);
}

static int imx7d_adc_enable(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct imx7d_adc *info = iio_priv(indio_dev);
	int ret;

	ret = regulator_enable(info->vref);
	if (ret) {
		dev_err(info->dev,
			"Can't enable adc reference top voltage, err = %d\n",
			ret);
		return ret;
	}

	ret = clk_prepare_enable(info->clk);
	if (ret) {
		dev_err(info->dev,
			"Could not prepare or enable clock.\n");
		regulator_disable(info->vref);
		return ret;
	}

	imx7d_adc_hw_init(info);

	return 0;
}

static int imx7d_adc_disable(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct imx7d_adc *info = iio_priv(indio_dev);

	imx7d_adc_power_down(info);

	clk_disable_unprepare(info->clk);
	regulator_disable(info->vref);

	return 0;
}

static void __imx7d_adc_disable(void *data)
{
	imx7d_adc_disable(data);
}

static int imx7d_adc_probe(struct platform_device *pdev)
{
	struct imx7d_adc *info;
	struct iio_dev *indio_dev;
	struct device *dev = &pdev->dev;
	int irq;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*info));
	if (!indio_dev) {
		dev_err(&pdev->dev, "Failed allocating iio device\n");
		return -ENOMEM;
	}

	info = iio_priv(indio_dev);
	info->dev = dev;

	info->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(info->regs))
		return PTR_ERR(info->regs);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "No irq resource?\n");
		return irq;
	}

	info->clk = devm_clk_get(dev, "adc");
	if (IS_ERR(info->clk)) {
		ret = PTR_ERR(info->clk);
		dev_err(dev, "Failed getting clock, err = %d\n", ret);
		return ret;
	}

	info->vref = devm_regulator_get(dev, "vref");
	if (IS_ERR(info->vref)) {
		ret = PTR_ERR(info->vref);
		dev_err(dev,
			"Failed getting reference voltage, err = %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, indio_dev);

	init_completion(&info->completion);

	indio_dev->name = dev_name(dev);
	indio_dev->dev.parent = dev;
	indio_dev->info = &imx7d_adc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = imx7d_adc_iio_channels;
	indio_dev->num_channels = ARRAY_SIZE(imx7d_adc_iio_channels);

	ret = devm_request_irq(dev, irq,
			       imx7d_adc_isr, 0,
			       dev_name(dev), info);
	if (ret < 0) {
		dev_err(dev, "Failed requesting irq, irq = %d\n", irq);
		return ret;
	}

	imx7d_adc_feature_config(info);

	ret = imx7d_adc_enable(&indio_dev->dev);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, __imx7d_adc_disable,
				       &indio_dev->dev);
	if (ret)
		return ret;

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret) {
		dev_err(&pdev->dev, "Couldn't register the device.\n");
		return ret;
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(imx7d_adc_pm_ops, imx7d_adc_disable, imx7d_adc_enable);

static struct platform_driver imx7d_adc_driver = {
	.probe		= imx7d_adc_probe,
	.driver		= {
		.name	= "imx7d_adc",
		.of_match_table = imx7d_adc_match,
		.pm	= &imx7d_adc_pm_ops,
	},
};

module_platform_driver(imx7d_adc_driver);

MODULE_AUTHOR("Haibo Chen <haibo.chen@freescale.com>");
MODULE_DESCRIPTION("Freescale IMX7D ADC driver");
MODULE_LICENSE("GPL v2");
