// SPDX-License-Identifier: GPL-2.0-only
/*
 * Cadence Torrent SD0801 PHY driver.
 *
 * Copyright 2018 Cadence Design Systems, Inc.
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>

#define REF_CLK_19_2MHz		19200000
#define REF_CLK_25MHz		25000000

#define DEFAULT_NUM_LANES	4
#define MAX_NUM_LANES		4
#define DEFAULT_MAX_BIT_RATE	8100 /* in Mbps */

#define POLL_TIMEOUT_US		5000
#define LANE_MASK		0x7

/*
 * register offsets from DPTX PHY register block base (i.e MHDP
 * register base + 0x30a00)
 */
#define PHY_AUX_CONFIG			0x00
#define PHY_AUX_CTRL			0x04
#define PHY_RESET			0x20
#define PHY_PMA_XCVR_PLLCLK_EN		0x24
#define PHY_PMA_XCVR_PLLCLK_EN_ACK	0x28
#define PHY_PMA_XCVR_POWER_STATE_REQ	0x2c
#define PHY_POWER_STATE_LN_0	0x0000
#define PHY_POWER_STATE_LN_1	0x0008
#define PHY_POWER_STATE_LN_2	0x0010
#define PHY_POWER_STATE_LN_3	0x0018
#define PMA_XCVR_POWER_STATE_REQ_LN_MASK	0x3FU
#define PHY_PMA_XCVR_POWER_STATE_ACK	0x30
#define PHY_PMA_CMN_READY		0x34
#define PHY_PMA_XCVR_TX_VMARGIN		0x38
#define PHY_PMA_XCVR_TX_DEEMPH		0x3c

/*
 * register offsets from SD0801 PHY register block base (i.e MHDP
 * register base + 0x500000)
 */
#define CMN_SSM_BANDGAP_TMR		0x00084
#define CMN_SSM_BIAS_TMR		0x00088
#define CMN_PLLSM0_PLLPRE_TMR		0x000a8
#define CMN_PLLSM0_PLLLOCK_TMR		0x000b0
#define CMN_PLLSM1_PLLPRE_TMR		0x000c8
#define CMN_PLLSM1_PLLLOCK_TMR		0x000d0
#define CMN_BGCAL_INIT_TMR		0x00190
#define CMN_BGCAL_ITER_TMR		0x00194
#define CMN_IBCAL_INIT_TMR		0x001d0
#define CMN_PLL0_VCOCAL_TCTRL		0x00208
#define CMN_PLL0_VCOCAL_INIT_TMR	0x00210
#define CMN_PLL0_VCOCAL_ITER_TMR	0x00214
#define CMN_PLL0_VCOCAL_REFTIM_START	0x00218
#define CMN_PLL0_VCOCAL_PLLCNT_START	0x00220
#define CMN_PLL0_INTDIV_M0		0x00240
#define CMN_PLL0_FRACDIVL_M0		0x00244
#define CMN_PLL0_FRACDIVH_M0		0x00248
#define CMN_PLL0_HIGH_THR_M0		0x0024c
#define CMN_PLL0_DSM_DIAG_M0		0x00250
#define CMN_PLL0_SS_CTRL1_M0		0x00260
#define CMN_PLL0_SS_CTRL2_M0            0x00264
#define CMN_PLL0_SS_CTRL3_M0            0x00268
#define CMN_PLL0_SS_CTRL4_M0            0x0026C
#define CMN_PLL0_LOCK_REFCNT_START      0x00270
#define CMN_PLL0_LOCK_PLLCNT_START	0x00278
#define CMN_PLL0_LOCK_PLLCNT_THR        0x0027C
#define CMN_PLL1_VCOCAL_TCTRL		0x00308
#define CMN_PLL1_VCOCAL_INIT_TMR	0x00310
#define CMN_PLL1_VCOCAL_ITER_TMR	0x00314
#define CMN_PLL1_VCOCAL_REFTIM_START	0x00318
#define CMN_PLL1_VCOCAL_PLLCNT_START	0x00320
#define CMN_PLL1_INTDIV_M0		0x00340
#define CMN_PLL1_FRACDIVL_M0		0x00344
#define CMN_PLL1_FRACDIVH_M0		0x00348
#define CMN_PLL1_HIGH_THR_M0		0x0034c
#define CMN_PLL1_DSM_DIAG_M0		0x00350
#define CMN_PLL1_SS_CTRL1_M0		0x00360
#define CMN_PLL1_SS_CTRL2_M0            0x00364
#define CMN_PLL1_SS_CTRL3_M0            0x00368
#define CMN_PLL1_SS_CTRL4_M0            0x0036C
#define CMN_PLL1_LOCK_REFCNT_START      0x00370
#define CMN_PLL1_LOCK_PLLCNT_START	0x00378
#define CMN_PLL1_LOCK_PLLCNT_THR        0x0037C
#define CMN_TXPUCAL_INIT_TMR		0x00410
#define CMN_TXPUCAL_ITER_TMR		0x00414
#define CMN_TXPDCAL_INIT_TMR		0x00430
#define CMN_TXPDCAL_ITER_TMR		0x00434
#define CMN_RXCAL_INIT_TMR		0x00450
#define CMN_RXCAL_ITER_TMR		0x00454
#define CMN_SD_CAL_INIT_TMR		0x00490
#define CMN_SD_CAL_ITER_TMR		0x00494
#define CMN_SD_CAL_REFTIM_START		0x00498
#define CMN_SD_CAL_PLLCNT_START		0x004a0
#define CMN_PDIAG_PLL0_CTRL_M0		0x00680
#define CMN_PDIAG_PLL0_CLK_SEL_M0	0x00684
#define CMN_PDIAG_PLL0_CP_PADJ_M0	0x00690
#define CMN_PDIAG_PLL0_CP_IADJ_M0	0x00694
#define CMN_PDIAG_PLL0_FILT_PADJ_M0	0x00698
#define CMN_PDIAG_PLL0_CP_PADJ_M1	0x006d0
#define CMN_PDIAG_PLL0_CP_IADJ_M1	0x006d4
#define CMN_PDIAG_PLL1_CTRL_M0		0x00700
#define CMN_PDIAG_PLL1_CLK_SEL_M0	0x00704
#define CMN_PDIAG_PLL1_CP_PADJ_M0	0x00710
#define CMN_PDIAG_PLL1_CP_IADJ_M0	0x00714
#define CMN_PDIAG_PLL1_FILT_PADJ_M0	0x00718

