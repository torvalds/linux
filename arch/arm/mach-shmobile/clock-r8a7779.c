/*
 * r8a7779 clock framework support
 *
 * Copyright (C) 2011  Renesas Solutions Corp.
 * Copyright (C) 2011  Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/sh_clk.h>
#include <linux/clkdev.h>
#include <mach/common.h>

#define FRQMR   0xffc80014
#define MSTPCR0 0xffc80030
#define MSTPCR1 0xffc80034
#define MSTPCR3 0xffc8003c
#define MSTPSR1 0xffc80044
#define MSTPSR4 0xffc80048
#define MSTPSR6 0xffc8004c
#define MSTPCR4 0xffc80050
#define MSTPCR5 0xffc80054
#define MSTPCR6 0xffc80058
#define MSTPCR7 0xffc80040

/* ioremap() through clock mapping mandatory to avoid
 * collision with ARM coherent DMA virtual memory range.
 */

static struct clk_mapping cpg_mapping = {
	.phys	= 0xffc80000,
	.len	= 0x80,
};

/*
 * Default rate for the root input clock, reset this with clk_set_rate()
 * from the platform code.
 */
static struct clk plla_clk = {
	.rate		= 1500000000,
	.mapping	= &cpg_mapping,
};

static struct clk *main_clks[] = {
	&plla_clk,
};

static int divisors[] = { 0, 0, 0, 6, 8, 12, 16, 0, 24, 32, 36, 0, 0, 0, 0, 0 };

static struct clk_div_mult_table div4_div_mult_table = {
	.divisors = divisors,
	.nr_divisors = ARRAY_SIZE(divisors),
};

static struct clk_div4_table div4_table = {
	.div_mult_table = &div4_div_mult_table,
};

enum { DIV4_S, DIV4_OUT, DIV4_S4, DIV4_S3, DIV4_S1, DIV4_P, DIV4_NR };

static struct clk div4_clks[DIV4_NR] = {
	[DIV4_S]	= SH_CLK_DIV4(&plla_clk, FRQMR, 20,
				      0x0018, CLK_ENABLE_ON_INIT),
	[DIV4_OUT]	= SH_CLK_DIV4(&plla_clk, FRQMR, 16,
				      0x0700, CLK_ENABLE_ON_INIT),
	[DIV4_S4]	= SH_CLK_DIV4(&plla_clk, FRQMR, 12,
				      0x0040, CLK_ENABLE_ON_INIT),
	[DIV4_S3]	= SH_CLK_DIV4(&plla_clk, FRQMR, 8,
				      0x0010, CLK_ENABLE_ON_INIT),
	[DIV4_S1]	= SH_CLK_DIV4(&plla_clk, FRQMR, 4,
				      0x0060, CLK_ENABLE_ON_INIT),
	[DIV4_P]	= SH_CLK_DIV4(&plla_clk, FRQMR, 0,
				      0x0300, CLK_ENABLE_ON_INIT),
};

enum { MSTP026, MSTP025, MSTP024, MSTP023, MSTP022, MSTP021,
	MSTP016, MSTP015, MSTP014,
	MSTP_NR };

static struct clk mstp_clks[MSTP_NR] = {
	[MSTP026] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 26, 0), /* SCIF0 */
	[MSTP025] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 25, 0), /* SCIF1 */
	[MSTP024] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 24, 0), /* SCIF2 */
	[MSTP023] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 23, 0), /* SCIF3 */
	[MSTP022] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 22, 0), /* SCIF4 */
	[MSTP021] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 21, 0), /* SCIF5 */
	[MSTP016] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 16, 0), /* TMU0 */
	[MSTP015] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 15, 0), /* TMU1 */
	[MSTP014] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 14, 0), /* TMU2 */
};

static unsigned long mul4_recalc(struct clk *clk)
{
	return clk->parent->rate * 4;
}

static struct clk_ops mul4_clk_ops = {
	.recalc		= mul4_recalc,
};

struct clk clkz_clk = {
	.ops		= &mul4_clk_ops,
	.parent		= &div4_clks[DIV4_S],
};

struct clk clkzs_clk = {
	/* clks x 4 / 4 = clks */
	.parent		= &div4_clks[DIV4_S],
};

static struct clk *late_main_clks[] = {
	&clkz_clk,
	&clkzs_clk,
};

static struct clk_lookup lookups[] = {
	/* main clocks */
	CLKDEV_CON_ID("plla_clk", &plla_clk),
	CLKDEV_CON_ID("clkz_clk", &clkz_clk),
	CLKDEV_CON_ID("clkzs_clk", &clkzs_clk),

	/* DIV4 clocks */
	CLKDEV_CON_ID("shyway_clk",	&div4_clks[DIV4_S]),
	CLKDEV_CON_ID("bus_clk",	&div4_clks[DIV4_OUT]),
	CLKDEV_CON_ID("shyway4_clk",	&div4_clks[DIV4_S4]),
	CLKDEV_CON_ID("shyway3_clk",	&div4_clks[DIV4_S3]),
	CLKDEV_CON_ID("shyway1_clk",	&div4_clks[DIV4_S1]),
	CLKDEV_CON_ID("peripheral_clk",	&div4_clks[DIV4_P]),

	/* MSTP32 clocks */
	CLKDEV_DEV_ID("sh_tmu.0", &mstp_clks[MSTP016]), /* TMU00 */
	CLKDEV_DEV_ID("sh_tmu.1", &mstp_clks[MSTP016]), /* TMU01 */
	CLKDEV_DEV_ID("sh-sci.0", &mstp_clks[MSTP026]), /* SCIF0 */
	CLKDEV_DEV_ID("sh-sci.1", &mstp_clks[MSTP025]), /* SCIF1 */
	CLKDEV_DEV_ID("sh-sci.2", &mstp_clks[MSTP024]), /* SCIF2 */
	CLKDEV_DEV_ID("sh-sci.3", &mstp_clks[MSTP023]), /* SCIF3 */
	CLKDEV_DEV_ID("sh-sci.4", &mstp_clks[MSTP022]), /* SCIF4 */
	CLKDEV_DEV_ID("sh-sci.5", &mstp_clks[MSTP021]), /* SCIF6 */
};

void __init r8a7779_clock_init(void)
{
	int k, ret = 0;

	for (k = 0; !ret && (k < ARRAY_SIZE(main_clks)); k++)
		ret = clk_register(main_clks[k]);

	if (!ret)
		ret = sh_clk_div4_register(div4_clks, DIV4_NR, &div4_table);

	if (!ret)
		ret = sh_clk_mstp32_register(mstp_clks, MSTP_NR);

	for (k = 0; !ret && (k < ARRAY_SIZE(late_main_clks)); k++)
		ret = clk_register(late_main_clks[k]);

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	if (!ret)
		clk_init();
	else
		panic("failed to setup r8a7779 clocks\n");
}
