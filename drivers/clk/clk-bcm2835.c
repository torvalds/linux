/*
 * Copyright (C) 2010 Broadcom
 * Copyright (C) 2012 Stephen Warren
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/clk/bcm2835.h>
#include <linux/clk-provider.h>
#include <linux/of.h>

static const __initconst struct of_device_id clk_match[] = {
	{ .compatible = "fixed-clock", .data = of_fixed_clk_setup, },
	{ }
};

/*
 * These are fixed clocks. They're probably not all root clocks and it may
 * be possible to turn them on and off but until this is mapped out better
 * it's the only way they can be used.
 */
void __init bcm2835_init_clocks(void)
{
	struct clk *clk;
	int ret;

	clk = clk_register_fixed_rate(NULL, "sys_pclk", NULL, CLK_IS_ROOT,
					250000000);
	if (IS_ERR(clk))
		pr_err("sys_pclk not registered\n");

	clk = clk_register_fixed_rate(NULL, "apb_pclk", NULL, CLK_IS_ROOT,
					126000000);
	if (IS_ERR(clk))
		pr_err("apb_pclk not registered\n");

	clk = clk_register_fixed_rate(NULL, "uart0_pclk", NULL, CLK_IS_ROOT,
					3000000);
	if (IS_ERR(clk))
		pr_err("uart0_pclk not registered\n");
	ret = clk_register_clkdev(clk, NULL, "20201000.uart");
	if (ret)
		pr_err("uart0_pclk alias not registered\n");

	clk = clk_register_fixed_rate(NULL, "uart1_pclk", NULL, CLK_IS_ROOT,
					125000000);
	if (IS_ERR(clk))
		pr_err("uart1_pclk not registered\n");
	ret = clk_register_clkdev(clk, NULL, "20215000.uart");
	if (ret)
		pr_err("uart1_pclk alias not registered\n");

	of_clk_init(clk_match);
}
