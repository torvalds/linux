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
#include <linux/regmap.h>

#define REF_CLK_19_2MHz		19200000
#define REF_CLK_25MHz		25000000

#define DEFAULT_NUM_LANES	4
#define MAX_NUM_LANES		4
#define DEFAULT_MAX_BIT_RATE	8100 /* in Mbps */

#define POLL_TIMEOUT_US		5000

#define TORRENT_COMMON_CDB_OFFSET	0x0

#define TORRENT_TX_LANE_CDB_OFFSET(ln, block_offset, reg_offset)	\
				((0x4000 << (block_offset)) +		\
				(((ln) << 9) << (reg_offset)))

#define TORRENT_RX_LANE_CDB_OFFSET(ln, block_offset, reg_offset)	\
				((0x8000 << (block_offset)) +		\
				(((ln) << 9) << (reg_offset)))

#define TORRENT_PHY_PCS_COMMON_OFFSET(block_offset)	\
				(0xC000 << (block_offset))

#define TORRENT_PHY_PMA_COMMON_OFFSET(block_offset)	\
				(0xE000 << (block_offset))

#define TORRENT_DPTX_PHY_OFFSET		0x0

/*
 * register offsets from DPTX PHY register block base (i.e MHDP
 * register base + 0x30a00)
 */
#define PHY_AUX_CTRL			0x04
#define PHY_RESET			0x20
#define PMA_TX_ELEC_IDLE_MASK		0xF0U
#define PMA_TX_ELEC_IDLE_SHIFT		4
#define PHY_L00_RESET_N_MASK		0x01U
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

/*
 * register offsets from SD0801 PHY register block base (i.e MHDP
 * register base + 0x500000)
 */
#define CMN_SSM_BANDGAP_TMR		0x0021U
#define CMN_SSM_BIAS_TMR		0x0022U
#define CMN_PLLSM0_PLLPRE_TMR		0x002AU
#define CMN_PLLSM0_PLLLOCK_TMR		0x002CU
#define CMN_PLLSM1_PLLPRE_TMR		0x0032U
#define CMN_PLLSM1_PLLLOCK_TMR		0x0034U
#define CMN_BGCAL_INIT_TMR		0x0064U
#define CMN_BGCAL_ITER_TMR		0x0065U
#define CMN_IBCAL_INIT_TMR		0x0074U
#define CMN_PLL0_VCOCAL_TCTRL		0x0082U
#define CMN_PLL0_VCOCAL_INIT_TMR	0x0084U
#define CMN_PLL0_VCOCAL_ITER_TMR	0x0085U
#define CMN_PLL0_VCOCAL_REFTIM_START	0x0086U
#define CMN_PLL0_VCOCAL_PLLCNT_START	0x0088U
#define CMN_PLL0_INTDIV_M0		0x0090U
#define CMN_PLL0_FRACDIVL_M0		0x0091U
#define CMN_PLL0_FRACDIVH_M0		0x0092U
#define CMN_PLL0_HIGH_THR_M0		0x0093U
#define CMN_PLL0_DSM_DIAG_M0		0x0094U
#define CMN_PLL0_SS_CTRL1_M0		0x0098U
#define CMN_PLL0_SS_CTRL2_M0            0x0099U
#define CMN_PLL0_SS_CTRL3_M0            0x009AU
#define CMN_PLL0_SS_CTRL4_M0            0x009BU
#define CMN_PLL0_LOCK_REFCNT_START      0x009CU
#define CMN_PLL0_LOCK_PLLCNT_START	0x009EU
#define CMN_PLL0_LOCK_PLLCNT_THR        0x009FU
#define CMN_PLL1_VCOCAL_TCTRL		0x00C2U
#define CMN_PLL1_VCOCAL_INIT_TMR	0x00C4U
#define CMN_PLL1_VCOCAL_ITER_TMR	0x00C5U
#define CMN_PLL1_VCOCAL_REFTIM_START	0x00C6U
#define CMN_PLL1_VCOCAL_PLLCNT_START	0x00C8U
#define CMN_PLL1_INTDIV_M0		0x00D0U
#define CMN_PLL1_FRACDIVL_M0		0x00D1U
#define CMN_PLL1_FRACDIVH_M0		0x00D2U
#define CMN_PLL1_HIGH_THR_M0		0x00D3U
#define CMN_PLL1_DSM_DIAG_M0		0x00D4U
#define CMN_PLL1_SS_CTRL1_M0		0x00D8U
#define CMN_PLL1_SS_CTRL2_M0            0x00D9U
#define CMN_PLL1_SS_CTRL3_M0            0x00DAU
#define CMN_PLL1_SS_CTRL4_M0            0x00DBU
#define CMN_PLL1_LOCK_REFCNT_START      0x00DCU
#define CMN_PLL1_LOCK_PLLCNT_START	0x00DEU
#define CMN_PLL1_LOCK_PLLCNT_THR        0x00DFU
#define CMN_TXPUCAL_INIT_TMR		0x0104U
#define CMN_TXPUCAL_ITER_TMR		0x0105U
#define CMN_TXPDCAL_INIT_TMR		0x010CU
#define CMN_TXPDCAL_ITER_TMR		0x010DU
#define CMN_RXCAL_INIT_TMR		0x0114U
#define CMN_RXCAL_ITER_TMR		0x0115U
#define CMN_SD_CAL_INIT_TMR		0x0124U
#define CMN_SD_CAL_ITER_TMR		0x0125U
#define CMN_SD_CAL_REFTIM_START		0x0126U
#define CMN_SD_CAL_PLLCNT_START		0x0128U
#define CMN_PDIAG_PLL0_CTRL_M0		0x01A0U
#define CMN_PDIAG_PLL0_CLK_SEL_M0	0x01A1U
#define CMN_PDIAG_PLL0_CP_PADJ_M0	0x01A4U
#define CMN_PDIAG_PLL0_CP_IADJ_M0	0x01A5U
#define CMN_PDIAG_PLL0_FILT_PADJ_M0	0x01A6U
#define CMN_PDIAG_PLL0_CP_PADJ_M1	0x01B4U
#define CMN_PDIAG_PLL0_CP_IADJ_M1	0x01B5U
#define CMN_PDIAG_PLL1_CTRL_M0		0x01C0U
#define CMN_PDIAG_PLL1_CLK_SEL_M0	0x01C1U
#define CMN_PDIAG_PLL1_CP_PADJ_M0	0x01C4U
#define CMN_PDIAG_PLL1_CP_IADJ_M0	0x01C5U
#define CMN_PDIAG_PLL1_FILT_PADJ_M0	0x01C6U

/* PMA TX Lane registers */
#define TX_TXCC_CTRL			0x0040U
#define TX_TXCC_CPOST_MULT_00		0x004CU
#define TX_TXCC_MGNFS_MULT_000		0x0050U
#define DRV_DIAG_TX_DRV			0x00C6U
#define XCVR_DIAG_PLLDRC_CTRL		0x00E5U
#define XCVR_DIAG_HSCLK_SEL		0x00E6U
#define XCVR_DIAG_HSCLK_DIV		0x00E7U
#define XCVR_DIAG_BIDI_CTRL		0x00EAU
#define TX_PSC_A0			0x0100U
#define TX_PSC_A2			0x0102U
#define TX_PSC_A3			0x0103U
#define TX_RCVDET_ST_TMR		0x0123U
#define TX_DIAG_ACYA			0x01E7U
#define TX_DIAG_ACYA_HBDC_MASK		0x0001U

/* PMA RX Lane registers */
#define RX_PSC_A0			0x0000U
#define RX_PSC_A2			0x0002U
#define RX_PSC_A3			0x0003U
#define RX_PSC_CAL			0x0006U
#define RX_REE_GCSM1_CTRL		0x0108U
#define RX_REE_GCSM2_CTRL		0x0110U
#define RX_REE_PERGCSM_CTRL		0x0118U

/* PHY PCS common registers */
#define PHY_PLL_CFG			0x000EU

/* PHY PMA common registers */
#define PHY_PMA_CMN_CTRL2		0x0001U
#define PHY_PMA_PLL_RAW_CTRL		0x0003U

static const struct reg_field phy_pll_cfg =
				REG_FIELD(PHY_PLL_CFG, 0, 1);

static const struct reg_field phy_pma_cmn_ctrl_2 =
				REG_FIELD(PHY_PMA_CMN_CTRL2, 0, 7);

