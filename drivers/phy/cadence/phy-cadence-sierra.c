// SPDX-License-Identifier: GPL-2.0
/*
 * Cadence Sierra PHY Driver
 *
 * Copyright (c) 2018 Cadence Design Systems
 * Author: Alan Douglas <adouglas@cadence.com>
 *
 */
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <dt-bindings/phy/phy.h>
#include <dt-bindings/phy/phy-cadence.h>

#define NUM_SSC_MODE		3
#define NUM_PHY_TYPE		4

/* PHY register offsets */
#define SIERRA_COMMON_CDB_OFFSET			0x0
#define SIERRA_MACRO_ID_REG				0x0
#define SIERRA_CMN_PLLLC_GEN_PREG			0x42
#define SIERRA_CMN_PLLLC_MODE_PREG			0x48
#define SIERRA_CMN_PLLLC_LF_COEFF_MODE1_PREG		0x49
#define SIERRA_CMN_PLLLC_LF_COEFF_MODE0_PREG		0x4A
#define SIERRA_CMN_PLLLC_LOCK_CNTSTART_PREG		0x4B
#define SIERRA_CMN_PLLLC_CLK1_PREG			0x4D
#define SIERRA_CMN_PLLLC_BWCAL_MODE1_PREG		0x4F
#define SIERRA_CMN_PLLLC_BWCAL_MODE0_PREG		0x50
#define SIERRA_CMN_PLLLC_DSMCORR_PREG			0x51
#define SIERRA_CMN_PLLLC_SS_PREG			0x52
#define SIERRA_CMN_PLLLC_SS_AMP_STEP_SIZE_PREG		0x53
#define SIERRA_CMN_PLLLC_SSTWOPT_PREG			0x54
#define SIERRA_CMN_PLLLC_SS_TIME_STEPSIZE_MODE_PREG	0x62
#define SIERRA_CMN_PLLLC_LOCK_DELAY_CTRL_PREG		0x63
#define SIERRA_CMN_REFRCV_PREG				0x98
#define SIERRA_CMN_REFRCV1_PREG				0xB8
#define SIERRA_CMN_PLLLC1_GEN_PREG			0xC2
#define SIERRA_CMN_PLLLC1_LF_COEFF_MODE0_PREG		0xCA
#define SIERRA_CMN_PLLLC1_BWCAL_MODE0_PREG		0xD0
#define SIERRA_CMN_PLLLC1_SS_TIME_STEPSIZE_MODE_PREG	0xE2

#define SIERRA_LANE_CDB_OFFSET(ln, block_offset, reg_offset)	\
				((0x4000 << (block_offset)) + \
				 (((ln) << 9) << (reg_offset)))

#define SIERRA_DET_STANDEC_A_PREG			0x000
#define SIERRA_DET_STANDEC_B_PREG			0x001
#define SIERRA_DET_STANDEC_C_PREG			0x002
#define SIERRA_DET_STANDEC_D_PREG			0x003
#define SIERRA_DET_STANDEC_E_PREG			0x004
#define SIERRA_PSM_LANECAL_DLY_A1_RESETS_PREG		0x008
#define SIERRA_PSM_A0IN_TMR_PREG			0x009
#define SIERRA_PSM_A3IN_TMR_PREG			0x00C
#define SIERRA_PSM_DIAG_PREG				0x015
#define SIERRA_PSC_LN_A3_PREG				0x023
#define SIERRA_PSC_LN_A4_PREG				0x024
#define SIERRA_PSC_LN_IDLE_PREG				0x026
#define SIERRA_PSC_TX_A0_PREG				0x028
#define SIERRA_PSC_TX_A1_PREG				0x029
#define SIERRA_PSC_TX_A2_PREG				0x02A
#define SIERRA_PSC_TX_A3_PREG				0x02B
#define SIERRA_PSC_RX_A0_PREG				0x030
#define SIERRA_PSC_RX_A1_PREG				0x031
#define SIERRA_PSC_RX_A2_PREG				0x032
#define SIERRA_PSC_RX_A3_PREG				0x033
#define SIERRA_PLLCTRL_SUBRATE_PREG			0x03A
#define SIERRA_PLLCTRL_GEN_A_PREG			0x03B
#define SIERRA_PLLCTRL_GEN_D_PREG			0x03E
#define SIERRA_PLLCTRL_CPGAIN_MODE_PREG			0x03F
#define SIERRA_PLLCTRL_STATUS_PREG			0x044
#define SIERRA_CLKPATH_BIASTRIM_PREG			0x04B
#define SIERRA_DFE_BIASTRIM_PREG			0x04C
#define SIERRA_DRVCTRL_ATTEN_PREG			0x06A
#define SIERRA_DRVCTRL_BOOST_PREG			0x06F
#define SIERRA_CLKPATHCTRL_TMR_PREG			0x081
#define SIERRA_RX_CREQ_FLTR_A_MODE3_PREG		0x085
#define SIERRA_RX_CREQ_FLTR_A_MODE2_PREG		0x086
#define SIERRA_RX_CREQ_FLTR_A_MODE1_PREG		0x087
#define SIERRA_RX_CREQ_FLTR_A_MODE0_PREG		0x088
#define SIERRA_CREQ_DCBIASATTEN_OVR_PREG		0x08C
#define SIERRA_CREQ_CCLKDET_MODE01_PREG			0x08E
#define SIERRA_RX_CTLE_CAL_PREG				0x08F
#define SIERRA_RX_CTLE_MAINTENANCE_PREG			0x091
#define SIERRA_CREQ_FSMCLK_SEL_PREG			0x092
#define SIERRA_CREQ_EQ_CTRL_PREG			0x093
#define SIERRA_CREQ_SPARE_PREG				0x096
#define SIERRA_CREQ_EQ_OPEN_EYE_THRESH_PREG		0x097
#define SIERRA_CTLELUT_CTRL_PREG			0x098
#define SIERRA_DFE_ECMP_RATESEL_PREG			0x0C0
#define SIERRA_DFE_SMP_RATESEL_PREG			0x0C1
#define SIERRA_DEQ_PHALIGN_CTRL				0x0C4
#define SIERRA_DEQ_CONCUR_CTRL1_PREG			0x0C8
#define SIERRA_DEQ_CONCUR_CTRL2_PREG			0x0C9
#define SIERRA_DEQ_EPIPWR_CTRL2_PREG			0x0CD
#define SIERRA_DEQ_FAST_MAINT_CYCLES_PREG		0x0CE
#define SIERRA_DEQ_ERRCMP_CTRL_PREG			0x0D0
#define SIERRA_DEQ_OFFSET_CTRL_PREG			0x0D8
#define SIERRA_DEQ_GAIN_CTRL_PREG			0x0E0
#define SIERRA_DEQ_VGATUNE_CTRL_PREG			0x0E1
#define SIERRA_DEQ_GLUT0				0x0E8
#define SIERRA_DEQ_GLUT1				0x0E9
#define SIERRA_DEQ_GLUT2				0x0EA
#define SIERRA_DEQ_GLUT3				0x0EB
#define SIERRA_DEQ_GLUT4				0x0EC
#define SIERRA_DEQ_GLUT5				0x0ED
#define SIERRA_DEQ_GLUT6				0x0EE
#define SIERRA_DEQ_GLUT7				0x0EF
#define SIERRA_DEQ_GLUT8				0x0F0
#define SIERRA_DEQ_GLUT9				0x0F1
#define SIERRA_DEQ_GLUT10				0x0F2
#define SIERRA_DEQ_GLUT11				0x0F3
#define SIERRA_DEQ_GLUT12				0x0F4
#define SIERRA_DEQ_GLUT13				0x0F5
#define SIERRA_DEQ_GLUT14				0x0F6
#define SIERRA_DEQ_GLUT15				0x0F7
#define SIERRA_DEQ_GLUT16				0x0F8
#define SIERRA_DEQ_ALUT0				0x108
#define SIERRA_DEQ_ALUT1				0x109
#define SIERRA_DEQ_ALUT2				0x10A
#define SIERRA_DEQ_ALUT3				0x10B
#define SIERRA_DEQ_ALUT4				0x10C
#define SIERRA_DEQ_ALUT5				0x10D
#define SIERRA_DEQ_ALUT6				0x10E
#define SIERRA_DEQ_ALUT7				0x10F
#define SIERRA_DEQ_ALUT8				0x110
#define SIERRA_DEQ_ALUT9				0x111
#define SIERRA_DEQ_ALUT10				0x112
#define SIERRA_DEQ_ALUT11				0x113
#define SIERRA_DEQ_ALUT12				0x114
#define SIERRA_DEQ_ALUT13				0x115
#define SIERRA_DEQ_DFETAP_CTRL_PREG			0x128
#define SIERRA_DEQ_DFETAP0				0x129
#define SIERRA_DEQ_DFETAP1				0x12B
#define SIERRA_DEQ_DFETAP2				0x12D
#define SIERRA_DEQ_DFETAP3				0x12F
#define SIERRA_DEQ_DFETAP4				0x131
#define SIERRA_DFE_EN_1010_IGNORE_PREG			0x134
#define SIERRA_DEQ_PRECUR_PREG				0x138
#define SIERRA_DEQ_POSTCUR_PREG				0x140
#define SIERRA_DEQ_POSTCUR_DECR_PREG			0x142
#define SIERRA_DEQ_TAU_CTRL1_SLOW_MAINT_PREG		0x150
#define SIERRA_DEQ_TAU_CTRL2_PREG			0x151
#define SIERRA_DEQ_TAU_CTRL3_PREG			0x152
#define SIERRA_DEQ_OPENEYE_CTRL_PREG			0x158
#define SIERRA_DEQ_PICTRL_PREG				0x161
#define SIERRA_CPICAL_TMRVAL_MODE1_PREG			0x170
#define SIERRA_CPICAL_TMRVAL_MODE0_PREG			0x171
#define SIERRA_CPICAL_PICNT_MODE1_PREG			0x174
#define SIERRA_CPI_OUTBUF_RATESEL_PREG			0x17C
#define SIERRA_CPI_RESBIAS_BIN_PREG			0x17E
#define SIERRA_CPI_TRIM_PREG				0x17F
#define SIERRA_CPICAL_RES_STARTCODE_MODE23_PREG		0x183
#define SIERRA_EPI_CTRL_PREG				0x187
#define SIERRA_LFPSDET_SUPPORT_PREG			0x188
#define SIERRA_LFPSFILT_NS_PREG				0x18A
#define SIERRA_LFPSFILT_RD_PREG				0x18B
#define SIERRA_LFPSFILT_MP_PREG				0x18C
#define SIERRA_SIGDET_SUPPORT_PREG			0x190
#define SIERRA_SDFILT_H2L_A_PREG			0x191
#define SIERRA_SDFILT_L2H_PREG				0x193
#define SIERRA_RXBUFFER_CTLECTRL_PREG			0x19E
#define SIERRA_RXBUFFER_RCDFECTRL_PREG			0x19F
#define SIERRA_RXBUFFER_DFECTRL_PREG			0x1A0
#define SIERRA_DEQ_TAU_CTRL1_FAST_MAINT_PREG		0x14F
#define SIERRA_DEQ_TAU_CTRL1_SLOW_MAINT_PREG		0x150

/* PHY PCS common registers */
#define SIERRA_PHY_PCS_COMMON_OFFSET(block_offset)	\
				     (0xc000 << (block_offset))
#define SIERRA_PHY_PIPE_CMN_CTRL1			0x0
#define SIERRA_PHY_PLL_CFG				0xe

/* PHY PCS lane registers */
#define SIERRA_PHY_PCS_LANE_CDB_OFFSET(ln, block_offset, reg_offset)	\
				       ((0xD000 << (block_offset)) +	\
				       (((ln) << 8) << (reg_offset)))

#define SIERRA_PHY_ISO_LINK_CTRL			0xB

/* PHY PMA common registers */
#define SIERRA_PHY_PMA_COMMON_OFFSET(block_offset)	\
				     (0xE000 << (block_offset))
#define SIERRA_PHY_PMA_CMN_CTRL				0x000

/* PHY PMA lane registers */
#define SIERRA_PHY_PMA_LANE_CDB_OFFSET(ln, block_offset, reg_offset)	\
				       ((0xF000 << (block_offset)) +	\
				       (((ln) << 8) << (reg_offset)))

#define SIERRA_PHY_PMA_XCVR_CTRL			0x000

#define SIERRA_MACRO_ID					0x00007364
#define SIERRA_MAX_LANES				16
#define PLL_LOCK_TIME					100000

#define CDNS_SIERRA_OUTPUT_CLOCKS			3
#define CDNS_SIERRA_INPUT_CLOCKS			5
enum cdns_sierra_clock_input {
	PHY_CLK,
	CMN_REFCLK_DIG_DIV,
	CMN_REFCLK1_DIG_DIV,
	PLL0_REFCLK,
	PLL1_REFCLK,
};

#define SIERRA_NUM_CMN_PLLC				2
#define SIERRA_NUM_CMN_PLLC_PARENTS			2

static const struct reg_field macro_id_type =
				REG_FIELD(SIERRA_MACRO_ID_REG, 0, 15);
static const struct reg_field phy_pll_cfg_1 =
				REG_FIELD(SIERRA_PHY_PLL_CFG, 1, 1);
static const struct reg_field pma_cmn_ready =
				REG_FIELD(SIERRA_PHY_PMA_CMN_CTRL, 0, 0);
static const struct reg_field pllctrl_lock =
				REG_FIELD(SIERRA_PLLCTRL_STATUS_PREG, 0, 0);
static const struct reg_field phy_iso_link_ctrl_1 =
				REG_FIELD(SIERRA_PHY_ISO_LINK_CTRL, 1, 1);
static const struct reg_field cmn_plllc_clk1outdiv_preg =
				REG_FIELD(SIERRA_CMN_PLLLC_CLK1_PREG, 0, 6);
static const struct reg_field cmn_plllc_clk1_en_preg =
				REG_FIELD(SIERRA_CMN_PLLLC_CLK1_PREG, 12, 12);

static const char * const clk_names[] = {
	[CDNS_SIERRA_PLL_CMNLC] = "pll_cmnlc",
	[CDNS_SIERRA_PLL_CMNLC1] = "pll_cmnlc1",
	[CDNS_SIERRA_DERIVED_REFCLK] = "refclk_der",
};

enum cdns_sierra_cmn_plllc {
	CMN_PLLLC,
	CMN_PLLLC1,
};

