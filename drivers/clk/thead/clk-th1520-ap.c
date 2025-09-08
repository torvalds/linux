// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Jisheng Zhang <jszhang@kernel.org>
 * Copyright (C) 2023 Vivo Communication Technology Co. Ltd.
 *  Authors: Yangtao Li <frank.li@vivo.com>
 */

#include <dt-bindings/clock/thead,th1520-clk-ap.h>
#include <linux/bitfield.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define TH1520_PLL_POSTDIV2	GENMASK(26, 24)
#define TH1520_PLL_POSTDIV1	GENMASK(22, 20)
#define TH1520_PLL_FBDIV	GENMASK(19, 8)
#define TH1520_PLL_REFDIV	GENMASK(5, 0)
#define TH1520_PLL_BYPASS	BIT(30)
#define TH1520_PLL_DSMPD	BIT(24)
#define TH1520_PLL_FRAC		GENMASK(23, 0)
#define TH1520_PLL_FRAC_BITS    24

struct ccu_internal {
	u8	shift;
	u8	width;
};

struct ccu_div_internal {
	u8	shift;
	u8	width;
	u32	flags;
};

struct ccu_common {
	int		clkid;
	struct regmap	*map;
	u16		cfg0;
	u16		cfg1;
	struct clk_hw	hw;
};

struct ccu_mux {
	int			clkid;
	u32			reg;
	struct clk_mux		mux;
};

struct ccu_gate {
	u32			enable;
	struct ccu_common	common;
};

struct ccu_div {
	u32			enable;
	struct ccu_div_internal	div;
	struct ccu_internal	mux;
	struct ccu_common	common;
};

struct ccu_pll {
	struct ccu_common	common;
};

#define TH_CCU_ARG(_shift, _width)					\
	{								\
		.shift	= _shift,					\
		.width	= _width,					\
	}

#define TH_CCU_DIV_FLAGS(_shift, _width, _flags)			\
	{								\
		.shift	= _shift,					\
		.width	= _width,					\
		.flags	= _flags,					\
	}

#define TH_CCU_MUX(_name, _parents, _shift, _width)			\
	{								\
		.mask		= GENMASK(_width - 1, 0),		\
		.shift		= _shift,				\
		.hw.init	= CLK_HW_INIT_PARENTS_DATA(		\
					_name,				\
					_parents,			\
					&clk_mux_ops,			\
					0),				\
	}

#define CCU_GATE(_clkid, _struct, _name, _parent, _reg, _gate, _flags)	\
	struct ccu_gate _struct = {					\
		.enable	= _gate,					\
		.common	= {						\
			.clkid		= _clkid,			\
			.cfg0		= _reg,				\
			.hw.init	= CLK_HW_INIT_PARENTS_DATA(	\
						_name,			\
						_parent,		\
						&clk_gate_ops,		\
						_flags),		\
		}							\
	}

static inline struct ccu_common *hw_to_ccu_common(struct clk_hw *hw)
{
	return container_of(hw, struct ccu_common, hw);
}

static inline struct ccu_pll *hw_to_ccu_pll(struct clk_hw *hw)
{
	struct ccu_common *common = hw_to_ccu_common(hw);

	return container_of(common, struct ccu_pll, common);
}

static inline struct ccu_div *hw_to_ccu_div(struct clk_hw *hw)
{
	struct ccu_common *common = hw_to_ccu_common(hw);

	return container_of(common, struct ccu_div, common);
}

static inline struct ccu_gate *hw_to_ccu_gate(struct clk_hw *hw)
{
	struct ccu_common *common = hw_to_ccu_common(hw);

	return container_of(common, struct ccu_gate, common);
}

static u8 ccu_get_parent_helper(struct ccu_common *common,
				struct ccu_internal *mux)
{
	unsigned int val;
	u8 parent;

	regmap_read(common->map, common->cfg0, &val);
	parent = val >> mux->shift;
	parent &= GENMASK(mux->width - 1, 0);

	return parent;
}

static int ccu_set_parent_helper(struct ccu_common *common,
				 struct ccu_internal *mux,
				 u8 index)
{
	return regmap_update_bits(common->map, common->cfg0,
			GENMASK(mux->width - 1, 0) << mux->shift,
			index << mux->shift);
}

static void ccu_disable_helper(struct ccu_common *common, u32 gate)
{
	if (!gate)
		return;
	regmap_update_bits(common->map, common->cfg0,
			   gate, ~gate);
}

static int ccu_enable_helper(struct ccu_common *common, u32 gate)
{
	unsigned int val;
	int ret;

	if (!gate)
		return 0;

	ret = regmap_update_bits(common->map, common->cfg0, gate, gate);
	regmap_read(common->map, common->cfg0, &val);
	return ret;
}

static int ccu_is_enabled_helper(struct ccu_common *common, u32 gate)
{
	unsigned int val;

	if (!gate)
		return true;

	regmap_read(common->map, common->cfg0, &val);
	return val & gate;
}

static unsigned long ccu_div_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct ccu_div *cd = hw_to_ccu_div(hw);
	unsigned long rate;
	unsigned int val;

	regmap_read(cd->common.map, cd->common.cfg0, &val);
	val = val >> cd->div.shift;
	val &= GENMASK(cd->div.width - 1, 0);
	rate = divider_recalc_rate(hw, parent_rate, val, NULL,
				   cd->div.flags, cd->div.width);

	return rate;
}

static u8 ccu_div_get_parent(struct clk_hw *hw)
{
	struct ccu_div *cd = hw_to_ccu_div(hw);

	return ccu_get_parent_helper(&cd->common, &cd->mux);
}

static int ccu_div_set_parent(struct clk_hw *hw, u8 index)
{
	struct ccu_div *cd = hw_to_ccu_div(hw);

	return ccu_set_parent_helper(&cd->common, &cd->mux, index);
}

static void ccu_div_disable(struct clk_hw *hw)
{
	struct ccu_div *cd = hw_to_ccu_div(hw);

	ccu_disable_helper(&cd->common, cd->enable);
}

static int ccu_div_enable(struct clk_hw *hw)
{
	struct ccu_div *cd = hw_to_ccu_div(hw);

	return ccu_enable_helper(&cd->common, cd->enable);
}

static int ccu_div_is_enabled(struct clk_hw *hw)
{
	struct ccu_div *cd = hw_to_ccu_div(hw);

	return ccu_is_enabled_helper(&cd->common, cd->enable);
}