#define XCVR_DIAG_PLLDRC_CTRL		0x10394
#define XCVR_DIAG_HSCLK_SEL		0x10398
#define XCVR_DIAG_HSCLK_DIV		0x1039c
#define XCVR_DIAG_BIDI_CTRL		0x103a8
#define TX_PSC_A0			0x10400
#define TX_PSC_A1			0x10404
#define TX_PSC_A2			0x10408
#define TX_PSC_A3			0x1040c
#define TX_RCVDET_ST_TMR		0x1048c
#define RX_PSC_A0			0x20000
#define RX_PSC_A1			0x20004
#define RX_PSC_A2			0x20008
#define RX_PSC_A3			0x2000c
#define RX_PSC_CAL			0x20018
#define RX_REE_GCSM1_CTRL		0x20420
#define RX_REE_GCSM2_CTRL		0x20440
#define RX_REE_PERGCSM_CTRL		0x20460

#define PHY_PLL_CFG			0x30038

struct cdns_torrent_phy {
	void __iomem *base;	/* DPTX registers base */
	void __iomem *sd_base; /* SD0801 registers base */
	u32 num_lanes; /* Number of lanes to use */
	u32 max_bit_rate; /* Maximum link bit rate to use (in Mbps) */
	struct device *dev;
	struct clk *clk;
	unsigned long ref_clk_rate;
};

enum phy_powerstate {
	POWERSTATE_A0 = 0,
	/* Powerstate A1 is unused */
	POWERSTATE_A2 = 2,
	POWERSTATE_A3 = 3,
};

static int cdns_torrent_dp_init(struct phy *phy);
static int cdns_torrent_dp_exit(struct phy *phy);
static int cdns_torrent_dp_run(struct cdns_torrent_phy *cdns_phy);
static
int cdns_torrent_dp_wait_pma_cmn_ready(struct cdns_torrent_phy *cdns_phy);
static void cdns_torrent_dp_pma_cfg(struct cdns_torrent_phy *cdns_phy);
static
void cdns_torrent_dp_pma_cmn_cfg_19_2mhz(struct cdns_torrent_phy *cdns_phy);
static
void cdns_torrent_dp_pma_cmn_vco_cfg_19_2mhz(struct cdns_torrent_phy *cdns_phy,
					     u32 rate, bool ssc);
static
void cdns_torrent_dp_pma_cmn_cfg_25mhz(struct cdns_torrent_phy *cdns_phy);
static
void cdns_torrent_dp_pma_cmn_vco_cfg_25mhz(struct cdns_torrent_phy *cdns_phy,
					   u32 rate, bool ssc);
static void cdns_torrent_dp_pma_lane_cfg(struct cdns_torrent_phy *cdns_phy,
					 unsigned int lane);
static void cdns_torrent_dp_pma_cmn_rate(struct cdns_torrent_phy *cdns_phy,
					 u32 rate, u32 num_lanes);
static void cdns_dp_phy_write_field(struct cdns_torrent_phy *cdns_phy,
				    unsigned int offset,
				    unsigned char start_bit,
				    unsigned char num_bits,
				    unsigned int val);

static const struct phy_ops cdns_torrent_phy_ops = {
	.init		= cdns_torrent_dp_init,
	.exit		= cdns_torrent_dp_exit,
	.owner		= THIS_MODULE,
};

/* PHY mmr access functions */

static void cdns_torrent_phy_write(struct cdns_torrent_phy *cdns_phy,
				   u32 offset, u32 val)
{
	writel(val, cdns_phy->sd_base + offset);
}

/* DPTX mmr access functions */

static void cdns_torrent_dp_write(struct cdns_torrent_phy *cdns_phy,
				  u32 offset, u32 val)
{
	writel(val, cdns_phy->base + offset);
}

static u32 cdns_torrent_dp_read(struct cdns_torrent_phy *cdns_phy, u32 offset)
{
	return readl(cdns_phy->base + offset);
}

#define cdns_torrent_dp_read_poll_timeout(cdns_phy, offset, val, cond, \
					  delay_us, timeout_us) \
	readl_poll_timeout((cdns_phy)->base + (offset), \
			   val, cond, delay_us, timeout_us)

