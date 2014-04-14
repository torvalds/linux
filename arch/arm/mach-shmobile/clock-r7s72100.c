/*
 * r7a72100 clock framework support
 *
 * Copyright (C) 2013  Renesas Solutions Corp.
 * Copyright (C) 2012  Phil Edworthy
 * Copyright (C) 2011  Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/sh_clk.h>
#include <linux/clkdev.h>
#include <mach/common.h>
#include <mach/r7s72100.h>

/* registers */
#define FRQCR		0xfcfe0010
#define FRQCR2		0xfcfe0014
#define STBCR3		0xfcfe0420
#define STBCR4		0xfcfe0424
#define STBCR9		0xfcfe0438

#define PLL_RATE 30

static struct clk_mapping cpg_mapping = {
	.phys	= 0xfcfe0000,
	.len	= 0x1000,
};

/* Fixed 32 KHz root clock for RTC */
static struct clk r_clk = {
	.rate           = 32768,
};

/*
 * Default rate for the root input clock, reset this with clk_set_rate()
 * from the platform code.
 */
static struct clk extal_clk = {
	.rate		= 13330000,
	.mapping	= &cpg_mapping,
};

static unsigned long pll_recalc(struct clk *clk)
{
	return clk->parent->rate * PLL_RATE;
}

static struct sh_clk_ops pll_clk_ops = {
	.recalc		= pll_recalc,
};

static struct clk pll_clk = {
	.ops		= &pll_clk_ops,
	.parent		= &extal_clk,
	.flags		= CLK_ENABLE_ON_INIT,
};

static unsigned long bus_recalc(struct clk *clk)
{
	return clk->parent->rate * 2 / 3;
}

static struct sh_clk_ops bus_clk_ops = {
	.recalc		= bus_recalc,
};

static struct clk bus_clk = {
	.ops		= &bus_clk_ops,
	.parent		= &pll_clk,
	.flags		= CLK_ENABLE_ON_INIT,
};

static unsigned long peripheral0_recalc(struct clk *clk)
{
	return clk->parent->rate / 12;
}

static struct sh_clk_ops peripheral0_clk_ops = {
	.recalc		= peripheral0_recalc,
};

static struct clk peripheral0_clk = {
	.ops		= &peripheral0_clk_ops,
	.parent		= &pll_clk,
	.flags		= CLK_ENABLE_ON_INIT,
};

static unsigned long peripheral1_recalc(struct clk *clk)
{
	return clk->parent->rate / 6;
}

static struct sh_clk_ops peripheral1_clk_ops = {
	.recalc		= peripheral1_recalc,
};

static struct clk peripheral1_clk = {
	.ops		= &peripheral1_clk_ops,
	.parent		= &pll_clk,
	.flags		= CLK_ENABLE_ON_INIT,
};

struct clk *main_clks[] = {
	&r_clk,
	&extal_clk,
	&pll_clk,
	&bus_clk,
	&peripheral0_clk,
	&peripheral1_clk,
};

static int div2[] = { 1, 3, 0, 3 }; /* 1, 2/3, reserve, 1/3 */
static int multipliers[] = { 1, 2, 1, 1 };

static struct clk_div_mult_table div4_div_mult_table = {
	.divisors = div2,
	.nr_divisors = ARRAY_SIZE(div2),
	.multipliers = multipliers,
	.nr_multipliers = ARRAY_SIZE(multipliers),
};

static struct clk_div4_table div4_table = {
	.div_mult_table = &div4_div_mult_table,
};

enum { DIV4_I,
	DIV4_NR };

#define DIV4(_reg, _bit, _mask, _flags) \
	SH_CLK_DIV4(&pll_clk, _reg, _bit, _mask, _flags)

/* The mask field specifies the div2 entries that are valid */
struct clk div4_clks[DIV4_NR] = {
	[DIV4_I]  = DIV4(FRQCR, 8, 0xB, CLK_ENABLE_REG_16BIT
					| CLK_ENABLE_ON_INIT),
};

