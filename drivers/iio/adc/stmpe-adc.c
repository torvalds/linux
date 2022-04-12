// SPDX-License-Identifier: GPL-2.0
/*
 *  STMicroelectronics STMPE811 IIO ADC Driver
 *
 *  4 channel, 10/12-bit ADC
 *
 *  Copyright (C) 2013-2018 Toradex AG <stefan.agner@toradex.com>
 */

#include <linux/completion.h>
#include <linux/err.h>
#include <linux/iio/iio.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/stmpe.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/device.h>

#define STMPE_REG_INT_STA		0x0B
#define STMPE_REG_ADC_INT_EN		0x0E
#define STMPE_REG_ADC_INT_STA		0x0F

#define STMPE_REG_ADC_CTRL1		0x20
#define STMPE_REG_ADC_CTRL2		0x21
#define STMPE_REG_ADC_CAPT		0x22
#define STMPE_REG_ADC_DATA_CH(channel)	(0x30 + 2 * (channel))

#define STMPE_REG_TEMP_CTRL		0x60
#define STMPE_TEMP_CTRL_ENABLE		BIT(0)
#define STMPE_TEMP_CTRL_ACQ		BIT(1)
#define STMPE_TEMP_CTRL_THRES_EN	BIT(3)
#define STMPE_START_ONE_TEMP_CONV	(STMPE_TEMP_CTRL_ENABLE | \
					STMPE_TEMP_CTRL_ACQ | \
					STMPE_TEMP_CTRL_THRES_EN)
#define STMPE_REG_TEMP_DATA		0x61
#define STMPE_REG_TEMP_TH		0x63
#define STMPE_ADC_LAST_NR		7
#define STMPE_TEMP_CHANNEL		(STMPE_ADC_LAST_NR + 1)

#define STMPE_ADC_CH(channel)		((1 << (channel)) & 0xff)

#define STMPE_ADC_TIMEOUT		msecs_to_jiffies(1000)

struct stmpe_adc {
	struct stmpe *stmpe;
	struct clk *clk;
	struct device *dev;
	struct mutex lock;

	/* We are allocating plus one for the temperature channel */
	struct iio_chan_spec stmpe_adc_iio_channels[STMPE_ADC_LAST_NR + 2];

	struct completion completion;

	u8 channel;
	u32 value;
};

static int stmpe_read_voltage(struct stmpe_adc *info,
		struct iio_chan_spec const *chan, int *val)
{
	unsigned long ret;

	mutex_lock(&info->lock);

	reinit_completion(&info->completion);

	info->channel = (u8)chan->channel;

	if (info->channel > STMPE_ADC_LAST_NR) {
		mutex_unlock(&info->lock);
		return -EINVAL;
	}

	stmpe_reg_write(info->stmpe, STMPE_REG_ADC_CAPT,
			STMPE_ADC_CH(info->channel));

	ret = wait_for_completion_timeout(&info->completion, STMPE_ADC_TIMEOUT);

	if (ret == 0) {
		stmpe_reg_write(info->stmpe, STMPE_REG_ADC_INT_STA,
				STMPE_ADC_CH(info->channel));
		mutex_unlock(&info->lock);
		return -ETIMEDOUT;
	}

	*val = info->value;

	mutex_unlock(&info->lock);

	return 0;
}

static int stmpe_read_temp(struct stmpe_adc *info,
		struct iio_chan_spec const *chan, int *val)
{
	unsigned long ret;

	mutex_lock(&info->lock);

	reinit_completion(&info->completion);

	info->channel = (u8)chan->channel;

	if (info->channel != STMPE_TEMP_CHANNEL) {
		mutex_unlock(&info->lock);
		return -EINVAL;
	}

	stmpe_reg_write(info->stmpe, STMPE_REG_TEMP_CTRL,
			STMPE_START_ONE_TEMP_CONV);

