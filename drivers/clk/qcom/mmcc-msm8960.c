/*
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>

#include <dt-bindings/clock/qcom,mmcc-msm8960.h>
#include <dt-bindings/reset/qcom,mmcc-msm8960.h>

#include "common.h"
#include "clk-regmap.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-branch.h"
#include "reset.h"

#define P_PXO	0
#define P_PLL8	1
#define P_PLL2	2
#define P_PLL3	3
#define P_PLL15	3

#define F_MN(f, s, _m, _n) { .freq = f, .src = s, .m = _m, .n = _n }

static u8 mmcc_pxo_pll8_pll2_map[] = {
	[P_PXO]		= 0,
	[P_PLL8]	= 2,
	[P_PLL2]	= 1,
};

static const char *mmcc_pxo_pll8_pll2[] = {
	"pxo",
	"pll8_vote",
	"pll2",
};

static u8 mmcc_pxo_pll8_pll2_pll3_map[] = {
	[P_PXO]		= 0,
	[P_PLL8]	= 2,
	[P_PLL2]	= 1,
	[P_PLL3]	= 3,
};

static const char *mmcc_pxo_pll8_pll2_pll15[] = {
	"pxo",
	"pll8_vote",
	"pll2",
	"pll15",
};

static u8 mmcc_pxo_pll8_pll2_pll15_map[] = {
	[P_PXO]		= 0,
	[P_PLL8]	= 2,
	[P_PLL2]	= 1,
	[P_PLL15]	= 3,
};

static const char *mmcc_pxo_pll8_pll2_pll3[] = {
	"pxo",
	"pll8_vote",
	"pll2",
	"pll3",
};

static struct clk_pll pll2 = {
	.l_reg = 0x320,
	.m_reg = 0x324,
	.n_reg = 0x328,
	.config_reg = 0x32c,
	.mode_reg = 0x31c,
	.status_reg = 0x334,
	.status_bit = 16,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pll2",
		.parent_names = (const char *[]){ "pxo" },
		.num_parents = 1,
		.ops = &clk_pll_ops,
	},
};

static struct clk_pll pll15 = {
	.l_reg = 0x33c,
	.m_reg = 0x340,
	.n_reg = 0x344,
	.config_reg = 0x348,
	.mode_reg = 0x338,
	.status_reg = 0x350,
	.status_bit = 16,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "pll15",
		.parent_names = (const char *[]){ "pxo" },
		.num_parents = 1,
		.ops = &clk_pll_ops,
	},
};

static const struct pll_config pll15_config = {
	.l = 33,
	.m = 1,
	.n = 3,
	.vco_val = 0x2 << 16,
	.vco_mask = 0x3 << 16,
	.pre_div_val = 0x0,
	.pre_div_mask = BIT(19),
	.post_div_val = 0x0,
	.post_div_mask = 0x3 << 20,
	.mn_ena_mask = BIT(22),
	.main_output_mask = BIT(23),
};

static struct freq_tbl clk_tbl_cam[] = {
	{   6000000, P_PLL8, 4, 1, 16 },
	{   8000000, P_PLL8, 4, 1, 12 },
	{  12000000, P_PLL8, 4, 1,  8 },
	{  16000000, P_PLL8, 4, 1,  6 },
	{  19200000, P_PLL8, 4, 1,  5 },
	{  24000000, P_PLL8, 4, 1,  4 },
	{  32000000, P_PLL8, 4, 1,  3 },
	{  48000000, P_PLL8, 4, 1,  2 },
	{  64000000, P_PLL8, 3, 1,  2 },
	{  96000000, P_PLL8, 4, 0,  0 },
	{ 128000000, P_PLL8, 3, 0,  0 },
	{ }
};

static struct clk_rcg camclk0_src = {
	.ns_reg = 0x0148,
	.md_reg = 0x0144,
	.mn = {
		.mnctr_en_bit = 5,
		.mnctr_reset_bit = 8,
		.reset_in_cc = true,
		.mnctr_mode_shift = 6,
		.n_val_shift = 24,
		.m_val_shift = 8,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 14,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = mmcc_pxo_pll8_pll2_map,
	},
	.freq_tbl = clk_tbl_cam,
	.clkr = {
		.enable_reg = 0x0140,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "camclk0_src",
			.parent_names = mmcc_pxo_pll8_pll2,
			.num_parents = 3,
			.ops = &clk_rcg_ops,
		},
	},
};

static struct clk_branch camclk0_clk = {
	.halt_reg = 0x01e8,
	.halt_bit = 15,
	.clkr = {
		.enable_reg = 0x0140,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camclk0_clk",
			.parent_names = (const char *[]){ "camclk0_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},

};

static struct clk_rcg camclk1_src = {
	.ns_reg = 0x015c,
	.md_reg = 0x0158,
	.mn = {
		.mnctr_en_bit = 5,
		.mnctr_reset_bit = 8,
		.reset_in_cc = true,
		.mnctr_mode_shift = 6,
		.n_val_shift = 24,
		.m_val_shift = 8,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 14,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = mmcc_pxo_pll8_pll2_map,
	},
	.freq_tbl = clk_tbl_cam,
	.clkr = {
		.enable_reg = 0x0154,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "camclk1_src",
			.parent_names = mmcc_pxo_pll8_pll2,
			.num_parents = 3,
			.ops = &clk_rcg_ops,
		},
	},
};

static struct clk_branch camclk1_clk = {
	.halt_reg = 0x01e8,
	.halt_bit = 16,
	.clkr = {
		.enable_reg = 0x0154,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camclk1_clk",
			.parent_names = (const char *[]){ "camclk1_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},

};

static struct clk_rcg camclk2_src = {
	.ns_reg = 0x0228,
	.md_reg = 0x0224,
	.mn = {
		.mnctr_en_bit = 5,
		.mnctr_reset_bit = 8,
		.reset_in_cc = true,
		.mnctr_mode_shift = 6,
		.n_val_shift = 24,
		.m_val_shift = 8,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 14,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = mmcc_pxo_pll8_pll2_map,
	},
	.freq_tbl = clk_tbl_cam,
	.clkr = {
		.enable_reg = 0x0220,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "camclk2_src",
			.parent_names = mmcc_pxo_pll8_pll2,
			.num_parents = 3,
			.ops = &clk_rcg_ops,
		},
	},
};

static struct clk_branch camclk2_clk = {
	.halt_reg = 0x01e8,
	.halt_bit = 16,
	.clkr = {
		.enable_reg = 0x0220,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "camclk2_clk",
			.parent_names = (const char *[]){ "camclk2_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
		},
	},

};

static struct freq_tbl clk_tbl_csi[] = {
	{  27000000, P_PXO,  1, 0, 0 },
	{  85330000, P_PLL8, 1, 2, 9 },
	{ 177780000, P_PLL2, 1, 2, 9 },
	{ }
};

static struct clk_rcg csi0_src = {
	.ns_reg = 0x0048,
	.md_reg	= 0x0044,
	.mn = {
		.mnctr_en_bit = 5,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 6,
		.n_val_shift = 24,
		.m_val_shift = 8,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 14,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = mmcc_pxo_pll8_pll2_map,
	},
	.freq_tbl = clk_tbl_csi,
	.clkr = {
		.enable_reg = 0x0040,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "csi0_src",
			.parent_names = mmcc_pxo_pll8_pll2,
			.num_parents = 3,
			.ops = &clk_rcg_ops,
		},
	},
};

static struct clk_branch csi0_clk = {
	.halt_reg = 0x01cc,
	.halt_bit = 13,
	.clkr = {
		.enable_reg = 0x0040,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.parent_names = (const char *[]){ "csi0_src" },
			.num_parents = 1,
			.name = "csi0_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch csi0_phy_clk = {
	.halt_reg = 0x01e8,
	.halt_bit = 9,
	.clkr = {
		.enable_reg = 0x0040,
		.enable_mask = BIT(8),
		.hw.init = &(struct clk_init_data){
			.parent_names = (const char *[]){ "csi0_src" },
			.num_parents = 1,
			.name = "csi0_phy_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg csi1_src = {
	.ns_reg = 0x0010,
	.md_reg	= 0x0028,
	.mn = {
		.mnctr_en_bit = 5,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 6,
		.n_val_shift = 24,
		.m_val_shift = 8,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 14,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = mmcc_pxo_pll8_pll2_map,
	},
	.freq_tbl = clk_tbl_csi,
	.clkr = {
		.enable_reg = 0x0024,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "csi1_src",
			.parent_names = mmcc_pxo_pll8_pll2,
			.num_parents = 3,
			.ops = &clk_rcg_ops,
		},
	},
};

static struct clk_branch csi1_clk = {
	.halt_reg = 0x01cc,
	.halt_bit = 14,
	.clkr = {
		.enable_reg = 0x0024,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.parent_names = (const char *[]){ "csi1_src" },
			.num_parents = 1,
			.name = "csi1_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch csi1_phy_clk = {
	.halt_reg = 0x01e8,
	.halt_bit = 10,
	.clkr = {
		.enable_reg = 0x0024,
		.enable_mask = BIT(8),
		.hw.init = &(struct clk_init_data){
			.parent_names = (const char *[]){ "csi1_src" },
			.num_parents = 1,
			.name = "csi1_phy_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_rcg csi2_src = {
	.ns_reg = 0x0234,
	.md_reg = 0x022c,
	.mn = {
		.mnctr_en_bit = 5,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 6,
		.n_val_shift = 24,
		.m_val_shift = 8,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 14,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = mmcc_pxo_pll8_pll2_map,
	},
	.freq_tbl = clk_tbl_csi,
	.clkr = {
		.enable_reg = 0x022c,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "csi2_src",
			.parent_names = mmcc_pxo_pll8_pll2,
			.num_parents = 3,
			.ops = &clk_rcg_ops,
		},
	},
};

static struct clk_branch csi2_clk = {
	.halt_reg = 0x01cc,
	.halt_bit = 29,
	.clkr = {
		.enable_reg = 0x022c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.parent_names = (const char *[]){ "csi2_src" },
			.num_parents = 1,
			.name = "csi2_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch csi2_phy_clk = {
	.halt_reg = 0x01e8,
	.halt_bit = 29,
	.clkr = {
		.enable_reg = 0x022c,
		.enable_mask = BIT(8),
		.hw.init = &(struct clk_init_data){
			.parent_names = (const char *[]){ "csi2_src" },
			.num_parents = 1,
			.name = "csi2_phy_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

struct clk_pix_rdi {
	u32 s_reg;
	u32 s_mask;
	u32 s2_reg;
	u32 s2_mask;
	struct clk_regmap clkr;
};

#define to_clk_pix_rdi(_hw) \
	container_of(to_clk_regmap(_hw), struct clk_pix_rdi, clkr)

static int pix_rdi_set_parent(struct clk_hw *hw, u8 index)
{
	int i;
	int ret = 0;
	u32 val;
	struct clk_pix_rdi *rdi = to_clk_pix_rdi(hw);
	struct clk *clk = hw->clk;
	int num_parents = __clk_get_num_parents(hw->clk);

	/*
	 * These clocks select three inputs via two muxes. One mux selects
	 * between csi0 and csi1 and the second mux selects between that mux's
	 * output and csi2. The source and destination selections for each
	 * mux must be clocking for the switch to succeed so just turn on
	 * all three sources because it's easier than figuring out what source
	 * needs to be on at what time.
	 */
	for (i = 0; i < num_parents; i++) {
		ret = clk_prepare_enable(clk_get_parent_by_index(clk, i));
		if (ret)
			goto err;
	}

	if (index == 2)
		val = rdi->s2_mask;
	else
		val = 0;
	regmap_update_bits(rdi->clkr.regmap, rdi->s2_reg, rdi->s2_mask, val);
	/*
	 * Wait at least 6 cycles of slowest clock
	 * for the glitch-free MUX to fully switch sources.
	 */
	udelay(1);

	if (index == 1)
		val = rdi->s_mask;
	else
		val = 0;
	regmap_update_bits(rdi->clkr.regmap, rdi->s_reg, rdi->s_mask, val);
	/*
	 * Wait at least 6 cycles of slowest clock
	 * for the glitch-free MUX to fully switch sources.
	 */
	udelay(1);

