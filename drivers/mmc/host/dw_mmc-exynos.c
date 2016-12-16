/*
 * Exynos Specific Extensions for Synopsys DW Multimedia Card Interface driver
 *
 * Copyright (C) 2012, Samsung Electronics Co., Ltd.
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
#include <linux/mmc/mmc.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>

#include "dw_mmc.h"
#include "dw_mmc-pltfm.h"
#include "dw_mmc-exynos.h"

/* Variations in Exynos specific dw-mshc controller */
enum dw_mci_exynos_type {
	DW_MCI_TYPE_EXYNOS4210,
	DW_MCI_TYPE_EXYNOS4412,
	DW_MCI_TYPE_EXYNOS5250,
	DW_MCI_TYPE_EXYNOS5420,
	DW_MCI_TYPE_EXYNOS5420_SMU,
	DW_MCI_TYPE_EXYNOS7,
	DW_MCI_TYPE_EXYNOS7_SMU,
};

/* Exynos implementation specific driver private data */
struct dw_mci_exynos_priv_data {
	enum dw_mci_exynos_type		ctrl_type;
	u8				ciu_div;
	u32				sdr_timing;
	u32				ddr_timing;
	u32				hs400_timing;
	u32				tuned_sample;
	u32				cur_speed;
	u32				dqs_delay;
	u32				saved_dqs_en;
	u32				saved_strobe_ctrl;
};

static struct dw_mci_exynos_compatible {
	char				*compatible;
	enum dw_mci_exynos_type		ctrl_type;
} exynos_compat[] = {
	{
		.compatible	= "samsung,exynos4210-dw-mshc",
		.ctrl_type	= DW_MCI_TYPE_EXYNOS4210,
	}, {
		.compatible	= "samsung,exynos4412-dw-mshc",
		.ctrl_type	= DW_MCI_TYPE_EXYNOS4412,
	}, {
		.compatible	= "samsung,exynos5250-dw-mshc",
		.ctrl_type	= DW_MCI_TYPE_EXYNOS5250,
	}, {
		.compatible	= "samsung,exynos5420-dw-mshc",
		.ctrl_type	= DW_MCI_TYPE_EXYNOS5420,
	}, {
		.compatible	= "samsung,exynos5420-dw-mshc-smu",
		.ctrl_type	= DW_MCI_TYPE_EXYNOS5420_SMU,
	}, {
		.compatible	= "samsung,exynos7-dw-mshc",
		.ctrl_type	= DW_MCI_TYPE_EXYNOS7,
	}, {
		.compatible	= "samsung,exynos7-dw-mshc-smu",
		.ctrl_type	= DW_MCI_TYPE_EXYNOS7_SMU,
	},
};

static inline u8 dw_mci_exynos_get_ciu_div(struct dw_mci *host)
{
	struct dw_mci_exynos_priv_data *priv = host->priv;

	if (priv->ctrl_type == DW_MCI_TYPE_EXYNOS4412)
		return EXYNOS4412_FIXED_CIU_CLK_DIV;
	else if (priv->ctrl_type == DW_MCI_TYPE_EXYNOS4210)
		return EXYNOS4210_FIXED_CIU_CLK_DIV;
	else if (priv->ctrl_type == DW_MCI_TYPE_EXYNOS7 ||
			priv->ctrl_type == DW_MCI_TYPE_EXYNOS7_SMU)
		return SDMMC_CLKSEL_GET_DIV(mci_readl(host, CLKSEL64)) + 1;
	else
		return SDMMC_CLKSEL_GET_DIV(mci_readl(host, CLKSEL)) + 1;
}

static void dw_mci_exynos_config_smu(struct dw_mci *host)
{
	struct dw_mci_exynos_priv_data *priv = host->priv;

	/*
	 * If Exynos is provided the Security management,
	 * set for non-ecryption mode at this time.
	 */
	if (priv->ctrl_type == DW_MCI_TYPE_EXYNOS5420_SMU ||
		priv->ctrl_type == DW_MCI_TYPE_EXYNOS7_SMU) {
		mci_writel(host, MPSBEGIN0, 0);
		mci_writel(host, MPSEND0, SDMMC_ENDING_SEC_NR_MAX);
		mci_writel(host, MPSCTRL0, SDMMC_MPSCTRL_SECURE_WRITE_BIT |
			   SDMMC_MPSCTRL_NON_SECURE_READ_BIT |
			   SDMMC_MPSCTRL_VALID |
			   SDMMC_MPSCTRL_NON_SECURE_WRITE_BIT);
	}
}

