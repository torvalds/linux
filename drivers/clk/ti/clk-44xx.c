/*
 * OMAP4 Clock init
 *
 * Copyright (C) 2013 Texas Instruments, Inc.
 *
 * Tero Kristo (t-kristo@ti.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk/ti.h>

/*
 * OMAP4 ABE DPLL default frequency. In OMAP4460 TRM version V, section
 * "3.6.3.2.3 CM1_ABE Clock Generator" states that the "DPLL_ABE_X2_CLK
 * must be set to 196.608 MHz" and hence, the DPLL locked frequency is
 * half of this value.
 */
#define OMAP4_DPLL_ABE_DEFFREQ				98304000

/*
 * OMAP4 USB DPLL default frequency. In OMAP4430 TRM version V, section
 * "3.6.3.9.5 DPLL_USB Preferred Settings" shows that the preferred
 * locked frequency for the USB DPLL is 960MHz.
 */
#define OMAP4_DPLL_USB_DEFFREQ				960000000

static struct ti_dt_clk omap44xx_clks[] = {
	DT_CLK(NULL, "extalt_clkin_ck", "extalt_clkin_ck"),
	DT_CLK(NULL, "pad_clks_src_ck", "pad_clks_src_ck"),
	DT_CLK(NULL, "pad_clks_ck", "pad_clks_ck"),
	DT_CLK(NULL, "pad_slimbus_core_clks_ck", "pad_slimbus_core_clks_ck"),
	DT_CLK(NULL, "secure_32k_clk_src_ck", "secure_32k_clk_src_ck"),
	DT_CLK(NULL, "slimbus_src_clk", "slimbus_src_clk"),
	DT_CLK(NULL, "slimbus_clk", "slimbus_clk"),
	DT_CLK(NULL, "sys_32k_ck", "sys_32k_ck"),
	DT_CLK(NULL, "virt_12000000_ck", "virt_12000000_ck"),
	DT_CLK(NULL, "virt_13000000_ck", "virt_13000000_ck"),
	DT_CLK(NULL, "virt_16800000_ck", "virt_16800000_ck"),
	DT_CLK(NULL, "virt_19200000_ck", "virt_19200000_ck"),
	DT_CLK(NULL, "virt_26000000_ck", "virt_26000000_ck"),
	DT_CLK(NULL, "virt_27000000_ck", "virt_27000000_ck"),
	DT_CLK(NULL, "virt_38400000_ck", "virt_38400000_ck"),
	DT_CLK(NULL, "sys_clkin_ck", "sys_clkin_ck"),
	DT_CLK(NULL, "tie_low_clock_ck", "tie_low_clock_ck"),
	DT_CLK(NULL, "utmi_phy_clkout_ck", "utmi_phy_clkout_ck"),
	DT_CLK(NULL, "xclk60mhsp1_ck", "xclk60mhsp1_ck"),
	DT_CLK(NULL, "xclk60mhsp2_ck", "xclk60mhsp2_ck"),
	DT_CLK(NULL, "xclk60motg_ck", "xclk60motg_ck"),
	DT_CLK(NULL, "abe_dpll_bypass_clk_mux_ck", "abe_dpll_bypass_clk_mux_ck"),
	DT_CLK(NULL, "abe_dpll_refclk_mux_ck", "abe_dpll_refclk_mux_ck"),
	DT_CLK(NULL, "dpll_abe_ck", "dpll_abe_ck"),
	DT_CLK(NULL, "dpll_abe_x2_ck", "dpll_abe_x2_ck"),
	DT_CLK(NULL, "dpll_abe_m2x2_ck", "dpll_abe_m2x2_ck"),
	DT_CLK(NULL, "abe_24m_fclk", "abe_24m_fclk"),
	DT_CLK(NULL, "abe_clk", "abe_clk"),
	DT_CLK(NULL, "aess_fclk", "aess_fclk"),
	DT_CLK(NULL, "dpll_abe_m3x2_ck", "dpll_abe_m3x2_ck"),
	DT_CLK(NULL, "core_hsd_byp_clk_mux_ck", "core_hsd_byp_clk_mux_ck"),
	DT_CLK(NULL, "dpll_core_ck", "dpll_core_ck"),
	DT_CLK(NULL, "dpll_core_x2_ck", "dpll_core_x2_ck"),
	DT_CLK(NULL, "dpll_core_m6x2_ck", "dpll_core_m6x2_ck"),
	DT_CLK(NULL, "dbgclk_mux_ck", "dbgclk_mux_ck"),
	DT_CLK(NULL, "dpll_core_m2_ck", "dpll_core_m2_ck"),
	DT_CLK(NULL, "ddrphy_ck", "ddrphy_ck"),
	DT_CLK(NULL, "dpll_core_m5x2_ck", "dpll_core_m5x2_ck"),
	DT_CLK(NULL, "div_core_ck", "div_core_ck"),
	DT_CLK(NULL, "div_iva_hs_clk", "div_iva_hs_clk"),
	DT_CLK(NULL, "div_mpu_hs_clk", "div_mpu_hs_clk"),
	DT_CLK(NULL, "dpll_core_m4x2_ck", "dpll_core_m4x2_ck"),
	DT_CLK(NULL, "dll_clk_div_ck", "dll_clk_div_ck"),
	DT_CLK(NULL, "dpll_abe_m2_ck", "dpll_abe_m2_ck"),
	DT_CLK(NULL, "dpll_core_m3x2_ck", "dpll_core_m3x2_ck"),
	DT_CLK(NULL, "dpll_core_m7x2_ck", "dpll_core_m7x2_ck"),
	DT_CLK(NULL, "iva_hsd_byp_clk_mux_ck", "iva_hsd_byp_clk_mux_ck"),
	DT_CLK(NULL, "dpll_iva_ck", "dpll_iva_ck"),
	DT_CLK(NULL, "dpll_iva_x2_ck", "dpll_iva_x2_ck"),
	DT_CLK(NULL, "dpll_iva_m4x2_ck", "dpll_iva_m4x2_ck"),
	DT_CLK(NULL, "dpll_iva_m5x2_ck", "dpll_iva_m5x2_ck"),
	DT_CLK(NULL, "dpll_mpu_ck", "dpll_mpu_ck"),
	DT_CLK(NULL, "dpll_mpu_m2_ck", "dpll_mpu_m2_ck"),
	DT_CLK(NULL, "per_hs_clk_div_ck", "per_hs_clk_div_ck"),
	DT_CLK(NULL, "per_hsd_byp_clk_mux_ck", "per_hsd_byp_clk_mux_ck"),
	DT_CLK(NULL, "dpll_per_ck", "dpll_per_ck"),
	DT_CLK(NULL, "dpll_per_m2_ck", "dpll_per_m2_ck"),
	DT_CLK(NULL, "dpll_per_x2_ck", "dpll_per_x2_ck"),
	DT_CLK(NULL, "dpll_per_m2x2_ck", "dpll_per_m2x2_ck"),
	DT_CLK(NULL, "dpll_per_m3x2_ck", "dpll_per_m3x2_ck"),
	DT_CLK(NULL, "dpll_per_m4x2_ck", "dpll_per_m4x2_ck"),
	DT_CLK(NULL, "dpll_per_m5x2_ck", "dpll_per_m5x2_ck"),
	DT_CLK(NULL, "dpll_per_m6x2_ck", "dpll_per_m6x2_ck"),
	DT_CLK(NULL, "dpll_per_m7x2_ck", "dpll_per_m7x2_ck"),
	DT_CLK(NULL, "usb_hs_clk_div_ck", "usb_hs_clk_div_ck"),
	DT_CLK(NULL, "dpll_usb_ck", "dpll_usb_ck"),
	DT_CLK(NULL, "dpll_usb_clkdcoldo_ck", "dpll_usb_clkdcoldo_ck"),
	DT_CLK(NULL, "dpll_usb_m2_ck", "dpll_usb_m2_ck"),
	DT_CLK(NULL, "ducati_clk_mux_ck", "ducati_clk_mux_ck"),
	DT_CLK(NULL, "func_12m_fclk", "func_12m_fclk"),
	DT_CLK(NULL, "func_24m_clk", "func_24m_clk"),
	DT_CLK(NULL, "func_24mc_fclk", "func_24mc_fclk"),
	DT_CLK(NULL, "func_48m_fclk", "func_48m_fclk"),
	DT_CLK(NULL, "func_48mc_fclk", "func_48mc_fclk"),
	DT_CLK(NULL, "func_64m_fclk", "func_64m_fclk"),
	DT_CLK(NULL, "func_96m_fclk", "func_96m_fclk"),
	DT_CLK(NULL, "init_60m_fclk", "init_60m_fclk"),
	DT_CLK(NULL, "l3_div_ck", "l3_div_ck"),
	DT_CLK(NULL, "l4_div_ck", "l4_div_ck"),
	DT_CLK(NULL, "lp_clk_div_ck", "lp_clk_div_ck"),
	DT_CLK(NULL, "l4_wkup_clk_mux_ck", "l4_wkup_clk_mux_ck"),
	DT_CLK("smp_twd", NULL, "mpu_periphclk"),
	DT_CLK(NULL, "ocp_abe_iclk", "ocp_abe_iclk"),
	DT_CLK(NULL, "per_abe_24m_fclk", "per_abe_24m_fclk"),
	DT_CLK(NULL, "per_abe_nc_fclk", "per_abe_nc_fclk"),
	DT_CLK(NULL, "syc_clk_div_ck", "syc_clk_div_ck"),
	DT_CLK(NULL, "aes1_fck", "aes1_fck"),
	DT_CLK(NULL, "aes2_fck", "aes2_fck"),
	DT_CLK(NULL, "dmic_sync_mux_ck", "dmic_sync_mux_ck"),
	DT_CLK(NULL, "func_dmic_abe_gfclk", "func_dmic_abe_gfclk"),
	DT_CLK(NULL, "dss_sys_clk", "dss_sys_clk"),
	DT_CLK(NULL, "dss_tv_clk", "dss_tv_clk"),
	DT_CLK(NULL, "dss_dss_clk", "dss_dss_clk"),
	DT_CLK(NULL, "dss_48mhz_clk", "dss_48mhz_clk"),
	DT_CLK(NULL, "dss_fck", "dss_fck"),
	DT_CLK("omapdss_dss", "ick", "dss_fck"),
	DT_CLK(NULL, "fdif_fck", "fdif_fck"),
	DT_CLK(NULL, "gpio1_dbclk", "gpio1_dbclk"),
	DT_CLK(NULL, "gpio2_dbclk", "gpio2_dbclk"),
	DT_CLK(NULL, "gpio3_dbclk", "gpio3_dbclk"),
	DT_CLK(NULL, "gpio4_dbclk", "gpio4_dbclk"),
	DT_CLK(NULL, "gpio5_dbclk", "gpio5_dbclk"),
	DT_CLK(NULL, "gpio6_dbclk", "gpio6_dbclk"),
	DT_CLK(NULL, "sgx_clk_mux", "sgx_clk_mux"),
	DT_CLK(NULL, "hsi_fck", "hsi_fck"),
	DT_CLK(NULL, "iss_ctrlclk", "iss_ctrlclk"),
	DT_CLK(NULL, "mcasp_sync_mux_ck", "mcasp_sync_mux_ck"),
	DT_CLK(NULL, "func_mcasp_abe_gfclk", "func_mcasp_abe_gfclk"),
	DT_CLK(NULL, "mcbsp1_sync_mux_ck", "mcbsp1_sync_mux_ck"),
	DT_CLK(NULL, "func_mcbsp1_gfclk", "func_mcbsp1_gfclk"),
	DT_CLK(NULL, "mcbsp2_sync_mux_ck", "mcbsp2_sync_mux_ck"),
	DT_CLK(NULL, "func_mcbsp2_gfclk", "func_mcbsp2_gfclk"),
	DT_CLK(NULL, "mcbsp3_sync_mux_ck", "mcbsp3_sync_mux_ck"),
	DT_CLK(NULL, "func_mcbsp3_gfclk", "func_mcbsp3_gfclk"),
	DT_CLK(NULL, "mcbsp4_sync_mux_ck", "mcbsp4_sync_mux_ck"),
	DT_CLK(NULL, "per_mcbsp4_gfclk", "per_mcbsp4_gfclk"),
	DT_CLK(NULL, "hsmmc1_fclk", "hsmmc1_fclk"),
	DT_CLK(NULL, "hsmmc2_fclk", "hsmmc2_fclk"),
	DT_CLK(NULL, "ocp2scp_usb_phy_phy_48m", "ocp2scp_usb_phy_phy_48m"),
	DT_CLK(NULL, "sha2md5_fck", "sha2md5_fck"),
	DT_CLK(NULL, "slimbus1_fclk_1", "slimbus1_fclk_1"),
	DT_CLK(NULL, "slimbus1_fclk_0", "slimbus1_fclk_0"),
	DT_CLK(NULL, "slimbus1_fclk_2", "slimbus1_fclk_2"),
	DT_CLK(NULL, "slimbus1_slimbus_clk", "slimbus1_slimbus_clk"),
	DT_CLK(NULL, "slimbus2_fclk_1", "slimbus2_fclk_1"),
	DT_CLK(NULL, "slimbus2_fclk_0", "slimbus2_fclk_0"),
	DT_CLK(NULL, "slimbus2_slimbus_clk", "slimbus2_slimbus_clk"),
	DT_CLK(NULL, "smartreflex_core_fck", "smartreflex_core_fck"),
	DT_CLK(NULL, "smartreflex_iva_fck", "smartreflex_iva_fck"),
	DT_CLK(NULL, "smartreflex_mpu_fck", "smartreflex_mpu_fck"),
	DT_CLK(NULL, "dmt1_clk_mux", "dmt1_clk_mux"),
	DT_CLK(NULL, "cm2_dm10_mux", "cm2_dm10_mux"),
	DT_CLK(NULL, "cm2_dm11_mux", "cm2_dm11_mux"),
	DT_CLK(NULL, "cm2_dm2_mux", "cm2_dm2_mux"),
	DT_CLK(NULL, "cm2_dm3_mux", "cm2_dm3_mux"),
	DT_CLK(NULL, "cm2_dm4_mux", "cm2_dm4_mux"),
	DT_CLK(NULL, "timer5_sync_mux", "timer5_sync_mux"),
	DT_CLK(NULL, "timer6_sync_mux", "timer6_sync_mux"),
	DT_CLK(NULL, "timer7_sync_mux", "timer7_sync_mux"),
	DT_CLK(NULL, "timer8_sync_mux", "timer8_sync_mux"),
	DT_CLK(NULL, "cm2_dm9_mux", "cm2_dm9_mux"),
	DT_CLK(NULL, "usb_host_fs_fck", "usb_host_fs_fck"),
	DT_CLK("usbhs_omap", "fs_fck", "usb_host_fs_fck"),
	DT_CLK(NULL, "utmi_p1_gfclk", "utmi_p1_gfclk"),
	DT_CLK(NULL, "usb_host_hs_utmi_p1_clk", "usb_host_hs_utmi_p1_clk"),
	DT_CLK(NULL, "utmi_p2_gfclk", "utmi_p2_gfclk"),
	DT_CLK(NULL, "usb_host_hs_utmi_p2_clk", "usb_host_hs_utmi_p2_clk"),
	DT_CLK(NULL, "usb_host_hs_utmi_p3_clk", "usb_host_hs_utmi_p3_clk"),
	DT_CLK(NULL, "usb_host_hs_hsic480m_p1_clk", "usb_host_hs_hsic480m_p1_clk"),
	DT_CLK(NULL, "usb_host_hs_hsic60m_p1_clk", "usb_host_hs_hsic60m_p1_clk"),
	DT_CLK(NULL, "usb_host_hs_hsic60m_p2_clk", "usb_host_hs_hsic60m_p2_clk"),
	DT_CLK(NULL, "usb_host_hs_hsic480m_p2_clk", "usb_host_hs_hsic480m_p2_clk"),
	DT_CLK(NULL, "usb_host_hs_func48mclk", "usb_host_hs_func48mclk"),
	DT_CLK(NULL, "usb_host_hs_fck", "usb_host_hs_fck"),
	DT_CLK("usbhs_omap", "hs_fck", "usb_host_hs_fck"),
	DT_CLK(NULL, "otg_60m_gfclk", "otg_60m_gfclk"),
	DT_CLK(NULL, "usb_otg_hs_xclk", "usb_otg_hs_xclk"),
	DT_CLK(NULL, "usb_otg_hs_ick", "usb_otg_hs_ick"),
	DT_CLK("musb-omap2430", "ick", "usb_otg_hs_ick"),
	DT_CLK(NULL, "usb_phy_cm_clk32k", "usb_phy_cm_clk32k"),
	DT_CLK(NULL, "usb_tll_hs_usb_ch2_clk", "usb_tll_hs_usb_ch2_clk"),
	DT_CLK(NULL, "usb_tll_hs_usb_ch0_clk", "usb_tll_hs_usb_ch0_clk"),
	DT_CLK(NULL, "usb_tll_hs_usb_ch1_clk", "usb_tll_hs_usb_ch1_clk"),
	DT_CLK(NULL, "usb_tll_hs_ick", "usb_tll_hs_ick"),
	DT_CLK("usbhs_omap", "usbtll_ick", "usb_tll_hs_ick"),
	DT_CLK("usbhs_tll", "usbtll_ick", "usb_tll_hs_ick"),
	DT_CLK(NULL, "usim_ck", "usim_ck"),
	DT_CLK(NULL, "usim_fclk", "usim_fclk"),
	DT_CLK(NULL, "pmd_stm_clock_mux_ck", "pmd_stm_clock_mux_ck"),
	DT_CLK(NULL, "pmd_trace_clk_mux_ck", "pmd_trace_clk_mux_ck"),
	DT_CLK(NULL, "stm_clk_div_ck", "stm_clk_div_ck"),
	DT_CLK(NULL, "trace_clk_div_ck", "trace_clk_div_ck"),
	DT_CLK(NULL, "auxclk0_src_ck", "auxclk0_src_ck"),
	DT_CLK(NULL, "auxclk0_ck", "auxclk0_ck"),
	DT_CLK(NULL, "auxclkreq0_ck", "auxclkreq0_ck"),
	DT_CLK(NULL, "auxclk1_src_ck", "auxclk1_src_ck"),
	DT_CLK(NULL, "auxclk1_ck", "auxclk1_ck"),
	DT_CLK(NULL, "auxclkreq1_ck", "auxclkreq1_ck"),
	DT_CLK(NULL, "auxclk2_src_ck", "auxclk2_src_ck"),
	DT_CLK(NULL, "auxclk2_ck", "auxclk2_ck"),
	DT_CLK(NULL, "auxclkreq2_ck", "auxclkreq2_ck"),
	DT_CLK(NULL, "auxclk3_src_ck", "auxclk3_src_ck"),
	DT_CLK(NULL, "auxclk3_ck", "auxclk3_ck"),
	DT_CLK(NULL, "auxclkreq3_ck", "auxclkreq3_ck"),
	DT_CLK(NULL, "auxclk4_src_ck", "auxclk4_src_ck"),
	DT_CLK(NULL, "auxclk4_ck", "auxclk4_ck"),
	DT_CLK(NULL, "auxclkreq4_ck", "auxclkreq4_ck"),
	DT_CLK(NULL, "auxclk5_src_ck", "auxclk5_src_ck"),
	DT_CLK(NULL, "auxclk5_ck", "auxclk5_ck"),
	DT_CLK(NULL, "auxclkreq5_ck", "auxclkreq5_ck"),
	DT_CLK("omap_i2c.1", "ick", "dummy_ck"),
	DT_CLK("omap_i2c.2", "ick", "dummy_ck"),
	DT_CLK("omap_i2c.3", "ick", "dummy_ck"),
	DT_CLK("omap_i2c.4", "ick", "dummy_ck"),
	DT_CLK(NULL, "mailboxes_ick", "dummy_ck"),
	DT_CLK("omap_hsmmc.0", "ick", "dummy_ck"),
	DT_CLK("omap_hsmmc.1", "ick", "dummy_ck"),
	DT_CLK("omap_hsmmc.2", "ick", "dummy_ck"),
	DT_CLK("omap_hsmmc.3", "ick", "dummy_ck"),
	DT_CLK("omap_hsmmc.4", "ick", "dummy_ck"),
	DT_CLK("omap-mcbsp.1", "ick", "dummy_ck"),
	DT_CLK("omap-mcbsp.2", "ick", "dummy_ck"),
	DT_CLK("omap-mcbsp.3", "ick", "dummy_ck"),
	DT_CLK("omap-mcbsp.4", "ick", "dummy_ck"),
	DT_CLK("omap2_mcspi.1", "ick", "dummy_ck"),
	DT_CLK("omap2_mcspi.2", "ick", "dummy_ck"),
	DT_CLK("omap2_mcspi.3", "ick", "dummy_ck"),
	DT_CLK("omap2_mcspi.4", "ick", "dummy_ck"),
	DT_CLK(NULL, "uart1_ick", "dummy_ck"),
	DT_CLK(NULL, "uart2_ick", "dummy_ck"),
	DT_CLK(NULL, "uart3_ick", "dummy_ck"),
	DT_CLK(NULL, "uart4_ick", "dummy_ck"),
	DT_CLK("usbhs_omap", "usbhost_ick", "dummy_ck"),
	DT_CLK("usbhs_omap", "usbtll_fck", "dummy_ck"),
	DT_CLK("usbhs_tll", "usbtll_fck", "dummy_ck"),
	DT_CLK("omap_wdt", "ick", "dummy_ck"),
	DT_CLK(NULL, "timer_32k_ck", "sys_32k_ck"),
	DT_CLK("4a318000.timer", "timer_sys_ck", "sys_clkin_ck"),
	DT_CLK("48032000.timer", "timer_sys_ck", "sys_clkin_ck"),
	DT_CLK("48034000.timer", "timer_sys_ck", "sys_clkin_ck"),
	DT_CLK("48036000.timer", "timer_sys_ck", "sys_clkin_ck"),
	DT_CLK("4803e000.timer", "timer_sys_ck", "sys_clkin_ck"),
	DT_CLK("48086000.timer", "timer_sys_ck", "sys_clkin_ck"),
	DT_CLK("48088000.timer", "timer_sys_ck", "sys_clkin_ck"),
	DT_CLK("40138000.timer", "timer_sys_ck", "syc_clk_div_ck"),
	DT_CLK("4013a000.timer", "timer_sys_ck", "syc_clk_div_ck"),
	DT_CLK("4013c000.timer", "timer_sys_ck", "syc_clk_div_ck"),
	DT_CLK("4013e000.timer", "timer_sys_ck", "syc_clk_div_ck"),
	DT_CLK(NULL, "cpufreq_ck", "dpll_mpu_ck"),
	DT_CLK(NULL, "bandgap_fclk", "bandgap_fclk"),
	DT_CLK(NULL, "div_ts_ck", "div_ts_ck"),
	DT_CLK(NULL, "bandgap_ts_fclk", "bandgap_ts_fclk"),
	{ .node_name = NULL },
};

