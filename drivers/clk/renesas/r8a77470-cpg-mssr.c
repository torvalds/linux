// SPDX-License-Identifier: GPL-2.0
/*
 * r8a77470 Clock Pulse Generator / Module Standby and Software Reset
 *
 * Copyright (C) 2018 Renesas Electronics Corp.
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/soc/renesas/rcar-rst.h>

#include <dt-bindings/clock/r8a77470-cpg-mssr.h>

#include "renesas-cpg-mssr.h"
#include "rcar-gen2-cpg.h"

enum clk_ids {
	/* Core Clock Outputs exported to DT */
	LAST_DT_CORE_CLK = R8A77470_CLK_OSC,

	/* External Input Clocks */
	CLK_EXTAL,
	CLK_USB_EXTAL,

	/* Internal Core Clocks */
	CLK_MAIN,
	CLK_PLL0,
	CLK_PLL1,
	CLK_PLL3,
	CLK_PLL1_DIV2,

	/* Module Clocks */
	MOD_CLK_BASE
};

static const struct cpg_core_clk r8a77470_core_clks[] __initconst = {
	/* External Clock Inputs */
	DEF_INPUT("extal",	CLK_EXTAL),
	DEF_INPUT("usb_extal",	CLK_USB_EXTAL),

	/* Internal Core Clocks */
	DEF_BASE(".main",	CLK_MAIN, CLK_TYPE_GEN2_MAIN, CLK_EXTAL),
	DEF_BASE(".pll0",	CLK_PLL0, CLK_TYPE_GEN2_PLL0, CLK_MAIN),
	DEF_BASE(".pll1",	CLK_PLL1, CLK_TYPE_GEN2_PLL1, CLK_MAIN),
	DEF_BASE(".pll3",	CLK_PLL3, CLK_TYPE_GEN2_PLL3, CLK_MAIN),

	DEF_FIXED(".pll1_div2",	CLK_PLL1_DIV2, CLK_PLL1, 2, 1),

	/* Core Clock Outputs */
	DEF_BASE("sdh",  R8A77470_CLK_SDH,  CLK_TYPE_GEN2_SDH,	CLK_PLL1),
	DEF_BASE("sd0",  R8A77470_CLK_SD0,  CLK_TYPE_GEN2_SD0,	CLK_PLL1),
	DEF_BASE("sd1",  R8A77470_CLK_SD1,  CLK_TYPE_GEN2_SD1,	CLK_PLL1),
	DEF_BASE("qspi", R8A77470_CLK_QSPI, CLK_TYPE_GEN2_QSPI,	CLK_PLL1_DIV2),
	DEF_BASE("rcan", R8A77470_CLK_RCAN, CLK_TYPE_GEN2_RCAN,	CLK_USB_EXTAL),

	DEF_FIXED("z2",    R8A77470_CLK_Z2,	CLK_PLL0,	    1, 1),
	DEF_FIXED("zx",    R8A77470_CLK_ZX,	CLK_PLL1,	    3, 1),
	DEF_FIXED("zs",    R8A77470_CLK_ZS,	CLK_PLL1,	    6, 1),
	DEF_FIXED("hp",    R8A77470_CLK_HP,	CLK_PLL1,	   12, 1),
	DEF_FIXED("b",     R8A77470_CLK_B,	CLK_PLL1,	   12, 1),
	DEF_FIXED("lb",	   R8A77470_CLK_LB,     CLK_PLL1,	   24, 1),
	DEF_FIXED("p",     R8A77470_CLK_P,	CLK_PLL1,	   24, 1),
	DEF_FIXED("cl",    R8A77470_CLK_CL,	CLK_PLL1,	   48, 1),
	DEF_FIXED("cp",    R8A77470_CLK_CP,	CLK_PLL1,	   48, 1),
	DEF_FIXED("m2",    R8A77470_CLK_M2,	CLK_PLL1,	    8, 1),
	DEF_FIXED("zb3",   R8A77470_CLK_ZB3,	CLK_PLL3,	    4, 1),
	DEF_FIXED("mp",    R8A77470_CLK_MP,	CLK_PLL1_DIV2,	   15, 1),
	DEF_FIXED("cpex",  R8A77470_CLK_CPEX,	CLK_EXTAL,	    2, 1),
	DEF_FIXED("r",     R8A77470_CLK_R,	CLK_PLL1,	49152, 1),
	DEF_FIXED("osc",   R8A77470_CLK_OSC,	CLK_PLL1,	12288, 1),

	DEF_DIV6P1("sd2",  R8A77470_CLK_SD2,	CLK_PLL1_DIV2,	0x078),
};

