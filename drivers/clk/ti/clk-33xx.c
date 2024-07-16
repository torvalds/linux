// SPDX-License-Identifier: GPL-2.0-only
/*
 * AM33XX Clock init
 *
 * Copyright (C) 2013 Texas Instruments, Inc
 *     Tero Kristo (t-kristo@ti.com)
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clk/ti.h>
#include <dt-bindings/clock/am3.h>

#include "clock.h"

static const char * const am3_gpio1_dbclk_parents[] __initconst = {
	"clk-24mhz-clkctrl:0000:0",
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

static const struct omap_clkctrl_reg_data am3_l4ls_clkctrl_regs[] __initconst = {
	{ AM3_L4LS_UART6_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_ck" },
	{ AM3_L4LS_MMC1_CLKCTRL, NULL, CLKF_SW_SUP, "mmc_clk" },
	{ AM3_L4LS_ELM_CLKCTRL, NULL, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM3_L4LS_I2C3_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_ck" },
	{ AM3_L4LS_I2C2_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_ck" },
	{ AM3_L4LS_SPI0_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_ck" },
	{ AM3_L4LS_SPI1_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_ck" },
	{ AM3_L4LS_L4_LS_CLKCTRL, NULL, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM3_L4LS_UART2_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_ck" },
	{ AM3_L4LS_UART3_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_ck" },
	{ AM3_L4LS_UART4_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_ck" },
	{ AM3_L4LS_UART5_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_ck" },
	{ AM3_L4LS_TIMER7_CLKCTRL, NULL, CLKF_SW_SUP, "timer7_fck" },
	{ AM3_L4LS_TIMER2_CLKCTRL, NULL, CLKF_SW_SUP, "timer2_fck" },
	{ AM3_L4LS_TIMER3_CLKCTRL, NULL, CLKF_SW_SUP, "timer3_fck" },
	{ AM3_L4LS_TIMER4_CLKCTRL, NULL, CLKF_SW_SUP, "timer4_fck" },
	{ AM3_L4LS_RNG_CLKCTRL, NULL, CLKF_SW_SUP, "rng_fck" },
	{ AM3_L4LS_GPIO2_CLKCTRL, am3_gpio2_bit_data, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM3_L4LS_GPIO3_CLKCTRL, am3_gpio3_bit_data, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM3_L4LS_GPIO4_CLKCTRL, am3_gpio4_bit_data, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM3_L4LS_D_CAN0_CLKCTRL, NULL, CLKF_SW_SUP, "dcan0_fck" },
	{ AM3_L4LS_D_CAN1_CLKCTRL, NULL, CLKF_SW_SUP, "dcan1_fck" },
	{ AM3_L4LS_EPWMSS1_CLKCTRL, NULL, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM3_L4LS_EPWMSS0_CLKCTRL, NULL, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM3_L4LS_EPWMSS2_CLKCTRL, NULL, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM3_L4LS_TIMER5_CLKCTRL, NULL, CLKF_SW_SUP, "timer5_fck" },
	{ AM3_L4LS_TIMER6_CLKCTRL, NULL, CLKF_SW_SUP, "timer6_fck" },
	{ AM3_L4LS_MMC2_CLKCTRL, NULL, CLKF_SW_SUP, "mmc_clk" },
	{ AM3_L4LS_SPINLOCK_CLKCTRL, NULL, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM3_L4LS_MAILBOX_CLKCTRL, NULL, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM3_L4LS_OCPWP_CLKCTRL, NULL, CLKF_SW_SUP, "l4ls_gclk" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data am3_l3s_clkctrl_regs[] __initconst = {
	{ AM3_L3S_USB_OTG_HS_CLKCTRL, NULL, CLKF_SW_SUP, "usbotg_fck" },
	{ AM3_L3S_GPMC_CLKCTRL, NULL, CLKF_SW_SUP, "l3s_gclk" },
	{ AM3_L3S_MCASP0_CLKCTRL, NULL, CLKF_SW_SUP, "mcasp0_fck" },
	{ AM3_L3S_MCASP1_CLKCTRL, NULL, CLKF_SW_SUP, "mcasp1_fck" },
	{ AM3_L3S_MMC3_CLKCTRL, NULL, CLKF_SW_SUP, "mmc_clk" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data am3_l3_clkctrl_regs[] __initconst = {
	{ AM3_L3_TPTC0_CLKCTRL, NULL, CLKF_SW_SUP, "l3_gclk" },
	{ AM3_L3_EMIF_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_ddr_m2_div2_ck" },
	{ AM3_L3_OCMCRAM_CLKCTRL, NULL, CLKF_SW_SUP, "l3_gclk" },
	{ AM3_L3_AES_CLKCTRL, NULL, CLKF_SW_SUP, "aes0_fck" },
	{ AM3_L3_SHAM_CLKCTRL, NULL, CLKF_SW_SUP, "l3_gclk" },
	{ AM3_L3_TPCC_CLKCTRL, NULL, CLKF_SW_SUP, "l3_gclk" },
	{ AM3_L3_L3_INSTR_CLKCTRL, NULL, CLKF_SW_SUP, "l3_gclk" },
	{ AM3_L3_L3_MAIN_CLKCTRL, NULL, CLKF_SW_SUP, "l3_gclk" },
	{ AM3_L3_TPTC1_CLKCTRL, NULL, CLKF_SW_SUP, "l3_gclk" },
	{ AM3_L3_TPTC2_CLKCTRL, NULL, CLKF_SW_SUP, "l3_gclk" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data am3_l4hs_clkctrl_regs[] __initconst = {
	{ AM3_L4HS_L4_HS_CLKCTRL, NULL, CLKF_SW_SUP, "l4hs_gclk" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data am3_pruss_ocp_clkctrl_regs[] __initconst = {
	{ AM3_PRUSS_OCP_PRUSS_CLKCTRL, NULL, CLKF_SW_SUP | CLKF_NO_IDLEST, "pruss_ocp_gclk" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data am3_cpsw_125mhz_clkctrl_regs[] __initconst = {
	{ AM3_CPSW_125MHZ_CPGMAC0_CLKCTRL, NULL, CLKF_SW_SUP, "cpsw_125mhz_gclk" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data am3_lcdc_clkctrl_regs[] __initconst = {
	{ AM3_LCDC_LCDC_CLKCTRL, NULL, CLKF_SW_SUP | CLKF_SET_RATE_PARENT, "lcd_gclk" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data am3_clk_24mhz_clkctrl_regs[] __initconst = {
	{ AM3_CLK_24MHZ_CLKDIV32K_CLKCTRL, NULL, CLKF_SW_SUP, "clkdiv32k_ck" },
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

static const struct omap_clkctrl_reg_data am3_l4_wkup_clkctrl_regs[] __initconst = {
	{ AM3_L4_WKUP_CONTROL_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_core_m4_div2_ck" },
	{ AM3_L4_WKUP_GPIO1_CLKCTRL, am3_gpio1_bit_data, CLKF_SW_SUP, "dpll_core_m4_div2_ck" },
	{ AM3_L4_WKUP_L4_WKUP_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_core_m4_div2_ck" },
	{ AM3_L4_WKUP_UART1_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_wkupdm_ck" },
	{ AM3_L4_WKUP_I2C1_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_wkupdm_ck" },
	{ AM3_L4_WKUP_ADC_TSC_CLKCTRL, NULL, CLKF_SW_SUP, "adc_tsc_fck" },
	{ AM3_L4_WKUP_SMARTREFLEX0_CLKCTRL, NULL, CLKF_SW_SUP, "smartreflex0_fck" },
	{ AM3_L4_WKUP_TIMER1_CLKCTRL, NULL, CLKF_SW_SUP, "timer1_fck" },
	{ AM3_L4_WKUP_SMARTREFLEX1_CLKCTRL, NULL, CLKF_SW_SUP, "smartreflex1_fck" },
	{ AM3_L4_WKUP_WD_TIMER2_CLKCTRL, NULL, CLKF_SW_SUP, "wdt1_fck" },
	{ 0 },
};

static const char * const am3_dbg_sysclk_ck_parents[] __initconst = {
	"sys_clkin_ck",
	NULL,
};

static const char * const am3_trace_pmd_clk_mux_ck_parents[] __initconst = {
	"l3-aon-clkctrl:0000:19",
	"l3-aon-clkctrl:0000:30",
	NULL,
};

static const char * const am3_trace_clk_div_ck_parents[] __initconst = {
	"l3-aon-clkctrl:0000:20",
	NULL,
};

static const struct omap_clkctrl_div_data am3_trace_clk_div_ck_data __initconst = {
	.max_div = 64,
	.flags = CLK_DIVIDER_POWER_OF_TWO,
};

static const char * const am3_stm_clk_div_ck_parents[] __initconst = {
	"l3-aon-clkctrl:0000:22",
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

static const struct omap_clkctrl_reg_data am3_l3_aon_clkctrl_regs[] __initconst = {
	{ AM3_L3_AON_DEBUGSS_CLKCTRL, am3_debugss_bit_data, CLKF_SW_SUP, "l3-aon-clkctrl:0000:24" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data am3_l4_wkup_aon_clkctrl_regs[] __initconst = {
	{ AM3_L4_WKUP_AON_WKUP_M3_CLKCTRL, NULL, CLKF_NO_IDLEST, "dpll_core_m4_div2_ck" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data am3_mpu_clkctrl_regs[] __initconst = {
	{ AM3_MPU_MPU_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_mpu_m2_ck" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data am3_l4_rtc_clkctrl_regs[] __initconst = {
	{ AM3_L4_RTC_RTC_CLKCTRL, NULL, CLKF_SW_SUP, "clk-24mhz-clkctrl:0000:0" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data am3_gfx_l3_clkctrl_regs[] __initconst = {
	{ AM3_GFX_L3_GFX_CLKCTRL, NULL, CLKF_SW_SUP | CLKF_NO_IDLEST, "gfx_fck_div_ck" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data am3_l4_cefuse_clkctrl_regs[] __initconst = {
	{ AM3_L4_CEFUSE_CEFUSE_CLKCTRL, NULL, CLKF_SW_SUP, "sys_clkin_ck" },
	{ 0 },
};

const struct omap_clkctrl_data am3_clkctrl_data[] __initconst = {
	{ 0x44e00038, am3_l4ls_clkctrl_regs },
	{ 0x44e0001c, am3_l3s_clkctrl_regs },
	{ 0x44e00024, am3_l3_clkctrl_regs },
	{ 0x44e00120, am3_l4hs_clkctrl_regs },
	{ 0x44e000e8, am3_pruss_ocp_clkctrl_regs },
	{ 0x44e00000, am3_cpsw_125mhz_clkctrl_regs },
	{ 0x44e00018, am3_lcdc_clkctrl_regs },
	{ 0x44e0014c, am3_clk_24mhz_clkctrl_regs },
	{ 0x44e00400, am3_l4_wkup_clkctrl_regs },
	{ 0x44e00414, am3_l3_aon_clkctrl_regs },
	{ 0x44e004b0, am3_l4_wkup_aon_clkctrl_regs },
	{ 0x44e00600, am3_mpu_clkctrl_regs },
	{ 0x44e00800, am3_l4_rtc_clkctrl_regs },
	{ 0x44e00900, am3_gfx_l3_clkctrl_regs },
	{ 0x44e00a00, am3_l4_cefuse_clkctrl_regs },
	{ 0 },
};

static struct ti_dt_clk am33xx_clks[] = {
	DT_CLK(NULL, "timer_32k_ck", "clk-24mhz-clkctrl:0000:0"),
	DT_CLK(NULL, "timer_sys_ck", "sys_clkin_ck"),
	DT_CLK(NULL, "clkdiv32k_ick", "clk-24mhz-clkctrl:0000:0"),
	DT_CLK(NULL, "dbg_clka_ck", "l3-aon-clkctrl:0000:30"),
	DT_CLK(NULL, "dbg_sysclk_ck", "l3-aon-clkctrl:0000:19"),
	DT_CLK(NULL, "gpio0_dbclk", "l4-wkup-clkctrl:0008:18"),
	DT_CLK(NULL, "gpio1_dbclk", "l4ls-clkctrl:0074:18"),
	DT_CLK(NULL, "gpio2_dbclk", "l4ls-clkctrl:0078:18"),
	DT_CLK(NULL, "gpio3_dbclk", "l4ls-clkctrl:007c:18"),
	DT_CLK(NULL, "stm_clk_div_ck", "l3-aon-clkctrl:0000:27"),
	DT_CLK(NULL, "stm_pmd_clock_mux_ck", "l3-aon-clkctrl:0000:22"),
	DT_CLK(NULL, "trace_clk_div_ck", "l3-aon-clkctrl:0000:24"),
	DT_CLK(NULL, "trace_pmd_clk_mux_ck", "l3-aon-clkctrl:0000:20"),
	{ .node_name = NULL },
};

static const char *enable_init_clks[] = {
	"dpll_ddr_m2_ck",
	"dpll_mpu_m2_ck",
	"l3_gclk",
	/* AM3_L3_L3_MAIN_CLKCTRL, needed during suspend */
	"l3-clkctrl:00bc:0",
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
