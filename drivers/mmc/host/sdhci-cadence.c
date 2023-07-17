// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 Socionext Inc.
 *   Author: Masahiro Yamada <yamada.masahiro@socionext.com>
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/reset.h>

#include "sdhci-pltfm.h"

/* HRS - Host Register Set (specific to Cadence) */
#define SDHCI_CDNS_HRS04		0x10		/* PHY access port */
#define   SDHCI_CDNS_HRS04_ACK			BIT(26)
#define   SDHCI_CDNS_HRS04_RD			BIT(25)
#define   SDHCI_CDNS_HRS04_WR			BIT(24)
#define   SDHCI_CDNS_HRS04_RDATA		GENMASK(23, 16)
#define   SDHCI_CDNS_HRS04_WDATA		GENMASK(15, 8)
#define   SDHCI_CDNS_HRS04_ADDR			GENMASK(5, 0)

#define SDHCI_CDNS_HRS06		0x18		/* eMMC control */
#define   SDHCI_CDNS_HRS06_TUNE_UP		BIT(15)
#define   SDHCI_CDNS_HRS06_TUNE			GENMASK(13, 8)
#define   SDHCI_CDNS_HRS06_MODE			GENMASK(2, 0)
#define   SDHCI_CDNS_HRS06_MODE_SD		0x0
#define   SDHCI_CDNS_HRS06_MODE_MMC_SDR		0x2
#define   SDHCI_CDNS_HRS06_MODE_MMC_DDR		0x3
#define   SDHCI_CDNS_HRS06_MODE_MMC_HS200	0x4
#define   SDHCI_CDNS_HRS06_MODE_MMC_HS400	0x5
#define   SDHCI_CDNS_HRS06_MODE_MMC_HS400ES	0x6

/* SRS - Slot Register Set (SDHCI-compatible) */
#define SDHCI_CDNS_SRS_BASE		0x200

/* PHY */
#define SDHCI_CDNS_PHY_DLY_SD_HS	0x00
#define SDHCI_CDNS_PHY_DLY_SD_DEFAULT	0x01
#define SDHCI_CDNS_PHY_DLY_UHS_SDR12	0x02
#define SDHCI_CDNS_PHY_DLY_UHS_SDR25	0x03
#define SDHCI_CDNS_PHY_DLY_UHS_SDR50	0x04
#define SDHCI_CDNS_PHY_DLY_UHS_DDR50	0x05
#define SDHCI_CDNS_PHY_DLY_EMMC_LEGACY	0x06
#define SDHCI_CDNS_PHY_DLY_EMMC_SDR	0x07
#define SDHCI_CDNS_PHY_DLY_EMMC_DDR	0x08
#define SDHCI_CDNS_PHY_DLY_SDCLK	0x0b
#define SDHCI_CDNS_PHY_DLY_HSMMC	0x0c
#define SDHCI_CDNS_PHY_DLY_STROBE	0x0d

/*
 * The tuned val register is 6 bit-wide, but not the whole of the range is
 * available.  The range 0-42 seems to be available (then 43 wraps around to 0)
 * but I am not quite sure if it is official.  Use only 0 to 39 for safety.
 */
#define SDHCI_CDNS_MAX_TUNING_LOOP	40

struct sdhci_cdns_phy_param {
	u8 addr;
	u8 data;
};

struct sdhci_cdns_priv {
	void __iomem *hrs_addr;
	void __iomem *ctl_addr;	/* write control */
	spinlock_t wrlock;	/* write lock */
	bool enhanced_strobe;
	void (*priv_writel)(struct sdhci_cdns_priv *priv, u32 val, void __iomem *reg);
	struct reset_control *rst_hw;
	unsigned int nr_phy_params;
	struct sdhci_cdns_phy_param phy_params[];
};

struct sdhci_cdns_phy_cfg {
	const char *property;
	u8 addr;
};

