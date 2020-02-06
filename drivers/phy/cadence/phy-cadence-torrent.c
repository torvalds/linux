// SPDX-License-Identifier: GPL-2.0-only
/*
 * Cadence Torrent SD0801 PHY driver.
 *
 * Copyright 2018 Cadence Design Systems, Inc.
 *
 */

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

#define DEFAULT_NUM_LANES	2
#define MAX_NUM_LANES		4
#define DEFAULT_MAX_BIT_RATE	8100 /* in Mbps */

#define POLL_TIMEOUT_US		2000
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
#define CMN_PLL0_VCOCAL_INIT_TMR	0x00210
#define CMN_PLL0_VCOCAL_ITER_TMR	0x00214
#define CMN_PLL0_VCOCAL_REFTIM_START	0x00218
#define CMN_PLL0_VCOCAL_PLLCNT_START	0x00220
#define CMN_PLL0_INTDIV_M0		0x00240
#define CMN_PLL0_FRACDIVL_M0		0x00244
#define CMN_PLL0_FRACDIVH_M0		0x00248
#define CMN_PLL0_HIGH_THR_M0		0x0024c
#define CMN_PLL0_DSM_DIAG_M0		0x00250
#define CMN_PLL0_LOCK_PLLCNT_START	0x00278
#define CMN_PLL1_VCOCAL_INIT_TMR	0x00310
#define CMN_PLL1_VCOCAL_ITER_TMR	0x00314
#define CMN_PLL1_DSM_DIAG_M0		0x00350
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
#define CMN_PDIAG_PLL1_CLK_SEL_M0	0x00704
#define XCVR_DIAG_PLLDRC_CTRL		0x10394
#define XCVR_DIAG_HSCLK_SEL		0x10398
#define XCVR_DIAG_HSCLK_DIV		0x1039c
#define TX_PSC_A0			0x10400
#define TX_PSC_A1			0x10404
#define TX_PSC_A2			0x10408
#define TX_PSC_A3			0x1040c
#define RX_PSC_A0			0x20000
#define RX_PSC_A1			0x20004
#define RX_PSC_A2			0x20008
#define RX_PSC_A3			0x2000c
#define PHY_PLL_CFG			0x30038

struct cdns_torrent_phy {
	void __iomem *base;	/* DPTX registers base */
	void __iomem *sd_base; /* SD0801 registers base */
	u32 num_lanes; /* Number of lanes to use */
	u32 max_bit_rate; /* Maximum link bit rate to use (in Mbps) */
	struct device *dev;
};

static int cdns_torrent_dp_init(struct phy *phy);
static void cdns_torrent_dp_run(struct cdns_torrent_phy *cdns_phy);
static
void cdns_torrent_dp_wait_pma_cmn_ready(struct cdns_torrent_phy *cdns_phy);
static void cdns_torrent_dp_pma_cfg(struct cdns_torrent_phy *cdns_phy);
static
void cdns_torrent_dp_pma_cmn_cfg_25mhz(struct cdns_torrent_phy *cdns_phy);
static void cdns_torrent_dp_pma_lane_cfg(struct cdns_torrent_phy *cdns_phy,
					 unsigned int lane);
static
void cdns_torrent_dp_pma_cmn_vco_cfg_25mhz(struct cdns_torrent_phy *cdns_phy);
static void cdns_torrent_dp_pma_cmn_rate(struct cdns_torrent_phy *cdns_phy);
static void cdns_dp_phy_write_field(struct cdns_torrent_phy *cdns_phy,
				    unsigned int offset,
				    unsigned char start_bit,
				    unsigned char num_bits,
				    unsigned int val);

