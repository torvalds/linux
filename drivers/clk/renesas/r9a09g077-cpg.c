// SPDX-License-Identifier: GPL-2.0
/*
 * r9a09g077 Clock Pulse Generator / Module Standby and Software Reset
 *
 * Copyright (C) 2025 Renesas Electronics Corp.
 *
 */

#include <linux/bitfield.h>
#include <linux/clk-provider.h>
#include <linux/clk/renesas.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/units.h>

#include <dt-bindings/clock/renesas,r9a09g077-cpg-mssr.h>
#include <dt-bindings/clock/renesas,r9a09g087-cpg-mssr.h>
#include "renesas-cpg-mssr.h"

MODULE_IMPORT_NS("RZV2H_CPG");

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

#define FSELXSPI0	CONF_PACK(SCKCR, 0, 3)
#define FSELXSPI1	CONF_PACK(SCKCR, 8, 3)
#define DIVSEL_XSPI0	CONF_PACK(SCKCR, 6, 1)
#define DIVSEL_XSPI1	CONF_PACK(SCKCR, 14, 1)
#define FSELCANFD	CONF_PACK(SCKCR, 20, 1)
#define SEL_PLL		CONF_PACK(SCKCR, 22, 1)

#define DIVCA55C0	CONF_PACK(SCKCR2, 8, 1)
#define DIVCA55C1	CONF_PACK(SCKCR2, 9, 1)
#define DIVCA55C2	CONF_PACK(SCKCR2, 10, 1)
#define DIVCA55C3	CONF_PACK(SCKCR2, 11, 1)
#define DIVCA55S	CONF_PACK(SCKCR2, 12, 1)
#define DIVSPI3ASYNC	CONF_PACK(SCKCR2, 16, 2)
#define DIVSCI5ASYNC	CONF_PACK(SCKCR2, 18, 2)

#define DIVSPI0ASYNC	CONF_PACK(SCKCR3, 0, 2)
#define DIVSPI1ASYNC	CONF_PACK(SCKCR3, 2, 2)
#define DIVSPI2ASYNC	CONF_PACK(SCKCR3, 4, 2)
#define DIVSCI0ASYNC	CONF_PACK(SCKCR3, 6, 2)
#define DIVSCI1ASYNC	CONF_PACK(SCKCR3, 8, 2)
#define DIVSCI2ASYNC	CONF_PACK(SCKCR3, 10, 2)
#define DIVSCI3ASYNC	CONF_PACK(SCKCR3, 12, 2)
#define DIVSCI4ASYNC	CONF_PACK(SCKCR3, 14, 2)
#define LCDCDIVSEL	CONF_PACK(SCKCR3, 20, 4)

#define PLL3EN		FIELD_PREP_CONST(OFFSET_MASK, (0xc0))

#define CPG_PLL_EN_EN		BIT(0)
#define CPG_PLL3_VCO_CTR0(x)	((x) + 0x4)
#define CPG_PLL3_VCO_CTR0_PDIV	GENMASK(21, 16)
#define CPG_PLL3_VCO_CTR0_MDIV	GENMASK(9, 0)
#define CPG_PLL3_VCO_CTR1(x)	((x) + 0x8)
#define CPG_PLL3_VCO_CTR1_KDIV	GENMASK(31, 16)
#define CPG_PLL3_VCO_CTR1_SDIV	GENMASK(2, 0)
#define CPG_PLL_MON(x)		((x) - 0x10)
#define CPG_PLL_MON_LOCK	BIT(0)

enum rzt2h_clk_types {
	CLK_TYPE_RZT2H_DIV = CLK_TYPE_CUSTOM,	/* Clock with divider */
	CLK_TYPE_RZT2H_MUX,			/* Clock with clock source selector */
	CLK_TYPE_RZT2H_FSELXSPI,		/* Clock with FSELXSPIn source selector */
	CLK_TYPE_RZT2H_PLL3,			/* PLL3 Clock */
	CLK_TYPE_RZT2H_LCDCDIV,			/* LCDC divider clock */
};

#define DEF_DIV(_name, _id, _parent, _conf, _dtable) \
	DEF_TYPE(_name, _id, CLK_TYPE_RZT2H_DIV, .conf = _conf, \
		 .parent = _parent, .dtable = _dtable, .flag = 0)