static const struct clk_ops ccu_div_ops = {
	.disable	= ccu_div_disable,
	.enable		= ccu_div_enable,
	.is_enabled	= ccu_div_is_enabled,
	.get_parent	= ccu_div_get_parent,
	.set_parent	= ccu_div_set_parent,
	.recalc_rate	= ccu_div_recalc_rate,
	.determine_rate	= clk_hw_determine_rate_no_reparent,
};

static unsigned long th1520_pll_vco_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct ccu_pll *pll = hw_to_ccu_pll(hw);
	unsigned long div, mul, frac;
	unsigned int cfg0, cfg1;
	u64 rate = parent_rate;

	regmap_read(pll->common.map, pll->common.cfg0, &cfg0);
	regmap_read(pll->common.map, pll->common.cfg1, &cfg1);

	mul = FIELD_GET(TH1520_PLL_FBDIV, cfg0);
	div = FIELD_GET(TH1520_PLL_REFDIV, cfg0);
	if (!(cfg1 & TH1520_PLL_DSMPD)) {
		mul <<= TH1520_PLL_FRAC_BITS;
		frac = FIELD_GET(TH1520_PLL_FRAC, cfg1);
		mul += frac;
		div <<= TH1520_PLL_FRAC_BITS;
	}
	rate = parent_rate * mul;
	rate = rate / div;
	return rate;
}

static unsigned long th1520_pll_postdiv_recalc_rate(struct clk_hw *hw,
						    unsigned long parent_rate)
{
	struct ccu_pll *pll = hw_to_ccu_pll(hw);
	unsigned long div, rate = parent_rate;
	unsigned int cfg0, cfg1;

	regmap_read(pll->common.map, pll->common.cfg0, &cfg0);
	regmap_read(pll->common.map, pll->common.cfg1, &cfg1);

	if (cfg1 & TH1520_PLL_BYPASS)
		return rate;

	div = FIELD_GET(TH1520_PLL_POSTDIV1, cfg0) *
	      FIELD_GET(TH1520_PLL_POSTDIV2, cfg0);

	rate = rate / div;

	return rate;
}

static unsigned long ccu_pll_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	unsigned long rate = parent_rate;

	rate = th1520_pll_vco_recalc_rate(hw, rate);
	rate = th1520_pll_postdiv_recalc_rate(hw, rate);

	return rate;
}

static const struct clk_ops clk_pll_ops = {
	.recalc_rate	= ccu_pll_recalc_rate,
};

static const struct clk_parent_data osc_24m_clk[] = {
	{ .index = 0 }
};

static struct ccu_pll cpu_pll0_clk = {
	.common		= {
		.clkid		= CLK_CPU_PLL0,
		.cfg0		= 0x000,
		.cfg1		= 0x004,
		.hw.init	= CLK_HW_INIT_PARENTS_DATA("cpu-pll0",
					      osc_24m_clk,
					      &clk_pll_ops,
					      0),
	},
};

static struct ccu_pll cpu_pll1_clk = {
	.common		= {
		.clkid		= CLK_CPU_PLL1,
		.cfg0		= 0x010,
		.cfg1		= 0x014,
		.hw.init	= CLK_HW_INIT_PARENTS_DATA("cpu-pll1",
					      osc_24m_clk,
					      &clk_pll_ops,
					      0),
	},
};

static struct ccu_pll gmac_pll_clk = {
	.common		= {
		.clkid		= CLK_GMAC_PLL,
		.cfg0		= 0x020,
		.cfg1		= 0x024,
		.hw.init	= CLK_HW_INIT_PARENTS_DATA("gmac-pll",
					      osc_24m_clk,
					      &clk_pll_ops,
					      0),
	},
};

static const struct clk_hw *gmac_pll_clk_parent[] = {
	&gmac_pll_clk.common.hw
};

static const struct clk_parent_data gmac_pll_clk_pd[] = {
	{ .hw = &gmac_pll_clk.common.hw }
};

static struct ccu_pll video_pll_clk = {
	.common		= {
		.clkid		= CLK_VIDEO_PLL,
		.cfg0		= 0x030,
		.cfg1		= 0x034,
		.hw.init	= CLK_HW_INIT_PARENTS_DATA("video-pll",
					      osc_24m_clk,
					      &clk_pll_ops,
					      0),
	},
};

static const struct clk_hw *video_pll_clk_parent[] = {
	&video_pll_clk.common.hw
};

static const struct clk_parent_data video_pll_clk_pd[] = {
	{ .hw = &video_pll_clk.common.hw }
};

static struct ccu_pll dpu0_pll_clk = {
	.common		= {
		.clkid		= CLK_DPU0_PLL,
		.cfg0		= 0x040,
		.cfg1		= 0x044,
		.hw.init	= CLK_HW_INIT_PARENTS_DATA("dpu0-pll",
					      osc_24m_clk,
					      &clk_pll_ops,
					      0),
	},
};

static const struct clk_hw *dpu0_pll_clk_parent[] = {
	&dpu0_pll_clk.common.hw
};

static struct ccu_pll dpu1_pll_clk = {
	.common		= {
		.clkid		= CLK_DPU1_PLL,
		.cfg0		= 0x050,
		.cfg1		= 0x054,
		.hw.init	= CLK_HW_INIT_PARENTS_DATA("dpu1-pll",
					      osc_24m_clk,
					      &clk_pll_ops,
					      0),
	},
};

static const struct clk_hw *dpu1_pll_clk_parent[] = {
	&dpu1_pll_clk.common.hw
};

static struct ccu_pll tee_pll_clk = {
	.common		= {
		.clkid		= CLK_TEE_PLL,
		.cfg0		= 0x060,
		.cfg1		= 0x064,
		.hw.init	= CLK_HW_INIT_PARENTS_DATA("tee-pll",
					      osc_24m_clk,
					      &clk_pll_ops,
					      0),
	},
};

static const struct clk_parent_data c910_i0_parents[] = {
	{ .hw = &cpu_pll0_clk.common.hw },
	{ .index = 0 }
};

static struct ccu_mux c910_i0_clk = {
	.clkid	= CLK_C910_I0,
	.reg	= 0x100,
	.mux	= TH_CCU_MUX("c910-i0", c910_i0_parents, 1, 1),
};

static const struct clk_parent_data c910_parents[] = {
	{ .hw = &c910_i0_clk.mux.hw },
	{ .hw = &cpu_pll1_clk.common.hw }
};

