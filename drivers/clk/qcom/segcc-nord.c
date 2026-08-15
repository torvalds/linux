// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <dt-bindings/clock/qcom,nord-segcc.h>

#include "clk-alpha-pll.h"
#include "clk-branch.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-regmap.h"
#include "clk-regmap-divider.h"
#include "common.h"
#include "gdsc.h"
#include "reset.h"

enum {
	DT_BI_TCXO,
	DT_SLEEP_CLK,
};

enum {
	P_BI_TCXO,
	P_SE_GCC_GPLL0_OUT_EVEN,
	P_SE_GCC_GPLL0_OUT_MAIN,
	P_SE_GCC_GPLL2_OUT_MAIN,
	P_SE_GCC_GPLL4_OUT_MAIN,
	P_SE_GCC_GPLL5_OUT_MAIN,
	P_SLEEP_CLK,
};

static struct clk_alpha_pll se_gcc_gpll0 = {
	.offset = 0x0,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.enable_reg = 0x0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_gpll0",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_ole_ops,
		},
	},
};

static const struct clk_div_table post_div_table_se_gcc_gpll0_out_even[] = {
	{ 0x1, 2 },
	{ }
};

static struct clk_alpha_pll_postdiv se_gcc_gpll0_out_even = {
	.offset = 0x0,
	.post_div_shift = 10,
	.post_div_table = post_div_table_se_gcc_gpll0_out_even,
	.num_post_div = ARRAY_SIZE(post_div_table_se_gcc_gpll0_out_even),
	.width = 4,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "se_gcc_gpll0_out_even",
		.parent_hws = (const struct clk_hw*[]) {
			&se_gcc_gpll0.clkr.hw,
		},
		.num_parents = 1,
		.ops = &clk_alpha_pll_postdiv_lucid_ole_ops,
	},
};

static struct clk_alpha_pll se_gcc_gpll2 = {
	.offset = 0x2000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.enable_reg = 0x0,
		.enable_mask = BIT(2),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_gpll2",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_ole_ops,
		},
	},
};

static struct clk_alpha_pll se_gcc_gpll4 = {
	.offset = 0x4000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.enable_reg = 0x0,
		.enable_mask = BIT(4),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_gpll4",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_ole_ops,
		},
	},
};

static struct clk_alpha_pll se_gcc_gpll5 = {
	.offset = 0x5000,
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID_OLE],
	.clkr = {
		.enable_reg = 0x0,
		.enable_mask = BIT(5),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_gpll5",
			.parent_data = &(const struct clk_parent_data) {
				.index = DT_BI_TCXO,
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_fixed_lucid_ole_ops,
		},
	},
};

static const struct parent_map se_gcc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_SE_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_SE_GCC_GPLL0_OUT_EVEN, 2 },
};

static const struct clk_parent_data se_gcc_parent_data_0[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &se_gcc_gpll0.clkr.hw },
	{ .hw = &se_gcc_gpll0_out_even.clkr.hw },
};

static const struct parent_map se_gcc_parent_map_1[] = {
	{ P_BI_TCXO, 0 },
	{ P_SE_GCC_GPLL0_OUT_MAIN, 1 },
};

static const struct clk_parent_data se_gcc_parent_data_1[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &se_gcc_gpll0.clkr.hw },
};

static const struct parent_map se_gcc_parent_map_2[] = {
	{ P_BI_TCXO, 0 },
	{ P_SE_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_SLEEP_CLK, 5 },
};

static const struct clk_parent_data se_gcc_parent_data_2[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &se_gcc_gpll0.clkr.hw },
	{ .index = DT_SLEEP_CLK },
};

static const struct parent_map se_gcc_parent_map_3[] = {
	{ P_BI_TCXO, 0 },
	{ P_SE_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_SE_GCC_GPLL5_OUT_MAIN, 3 },
	{ P_SE_GCC_GPLL4_OUT_MAIN, 5 },
	{ P_SE_GCC_GPLL2_OUT_MAIN, 6 },
};

static const struct clk_parent_data se_gcc_parent_data_3[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &se_gcc_gpll0.clkr.hw },
	{ .hw = &se_gcc_gpll5.clkr.hw },
	{ .hw = &se_gcc_gpll4.clkr.hw },
	{ .hw = &se_gcc_gpll2.clkr.hw },
};

static const struct parent_map se_gcc_parent_map_4[] = {
	{ P_BI_TCXO, 0 },
	{ P_SE_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_SE_GCC_GPLL0_OUT_EVEN, 2 },
	{ P_SLEEP_CLK, 5 },
};

static const struct clk_parent_data se_gcc_parent_data_4[] = {
	{ .index = DT_BI_TCXO },
	{ .hw = &se_gcc_gpll0.clkr.hw },
	{ .hw = &se_gcc_gpll0_out_even.clkr.hw },
	{ .index = DT_SLEEP_CLK },
};

static const struct freq_tbl ftbl_se_gcc_eee_emac0_clk_src[] = {
	F(66666667, P_SE_GCC_GPLL0_OUT_MAIN, 9, 0, 0),
	F(100000000, P_SE_GCC_GPLL0_OUT_MAIN, 6, 0, 0),
	{ }
};

