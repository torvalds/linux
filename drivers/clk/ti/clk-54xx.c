/*
 * OMAP5 Clock init
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
#include <linux/io.h>
#include <linux/clk/ti.h>

#define OMAP5_DPLL_ABE_DEFFREQ				98304000

/*
 * OMAP543x TRM, section "3.6.3.9.5 DPLL_USB Preferred Settings"
 * states it must be at 960MHz
 */
#define OMAP5_DPLL_USB_DEFFREQ				960000000

static struct ti_dt_clk omap54xx_clks[] = {
	DT_CLK(NULL, "pad_clks_src_ck", "pad_clks_src_ck"),
	DT_CLK(NULL, "pad_clks_ck", "pad_clks_ck"),
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
	DT_CLK(NULL, "sys_clkin", "sys_clkin"),
	DT_CLK(NULL, "xclk60mhsp1_ck", "xclk60mhsp1_ck"),
	DT_CLK(NULL, "xclk60mhsp2_ck", "xclk60mhsp2_ck"),
	DT_CLK(NULL, "abe_dpll_bypass_clk_mux", "abe_dpll_bypass_clk_mux"),
	DT_CLK(NULL, "abe_dpll_clk_mux", "abe_dpll_clk_mux"),
	DT_CLK(NULL, "dpll_abe_ck", "dpll_abe_ck"),
	DT_CLK(NULL, "dpll_abe_x2_ck", "dpll_abe_x2_ck"),
	DT_CLK(NULL, "dpll_abe_m2x2_ck", "dpll_abe_m2x2_ck"),
	DT_CLK(NULL, "abe_24m_fclk", "abe_24m_fclk"),
	DT_CLK(NULL, "abe_clk", "abe_clk"),
	DT_CLK(NULL, "abe_iclk", "abe_iclk"),
	DT_CLK(NULL, "abe_lp_clk_div", "abe_lp_clk_div"),
	DT_CLK(NULL, "dpll_abe_m3x2_ck", "dpll_abe_m3x2_ck"),
	DT_CLK(NULL, "dpll_core_ck", "dpll_core_ck"),
	DT_CLK(NULL, "dpll_core_x2_ck", "dpll_core_x2_ck"),
	DT_CLK(NULL, "dpll_core_h21x2_ck", "dpll_core_h21x2_ck"),
	DT_CLK(NULL, "c2c_fclk", "c2c_fclk"),
	DT_CLK(NULL, "c2c_iclk", "c2c_iclk"),
	DT_CLK(NULL, "custefuse_sys_gfclk_div", "custefuse_sys_gfclk_div"),
	DT_CLK(NULL, "dpll_core_h11x2_ck", "dpll_core_h11x2_ck"),
	DT_CLK(NULL, "dpll_core_h12x2_ck", "dpll_core_h12x2_ck"),
	DT_CLK(NULL, "dpll_core_h13x2_ck", "dpll_core_h13x2_ck"),
	DT_CLK(NULL, "dpll_core_h14x2_ck", "dpll_core_h14x2_ck"),
	DT_CLK(NULL, "dpll_core_h22x2_ck", "dpll_core_h22x2_ck"),
	DT_CLK(NULL, "dpll_core_h23x2_ck", "dpll_core_h23x2_ck"),
	DT_CLK(NULL, "dpll_core_h24x2_ck", "dpll_core_h24x2_ck"),
	DT_CLK(NULL, "dpll_core_m2_ck", "dpll_core_m2_ck"),
	DT_CLK(NULL, "dpll_core_m3x2_ck", "dpll_core_m3x2_ck"),
	DT_CLK(NULL, "iva_dpll_hs_clk_div", "iva_dpll_hs_clk_div"),
	DT_CLK(NULL, "dpll_iva_ck", "dpll_iva_ck"),
	DT_CLK(NULL, "dpll_iva_x2_ck", "dpll_iva_x2_ck"),
	DT_CLK(NULL, "dpll_iva_h11x2_ck", "dpll_iva_h11x2_ck"),
	DT_CLK(NULL, "dpll_iva_h12x2_ck", "dpll_iva_h12x2_ck"),
	DT_CLK(NULL, "mpu_dpll_hs_clk_div", "mpu_dpll_hs_clk_div"),
	DT_CLK(NULL, "dpll_mpu_ck", "dpll_mpu_ck"),
	DT_CLK(NULL, "dpll_mpu_m2_ck", "dpll_mpu_m2_ck"),
	DT_CLK(NULL, "per_dpll_hs_clk_div", "per_dpll_hs_clk_div"),
	DT_CLK(NULL, "dpll_per_ck", "dpll_per_ck"),
	DT_CLK(NULL, "dpll_per_x2_ck", "dpll_per_x2_ck"),
	DT_CLK(NULL, "dpll_per_h11x2_ck", "dpll_per_h11x2_ck"),
	DT_CLK(NULL, "dpll_per_h12x2_ck", "dpll_per_h12x2_ck"),
	DT_CLK(NULL, "dpll_per_h14x2_ck", "dpll_per_h14x2_ck"),
	DT_CLK(NULL, "dpll_per_m2_ck", "dpll_per_m2_ck"),
	DT_CLK(NULL, "dpll_per_m2x2_ck", "dpll_per_m2x2_ck"),
	DT_CLK(NULL, "dpll_per_m3x2_ck", "dpll_per_m3x2_ck"),
	DT_CLK(NULL, "dpll_unipro1_ck", "dpll_unipro1_ck"),
	DT_CLK(NULL, "dpll_unipro1_clkdcoldo", "dpll_unipro1_clkdcoldo"),
	DT_CLK(NULL, "dpll_unipro1_m2_ck", "dpll_unipro1_m2_ck"),
	DT_CLK(NULL, "dpll_unipro2_ck", "dpll_unipro2_ck"),
	DT_CLK(NULL, "dpll_unipro2_clkdcoldo", "dpll_unipro2_clkdcoldo"),
	DT_CLK(NULL, "dpll_unipro2_m2_ck", "dpll_unipro2_m2_ck"),
	DT_CLK(NULL, "usb_dpll_hs_clk_div", "usb_dpll_hs_clk_div"),
	DT_CLK(NULL, "dpll_usb_ck", "dpll_usb_ck"),
	DT_CLK(NULL, "dpll_usb_clkdcoldo", "dpll_usb_clkdcoldo"),
	DT_CLK(NULL, "dpll_usb_m2_ck", "dpll_usb_m2_ck"),
	DT_CLK(NULL, "dss_syc_gfclk_div", "dss_syc_gfclk_div"),
	DT_CLK(NULL, "func_128m_clk", "func_128m_clk"),
	DT_CLK(NULL, "func_12m_fclk", "func_12m_fclk"),
	DT_CLK(NULL, "func_24m_clk", "func_24m_clk"),
	DT_CLK(NULL, "func_48m_fclk", "func_48m_fclk"),
	DT_CLK(NULL, "func_96m_fclk", "func_96m_fclk"),
	DT_CLK(NULL, "l3_iclk_div", "l3_iclk_div"),
	DT_CLK(NULL, "gpu_l3_iclk", "gpu_l3_iclk"),
	DT_CLK(NULL, "l3init_60m_fclk", "l3init_60m_fclk"),
	DT_CLK(NULL, "wkupaon_iclk_mux", "wkupaon_iclk_mux"),
	DT_CLK(NULL, "l3instr_ts_gclk_div", "l3instr_ts_gclk_div"),
	DT_CLK(NULL, "l4_root_clk_div", "l4_root_clk_div"),
	DT_CLK(NULL, "dss_32khz_clk", "dss_32khz_clk"),
	DT_CLK(NULL, "dss_48mhz_clk", "dss_48mhz_clk"),
	DT_CLK(NULL, "dss_dss_clk", "dss_dss_clk"),
	DT_CLK(NULL, "dss_sys_clk", "dss_sys_clk"),
	DT_CLK(NULL, "gpio1_dbclk", "gpio1_dbclk"),
	DT_CLK(NULL, "gpio2_dbclk", "gpio2_dbclk"),
	DT_CLK(NULL, "gpio3_dbclk", "gpio3_dbclk"),
	DT_CLK(NULL, "gpio4_dbclk", "gpio4_dbclk"),
	DT_CLK(NULL, "gpio5_dbclk", "gpio5_dbclk"),
	DT_CLK(NULL, "gpio6_dbclk", "gpio6_dbclk"),
	DT_CLK(NULL, "gpio7_dbclk", "gpio7_dbclk"),
	DT_CLK(NULL, "gpio8_dbclk", "gpio8_dbclk"),
	DT_CLK(NULL, "iss_ctrlclk", "iss_ctrlclk"),
	DT_CLK(NULL, "lli_txphy_clk", "lli_txphy_clk"),
	DT_CLK(NULL, "lli_txphy_ls_clk", "lli_txphy_ls_clk"),
	DT_CLK(NULL, "mmc1_32khz_clk", "mmc1_32khz_clk"),
	DT_CLK(NULL, "sata_ref_clk", "sata_ref_clk"),
	DT_CLK(NULL, "slimbus1_slimbus_clk", "slimbus1_slimbus_clk"),
	DT_CLK(NULL, "usb_host_hs_hsic480m_p1_clk", "usb_host_hs_hsic480m_p1_clk"),
	DT_CLK(NULL, "usb_host_hs_hsic480m_p2_clk", "usb_host_hs_hsic480m_p2_clk"),
	DT_CLK(NULL, "usb_host_hs_hsic480m_p3_clk", "usb_host_hs_hsic480m_p3_clk"),
	DT_CLK(NULL, "usb_host_hs_hsic60m_p1_clk", "usb_host_hs_hsic60m_p1_clk"),
	DT_CLK(NULL, "usb_host_hs_hsic60m_p2_clk", "usb_host_hs_hsic60m_p2_clk"),
	DT_CLK(NULL, "usb_host_hs_hsic60m_p3_clk", "usb_host_hs_hsic60m_p3_clk"),
	DT_CLK(NULL, "usb_host_hs_utmi_p1_clk", "usb_host_hs_utmi_p1_clk"),
	DT_CLK(NULL, "usb_host_hs_utmi_p2_clk", "usb_host_hs_utmi_p2_clk"),
	DT_CLK(NULL, "usb_host_hs_utmi_p3_clk", "usb_host_hs_utmi_p3_clk"),
	DT_CLK(NULL, "usb_otg_ss_refclk960m", "usb_otg_ss_refclk960m"),
	DT_CLK(NULL, "usb_phy_cm_clk32k", "usb_phy_cm_clk32k"),
	DT_CLK(NULL, "usb_tll_hs_usb_ch0_clk", "usb_tll_hs_usb_ch0_clk"),
	DT_CLK(NULL, "usb_tll_hs_usb_ch1_clk", "usb_tll_hs_usb_ch1_clk"),
	DT_CLK(NULL, "usb_tll_hs_usb_ch2_clk", "usb_tll_hs_usb_ch2_clk"),
	DT_CLK(NULL, "aess_fclk", "aess_fclk"),
	DT_CLK(NULL, "dmic_sync_mux_ck", "dmic_sync_mux_ck"),
	DT_CLK(NULL, "dmic_gfclk", "dmic_gfclk"),
	DT_CLK(NULL, "fdif_fclk", "fdif_fclk"),
	DT_CLK(NULL, "gpu_core_gclk_mux", "gpu_core_gclk_mux"),
	DT_CLK(NULL, "gpu_hyd_gclk_mux", "gpu_hyd_gclk_mux"),
	DT_CLK(NULL, "hsi_fclk", "hsi_fclk"),
	DT_CLK(NULL, "mcasp_sync_mux_ck", "mcasp_sync_mux_ck"),
	DT_CLK(NULL, "mcasp_gfclk", "mcasp_gfclk"),
	DT_CLK(NULL, "mcbsp1_sync_mux_ck", "mcbsp1_sync_mux_ck"),
	DT_CLK(NULL, "mcbsp1_gfclk", "mcbsp1_gfclk"),
	DT_CLK(NULL, "mcbsp2_sync_mux_ck", "mcbsp2_sync_mux_ck"),
	DT_CLK(NULL, "mcbsp2_gfclk", "mcbsp2_gfclk"),
	DT_CLK(NULL, "mcbsp3_sync_mux_ck", "mcbsp3_sync_mux_ck"),
	DT_CLK(NULL, "mcbsp3_gfclk", "mcbsp3_gfclk"),
	DT_CLK(NULL, "mmc1_fclk_mux", "mmc1_fclk_mux"),
	DT_CLK(NULL, "mmc1_fclk", "mmc1_fclk"),
	DT_CLK(NULL, "mmc2_fclk_mux", "mmc2_fclk_mux"),
	DT_CLK(NULL, "mmc2_fclk", "mmc2_fclk"),
	DT_CLK(NULL, "timer10_gfclk_mux", "timer10_gfclk_mux"),
	DT_CLK(NULL, "timer11_gfclk_mux", "timer11_gfclk_mux"),
	DT_CLK(NULL, "timer1_gfclk_mux", "timer1_gfclk_mux"),
	DT_CLK(NULL, "timer2_gfclk_mux", "timer2_gfclk_mux"),
	DT_CLK(NULL, "timer3_gfclk_mux", "timer3_gfclk_mux"),
	DT_CLK(NULL, "timer4_gfclk_mux", "timer4_gfclk_mux"),
	DT_CLK(NULL, "timer5_gfclk_mux", "timer5_gfclk_mux"),
	DT_CLK(NULL, "timer6_gfclk_mux", "timer6_gfclk_mux"),
	DT_CLK(NULL, "timer7_gfclk_mux", "timer7_gfclk_mux"),
	DT_CLK(NULL, "timer8_gfclk_mux", "timer8_gfclk_mux"),
	DT_CLK(NULL, "timer9_gfclk_mux", "timer9_gfclk_mux"),
	DT_CLK(NULL, "utmi_p1_gfclk", "utmi_p1_gfclk"),
	DT_CLK(NULL, "utmi_p2_gfclk", "utmi_p2_gfclk"),
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
	DT_CLK("omap_wdt", "ick", "dummy_ck"),
	DT_CLK(NULL, "timer_32k_ck", "sys_32k_ck"),
	DT_CLK("4ae18000.timer", "timer_sys_ck", "sys_clkin"),
	DT_CLK("48032000.timer", "timer_sys_ck", "sys_clkin"),
	DT_CLK("48034000.timer", "timer_sys_ck", "sys_clkin"),
	DT_CLK("48036000.timer", "timer_sys_ck", "sys_clkin"),
	DT_CLK("4803e000.timer", "timer_sys_ck", "sys_clkin"),
	DT_CLK("48086000.timer", "timer_sys_ck", "sys_clkin"),
	DT_CLK("48088000.timer", "timer_sys_ck", "sys_clkin"),
	DT_CLK("40138000.timer", "timer_sys_ck", "dss_syc_gfclk_div"),
	DT_CLK("4013a000.timer", "timer_sys_ck", "dss_syc_gfclk_div"),
	DT_CLK("4013c000.timer", "timer_sys_ck", "dss_syc_gfclk_div"),
	DT_CLK("4013e000.timer", "timer_sys_ck", "dss_syc_gfclk_div"),
	{ .node_name = NULL },
};