static struct ccu_mux c910_clk = {
	.clkid	= CLK_C910,
	.reg	= 0x100,
	.mux	= TH_CCU_MUX("c910", c910_parents, 0, 1),
};

static const struct clk_parent_data ahb2_cpusys_parents[] = {
	{ .hw = &gmac_pll_clk.common.hw },
	{ .index = 0 }
};

static struct ccu_div ahb2_cpusys_hclk = {
	.div		= TH_CCU_DIV_FLAGS(0, 3, CLK_DIVIDER_ONE_BASED),
	.mux		= TH_CCU_ARG(5, 1),
	.common		= {
		.clkid          = CLK_AHB2_CPUSYS_HCLK,
		.cfg0		= 0x120,
		.hw.init	= CLK_HW_INIT_PARENTS_DATA("ahb2-cpusys-hclk",
						      ahb2_cpusys_parents,
						      &ccu_div_ops,
						      0),
	},
};

static const struct clk_parent_data ahb2_cpusys_hclk_pd[] = {
	{ .hw = &ahb2_cpusys_hclk.common.hw }
};

static const struct clk_hw *ahb2_cpusys_hclk_parent[] = {
	&ahb2_cpusys_hclk.common.hw,
};

static struct ccu_div apb3_cpusys_pclk = {
	.div		= TH_CCU_ARG(0, 3),
	.common		= {
		.clkid          = CLK_APB3_CPUSYS_PCLK,
		.cfg0		= 0x130,
		.hw.init	= CLK_HW_INIT_PARENTS_HW("apb3-cpusys-pclk",
							   ahb2_cpusys_hclk_parent,
							   &ccu_div_ops,
							   0),
	},
};

static const struct clk_parent_data apb3_cpusys_pclk_pd[] = {
	{ .hw = &apb3_cpusys_pclk.common.hw }
};

static struct ccu_div axi4_cpusys2_aclk = {
	.div		= TH_CCU_DIV_FLAGS(0, 3, CLK_DIVIDER_ONE_BASED),
	.common		= {
		.clkid          = CLK_AXI4_CPUSYS2_ACLK,
		.cfg0		= 0x134,
		.hw.init	= CLK_HW_INIT_PARENTS_HW("axi4-cpusys2-aclk",
					      gmac_pll_clk_parent,
					      &ccu_div_ops,
					      0),
	},
};

static const struct clk_parent_data axi4_cpusys2_aclk_pd[] = {
	{ .hw = &axi4_cpusys2_aclk.common.hw }
};

static const struct clk_parent_data axi_parents[] = {
	{ .hw = &video_pll_clk.common.hw },
	{ .index = 0 }
};

static struct ccu_div axi_aclk = {
	.div		= TH_CCU_DIV_FLAGS(0, 4, CLK_DIVIDER_ONE_BASED),
	.mux		= TH_CCU_ARG(5, 1),
	.common		= {
		.clkid          = CLK_AXI_ACLK,
		.cfg0		= 0x138,
		.hw.init	= CLK_HW_INIT_PARENTS_DATA("axi-aclk",
						      axi_parents,
						      &ccu_div_ops,
						      0),
	},
};

static const struct clk_parent_data axi_aclk_pd[] = {
	{ .hw = &axi_aclk.common.hw }
};

static const struct clk_parent_data perisys_ahb_hclk_parents[] = {
	{ .hw = &gmac_pll_clk.common.hw },
	{ .index = 0 },
};

static struct ccu_div perisys_ahb_hclk = {
	.enable		= BIT(6),
	.div		= TH_CCU_DIV_FLAGS(0, 4, CLK_DIVIDER_ONE_BASED),
	.mux		= TH_CCU_ARG(5, 1),
	.common		= {
		.clkid          = CLK_PERI_AHB_HCLK,
		.cfg0		= 0x140,
		.hw.init	= CLK_HW_INIT_PARENTS_DATA("perisys-ahb-hclk",
						      perisys_ahb_hclk_parents,
						      &ccu_div_ops,
						      0),
	},
};

static const struct clk_parent_data perisys_ahb_hclk_pd[] = {
	{ .hw = &perisys_ahb_hclk.common.hw }
};

static const struct clk_hw *perisys_ahb_hclk_parent[] = {
	&perisys_ahb_hclk.common.hw
};

static struct ccu_div perisys_apb_pclk = {
	.div		= TH_CCU_ARG(0, 3),
	.common		= {
		.clkid          = CLK_PERI_APB_PCLK,
		.cfg0		= 0x150,
		.hw.init	= CLK_HW_INIT_PARENTS_HW("perisys-apb-pclk",
					      perisys_ahb_hclk_parent,
					      &ccu_div_ops,
					      0),
	},
};

static const struct clk_parent_data perisys_apb_pclk_pd[] = {
	{ .hw = &perisys_apb_pclk.common.hw }
};

static struct ccu_div peri2sys_apb_pclk = {
	.div		= TH_CCU_DIV_FLAGS(4, 3, CLK_DIVIDER_ONE_BASED),
	.common		= {
		.clkid          = CLK_PERI2APB_PCLK,
		.cfg0		= 0x150,
		.hw.init	= CLK_HW_INIT_PARENTS_HW("peri2sys-apb-pclk",
					      gmac_pll_clk_parent,
					      &ccu_div_ops,
					      0),
	},
};

static const struct clk_parent_data peri2sys_apb_pclk_pd[] = {
	{ .hw = &peri2sys_apb_pclk.common.hw }
};

static struct clk_fixed_factor osc12m_clk = {
	.div		= 2,
	.mult		= 1,
	.hw.init	= CLK_HW_INIT_PARENTS_DATA("osc_12m",
						   osc_24m_clk,
						   &clk_fixed_factor_ops,
						   0),
};

static const char * const out_parents[] = { "osc_24m", "osc_12m" };

static struct ccu_div out1_clk = {
	.enable		= BIT(5),
	.div		= TH_CCU_DIV_FLAGS(0, 3, CLK_DIVIDER_ONE_BASED),
	.mux		= TH_CCU_ARG(4, 1),
	.common		= {
		.clkid          = CLK_OUT1,
		.cfg0		= 0x1b4,
		.hw.init	= CLK_HW_INIT_PARENTS("out1",
						      out_parents,
						      &ccu_div_ops,
						      0),
	},
};

