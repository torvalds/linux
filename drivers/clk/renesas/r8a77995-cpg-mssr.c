/*
 * r8a77995 Clock Pulse Generator / Module Standby and Software Reset
 *
 * Copyright (C) 2017 Glider bvba
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

#include <dt-bindings/clock/r8a77995-cpg-mssr.h>

#include "renesas-cpg-mssr.h"
#include "rcar-gen3-cpg.h"

enum clk_ids {
	/* Core Clock Outputs exported to DT */
	LAST_DT_CORE_CLK = R8A77995_CLK_CP,

	/* External Input Clocks */
	CLK_EXTAL,

	/* Internal Core Clocks */
	CLK_MAIN,
	CLK_PLL0,
	CLK_PLL1,
	CLK_PLL3,
	CLK_PLL0D2,
	CLK_PLL0D3,
	CLK_PLL0D5,
	CLK_PLL1D2,
	CLK_PE,
	CLK_S0,
	CLK_S1,
	CLK_S2,
	CLK_S3,
	CLK_SDSRC,
	CLK_SSPSRC,

	/* Module Clocks */
	MOD_CLK_BASE
};

static const struct cpg_core_clk r8a77995_core_clks[] __initconst = {
	/* External Clock Inputs */
	DEF_INPUT("extal",     CLK_EXTAL),

	/* Internal Core Clocks */
	DEF_BASE(".main",      CLK_MAIN, CLK_TYPE_GEN3_MAIN,       CLK_EXTAL),
	DEF_BASE(".pll1",      CLK_PLL1, CLK_TYPE_GEN3_PLL1,       CLK_MAIN),
	DEF_BASE(".pll3",      CLK_PLL3, CLK_TYPE_GEN3_PLL3,       CLK_MAIN),

	DEF_FIXED(".pll0",     CLK_PLL0,           CLK_MAIN,	   4, 250),
	DEF_FIXED(".pll0d2",   CLK_PLL0D2,         CLK_PLL0,       2, 1),
	DEF_FIXED(".pll0d3",   CLK_PLL0D3,         CLK_PLL0,       3, 1),
	DEF_FIXED(".pll0d5",   CLK_PLL0D5,         CLK_PLL0,       5, 1),
	DEF_FIXED(".pll1d2",   CLK_PLL1D2,         CLK_PLL1,       2, 1),
	DEF_FIXED(".pe",       CLK_PE,             CLK_PLL0D3,     4, 1),
	DEF_FIXED(".s0",       CLK_S0,             CLK_PLL1,       2, 1),
	DEF_FIXED(".s1",       CLK_S1,             CLK_PLL1,       3, 1),
	DEF_FIXED(".s2",       CLK_S2,             CLK_PLL1,       4, 1),
	DEF_FIXED(".s3",       CLK_S3,             CLK_PLL1,       6, 1),
	DEF_FIXED(".sdsrc",    CLK_SDSRC,          CLK_PLL1,       2, 1),

	/* Core Clock Outputs */
	DEF_FIXED("z2",        R8A77995_CLK_Z2,    CLK_PLL0D3,     1, 1),
	DEF_FIXED("ztr",       R8A77995_CLK_ZTR,   CLK_PLL1,       6, 1),
	DEF_FIXED("zt",        R8A77995_CLK_ZT,    CLK_PLL1,       4, 1),
	DEF_FIXED("zx",        R8A77995_CLK_ZX,    CLK_PLL1,       3, 1),
	DEF_FIXED("s0d1",      R8A77995_CLK_S0D1,  CLK_S0,         1, 1),
	DEF_FIXED("s1d1",      R8A77995_CLK_S1D1,  CLK_S1,         1, 1),
	DEF_FIXED("s1d2",      R8A77995_CLK_S1D2,  CLK_S1,         2, 1),
	DEF_FIXED("s1d4",      R8A77995_CLK_S1D4,  CLK_S1,         4, 1),
	DEF_FIXED("s2d1",      R8A77995_CLK_S2D1,  CLK_S2,         1, 1),
	DEF_FIXED("s2d2",      R8A77995_CLK_S2D2,  CLK_S2,         2, 1),
	DEF_FIXED("s2d4",      R8A77995_CLK_S2D4,  CLK_S2,         4, 1),
	DEF_FIXED("s3d1",      R8A77995_CLK_S3D1,  CLK_S3,         1, 1),
	DEF_FIXED("s3d2",      R8A77995_CLK_S3D2,  CLK_S3,         2, 1),
	DEF_FIXED("s3d4",      R8A77995_CLK_S3D4,  CLK_S3,         4, 1),

	DEF_FIXED("cl",        R8A77995_CLK_CL,    CLK_PLL1,      48, 1),
	DEF_FIXED("cp",        R8A77995_CLK_CP,    CLK_EXTAL,      2, 1),
	DEF_FIXED("osc",       R8A77995_CLK_OSC,   CLK_EXTAL,    384, 1),
	DEF_FIXED("r",         R8A77995_CLK_R,     CLK_EXTAL,   1536, 1),

	DEF_GEN3_PE("s1d4c",   R8A77995_CLK_S1D4C, CLK_S1, 4, CLK_PE, 2),
	DEF_GEN3_PE("s3d1c",   R8A77995_CLK_S3D1C, CLK_S3, 1, CLK_PE, 1),
	DEF_GEN3_PE("s3d2c",   R8A77995_CLK_S3D2C, CLK_S3, 2, CLK_PE, 2),
	DEF_GEN3_PE("s3d4c",   R8A77995_CLK_S3D4C, CLK_S3, 4, CLK_PE, 4),

	DEF_GEN3_SD("sd0",     R8A77995_CLK_SD0,   CLK_SDSRC,     0x268),

	DEF_DIV6P1("canfd",    R8A77995_CLK_CANFD, CLK_PLL0D3,    0x244),
	DEF_DIV6P1("mso",      R8A77995_CLK_MSO,   CLK_PLL1D2,    0x014),
};

