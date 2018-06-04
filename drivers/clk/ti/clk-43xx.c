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
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clk/ti.h>
#include <dt-bindings/clock/am4.h>

#include "clock.h"

static const char * const am4_synctimer_32kclk_parents[] __initconst = {
	"mux_synctimer32k_ck",
	NULL,
};

static const struct omap_clkctrl_bit_data am4_counter_32k_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, am4_synctimer_32kclk_parents, NULL },
	{ 0 },
};

static const char * const am4_gpio0_dbclk_parents[] __initconst = {
	"gpio0_dbclk_mux_ck",
	NULL,
};

static const struct omap_clkctrl_bit_data am4_gpio1_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, am4_gpio0_dbclk_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_reg_data am4_l4_wkup_clkctrl_regs[] __initconst = {
	{ AM4_ADC_TSC_CLKCTRL, NULL, CLKF_SW_SUP, "adc_tsc_fck", "l3s_tsc_clkdm" },
	{ AM4_L4_WKUP_CLKCTRL, NULL, CLKF_SW_SUP, "sys_clkin_ck", "l4_wkup_clkdm" },
	{ AM4_WKUP_M3_CLKCTRL, NULL, CLKF_NO_IDLEST, "sys_clkin_ck" },
	{ AM4_COUNTER_32K_CLKCTRL, am4_counter_32k_bit_data, CLKF_SW_SUP, "l4_wkup_cm:clk:0210:8" },
	{ AM4_TIMER1_CLKCTRL, NULL, CLKF_SW_SUP, "timer1_fck", "l4_wkup_clkdm" },
	{ AM4_WD_TIMER2_CLKCTRL, NULL, CLKF_SW_SUP, "wdt1_fck", "l4_wkup_clkdm" },
	{ AM4_I2C1_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_wkupdm_ck", "l4_wkup_clkdm" },
	{ AM4_UART1_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_wkupdm_ck", "l4_wkup_clkdm" },
	{ AM4_SMARTREFLEX0_CLKCTRL, NULL, CLKF_SW_SUP, "smartreflex0_fck", "l4_wkup_clkdm" },
	{ AM4_SMARTREFLEX1_CLKCTRL, NULL, CLKF_SW_SUP, "smartreflex1_fck", "l4_wkup_clkdm" },
	{ AM4_CONTROL_CLKCTRL, NULL, CLKF_SW_SUP, "sys_clkin_ck", "l4_wkup_clkdm" },
	{ AM4_GPIO1_CLKCTRL, am4_gpio1_bit_data, CLKF_SW_SUP, "sys_clkin_ck", "l4_wkup_clkdm" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data am4_mpu_clkctrl_regs[] __initconst = {
	{ AM4_MPU_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_mpu_m2_ck" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data am4_gfx_l3_clkctrl_regs[] __initconst = {
	{ AM4_GFX_CLKCTRL, NULL, CLKF_SW_SUP, "gfx_fck_div_ck" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data am4_l4_rtc_clkctrl_regs[] __initconst = {
	{ AM4_RTC_CLKCTRL, NULL, CLKF_SW_SUP, "clk_32768_ck" },
	{ 0 },
};

static const char * const am4_usb_otg_ss0_refclk960m_parents[] __initconst = {
	"dpll_per_clkdcoldo",
	NULL,
};

static const struct omap_clkctrl_bit_data am4_usb_otg_ss0_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, am4_usb_otg_ss0_refclk960m_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data am4_usb_otg_ss1_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, am4_usb_otg_ss0_refclk960m_parents, NULL },
	{ 0 },
};

static const char * const am4_gpio1_dbclk_parents[] __initconst = {
	"clkdiv32k_ick",
	NULL,
};

static const struct omap_clkctrl_bit_data am4_gpio2_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, am4_gpio1_dbclk_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data am4_gpio3_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, am4_gpio1_dbclk_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data am4_gpio4_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, am4_gpio1_dbclk_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data am4_gpio5_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, am4_gpio1_dbclk_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data am4_gpio6_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, am4_gpio1_dbclk_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_reg_data am4_l4_per_clkctrl_regs[] __initconst = {
	{ AM4_L3_MAIN_CLKCTRL, NULL, CLKF_SW_SUP, "l3_gclk", "l3_clkdm" },
	{ AM4_AES_CLKCTRL, NULL, CLKF_SW_SUP, "aes0_fck", "l3_clkdm" },
	{ AM4_DES_CLKCTRL, NULL, CLKF_SW_SUP, "l3_gclk", "l3_clkdm" },
	{ AM4_L3_INSTR_CLKCTRL, NULL, CLKF_SW_SUP, "l3_gclk", "l3_clkdm" },
	{ AM4_OCMCRAM_CLKCTRL, NULL, CLKF_SW_SUP, "l3_gclk", "l3_clkdm" },
	{ AM4_SHAM_CLKCTRL, NULL, CLKF_SW_SUP, "l3_gclk", "l3_clkdm" },
	{ AM4_VPFE0_CLKCTRL, NULL, CLKF_SW_SUP, "l3_gclk", "l3s_clkdm" },
	{ AM4_VPFE1_CLKCTRL, NULL, CLKF_SW_SUP, "l3_gclk", "l3s_clkdm" },
	{ AM4_TPCC_CLKCTRL, NULL, CLKF_SW_SUP, "l3_gclk", "l3_clkdm" },
	{ AM4_TPTC0_CLKCTRL, NULL, CLKF_SW_SUP, "l3_gclk", "l3_clkdm" },
	{ AM4_TPTC1_CLKCTRL, NULL, CLKF_SW_SUP, "l3_gclk", "l3_clkdm" },
	{ AM4_TPTC2_CLKCTRL, NULL, CLKF_SW_SUP, "l3_gclk", "l3_clkdm" },
	{ AM4_L4_HS_CLKCTRL, NULL, CLKF_SW_SUP, "l4hs_gclk", "l3_clkdm" },
	{ AM4_GPMC_CLKCTRL, NULL, CLKF_SW_SUP, "l3s_gclk", "l3s_clkdm" },
	{ AM4_MCASP0_CLKCTRL, NULL, CLKF_SW_SUP, "mcasp0_fck", "l3s_clkdm" },
	{ AM4_MCASP1_CLKCTRL, NULL, CLKF_SW_SUP, "mcasp1_fck", "l3s_clkdm" },
	{ AM4_MMC3_CLKCTRL, NULL, CLKF_SW_SUP, "mmc_clk", "l3s_clkdm" },
	{ AM4_QSPI_CLKCTRL, NULL, CLKF_SW_SUP, "l3s_gclk", "l3s_clkdm" },
	{ AM4_USB_OTG_SS0_CLKCTRL, am4_usb_otg_ss0_bit_data, CLKF_SW_SUP, "l3s_gclk", "l3s_clkdm" },
	{ AM4_USB_OTG_SS1_CLKCTRL, am4_usb_otg_ss1_bit_data, CLKF_SW_SUP, "l3s_gclk", "l3s_clkdm" },
	{ AM4_PRUSS_CLKCTRL, NULL, CLKF_SW_SUP, "pruss_ocp_gclk", "pruss_ocp_clkdm" },
	{ AM4_L4_LS_CLKCTRL, NULL, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM4_D_CAN0_CLKCTRL, NULL, CLKF_SW_SUP, "dcan0_fck" },
	{ AM4_D_CAN1_CLKCTRL, NULL, CLKF_SW_SUP, "dcan1_fck" },
	{ AM4_EPWMSS0_CLKCTRL, NULL, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM4_EPWMSS1_CLKCTRL, NULL, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM4_EPWMSS2_CLKCTRL, NULL, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM4_EPWMSS3_CLKCTRL, NULL, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM4_EPWMSS4_CLKCTRL, NULL, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM4_EPWMSS5_CLKCTRL, NULL, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM4_ELM_CLKCTRL, NULL, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM4_GPIO2_CLKCTRL, am4_gpio2_bit_data, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM4_GPIO3_CLKCTRL, am4_gpio3_bit_data, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM4_GPIO4_CLKCTRL, am4_gpio4_bit_data, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM4_GPIO5_CLKCTRL, am4_gpio5_bit_data, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM4_GPIO6_CLKCTRL, am4_gpio6_bit_data, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM4_HDQ1W_CLKCTRL, NULL, CLKF_SW_SUP, "func_12m_clk" },
	{ AM4_I2C2_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_ck" },
	{ AM4_I2C3_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_ck" },
	{ AM4_MAILBOX_CLKCTRL, NULL, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM4_MMC1_CLKCTRL, NULL, CLKF_SW_SUP, "mmc_clk" },
	{ AM4_MMC2_CLKCTRL, NULL, CLKF_SW_SUP, "mmc_clk" },
	{ AM4_RNG_CLKCTRL, NULL, CLKF_SW_SUP, "rng_fck" },
	{ AM4_SPI0_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_ck" },
	{ AM4_SPI1_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_ck" },
	{ AM4_SPI2_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_ck" },
	{ AM4_SPI3_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_ck" },
	{ AM4_SPI4_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_ck" },
	{ AM4_SPINLOCK_CLKCTRL, NULL, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM4_TIMER2_CLKCTRL, NULL, CLKF_SW_SUP, "timer2_fck" },
	{ AM4_TIMER3_CLKCTRL, NULL, CLKF_SW_SUP, "timer3_fck" },
	{ AM4_TIMER4_CLKCTRL, NULL, CLKF_SW_SUP, "timer4_fck" },
	{ AM4_TIMER5_CLKCTRL, NULL, CLKF_SW_SUP, "timer5_fck" },
	{ AM4_TIMER6_CLKCTRL, NULL, CLKF_SW_SUP, "timer6_fck" },
	{ AM4_TIMER7_CLKCTRL, NULL, CLKF_SW_SUP, "timer7_fck" },
	{ AM4_TIMER8_CLKCTRL, NULL, CLKF_SW_SUP, "timer8_fck" },
	{ AM4_TIMER9_CLKCTRL, NULL, CLKF_SW_SUP, "timer9_fck" },
	{ AM4_TIMER10_CLKCTRL, NULL, CLKF_SW_SUP, "timer10_fck" },
	{ AM4_TIMER11_CLKCTRL, NULL, CLKF_SW_SUP, "timer11_fck" },
	{ AM4_UART2_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_ck" },
	{ AM4_UART3_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_ck" },
	{ AM4_UART4_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_ck" },
	{ AM4_UART5_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_ck" },
	{ AM4_UART6_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_per_m2_div4_ck" },
	{ AM4_OCP2SCP0_CLKCTRL, NULL, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM4_OCP2SCP1_CLKCTRL, NULL, CLKF_SW_SUP, "l4ls_gclk" },
	{ AM4_EMIF_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_ddr_m2_ck", "emif_clkdm" },
	{ AM4_DSS_CORE_CLKCTRL, NULL, CLKF_SW_SUP | CLKF_SET_RATE_PARENT, "disp_clk", "dss_clkdm" },
	{ AM4_CPGMAC0_CLKCTRL, NULL, CLKF_SW_SUP, "cpsw_125mhz_gclk", "cpsw_125mhz_clkdm" },
	{ 0 },
};

const struct omap_clkctrl_data am4_clkctrl_data[] __initconst = {
	{ 0x44df2820, am4_l4_wkup_clkctrl_regs },
	{ 0x44df8320, am4_mpu_clkctrl_regs },
	{ 0x44df8420, am4_gfx_l3_clkctrl_regs },
	{ 0x44df8520, am4_l4_rtc_clkctrl_regs },
	{ 0x44df8820, am4_l4_per_clkctrl_regs },
	{ 0 },
};

const struct omap_clkctrl_data am438x_clkctrl_data[] __initconst = {
	{ 0x44df2820, am4_l4_wkup_clkctrl_regs },
	{ 0x44df8320, am4_mpu_clkctrl_regs },
	{ 0x44df8420, am4_gfx_l3_clkctrl_regs },
	{ 0x44df8820, am4_l4_per_clkctrl_regs },
	{ 0 },
};

static struct ti_dt_clk am43xx_clks[] = {
	DT_CLK(NULL, "timer_32k_ck", "clkdiv32k_ick"),
	DT_CLK(NULL, "timer_sys_ck", "sys_clkin_ck"),
	DT_CLK(NULL, "gpio0_dbclk", "l4_wkup_cm:0348:8"),
	DT_CLK(NULL, "gpio1_dbclk", "l4_per_cm:0458:8"),
	DT_CLK(NULL, "gpio2_dbclk", "l4_per_cm:0460:8"),
	DT_CLK(NULL, "gpio3_dbclk", "l4_per_cm:0468:8"),
	DT_CLK(NULL, "gpio4_dbclk", "l4_per_cm:0470:8"),
	DT_CLK(NULL, "gpio5_dbclk", "l4_per_cm:0478:8"),
	DT_CLK(NULL, "synctimer_32kclk", "l4_wkup_cm:0210:8"),
	DT_CLK(NULL, "usb_otg_ss0_refclk960m", "l4_per_cm:0240:8"),
	DT_CLK(NULL, "usb_otg_ss1_refclk960m", "l4_per_cm:0248:8"),
	{ .node_name = NULL },
};

int __init am43xx_dt_clk_init(void)
{
	struct clk *clk1, *clk2;

	ti_dt_clocks_register(am43xx_clks);

	omap2_clk_disable_autoidle_all();

	ti_clk_add_aliases();

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