static struct ccu_div out2_clk = {
	.enable		= BIT(5),
	.div		= TH_CCU_DIV_FLAGS(0, 3, CLK_DIVIDER_ONE_BASED),
	.mux		= TH_CCU_ARG(4, 1),
	.common		= {
		.clkid          = CLK_OUT2,
		.cfg0		= 0x1b8,
		.hw.init	= CLK_HW_INIT_PARENTS("out2",
						      out_parents,
						      &ccu_div_ops,
						      0),
	},
};

static struct ccu_div out3_clk = {
	.enable		= BIT(5),
	.div		= TH_CCU_DIV_FLAGS(0, 3, CLK_DIVIDER_ONE_BASED),
	.mux		= TH_CCU_ARG(4, 1),
	.common		= {
		.clkid          = CLK_OUT3,
		.cfg0		= 0x1bc,
		.hw.init	= CLK_HW_INIT_PARENTS("out3",
						      out_parents,
						      &ccu_div_ops,
						      0),
	},
};

static struct ccu_div out4_clk = {
	.enable		= BIT(5),
	.div		= TH_CCU_DIV_FLAGS(0, 3, CLK_DIVIDER_ONE_BASED),
	.mux		= TH_CCU_ARG(4, 1),
	.common		= {
		.clkid          = CLK_OUT4,
		.cfg0		= 0x1c0,
		.hw.init	= CLK_HW_INIT_PARENTS("out4",
						      out_parents,
						      &ccu_div_ops,
						      0),
	},
};

static const struct clk_parent_data apb_parents[] = {
	{ .hw = &gmac_pll_clk.common.hw },
	{ .index = 0 },
};

static struct ccu_div apb_pclk = {
	.enable		= BIT(5),
	.div		= TH_CCU_DIV_FLAGS(0, 4, CLK_DIVIDER_ONE_BASED),
	.mux		= TH_CCU_ARG(7, 1),
	.common		= {
		.clkid          = CLK_APB_PCLK,
		.cfg0		= 0x1c4,
		.hw.init	= CLK_HW_INIT_PARENTS_DATA("apb-pclk",
						      apb_parents,
						      &ccu_div_ops,
						      CLK_IGNORE_UNUSED),
	},
};

static const struct clk_hw *npu_parents[] = {
	&gmac_pll_clk.common.hw,
	&video_pll_clk.common.hw
};

static struct ccu_div npu_clk = {
	.enable		= BIT(4),
	.div		= TH_CCU_DIV_FLAGS(0, 3, CLK_DIVIDER_ONE_BASED),
	.mux		= TH_CCU_ARG(6, 1),
	.common		= {
		.clkid          = CLK_NPU,
		.cfg0		= 0x1c8,
		.hw.init	= CLK_HW_INIT_PARENTS_HW("npu",
						      npu_parents,
						      &ccu_div_ops,
						      0),
	},
};

static struct ccu_div vi_clk = {
	.div		= TH_CCU_DIV_FLAGS(16, 4, CLK_DIVIDER_ONE_BASED),
	.common		= {
		.clkid          = CLK_VI,
		.cfg0		= 0x1d0,
		.hw.init	= CLK_HW_INIT_PARENTS_HW("vi",
					      video_pll_clk_parent,
					      &ccu_div_ops,
					      0),
	},
};

static struct ccu_div vi_ahb_clk = {
	.div		= TH_CCU_DIV_FLAGS(0, 4, CLK_DIVIDER_ONE_BASED),
	.common		= {
		.clkid          = CLK_VI_AHB,
		.cfg0		= 0x1d0,
		.hw.init	= CLK_HW_INIT_PARENTS_HW("vi-ahb",
					      video_pll_clk_parent,
					      &ccu_div_ops,
					      0),
	},
};

static struct ccu_div vo_axi_clk = {
	.enable		= BIT(5),
	.div		= TH_CCU_DIV_FLAGS(0, 4, CLK_DIVIDER_ONE_BASED),
	.common		= {
		.clkid          = CLK_VO_AXI,
		.cfg0		= 0x1dc,
		.hw.init	= CLK_HW_INIT_PARENTS_HW("vo-axi",
					      video_pll_clk_parent,
					      &ccu_div_ops,
					      0),
	},
};

static struct ccu_div vp_apb_clk = {
	.div		= TH_CCU_DIV_FLAGS(0, 3, CLK_DIVIDER_ONE_BASED),
	.common		= {
		.clkid          = CLK_VP_APB,
		.cfg0		= 0x1e0,
		.hw.init	= CLK_HW_INIT_PARENTS_HW("vp-apb",
					      gmac_pll_clk_parent,
					      &ccu_div_ops,
					      0),
	},
};

static struct ccu_div vp_axi_clk = {
	.enable		= BIT(15),
	.div		= TH_CCU_DIV_FLAGS(8, 4, CLK_DIVIDER_ONE_BASED),
	.common		= {
		.clkid          = CLK_VP_AXI,
		.cfg0		= 0x1e0,
		.hw.init	= CLK_HW_INIT_PARENTS_HW("vp-axi",
					      video_pll_clk_parent,
					      &ccu_div_ops,
					      CLK_IGNORE_UNUSED),
	},
};

static struct ccu_div venc_clk = {
	.enable		= BIT(5),
	.div		= TH_CCU_DIV_FLAGS(0, 3, CLK_DIVIDER_ONE_BASED),
	.common		= {
		.clkid          = CLK_VENC,
		.cfg0		= 0x1e4,
		.hw.init	= CLK_HW_INIT_PARENTS_HW("venc",
					      gmac_pll_clk_parent,
					      &ccu_div_ops,
					      0),
	},
};

static struct ccu_div dpu0_clk = {
	.div		= TH_CCU_DIV_FLAGS(0, 8, CLK_DIVIDER_ONE_BASED),
	.common		= {
		.clkid          = CLK_DPU0,
		.cfg0		= 0x1e8,
		.hw.init	= CLK_HW_INIT_PARENTS_HW("dpu0",
					      dpu0_pll_clk_parent,
					      &ccu_div_ops,
					      0),
	},
};

static struct ccu_div dpu1_clk = {
	.div		= TH_CCU_DIV_FLAGS(0, 8, CLK_DIVIDER_ONE_BASED),
	.common		= {
		.clkid          = CLK_DPU1,
		.cfg0		= 0x1ec,
		.hw.init	= CLK_HW_INIT_PARENTS_HW("dpu1",
					      dpu1_pll_clk_parent,
					      &ccu_div_ops,
					      0),
	},
};

static CLK_FIXED_FACTOR_HW(emmc_sdio_ref_clk, "emmc-sdio-ref",
			   &video_pll_clk.common.hw, 4, 1, 0);

