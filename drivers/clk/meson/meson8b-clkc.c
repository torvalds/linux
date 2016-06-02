/*
 * Copyright (c) 2015 Endless Mobile, Inc.
 * Author: Carlo Caione <carlo@endlessm.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/clk-provider.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <dt-bindings/clock/meson8b-clkc.h>

#include "clkc.h"

#define MESON8B_REG_CTL0_ADDR		0x0000
#define MESON8B_REG_SYS_CPU_CNTL1	0x015c
#define MESON8B_REG_HHI_MPEG		0x0174
#define MESON8B_REG_MALI		0x01b0
#define MESON8B_REG_PLL_FIXED		0x0280
#define MESON8B_REG_PLL_SYS		0x0300
#define MESON8B_REG_PLL_VID		0x0320

static const struct pll_rate_table sys_pll_rate_table[] = {
	PLL_RATE(312000000, 52, 1, 2),
	PLL_RATE(336000000, 56, 1, 2),
	PLL_RATE(360000000, 60, 1, 2),
	PLL_RATE(384000000, 64, 1, 2),
	PLL_RATE(408000000, 68, 1, 2),
	PLL_RATE(432000000, 72, 1, 2),
	PLL_RATE(456000000, 76, 1, 2),
	PLL_RATE(480000000, 80, 1, 2),
	PLL_RATE(504000000, 84, 1, 2),
	PLL_RATE(528000000, 88, 1, 2),
	PLL_RATE(552000000, 92, 1, 2),
	PLL_RATE(576000000, 96, 1, 2),
	PLL_RATE(600000000, 50, 1, 1),
	PLL_RATE(624000000, 52, 1, 1),
	PLL_RATE(648000000, 54, 1, 1),
	PLL_RATE(672000000, 56, 1, 1),
	PLL_RATE(696000000, 58, 1, 1),
	PLL_RATE(720000000, 60, 1, 1),
	PLL_RATE(744000000, 62, 1, 1),
	PLL_RATE(768000000, 64, 1, 1),
	PLL_RATE(792000000, 66, 1, 1),
	PLL_RATE(816000000, 68, 1, 1),
	PLL_RATE(840000000, 70, 1, 1),
	PLL_RATE(864000000, 72, 1, 1),
	PLL_RATE(888000000, 74, 1, 1),
	PLL_RATE(912000000, 76, 1, 1),
	PLL_RATE(936000000, 78, 1, 1),
	PLL_RATE(960000000, 80, 1, 1),
	PLL_RATE(984000000, 82, 1, 1),
	PLL_RATE(1008000000, 84, 1, 1),
	PLL_RATE(1032000000, 86, 1, 1),
	PLL_RATE(1056000000, 88, 1, 1),
	PLL_RATE(1080000000, 90, 1, 1),
	PLL_RATE(1104000000, 92, 1, 1),
	PLL_RATE(1128000000, 94, 1, 1),
	PLL_RATE(1152000000, 96, 1, 1),
	PLL_RATE(1176000000, 98, 1, 1),
	PLL_RATE(1200000000, 50, 1, 0),
	PLL_RATE(1224000000, 51, 1, 0),
	PLL_RATE(1248000000, 52, 1, 0),
	PLL_RATE(1272000000, 53, 1, 0),
	PLL_RATE(1296000000, 54, 1, 0),
	PLL_RATE(1320000000, 55, 1, 0),
	PLL_RATE(1344000000, 56, 1, 0),
	PLL_RATE(1368000000, 57, 1, 0),
	PLL_RATE(1392000000, 58, 1, 0),
	PLL_RATE(1416000000, 59, 1, 0),
	PLL_RATE(1440000000, 60, 1, 0),
	PLL_RATE(1464000000, 61, 1, 0),
	PLL_RATE(1488000000, 62, 1, 0),
	PLL_RATE(1512000000, 63, 1, 0),
	PLL_RATE(1536000000, 64, 1, 0),
	{ /* sentinel */ },
};

static const struct clk_div_table cpu_div_table[] = {
	{ .val = 1, .div = 1 },
	{ .val = 2, .div = 2 },
	{ .val = 3, .div = 3 },
	{ .val = 2, .div = 4 },
	{ .val = 3, .div = 6 },
	{ .val = 4, .div = 8 },
	{ .val = 5, .div = 10 },
	{ .val = 6, .div = 12 },
	{ .val = 7, .div = 14 },
	{ .val = 8, .div = 16 },
	{ /* sentinel */ },
};

