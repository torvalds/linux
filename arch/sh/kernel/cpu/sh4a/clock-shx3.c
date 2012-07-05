/*
 * arch/sh/kernel/cpu/sh4/clock-shx3.c
 *
 * SH-X3 support for the clock framework
 *
 *  Copyright (C) 2006-2007  Renesas Technology Corp.
 *  Copyright (C) 2006-2007  Renesas Solutions Corp.
 *  Copyright (C) 2006-2010  Paul Mundt
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
	.rate		= 16666666,
};

static unsigned long pll_recalc(struct clk *clk)
{
	/* PLL1 has a fixed x72 multiplier.  */
	return clk->parent->rate * 72;
}

static struct sh_clk_ops pll_clk_ops = {
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

static unsigned int div2[] = { 1, 2, 4, 6, 8, 12, 16, 18,
			       24, 32, 36, 48 };

static struct clk_div_mult_table div4_div_mult_table = {
	.divisors = div2,
	.nr_divisors = ARRAY_SIZE(div2),
};

static struct clk_div4_table div4_table = {
	.div_mult_table = &div4_div_mult_table,
};

enum { DIV4_I, DIV4_SH, DIV4_B, DIV4_DDR, DIV4_SHA, DIV4_P, DIV4_NR };

#define DIV4(_bit, _mask, _flags) \
  SH_CLK_DIV4(&pll_clk, FRQMR1, _bit, _mask, _flags)

struct clk div4_clks[DIV4_NR] = {
	[DIV4_P] = DIV4(0, 0x0f80, 0),
	[DIV4_SHA] = DIV4(4, 0x0ff0, 0),
	[DIV4_DDR] = DIV4(12, 0x000c, CLK_ENABLE_ON_INIT),
	[DIV4_B] = DIV4(16, 0x0fe0, CLK_ENABLE_ON_INIT),
	[DIV4_SH] = DIV4(20, 0x000c, CLK_ENABLE_ON_INIT),
	[DIV4_I] = DIV4(28, 0x000e, CLK_ENABLE_ON_INIT),
};

#define MSTPCR0		0xffc00030
#define MSTPCR1		0xffc00034

enum { MSTP027, MSTP026, MSTP025, MSTP024,
       MSTP009, MSTP008, MSTP003, MSTP002,
       MSTP001, MSTP000, MSTP119, MSTP105,
       MSTP104, MSTP_NR };

static struct clk mstp_clks[MSTP_NR] = {
	/* MSTPCR0 */
	[MSTP027] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 27, 0),
	[MSTP026] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 26, 0),
	[MSTP025] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 25, 0),
	[MSTP024] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 24, 0),
	[MSTP009] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 9, 0),
	[MSTP008] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 8, 0),
	[MSTP003] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 3, 0),
	[MSTP002] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 2, 0),
	[MSTP001] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 1, 0),
	[MSTP000] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 0, 0),

	/* MSTPCR1 */
	[MSTP119] = SH_CLK_MSTP32(NULL, MSTPCR1, 19, 0),
	[MSTP105] = SH_CLK_MSTP32(NULL, MSTPCR1, 5, 0),
	[MSTP104] = SH_CLK_MSTP32(NULL, MSTPCR1, 4, 0),
};

static struct clk_lookup lookups[] = {
	/* main clocks */
	CLKDEV_CON_ID("extal", &extal_clk),
	CLKDEV_CON_ID("pll_clk", &pll_clk),

	/* DIV4 clocks */
	CLKDEV_CON_ID("peripheral_clk", &div4_clks[DIV4_P]),
	CLKDEV_CON_ID("shywaya_clk", &div4_clks[DIV4_SHA]),
	CLKDEV_CON_ID("ddr_clk", &div4_clks[DIV4_DDR]),
	CLKDEV_CON_ID("bus_clk", &div4_clks[DIV4_B]),
	CLKDEV_CON_ID("shyway_clk", &div4_clks[DIV4_SH]),
	CLKDEV_CON_ID("cpu_clk", &div4_clks[DIV4_I]),

	/* MSTP32 clocks */
	CLKDEV_ICK_ID("sci_fck", "sh-sci.3", &mstp_clks[MSTP027]),
	CLKDEV_ICK_ID("sci_fck", "sh-sci.2", &mstp_clks[MSTP026]),
	CLKDEV_ICK_ID("sci_fck", "sh-sci.1", &mstp_clks[MSTP025]),
	CLKDEV_ICK_ID("sci_fck", "sh-sci.0", &mstp_clks[MSTP024]),

	CLKDEV_CON_ID("h8ex_fck", &mstp_clks[MSTP003]),
	CLKDEV_CON_ID("csm_fck", &mstp_clks[MSTP002]),
	CLKDEV_CON_ID("fe1_fck", &mstp_clks[MSTP001]),
	CLKDEV_CON_ID("fe0_fck", &mstp_clks[MSTP000]),

	CLKDEV_ICK_ID("tmu_fck", "sh_tmu.0", &mstp_clks[MSTP008]),
	CLKDEV_ICK_ID("tmu_fck", "sh_tmu.1", &mstp_clks[MSTP008]),
	CLKDEV_ICK_ID("tmu_fck", "sh_tmu.2", &mstp_clks[MSTP008]),
	CLKDEV_ICK_ID("tmu_fck", "sh_tmu.3", &mstp_clks[MSTP009]),
	CLKDEV_ICK_ID("tmu_fck", "sh_tmu.4", &mstp_clks[MSTP009]),
	CLKDEV_ICK_ID("tmu_fck", "sh_tmu.5", &mstp_clks[MSTP009]),

	CLKDEV_CON_ID("hudi_fck", &mstp_clks[MSTP119]),
	CLKDEV_CON_ID("dmac_11_6_fck", &mstp_clks[MSTP105]),
	CLKDEV_CON_ID("dmac_5_0_fck", &mstp_clks[MSTP104]),
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
		ret = sh_clk_mstp_register(mstp_clks, MSTP_NR);

	return ret;
}