struct cdns_sierra_pll_mux_reg_fields {
	struct reg_field	pfdclk_sel_preg;
	struct reg_field	plllc1en_field;
	struct reg_field	termen_field;
};

static const struct cdns_sierra_pll_mux_reg_fields cmn_plllc_pfdclk1_sel_preg[] = {
	[CMN_PLLLC] = {
		.pfdclk_sel_preg = REG_FIELD(SIERRA_CMN_PLLLC_GEN_PREG, 1, 1),
		.plllc1en_field = REG_FIELD(SIERRA_CMN_REFRCV1_PREG, 8, 8),
		.termen_field = REG_FIELD(SIERRA_CMN_REFRCV1_PREG, 0, 0),
	},
	[CMN_PLLLC1] = {
		.pfdclk_sel_preg = REG_FIELD(SIERRA_CMN_PLLLC1_GEN_PREG, 1, 1),
		.plllc1en_field = REG_FIELD(SIERRA_CMN_REFRCV_PREG, 8, 8),
		.termen_field = REG_FIELD(SIERRA_CMN_REFRCV_PREG, 0, 0),
	},
};

struct cdns_sierra_pll_mux {
	struct clk_hw		hw;
	struct regmap_field	*pfdclk_sel_preg;
	struct regmap_field	*plllc1en_field;
	struct regmap_field	*termen_field;
	struct clk_init_data	clk_data;
};

#define to_cdns_sierra_pll_mux(_hw)	\
			container_of(_hw, struct cdns_sierra_pll_mux, hw)

static const int pll_mux_parent_index[][SIERRA_NUM_CMN_PLLC_PARENTS] = {
	[CMN_PLLLC] = { PLL0_REFCLK, PLL1_REFCLK },
	[CMN_PLLLC1] = { PLL1_REFCLK, PLL0_REFCLK },
};

static u32 cdns_sierra_pll_mux_table[][SIERRA_NUM_CMN_PLLC_PARENTS] = {
	[CMN_PLLLC] = { 0, 1 },
	[CMN_PLLLC1] = { 1, 0 },
};

struct cdns_sierra_derived_refclk {
	struct clk_hw           hw;
	struct regmap_field     *cmn_plllc_clk1outdiv_preg;
	struct regmap_field     *cmn_plllc_clk1_en_preg;
	struct clk_init_data	clk_data;
};

#define to_cdns_sierra_derived_refclk(_hw)	\
			container_of(_hw, struct cdns_sierra_derived_refclk, hw)

enum cdns_sierra_phy_type {
	TYPE_NONE,
	TYPE_PCIE,
	TYPE_USB,
	TYPE_QSGMII
};

enum cdns_sierra_ssc_mode {
	NO_SSC,
	EXTERNAL_SSC,
	INTERNAL_SSC
};

struct cdns_sierra_inst {
	struct phy *phy;
	enum cdns_sierra_phy_type phy_type;
	u32 num_lanes;
	u32 mlane;
	struct reset_control *lnk_rst;
	enum cdns_sierra_ssc_mode ssc_mode;
};

struct cdns_reg_pairs {
	u16 val;
	u32 off;
};

struct cdns_sierra_vals {
	const struct cdns_reg_pairs *reg_pairs;
	u32 num_regs;
};

struct cdns_sierra_data {
	u32 id_value;
	u8 block_offset_shift;
	u8 reg_offset_shift;
	struct cdns_sierra_vals *pcs_cmn_vals[NUM_PHY_TYPE][NUM_PHY_TYPE]
					     [NUM_SSC_MODE];
	struct cdns_sierra_vals *phy_pma_ln_vals[NUM_PHY_TYPE][NUM_PHY_TYPE]
						[NUM_SSC_MODE];
	struct cdns_sierra_vals *pma_cmn_vals[NUM_PHY_TYPE][NUM_PHY_TYPE]
					     [NUM_SSC_MODE];
	struct cdns_sierra_vals *pma_ln_vals[NUM_PHY_TYPE][NUM_PHY_TYPE]
					    [NUM_SSC_MODE];
};

struct cdns_regmap_cdb_context {
	struct device *dev;
	void __iomem *base;
	u8 reg_offset_shift;
};

struct cdns_sierra_phy {
	struct device *dev;
	struct regmap *regmap;
	const struct cdns_sierra_data *init_data;
	struct cdns_sierra_inst phys[SIERRA_MAX_LANES];
	struct reset_control *phy_rst;
	struct reset_control *apb_rst;
	struct regmap *regmap_lane_cdb[SIERRA_MAX_LANES];
	struct regmap *regmap_phy_pcs_common_cdb;
	struct regmap *regmap_phy_pcs_lane_cdb[SIERRA_MAX_LANES];
	struct regmap *regmap_phy_pma_common_cdb;
	struct regmap *regmap_phy_pma_lane_cdb[SIERRA_MAX_LANES];
	struct regmap *regmap_common_cdb;
	struct regmap_field *macro_id_type;
	struct regmap_field *phy_pll_cfg_1;
	struct regmap_field *pma_cmn_ready;
	struct regmap_field *pllctrl_lock[SIERRA_MAX_LANES];
	struct regmap_field *phy_iso_link_ctrl_1[SIERRA_MAX_LANES];
	struct regmap_field *cmn_refrcv_refclk_plllc1en_preg[SIERRA_NUM_CMN_PLLC];
	struct regmap_field *cmn_refrcv_refclk_termen_preg[SIERRA_NUM_CMN_PLLC];
	struct regmap_field *cmn_plllc_pfdclk1_sel_preg[SIERRA_NUM_CMN_PLLC];
	struct clk *input_clks[CDNS_SIERRA_INPUT_CLOCKS];
	int nsubnodes;
	u32 num_lanes;
	bool autoconf;
	struct clk_onecell_data clk_data;
	struct clk *output_clks[CDNS_SIERRA_OUTPUT_CLOCKS];
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

#define SIERRA_LANE_CDB_REGMAP_CONF(n) \
{ \
	.name = "sierra_lane" n "_cdb", \
	.reg_stride = 1, \
	.fast_io = true, \
	.reg_write = cdns_regmap_write, \
	.reg_read = cdns_regmap_read, \
}

static const struct regmap_config cdns_sierra_lane_cdb_config[] = {
	SIERRA_LANE_CDB_REGMAP_CONF("0"),
	SIERRA_LANE_CDB_REGMAP_CONF("1"),
	SIERRA_LANE_CDB_REGMAP_CONF("2"),
	SIERRA_LANE_CDB_REGMAP_CONF("3"),
	SIERRA_LANE_CDB_REGMAP_CONF("4"),
	SIERRA_LANE_CDB_REGMAP_CONF("5"),
	SIERRA_LANE_CDB_REGMAP_CONF("6"),
	SIERRA_LANE_CDB_REGMAP_CONF("7"),
	SIERRA_LANE_CDB_REGMAP_CONF("8"),
	SIERRA_LANE_CDB_REGMAP_CONF("9"),
	SIERRA_LANE_CDB_REGMAP_CONF("10"),
	SIERRA_LANE_CDB_REGMAP_CONF("11"),
	SIERRA_LANE_CDB_REGMAP_CONF("12"),
	SIERRA_LANE_CDB_REGMAP_CONF("13"),
	SIERRA_LANE_CDB_REGMAP_CONF("14"),
	SIERRA_LANE_CDB_REGMAP_CONF("15"),
};

static const struct regmap_config cdns_sierra_common_cdb_config = {
	.name = "sierra_common_cdb",
	.reg_stride = 1,
	.fast_io = true,
	.reg_write = cdns_regmap_write,
	.reg_read = cdns_regmap_read,
};

static const struct regmap_config cdns_sierra_phy_pcs_cmn_cdb_config = {
	.name = "sierra_phy_pcs_cmn_cdb",
	.reg_stride = 1,
	.fast_io = true,
	.reg_write = cdns_regmap_write,
	.reg_read = cdns_regmap_read,
};

#define SIERRA_PHY_PCS_LANE_CDB_REGMAP_CONF(n) \
{ \
	.name = "sierra_phy_pcs_lane" n "_cdb", \
	.reg_stride = 1, \
	.fast_io = true, \
	.reg_write = cdns_regmap_write, \
	.reg_read = cdns_regmap_read, \
}

static const struct regmap_config cdns_sierra_phy_pcs_lane_cdb_config[] = {
	SIERRA_PHY_PCS_LANE_CDB_REGMAP_CONF("0"),
	SIERRA_PHY_PCS_LANE_CDB_REGMAP_CONF("1"),
	SIERRA_PHY_PCS_LANE_CDB_REGMAP_CONF("2"),
	SIERRA_PHY_PCS_LANE_CDB_REGMAP_CONF("3"),
	SIERRA_PHY_PCS_LANE_CDB_REGMAP_CONF("4"),
	SIERRA_PHY_PCS_LANE_CDB_REGMAP_CONF("5"),
	SIERRA_PHY_PCS_LANE_CDB_REGMAP_CONF("6"),
	SIERRA_PHY_PCS_LANE_CDB_REGMAP_CONF("7"),
	SIERRA_PHY_PCS_LANE_CDB_REGMAP_CONF("8"),
	SIERRA_PHY_PCS_LANE_CDB_REGMAP_CONF("9"),
	SIERRA_PHY_PCS_LANE_CDB_REGMAP_CONF("10"),
	SIERRA_PHY_PCS_LANE_CDB_REGMAP_CONF("11"),
	SIERRA_PHY_PCS_LANE_CDB_REGMAP_CONF("12"),
	SIERRA_PHY_PCS_LANE_CDB_REGMAP_CONF("13"),
	SIERRA_PHY_PCS_LANE_CDB_REGMAP_CONF("14"),
	SIERRA_PHY_PCS_LANE_CDB_REGMAP_CONF("15"),
};

static const struct regmap_config cdns_sierra_phy_pma_cmn_cdb_config = {
	.name = "sierra_phy_pma_cmn_cdb",
	.reg_stride = 1,
	.fast_io = true,
	.reg_write = cdns_regmap_write,
	.reg_read = cdns_regmap_read,
};

#define SIERRA_PHY_PMA_LANE_CDB_REGMAP_CONF(n) \
{ \
	.name = "sierra_phy_pma_lane" n "_cdb", \
	.reg_stride = 1, \
	.fast_io = true, \
	.reg_write = cdns_regmap_write, \
	.reg_read = cdns_regmap_read, \
}

static const struct regmap_config cdns_sierra_phy_pma_lane_cdb_config[] = {
	SIERRA_PHY_PMA_LANE_CDB_REGMAP_CONF("0"),
	SIERRA_PHY_PMA_LANE_CDB_REGMAP_CONF("1"),
	SIERRA_PHY_PMA_LANE_CDB_REGMAP_CONF("2"),
	SIERRA_PHY_PMA_LANE_CDB_REGMAP_CONF("3"),
	SIERRA_PHY_PMA_LANE_CDB_REGMAP_CONF("4"),
	SIERRA_PHY_PMA_LANE_CDB_REGMAP_CONF("5"),
	SIERRA_PHY_PMA_LANE_CDB_REGMAP_CONF("6"),
	SIERRA_PHY_PMA_LANE_CDB_REGMAP_CONF("7"),
	SIERRA_PHY_PMA_LANE_CDB_REGMAP_CONF("8"),
	SIERRA_PHY_PMA_LANE_CDB_REGMAP_CONF("9"),
	SIERRA_PHY_PMA_LANE_CDB_REGMAP_CONF("10"),
	SIERRA_PHY_PMA_LANE_CDB_REGMAP_CONF("11"),
	SIERRA_PHY_PMA_LANE_CDB_REGMAP_CONF("12"),
	SIERRA_PHY_PMA_LANE_CDB_REGMAP_CONF("13"),
	SIERRA_PHY_PMA_LANE_CDB_REGMAP_CONF("14"),
	SIERRA_PHY_PMA_LANE_CDB_REGMAP_CONF("15"),
};

static int cdns_sierra_phy_init(struct phy *gphy)
{
	struct cdns_sierra_inst *ins = phy_get_drvdata(gphy);
	struct cdns_sierra_phy *phy = dev_get_drvdata(gphy->dev.parent);
	const struct cdns_sierra_data *init_data = phy->init_data;
	struct cdns_sierra_vals *pma_cmn_vals, *pma_ln_vals;
	enum cdns_sierra_phy_type phy_type = ins->phy_type;
	enum cdns_sierra_ssc_mode ssc = ins->ssc_mode;
	struct cdns_sierra_vals *phy_pma_ln_vals;
	const struct cdns_reg_pairs *reg_pairs;
	struct cdns_sierra_vals *pcs_cmn_vals;
	struct regmap *regmap;
	u32 num_regs;
	int i, j;

	/* Initialise the PHY registers, unless auto configured */
	if (phy->autoconf || phy->nsubnodes > 1)
		return 0;

	clk_set_rate(phy->input_clks[CMN_REFCLK_DIG_DIV], 25000000);
	clk_set_rate(phy->input_clks[CMN_REFCLK1_DIG_DIV], 25000000);

	/* PHY PCS common registers configurations */
	pcs_cmn_vals = init_data->pcs_cmn_vals[phy_type][TYPE_NONE][ssc];
	if (pcs_cmn_vals) {
		reg_pairs = pcs_cmn_vals->reg_pairs;
		num_regs = pcs_cmn_vals->num_regs;
		regmap = phy->regmap_phy_pcs_common_cdb;
		for (i = 0; i < num_regs; i++)
			regmap_write(regmap, reg_pairs[i].off, reg_pairs[i].val);
	}

	/* PHY PMA lane registers configurations */
	phy_pma_ln_vals = init_data->phy_pma_ln_vals[phy_type][TYPE_NONE][ssc];
	if (phy_pma_ln_vals) {
		reg_pairs = phy_pma_ln_vals->reg_pairs;
		num_regs = phy_pma_ln_vals->num_regs;
		for (i = 0; i < ins->num_lanes; i++) {
			regmap = phy->regmap_phy_pma_lane_cdb[i + ins->mlane];
			for (j = 0; j < num_regs; j++)
				regmap_write(regmap, reg_pairs[j].off, reg_pairs[j].val);
		}
	}

	/* PMA common registers configurations */
	pma_cmn_vals = init_data->pma_cmn_vals[phy_type][TYPE_NONE][ssc];
	if (pma_cmn_vals) {
		reg_pairs = pma_cmn_vals->reg_pairs;
		num_regs = pma_cmn_vals->num_regs;
		regmap = phy->regmap_common_cdb;
		for (i = 0; i < num_regs; i++)
			regmap_write(regmap, reg_pairs[i].off, reg_pairs[i].val);
	}

	/* PMA lane registers configurations */
	pma_ln_vals = init_data->pma_ln_vals[phy_type][TYPE_NONE][ssc];
	if (pma_ln_vals) {
		reg_pairs = pma_ln_vals->reg_pairs;
		num_regs = pma_ln_vals->num_regs;
		for (i = 0; i < ins->num_lanes; i++) {
			regmap = phy->regmap_lane_cdb[i + ins->mlane];
			for (j = 0; j < num_regs; j++)
				regmap_write(regmap, reg_pairs[j].off, reg_pairs[j].val);
		}
	}

	return 0;
}

static int cdns_sierra_phy_on(struct phy *gphy)
{
	struct cdns_sierra_phy *sp = dev_get_drvdata(gphy->dev.parent);
	struct cdns_sierra_inst *ins = phy_get_drvdata(gphy);
	struct device *dev = sp->dev;
	u32 val;
	int ret;

	if (sp->nsubnodes == 1) {
		/* Take the PHY out of reset */
		ret = reset_control_deassert(sp->phy_rst);
		if (ret) {
			dev_err(dev, "Failed to take the PHY out of reset\n");
			return ret;
		}
	}

	/* Take the PHY lane group out of reset */
	ret = reset_control_deassert(ins->lnk_rst);
	if (ret) {
		dev_err(dev, "Failed to take the PHY lane out of reset\n");
		return ret;
	}

	if (ins->phy_type == TYPE_PCIE || ins->phy_type == TYPE_USB) {
		ret = regmap_field_read_poll_timeout(sp->phy_iso_link_ctrl_1[ins->mlane],
						     val, !val, 1000, PLL_LOCK_TIME);
		if (ret) {
			dev_err(dev, "Timeout waiting for PHY status ready\n");
			return ret;
		}
	}

	/*
	 * Wait for cmn_ready assertion
	 * PHY_PMA_CMN_CTRL[0] == 1
	 */
	ret = regmap_field_read_poll_timeout(sp->pma_cmn_ready, val, val,
					     1000, PLL_LOCK_TIME);
	if (ret) {
		dev_err(dev, "Timeout waiting for CMN ready\n");
		return ret;
	}

	ret = regmap_field_read_poll_timeout(sp->pllctrl_lock[ins->mlane],
					     val, val, 1000, PLL_LOCK_TIME);
	if (ret < 0)
		dev_err(dev, "PLL lock of lane failed\n");

	return ret;
}

static int cdns_sierra_phy_off(struct phy *gphy)
{
	struct cdns_sierra_inst *ins = phy_get_drvdata(gphy);

	return reset_control_assert(ins->lnk_rst);
}

static int cdns_sierra_phy_reset(struct phy *gphy)
{
	struct cdns_sierra_phy *sp = dev_get_drvdata(gphy->dev.parent);

	reset_control_assert(sp->phy_rst);
	reset_control_deassert(sp->phy_rst);
	return 0;
};

static const struct phy_ops ops = {
	.init		= cdns_sierra_phy_init,
	.power_on	= cdns_sierra_phy_on,
	.power_off	= cdns_sierra_phy_off,
	.reset		= cdns_sierra_phy_reset,
	.owner		= THIS_MODULE,
};

static u8 cdns_sierra_pll_mux_get_parent(struct clk_hw *hw)
{
	struct cdns_sierra_pll_mux *mux = to_cdns_sierra_pll_mux(hw);
	struct regmap_field *plllc1en_field = mux->plllc1en_field;
	struct regmap_field *termen_field = mux->termen_field;
	struct regmap_field *field = mux->pfdclk_sel_preg;
	unsigned int val;
	int index;

	regmap_field_read(field, &val);

	if (strstr(clk_hw_get_name(hw), clk_names[CDNS_SIERRA_PLL_CMNLC1])) {
		index = clk_mux_val_to_index(hw, cdns_sierra_pll_mux_table[CMN_PLLLC1], 0, val);
		if (index == 1) {
			regmap_field_write(plllc1en_field, 1);
			regmap_field_write(termen_field, 1);
		}
	} else {
		index = clk_mux_val_to_index(hw, cdns_sierra_pll_mux_table[CMN_PLLLC], 0, val);
	}

	return index;
}

static int cdns_sierra_pll_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct cdns_sierra_pll_mux *mux = to_cdns_sierra_pll_mux(hw);
	struct regmap_field *plllc1en_field = mux->plllc1en_field;
	struct regmap_field *termen_field = mux->termen_field;
	struct regmap_field *field = mux->pfdclk_sel_preg;
	int val, ret;

