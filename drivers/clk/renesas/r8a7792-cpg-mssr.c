// SPDX-License-Identifier: GPL-2.0
/*
 * r8a7792 Clock Pulse Generator / Module Standby and Software Reset
 *
 * Copyright (C) 2017 Glider bvba
 *
 * Based on clk-rcar-gen2.c
 *
 * Copyright (C) 2013 Ideas On Board SPRL
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/soc/renesas/rcar-rst.h>

#include <dt-bindings/clock/r8a7792-cpg-mssr.h>

#include "renesas-cpg-mssr.h"
#include "rcar-gen2-cpg.h"

enum clk_ids {
	/* Core Clock Outputs exported to DT */
	LAST_DT_CORE_CLK = R8A7792_CLK_OSC,

	/* External Input Clocks */
	CLK_EXTAL,

	/* Internal Core Clocks */
	CLK_MAIN,
	CLK_PLL0,
	CLK_PLL1,
	CLK_PLL3,
	CLK_PLL1_DIV2,

	/* Module Clocks */
	MOD_CLK_BASE
};

static const struct cpg_core_clk r8a7792_core_clks[] __initconst = {
	/* External Clock Inputs */
	DEF_INPUT("extal",     CLK_EXTAL),

	/* Internal Core Clocks */
	DEF_BASE(".main",       CLK_MAIN, CLK_TYPE_GEN2_MAIN, CLK_EXTAL),
	DEF_BASE(".pll0",       CLK_PLL0, CLK_TYPE_GEN2_PLL0, CLK_MAIN),
	DEF_BASE(".pll1",       CLK_PLL1, CLK_TYPE_GEN2_PLL1, CLK_MAIN),
	DEF_BASE(".pll3",       CLK_PLL3, CLK_TYPE_GEN2_PLL3, CLK_MAIN),

	DEF_FIXED(".pll1_div2", CLK_PLL1_DIV2, CLK_PLL1, 2, 1),

	/* Core Clock Outputs */
	DEF_BASE("qspi", R8A7792_CLK_QSPI, CLK_TYPE_GEN2_QSPI, CLK_PLL1_DIV2),

	DEF_FIXED("z",      R8A7792_CLK_Z,     CLK_PLL0,          1, 1),
	DEF_FIXED("zg",     R8A7792_CLK_ZG,    CLK_PLL1,          5, 1),
	DEF_FIXED("zx",     R8A7792_CLK_ZX,    CLK_PLL1,          3, 1),
	DEF_FIXED("zs",     R8A7792_CLK_ZS,    CLK_PLL1,          6, 1),
	DEF_FIXED("hp",     R8A7792_CLK_HP,    CLK_PLL1,         12, 1),
	DEF_FIXED("i",      R8A7792_CLK_I,     CLK_PLL1,          3, 1),
	DEF_FIXED("b",      R8A7792_CLK_B,     CLK_PLL1,         12, 1),
	DEF_FIXED("lb",     R8A7792_CLK_LB,    CLK_PLL1,         24, 1),
	DEF_FIXED("p",      R8A7792_CLK_P,     CLK_PLL1,         24, 1),
	DEF_FIXED("cl",     R8A7792_CLK_CL,    CLK_PLL1,         48, 1),
	DEF_FIXED("m2",     R8A7792_CLK_M2,    CLK_PLL1,          8, 1),
	DEF_FIXED("imp",    R8A7792_CLK_IMP,   CLK_PLL1,          4, 1),
	DEF_FIXED("zb3",    R8A7792_CLK_ZB3,   CLK_PLL3,          4, 1),
	DEF_FIXED("zb3d2",  R8A7792_CLK_ZB3D2, CLK_PLL3,          8, 1),
	DEF_FIXED("ddr",    R8A7792_CLK_DDR,   CLK_PLL3,          8, 1),
	DEF_FIXED("sd",     R8A7792_CLK_SD,    CLK_PLL1_DIV2,     8, 1),
	DEF_FIXED("mp",     R8A7792_CLK_MP,    CLK_PLL1_DIV2,    15, 1),
	DEF_FIXED("cp",     R8A7792_CLK_CP,    CLK_PLL1,         48, 1),
	DEF_FIXED("cpex",   R8A7792_CLK_CPEX,  CLK_EXTAL,         2, 1),
	DEF_FIXED("rcan",   R8A7792_CLK_RCAN,  CLK_PLL1_DIV2,    49, 1),
	DEF_FIXED("r",      R8A7792_CLK_R,     CLK_PLL1,      49152, 1),
	DEF_FIXED("osc",    R8A7792_CLK_OSC,   CLK_PLL1,      12288, 1),
};

