// SPDX-License-Identifier: GPL-2.0
/*
 * r9a09g077 Clock Pulse Generator / Module Standby and Software Reset
 *
 * Copyright (C) 2025 Renesas Electronics Corp.
 *
 */

#include <linux/bitfield.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include <dt-bindings/clock/renesas,r9a09g077-cpg-mssr.h>
#include <dt-bindings/clock/renesas,r9a09g087-cpg-mssr.h>
#include "renesas-cpg-mssr.h"

#define RZT2H_REG_BLOCK_SHIFT	11
#define RZT2H_REG_OFFSET_MASK	GENMASK(10, 0)
#define RZT2H_REG_CONF(block, offset)	(((block) << RZT2H_REG_BLOCK_SHIFT) | \
					((offset) & RZT2H_REG_OFFSET_MASK))

#define RZT2H_REG_BLOCK(x)		((x) >> RZT2H_REG_BLOCK_SHIFT)
#define RZT2H_REG_OFFSET(x)		((x) & RZT2H_REG_OFFSET_MASK)

#define SCKCR		RZT2H_REG_CONF(0, 0x00)
#define SCKCR2		RZT2H_REG_CONF(1, 0x04)
#define SCKCR3		RZT2H_REG_CONF(0, 0x08)

#define OFFSET_MASK	GENMASK(31, 20)
#define SHIFT_MASK	GENMASK(19, 12)
#define WIDTH_MASK	GENMASK(11, 8)

#define CONF_PACK(offset, shift, width)  \
	(FIELD_PREP_CONST(OFFSET_MASK, (offset)) | \
	FIELD_PREP_CONST(SHIFT_MASK, (shift)) | \
	FIELD_PREP_CONST(WIDTH_MASK, (width)))

#define GET_SHIFT(val)         FIELD_GET(SHIFT_MASK, val)
#define GET_WIDTH(val)         FIELD_GET(WIDTH_MASK, val)
#define GET_REG_OFFSET(val)    FIELD_GET(OFFSET_MASK, val)

#define DIVCA55C0	CONF_PACK(SCKCR2, 8, 1)
#define DIVCA55C1	CONF_PACK(SCKCR2, 9, 1)
#define DIVCA55C2	CONF_PACK(SCKCR2, 10, 1)
#define DIVCA55C3	CONF_PACK(SCKCR2, 11, 1)
#define DIVCA55S	CONF_PACK(SCKCR2, 12, 1)
#define DIVSCI5ASYNC	CONF_PACK(SCKCR2, 18, 2)

#define DIVSCI0ASYNC	CONF_PACK(SCKCR3, 6, 2)
#define DIVSCI1ASYNC	CONF_PACK(SCKCR3, 8, 2)
#define DIVSCI2ASYNC	CONF_PACK(SCKCR3, 10, 2)
#define DIVSCI3ASYNC	CONF_PACK(SCKCR3, 12, 2)
#define DIVSCI4ASYNC	CONF_PACK(SCKCR3, 14, 2)

#define SEL_PLL		CONF_PACK(SCKCR, 22, 1)


enum rzt2h_clk_types {
	CLK_TYPE_RZT2H_DIV = CLK_TYPE_CUSTOM,	/* Clock with divider */
	CLK_TYPE_RZT2H_MUX,			/* Clock with clock source selector */
};

#define DEF_DIV(_name, _id, _parent, _conf, _dtable) \
	DEF_TYPE(_name, _id, CLK_TYPE_RZT2H_DIV, .conf = _conf, \
		 .parent = _parent, .dtable = _dtable, .flag = 0)
#define DEF_MUX(_name, _id, _conf, _parent_names, _num_parents, _mux_flags) \
	DEF_TYPE(_name, _id, CLK_TYPE_RZT2H_MUX, .conf = _conf, \
		 .parent_names = _parent_names, .num_parents = _num_parents, \
		 .flag = 0, .mux_flags = _mux_flags)