	ret = wait_for_completion_timeout(&info->completion, STMPE_ADC_TIMEOUT);

	if (ret == 0) {
		mutex_unlock(&info->lock);
		return -ETIMEDOUT;
	}

	/*
	 * absolute temp = +V3.3 * value /7.51 [K]
	 * scale to [milli °C]
	 */
	*val = ((449960l * info->value) / 1024l) - 273150;

	mutex_unlock(&info->lock);

	return 0;
}

static int stmpe_read_raw(struct iio_dev *indio_dev,
			  struct iio_chan_spec const *chan,
			  int *val,
			  int *val2,
			  long mask)
{
	struct stmpe_adc *info = iio_priv(indio_dev);
	long ret;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
	case IIO_CHAN_INFO_PROCESSED:

		switch (chan->type) {
		case IIO_VOLTAGE:
			ret = stmpe_read_voltage(info, chan, val);
			break;

		case IIO_TEMP:
			ret = stmpe_read_temp(info, chan, val);
			break;
		default:
			return -EINVAL;
		}

		if (ret < 0)
			return ret;

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		*val = 3300;
		*val2 = info->stmpe->mod_12b ? 12 : 10;
		return IIO_VAL_FRACTIONAL_LOG2;

	default:
		break;
	}

	return -EINVAL;
}

static irqreturn_t stmpe_adc_isr(int irq, void *dev_id)
{
	struct stmpe_adc *info = (struct stmpe_adc *)dev_id;
	__be16 data;

	if (info->channel <= STMPE_ADC_LAST_NR) {
		int int_sta;

		int_sta = stmpe_reg_read(info->stmpe, STMPE_REG_ADC_INT_STA);

		/* Is the interrupt relevant */
		if (!(int_sta & STMPE_ADC_CH(info->channel)))
			return IRQ_NONE;

		/* Read value */
		stmpe_block_read(info->stmpe,
			STMPE_REG_ADC_DATA_CH(info->channel), 2, (u8 *) &data);

		stmpe_reg_write(info->stmpe, STMPE_REG_ADC_INT_STA, int_sta);
	} else if (info->channel == STMPE_TEMP_CHANNEL) {
		/* Read value */
		stmpe_block_read(info->stmpe, STMPE_REG_TEMP_DATA, 2,
				(u8 *) &data);
	} else {
		return IRQ_NONE;
	}

	info->value = (u32) be16_to_cpu(data);
	complete(&info->completion);

	return IRQ_HANDLED;
}

static const struct iio_info stmpe_adc_iio_info = {
	.read_raw = &stmpe_read_raw,
};

static void stmpe_adc_voltage_chan(struct iio_chan_spec *ics, int chan)
{
	ics->type = IIO_VOLTAGE;
	ics->info_mask_separate = BIT(IIO_CHAN_INFO_RAW);
	ics->info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE);
	ics->indexed = 1;
	ics->channel = chan;
}

static void stmpe_adc_temp_chan(struct iio_chan_spec *ics, int chan)
{
	ics->type = IIO_TEMP;
	ics->info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED);
	ics->indexed = 1;
	ics->channel = chan;
}

static int stmpe_adc_init_hw(struct stmpe_adc *adc)
{
	int ret;
	struct stmpe *stmpe = adc->stmpe;

	ret = stmpe_enable(stmpe, STMPE_BLOCK_ADC);
	if (ret) {
		dev_err(stmpe->dev, "Could not enable clock for ADC\n");
		return ret;
	}

	ret = stmpe811_adc_common_init(stmpe);
	if (ret) {
		stmpe_disable(stmpe, STMPE_BLOCK_ADC);
		return ret;
	}

	/* use temp irq for each conversion completion */
	stmpe_reg_write(stmpe, STMPE_REG_TEMP_TH, 0);
	stmpe_reg_write(stmpe, STMPE_REG_TEMP_TH + 1, 0);

	return 0;
}

