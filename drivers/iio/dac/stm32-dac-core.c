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
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>

#include "stm32-dac-core.h"

/**
 * struct stm32_dac_priv - stm32 DAC core private data
 * @pclk:		peripheral clock common for all DACs
 * @vref:		regulator reference
 * @common:		Common data for all DAC instances
 */
struct stm32_dac_priv {
	struct clk *pclk;
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

static int stm32_dac_core_hw_start(struct device *dev)
{
	struct stm32_dac_common *common = dev_get_drvdata(dev);
	struct stm32_dac_priv *priv = to_stm32_dac_priv(common);
	int ret;

	ret = regulator_enable(priv->vref);
	if (ret < 0) {
		dev_err(dev, "vref enable failed: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(priv->pclk);
	if (ret < 0) {
		dev_err(dev, "pclk enable failed: %d\n", ret);
		goto err_regulator_disable;
	}

	return 0;

err_regulator_disable:
	regulator_disable(priv->vref);

	return ret;
}

static void stm32_dac_core_hw_stop(struct device *dev)
{
	struct stm32_dac_common *common = dev_get_drvdata(dev);
	struct stm32_dac_priv *priv = to_stm32_dac_priv(common);

	clk_disable_unprepare(priv->pclk);
	regulator_disable(priv->vref);
}

static int stm32_dac_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct stm32_dac_cfg *cfg;
	struct stm32_dac_priv *priv;
	struct regmap *regmap;
	void __iomem *mmio;
	struct reset_control *rst;
	int ret;

	if (!dev->of_node)
		return -ENODEV;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	platform_set_drvdata(pdev, &priv->common);

	cfg = (const struct stm32_dac_cfg *)
		of_match_device(dev->driver->of_match_table, dev)->data;

	mmio = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mmio))
		return PTR_ERR(mmio);

	regmap = devm_regmap_init_mmio_clk(dev, "pclk", mmio,
					   &stm32_dac_regmap_cfg);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);
	priv->common.regmap = regmap;

	priv->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(priv->pclk))
		return dev_err_probe(dev, PTR_ERR(priv->pclk), "pclk get failed\n");

	priv->vref = devm_regulator_get(dev, "vref");
	if (IS_ERR(priv->vref))
		return dev_err_probe(dev, PTR_ERR(priv->vref), "vref get failed\n");

	pm_runtime_get_noresume(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	ret = stm32_dac_core_hw_start(dev);
	if (ret)
		goto err_pm_stop;

	ret = regulator_get_voltage(priv->vref);
	if (ret < 0) {
		dev_err(dev, "vref get voltage failed, %d\n", ret);
		goto err_hw_stop;
	}
	priv->common.vref_mv = ret / 1000;
	dev_dbg(dev, "vref+=%dmV\n", priv->common.vref_mv);

	rst = devm_reset_control_get_optional_exclusive(dev, NULL);
	if (rst) {
		if (IS_ERR(rst)) {
			ret = dev_err_probe(dev, PTR_ERR(rst), "reset get failed\n");
			goto err_hw_stop;
		}

		reset_control_assert(rst);
		udelay(2);
		reset_control_deassert(rst);
	}

	if (cfg && cfg->has_hfsel) {
		/* When clock speed is higher than 80MHz, set HFSEL */
		priv->common.hfsel = (clk_get_rate(priv->pclk) > 80000000UL);
		ret = regmap_update_bits(regmap, STM32_DAC_CR,
					 STM32H7_DAC_CR_HFSEL,
					 priv->common.hfsel ?
					 STM32H7_DAC_CR_HFSEL : 0);
		if (ret)
			goto err_hw_stop;
	}


	ret = of_platform_populate(pdev->dev.of_node, NULL, NULL, dev);
	if (ret < 0) {
		dev_err(dev, "failed to populate DT children\n");
		goto err_hw_stop;
	}

	pm_runtime_put(dev);

	return 0;

err_hw_stop:
	stm32_dac_core_hw_stop(dev);
err_pm_stop:
	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	pm_runtime_put_noidle(dev);

	return ret;
}

static int stm32_dac_remove(struct platform_device *pdev)
{
	pm_runtime_get_sync(&pdev->dev);
	of_platform_depopulate(&pdev->dev);
	stm32_dac_core_hw_stop(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);

	return 0;
}

static int __maybe_unused stm32_dac_core_resume(struct device *dev)
{
	struct stm32_dac_common *common = dev_get_drvdata(dev);
	struct stm32_dac_priv *priv = to_stm32_dac_priv(common);
	int ret;

	if (priv->common.hfsel) {
		/* restore hfsel (maybe lost under low power state) */
		ret = regmap_update_bits(priv->common.regmap, STM32_DAC_CR,
					 STM32H7_DAC_CR_HFSEL,
					 STM32H7_DAC_CR_HFSEL);
		if (ret)
			return ret;
	}

	return pm_runtime_force_resume(dev);
}

static int __maybe_unused stm32_dac_core_runtime_suspend(struct device *dev)
{
	stm32_dac_core_hw_stop(dev);

	return 0;
}

static int __maybe_unused stm32_dac_core_runtime_resume(struct device *dev)
{
	return stm32_dac_core_hw_start(dev);
}

static const struct dev_pm_ops stm32_dac_core_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend, stm32_dac_core_resume)
	SET_RUNTIME_PM_OPS(stm32_dac_core_runtime_suspend,
			   stm32_dac_core_runtime_resume,
			   NULL)
};

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
		.pm = &stm32_dac_core_pm_ops,
	},
};
module_platform_driver(stm32_dac_driver);

MODULE_AUTHOR("Fabrice Gasnier <fabrice.gasnier@st.com>");
MODULE_DESCRIPTION("STMicroelectronics STM32 DAC core driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:stm32-dac-core");