struct sdhci_cdns_drv_data {
	int (*init)(struct platform_device *pdev);
	const struct sdhci_pltfm_data pltfm_data;
};

static const struct sdhci_cdns_phy_cfg sdhci_cdns_phy_cfgs[] = {
	{ "cdns,phy-input-delay-sd-highspeed", SDHCI_CDNS_PHY_DLY_SD_HS, },
	{ "cdns,phy-input-delay-legacy", SDHCI_CDNS_PHY_DLY_SD_DEFAULT, },
	{ "cdns,phy-input-delay-sd-uhs-sdr12", SDHCI_CDNS_PHY_DLY_UHS_SDR12, },
	{ "cdns,phy-input-delay-sd-uhs-sdr25", SDHCI_CDNS_PHY_DLY_UHS_SDR25, },
	{ "cdns,phy-input-delay-sd-uhs-sdr50", SDHCI_CDNS_PHY_DLY_UHS_SDR50, },
	{ "cdns,phy-input-delay-sd-uhs-ddr50", SDHCI_CDNS_PHY_DLY_UHS_DDR50, },
	{ "cdns,phy-input-delay-mmc-highspeed", SDHCI_CDNS_PHY_DLY_EMMC_SDR, },
	{ "cdns,phy-input-delay-mmc-ddr", SDHCI_CDNS_PHY_DLY_EMMC_DDR, },
	{ "cdns,phy-dll-delay-sdclk", SDHCI_CDNS_PHY_DLY_SDCLK, },
	{ "cdns,phy-dll-delay-sdclk-hsmmc", SDHCI_CDNS_PHY_DLY_HSMMC, },
	{ "cdns,phy-dll-delay-strobe", SDHCI_CDNS_PHY_DLY_STROBE, },
};

static inline void cdns_writel(struct sdhci_cdns_priv *priv, u32 val,
			       void __iomem *reg)
{
	writel(val, reg);
}

static int sdhci_cdns_write_phy_reg(struct sdhci_cdns_priv *priv,
				    u8 addr, u8 data)
{
	void __iomem *reg = priv->hrs_addr + SDHCI_CDNS_HRS04;
	u32 tmp;
	int ret;

	ret = readl_poll_timeout(reg, tmp, !(tmp & SDHCI_CDNS_HRS04_ACK),
				 0, 10);
	if (ret)
		return ret;

	tmp = FIELD_PREP(SDHCI_CDNS_HRS04_WDATA, data) |
	      FIELD_PREP(SDHCI_CDNS_HRS04_ADDR, addr);
	priv->priv_writel(priv, tmp, reg);

	tmp |= SDHCI_CDNS_HRS04_WR;
	priv->priv_writel(priv, tmp, reg);

	ret = readl_poll_timeout(reg, tmp, tmp & SDHCI_CDNS_HRS04_ACK, 0, 10);
	if (ret)
		return ret;

	tmp &= ~SDHCI_CDNS_HRS04_WR;
	priv->priv_writel(priv, tmp, reg);

	ret = readl_poll_timeout(reg, tmp, !(tmp & SDHCI_CDNS_HRS04_ACK),
				 0, 10);

	return ret;
}

static unsigned int sdhci_cdns_phy_param_count(struct device_node *np)
{
	unsigned int count = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(sdhci_cdns_phy_cfgs); i++)
		if (of_property_read_bool(np, sdhci_cdns_phy_cfgs[i].property))
			count++;

	return count;
}

static void sdhci_cdns_phy_param_parse(struct device_node *np,
				       struct sdhci_cdns_priv *priv)
{
	struct sdhci_cdns_phy_param *p = priv->phy_params;
	u32 val;
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(sdhci_cdns_phy_cfgs); i++) {
		ret = of_property_read_u32(np, sdhci_cdns_phy_cfgs[i].property,
					   &val);
		if (ret)
			continue;

		p->addr = sdhci_cdns_phy_cfgs[i].addr;
		p->data = val;
		p++;
	}
}

