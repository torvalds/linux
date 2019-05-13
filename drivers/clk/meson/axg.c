// SPDX-License-Identifier: GPL-2.0+
/*
 * AmLogic Meson-AXG Clock Controller Driver
 *
 * Copyright (c) 2016 Baylibre SAS.
 * Author: Michael Turquette <mturquette@baylibre.com>
 *
 * Copyright (c) 2017 Amlogic, inc.
 * Author: Qiufang Dai <qiufang.dai@amlogic.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/init.h>
#include <linux/of_device.h>
#include <linux/mfd/syscon.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "clkc.h"
#include "axg.h"

static DEFINE_SPINLOCK(meson_clk_lock);

static struct clk_regmap axg_fixed_pll = {
	.data = &(struct meson_clk_pll_data){
		.m = {
			.reg_off = HHI_MPLL_CNTL,
			.shift   = 0,
			.width   = 9,
		},
		.n = {
			.reg_off = HHI_MPLL_CNTL,
			.shift   = 9,
			.width   = 5,
		},
		.od = {
			.reg_off = HHI_MPLL_CNTL,
			.shift   = 16,
			.width   = 2,
		},
		.frac = {
			.reg_off = HHI_MPLL_CNTL2,
			.shift   = 0,
			.width   = 12,
		},
		.l = {
			.reg_off = HHI_MPLL_CNTL,
			.shift   = 31,
			.width   = 1,
		},
		.rst = {
			.reg_off = HHI_MPLL_CNTL,
			.shift   = 29,
			.width   = 1,
		},
	},
	.hw.init = &(struct clk_init_data){
		.name = "fixed_pll",
		.ops = &meson_clk_pll_ro_ops,
		.parent_names = (const char *[]){ "xtal" },
		.num_parents = 1,
	},
};

static struct clk_regmap axg_sys_pll = {
	.data = &(struct meson_clk_pll_data){
		.m = {
			.reg_off = HHI_SYS_PLL_CNTL,
			.shift   = 0,
			.width   = 9,
		},
		.n = {
			.reg_off = HHI_SYS_PLL_CNTL,
			.shift   = 9,
			.width   = 5,
		},
		.od = {
			.reg_off = HHI_SYS_PLL_CNTL,
			.shift   = 16,
			.width   = 2,
		},
		.l = {
			.reg_off = HHI_SYS_PLL_CNTL,
			.shift   = 31,
			.width   = 1,
		},
		.rst = {
			.reg_off = HHI_SYS_PLL_CNTL,
			.shift   = 29,
			.width   = 1,
		},
	},
	.hw.init = &(struct clk_init_data){
		.name = "sys_pll",
		.ops = &meson_clk_pll_ro_ops,
		.parent_names = (const char *[]){ "xtal" },
		.num_parents = 1,
	},
};

static const struct pll_rate_table axg_gp0_pll_rate_table[] = {
	PLL_RATE(240000000, 40, 1, 2),
	PLL_RATE(246000000, 41, 1, 2),
	PLL_RATE(252000000, 42, 1, 2),
	PLL_RATE(258000000, 43, 1, 2),
	PLL_RATE(264000000, 44, 1, 2),
	PLL_RATE(270000000, 45, 1, 2),
	PLL_RATE(276000000, 46, 1, 2),
	PLL_RATE(282000000, 47, 1, 2),
	PLL_RATE(288000000, 48, 1, 2),
	PLL_RATE(294000000, 49, 1, 2),
	PLL_RATE(300000000, 50, 1, 2),
	PLL_RATE(306000000, 51, 1, 2),
	PLL_RATE(312000000, 52, 1, 2),
	PLL_RATE(318000000, 53, 1, 2),
	PLL_RATE(324000000, 54, 1, 2),
	PLL_RATE(330000000, 55, 1, 2),
	PLL_RATE(336000000, 56, 1, 2),
	PLL_RATE(342000000, 57, 1, 2),
	PLL_RATE(348000000, 58, 1, 2),
	PLL_RATE(354000000, 59, 1, 2),
	PLL_RATE(360000000, 60, 1, 2),
	PLL_RATE(366000000, 61, 1, 2),
	PLL_RATE(372000000, 62, 1, 2),
	PLL_RATE(378000000, 63, 1, 2),
	PLL_RATE(384000000, 64, 1, 2),
	PLL_RATE(390000000, 65, 1, 3),
	PLL_RATE(396000000, 66, 1, 3),
	PLL_RATE(402000000, 67, 1, 3),
	PLL_RATE(408000000, 68, 1, 3),
	PLL_RATE(480000000, 40, 1, 1),
	PLL_RATE(492000000, 41, 1, 1),
	PLL_RATE(504000000, 42, 1, 1),
	PLL_RATE(516000000, 43, 1, 1),
	PLL_RATE(528000000, 44, 1, 1),
	PLL_RATE(540000000, 45, 1, 1),
	PLL_RATE(552000000, 46, 1, 1),
	PLL_RATE(564000000, 47, 1, 1),
	PLL_RATE(576000000, 48, 1, 1),
	PLL_RATE(588000000, 49, 1, 1),
	PLL_RATE(600000000, 50, 1, 1),
	PLL_RATE(612000000, 51, 1, 1),
	PLL_RATE(624000000, 52, 1, 1),
	PLL_RATE(636000000, 53, 1, 1),
	PLL_RATE(648000000, 54, 1, 1),
	PLL_RATE(660000000, 55, 1, 1),
	PLL_RATE(672000000, 56, 1, 1),
	PLL_RATE(684000000, 57, 1, 1),
	PLL_RATE(696000000, 58, 1, 1),
	PLL_RATE(708000000, 59, 1, 1),
	PLL_RATE(720000000, 60, 1, 1),
	PLL_RATE(732000000, 61, 1, 1),
	PLL_RATE(744000000, 62, 1, 1),
	PLL_RATE(756000000, 63, 1, 1),
	PLL_RATE(768000000, 64, 1, 1),
	PLL_RATE(780000000, 65, 1, 1),
	PLL_RATE(792000000, 66, 1, 1),
	PLL_RATE(804000000, 67, 1, 1),
	PLL_RATE(816000000, 68, 1, 1),
	PLL_RATE(960000000, 40, 1, 0),
	PLL_RATE(984000000, 41, 1, 0),
	PLL_RATE(1008000000, 42, 1, 0),
	PLL_RATE(1032000000, 43, 1, 0),
	PLL_RATE(1056000000, 44, 1, 0),
	PLL_RATE(1080000000, 45, 1, 0),
	PLL_RATE(1104000000, 46, 1, 0),
	PLL_RATE(1128000000, 47, 1, 0),
	PLL_RATE(1152000000, 48, 1, 0),
	PLL_RATE(1176000000, 49, 1, 0),
	PLL_RATE(1200000000, 50, 1, 0),
	PLL_RATE(1224000000, 51, 1, 0),
	PLL_RATE(1248000000, 52, 1, 0),
	PLL_RATE(1272000000, 53, 1, 0),
	PLL_RATE(1296000000, 54, 1, 0),
	PLL_RATE(1320000000, 55, 1, 0),
	PLL_RATE(1344000000, 56, 1, 0),
	PLL_RATE(1368000000, 57, 1, 0),
	PLL_RATE(1392000000, 58, 1, 0),
	PLL_RATE(1416000000, 59, 1, 0),
	PLL_RATE(1440000000, 60, 1, 0),
	PLL_RATE(1464000000, 61, 1, 0),
	PLL_RATE(1488000000, 62, 1, 0),
	PLL_RATE(1512000000, 63, 1, 0),
	PLL_RATE(1536000000, 64, 1, 0),
	PLL_RATE(1560000000, 65, 1, 0),
	PLL_RATE(1584000000, 66, 1, 0),
	PLL_RATE(1608000000, 67, 1, 0),
	PLL_RATE(1632000000, 68, 1, 0),
	{ /* sentinel */ },
};

