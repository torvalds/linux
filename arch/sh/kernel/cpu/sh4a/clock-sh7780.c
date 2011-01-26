/*
 * arch/sh/kernel/cpu/sh4a/clock-sh7780.c
 *
 * SH7780 support for the clock framework
 *
 *  Copyright (C) 2005  Paul Mundt
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
#include <asm/io.h>

static int ifc_divisors[] = { 2, 4 };
static int bfc_divisors[] = { 1, 1, 1, 8, 12, 16, 24, 1 };
static int pfc_divisors[] = { 1, 24, 24, 1 };
static int cfc_divisors[] = { 1, 1, 4, 1, 6, 1, 1, 1 };

static void master_clk_init(struct clk *clk)
{
	clk->rate *= pfc_divisors[__raw_readl(FRQCR) & 0x0003];
}

static struct clk_ops sh7780_master_clk_ops = {
	.init		= master_clk_init,
};

static unsigned long module_clk_recalc(struct clk *clk)
{
	int idx = (__raw_readl(FRQCR) & 0x0003);
	return clk->parent->rate / pfc_divisors[idx];
}

static struct clk_ops sh7780_module_clk_ops = {
	.recalc		= module_clk_recalc,
};

static unsigned long bus_clk_recalc(struct clk *clk)
{
	int idx = ((__raw_readl(FRQCR) >> 16) & 0x0007);
	return clk->parent->rate / bfc_divisors[idx];
}

static struct clk_ops sh7780_bus_clk_ops = {
	.recalc		= bus_clk_recalc,
};

static unsigned long cpu_clk_recalc(struct clk *clk)
{
	int idx = ((__raw_readl(FRQCR) >> 24) & 0x0001);
	return clk->parent->rate / ifc_divisors[idx];
}

static struct clk_ops sh7780_cpu_clk_ops = {
	.recalc		= cpu_clk_recalc,
};

static struct clk_ops *sh7780_clk_ops[] = {
	&sh7780_master_clk_ops,
	&sh7780_module_clk_ops,
	&sh7780_bus_clk_ops,
	&sh7780_cpu_clk_ops,
};

void __init arch_init_clk_ops(struct clk_ops **ops, int idx)
{
	if (idx < ARRAY_SIZE(sh7780_clk_ops))
		*ops = sh7780_clk_ops[idx];
}

static unsigned long shyway_clk_recalc(struct clk *clk)
{
	int idx = ((__raw_readl(FRQCR) >> 20) & 0x0007);
	return clk->parent->rate / cfc_divisors[idx];
}

static struct clk_ops sh7780_shyway_clk_ops = {
	.recalc		= shyway_clk_recalc,
};

static struct clk sh7780_shyway_clk = {
	.flags		= CLK_ENABLE_ON_INIT,
	.ops		= &sh7780_shyway_clk_ops,
};

/*
 * Additional SH7780-specific on-chip clocks that aren't already part of the
 * clock framework
 */
static struct clk *sh7780_onchip_clocks[] = {
	&sh7780_shyway_clk,
};

#define CLKDEV_CON_ID(_id, _clk) { .con_id = _id, .clk = _clk }

static struct clk_lookup lookups[] = {
	/* main clocks */
	CLKDEV_CON_ID("shyway_clk", &sh7780_shyway_clk),
};

int __init arch_clk_init(void)
{
	struct clk *clk;
	int i, ret = 0;

	cpg_clk_init();

	clk = clk_get(NULL, "master_clk");
	for (i = 0; i < ARRAY_SIZE(sh7780_onchip_clocks); i++) {
		struct clk *clkp = sh7780_onchip_clocks[i];

		clkp->parent = clk;
		ret |= clk_register(clkp);
	}

	clk_put(clk);

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	return ret;
}
