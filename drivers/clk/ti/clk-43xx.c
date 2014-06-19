/*
 * AM43XX Clock init
 *
 * Copyright (C) 2013 Texas Instruments, Inc
 *     Tero Kristo (t-kristo@ti.com)
 *
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

static struct ti_dt_clk am43xx_clks[] = {
	DT_CLK(NULL, "clk_32768_ck", "clk_32768_ck"),
	DT_CLK(NULL, "clk_rc32k_ck", "clk_rc32k_ck"),
	DT_CLK(NULL, "virt_19200000_ck", "virt_19200000_ck"),
	DT_CLK(NULL, "virt_24000000_ck", "virt_24000000_ck"),
	DT_CLK(NULL, "virt_25000000_ck", "virt_25000000_ck"),
	DT_CLK(NULL, "virt_26000000_ck", "virt_26000000_ck"),
	DT_CLK(NULL, "sys_clkin_ck", "sys_clkin_ck"),
	DT_CLK(NULL, "tclkin_ck", "tclkin_ck"),
	DT_CLK(NULL, "dpll_core_ck", "dpll_core_ck"),
	DT_CLK(NULL, "dpll_core_x2_ck", "dpll_core_x2_ck"),
	DT_CLK(NULL, "dpll_core_m4_ck", "dpll_core_m4_ck"),
	DT_CLK(NULL, "dpll_core_m5_ck", "dpll_core_m5_ck"),
	DT_CLK(NULL, "dpll_core_m6_ck", "dpll_core_m6_ck"),
	DT_CLK(NULL, "dpll_mpu_ck", "dpll_mpu_ck"),
	DT_CLK(NULL, "dpll_mpu_m2_ck", "dpll_mpu_m2_ck"),
	DT_CLK(NULL, "dpll_ddr_ck", "dpll_ddr_ck"),
	DT_CLK(NULL, "dpll_ddr_m2_ck", "dpll_ddr_m2_ck"),
	DT_CLK(NULL, "dpll_disp_ck", "dpll_disp_ck"),
	DT_CLK(NULL, "dpll_disp_m2_ck", "dpll_disp_m2_ck"),
	DT_CLK(NULL, "dpll_per_ck", "dpll_per_ck"),
	DT_CLK(NULL, "dpll_per_m2_ck", "dpll_per_m2_ck"),
	DT_CLK(NULL, "dpll_per_m2_div4_wkupdm_ck", "dpll_per_m2_div4_wkupdm_ck"),
	DT_CLK(NULL, "dpll_per_m2_div4_ck", "dpll_per_m2_div4_ck"),
	DT_CLK(NULL, "adc_tsc_fck", "adc_tsc_fck"),
	DT_CLK(NULL, "clkdiv32k_ck", "clkdiv32k_ck"),
	DT_CLK(NULL, "clkdiv32k_ick", "clkdiv32k_ick"),
	DT_CLK(NULL, "dcan0_fck", "dcan0_fck"),
	DT_CLK(NULL, "dcan1_fck", "dcan1_fck"),
	DT_CLK(NULL, "pruss_ocp_gclk", "pruss_ocp_gclk"),
	DT_CLK(NULL, "mcasp0_fck", "mcasp0_fck"),
	DT_CLK(NULL, "mcasp1_fck", "mcasp1_fck"),
	DT_CLK(NULL, "smartreflex0_fck", "smartreflex0_fck"),
	DT_CLK(NULL, "smartreflex1_fck", "smartreflex1_fck"),
	DT_CLK(NULL, "sha0_fck", "sha0_fck"),
	DT_CLK(NULL, "aes0_fck", "aes0_fck"),
	DT_CLK(NULL, "timer1_fck", "timer1_fck"),
	DT_CLK(NULL, "timer2_fck", "timer2_fck"),
	DT_CLK(NULL, "timer3_fck", "timer3_fck"),
	DT_CLK(NULL, "timer4_fck", "timer4_fck"),
	DT_CLK(NULL, "timer5_fck", "timer5_fck"),
	DT_CLK(NULL, "timer6_fck", "timer6_fck"),
	DT_CLK(NULL, "timer7_fck", "timer7_fck"),
	DT_CLK(NULL, "wdt1_fck", "wdt1_fck"),
	DT_CLK(NULL, "l3_gclk", "l3_gclk"),
	DT_CLK(NULL, "dpll_core_m4_div2_ck", "dpll_core_m4_div2_ck"),
	DT_CLK(NULL, "l4hs_gclk", "l4hs_gclk"),
	DT_CLK(NULL, "l3s_gclk", "l3s_gclk"),
	DT_CLK(NULL, "l4ls_gclk", "l4ls_gclk"),
	DT_CLK(NULL, "clk_24mhz", "clk_24mhz"),
	DT_CLK(NULL, "cpsw_125mhz_gclk", "cpsw_125mhz_gclk"),
	DT_CLK(NULL, "cpsw_cpts_rft_clk", "cpsw_cpts_rft_clk"),
	DT_CLK(NULL, "gpio0_dbclk_mux_ck", "gpio0_dbclk_mux_ck"),
	DT_CLK(NULL, "gpio0_dbclk", "gpio0_dbclk"),
	DT_CLK(NULL, "gpio1_dbclk", "gpio1_dbclk"),
	DT_CLK(NULL, "gpio2_dbclk", "gpio2_dbclk"),
	DT_CLK(NULL, "gpio3_dbclk", "gpio3_dbclk"),
	DT_CLK(NULL, "gpio4_dbclk", "gpio4_dbclk"),
	DT_CLK(NULL, "gpio5_dbclk", "gpio5_dbclk"),
	DT_CLK(NULL, "mmc_clk", "mmc_clk"),
	DT_CLK(NULL, "gfx_fclk_clksel_ck", "gfx_fclk_clksel_ck"),
	DT_CLK(NULL, "gfx_fck_div_ck", "gfx_fck_div_ck"),
	DT_CLK(NULL, "timer_32k_ck", "clkdiv32k_ick"),
	DT_CLK(NULL, "timer_sys_ck", "sys_clkin_ck"),
	DT_CLK(NULL, "sysclk_div", "sysclk_div"),
	DT_CLK(NULL, "disp_clk", "disp_clk"),
	DT_CLK(NULL, "clk_32k_mosc_ck", "clk_32k_mosc_ck"),
	DT_CLK(NULL, "clk_32k_tpm_ck", "clk_32k_tpm_ck"),
	DT_CLK(NULL, "dpll_extdev_ck", "dpll_extdev_ck"),
	DT_CLK(NULL, "dpll_extdev_m2_ck", "dpll_extdev_m2_ck"),
	DT_CLK(NULL, "mux_synctimer32k_ck", "mux_synctimer32k_ck"),
	DT_CLK(NULL, "synctimer_32kclk", "synctimer_32kclk"),
	DT_CLK(NULL, "timer8_fck", "timer8_fck"),
	DT_CLK(NULL, "timer9_fck", "timer9_fck"),
	DT_CLK(NULL, "timer10_fck", "timer10_fck"),
	DT_CLK(NULL, "timer11_fck", "timer11_fck"),
	DT_CLK(NULL, "cpsw_50m_clkdiv", "cpsw_50m_clkdiv"),
	DT_CLK(NULL, "cpsw_5m_clkdiv", "cpsw_5m_clkdiv"),
	DT_CLK(NULL, "dpll_ddr_x2_ck", "dpll_ddr_x2_ck"),
	DT_CLK(NULL, "dpll_ddr_m4_ck", "dpll_ddr_m4_ck"),
	DT_CLK(NULL, "dpll_per_clkdcoldo", "dpll_per_clkdcoldo"),
	DT_CLK(NULL, "dll_aging_clk_div", "dll_aging_clk_div"),
	DT_CLK(NULL, "div_core_25m_ck", "div_core_25m_ck"),
	DT_CLK(NULL, "func_12m_clk", "func_12m_clk"),
	DT_CLK(NULL, "vtp_clk_div", "vtp_clk_div"),
	DT_CLK(NULL, "usbphy_32khz_clkmux", "usbphy_32khz_clkmux"),
	DT_CLK("48300200.ehrpwm", "tbclk", "ehrpwm0_tbclk"),
	DT_CLK("48302200.ehrpwm", "tbclk", "ehrpwm1_tbclk"),
	DT_CLK("48304200.ehrpwm", "tbclk", "ehrpwm2_tbclk"),
	DT_CLK("48306200.ehrpwm", "tbclk", "ehrpwm3_tbclk"),
	DT_CLK("48308200.ehrpwm", "tbclk", "ehrpwm4_tbclk"),
	DT_CLK("4830a200.ehrpwm", "tbclk", "ehrpwm5_tbclk"),
	{ .node_name = NULL },
};

int __init am43xx_dt_clk_init(void)
{
	struct clk *clk1, *clk2;

	ti_dt_clocks_register(am43xx_clks);

	omap2_clk_disable_autoidle_all();

	/*
	 * cpsw_cpts_rft_clk  has got the choice of 3 clocksources
	 * dpll_core_m4_ck, dpll_core_m5_ck and dpll_disp_m2_ck.
	 * By default dpll_core_m4_ck is selected, witn this as clock
	 * source the CPTS doesnot work properly. It gives clockcheck errors
	 * while running PTP.
	 * clockcheck: clock jumped backward or running slower than expected!
	 * By selecting dpll_core_m5_ck as the clocksource fixes this issue.
	 * In AM335x dpll_core_m5_ck is the default clocksource.
	 */
	clk1 = clk_get_sys(NULL, "cpsw_cpts_rft_clk");
	clk2 = clk_get_sys(NULL, "dpll_core_m5_ck");
	clk_set_parent(clk1, clk2);

	return 0;
}
