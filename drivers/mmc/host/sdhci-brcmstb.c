// SPDX-License-Identifier: GPL-2.0-only
/*
 * sdhci-brcmstb.c Support for SDHCI on Broadcom BRCMSTB SoC's
 *
 * Copyright (C) 2015 Broadcom Corporation
 */

#include <linux/io.h>
#include <linux/mmc/host.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/bitops.h>

#include "sdhci-pltfm.h"

#define SDHCI_VENDOR 0x78
#define  SDHCI_VENDOR_ENHANCED_STRB 0x1

#define BRCMSTB_PRIV_FLAGS_NO_64BIT		BIT(0)
#define BRCMSTB_PRIV_FLAGS_BROKEN_TIMEOUT	BIT(1)

struct sdhci_brcmstb_priv {
	void __iomem *cfg_regs;
};

struct brcmstb_match_priv {
	void (*hs400es)(struct mmc_host *mmc, struct mmc_ios *ios);
	unsigned int flags;
};

static void sdhci_brcmstb_hs400es(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct sdhci_host *host = mmc_priv(mmc);

	u32 reg;

	dev_dbg(mmc_dev(mmc), "%s(): Setting HS400-Enhanced-Strobe mode\n",
		__func__);
	reg = readl(host->ioaddr + SDHCI_VENDOR);
	if (ios->enhanced_strobe)
		reg |= SDHCI_VENDOR_ENHANCED_STRB;
	else
		reg &= ~SDHCI_VENDOR_ENHANCED_STRB;
	writel(reg, host->ioaddr + SDHCI_VENDOR);
}

static const struct sdhci_ops sdhci_brcmstb_ops = {
	.set_clock = sdhci_set_clock,
	.set_bus_width = sdhci_set_bus_width,
	.reset = sdhci_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
};

static const struct sdhci_pltfm_data sdhci_brcmstb_pdata = {
	.ops = &sdhci_brcmstb_ops,
};

static const struct brcmstb_match_priv match_priv_7425 = {
	.flags = BRCMSTB_PRIV_FLAGS_NO_64BIT |
	BRCMSTB_PRIV_FLAGS_BROKEN_TIMEOUT,
};

static const struct brcmstb_match_priv match_priv_7445 = {
	.flags = BRCMSTB_PRIV_FLAGS_BROKEN_TIMEOUT,
};

static const struct brcmstb_match_priv match_priv_7216 = {
	.hs400es = sdhci_brcmstb_hs400es,
};

static const struct of_device_id sdhci_brcm_of_match[] = {
	{ .compatible = "brcm,bcm7425-sdhci", .data = &match_priv_7425 },
	{ .compatible = "brcm,bcm7445-sdhci", .data = &match_priv_7445 },
	{ .compatible = "brcm,bcm7216-sdhci", .data = &match_priv_7216 },
	{},
};

static int sdhci_brcmstb_probe(struct platform_device *pdev)
{
	const struct brcmstb_match_priv *match_priv;
	struct sdhci_pltfm_host *pltfm_host;
	const struct of_device_id *match;
	struct sdhci_brcmstb_priv *priv;
	struct sdhci_host *host;
	struct resource *iomem;
	struct clk *clk;
	int res;

	match = of_match_node(sdhci_brcm_of_match, pdev->dev.of_node);
	match_priv = match->data;

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		if (PTR_ERR(clk) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_err(&pdev->dev, "Clock not found in Device Tree\n");
		clk = NULL;
	}
	res = clk_prepare_enable(clk);
	if (res)
		return res;

	host = sdhci_pltfm_init(pdev, &sdhci_brcmstb_pdata,
				sizeof(struct sdhci_brcmstb_priv));
	if (IS_ERR(host)) {
		res = PTR_ERR(host);
		goto err_clk;
	}

	pltfm_host = sdhci_priv(host);
	priv = sdhci_pltfm_priv(pltfm_host);

	/* Map in the non-standard CFG registers */
	iomem = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	priv->cfg_regs = devm_ioremap_resource(&pdev->dev, iomem);
	if (IS_ERR(priv->cfg_regs)) {
		res = PTR_ERR(priv->cfg_regs);
		goto err;
	}

	sdhci_get_of_property(pdev);
	res = mmc_of_parse(host->mmc);
	if (res)
		goto err;

	/*
	 * If the chip has enhanced strobe and it's enabled, add
	 * callback
	 */
	if (match_priv->hs400es &&
	    (host->mmc->caps2 & MMC_CAP2_HS400_ES))
		host->mmc_host_ops.hs400_enhanced_strobe = match_priv->hs400es;

	/*
	 * Supply the existing CAPS, but clear the UHS modes. This
	 * will allow these modes to be specified by device tree
	 * properties through mmc_of_parse().
	 */
	host->caps = sdhci_readl(host, SDHCI_CAPABILITIES);
	if (match_priv->flags & BRCMSTB_PRIV_FLAGS_NO_64BIT)
		host->caps &= ~SDHCI_CAN_64BIT;
	host->caps1 = sdhci_readl(host, SDHCI_CAPABILITIES_1);
	host->caps1 &= ~(SDHCI_SUPPORT_SDR50 | SDHCI_SUPPORT_SDR104 |
			SDHCI_SUPPORT_DDR50);
	host->quirks |= SDHCI_QUIRK_MISSING_CAPS;

	if (match_priv->flags & BRCMSTB_PRIV_FLAGS_BROKEN_TIMEOUT)
		host->quirks |= SDHCI_QUIRK_BROKEN_TIMEOUT_VAL;

	res = sdhci_add_host(host);
	if (res)
		goto err;

	pltfm_host->clk = clk;
	return res;

err:
	sdhci_pltfm_free(pdev);
err_clk:
	clk_disable_unprepare(clk);
	return res;
}

MODULE_DEVICE_TABLE(of, sdhci_brcm_of_match);

static struct platform_driver sdhci_brcmstb_driver = {
	.driver		= {
		.name	= "sdhci-brcmstb",
		.pm	= &sdhci_pltfm_pmops,
		.of_match_table = of_match_ptr(sdhci_brcm_of_match),
	},
	.probe		= sdhci_brcmstb_probe,
	.remove		= sdhci_pltfm_unregister,
};

module_platform_driver(sdhci_brcmstb_driver);

MODULE_DESCRIPTION("SDHCI driver for Broadcom BRCMSTB SoCs");
MODULE_AUTHOR("Broadcom");
MODULE_LICENSE("GPL v2");
