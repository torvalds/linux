// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Exynos Specific Extensions for Synopsys DW Multimedia Card Interface driver
 *
 * Copyright (C) 2012, Samsung Electronics Co., Ltd.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
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
	DW_MCI_TYPE_ARTPEC8,
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
	}, {
		.compatible	= "axis,artpec8-dw-mshc",
		.ctrl_type	= DW_MCI_TYPE_ARTPEC8,
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
			priv->ctrl_type == DW_MCI_TYPE_EXYNOS7_SMU ||
			priv->ctrl_type == DW_MCI_TYPE_ARTPEC8)
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

	if (priv->ctrl_type == DW_MCI_TYPE_ARTPEC8) {
		/* Quirk needed for the ARTPEC-8 SoC */
		host->quirks |= DW_MMC_QUIRK_EXTENDED_TMOUT;
	}

	host->bus_hz /= (priv->ciu_div + 1);

	return 0;
}

static void dw_mci_exynos_set_clksel_timing(struct dw_mci *host, u32 timing)
{
	struct dw_mci_exynos_priv_data *priv = host->priv;
	u32 clksel;

	if (priv->ctrl_type == DW_MCI_TYPE_EXYNOS7 ||
		priv->ctrl_type == DW_MCI_TYPE_EXYNOS7_SMU ||
		priv->ctrl_type == DW_MCI_TYPE_ARTPEC8)
		clksel = mci_readl(host, CLKSEL64);
	else
		clksel = mci_readl(host, CLKSEL);

	clksel = (clksel & ~SDMMC_CLKSEL_TIMING_MASK) | timing;

	if (priv->ctrl_type == DW_MCI_TYPE_EXYNOS7 ||
		priv->ctrl_type == DW_MCI_TYPE_EXYNOS7_SMU ||
		priv->ctrl_type == DW_MCI_TYPE_ARTPEC8)
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
	if (!SDMMC_CLKSEL_GET_DRV_WD3(clksel) && host->slot)
		set_bit(DW_MMC_CARD_NO_USE_HOLD, &host->slot->flags);
}

#ifdef CONFIG_PM
static int dw_mci_exynos_runtime_resume(struct device *dev)
{
	struct dw_mci *host = dev_get_drvdata(dev);
	int ret;

	ret = dw_mci_runtime_resume(dev);
	if (ret)
		return ret;

	dw_mci_exynos_config_smu(host);

	return ret;
}
#endif /* CONFIG_PM */

#ifdef CONFIG_PM_SLEEP
/**
 * dw_mci_exynos_suspend_noirq - Exynos-specific suspend code
 * @dev: Device to suspend (this device)
 *
 * This ensures that device will be in runtime active state in
 * dw_mci_exynos_resume_noirq after calling pm_runtime_force_resume()
 */
static int dw_mci_exynos_suspend_noirq(struct device *dev)
{
	pm_runtime_get_noresume(dev);
	return pm_runtime_force_suspend(dev);
}

/**
 * dw_mci_exynos_resume_noirq - Exynos-specific resume code
 * @dev: Device to resume (this device)
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
	int ret;

	ret = pm_runtime_force_resume(dev);
	if (ret)
		return ret;

	if (priv->ctrl_type == DW_MCI_TYPE_EXYNOS7 ||
		priv->ctrl_type == DW_MCI_TYPE_EXYNOS7_SMU ||
		priv->ctrl_type == DW_MCI_TYPE_ARTPEC8)
		clksel = mci_readl(host, CLKSEL64);
	else
		clksel = mci_readl(host, CLKSEL);

	if (clksel & SDMMC_CLKSEL_WAKEUP_INT) {
		if (priv->ctrl_type == DW_MCI_TYPE_EXYNOS7 ||
			priv->ctrl_type == DW_MCI_TYPE_EXYNOS7_SMU ||
			priv->ctrl_type == DW_MCI_TYPE_ARTPEC8)
			mci_writel(host, CLKSEL64, clksel);
		else
			mci_writel(host, CLKSEL, clksel);
	}

	pm_runtime_put(dev);

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static void dw_mci_exynos_config_hs400(struct dw_mci *host, u32 timing)
{
	struct dw_mci_exynos_priv_data *priv = host->priv;
	u32 dqs, strobe;

	/*
	 * Not supported to configure register
	 * related to HS400
	 */
	if ((priv->ctrl_type < DW_MCI_TYPE_EXYNOS5420) ||
		(priv->ctrl_type == DW_MCI_TYPE_ARTPEC8)) {
		if (timing == MMC_TIMING_MMC_HS400)
			dev_warn(host->dev,
				 "cannot configure HS400, unsupported chipset\n");
		return;
	}

	dqs = priv->saved_dqs_en;
	strobe = priv->saved_strobe_ctrl;

	if (timing == MMC_TIMING_MMC_HS400) {
		dqs |= DATA_STROBE_EN;
		strobe = DQS_CTRL_RD_DELAY(strobe, priv->dqs_delay);
	} else if (timing == MMC_TIMING_UHS_SDR104) {
		dqs &= 0xffffff00;
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
	case MMC_TIMING_UHS_SDR104:
	case MMC_TIMING_UHS_SDR50:
		clksel = (priv->sdr_timing & 0xfff8ffff) |
			(priv->ciu_div << 16);
		break;
	case MMC_TIMING_UHS_DDR50:
		clksel = (priv->ddr_timing & 0xfff8ffff) |
			(priv->ciu_div << 16);
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
		priv->ctrl_type == DW_MCI_TYPE_EXYNOS7_SMU ||
		priv->ctrl_type == DW_MCI_TYPE_ARTPEC8)
		return SDMMC_CLKSEL_CCLK_SAMPLE(mci_readl(host, CLKSEL64));
	else
		return SDMMC_CLKSEL_CCLK_SAMPLE(mci_readl(host, CLKSEL));
}