#define DEF_MUX(_name, _id, _conf, _parent_names, _num_parents, _mux_flags) \
	DEF_TYPE(_name, _id, CLK_TYPE_RZT2H_MUX, .conf = _conf, \
		 .parent_names = _parent_names, .num_parents = _num_parents, \
		 .flag = CLK_SET_RATE_PARENT, .mux_flags = _mux_flags)
#define DEF_DIV_FSELXSPI(_name, _id, _parent, _conf, _dtable) \
	DEF_TYPE(_name, _id, CLK_TYPE_RZT2H_FSELXSPI, .conf = _conf, \
		 .parent = _parent, .dtable = _dtable, .flag = 0)
#define DEF_PLL3(_name, _id, _parent, _conf) \
	DEF_TYPE(_name, _id, CLK_TYPE_RZT2H_PLL3, .conf = _conf, \
		 .parent = _parent)
#define DEF_DIV_LCDC(_name, _id, _parent, _conf, _dtable) \
	DEF_TYPE(_name, _id, CLK_TYPE_RZT2H_LCDCDIV, .conf = _conf, \
		 .parent = _parent, .dtable = _dtable, .flag = CLK_SET_RATE_PARENT)

struct pll_clk {
	void __iomem *reg;
	const struct rzv2h_pll_limits *limits;
	struct device *dev;
	struct rzv2h_pll_pars pll_parameters;
	struct clk_hw hw;
	unsigned long cur_rate;
};

#define to_pll(_hw)	container_of(_hw, struct pll_clk, hw)

struct r9a09g077_lcdc_div_clk {
	const struct clk_div_table *dtable;
	void __iomem *reg;
	struct device *dev;
	struct clk_hw hw;
	u32 conf;
	u8 divider;
};

#define to_lcdc_div_clk(_hw) \
	container_of(_hw, struct r9a09g077_lcdc_div_clk, hw)

#define RZT2H_MAX_LCDC_DIV_TABLES	16

static const struct rzv2h_pll_limits r9a09g077_cpg_pll3_limits = {
	.input_fref = 48 * MEGA,
	.fout = { .min = 25 * MEGA, .max = 430 * MEGA },
	.fvco = { .min = 1600 * MEGA, .max = 3200 * MEGA },
	.m = { .min = 0x40, .max = 0x3ff },
	.p = { .min = 0x2, .max = 0x8 },
	.s = { .min = 0x0, .max = 0x6 },
	.k = { .min = -32768, .max = 32767 },
};

enum clk_ids {
	/* Core Clock Outputs exported to DT */
	LAST_DT_CORE_CLK = R9A09G077_PCLKRTC,

	/* External Input Clocks */
	CLK_EXTAL,

	/* Internal Core Clocks */
	CLK_LOCO,
	CLK_PLL0,
	CLK_PLL1,
	CLK_PLL2,
	CLK_PLL3,
	CLK_PLL4,
	CLK_SEL_CLK_PLL0,
	CLK_SEL_CLK_PLL1,
	CLK_SEL_CLK_PLL2,
	CLK_SEL_CLK_PLL3,
	CLK_SEL_CLK_PLL4,
	CLK_PLL4D1,
	CLK_PLL4D1_DIV3,
	CLK_PLL4D1_DIV4,
	CLK_PLL4D3,
	CLK_PLL4D3_DIV10,
	CLK_PLL4D3_DIV20,
	CLK_PLL4D50,
	CLK_SCI0ASYNC,
	CLK_SCI1ASYNC,
	CLK_SCI2ASYNC,
	CLK_SCI3ASYNC,
	CLK_SCI4ASYNC,
	CLK_SCI5ASYNC,
	CLK_SPI0ASYNC,
	CLK_SPI1ASYNC,
	CLK_SPI2ASYNC,
	CLK_SPI3ASYNC,
	CLK_DIVSELXSPI0_SCKCR,
	CLK_DIVSELXSPI1_SCKCR,
	CLK_LCDDIVSEL,

	/* Module Clocks */
	MOD_CLK_BASE,
};