static int dw_mci_exynos_priv_init(struct dw_mci *host)
{
	struct dw_mci_exynos_priv_data *priv = host->priv;

	dw_mci_exynos_config_smu(host);

	if (priv->ctrl_type >= DW_MCI_TYPE_EXYNOS5420) {
		priv->saved_strobe_ctrl = mci_readl(host, HS400_DLINE_CTRL);
		priv->saved_dqs_en = mci_readl(host, HS400_DQS_EN);
		priv->saved_dqs_en |= AXI_NON_BLOCKING_WR;
		mci_writel(host, HS400_DQS_EN, priv->saved_dqs_en);
		if (!priv->dqs_delay)
			priv->dqs_delay =
				DQS_CTRL_GET_RD_DELAY(priv->saved_strobe_ctrl);
	}

	host->bus_hz /= (priv->ciu_div + 1);

	return 0;
}

static void dw_mci_exynos_set_clksel_timing(struct dw_mci *host, u32 timing)
{
	struct dw_mci_exynos_priv_data *priv = host->priv;
	u32 clksel;

	if (priv->ctrl_type == DW_MCI_TYPE_EXYNOS7 ||
		priv->ctrl_type == DW_MCI_TYPE_EXYNOS7_SMU)
		clksel = mci_readl(host, CLKSEL64);
	else
		clksel = mci_readl(host, CLKSEL);

	clksel = (clksel & ~SDMMC_CLKSEL_TIMING_MASK) | timing;

	if (priv->ctrl_type == DW_MCI_TYPE_EXYNOS7 ||
		priv->ctrl_type == DW_MCI_TYPE_EXYNOS7_SMU)
		mci_writel(host, CLKSEL64, clksel);
	else
		mci_writel(host, CLKSEL, clksel);

	/*
	 * Exynos4412 and Exynos5250 extends the use of CMD register with the
	 * use of bit 29 (which is reserved on standard MSHC controllers) for
	 * optionally bypassing the HOLD register for command and data. The
	 * HOLD register should be bypassed in case there is no phase shift
	 * applied on CMD/DATA that is sent to the card.
	 */
	if (!SDMMC_CLKSEL_GET_DRV_WD3(clksel) && host->cur_slot)
		set_bit(DW_MMC_CARD_NO_USE_HOLD, &host->cur_slot->flags);
}

#ifdef CONFIG_PM_SLEEP
static int dw_mci_exynos_suspend(struct device *dev)
{
	struct dw_mci *host = dev_get_drvdata(dev);

	return dw_mci_suspend(host);
}

static int dw_mci_exynos_resume(struct device *dev)
{
	struct dw_mci *host = dev_get_drvdata(dev);

	dw_mci_exynos_config_smu(host);
	return dw_mci_resume(host);
}

/**
 * dw_mci_exynos_resume_noirq - Exynos-specific resume code
 *
 * On exynos5420 there is a silicon errata that will sometimes leave the
 * WAKEUP_INT bit in the CLKSEL register asserted.  This bit is 1 to indicate
 * that it fired and we can clear it by writing a 1 back.  Clear it to prevent
 * interrupts from going off constantly.
 *
 * We run this code on all exynos variants because it doesn't hurt.
 */

static int dw_mci_exynos_resume_noirq(struct device *dev)
{
	struct dw_mci *host = dev_get_drvdata(dev);
	struct dw_mci_exynos_priv_data *priv = host->priv;
	u32 clksel;

	if (priv->ctrl_type == DW_MCI_TYPE_EXYNOS7 ||
		priv->ctrl_type == DW_MCI_TYPE_EXYNOS7_SMU)
		clksel = mci_readl(host, CLKSEL64);
	else
		clksel = mci_readl(host, CLKSEL);

	if (clksel & SDMMC_CLKSEL_WAKEUP_INT) {
		if (priv->ctrl_type == DW_MCI_TYPE_EXYNOS7 ||
			priv->ctrl_type == DW_MCI_TYPE_EXYNOS7_SMU)
			mci_writel(host, CLKSEL64, clksel);
		else
			mci_writel(host, CLKSEL, clksel);
	}

	return 0;
}
#else
#define dw_mci_exynos_suspend		NULL
#define dw_mci_exynos_resume		NULL
#define dw_mci_exynos_resume_noirq	NULL
#endif /* CONFIG_PM_SLEEP */

