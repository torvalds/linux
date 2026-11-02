// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include "phy-qcom-qmp.h"
#include "phy-qcom-qmp-common.h"
#include "phy-qcom-qmp-pcs-v6_40.h"
#include "phy-qcom-qmp-pcs-pcie-v6_40.h"
#include "phy-qcom-qmp-qserdes-com-pcie-v6_40.h"
#include "phy-qcom-qmp-qserdes-txrxz-pcie-v6_40.h"

#define PHY_INIT_COMPLETE_TIMEOUT_US		10000

enum qmp_pcie_glymur_link_mode {
	QMP_PCIE_GLYMUR_MODE_X8,
	QMP_PCIE_GLYMUR_MODE_X4X4,
};

enum qmp_pcie_nord_link_mode {
	QMP_PCIE_NORD_MODE_X16,
	QMP_PCIE_NORD_MODE_X8_X8,
	QMP_PCIE_NORD_MODE_X8_X4_X4,
	QMP_PCIE_NORD_MODE_X8_X4_X2_X2
};

enum qphy_reg_layout {
	QPHY_PCS_STATUS,
	QPHY_PCS_START_CONTROL,
	QPHY_PCS_SW_RESET,
	QPHY_PCS_POWER_DOWN_CONTROL,
	QPHY_COM_BIAS_EN_CLKBUFLR_EN,
	QPHY_LAYOUT_SIZE
};

static const unsigned int pciephy_v8_50_regs_layout[QPHY_LAYOUT_SIZE] = {
	[QPHY_PCS_STATUS]		= QPHY_V8_50_PCS_STATUS1,
};

static const unsigned int nord_pciephy_v6_40_regs_layout[QPHY_LAYOUT_SIZE] = {
	[QPHY_PCS_STATUS]		= QPHY_V6_40_PCS_STATUS1,
	[QPHY_PCS_START_CONTROL]	= QPHY_V6_40_PCS_START_CONTROL,
	[QPHY_PCS_SW_RESET]		= QPHY_V6_40_PCS_SW_RESET,
	[QPHY_PCS_POWER_DOWN_CONTROL]	= QPHY_V6_40_PCS_POWER_DOWN_CONTROL,
	[QPHY_COM_BIAS_EN_CLKBUFLR_EN]	= QSERDES_PCIE_V6_40_COM_BIAS_EN_CLKBUFLR_EN,
};

struct qmp_pcie_offsets {
	u16 pcs;
	u16 pll;
	u16 txrxz;
	u16 pcs_lanez;
};

struct qmp_phy_cfg_tbls {
	const struct qmp_phy_init_tbl *pll_common;
	int pll_common_num;
	const struct qmp_phy_init_tbl *txrxz;
	int txrxz_num;
	const struct qmp_phy_init_tbl *pcs;
	int pcs_num;
	const struct qmp_phy_init_tbl *pcs_lanez;
	int pcs_lanez_num;
	const struct qmp_phy_init_tbl *port_split_pre_pwrup;
	int port_split_pre_pwrup_num;
	const struct qmp_phy_init_tbl *port_split_post_cfg;
	int port_split_post_cfg_num;
};

struct qmp_phy_cfg {
	const struct qmp_pcie_offsets *offsets;
	const struct qmp_phy_cfg_tbls tbls;
	const struct qmp_phy_init_tbl * const *pll_tbls;
	const int *pll_nums;
	bool port_split;
	const char * const *reg_names;
	int num_regs;
	const char * const *pd_names;
	int num_pds;
	const char * const *reset_list;
	int num_resets;
	const char * const *nocsr_reset_list;
	int num_nocsr_resets;
	const char * const *vreg_list;
	int num_vregs;
	const unsigned int *regs;
	unsigned int phy_status;
	const char * const *clk_list;
	int num_clks;
	const char * const *pipe_clk_list;
	int num_pipe_clks;
};

struct qmp_pcie {
	struct device *dev;
	const struct qmp_phy_cfg *cfg;
	bool skip_init;
	void __iomem **base;
	struct clk_bulk_data *clks;
	struct clk_bulk_data *pipe_clks;
	struct reset_control_bulk_data *resets;
	struct reset_control_bulk_data *nocsr_resets;
	struct regulator_bulk_data *vregs;
	struct device **pd_devs;
};

struct qmp_pcie_multiphy {
	struct phy **phys;
	const struct qmp_pcie_link_mode_cfg *mode_cfg;
	int num_pipe_outputs;
	struct clk_fixed_rate *pipe_out_clks;
};

struct qmp_pcie_link_mode_cfg {
	const struct qmp_phy_cfg * const *cfgs;
	u32 num_phys;
};

struct qmp_pcie_match_data {
	const struct qmp_pcie_link_mode_cfg *mode_cfgs;
	u32 num_modes;
};

/* QSERDES_PCIE_V6_40_COM_BIAS_EN_CLKBUFLR_EN bits */
#define COM_BIAS_EN				BIT(0)
#define COM_BIAS_EN_MUX				BIT(1)

static const char * const glymur_pciephy_clk_l[] = {
	"aux", "cfg_ahb", "ref", "rchng", "phy_b_aux",
};

static const char * const glymur_pciephy_a_clk_l[] = {
	"aux", "cfg_ahb", "ref", "rchng",
};

static const char * const glymur_pciephy_b_clk_l[] = {
	"phy_b_aux", "cfg_ahb_b", "ref", "rchng_b",
};

static const char * const glymur_pciephy_pipeclk_l[] = {
	"pipe",
};

static const char * const glymur_pipephy_b_pipeclk_l[] = {
	"pipe_b", "pipediv2_b",
};

static const char * const glymur_vreg_l[] = {
	"vdda-phy", "vdda-pll", "vdda-refgen0p9", "vdda-refgen1p2",
};

static const char * const pciephy_port_a_reg_l[] = {
	"port_a",
};

static const char * const pciephy_port_b_reg_l[] = {
	"port_b",
};

static const char * const pciephy_port_ab_reg_l[] = {
	"port_a", "port_b",
};

static const char * const pciephy_port_a_nocsr_reset_l[] = {
	"port_a_nocsr",
};

static const char * const glymur_pciephy_nocsr_reset_l[] = {
	"port_a_nocsr", "port_b_nocsr",
};

static const char * const pciephy_port_b_nocsr_reset_l[] = {
	"port_b_nocsr",
};

static const struct qmp_pcie_offsets glymur_pcie_offsets_v8_50 = {
	.pcs		= 0x9000,
};

static const struct qmp_phy_cfg glymur_qmp_gen5x4_pciephy_a_cfg = {
	.offsets		= &glymur_pcie_offsets_v8_50,
	.reg_names		= pciephy_port_a_reg_l,
	.num_regs		= ARRAY_SIZE(pciephy_port_a_reg_l),
	.pd_names		= pciephy_port_a_reg_l,
	.num_pds		= ARRAY_SIZE(pciephy_port_a_reg_l),
	.nocsr_reset_list	= pciephy_port_a_nocsr_reset_l,
	.num_nocsr_resets	= ARRAY_SIZE(pciephy_port_a_nocsr_reset_l),
	.vreg_list		= glymur_vreg_l,
	.num_vregs		= ARRAY_SIZE(glymur_vreg_l),
	.regs			= pciephy_v8_50_regs_layout,
	.phy_status		= PHYSTATUS_4_20,
	.pipe_clk_list		= glymur_pciephy_pipeclk_l,
	.num_pipe_clks		= ARRAY_SIZE(glymur_pciephy_pipeclk_l),
	.clk_list		= glymur_pciephy_a_clk_l,
	.num_clks		= ARRAY_SIZE(glymur_pciephy_a_clk_l),
};

static const struct qmp_phy_cfg glymur_qmp_gen5x4_pciephy_b_cfg = {
	.offsets		= &glymur_pcie_offsets_v8_50,
	.reg_names		= pciephy_port_b_reg_l,
	.num_regs		= ARRAY_SIZE(pciephy_port_b_reg_l),
	.pd_names		= pciephy_port_b_reg_l,
	.num_pds		= ARRAY_SIZE(pciephy_port_b_reg_l),
	.nocsr_reset_list	= pciephy_port_b_nocsr_reset_l,
	.num_nocsr_resets	= ARRAY_SIZE(pciephy_port_b_nocsr_reset_l),
	.vreg_list		= glymur_vreg_l,
	.num_vregs		= ARRAY_SIZE(glymur_vreg_l),
	.regs			= pciephy_v8_50_regs_layout,
	.phy_status		= PHYSTATUS_4_20,
	.pipe_clk_list		= glymur_pipephy_b_pipeclk_l,
	.num_pipe_clks		= ARRAY_SIZE(glymur_pipephy_b_pipeclk_l),
	.clk_list		= glymur_pciephy_b_clk_l,
	.num_clks		= ARRAY_SIZE(glymur_pciephy_b_clk_l),
};

