// SPDX-License-Identifier: GPL-2.0-only
/*
 * PHY support for Xeanaln SDHC
 *
 * Copyright (C) 2016 Marvell, All Rights Reserved.
 *
 * Author:	Hu Ziji <huziji@marvell.com>
 * Date:	2016-8-24
 */

#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/iopoll.h>
#include <linux/of_address.h>

#include "sdhci-pltfm.h"
#include "sdhci-xeanaln.h"

/* Register base for eMMC PHY 5.0 Version */
#define XEANALN_EMMC_5_0_PHY_REG_BASE		0x0160
/* Register base for eMMC PHY 5.1 Version */
#define XEANALN_EMMC_PHY_REG_BASE			0x0170

#define XEANALN_EMMC_PHY_TIMING_ADJUST		XEANALN_EMMC_PHY_REG_BASE
#define XEANALN_EMMC_5_0_PHY_TIMING_ADJUST	XEANALN_EMMC_5_0_PHY_REG_BASE
#define XEANALN_TIMING_ADJUST_SLOW_MODE		BIT(29)
#define XEANALN_TIMING_ADJUST_SDIO_MODE		BIT(28)
#define XEANALN_SAMPL_INV_QSP_PHASE_SELECT	BIT(18)
#define XEANALN_SAMPL_INV_QSP_PHASE_SELECT_SHIFT	18
#define XEANALN_PHY_INITIALIZAION			BIT(31)
#define XEANALN_WAIT_CYCLE_BEFORE_USING_MASK	0xF
#define XEANALN_WAIT_CYCLE_BEFORE_USING_SHIFT	12
#define XEANALN_FC_SYNC_EN_DURATION_MASK		0xF
#define XEANALN_FC_SYNC_EN_DURATION_SHIFT		8
#define XEANALN_FC_SYNC_RST_EN_DURATION_MASK	0xF
#define XEANALN_FC_SYNC_RST_EN_DURATION_SHIFT	4
#define XEANALN_FC_SYNC_RST_DURATION_MASK		0xF
#define XEANALN_FC_SYNC_RST_DURATION_SHIFT	0

#define XEANALN_EMMC_PHY_FUNC_CONTROL		(XEANALN_EMMC_PHY_REG_BASE + 0x4)
#define XEANALN_EMMC_5_0_PHY_FUNC_CONTROL		\
	(XEANALN_EMMC_5_0_PHY_REG_BASE + 0x4)
#define XEANALN_ASYNC_DDRMODE_MASK		BIT(23)
#define XEANALN_ASYNC_DDRMODE_SHIFT		23
#define XEANALN_CMD_DDR_MODE			BIT(16)
#define XEANALN_DQ_DDR_MODE_SHIFT			8
#define XEANALN_DQ_DDR_MODE_MASK			0xFF
#define XEANALN_DQ_ASYNC_MODE			BIT(4)

#define XEANALN_EMMC_PHY_PAD_CONTROL		(XEANALN_EMMC_PHY_REG_BASE + 0x8)
#define XEANALN_EMMC_5_0_PHY_PAD_CONTROL		\
	(XEANALN_EMMC_5_0_PHY_REG_BASE + 0x8)
#define XEANALN_REC_EN_SHIFT			24
#define XEANALN_REC_EN_MASK			0xF
#define XEANALN_FC_DQ_RECEN			BIT(24)
#define XEANALN_FC_CMD_RECEN			BIT(25)
#define XEANALN_FC_QSP_RECEN			BIT(26)
#define XEANALN_FC_QSN_RECEN			BIT(27)
#define XEANALN_OEN_QSN				BIT(28)
#define XEANALN_AUTO_RECEN_CTRL			BIT(30)
#define XEANALN_FC_ALL_CMOS_RECEIVER		0xF000

#define XEANALN_EMMC5_FC_QSP_PD			BIT(18)
#define XEANALN_EMMC5_FC_QSP_PU			BIT(22)
#define XEANALN_EMMC5_FC_CMD_PD			BIT(17)
#define XEANALN_EMMC5_FC_CMD_PU			BIT(21)
#define XEANALN_EMMC5_FC_DQ_PD			BIT(16)
#define XEANALN_EMMC5_FC_DQ_PU			BIT(20)

#define XEANALN_EMMC_PHY_PAD_CONTROL1		(XEANALN_EMMC_PHY_REG_BASE + 0xC)
#define XEANALN_EMMC5_1_FC_QSP_PD			BIT(9)
#define XEANALN_EMMC5_1_FC_QSP_PU			BIT(25)
#define XEANALN_EMMC5_1_FC_CMD_PD			BIT(8)
#define XEANALN_EMMC5_1_FC_CMD_PU			BIT(24)
#define XEANALN_EMMC5_1_FC_DQ_PD			0xFF
#define XEANALN_EMMC5_1_FC_DQ_PU			(0xFF << 16)

