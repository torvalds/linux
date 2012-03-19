/*
 * arch/sh/kernel/cpu/sh3/clock-sh7705.c
 *
 * SH7705 support for the clock framework
 *
 *  Copyright (C) 2005  Paul Mundt
 *
 * FRQCR parsing hacked out of arch/sh/kernel/time.c
 *
 *  Copyright (C) 1999  Tetsuya Okada & Niibe Yutaka
 *  Copyright (C) 2000  Philipp Rumpf <prumpf@tux.org>
 *  Copyright (C) 2002, 2003, 2004  Paul Mundt
 *  Copyright (C) 2002  M. R. Brown  <mrbrown@linux-sh.org>
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

/*
 * SH7705 uses the same divisors as the generic SH-3 case, it's just the
 * FRQCR layout that is a bit different..
 */
static int stc_multipliers[] = { 1, 2, 3, 4, 6, 1, 1, 1 };
static int ifc_divisors[]    = { 1, 2, 3, 4, 1, 1, 1, 1 };
static int pfc_divisors[]    = { 1, 2, 3, 4, 6, 1, 1, 1 };

static void master_clk_init(struct clk *clk)
{
	clk->rate *= pfc_divisors[__raw_readw(FRQCR) & 0x0003];
}

static struct sh_clk_ops sh7705_master_clk_ops = {
	.init		= master_clk_init,
};

static unsigned long module_clk_recalc(struct clk *clk)
{
	int idx = __raw_readw(FRQCR) & 0x0003;
	return clk->parent->rate / pfc_divisors[idx];
}

static struct sh_clk_ops sh7705_module_clk_ops = {
	.recalc		= module_clk_recalc,
};

static unsigned long bus_clk_recalc(struct clk *clk)
{
	int idx = (__raw_readw(FRQCR) & 0x0300) >> 8;
	return clk->parent->rate / stc_multipliers[idx];
}

static struct sh_clk_ops sh7705_bus_clk_ops = {
	.recalc		= bus_clk_recalc,
};

static unsigned long cpu_clk_recalc(struct clk *clk)
{
	int idx = (__raw_readw(FRQCR) & 0x0030) >> 4;
	return clk->parent->rate / ifc_divisors[idx];
}

static struct sh_clk_ops sh7705_cpu_clk_ops = {
	.recalc		= cpu_clk_recalc,
};

static struct sh_clk_ops *sh7705_clk_ops[] = {
	&sh7705_master_clk_ops,
	&sh7705_module_clk_ops,
	&sh7705_bus_clk_ops,
	&sh7705_cpu_clk_ops,
};

void __init arch_init_clk_ops(struct sh_clk_ops **ops, int idx)
{
	if (idx < ARRAY_SIZE(sh7705_clk_ops))
		*ops = sh7705_clk_ops[idx];
}