static const struct reg_field phy_pma_pll_raw_ctrl =
				REG_FIELD(PHY_PMA_PLL_RAW_CTRL, 0, 1);

static const struct reg_field phy_reset_ctrl =
				REG_FIELD(PHY_RESET, 8, 8);

static const struct of_device_id cdns_torrent_phy_of_match[];

struct cdns_torrent_phy {
	void __iomem *base;	/* DPTX registers base */
	void __iomem *sd_base; /* SD0801 registers base */
	u32 num_lanes; /* Number of lanes to use */
	u32 max_bit_rate; /* Maximum link bit rate to use (in Mbps) */
	struct device *dev;
	struct clk *clk;
	unsigned long ref_clk_rate;
	struct regmap *regmap;
	struct regmap *regmap_common_cdb;
	struct regmap *regmap_phy_pcs_common_cdb;
	struct regmap *regmap_phy_pma_common_cdb;
	struct regmap *regmap_tx_lane_cdb[MAX_NUM_LANES];
	struct regmap *regmap_rx_lane_cdb[MAX_NUM_LANES];
	struct regmap *regmap_dptx_phy_reg;
	struct regmap_field *phy_pll_cfg;
	struct regmap_field *phy_pma_cmn_ctrl_2;
	struct regmap_field *phy_pma_pll_raw_ctrl;
	struct regmap_field *phy_reset_ctrl;
};

enum phy_powerstate {
	POWERSTATE_A0 = 0,
	/* Powerstate A1 is unused */
	POWERSTATE_A2 = 2,
	POWERSTATE_A3 = 3,
};

static int cdns_torrent_dp_init(struct phy *phy);
static int cdns_torrent_dp_exit(struct phy *phy);
static int cdns_torrent_dp_run(struct cdns_torrent_phy *cdns_phy,
			       u32 num_lanes);
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
static int cdns_torrent_dp_configure(struct phy *phy,
				     union phy_configure_opts *opts);
static int cdns_torrent_dp_set_power_state(struct cdns_torrent_phy *cdns_phy,
					   u32 num_lanes,
					   enum phy_powerstate powerstate);

static const struct phy_ops cdns_torrent_phy_ops = {
	.init		= cdns_torrent_dp_init,
	.exit		= cdns_torrent_dp_exit,
	.configure	= cdns_torrent_dp_configure,
	.owner		= THIS_MODULE,
};

struct cdns_torrent_data {
		u8 block_offset_shift;
		u8 reg_offset_shift;
};

struct cdns_regmap_cdb_context {
	struct device *dev;
	void __iomem *base;
	u8 reg_offset_shift;
};

static int cdns_regmap_write(void *context, unsigned int reg, unsigned int val)
{
	struct cdns_regmap_cdb_context *ctx = context;
	u32 offset = reg << ctx->reg_offset_shift;

	writew(val, ctx->base + offset);

	return 0;
}

static int cdns_regmap_read(void *context, unsigned int reg, unsigned int *val)
{
	struct cdns_regmap_cdb_context *ctx = context;
	u32 offset = reg << ctx->reg_offset_shift;

	*val = readw(ctx->base + offset);
	return 0;
}

static int cdns_regmap_dptx_write(void *context, unsigned int reg,
				  unsigned int val)
{
	struct cdns_regmap_cdb_context *ctx = context;
	u32 offset = reg;

	writel(val, ctx->base + offset);

	return 0;
}

static int cdns_regmap_dptx_read(void *context, unsigned int reg,
				 unsigned int *val)
{
	struct cdns_regmap_cdb_context *ctx = context;
	u32 offset = reg;

	*val = readl(ctx->base + offset);
	return 0;
}

#define TORRENT_TX_LANE_CDB_REGMAP_CONF(n) \
{ \
	.name = "torrent_tx_lane" n "_cdb", \
	.reg_stride = 1, \
	.fast_io = true, \
	.reg_write = cdns_regmap_write, \
	.reg_read = cdns_regmap_read, \
}

#define TORRENT_RX_LANE_CDB_REGMAP_CONF(n) \
{ \
	.name = "torrent_rx_lane" n "_cdb", \
	.reg_stride = 1, \
	.fast_io = true, \
	.reg_write = cdns_regmap_write, \
	.reg_read = cdns_regmap_read, \
}

static struct regmap_config cdns_torrent_tx_lane_cdb_config[] = {
	TORRENT_TX_LANE_CDB_REGMAP_CONF("0"),
	TORRENT_TX_LANE_CDB_REGMAP_CONF("1"),
	TORRENT_TX_LANE_CDB_REGMAP_CONF("2"),
	TORRENT_TX_LANE_CDB_REGMAP_CONF("3"),
};

static struct regmap_config cdns_torrent_rx_lane_cdb_config[] = {
	TORRENT_RX_LANE_CDB_REGMAP_CONF("0"),
	TORRENT_RX_LANE_CDB_REGMAP_CONF("1"),
	TORRENT_RX_LANE_CDB_REGMAP_CONF("2"),
	TORRENT_RX_LANE_CDB_REGMAP_CONF("3"),
};

static struct regmap_config cdns_torrent_common_cdb_config = {
	.name = "torrent_common_cdb",
	.reg_stride = 1,
	.fast_io = true,
	.reg_write = cdns_regmap_write,
	.reg_read = cdns_regmap_read,
};

static struct regmap_config cdns_torrent_phy_pcs_cmn_cdb_config = {
	.name = "torrent_phy_pcs_cmn_cdb",
	.reg_stride = 1,
	.fast_io = true,
	.reg_write = cdns_regmap_write,
	.reg_read = cdns_regmap_read,
};

static struct regmap_config cdns_torrent_phy_pma_cmn_cdb_config = {
	.name = "torrent_phy_pma_cmn_cdb",
	.reg_stride = 1,
	.fast_io = true,
	.reg_write = cdns_regmap_write,
	.reg_read = cdns_regmap_read,
};

static struct regmap_config cdns_torrent_dptx_phy_config = {
	.name = "torrent_dptx_phy",
	.reg_stride = 1,
	.fast_io = true,
	.reg_write = cdns_regmap_dptx_write,
	.reg_read = cdns_regmap_dptx_read,
};

/* PHY mmr access functions */

static void cdns_torrent_phy_write(struct regmap *regmap, u32 offset, u32 val)
{
	regmap_write(regmap, offset, val);
}

static u32 cdns_torrent_phy_read(struct regmap *regmap, u32 offset)
{
	unsigned int val;

	regmap_read(regmap, offset, &val);
	return val;
}

/* DPTX mmr access functions */

static void cdns_torrent_dp_write(struct regmap *regmap, u32 offset, u32 val)
{
	regmap_write(regmap, offset, val);
}

static u32 cdns_torrent_dp_read(struct regmap *regmap, u32 offset)
{
	u32 val;

	regmap_read(regmap, offset, &val);
	return val;
}

/*
 * Structure used to store values of PHY registers for voltage-related
 * coefficients, for particular voltage swing and pre-emphasis level. Values
 * are shared across all physical lanes.
 */
struct coefficients {
	/* Value of DRV_DIAG_TX_DRV register to use */
	u16 diag_tx_drv;
	/* Value of TX_TXCC_MGNFS_MULT_000 register to use */
	u16 mgnfs_mult;
	/* Value of TX_TXCC_CPOST_MULT_00 register to use */
	u16 cpost_mult;
};

/*
 * Array consists of values of voltage-related registers for sd0801 PHY. A value
 * of 0xFFFF is a placeholder for invalid combination, and will never be used.
 */
