/*
 * arch/sh/kernel/cpu/sh4/clock-sh7770.c
 *
 * SH7770 support for the clock framework
 *
 *  Copyright (C) 2005  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
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
	clk->rate *= pfc_divisors[(ctrl_inl(FRQCR) >> 28) & 0x000f];
}

static struct clk_ops sh7770_master_clk_ops = {
	.init		= master_clk_init,
};

static void module_clk_recalc(struct clk *clk)
{
	int idx = ((ctrl_inl(FRQCR) >> 28) & 0x000f);
	clk->rate = clk->parent->rate / pfc_divisors[idx];
}

static struct clk_ops sh7770_module_clk_ops = {
	.recalc		= module_clk_recalc,
};

static void bus_clk_recalc(struct clk *clk)
{
	int idx = (ctrl_inl(FRQCR) & 0x000f);
	clk->rate = clk->parent->rate / bfc_divisors[idx];
}

static struct clk_ops sh7770_bus_clk_ops = {
	.recalc		= bus_clk_recalc,
};

static void cpu_clk_recalc(struct clk *clk)
{
	int idx = ((ctrl_inl(FRQCR) >> 24) & 0x000f);
	clk->rate = clk->parent->rate / ifc_divisors[idx];
}

static struct clk_ops sh7770_cpu_clk_ops = {
	.recalc		= cpu_clk_recalc,
};

static struct clk_ops *sh7770_clk_ops[] = {
	&sh7770_master_clk_ops,
	&sh7770_module_clk_ops,
	&sh7770_bus_clk_ops,
	&sh7770_cpu_clk_ops,
};

void __init arch_init_clk_ops(struct clk_ops **ops, int idx)
{
	if (idx < ARRAY_SIZE(sh7770_clk_ops))
		*ops = sh7770_clk_ops[idx];
}

