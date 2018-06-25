// SPDX-License-Identifier: GPL-2.0
/*
 * This file is part the core part STM32 DFSDM driver
 *
 * Copyright (C) 2017, STMicroelectronics - All Rights Reserved
 * Author(s): Arnaud Pouliquen <arnaud.pouliquen@st.com> for STMicroelectronics.
 */

#include <linux/clk.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include "stm32-dfsdm.h"

struct stm32_dfsdm_dev_data {
	unsigned int num_filters;
	unsigned int num_channels;
	const struct regmap_config *regmap_cfg;
};

#define STM32H7_DFSDM_NUM_FILTERS	4
#define STM32H7_DFSDM_NUM_CHANNELS	8
#define STM32MP1_DFSDM_NUM_FILTERS	6
#define STM32MP1_DFSDM_NUM_CHANNELS	8

static bool stm32_dfsdm_volatile_reg(struct device *dev, unsigned int reg)
{
	if (reg < DFSDM_FILTER_BASE_ADR)
		return false;

	/*
	 * Mask is done on register to avoid to list registers of all
	 * filter instances.
	 */
	switch (reg & DFSDM_FILTER_REG_MASK) {
	case DFSDM_CR1(0) & DFSDM_FILTER_REG_MASK:
	case DFSDM_ISR(0) & DFSDM_FILTER_REG_MASK:
	case DFSDM_JDATAR(0) & DFSDM_FILTER_REG_MASK:
	case DFSDM_RDATAR(0) & DFSDM_FILTER_REG_MASK:
		return true;
	}

	return false;
}

static const struct regmap_config stm32h7_dfsdm_regmap_cfg = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = sizeof(u32),
	.max_register = 0x2B8,
	.volatile_reg = stm32_dfsdm_volatile_reg,
	.fast_io = true,
};

static const struct stm32_dfsdm_dev_data stm32h7_dfsdm_data = {
	.num_filters = STM32H7_DFSDM_NUM_FILTERS,
	.num_channels = STM32H7_DFSDM_NUM_CHANNELS,
	.regmap_cfg = &stm32h7_dfsdm_regmap_cfg,
};

static const struct regmap_config stm32mp1_dfsdm_regmap_cfg = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = sizeof(u32),
	.max_register = 0x7fc,
	.volatile_reg = stm32_dfsdm_volatile_reg,
	.fast_io = true,
};

static const struct stm32_dfsdm_dev_data stm32mp1_dfsdm_data = {
	.num_filters = STM32MP1_DFSDM_NUM_FILTERS,
	.num_channels = STM32MP1_DFSDM_NUM_CHANNELS,
	.regmap_cfg = &stm32mp1_dfsdm_regmap_cfg,
};

struct dfsdm_priv {
	struct platform_device *pdev; /* platform device */

	struct stm32_dfsdm dfsdm; /* common data exported for all instances */

	unsigned int spi_clk_out_div; /* SPI clkout divider value */
	atomic_t n_active_ch;	/* number of current active channels */

	struct clk *clk; /* DFSDM clock */
	struct clk *aclk; /* audio clock */
};

/**
 * stm32_dfsdm_start_dfsdm - start global dfsdm interface.
 *
 * Enable interface if n_active_ch is not null.
 * @dfsdm: Handle used to retrieve dfsdm context.
 */
