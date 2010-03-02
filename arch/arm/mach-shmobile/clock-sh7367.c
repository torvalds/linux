/*
 * Preliminary clock framework support for sh7367
 *
 * Copyright (C) 2010  Magnus Damm
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
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/clk.h>

struct clk {
	const char *name;
	unsigned long rate;
};

#include <asm/clkdev.h>

int __clk_get(struct clk *clk)
{
	return 1;
}
EXPORT_SYMBOL(__clk_get);

void __clk_put(struct clk *clk)
{
}
EXPORT_SYMBOL(__clk_put);


int clk_enable(struct clk *clk)
{
	return 0;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	return clk ? clk->rate : 0;
}
EXPORT_SYMBOL(clk_get_rate);

/* a static peripheral clock for now - enough to get sh-sci working */
static struct clk peripheral_clk = {
	.name	    = "peripheral_clk",
	.rate	    = 48000000,
};

/* a static rclk for now - enough to get sh_cmt working */
static struct clk r_clk = {
	.name	    = "r_clk",
	.rate	    = 32768,
};

/* a static usb0 for now - enough to get r8a66597 working */
static struct clk usb0_clk = {
	.name	    = "usb0",
};

static struct clk_lookup lookups[] = {
	{
		.clk = &peripheral_clk,
	}, {
		.clk = &r_clk,
	}, {
		.clk = &usb0_clk,
	}
};

void __init sh7367_clock_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(lookups); i++) {
		lookups[i].con_id = lookups[i].clk->name;
		clkdev_add(&lookups[i]);
	}
}
