// SPDX-License-Identifier: GPL-2.0+
/*
 * NXP i.MX93 ADC driver
 *
 * Copyright 2023 NXP
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/iio/iio.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>

#define IMX93_ADC_DRIVER_NAME	"imx93-adc"

/* Register map definition */
#define IMX93_ADC_MCR		0x00
#define IMX93_ADC_MSR		0x04
#define IMX93_ADC_ISR		0x10
#define IMX93_ADC_IMR		0x20
#define IMX93_ADC_CIMR0		0x24
#define IMX93_ADC_CTR0		0x94
#define IMX93_ADC_NCMR0		0xA4
#define IMX93_ADC_PCDR0		0x100
#define IMX93_ADC_PCDR1		0x104
#define IMX93_ADC_PCDR2		0x108
#define IMX93_ADC_PCDR3		0x10c
#define IMX93_ADC_PCDR4		0x110
#define IMX93_ADC_PCDR5		0x114
#define IMX93_ADC_PCDR6		0x118
#define IMX93_ADC_PCDR7		0x11c
#define IMX93_ADC_CALSTAT	0x39C

/* ADC bit shift */
#define IMX93_ADC_MCR_MODE_MASK			BIT(29)
#define IMX93_ADC_MCR_NSTART_MASK		BIT(24)
#define IMX93_ADC_MCR_CALSTART_MASK		BIT(14)
#define IMX93_ADC_MCR_ADCLKSE_MASK		BIT(8)
#define IMX93_ADC_MCR_PWDN_MASK			BIT(0)
#define IMX93_ADC_MSR_CALFAIL_MASK		BIT(30)
#define IMX93_ADC_MSR_CALBUSY_MASK		BIT(29)
#define IMX93_ADC_MSR_ADCSTATUS_MASK		GENMASK(2, 0)
#define IMX93_ADC_ISR_ECH_MASK			BIT(0)
#define IMX93_ADC_ISR_EOC_MASK			BIT(1)
#define IMX93_ADC_ISR_EOC_ECH_MASK		(IMX93_ADC_ISR_EOC_MASK | \
						 IMX93_ADC_ISR_ECH_MASK)
#define IMX93_ADC_IMR_JEOC_MASK			BIT(3)
#define IMX93_ADC_IMR_JECH_MASK			BIT(2)
#define IMX93_ADC_IMR_EOC_MASK			BIT(1)
#define IMX93_ADC_IMR_ECH_MASK			BIT(0)
#define IMX93_ADC_PCDR_CDATA_MASK		GENMASK(11, 0)

/* ADC status */
#define IMX93_ADC_MSR_ADCSTATUS_IDLE			0
#define IMX93_ADC_MSR_ADCSTATUS_POWER_DOWN		1
#define IMX93_ADC_MSR_ADCSTATUS_WAIT_STATE		2
#define IMX93_ADC_MSR_ADCSTATUS_BUSY_IN_CALIBRATION	3
#define IMX93_ADC_MSR_ADCSTATUS_SAMPLE			4
#define IMX93_ADC_MSR_ADCSTATUS_CONVERSION		6

#define IMX93_ADC_TIMEOUT		msecs_to_jiffies(100)

struct imx93_adc {
	struct device *dev;
	void __iomem *regs;
	struct clk *ipg_clk;
	int irq;
	struct regulator *vref;
	/* lock to protect against multiple access to the device */
	struct mutex lock;
	struct completion completion;
};

#define IMX93_ADC_CHAN(_idx) {					\
	.type = IIO_VOLTAGE,					\
	.indexed = 1,						\
	.channel = (_idx),					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |	\
				BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
}

static const struct iio_chan_spec imx93_adc_iio_channels[] = {
	IMX93_ADC_CHAN(0),
	IMX93_ADC_CHAN(1),
	IMX93_ADC_CHAN(2),
	IMX93_ADC_CHAN(3),
};

