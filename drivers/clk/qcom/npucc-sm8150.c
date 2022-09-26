// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,npucc-sm8150.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "common.h"
#include "reset.h"
#include "vdd-level-sm8150.h"
#include "clk-pm.h"

#define CRC_SID_FSM_CTRL		0x100c
#define CRC_SID_FSM_CTRL_SETTING	0x800000
#define CRC_MND_CFG			0x1010
#define CRC_MND_CFG_SETTING		0x15010

static DEFINE_VDD_REGULATORS(vdd_cx, VDD_NUM, 1, vdd_corner);

static struct clk_vdd_class *npu_cc_sm8150_regulators[] = {
	&vdd_cx,
};

enum {
	P_BI_TCXO,
	P_GPLL0_OUT_MAIN,
	P_GPLL0_OUT_MAIN_DIV,
	P_NPU_CC_PLL0_OUT_EVEN,
	P_NPU_CC_PLL1_OUT_EVEN,
	P_NPU_CC_CRC_DIV,
};

static struct pll_vco trion_vco[] = {
	{ 249600000, 2000000000, 0 },
};

/* 600MHz configuration */
static struct alpha_pll_config npu_cc_pll0_config = {
	.l = 0x1F,
	.alpha = 0x4000,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00002267,
	.config_ctl_hi1_val = 0x00000024,
	.test_ctl_val = 0x00000000,
	.test_ctl_hi_val = 0x00000000,
	.test_ctl_hi1_val = 0x00000020,
	.user_ctl_val = 0x00000000,
	.user_ctl_hi_val = 0x00000805,
	.user_ctl_hi1_val = 0x000000D0,
};

static struct clk_alpha_pll npu_cc_pll0 = {
	.offset = 0x0,
	.vco_table = trion_vco,
	.num_vco = ARRAY_SIZE(trion_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TRION],
	.config = &npu_cc_pll0_config,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "npu_cc_pll0",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_trion_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_cx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_MIN] = 615000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1600000000,
				[VDD_NOMINAL] = 2000000000},
		},
	},
};

/* 800MHz configuration */
static struct alpha_pll_config npu_cc_pll1_config = {
	.l = 0x29,
	.alpha = 0xAAAA,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x00002267,
	.config_ctl_hi1_val = 0x00000024,
	.test_ctl_val = 0x00000000,
	.test_ctl_hi_val = 0x00000000,
	.test_ctl_hi1_val = 0x00000020,
	.user_ctl_val = 0x00000000,
	.user_ctl_hi_val = 0x00000805,
	.user_ctl_hi1_val = 0x000000D0,
};

static struct clk_alpha_pll npu_cc_pll1 = {
	.offset = 0x400,
	.vco_table = trion_vco,
	.num_vco = ARRAY_SIZE(trion_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_TRION],
	.config = &npu_cc_pll1_config,
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "npu_cc_pll1",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_trion_ops,
		},
		.vdd_data = {
			.vdd_class = &vdd_cx,
			.num_rate_max = VDD_NUM,
			.rate_max = (unsigned long[VDD_NUM]) {
				[VDD_MIN] = 615000000,
				[VDD_LOW] = 1066000000,
				[VDD_LOW_L1] = 1600000000,
				[VDD_NOMINAL] = 2000000000},
		},
	},
};

static const struct parent_map npu_cc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_NPU_CC_PLL1_OUT_EVEN, 1 },
	{ P_NPU_CC_PLL0_OUT_EVEN, 2 },
	{ P_GPLL0_OUT_MAIN, 4 },
	{ P_GPLL0_OUT_MAIN_DIV, 5 },
};

static const struct clk_parent_data npu_cc_parent_data_0[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &npu_cc_pll1.clkr.hw },
	{ .hw = &npu_cc_pll0.clkr.hw },
	{ .fw_name = "gcc_npu_gpll0_clk_src" },
	{ .fw_name = "gcc_npu_gpll0_div_clk_src" },
};

static struct clk_fixed_factor npu_cc_crc_div = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "npu_cc_crc_div",
		.parent_data = &(const struct clk_parent_data){
				.hw = &npu_cc_pll0.clkr.hw,
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_fixed_factor_ops,
	},
};

static const struct parent_map npu_cc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_NPU_CC_PLL1_OUT_EVEN, 1 },
	{ P_NPU_CC_CRC_DIV, 2 },
	{ P_GPLL0_OUT_MAIN, 4 },
	{ P_GPLL0_OUT_MAIN_DIV, 5 },
};

static const struct clk_parent_data npu_cc_parent_data_1[] = {
	{ .fw_name = "bi_tcxo" },
	{ .hw = &npu_cc_pll1.clkr.hw },
	{ .hw = &npu_cc_crc_div.hw },
	{ .fw_name = "gcc_npu_gpll0_clk_src" },
	{ .fw_name = "gcc_npu_gpll0_div_clk_src" },
};

