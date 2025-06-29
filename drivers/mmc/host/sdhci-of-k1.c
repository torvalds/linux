// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023-2025 SpacemiT (Hangzhou) Technology Co. Ltd
 * Copyright (c) 2025 Yixun Lan <dlan@gentoo.org>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/init.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "sdhci.h"
#include "sdhci-pltfm.h"

#define SDHC_MMC_CTRL_REG		0x114
#define  MISC_INT_EN			BIT(1)
#define  MISC_INT			BIT(2)
#define  ENHANCE_STROBE_EN		BIT(8)
#define  MMC_HS400			BIT(9)
#define  MMC_HS200			BIT(10)
#define  MMC_CARD_MODE			BIT(12)

#define SDHC_TX_CFG_REG			0x11C
#define  TX_INT_CLK_SEL			BIT(30)
#define  TX_MUX_SEL			BIT(31)

#define SDHC_PHY_CTRL_REG		0x160
#define  PHY_FUNC_EN			BIT(0)
#define  PHY_PLL_LOCK			BIT(1)
#define  HOST_LEGACY_MODE		BIT(31)

#define SDHC_PHY_FUNC_REG		0x164
#define  PHY_TEST_EN			BIT(7)
#define  HS200_USE_RFIFO		BIT(15)

#define SDHC_PHY_DLLCFG			0x168
#define  DLL_PREDLY_NUM			GENMASK(3, 2)
#define  DLL_FULLDLY_RANGE		GENMASK(5, 4)
#define  DLL_VREG_CTRL			GENMASK(7, 6)
#define  DLL_ENABLE			BIT(31)

#define SDHC_PHY_DLLCFG1		0x16C
#define  DLL_REG1_CTRL			GENMASK(7, 0)
#define  DLL_REG2_CTRL			GENMASK(15, 8)
#define  DLL_REG3_CTRL			GENMASK(23, 16)
#define  DLL_REG4_CTRL			GENMASK(31, 24)

#define SDHC_PHY_DLLSTS			0x170
#define  DLL_LOCK_STATE			BIT(0)

#define SDHC_PHY_PADCFG_REG		0x178
#define  PHY_DRIVE_SEL			GENMASK(2, 0)
#define  RX_BIAS_CTRL			BIT(5)

struct spacemit_sdhci_host {
	struct clk *clk_core;
	struct clk *clk_io;
};

/* All helper functions will update clr/set while preserve rest bits */
static inline void spacemit_sdhci_setbits(struct sdhci_host *host, u32 val, int reg)
{
	sdhci_writel(host, sdhci_readl(host, reg) | val, reg);
}

static inline void spacemit_sdhci_clrbits(struct sdhci_host *host, u32 val, int reg)
{
	sdhci_writel(host, sdhci_readl(host, reg) & ~val, reg);
}

static inline void spacemit_sdhci_clrsetbits(struct sdhci_host *host, u32 clr, u32 set, int reg)
{
	u32 val = sdhci_readl(host, reg);

	val = (val & ~clr) | set;
	sdhci_writel(host, val, reg);
}

static void spacemit_sdhci_reset(struct sdhci_host *host, u8 mask)
{
	sdhci_reset(host, mask);

	if (mask != SDHCI_RESET_ALL)
		return;

	spacemit_sdhci_setbits(host, PHY_FUNC_EN | PHY_PLL_LOCK, SDHC_PHY_CTRL_REG);

	spacemit_sdhci_clrsetbits(host, PHY_DRIVE_SEL,
				  RX_BIAS_CTRL | FIELD_PREP(PHY_DRIVE_SEL, 4),
				  SDHC_PHY_PADCFG_REG);

	if (!(host->mmc->caps2 & MMC_CAP2_NO_MMC))
		spacemit_sdhci_setbits(host, MMC_CARD_MODE, SDHC_MMC_CTRL_REG);
}

