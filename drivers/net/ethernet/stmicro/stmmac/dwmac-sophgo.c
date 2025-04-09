// SPDX-License-Identifier: GPL-2.0+
/*
 * Sophgo DWMAC platform driver
 *
 * Copyright (C) 2024 Inochi Amaoto <inochiama@gmail.com>
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>

#include "stmmac_platform.h"

static int sophgo_sg2044_dwmac_init(struct platform_device *pdev,
				    struct plat_stmmacenet_data *plat_dat,
				    struct stmmac_resources *stmmac_res)
{
	plat_dat->clk_tx_i = devm_clk_get_enabled(&pdev->dev, "tx");
	if (IS_ERR(plat_dat->clk_tx_i))
		return dev_err_probe(&pdev->dev, PTR_ERR(plat_dat->clk_tx_i),
				     "failed to get tx clock\n");

	plat_dat->flags |= STMMAC_FLAG_SPH_DISABLE;
	plat_dat->set_clk_tx_rate = stmmac_set_clk_tx_rate;
	plat_dat->multicast_filter_bins = 0;
	plat_dat->unicast_filter_entries = 1;

	return 0;
}

static int sophgo_dwmac_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	struct device *dev = &pdev->dev;
	int ret;

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to get platform resources\n");

	plat_dat = devm_stmmac_probe_config_dt(pdev, stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return dev_err_probe(dev, PTR_ERR(plat_dat),
				     "failed to parse DT parameters\n");

	ret = sophgo_sg2044_dwmac_init(pdev, plat_dat, &stmmac_res);
	if (ret)
		return ret;

	return stmmac_dvr_probe(dev, plat_dat, &stmmac_res);
}

static const struct of_device_id sophgo_dwmac_match[] = {
	{ .compatible = "sophgo,sg2044-dwmac" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sophgo_dwmac_match);

static struct platform_driver sophgo_dwmac_driver = {
	.probe  = sophgo_dwmac_probe,
	.remove = stmmac_pltfr_remove,
	.driver = {
		.name = "sophgo-dwmac",
		.pm = &stmmac_pltfr_pm_ops,
		.of_match_table = sophgo_dwmac_match,
	},
};
module_platform_driver(sophgo_dwmac_driver);

MODULE_AUTHOR("Inochi Amaoto <inochiama@gmail.com>");
MODULE_DESCRIPTION("Sophgo DWMAC platform driver");
MODULE_LICENSE("GPL");