static const struct freq_tbl ftbl_npu_cc_cal_dp_clk_src[] = {
	F(300000000, P_NPU_CC_CRC_DIV, 1, 0, 0),
	F(400000000, P_NPU_CC_CRC_DIV, 1, 0, 0),
	F(487000000, P_NPU_CC_CRC_DIV, 1, 0, 0),
	F(652000000, P_NPU_CC_CRC_DIV, 1, 0, 0),
	F(811000000, P_NPU_CC_CRC_DIV, 1, 0, 0),
	F(908000000, P_NPU_CC_CRC_DIV, 1, 0, 0),
	{ }
};

static struct clk_rcg2 npu_cc_cal_dp_clk_src = {
	.cmd_rcgr = 0x1004,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = npu_cc_parent_map_1,
	.freq_tbl = ftbl_npu_cc_cal_dp_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "npu_cc_cal_dp_clk_src",
		.parent_data = npu_cc_parent_data_1,
		.num_parents = ARRAY_SIZE(npu_cc_parent_data_1),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 300000000,
			[VDD_LOW] = 400000000,
			[VDD_LOW_L1] = 487000000,
			[VDD_NOMINAL] = 652000000,
			[VDD_HIGH] = 811000000,
			[VDD_HIGH] = 908000000},
	},
};

static const struct freq_tbl ftbl_npu_cc_npu_core_clk_src[] = {
	F(60000000, P_GPLL0_OUT_MAIN_DIV, 5, 0, 0),
	F(100000000, P_GPLL0_OUT_MAIN, 6, 0, 0),
	F(150000000, P_GPLL0_OUT_MAIN, 4, 0, 0),
	F(200000000, P_NPU_CC_PLL1_OUT_EVEN, 4, 0, 0),
	F(300000000, P_GPLL0_OUT_MAIN, 2, 0, 0),
	F(400000000, P_NPU_CC_PLL1_OUT_EVEN, 2, 0, 0),
	{ }
};

static struct clk_rcg2 npu_cc_npu_core_clk_src = {
	.cmd_rcgr = 0x1030,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = npu_cc_parent_map_0,
	.freq_tbl = ftbl_npu_cc_npu_core_clk_src,
	.enable_safe_config = true,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "npu_cc_npu_core_clk_src",
		.parent_data = npu_cc_parent_data_0,
		.num_parents = ARRAY_SIZE(npu_cc_parent_data_0),
		.ops = &clk_rcg2_ops,
	},
	.clkr.vdd_data = {
		.vdd_class = &vdd_cx,
		.num_rate_max = VDD_NUM,
		.rate_max = (unsigned long[VDD_NUM]) {
			[VDD_LOWER] = 100000000,
			[VDD_LOW] = 150000000,
			[VDD_LOW_L1] = 200000000,
			[VDD_NOMINAL] = 300000000,
			[VDD_HIGH] = 400000000},
	},
};

