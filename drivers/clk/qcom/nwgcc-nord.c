// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,nord-nwgcc.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "clk-regmap-mux.h"
#include "common.h"
#include "reset.h"

enum {
	DT_BI_TCXO,
	DT_SLEEP_CLK,
};

enum {
	P_BI_TCXO,
	P_NW_GCC_GPLL0_OUT_EVEN,
	P_NW_GCC_GPLL0_OUT_MAIN,
	P_SLEEP_CLK,
};

static struct clk_alpha_pll nw_gcc_gpll0 = {
	.offset = 0x0,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.enable_reg = 0x0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_gpll0",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_ole_ops,
		},
	},
};

static const struct clk_div_table post_div_table_nw_gcc_gpll0_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv nw_gcc_gpll0_out_even = {
	.offset = 0x0,
	.post_div_shift = 10,
	.post_div_table = post_div_table_nw_gcc_gpll0_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_nw_gcc_gpll0_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nw_gcc_gpll0_out_even",
		.parent_hws = (const struct clk_hw*[]) {
			&nw_gcc_gpll0.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_lucid_ole_ops,
	},
};

static const struct parent_map nw_gcc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_NW_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_SLEEP_CLK, 5 },
	{ P_NW_GCC_GPLL0_OUT_EVEN, 6 },
};

static const struct clk_parent_data nw_gcc_parent_data_0[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &nw_gcc_gpll0.clkr.hw },
	{ .index = DT_SLEEP_CLK },
	{ .hw = &nw_gcc_gpll0_out_even.clkr.hw },
};