static const struct clk_parent_data emmc_sdio_ref_clk_pd[] = {
	{ .hw = &emmc_sdio_ref_clk.hw },
};

static CCU_GATE(CLK_BROM, brom_clk, "brom", ahb2_cpusys_hclk_pd, 0x100, BIT(4), 0);
static CCU_GATE(CLK_BMU, bmu_clk, "bmu", axi4_cpusys2_aclk_pd, 0x100, BIT(5), 0);
static CCU_GATE(CLK_AON2CPU_A2X, aon2cpu_a2x_clk, "aon2cpu-a2x", axi4_cpusys2_aclk_pd,
		0x134, BIT(8), 0);
static CCU_GATE(CLK_X2X_CPUSYS, x2x_cpusys_clk, "x2x-cpusys", axi4_cpusys2_aclk_pd,
		0x134, BIT(7), 0);
static CCU_GATE(CLK_CPU2AON_X2H, cpu2aon_x2h_clk, "cpu2aon-x2h", axi_aclk_pd,
		0x138, BIT(8), CLK_IGNORE_UNUSED);
static CCU_GATE(CLK_CPU2PERI_X2H, cpu2peri_x2h_clk, "cpu2peri-x2h", axi4_cpusys2_aclk_pd,
		0x140, BIT(9), CLK_IGNORE_UNUSED);
static CCU_GATE(CLK_PERISYS_APB1_HCLK, perisys_apb1_hclk, "perisys-apb1-hclk", perisys_ahb_hclk_pd,
		0x150, BIT(9), CLK_IGNORE_UNUSED);
static CCU_GATE(CLK_PERISYS_APB2_HCLK, perisys_apb2_hclk, "perisys-apb2-hclk", perisys_ahb_hclk_pd,
		0x150, BIT(10), CLK_IGNORE_UNUSED);
static CCU_GATE(CLK_PERISYS_APB3_HCLK, perisys_apb3_hclk, "perisys-apb3-hclk", perisys_ahb_hclk_pd,
		0x150, BIT(11), CLK_IGNORE_UNUSED);
static CCU_GATE(CLK_PERISYS_APB4_HCLK, perisys_apb4_hclk, "perisys-apb4-hclk", perisys_ahb_hclk_pd,
		0x150, BIT(12), 0);
static CCU_GATE(CLK_NPU_AXI, npu_axi_clk, "npu-axi", axi_aclk_pd, 0x1c8, BIT(5), 0);
static CCU_GATE(CLK_CPU2VP, cpu2vp_clk, "cpu2vp", axi_aclk_pd, 0x1e0, BIT(13), 0);
static CCU_GATE(CLK_EMMC_SDIO, emmc_sdio_clk, "emmc-sdio", emmc_sdio_ref_clk_pd, 0x204, BIT(30), 0);
static CCU_GATE(CLK_GMAC1, gmac1_clk, "gmac1", gmac_pll_clk_pd, 0x204, BIT(26), 0);
static CCU_GATE(CLK_PADCTRL1, padctrl1_clk, "padctrl1", perisys_apb_pclk_pd, 0x204, BIT(24), 0);
static CCU_GATE(CLK_DSMART, dsmart_clk, "dsmart", perisys_apb_pclk_pd, 0x204, BIT(23), 0);
static CCU_GATE(CLK_PADCTRL0, padctrl0_clk, "padctrl0", perisys_apb_pclk_pd, 0x204, BIT(22), 0);
static CCU_GATE(CLK_GMAC_AXI, gmac_axi_clk, "gmac-axi", axi4_cpusys2_aclk_pd, 0x204, BIT(21), 0);
static CCU_GATE(CLK_GPIO3, gpio3_clk, "gpio3-clk", peri2sys_apb_pclk_pd, 0x204, BIT(20), 0);
static CCU_GATE(CLK_GMAC0, gmac0_clk, "gmac0", gmac_pll_clk_pd, 0x204, BIT(19), 0);
static CCU_GATE(CLK_PWM, pwm_clk, "pwm", perisys_apb_pclk_pd, 0x204, BIT(18), 0);
static CCU_GATE(CLK_QSPI0, qspi0_clk, "qspi0", video_pll_clk_pd, 0x204, BIT(17), 0);
static CCU_GATE(CLK_QSPI1, qspi1_clk, "qspi1", video_pll_clk_pd, 0x204, BIT(16), 0);
static CCU_GATE(CLK_SPI, spi_clk, "spi", video_pll_clk_pd, 0x204, BIT(15), 0);
static CCU_GATE(CLK_UART0_PCLK, uart0_pclk, "uart0-pclk", perisys_apb_pclk_pd, 0x204, BIT(14), 0);
static CCU_GATE(CLK_UART1_PCLK, uart1_pclk, "uart1-pclk", perisys_apb_pclk_pd, 0x204, BIT(13), 0);
static CCU_GATE(CLK_UART2_PCLK, uart2_pclk, "uart2-pclk", perisys_apb_pclk_pd, 0x204, BIT(12), 0);
static CCU_GATE(CLK_UART3_PCLK, uart3_pclk, "uart3-pclk", perisys_apb_pclk_pd, 0x204, BIT(11), 0);
static CCU_GATE(CLK_UART4_PCLK, uart4_pclk, "uart4-pclk", perisys_apb_pclk_pd, 0x204, BIT(10), 0);
static CCU_GATE(CLK_UART5_PCLK, uart5_pclk, "uart5-pclk", perisys_apb_pclk_pd, 0x204, BIT(9), 0);
static CCU_GATE(CLK_GPIO0, gpio0_clk, "gpio0-clk", perisys_apb_pclk_pd, 0x204, BIT(8), 0);
static CCU_GATE(CLK_GPIO1, gpio1_clk, "gpio1-clk", perisys_apb_pclk_pd, 0x204, BIT(7), 0);
static CCU_GATE(CLK_GPIO2, gpio2_clk, "gpio2-clk", peri2sys_apb_pclk_pd, 0x204, BIT(6), 0);
static CCU_GATE(CLK_I2C0, i2c0_clk, "i2c0", perisys_apb_pclk_pd, 0x204, BIT(5), 0);
static CCU_GATE(CLK_I2C1, i2c1_clk, "i2c1", perisys_apb_pclk_pd, 0x204, BIT(4), 0);
static CCU_GATE(CLK_I2C2, i2c2_clk, "i2c2", perisys_apb_pclk_pd, 0x204, BIT(3), 0);
static CCU_GATE(CLK_I2C3, i2c3_clk, "i2c3", perisys_apb_pclk_pd, 0x204, BIT(2), 0);
static CCU_GATE(CLK_I2C4, i2c4_clk, "i2c4", perisys_apb_pclk_pd, 0x204, BIT(1), 0);
static CCU_GATE(CLK_I2C5, i2c5_clk, "i2c5", perisys_apb_pclk_pd, 0x204, BIT(0), 0);
static CCU_GATE(CLK_SPINLOCK, spinlock_clk, "spinlock", ahb2_cpusys_hclk_pd, 0x208, BIT(10), 0);
static CCU_GATE(CLK_DMA, dma_clk, "dma", axi4_cpusys2_aclk_pd, 0x208, BIT(8), 0);
static CCU_GATE(CLK_MBOX0, mbox0_clk, "mbox0", apb3_cpusys_pclk_pd, 0x208, BIT(7), 0);
static CCU_GATE(CLK_MBOX1, mbox1_clk, "mbox1", apb3_cpusys_pclk_pd, 0x208, BIT(6), 0);
static CCU_GATE(CLK_MBOX2, mbox2_clk, "mbox2", apb3_cpusys_pclk_pd, 0x208, BIT(5), 0);
static CCU_GATE(CLK_MBOX3, mbox3_clk, "mbox3", apb3_cpusys_pclk_pd, 0x208, BIT(4), 0);
static CCU_GATE(CLK_WDT0, wdt0_clk, "wdt0", apb3_cpusys_pclk_pd, 0x208, BIT(3), 0);
static CCU_GATE(CLK_WDT1, wdt1_clk, "wdt1", apb3_cpusys_pclk_pd, 0x208, BIT(2), 0);
static CCU_GATE(CLK_TIMER0, timer0_clk, "timer0", apb3_cpusys_pclk_pd, 0x208, BIT(1), 0);
static CCU_GATE(CLK_TIMER1, timer1_clk, "timer1", apb3_cpusys_pclk_pd, 0x208, BIT(0), 0);
static CCU_GATE(CLK_SRAM0, sram0_clk, "sram0", axi_aclk_pd, 0x20c, BIT(4), 0);
static CCU_GATE(CLK_SRAM1, sram1_clk, "sram1", axi_aclk_pd, 0x20c, BIT(3), 0);
static CCU_GATE(CLK_SRAM2, sram2_clk, "sram2", axi_aclk_pd, 0x20c, BIT(2), 0);
static CCU_GATE(CLK_SRAM3, sram3_clk, "sram3", axi_aclk_pd, 0x20c, BIT(1), 0);

