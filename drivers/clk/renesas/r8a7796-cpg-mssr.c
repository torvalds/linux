/*
 * r8a7796 Clock Pulse Generator / Module Standby and Software Reset
 *
 * Copyright (C) 2016 Glider bvba
 *
 * Based on r8a7795-cpg-mssr.c
 *
 * Copyright (C) 2015 Glider bvba
 * Copyright (C) 2015 Renesas Electronics Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/soc/renesas/rcar-rst.h>

#include <dt-bindings/clock/r8a7796-cpg-mssr.h>

#include "renesas-cpg-mssr.h"
#include "rcar-gen3-cpg.h"

enum clk_ids {
	/* Core Clock Outputs exported to DT */
	LAST_DT_CORE_CLK = R8A7796_CLK_OSC,

	/* External Input Clocks */
	CLK_EXTAL,
	CLK_EXTALR,

	/* Internal Core Clocks */
	CLK_MAIN,
	CLK_PLL0,
	CLK_PLL1,
	CLK_PLL2,
	CLK_PLL3,
	CLK_PLL4,
	CLK_PLL1_DIV2,
	CLK_PLL1_DIV4,
	CLK_S0,
	CLK_S1,
	CLK_S2,
	CLK_S3,
	CLK_SDSRC,
	CLK_SSPSRC,
	CLK_RINT,

	/* Module Clocks */
	MOD_CLK_BASE
};

static const struct cpg_core_clk r8a7796_core_clks[] __initconst = {
	/* External Clock Inputs */
	DEF_INPUT("extal",      CLK_EXTAL),
	DEF_INPUT("extalr",     CLK_EXTALR),

	/* Internal Core Clocks */
	DEF_BASE(".main",       CLK_MAIN, CLK_TYPE_GEN3_MAIN, CLK_EXTAL),
	DEF_BASE(".pll0",       CLK_PLL0, CLK_TYPE_GEN3_PLL0, CLK_MAIN),
	DEF_BASE(".pll1",       CLK_PLL1, CLK_TYPE_GEN3_PLL1, CLK_MAIN),
	DEF_BASE(".pll2",       CLK_PLL2, CLK_TYPE_GEN3_PLL2, CLK_MAIN),
	DEF_BASE(".pll3",       CLK_PLL3, CLK_TYPE_GEN3_PLL3, CLK_MAIN),
	DEF_BASE(".pll4",       CLK_PLL4, CLK_TYPE_GEN3_PLL4, CLK_MAIN),

	DEF_FIXED(".pll1_div2", CLK_PLL1_DIV2,     CLK_PLL1,       2, 1),
	DEF_FIXED(".pll1_div4", CLK_PLL1_DIV4,     CLK_PLL1_DIV2,  2, 1),
	DEF_FIXED(".s0",        CLK_S0,            CLK_PLL1_DIV2,  2, 1),
	DEF_FIXED(".s1",        CLK_S1,            CLK_PLL1_DIV2,  3, 1),
	DEF_FIXED(".s2",        CLK_S2,            CLK_PLL1_DIV2,  4, 1),
	DEF_FIXED(".s3",        CLK_S3,            CLK_PLL1_DIV2,  6, 1),
	DEF_FIXED(".sdsrc",     CLK_SDSRC,         CLK_PLL1_DIV2,  2, 1),

	/* Core Clock Outputs */
	DEF_FIXED("ztr",        R8A7796_CLK_ZTR,   CLK_PLL1_DIV2,  6, 1),
	DEF_FIXED("ztrd2",      R8A7796_CLK_ZTRD2, CLK_PLL1_DIV2, 12, 1),
	DEF_FIXED("zt",         R8A7796_CLK_ZT,    CLK_PLL1_DIV2,  4, 1),
	DEF_FIXED("zx",         R8A7796_CLK_ZX,    CLK_PLL1_DIV2,  2, 1),
	DEF_FIXED("s0d1",       R8A7796_CLK_S0D1,  CLK_S0,         1, 1),
	DEF_FIXED("s0d2",       R8A7796_CLK_S0D2,  CLK_S0,         2, 1),
	DEF_FIXED("s0d3",       R8A7796_CLK_S0D3,  CLK_S0,         3, 1),
	DEF_FIXED("s0d4",       R8A7796_CLK_S0D4,  CLK_S0,         4, 1),
	DEF_FIXED("s0d6",       R8A7796_CLK_S0D6,  CLK_S0,         6, 1),
	DEF_FIXED("s0d8",       R8A7796_CLK_S0D8,  CLK_S0,         8, 1),
	DEF_FIXED("s0d12",      R8A7796_CLK_S0D12, CLK_S0,        12, 1),
	DEF_FIXED("s1d1",       R8A7796_CLK_S1D1,  CLK_S1,         1, 1),
	DEF_FIXED("s1d2",       R8A7796_CLK_S1D2,  CLK_S1,         2, 1),
	DEF_FIXED("s1d4",       R8A7796_CLK_S1D4,  CLK_S1,         4, 1),
	DEF_FIXED("s2d1",       R8A7796_CLK_S2D1,  CLK_S2,         1, 1),
	DEF_FIXED("s2d2",       R8A7796_CLK_S2D2,  CLK_S2,         2, 1),
	DEF_FIXED("s2d4",       R8A7796_CLK_S2D4,  CLK_S2,         4, 1),
	DEF_FIXED("s3d1",       R8A7796_CLK_S3D1,  CLK_S3,         1, 1),
	DEF_FIXED("s3d2",       R8A7796_CLK_S3D2,  CLK_S3,         2, 1),
	DEF_FIXED("s3d4",       R8A7796_CLK_S3D4,  CLK_S3,         4, 1),

	DEF_GEN3_SD("sd0",      R8A7796_CLK_SD0,   CLK_SDSRC,     0x074),
	DEF_GEN3_SD("sd1",      R8A7796_CLK_SD1,   CLK_SDSRC,     0x078),
	DEF_GEN3_SD("sd2",      R8A7796_CLK_SD2,   CLK_SDSRC,     0x268),
	DEF_GEN3_SD("sd3",      R8A7796_CLK_SD3,   CLK_SDSRC,     0x26c),

	DEF_FIXED("cl",         R8A7796_CLK_CL,    CLK_PLL1_DIV2, 48, 1),
	DEF_FIXED("cp",         R8A7796_CLK_CP,    CLK_EXTAL,      2, 1),

	DEF_DIV6P1("canfd",     R8A7796_CLK_CANFD, CLK_PLL1_DIV4, 0x244),
	DEF_DIV6P1("csi0",      R8A7796_CLK_CSI0,  CLK_PLL1_DIV4, 0x00c),
	DEF_DIV6P1("mso",       R8A7796_CLK_MSO,   CLK_PLL1_DIV4, 0x014),

	DEF_DIV6_RO("osc",      R8A7796_CLK_OSC,   CLK_EXTAL, CPG_RCKCR,  8),
	DEF_DIV6_RO("r_int",    CLK_RINT,          CLK_EXTAL, CPG_RCKCR, 32),

	DEF_BASE("r",           R8A7796_CLK_R,     CLK_TYPE_GEN3_R, CLK_RINT),
};

