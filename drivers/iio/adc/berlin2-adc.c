/*
 * Marvell Berlin2 ADC driver
 *
 * Copyright (C) 2015 Marvell Technology Group Ltd.
 *
 * Antoine Tenart <antoine.tenart@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/iio/iio.h>
#include <linux/iio/driver.h>
#include <linux/iio/machine.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/sched.h>
#include <linux/wait.h>

#define BERLIN2_SM_CTRL				0x14
#define  BERLIN2_SM_CTRL_SM_SOC_INT		BIT(1)
#define  BERLIN2_SM_CTRL_SOC_SM_INT		BIT(2)
#define  BERLIN2_SM_CTRL_ADC_SEL(x)		(BIT(x) << 5)	/* 0-15 */
#define  BERLIN2_SM_CTRL_ADC_SEL_MASK		(0xf << 5)
#define  BERLIN2_SM_CTRL_ADC_POWER		BIT(9)
#define  BERLIN2_SM_CTRL_ADC_CLKSEL_DIV2	(0x0 << 10)
#define  BERLIN2_SM_CTRL_ADC_CLKSEL_DIV3	(0x1 << 10)
#define  BERLIN2_SM_CTRL_ADC_CLKSEL_DIV4	(0x2 << 10)
#define  BERLIN2_SM_CTRL_ADC_CLKSEL_DIV8	(0x3 << 10)
#define  BERLIN2_SM_CTRL_ADC_CLKSEL_MASK	(0x3 << 10)
#define  BERLIN2_SM_CTRL_ADC_START		BIT(12)
#define  BERLIN2_SM_CTRL_ADC_RESET		BIT(13)
#define  BERLIN2_SM_CTRL_ADC_BANDGAP_RDY	BIT(14)
#define  BERLIN2_SM_CTRL_ADC_CONT_SINGLE	(0x0 << 15)
#define  BERLIN2_SM_CTRL_ADC_CONT_CONTINUOUS	(0x1 << 15)
#define  BERLIN2_SM_CTRL_ADC_BUFFER_EN		BIT(16)
#define  BERLIN2_SM_CTRL_ADC_VREF_EXT		(0x0 << 17)
#define  BERLIN2_SM_CTRL_ADC_VREF_INT		(0x1 << 17)
#define  BERLIN2_SM_CTRL_ADC_ROTATE		BIT(19)
#define  BERLIN2_SM_CTRL_TSEN_EN		BIT(20)
#define  BERLIN2_SM_CTRL_TSEN_CLK_SEL_125	(0x0 << 21)	/* 1.25 MHz */
#define  BERLIN2_SM_CTRL_TSEN_CLK_SEL_250	(0x1 << 21)	/* 2.5 MHz */
#define  BERLIN2_SM_CTRL_TSEN_MODE_0_125	(0x0 << 22)	/* 0-125 C */
#define  BERLIN2_SM_CTRL_TSEN_MODE_10_50	(0x1 << 22)	/* 10-50 C */
#define  BERLIN2_SM_CTRL_TSEN_RESET		BIT(29)
#define BERLIN2_SM_ADC_DATA			0x20
#define  BERLIN2_SM_ADC_MASK			0x3ff
#define BERLIN2_SM_ADC_STATUS			0x1c
#define  BERLIN2_SM_ADC_STATUS_DATA_RDY(x)	BIT(x)		/* 0-15 */
#define  BERLIN2_SM_ADC_STATUS_DATA_RDY_MASK	0xf
#define  BERLIN2_SM_ADC_STATUS_INT_EN(x)	(BIT(x) << 16)	/* 0-15 */
#define  BERLIN2_SM_ADC_STATUS_INT_EN_MASK	(0xf << 16)
#define BERLIN2_SM_TSEN_STATUS			0x24
#define  BERLIN2_SM_TSEN_STATUS_DATA_RDY	BIT(0)
#define  BERLIN2_SM_TSEN_STATUS_INT_EN		BIT(1)
#define BERLIN2_SM_TSEN_DATA			0x28
#define  BERLIN2_SM_TSEN_MASK			0xfff
#define BERLIN2_SM_TSEN_CTRL			0x74
#define  BERLIN2_SM_TSEN_CTRL_START		BIT(8)
#define  BERLIN2_SM_TSEN_CTRL_SETTLING_4	(0x0 << 21)	/* 4 us */
#define  BERLIN2_SM_TSEN_CTRL_SETTLING_12	(0x1 << 21)	/* 12 us */
#define  BERLIN2_SM_TSEN_CTRL_SETTLING_MASK	(0x1 << 21)
#define  BERLIN2_SM_TSEN_CTRL_TRIM(x)		((x) << 22)
#define  BERLIN2_SM_TSEN_CTRL_TRIM_MASK		(0xf << 22)

