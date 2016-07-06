/*
 * r8a7795 Clock Pulse Generator / Module Standby and Software Reset
 *
 * Copyright (C) 2015 Glider bvba
 *
 * Based on clk-rcar-gen3.c
 *
 * Copyright (C) 2015 Renesas Electronics Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <linux/bug.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/slab.h>

#include <dt-bindings/clock/r8a7795-cpg-mssr.h>

#include "renesas-cpg-mssr.h"

#define CPG_RCKCR	0x240

enum clk_ids {
	/* Core Clock Outputs exported to DT */
	LAST_DT_CORE_CLK = R8A7795_CLK_OSC,

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

enum r8a7795_clk_types {
	CLK_TYPE_GEN3_MAIN = CLK_TYPE_CUSTOM,
	CLK_TYPE_GEN3_PLL0,
	CLK_TYPE_GEN3_PLL1,
	CLK_TYPE_GEN3_PLL2,
	CLK_TYPE_GEN3_PLL3,
	CLK_TYPE_GEN3_PLL4,
	CLK_TYPE_GEN3_SD,
	CLK_TYPE_GEN3_R,
};

#define DEF_GEN3_SD(_name, _id, _parent, _offset)	\
	DEF_BASE(_name, _id, CLK_TYPE_GEN3_SD, _parent, .offset = _offset)

static const struct cpg_core_clk r8a7795_core_clks[] __initconst = {
	/* External Clock Inputs */
	DEF_INPUT("extal",  CLK_EXTAL),
	DEF_INPUT("extalr", CLK_EXTALR),

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

	/* Core Clock Outputs */
	DEF_FIXED("ztr",        R8A7795_CLK_ZTR,   CLK_PLL1_DIV2,  6, 1),
	DEF_FIXED("ztrd2",      R8A7795_CLK_ZTRD2, CLK_PLL1_DIV2, 12, 1),
	DEF_FIXED("zt",         R8A7795_CLK_ZT,    CLK_PLL1_DIV2,  4, 1),
	DEF_FIXED("zx",         R8A7795_CLK_ZX,    CLK_PLL1_DIV2,  2, 1),
	DEF_FIXED("s0d1",       R8A7795_CLK_S0D1,  CLK_S0,         1, 1),
	DEF_FIXED("s0d4",       R8A7795_CLK_S0D4,  CLK_S0,         4, 1),
	DEF_FIXED("s1d1",       R8A7795_CLK_S1D1,  CLK_S1,         1, 1),
	DEF_FIXED("s1d2",       R8A7795_CLK_S1D2,  CLK_S1,         2, 1),
	DEF_FIXED("s1d4",       R8A7795_CLK_S1D4,  CLK_S1,         4, 1),
	DEF_FIXED("s2d1",       R8A7795_CLK_S2D1,  CLK_S2,         1, 1),
	DEF_FIXED("s2d2",       R8A7795_CLK_S2D2,  CLK_S2,         2, 1),
	DEF_FIXED("s2d4",       R8A7795_CLK_S2D4,  CLK_S2,         4, 1),
	DEF_FIXED("s3d1",       R8A7795_CLK_S3D1,  CLK_S3,         1, 1),
	DEF_FIXED("s3d2",       R8A7795_CLK_S3D2,  CLK_S3,         2, 1),
	DEF_FIXED("s3d4",       R8A7795_CLK_S3D4,  CLK_S3,         4, 1),

	DEF_GEN3_SD("sd0",      R8A7795_CLK_SD0,   CLK_PLL1_DIV2, 0x0074),
	DEF_GEN3_SD("sd1",      R8A7795_CLK_SD1,   CLK_PLL1_DIV2, 0x0078),
	DEF_GEN3_SD("sd2",      R8A7795_CLK_SD2,   CLK_PLL1_DIV2, 0x0268),
	DEF_GEN3_SD("sd3",      R8A7795_CLK_SD3,   CLK_PLL1_DIV2, 0x026c),

	DEF_FIXED("cl",         R8A7795_CLK_CL,    CLK_PLL1_DIV2, 48, 1),
	DEF_FIXED("cp",         R8A7795_CLK_CP,    CLK_EXTAL,      2, 1),

	DEF_DIV6P1("mso",       R8A7795_CLK_MSO,   CLK_PLL1_DIV4, 0x014),
	DEF_DIV6P1("hdmi",      R8A7795_CLK_HDMI,  CLK_PLL1_DIV2, 0x250),
	DEF_DIV6P1("canfd",     R8A7795_CLK_CANFD, CLK_PLL1_DIV4, 0x244),
	DEF_DIV6P1("csi0",      R8A7795_CLK_CSI0,  CLK_PLL1_DIV4, 0x00c),

	DEF_DIV6_RO("osc",      R8A7795_CLK_OSC,   CLK_EXTAL, CPG_RCKCR, 8),
	DEF_DIV6_RO("r_int",    CLK_RINT,          CLK_EXTAL, CPG_RCKCR, 32),

	DEF_BASE("r",           R8A7795_CLK_R, CLK_TYPE_GEN3_R, CLK_RINT),
};