static CCU_GATE(CLK_AXI4_VO_ACLK, axi4_vo_aclk, "axi4-vo-aclk",
		video_pll_clk_pd, 0x0, BIT(0), 0);
static CCU_GATE(CLK_GPU_CORE, gpu_core_clk, "gpu-core-clk", video_pll_clk_pd,
		0x0, BIT(3), 0);
static CCU_GATE(CLK_GPU_CFG_ACLK, gpu_cfg_aclk, "gpu-cfg-aclk",
		video_pll_clk_pd, 0x0, BIT(4), 0);
static CCU_GATE(CLK_DPU_PIXELCLK0, dpu0_pixelclk, "dpu0-pixelclk",
		video_pll_clk_pd, 0x0, BIT(5), 0);
static CCU_GATE(CLK_DPU_PIXELCLK1, dpu1_pixelclk, "dpu1-pixelclk",
		video_pll_clk_pd, 0x0, BIT(6), 0);
static CCU_GATE(CLK_DPU_HCLK, dpu_hclk, "dpu-hclk", video_pll_clk_pd, 0x0,
		BIT(7), 0);
static CCU_GATE(CLK_DPU_ACLK, dpu_aclk, "dpu-aclk", video_pll_clk_pd, 0x0,
		BIT(8), 0);
static CCU_GATE(CLK_DPU_CCLK, dpu_cclk, "dpu-cclk", video_pll_clk_pd, 0x0,
		BIT(9), 0);
static CCU_GATE(CLK_HDMI_SFR, hdmi_sfr_clk, "hdmi-sfr-clk", video_pll_clk_pd,
		0x0, BIT(10), 0);
static CCU_GATE(CLK_HDMI_PCLK, hdmi_pclk, "hdmi-pclk", video_pll_clk_pd, 0x0,
		BIT(11), 0);
static CCU_GATE(CLK_HDMI_CEC, hdmi_cec_clk, "hdmi-cec-clk", video_pll_clk_pd,
		0x0, BIT(12), 0);
static CCU_GATE(CLK_MIPI_DSI0_PCLK, mipi_dsi0_pclk, "mipi-dsi0-pclk",
		video_pll_clk_pd, 0x0, BIT(13), 0);
static CCU_GATE(CLK_MIPI_DSI1_PCLK, mipi_dsi1_pclk, "mipi-dsi1-pclk",
		video_pll_clk_pd, 0x0, BIT(14), 0);
static CCU_GATE(CLK_MIPI_DSI0_CFG, mipi_dsi0_cfg_clk, "mipi-dsi0-cfg-clk",
		video_pll_clk_pd, 0x0, BIT(15), 0);
static CCU_GATE(CLK_MIPI_DSI1_CFG, mipi_dsi1_cfg_clk, "mipi-dsi1-cfg-clk",
		video_pll_clk_pd, 0x0, BIT(16), 0);
static CCU_GATE(CLK_MIPI_DSI0_REFCLK, mipi_dsi0_refclk, "mipi-dsi0-refclk",
		video_pll_clk_pd, 0x0, BIT(17), 0);
static CCU_GATE(CLK_MIPI_DSI1_REFCLK, mipi_dsi1_refclk, "mipi-dsi1-refclk",
		video_pll_clk_pd, 0x0, BIT(18), 0);
static CCU_GATE(CLK_HDMI_I2S, hdmi_i2s_clk, "hdmi-i2s-clk", video_pll_clk_pd,
		0x0, BIT(19), 0);
static CCU_GATE(CLK_X2H_DPU1_ACLK, x2h_dpu1_aclk, "x2h-dpu1-aclk",
		video_pll_clk_pd, 0x0, BIT(20), 0);
static CCU_GATE(CLK_X2H_DPU_ACLK, x2h_dpu_aclk, "x2h-dpu-aclk",
		video_pll_clk_pd, 0x0, BIT(21), 0);