static const struct qmp_phy_cfg glymur_qmp_gen5x8_pciephy_cfg = {
	.offsets		= &glymur_pcie_offsets_v8_50,
	.reg_names		= pciephy_port_ab_reg_l,
	.num_regs		= ARRAY_SIZE(pciephy_port_ab_reg_l),
	.pd_names		= pciephy_port_ab_reg_l,
	.num_pds		= ARRAY_SIZE(pciephy_port_ab_reg_l),
	.nocsr_reset_list	= glymur_pciephy_nocsr_reset_l,
	.num_nocsr_resets	= ARRAY_SIZE(glymur_pciephy_nocsr_reset_l),
	.vreg_list		= glymur_vreg_l,
	.num_vregs		= ARRAY_SIZE(glymur_vreg_l),
	.regs			= pciephy_v8_50_regs_layout,
	.phy_status		= PHYSTATUS_4_20,
	.pipe_clk_list		= glymur_pciephy_pipeclk_l,
	.num_pipe_clks		= ARRAY_SIZE(glymur_pciephy_pipeclk_l),
	.clk_list		= glymur_pciephy_clk_l,
	.num_clks		= ARRAY_SIZE(glymur_pciephy_clk_l),
};

static const struct qmp_phy_cfg * const glymur_qmp_gen5x8_mode_x8_cfgs[] = {
	&glymur_qmp_gen5x8_pciephy_cfg,
};

static const struct qmp_phy_cfg * const glymur_qmp_gen5x8_mode_x4x4_cfgs[] = {
	&glymur_qmp_gen5x4_pciephy_a_cfg,
	&glymur_qmp_gen5x4_pciephy_b_cfg,
};

static const struct qmp_pcie_link_mode_cfg glymur_qmp_gen5x8_mode_cfgs[] = {
	[QMP_PCIE_GLYMUR_MODE_X8] = {
		.cfgs		= glymur_qmp_gen5x8_mode_x8_cfgs,
		.num_phys	= ARRAY_SIZE(glymur_qmp_gen5x8_mode_x8_cfgs),
	},
	[QMP_PCIE_GLYMUR_MODE_X4X4] = {
		.cfgs		= glymur_qmp_gen5x8_mode_x4x4_cfgs,
		.num_phys	= ARRAY_SIZE(glymur_qmp_gen5x8_mode_x4x4_cfgs),
	},
};

static const struct qmp_pcie_match_data glymur_qmp_gen5x8_match_data = {
	.mode_cfgs	= glymur_qmp_gen5x8_mode_cfgs,
	.num_modes	= ARRAY_SIZE(glymur_qmp_gen5x8_mode_cfgs),
};

static const char * const nord_pciephy_port_a_vreg_l[] = {
	"vdda-phy-a", "vdda-pll-a",
};

static const char * const nord_pciephy_port_b_vreg_l[] = {
	"vdda-phy-b", "vdda-pll-b",
};

static const char * const nord_pciephy_port_c_vreg_l[] = {
	"vdda-phy-c", "vdda-pll-c",
};

static const char * const nord_pciephy_port_d_vreg_l[] = {
	"vdda-phy-d", "vdda-pll-d",
};

static const char * const nord_pciephy_port_cd_vreg_l[] = {
	"vdda-phy-c", "vdda-pll-c", "vdda-phy-d", "vdda-pll-d",
};

static const char * const nord_pciephy_port_bcd_vreg_l[] = {
	"vdda-phy-b", "vdda-pll-b", "vdda-phy-c", "vdda-pll-c",
	"vdda-phy-d", "vdda-pll-d",
};

static const char * const nord_pciephy_port_abcd_vreg_l[] = {
	"vdda-phy-a", "vdda-pll-a", "vdda-phy-b", "vdda-pll-b",
	"vdda-phy-c", "vdda-pll-c", "vdda-phy-d", "vdda-pll-d",
};

static const struct qmp_pcie_offsets nord_pcie_offsets_v6_40 = {
	.pll		= 0x8000,
	.pcs		= 0x9000,
	.txrxz		= 0xd000,
	.pcs_lanez	= 0xe800,
};

static const struct qmp_phy_init_tbl nord_qmp_pcie_pll_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_CLK_FWD_CONFIG_1, 0x0f),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_SSC_STEP_SIZE1_MODE2, 0xab),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_SSC_STEP_SIZE2_MODE2, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_CP_CTRL_MODE2, 0x06),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_PLL_RCTRL_MODE2, 0x16),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_PLL_CCTRL_MODE2, 0x36),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_CORECLK_DIV_MODE2, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_LOCK_CMP1_MODE2, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_LOCK_CMP2_MODE2, 0x1a),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_DEC_START_MODE2, 0x68),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_DIV_FRAC_START1_MODE2, 0xab),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_DIV_FRAC_START2_MODE2, 0xaa),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_DIV_FRAC_START3_MODE2, 0x02),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_BIN_VCOCAL_CMP_CODE1_MODE2, 0xa4),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_BIN_VCOCAL_CMP_CODE2_MODE2, 0x18),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_SSC_STEP_SIZE1_MODE1, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_SSC_STEP_SIZE2_MODE1, 0x03),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_CP_CTRL_MODE1, 0x06),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_PLL_RCTRL_MODE1, 0x16),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_PLL_CCTRL_MODE1, 0x36),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_CORECLK_DIV_MODE1, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_LOCK_CMP1_MODE1, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_LOCK_CMP2_MODE1, 0x0d),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_DEC_START_MODE1, 0x68),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_DIV_FRAC_START1_MODE1, 0xab),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_DIV_FRAC_START2_MODE1, 0xaa),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_DIV_FRAC_START3_MODE1, 0x02),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_BIN_VCOCAL_CMP_CODE1_MODE1, 0x52),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_BIN_VCOCAL_CMP_CODE2_MODE1, 0x0c),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_HSCLK_SEL_1, 0x3c),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_HSCLK_SEL_2, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_BIN_VCOCAL_HSCLK_SEL_1, 0x3c),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_BIN_VCOCAL_HSCLK_SEL_2, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_SSC_STEP_SIZE1_MODE0, 0xe0),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_SSC_STEP_SIZE2_MODE0, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_CP_CTRL_MODE0, 0x06),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_PLL_RCTRL_MODE0, 0x16),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_PLL_CCTRL_MODE0, 0x36),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_CORECLK_DIV_MODE0, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_LOCK_CMP1_MODE0, 0x40),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_LOCK_CMP2_MODE0, 0x03),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_DEC_START_MODE0, 0x41),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_DIV_FRAC_START1_MODE0, 0xab),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_DIV_FRAC_START2_MODE0, 0xaa),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_DIV_FRAC_START3_MODE0, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_BIN_VCOCAL_CMP_CODE1_MODE0, 0xd4),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_BIN_VCOCAL_CMP_CODE2_MODE0, 0x03),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_HSCLK_HS_SWITCH_SEL_1, 0x16),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_HSCLK_HS_SWITCH_SEL_2, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_BG_TIMER, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_VCO_TUNE_CTRL, 0x40),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_SSC_EN_CENTER, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_SSC_PER1, 0x62),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_SSC_PER2, 0x02),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_POST_DIV_MUX, 0xc0),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_BIAS_EN_CLKBUFLR_EN, 0x1f),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_CLK_ENABLE1, 0x90),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_SYS_CLK_CTRL, 0x82),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_PLL_IVCO, 0x1f),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_PLL_IVCO_MODE1, 0x1f),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_PLL_IVCO_MODE2, 0x1f),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_SYSCLK_EN_SEL, 0x08),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_LOCK_CMP_EN, 0x46),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_LOCK_CMP_CFG, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_VCO_TUNE_MAP, 0x24),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_CLK_SELECT, 0x34),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_CORE_CLK_EN, 0xe0),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_CMN_CONFIG_1, 0x86),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_CMN_MISC1, 0x88),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_CMN_MODE, 0x14),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_VCO_DC_LEVEL_CTRL, 0x0f),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_CMN_CONFIG_2, 0x10),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_CLK_EP_DIV_MODE1, 0x14),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_CLK_EP_DIV_MODE0, 0x32),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_PSM_CAL_EN, 0x05),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_PLL_SPARE_FOR_ECO, 0x40),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_DCC_CAL_1, 0x40),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_DCC_CAL_2, 0x07),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_DCC_CAL_3, 0x60),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_DCC_CAL_8, 0x32),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_IP_CTRL_AND_DP_SEL, 0xaf),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_CMN_IETRIM, 0x0d),
};

