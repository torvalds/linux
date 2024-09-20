// SPDX-License-Identifier: GPL-2.0
/*
 * r8a779h0 Clock Pulse Generator / Module Standby and Software Reset
 *
 * Copyright (C) 2023 Renesas Electronics Corp.
 *
 * Based on r8a779g0-cpg-mssr.c
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/soc/renesas/rcar-rst.h>

#include <dt-bindings/clock/renesas,r8a779h0-cpg-mssr.h>

#include "renesas-cpg-mssr.h"
#include "rcar-gen4-cpg.h"

enum clk_ids {
	/* Core Clock Outputs exported to DT */
	LAST_DT_CORE_CLK = R8A779H0_CLK_R,

	/* External Input Clocks */
	CLK_EXTAL,
	CLK_EXTALR,

	/* Internal Core Clocks */
	CLK_MAIN,
	CLK_PLL1,
	CLK_PLL2,
	CLK_PLL3,
	CLK_PLL4,
	CLK_PLL5,
	CLK_PLL6,
	CLK_PLL1_DIV2,
	CLK_PLL2_DIV2,
	CLK_PLL3_DIV2,
	CLK_PLL4_DIV2,
	CLK_PLL4_DIV5,
	CLK_PLL5_DIV2,
	CLK_PLL5_DIV4,
	CLK_PLL6_DIV2,
	CLK_S0,
	CLK_S0_VIO,
	CLK_S0_VC,
	CLK_S0_HSC,
	CLK_SASYNCPER,
	CLK_SV_VIP,
	CLK_SV_IR,
	CLK_IMPASRC,
	CLK_IMPBSRC,
	CLK_VIOSRC,
	CLK_VCSRC,
	CLK_SDSRC,
	CLK_RPCSRC,
	CLK_OCO,

	/* Module Clocks */
	MOD_CLK_BASE
};