static const struct coefficients vltg_coeff[4][4] = {
	/* voltage swing 0, pre-emphasis 0->3 */
	{	{.diag_tx_drv = 0x0003, .mgnfs_mult = 0x002A,
		 .cpost_mult = 0x0000},
		{.diag_tx_drv = 0x0003, .mgnfs_mult = 0x001F,
		 .cpost_mult = 0x0014},
		{.diag_tx_drv = 0x0003, .mgnfs_mult = 0x0012,
		 .cpost_mult = 0x0020},
		{.diag_tx_drv = 0x0003, .mgnfs_mult = 0x0000,
		 .cpost_mult = 0x002A}
	},

	/* voltage swing 1, pre-emphasis 0->3 */
	{	{.diag_tx_drv = 0x0003, .mgnfs_mult = 0x001F,
		 .cpost_mult = 0x0000},
		{.diag_tx_drv = 0x0003, .mgnfs_mult = 0x0013,
		 .cpost_mult = 0x0012},
		{.diag_tx_drv = 0x0003, .mgnfs_mult = 0x0000,
		 .cpost_mult = 0x001F},
		{.diag_tx_drv = 0xFFFF, .mgnfs_mult = 0xFFFF,
		 .cpost_mult = 0xFFFF}
	},

	/* voltage swing 2, pre-emphasis 0->3 */
	{	{.diag_tx_drv = 0x0003, .mgnfs_mult = 0x0013,
		 .cpost_mult = 0x0000},
		{.diag_tx_drv = 0x0003, .mgnfs_mult = 0x0000,
		 .cpost_mult = 0x0013},
		{.diag_tx_drv = 0xFFFF, .mgnfs_mult = 0xFFFF,
		 .cpost_mult = 0xFFFF},
		{.diag_tx_drv = 0xFFFF, .mgnfs_mult = 0xFFFF,
		 .cpost_mult = 0xFFFF}
	},

	/* voltage swing 3, pre-emphasis 0->3 */
	{	{.diag_tx_drv = 0x0003, .mgnfs_mult = 0x0000,
		 .cpost_mult = 0x0000},
		{.diag_tx_drv = 0xFFFF, .mgnfs_mult = 0xFFFF,
		 .cpost_mult = 0xFFFF},
		{.diag_tx_drv = 0xFFFF, .mgnfs_mult = 0xFFFF,
		 .cpost_mult = 0xFFFF},
		{.diag_tx_drv = 0xFFFF, .mgnfs_mult = 0xFFFF,
		 .cpost_mult = 0xFFFF}
	}
};

/*
 * Enable or disable PLL for selected lanes.
 */
static int cdns_torrent_dp_set_pll_en(struct cdns_torrent_phy *cdns_phy,
				      struct phy_configure_opts_dp *dp,
				      bool enable)
{
	u32 rd_val;
	u32 ret;
	struct regmap *regmap = cdns_phy->regmap_dptx_phy_reg;

	/*
	 * Used to determine, which bits to check for or enable in
	 * PHY_PMA_XCVR_PLLCLK_EN register.
	 */
	u32 pll_bits;
	/* Used to enable or disable lanes. */
	u32 pll_val;

	/* Select values of registers and mask, depending on enabled lane
	 * count.
	 */
	switch (dp->lanes) {
	/* lane 0 */
	case (1):
		pll_bits = 0x00000001;
		break;
	/* lanes 0-1 */
	case (2):
		pll_bits = 0x00000003;
		break;
	/* lanes 0-3, all */
	default:
		pll_bits = 0x0000000F;
		break;
	}

	if (enable)
		pll_val = pll_bits;
	else
		pll_val = 0x00000000;

	cdns_torrent_dp_write(regmap, PHY_PMA_XCVR_PLLCLK_EN, pll_val);

	/* Wait for acknowledgment from PHY. */
	ret = regmap_read_poll_timeout(regmap,
				       PHY_PMA_XCVR_PLLCLK_EN_ACK,
				       rd_val,
				       (rd_val & pll_bits) == pll_val,
				       0, POLL_TIMEOUT_US);
	ndelay(100);
	return ret;
}

/*
 * Perform register operations related to setting link rate, once powerstate is
 * set and PLL disable request was processed.
 */
static int cdns_torrent_dp_configure_rate(struct cdns_torrent_phy *cdns_phy,
					  struct phy_configure_opts_dp *dp)
{
	u32 ret;
	u32 read_val;

	/* Disable the cmn_pll0_en before re-programming the new data rate. */
	regmap_field_write(cdns_phy->phy_pma_pll_raw_ctrl, 0x0);

	/*
	 * Wait for PLL ready de-assertion.
	 * For PLL0 - PHY_PMA_CMN_CTRL2[2] == 1
	 */
	ret = regmap_field_read_poll_timeout(cdns_phy->phy_pma_cmn_ctrl_2,
					     read_val,
					     ((read_val >> 2) & 0x01) != 0,
					     0, POLL_TIMEOUT_US);
	if (ret)
		return ret;
	ndelay(200);

	/* DP Rate Change - VCO Output settings. */
	if (cdns_phy->ref_clk_rate == REF_CLK_19_2MHz) {
		/* PMA common configuration 19.2MHz */
		cdns_torrent_dp_pma_cmn_vco_cfg_19_2mhz(cdns_phy, dp->link_rate,
							dp->ssc);
		cdns_torrent_dp_pma_cmn_cfg_19_2mhz(cdns_phy);
	} else if (cdns_phy->ref_clk_rate == REF_CLK_25MHz) {
		/* PMA common configuration 25MHz */
		cdns_torrent_dp_pma_cmn_vco_cfg_25mhz(cdns_phy, dp->link_rate,
						      dp->ssc);
		cdns_torrent_dp_pma_cmn_cfg_25mhz(cdns_phy);
	}
	cdns_torrent_dp_pma_cmn_rate(cdns_phy, dp->link_rate, dp->lanes);

	/* Enable the cmn_pll0_en. */
	regmap_field_write(cdns_phy->phy_pma_pll_raw_ctrl, 0x3);

	/*
	 * Wait for PLL ready assertion.
	 * For PLL0 - PHY_PMA_CMN_CTRL2[0] == 1
	 */
	ret = regmap_field_read_poll_timeout(cdns_phy->phy_pma_cmn_ctrl_2,
					     read_val,
					     (read_val & 0x01) != 0,
					     0, POLL_TIMEOUT_US);
	return ret;
}

/*
 * Verify, that parameters to configure PHY with are correct.
 */
static int cdns_torrent_dp_verify_config(struct cdns_torrent_phy *cdns_phy,
					 struct phy_configure_opts_dp *dp)
{
	u8 i;

	/* If changing link rate was required, verify it's supported. */
	if (dp->set_rate) {
		switch (dp->link_rate) {
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
			return -EINVAL;
		}
	}

	/* Verify lane count. */
	switch (dp->lanes) {
	case 1:
	case 2:
	case 4:
		/* valid lane count. */
		break;
	default:
		return -EINVAL;
	}

	/* Check against actual number of PHY's lanes. */
	if (dp->lanes > cdns_phy->num_lanes)
		return -EINVAL;

	/*
	 * If changing voltages is required, check swing and pre-emphasis
	 * levels, per-lane.
	 */
	if (dp->set_voltages) {
		/* Lane count verified previously. */
		for (i = 0; i < dp->lanes; i++) {
			if (dp->voltage[i] > 3 || dp->pre[i] > 3)
				return -EINVAL;

			/* Sum of voltage swing and pre-emphasis levels cannot
			 * exceed 3.
			 */
			if (dp->voltage[i] + dp->pre[i] > 3)
				return -EINVAL;
		}
	}

	return 0;
}

