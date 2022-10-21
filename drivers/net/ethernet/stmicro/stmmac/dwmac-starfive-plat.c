// SPDX-License-Identifier: GPL-2.0
/* StarFive DWMAC platform driver
 *
 * Copyright(C) 2022 StarFive Technology Co., Ltd.
 */

#include <linux/of_device.h>
#include "stmmac_platform.h"

struct starfive_dwmac {
	struct device *dev;
	void __iomem *regs;
	struct clk *clk_tx;
	struct clk *clk_gtx;
	struct clk *clk_gtxc;
	struct clk *clk_rmii_rtx;
};

static void starfive_eth_fix_mac_speed(void *priv, unsigned int speed)
{
	struct starfive_dwmac *dwmac = priv;
	unsigned long rate;
	int err;

	switch (speed) {
	case SPEED_1000:
		rate = 125000000;
		break;
	case SPEED_100:
		rate = 25000000;
		break;
	case SPEED_10:
		rate = 2500000;
		break;
	default:
		dev_err(dwmac->dev, "invalid speed %u\n", speed);
		return;
	}

	err = clk_set_rate(dwmac->clk_gtx, rate);
	if (err < 0)
		dev_err(dwmac->dev, "failed to set tx rate %lu\n", rate);

	err = clk_set_rate(dwmac->clk_rmii_rtx, rate);
	if (err < 0)
		dev_err(dwmac->dev, "failed to set rtx rate %lu\n", rate);
}

static const struct of_device_id starfive_eth_plat_match[] = {
	{.compatible = "starfive,dwmac"},
	{ }
};

static int starfive_eth_plat_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	struct starfive_dwmac *dwmac;
	int err;

	err = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (err)
		return err;

	plat_dat = stmmac_probe_config_dt(pdev, stmmac_res.mac);
	if (IS_ERR(plat_dat)) {
		dev_err(&pdev->dev, "dt configuration failed\n");
		return PTR_ERR(plat_dat);
	}

	dwmac = devm_kzalloc(&pdev->dev, sizeof(*dwmac), GFP_KERNEL);
	if (!dwmac)
		return -ENOMEM;

	dwmac->dev = &pdev->dev;
	dwmac->regs = stmmac_res.addr;

	if (!is_of_node(dev->fwnode))
		goto bypass_clk_reset_gpio;

	dwmac->clk_tx = devm_clk_get(&pdev->dev, "tx");
	if (IS_ERR(dwmac->clk_tx)) {
		err = PTR_ERR(dwmac->clk_tx);
		goto err;
	}

	err = clk_prepare_enable(dwmac->clk_tx);
	if (err < 0)
		goto err;

	dwmac->clk_gtx = devm_clk_get(&pdev->dev, "gtx");
	if (IS_ERR(dwmac->clk_gtx)) {
		err = PTR_ERR(dwmac->clk_gtx);
		goto disable_tx;
	}

	err = clk_prepare_enable(dwmac->clk_gtx);
	if (err < 0)
		goto disable_tx;

	dwmac->clk_gtxc = devm_clk_get(&pdev->dev, "gtxc");
	if (IS_ERR(dwmac->clk_gtxc)) {
		err = PTR_ERR(dwmac->clk_gtxc);
		goto disable_gtx;
	}

	dwmac->clk_rmii_rtx = devm_clk_get(&pdev->dev, "rmii_rtx");
	if (IS_ERR(dwmac->clk_rmii_rtx)) {
		err = PTR_ERR(dwmac->clk_rmii_rtx);
		goto disable_gtx;
	}

	err = clk_prepare_enable(dwmac->clk_gtxc);
	if (err < 0)
		goto disable_gtx;

	err = stmmac_dvr_probe(&pdev->dev, plat_dat, &stmmac_res);
	if (err)
		goto err;

bypass_clk_reset_gpio:
	plat_dat->fix_mac_speed = starfive_eth_fix_mac_speed;
	plat_dat->init = NULL;
	plat_dat->bsp_priv = dwmac;
	return 0;

disable_gtx:
	clk_disable_unprepare(dwmac->clk_gtx);
disable_tx:
	clk_disable_unprepare(dwmac->clk_tx);
err:
	stmmac_remove_config_dt(pdev, plat_dat);
	return err;
}

static int starfive_eth_plat_remove(struct platform_device *pdev)
{
	struct starfive_dwmac *dwmac = get_stmmac_bsp_priv(&pdev->dev);

	clk_disable_unprepare(dwmac->clk_gtxc);
	clk_disable_unprepare(dwmac->clk_gtx);
	clk_disable_unprepare(dwmac->clk_tx);

	return 0;
}

static struct platform_driver starfive_eth_plat_driver = {
	.probe  = starfive_eth_plat_probe,
	.remove = starfive_eth_plat_remove,
	.driver = {
		.name = "starfive-eth-plat",
		.pm = &stmmac_pltfr_pm_ops,
		.of_match_table = starfive_eth_plat_match,
	},
};

module_platform_driver(starfive_eth_plat_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("StarFive DWMAC platform driver");