static void spacemit_sdhci_set_uhs_signaling(struct sdhci_host *host, unsigned int timing)
{
	if (timing == MMC_TIMING_MMC_HS200)
		spacemit_sdhci_setbits(host, MMC_HS200, SDHC_MMC_CTRL_REG);

	if (timing == MMC_TIMING_MMC_HS400)
		spacemit_sdhci_setbits(host, MMC_HS400, SDHC_MMC_CTRL_REG);

	sdhci_set_uhs_signaling(host, timing);

	if (!(host->mmc->caps2 & MMC_CAP2_NO_SDIO))
		spacemit_sdhci_setbits(host, SDHCI_CTRL_VDD_180, SDHCI_HOST_CONTROL2);
}

static void spacemit_sdhci_set_clock(struct sdhci_host *host, unsigned int clock)
{
	struct mmc_host *mmc = host->mmc;

	if (mmc->ios.timing <= MMC_TIMING_UHS_SDR50)
		spacemit_sdhci_setbits(host, TX_INT_CLK_SEL, SDHC_TX_CFG_REG);
	else
		spacemit_sdhci_clrbits(host, TX_INT_CLK_SEL, SDHC_TX_CFG_REG);

	sdhci_set_clock(host, clock);
};

static void spacemit_sdhci_phy_dll_init(struct sdhci_host *host)
{
	u32 state;
	int ret;

	spacemit_sdhci_clrsetbits(host, DLL_PREDLY_NUM | DLL_FULLDLY_RANGE | DLL_VREG_CTRL,
				  FIELD_PREP(DLL_PREDLY_NUM, 1) |
				  FIELD_PREP(DLL_FULLDLY_RANGE, 1) |
				  FIELD_PREP(DLL_VREG_CTRL, 1),
				  SDHC_PHY_DLLCFG);

	spacemit_sdhci_clrsetbits(host, DLL_REG1_CTRL,
				  FIELD_PREP(DLL_REG1_CTRL, 0x92),
				  SDHC_PHY_DLLCFG1);

	spacemit_sdhci_setbits(host, DLL_ENABLE, SDHC_PHY_DLLCFG);

	ret = readl_poll_timeout(host->ioaddr + SDHC_PHY_DLLSTS, state,
				 state & DLL_LOCK_STATE, 2, 100);
	if (ret == -ETIMEDOUT)
		dev_warn(mmc_dev(host->mmc), "fail to lock phy dll in 100us!\n");
}

static void spacemit_sdhci_hs400_enhanced_strobe(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct sdhci_host *host = mmc_priv(mmc);

	if (!ios->enhanced_strobe) {
		spacemit_sdhci_clrbits(host, ENHANCE_STROBE_EN, SDHC_MMC_CTRL_REG);
		return;
	}

	spacemit_sdhci_setbits(host, ENHANCE_STROBE_EN, SDHC_MMC_CTRL_REG);
	spacemit_sdhci_phy_dll_init(host);
}

static unsigned int spacemit_sdhci_clk_get_max_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	return clk_get_rate(pltfm_host->clk);
}

static int spacemit_sdhci_pre_select_hs400(struct mmc_host *mmc)
{
	struct sdhci_host *host = mmc_priv(mmc);

	spacemit_sdhci_setbits(host, MMC_HS400, SDHC_MMC_CTRL_REG);
	host->mmc->caps |= MMC_CAP_WAIT_WHILE_BUSY;

	return 0;
}

static void spacemit_sdhci_post_select_hs400(struct mmc_host *mmc)
{
	struct sdhci_host *host = mmc_priv(mmc);

	spacemit_sdhci_phy_dll_init(host);
	host->mmc->caps &= ~MMC_CAP_WAIT_WHILE_BUSY;
}

static void spacemit_sdhci_pre_hs400_to_hs200(struct mmc_host *mmc)
{
	struct sdhci_host *host = mmc_priv(mmc);

	spacemit_sdhci_clrbits(host, PHY_FUNC_EN | PHY_PLL_LOCK, SDHC_PHY_CTRL_REG);
	spacemit_sdhci_clrbits(host, MMC_HS400 | MMC_HS200 | ENHANCE_STROBE_EN, SDHC_MMC_CTRL_REG);
	spacemit_sdhci_clrbits(host, HS200_USE_RFIFO, SDHC_PHY_FUNC_REG);

	udelay(5);

	spacemit_sdhci_setbits(host, PHY_FUNC_EN | PHY_PLL_LOCK, SDHC_PHY_CTRL_REG);
}