/* Set power state A0 and PLL clock enable to 0 on enabled lanes. */
static void cdns_torrent_dp_set_a0_pll(struct cdns_torrent_phy *cdns_phy,
				       u32 num_lanes)
{
	struct regmap *regmap = cdns_phy->regmap_dptx_phy_reg;
	u32 pwr_state = cdns_torrent_dp_read(regmap,
					     PHY_PMA_XCVR_POWER_STATE_REQ);
	u32 pll_clk_en = cdns_torrent_dp_read(regmap,
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

	cdns_torrent_dp_write(regmap, PHY_PMA_XCVR_POWER_STATE_REQ, pwr_state);
	cdns_torrent_dp_write(regmap, PHY_PMA_XCVR_PLLCLK_EN, pll_clk_en);
}

/* Configure lane count as required. */
static int cdns_torrent_dp_set_lanes(struct cdns_torrent_phy *cdns_phy,
				     struct phy_configure_opts_dp *dp)
{
	u32 value;
	u32 ret;
	struct regmap *regmap = cdns_phy->regmap_dptx_phy_reg;
	u8 lane_mask = (1 << dp->lanes) - 1;

	value = cdns_torrent_dp_read(regmap, PHY_RESET);
	/* clear pma_tx_elec_idle_ln_* bits. */
	value &= ~PMA_TX_ELEC_IDLE_MASK;
	/* Assert pma_tx_elec_idle_ln_* for disabled lanes. */
	value |= ((~lane_mask) << PMA_TX_ELEC_IDLE_SHIFT) &
		 PMA_TX_ELEC_IDLE_MASK;
	cdns_torrent_dp_write(regmap, PHY_RESET, value);

	/* reset the link by asserting phy_l00_reset_n low */
	cdns_torrent_dp_write(regmap, PHY_RESET,
			      value & (~PHY_L00_RESET_N_MASK));

	/*
	 * Assert lane reset on unused lanes and lane 0 so they remain in reset
	 * and powered down when re-enabling the link
	 */
	value = (value & 0x0000FFF0) | (0x0000000E & lane_mask);
	cdns_torrent_dp_write(regmap, PHY_RESET, value);

	cdns_torrent_dp_set_a0_pll(cdns_phy, dp->lanes);

	/* release phy_l0*_reset_n based on used laneCount */
	value = (value & 0x0000FFF0) | (0x0000000F & lane_mask);
	cdns_torrent_dp_write(regmap, PHY_RESET, value);

	/* Wait, until PHY gets ready after releasing PHY reset signal. */
	ret = cdns_torrent_dp_wait_pma_cmn_ready(cdns_phy);
	if (ret)
		return ret;

	ndelay(100);

	/* release pma_xcvr_pllclk_en_ln_*, only for the master lane */
	cdns_torrent_dp_write(regmap, PHY_PMA_XCVR_PLLCLK_EN, 0x0001);

	ret = cdns_torrent_dp_run(cdns_phy, dp->lanes);

	return ret;
}

/* Configure link rate as required. */
static int cdns_torrent_dp_set_rate(struct cdns_torrent_phy *cdns_phy,
				    struct phy_configure_opts_dp *dp)
{
	u32 ret;

	ret = cdns_torrent_dp_set_power_state(cdns_phy, dp->lanes,
					      POWERSTATE_A3);
	if (ret)
		return ret;
	ret = cdns_torrent_dp_set_pll_en(cdns_phy, dp, false);
	if (ret)
		return ret;
	ndelay(200);

	ret = cdns_torrent_dp_configure_rate(cdns_phy, dp);
	if (ret)
		return ret;
	ndelay(200);

	ret = cdns_torrent_dp_set_pll_en(cdns_phy, dp, true);
	if (ret)
		return ret;
	ret = cdns_torrent_dp_set_power_state(cdns_phy, dp->lanes,
					      POWERSTATE_A2);
	if (ret)
		return ret;
	ret = cdns_torrent_dp_set_power_state(cdns_phy, dp->lanes,
					      POWERSTATE_A0);
	if (ret)
		return ret;
	ndelay(900);

	return ret;
}

/* Configure voltage swing and pre-emphasis for all enabled lanes. */
static void cdns_torrent_dp_set_voltages(struct cdns_torrent_phy *cdns_phy,
					 struct phy_configure_opts_dp *dp)
{
	u8 lane;
	u16 val;

	for (lane = 0; lane < dp->lanes; lane++) {
		val = cdns_torrent_phy_read(cdns_phy->regmap_tx_lane_cdb[lane],
					    TX_DIAG_ACYA);
		/*
		 * Write 1 to register bit TX_DIAG_ACYA[0] to freeze the
		 * current state of the analog TX driver.
		 */
		val |= TX_DIAG_ACYA_HBDC_MASK;
		cdns_torrent_phy_write(cdns_phy->regmap_tx_lane_cdb[lane],
				       TX_DIAG_ACYA, val);

		cdns_torrent_phy_write(cdns_phy->regmap_tx_lane_cdb[lane],
				       TX_TXCC_CTRL, 0x08A4);
		val = vltg_coeff[dp->voltage[lane]][dp->pre[lane]].diag_tx_drv;
		cdns_torrent_phy_write(cdns_phy->regmap_tx_lane_cdb[lane],
				       DRV_DIAG_TX_DRV, val);
		val = vltg_coeff[dp->voltage[lane]][dp->pre[lane]].mgnfs_mult;
		cdns_torrent_phy_write(cdns_phy->regmap_tx_lane_cdb[lane],
				       TX_TXCC_MGNFS_MULT_000,
				       val);
		val = vltg_coeff[dp->voltage[lane]][dp->pre[lane]].cpost_mult;
		cdns_torrent_phy_write(cdns_phy->regmap_tx_lane_cdb[lane],
				       TX_TXCC_CPOST_MULT_00,
				       val);

		val = cdns_torrent_phy_read(cdns_phy->regmap_tx_lane_cdb[lane],
					    TX_DIAG_ACYA);
		/*
		 * Write 0 to register bit TX_DIAG_ACYA[0] to allow the state of
		 * analog TX driver to reflect the new programmed one.
		 */
		val &= ~TX_DIAG_ACYA_HBDC_MASK;
		cdns_torrent_phy_write(cdns_phy->regmap_tx_lane_cdb[lane],
				       TX_DIAG_ACYA, val);
	}
};

static int cdns_torrent_dp_configure(struct phy *phy,
				     union phy_configure_opts *opts)
{
	struct cdns_torrent_phy *cdns_phy = phy_get_drvdata(phy);
	int ret;

	ret = cdns_torrent_dp_verify_config(cdns_phy, &opts->dp);
	if (ret) {
		dev_err(&phy->dev, "invalid params for phy configure\n");
		return ret;
	}

	if (opts->dp.set_lanes) {
		ret = cdns_torrent_dp_set_lanes(cdns_phy, &opts->dp);
		if (ret) {
			dev_err(&phy->dev, "cdns_torrent_dp_set_lanes failed\n");
			return ret;
		}
	}

	if (opts->dp.set_rate) {
		ret = cdns_torrent_dp_set_rate(cdns_phy, &opts->dp);
		if (ret) {
			dev_err(&phy->dev, "cdns_torrent_dp_set_rate failed\n");
			return ret;
		}
	}

	if (opts->dp.set_voltages)
		cdns_torrent_dp_set_voltages(cdns_phy, &opts->dp);

	return ret;
}

static int cdns_torrent_dp_init(struct phy *phy)
{
	unsigned char lane_bits;
	int ret;

	struct cdns_torrent_phy *cdns_phy = phy_get_drvdata(phy);
	struct regmap *regmap = cdns_phy->regmap_dptx_phy_reg;

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

	cdns_torrent_dp_write(regmap, PHY_AUX_CTRL, 0x0003); /* enable AUX */

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
	cdns_torrent_dp_write(regmap, PHY_RESET,
			      ((0xF & ~lane_bits) << 4) | (0xF & lane_bits));

	/* release pma_xcvr_pllclk_en_ln_*, only for the master lane */
	cdns_torrent_dp_write(regmap, PHY_PMA_XCVR_PLLCLK_EN, 0x0001);

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
	regmap_field_write(cdns_phy->phy_reset_ctrl, 0x1);

	ret = cdns_torrent_dp_wait_pma_cmn_ready(cdns_phy);
	if (ret)
		return ret;

	ret = cdns_torrent_dp_run(cdns_phy, cdns_phy->num_lanes);

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
	struct regmap *regmap = cdns_phy->regmap_dptx_phy_reg;

	ret = regmap_read_poll_timeout(regmap, PHY_PMA_CMN_READY, reg,
				       reg & 1, 0, POLL_TIMEOUT_US);
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
	struct regmap *regmap = cdns_phy->regmap_common_cdb;

	/* refclock registers - assumes 19.2 MHz refclock */
	cdns_torrent_phy_write(regmap, CMN_SSM_BIAS_TMR, 0x0014);
	cdns_torrent_phy_write(regmap, CMN_PLLSM0_PLLPRE_TMR, 0x0027);
	cdns_torrent_phy_write(regmap, CMN_PLLSM0_PLLLOCK_TMR, 0x00A1);
	cdns_torrent_phy_write(regmap, CMN_PLLSM1_PLLPRE_TMR, 0x0027);
	cdns_torrent_phy_write(regmap, CMN_PLLSM1_PLLLOCK_TMR, 0x00A1);
	cdns_torrent_phy_write(regmap, CMN_BGCAL_INIT_TMR, 0x0060);
	cdns_torrent_phy_write(regmap, CMN_BGCAL_ITER_TMR, 0x0060);
	cdns_torrent_phy_write(regmap, CMN_IBCAL_INIT_TMR, 0x0014);
	cdns_torrent_phy_write(regmap, CMN_TXPUCAL_INIT_TMR, 0x0018);
	cdns_torrent_phy_write(regmap, CMN_TXPUCAL_ITER_TMR, 0x0005);
	cdns_torrent_phy_write(regmap, CMN_TXPDCAL_INIT_TMR, 0x0018);
	cdns_torrent_phy_write(regmap, CMN_TXPDCAL_ITER_TMR, 0x0005);
	cdns_torrent_phy_write(regmap, CMN_RXCAL_INIT_TMR, 0x0240);
	cdns_torrent_phy_write(regmap, CMN_RXCAL_ITER_TMR, 0x0005);
	cdns_torrent_phy_write(regmap, CMN_SD_CAL_INIT_TMR, 0x0002);
	cdns_torrent_phy_write(regmap, CMN_SD_CAL_ITER_TMR, 0x0002);
	cdns_torrent_phy_write(regmap, CMN_SD_CAL_REFTIM_START, 0x000B);
	cdns_torrent_phy_write(regmap, CMN_SD_CAL_PLLCNT_START, 0x0137);

	/* PLL registers */
	cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL0_CP_PADJ_M0, 0x0509);
	cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL0_CP_IADJ_M0, 0x0F00);
	cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL0_FILT_PADJ_M0, 0x0F08);
	cdns_torrent_phy_write(regmap, CMN_PLL0_DSM_DIAG_M0, 0x0004);
	cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL1_CP_PADJ_M0, 0x0509);
	cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL1_CP_IADJ_M0, 0x0F00);
	cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL1_FILT_PADJ_M0, 0x0F08);
	cdns_torrent_phy_write(regmap, CMN_PLL1_DSM_DIAG_M0, 0x0004);
	cdns_torrent_phy_write(regmap, CMN_PLL0_VCOCAL_INIT_TMR, 0x00C0);
	cdns_torrent_phy_write(regmap, CMN_PLL0_VCOCAL_ITER_TMR, 0x0004);
	cdns_torrent_phy_write(regmap, CMN_PLL1_VCOCAL_INIT_TMR, 0x00C0);
	cdns_torrent_phy_write(regmap, CMN_PLL1_VCOCAL_ITER_TMR, 0x0004);
	cdns_torrent_phy_write(regmap, CMN_PLL0_VCOCAL_REFTIM_START, 0x0260);
	cdns_torrent_phy_write(regmap, CMN_PLL0_VCOCAL_TCTRL, 0x0003);
	cdns_torrent_phy_write(regmap, CMN_PLL1_VCOCAL_REFTIM_START, 0x0260);
	cdns_torrent_phy_write(regmap, CMN_PLL1_VCOCAL_TCTRL, 0x0003);
}

