/*
 * SH-Mobile Clock Framework
 *
 * Copyright (C) 2010  Magnus Damm
 *
 * Used together with arch/arm/common/clkdev.c and drivers/sh/clk.c.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sh_clk.h>
#include <linux/export.h>
#include <mach/clock.h>
#include <mach/common.h>

unsigned long shmobile_fixed_ratio_clk_recalc(struct clk *clk)
{
	struct clk_ratio *p = clk->priv;

	return clk->parent->rate / p->div * p->mul;
};

struct sh_clk_ops shmobile_fixed_ratio_clk_ops = {
	.recalc	= shmobile_fixed_ratio_clk_recalc,
};

int __init shmobile_clk_init(void)
{
	/* Kick the child clocks.. */
	recalculate_root_clocks();

	/* Enable the necessary init clocks */
	clk_enable_init_clocks();

	return 0;
}

int __clk_get(struct clk *clk)
{
	return 1;
}
EXPORT_SYMBOL(__clk_get);

void __clk_put(struct clk *clk)
{
}
EXPORT_SYMBOL(__clk_put);
