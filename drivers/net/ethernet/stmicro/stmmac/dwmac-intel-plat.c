// SPDX-License-Identifier: GPL-2.0
/* Intel DWMAC platform driver
 *
 * Copyright(C) 2020 Intel Corporation
 */

#include <linux/ethtool.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/stmmac.h>

#include "dwmac4.h"
#include "stmmac.h"
#include "stmmac_platform.h"

struct intel_dwmac {
	struct device *dev;
	struct clk *tx_clk;
	const struct intel_dwmac_data *data;
};

struct intel_dwmac_data {
	void (*fix_mac_speed)(void *priv, unsigned int speed);
	unsigned long ptp_ref_clk_rate;
	unsigned long tx_clk_rate;
	bool tx_clk_en;
};

static void kmb_eth_fix_mac_speed(void *priv, unsigned int speed)
{
	struct intel_dwmac *dwmac = priv;
	unsigned long rate;
	int ret;

	rate = clk_get_rate(dwmac->tx_clk);

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
		dev_err(dwmac->dev, "Invalid speed\n");
		break;
	}

	ret = clk_set_rate(dwmac->tx_clk, rate);
	if (ret)
		dev_err(dwmac->dev, "Failed to configure tx clock rate\n");
}

static const struct intel_dwmac_data kmb_data = {
	.fix_mac_speed = kmb_eth_fix_mac_speed,
	.ptp_ref_clk_rate = 200000000,
	.tx_clk_rate = 125000000,
	.tx_clk_en = true,
};

static const struct of_device_id intel_eth_plat_match[] = {
	{ .compatible = "intel,keembay-dwmac", .data = &kmb_data },
	{ }
};
MODULE_DEVICE_TABLE(of, intel_eth_plat_match);

static int intel_eth_plat_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	const struct of_device_id *match;
	struct intel_dwmac *dwmac;
	unsigned long rate;
	int ret;

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return ret;

	plat_dat = stmmac_probe_config_dt(pdev, stmmac_res.mac);
	if (IS_ERR(plat_dat)) {
		dev_err(&pdev->dev, "dt configuration failed\n");
		return PTR_ERR(plat_dat);
	}

	dwmac = devm_kzalloc(&pdev->dev, sizeof(*dwmac), GFP_KERNEL);
	if (!dwmac) {
		ret = -ENOMEM;
		goto err_remove_config_dt;
	}

	dwmac->dev = &pdev->dev;
	dwmac->tx_clk = NULL;

	match = of_match_device(intel_eth_plat_match, &pdev->dev);
	if (match && match->data) {
		dwmac->data = (const struct intel_dwmac_data *)match->data;

		if (dwmac->data->fix_mac_speed)
			plat_dat->fix_mac_speed = dwmac->data->fix_mac_speed;

		/* Enable TX clock */
		if (dwmac->data->tx_clk_en) {
			dwmac->tx_clk = devm_clk_get(&pdev->dev, "tx_clk");
			if (IS_ERR(dwmac->tx_clk)) {
				ret = PTR_ERR(dwmac->tx_clk);
				goto err_remove_config_dt;
			}

			clk_prepare_enable(dwmac->tx_clk);

			/* Check and configure TX clock rate */
			rate = clk_get_rate(dwmac->tx_clk);
			if (dwmac->data->tx_clk_rate &&
			    rate != dwmac->data->tx_clk_rate) {
				rate = dwmac->data->tx_clk_rate;
				ret = clk_set_rate(dwmac->tx_clk, rate);
				if (ret) {
					dev_err(&pdev->dev,
						"Failed to set tx_clk\n");
					goto err_remove_config_dt;
				}
			}
		}

		/* Check and configure PTP ref clock rate */
		rate = clk_get_rate(plat_dat->clk_ptp_ref);
		if (dwmac->data->ptp_ref_clk_rate &&
		    rate != dwmac->data->ptp_ref_clk_rate) {
			rate = dwmac->data->ptp_ref_clk_rate;
			ret = clk_set_rate(plat_dat->clk_ptp_ref, rate);
			if (ret) {
				dev_err(&pdev->dev,
					"Failed to set clk_ptp_ref\n");
				goto err_remove_config_dt;
			}
		}
	}

	plat_dat->bsp_priv = dwmac;
	plat_dat->eee_usecs_rate = plat_dat->clk_ptp_rate;

	if (plat_dat->eee_usecs_rate > 0) {
		u32 tx_lpi_usec;

		tx_lpi_usec = (plat_dat->eee_usecs_rate / 1000000) - 1;
		writel(tx_lpi_usec, stmmac_res.addr + GMAC_1US_TIC_COUNTER);
	}

	ret = stmmac_dvr_probe(&pdev->dev, plat_dat, &stmmac_res);
	if (ret) {
		clk_disable_unprepare(dwmac->tx_clk);
		goto err_remove_config_dt;
	}

	return 0;

err_remove_config_dt:
	stmmac_remove_config_dt(pdev, plat_dat);

	return ret;
}

static void intel_eth_plat_remove(struct platform_device *pdev)
{
	struct intel_dwmac *dwmac = get_stmmac_bsp_priv(&pdev->dev);

	stmmac_pltfr_remove(pdev);
	clk_disable_unprepare(dwmac->tx_clk);
}

static struct platform_driver intel_eth_plat_driver = {
	.probe  = intel_eth_plat_probe,
	.remove_new = intel_eth_plat_remove,
	.driver = {
		.name		= "intel-eth-plat",
		.pm		= &stmmac_pltfr_pm_ops,
		.of_match_table = intel_eth_plat_match,
	},
};
module_platform_driver(intel_eth_plat_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel DWMAC platform driver");