enum clk_ids {
	/* Core Clock Outputs exported to DT */
	LAST_DT_CORE_CLK = R9A09G077_ETCLKE,

	/* External Input Clocks */
	CLK_EXTAL,

	/* Internal Core Clocks */
	CLK_LOCO,
	CLK_PLL0,
	CLK_PLL1,
	CLK_PLL2,
	CLK_PLL4,
	CLK_SEL_CLK_PLL0,
	CLK_SEL_CLK_PLL1,
	CLK_SEL_CLK_PLL2,
	CLK_SEL_CLK_PLL4,
	CLK_PLL4D1,
	CLK_SCI0ASYNC,
	CLK_SCI1ASYNC,
	CLK_SCI2ASYNC,
	CLK_SCI3ASYNC,
	CLK_SCI4ASYNC,
	CLK_SCI5ASYNC,

	/* Module Clocks */
	MOD_CLK_BASE,
};

static const struct clk_div_table dtable_1_2[] = {
	{0, 2},
	{1, 1},
	{0, 0},
};

static const struct clk_div_table dtable_24_25_30_32[] = {
	{0, 32},
	{1, 30},
	{2, 25},
	{3, 24},
	{0, 0},
};

/* Mux clock tables */

static const char * const sel_clk_pll0[] = { ".loco", ".pll0" };
static const char * const sel_clk_pll1[] = { ".loco", ".pll1" };
static const char * const sel_clk_pll2[] = { ".loco", ".pll2" };
static const char * const sel_clk_pll4[] = { ".loco", ".pll4" };