static int sdhci_cdns_phy_init(struct sdhci_cdns_priv *priv)
{
	int ret, i;

	for (i = 0; i < priv->nr_phy_params; i++) {
		ret = sdhci_cdns_write_phy_reg(priv, priv->phy_params[i].addr,
					       priv->phy_params[i].data);
		if (ret)
			return ret;
	}

	return 0;
}

static void *sdhci_cdns_priv(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	return sdhci_pltfm_priv(pltfm_host);
}

static unsigned int sdhci_cdns_get_timeout_clock(struct sdhci_host *host)
{
	/*
	 * Cadence's spec says the Timeout Clock Frequency is the same as the
	 * Base Clock Frequency.
	 */
	return host->max_clk;
}

static void sdhci_cdns_set_emmc_mode(struct sdhci_cdns_priv *priv, u32 mode)
{
	u32 tmp;

	/* The speed mode for eMMC is selected by HRS06 register */
	tmp = readl(priv->hrs_addr + SDHCI_CDNS_HRS06);
	tmp &= ~SDHCI_CDNS_HRS06_MODE;
	tmp |= FIELD_PREP(SDHCI_CDNS_HRS06_MODE, mode);
	priv->priv_writel(priv, tmp, priv->hrs_addr + SDHCI_CDNS_HRS06);
}

static u32 sdhci_cdns_get_emmc_mode(struct sdhci_cdns_priv *priv)
{
	u32 tmp;

	tmp = readl(priv->hrs_addr + SDHCI_CDNS_HRS06);
	return FIELD_GET(SDHCI_CDNS_HRS06_MODE, tmp);
}

static int sdhci_cdns_set_tune_val(struct sdhci_host *host, unsigned int val)
{
	struct sdhci_cdns_priv *priv = sdhci_cdns_priv(host);
	void __iomem *reg = priv->hrs_addr + SDHCI_CDNS_HRS06;
	u32 tmp;
	int i, ret;

	if (WARN_ON(!FIELD_FIT(SDHCI_CDNS_HRS06_TUNE, val)))
		return -EINVAL;

	tmp = readl(reg);
	tmp &= ~SDHCI_CDNS_HRS06_TUNE;
	tmp |= FIELD_PREP(SDHCI_CDNS_HRS06_TUNE, val);

	/*
	 * Workaround for IP errata:
	 * The IP6116 SD/eMMC PHY design has a timing issue on receive data
	 * path. Send tune request twice.
	 */
	for (i = 0; i < 2; i++) {
		tmp |= SDHCI_CDNS_HRS06_TUNE_UP;
		priv->priv_writel(priv, tmp, reg);

		ret = readl_poll_timeout(reg, tmp,
					 !(tmp & SDHCI_CDNS_HRS06_TUNE_UP),
					 0, 1);
		if (ret)
			return ret;
	}

	return 0;
}

/*
 * In SD mode, software must not use the hardware tuning and instead perform
 * an almost identical procedure to eMMC.
 */
static int sdhci_cdns_execute_tuning(struct sdhci_host *host, u32 opcode)
{
	int cur_streak = 0;
	int max_streak = 0;
	int end_of_streak = 0;
	int i;

	/*
	 * Do not execute tuning for UHS_SDR50 or UHS_DDR50.
	 * The delay is set by probe, based on the DT properties.
	 */
	if (host->timing != MMC_TIMING_MMC_HS200 &&
	    host->timing != MMC_TIMING_UHS_SDR104)
		return 0;

	for (i = 0; i < SDHCI_CDNS_MAX_TUNING_LOOP; i++) {
		if (sdhci_cdns_set_tune_val(host, i) ||
		    mmc_send_tuning(host->mmc, opcode, NULL)) { /* bad */
			cur_streak = 0;
		} else { /* good */
			cur_streak++;
			if (cur_streak > max_streak) {
				max_streak = cur_streak;
				end_of_streak = i;
			}
		}
	}

	if (!max_streak) {
		dev_err(mmc_dev(host->mmc), "no tuning point found\n");
		return -EIO;
	}

	return sdhci_cdns_set_tune_val(host, end_of_streak - max_streak / 2);
}