int __init omap4xxx_dt_clk_init(void)
{
	int rc;
	struct clk *abe_dpll_ref, *abe_dpll, *sys_32k_ck, *usb_dpll;

	ti_dt_clocks_register(omap44xx_clks);

	omap2_clk_disable_autoidle_all();

	/*
	 * Lock USB DPLL on OMAP4 devices so that the L3INIT power
	 * domain can transition to retention state when not in use.
	 */
	usb_dpll = clk_get_sys(NULL, "dpll_usb_ck");
	rc = clk_set_rate(usb_dpll, OMAP4_DPLL_USB_DEFFREQ);
	if (rc)
		pr_err("%s: failed to configure USB DPLL!\n", __func__);

	/*
	 * On OMAP4460 the ABE DPLL fails to turn on if in idle low-power
	 * state when turning the ABE clock domain. Workaround this by
	 * locking the ABE DPLL on boot.
	 * Lock the ABE DPLL in any case to avoid issues with audio.
	 */
	abe_dpll_ref = clk_get_sys(NULL, "abe_dpll_refclk_mux_ck");
	sys_32k_ck = clk_get_sys(NULL, "sys_32k_ck");
	rc = clk_set_parent(abe_dpll_ref, sys_32k_ck);
	abe_dpll = clk_get_sys(NULL, "dpll_abe_ck");
	if (!rc)
		rc = clk_set_rate(abe_dpll, OMAP4_DPLL_ABE_DEFFREQ);
	if (rc)
		pr_err("%s: failed to configure ABE DPLL!\n", __func__);

	return 0;
}