/* Set power state A0 and PLL clock enable to 0 on enabled lanes. */
static void cdns_torrent_dp_set_a0_pll(struct cdns_torrent_phy *cdns_phy,
				       u32 num_lanes)
{
	u32 pwr_state = cdns_torrent_dp_read(cdns_phy,
					     PHY_PMA_XCVR_POWER_STATE_REQ);
	u32 pll_clk_en = cdns_torrent_dp_read(cdns_phy,
					      PHY_PMA_XCVR_PLLCLK_EN);

	/* Lane 0 is always enabled. */
	pwr_state &= ~(PMA_XCVR_POWER_STATE_REQ_LN_MASK <<
		       PHY_POWER_STATE_LN_0);
	pll_clk_en &= ~0x01U;

	if (num_lanes > 1) {
		/* lane 1 */
		pwr_state &= ~(PMA_XCVR_POWER_STATE_REQ_LN_MASK <<
			       PHY_POWER_STATE_LN_1);
		pll_clk_en &= ~(0x01U << 1);
	}

	if (num_lanes > 2) {
		/* lanes 2 and 3 */
		pwr_state &= ~(PMA_XCVR_POWER_STATE_REQ_LN_MASK <<
			       PHY_POWER_STATE_LN_2);
		pwr_state &= ~(PMA_XCVR_POWER_STATE_REQ_LN_MASK <<
			       PHY_POWER_STATE_LN_3);
		pll_clk_en &= ~(0x01U << 2);
		pll_clk_en &= ~(0x01U << 3);
	}

	cdns_torrent_dp_write(cdns_phy,
			      PHY_PMA_XCVR_POWER_STATE_REQ, pwr_state);
	cdns_torrent_dp_write(cdns_phy, PHY_PMA_XCVR_PLLCLK_EN, pll_clk_en);
}

static int cdns_torrent_dp_init(struct phy *phy)
{
	unsigned char lane_bits;
	int ret;

	struct cdns_torrent_phy *cdns_phy = phy_get_drvdata(phy);

	ret = clk_prepare_enable(cdns_phy->clk);
	if (ret) {
		dev_err(cdns_phy->dev, "Failed to prepare ref clock\n");
		return ret;
	}

	cdns_phy->ref_clk_rate = clk_get_rate(cdns_phy->clk);
	if (!(cdns_phy->ref_clk_rate)) {
		dev_err(cdns_phy->dev, "Failed to get ref clock rate\n");
		clk_disable_unprepare(cdns_phy->clk);
		return -EINVAL;
	}

	switch (cdns_phy->ref_clk_rate) {
	case REF_CLK_19_2MHz:
	case REF_CLK_25MHz:
		/* Valid Ref Clock Rate */
		break;
	default:
		dev_err(cdns_phy->dev, "Unsupported Ref Clock Rate\n");
		return -EINVAL;
	}

	cdns_torrent_dp_write(cdns_phy, PHY_AUX_CTRL, 0x0003); /* enable AUX */

	/* PHY PMA registers configuration function */
	cdns_torrent_dp_pma_cfg(cdns_phy);

	/*
	 * Set lines power state to A0
	 * Set lines pll clk enable to 0
	 */
	cdns_torrent_dp_set_a0_pll(cdns_phy, cdns_phy->num_lanes);

	/*
	 * release phy_l0*_reset_n and pma_tx_elec_idle_ln_* based on
	 * used lanes
	 */
	lane_bits = (1 << cdns_phy->num_lanes) - 1;
	cdns_torrent_dp_write(cdns_phy, PHY_RESET,
			      ((0xF & ~lane_bits) << 4) | (0xF & lane_bits));

	/* release pma_xcvr_pllclk_en_ln_*, only for the master lane */
	cdns_torrent_dp_write(cdns_phy, PHY_PMA_XCVR_PLLCLK_EN, 0x0001);

	/* PHY PMA registers configuration functions */
	/* Initialize PHY with max supported link rate, without SSC. */
	if (cdns_phy->ref_clk_rate == REF_CLK_19_2MHz)
		cdns_torrent_dp_pma_cmn_vco_cfg_19_2mhz(cdns_phy,
							cdns_phy->max_bit_rate,
							false);
	else if (cdns_phy->ref_clk_rate == REF_CLK_25MHz)
		cdns_torrent_dp_pma_cmn_vco_cfg_25mhz(cdns_phy,
						      cdns_phy->max_bit_rate,
						      false);
	cdns_torrent_dp_pma_cmn_rate(cdns_phy, cdns_phy->max_bit_rate,
				     cdns_phy->num_lanes);

	/* take out of reset */
	cdns_dp_phy_write_field(cdns_phy, PHY_RESET, 8, 1, 1);
	ret = cdns_torrent_dp_wait_pma_cmn_ready(cdns_phy);
	if (ret)
		return ret;

	ret = cdns_torrent_dp_run(cdns_phy);

	return ret;
}

static int cdns_torrent_dp_exit(struct phy *phy)
{
	struct cdns_torrent_phy *cdns_phy = phy_get_drvdata(phy);

	clk_disable_unprepare(cdns_phy->clk);
	return 0;
}