/*
 * Set registers responsible for enabling and configuring SSC, with second and
 * third register values provided by parameters.
 */
static
void cdns_torrent_dp_enable_ssc_19_2mhz(struct cdns_torrent_phy *cdns_phy,
					u32 ctrl2_val, u32 ctrl3_val)
{
	struct regmap *regmap = cdns_phy->regmap_common_cdb;

	cdns_torrent_phy_write(regmap, CMN_PLL0_SS_CTRL1_M0, 0x0001);
	cdns_torrent_phy_write(regmap, CMN_PLL0_SS_CTRL1_M0, ctrl2_val);
	cdns_torrent_phy_write(regmap, CMN_PLL0_SS_CTRL1_M0, ctrl3_val);
	cdns_torrent_phy_write(regmap, CMN_PLL0_SS_CTRL4_M0, 0x0003);
	cdns_torrent_phy_write(regmap, CMN_PLL1_SS_CTRL1_M0, 0x0001);
	cdns_torrent_phy_write(regmap, CMN_PLL1_SS_CTRL1_M0, ctrl2_val);
	cdns_torrent_phy_write(regmap, CMN_PLL1_SS_CTRL1_M0, ctrl3_val);
	cdns_torrent_phy_write(regmap, CMN_PLL1_SS_CTRL4_M0, 0x0003);
}

static
void cdns_torrent_dp_pma_cmn_vco_cfg_19_2mhz(struct cdns_torrent_phy *cdns_phy,
					     u32 rate, bool ssc)
{
	struct regmap *regmap = cdns_phy->regmap_common_cdb;

	/* Assumes 19.2 MHz refclock */
	switch (rate) {
	/* Setting VCO for 10.8GHz */
	case 2700:
	case 5400:
		cdns_torrent_phy_write(regmap,
				       CMN_PLL0_INTDIV_M0, 0x0119);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL0_FRACDIVL_M0, 0x4000);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL0_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL0_HIGH_THR_M0, 0x00BC);
		cdns_torrent_phy_write(regmap,
				       CMN_PDIAG_PLL0_CTRL_M0, 0x0012);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL1_INTDIV_M0, 0x0119);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL1_FRACDIVL_M0, 0x4000);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL1_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL1_HIGH_THR_M0, 0x00BC);
		cdns_torrent_phy_write(regmap,
				       CMN_PDIAG_PLL1_CTRL_M0, 0x0012);
		if (ssc)
			cdns_torrent_dp_enable_ssc_19_2mhz(cdns_phy, 0x033A,
							   0x006A);
		break;
	/* Setting VCO for 9.72GHz */
	case 1620:
	case 2430:
	case 3240:
		cdns_torrent_phy_write(regmap,
				       CMN_PLL0_INTDIV_M0, 0x01FA);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL0_FRACDIVL_M0, 0x4000);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL0_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL0_HIGH_THR_M0, 0x0152);
		cdns_torrent_phy_write(regmap,
				       CMN_PDIAG_PLL0_CTRL_M0, 0x0002);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL1_INTDIV_M0, 0x01FA);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL1_FRACDIVL_M0, 0x4000);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL1_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL1_HIGH_THR_M0, 0x0152);
		cdns_torrent_phy_write(regmap,
				       CMN_PDIAG_PLL1_CTRL_M0, 0x0002);
		if (ssc)
			cdns_torrent_dp_enable_ssc_19_2mhz(cdns_phy, 0x05DD,
							   0x0069);
		break;
	/* Setting VCO for 8.64GHz */
	case 2160:
	case 4320:
		cdns_torrent_phy_write(regmap,
				       CMN_PLL0_INTDIV_M0, 0x01C2);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL0_FRACDIVL_M0, 0x0000);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL0_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL0_HIGH_THR_M0, 0x012C);
		cdns_torrent_phy_write(regmap,
				       CMN_PDIAG_PLL0_CTRL_M0, 0x0002);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL1_INTDIV_M0, 0x01C2);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL1_FRACDIVL_M0, 0x0000);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL1_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL1_HIGH_THR_M0, 0x012C);
		cdns_torrent_phy_write(regmap,
				       CMN_PDIAG_PLL1_CTRL_M0, 0x0002);
		if (ssc)
			cdns_torrent_dp_enable_ssc_19_2mhz(cdns_phy, 0x0536,
							   0x0069);
		break;
	/* Setting VCO for 8.1GHz */
	case 8100:
		cdns_torrent_phy_write(regmap,
				       CMN_PLL0_INTDIV_M0, 0x01A5);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL0_FRACDIVL_M0, 0xE000);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL0_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL0_HIGH_THR_M0, 0x011A);
		cdns_torrent_phy_write(regmap,
				       CMN_PDIAG_PLL0_CTRL_M0, 0x0002);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL1_INTDIV_M0, 0x01A5);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL1_FRACDIVL_M0, 0xE000);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL1_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL1_HIGH_THR_M0, 0x011A);
		cdns_torrent_phy_write(regmap,
				       CMN_PDIAG_PLL1_CTRL_M0, 0x0002);
		if (ssc)
			cdns_torrent_dp_enable_ssc_19_2mhz(cdns_phy, 0x04D7,
							   0x006A);
		break;
	}

	if (ssc) {
		cdns_torrent_phy_write(regmap,
				       CMN_PLL0_VCOCAL_PLLCNT_START, 0x025E);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL0_LOCK_PLLCNT_THR, 0x0005);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL1_VCOCAL_PLLCNT_START, 0x025E);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL1_LOCK_PLLCNT_THR, 0x0005);
	} else {
		cdns_torrent_phy_write(regmap,
				       CMN_PLL0_VCOCAL_PLLCNT_START, 0x0260);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL1_VCOCAL_PLLCNT_START, 0x0260);
		/* Set reset register values to disable SSC */
		cdns_torrent_phy_write(regmap,
				       CMN_PLL0_SS_CTRL1_M0, 0x0002);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL0_SS_CTRL2_M0, 0x0000);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL0_SS_CTRL3_M0, 0x0000);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL0_SS_CTRL4_M0, 0x0000);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL0_LOCK_PLLCNT_THR, 0x0003);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL1_SS_CTRL1_M0, 0x0002);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL1_SS_CTRL2_M0, 0x0000);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL1_SS_CTRL3_M0, 0x0000);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL1_SS_CTRL4_M0, 0x0000);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL1_LOCK_PLLCNT_THR, 0x0003);
	}

	cdns_torrent_phy_write(regmap, CMN_PLL0_LOCK_REFCNT_START, 0x0099);
	cdns_torrent_phy_write(regmap, CMN_PLL0_LOCK_PLLCNT_START, 0x0099);
	cdns_torrent_phy_write(regmap, CMN_PLL1_LOCK_REFCNT_START, 0x0099);
	cdns_torrent_phy_write(regmap, CMN_PLL1_LOCK_PLLCNT_START, 0x0099);
}