static const struct phy_ops cdns_torrent_phy_ops = {
	.init		= cdns_torrent_dp_init,
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

static int cdns_torrent_dp_init(struct phy *phy)
{
	unsigned char lane_bits;

	struct cdns_torrent_phy *cdns_phy = phy_get_drvdata(phy);

	cdns_torrent_dp_write(cdns_phy, PHY_AUX_CTRL, 0x0003); /* enable AUX */

	/* PHY PMA registers configuration function */
	cdns_torrent_dp_pma_cfg(cdns_phy);

	/*
	 * Set lines power state to A0
	 * Set lines pll clk enable to 0
	 */

	cdns_dp_phy_write_field(cdns_phy, PHY_PMA_XCVR_POWER_STATE_REQ,
				PHY_POWER_STATE_LN_0, 6, 0x0000);

	if (cdns_phy->num_lanes >= 2) {
		cdns_dp_phy_write_field(cdns_phy,
					PHY_PMA_XCVR_POWER_STATE_REQ,
					PHY_POWER_STATE_LN_1, 6, 0x0000);

		if (cdns_phy->num_lanes == 4) {
			cdns_dp_phy_write_field(cdns_phy,
						PHY_PMA_XCVR_POWER_STATE_REQ,
						PHY_POWER_STATE_LN_2, 6, 0);
			cdns_dp_phy_write_field(cdns_phy,
						PHY_PMA_XCVR_POWER_STATE_REQ,
						PHY_POWER_STATE_LN_3, 6, 0);
		}
	}

	cdns_dp_phy_write_field(cdns_phy, PHY_PMA_XCVR_PLLCLK_EN,
				0, 1, 0x0000);

	if (cdns_phy->num_lanes >= 2) {
		cdns_dp_phy_write_field(cdns_phy, PHY_PMA_XCVR_PLLCLK_EN,
					1, 1, 0x0000);
		if (cdns_phy->num_lanes == 4) {
			cdns_dp_phy_write_field(cdns_phy,
						PHY_PMA_XCVR_PLLCLK_EN,
						2, 1, 0x0000);
			cdns_dp_phy_write_field(cdns_phy,
						PHY_PMA_XCVR_PLLCLK_EN,
						3, 1, 0x0000);
		}
	}

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
	cdns_torrent_dp_pma_cmn_vco_cfg_25mhz(cdns_phy);
	cdns_torrent_dp_pma_cmn_rate(cdns_phy);

	/* take out of reset */
	cdns_dp_phy_write_field(cdns_phy, PHY_RESET, 8, 1, 1);
	cdns_torrent_dp_wait_pma_cmn_ready(cdns_phy);
	cdns_torrent_dp_run(cdns_phy);

	return 0;
}

static
void cdns_torrent_dp_wait_pma_cmn_ready(struct cdns_torrent_phy *cdns_phy)
{
	unsigned int reg;
	int ret;

	ret = cdns_torrent_dp_read_poll_timeout(cdns_phy, PHY_PMA_CMN_READY,
						reg, reg & 1, 0, 500);
	if (ret == -ETIMEDOUT)
		dev_err(cdns_phy->dev,
			"timeout waiting for PMA common ready\n");
}

static void cdns_torrent_dp_pma_cfg(struct cdns_torrent_phy *cdns_phy)
{
	unsigned int i;

	/* PMA common configuration */
	cdns_torrent_dp_pma_cmn_cfg_25mhz(cdns_phy);

	/* PMA lane configuration to deal with multi-link operation */
	for (i = 0; i < cdns_phy->num_lanes; i++)
		cdns_torrent_dp_pma_lane_cfg(cdns_phy, i);
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
	cdns_torrent_phy_write(cdns_phy, CMN_PDIAG_PLL0_CP_PADJ_M0, 0x0409);
	cdns_torrent_phy_write(cdns_phy, CMN_PDIAG_PLL0_CP_IADJ_M0, 0x1001);
	cdns_torrent_phy_write(cdns_phy, CMN_PDIAG_PLL0_FILT_PADJ_M0, 0x0F08);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL0_DSM_DIAG_M0, 0x0004);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL0_VCOCAL_INIT_TMR, 0x00FA);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL0_VCOCAL_ITER_TMR, 0x0004);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL1_VCOCAL_INIT_TMR, 0x00FA);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL1_VCOCAL_ITER_TMR, 0x0004);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL0_VCOCAL_REFTIM_START, 0x0318);
}

static
void cdns_torrent_dp_pma_cmn_vco_cfg_25mhz(struct cdns_torrent_phy *cdns_phy)
{
	/* Assumes 25 MHz refclock */
	switch (cdns_phy->max_bit_rate) {
	/* Setting VCO for 10.8GHz */
	case 2700:
	case 5400:
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_INTDIV_M0, 0x01B0);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_FRACDIVL_M0, 0x0000);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_HIGH_THR_M0, 0x0120);
		break;
	/* Setting VCO for 9.72GHz */
	case 2430:
	case 3240:
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_INTDIV_M0, 0x0184);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_FRACDIVL_M0, 0xCCCD);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_HIGH_THR_M0, 0x0104);
		break;
	/* Setting VCO for 8.64GHz */
	case 2160:
	case 4320:
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_INTDIV_M0, 0x0159);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_FRACDIVL_M0, 0x999A);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_HIGH_THR_M0, 0x00E7);
		break;
	/* Setting VCO for 8.1GHz */
	case 8100:
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_INTDIV_M0, 0x0144);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_FRACDIVL_M0, 0x0000);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(cdns_phy, CMN_PLL0_HIGH_THR_M0, 0x00D8);
		break;
	}

	cdns_torrent_phy_write(cdns_phy, CMN_PDIAG_PLL0_CTRL_M0, 0x0002);
	cdns_torrent_phy_write(cdns_phy, CMN_PLL0_VCOCAL_PLLCNT_START, 0x0318);
}

