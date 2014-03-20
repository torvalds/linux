/*
 * Copyright (c) 2013 Linaro Ltd.
 * Copyright (c) 2013 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/mmc/host.h>
#include <linux/mmc/dw_mmc.h>
#include <linux/of_address.h>

#include "dw_mmc.h"
#include "dw_mmc-pltfm.h"

static void dw_mci_k3_set_ios(struct dw_mci *host, struct mmc_ios *ios)
{
	int ret;

	ret = clk_set_rate(host->ciu_clk, ios->clock);
	if (ret)
		dev_warn(host->dev, "failed to set rate %uHz\n", ios->clock);

	host->bus_hz = clk_get_rate(host->ciu_clk);
}

static const struct dw_mci_drv_data k3_drv_data = {
	.set_ios		= dw_mci_k3_set_ios,
};

static const struct of_device_id dw_mci_k3_match[] = {
	{ .compatible = "hisilicon,hi4511-dw-mshc", .data = &k3_drv_data, },
	{},
};
MODULE_DEVICE_TABLE(of, dw_mci_k3_match);

static int dw_mci_k3_probe(struct platform_device *pdev)
{
	const struct dw_mci_drv_data *drv_data;
	const struct of_device_id *match;

	match = of_match_node(dw_mci_k3_match, pdev->dev.of_node);
	drv_data = match->data;

	return dw_mci_pltfm_register(pdev, drv_data);
}

static int dw_mci_k3_suspend(struct device *dev)
{
	struct dw_mci *host = dev_get_drvdata(dev);
	int ret;

	ret = dw_mci_suspend(host);
	if (!ret)
		clk_disable_unprepare(host->ciu_clk);

	return ret;
}

static int dw_mci_k3_resume(struct device *dev)
{
	struct dw_mci *host = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(host->ciu_clk);
	if (ret) {
		dev_err(host->dev, "failed to enable ciu clock\n");
		return ret;
	}

	return dw_mci_resume(host);
}

static SIMPLE_DEV_PM_OPS(dw_mci_k3_pmops, dw_mci_k3_suspend, dw_mci_k3_resume);

static struct platform_driver dw_mci_k3_pltfm_driver = {
	.probe		= dw_mci_k3_probe,
	.remove		= dw_mci_pltfm_remove,
	.driver		= {
		.name		= "dwmmc_k3",
		.of_match_table	= dw_mci_k3_match,
		.pm		= &dw_mci_k3_pmops,
	},
};

module_platform_driver(dw_mci_k3_pltfm_driver);

MODULE_DESCRIPTION("K3 Specific DW-MSHC Driver Extension");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:dwmmc-k3");