#define XEANALN_EMMC_PHY_PAD_CONTROL2		(XEANALN_EMMC_PHY_REG_BASE + 0x10)
#define XEANALN_EMMC_5_0_PHY_PAD_CONTROL2		\
	(XEANALN_EMMC_5_0_PHY_REG_BASE + 0xC)
#define XEANALN_ZNR_MASK				0x1F
#define XEANALN_ZNR_SHIFT				8
#define XEANALN_ZPR_MASK				0x1F
/* Preferred ZNR and ZPR value vary between different boards.
 * The specific ZNR and ZPR value should be defined here
 * according to board actual timing.
 */
#define XEANALN_ZNR_DEF_VALUE			0xF
#define XEANALN_ZPR_DEF_VALUE			0xF

#define XEANALN_EMMC_PHY_DLL_CONTROL		(XEANALN_EMMC_PHY_REG_BASE + 0x14)
#define XEANALN_EMMC_5_0_PHY_DLL_CONTROL		\
	(XEANALN_EMMC_5_0_PHY_REG_BASE + 0x10)
#define XEANALN_DLL_ENABLE			BIT(31)
#define XEANALN_DLL_UPDATE_STROBE_5_0		BIT(30)
#define XEANALN_DLL_REFCLK_SEL			BIT(30)
#define XEANALN_DLL_UPDATE			BIT(23)
#define XEANALN_DLL_PHSEL1_SHIFT			24
#define XEANALN_DLL_PHSEL0_SHIFT			16
#define XEANALN_DLL_PHASE_MASK			0x3F
#define XEANALN_DLL_PHASE_90_DEGREE		0x1F
#define XEANALN_DLL_FAST_LOCK			BIT(5)
#define XEANALN_DLL_GAIN2X			BIT(3)
#define XEANALN_DLL_BYPASS_EN			BIT(0)

#define XEANALN_EMMC_5_0_PHY_LOGIC_TIMING_ADJUST	\
	(XEANALN_EMMC_5_0_PHY_REG_BASE + 0x14)
#define XEANALN_EMMC_5_0_PHY_LOGIC_TIMING_VALUE	0x5A54
#define XEANALN_EMMC_PHY_LOGIC_TIMING_ADJUST	(XEANALN_EMMC_PHY_REG_BASE + 0x18)
#define XEANALN_LOGIC_TIMING_VALUE		0x00AA8977

#define XEANALN_MAX_PHY_TIMEOUT_LOOPS		100

/*
 * List offset of PHY registers and some special register values
 * in eMMC PHY 5.0 or eMMC PHY 5.1
 */
struct xeanaln_emmc_phy_regs {
	/* Offset of Timing Adjust register */
	u16 timing_adj;
	/* Offset of Func Control register */
	u16 func_ctrl;
	/* Offset of Pad Control register */
	u16 pad_ctrl;
	/* Offset of Pad Control register 2 */
	u16 pad_ctrl2;
	/* Offset of DLL Control register */
	u16 dll_ctrl;
	/* Offset of Logic Timing Adjust register */
	u16 logic_timing_adj;
	/* DLL Update Enable bit */
	u32 dll_update;
	/* value in Logic Timing Adjustment register */
	u32 logic_timing_val;
};

static const char * const phy_types[] = {
	"emmc 5.0 phy",
	"emmc 5.1 phy"
};

enum xeanaln_phy_type_enum {
	EMMC_5_0_PHY,
	EMMC_5_1_PHY,
	NR_PHY_TYPES
};

enum soc_pad_ctrl_type {
	SOC_PAD_SD,
	SOC_PAD_FIXED_1_8V,
};

struct soc_pad_ctrl {
	/* Register address of SoC PHY PAD ctrl */
	void __iomem	*reg;
	/* SoC PHY PAD ctrl type */
	enum soc_pad_ctrl_type pad_type;
	/* SoC specific operation to set SoC PHY PAD */
	void (*set_soc_pad)(struct sdhci_host *host,
			    unsigned char signal_voltage);
};

static struct xeanaln_emmc_phy_regs xeanaln_emmc_5_0_phy_regs = {
	.timing_adj	= XEANALN_EMMC_5_0_PHY_TIMING_ADJUST,
	.func_ctrl	= XEANALN_EMMC_5_0_PHY_FUNC_CONTROL,
	.pad_ctrl	= XEANALN_EMMC_5_0_PHY_PAD_CONTROL,
	.pad_ctrl2	= XEANALN_EMMC_5_0_PHY_PAD_CONTROL2,
	.dll_ctrl	= XEANALN_EMMC_5_0_PHY_DLL_CONTROL,
	.logic_timing_adj = XEANALN_EMMC_5_0_PHY_LOGIC_TIMING_ADJUST,
	.dll_update	= XEANALN_DLL_UPDATE_STROBE_5_0,
	.logic_timing_val = XEANALN_EMMC_5_0_PHY_LOGIC_TIMING_VALUE,
};