static void dw_mci_exynos_config_hs400(struct dw_mci *host, u32 timing)
{
	struct dw_mci_exynos_priv_data *priv = host->priv;
	u32 dqs, strobe;

	/*
	 * Not supported to configure register
	 * related to HS400
	 */
	if (priv->ctrl_type < DW_MCI_TYPE_EXYNOS5420)
		return;

	dqs = priv->saved_dqs_en;
	strobe = priv->saved_strobe_ctrl;

	if (timing == MMC_TIMING_MMC_HS400) {
		dqs |= DATA_STROBE_EN;
		strobe = DQS_CTRL_RD_DELAY(strobe, priv->dqs_delay);
	} else {
		dqs &= ~DATA_STROBE_EN;
	}

	mci_writel(host, HS400_DQS_EN, dqs);
	mci_writel(host, HS400_DLINE_CTRL, strobe);
}

static void dw_mci_exynos_adjust_clock(struct dw_mci *host, unsigned int wanted)
{
	struct dw_mci_exynos_priv_data *priv = host->priv;
	unsigned long actual;
	u8 div;
	int ret;
	/*
	 * Don't care if wanted clock is zero or
	 * ciu clock is unavailable
	 */
	if (!wanted || IS_ERR(host->ciu_clk))
		return;

	/* Guaranteed minimum frequency for cclkin */
	if (wanted < EXYNOS_CCLKIN_MIN)
		wanted = EXYNOS_CCLKIN_MIN;

	if (wanted == priv->cur_speed)
		return;

	div = dw_mci_exynos_get_ciu_div(host);
	ret = clk_set_rate(host->ciu_clk, wanted * div);
	if (ret)
		dev_warn(host->dev,
			"failed to set clk-rate %u error: %d\n",
			wanted * div, ret);
	actual = clk_get_rate(host->ciu_clk);
	host->bus_hz = actual / div;
	priv->cur_speed = wanted;
	host->current_speed = 0;
}

static void dw_mci_exynos_set_ios(struct dw_mci *host, struct mmc_ios *ios)
{
	struct dw_mci_exynos_priv_data *priv = host->priv;
	unsigned int wanted = ios->clock;
	u32 timing = ios->timing, clksel;

	switch (timing) {
	case MMC_TIMING_MMC_HS400:
		/* Update tuned sample timing */
		clksel = SDMMC_CLKSEL_UP_SAMPLE(
				priv->hs400_timing, priv->tuned_sample);
		wanted <<= 1;
		break;
	case MMC_TIMING_MMC_DDR52:
		clksel = priv->ddr_timing;
		/* Should be double rate for DDR mode */
		if (ios->bus_width == MMC_BUS_WIDTH_8)
			wanted <<= 1;
		break;
	default:
		clksel = priv->sdr_timing;
	}

	/* Set clock timing for the requested speed mode*/
	dw_mci_exynos_set_clksel_timing(host, clksel);

	/* Configure setting for HS400 */
	dw_mci_exynos_config_hs400(host, timing);

	/* Configure clock rate */
	dw_mci_exynos_adjust_clock(host, wanted);
}