struct berlin2_adc_priv {
	struct regmap		*regmap;
	struct mutex		lock;
	wait_queue_head_t	wq;
	bool			data_available;
	int			data;
};

#define BERLIN2_ADC_CHANNEL(n, t)					\
		{							\
			.channel	= n,				\
			.datasheet_name	= "channel"#n,			\
			.type		= t,				\
			.indexed	= 1,				\
			.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),	\
		}

static struct iio_chan_spec berlin2_adc_channels[] = {
	BERLIN2_ADC_CHANNEL(0, IIO_VOLTAGE),	/* external input */
	BERLIN2_ADC_CHANNEL(1, IIO_VOLTAGE),	/* external input */
	BERLIN2_ADC_CHANNEL(2, IIO_VOLTAGE),	/* external input */
	BERLIN2_ADC_CHANNEL(3, IIO_VOLTAGE),	/* external input */
	BERLIN2_ADC_CHANNEL(4, IIO_VOLTAGE),	/* reserved */
	BERLIN2_ADC_CHANNEL(5, IIO_VOLTAGE),	/* reserved */
	{					/* temperature sensor */
		.channel		= 6,
		.datasheet_name		= "channel6",
		.type			= IIO_TEMP,
		.indexed		= 0,
		.info_mask_separate	= BIT(IIO_CHAN_INFO_PROCESSED),
	},
	BERLIN2_ADC_CHANNEL(7, IIO_VOLTAGE),	/* reserved */
	IIO_CHAN_SOFT_TIMESTAMP(8),		/* timestamp */
};
#define BERLIN2_N_CHANNELS	ARRAY_SIZE(berlin2_adc_channels)

static int berlin2_adc_read(struct iio_dev *indio_dev, int channel)
{
	struct berlin2_adc_priv *priv = iio_priv(indio_dev);
	int data, ret;

	mutex_lock(&priv->lock);

	/* Configure the ADC */
	regmap_update_bits(priv->regmap, BERLIN2_SM_CTRL,
			BERLIN2_SM_CTRL_ADC_RESET | BERLIN2_SM_CTRL_ADC_SEL_MASK
			| BERLIN2_SM_CTRL_ADC_START,
			BERLIN2_SM_CTRL_ADC_SEL(channel) | BERLIN2_SM_CTRL_ADC_START);

	ret = wait_event_interruptible_timeout(priv->wq, priv->data_available,
			msecs_to_jiffies(1000));

	/* Disable the interrupts */
	regmap_update_bits(priv->regmap, BERLIN2_SM_ADC_STATUS,
			BERLIN2_SM_ADC_STATUS_INT_EN(channel), 0);

	if (ret == 0)
		ret = -ETIMEDOUT;
	if (ret < 0) {
		mutex_unlock(&priv->lock);
		return ret;
	}

	regmap_update_bits(priv->regmap, BERLIN2_SM_CTRL,
			BERLIN2_SM_CTRL_ADC_START, 0);

	data = priv->data;
	priv->data_available = false;

	mutex_unlock(&priv->lock);

	return data;
}