static void imx93_adc_power_down(struct imx93_adc *adc)
{
	u32 mcr, msr;
	int ret;

	mcr = readl(adc->regs + IMX93_ADC_MCR);
	mcr |= FIELD_PREP(IMX93_ADC_MCR_PWDN_MASK, 1);
	writel(mcr, adc->regs + IMX93_ADC_MCR);

	ret = readl_poll_timeout(adc->regs + IMX93_ADC_MSR, msr,
				 ((msr & IMX93_ADC_MSR_ADCSTATUS_MASK) ==
				  IMX93_ADC_MSR_ADCSTATUS_POWER_DOWN),
				 1, 50);
	if (ret == -ETIMEDOUT)
		dev_warn(adc->dev,
			 "ADC do not in power down mode, current MSR is %x\n",
			 msr);
}

static void imx93_adc_power_up(struct imx93_adc *adc)
{
	u32 mcr;

	/* bring ADC out of power down state, in idle state */
	mcr = readl(adc->regs + IMX93_ADC_MCR);
	mcr &= ~FIELD_PREP(IMX93_ADC_MCR_PWDN_MASK, 1);
	writel(mcr, adc->regs + IMX93_ADC_MCR);
}

static void imx93_adc_config_ad_clk(struct imx93_adc *adc)
{
	u32 mcr;

	/* put adc in power down mode */
	imx93_adc_power_down(adc);

	/* config the AD_CLK equal to bus clock */
	mcr = readl(adc->regs + IMX93_ADC_MCR);
	mcr |= FIELD_PREP(IMX93_ADC_MCR_ADCLKSE_MASK, 1);
	writel(mcr, adc->regs + IMX93_ADC_MCR);

	imx93_adc_power_up(adc);
}

static int imx93_adc_calibration(struct imx93_adc *adc)
{
	u32 mcr, msr;
	int ret;

	/* make sure ADC in power down mode */
	imx93_adc_power_down(adc);

	/* config SAR controller operating clock */
	mcr = readl(adc->regs + IMX93_ADC_MCR);
	mcr &= ~FIELD_PREP(IMX93_ADC_MCR_ADCLKSE_MASK, 1);
	writel(mcr, adc->regs + IMX93_ADC_MCR);

	imx93_adc_power_up(adc);

	/*
	 * TODO: we use the default TSAMP/NRSMPL/AVGEN in MCR,
	 * can add the setting of these bit if need in future.
	 */

	/* run calibration */
	mcr = readl(adc->regs + IMX93_ADC_MCR);
	mcr |= FIELD_PREP(IMX93_ADC_MCR_CALSTART_MASK, 1);
	writel(mcr, adc->regs + IMX93_ADC_MCR);

	/* wait calibration to be finished */
	ret = readl_poll_timeout(adc->regs + IMX93_ADC_MSR, msr,
		!(msr & IMX93_ADC_MSR_CALBUSY_MASK), 1000, 2000000);
	if (ret == -ETIMEDOUT) {
		dev_warn(adc->dev, "ADC do not finish calibration in 2 min!\n");
		imx93_adc_power_down(adc);
		return ret;
	}

	/* check whether calbration is success or not */
	msr = readl(adc->regs + IMX93_ADC_MSR);
	if (msr & IMX93_ADC_MSR_CALFAIL_MASK) {
		dev_warn(adc->dev, "ADC calibration failed!\n");
		imx93_adc_power_down(adc);
		return -EAGAIN;
	}

	return 0;
}