static void sdhci_cdns_set_uhs_signaling(struct sdhci_host *host,
					 unsigned int timing)
{
	struct sdhci_cdns_priv *priv = sdhci_cdns_priv(host);
	u32 mode;

	switch (timing) {
	case MMC_TIMING_MMC_HS:
		mode = SDHCI_CDNS_HRS06_MODE_MMC_SDR;
		break;
	case MMC_TIMING_MMC_DDR52:
		mode = SDHCI_CDNS_HRS06_MODE_MMC_DDR;
		break;
	case MMC_TIMING_MMC_HS200:
		mode = SDHCI_CDNS_HRS06_MODE_MMC_HS200;
		break;
	case MMC_TIMING_MMC_HS400:
		if (priv->enhanced_strobe)
			mode = SDHCI_CDNS_HRS06_MODE_MMC_HS400ES;
		else
			mode = SDHCI_CDNS_HRS06_MODE_MMC_HS400;
		break;
	default:
		mode = SDHCI_CDNS_HRS06_MODE_SD;
		break;
	}

	sdhci_cdns_set_emmc_mode(priv, mode);

	/* For SD, fall back to the default handler */
	if (mode == SDHCI_CDNS_HRS06_MODE_SD)
		sdhci_set_uhs_signaling(host, timing);
}

/* Elba control register bits [6:3] are byte-lane enables */
#define ELBA_BYTE_ENABLE_MASK(x)	((x) << 3)

/*
 * The Pensando Elba SoC explicitly controls byte-lane enabling on writes
 * which includes writes to the HRS registers.  The write lock (wrlock)
 * is used to ensure byte-lane enable, using write control (ctl_addr),
 * occurs before the data write.
 */
static void elba_priv_writel(struct sdhci_cdns_priv *priv, u32 val,
			     void __iomem *reg)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->wrlock, flags);
	writel(GENMASK(7, 3), priv->ctl_addr);
	writel(val, reg);
	spin_unlock_irqrestore(&priv->wrlock, flags);
}

static void elba_write_l(struct sdhci_host *host, u32 val, int reg)
{
	elba_priv_writel(sdhci_cdns_priv(host), val, host->ioaddr + reg);
}

static void elba_write_w(struct sdhci_host *host, u16 val, int reg)
{
	struct sdhci_cdns_priv *priv = sdhci_cdns_priv(host);
	u32 shift = reg & GENMASK(1, 0);
	unsigned long flags;
	u32 byte_enables;

	byte_enables = GENMASK(1, 0) << shift;
	spin_lock_irqsave(&priv->wrlock, flags);
	writel(ELBA_BYTE_ENABLE_MASK(byte_enables), priv->ctl_addr);
	writew(val, host->ioaddr + reg);
	spin_unlock_irqrestore(&priv->wrlock, flags);
}

static void elba_write_b(struct sdhci_host *host, u8 val, int reg)
{
	struct sdhci_cdns_priv *priv = sdhci_cdns_priv(host);
	u32 shift = reg & GENMASK(1, 0);
	unsigned long flags;
	u32 byte_enables;

	byte_enables = BIT(0) << shift;
	spin_lock_irqsave(&priv->wrlock, flags);
	writel(ELBA_BYTE_ENABLE_MASK(byte_enables), priv->ctl_addr);
	writeb(val, host->ioaddr + reg);
	spin_unlock_irqrestore(&priv->wrlock, flags);
}

