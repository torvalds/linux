// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Nuvoton Technology corporation.

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/mfd/syscon.h>
#include <linux/io.h>
#include <linux/iio/iio.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>

struct npcm_adc {
	bool int_status;
	u32 adc_sample_hz;
	struct device *dev;
	void __iomem *regs;
	struct clk *adc_clk;
	wait_queue_head_t wq;
	struct regulator *vref;
	struct regmap *rst_regmap;
};

/* NPCM7xx reset module */
#define NPCM7XX_IPSRST1_OFFSET		0x020
#define NPCM7XX_IPSRST1_ADC_RST		BIT(27)

/* ADC registers */
#define NPCM_ADCCON	 0x00
#define NPCM_ADCDATA	 0x04

/* ADCCON Register Bits */
#define NPCM_ADCCON_ADC_INT_EN		BIT(21)
#define NPCM_ADCCON_REFSEL		BIT(19)
#define NPCM_ADCCON_ADC_INT_ST		BIT(18)
#define NPCM_ADCCON_ADC_EN		BIT(17)
#define NPCM_ADCCON_ADC_RST		BIT(16)
#define NPCM_ADCCON_ADC_CONV		BIT(13)

#define NPCM_ADCCON_CH_MASK		GENMASK(27, 24)
#define NPCM_ADCCON_CH(x)		((x) << 24)
#define NPCM_ADCCON_DIV_SHIFT		1
#define NPCM_ADCCON_DIV_MASK		GENMASK(8, 1)
#define NPCM_ADC_DATA_MASK(x)		((x) & GENMASK(9, 0))

#define NPCM_ADC_ENABLE		(NPCM_ADCCON_ADC_EN | NPCM_ADCCON_ADC_INT_EN)

/* ADC General Definition */
#define NPCM_RESOLUTION_BITS		10
#define NPCM_INT_VREF_MV		2000

#define NPCM_ADC_CHAN(ch) {					\
	.type = IIO_VOLTAGE,					\
	.indexed = 1,						\
	.channel = ch,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |	\
				BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
}

static const struct iio_chan_spec npcm_adc_iio_channels[] = {
	NPCM_ADC_CHAN(0),
	NPCM_ADC_CHAN(1),
	NPCM_ADC_CHAN(2),
	NPCM_ADC_CHAN(3),
	NPCM_ADC_CHAN(4),
	NPCM_ADC_CHAN(5),
	NPCM_ADC_CHAN(6),
	NPCM_ADC_CHAN(7),
};

static irqreturn_t npcm_adc_isr(int irq, void *data)
{
	u32 regtemp;
	struct iio_dev *indio_dev = data;
	struct npcm_adc *info = iio_priv(indio_dev);

	regtemp = ioread32(info->regs + NPCM_ADCCON);
	if (regtemp & NPCM_ADCCON_ADC_INT_ST) {
		iowrite32(regtemp, info->regs + NPCM_ADCCON);
		wake_up_interruptible(&info->wq);
		info->int_status = true;
	}

	return IRQ_HANDLED;
}

static int npcm_adc_read(struct npcm_adc *info, int *val, u8 channel)
{
	int ret;
	u32 regtemp;

	/* Select ADC channel */
	regtemp = ioread32(info->regs + NPCM_ADCCON);
	regtemp &= ~NPCM_ADCCON_CH_MASK;
	info->int_status = false;
	iowrite32(regtemp | NPCM_ADCCON_CH(channel) |
		  NPCM_ADCCON_ADC_CONV, info->regs + NPCM_ADCCON);

	ret = wait_event_interruptible_timeout(info->wq, info->int_status,
					       msecs_to_jiffies(10));
	if (ret == 0) {
		regtemp = ioread32(info->regs + NPCM_ADCCON);
		if ((regtemp & NPCM_ADCCON_ADC_CONV) && info->rst_regmap) {
			/* if conversion failed - reset ADC module */
			regmap_write(info->rst_regmap, NPCM7XX_IPSRST1_OFFSET,
				     NPCM7XX_IPSRST1_ADC_RST);
			msleep(100);
			regmap_write(info->rst_regmap, NPCM7XX_IPSRST1_OFFSET,
				     0x0);
			msleep(100);

			/* Enable ADC and start conversion module */
			iowrite32(NPCM_ADC_ENABLE | NPCM_ADCCON_ADC_CONV,
				  info->regs + NPCM_ADCCON);
			dev_err(info->dev, "RESET ADC Complete\n");
		}
		return -ETIMEDOUT;
	}
	if (ret < 0)
		return ret;

	*val = NPCM_ADC_DATA_MASK(ioread32(info->regs + NPCM_ADCDATA));

	return 0;
}

static int npcm_adc_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan, int *val,
			     int *val2, long mask)
{
	int ret;
	int vref_uv;
	struct npcm_adc *info = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&indio_dev->mlock);
		ret = npcm_adc_read(info, val, chan->channel);
		mutex_unlock(&indio_dev->mlock);
		if (ret) {
			dev_err(info->dev, "NPCM ADC read failed\n");
			return ret;
		}
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		if (!IS_ERR(info->vref)) {
			vref_uv = regulator_get_voltage(info->vref);
			*val = vref_uv / 1000;
		} else {
			*val = NPCM_INT_VREF_MV;
		}
		*val2 = NPCM_RESOLUTION_BITS;
		return IIO_VAL_FRACTIONAL_LOG2;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = info->adc_sample_hz;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct iio_info npcm_adc_iio_info = {
	.read_raw = &npcm_adc_read_raw,
};

