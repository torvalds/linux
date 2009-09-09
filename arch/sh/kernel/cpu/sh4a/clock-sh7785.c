/*
 * arch/sh/kernel/cpu/sh4a/clock-sh7785.c
 *
 * SH7785 support for the clock framework
 *
 *  Copyright (C) 2007 - 2009  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/cpufreq.h>
#include <asm/clock.h>
#include <asm/freq.h>
#include <cpu/sh7785.h>

/*
 * Default rate for the root input clock, reset this with clk_set_rate()
 * from the platform code.
 */
static struct clk extal_clk = {
	.name		= "extal",
	.id		= -1,
	.rate		= 33333333,
};

static unsigned long pll_recalc(struct clk *clk)
{
	int multiplier;

	multiplier = test_mode_pin(MODE_PIN4) ? 36 : 72;

	return clk->parent->rate * multiplier;
}

static struct clk_ops pll_clk_ops = {
	.recalc		= pll_recalc,
};

static struct clk pll_clk = {
	.name		= "pll_clk",
	.id		= -1,
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

static struct clk_div_mult_table div4_table = {
	.divisors = div2,
	.nr_divisors = ARRAY_SIZE(div2),
};

enum { DIV4_I, DIV4_U, DIV4_SH, DIV4_B, DIV4_DDR, DIV4_GA,
	DIV4_DU, DIV4_P, DIV4_NR };

#define DIV4(_str, _bit, _mask, _flags) \
  SH_CLK_DIV4(_str, &pll_clk, FRQMR1, _bit, _mask, _flags)

struct clk div4_clks[DIV4_NR] = {
	[DIV4_P] = DIV4("peripheral_clk", 0, 0x0f80, 0),
	[DIV4_DU] = DIV4("du_clk", 4, 0x0ff0, 0),
	[DIV4_GA] = DIV4("ga_clk", 8, 0x0030, 0),
	[DIV4_DDR] = DIV4("ddr_clk", 12, 0x000c, CLK_ENABLE_ON_INIT),
	[DIV4_B] = DIV4("bus_clk", 16, 0x0fe0, CLK_ENABLE_ON_INIT),
	[DIV4_SH] = DIV4("shyway_clk", 20, 0x000c, CLK_ENABLE_ON_INIT),
	[DIV4_U] = DIV4("umem_clk", 24, 0x000c, CLK_ENABLE_ON_INIT),
	[DIV4_I] = DIV4("cpu_clk", 28, 0x000e, CLK_ENABLE_ON_INIT),
};

#define MSTPCR0		0xffc80030
#define MSTPCR1		0xffc80034

static struct clk mstp_clks[] = {
	/* MSTPCR0 */
	SH_CLK_MSTP32("scif_fck", 5, &div4_clks[DIV4_P], MSTPCR0, 29, 0),
	SH_CLK_MSTP32("scif_fck", 4, &div4_clks[DIV4_P], MSTPCR0, 28, 0),
	SH_CLK_MSTP32("scif_fck", 3, &div4_clks[DIV4_P], MSTPCR0, 27, 0),
	SH_CLK_MSTP32("scif_fck", 2, &div4_clks[DIV4_P], MSTPCR0, 26, 0),
	SH_CLK_MSTP32("scif_fck", 1, &div4_clks[DIV4_P], MSTPCR0, 25, 0),
	SH_CLK_MSTP32("scif_fck", 0, &div4_clks[DIV4_P], MSTPCR0, 24, 0),
	SH_CLK_MSTP32("ssi_fck", 1, &div4_clks[DIV4_P], MSTPCR0, 21, 0),
	SH_CLK_MSTP32("ssi_fck", 0, &div4_clks[DIV4_P], MSTPCR0, 20, 0),
	SH_CLK_MSTP32("hac_fck", 1, &div4_clks[DIV4_P], MSTPCR0, 17, 0),
	SH_CLK_MSTP32("hac_fck", 0, &div4_clks[DIV4_P], MSTPCR0, 16, 0),
	SH_CLK_MSTP32("mmcif_fck", -1, &div4_clks[DIV4_P], MSTPCR0, 13, 0),
	SH_CLK_MSTP32("flctl_fck", -1, &div4_clks[DIV4_P], MSTPCR0, 12, 0),
	SH_CLK_MSTP32("tmu345_fck", -1, &div4_clks[DIV4_P], MSTPCR0, 9, 0),
	SH_CLK_MSTP32("tmu012_fck", -1, &div4_clks[DIV4_P], MSTPCR0, 8, 0),
	SH_CLK_MSTP32("siof_fck", -1, &div4_clks[DIV4_P], MSTPCR0, 3, 0),
	SH_CLK_MSTP32("hspi_fck", -1, &div4_clks[DIV4_P], MSTPCR0, 2, 0),

	/* MSTPCR1 */
	SH_CLK_MSTP32("hudi_fck", -1, NULL, MSTPCR1, 19, 0),
	SH_CLK_MSTP32("ubc_fck", -1, NULL, MSTPCR1, 17, 0),
	SH_CLK_MSTP32("dmac_11_6_fck", -1, NULL, MSTPCR1, 5, 0),
	SH_CLK_MSTP32("dmac_5_0_fck", -1, NULL, MSTPCR1, 4, 0),
	SH_CLK_MSTP32("gdta_fck", -1, NULL, MSTPCR1, 0, 0),
};

int __init arch_clk_init(void)
{
	int i, ret = 0;

	for (i = 0; i < ARRAY_SIZE(clks); i++)
		ret |= clk_register(clks[i]);

	if (!ret)
		ret = sh_clk_div4_register(div4_clks, ARRAY_SIZE(div4_clks),
					   &div4_table);
	if (!ret)
		ret = sh_clk_mstp32_register(mstp_clks, ARRAY_SIZE(mstp_clks));

	return ret;
}