static const struct sdhci_ops sdhci_elba_ops = {
	.write_l = elba_write_l,
	.write_w = elba_write_w,
	.write_b = elba_write_b,
	.set_clock = sdhci_set_clock,
	.get_timeout_clock = sdhci_cdns_get_timeout_clock,
	.set_bus_width = sdhci_set_bus_width,
	.reset = sdhci_reset,
	.set_uhs_signaling = sdhci_cdns_set_uhs_signaling,
};

static int elba_drv_init(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_cdns_priv *priv = sdhci_cdns_priv(host);
	void __iomem *ioaddr;

	host->mmc->caps |= MMC_CAP_1_8V_DDR | MMC_CAP_8_BIT_DATA;
	spin_lock_init(&priv->wrlock);

	/* Byte-lane control register */
	ioaddr = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(ioaddr))
		return PTR_ERR(ioaddr);

	priv->ctl_addr = ioaddr;
	priv->priv_writel = elba_priv_writel;
	writel(ELBA_BYTE_ENABLE_MASK(0xf), priv->ctl_addr);

	return 0;
}

static const struct sdhci_ops sdhci_cdns_ops = {
	.set_clock = sdhci_set_clock,
	.get_timeout_clock = sdhci_cdns_get_timeout_clock,
	.set_bus_width = sdhci_set_bus_width,
	.reset = sdhci_reset,
	.platform_execute_tuning = sdhci_cdns_execute_tuning,
	.set_uhs_signaling = sdhci_cdns_set_uhs_signaling,
};

static const struct sdhci_cdns_drv_data sdhci_cdns_uniphier_drv_data = {
	.pltfm_data = {
		.ops = &sdhci_cdns_ops,
		.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN,
	},
};

static const struct sdhci_cdns_drv_data sdhci_elba_drv_data = {
	.init = elba_drv_init,
	.pltfm_data = {
		.ops = &sdhci_elba_ops,
	},
};

static const struct sdhci_cdns_drv_data sdhci_cdns_drv_data = {
	.pltfm_data = {
		.ops = &sdhci_cdns_ops,
	},
};

static void sdhci_cdns_hs400_enhanced_strobe(struct mmc_host *mmc,
					     struct mmc_ios *ios)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct sdhci_cdns_priv *priv = sdhci_cdns_priv(host);
	u32 mode;

	priv->enhanced_strobe = ios->enhanced_strobe;

	mode = sdhci_cdns_get_emmc_mode(priv);

	if (mode == SDHCI_CDNS_HRS06_MODE_MMC_HS400 && ios->enhanced_strobe)
		sdhci_cdns_set_emmc_mode(priv,
					 SDHCI_CDNS_HRS06_MODE_MMC_HS400ES);

	if (mode == SDHCI_CDNS_HRS06_MODE_MMC_HS400ES && !ios->enhanced_strobe)
		sdhci_cdns_set_emmc_mode(priv,
					 SDHCI_CDNS_HRS06_MODE_MMC_HS400);
}

static void sdhci_cdns_mmc_hw_reset(struct mmc_host *mmc)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct sdhci_cdns_priv *priv = sdhci_cdns_priv(host);

	dev_dbg(mmc_dev(host->mmc), "emmc hardware reset\n");

	reset_control_assert(priv->rst_hw);
	/* For eMMC, minimum is 1us but give it 3us for good measure */
	udelay(3);

	reset_control_deassert(priv->rst_hw);
	/* For eMMC, minimum is 200us but give it 300us for good measure */
	usleep_range(300, 1000);
}