static inline void dw_mci_exynos_set_clksmpl(struct dw_mci *host, u8 sample)
{
	u32 clksel;
	struct dw_mci_exynos_priv_data *priv = host->priv;

	if (priv->ctrl_type == DW_MCI_TYPE_EXYNOS7 ||
		priv->ctrl_type == DW_MCI_TYPE_EXYNOS7_SMU ||
		priv->ctrl_type == DW_MCI_TYPE_ARTPEC8)
		clksel = mci_readl(host, CLKSEL64);
	else
		clksel = mci_readl(host, CLKSEL);
	clksel = SDMMC_CLKSEL_UP_SAMPLE(clksel, sample);
	if (priv->ctrl_type == DW_MCI_TYPE_EXYNOS7 ||
		priv->ctrl_type == DW_MCI_TYPE_EXYNOS7_SMU ||
		priv->ctrl_type == DW_MCI_TYPE_ARTPEC8)
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
		priv->ctrl_type == DW_MCI_TYPE_EXYNOS7_SMU ||
		priv->ctrl_type == DW_MCI_TYPE_ARTPEC8)
		clksel = mci_readl(host, CLKSEL64);
	else
		clksel = mci_readl(host, CLKSEL);

	sample = (clksel + 1) & 0x7;
	clksel = SDMMC_CLKSEL_UP_SAMPLE(clksel, sample);

	if (priv->ctrl_type == DW_MCI_TYPE_EXYNOS7 ||
		priv->ctrl_type == DW_MCI_TYPE_EXYNOS7_SMU ||
		priv->ctrl_type == DW_MCI_TYPE_ARTPEC8)
		mci_writel(host, CLKSEL64, clksel);
	else
		mci_writel(host, CLKSEL, clksel);

	return sample;
}