static const struct mssr_mod_clk r8a7792_mod_clks[] __initconst = {
	DEF_MOD("msiof0",		   0,	R8A7792_CLK_MP),
	DEF_MOD("jpu",			 106,	R8A7792_CLK_M2),
	DEF_MOD("tmu1",			 111,	R8A7792_CLK_P),
	DEF_MOD("3dg",			 112,	R8A7792_CLK_ZG),
	DEF_MOD("2d-dmac",		 115,	R8A7792_CLK_ZS),
	DEF_MOD("tmu3",			 121,	R8A7792_CLK_P),
	DEF_MOD("tmu2",			 122,	R8A7792_CLK_P),
	DEF_MOD("cmt0",			 124,	R8A7792_CLK_R),
	DEF_MOD("tmu0",			 125,	R8A7792_CLK_CP),
	DEF_MOD("vsp1du1",		 127,	R8A7792_CLK_ZS),
	DEF_MOD("vsp1du0",		 128,	R8A7792_CLK_ZS),
	DEF_MOD("vsps",			 131,	R8A7792_CLK_ZS),
	DEF_MOD("msiof1",		 208,	R8A7792_CLK_MP),
	DEF_MOD("sys-dmac1",		 218,	R8A7792_CLK_ZS),
	DEF_MOD("sys-dmac0",		 219,	R8A7792_CLK_ZS),
	DEF_MOD("tpu0",			 304,	R8A7792_CLK_CP),
	DEF_MOD("sdhi0",		 314,	R8A7792_CLK_SD),
	DEF_MOD("cmt1",			 329,	R8A7792_CLK_R),
	DEF_MOD("rwdt",			 402,	R8A7792_CLK_R),
	DEF_MOD("irqc",			 407,	R8A7792_CLK_CP),
	DEF_MOD("intc-sys",		 408,	R8A7792_CLK_ZS),
	DEF_MOD("audio-dmac0",		 502,	R8A7792_CLK_HP),
	DEF_MOD("thermal",		 522,	CLK_EXTAL),
	DEF_MOD("pwm",			 523,	R8A7792_CLK_P),
	DEF_MOD("hscif1",		 716,	R8A7792_CLK_ZS),
	DEF_MOD("hscif0",		 717,	R8A7792_CLK_ZS),
	DEF_MOD("scif3",		 718,	R8A7792_CLK_P),
	DEF_MOD("scif2",		 719,	R8A7792_CLK_P),
	DEF_MOD("scif1",		 720,	R8A7792_CLK_P),
	DEF_MOD("scif0",		 721,	R8A7792_CLK_P),
	DEF_MOD("du1",			 723,	R8A7792_CLK_ZX),
	DEF_MOD("du0",			 724,	R8A7792_CLK_ZX),
	DEF_MOD("vin5",			 804,	R8A7792_CLK_ZG),
	DEF_MOD("vin4",			 805,	R8A7792_CLK_ZG),
	DEF_MOD("vin3",			 808,	R8A7792_CLK_ZG),
	DEF_MOD("vin2",			 809,	R8A7792_CLK_ZG),
	DEF_MOD("vin1",			 810,	R8A7792_CLK_ZG),
	DEF_MOD("vin0",			 811,	R8A7792_CLK_ZG),
	DEF_MOD("etheravb",		 812,	R8A7792_CLK_HP),
	DEF_MOD("imr-lx3",		 821,	R8A7792_CLK_ZG),
	DEF_MOD("imr-lsx3-1",		 822,	R8A7792_CLK_ZG),
	DEF_MOD("imr-lsx3-0",		 823,	R8A7792_CLK_ZG),
	DEF_MOD("imr-lsx3-5",		 825,	R8A7792_CLK_ZG),
	DEF_MOD("imr-lsx3-4",		 826,	R8A7792_CLK_ZG),
	DEF_MOD("imr-lsx3-3",		 827,	R8A7792_CLK_ZG),
	DEF_MOD("imr-lsx3-2",		 828,	R8A7792_CLK_ZG),
	DEF_MOD("gyro-adc",		 901,	R8A7792_CLK_P),
	DEF_MOD("gpio7",		 904,	R8A7792_CLK_CP),
	DEF_MOD("gpio6",		 905,	R8A7792_CLK_CP),
	DEF_MOD("gpio5",		 907,	R8A7792_CLK_CP),
	DEF_MOD("gpio4",		 908,	R8A7792_CLK_CP),
	DEF_MOD("gpio3",		 909,	R8A7792_CLK_CP),
	DEF_MOD("gpio2",		 910,	R8A7792_CLK_CP),
	DEF_MOD("gpio1",		 911,	R8A7792_CLK_CP),
	DEF_MOD("gpio0",		 912,	R8A7792_CLK_CP),
	DEF_MOD("gpio11",		 913,	R8A7792_CLK_CP),
	DEF_MOD("gpio10",		 914,	R8A7792_CLK_CP),
	DEF_MOD("can1",			 915,	R8A7792_CLK_P),
	DEF_MOD("can0",			 916,	R8A7792_CLK_P),
	DEF_MOD("qspi_mod",		 917,	R8A7792_CLK_QSPI),
	DEF_MOD("gpio9",		 919,	R8A7792_CLK_CP),
	DEF_MOD("gpio8",		 921,	R8A7792_CLK_CP),
	DEF_MOD("i2c5",			 925,	R8A7792_CLK_HP),
	DEF_MOD("iicdvfs",		 926,	R8A7792_CLK_CP),
	DEF_MOD("i2c4",			 927,	R8A7792_CLK_HP),
	DEF_MOD("i2c3",			 928,	R8A7792_CLK_HP),
	DEF_MOD("i2c2",			 929,	R8A7792_CLK_HP),
	DEF_MOD("i2c1",			 930,	R8A7792_CLK_HP),
	DEF_MOD("i2c0",			 931,	R8A7792_CLK_HP),
	DEF_MOD("ssi-all",		1005,	R8A7792_CLK_P),
	DEF_MOD("ssi4",			1011,	MOD_CLK_ID(1005)),
	DEF_MOD("ssi3",			1012,	MOD_CLK_ID(1005)),
};