static const struct mssr_mod_clk r8a7795_mod_clks[] __initconst = {
	DEF_MOD("scif5",		 202,	R8A7795_CLK_S3D4),
	DEF_MOD("scif4",		 203,	R8A7795_CLK_S3D4),
	DEF_MOD("scif3",		 204,	R8A7795_CLK_S3D4),
	DEF_MOD("scif1",		 206,	R8A7795_CLK_S3D4),
	DEF_MOD("scif0",		 207,	R8A7795_CLK_S3D4),
	DEF_MOD("msiof3",		 208,	R8A7795_CLK_MSO),
	DEF_MOD("msiof2",		 209,	R8A7795_CLK_MSO),
	DEF_MOD("msiof1",		 210,	R8A7795_CLK_MSO),
	DEF_MOD("msiof0",		 211,	R8A7795_CLK_MSO),
	DEF_MOD("sys-dmac2",		 217,	R8A7795_CLK_S3D1),
	DEF_MOD("sys-dmac1",		 218,	R8A7795_CLK_S3D1),
	DEF_MOD("sys-dmac0",		 219,	R8A7795_CLK_S3D1),
	DEF_MOD("scif2",		 310,	R8A7795_CLK_S3D4),
	DEF_MOD("sdif3",		 311,	R8A7795_CLK_SD3),
	DEF_MOD("sdif2",		 312,	R8A7795_CLK_SD2),
	DEF_MOD("sdif1",		 313,	R8A7795_CLK_SD1),
	DEF_MOD("sdif0",		 314,	R8A7795_CLK_SD0),
	DEF_MOD("pcie1",		 318,	R8A7795_CLK_S3D1),
	DEF_MOD("pcie0",		 319,	R8A7795_CLK_S3D1),
	DEF_MOD("usb3-if1",		 327,	R8A7795_CLK_S3D1),
	DEF_MOD("usb3-if0",		 328,	R8A7795_CLK_S3D1),
	DEF_MOD("usb-dmac0",		 330,	R8A7795_CLK_S3D1),
	DEF_MOD("usb-dmac1",		 331,	R8A7795_CLK_S3D1),
	DEF_MOD("rwdt0",		 402,	R8A7795_CLK_R),
	DEF_MOD("intc-ex",		 407,	R8A7795_CLK_CP),
	DEF_MOD("intc-ap",		 408,	R8A7795_CLK_S3D1),
	DEF_MOD("audmac0",		 502,	R8A7795_CLK_S3D4),
	DEF_MOD("audmac1",		 501,	R8A7795_CLK_S3D4),
	DEF_MOD("hscif4",		 516,	R8A7795_CLK_S3D1),
	DEF_MOD("hscif3",		 517,	R8A7795_CLK_S3D1),
	DEF_MOD("hscif2",		 518,	R8A7795_CLK_S3D1),
	DEF_MOD("hscif1",		 519,	R8A7795_CLK_S3D1),
	DEF_MOD("hscif0",		 520,	R8A7795_CLK_S3D1),
	DEF_MOD("pwm",			 523,	R8A7795_CLK_S3D4),
	DEF_MOD("fcpvd3",		 600,	R8A7795_CLK_S2D1),
	DEF_MOD("fcpvd2",		 601,	R8A7795_CLK_S2D1),
	DEF_MOD("fcpvd1",		 602,	R8A7795_CLK_S2D1),
	DEF_MOD("fcpvd0",		 603,	R8A7795_CLK_S2D1),
	DEF_MOD("fcpvb1",		 606,	R8A7795_CLK_S2D1),
	DEF_MOD("fcpvb0",		 607,	R8A7795_CLK_S2D1),
	DEF_MOD("fcpvi2",		 609,	R8A7795_CLK_S2D1),
	DEF_MOD("fcpvi1",		 610,	R8A7795_CLK_S2D1),
	DEF_MOD("fcpvi0",		 611,	R8A7795_CLK_S2D1),
	DEF_MOD("fcpf2",		 613,	R8A7795_CLK_S2D1),
	DEF_MOD("fcpf1",		 614,	R8A7795_CLK_S2D1),
	DEF_MOD("fcpf0",		 615,	R8A7795_CLK_S2D1),
	DEF_MOD("fcpci1",		 616,	R8A7795_CLK_S2D1),
	DEF_MOD("fcpci0",		 617,	R8A7795_CLK_S2D1),
	DEF_MOD("fcpcs",		 619,	R8A7795_CLK_S2D1),
	DEF_MOD("vspd3",		 620,	R8A7795_CLK_S2D1),
	DEF_MOD("vspd2",		 621,	R8A7795_CLK_S2D1),
	DEF_MOD("vspd1",		 622,	R8A7795_CLK_S2D1),
	DEF_MOD("vspd0",		 623,	R8A7795_CLK_S2D1),
	DEF_MOD("vspbc",		 624,	R8A7795_CLK_S2D1),
	DEF_MOD("vspbd",		 626,	R8A7795_CLK_S2D1),
	DEF_MOD("vspi2",		 629,	R8A7795_CLK_S2D1),
	DEF_MOD("vspi1",		 630,	R8A7795_CLK_S2D1),
	DEF_MOD("vspi0",		 631,	R8A7795_CLK_S2D1),
	DEF_MOD("ehci2",		 701,	R8A7795_CLK_S3D4),
	DEF_MOD("ehci1",		 702,	R8A7795_CLK_S3D4),
	DEF_MOD("ehci0",		 703,	R8A7795_CLK_S3D4),
	DEF_MOD("hsusb",		 704,	R8A7795_CLK_S3D4),
	DEF_MOD("csi21",		 713,	R8A7795_CLK_CSI0),
	DEF_MOD("csi20",		 714,	R8A7795_CLK_CSI0),
	DEF_MOD("csi41",		 715,	R8A7795_CLK_CSI0),
	DEF_MOD("csi40",		 716,	R8A7795_CLK_CSI0),
	DEF_MOD("du3",			 721,	R8A7795_CLK_S2D1),
	DEF_MOD("du2",			 722,	R8A7795_CLK_S2D1),
	DEF_MOD("du1",			 723,	R8A7795_CLK_S2D1),
	DEF_MOD("du0",			 724,	R8A7795_CLK_S2D1),
	DEF_MOD("lvds",			 727,	R8A7795_CLK_S2D1),
	DEF_MOD("hdmi1",		 728,	R8A7795_CLK_HDMI),
	DEF_MOD("hdmi0",		 729,	R8A7795_CLK_HDMI),
	DEF_MOD("vin7",			 804,	R8A7795_CLK_S2D1),
	DEF_MOD("vin6",			 805,	R8A7795_CLK_S2D1),
	DEF_MOD("vin5",			 806,	R8A7795_CLK_S2D1),
	DEF_MOD("vin4",			 807,	R8A7795_CLK_S2D1),
	DEF_MOD("vin3",			 808,	R8A7795_CLK_S2D1),
	DEF_MOD("vin2",			 809,	R8A7795_CLK_S2D1),
	DEF_MOD("vin1",			 810,	R8A7795_CLK_S2D1),
	DEF_MOD("vin0",			 811,	R8A7795_CLK_S2D1),
	DEF_MOD("etheravb",		 812,	R8A7795_CLK_S3D2),
	DEF_MOD("sata0",		 815,	R8A7795_CLK_S3D2),
	DEF_MOD("gpio7",		 905,	R8A7795_CLK_CP),
	DEF_MOD("gpio6",		 906,	R8A7795_CLK_CP),
	DEF_MOD("gpio5",		 907,	R8A7795_CLK_CP),
	DEF_MOD("gpio4",		 908,	R8A7795_CLK_CP),
	DEF_MOD("gpio3",		 909,	R8A7795_CLK_CP),
	DEF_MOD("gpio2",		 910,	R8A7795_CLK_CP),
	DEF_MOD("gpio1",		 911,	R8A7795_CLK_CP),
	DEF_MOD("gpio0",		 912,	R8A7795_CLK_CP),
	DEF_MOD("can-fd",		 914,	R8A7795_CLK_S3D2),
	DEF_MOD("can-if1",		 915,	R8A7795_CLK_S3D4),
	DEF_MOD("can-if0",		 916,	R8A7795_CLK_S3D4),
	DEF_MOD("i2c6",			 918,	R8A7795_CLK_S3D2),
	DEF_MOD("i2c5",			 919,	R8A7795_CLK_S3D2),
	DEF_MOD("i2c4",			 927,	R8A7795_CLK_S3D2),
	DEF_MOD("i2c3",			 928,	R8A7795_CLK_S3D2),
	DEF_MOD("i2c2",			 929,	R8A7795_CLK_S3D2),
	DEF_MOD("i2c1",			 930,	R8A7795_CLK_S3D2),
	DEF_MOD("i2c0",			 931,	R8A7795_CLK_S3D2),
	DEF_MOD("ssi-all",		1005,	R8A7795_CLK_S3D4),
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
	DEF_MOD("scu-all",		1017,	R8A7795_CLK_S3D4),
	DEF_MOD("scu-dvc1",		1018,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-dvc0",		1019,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-ctu1-mix1",	1020,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-ctu0-mix0",	1021,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-src9",		1022,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-src8",		1023,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-src7",		1024,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-src6",		1025,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-src5",		1026,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-src4",		1027,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-src3",		1028,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-src2",		1029,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-src1",		1030,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-src0",		1031,	MOD_CLK_ID(1017)),
};

static const unsigned int r8a7795_crit_mod_clks[] __initconst = {
	MOD_CLK_ID(408),	/* INTC-AP (GIC) */
};

/* -----------------------------------------------------------------------------
 * SDn Clock
 *
 */
#define CPG_SD_STP_HCK		BIT(9)
#define CPG_SD_STP_CK		BIT(8)

#define CPG_SD_STP_MASK		(CPG_SD_STP_HCK | CPG_SD_STP_CK)
#define CPG_SD_FC_MASK		(0x7 << 2 | 0x3 << 0)

#define CPG_SD_DIV_TABLE_DATA(stp_hck, stp_ck, sd_srcfc, sd_fc, sd_div) \
{ \
	.val = ((stp_hck) ? CPG_SD_STP_HCK : 0) | \
	       ((stp_ck) ? CPG_SD_STP_CK : 0) | \
	       ((sd_srcfc) << 2) | \
	       ((sd_fc) << 0), \
	.div = (sd_div), \
}

struct sd_div_table {
	u32 val;
	unsigned int div;
};

struct sd_clock {
	struct clk_hw hw;
	void __iomem *reg;
	const struct sd_div_table *div_table;
	unsigned int div_num;
	unsigned int div_min;
	unsigned int div_max;
};

/* SDn divider
 *                     sd_srcfc   sd_fc   div
 * stp_hck   stp_ck    (div)      (div)     = sd_srcfc x sd_fc
 *-------------------------------------------------------------------
 *  0         0         0 (1)      1 (4)      4
 *  0         0         1 (2)      1 (4)      8
 *  1         0         2 (4)      1 (4)     16
 *  1         0         3 (8)      1 (4)     32
 *  1         0         4 (16)     1 (4)     64
 *  0         0         0 (1)      0 (2)      2
 *  0         0         1 (2)      0 (2)      4
 *  1         0         2 (4)      0 (2)      8
 *  1         0         3 (8)      0 (2)     16
 *  1         0         4 (16)     0 (2)     32
 */
static const struct sd_div_table cpg_sd_div_table[] = {
/*	CPG_SD_DIV_TABLE_DATA(stp_hck,  stp_ck,   sd_srcfc,   sd_fc,  sd_div) */
	CPG_SD_DIV_TABLE_DATA(0,        0,        0,          1,        4),
	CPG_SD_DIV_TABLE_DATA(0,        0,        1,          1,        8),
	CPG_SD_DIV_TABLE_DATA(1,        0,        2,          1,       16),
	CPG_SD_DIV_TABLE_DATA(1,        0,        3,          1,       32),
	CPG_SD_DIV_TABLE_DATA(1,        0,        4,          1,       64),
	CPG_SD_DIV_TABLE_DATA(0,        0,        0,          0,        2),
	CPG_SD_DIV_TABLE_DATA(0,        0,        1,          0,        4),
	CPG_SD_DIV_TABLE_DATA(1,        0,        2,          0,        8),
	CPG_SD_DIV_TABLE_DATA(1,        0,        3,          0,       16),
	CPG_SD_DIV_TABLE_DATA(1,        0,        4,          0,       32),
};

#define to_sd_clock(_hw) container_of(_hw, struct sd_clock, hw)

static int cpg_sd_clock_enable(struct clk_hw *hw)
{
	struct sd_clock *clock = to_sd_clock(hw);
	u32 val, sd_fc;
	unsigned int i;

	val = clk_readl(clock->reg);

	sd_fc = val & CPG_SD_FC_MASK;
	for (i = 0; i < clock->div_num; i++)
		if (sd_fc == (clock->div_table[i].val & CPG_SD_FC_MASK))
			break;

	if (i >= clock->div_num)
		return -EINVAL;

	val &= ~(CPG_SD_STP_MASK);
	val |= clock->div_table[i].val & CPG_SD_STP_MASK;

	clk_writel(val, clock->reg);

	return 0;
}

static void cpg_sd_clock_disable(struct clk_hw *hw)
{
	struct sd_clock *clock = to_sd_clock(hw);

	clk_writel(clk_readl(clock->reg) | CPG_SD_STP_MASK, clock->reg);
}

static int cpg_sd_clock_is_enabled(struct clk_hw *hw)
{
	struct sd_clock *clock = to_sd_clock(hw);

	return !(clk_readl(clock->reg) & CPG_SD_STP_MASK);
}

static unsigned long cpg_sd_clock_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct sd_clock *clock = to_sd_clock(hw);
	unsigned long rate = parent_rate;
	u32 val, sd_fc;
	unsigned int i;

	val = clk_readl(clock->reg);

	sd_fc = val & CPG_SD_FC_MASK;
	for (i = 0; i < clock->div_num; i++)
		if (sd_fc == (clock->div_table[i].val & CPG_SD_FC_MASK))
			break;

	if (i >= clock->div_num)
		return -EINVAL;

	return DIV_ROUND_CLOSEST(rate, clock->div_table[i].div);
}

