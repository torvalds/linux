/*
 * Support for SDHCI on STMicroelectronics SoCs
 *
 * Copyright (C) 2014 STMicroelectronics Ltd
 * Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
 * Contributors: Peter Griffin <peter.griffin@linaro.org>
 *
 * Based on sdhci-cns3xxx.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/mmc/host.h>

#include "sdhci-pltfm.h"

static u32 sdhci_st_readl(struct sdhci_host *host, int reg)
{
	u32 ret;

	switch (reg) {
	case SDHCI_CAPABILITIES:
		ret = readl_relaxed(host->ioaddr + reg);
		/* Support 3.3V and 1.8V */
		ret &= ~SDHCI_CAN_VDD_300;
		break;
	default:
		ret = readl_relaxed(host->ioaddr + reg);
	}
	return ret;
}

static const struct sdhci_ops sdhci_st_ops = {
	.get_max_clock = sdhci_pltfm_clk_get_max_clock,
	.set_clock = sdhci_set_clock,
	.set_bus_width = sdhci_set_bus_width,
	.read_l = sdhci_st_readl,
	.reset = sdhci_reset,
};

static const struct sdhci_pltfm_data sdhci_st_pdata = {
	.ops = &sdhci_st_ops,
	.quirks = SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC |
	    SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN,
};


static int sdhci_st_probe(struct platform_device *pdev)
{
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	struct clk *clk;
	int ret = 0;
	u16 host_version;

	clk =  devm_clk_get(&pdev->dev, "mmc");
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "Peripheral clk not found\n");
		return PTR_ERR(clk);
	}

	host = sdhci_pltfm_init(pdev, &sdhci_st_pdata, 0);
	if (IS_ERR(host)) {
		dev_err(&pdev->dev, "Failed sdhci_pltfm_init\n");
		return PTR_ERR(host);
	}

	ret = mmc_of_parse(host->mmc);
	if (ret) {
		dev_err(&pdev->dev, "Failed mmc_of_parse\n");
		goto err_of;
	}

	clk_prepare_enable(clk);

	pltfm_host = sdhci_priv(host);
	pltfm_host->clk = clk;

	ret = sdhci_add_host(host);
	if (ret) {
		dev_err(&pdev->dev, "Failed sdhci_add_host\n");
		goto err_out;
	}

	platform_set_drvdata(pdev, host);

	host_version = readw_relaxed((host->ioaddr + SDHCI_HOST_VERSION));

	dev_info(&pdev->dev, "SDHCI ST Initialised: Host Version: 0x%x Vendor Version 0x%x\n",
		((host_version & SDHCI_SPEC_VER_MASK) >> SDHCI_SPEC_VER_SHIFT),
		((host_version & SDHCI_VENDOR_VER_MASK) >>
		SDHCI_VENDOR_VER_SHIFT));

	return 0;

err_out:
	clk_disable_unprepare(clk);
err_of:
	sdhci_pltfm_free(pdev);

	return ret;
}

static int sdhci_st_remove(struct platform_device *pdev)
{
	return sdhci_pltfm_unregister(pdev);
}

#ifdef CONFIG_PM_SLEEP
static int sdhci_st_suspend(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	int ret = sdhci_suspend_host(host);

	if (ret)
		goto out;

	clk_disable_unprepare(pltfm_host->clk);
out:
	return ret;
}

static int sdhci_st_resume(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	clk_prepare_enable(pltfm_host->clk);

	return sdhci_resume_host(host);
}
#endif

static SIMPLE_DEV_PM_OPS(sdhci_st_pmops, sdhci_st_suspend, sdhci_st_resume);

static const struct of_device_id st_sdhci_match[] = {
	{ .compatible = "st,sdhci" },
	{},
};

MODULE_DEVICE_TABLE(of, st_sdhci_match);

static struct platform_driver sdhci_st_driver = {
	.probe = sdhci_st_probe,
	.remove = sdhci_st_remove,
	.driver = {
		   .name = "sdhci-st",
		   .pm = &sdhci_st_pmops,
		   .of_match_table = of_match_ptr(st_sdhci_match),
		  },
};

module_platform_driver(sdhci_st_driver);

MODULE_DESCRIPTION("SDHCI driver for STMicroelectronics SoCs");
MODULE_AUTHOR("Giuseppe Cavallaro <peppe.cavallaro@st.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:st-sdhci");