	ret = regmap_field_write(plllc1en_field, 0);
	ret |= regmap_field_write(termen_field, 0);
	if (index == 1) {
		ret |= regmap_field_write(plllc1en_field, 1);
		ret |= regmap_field_write(termen_field, 1);
	}

	if (strstr(clk_hw_get_name(hw), clk_names[CDNS_SIERRA_PLL_CMNLC1]))
		val = cdns_sierra_pll_mux_table[CMN_PLLLC1][index];
	else
		val = cdns_sierra_pll_mux_table[CMN_PLLLC][index];

	ret |= regmap_field_write(field, val);

	return ret;
}

static const struct clk_ops cdns_sierra_pll_mux_ops = {
	.set_parent = cdns_sierra_pll_mux_set_parent,
	.get_parent = cdns_sierra_pll_mux_get_parent,
};

static int cdns_sierra_pll_mux_register(struct cdns_sierra_phy *sp,
					struct regmap_field *pfdclk1_sel_field,
					struct regmap_field *plllc1en_field,
					struct regmap_field *termen_field,
					int clk_index)
{
	struct cdns_sierra_pll_mux *mux;
	struct device *dev = sp->dev;
	struct clk_init_data *init;
	const char **parent_names;
	unsigned int num_parents;
	char clk_name[100];
	struct clk *clk;
	int i;

	mux = devm_kzalloc(dev, sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return -ENOMEM;

	num_parents = SIERRA_NUM_CMN_PLLC_PARENTS;
	parent_names = devm_kzalloc(dev, (sizeof(char *) * num_parents), GFP_KERNEL);
	if (!parent_names)
		return -ENOMEM;

	for (i = 0; i < num_parents; i++) {
		clk = sp->input_clks[pll_mux_parent_index[clk_index][i]];
		if (IS_ERR_OR_NULL(clk)) {
			dev_err(dev, "No parent clock for PLL mux clocks\n");
			return IS_ERR(clk) ? PTR_ERR(clk) : -ENOENT;
		}
		parent_names[i] = __clk_get_name(clk);
	}

	snprintf(clk_name, sizeof(clk_name), "%s_%s", dev_name(dev), clk_names[clk_index]);

	init = &mux->clk_data;

	init->ops = &cdns_sierra_pll_mux_ops;
	init->flags = CLK_SET_RATE_NO_REPARENT;
	init->parent_names = parent_names;
	init->num_parents = num_parents;
	init->name = clk_name;

	mux->pfdclk_sel_preg = pfdclk1_sel_field;
	mux->plllc1en_field = plllc1en_field;
	mux->termen_field = termen_field;
	mux->hw.init = init;

	clk = devm_clk_register(dev, &mux->hw);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	sp->output_clks[clk_index] = clk;

	return 0;
}

static int cdns_sierra_phy_register_pll_mux(struct cdns_sierra_phy *sp)
{
	struct regmap_field *pfdclk1_sel_field;
	struct regmap_field *plllc1en_field;
	struct regmap_field *termen_field;
	struct device *dev = sp->dev;
	int ret = 0, i, clk_index;

	clk_index = CDNS_SIERRA_PLL_CMNLC;
	for (i = 0; i < SIERRA_NUM_CMN_PLLC; i++, clk_index++) {
		pfdclk1_sel_field = sp->cmn_plllc_pfdclk1_sel_preg[i];
		plllc1en_field = sp->cmn_refrcv_refclk_plllc1en_preg[i];
		termen_field = sp->cmn_refrcv_refclk_termen_preg[i];

		ret = cdns_sierra_pll_mux_register(sp, pfdclk1_sel_field, plllc1en_field,
						   termen_field, clk_index);
		if (ret) {
			dev_err(dev, "Fail to register cmn plllc mux\n");
			return ret;
		}
	}

	return 0;
}

static int cdns_sierra_derived_refclk_enable(struct clk_hw *hw)
{
	struct cdns_sierra_derived_refclk *derived_refclk = to_cdns_sierra_derived_refclk(hw);

	regmap_field_write(derived_refclk->cmn_plllc_clk1_en_preg, 0x1);

	/* Programming to get 100Mhz clock output in ref_der_clk_out 5GHz VCO/50 = 100MHz */
	regmap_field_write(derived_refclk->cmn_plllc_clk1outdiv_preg, 0x2E);

	return 0;
}

static void cdns_sierra_derived_refclk_disable(struct clk_hw *hw)
{
	struct cdns_sierra_derived_refclk *derived_refclk = to_cdns_sierra_derived_refclk(hw);

	regmap_field_write(derived_refclk->cmn_plllc_clk1_en_preg, 0);
}

static int cdns_sierra_derived_refclk_is_enabled(struct clk_hw *hw)
{
	struct cdns_sierra_derived_refclk *derived_refclk = to_cdns_sierra_derived_refclk(hw);
	int val;

	regmap_field_read(derived_refclk->cmn_plllc_clk1_en_preg, &val);

	return !!val;
}

static const struct clk_ops cdns_sierra_derived_refclk_ops = {
	.enable = cdns_sierra_derived_refclk_enable,
	.disable = cdns_sierra_derived_refclk_disable,
	.is_enabled = cdns_sierra_derived_refclk_is_enabled,
};

static int cdns_sierra_derived_refclk_register(struct cdns_sierra_phy *sp)
{
	struct cdns_sierra_derived_refclk *derived_refclk;
	struct device *dev = sp->dev;
	struct regmap_field *field;
	struct clk_init_data *init;
	struct regmap *regmap;
	char clk_name[100];
	struct clk *clk;

	derived_refclk = devm_kzalloc(dev, sizeof(*derived_refclk), GFP_KERNEL);
	if (!derived_refclk)
		return -ENOMEM;

	snprintf(clk_name, sizeof(clk_name), "%s_%s", dev_name(dev),
		 clk_names[CDNS_SIERRA_DERIVED_REFCLK]);

	init = &derived_refclk->clk_data;

	init->ops = &cdns_sierra_derived_refclk_ops;
	init->flags = 0;
	init->name = clk_name;

	regmap = sp->regmap_common_cdb;

	field = devm_regmap_field_alloc(dev, regmap, cmn_plllc_clk1outdiv_preg);
	if (IS_ERR(field)) {
		dev_err(dev, "cmn_plllc_clk1outdiv_preg reg field init failed\n");
		return PTR_ERR(field);
	}
	derived_refclk->cmn_plllc_clk1outdiv_preg = field;

	field = devm_regmap_field_alloc(dev, regmap, cmn_plllc_clk1_en_preg);
	if (IS_ERR(field)) {
		dev_err(dev, "cmn_plllc_clk1_en_preg reg field init failed\n");
		return PTR_ERR(field);
	}
	derived_refclk->cmn_plllc_clk1_en_preg = field;

	derived_refclk->hw.init = init;

	clk = devm_clk_register(dev, &derived_refclk->hw);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	sp->output_clks[CDNS_SIERRA_DERIVED_REFCLK] = clk;

	return 0;
}

static void cdns_sierra_clk_unregister(struct cdns_sierra_phy *sp)
{
	struct device *dev = sp->dev;
	struct device_node *node = dev->of_node;

	of_clk_del_provider(node);
}

static int cdns_sierra_clk_register(struct cdns_sierra_phy *sp)
{
	struct device *dev = sp->dev;
	struct device_node *node = dev->of_node;
	int ret;

	ret = cdns_sierra_phy_register_pll_mux(sp);
	if (ret) {
		dev_err(dev, "Failed to pll mux clocks\n");
		return ret;
	}

	ret = cdns_sierra_derived_refclk_register(sp);
	if (ret) {
		dev_err(dev, "Failed to register derived refclk\n");
		return ret;
	}

	sp->clk_data.clks = sp->output_clks;
	sp->clk_data.clk_num = CDNS_SIERRA_OUTPUT_CLOCKS;
	ret = of_clk_add_provider(node, of_clk_src_onecell_get, &sp->clk_data);
	if (ret)
		dev_err(dev, "Failed to add clock provider: %s\n", node->name);

	return ret;
}

static int cdns_sierra_get_optional(struct cdns_sierra_inst *inst,
				    struct device_node *child)
{
	u32 phy_type;

	if (of_property_read_u32(child, "reg", &inst->mlane))
		return -EINVAL;

	if (of_property_read_u32(child, "cdns,num-lanes", &inst->num_lanes))
		return -EINVAL;

	if (of_property_read_u32(child, "cdns,phy-type", &phy_type))
		return -EINVAL;

	switch (phy_type) {
	case PHY_TYPE_PCIE:
		inst->phy_type = TYPE_PCIE;
		break;
	case PHY_TYPE_USB3:
		inst->phy_type = TYPE_USB;
		break;
	case PHY_TYPE_QSGMII:
		inst->phy_type = TYPE_QSGMII;
		break;
	default:
		return -EINVAL;
	}

	inst->ssc_mode = EXTERNAL_SSC;
	of_property_read_u32(child, "cdns,ssc-mode", &inst->ssc_mode);

	return 0;
}

static struct regmap *cdns_regmap_init(struct device *dev, void __iomem *base,
				       u32 block_offset, u8 reg_offset_shift,
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

static int cdns_regfield_init(struct cdns_sierra_phy *sp)
{
	struct device *dev = sp->dev;
	struct regmap_field *field;
	struct reg_field reg_field;
	struct regmap *regmap;
	int i;

	regmap = sp->regmap_common_cdb;
	field = devm_regmap_field_alloc(dev, regmap, macro_id_type);
	if (IS_ERR(field)) {
		dev_err(dev, "MACRO_ID_TYPE reg field init failed\n");
		return PTR_ERR(field);
	}
	sp->macro_id_type = field;

	for (i = 0; i < SIERRA_NUM_CMN_PLLC; i++) {
		reg_field = cmn_plllc_pfdclk1_sel_preg[i].pfdclk_sel_preg;
		field = devm_regmap_field_alloc(dev, regmap, reg_field);
		if (IS_ERR(field)) {
			dev_err(dev, "PLLLC%d_PFDCLK1_SEL failed\n", i);
			return PTR_ERR(field);
		}
		sp->cmn_plllc_pfdclk1_sel_preg[i] = field;

		reg_field = cmn_plllc_pfdclk1_sel_preg[i].plllc1en_field;
		field = devm_regmap_field_alloc(dev, regmap, reg_field);
		if (IS_ERR(field)) {
			dev_err(dev, "REFRCV%d_REFCLK_PLLLC1EN failed\n", i);
			return PTR_ERR(field);
		}
		sp->cmn_refrcv_refclk_plllc1en_preg[i] = field;

		reg_field = cmn_plllc_pfdclk1_sel_preg[i].termen_field;
		field = devm_regmap_field_alloc(dev, regmap, reg_field);
		if (IS_ERR(field)) {
			dev_err(dev, "REFRCV%d_REFCLK_TERMEN failed\n", i);
			return PTR_ERR(field);
		}
		sp->cmn_refrcv_refclk_termen_preg[i] = field;
	}

	regmap = sp->regmap_phy_pcs_common_cdb;
	field = devm_regmap_field_alloc(dev, regmap, phy_pll_cfg_1);
	if (IS_ERR(field)) {
		dev_err(dev, "PHY_PLL_CFG_1 reg field init failed\n");
		return PTR_ERR(field);
	}
	sp->phy_pll_cfg_1 = field;

	regmap = sp->regmap_phy_pma_common_cdb;
	field = devm_regmap_field_alloc(dev, regmap, pma_cmn_ready);
	if (IS_ERR(field)) {
		dev_err(dev, "PHY_PMA_CMN_CTRL reg field init failed\n");
		return PTR_ERR(field);
	}
	sp->pma_cmn_ready = field;

	for (i = 0; i < SIERRA_MAX_LANES; i++) {
		regmap = sp->regmap_lane_cdb[i];
		field = devm_regmap_field_alloc(dev, regmap, pllctrl_lock);
		if (IS_ERR(field)) {
			dev_err(dev, "P%d_ENABLE reg field init failed\n", i);
			return PTR_ERR(field);
		}
		sp->pllctrl_lock[i] = field;
	}

	for (i = 0; i < SIERRA_MAX_LANES; i++) {
		regmap = sp->regmap_phy_pcs_lane_cdb[i];
		field = devm_regmap_field_alloc(dev, regmap, phy_iso_link_ctrl_1);
		if (IS_ERR(field)) {
			dev_err(dev, "PHY_ISO_LINK_CTRL reg field init for lane %d failed\n", i);
			return PTR_ERR(field);
		}
		sp->phy_iso_link_ctrl_1[i] = field;
	}

	return 0;
}

static int cdns_regmap_init_blocks(struct cdns_sierra_phy *sp,
				   void __iomem *base, u8 block_offset_shift,
				   u8 reg_offset_shift)
{
	struct device *dev = sp->dev;
	struct regmap *regmap;
	u32 block_offset;
	int i;

	for (i = 0; i < SIERRA_MAX_LANES; i++) {
		block_offset = SIERRA_LANE_CDB_OFFSET(i, block_offset_shift,
						      reg_offset_shift);
		regmap = cdns_regmap_init(dev, base, block_offset,
					  reg_offset_shift,
					  &cdns_sierra_lane_cdb_config[i]);
		if (IS_ERR(regmap)) {
			dev_err(dev, "Failed to init lane CDB regmap\n");
			return PTR_ERR(regmap);
		}
		sp->regmap_lane_cdb[i] = regmap;
	}

	regmap = cdns_regmap_init(dev, base, SIERRA_COMMON_CDB_OFFSET,
				  reg_offset_shift,
				  &cdns_sierra_common_cdb_config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "Failed to init common CDB regmap\n");
		return PTR_ERR(regmap);
	}
	sp->regmap_common_cdb = regmap;

	block_offset = SIERRA_PHY_PCS_COMMON_OFFSET(block_offset_shift);
	regmap = cdns_regmap_init(dev, base, block_offset, reg_offset_shift,
				  &cdns_sierra_phy_pcs_cmn_cdb_config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "Failed to init PHY PCS common CDB regmap\n");
		return PTR_ERR(regmap);
	}
	sp->regmap_phy_pcs_common_cdb = regmap;

	for (i = 0; i < SIERRA_MAX_LANES; i++) {
		block_offset = SIERRA_PHY_PCS_LANE_CDB_OFFSET(i, block_offset_shift,
							      reg_offset_shift);
		regmap = cdns_regmap_init(dev, base, block_offset,
					  reg_offset_shift,
					  &cdns_sierra_phy_pcs_lane_cdb_config[i]);
		if (IS_ERR(regmap)) {
			dev_err(dev, "Failed to init PHY PCS lane CDB regmap\n");
			return PTR_ERR(regmap);
		}
		sp->regmap_phy_pcs_lane_cdb[i] = regmap;
	}

	block_offset = SIERRA_PHY_PMA_COMMON_OFFSET(block_offset_shift);
	regmap = cdns_regmap_init(dev, base, block_offset, reg_offset_shift,
				  &cdns_sierra_phy_pma_cmn_cdb_config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "Failed to init PHY PMA common CDB regmap\n");
		return PTR_ERR(regmap);
	}
	sp->regmap_phy_pma_common_cdb = regmap;

	for (i = 0; i < SIERRA_MAX_LANES; i++) {
		block_offset = SIERRA_PHY_PMA_LANE_CDB_OFFSET(i, block_offset_shift,
							      reg_offset_shift);
		regmap = cdns_regmap_init(dev, base, block_offset,
					  reg_offset_shift,
					  &cdns_sierra_phy_pma_lane_cdb_config[i]);
		if (IS_ERR(regmap)) {
			dev_err(dev, "Failed to init PHY PMA lane CDB regmap\n");
			return PTR_ERR(regmap);
		}
		sp->regmap_phy_pma_lane_cdb[i] = regmap;
	}

	return 0;
}

