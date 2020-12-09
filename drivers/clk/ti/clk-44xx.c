// SPDX-License-Identifier: GPL-2.0-only
/*
 * OMAP4 Clock init
 *
 * Copyright (C) 2013 Texas Instruments, Inc.
 *
 * Tero Kristo (t-kristo@ti.com)
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk/ti.h>
#include <dt-bindings/clock/omap4.h>

#include "clock.h"

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

static const struct omap_clkctrl_reg_data omap4_mpuss_clkctrl_regs[] __initconst = {
	{ OMAP4_MPU_CLKCTRL, NULL, 0, "dpll_mpu_m2_ck" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data omap4_tesla_clkctrl_regs[] __initconst = {
	{ OMAP4_DSP_CLKCTRL, NULL, CLKF_HW_SUP | CLKF_NO_IDLEST, "dpll_iva_m4x2_ck" },
	{ 0 },
};

static const char * const omap4_aess_fclk_parents[] __initconst = {
	"abe_clk",
	NULL,
};

static const struct omap_clkctrl_div_data omap4_aess_fclk_data __initconst = {
	.max_div = 2,
};

static const struct omap_clkctrl_bit_data omap4_aess_bit_data[] __initconst = {
	{ 24, TI_CLK_DIVIDER, omap4_aess_fclk_parents, &omap4_aess_fclk_data },
	{ 0 },
};

static const char * const omap4_func_dmic_abe_gfclk_parents[] __initconst = {
	"abe_cm:clk:0018:26",
	"pad_clks_ck",
	"slimbus_clk",
	NULL,
};

static const char * const omap4_dmic_sync_mux_ck_parents[] __initconst = {
	"abe_24m_fclk",
	"syc_clk_div_ck",
	"func_24m_clk",
	NULL,
};

static const struct omap_clkctrl_bit_data omap4_dmic_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, omap4_func_dmic_abe_gfclk_parents, NULL },
	{ 26, TI_CLK_MUX, omap4_dmic_sync_mux_ck_parents, NULL },
	{ 0 },
};

static const char * const omap4_func_mcasp_abe_gfclk_parents[] __initconst = {
	"abe_cm:clk:0020:26",
	"pad_clks_ck",
	"slimbus_clk",
	NULL,
};

static const struct omap_clkctrl_bit_data omap4_mcasp_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, omap4_func_mcasp_abe_gfclk_parents, NULL },
	{ 26, TI_CLK_MUX, omap4_dmic_sync_mux_ck_parents, NULL },
	{ 0 },
};

static const char * const omap4_func_mcbsp1_gfclk_parents[] __initconst = {
	"abe_cm:clk:0028:26",
	"pad_clks_ck",
	"slimbus_clk",
	NULL,
};

static const struct omap_clkctrl_bit_data omap4_mcbsp1_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, omap4_func_mcbsp1_gfclk_parents, NULL },
	{ 26, TI_CLK_MUX, omap4_dmic_sync_mux_ck_parents, NULL },
	{ 0 },
};

static const char * const omap4_func_mcbsp2_gfclk_parents[] __initconst = {
	"abe_cm:clk:0030:26",
	"pad_clks_ck",
	"slimbus_clk",
	NULL,
};

static const struct omap_clkctrl_bit_data omap4_mcbsp2_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, omap4_func_mcbsp2_gfclk_parents, NULL },
	{ 26, TI_CLK_MUX, omap4_dmic_sync_mux_ck_parents, NULL },
	{ 0 },
};

static const char * const omap4_func_mcbsp3_gfclk_parents[] __initconst = {
	"abe_cm:clk:0038:26",
	"pad_clks_ck",
	"slimbus_clk",
	NULL,
};

static const struct omap_clkctrl_bit_data omap4_mcbsp3_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, omap4_func_mcbsp3_gfclk_parents, NULL },
	{ 26, TI_CLK_MUX, omap4_dmic_sync_mux_ck_parents, NULL },
	{ 0 },
};

static const char * const omap4_slimbus1_fclk_0_parents[] __initconst = {
	"abe_24m_fclk",
	NULL,
};

static const char * const omap4_slimbus1_fclk_1_parents[] __initconst = {
	"func_24m_clk",
	NULL,
};

static const char * const omap4_slimbus1_fclk_2_parents[] __initconst = {
	"pad_clks_ck",
	NULL,
};

static const char * const omap4_slimbus1_slimbus_clk_parents[] __initconst = {
	"slimbus_clk",
	NULL,
};

static const struct omap_clkctrl_bit_data omap4_slimbus1_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, omap4_slimbus1_fclk_0_parents, NULL },
	{ 9, TI_CLK_GATE, omap4_slimbus1_fclk_1_parents, NULL },
	{ 10, TI_CLK_GATE, omap4_slimbus1_fclk_2_parents, NULL },
	{ 11, TI_CLK_GATE, omap4_slimbus1_slimbus_clk_parents, NULL },
	{ 0 },
};

static const char * const omap4_timer5_sync_mux_parents[] __initconst = {
	"syc_clk_div_ck",
	"sys_32k_ck",
	NULL,
};

static const struct omap_clkctrl_bit_data omap4_timer5_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, omap4_timer5_sync_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data omap4_timer6_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, omap4_timer5_sync_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data omap4_timer7_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, omap4_timer5_sync_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data omap4_timer8_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, omap4_timer5_sync_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_reg_data omap4_abe_clkctrl_regs[] __initconst = {
	{ OMAP4_L4_ABE_CLKCTRL, NULL, 0, "ocp_abe_iclk" },
	{ OMAP4_AESS_CLKCTRL, omap4_aess_bit_data, CLKF_SW_SUP, "abe_cm:clk:0008:24" },
	{ OMAP4_MCPDM_CLKCTRL, NULL, CLKF_SW_SUP, "pad_clks_ck" },
	{ OMAP4_DMIC_CLKCTRL, omap4_dmic_bit_data, CLKF_SW_SUP, "abe_cm:clk:0018:24" },
	{ OMAP4_MCASP_CLKCTRL, omap4_mcasp_bit_data, CLKF_SW_SUP, "abe_cm:clk:0020:24" },
	{ OMAP4_MCBSP1_CLKCTRL, omap4_mcbsp1_bit_data, CLKF_SW_SUP, "abe_cm:clk:0028:24" },
	{ OMAP4_MCBSP2_CLKCTRL, omap4_mcbsp2_bit_data, CLKF_SW_SUP, "abe_cm:clk:0030:24" },
	{ OMAP4_MCBSP3_CLKCTRL, omap4_mcbsp3_bit_data, CLKF_SW_SUP, "abe_cm:clk:0038:24" },
	{ OMAP4_SLIMBUS1_CLKCTRL, omap4_slimbus1_bit_data, CLKF_SW_SUP, "abe_cm:clk:0040:8" },
	{ OMAP4_TIMER5_CLKCTRL, omap4_timer5_bit_data, CLKF_SW_SUP, "abe_cm:clk:0048:24" },
	{ OMAP4_TIMER6_CLKCTRL, omap4_timer6_bit_data, CLKF_SW_SUP, "abe_cm:clk:0050:24" },
	{ OMAP4_TIMER7_CLKCTRL, omap4_timer7_bit_data, CLKF_SW_SUP, "abe_cm:clk:0058:24" },
	{ OMAP4_TIMER8_CLKCTRL, omap4_timer8_bit_data, CLKF_SW_SUP, "abe_cm:clk:0060:24" },
	{ OMAP4_WD_TIMER3_CLKCTRL, NULL, CLKF_SW_SUP, "sys_32k_ck" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data omap4_l4_ao_clkctrl_regs[] __initconst = {
	{ OMAP4_SMARTREFLEX_MPU_CLKCTRL, NULL, CLKF_SW_SUP, "l4_wkup_clk_mux_ck" },
	{ OMAP4_SMARTREFLEX_IVA_CLKCTRL, NULL, CLKF_SW_SUP, "l4_wkup_clk_mux_ck" },
	{ OMAP4_SMARTREFLEX_CORE_CLKCTRL, NULL, CLKF_SW_SUP, "l4_wkup_clk_mux_ck" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data omap4_l3_1_clkctrl_regs[] __initconst = {
	{ OMAP4_L3_MAIN_1_CLKCTRL, NULL, 0, "l3_div_ck" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data omap4_l3_2_clkctrl_regs[] __initconst = {
	{ OMAP4_L3_MAIN_2_CLKCTRL, NULL, 0, "l3_div_ck" },
	{ OMAP4_GPMC_CLKCTRL, NULL, CLKF_HW_SUP, "l3_div_ck" },
	{ OMAP4_OCMC_RAM_CLKCTRL, NULL, 0, "l3_div_ck" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data omap4_ducati_clkctrl_regs[] __initconst = {
	{ OMAP4_IPU_CLKCTRL, NULL, CLKF_HW_SUP | CLKF_NO_IDLEST, "ducati_clk_mux_ck" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data omap4_l3_dma_clkctrl_regs[] __initconst = {
	{ OMAP4_DMA_SYSTEM_CLKCTRL, NULL, 0, "l3_div_ck" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data omap4_l3_emif_clkctrl_regs[] __initconst = {
	{ OMAP4_DMM_CLKCTRL, NULL, 0, "l3_div_ck" },
	{ OMAP4_EMIF1_CLKCTRL, NULL, CLKF_HW_SUP, "ddrphy_ck" },
	{ OMAP4_EMIF2_CLKCTRL, NULL, CLKF_HW_SUP, "ddrphy_ck" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data omap4_d2d_clkctrl_regs[] __initconst = {
	{ OMAP4_C2C_CLKCTRL, NULL, 0, "div_core_ck" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data omap4_l4_cfg_clkctrl_regs[] __initconst = {
	{ OMAP4_L4_CFG_CLKCTRL, NULL, 0, "l4_div_ck" },
	{ OMAP4_SPINLOCK_CLKCTRL, NULL, 0, "l4_div_ck" },
	{ OMAP4_MAILBOX_CLKCTRL, NULL, 0, "l4_div_ck" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data omap4_l3_instr_clkctrl_regs[] __initconst = {
	{ OMAP4_L3_MAIN_3_CLKCTRL, NULL, CLKF_HW_SUP, "l3_div_ck" },
	{ OMAP4_L3_INSTR_CLKCTRL, NULL, CLKF_HW_SUP, "l3_div_ck" },
	{ OMAP4_OCP_WP_NOC_CLKCTRL, NULL, CLKF_HW_SUP, "l3_div_ck" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data omap4_ivahd_clkctrl_regs[] __initconst = {
	{ OMAP4_IVA_CLKCTRL, NULL, CLKF_HW_SUP | CLKF_NO_IDLEST, "dpll_iva_m5x2_ck" },
	{ OMAP4_SL2IF_CLKCTRL, NULL, CLKF_HW_SUP, "dpll_iva_m5x2_ck" },
	{ 0 },
};

static const char * const omap4_iss_ctrlclk_parents[] __initconst = {
	"func_96m_fclk",
	NULL,
};

static const struct omap_clkctrl_bit_data omap4_iss_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, omap4_iss_ctrlclk_parents, NULL },
	{ 0 },
};

static const char * const omap4_fdif_fck_parents[] __initconst = {
	"dpll_per_m4x2_ck",
	NULL,
};

static const struct omap_clkctrl_div_data omap4_fdif_fck_data __initconst = {
	.max_div = 4,
	.flags = CLK_DIVIDER_POWER_OF_TWO,
};

static const struct omap_clkctrl_bit_data omap4_fdif_bit_data[] __initconst = {
	{ 24, TI_CLK_DIVIDER, omap4_fdif_fck_parents, &omap4_fdif_fck_data },
	{ 0 },
};

static const struct omap_clkctrl_reg_data omap4_iss_clkctrl_regs[] __initconst = {
	{ OMAP4_ISS_CLKCTRL, omap4_iss_bit_data, CLKF_SW_SUP, "ducati_clk_mux_ck" },
	{ OMAP4_FDIF_CLKCTRL, omap4_fdif_bit_data, CLKF_SW_SUP, "iss_cm:clk:0008:24" },
	{ 0 },
};

static const char * const omap4_dss_dss_clk_parents[] __initconst = {
	"dpll_per_m5x2_ck",
	NULL,
};

static const char * const omap4_dss_48mhz_clk_parents[] __initconst = {
	"func_48mc_fclk",
	NULL,
};

static const char * const omap4_dss_sys_clk_parents[] __initconst = {
	"syc_clk_div_ck",
	NULL,
};

static const char * const omap4_dss_tv_clk_parents[] __initconst = {
	"extalt_clkin_ck",
	NULL,
};

static const struct omap_clkctrl_bit_data omap4_dss_core_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, omap4_dss_dss_clk_parents, NULL },
	{ 9, TI_CLK_GATE, omap4_dss_48mhz_clk_parents, NULL },
	{ 10, TI_CLK_GATE, omap4_dss_sys_clk_parents, NULL },
	{ 11, TI_CLK_GATE, omap4_dss_tv_clk_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_reg_data omap4_l3_dss_clkctrl_regs[] __initconst = {
	{ OMAP4_DSS_CORE_CLKCTRL, omap4_dss_core_bit_data, CLKF_SW_SUP, "l3_dss_cm:clk:0000:8" },
	{ 0 },
};

static const char * const omap4_sgx_clk_mux_parents[] __initconst = {
	"dpll_core_m7x2_ck",
	"dpll_per_m7x2_ck",
	NULL,
};

static const struct omap_clkctrl_bit_data omap4_gpu_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, omap4_sgx_clk_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_reg_data omap4_l3_gfx_clkctrl_regs[] __initconst = {
	{ OMAP4_GPU_CLKCTRL, omap4_gpu_bit_data, CLKF_SW_SUP, "l3_gfx_cm:clk:0000:24" },
	{ 0 },
};

static const char * const omap4_hsmmc1_fclk_parents[] __initconst = {
	"func_64m_fclk",
	"func_96m_fclk",
	NULL,
};

static const struct omap_clkctrl_bit_data omap4_mmc1_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, omap4_hsmmc1_fclk_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data omap4_mmc2_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, omap4_hsmmc1_fclk_parents, NULL },
	{ 0 },
};

static const char * const omap4_hsi_fck_parents[] __initconst = {
	"dpll_per_m2x2_ck",
	NULL,
};

static const struct omap_clkctrl_div_data omap4_hsi_fck_data __initconst = {
	.max_div = 4,
	.flags = CLK_DIVIDER_POWER_OF_TWO,
};

static const struct omap_clkctrl_bit_data omap4_hsi_bit_data[] __initconst = {
	{ 24, TI_CLK_DIVIDER, omap4_hsi_fck_parents, &omap4_hsi_fck_data },
	{ 0 },
};

static const char * const omap4_usb_host_hs_utmi_p1_clk_parents[] __initconst = {
	"l3_init_cm:clk:0038:24",
	NULL,
};

static const char * const omap4_usb_host_hs_utmi_p2_clk_parents[] __initconst = {
	"l3_init_cm:clk:0038:25",
	NULL,
};

static const char * const omap4_usb_host_hs_utmi_p3_clk_parents[] __initconst = {
	"init_60m_fclk",
	NULL,
};

static const char * const omap4_usb_host_hs_hsic480m_p1_clk_parents[] __initconst = {
	"dpll_usb_m2_ck",
	NULL,
};

static const char * const omap4_utmi_p1_gfclk_parents[] __initconst = {
	"init_60m_fclk",
	"xclk60mhsp1_ck",
	NULL,
};

static const char * const omap4_utmi_p2_gfclk_parents[] __initconst = {
	"init_60m_fclk",
	"xclk60mhsp2_ck",
	NULL,
};

static const struct omap_clkctrl_bit_data omap4_usb_host_hs_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, omap4_usb_host_hs_utmi_p1_clk_parents, NULL },
	{ 9, TI_CLK_GATE, omap4_usb_host_hs_utmi_p2_clk_parents, NULL },
	{ 10, TI_CLK_GATE, omap4_usb_host_hs_utmi_p3_clk_parents, NULL },
	{ 11, TI_CLK_GATE, omap4_usb_host_hs_utmi_p3_clk_parents, NULL },
	{ 12, TI_CLK_GATE, omap4_usb_host_hs_utmi_p3_clk_parents, NULL },
	{ 13, TI_CLK_GATE, omap4_usb_host_hs_hsic480m_p1_clk_parents, NULL },
	{ 14, TI_CLK_GATE, omap4_usb_host_hs_hsic480m_p1_clk_parents, NULL },
	{ 15, TI_CLK_GATE, omap4_dss_48mhz_clk_parents, NULL },
	{ 24, TI_CLK_MUX, omap4_utmi_p1_gfclk_parents, NULL },
	{ 25, TI_CLK_MUX, omap4_utmi_p2_gfclk_parents, NULL },
	{ 0 },
};

static const char * const omap4_usb_otg_hs_xclk_parents[] __initconst = {
	"l3_init_cm:clk:0040:24",
	NULL,
};

static const char * const omap4_otg_60m_gfclk_parents[] __initconst = {
	"utmi_phy_clkout_ck",
	"xclk60motg_ck",
	NULL,
};

static const struct omap_clkctrl_bit_data omap4_usb_otg_hs_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, omap4_usb_otg_hs_xclk_parents, NULL },
	{ 24, TI_CLK_MUX, omap4_otg_60m_gfclk_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data omap4_usb_tll_hs_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, omap4_usb_host_hs_utmi_p3_clk_parents, NULL },
	{ 9, TI_CLK_GATE, omap4_usb_host_hs_utmi_p3_clk_parents, NULL },
	{ 10, TI_CLK_GATE, omap4_usb_host_hs_utmi_p3_clk_parents, NULL },
	{ 0 },
};

static const char * const omap4_ocp2scp_usb_phy_phy_48m_parents[] __initconst = {
	"func_48m_fclk",
	NULL,
};

static const struct omap_clkctrl_bit_data omap4_ocp2scp_usb_phy_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, omap4_ocp2scp_usb_phy_phy_48m_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_reg_data omap4_l3_init_clkctrl_regs[] __initconst = {
	{ OMAP4_MMC1_CLKCTRL, omap4_mmc1_bit_data, CLKF_SW_SUP, "l3_init_cm:clk:0008:24" },
	{ OMAP4_MMC2_CLKCTRL, omap4_mmc2_bit_data, CLKF_SW_SUP, "l3_init_cm:clk:0010:24" },
	{ OMAP4_HSI_CLKCTRL, omap4_hsi_bit_data, CLKF_HW_SUP, "l3_init_cm:clk:0018:24" },
	{ OMAP4_USB_HOST_HS_CLKCTRL, omap4_usb_host_hs_bit_data, CLKF_SW_SUP, "init_60m_fclk" },
	{ OMAP4_USB_OTG_HS_CLKCTRL, omap4_usb_otg_hs_bit_data, CLKF_HW_SUP, "l3_div_ck" },
	{ OMAP4_USB_TLL_HS_CLKCTRL, omap4_usb_tll_hs_bit_data, CLKF_HW_SUP, "l4_div_ck" },
	{ OMAP4_USB_HOST_FS_CLKCTRL, NULL, CLKF_SW_SUP, "func_48mc_fclk" },
	{ OMAP4_OCP2SCP_USB_PHY_CLKCTRL, omap4_ocp2scp_usb_phy_bit_data, CLKF_HW_SUP, "l3_init_cm:clk:00c0:8" },
	{ 0 },
};

static const char * const omap4_cm2_dm10_mux_parents[] __initconst = {
	"sys_clkin_ck",
	"sys_32k_ck",
	NULL,
};

static const struct omap_clkctrl_bit_data omap4_timer10_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, omap4_cm2_dm10_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data omap4_timer11_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, omap4_cm2_dm10_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data omap4_timer2_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, omap4_cm2_dm10_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data omap4_timer3_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, omap4_cm2_dm10_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data omap4_timer4_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, omap4_cm2_dm10_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data omap4_timer9_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, omap4_cm2_dm10_mux_parents, NULL },
	{ 0 },
};

static const char * const omap4_gpio2_dbclk_parents[] __initconst = {
	"sys_32k_ck",
	NULL,
};

static const struct omap_clkctrl_bit_data omap4_gpio2_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, omap4_gpio2_dbclk_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data omap4_gpio3_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, omap4_gpio2_dbclk_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data omap4_gpio4_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, omap4_gpio2_dbclk_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data omap4_gpio5_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, omap4_gpio2_dbclk_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data omap4_gpio6_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, omap4_gpio2_dbclk_parents, NULL },
	{ 0 },
};

static const char * const omap4_per_mcbsp4_gfclk_parents[] __initconst = {
	"l4_per_cm:clk:00c0:26",
	"pad_clks_ck",
	NULL,
};

static const char * const omap4_mcbsp4_sync_mux_ck_parents[] __initconst = {
	"func_96m_fclk",
	"per_abe_nc_fclk",
	NULL,
};

static const struct omap_clkctrl_bit_data omap4_mcbsp4_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, omap4_per_mcbsp4_gfclk_parents, NULL },
	{ 26, TI_CLK_MUX, omap4_mcbsp4_sync_mux_ck_parents, NULL },
	{ 0 },
};

static const char * const omap4_slimbus2_fclk_0_parents[] __initconst = {
	"func_24mc_fclk",
	NULL,
};

static const char * const omap4_slimbus2_fclk_1_parents[] __initconst = {
	"per_abe_24m_fclk",
	NULL,
};

static const char * const omap4_slimbus2_slimbus_clk_parents[] __initconst = {
	"pad_slimbus_core_clks_ck",
	NULL,
};

static const struct omap_clkctrl_bit_data omap4_slimbus2_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, omap4_slimbus2_fclk_0_parents, NULL },
	{ 9, TI_CLK_GATE, omap4_slimbus2_fclk_1_parents, NULL },
	{ 10, TI_CLK_GATE, omap4_slimbus2_slimbus_clk_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_reg_data omap4_l4_per_clkctrl_regs[] __initconst = {
	{ OMAP4_TIMER10_CLKCTRL, omap4_timer10_bit_data, CLKF_SW_SUP, "l4_per_cm:clk:0008:24" },
	{ OMAP4_TIMER11_CLKCTRL, omap4_timer11_bit_data, CLKF_SW_SUP, "l4_per_cm:clk:0010:24" },
	{ OMAP4_TIMER2_CLKCTRL, omap4_timer2_bit_data, CLKF_SW_SUP, "l4_per_cm:clk:0018:24" },
	{ OMAP4_TIMER3_CLKCTRL, omap4_timer3_bit_data, CLKF_SW_SUP, "l4_per_cm:clk:0020:24" },
	{ OMAP4_TIMER4_CLKCTRL, omap4_timer4_bit_data, CLKF_SW_SUP, "l4_per_cm:clk:0028:24" },
	{ OMAP4_TIMER9_CLKCTRL, omap4_timer9_bit_data, CLKF_SW_SUP, "l4_per_cm:clk:0030:24" },
	{ OMAP4_ELM_CLKCTRL, NULL, 0, "l4_div_ck" },
	{ OMAP4_GPIO2_CLKCTRL, omap4_gpio2_bit_data, CLKF_HW_SUP, "l4_div_ck" },
	{ OMAP4_GPIO3_CLKCTRL, omap4_gpio3_bit_data, CLKF_HW_SUP, "l4_div_ck" },
	{ OMAP4_GPIO4_CLKCTRL, omap4_gpio4_bit_data, CLKF_HW_SUP, "l4_div_ck" },
	{ OMAP4_GPIO5_CLKCTRL, omap4_gpio5_bit_data, CLKF_HW_SUP, "l4_div_ck" },
	{ OMAP4_GPIO6_CLKCTRL, omap4_gpio6_bit_data, CLKF_HW_SUP, "l4_div_ck" },
	{ OMAP4_HDQ1W_CLKCTRL, NULL, CLKF_SW_SUP, "func_12m_fclk" },
	{ OMAP4_I2C1_CLKCTRL, NULL, CLKF_SW_SUP, "func_96m_fclk" },
	{ OMAP4_I2C2_CLKCTRL, NULL, CLKF_SW_SUP, "func_96m_fclk" },
	{ OMAP4_I2C3_CLKCTRL, NULL, CLKF_SW_SUP, "func_96m_fclk" },
	{ OMAP4_I2C4_CLKCTRL, NULL, CLKF_SW_SUP, "func_96m_fclk" },
	{ OMAP4_L4_PER_CLKCTRL, NULL, 0, "l4_div_ck" },
	{ OMAP4_MCBSP4_CLKCTRL, omap4_mcbsp4_bit_data, CLKF_SW_SUP, "l4_per_cm:clk:00c0:24" },
	{ OMAP4_MCSPI1_CLKCTRL, NULL, CLKF_SW_SUP, "func_48m_fclk" },
	{ OMAP4_MCSPI2_CLKCTRL, NULL, CLKF_SW_SUP, "func_48m_fclk" },
	{ OMAP4_MCSPI3_CLKCTRL, NULL, CLKF_SW_SUP, "func_48m_fclk" },
	{ OMAP4_MCSPI4_CLKCTRL, NULL, CLKF_SW_SUP, "func_48m_fclk" },
	{ OMAP4_MMC3_CLKCTRL, NULL, CLKF_SW_SUP, "func_48m_fclk" },
	{ OMAP4_MMC4_CLKCTRL, NULL, CLKF_SW_SUP, "func_48m_fclk" },
	{ OMAP4_SLIMBUS2_CLKCTRL, omap4_slimbus2_bit_data, CLKF_SW_SUP, "l4_per_cm:clk:0118:8" },
	{ OMAP4_UART1_CLKCTRL, NULL, CLKF_SW_SUP, "func_48m_fclk" },
	{ OMAP4_UART2_CLKCTRL, NULL, CLKF_SW_SUP, "func_48m_fclk" },
	{ OMAP4_UART3_CLKCTRL, NULL, CLKF_SW_SUP, "func_48m_fclk" },
	{ OMAP4_UART4_CLKCTRL, NULL, CLKF_SW_SUP, "func_48m_fclk" },
	{ OMAP4_MMC5_CLKCTRL, NULL, CLKF_SW_SUP, "func_48m_fclk" },
	{ 0 },
};

static const struct
omap_clkctrl_reg_data omap4_l4_secure_clkctrl_regs[] __initconst = {
	{ OMAP4_AES1_CLKCTRL, NULL, CLKF_SW_SUP, "l3_div_ck" },
	{ OMAP4_AES2_CLKCTRL, NULL, CLKF_SW_SUP, "l3_div_ck" },
	{ OMAP4_DES3DES_CLKCTRL, NULL, CLKF_SW_SUP, "l4_div_ck" },
	{ OMAP4_PKA_CLKCTRL, NULL, CLKF_SW_SUP, "l4_div_ck" },
	{ OMAP4_RNG_CLKCTRL, NULL, CLKF_HW_SUP | CLKF_SOC_NONSEC, "l4_div_ck" },
	{ OMAP4_SHA2MD5_CLKCTRL, NULL, CLKF_SW_SUP, "l3_div_ck" },
	{ OMAP4_CRYPTODMA_CLKCTRL, NULL, CLKF_HW_SUP | CLKF_SOC_NONSEC, "l3_div_ck" },
	{ 0 },
};

static const struct omap_clkctrl_bit_data omap4_gpio1_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, omap4_gpio2_dbclk_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data omap4_timer1_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, omap4_cm2_dm10_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_reg_data omap4_l4_wkup_clkctrl_regs[] __initconst = {
	{ OMAP4_L4_WKUP_CLKCTRL, NULL, 0, "l4_wkup_clk_mux_ck" },
	{ OMAP4_WD_TIMER2_CLKCTRL, NULL, CLKF_SW_SUP, "sys_32k_ck" },
	{ OMAP4_GPIO1_CLKCTRL, omap4_gpio1_bit_data, CLKF_HW_SUP, "l4_wkup_clk_mux_ck" },
	{ OMAP4_TIMER1_CLKCTRL, omap4_timer1_bit_data, CLKF_SW_SUP, "l4_wkup_cm:clk:0020:24" },
	{ OMAP4_COUNTER_32K_CLKCTRL, NULL, 0, "sys_32k_ck" },
	{ OMAP4_KBD_CLKCTRL, NULL, CLKF_SW_SUP, "sys_32k_ck" },
	{ 0 },
};

static const char * const omap4_pmd_stm_clock_mux_ck_parents[] __initconst = {
	"sys_clkin_ck",
	"dpll_core_m6x2_ck",
	"tie_low_clock_ck",
	NULL,
};

static const char * const omap4_trace_clk_div_div_ck_parents[] __initconst = {
	"emu_sys_cm:clk:0000:22",
	NULL,
};

static const int omap4_trace_clk_div_div_ck_divs[] __initconst = {
	0,
	1,
	2,
	0,
	4,
	-1,
};

static const struct omap_clkctrl_div_data omap4_trace_clk_div_div_ck_data __initconst = {
	.dividers = omap4_trace_clk_div_div_ck_divs,
};

static const char * const omap4_stm_clk_div_ck_parents[] __initconst = {
	"emu_sys_cm:clk:0000:20",
	NULL,
};

static const struct omap_clkctrl_div_data omap4_stm_clk_div_ck_data __initconst = {
	.max_div = 64,
	.flags = CLK_DIVIDER_POWER_OF_TWO,
};

static const struct omap_clkctrl_bit_data omap4_debugss_bit_data[] __initconst = {
	{ 20, TI_CLK_MUX, omap4_pmd_stm_clock_mux_ck_parents, NULL },
	{ 22, TI_CLK_MUX, omap4_pmd_stm_clock_mux_ck_parents, NULL },
	{ 24, TI_CLK_DIVIDER, omap4_trace_clk_div_div_ck_parents, &omap4_trace_clk_div_div_ck_data },
	{ 27, TI_CLK_DIVIDER, omap4_stm_clk_div_ck_parents, &omap4_stm_clk_div_ck_data },
	{ 0 },
};

static const struct omap_clkctrl_reg_data omap4_emu_sys_clkctrl_regs[] __initconst = {
	{ OMAP4_DEBUGSS_CLKCTRL, omap4_debugss_bit_data, 0, "trace_clk_div_ck" },
	{ 0 },
};

const struct omap_clkctrl_data omap4_clkctrl_data[] __initconst = {
	{ 0x4a004320, omap4_mpuss_clkctrl_regs },
	{ 0x4a004420, omap4_tesla_clkctrl_regs },
	{ 0x4a004520, omap4_abe_clkctrl_regs },
	{ 0x4a008620, omap4_l4_ao_clkctrl_regs },
	{ 0x4a008720, omap4_l3_1_clkctrl_regs },
	{ 0x4a008820, omap4_l3_2_clkctrl_regs },
	{ 0x4a008920, omap4_ducati_clkctrl_regs },
	{ 0x4a008a20, omap4_l3_dma_clkctrl_regs },
	{ 0x4a008b20, omap4_l3_emif_clkctrl_regs },
	{ 0x4a008c20, omap4_d2d_clkctrl_regs },
	{ 0x4a008d20, omap4_l4_cfg_clkctrl_regs },
	{ 0x4a008e20, omap4_l3_instr_clkctrl_regs },
	{ 0x4a008f20, omap4_ivahd_clkctrl_regs },
	{ 0x4a009020, omap4_iss_clkctrl_regs },
	{ 0x4a009120, omap4_l3_dss_clkctrl_regs },
	{ 0x4a009220, omap4_l3_gfx_clkctrl_regs },
	{ 0x4a009320, omap4_l3_init_clkctrl_regs },
	{ 0x4a009420, omap4_l4_per_clkctrl_regs },
	{ 0x4a0095a0, omap4_l4_secure_clkctrl_regs },
	{ 0x4a307820, omap4_l4_wkup_clkctrl_regs },
	{ 0x4a307a20, omap4_emu_sys_clkctrl_regs },
	{ 0 },
};

static struct ti_dt_clk omap44xx_clks[] = {
	DT_CLK(NULL, "timer_32k_ck", "sys_32k_ck"),
	/*
	 * XXX: All the clock aliases below are only needed for legacy
	 * hwmod support. Once hwmod is removed, these can be removed
	 * also.
	 */
	DT_CLK(NULL, "aess_fclk", "abe_cm:0008:24"),
	DT_CLK(NULL, "cm2_dm10_mux", "l4_per_cm:0008:24"),
	DT_CLK(NULL, "cm2_dm11_mux", "l4_per_cm:0010:24"),
	DT_CLK(NULL, "cm2_dm2_mux", "l4_per_cm:0018:24"),
	DT_CLK(NULL, "cm2_dm3_mux", "l4_per_cm:0020:24"),
	DT_CLK(NULL, "cm2_dm4_mux", "l4_per_cm:0028:24"),
	DT_CLK(NULL, "cm2_dm9_mux", "l4_per_cm:0030:24"),
	DT_CLK(NULL, "dmic_sync_mux_ck", "abe_cm:0018:26"),
	DT_CLK(NULL, "dmt1_clk_mux", "l4_wkup_cm:0020:24"),
	DT_CLK(NULL, "dss_48mhz_clk", "l3_dss_cm:0000:9"),
	DT_CLK(NULL, "dss_dss_clk", "l3_dss_cm:0000:8"),
	DT_CLK(NULL, "dss_sys_clk", "l3_dss_cm:0000:10"),
	DT_CLK(NULL, "dss_tv_clk", "l3_dss_cm:0000:11"),
	DT_CLK(NULL, "fdif_fck", "iss_cm:0008:24"),
	DT_CLK(NULL, "func_dmic_abe_gfclk", "abe_cm:0018:24"),
	DT_CLK(NULL, "func_mcasp_abe_gfclk", "abe_cm:0020:24"),
	DT_CLK(NULL, "func_mcbsp1_gfclk", "abe_cm:0028:24"),
	DT_CLK(NULL, "func_mcbsp2_gfclk", "abe_cm:0030:24"),
	DT_CLK(NULL, "func_mcbsp3_gfclk", "abe_cm:0038:24"),
	DT_CLK(NULL, "gpio1_dbclk", "l4_wkup_cm:0018:8"),
	DT_CLK(NULL, "gpio2_dbclk", "l4_per_cm:0040:8"),
	DT_CLK(NULL, "gpio3_dbclk", "l4_per_cm:0048:8"),
	DT_CLK(NULL, "gpio4_dbclk", "l4_per_cm:0050:8"),
	DT_CLK(NULL, "gpio5_dbclk", "l4_per_cm:0058:8"),
	DT_CLK(NULL, "gpio6_dbclk", "l4_per_cm:0060:8"),
	DT_CLK(NULL, "hsi_fck", "l3_init_cm:0018:24"),
	DT_CLK(NULL, "hsmmc1_fclk", "l3_init_cm:0008:24"),
	DT_CLK(NULL, "hsmmc2_fclk", "l3_init_cm:0010:24"),
	DT_CLK(NULL, "iss_ctrlclk", "iss_cm:0000:8"),
	DT_CLK(NULL, "mcasp_sync_mux_ck", "abe_cm:0020:26"),
	DT_CLK(NULL, "mcbsp1_sync_mux_ck", "abe_cm:0028:26"),
	DT_CLK(NULL, "mcbsp2_sync_mux_ck", "abe_cm:0030:26"),
	DT_CLK(NULL, "mcbsp3_sync_mux_ck", "abe_cm:0038:26"),
	DT_CLK(NULL, "mcbsp4_sync_mux_ck", "l4_per_cm:00c0:26"),
	DT_CLK(NULL, "ocp2scp_usb_phy_phy_48m", "l3_init_cm:00c0:8"),
	DT_CLK(NULL, "otg_60m_gfclk", "l3_init_cm:0040:24"),
	DT_CLK(NULL, "per_mcbsp4_gfclk", "l4_per_cm:00c0:24"),
	DT_CLK(NULL, "pmd_stm_clock_mux_ck", "emu_sys_cm:0000:20"),
	DT_CLK(NULL, "pmd_trace_clk_mux_ck", "emu_sys_cm:0000:22"),
	DT_CLK(NULL, "sgx_clk_mux", "l3_gfx_cm:0000:24"),
	DT_CLK(NULL, "slimbus1_fclk_0", "abe_cm:0040:8"),
	DT_CLK(NULL, "slimbus1_fclk_1", "abe_cm:0040:9"),
	DT_CLK(NULL, "slimbus1_fclk_2", "abe_cm:0040:10"),
	DT_CLK(NULL, "slimbus1_slimbus_clk", "abe_cm:0040:11"),
	DT_CLK(NULL, "slimbus2_fclk_0", "l4_per_cm:0118:8"),
	DT_CLK(NULL, "slimbus2_fclk_1", "l4_per_cm:0118:9"),
	DT_CLK(NULL, "slimbus2_slimbus_clk", "l4_per_cm:0118:10"),
	DT_CLK(NULL, "stm_clk_div_ck", "emu_sys_cm:0000:27"),
	DT_CLK(NULL, "timer5_sync_mux", "abe_cm:0048:24"),
	DT_CLK(NULL, "timer6_sync_mux", "abe_cm:0050:24"),
	DT_CLK(NULL, "timer7_sync_mux", "abe_cm:0058:24"),
	DT_CLK(NULL, "timer8_sync_mux", "abe_cm:0060:24"),
	DT_CLK(NULL, "trace_clk_div_div_ck", "emu_sys_cm:0000:24"),
	DT_CLK(NULL, "usb_host_hs_func48mclk", "l3_init_cm:0038:15"),
	DT_CLK(NULL, "usb_host_hs_hsic480m_p1_clk", "l3_init_cm:0038:13"),
	DT_CLK(NULL, "usb_host_hs_hsic480m_p2_clk", "l3_init_cm:0038:14"),
	DT_CLK(NULL, "usb_host_hs_hsic60m_p1_clk", "l3_init_cm:0038:11"),
	DT_CLK(NULL, "usb_host_hs_hsic60m_p2_clk", "l3_init_cm:0038:12"),
	DT_CLK(NULL, "usb_host_hs_utmi_p1_clk", "l3_init_cm:0038:8"),
	DT_CLK(NULL, "usb_host_hs_utmi_p2_clk", "l3_init_cm:0038:9"),
	DT_CLK(NULL, "usb_host_hs_utmi_p3_clk", "l3_init_cm:0038:10"),
	DT_CLK(NULL, "usb_otg_hs_xclk", "l3_init_cm:0040:8"),
	DT_CLK(NULL, "usb_tll_hs_usb_ch0_clk", "l3_init_cm:0048:8"),
	DT_CLK(NULL, "usb_tll_hs_usb_ch1_clk", "l3_init_cm:0048:9"),
	DT_CLK(NULL, "usb_tll_hs_usb_ch2_clk", "l3_init_cm:0048:10"),
	DT_CLK(NULL, "utmi_p1_gfclk", "l3_init_cm:0038:24"),
	DT_CLK(NULL, "utmi_p2_gfclk", "l3_init_cm:0038:25"),
	{ .node_name = NULL },
};

int __init omap4xxx_dt_clk_init(void)
{
	int rc;
	struct clk *abe_dpll_ref, *abe_dpll, *sys_32k_ck, *usb_dpll;

	ti_dt_clocks_register(omap44xx_clks);

	omap2_clk_disable_autoidle_all();

	ti_clk_add_aliases();

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
