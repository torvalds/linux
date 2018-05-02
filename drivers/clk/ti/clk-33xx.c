/*
 * AM33XX Clock init
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
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clk/ti.h>
#include <dt-bindings/clock/am3.h>

#include "clock.h"

static const char * const am3_gpio1_dbclk_parents[] __initconst = {
	"l4_per_cm:clk:0138:0",
	NULL,
};

static const struct omap_clkctrl_bit_data am3_gpio2_bit_data[] __initconst = {
	{ 18, TI_CLK_GATE, am3_gpio1_dbclk_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data am3_gpio3_bit_data[] __initconst = {
	{ 18, TI_CLK_GATE, am3_gpio1_dbclk_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data am3_gpio4_bit_data[] __initconst = {
	{ 18, TI_CLK_GATE, am3_gpio1_dbclk_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_reg_data am3_l4_per_clkctrl_regs[] __initconst = {
	{ AM3_CPGMAC0_CLKCTRL, NULL, CLKF_SW_SUP, "cpsw_125mhz_gclk", "cpsw_125mhz_clkdm" },
	{ AM3_LCDC_CLKCTRL, NULL, CLKF_SW_SUP | CLKF_SET_RATE_PARENT, "lcd_gclk", "lcdc_clkdm" },
	{ AM3_USB_OTG_HS_CLKCTRL, NULL, CLKF_SW_SUP, "usbotg_fck", "l3s_clkdm" },
	{ AM3_TPTC0_CLKCTRL, NULL, CLKF_SW_SUP, "l3_gclk", "l3_clkdm" },
	{ AM3_EMIF_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_ddr_m2_div2_ck", "l3_clkdm" },
	{ AM3_OCMCRAM_CLKCTRL, NULL, CLKF_SW_SUP, "l3_gclk", "l3_clkdm" },
	{ AM3_GPMC_CLKCTRL, NULL, CLKF_SW_SUP, "l3s_gclk", "l3s_clkdm" },
	{ AM3_MCASP0_CLKCTRL, NULL, CLKF_SW_SUP, "mcasp0_fck", "l3s_clkdm" },
	{ AM3_UART6_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_ck" },
	{ AM3_MMC1_CLKCTRL, NULL, CLKF_SW_SUP, "mmc_clk" },
	{ AM3_ELM_CLKCTRL, NULL, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM3_I2C3_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_ck" },
	{ AM3_I2C2_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_ck" },
	{ AM3_SPI0_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_ck" },
	{ AM3_SPI1_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_ck" },
	{ AM3_L4_LS_CLKCTRL, NULL, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM3_MCASP1_CLKCTRL, NULL, CLKF_SW_SUP, "mcasp1_fck", "l3s_clkdm" },
	{ AM3_UART2_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_ck" },
	{ AM3_UART3_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_ck" },
	{ AM3_UART4_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_ck" },
	{ AM3_UART5_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_ck" },
	{ AM3_TIMER7_CLKCTRL, NULL, CLKF_SW_SUP, "timer7_fck" },
	{ AM3_TIMER2_CLKCTRL, NULL, CLKF_SW_SUP, "timer2_fck" },
	{ AM3_TIMER3_CLKCTRL, NULL, CLKF_SW_SUP, "timer3_fck" },
	{ AM3_TIMER4_CLKCTRL, NULL, CLKF_SW_SUP, "timer4_fck" },
	{ AM3_RNG_CLKCTRL, NULL, CLKF_SW_SUP, "rng_fck" },
	{ AM3_AES_CLKCTRL, NULL, CLKF_SW_SUP, "aes0_fck", "l3_clkdm" },
	{ AM3_SHAM_CLKCTRL, NULL, CLKF_SW_SUP, "l3_gclk", "l3_clkdm" },
	{ AM3_GPIO2_CLKCTRL, am3_gpio2_bit_data, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM3_GPIO3_CLKCTRL, am3_gpio3_bit_data, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM3_GPIO4_CLKCTRL, am3_gpio4_bit_data, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM3_TPCC_CLKCTRL, NULL, CLKF_SW_SUP, "l3_gclk", "l3_clkdm" },
	{ AM3_D_CAN0_CLKCTRL, NULL, CLKF_SW_SUP, "dcan0_fck" },
	{ AM3_D_CAN1_CLKCTRL, NULL, CLKF_SW_SUP, "dcan1_fck" },
	{ AM3_EPWMSS1_CLKCTRL, NULL, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM3_EPWMSS0_CLKCTRL, NULL, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM3_EPWMSS2_CLKCTRL, NULL, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM3_L3_INSTR_CLKCTRL, NULL, CLKF_SW_SUP, "l3_gclk", "l3_clkdm" },
	{ AM3_L3_MAIN_CLKCTRL, NULL, CLKF_SW_SUP, "l3_gclk", "l3_clkdm" },
	{ AM3_PRUSS_CLKCTRL, NULL, CLKF_SW_SUP, "pruss_ocp_gclk", "pruss_ocp_clkdm" },
	{ AM3_TIMER5_CLKCTRL, NULL, CLKF_SW_SUP, "timer5_fck" },
	{ AM3_TIMER6_CLKCTRL, NULL, CLKF_SW_SUP, "timer6_fck" },
	{ AM3_MMC2_CLKCTRL, NULL, CLKF_SW_SUP, "mmc_clk" },
	{ AM3_MMC3_CLKCTRL, NULL, CLKF_SW_SUP, "mmc_clk", "l3s_clkdm" },
	{ AM3_TPTC1_CLKCTRL, NULL, CLKF_SW_SUP, "l3_gclk", "l3_clkdm" },
	{ AM3_TPTC2_CLKCTRL, NULL, CLKF_SW_SUP, "l3_gclk", "l3_clkdm" },
	{ AM3_SPINLOCK_CLKCTRL, NULL, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM3_MAILBOX_CLKCTRL, NULL, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM3_L4_HS_CLKCTRL, NULL, CLKF_SW_SUP, "l4hs_gclk", "l4hs_clkdm" },
	{ AM3_OCPWP_CLKCTRL, NULL, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM3_CLKDIV32K_CLKCTRL, NULL, CLKF_SW_SUP, "clkdiv32k_ck", "clk_24mhz_clkdm" },
	{ 0 },
};

static const char * const am3_gpio0_dbclk_parents[] __initconst = {
	"gpio0_dbclk_mux_ck",
	NULL,
};

static const struct omap_clkctrl_bit_data am3_gpio1_bit_data[] __initconst = {
	{ 18, TI_CLK_GATE, am3_gpio0_dbclk_parents, NULL },
	{ 0 },
};

static const char * const am3_dbg_sysclk_ck_parents[] __initconst = {
	"sys_clkin_ck",
	NULL,
};

static const char * const am3_trace_pmd_clk_mux_ck_parents[] __initconst = {
	"l4_wkup_cm:clk:0010:19",
	"l4_wkup_cm:clk:0010:30",
	NULL,
};

static const char * const am3_trace_clk_div_ck_parents[] __initconst = {
	"l4_wkup_cm:clk:0010:20",
	NULL,
};

static const struct omap_clkctrl_div_data am3_trace_clk_div_ck_data __initconst = {
	.max_div = 64,
	.flags = CLK_DIVIDER_POWER_OF_TWO,
};

static const char * const am3_stm_clk_div_ck_parents[] __initconst = {
	"l4_wkup_cm:clk:0010:22",
	NULL,
};

static const struct omap_clkctrl_div_data am3_stm_clk_div_ck_data __initconst = {
	.max_div = 64,
	.flags = CLK_DIVIDER_POWER_OF_TWO,
};

static const char * const am3_dbg_clka_ck_parents[] __initconst = {
	"dpll_core_m4_ck",
	NULL,
};

static const struct omap_clkctrl_bit_data am3_debugss_bit_data[] __initconst = {
	{ 19, TI_CLK_GATE, am3_dbg_sysclk_ck_parents, NULL },
	{ 20, TI_CLK_MUX, am3_trace_pmd_clk_mux_ck_parents, NULL },
	{ 22, TI_CLK_MUX, am3_trace_pmd_clk_mux_ck_parents, NULL },
	{ 24, TI_CLK_DIVIDER, am3_trace_clk_div_ck_parents, &am3_trace_clk_div_ck_data },
	{ 27, TI_CLK_DIVIDER, am3_stm_clk_div_ck_parents, &am3_stm_clk_div_ck_data },
	{ 30, TI_CLK_GATE, am3_dbg_clka_ck_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_reg_data am3_l4_wkup_clkctrl_regs[] __initconst = {
	{ AM3_CONTROL_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_core_m4_div2_ck" },
	{ AM3_GPIO1_CLKCTRL, am3_gpio1_bit_data, CLKF_SW_SUP, "dpll_core_m4_div2_ck" },
	{ AM3_L4_WKUP_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_core_m4_div2_ck" },
	{ AM3_DEBUGSS_CLKCTRL, am3_debugss_bit_data, CLKF_SW_SUP, "l4_wkup_cm:clk:0010:24", "l3_aon_clkdm" },
	{ AM3_WKUP_M3_CLKCTRL, NULL, CLKF_NO_IDLEST, "dpll_core_m4_div2_ck", "l4_wkup_aon_clkdm" },
	{ AM3_UART1_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_wkupdm_ck" },
	{ AM3_I2C1_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_wkupdm_ck" },
	{ AM3_ADC_TSC_CLKCTRL, NULL, CLKF_SW_SUP, "adc_tsc_fck" },
	{ AM3_SMARTREFLEX0_CLKCTRL, NULL, CLKF_SW_SUP, "smartreflex0_fck" },
	{ AM3_TIMER1_CLKCTRL, NULL, CLKF_SW_SUP, "timer1_fck" },
	{ AM3_SMARTREFLEX1_CLKCTRL, NULL, CLKF_SW_SUP, "smartreflex1_fck" },
	{ AM3_WD_TIMER2_CLKCTRL, NULL, CLKF_SW_SUP, "wdt1_fck" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data am3_mpu_clkctrl_regs[] __initconst = {
	{ AM3_MPU_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_mpu_m2_ck" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data am3_l4_rtc_clkctrl_regs[] __initconst = {
	{ AM3_RTC_CLKCTRL, NULL, CLKF_SW_SUP, "clk_32768_ck" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data am3_gfx_l3_clkctrl_regs[] __initconst = {
	{ AM3_GFX_CLKCTRL, NULL, CLKF_SW_SUP, "gfx_fck_div_ck" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data am3_l4_cefuse_clkctrl_regs[] __initconst = {
	{ AM3_CEFUSE_CLKCTRL, NULL, CLKF_SW_SUP, "sys_clkin_ck" },
	{ 0 },
};

const struct omap_clkctrl_data am3_clkctrl_data[] __initconst = {
	{ 0x44e00014, am3_l4_per_clkctrl_regs },
	{ 0x44e00404, am3_l4_wkup_clkctrl_regs },
	{ 0x44e00604, am3_mpu_clkctrl_regs },
	{ 0x44e00800, am3_l4_rtc_clkctrl_regs },
	{ 0x44e00904, am3_gfx_l3_clkctrl_regs },
	{ 0x44e00a20, am3_l4_cefuse_clkctrl_regs },
	{ 0 },
};

static struct ti_dt_clk am33xx_clks[] = {
	DT_CLK(NULL, "timer_32k_ck", "l4_per_cm:0138:0"),
	DT_CLK(NULL, "timer_sys_ck", "sys_clkin_ck"),
	DT_CLK(NULL, "clkdiv32k_ick", "l4_per_cm:0138:0"),
	DT_CLK(NULL, "dbg_clka_ck", "l4_wkup_cm:0010:30"),
	DT_CLK(NULL, "dbg_sysclk_ck", "l4_wkup_cm:0010:19"),
	DT_CLK(NULL, "gpio0_dbclk", "l4_wkup_cm:0004:18"),
	DT_CLK(NULL, "gpio1_dbclk", "l4_per_cm:0098:18"),
	DT_CLK(NULL, "gpio2_dbclk", "l4_per_cm:009c:18"),
	DT_CLK(NULL, "gpio3_dbclk", "l4_per_cm:00a0:18"),
	DT_CLK(NULL, "stm_clk_div_ck", "l4_wkup_cm:0010:27"),
	DT_CLK(NULL, "stm_pmd_clock_mux_ck", "l4_wkup_cm:0010:22"),
	DT_CLK(NULL, "trace_clk_div_ck", "l4_wkup_cm:0010:24"),
	DT_CLK(NULL, "trace_pmd_clk_mux_ck", "l4_wkup_cm:0010:20"),
	{ .node_name = NULL },
};

static const char *enable_init_clks[] = {
	"dpll_ddr_m2_ck",
	"dpll_mpu_m2_ck",
	"l3_gclk",
	"l4hs_gclk",
	"l4fw_gclk",
	"l4ls_gclk",
	/* Required for external peripherals like, Audio codecs */
	"clkout2_ck",
};