static
int cdns_torrent_dp_wait_pma_cmn_ready(struct cdns_torrent_phy *cdns_phy)
{
	unsigned int reg;
	int ret;

	ret = cdns_torrent_dp_read_poll_timeout(cdns_phy, PHY_PMA_CMN_READY,
						reg, reg & 1, 0,
						POLL_TIMEOUT_US);
	if (ret == -ETIMEDOUT) {
		dev_err(cdns_phy->dev,
			"timeout waiting for PMA common ready\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static void cdns_torrent_dp_pma_cfg(struct cdns_torrent_phy *cdns_phy)
{
	unsigned int i;

	if (cdns_phy->ref_clk_rate == REF_CLK_19_2MHz)
		/* PMA common configuration 19.2MHz */
		cdns_torrent_dp_pma_cmn_cfg_19_2mhz(cdns_phy);
	else if (cdns_phy->ref_clk_rate == REF_CLK_25MHz)
		/* PMA common configuration 25MHz */
		cdns_torrent_dp_pma_cmn_cfg_25mhz(cdns_phy);

	/* PMA lane configuration to deal with multi-link operation */
	for (i = 0; i < cdns_phy->num_lanes; i++)
		cdns_torrent_dp_pma_lane_cfg(cdns_phy, i);
}

static
void cdns_torrent_dp_pma_cmn_cfg_19_2mhz(struct cdns_torrent_phy *cdns_phy)
{
	/* refclock registers - assumes 19.2 MHz refclock */
	cdns_torrent_phy_write(cdns_phy, CMN_SSM_BIAS_TMR, 0x0014);
	cdns_torrent_phy_write(cdns_phy, CMN_PLLSM0_PLLPRE_TMR, 0x0027);
	cdns_torrent_phy_write(cdns_phy, CMN_PLLSM0_PLLLOCK_TMR, 0x00A1);
	cdns_torrent_phy_write(cdns_phy, CMN_PLLSM1_PLLPRE_TMR, 0x0027);
	cdns_torrent_phy_write(cdns_phy, CMN_PLLSM1_PLLLOCK_TMR, 0x00A1);
	cdns_torrent_phy_write(cdns_phy, CMN_BGCAL_INIT_TMR, 0x0060);
	cdns_torrent_phy_write(cdns_phy, CMN_BGCAL_ITER_TMR, 0x0060);
	cdns_torrent_phy_write(cdns_phy, CMN_IBCAL_INIT_TMR, 0x0014);
	cdns_torrent_phy_write(cdns_phy, CMN_TXPUCAL_INIT_TMR, 0x0018);
	cdns_torrent_phy_write(cdns_phy, CMN_TXPUCAL_ITER_TMR, 0x0005);
	cdns_torrent_phy_write(cdns_phy, CMN_TXPDCAL_INIT_TMR, 0x0018);
	cdns_torrent_phy_write(cdns_phy, CMN_TXPDCAL_ITER_TMR, 0x0005);
	cdns_torrent_phy_write(cdns_phy, CMN_RXCAL_INIT_TMR, 0x0240);
	cdns_torrent_phy_write(cdns_phy, CMN_RXCAL_ITER_TMR, 0x0005);
	cdns_torrent_phy_write(cdns_phy, CMN_SD_CAL_INIT_TMR, 0x0002);
	cdns_torrent_phy_write(cdns_phy, CMN_SD_CAL_ITER_TMR, 0x0002);
	cdns_torrent_phy_write(cdns_phy, CMN_SD_CAL_REFTIM_START, 0x000B);
	cdns_torrent_phy_write(cdns_phy, CMN_SD_CAL_PLLCNT_START, 0x0137);

	/* PLL registers */
	cdns_torrent_phy_write(cdns_phy, CMN_PDIAG_PLL0_CP_PADJ_M0, 0x0509);
	cdns_torrent_phy_write(cdns_phy, CMN_PDIAG_PLL0_CP_IADJ_M0, 0x0F00);
	cdns_torrent_phy_write(cdns_phy, CMN_PDIAG_PLL0_FILT_PADJ_M0, 0x0F08);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL0_DSM_DIAG_M0, 0x0004);
	cdns_torrent_phy_write(cdns_phy, CMN_PDIAG_PLL1_CP_PADJ_M0, 0x0509);
	cdns_torrent_phy_write(cdns_phy, CMN_PDIAG_PLL1_CP_IADJ_M0, 0x0F00);
	cdns_torrent_phy_write(cdns_phy, CMN_PDIAG_PLL1_FILT_PADJ_M0, 0x0F08);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL1_DSM_DIAG_M0, 0x0004);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL0_VCOCAL_INIT_TMR, 0x00C0);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL0_VCOCAL_ITER_TMR, 0x0004);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL1_VCOCAL_INIT_TMR, 0x00C0);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL1_VCOCAL_ITER_TMR, 0x0004);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL0_VCOCAL_REFTIM_START, 0x0260);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL0_VCOCAL_TCTRL, 0x0003);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL1_VCOCAL_REFTIM_START, 0x0260);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL1_VCOCAL_TCTRL, 0x0003);
}

/*
 * Set registers responsible for enabling and configuring SSC, with second and
 * third register values provided by parameters.
 */
static
void cdns_torrent_dp_enable_ssc_19_2mhz(struct cdns_torrent_phy *cdns_phy,
					u32 ctrl2_val, u32 ctrl3_val)
{
	cdns_torrent_phy_write(cdns_phy, CMN_PLL0_SS_CTRL1_M0, 0x0001);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL0_SS_CTRL1_M0, ctrl2_val);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL0_SS_CTRL1_M0, ctrl3_val);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL0_SS_CTRL4_M0, 0x0003);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL1_SS_CTRL1_M0, 0x0001);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL1_SS_CTRL1_M0, ctrl2_val);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL1_SS_CTRL1_M0, ctrl3_val);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL1_SS_CTRL4_M0, 0x0003);
}

