/*
 * dwmac-stm32.c - DWMAC Specific Glue layer for STM32 MCU
 *
 * Copyright (C) Alexandre Torgue 2015
 * Author:  Alexandre Torgue <alexandre.torgue@gmail.com>
 * License terms:  GNU General Public License (GPL), version 2
 *
 */

#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_net.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/stmmac.h>

#include "stmmac_platform.h"

#define MII_PHY_SEL_MASK	BIT(23)

struct stm32_dwmac {
	struct clk *clk_tx;
	struct clk *clk_rx;
	u32 mode_reg;		/* MAC glue-logic mode register */
	struct regmap *regmap;
	u32 speed;
};

static int stm32_dwmac_init(struct plat_stmmacenet_data *plat_dat)
{
	struct stm32_dwmac *dwmac = plat_dat->bsp_priv;
	u32 reg = dwmac->mode_reg;
	u32 val;
	int ret;

	val = (plat_dat->interface == PHY_INTERFACE_MODE_MII) ? 0 : 1;
	ret = regmap_update_bits(dwmac->regmap, reg, MII_PHY_SEL_MASK, val);
	if (ret)
		return ret;

	ret = clk_prepare_enable(dwmac->clk_tx);
	if (ret)
		return ret;

	ret = clk_prepare_enable(dwmac->clk_rx);
	if (ret)
		clk_disable_unprepare(dwmac->clk_tx);

	return ret;
}

static void stm32_dwmac_clk_disable(struct stm32_dwmac *dwmac)
{
	clk_disable_unprepare(dwmac->clk_tx);
	clk_disable_unprepare(dwmac->clk_rx);
}

static int stm32_dwmac_parse_data(struct stm32_dwmac *dwmac,
				  struct device *dev)
{
	struct device_node *np = dev->of_node;
	int err;

	/*  Get TX/RX clocks */
	dwmac->clk_tx = devm_clk_get(dev, "mac-clk-tx");
	if (IS_ERR(dwmac->clk_tx)) {
		dev_err(dev, "No tx clock provided...\n");
		return PTR_ERR(dwmac->clk_tx);
	}
	dwmac->clk_rx = devm_clk_get(dev, "mac-clk-rx");
	if (IS_ERR(dwmac->clk_rx)) {
		dev_err(dev, "No rx clock provided...\n");
		return PTR_ERR(dwmac->clk_rx);
	}

	/* Get mode register */
	dwmac->regmap = syscon_regmap_lookup_by_phandle(np, "st,syscon");
	if (IS_ERR(dwmac->regmap))
		return PTR_ERR(dwmac->regmap);

	err = of_property_read_u32_index(np, "st,syscon", 1, &dwmac->mode_reg);
	if (err)
		dev_err(dev, "Can't get sysconfig mode offset (%d)\n", err);

	return err;
}

static int stm32_dwmac_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	struct stm32_dwmac *dwmac;
	int ret;

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return ret;

	plat_dat = stmmac_probe_config_dt(pdev, &stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return PTR_ERR(plat_dat);

	dwmac = devm_kzalloc(&pdev->dev, sizeof(*dwmac), GFP_KERNEL);
	if (!dwmac)
		return -ENOMEM;

	ret = stm32_dwmac_parse_data(dwmac, &pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "Unable to parse OF data\n");
		return ret;
	}

	plat_dat->bsp_priv = dwmac;

	ret = stm32_dwmac_init(plat_dat);
	if (ret)
		return ret;

	ret = stmmac_dvr_probe(&pdev->dev, plat_dat, &stmmac_res);
	if (ret)
		stm32_dwmac_clk_disable(dwmac);

	return ret;
}

static int stm32_dwmac_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	int ret = stmmac_dvr_remove(&pdev->dev);

	stm32_dwmac_clk_disable(priv->plat->bsp_priv);

	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int stm32_dwmac_suspend(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	int ret;

	ret = stmmac_suspend(dev);
	stm32_dwmac_clk_disable(priv->plat->bsp_priv);

	return ret;
}

static int stm32_dwmac_resume(struct device *dev)
{
	struct net_device *ndev = dev_get_drvdata(dev);
	struct stmmac_priv *priv = netdev_priv(ndev);
	int ret;

	ret = stm32_dwmac_init(priv->plat);
	if (ret)
		return ret;

	ret = stmmac_resume(dev);

	return ret;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(stm32_dwmac_pm_ops,
	stm32_dwmac_suspend, stm32_dwmac_resume);

static const struct of_device_id stm32_dwmac_match[] = {
	{ .compatible = "st,stm32-dwmac"},
	{ }
};
MODULE_DEVICE_TABLE(of, stm32_dwmac_match);

static struct platform_driver stm32_dwmac_driver = {
	.probe  = stm32_dwmac_probe,
	.remove = stm32_dwmac_remove,
	.driver = {
		.name           = "stm32-dwmac",
		.pm		= &stm32_dwmac_pm_ops,
		.of_match_table = stm32_dwmac_match,
	},
};
module_platform_driver(stm32_dwmac_driver);

MODULE_AUTHOR("Alexandre Torgue <alexandre.torgue@gmail.com>");
MODULE_DESCRIPTION("STMicroelectronics MCU DWMAC Specific Glue layer");
MODULE_LICENSE("GPL v2");
