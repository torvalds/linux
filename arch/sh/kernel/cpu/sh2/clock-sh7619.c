/*
 * arch/sh/kernel/cpu/sh2/clock-sh7619.c
 *
 * SH7619 support for the clock framework
 *
 *  Copyright (C) 2006  Yoshinori Sato
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
#include <linux/io.h>
#include <asm/clock.h>
#include <asm/freq.h>
#include <asm/processor.h>

static const int pll1rate[] = {1,2};
static const int pfc_divisors[] = {1,2,0,4};
static unsigned int pll2_mult;

static void master_clk_init(struct clk *clk)
{
	clk->rate *= pll2_mult * pll1rate[(__raw_readw(FREQCR) >> 8) & 7];
}

static struct clk_ops sh7619_master_clk_ops = {
	.init		= master_clk_init,
};

static unsigned long module_clk_recalc(struct clk *clk)
{
	int idx = (__raw_readw(FREQCR) & 0x0007);
	return clk->parent->rate / pfc_divisors[idx];
}

static struct clk_ops sh7619_module_clk_ops = {
	.recalc		= module_clk_recalc,
};

static unsigned long bus_clk_recalc(struct clk *clk)
{
	return clk->parent->rate / pll1rate[(__raw_readw(FREQCR) >> 8) & 7];
}

static struct clk_ops sh7619_bus_clk_ops = {
	.recalc		= bus_clk_recalc,
};

static struct clk_ops sh7619_cpu_clk_ops = {
	.recalc		= followparent_recalc,
};

static struct clk_ops *sh7619_clk_ops[] = {
	&sh7619_master_clk_ops,
	&sh7619_module_clk_ops,
	&sh7619_bus_clk_ops,
	&sh7619_cpu_clk_ops,
};

void __init arch_init_clk_ops(struct clk_ops **ops, int idx)
{
	if (test_mode_pin(MODE_PIN2 | MODE_PIN0) ||
	    test_mode_pin(MODE_PIN2 | MODE_PIN1))
		pll2_mult = 2;
	else if (test_mode_pin(MODE_PIN0) || test_mode_pin(MODE_PIN1))
		pll2_mult = 4;

	BUG_ON(!pll2_mult);

	if (idx < ARRAY_SIZE(sh7619_clk_ops))
		*ops = sh7619_clk_ops[idx];
}
