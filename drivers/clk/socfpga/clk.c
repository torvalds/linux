/*
 *  Copyright 2011-2012 Calxeda, Inc.
 *  Copyright (C) 2012-2013 Altera Corporation <www.altera.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Based from clk-highbank.c
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include "clk.h"

void __iomem *clk_mgr_base_addr;

static const struct of_device_id socfpga_child_clocks[] __initconst = {
	{ .compatible = "altr,socfpga-pll-clock", socfpga_pll_init, },
	{ .compatible = "altr,socfpga-perip-clk", socfpga_periph_init, },
	{ .compatible = "altr,socfpga-gate-clk", socfpga_gate_init, },
	{},
};

static void __init socfpga_clkmgr_init(struct device_node *node)
{
	clk_mgr_base_addr = of_iomap(node, 0);
	of_clk_init(socfpga_child_clocks);
}
CLK_OF_DECLARE(socfpga_mgr, "altr,clk-mgr", socfpga_clkmgr_init);