static int cdns_sierra_phy_get_clocks(struct cdns_sierra_phy *sp,
				      struct device *dev)
{
	struct clk *clk;
	int ret;

	clk = devm_clk_get_optional(dev, "phy_clk");
	if (IS_ERR(clk)) {
		dev_err(dev, "failed to get clock phy_clk\n");
		return PTR_ERR(clk);
	}
	sp->input_clks[PHY_CLK] = clk;

	clk = devm_clk_get_optional(dev, "cmn_refclk_dig_div");
	if (IS_ERR(clk)) {
		dev_err(dev, "cmn_refclk_dig_div clock not found\n");
		ret = PTR_ERR(clk);
		return ret;
	}
	sp->input_clks[CMN_REFCLK_DIG_DIV] = clk;

	clk = devm_clk_get_optional(dev, "cmn_refclk1_dig_div");
	if (IS_ERR(clk)) {
		dev_err(dev, "cmn_refclk1_dig_div clock not found\n");
		ret = PTR_ERR(clk);
		return ret;
	}
	sp->input_clks[CMN_REFCLK1_DIG_DIV] = clk;

	clk = devm_clk_get_optional(dev, "pll0_refclk");
	if (IS_ERR(clk)) {
		dev_err(dev, "pll0_refclk clock not found\n");
		ret = PTR_ERR(clk);
		return ret;
	}
	sp->input_clks[PLL0_REFCLK] = clk;

	clk = devm_clk_get_optional(dev, "pll1_refclk");
	if (IS_ERR(clk)) {
		dev_err(dev, "pll1_refclk clock not found\n");
		ret = PTR_ERR(clk);
		return ret;
	}
	sp->input_clks[PLL1_REFCLK] = clk;

	return 0;
}

static int cdns_sierra_phy_enable_clocks(struct cdns_sierra_phy *sp)
{
	int ret;

	ret = clk_prepare_enable(sp->input_clks[PHY_CLK]);
	if (ret)
		return ret;

	ret = clk_prepare_enable(sp->output_clks[CDNS_SIERRA_PLL_CMNLC]);
	if (ret)
		goto err_pll_cmnlc;

	ret = clk_prepare_enable(sp->output_clks[CDNS_SIERRA_PLL_CMNLC1]);
	if (ret)
		goto err_pll_cmnlc1;

	return 0;

err_pll_cmnlc1:
	clk_disable_unprepare(sp->output_clks[CDNS_SIERRA_PLL_CMNLC]);

err_pll_cmnlc:
	clk_disable_unprepare(sp->input_clks[PHY_CLK]);

	return ret;
}

static void cdns_sierra_phy_disable_clocks(struct cdns_sierra_phy *sp)
{
	clk_disable_unprepare(sp->output_clks[CDNS_SIERRA_PLL_CMNLC1]);
	clk_disable_unprepare(sp->output_clks[CDNS_SIERRA_PLL_CMNLC]);
	clk_disable_unprepare(sp->input_clks[PHY_CLK]);
}

static int cdns_sierra_phy_get_resets(struct cdns_sierra_phy *sp,
				      struct device *dev)
{
	struct reset_control *rst;

	rst = devm_reset_control_get_exclusive(dev, "sierra_reset");
	if (IS_ERR(rst)) {
		dev_err(dev, "failed to get reset\n");
		return PTR_ERR(rst);
	}
	sp->phy_rst = rst;

	rst = devm_reset_control_get_optional_exclusive(dev, "sierra_apb");
	if (IS_ERR(rst)) {
		dev_err(dev, "failed to get apb reset\n");
		return PTR_ERR(rst);
	}
	sp->apb_rst = rst;

	return 0;
}

static int cdns_sierra_phy_configure_multilink(struct cdns_sierra_phy *sp)
{
	const struct cdns_sierra_data *init_data = sp->init_data;
	struct cdns_sierra_vals *pma_cmn_vals, *pma_ln_vals;
	enum cdns_sierra_phy_type phy_t1, phy_t2;
	struct cdns_sierra_vals *phy_pma_ln_vals;
	const struct cdns_reg_pairs *reg_pairs;
	struct cdns_sierra_vals *pcs_cmn_vals;
	int i, j, node, mlane, num_lanes, ret;
	enum cdns_sierra_ssc_mode ssc;
	struct regmap *regmap;
	u32 num_regs;

	/* Maximum 2 links (subnodes) are supported */
	if (sp->nsubnodes != 2)
		return -EINVAL;

	clk_set_rate(sp->input_clks[CMN_REFCLK_DIG_DIV], 25000000);
	clk_set_rate(sp->input_clks[CMN_REFCLK1_DIG_DIV], 25000000);

	/* PHY configured to use both PLL LC and LC1 */
	regmap_field_write(sp->phy_pll_cfg_1, 0x1);

	phy_t1 = sp->phys[0].phy_type;
	phy_t2 = sp->phys[1].phy_type;

	/*
	 * PHY configuration for multi-link operation is done in two steps.
	 * e.g. Consider a case for a 4 lane PHY with PCIe using 2 lanes and QSGMII other 2 lanes.
	 * Sierra PHY has 2 PLLs, viz. PLLLC and PLLLC1. So in this case, PLLLC is used for PCIe
	 * and PLLLC1 is used for QSGMII. PHY is configured in two steps as described below.
	 *
	 * [1] For first step, phy_t1 = TYPE_PCIE and phy_t2 = TYPE_QSGMII
	 *     So the register values are selected as [TYPE_PCIE][TYPE_QSGMII][ssc].
	 *     This will configure PHY registers associated for PCIe (i.e. first protocol)
	 *     involving PLLLC registers and registers for first 2 lanes of PHY.
	 * [2] In second step, the variables phy_t1 and phy_t2 are swapped. So now,
	 *     phy_t1 = TYPE_QSGMII and phy_t2 = TYPE_PCIE. And the register values are selected as
	 *     [TYPE_QSGMII][TYPE_PCIE][ssc].
	 *     This will configure PHY registers associated for QSGMII (i.e. second protocol)
	 *     involving PLLLC1 registers and registers for other 2 lanes of PHY.
	 *
	 * This completes the PHY configuration for multilink operation. This approach enables
	 * dividing the large number of PHY register configurations into protocol specific
	 * smaller groups.
	 */
	for (node = 0; node < sp->nsubnodes; node++) {
		if (node == 1) {
			/*
			 * If first link with phy_t1 is configured, then configure the PHY for
			 * second link with phy_t2. Get the array values as [phy_t2][phy_t1][ssc].
			 */
			swap(phy_t1, phy_t2);
		}

		mlane = sp->phys[node].mlane;
		ssc = sp->phys[node].ssc_mode;
		num_lanes = sp->phys[node].num_lanes;

		/* PHY PCS common registers configurations */
		pcs_cmn_vals = init_data->pcs_cmn_vals[phy_t1][phy_t2][ssc];
		if (pcs_cmn_vals) {
			reg_pairs = pcs_cmn_vals->reg_pairs;
			num_regs = pcs_cmn_vals->num_regs;
			regmap = sp->regmap_phy_pcs_common_cdb;
			for (i = 0; i < num_regs; i++)
				regmap_write(regmap, reg_pairs[i].off, reg_pairs[i].val);
		}

		/* PHY PMA lane registers configurations */
		phy_pma_ln_vals = init_data->phy_pma_ln_vals[phy_t1][phy_t2][ssc];
		if (phy_pma_ln_vals) {
			reg_pairs = phy_pma_ln_vals->reg_pairs;
			num_regs = phy_pma_ln_vals->num_regs;
			for (i = 0; i < num_lanes; i++) {
				regmap = sp->regmap_phy_pma_lane_cdb[i + mlane];
				for (j = 0; j < num_regs; j++)
					regmap_write(regmap, reg_pairs[j].off, reg_pairs[j].val);
			}
		}

		/* PMA common registers configurations */
		pma_cmn_vals = init_data->pma_cmn_vals[phy_t1][phy_t2][ssc];
		if (pma_cmn_vals) {
			reg_pairs = pma_cmn_vals->reg_pairs;
			num_regs = pma_cmn_vals->num_regs;
			regmap = sp->regmap_common_cdb;
			for (i = 0; i < num_regs; i++)
				regmap_write(regmap, reg_pairs[i].off, reg_pairs[i].val);
		}

		/* PMA lane registers configurations */
		pma_ln_vals = init_data->pma_ln_vals[phy_t1][phy_t2][ssc];
		if (pma_ln_vals) {
			reg_pairs = pma_ln_vals->reg_pairs;
			num_regs = pma_ln_vals->num_regs;
			for (i = 0; i < num_lanes; i++) {
				regmap = sp->regmap_lane_cdb[i + mlane];
				for (j = 0; j < num_regs; j++)
					regmap_write(regmap, reg_pairs[j].off, reg_pairs[j].val);
			}
		}

		if (phy_t1 == TYPE_QSGMII)
			reset_control_deassert(sp->phys[node].lnk_rst);
	}

	/* Take the PHY out of reset */
	ret = reset_control_deassert(sp->phy_rst);
	if (ret)
		return ret;

	return 0;
}