static struct clk_rcg2 se_gcc_eee_emac0_clk_src = {
	.cmd_rcgr = 0x240b8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = se_gcc_parent_map_2,
	.freq_tbl = ftbl_se_gcc_eee_emac0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "se_gcc_eee_emac0_clk_src",
		.parent_data = se_gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(se_gcc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 se_gcc_eee_emac1_clk_src = {
	.cmd_rcgr = 0x250b8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = se_gcc_parent_map_2,
	.freq_tbl = ftbl_se_gcc_eee_emac0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "se_gcc_eee_emac1_clk_src",
		.parent_data = se_gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(se_gcc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_se_gcc_emac0_phy_aux_clk_src[] = {
	F(19200000, P_BI_TCXO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 se_gcc_emac0_phy_aux_clk_src = {
	.cmd_rcgr = 0x24030,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = se_gcc_parent_map_2,
	.freq_tbl = ftbl_se_gcc_emac0_phy_aux_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "se_gcc_emac0_phy_aux_clk_src",
		.parent_data = se_gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(se_gcc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_se_gcc_emac0_ptp_clk_src[] = {
	F(150000000, P_SE_GCC_GPLL0_OUT_MAIN, 4, 0, 0),
	F(250000000, P_SE_GCC_GPLL5_OUT_MAIN, 4, 0, 0),
	{ }
};

static struct clk_rcg2 se_gcc_emac0_ptp_clk_src = {
	.cmd_rcgr = 0x24084,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = se_gcc_parent_map_3,
	.freq_tbl = ftbl_se_gcc_emac0_ptp_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "se_gcc_emac0_ptp_clk_src",
		.parent_data = se_gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(se_gcc_parent_data_3),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_se_gcc_emac0_rgmii_clk_src[] = {
	F(75000000, P_SE_GCC_GPLL0_OUT_MAIN, 8, 0, 0),
	F(120000000, P_SE_GCC_GPLL0_OUT_MAIN, 5, 0, 0),
	F(250000000, P_SE_GCC_GPLL5_OUT_MAIN, 4, 0, 0),
	{ }
};

static struct clk_rcg2 se_gcc_emac0_rgmii_clk_src = {
	.cmd_rcgr = 0x2406c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = se_gcc_parent_map_3,
	.freq_tbl = ftbl_se_gcc_emac0_rgmii_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "se_gcc_emac0_rgmii_clk_src",
		.parent_data = se_gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(se_gcc_parent_data_3),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 se_gcc_emac1_phy_aux_clk_src = {
	.cmd_rcgr = 0x25030,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = se_gcc_parent_map_2,
	.freq_tbl = ftbl_se_gcc_emac0_phy_aux_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "se_gcc_emac1_phy_aux_clk_src",
		.parent_data = se_gcc_parent_data_2,
		.num_parents = ARRAY_SIZE(se_gcc_parent_data_2),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 se_gcc_emac1_ptp_clk_src = {
	.cmd_rcgr = 0x25084,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = se_gcc_parent_map_3,
	.freq_tbl = ftbl_se_gcc_emac0_ptp_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "se_gcc_emac1_ptp_clk_src",
		.parent_data = se_gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(se_gcc_parent_data_3),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 se_gcc_emac1_rgmii_clk_src = {
	.cmd_rcgr = 0x2506c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = se_gcc_parent_map_3,
	.freq_tbl = ftbl_se_gcc_emac0_rgmii_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "se_gcc_emac1_rgmii_clk_src",
		.parent_data = se_gcc_parent_data_3,
		.num_parents = ARRAY_SIZE(se_gcc_parent_data_3),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_se_gcc_gp1_clk_src[] = {
	F(66666667, P_SE_GCC_GPLL0_OUT_MAIN, 9, 0, 0),
	F(100000000, P_SE_GCC_GPLL0_OUT_MAIN, 6, 0, 0),
	F(200000000, P_SE_GCC_GPLL0_OUT_MAIN, 3, 0, 0),
	{ }
};

static struct clk_rcg2 se_gcc_gp1_clk_src = {
	.cmd_rcgr = 0x19004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = se_gcc_parent_map_4,
	.freq_tbl = ftbl_se_gcc_gp1_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "se_gcc_gp1_clk_src",
		.parent_data = se_gcc_parent_data_4,
		.num_parents = ARRAY_SIZE(se_gcc_parent_data_4),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static struct clk_rcg2 se_gcc_gp2_clk_src = {
	.cmd_rcgr = 0x1a004,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = se_gcc_parent_map_4,
	.freq_tbl = ftbl_se_gcc_gp1_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &(const struct clk_init_data) {
		.name = "se_gcc_gp2_clk_src",
		.parent_data = se_gcc_parent_data_4,
		.num_parents = ARRAY_SIZE(se_gcc_parent_data_4),
		.flags = CLK_SET_RATE_PARENT,
		.ops = &clk_rcg2_shared_ops,
	},
};

static const struct freq_tbl ftbl_se_gcc_qupv3_wrap0_s0_clk_src[] = {
	F(7372800, P_SE_GCC_GPLL0_OUT_MAIN, 1, 192, 15625),
	F(14745600, P_SE_GCC_GPLL0_OUT_MAIN, 1, 384, 15625),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(29491200, P_SE_GCC_GPLL0_OUT_MAIN, 1, 768, 15625),
	F(32000000, P_SE_GCC_GPLL0_OUT_MAIN, 1, 4, 75),
	F(48000000, P_SE_GCC_GPLL0_OUT_MAIN, 1, 2, 25),
	F(51200000, P_SE_GCC_GPLL0_OUT_MAIN, 1, 32, 375),
	F(64000000, P_SE_GCC_GPLL0_OUT_MAIN, 1, 8, 75),
	F(66666667, P_SE_GCC_GPLL0_OUT_MAIN, 9, 0, 0),
	F(75000000, P_SE_GCC_GPLL0_OUT_MAIN, 8, 0, 0),
	F(80000000, P_SE_GCC_GPLL0_OUT_MAIN, 1, 2, 15),
	F(96000000, P_SE_GCC_GPLL0_OUT_MAIN, 1, 4, 25),
	F(100000000, P_SE_GCC_GPLL0_OUT_MAIN, 6, 0, 0),
	F(102400000, P_SE_GCC_GPLL0_OUT_MAIN, 1, 64, 375),
	F(112000000, P_SE_GCC_GPLL0_OUT_MAIN, 1, 14, 75),
	F(117964800, P_SE_GCC_GPLL0_OUT_MAIN, 1, 3072, 15625),
	F(120000000, P_SE_GCC_GPLL0_OUT_MAIN, 5, 0, 0),
	{ }
};

static struct clk_init_data se_gcc_qupv3_wrap0_s0_clk_src_init = {
	.name = "se_gcc_qupv3_wrap0_s0_clk_src",
	.parent_data = se_gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(se_gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 se_gcc_qupv3_wrap0_s0_clk_src = {
	.cmd_rcgr = 0x2616c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = se_gcc_parent_map_1,
	.freq_tbl = ftbl_se_gcc_qupv3_wrap0_s0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &se_gcc_qupv3_wrap0_s0_clk_src_init,
};

static struct clk_init_data se_gcc_qupv3_wrap0_s1_clk_src_init = {
	.name = "se_gcc_qupv3_wrap0_s1_clk_src",
	.parent_data = se_gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(se_gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 se_gcc_qupv3_wrap0_s1_clk_src = {
	.cmd_rcgr = 0x262a8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = se_gcc_parent_map_1,
	.freq_tbl = ftbl_se_gcc_qupv3_wrap0_s0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &se_gcc_qupv3_wrap0_s1_clk_src_init,
};

static const struct freq_tbl ftbl_se_gcc_qupv3_wrap0_s2_clk_src[] = {
	F(7372800, P_SE_GCC_GPLL0_OUT_MAIN, 1, 192, 15625),
	F(14745600, P_SE_GCC_GPLL0_OUT_MAIN, 1, 384, 15625),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(29491200, P_SE_GCC_GPLL0_OUT_MAIN, 1, 768, 15625),
	F(32000000, P_SE_GCC_GPLL0_OUT_MAIN, 1, 4, 75),
	F(48000000, P_SE_GCC_GPLL0_OUT_MAIN, 1, 2, 25),
	F(51200000, P_SE_GCC_GPLL0_OUT_MAIN, 1, 32, 375),
	F(64000000, P_SE_GCC_GPLL0_OUT_MAIN, 1, 8, 75),
	F(66666667, P_SE_GCC_GPLL0_OUT_MAIN, 9, 0, 0),
	F(75000000, P_SE_GCC_GPLL0_OUT_MAIN, 8, 0, 0),
	F(80000000, P_SE_GCC_GPLL0_OUT_MAIN, 1, 2, 15),
	F(96000000, P_SE_GCC_GPLL0_OUT_MAIN, 1, 4, 25),
	F(100000000, P_SE_GCC_GPLL0_OUT_MAIN, 6, 0, 0),
	{ }
};

static struct clk_init_data se_gcc_qupv3_wrap0_s2_clk_src_init = {
	.name = "se_gcc_qupv3_wrap0_s2_clk_src",
	.parent_data = se_gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(se_gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 se_gcc_qupv3_wrap0_s2_clk_src = {
	.cmd_rcgr = 0x263e4,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = se_gcc_parent_map_1,
	.freq_tbl = ftbl_se_gcc_qupv3_wrap0_s2_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &se_gcc_qupv3_wrap0_s2_clk_src_init,
};

static struct clk_init_data se_gcc_qupv3_wrap0_s3_clk_src_init = {
	.name = "se_gcc_qupv3_wrap0_s3_clk_src",
	.parent_data = se_gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(se_gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 se_gcc_qupv3_wrap0_s3_clk_src = {
	.cmd_rcgr = 0x26520,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = se_gcc_parent_map_1,
	.freq_tbl = ftbl_se_gcc_qupv3_wrap0_s2_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &se_gcc_qupv3_wrap0_s3_clk_src_init,
};

static struct clk_init_data se_gcc_qupv3_wrap0_s4_clk_src_init = {
	.name = "se_gcc_qupv3_wrap0_s4_clk_src",
	.parent_data = se_gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(se_gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 se_gcc_qupv3_wrap0_s4_clk_src = {
	.cmd_rcgr = 0x2665c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = se_gcc_parent_map_1,
	.freq_tbl = ftbl_se_gcc_qupv3_wrap0_s2_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &se_gcc_qupv3_wrap0_s4_clk_src_init,
};

static struct clk_init_data se_gcc_qupv3_wrap0_s5_clk_src_init = {
	.name = "se_gcc_qupv3_wrap0_s5_clk_src",
	.parent_data = se_gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(se_gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 se_gcc_qupv3_wrap0_s5_clk_src = {
	.cmd_rcgr = 0x26798,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = se_gcc_parent_map_1,
	.freq_tbl = ftbl_se_gcc_qupv3_wrap0_s2_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &se_gcc_qupv3_wrap0_s5_clk_src_init,
};

static struct clk_init_data se_gcc_qupv3_wrap0_s6_clk_src_init = {
	.name = "se_gcc_qupv3_wrap0_s6_clk_src",
	.parent_data = se_gcc_parent_data_1,
	.num_parents = ARRAY_SIZE(se_gcc_parent_data_1),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 se_gcc_qupv3_wrap0_s6_clk_src = {
	.cmd_rcgr = 0x268d4,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = se_gcc_parent_map_1,
	.freq_tbl = ftbl_se_gcc_qupv3_wrap0_s2_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &se_gcc_qupv3_wrap0_s6_clk_src_init,
};

static struct clk_init_data se_gcc_qupv3_wrap1_s0_clk_src_init = {
	.name = "se_gcc_qupv3_wrap1_s0_clk_src",
	.parent_data = se_gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(se_gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 se_gcc_qupv3_wrap1_s0_clk_src = {
	.cmd_rcgr = 0x2716c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = se_gcc_parent_map_0,
	.freq_tbl = ftbl_se_gcc_qupv3_wrap0_s0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &se_gcc_qupv3_wrap1_s0_clk_src_init,
};

static struct clk_init_data se_gcc_qupv3_wrap1_s1_clk_src_init = {
	.name = "se_gcc_qupv3_wrap1_s1_clk_src",
	.parent_data = se_gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(se_gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 se_gcc_qupv3_wrap1_s1_clk_src = {
	.cmd_rcgr = 0x272a8,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = se_gcc_parent_map_0,
	.freq_tbl = ftbl_se_gcc_qupv3_wrap0_s0_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &se_gcc_qupv3_wrap1_s1_clk_src_init,
};

static struct clk_init_data se_gcc_qupv3_wrap1_s2_clk_src_init = {
	.name = "se_gcc_qupv3_wrap1_s2_clk_src",
	.parent_data = se_gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(se_gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 se_gcc_qupv3_wrap1_s2_clk_src = {
	.cmd_rcgr = 0x273e4,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = se_gcc_parent_map_0,
	.freq_tbl = ftbl_se_gcc_qupv3_wrap0_s2_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &se_gcc_qupv3_wrap1_s2_clk_src_init,
};

static struct clk_init_data se_gcc_qupv3_wrap1_s3_clk_src_init = {
	.name = "se_gcc_qupv3_wrap1_s3_clk_src",
	.parent_data = se_gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(se_gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 se_gcc_qupv3_wrap1_s3_clk_src = {
	.cmd_rcgr = 0x27520,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = se_gcc_parent_map_0,
	.freq_tbl = ftbl_se_gcc_qupv3_wrap0_s2_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &se_gcc_qupv3_wrap1_s3_clk_src_init,
};

static struct clk_init_data se_gcc_qupv3_wrap1_s4_clk_src_init = {
	.name = "se_gcc_qupv3_wrap1_s4_clk_src",
	.parent_data = se_gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(se_gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 se_gcc_qupv3_wrap1_s4_clk_src = {
	.cmd_rcgr = 0x2765c,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = se_gcc_parent_map_0,
	.freq_tbl = ftbl_se_gcc_qupv3_wrap0_s2_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &se_gcc_qupv3_wrap1_s4_clk_src_init,
};

static struct clk_init_data se_gcc_qupv3_wrap1_s5_clk_src_init = {
	.name = "se_gcc_qupv3_wrap1_s5_clk_src",
	.parent_data = se_gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(se_gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 se_gcc_qupv3_wrap1_s5_clk_src = {
	.cmd_rcgr = 0x27798,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = se_gcc_parent_map_0,
	.freq_tbl = ftbl_se_gcc_qupv3_wrap0_s2_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &se_gcc_qupv3_wrap1_s5_clk_src_init,
};

static struct clk_init_data se_gcc_qupv3_wrap1_s6_clk_src_init = {
	.name = "se_gcc_qupv3_wrap1_s6_clk_src",
	.parent_data = se_gcc_parent_data_0,
	.num_parents = ARRAY_SIZE(se_gcc_parent_data_0),
	.flags = CLK_SET_RATE_PARENT,
	.ops = &clk_rcg2_shared_no_init_park_ops,
};

static struct clk_rcg2 se_gcc_qupv3_wrap1_s6_clk_src = {
	.cmd_rcgr = 0x278d4,
	.mnd_width = 16,
	.hid_width = 5,
	.parent_map = se_gcc_parent_map_0,
	.freq_tbl = ftbl_se_gcc_qupv3_wrap0_s2_clk_src,
	.hw_clk_ctrl = true,
	.clkr.hw.init = &se_gcc_qupv3_wrap1_s6_clk_src_init,
};

static struct clk_branch se_gcc_eee_emac0_clk = {
	.halt_reg = 0x240b4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x240b4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_eee_emac0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&se_gcc_eee_emac0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_eee_emac1_clk = {
	.halt_reg = 0x250b4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x250b4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_eee_emac1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&se_gcc_eee_emac1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_emac0_axi_clk = {
	.halt_reg = 0x2401c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2401c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2401c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_emac0_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_emac0_cc_sgmiiphy_rx_clk = {
	.halt_reg = 0x24064,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x24064,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_emac0_cc_sgmiiphy_rx_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_emac0_cc_sgmiiphy_tx_clk = {
	.halt_reg = 0x2405c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2405c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_emac0_cc_sgmiiphy_tx_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_emac0_phy_aux_clk = {
	.halt_reg = 0x2402c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2402c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_emac0_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&se_gcc_emac0_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_emac0_ptp_clk = {
	.halt_reg = 0x24048,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x24048,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_emac0_ptp_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&se_gcc_emac0_ptp_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_emac0_rgmii_clk = {
	.halt_reg = 0x24058,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x24058,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_emac0_rgmii_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&se_gcc_emac0_rgmii_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_emac0_rpcs_rx_clk = {
	.halt_reg = 0x240a8,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x240a8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_emac0_rpcs_rx_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_emac0_rpcs_tx_clk = {
	.halt_reg = 0x240a4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x240a4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_emac0_rpcs_tx_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_emac0_xgxs_rx_clk = {
	.halt_reg = 0x240b0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x240b0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_emac0_xgxs_rx_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_emac0_xgxs_tx_clk = {
	.halt_reg = 0x240ac,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x240ac,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_emac0_xgxs_tx_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_emac1_axi_clk = {
	.halt_reg = 0x2501c,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x2501c,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x2501c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_emac1_axi_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_emac1_cc_sgmiiphy_rx_clk = {
	.halt_reg = 0x25064,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x25064,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_emac1_cc_sgmiiphy_rx_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_emac1_cc_sgmiiphy_tx_clk = {
	.halt_reg = 0x2505c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2505c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_emac1_cc_sgmiiphy_tx_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_emac1_phy_aux_clk = {
	.halt_reg = 0x2502c,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x2502c,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_emac1_phy_aux_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&se_gcc_emac1_phy_aux_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_emac1_ptp_clk = {
	.halt_reg = 0x25048,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x25048,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_emac1_ptp_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&se_gcc_emac1_ptp_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_emac1_rgmii_clk = {
	.halt_reg = 0x25058,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x25058,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_emac1_rgmii_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&se_gcc_emac1_rgmii_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_emac1_rpcs_rx_clk = {
	.halt_reg = 0x250a8,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x250a8,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_emac1_rpcs_rx_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_emac1_rpcs_tx_clk = {
	.halt_reg = 0x250a4,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x250a4,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_emac1_rpcs_tx_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_emac1_xgxs_rx_clk = {
	.halt_reg = 0x250b0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x250b0,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_emac1_xgxs_rx_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_emac1_xgxs_tx_clk = {
	.halt_reg = 0x250ac,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x250ac,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_emac1_xgxs_tx_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_frq_measure_ref_clk = {
	.halt_reg = 0x18008,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x18008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_frq_measure_ref_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_gp1_clk = {
	.halt_reg = 0x19000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x19000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_gp1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&se_gcc_gp1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_gp2_clk = {
	.halt_reg = 0x1a000,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1a000,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_gp2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&se_gcc_gp2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_mmu_2_tcu_vote_clk = {
	.halt_reg = 0x57040,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x57040,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_mmu_2_tcu_vote_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_qupv3_wrap0_core_2x_clk = {
	.halt_reg = 0x26020,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x57000,
		.enable_mask = BIT(15),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_qupv3_wrap0_core_2x_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_qupv3_wrap0_core_clk = {
	.halt_reg = 0x2600c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x57000,
		.enable_mask = BIT(14),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_qupv3_wrap0_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_qupv3_wrap0_m_ahb_clk = {
	.halt_reg = 0x26004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x26004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x57000,
		.enable_mask = BIT(12),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_qupv3_wrap0_m_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_qupv3_wrap0_s0_clk = {
	.halt_reg = 0x2615c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x57000,
		.enable_mask = BIT(16),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_qupv3_wrap0_s0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&se_gcc_qupv3_wrap0_s0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_qupv3_wrap0_s1_clk = {
	.halt_reg = 0x26298,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x57000,
		.enable_mask = BIT(17),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_qupv3_wrap0_s1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&se_gcc_qupv3_wrap0_s1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_qupv3_wrap0_s2_clk = {
	.halt_reg = 0x263d4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x57000,
		.enable_mask = BIT(18),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_qupv3_wrap0_s2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&se_gcc_qupv3_wrap0_s2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_qupv3_wrap0_s3_clk = {
	.halt_reg = 0x26510,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x57000,
		.enable_mask = BIT(19),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_qupv3_wrap0_s3_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&se_gcc_qupv3_wrap0_s3_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_qupv3_wrap0_s4_clk = {
	.halt_reg = 0x2664c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x57000,
		.enable_mask = BIT(20),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_qupv3_wrap0_s4_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&se_gcc_qupv3_wrap0_s4_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_qupv3_wrap0_s5_clk = {
	.halt_reg = 0x26788,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x57000,
		.enable_mask = BIT(21),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_qupv3_wrap0_s5_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&se_gcc_qupv3_wrap0_s5_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_qupv3_wrap0_s6_clk = {
	.halt_reg = 0x268c4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x57000,
		.enable_mask = BIT(22),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_qupv3_wrap0_s6_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&se_gcc_qupv3_wrap0_s6_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_qupv3_wrap0_s_ahb_clk = {
	.halt_reg = 0x26008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x26008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x57000,
		.enable_mask = BIT(13),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_qupv3_wrap0_s_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_qupv3_wrap1_core_2x_clk = {
	.halt_reg = 0x27020,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x57000,
		.enable_mask = BIT(26),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_qupv3_wrap1_core_2x_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_qupv3_wrap1_core_clk = {
	.halt_reg = 0x2700c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x57000,
		.enable_mask = BIT(25),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_qupv3_wrap1_core_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_qupv3_wrap1_m_ahb_clk = {
	.halt_reg = 0x27004,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x27004,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x57000,
		.enable_mask = BIT(23),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_qupv3_wrap1_m_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_qupv3_wrap1_s0_clk = {
	.halt_reg = 0x2715c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x57000,
		.enable_mask = BIT(27),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_qupv3_wrap1_s0_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&se_gcc_qupv3_wrap1_s0_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_qupv3_wrap1_s1_clk = {
	.halt_reg = 0x27298,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x57000,
		.enable_mask = BIT(28),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_qupv3_wrap1_s1_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&se_gcc_qupv3_wrap1_s1_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_qupv3_wrap1_s2_clk = {
	.halt_reg = 0x273d4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x57000,
		.enable_mask = BIT(29),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_qupv3_wrap1_s2_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&se_gcc_qupv3_wrap1_s2_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_qupv3_wrap1_s3_clk = {
	.halt_reg = 0x27510,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x57000,
		.enable_mask = BIT(30),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_qupv3_wrap1_s3_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&se_gcc_qupv3_wrap1_s3_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_qupv3_wrap1_s4_clk = {
	.halt_reg = 0x2764c,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x57000,
		.enable_mask = BIT(31),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_qupv3_wrap1_s4_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&se_gcc_qupv3_wrap1_s4_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_qupv3_wrap1_s5_clk = {
	.halt_reg = 0x27788,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x57008,
		.enable_mask = BIT(0),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_qupv3_wrap1_s5_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&se_gcc_qupv3_wrap1_s5_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_qupv3_wrap1_s6_clk = {
	.halt_reg = 0x278c4,
	.halt_check = BRANCH_HALT_VOTED,
	.clkr = {
		.enable_reg = 0x57008,
		.enable_mask = BIT(1),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_qupv3_wrap1_s6_clk",
			.parent_hws = (const struct clk_hw*[]) {
				&se_gcc_qupv3_wrap1_s6_clk_src.clkr.hw,
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch se_gcc_qupv3_wrap1_s_ahb_clk = {
	.halt_reg = 0x27008,
	.halt_check = BRANCH_HALT_VOTED,
	.hwcg_reg = 0x27008,
	.hwcg_bit = 1,
	.clkr = {
		.enable_reg = 0x57000,
		.enable_mask = BIT(24),
		.hw.init = &(const struct clk_init_data) {
			.name = "se_gcc_qupv3_wrap1_s_ahb_clk",
			.ops = &clk_branch2_ops,
		},
	},
};

static struct gdsc se_gcc_emac0_gdsc = {
	.gdscr = 0x24004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "se_gcc_emac0_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct gdsc se_gcc_emac1_gdsc = {
	.gdscr = 0x25004,
	.en_rest_wait_val = 0x2,
	.en_few_wait_val = 0x2,
	.clk_dis_wait_val = 0xf,
	.pd = {
		.name = "se_gcc_emac1_gdsc",
	},
	.pwrsts = PWRSTS_OFF_ON,
	.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE,
};

static struct clk_regmap *se_gcc_nord_clocks[] = {
	[SE_GCC_EEE_EMAC0_CLK] = &se_gcc_eee_emac0_clk.clkr,
	[SE_GCC_EEE_EMAC0_CLK_SRC] = &se_gcc_eee_emac0_clk_src.clkr,
	[SE_GCC_EEE_EMAC1_CLK] = &se_gcc_eee_emac1_clk.clkr,
	[SE_GCC_EEE_EMAC1_CLK_SRC] = &se_gcc_eee_emac1_clk_src.clkr,
	[SE_GCC_EMAC0_AXI_CLK] = &se_gcc_emac0_axi_clk.clkr,
	[SE_GCC_EMAC0_CC_SGMIIPHY_RX_CLK] = &se_gcc_emac0_cc_sgmiiphy_rx_clk.clkr,
	[SE_GCC_EMAC0_CC_SGMIIPHY_TX_CLK] = &se_gcc_emac0_cc_sgmiiphy_tx_clk.clkr,
	[SE_GCC_EMAC0_PHY_AUX_CLK] = &se_gcc_emac0_phy_aux_clk.clkr,
	[SE_GCC_EMAC0_PHY_AUX_CLK_SRC] = &se_gcc_emac0_phy_aux_clk_src.clkr,
	[SE_GCC_EMAC0_PTP_CLK] = &se_gcc_emac0_ptp_clk.clkr,
	[SE_GCC_EMAC0_PTP_CLK_SRC] = &se_gcc_emac0_ptp_clk_src.clkr,
	[SE_GCC_EMAC0_RGMII_CLK] = &se_gcc_emac0_rgmii_clk.clkr,
	[SE_GCC_EMAC0_RGMII_CLK_SRC] = &se_gcc_emac0_rgmii_clk_src.clkr,
	[SE_GCC_EMAC0_RPCS_RX_CLK] = &se_gcc_emac0_rpcs_rx_clk.clkr,
	[SE_GCC_EMAC0_RPCS_TX_CLK] = &se_gcc_emac0_rpcs_tx_clk.clkr,
	[SE_GCC_EMAC0_XGXS_RX_CLK] = &se_gcc_emac0_xgxs_rx_clk.clkr,
	[SE_GCC_EMAC0_XGXS_TX_CLK] = &se_gcc_emac0_xgxs_tx_clk.clkr,
	[SE_GCC_EMAC1_AXI_CLK] = &se_gcc_emac1_axi_clk.clkr,
	[SE_GCC_EMAC1_CC_SGMIIPHY_RX_CLK] = &se_gcc_emac1_cc_sgmiiphy_rx_clk.clkr,
	[SE_GCC_EMAC1_CC_SGMIIPHY_TX_CLK] = &se_gcc_emac1_cc_sgmiiphy_tx_clk.clkr,
	[SE_GCC_EMAC1_PHY_AUX_CLK] = &se_gcc_emac1_phy_aux_clk.clkr,
	[SE_GCC_EMAC1_PHY_AUX_CLK_SRC] = &se_gcc_emac1_phy_aux_clk_src.clkr,
	[SE_GCC_EMAC1_PTP_CLK] = &se_gcc_emac1_ptp_clk.clkr,
	[SE_GCC_EMAC1_PTP_CLK_SRC] = &se_gcc_emac1_ptp_clk_src.clkr,
	[SE_GCC_EMAC1_RGMII_CLK] = &se_gcc_emac1_rgmii_clk.clkr,
	[SE_GCC_EMAC1_RGMII_CLK_SRC] = &se_gcc_emac1_rgmii_clk_src.clkr,
	[SE_GCC_EMAC1_RPCS_RX_CLK] = &se_gcc_emac1_rpcs_rx_clk.clkr,
	[SE_GCC_EMAC1_RPCS_TX_CLK] = &se_gcc_emac1_rpcs_tx_clk.clkr,
	[SE_GCC_EMAC1_XGXS_RX_CLK] = &se_gcc_emac1_xgxs_rx_clk.clkr,
	[SE_GCC_EMAC1_XGXS_TX_CLK] = &se_gcc_emac1_xgxs_tx_clk.clkr,
	[SE_GCC_FRQ_MEASURE_REF_CLK] = &se_gcc_frq_measure_ref_clk.clkr,
	[SE_GCC_GP1_CLK] = &se_gcc_gp1_clk.clkr,
	[SE_GCC_GP1_CLK_SRC] = &se_gcc_gp1_clk_src.clkr,
	[SE_GCC_GP2_CLK] = &se_gcc_gp2_clk.clkr,
	[SE_GCC_GP2_CLK_SRC] = &se_gcc_gp2_clk_src.clkr,
	[SE_GCC_GPLL0] = &se_gcc_gpll0.clkr,
	[SE_GCC_GPLL0_OUT_EVEN] = &se_gcc_gpll0_out_even.clkr,
	[SE_GCC_GPLL2] = &se_gcc_gpll2.clkr,
	[SE_GCC_GPLL4] = &se_gcc_gpll4.clkr,
	[SE_GCC_GPLL5] = &se_gcc_gpll5.clkr,
	[SE_GCC_MMU_2_TCU_VOTE_CLK] = &se_gcc_mmu_2_tcu_vote_clk.clkr,
	[SE_GCC_QUPV3_WRAP0_CORE_2X_CLK] = &se_gcc_qupv3_wrap0_core_2x_clk.clkr,
	[SE_GCC_QUPV3_WRAP0_CORE_CLK] = &se_gcc_qupv3_wrap0_core_clk.clkr,
	[SE_GCC_QUPV3_WRAP0_M_AHB_CLK] = &se_gcc_qupv3_wrap0_m_ahb_clk.clkr,
	[SE_GCC_QUPV3_WRAP0_S0_CLK] = &se_gcc_qupv3_wrap0_s0_clk.clkr,
	[SE_GCC_QUPV3_WRAP0_S0_CLK_SRC] = &se_gcc_qupv3_wrap0_s0_clk_src.clkr,
	[SE_GCC_QUPV3_WRAP0_S1_CLK] = &se_gcc_qupv3_wrap0_s1_clk.clkr,
	[SE_GCC_QUPV3_WRAP0_S1_CLK_SRC] = &se_gcc_qupv3_wrap0_s1_clk_src.clkr,
	[SE_GCC_QUPV3_WRAP0_S2_CLK] = &se_gcc_qupv3_wrap0_s2_clk.clkr,
	[SE_GCC_QUPV3_WRAP0_S2_CLK_SRC] = &se_gcc_qupv3_wrap0_s2_clk_src.clkr,
	[SE_GCC_QUPV3_WRAP0_S3_CLK] = &se_gcc_qupv3_wrap0_s3_clk.clkr,
	[SE_GCC_QUPV3_WRAP0_S3_CLK_SRC] = &se_gcc_qupv3_wrap0_s3_clk_src.clkr,
	[SE_GCC_QUPV3_WRAP0_S4_CLK] = &se_gcc_qupv3_wrap0_s4_clk.clkr,
	[SE_GCC_QUPV3_WRAP0_S4_CLK_SRC] = &se_gcc_qupv3_wrap0_s4_clk_src.clkr,
	[SE_GCC_QUPV3_WRAP0_S5_CLK] = &se_gcc_qupv3_wrap0_s5_clk.clkr,
	[SE_GCC_QUPV3_WRAP0_S5_CLK_SRC] = &se_gcc_qupv3_wrap0_s5_clk_src.clkr,
	[SE_GCC_QUPV3_WRAP0_S6_CLK] = &se_gcc_qupv3_wrap0_s6_clk.clkr,
	[SE_GCC_QUPV3_WRAP0_S6_CLK_SRC] = &se_gcc_qupv3_wrap0_s6_clk_src.clkr,
	[SE_GCC_QUPV3_WRAP0_S_AHB_CLK] = &se_gcc_qupv3_wrap0_s_ahb_clk.clkr,
	[SE_GCC_QUPV3_WRAP1_CORE_2X_CLK] = &se_gcc_qupv3_wrap1_core_2x_clk.clkr,
	[SE_GCC_QUPV3_WRAP1_CORE_CLK] = &se_gcc_qupv3_wrap1_core_clk.clkr,
	[SE_GCC_QUPV3_WRAP1_M_AHB_CLK] = &se_gcc_qupv3_wrap1_m_ahb_clk.clkr,
	[SE_GCC_QUPV3_WRAP1_S0_CLK] = &se_gcc_qupv3_wrap1_s0_clk.clkr,
	[SE_GCC_QUPV3_WRAP1_S0_CLK_SRC] = &se_gcc_qupv3_wrap1_s0_clk_src.clkr,
	[SE_GCC_QUPV3_WRAP1_S1_CLK] = &se_gcc_qupv3_wrap1_s1_clk.clkr,
	[SE_GCC_QUPV3_WRAP1_S1_CLK_SRC] = &se_gcc_qupv3_wrap1_s1_clk_src.clkr,
	[SE_GCC_QUPV3_WRAP1_S2_CLK] = &se_gcc_qupv3_wrap1_s2_clk.clkr,
	[SE_GCC_QUPV3_WRAP1_S2_CLK_SRC] = &se_gcc_qupv3_wrap1_s2_clk_src.clkr,
	[SE_GCC_QUPV3_WRAP1_S3_CLK] = &se_gcc_qupv3_wrap1_s3_clk.clkr,
	[SE_GCC_QUPV3_WRAP1_S3_CLK_SRC] = &se_gcc_qupv3_wrap1_s3_clk_src.clkr,
	[SE_GCC_QUPV3_WRAP1_S4_CLK] = &se_gcc_qupv3_wrap1_s4_clk.clkr,
	[SE_GCC_QUPV3_WRAP1_S4_CLK_SRC] = &se_gcc_qupv3_wrap1_s4_clk_src.clkr,
	[SE_GCC_QUPV3_WRAP1_S5_CLK] = &se_gcc_qupv3_wrap1_s5_clk.clkr,
	[SE_GCC_QUPV3_WRAP1_S5_CLK_SRC] = &se_gcc_qupv3_wrap1_s5_clk_src.clkr,
	[SE_GCC_QUPV3_WRAP1_S6_CLK] = &se_gcc_qupv3_wrap1_s6_clk.clkr,
	[SE_GCC_QUPV3_WRAP1_S6_CLK_SRC] = &se_gcc_qupv3_wrap1_s6_clk_src.clkr,
	[SE_GCC_QUPV3_WRAP1_S_AHB_CLK] = &se_gcc_qupv3_wrap1_s_ahb_clk.clkr,
};

static struct gdsc *se_gcc_nord_gdscs[] = {
	[SE_GCC_EMAC0_GDSC] = &se_gcc_emac0_gdsc,
	[SE_GCC_EMAC1_GDSC] = &se_gcc_emac1_gdsc,
};

static const struct qcom_reset_map se_gcc_nord_resets[] = {
	[SE_GCC_EMAC0_BCR] = { 0x24000 },
	[SE_GCC_EMAC1_BCR] = { 0x25000 },
	[SE_GCC_QUPV3_WRAPPER_0_BCR] = { 0x26000 },
	[SE_GCC_QUPV3_WRAPPER_1_BCR] = { 0x27000 },
};

static const struct clk_rcg_dfs_data se_gcc_nord_dfs_clocks[] = {
	DEFINE_RCG_DFS(se_gcc_qupv3_wrap0_s0_clk_src),
	DEFINE_RCG_DFS(se_gcc_qupv3_wrap0_s1_clk_src),
	DEFINE_RCG_DFS(se_gcc_qupv3_wrap0_s2_clk_src),
	DEFINE_RCG_DFS(se_gcc_qupv3_wrap0_s3_clk_src),
	DEFINE_RCG_DFS(se_gcc_qupv3_wrap0_s4_clk_src),
	DEFINE_RCG_DFS(se_gcc_qupv3_wrap0_s5_clk_src),
	DEFINE_RCG_DFS(se_gcc_qupv3_wrap0_s6_clk_src),
	DEFINE_RCG_DFS(se_gcc_qupv3_wrap1_s0_clk_src),
	DEFINE_RCG_DFS(se_gcc_qupv3_wrap1_s1_clk_src),
	DEFINE_RCG_DFS(se_gcc_qupv3_wrap1_s2_clk_src),
	DEFINE_RCG_DFS(se_gcc_qupv3_wrap1_s3_clk_src),
	DEFINE_RCG_DFS(se_gcc_qupv3_wrap1_s4_clk_src),
	DEFINE_RCG_DFS(se_gcc_qupv3_wrap1_s5_clk_src),
	DEFINE_RCG_DFS(se_gcc_qupv3_wrap1_s6_clk_src),
};

static const struct regmap_config se_gcc_nord_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = 0xf41f0,
	.fast_io = true,
};

static const struct qcom_cc_driver_data se_gcc_nord_driver_data = {
	.dfs_rcgs = se_gcc_nord_dfs_clocks,
	.num_dfs_rcgs = ARRAY_SIZE(se_gcc_nord_dfs_clocks),
};

static const struct qcom_cc_desc se_gcc_nord_desc = {
	.config = &se_gcc_nord_regmap_config,
	.clks = se_gcc_nord_clocks,
	.num_clks = ARRAY_SIZE(se_gcc_nord_clocks),
	.resets = se_gcc_nord_resets,
	.num_resets = ARRAY_SIZE(se_gcc_nord_resets),
	.gdscs = se_gcc_nord_gdscs,
	.num_gdscs = ARRAY_SIZE(se_gcc_nord_gdscs),
	.driver_data = &se_gcc_nord_driver_data,
};

static const struct of_device_id se_gcc_nord_match_table[] = {
	{ .compatible = "qcom,nord-segcc" },
	{ }
};
MODULE_DEVICE_TABLE(of, se_gcc_nord_match_table);

static int se_gcc_nord_probe(struct platform_device *pdev)
{
	return qcom_cc_probe(pdev, &se_gcc_nord_desc);
}

static struct platform_driver se_gcc_nord_driver = {
	.probe = se_gcc_nord_probe,
	.driver = {
		.name = "segcc-nord",
		.of_match_table = se_gcc_nord_match_table,
	},
};

module_platform_driver(se_gcc_nord_driver);

MODULE_DESCRIPTION("QTI SEGCC NORD Driver");
MODULE_LICENSE("GPL");
