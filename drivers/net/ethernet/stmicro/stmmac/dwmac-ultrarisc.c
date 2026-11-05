// SPDX-License-Identifier: GPL-2.0-only
/*
 * UltraRISC DWMAC platform driver
 *
 * Copyright (C) 2026 UltraRISC Technology (Shanghai) Co., Ltd.
 */

#include <linux/module.h>
#include <linux/platform_device.h>

#include "stmmac_platform.h"

static int ultrarisc_dwmac_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	struct device *dev = &pdev->dev;
	int ret;

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get resources\n");

	plat_dat = devm_stmmac_probe_config_dt(pdev, stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return dev_err_probe(dev, PTR_ERR(plat_dat),
				     "failed to parse DT parameters\n");

	plat_dat->has_internal_tx_delay = true;
	plat_dat->has_internal_rx_delay = true;

	return devm_stmmac_pltfr_probe(pdev, plat_dat, &stmmac_res);
}

static const struct of_device_id ultrarisc_dwmac_match[] = {
	{ .compatible = "ultrarisc,dp1000-gmac" },
	{ }
};
MODULE_DEVICE_TABLE(of, ultrarisc_dwmac_match);

static struct platform_driver ultrarisc_dwmac_driver = {
	.probe = ultrarisc_dwmac_probe,
	.driver = {
		.name = "ultrarisc-dwmac",
		.pm = &stmmac_pltfr_pm_ops,
		.of_match_table = ultrarisc_dwmac_match,
	},
};
module_platform_driver(ultrarisc_dwmac_driver);

MODULE_DESCRIPTION("UltraRISC DWMAC platform driver");
MODULE_LICENSE("GPL");