static const struct qmp_phy_init_tbl nord_qmp_pcie_pll_a_ovl_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_DCC_CAL_7, 0x31),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_DCC_CAL_9, 0x31),
};

static const struct qmp_phy_init_tbl nord_qmp_pcie_pll_b_ovl_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_DCC_CAL_7, 0x32),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_DCC_CAL_9, 0x22),
};

static const struct qmp_phy_init_tbl nord_qmp_pcie_pll_c_ovl_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_DCC_CAL_7, 0x20),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_DCC_CAL_9, 0x31),
};

static const struct qmp_phy_init_tbl nord_qmp_pcie_pll_d_ovl_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_DCC_CAL_7, 0x22),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_COM_DCC_CAL_9, 0x22),
};

static const struct qmp_phy_init_tbl nord_qmp_pcie_txrxz_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_SIGDET_ENABLES, 0x1c),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_MODE_RATE2_B0, 0xcd),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_MODE_RATE2_B1, 0x34),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_MODE_RATE2_B2, 0x55),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_MODE_RATE2_B3, 0x50),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_MODE_RATE2_B4, 0xac),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_MODE_RATE2_B5, 0xdb),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_MODE_RATE2_B6, 0x52),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_MODE_RATE2_B7, 0x3e),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_MODE_RATE3_B0, 0xce),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_MODE_RATE3_B1, 0x55),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_MODE_RATE3_B2, 0x6d),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_MODE_RATE3_B3, 0x60),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_MODE_RATE3_B4, 0x8c),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_MODE_RATE3_B5, 0x9d),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_MODE_RATE3_B6, 0x6d),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_MODE_RATE3_B7, 0x26),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_MODE_RATE4_B0, 0xfb),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_MODE_RATE4_B1, 0xea),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_MODE_RATE4_B2, 0xe8),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_MODE_RATE4_B3, 0x70),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_MODE_RATE4_B4, 0x9c),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_MODE_RATE4_B5, 0xf4),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_MODE_RATE4_B6, 0x6d),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_MODE_RATE4_B7, 0x3b),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_SUMMER_CAL_SPD_MODE_RATE_0123, 0x6f),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_TX_ADAPT_POST_THRESH1, 0x02),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_TX_ADAPT_POST_THRESH2, 0x0d),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_PHPRE_CTRL, 0x20),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_TX_ADPT_CTRL, 0x30),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_MODE_RATE_0_1_B0, 0xff),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_MODE_RATE_0_1_B1, 0x4a),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_MODE_RATE_0_1_B2, 0x77),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_MODE_RATE_0_1_B3, 0x50),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_MODE_RATE_0_1_B4, 0x9a),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_MODE_RATE_0_1_B5, 0x47),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_MODE_RATE_0_1_B6, 0x52),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_MODE_RATE_0_1_B7, 0x1d),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_LANE_MODE_1, 0x05),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_LANE_MODE_2, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_LANE_MODE_3, 0x41),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_LANE_MODE_4, 0x0f),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_TOP_LDO_CODE_CTRL1, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_TOP_LDO_CODE_CTRL2, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_TOP_LDO_CODE_CTRL3, 0x81),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_TOP_LDO_CODE_CTRL4, 0x8d),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_LOCK_CTRL, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_DFE_TAP1_DAC_ENABLE, 0x1c),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_DFE_TAP2_DAC_ENABLE, 0x1c),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_DFE_TAP345_DAC_ENABLE, 0x18),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_DFE_TAP67_DAC_ENABLE, 0x10),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_CP_CUR_FLL_RATE0, 0x07),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_CP_CUR_FLL_RATE1, 0x07),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_CP_CUR_FLL_RATE2, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_CP_CUR_FLL_RATE3, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_CP_CUR_FLL_RATE4, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_CP_CUR_PLL_RATE0, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_CP_CUR_PLL_RATE1, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_CP_CUR_PLL_RATE2, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_CP_CUR_PLL_RATE3, 0x03),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_CP_CUR_PLL_RATE4, 0x03),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_FLL_DIV_RATIO_RATE_0123, 0x94),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_LOOP_CCODE_RATE_01, 0x66),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_LOOP_CCODE_RATE_23, 0x77),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_LOOP_CCODE_RATE4, 0x07),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_IQTUNE_CTRL, 0x58),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_IQTUNE_MAN_INDEX, 0x0d),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_IQTUNE_ANA_CTRL, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_LOOP_FUNC_CTRL, 0xd0),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_KP_CAL_UPPER_FREQ_DIFF_BND1_RATE0, 0xff),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_KP_CAL_UPPER_FREQ_DIFF_BND1_RATE1, 0xff),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_KP_CAL_UPPER_FREQ_DIFF_BND1_RATE2, 0xff),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_KP_CAL_UPPER_FREQ_DIFF_BND1_RATE3, 0xff),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_KP_CAL_UPPER_FREQ_DIFF_BND1_RATE4, 0xff),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_KP_CAL_LOWER_FREQ_DIFF_BND_RATE0, 0x15),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_KP_CAL_LOWER_FREQ_DIFF_BND_RATE1, 0x15),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_KP_CAL_LOWER_FREQ_DIFF_BND_RATE2, 0x10),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_KP_CAL_LOWER_FREQ_DIFF_BND_RATE3, 0x07),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_KP_CAL_LOWER_FREQ_DIFF_BND_RATE4, 0x0c),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_KP_CODE_OVRD_RATE_2_3, 0x24),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_IVCM_CAL_CTRL2, 0x83),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_IVCM_CAL_CTRL3, 0x44),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_IVCM_CAL_CTRL4, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_LOOP_RCODE_PLL_RATE_0_1, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_LOOP_RCODE_PLL_RATE_2_3, 0x11),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_LOOP_RCODE_PLL_RATE4, 0x02),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_LOOP_RCODE_FLL_RATE_0_1, 0x33),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_LOOP_RCODE_FLL_RATE_2_3, 0x33),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_LOOP_RCODE_FLL_RATE4, 0x03),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_LOOP_RCODE_FAST_RATE_0_1, 0x22),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_LOOP_RCODE_FAST_RATE_2_3, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_LOOP_RCODE_FAST_RATE4, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_IQTUNE_DIV2_CTRL_RATE0123, 0x7f),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_VCO_CTUNE_MEAS_CNT1_RATE0, 0x13),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_VCO_CTUNE_MEAS_CNT2_RATE0, 0x13),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_VCO_CTUNE_MEAS_CNT1_RATE1, 0x26),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_VCO_CTUNE_MEAS_CNT2_RATE1, 0x26),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_VCO_CTUNE_MEAS_CNT1_RATE2, 0x9c),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_VCO_CTUNE_MEAS_CNT2_RATE2, 0x3d),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_VCO_CTUNE_MEAS_CNT1_RATE3, 0x9c),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_VCO_CTUNE_MEAS_CNT2_RATE3, 0x3d),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_VCO_CTUNE_MEAS_CNT1_RATE4, 0x09),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_VCO_CTUNE_MEAS_CNT2_RATE4, 0x3d),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_EQU_ADAPTOR_CNTRL3, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_EQU_ADAPTOR_CNTRL4, 0xaa),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RX_EQU_ADAPTOR_CNTRL5, 0x0a),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_VGA_CAL_CNTRL1, 0xe1),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_VGA_CAL_MAN_VAL_RATE0_1, 0xdd),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_VGA_CAL_MAN_VAL_RATE2_3, 0xdc),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_VGA_CAL_MAN_VAL_RATE4, 0x05),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_VCO_CAP_CODE_OVRD_MUXES, 0x11),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_VCO_CAP_CODE_RATE_0123, 0x55),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_VCO_CAP_CODE_RATE4, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_KVCO_IDEAL_FREQ_DIFF1_RATE0, 0x4b),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_KVCO_IDEAL_FREQ_DIFF2_RATE0, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_KVCO_IDEAL_FREQ_DIFF1_RATE1, 0x4b),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_KVCO_IDEAL_FREQ_DIFF2_RATE1, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_KVCO_IDEAL_FREQ_DIFF1_RATE2, 0x35),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_KVCO_IDEAL_FREQ_DIFF2_RATE2, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_KVCO_IDEAL_FREQ_DIFF1_RATE3, 0x28),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_KVCO_IDEAL_FREQ_DIFF1_RATE4, 0x14),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_KVCO_INIT_RATE_0_1, 0x11),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_KVCO_INIT_RATE_2_3, 0x22),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_KVCO_INIT_RATE_4, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_KVCO_CODE_OVRD_RATE0, 0x04),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_KVCO_CODE_OVRD_RATE1, 0x03),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_KVCO_CODE_OVRD_RATE2, 0x01),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_FREQ_LOCK_DET_DLY_RATE0, 0xff),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_FREQ_LOCK_DET_DLY_RATE1, 0xff),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_FREQ_LOCK_DET_DLY_RATE2, 0xff),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_FREQ_LOCK_DET_DLY_RATE3, 0xff),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_FREQ_LOCK_DET_DLY_RATE4, 0xff),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_VCTRL_RATE_0_1, 0x43),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_VCTRL_RATE_2_3, 0x44),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_VCO_TYPE_CONFIG, 0x03),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CTLE_POST_CAL_OFFSET_RATE_0_1_2, 0x67),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CDR_VCO_EN_LOWFREQ, 0x1f),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RES_CODE_LANE_OFFSET_RX, 0x18),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_GM_CAL_RES_RATE0_1, 0x88),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_GM_CAL_RES_RATE2_3, 0x88),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_GM_CAL_RES_RATE4, 0x09),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_VTHRESH_CAL_MAN_VAL_RATE4, 0x56),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_CLKBUF_ENABLE, 0x40),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_VCO_CTUNE_UPPER_BND_RATE0, 0x1e),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_VCO_CTUNE_UPPER_BND_RATE1, 0x1e),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_VCO_CTUNE_UPPER_BND_RATE2, 0x1e),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_VCO_CTUNE_UPPER_BND_RATE3, 0x1e),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_VCO_CTUNE_UPPER_BND_RATE4, 0x1e),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_SIGDET_CNTRL, 0x2f),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_SIGDET_LVL, 0x84),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_SIGDET_CAL_CTRL1, 0x10),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_DIG_BKUP_CTRL15, 0x00),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_DIG_BKUP_CTRL16, 0x52),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RESTRIM_CAL_CTRL, 0x08),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RESTRIM_POST_CAL_OFFSET, 0x10),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_RESTRIM_VREF_SEL, 0x16),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_BLW_CTRL, 0x3c),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_BLW_MAN_VAL_RATE3, 0x17),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_BLW_MAN_VAL_RATE4, 0x17),
};