static
void cdns_torrent_dp_pma_cmn_cfg_25mhz(struct cdns_torrent_phy *cdns_phy)
{
	struct regmap *regmap = cdns_phy->regmap_common_cdb;

	/* refclock registers - assumes 25 MHz refclock */
	cdns_torrent_phy_write(regmap, CMN_SSM_BIAS_TMR, 0x0019);
	cdns_torrent_phy_write(regmap, CMN_PLLSM0_PLLPRE_TMR, 0x0032);
	cdns_torrent_phy_write(regmap, CMN_PLLSM0_PLLLOCK_TMR, 0x00D1);
	cdns_torrent_phy_write(regmap, CMN_PLLSM1_PLLPRE_TMR, 0x0032);
	cdns_torrent_phy_write(regmap, CMN_PLLSM1_PLLLOCK_TMR, 0x00D1);
	cdns_torrent_phy_write(regmap, CMN_BGCAL_INIT_TMR, 0x007D);
	cdns_torrent_phy_write(regmap, CMN_BGCAL_ITER_TMR, 0x007D);
	cdns_torrent_phy_write(regmap, CMN_IBCAL_INIT_TMR, 0x0019);
	cdns_torrent_phy_write(regmap, CMN_TXPUCAL_INIT_TMR, 0x001E);
	cdns_torrent_phy_write(regmap, CMN_TXPUCAL_ITER_TMR, 0x0006);
	cdns_torrent_phy_write(regmap, CMN_TXPDCAL_INIT_TMR, 0x001E);
	cdns_torrent_phy_write(regmap, CMN_TXPDCAL_ITER_TMR, 0x0006);
	cdns_torrent_phy_write(regmap, CMN_RXCAL_INIT_TMR, 0x02EE);
	cdns_torrent_phy_write(regmap, CMN_RXCAL_ITER_TMR, 0x0006);
	cdns_torrent_phy_write(regmap, CMN_SD_CAL_INIT_TMR, 0x0002);
	cdns_torrent_phy_write(regmap, CMN_SD_CAL_ITER_TMR, 0x0002);
	cdns_torrent_phy_write(regmap, CMN_SD_CAL_REFTIM_START, 0x000E);
	cdns_torrent_phy_write(regmap, CMN_SD_CAL_PLLCNT_START, 0x012B);

	/* PLL registers */
	cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL0_CP_PADJ_M0, 0x0509);
	cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL0_CP_IADJ_M0, 0x0F00);
	cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL0_FILT_PADJ_M0, 0x0F08);
	cdns_torrent_phy_write(regmap, CMN_PLL0_DSM_DIAG_M0, 0x0004);
	cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL1_CP_PADJ_M0, 0x0509);
	cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL1_CP_IADJ_M0, 0x0F00);
	cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL1_FILT_PADJ_M0, 0x0F08);
	cdns_torrent_phy_write(regmap, CMN_PLL1_DSM_DIAG_M0, 0x0004);
	cdns_torrent_phy_write(regmap, CMN_PLL0_VCOCAL_INIT_TMR, 0x00FA);
	cdns_torrent_phy_write(regmap, CMN_PLL0_VCOCAL_ITER_TMR, 0x0004);
	cdns_torrent_phy_write(regmap, CMN_PLL1_VCOCAL_INIT_TMR, 0x00FA);
	cdns_torrent_phy_write(regmap, CMN_PLL1_VCOCAL_ITER_TMR, 0x0004);
	cdns_torrent_phy_write(regmap, CMN_PLL0_VCOCAL_REFTIM_START, 0x0317);
	cdns_torrent_phy_write(regmap, CMN_PLL0_VCOCAL_TCTRL, 0x0003);
	cdns_torrent_phy_write(regmap, CMN_PLL1_VCOCAL_REFTIM_START, 0x0317);
	cdns_torrent_phy_write(regmap, CMN_PLL1_VCOCAL_TCTRL, 0x0003);
}

/*
 * Set registers responsible for enabling and configuring SSC, with second
 * register value provided by a parameter.
 */
static void cdns_torrent_dp_enable_ssc_25mhz(struct cdns_torrent_phy *cdns_phy,
					     u32 ctrl2_val)
{
	struct regmap *regmap = cdns_phy->regmap_common_cdb;

	cdns_torrent_phy_write(regmap, CMN_PLL0_SS_CTRL1_M0, 0x0001);
	cdns_torrent_phy_write(regmap, CMN_PLL0_SS_CTRL1_M0, ctrl2_val);
	cdns_torrent_phy_write(regmap, CMN_PLL0_SS_CTRL1_M0, 0x007F);
	cdns_torrent_phy_write(regmap, CMN_PLL0_SS_CTRL4_M0, 0x0003);
	cdns_torrent_phy_write(regmap, CMN_PLL1_SS_CTRL1_M0, 0x0001);
	cdns_torrent_phy_write(regmap, CMN_PLL1_SS_CTRL1_M0, ctrl2_val);
	cdns_torrent_phy_write(regmap, CMN_PLL1_SS_CTRL1_M0, 0x007F);
	cdns_torrent_phy_write(regmap, CMN_PLL1_SS_CTRL4_M0, 0x0003);
}

