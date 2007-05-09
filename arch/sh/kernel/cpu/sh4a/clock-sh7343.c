/*
 * arch/sh/kernel/cpu/sh4a/clock-sh7343.c
 *
 * SH7343/SH7722 support for the clock framework
 *
 *  Copyright (C) 2006  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <asm/clock.h>
#include <asm/freq.h>

/*
 * SH7343/SH7722 uses a common set of multipliers and divisors, so this
 * is quite simple..
 */
static int multipliers[] = { 1, 2, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
static int divisors[] = { 1, 3, 2, 5, 3, 4, 5, 6, 8, 10, 12, 16, 20 };

#define pll_calc() (((ctrl_inl(FRQCR) >> 24) & 0x1f) + 1)

static void master_clk_init(struct clk *clk)
{
	clk->parent = clk_get(NULL, "cpu_clk");
}

static void master_clk_recalc(struct clk *clk)
{
	int idx = (ctrl_inl(FRQCR) & 0x000f);
	clk->rate *= clk->parent->rate * multipliers[idx] / divisors[idx];
}

static struct clk_ops sh7343_master_clk_ops = {
	.init		= master_clk_init,
	.recalc		= master_clk_recalc,
};

static void module_clk_init(struct clk *clk)
{
	clk->parent = NULL;
	clk->rate = CONFIG_SH_PCLK_FREQ;
}

static struct clk_ops sh7343_module_clk_ops = {
	.init		= module_clk_init,
};

static void bus_clk_init(struct clk *clk)
{
	clk->parent = clk_get(NULL, "cpu_clk");
}

static void bus_clk_recalc(struct clk *clk)
{
	int idx = (ctrl_inl(FRQCR) >> 8) & 0x000f;
	clk->rate = clk->parent->rate * multipliers[idx] / divisors[idx];
}

static struct clk_ops sh7343_bus_clk_ops = {
	.init		= bus_clk_init,
	.recalc		= bus_clk_recalc,
};

static void cpu_clk_init(struct clk *clk)
{
	clk->parent = clk_get(NULL, "module_clk");
	clk->flags |= CLK_RATE_PROPAGATES;
	clk_set_rate(clk, clk_get_rate(clk));
}

static void cpu_clk_recalc(struct clk *clk)
{
	int idx = (ctrl_inl(FRQCR) >> 20) & 0x000f;
	clk->rate = clk->parent->rate * pll_calc() *
		multipliers[idx] / divisors[idx];
}

static struct clk_ops sh7343_cpu_clk_ops = {
	.init		= cpu_clk_init,
	.recalc		= cpu_clk_recalc,
};

static struct clk_ops *sh7343_clk_ops[] = {
	&sh7343_master_clk_ops,
	&sh7343_module_clk_ops,
	&sh7343_bus_clk_ops,
	&sh7343_cpu_clk_ops,
};

void __init arch_init_clk_ops(struct clk_ops **ops, int idx)
{
	if (idx < ARRAY_SIZE(sh7343_clk_ops))
		*ops = sh7343_clk_ops[idx];
}