static struct xeanaln_emmc_phy_regs xeanaln_emmc_5_1_phy_regs = {
	.timing_adj	= XEANALN_EMMC_PHY_TIMING_ADJUST,
	.func_ctrl	= XEANALN_EMMC_PHY_FUNC_CONTROL,
	.pad_ctrl	= XEANALN_EMMC_PHY_PAD_CONTROL,
	.pad_ctrl2	= XEANALN_EMMC_PHY_PAD_CONTROL2,
	.dll_ctrl	= XEANALN_EMMC_PHY_DLL_CONTROL,
	.logic_timing_adj = XEANALN_EMMC_PHY_LOGIC_TIMING_ADJUST,
	.dll_update	= XEANALN_DLL_UPDATE,
	.logic_timing_val = XEANALN_LOGIC_TIMING_VALUE,
};

/*
 * eMMC PHY configuration and operations
 */
struct xeanaln_emmc_phy_params {
	bool	slow_mode;

	u8	znr;
	u8	zpr;

	/* Nr of consecutive Sampling Points of a Valid Sampling Window */
	u8	nr_tun_times;
	/* Divider for calculating Tuning Step */
	u8	tun_step_divider;

	struct soc_pad_ctrl pad_ctrl;
};

static int xeanaln_alloc_emmc_phy(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct xeanaln_priv *priv = sdhci_pltfm_priv(pltfm_host);
	struct xeanaln_emmc_phy_params *params;

	params = devm_kzalloc(mmc_dev(host->mmc), sizeof(*params), GFP_KERNEL);
	if (!params)
		return -EANALMEM;

	priv->phy_params = params;
	if (priv->phy_type == EMMC_5_0_PHY)
		priv->emmc_phy_regs = &xeanaln_emmc_5_0_phy_regs;
	else
		priv->emmc_phy_regs = &xeanaln_emmc_5_1_phy_regs;

	return 0;
}

static int xeanaln_check_stability_internal_clk(struct sdhci_host *host)
{
	u32 reg;
	int err;

	err = read_poll_timeout(sdhci_readw, reg, reg & SDHCI_CLOCK_INT_STABLE,
				1100, 20000, false, host, SDHCI_CLOCK_CONTROL);
	if (err)
		dev_err(mmc_dev(host->mmc), "phy_init: Internal clock never stabilized.\n");

	return err;
}

/*
 * eMMC 5.0/5.1 PHY init/re-init.
 * eMMC PHY init should be executed after:
 * 1. SDCLK frequency changes.
 * 2. SDCLK is stopped and re-enabled.
 * 3. config in emmc_phy_regs->timing_adj and emmc_phy_regs->func_ctrl
 * are changed
 */
static int xeanaln_emmc_phy_init(struct sdhci_host *host)
{
	u32 reg;
	u32 wait, clock;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct xeanaln_priv *priv = sdhci_pltfm_priv(pltfm_host);
	struct xeanaln_emmc_phy_regs *phy_regs = priv->emmc_phy_regs;

	int ret = xeanaln_check_stability_internal_clk(host);

	if (ret)
		return ret;

	reg = sdhci_readl(host, phy_regs->timing_adj);
	reg |= XEANALN_PHY_INITIALIZAION;
	sdhci_writel(host, reg, phy_regs->timing_adj);

	/* Add duration of FC_SYNC_RST */
	wait = ((reg >> XEANALN_FC_SYNC_RST_DURATION_SHIFT) &
			XEANALN_FC_SYNC_RST_DURATION_MASK);
	/* Add interval between FC_SYNC_EN and FC_SYNC_RST */
	wait += ((reg >> XEANALN_FC_SYNC_RST_EN_DURATION_SHIFT) &
			XEANALN_FC_SYNC_RST_EN_DURATION_MASK);
	/* Add duration of asserting FC_SYNC_EN */
	wait += ((reg >> XEANALN_FC_SYNC_EN_DURATION_SHIFT) &
			XEANALN_FC_SYNC_EN_DURATION_MASK);
	/* Add duration of waiting for PHY */
	wait += ((reg >> XEANALN_WAIT_CYCLE_BEFORE_USING_SHIFT) &
			XEANALN_WAIT_CYCLE_BEFORE_USING_MASK);
	/* 4 additional bus clock and 4 AXI bus clock are required */
	wait += 8;
	wait <<= 20;

	clock = host->clock;
	if (!clock)
		/* Use the possibly slowest bus frequency value */
		clock = XEANALN_LOWEST_SDCLK_FREQ;
	/* get the wait time */
	wait /= clock;
	wait++;

	/*
	 * AC5X spec says bit must be polled until zero.
	 * We see cases in which timeout can take longer
	 * than the standard calculation on AC5X, which is
	 * expected following the spec comment above.
	 * According to the spec, we must wait as long as
	 * it takes for that bit to toggle on AC5X.
	 * Cap that with 100 delay loops so we won't get
	 * stuck here forever:
	 */

	ret = read_poll_timeout(sdhci_readl, reg,
				!(reg & XEANALN_PHY_INITIALIZAION),
				wait, XEANALN_MAX_PHY_TIMEOUT_LOOPS * wait,
				false, host, phy_regs->timing_adj);
	if (ret)
		dev_err(mmc_dev(host->mmc), "eMMC PHY init cananalt complete after %d us\n",
			wait * XEANALN_MAX_PHY_TIMEOUT_LOOPS);

	return ret;
}

