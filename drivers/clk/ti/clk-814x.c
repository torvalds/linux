/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 */

#include <linux/kernel.h>
#include <linux/clk-provider.h>
#include <linux/clk/ti.h>

static struct ti_dt_clk dm814_clks[] = {
	DT_CLK(NULL, "devosc_ck", "devosc_ck"),
	DT_CLK(NULL, "mpu_ck", "mpu_ck"),
	DT_CLK(NULL, "sysclk4_ck", "sysclk4_ck"),
	DT_CLK(NULL, "sysclk6_ck", "sysclk6_ck"),
	DT_CLK(NULL, "sysclk10_ck", "sysclk10_ck"),
	DT_CLK(NULL, "sysclk18_ck", "sysclk18_ck"),
	DT_CLK(NULL, "timer_sys_ck", "devosc_ck"),
	DT_CLK(NULL, "cpsw_125mhz_gclk", "cpsw_125mhz_gclk"),
	DT_CLK(NULL, "cpsw_cpts_rft_clk", "cpsw_cpts_rft_clk"),
	{ .node_name = NULL },
};

int __init dm814x_dt_clk_init(void)
{
	ti_dt_clocks_register(dm814_clks);
	omap2_clk_disable_autoidle_all();
	omap2_clk_enable_init_clocks(NULL, 0);

	return 0;
}