static const struct mssr_mod_clk r8a77470_mod_clks[] __initconst = {
	DEF_MOD("msiof0",		   0,	R8A77470_CLK_MP),
	DEF_MOD("vcp0",			 101,	R8A77470_CLK_ZS),
	DEF_MOD("vpc0",			 103,	R8A77470_CLK_ZS),
	DEF_MOD("tmu1",			 111,	R8A77470_CLK_P),
	DEF_MOD("3dg",			 112,	R8A77470_CLK_ZS),
	DEF_MOD("2d-dmac",		 115,	R8A77470_CLK_ZS),
	DEF_MOD("fdp1-0",		 119,	R8A77470_CLK_ZS),
	DEF_MOD("tmu3",			 121,	R8A77470_CLK_P),
	DEF_MOD("tmu2",			 122,	R8A77470_CLK_P),
	DEF_MOD("cmt0",			 124,	R8A77470_CLK_R),
	DEF_MOD("vsp1du0",		 128,	R8A77470_CLK_ZS),
	DEF_MOD("vsp1-sy",		 131,	R8A77470_CLK_ZS),
	DEF_MOD("msiof2",		 205,	R8A77470_CLK_MP),
	DEF_MOD("msiof1",		 208,	R8A77470_CLK_MP),
	DEF_MOD("sys-dmac1",		 218,	R8A77470_CLK_ZS),
	DEF_MOD("sys-dmac0",		 219,	R8A77470_CLK_ZS),
	DEF_MOD("sdhi2",		 312,	R8A77470_CLK_SD2),
	DEF_MOD("sdhi1",		 313,	R8A77470_CLK_SD1),
	DEF_MOD("sdhi0",		 314,	R8A77470_CLK_SD0),
	DEF_MOD("usbhs-dmac0-ch1",	 326,	R8A77470_CLK_HP),
	DEF_MOD("usbhs-dmac1-ch1",	 327,	R8A77470_CLK_HP),
	DEF_MOD("cmt1",			 329,	R8A77470_CLK_R),
	DEF_MOD("usbhs-dmac0-ch0",	 330,	R8A77470_CLK_HP),
	DEF_MOD("usbhs-dmac1-ch0",	 331,	R8A77470_CLK_HP),
	DEF_MOD("rwdt",			 402,	R8A77470_CLK_R),
	DEF_MOD("irqc",			 407,	R8A77470_CLK_CP),
	DEF_MOD("intc-sys",		 408,	R8A77470_CLK_ZS),
	DEF_MOD("audio-dmac0",		 502,	R8A77470_CLK_HP),
	DEF_MOD("pwm",			 523,	R8A77470_CLK_P),
	DEF_MOD("usb-ehci-0",		 703,	R8A77470_CLK_MP),
	DEF_MOD("usbhs-0",		 704,	R8A77470_CLK_HP),
	DEF_MOD("usb-ehci-1",		 705,	R8A77470_CLK_MP),
	DEF_MOD("usbhs-1",		 706,	R8A77470_CLK_HP),
	DEF_MOD("hscif2",		 713,	R8A77470_CLK_ZS),
	DEF_MOD("scif5",		 714,	R8A77470_CLK_P),
	DEF_MOD("scif4",		 715,	R8A77470_CLK_P),
	DEF_MOD("hscif1",		 716,	R8A77470_CLK_ZS),
	DEF_MOD("hscif0",		 717,	R8A77470_CLK_ZS),
	DEF_MOD("scif3",		 718,	R8A77470_CLK_P),
	DEF_MOD("scif2",		 719,	R8A77470_CLK_P),
	DEF_MOD("scif1",		 720,	R8A77470_CLK_P),
	DEF_MOD("scif0",		 721,	R8A77470_CLK_P),
	DEF_MOD("du1",			 723,	R8A77470_CLK_ZX),
	DEF_MOD("du0",			 724,	R8A77470_CLK_ZX),
	DEF_MOD("ipmmu-sgx",		 800,	R8A77470_CLK_ZX),
	DEF_MOD("etheravb",		 812,	R8A77470_CLK_HP),
	DEF_MOD("ether",		 813,	R8A77470_CLK_P),
	DEF_MOD("gpio5",		 907,	R8A77470_CLK_CP),
	DEF_MOD("gpio4",		 908,	R8A77470_CLK_CP),
	DEF_MOD("gpio3",		 909,	R8A77470_CLK_CP),
	DEF_MOD("gpio2",		 910,	R8A77470_CLK_CP),
	DEF_MOD("gpio1",		 911,	R8A77470_CLK_CP),
	DEF_MOD("gpio0",		 912,	R8A77470_CLK_CP),
	DEF_MOD("can1",			 915,	R8A77470_CLK_P),
	DEF_MOD("can0",			 916,	R8A77470_CLK_P),
	DEF_MOD("qspi_mod-1",		 917,	R8A77470_CLK_QSPI),
	DEF_MOD("qspi_mod-0",		 918,	R8A77470_CLK_QSPI),
	DEF_MOD("i2c4",			 927,	R8A77470_CLK_HP),
	DEF_MOD("i2c3",			 928,	R8A77470_CLK_HP),
	DEF_MOD("i2c2",			 929,	R8A77470_CLK_HP),
	DEF_MOD("i2c1",			 930,	R8A77470_CLK_HP),
	DEF_MOD("i2c0",			 931,	R8A77470_CLK_HP),
	DEF_MOD("ssi-all",		1005,	R8A77470_CLK_P),
	DEF_MOD("ssi9",			1006,	MOD_CLK_ID(1005)),
	DEF_MOD("ssi8",			1007,	MOD_CLK_ID(1005)),
	DEF_MOD("ssi7",			1008,	MOD_CLK_ID(1005)),
	DEF_MOD("ssi6",			1009,	MOD_CLK_ID(1005)),
	DEF_MOD("ssi5",			1010,	MOD_CLK_ID(1005)),
	DEF_MOD("ssi4",			1011,	MOD_CLK_ID(1005)),
	DEF_MOD("ssi3",			1012,	MOD_CLK_ID(1005)),
	DEF_MOD("ssi2",			1013,	MOD_CLK_ID(1005)),
	DEF_MOD("ssi1",			1014,	MOD_CLK_ID(1005)),
	DEF_MOD("ssi0",			1015,	MOD_CLK_ID(1005)),
	DEF_MOD("scu-all",		1017,	R8A77470_CLK_P),
	DEF_MOD("scu-dvc1",		1018,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-dvc0",		1019,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-ctu1-mix1",	1020,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-ctu0-mix0",	1021,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-src6",		1025,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-src5",		1026,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-src4",		1027,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-src3",		1028,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-src2",		1029,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-src1",		1030,	MOD_CLK_ID(1017)),
};

static const unsigned int r8a77470_crit_mod_clks[] __initconst = {
	MOD_CLK_ID(402),	/* RWDT */
	MOD_CLK_ID(408),	/* INTC-SYS (GIC) */
};

/*
 * CPG Clock Data
 */

/*
 *    MD	EXTAL		PLL0	PLL1	PLL3
 * 14 13	(MHz)		*1	*2
 *---------------------------------------------------
 * 0  0		20		x80	x78	x50
 * 0  1		26		x60	x60	x56
 * 1  0		Prohibitted setting
 * 1  1		30		x52	x52	x50
 *
 * *1 :	Table 7.4 indicates VCO output (PLL0 = VCO)
 * *2 :	Table 7.4 indicates VCO output (PLL1 = VCO)
 */
#define CPG_PLL_CONFIG_INDEX(md)	((((md) & BIT(14)) >> 13) | \
					 (((md) & BIT(13)) >> 13))