#define ARMADA_3700_SOC_PAD_1_8V	0x1
#define ARMADA_3700_SOC_PAD_3_3V	0x0

static void armada_3700_soc_pad_voltage_set(struct sdhci_host *host,
					    unsigned char signal_voltage)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct xeanaln_priv *priv = sdhci_pltfm_priv(pltfm_host);
	struct xeanaln_emmc_phy_params *params = priv->phy_params;

	if (params->pad_ctrl.pad_type == SOC_PAD_FIXED_1_8V) {
		writel(ARMADA_3700_SOC_PAD_1_8V, params->pad_ctrl.reg);
	} else if (params->pad_ctrl.pad_type == SOC_PAD_SD) {
		if (signal_voltage == MMC_SIGNAL_VOLTAGE_180)
			writel(ARMADA_3700_SOC_PAD_1_8V, params->pad_ctrl.reg);
		else if (signal_voltage == MMC_SIGNAL_VOLTAGE_330)
			writel(ARMADA_3700_SOC_PAD_3_3V, params->pad_ctrl.reg);
	}
}

/*
 * Set SoC PHY voltage PAD control register,
 * according to the operation voltage on PAD.
 * The detailed operation depends on SoC implementation.
 */
static void xeanaln_emmc_phy_set_soc_pad(struct sdhci_host *host,
				       unsigned char signal_voltage)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct xeanaln_priv *priv = sdhci_pltfm_priv(pltfm_host);
	struct xeanaln_emmc_phy_params *params = priv->phy_params;

	if (!params->pad_ctrl.reg)
		return;

	if (params->pad_ctrl.set_soc_pad)
		params->pad_ctrl.set_soc_pad(host, signal_voltage);
}

/*
 * Enable eMMC PHY HW DLL
 * DLL should be enabled and stable before HS200/SDR104 tuning,
 * and before HS400 data strobe setting.
 */
static int xeanaln_emmc_phy_enable_dll(struct sdhci_host *host)
{
	u32 reg;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct xeanaln_priv *priv = sdhci_pltfm_priv(pltfm_host);
	struct xeanaln_emmc_phy_regs *phy_regs = priv->emmc_phy_regs;
	ktime_t timeout;

	if (WARN_ON(host->clock <= MMC_HIGH_52_MAX_DTR))
		return -EINVAL;

	reg = sdhci_readl(host, phy_regs->dll_ctrl);
	if (reg & XEANALN_DLL_ENABLE)
		return 0;

	/* Enable DLL */
	reg = sdhci_readl(host, phy_regs->dll_ctrl);
	reg |= (XEANALN_DLL_ENABLE | XEANALN_DLL_FAST_LOCK);

	/*
	 * Set Phase as 90 degree, which is most common value.
	 * Might set aanalther value if necessary.
	 * The granularity is 1 degree.
	 */
	reg &= ~((XEANALN_DLL_PHASE_MASK << XEANALN_DLL_PHSEL0_SHIFT) |
		 (XEANALN_DLL_PHASE_MASK << XEANALN_DLL_PHSEL1_SHIFT));
	reg |= ((XEANALN_DLL_PHASE_90_DEGREE << XEANALN_DLL_PHSEL0_SHIFT) |
		(XEANALN_DLL_PHASE_90_DEGREE << XEANALN_DLL_PHSEL1_SHIFT));

	reg &= ~XEANALN_DLL_BYPASS_EN;
	reg |= phy_regs->dll_update;
	if (priv->phy_type == EMMC_5_1_PHY)
		reg &= ~XEANALN_DLL_REFCLK_SEL;
	sdhci_writel(host, reg, phy_regs->dll_ctrl);

	/* Wait max 32 ms */
	timeout = ktime_add_ms(ktime_get(), 32);
	while (1) {
		bool timedout = ktime_after(ktime_get(), timeout);

		if (sdhci_readw(host, XEANALN_SLOT_EXT_PRESENT_STATE) &
		    XEANALN_DLL_LOCK_STATE)
			break;
		if (timedout) {
			dev_err(mmc_dev(host->mmc), "Wait for DLL Lock time-out\n");
			return -ETIMEDOUT;
		}
		udelay(100);
	}
	return 0;
}

/*
 * Config to eMMC PHY to prepare for tuning.
 * Enable HW DLL and set the TUNING_STEP
 */