static unsigned int cpg_sd_clock_calc_div(struct sd_clock *clock,
					  unsigned long rate,
					  unsigned long parent_rate)
{
	unsigned int div;

	if (!rate)
		rate = 1;

	div = DIV_ROUND_CLOSEST(parent_rate, rate);

	return clamp_t(unsigned int, div, clock->div_min, clock->div_max);
}

static long cpg_sd_clock_round_rate(struct clk_hw *hw, unsigned long rate,
				      unsigned long *parent_rate)
{
	struct sd_clock *clock = to_sd_clock(hw);
	unsigned int div = cpg_sd_clock_calc_div(clock, rate, *parent_rate);

	return DIV_ROUND_CLOSEST(*parent_rate, div);
}

static int cpg_sd_clock_set_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long parent_rate)
{
	struct sd_clock *clock = to_sd_clock(hw);
	unsigned int div = cpg_sd_clock_calc_div(clock, rate, parent_rate);
	u32 val;
	unsigned int i;

	for (i = 0; i < clock->div_num; i++)
		if (div == clock->div_table[i].div)
			break;

	if (i >= clock->div_num)
		return -EINVAL;

	val = clk_readl(clock->reg);
	val &= ~(CPG_SD_STP_MASK | CPG_SD_FC_MASK);
	val |= clock->div_table[i].val & (CPG_SD_STP_MASK | CPG_SD_FC_MASK);
	clk_writel(val, clock->reg);

	return 0;
}