static struct clk_branch npu_cc_armwic_core_clk = {
	.halt_reg = 0x1058,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1058,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "npu_cc_armwic_core_clk",
			.parent_hws = (const struct clk_hw*[]){
				&npu_cc_npu_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

/* CLK_DONT_HOLD_STATE flag is needed due to sync_state */
static struct clk_branch npu_cc_bto_core_clk = {
	.halt_reg = 0x1090,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1090,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "npu_cc_bto_core_clk",
			.flags = CLK_DONT_HOLD_STATE,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch npu_cc_bwmon_clk = {
	.halt_reg = 0x1088,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1088,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "npu_cc_bwmon_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch npu_cc_cal_dp_cdc_clk = {
	.halt_reg = 0x1068,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1068,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "npu_cc_cal_dp_cdc_clk",
			.parent_hws = (const struct clk_hw*[]){
				&npu_cc_cal_dp_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch npu_cc_cal_dp_clk = {
	.halt_reg = 0x101c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x101c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "npu_cc_cal_dp_clk",
			.parent_hws = (const struct clk_hw*[]){
				&npu_cc_cal_dp_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch npu_cc_comp_noc_axi_clk = {
	.halt_reg = 0x106c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x106c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "npu_cc_comp_noc_axi_clk",
			.parent_data = &(const struct clk_parent_data){
				.fw_name = "gcc_npu_axi_clk",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch npu_cc_conf_noc_ahb_clk = {
	.halt_reg = 0x1074,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1074,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "npu_cc_conf_noc_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch npu_cc_npu_core_apb_clk = {
	.halt_reg = 0x1080,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1080,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "npu_cc_npu_core_apb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch npu_cc_npu_core_atb_clk = {
	.halt_reg = 0x1078,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1078,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "npu_cc_npu_core_atb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch npu_cc_npu_core_clk = {
	.halt_reg = 0x1048,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1048,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "npu_cc_npu_core_clk",
			.parent_hws = (const struct clk_hw*[]){
				&npu_cc_npu_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch npu_cc_npu_core_cti_clk = {
	.halt_reg = 0x107c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x107c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "npu_cc_npu_core_cti_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch npu_cc_npu_cpc_clk = {
	.halt_reg = 0x1050,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1050,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "npu_cc_npu_cpc_clk",
			.parent_hws = (const struct clk_hw*[]){
				&npu_cc_npu_core_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch npu_cc_npu_cpc_timer_clk = {
	.halt_reg = 0x105c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x105c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "npu_cc_npu_cpc_timer_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch npu_cc_perf_cnt_clk = {
	.halt_reg = 0x108c,
	.halt_check = BRANCH_HALT_DELAY,
	.clkr = {
		.enable_reg = 0x108c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "npu_cc_perf_cnt_clk",
			.parent_hws = (const struct clk_hw*[]){
				&npu_cc_cal_dp_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch npu_cc_qtimer_core_clk = {
	.halt_reg = 0x1060,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1060,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "npu_cc_qtimer_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch npu_cc_sleep_clk = {
	.halt_reg = 0x1064,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1064,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "npu_cc_sleep_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct critical_clk_offset critical_clk_list[] = {
	{ .offset = 0x3020, .mask = BIT(0) },
};

static struct clk_regmap *npu_cc_sm8150_clocks[] = {
	[NPU_CC_PLL0] = &npu_cc_pll0.clkr,
	[NPU_CC_PLL1] = &npu_cc_pll1.clkr,
	[NPU_CC_ARMWIC_CORE_CLK] = &npu_cc_armwic_core_clk.clkr,
	[NPU_CC_BTO_CORE_CLK] = &npu_cc_bto_core_clk.clkr,
	[NPU_CC_BWMON_CLK] = &npu_cc_bwmon_clk.clkr,
	[NPU_CC_CAL_DP_CDC_CLK] = &npu_cc_cal_dp_cdc_clk.clkr,
	[NPU_CC_CAL_DP_CLK] = &npu_cc_cal_dp_clk.clkr,
	[NPU_CC_CAL_DP_CLK_SRC] = &npu_cc_cal_dp_clk_src.clkr,
	[NPU_CC_COMP_NOC_AXI_CLK] = &npu_cc_comp_noc_axi_clk.clkr,
	[NPU_CC_CONF_NOC_AHB_CLK] = &npu_cc_conf_noc_ahb_clk.clkr,
	[NPU_CC_NPU_CORE_APB_CLK] = &npu_cc_npu_core_apb_clk.clkr,
	[NPU_CC_NPU_CORE_ATB_CLK] = &npu_cc_npu_core_atb_clk.clkr,
	[NPU_CC_NPU_CORE_CLK] = &npu_cc_npu_core_clk.clkr,
	[NPU_CC_NPU_CORE_CLK_SRC] = &npu_cc_npu_core_clk_src.clkr,
	[NPU_CC_NPU_CORE_CTI_CLK] = &npu_cc_npu_core_cti_clk.clkr,
	[NPU_CC_NPU_CPC_CLK] = &npu_cc_npu_cpc_clk.clkr,
	[NPU_CC_NPU_CPC_TIMER_CLK] = &npu_cc_npu_cpc_timer_clk.clkr,
	[NPU_CC_PERF_CNT_CLK] = &npu_cc_perf_cnt_clk.clkr,
	[NPU_CC_QTIMER_CORE_CLK] = &npu_cc_qtimer_core_clk.clkr,
	[NPU_CC_SLEEP_CLK] = &npu_cc_sleep_clk.clkr,
};

static const struct qcom_reset_map npu_cc_sm8150_resets[] = {
	[NPU_CC_CAL_DP_BCR] = { 0x1000 },
	[NPU_CC_NPU_CORE_BCR] = { 0x1024 },
};

static const struct regmap_config npu_cc_sm8150_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0x8000,
	.fast_io = true,
};

static struct qcom_cc_desc npu_cc_sm8150_desc = {
	.config = &npu_cc_sm8150_regmap_config,
	.clks = npu_cc_sm8150_clocks,
	.num_clks = ARRAY_SIZE(npu_cc_sm8150_clocks),
	.resets = npu_cc_sm8150_resets,
	.num_resets = ARRAY_SIZE(npu_cc_sm8150_resets),
	.clk_regulators = npu_cc_sm8150_regulators,
	.num_clk_regulators = ARRAY_SIZE(npu_cc_sm8150_regulators),
	.critical_clk_en = critical_clk_list,
	.num_critical_clk = ARRAY_SIZE(critical_clk_list),
};

static const struct of_device_id npu_cc_sm8150_match_table[] = {
	{ .compatible = "qcom,sm8150-npucc" },
	{ .compatible = "qcom,sa8155-npucc" },
	{ }
};
MODULE_DEVICE_TABLE(of, npu_cc_sm8150_match_table);

static struct regulator *vdd_gdsc;

static int enable_npu_crc(struct regmap *regmap)
{
	int ret;

	/* Set npu_cc_cal_cp_clk to the lowest supported frequency */
	clk_set_rate(npu_cc_cal_dp_clk.clkr.hw.clk,
		clk_round_rate(npu_cc_cal_dp_clk_src.clkr.hw.clk, 1));

	/* Turn on the NPU GDSC */
	ret = regulator_enable(vdd_gdsc);
	if (ret) {
		pr_err("Failed to enable the NPU GDSC during CRC sequence\n");
		return ret;
	}

	/* Enable npu_cc_cal_cp_clk */
	ret = clk_prepare_enable(npu_cc_cal_dp_clk.clkr.hw.clk);
	if (ret) {
		pr_err("Failed to enable npu_cc_cal_dp_clk during CRC sequence\n");
		return ret;
	}

	/* Enable MND RC */
	regmap_write(regmap, CRC_MND_CFG, CRC_MND_CFG_SETTING);
	regmap_write(regmap, CRC_SID_FSM_CTRL, CRC_SID_FSM_CTRL_SETTING);

	/* Wait for 16 cycles before continuing */
	udelay(1);

	/* Disable npu_cc_cal_cp_clk */
	clk_disable_unprepare(npu_cc_cal_dp_clk.clkr.hw.clk);

	/* Turn off the NPU GDSC */
	regulator_disable(vdd_gdsc);

	return ret;
}

static int npu_cc_sm8150_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	int ret;

	vdd_gdsc = devm_regulator_get(&pdev->dev, "vdd_gdsc");
	if (IS_ERR(vdd_gdsc)) {
		if (!(PTR_ERR(vdd_gdsc) == -EPROBE_DEFER))
			dev_err(&pdev->dev,
				"Unable to get vdd_gdsc regulator\n");
		return PTR_ERR(vdd_gdsc);
	}

	regmap = qcom_cc_map(pdev, &npu_cc_sm8150_desc);
	if (IS_ERR(regmap)) {
		pr_err("Failed to map the npu CC registers\n");
		return PTR_ERR(regmap);
	}

	clk_trion_pll_configure(&npu_cc_pll0, regmap, npu_cc_pll0.config);
	clk_trion_pll_configure(&npu_cc_pll1, regmap, npu_cc_pll1.config);

	/*
	 * Keep clocks always enabled:
	 * npu_cc_xo_clk
	 */
	regmap_update_bits(regmap, 0x3020, BIT(0), BIT(0));

	/* Register the fixed factor clock for CRC divide */
	ret = devm_clk_hw_register(&pdev->dev, &npu_cc_crc_div.hw);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register CRC divide clock\n");
		return ret;
	}

	ret = qcom_cc_really_probe(pdev, &npu_cc_sm8150_desc, regmap);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register NPU CC clocks\n");
		return ret;
	}

	ret = enable_npu_crc(regmap);
	if (ret) {
		dev_err(&pdev->dev, "Failed to enable CRC for NPU cal RCG\n");
		return ret;
	}

	ret = register_qcom_clks_pm(pdev, false, &npu_cc_sm8150_desc);
	if (ret)
		dev_err(&pdev->dev, "Failed to register for pm ops\n");

	dev_info(&pdev->dev, "Registered NPU CC clocks\n");

	return ret;
}

static void npu_cc_sm8150_sync_state(struct device *dev)
{
	qcom_cc_sync_state(dev, &npu_cc_sm8150_desc);
}

static struct platform_driver npu_cc_sm8150_driver = {
	.probe = npu_cc_sm8150_probe,
	.driver = {
		.name = "npu_cc-sm8150",
		.of_match_table = npu_cc_sm8150_match_table,
		.sync_state = npu_cc_sm8150_sync_state,
	},
};

static int __init npu_cc_sm8150_init(void)
{
	return platform_driver_register(&npu_cc_sm8150_driver);
}
subsys_initcall(npu_cc_sm8150_init);

static void __exit npu_cc_sm8150_exit(void)
{
	platform_driver_unregister(&npu_cc_sm8150_driver);
}
module_exit(npu_cc_sm8150_exit);

MODULE_DESCRIPTION("QTI NPU_CC SM8150 Driver");
MODULE_LICENSE("GPL");