static int imx93_adc_read_channel_conversion(struct imx93_adc *adc,
						int channel_number,
						int *result)
{
	u32 channel;
	u32 imr, mcr, pcda;
	long ret;

	reinit_completion(&adc->completion);

	/* config channel mask register */
	channel = 1 << channel_number;
	writel(channel, adc->regs + IMX93_ADC_NCMR0);

	/* TODO: can config desired sample time in CTRn if need */

	/* config interrupt mask */
	imr = FIELD_PREP(IMX93_ADC_IMR_EOC_MASK, 1);
	writel(imr, adc->regs + IMX93_ADC_IMR);
	writel(channel, adc->regs + IMX93_ADC_CIMR0);

	/* config one-shot mode */
	mcr = readl(adc->regs + IMX93_ADC_MCR);
	mcr &= ~FIELD_PREP(IMX93_ADC_MCR_MODE_MASK, 1);
	writel(mcr, adc->regs + IMX93_ADC_MCR);

	/* start normal conversion */
	mcr = readl(adc->regs + IMX93_ADC_MCR);
	mcr |= FIELD_PREP(IMX93_ADC_MCR_NSTART_MASK, 1);
	writel(mcr, adc->regs + IMX93_ADC_MCR);

	ret = wait_for_completion_interruptible_timeout(&adc->completion,
							IMX93_ADC_TIMEOUT);
	if (ret == 0)
		return -ETIMEDOUT;

	if (ret < 0)
		return ret;

	pcda = readl(adc->regs + IMX93_ADC_PCDR0 + channel_number * 4);

	*result = FIELD_GET(IMX93_ADC_PCDR_CDATA_MASK, pcda);

	return ret;
}

static int imx93_adc_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val, int *val2, long mask)
{
	struct imx93_adc *adc = iio_priv(indio_dev);
	struct device *dev = adc->dev;
	int ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		pm_runtime_get_sync(dev);
		mutex_lock(&adc->lock);
		ret = imx93_adc_read_channel_conversion(adc, chan->channel, val);
		mutex_unlock(&adc->lock);
		pm_runtime_mark_last_busy(dev);
		pm_runtime_put_sync_autosuspend(dev);
		if (ret < 0)
			return ret;

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		ret = regulator_get_voltage(adc->vref);
		if (ret < 0)
			return ret;
		*val = ret / 1000;
		*val2 = 12;
		return IIO_VAL_FRACTIONAL_LOG2;

	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = clk_get_rate(adc->ipg_clk);
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static irqreturn_t imx93_adc_isr(int irq, void *dev_id)
{
	struct imx93_adc *adc = dev_id;
	u32 isr, eoc, unexpected;

	isr = readl(adc->regs + IMX93_ADC_ISR);

	if (FIELD_GET(IMX93_ADC_ISR_EOC_ECH_MASK, isr)) {
		eoc = isr & IMX93_ADC_ISR_EOC_ECH_MASK;
		writel(eoc, adc->regs + IMX93_ADC_ISR);
		complete(&adc->completion);
	}

	unexpected = isr & ~IMX93_ADC_ISR_EOC_ECH_MASK;
	if (unexpected) {
		writel(unexpected, adc->regs + IMX93_ADC_ISR);
		dev_err(adc->dev, "Unexpected interrupt 0x%08x.\n", unexpected);
		return IRQ_NONE;
	}

	return IRQ_HANDLED;
}

static const struct iio_info imx93_adc_iio_info = {
	.read_raw = &imx93_adc_read_raw,
};

static int imx93_adc_probe(struct platform_device *pdev)
{
	struct imx93_adc *adc;
	struct iio_dev *indio_dev;
	struct device *dev = &pdev->dev;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*adc));
	if (!indio_dev)
		return dev_err_probe(dev, -ENOMEM,
				     "Failed allocating iio device\n");

	adc = iio_priv(indio_dev);
	adc->dev = dev;

	mutex_init(&adc->lock);
	adc->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(adc->regs))
		return dev_err_probe(dev, PTR_ERR(adc->regs),
				     "Failed getting ioremap resource\n");

	/* The third irq is for ADC conversion usage */
	adc->irq = platform_get_irq(pdev, 2);
	if (adc->irq < 0)
		return adc->irq;

	adc->ipg_clk = devm_clk_get(dev, "ipg");
	if (IS_ERR(adc->ipg_clk))
		return dev_err_probe(dev, PTR_ERR(adc->ipg_clk),
				     "Failed getting clock.\n");

	adc->vref = devm_regulator_get(dev, "vref");
	if (IS_ERR(adc->vref))
		return dev_err_probe(dev, PTR_ERR(adc->vref),
				     "Failed getting reference voltage.\n");

	ret = regulator_enable(adc->vref);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to enable reference voltage.\n");

	platform_set_drvdata(pdev, indio_dev);

	init_completion(&adc->completion);

	indio_dev->name = "imx93-adc";
	indio_dev->info = &imx93_adc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = imx93_adc_iio_channels;
	indio_dev->num_channels = ARRAY_SIZE(imx93_adc_iio_channels);

	ret = clk_prepare_enable(adc->ipg_clk);
	if (ret) {
		dev_err_probe(dev, ret,
			      "Failed to enable ipg clock.\n");
		goto error_regulator_disable;
	}

	ret = request_irq(adc->irq, imx93_adc_isr, 0, IMX93_ADC_DRIVER_NAME, adc);
	if (ret < 0) {
		dev_err_probe(dev, ret,
			      "Failed requesting irq, irq = %d\n", adc->irq);
		goto error_ipg_clk_disable;
	}

	ret = imx93_adc_calibration(adc);
	if (ret < 0)
		goto error_free_adc_irq;

	imx93_adc_config_ad_clk(adc);

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err_probe(dev, ret,
			      "Failed to register this iio device.\n");
		goto error_adc_power_down;
	}

	pm_runtime_set_active(dev);
	pm_runtime_set_autosuspend_delay(dev, 50);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_enable(dev);

	return 0;

