// SPDX-License-Identifier: GPL-2.0-only
/*
 * Cadence Torrent SD0801 PHY driver.
 *
 * Copyright 2018 Cadence Design Systems, Inc.
 *
 */

#include <dt-bindings/phy/phy.h>
#include <dt-bindings/phy/phy-cadence.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
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
#include <linux/reset.h>
#include <linux/regmap.h>

#define REF_CLK_19_2MHZ		19200000
#define REF_CLK_25MHZ		25000000
#define REF_CLK_100MHZ		100000000

#define MAX_NUM_LANES		4
#define DEFAULT_MAX_BIT_RATE	8100 /* in Mbps */

#define NUM_SSC_MODE		3
#define NUM_REF_CLK		3
#define NUM_PHY_TYPE		6

#define POLL_TIMEOUT_US		5000
#define PLL_LOCK_TIMEOUT	100000

#define TORRENT_COMMON_CDB_OFFSET	0x0

#define TORRENT_TX_LANE_CDB_OFFSET(ln, block_offset, reg_offset)	\
				((0x4000 << (block_offset)) +		\
				(((ln) << 9) << (reg_offset)))

#define TORRENT_RX_LANE_CDB_OFFSET(ln, block_offset, reg_offset)	\
				((0x8000 << (block_offset)) +		\
				(((ln) << 9) << (reg_offset)))

#define TORRENT_PHY_PCS_COMMON_OFFSET(block_offset)	\
				(0xC000 << (block_offset))

#define TORRENT_PHY_PCS_LANE_CDB_OFFSET(ln, block_offset, reg_offset)	\
				((0xD000 << (block_offset)) +		\
				(((ln) << 8) << (reg_offset)))

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
#define CMN_CDIAG_CDB_PWRI_OVRD		0x0041U
#define CMN_CDIAG_XCVRC_PWRI_OVRD	0x0047U
#define CMN_CDIAG_REFCLK_OVRD		0x004CU
#define CMN_CDIAG_REFCLK_DRV0_CTRL	0x0050U
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
#define CMN_PLL0_DSM_FBH_OVRD_M0	0x0095U
#define CMN_PLL0_SS_CTRL1_M0		0x0098U
#define CMN_PLL0_SS_CTRL2_M0            0x0099U
#define CMN_PLL0_SS_CTRL3_M0            0x009AU
#define CMN_PLL0_SS_CTRL4_M0            0x009BU
#define CMN_PLL0_LOCK_REFCNT_START      0x009CU
#define CMN_PLL0_LOCK_PLLCNT_START	0x009EU
#define CMN_PLL0_LOCK_PLLCNT_THR        0x009FU
#define CMN_PLL0_INTDIV_M1		0x00A0U
#define CMN_PLL0_FRACDIVH_M1		0x00A2U
#define CMN_PLL0_HIGH_THR_M1		0x00A3U
#define CMN_PLL0_DSM_DIAG_M1		0x00A4U
#define CMN_PLL0_SS_CTRL1_M1		0x00A8U
#define CMN_PLL0_SS_CTRL2_M1		0x00A9U
#define CMN_PLL0_SS_CTRL3_M1		0x00AAU
#define CMN_PLL0_SS_CTRL4_M1		0x00ABU
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
#define CMN_PLL1_DSM_FBH_OVRD_M0	0x00D5U
#define CMN_PLL1_DSM_FBL_OVRD_M0	0x00D6U
#define CMN_PLL1_SS_CTRL1_M0		0x00D8U
#define CMN_PLL1_SS_CTRL2_M0            0x00D9U
#define CMN_PLL1_SS_CTRL3_M0            0x00DAU
#define CMN_PLL1_SS_CTRL4_M0            0x00DBU
#define CMN_PLL1_LOCK_REFCNT_START      0x00DCU
#define CMN_PLL1_LOCK_PLLCNT_START	0x00DEU
#define CMN_PLL1_LOCK_PLLCNT_THR        0x00DFU
#define CMN_TXPUCAL_TUNE		0x0103U
#define CMN_TXPUCAL_INIT_TMR		0x0104U
#define CMN_TXPUCAL_ITER_TMR		0x0105U
#define CMN_TXPDCAL_TUNE		0x010BU
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
#define CMN_PDIAG_PLL0_CTRL_M1		0x01B0U
#define CMN_PDIAG_PLL0_CLK_SEL_M1	0x01B1U
#define CMN_PDIAG_PLL0_CP_PADJ_M1	0x01B4U
#define CMN_PDIAG_PLL0_CP_IADJ_M1	0x01B5U
#define CMN_PDIAG_PLL0_FILT_PADJ_M1	0x01B6U
#define CMN_PDIAG_PLL1_CTRL_M0		0x01C0U
#define CMN_PDIAG_PLL1_CLK_SEL_M0	0x01C1U
#define CMN_PDIAG_PLL1_CP_PADJ_M0	0x01C4U
#define CMN_PDIAG_PLL1_CP_IADJ_M0	0x01C5U
#define CMN_PDIAG_PLL1_FILT_PADJ_M0	0x01C6U
#define CMN_DIAG_BIAS_OVRD1		0x01E1U

/* PMA TX Lane registers */
#define TX_TXCC_CTRL			0x0040U
#define TX_TXCC_CPOST_MULT_00		0x004CU
#define TX_TXCC_CPOST_MULT_01		0x004DU
#define TX_TXCC_MGNFS_MULT_000		0x0050U
#define TX_TXCC_MGNFS_MULT_100		0x0054U
#define DRV_DIAG_TX_DRV			0x00C6U
#define XCVR_DIAG_PLLDRC_CTRL		0x00E5U
#define XCVR_DIAG_HSCLK_SEL		0x00E6U
#define XCVR_DIAG_HSCLK_DIV		0x00E7U
#define XCVR_DIAG_RXCLK_CTRL		0x00E9U
#define XCVR_DIAG_BIDI_CTRL		0x00EAU
#define XCVR_DIAG_PSC_OVRD		0x00EBU
#define TX_PSC_A0			0x0100U
#define TX_PSC_A1			0x0101U
#define TX_PSC_A2			0x0102U
#define TX_PSC_A3			0x0103U
#define TX_RCVDET_ST_TMR		0x0123U
#define TX_DIAG_ACYA			0x01E7U
#define TX_DIAG_ACYA_HBDC_MASK		0x0001U

/* PMA RX Lane registers */
#define RX_PSC_A0			0x0000U
#define RX_PSC_A1			0x0001U
#define RX_PSC_A2			0x0002U
#define RX_PSC_A3			0x0003U
#define RX_PSC_CAL			0x0006U
#define RX_CDRLF_CNFG			0x0080U
#define RX_CDRLF_CNFG3			0x0082U
#define RX_SIGDET_HL_FILT_TMR		0x0090U
#define RX_REE_GCSM1_CTRL		0x0108U
#define RX_REE_GCSM1_EQENM_PH1		0x0109U
#define RX_REE_GCSM1_EQENM_PH2		0x010AU
#define RX_REE_GCSM2_CTRL		0x0110U
#define RX_REE_PERGCSM_CTRL		0x0118U
#define RX_REE_ATTEN_THR		0x0149U
#define RX_REE_TAP1_CLIP		0x0171U
#define RX_REE_TAP2TON_CLIP		0x0172U
#define RX_REE_SMGM_CTRL1		0x0177U
#define RX_REE_SMGM_CTRL2		0x0178U
#define RX_DIAG_DFE_CTRL		0x01E0U
#define RX_DIAG_DFE_AMP_TUNE_2		0x01E2U
#define RX_DIAG_DFE_AMP_TUNE_3		0x01E3U
#define RX_DIAG_NQST_CTRL		0x01E5U
#define RX_DIAG_SIGDET_TUNE		0x01E8U
#define RX_DIAG_PI_RATE			0x01F4U
#define RX_DIAG_PI_CAP			0x01F5U
#define RX_DIAG_ACYA			0x01FFU

/* PHY PCS common registers */
#define PHY_PIPE_CMN_CTRL1		0x0000U
#define PHY_PLL_CFG			0x000EU
#define PHY_PIPE_USB3_GEN2_PRE_CFG0	0x0020U
#define PHY_PIPE_USB3_GEN2_POST_CFG0	0x0022U
#define PHY_PIPE_USB3_GEN2_POST_CFG1	0x0023U

/* PHY PCS lane registers */
#define PHY_PCS_ISO_LINK_CTRL		0x000BU

/* PHY PMA common registers */
#define PHY_PMA_CMN_CTRL1		0x0000U
#define PHY_PMA_CMN_CTRL2		0x0001U
#define PHY_PMA_PLL_RAW_CTRL		0x0003U

#define CDNS_TORRENT_OUTPUT_CLOCKS	3

static const char * const clk_names[] = {
	[CDNS_TORRENT_REFCLK_DRIVER] = "refclk-driver",
	[CDNS_TORRENT_DERIVED_REFCLK] = "refclk-der",
	[CDNS_TORRENT_RECEIVED_REFCLK] = "refclk-rec",
};

static const struct reg_field phy_pll_cfg =
				REG_FIELD(PHY_PLL_CFG, 0, 1);

static const struct reg_field phy_pma_cmn_ctrl_1 =
				REG_FIELD(PHY_PMA_CMN_CTRL1, 0, 0);

static const struct reg_field phy_pma_cmn_ctrl_2 =
				REG_FIELD(PHY_PMA_CMN_CTRL2, 0, 7);

static const struct reg_field phy_pma_pll_raw_ctrl =
				REG_FIELD(PHY_PMA_PLL_RAW_CTRL, 0, 1);

static const struct reg_field phy_reset_ctrl =
				REG_FIELD(PHY_RESET, 8, 8);

static const struct reg_field phy_pcs_iso_link_ctrl_1 =
				REG_FIELD(PHY_PCS_ISO_LINK_CTRL, 1, 1);

static const struct reg_field phy_pipe_cmn_ctrl1_0 = REG_FIELD(PHY_PIPE_CMN_CTRL1, 0, 0);

static const struct reg_field cmn_cdiag_refclk_ovrd_4 =
				REG_FIELD(CMN_CDIAG_REFCLK_OVRD, 4, 4);

#define REFCLK_OUT_NUM_CMN_CONFIG	4

enum cdns_torrent_refclk_out_cmn {
	CMN_CDIAG_REFCLK_DRV0_CTRL_1,
	CMN_CDIAG_REFCLK_DRV0_CTRL_4,
	CMN_CDIAG_REFCLK_DRV0_CTRL_5,
	CMN_CDIAG_REFCLK_DRV0_CTRL_6,
};

static const struct reg_field refclk_out_cmn_cfg[] = {
	[CMN_CDIAG_REFCLK_DRV0_CTRL_1]	= REG_FIELD(CMN_CDIAG_REFCLK_DRV0_CTRL, 1, 1),
	[CMN_CDIAG_REFCLK_DRV0_CTRL_4]	= REG_FIELD(CMN_CDIAG_REFCLK_DRV0_CTRL, 4, 4),
	[CMN_CDIAG_REFCLK_DRV0_CTRL_5]  = REG_FIELD(CMN_CDIAG_REFCLK_DRV0_CTRL, 5, 5),
	[CMN_CDIAG_REFCLK_DRV0_CTRL_6]	= REG_FIELD(CMN_CDIAG_REFCLK_DRV0_CTRL, 6, 6),
};

static const int refclk_driver_parent_index[] = {
	CDNS_TORRENT_DERIVED_REFCLK,
	CDNS_TORRENT_RECEIVED_REFCLK
};

static u32 cdns_torrent_refclk_driver_mux_table[] = { 1, 0 };

enum cdns_torrent_phy_type {
	TYPE_NONE,
	TYPE_DP,
	TYPE_PCIE,
	TYPE_SGMII,
	TYPE_QSGMII,
	TYPE_USB,
};

enum cdns_torrent_ref_clk {
	CLK_19_2_MHZ,
	CLK_25_MHZ,
	CLK_100_MHZ
};

enum cdns_torrent_ssc_mode {
	NO_SSC,
	EXTERNAL_SSC,
	INTERNAL_SSC
};

struct cdns_torrent_inst {
	struct phy *phy;
	u32 mlane;
	enum cdns_torrent_phy_type phy_type;
	u32 num_lanes;
	struct reset_control *lnk_rst;
	enum cdns_torrent_ssc_mode ssc_mode;
};

struct cdns_torrent_phy {
	void __iomem *base;	/* DPTX registers base */
	void __iomem *sd_base; /* SD0801 registers base */
	u32 max_bit_rate; /* Maximum link bit rate to use (in Mbps) */
	struct reset_control *phy_rst;
	struct reset_control *apb_rst;
	struct device *dev;
	struct clk *clk;
	enum cdns_torrent_ref_clk ref_clk_rate;
	struct cdns_torrent_inst phys[MAX_NUM_LANES];
	int nsubnodes;
	const struct cdns_torrent_data *init_data;
	struct regmap *regmap_common_cdb;
	struct regmap *regmap_phy_pcs_common_cdb;
	struct regmap *regmap_phy_pma_common_cdb;
	struct regmap *regmap_tx_lane_cdb[MAX_NUM_LANES];
	struct regmap *regmap_rx_lane_cdb[MAX_NUM_LANES];
	struct regmap *regmap_phy_pcs_lane_cdb[MAX_NUM_LANES];
	struct regmap *regmap_dptx_phy_reg;
	struct regmap_field *phy_pll_cfg;
	struct regmap_field *phy_pipe_cmn_ctrl1_0;
	struct regmap_field *cmn_cdiag_refclk_ovrd_4;
	struct regmap_field *phy_pma_cmn_ctrl_1;
	struct regmap_field *phy_pma_cmn_ctrl_2;
	struct regmap_field *phy_pma_pll_raw_ctrl;
	struct regmap_field *phy_reset_ctrl;
	struct regmap_field *phy_pcs_iso_link_ctrl_1[MAX_NUM_LANES];
	struct clk_hw_onecell_data *clk_hw_data;
};

enum phy_powerstate {
	POWERSTATE_A0 = 0,
	/* Powerstate A1 is unused */
	POWERSTATE_A2 = 2,
	POWERSTATE_A3 = 3,
};

struct cdns_torrent_refclk_driver {
	struct clk_hw		hw;
	struct regmap_field	*cmn_fields[REFCLK_OUT_NUM_CMN_CONFIG];
	struct clk_init_data	clk_data;
};

#define to_cdns_torrent_refclk_driver(_hw)	\
			container_of(_hw, struct cdns_torrent_refclk_driver, hw)

struct cdns_torrent_derived_refclk {
	struct clk_hw		hw;
	struct regmap_field	*phy_pipe_cmn_ctrl1_0;
	struct regmap_field	*cmn_cdiag_refclk_ovrd_4;
	struct clk_init_data	clk_data;
};

#define to_cdns_torrent_derived_refclk(_hw)	\
			container_of(_hw, struct cdns_torrent_derived_refclk, hw)

struct cdns_torrent_received_refclk {
	struct clk_hw		hw;
	struct regmap_field	*phy_pipe_cmn_ctrl1_0;
	struct regmap_field	*cmn_cdiag_refclk_ovrd_4;
	struct clk_init_data	clk_data;
};

#define to_cdns_torrent_received_refclk(_hw)	\
			container_of(_hw, struct cdns_torrent_received_refclk, hw)

struct cdns_reg_pairs {
	u32 val;
	u32 off;
};

struct cdns_torrent_vals {
	struct cdns_reg_pairs *reg_pairs;
	u32 num_regs;
};

