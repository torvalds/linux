// SPDX-License-Identifier: GPL-2.0
/*
 * r8a779a0 Clock Pulse Generator / Module Standby and Software Reset
 *
 * Copyright (C) 2020 Renesas Electronics Corp.
 *
 * Based on r8a7795-cpg-mssr.c
 *
 * Copyright (C) 2015 Glider bvba
 * Copyright (C) 2015 Renesas Electronics Corp.
 */

#include <linux/bug.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/soc/renesas/rcar-rst.h>

#include <dt-bindings/clock/r8a779a0-cpg-mssr.h>

#include "rcar-cpg-lib.h"
#include "renesas-cpg-mssr.h"

enum rcar_r8a779a0_clk_types {
	CLK_TYPE_R8A779A0_MAIN = CLK_TYPE_CUSTOM,
	CLK_TYPE_R8A779A0_PLL1,
	CLK_TYPE_R8A779A0_PLL2X_3X,	/* PLL[23][01] */
	CLK_TYPE_R8A779A0_PLL5,
	CLK_TYPE_R8A779A0_SD,
	CLK_TYPE_R8A779A0_MDSEL,	/* Select parent/divider using mode pin */
	CLK_TYPE_R8A779A0_OSC,	/* OSC EXTAL predivider and fixed divider */
};

struct rcar_r8a779a0_cpg_pll_config {
	u8 extal_div;
	u8 pll1_mult;
	u8 pll1_div;
	u8 pll5_mult;
	u8 pll5_div;
	u8 osc_prediv;
};

enum clk_ids {
	/* Core Clock Outputs exported to DT */
	LAST_DT_CORE_CLK = R8A779A0_CLK_OSC,

	/* External Input Clocks */
	CLK_EXTAL,
	CLK_EXTALR,

	/* Internal Core Clocks */
	CLK_MAIN,
	CLK_PLL1,
	CLK_PLL20,
	CLK_PLL21,
	CLK_PLL30,
	CLK_PLL31,
	CLK_PLL5,
	CLK_PLL1_DIV2,
	CLK_PLL20_DIV2,
	CLK_PLL21_DIV2,
	CLK_PLL30_DIV2,
	CLK_PLL31_DIV2,
	CLK_PLL5_DIV2,
	CLK_PLL5_DIV4,
	CLK_S1,
	CLK_S3,
	CLK_SDSRC,
	CLK_RPCSRC,
	CLK_OCO,

	/* Module Clocks */
	MOD_CLK_BASE
};

#define DEF_PLL(_name, _id, _offset)	\
	DEF_BASE(_name, _id, CLK_TYPE_R8A779A0_PLL2X_3X, CLK_MAIN, \
		 .offset = _offset)

#define DEF_SD(_name, _id, _parent, _offset)   \
	DEF_BASE(_name, _id, CLK_TYPE_R8A779A0_SD, _parent, .offset = _offset)

#define DEF_MDSEL(_name, _id, _md, _parent0, _div0, _parent1, _div1) \
	DEF_BASE(_name, _id, CLK_TYPE_R8A779A0_MDSEL,	\
		 (_parent0) << 16 | (_parent1),		\
		 .div = (_div0) << 16 | (_div1), .offset = _md)

#define DEF_OSC(_name, _id, _parent, _div)		\
	DEF_BASE(_name, _id, CLK_TYPE_R8A779A0_OSC, _parent, .div = _div)