static inline int spacemit_sdhci_get_clocks(struct device *dev,
					    struct sdhci_pltfm_host *pltfm_host)
{
	struct spacemit_sdhci_host *sdhst = sdhci_pltfm_priv(pltfm_host);

	sdhst->clk_core = devm_clk_get_enabled(dev, "core");
	if (IS_ERR(sdhst->clk_core))
		return -EINVAL;

	sdhst->clk_io = devm_clk_get_enabled(dev, "io");
	if (IS_ERR(sdhst->clk_io))
		return -EINVAL;

	pltfm_host->clk = sdhst->clk_io;

	return 0;
}

static const struct sdhci_ops spacemit_sdhci_ops = {
	.get_max_clock		= spacemit_sdhci_clk_get_max_clock,
	.reset			= spacemit_sdhci_reset,
	.set_bus_width		= sdhci_set_bus_width,
	.set_clock		= spacemit_sdhci_set_clock,
	.set_uhs_signaling	= spacemit_sdhci_set_uhs_signaling,
};

static const struct sdhci_pltfm_data spacemit_sdhci_k1_pdata = {
	.ops = &spacemit_sdhci_ops,
	.quirks = SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK |
		  SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC |
		  SDHCI_QUIRK_32BIT_ADMA_SIZE |
		  SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN |
		  SDHCI_QUIRK_BROKEN_CARD_DETECTION |
		  SDHCI_QUIRK_BROKEN_TIMEOUT_VAL,
	.quirks2 = SDHCI_QUIRK2_BROKEN_64_BIT_DMA |
		   SDHCI_QUIRK2_PRESET_VALUE_BROKEN,
};

static const struct of_device_id spacemit_sdhci_of_match[] = {
	{ .compatible = "spacemit,k1-sdhci" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, spacemit_sdhci_of_match);

static int spacemit_sdhci_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spacemit_sdhci_host *sdhst;
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_host *host;
	struct mmc_host_ops *mops;
	int ret;

	host = sdhci_pltfm_init(pdev, &spacemit_sdhci_k1_pdata, sizeof(*sdhst));
	if (IS_ERR(host))
		return PTR_ERR(host);

	pltfm_host = sdhci_priv(host);

	ret = mmc_of_parse(host->mmc);
	if (ret)
		goto err_pltfm;

	sdhci_get_of_property(pdev);

	if (!(host->mmc->caps2 & MMC_CAP2_NO_MMC)) {
		mops = &host->mmc_host_ops;
		mops->hs400_prepare_ddr	= spacemit_sdhci_pre_select_hs400;
		mops->hs400_complete	= spacemit_sdhci_post_select_hs400;
		mops->hs400_downgrade	= spacemit_sdhci_pre_hs400_to_hs200;
		mops->hs400_enhanced_strobe = spacemit_sdhci_hs400_enhanced_strobe;
	}

	host->mmc->caps |= MMC_CAP_NEED_RSP_BUSY;

	if (spacemit_sdhci_get_clocks(dev, pltfm_host))
		goto err_pltfm;

	ret = sdhci_add_host(host);
	if (ret)
		goto err_pltfm;

	return 0;

err_pltfm:
	sdhci_pltfm_free(pdev);
	return ret;
}

static struct platform_driver spacemit_sdhci_driver = {
	.driver		= {
		.name	= "sdhci-spacemit",
		.of_match_table = spacemit_sdhci_of_match,
	},
	.probe		= spacemit_sdhci_probe,
	.remove		= sdhci_pltfm_remove,
};
module_platform_driver(spacemit_sdhci_driver);

MODULE_DESCRIPTION("SpacemiT SDHCI platform driver");
MODULE_LICENSE("GPL");
