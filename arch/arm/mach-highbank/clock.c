/*
 * Copyright 2011 Calxeda, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/clkdev.h>

struct clk {
	unsigned long rate;
};

int clk_enable(struct clk *clk)
{
	return 0;
}

void clk_disable(struct clk *clk)
{}

unsigned long clk_get_rate(struct clk *clk)
{
	return clk->rate;
}

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	return clk->rate;
}

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	return 0;
}

static struct clk eclk = { .rate = 200000000 };
static struct clk pclk = { .rate = 150000000 };

static struct clk_lookup lookups[] = {
	{ .clk = &pclk, .con_id = "apb_pclk", },
	{ .clk = &pclk, .dev_id = "sp804", },
	{ .clk = &eclk, .dev_id = "ffe0e000.sdhci", },
	{ .clk = &pclk, .dev_id = "fff36000.serial", },
};

void __init highbank_clocks_init(void)
{
	clkdev_add_table(lookups, ARRAY_SIZE(lookups));
}