struct cdns_torrent_data {
	u8 block_offset_shift;
	u8 reg_offset_shift;
	struct cdns_torrent_vals *link_cmn_vals[NUM_PHY_TYPE][NUM_PHY_TYPE]
					       [NUM_SSC_MODE];
	struct cdns_torrent_vals *xcvr_diag_vals[NUM_PHY_TYPE][NUM_PHY_TYPE]
						[NUM_SSC_MODE];
	struct cdns_torrent_vals *pcs_cmn_vals[NUM_PHY_TYPE][NUM_PHY_TYPE]
					      [NUM_SSC_MODE];
	struct cdns_torrent_vals *cmn_vals[NUM_REF_CLK][NUM_PHY_TYPE]
					  [NUM_PHY_TYPE][NUM_SSC_MODE];
	struct cdns_torrent_vals *tx_ln_vals[NUM_REF_CLK][NUM_PHY_TYPE]
					    [NUM_PHY_TYPE][NUM_SSC_MODE];
	struct cdns_torrent_vals *rx_ln_vals[NUM_REF_CLK][NUM_PHY_TYPE]
					    [NUM_PHY_TYPE][NUM_SSC_MODE];
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

static const struct regmap_config cdns_torrent_tx_lane_cdb_config[] = {
	TORRENT_TX_LANE_CDB_REGMAP_CONF("0"),
	TORRENT_TX_LANE_CDB_REGMAP_CONF("1"),
	TORRENT_TX_LANE_CDB_REGMAP_CONF("2"),
	TORRENT_TX_LANE_CDB_REGMAP_CONF("3"),
};

static const struct regmap_config cdns_torrent_rx_lane_cdb_config[] = {
	TORRENT_RX_LANE_CDB_REGMAP_CONF("0"),
	TORRENT_RX_LANE_CDB_REGMAP_CONF("1"),
	TORRENT_RX_LANE_CDB_REGMAP_CONF("2"),
	TORRENT_RX_LANE_CDB_REGMAP_CONF("3"),
};

static const struct regmap_config cdns_torrent_common_cdb_config = {
	.name = "torrent_common_cdb",
	.reg_stride = 1,
	.fast_io = true,
	.reg_write = cdns_regmap_write,
	.reg_read = cdns_regmap_read,
};

#define TORRENT_PHY_PCS_LANE_CDB_REGMAP_CONF(n) \
{ \
	.name = "torrent_phy_pcs_lane" n "_cdb", \
	.reg_stride = 1, \
	.fast_io = true, \
	.reg_write = cdns_regmap_write, \
	.reg_read = cdns_regmap_read, \
}

static const struct regmap_config cdns_torrent_phy_pcs_lane_cdb_config[] = {
	TORRENT_PHY_PCS_LANE_CDB_REGMAP_CONF("0"),
	TORRENT_PHY_PCS_LANE_CDB_REGMAP_CONF("1"),
	TORRENT_PHY_PCS_LANE_CDB_REGMAP_CONF("2"),
	TORRENT_PHY_PCS_LANE_CDB_REGMAP_CONF("3"),
};

static const struct regmap_config cdns_torrent_phy_pcs_cmn_cdb_config = {
	.name = "torrent_phy_pcs_cmn_cdb",
	.reg_stride = 1,
	.fast_io = true,
	.reg_write = cdns_regmap_write,
	.reg_read = cdns_regmap_read,
};

static const struct regmap_config cdns_torrent_phy_pma_cmn_cdb_config = {
	.name = "torrent_phy_pma_cmn_cdb",
	.reg_stride = 1,
	.fast_io = true,
	.reg_write = cdns_regmap_write,
	.reg_read = cdns_regmap_read,
};

static const struct regmap_config cdns_torrent_dptx_phy_config = {
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

static const char *cdns_torrent_get_phy_type(enum cdns_torrent_phy_type phy_type)
{
	switch (phy_type) {
	case TYPE_DP:
		return "DisplayPort";
	case TYPE_PCIE:
		return "PCIe";
	case TYPE_SGMII:
		return "SGMII";
	case TYPE_QSGMII:
		return "QSGMII";
	case TYPE_USB:
		return "USB";
	default:
		return "None";
	}
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
		cdns_torrent_phy_write(regmap, CMN_PLL0_INTDIV_M0, 0x0119);
		cdns_torrent_phy_write(regmap, CMN_PLL0_FRACDIVL_M0, 0x4000);
		cdns_torrent_phy_write(regmap, CMN_PLL0_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(regmap, CMN_PLL0_HIGH_THR_M0, 0x00BC);
		cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL0_CTRL_M0, 0x0012);
		cdns_torrent_phy_write(regmap, CMN_PLL1_INTDIV_M0, 0x0119);
		cdns_torrent_phy_write(regmap, CMN_PLL1_FRACDIVL_M0, 0x4000);
		cdns_torrent_phy_write(regmap, CMN_PLL1_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(regmap, CMN_PLL1_HIGH_THR_M0, 0x00BC);
		cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL1_CTRL_M0, 0x0012);
		if (ssc)
			cdns_torrent_dp_enable_ssc_19_2mhz(cdns_phy, 0x033A, 0x006A);
		break;
	/* Setting VCO for 9.72GHz */
	case 1620:
	case 2430:
	case 3240:
		cdns_torrent_phy_write(regmap, CMN_PLL0_INTDIV_M0, 0x01FA);
		cdns_torrent_phy_write(regmap, CMN_PLL0_FRACDIVL_M0, 0x4000);
		cdns_torrent_phy_write(regmap, CMN_PLL0_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(regmap, CMN_PLL0_HIGH_THR_M0, 0x0152);
		cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL0_CTRL_M0, 0x0002);
		cdns_torrent_phy_write(regmap, CMN_PLL1_INTDIV_M0, 0x01FA);
		cdns_torrent_phy_write(regmap, CMN_PLL1_FRACDIVL_M0, 0x4000);
		cdns_torrent_phy_write(regmap, CMN_PLL1_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(regmap, CMN_PLL1_HIGH_THR_M0, 0x0152);
		cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL1_CTRL_M0, 0x0002);
		if (ssc)
			cdns_torrent_dp_enable_ssc_19_2mhz(cdns_phy, 0x05DD, 0x0069);
		break;
	/* Setting VCO for 8.64GHz */
	case 2160:
	case 4320:
		cdns_torrent_phy_write(regmap, CMN_PLL0_INTDIV_M0, 0x01C2);
		cdns_torrent_phy_write(regmap, CMN_PLL0_FRACDIVL_M0, 0x0000);
		cdns_torrent_phy_write(regmap, CMN_PLL0_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(regmap, CMN_PLL0_HIGH_THR_M0, 0x012C);
		cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL0_CTRL_M0, 0x0002);
		cdns_torrent_phy_write(regmap, CMN_PLL1_INTDIV_M0, 0x01C2);
		cdns_torrent_phy_write(regmap, CMN_PLL1_FRACDIVL_M0, 0x0000);
		cdns_torrent_phy_write(regmap, CMN_PLL1_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(regmap, CMN_PLL1_HIGH_THR_M0, 0x012C);
		cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL1_CTRL_M0, 0x0002);
		if (ssc)
			cdns_torrent_dp_enable_ssc_19_2mhz(cdns_phy, 0x0536, 0x0069);
		break;
	/* Setting VCO for 8.1GHz */
	case 8100:
		cdns_torrent_phy_write(regmap, CMN_PLL0_INTDIV_M0, 0x01A5);
		cdns_torrent_phy_write(regmap, CMN_PLL0_FRACDIVL_M0, 0xE000);
		cdns_torrent_phy_write(regmap, CMN_PLL0_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(regmap, CMN_PLL0_HIGH_THR_M0, 0x011A);
		cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL0_CTRL_M0, 0x0002);
		cdns_torrent_phy_write(regmap, CMN_PLL1_INTDIV_M0, 0x01A5);
		cdns_torrent_phy_write(regmap, CMN_PLL1_FRACDIVL_M0, 0xE000);
		cdns_torrent_phy_write(regmap, CMN_PLL1_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(regmap, CMN_PLL1_HIGH_THR_M0, 0x011A);
		cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL1_CTRL_M0, 0x0002);
		if (ssc)
			cdns_torrent_dp_enable_ssc_19_2mhz(cdns_phy, 0x04D7, 0x006A);
		break;
	}

	if (ssc) {
		cdns_torrent_phy_write(regmap, CMN_PLL0_VCOCAL_PLLCNT_START, 0x025E);
		cdns_torrent_phy_write(regmap, CMN_PLL0_LOCK_PLLCNT_THR, 0x0005);
		cdns_torrent_phy_write(regmap, CMN_PLL1_VCOCAL_PLLCNT_START, 0x025E);
		cdns_torrent_phy_write(regmap, CMN_PLL1_LOCK_PLLCNT_THR, 0x0005);
	} else {
		cdns_torrent_phy_write(regmap, CMN_PLL0_VCOCAL_PLLCNT_START, 0x0260);
		cdns_torrent_phy_write(regmap, CMN_PLL1_VCOCAL_PLLCNT_START, 0x0260);
		/* Set reset register values to disable SSC */
		cdns_torrent_phy_write(regmap, CMN_PLL0_SS_CTRL1_M0, 0x0002);
		cdns_torrent_phy_write(regmap, CMN_PLL0_SS_CTRL2_M0, 0x0000);
		cdns_torrent_phy_write(regmap, CMN_PLL0_SS_CTRL3_M0, 0x0000);
		cdns_torrent_phy_write(regmap, CMN_PLL0_SS_CTRL4_M0, 0x0000);
		cdns_torrent_phy_write(regmap, CMN_PLL0_LOCK_PLLCNT_THR, 0x0003);
		cdns_torrent_phy_write(regmap, CMN_PLL1_SS_CTRL1_M0, 0x0002);
		cdns_torrent_phy_write(regmap, CMN_PLL1_SS_CTRL2_M0, 0x0000);
		cdns_torrent_phy_write(regmap, CMN_PLL1_SS_CTRL3_M0, 0x0000);
		cdns_torrent_phy_write(regmap, CMN_PLL1_SS_CTRL4_M0, 0x0000);
		cdns_torrent_phy_write(regmap, CMN_PLL1_LOCK_PLLCNT_THR, 0x0003);
	}

	cdns_torrent_phy_write(regmap, CMN_PLL0_LOCK_REFCNT_START, 0x0099);
	cdns_torrent_phy_write(regmap, CMN_PLL0_LOCK_PLLCNT_START, 0x0099);
	cdns_torrent_phy_write(regmap, CMN_PLL1_LOCK_REFCNT_START, 0x0099);
	cdns_torrent_phy_write(regmap, CMN_PLL1_LOCK_PLLCNT_START, 0x0099);
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

static
void cdns_torrent_dp_pma_cmn_vco_cfg_100mhz(struct cdns_torrent_phy *cdns_phy,
					    u32 rate, bool ssc)
{
	struct regmap *regmap = cdns_phy->regmap_common_cdb;

	/* Assumes 100 MHz refclock */
	switch (rate) {
	/* Setting VCO for 10.8GHz */
	case 2700:
	case 5400:
		cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL1_CP_PADJ_M0, 0x0028);
		cdns_torrent_phy_write(regmap, CMN_PLL0_DSM_FBH_OVRD_M0, 0x0022);
		cdns_torrent_phy_write(regmap, CMN_PLL1_DSM_FBH_OVRD_M0, 0x0022);
		cdns_torrent_phy_write(regmap, CMN_PLL1_DSM_FBL_OVRD_M0, 0x000C);
		break;
	/* Setting VCO for 9.72GHz */
	case 1620:
	case 2430:
	case 3240:
		cdns_torrent_phy_write(regmap, CMN_PLL0_DSM_DIAG_M0, 0x0004);
		cdns_torrent_phy_write(regmap, CMN_PLL1_DSM_DIAG_M0, 0x0004);
		cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL0_CP_PADJ_M0, 0x0509);
		cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL1_CP_PADJ_M0, 0x0509);
		cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL0_CP_IADJ_M0, 0x0F00);
		cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL1_CP_IADJ_M0, 0x0F00);
		cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL0_FILT_PADJ_M0, 0x0F08);
		cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL1_FILT_PADJ_M0, 0x0F08);
		cdns_torrent_phy_write(regmap, CMN_PLL0_INTDIV_M0, 0x0061);
		cdns_torrent_phy_write(regmap, CMN_PLL1_INTDIV_M0, 0x0061);
		cdns_torrent_phy_write(regmap, CMN_PLL0_FRACDIVL_M0, 0x3333);
		cdns_torrent_phy_write(regmap, CMN_PLL1_FRACDIVL_M0, 0x3333);
		cdns_torrent_phy_write(regmap, CMN_PLL0_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(regmap, CMN_PLL1_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(regmap, CMN_PLL0_HIGH_THR_M0, 0x0042);
		cdns_torrent_phy_write(regmap, CMN_PLL1_HIGH_THR_M0, 0x0042);
		cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL0_CTRL_M0, 0x0002);
		cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL1_CTRL_M0, 0x0002);
		break;
	/* Setting VCO for 8.64GHz */
	case 2160:
	case 4320:
		cdns_torrent_phy_write(regmap, CMN_PLL0_DSM_DIAG_M0, 0x0004);
		cdns_torrent_phy_write(regmap, CMN_PLL1_DSM_DIAG_M0, 0x0004);
		cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL0_CP_PADJ_M0, 0x0509);
		cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL1_CP_PADJ_M0, 0x0509);
		cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL0_CP_IADJ_M0, 0x0F00);
		cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL1_CP_IADJ_M0, 0x0F00);
		cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL0_FILT_PADJ_M0, 0x0F08);
		cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL1_FILT_PADJ_M0, 0x0F08);
		cdns_torrent_phy_write(regmap, CMN_PLL0_INTDIV_M0, 0x0056);
		cdns_torrent_phy_write(regmap, CMN_PLL1_INTDIV_M0, 0x0056);
		cdns_torrent_phy_write(regmap, CMN_PLL0_FRACDIVL_M0, 0x6666);
		cdns_torrent_phy_write(regmap, CMN_PLL1_FRACDIVL_M0, 0x6666);
		cdns_torrent_phy_write(regmap, CMN_PLL0_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(regmap, CMN_PLL1_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(regmap, CMN_PLL0_HIGH_THR_M0, 0x003A);
		cdns_torrent_phy_write(regmap, CMN_PLL1_HIGH_THR_M0, 0x003A);
		cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL0_CTRL_M0, 0x0002);
		cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL1_CTRL_M0, 0x0002);
		break;
	/* Setting VCO for 8.1GHz */
	case 8100:
		cdns_torrent_phy_write(regmap, CMN_PLL0_DSM_DIAG_M0, 0x0004);
		cdns_torrent_phy_write(regmap, CMN_PLL1_DSM_DIAG_M0, 0x0004);
		cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL0_CP_PADJ_M0, 0x0509);
		cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL1_CP_PADJ_M0, 0x0509);
		cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL0_CP_IADJ_M0, 0x0F00);
		cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL1_CP_IADJ_M0, 0x0F00);
		cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL0_FILT_PADJ_M0, 0x0F08);
		cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL1_FILT_PADJ_M0, 0x0F08);
		cdns_torrent_phy_write(regmap, CMN_PLL0_INTDIV_M0, 0x0051);
		cdns_torrent_phy_write(regmap, CMN_PLL1_INTDIV_M0, 0x0051);
		cdns_torrent_phy_write(regmap, CMN_PLL0_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(regmap, CMN_PLL1_FRACDIVH_M0, 0x0002);
		cdns_torrent_phy_write(regmap, CMN_PLL0_HIGH_THR_M0, 0x0036);
		cdns_torrent_phy_write(regmap, CMN_PLL1_HIGH_THR_M0, 0x0036);
		cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL0_CTRL_M0, 0x0002);
		cdns_torrent_phy_write(regmap, CMN_PDIAG_PLL1_CTRL_M0, 0x0002);
		break;
	}
}

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

static int cdns_torrent_dp_wait_pma_cmn_ready(struct cdns_torrent_phy *cdns_phy)
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