static const unsigned int r8a7792_crit_mod_clks[] __initconst = {
	MOD_CLK_ID(402),	/* RWDT */
	MOD_CLK_ID(408),	/* INTC-SYS (GIC) */
};

/*
 * CPG Clock Data
 */

/*
 *   MD		EXTAL		PLL0	PLL1	PLL3
 * 14 13 19	(MHz)		*1	*2
 *---------------------------------------------------
 * 0  0  0	15		x200/3	x208/2	x106
 * 0  0  1	15		x200/3	x208/2	x88
 * 0  1  0	20		x150/3	x156/2	x80
 * 0  1  1	20		x150/3	x156/2	x66
 * 1  0  0	26 / 2		x230/3	x240/2	x122
 * 1  0  1	26 / 2		x230/3	x240/2	x102
 * 1  1  0	30 / 2		x200/3	x208/2	x106
 * 1  1  1	30 / 2		x200/3	x208/2	x88
 *
 * *1 :	Table 7.5b indicates VCO output (PLL0 = VCO/3)
 * *2 :	Table 7.5b indicates VCO output (PLL1 = VCO/2)
 */
#define CPG_PLL_CONFIG_INDEX(md)	((((md) & BIT(14)) >> 12) | \
					 (((md) & BIT(13)) >> 12) | \
					 (((md) & BIT(19)) >> 19))
static const struct rcar_gen2_cpg_pll_config cpg_pll_configs[8] __initconst = {
	{ 1, 208, 106, 200 },
	{ 1, 208,  88, 200 },
	{ 1, 156,  80, 150 },
	{ 1, 156,  66, 150 },
	{ 2, 240, 122, 230 },
	{ 2, 240, 102, 230 },
	{ 2, 208, 106, 200 },
	{ 2, 208,  88, 200 },
};

static int __init r8a7792_cpg_mssr_init(struct device *dev)
{
	const struct rcar_gen2_cpg_pll_config *cpg_pll_config;
	u32 cpg_mode;
	int error;

	error = rcar_rst_read_mode_pins(&cpg_mode);
	if (error)
		return error;

	cpg_pll_config = &cpg_pll_configs[CPG_PLL_CONFIG_INDEX(cpg_mode)];

	return rcar_gen2_cpg_init(cpg_pll_config, 3, cpg_mode);
}

const struct cpg_mssr_info r8a7792_cpg_mssr_info __initconst = {
	/* Core Clocks */
	.core_clks = r8a7792_core_clks,
	.num_core_clks = ARRAY_SIZE(r8a7792_core_clks),
	.last_dt_core_clk = LAST_DT_CORE_CLK,
	.num_total_core_clks = MOD_CLK_BASE,

	/* Module Clocks */
	.mod_clks = r8a7792_mod_clks,
	.num_mod_clks = ARRAY_SIZE(r8a7792_mod_clks),
	.num_hw_mod_clks = 12 * 32,

	/* Critical Module Clocks */
	.crit_mod_clks = r8a7792_crit_mod_clks,
	.num_crit_mod_clks = ARRAY_SIZE(r8a7792_crit_mod_clks),

	/* Callbacks */
	.init = r8a7792_cpg_mssr_init,
	.cpg_clk_register = rcar_gen2_cpg_clk_register,
};