static const struct mssr_mod_clk r8a7796_mod_clks[] __initconst = {
	DEF_MOD("scif5",		 202,	R8A7796_CLK_S3D4),
	DEF_MOD("scif4",		 203,	R8A7796_CLK_S3D4),
	DEF_MOD("scif3",		 204,	R8A7796_CLK_S3D4),
	DEF_MOD("scif1",		 206,	R8A7796_CLK_S3D4),
	DEF_MOD("scif0",		 207,	R8A7796_CLK_S3D4),
	DEF_MOD("msiof3",		 208,	R8A7796_CLK_MSO),
	DEF_MOD("msiof2",		 209,	R8A7796_CLK_MSO),
	DEF_MOD("msiof1",		 210,	R8A7796_CLK_MSO),
	DEF_MOD("msiof0",		 211,	R8A7796_CLK_MSO),
	DEF_MOD("sys-dmac2",		 217,	R8A7796_CLK_S0D3),
	DEF_MOD("sys-dmac1",		 218,	R8A7796_CLK_S0D3),
	DEF_MOD("sys-dmac0",		 219,	R8A7796_CLK_S0D3),
	DEF_MOD("cmt3",			 300,	R8A7796_CLK_R),
	DEF_MOD("cmt2",			 301,	R8A7796_CLK_R),
	DEF_MOD("cmt1",			 302,	R8A7796_CLK_R),
	DEF_MOD("cmt0",			 303,	R8A7796_CLK_R),
	DEF_MOD("scif2",		 310,	R8A7796_CLK_S3D4),
	DEF_MOD("sdif3",		 311,	R8A7796_CLK_SD3),
	DEF_MOD("sdif2",		 312,	R8A7796_CLK_SD2),
	DEF_MOD("sdif1",		 313,	R8A7796_CLK_SD1),
	DEF_MOD("sdif0",		 314,	R8A7796_CLK_SD0),
	DEF_MOD("rwdt",			 402,	R8A7796_CLK_R),
	DEF_MOD("intc-ap",		 408,	R8A7796_CLK_S3D1),
	DEF_MOD("drif7",		 508,	R8A7796_CLK_S3D2),
	DEF_MOD("drif6",		 509,	R8A7796_CLK_S3D2),
	DEF_MOD("drif5",		 510,	R8A7796_CLK_S3D2),
	DEF_MOD("drif4",		 511,	R8A7796_CLK_S3D2),
	DEF_MOD("drif3",		 512,	R8A7796_CLK_S3D2),
	DEF_MOD("drif2",		 513,	R8A7796_CLK_S3D2),
	DEF_MOD("drif1",		 514,	R8A7796_CLK_S3D2),
	DEF_MOD("drif0",		 515,	R8A7796_CLK_S3D2),
	DEF_MOD("hscif4",		 516,	R8A7796_CLK_S3D1),
	DEF_MOD("hscif3",		 517,	R8A7796_CLK_S3D1),
	DEF_MOD("hscif2",		 518,	R8A7796_CLK_S3D1),
	DEF_MOD("hscif1",		 519,	R8A7796_CLK_S3D1),
	DEF_MOD("hscif0",		 520,	R8A7796_CLK_S3D1),
	DEF_MOD("thermal",		 522,	R8A7796_CLK_CP),
	DEF_MOD("fcpvd2",		 601,	R8A7796_CLK_S0D2),
	DEF_MOD("fcpvd1",		 602,	R8A7796_CLK_S0D2),
	DEF_MOD("fcpvd0",		 603,	R8A7796_CLK_S0D2),
	DEF_MOD("fcpvb0",		 607,	R8A7796_CLK_S0D1),
	DEF_MOD("fcpvi0",		 611,	R8A7796_CLK_S0D1),
	DEF_MOD("fcpf0",		 615,	R8A7796_CLK_S0D1),
	DEF_MOD("fcpci0",		 617,	R8A7796_CLK_S0D2),
	DEF_MOD("fcpcs",		 619,	R8A7796_CLK_S0D2),
	DEF_MOD("vspd2",		 621,	R8A7796_CLK_S0D2),
	DEF_MOD("vspd1",		 622,	R8A7796_CLK_S0D2),
	DEF_MOD("vspd0",		 623,	R8A7796_CLK_S0D2),
	DEF_MOD("vspb",			 626,	R8A7796_CLK_S0D1),
	DEF_MOD("vspi0",		 631,	R8A7796_CLK_S0D1),
	DEF_MOD("csi20",		 714,	R8A7796_CLK_CSI0),
	DEF_MOD("csi40",		 716,	R8A7796_CLK_CSI0),
	DEF_MOD("du2",			 722,	R8A7796_CLK_S2D1),
	DEF_MOD("du1",			 723,	R8A7796_CLK_S2D1),
	DEF_MOD("du0",			 724,	R8A7796_CLK_S2D1),
	DEF_MOD("lvds",			 727,	R8A7796_CLK_S2D1),
	DEF_MOD("vin7",			 804,	R8A7796_CLK_S0D2),
	DEF_MOD("vin6",			 805,	R8A7796_CLK_S0D2),
	DEF_MOD("vin5",			 806,	R8A7796_CLK_S0D2),
	DEF_MOD("vin4",			 807,	R8A7796_CLK_S0D2),
	DEF_MOD("vin3",			 808,	R8A7796_CLK_S0D2),
	DEF_MOD("vin2",			 809,	R8A7796_CLK_S0D2),
	DEF_MOD("vin1",			 810,	R8A7796_CLK_S0D2),
	DEF_MOD("vin0",			 811,	R8A7796_CLK_S0D2),
	DEF_MOD("etheravb",		 812,	R8A7796_CLK_S0D6),
	DEF_MOD("imr1",			 822,	R8A7796_CLK_S0D2),
	DEF_MOD("imr0",			 823,	R8A7796_CLK_S0D2),
	DEF_MOD("gpio7",		 905,	R8A7796_CLK_S3D4),
	DEF_MOD("gpio6",		 906,	R8A7796_CLK_S3D4),
	DEF_MOD("gpio5",		 907,	R8A7796_CLK_S3D4),
	DEF_MOD("gpio4",		 908,	R8A7796_CLK_S3D4),
	DEF_MOD("gpio3",		 909,	R8A7796_CLK_S3D4),
	DEF_MOD("gpio2",		 910,	R8A7796_CLK_S3D4),
	DEF_MOD("gpio1",		 911,	R8A7796_CLK_S3D4),
	DEF_MOD("gpio0",		 912,	R8A7796_CLK_S3D4),
	DEF_MOD("can-fd",		 914,	R8A7796_CLK_S3D2),
	DEF_MOD("can-if1",		 915,	R8A7796_CLK_S3D4),
	DEF_MOD("can-if0",		 916,	R8A7796_CLK_S3D4),
	DEF_MOD("i2c6",			 918,	R8A7796_CLK_S0D6),
	DEF_MOD("i2c5",			 919,	R8A7796_CLK_S0D6),
	DEF_MOD("i2c-dvfs",		 926,	R8A7796_CLK_CP),
	DEF_MOD("i2c4",			 927,	R8A7796_CLK_S0D6),
	DEF_MOD("i2c3",			 928,	R8A7796_CLK_S0D6),
	DEF_MOD("i2c2",			 929,	R8A7796_CLK_S3D2),
	DEF_MOD("i2c1",			 930,	R8A7796_CLK_S3D2),
	DEF_MOD("i2c0",			 931,	R8A7796_CLK_S3D2),
};