static const struct rcar_gen2_cpg_pll_config cpg_pll_configs[4] __initconst = {
	/* EXTAL div	PLL1 mult x2	PLL3 mult */
	{ 1,		156,		50,	},
	{ 1,		120,		56,	},
	{ /* Invalid*/				},
	{ 1,		104,		50,	},
};

static int __init r8a77470_cpg_mssr_init(struct device *dev)
{
	const struct rcar_gen2_cpg_pll_config *cpg_pll_config;
	u32 cpg_mode;
	int error;

	error = rcar_rst_read_mode_pins(&cpg_mode);
	if (error)
		return error;

	cpg_pll_config = &cpg_pll_configs[CPG_PLL_CONFIG_INDEX(cpg_mode)];

	return rcar_gen2_cpg_init(cpg_pll_config, 2, cpg_mode);
}

const struct cpg_mssr_info r8a77470_cpg_mssr_info __initconst = {
	/* Core Clocks */
	.core_clks = r8a77470_core_clks,
	.num_core_clks = ARRAY_SIZE(r8a77470_core_clks),
	.last_dt_core_clk = LAST_DT_CORE_CLK,
	.num_total_core_clks = MOD_CLK_BASE,

	/* Module Clocks */
	.mod_clks = r8a77470_mod_clks,
	.num_mod_clks = ARRAY_SIZE(r8a77470_mod_clks),
	.num_hw_mod_clks = 12 * 32,

	/* Critical Module Clocks */
	.crit_mod_clks = r8a77470_crit_mod_clks,
	.num_crit_mod_clks = ARRAY_SIZE(r8a77470_crit_mod_clks),

	/* Callbacks */
	.init = r8a77470_cpg_mssr_init,
	.cpg_clk_register = rcar_gen2_cpg_clk_register,
};