static const struct cpg_core_clk r8a779h0_core_clks[] = {
	/* External Clock Inputs */
	DEF_INPUT("extal",	CLK_EXTAL),
	DEF_INPUT("extalr",	CLK_EXTALR),

	/* Internal Core Clocks */
	DEF_BASE(".main", CLK_MAIN,	CLK_TYPE_GEN4_MAIN,	CLK_EXTAL),
	DEF_BASE(".pll1", CLK_PLL1,	CLK_TYPE_GEN4_PLL1,	CLK_MAIN),
	DEF_BASE(".pll2", CLK_PLL2,	CLK_TYPE_GEN4_PLL2,	CLK_MAIN),
	DEF_BASE(".pll3", CLK_PLL3,	CLK_TYPE_GEN4_PLL3,	CLK_MAIN),
	DEF_BASE(".pll4", CLK_PLL4,	CLK_TYPE_GEN4_PLL4,	CLK_MAIN),
	DEF_BASE(".pll5", CLK_PLL5,	CLK_TYPE_GEN4_PLL5,	CLK_MAIN),
	DEF_BASE(".pll6", CLK_PLL6,	CLK_TYPE_GEN4_PLL6,	CLK_MAIN),

	DEF_FIXED(".pll1_div2",	CLK_PLL1_DIV2,	CLK_PLL1,	2, 1),
	DEF_FIXED(".pll2_div2",	CLK_PLL2_DIV2,	CLK_PLL2,	2, 1),
	DEF_FIXED(".pll3_div2",	CLK_PLL3_DIV2,	CLK_PLL3,	2, 1),
	DEF_FIXED(".pll4_div2",	CLK_PLL4_DIV2,	CLK_PLL4,	2, 1),
	DEF_FIXED(".pll4_div5",	CLK_PLL4_DIV5,	CLK_PLL4,	5, 1),
	DEF_FIXED(".pll5_div2",	CLK_PLL5_DIV2,	CLK_PLL5,	2, 1),
	DEF_FIXED(".pll5_div4",	CLK_PLL5_DIV4,	CLK_PLL5_DIV2,	2, 1),
	DEF_FIXED(".pll6_div2",	CLK_PLL6_DIV2,	CLK_PLL6,	2, 1),
	DEF_FIXED(".s0",	CLK_S0,		CLK_PLL1_DIV2,	2, 1),
	DEF_FIXED(".s0_vio",	CLK_S0_VIO,	CLK_PLL1_DIV2,	2, 1),
	DEF_FIXED(".s0_vc",	CLK_S0_VC,	CLK_PLL1_DIV2,	2, 1),
	DEF_FIXED(".s0_hsc",	CLK_S0_HSC,	CLK_PLL1_DIV2,	2, 1),
	DEF_FIXED(".sasyncper",	CLK_SASYNCPER,	CLK_PLL5_DIV4,	3, 1),
	DEF_FIXED(".sv_vip",	CLK_SV_VIP,	CLK_PLL1,	5, 1),
	DEF_FIXED(".sv_ir",	CLK_SV_IR,	CLK_PLL1,	5, 1),
	DEF_FIXED(".impasrc",	CLK_IMPASRC,	CLK_PLL1_DIV2,	2, 1),
	DEF_FIXED(".impbsrc",	CLK_IMPBSRC,	CLK_PLL1,	4, 1),
	DEF_FIXED(".viosrc",	CLK_VIOSRC,	CLK_PLL1,	6, 1),
	DEF_FIXED(".vcsrc",	CLK_VCSRC,	CLK_PLL1,	6, 1),
	DEF_BASE(".sdsrc",	CLK_SDSRC,	CLK_TYPE_GEN4_SDSRC, CLK_PLL5),
	DEF_BASE(".rpcsrc",	CLK_RPCSRC,	CLK_TYPE_GEN4_RPCSRC, CLK_PLL5),
	DEF_RATE(".oco",	CLK_OCO,	32768),

	/* Core Clock Outputs */
	DEF_GEN4_Z("zc0",	R8A779H0_CLK_ZC0,	CLK_TYPE_GEN4_Z,	CLK_PLL2_DIV2,	2, 0),
	DEF_GEN4_Z("zc1",	R8A779H0_CLK_ZC1,	CLK_TYPE_GEN4_Z,	CLK_PLL2_DIV2,	2, 8),
	DEF_GEN4_Z("zc2",	R8A779H0_CLK_ZC2,	CLK_TYPE_GEN4_Z,	CLK_PLL2_DIV2,	2, 32),
	DEF_GEN4_Z("zc3",	R8A779H0_CLK_ZC3,	CLK_TYPE_GEN4_Z,	CLK_PLL2_DIV2,	2, 40),
	DEF_FIXED("s0d2",	R8A779H0_CLK_S0D2,	CLK_S0,		2, 1),
	DEF_FIXED("s0d3",	R8A779H0_CLK_S0D3,	CLK_S0,		3, 1),
	DEF_FIXED("s0d4",	R8A779H0_CLK_S0D4,	CLK_S0,		4, 1),
	DEF_FIXED("cl16m",	R8A779H0_CLK_CL16M,	CLK_S0,		48, 1),
	DEF_FIXED("s0d2_rt",	R8A779H0_CLK_S0D2_RT,	CLK_S0,		2, 1),
	DEF_FIXED("s0d3_rt",	R8A779H0_CLK_S0D3_RT,	CLK_S0,		3, 1),
	DEF_FIXED("s0d4_rt",	R8A779H0_CLK_S0D4_RT,	CLK_S0,		4, 1),
	DEF_FIXED("s0d6_rt",	R8A779H0_CLK_S0D6_RT,	CLK_S0,		6, 1),
	DEF_FIXED("cl16m_rt",	R8A779H0_CLK_CL16M_RT,	CLK_S0,		48, 1),
	DEF_FIXED("s0d2_per",	R8A779H0_CLK_S0D2_PER,	CLK_S0,		2, 1),
	DEF_FIXED("s0d3_per",	R8A779H0_CLK_S0D3_PER,	CLK_S0,		3, 1),
	DEF_FIXED("s0d4_per",	R8A779H0_CLK_S0D4_PER,	CLK_S0,		4, 1),
	DEF_FIXED("s0d6_per",	R8A779H0_CLK_S0D6_PER,	CLK_S0,		6, 1),
	DEF_FIXED("s0d12_per",	R8A779H0_CLK_S0D12_PER,	CLK_S0,		12, 1),
	DEF_FIXED("s0d24_per",	R8A779H0_CLK_S0D24_PER,	CLK_S0,		24, 1),
	DEF_FIXED("cl16m_per",	R8A779H0_CLK_CL16M_PER,	CLK_S0,		48, 1),
	DEF_FIXED("s0d2_mm",	R8A779H0_CLK_S0D2_MM,	CLK_S0,		2, 1),
	DEF_FIXED("s0d4_mm",	R8A779H0_CLK_S0D4_MM,	CLK_S0,		4, 1),
	DEF_FIXED("cl16m_mm",	R8A779H0_CLK_CL16M_MM,	CLK_S0,		48, 1),
	DEF_FIXED("s0d2_u3dg",	R8A779H0_CLK_S0D2_U3DG,	CLK_S0,		2, 1),
	DEF_FIXED("s0d4_u3dg",	R8A779H0_CLK_S0D4_U3DG,	CLK_S0,		4, 1),
	DEF_FIXED("s0d1_vio",	R8A779H0_CLK_S0D1_VIO,	CLK_S0_VIO,	1, 1),
	DEF_FIXED("s0d2_vio",	R8A779H0_CLK_S0D2_VIO,	CLK_S0_VIO,	2, 1),
	DEF_FIXED("s0d4_vio",	R8A779H0_CLK_S0D4_VIO,	CLK_S0_VIO,	4, 1),
	DEF_FIXED("s0d8_vio",	R8A779H0_CLK_S0D8_VIO,	CLK_S0_VIO,	8, 1),
	DEF_FIXED("s0d1_vc",	R8A779H0_CLK_S0D1_VC,	CLK_S0_VC,	1, 1),
	DEF_FIXED("s0d2_vc",	R8A779H0_CLK_S0D2_VC,	CLK_S0_VC,	2, 1),
	DEF_FIXED("s0d4_vc",	R8A779H0_CLK_S0D4_VC,	CLK_S0_VC,	4, 1),
	DEF_FIXED("s0d1_hsc",	R8A779H0_CLK_S0D1_HSC,	CLK_S0_HSC,	1, 1),
	DEF_FIXED("s0d2_hsc",	R8A779H0_CLK_S0D2_HSC,	CLK_S0_HSC,	2, 1),
	DEF_FIXED("s0d4_hsc",	R8A779H0_CLK_S0D4_HSC,	CLK_S0_HSC,	4, 1),
	DEF_FIXED("s0d8_hsc",	R8A779H0_CLK_S0D8_HSC,	CLK_S0_HSC,	8, 1),
	DEF_FIXED("cl16m_hsc",	R8A779H0_CLK_CL16M_HSC,	CLK_S0_HSC,	48, 1),
	DEF_FIXED("sasyncrt",	R8A779H0_CLK_SASYNCRT,	CLK_PLL5_DIV4,	48, 1),
	DEF_FIXED("sasyncperd1", R8A779H0_CLK_SASYNCPERD1, CLK_SASYNCPER, 1, 1),
	DEF_FIXED("sasyncperd2", R8A779H0_CLK_SASYNCPERD2, CLK_SASYNCPER, 2, 1),
	DEF_FIXED("sasyncperd4", R8A779H0_CLK_SASYNCPERD4, CLK_SASYNCPER, 4, 1),
	DEF_FIXED("svd1_vip",	R8A779H0_CLK_SVD1_VIP,	CLK_SV_VIP,	1, 1),
	DEF_FIXED("svd2_vip",	R8A779H0_CLK_SVD2_VIP,	CLK_SV_VIP,	2, 1),
	DEF_FIXED("svd1_ir",	R8A779H0_CLK_SVD1_IR,	CLK_SV_IR,	1, 1),
	DEF_FIXED("svd2_ir",	R8A779H0_CLK_SVD2_IR,	CLK_SV_IR,	2, 1),
	DEF_FIXED("cbfusa",	R8A779H0_CLK_CBFUSA,	CLK_EXTAL,	2, 1),
	DEF_FIXED("cpex",	R8A779H0_CLK_CPEX,	CLK_EXTAL,	2, 1),
	DEF_FIXED("cp",		R8A779H0_CLK_CP,	CLK_EXTAL,	2, 1),
	DEF_FIXED("impad1",	R8A779H0_CLK_IMPAD1,	CLK_IMPASRC,	1, 1),
	DEF_FIXED("impad4",	R8A779H0_CLK_IMPAD4,	CLK_IMPASRC,	4, 1),
	DEF_FIXED("impb",	R8A779H0_CLK_IMPB,	CLK_IMPBSRC,	1, 1),
	DEF_FIXED("viobusd1",	R8A779H0_CLK_VIOBUSD1,	CLK_VIOSRC,	1, 1),
	DEF_FIXED("viobusd2",	R8A779H0_CLK_VIOBUSD2,	CLK_VIOSRC,	2, 1),
	DEF_FIXED("vcbusd1",	R8A779H0_CLK_VCBUSD1,	CLK_VCSRC,	1, 1),
	DEF_FIXED("vcbusd2",	R8A779H0_CLK_VCBUSD2,	CLK_VCSRC,	2, 1),
	DEF_DIV6P1("canfd",	R8A779H0_CLK_CANFD,	CLK_PLL5_DIV4,	0x878),
	DEF_DIV6P1("csi",	R8A779H0_CLK_CSI,	CLK_PLL5_DIV4,	0x880),
	DEF_FIXED("dsiref",	R8A779H0_CLK_DSIREF,	CLK_PLL5_DIV4,	48, 1),
	DEF_DIV6P1("dsiext",	R8A779H0_CLK_DSIEXT,	CLK_PLL5_DIV4,	0x884),
	DEF_DIV6P1("mso",	R8A779H0_CLK_MSO,	CLK_PLL5_DIV4,	0x87c),

	DEF_GEN4_SDH("sd0h",	R8A779H0_CLK_SD0H,	CLK_SDSRC,	   0x870),
	DEF_GEN4_SD("sd0",	R8A779H0_CLK_SD0,	R8A779H0_CLK_SD0H, 0x870),

	DEF_BASE("rpc",		R8A779H0_CLK_RPC,	CLK_TYPE_GEN4_RPC, CLK_RPCSRC),
	DEF_BASE("rpcd2",	R8A779H0_CLK_RPCD2,	CLK_TYPE_GEN4_RPCD2, R8A779H0_CLK_RPC),

	DEF_GEN4_OSC("osc",	R8A779H0_CLK_OSC,	CLK_EXTAL,	8),
	DEF_GEN4_MDSEL("r",	R8A779H0_CLK_R, 29, CLK_EXTALR, 1, CLK_OCO, 1),
};