static const struct qmp_phy_init_tbl nord_qmp_pcie_pcs_tbl[] = {
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_POWER_STATE_CONFIG2, 0x3d),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_POWER_STATE_CONFIG5, 0xe4),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_POWER_STATE_CONFIG6, 0x1f),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_POWER_STATE_CONFIG7, 0x0d),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_MULTIPHY_CONFIG2, 0x01),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_PCS_TX_RX_CONFIG2, 0xe4),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_PCS_TX_RX_CONFIG3, 0x30),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_PCS_TX_RX_CONFIG5, 0x05),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_PCS_TX_RX_CONFIG6, 0x08),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_PCS_TX_RX_CONFIG7, 0x20),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_PCS_TX_RX_CONFIG8, 0x20),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_PCS_TX_RX_CONFIG10, 0x40),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_DIVTX_CLK_DCC_CAL_CONFIG2, 0x00),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_DIVTX_CLK_DCC_CAL_CONFIG3, 0x00),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_DIVTX_CLK_DCC_CAL_CONFIG4, 0x00),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_ENDPOINT_REFCLK_DRIVE, 0xc1),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_RX_SIGDET_LVL, 0x66),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_OSC_DTCT_ACTIONS, 0x00),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_LOCK_DETECT_CONFIG1, 0xff),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_ALIGN_DETECT_CONFIG1, 0x00),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_ALIGN_DETECT_CONFIG2, 0x04),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_ALIGN_DETECT_CONFIG3, 0x1f),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_ALIGN_DETECT_CONFIG7, 0x35),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_ALIGN_DETECT_CONFIG8, 0x3e),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_EQ_CONFIG1, 0x06),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_EQ_CONFIG2, 0x1c),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_G345_EQ_CONFIG1, 0x03),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_G345_EQ_CONFIG3, 0x00),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_G3_EQ_CONFIG1, 0x00),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_G3_EQ_CONFIG5, 0x90),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_G3_EQ_CONFIG7, 0x0d),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_G4_EQ_CONFIG1, 0x00),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_G4_EQ_CONFIG5, 0x90),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_G4_EQ_CONFIG7, 0x0d),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_G5_EQ_CONFIG1, 0x00),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_G5_EQ_CONFIG5, 0x90),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_G5_EQ_CONFIG7, 0x0d),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_COM_TEST_CONTROL1, 0x08),
};

static const struct qmp_phy_init_tbl nord_qmp_pcie_pcs_lanez_tbl[] = {
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_LANEZ_OUTSIG_MX_CTRL2, 0x00),
	QMP_PHY_INIT_CFG(QPHY_PCIE_V6_40_PCS_LANEZ_OUTSIG_MX_CTRL5, 0x00),
};

static const struct qmp_phy_init_tbl nord_qmp_pcie_port_split_pre_pwrup_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_PRE_STALL_LDO_BOOST_EN, 0x80),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_LDO_TIMER_CTRL, 0x91),
};

static const struct qmp_phy_init_tbl nord_qmp_pcie_port_split_post_cfg_tbl[] = {
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_LDO_TIMER_CTRL, 0x11),
	QMP_PHY_INIT_CFG(QSERDES_PCIE_V6_40_TXRXZ_PRE_STALL_LDO_BOOST_EN, 0x00),
};

static const struct qmp_phy_cfg_tbls nord_qmp_pcie_tbls = {
	.pll_common			= nord_qmp_pcie_pll_tbl,
	.pll_common_num			= ARRAY_SIZE(nord_qmp_pcie_pll_tbl),
	.txrxz				= nord_qmp_pcie_txrxz_tbl,
	.txrxz_num			= ARRAY_SIZE(nord_qmp_pcie_txrxz_tbl),
	.pcs				= nord_qmp_pcie_pcs_tbl,
	.pcs_num			= ARRAY_SIZE(nord_qmp_pcie_pcs_tbl),
	.pcs_lanez			= nord_qmp_pcie_pcs_lanez_tbl,
	.pcs_lanez_num			= ARRAY_SIZE(nord_qmp_pcie_pcs_lanez_tbl),
	.port_split_pre_pwrup		= nord_qmp_pcie_port_split_pre_pwrup_tbl,
	.port_split_pre_pwrup_num	= ARRAY_SIZE(nord_qmp_pcie_port_split_pre_pwrup_tbl),
	.port_split_post_cfg		= nord_qmp_pcie_port_split_post_cfg_tbl,
	.port_split_post_cfg_num	= ARRAY_SIZE(nord_qmp_pcie_port_split_post_cfg_tbl),
};

static const struct qmp_phy_init_tbl * const nord_qmp_pcie_pll_a[] = {
	nord_qmp_pcie_pll_a_ovl_tbl,
};

static const int nord_qmp_pcie_pll_a_nums[] = {
	ARRAY_SIZE(nord_qmp_pcie_pll_a_ovl_tbl),
};

static const struct qmp_phy_init_tbl * const nord_qmp_pcie_pll_b[] = {
	nord_qmp_pcie_pll_b_ovl_tbl,
};

static const int nord_qmp_pcie_pll_b_nums[] = {
	ARRAY_SIZE(nord_qmp_pcie_pll_b_ovl_tbl),
};

static const struct qmp_phy_init_tbl * const nord_qmp_pcie_pll_c[] = {
	nord_qmp_pcie_pll_c_ovl_tbl,
};

static const int nord_qmp_pcie_pll_c_nums[] = {
	ARRAY_SIZE(nord_qmp_pcie_pll_c_ovl_tbl),
};

static const struct qmp_phy_init_tbl * const nord_qmp_pcie_pll_d[] = {
	nord_qmp_pcie_pll_d_ovl_tbl,
};

static const int nord_qmp_pcie_pll_d_nums[] = {
	ARRAY_SIZE(nord_qmp_pcie_pll_d_ovl_tbl),
};

static const struct qmp_phy_init_tbl * const nord_qmp_pcie_pll_cd[] = {
	nord_qmp_pcie_pll_c_ovl_tbl, nord_qmp_pcie_pll_d_ovl_tbl,
};