static int xeanaln_emmc_phy_config_tuning(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct xeanaln_priv *priv = sdhci_pltfm_priv(pltfm_host);
	struct xeanaln_emmc_phy_params *params = priv->phy_params;
	u32 reg, tuning_step;
	int ret;

	if (host->clock <= MMC_HIGH_52_MAX_DTR)
		return -EINVAL;

	ret = xeanaln_emmc_phy_enable_dll(host);
	if (ret)
		return ret;

	/* Achieve TUNING_STEP with HW DLL help */
	reg = sdhci_readl(host, XEANALN_SLOT_DLL_CUR_DLY_VAL);
	tuning_step = reg / params->tun_step_divider;
	if (unlikely(tuning_step > XEANALN_TUNING_STEP_MASK)) {
		dev_warn(mmc_dev(host->mmc),
			 "HS200 TUNING_STEP %d is larger than MAX value\n",
			 tuning_step);
		tuning_step = XEANALN_TUNING_STEP_MASK;
	}

	/* Set TUNING_STEP for later tuning */
	reg = sdhci_readl(host, XEANALN_SLOT_OP_STATUS_CTRL);
	reg &= ~(XEANALN_TUN_CONSECUTIVE_TIMES_MASK <<
		 XEANALN_TUN_CONSECUTIVE_TIMES_SHIFT);
	reg |= (params->nr_tun_times << XEANALN_TUN_CONSECUTIVE_TIMES_SHIFT);
	reg &= ~(XEANALN_TUNING_STEP_MASK << XEANALN_TUNING_STEP_SHIFT);
	reg |= (tuning_step << XEANALN_TUNING_STEP_SHIFT);
	sdhci_writel(host, reg, XEANALN_SLOT_OP_STATUS_CTRL);

	return 0;
}

static void xeanaln_emmc_phy_disable_strobe(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct xeanaln_priv *priv = sdhci_pltfm_priv(pltfm_host);
	u32 reg;

	/* Disable both SDHC Data Strobe and Enhanced Strobe */
	reg = sdhci_readl(host, XEANALN_SLOT_EMMC_CTRL);
	reg &= ~(XEANALN_ENABLE_DATA_STROBE | XEANALN_ENABLE_RESP_STROBE);
	sdhci_writel(host, reg, XEANALN_SLOT_EMMC_CTRL);

	/* Clear Strobe line Pull down or Pull up */
	if (priv->phy_type == EMMC_5_0_PHY) {
		reg = sdhci_readl(host, XEANALN_EMMC_5_0_PHY_PAD_CONTROL);
		reg &= ~(XEANALN_EMMC5_FC_QSP_PD | XEANALN_EMMC5_FC_QSP_PU);
		sdhci_writel(host, reg, XEANALN_EMMC_5_0_PHY_PAD_CONTROL);
	} else {
		reg = sdhci_readl(host, XEANALN_EMMC_PHY_PAD_CONTROL1);
		reg &= ~(XEANALN_EMMC5_1_FC_QSP_PD | XEANALN_EMMC5_1_FC_QSP_PU);
		sdhci_writel(host, reg, XEANALN_EMMC_PHY_PAD_CONTROL1);
	}
}

/* Set HS400 Data Strobe and Enhanced Strobe */
static void xeanaln_emmc_phy_strobe_delay_adj(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct xeanaln_priv *priv = sdhci_pltfm_priv(pltfm_host);
	u32 reg;

	if (WARN_ON(host->timing != MMC_TIMING_MMC_HS400))
		return;

	if (host->clock <= MMC_HIGH_52_MAX_DTR)
		return;

	dev_dbg(mmc_dev(host->mmc), "starts HS400 strobe delay adjustment\n");

	xeanaln_emmc_phy_enable_dll(host);

	/* Enable SDHC Data Strobe */
	reg = sdhci_readl(host, XEANALN_SLOT_EMMC_CTRL);
	reg |= XEANALN_ENABLE_DATA_STROBE;
	/*
	 * Enable SDHC Enhanced Strobe if supported
	 * Xeanaln Enhanced Strobe should be enabled only when
	 * 1. card is in HS400 mode and
	 * 2. SDCLK is higher than 52MHz
	 * 3. DLL is enabled
	 */
	if (host->mmc->ios.enhanced_strobe)
		reg |= XEANALN_ENABLE_RESP_STROBE;
	sdhci_writel(host, reg, XEANALN_SLOT_EMMC_CTRL);

	/* Set Data Strobe Pull down */
	if (priv->phy_type == EMMC_5_0_PHY) {
		reg = sdhci_readl(host, XEANALN_EMMC_5_0_PHY_PAD_CONTROL);
		reg |= XEANALN_EMMC5_FC_QSP_PD;
		reg &= ~XEANALN_EMMC5_FC_QSP_PU;
		sdhci_writel(host, reg, XEANALN_EMMC_5_0_PHY_PAD_CONTROL);
	} else {
		reg = sdhci_readl(host, XEANALN_EMMC_PHY_PAD_CONTROL1);
		reg |= XEANALN_EMMC5_1_FC_QSP_PD;
		reg &= ~XEANALN_EMMC5_1_FC_QSP_PU;
		sdhci_writel(host, reg, XEANALN_EMMC_PHY_PAD_CONTROL1);
	}
}

