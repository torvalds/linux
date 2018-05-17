// SPDX-License-Identifier: GPL-2.0
/*
 * This file is part of STM32 DAC driver
 *
 * Copyright (C) 2017, STMicroelectronics - All Rights Reserved
 * Author: Fabrice Gasnier <fabrice.gasnier@st.com>.
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>

#include "stm32-dac-core.h"

/**
 * struct stm32_dac_priv - stm32 DAC core private data
 * @pclk:		peripheral clock common for all DACs
 * @rst:		peripheral reset control
 * @vref:		regulator reference
 * @common:		Common data for all DAC instances
 */
struct stm32_dac_priv {
	struct clk *pclk;
	struct reset_control *rst;
	struct regulator *vref;
	struct stm32_dac_common common;
};

/**
 * struct stm32_dac_cfg - DAC configuration
 * @has_hfsel: DAC has high frequency control
 */
struct stm32_dac_cfg {
	bool has_hfsel;
};

static struct stm32_dac_priv *to_stm32_dac_priv(struct stm32_dac_common *com)
{
	return container_of(com, struct stm32_dac_priv, common);
}

static const struct regmap_config stm32_dac_regmap_cfg = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = sizeof(u32),
	.max_register = 0x3fc,
};

static int stm32_dac_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct stm32_dac_cfg *cfg;
	struct stm32_dac_priv *priv;
	struct regmap *regmap;
	struct resource *res;
	void __iomem *mmio;
	int ret;

	if (!dev->of_node)
		return -ENODEV;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	cfg = (const struct stm32_dac_cfg *)
		of_match_device(dev->driver->of_match_table, dev)->data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mmio = devm_ioremap_resource(dev, res);
	if (IS_ERR(mmio))
		return PTR_ERR(mmio);

	regmap = devm_regmap_init_mmio(dev, mmio, &stm32_dac_regmap_cfg);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);
	priv->common.regmap = regmap;

	priv->vref = devm_regulator_get(dev, "vref");
	if (IS_ERR(priv->vref)) {
		ret = PTR_ERR(priv->vref);
		dev_err(dev, "vref get failed, %d\n", ret);
		return ret;
	}

	ret = regulator_enable(priv->vref);
	if (ret < 0) {
		dev_err(dev, "vref enable failed\n");
		return ret;
	}

	ret = regulator_get_voltage(priv->vref);
	if (ret < 0) {
		dev_err(dev, "vref get voltage failed, %d\n", ret);
		goto err_vref;
	}
	priv->common.vref_mv = ret / 1000;
	dev_dbg(dev, "vref+=%dmV\n", priv->common.vref_mv);

	priv->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(priv->pclk)) {
		ret = PTR_ERR(priv->pclk);
		dev_err(dev, "pclk get failed\n");
		goto err_vref;
	}

	ret = clk_prepare_enable(priv->pclk);
	if (ret < 0) {
		dev_err(dev, "pclk enable failed\n");
		goto err_vref;
	}

	priv->rst = devm_reset_control_get_exclusive(dev, NULL);
	if (!IS_ERR(priv->rst)) {
		reset_control_assert(priv->rst);
		udelay(2);
		reset_control_deassert(priv->rst);
	}

	if (cfg && cfg->has_hfsel) {
		/* When clock speed is higher than 80MHz, set HFSEL */
		priv->common.hfsel = (clk_get_rate(priv->pclk) > 80000000UL);
		ret = regmap_update_bits(regmap, STM32_DAC_CR,
					 STM32H7_DAC_CR_HFSEL,
					 priv->common.hfsel ?
					 STM32H7_DAC_CR_HFSEL : 0);
		if (ret)
			goto err_pclk;
	}

	platform_set_drvdata(pdev, &priv->common);

	ret = of_platform_populate(pdev->dev.of_node, NULL, NULL, dev);
	if (ret < 0) {
		dev_err(dev, "failed to populate DT children\n");
		goto err_pclk;
	}

	return 0;

err_pclk:
	clk_disable_unprepare(priv->pclk);
err_vref:
	regulator_disable(priv->vref);

	return ret;
}

static int stm32_dac_remove(struct platform_device *pdev)
{
	struct stm32_dac_common *common = platform_get_drvdata(pdev);
	struct stm32_dac_priv *priv = to_stm32_dac_priv(common);

	of_platform_depopulate(&pdev->dev);
	clk_disable_unprepare(priv->pclk);
	regulator_disable(priv->vref);

	return 0;
}

static const struct stm32_dac_cfg stm32h7_dac_cfg = {
	.has_hfsel = true,
};

static const struct of_device_id stm32_dac_of_match[] = {
	{
		.compatible = "st,stm32f4-dac-core",
	}, {
		.compatible = "st,stm32h7-dac-core",
		.data = (void *)&stm32h7_dac_cfg,
	},
	{},
};
MODULE_DEVICE_TABLE(of, stm32_dac_of_match);

static struct platform_driver stm32_dac_driver = {
	.probe = stm32_dac_probe,
	.remove = stm32_dac_remove,
	.driver = {
		.name = "stm32-dac-core",
		.of_match_table = stm32_dac_of_match,
	},
};
module_platform_driver(stm32_dac_driver);

MODULE_AUTHOR("Fabrice Gasnier <fabrice.gasnier@st.com>");
MODULE_DESCRIPTION("STMicroelectronics STM32 DAC core driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:stm32-dac-core");
