// SPDX-License-Identifier: GPL-2.0+
/*
 * StarFive DWMAC platform driver
 *
 * Copyright (C) 2021 Emil Renner Berthing <kernel@esmil.dk>
 * Copyright (C) 2022 StarFive Technology Co., Ltd.
 *
 */

#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include "stmmac_platform.h"

#define STARFIVE_DWMAC_PHY_INFT_RGMII		0x1
#define STARFIVE_DWMAC_PHY_INFT_RMII		0x4
#define STARFIVE_DWMAC_PHY_INFT_FIELD		0x7U

#define JH7100_SYSMAIN_REGISTER49_DLYCHAIN	0xc8

struct starfive_dwmac_data {
	unsigned int gtxclk_dlychain;
};

struct starfive_dwmac {
	struct device *dev;
	const struct starfive_dwmac_data *data;
};

static int starfive_dwmac_set_mode(struct plat_stmmacenet_data *plat_dat)
{
	struct starfive_dwmac *dwmac = plat_dat->bsp_priv;
	struct regmap *regmap;
	unsigned int args[2];
	unsigned int mode;
	int err;

	switch (plat_dat->phy_interface) {
	case PHY_INTERFACE_MODE_RMII:
		mode = STARFIVE_DWMAC_PHY_INFT_RMII;
		break;

	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		mode = STARFIVE_DWMAC_PHY_INFT_RGMII;
		break;

	default:
		dev_err(dwmac->dev, "unsupported interface %s\n",
			phy_modes(plat_dat->phy_interface));
		return -EINVAL;
	}

	regmap = syscon_regmap_lookup_by_phandle_args(dwmac->dev->of_node,
						      "starfive,syscon",
						      2, args);
	if (IS_ERR(regmap))
		return dev_err_probe(dwmac->dev, PTR_ERR(regmap), "getting the regmap failed\n");

	/* args[0]:offset  args[1]: shift */
	err = regmap_update_bits(regmap, args[0],
				 STARFIVE_DWMAC_PHY_INFT_FIELD << args[1],
				 mode << args[1]);
	if (err)
		return dev_err_probe(dwmac->dev, err, "error setting phy mode\n");

	if (dwmac->data) {
		err = regmap_write(regmap, JH7100_SYSMAIN_REGISTER49_DLYCHAIN,
				   dwmac->data->gtxclk_dlychain);
		if (err)
			return dev_err_probe(dwmac->dev, err,
					     "error selecting gtxclk delay chain\n");
	}

	return 0;
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

	plat_dat = devm_stmmac_probe_config_dt(pdev, stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return dev_err_probe(&pdev->dev, PTR_ERR(plat_dat),
				     "dt configuration failed\n");

	dwmac = devm_kzalloc(&pdev->dev, sizeof(*dwmac), GFP_KERNEL);
	if (!dwmac)
		return -ENOMEM;

	dwmac->data = device_get_match_data(&pdev->dev);

	plat_dat->clk_tx_i = devm_clk_get_enabled(&pdev->dev, "tx");
	if (IS_ERR(plat_dat->clk_tx_i))
		return dev_err_probe(&pdev->dev, PTR_ERR(plat_dat->clk_tx_i),
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
		plat_dat->set_clk_tx_rate = stmmac_set_clk_tx_rate;

	dwmac->dev = &pdev->dev;
	plat_dat->flags |= STMMAC_FLAG_EN_TX_LPI_CLK_PHY_CAP;
	plat_dat->bsp_priv = dwmac;
	plat_dat->dma_cfg->dche = true;

	err = starfive_dwmac_set_mode(plat_dat);
	if (err)
		return err;

	return stmmac_dvr_probe(&pdev->dev, plat_dat, &stmmac_res);
}

static const struct starfive_dwmac_data jh7100_data = {
	.gtxclk_dlychain = 4,
};

static const struct of_device_id starfive_dwmac_match[] = {
	{ .compatible = "starfive,jh7100-dwmac", .data = &jh7100_data },
	{ .compatible = "starfive,jh7110-dwmac" },
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