static s8 dw_mci_exynos_get_best_clksmpl(u8 candidates)
{
	const u8 iter = 8;
	u8 __c;
	s8 i, loc = -1;

	for (i = 0; i < iter; i++) {
		__c = ror8(candidates, i);
		if ((__c & 0xc7) == 0xc7) {
			loc = i;
			goto out;
		}
	}

	for (i = 0; i < iter; i++) {
		__c = ror8(candidates, i);
		if ((__c & 0x83) == 0x83) {
			loc = i;
			goto out;
		}
	}

	/*
	 * If there is no cadiates value, then it needs to return -EIO.
	 * If there are candidates values and don't find bset clk sample value,
	 * then use a first candidates clock sample value.
	 */
	for (i = 0; i < iter; i++) {
		__c = ror8(candidates, i);
		if ((__c & 0x1) == 0x1) {
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
	u8 start_smpl, smpl, candidates = 0;
	s8 found;
	int ret = 0;

	start_smpl = dw_mci_exynos_get_clksmpl(host);

	do {
		mci_writel(host, TMOUT, ~0);
		smpl = dw_mci_exynos_move_next_clksmpl(host);

		if (!mmc_send_tuning(mmc, opcode, NULL))
			candidates |= (1 << smpl);

	} while (start_smpl != smpl);

	found = dw_mci_exynos_get_best_clksmpl(candidates);
	if (found >= 0) {
		dw_mci_exynos_set_clksmpl(host, found);
		priv->tuned_sample = found;
	} else {
		ret = -EIO;
		dev_warn(&mmc->class_dev,
			"There is no candidates value about clksmpl!\n");
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

static void dw_mci_exynos_set_data_timeout(struct dw_mci *host,
					   unsigned int timeout_ns)
{
	u32 clk_div, tmout;
	u64 tmp;
	unsigned int tmp2;

	clk_div = (mci_readl(host, CLKDIV) & 0xFF) * 2;
	if (clk_div == 0)
		clk_div = 1;

	tmp = DIV_ROUND_UP_ULL((u64)timeout_ns * host->bus_hz, NSEC_PER_SEC);
	tmp = DIV_ROUND_UP_ULL(tmp, clk_div);

	/* TMOUT[7:0] (RESPONSE_TIMEOUT) */
	tmout = 0xFF; /* Set maximum */

	/*
	 * Extended HW timer (max = 0x6FFFFF2):
	 * ((TMOUT[10:8] - 1) * 0xFFFFFF + TMOUT[31:11] * 8)
	 */
	if (!tmp || tmp > 0x6FFFFF2)
		tmout |= (0xFFFFFF << 8);
	else {
		/* TMOUT[10:8] */
		tmp2 = (((unsigned int)tmp / 0xFFFFFF) + 1) & 0x7;
		tmout |= tmp2 << 8;

		/* TMOUT[31:11] */
		tmp = tmp - ((tmp2 - 1) * 0xFFFFFF);
		tmout |= (tmp & 0xFFFFF8) << 8;
	}

	mci_writel(host, TMOUT, tmout);
	dev_dbg(host->dev, "timeout_ns: %u => TMOUT[31:8]: %#08x",
		timeout_ns, tmout >> 8);
}

static u32 dw_mci_exynos_get_drto_clks(struct dw_mci *host)
{
	u32 drto_clks;

	drto_clks = mci_readl(host, TMOUT) >> 8;

	return (((drto_clks & 0x7) - 1) * 0xFFFFFF) + ((drto_clks & 0xFFFFF8));
}

/* Common capabilities of Exynos4/Exynos5 SoC */
static unsigned long exynos_dwmmc_caps[4] = {
	MMC_CAP_1_8V_DDR | MMC_CAP_8_BIT_DATA,
	0,
	0,
	0,
};

static const struct dw_mci_drv_data exynos_drv_data = {
	.caps			= exynos_dwmmc_caps,
	.num_caps		= ARRAY_SIZE(exynos_dwmmc_caps),
	.common_caps		= MMC_CAP_CMD23,
	.init			= dw_mci_exynos_priv_init,
	.set_ios		= dw_mci_exynos_set_ios,
	.parse_dt		= dw_mci_exynos_parse_dt,
	.execute_tuning		= dw_mci_exynos_execute_tuning,
	.prepare_hs400_tuning	= dw_mci_exynos_prepare_hs400_tuning,
};

static const struct dw_mci_drv_data artpec_drv_data = {
	.common_caps		= MMC_CAP_CMD23,
	.init			= dw_mci_exynos_priv_init,
	.set_ios		= dw_mci_exynos_set_ios,
	.parse_dt		= dw_mci_exynos_parse_dt,
	.execute_tuning		= dw_mci_exynos_execute_tuning,
	.set_data_timeout		= dw_mci_exynos_set_data_timeout,
	.get_drto_clks		= dw_mci_exynos_get_drto_clks,
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
	{ .compatible = "axis,artpec8-dw-mshc",
			.data = &artpec_drv_data, },
	{},
};
MODULE_DEVICE_TABLE(of, dw_mci_exynos_match);

static int dw_mci_exynos_probe(struct platform_device *pdev)
{
	const struct dw_mci_drv_data *drv_data;
	const struct of_device_id *match;
	int ret;

	match = of_match_node(dw_mci_exynos_match, pdev->dev.of_node);
	drv_data = match->data;

	pm_runtime_get_noresume(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	ret = dw_mci_pltfm_register(pdev, drv_data);
	if (ret) {
		pm_runtime_disable(&pdev->dev);
		pm_runtime_set_suspended(&pdev->dev);
		pm_runtime_put_noidle(&pdev->dev);

		return ret;
	}

	return 0;
}

static void dw_mci_exynos_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);

	dw_mci_pltfm_remove(pdev);
}

static const struct dev_pm_ops dw_mci_exynos_pmops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(dw_mci_exynos_suspend_noirq,
				      dw_mci_exynos_resume_noirq)
	SET_RUNTIME_PM_OPS(dw_mci_runtime_suspend,
			   dw_mci_exynos_runtime_resume,
			   NULL)
};

static struct platform_driver dw_mci_exynos_pltfm_driver = {
	.probe		= dw_mci_exynos_probe,
	.remove_new	= dw_mci_exynos_remove,
	.driver		= {
		.name		= "dwmmc_exynos",
		.probe_type	= PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table	= dw_mci_exynos_match,
		.pm		= &dw_mci_exynos_pmops,
	},
};

module_platform_driver(dw_mci_exynos_pltfm_driver);

MODULE_DESCRIPTION("Samsung Specific DW-MSHC Driver Extension");
MODULE_AUTHOR("Thomas Abraham <thomas.ab@samsung.com");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:dwmmc_exynos");