static const struct clk_ops cpg_sd_clock_ops = {
	.enable = cpg_sd_clock_enable,
	.disable = cpg_sd_clock_disable,
	.is_enabled = cpg_sd_clock_is_enabled,
	.recalc_rate = cpg_sd_clock_recalc_rate,
	.round_rate = cpg_sd_clock_round_rate,
	.set_rate = cpg_sd_clock_set_rate,
};

static struct clk * __init cpg_sd_clk_register(const struct cpg_core_clk *core,
					       void __iomem *base,
					       const char *parent_name)
{
	struct clk_init_data init;
	struct sd_clock *clock;
	struct clk *clk;
	unsigned int i;

	clock = kzalloc(sizeof(*clock), GFP_KERNEL);
	if (!clock)
		return ERR_PTR(-ENOMEM);

	init.name = core->name;
	init.ops = &cpg_sd_clock_ops;
	init.flags = CLK_IS_BASIC | CLK_SET_RATE_PARENT;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	clock->reg = base + core->offset;
	clock->hw.init = &init;
	clock->div_table = cpg_sd_div_table;
	clock->div_num = ARRAY_SIZE(cpg_sd_div_table);

	clock->div_max = clock->div_table[0].div;
	clock->div_min = clock->div_max;
	for (i = 1; i < clock->div_num; i++) {
		clock->div_max = max(clock->div_max, clock->div_table[i].div);
		clock->div_min = min(clock->div_min, clock->div_table[i].div);
	}

	clk = clk_register(NULL, &clock->hw);
	if (IS_ERR(clk))
		kfree(clock);

	return clk;
}