static const struct reg_sequence axg_gp0_init_regs[] = {
	{ .reg = HHI_GP0_PLL_CNTL1,	.def = 0xc084b000 },
	{ .reg = HHI_GP0_PLL_CNTL2,	.def = 0xb75020be },
	{ .reg = HHI_GP0_PLL_CNTL3,	.def = 0x0a59a288 },
	{ .reg = HHI_GP0_PLL_CNTL4,	.def = 0xc000004d },
	{ .reg = HHI_GP0_PLL_CNTL5,	.def = 0x00078000 },
	{ .reg = HHI_GP0_PLL_CNTL,	.def = 0x40010250 },
};

static struct clk_regmap axg_gp0_pll = {
	.data = &(struct meson_clk_pll_data){
		.m = {
			.reg_off = HHI_GP0_PLL_CNTL,
			.shift   = 0,
			.width   = 9,
		},
		.n = {
			.reg_off = HHI_GP0_PLL_CNTL,
			.shift   = 9,
			.width   = 5,
		},
		.od = {
			.reg_off = HHI_GP0_PLL_CNTL,
			.shift   = 16,
			.width   = 2,
		},
		.frac = {
			.reg_off = HHI_GP0_PLL_CNTL1,
			.shift   = 0,
			.width   = 10,
		},
		.l = {
			.reg_off = HHI_GP0_PLL_CNTL,
			.shift   = 31,
			.width   = 1,
		},
		.rst = {
			.reg_off = HHI_GP0_PLL_CNTL,
			.shift   = 29,
			.width   = 1,
		},
		.table = axg_gp0_pll_rate_table,
		.init_regs = axg_gp0_init_regs,
		.init_count = ARRAY_SIZE(axg_gp0_init_regs),
	},
	.hw.init = &(struct clk_init_data){
		.name = "gp0_pll",
		.ops = &meson_clk_pll_ops,
		.parent_names = (const char *[]){ "xtal" },
		.num_parents = 1,
	},
};

static const struct reg_sequence axg_hifi_init_regs[] = {
	{ .reg = HHI_HIFI_PLL_CNTL1,	.def = 0xc084b000 },
	{ .reg = HHI_HIFI_PLL_CNTL2,	.def = 0xb75020be },
	{ .reg = HHI_HIFI_PLL_CNTL3,	.def = 0x0a6a3a88 },
	{ .reg = HHI_HIFI_PLL_CNTL4,	.def = 0xc000004d },
	{ .reg = HHI_HIFI_PLL_CNTL5,	.def = 0x00058000 },
	{ .reg = HHI_HIFI_PLL_CNTL,	.def = 0x40010250 },
};

static struct clk_regmap axg_hifi_pll = {
	.data = &(struct meson_clk_pll_data){
		.m = {
			.reg_off = HHI_HIFI_PLL_CNTL,
			.shift   = 0,
			.width   = 9,
		},
		.n = {
			.reg_off = HHI_HIFI_PLL_CNTL,
			.shift   = 9,
			.width   = 5,
		},
		.od = {
			.reg_off = HHI_HIFI_PLL_CNTL,
			.shift   = 16,
			.width   = 2,
		},
		.frac = {
			.reg_off = HHI_HIFI_PLL_CNTL5,
			.shift   = 0,
			.width   = 13,
		},
		.l = {
			.reg_off = HHI_HIFI_PLL_CNTL,
			.shift   = 31,
			.width   = 1,
		},
		.rst = {
			.reg_off = HHI_HIFI_PLL_CNTL,
			.shift   = 29,
			.width   = 1,
		},
		.table = axg_gp0_pll_rate_table,
		.init_regs = axg_hifi_init_regs,
		.init_count = ARRAY_SIZE(axg_hifi_init_regs),
		.flags = CLK_MESON_PLL_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "hifi_pll",
		.ops = &meson_clk_pll_ops,
		.parent_names = (const char *[]){ "xtal" },
		.num_parents = 1,
	},
};

