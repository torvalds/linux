/*
 * arch/sh/kernel/cpu/sh2a/clock-sh7201.c
 *
 * SH7201 support for the clock framework
 *
 *  Copyright (C) 2008 Peter Griffin  <pgriffin@mpc-data.co.uk>
 *
 * Based on clock-sh4.c
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

static const int pll1rate[]={1,2,3,4,6,8};
static const int pfc_divisors[]={1,2,3,4,6,8,12};
#define ifc_divisors pfc_divisors

#if (CONFIG_SH_CLK_MD == 0)
#define PLL2 (4)
#elif (CONFIG_SH_CLK_MD == 2)
#define PLL2 (2)
#elif (CONFIG_SH_CLK_MD == 3)
#define PLL2 (1)
#else
#error "Illegal Clock Mode!"
#endif

static void master_clk_init(struct clk *clk)
{
	clk->rate = 10000000 * PLL2 * pll1rate[(__raw_readw(FREQCR) >> 8) & 0x0007];
}

static struct clk_ops sh7201_master_clk_ops = {
	.init		= master_clk_init,
};

static unsigned long module_clk_recalc(struct clk *clk)
{
	int idx = (__raw_readw(FREQCR) & 0x0007);
	return clk->parent->rate / pfc_divisors[idx];
}

static struct clk_ops sh7201_module_clk_ops = {
	.recalc		= module_clk_recalc,
};

static unsigned long bus_clk_recalc(struct clk *clk)
{
	int idx = (__raw_readw(FREQCR) & 0x0007);
	return clk->parent->rate / pfc_divisors[idx];
}

static struct clk_ops sh7201_bus_clk_ops = {
	.recalc		= bus_clk_recalc,
};

static unsigned long cpu_clk_recalc(struct clk *clk)
{
	int idx = ((__raw_readw(FREQCR) >> 4) & 0x0007);
	return clk->parent->rate / ifc_divisors[idx];
}

static struct clk_ops sh7201_cpu_clk_ops = {
	.recalc		= cpu_clk_recalc,
};

static struct clk_ops *sh7201_clk_ops[] = {
	&sh7201_master_clk_ops,
	&sh7201_module_clk_ops,
	&sh7201_bus_clk_ops,
	&sh7201_cpu_clk_ops,
};

void __init arch_init_clk_ops(struct clk_ops **ops, int idx)
{
	if (idx < ARRAY_SIZE(sh7201_clk_ops))
		*ops = sh7201_clk_ops[idx];
}