err:
	for (i--; i >= 0; i--)
		clk_disable_unprepare(clk_get_parent_by_index(clk, i));

	return ret;
}

static u8 pix_rdi_get_parent(struct clk_hw *hw)
{
	u32 val;
	struct clk_pix_rdi *rdi = to_clk_pix_rdi(hw);


	regmap_read(rdi->clkr.regmap, rdi->s2_reg, &val);
	if (val & rdi->s2_mask)
		return 2;

	regmap_read(rdi->clkr.regmap, rdi->s_reg, &val);
	if (val & rdi->s_mask)
		return 1;

	return 0;
}

static const struct clk_ops clk_ops_pix_rdi = {
	.enable = clk_enable_regmap,
	.disable = clk_disable_regmap,
	.set_parent = pix_rdi_set_parent,
	.get_parent = pix_rdi_get_parent,
	.determine_rate = __clk_mux_determine_rate,
};

static const char *pix_rdi_parents[] = {
	"csi0_clk",
	"csi1_clk",
	"csi2_clk",
};

static struct clk_pix_rdi csi_pix_clk = {
	.s_reg = 0x0058,
	.s_mask = BIT(25),
	.s2_reg = 0x0238,
	.s2_mask = BIT(13),
	.clkr = {
		.enable_reg = 0x0058,
		.enable_mask = BIT(26),
		.hw.init = &(struct clk_init_data){
			.name = "csi_pix_clk",
			.parent_names = pix_rdi_parents,
			.num_parents = 3,
			.ops = &clk_ops_pix_rdi,
		},
	},
};

static struct clk_pix_rdi csi_pix1_clk = {
	.s_reg = 0x0238,
	.s_mask = BIT(8),
	.s2_reg = 0x0238,
	.s2_mask = BIT(9),
	.clkr = {
		.enable_reg = 0x0238,
		.enable_mask = BIT(10),
		.hw.init = &(struct clk_init_data){
			.name = "csi_pix1_clk",
			.parent_names = pix_rdi_parents,
			.num_parents = 3,
			.ops = &clk_ops_pix_rdi,
		},
	},
};

static struct clk_pix_rdi csi_rdi_clk = {
	.s_reg = 0x0058,
	.s_mask = BIT(12),
	.s2_reg = 0x0238,
	.s2_mask = BIT(12),
	.clkr = {
		.enable_reg = 0x0058,
		.enable_mask = BIT(13),
		.hw.init = &(struct clk_init_data){
			.name = "csi_rdi_clk",
			.parent_names = pix_rdi_parents,
			.num_parents = 3,
			.ops = &clk_ops_pix_rdi,
		},
	},
};

static struct clk_pix_rdi csi_rdi1_clk = {
	.s_reg = 0x0238,
	.s_mask = BIT(0),
	.s2_reg = 0x0238,
	.s2_mask = BIT(1),
	.clkr = {
		.enable_reg = 0x0238,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "csi_rdi1_clk",
			.parent_names = pix_rdi_parents,
			.num_parents = 3,
			.ops = &clk_ops_pix_rdi,
		},
	},
};

static struct clk_pix_rdi csi_rdi2_clk = {
	.s_reg = 0x0238,
	.s_mask = BIT(4),
	.s2_reg = 0x0238,
	.s2_mask = BIT(5),
	.clkr = {
		.enable_reg = 0x0238,
		.enable_mask = BIT(6),
		.hw.init = &(struct clk_init_data){
			.name = "csi_rdi2_clk",
			.parent_names = pix_rdi_parents,
			.num_parents = 3,
			.ops = &clk_ops_pix_rdi,
		},
	},
};

static struct freq_tbl clk_tbl_csiphytimer[] = {
	{  85330000, P_PLL8, 1, 2, 9 },
	{ 177780000, P_PLL2, 1, 2, 9 },
	{ }
};

static struct clk_rcg csiphytimer_src = {
	.ns_reg = 0x0168,
	.md_reg = 0x0164,
	.mn = {
		.mnctr_en_bit = 5,
		.mnctr_reset_bit = 8,
		.reset_in_cc = true,
		.mnctr_mode_shift = 6,
		.n_val_shift = 24,
		.m_val_shift = 8,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 14,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = mmcc_pxo_pll8_pll2_map,
	},
	.freq_tbl = clk_tbl_csiphytimer,
	.clkr = {
		.enable_reg = 0x0160,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "csiphytimer_src",
			.parent_names = mmcc_pxo_pll8_pll2,
			.num_parents = 3,
			.ops = &clk_rcg_ops,
		},
	},
};

static const char *csixphy_timer_src[] = { "csiphytimer_src" };