enum {	MSTP97, MSTP96, MSTP95, MSTP94,
	MSTP47, MSTP46, MSTP45, MSTP44, MSTP43, MSTP42, MSTP41, MSTP40,
	MSTP33,	MSTP_NR };

static struct clk mstp_clks[MSTP_NR] = {
	[MSTP97] = SH_CLK_MSTP8(&peripheral0_clk, STBCR9, 7, 0), /* RIIC0 */
	[MSTP96] = SH_CLK_MSTP8(&peripheral0_clk, STBCR9, 6, 0), /* RIIC1 */
	[MSTP95] = SH_CLK_MSTP8(&peripheral0_clk, STBCR9, 5, 0), /* RIIC2 */
	[MSTP94] = SH_CLK_MSTP8(&peripheral0_clk, STBCR9, 4, 0), /* RIIC3 */
	[MSTP47] = SH_CLK_MSTP8(&peripheral1_clk, STBCR4, 7, 0), /* SCIF0 */
	[MSTP46] = SH_CLK_MSTP8(&peripheral1_clk, STBCR4, 6, 0), /* SCIF1 */
	[MSTP45] = SH_CLK_MSTP8(&peripheral1_clk, STBCR4, 5, 0), /* SCIF2 */
	[MSTP44] = SH_CLK_MSTP8(&peripheral1_clk, STBCR4, 4, 0), /* SCIF3 */
	[MSTP43] = SH_CLK_MSTP8(&peripheral1_clk, STBCR4, 3, 0), /* SCIF4 */
	[MSTP42] = SH_CLK_MSTP8(&peripheral1_clk, STBCR4, 2, 0), /* SCIF5 */
	[MSTP41] = SH_CLK_MSTP8(&peripheral1_clk, STBCR4, 1, 0), /* SCIF6 */
	[MSTP40] = SH_CLK_MSTP8(&peripheral1_clk, STBCR4, 0, 0), /* SCIF7 */
	[MSTP33] = SH_CLK_MSTP8(&peripheral0_clk, STBCR3, 3, 0), /* MTU2 */
};

static struct clk_lookup lookups[] = {
	/* main clocks */
	CLKDEV_CON_ID("rclk", &r_clk),
	CLKDEV_CON_ID("extal", &extal_clk),
	CLKDEV_CON_ID("pll_clk", &pll_clk),
	CLKDEV_CON_ID("peripheral_clk", &peripheral1_clk),

	/* DIV4 clocks */
	CLKDEV_CON_ID("cpu_clk", &div4_clks[DIV4_I]),

	/* MSTP clocks */
	CLKDEV_CON_ID("mtu2_fck", &mstp_clks[MSTP33]),

	/* ICK */
	CLKDEV_ICK_ID("sci_fck", "sh-sci.0", &mstp_clks[MSTP47]),
	CLKDEV_ICK_ID("sci_fck", "sh-sci.1", &mstp_clks[MSTP46]),
	CLKDEV_ICK_ID("sci_fck", "sh-sci.2", &mstp_clks[MSTP45]),
	CLKDEV_ICK_ID("sci_fck", "sh-sci.3", &mstp_clks[MSTP44]),
	CLKDEV_ICK_ID("sci_fck", "sh-sci.4", &mstp_clks[MSTP43]),
	CLKDEV_ICK_ID("sci_fck", "sh-sci.5", &mstp_clks[MSTP42]),
	CLKDEV_ICK_ID("sci_fck", "sh-sci.6", &mstp_clks[MSTP41]),
	CLKDEV_ICK_ID("sci_fck", "sh-sci.7", &mstp_clks[MSTP40]),
};

void __init r7s72100_clock_init(void)
{
	int k, ret = 0;

	for (k = 0; !ret && (k < ARRAY_SIZE(main_clks)); k++)
		ret = clk_register(main_clks[k]);

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	if (!ret)
		ret = sh_clk_div4_register(div4_clks, DIV4_NR, &div4_table);

	if (!ret)
		ret = sh_clk_mstp_register(mstp_clks, MSTP_NR);

	if (!ret)
		shmobile_clk_init();
	else
		panic("failed to setup rza1 clocks\n");
}