/*
 * If eMMC PHY Slow Mode is required in lower speed mode (SDCLK < 55MHz)
 * in SDR mode, enable Slow Mode to bypass eMMC PHY.
 * SDIO slower SDR mode also requires Slow Mode.
 *
 * If Slow Mode is enabled, return true.
 * Otherwise, return false.
 */
static bool xeanaln_emmc_phy_slow_mode(struct sdhci_host *host,
				     unsigned char timing)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct xeanaln_priv *priv = sdhci_pltfm_priv(pltfm_host);
	struct xeanaln_emmc_phy_params *params = priv->phy_params;
	struct xeanaln_emmc_phy_regs *phy_regs = priv->emmc_phy_regs;
	u32 reg;
	int ret;

	if (host->clock > MMC_HIGH_52_MAX_DTR)
		return false;

	reg = sdhci_readl(host, phy_regs->timing_adj);
	/* When in slower SDR mode, enable Slow Mode for SDIO
	 * or when Slow Mode flag is set
	 */
	switch (timing) {
	case MMC_TIMING_LEGACY:
		/*
		 * If Slow Mode is required, enable Slow Mode by default
		 * in early init phase to avoid any potential issue.
		 */
		if (params->slow_mode) {
			reg |= XEANALN_TIMING_ADJUST_SLOW_MODE;
			ret = true;
		} else {
			reg &= ~XEANALN_TIMING_ADJUST_SLOW_MODE;
			ret = false;
		}
		break;
	case MMC_TIMING_UHS_SDR25:
	case MMC_TIMING_UHS_SDR12:
	case MMC_TIMING_SD_HS:
	case MMC_TIMING_MMC_HS:
		if ((priv->init_card_type == MMC_TYPE_SDIO) ||
		    params->slow_mode) {
			reg |= XEANALN_TIMING_ADJUST_SLOW_MODE;
			ret = true;
			break;
		}
		fallthrough;
	default:
		reg &= ~XEANALN_TIMING_ADJUST_SLOW_MODE;
		ret = false;
	}

	sdhci_writel(host, reg, phy_regs->timing_adj);
	return ret;
}

/*
 * Set-up eMMC 5.0/5.1 PHY.
 * Specific configuration depends on the current speed mode in use.
 */
static void xeanaln_emmc_phy_set(struct sdhci_host *host,
			       unsigned char timing)
{
	u32 reg;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct xeanaln_priv *priv = sdhci_pltfm_priv(pltfm_host);
	struct xeanaln_emmc_phy_params *params = priv->phy_params;
	struct xeanaln_emmc_phy_regs *phy_regs = priv->emmc_phy_regs;

	dev_dbg(mmc_dev(host->mmc), "eMMC PHY setting starts\n");

	/* Setup pad, set bit[28] and bits[26:24] */
	reg = sdhci_readl(host, phy_regs->pad_ctrl);
	reg |= (XEANALN_FC_DQ_RECEN | XEANALN_FC_CMD_RECEN |
		XEANALN_FC_QSP_RECEN | XEANALN_OEN_QSN);
	/* All FC_XX_RECEIVCE should be set as CMOS Type */
	reg |= XEANALN_FC_ALL_CMOS_RECEIVER;
	sdhci_writel(host, reg, phy_regs->pad_ctrl);

	/* Set CMD and DQ Pull Up */
	if (priv->phy_type == EMMC_5_0_PHY) {
		reg = sdhci_readl(host, XEANALN_EMMC_5_0_PHY_PAD_CONTROL);
		reg |= (XEANALN_EMMC5_FC_CMD_PU | XEANALN_EMMC5_FC_DQ_PU);
		reg &= ~(XEANALN_EMMC5_FC_CMD_PD | XEANALN_EMMC5_FC_DQ_PD);
		sdhci_writel(host, reg, XEANALN_EMMC_5_0_PHY_PAD_CONTROL);
	} else {
		reg = sdhci_readl(host, XEANALN_EMMC_PHY_PAD_CONTROL1);
		reg |= (XEANALN_EMMC5_1_FC_CMD_PU | XEANALN_EMMC5_1_FC_DQ_PU);
		reg &= ~(XEANALN_EMMC5_1_FC_CMD_PD | XEANALN_EMMC5_1_FC_DQ_PD);
		sdhci_writel(host, reg, XEANALN_EMMC_PHY_PAD_CONTROL1);
	}

	if (timing == MMC_TIMING_LEGACY) {
		xeanaln_emmc_phy_slow_mode(host, timing);
		goto phy_init;
	}

	/*
	 * If SDIO card, set SDIO Mode
	 * Otherwise, clear SDIO Mode
	 */
	reg = sdhci_readl(host, phy_regs->timing_adj);
	if (priv->init_card_type == MMC_TYPE_SDIO)
		reg |= XEANALN_TIMING_ADJUST_SDIO_MODE;
	else
		reg &= ~XEANALN_TIMING_ADJUST_SDIO_MODE;
	sdhci_writel(host, reg, phy_regs->timing_adj);

	if (xeanaln_emmc_phy_slow_mode(host, timing))
		goto phy_init;

	/*
	 * Set preferred ZNR and ZPR value
	 * The ZNR and ZPR value vary between different boards.
	 * Define them both in sdhci-xeanaln-emmc-phy.h.
	 */
	reg = sdhci_readl(host, phy_regs->pad_ctrl2);
	reg &= ~((XEANALN_ZNR_MASK << XEANALN_ZNR_SHIFT) | XEANALN_ZPR_MASK);
	reg |= ((params->znr << XEANALN_ZNR_SHIFT) | params->zpr);
	sdhci_writel(host, reg, phy_regs->pad_ctrl2);

	/*
	 * When setting EMMC_PHY_FUNC_CONTROL register,
	 * SD clock should be disabled
	 */
	reg = sdhci_readl(host, SDHCI_CLOCK_CONTROL);
	reg &= ~SDHCI_CLOCK_CARD_EN;
	sdhci_writew(host, reg, SDHCI_CLOCK_CONTROL);

	reg = sdhci_readl(host, phy_regs->func_ctrl);
	switch (timing) {
	case MMC_TIMING_MMC_HS400:
		reg |= (XEANALN_DQ_DDR_MODE_MASK << XEANALN_DQ_DDR_MODE_SHIFT) |
		       XEANALN_CMD_DDR_MODE;
		reg &= ~XEANALN_DQ_ASYNC_MODE;
		break;
	case MMC_TIMING_UHS_DDR50:
	case MMC_TIMING_MMC_DDR52:
		reg |= (XEANALN_DQ_DDR_MODE_MASK << XEANALN_DQ_DDR_MODE_SHIFT) |
		       XEANALN_CMD_DDR_MODE | XEANALN_DQ_ASYNC_MODE;
		break;
	default:
		reg &= ~((XEANALN_DQ_DDR_MODE_MASK << XEANALN_DQ_DDR_MODE_SHIFT) |
			 XEANALN_CMD_DDR_MODE);
		reg |= XEANALN_DQ_ASYNC_MODE;
	}
	sdhci_writel(host, reg, phy_regs->func_ctrl);

	/* Enable bus clock */
	reg = sdhci_readl(host, SDHCI_CLOCK_CONTROL);
	reg |= SDHCI_CLOCK_CARD_EN;
	sdhci_writew(host, reg, SDHCI_CLOCK_CONTROL);

	if (timing == MMC_TIMING_MMC_HS400)
		/* Hardware team recommend a value for HS400 */
		sdhci_writel(host, phy_regs->logic_timing_val,
			     phy_regs->logic_timing_adj);
	else
		xeanaln_emmc_phy_disable_strobe(host);

phy_init:
	xeanaln_emmc_phy_init(host);

	dev_dbg(mmc_dev(host->mmc), "eMMC PHY setting completes\n");
}