static const struct cpg_core_clk r8a779a0_core_clks[] __initconst = {
	/* External Clock Inputs */
	DEF_INPUT("extal",  CLK_EXTAL),
	DEF_INPUT("extalr", CLK_EXTALR),

	/* Internal Core Clocks */
	DEF_BASE(".main", CLK_MAIN,	CLK_TYPE_R8A779A0_MAIN, CLK_EXTAL),
	DEF_BASE(".pll1", CLK_PLL1,	CLK_TYPE_R8A779A0_PLL1, CLK_MAIN),
	DEF_BASE(".pll5", CLK_PLL5,	CLK_TYPE_R8A779A0_PLL5, CLK_MAIN),
	DEF_PLL(".pll20", CLK_PLL20,	0x0834),
	DEF_PLL(".pll21", CLK_PLL21,	0x0838),
	DEF_PLL(".pll30", CLK_PLL30,	0x083c),
	DEF_PLL(".pll31", CLK_PLL31,	0x0840),

	DEF_FIXED(".pll1_div2",		CLK_PLL1_DIV2,	CLK_PLL1,	2, 1),
	DEF_FIXED(".pll20_div2",	CLK_PLL20_DIV2,	CLK_PLL20,	2, 1),
	DEF_FIXED(".pll21_div2",	CLK_PLL21_DIV2,	CLK_PLL21,	2, 1),
	DEF_FIXED(".pll30_div2",	CLK_PLL30_DIV2,	CLK_PLL30,	2, 1),
	DEF_FIXED(".pll31_div2",	CLK_PLL31_DIV2,	CLK_PLL31,	2, 1),
	DEF_FIXED(".pll5_div2",		CLK_PLL5_DIV2,	CLK_PLL5,	2, 1),
	DEF_FIXED(".pll5_div4",		CLK_PLL5_DIV4,	CLK_PLL5_DIV2,	2, 1),
	DEF_FIXED(".s1",		CLK_S1,		CLK_PLL1_DIV2,	2, 1),
	DEF_FIXED(".s3",		CLK_S3,		CLK_PLL1_DIV2,	4, 1),
	DEF_FIXED(".sdsrc",		CLK_SDSRC,	CLK_PLL5_DIV4,	1, 1),
	DEF_RATE(".oco",		CLK_OCO,	32768),

	/* Core Clock Outputs */
	DEF_FIXED("zx",		R8A779A0_CLK_ZX,	CLK_PLL20_DIV2,	2, 1),
	DEF_FIXED("s1d1",	R8A779A0_CLK_S1D1,	CLK_S1,		1, 1),
	DEF_FIXED("s1d2",	R8A779A0_CLK_S1D2,	CLK_S1,		2, 1),
	DEF_FIXED("s1d4",	R8A779A0_CLK_S1D4,	CLK_S1,		4, 1),
	DEF_FIXED("s1d8",	R8A779A0_CLK_S1D8,	CLK_S1,		8, 1),
	DEF_FIXED("s1d12",	R8A779A0_CLK_S1D12,	CLK_S1,		12, 1),
	DEF_FIXED("s3d1",	R8A779A0_CLK_S3D1,	CLK_S3,		1, 1),
	DEF_FIXED("s3d2",	R8A779A0_CLK_S3D2,	CLK_S3,		2, 1),
	DEF_FIXED("s3d4",	R8A779A0_CLK_S3D4,	CLK_S3,		4, 1),
	DEF_FIXED("zs",		R8A779A0_CLK_ZS,	CLK_PLL1_DIV2,	4, 1),
	DEF_FIXED("zt",		R8A779A0_CLK_ZT,	CLK_PLL1_DIV2,	2, 1),
	DEF_FIXED("ztr",	R8A779A0_CLK_ZTR,	CLK_PLL1_DIV2,	2, 1),
	DEF_FIXED("zr",		R8A779A0_CLK_ZR,	CLK_PLL1_DIV2,	1, 1),
	DEF_FIXED("dsi",	R8A779A0_CLK_DSI,	CLK_PLL5_DIV4,	1, 1),
	DEF_FIXED("cnndsp",	R8A779A0_CLK_CNNDSP,	CLK_PLL5_DIV4,	1, 1),
	DEF_FIXED("vip",	R8A779A0_CLK_VIP,	CLK_PLL5,	5, 1),
	DEF_FIXED("adgh",	R8A779A0_CLK_ADGH,	CLK_PLL5_DIV4,	1, 1),
	DEF_FIXED("icu",	R8A779A0_CLK_ICU,	CLK_PLL5_DIV4,	2, 1),
	DEF_FIXED("icud2",	R8A779A0_CLK_ICUD2,	CLK_PLL5_DIV4,	4, 1),
	DEF_FIXED("vcbus",	R8A779A0_CLK_VCBUS,	CLK_PLL5_DIV4,	1, 1),
	DEF_FIXED("cbfusa",	R8A779A0_CLK_CBFUSA,	CLK_EXTAL,	2, 1),
	DEF_FIXED("cp",		R8A779A0_CLK_CP,	CLK_EXTAL,	2, 1),

	DEF_SD("sd0",		R8A779A0_CLK_SD0,	CLK_SDSRC,	0x870),

	DEF_DIV6P1("mso",	R8A779A0_CLK_MSO,	CLK_PLL5_DIV4,	0x87c),
	DEF_DIV6P1("canfd",	R8A779A0_CLK_CANFD,	CLK_PLL5_DIV4,	0x878),
	DEF_DIV6P1("csi0",	R8A779A0_CLK_CSI0,	CLK_PLL5_DIV4,	0x880),

	DEF_OSC("osc",		R8A779A0_CLK_OSC,	CLK_EXTAL,	8),
	DEF_MDSEL("r",		R8A779A0_CLK_R, 29, CLK_EXTALR, 1, CLK_OCO, 1),
};