static const struct clk_div_table dtable_1_2[] = {
	{0, 2},
	{1, 1},
	{0, 0},
};

static const struct clk_div_table dtable_2_32[] = {
	{0, 2},
	{1, 4},
	{2, 6},
	{3, 8},
	{4, 10},
	{5, 12},
	{6, 14},
	{7, 16},
	{8, 18},
	{9, 20},
	{10, 22},
	{11, 24},
	{12, 26},
	{13, 28},
	{14, 30},
	{15, 32},
	{0, 0},
};

static const struct clk_div_table dtable_6_8_16_32_64[] = {
	{6, 64},
	{5, 32},
	{4, 16},
	{3, 8},
	{2, 6},
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
static const char * const sel_clk_pll3[] = { ".loco", ".pll3" };
static const char * const sel_clk_pll4[] = { ".loco", ".pll4" };
static const char * const sel_clk_pll4d1_div3_div4[] = { ".pll4d1_div3", ".pll4d1_div4" };
static const char * const sel_clk_pll4d3_div10_div20[] = { ".pll4d3_div10", ".pll4d3_div20" };

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
	DEF_MUX(".sel_clk_pll3", CLK_SEL_CLK_PLL3, SEL_PLL,
		sel_clk_pll3, ARRAY_SIZE(sel_clk_pll3), CLK_MUX_READ_ONLY),
	DEF_MUX(".sel_clk_pll4", CLK_SEL_CLK_PLL4, SEL_PLL,
		sel_clk_pll4, ARRAY_SIZE(sel_clk_pll4), CLK_MUX_READ_ONLY),

	DEF_FIXED(".pll4d1", CLK_PLL4D1, CLK_SEL_CLK_PLL4, 1, 1),
	DEF_FIXED(".pll4d50", CLK_PLL4D50, CLK_SEL_CLK_PLL4, 50, 1),
	DEF_PLL3(".pll3", CLK_PLL3, CLK_PLL4D50, PLL3EN),
	DEF_FIXED(".pll4d1_div3", CLK_PLL4D1_DIV3, CLK_PLL4D1, 3, 1),
	DEF_FIXED(".pll4d1_div4", CLK_PLL4D1_DIV4, CLK_PLL4D1, 4, 1),
	DEF_FIXED(".pll4d3", CLK_PLL4D3, CLK_SEL_CLK_PLL4, 3, 1),
	DEF_FIXED(".pll4d3_div10", CLK_PLL4D3_DIV10, CLK_PLL4D3, 10, 1),
	DEF_FIXED(".pll4d3_div20", CLK_PLL4D3_DIV20, CLK_PLL4D3, 20, 1),

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

	DEF_DIV(".spi0async", CLK_SPI0ASYNC, CLK_PLL4D1, DIVSPI0ASYNC,
		dtable_24_25_30_32),
	DEF_DIV(".spi1async", CLK_SPI1ASYNC, CLK_PLL4D1, DIVSPI1ASYNC,
		dtable_24_25_30_32),
	DEF_DIV(".spi2async", CLK_SPI2ASYNC, CLK_PLL4D1, DIVSPI2ASYNC,
		dtable_24_25_30_32),
	DEF_DIV(".spi3async", CLK_SPI3ASYNC, CLK_PLL4D1, DIVSPI3ASYNC,
		dtable_24_25_30_32),

	DEF_MUX(".divselxspi0", CLK_DIVSELXSPI0_SCKCR, DIVSEL_XSPI0,
		sel_clk_pll4d1_div3_div4,
		ARRAY_SIZE(sel_clk_pll4d1_div3_div4), 0),
	DEF_MUX(".divselxspi1", CLK_DIVSELXSPI1_SCKCR, DIVSEL_XSPI1,
		sel_clk_pll4d1_div3_div4,
		ARRAY_SIZE(sel_clk_pll4d1_div3_div4), 0),

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
	DEF_FIXED("PCLKAL", R9A09G077_CLK_PCLKAL, CLK_PLL4D1, 24, 1),
	DEF_FIXED("SDHI_CLKHS", R9A09G077_SDHI_CLKHS, CLK_SEL_CLK_PLL2, 1, 1),
	DEF_FIXED("USB_CLK", R9A09G077_USB_CLK, CLK_PLL4D1, 48, 1),
	DEF_FIXED("ETCLKA", R9A09G077_ETCLKA, CLK_SEL_CLK_PLL1, 5, 1),
	DEF_FIXED("ETCLKB", R9A09G077_ETCLKB, CLK_SEL_CLK_PLL1, 8, 1),
	DEF_FIXED("ETCLKC", R9A09G077_ETCLKC, CLK_SEL_CLK_PLL1, 10, 1),
	DEF_FIXED("ETCLKD", R9A09G077_ETCLKD, CLK_SEL_CLK_PLL1, 20, 1),
	DEF_FIXED("ETCLKE", R9A09G077_ETCLKE, CLK_SEL_CLK_PLL1, 40, 1),
	DEF_DIV_FSELXSPI("XSPI_CLK0", R9A09G077_XSPI_CLK0, CLK_DIVSELXSPI0_SCKCR,
			 FSELXSPI0, dtable_6_8_16_32_64),
	DEF_DIV_FSELXSPI("XSPI_CLK1", R9A09G077_XSPI_CLK1, CLK_DIVSELXSPI1_SCKCR,
			 FSELXSPI1, dtable_6_8_16_32_64),
	DEF_MUX("PCLKCAN", R9A09G077_PCLKCAN, FSELCANFD,
		sel_clk_pll4d3_div10_div20, ARRAY_SIZE(sel_clk_pll4d3_div10_div20), 0),
	DEF_DIV_LCDC("LCDC_CLKD", R9A09G077_LCDC_CLKD, CLK_SEL_CLK_PLL3, LCDCDIVSEL,
		     dtable_2_32),
	DEF_FIXED("PCLKRTC", R9A09G077_PCLKRTC, CLK_EXTAL, 128, 1),
};