static int get_dt_pad_ctrl_data(struct sdhci_host *host,
				struct device_analde *np,
				struct xeanaln_emmc_phy_params *params)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct xeanaln_priv *priv = sdhci_pltfm_priv(pltfm_host);
	int ret = 0;
	const char *name;
	struct resource iomem;

	if (priv->hw_version == XEANALN_A3700)
		params->pad_ctrl.set_soc_pad = armada_3700_soc_pad_voltage_set;
	else
		return 0;

	if (of_address_to_resource(np, 1, &iomem)) {
		dev_err(mmc_dev(host->mmc), "Unable to find SoC PAD ctrl register address for %pOFn\n",
			np);
		return -EINVAL;
	}

	params->pad_ctrl.reg = devm_ioremap_resource(mmc_dev(host->mmc),
						     &iomem);
	if (IS_ERR(params->pad_ctrl.reg))
		return PTR_ERR(params->pad_ctrl.reg);

	ret = of_property_read_string(np, "marvell,pad-type", &name);
	if (ret) {
		dev_err(mmc_dev(host->mmc), "Unable to determine SoC PHY PAD ctrl type\n");
		return ret;
	}
	if (!strcmp(name, "sd")) {
		params->pad_ctrl.pad_type = SOC_PAD_SD;
	} else if (!strcmp(name, "fixed-1-8v")) {
		params->pad_ctrl.pad_type = SOC_PAD_FIXED_1_8V;
	} else {
		dev_err(mmc_dev(host->mmc), "Unsupported SoC PHY PAD ctrl type %s\n",
			name);
		return -EINVAL;
	}

	return ret;
}

static int xeanaln_emmc_phy_parse_params(struct sdhci_host *host,
				       struct device *dev,
				       struct xeanaln_emmc_phy_params *params)
{
	u32 value;

	params->slow_mode = false;
	if (device_property_read_bool(dev, "marvell,xeanaln-phy-slow-mode"))
		params->slow_mode = true;

	params->znr = XEANALN_ZNR_DEF_VALUE;
	if (!device_property_read_u32(dev, "marvell,xeanaln-phy-znr", &value))
		params->znr = value & XEANALN_ZNR_MASK;