static int stmpe_adc_probe(struct platform_device *pdev)
{
	struct iio_dev *indio_dev;
	struct stmpe_adc *info;
	struct device_node *np;
	u32 norequest_mask = 0;
	unsigned long bits;
	int irq_temp, irq_adc;
	int num_chan = 0;
	int i = 0;
	int ret;

	irq_adc = platform_get_irq_byname(pdev, "STMPE_ADC");
	if (irq_adc < 0)
		return irq_adc;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(struct stmpe_adc));
	if (!indio_dev) {
		dev_err(&pdev->dev, "failed allocating iio device\n");
		return -ENOMEM;
	}

	info = iio_priv(indio_dev);
	mutex_init(&info->lock);

	init_completion(&info->completion);
	ret = devm_request_threaded_irq(&pdev->dev, irq_adc, NULL,
					stmpe_adc_isr, IRQF_ONESHOT,
					"stmpe-adc", info);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed requesting irq, irq = %d\n",
				irq_adc);
		return ret;
	}

	irq_temp = platform_get_irq_byname(pdev, "STMPE_TEMP_SENS");
	if (irq_temp >= 0) {
		ret = devm_request_threaded_irq(&pdev->dev, irq_temp, NULL,
						stmpe_adc_isr, IRQF_ONESHOT,
						"stmpe-adc", info);
		if (ret < 0)
			dev_warn(&pdev->dev, "failed requesting irq for"
				 " temp sensor, irq = %d\n", irq_temp);
	}

	platform_set_drvdata(pdev, indio_dev);

	indio_dev->name		= dev_name(&pdev->dev);
	indio_dev->info		= &stmpe_adc_iio_info;
	indio_dev->modes	= INDIO_DIRECT_MODE;

	info->stmpe = dev_get_drvdata(pdev->dev.parent);

	np = pdev->dev.of_node;

	if (!np)
		dev_err(&pdev->dev, "no device tree node found\n");

	of_property_read_u32(np, "st,norequest-mask", &norequest_mask);

	bits = norequest_mask;
	for_each_clear_bit(i, &bits, (STMPE_ADC_LAST_NR + 1)) {
		stmpe_adc_voltage_chan(&info->stmpe_adc_iio_channels[num_chan], i);
		num_chan++;
	}
	stmpe_adc_temp_chan(&info->stmpe_adc_iio_channels[num_chan], i);
	num_chan++;
	indio_dev->channels = info->stmpe_adc_iio_channels;
	indio_dev->num_channels = num_chan;

	ret = stmpe_adc_init_hw(info);
	if (ret)
		return ret;

	stmpe_reg_write(info->stmpe, STMPE_REG_ADC_INT_EN,
			~(norequest_mask & 0xFF));

	stmpe_reg_write(info->stmpe, STMPE_REG_ADC_INT_STA,
			~(norequest_mask & 0xFF));

	return devm_iio_device_register(&pdev->dev, indio_dev);
}

static int __maybe_unused stmpe_adc_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct stmpe_adc *info = iio_priv(indio_dev);

	stmpe_adc_init_hw(info);

	return 0;
}

static SIMPLE_DEV_PM_OPS(stmpe_adc_pm_ops, NULL, stmpe_adc_resume);

static struct platform_driver stmpe_adc_driver = {
	.probe		= stmpe_adc_probe,
	.driver		= {
		.name	= "stmpe-adc",
		.pm	= &stmpe_adc_pm_ops,
	},
};
module_platform_driver(stmpe_adc_driver);

static const struct of_device_id stmpe_adc_ids[] = {
	{ .compatible = "st,stmpe-adc", },
	{ },
};
MODULE_DEVICE_TABLE(of, stmpe_adc_ids);

MODULE_AUTHOR("Stefan Agner <stefan.agner@toradex.com>");
MODULE_DESCRIPTION("STMPEXXX ADC driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:stmpe-adc");