static const struct mssr_mod_clk r9a09g077_mod_clks[] __initconst = {
	DEF_MOD("xspi0", 4, R9A09G077_CLK_PCLKH),
	DEF_MOD("xspi1", 5, R9A09G077_CLK_PCLKH),
	DEF_MOD("sci0fck", 8, CLK_SCI0ASYNC),
	DEF_MOD("sci1fck", 9, CLK_SCI1ASYNC),
	DEF_MOD("sci2fck", 10, CLK_SCI2ASYNC),
	DEF_MOD("sci3fck", 11, CLK_SCI3ASYNC),
	DEF_MOD("sci4fck", 12, CLK_SCI4ASYNC),
	DEF_MOD("iic0", 100, R9A09G077_CLK_PCLKL),
	DEF_MOD("iic1", 101, R9A09G077_CLK_PCLKL),
	DEF_MOD("spi0", 104, CLK_SPI0ASYNC),
	DEF_MOD("spi1", 105, CLK_SPI1ASYNC),
	DEF_MOD("spi2", 106, CLK_SPI2ASYNC),
	DEF_MOD("mtu3", 200, R9A09G077_CLK_PCLKH),
	DEF_MOD("adc0", 206, R9A09G077_CLK_PCLKH),
	DEF_MOD("adc1", 207, R9A09G077_CLK_PCLKH),
	DEF_MOD("adc2", 225, R9A09G077_CLK_PCLKM),
	DEF_MOD("tsu", 307, R9A09G077_CLK_PCLKL),
	DEF_MOD("canfd", 310, R9A09G077_CLK_PCLKM),
	DEF_MOD("gmac0", 400, R9A09G077_CLK_PCLKM),
	DEF_MOD("ethsw", 401, R9A09G077_CLK_PCLKM),
	DEF_MOD("ethss", 403, R9A09G077_CLK_PCLKM),
	DEF_MOD("usb", 408, R9A09G077_CLK_PCLKAM),
	DEF_MOD("gmac1", 416, R9A09G077_CLK_PCLKAM),
	DEF_MOD("gmac2", 417, R9A09G077_CLK_PCLKAM),
	DEF_MOD("sci5fck", 600, CLK_SCI5ASYNC),
	DEF_MOD("iic2", 601, R9A09G077_CLK_PCLKL),
	DEF_MOD("spi3", 602, CLK_SPI3ASYNC),
	DEF_MOD("rtc", 605, R9A09G077_CLK_PCLKL),
	DEF_MOD("lcdc", 1204, R9A09G077_CLK_PCLKAL),
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
		clk_hw = devm_clk_hw_register_divider_table(dev, core->name,
							    parent_name,
							    CLK_SET_RATE_PARENT,
							    addr,
							    GET_SHIFT(core->conf),
							    GET_WIDTH(core->conf),
							    core->flag,
							    core->dtable,
							    &pub->rmw_lock);
	else
		clk_hw = devm_clk_hw_register_divider(dev, core->name,
						      parent_name,
						      CLK_SET_RATE_PARENT,
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

static unsigned int r9a09g077_cpg_fselxspi_get_divider(struct clk_hw *hw, unsigned long rate,
						       unsigned int num_parents)
{
	struct clk_fixed_factor *ff;
	struct clk_hw *parent_hw;
	unsigned long best_rate;
	unsigned int i;

	for (i = 0; i < num_parents; i++) {
		parent_hw = clk_hw_get_parent_by_index(hw, i);
		best_rate = clk_hw_round_rate(parent_hw, rate);

		if (best_rate == rate) {
			ff = to_clk_fixed_factor(parent_hw);
			return ff->div;
		}
	}

	/* No parent could provide the exact rate - this should not happen */
	return 0;
}

static int r9a09g077_cpg_fselxspi_determine_rate(struct clk_hw *hw,
						 struct clk_rate_request *req)
{
	struct clk_divider *divider = to_clk_divider(hw);
	unsigned long parent_rate, best = 0, now;
	const struct clk_div_table *clkt;
	unsigned long rate = req->rate;
	unsigned int num_parents;
	unsigned int divselxspi;
	unsigned int div = 0;

	if (!rate)
		rate = 1;

	/* Get the number of parents for FSELXSPIn */
	num_parents = clk_hw_get_num_parents(req->best_parent_hw);

	for (clkt = divider->table; clkt->div; clkt++) {
		parent_rate = clk_hw_round_rate(req->best_parent_hw, rate * clkt->div);
		/* Skip if parent can't provide any valid rate */
		if (!parent_rate)
			continue;

		/* Determine which DIVSELXSPIn divider (3 or 4) provides this parent_rate */
		divselxspi = r9a09g077_cpg_fselxspi_get_divider(req->best_parent_hw, parent_rate,
								num_parents);
		if (!divselxspi)
			continue;

		/*
		 * DIVSELXSPIx supports 800MHz and 600MHz operation.
		 * When divselxspi is 4 (600MHz operation), only FSELXSPIn dividers of 8 and 16
		 * are supported. Otherwise, when divselxspi is 3 (800MHz operation),
		 * dividers of 6, 8, 16, 32, and 64 are supported. This check ensures that
		 * FSELXSPIx is set correctly based on hardware limitations.
		 */
		if (divselxspi == 4 && (clkt->div != 8 && clkt->div != 16))
			continue;

		now = DIV_ROUND_UP_ULL(parent_rate, clkt->div);
		if (abs(rate - now) < abs(rate - best)) {
			div = clkt->div;
			best = now;
			req->best_parent_rate = parent_rate;
		}
	}

	if (!div) {
		req->best_parent_rate = clk_hw_round_rate(req->best_parent_hw, 1);
		divselxspi = r9a09g077_cpg_fselxspi_get_divider(req->best_parent_hw,
								req->best_parent_rate,
								num_parents);
		/* default to divider 3 which will result DIVSELXSPIn = 800 MHz */
		if (!divselxspi)
			divselxspi = 3;

		/*
		 * Use the maximum divider based on the parent clock rate:
		 *  - 64 when DIVSELXSPIx is 800 MHz (divider = 3)
		 *  - 16 when DIVSELXSPIx is 600 MHz (divider = 4)
		 */
		div = divselxspi == 3 ? 64 : 16;
	}

	req->rate = DIV_ROUND_UP_ULL(req->best_parent_rate, div);

	return 0;
}

static struct clk * __init
r9a09g077_cpg_fselxspi_div_clk_register(struct device *dev,
					const struct cpg_core_clk *core,
					void __iomem *addr,
					struct cpg_mssr_pub *pub)
{
	static struct clk_ops *xspi_div_ops;
	struct clk_init_data init = {};
	const struct clk *parent;
	const char *parent_name;
	struct clk_divider *div;
	struct clk_hw *hw;
	int ret;

	parent = pub->clks[core->parent];
	if (IS_ERR(parent))
		return ERR_CAST(parent);

	div = devm_kzalloc(dev, sizeof(*div), GFP_KERNEL);
	if (!div)
		return ERR_PTR(-ENOMEM);

	if (!xspi_div_ops) {
		xspi_div_ops = devm_kzalloc(dev, sizeof(*xspi_div_ops), GFP_KERNEL);
		if (!xspi_div_ops)
			return  ERR_PTR(-ENOMEM);
		memcpy(xspi_div_ops, &clk_divider_ops,
		       sizeof(const struct clk_ops));
		xspi_div_ops->determine_rate = r9a09g077_cpg_fselxspi_determine_rate;
	}

	parent_name = __clk_get_name(parent);
	init.name = core->name;
	init.ops = xspi_div_ops;
	init.flags = CLK_SET_RATE_PARENT;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	div->reg = addr;
	div->shift = GET_SHIFT(core->conf);
	div->width = GET_WIDTH(core->conf);
	div->flags = core->flag;
	div->lock = &pub->rmw_lock;
	div->hw.init = &init;
	div->table = core->dtable;

	hw = &div->hw;
	ret = devm_clk_hw_register(dev, hw);
	if (ret)
		return ERR_PTR(ret);

	return hw->clk;
}

static unsigned long r9a09g077_cpg_pll3_clk_recalc_rate(struct clk_hw *hw,
							unsigned long parent_rate)
{
	struct pll_clk *pll_clk = to_pll(hw);
	u32 ctr0, ctr1;
	u8 pdiv, sdiv;
	u64 rate;
	u16 mdiv;
	s16 kdiv;

	ctr0 = readl(CPG_PLL3_VCO_CTR0(pll_clk->reg));
	ctr1 = readl(CPG_PLL3_VCO_CTR1(pll_clk->reg));

	pdiv = FIELD_GET(CPG_PLL3_VCO_CTR0_PDIV, ctr0);
	mdiv = FIELD_GET(CPG_PLL3_VCO_CTR0_MDIV, ctr0);
	kdiv = (s16)FIELD_GET(CPG_PLL3_VCO_CTR1_KDIV, ctr1);
	sdiv = FIELD_GET(CPG_PLL3_VCO_CTR1_SDIV, ctr1);

	rate = mul_u64_u32_shr(parent_rate, (mdiv << 16) + kdiv, 16 + sdiv);

	return DIV_ROUND_CLOSEST_ULL(rate, pdiv);
}

static int r9a09g077_cpg_pll3_determine_rate(struct clk_hw *hw,
					     struct clk_rate_request *req)
{
	struct pll_clk *pll_clk = to_pll(hw);
	u64 rate_millihz;

	if (req->rate == pll_clk->cur_rate)
		return 0;

	rate_millihz = mul_u32_u32(req->rate, MILLI);
	if (!rzv2h_cpg_get_pll_pars(pll_clk->limits, &pll_clk->pll_parameters,
				    rate_millihz)) {
		dev_dbg(pll_clk->dev,
			"failed to determine rate for req->rate: %lu\n",
			req->rate);
		return -EINVAL;
	}
	req->rate = DIV_ROUND_CLOSEST_ULL(pll_clk->pll_parameters.freq_millihz, MILLI);
	pll_clk->cur_rate = req->rate;

	return 0;
}

static int r9a09g077_cpg_pll3_set_rate(struct clk_hw *hw, unsigned long rate,
				       unsigned long parent_rate)
{
	struct pll_clk *pll_clk = to_pll(hw);
	struct rzv2h_pll_pars *params = &pll_clk->pll_parameters;
	void __iomem *offset = pll_clk->reg;
	u32 val;
	int ret;

	/* Put PLL into standby mode */
	writel(0, offset);
	ret = readl_poll_timeout_atomic(CPG_PLL_MON(offset),
					val, !(val & CPG_PLL_MON_LOCK),
					100, 2000);
	if (ret) {
		dev_err(pll_clk->dev, "Failed to put PLL into standby mode");
		return ret;
	}

	/* Output clock setting 1 */
	val = readl(CPG_PLL3_VCO_CTR0(offset));
	FIELD_MODIFY(CPG_PLL3_VCO_CTR0_MDIV, &val, params->m);
	FIELD_MODIFY(CPG_PLL3_VCO_CTR0_PDIV, &val, params->p);
	writel(val, CPG_PLL3_VCO_CTR0(offset));

	/* Output clock setting 2 */
	val = readl(CPG_PLL3_VCO_CTR1(offset));
	FIELD_MODIFY(CPG_PLL3_VCO_CTR1_KDIV, &val, params->k);
	FIELD_MODIFY(CPG_PLL3_VCO_CTR1_SDIV, &val, params->s);
	writel(val, CPG_PLL3_VCO_CTR1(offset));

	writel(CPG_PLL_EN_EN, offset);

	/* PLL normal mode transition, output clock stability check */
	ret = readl_poll_timeout_atomic(CPG_PLL_MON(offset),
					val, (val & CPG_PLL_MON_LOCK),
					100, 2000);
	if (ret) {
		writel(0, offset);
		dev_err(pll_clk->dev, "Failed to put PLL into normal mode");
		return ret;
	}

	return 0;
}

static const struct clk_ops r9a09g077_cpg_pll3_ops = {
	.recalc_rate = r9a09g077_cpg_pll3_clk_recalc_rate,
	.determine_rate = r9a09g077_cpg_pll3_determine_rate,
	.set_rate = r9a09g077_cpg_pll3_set_rate,
};

static struct clk * __init
r9a09g077_cpg_pll3_clk_register(struct device *dev,
				const struct cpg_core_clk *core,
				void __iomem *addr,
				struct cpg_mssr_pub *pub,
				const struct rzv2h_pll_limits *limits)
{
	struct clk_init_data init = {};
	const struct clk *parent;
	const char *parent_name;
	struct pll_clk *pll_clk;
	int ret;

	parent = pub->clks[core->parent];
	if (IS_ERR(parent))
		return ERR_CAST(parent);

	pll_clk = devm_kzalloc(dev, sizeof(*pll_clk), GFP_KERNEL);
	if (!pll_clk)
		return ERR_PTR(-ENOMEM);

	parent_name = __clk_get_name(parent);
	init.name = core->name;
	init.ops = &r9a09g077_cpg_pll3_ops;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	pll_clk->dev = dev;
	pll_clk->hw.init = &init;
	pll_clk->reg = addr;
	pll_clk->limits = limits;

	ret = devm_clk_hw_register(dev, &pll_clk->hw);
	if (ret)
		return ERR_PTR(ret);

	return pll_clk->hw.clk;
}

static int r9a09g077_cpg_lcdc_div_determine_rate(struct clk_hw *hw,
						 struct clk_rate_request *req)
{
	struct r9a09g077_lcdc_div_clk *dsi_div = to_lcdc_div_clk(hw);
	struct clk_hw *mux_hw = clk_hw_get_parent(hw);
	u8 table[RZT2H_MAX_LCDC_DIV_TABLES] = { 0 };
	struct rzv2h_pll_div_pars dsi_params;
	const struct clk_div_table *div;
	struct pll_clk *pll_clk;
	unsigned int i = 0;
	u64 freq_millihz;

	/* index 1 is always .pll3 in sel_clk_pll3[] */
	pll_clk = to_pll(clk_hw_get_parent_by_index(mux_hw, 1));

	for (div = dsi_div->dtable; div->div; div++) {
		if (i >= RZT2H_MAX_LCDC_DIV_TABLES)
			return -EINVAL;
		table[i++] = div->div;
	}

	freq_millihz = mul_u32_u32(req->rate, MILLI);

	if (!rzv2h_cpg_get_pll_divs_pars(pll_clk->limits, &dsi_params, table,
					 i, freq_millihz)) {
		dev_err(dsi_div->dev,
			"LCDC divider failed to determine rate for req->rate: %lu\n",
			req->rate);
		return -EINVAL;
	}

	req->rate = DIV_ROUND_CLOSEST_ULL(dsi_params.div.freq_millihz, MILLI);
	req->best_parent_rate = req->rate * dsi_params.div.divider_value;
	dsi_div->divider = dsi_params.div.divider_value;
	pll_clk->cur_rate = req->best_parent_rate;
	pll_clk->pll_parameters = dsi_params.pll;

	return 0;
}

static int r9a09g077_cpg_lcdc_div_set_rate(struct clk_hw *hw,
					   unsigned long rate,
					   unsigned long parent_rate)
{
	struct r9a09g077_lcdc_div_clk *dsi_div = to_lcdc_div_clk(hw);
	const struct clk_div_table *clkt;
	bool divider_found = false;
	u32 val, shift;

	for (clkt = dsi_div->dtable; clkt->div; clkt++) {
		if (clkt->div == dsi_div->divider) {
			divider_found = true;
			break;
		}
	}

	if (!divider_found)
		return -EINVAL;

	shift = GET_SHIFT(dsi_div->conf);
	val = readl(dsi_div->reg);
	val &= ~(clk_div_mask(GET_WIDTH(dsi_div->conf)) << shift);
	val |= clkt->val << shift;
	writel(val, dsi_div->reg);

	return 0;
}

static unsigned long
r9a09g077_cpg_lcdc_div_recalc_rate(struct clk_hw *hw,
				   unsigned long parent_rate)
{
	struct r9a09g077_lcdc_div_clk *dsi_div = to_lcdc_div_clk(hw);
	u32 div;

	div = readl(dsi_div->reg);
	div >>= GET_SHIFT(dsi_div->conf);
	div &= clk_div_mask(GET_WIDTH(dsi_div->conf));
	div = dsi_div->dtable[div].div;

	return DIV_ROUND_CLOSEST_ULL(parent_rate, div);
}

static const struct clk_ops r9a09g077_cpg_lcdc_div_ops = {
	.recalc_rate = r9a09g077_cpg_lcdc_div_recalc_rate,
	.determine_rate = r9a09g077_cpg_lcdc_div_determine_rate,
	.set_rate = r9a09g077_cpg_lcdc_div_set_rate,
};

static struct clk * __init
r9a09g077_cpg_lcdc_div_clk_register(struct device *dev,
				    const struct cpg_core_clk *core,
				    void __iomem *addr,
				    struct cpg_mssr_pub *pub)
{
	struct r9a09g077_lcdc_div_clk *clk_hw_data;
	struct clk_init_data init = {};
	struct clk **clks = pub->clks;
	const struct clk *parent;
	const char *parent_name;
	struct clk_hw *hw;
	int ret;

	parent = clks[core->parent];
	if (IS_ERR(parent))
		return ERR_CAST(parent);

	clk_hw_data = devm_kzalloc(dev, sizeof(*clk_hw_data), GFP_KERNEL);
	if (!clk_hw_data)
		return ERR_PTR(-ENOMEM);

	clk_hw_data->dtable = core->dtable;
	clk_hw_data->reg = addr;
	clk_hw_data->conf = core->conf;
	clk_hw_data->dev = dev;
	clk_hw_data->divider = 32; /* Initialize divider for LCDC */

	parent_name = __clk_get_name(parent);
	init.name = core->name;
	init.ops = &r9a09g077_cpg_lcdc_div_ops;
	init.flags = core->flag;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	hw = &clk_hw_data->hw;
	hw->init = &init;
	ret = devm_clk_hw_register(dev, hw);
	if (ret)
		return ERR_PTR(ret);

	return hw->clk;
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
	case CLK_TYPE_RZT2H_FSELXSPI:
		return r9a09g077_cpg_fselxspi_div_clk_register(dev, core, addr, pub);
	case CLK_TYPE_RZT2H_PLL3:
		return r9a09g077_cpg_pll3_clk_register(dev, core, pub->base1 + offset,
						       pub, &r9a09g077_cpg_pll3_limits);
	case CLK_TYPE_RZT2H_LCDCDIV:
		return r9a09g077_cpg_lcdc_div_clk_register(dev, core, addr, pub);
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