static int sdhci_cdns_probe(struct platform_device *pdev)
{
	struct sdhci_host *host;
	const struct sdhci_cdns_drv_data *data;
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_cdns_priv *priv;
	struct clk *clk;
	unsigned int nr_phy_params;
	int ret;
	struct device *dev = &pdev->dev;
	static const u16 version = SDHCI_SPEC_400 << SDHCI_SPEC_VER_SHIFT;

	clk = devm_clk_get(dev, NULL);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	ret = clk_prepare_enable(clk);
	if (ret)
		return ret;

	data = of_device_get_match_data(dev);
	if (!data)
		data = &sdhci_cdns_drv_data;

	nr_phy_params = sdhci_cdns_phy_param_count(dev->of_node);
	host = sdhci_pltfm_init(pdev, &data->pltfm_data,
				struct_size(priv, phy_params, nr_phy_params));
	if (IS_ERR(host)) {
		ret = PTR_ERR(host);
		goto disable_clk;
	}

	pltfm_host = sdhci_priv(host);
	pltfm_host->clk = clk;

	priv = sdhci_pltfm_priv(pltfm_host);
	priv->nr_phy_params = nr_phy_params;
	priv->hrs_addr = host->ioaddr;
	priv->enhanced_strobe = false;
	priv->priv_writel = cdns_writel;
	host->ioaddr += SDHCI_CDNS_SRS_BASE;
	host->mmc_host_ops.hs400_enhanced_strobe =
				sdhci_cdns_hs400_enhanced_strobe;
	if (data->init) {
		ret = data->init(pdev);
		if (ret)
			goto free;
	}
	sdhci_enable_v4_mode(host);
	__sdhci_read_caps(host, &version, NULL, NULL);

	sdhci_get_of_property(pdev);

	ret = mmc_of_parse(host->mmc);
	if (ret)
		goto free;

	sdhci_cdns_phy_param_parse(dev->of_node, priv);

	ret = sdhci_cdns_phy_init(priv);
	if (ret)
		goto free;

	if (host->mmc->caps & MMC_CAP_HW_RESET) {
		priv->rst_hw = devm_reset_control_get_optional_exclusive(dev, NULL);
		if (IS_ERR(priv->rst_hw)) {
			ret = dev_err_probe(mmc_dev(host->mmc), PTR_ERR(priv->rst_hw),
					    "reset controller error\n");
			goto free;
		}
		if (priv->rst_hw)
			host->mmc_host_ops.card_hw_reset = sdhci_cdns_mmc_hw_reset;
	}

	ret = sdhci_add_host(host);
	if (ret)
		goto free;

	return 0;
free:
	sdhci_pltfm_free(pdev);
disable_clk:
	clk_disable_unprepare(clk);

	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int sdhci_cdns_resume(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_cdns_priv *priv = sdhci_pltfm_priv(pltfm_host);
	int ret;

	ret = clk_prepare_enable(pltfm_host->clk);
	if (ret)
		return ret;

	ret = sdhci_cdns_phy_init(priv);
	if (ret)
		goto disable_clk;

	ret = sdhci_resume_host(host);
	if (ret)
		goto disable_clk;

	return 0;

disable_clk:
	clk_disable_unprepare(pltfm_host->clk);

	return ret;
}
#endif

static const struct dev_pm_ops sdhci_cdns_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sdhci_pltfm_suspend, sdhci_cdns_resume)
};

static const struct of_device_id sdhci_cdns_match[] = {
	{
		.compatible = "socionext,uniphier-sd4hc",
		.data = &sdhci_cdns_uniphier_drv_data,
	},
	{
		.compatible = "amd,pensando-elba-sd4hc",
		.data = &sdhci_elba_drv_data,
	},
	{ .compatible = "cdns,sd4hc" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sdhci_cdns_match);

static struct platform_driver sdhci_cdns_driver = {
	.driver = {
		.name = "sdhci-cdns",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.pm = &sdhci_cdns_pm_ops,
		.of_match_table = sdhci_cdns_match,
	},
	.probe = sdhci_cdns_probe,
	.remove = sdhci_pltfm_unregister,
};
module_platform_driver(sdhci_cdns_driver);

MODULE_AUTHOR("Masahiro Yamada <yamada.masahiro@socionext.com>");
MODULE_DESCRIPTION("Cadence SD/SDIO/eMMC Host Controller Driver");
MODULE_LICENSE("GPL");