static const struct mssr_mod_clk r8a779h0_mod_clks[] = {
	DEF_MOD("avb0:rgmii0",	211,	R8A779H0_CLK_S0D8_HSC),
	DEF_MOD("avb1:rgmii1",	212,	R8A779H0_CLK_S0D8_HSC),
	DEF_MOD("avb2:rgmii2",	213,	R8A779H0_CLK_S0D8_HSC),
	DEF_MOD("csi40",	331,	R8A779H0_CLK_CSI),
	DEF_MOD("csi41",	400,	R8A779H0_CLK_CSI),
	DEF_MOD("hscif0",	514,	R8A779H0_CLK_SASYNCPERD1),
	DEF_MOD("hscif1",	515,	R8A779H0_CLK_SASYNCPERD1),
	DEF_MOD("hscif2",	516,	R8A779H0_CLK_SASYNCPERD1),
	DEF_MOD("hscif3",	517,	R8A779H0_CLK_SASYNCPERD1),
	DEF_MOD("i2c0",		518,	R8A779H0_CLK_S0D6_PER),
	DEF_MOD("i2c1",		519,	R8A779H0_CLK_S0D6_PER),
	DEF_MOD("i2c2",		520,	R8A779H0_CLK_S0D6_PER),
	DEF_MOD("i2c3",		521,	R8A779H0_CLK_S0D6_PER),
	DEF_MOD("irqc",		611,	R8A779H0_CLK_CL16M),
	DEF_MOD("ispcs0",	612,	R8A779H0_CLK_S0D2_VIO),
	DEF_MOD("ispcs1",	613,	R8A779H0_CLK_S0D2_VIO),
	DEF_MOD("msi0",		618,	R8A779H0_CLK_MSO),
	DEF_MOD("msi1",		619,	R8A779H0_CLK_MSO),
	DEF_MOD("msi2",		620,	R8A779H0_CLK_MSO),
	DEF_MOD("msi3",		621,	R8A779H0_CLK_MSO),
	DEF_MOD("msi4",		622,	R8A779H0_CLK_MSO),
	DEF_MOD("msi5",		623,	R8A779H0_CLK_MSO),
	DEF_MOD("rpc-if",	629,	R8A779H0_CLK_RPCD2),
	DEF_MOD("scif0",	702,	R8A779H0_CLK_SASYNCPERD4),
	DEF_MOD("scif1",	703,	R8A779H0_CLK_SASYNCPERD4),
	DEF_MOD("scif3",	704,	R8A779H0_CLK_SASYNCPERD4),
	DEF_MOD("scif4",	705,	R8A779H0_CLK_SASYNCPERD4),
	DEF_MOD("sdhi0",	706,	R8A779H0_CLK_SD0),
	DEF_MOD("sydm1",	709,	R8A779H0_CLK_S0D6_PER),
	DEF_MOD("sydm2",	710,	R8A779H0_CLK_S0D6_PER),
	DEF_MOD("tmu0",		713,	R8A779H0_CLK_SASYNCRT),
	DEF_MOD("tmu1",		714,	R8A779H0_CLK_SASYNCPERD2),
	DEF_MOD("tmu2",		715,	R8A779H0_CLK_SASYNCPERD2),
	DEF_MOD("tmu3",		716,	R8A779H0_CLK_SASYNCPERD2),
	DEF_MOD("tmu4",		717,	R8A779H0_CLK_SASYNCPERD2),
	DEF_MOD("vin00",	730,	R8A779H0_CLK_S0D4_VIO),
	DEF_MOD("vin01",	731,	R8A779H0_CLK_S0D4_VIO),
	DEF_MOD("vin02",	800,	R8A779H0_CLK_S0D4_VIO),
	DEF_MOD("vin03",	801,	R8A779H0_CLK_S0D4_VIO),
	DEF_MOD("vin04",	802,	R8A779H0_CLK_S0D4_VIO),
	DEF_MOD("vin05",	803,	R8A779H0_CLK_S0D4_VIO),
	DEF_MOD("vin06",	804,	R8A779H0_CLK_S0D4_VIO),
	DEF_MOD("vin07",	805,	R8A779H0_CLK_S0D4_VIO),
	DEF_MOD("vin10",	806,	R8A779H0_CLK_S0D4_VIO),
	DEF_MOD("vin11",	807,	R8A779H0_CLK_S0D4_VIO),
	DEF_MOD("vin12",	808,	R8A779H0_CLK_S0D4_VIO),
	DEF_MOD("vin13",	809,	R8A779H0_CLK_S0D4_VIO),
	DEF_MOD("vin14",	810,	R8A779H0_CLK_S0D4_VIO),
	DEF_MOD("vin15",	811,	R8A779H0_CLK_S0D4_VIO),
	DEF_MOD("vin16",	812,	R8A779H0_CLK_S0D4_VIO),
	DEF_MOD("vin17",	813,	R8A779H0_CLK_S0D4_VIO),
	DEF_MOD("wdt1:wdt0",	907,	R8A779H0_CLK_R),
	DEF_MOD("cmt0",		910,	R8A779H0_CLK_R),
	DEF_MOD("cmt1",		911,	R8A779H0_CLK_R),
	DEF_MOD("cmt2",		912,	R8A779H0_CLK_R),
	DEF_MOD("cmt3",		913,	R8A779H0_CLK_R),
	DEF_MOD("pfc0",		915,	R8A779H0_CLK_CP),
	DEF_MOD("pfc1",		916,	R8A779H0_CLK_CP),
	DEF_MOD("pfc2",		917,	R8A779H0_CLK_CP),
	DEF_MOD("tsc2:tsc1",	919,	R8A779H0_CLK_CL16M),
	DEF_MOD("ssiu",		2926,	R8A779H0_CLK_S0D6_PER),
	DEF_MOD("ssi",		2927,	R8A779H0_CLK_S0D6_PER),
};

