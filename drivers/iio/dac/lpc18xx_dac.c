// SPDX-License-Identifier: GPL-2.0-only
/*
 * IIO DAC driver for NXP LPC18xx DAC
 *
 * Copyright (C) 2016 Joachim Eastwood <manabian@gmail.com>
 *
 * UNSUPPORTED hardware features:
 *  - Interrupts
 *  - DMA
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/iio/iio.h>
#include <linux/iio/driver.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

/* LPC18XX DAC registers and bits */
#define LPC18XX_DAC_CR			0x000
#define  LPC18XX_DAC_CR_VALUE_SHIFT	6
#define  LPC18XX_DAC_CR_VALUE_MASK	0x3ff
#define  LPC18XX_DAC_CR_BIAS		BIT(16)
#define LPC18XX_DAC_CTRL		0x004
#define  LPC18XX_DAC_CTRL_DMA_ENA	BIT(3)

struct lpc18xx_dac {
	struct regulator *vref;
	void __iomem *base;
	struct mutex lock;
	struct clk *clk;
};

static const struct iio_chan_spec lpc18xx_dac_iio_channels[] = {
	{
		.type = IIO_VOLTAGE,
		.output = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
	},
};

static int lpc18xx_dac_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan,
				int *val, int *val2, long mask)
{
	struct lpc18xx_dac *dac = iio_priv(indio_dev);
	u32 reg;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		reg = readl(dac->base + LPC18XX_DAC_CR);
		*val = reg >> LPC18XX_DAC_CR_VALUE_SHIFT;
		*val &= LPC18XX_DAC_CR_VALUE_MASK;

		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		*val = regulator_get_voltage(dac->vref) / 1000;
		*val2 = 10;

		return IIO_VAL_FRACTIONAL_LOG2;
	}

	return -EINVAL;
}

static int lpc18xx_dac_write_raw(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan,
				 int val, int val2, long mask)
{
	struct lpc18xx_dac *dac = iio_priv(indio_dev);
	u32 reg;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (val < 0 || val > LPC18XX_DAC_CR_VALUE_MASK)
			return -EINVAL;

		reg = LPC18XX_DAC_CR_BIAS;
		reg |= val << LPC18XX_DAC_CR_VALUE_SHIFT;

		mutex_lock(&dac->lock);
		writel(reg, dac->base + LPC18XX_DAC_CR);
		writel(LPC18XX_DAC_CTRL_DMA_ENA, dac->base + LPC18XX_DAC_CTRL);
		mutex_unlock(&dac->lock);

		return 0;
	}

	return -EINVAL;
}

static const struct iio_info lpc18xx_dac_info = {
	.read_raw = lpc18xx_dac_read_raw,
	.write_raw = lpc18xx_dac_write_raw,
};

static int lpc18xx_dac_probe(struct platform_device *pdev)
{
	struct iio_dev *indio_dev;
	struct lpc18xx_dac *dac;
	int ret;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*dac));
	if (!indio_dev)
		return -ENOMEM;

	platform_set_drvdata(pdev, indio_dev);
	dac = iio_priv(indio_dev);
	mutex_init(&dac->lock);

	dac->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dac->base))
		return PTR_ERR(dac->base);

	dac->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(dac->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(dac->clk),
				     "error getting clock\n");

	dac->vref = devm_regulator_get(&pdev->dev, "vref");
	if (IS_ERR(dac->vref))
		return dev_err_probe(&pdev->dev, PTR_ERR(dac->vref),
				     "error getting regulator\n");

	indio_dev->name = dev_name(&pdev->dev);
	indio_dev->info = &lpc18xx_dac_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = lpc18xx_dac_iio_channels;
	indio_dev->num_channels = ARRAY_SIZE(lpc18xx_dac_iio_channels);

	ret = regulator_enable(dac->vref);
	if (ret) {
		dev_err(&pdev->dev, "unable to enable regulator\n");
		return ret;
	}

	ret = clk_prepare_enable(dac->clk);
	if (ret) {
		dev_err(&pdev->dev, "unable to enable clock\n");
		goto dis_reg;
	}

	writel(0, dac->base + LPC18XX_DAC_CTRL);
	writel(0, dac->base + LPC18XX_DAC_CR);

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&pdev->dev, "unable to register device\n");
		goto dis_clk;
	}

	return 0;

dis_clk:
	clk_disable_unprepare(dac->clk);
dis_reg:
	regulator_disable(dac->vref);
	return ret;
}

static void lpc18xx_dac_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct lpc18xx_dac *dac = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	writel(0, dac->base + LPC18XX_DAC_CTRL);
	clk_disable_unprepare(dac->clk);
	regulator_disable(dac->vref);
}

static const struct of_device_id lpc18xx_dac_match[] = {
	{ .compatible = "nxp,lpc1850-dac" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, lpc18xx_dac_match);

static struct platform_driver lpc18xx_dac_driver = {
	.probe	= lpc18xx_dac_probe,
	.remove_new = lpc18xx_dac_remove,
	.driver	= {
		.name = "lpc18xx-dac",
		.of_match_table = lpc18xx_dac_match,
	},
};
module_platform_driver(lpc18xx_dac_driver);

MODULE_DESCRIPTION("LPC18xx DAC driver");
MODULE_AUTHOR("Joachim Eastwood <manabian@gmail.com>");
MODULE_LICENSE("GPL v2");