static CCU_GATE(CLK_AXI4_VO_PCLK, axi4_vo_pclk, "axi4-vo-pclk",
		video_pll_clk_pd, 0x0, BIT(22), 0);
static CCU_GATE(CLK_IOPMP_VOSYS_DPU_PCLK, iopmp_vosys_dpu_pclk,
		"iopmp-vosys-dpu-pclk", video_pll_clk_pd, 0x0, BIT(23), 0);
static CCU_GATE(CLK_IOPMP_VOSYS_DPU1_PCLK, iopmp_vosys_dpu1_pclk,
		"iopmp-vosys-dpu1-pclk", video_pll_clk_pd, 0x0, BIT(24), 0);
static CCU_GATE(CLK_IOPMP_VOSYS_GPU_PCLK, iopmp_vosys_gpu_pclk,
		"iopmp-vosys-gpu-pclk", video_pll_clk_pd, 0x0, BIT(25), 0);
static CCU_GATE(CLK_IOPMP_DPU1_ACLK, iopmp_dpu1_aclk, "iopmp-dpu1-aclk",
		video_pll_clk_pd, 0x0, BIT(27), 0);
static CCU_GATE(CLK_IOPMP_DPU_ACLK, iopmp_dpu_aclk, "iopmp-dpu-aclk",
		video_pll_clk_pd, 0x0, BIT(28), 0);
static CCU_GATE(CLK_IOPMP_GPU_ACLK, iopmp_gpu_aclk, "iopmp-gpu-aclk",
		video_pll_clk_pd, 0x0, BIT(29), 0);
static CCU_GATE(CLK_MIPIDSI0_PIXCLK, mipi_dsi0_pixclk, "mipi-dsi0-pixclk",
		video_pll_clk_pd, 0x0, BIT(30), 0);
static CCU_GATE(CLK_MIPIDSI1_PIXCLK, mipi_dsi1_pixclk, "mipi-dsi1-pixclk",
		video_pll_clk_pd, 0x0, BIT(31), 0);
static CCU_GATE(CLK_HDMI_PIXCLK, hdmi_pixclk, "hdmi-pixclk", video_pll_clk_pd,
		0x4, BIT(0), 0);

static CLK_FIXED_FACTOR_HW(gmac_pll_clk_100m, "gmac-pll-clk-100m",
			   &gmac_pll_clk.common.hw, 10, 1, 0);

static const struct clk_parent_data uart_sclk_parents[] = {
	{ .hw = &gmac_pll_clk_100m.hw },
	{ .index = 0 },
};

static struct ccu_mux uart_sclk = {
	.clkid	= CLK_UART_SCLK,
	.reg	= 0x210,
	.mux	= TH_CCU_MUX("uart-sclk", uart_sclk_parents, 0, 1),
};

static struct ccu_common *th1520_pll_clks[] = {
	&cpu_pll0_clk.common,
	&cpu_pll1_clk.common,
	&gmac_pll_clk.common,
	&video_pll_clk.common,
	&dpu0_pll_clk.common,
	&dpu1_pll_clk.common,
	&tee_pll_clk.common,
};

static struct ccu_common *th1520_div_clks[] = {
	&ahb2_cpusys_hclk.common,
	&apb3_cpusys_pclk.common,
	&axi4_cpusys2_aclk.common,
	&perisys_ahb_hclk.common,
	&perisys_apb_pclk.common,
	&axi_aclk.common,
	&peri2sys_apb_pclk.common,
	&out1_clk.common,
	&out2_clk.common,
	&out3_clk.common,
	&out4_clk.common,
	&apb_pclk.common,
	&npu_clk.common,
	&vi_clk.common,
	&vi_ahb_clk.common,
	&vo_axi_clk.common,
	&vp_apb_clk.common,
	&vp_axi_clk.common,
	&venc_clk.common,
	&dpu0_clk.common,
	&dpu1_clk.common,
};

static struct ccu_mux *th1520_mux_clks[] = {
	&c910_i0_clk,
	&c910_clk,
	&uart_sclk,
};

static struct ccu_common *th1520_gate_clks[] = {
	&emmc_sdio_clk.common,
	&aon2cpu_a2x_clk.common,
	&x2x_cpusys_clk.common,
	&brom_clk.common,
	&bmu_clk.common,
	&cpu2aon_x2h_clk.common,
	&cpu2peri_x2h_clk.common,
	&cpu2vp_clk.common,
	&perisys_apb1_hclk.common,
	&perisys_apb2_hclk.common,
	&perisys_apb3_hclk.common,
	&perisys_apb4_hclk.common,
	&npu_axi_clk.common,
	&gmac1_clk.common,
	&padctrl1_clk.common,
	&dsmart_clk.common,
	&padctrl0_clk.common,
	&gmac_axi_clk.common,
	&gpio3_clk.common,
	&gmac0_clk.common,
	&pwm_clk.common,
	&qspi0_clk.common,
	&qspi1_clk.common,
	&spi_clk.common,
	&uart0_pclk.common,
	&uart1_pclk.common,
	&uart2_pclk.common,
	&uart3_pclk.common,
	&uart4_pclk.common,
	&uart5_pclk.common,
	&gpio0_clk.common,
	&gpio1_clk.common,
	&gpio2_clk.common,
	&i2c0_clk.common,
	&i2c1_clk.common,
	&i2c2_clk.common,
	&i2c3_clk.common,
	&i2c4_clk.common,
	&i2c5_clk.common,
	&spinlock_clk.common,
	&dma_clk.common,
	&mbox0_clk.common,
	&mbox1_clk.common,
	&mbox2_clk.common,
	&mbox3_clk.common,
	&wdt0_clk.common,
	&wdt1_clk.common,
	&timer0_clk.common,
	&timer1_clk.common,
	&sram0_clk.common,
	&sram1_clk.common,
	&sram2_clk.common,
	&sram3_clk.common,
};