int stm32_dfsdm_start_dfsdm(struct stm32_dfsdm *dfsdm)
{
	struct dfsdm_priv *priv = container_of(dfsdm, struct dfsdm_priv, dfsdm);
	struct device *dev = &priv->pdev->dev;
	unsigned int clk_div = priv->spi_clk_out_div, clk_src;
	int ret;

	if (atomic_inc_return(&priv->n_active_ch) == 1) {
		ret = clk_prepare_enable(priv->clk);
		if (ret < 0) {
			dev_err(dev, "Failed to start clock\n");
			goto error_ret;
		}
		if (priv->aclk) {
			ret = clk_prepare_enable(priv->aclk);
			if (ret < 0) {
				dev_err(dev, "Failed to start audio clock\n");
				goto disable_clk;
			}
		}

		/* select clock source, e.g. 0 for "dfsdm" or 1 for "audio" */
		clk_src = priv->aclk ? 1 : 0;
		ret = regmap_update_bits(dfsdm->regmap, DFSDM_CHCFGR1(0),
					 DFSDM_CHCFGR1_CKOUTSRC_MASK,
					 DFSDM_CHCFGR1_CKOUTSRC(clk_src));
		if (ret < 0)
			goto disable_aclk;

		/* Output the SPI CLKOUT (if clk_div == 0 clock if OFF) */
		ret = regmap_update_bits(dfsdm->regmap, DFSDM_CHCFGR1(0),
					 DFSDM_CHCFGR1_CKOUTDIV_MASK,
					 DFSDM_CHCFGR1_CKOUTDIV(clk_div));
		if (ret < 0)
			goto disable_aclk;

		/* Global enable of DFSDM interface */
		ret = regmap_update_bits(dfsdm->regmap, DFSDM_CHCFGR1(0),
					 DFSDM_CHCFGR1_DFSDMEN_MASK,
					 DFSDM_CHCFGR1_DFSDMEN(1));
		if (ret < 0)
			goto disable_aclk;
	}

	dev_dbg(dev, "%s: n_active_ch %d\n", __func__,
		atomic_read(&priv->n_active_ch));

	return 0;

disable_aclk:
	clk_disable_unprepare(priv->aclk);
disable_clk:
	clk_disable_unprepare(priv->clk);

error_ret:
	atomic_dec(&priv->n_active_ch);

	return ret;
}
EXPORT_SYMBOL_GPL(stm32_dfsdm_start_dfsdm);

/**
 * stm32_dfsdm_stop_dfsdm - stop global DFSDM interface.
 *
 * Disable interface if n_active_ch is null
 * @dfsdm: Handle used to retrieve dfsdm context.
 */
int stm32_dfsdm_stop_dfsdm(struct stm32_dfsdm *dfsdm)
{
	struct dfsdm_priv *priv = container_of(dfsdm, struct dfsdm_priv, dfsdm);
	int ret;

	if (atomic_dec_and_test(&priv->n_active_ch)) {
		/* Global disable of DFSDM interface */
		ret = regmap_update_bits(dfsdm->regmap, DFSDM_CHCFGR1(0),
					 DFSDM_CHCFGR1_DFSDMEN_MASK,
					 DFSDM_CHCFGR1_DFSDMEN(0));
		if (ret < 0)
			return ret;

		/* Stop SPI CLKOUT */
		ret = regmap_update_bits(dfsdm->regmap, DFSDM_CHCFGR1(0),
					 DFSDM_CHCFGR1_CKOUTDIV_MASK,
					 DFSDM_CHCFGR1_CKOUTDIV(0));
		if (ret < 0)
			return ret;

		clk_disable_unprepare(priv->clk);
		if (priv->aclk)
			clk_disable_unprepare(priv->aclk);
	}
	dev_dbg(&priv->pdev->dev, "%s: n_active_ch %d\n", __func__,
		atomic_read(&priv->n_active_ch));

	return 0;
}
EXPORT_SYMBOL_GPL(stm32_dfsdm_stop_dfsdm);

static int stm32_dfsdm_parse_of(struct platform_device *pdev,
				struct dfsdm_priv *priv)
{
	struct device_node *node = pdev->dev.of_node;
	struct resource *res;
	unsigned long clk_freq;
	unsigned int spi_freq, rem;
	int ret;