static int dw_mci_exynos_parse_dt(struct dw_mci *host)
{
	struct dw_mci_exynos_priv_data *priv;
	struct device_node *np = host->dev->of_node;
	u32 timing[2];
	u32 div = 0;
	int idx;
	int ret;

	priv = devm_kzalloc(host->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	for (idx = 0; idx < ARRAY_SIZE(exynos_compat); idx++) {
		if (of_device_is_compatible(np, exynos_compat[idx].compatible))
			priv->ctrl_type = exynos_compat[idx].ctrl_type;
	}

	if (priv->ctrl_type == DW_MCI_TYPE_EXYNOS4412)
		priv->ciu_div = EXYNOS4412_FIXED_CIU_CLK_DIV - 1;
	else if (priv->ctrl_type == DW_MCI_TYPE_EXYNOS4210)
		priv->ciu_div = EXYNOS4210_FIXED_CIU_CLK_DIV - 1;
	else {
		of_property_read_u32(np, "samsung,dw-mshc-ciu-div", &div);
		priv->ciu_div = div;
	}

	ret = of_property_read_u32_array(np,
			"samsung,dw-mshc-sdr-timing", timing, 2);
	if (ret)
		return ret;

	priv->sdr_timing = SDMMC_CLKSEL_TIMING(timing[0], timing[1], div);

	ret = of_property_read_u32_array(np,
			"samsung,dw-mshc-ddr-timing", timing, 2);
	if (ret)
		return ret;

	priv->ddr_timing = SDMMC_CLKSEL_TIMING(timing[0], timing[1], div);

	ret = of_property_read_u32_array(np,
			"samsung,dw-mshc-hs400-timing", timing, 2);
	if (!ret && of_property_read_u32(np,
				"samsung,read-strobe-delay", &priv->dqs_delay))
		dev_dbg(host->dev,
			"read-strobe-delay is not found, assuming usage of default value\n");

	priv->hs400_timing = SDMMC_CLKSEL_TIMING(timing[0], timing[1],
						HS400_FIXED_CIU_CLK_DIV);
	host->priv = priv;
	return 0;
}

static inline u8 dw_mci_exynos_get_clksmpl(struct dw_mci *host)
{
	struct dw_mci_exynos_priv_data *priv = host->priv;

	if (priv->ctrl_type == DW_MCI_TYPE_EXYNOS7 ||
		priv->ctrl_type == DW_MCI_TYPE_EXYNOS7_SMU)
		return SDMMC_CLKSEL_CCLK_SAMPLE(mci_readl(host, CLKSEL64));
	else
		return SDMMC_CLKSEL_CCLK_SAMPLE(mci_readl(host, CLKSEL));
}

static inline void dw_mci_exynos_set_clksmpl(struct dw_mci *host, u8 sample)
{
	u32 clksel;
	struct dw_mci_exynos_priv_data *priv = host->priv;

	if (priv->ctrl_type == DW_MCI_TYPE_EXYNOS7 ||
		priv->ctrl_type == DW_MCI_TYPE_EXYNOS7_SMU)
		clksel = mci_readl(host, CLKSEL64);
	else
		clksel = mci_readl(host, CLKSEL);
	clksel = SDMMC_CLKSEL_UP_SAMPLE(clksel, sample);
	if (priv->ctrl_type == DW_MCI_TYPE_EXYNOS7 ||
		priv->ctrl_type == DW_MCI_TYPE_EXYNOS7_SMU)
		mci_writel(host, CLKSEL64, clksel);
	else
		mci_writel(host, CLKSEL, clksel);
}

static inline u8 dw_mci_exynos_move_next_clksmpl(struct dw_mci *host)
{
	struct dw_mci_exynos_priv_data *priv = host->priv;
	u32 clksel;
	u8 sample;

	if (priv->ctrl_type == DW_MCI_TYPE_EXYNOS7 ||
		priv->ctrl_type == DW_MCI_TYPE_EXYNOS7_SMU)
		clksel = mci_readl(host, CLKSEL64);
	else
		clksel = mci_readl(host, CLKSEL);

	sample = (clksel + 1) & 0x7;
	clksel = SDMMC_CLKSEL_UP_SAMPLE(clksel, sample);

	if (priv->ctrl_type == DW_MCI_TYPE_EXYNOS7 ||
		priv->ctrl_type == DW_MCI_TYPE_EXYNOS7_SMU)
		mci_writel(host, CLKSEL64, clksel);
	else
		mci_writel(host, CLKSEL, clksel);

	return sample;
}

static s8 dw_mci_exynos_get_best_clksmpl(u8 candiates)
{
	const u8 iter = 8;
	u8 __c;
	s8 i, loc = -1;

	for (i = 0; i < iter; i++) {
		__c = ror8(candiates, i);
		if ((__c & 0xc7) == 0xc7) {
			loc = i;
			goto out;
		}
	}

	for (i = 0; i < iter; i++) {
		__c = ror8(candiates, i);
		if ((__c & 0x83) == 0x83) {
			loc = i;
			goto out;
		}
	}

out:
	return loc;
}

static int dw_mci_exynos_execute_tuning(struct dw_mci_slot *slot, u32 opcode)
{
	struct dw_mci *host = slot->host;
	struct dw_mci_exynos_priv_data *priv = host->priv;
	struct mmc_host *mmc = slot->mmc;
	u8 start_smpl, smpl, candiates = 0;
	s8 found = -1;
	int ret = 0;

	start_smpl = dw_mci_exynos_get_clksmpl(host);

	do {
		mci_writel(host, TMOUT, ~0);
		smpl = dw_mci_exynos_move_next_clksmpl(host);

		if (!mmc_send_tuning(mmc, opcode, NULL))
			candiates |= (1 << smpl);

	} while (start_smpl != smpl);

	found = dw_mci_exynos_get_best_clksmpl(candiates);
	if (found >= 0) {
		dw_mci_exynos_set_clksmpl(host, found);
		priv->tuned_sample = found;
	} else {
		ret = -EIO;
	}

	return ret;
}

static int dw_mci_exynos_prepare_hs400_tuning(struct dw_mci *host,
					struct mmc_ios *ios)
{
	struct dw_mci_exynos_priv_data *priv = host->priv;

	dw_mci_exynos_set_clksel_timing(host, priv->hs400_timing);
	dw_mci_exynos_adjust_clock(host, (ios->clock) << 1);

	return 0;
}

/* Common capabilities of Exynos4/Exynos5 SoC */
static unsigned long exynos_dwmmc_caps[4] = {
	MMC_CAP_1_8V_DDR | MMC_CAP_8_BIT_DATA | MMC_CAP_CMD23,
	MMC_CAP_CMD23,
	MMC_CAP_CMD23,
	MMC_CAP_CMD23,
};

static const struct dw_mci_drv_data exynos_drv_data = {
	.caps			= exynos_dwmmc_caps,
	.init			= dw_mci_exynos_priv_init,
	.set_ios		= dw_mci_exynos_set_ios,
	.parse_dt		= dw_mci_exynos_parse_dt,
	.execute_tuning		= dw_mci_exynos_execute_tuning,
	.prepare_hs400_tuning	= dw_mci_exynos_prepare_hs400_tuning,
};

static const struct of_device_id dw_mci_exynos_match[] = {
	{ .compatible = "samsung,exynos4412-dw-mshc",
			.data = &exynos_drv_data, },
	{ .compatible = "samsung,exynos5250-dw-mshc",
			.data = &exynos_drv_data, },
	{ .compatible = "samsung,exynos5420-dw-mshc",
			.data = &exynos_drv_data, },
	{ .compatible = "samsung,exynos5420-dw-mshc-smu",
			.data = &exynos_drv_data, },
	{ .compatible = "samsung,exynos7-dw-mshc",
			.data = &exynos_drv_data, },
	{ .compatible = "samsung,exynos7-dw-mshc-smu",
			.data = &exynos_drv_data, },
	{},
};
MODULE_DEVICE_TABLE(of, dw_mci_exynos_match);

static int dw_mci_exynos_probe(struct platform_device *pdev)
{
	const struct dw_mci_drv_data *drv_data;
	const struct of_device_id *match;

	match = of_match_node(dw_mci_exynos_match, pdev->dev.of_node);
	drv_data = match->data;
	return dw_mci_pltfm_register(pdev, drv_data);
}

static const struct dev_pm_ops dw_mci_exynos_pmops = {
	SET_SYSTEM_SLEEP_PM_OPS(dw_mci_exynos_suspend, dw_mci_exynos_resume)
	.resume_noirq = dw_mci_exynos_resume_noirq,
	.thaw_noirq = dw_mci_exynos_resume_noirq,
	.restore_noirq = dw_mci_exynos_resume_noirq,
};

static struct platform_driver dw_mci_exynos_pltfm_driver = {
	.probe		= dw_mci_exynos_probe,
	.remove		= dw_mci_pltfm_remove,
	.driver		= {
		.name		= "dwmmc_exynos",
		.of_match_table	= dw_mci_exynos_match,
		.pm		= &dw_mci_exynos_pmops,
	},
};

module_platform_driver(dw_mci_exynos_pltfm_driver);

MODULE_DESCRIPTION("Samsung Specific DW-MSHC Driver Extension");
MODULE_AUTHOR("Thomas Abraham <thomas.ab@samsung.com");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:dwmmc_exynos");
