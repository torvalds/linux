// SPDX-License-Identifier: GPL-2.0
/*
 * arch/sh/kernel/cpu/sh4a/clock-sh7770.c
 *
 * SH7770 support for the clock framework
 *
 *  Copyright (C) 2005  Paul Mundt
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <asm/clock.h>
#include <asm/freq.h>
#include <asm/io.h>

static int ifc_divisors[] = { 1, 1, 1, 1, 1, 1, 1, 1 };
static int bfc_divisors[] = { 1, 1, 1, 1, 1, 8,12, 1 };
static int pfc_divisors[] = { 1, 8, 1,10,12,16, 1, 1 };

static void master_clk_init(struct clk *clk)
{
	clk->rate *= pfc_divisors[(__raw_readl(FRQCR) >> 28) & 0x000f];
}

static struct sh_clk_ops sh7770_master_clk_ops = {
	.init		= master_clk_init,
};

static unsigned long module_clk_recalc(struct clk *clk)
{
	int idx = ((__raw_readl(FRQCR) >> 28) & 0x000f);
	return clk->parent->rate / pfc_divisors[idx];
}

static struct sh_clk_ops sh7770_module_clk_ops = {
	.recalc		= module_clk_recalc,
};

static unsigned long bus_clk_recalc(struct clk *clk)
{
	int idx = (__raw_readl(FRQCR) & 0x000f);
	return clk->parent->rate / bfc_divisors[idx];
}

static struct sh_clk_ops sh7770_bus_clk_ops = {
	.recalc		= bus_clk_recalc,
};

static unsigned long cpu_clk_recalc(struct clk *clk)
{
	int idx = ((__raw_readl(FRQCR) >> 24) & 0x000f);
	return clk->parent->rate / ifc_divisors[idx];
}

static struct sh_clk_ops sh7770_cpu_clk_ops = {
	.recalc		= cpu_clk_recalc,
};

static struct sh_clk_ops *sh7770_clk_ops[] = {
	&sh7770_master_clk_ops,
	&sh7770_module_clk_ops,
	&sh7770_bus_clk_ops,
	&sh7770_cpu_clk_ops,
};

void __init arch_init_clk_ops(struct sh_clk_ops **ops, int idx)
{
	if (idx < ARRAY_SIZE(sh7770_clk_ops))
		*ops = sh7770_clk_ops[idx];
}