static int cdns_sierra_phy_probe(struct platform_device *pdev)
{
	struct cdns_sierra_phy *sp;
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	const struct cdns_sierra_data *data;
	unsigned int id_value;
	int i, ret, node = 0;
	void __iomem *base;
	struct device_node *dn = dev->of_node, *child;

	if (of_get_child_count(dn) == 0)
		return -ENODEV;

	/* Get init data for this PHY */
	data = of_device_get_match_data(dev);
	if (!data)
		return -EINVAL;

	sp = devm_kzalloc(dev, sizeof(*sp), GFP_KERNEL);
	if (!sp)
		return -ENOMEM;
	dev_set_drvdata(dev, sp);
	sp->dev = dev;
	sp->init_data = data;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base)) {
		dev_err(dev, "missing \"reg\"\n");
		return PTR_ERR(base);
	}

	ret = cdns_regmap_init_blocks(sp, base, data->block_offset_shift,
				      data->reg_offset_shift);
	if (ret)
		return ret;

	ret = cdns_regfield_init(sp);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, sp);

	ret = cdns_sierra_phy_get_clocks(sp, dev);
	if (ret)
		return ret;

	ret = cdns_sierra_clk_register(sp);
	if (ret)
		return ret;

	ret = cdns_sierra_phy_get_resets(sp, dev);
	if (ret)
		goto unregister_clk;

	ret = cdns_sierra_phy_enable_clocks(sp);
	if (ret)
		goto unregister_clk;

	/* Enable APB */
	reset_control_deassert(sp->apb_rst);

	/* Check that PHY is present */
	regmap_field_read(sp->macro_id_type, &id_value);
	if  (sp->init_data->id_value != id_value) {
		ret = -EINVAL;
		goto clk_disable;
	}

	sp->autoconf = of_property_read_bool(dn, "cdns,autoconf");

	for_each_available_child_of_node(dn, child) {
		struct phy *gphy;

		if (!(of_node_name_eq(child, "phy") ||
		      of_node_name_eq(child, "link")))
			continue;

		sp->phys[node].lnk_rst =
			of_reset_control_array_get_exclusive(child);

		if (IS_ERR(sp->phys[node].lnk_rst)) {
			dev_err(dev, "failed to get reset %s\n",
				child->full_name);
			ret = PTR_ERR(sp->phys[node].lnk_rst);
			goto put_child2;
		}

		if (!sp->autoconf) {
			ret = cdns_sierra_get_optional(&sp->phys[node], child);
			if (ret) {
				dev_err(dev, "missing property in node %s\n",
					child->name);
				goto put_child;
			}
		}

		sp->num_lanes += sp->phys[node].num_lanes;

		gphy = devm_phy_create(dev, child, &ops);

		if (IS_ERR(gphy)) {
			ret = PTR_ERR(gphy);
			goto put_child;
		}
		sp->phys[node].phy = gphy;
		phy_set_drvdata(gphy, &sp->phys[node]);

		node++;
	}
	sp->nsubnodes = node;

	if (sp->num_lanes > SIERRA_MAX_LANES) {
		ret = -EINVAL;
		dev_err(dev, "Invalid lane configuration\n");
		goto put_child2;
	}

	/* If more than one subnode, configure the PHY as multilink */
	if (!sp->autoconf && sp->nsubnodes > 1) {
		ret = cdns_sierra_phy_configure_multilink(sp);
		if (ret)
			goto put_child2;
	}

	pm_runtime_enable(dev);
	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	return PTR_ERR_OR_ZERO(phy_provider);

put_child:
	node++;
put_child2:
	for (i = 0; i < node; i++)
		reset_control_put(sp->phys[i].lnk_rst);
	of_node_put(child);
clk_disable:
	cdns_sierra_phy_disable_clocks(sp);
	reset_control_assert(sp->apb_rst);
unregister_clk:
	cdns_sierra_clk_unregister(sp);
	return ret;
}

static int cdns_sierra_phy_remove(struct platform_device *pdev)
{
	struct cdns_sierra_phy *phy = platform_get_drvdata(pdev);
	int i;

	reset_control_assert(phy->phy_rst);
	reset_control_assert(phy->apb_rst);
	pm_runtime_disable(&pdev->dev);

	cdns_sierra_phy_disable_clocks(phy);
	/*
	 * The device level resets will be put automatically.
	 * Need to put the subnode resets here though.
	 */
	for (i = 0; i < phy->nsubnodes; i++) {
		reset_control_assert(phy->phys[i].lnk_rst);
		reset_control_put(phy->phys[i].lnk_rst);
	}

	cdns_sierra_clk_unregister(phy);

	return 0;
}

/* QSGMII PHY PMA lane configuration */
static struct cdns_reg_pairs qsgmii_phy_pma_ln_regs[] = {
	{0x9010, SIERRA_PHY_PMA_XCVR_CTRL}
};

static struct cdns_sierra_vals qsgmii_phy_pma_ln_vals = {
	.reg_pairs = qsgmii_phy_pma_ln_regs,
	.num_regs = ARRAY_SIZE(qsgmii_phy_pma_ln_regs),
};

/* QSGMII refclk 100MHz, 20b, opt1, No BW cal, no ssc, PLL LC1 */
static const struct cdns_reg_pairs qsgmii_100_no_ssc_plllc1_cmn_regs[] = {
	{0x2085, SIERRA_CMN_PLLLC1_LF_COEFF_MODE0_PREG},
	{0x0000, SIERRA_CMN_PLLLC1_BWCAL_MODE0_PREG},
	{0x0000, SIERRA_CMN_PLLLC1_SS_TIME_STEPSIZE_MODE_PREG}
};

static const struct cdns_reg_pairs qsgmii_100_no_ssc_plllc1_ln_regs[] = {
	{0xFC08, SIERRA_DET_STANDEC_A_PREG},
	{0x0252, SIERRA_DET_STANDEC_E_PREG},
	{0x0004, SIERRA_PSC_LN_IDLE_PREG},
	{0x0FFE, SIERRA_PSC_RX_A0_PREG},
	{0x0011, SIERRA_PLLCTRL_SUBRATE_PREG},
	{0x0001, SIERRA_PLLCTRL_GEN_A_PREG},
	{0x5233, SIERRA_PLLCTRL_CPGAIN_MODE_PREG},
	{0x0000, SIERRA_DRVCTRL_ATTEN_PREG},
	{0x0089, SIERRA_RX_CREQ_FLTR_A_MODE0_PREG},
	{0x3C3C, SIERRA_CREQ_CCLKDET_MODE01_PREG},
	{0x3222, SIERRA_CREQ_FSMCLK_SEL_PREG},
	{0x0000, SIERRA_CREQ_EQ_CTRL_PREG},
	{0x8422, SIERRA_CTLELUT_CTRL_PREG},
	{0x4111, SIERRA_DFE_ECMP_RATESEL_PREG},
	{0x4111, SIERRA_DFE_SMP_RATESEL_PREG},
	{0x0002, SIERRA_DEQ_PHALIGN_CTRL},
	{0x9595, SIERRA_DEQ_VGATUNE_CTRL_PREG},
	{0x0186, SIERRA_DEQ_GLUT0},
	{0x0186, SIERRA_DEQ_GLUT1},
	{0x0186, SIERRA_DEQ_GLUT2},
	{0x0186, SIERRA_DEQ_GLUT3},
	{0x0186, SIERRA_DEQ_GLUT4},
	{0x0861, SIERRA_DEQ_ALUT0},
	{0x07E0, SIERRA_DEQ_ALUT1},
	{0x079E, SIERRA_DEQ_ALUT2},
	{0x071D, SIERRA_DEQ_ALUT3},
	{0x03F5, SIERRA_DEQ_DFETAP_CTRL_PREG},
	{0x0C01, SIERRA_DEQ_TAU_CTRL1_FAST_MAINT_PREG},
	{0x3C40, SIERRA_DEQ_TAU_CTRL1_SLOW_MAINT_PREG},
	{0x1C04, SIERRA_DEQ_TAU_CTRL2_PREG},
	{0x0033, SIERRA_DEQ_PICTRL_PREG},
	{0x0660, SIERRA_CPICAL_TMRVAL_MODE0_PREG},
	{0x00D5, SIERRA_CPI_OUTBUF_RATESEL_PREG},
	{0x0B6D, SIERRA_CPI_RESBIAS_BIN_PREG},
	{0x0102, SIERRA_RXBUFFER_CTLECTRL_PREG},
	{0x0002, SIERRA_RXBUFFER_RCDFECTRL_PREG}
};

static struct cdns_sierra_vals qsgmii_100_no_ssc_plllc1_cmn_vals = {
	.reg_pairs = qsgmii_100_no_ssc_plllc1_cmn_regs,
	.num_regs = ARRAY_SIZE(qsgmii_100_no_ssc_plllc1_cmn_regs),
};

static struct cdns_sierra_vals qsgmii_100_no_ssc_plllc1_ln_vals = {
	.reg_pairs = qsgmii_100_no_ssc_plllc1_ln_regs,
	.num_regs = ARRAY_SIZE(qsgmii_100_no_ssc_plllc1_ln_regs),
};

/* PCIE PHY PCS common configuration */
static struct cdns_reg_pairs pcie_phy_pcs_cmn_regs[] = {
	{0x0430, SIERRA_PHY_PIPE_CMN_CTRL1}
};

static struct cdns_sierra_vals pcie_phy_pcs_cmn_vals = {
	.reg_pairs = pcie_phy_pcs_cmn_regs,
	.num_regs = ARRAY_SIZE(pcie_phy_pcs_cmn_regs),
};

/* refclk100MHz_32b_PCIe_cmn_pll_no_ssc, pcie_links_using_plllc, pipe_bw_3 */
static const struct cdns_reg_pairs pcie_100_no_ssc_plllc_cmn_regs[] = {
	{0x2105, SIERRA_CMN_PLLLC_LF_COEFF_MODE1_PREG},
	{0x2105, SIERRA_CMN_PLLLC_LF_COEFF_MODE0_PREG},
	{0x8A06, SIERRA_CMN_PLLLC_BWCAL_MODE1_PREG},
	{0x8A06, SIERRA_CMN_PLLLC_BWCAL_MODE0_PREG}
};

/*
 * refclk100MHz_32b_PCIe_ln_no_ssc, multilink, using_plllc,
 * cmn_pllcy_anaclk0_1Ghz, xcvr_pllclk_fullrt_500mhz
 */
static const struct cdns_reg_pairs ml_pcie_100_no_ssc_ln_regs[] = {
	{0xFC08, SIERRA_DET_STANDEC_A_PREG},
	{0x001D, SIERRA_PSM_A3IN_TMR_PREG},
	{0x0004, SIERRA_PSC_LN_A3_PREG},
	{0x0004, SIERRA_PSC_LN_A4_PREG},
	{0x0004, SIERRA_PSC_LN_IDLE_PREG},
	{0x1555, SIERRA_DFE_BIASTRIM_PREG},
	{0x9703, SIERRA_DRVCTRL_BOOST_PREG},
	{0x8055, SIERRA_RX_CREQ_FLTR_A_MODE3_PREG},
	{0x80BB, SIERRA_RX_CREQ_FLTR_A_MODE2_PREG},
	{0x8351, SIERRA_RX_CREQ_FLTR_A_MODE1_PREG},
	{0x8349, SIERRA_RX_CREQ_FLTR_A_MODE0_PREG},
	{0x0002, SIERRA_CREQ_DCBIASATTEN_OVR_PREG},
	{0x9800, SIERRA_RX_CTLE_CAL_PREG},
	{0x5624, SIERRA_DEQ_CONCUR_CTRL2_PREG},
	{0x000F, SIERRA_DEQ_EPIPWR_CTRL2_PREG},
	{0x00FF, SIERRA_DEQ_FAST_MAINT_CYCLES_PREG},
	{0x4C4C, SIERRA_DEQ_ERRCMP_CTRL_PREG},
	{0x02FA, SIERRA_DEQ_OFFSET_CTRL_PREG},
	{0x02FA, SIERRA_DEQ_GAIN_CTRL_PREG},
	{0x0041, SIERRA_DEQ_GLUT0},
	{0x0082, SIERRA_DEQ_GLUT1},
	{0x00C3, SIERRA_DEQ_GLUT2},
	{0x0145, SIERRA_DEQ_GLUT3},
	{0x0186, SIERRA_DEQ_GLUT4},
	{0x09E7, SIERRA_DEQ_ALUT0},
	{0x09A6, SIERRA_DEQ_ALUT1},
	{0x0965, SIERRA_DEQ_ALUT2},
	{0x08E3, SIERRA_DEQ_ALUT3},
	{0x00FA, SIERRA_DEQ_DFETAP0},
	{0x00FA, SIERRA_DEQ_DFETAP1},
	{0x00FA, SIERRA_DEQ_DFETAP2},
	{0x00FA, SIERRA_DEQ_DFETAP3},
	{0x00FA, SIERRA_DEQ_DFETAP4},
	{0x000F, SIERRA_DEQ_PRECUR_PREG},
	{0x0280, SIERRA_DEQ_POSTCUR_PREG},
	{0x8F00, SIERRA_DEQ_POSTCUR_DECR_PREG},
	{0x3C0F, SIERRA_DEQ_TAU_CTRL1_SLOW_MAINT_PREG},
	{0x1C0C, SIERRA_DEQ_TAU_CTRL2_PREG},
	{0x0100, SIERRA_DEQ_TAU_CTRL3_PREG},
	{0x5E82, SIERRA_DEQ_OPENEYE_CTRL_PREG},
	{0x002B, SIERRA_CPI_TRIM_PREG},
	{0x0003, SIERRA_EPI_CTRL_PREG},
	{0x803F, SIERRA_SDFILT_H2L_A_PREG},
	{0x0004, SIERRA_RXBUFFER_CTLECTRL_PREG},
	{0x2010, SIERRA_RXBUFFER_RCDFECTRL_PREG},
	{0x4432, SIERRA_RXBUFFER_DFECTRL_PREG}
};

