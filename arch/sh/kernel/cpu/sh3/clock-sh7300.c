/*
 * arch/sh/kernel/cpu/sh3/clock-sh7300.c
 *
 * SH7300 support for the clock framework
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

static int md_table[] = { 1, 2, 3, 4, 6, 8, 12 };

static void master_clk_init(struct clk *clk)
{
	clk->rate *= md_table[ctrl_inw(FRQCR) & 0x0007];
}

static struct clk_ops sh7300_master_clk_ops = {
	.init		= master_clk_init,
};

static void module_clk_recalc(struct clk *clk)
{
	int idx = (ctrl_inw(FRQCR) & 0x0007);
	clk->rate = clk->parent->rate / md_table[idx];
}

static struct clk_ops sh7300_module_clk_ops = {
	.recalc		= module_clk_recalc,
};

static void bus_clk_recalc(struct clk *clk)
{
	int idx = (ctrl_inw(FRQCR) & 0x0700) >> 8;
	clk->rate = clk->parent->rate / md_table[idx];
}

static struct clk_ops sh7300_bus_clk_ops = {
	.recalc		= bus_clk_recalc,
};

static void cpu_clk_recalc(struct clk *clk)
{
	int idx = (ctrl_inw(FRQCR) & 0x0070) >> 4;
	clk->rate = clk->parent->rate / md_table[idx];
}

static struct clk_ops sh7300_cpu_clk_ops = {
	.recalc		= cpu_clk_recalc,
};

static struct clk_ops *sh7300_clk_ops[] = {
	&sh7300_master_clk_ops,
	&sh7300_module_clk_ops,
	&sh7300_bus_clk_ops,
	&sh7300_cpu_clk_ops,
};

void __init arch_init_clk_ops(struct clk_ops **ops, int idx)
{
	if (idx < ARRAY_SIZE(sh7300_clk_ops))
		*ops = sh7300_clk_ops[idx];
}