static const struct mssr_mod_clk r8a779a0_mod_clks[] __initconst = {
	DEF_MOD("avb0",		211,	R8A779A0_CLK_S3D2),
	DEF_MOD("avb1",		212,	R8A779A0_CLK_S3D2),
	DEF_MOD("avb2",		213,	R8A779A0_CLK_S3D2),
	DEF_MOD("avb3",		214,	R8A779A0_CLK_S3D2),
	DEF_MOD("avb4",		215,	R8A779A0_CLK_S3D2),
	DEF_MOD("avb5",		216,	R8A779A0_CLK_S3D2),
	DEF_MOD("csi40",	331,	R8A779A0_CLK_CSI0),
	DEF_MOD("csi41",	400,	R8A779A0_CLK_CSI0),
	DEF_MOD("csi42",	401,	R8A779A0_CLK_CSI0),
	DEF_MOD("csi43",	402,	R8A779A0_CLK_CSI0),
	DEF_MOD("fcpvd0",	508,	R8A779A0_CLK_S3D1),
	DEF_MOD("fcpvd1",	509,	R8A779A0_CLK_S3D1),
	DEF_MOD("hscif0",	514,	R8A779A0_CLK_S1D2),
	DEF_MOD("hscif1",	515,	R8A779A0_CLK_S1D2),
	DEF_MOD("hscif2",	516,	R8A779A0_CLK_S1D2),
	DEF_MOD("hscif3",	517,	R8A779A0_CLK_S1D2),
	DEF_MOD("i2c0",		518,	R8A779A0_CLK_S1D4),
	DEF_MOD("i2c1",		519,	R8A779A0_CLK_S1D4),
	DEF_MOD("i2c2",		520,	R8A779A0_CLK_S1D4),
	DEF_MOD("i2c3",		521,	R8A779A0_CLK_S1D4),
	DEF_MOD("i2c4",		522,	R8A779A0_CLK_S1D4),
	DEF_MOD("i2c5",		523,	R8A779A0_CLK_S1D4),
	DEF_MOD("i2c6",		524,	R8A779A0_CLK_S1D4),
	DEF_MOD("msi0",		618,	R8A779A0_CLK_MSO),
	DEF_MOD("msi1",		619,	R8A779A0_CLK_MSO),
	DEF_MOD("msi2",		620,	R8A779A0_CLK_MSO),
	DEF_MOD("msi3",		621,	R8A779A0_CLK_MSO),
	DEF_MOD("msi4",		622,	R8A779A0_CLK_MSO),
	DEF_MOD("msi5",		623,	R8A779A0_CLK_MSO),
	DEF_MOD("scif0",	702,	R8A779A0_CLK_S1D8),
	DEF_MOD("scif1",	703,	R8A779A0_CLK_S1D8),
	DEF_MOD("scif3",	704,	R8A779A0_CLK_S1D8),
	DEF_MOD("scif4",	705,	R8A779A0_CLK_S1D8),
	DEF_MOD("sdhi0",	706,	R8A779A0_CLK_SD0),
	DEF_MOD("sydm1",	709,	R8A779A0_CLK_S1D2),
	DEF_MOD("sydm2",	710,	R8A779A0_CLK_S1D2),
	DEF_MOD("vin00",	730,	R8A779A0_CLK_S1D1),
	DEF_MOD("vin01",	731,	R8A779A0_CLK_S1D1),
	DEF_MOD("vin02",	800,	R8A779A0_CLK_S1D1),
	DEF_MOD("vin03",	801,	R8A779A0_CLK_S1D1),
	DEF_MOD("vin04",	802,	R8A779A0_CLK_S1D1),
	DEF_MOD("vin05",	803,	R8A779A0_CLK_S1D1),
	DEF_MOD("vin06",	804,	R8A779A0_CLK_S1D1),
	DEF_MOD("vin07",	805,	R8A779A0_CLK_S1D1),
	DEF_MOD("vin10",	806,	R8A779A0_CLK_S1D1),
	DEF_MOD("vin11",	807,	R8A779A0_CLK_S1D1),
	DEF_MOD("vin12",	808,	R8A779A0_CLK_S1D1),
	DEF_MOD("vin13",	809,	R8A779A0_CLK_S1D1),
	DEF_MOD("vin14",	810,	R8A779A0_CLK_S1D1),
	DEF_MOD("vin15",	811,	R8A779A0_CLK_S1D1),
	DEF_MOD("vin16",	812,	R8A779A0_CLK_S1D1),
	DEF_MOD("vin17",	813,	R8A779A0_CLK_S1D1),
	DEF_MOD("vin20",	814,	R8A779A0_CLK_S1D1),
	DEF_MOD("vin21",	815,	R8A779A0_CLK_S1D1),
	DEF_MOD("vin22",	816,	R8A779A0_CLK_S1D1),
	DEF_MOD("vin23",	817,	R8A779A0_CLK_S1D1),
	DEF_MOD("vin24",	818,	R8A779A0_CLK_S1D1),
	DEF_MOD("vin25",	819,	R8A779A0_CLK_S1D1),
	DEF_MOD("vin26",	820,	R8A779A0_CLK_S1D1),
	DEF_MOD("vin27",	821,	R8A779A0_CLK_S1D1),
	DEF_MOD("vin30",	822,	R8A779A0_CLK_S1D1),
	DEF_MOD("vin31",	823,	R8A779A0_CLK_S1D1),
	DEF_MOD("vin32",	824,	R8A779A0_CLK_S1D1),
	DEF_MOD("vin33",	825,	R8A779A0_CLK_S1D1),
	DEF_MOD("vin34",	826,	R8A779A0_CLK_S1D1),
	DEF_MOD("vin35",	827,	R8A779A0_CLK_S1D1),
	DEF_MOD("vin36",	828,	R8A779A0_CLK_S1D1),
	DEF_MOD("vin37",	829,	R8A779A0_CLK_S1D1),
	DEF_MOD("vspd0",	830,	R8A779A0_CLK_S3D1),
	DEF_MOD("vspd1",	831,	R8A779A0_CLK_S3D1),
	DEF_MOD("rwdt",		907,	R8A779A0_CLK_R),
	DEF_MOD("pfc0",		915,	R8A779A0_CLK_CP),
	DEF_MOD("pfc1",		916,	R8A779A0_CLK_CP),
	DEF_MOD("pfc2",		917,	R8A779A0_CLK_CP),
	DEF_MOD("pfc3",		918,	R8A779A0_CLK_CP),
	DEF_MOD("vspx0",	1028,	R8A779A0_CLK_S1D1),
	DEF_MOD("vspx1",	1029,	R8A779A0_CLK_S1D1),
	DEF_MOD("vspx2",	1030,	R8A779A0_CLK_S1D1),
	DEF_MOD("vspx3",	1031,	R8A779A0_CLK_S1D1),
};