static const struct mssr_mod_clk r8a77995_mod_clks[] __initconst = {
	DEF_MOD("scif5",		 202,	R8A77995_CLK_S3D4C),
	DEF_MOD("scif4",		 203,	R8A77995_CLK_S3D4C),
	DEF_MOD("scif3",		 204,	R8A77995_CLK_S3D4C),
	DEF_MOD("scif1",		 206,	R8A77995_CLK_S3D4C),
	DEF_MOD("scif0",		 207,	R8A77995_CLK_S3D4C),
	DEF_MOD("msiof3",		 208,	R8A77995_CLK_MSO),
	DEF_MOD("msiof2",		 209,	R8A77995_CLK_MSO),
	DEF_MOD("msiof1",		 210,	R8A77995_CLK_MSO),
	DEF_MOD("msiof0",		 211,	R8A77995_CLK_MSO),
	DEF_MOD("sys-dmac2",		 217,	R8A77995_CLK_S3D1),
	DEF_MOD("sys-dmac1",		 218,	R8A77995_CLK_S3D1),
	DEF_MOD("sys-dmac0",		 219,	R8A77995_CLK_S3D1),
	DEF_MOD("cmt3",			 300,	R8A77995_CLK_R),
	DEF_MOD("cmt2",			 301,	R8A77995_CLK_R),
	DEF_MOD("cmt1",			 302,	R8A77995_CLK_R),
	DEF_MOD("cmt0",			 303,	R8A77995_CLK_R),
	DEF_MOD("scif2",		 310,	R8A77995_CLK_S3D4C),
	DEF_MOD("emmc0",		 312,	R8A77995_CLK_SD0),
	DEF_MOD("usb-dmac0",		 330,	R8A77995_CLK_S3D1),
	DEF_MOD("usb-dmac1",		 331,	R8A77995_CLK_S3D1),
	DEF_MOD("rwdt",			 402,	R8A77995_CLK_R),
	DEF_MOD("intc-ex",		 407,	R8A77995_CLK_CP),
	DEF_MOD("intc-ap",		 408,	R8A77995_CLK_S3D1),
	DEF_MOD("audmac0",		 502,	R8A77995_CLK_S3D1),
	DEF_MOD("hscif3",		 517,	R8A77995_CLK_S3D1C),
	DEF_MOD("hscif0",		 520,	R8A77995_CLK_S3D1C),
	DEF_MOD("thermal",		 522,	R8A77995_CLK_CP),
	DEF_MOD("pwm",			 523,	R8A77995_CLK_S3D4C),
	DEF_MOD("fcpvd1",		 602,	R8A77995_CLK_S1D2),
	DEF_MOD("fcpvd0",		 603,	R8A77995_CLK_S1D2),
	DEF_MOD("fcpvbs",		 607,	R8A77995_CLK_S0D1),
	DEF_MOD("vspd1",		 622,	R8A77995_CLK_S1D2),
	DEF_MOD("vspd0",		 623,	R8A77995_CLK_S1D2),
	DEF_MOD("vspbs",		 627,	R8A77995_CLK_S0D1),
	DEF_MOD("ehci0",		 703,	R8A77995_CLK_S3D2),
	DEF_MOD("hsusb",		 704,	R8A77995_CLK_S3D2),
	DEF_MOD("du1",			 723,	R8A77995_CLK_S2D1),
	DEF_MOD("du0",			 724,	R8A77995_CLK_S2D1),
	DEF_MOD("lvds",			 727,	R8A77995_CLK_S2D1),
	DEF_MOD("vin7",			 804,	R8A77995_CLK_S1D2),
	DEF_MOD("vin6",			 805,	R8A77995_CLK_S1D2),
	DEF_MOD("vin5",			 806,	R8A77995_CLK_S1D2),
	DEF_MOD("vin4",			 807,	R8A77995_CLK_S1D2),
	DEF_MOD("etheravb",		 812,	R8A77995_CLK_S3D2),
	DEF_MOD("imr0",			 823,	R8A77995_CLK_S1D2),
	DEF_MOD("gpio6",		 906,	R8A77995_CLK_S3D4),
	DEF_MOD("gpio5",		 907,	R8A77995_CLK_S3D4),
	DEF_MOD("gpio4",		 908,	R8A77995_CLK_S3D4),
	DEF_MOD("gpio3",		 909,	R8A77995_CLK_S3D4),
	DEF_MOD("gpio2",		 910,	R8A77995_CLK_S3D4),
	DEF_MOD("gpio1",		 911,	R8A77995_CLK_S3D4),
	DEF_MOD("gpio0",		 912,	R8A77995_CLK_S3D4),
	DEF_MOD("can-fd",		 914,	R8A77995_CLK_S3D2),
	DEF_MOD("can-if1",		 915,	R8A77995_CLK_S3D4),
	DEF_MOD("can-if0",		 916,	R8A77995_CLK_S3D4),
	DEF_MOD("i2c3",			 928,	R8A77995_CLK_S3D2),
	DEF_MOD("i2c2",			 929,	R8A77995_CLK_S3D2),
	DEF_MOD("i2c1",			 930,	R8A77995_CLK_S3D2),
	DEF_MOD("i2c0",			 931,	R8A77995_CLK_S3D2),
	DEF_MOD("ssi-all",		1005,	R8A77995_CLK_S3D4),
	DEF_MOD("ssi4",			1011,	MOD_CLK_ID(1005)),
	DEF_MOD("ssi3",			1012,	MOD_CLK_ID(1005)),
	DEF_MOD("scu-all",		1017,	R8A77995_CLK_S3D4),
	DEF_MOD("scu-dvc1",		1018,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-dvc0",		1019,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-ctu1-mix1",	1020,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-ctu0-mix0",	1021,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-src6",		1025,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-src5",		1026,	MOD_CLK_ID(1017)),
};

static const unsigned int r8a77995_crit_mod_clks[] __initconst = {
	MOD_CLK_ID(408),	/* INTC-AP (GIC) */
};


/*
 * CPG Clock Data
 */

/*
 * MD19		EXTAL (MHz)	PLL0		PLL1		PLL3
 *--------------------------------------------------------------------
 * 0		48 x 1		x250/4		x100/3		x100/3
 * 1		48 x 1		x250/4		x100/3		x116/6
 */
#define CPG_PLL_CONFIG_INDEX(md)	(((md) & BIT(19)) >> 19)

static const struct rcar_gen3_cpg_pll_config cpg_pll_configs[2] __initconst = {
	/* EXTAL div	PLL1 mult/div	PLL3 mult/div */
	{ 1,		100,	3,	100,	3,	},
	{ 1,		100,	3,	116,	6,	},
};

static int __init r8a77995_cpg_mssr_init(struct device *dev)
{
	const struct rcar_gen3_cpg_pll_config *cpg_pll_config;
	u32 cpg_mode;
	int error;

	error = rcar_rst_read_mode_pins(&cpg_mode);
	if (error)
		return error;

	cpg_pll_config = &cpg_pll_configs[CPG_PLL_CONFIG_INDEX(cpg_mode)];

	return rcar_gen3_cpg_init(cpg_pll_config, 0, cpg_mode);
}

const struct cpg_mssr_info r8a77995_cpg_mssr_info __initconst = {
	/* Core Clocks */
	.core_clks = r8a77995_core_clks,
	.num_core_clks = ARRAY_SIZE(r8a77995_core_clks),
	.last_dt_core_clk = LAST_DT_CORE_CLK,
	.num_total_core_clks = MOD_CLK_BASE,

	/* Module Clocks */
	.mod_clks = r8a77995_mod_clks,
	.num_mod_clks = ARRAY_SIZE(r8a77995_mod_clks),
	.num_hw_mod_clks = 12 * 32,

	/* Critical Module Clocks */
	.crit_mod_clks = r8a77995_crit_mod_clks,
	.num_crit_mod_clks = ARRAY_SIZE(r8a77995_crit_mod_clks),

	/* Callbacks */
	.init = r8a77995_cpg_mssr_init,
	.cpg_clk_register = rcar_gen3_cpg_clk_register,
};