static const unsigned int r8a7796_crit_mod_clks[] __initconst = {
	MOD_CLK_ID(408),	/* INTC-AP (GIC) */
};


/*
 * CPG Clock Data
 */

/*
 *   MD		EXTAL		PLL0	PLL1	PLL2	PLL3	PLL4
 * 14 13 19 17	(MHz)
 *-------------------------------------------------------------------
 * 0  0  0  0	16.66 x 1	x180	x192	x144	x192	x144
 * 0  0  0  1	16.66 x 1	x180	x192	x144	x128	x144
 * 0  0  1  0	Prohibited setting
 * 0  0  1  1	16.66 x 1	x180	x192	x144	x192	x144
 * 0  1  0  0	20    x 1	x150	x160	x120	x160	x120
 * 0  1  0  1	20    x 1	x150	x160	x120	x106	x120
 * 0  1  1  0	Prohibited setting
 * 0  1  1  1	20    x 1	x150	x160	x120	x160	x120
 * 1  0  0  0	25    x 1	x120	x128	x96	x128	x96
 * 1  0  0  1	25    x 1	x120	x128	x96	x84	x96
 * 1  0  1  0	Prohibited setting
 * 1  0  1  1	25    x 1	x120	x128	x96	x128	x96
 * 1  1  0  0	33.33 / 2	x180	x192	x144	x192	x144
 * 1  1  0  1	33.33 / 2	x180	x192	x144	x128	x144
 * 1  1  1  0	Prohibited setting
 * 1  1  1  1	33.33 / 2	x180	x192	x144	x192	x144
 */