static
void cdns_torrent_dp_pma_cmn_vco_cfg_19_2mhz(struct cdns_torrent_phy *cdns_phy,
					     u32 rate, bool ssc)
{
	/* Assumes 19.2 MHz refclock */
	switch (rate) {
	/* Setting VCO for 10.8GHz */
	case 2700:
	case 5400:
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL0_INTDIV_M0, 0x0119);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL0_FRACDIVL_M0, 0x4000);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL0_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL0_HIGH_THR_M0, 0x00BC);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PDIAG_PLL0_CTRL_M0, 0x0012);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL1_INTDIV_M0, 0x0119);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL1_FRACDIVL_M0, 0x4000);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL1_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL1_HIGH_THR_M0, 0x00BC);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PDIAG_PLL1_CTRL_M0, 0x0012);
		if (ssc)
			cdns_torrent_dp_enable_ssc_19_2mhz(cdns_phy, 0x033A,
							   0x006A);
		break;
	/* Setting VCO for 9.72GHz */
	case 1620:
	case 2430:
	case 3240:
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL0_INTDIV_M0, 0x01FA);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL0_FRACDIVL_M0, 0x4000);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL0_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL0_HIGH_THR_M0, 0x0152);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PDIAG_PLL0_CTRL_M0, 0x0002);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL1_INTDIV_M0, 0x01FA);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL1_FRACDIVL_M0, 0x4000);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL1_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL1_HIGH_THR_M0, 0x0152);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PDIAG_PLL1_CTRL_M0, 0x0002);
		if (ssc)
			cdns_torrent_dp_enable_ssc_19_2mhz(cdns_phy, 0x05DD,
							   0x0069);
		break;
	/* Setting VCO for 8.64GHz */
	case 2160:
	case 4320:
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL0_INTDIV_M0, 0x01C2);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL0_FRACDIVL_M0, 0x0000);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL0_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL0_HIGH_THR_M0, 0x012C);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PDIAG_PLL0_CTRL_M0, 0x0002);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL1_INTDIV_M0, 0x01C2);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL1_FRACDIVL_M0, 0x0000);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL1_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL1_HIGH_THR_M0, 0x012C);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PDIAG_PLL1_CTRL_M0, 0x0002);
		if (ssc)
			cdns_torrent_dp_enable_ssc_19_2mhz(cdns_phy, 0x0536,
							   0x0069);
		break;
	/* Setting VCO for 8.1GHz */
	case 8100:
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL0_INTDIV_M0, 0x01A5);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL0_FRACDIVL_M0, 0xE000);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL0_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL0_HIGH_THR_M0, 0x011A);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PDIAG_PLL0_CTRL_M0, 0x0002);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL1_INTDIV_M0, 0x01A5);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL1_FRACDIVL_M0, 0xE000);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL1_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL1_HIGH_THR_M0, 0x011A);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PDIAG_PLL1_CTRL_M0, 0x0002);
		if (ssc)
			cdns_torrent_dp_enable_ssc_19_2mhz(cdns_phy, 0x04D7,
							   0x006A);
		break;
	}

	if (ssc) {
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL0_VCOCAL_PLLCNT_START, 0x025E);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL0_LOCK_PLLCNT_THR, 0x0005);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL1_VCOCAL_PLLCNT_START, 0x025E);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL1_LOCK_PLLCNT_THR, 0x0005);
	} else {
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL0_VCOCAL_PLLCNT_START, 0x0260);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL1_VCOCAL_PLLCNT_START, 0x0260);
		/* Set reset register values to disable SSC */
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL0_SS_CTRL1_M0, 0x0002);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL0_SS_CTRL2_M0, 0x0000);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL0_SS_CTRL3_M0, 0x0000);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL0_SS_CTRL4_M0, 0x0000);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL0_LOCK_PLLCNT_THR, 0x0003);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL1_SS_CTRL1_M0, 0x0002);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL1_SS_CTRL2_M0, 0x0000);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL1_SS_CTRL3_M0, 0x0000);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL1_SS_CTRL4_M0, 0x0000);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL1_LOCK_PLLCNT_THR, 0x0003);
	}

	cdns_torrent_phy_write(cdns_phy, CMN_PLL0_LOCK_REFCNT_START, 0x0099);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL0_LOCK_PLLCNT_START, 0x0099);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL1_LOCK_REFCNT_START, 0x0099);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL1_LOCK_PLLCNT_START, 0x0099);
}

static
void cdns_torrent_dp_pma_cmn_cfg_25mhz(struct cdns_torrent_phy *cdns_phy)
{
	/* refclock registers - assumes 25 MHz refclock */
	cdns_torrent_phy_write(cdns_phy, CMN_SSM_BIAS_TMR, 0x0019);
	cdns_torrent_phy_write(cdns_phy, CMN_PLLSM0_PLLPRE_TMR, 0x0032);
	cdns_torrent_phy_write(cdns_phy, CMN_PLLSM0_PLLLOCK_TMR, 0x00D1);
	cdns_torrent_phy_write(cdns_phy, CMN_PLLSM1_PLLPRE_TMR, 0x0032);
	cdns_torrent_phy_write(cdns_phy, CMN_PLLSM1_PLLLOCK_TMR, 0x00D1);
	cdns_torrent_phy_write(cdns_phy, CMN_BGCAL_INIT_TMR, 0x007D);
	cdns_torrent_phy_write(cdns_phy, CMN_BGCAL_ITER_TMR, 0x007D);
	cdns_torrent_phy_write(cdns_phy, CMN_IBCAL_INIT_TMR, 0x0019);
	cdns_torrent_phy_write(cdns_phy, CMN_TXPUCAL_INIT_TMR, 0x001E);
	cdns_torrent_phy_write(cdns_phy, CMN_TXPUCAL_ITER_TMR, 0x0006);
	cdns_torrent_phy_write(cdns_phy, CMN_TXPDCAL_INIT_TMR, 0x001E);
	cdns_torrent_phy_write(cdns_phy, CMN_TXPDCAL_ITER_TMR, 0x0006);
	cdns_torrent_phy_write(cdns_phy, CMN_RXCAL_INIT_TMR, 0x02EE);
	cdns_torrent_phy_write(cdns_phy, CMN_RXCAL_ITER_TMR, 0x0006);
	cdns_torrent_phy_write(cdns_phy, CMN_SD_CAL_INIT_TMR, 0x0002);
	cdns_torrent_phy_write(cdns_phy, CMN_SD_CAL_ITER_TMR, 0x0002);
	cdns_torrent_phy_write(cdns_phy, CMN_SD_CAL_REFTIM_START, 0x000E);
	cdns_torrent_phy_write(cdns_phy, CMN_SD_CAL_PLLCNT_START, 0x012B);

	/* PLL registers */
	cdns_torrent_phy_write(cdns_phy, CMN_PDIAG_PLL0_CP_PADJ_M0, 0x0509);
	cdns_torrent_phy_write(cdns_phy, CMN_PDIAG_PLL0_CP_IADJ_M0, 0x0F00);
	cdns_torrent_phy_write(cdns_phy, CMN_PDIAG_PLL0_FILT_PADJ_M0, 0x0F08);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL0_DSM_DIAG_M0, 0x0004);
	cdns_torrent_phy_write(cdns_phy, CMN_PDIAG_PLL1_CP_PADJ_M0, 0x0509);
	cdns_torrent_phy_write(cdns_phy, CMN_PDIAG_PLL1_CP_IADJ_M0, 0x0F00);
	cdns_torrent_phy_write(cdns_phy, CMN_PDIAG_PLL1_FILT_PADJ_M0, 0x0F08);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL1_DSM_DIAG_M0, 0x0004);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL0_VCOCAL_INIT_TMR, 0x00FA);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL0_VCOCAL_ITER_TMR, 0x0004);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL1_VCOCAL_INIT_TMR, 0x00FA);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL1_VCOCAL_ITER_TMR, 0x0004);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL0_VCOCAL_REFTIM_START, 0x0317);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL0_VCOCAL_TCTRL, 0x0003);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL1_VCOCAL_REFTIM_START, 0x0317);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL1_VCOCAL_TCTRL, 0x0003);
}