static void cdns_torrent_dp_pma_cmn_rate(struct cdns_torrent_phy *cdns_phy)
{
	unsigned int clk_sel_val = 0;
	unsigned int hsclk_div_val = 0;
	unsigned int i;

	/* 16'h0000 for single DP link configuration */
	cdns_torrent_phy_write(cdns_phy, PHY_PLL_CFG, 0x0000);

	switch (cdns_phy->max_bit_rate) {
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

	/* PMA lane configuration to deal with multi-link operation */
	for (i = 0; i < cdns_phy->num_lanes; i++)
		cdns_torrent_phy_write(cdns_phy,
				       (XCVR_DIAG_HSCLK_DIV | (i << 11)),
				       hsclk_div_val);
}

static void cdns_torrent_dp_pma_lane_cfg(struct cdns_torrent_phy *cdns_phy,
					 unsigned int lane)
{
	unsigned int lane_bits = (lane & LANE_MASK) << 11;

	/* Writing Tx/Rx Power State Controllers registers */
	cdns_torrent_phy_write(cdns_phy, (TX_PSC_A0 | lane_bits), 0x00FB);
	cdns_torrent_phy_write(cdns_phy, (TX_PSC_A2 | lane_bits), 0x04AA);
	cdns_torrent_phy_write(cdns_phy, (TX_PSC_A3 | lane_bits), 0x04AA);
	cdns_torrent_phy_write(cdns_phy, (RX_PSC_A0 | lane_bits), 0x0000);
	cdns_torrent_phy_write(cdns_phy, (RX_PSC_A2 | lane_bits), 0x0000);
	cdns_torrent_phy_write(cdns_phy, (RX_PSC_A3 | lane_bits), 0x0000);

	cdns_torrent_phy_write(cdns_phy,
			       (XCVR_DIAG_PLLDRC_CTRL | lane_bits), 0x0001);
	cdns_torrent_phy_write(cdns_phy,
			       (XCVR_DIAG_HSCLK_SEL | lane_bits), 0x0000);
}

static void cdns_torrent_dp_run(struct cdns_torrent_phy *cdns_phy)
{
	unsigned int read_val;
	u32 write_val1 = 0;
	u32 write_val2 = 0;
	u32 mask = 0;
	int ret;

	/*
	 * waiting for ACK of pma_xcvr_pllclk_en_ln_*, only for the
	 * master lane
	 */
	ret = cdns_torrent_dp_read_poll_timeout(cdns_phy,
						PHY_PMA_XCVR_PLLCLK_EN_ACK,
						read_val, read_val & 1, 0,
						POLL_TIMEOUT_US);
	if (ret == -ETIMEDOUT)
		dev_err(cdns_phy->dev,
			"timeout waiting for link PLL clock enable ack\n");

	ndelay(100);

	switch (cdns_phy->num_lanes) {
	case 1:	/* lane 0 */
		write_val1 = 0x00000004;
		write_val2 = 0x00000001;
		mask = 0x0000003f;
		break;
	case 2: /* lane 0-1 */
		write_val1 = 0x00000404;
		write_val2 = 0x00000101;
		mask = 0x00003f3f;
		break;
	case 4: /* lane 0-3 */
		write_val1 = 0x04040404;
		write_val2 = 0x01010101;
		mask = 0x3f3f3f3f;
		break;
	}

	cdns_torrent_dp_write(cdns_phy,
			      PHY_PMA_XCVR_POWER_STATE_REQ, write_val1);

	ret = cdns_torrent_dp_read_poll_timeout(cdns_phy,
						PHY_PMA_XCVR_POWER_STATE_ACK,
						read_val,
						(read_val & mask) == write_val1,
						0, POLL_TIMEOUT_US);

	if (ret == -ETIMEDOUT)
		dev_err(cdns_phy->dev,
			"timeout waiting for link power state ack\n");

	cdns_torrent_dp_write(cdns_phy, PHY_PMA_XCVR_POWER_STATE_REQ, 0);
	ndelay(100);

	cdns_torrent_dp_write(cdns_phy,
			      PHY_PMA_XCVR_POWER_STATE_REQ, write_val2);

	ret = cdns_torrent_dp_read_poll_timeout(cdns_phy,
						PHY_PMA_XCVR_POWER_STATE_ACK,
						read_val,
						(read_val & mask) == write_val2,
						0, POLL_TIMEOUT_US);
	if (ret == -ETIMEDOUT)
		dev_err(cdns_phy->dev,
			"timeout waiting for link power state ack\n");

	cdns_torrent_dp_write(cdns_phy, PHY_PMA_XCVR_POWER_STATE_REQ, 0);
	ndelay(100);
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