static struct ccu_common *th1520_vo_gate_clks[] = {
	&axi4_vo_aclk.common,
	&gpu_core_clk.common,
	&gpu_cfg_aclk.common,
	&dpu0_pixelclk.common,
	&dpu1_pixelclk.common,
	&dpu_hclk.common,
	&dpu_aclk.common,
	&dpu_cclk.common,
	&hdmi_sfr_clk.common,
	&hdmi_pclk.common,
	&hdmi_cec_clk.common,
	&mipi_dsi0_pclk.common,
	&mipi_dsi1_pclk.common,
	&mipi_dsi0_cfg_clk.common,
	&mipi_dsi1_cfg_clk.common,
	&mipi_dsi0_refclk.common,
	&mipi_dsi1_refclk.common,
	&hdmi_i2s_clk.common,
	&x2h_dpu1_aclk.common,
	&x2h_dpu_aclk.common,
	&axi4_vo_pclk.common,
	&iopmp_vosys_dpu_pclk.common,
	&iopmp_vosys_dpu1_pclk.common,
	&iopmp_vosys_gpu_pclk.common,
	&iopmp_dpu1_aclk.common,
	&iopmp_dpu_aclk.common,
	&iopmp_gpu_aclk.common,
	&mipi_dsi0_pixclk.common,
	&mipi_dsi1_pixclk.common,
	&hdmi_pixclk.common
};

static const struct regmap_config th1520_clk_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.fast_io = true,
};

struct th1520_plat_data {
	struct ccu_common **th1520_pll_clks;
	struct ccu_common **th1520_div_clks;
	struct ccu_mux	  **th1520_mux_clks;
	struct ccu_common **th1520_gate_clks;

	int nr_clks;
	int nr_pll_clks;
	int nr_div_clks;
	int nr_mux_clks;
	int nr_gate_clks;
};

static const struct th1520_plat_data th1520_ap_platdata = {
	.th1520_pll_clks = th1520_pll_clks,
	.th1520_div_clks = th1520_div_clks,
	.th1520_mux_clks = th1520_mux_clks,
	.th1520_gate_clks = th1520_gate_clks,

	.nr_clks = CLK_UART_SCLK + 1,

	.nr_pll_clks = ARRAY_SIZE(th1520_pll_clks),
	.nr_div_clks = ARRAY_SIZE(th1520_div_clks),
	.nr_mux_clks = ARRAY_SIZE(th1520_mux_clks),
	.nr_gate_clks = ARRAY_SIZE(th1520_gate_clks),
};

static const struct th1520_plat_data th1520_vo_platdata = {
	.th1520_gate_clks = th1520_vo_gate_clks,

	.nr_clks = CLK_HDMI_PIXCLK + 1,

	.nr_gate_clks = ARRAY_SIZE(th1520_vo_gate_clks),
};

static int th1520_clk_probe(struct platform_device *pdev)
{
	const struct th1520_plat_data *plat_data;
	struct device *dev = &pdev->dev;
	struct clk_hw_onecell_data *priv;

	struct regmap *map;
	void __iomem *base;
	struct clk_hw *hw;
	int ret, i;

	plat_data = device_get_match_data(&pdev->dev);
	if (!plat_data)
		return dev_err_probe(&pdev->dev, -ENODEV,
				     "No device match data found\n");

	priv = devm_kzalloc(dev, struct_size(priv, hws, plat_data->nr_clks), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->num = plat_data->nr_clks;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	map = devm_regmap_init_mmio(dev, base, &th1520_clk_regmap_config);
	if (IS_ERR(map))
		return PTR_ERR(map);

	for (i = 0; i < plat_data->nr_pll_clks; i++) {
		struct ccu_pll *cp = hw_to_ccu_pll(&plat_data->th1520_pll_clks[i]->hw);

		plat_data->th1520_pll_clks[i]->map = map;

		ret = devm_clk_hw_register(dev, &plat_data->th1520_pll_clks[i]->hw);
		if (ret)
			return ret;

		priv->hws[cp->common.clkid] = &cp->common.hw;
	}

	for (i = 0; i < plat_data->nr_div_clks; i++) {
		struct ccu_div *cd = hw_to_ccu_div(&plat_data->th1520_div_clks[i]->hw);

		plat_data->th1520_div_clks[i]->map = map;

		ret = devm_clk_hw_register(dev, &plat_data->th1520_div_clks[i]->hw);
		if (ret)
			return ret;

		priv->hws[cd->common.clkid] = &cd->common.hw;
	}

	for (i = 0; i < plat_data->nr_mux_clks; i++) {
		struct ccu_mux *cm = plat_data->th1520_mux_clks[i];

		cm->mux.reg = base + cm->reg;

		ret = devm_clk_hw_register(dev, &cm->mux.hw);
		if (ret)
			return ret;

		priv->hws[cm->clkid] = &cm->mux.hw;
	}

	for (i = 0; i < plat_data->nr_gate_clks; i++) {
		struct ccu_gate *cg = hw_to_ccu_gate(&plat_data->th1520_gate_clks[i]->hw);

		plat_data->th1520_gate_clks[i]->map = map;

		hw = devm_clk_hw_register_gate_parent_data(dev,
							   cg->common.hw.init->name,
							   cg->common.hw.init->parent_data,
							   cg->common.hw.init->flags,
							   base + cg->common.cfg0,
							   ffs(cg->enable) - 1, 0, NULL);
		if (IS_ERR(hw))
			return PTR_ERR(hw);

		priv->hws[cg->common.clkid] = hw;
	}

	if (plat_data == &th1520_ap_platdata) {
		ret = devm_clk_hw_register(dev, &osc12m_clk.hw);
		if (ret)
			return ret;
		priv->hws[CLK_OSC12M] = &osc12m_clk.hw;

		ret = devm_clk_hw_register(dev, &gmac_pll_clk_100m.hw);
		if (ret)
			return ret;
		priv->hws[CLK_PLL_GMAC_100M] = &gmac_pll_clk_100m.hw;

		ret = devm_clk_hw_register(dev, &emmc_sdio_ref_clk.hw);
		if (ret)
			return ret;
	}

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, priv);
	if (ret)
		return ret;

	return 0;
}

static const struct of_device_id th1520_clk_match[] = {
	{
		.compatible = "thead,th1520-clk-ap",
		.data = &th1520_ap_platdata,
	},
	{
		.compatible = "thead,th1520-clk-vo",
		.data = &th1520_vo_platdata,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, th1520_clk_match);

static struct platform_driver th1520_clk_driver = {
	.probe		= th1520_clk_probe,
	.driver		= {
		.name	= "th1520-clk",
		.of_match_table = th1520_clk_match,
	},
};
module_platform_driver(th1520_clk_driver);

MODULE_DESCRIPTION("T-HEAD TH1520 AP Clock driver");
MODULE_AUTHOR("Yangtao Li <frank.li@vivo.com>");
MODULE_AUTHOR("Jisheng Zhang <jszhang@kernel.org>");
MODULE_LICENSE("GPL");