PNAME(p_xtal)		= { "xtal" };
PNAME(p_fclk_div)	= { "fixed_pll" };
PNAME(p_cpu_clk)	= { "sys_pll" };
PNAME(p_clk81)		= { "fclk_div3", "fclk_div4", "fclk_div5" };
PNAME(p_mali)		= { "fclk_div3", "fclk_div4", "fclk_div5",
			    "fclk_div7", "zero" };

static u32 mux_table_clk81[]	= { 6, 5, 7 };
static u32 mux_table_mali[]	= { 6, 5, 7, 4, 0 };

static struct pll_conf pll_confs = {
	.m		= PARM(0x00, 0,  9),
	.n		= PARM(0x00, 9,  5),
	.od		= PARM(0x00, 16, 2),
};

static struct pll_conf sys_pll_conf = {
	.m		= PARM(0x00, 0,  9),
	.n		= PARM(0x00, 9,  5),
	.od		= PARM(0x00, 16, 2),
	.rate_table	= sys_pll_rate_table,
};

static const struct composite_conf clk81_conf __initconst = {
	.mux_table		= mux_table_clk81,
	.mux_flags		= CLK_MUX_READ_ONLY,
	.mux_parm		= PARM(0x00, 12, 3),
	.div_parm		= PARM(0x00, 0, 7),
	.gate_parm		= PARM(0x00, 7, 1),
};

static const struct composite_conf mali_conf __initconst = {
	.mux_table		= mux_table_mali,
	.mux_parm		= PARM(0x00, 9, 3),
	.div_parm		= PARM(0x00, 0, 7),
	.gate_parm		= PARM(0x00, 8, 1),
};

static const struct clk_conf meson8b_xtal_conf __initconst =
	FIXED_RATE_P(MESON8B_REG_CTL0_ADDR, CLKID_XTAL, "xtal", 0,
			PARM(0x00, 4, 7));

static const struct clk_conf meson8b_clk_confs[] __initconst = {
	FIXED_RATE(CLKID_ZERO, "zero", 0, 0),
	PLL(MESON8B_REG_PLL_FIXED, CLKID_PLL_FIXED, "fixed_pll",
	    p_xtal, 0, &pll_confs),
	PLL(MESON8B_REG_PLL_VID, CLKID_PLL_VID, "vid_pll",
	    p_xtal, 0, &pll_confs),
	PLL(MESON8B_REG_PLL_SYS, CLKID_PLL_SYS, "sys_pll",
	    p_xtal, 0, &sys_pll_conf),
	FIXED_FACTOR_DIV(CLKID_FCLK_DIV2, "fclk_div2", p_fclk_div, 0, 2),
	FIXED_FACTOR_DIV(CLKID_FCLK_DIV3, "fclk_div3", p_fclk_div, 0, 3),
	FIXED_FACTOR_DIV(CLKID_FCLK_DIV4, "fclk_div4", p_fclk_div, 0, 4),
	FIXED_FACTOR_DIV(CLKID_FCLK_DIV5, "fclk_div5", p_fclk_div, 0, 5),
	FIXED_FACTOR_DIV(CLKID_FCLK_DIV7, "fclk_div7", p_fclk_div, 0, 7),
	CPU(MESON8B_REG_SYS_CPU_CNTL1, CLKID_CPUCLK, "a5_clk", p_cpu_clk,
	    cpu_div_table),
	COMPOSITE(MESON8B_REG_HHI_MPEG, CLKID_CLK81, "clk81", p_clk81,
		  CLK_SET_RATE_NO_REPARENT | CLK_IGNORE_UNUSED, &clk81_conf),
	COMPOSITE(MESON8B_REG_MALI, CLKID_MALI, "mali", p_mali,
		  CLK_IGNORE_UNUSED, &mali_conf),
};

static void __init meson8b_clkc_init(struct device_node *np)
{
	void __iomem *clk_base;

	if (!meson_clk_init(np, CLK_NR_CLKS))
		return;

	/* XTAL */
	clk_base = of_iomap(np, 0);
	if (!clk_base) {
		pr_err("%s: Unable to map xtal base\n", __func__);
		return;
	}

	meson_clk_register_clks(&meson8b_xtal_conf, 1, clk_base);
	iounmap(clk_base);

	/*  Generic clocks and PLLs */
	clk_base = of_iomap(np, 1);
	if (!clk_base) {
		pr_err("%s: Unable to map clk base\n", __func__);
		return;
	}

	meson_clk_register_clks(meson8b_clk_confs,
				ARRAY_SIZE(meson8b_clk_confs),
				clk_base);
}
CLK_OF_DECLARE(meson8b_clock, "amlogic,meson8b-clkc", meson8b_clkc_init);
