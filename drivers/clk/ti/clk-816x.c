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

static struct ti_dt_clk dm816x_clks[] = {
	DT_CLK(NULL, "sys_clkin", "sys_clkin_ck"),
	DT_CLK(NULL, "timer_sys_ck", "sys_clkin_ck"),
	DT_CLK(NULL, "sys_32k_ck", "sys_32k_ck"),
	DT_CLK(NULL, "mpu_ck", "mpu_ck"),
	DT_CLK(NULL, "timer1_fck", "timer1_fck"),
	DT_CLK(NULL, "timer2_fck", "timer2_fck"),
	DT_CLK(NULL, "timer3_fck", "timer3_fck"),
	DT_CLK(NULL, "timer4_fck", "timer4_fck"),
	DT_CLK(NULL, "timer5_fck", "timer5_fck"),
	DT_CLK(NULL, "timer6_fck", "timer6_fck"),
	DT_CLK(NULL, "timer7_fck", "timer7_fck"),
	DT_CLK(NULL, "sysclk4_ck", "sysclk4_ck"),
	DT_CLK(NULL, "sysclk5_ck", "sysclk5_ck"),
	DT_CLK(NULL, "sysclk6_ck", "sysclk6_ck"),
	DT_CLK(NULL, "sysclk10_ck", "sysclk10_ck"),
	DT_CLK(NULL, "sysclk18_ck", "sysclk18_ck"),
	DT_CLK(NULL, "sysclk24_ck", "sysclk24_ck"),
	DT_CLK("4a100000.ethernet", "sysclk24_ck", "sysclk24_ck"),
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
	omap2_clk_enable_init_clocks(enable_init_clks,
				     ARRAY_SIZE(enable_init_clks));

	return 0;
}