static const int nord_qmp_pcie_pll_cd_nums[] = {
	ARRAY_SIZE(nord_qmp_pcie_pll_c_ovl_tbl), ARRAY_SIZE(nord_qmp_pcie_pll_d_ovl_tbl),
};

static const struct qmp_phy_init_tbl * const nord_qmp_pcie_pll_bcd[] = {
	nord_qmp_pcie_pll_b_ovl_tbl, nord_qmp_pcie_pll_c_ovl_tbl, nord_qmp_pcie_pll_d_ovl_tbl,
};

static const int nord_qmp_pcie_pll_bcd_nums[] = {
	ARRAY_SIZE(nord_qmp_pcie_pll_b_ovl_tbl), ARRAY_SIZE(nord_qmp_pcie_pll_c_ovl_tbl),
	ARRAY_SIZE(nord_qmp_pcie_pll_d_ovl_tbl),
};

static const struct qmp_phy_init_tbl * const nord_qmp_pcie_pll_abcd[] = {
	nord_qmp_pcie_pll_a_ovl_tbl, nord_qmp_pcie_pll_b_ovl_tbl,
	nord_qmp_pcie_pll_c_ovl_tbl, nord_qmp_pcie_pll_d_ovl_tbl,
};

static const int nord_qmp_pcie_pll_abcd_nums[] = {
	ARRAY_SIZE(nord_qmp_pcie_pll_a_ovl_tbl), ARRAY_SIZE(nord_qmp_pcie_pll_b_ovl_tbl),
	ARRAY_SIZE(nord_qmp_pcie_pll_c_ovl_tbl), ARRAY_SIZE(nord_qmp_pcie_pll_d_ovl_tbl),
};

static const char * const nord_pciephy_port_c_reg_l[] = { "port_c" };

static const char * const nord_pciephy_port_d_reg_l[] = { "port_d" };

static const char * const nord_pciephy_port_cd_reg_l[] = { "port_c", "port_d" };

static const char * const nord_pciephy_port_bcd_reg_l[] = {
	"port_b", "port_c", "port_d",
};

static const char * const nord_pciephy_port_abcd_reg_l[] = {
	"port_a", "port_b", "port_c", "port_d",
};

static const char * const nord_pciephy_port_a_reset_l[] = { "port_a" };

static const char * const nord_pciephy_port_b_reset_l[] = { "port_b" };

static const char * const nord_pciephy_port_c_reset_l[] = { "port_c" };

static const char * const nord_pciephy_port_d_reset_l[] = { "port_d" };

static const char * const nord_pciephy_port_cd_reset_l[] = {
	"port_c", "port_d",
};

static const char * const nord_pciephy_port_bcd_reset_l[] = {
	"port_b", "port_c", "port_d",
};

static const char * const nord_pciephy_port_abcd_reset_l[] = {
	"port_a", "port_b", "port_c", "port_d",
};

static const char * const nord_pciephy_port_c_nocsr_reset_l[] = {
	"port_c_nocsr",
};

static const char * const nord_pciephy_port_d_nocsr_reset_l[] = {
	"port_d_nocsr",
};

static const char * const nord_pciephy_port_cd_nocsr_reset_l[] = {
	"port_c_nocsr", "port_d_nocsr",
};

static const char * const nord_pciephy_port_bcd_nocsr_reset_l[] = {
	"port_b_nocsr", "port_c_nocsr", "port_d_nocsr",
};

static const char * const nord_pciephy_port_abcd_nocsr_reset_l[] = {
	"port_a_nocsr", "port_b_nocsr", "port_c_nocsr", "port_d_nocsr",
};

static const char * const nord_pciephy_port_a_pd_l[] = { "port_a" };

static const char * const nord_pciephy_port_b_pd_l[] = {
	"port_a", "port_b",
};

static const char * const nord_pciephy_port_c_pd_l[] = {
	"port_a", "port_c",
};

static const char * const nord_pciephy_port_d_pd_l[] = {
	"port_a", "port_d",
};

static const char * const nord_pciephy_port_cd_pd_l[] = {
	"port_a", "port_c", "port_d",
};

static const char * const nord_pciephy_port_bcd_pd_l[] = {
	"port_a", "port_b", "port_c", "port_d",
};

static const char * const nord_pciephy_port_abcd_pd_l[] = {
	"port_a", "port_b", "port_c", "port_d",
};

static const char * const nord_pciephy_port_a_clk_l[] = {
	"aux_a", "cfg_ahb_a", "rchng_a", "ref",
};

static const char * const nord_pciephy_port_b_clk_l[] = {
	"aux_a", "aux_b", "cfg_ahb_b", "rchng_b", "ref",
};

static const char * const nord_pciephy_port_c_clk_l[] = {
	"aux_a", "aux_c", "cfg_ahb_c", "rchng_c", "ref",
};

static const char * const nord_pciephy_port_d_clk_l[] = {
	"aux_a", "aux_d", "cfg_ahb_d", "rchng_d", "ref",
};

static const char * const nord_pciephy_port_cd_clk_l[] = {
	"aux_a", "aux_c", "cfg_ahb_c", "rchng_c",
	"aux_d", "cfg_ahb_d", "rchng_d", "ref",
};

static const char * const nord_pciephy_port_bcd_clk_l[] = {
	"aux_a", "aux_b", "cfg_ahb_b", "rchng_b",
	"aux_c", "cfg_ahb_c", "rchng_c",
	"aux_d", "cfg_ahb_d", "rchng_d", "ref",
};

static const char * const nord_pciephy_port_abcd_clk_l[] = {
	"aux_a", "cfg_ahb_a", "rchng_a",
	"aux_b", "cfg_ahb_b", "rchng_b",
	"aux_c", "cfg_ahb_c", "rchng_c",
	"aux_d", "cfg_ahb_d", "rchng_d", "ref",
};

static const char * const nord_pciephy_port_a_pipeclk_l[] = { "pipe_a" };

static const char * const nord_pciephy_port_b_pipeclk_l[] = { "pipe_b" };

static const char * const nord_pciephy_port_c_pipeclk_l[] = { "pipe_c" };

static const char * const nord_pciephy_port_d_pipeclk_l[] = { "pipe_d" };

#define NORD_QMP_PCIE_PORT_CFG(_port, _reg_l, _pd_l, _reset_l, _nocsr_reset_l, _pipeclk_l, \
				_port_split)					\