static struct cdns_sierra_vals pcie_100_no_ssc_plllc_cmn_vals = {
	.reg_pairs = pcie_100_no_ssc_plllc_cmn_regs,
	.num_regs = ARRAY_SIZE(pcie_100_no_ssc_plllc_cmn_regs),
};

static struct cdns_sierra_vals ml_pcie_100_no_ssc_ln_vals = {
	.reg_pairs = ml_pcie_100_no_ssc_ln_regs,
	.num_regs = ARRAY_SIZE(ml_pcie_100_no_ssc_ln_regs),
};

/* refclk100MHz_32b_PCIe_cmn_pll_int_ssc, pcie_links_using_plllc, pipe_bw_3 */
static const struct cdns_reg_pairs pcie_100_int_ssc_plllc_cmn_regs[] = {
	{0x000E, SIERRA_CMN_PLLLC_MODE_PREG},
	{0x4006, SIERRA_CMN_PLLLC_LF_COEFF_MODE1_PREG},
	{0x4006, SIERRA_CMN_PLLLC_LF_COEFF_MODE0_PREG},
	{0x0000, SIERRA_CMN_PLLLC_BWCAL_MODE1_PREG},
	{0x0000, SIERRA_CMN_PLLLC_BWCAL_MODE0_PREG},
	{0x0581, SIERRA_CMN_PLLLC_DSMCORR_PREG},
	{0x7F80, SIERRA_CMN_PLLLC_SS_PREG},
	{0x0041, SIERRA_CMN_PLLLC_SS_AMP_STEP_SIZE_PREG},
	{0x0464, SIERRA_CMN_PLLLC_SSTWOPT_PREG},
	{0x0D0D, SIERRA_CMN_PLLLC_SS_TIME_STEPSIZE_MODE_PREG},
	{0x0060, SIERRA_CMN_PLLLC_LOCK_DELAY_CTRL_PREG}
};

/*
 * refclk100MHz_32b_PCIe_ln_int_ssc, multilink, using_plllc,
 * cmn_pllcy_anaclk0_1Ghz, xcvr_pllclk_fullrt_500mhz
 */
static const struct cdns_reg_pairs ml_pcie_100_int_ssc_ln_regs[] = {
	{0xFC08, SIERRA_DET_STANDEC_A_PREG},
	{0x001D, SIERRA_PSM_A3IN_TMR_PREG},
	{0x0004, SIERRA_PSC_LN_A3_PREG},
	{0x0004, SIERRA_PSC_LN_A4_PREG},
	{0x0004, SIERRA_PSC_LN_IDLE_PREG},
	{0x1555, SIERRA_DFE_BIASTRIM_PREG},
	{0x9703, SIERRA_DRVCTRL_BOOST_PREG},
	{0x813E, SIERRA_CLKPATHCTRL_TMR_PREG},
	{0x8047, SIERRA_RX_CREQ_FLTR_A_MODE3_PREG},
	{0x808F, SIERRA_RX_CREQ_FLTR_A_MODE2_PREG},
	{0x808F, SIERRA_RX_CREQ_FLTR_A_MODE1_PREG},
	{0x808F, SIERRA_RX_CREQ_FLTR_A_MODE0_PREG},
	{0x0002, SIERRA_CREQ_DCBIASATTEN_OVR_PREG},
	{0x9800, SIERRA_RX_CTLE_CAL_PREG},
	{0x033C, SIERRA_RX_CTLE_MAINTENANCE_PREG},
	{0x44CC, SIERRA_CREQ_EQ_OPEN_EYE_THRESH_PREG},
	{0x5624, SIERRA_DEQ_CONCUR_CTRL2_PREG},
	{0x000F, SIERRA_DEQ_EPIPWR_CTRL2_PREG},
	{0x00FF, SIERRA_DEQ_FAST_MAINT_CYCLES_PREG},
	{0x4C4C, SIERRA_DEQ_ERRCMP_CTRL_PREG},
	{0x02FA, SIERRA_DEQ_OFFSET_CTRL_PREG},
	{0x02FA, SIERRA_DEQ_GAIN_CTRL_PREG},
	{0x0041, SIERRA_DEQ_GLUT0},
	{0x0082, SIERRA_DEQ_GLUT1},
	{0x00C3, SIERRA_DEQ_GLUT2},
	{0x0145, SIERRA_DEQ_GLUT3},
	{0x0186, SIERRA_DEQ_GLUT4},
	{0x09E7, SIERRA_DEQ_ALUT0},
	{0x09A6, SIERRA_DEQ_ALUT1},
	{0x0965, SIERRA_DEQ_ALUT2},
	{0x08E3, SIERRA_DEQ_ALUT3},
	{0x00FA, SIERRA_DEQ_DFETAP0},
	{0x00FA, SIERRA_DEQ_DFETAP1},
	{0x00FA, SIERRA_DEQ_DFETAP2},
	{0x00FA, SIERRA_DEQ_DFETAP3},
	{0x00FA, SIERRA_DEQ_DFETAP4},
	{0x000F, SIERRA_DEQ_PRECUR_PREG},
	{0x0280, SIERRA_DEQ_POSTCUR_PREG},
	{0x8F00, SIERRA_DEQ_POSTCUR_DECR_PREG},
	{0x3C0F, SIERRA_DEQ_TAU_CTRL1_SLOW_MAINT_PREG},
	{0x1C0C, SIERRA_DEQ_TAU_CTRL2_PREG},
	{0x0100, SIERRA_DEQ_TAU_CTRL3_PREG},
	{0x5E82, SIERRA_DEQ_OPENEYE_CTRL_PREG},
	{0x002B, SIERRA_CPI_TRIM_PREG},
	{0x0003, SIERRA_EPI_CTRL_PREG},
	{0x803F, SIERRA_SDFILT_H2L_A_PREG},
	{0x0004, SIERRA_RXBUFFER_CTLECTRL_PREG},
	{0x2010, SIERRA_RXBUFFER_RCDFECTRL_PREG},
	{0x4432, SIERRA_RXBUFFER_DFECTRL_PREG}
};

static struct cdns_sierra_vals pcie_100_int_ssc_plllc_cmn_vals = {
	.reg_pairs = pcie_100_int_ssc_plllc_cmn_regs,
	.num_regs = ARRAY_SIZE(pcie_100_int_ssc_plllc_cmn_regs),
};

static struct cdns_sierra_vals ml_pcie_100_int_ssc_ln_vals = {
	.reg_pairs = ml_pcie_100_int_ssc_ln_regs,
	.num_regs = ARRAY_SIZE(ml_pcie_100_int_ssc_ln_regs),
};

/* refclk100MHz_32b_PCIe_cmn_pll_ext_ssc, pcie_links_using_plllc, pipe_bw_3 */
static const struct cdns_reg_pairs pcie_100_ext_ssc_plllc_cmn_regs[] = {
	{0x2106, SIERRA_CMN_PLLLC_LF_COEFF_MODE1_PREG},
	{0x2106, SIERRA_CMN_PLLLC_LF_COEFF_MODE0_PREG},
	{0x8A06, SIERRA_CMN_PLLLC_BWCAL_MODE1_PREG},
	{0x8A06, SIERRA_CMN_PLLLC_BWCAL_MODE0_PREG},
	{0x1B1B, SIERRA_CMN_PLLLC_SS_TIME_STEPSIZE_MODE_PREG}
};

/*
 * refclk100MHz_32b_PCIe_ln_ext_ssc, multilink, using_plllc,
 * cmn_pllcy_anaclk0_1Ghz, xcvr_pllclk_fullrt_500mhz
 */
static const struct cdns_reg_pairs ml_pcie_100_ext_ssc_ln_regs[] = {
	{0xFC08, SIERRA_DET_STANDEC_A_PREG},
	{0x001D, SIERRA_PSM_A3IN_TMR_PREG},
	{0x0004, SIERRA_PSC_LN_A3_PREG},
	{0x0004, SIERRA_PSC_LN_A4_PREG},
	{0x0004, SIERRA_PSC_LN_IDLE_PREG},
	{0x1555, SIERRA_DFE_BIASTRIM_PREG},
	{0x9703, SIERRA_DRVCTRL_BOOST_PREG},
	{0x813E, SIERRA_CLKPATHCTRL_TMR_PREG},
	{0x8047, SIERRA_RX_CREQ_FLTR_A_MODE3_PREG},
	{0x808F, SIERRA_RX_CREQ_FLTR_A_MODE2_PREG},
	{0x808F, SIERRA_RX_CREQ_FLTR_A_MODE1_PREG},
	{0x808F, SIERRA_RX_CREQ_FLTR_A_MODE0_PREG},
	{0x0002, SIERRA_CREQ_DCBIASATTEN_OVR_PREG},
	{0x9800, SIERRA_RX_CTLE_CAL_PREG},
	{0x033C, SIERRA_RX_CTLE_MAINTENANCE_PREG},
	{0x44CC, SIERRA_CREQ_EQ_OPEN_EYE_THRESH_PREG},
	{0x5624, SIERRA_DEQ_CONCUR_CTRL2_PREG},
	{0x000F, SIERRA_DEQ_EPIPWR_CTRL2_PREG},
	{0x00FF, SIERRA_DEQ_FAST_MAINT_CYCLES_PREG},
	{0x4C4C, SIERRA_DEQ_ERRCMP_CTRL_PREG},
	{0x02FA, SIERRA_DEQ_OFFSET_CTRL_PREG},
	{0x02FA, SIERRA_DEQ_GAIN_CTRL_PREG},
	{0x0041, SIERRA_DEQ_GLUT0},
	{0x0082, SIERRA_DEQ_GLUT1},
	{0x00C3, SIERRA_DEQ_GLUT2},
	{0x0145, SIERRA_DEQ_GLUT3},
	{0x0186, SIERRA_DEQ_GLUT4},
	{0x09E7, SIERRA_DEQ_ALUT0},
	{0x09A6, SIERRA_DEQ_ALUT1},
	{0x0965, SIERRA_DEQ_ALUT2},
	{0x08E3, SIERRA_DEQ_ALUT3},
	{0x00FA, SIERRA_DEQ_DFETAP0},
	{0x00FA, SIERRA_DEQ_DFETAP1},
	{0x00FA, SIERRA_DEQ_DFETAP2},
	{0x00FA, SIERRA_DEQ_DFETAP3},
	{0x00FA, SIERRA_DEQ_DFETAP4},
	{0x000F, SIERRA_DEQ_PRECUR_PREG},
	{0x0280, SIERRA_DEQ_POSTCUR_PREG},
	{0x8F00, SIERRA_DEQ_POSTCUR_DECR_PREG},
	{0x3C0F, SIERRA_DEQ_TAU_CTRL1_SLOW_MAINT_PREG},
	{0x1C0C, SIERRA_DEQ_TAU_CTRL2_PREG},
	{0x0100, SIERRA_DEQ_TAU_CTRL3_PREG},
	{0x5E82, SIERRA_DEQ_OPENEYE_CTRL_PREG},
	{0x002B, SIERRA_CPI_TRIM_PREG},
	{0x0003, SIERRA_EPI_CTRL_PREG},
	{0x803F, SIERRA_SDFILT_H2L_A_PREG},
	{0x0004, SIERRA_RXBUFFER_CTLECTRL_PREG},
	{0x2010, SIERRA_RXBUFFER_RCDFECTRL_PREG},
	{0x4432, SIERRA_RXBUFFER_DFECTRL_PREG}
};

static struct cdns_sierra_vals pcie_100_ext_ssc_plllc_cmn_vals = {
	.reg_pairs = pcie_100_ext_ssc_plllc_cmn_regs,
	.num_regs = ARRAY_SIZE(pcie_100_ext_ssc_plllc_cmn_regs),
};

static struct cdns_sierra_vals ml_pcie_100_ext_ssc_ln_vals = {
	.reg_pairs = ml_pcie_100_ext_ssc_ln_regs,
	.num_regs = ARRAY_SIZE(ml_pcie_100_ext_ssc_ln_regs),
};

/* refclk100MHz_32b_PCIe_cmn_pll_no_ssc */
static const struct cdns_reg_pairs cdns_pcie_cmn_regs_no_ssc[] = {
	{0x2105, SIERRA_CMN_PLLLC_LF_COEFF_MODE1_PREG},
	{0x2105, SIERRA_CMN_PLLLC_LF_COEFF_MODE0_PREG},
	{0x8A06, SIERRA_CMN_PLLLC_BWCAL_MODE1_PREG},
	{0x8A06, SIERRA_CMN_PLLLC_BWCAL_MODE0_PREG}
};

/* refclk100MHz_32b_PCIe_ln_no_ssc */
static const struct cdns_reg_pairs cdns_pcie_ln_regs_no_ssc[] = {
	{0xFC08, SIERRA_DET_STANDEC_A_PREG},
	{0x001D, SIERRA_PSM_A3IN_TMR_PREG},
	{0x1555, SIERRA_DFE_BIASTRIM_PREG},
	{0x9703, SIERRA_DRVCTRL_BOOST_PREG},
	{0x8055, SIERRA_RX_CREQ_FLTR_A_MODE3_PREG},
	{0x80BB, SIERRA_RX_CREQ_FLTR_A_MODE2_PREG},
	{0x8351, SIERRA_RX_CREQ_FLTR_A_MODE1_PREG},
	{0x8349, SIERRA_RX_CREQ_FLTR_A_MODE0_PREG},
	{0x0002, SIERRA_CREQ_DCBIASATTEN_OVR_PREG},
	{0x9800, SIERRA_RX_CTLE_CAL_PREG},
	{0x5624, SIERRA_DEQ_CONCUR_CTRL2_PREG},
	{0x000F, SIERRA_DEQ_EPIPWR_CTRL2_PREG},
	{0x00FF, SIERRA_DEQ_FAST_MAINT_CYCLES_PREG},
	{0x4C4C, SIERRA_DEQ_ERRCMP_CTRL_PREG},
	{0x02FA, SIERRA_DEQ_OFFSET_CTRL_PREG},
	{0x02FA, SIERRA_DEQ_GAIN_CTRL_PREG},
	{0x0041, SIERRA_DEQ_GLUT0},
	{0x0082, SIERRA_DEQ_GLUT1},
	{0x00C3, SIERRA_DEQ_GLUT2},
	{0x0145, SIERRA_DEQ_GLUT3},
	{0x0186, SIERRA_DEQ_GLUT4},
	{0x09E7, SIERRA_DEQ_ALUT0},
	{0x09A6, SIERRA_DEQ_ALUT1},
	{0x0965, SIERRA_DEQ_ALUT2},
	{0x08E3, SIERRA_DEQ_ALUT3},
	{0x00FA, SIERRA_DEQ_DFETAP0},
	{0x00FA, SIERRA_DEQ_DFETAP1},
	{0x00FA, SIERRA_DEQ_DFETAP2},
	{0x00FA, SIERRA_DEQ_DFETAP3},
	{0x00FA, SIERRA_DEQ_DFETAP4},
	{0x000F, SIERRA_DEQ_PRECUR_PREG},
	{0x0280, SIERRA_DEQ_POSTCUR_PREG},
	{0x8F00, SIERRA_DEQ_POSTCUR_DECR_PREG},
	{0x3C0F, SIERRA_DEQ_TAU_CTRL1_SLOW_MAINT_PREG},
	{0x1C0C, SIERRA_DEQ_TAU_CTRL2_PREG},
	{0x0100, SIERRA_DEQ_TAU_CTRL3_PREG},
	{0x5E82, SIERRA_DEQ_OPENEYE_CTRL_PREG},
	{0x002B, SIERRA_CPI_TRIM_PREG},
	{0x0003, SIERRA_EPI_CTRL_PREG},
	{0x803F, SIERRA_SDFILT_H2L_A_PREG},
	{0x0004, SIERRA_RXBUFFER_CTLECTRL_PREG},
	{0x2010, SIERRA_RXBUFFER_RCDFECTRL_PREG},
	{0x4432, SIERRA_RXBUFFER_DFECTRL_PREG}
};