/*
 * Set registers responsible for enabling and configuring SSC, with second
 * register value provided by a parameter.
 */
static void cdns_torrent_dp_enable_ssc_25mhz(struct cdns_torrent_phy *cdns_phy,
					     u32 ctrl2_val)
{
	cdns_torrent_phy_write(cdns_phy, CMN_PLL0_SS_CTRL1_M0, 0x0001);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL0_SS_CTRL1_M0, ctrl2_val);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL0_SS_CTRL1_M0, 0x007F);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL0_SS_CTRL4_M0, 0x0003);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL1_SS_CTRL1_M0, 0x0001);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL1_SS_CTRL1_M0, ctrl2_val);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL1_SS_CTRL1_M0, 0x007F);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL1_SS_CTRL4_M0, 0x0003);
}

static
void cdns_torrent_dp_pma_cmn_vco_cfg_25mhz(struct cdns_torrent_phy *cdns_phy,
					   u32 rate, bool ssc)
{
	/* Assumes 25 MHz refclock */
	switch (rate) {
	/* Setting VCO for 10.8GHz */
	case 2700:
	case 5400:
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_INTDIV_M0, 0x01B0);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_FRACDIVL_M0, 0x0000);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_HIGH_THR_M0, 0x0120);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL1_INTDIV_M0, 0x01B0);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL1_FRACDIVL_M0, 0x0000);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL1_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL1_HIGH_THR_M0, 0x0120);
		if (ssc)
			cdns_torrent_dp_enable_ssc_25mhz(cdns_phy, 0x0423);
		break;
	/* Setting VCO for 9.72GHz */
	case 1620:
	case 2430:
	case 3240:
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_INTDIV_M0, 0x0184);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_FRACDIVL_M0, 0xCCCD);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_HIGH_THR_M0, 0x0104);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL1_INTDIV_M0, 0x0184);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL1_FRACDIVL_M0, 0xCCCD);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL1_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL1_HIGH_THR_M0, 0x0104);
		if (ssc)
			cdns_torrent_dp_enable_ssc_25mhz(cdns_phy, 0x03B9);
		break;
	/* Setting VCO for 8.64GHz */
	case 2160:
	case 4320:
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_INTDIV_M0, 0x0159);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_FRACDIVL_M0, 0x999A);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_HIGH_THR_M0, 0x00E7);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL1_INTDIV_M0, 0x0159);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL1_FRACDIVL_M0, 0x999A);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL1_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL1_HIGH_THR_M0, 0x00E7);
		if (ssc)
			cdns_torrent_dp_enable_ssc_25mhz(cdns_phy, 0x034F);
		break;
	/* Setting VCO for 8.1GHz */
	case 8100:
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_INTDIV_M0, 0x0144);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_FRACDIVL_M0, 0x0000);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_HIGH_THR_M0, 0x00D8);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL1_INTDIV_M0, 0x0144);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL1_FRACDIVL_M0, 0x0000);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL1_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL1_HIGH_THR_M0, 0x00D8);
		if (ssc)
			cdns_torrent_dp_enable_ssc_25mhz(cdns_phy, 0x031A);
		break;
	}

	cdns_torrent_phy_write(cdns_phy, CMN_PDIAG_PLL0_CTRL_M0, 0x0002);
	cdns_torrent_phy_write(cdns_phy, CMN_PDIAG_PLL1_CTRL_M0, 0x0002);

	if (ssc) {
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL0_VCOCAL_PLLCNT_START, 0x0315);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL0_LOCK_PLLCNT_THR, 0x0005);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL1_VCOCAL_PLLCNT_START, 0x0315);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL1_LOCK_PLLCNT_THR, 0x0005);
	} else {
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL0_VCOCAL_PLLCNT_START, 0x0317);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL1_VCOCAL_PLLCNT_START, 0x0317);
		/* Set reset register values to disable SSC */
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_SS_CTRL1_M0, 0x0002);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_SS_CTRL2_M0, 0x0000);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_SS_CTRL3_M0, 0x0000);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_SS_CTRL4_M0, 0x0000);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL0_LOCK_PLLCNT_THR, 0x0003);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL1_SS_CTRL1_M0, 0x0002);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL1_SS_CTRL2_M0, 0x0000);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL1_SS_CTRL3_M0, 0x0000);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL1_SS_CTRL4_M0, 0x0000);
		cdns_torrent_phy_write(cdns_phy,
				       CMN_PLL1_LOCK_PLLCNT_THR, 0x0003);
	}

	cdns_torrent_phy_write(cdns_phy, CMN_PLL0_LOCK_REFCNT_START, 0x00C7);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL0_LOCK_PLLCNT_START, 0x00C7);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL1_LOCK_REFCNT_START, 0x00C7);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL1_LOCK_PLLCNT_START, 0x00C7);
}