static
void cdns_torrent_dp_pma_cmn_vco_cfg_25mhz(struct cdns_torrent_phy *cdns_phy,
					   u32 rate, bool ssc)
{
	struct regmap *regmap = cdns_phy->regmap_common_cdb;

	/* Assumes 25 MHz refclock */
	switch (rate) {
	/* Setting VCO for 10.8GHz */
	case 2700:
	case 5400:
		cdns_torrent_phy_write(regmap, CMN_PLL0_INTDIV_M0, 0x01B0);
		cdns_torrent_phy_write(regmap, CMN_PLL0_FRACDIVL_M0, 0x0000);
		cdns_torrent_phy_write(regmap, CMN_PLL0_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(regmap, CMN_PLL0_HIGH_THR_M0, 0x0120);
		cdns_torrent_phy_write(regmap, CMN_PLL1_INTDIV_M0, 0x01B0);
		cdns_torrent_phy_write(regmap, CMN_PLL1_FRACDIVL_M0, 0x0000);
		cdns_torrent_phy_write(regmap, CMN_PLL1_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(regmap, CMN_PLL1_HIGH_THR_M0, 0x0120);
		if (ssc)
			cdns_torrent_dp_enable_ssc_25mhz(cdns_phy, 0x0423);
		break;
	/* Setting VCO for 9.72GHz */
	case 1620:
	case 2430:
	case 3240:
		cdns_torrent_phy_write(regmap, CMN_PLL0_INTDIV_M0, 0x0184);
		cdns_torrent_phy_write(regmap, CMN_PLL0_FRACDIVL_M0, 0xCCCD);
		cdns_torrent_phy_write(regmap, CMN_PLL0_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(regmap, CMN_PLL0_HIGH_THR_M0, 0x0104);
		cdns_torrent_phy_write(regmap, CMN_PLL1_INTDIV_M0, 0x0184);
		cdns_torrent_phy_write(regmap, CMN_PLL1_FRACDIVL_M0, 0xCCCD);
		cdns_torrent_phy_write(regmap, CMN_PLL1_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(regmap, CMN_PLL1_HIGH_THR_M0, 0x0104);
		if (ssc)
			cdns_torrent_dp_enable_ssc_25mhz(cdns_phy, 0x03B9);
		break;
	/* Setting VCO for 8.64GHz */
	case 2160:
	case 4320:
		cdns_torrent_phy_write(regmap, CMN_PLL0_INTDIV_M0, 0x0159);
		cdns_torrent_phy_write(regmap, CMN_PLL0_FRACDIVL_M0, 0x999A);
		cdns_torrent_phy_write(regmap, CMN_PLL0_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(regmap, CMN_PLL0_HIGH_THR_M0, 0x00E7);
		cdns_torrent_phy_write(regmap, CMN_PLL1_INTDIV_M0, 0x0159);
		cdns_torrent_phy_write(regmap, CMN_PLL1_FRACDIVL_M0, 0x999A);
		cdns_torrent_phy_write(regmap, CMN_PLL1_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(regmap, CMN_PLL1_HIGH_THR_M0, 0x00E7);
		if (ssc)
			cdns_torrent_dp_enable_ssc_25mhz(cdns_phy, 0x034F);
		break;
	/* Setting VCO for 8.1GHz */
	case 8100:
		cdns_torrent_phy_write(regmap, CMN_PLL0_INTDIV_M0, 0x0144);
		cdns_torrent_phy_write(regmap, CMN_PLL0_FRACDIVL_M0, 0x0000);
		cdns_torrent_phy_write(regmap, CMN_PLL0_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(regmap, CMN_PLL0_HIGH_THR_M0, 0x00D8);
		cdns_torrent_phy_write(regmap, CMN_PLL1_INTDIV_M0, 0x0144);
		cdns_torrent_phy_write(regmap, CMN_PLL1_FRACDIVL_M0, 0x0000);
		cdns_torrent_phy_write(regmap, CMN_PLL1_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(regmap, CMN_PLL1_HIGH_THR_M0, 0x00D8);
		if (ssc)
			cdns_torrent_dp_enable_ssc_25mhz(cdns_phy, 0x031A);
		break;
	}

	cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL0_CTRL_M0, 0x0002);
	cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL1_CTRL_M0, 0x0002);

	if (ssc) {
		cdns_torrent_phy_write(regmap,
				       CMN_PLL0_VCOCAL_PLLCNT_START, 0x0315);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL0_LOCK_PLLCNT_THR, 0x0005);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL1_VCOCAL_PLLCNT_START, 0x0315);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL1_LOCK_PLLCNT_THR, 0x0005);
	} else {
		cdns_torrent_phy_write(regmap,
				       CMN_PLL0_VCOCAL_PLLCNT_START, 0x0317);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL1_VCOCAL_PLLCNT_START, 0x0317);
		/* Set reset register values to disable SSC */
		cdns_torrent_phy_write(regmap, CMN_PLL0_SS_CTRL1_M0, 0x0002);
		cdns_torrent_phy_write(regmap, CMN_PLL0_SS_CTRL2_M0, 0x0000);
		cdns_torrent_phy_write(regmap, CMN_PLL0_SS_CTRL3_M0, 0x0000);
		cdns_torrent_phy_write(regmap, CMN_PLL0_SS_CTRL4_M0, 0x0000);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL0_LOCK_PLLCNT_THR, 0x0003);
		cdns_torrent_phy_write(regmap, CMN_PLL1_SS_CTRL1_M0, 0x0002);
		cdns_torrent_phy_write(regmap, CMN_PLL1_SS_CTRL2_M0, 0x0000);
		cdns_torrent_phy_write(regmap, CMN_PLL1_SS_CTRL3_M0, 0x0000);
		cdns_torrent_phy_write(regmap, CMN_PLL1_SS_CTRL4_M0, 0x0000);
		cdns_torrent_phy_write(regmap,
				       CMN_PLL1_LOCK_PLLCNT_THR, 0x0003);
	}

	cdns_torrent_phy_write(regmap, CMN_PLL0_LOCK_REFCNT_START, 0x00C7);
	cdns_torrent_phy_write(regmap, CMN_PLL0_LOCK_PLLCNT_START, 0x00C7);
	cdns_torrent_phy_write(regmap, CMN_PLL1_LOCK_REFCNT_START, 0x00C7);
	cdns_torrent_phy_write(regmap, CMN_PLL1_LOCK_PLLCNT_START, 0x00C7);
}

static void cdns_torrent_dp_pma_cmn_rate(struct cdns_torrent_phy *cdns_phy,
					 u32 rate, u32 num_lanes)
{
	unsigned int clk_sel_val = 0;
	unsigned int hsclk_div_val = 0;
	unsigned int i;

	/* 16'h0000 for single DP link configuration */
	regmap_field_write(cdns_phy->phy_pll_cfg, 0x0);

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

	cdns_torrent_phy_write(cdns_phy->regmap_common_cdb,
			       CMN_PDIAG_PLL0_CLK_SEL_M0, clk_sel_val);
	cdns_torrent_phy_write(cdns_phy->regmap_common_cdb,
			       CMN_PDIAG_PLL1_CLK_SEL_M0, clk_sel_val);

	/* PMA lane configuration to deal with multi-link operation */
	for (i = 0; i < num_lanes; i++)
		cdns_torrent_phy_write(cdns_phy->regmap_tx_lane_cdb[i],
				       XCVR_DIAG_HSCLK_DIV, hsclk_div_val);
}

static void cdns_torrent_dp_pma_lane_cfg(struct cdns_torrent_phy *cdns_phy,
					 unsigned int lane)
{
	/* Per lane, refclock-dependent receiver detection setting */
	if (cdns_phy->ref_clk_rate == REF_CLK_19_2MHz)
		cdns_torrent_phy_write(cdns_phy->regmap_tx_lane_cdb[lane],
				       TX_RCVDET_ST_TMR, 0x0780);
	else if (cdns_phy->ref_clk_rate == REF_CLK_25MHz)
		cdns_torrent_phy_write(cdns_phy->regmap_tx_lane_cdb[lane],
				       TX_RCVDET_ST_TMR, 0x09C4);

	/* Writing Tx/Rx Power State Controllers registers */
	cdns_torrent_phy_write(cdns_phy->regmap_tx_lane_cdb[lane],
			       TX_PSC_A0, 0x00FB);
	cdns_torrent_phy_write(cdns_phy->regmap_tx_lane_cdb[lane],
			       TX_PSC_A2, 0x04AA);
	cdns_torrent_phy_write(cdns_phy->regmap_tx_lane_cdb[lane],
			       TX_PSC_A3, 0x04AA);
	cdns_torrent_phy_write(cdns_phy->regmap_rx_lane_cdb[lane],
			       RX_PSC_A0, 0x0000);
	cdns_torrent_phy_write(cdns_phy->regmap_rx_lane_cdb[lane],
			       RX_PSC_A2, 0x0000);
	cdns_torrent_phy_write(cdns_phy->regmap_rx_lane_cdb[lane],
			       RX_PSC_A3, 0x0000);

	cdns_torrent_phy_write(cdns_phy->regmap_rx_lane_cdb[lane],
			       RX_PSC_CAL, 0x0000);

	cdns_torrent_phy_write(cdns_phy->regmap_rx_lane_cdb[lane],
			       RX_REE_GCSM1_CTRL, 0x0000);
	cdns_torrent_phy_write(cdns_phy->regmap_rx_lane_cdb[lane],
			       RX_REE_GCSM2_CTRL, 0x0000);
	cdns_torrent_phy_write(cdns_phy->regmap_rx_lane_cdb[lane],
			       RX_REE_PERGCSM_CTRL, 0x0000);

	cdns_torrent_phy_write(cdns_phy->regmap_tx_lane_cdb[lane],
			       XCVR_DIAG_BIDI_CTRL, 0x000F);
	cdns_torrent_phy_write(cdns_phy->regmap_tx_lane_cdb[lane],
			       XCVR_DIAG_PLLDRC_CTRL, 0x0001);
	cdns_torrent_phy_write(cdns_phy->regmap_tx_lane_cdb[lane],
			       XCVR_DIAG_HSCLK_SEL, 0x0000);
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
	struct regmap *regmap = cdns_phy->regmap_dptx_phy_reg;

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
	cdns_torrent_dp_write(regmap, PHY_PMA_XCVR_POWER_STATE_REQ, value);
	/* Wait, until PHY acknowledges power state completion. */
	ret = regmap_read_poll_timeout(regmap, PHY_PMA_XCVR_POWER_STATE_ACK,
				       read_val, (read_val & mask) == value, 0,
				       POLL_TIMEOUT_US);
	cdns_torrent_dp_write(regmap, PHY_PMA_XCVR_POWER_STATE_REQ, 0x00000000);
	ndelay(100);

	return ret;
}

static int cdns_torrent_dp_run(struct cdns_torrent_phy *cdns_phy, u32 num_lanes)
{
	unsigned int read_val;
	int ret;
	struct regmap *regmap = cdns_phy->regmap_dptx_phy_reg;

	/*
	 * waiting for ACK of pma_xcvr_pllclk_en_ln_*, only for the
	 * master lane
	 */
	ret = regmap_read_poll_timeout(regmap, PHY_PMA_XCVR_PLLCLK_EN_ACK,
				       read_val, read_val & 1,
				       0, POLL_TIMEOUT_US);
	if (ret == -ETIMEDOUT) {
		dev_err(cdns_phy->dev,
			"timeout waiting for link PLL clock enable ack\n");
		return ret;
	}

	ndelay(100);

	ret = cdns_torrent_dp_set_power_state(cdns_phy, num_lanes,
					      POWERSTATE_A2);
	if (ret)
		return ret;

	ret = cdns_torrent_dp_set_power_state(cdns_phy, num_lanes,
					      POWERSTATE_A0);

	return ret;
}


static struct regmap *cdns_regmap_init(struct device *dev, void __iomem *base,
				       u32 block_offset,
				       u8 reg_offset_shift,
				       const struct regmap_config *config)
{
	struct cdns_regmap_cdb_context *ctx;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	ctx->dev = dev;
	ctx->base = base + block_offset;
	ctx->reg_offset_shift = reg_offset_shift;

	return devm_regmap_init(dev, NULL, ctx, config);
}

static int cdns_regfield_init(struct cdns_torrent_phy *cdns_phy)
{
	struct device *dev = cdns_phy->dev;
	struct regmap_field *field;
	struct regmap *regmap;

	regmap = cdns_phy->regmap_phy_pcs_common_cdb;
	field = devm_regmap_field_alloc(dev, regmap, phy_pll_cfg);
	if (IS_ERR(field)) {
		dev_err(dev, "PHY_PLL_CFG reg field init failed\n");
		return PTR_ERR(field);
	}
	cdns_phy->phy_pll_cfg = field;

	regmap = cdns_phy->regmap_phy_pma_common_cdb;
	field = devm_regmap_field_alloc(dev, regmap, phy_pma_cmn_ctrl_2);
	if (IS_ERR(field)) {
		dev_err(dev, "PHY_PMA_CMN_CTRL2 reg field init failed\n");
		return PTR_ERR(field);
	}
	cdns_phy->phy_pma_cmn_ctrl_2 = field;

	regmap = cdns_phy->regmap_phy_pma_common_cdb;
	field = devm_regmap_field_alloc(dev, regmap, phy_pma_pll_raw_ctrl);
	if (IS_ERR(field)) {
		dev_err(dev, "PHY_PMA_PLL_RAW_CTRL reg field init failed\n");
		return PTR_ERR(field);
	}
	cdns_phy->phy_pma_pll_raw_ctrl = field;

	regmap = cdns_phy->regmap_dptx_phy_reg;
	field = devm_regmap_field_alloc(dev, regmap, phy_reset_ctrl);
	if (IS_ERR(field)) {
		dev_err(dev, "PHY_RESET reg field init failed\n");
		return PTR_ERR(field);
	}
	cdns_phy->phy_reset_ctrl = field;

	return 0;
}

static int cdns_regmap_init_torrent_dp(struct cdns_torrent_phy *cdns_phy,
				       void __iomem *sd_base,
				       void __iomem *base,
				       u8 block_offset_shift,
				       u8 reg_offset_shift)
{
	struct device *dev = cdns_phy->dev;
	struct regmap *regmap;
	u32 block_offset;
	int i;

	for (i = 0; i < MAX_NUM_LANES; i++) {
		block_offset = TORRENT_TX_LANE_CDB_OFFSET(i, block_offset_shift,
							  reg_offset_shift);
		regmap = cdns_regmap_init(dev, sd_base, block_offset,
					  reg_offset_shift,
					  &cdns_torrent_tx_lane_cdb_config[i]);
		if (IS_ERR(regmap)) {
			dev_err(dev, "Failed to init tx lane CDB regmap\n");
			return PTR_ERR(regmap);
		}
		cdns_phy->regmap_tx_lane_cdb[i] = regmap;

		block_offset = TORRENT_RX_LANE_CDB_OFFSET(i, block_offset_shift,
							  reg_offset_shift);
		regmap = cdns_regmap_init(dev, sd_base, block_offset,
					  reg_offset_shift,
					  &cdns_torrent_rx_lane_cdb_config[i]);
		if (IS_ERR(regmap)) {
			dev_err(dev, "Failed to init rx lane CDB regmap\n");
			return PTR_ERR(regmap);
		}
		cdns_phy->regmap_rx_lane_cdb[i] = regmap;
	}

	block_offset = TORRENT_COMMON_CDB_OFFSET;
	regmap = cdns_regmap_init(dev, sd_base, block_offset,
				  reg_offset_shift,
				  &cdns_torrent_common_cdb_config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "Failed to init common CDB regmap\n");
		return PTR_ERR(regmap);
	}
	cdns_phy->regmap_common_cdb = regmap;

	block_offset = TORRENT_PHY_PCS_COMMON_OFFSET(block_offset_shift);
	regmap = cdns_regmap_init(dev, sd_base, block_offset,
				  reg_offset_shift,
				  &cdns_torrent_phy_pcs_cmn_cdb_config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "Failed to init PHY PCS common CDB regmap\n");
		return PTR_ERR(regmap);
	}
	cdns_phy->regmap_phy_pcs_common_cdb = regmap;

	block_offset = TORRENT_PHY_PMA_COMMON_OFFSET(block_offset_shift);
	regmap = cdns_regmap_init(dev, sd_base, block_offset,
				  reg_offset_shift,
				  &cdns_torrent_phy_pma_cmn_cdb_config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "Failed to init PHY PMA common CDB regmap\n");
		return PTR_ERR(regmap);
	}
	cdns_phy->regmap_phy_pma_common_cdb = regmap;

	block_offset = TORRENT_DPTX_PHY_OFFSET;
	regmap = cdns_regmap_init(dev, base, block_offset,
				  reg_offset_shift,
				  &cdns_torrent_dptx_phy_config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "Failed to init DPTX PHY regmap\n");
		return PTR_ERR(regmap);
	}
	cdns_phy->regmap_dptx_phy_reg = regmap;

	return 0;
}