#define CPG_PLL_CONFIG_INDEX(md)	((((md) & BIT(14)) >> 11) | \
					 (((md) & BIT(13)) >> 11) | \
					 (((md) & BIT(19)) >> 18) | \
					 (((md) & BIT(17)) >> 17))

static const struct rcar_gen3_cpg_pll_config cpg_pll_configs[16] __initconst = {
	/* EXTAL div	PLL1 mult	PLL3 mult */
	{ 1,		192,		192,	},
	{ 1,		192,		128,	},
	{ 0, /* Prohibited setting */		},
	{ 1,		192,		192,	},
	{ 1,		160,		160,	},
	{ 1,		160,		106,	},
	{ 0, /* Prohibited setting */		},
	{ 1,		160,		160,	},
	{ 1,		128,		128,	},
	{ 1,		128,		84,	},
	{ 0, /* Prohibited setting */		},
	{ 1,		128,		128,	},
	{ 2,		192,		192,	},
	{ 2,		192,		128,	},
	{ 0, /* Prohibited setting */		},
	{ 2,		192,		192,	},
};

static int __init r8a7796_cpg_mssr_init(struct device *dev)
{
	const struct rcar_gen3_cpg_pll_config *cpg_pll_config;
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

	return rcar_gen3_cpg_init(cpg_pll_config, CLK_EXTALR, cpg_mode);
}

const struct cpg_mssr_info r8a7796_cpg_mssr_info __initconst = {
	/* Core Clocks */
	.core_clks = r8a7796_core_clks,
	.num_core_clks = ARRAY_SIZE(r8a7796_core_clks),
	.last_dt_core_clk = LAST_DT_CORE_CLK,
	.num_total_core_clks = MOD_CLK_BASE,

	/* Module Clocks */
	.mod_clks = r8a7796_mod_clks,
	.num_mod_clks = ARRAY_SIZE(r8a7796_mod_clks),
	.num_hw_mod_clks = 12 * 32,

	/* Critical Module Clocks */
	.crit_mod_clks = r8a7796_crit_mod_clks,
	.num_crit_mod_clks = ARRAY_SIZE(r8a7796_crit_mod_clks),

	/* Callbacks */
	.init = r8a7796_cpg_mssr_init,
	.cpg_clk_register = rcar_gen3_cpg_clk_register,
};