/*
 * CPG Clock Data
 */
/*
 *   MD	 EXTAL		PLL1	PLL2	PLL3	PLL4	PLL5	PLL6	OSC
 * 14 13 (MHz)
 * ------------------------------------------------------------------------
 * 0  0	 16.66 / 1	x192	x240	x192	x240	x192	x168	/16
 * 0  1	 20    / 1	x160	x200	x160	x200	x160	x140	/19
 * 1  0	 Prohibited setting
 * 1  1	 33.33 / 2	x192	x240	x192	x240	x192	x168	/32
 */
#define CPG_PLL_CONFIG_INDEX(md)	((((md) & BIT(14)) >> 13) | \
					 (((md) & BIT(13)) >> 13))

static const struct rcar_gen4_cpg_pll_config cpg_pll_configs[4] = {
	/* EXTAL div	PLL1 mult/div	PLL2 mult/div	PLL3 mult/div	PLL4 mult/div	PLL5 mult/div	PLL6 mult/div	OSC prediv */
	{ 1,		192,	1,	240,	1,	192,	1,	240,	1,	192,	1,	168,	1,	16,	},
	{ 1,		160,	1,	200,	1,	160,	1,	200,	1,	160,	1,	140,	1,	19,	},
	{ 0,		0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	0,	},
	{ 2,		192,	1,	240,	1,	192,	1,	240,	1,	192,	1,	168,	1,	32,	},
};

