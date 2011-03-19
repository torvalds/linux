/*
 * arch/sh/kernel/cpu/sh4/clock-sh7757.c
 *
 * SH7757 support for the clock framework
 *
 *  Copyright (C) 2009-2010  Renesas Solutions Corp.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/clkdev.h>
#include <asm/clock.h>
#include <asm/freq.h>

/*
 * Default rate for the root input clock, reset this with clk_set_rate()
 * from the platform code.
 */
static struct clk extal_clk = {
	.rate		= 48000000,
};

static unsigned long pll_recalc(struct clk *clk)
{
	int multiplier;

	multiplier = test_mode_pin(MODE_PIN0) ? 24 : 16;

	return clk->parent->rate * multiplier;
}

static struct clk_ops pll_clk_ops = {
	.recalc		= pll_recalc,
};

static struct clk pll_clk = {
	.ops		= &pll_clk_ops,
	.parent		= &extal_clk,
	.flags		= CLK_ENABLE_ON_INIT,
};

static struct clk *clks[] = {
	&extal_clk,
	&pll_clk,
};

static unsigned int div2[] = { 1, 1, 2, 1, 1, 4, 1, 6,
			       1, 1, 1, 16, 1, 24, 1, 1 };

static struct clk_div_mult_table div4_div_mult_table = {
	.divisors = div2,
	.nr_divisors = ARRAY_SIZE(div2),
};

static struct clk_div4_table div4_table = {
	.div_mult_table = &div4_div_mult_table,
};

enum { DIV4_I, DIV4_SH, DIV4_P, DIV4_NR };

#define DIV4(_bit, _mask, _flags) \
  SH_CLK_DIV4(&pll_clk, FRQCR, _bit, _mask, _flags)

struct clk div4_clks[DIV4_NR] = {
	/*
	 * P clock is always enable, because some P clock modules is used
	 * by Host PC.
	 */
	[DIV4_P] = DIV4(0, 0x2800, CLK_ENABLE_ON_INIT),
	[DIV4_SH] = DIV4(12, 0x00a0, CLK_ENABLE_ON_INIT),
	[DIV4_I] = DIV4(20, 0x0004, CLK_ENABLE_ON_INIT),
};

#define MSTPCR0		0xffc80030
#define MSTPCR1		0xffc80034

enum { MSTP004, MSTP000, MSTP114, MSTP113, MSTP112,
       MSTP111, MSTP110, MSTP103, MSTP102,
       MSTP_NR };

static struct clk mstp_clks[MSTP_NR] = {
	/* MSTPCR0 */
	[MSTP004] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 4, 0),
	[MSTP000] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 0, 0),

	/* MSTPCR1 */
	[MSTP114] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR1, 14, 0),
	[MSTP113] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR1, 13, 0),
	[MSTP112] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR1, 12, 0),
	[MSTP111] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR1, 11, 0),
	[MSTP110] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR1, 10, 0),
	[MSTP103] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR1, 3, 0),
	[MSTP102] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR1, 2, 0),
};

#define CLKDEV_CON_ID(_id, _clk) { .con_id = _id, .clk = _clk }

static struct clk_lookup lookups[] = {
	/* main clocks */
	CLKDEV_CON_ID("extal", &extal_clk),
	CLKDEV_CON_ID("pll_clk", &pll_clk),

	/* DIV4 clocks */
	CLKDEV_CON_ID("peripheral_clk", &div4_clks[DIV4_P]),
	CLKDEV_CON_ID("shyway_clk", &div4_clks[DIV4_SH]),
	CLKDEV_CON_ID("cpu_clk", &div4_clks[DIV4_I]),

	/* MSTP32 clocks */
	CLKDEV_CON_ID("sdhi0", &mstp_clks[MSTP004]),
	CLKDEV_CON_ID("riic", &mstp_clks[MSTP000]),
	{
		/* TMU0 */
		.dev_id		= "sh_tmu.0",
		.con_id		= "tmu_fck",
		.clk		= &mstp_clks[MSTP113],
	}, {
		/* TMU1 */
		.dev_id		= "sh_tmu.1",
		.con_id		= "tmu_fck",
		.clk		= &mstp_clks[MSTP114],
	},
	{
		/* SCIF4 (But, ID is 2) */
		.dev_id		= "sh-sci.2",
		.con_id		= "sci_fck",
		.clk		= &mstp_clks[MSTP112],
	}, {
		/* SCIF3 */
		.dev_id		= "sh-sci.1",
		.con_id		= "sci_fck",
		.clk		= &mstp_clks[MSTP111],
	}, {
		/* SCIF2 */
		.dev_id		= "sh-sci.0",
		.con_id		= "sci_fck",
		.clk		= &mstp_clks[MSTP110],
	},
	CLKDEV_CON_ID("usb0", &mstp_clks[MSTP102]),
};

int __init arch_clk_init(void)
{
	int i, ret = 0;

	for (i = 0; i < ARRAY_SIZE(clks); i++)
		ret |= clk_register(clks[i]);
	for (i = 0; i < ARRAY_SIZE(lookups); i++)
		clkdev_add(&lookups[i]);

	if (!ret)
		ret = sh_clk_div4_register(div4_clks, ARRAY_SIZE(div4_clks),
					   &div4_table);
	if (!ret)
		ret = sh_clk_mstp32_register(mstp_clks, MSTP_NR);

	return ret;
}