static const struct qmp_phy_cfg nord_qmp_pcie_port_##_port##_cfg = {		\
	.offsets		= &nord_pcie_offsets_v6_40,			\
	.tbls			= nord_qmp_pcie_tbls,				\
	.pll_tbls		= nord_qmp_pcie_pll_##_port,			\
	.pll_nums		= nord_qmp_pcie_pll_##_port##_nums,		\
	.port_split		= _port_split,					\
	.reg_names		= _reg_l,					\
	.num_regs		= ARRAY_SIZE(_reg_l),				\
	.pd_names		= _pd_l,					\
	.num_pds		= ARRAY_SIZE(_pd_l),				\
	.reset_list		= _reset_l,					\
	.num_resets		= ARRAY_SIZE(_reset_l),				\
	.nocsr_reset_list	= _nocsr_reset_l,				\
	.num_nocsr_resets	= ARRAY_SIZE(_nocsr_reset_l),			\
	.vreg_list		= nord_pciephy_port_##_port##_vreg_l,		\
	.num_vregs		= ARRAY_SIZE(nord_pciephy_port_##_port##_vreg_l),	\
	.regs			= nord_pciephy_v6_40_regs_layout,	\
	.phy_status		= PHYSTATUS_4_20,				\
	.clk_list		= nord_pciephy_port_##_port##_clk_l,		\
	.num_clks		= ARRAY_SIZE(nord_pciephy_port_##_port##_clk_l),	\
	.pipe_clk_list		= _pipeclk_l,					\
	.num_pipe_clks		= 1,						\
}

NORD_QMP_PCIE_PORT_CFG(a, pciephy_port_a_reg_l, nord_pciephy_port_a_pd_l,
		       nord_pciephy_port_a_reset_l, pciephy_port_a_nocsr_reset_l,
		       nord_pciephy_port_a_pipeclk_l, true);
NORD_QMP_PCIE_PORT_CFG(b, pciephy_port_b_reg_l, nord_pciephy_port_b_pd_l,
		       nord_pciephy_port_b_reset_l, pciephy_port_b_nocsr_reset_l,
		       nord_pciephy_port_b_pipeclk_l, true);
NORD_QMP_PCIE_PORT_CFG(c, nord_pciephy_port_c_reg_l, nord_pciephy_port_c_pd_l,
		       nord_pciephy_port_c_reset_l, nord_pciephy_port_c_nocsr_reset_l,
		       nord_pciephy_port_c_pipeclk_l, true);
NORD_QMP_PCIE_PORT_CFG(d, nord_pciephy_port_d_reg_l, nord_pciephy_port_d_pd_l,
		       nord_pciephy_port_d_reset_l, nord_pciephy_port_d_nocsr_reset_l,
		       nord_pciephy_port_d_pipeclk_l, true);
NORD_QMP_PCIE_PORT_CFG(cd, nord_pciephy_port_cd_reg_l, nord_pciephy_port_cd_pd_l,
		       nord_pciephy_port_cd_reset_l, nord_pciephy_port_cd_nocsr_reset_l,
		       nord_pciephy_port_c_pipeclk_l, true);
NORD_QMP_PCIE_PORT_CFG(bcd, nord_pciephy_port_bcd_reg_l, nord_pciephy_port_bcd_pd_l,
		       nord_pciephy_port_bcd_reset_l, nord_pciephy_port_bcd_nocsr_reset_l,
		       nord_pciephy_port_b_pipeclk_l, true);
NORD_QMP_PCIE_PORT_CFG(abcd, nord_pciephy_port_abcd_reg_l, nord_pciephy_port_abcd_pd_l,
		       nord_pciephy_port_abcd_reset_l, nord_pciephy_port_abcd_nocsr_reset_l,
		       nord_pciephy_port_a_pipeclk_l, false);

static const struct qmp_phy_cfg * const nord_qmp_gen5x16_mode_x16_cfgs[] = {
	&nord_qmp_pcie_port_abcd_cfg,
};

static const struct qmp_phy_cfg * const nord_qmp_gen5x16_mode_x8x8_cfgs[] = {
	&nord_qmp_pcie_port_a_cfg,
	&nord_qmp_pcie_port_bcd_cfg,
};

static const struct qmp_phy_cfg * const nord_qmp_gen5x16_mode_x8x4x4_cfgs[] = {
	&nord_qmp_pcie_port_a_cfg,
	&nord_qmp_pcie_port_b_cfg,
	&nord_qmp_pcie_port_cd_cfg,
};

static const struct qmp_phy_cfg * const nord_qmp_gen5x16_mode_x8x4x2x2_cfgs[] = {
	&nord_qmp_pcie_port_a_cfg,
	&nord_qmp_pcie_port_b_cfg,
	&nord_qmp_pcie_port_c_cfg,
	&nord_qmp_pcie_port_d_cfg,
};

static const struct qmp_pcie_link_mode_cfg nord_qmp_gen5x16_mode_cfgs[] = {
	[QMP_PCIE_NORD_MODE_X16] = {
		.cfgs		= nord_qmp_gen5x16_mode_x16_cfgs,
		.num_phys	= ARRAY_SIZE(nord_qmp_gen5x16_mode_x16_cfgs),
	},
	[QMP_PCIE_NORD_MODE_X8_X8] = {
		.cfgs		= nord_qmp_gen5x16_mode_x8x8_cfgs,
		.num_phys	= ARRAY_SIZE(nord_qmp_gen5x16_mode_x8x8_cfgs),
	},
	[QMP_PCIE_NORD_MODE_X8_X4_X4] = {
		.cfgs		= nord_qmp_gen5x16_mode_x8x4x4_cfgs,
		.num_phys	= ARRAY_SIZE(nord_qmp_gen5x16_mode_x8x4x4_cfgs),
	},
	[QMP_PCIE_NORD_MODE_X8_X4_X2_X2] = {
		.cfgs		= nord_qmp_gen5x16_mode_x8x4x2x2_cfgs,
		.num_phys	= ARRAY_SIZE(nord_qmp_gen5x16_mode_x8x4x2x2_cfgs),
	},
};

static const struct qmp_pcie_match_data nord_qmp_gen5x16_match_data = {
	.mode_cfgs		= nord_qmp_gen5x16_mode_cfgs,
	.num_modes		= ARRAY_SIZE(nord_qmp_gen5x16_mode_cfgs),
};

static void qmp_pcie_init_port_registers(struct qmp_pcie *qmp)
{
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	const struct qmp_pcie_offsets *offs = cfg->offsets;
	const struct qmp_phy_cfg_tbls *tbls = &cfg->tbls;
	int i;

	for (i = 0; i < cfg->num_regs; i++) {
		void __iomem *port = qmp->base[i];

		if (cfg->port_split)
			qmp_configure(qmp->dev, port + offs->txrxz,
				      tbls->port_split_pre_pwrup, tbls->port_split_pre_pwrup_num);

		writel(SW_PWRDN | REFCLK_DRV_DSBL,
		       port + offs->pcs + cfg->regs[QPHY_PCS_POWER_DOWN_CONTROL]);

		if (cfg->port_split)
			qphy_setbits(port + offs->pll, cfg->regs[QPHY_COM_BIAS_EN_CLKBUFLR_EN],
				     COM_BIAS_EN_MUX | COM_BIAS_EN);

		qmp_configure(qmp->dev, port + offs->pll,
			      tbls->pll_common, tbls->pll_common_num);
		qmp_configure(qmp->dev, port + offs->pll,
			      cfg->pll_tbls[i], cfg->pll_nums[i]);
		qmp_configure(qmp->dev, port + offs->txrxz,
			      tbls->txrxz, tbls->txrxz_num);
		qmp_configure(qmp->dev, port + offs->pcs,
			      tbls->pcs, tbls->pcs_num);
		qmp_configure(qmp->dev, port + offs->pcs_lanez,
			      tbls->pcs_lanez, tbls->pcs_lanez_num);

		if (cfg->port_split)
			qmp_configure(qmp->dev, port + offs->txrxz,
				      tbls->port_split_post_cfg, tbls->port_split_post_cfg_num);
	}
}

static int qmp_pcie_pd_power_on(struct qmp_pcie *qmp)
{
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	int i, ret;

	for (i = 0; i < cfg->num_pds; i++) {
		ret = pm_runtime_resume_and_get(qmp->pd_devs[i]);
		if (ret < 0) {
			dev_err(qmp->dev, "failed to power on %s domain: %d\n",
				cfg->pd_names[i], ret);
			goto err_power_off;
		}
	}

	return 0;

err_power_off:
	while (--i >= 0)
		pm_runtime_put(qmp->pd_devs[i]);

	return ret;
}

static void qmp_pcie_pd_power_off(struct qmp_pcie *qmp)
{
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	int i;

	for (i = cfg->num_pds - 1; i >= 0; i--)
		pm_runtime_put(qmp->pd_devs[i]);
}

static int qmp_pcie_init(struct phy *phy)
{
	struct qmp_pcie *qmp = phy_get_drvdata(phy);
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	const struct qmp_pcie_offsets *offs = cfg->offsets;
	int i, ret;

	ret = qmp_pcie_pd_power_on(qmp);
	if (ret)
		return ret;

	/*
	 * We can skip PHY initialization if all of the following conditions
	 * are met:
	 *  1. The PHY supports the nocsr_reset that preserves the PHY config.
	 *  2. The PHY was started (and not powered down again) by the
	 *     bootloader, with all of the expected bits set correctly.
	 * In this case, we can continue without having the init sequence
	 * defined in the driver.
	 */
	qmp->skip_init =  qmp->cfg->num_nocsr_resets;
	for (i = 0; qmp->skip_init && i < cfg->num_regs; i++) {
		void __iomem *port = qmp->base[i];

		if (!qphy_checkbits(port + offs->pcs, cfg->regs[QPHY_PCS_START_CONTROL],
				    SERDES_START | PCS_START) ||
		    !qphy_checkbits(port + offs->pcs, cfg->regs[QPHY_PCS_POWER_DOWN_CONTROL],
				     SW_PWRDN | REFCLK_DRV_DSBL))
			qmp->skip_init = false;
	}

	ret = regulator_bulk_enable(cfg->num_vregs, qmp->vregs);
	if (ret) {
		dev_err(qmp->dev, "failed to enable regulators: %d\n", ret);
		goto err_pd_power_off;
	}

	/*
	 * Toggle BCR reset for a PHY that doesn't support the nocsr_reset,
	 * or has not been initialized yet.
	 */
	if (!qmp->skip_init) {
		ret = reset_control_bulk_assert(cfg->num_resets, qmp->resets);
		if (ret) {
			dev_err(qmp->dev, "reset assert failed: %d\n", ret);
			goto err_disable_regulators;
		}
	}

	ret = reset_control_bulk_assert(qmp->cfg->num_nocsr_resets, qmp->nocsr_resets);
	if (ret) {
		dev_err(qmp->dev, "no-csr reset assert failed: %d\n", ret);
		goto err_assert_reset;
	}

	usleep_range(200, 300);

	if (!qmp->skip_init) {
		ret = reset_control_bulk_deassert(cfg->num_resets, qmp->resets);
		if (ret) {
			dev_err(qmp->dev, "reset deassert failed: %d\n", ret);
			goto err_assert_reset;
		}
	}

	ret = clk_bulk_prepare_enable(qmp->cfg->num_clks, qmp->clks);
	if (ret)
		goto err_assert_reset;

	return 0;

err_assert_reset:
	if (!qmp->skip_init)
		reset_control_bulk_assert(cfg->num_resets, qmp->resets);
err_disable_regulators:
	regulator_bulk_disable(cfg->num_vregs, qmp->vregs);
err_pd_power_off:
	qmp_pcie_pd_power_off(qmp);

	return ret;
}

static int qmp_pcie_exit(struct phy *phy)
{
	struct qmp_pcie *qmp = phy_get_drvdata(phy);
	const struct qmp_phy_cfg *cfg = qmp->cfg;

	if (qmp->nocsr_resets)
		reset_control_bulk_assert(qmp->cfg->num_nocsr_resets, qmp->nocsr_resets);

	clk_bulk_disable_unprepare(qmp->cfg->num_clks, qmp->clks);
	regulator_bulk_disable(cfg->num_vregs, qmp->vregs);
	qmp_pcie_pd_power_off(qmp);

	return 0;
}

static int qmp_pcie_power_on(struct phy *phy)
{
	struct qmp_pcie *qmp = phy_get_drvdata(phy);
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	const struct qmp_pcie_offsets *offs = cfg->offsets;
	void __iomem *status;
	unsigned int val;
	int i, ret;

	ret = clk_bulk_prepare_enable(qmp->cfg->num_pipe_clks, qmp->pipe_clks);
	if (ret)
		return ret;

	ret = reset_control_bulk_deassert(qmp->cfg->num_nocsr_resets, qmp->nocsr_resets);
	if (ret) {
		dev_err(qmp->dev, "no-csr reset deassert failed: %d\n", ret);
		goto err_disable_pipe_clk;
	}

	if (cfg->pll_tbls && !qmp->skip_init) {
		qmp_pcie_init_port_registers(qmp);

		for (i = 0; i < cfg->num_regs; i++) {
			qphy_clrbits(qmp->base[i] + offs->pcs,
				     cfg->regs[QPHY_PCS_SW_RESET], SW_RESET);
			qphy_setbits(qmp->base[i] + offs->pcs,
				     cfg->regs[QPHY_PCS_START_CONTROL],
				     SERDES_START | PCS_START);
		}

		usleep_range(2000, 2500);
	}

	for (i = 0; i < cfg->num_regs; i++) {
		status = qmp->base[i] + offs->pcs + cfg->regs[QPHY_PCS_STATUS];
		ret = readl_poll_timeout(status, val, !(val & cfg->phy_status), 200,
					 PHY_INIT_COMPLETE_TIMEOUT_US);
		if (ret) {
			dev_err(qmp->dev, "PHY power on timed-out (%s): %d\n",
				cfg->reg_names[i], ret);
			goto err_disable_pipe_clk;
		}
	}

	return 0;

err_disable_pipe_clk:
	clk_bulk_disable_unprepare(qmp->cfg->num_pipe_clks, qmp->pipe_clks);

	return ret;
}

static int qmp_pcie_power_off(struct phy *phy)
{
	struct qmp_pcie *qmp = phy_get_drvdata(phy);

	clk_bulk_disable_unprepare(qmp->cfg->num_pipe_clks, qmp->pipe_clks);

	return 0;
}

static int qmp_pcie_enable(struct phy *phy)
{
	int ret;

	ret = qmp_pcie_init(phy);
	if (ret)
		return ret;

	ret = qmp_pcie_power_on(phy);
	if (ret)
		qmp_pcie_exit(phy);

	return ret;
}

static int qmp_pcie_disable(struct phy *phy)
{
	int ret;

	ret = qmp_pcie_power_off(phy);
	if (ret)
		return ret;

	return qmp_pcie_exit(phy);
}

static const struct phy_ops qmp_pcie_phy_ops = {
	.power_on	= qmp_pcie_enable,
	.power_off	= qmp_pcie_disable,
	.owner		= THIS_MODULE,
};

static void qmp_pcie_pd_detach(void *data)
{
	struct qmp_pcie *qmp = data;
	int i;

	for (i = 0; i < qmp->cfg->num_pds; i++) {
		if (!IS_ERR_OR_NULL(qmp->pd_devs[i]))
			dev_pm_domain_detach(qmp->pd_devs[i], true);
	}
}

static int qmp_pcie_pd_init(struct qmp_pcie *qmp, struct phy *phy)
{
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	struct device *pd_dev = &phy->dev;
	int i, ret;

	if (!cfg->num_pds)
		return 0;

	qmp->pd_devs = devm_kcalloc(pd_dev, cfg->num_pds, sizeof(*qmp->pd_devs),
				    GFP_KERNEL);
	if (!qmp->pd_devs)
		return -ENOMEM;

	ret = devm_add_action_or_reset(pd_dev, qmp_pcie_pd_detach, qmp);
	if (ret)
		return ret;

	for (i = 0; i < cfg->num_pds; i++) {
		qmp->pd_devs[i] = dev_pm_domain_attach_by_name(pd_dev, cfg->pd_names[i]);
		if (IS_ERR_OR_NULL(qmp->pd_devs[i]))
			return PTR_ERR_OR_ZERO(qmp->pd_devs[i]) ? : -ENODATA;
	}

	return 0;
}

static int qmp_pcie_vreg_init(struct qmp_pcie *qmp)
{
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	struct device *dev = qmp->dev;
	int i;

	qmp->vregs = devm_kcalloc(dev, cfg->num_vregs, sizeof(*qmp->vregs),
				  GFP_KERNEL);
	if (!qmp->vregs)
		return -ENOMEM;

	for (i = 0; i < cfg->num_vregs; i++)
		qmp->vregs[i].supply = cfg->vreg_list[i];

	return devm_regulator_bulk_get(dev, cfg->num_vregs, qmp->vregs);
}

static int qmp_pcie_reset_init(struct qmp_pcie *qmp)
{
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	struct device *dev = qmp->dev;
	int i, ret;

	if (cfg->num_resets) {
		qmp->resets = devm_kcalloc(dev, cfg->num_resets,
					   sizeof(*qmp->resets), GFP_KERNEL);
		if (!qmp->resets)
			return -ENOMEM;

		for (i = 0; i < cfg->num_resets; i++)
			qmp->resets[i].id = cfg->reset_list[i];

		ret = devm_reset_control_bulk_get_exclusive(dev, cfg->num_resets,
							    qmp->resets);
		if (ret)
			return dev_err_probe(dev, ret, "failed to get resets\n");
	}

	if (cfg->num_nocsr_resets) {
		qmp->nocsr_resets = devm_kcalloc(dev, cfg->num_nocsr_resets,
						 sizeof(*qmp->nocsr_resets),
						 GFP_KERNEL);
		if (!qmp->nocsr_resets)
			return -ENOMEM;

		for (i = 0; i < cfg->num_nocsr_resets; i++)
			qmp->nocsr_resets[i].id = cfg->nocsr_reset_list[i];

		ret = devm_reset_control_bulk_get_exclusive(dev,
							    cfg->num_nocsr_resets,
							    qmp->nocsr_resets);
		if (ret)
			return dev_err_probe(dev, ret, "failed to get no-csr resets\n");
	}

	return 0;
}

static int qmp_pcie_clk_init(struct qmp_pcie *qmp)
{
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	struct device *dev = qmp->dev;
	int i, ret;

	qmp->clks = devm_kcalloc(dev, cfg->num_clks, sizeof(*qmp->clks),
				 GFP_KERNEL);
	if (!qmp->clks)
		return -ENOMEM;

	for (i = 0; i < cfg->num_clks; i++)
		qmp->clks[i].id = cfg->clk_list[i];

	ret = devm_clk_bulk_get_optional(dev, cfg->num_clks, qmp->clks);
	if (ret)
		return ret;

	qmp->pipe_clks = devm_kcalloc(dev, cfg->num_pipe_clks,
				      sizeof(*qmp->pipe_clks), GFP_KERNEL);
	if (!qmp->pipe_clks)
		return -ENOMEM;

	for (i = 0; i < cfg->num_pipe_clks; i++)
		qmp->pipe_clks[i].id = cfg->pipe_clk_list[i];

	return devm_clk_bulk_get_optional(dev, cfg->num_pipe_clks,
					  qmp->pipe_clks);
}

static int __phy_pipe_clk_register(struct device *dev, struct device_node *np,
				   int idx, struct clk_fixed_rate *fixed)
{
	struct clk_init_data init = { };
	int ret;

	ret = of_property_read_string_index(np, "clock-output-names", idx,
					    &init.name);
	if (ret) {
		dev_err(dev, "%pOFn: No clock-output-names\n", np);
		return ret;
	}

	init.ops = &clk_fixed_rate_ops;

	fixed->fixed_rate = 125000000;

	fixed->hw.init = &init;

	return devm_clk_hw_register(dev, &fixed->hw);
}

static struct clk_hw *
qmp_pcie_multiphy_clk_hw_get(struct of_phandle_args *clkspec, void *data)
{
	struct qmp_pcie_multiphy *qmp_data = data;
	unsigned int idx = 0;

	if (clkspec->args_count)
		idx = clkspec->args[0];

	if (idx < (unsigned int)qmp_data->num_pipe_outputs)
		return &qmp_data->pipe_out_clks[idx].hw;

	return ERR_PTR(-EINVAL);
}

static int qmp_pcie_multiphy_register_clocks(struct device *dev,
					     struct device_node *np,
					     struct qmp_pcie_multiphy *qmp_data)
{
	int num_pipe_outputs;
	int i, ret;

	num_pipe_outputs = of_property_count_strings(np, "clock-output-names");
	if (num_pipe_outputs < 0)
		return dev_err_probe(dev, num_pipe_outputs,
				     "failed to read clock-output-names\n");

	qmp_data->num_pipe_outputs = num_pipe_outputs;
	qmp_data->pipe_out_clks = devm_kcalloc(dev, num_pipe_outputs,
					       sizeof(*qmp_data->pipe_out_clks),
					       GFP_KERNEL);
	if (!qmp_data->pipe_out_clks)
		return -ENOMEM;

	for (i = 0; i < num_pipe_outputs; i++) {
		ret = __phy_pipe_clk_register(dev, np, i,
					      &qmp_data->pipe_out_clks[i]);
		if (ret)
			return ret;
	}

	return devm_of_clk_add_hw_provider(dev, qmp_pcie_multiphy_clk_hw_get, qmp_data);
}

static int qmp_pcie_get_mmio(struct qmp_pcie *qmp)
{
	struct platform_device *pdev = to_platform_device(qmp->dev);
	const struct qmp_phy_cfg *cfg = qmp->cfg;
	struct device *dev = qmp->dev;
	void __iomem *base;
	int i;

	qmp->base = devm_kcalloc(dev, cfg->num_regs, sizeof(*qmp->base),
				 GFP_KERNEL);
	if (!qmp->base)
		return -ENOMEM;

	for (i = 0; i < cfg->num_regs; i++) {
		base = devm_platform_ioremap_resource_byname(pdev, cfg->reg_names[i]);
		if (IS_ERR(base))
			return PTR_ERR(base);

		qmp->base[i] = base;
	}

	return 0;
}

static int qmp_pcie_read_link_mode(struct device *dev, unsigned int *link_mode)
{
	struct regmap *map;
	unsigned int args[1];

	map = syscon_regmap_lookup_by_phandle_args(dev->of_node, "qcom,link-mode",
						   ARRAY_SIZE(args), args);
	if (IS_ERR(map))
		return PTR_ERR(map);

	return regmap_read(map, args[0], link_mode);
}

static struct phy *qmp_pcie_multiphy_xlate(struct device *dev,
					   const struct of_phandle_args *args)
{
	struct qmp_pcie_multiphy *qmp_data = dev_get_drvdata(dev);
	unsigned int idx;

	if (!qmp_data || args->args_count < 1)
		return ERR_PTR(-EINVAL);

	idx = args->args[0];

	if (idx < (unsigned int)qmp_data->mode_cfg->num_phys)
		return qmp_data->phys[idx] ?: ERR_PTR(-EINVAL);

	return ERR_PTR(-EINVAL);
}

static int qmp_pcie_probe_phy(struct qmp_pcie *qmp, struct device_node *np,
			      struct phy **out_phy)
{
	struct phy *phy;
	int ret;

	ret = qmp_pcie_get_mmio(qmp);
	if (ret)
		return ret;

	ret = qmp_pcie_clk_init(qmp);
	if (ret)
		return ret;

	ret = qmp_pcie_reset_init(qmp);
	if (ret)
		return ret;

	ret = qmp_pcie_vreg_init(qmp);
	if (ret)
		return ret;

	phy = devm_phy_create(qmp->dev, np, &qmp_pcie_phy_ops);
	if (IS_ERR(phy))
		return PTR_ERR(phy);

	ret = qmp_pcie_pd_init(qmp, phy);
	if (ret)
		return ret;

	phy_set_drvdata(phy, qmp);
	*out_phy = phy;

	return 0;
}

static int qmp_pcie_multiphy_probe(struct platform_device *pdev)
{
	const struct qmp_pcie_match_data *match_data;
	struct qmp_pcie_multiphy *qmp_data;
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	unsigned int link_mode;
	struct qmp_pcie *qmp;
	struct phy **phys;
	int phy_index;
	int ret;

	qmp_data = devm_kzalloc(dev, sizeof(*qmp_data), GFP_KERNEL);
	if (!qmp_data)
		return -ENOMEM;

	match_data = of_device_get_match_data(dev);
	if (!match_data)
		return -EINVAL;

	ret = qmp_pcie_read_link_mode(dev, &link_mode);
	if (ret)
		return dev_err_probe(dev, ret, "failed to read qcom,link-mode\n");

	if (link_mode >= match_data->num_modes)
		return dev_err_probe(dev, -EINVAL, "invalid qcom,link-mode: %u\n",
				     link_mode);

	qmp_data->mode_cfg = &match_data->mode_cfgs[link_mode];

	qmp = devm_kcalloc(dev, qmp_data->mode_cfg->num_phys, sizeof(*qmp), GFP_KERNEL);
	if (!qmp)
		return -ENOMEM;

	phys = devm_kcalloc(dev, qmp_data->mode_cfg->num_phys, sizeof(*phys), GFP_KERNEL);
	if (!phys)
		return -ENOMEM;

	qmp_data->phys = phys;
	dev_set_drvdata(dev, qmp_data);

	for (phy_index = 0; phy_index < qmp_data->mode_cfg->num_phys; phy_index++) {
		qmp[phy_index].dev = dev;
		qmp[phy_index].cfg = qmp_data->mode_cfg->cfgs[phy_index];
		ret = qmp_pcie_probe_phy(&qmp[phy_index], dev->of_node, &phys[phy_index]);
		if (ret)
			return ret;
	}

	ret = qmp_pcie_multiphy_register_clocks(dev, dev->of_node, qmp_data);
	if (ret)
		return ret;

	phy_provider = devm_of_phy_provider_register(dev, qmp_pcie_multiphy_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static const struct of_device_id qmp_pcie_multiphy_of_match_table[] = {
	{
		.compatible = "qcom,glymur-qmp-gen5x8-pcie-phy",
		.data = &glymur_qmp_gen5x8_match_data,
	},
	{
		.compatible = "qcom,nord-qmp-gen5x16-pcie-phy",
		.data = &nord_qmp_gen5x16_match_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, qmp_pcie_multiphy_of_match_table);

static struct platform_driver qmp_pcie_multiphy_driver = {
	.probe		= qmp_pcie_multiphy_probe,
	.driver = {
		.name	= "qcom-qmp-pcie-multiphy",
		.of_match_table = qmp_pcie_multiphy_of_match_table,
	},
};
module_platform_driver(qmp_pcie_multiphy_driver);

MODULE_AUTHOR("Qiang Yu <qiang.yu@oss.qualcomm.com>");
MODULE_DESCRIPTION("Qualcomm QMP PCIe Multi-PHY driver");
MODULE_LICENSE("GPL");