static int __init r8a779h0_cpg_mssr_init(struct device *dev)
{
	const struct rcar_gen4_cpg_pll_config *cpg_pll_config;
	u32 cpg_mode;
	int error;

	error = rcar_rst_read_mode_pins(&cpg_mode);
	if (error)
		return error;

	cpg_pll_config = &cpg_pll_configs[CPG_PLL_CONFIG_INDEX(cpg_mode)];
	if (!cpg_pll_config->extal_div) {
		dev_err(dev, "Prohibited setting (cpg_mode=0x%x)\n", cpg_mode);
		return -EINVAL;
	}

	return rcar_gen4_cpg_init(cpg_pll_config, CLK_EXTALR, cpg_mode);
}

const struct cpg_mssr_info r8a779h0_cpg_mssr_info __initconst = {
	/* Core Clocks */
	.core_clks = r8a779h0_core_clks,
	.num_core_clks = ARRAY_SIZE(r8a779h0_core_clks),
	.last_dt_core_clk = LAST_DT_CORE_CLK,
	.num_total_core_clks = MOD_CLK_BASE,

	/* Module Clocks */
	.mod_clks = r8a779h0_mod_clks,
	.num_mod_clks = ARRAY_SIZE(r8a779h0_mod_clks),
	.num_hw_mod_clks = 30 * 32,

	/* Callbacks */
	.init = r8a779h0_cpg_mssr_init,
	.cpg_clk_register = rcar_gen4_cpg_clk_register,

	.reg_layout = CLK_REG_LAYOUT_RCAR_GEN4,
};