	if (!node)
		return -EINVAL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get memory resource\n");
		return -ENODEV;
	}
	priv->dfsdm.phys_base = res->start;
	priv->dfsdm.base = devm_ioremap_resource(&pdev->dev, res);

	/*
	 * "dfsdm" clock is mandatory for DFSDM peripheral clocking.
	 * "dfsdm" or "audio" clocks can be used as source clock for
	 * the SPI clock out signal and internal processing, depending
	 * on use case.
	 */
	priv->clk = devm_clk_get(&pdev->dev, "dfsdm");
	if (IS_ERR(priv->clk)) {
		dev_err(&pdev->dev, "No stm32_dfsdm_clk clock found\n");
		return -EINVAL;
	}

	priv->aclk = devm_clk_get(&pdev->dev, "audio");
	if (IS_ERR(priv->aclk))
		priv->aclk = NULL;

	if (priv->aclk)
		clk_freq = clk_get_rate(priv->aclk);
	else
		clk_freq = clk_get_rate(priv->clk);

	/* SPI clock out frequency */
	ret = of_property_read_u32(pdev->dev.of_node, "spi-max-frequency",
				   &spi_freq);
	if (ret < 0) {
		/* No SPI master mode */
		return 0;
	}

	priv->spi_clk_out_div = div_u64_rem(clk_freq, spi_freq, &rem) - 1;
	if (!priv->spi_clk_out_div) {
		/* spi_clk_out_div == 0 means ckout is OFF */
		dev_err(&pdev->dev, "spi-max-frequency not achievable\n");
		return -EINVAL;
	}
	priv->dfsdm.spi_master_freq = spi_freq;

	if (rem) {
		dev_warn(&pdev->dev, "SPI clock not accurate\n");
		dev_warn(&pdev->dev, "%ld = %d * %d + %d\n",
			 clk_freq, spi_freq, priv->spi_clk_out_div + 1, rem);
	}

	return 0;
};

static const struct of_device_id stm32_dfsdm_of_match[] = {
	{
		.compatible = "st,stm32h7-dfsdm",
		.data = &stm32h7_dfsdm_data,
	},
	{
		.compatible = "st,stm32mp1-dfsdm",
		.data = &stm32mp1_dfsdm_data,
	},
	{}
};
MODULE_DEVICE_TABLE(of, stm32_dfsdm_of_match);

static int stm32_dfsdm_probe(struct platform_device *pdev)
{
	struct dfsdm_priv *priv;
	const struct stm32_dfsdm_dev_data *dev_data;
	struct stm32_dfsdm *dfsdm;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->pdev = pdev;

	dev_data = of_device_get_match_data(&pdev->dev);

	dfsdm = &priv->dfsdm;
	dfsdm->fl_list = devm_kcalloc(&pdev->dev, dev_data->num_filters,
				      sizeof(*dfsdm->fl_list), GFP_KERNEL);
	if (!dfsdm->fl_list)
		return -ENOMEM;

	dfsdm->num_fls = dev_data->num_filters;
	dfsdm->ch_list = devm_kcalloc(&pdev->dev, dev_data->num_channels,
				      sizeof(*dfsdm->ch_list),
				      GFP_KERNEL);
	if (!dfsdm->ch_list)
		return -ENOMEM;
	dfsdm->num_chs = dev_data->num_channels;

	ret = stm32_dfsdm_parse_of(pdev, priv);
	if (ret < 0)
		return ret;

	dfsdm->regmap = devm_regmap_init_mmio_clk(&pdev->dev, "dfsdm",
						  dfsdm->base,
						  dev_data->regmap_cfg);
	if (IS_ERR(dfsdm->regmap)) {
		ret = PTR_ERR(dfsdm->regmap);
		dev_err(&pdev->dev, "%s: Failed to allocate regmap: %d\n",
			__func__, ret);
		return ret;
	}

	platform_set_drvdata(pdev, dfsdm);

	return devm_of_platform_populate(&pdev->dev);
}

static struct platform_driver stm32_dfsdm_driver = {
	.probe = stm32_dfsdm_probe,
	.driver = {
		.name = "stm32-dfsdm",
		.of_match_table = stm32_dfsdm_of_match,
	},
};

module_platform_driver(stm32_dfsdm_driver);

MODULE_AUTHOR("Arnaud Pouliquen <arnaud.pouliquen@st.com>");
MODULE_DESCRIPTION("STMicroelectronics STM32 dfsdm driver");
MODULE_LICENSE("GPL v2");
