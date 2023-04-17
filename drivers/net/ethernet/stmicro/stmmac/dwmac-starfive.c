// SPDX-License-Identifier: GPL-2.0+
/*
 * StarFive DWMAC platform driver
 *
 * Copyright (C) 2021 Emil Renner Berthing <kernel@esmil.dk>
 * Copyright (C) 2022 StarFive Technology Co., Ltd.
 *
 */

#include <linux/mfd/syscon.h>
#include <linux/of_device.h>
#include <linux/regmap.h>

#include "stmmac_platform.h"

struct starfive_dwmac {
	struct device *dev;
	struct clk *clk_tx;
};

static void starfive_dwmac_fix_mac_speed(void *priv, unsigned int speed)
{
	struct starfive_dwmac *dwmac = priv;
	unsigned long rate;
	int err;

	rate = clk_get_rate(dwmac->clk_tx);

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
		break;
	}

	err = clk_set_rate(dwmac->clk_tx, rate);
	if (err)
		dev_err(dwmac->dev, "failed to set tx rate %lu\n", rate);
}

static int starfive_dwmac_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	struct starfive_dwmac *dwmac;
	struct clk *clk_gtx;
	int err;

	err = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (err)
		return dev_err_probe(&pdev->dev, err,
				     "failed to get resources\n");

	plat_dat = stmmac_probe_config_dt(pdev, stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return dev_err_probe(&pdev->dev, PTR_ERR(plat_dat),
				     "dt configuration failed\n");

	dwmac = devm_kzalloc(&pdev->dev, sizeof(*dwmac), GFP_KERNEL);
	if (!dwmac)
		return -ENOMEM;

	dwmac->clk_tx = devm_clk_get_enabled(&pdev->dev, "tx");
	if (IS_ERR(dwmac->clk_tx))
		return dev_err_probe(&pdev->dev, PTR_ERR(dwmac->clk_tx),
				     "error getting tx clock\n");

	clk_gtx = devm_clk_get_enabled(&pdev->dev, "gtx");
	if (IS_ERR(clk_gtx))
		return dev_err_probe(&pdev->dev, PTR_ERR(clk_gtx),
				     "error getting gtx clock\n");

	/* Generally, the rgmii_tx clock is provided by the internal clock,
	 * which needs to match the corresponding clock frequency according
	 * to different speeds. If the rgmii_tx clock is provided by the
	 * external rgmii_rxin, there is no need to configure the clock
	 * internally, because rgmii_rxin will be adaptively adjusted.
	 */
	if (!device_property_read_bool(&pdev->dev, "starfive,tx-use-rgmii-clk"))
		plat_dat->fix_mac_speed = starfive_dwmac_fix_mac_speed;

	dwmac->dev = &pdev->dev;
	plat_dat->bsp_priv = dwmac;
	plat_dat->dma_cfg->dche = true;

	err = stmmac_dvr_probe(&pdev->dev, plat_dat, &stmmac_res);
	if (err) {
		stmmac_remove_config_dt(pdev, plat_dat);
		return err;
	}

	return 0;
}

static const struct of_device_id starfive_dwmac_match[] = {
	{ .compatible = "starfive,jh7110-dwmac"	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, starfive_dwmac_match);

static struct platform_driver starfive_dwmac_driver = {
	.probe  = starfive_dwmac_probe,
	.remove = stmmac_pltfr_remove,
	.driver = {
		.name = "starfive-dwmac",
		.pm = &stmmac_pltfr_pm_ops,
		.of_match_table = starfive_dwmac_match,
	},
};
module_platform_driver(starfive_dwmac_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("StarFive DWMAC platform driver");
MODULE_AUTHOR("Emil Renner Berthing <kernel@esmil.dk>");
MODULE_AUTHOR("Samin Guo <samin.guo@starfivetech.com>");
