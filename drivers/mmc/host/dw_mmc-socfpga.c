/*
 * Altera SoCFPGA Specific Extensions for Synopsys DW Multimedia Card Interface driver
 *
 *  Copyright (C) 2012, Samsung Electronics Co., Ltd.
 *  Copyright (C) 2013 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Taken from dw_mmc-exynos.c
 */
#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/mmc/host.h>
#include <linux/mmc/dw_mmc.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "dw_mmc.h"
#include "dw_mmc-pltfm.h"

#define SYSMGR_SDMMCGRP_CTRL_OFFSET		0x108
#define DRV_CLK_PHASE_SHIFT_SEL_MASK	0x7
#define SYSMGR_SDMMC_CTRL_SET(smplsel, drvsel)		\
	((((drvsel) << 0) & 0x7) | (((smplsel) << 3) & 0x38))

/* SOCFPGA implementation specific driver private data */
struct dw_mci_socfpga_priv_data {
	u8	ciu_div;
	u32	hs_timing;
	struct regmap   *sysreg;
};

static int dw_mci_socfpga_priv_init(struct dw_mci *host)
{
	struct dw_mci_socfpga_priv_data *priv;
	struct device *dev = host->dev;
	int pwr_en;

	priv = devm_kzalloc(host->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(host->dev, "mem alloc failed for private data\n");
		return -ENOMEM;
	}

	host->priv = priv;

	priv->sysreg = syscon_regmap_lookup_by_compatible("altr,sys-mgr");
	if (IS_ERR(priv->sysreg)) {
		dev_err(host->dev, "regmap for altr,sys-mgr lookup failed.\n");
		return PTR_ERR(priv->sysreg);
	}

	if (of_property_read_u32(dev->of_node, "pwr-en", &pwr_en)) {
		dev_info(dev, "couldn't determine pwr-en, assuming pwr-en = 0\n");
		pwr_en = 0;
	}

	/* Set PWREN bit */
	mci_writel(host, PWREN, pwr_en);

	return 0;
}

static int dw_mci_socfpga_setup_clock(struct dw_mci *host)
{
	struct dw_mci_socfpga_priv_data *priv = host->priv;

	clk_disable_unprepare(host->ciu_clk);
	regmap_write(priv->sysreg, SYSMGR_SDMMCGRP_CTRL_OFFSET, priv->hs_timing);
	clk_prepare_enable(host->ciu_clk);

	host->bus_hz /= (priv->ciu_div + 1);
	return 0;
}

static void dw_mci_socfpga_prepare_command(struct dw_mci *host, u32 *cmdr)
{
	struct dw_mci_socfpga_priv_data *priv = host->priv;

	if (priv->hs_timing & DRV_CLK_PHASE_SHIFT_SEL_MASK)
		*cmdr |= SDMMC_CMD_USE_HOLD_REG;
}

static int dw_mci_socfpga_parse_dt(struct dw_mci *host)
{
	struct dw_mci_socfpga_priv_data *priv = host->priv;
	struct device_node *np = host->dev->of_node;
	u32 timing[2];
	u32 div = 0;
	int ret;

	ret = of_property_read_u32(np, "altr,dw-mshc-ciu-div", &div);
	if (ret)
		dev_info(host->dev, "No dw-mshc-ciu-div specified, assuming 1");
	priv->ciu_div = div;

	ret = of_property_read_u32_array(np,
			"altr,dw-mshc-sdr-timing", timing, 2);
	if (ret)
		return ret;

	priv->hs_timing = SYSMGR_SDMMC_CTRL_SET(timing[0], timing[1]);
	return 0;
}

static const struct dw_mci_drv_data socfpga_drv_data = {
	.init			= dw_mci_socfpga_priv_init,
	.setup_clock		= dw_mci_socfpga_setup_clock,
	.prepare_command	= dw_mci_socfpga_prepare_command,
	.parse_dt		= dw_mci_socfpga_parse_dt,
};

static const struct of_device_id dw_mci_socfpga_match[] = {
	{ .compatible = "altr,socfpga-dw-mshc",
			.data = &socfpga_drv_data, },
	{},
};
MODULE_DEVICE_TABLE(of, dw_mci_socfpga_match);

int dw_mci_socfpga_probe(struct platform_device *pdev)
{
	const struct dw_mci_drv_data *drv_data;
	const struct of_device_id *match;

	match = of_match_node(dw_mci_socfpga_match, pdev->dev.of_node);
	drv_data = match->data;
	return dw_mci_pltfm_register(pdev, drv_data);
}

static struct platform_driver dw_mci_socfpga_pltfm_driver = {
	.probe		= dw_mci_socfpga_probe,
	.remove		= __exit_p(dw_mci_pltfm_remove),
	.driver		= {
		.name		= "dwmmc_socfpga",
		.of_match_table	= of_match_ptr(dw_mci_socfpga_match),
		.pm		= &dw_mci_pltfm_pmops,
	},
};

module_platform_driver(dw_mci_socfpga_pltfm_driver);

MODULE_DESCRIPTION("Altera SOCFPGA Specific DW-MSHC Driver Extension");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:dwmmc-socfpga");