int __init omap5xxx_dt_clk_init(void)
{
	int rc;
	struct clk *abe_dpll_ref, *abe_dpll, *sys_32k_ck, *usb_dpll;

	ti_dt_clocks_register(omap54xx_clks);

	omap2_clk_disable_autoidle_all();

	abe_dpll_ref = clk_get_sys(NULL, "abe_dpll_clk_mux");
	sys_32k_ck = clk_get_sys(NULL, "sys_32k_ck");
	rc = clk_set_parent(abe_dpll_ref, sys_32k_ck);
	abe_dpll = clk_get_sys(NULL, "dpll_abe_ck");
	if (!rc)
		rc = clk_set_rate(abe_dpll, OMAP5_DPLL_ABE_DEFFREQ);
	if (rc)
		pr_err("%s: failed to configure ABE DPLL!\n", __func__);

	abe_dpll = clk_get_sys(NULL, "dpll_abe_m2x2_ck");
	if (!rc)
		rc = clk_set_rate(abe_dpll, OMAP5_DPLL_ABE_DEFFREQ * 2);
	if (rc)
		pr_err("%s: failed to configure ABE m2x2 DPLL!\n", __func__);

	usb_dpll = clk_get_sys(NULL, "dpll_usb_ck");
	rc = clk_set_rate(usb_dpll, OMAP5_DPLL_USB_DEFFREQ);
	if (rc)
		pr_err("%s: failed to configure USB DPLL!\n", __func__);

	usb_dpll = clk_get_sys(NULL, "dpll_usb_m2_ck");
	rc = clk_set_rate(usb_dpll, OMAP5_DPLL_USB_DEFFREQ/2);
	if (rc)
		pr_err("%s: failed to set USB_DPLL M2 OUT\n", __func__);

	return 0;
}
