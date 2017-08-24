/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/clk-provider.h>
#include <linux/clk/ti.h>

#include "clock.h"

static struct ti_dt_clk dm816x_clks[] = {
	DT_CLK(NULL, "sys_clkin", "sys_clkin_ck"),
	DT_CLK(NULL, "timer_sys_ck", "sys_clkin_ck"),
	DT_CLK(NULL, "timer_32k_ck", "sysclk18_ck"),
	DT_CLK(NULL, "timer_ext_ck", "tclkin_ck"),
	{ .node_name = NULL },
};

static const char *enable_init_clks[] = {
	"ddr_pll_clk1",
	"ddr_pll_clk2",
	"ddr_pll_clk3",
};

int __init dm816x_dt_clk_init(void)
{
	ti_dt_clocks_register(dm816x_clks);
	omap2_clk_disable_autoidle_all();
	ti_clk_add_aliases();
	omap2_clk_enable_init_clocks(enable_init_clks,
				     ARRAY_SIZE(enable_init_clks));

	return 0;
}
