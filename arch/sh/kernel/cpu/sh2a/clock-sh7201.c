// SPDX-License-Identifier: GPL-2.0
/*
 * arch/sh/kernel/cpu/sh2a/clock-sh7201.c
 *
 * SH7201 support for the clock framework
 *
 *  Copyright (C) 2008 Peter Griffin  <pgriffin@mpc-data.co.uk>
 *
 * Based on clock-sh4.c
 *  Copyright (C) 2005  Paul Mundt
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <asm/clock.h>
#include <asm/freq.h>
#include <asm/io.h>

static const int pll1rate[]={1,2,3,4,6,8};
static const int pfc_divisors[]={1,2,3,4,6,8,12};
#define ifc_divisors pfc_divisors

static unsigned int pll2_mult;

static void master_clk_init(struct clk *clk)
{
	clk->rate = 10000000 * pll2_mult *
	       pll1rate[(__raw_readw(FREQCR) >> 8) & 0x0007];
}

static struct sh_clk_ops sh7201_master_clk_ops = {
	.init		= master_clk_init,
};

static unsigned long module_clk_recalc(struct clk *clk)
{
	int idx = (__raw_readw(FREQCR) & 0x0007);
	return clk->parent->rate / pfc_divisors[idx];
}

static struct sh_clk_ops sh7201_module_clk_ops = {
	.recalc		= module_clk_recalc,
};

static unsigned long bus_clk_recalc(struct clk *clk)
{
	int idx = (__raw_readw(FREQCR) & 0x0007);
	return clk->parent->rate / pfc_divisors[idx];
}

static struct sh_clk_ops sh7201_bus_clk_ops = {
	.recalc		= bus_clk_recalc,
};

static unsigned long cpu_clk_recalc(struct clk *clk)
{
	int idx = ((__raw_readw(FREQCR) >> 4) & 0x0007);
	return clk->parent->rate / ifc_divisors[idx];
}

static struct sh_clk_ops sh7201_cpu_clk_ops = {
	.recalc		= cpu_clk_recalc,
};

static struct sh_clk_ops *sh7201_clk_ops[] = {
	&sh7201_master_clk_ops,
	&sh7201_module_clk_ops,
	&sh7201_bus_clk_ops,
	&sh7201_cpu_clk_ops,
};

void __init arch_init_clk_ops(struct sh_clk_ops **ops, int idx)
{
	if (test_mode_pin(MODE_PIN1 | MODE_PIN0))
		pll2_mult = 1;
	else if (test_mode_pin(MODE_PIN1))
		pll2_mult = 2;
	else
		pll2_mult = 4;

	if (idx < ARRAY_SIZE(sh7201_clk_ops))
		*ops = sh7201_clk_ops[idx];
}