static const struct cpg_core_clk r9a09g077_core_clks[] __initconst = {
	/* External Clock Inputs */
	DEF_INPUT("extal", CLK_EXTAL),

	/* Internal Core Clocks */
	DEF_RATE(".loco", CLK_LOCO, 1000 * 1000),
	DEF_FIXED(".pll0", CLK_PLL0, CLK_EXTAL, 1, 48),
	DEF_FIXED(".pll1", CLK_PLL1, CLK_EXTAL, 1, 40),
	DEF_FIXED(".pll2", CLK_PLL2, CLK_EXTAL, 1, 32),
	DEF_FIXED(".pll4", CLK_PLL4, CLK_EXTAL, 1, 96),

	DEF_MUX(".sel_clk_pll0", CLK_SEL_CLK_PLL0, SEL_PLL,
		sel_clk_pll0, ARRAY_SIZE(sel_clk_pll0), CLK_MUX_READ_ONLY),
	DEF_MUX(".sel_clk_pll1", CLK_SEL_CLK_PLL1, SEL_PLL,
		sel_clk_pll1, ARRAY_SIZE(sel_clk_pll1), CLK_MUX_READ_ONLY),
	DEF_MUX(".sel_clk_pll2", CLK_SEL_CLK_PLL2, SEL_PLL,
		sel_clk_pll2, ARRAY_SIZE(sel_clk_pll2), CLK_MUX_READ_ONLY),
	DEF_MUX(".sel_clk_pll4", CLK_SEL_CLK_PLL4, SEL_PLL,
		sel_clk_pll4, ARRAY_SIZE(sel_clk_pll4), CLK_MUX_READ_ONLY),

	DEF_FIXED(".pll4d1", CLK_PLL4D1, CLK_SEL_CLK_PLL4, 1, 1),
	DEF_DIV(".sci0async", CLK_SCI0ASYNC, CLK_PLL4D1, DIVSCI0ASYNC,
		dtable_24_25_30_32),
	DEF_DIV(".sci1async", CLK_SCI1ASYNC, CLK_PLL4D1, DIVSCI1ASYNC,
		dtable_24_25_30_32),
	DEF_DIV(".sci2async", CLK_SCI2ASYNC, CLK_PLL4D1, DIVSCI2ASYNC,
		dtable_24_25_30_32),
	DEF_DIV(".sci3async", CLK_SCI3ASYNC, CLK_PLL4D1, DIVSCI3ASYNC,
		dtable_24_25_30_32),
	DEF_DIV(".sci4async", CLK_SCI4ASYNC, CLK_PLL4D1, DIVSCI4ASYNC,
		dtable_24_25_30_32),
	DEF_DIV(".sci5async", CLK_SCI5ASYNC, CLK_PLL4D1, DIVSCI5ASYNC,
		dtable_24_25_30_32),

	/* Core output clk */
	DEF_DIV("CA55C0", R9A09G077_CLK_CA55C0, CLK_SEL_CLK_PLL0, DIVCA55C0,
		dtable_1_2),
	DEF_DIV("CA55C1", R9A09G077_CLK_CA55C1, CLK_SEL_CLK_PLL0, DIVCA55C1,
		dtable_1_2),
	DEF_DIV("CA55C2", R9A09G077_CLK_CA55C2, CLK_SEL_CLK_PLL0, DIVCA55C2,
		dtable_1_2),
	DEF_DIV("CA55C3", R9A09G077_CLK_CA55C3, CLK_SEL_CLK_PLL0, DIVCA55C3,
		dtable_1_2),
	DEF_DIV("CA55S", R9A09G077_CLK_CA55S, CLK_SEL_CLK_PLL0, DIVCA55S,
		dtable_1_2),
	DEF_FIXED("PCLKGPTL", R9A09G077_CLK_PCLKGPTL, CLK_SEL_CLK_PLL1, 2, 1),
	DEF_FIXED("PCLKH", R9A09G077_CLK_PCLKH, CLK_SEL_CLK_PLL1, 4, 1),
	DEF_FIXED("PCLKM", R9A09G077_CLK_PCLKM, CLK_SEL_CLK_PLL1, 8, 1),
	DEF_FIXED("PCLKL", R9A09G077_CLK_PCLKL, CLK_SEL_CLK_PLL1, 16, 1),
	DEF_FIXED("PCLKAH", R9A09G077_CLK_PCLKAH, CLK_PLL4D1, 6, 1),
	DEF_FIXED("PCLKAM", R9A09G077_CLK_PCLKAM, CLK_PLL4D1, 12, 1),
	DEF_FIXED("SDHI_CLKHS", R9A09G077_SDHI_CLKHS, CLK_SEL_CLK_PLL2, 1, 1),
	DEF_FIXED("USB_CLK", R9A09G077_USB_CLK, CLK_PLL4D1, 48, 1),
	DEF_FIXED("ETCLKA", R9A09G077_ETCLKA, CLK_SEL_CLK_PLL1, 5, 1),
	DEF_FIXED("ETCLKB", R9A09G077_ETCLKB, CLK_SEL_CLK_PLL1, 8, 1),
	DEF_FIXED("ETCLKC", R9A09G077_ETCLKC, CLK_SEL_CLK_PLL1, 10, 1),
	DEF_FIXED("ETCLKD", R9A09G077_ETCLKD, CLK_SEL_CLK_PLL1, 20, 1),
	DEF_FIXED("ETCLKE", R9A09G077_ETCLKE, CLK_SEL_CLK_PLL1, 40, 1),
};

static const struct mssr_mod_clk r9a09g077_mod_clks[] __initconst = {
	DEF_MOD("sci0fck", 8, CLK_SCI0ASYNC),
	DEF_MOD("sci1fck", 9, CLK_SCI1ASYNC),
	DEF_MOD("sci2fck", 10, CLK_SCI2ASYNC),
	DEF_MOD("sci3fck", 11, CLK_SCI3ASYNC),
	DEF_MOD("sci4fck", 12, CLK_SCI4ASYNC),
	DEF_MOD("iic0", 100, R9A09G077_CLK_PCLKL),
	DEF_MOD("iic1", 101, R9A09G077_CLK_PCLKL),
	DEF_MOD("gmac0", 400, R9A09G077_CLK_PCLKM),
	DEF_MOD("ethsw", 401, R9A09G077_CLK_PCLKM),
	DEF_MOD("ethss", 403, R9A09G077_CLK_PCLKM),
	DEF_MOD("usb", 408, R9A09G077_CLK_PCLKAM),
	DEF_MOD("gmac1", 416, R9A09G077_CLK_PCLKAM),
	DEF_MOD("gmac2", 417, R9A09G077_CLK_PCLKAM),
	DEF_MOD("sci5fck", 600, CLK_SCI5ASYNC),
	DEF_MOD("iic2", 601, R9A09G077_CLK_PCLKL),
	DEF_MOD("sdhi0", 1212, R9A09G077_CLK_PCLKAM),
	DEF_MOD("sdhi1", 1213, R9A09G077_CLK_PCLKAM),
};

