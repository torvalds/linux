/*
 * arch/sh/kernel/cpu/sh5/clock-sh5.c
 *
 * SH-5 support for the clock framework
 *
 *  Copyright (C) 2008  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <asm/clock.h>
#include <asm/io.h>

static int ifc_table[] = { 2, 4, 6, 8, 10, 12, 16, 24 };

/* Clock, Power and Reset Controller */
#define	CPRC_BLOCK_OFF	0x01010000
#define CPRC_BASE	(PHYS_PERIPHERAL_BLOCK + CPRC_BLOCK_OFF)

static unsigned long cprc_base;

static void master_clk_init(struct clk *clk)
{
	int idx = (__raw_readl(cprc_base + 0x00) >> 6) & 0x0007;
	clk->rate *= ifc_table[idx];
}

static struct clk_ops sh5_master_clk_ops = {
	.init		= master_clk_init,
};

static unsigned long module_clk_recalc(struct clk *clk)
{
	int idx = (__raw_readw(cprc_base) >> 12) & 0x0007;
	return clk->parent->rate / ifc_table[idx];
}

static struct clk_ops sh5_module_clk_ops = {
	.recalc		= module_clk_recalc,
};

static unsigned long bus_clk_recalc(struct clk *clk)
{
	int idx = (__raw_readw(cprc_base) >> 3) & 0x0007;
	return clk->parent->rate / ifc_table[idx];
}

static struct clk_ops sh5_bus_clk_ops = {
	.recalc		= bus_clk_recalc,
};

static unsigned long cpu_clk_recalc(struct clk *clk)
{
	int idx = (__raw_readw(cprc_base) & 0x0007);
	return clk->parent->rate / ifc_table[idx];
}

static struct clk_ops sh5_cpu_clk_ops = {
	.recalc		= cpu_clk_recalc,
};

static struct clk_ops *sh5_clk_ops[] = {
	&sh5_master_clk_ops,
	&sh5_module_clk_ops,
	&sh5_bus_clk_ops,
	&sh5_cpu_clk_ops,
};

void __init arch_init_clk_ops(struct clk_ops **ops, int idx)
{
	cprc_base = (unsigned long)ioremap_nocache(CPRC_BASE, 1024);
	BUG_ON(!cprc_base);

	if (idx < ARRAY_SIZE(sh5_clk_ops))
		*ops = sh5_clk_ops[idx];
}