static int berlin2_adc_tsen_read(struct iio_dev *indio_dev)
{
	struct berlin2_adc_priv *priv = iio_priv(indio_dev);
	int data, ret;

	mutex_lock(&priv->lock);

	/* Configure the ADC */
	regmap_update_bits(priv->regmap, BERLIN2_SM_CTRL,
			BERLIN2_SM_CTRL_TSEN_RESET | BERLIN2_SM_CTRL_ADC_ROTATE,
			BERLIN2_SM_CTRL_ADC_ROTATE);

	/* Configure the temperature sensor */
	regmap_update_bits(priv->regmap, BERLIN2_SM_TSEN_CTRL,
			BERLIN2_SM_TSEN_CTRL_TRIM_MASK | BERLIN2_SM_TSEN_CTRL_SETTLING_MASK
			| BERLIN2_SM_TSEN_CTRL_START,
			BERLIN2_SM_TSEN_CTRL_TRIM(3) | BERLIN2_SM_TSEN_CTRL_SETTLING_12
			| BERLIN2_SM_TSEN_CTRL_START);

	ret = wait_event_interruptible_timeout(priv->wq, priv->data_available,
			msecs_to_jiffies(1000));

	/* Disable interrupts */
	regmap_update_bits(priv->regmap, BERLIN2_SM_TSEN_STATUS,
			BERLIN2_SM_TSEN_STATUS_INT_EN, 0);

	if (ret == 0)
		ret = -ETIMEDOUT;
	if (ret < 0) {
		mutex_unlock(&priv->lock);
		return ret;
	}

	regmap_update_bits(priv->regmap, BERLIN2_SM_TSEN_CTRL,
			BERLIN2_SM_TSEN_CTRL_START, 0);

	data = priv->data;
	priv->data_available = false;

	mutex_unlock(&priv->lock);

	return data;
}

static int berlin2_adc_read_raw(struct iio_dev *indio_dev,
		struct iio_chan_spec const *chan, int *val, int *val2,
		long mask)
{
	struct berlin2_adc_priv *priv = iio_priv(indio_dev);
	int temp;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (chan->type != IIO_VOLTAGE)
			return -EINVAL;

		/* Enable the interrupts */
		regmap_write(priv->regmap, BERLIN2_SM_ADC_STATUS,
				BERLIN2_SM_ADC_STATUS_INT_EN(chan->channel));

		*val = berlin2_adc_read(indio_dev, chan->channel);
		if (*val < 0)
			return *val;

		return IIO_VAL_INT;
	case IIO_CHAN_INFO_PROCESSED:
		if (chan->type != IIO_TEMP)
			return -EINVAL;

		/* Enable interrupts */
		regmap_write(priv->regmap, BERLIN2_SM_TSEN_STATUS,
				BERLIN2_SM_TSEN_STATUS_INT_EN);

		temp = berlin2_adc_tsen_read(indio_dev);
		if (temp < 0)
			return temp;

		if (temp > 2047)
			temp = -(4096 - temp);

		/* Convert to milli Celsius */
		*val = ((temp * 100000) / 264 - 270000);
		return IIO_VAL_INT;
	default:
		break;
	}

	return -EINVAL;
}

static irqreturn_t berlin2_adc_irq(int irq, void *private)
{
	struct berlin2_adc_priv *priv = iio_priv(private);
	unsigned val;

	regmap_read(priv->regmap, BERLIN2_SM_ADC_STATUS, &val);
	if (val & BERLIN2_SM_ADC_STATUS_DATA_RDY_MASK) {
		regmap_read(priv->regmap, BERLIN2_SM_ADC_DATA, &priv->data);
		priv->data &= BERLIN2_SM_ADC_MASK;

		val &= ~BERLIN2_SM_ADC_STATUS_DATA_RDY_MASK;
		regmap_write(priv->regmap, BERLIN2_SM_ADC_STATUS, val);

		priv->data_available = true;
		wake_up_interruptible(&priv->wq);
	}

	return IRQ_HANDLED;
}