static struct clk * __init
r9a09g077_cpg_div_clk_register(struct device *dev,
			       const struct cpg_core_clk *core,
			       void __iomem *addr, struct cpg_mssr_pub *pub)
{
	const struct clk *parent;
	const char *parent_name;
	struct clk_hw *clk_hw;

	parent = pub->clks[core->parent];
	if (IS_ERR(parent))
		return ERR_CAST(parent);

	parent_name = __clk_get_name(parent);

	if (core->dtable)
		clk_hw = clk_hw_register_divider_table(dev, core->name,
						       parent_name, 0,
						       addr,
						       GET_SHIFT(core->conf),
						       GET_WIDTH(core->conf),
						       core->flag,
						       core->dtable,
						       &pub->rmw_lock);
	else
		clk_hw = clk_hw_register_divider(dev, core->name,
						 parent_name, 0,
						 addr,
						 GET_SHIFT(core->conf),
						 GET_WIDTH(core->conf),
						 core->flag, &pub->rmw_lock);

	if (IS_ERR(clk_hw))
		return ERR_CAST(clk_hw);

	return clk_hw->clk;

}

static struct clk * __init
r9a09g077_cpg_mux_clk_register(struct device *dev,
			       const struct cpg_core_clk *core,
			       void __iomem *addr, struct cpg_mssr_pub *pub)
{
	struct clk_hw *clk_hw;

	clk_hw = devm_clk_hw_register_mux(dev, core->name,
					  core->parent_names, core->num_parents,
					  core->flag,
					  addr,
					  GET_SHIFT(core->conf),
					  GET_WIDTH(core->conf),
					  core->mux_flags, &pub->rmw_lock);
	if (IS_ERR(clk_hw))
		return ERR_CAST(clk_hw);

	return clk_hw->clk;
}

static struct clk * __init
r9a09g077_cpg_clk_register(struct device *dev, const struct cpg_core_clk *core,
			   const struct cpg_mssr_info *info,
			   struct cpg_mssr_pub *pub)
{
	u32 offset = GET_REG_OFFSET(core->conf);
	void __iomem *base = RZT2H_REG_BLOCK(offset) ? pub->base1 : pub->base0;
	void __iomem *addr = base + RZT2H_REG_OFFSET(offset);

	switch (core->type) {
	case CLK_TYPE_RZT2H_DIV:
		return r9a09g077_cpg_div_clk_register(dev, core, addr, pub);
	case CLK_TYPE_RZT2H_MUX:
		return r9a09g077_cpg_mux_clk_register(dev, core, addr, pub);
	default:
		return ERR_PTR(-EINVAL);
	}
}

const struct cpg_mssr_info r9a09g077_cpg_mssr_info = {
	/* Core Clocks */
	.core_clks = r9a09g077_core_clks,
	.num_core_clks = ARRAY_SIZE(r9a09g077_core_clks),
	.last_dt_core_clk = LAST_DT_CORE_CLK,
	.num_total_core_clks = MOD_CLK_BASE,

	/* Module Clocks */
	.mod_clks = r9a09g077_mod_clks,
	.num_mod_clks = ARRAY_SIZE(r9a09g077_mod_clks),
	.num_hw_mod_clks = 14 * 32,

	.reg_layout = CLK_REG_LAYOUT_RZ_T2H,
	.cpg_clk_register = r9a09g077_cpg_clk_register,
};