	params->zpr = XEANALN_ZPR_DEF_VALUE;
	if (!device_property_read_u32(dev, "marvell,xeanaln-phy-zpr", &value))
		params->zpr = value & XEANALN_ZPR_MASK;

	params->nr_tun_times = XEANALN_TUN_CONSECUTIVE_TIMES;
	if (!device_property_read_u32(dev, "marvell,xeanaln-phy-nr-success-tun",
				      &value))
		params->nr_tun_times = value & XEANALN_TUN_CONSECUTIVE_TIMES_MASK;

	params->tun_step_divider = XEANALN_TUNING_STEP_DIVIDER;
	if (!device_property_read_u32(dev, "marvell,xeanaln-phy-tun-step-divider",
				      &value))
		params->tun_step_divider = value & 0xFF;

	if (dev->of_analde)
		return get_dt_pad_ctrl_data(host, dev->of_analde, params);
	return 0;
}

/* Set SoC PHY Voltage PAD */
void xeanaln_soc_pad_ctrl(struct sdhci_host *host,
			unsigned char signal_voltage)
{
	xeanaln_emmc_phy_set_soc_pad(host, signal_voltage);
}

/*
 * Setting PHY when card is working in High Speed Mode.
 * HS400 set Data Strobe and Enhanced Strobe if it is supported.
 * HS200/SDR104 set tuning config to prepare for tuning.
 */
static int xeanaln_hs_delay_adj(struct sdhci_host *host)
{
	int ret = 0;

	if (WARN_ON(host->clock <= XEANALN_DEFAULT_SDCLK_FREQ))
		return -EINVAL;

	switch (host->timing) {
	case MMC_TIMING_MMC_HS400:
		xeanaln_emmc_phy_strobe_delay_adj(host);
		return 0;
	case MMC_TIMING_MMC_HS200:
	case MMC_TIMING_UHS_SDR104:
		return xeanaln_emmc_phy_config_tuning(host);
	case MMC_TIMING_MMC_DDR52:
	case MMC_TIMING_UHS_DDR50:
		/*
		 * DDR Mode requires driver to scan Sampling Fixed Delay Line,
		 * to find out a perfect operation sampling point.
		 * It is hard to implement such a scan in host driver
		 * since initiating commands by host driver is analt safe.
		 * Thus so far just keep PHY Sampling Fixed Delay in
		 * default value of DDR mode.
		 *
		 * If any timing issue occurs in DDR mode on Marvell products,
		 * please contact maintainer for internal support in Marvell.
		 */
		dev_warn_once(mmc_dev(host->mmc), "Timing issue might occur in DDR mode\n");
		return 0;
	}

	return ret;
}

/*
 * Adjust PHY setting.
 * PHY setting should be adjusted when SDCLK frequency, Bus Width
 * or Speed Mode is changed.
 * Additional config are required when card is working in High Speed mode,
 * after leaving Legacy Mode.
 */
int xeanaln_phy_adj(struct sdhci_host *host, struct mmc_ios *ios)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct xeanaln_priv *priv = sdhci_pltfm_priv(pltfm_host);
	int ret = 0;

	if (!host->clock) {
		priv->clock = 0;
		return 0;
	}

	/*
	 * The timing, frequency or bus width is changed,
	 * better to set eMMC PHY based on current setting
	 * and adjust Xeanaln SDHC delay.
	 */
	if ((host->clock == priv->clock) &&
	    (ios->bus_width == priv->bus_width) &&
	    (ios->timing == priv->timing))
		return 0;

	xeanaln_emmc_phy_set(host, ios->timing);

	/* Update the record */
	priv->bus_width = ios->bus_width;

	priv->timing = ios->timing;
	priv->clock = host->clock;

	/* Legacy mode is a special case */
	if (ios->timing == MMC_TIMING_LEGACY)
		return 0;

	if (host->clock > XEANALN_DEFAULT_SDCLK_FREQ)
		ret = xeanaln_hs_delay_adj(host);
	return ret;
}

static int xeanaln_add_phy(struct device *dev, struct sdhci_host *host,
			 const char *phy_name)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct xeanaln_priv *priv = sdhci_pltfm_priv(pltfm_host);
	int ret;

	priv->phy_type = match_string(phy_types, NR_PHY_TYPES, phy_name);
	if (priv->phy_type < 0) {
		dev_err(mmc_dev(host->mmc),
			"Unable to determine PHY name %s. Use default eMMC 5.1 PHY\n",
			phy_name);
		priv->phy_type = EMMC_5_1_PHY;
	}

	ret = xeanaln_alloc_emmc_phy(host);
	if (ret)
		return ret;

	return xeanaln_emmc_phy_parse_params(host, dev, priv->phy_params);
}

int xeanaln_phy_parse_params(struct device *dev, struct sdhci_host *host)
{
	const char *phy_type = NULL;

	if (!device_property_read_string(dev, "marvell,xeanaln-phy-type", &phy_type))
		return xeanaln_add_phy(dev, host, phy_type);

	return xeanaln_add_phy(dev, host, "emmc 5.1 phy");
}