static int cdns_torrent_phy_probe(struct platform_device *pdev)
{
	struct resource *regs;
	struct cdns_torrent_phy *cdns_phy;
	struct device *dev = &pdev->dev;
	struct phy_provider *phy_provider;
	const struct of_device_id *match;
	struct cdns_torrent_data *data;
	struct phy *phy;
	int err, ret;

	/* Get init data for this PHY */
	match = of_match_device(cdns_torrent_phy_of_match, dev);
	if (!match)
		return -EINVAL;

	data = (struct cdns_torrent_data *)match->data;

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
	cdns_phy->sd_base = devm_ioremap_resource(&pdev->dev, regs);
	if (IS_ERR(cdns_phy->sd_base))
		return PTR_ERR(cdns_phy->sd_base);

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	cdns_phy->base = devm_ioremap_resource(&pdev->dev, regs);
	if (IS_ERR(cdns_phy->base))
		return PTR_ERR(cdns_phy->base);


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

	ret = cdns_regmap_init_torrent_dp(cdns_phy, cdns_phy->sd_base,
					  cdns_phy->base,
					  data->block_offset_shift,
					  data->reg_offset_shift);
	if (ret)
		return ret;

	ret = cdns_regfield_init(cdns_phy);
	if (ret)
		return ret;

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	dev_info(dev, "%d lanes, max bit rate %d.%03d Gbps\n",
		 cdns_phy->num_lanes,
		 cdns_phy->max_bit_rate / 1000,
		 cdns_phy->max_bit_rate % 1000);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct cdns_torrent_data cdns_map_torrent = {
	.block_offset_shift = 0x2,
	.reg_offset_shift = 0x2,
};

static const struct cdns_torrent_data ti_j721e_map_torrent = {
	.block_offset_shift = 0x0,
	.reg_offset_shift = 0x1,
};

static const struct of_device_id cdns_torrent_phy_of_match[] = {
	{
		.compatible = "cdns,torrent-phy",
		.data = &cdns_map_torrent,
	},
	{
		.compatible = "ti,j721e-serdes-10g",
		.data = &ti_j721e_map_torrent,
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