static struct clk_fixed_factor axg_fclk_div2_div = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div2_div",
		.ops = &clk_fixed_factor_ops,
		.parent_names = (const char *[]){ "fixed_pll" },
		.num_parents = 1,
	},
};

static struct clk_regmap axg_fclk_div2 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_MPLL_CNTL6,
		.bit_idx = 27,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div2",
		.ops = &clk_regmap_gate_ops,
		.parent_names = (const char *[]){ "fclk_div2_div" },
		.num_parents = 1,
		.flags = CLK_IS_CRITICAL,
	},
};

static struct clk_fixed_factor axg_fclk_div3_div = {
	.mult = 1,
	.div = 3,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div3_div",
		.ops = &clk_fixed_factor_ops,
		.parent_names = (const char *[]){ "fixed_pll" },
		.num_parents = 1,
	},
};

static struct clk_regmap axg_fclk_div3 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_MPLL_CNTL6,
		.bit_idx = 28,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div3",
		.ops = &clk_regmap_gate_ops,
		.parent_names = (const char *[]){ "fclk_div3_div" },
		.num_parents = 1,
		/*
		 * FIXME:
		 * This clock, as fdiv2, is used by the SCPI FW and is required
		 * by the platform to operate correctly.
		 * Until the following condition are met, we need this clock to
		 * be marked as critical:
		 * a) The SCPI generic driver claims and enable all the clocks
		 *    it needs
		 * b) CCF has a clock hand-off mechanism to make the sure the
		 *    clock stays on until the proper driver comes along
		 */
		.flags = CLK_IS_CRITICAL,
	},
};

static struct clk_fixed_factor axg_fclk_div4_div = {
	.mult = 1,
	.div = 4,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div4_div",
		.ops = &clk_fixed_factor_ops,
		.parent_names = (const char *[]){ "fixed_pll" },
		.num_parents = 1,
	},
};

static struct clk_regmap axg_fclk_div4 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_MPLL_CNTL6,
		.bit_idx = 29,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div4",
		.ops = &clk_regmap_gate_ops,
		.parent_names = (const char *[]){ "fclk_div4_div" },
		.num_parents = 1,
	},
};

static struct clk_fixed_factor axg_fclk_div5_div = {
	.mult = 1,
	.div = 5,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div5_div",
		.ops = &clk_fixed_factor_ops,
		.parent_names = (const char *[]){ "fixed_pll" },
		.num_parents = 1,
	},
};

static struct clk_regmap axg_fclk_div5 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_MPLL_CNTL6,
		.bit_idx = 30,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div5",
		.ops = &clk_regmap_gate_ops,
		.parent_names = (const char *[]){ "fclk_div5_div" },
		.num_parents = 1,
	},
};

static struct clk_fixed_factor axg_fclk_div7_div = {
	.mult = 1,
	.div = 7,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div7_div",
		.ops = &clk_fixed_factor_ops,
		.parent_names = (const char *[]){ "fixed_pll" },
		.num_parents = 1,
	},
};

static struct clk_regmap axg_fclk_div7 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_MPLL_CNTL6,
		.bit_idx = 31,
	},
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div7",
		.ops = &clk_regmap_gate_ops,
		.parent_names = (const char *[]){ "fclk_div7_div" },
		.num_parents = 1,
	},
};

static struct clk_regmap axg_mpll_prediv = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_MPLL_CNTL5,
		.shift = 12,
		.width = 1,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll_prediv",
		.ops = &clk_regmap_divider_ro_ops,
		.parent_names = (const char *[]){ "fixed_pll" },
		.num_parents = 1,
	},
};

static struct clk_regmap axg_mpll0_div = {
	.data = &(struct meson_clk_mpll_data){
		.sdm = {
			.reg_off = HHI_MPLL_CNTL7,
			.shift   = 0,
			.width   = 14,
		},
		.sdm_en = {
			.reg_off = HHI_MPLL_CNTL7,
			.shift   = 15,
			.width	 = 1,
		},
		.n2 = {
			.reg_off = HHI_MPLL_CNTL7,
			.shift   = 16,
			.width   = 9,
		},
		.misc = {
			.reg_off = HHI_PLL_TOP_MISC,
			.shift   = 0,
			.width	 = 1,
		},
		.lock = &meson_clk_lock,
		.flags = CLK_MESON_MPLL_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll0_div",
		.ops = &meson_clk_mpll_ops,
		.parent_names = (const char *[]){ "mpll_prediv" },
		.num_parents = 1,
	},
};