static const struct freq_tbl ftbl_nw_gcc_gp1_clk_src[] = {
	F(60000000, P_NW_GCC_GPLL0_OUT_MAIN, 10, 0, 0),
	F(100000000, P_NW_GCC_GPLL0_OUT_MAIN, 6, 0, 0),
	F(200000000, P_NW_GCC_GPLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 nw_gcc_gp1_clk_src = {
	.cmd_rcgr = 0x20004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = nw_gcc_parent_map_0,
	.freq_tbl = ftbl_nw_gcc_gp1_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nw_gcc_gp1_clk_src",
		.parent_data = nw_gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(nw_gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 nw_gcc_gp2_clk_src = {
	.cmd_rcgr = 0x21004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = nw_gcc_parent_map_0,
	.freq_tbl = ftbl_nw_gcc_gp1_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "nw_gcc_gp2_clk_src",
		.parent_data = nw_gcc_parent_data_0,
		.num_parents = ARRAY_SIZE(nw_gcc_parent_data_0),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_branch nw_gcc_acmu_mux_clk = {
	.halt_reg = 0x1f01c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1f01c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_acmu_mux_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_camera_hf_axi_clk = {
	.halt_reg = 0x16008,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x16008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x16008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_camera_hf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_camera_sf_axi_clk = {
	.halt_reg = 0x1601c,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x1601c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1601c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_camera_sf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_camera_trig_clk = {
	.halt_reg = 0x16034,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x16034,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x16034,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_camera_trig_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_disp_0_hf_axi_clk = {
	.halt_reg = 0x18008,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x18008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x18008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_disp_0_hf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_disp_0_trig_clk = {
	.halt_reg = 0x1801c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x1801c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1801c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_disp_0_trig_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_disp_1_hf_axi_clk = {
	.halt_reg = 0x19008,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x19008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x19008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_disp_1_hf_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_disp_1_trig_clk = {
	.halt_reg = 0x1901c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x1901c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1901c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_disp_1_trig_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_dprx0_axi_hf_clk = {
	.halt_reg = 0x29004,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x29004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x29004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_dprx0_axi_hf_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_dprx1_axi_hf_clk = {
	.halt_reg = 0x2a004,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x2a004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2a004,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_dprx1_axi_hf_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_eva_axi0_clk = {
	.halt_reg = 0x1b008,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x1b008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1b008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_eva_axi0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_eva_axi0c_clk = {
	.halt_reg = 0x1b01c,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x1b01c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1b01c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_eva_axi0c_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_eva_trig_clk = {
	.halt_reg = 0x1b028,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x1b028,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1b028,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_eva_trig_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_frq_measure_ref_clk = {
	.halt_reg = 0x1f008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1f008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_frq_measure_ref_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_gp1_clk = {
	.halt_reg = 0x20000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x20000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_gp1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&nw_gcc_gp1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_gp2_clk = {
	.halt_reg = 0x21000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x21000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_gp2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&nw_gcc_gp2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_gpu_2_gpll0_clk_src = {
	.halt_reg = 0x24150,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x24150,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x76000,
		.enable_mask = BIT(6),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_gpu_2_gpll0_clk_src",
			.parent_hws = (const struct clk_hw*[]) {
				&nw_gcc_gpll0.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_gpu_2_gpll0_div_clk_src = {
	.halt_reg = 0x24158,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x24158,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x76000,
		.enable_mask = BIT(7),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_gpu_2_gpll0_div_clk_src",
			.parent_hws = (const struct clk_hw*[]) {
				&nw_gcc_gpll0_out_even.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_gpu_2_hscnoc_gfx_clk = {
	.halt_reg = 0x2400c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2400c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2400c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_gpu_2_hscnoc_gfx_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_gpu_gpll0_clk_src = {
	.halt_reg = 0x23150,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x23150,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x76000,
		.enable_mask = BIT(4),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_gpu_gpll0_clk_src",
			.parent_hws = (const struct clk_hw*[]) {
				&nw_gcc_gpll0.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_gpu_gpll0_div_clk_src = {
	.halt_reg = 0x23158,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x23158,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x76000,
		.enable_mask = BIT(5),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_gpu_gpll0_div_clk_src",
			.parent_hws = (const struct clk_hw*[]) {
				&nw_gcc_gpll0_out_even.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_gpu_hscnoc_gfx_clk = {
	.halt_reg = 0x2300c,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x2300c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2300c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_gpu_hscnoc_gfx_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_gpu_smmu_vote_clk = {
	.halt_reg = 0x86038,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x86038,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_gpu_smmu_vote_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_hscnoc_gpu_2_axi_clk = {
	.halt_reg = 0x24160,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x24160,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x24160,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_hscnoc_gpu_2_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_hscnoc_gpu_axi_clk = {
	.halt_reg = 0x23160,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x23160,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x23160,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_hscnoc_gpu_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_mmu_1_tcu_vote_clk = {
	.halt_reg = 0x86040,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x86040,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_mmu_1_tcu_vote_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_video_axi0_clk = {
	.halt_reg = 0x1a008,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x1a008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1a008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_video_axi0_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_video_axi0c_clk = {
	.halt_reg = 0x1a01c,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x1a01c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1a01c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_video_axi0c_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch nw_gcc_video_axi1_clk = {
	.halt_reg = 0x1a030,
	.halt_check = BRANCH_HALT_SKIP,
	.hwcg_reg = 0x1a030,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x1a030,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "nw_gcc_video_axi1_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap *nw_gcc_nord_clocks[] = {
	[NW_GCC_ACMU_MUX_CLK] = &nw_gcc_acmu_mux_clk.clkr,
	[NW_GCC_CAMERA_HF_AXI_CLK] = &nw_gcc_camera_hf_axi_clk.clkr,
	[NW_GCC_CAMERA_SF_AXI_CLK] = &nw_gcc_camera_sf_axi_clk.clkr,
	[NW_GCC_CAMERA_TRIG_CLK] = &nw_gcc_camera_trig_clk.clkr,
	[NW_GCC_DISP_0_HF_AXI_CLK] = &nw_gcc_disp_0_hf_axi_clk.clkr,
	[NW_GCC_DISP_0_TRIG_CLK] = &nw_gcc_disp_0_trig_clk.clkr,
	[NW_GCC_DISP_1_HF_AXI_CLK] = &nw_gcc_disp_1_hf_axi_clk.clkr,
	[NW_GCC_DISP_1_TRIG_CLK] = &nw_gcc_disp_1_trig_clk.clkr,
	[NW_GCC_DPRX0_AXI_HF_CLK] = &nw_gcc_dprx0_axi_hf_clk.clkr,
	[NW_GCC_DPRX1_AXI_HF_CLK] = &nw_gcc_dprx1_axi_hf_clk.clkr,
	[NW_GCC_EVA_AXI0_CLK] = &nw_gcc_eva_axi0_clk.clkr,
	[NW_GCC_EVA_AXI0C_CLK] = &nw_gcc_eva_axi0c_clk.clkr,
	[NW_GCC_EVA_TRIG_CLK] = &nw_gcc_eva_trig_clk.clkr,
	[NW_GCC_FRQ_MEASURE_REF_CLK] = &nw_gcc_frq_measure_ref_clk.clkr,
	[NW_GCC_GP1_CLK] = &nw_gcc_gp1_clk.clkr,
	[NW_GCC_GP1_CLK_SRC] = &nw_gcc_gp1_clk_src.clkr,
	[NW_GCC_GP2_CLK] = &nw_gcc_gp2_clk.clkr,
	[NW_GCC_GP2_CLK_SRC] = &nw_gcc_gp2_clk_src.clkr,
	[NW_GCC_GPLL0] = &nw_gcc_gpll0.clkr,
	[NW_GCC_GPLL0_OUT_EVEN] = &nw_gcc_gpll0_out_even.clkr,
	[NW_GCC_GPU_2_GPLL0_CLK_SRC] = &nw_gcc_gpu_2_gpll0_clk_src.clkr,
	[NW_GCC_GPU_2_GPLL0_DIV_CLK_SRC] = &nw_gcc_gpu_2_gpll0_div_clk_src.clkr,
	[NW_GCC_GPU_2_HSCNOC_GFX_CLK] = &nw_gcc_gpu_2_hscnoc_gfx_clk.clkr,
	[NW_GCC_GPU_GPLL0_CLK_SRC] = &nw_gcc_gpu_gpll0_clk_src.clkr,
	[NW_GCC_GPU_GPLL0_DIV_CLK_SRC] = &nw_gcc_gpu_gpll0_div_clk_src.clkr,
	[NW_GCC_GPU_HSCNOC_GFX_CLK] = &nw_gcc_gpu_hscnoc_gfx_clk.clkr,
	[NW_GCC_GPU_SMMU_VOTE_CLK] = &nw_gcc_gpu_smmu_vote_clk.clkr,
	[NW_GCC_HSCNOC_GPU_2_AXI_CLK] = &nw_gcc_hscnoc_gpu_2_axi_clk.clkr,
	[NW_GCC_HSCNOC_GPU_AXI_CLK] = &nw_gcc_hscnoc_gpu_axi_clk.clkr,
	[NW_GCC_MMU_1_TCU_VOTE_CLK] = &nw_gcc_mmu_1_tcu_vote_clk.clkr,
	[NW_GCC_VIDEO_AXI0_CLK] = &nw_gcc_video_axi0_clk.clkr,
	[NW_GCC_VIDEO_AXI0C_CLK] = &nw_gcc_video_axi0c_clk.clkr,
	[NW_GCC_VIDEO_AXI1_CLK] = &nw_gcc_video_axi1_clk.clkr,
};

static const struct qcom_reset_map nw_gcc_nord_resets[] = {
	[NW_GCC_CAMERA_BCR] = { 0x16000 },
	[NW_GCC_DISPLAY_0_BCR] = { 0x18000 },
	[NW_GCC_DISPLAY_1_BCR] = { 0x19000 },
	[NW_GCC_DPRX0_BCR] = { 0x29000 },
	[NW_GCC_DPRX1_BCR] = { 0x2a000 },
	[NW_GCC_EVA_BCR] = { 0x1b000 },
	[NW_GCC_GPU_2_BCR] = { 0x24000 },
	[NW_GCC_GPU_BCR] = { 0x23000 },
	[NW_GCC_VIDEO_BCR] = { 0x1a000 },
};

static const u32 nw_gcc_nord_critical_cbcrs[] = {
	0x16004, /* NW_GCC_CAMERA_AHB_CLK */
	0x16030, /* NW_GCC_CAMERA_XO_CLK */
	0x18004, /* NW_GCC_DISP_0_AHB_CLK */
	0x19004, /* NW_GCC_DISP_1_AHB_CLK */
	0x29018, /* NW_GCC_DPRX0_CFG_AHB_CLK */
	0x2a018, /* NW_GCC_DPRX1_CFG_AHB_CLK */
	0x1b004, /* NW_GCC_EVA_AHB_CLK */
	0x1b024, /* NW_GCC_EVA_XO_CLK */
	0x23004, /* NW_GCC_GPU_CFG_AHB_CLK */
	0x24004, /* NW_GCC_GPU_2_CFG_AHB_CLK */
	0x1a004, /* NW_GCC_VIDEO_AHB_CLK */
	0x1a044, /* NW_GCC_VIDEO_XO_CLK */
};

static const struct qcom_cc_driver_data nw_gcc_nord_driver_data = {
	.clk_cbcrs = nw_gcc_nord_critical_cbcrs,
	.num_clk_cbcrs = ARRAY_SIZE(nw_gcc_nord_critical_cbcrs),
};

static const struct regmap_config nw_gcc_nord_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xf41f0,
	.fast_io = true,
};

static const struct qcom_cc_desc nw_gcc_nord_desc = {
	.config = &nw_gcc_nord_regmap_config,
	.clks = nw_gcc_nord_clocks,
	.num_clks = ARRAY_SIZE(nw_gcc_nord_clocks),
	.resets = nw_gcc_nord_resets,
	.num_resets = ARRAY_SIZE(nw_gcc_nord_resets),
	.driver_data = &nw_gcc_nord_driver_data,
};

static const struct of_device_id nw_gcc_nord_match_table[] = {
	{ .compatible = "qcom,nord-nwgcc" },
	{ }
};
MODULE_DEVICE_TABLE(of, nw_gcc_nord_match_table);

static int nw_gcc_nord_probe(struct platform_device *pdev)
{
	return qcom_cc_probe(pdev, &nw_gcc_nord_desc);
}

static struct platform_driver nw_gcc_nord_driver = {
	.probe = nw_gcc_nord_probe,
	.driver = {
		.name = "nwgcc-nord",
		.of_match_table = nw_gcc_nord_match_table,
	},
};

module_platform_driver(nw_gcc_nord_driver);

MODULE_DESCRIPTION("QTI NWGCC NORD Driver");
MODULE_LICENSE("GPL");