static irqreturn_t berlin2_adc_tsen_irq(int irq, void *private)
{
	struct berlin2_adc_priv *priv = iio_priv(private);
	unsigned val;

	regmap_read(priv->regmap, BERLIN2_SM_TSEN_STATUS, &val);
	if (val & BERLIN2_SM_TSEN_STATUS_DATA_RDY) {
		regmap_read(priv->regmap, BERLIN2_SM_TSEN_DATA, &priv->data);
		priv->data &= BERLIN2_SM_TSEN_MASK;

		val &= ~BERLIN2_SM_TSEN_STATUS_DATA_RDY;
		regmap_write(priv->regmap, BERLIN2_SM_TSEN_STATUS, val);

		priv->data_available = true;
		wake_up_interruptible(&priv->wq);
	}

	return IRQ_HANDLED;
}

static const struct iio_info berlin2_adc_info = {
	.driver_module	= THIS_MODULE,
	.read_raw	= berlin2_adc_read_raw,
};

static int berlin2_adc_probe(struct platform_device *pdev)
{
	struct iio_dev *indio_dev;
	struct berlin2_adc_priv *priv;
	struct device_node *parent_np = of_get_parent(pdev->dev.of_node);
	int irq, tsen_irq;
	int ret;

	indio_dev = devm_iio_device_alloc(&pdev->dev,
					  sizeof(struct berlin2_adc_priv));
	if (!indio_dev)
		return -ENOMEM;

	priv = iio_priv(indio_dev);
	platform_set_drvdata(pdev, indio_dev);

	priv->regmap = syscon_node_to_regmap(parent_np);
	of_node_put(parent_np);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	irq = platform_get_irq_byname(pdev, "adc");
	if (irq < 0)
		return -ENODEV;

	tsen_irq = platform_get_irq_byname(pdev, "tsen");
	if (tsen_irq < 0)
		return -ENODEV;

	ret = devm_request_irq(&pdev->dev, irq, berlin2_adc_irq, 0,
			pdev->dev.driver->name, indio_dev);
	if (ret)
		return ret;

	ret = devm_request_irq(&pdev->dev, tsen_irq, berlin2_adc_tsen_irq,
			0, pdev->dev.driver->name, indio_dev);
	if (ret)
		return ret;

	init_waitqueue_head(&priv->wq);
	mutex_init(&priv->lock);

	indio_dev->dev.parent = &pdev->dev;
	indio_dev->name = dev_name(&pdev->dev);
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &berlin2_adc_info;

	indio_dev->num_channels = BERLIN2_N_CHANNELS;
	indio_dev->channels = berlin2_adc_channels;

	/* Power up the ADC */
	regmap_update_bits(priv->regmap, BERLIN2_SM_CTRL,
			BERLIN2_SM_CTRL_ADC_POWER, BERLIN2_SM_CTRL_ADC_POWER);

	ret = iio_device_register(indio_dev);
	if (ret) {
		/* Power down the ADC */
		regmap_update_bits(priv->regmap, BERLIN2_SM_CTRL,
				BERLIN2_SM_CTRL_ADC_POWER, 0);
		return ret;
	}

	return 0;
}

static int berlin2_adc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct berlin2_adc_priv *priv = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	/* Power down the ADC */
	regmap_update_bits(priv->regmap, BERLIN2_SM_CTRL,
			BERLIN2_SM_CTRL_ADC_POWER, 0);

	return 0;
}

static const struct of_device_id berlin2_adc_match[] = {
	{ .compatible = "marvell,berlin2-adc", },
	{ },
};
MODULE_DEVICE_TABLE(of, berlin2_adc_match);

static struct platform_driver berlin2_adc_driver = {
	.driver	= {
		.name		= "berlin2-adc",
		.of_match_table	= berlin2_adc_match,
	},
	.probe	= berlin2_adc_probe,
	.remove	= berlin2_adc_remove,
};
module_platform_driver(berlin2_adc_driver);

MODULE_AUTHOR("Antoine Tenart <antoine.tenart@free-electrons.com>");
MODULE_DESCRIPTION("Marvell Berlin2 ADC driver");
MODULE_LICENSE("GPL v2");