static void cdns_torrent_dp_pma_cmn_rate(struct cdns_torrent_phy *cdns_phy,
					 u32 rate, u32 num_lanes)
{
	unsigned int clk_sel_val = 0;
	unsigned int hsclk_div_val = 0;
	unsigned int i;

	/* 16'h0000 for single DP link configuration */
	cdns_torrent_phy_write(cdns_phy, PHY_PLL_CFG, 0x0000);

	switch (rate) {
	case 1620:
		clk_sel_val = 0x0f01;
		hsclk_div_val = 2;
		break;
	case 2160:
	case 2430:
	case 2700:
		clk_sel_val = 0x0701;
		hsclk_div_val = 1;
		break;
	case 3240:
		clk_sel_val = 0x0b00;
		hsclk_div_val = 2;
		break;
	case 4320:
	case 5400:
		clk_sel_val = 0x0301;
		hsclk_div_val = 0;
		break;
	case 8100:
		clk_sel_val = 0x0200;
		hsclk_div_val = 0;
		break;
	}

	cdns_torrent_phy_write(cdns_phy,
			       CMN_PDIAG_PLL0_CLK_SEL_M0, clk_sel_val);
	cdns_torrent_phy_write(cdns_phy,
			       CMN_PDIAG_PLL1_CLK_SEL_M0, clk_sel_val);

	/* PMA lane configuration to deal with multi-link operation */
	for (i = 0; i < num_lanes; i++)
		cdns_torrent_phy_write(cdns_phy,
				       (XCVR_DIAG_HSCLK_DIV | (i << 11)),
				       hsclk_div_val);
}

static void cdns_torrent_dp_pma_lane_cfg(struct cdns_torrent_phy *cdns_phy,
					 unsigned int lane)
{
	unsigned int lane_bits = (lane & LANE_MASK) << 11;

	/* Per lane, refclock-dependent receiver detection setting */
	if (cdns_phy->ref_clk_rate == REF_CLK_19_2MHz)
		cdns_torrent_phy_write(cdns_phy,
				       (TX_RCVDET_ST_TMR | lane_bits), 0x0780);
	else if (cdns_phy->ref_clk_rate == REF_CLK_25MHz)
		cdns_torrent_phy_write(cdns_phy,
				       (TX_RCVDET_ST_TMR | lane_bits), 0x09C4);

	/* Writing Tx/Rx Power State Controllers registers */
	cdns_torrent_phy_write(cdns_phy, (TX_PSC_A0 | lane_bits), 0x00FB);
	cdns_torrent_phy_write(cdns_phy, (TX_PSC_A2 | lane_bits), 0x04AA);
	cdns_torrent_phy_write(cdns_phy, (TX_PSC_A3 | lane_bits), 0x04AA);
	cdns_torrent_phy_write(cdns_phy, (RX_PSC_A0 | lane_bits), 0x0000);
	cdns_torrent_phy_write(cdns_phy, (RX_PSC_A2 | lane_bits), 0x0000);
	cdns_torrent_phy_write(cdns_phy, (RX_PSC_A3 | lane_bits), 0x0000);

	cdns_torrent_phy_write(cdns_phy, (RX_PSC_CAL | lane_bits), 0x0000);

	cdns_torrent_phy_write(cdns_phy,
			       (RX_REE_GCSM1_CTRL | lane_bits), 0x0000);
	cdns_torrent_phy_write(cdns_phy,
			       (RX_REE_GCSM2_CTRL | lane_bits), 0x0000);
	cdns_torrent_phy_write(cdns_phy,
			       (RX_REE_PERGCSM_CTRL | lane_bits), 0x0000);

	cdns_torrent_phy_write(cdns_phy,
			       (XCVR_DIAG_BIDI_CTRL | lane_bits), 0x000F);
	cdns_torrent_phy_write(cdns_phy,
			       (XCVR_DIAG_PLLDRC_CTRL | lane_bits), 0x0001);
	cdns_torrent_phy_write(cdns_phy,
			       (XCVR_DIAG_HSCLK_SEL | lane_bits), 0x0000);
}

static int cdns_torrent_dp_set_power_state(struct cdns_torrent_phy *cdns_phy,
					   u32 num_lanes,
					   enum phy_powerstate powerstate)
{
	/* Register value for power state for a single byte. */
	u32 value_part;
	u32 value;
	u32 mask;
	u32 read_val;
	u32 ret;

	switch (powerstate) {
	case (POWERSTATE_A0):
		value_part = 0x01U;
		break;
	case (POWERSTATE_A2):
		value_part = 0x04U;
		break;
	default:
		/* Powerstate A3 */
		value_part = 0x08U;
		break;
	}

	/* Select values of registers and mask, depending on enabled
	 * lane count.
	 */
	switch (num_lanes) {
	/* lane 0 */
	case (1):
		value = value_part;
		mask = 0x0000003FU;
		break;
	/* lanes 0-1 */
	case (2):
		value = (value_part
			 | (value_part << 8));
		mask = 0x00003F3FU;
		break;
	/* lanes 0-3, all */
	default:
		value = (value_part
			 | (value_part << 8)
			 | (value_part << 16)
			 | (value_part << 24));
		mask = 0x3F3F3F3FU;
		break;
	}