error_adc_power_down:
	imx93_adc_power_down(adc);
error_free_adc_irq:
	free_irq(adc->irq, adc);
error_ipg_clk_disable:
	clk_disable_unprepare(adc->ipg_clk);
error_regulator_disable:
	regulator_disable(adc->vref);

	return ret;
}

static void imx93_adc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct imx93_adc *adc = iio_priv(indio_dev);
	struct device *dev = adc->dev;

	/* adc power down need clock on */
	pm_runtime_get_sync(dev);

	pm_runtime_disable(dev);
	pm_runtime_dont_use_autosuspend(dev);
	pm_runtime_put_noidle(dev);

	iio_device_unregister(indio_dev);
	imx93_adc_power_down(adc);
	free_irq(adc->irq, adc);
	clk_disable_unprepare(adc->ipg_clk);
	regulator_disable(adc->vref);
}

static int imx93_adc_runtime_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct imx93_adc *adc = iio_priv(indio_dev);

	imx93_adc_power_down(adc);
	clk_disable_unprepare(adc->ipg_clk);
	regulator_disable(adc->vref);

	return 0;
}

static int imx93_adc_runtime_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct imx93_adc *adc = iio_priv(indio_dev);
	int ret;

	ret = regulator_enable(adc->vref);
	if (ret) {
		dev_err(dev,
			"Can't enable adc reference top voltage, err = %d\n",
			ret);
		return ret;
	}

	ret = clk_prepare_enable(adc->ipg_clk);
	if (ret) {
		dev_err(dev, "Could not prepare or enable clock.\n");
		goto err_disable_reg;
	}

	imx93_adc_power_up(adc);

	return 0;

err_disable_reg:
	regulator_disable(adc->vref);

	return ret;
}

static DEFINE_RUNTIME_DEV_PM_OPS(imx93_adc_pm_ops,
				 imx93_adc_runtime_suspend,
				 imx93_adc_runtime_resume, NULL);

static const struct of_device_id imx93_adc_match[] = {
	{ .compatible = "nxp,imx93-adc", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx93_adc_match);

static struct platform_driver imx93_adc_driver = {
	.probe		= imx93_adc_probe,
	.remove_new	= imx93_adc_remove,
	.driver		= {
		.name	= IMX93_ADC_DRIVER_NAME,
		.of_match_table = imx93_adc_match,
		.pm	= pm_ptr(&imx93_adc_pm_ops),
	},
};

module_platform_driver(imx93_adc_driver);

MODULE_DESCRIPTION("NXP i.MX93 ADC driver");
MODULE_AUTHOR("Haibo Chen <haibo.chen@nxp.com>");
MODULE_LICENSE("GPL");