static struct cdns_sierra_vals pcie_100_no_ssc_cmn_vals = {
	.reg_pairs = cdns_pcie_cmn_regs_no_ssc,
	.num_regs = ARRAY_SIZE(cdns_pcie_cmn_regs_no_ssc),
};

static struct cdns_sierra_vals pcie_100_no_ssc_ln_vals = {
	.reg_pairs = cdns_pcie_ln_regs_no_ssc,
	.num_regs = ARRAY_SIZE(cdns_pcie_ln_regs_no_ssc),
};

/* refclk100MHz_32b_PCIe_cmn_pll_int_ssc */
static const struct cdns_reg_pairs cdns_pcie_cmn_regs_int_ssc[] = {
	{0x000E, SIERRA_CMN_PLLLC_MODE_PREG},
	{0x4006, SIERRA_CMN_PLLLC_LF_COEFF_MODE1_PREG},
	{0x4006, SIERRA_CMN_PLLLC_LF_COEFF_MODE0_PREG},
	{0x0000, SIERRA_CMN_PLLLC_BWCAL_MODE1_PREG},
	{0x0000, SIERRA_CMN_PLLLC_BWCAL_MODE0_PREG},
	{0x0581, SIERRA_CMN_PLLLC_DSMCORR_PREG},
	{0x7F80, SIERRA_CMN_PLLLC_SS_PREG},
	{0x0041, SIERRA_CMN_PLLLC_SS_AMP_STEP_SIZE_PREG},
	{0x0464, SIERRA_CMN_PLLLC_SSTWOPT_PREG},
	{0x0D0D, SIERRA_CMN_PLLLC_SS_TIME_STEPSIZE_MODE_PREG},
	{0x0060, SIERRA_CMN_PLLLC_LOCK_DELAY_CTRL_PREG}
};

/* refclk100MHz_32b_PCIe_ln_int_ssc */
static const struct cdns_reg_pairs cdns_pcie_ln_regs_int_ssc[] = {
	{0xFC08, SIERRA_DET_STANDEC_A_PREG},
	{0x001D, SIERRA_PSM_A3IN_TMR_PREG},
	{0x1555, SIERRA_DFE_BIASTRIM_PREG},
	{0x9703, SIERRA_DRVCTRL_BOOST_PREG},
	{0x813E, SIERRA_CLKPATHCTRL_TMR_PREG},
	{0x8047, SIERRA_RX_CREQ_FLTR_A_MODE3_PREG},
	{0x808F, SIERRA_RX_CREQ_FLTR_A_MODE2_PREG},
	{0x808F, SIERRA_RX_CREQ_FLTR_A_MODE1_PREG},
	{0x808F, SIERRA_RX_CREQ_FLTR_A_MODE0_PREG},
	{0x0002, SIERRA_CREQ_DCBIASATTEN_OVR_PREG},
	{0x9800, SIERRA_RX_CTLE_CAL_PREG},
	{0x033C, SIERRA_RX_CTLE_MAINTENANCE_PREG},
	{0x44CC, SIERRA_CREQ_EQ_OPEN_EYE_THRESH_PREG},
	{0x5624, SIERRA_DEQ_CONCUR_CTRL2_PREG},
	{0x000F, SIERRA_DEQ_EPIPWR_CTRL2_PREG},
	{0x00FF, SIERRA_DEQ_FAST_MAINT_CYCLES_PREG},
	{0x4C4C, SIERRA_DEQ_ERRCMP_CTRL_PREG},
	{0x02FA, SIERRA_DEQ_OFFSET_CTRL_PREG},
	{0x02FA, SIERRA_DEQ_GAIN_CTRL_PREG},
	{0x0041, SIERRA_DEQ_GLUT0},
	{0x0082, SIERRA_DEQ_GLUT1},
	{0x00C3, SIERRA_DEQ_GLUT2},
	{0x0145, SIERRA_DEQ_GLUT3},
	{0x0186, SIERRA_DEQ_GLUT4},
	{0x09E7, SIERRA_DEQ_ALUT0},
	{0x09A6, SIERRA_DEQ_ALUT1},
	{0x0965, SIERRA_DEQ_ALUT2},
	{0x08E3, SIERRA_DEQ_ALUT3},
	{0x00FA, SIERRA_DEQ_DFETAP0},
	{0x00FA, SIERRA_DEQ_DFETAP1},
	{0x00FA, SIERRA_DEQ_DFETAP2},
	{0x00FA, SIERRA_DEQ_DFETAP3},
	{0x00FA, SIERRA_DEQ_DFETAP4},
	{0x000F, SIERRA_DEQ_PRECUR_PREG},
	{0x0280, SIERRA_DEQ_POSTCUR_PREG},
	{0x8F00, SIERRA_DEQ_POSTCUR_DECR_PREG},
	{0x3C0F, SIERRA_DEQ_TAU_CTRL1_SLOW_MAINT_PREG},
	{0x1C0C, SIERRA_DEQ_TAU_CTRL2_PREG},
	{0x0100, SIERRA_DEQ_TAU_CTRL3_PREG},
	{0x5E82, SIERRA_DEQ_OPENEYE_CTRL_PREG},
	{0x002B, SIERRA_CPI_TRIM_PREG},
	{0x0003, SIERRA_EPI_CTRL_PREG},
	{0x803F, SIERRA_SDFILT_H2L_A_PREG},
	{0x0004, SIERRA_RXBUFFER_CTLECTRL_PREG},
	{0x2010, SIERRA_RXBUFFER_RCDFECTRL_PREG},
	{0x4432, SIERRA_RXBUFFER_DFECTRL_PREG}
};

static struct cdns_sierra_vals pcie_100_int_ssc_cmn_vals = {
	.reg_pairs = cdns_pcie_cmn_regs_int_ssc,
	.num_regs = ARRAY_SIZE(cdns_pcie_cmn_regs_int_ssc),
};

static struct cdns_sierra_vals pcie_100_int_ssc_ln_vals = {
	.reg_pairs = cdns_pcie_ln_regs_int_ssc,
	.num_regs = ARRAY_SIZE(cdns_pcie_ln_regs_int_ssc),
};

/* refclk100MHz_32b_PCIe_cmn_pll_ext_ssc */
static const struct cdns_reg_pairs cdns_pcie_cmn_regs_ext_ssc[] = {
	{0x2106, SIERRA_CMN_PLLLC_LF_COEFF_MODE1_PREG},
	{0x2106, SIERRA_CMN_PLLLC_LF_COEFF_MODE0_PREG},
	{0x8A06, SIERRA_CMN_PLLLC_BWCAL_MODE1_PREG},
	{0x8A06, SIERRA_CMN_PLLLC_BWCAL_MODE0_PREG},
	{0x1B1B, SIERRA_CMN_PLLLC_SS_TIME_STEPSIZE_MODE_PREG}
};

/* refclk100MHz_32b_PCIe_ln_ext_ssc */
static const struct cdns_reg_pairs cdns_pcie_ln_regs_ext_ssc[] = {
	{0xFC08, SIERRA_DET_STANDEC_A_PREG},
	{0x001D, SIERRA_PSM_A3IN_TMR_PREG},
	{0x1555, SIERRA_DFE_BIASTRIM_PREG},
	{0x9703, SIERRA_DRVCTRL_BOOST_PREG},
	{0x813E, SIERRA_CLKPATHCTRL_TMR_PREG},
	{0x8047, SIERRA_RX_CREQ_FLTR_A_MODE3_PREG},
	{0x808F, SIERRA_RX_CREQ_FLTR_A_MODE2_PREG},
	{0x808F, SIERRA_RX_CREQ_FLTR_A_MODE1_PREG},
	{0x808F, SIERRA_RX_CREQ_FLTR_A_MODE0_PREG},
	{0x0002, SIERRA_CREQ_DCBIASATTEN_OVR_PREG},
	{0x9800, SIERRA_RX_CTLE_CAL_PREG},
	{0x033C, SIERRA_RX_CTLE_MAINTENANCE_PREG},
	{0x44CC, SIERRA_CREQ_EQ_OPEN_EYE_THRESH_PREG},
	{0x5624, SIERRA_DEQ_CONCUR_CTRL2_PREG},
	{0x000F, SIERRA_DEQ_EPIPWR_CTRL2_PREG},
	{0x00FF, SIERRA_DEQ_FAST_MAINT_CYCLES_PREG},
	{0x4C4C, SIERRA_DEQ_ERRCMP_CTRL_PREG},
	{0x02FA, SIERRA_DEQ_OFFSET_CTRL_PREG},
	{0x02FA, SIERRA_DEQ_GAIN_CTRL_PREG},
	{0x0041, SIERRA_DEQ_GLUT0},
	{0x0082, SIERRA_DEQ_GLUT1},
	{0x00C3, SIERRA_DEQ_GLUT2},
	{0x0145, SIERRA_DEQ_GLUT3},
	{0x0186, SIERRA_DEQ_GLUT4},
	{0x09E7, SIERRA_DEQ_ALUT0},
	{0x09A6, SIERRA_DEQ_ALUT1},
	{0x0965, SIERRA_DEQ_ALUT2},
	{0x08E3, SIERRA_DEQ_ALUT3},
	{0x00FA, SIERRA_DEQ_DFETAP0},
	{0x00FA, SIERRA_DEQ_DFETAP1},
	{0x00FA, SIERRA_DEQ_DFETAP2},
	{0x00FA, SIERRA_DEQ_DFETAP3},
	{0x00FA, SIERRA_DEQ_DFETAP4},
	{0x000F, SIERRA_DEQ_PRECUR_PREG},
	{0x0280, SIERRA_DEQ_POSTCUR_PREG},
	{0x8F00, SIERRA_DEQ_POSTCUR_DECR_PREG},
	{0x3C0F, SIERRA_DEQ_TAU_CTRL1_SLOW_MAINT_PREG},
	{0x1C0C, SIERRA_DEQ_TAU_CTRL2_PREG},
	{0x0100, SIERRA_DEQ_TAU_CTRL3_PREG},
	{0x5E82, SIERRA_DEQ_OPENEYE_CTRL_PREG},
	{0x002B, SIERRA_CPI_TRIM_PREG},
	{0x0003, SIERRA_EPI_CTRL_PREG},
	{0x803F, SIERRA_SDFILT_H2L_A_PREG},
	{0x0004, SIERRA_RXBUFFER_CTLECTRL_PREG},
	{0x2010, SIERRA_RXBUFFER_RCDFECTRL_PREG},
	{0x4432, SIERRA_RXBUFFER_DFECTRL_PREG}
};

static struct cdns_sierra_vals pcie_100_ext_ssc_cmn_vals = {
	.reg_pairs = cdns_pcie_cmn_regs_ext_ssc,
	.num_regs = ARRAY_SIZE(cdns_pcie_cmn_regs_ext_ssc),
};

static struct cdns_sierra_vals pcie_100_ext_ssc_ln_vals = {
	.reg_pairs = cdns_pcie_ln_regs_ext_ssc,
	.num_regs = ARRAY_SIZE(cdns_pcie_ln_regs_ext_ssc),
};

/* refclk100MHz_20b_USB_cmn_pll_ext_ssc */
static const struct cdns_reg_pairs cdns_usb_cmn_regs_ext_ssc[] = {
	{0x2085, SIERRA_CMN_PLLLC_LF_COEFF_MODE1_PREG},
	{0x2085, SIERRA_CMN_PLLLC_LF_COEFF_MODE0_PREG},
	{0x0000, SIERRA_CMN_PLLLC_BWCAL_MODE0_PREG},
	{0x0000, SIERRA_CMN_PLLLC_SS_TIME_STEPSIZE_MODE_PREG}
};

