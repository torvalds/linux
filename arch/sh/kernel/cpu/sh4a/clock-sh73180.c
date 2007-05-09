/*
 * arch/sh/kernel/cpu/sh4a/clock-sh73180.c
 *
 * SH73180 support for the clock framework
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
 * SH73180 uses a common set of divisors, so this is quite simple..
 */
static int divisors[] = { 1, 2, 3, 4, 6, 8, 12, 16 };

static void master_clk_init(struct clk *clk)
{
	clk->rate *= divisors[ctrl_inl(FRQCR) & 0x0007];
}

static struct clk_ops sh73180_master_clk_ops = {
	.init		= master_clk_init,
};

static void module_clk_recalc(struct clk *clk)
{
	int idx = (ctrl_inl(FRQCR) & 0x0007);
	clk->rate = clk->parent->rate / divisors[idx];
}

static struct clk_ops sh73180_module_clk_ops = {
	.recalc		= module_clk_recalc,
};

static void bus_clk_recalc(struct clk *clk)
{
	int idx = (ctrl_inl(FRQCR) >> 12) & 0x0007;
	clk->rate = clk->parent->rate / divisors[idx];
}

static struct clk_ops sh73180_bus_clk_ops = {
	.recalc		= bus_clk_recalc,
};

static void cpu_clk_recalc(struct clk *clk)
{
	int idx = (ctrl_inl(FRQCR) >> 20) & 0x0007;
	clk->rate = clk->parent->rate / divisors[idx];
}

static struct clk_ops sh73180_cpu_clk_ops = {
	.recalc		= cpu_clk_recalc,
};

static struct clk_ops *sh73180_clk_ops[] = {
	&sh73180_master_clk_ops,
	&sh73180_module_clk_ops,
	&sh73180_bus_clk_ops,
	&sh73180_cpu_clk_ops,
};

void __init arch_init_clk_ops(struct clk_ops **ops, int idx)
{
	if (idx < ARRAY_SIZE(sh73180_clk_ops))
		*ops = sh73180_clk_ops[idx];
}