static const struct of_device_id npcm_adc_match[] = {
	{ .compatible = "nuvoton,npcm750-adc", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, npcm_adc_match);

static int npcm_adc_probe(struct platform_device *pdev)
{
	int ret;
	int irq;
	u32 div;
	u32 reg_con;
	struct npcm_adc *info;
	struct iio_dev *indio_dev;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*info));
	if (!indio_dev)
		return -ENOMEM;
	info = iio_priv(indio_dev);

	info->dev = &pdev->dev;

	info->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(info->regs))
		return PTR_ERR(info->regs);

	info->adc_clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(info->adc_clk)) {
		dev_warn(&pdev->dev, "ADC clock failed: can't read clk\n");
		return PTR_ERR(info->adc_clk);
	}

	/* calculate ADC clock sample rate */
	reg_con = ioread32(info->regs + NPCM_ADCCON);
	div = reg_con & NPCM_ADCCON_DIV_MASK;
	div = div >> NPCM_ADCCON_DIV_SHIFT;
	info->adc_sample_hz = clk_get_rate(info->adc_clk) / ((div + 1) * 2);

	if (of_device_is_compatible(np, "nuvoton,npcm750-adc")) {
		info->rst_regmap = syscon_regmap_lookup_by_compatible
			("nuvoton,npcm750-rst");
		if (IS_ERR(info->rst_regmap)) {
			dev_err(&pdev->dev, "Failed to find nuvoton,npcm750-rst\n");
			ret = PTR_ERR(info->rst_regmap);
			goto err_disable_clk;
		}
	}

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		ret = -EINVAL;
		goto err_disable_clk;
	}

	ret = devm_request_irq(&pdev->dev, irq, npcm_adc_isr, 0,
			       "NPCM_ADC", indio_dev);
	if (ret < 0) {
		dev_err(dev, "failed requesting interrupt\n");
		goto err_disable_clk;
	}

	reg_con = ioread32(info->regs + NPCM_ADCCON);
	info->vref = devm_regulator_get_optional(&pdev->dev, "vref");
	if (!IS_ERR(info->vref)) {
		ret = regulator_enable(info->vref);
		if (ret) {
			dev_err(&pdev->dev, "Can't enable ADC reference voltage\n");
			goto err_disable_clk;
		}

		iowrite32(reg_con & ~NPCM_ADCCON_REFSEL,
			  info->regs + NPCM_ADCCON);
	} else {
		/*
		 * Any error which is not ENODEV indicates the regulator
		 * has been specified and so is a failure case.
		 */
		if (PTR_ERR(info->vref) != -ENODEV) {
			ret = PTR_ERR(info->vref);
			goto err_disable_clk;
		}

		/* Use internal reference */
		iowrite32(reg_con | NPCM_ADCCON_REFSEL,
			  info->regs + NPCM_ADCCON);
	}

	init_waitqueue_head(&info->wq);

	reg_con = ioread32(info->regs + NPCM_ADCCON);
	reg_con |= NPCM_ADC_ENABLE;

	/* Enable the ADC Module */
	iowrite32(reg_con, info->regs + NPCM_ADCCON);

	/* Start ADC conversion */
	iowrite32(reg_con | NPCM_ADCCON_ADC_CONV, info->regs + NPCM_ADCCON);

	platform_set_drvdata(pdev, indio_dev);
	indio_dev->name = dev_name(&pdev->dev);
	indio_dev->dev.parent = &pdev->dev;
	indio_dev->info = &npcm_adc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = npcm_adc_iio_channels;
	indio_dev->num_channels = ARRAY_SIZE(npcm_adc_iio_channels);

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&pdev->dev, "Couldn't register the device.\n");
		goto err_iio_register;
	}

	pr_info("NPCM ADC driver probed\n");

	return 0;

err_iio_register:
	iowrite32(reg_con & ~NPCM_ADCCON_ADC_EN, info->regs + NPCM_ADCCON);
	if (!IS_ERR(info->vref))
		regulator_disable(info->vref);
err_disable_clk:
	clk_disable_unprepare(info->adc_clk);

	return ret;
}

static int npcm_adc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct npcm_adc *info = iio_priv(indio_dev);
	u32 regtemp;

	iio_device_unregister(indio_dev);

	regtemp = ioread32(info->regs + NPCM_ADCCON);
	iowrite32(regtemp & ~NPCM_ADCCON_ADC_EN, info->regs + NPCM_ADCCON);
	if (!IS_ERR(info->vref))
		regulator_disable(info->vref);
	clk_disable_unprepare(info->adc_clk);

	return 0;
}

static struct platform_driver npcm_adc_driver = {
	.probe		= npcm_adc_probe,
	.remove		= npcm_adc_remove,
	.driver		= {
		.name	= "npcm_adc",
		.of_match_table = npcm_adc_match,
	},
};

module_platform_driver(npcm_adc_driver);

MODULE_DESCRIPTION("Nuvoton NPCM ADC Driver");
MODULE_AUTHOR("Tomer Maimon <tomer.maimon@nuvoton.com>");
MODULE_LICENSE("GPL v2");