static const struct rcar_r8a779a0_cpg_pll_config *cpg_pll_config __initdata;
static unsigned int cpg_clk_extalr __initdata;
static u32 cpg_mode __initdata;

static struct clk * __init rcar_r8a779a0_cpg_clk_register(struct device *dev,
	const struct cpg_core_clk *core, const struct cpg_mssr_info *info,
	struct clk **clks, void __iomem *base,
	struct raw_notifier_head *notifiers)
{
	const struct clk *parent;
	unsigned int mult = 1;
	unsigned int div = 1;
	u32 value;

	parent = clks[core->parent & 0xffff];	/* some types use high bits */
	if (IS_ERR(parent))
		return ERR_CAST(parent);

	switch (core->type) {
	case CLK_TYPE_R8A779A0_MAIN:
		div = cpg_pll_config->extal_div;
		break;

	case CLK_TYPE_R8A779A0_PLL1:
		mult = cpg_pll_config->pll1_mult;
		div = cpg_pll_config->pll1_div;
		break;

	case CLK_TYPE_R8A779A0_PLL2X_3X:
		value = readl(base + core->offset);
		mult = (((value >> 24) & 0x7f) + 1) * 2;
		break;

	case CLK_TYPE_R8A779A0_PLL5:
		mult = cpg_pll_config->pll5_mult;
		div = cpg_pll_config->pll5_div;
		break;

	case CLK_TYPE_R8A779A0_SD:
		return cpg_sd_clk_register(core->name, base, core->offset,
					   __clk_get_name(parent), notifiers,
					   false);
		break;

	case CLK_TYPE_R8A779A0_MDSEL:
		/*
		 * Clock selectable between two parents and two fixed dividers
		 * using a mode pin
		 */
		if (cpg_mode & BIT(core->offset)) {
			div = core->div & 0xffff;
		} else {
			parent = clks[core->parent >> 16];
			if (IS_ERR(parent))
				return ERR_CAST(parent);
			div = core->div >> 16;
		}
		mult = 1;
		break;

	case CLK_TYPE_R8A779A0_OSC:
		/*
		 * Clock combining OSC EXTAL predivider and a fixed divider
		 */
		div = cpg_pll_config->osc_prediv * core->div;
		break;

	default:
		return ERR_PTR(-EINVAL);
	}