	/* Set power state A<n>. */
	cdns_torrent_dp_write(cdns_phy, PHY_PMA_XCVR_POWER_STATE_REQ, value);
	/* Wait, until PHY acknowledges power state completion. */
	ret = cdns_torrent_dp_read_poll_timeout(cdns_phy,
						PHY_PMA_XCVR_POWER_STATE_ACK,
						read_val,
						(read_val & mask) == value, 0,
						POLL_TIMEOUT_US);
	cdns_torrent_dp_write(cdns_phy,
			      PHY_PMA_XCVR_POWER_STATE_REQ, 0x00000000);
	ndelay(100);

	return ret;
}

static int cdns_torrent_dp_run(struct cdns_torrent_phy *cdns_phy)
{
	unsigned int read_val;
	int ret;

	/*
	 * waiting for ACK of pma_xcvr_pllclk_en_ln_*, only for the
	 * master lane
	 */
	ret = cdns_torrent_dp_read_poll_timeout(cdns_phy,
						PHY_PMA_XCVR_PLLCLK_EN_ACK,
						read_val, read_val & 1, 0,
						POLL_TIMEOUT_US);
	if (ret == -ETIMEDOUT) {
		dev_err(cdns_phy->dev,
			"timeout waiting for link PLL clock enable ack\n");
		return ret;
	}

	ndelay(100);

	ret = cdns_torrent_dp_set_power_state(cdns_phy, cdns_phy->num_lanes,
					      POWERSTATE_A2);
	if (ret)
		return ret;

	ret = cdns_torrent_dp_set_power_state(cdns_phy, cdns_phy->num_lanes,
					      POWERSTATE_A0);

	return ret;
}

static void cdns_dp_phy_write_field(struct cdns_torrent_phy *cdns_phy,
				    unsigned int offset,
				    unsigned char start_bit,
				    unsigned char num_bits,
				    unsigned int val)
{
	unsigned int read_val;

	read_val = cdns_torrent_dp_read(cdns_phy, offset);
	cdns_torrent_dp_write(cdns_phy, offset,
			      ((val << start_bit) |
			      (read_val & ~(((1 << num_bits) - 1) <<
			      start_bit))));
}

static int cdns_torrent_phy_probe(struct platform_device *pdev)
{
	struct resource *regs;
	struct cdns_torrent_phy *cdns_phy;
	struct device *dev = &pdev->dev;
	struct phy_provider *phy_provider;
	struct phy *phy;
	int err;

	cdns_phy = devm_kzalloc(dev, sizeof(*cdns_phy), GFP_KERNEL);
	if (!cdns_phy)
		return -ENOMEM;

	cdns_phy->dev = &pdev->dev;

	phy = devm_phy_create(dev, NULL, &cdns_torrent_phy_ops);
	if (IS_ERR(phy)) {
		dev_err(dev, "failed to create Torrent PHY\n");
		return PTR_ERR(phy);
	}

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	cdns_phy->base = devm_ioremap_resource(&pdev->dev, regs);
	if (IS_ERR(cdns_phy->base))
		return PTR_ERR(cdns_phy->base);

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	cdns_phy->sd_base = devm_ioremap_resource(&pdev->dev, regs);
	if (IS_ERR(cdns_phy->sd_base))
		return PTR_ERR(cdns_phy->sd_base);

	err = device_property_read_u32(dev, "num_lanes",
				       &cdns_phy->num_lanes);
	if (err)
		cdns_phy->num_lanes = DEFAULT_NUM_LANES;

	switch (cdns_phy->num_lanes) {
	case 1:
	case 2:
	case 4:
		/* valid number of lanes */
		break;
	default:
		dev_err(dev, "unsupported number of lanes: %d\n",
			cdns_phy->num_lanes);
		return -EINVAL;
	}

	err = device_property_read_u32(dev, "max_bit_rate",
				       &cdns_phy->max_bit_rate);
	if (err)
		cdns_phy->max_bit_rate = DEFAULT_MAX_BIT_RATE;

	switch (cdns_phy->max_bit_rate) {
	case 1620:
	case 2160:
	case 2430:
	case 2700:
	case 3240:
	case 4320:
	case 5400:
	case 8100:
		/* valid bit rate */
		break;
	default:
		dev_err(dev, "unsupported max bit rate: %dMbps\n",
			cdns_phy->max_bit_rate);
		return -EINVAL;
	}

	cdns_phy->clk = devm_clk_get(dev, "refclk");
	if (IS_ERR(cdns_phy->clk)) {
		dev_err(dev, "phy ref clock not found\n");
		return PTR_ERR(cdns_phy->clk);
	}

	phy_set_drvdata(phy, cdns_phy);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	dev_info(dev, "%d lanes, max bit rate %d.%03d Gbps\n",
		 cdns_phy->num_lanes,
		 cdns_phy->max_bit_rate / 1000,
		 cdns_phy->max_bit_rate % 1000);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id cdns_torrent_phy_of_match[] = {
	{
		.compatible = "cdns,torrent-phy"
	},
	{}
};
MODULE_DEVICE_TABLE(of, cdns_torrent_phy_of_match);

static struct platform_driver cdns_torrent_phy_driver = {
	.probe	= cdns_torrent_phy_probe,
	.driver = {
		.name	= "cdns-torrent-phy",
		.of_match_table	= cdns_torrent_phy_of_match,
	}
};
module_platform_driver(cdns_torrent_phy_driver);

MODULE_AUTHOR("Cadence Design Systems, Inc.");
MODULE_DESCRIPTION("Cadence Torrent PHY driver");
MODULE_LICENSE("GPL v2");