static void cdns_torrent_dp_pma_cmn_rate(struct cdns_torrent_phy *cdns_phy,
					 u32 rate, u32 num_lanes)
{
	unsigned int clk_sel_val = 0;
	unsigned int hsclk_div_val = 0;
	unsigned int i;

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

/*
 * Perform register operations related to setting link rate, once powerstate is
 * set and PLL disable request was processed.
 */
static int cdns_torrent_dp_configure_rate(struct cdns_torrent_phy *cdns_phy,
					  struct phy_configure_opts_dp *dp)
{
	u32 read_val, ret;

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
	if (cdns_phy->ref_clk_rate == CLK_19_2_MHZ)
		/* PMA common configuration 19.2MHz */
		cdns_torrent_dp_pma_cmn_vco_cfg_19_2mhz(cdns_phy, dp->link_rate, dp->ssc);
	else if (cdns_phy->ref_clk_rate == CLK_25_MHZ)
		/* PMA common configuration 25MHz */
		cdns_torrent_dp_pma_cmn_vco_cfg_25mhz(cdns_phy, dp->link_rate, dp->ssc);
	else if (cdns_phy->ref_clk_rate == CLK_100_MHZ)
		/* PMA common configuration 100MHz */
		cdns_torrent_dp_pma_cmn_vco_cfg_100mhz(cdns_phy, dp->link_rate, dp->ssc);

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
static int cdns_torrent_dp_verify_config(struct cdns_torrent_inst *inst,
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
	if (dp->lanes > inst->num_lanes)
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
	struct cdns_torrent_inst *inst = phy_get_drvdata(phy);
	struct cdns_torrent_phy *cdns_phy = dev_get_drvdata(phy->dev.parent);
	int ret;

	ret = cdns_torrent_dp_verify_config(inst, &opts->dp);
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

static int cdns_torrent_phy_on(struct phy *phy)
{
	struct cdns_torrent_inst *inst = phy_get_drvdata(phy);
	struct cdns_torrent_phy *cdns_phy = dev_get_drvdata(phy->dev.parent);
	u32 read_val;
	int ret;

	if (cdns_phy->nsubnodes == 1) {
		/* Take the PHY lane group out of reset */
		reset_control_deassert(inst->lnk_rst);

		/* Take the PHY out of reset */
		ret = reset_control_deassert(cdns_phy->phy_rst);
		if (ret)
			return ret;
	}

	/*
	 * Wait for cmn_ready assertion
	 * PHY_PMA_CMN_CTRL1[0] == 1
	 */
	ret = regmap_field_read_poll_timeout(cdns_phy->phy_pma_cmn_ctrl_1,
					     read_val, read_val, 1000,
					     PLL_LOCK_TIMEOUT);
	if (ret) {
		dev_err(cdns_phy->dev, "Timeout waiting for CMN ready\n");
		return ret;
	}

	if (inst->phy_type == TYPE_PCIE || inst->phy_type == TYPE_USB) {
		ret = regmap_field_read_poll_timeout(cdns_phy->phy_pcs_iso_link_ctrl_1[inst->mlane],
						     read_val, !read_val, 1000,
						     PLL_LOCK_TIMEOUT);
		if (ret == -ETIMEDOUT) {
			dev_err(cdns_phy->dev, "Timeout waiting for PHY status ready\n");
			return ret;
		}
	}

	return 0;
}

static int cdns_torrent_phy_off(struct phy *phy)
{
	struct cdns_torrent_inst *inst = phy_get_drvdata(phy);
	struct cdns_torrent_phy *cdns_phy = dev_get_drvdata(phy->dev.parent);
	int ret;

	if (cdns_phy->nsubnodes != 1)
		return 0;

	ret = reset_control_assert(cdns_phy->phy_rst);
	if (ret)
		return ret;

	return reset_control_assert(inst->lnk_rst);
}

static void cdns_torrent_dp_common_init(struct cdns_torrent_phy *cdns_phy,
					struct cdns_torrent_inst *inst)
{
	struct regmap *regmap = cdns_phy->regmap_dptx_phy_reg;
	unsigned char lane_bits;

	cdns_torrent_dp_write(regmap, PHY_AUX_CTRL, 0x0003); /* enable AUX */

	/*
	 * Set lines power state to A0
	 * Set lines pll clk enable to 0
	 */
	cdns_torrent_dp_set_a0_pll(cdns_phy, inst->num_lanes);

	/*
	 * release phy_l0*_reset_n and pma_tx_elec_idle_ln_* based on
	 * used lanes
	 */
	lane_bits = (1 << inst->num_lanes) - 1;
	cdns_torrent_dp_write(regmap, PHY_RESET,
			      ((0xF & ~lane_bits) << 4) | (0xF & lane_bits));

	/* release pma_xcvr_pllclk_en_ln_*, only for the master lane */
	cdns_torrent_dp_write(regmap, PHY_PMA_XCVR_PLLCLK_EN, 0x0001);

	/*
	 * PHY PMA registers configuration functions
	 * Initialize PHY with max supported link rate, without SSC.
	 */
	if (cdns_phy->ref_clk_rate == CLK_19_2_MHZ)
		cdns_torrent_dp_pma_cmn_vco_cfg_19_2mhz(cdns_phy,
							cdns_phy->max_bit_rate,
							false);
	else if (cdns_phy->ref_clk_rate == CLK_25_MHZ)
		cdns_torrent_dp_pma_cmn_vco_cfg_25mhz(cdns_phy,
						      cdns_phy->max_bit_rate,
						      false);
	else if (cdns_phy->ref_clk_rate == CLK_100_MHZ)
		cdns_torrent_dp_pma_cmn_vco_cfg_100mhz(cdns_phy,
						       cdns_phy->max_bit_rate,
						       false);

	cdns_torrent_dp_pma_cmn_rate(cdns_phy, cdns_phy->max_bit_rate,
				     inst->num_lanes);

	/* take out of reset */
	regmap_field_write(cdns_phy->phy_reset_ctrl, 0x1);
}

static int cdns_torrent_dp_start(struct cdns_torrent_phy *cdns_phy,
				 struct cdns_torrent_inst *inst,
				 struct phy *phy)
{
	int ret;

	cdns_torrent_phy_on(phy);

	ret = cdns_torrent_dp_wait_pma_cmn_ready(cdns_phy);
	if (ret)
		return ret;

	ret = cdns_torrent_dp_run(cdns_phy, inst->num_lanes);

	return ret;
}

static int cdns_torrent_dp_init(struct phy *phy)
{
	struct cdns_torrent_inst *inst = phy_get_drvdata(phy);
	struct cdns_torrent_phy *cdns_phy = dev_get_drvdata(phy->dev.parent);

	switch (cdns_phy->ref_clk_rate) {
	case CLK_19_2_MHZ:
	case CLK_25_MHZ:
	case CLK_100_MHZ:
		/* Valid Ref Clock Rate */
		break;
	default:
		dev_err(cdns_phy->dev, "Unsupported Ref Clock Rate\n");
		return -EINVAL;
	}

	cdns_torrent_dp_common_init(cdns_phy, inst);

	return cdns_torrent_dp_start(cdns_phy, inst, phy);
}

static int cdns_torrent_derived_refclk_enable(struct clk_hw *hw)
{
	struct cdns_torrent_derived_refclk *derived_refclk = to_cdns_torrent_derived_refclk(hw);

	regmap_field_write(derived_refclk->cmn_cdiag_refclk_ovrd_4, 1);
	regmap_field_write(derived_refclk->phy_pipe_cmn_ctrl1_0, 1);

	return 0;
}

static void cdns_torrent_derived_refclk_disable(struct clk_hw *hw)
{
	struct cdns_torrent_derived_refclk *derived_refclk = to_cdns_torrent_derived_refclk(hw);

	regmap_field_write(derived_refclk->phy_pipe_cmn_ctrl1_0, 0);
	regmap_field_write(derived_refclk->cmn_cdiag_refclk_ovrd_4, 0);
}

static int cdns_torrent_derived_refclk_is_enabled(struct clk_hw *hw)
{
	struct cdns_torrent_derived_refclk *derived_refclk = to_cdns_torrent_derived_refclk(hw);
	int val;

	regmap_field_read(derived_refclk->cmn_cdiag_refclk_ovrd_4, &val);

	return !!val;
}

static const struct clk_ops cdns_torrent_derived_refclk_ops = {
	.enable = cdns_torrent_derived_refclk_enable,
	.disable = cdns_torrent_derived_refclk_disable,
	.is_enabled = cdns_torrent_derived_refclk_is_enabled,
};

static int cdns_torrent_derived_refclk_register(struct cdns_torrent_phy *cdns_phy)
{
	struct cdns_torrent_derived_refclk *derived_refclk;
	struct device *dev = cdns_phy->dev;
	struct clk_init_data *init;
	const char *parent_name;
	char clk_name[100];
	struct clk_hw *hw;
	struct clk *clk;
	int ret;

	derived_refclk = devm_kzalloc(dev, sizeof(*derived_refclk), GFP_KERNEL);
	if (!derived_refclk)
		return -ENOMEM;

	snprintf(clk_name, sizeof(clk_name), "%s_%s", dev_name(dev),
		 clk_names[CDNS_TORRENT_DERIVED_REFCLK]);

	clk = devm_clk_get_optional(dev, "phy_en_refclk");
	if (IS_ERR(clk)) {
		dev_err(dev, "No parent clock for derived_refclk\n");
		return PTR_ERR(clk);
	}

	init = &derived_refclk->clk_data;

	if (clk) {
		parent_name = __clk_get_name(clk);
		init->parent_names = &parent_name;
		init->num_parents = 1;
	}
	init->ops = &cdns_torrent_derived_refclk_ops;
	init->flags = 0;
	init->name = clk_name;

	derived_refclk->phy_pipe_cmn_ctrl1_0 = cdns_phy->phy_pipe_cmn_ctrl1_0;
	derived_refclk->cmn_cdiag_refclk_ovrd_4 = cdns_phy->cmn_cdiag_refclk_ovrd_4;

	derived_refclk->hw.init = init;

	hw = &derived_refclk->hw;
	ret = devm_clk_hw_register(dev, hw);
	if (ret)
		return ret;

	cdns_phy->clk_hw_data->hws[CDNS_TORRENT_DERIVED_REFCLK] = hw;

	return 0;
}

static int cdns_torrent_received_refclk_enable(struct clk_hw *hw)
{
	struct cdns_torrent_received_refclk *received_refclk = to_cdns_torrent_received_refclk(hw);

	regmap_field_write(received_refclk->phy_pipe_cmn_ctrl1_0, 1);

	return 0;
}

static void cdns_torrent_received_refclk_disable(struct clk_hw *hw)
{
	struct cdns_torrent_received_refclk *received_refclk = to_cdns_torrent_received_refclk(hw);

	regmap_field_write(received_refclk->phy_pipe_cmn_ctrl1_0, 0);
}

static int cdns_torrent_received_refclk_is_enabled(struct clk_hw *hw)
{
	struct cdns_torrent_received_refclk *received_refclk = to_cdns_torrent_received_refclk(hw);
	int val, cmn_val;

	regmap_field_read(received_refclk->phy_pipe_cmn_ctrl1_0, &val);
	regmap_field_read(received_refclk->cmn_cdiag_refclk_ovrd_4, &cmn_val);

	return val && !cmn_val;
}

static const struct clk_ops cdns_torrent_received_refclk_ops = {
	.enable = cdns_torrent_received_refclk_enable,
	.disable = cdns_torrent_received_refclk_disable,
	.is_enabled = cdns_torrent_received_refclk_is_enabled,
};

static int cdns_torrent_received_refclk_register(struct cdns_torrent_phy *cdns_phy)
{
	struct cdns_torrent_received_refclk *received_refclk;
	struct device *dev = cdns_phy->dev;
	struct clk_init_data *init;
	const char *parent_name;
	char clk_name[100];
	struct clk_hw *hw;
	struct clk *clk;
	int ret;

	received_refclk = devm_kzalloc(dev, sizeof(*received_refclk), GFP_KERNEL);
	if (!received_refclk)
		return -ENOMEM;

	snprintf(clk_name, sizeof(clk_name), "%s_%s", dev_name(dev),
		 clk_names[CDNS_TORRENT_RECEIVED_REFCLK]);

	clk = devm_clk_get_optional(dev, "phy_en_refclk");
	if (IS_ERR(clk)) {
		dev_err(dev, "No parent clock for received_refclk\n");
		return PTR_ERR(clk);
	}

	init = &received_refclk->clk_data;

	if (clk) {
		parent_name = __clk_get_name(clk);
		init->parent_names = &parent_name;
		init->num_parents = 1;
	}
	init->ops = &cdns_torrent_received_refclk_ops;
	init->flags = 0;
	init->name = clk_name;

	received_refclk->phy_pipe_cmn_ctrl1_0 = cdns_phy->phy_pipe_cmn_ctrl1_0;
	received_refclk->cmn_cdiag_refclk_ovrd_4 = cdns_phy->cmn_cdiag_refclk_ovrd_4;

	received_refclk->hw.init = init;

	hw = &received_refclk->hw;
	ret = devm_clk_hw_register(dev, hw);
	if (ret)
		return ret;

	cdns_phy->clk_hw_data->hws[CDNS_TORRENT_RECEIVED_REFCLK] = hw;

	return 0;
}

static int cdns_torrent_refclk_driver_enable(struct clk_hw *hw)
{
	struct cdns_torrent_refclk_driver *refclk_driver = to_cdns_torrent_refclk_driver(hw);

	regmap_field_write(refclk_driver->cmn_fields[CMN_CDIAG_REFCLK_DRV0_CTRL_6], 0);
	regmap_field_write(refclk_driver->cmn_fields[CMN_CDIAG_REFCLK_DRV0_CTRL_5], 1);
	regmap_field_write(refclk_driver->cmn_fields[CMN_CDIAG_REFCLK_DRV0_CTRL_1], 0);

	return 0;
}

static void cdns_torrent_refclk_driver_disable(struct clk_hw *hw)
{
	struct cdns_torrent_refclk_driver *refclk_driver = to_cdns_torrent_refclk_driver(hw);

	regmap_field_write(refclk_driver->cmn_fields[CMN_CDIAG_REFCLK_DRV0_CTRL_1], 1);
}

static int cdns_torrent_refclk_driver_is_enabled(struct clk_hw *hw)
{
	struct cdns_torrent_refclk_driver *refclk_driver = to_cdns_torrent_refclk_driver(hw);
	int val;

	regmap_field_read(refclk_driver->cmn_fields[CMN_CDIAG_REFCLK_DRV0_CTRL_1], &val);

	return !val;
}

static u8 cdns_torrent_refclk_driver_get_parent(struct clk_hw *hw)
{
	struct cdns_torrent_refclk_driver *refclk_driver = to_cdns_torrent_refclk_driver(hw);
	unsigned int val;

	regmap_field_read(refclk_driver->cmn_fields[CMN_CDIAG_REFCLK_DRV0_CTRL_4], &val);
	return clk_mux_val_to_index(hw, cdns_torrent_refclk_driver_mux_table, 0, val);
}

static int cdns_torrent_refclk_driver_set_parent(struct clk_hw *hw, u8 index)
{
	struct cdns_torrent_refclk_driver *refclk_driver = to_cdns_torrent_refclk_driver(hw);
	unsigned int val;

	val = cdns_torrent_refclk_driver_mux_table[index];
	return regmap_field_write(refclk_driver->cmn_fields[CMN_CDIAG_REFCLK_DRV0_CTRL_4], val);
}

static const struct clk_ops cdns_torrent_refclk_driver_ops = {
	.enable = cdns_torrent_refclk_driver_enable,
	.disable = cdns_torrent_refclk_driver_disable,
	.is_enabled = cdns_torrent_refclk_driver_is_enabled,
	.set_parent = cdns_torrent_refclk_driver_set_parent,
	.get_parent = cdns_torrent_refclk_driver_get_parent,
};

static int cdns_torrent_refclk_driver_register(struct cdns_torrent_phy *cdns_phy)
{
	struct cdns_torrent_refclk_driver *refclk_driver;
	struct device *dev = cdns_phy->dev;
	struct regmap_field *field;
	struct clk_init_data *init;
	const char **parent_names;
	unsigned int num_parents;
	struct regmap *regmap;
	char clk_name[100];
	struct clk_hw *hw;
	int i, ret;

	refclk_driver = devm_kzalloc(dev, sizeof(*refclk_driver), GFP_KERNEL);
	if (!refclk_driver)
		return -ENOMEM;

	num_parents = ARRAY_SIZE(refclk_driver_parent_index);
	parent_names = devm_kzalloc(dev, (sizeof(char *) * num_parents), GFP_KERNEL);
	if (!parent_names)
		return -ENOMEM;

	for (i = 0; i < num_parents; i++) {
		hw = cdns_phy->clk_hw_data->hws[refclk_driver_parent_index[i]];
		if (IS_ERR_OR_NULL(hw)) {
			dev_err(dev, "No parent clock for refclk driver clock\n");
			return IS_ERR(hw) ? PTR_ERR(hw) : -ENOENT;
		}
		parent_names[i] = clk_hw_get_name(hw);
	}

	snprintf(clk_name, sizeof(clk_name), "%s_%s", dev_name(dev),
		 clk_names[CDNS_TORRENT_REFCLK_DRIVER]);

	init = &refclk_driver->clk_data;

	init->ops = &cdns_torrent_refclk_driver_ops;
	init->flags = CLK_SET_RATE_NO_REPARENT;
	init->parent_names = parent_names;
	init->num_parents = num_parents;
	init->name = clk_name;

	regmap = cdns_phy->regmap_common_cdb;

	for (i = 0; i < REFCLK_OUT_NUM_CMN_CONFIG; i++) {
		field = devm_regmap_field_alloc(dev, regmap, refclk_out_cmn_cfg[i]);
		if (IS_ERR(field)) {
			dev_err(dev, "Refclk driver CMN reg field init failed\n");
			return PTR_ERR(field);
		}
		refclk_driver->cmn_fields[i] = field;
	}

	/* Enable Derived reference clock as default */
	regmap_field_write(refclk_driver->cmn_fields[CMN_CDIAG_REFCLK_DRV0_CTRL_4], 1);

	refclk_driver->hw.init = init;

	hw = &refclk_driver->hw;
	ret = devm_clk_hw_register(dev, hw);
	if (ret)
		return ret;

	cdns_phy->clk_hw_data->hws[CDNS_TORRENT_REFCLK_DRIVER] = hw;

	return 0;
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

static int cdns_torrent_dp_regfield_init(struct cdns_torrent_phy *cdns_phy)
{
	struct device *dev = cdns_phy->dev;
	struct regmap_field *field;
	struct regmap *regmap;

	regmap = cdns_phy->regmap_dptx_phy_reg;
	field = devm_regmap_field_alloc(dev, regmap, phy_reset_ctrl);
	if (IS_ERR(field)) {
		dev_err(dev, "PHY_RESET reg field init failed\n");
		return PTR_ERR(field);
	}
	cdns_phy->phy_reset_ctrl = field;

	return 0;
}

static int cdns_torrent_regfield_init(struct cdns_torrent_phy *cdns_phy)
{
	struct device *dev = cdns_phy->dev;
	struct regmap_field *field;
	struct regmap *regmap;
	int i;

	regmap = cdns_phy->regmap_phy_pcs_common_cdb;
	field = devm_regmap_field_alloc(dev, regmap, phy_pll_cfg);
	if (IS_ERR(field)) {
		dev_err(dev, "PHY_PLL_CFG reg field init failed\n");
		return PTR_ERR(field);
	}
	cdns_phy->phy_pll_cfg = field;

	regmap = cdns_phy->regmap_phy_pcs_common_cdb;
	field = devm_regmap_field_alloc(dev, regmap, phy_pipe_cmn_ctrl1_0);
	if (IS_ERR(field)) {
		dev_err(dev, "phy_pipe_cmn_ctrl1_0 reg field init failed\n");
		return PTR_ERR(field);
	}
	cdns_phy->phy_pipe_cmn_ctrl1_0 = field;

	regmap = cdns_phy->regmap_common_cdb;
	field = devm_regmap_field_alloc(dev, regmap, cmn_cdiag_refclk_ovrd_4);
	if (IS_ERR(field)) {
		dev_err(dev, "cmn_cdiag_refclk_ovrd_4 reg field init failed\n");
		return PTR_ERR(field);
	}
	cdns_phy->cmn_cdiag_refclk_ovrd_4 = field;

	regmap = cdns_phy->regmap_phy_pma_common_cdb;
	field = devm_regmap_field_alloc(dev, regmap, phy_pma_cmn_ctrl_1);
	if (IS_ERR(field)) {
		dev_err(dev, "PHY_PMA_CMN_CTRL1 reg field init failed\n");
		return PTR_ERR(field);
	}
	cdns_phy->phy_pma_cmn_ctrl_1 = field;

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

	for (i = 0; i < MAX_NUM_LANES; i++) {
		regmap = cdns_phy->regmap_phy_pcs_lane_cdb[i];
		field = devm_regmap_field_alloc(dev, regmap, phy_pcs_iso_link_ctrl_1);
		if (IS_ERR(field)) {
			dev_err(dev, "PHY_PCS_ISO_LINK_CTRL reg field init for ln %d failed\n", i);
			return PTR_ERR(field);
		}
		cdns_phy->phy_pcs_iso_link_ctrl_1[i] = field;
	}

	return 0;
}

static int cdns_torrent_dp_regmap_init(struct cdns_torrent_phy *cdns_phy)
{
	void __iomem *base = cdns_phy->base;
	struct device *dev = cdns_phy->dev;
	struct regmap *regmap;
	u8 reg_offset_shift;
	u32 block_offset;

	reg_offset_shift = cdns_phy->init_data->reg_offset_shift;

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

static int cdns_torrent_regmap_init(struct cdns_torrent_phy *cdns_phy)
{
	void __iomem *sd_base = cdns_phy->sd_base;
	u8 block_offset_shift, reg_offset_shift;
	struct device *dev = cdns_phy->dev;
	struct regmap *regmap;
	u32 block_offset;
	int i;

	block_offset_shift = cdns_phy->init_data->block_offset_shift;
	reg_offset_shift = cdns_phy->init_data->reg_offset_shift;

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

		block_offset = TORRENT_PHY_PCS_LANE_CDB_OFFSET(i, block_offset_shift,
							       reg_offset_shift);
		regmap = cdns_regmap_init(dev, sd_base, block_offset,
					  reg_offset_shift,
					  &cdns_torrent_phy_pcs_lane_cdb_config[i]);
		if (IS_ERR(regmap)) {
			dev_err(dev, "Failed to init PHY PCS lane CDB regmap\n");
			return PTR_ERR(regmap);
		}
		cdns_phy->regmap_phy_pcs_lane_cdb[i] = regmap;
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

	return 0;
}

static int cdns_torrent_phy_init(struct phy *phy)
{
	struct cdns_torrent_phy *cdns_phy = dev_get_drvdata(phy->dev.parent);
	const struct cdns_torrent_data *init_data = cdns_phy->init_data;
	struct cdns_torrent_vals *cmn_vals, *tx_ln_vals, *rx_ln_vals;
	enum cdns_torrent_ref_clk ref_clk = cdns_phy->ref_clk_rate;
	struct cdns_torrent_vals *link_cmn_vals, *xcvr_diag_vals;
	struct cdns_torrent_inst *inst = phy_get_drvdata(phy);
	enum cdns_torrent_phy_type phy_type = inst->phy_type;
	enum cdns_torrent_ssc_mode ssc = inst->ssc_mode;
	struct cdns_torrent_vals *pcs_cmn_vals;
	struct cdns_reg_pairs *reg_pairs;
	struct regmap *regmap;
	u32 num_regs;
	int i, j;

	if (cdns_phy->nsubnodes > 1)
		return 0;

	/**
	 * Spread spectrum generation is not required or supported
	 * for SGMII/QSGMII
	 */
	if (phy_type == TYPE_SGMII || phy_type == TYPE_QSGMII)
		ssc = NO_SSC;

	/* PHY configuration specific registers for single link */
	link_cmn_vals = init_data->link_cmn_vals[phy_type][TYPE_NONE][ssc];
	if (link_cmn_vals) {
		reg_pairs = link_cmn_vals->reg_pairs;
		num_regs = link_cmn_vals->num_regs;
		regmap = cdns_phy->regmap_common_cdb;

		/**
		 * First array value in link_cmn_vals must be of
		 * PHY_PLL_CFG register
		 */
		regmap_field_write(cdns_phy->phy_pll_cfg, reg_pairs[0].val);

		for (i = 1; i < num_regs; i++)
			regmap_write(regmap, reg_pairs[i].off,
				     reg_pairs[i].val);
	}

	xcvr_diag_vals = init_data->xcvr_diag_vals[phy_type][TYPE_NONE][ssc];
	if (xcvr_diag_vals) {
		reg_pairs = xcvr_diag_vals->reg_pairs;
		num_regs = xcvr_diag_vals->num_regs;
		for (i = 0; i < inst->num_lanes; i++) {
			regmap = cdns_phy->regmap_tx_lane_cdb[i + inst->mlane];
			for (j = 0; j < num_regs; j++)
				regmap_write(regmap, reg_pairs[j].off,
					     reg_pairs[j].val);
		}
	}

	/* PHY PCS common registers configurations */
	pcs_cmn_vals = init_data->pcs_cmn_vals[phy_type][TYPE_NONE][ssc];
	if (pcs_cmn_vals) {
		reg_pairs = pcs_cmn_vals->reg_pairs;
		num_regs = pcs_cmn_vals->num_regs;
		regmap = cdns_phy->regmap_phy_pcs_common_cdb;
		for (i = 0; i < num_regs; i++)
			regmap_write(regmap, reg_pairs[i].off,
				     reg_pairs[i].val);
	}

	/* PMA common registers configurations */
	cmn_vals = init_data->cmn_vals[ref_clk][phy_type][TYPE_NONE][ssc];
	if (cmn_vals) {
		reg_pairs = cmn_vals->reg_pairs;
		num_regs = cmn_vals->num_regs;
		regmap = cdns_phy->regmap_common_cdb;
		for (i = 0; i < num_regs; i++)
			regmap_write(regmap, reg_pairs[i].off,
				     reg_pairs[i].val);
	}

	/* PMA TX lane registers configurations */
	tx_ln_vals = init_data->tx_ln_vals[ref_clk][phy_type][TYPE_NONE][ssc];
	if (tx_ln_vals) {
		reg_pairs = tx_ln_vals->reg_pairs;
		num_regs = tx_ln_vals->num_regs;
		for (i = 0; i < inst->num_lanes; i++) {
			regmap = cdns_phy->regmap_tx_lane_cdb[i + inst->mlane];
			for (j = 0; j < num_regs; j++)
				regmap_write(regmap, reg_pairs[j].off,
					     reg_pairs[j].val);
		}
	}

	/* PMA RX lane registers configurations */
	rx_ln_vals = init_data->rx_ln_vals[ref_clk][phy_type][TYPE_NONE][ssc];
	if (rx_ln_vals) {
		reg_pairs = rx_ln_vals->reg_pairs;
		num_regs = rx_ln_vals->num_regs;
		for (i = 0; i < inst->num_lanes; i++) {
			regmap = cdns_phy->regmap_rx_lane_cdb[i + inst->mlane];
			for (j = 0; j < num_regs; j++)
				regmap_write(regmap, reg_pairs[j].off,
					     reg_pairs[j].val);
		}
	}

	if (phy_type == TYPE_DP)
		return cdns_torrent_dp_init(phy);

	return 0;
}

static const struct phy_ops cdns_torrent_phy_ops = {
	.init		= cdns_torrent_phy_init,
	.configure	= cdns_torrent_dp_configure,
	.power_on	= cdns_torrent_phy_on,
	.power_off	= cdns_torrent_phy_off,
	.owner		= THIS_MODULE,
};

static int cdns_torrent_noop_phy_on(struct phy *phy)
{
	/* Give 5ms to 10ms delay for the PIPE clock to be stable */
	usleep_range(5000, 10000);

	return 0;
}

static const struct phy_ops noop_ops = {
	.power_on	= cdns_torrent_noop_phy_on,
	.owner		= THIS_MODULE,
};

static
int cdns_torrent_phy_configure_multilink(struct cdns_torrent_phy *cdns_phy)
{
	const struct cdns_torrent_data *init_data = cdns_phy->init_data;
	struct cdns_torrent_vals *cmn_vals, *tx_ln_vals, *rx_ln_vals;
	enum cdns_torrent_ref_clk ref_clk = cdns_phy->ref_clk_rate;
	struct cdns_torrent_vals *link_cmn_vals, *xcvr_diag_vals;
	enum cdns_torrent_phy_type phy_t1, phy_t2;
	struct cdns_torrent_vals *pcs_cmn_vals;
	int i, j, node, mlane, num_lanes, ret;
	struct cdns_reg_pairs *reg_pairs;
	enum cdns_torrent_ssc_mode ssc;
	struct regmap *regmap;
	u32 num_regs;

	/* Maximum 2 links (subnodes) are supported */
	if (cdns_phy->nsubnodes != 2)
		return -EINVAL;

	phy_t1 = cdns_phy->phys[0].phy_type;
	phy_t2 = cdns_phy->phys[1].phy_type;

	/**
	 * First configure the PHY for first link with phy_t1. Get the array
	 * values as [phy_t1][phy_t2][ssc].
	 */
	for (node = 0; node < cdns_phy->nsubnodes; node++) {
		if (node == 1) {
			/**
			 * If first link with phy_t1 is configured, then
			 * configure the PHY for second link with phy_t2.
			 * Get the array values as [phy_t2][phy_t1][ssc].
			 */
			swap(phy_t1, phy_t2);
		}

		mlane = cdns_phy->phys[node].mlane;
		ssc = cdns_phy->phys[node].ssc_mode;
		num_lanes = cdns_phy->phys[node].num_lanes;

		/**
		 * PHY configuration specific registers:
		 * link_cmn_vals depend on combination of PHY types being
		 * configured and are common for both PHY types, so array
		 * values should be same for [phy_t1][phy_t2][ssc] and
		 * [phy_t2][phy_t1][ssc].
		 * xcvr_diag_vals also depend on combination of PHY types
		 * being configured, but these can be different for particular
		 * PHY type and are per lane.
		 */
		link_cmn_vals = init_data->link_cmn_vals[phy_t1][phy_t2][ssc];
		if (link_cmn_vals) {
			reg_pairs = link_cmn_vals->reg_pairs;
			num_regs = link_cmn_vals->num_regs;
			regmap = cdns_phy->regmap_common_cdb;

			/**
			 * First array value in link_cmn_vals must be of
			 * PHY_PLL_CFG register
			 */
			regmap_field_write(cdns_phy->phy_pll_cfg,
					   reg_pairs[0].val);

			for (i = 1; i < num_regs; i++)
				regmap_write(regmap, reg_pairs[i].off,
					     reg_pairs[i].val);
		}

		xcvr_diag_vals = init_data->xcvr_diag_vals[phy_t1][phy_t2][ssc];
		if (xcvr_diag_vals) {
			reg_pairs = xcvr_diag_vals->reg_pairs;
			num_regs = xcvr_diag_vals->num_regs;
			for (i = 0; i < num_lanes; i++) {
				regmap = cdns_phy->regmap_tx_lane_cdb[i + mlane];
				for (j = 0; j < num_regs; j++)
					regmap_write(regmap, reg_pairs[j].off,
						     reg_pairs[j].val);
			}
		}

		/* PHY PCS common registers configurations */
		pcs_cmn_vals = init_data->pcs_cmn_vals[phy_t1][phy_t2][ssc];
		if (pcs_cmn_vals) {
			reg_pairs = pcs_cmn_vals->reg_pairs;
			num_regs = pcs_cmn_vals->num_regs;
			regmap = cdns_phy->regmap_phy_pcs_common_cdb;
			for (i = 0; i < num_regs; i++)
				regmap_write(regmap, reg_pairs[i].off,
					     reg_pairs[i].val);
		}

		/* PMA common registers configurations */
		cmn_vals = init_data->cmn_vals[ref_clk][phy_t1][phy_t2][ssc];
		if (cmn_vals) {
			reg_pairs = cmn_vals->reg_pairs;
			num_regs = cmn_vals->num_regs;
			regmap = cdns_phy->regmap_common_cdb;
			for (i = 0; i < num_regs; i++)
				regmap_write(regmap, reg_pairs[i].off,
					     reg_pairs[i].val);
		}

		/* PMA TX lane registers configurations */
		tx_ln_vals = init_data->tx_ln_vals[ref_clk][phy_t1][phy_t2][ssc];
		if (tx_ln_vals) {
			reg_pairs = tx_ln_vals->reg_pairs;
			num_regs = tx_ln_vals->num_regs;
			for (i = 0; i < num_lanes; i++) {
				regmap = cdns_phy->regmap_tx_lane_cdb[i + mlane];
				for (j = 0; j < num_regs; j++)
					regmap_write(regmap, reg_pairs[j].off,
						     reg_pairs[j].val);
			}
		}

		/* PMA RX lane registers configurations */
		rx_ln_vals = init_data->rx_ln_vals[ref_clk][phy_t1][phy_t2][ssc];
		if (rx_ln_vals) {
			reg_pairs = rx_ln_vals->reg_pairs;
			num_regs = rx_ln_vals->num_regs;
			for (i = 0; i < num_lanes; i++) {
				regmap = cdns_phy->regmap_rx_lane_cdb[i + mlane];
				for (j = 0; j < num_regs; j++)
					regmap_write(regmap, reg_pairs[j].off,
						     reg_pairs[j].val);
			}
		}

		reset_control_deassert(cdns_phy->phys[node].lnk_rst);
	}

	/* Take the PHY out of reset */
	ret = reset_control_deassert(cdns_phy->phy_rst);
	if (ret)
		return ret;

	return 0;
}

static void cdns_torrent_clk_cleanup(struct cdns_torrent_phy *cdns_phy)
{
	struct device *dev = cdns_phy->dev;

	of_clk_del_provider(dev->of_node);
}

static int cdns_torrent_clk_register(struct cdns_torrent_phy *cdns_phy)
{
	struct device *dev = cdns_phy->dev;
	struct device_node *node = dev->of_node;
	struct clk_hw_onecell_data *data;
	int ret;

	data = devm_kzalloc(dev, struct_size(data, hws, CDNS_TORRENT_OUTPUT_CLOCKS), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->num = CDNS_TORRENT_OUTPUT_CLOCKS;
	cdns_phy->clk_hw_data = data;

	ret = cdns_torrent_derived_refclk_register(cdns_phy);
	if (ret) {
		dev_err(dev, "failed to register derived refclk\n");
		return ret;
	}

	ret = cdns_torrent_received_refclk_register(cdns_phy);
	if (ret) {
		dev_err(dev, "failed to register received refclk\n");
		return ret;
	}

	ret = cdns_torrent_refclk_driver_register(cdns_phy);
	if (ret) {
		dev_err(dev, "failed to register refclk driver\n");
		return ret;
	}

	ret = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, data);
	if (ret) {
		dev_err(dev, "Failed to add clock provider: %s\n", node->name);
		return ret;
	}

	return 0;
}

static int cdns_torrent_reset(struct cdns_torrent_phy *cdns_phy)
{
	struct device *dev = cdns_phy->dev;

	cdns_phy->phy_rst = devm_reset_control_get_exclusive_by_index(dev, 0);
	if (IS_ERR(cdns_phy->phy_rst)) {
		dev_err(dev, "%s: failed to get reset\n",
			dev->of_node->full_name);
		return PTR_ERR(cdns_phy->phy_rst);
	}

	cdns_phy->apb_rst = devm_reset_control_get_optional_exclusive(dev, "torrent_apb");
	if (IS_ERR(cdns_phy->apb_rst)) {
		dev_err(dev, "%s: failed to get apb reset\n",
			dev->of_node->full_name);
		return PTR_ERR(cdns_phy->apb_rst);
	}

	return 0;
}

static int cdns_torrent_clk(struct cdns_torrent_phy *cdns_phy)
{
	struct device *dev = cdns_phy->dev;
	unsigned long ref_clk_rate;
	int ret;

	cdns_phy->clk = devm_clk_get(dev, "refclk");
	if (IS_ERR(cdns_phy->clk)) {
		dev_err(dev, "phy ref clock not found\n");
		return PTR_ERR(cdns_phy->clk);
	}

	ret = clk_prepare_enable(cdns_phy->clk);
	if (ret) {
		dev_err(cdns_phy->dev, "Failed to prepare ref clock\n");
		return ret;
	}

	ref_clk_rate = clk_get_rate(cdns_phy->clk);
	if (!ref_clk_rate) {
		dev_err(cdns_phy->dev, "Failed to get ref clock rate\n");
		clk_disable_unprepare(cdns_phy->clk);
		return -EINVAL;
	}

	switch (ref_clk_rate) {
	case REF_CLK_19_2MHZ:
		cdns_phy->ref_clk_rate = CLK_19_2_MHZ;
		break;
	case REF_CLK_25MHZ:
		cdns_phy->ref_clk_rate = CLK_25_MHZ;
		break;
	case REF_CLK_100MHZ:
		cdns_phy->ref_clk_rate = CLK_100_MHZ;
		break;
	default:
		dev_err(cdns_phy->dev, "Invalid Ref Clock Rate\n");
		clk_disable_unprepare(cdns_phy->clk);
		return -EINVAL;
	}

	return 0;
}

static int cdns_torrent_phy_probe(struct platform_device *pdev)
{
	struct cdns_torrent_phy *cdns_phy;
	struct device *dev = &pdev->dev;
	struct phy_provider *phy_provider;
	const struct cdns_torrent_data *data;
	struct device_node *child;
	int ret, subnodes, node = 0, i;
	u32 total_num_lanes = 0;
	int already_configured;
	u8 init_dp_regmap = 0;
	u32 phy_type;

	/* Get init data for this PHY */
	data = of_device_get_match_data(dev);
	if (!data)
		return -EINVAL;

	cdns_phy = devm_kzalloc(dev, sizeof(*cdns_phy), GFP_KERNEL);
	if (!cdns_phy)
		return -ENOMEM;

	dev_set_drvdata(dev, cdns_phy);
	cdns_phy->dev = dev;
	cdns_phy->init_data = data;

	cdns_phy->sd_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(cdns_phy->sd_base))
		return PTR_ERR(cdns_phy->sd_base);

	subnodes = of_get_available_child_count(dev->of_node);
	if (subnodes == 0) {
		dev_err(dev, "No available link subnodes found\n");
		return -EINVAL;
	}

	ret = cdns_torrent_regmap_init(cdns_phy);
	if (ret)
		return ret;

	ret = cdns_torrent_regfield_init(cdns_phy);
	if (ret)
		return ret;

	ret = cdns_torrent_clk_register(cdns_phy);
	if (ret)
		return ret;

	regmap_field_read(cdns_phy->phy_pma_cmn_ctrl_1, &already_configured);

	if (!already_configured) {
		ret = cdns_torrent_reset(cdns_phy);
		if (ret)
			goto clk_cleanup;

		ret = cdns_torrent_clk(cdns_phy);
		if (ret)
			goto clk_cleanup;

		/* Enable APB */
		reset_control_deassert(cdns_phy->apb_rst);
	}

	for_each_available_child_of_node(dev->of_node, child) {
		struct phy *gphy;

		/* PHY subnode name must be 'phy'. */
		if (!(of_node_name_eq(child, "phy")))
			continue;

		cdns_phy->phys[node].lnk_rst =
				of_reset_control_array_get_exclusive(child);
		if (IS_ERR(cdns_phy->phys[node].lnk_rst)) {
			dev_err(dev, "%s: failed to get reset\n",
				child->full_name);
			ret = PTR_ERR(cdns_phy->phys[node].lnk_rst);
			goto put_lnk_rst;
		}

		if (of_property_read_u32(child, "reg",
					 &cdns_phy->phys[node].mlane)) {
			dev_err(dev, "%s: No \"reg\"-property.\n",
				child->full_name);
			ret = -EINVAL;
			goto put_child;
		}

		if (of_property_read_u32(child, "cdns,phy-type", &phy_type)) {
			dev_err(dev, "%s: No \"cdns,phy-type\"-property.\n",
				child->full_name);
			ret = -EINVAL;
			goto put_child;
		}

		switch (phy_type) {
		case PHY_TYPE_PCIE:
			cdns_phy->phys[node].phy_type = TYPE_PCIE;
			break;
		case PHY_TYPE_DP:
			cdns_phy->phys[node].phy_type = TYPE_DP;
			break;
		case PHY_TYPE_SGMII:
			cdns_phy->phys[node].phy_type = TYPE_SGMII;
			break;
		case PHY_TYPE_QSGMII:
			cdns_phy->phys[node].phy_type = TYPE_QSGMII;
			break;
		case PHY_TYPE_USB3:
			cdns_phy->phys[node].phy_type = TYPE_USB;
			break;
		default:
			dev_err(dev, "Unsupported protocol\n");
			ret = -EINVAL;
			goto put_child;
		}

		if (of_property_read_u32(child, "cdns,num-lanes",
					 &cdns_phy->phys[node].num_lanes)) {
			dev_err(dev, "%s: No \"cdns,num-lanes\"-property.\n",
				child->full_name);
			ret = -EINVAL;
			goto put_child;
		}

		total_num_lanes += cdns_phy->phys[node].num_lanes;

		/* Get SSC mode */
		cdns_phy->phys[node].ssc_mode = NO_SSC;
		of_property_read_u32(child, "cdns,ssc-mode",
				     &cdns_phy->phys[node].ssc_mode);

		if (!already_configured)
			gphy = devm_phy_create(dev, child, &cdns_torrent_phy_ops);
		else
			gphy = devm_phy_create(dev, child, &noop_ops);
		if (IS_ERR(gphy)) {
			ret = PTR_ERR(gphy);
			goto put_child;
		}

		if (cdns_phy->phys[node].phy_type == TYPE_DP) {
			switch (cdns_phy->phys[node].num_lanes) {
			case 1:
			case 2:
			case 4:
			/* valid number of lanes */
				break;
			default:
				dev_err(dev, "unsupported number of lanes: %d\n",
					cdns_phy->phys[node].num_lanes);
				ret = -EINVAL;
				goto put_child;
			}

			cdns_phy->max_bit_rate = DEFAULT_MAX_BIT_RATE;
			of_property_read_u32(child, "cdns,max-bit-rate",
					     &cdns_phy->max_bit_rate);

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
				ret = -EINVAL;
				goto put_child;
			}

			/* DPTX registers */
			cdns_phy->base = devm_platform_ioremap_resource(pdev, 1);
			if (IS_ERR(cdns_phy->base)) {
				ret = PTR_ERR(cdns_phy->base);
				goto put_child;
			}

			if (!init_dp_regmap) {
				ret = cdns_torrent_dp_regmap_init(cdns_phy);
				if (ret)
					goto put_child;

				ret = cdns_torrent_dp_regfield_init(cdns_phy);
				if (ret)
					goto put_child;

				init_dp_regmap++;
			}

			dev_dbg(dev, "DP max bit rate %d.%03d Gbps\n",
				cdns_phy->max_bit_rate / 1000,
				cdns_phy->max_bit_rate % 1000);

			gphy->attrs.bus_width = cdns_phy->phys[node].num_lanes;
			gphy->attrs.max_link_rate = cdns_phy->max_bit_rate;
			gphy->attrs.mode = PHY_MODE_DP;
		}

		cdns_phy->phys[node].phy = gphy;
		phy_set_drvdata(gphy, &cdns_phy->phys[node]);

		node++;
	}
	cdns_phy->nsubnodes = node;

	if (total_num_lanes > MAX_NUM_LANES) {
		dev_err(dev, "Invalid lane configuration\n");
		ret = -EINVAL;
		goto put_lnk_rst;
	}

	if (cdns_phy->nsubnodes > 1 && !already_configured) {
		ret = cdns_torrent_phy_configure_multilink(cdns_phy);
		if (ret)
			goto put_lnk_rst;
	}

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(phy_provider)) {
		ret = PTR_ERR(phy_provider);
		goto put_lnk_rst;
	}

	if (cdns_phy->nsubnodes > 1)
		dev_dbg(dev, "Multi-link: %s (%d lanes) & %s (%d lanes)",
			cdns_torrent_get_phy_type(cdns_phy->phys[0].phy_type),
			cdns_phy->phys[0].num_lanes,
			cdns_torrent_get_phy_type(cdns_phy->phys[1].phy_type),
			cdns_phy->phys[1].num_lanes);
	else
		dev_dbg(dev, "Single link: %s (%d lanes)",
			cdns_torrent_get_phy_type(cdns_phy->phys[0].phy_type),
			cdns_phy->phys[0].num_lanes);

	return 0;

put_child:
	node++;
put_lnk_rst:
	for (i = 0; i < node; i++)
		reset_control_put(cdns_phy->phys[i].lnk_rst);
	of_node_put(child);
	reset_control_assert(cdns_phy->apb_rst);
	clk_disable_unprepare(cdns_phy->clk);
clk_cleanup:
	cdns_torrent_clk_cleanup(cdns_phy);
	return ret;
}

static int cdns_torrent_phy_remove(struct platform_device *pdev)
{
	struct cdns_torrent_phy *cdns_phy = platform_get_drvdata(pdev);
	int i;

	reset_control_assert(cdns_phy->phy_rst);
	reset_control_assert(cdns_phy->apb_rst);
	for (i = 0; i < cdns_phy->nsubnodes; i++) {
		reset_control_assert(cdns_phy->phys[i].lnk_rst);
		reset_control_put(cdns_phy->phys[i].lnk_rst);
	}

	clk_disable_unprepare(cdns_phy->clk);
	cdns_torrent_clk_cleanup(cdns_phy);

	return 0;
}

/* Single DisplayPort(DP) link configuration */
static struct cdns_reg_pairs sl_dp_link_cmn_regs[] = {
	{0x0000, PHY_PLL_CFG},
};

static struct cdns_reg_pairs sl_dp_xcvr_diag_ln_regs[] = {
	{0x0000, XCVR_DIAG_HSCLK_SEL},
	{0x0001, XCVR_DIAG_PLLDRC_CTRL}
};

static struct cdns_torrent_vals sl_dp_link_cmn_vals = {
	.reg_pairs = sl_dp_link_cmn_regs,
	.num_regs = ARRAY_SIZE(sl_dp_link_cmn_regs),
};

static struct cdns_torrent_vals sl_dp_xcvr_diag_ln_vals = {
	.reg_pairs = sl_dp_xcvr_diag_ln_regs,
	.num_regs = ARRAY_SIZE(sl_dp_xcvr_diag_ln_regs),
};

/* Single DP, 19.2 MHz Ref clk, no SSC */
static struct cdns_reg_pairs sl_dp_19_2_no_ssc_cmn_regs[] = {
	{0x0014, CMN_SSM_BIAS_TMR},
	{0x0027, CMN_PLLSM0_PLLPRE_TMR},
	{0x00A1, CMN_PLLSM0_PLLLOCK_TMR},
	{0x0027, CMN_PLLSM1_PLLPRE_TMR},
	{0x00A1, CMN_PLLSM1_PLLLOCK_TMR},
	{0x0060, CMN_BGCAL_INIT_TMR},
	{0x0060, CMN_BGCAL_ITER_TMR},
	{0x0014, CMN_IBCAL_INIT_TMR},
	{0x0018, CMN_TXPUCAL_INIT_TMR},
	{0x0005, CMN_TXPUCAL_ITER_TMR},
	{0x0018, CMN_TXPDCAL_INIT_TMR},
	{0x0005, CMN_TXPDCAL_ITER_TMR},
	{0x0240, CMN_RXCAL_INIT_TMR},
	{0x0005, CMN_RXCAL_ITER_TMR},
	{0x0002, CMN_SD_CAL_INIT_TMR},
	{0x0002, CMN_SD_CAL_ITER_TMR},
	{0x000B, CMN_SD_CAL_REFTIM_START},
	{0x0137, CMN_SD_CAL_PLLCNT_START},
	{0x0509, CMN_PDIAG_PLL0_CP_PADJ_M0},
	{0x0F00, CMN_PDIAG_PLL0_CP_IADJ_M0},
	{0x0F08, CMN_PDIAG_PLL0_FILT_PADJ_M0},
	{0x0004, CMN_PLL0_DSM_DIAG_M0},
	{0x0509, CMN_PDIAG_PLL1_CP_PADJ_M0},
	{0x0F00, CMN_PDIAG_PLL1_CP_IADJ_M0},
	{0x0F08, CMN_PDIAG_PLL1_FILT_PADJ_M0},
	{0x0004, CMN_PLL1_DSM_DIAG_M0},
	{0x00C0, CMN_PLL0_VCOCAL_INIT_TMR},
	{0x0004, CMN_PLL0_VCOCAL_ITER_TMR},
	{0x00C0, CMN_PLL1_VCOCAL_INIT_TMR},
	{0x0004, CMN_PLL1_VCOCAL_ITER_TMR},
	{0x0260, CMN_PLL0_VCOCAL_REFTIM_START},
	{0x0003, CMN_PLL0_VCOCAL_TCTRL},
	{0x0260, CMN_PLL1_VCOCAL_REFTIM_START},
	{0x0003, CMN_PLL1_VCOCAL_TCTRL}
};

static struct cdns_reg_pairs sl_dp_19_2_no_ssc_tx_ln_regs[] = {
	{0x0780, TX_RCVDET_ST_TMR},
	{0x00FB, TX_PSC_A0},
	{0x04AA, TX_PSC_A2},
	{0x04AA, TX_PSC_A3},
	{0x000F, XCVR_DIAG_BIDI_CTRL}
};

static struct cdns_reg_pairs sl_dp_19_2_no_ssc_rx_ln_regs[] = {
	{0x0000, RX_PSC_A0},
	{0x0000, RX_PSC_A2},
	{0x0000, RX_PSC_A3},
	{0x0000, RX_PSC_CAL},
	{0x0000, RX_REE_GCSM1_CTRL},
	{0x0000, RX_REE_GCSM2_CTRL},
	{0x0000, RX_REE_PERGCSM_CTRL}
};

static struct cdns_torrent_vals sl_dp_19_2_no_ssc_cmn_vals = {
	.reg_pairs = sl_dp_19_2_no_ssc_cmn_regs,
	.num_regs = ARRAY_SIZE(sl_dp_19_2_no_ssc_cmn_regs),
};

static struct cdns_torrent_vals sl_dp_19_2_no_ssc_tx_ln_vals = {
	.reg_pairs = sl_dp_19_2_no_ssc_tx_ln_regs,
	.num_regs = ARRAY_SIZE(sl_dp_19_2_no_ssc_tx_ln_regs),
};

static struct cdns_torrent_vals sl_dp_19_2_no_ssc_rx_ln_vals = {
	.reg_pairs = sl_dp_19_2_no_ssc_rx_ln_regs,
	.num_regs = ARRAY_SIZE(sl_dp_19_2_no_ssc_rx_ln_regs),
};

/* Single DP, 25 MHz Ref clk, no SSC */
static struct cdns_reg_pairs sl_dp_25_no_ssc_cmn_regs[] = {
	{0x0019, CMN_SSM_BIAS_TMR},
	{0x0032, CMN_PLLSM0_PLLPRE_TMR},
	{0x00D1, CMN_PLLSM0_PLLLOCK_TMR},
	{0x0032, CMN_PLLSM1_PLLPRE_TMR},
	{0x00D1, CMN_PLLSM1_PLLLOCK_TMR},
	{0x007D, CMN_BGCAL_INIT_TMR},
	{0x007D, CMN_BGCAL_ITER_TMR},
	{0x0019, CMN_IBCAL_INIT_TMR},
	{0x001E, CMN_TXPUCAL_INIT_TMR},
	{0x0006, CMN_TXPUCAL_ITER_TMR},
	{0x001E, CMN_TXPDCAL_INIT_TMR},
	{0x0006, CMN_TXPDCAL_ITER_TMR},
	{0x02EE, CMN_RXCAL_INIT_TMR},
	{0x0006, CMN_RXCAL_ITER_TMR},
	{0x0002, CMN_SD_CAL_INIT_TMR},
	{0x0002, CMN_SD_CAL_ITER_TMR},
	{0x000E, CMN_SD_CAL_REFTIM_START},
	{0x012B, CMN_SD_CAL_PLLCNT_START},
	{0x0509, CMN_PDIAG_PLL0_CP_PADJ_M0},
	{0x0F00, CMN_PDIAG_PLL0_CP_IADJ_M0},
	{0x0F08, CMN_PDIAG_PLL0_FILT_PADJ_M0},
	{0x0004, CMN_PLL0_DSM_DIAG_M0},
	{0x0509, CMN_PDIAG_PLL1_CP_PADJ_M0},
	{0x0F00, CMN_PDIAG_PLL1_CP_IADJ_M0},
	{0x0F08, CMN_PDIAG_PLL1_FILT_PADJ_M0},
	{0x0004, CMN_PLL1_DSM_DIAG_M0},
	{0x00FA, CMN_PLL0_VCOCAL_INIT_TMR},
	{0x0004, CMN_PLL0_VCOCAL_ITER_TMR},
	{0x00FA, CMN_PLL1_VCOCAL_INIT_TMR},
	{0x0004, CMN_PLL1_VCOCAL_ITER_TMR},
	{0x0317, CMN_PLL0_VCOCAL_REFTIM_START},
	{0x0003, CMN_PLL0_VCOCAL_TCTRL},
	{0x0317, CMN_PLL1_VCOCAL_REFTIM_START},
	{0x0003, CMN_PLL1_VCOCAL_TCTRL}
};

static struct cdns_reg_pairs sl_dp_25_no_ssc_tx_ln_regs[] = {
	{0x09C4, TX_RCVDET_ST_TMR},
	{0x00FB, TX_PSC_A0},
	{0x04AA, TX_PSC_A2},
	{0x04AA, TX_PSC_A3},
	{0x000F, XCVR_DIAG_BIDI_CTRL}
};

static struct cdns_reg_pairs sl_dp_25_no_ssc_rx_ln_regs[] = {
	{0x0000, RX_PSC_A0},
	{0x0000, RX_PSC_A2},
	{0x0000, RX_PSC_A3},
	{0x0000, RX_PSC_CAL},
	{0x0000, RX_REE_GCSM1_CTRL},
	{0x0000, RX_REE_GCSM2_CTRL},
	{0x0000, RX_REE_PERGCSM_CTRL}
};

static struct cdns_torrent_vals sl_dp_25_no_ssc_cmn_vals = {
	.reg_pairs = sl_dp_25_no_ssc_cmn_regs,
	.num_regs = ARRAY_SIZE(sl_dp_25_no_ssc_cmn_regs),
};

static struct cdns_torrent_vals sl_dp_25_no_ssc_tx_ln_vals = {
	.reg_pairs = sl_dp_25_no_ssc_tx_ln_regs,
	.num_regs = ARRAY_SIZE(sl_dp_25_no_ssc_tx_ln_regs),
};

static struct cdns_torrent_vals sl_dp_25_no_ssc_rx_ln_vals = {
	.reg_pairs = sl_dp_25_no_ssc_rx_ln_regs,
	.num_regs = ARRAY_SIZE(sl_dp_25_no_ssc_rx_ln_regs),
};

/* Single DP, 100 MHz Ref clk, no SSC */
static struct cdns_reg_pairs sl_dp_100_no_ssc_cmn_regs[] = {
	{0x0003, CMN_PLL0_VCOCAL_TCTRL},
	{0x0003, CMN_PLL1_VCOCAL_TCTRL}
};

static struct cdns_reg_pairs sl_dp_100_no_ssc_tx_ln_regs[] = {
	{0x00FB, TX_PSC_A0},
	{0x04AA, TX_PSC_A2},
	{0x04AA, TX_PSC_A3},
	{0x000F, XCVR_DIAG_BIDI_CTRL}
};

static struct cdns_reg_pairs sl_dp_100_no_ssc_rx_ln_regs[] = {
	{0x0000, RX_PSC_A0},
	{0x0000, RX_PSC_A2},
	{0x0000, RX_PSC_A3},
	{0x0000, RX_PSC_CAL},
	{0x0000, RX_REE_GCSM1_CTRL},
	{0x0000, RX_REE_GCSM2_CTRL},
	{0x0000, RX_REE_PERGCSM_CTRL}
};

static struct cdns_torrent_vals sl_dp_100_no_ssc_cmn_vals = {
	.reg_pairs = sl_dp_100_no_ssc_cmn_regs,
	.num_regs = ARRAY_SIZE(sl_dp_100_no_ssc_cmn_regs),
};

static struct cdns_torrent_vals sl_dp_100_no_ssc_tx_ln_vals = {
	.reg_pairs = sl_dp_100_no_ssc_tx_ln_regs,
	.num_regs = ARRAY_SIZE(sl_dp_100_no_ssc_tx_ln_regs),
};

static struct cdns_torrent_vals sl_dp_100_no_ssc_rx_ln_vals = {
	.reg_pairs = sl_dp_100_no_ssc_rx_ln_regs,
	.num_regs = ARRAY_SIZE(sl_dp_100_no_ssc_rx_ln_regs),
};

/* USB and SGMII/QSGMII link configuration */
static struct cdns_reg_pairs usb_sgmii_link_cmn_regs[] = {
	{0x0002, PHY_PLL_CFG},
	{0x8600, CMN_PDIAG_PLL0_CLK_SEL_M0},
	{0x0601, CMN_PDIAG_PLL1_CLK_SEL_M0}
};

static struct cdns_reg_pairs usb_sgmii_xcvr_diag_ln_regs[] = {
	{0x0000, XCVR_DIAG_HSCLK_SEL},
	{0x0001, XCVR_DIAG_HSCLK_DIV},
	{0x0041, XCVR_DIAG_PLLDRC_CTRL}
};

static struct cdns_reg_pairs sgmii_usb_xcvr_diag_ln_regs[] = {
	{0x0011, XCVR_DIAG_HSCLK_SEL},
	{0x0003, XCVR_DIAG_HSCLK_DIV},
	{0x009B, XCVR_DIAG_PLLDRC_CTRL}
};

static struct cdns_torrent_vals usb_sgmii_link_cmn_vals = {
	.reg_pairs = usb_sgmii_link_cmn_regs,
	.num_regs = ARRAY_SIZE(usb_sgmii_link_cmn_regs),
};

static struct cdns_torrent_vals usb_sgmii_xcvr_diag_ln_vals = {
	.reg_pairs = usb_sgmii_xcvr_diag_ln_regs,
	.num_regs = ARRAY_SIZE(usb_sgmii_xcvr_diag_ln_regs),
};

static struct cdns_torrent_vals sgmii_usb_xcvr_diag_ln_vals = {
	.reg_pairs = sgmii_usb_xcvr_diag_ln_regs,
	.num_regs = ARRAY_SIZE(sgmii_usb_xcvr_diag_ln_regs),
};

/* PCIe and USB Unique SSC link configuration */
static struct cdns_reg_pairs pcie_usb_link_cmn_regs[] = {
	{0x0003, PHY_PLL_CFG},
	{0x0601, CMN_PDIAG_PLL0_CLK_SEL_M0},
	{0x0400, CMN_PDIAG_PLL0_CLK_SEL_M1},
	{0x8600, CMN_PDIAG_PLL1_CLK_SEL_M0}
};

static struct cdns_reg_pairs pcie_usb_xcvr_diag_ln_regs[] = {
	{0x0000, XCVR_DIAG_HSCLK_SEL},
	{0x0001, XCVR_DIAG_HSCLK_DIV},
	{0x0012, XCVR_DIAG_PLLDRC_CTRL}
};

static struct cdns_reg_pairs usb_pcie_xcvr_diag_ln_regs[] = {
	{0x0011, XCVR_DIAG_HSCLK_SEL},
	{0x0001, XCVR_DIAG_HSCLK_DIV},
	{0x00C9, XCVR_DIAG_PLLDRC_CTRL}
};

static struct cdns_torrent_vals pcie_usb_link_cmn_vals = {
	.reg_pairs = pcie_usb_link_cmn_regs,
	.num_regs = ARRAY_SIZE(pcie_usb_link_cmn_regs),
};

static struct cdns_torrent_vals pcie_usb_xcvr_diag_ln_vals = {
	.reg_pairs = pcie_usb_xcvr_diag_ln_regs,
	.num_regs = ARRAY_SIZE(pcie_usb_xcvr_diag_ln_regs),
};

static struct cdns_torrent_vals usb_pcie_xcvr_diag_ln_vals = {
	.reg_pairs = usb_pcie_xcvr_diag_ln_regs,
	.num_regs = ARRAY_SIZE(usb_pcie_xcvr_diag_ln_regs),
};

/* USB 100 MHz Ref clk, internal SSC */
static struct cdns_reg_pairs usb_100_int_ssc_cmn_regs[] = {
	{0x0004, CMN_PLL0_DSM_DIAG_M0},
	{0x0004, CMN_PLL0_DSM_DIAG_M1},
	{0x0004, CMN_PLL1_DSM_DIAG_M0},
	{0x0509, CMN_PDIAG_PLL0_CP_PADJ_M0},
	{0x0509, CMN_PDIAG_PLL0_CP_PADJ_M1},
	{0x0509, CMN_PDIAG_PLL1_CP_PADJ_M0},
	{0x0F00, CMN_PDIAG_PLL0_CP_IADJ_M0},
	{0x0F00, CMN_PDIAG_PLL0_CP_IADJ_M1},
	{0x0F00, CMN_PDIAG_PLL1_CP_IADJ_M0},
	{0x0F08, CMN_PDIAG_PLL0_FILT_PADJ_M0},
	{0x0F08, CMN_PDIAG_PLL0_FILT_PADJ_M1},
	{0x0F08, CMN_PDIAG_PLL1_FILT_PADJ_M0},
	{0x0064, CMN_PLL0_INTDIV_M0},
	{0x0050, CMN_PLL0_INTDIV_M1},
	{0x0064, CMN_PLL1_INTDIV_M0},
	{0x0002, CMN_PLL0_FRACDIVH_M0},
	{0x0002, CMN_PLL0_FRACDIVH_M1},
	{0x0002, CMN_PLL1_FRACDIVH_M0},
	{0x0044, CMN_PLL0_HIGH_THR_M0},
	{0x0036, CMN_PLL0_HIGH_THR_M1},
	{0x0044, CMN_PLL1_HIGH_THR_M0},
	{0x0002, CMN_PDIAG_PLL0_CTRL_M0},
	{0x0002, CMN_PDIAG_PLL0_CTRL_M1},
	{0x0002, CMN_PDIAG_PLL1_CTRL_M0},
	{0x0001, CMN_PLL0_SS_CTRL1_M0},
	{0x0001, CMN_PLL0_SS_CTRL1_M1},
	{0x0001, CMN_PLL1_SS_CTRL1_M0},
	{0x011B, CMN_PLL0_SS_CTRL2_M0},
	{0x011B, CMN_PLL0_SS_CTRL2_M1},
	{0x011B, CMN_PLL1_SS_CTRL2_M0},
	{0x006E, CMN_PLL0_SS_CTRL3_M0},
	{0x0058, CMN_PLL0_SS_CTRL3_M1},
	{0x006E, CMN_PLL1_SS_CTRL3_M0},
	{0x000E, CMN_PLL0_SS_CTRL4_M0},
	{0x0012, CMN_PLL0_SS_CTRL4_M1},
	{0x000E, CMN_PLL1_SS_CTRL4_M0},
	{0x0C5E, CMN_PLL0_VCOCAL_REFTIM_START},
	{0x0C5E, CMN_PLL1_VCOCAL_REFTIM_START},
	{0x0C56, CMN_PLL0_VCOCAL_PLLCNT_START},
	{0x0C56, CMN_PLL1_VCOCAL_PLLCNT_START},
	{0x00C7, CMN_PLL0_LOCK_REFCNT_START},
	{0x00C7, CMN_PLL1_LOCK_REFCNT_START},
	{0x00C7, CMN_PLL0_LOCK_PLLCNT_START},
	{0x00C7, CMN_PLL1_LOCK_PLLCNT_START},
	{0x0005, CMN_PLL0_LOCK_PLLCNT_THR},
	{0x0005, CMN_PLL1_LOCK_PLLCNT_THR},
	{0x8200, CMN_CDIAG_CDB_PWRI_OVRD},
	{0x8200, CMN_CDIAG_XCVRC_PWRI_OVRD},
	{0x007F, CMN_TXPUCAL_TUNE},
	{0x007F, CMN_TXPDCAL_TUNE}
};

static struct cdns_torrent_vals usb_100_int_ssc_cmn_vals = {
	.reg_pairs = usb_100_int_ssc_cmn_regs,
	.num_regs = ARRAY_SIZE(usb_100_int_ssc_cmn_regs),
};

/* Single USB link configuration */
static struct cdns_reg_pairs sl_usb_link_cmn_regs[] = {
	{0x0000, PHY_PLL_CFG},
	{0x8600, CMN_PDIAG_PLL0_CLK_SEL_M0}
};

static struct cdns_reg_pairs sl_usb_xcvr_diag_ln_regs[] = {
	{0x0000, XCVR_DIAG_HSCLK_SEL},
	{0x0001, XCVR_DIAG_HSCLK_DIV},
	{0x0041, XCVR_DIAG_PLLDRC_CTRL}
};

static struct cdns_torrent_vals sl_usb_link_cmn_vals = {
	.reg_pairs = sl_usb_link_cmn_regs,
	.num_regs = ARRAY_SIZE(sl_usb_link_cmn_regs),
};

static struct cdns_torrent_vals sl_usb_xcvr_diag_ln_vals = {
	.reg_pairs = sl_usb_xcvr_diag_ln_regs,
	.num_regs = ARRAY_SIZE(sl_usb_xcvr_diag_ln_regs),
};

/* USB PHY PCS common configuration */
static struct cdns_reg_pairs usb_phy_pcs_cmn_regs[] = {
	{0x0A0A, PHY_PIPE_USB3_GEN2_PRE_CFG0},
	{0x1000, PHY_PIPE_USB3_GEN2_POST_CFG0},
	{0x0010, PHY_PIPE_USB3_GEN2_POST_CFG1}
};

static struct cdns_torrent_vals usb_phy_pcs_cmn_vals = {
	.reg_pairs = usb_phy_pcs_cmn_regs,
	.num_regs = ARRAY_SIZE(usb_phy_pcs_cmn_regs),
};

/* USB 100 MHz Ref clk, no SSC */
static struct cdns_reg_pairs sl_usb_100_no_ssc_cmn_regs[] = {
	{0x0028, CMN_PDIAG_PLL1_CP_PADJ_M0},
	{0x001E, CMN_PLL1_DSM_FBH_OVRD_M0},
	{0x000C, CMN_PLL1_DSM_FBL_OVRD_M0},
	{0x0003, CMN_PLL0_VCOCAL_TCTRL},
	{0x0003, CMN_PLL1_VCOCAL_TCTRL},
	{0x8200, CMN_CDIAG_CDB_PWRI_OVRD},
	{0x8200, CMN_CDIAG_XCVRC_PWRI_OVRD}
};

static struct cdns_torrent_vals sl_usb_100_no_ssc_cmn_vals = {
	.reg_pairs = sl_usb_100_no_ssc_cmn_regs,
	.num_regs = ARRAY_SIZE(sl_usb_100_no_ssc_cmn_regs),
};

static struct cdns_reg_pairs usb_100_no_ssc_cmn_regs[] = {
	{0x8200, CMN_CDIAG_CDB_PWRI_OVRD},
	{0x8200, CMN_CDIAG_XCVRC_PWRI_OVRD},
	{0x007F, CMN_TXPUCAL_TUNE},
	{0x007F, CMN_TXPDCAL_TUNE}
};

static struct cdns_reg_pairs usb_100_no_ssc_tx_ln_regs[] = {
	{0x02FF, TX_PSC_A0},
	{0x06AF, TX_PSC_A1},
	{0x06AE, TX_PSC_A2},
	{0x06AE, TX_PSC_A3},
	{0x2A82, TX_TXCC_CTRL},
	{0x0014, TX_TXCC_CPOST_MULT_01},
	{0x0003, XCVR_DIAG_PSC_OVRD}
};

static struct cdns_reg_pairs usb_100_no_ssc_rx_ln_regs[] = {
	{0x0D1D, RX_PSC_A0},
	{0x0D1D, RX_PSC_A1},
	{0x0D00, RX_PSC_A2},
	{0x0500, RX_PSC_A3},
	{0x0013, RX_SIGDET_HL_FILT_TMR},
	{0x0000, RX_REE_GCSM1_CTRL},
	{0x0C02, RX_REE_ATTEN_THR},
	{0x0330, RX_REE_SMGM_CTRL1},
	{0x0300, RX_REE_SMGM_CTRL2},
	{0x0019, RX_REE_TAP1_CLIP},
	{0x0019, RX_REE_TAP2TON_CLIP},
	{0x1004, RX_DIAG_SIGDET_TUNE},
	{0x00F9, RX_DIAG_NQST_CTRL},
	{0x0C01, RX_DIAG_DFE_AMP_TUNE_2},
	{0x0002, RX_DIAG_DFE_AMP_TUNE_3},
	{0x0000, RX_DIAG_PI_CAP},
	{0x0031, RX_DIAG_PI_RATE},
	{0x0001, RX_DIAG_ACYA},
	{0x018C, RX_CDRLF_CNFG},
	{0x0003, RX_CDRLF_CNFG3}
};

static struct cdns_torrent_vals usb_100_no_ssc_cmn_vals = {
	.reg_pairs = usb_100_no_ssc_cmn_regs,
	.num_regs = ARRAY_SIZE(usb_100_no_ssc_cmn_regs),
};

static struct cdns_torrent_vals usb_100_no_ssc_tx_ln_vals = {
	.reg_pairs = usb_100_no_ssc_tx_ln_regs,
	.num_regs = ARRAY_SIZE(usb_100_no_ssc_tx_ln_regs),
};

static struct cdns_torrent_vals usb_100_no_ssc_rx_ln_vals = {
	.reg_pairs = usb_100_no_ssc_rx_ln_regs,
	.num_regs = ARRAY_SIZE(usb_100_no_ssc_rx_ln_regs),
};

/* Single link USB, 100 MHz Ref clk, internal SSC */
static struct cdns_reg_pairs sl_usb_100_int_ssc_cmn_regs[] = {
	{0x0004, CMN_PLL0_DSM_DIAG_M0},
	{0x0004, CMN_PLL1_DSM_DIAG_M0},
	{0x0509, CMN_PDIAG_PLL0_CP_PADJ_M0},
	{0x0509, CMN_PDIAG_PLL1_CP_PADJ_M0},
	{0x0F00, CMN_PDIAG_PLL0_CP_IADJ_M0},
	{0x0F00, CMN_PDIAG_PLL1_CP_IADJ_M0},
	{0x0F08, CMN_PDIAG_PLL0_FILT_PADJ_M0},
	{0x0F08, CMN_PDIAG_PLL1_FILT_PADJ_M0},
	{0x0064, CMN_PLL0_INTDIV_M0},
	{0x0064, CMN_PLL1_INTDIV_M0},
	{0x0002, CMN_PLL0_FRACDIVH_M0},
	{0x0002, CMN_PLL1_FRACDIVH_M0},
	{0x0044, CMN_PLL0_HIGH_THR_M0},
	{0x0044, CMN_PLL1_HIGH_THR_M0},
	{0x0002, CMN_PDIAG_PLL0_CTRL_M0},
	{0x0002, CMN_PDIAG_PLL1_CTRL_M0},
	{0x0001, CMN_PLL0_SS_CTRL1_M0},
	{0x0001, CMN_PLL1_SS_CTRL1_M0},
	{0x011B, CMN_PLL0_SS_CTRL2_M0},
	{0x011B, CMN_PLL1_SS_CTRL2_M0},
	{0x006E, CMN_PLL0_SS_CTRL3_M0},
	{0x006E, CMN_PLL1_SS_CTRL3_M0},
	{0x000E, CMN_PLL0_SS_CTRL4_M0},
	{0x000E, CMN_PLL1_SS_CTRL4_M0},
	{0x0C5E, CMN_PLL0_VCOCAL_REFTIM_START},
	{0x0C5E, CMN_PLL1_VCOCAL_REFTIM_START},
	{0x0C56, CMN_PLL0_VCOCAL_PLLCNT_START},
	{0x0C56, CMN_PLL1_VCOCAL_PLLCNT_START},
	{0x0003, CMN_PLL0_VCOCAL_TCTRL},
	{0x0003, CMN_PLL1_VCOCAL_TCTRL},
	{0x00C7, CMN_PLL0_LOCK_REFCNT_START},
	{0x00C7, CMN_PLL1_LOCK_REFCNT_START},
	{0x00C7, CMN_PLL0_LOCK_PLLCNT_START},
	{0x00C7, CMN_PLL1_LOCK_PLLCNT_START},
	{0x0005, CMN_PLL0_LOCK_PLLCNT_THR},
	{0x0005, CMN_PLL1_LOCK_PLLCNT_THR},
	{0x8200, CMN_CDIAG_CDB_PWRI_OVRD},
	{0x8200, CMN_CDIAG_XCVRC_PWRI_OVRD}
};

static struct cdns_torrent_vals sl_usb_100_int_ssc_cmn_vals = {
	.reg_pairs = sl_usb_100_int_ssc_cmn_regs,
	.num_regs = ARRAY_SIZE(sl_usb_100_int_ssc_cmn_regs),
};

/* PCIe and SGMII/QSGMII Unique SSC link configuration */
static struct cdns_reg_pairs pcie_sgmii_link_cmn_regs[] = {
	{0x0003, PHY_PLL_CFG},
	{0x0601, CMN_PDIAG_PLL0_CLK_SEL_M0},
	{0x0400, CMN_PDIAG_PLL0_CLK_SEL_M1},
	{0x0601, CMN_PDIAG_PLL1_CLK_SEL_M0}
};

static struct cdns_reg_pairs pcie_sgmii_xcvr_diag_ln_regs[] = {
	{0x0000, XCVR_DIAG_HSCLK_SEL},
	{0x0001, XCVR_DIAG_HSCLK_DIV},
	{0x0012, XCVR_DIAG_PLLDRC_CTRL}
};

static struct cdns_reg_pairs sgmii_pcie_xcvr_diag_ln_regs[] = {
	{0x0011, XCVR_DIAG_HSCLK_SEL},
	{0x0003, XCVR_DIAG_HSCLK_DIV},
	{0x009B, XCVR_DIAG_PLLDRC_CTRL}
};

static struct cdns_torrent_vals pcie_sgmii_link_cmn_vals = {
	.reg_pairs = pcie_sgmii_link_cmn_regs,
	.num_regs = ARRAY_SIZE(pcie_sgmii_link_cmn_regs),
};

static struct cdns_torrent_vals pcie_sgmii_xcvr_diag_ln_vals = {
	.reg_pairs = pcie_sgmii_xcvr_diag_ln_regs,
	.num_regs = ARRAY_SIZE(pcie_sgmii_xcvr_diag_ln_regs),
};

static struct cdns_torrent_vals sgmii_pcie_xcvr_diag_ln_vals = {
	.reg_pairs = sgmii_pcie_xcvr_diag_ln_regs,
	.num_regs = ARRAY_SIZE(sgmii_pcie_xcvr_diag_ln_regs),
};

/* SGMII 100 MHz Ref clk, no SSC */
static struct cdns_reg_pairs sl_sgmii_100_no_ssc_cmn_regs[] = {
	{0x0028, CMN_PDIAG_PLL1_CP_PADJ_M0},
	{0x001E, CMN_PLL1_DSM_FBH_OVRD_M0},
	{0x000C, CMN_PLL1_DSM_FBL_OVRD_M0},
	{0x0003, CMN_PLL0_VCOCAL_TCTRL},
	{0x0003, CMN_PLL1_VCOCAL_TCTRL}
};

static struct cdns_torrent_vals sl_sgmii_100_no_ssc_cmn_vals = {
	.reg_pairs = sl_sgmii_100_no_ssc_cmn_regs,
	.num_regs = ARRAY_SIZE(sl_sgmii_100_no_ssc_cmn_regs),
};

static struct cdns_reg_pairs sgmii_100_no_ssc_cmn_regs[] = {
	{0x007F, CMN_TXPUCAL_TUNE},
	{0x007F, CMN_TXPDCAL_TUNE}
};

static struct cdns_reg_pairs sgmii_100_no_ssc_tx_ln_regs[] = {
	{0x00F3, TX_PSC_A0},
	{0x04A2, TX_PSC_A2},
	{0x04A2, TX_PSC_A3},
	{0x0000, TX_TXCC_CPOST_MULT_00},
	{0x00B3, DRV_DIAG_TX_DRV}
};

static struct cdns_reg_pairs ti_sgmii_100_no_ssc_tx_ln_regs[] = {
	{0x00F3, TX_PSC_A0},
	{0x04A2, TX_PSC_A2},
	{0x04A2, TX_PSC_A3},
	{0x0000, TX_TXCC_CPOST_MULT_00},
	{0x00B3, DRV_DIAG_TX_DRV},
	{0x4000, XCVR_DIAG_RXCLK_CTRL},
};

static struct cdns_reg_pairs sgmii_100_no_ssc_rx_ln_regs[] = {
	{0x091D, RX_PSC_A0},
	{0x0900, RX_PSC_A2},
	{0x0100, RX_PSC_A3},
	{0x03C7, RX_REE_GCSM1_EQENM_PH1},
	{0x01C7, RX_REE_GCSM1_EQENM_PH2},
	{0x0000, RX_DIAG_DFE_CTRL},
	{0x0019, RX_REE_TAP1_CLIP},
	{0x0019, RX_REE_TAP2TON_CLIP},
	{0x0098, RX_DIAG_NQST_CTRL},
	{0x0C01, RX_DIAG_DFE_AMP_TUNE_2},
	{0x0000, RX_DIAG_DFE_AMP_TUNE_3},
	{0x0000, RX_DIAG_PI_CAP},
	{0x0010, RX_DIAG_PI_RATE},
	{0x0001, RX_DIAG_ACYA},
	{0x018C, RX_CDRLF_CNFG},
};

static struct cdns_torrent_vals sgmii_100_no_ssc_cmn_vals = {
	.reg_pairs = sgmii_100_no_ssc_cmn_regs,
	.num_regs = ARRAY_SIZE(sgmii_100_no_ssc_cmn_regs),
};

static struct cdns_torrent_vals sgmii_100_no_ssc_tx_ln_vals = {
	.reg_pairs = sgmii_100_no_ssc_tx_ln_regs,
	.num_regs = ARRAY_SIZE(sgmii_100_no_ssc_tx_ln_regs),
};

static struct cdns_torrent_vals ti_sgmii_100_no_ssc_tx_ln_vals = {
	.reg_pairs = ti_sgmii_100_no_ssc_tx_ln_regs,
	.num_regs = ARRAY_SIZE(ti_sgmii_100_no_ssc_tx_ln_regs),
};

static struct cdns_torrent_vals sgmii_100_no_ssc_rx_ln_vals = {
	.reg_pairs = sgmii_100_no_ssc_rx_ln_regs,
	.num_regs = ARRAY_SIZE(sgmii_100_no_ssc_rx_ln_regs),
};

/* SGMII 100 MHz Ref clk, internal SSC */
static struct cdns_reg_pairs sgmii_100_int_ssc_cmn_regs[] = {
	{0x0004, CMN_PLL0_DSM_DIAG_M0},
	{0x0004, CMN_PLL0_DSM_DIAG_M1},
	{0x0004, CMN_PLL1_DSM_DIAG_M0},
	{0x0509, CMN_PDIAG_PLL0_CP_PADJ_M0},
	{0x0509, CMN_PDIAG_PLL0_CP_PADJ_M1},
	{0x0509, CMN_PDIAG_PLL1_CP_PADJ_M0},
	{0x0F00, CMN_PDIAG_PLL0_CP_IADJ_M0},
	{0x0F00, CMN_PDIAG_PLL0_CP_IADJ_M1},
	{0x0F00, CMN_PDIAG_PLL1_CP_IADJ_M0},
	{0x0F08, CMN_PDIAG_PLL0_FILT_PADJ_M0},
	{0x0F08, CMN_PDIAG_PLL0_FILT_PADJ_M1},
	{0x0F08, CMN_PDIAG_PLL1_FILT_PADJ_M0},
	{0x0064, CMN_PLL0_INTDIV_M0},
	{0x0050, CMN_PLL0_INTDIV_M1},
	{0x0064, CMN_PLL1_INTDIV_M0},
	{0x0002, CMN_PLL0_FRACDIVH_M0},
	{0x0002, CMN_PLL0_FRACDIVH_M1},
	{0x0002, CMN_PLL1_FRACDIVH_M0},
	{0x0044, CMN_PLL0_HIGH_THR_M0},
	{0x0036, CMN_PLL0_HIGH_THR_M1},
	{0x0044, CMN_PLL1_HIGH_THR_M0},
	{0x0002, CMN_PDIAG_PLL0_CTRL_M0},
	{0x0002, CMN_PDIAG_PLL0_CTRL_M1},
	{0x0002, CMN_PDIAG_PLL1_CTRL_M0},
	{0x0001, CMN_PLL0_SS_CTRL1_M0},
	{0x0001, CMN_PLL0_SS_CTRL1_M1},
	{0x0001, CMN_PLL1_SS_CTRL1_M0},
	{0x011B, CMN_PLL0_SS_CTRL2_M0},
	{0x011B, CMN_PLL0_SS_CTRL2_M1},
	{0x011B, CMN_PLL1_SS_CTRL2_M0},
	{0x006E, CMN_PLL0_SS_CTRL3_M0},
	{0x0058, CMN_PLL0_SS_CTRL3_M1},
	{0x006E, CMN_PLL1_SS_CTRL3_M0},
	{0x000E, CMN_PLL0_SS_CTRL4_M0},
	{0x0012, CMN_PLL0_SS_CTRL4_M1},
	{0x000E, CMN_PLL1_SS_CTRL4_M0},
	{0x0C5E, CMN_PLL0_VCOCAL_REFTIM_START},
	{0x0C5E, CMN_PLL1_VCOCAL_REFTIM_START},
	{0x0C56, CMN_PLL0_VCOCAL_PLLCNT_START},
	{0x0C56, CMN_PLL1_VCOCAL_PLLCNT_START},
	{0x00C7, CMN_PLL0_LOCK_REFCNT_START},
	{0x00C7, CMN_PLL1_LOCK_REFCNT_START},
	{0x00C7, CMN_PLL0_LOCK_PLLCNT_START},
	{0x00C7, CMN_PLL1_LOCK_PLLCNT_START},
	{0x0005, CMN_PLL0_LOCK_PLLCNT_THR},
	{0x0005, CMN_PLL1_LOCK_PLLCNT_THR},
	{0x007F, CMN_TXPUCAL_TUNE},
	{0x007F, CMN_TXPDCAL_TUNE}
};

static struct cdns_torrent_vals sgmii_100_int_ssc_cmn_vals = {
	.reg_pairs = sgmii_100_int_ssc_cmn_regs,
	.num_regs = ARRAY_SIZE(sgmii_100_int_ssc_cmn_regs),
};

/* QSGMII 100 MHz Ref clk, no SSC */
static struct cdns_reg_pairs sl_qsgmii_100_no_ssc_cmn_regs[] = {
	{0x0028, CMN_PDIAG_PLL1_CP_PADJ_M0},
	{0x001E, CMN_PLL1_DSM_FBH_OVRD_M0},
	{0x000C, CMN_PLL1_DSM_FBL_OVRD_M0},
	{0x0003, CMN_PLL0_VCOCAL_TCTRL},
	{0x0003, CMN_PLL1_VCOCAL_TCTRL}
};

static struct cdns_torrent_vals sl_qsgmii_100_no_ssc_cmn_vals = {
	.reg_pairs = sl_qsgmii_100_no_ssc_cmn_regs,
	.num_regs = ARRAY_SIZE(sl_qsgmii_100_no_ssc_cmn_regs),
};

static struct cdns_reg_pairs qsgmii_100_no_ssc_cmn_regs[] = {
	{0x007F, CMN_TXPUCAL_TUNE},
	{0x007F, CMN_TXPDCAL_TUNE}
};

static struct cdns_reg_pairs qsgmii_100_no_ssc_tx_ln_regs[] = {
	{0x00F3, TX_PSC_A0},
	{0x04A2, TX_PSC_A2},
	{0x04A2, TX_PSC_A3},
	{0x0000, TX_TXCC_CPOST_MULT_00},
	{0x0011, TX_TXCC_MGNFS_MULT_100},
	{0x0003, DRV_DIAG_TX_DRV}
};

static struct cdns_reg_pairs ti_qsgmii_100_no_ssc_tx_ln_regs[] = {
	{0x00F3, TX_PSC_A0},
	{0x04A2, TX_PSC_A2},
	{0x04A2, TX_PSC_A3},
	{0x0000, TX_TXCC_CPOST_MULT_00},
	{0x0011, TX_TXCC_MGNFS_MULT_100},
	{0x0003, DRV_DIAG_TX_DRV},
	{0x4000, XCVR_DIAG_RXCLK_CTRL},
};

static struct cdns_reg_pairs qsgmii_100_no_ssc_rx_ln_regs[] = {
	{0x091D, RX_PSC_A0},
	{0x0900, RX_PSC_A2},
	{0x0100, RX_PSC_A3},
	{0x03C7, RX_REE_GCSM1_EQENM_PH1},
	{0x01C7, RX_REE_GCSM1_EQENM_PH2},
	{0x0000, RX_DIAG_DFE_CTRL},
	{0x0019, RX_REE_TAP1_CLIP},
	{0x0019, RX_REE_TAP2TON_CLIP},
	{0x0098, RX_DIAG_NQST_CTRL},
	{0x0C01, RX_DIAG_DFE_AMP_TUNE_2},
	{0x0000, RX_DIAG_DFE_AMP_TUNE_3},
	{0x0000, RX_DIAG_PI_CAP},
	{0x0010, RX_DIAG_PI_RATE},
	{0x0001, RX_DIAG_ACYA},
	{0x018C, RX_CDRLF_CNFG},
};

static struct cdns_torrent_vals qsgmii_100_no_ssc_cmn_vals = {
	.reg_pairs = qsgmii_100_no_ssc_cmn_regs,
	.num_regs = ARRAY_SIZE(qsgmii_100_no_ssc_cmn_regs),
};

static struct cdns_torrent_vals qsgmii_100_no_ssc_tx_ln_vals = {
	.reg_pairs = qsgmii_100_no_ssc_tx_ln_regs,
	.num_regs = ARRAY_SIZE(qsgmii_100_no_ssc_tx_ln_regs),
};

static struct cdns_torrent_vals ti_qsgmii_100_no_ssc_tx_ln_vals = {
	.reg_pairs = ti_qsgmii_100_no_ssc_tx_ln_regs,
	.num_regs = ARRAY_SIZE(ti_qsgmii_100_no_ssc_tx_ln_regs),
};

static struct cdns_torrent_vals qsgmii_100_no_ssc_rx_ln_vals = {
	.reg_pairs = qsgmii_100_no_ssc_rx_ln_regs,
	.num_regs = ARRAY_SIZE(qsgmii_100_no_ssc_rx_ln_regs),
};

/* QSGMII 100 MHz Ref clk, internal SSC */
static struct cdns_reg_pairs qsgmii_100_int_ssc_cmn_regs[] = {
	{0x0004, CMN_PLL0_DSM_DIAG_M0},
	{0x0004, CMN_PLL0_DSM_DIAG_M1},
	{0x0004, CMN_PLL1_DSM_DIAG_M0},
	{0x0509, CMN_PDIAG_PLL0_CP_PADJ_M0},
	{0x0509, CMN_PDIAG_PLL0_CP_PADJ_M1},
	{0x0509, CMN_PDIAG_PLL1_CP_PADJ_M0},
	{0x0F00, CMN_PDIAG_PLL0_CP_IADJ_M0},
	{0x0F00, CMN_PDIAG_PLL0_CP_IADJ_M1},
	{0x0F00, CMN_PDIAG_PLL1_CP_IADJ_M0},
	{0x0F08, CMN_PDIAG_PLL0_FILT_PADJ_M0},
	{0x0F08, CMN_PDIAG_PLL0_FILT_PADJ_M1},
	{0x0F08, CMN_PDIAG_PLL1_FILT_PADJ_M0},
	{0x0064, CMN_PLL0_INTDIV_M0},
	{0x0050, CMN_PLL0_INTDIV_M1},
	{0x0064, CMN_PLL1_INTDIV_M0},
	{0x0002, CMN_PLL0_FRACDIVH_M0},
	{0x0002, CMN_PLL0_FRACDIVH_M1},
	{0x0002, CMN_PLL1_FRACDIVH_M0},
	{0x0044, CMN_PLL0_HIGH_THR_M0},
	{0x0036, CMN_PLL0_HIGH_THR_M1},
	{0x0044, CMN_PLL1_HIGH_THR_M0},
	{0x0002, CMN_PDIAG_PLL0_CTRL_M0},
	{0x0002, CMN_PDIAG_PLL0_CTRL_M1},
	{0x0002, CMN_PDIAG_PLL1_CTRL_M0},
	{0x0001, CMN_PLL0_SS_CTRL1_M0},
	{0x0001, CMN_PLL0_SS_CTRL1_M1},
	{0x0001, CMN_PLL1_SS_CTRL1_M0},
	{0x011B, CMN_PLL0_SS_CTRL2_M0},
	{0x011B, CMN_PLL0_SS_CTRL2_M1},
	{0x011B, CMN_PLL1_SS_CTRL2_M0},
	{0x006E, CMN_PLL0_SS_CTRL3_M0},
	{0x0058, CMN_PLL0_SS_CTRL3_M1},
	{0x006E, CMN_PLL1_SS_CTRL3_M0},
	{0x000E, CMN_PLL0_SS_CTRL4_M0},
	{0x0012, CMN_PLL0_SS_CTRL4_M1},
	{0x000E, CMN_PLL1_SS_CTRL4_M0},
	{0x0C5E, CMN_PLL0_VCOCAL_REFTIM_START},
	{0x0C5E, CMN_PLL1_VCOCAL_REFTIM_START},
	{0x0C56, CMN_PLL0_VCOCAL_PLLCNT_START},
	{0x0C56, CMN_PLL1_VCOCAL_PLLCNT_START},
	{0x00C7, CMN_PLL0_LOCK_REFCNT_START},
	{0x00C7, CMN_PLL1_LOCK_REFCNT_START},
	{0x00C7, CMN_PLL0_LOCK_PLLCNT_START},
	{0x00C7, CMN_PLL1_LOCK_PLLCNT_START},
	{0x0005, CMN_PLL0_LOCK_PLLCNT_THR},
	{0x0005, CMN_PLL1_LOCK_PLLCNT_THR},
	{0x007F, CMN_TXPUCAL_TUNE},
	{0x007F, CMN_TXPDCAL_TUNE}
};

static struct cdns_torrent_vals qsgmii_100_int_ssc_cmn_vals = {
	.reg_pairs = qsgmii_100_int_ssc_cmn_regs,
	.num_regs = ARRAY_SIZE(qsgmii_100_int_ssc_cmn_regs),
};

/* Single SGMII/QSGMII link configuration */
static struct cdns_reg_pairs sl_sgmii_link_cmn_regs[] = {
	{0x0000, PHY_PLL_CFG},
	{0x0601, CMN_PDIAG_PLL0_CLK_SEL_M0}
};

static struct cdns_reg_pairs sl_sgmii_xcvr_diag_ln_regs[] = {
	{0x0000, XCVR_DIAG_HSCLK_SEL},
	{0x0003, XCVR_DIAG_HSCLK_DIV},
	{0x0013, XCVR_DIAG_PLLDRC_CTRL}
};

static struct cdns_torrent_vals sl_sgmii_link_cmn_vals = {
	.reg_pairs = sl_sgmii_link_cmn_regs,
	.num_regs = ARRAY_SIZE(sl_sgmii_link_cmn_regs),
};

static struct cdns_torrent_vals sl_sgmii_xcvr_diag_ln_vals = {
	.reg_pairs = sl_sgmii_xcvr_diag_ln_regs,
	.num_regs = ARRAY_SIZE(sl_sgmii_xcvr_diag_ln_regs),
};

/* Multi link PCIe, 100 MHz Ref clk, internal SSC */
static struct cdns_reg_pairs pcie_100_int_ssc_cmn_regs[] = {
	{0x0004, CMN_PLL0_DSM_DIAG_M0},
	{0x0004, CMN_PLL0_DSM_DIAG_M1},
	{0x0004, CMN_PLL1_DSM_DIAG_M0},
	{0x0509, CMN_PDIAG_PLL0_CP_PADJ_M0},
	{0x0509, CMN_PDIAG_PLL0_CP_PADJ_M1},
	{0x0509, CMN_PDIAG_PLL1_CP_PADJ_M0},
	{0x0F00, CMN_PDIAG_PLL0_CP_IADJ_M0},
	{0x0F00, CMN_PDIAG_PLL0_CP_IADJ_M1},
	{0x0F00, CMN_PDIAG_PLL1_CP_IADJ_M0},
	{0x0F08, CMN_PDIAG_PLL0_FILT_PADJ_M0},
	{0x0F08, CMN_PDIAG_PLL0_FILT_PADJ_M1},
	{0x0F08, CMN_PDIAG_PLL1_FILT_PADJ_M0},
	{0x0064, CMN_PLL0_INTDIV_M0},
	{0x0050, CMN_PLL0_INTDIV_M1},
	{0x0064, CMN_PLL1_INTDIV_M0},
	{0x0002, CMN_PLL0_FRACDIVH_M0},
	{0x0002, CMN_PLL0_FRACDIVH_M1},
	{0x0002, CMN_PLL1_FRACDIVH_M0},
	{0x0044, CMN_PLL0_HIGH_THR_M0},
	{0x0036, CMN_PLL0_HIGH_THR_M1},
	{0x0044, CMN_PLL1_HIGH_THR_M0},
	{0x0002, CMN_PDIAG_PLL0_CTRL_M0},
	{0x0002, CMN_PDIAG_PLL0_CTRL_M1},
	{0x0002, CMN_PDIAG_PLL1_CTRL_M0},
	{0x0001, CMN_PLL0_SS_CTRL1_M0},
	{0x0001, CMN_PLL0_SS_CTRL1_M1},
	{0x0001, CMN_PLL1_SS_CTRL1_M0},
	{0x011B, CMN_PLL0_SS_CTRL2_M0},
	{0x011B, CMN_PLL0_SS_CTRL2_M1},
	{0x011B, CMN_PLL1_SS_CTRL2_M0},
	{0x006E, CMN_PLL0_SS_CTRL3_M0},
	{0x0058, CMN_PLL0_SS_CTRL3_M1},
	{0x006E, CMN_PLL1_SS_CTRL3_M0},
	{0x000E, CMN_PLL0_SS_CTRL4_M0},
	{0x0012, CMN_PLL0_SS_CTRL4_M1},
	{0x000E, CMN_PLL1_SS_CTRL4_M0},
	{0x0C5E, CMN_PLL0_VCOCAL_REFTIM_START},
	{0x0C5E, CMN_PLL1_VCOCAL_REFTIM_START},
	{0x0C56, CMN_PLL0_VCOCAL_PLLCNT_START},
	{0x0C56, CMN_PLL1_VCOCAL_PLLCNT_START},
	{0x00C7, CMN_PLL0_LOCK_REFCNT_START},
	{0x00C7, CMN_PLL1_LOCK_REFCNT_START},
	{0x00C7, CMN_PLL0_LOCK_PLLCNT_START},
	{0x00C7, CMN_PLL1_LOCK_PLLCNT_START},
	{0x0005, CMN_PLL0_LOCK_PLLCNT_THR},
	{0x0005, CMN_PLL1_LOCK_PLLCNT_THR}
};

static struct cdns_torrent_vals pcie_100_int_ssc_cmn_vals = {
	.reg_pairs = pcie_100_int_ssc_cmn_regs,
	.num_regs = ARRAY_SIZE(pcie_100_int_ssc_cmn_regs),
};

/* Single link PCIe, 100 MHz Ref clk, internal SSC */
static struct cdns_reg_pairs sl_pcie_100_int_ssc_cmn_regs[] = {
	{0x0004, CMN_PLL0_DSM_DIAG_M0},
	{0x0004, CMN_PLL0_DSM_DIAG_M1},
	{0x0004, CMN_PLL1_DSM_DIAG_M0},
	{0x0509, CMN_PDIAG_PLL0_CP_PADJ_M0},
	{0x0509, CMN_PDIAG_PLL0_CP_PADJ_M1},
	{0x0509, CMN_PDIAG_PLL1_CP_PADJ_M0},
	{0x0F00, CMN_PDIAG_PLL0_CP_IADJ_M0},
	{0x0F00, CMN_PDIAG_PLL0_CP_IADJ_M1},
	{0x0F00, CMN_PDIAG_PLL1_CP_IADJ_M0},
	{0x0F08, CMN_PDIAG_PLL0_FILT_PADJ_M0},
	{0x0F08, CMN_PDIAG_PLL0_FILT_PADJ_M1},
	{0x0F08, CMN_PDIAG_PLL1_FILT_PADJ_M0},
	{0x0064, CMN_PLL0_INTDIV_M0},
	{0x0050, CMN_PLL0_INTDIV_M1},
	{0x0050, CMN_PLL1_INTDIV_M0},
	{0x0002, CMN_PLL0_FRACDIVH_M0},
	{0x0002, CMN_PLL0_FRACDIVH_M1},
	{0x0002, CMN_PLL1_FRACDIVH_M0},
	{0x0044, CMN_PLL0_HIGH_THR_M0},
	{0x0036, CMN_PLL0_HIGH_THR_M1},
	{0x0036, CMN_PLL1_HIGH_THR_M0},
	{0x0002, CMN_PDIAG_PLL0_CTRL_M0},
	{0x0002, CMN_PDIAG_PLL0_CTRL_M1},
	{0x0002, CMN_PDIAG_PLL1_CTRL_M0},
	{0x0001, CMN_PLL0_SS_CTRL1_M0},
	{0x0001, CMN_PLL0_SS_CTRL1_M1},
	{0x0001, CMN_PLL1_SS_CTRL1_M0},
	{0x011B, CMN_PLL0_SS_CTRL2_M0},
	{0x011B, CMN_PLL0_SS_CTRL2_M1},
	{0x011B, CMN_PLL1_SS_CTRL2_M0},
	{0x006E, CMN_PLL0_SS_CTRL3_M0},
	{0x0058, CMN_PLL0_SS_CTRL3_M1},
	{0x0058, CMN_PLL1_SS_CTRL3_M0},
	{0x000E, CMN_PLL0_SS_CTRL4_M0},
	{0x0012, CMN_PLL0_SS_CTRL4_M1},
	{0x0012, CMN_PLL1_SS_CTRL4_M0},
	{0x0C5E, CMN_PLL0_VCOCAL_REFTIM_START},
	{0x0C5E, CMN_PLL1_VCOCAL_REFTIM_START},
	{0x0C56, CMN_PLL0_VCOCAL_PLLCNT_START},
	{0x0C56, CMN_PLL1_VCOCAL_PLLCNT_START},
	{0x00C7, CMN_PLL0_LOCK_REFCNT_START},
	{0x00C7, CMN_PLL1_LOCK_REFCNT_START},
	{0x00C7, CMN_PLL0_LOCK_PLLCNT_START},
	{0x00C7, CMN_PLL1_LOCK_PLLCNT_START},
	{0x0005, CMN_PLL0_LOCK_PLLCNT_THR},
	{0x0005, CMN_PLL1_LOCK_PLLCNT_THR}
};

static struct cdns_torrent_vals sl_pcie_100_int_ssc_cmn_vals = {
	.reg_pairs = sl_pcie_100_int_ssc_cmn_regs,
	.num_regs = ARRAY_SIZE(sl_pcie_100_int_ssc_cmn_regs),
};

/* PCIe, 100 MHz Ref clk, no SSC & external SSC */
static struct cdns_reg_pairs pcie_100_ext_no_ssc_cmn_regs[] = {
	{0x0028, CMN_PDIAG_PLL1_CP_PADJ_M0},
	{0x001E, CMN_PLL1_DSM_FBH_OVRD_M0},
	{0x000C, CMN_PLL1_DSM_FBL_OVRD_M0}
};

static struct cdns_reg_pairs pcie_100_ext_no_ssc_rx_ln_regs[] = {
	{0x0019, RX_REE_TAP1_CLIP},
	{0x0019, RX_REE_TAP2TON_CLIP},
	{0x0001, RX_DIAG_ACYA}
};

static struct cdns_torrent_vals pcie_100_no_ssc_cmn_vals = {
	.reg_pairs = pcie_100_ext_no_ssc_cmn_regs,
	.num_regs = ARRAY_SIZE(pcie_100_ext_no_ssc_cmn_regs),
};

static struct cdns_torrent_vals pcie_100_no_ssc_rx_ln_vals = {
	.reg_pairs = pcie_100_ext_no_ssc_rx_ln_regs,
	.num_regs = ARRAY_SIZE(pcie_100_ext_no_ssc_rx_ln_regs),
};

static const struct cdns_torrent_data cdns_map_torrent = {
	.block_offset_shift = 0x2,
	.reg_offset_shift = 0x2,
	.link_cmn_vals = {
		[TYPE_DP] = {
			[TYPE_NONE] = {
				[NO_SSC] = &sl_dp_link_cmn_vals,
			},
		},
		[TYPE_PCIE] = {
			[TYPE_NONE] = {
				[NO_SSC] = NULL,
				[EXTERNAL_SSC] = NULL,
				[INTERNAL_SSC] = NULL,
			},
			[TYPE_SGMII] = {
				[NO_SSC] = &pcie_sgmii_link_cmn_vals,
				[EXTERNAL_SSC] = &pcie_sgmii_link_cmn_vals,
				[INTERNAL_SSC] = &pcie_sgmii_link_cmn_vals,
			},
			[TYPE_QSGMII] = {
				[NO_SSC] = &pcie_sgmii_link_cmn_vals,
				[EXTERNAL_SSC] = &pcie_sgmii_link_cmn_vals,
				[INTERNAL_SSC] = &pcie_sgmii_link_cmn_vals,
			},
			[TYPE_USB] = {
				[NO_SSC] = &pcie_usb_link_cmn_vals,
				[EXTERNAL_SSC] = &pcie_usb_link_cmn_vals,
				[INTERNAL_SSC] = &pcie_usb_link_cmn_vals,
			},
		},
		[TYPE_SGMII] = {
			[TYPE_NONE] = {
				[NO_SSC] = &sl_sgmii_link_cmn_vals,
			},
			[TYPE_PCIE] = {
				[NO_SSC] = &pcie_sgmii_link_cmn_vals,
				[EXTERNAL_SSC] = &pcie_sgmii_link_cmn_vals,
				[INTERNAL_SSC] = &pcie_sgmii_link_cmn_vals,
			},
			[TYPE_USB] = {
				[NO_SSC] = &usb_sgmii_link_cmn_vals,
				[EXTERNAL_SSC] = &usb_sgmii_link_cmn_vals,
				[INTERNAL_SSC] = &usb_sgmii_link_cmn_vals,
			},
		},
		[TYPE_QSGMII] = {
			[TYPE_NONE] = {
				[NO_SSC] = &sl_sgmii_link_cmn_vals,
			},
			[TYPE_PCIE] = {
				[NO_SSC] = &pcie_sgmii_link_cmn_vals,
				[EXTERNAL_SSC] = &pcie_sgmii_link_cmn_vals,
				[INTERNAL_SSC] = &pcie_sgmii_link_cmn_vals,
			},
			[TYPE_USB] = {
				[NO_SSC] = &usb_sgmii_link_cmn_vals,
				[EXTERNAL_SSC] = &usb_sgmii_link_cmn_vals,
				[INTERNAL_SSC] = &usb_sgmii_link_cmn_vals,
			},
		},
		[TYPE_USB] = {
			[TYPE_NONE] = {
				[NO_SSC] = &sl_usb_link_cmn_vals,
				[EXTERNAL_SSC] = &sl_usb_link_cmn_vals,
				[INTERNAL_SSC] = &sl_usb_link_cmn_vals,
			},
			[TYPE_PCIE] = {
				[NO_SSC] = &pcie_usb_link_cmn_vals,
				[EXTERNAL_SSC] = &pcie_usb_link_cmn_vals,
				[INTERNAL_SSC] = &pcie_usb_link_cmn_vals,
			},
			[TYPE_SGMII] = {
				[NO_SSC] = &usb_sgmii_link_cmn_vals,
				[EXTERNAL_SSC] = &usb_sgmii_link_cmn_vals,
				[INTERNAL_SSC] = &usb_sgmii_link_cmn_vals,
			},
			[TYPE_QSGMII] = {
				[NO_SSC] = &usb_sgmii_link_cmn_vals,
				[EXTERNAL_SSC] = &usb_sgmii_link_cmn_vals,
				[INTERNAL_SSC] = &usb_sgmii_link_cmn_vals,
			},
		},
	},
	.xcvr_diag_vals = {
		[TYPE_DP] = {
			[TYPE_NONE] = {
				[NO_SSC] = &sl_dp_xcvr_diag_ln_vals,
			},
		},
		[TYPE_PCIE] = {
			[TYPE_NONE] = {
				[NO_SSC] = NULL,
				[EXTERNAL_SSC] = NULL,
				[INTERNAL_SSC] = NULL,
			},
			[TYPE_SGMII] = {
				[NO_SSC] = &pcie_sgmii_xcvr_diag_ln_vals,
				[EXTERNAL_SSC] = &pcie_sgmii_xcvr_diag_ln_vals,
				[INTERNAL_SSC] = &pcie_sgmii_xcvr_diag_ln_vals,
			},
			[TYPE_QSGMII] = {
				[NO_SSC] = &pcie_sgmii_xcvr_diag_ln_vals,
				[EXTERNAL_SSC] = &pcie_sgmii_xcvr_diag_ln_vals,
				[INTERNAL_SSC] = &pcie_sgmii_xcvr_diag_ln_vals,
			},
			[TYPE_USB] = {
				[NO_SSC] = &pcie_usb_xcvr_diag_ln_vals,
				[EXTERNAL_SSC] = &pcie_usb_xcvr_diag_ln_vals,
				[INTERNAL_SSC] = &pcie_usb_xcvr_diag_ln_vals,
			},
		},
		[TYPE_SGMII] = {
			[TYPE_NONE] = {
				[NO_SSC] = &sl_sgmii_xcvr_diag_ln_vals,
			},
			[TYPE_PCIE] = {
				[NO_SSC] = &sgmii_pcie_xcvr_diag_ln_vals,
				[EXTERNAL_SSC] = &sgmii_pcie_xcvr_diag_ln_vals,
				[INTERNAL_SSC] = &sgmii_pcie_xcvr_diag_ln_vals,
			},
			[TYPE_USB] = {
				[NO_SSC] = &sgmii_usb_xcvr_diag_ln_vals,
				[EXTERNAL_SSC] = &sgmii_usb_xcvr_diag_ln_vals,
				[INTERNAL_SSC] = &sgmii_usb_xcvr_diag_ln_vals,
			},
		},
		[TYPE_QSGMII] = {
			[TYPE_NONE] = {
				[NO_SSC] = &sl_sgmii_xcvr_diag_ln_vals,
			},
			[TYPE_PCIE] = {
				[NO_SSC] = &sgmii_pcie_xcvr_diag_ln_vals,
				[EXTERNAL_SSC] = &sgmii_pcie_xcvr_diag_ln_vals,
				[INTERNAL_SSC] = &sgmii_pcie_xcvr_diag_ln_vals,
			},
			[TYPE_USB] = {
				[NO_SSC] = &sgmii_usb_xcvr_diag_ln_vals,
				[EXTERNAL_SSC] = &sgmii_usb_xcvr_diag_ln_vals,
				[INTERNAL_SSC] = &sgmii_usb_xcvr_diag_ln_vals,
			},
		},
		[TYPE_USB] = {
			[TYPE_NONE] = {
				[NO_SSC] = &sl_usb_xcvr_diag_ln_vals,
				[EXTERNAL_SSC] = &sl_usb_xcvr_diag_ln_vals,
				[INTERNAL_SSC] = &sl_usb_xcvr_diag_ln_vals,
			},
			[TYPE_PCIE] = {
				[NO_SSC] = &usb_pcie_xcvr_diag_ln_vals,
				[EXTERNAL_SSC] = &usb_pcie_xcvr_diag_ln_vals,
				[INTERNAL_SSC] = &usb_pcie_xcvr_diag_ln_vals,
			},
			[TYPE_SGMII] = {
				[NO_SSC] = &usb_sgmii_xcvr_diag_ln_vals,
				[EXTERNAL_SSC] = &usb_sgmii_xcvr_diag_ln_vals,
				[INTERNAL_SSC] = &usb_sgmii_xcvr_diag_ln_vals,
			},
			[TYPE_QSGMII] = {
				[NO_SSC] = &usb_sgmii_xcvr_diag_ln_vals,
				[EXTERNAL_SSC] = &usb_sgmii_xcvr_diag_ln_vals,
				[INTERNAL_SSC] = &usb_sgmii_xcvr_diag_ln_vals,
			},
		},
	},
	.pcs_cmn_vals = {
		[TYPE_USB] = {
			[TYPE_NONE] = {
				[NO_SSC] = &usb_phy_pcs_cmn_vals,
				[EXTERNAL_SSC] = &usb_phy_pcs_cmn_vals,
				[INTERNAL_SSC] = &usb_phy_pcs_cmn_vals,
			},
			[TYPE_PCIE] = {
				[NO_SSC] = &usb_phy_pcs_cmn_vals,
				[EXTERNAL_SSC] = &usb_phy_pcs_cmn_vals,
				[INTERNAL_SSC] = &usb_phy_pcs_cmn_vals,
			},
			[TYPE_SGMII] = {
				[NO_SSC] = &usb_phy_pcs_cmn_vals,
				[EXTERNAL_SSC] = &usb_phy_pcs_cmn_vals,
				[INTERNAL_SSC] = &usb_phy_pcs_cmn_vals,
			},
			[TYPE_QSGMII] = {
				[NO_SSC] = &usb_phy_pcs_cmn_vals,
				[EXTERNAL_SSC] = &usb_phy_pcs_cmn_vals,
				[INTERNAL_SSC] = &usb_phy_pcs_cmn_vals,
			},
		},
	},
	.cmn_vals = {
		[CLK_19_2_MHZ] = {
			[TYPE_DP] = {
				[TYPE_NONE] = {
					[NO_SSC] = &sl_dp_19_2_no_ssc_cmn_vals,
				},
			},
		},
		[CLK_25_MHZ] = {
			[TYPE_DP] = {
				[TYPE_NONE] = {
					[NO_SSC] = &sl_dp_25_no_ssc_cmn_vals,
				},
			},
		},
		[CLK_100_MHZ] = {
			[TYPE_DP] = {
				[TYPE_NONE] = {
					[NO_SSC] = &sl_dp_100_no_ssc_cmn_vals,
				},
			},
			[TYPE_PCIE] = {
				[TYPE_NONE] = {
					[NO_SSC] = NULL,
					[EXTERNAL_SSC] = NULL,
					[INTERNAL_SSC] = &sl_pcie_100_int_ssc_cmn_vals,
				},
				[TYPE_SGMII] = {
					[NO_SSC] = &pcie_100_no_ssc_cmn_vals,
					[EXTERNAL_SSC] = &pcie_100_no_ssc_cmn_vals,
					[INTERNAL_SSC] = &pcie_100_int_ssc_cmn_vals,
				},
				[TYPE_QSGMII] = {
					[NO_SSC] = &pcie_100_no_ssc_cmn_vals,
					[EXTERNAL_SSC] = &pcie_100_no_ssc_cmn_vals,
					[INTERNAL_SSC] = &pcie_100_int_ssc_cmn_vals,
				},
				[TYPE_USB] = {
					[NO_SSC] = &pcie_100_no_ssc_cmn_vals,
					[EXTERNAL_SSC] = &pcie_100_no_ssc_cmn_vals,
					[INTERNAL_SSC] = &pcie_100_int_ssc_cmn_vals,
				},
			},
			[TYPE_SGMII] = {
				[TYPE_NONE] = {
					[NO_SSC] = &sl_sgmii_100_no_ssc_cmn_vals,
				},
				[TYPE_PCIE] = {
					[NO_SSC] = &sgmii_100_no_ssc_cmn_vals,
					[EXTERNAL_SSC] = &sgmii_100_no_ssc_cmn_vals,
					[INTERNAL_SSC] = &sgmii_100_int_ssc_cmn_vals,
				},
				[TYPE_USB] = {
					[NO_SSC] = &sgmii_100_no_ssc_cmn_vals,
					[EXTERNAL_SSC] = &sgmii_100_no_ssc_cmn_vals,
					[INTERNAL_SSC] = &sgmii_100_no_ssc_cmn_vals,
				},
			},
			[TYPE_QSGMII] = {
				[TYPE_NONE] = {
					[NO_SSC] = &sl_qsgmii_100_no_ssc_cmn_vals,
				},
				[TYPE_PCIE] = {
					[NO_SSC] = &qsgmii_100_no_ssc_cmn_vals,
					[EXTERNAL_SSC] = &qsgmii_100_no_ssc_cmn_vals,
					[INTERNAL_SSC] = &qsgmii_100_int_ssc_cmn_vals,
				},
				[TYPE_USB] = {
					[NO_SSC] = &qsgmii_100_no_ssc_cmn_vals,
					[EXTERNAL_SSC] = &qsgmii_100_no_ssc_cmn_vals,
					[INTERNAL_SSC] = &qsgmii_100_no_ssc_cmn_vals,
				},
			},
			[TYPE_USB] = {
				[TYPE_NONE] = {
					[NO_SSC] = &sl_usb_100_no_ssc_cmn_vals,
					[EXTERNAL_SSC] = &sl_usb_100_no_ssc_cmn_vals,
					[INTERNAL_SSC] = &sl_usb_100_int_ssc_cmn_vals,
				},
				[TYPE_PCIE] = {
					[NO_SSC] = &usb_100_no_ssc_cmn_vals,
					[EXTERNAL_SSC] = &usb_100_no_ssc_cmn_vals,
					[INTERNAL_SSC] = &usb_100_int_ssc_cmn_vals,
				},
				[TYPE_SGMII] = {
					[NO_SSC] = &sl_usb_100_no_ssc_cmn_vals,
					[EXTERNAL_SSC] = &sl_usb_100_no_ssc_cmn_vals,
					[INTERNAL_SSC] = &sl_usb_100_int_ssc_cmn_vals,
				},
				[TYPE_QSGMII] = {
					[NO_SSC] = &sl_usb_100_no_ssc_cmn_vals,
					[EXTERNAL_SSC] = &sl_usb_100_no_ssc_cmn_vals,
					[INTERNAL_SSC] = &sl_usb_100_int_ssc_cmn_vals,
				},
			},
		},
	},
	.tx_ln_vals = {
		[CLK_19_2_MHZ] = {
			[TYPE_DP] = {
				[TYPE_NONE] = {
					[NO_SSC] = &sl_dp_19_2_no_ssc_tx_ln_vals,
				},
			},
		},
		[CLK_25_MHZ] = {
			[TYPE_DP] = {
				[TYPE_NONE] = {
					[NO_SSC] = &sl_dp_25_no_ssc_tx_ln_vals,
				},
			},
		},
		[CLK_100_MHZ] = {
			[TYPE_DP] = {
				[TYPE_NONE] = {
					[NO_SSC] = &sl_dp_100_no_ssc_tx_ln_vals,
				},
			},
			[TYPE_PCIE] = {
				[TYPE_NONE] = {
					[NO_SSC] = NULL,
					[EXTERNAL_SSC] = NULL,
					[INTERNAL_SSC] = NULL,
				},
				[TYPE_SGMII] = {
					[NO_SSC] = NULL,
					[EXTERNAL_SSC] = NULL,
					[INTERNAL_SSC] = NULL,
				},
				[TYPE_QSGMII] = {
					[NO_SSC] = NULL,
					[EXTERNAL_SSC] = NULL,
					[INTERNAL_SSC] = NULL,
				},
				[TYPE_USB] = {
					[NO_SSC] = NULL,
					[EXTERNAL_SSC] = NULL,
					[INTERNAL_SSC] = NULL,
				},
			},
			[TYPE_SGMII] = {
				[TYPE_NONE] = {
					[NO_SSC] = &sgmii_100_no_ssc_tx_ln_vals,
				},
				[TYPE_PCIE] = {
					[NO_SSC] = &sgmii_100_no_ssc_tx_ln_vals,
					[EXTERNAL_SSC] = &sgmii_100_no_ssc_tx_ln_vals,
					[INTERNAL_SSC] = &sgmii_100_no_ssc_tx_ln_vals,
				},
				[TYPE_USB] = {
					[NO_SSC] = &sgmii_100_no_ssc_tx_ln_vals,
					[EXTERNAL_SSC] = &sgmii_100_no_ssc_tx_ln_vals,
					[INTERNAL_SSC] = &sgmii_100_no_ssc_tx_ln_vals,
				},
			},
			[TYPE_QSGMII] = {
				[TYPE_NONE] = {
					[NO_SSC] = &qsgmii_100_no_ssc_tx_ln_vals,
				},
				[TYPE_PCIE] = {
					[NO_SSC] = &qsgmii_100_no_ssc_tx_ln_vals,
					[EXTERNAL_SSC] = &qsgmii_100_no_ssc_tx_ln_vals,
					[INTERNAL_SSC] = &qsgmii_100_no_ssc_tx_ln_vals,
				},
				[TYPE_USB] = {
					[NO_SSC] = &qsgmii_100_no_ssc_tx_ln_vals,
					[EXTERNAL_SSC] = &qsgmii_100_no_ssc_tx_ln_vals,
					[INTERNAL_SSC] = &qsgmii_100_no_ssc_tx_ln_vals,
				},
			},
			[TYPE_USB] = {
				[TYPE_NONE] = {
					[NO_SSC] = &usb_100_no_ssc_tx_ln_vals,
					[EXTERNAL_SSC] = &usb_100_no_ssc_tx_ln_vals,
					[INTERNAL_SSC] = &usb_100_no_ssc_tx_ln_vals,
				},
				[TYPE_PCIE] = {
					[NO_SSC] = &usb_100_no_ssc_tx_ln_vals,
					[EXTERNAL_SSC] = &usb_100_no_ssc_tx_ln_vals,
					[INTERNAL_SSC] = &usb_100_no_ssc_tx_ln_vals,
				},
				[TYPE_SGMII] = {
					[NO_SSC] = &usb_100_no_ssc_tx_ln_vals,
					[EXTERNAL_SSC] = &usb_100_no_ssc_tx_ln_vals,
					[INTERNAL_SSC] = &usb_100_no_ssc_tx_ln_vals,
				},
				[TYPE_QSGMII] = {
					[NO_SSC] = &usb_100_no_ssc_tx_ln_vals,
					[EXTERNAL_SSC] = &usb_100_no_ssc_tx_ln_vals,
					[INTERNAL_SSC] = &usb_100_no_ssc_tx_ln_vals,
				},
			},
		},
	},
	.rx_ln_vals = {
		[CLK_19_2_MHZ] = {
			[TYPE_DP] = {
				[TYPE_NONE] = {
					[NO_SSC] = &sl_dp_19_2_no_ssc_rx_ln_vals,
				},
			},
		},
		[CLK_25_MHZ] = {
			[TYPE_DP] = {
				[TYPE_NONE] = {
					[NO_SSC] = &sl_dp_25_no_ssc_rx_ln_vals,
				},
			},
		},
		[CLK_100_MHZ] = {
			[TYPE_DP] = {
				[TYPE_NONE] = {
					[NO_SSC] = &sl_dp_100_no_ssc_rx_ln_vals,
				},
			},
			[TYPE_PCIE] = {
				[TYPE_NONE] = {
					[NO_SSC] = &pcie_100_no_ssc_rx_ln_vals,
					[EXTERNAL_SSC] = &pcie_100_no_ssc_rx_ln_vals,
					[INTERNAL_SSC] = &pcie_100_no_ssc_rx_ln_vals,
				},
				[TYPE_SGMII] = {
					[NO_SSC] = &pcie_100_no_ssc_rx_ln_vals,
					[EXTERNAL_SSC] = &pcie_100_no_ssc_rx_ln_vals,
					[INTERNAL_SSC] = &pcie_100_no_ssc_rx_ln_vals,
				},
				[TYPE_QSGMII] = {
					[NO_SSC] = &pcie_100_no_ssc_rx_ln_vals,
					[EXTERNAL_SSC] = &pcie_100_no_ssc_rx_ln_vals,
					[INTERNAL_SSC] = &pcie_100_no_ssc_rx_ln_vals,
				},
				[TYPE_USB] = {
					[NO_SSC] = &pcie_100_no_ssc_rx_ln_vals,
					[EXTERNAL_SSC] = &pcie_100_no_ssc_rx_ln_vals,
					[INTERNAL_SSC] = &pcie_100_no_ssc_rx_ln_vals,
				},
			},
			[TYPE_SGMII] = {
				[TYPE_NONE] = {
					[NO_SSC] = &sgmii_100_no_ssc_rx_ln_vals,
				},
				[TYPE_PCIE] = {
					[NO_SSC] = &sgmii_100_no_ssc_rx_ln_vals,
					[EXTERNAL_SSC] = &sgmii_100_no_ssc_rx_ln_vals,
					[INTERNAL_SSC] = &sgmii_100_no_ssc_rx_ln_vals,
				},
				[TYPE_USB] = {
					[NO_SSC] = &sgmii_100_no_ssc_rx_ln_vals,
					[EXTERNAL_SSC] = &sgmii_100_no_ssc_rx_ln_vals,
					[INTERNAL_SSC] = &sgmii_100_no_ssc_rx_ln_vals,
				},
			},
			[TYPE_QSGMII] = {
				[TYPE_NONE] = {
					[NO_SSC] = &qsgmii_100_no_ssc_rx_ln_vals,
				},
				[TYPE_PCIE] = {
					[NO_SSC] = &qsgmii_100_no_ssc_rx_ln_vals,
					[EXTERNAL_SSC] = &qsgmii_100_no_ssc_rx_ln_vals,
					[INTERNAL_SSC] = &qsgmii_100_no_ssc_rx_ln_vals,
				},
				[TYPE_USB] = {
					[NO_SSC] = &qsgmii_100_no_ssc_rx_ln_vals,
					[EXTERNAL_SSC] = &qsgmii_100_no_ssc_rx_ln_vals,
					[INTERNAL_SSC] = &qsgmii_100_no_ssc_rx_ln_vals,
				},
			},
			[TYPE_USB] = {
				[TYPE_NONE] = {
					[NO_SSC] = &usb_100_no_ssc_rx_ln_vals,
					[EXTERNAL_SSC] = &usb_100_no_ssc_rx_ln_vals,
					[INTERNAL_SSC] = &usb_100_no_ssc_rx_ln_vals,
				},
				[TYPE_PCIE] = {
					[NO_SSC] = &usb_100_no_ssc_rx_ln_vals,
					[EXTERNAL_SSC] = &usb_100_no_ssc_rx_ln_vals,
					[INTERNAL_SSC] = &usb_100_no_ssc_rx_ln_vals,
				},
				[TYPE_SGMII] = {
					[NO_SSC] = &usb_100_no_ssc_rx_ln_vals,
					[EXTERNAL_SSC] = &usb_100_no_ssc_rx_ln_vals,
					[INTERNAL_SSC] = &usb_100_no_ssc_rx_ln_vals,
				},
				[TYPE_QSGMII] = {
					[NO_SSC] = &usb_100_no_ssc_rx_ln_vals,
					[EXTERNAL_SSC] = &usb_100_no_ssc_rx_ln_vals,
					[INTERNAL_SSC] = &usb_100_no_ssc_rx_ln_vals,
				},
			},
		},
	},
};

static const struct cdns_torrent_data ti_j721e_map_torrent = {
	.block_offset_shift = 0x0,
	.reg_offset_shift = 0x1,
	.link_cmn_vals = {
		[TYPE_DP] = {
			[TYPE_NONE] = {
				[NO_SSC] = &sl_dp_link_cmn_vals,
			},
		},
		[TYPE_PCIE] = {
			[TYPE_NONE] = {
				[NO_SSC] = NULL,
				[EXTERNAL_SSC] = NULL,
				[INTERNAL_SSC] = NULL,
			},
			[TYPE_SGMII] = {
				[NO_SSC] = &pcie_sgmii_link_cmn_vals,
				[EXTERNAL_SSC] = &pcie_sgmii_link_cmn_vals,
				[INTERNAL_SSC] = &pcie_sgmii_link_cmn_vals,
			},
			[TYPE_QSGMII] = {
				[NO_SSC] = &pcie_sgmii_link_cmn_vals,
				[EXTERNAL_SSC] = &pcie_sgmii_link_cmn_vals,
				[INTERNAL_SSC] = &pcie_sgmii_link_cmn_vals,
			},
			[TYPE_USB] = {
				[NO_SSC] = &pcie_usb_link_cmn_vals,
				[EXTERNAL_SSC] = &pcie_usb_link_cmn_vals,
				[INTERNAL_SSC] = &pcie_usb_link_cmn_vals,
			},
		},
		[TYPE_SGMII] = {
			[TYPE_NONE] = {
				[NO_SSC] = &sl_sgmii_link_cmn_vals,
			},
			[TYPE_PCIE] = {
				[NO_SSC] = &pcie_sgmii_link_cmn_vals,
				[EXTERNAL_SSC] = &pcie_sgmii_link_cmn_vals,
				[INTERNAL_SSC] = &pcie_sgmii_link_cmn_vals,
			},
			[TYPE_USB] = {
				[NO_SSC] = &usb_sgmii_link_cmn_vals,
				[EXTERNAL_SSC] = &usb_sgmii_link_cmn_vals,
				[INTERNAL_SSC] = &usb_sgmii_link_cmn_vals,
			},
		},
		[TYPE_QSGMII] = {
			[TYPE_NONE] = {
				[NO_SSC] = &sl_sgmii_link_cmn_vals,
			},
			[TYPE_PCIE] = {
				[NO_SSC] = &pcie_sgmii_link_cmn_vals,
				[EXTERNAL_SSC] = &pcie_sgmii_link_cmn_vals,
				[INTERNAL_SSC] = &pcie_sgmii_link_cmn_vals,
			},
			[TYPE_USB] = {
				[NO_SSC] = &usb_sgmii_link_cmn_vals,
				[EXTERNAL_SSC] = &usb_sgmii_link_cmn_vals,
				[INTERNAL_SSC] = &usb_sgmii_link_cmn_vals,
			},
		},
		[TYPE_USB] = {
			[TYPE_NONE] = {
				[NO_SSC] = &sl_usb_link_cmn_vals,
				[EXTERNAL_SSC] = &sl_usb_link_cmn_vals,
				[INTERNAL_SSC] = &sl_usb_link_cmn_vals,
			},
			[TYPE_PCIE] = {
				[NO_SSC] = &pcie_usb_link_cmn_vals,
				[EXTERNAL_SSC] = &pcie_usb_link_cmn_vals,
				[INTERNAL_SSC] = &pcie_usb_link_cmn_vals,
			},
			[TYPE_SGMII] = {
				[NO_SSC] = &usb_sgmii_link_cmn_vals,
				[EXTERNAL_SSC] = &usb_sgmii_link_cmn_vals,
				[INTERNAL_SSC] = &usb_sgmii_link_cmn_vals,
			},
			[TYPE_QSGMII] = {
				[NO_SSC] = &usb_sgmii_link_cmn_vals,
				[EXTERNAL_SSC] = &usb_sgmii_link_cmn_vals,
				[INTERNAL_SSC] = &usb_sgmii_link_cmn_vals,
			},
		},
	},
	.xcvr_diag_vals = {
		[TYPE_DP] = {
			[TYPE_NONE] = {
				[NO_SSC] = &sl_dp_xcvr_diag_ln_vals,
			},
		},
		[TYPE_PCIE] = {
			[TYPE_NONE] = {
				[NO_SSC] = NULL,
				[EXTERNAL_SSC] = NULL,
				[INTERNAL_SSC] = NULL,
			},
			[TYPE_SGMII] = {
				[NO_SSC] = &pcie_sgmii_xcvr_diag_ln_vals,
				[EXTERNAL_SSC] = &pcie_sgmii_xcvr_diag_ln_vals,
				[INTERNAL_SSC] = &pcie_sgmii_xcvr_diag_ln_vals,
			},
			[TYPE_QSGMII] = {
				[NO_SSC] = &pcie_sgmii_xcvr_diag_ln_vals,
				[EXTERNAL_SSC] = &pcie_sgmii_xcvr_diag_ln_vals,
				[INTERNAL_SSC] = &pcie_sgmii_xcvr_diag_ln_vals,
			},
			[TYPE_USB] = {
				[NO_SSC] = &pcie_usb_xcvr_diag_ln_vals,
				[EXTERNAL_SSC] = &pcie_usb_xcvr_diag_ln_vals,
				[INTERNAL_SSC] = &pcie_usb_xcvr_diag_ln_vals,
			},
		},
		[TYPE_SGMII] = {
			[TYPE_NONE] = {
				[NO_SSC] = &sl_sgmii_xcvr_diag_ln_vals,
			},
			[TYPE_PCIE] = {
				[NO_SSC] = &sgmii_pcie_xcvr_diag_ln_vals,
				[EXTERNAL_SSC] = &sgmii_pcie_xcvr_diag_ln_vals,
				[INTERNAL_SSC] = &sgmii_pcie_xcvr_diag_ln_vals,
			},
			[TYPE_USB] = {
				[NO_SSC] = &sgmii_usb_xcvr_diag_ln_vals,
				[EXTERNAL_SSC] = &sgmii_usb_xcvr_diag_ln_vals,
				[INTERNAL_SSC] = &sgmii_usb_xcvr_diag_ln_vals,
			},
		},
		[TYPE_QSGMII] = {
			[TYPE_NONE] = {
				[NO_SSC] = &sl_sgmii_xcvr_diag_ln_vals,
			},
			[TYPE_PCIE] = {
				[NO_SSC] = &sgmii_pcie_xcvr_diag_ln_vals,
				[EXTERNAL_SSC] = &sgmii_pcie_xcvr_diag_ln_vals,
				[INTERNAL_SSC] = &sgmii_pcie_xcvr_diag_ln_vals,
			},
			[TYPE_USB] = {
				[NO_SSC] = &sgmii_usb_xcvr_diag_ln_vals,
				[EXTERNAL_SSC] = &sgmii_usb_xcvr_diag_ln_vals,
				[INTERNAL_SSC] = &sgmii_usb_xcvr_diag_ln_vals,
			},
		},
		[TYPE_USB] = {
			[TYPE_NONE] = {
				[NO_SSC] = &sl_usb_xcvr_diag_ln_vals,
				[EXTERNAL_SSC] = &sl_usb_xcvr_diag_ln_vals,
				[INTERNAL_SSC] = &sl_usb_xcvr_diag_ln_vals,
			},
			[TYPE_PCIE] = {
				[NO_SSC] = &usb_pcie_xcvr_diag_ln_vals,
				[EXTERNAL_SSC] = &usb_pcie_xcvr_diag_ln_vals,
				[INTERNAL_SSC] = &usb_pcie_xcvr_diag_ln_vals,
			},
			[TYPE_SGMII] = {
				[NO_SSC] = &usb_sgmii_xcvr_diag_ln_vals,
				[EXTERNAL_SSC] = &usb_sgmii_xcvr_diag_ln_vals,
				[INTERNAL_SSC] = &usb_sgmii_xcvr_diag_ln_vals,
			},
			[TYPE_QSGMII] = {
				[NO_SSC] = &usb_sgmii_xcvr_diag_ln_vals,
				[EXTERNAL_SSC] = &usb_sgmii_xcvr_diag_ln_vals,
				[INTERNAL_SSC] = &usb_sgmii_xcvr_diag_ln_vals,
			},
		},
	},
	.pcs_cmn_vals = {
		[TYPE_USB] = {
			[TYPE_NONE] = {
				[NO_SSC] = &usb_phy_pcs_cmn_vals,
				[EXTERNAL_SSC] = &usb_phy_pcs_cmn_vals,
				[INTERNAL_SSC] = &usb_phy_pcs_cmn_vals,
			},
			[TYPE_PCIE] = {
				[NO_SSC] = &usb_phy_pcs_cmn_vals,
				[EXTERNAL_SSC] = &usb_phy_pcs_cmn_vals,
				[INTERNAL_SSC] = &usb_phy_pcs_cmn_vals,
			},
			[TYPE_SGMII] = {
				[NO_SSC] = &usb_phy_pcs_cmn_vals,
				[EXTERNAL_SSC] = &usb_phy_pcs_cmn_vals,
				[INTERNAL_SSC] = &usb_phy_pcs_cmn_vals,
			},
			[TYPE_QSGMII] = {
				[NO_SSC] = &usb_phy_pcs_cmn_vals,
				[EXTERNAL_SSC] = &usb_phy_pcs_cmn_vals,
				[INTERNAL_SSC] = &usb_phy_pcs_cmn_vals,
			},
		},
	},
	.cmn_vals = {
		[CLK_19_2_MHZ] = {
			[TYPE_DP] = {
				[TYPE_NONE] = {
					[NO_SSC] = &sl_dp_19_2_no_ssc_cmn_vals,
				},
			},
		},
		[CLK_25_MHZ] = {
			[TYPE_DP] = {
				[TYPE_NONE] = {
					[NO_SSC] = &sl_dp_25_no_ssc_cmn_vals,
				},
			},
		},
		[CLK_100_MHZ] = {
			[TYPE_DP] = {
				[TYPE_NONE] = {
					[NO_SSC] = &sl_dp_100_no_ssc_cmn_vals,
				},
			},
			[TYPE_PCIE] = {
				[TYPE_NONE] = {
					[NO_SSC] = NULL,
					[EXTERNAL_SSC] = NULL,
					[INTERNAL_SSC] = &sl_pcie_100_int_ssc_cmn_vals,
				},
				[TYPE_SGMII] = {
					[NO_SSC] = &pcie_100_no_ssc_cmn_vals,
					[EXTERNAL_SSC] = &pcie_100_no_ssc_cmn_vals,
					[INTERNAL_SSC] = &pcie_100_int_ssc_cmn_vals,
				},
				[TYPE_QSGMII] = {
					[NO_SSC] = &pcie_100_no_ssc_cmn_vals,
					[EXTERNAL_SSC] = &pcie_100_no_ssc_cmn_vals,
					[INTERNAL_SSC] = &pcie_100_int_ssc_cmn_vals,
				},
				[TYPE_USB] = {
					[NO_SSC] = &pcie_100_no_ssc_cmn_vals,
					[EXTERNAL_SSC] = &pcie_100_no_ssc_cmn_vals,
					[INTERNAL_SSC] = &pcie_100_int_ssc_cmn_vals,
				},
			},
			[TYPE_SGMII] = {
				[TYPE_NONE] = {
					[NO_SSC] = &sl_sgmii_100_no_ssc_cmn_vals,
				},
				[TYPE_PCIE] = {
					[NO_SSC] = &sgmii_100_no_ssc_cmn_vals,
					[EXTERNAL_SSC] = &sgmii_100_no_ssc_cmn_vals,
					[INTERNAL_SSC] = &sgmii_100_int_ssc_cmn_vals,
				},
				[TYPE_USB] = {
					[NO_SSC] = &sgmii_100_no_ssc_cmn_vals,
					[EXTERNAL_SSC] = &sgmii_100_no_ssc_cmn_vals,
					[INTERNAL_SSC] = &sgmii_100_no_ssc_cmn_vals,
				},
			},
			[TYPE_QSGMII] = {
				[TYPE_NONE] = {
					[NO_SSC] = &sl_qsgmii_100_no_ssc_cmn_vals,
				},
				[TYPE_PCIE] = {
					[NO_SSC] = &qsgmii_100_no_ssc_cmn_vals,
					[EXTERNAL_SSC] = &qsgmii_100_no_ssc_cmn_vals,
					[INTERNAL_SSC] = &qsgmii_100_int_ssc_cmn_vals,
				},
				[TYPE_USB] = {
					[NO_SSC] = &qsgmii_100_no_ssc_cmn_vals,
					[EXTERNAL_SSC] = &qsgmii_100_no_ssc_cmn_vals,
					[INTERNAL_SSC] = &qsgmii_100_no_ssc_cmn_vals,
				},
			},
			[TYPE_USB] = {
				[TYPE_NONE] = {
					[NO_SSC] = &sl_usb_100_no_ssc_cmn_vals,
					[EXTERNAL_SSC] = &sl_usb_100_no_ssc_cmn_vals,
					[INTERNAL_SSC] = &sl_usb_100_int_ssc_cmn_vals,
				},
				[TYPE_PCIE] = {
					[NO_SSC] = &usb_100_no_ssc_cmn_vals,
					[EXTERNAL_SSC] = &usb_100_no_ssc_cmn_vals,
					[INTERNAL_SSC] = &usb_100_int_ssc_cmn_vals,
				},
				[TYPE_SGMII] = {
					[NO_SSC] = &sl_usb_100_no_ssc_cmn_vals,
					[EXTERNAL_SSC] = &sl_usb_100_no_ssc_cmn_vals,
					[INTERNAL_SSC] = &sl_usb_100_int_ssc_cmn_vals,
				},
				[TYPE_QSGMII] = {
					[NO_SSC] = &sl_usb_100_no_ssc_cmn_vals,
					[EXTERNAL_SSC] = &sl_usb_100_no_ssc_cmn_vals,
					[INTERNAL_SSC] = &sl_usb_100_int_ssc_cmn_vals,
				},
			},
		},
	},
	.tx_ln_vals = {
		[CLK_19_2_MHZ] = {
			[TYPE_DP] = {
				[TYPE_NONE] = {
					[NO_SSC] = &sl_dp_19_2_no_ssc_tx_ln_vals,
				},
			},
		},
		[CLK_25_MHZ] = {
			[TYPE_DP] = {
				[TYPE_NONE] = {
					[NO_SSC] = &sl_dp_25_no_ssc_tx_ln_vals,
				},
			},
		},
		[CLK_100_MHZ] = {
			[TYPE_DP] = {
				[TYPE_NONE] = {
					[NO_SSC] = &sl_dp_100_no_ssc_tx_ln_vals,
				},
			},
			[TYPE_PCIE] = {
				[TYPE_NONE] = {
					[NO_SSC] = NULL,
					[EXTERNAL_SSC] = NULL,
					[INTERNAL_SSC] = NULL,
				},
				[TYPE_SGMII] = {
					[NO_SSC] = NULL,
					[EXTERNAL_SSC] = NULL,
					[INTERNAL_SSC] = NULL,
				},
				[TYPE_QSGMII] = {
					[NO_SSC] = NULL,
					[EXTERNAL_SSC] = NULL,
					[INTERNAL_SSC] = NULL,
				},
				[TYPE_USB] = {
					[NO_SSC] = NULL,
					[EXTERNAL_SSC] = NULL,
					[INTERNAL_SSC] = NULL,
				},
			},
			[TYPE_SGMII] = {
				[TYPE_NONE] = {
					[NO_SSC] = &ti_sgmii_100_no_ssc_tx_ln_vals,
				},
				[TYPE_PCIE] = {
					[NO_SSC] = &ti_sgmii_100_no_ssc_tx_ln_vals,
					[EXTERNAL_SSC] = &ti_sgmii_100_no_ssc_tx_ln_vals,
					[INTERNAL_SSC] = &ti_sgmii_100_no_ssc_tx_ln_vals,
				},
				[TYPE_USB] = {
					[NO_SSC] = &ti_sgmii_100_no_ssc_tx_ln_vals,
					[EXTERNAL_SSC] = &ti_sgmii_100_no_ssc_tx_ln_vals,
					[INTERNAL_SSC] = &ti_sgmii_100_no_ssc_tx_ln_vals,
				},
			},
			[TYPE_QSGMII] = {
				[TYPE_NONE] = {
					[NO_SSC] = &ti_qsgmii_100_no_ssc_tx_ln_vals,
				},
				[TYPE_PCIE] = {
					[NO_SSC] = &ti_qsgmii_100_no_ssc_tx_ln_vals,
					[EXTERNAL_SSC] = &ti_qsgmii_100_no_ssc_tx_ln_vals,
					[INTERNAL_SSC] = &ti_qsgmii_100_no_ssc_tx_ln_vals,
				},
				[TYPE_USB] = {
					[NO_SSC] = &ti_qsgmii_100_no_ssc_tx_ln_vals,
					[EXTERNAL_SSC] = &ti_qsgmii_100_no_ssc_tx_ln_vals,
					[INTERNAL_SSC] = &ti_qsgmii_100_no_ssc_tx_ln_vals,
				},
			},
			[TYPE_USB] = {
				[TYPE_NONE] = {
					[NO_SSC] = &usb_100_no_ssc_tx_ln_vals,
					[EXTERNAL_SSC] = &usb_100_no_ssc_tx_ln_vals,
					[INTERNAL_SSC] = &usb_100_no_ssc_tx_ln_vals,
				},
				[TYPE_PCIE] = {
					[NO_SSC] = &usb_100_no_ssc_tx_ln_vals,
					[EXTERNAL_SSC] = &usb_100_no_ssc_tx_ln_vals,
					[INTERNAL_SSC] = &usb_100_no_ssc_tx_ln_vals,
				},
				[TYPE_SGMII] = {
					[NO_SSC] = &usb_100_no_ssc_tx_ln_vals,
					[EXTERNAL_SSC] = &usb_100_no_ssc_tx_ln_vals,
					[INTERNAL_SSC] = &usb_100_no_ssc_tx_ln_vals,
				},
				[TYPE_QSGMII] = {
					[NO_SSC] = &usb_100_no_ssc_tx_ln_vals,
					[EXTERNAL_SSC] = &usb_100_no_ssc_tx_ln_vals,
					[INTERNAL_SSC] = &usb_100_no_ssc_tx_ln_vals,
				},
			},
		},
	},
	.rx_ln_vals = {
		[CLK_19_2_MHZ] = {
			[TYPE_DP] = {
				[TYPE_NONE] = {
					[NO_SSC] = &sl_dp_19_2_no_ssc_rx_ln_vals,
				},
			},
		},
		[CLK_25_MHZ] = {
			[TYPE_DP] = {
				[TYPE_NONE] = {
					[NO_SSC] = &sl_dp_25_no_ssc_rx_ln_vals,
				},
			},
		},
		[CLK_100_MHZ] = {
			[TYPE_DP] = {
				[TYPE_NONE] = {
					[NO_SSC] = &sl_dp_100_no_ssc_rx_ln_vals,
				},
			},
			[TYPE_PCIE] = {
				[TYPE_NONE] = {
					[NO_SSC] = &pcie_100_no_ssc_rx_ln_vals,
					[EXTERNAL_SSC] = &pcie_100_no_ssc_rx_ln_vals,
					[INTERNAL_SSC] = &pcie_100_no_ssc_rx_ln_vals,
				},
				[TYPE_SGMII] = {
					[NO_SSC] = &pcie_100_no_ssc_rx_ln_vals,
					[EXTERNAL_SSC] = &pcie_100_no_ssc_rx_ln_vals,
					[INTERNAL_SSC] = &pcie_100_no_ssc_rx_ln_vals,
				},
				[TYPE_QSGMII] = {
					[NO_SSC] = &pcie_100_no_ssc_rx_ln_vals,
					[EXTERNAL_SSC] = &pcie_100_no_ssc_rx_ln_vals,
					[INTERNAL_SSC] = &pcie_100_no_ssc_rx_ln_vals,
				},
				[TYPE_USB] = {
					[NO_SSC] = &pcie_100_no_ssc_rx_ln_vals,
					[EXTERNAL_SSC] = &pcie_100_no_ssc_rx_ln_vals,
					[INTERNAL_SSC] = &pcie_100_no_ssc_rx_ln_vals,
				},
			},
			[TYPE_SGMII] = {
				[TYPE_NONE] = {
					[NO_SSC] = &sgmii_100_no_ssc_rx_ln_vals,
				},
				[TYPE_PCIE] = {
					[NO_SSC] = &sgmii_100_no_ssc_rx_ln_vals,
					[EXTERNAL_SSC] = &sgmii_100_no_ssc_rx_ln_vals,
					[INTERNAL_SSC] = &sgmii_100_no_ssc_rx_ln_vals,
				},
				[TYPE_USB] = {
					[NO_SSC] = &sgmii_100_no_ssc_rx_ln_vals,
					[EXTERNAL_SSC] = &sgmii_100_no_ssc_rx_ln_vals,
					[INTERNAL_SSC] = &sgmii_100_no_ssc_rx_ln_vals,
				},
			},
			[TYPE_QSGMII] = {
				[TYPE_NONE] = {
					[NO_SSC] = &qsgmii_100_no_ssc_rx_ln_vals,
				},
				[TYPE_PCIE] = {
					[NO_SSC] = &qsgmii_100_no_ssc_rx_ln_vals,
					[EXTERNAL_SSC] = &qsgmii_100_no_ssc_rx_ln_vals,
					[INTERNAL_SSC] = &qsgmii_100_no_ssc_rx_ln_vals,
				},
				[TYPE_USB] = {
					[NO_SSC] = &qsgmii_100_no_ssc_rx_ln_vals,
					[EXTERNAL_SSC] = &qsgmii_100_no_ssc_rx_ln_vals,
					[INTERNAL_SSC] = &qsgmii_100_no_ssc_rx_ln_vals,
				},
			},
			[TYPE_USB] = {
				[TYPE_NONE] = {
					[NO_SSC] = &usb_100_no_ssc_rx_ln_vals,
					[EXTERNAL_SSC] = &usb_100_no_ssc_rx_ln_vals,
					[INTERNAL_SSC] = &usb_100_no_ssc_rx_ln_vals,
				},
				[TYPE_PCIE] = {
					[NO_SSC] = &usb_100_no_ssc_rx_ln_vals,
					[EXTERNAL_SSC] = &usb_100_no_ssc_rx_ln_vals,
					[INTERNAL_SSC] = &usb_100_no_ssc_rx_ln_vals,
				},
				[TYPE_SGMII] = {
					[NO_SSC] = &usb_100_no_ssc_rx_ln_vals,
					[EXTERNAL_SSC] = &usb_100_no_ssc_rx_ln_vals,
					[INTERNAL_SSC] = &usb_100_no_ssc_rx_ln_vals,
				},
				[TYPE_QSGMII] = {
					[NO_SSC] = &usb_100_no_ssc_rx_ln_vals,
					[EXTERNAL_SSC] = &usb_100_no_ssc_rx_ln_vals,
					[INTERNAL_SSC] = &usb_100_no_ssc_rx_ln_vals,
				},
			},
		},
	},
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
	.remove = cdns_torrent_phy_remove,
	.driver = {
		.name	= "cdns-torrent-phy",
		.of_match_table	= cdns_torrent_phy_of_match,
	}
};
module_platform_driver(cdns_torrent_phy_driver);

MODULE_AUTHOR("Cadence Design Systems, Inc.");
MODULE_DESCRIPTION("Cadence Torrent PHY driver");
MODULE_LICENSE("GPL v2");