/* refclk100MHz_20b_USB_ln_ext_ssc */
static const struct cdns_reg_pairs cdns_usb_ln_regs_ext_ssc[] = {
	{0xFE0A, SIERRA_DET_STANDEC_A_PREG},
	{0x000F, SIERRA_DET_STANDEC_B_PREG},
	{0x55A5, SIERRA_DET_STANDEC_C_PREG},
	{0x69ad, SIERRA_DET_STANDEC_D_PREG},
	{0x0241, SIERRA_DET_STANDEC_E_PREG},
	{0x0110, SIERRA_PSM_LANECAL_DLY_A1_RESETS_PREG},
	{0x0014, SIERRA_PSM_A0IN_TMR_PREG},
	{0xCF00, SIERRA_PSM_DIAG_PREG},
	{0x001F, SIERRA_PSC_TX_A0_PREG},
	{0x0007, SIERRA_PSC_TX_A1_PREG},
	{0x0003, SIERRA_PSC_TX_A2_PREG},
	{0x0003, SIERRA_PSC_TX_A3_PREG},
	{0x0FFF, SIERRA_PSC_RX_A0_PREG},
	{0x0003, SIERRA_PSC_RX_A1_PREG},
	{0x0003, SIERRA_PSC_RX_A2_PREG},
	{0x0001, SIERRA_PSC_RX_A3_PREG},
	{0x0001, SIERRA_PLLCTRL_SUBRATE_PREG},
	{0x0406, SIERRA_PLLCTRL_GEN_D_PREG},
	{0x5233, SIERRA_PLLCTRL_CPGAIN_MODE_PREG},
	{0x00CA, SIERRA_CLKPATH_BIASTRIM_PREG},
	{0x2512, SIERRA_DFE_BIASTRIM_PREG},
	{0x0000, SIERRA_DRVCTRL_ATTEN_PREG},
	{0x823E, SIERRA_CLKPATHCTRL_TMR_PREG},
	{0x078F, SIERRA_RX_CREQ_FLTR_A_MODE1_PREG},
	{0x078F, SIERRA_RX_CREQ_FLTR_A_MODE0_PREG},
	{0x7B3C, SIERRA_CREQ_CCLKDET_MODE01_PREG},
	{0x023C, SIERRA_RX_CTLE_MAINTENANCE_PREG},
	{0x3232, SIERRA_CREQ_FSMCLK_SEL_PREG},
	{0x0000, SIERRA_CREQ_EQ_CTRL_PREG},
	{0x0000, SIERRA_CREQ_SPARE_PREG},
	{0xCC44, SIERRA_CREQ_EQ_OPEN_EYE_THRESH_PREG},
	{0x8452, SIERRA_CTLELUT_CTRL_PREG},
	{0x4121, SIERRA_DFE_ECMP_RATESEL_PREG},
	{0x4121, SIERRA_DFE_SMP_RATESEL_PREG},
	{0x0003, SIERRA_DEQ_PHALIGN_CTRL},
	{0x3200, SIERRA_DEQ_CONCUR_CTRL1_PREG},
	{0x5064, SIERRA_DEQ_CONCUR_CTRL2_PREG},
	{0x0030, SIERRA_DEQ_EPIPWR_CTRL2_PREG},
	{0x0048, SIERRA_DEQ_FAST_MAINT_CYCLES_PREG},
	{0x5A5A, SIERRA_DEQ_ERRCMP_CTRL_PREG},
	{0x02F5, SIERRA_DEQ_OFFSET_CTRL_PREG},
	{0x02F5, SIERRA_DEQ_GAIN_CTRL_PREG},
	{0x9999, SIERRA_DEQ_VGATUNE_CTRL_PREG},
	{0x0014, SIERRA_DEQ_GLUT0},
	{0x0014, SIERRA_DEQ_GLUT1},
	{0x0014, SIERRA_DEQ_GLUT2},
	{0x0014, SIERRA_DEQ_GLUT3},
	{0x0014, SIERRA_DEQ_GLUT4},
	{0x0014, SIERRA_DEQ_GLUT5},
	{0x0014, SIERRA_DEQ_GLUT6},
	{0x0014, SIERRA_DEQ_GLUT7},
	{0x0014, SIERRA_DEQ_GLUT8},
	{0x0014, SIERRA_DEQ_GLUT9},
	{0x0014, SIERRA_DEQ_GLUT10},
	{0x0014, SIERRA_DEQ_GLUT11},
	{0x0014, SIERRA_DEQ_GLUT12},
	{0x0014, SIERRA_DEQ_GLUT13},
	{0x0014, SIERRA_DEQ_GLUT14},
	{0x0014, SIERRA_DEQ_GLUT15},
	{0x0014, SIERRA_DEQ_GLUT16},
	{0x0BAE, SIERRA_DEQ_ALUT0},
	{0x0AEB, SIERRA_DEQ_ALUT1},
	{0x0A28, SIERRA_DEQ_ALUT2},
	{0x0965, SIERRA_DEQ_ALUT3},
	{0x08A2, SIERRA_DEQ_ALUT4},
	{0x07DF, SIERRA_DEQ_ALUT5},
	{0x071C, SIERRA_DEQ_ALUT6},
	{0x0659, SIERRA_DEQ_ALUT7},
	{0x0596, SIERRA_DEQ_ALUT8},
	{0x0514, SIERRA_DEQ_ALUT9},
	{0x0492, SIERRA_DEQ_ALUT10},
	{0x0410, SIERRA_DEQ_ALUT11},
	{0x038E, SIERRA_DEQ_ALUT12},
	{0x030C, SIERRA_DEQ_ALUT13},
	{0x03F4, SIERRA_DEQ_DFETAP_CTRL_PREG},
	{0x0001, SIERRA_DFE_EN_1010_IGNORE_PREG},
	{0x3C01, SIERRA_DEQ_TAU_CTRL1_FAST_MAINT_PREG},
	{0x3C40, SIERRA_DEQ_TAU_CTRL1_SLOW_MAINT_PREG},
	{0x1C08, SIERRA_DEQ_TAU_CTRL2_PREG},
	{0x0033, SIERRA_DEQ_PICTRL_PREG},
	{0x0400, SIERRA_CPICAL_TMRVAL_MODE1_PREG},
	{0x0330, SIERRA_CPICAL_TMRVAL_MODE0_PREG},
	{0x01FF, SIERRA_CPICAL_PICNT_MODE1_PREG},
	{0x0009, SIERRA_CPI_OUTBUF_RATESEL_PREG},
	{0x3232, SIERRA_CPICAL_RES_STARTCODE_MODE23_PREG},
	{0x0005, SIERRA_LFPSDET_SUPPORT_PREG},
	{0x000F, SIERRA_LFPSFILT_NS_PREG},
	{0x0009, SIERRA_LFPSFILT_RD_PREG},
	{0x0001, SIERRA_LFPSFILT_MP_PREG},
	{0x6013, SIERRA_SIGDET_SUPPORT_PREG},
	{0x8013, SIERRA_SDFILT_H2L_A_PREG},
	{0x8009, SIERRA_SDFILT_L2H_PREG},
	{0x0024, SIERRA_RXBUFFER_CTLECTRL_PREG},
	{0x0020, SIERRA_RXBUFFER_RCDFECTRL_PREG},
	{0x4243, SIERRA_RXBUFFER_DFECTRL_PREG}
};

static struct cdns_sierra_vals usb_100_ext_ssc_cmn_vals = {
	.reg_pairs = cdns_usb_cmn_regs_ext_ssc,
	.num_regs = ARRAY_SIZE(cdns_usb_cmn_regs_ext_ssc),
};

static struct cdns_sierra_vals usb_100_ext_ssc_ln_vals = {
	.reg_pairs = cdns_usb_ln_regs_ext_ssc,
	.num_regs = ARRAY_SIZE(cdns_usb_ln_regs_ext_ssc),
};

static const struct cdns_sierra_data cdns_map_sierra = {
	.id_value = SIERRA_MACRO_ID,
	.block_offset_shift = 0x2,
	.reg_offset_shift = 0x2,
	.pcs_cmn_vals = {
		[TYPE_PCIE] = {
			[TYPE_NONE] = {
				[NO_SSC] = &pcie_phy_pcs_cmn_vals,
				[EXTERNAL_SSC] = &pcie_phy_pcs_cmn_vals,
				[INTERNAL_SSC] = &pcie_phy_pcs_cmn_vals,
			},
			[TYPE_QSGMII] = {
				[NO_SSC] = &pcie_phy_pcs_cmn_vals,
				[EXTERNAL_SSC] = &pcie_phy_pcs_cmn_vals,
				[INTERNAL_SSC] = &pcie_phy_pcs_cmn_vals,
			},
		},
	},
	.pma_cmn_vals = {
		[TYPE_PCIE] = {
			[TYPE_NONE] = {
				[NO_SSC] = &pcie_100_no_ssc_cmn_vals,
				[EXTERNAL_SSC] = &pcie_100_ext_ssc_cmn_vals,
				[INTERNAL_SSC] = &pcie_100_int_ssc_cmn_vals,
			},
			[TYPE_QSGMII] = {
				[NO_SSC] = &pcie_100_no_ssc_plllc_cmn_vals,
				[EXTERNAL_SSC] = &pcie_100_ext_ssc_plllc_cmn_vals,
				[INTERNAL_SSC] = &pcie_100_int_ssc_plllc_cmn_vals,
			},
		},
		[TYPE_USB] = {
			[TYPE_NONE] = {
				[EXTERNAL_SSC] = &usb_100_ext_ssc_cmn_vals,
			},
		},
		[TYPE_QSGMII] = {
			[TYPE_PCIE] = {
				[NO_SSC] = &qsgmii_100_no_ssc_plllc1_cmn_vals,
				[EXTERNAL_SSC] = &qsgmii_100_no_ssc_plllc1_cmn_vals,
				[INTERNAL_SSC] = &qsgmii_100_no_ssc_plllc1_cmn_vals,
			},
		},
	},
	.pma_ln_vals = {
		[TYPE_PCIE] = {
			[TYPE_NONE] = {
				[NO_SSC] = &pcie_100_no_ssc_ln_vals,
				[EXTERNAL_SSC] = &pcie_100_ext_ssc_ln_vals,
				[INTERNAL_SSC] = &pcie_100_int_ssc_ln_vals,
			},
			[TYPE_QSGMII] = {
				[NO_SSC] = &ml_pcie_100_no_ssc_ln_vals,
				[EXTERNAL_SSC] = &ml_pcie_100_ext_ssc_ln_vals,
				[INTERNAL_SSC] = &ml_pcie_100_int_ssc_ln_vals,
			},
		},
		[TYPE_USB] = {
			[TYPE_NONE] = {
				[EXTERNAL_SSC] = &usb_100_ext_ssc_ln_vals,
			},
		},
		[TYPE_QSGMII] = {
			[TYPE_PCIE] = {
				[NO_SSC] = &qsgmii_100_no_ssc_plllc1_ln_vals,
				[EXTERNAL_SSC] = &qsgmii_100_no_ssc_plllc1_ln_vals,
				[INTERNAL_SSC] = &qsgmii_100_no_ssc_plllc1_ln_vals,
			},
		},
	},
};

static const struct cdns_sierra_data cdns_ti_map_sierra = {
	.id_value = SIERRA_MACRO_ID,
	.block_offset_shift = 0x0,
	.reg_offset_shift = 0x1,
	.pcs_cmn_vals = {
		[TYPE_PCIE] = {
			[TYPE_NONE] = {
				[NO_SSC] = &pcie_phy_pcs_cmn_vals,
				[EXTERNAL_SSC] = &pcie_phy_pcs_cmn_vals,
				[INTERNAL_SSC] = &pcie_phy_pcs_cmn_vals,
			},
			[TYPE_QSGMII] = {
				[NO_SSC] = &pcie_phy_pcs_cmn_vals,
				[EXTERNAL_SSC] = &pcie_phy_pcs_cmn_vals,
				[INTERNAL_SSC] = &pcie_phy_pcs_cmn_vals,
			},
		},
	},
	.phy_pma_ln_vals = {
		[TYPE_QSGMII] = {
			[TYPE_PCIE] = {
				[NO_SSC] = &qsgmii_phy_pma_ln_vals,
				[EXTERNAL_SSC] = &qsgmii_phy_pma_ln_vals,
				[INTERNAL_SSC] = &qsgmii_phy_pma_ln_vals,
			},
		},
	},
	.pma_cmn_vals = {
		[TYPE_PCIE] = {
			[TYPE_NONE] = {
				[NO_SSC] = &pcie_100_no_ssc_cmn_vals,
				[EXTERNAL_SSC] = &pcie_100_ext_ssc_cmn_vals,
				[INTERNAL_SSC] = &pcie_100_int_ssc_cmn_vals,
			},
			[TYPE_QSGMII] = {
				[NO_SSC] = &pcie_100_no_ssc_plllc_cmn_vals,
				[EXTERNAL_SSC] = &pcie_100_ext_ssc_plllc_cmn_vals,
				[INTERNAL_SSC] = &pcie_100_int_ssc_plllc_cmn_vals,
			},
		},
		[TYPE_USB] = {
			[TYPE_NONE] = {
				[EXTERNAL_SSC] = &usb_100_ext_ssc_cmn_vals,
			},
		},
		[TYPE_QSGMII] = {
			[TYPE_PCIE] = {
				[NO_SSC] = &qsgmii_100_no_ssc_plllc1_cmn_vals,
				[EXTERNAL_SSC] = &qsgmii_100_no_ssc_plllc1_cmn_vals,
				[INTERNAL_SSC] = &qsgmii_100_no_ssc_plllc1_cmn_vals,
			},
		},
	},
	.pma_ln_vals = {
		[TYPE_PCIE] = {
			[TYPE_NONE] = {
				[NO_SSC] = &pcie_100_no_ssc_ln_vals,
				[EXTERNAL_SSC] = &pcie_100_ext_ssc_ln_vals,
				[INTERNAL_SSC] = &pcie_100_int_ssc_ln_vals,
			},
			[TYPE_QSGMII] = {
				[NO_SSC] = &ml_pcie_100_no_ssc_ln_vals,
				[EXTERNAL_SSC] = &ml_pcie_100_ext_ssc_ln_vals,
				[INTERNAL_SSC] = &ml_pcie_100_int_ssc_ln_vals,
			},
		},
		[TYPE_USB] = {
			[TYPE_NONE] = {
				[EXTERNAL_SSC] = &usb_100_ext_ssc_ln_vals,
			},
		},
		[TYPE_QSGMII] = {
			[TYPE_PCIE] = {
				[NO_SSC] = &qsgmii_100_no_ssc_plllc1_ln_vals,
				[EXTERNAL_SSC] = &qsgmii_100_no_ssc_plllc1_ln_vals,
				[INTERNAL_SSC] = &qsgmii_100_no_ssc_plllc1_ln_vals,
			},
		},
	},
};

static const struct of_device_id cdns_sierra_id_table[] = {
	{
		.compatible = "cdns,sierra-phy-t0",
		.data = &cdns_map_sierra,
	},
	{
		.compatible = "ti,sierra-phy-t0",
		.data = &cdns_ti_map_sierra,
	},
	{}
};
MODULE_DEVICE_TABLE(of, cdns_sierra_id_table);

static struct platform_driver cdns_sierra_driver = {
	.probe		= cdns_sierra_phy_probe,
	.remove		= cdns_sierra_phy_remove,
	.driver		= {
		.name	= "cdns-sierra-phy",
		.of_match_table = cdns_sierra_id_table,
	},
};
module_platform_driver(cdns_sierra_driver);

MODULE_ALIAS("platform:cdns_sierra");
MODULE_AUTHOR("Cadence Design Systems");
MODULE_DESCRIPTION("CDNS sierra phy driver");
MODULE_LICENSE("GPL v2");