#define CPG_PLL0CR	0x00d8
#define CPG_PLL2CR	0x002c
#define CPG_PLL4CR	0x01f4

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

struct cpg_pll_config {
	unsigned int extal_div;
	unsigned int pll1_mult;
	unsigned int pll3_mult;
};

static const struct cpg_pll_config cpg_pll_configs[16] __initconst = {
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

static const struct cpg_pll_config *cpg_pll_config __initdata;

static
struct clk * __init r8a7795_cpg_clk_register(struct device *dev,
					     const struct cpg_core_clk *core,
					     const struct cpg_mssr_info *info,
					     struct clk **clks,
					     void __iomem *base)
{
	const struct clk *parent;
	unsigned int mult = 1;
	unsigned int div = 1;
	u32 value;

	parent = clks[core->parent];
	if (IS_ERR(parent))
		return ERR_CAST(parent);

	switch (core->type) {
	case CLK_TYPE_GEN3_MAIN:
		div = cpg_pll_config->extal_div;
		break;

	case CLK_TYPE_GEN3_PLL0:
		/*
		 * PLL0 is a configurable multiplier clock. Register it as a
		 * fixed factor clock for now as there's no generic multiplier
		 * clock implementation and we currently have no need to change
		 * the multiplier value.
		 */
		value = readl(base + CPG_PLL0CR);
		mult = (((value >> 24) & 0x7f) + 1) * 2;
		break;

	case CLK_TYPE_GEN3_PLL1:
		mult = cpg_pll_config->pll1_mult;
		break;

	case CLK_TYPE_GEN3_PLL2:
		/*
		 * PLL2 is a configurable multiplier clock. Register it as a
		 * fixed factor clock for now as there's no generic multiplier
		 * clock implementation and we currently have no need to change
		 * the multiplier value.
		 */
		value = readl(base + CPG_PLL2CR);
		mult = (((value >> 24) & 0x7f) + 1) * 2;
		break;

	case CLK_TYPE_GEN3_PLL3:
		mult = cpg_pll_config->pll3_mult;
		break;

	case CLK_TYPE_GEN3_PLL4:
		/*
		 * PLL4 is a configurable multiplier clock. Register it as a
		 * fixed factor clock for now as there's no generic multiplier
		 * clock implementation and we currently have no need to change
		 * the multiplier value.
		 */
		value = readl(base + CPG_PLL4CR);
		mult = (((value >> 24) & 0x7f) + 1) * 2;
		break;

	case CLK_TYPE_GEN3_SD:
		return cpg_sd_clk_register(core, base, __clk_get_name(parent));

	case CLK_TYPE_GEN3_R:
		/* RINT is default. Only if EXTALR is populated, we switch to it */
		value = readl(base + CPG_RCKCR) & 0x3f;

		if (clk_get_rate(clks[CLK_EXTALR])) {
			parent = clks[CLK_EXTALR];
			value |= BIT(15);
		}

		writel(value, base + CPG_RCKCR);
		break;

	default:
		return ERR_PTR(-EINVAL);
	}

	return clk_register_fixed_factor(NULL, core->name,
					 __clk_get_name(parent), 0, mult, div);
}

/*
 * Reset register definitions.
 */
#define MODEMR	0xe6160060

static u32 rcar_gen3_read_mode_pins(void)
{
	void __iomem *modemr = ioremap_nocache(MODEMR, 4);
	u32 mode;

	BUG_ON(!modemr);
	mode = ioread32(modemr);
	iounmap(modemr);

	return mode;
}

static int __init r8a7795_cpg_mssr_init(struct device *dev)
{
	u32 cpg_mode = rcar_gen3_read_mode_pins();

	cpg_pll_config = &cpg_pll_configs[CPG_PLL_CONFIG_INDEX(cpg_mode)];
	if (!cpg_pll_config->extal_div) {
		dev_err(dev, "Prohibited setting (cpg_mode=0x%x)\n", cpg_mode);
		return -EINVAL;
	}

	return 0;
}

const struct cpg_mssr_info r8a7795_cpg_mssr_info __initconst = {
	/* Core Clocks */
	.core_clks = r8a7795_core_clks,
	.num_core_clks = ARRAY_SIZE(r8a7795_core_clks),
	.last_dt_core_clk = LAST_DT_CORE_CLK,
	.num_total_core_clks = MOD_CLK_BASE,

	/* Module Clocks */
	.mod_clks = r8a7795_mod_clks,
	.num_mod_clks = ARRAY_SIZE(r8a7795_mod_clks),
	.num_hw_mod_clks = 12 * 32,

	/* Critical Module Clocks */
	.crit_mod_clks = r8a7795_crit_mod_clks,
	.num_crit_mod_clks = ARRAY_SIZE(r8a7795_crit_mod_clks),

	/* Callbacks */
	.init = r8a7795_cpg_mssr_init,
	.cpg_clk_register = r8a7795_cpg_clk_register,
};