int __init am33xx_dt_clk_init(void)
{
	struct clk *clk1, *clk2;

	ti_dt_clocks_register(am33xx_clks);

	omap2_clk_disable_autoidle_all();

	ti_clk_add_aliases();

	omap2_clk_enable_init_clocks(enable_init_clks,
				     ARRAY_SIZE(enable_init_clks));

	/* TRM ERRATA: Timer 3 & 6 default parent (TCLKIN) may not be always
	 *    physically present, in such a case HWMOD enabling of
	 *    clock would be failure with default parent. And timer
	 *    probe thinks clock is already enabled, this leads to
	 *    crash upon accessing timer 3 & 6 registers in probe.
	 *    Fix by setting parent of both these timers to master
	 *    oscillator clock.
	 */

	clk1 = clk_get_sys(NULL, "sys_clkin_ck");
	clk2 = clk_get_sys(NULL, "timer3_fck");
	clk_set_parent(clk2, clk1);

	clk2 = clk_get_sys(NULL, "timer6_fck");
	clk_set_parent(clk2, clk1);
	/*
	 * The On-Chip 32K RC Osc clock is not an accurate clock-source as per
	 * the design/spec, so as a result, for example, timer which supposed
	 * to get expired @60Sec, but will expire somewhere ~@40Sec, which is
	 * not expected by any use-case, so change WDT1 clock source to PRCM
	 * 32KHz clock.
	 */
	clk1 = clk_get_sys(NULL, "wdt1_fck");
	clk2 = clk_get_sys(NULL, "clkdiv32k_ick");
	clk_set_parent(clk1, clk2);

	return 0;
}