static struct clk_branch csiphy0_timer_clk = {
	.halt_reg = 0x01e8,
	.halt_bit = 17,
	.clkr = {
		.enable_reg = 0x0160,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.parent_names = csixphy_timer_src,
			.num_parents = 1,
			.name = "csiphy0_timer_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch csiphy1_timer_clk = {
	.halt_reg = 0x01e8,
	.halt_bit = 18,
	.clkr = {
		.enable_reg = 0x0160,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.parent_names = csixphy_timer_src,
			.num_parents = 1,
			.name = "csiphy1_timer_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch csiphy2_timer_clk = {
	.halt_reg = 0x01e8,
	.halt_bit = 30,
	.clkr = {
		.enable_reg = 0x0160,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.parent_names = csixphy_timer_src,
			.num_parents = 1,
			.name = "csiphy2_timer_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct freq_tbl clk_tbl_gfx2d[] = {
	F_MN( 27000000, P_PXO,  1,  0),
	F_MN( 48000000, P_PLL8, 1,  8),
	F_MN( 54857000, P_PLL8, 1,  7),
	F_MN( 64000000, P_PLL8, 1,  6),
	F_MN( 76800000, P_PLL8, 1,  5),
	F_MN( 96000000, P_PLL8, 1,  4),
	F_MN(128000000, P_PLL8, 1,  3),
	F_MN(145455000, P_PLL2, 2, 11),
	F_MN(160000000, P_PLL2, 1,  5),
	F_MN(177778000, P_PLL2, 2,  9),
	F_MN(200000000, P_PLL2, 1,  4),
	F_MN(228571000, P_PLL2, 2,  7),
	{ }
};

static struct clk_dyn_rcg gfx2d0_src = {
	.ns_reg[0] = 0x0070,
	.ns_reg[1] = 0x0070,
	.md_reg[0] = 0x0064,
	.md_reg[1] = 0x0068,
	.bank_reg = 0x0060,
	.mn[0] = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 25,
		.mnctr_mode_shift = 9,
		.n_val_shift = 20,
		.m_val_shift = 4,
		.width = 4,
	},
	.mn[1] = {
		.mnctr_en_bit = 5,
		.mnctr_reset_bit = 24,
		.mnctr_mode_shift = 6,
		.n_val_shift = 16,
		.m_val_shift = 4,
		.width = 4,
	},
	.s[0] = {
		.src_sel_shift = 3,
		.parent_map = mmcc_pxo_pll8_pll2_map,
	},
	.s[1] = {
		.src_sel_shift = 0,
		.parent_map = mmcc_pxo_pll8_pll2_map,
	},
	.mux_sel_bit = 11,
	.freq_tbl = clk_tbl_gfx2d,
	.clkr = {
		.enable_reg = 0x0060,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "gfx2d0_src",
			.parent_names = mmcc_pxo_pll8_pll2,
			.num_parents = 3,
			.ops = &clk_dyn_rcg_ops,
		},
	},
};

static struct clk_branch gfx2d0_clk = {
	.halt_reg = 0x01c8,
	.halt_bit = 9,
	.clkr = {
		.enable_reg = 0x0060,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gfx2d0_clk",
			.parent_names = (const char *[]){ "gfx2d0_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_dyn_rcg gfx2d1_src = {
	.ns_reg[0] = 0x007c,
	.ns_reg[1] = 0x007c,
	.md_reg[0] = 0x0078,
	.md_reg[1] = 0x006c,
	.bank_reg = 0x0074,
	.mn[0] = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 25,
		.mnctr_mode_shift = 9,
		.n_val_shift = 20,
		.m_val_shift = 4,
		.width = 4,
	},
	.mn[1] = {
		.mnctr_en_bit = 5,
		.mnctr_reset_bit = 24,
		.mnctr_mode_shift = 6,
		.n_val_shift = 16,
		.m_val_shift = 4,
		.width = 4,
	},
	.s[0] = {
		.src_sel_shift = 3,
		.parent_map = mmcc_pxo_pll8_pll2_map,
	},
	.s[1] = {
		.src_sel_shift = 0,
		.parent_map = mmcc_pxo_pll8_pll2_map,
	},
	.mux_sel_bit = 11,
	.freq_tbl = clk_tbl_gfx2d,
	.clkr = {
		.enable_reg = 0x0074,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "gfx2d1_src",
			.parent_names = mmcc_pxo_pll8_pll2,
			.num_parents = 3,
			.ops = &clk_dyn_rcg_ops,
		},
	},
};

static struct clk_branch gfx2d1_clk = {
	.halt_reg = 0x01c8,
	.halt_bit = 14,
	.clkr = {
		.enable_reg = 0x0074,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gfx2d1_clk",
			.parent_names = (const char *[]){ "gfx2d1_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct freq_tbl clk_tbl_gfx3d[] = {
	F_MN( 27000000, P_PXO,  1,  0),
	F_MN( 48000000, P_PLL8, 1,  8),
	F_MN( 54857000, P_PLL8, 1,  7),
	F_MN( 64000000, P_PLL8, 1,  6),
	F_MN( 76800000, P_PLL8, 1,  5),
	F_MN( 96000000, P_PLL8, 1,  4),
	F_MN(128000000, P_PLL8, 1,  3),
	F_MN(145455000, P_PLL2, 2, 11),
	F_MN(160000000, P_PLL2, 1,  5),
	F_MN(177778000, P_PLL2, 2,  9),
	F_MN(200000000, P_PLL2, 1,  4),
	F_MN(228571000, P_PLL2, 2,  7),
	F_MN(266667000, P_PLL2, 1,  3),
	F_MN(300000000, P_PLL3, 1,  4),
	F_MN(320000000, P_PLL2, 2,  5),
	F_MN(400000000, P_PLL2, 1,  2),
	{ }
};

static struct freq_tbl clk_tbl_gfx3d_8064[] = {
	F_MN( 27000000, P_PXO,   0,  0),
	F_MN( 48000000, P_PLL8,  1,  8),
	F_MN( 54857000, P_PLL8,  1,  7),
	F_MN( 64000000, P_PLL8,  1,  6),
	F_MN( 76800000, P_PLL8,  1,  5),
	F_MN( 96000000, P_PLL8,  1,  4),
	F_MN(128000000, P_PLL8,  1,  3),
	F_MN(145455000, P_PLL2,  2, 11),
	F_MN(160000000, P_PLL2,  1,  5),
	F_MN(177778000, P_PLL2,  2,  9),
	F_MN(192000000, P_PLL8,  1,  2),
	F_MN(200000000, P_PLL2,  1,  4),
	F_MN(228571000, P_PLL2,  2,  7),
	F_MN(266667000, P_PLL2,  1,  3),
	F_MN(320000000, P_PLL2,  2,  5),
	F_MN(400000000, P_PLL2,  1,  2),
	F_MN(450000000, P_PLL15, 1,  2),
	{ }
};

static struct clk_dyn_rcg gfx3d_src = {
	.ns_reg[0] = 0x008c,
	.ns_reg[1] = 0x008c,
	.md_reg[0] = 0x0084,
	.md_reg[1] = 0x0088,
	.bank_reg = 0x0080,
	.mn[0] = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 25,
		.mnctr_mode_shift = 9,
		.n_val_shift = 18,
		.m_val_shift = 4,
		.width = 4,
	},
	.mn[1] = {
		.mnctr_en_bit = 5,
		.mnctr_reset_bit = 24,
		.mnctr_mode_shift = 6,
		.n_val_shift = 14,
		.m_val_shift = 4,
		.width = 4,
	},
	.s[0] = {
		.src_sel_shift = 3,
		.parent_map = mmcc_pxo_pll8_pll2_pll3_map,
	},
	.s[1] = {
		.src_sel_shift = 0,
		.parent_map = mmcc_pxo_pll8_pll2_pll3_map,
	},
	.mux_sel_bit = 11,
	.freq_tbl = clk_tbl_gfx3d,
	.clkr = {
		.enable_reg = 0x0080,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "gfx3d_src",
			.parent_names = mmcc_pxo_pll8_pll2_pll3,
			.num_parents = 4,
			.ops = &clk_dyn_rcg_ops,
		},
	},
};

static const struct clk_init_data gfx3d_8064_init = {
	.name = "gfx3d_src",
	.parent_names = mmcc_pxo_pll8_pll2_pll15,
	.num_parents = 4,
	.ops = &clk_dyn_rcg_ops,
};

static struct clk_branch gfx3d_clk = {
	.halt_reg = 0x01c8,
	.halt_bit = 4,
	.clkr = {
		.enable_reg = 0x0080,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gfx3d_clk",
			.parent_names = (const char *[]){ "gfx3d_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct freq_tbl clk_tbl_vcap[] = {
	F_MN( 27000000, P_PXO,  0,  0),
	F_MN( 54860000, P_PLL8, 1,  7),
	F_MN( 64000000, P_PLL8, 1,  6),
	F_MN( 76800000, P_PLL8, 1,  5),
	F_MN(128000000, P_PLL8, 1,  3),
	F_MN(160000000, P_PLL2, 1,  5),
	F_MN(200000000, P_PLL2, 1,  4),
	{ }
};

static struct clk_dyn_rcg vcap_src = {
	.ns_reg[0] = 0x021c,
	.ns_reg[1] = 0x021c,
	.md_reg[0] = 0x01ec,
	.md_reg[1] = 0x0218,
	.bank_reg = 0x0178,
	.mn[0] = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 23,
		.mnctr_mode_shift = 9,
		.n_val_shift = 18,
		.m_val_shift = 4,
		.width = 4,
	},
	.mn[1] = {
		.mnctr_en_bit = 5,
		.mnctr_reset_bit = 22,
		.mnctr_mode_shift = 6,
		.n_val_shift = 14,
		.m_val_shift = 4,
		.width = 4,
	},
	.s[0] = {
		.src_sel_shift = 3,
		.parent_map = mmcc_pxo_pll8_pll2_map,
	},
	.s[1] = {
		.src_sel_shift = 0,
		.parent_map = mmcc_pxo_pll8_pll2_map,
	},
	.mux_sel_bit = 11,
	.freq_tbl = clk_tbl_vcap,
	.clkr = {
		.enable_reg = 0x0178,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "vcap_src",
			.parent_names = mmcc_pxo_pll8_pll2,
			.num_parents = 3,
			.ops = &clk_dyn_rcg_ops,
		},
	},
};

static struct clk_branch vcap_clk = {
	.halt_reg = 0x0240,
	.halt_bit = 15,
	.clkr = {
		.enable_reg = 0x0178,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "vcap_clk",
			.parent_names = (const char *[]){ "vcap_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch vcap_npl_clk = {
	.halt_reg = 0x0240,
	.halt_bit = 25,
	.clkr = {
		.enable_reg = 0x0178,
		.enable_mask = BIT(13),
		.hw.init = &(struct clk_init_data){
			.name = "vcap_npl_clk",
			.parent_names = (const char *[]){ "vcap_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct freq_tbl clk_tbl_ijpeg[] = {
	{  27000000, P_PXO,  1, 0,  0 },
	{  36570000, P_PLL8, 1, 2, 21 },
	{  54860000, P_PLL8, 7, 0,  0 },
	{  96000000, P_PLL8, 4, 0,  0 },
	{ 109710000, P_PLL8, 1, 2,  7 },
	{ 128000000, P_PLL8, 3, 0,  0 },
	{ 153600000, P_PLL8, 1, 2,  5 },
	{ 200000000, P_PLL2, 4, 0,  0 },
	{ 228571000, P_PLL2, 1, 2,  7 },
	{ 266667000, P_PLL2, 1, 1,  3 },
	{ 320000000, P_PLL2, 1, 2,  5 },
	{ }
};

static struct clk_rcg ijpeg_src = {
	.ns_reg = 0x00a0,
	.md_reg = 0x009c,
	.mn = {
		.mnctr_en_bit = 5,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 6,
		.n_val_shift = 16,
		.m_val_shift = 8,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 12,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = mmcc_pxo_pll8_pll2_map,
	},
	.freq_tbl = clk_tbl_ijpeg,
	.clkr = {
		.enable_reg = 0x0098,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "ijpeg_src",
			.parent_names = mmcc_pxo_pll8_pll2,
			.num_parents = 3,
			.ops = &clk_rcg_ops,
		},
	},
};

static struct clk_branch ijpeg_clk = {
	.halt_reg = 0x01c8,
	.halt_bit = 24,
	.clkr = {
		.enable_reg = 0x0098,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "ijpeg_clk",
			.parent_names = (const char *[]){ "ijpeg_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct freq_tbl clk_tbl_jpegd[] = {
	{  64000000, P_PLL8, 6 },
	{  76800000, P_PLL8, 5 },
	{  96000000, P_PLL8, 4 },
	{ 160000000, P_PLL2, 5 },
	{ 200000000, P_PLL2, 4 },
	{ }
};

static struct clk_rcg jpegd_src = {
	.ns_reg = 0x00ac,
	.p = {
		.pre_div_shift = 12,
		.pre_div_width = 4,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = mmcc_pxo_pll8_pll2_map,
	},
	.freq_tbl = clk_tbl_jpegd,
	.clkr = {
		.enable_reg = 0x00a4,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "jpegd_src",
			.parent_names = mmcc_pxo_pll8_pll2,
			.num_parents = 3,
			.ops = &clk_rcg_ops,
		},
	},
};

static struct clk_branch jpegd_clk = {
	.halt_reg = 0x01c8,
	.halt_bit = 19,
	.clkr = {
		.enable_reg = 0x00a4,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "jpegd_clk",
			.parent_names = (const char *[]){ "jpegd_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct freq_tbl clk_tbl_mdp[] = {
	{   9600000, P_PLL8, 1, 1, 40 },
	{  13710000, P_PLL8, 1, 1, 28 },
	{  27000000, P_PXO,  1, 0,  0 },
	{  29540000, P_PLL8, 1, 1, 13 },
	{  34910000, P_PLL8, 1, 1, 11 },
	{  38400000, P_PLL8, 1, 1, 10 },
	{  59080000, P_PLL8, 1, 2, 13 },
	{  76800000, P_PLL8, 1, 1,  5 },
	{  85330000, P_PLL8, 1, 2,  9 },
	{  96000000, P_PLL8, 1, 1,  4 },
	{ 128000000, P_PLL8, 1, 1,  3 },
	{ 160000000, P_PLL2, 1, 1,  5 },
	{ 177780000, P_PLL2, 1, 2,  9 },
	{ 200000000, P_PLL2, 1, 1,  4 },
	{ 228571000, P_PLL2, 1, 2,  7 },
	{ 266667000, P_PLL2, 1, 1,  3 },
	{ }
};

static struct clk_dyn_rcg mdp_src = {
	.ns_reg[0] = 0x00d0,
	.ns_reg[1] = 0x00d0,
	.md_reg[0] = 0x00c4,
	.md_reg[1] = 0x00c8,
	.bank_reg = 0x00c0,
	.mn[0] = {
		.mnctr_en_bit = 8,
		.mnctr_reset_bit = 31,
		.mnctr_mode_shift = 9,
		.n_val_shift = 22,
		.m_val_shift = 8,
		.width = 8,
	},
	.mn[1] = {
		.mnctr_en_bit = 5,
		.mnctr_reset_bit = 30,
		.mnctr_mode_shift = 6,
		.n_val_shift = 14,
		.m_val_shift = 8,
		.width = 8,
	},
	.s[0] = {
		.src_sel_shift = 3,
		.parent_map = mmcc_pxo_pll8_pll2_map,
	},
	.s[1] = {
		.src_sel_shift = 0,
		.parent_map = mmcc_pxo_pll8_pll2_map,
	},
	.mux_sel_bit = 11,
	.freq_tbl = clk_tbl_mdp,
	.clkr = {
		.enable_reg = 0x00c0,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "mdp_src",
			.parent_names = mmcc_pxo_pll8_pll2,
			.num_parents = 3,
			.ops = &clk_dyn_rcg_ops,
		},
	},
};

static struct clk_branch mdp_clk = {
	.halt_reg = 0x01d0,
	.halt_bit = 10,
	.clkr = {
		.enable_reg = 0x00c0,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "mdp_clk",
			.parent_names = (const char *[]){ "mdp_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch mdp_lut_clk = {
	.halt_reg = 0x01e8,
	.halt_bit = 13,
	.clkr = {
		.enable_reg = 0x016c,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.parent_names = (const char *[]){ "mdp_src" },
			.num_parents = 1,
			.name = "mdp_lut_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch mdp_vsync_clk = {
	.halt_reg = 0x01cc,
	.halt_bit = 22,
	.clkr = {
		.enable_reg = 0x0058,
		.enable_mask = BIT(6),
		.hw.init = &(struct clk_init_data){
			.name = "mdp_vsync_clk",
			.parent_names = (const char *[]){ "pxo" },
			.num_parents = 1,
			.ops = &clk_branch_ops
		},
	},
};

static struct freq_tbl clk_tbl_rot[] = {
	{  27000000, P_PXO,   1 },
	{  29540000, P_PLL8, 13 },
	{  32000000, P_PLL8, 12 },
	{  38400000, P_PLL8, 10 },
	{  48000000, P_PLL8,  8 },
	{  54860000, P_PLL8,  7 },
	{  64000000, P_PLL8,  6 },
	{  76800000, P_PLL8,  5 },
	{  96000000, P_PLL8,  4 },
	{ 100000000, P_PLL2,  8 },
	{ 114290000, P_PLL2,  7 },
	{ 133330000, P_PLL2,  6 },
	{ 160000000, P_PLL2,  5 },
	{ 200000000, P_PLL2,  4 },
	{ }
};

static struct clk_dyn_rcg rot_src = {
	.ns_reg[0] = 0x00e8,
	.ns_reg[1] = 0x00e8,
	.bank_reg = 0x00e8,
	.p[0] = {
		.pre_div_shift = 22,
		.pre_div_width = 4,
	},
	.p[1] = {
		.pre_div_shift = 26,
		.pre_div_width = 4,
	},
	.s[0] = {
		.src_sel_shift = 16,
		.parent_map = mmcc_pxo_pll8_pll2_map,
	},
	.s[1] = {
		.src_sel_shift = 19,
		.parent_map = mmcc_pxo_pll8_pll2_map,
	},
	.mux_sel_bit = 30,
	.freq_tbl = clk_tbl_rot,
	.clkr = {
		.enable_reg = 0x00e0,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "rot_src",
			.parent_names = mmcc_pxo_pll8_pll2,
			.num_parents = 3,
			.ops = &clk_dyn_rcg_ops,
		},
	},
};

static struct clk_branch rot_clk = {
	.halt_reg = 0x01d0,
	.halt_bit = 15,
	.clkr = {
		.enable_reg = 0x00e0,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "rot_clk",
			.parent_names = (const char *[]){ "rot_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

#define P_HDMI_PLL 1

static u8 mmcc_pxo_hdmi_map[] = {
	[P_PXO]		= 0,
	[P_HDMI_PLL]	= 3,
};

static const char *mmcc_pxo_hdmi[] = {
	"pxo",
	"hdmi_pll",
};

static struct freq_tbl clk_tbl_tv[] = {
	{  .src = P_HDMI_PLL, .pre_div = 1 },
	{ }
};

static struct clk_rcg tv_src = {
	.ns_reg = 0x00f4,
	.md_reg = 0x00f0,
	.mn = {
		.mnctr_en_bit = 5,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 6,
		.n_val_shift = 16,
		.m_val_shift = 8,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 14,
		.pre_div_width = 2,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = mmcc_pxo_hdmi_map,
	},
	.freq_tbl = clk_tbl_tv,
	.clkr = {
		.enable_reg = 0x00ec,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "tv_src",
			.parent_names = mmcc_pxo_hdmi,
			.num_parents = 2,
			.ops = &clk_rcg_bypass_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static const char *tv_src_name[] = { "tv_src" };

static struct clk_branch tv_enc_clk = {
	.halt_reg = 0x01d4,
	.halt_bit = 9,
	.clkr = {
		.enable_reg = 0x00ec,
		.enable_mask = BIT(8),
		.hw.init = &(struct clk_init_data){
			.parent_names = tv_src_name,
			.num_parents = 1,
			.name = "tv_enc_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch tv_dac_clk = {
	.halt_reg = 0x01d4,
	.halt_bit = 10,
	.clkr = {
		.enable_reg = 0x00ec,
		.enable_mask = BIT(10),
		.hw.init = &(struct clk_init_data){
			.parent_names = tv_src_name,
			.num_parents = 1,
			.name = "tv_dac_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch mdp_tv_clk = {
	.halt_reg = 0x01d4,
	.halt_bit = 12,
	.clkr = {
		.enable_reg = 0x00ec,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.parent_names = tv_src_name,
			.num_parents = 1,
			.name = "mdp_tv_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch hdmi_tv_clk = {
	.halt_reg = 0x01d4,
	.halt_bit = 11,
	.clkr = {
		.enable_reg = 0x00ec,
		.enable_mask = BIT(12),
		.hw.init = &(struct clk_init_data){
			.parent_names = tv_src_name,
			.num_parents = 1,
			.name = "hdmi_tv_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch rgb_tv_clk = {
	.halt_reg = 0x0240,
	.halt_bit = 27,
	.clkr = {
		.enable_reg = 0x0124,
		.enable_mask = BIT(14),
		.hw.init = &(struct clk_init_data){
			.parent_names = tv_src_name,
			.num_parents = 1,
			.name = "rgb_tv_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch npl_tv_clk = {
	.halt_reg = 0x0240,
	.halt_bit = 26,
	.clkr = {
		.enable_reg = 0x0124,
		.enable_mask = BIT(16),
		.hw.init = &(struct clk_init_data){
			.parent_names = tv_src_name,
			.num_parents = 1,
			.name = "npl_tv_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch hdmi_app_clk = {
	.halt_reg = 0x01cc,
	.halt_bit = 25,
	.clkr = {
		.enable_reg = 0x005c,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.parent_names = (const char *[]){ "pxo" },
			.num_parents = 1,
			.name = "hdmi_app_clk",
			.ops = &clk_branch_ops,
		},
	},
};

static struct freq_tbl clk_tbl_vcodec[] = {
	F_MN( 27000000, P_PXO,  1,  0),
	F_MN( 32000000, P_PLL8, 1, 12),
	F_MN( 48000000, P_PLL8, 1,  8),
	F_MN( 54860000, P_PLL8, 1,  7),
	F_MN( 96000000, P_PLL8, 1,  4),
	F_MN(133330000, P_PLL2, 1,  6),
	F_MN(200000000, P_PLL2, 1,  4),
	F_MN(228570000, P_PLL2, 2,  7),
	F_MN(266670000, P_PLL2, 1,  3),
	{ }
};

static struct clk_dyn_rcg vcodec_src = {
	.ns_reg[0] = 0x0100,
	.ns_reg[1] = 0x0100,
	.md_reg[0] = 0x00fc,
	.md_reg[1] = 0x0128,
	.bank_reg = 0x00f8,
	.mn[0] = {
		.mnctr_en_bit = 5,
		.mnctr_reset_bit = 31,
		.mnctr_mode_shift = 6,
		.n_val_shift = 11,
		.m_val_shift = 8,
		.width = 8,
	},
	.mn[1] = {
		.mnctr_en_bit = 10,
		.mnctr_reset_bit = 30,
		.mnctr_mode_shift = 11,
		.n_val_shift = 19,
		.m_val_shift = 8,
		.width = 8,
	},
	.s[0] = {
		.src_sel_shift = 27,
		.parent_map = mmcc_pxo_pll8_pll2_map,
	},
	.s[1] = {
		.src_sel_shift = 0,
		.parent_map = mmcc_pxo_pll8_pll2_map,
	},
	.mux_sel_bit = 13,
	.freq_tbl = clk_tbl_vcodec,
	.clkr = {
		.enable_reg = 0x00f8,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "vcodec_src",
			.parent_names = mmcc_pxo_pll8_pll2,
			.num_parents = 3,
			.ops = &clk_dyn_rcg_ops,
		},
	},
};

static struct clk_branch vcodec_clk = {
	.halt_reg = 0x01d0,
	.halt_bit = 29,
	.clkr = {
		.enable_reg = 0x00f8,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "vcodec_clk",
			.parent_names = (const char *[]){ "vcodec_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct freq_tbl clk_tbl_vpe[] = {
	{  27000000, P_PXO,   1 },
	{  34909000, P_PLL8, 11 },
	{  38400000, P_PLL8, 10 },
	{  64000000, P_PLL8,  6 },
	{  76800000, P_PLL8,  5 },
	{  96000000, P_PLL8,  4 },
	{ 100000000, P_PLL2,  8 },
	{ 160000000, P_PLL2,  5 },
	{ }
};

static struct clk_rcg vpe_src = {
	.ns_reg = 0x0118,
	.p = {
		.pre_div_shift = 12,
		.pre_div_width = 4,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = mmcc_pxo_pll8_pll2_map,
	},
	.freq_tbl = clk_tbl_vpe,
	.clkr = {
		.enable_reg = 0x0110,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "vpe_src",
			.parent_names = mmcc_pxo_pll8_pll2,
			.num_parents = 3,
			.ops = &clk_rcg_ops,
		},
	},
};

static struct clk_branch vpe_clk = {
	.halt_reg = 0x01c8,
	.halt_bit = 28,
	.clkr = {
		.enable_reg = 0x0110,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "vpe_clk",
			.parent_names = (const char *[]){ "vpe_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct freq_tbl clk_tbl_vfe[] = {
	{  13960000, P_PLL8,  1, 2, 55 },
	{  27000000, P_PXO,   1, 0,  0 },
	{  36570000, P_PLL8,  1, 2, 21 },
	{  38400000, P_PLL8,  2, 1,  5 },
	{  45180000, P_PLL8,  1, 2, 17 },
	{  48000000, P_PLL8,  2, 1,  4 },
	{  54860000, P_PLL8,  1, 1,  7 },
	{  64000000, P_PLL8,  2, 1,  3 },
	{  76800000, P_PLL8,  1, 1,  5 },
	{  96000000, P_PLL8,  2, 1,  2 },
	{ 109710000, P_PLL8,  1, 2,  7 },
	{ 128000000, P_PLL8,  1, 1,  3 },
	{ 153600000, P_PLL8,  1, 2,  5 },
	{ 200000000, P_PLL2,  2, 1,  2 },
	{ 228570000, P_PLL2,  1, 2,  7 },
	{ 266667000, P_PLL2,  1, 1,  3 },
	{ 320000000, P_PLL2,  1, 2,  5 },
	{ }
};

static struct clk_rcg vfe_src = {
	.ns_reg = 0x0108,
	.mn = {
		.mnctr_en_bit = 5,
		.mnctr_reset_bit = 7,
		.mnctr_mode_shift = 6,
		.n_val_shift = 16,
		.m_val_shift = 8,
		.width = 8,
	},
	.p = {
		.pre_div_shift = 10,
		.pre_div_width = 1,
	},
	.s = {
		.src_sel_shift = 0,
		.parent_map = mmcc_pxo_pll8_pll2_map,
	},
	.freq_tbl = clk_tbl_vfe,
	.clkr = {
		.enable_reg = 0x0104,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "vfe_src",
			.parent_names = mmcc_pxo_pll8_pll2,
			.num_parents = 3,
			.ops = &clk_rcg_ops,
		},
	},
};

static struct clk_branch vfe_clk = {
	.halt_reg = 0x01cc,
	.halt_bit = 6,
	.clkr = {
		.enable_reg = 0x0104,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "vfe_clk",
			.parent_names = (const char *[]){ "vfe_src" },
			.num_parents = 1,
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch vfe_csi_clk = {
	.halt_reg = 0x01cc,
	.halt_bit = 8,
	.clkr = {
		.enable_reg = 0x0104,
		.enable_mask = BIT(12),
		.hw.init = &(struct clk_init_data){
			.parent_names = (const char *[]){ "vfe_src" },
			.num_parents = 1,
			.name = "vfe_csi_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_SET_RATE_PARENT,
		},
	},
};

static struct clk_branch gmem_axi_clk = {
	.halt_reg = 0x01d8,
	.halt_bit = 6,
	.clkr = {
		.enable_reg = 0x0018,
		.enable_mask = BIT(24),
		.hw.init = &(struct clk_init_data){
			.name = "gmem_axi_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch ijpeg_axi_clk = {
	.hwcg_reg = 0x0018,
	.hwcg_bit = 11,
	.halt_reg = 0x01d8,
	.halt_bit = 4,
	.clkr = {
		.enable_reg = 0x0018,
		.enable_mask = BIT(21),
		.hw.init = &(struct clk_init_data){
			.name = "ijpeg_axi_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch mmss_imem_axi_clk = {
	.hwcg_reg = 0x0018,
	.hwcg_bit = 15,
	.halt_reg = 0x01d8,
	.halt_bit = 7,
	.clkr = {
		.enable_reg = 0x0018,
		.enable_mask = BIT(22),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_imem_axi_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch jpegd_axi_clk = {
	.halt_reg = 0x01d8,
	.halt_bit = 5,
	.clkr = {
		.enable_reg = 0x0018,
		.enable_mask = BIT(25),
		.hw.init = &(struct clk_init_data){
			.name = "jpegd_axi_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch vcodec_axi_b_clk = {
	.hwcg_reg = 0x0114,
	.hwcg_bit = 22,
	.halt_reg = 0x01e8,
	.halt_bit = 25,
	.clkr = {
		.enable_reg = 0x0114,
		.enable_mask = BIT(23),
		.hw.init = &(struct clk_init_data){
			.name = "vcodec_axi_b_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch vcodec_axi_a_clk = {
	.hwcg_reg = 0x0114,
	.hwcg_bit = 24,
	.halt_reg = 0x01e8,
	.halt_bit = 26,
	.clkr = {
		.enable_reg = 0x0114,
		.enable_mask = BIT(25),
		.hw.init = &(struct clk_init_data){
			.name = "vcodec_axi_a_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch vcodec_axi_clk = {
	.hwcg_reg = 0x0018,
	.hwcg_bit = 13,
	.halt_reg = 0x01d8,
	.halt_bit = 3,
	.clkr = {
		.enable_reg = 0x0018,
		.enable_mask = BIT(19),
		.hw.init = &(struct clk_init_data){
			.name = "vcodec_axi_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch vfe_axi_clk = {
	.halt_reg = 0x01d8,
	.halt_bit = 0,
	.clkr = {
		.enable_reg = 0x0018,
		.enable_mask = BIT(18),
		.hw.init = &(struct clk_init_data){
			.name = "vfe_axi_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch mdp_axi_clk = {
	.hwcg_reg = 0x0018,
	.hwcg_bit = 16,
	.halt_reg = 0x01d8,
	.halt_bit = 8,
	.clkr = {
		.enable_reg = 0x0018,
		.enable_mask = BIT(23),
		.hw.init = &(struct clk_init_data){
			.name = "mdp_axi_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch rot_axi_clk = {
	.hwcg_reg = 0x0020,
	.hwcg_bit = 25,
	.halt_reg = 0x01d8,
	.halt_bit = 2,
	.clkr = {
		.enable_reg = 0x0020,
		.enable_mask = BIT(24),
		.hw.init = &(struct clk_init_data){
			.name = "rot_axi_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch vcap_axi_clk = {
	.halt_reg = 0x0240,
	.halt_bit = 20,
	.hwcg_reg = 0x0244,
	.hwcg_bit = 11,
	.clkr = {
		.enable_reg = 0x0244,
		.enable_mask = BIT(12),
		.hw.init = &(struct clk_init_data){
			.name = "vcap_axi_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch vpe_axi_clk = {
	.hwcg_reg = 0x0020,
	.hwcg_bit = 27,
	.halt_reg = 0x01d8,
	.halt_bit = 1,
	.clkr = {
		.enable_reg = 0x0020,
		.enable_mask = BIT(26),
		.hw.init = &(struct clk_init_data){
			.name = "vpe_axi_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch gfx3d_axi_clk = {
	.hwcg_reg = 0x0244,
	.hwcg_bit = 24,
	.halt_reg = 0x0240,
	.halt_bit = 30,
	.clkr = {
		.enable_reg = 0x0244,
		.enable_mask = BIT(25),
		.hw.init = &(struct clk_init_data){
			.name = "gfx3d_axi_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch amp_ahb_clk = {
	.halt_reg = 0x01dc,
	.halt_bit = 18,
	.clkr = {
		.enable_reg = 0x0008,
		.enable_mask = BIT(24),
		.hw.init = &(struct clk_init_data){
			.name = "amp_ahb_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch csi_ahb_clk = {
	.halt_reg = 0x01dc,
	.halt_bit = 16,
	.clkr = {
		.enable_reg = 0x0008,
		.enable_mask = BIT(7),
		.hw.init = &(struct clk_init_data){
			.name = "csi_ahb_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT
		},
	},
};

static struct clk_branch dsi_m_ahb_clk = {
	.halt_reg = 0x01dc,
	.halt_bit = 19,
	.clkr = {
		.enable_reg = 0x0008,
		.enable_mask = BIT(9),
		.hw.init = &(struct clk_init_data){
			.name = "dsi_m_ahb_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch dsi_s_ahb_clk = {
	.hwcg_reg = 0x0038,
	.hwcg_bit = 20,
	.halt_reg = 0x01dc,
	.halt_bit = 21,
	.clkr = {
		.enable_reg = 0x0008,
		.enable_mask = BIT(18),
		.hw.init = &(struct clk_init_data){
			.name = "dsi_s_ahb_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch dsi2_m_ahb_clk = {
	.halt_reg = 0x01d8,
	.halt_bit = 18,
	.clkr = {
		.enable_reg = 0x0008,
		.enable_mask = BIT(17),
		.hw.init = &(struct clk_init_data){
			.name = "dsi2_m_ahb_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT
		},
	},
};

static struct clk_branch dsi2_s_ahb_clk = {
	.hwcg_reg = 0x0038,
	.hwcg_bit = 15,
	.halt_reg = 0x01dc,
	.halt_bit = 20,
	.clkr = {
		.enable_reg = 0x0008,
		.enable_mask = BIT(22),
		.hw.init = &(struct clk_init_data){
			.name = "dsi2_s_ahb_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch gfx2d0_ahb_clk = {
	.hwcg_reg = 0x0038,
	.hwcg_bit = 28,
	.halt_reg = 0x01dc,
	.halt_bit = 2,
	.clkr = {
		.enable_reg = 0x0008,
		.enable_mask = BIT(19),
		.hw.init = &(struct clk_init_data){
			.name = "gfx2d0_ahb_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch gfx2d1_ahb_clk = {
	.hwcg_reg = 0x0038,
	.hwcg_bit = 29,
	.halt_reg = 0x01dc,
	.halt_bit = 3,
	.clkr = {
		.enable_reg = 0x0008,
		.enable_mask = BIT(2),
		.hw.init = &(struct clk_init_data){
			.name = "gfx2d1_ahb_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch gfx3d_ahb_clk = {
	.hwcg_reg = 0x0038,
	.hwcg_bit = 27,
	.halt_reg = 0x01dc,
	.halt_bit = 4,
	.clkr = {
		.enable_reg = 0x0008,
		.enable_mask = BIT(3),
		.hw.init = &(struct clk_init_data){
			.name = "gfx3d_ahb_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch hdmi_m_ahb_clk = {
	.hwcg_reg = 0x0038,
	.hwcg_bit = 21,
	.halt_reg = 0x01dc,
	.halt_bit = 5,
	.clkr = {
		.enable_reg = 0x0008,
		.enable_mask = BIT(14),
		.hw.init = &(struct clk_init_data){
			.name = "hdmi_m_ahb_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch hdmi_s_ahb_clk = {
	.hwcg_reg = 0x0038,
	.hwcg_bit = 22,
	.halt_reg = 0x01dc,
	.halt_bit = 6,
	.clkr = {
		.enable_reg = 0x0008,
		.enable_mask = BIT(4),
		.hw.init = &(struct clk_init_data){
			.name = "hdmi_s_ahb_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch ijpeg_ahb_clk = {
	.halt_reg = 0x01dc,
	.halt_bit = 9,
	.clkr = {
		.enable_reg = 0x0008,
		.enable_mask = BIT(5),
		.hw.init = &(struct clk_init_data){
			.name = "ijpeg_ahb_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT
		},
	},
};

static struct clk_branch mmss_imem_ahb_clk = {
	.hwcg_reg = 0x0038,
	.hwcg_bit = 12,
	.halt_reg = 0x01dc,
	.halt_bit = 10,
	.clkr = {
		.enable_reg = 0x0008,
		.enable_mask = BIT(6),
		.hw.init = &(struct clk_init_data){
			.name = "mmss_imem_ahb_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT
		},
	},
};

static struct clk_branch jpegd_ahb_clk = {
	.halt_reg = 0x01dc,
	.halt_bit = 7,
	.clkr = {
		.enable_reg = 0x0008,
		.enable_mask = BIT(21),
		.hw.init = &(struct clk_init_data){
			.name = "jpegd_ahb_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch mdp_ahb_clk = {
	.halt_reg = 0x01dc,
	.halt_bit = 11,
	.clkr = {
		.enable_reg = 0x0008,
		.enable_mask = BIT(10),
		.hw.init = &(struct clk_init_data){
			.name = "mdp_ahb_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch rot_ahb_clk = {
	.halt_reg = 0x01dc,
	.halt_bit = 13,
	.clkr = {
		.enable_reg = 0x0008,
		.enable_mask = BIT(12),
		.hw.init = &(struct clk_init_data){
			.name = "rot_ahb_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT
		},
	},
};

static struct clk_branch smmu_ahb_clk = {
	.hwcg_reg = 0x0008,
	.hwcg_bit = 26,
	.halt_reg = 0x01dc,
	.halt_bit = 22,
	.clkr = {
		.enable_reg = 0x0008,
		.enable_mask = BIT(15),
		.hw.init = &(struct clk_init_data){
			.name = "smmu_ahb_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch tv_enc_ahb_clk = {
	.halt_reg = 0x01dc,
	.halt_bit = 23,
	.clkr = {
		.enable_reg = 0x0008,
		.enable_mask = BIT(25),
		.hw.init = &(struct clk_init_data){
			.name = "tv_enc_ahb_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch vcap_ahb_clk = {
	.halt_reg = 0x0240,
	.halt_bit = 23,
	.clkr = {
		.enable_reg = 0x0248,
		.enable_mask = BIT(1),
		.hw.init = &(struct clk_init_data){
			.name = "vcap_ahb_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch vcodec_ahb_clk = {
	.hwcg_reg = 0x0038,
	.hwcg_bit = 26,
	.halt_reg = 0x01dc,
	.halt_bit = 12,
	.clkr = {
		.enable_reg = 0x0008,
		.enable_mask = BIT(11),
		.hw.init = &(struct clk_init_data){
			.name = "vcodec_ahb_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch vfe_ahb_clk = {
	.halt_reg = 0x01dc,
	.halt_bit = 14,
	.clkr = {
		.enable_reg = 0x0008,
		.enable_mask = BIT(13),
		.hw.init = &(struct clk_init_data){
			.name = "vfe_ahb_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_branch vpe_ahb_clk = {
	.halt_reg = 0x01dc,
	.halt_bit = 15,
	.clkr = {
		.enable_reg = 0x0008,
		.enable_mask = BIT(16),
		.hw.init = &(struct clk_init_data){
			.name = "vpe_ahb_clk",
			.ops = &clk_branch_ops,
			.flags = CLK_IS_ROOT,
		},
	},
};

static struct clk_regmap *mmcc_msm8960_clks[] = {
	[TV_ENC_AHB_CLK] = &tv_enc_ahb_clk.clkr,
	[AMP_AHB_CLK] = &amp_ahb_clk.clkr,
	[DSI2_S_AHB_CLK] = &dsi2_s_ahb_clk.clkr,
	[JPEGD_AHB_CLK] = &jpegd_ahb_clk.clkr,
	[GFX2D0_AHB_CLK] = &gfx2d0_ahb_clk.clkr,
	[DSI_S_AHB_CLK] = &dsi_s_ahb_clk.clkr,
	[DSI2_M_AHB_CLK] = &dsi2_m_ahb_clk.clkr,
	[VPE_AHB_CLK] = &vpe_ahb_clk.clkr,
	[SMMU_AHB_CLK] = &smmu_ahb_clk.clkr,
	[HDMI_M_AHB_CLK] = &hdmi_m_ahb_clk.clkr,
	[VFE_AHB_CLK] = &vfe_ahb_clk.clkr,
	[ROT_AHB_CLK] = &rot_ahb_clk.clkr,
	[VCODEC_AHB_CLK] = &vcodec_ahb_clk.clkr,
	[MDP_AHB_CLK] = &mdp_ahb_clk.clkr,
	[DSI_M_AHB_CLK] = &dsi_m_ahb_clk.clkr,
	[CSI_AHB_CLK] = &csi_ahb_clk.clkr,
	[MMSS_IMEM_AHB_CLK] = &mmss_imem_ahb_clk.clkr,
	[IJPEG_AHB_CLK] = &ijpeg_ahb_clk.clkr,
	[HDMI_S_AHB_CLK] = &hdmi_s_ahb_clk.clkr,
	[GFX3D_AHB_CLK] = &gfx3d_ahb_clk.clkr,
	[GFX2D1_AHB_CLK] = &gfx2d1_ahb_clk.clkr,
	[JPEGD_AXI_CLK] = &jpegd_axi_clk.clkr,
	[GMEM_AXI_CLK] = &gmem_axi_clk.clkr,
	[MDP_AXI_CLK] = &mdp_axi_clk.clkr,
	[MMSS_IMEM_AXI_CLK] = &mmss_imem_axi_clk.clkr,
	[IJPEG_AXI_CLK] = &ijpeg_axi_clk.clkr,
	[GFX3D_AXI_CLK] = &gfx3d_axi_clk.clkr,
	[VCODEC_AXI_CLK] = &vcodec_axi_clk.clkr,
	[VFE_AXI_CLK] = &vfe_axi_clk.clkr,
	[VPE_AXI_CLK] = &vpe_axi_clk.clkr,
	[ROT_AXI_CLK] = &rot_axi_clk.clkr,
	[VCODEC_AXI_A_CLK] = &vcodec_axi_a_clk.clkr,
	[VCODEC_AXI_B_CLK] = &vcodec_axi_b_clk.clkr,
	[CSI0_SRC] = &csi0_src.clkr,
	[CSI0_CLK] = &csi0_clk.clkr,
	[CSI0_PHY_CLK] = &csi0_phy_clk.clkr,
	[CSI1_SRC] = &csi1_src.clkr,
	[CSI1_CLK] = &csi1_clk.clkr,
	[CSI1_PHY_CLK] = &csi1_phy_clk.clkr,
	[CSI2_SRC] = &csi2_src.clkr,
	[CSI2_CLK] = &csi2_clk.clkr,
	[CSI2_PHY_CLK] = &csi2_phy_clk.clkr,
	[CSI_PIX_CLK] = &csi_pix_clk.clkr,
	[CSI_RDI_CLK] = &csi_rdi_clk.clkr,
	[MDP_VSYNC_CLK] = &mdp_vsync_clk.clkr,
	[HDMI_APP_CLK] = &hdmi_app_clk.clkr,
	[CSI_PIX1_CLK] = &csi_pix1_clk.clkr,
	[CSI_RDI2_CLK] = &csi_rdi2_clk.clkr,
	[CSI_RDI1_CLK] = &csi_rdi1_clk.clkr,
	[GFX2D0_SRC] = &gfx2d0_src.clkr,
	[GFX2D0_CLK] = &gfx2d0_clk.clkr,
	[GFX2D1_SRC] = &gfx2d1_src.clkr,
	[GFX2D1_CLK] = &gfx2d1_clk.clkr,
	[GFX3D_SRC] = &gfx3d_src.clkr,
	[GFX3D_CLK] = &gfx3d_clk.clkr,
	[IJPEG_SRC] = &ijpeg_src.clkr,
	[IJPEG_CLK] = &ijpeg_clk.clkr,
	[JPEGD_SRC] = &jpegd_src.clkr,
	[JPEGD_CLK] = &jpegd_clk.clkr,
	[MDP_SRC] = &mdp_src.clkr,
	[MDP_CLK] = &mdp_clk.clkr,
	[MDP_LUT_CLK] = &mdp_lut_clk.clkr,
	[ROT_SRC] = &rot_src.clkr,
	[ROT_CLK] = &rot_clk.clkr,
	[TV_ENC_CLK] = &tv_enc_clk.clkr,
	[TV_DAC_CLK] = &tv_dac_clk.clkr,
	[HDMI_TV_CLK] = &hdmi_tv_clk.clkr,
	[MDP_TV_CLK] = &mdp_tv_clk.clkr,
	[TV_SRC] = &tv_src.clkr,
	[VCODEC_SRC] = &vcodec_src.clkr,
	[VCODEC_CLK] = &vcodec_clk.clkr,
	[VFE_SRC] = &vfe_src.clkr,
	[VFE_CLK] = &vfe_clk.clkr,
	[VFE_CSI_CLK] = &vfe_csi_clk.clkr,
	[VPE_SRC] = &vpe_src.clkr,
	[VPE_CLK] = &vpe_clk.clkr,
	[CAMCLK0_SRC] = &camclk0_src.clkr,
	[CAMCLK0_CLK] = &camclk0_clk.clkr,
	[CAMCLK1_SRC] = &camclk1_src.clkr,
	[CAMCLK1_CLK] = &camclk1_clk.clkr,
	[CAMCLK2_SRC] = &camclk2_src.clkr,
	[CAMCLK2_CLK] = &camclk2_clk.clkr,
	[CSIPHYTIMER_SRC] = &csiphytimer_src.clkr,
	[CSIPHY2_TIMER_CLK] = &csiphy2_timer_clk.clkr,
	[CSIPHY1_TIMER_CLK] = &csiphy1_timer_clk.clkr,
	[CSIPHY0_TIMER_CLK] = &csiphy0_timer_clk.clkr,
	[PLL2] = &pll2.clkr,
};

static const struct qcom_reset_map mmcc_msm8960_resets[] = {
	[VPE_AXI_RESET] = { 0x0208, 15 },
	[IJPEG_AXI_RESET] = { 0x0208, 14 },
	[MPD_AXI_RESET] = { 0x0208, 13 },
	[VFE_AXI_RESET] = { 0x0208, 9 },
	[SP_AXI_RESET] = { 0x0208, 8 },
	[VCODEC_AXI_RESET] = { 0x0208, 7 },
	[ROT_AXI_RESET] = { 0x0208, 6 },
	[VCODEC_AXI_A_RESET] = { 0x0208, 5 },
	[VCODEC_AXI_B_RESET] = { 0x0208, 4 },
	[FAB_S3_AXI_RESET] = { 0x0208, 3 },
	[FAB_S2_AXI_RESET] = { 0x0208, 2 },
	[FAB_S1_AXI_RESET] = { 0x0208, 1 },
	[FAB_S0_AXI_RESET] = { 0x0208 },
	[SMMU_GFX3D_ABH_RESET] = { 0x020c, 31 },
	[SMMU_VPE_AHB_RESET] = { 0x020c, 30 },
	[SMMU_VFE_AHB_RESET] = { 0x020c, 29 },
	[SMMU_ROT_AHB_RESET] = { 0x020c, 28 },
	[SMMU_VCODEC_B_AHB_RESET] = { 0x020c, 27 },
	[SMMU_VCODEC_A_AHB_RESET] = { 0x020c, 26 },
	[SMMU_MDP1_AHB_RESET] = { 0x020c, 25 },
	[SMMU_MDP0_AHB_RESET] = { 0x020c, 24 },
	[SMMU_JPEGD_AHB_RESET] = { 0x020c, 23 },
	[SMMU_IJPEG_AHB_RESET] = { 0x020c, 22 },
	[SMMU_GFX2D0_AHB_RESET] = { 0x020c, 21 },
	[SMMU_GFX2D1_AHB_RESET] = { 0x020c, 20 },
	[APU_AHB_RESET] = { 0x020c, 18 },
	[CSI_AHB_RESET] = { 0x020c, 17 },
	[TV_ENC_AHB_RESET] = { 0x020c, 15 },
	[VPE_AHB_RESET] = { 0x020c, 14 },
	[FABRIC_AHB_RESET] = { 0x020c, 13 },
	[GFX2D0_AHB_RESET] = { 0x020c, 12 },
	[GFX2D1_AHB_RESET] = { 0x020c, 11 },
	[GFX3D_AHB_RESET] = { 0x020c, 10 },
	[HDMI_AHB_RESET] = { 0x020c, 9 },
	[MSSS_IMEM_AHB_RESET] = { 0x020c, 8 },
	[IJPEG_AHB_RESET] = { 0x020c, 7 },
	[DSI_M_AHB_RESET] = { 0x020c, 6 },
	[DSI_S_AHB_RESET] = { 0x020c, 5 },
	[JPEGD_AHB_RESET] = { 0x020c, 4 },
	[MDP_AHB_RESET] = { 0x020c, 3 },
	[ROT_AHB_RESET] = { 0x020c, 2 },
	[VCODEC_AHB_RESET] = { 0x020c, 1 },
	[VFE_AHB_RESET] = { 0x020c, 0 },
	[DSI2_M_AHB_RESET] = { 0x0210, 31 },
	[DSI2_S_AHB_RESET] = { 0x0210, 30 },
	[CSIPHY2_RESET] = { 0x0210, 29 },
	[CSI_PIX1_RESET] = { 0x0210, 28 },
	[CSIPHY0_RESET] = { 0x0210, 27 },
	[CSIPHY1_RESET] = { 0x0210, 26 },
	[DSI2_RESET] = { 0x0210, 25 },
	[VFE_CSI_RESET] = { 0x0210, 24 },
	[MDP_RESET] = { 0x0210, 21 },
	[AMP_RESET] = { 0x0210, 20 },
	[JPEGD_RESET] = { 0x0210, 19 },
	[CSI1_RESET] = { 0x0210, 18 },
	[VPE_RESET] = { 0x0210, 17 },
	[MMSS_FABRIC_RESET] = { 0x0210, 16 },
	[VFE_RESET] = { 0x0210, 15 },
	[GFX2D0_RESET] = { 0x0210, 14 },
	[GFX2D1_RESET] = { 0x0210, 13 },
	[GFX3D_RESET] = { 0x0210, 12 },
	[HDMI_RESET] = { 0x0210, 11 },
	[MMSS_IMEM_RESET] = { 0x0210, 10 },
	[IJPEG_RESET] = { 0x0210, 9 },
	[CSI0_RESET] = { 0x0210, 8 },
	[DSI_RESET] = { 0x0210, 7 },
	[VCODEC_RESET] = { 0x0210, 6 },
	[MDP_TV_RESET] = { 0x0210, 4 },
	[MDP_VSYNC_RESET] = { 0x0210, 3 },
	[ROT_RESET] = { 0x0210, 2 },
	[TV_HDMI_RESET] = { 0x0210, 1 },
	[TV_ENC_RESET] = { 0x0210 },
	[CSI2_RESET] = { 0x0214, 2 },
	[CSI_RDI1_RESET] = { 0x0214, 1 },
	[CSI_RDI2_RESET] = { 0x0214 },
};

static struct clk_regmap *mmcc_apq8064_clks[] = {
	[AMP_AHB_CLK] = &amp_ahb_clk.clkr,
	[DSI2_S_AHB_CLK] = &dsi2_s_ahb_clk.clkr,
	[JPEGD_AHB_CLK] = &jpegd_ahb_clk.clkr,
	[DSI_S_AHB_CLK] = &dsi_s_ahb_clk.clkr,
	[DSI2_M_AHB_CLK] = &dsi2_m_ahb_clk.clkr,
	[VPE_AHB_CLK] = &vpe_ahb_clk.clkr,
	[SMMU_AHB_CLK] = &smmu_ahb_clk.clkr,
	[HDMI_M_AHB_CLK] = &hdmi_m_ahb_clk.clkr,
	[VFE_AHB_CLK] = &vfe_ahb_clk.clkr,
	[ROT_AHB_CLK] = &rot_ahb_clk.clkr,
	[VCODEC_AHB_CLK] = &vcodec_ahb_clk.clkr,
	[MDP_AHB_CLK] = &mdp_ahb_clk.clkr,
	[DSI_M_AHB_CLK] = &dsi_m_ahb_clk.clkr,
	[CSI_AHB_CLK] = &csi_ahb_clk.clkr,
	[MMSS_IMEM_AHB_CLK] = &mmss_imem_ahb_clk.clkr,
	[IJPEG_AHB_CLK] = &ijpeg_ahb_clk.clkr,
	[HDMI_S_AHB_CLK] = &hdmi_s_ahb_clk.clkr,
	[GFX3D_AHB_CLK] = &gfx3d_ahb_clk.clkr,
	[JPEGD_AXI_CLK] = &jpegd_axi_clk.clkr,
	[GMEM_AXI_CLK] = &gmem_axi_clk.clkr,
	[MDP_AXI_CLK] = &mdp_axi_clk.clkr,
	[MMSS_IMEM_AXI_CLK] = &mmss_imem_axi_clk.clkr,
	[IJPEG_AXI_CLK] = &ijpeg_axi_clk.clkr,
	[GFX3D_AXI_CLK] = &gfx3d_axi_clk.clkr,
	[VCODEC_AXI_CLK] = &vcodec_axi_clk.clkr,
	[VFE_AXI_CLK] = &vfe_axi_clk.clkr,
	[VPE_AXI_CLK] = &vpe_axi_clk.clkr,
	[ROT_AXI_CLK] = &rot_axi_clk.clkr,
	[VCODEC_AXI_A_CLK] = &vcodec_axi_a_clk.clkr,
	[VCODEC_AXI_B_CLK] = &vcodec_axi_b_clk.clkr,
	[CSI0_SRC] = &csi0_src.clkr,
	[CSI0_CLK] = &csi0_clk.clkr,
	[CSI0_PHY_CLK] = &csi0_phy_clk.clkr,
	[CSI1_SRC] = &csi1_src.clkr,
	[CSI1_CLK] = &csi1_clk.clkr,
	[CSI1_PHY_CLK] = &csi1_phy_clk.clkr,
	[CSI2_SRC] = &csi2_src.clkr,
	[CSI2_CLK] = &csi2_clk.clkr,
	[CSI2_PHY_CLK] = &csi2_phy_clk.clkr,
	[CSI_PIX_CLK] = &csi_pix_clk.clkr,
	[CSI_RDI_CLK] = &csi_rdi_clk.clkr,
	[MDP_VSYNC_CLK] = &mdp_vsync_clk.clkr,
	[HDMI_APP_CLK] = &hdmi_app_clk.clkr,
	[CSI_PIX1_CLK] = &csi_pix1_clk.clkr,
	[CSI_RDI2_CLK] = &csi_rdi2_clk.clkr,
	[CSI_RDI1_CLK] = &csi_rdi1_clk.clkr,
	[GFX3D_SRC] = &gfx3d_src.clkr,
	[GFX3D_CLK] = &gfx3d_clk.clkr,
	[IJPEG_SRC] = &ijpeg_src.clkr,
	[IJPEG_CLK] = &ijpeg_clk.clkr,
	[JPEGD_SRC] = &jpegd_src.clkr,
	[JPEGD_CLK] = &jpegd_clk.clkr,
	[MDP_SRC] = &mdp_src.clkr,
	[MDP_CLK] = &mdp_clk.clkr,
	[MDP_LUT_CLK] = &mdp_lut_clk.clkr,
	[ROT_SRC] = &rot_src.clkr,
	[ROT_CLK] = &rot_clk.clkr,
	[TV_DAC_CLK] = &tv_dac_clk.clkr,
	[HDMI_TV_CLK] = &hdmi_tv_clk.clkr,
	[MDP_TV_CLK] = &mdp_tv_clk.clkr,
	[TV_SRC] = &tv_src.clkr,
	[VCODEC_SRC] = &vcodec_src.clkr,
	[VCODEC_CLK] = &vcodec_clk.clkr,
	[VFE_SRC] = &vfe_src.clkr,
	[VFE_CLK] = &vfe_clk.clkr,
	[VFE_CSI_CLK] = &vfe_csi_clk.clkr,
	[VPE_SRC] = &vpe_src.clkr,
	[VPE_CLK] = &vpe_clk.clkr,
	[CAMCLK0_SRC] = &camclk0_src.clkr,
	[CAMCLK0_CLK] = &camclk0_clk.clkr,
	[CAMCLK1_SRC] = &camclk1_src.clkr,
	[CAMCLK1_CLK] = &camclk1_clk.clkr,
	[CAMCLK2_SRC] = &camclk2_src.clkr,
	[CAMCLK2_CLK] = &camclk2_clk.clkr,
	[CSIPHYTIMER_SRC] = &csiphytimer_src.clkr,
	[CSIPHY2_TIMER_CLK] = &csiphy2_timer_clk.clkr,
	[CSIPHY1_TIMER_CLK] = &csiphy1_timer_clk.clkr,
	[CSIPHY0_TIMER_CLK] = &csiphy0_timer_clk.clkr,
	[PLL2] = &pll2.clkr,
	[RGB_TV_CLK] = &rgb_tv_clk.clkr,
	[NPL_TV_CLK] = &npl_tv_clk.clkr,
	[VCAP_AHB_CLK] = &vcap_ahb_clk.clkr,
	[VCAP_AXI_CLK] = &vcap_axi_clk.clkr,
	[VCAP_SRC] = &vcap_src.clkr,
	[VCAP_CLK] = &vcap_clk.clkr,
	[VCAP_NPL_CLK] = &vcap_npl_clk.clkr,
	[PLL15] = &pll15.clkr,
};

static const struct qcom_reset_map mmcc_apq8064_resets[] = {
	[GFX3D_AXI_RESET] = { 0x0208, 17 },
	[VCAP_AXI_RESET] = { 0x0208, 16 },
	[VPE_AXI_RESET] = { 0x0208, 15 },
	[IJPEG_AXI_RESET] = { 0x0208, 14 },
	[MPD_AXI_RESET] = { 0x0208, 13 },
	[VFE_AXI_RESET] = { 0x0208, 9 },
	[SP_AXI_RESET] = { 0x0208, 8 },
	[VCODEC_AXI_RESET] = { 0x0208, 7 },
	[ROT_AXI_RESET] = { 0x0208, 6 },
	[VCODEC_AXI_A_RESET] = { 0x0208, 5 },
	[VCODEC_AXI_B_RESET] = { 0x0208, 4 },
	[FAB_S3_AXI_RESET] = { 0x0208, 3 },
	[FAB_S2_AXI_RESET] = { 0x0208, 2 },
	[FAB_S1_AXI_RESET] = { 0x0208, 1 },
	[FAB_S0_AXI_RESET] = { 0x0208 },
	[SMMU_GFX3D_ABH_RESET] = { 0x020c, 31 },
	[SMMU_VPE_AHB_RESET] = { 0x020c, 30 },
	[SMMU_VFE_AHB_RESET] = { 0x020c, 29 },
	[SMMU_ROT_AHB_RESET] = { 0x020c, 28 },
	[SMMU_VCODEC_B_AHB_RESET] = { 0x020c, 27 },
	[SMMU_VCODEC_A_AHB_RESET] = { 0x020c, 26 },
	[SMMU_MDP1_AHB_RESET] = { 0x020c, 25 },
	[SMMU_MDP0_AHB_RESET] = { 0x020c, 24 },
	[SMMU_JPEGD_AHB_RESET] = { 0x020c, 23 },
	[SMMU_IJPEG_AHB_RESET] = { 0x020c, 22 },
	[APU_AHB_RESET] = { 0x020c, 18 },
	[CSI_AHB_RESET] = { 0x020c, 17 },
	[TV_ENC_AHB_RESET] = { 0x020c, 15 },
	[VPE_AHB_RESET] = { 0x020c, 14 },
	[FABRIC_AHB_RESET] = { 0x020c, 13 },
	[GFX3D_AHB_RESET] = { 0x020c, 10 },
	[HDMI_AHB_RESET] = { 0x020c, 9 },
	[MSSS_IMEM_AHB_RESET] = { 0x020c, 8 },
	[IJPEG_AHB_RESET] = { 0x020c, 7 },
	[DSI_M_AHB_RESET] = { 0x020c, 6 },
	[DSI_S_AHB_RESET] = { 0x020c, 5 },
	[JPEGD_AHB_RESET] = { 0x020c, 4 },
	[MDP_AHB_RESET] = { 0x020c, 3 },
	[ROT_AHB_RESET] = { 0x020c, 2 },
	[VCODEC_AHB_RESET] = { 0x020c, 1 },
	[VFE_AHB_RESET] = { 0x020c, 0 },
	[SMMU_VCAP_AHB_RESET] = { 0x0200, 3 },
	[VCAP_AHB_RESET] = { 0x0200, 2 },
	[DSI2_M_AHB_RESET] = { 0x0200, 1 },
	[DSI2_S_AHB_RESET] = { 0x0200, 0 },
	[CSIPHY2_RESET] = { 0x0210, 31 },
	[CSI_PIX1_RESET] = { 0x0210, 30 },
	[CSIPHY0_RESET] = { 0x0210, 29 },
	[CSIPHY1_RESET] = { 0x0210, 28 },
	[CSI_RDI_RESET] = { 0x0210, 27 },
	[CSI_PIX_RESET] = { 0x0210, 26 },
	[DSI2_RESET] = { 0x0210, 25 },
	[VFE_CSI_RESET] = { 0x0210, 24 },
	[MDP_RESET] = { 0x0210, 21 },
	[AMP_RESET] = { 0x0210, 20 },
	[JPEGD_RESET] = { 0x0210, 19 },
	[CSI1_RESET] = { 0x0210, 18 },
	[VPE_RESET] = { 0x0210, 17 },
	[MMSS_FABRIC_RESET] = { 0x0210, 16 },
	[VFE_RESET] = { 0x0210, 15 },
	[GFX3D_RESET] = { 0x0210, 12 },
	[HDMI_RESET] = { 0x0210, 11 },
	[MMSS_IMEM_RESET] = { 0x0210, 10 },
	[IJPEG_RESET] = { 0x0210, 9 },
	[CSI0_RESET] = { 0x0210, 8 },
	[DSI_RESET] = { 0x0210, 7 },
	[VCODEC_RESET] = { 0x0210, 6 },
	[MDP_TV_RESET] = { 0x0210, 4 },
	[MDP_VSYNC_RESET] = { 0x0210, 3 },
	[ROT_RESET] = { 0x0210, 2 },
	[TV_HDMI_RESET] = { 0x0210, 1 },
	[VCAP_NPL_RESET] = { 0x0214, 4 },
	[VCAP_RESET] = { 0x0214, 3 },
	[CSI2_RESET] = { 0x0214, 2 },
	[CSI_RDI1_RESET] = { 0x0214, 1 },
	[CSI_RDI2_RESET] = { 0x0214 },
};

static const struct regmap_config mmcc_msm8960_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x334,
	.fast_io	= true,
};

static const struct regmap_config mmcc_apq8064_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x350,
	.fast_io	= true,
};

static const struct qcom_cc_desc mmcc_msm8960_desc = {
	.config = &mmcc_msm8960_regmap_config,
	.clks = mmcc_msm8960_clks,
	.num_clks = ARRAY_SIZE(mmcc_msm8960_clks),
	.resets = mmcc_msm8960_resets,
	.num_resets = ARRAY_SIZE(mmcc_msm8960_resets),
};

static const struct qcom_cc_desc mmcc_apq8064_desc = {
	.config = &mmcc_apq8064_regmap_config,
	.clks = mmcc_apq8064_clks,
	.num_clks = ARRAY_SIZE(mmcc_apq8064_clks),
	.resets = mmcc_apq8064_resets,
	.num_resets = ARRAY_SIZE(mmcc_apq8064_resets),
};

static const struct of_device_id mmcc_msm8960_match_table[] = {
	{ .compatible = "qcom,mmcc-msm8960", .data = &mmcc_msm8960_desc },
	{ .compatible = "qcom,mmcc-apq8064", .data = &mmcc_apq8064_desc },
	{ }
};
MODULE_DEVICE_TABLE(of, mmcc_msm8960_match_table);

static int mmcc_msm8960_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct regmap *regmap;
	bool is_8064;
	struct device *dev = &pdev->dev;

	match = of_match_device(mmcc_msm8960_match_table, dev);
	if (!match)
		return -EINVAL;

	is_8064 = of_device_is_compatible(dev->of_node, "qcom,mmcc-apq8064");
	if (is_8064) {
		gfx3d_src.freq_tbl = clk_tbl_gfx3d_8064;
		gfx3d_src.clkr.hw.init = &gfx3d_8064_init;
		gfx3d_src.s[0].parent_map = mmcc_pxo_pll8_pll2_pll15_map;
		gfx3d_src.s[1].parent_map = mmcc_pxo_pll8_pll2_pll15_map;
	}

	regmap = qcom_cc_map(pdev, match->data);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	clk_pll_configure_sr(&pll15, regmap, &pll15_config, false);

	return qcom_cc_really_probe(pdev, match->data, regmap);
}

static int mmcc_msm8960_remove(struct platform_device *pdev)
{
	qcom_cc_remove(pdev);
	return 0;
}

static struct platform_driver mmcc_msm8960_driver = {
	.probe		= mmcc_msm8960_probe,
	.remove		= mmcc_msm8960_remove,
	.driver		= {
		.name	= "mmcc-msm8960",
		.owner	= THIS_MODULE,
		.of_match_table = mmcc_msm8960_match_table,
	},
};

module_platform_driver(mmcc_msm8960_driver);

MODULE_DESCRIPTION("QCOM MMCC MSM8960 Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:mmcc-msm8960");