static struct clk_regmap axg_mpll0 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_MPLL_CNTL7,
		.bit_idx = 14,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll0",
		.ops = &clk_regmap_gate_ops,
		.parent_names = (const char *[]){ "mpll0_div" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap axg_mpll1_div = {
	.data = &(struct meson_clk_mpll_data){
		.sdm = {
			.reg_off = HHI_MPLL_CNTL8,
			.shift   = 0,
			.width   = 14,
		},
		.sdm_en = {
			.reg_off = HHI_MPLL_CNTL8,
			.shift   = 15,
			.width	 = 1,
		},
		.n2 = {
			.reg_off = HHI_MPLL_CNTL8,
			.shift   = 16,
			.width   = 9,
		},
		.misc = {
			.reg_off = HHI_PLL_TOP_MISC,
			.shift   = 1,
			.width	 = 1,
		},
		.lock = &meson_clk_lock,
		.flags = CLK_MESON_MPLL_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll1_div",
		.ops = &meson_clk_mpll_ops,
		.parent_names = (const char *[]){ "mpll_prediv" },
		.num_parents = 1,
	},
};

static struct clk_regmap axg_mpll1 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_MPLL_CNTL8,
		.bit_idx = 14,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll1",
		.ops = &clk_regmap_gate_ops,
		.parent_names = (const char *[]){ "mpll1_div" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap axg_mpll2_div = {
	.data = &(struct meson_clk_mpll_data){
		.sdm = {
			.reg_off = HHI_MPLL_CNTL9,
			.shift   = 0,
			.width   = 14,
		},
		.sdm_en = {
			.reg_off = HHI_MPLL_CNTL9,
			.shift   = 15,
			.width	 = 1,
		},
		.n2 = {
			.reg_off = HHI_MPLL_CNTL9,
			.shift   = 16,
			.width   = 9,
		},
		.ssen = {
			.reg_off = HHI_MPLL_CNTL,
			.shift   = 25,
			.width	 = 1,
		},
		.misc = {
			.reg_off = HHI_PLL_TOP_MISC,
			.shift   = 2,
			.width	 = 1,
		},
		.lock = &meson_clk_lock,
		.flags = CLK_MESON_MPLL_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll2_div",
		.ops = &meson_clk_mpll_ops,
		.parent_names = (const char *[]){ "mpll_prediv" },
		.num_parents = 1,
	},
};

static struct clk_regmap axg_mpll2 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_MPLL_CNTL9,
		.bit_idx = 14,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll2",
		.ops = &clk_regmap_gate_ops,
		.parent_names = (const char *[]){ "mpll2_div" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap axg_mpll3_div = {
	.data = &(struct meson_clk_mpll_data){
		.sdm = {
			.reg_off = HHI_MPLL3_CNTL0,
			.shift   = 12,
			.width   = 14,
		},
		.sdm_en = {
			.reg_off = HHI_MPLL3_CNTL0,
			.shift   = 11,
			.width	 = 1,
		},
		.n2 = {
			.reg_off = HHI_MPLL3_CNTL0,
			.shift   = 2,
			.width   = 9,
		},
		.misc = {
			.reg_off = HHI_PLL_TOP_MISC,
			.shift   = 3,
			.width	 = 1,
		},
		.lock = &meson_clk_lock,
		.flags = CLK_MESON_MPLL_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll3_div",
		.ops = &meson_clk_mpll_ops,
		.parent_names = (const char *[]){ "mpll_prediv" },
		.num_parents = 1,
	},
};

static struct clk_regmap axg_mpll3 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_MPLL3_CNTL0,
		.bit_idx = 0,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpll3",
		.ops = &clk_regmap_gate_ops,
		.parent_names = (const char *[]){ "mpll3_div" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct pll_rate_table axg_pcie_pll_rate_table[] = {
	{
		.rate	= 100000000,
		.m	= 200,
		.n	= 3,
		.od	= 1,
		.od2	= 3,
	},
	{ /* sentinel */ },
};

static const struct reg_sequence axg_pcie_init_regs[] = {
	{ .reg = HHI_PCIE_PLL_CNTL,	.def = 0x400106c8 },
	{ .reg = HHI_PCIE_PLL_CNTL1,	.def = 0x0084a2aa },
	{ .reg = HHI_PCIE_PLL_CNTL2,	.def = 0xb75020be },
	{ .reg = HHI_PCIE_PLL_CNTL3,	.def = 0x0a47488e },
	{ .reg = HHI_PCIE_PLL_CNTL4,	.def = 0xc000004d },
	{ .reg = HHI_PCIE_PLL_CNTL5,	.def = 0x00078000 },
	{ .reg = HHI_PCIE_PLL_CNTL6,	.def = 0x002323c6 },
};

static struct clk_regmap axg_pcie_pll = {
	.data = &(struct meson_clk_pll_data){
		.m = {
			.reg_off = HHI_PCIE_PLL_CNTL,
			.shift   = 0,
			.width   = 9,
		},
		.n = {
			.reg_off = HHI_PCIE_PLL_CNTL,
			.shift   = 9,
			.width   = 5,
		},
		.od = {
			.reg_off = HHI_PCIE_PLL_CNTL,
			.shift   = 16,
			.width   = 2,
		},
		.od2 = {
			.reg_off = HHI_PCIE_PLL_CNTL6,
			.shift   = 6,
			.width   = 2,
		},
		.frac = {
			.reg_off = HHI_PCIE_PLL_CNTL1,
			.shift   = 0,
			.width   = 12,
		},
		.l = {
			.reg_off = HHI_PCIE_PLL_CNTL,
			.shift   = 31,
			.width   = 1,
		},
		.rst = {
			.reg_off = HHI_PCIE_PLL_CNTL,
			.shift   = 29,
			.width   = 1,
		},
		.table = axg_pcie_pll_rate_table,
		.init_regs = axg_pcie_init_regs,
		.init_count = ARRAY_SIZE(axg_pcie_init_regs),
	},
	.hw.init = &(struct clk_init_data){
		.name = "pcie_pll",
		.ops = &meson_clk_pll_ops,
		.parent_names = (const char *[]){ "xtal" },
		.num_parents = 1,
	},
};

static struct clk_regmap axg_pcie_mux = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_PCIE_PLL_CNTL6,
		.mask = 0x1,
		.shift = 2,
		/* skip the parent mpll3, reserved for debug */
		.table = (u32[]){ 1 },
	},
	.hw.init = &(struct clk_init_data){
		.name = "pcie_mux",
		.ops = &clk_regmap_mux_ops,
		.parent_names = (const char *[]){ "pcie_pll" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap axg_pcie_ref = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_PCIE_PLL_CNTL6,
		.mask = 0x1,
		.shift = 1,
		/* skip the parent 0, reserved for debug */
		.table = (u32[]){ 1 },
	},
	.hw.init = &(struct clk_init_data){
		.name = "pcie_ref",
		.ops = &clk_regmap_mux_ops,
		.parent_names = (const char *[]){ "pcie_mux" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap axg_pcie_cml_en0 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_PCIE_PLL_CNTL6,
		.bit_idx = 4,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "pcie_cml_en0",
		.ops = &clk_regmap_gate_ops,
		.parent_names = (const char *[]){ "pcie_ref" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,

	},
};

static struct clk_regmap axg_pcie_cml_en1 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_PCIE_PLL_CNTL6,
		.bit_idx = 3,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "pcie_cml_en1",
		.ops = &clk_regmap_gate_ops,
		.parent_names = (const char *[]){ "pcie_ref" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static u32 mux_table_clk81[]	= { 0, 2, 3, 4, 5, 6, 7 };
static const char * const clk81_parent_names[] = {
	"xtal", "fclk_div7", "mpll1", "mpll2", "fclk_div4",
	"fclk_div3", "fclk_div5"
};

static struct clk_regmap axg_mpeg_clk_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_MPEG_CLK_CNTL,
		.mask = 0x7,
		.shift = 12,
		.table = mux_table_clk81,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpeg_clk_sel",
		.ops = &clk_regmap_mux_ro_ops,
		.parent_names = clk81_parent_names,
		.num_parents = ARRAY_SIZE(clk81_parent_names),
	},
};

static struct clk_regmap axg_mpeg_clk_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_MPEG_CLK_CNTL,
		.shift = 0,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data){
		.name = "mpeg_clk_div",
		.ops = &clk_regmap_divider_ops,
		.parent_names = (const char *[]){ "mpeg_clk_sel" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap axg_clk81 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_MPEG_CLK_CNTL,
		.bit_idx = 7,
	},
	.hw.init = &(struct clk_init_data){
		.name = "clk81",
		.ops = &clk_regmap_gate_ops,
		.parent_names = (const char *[]){ "mpeg_clk_div" },
		.num_parents = 1,
		.flags = (CLK_SET_RATE_PARENT | CLK_IS_CRITICAL),
	},
};

static const char * const axg_sd_emmc_clk0_parent_names[] = {
	"xtal", "fclk_div2", "fclk_div3", "fclk_div5", "fclk_div7",

	/*
	 * Following these parent clocks, we should also have had mpll2, mpll3
	 * and gp0_pll but these clocks are too precious to be used here. All
	 * the necessary rates for MMC and NAND operation can be acheived using
	 * xtal or fclk_div clocks
	 */
};

/* SDcard clock */
static struct clk_regmap axg_sd_emmc_b_clk0_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_SD_EMMC_CLK_CNTL,
		.mask = 0x7,
		.shift = 25,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "sd_emmc_b_clk0_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_names = axg_sd_emmc_clk0_parent_names,
		.num_parents = ARRAY_SIZE(axg_sd_emmc_clk0_parent_names),
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap axg_sd_emmc_b_clk0_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_SD_EMMC_CLK_CNTL,
		.shift = 16,
		.width = 7,
		.flags = CLK_DIVIDER_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "sd_emmc_b_clk0_div",
		.ops = &clk_regmap_divider_ops,
		.parent_names = (const char *[]){ "sd_emmc_b_clk0_sel" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap axg_sd_emmc_b_clk0 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_SD_EMMC_CLK_CNTL,
		.bit_idx = 23,
	},
	.hw.init = &(struct clk_init_data){
		.name = "sd_emmc_b_clk0",
		.ops = &clk_regmap_gate_ops,
		.parent_names = (const char *[]){ "sd_emmc_b_clk0_div" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

/* EMMC/NAND clock */
static struct clk_regmap axg_sd_emmc_c_clk0_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_NAND_CLK_CNTL,
		.mask = 0x7,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "sd_emmc_c_clk0_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_names = axg_sd_emmc_clk0_parent_names,
		.num_parents = ARRAY_SIZE(axg_sd_emmc_clk0_parent_names),
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap axg_sd_emmc_c_clk0_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_NAND_CLK_CNTL,
		.shift = 0,
		.width = 7,
		.flags = CLK_DIVIDER_ROUND_CLOSEST,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "sd_emmc_c_clk0_div",
		.ops = &clk_regmap_divider_ops,
		.parent_names = (const char *[]){ "sd_emmc_c_clk0_sel" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap axg_sd_emmc_c_clk0 = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_NAND_CLK_CNTL,
		.bit_idx = 7,
	},
	.hw.init = &(struct clk_init_data){
		.name = "sd_emmc_c_clk0",
		.ops = &clk_regmap_gate_ops,
		.parent_names = (const char *[]){ "sd_emmc_c_clk0_div" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static u32 mux_table_gen_clk[]	= { 0, 4, 5, 6, 7, 8,
				    9, 10, 11, 13, 14, };
static const char * const gen_clk_parent_names[] = {
	"xtal", "hifi_pll", "mpll0", "mpll1", "mpll2", "mpll3",
	"fclk_div4", "fclk_div3", "fclk_div5", "fclk_div7", "gp0_pll",
};

static struct clk_regmap axg_gen_clk_sel = {
	.data = &(struct clk_regmap_mux_data){
		.offset = HHI_GEN_CLK_CNTL,
		.mask = 0xf,
		.shift = 12,
		.table = mux_table_gen_clk,
	},
	.hw.init = &(struct clk_init_data){
		.name = "gen_clk_sel",
		.ops = &clk_regmap_mux_ops,
		/*
		 * bits 15:12 selects from 14 possible parents:
		 * xtal, [rtc_oscin_i], [sys_cpu_div16], [ddr_dpll_pt],
		 * hifi_pll, mpll0, mpll1, mpll2, mpll3, fdiv4,
		 * fdiv3, fdiv5, [cts_msr_clk], fdiv7, gp0_pll
		 */
		.parent_names = gen_clk_parent_names,
		.num_parents = ARRAY_SIZE(gen_clk_parent_names),
	},
};

static struct clk_regmap axg_gen_clk_div = {
	.data = &(struct clk_regmap_div_data){
		.offset = HHI_GEN_CLK_CNTL,
		.shift = 0,
		.width = 11,
	},
	.hw.init = &(struct clk_init_data){
		.name = "gen_clk_div",
		.ops = &clk_regmap_divider_ops,
		.parent_names = (const char *[]){ "gen_clk_sel" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap axg_gen_clk = {
	.data = &(struct clk_regmap_gate_data){
		.offset = HHI_GEN_CLK_CNTL,
		.bit_idx = 7,
	},
	.hw.init = &(struct clk_init_data){
		.name = "gen_clk",
		.ops = &clk_regmap_gate_ops,
		.parent_names = (const char *[]){ "gen_clk_div" },
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

/* Everything Else (EE) domain gates */
static MESON_GATE(axg_ddr, HHI_GCLK_MPEG0, 0);
static MESON_GATE(axg_audio_locker, HHI_GCLK_MPEG0, 2);
static MESON_GATE(axg_mipi_dsi_host, HHI_GCLK_MPEG0, 3);
static MESON_GATE(axg_isa, HHI_GCLK_MPEG0, 5);
static MESON_GATE(axg_pl301, HHI_GCLK_MPEG0, 6);
static MESON_GATE(axg_periphs, HHI_GCLK_MPEG0, 7);
static MESON_GATE(axg_spicc_0, HHI_GCLK_MPEG0, 8);
static MESON_GATE(axg_i2c, HHI_GCLK_MPEG0, 9);
static MESON_GATE(axg_rng0, HHI_GCLK_MPEG0, 12);
static MESON_GATE(axg_uart0, HHI_GCLK_MPEG0, 13);
static MESON_GATE(axg_mipi_dsi_phy, HHI_GCLK_MPEG0, 14);
static MESON_GATE(axg_spicc_1, HHI_GCLK_MPEG0, 15);
static MESON_GATE(axg_pcie_a, HHI_GCLK_MPEG0, 16);
static MESON_GATE(axg_pcie_b, HHI_GCLK_MPEG0, 17);
static MESON_GATE(axg_hiu_reg, HHI_GCLK_MPEG0, 19);
static MESON_GATE(axg_assist_misc, HHI_GCLK_MPEG0, 23);
static MESON_GATE(axg_emmc_b, HHI_GCLK_MPEG0, 25);
static MESON_GATE(axg_emmc_c, HHI_GCLK_MPEG0, 26);
static MESON_GATE(axg_dma, HHI_GCLK_MPEG0, 27);
static MESON_GATE(axg_spi, HHI_GCLK_MPEG0, 30);

static MESON_GATE(axg_audio, HHI_GCLK_MPEG1, 0);
static MESON_GATE(axg_eth_core, HHI_GCLK_MPEG1, 3);
static MESON_GATE(axg_uart1, HHI_GCLK_MPEG1, 16);
static MESON_GATE(axg_g2d, HHI_GCLK_MPEG1, 20);
static MESON_GATE(axg_usb0, HHI_GCLK_MPEG1, 21);
static MESON_GATE(axg_usb1, HHI_GCLK_MPEG1, 22);
static MESON_GATE(axg_reset, HHI_GCLK_MPEG1, 23);
static MESON_GATE(axg_usb_general, HHI_GCLK_MPEG1, 26);
static MESON_GATE(axg_ahb_arb0, HHI_GCLK_MPEG1, 29);
static MESON_GATE(axg_efuse, HHI_GCLK_MPEG1, 30);
static MESON_GATE(axg_boot_rom, HHI_GCLK_MPEG1, 31);

static MESON_GATE(axg_ahb_data_bus, HHI_GCLK_MPEG2, 1);
static MESON_GATE(axg_ahb_ctrl_bus, HHI_GCLK_MPEG2, 2);
static MESON_GATE(axg_usb1_to_ddr, HHI_GCLK_MPEG2, 8);
static MESON_GATE(axg_usb0_to_ddr, HHI_GCLK_MPEG2, 9);
static MESON_GATE(axg_mmc_pclk, HHI_GCLK_MPEG2, 11);
static MESON_GATE(axg_vpu_intr, HHI_GCLK_MPEG2, 25);
static MESON_GATE(axg_sec_ahb_ahb3_bridge, HHI_GCLK_MPEG2, 26);
static MESON_GATE(axg_gic, HHI_GCLK_MPEG2, 30);
static MESON_GATE(axg_mipi_enable, HHI_MIPI_CNTL0, 29);

/* Always On (AO) domain gates */

static MESON_GATE(axg_ao_media_cpu, HHI_GCLK_AO, 0);
static MESON_GATE(axg_ao_ahb_sram, HHI_GCLK_AO, 1);
static MESON_GATE(axg_ao_ahb_bus, HHI_GCLK_AO, 2);
static MESON_GATE(axg_ao_iface, HHI_GCLK_AO, 3);
static MESON_GATE(axg_ao_i2c, HHI_GCLK_AO, 4);

/* Array of all clocks provided by this provider */

static struct clk_hw_onecell_data axg_hw_onecell_data = {
	.hws = {
		[CLKID_SYS_PLL]			= &axg_sys_pll.hw,
		[CLKID_FIXED_PLL]		= &axg_fixed_pll.hw,
		[CLKID_FCLK_DIV2]		= &axg_fclk_div2.hw,
		[CLKID_FCLK_DIV3]		= &axg_fclk_div3.hw,
		[CLKID_FCLK_DIV4]		= &axg_fclk_div4.hw,
		[CLKID_FCLK_DIV5]		= &axg_fclk_div5.hw,
		[CLKID_FCLK_DIV7]		= &axg_fclk_div7.hw,
		[CLKID_GP0_PLL]			= &axg_gp0_pll.hw,
		[CLKID_MPEG_SEL]		= &axg_mpeg_clk_sel.hw,
		[CLKID_MPEG_DIV]		= &axg_mpeg_clk_div.hw,
		[CLKID_CLK81]			= &axg_clk81.hw,
		[CLKID_MPLL0]			= &axg_mpll0.hw,
		[CLKID_MPLL1]			= &axg_mpll1.hw,
		[CLKID_MPLL2]			= &axg_mpll2.hw,
		[CLKID_MPLL3]			= &axg_mpll3.hw,
		[CLKID_DDR]			= &axg_ddr.hw,
		[CLKID_AUDIO_LOCKER]		= &axg_audio_locker.hw,
		[CLKID_MIPI_DSI_HOST]		= &axg_mipi_dsi_host.hw,
		[CLKID_ISA]			= &axg_isa.hw,
		[CLKID_PL301]			= &axg_pl301.hw,
		[CLKID_PERIPHS]			= &axg_periphs.hw,
		[CLKID_SPICC0]			= &axg_spicc_0.hw,
		[CLKID_I2C]			= &axg_i2c.hw,
		[CLKID_RNG0]			= &axg_rng0.hw,
		[CLKID_UART0]			= &axg_uart0.hw,
		[CLKID_MIPI_DSI_PHY]		= &axg_mipi_dsi_phy.hw,
		[CLKID_SPICC1]			= &axg_spicc_1.hw,
		[CLKID_PCIE_A]			= &axg_pcie_a.hw,
		[CLKID_PCIE_B]			= &axg_pcie_b.hw,
		[CLKID_HIU_IFACE]		= &axg_hiu_reg.hw,
		[CLKID_ASSIST_MISC]		= &axg_assist_misc.hw,
		[CLKID_SD_EMMC_B]		= &axg_emmc_b.hw,
		[CLKID_SD_EMMC_C]		= &axg_emmc_c.hw,
		[CLKID_DMA]			= &axg_dma.hw,
		[CLKID_SPI]			= &axg_spi.hw,
		[CLKID_AUDIO]			= &axg_audio.hw,
		[CLKID_ETH]			= &axg_eth_core.hw,
		[CLKID_UART1]			= &axg_uart1.hw,
		[CLKID_G2D]			= &axg_g2d.hw,
		[CLKID_USB0]			= &axg_usb0.hw,
		[CLKID_USB1]			= &axg_usb1.hw,
		[CLKID_RESET]			= &axg_reset.hw,
		[CLKID_USB]			= &axg_usb_general.hw,
		[CLKID_AHB_ARB0]		= &axg_ahb_arb0.hw,
		[CLKID_EFUSE]			= &axg_efuse.hw,
		[CLKID_BOOT_ROM]		= &axg_boot_rom.hw,
		[CLKID_AHB_DATA_BUS]		= &axg_ahb_data_bus.hw,
		[CLKID_AHB_CTRL_BUS]		= &axg_ahb_ctrl_bus.hw,
		[CLKID_USB1_DDR_BRIDGE]		= &axg_usb1_to_ddr.hw,
		[CLKID_USB0_DDR_BRIDGE]		= &axg_usb0_to_ddr.hw,
		[CLKID_MMC_PCLK]		= &axg_mmc_pclk.hw,
		[CLKID_VPU_INTR]		= &axg_vpu_intr.hw,
		[CLKID_SEC_AHB_AHB3_BRIDGE]	= &axg_sec_ahb_ahb3_bridge.hw,
		[CLKID_GIC]			= &axg_gic.hw,
		[CLKID_AO_MEDIA_CPU]		= &axg_ao_media_cpu.hw,
		[CLKID_AO_AHB_SRAM]		= &axg_ao_ahb_sram.hw,
		[CLKID_AO_AHB_BUS]		= &axg_ao_ahb_bus.hw,
		[CLKID_AO_IFACE]		= &axg_ao_iface.hw,
		[CLKID_AO_I2C]			= &axg_ao_i2c.hw,
		[CLKID_SD_EMMC_B_CLK0_SEL]	= &axg_sd_emmc_b_clk0_sel.hw,
		[CLKID_SD_EMMC_B_CLK0_DIV]	= &axg_sd_emmc_b_clk0_div.hw,
		[CLKID_SD_EMMC_B_CLK0]		= &axg_sd_emmc_b_clk0.hw,
		[CLKID_SD_EMMC_C_CLK0_SEL]	= &axg_sd_emmc_c_clk0_sel.hw,
		[CLKID_SD_EMMC_C_CLK0_DIV]	= &axg_sd_emmc_c_clk0_div.hw,
		[CLKID_SD_EMMC_C_CLK0]		= &axg_sd_emmc_c_clk0.hw,
		[CLKID_MPLL0_DIV]		= &axg_mpll0_div.hw,
		[CLKID_MPLL1_DIV]		= &axg_mpll1_div.hw,
		[CLKID_MPLL2_DIV]		= &axg_mpll2_div.hw,
		[CLKID_MPLL3_DIV]		= &axg_mpll3_div.hw,
		[CLKID_HIFI_PLL]		= &axg_hifi_pll.hw,
		[CLKID_MPLL_PREDIV]		= &axg_mpll_prediv.hw,
		[CLKID_FCLK_DIV2_DIV]		= &axg_fclk_div2_div.hw,
		[CLKID_FCLK_DIV3_DIV]		= &axg_fclk_div3_div.hw,
		[CLKID_FCLK_DIV4_DIV]		= &axg_fclk_div4_div.hw,
		[CLKID_FCLK_DIV5_DIV]		= &axg_fclk_div5_div.hw,
		[CLKID_FCLK_DIV7_DIV]		= &axg_fclk_div7_div.hw,
		[CLKID_PCIE_PLL]		= &axg_pcie_pll.hw,
		[CLKID_PCIE_MUX]		= &axg_pcie_mux.hw,
		[CLKID_PCIE_REF]		= &axg_pcie_ref.hw,
		[CLKID_PCIE_CML_EN0]		= &axg_pcie_cml_en0.hw,
		[CLKID_PCIE_CML_EN1]		= &axg_pcie_cml_en1.hw,
		[CLKID_MIPI_ENABLE]		= &axg_mipi_enable.hw,
		[CLKID_GEN_CLK_SEL]		= &axg_gen_clk_sel.hw,
		[CLKID_GEN_CLK_DIV]		= &axg_gen_clk_div.hw,
		[CLKID_GEN_CLK]			= &axg_gen_clk.hw,
		[NR_CLKS]			= NULL,
	},
	.num = NR_CLKS,
};

/* Convenience table to populate regmap in .probe */
static struct clk_regmap *const axg_clk_regmaps[] = {
	&axg_clk81,
	&axg_ddr,
	&axg_audio_locker,
	&axg_mipi_dsi_host,
	&axg_isa,
	&axg_pl301,
	&axg_periphs,
	&axg_spicc_0,
	&axg_i2c,
	&axg_rng0,
	&axg_uart0,
	&axg_mipi_dsi_phy,
	&axg_spicc_1,
	&axg_pcie_a,
	&axg_pcie_b,
	&axg_hiu_reg,
	&axg_assist_misc,
	&axg_emmc_b,
	&axg_emmc_c,
	&axg_dma,
	&axg_spi,
	&axg_audio,
	&axg_eth_core,
	&axg_uart1,
	&axg_g2d,
	&axg_usb0,
	&axg_usb1,
	&axg_reset,
	&axg_usb_general,
	&axg_ahb_arb0,
	&axg_efuse,
	&axg_boot_rom,
	&axg_ahb_data_bus,
	&axg_ahb_ctrl_bus,
	&axg_usb1_to_ddr,
	&axg_usb0_to_ddr,
	&axg_mmc_pclk,
	&axg_vpu_intr,
	&axg_sec_ahb_ahb3_bridge,
	&axg_gic,
	&axg_ao_media_cpu,
	&axg_ao_ahb_sram,
	&axg_ao_ahb_bus,
	&axg_ao_iface,
	&axg_ao_i2c,
	&axg_sd_emmc_b_clk0,
	&axg_sd_emmc_c_clk0,
	&axg_mpeg_clk_div,
	&axg_sd_emmc_b_clk0_div,
	&axg_sd_emmc_c_clk0_div,
	&axg_mpeg_clk_sel,
	&axg_sd_emmc_b_clk0_sel,
	&axg_sd_emmc_c_clk0_sel,
	&axg_mpll0,
	&axg_mpll1,
	&axg_mpll2,
	&axg_mpll3,
	&axg_mpll0_div,
	&axg_mpll1_div,
	&axg_mpll2_div,
	&axg_mpll3_div,
	&axg_fixed_pll,
	&axg_sys_pll,
	&axg_gp0_pll,
	&axg_hifi_pll,
	&axg_mpll_prediv,
	&axg_fclk_div2,
	&axg_fclk_div3,
	&axg_fclk_div4,
	&axg_fclk_div5,
	&axg_fclk_div7,
	&axg_pcie_pll,
	&axg_pcie_mux,
	&axg_pcie_ref,
	&axg_pcie_cml_en0,
	&axg_pcie_cml_en1,
	&axg_mipi_enable,
	&axg_gen_clk_sel,
	&axg_gen_clk_div,
	&axg_gen_clk,
};

static const struct of_device_id clkc_match_table[] = {
	{ .compatible = "amlogic,axg-clkc" },
	{}
};

static int axg_clkc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct regmap *map;
	int ret, i;

	/* Get the hhi system controller node if available */
	map = syscon_node_to_regmap(of_get_parent(dev->of_node));
	if (IS_ERR(map)) {
		dev_err(dev, "failed to get HHI regmap\n");
		return PTR_ERR(map);
	}

	/* Populate regmap for the regmap backed clocks */
	for (i = 0; i < ARRAY_SIZE(axg_clk_regmaps); i++)
		axg_clk_regmaps[i]->map = map;

	for (i = 0; i < axg_hw_onecell_data.num; i++) {
		/* array might be sparse */
		if (!axg_hw_onecell_data.hws[i])
			continue;

		ret = devm_clk_hw_register(dev, axg_hw_onecell_data.hws[i]);
		if (ret) {
			dev_err(dev, "Clock registration failed\n");
			return ret;
		}
	}

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get,
					   &axg_hw_onecell_data);
}

static struct platform_driver axg_driver = {
	.probe		= axg_clkc_probe,
	.driver		= {
		.name	= "axg-clkc",
		.of_match_table = clkc_match_table,
	},
};

builtin_platform_driver(axg_driver);