	return clk_register_fixed_factor(NULL, core->name,
					 __clk_get_name(parent), 0, mult, div);
}

static const unsigned int r8a779a0_crit_mod_clks[] __initconst = {
	MOD_CLK_ID(907),	/* RWDT */
};

/*
 * CPG Clock Data
 */
/*
 *   MD	 EXTAL		PLL1	PLL20	PLL30	PLL4	PLL5	OSC
 * 14 13 (MHz)			   21	   31
 * --------------------------------------------------------
 * 0  0	 16.66 x 1	x128	x216	x128	x144	x192	/16
 * 0  1	 20    x 1	x106	x180	x106	x120	x160	/19
 * 1  0	 Prohibited setting
 * 1  1	 33.33 / 2	x128	x216	x128	x144	x192	/32
 */
#define CPG_PLL_CONFIG_INDEX(md)	((((md) & BIT(14)) >> 13) | \
					 (((md) & BIT(13)) >> 13))

static const struct rcar_r8a779a0_cpg_pll_config cpg_pll_configs[4] = {
	/* EXTAL div	PLL1 mult/div	PLL5 mult/div	OSC prediv */
	{ 1,		128,	1,	192,	1,	16,	},
	{ 1,		106,	1,	160,	1,	19,	},
	{ 0,		0,	0,	0,	0,	0,	},
	{ 2,		128,	1,	192,	1,	32,	},
};

static int __init r8a779a0_cpg_mssr_init(struct device *dev)
{
	int error;

	error = rcar_rst_read_mode_pins(&cpg_mode);
	if (error)
		return error;

	cpg_pll_config = &cpg_pll_configs[CPG_PLL_CONFIG_INDEX(cpg_mode)];
	cpg_clk_extalr = CLK_EXTALR;
	spin_lock_init(&cpg_lock);

	return 0;
}

const struct cpg_mssr_info r8a779a0_cpg_mssr_info __initconst = {
	/* Core Clocks */
	.core_clks = r8a779a0_core_clks,
	.num_core_clks = ARRAY_SIZE(r8a779a0_core_clks),
	.last_dt_core_clk = LAST_DT_CORE_CLK,
	.num_total_core_clks = MOD_CLK_BASE,

	/* Module Clocks */
	.mod_clks = r8a779a0_mod_clks,
	.num_mod_clks = ARRAY_SIZE(r8a779a0_mod_clks),
	.num_hw_mod_clks = 15 * 32,

	/* Critical Module Clocks */
	.crit_mod_clks		= r8a779a0_crit_mod_clks,
	.num_crit_mod_clks	= ARRAY_SIZE(r8a779a0_crit_mod_clks),

	/* Callbacks */
	.init = r8a779a0_cpg_mssr_init,
	.cpg_clk_register = rcar_r8a779a0_cpg_clk_register,

	.reg_layout = CLK_REG_LAYOUT_RCAR_V3U,
};
