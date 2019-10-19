// SPDX-License-Identifier: GPL-2.0-only
/*
 * DRA7 Clock init
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
#include <dt-bindings/clock/dra7.h>

#include "clock.h"

#define DRA7_DPLL_GMAC_DEFFREQ				1000000000
#define DRA7_DPLL_USB_DEFFREQ				960000000

static const struct omap_clkctrl_reg_data dra7_mpu_clkctrl_regs[] __initconst = {
	{ DRA7_MPU_MPU_CLKCTRL, NULL, 0, "dpll_mpu_m2_ck" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data dra7_dsp1_clkctrl_regs[] __initconst = {
	{ DRA7_DSP1_MMU0_DSP1_CLKCTRL, NULL, CLKF_HW_SUP, "dpll_dsp_m2_ck" },
	{ 0 },
};

static const char * const dra7_ipu1_gfclk_mux_parents[] __initconst = {
	"dpll_abe_m2x2_ck",
	"dpll_core_h22x2_ck",
	NULL,
};

static const struct omap_clkctrl_bit_data dra7_mmu_ipu1_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, dra7_ipu1_gfclk_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_reg_data dra7_ipu1_clkctrl_regs[] __initconst = {
	{ DRA7_IPU1_MMU_IPU1_CLKCTRL, dra7_mmu_ipu1_bit_data, CLKF_HW_SUP, "ipu1-clkctrl:0000:24" },
	{ 0 },
};

static const char * const dra7_mcasp1_aux_gfclk_mux_parents[] __initconst = {
	"per_abe_x1_gfclk2_div",
	"video1_clk2_div",
	"video2_clk2_div",
	"hdmi_clk2_div",
	NULL,
};

static const char * const dra7_mcasp1_ahclkx_mux_parents[] __initconst = {
	"abe_24m_fclk",
	"abe_sys_clk_div",
	"func_24m_clk",
	"atl_clkin3_ck",
	"atl_clkin2_ck",
	"atl_clkin1_ck",
	"atl_clkin0_ck",
	"sys_clkin2",
	"ref_clkin0_ck",
	"ref_clkin1_ck",
	"ref_clkin2_ck",
	"ref_clkin3_ck",
	"mlb_clk",
	"mlbp_clk",
	NULL,
};

static const struct omap_clkctrl_bit_data dra7_mcasp1_bit_data[] __initconst = {
	{ 22, TI_CLK_MUX, dra7_mcasp1_aux_gfclk_mux_parents, NULL },
	{ 24, TI_CLK_MUX, dra7_mcasp1_ahclkx_mux_parents, NULL },
	{ 28, TI_CLK_MUX, dra7_mcasp1_ahclkx_mux_parents, NULL },
	{ 0 },
};

static const char * const dra7_timer5_gfclk_mux_parents[] __initconst = {
	"timer_sys_clk_div",
	"sys_32k_ck",
	"sys_clkin2",
	"ref_clkin0_ck",
	"ref_clkin1_ck",
	"ref_clkin2_ck",
	"ref_clkin3_ck",
	"abe_giclk_div",
	"video1_div_clk",
	"video2_div_clk",
	"hdmi_div_clk",
	"clkoutmux0_clk_mux",
	NULL,
};

static const struct omap_clkctrl_bit_data dra7_timer5_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, dra7_timer5_gfclk_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_timer6_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, dra7_timer5_gfclk_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_timer7_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, dra7_timer5_gfclk_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_timer8_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, dra7_timer5_gfclk_mux_parents, NULL },
	{ 0 },
};

static const char * const dra7_uart6_gfclk_mux_parents[] __initconst = {
	"func_48m_fclk",
	"dpll_per_m2x2_ck",
	NULL,
};

static const struct omap_clkctrl_bit_data dra7_uart6_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, dra7_uart6_gfclk_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_reg_data dra7_ipu_clkctrl_regs[] __initconst = {
	{ DRA7_IPU_MCASP1_CLKCTRL, dra7_mcasp1_bit_data, CLKF_SW_SUP, "ipu-clkctrl:0000:22" },
	{ DRA7_IPU_TIMER5_CLKCTRL, dra7_timer5_bit_data, CLKF_SW_SUP, "ipu-clkctrl:0008:24" },
	{ DRA7_IPU_TIMER6_CLKCTRL, dra7_timer6_bit_data, CLKF_SW_SUP, "ipu-clkctrl:0010:24" },
	{ DRA7_IPU_TIMER7_CLKCTRL, dra7_timer7_bit_data, CLKF_SW_SUP, "ipu-clkctrl:0018:24" },
	{ DRA7_IPU_TIMER8_CLKCTRL, dra7_timer8_bit_data, CLKF_SW_SUP, "ipu-clkctrl:0020:24" },
	{ DRA7_IPU_I2C5_CLKCTRL, NULL, CLKF_SW_SUP, "func_96m_fclk" },
	{ DRA7_IPU_UART6_CLKCTRL, dra7_uart6_bit_data, CLKF_SW_SUP, "ipu-clkctrl:0030:24" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data dra7_dsp2_clkctrl_regs[] __initconst = {
	{ DRA7_DSP2_MMU0_DSP2_CLKCTRL, NULL, CLKF_HW_SUP, "dpll_dsp_m2_ck" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data dra7_rtc_clkctrl_regs[] __initconst = {
	{ DRA7_RTC_RTCSS_CLKCTRL, NULL, CLKF_SW_SUP, "sys_32k_ck" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data dra7_coreaon_clkctrl_regs[] __initconst = {
	{ DRA7_COREAON_SMARTREFLEX_MPU_CLKCTRL, NULL, CLKF_SW_SUP, "wkupaon_iclk_mux" },
	{ DRA7_COREAON_SMARTREFLEX_CORE_CLKCTRL, NULL, CLKF_SW_SUP, "wkupaon_iclk_mux" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data dra7_l3main1_clkctrl_regs[] __initconst = {
	{ DRA7_L3MAIN1_L3_MAIN_1_CLKCTRL, NULL, 0, "l3_iclk_div" },
	{ DRA7_L3MAIN1_GPMC_CLKCTRL, NULL, CLKF_HW_SUP, "l3_iclk_div" },
	{ DRA7_L3MAIN1_TPCC_CLKCTRL, NULL, 0, "l3_iclk_div" },
	{ DRA7_L3MAIN1_TPTC0_CLKCTRL, NULL, CLKF_HW_SUP, "l3_iclk_div" },
	{ DRA7_L3MAIN1_TPTC1_CLKCTRL, NULL, CLKF_HW_SUP, "l3_iclk_div" },
	{ DRA7_L3MAIN1_VCP1_CLKCTRL, NULL, 0, "l3_iclk_div" },
	{ DRA7_L3MAIN1_VCP2_CLKCTRL, NULL, 0, "l3_iclk_div" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data dra7_ipu2_clkctrl_regs[] __initconst = {
	{ DRA7_IPU2_MMU_IPU2_CLKCTRL, NULL, CLKF_HW_SUP, "dpll_core_h22x2_ck" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data dra7_dma_clkctrl_regs[] __initconst = {
	{ DRA7_DMA_DMA_SYSTEM_CLKCTRL, NULL, 0, "l3_iclk_div" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data dra7_emif_clkctrl_regs[] __initconst = {
	{ DRA7_EMIF_DMM_CLKCTRL, NULL, 0, "l3_iclk_div" },
	{ 0 },
};

static const char * const dra7_atl_dpll_clk_mux_parents[] __initconst = {
	"sys_32k_ck",
	"video1_clkin_ck",
	"video2_clkin_ck",
	"hdmi_clkin_ck",
	NULL,
};

static const char * const dra7_atl_gfclk_mux_parents[] __initconst = {
	"l3_iclk_div",
	"dpll_abe_m2_ck",
	"atl-clkctrl:0000:24",
	NULL,
};

static const struct omap_clkctrl_bit_data dra7_atl_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, dra7_atl_dpll_clk_mux_parents, NULL },
	{ 26, TI_CLK_MUX, dra7_atl_gfclk_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_reg_data dra7_atl_clkctrl_regs[] __initconst = {
	{ DRA7_ATL_ATL_CLKCTRL, dra7_atl_bit_data, CLKF_SW_SUP, "atl-clkctrl:0000:26" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data dra7_l4cfg_clkctrl_regs[] __initconst = {
	{ DRA7_L4CFG_L4_CFG_CLKCTRL, NULL, 0, "l3_iclk_div" },
	{ DRA7_L4CFG_SPINLOCK_CLKCTRL, NULL, 0, "l3_iclk_div" },
	{ DRA7_L4CFG_MAILBOX1_CLKCTRL, NULL, 0, "l3_iclk_div" },
	{ DRA7_L4CFG_MAILBOX2_CLKCTRL, NULL, 0, "l3_iclk_div" },
	{ DRA7_L4CFG_MAILBOX3_CLKCTRL, NULL, 0, "l3_iclk_div" },
	{ DRA7_L4CFG_MAILBOX4_CLKCTRL, NULL, 0, "l3_iclk_div" },
	{ DRA7_L4CFG_MAILBOX5_CLKCTRL, NULL, 0, "l3_iclk_div" },
	{ DRA7_L4CFG_MAILBOX6_CLKCTRL, NULL, 0, "l3_iclk_div" },
	{ DRA7_L4CFG_MAILBOX7_CLKCTRL, NULL, 0, "l3_iclk_div" },
	{ DRA7_L4CFG_MAILBOX8_CLKCTRL, NULL, 0, "l3_iclk_div" },
	{ DRA7_L4CFG_MAILBOX9_CLKCTRL, NULL, 0, "l3_iclk_div" },
	{ DRA7_L4CFG_MAILBOX10_CLKCTRL, NULL, 0, "l3_iclk_div" },
	{ DRA7_L4CFG_MAILBOX11_CLKCTRL, NULL, 0, "l3_iclk_div" },
	{ DRA7_L4CFG_MAILBOX12_CLKCTRL, NULL, 0, "l3_iclk_div" },
	{ DRA7_L4CFG_MAILBOX13_CLKCTRL, NULL, 0, "l3_iclk_div" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data dra7_l3instr_clkctrl_regs[] __initconst = {
	{ DRA7_L3INSTR_L3_MAIN_2_CLKCTRL, NULL, CLKF_HW_SUP, "l3_iclk_div" },
	{ DRA7_L3INSTR_L3_INSTR_CLKCTRL, NULL, CLKF_HW_SUP, "l3_iclk_div" },
	{ 0 },
};

static const char * const dra7_dss_dss_clk_parents[] __initconst = {
	"dpll_per_h12x2_ck",
	NULL,
};

static const char * const dra7_dss_48mhz_clk_parents[] __initconst = {
	"func_48m_fclk",
	NULL,
};

static const char * const dra7_dss_hdmi_clk_parents[] __initconst = {
	"hdmi_dpll_clk_mux",
	NULL,
};

static const char * const dra7_dss_32khz_clk_parents[] __initconst = {
	"sys_32k_ck",
	NULL,
};

static const char * const dra7_dss_video1_clk_parents[] __initconst = {
	"video1_dpll_clk_mux",
	NULL,
};

static const char * const dra7_dss_video2_clk_parents[] __initconst = {
	"video2_dpll_clk_mux",
	NULL,
};

static const struct omap_clkctrl_bit_data dra7_dss_core_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, dra7_dss_dss_clk_parents, NULL },
	{ 9, TI_CLK_GATE, dra7_dss_48mhz_clk_parents, NULL },
	{ 10, TI_CLK_GATE, dra7_dss_hdmi_clk_parents, NULL },
	{ 11, TI_CLK_GATE, dra7_dss_32khz_clk_parents, NULL },
	{ 12, TI_CLK_GATE, dra7_dss_video1_clk_parents, NULL },
	{ 13, TI_CLK_GATE, dra7_dss_video2_clk_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_reg_data dra7_dss_clkctrl_regs[] __initconst = {
	{ DRA7_DSS_DSS_CORE_CLKCTRL, dra7_dss_core_bit_data, CLKF_SW_SUP, "dss-clkctrl:0000:8" },
	{ DRA7_DSS_BB2D_CLKCTRL, NULL, CLKF_SW_SUP, "dpll_core_h24x2_ck" },
	{ 0 },
};

static const char * const dra7_mmc1_fclk_mux_parents[] __initconst = {
	"func_128m_clk",
	"dpll_per_m2x2_ck",
	NULL,
};

static const char * const dra7_mmc1_fclk_div_parents[] __initconst = {
	"l3init-clkctrl:0008:24",
	NULL,
};

static const struct omap_clkctrl_div_data dra7_mmc1_fclk_div_data __initconst = {
	.max_div = 4,
	.flags = CLK_DIVIDER_POWER_OF_TWO,
};

static const struct omap_clkctrl_bit_data dra7_mmc1_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, dra7_dss_32khz_clk_parents, NULL },
	{ 24, TI_CLK_MUX, dra7_mmc1_fclk_mux_parents, NULL },
	{ 25, TI_CLK_DIVIDER, dra7_mmc1_fclk_div_parents, &dra7_mmc1_fclk_div_data },
	{ 0 },
};

static const char * const dra7_mmc2_fclk_div_parents[] __initconst = {
	"l3init-clkctrl:0010:24",
	NULL,
};

static const struct omap_clkctrl_div_data dra7_mmc2_fclk_div_data __initconst = {
	.max_div = 4,
	.flags = CLK_DIVIDER_POWER_OF_TWO,
};

static const struct omap_clkctrl_bit_data dra7_mmc2_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, dra7_dss_32khz_clk_parents, NULL },
	{ 24, TI_CLK_MUX, dra7_mmc1_fclk_mux_parents, NULL },
	{ 25, TI_CLK_DIVIDER, dra7_mmc2_fclk_div_parents, &dra7_mmc2_fclk_div_data },
	{ 0 },
};

static const char * const dra7_usb_otg_ss2_refclk960m_parents[] __initconst = {
	"l3init_960m_gfclk",
	NULL,
};

static const struct omap_clkctrl_bit_data dra7_usb_otg_ss2_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, dra7_usb_otg_ss2_refclk960m_parents, NULL },
	{ 0 },
};

static const char * const dra7_sata_ref_clk_parents[] __initconst = {
	"sys_clkin1",
	NULL,
};

static const struct omap_clkctrl_bit_data dra7_sata_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, dra7_sata_ref_clk_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_usb_otg_ss1_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, dra7_usb_otg_ss2_refclk960m_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_reg_data dra7_l3init_clkctrl_regs[] __initconst = {
	{ DRA7_L3INIT_MMC1_CLKCTRL, dra7_mmc1_bit_data, CLKF_SW_SUP, "l3init-clkctrl:0008:25" },
	{ DRA7_L3INIT_MMC2_CLKCTRL, dra7_mmc2_bit_data, CLKF_SW_SUP, "l3init-clkctrl:0010:25" },
	{ DRA7_L3INIT_USB_OTG_SS2_CLKCTRL, dra7_usb_otg_ss2_bit_data, CLKF_HW_SUP, "dpll_core_h13x2_ck" },
	{ DRA7_L3INIT_USB_OTG_SS3_CLKCTRL, NULL, CLKF_HW_SUP, "dpll_core_h13x2_ck" },
	{ DRA7_L3INIT_USB_OTG_SS4_CLKCTRL, NULL, CLKF_HW_SUP | CLKF_SOC_DRA74 | CLKF_SOC_DRA76, "dpll_core_h13x2_ck" },
	{ DRA7_L3INIT_SATA_CLKCTRL, dra7_sata_bit_data, CLKF_SW_SUP, "func_48m_fclk" },
	{ DRA7_L3INIT_OCP2SCP1_CLKCTRL, NULL, CLKF_HW_SUP, "l4_root_clk_div" },
	{ DRA7_L3INIT_OCP2SCP3_CLKCTRL, NULL, CLKF_HW_SUP, "l4_root_clk_div" },
	{ DRA7_L3INIT_USB_OTG_SS1_CLKCTRL, dra7_usb_otg_ss1_bit_data, CLKF_HW_SUP, "dpll_core_h13x2_ck" },
	{ 0 },
};

static const char * const dra7_optfclk_pciephy1_clk_parents[] __initconst = {
	"apll_pcie_ck",
	NULL,
};

static const char * const dra7_optfclk_pciephy1_div_clk_parents[] __initconst = {
	"optfclk_pciephy_div",
	NULL,
};

static const struct omap_clkctrl_bit_data dra7_pcie1_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, dra7_dss_32khz_clk_parents, NULL },
	{ 9, TI_CLK_GATE, dra7_optfclk_pciephy1_clk_parents, NULL },
	{ 10, TI_CLK_GATE, dra7_optfclk_pciephy1_div_clk_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_pcie2_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, dra7_dss_32khz_clk_parents, NULL },
	{ 9, TI_CLK_GATE, dra7_optfclk_pciephy1_clk_parents, NULL },
	{ 10, TI_CLK_GATE, dra7_optfclk_pciephy1_div_clk_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_reg_data dra7_pcie_clkctrl_regs[] __initconst = {
	{ DRA7_PCIE_PCIE1_CLKCTRL, dra7_pcie1_bit_data, CLKF_SW_SUP, "l4_root_clk_div" },
	{ DRA7_PCIE_PCIE2_CLKCTRL, dra7_pcie2_bit_data, CLKF_SW_SUP, "l4_root_clk_div" },
	{ 0 },
};

static const char * const dra7_rmii_50mhz_clk_mux_parents[] __initconst = {
	"dpll_gmac_h11x2_ck",
	"rmii_clk_ck",
	NULL,
};

static const char * const dra7_gmac_rft_clk_mux_parents[] __initconst = {
	"video1_clkin_ck",
	"video2_clkin_ck",
	"dpll_abe_m2_ck",
	"hdmi_clkin_ck",
	"l3_iclk_div",
	NULL,
};

static const struct omap_clkctrl_bit_data dra7_gmac_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, dra7_rmii_50mhz_clk_mux_parents, NULL },
	{ 25, TI_CLK_MUX, dra7_gmac_rft_clk_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_reg_data dra7_gmac_clkctrl_regs[] __initconst = {
	{ DRA7_GMAC_GMAC_CLKCTRL, dra7_gmac_bit_data, CLKF_SW_SUP, "dpll_gmac_ck" },
	{ 0 },
};

static const char * const dra7_timer10_gfclk_mux_parents[] __initconst = {
	"timer_sys_clk_div",
	"sys_32k_ck",
	"sys_clkin2",
	"ref_clkin0_ck",
	"ref_clkin1_ck",
	"ref_clkin2_ck",
	"ref_clkin3_ck",
	"abe_giclk_div",
	"video1_div_clk",
	"video2_div_clk",
	"hdmi_div_clk",
	NULL,
};

static const struct omap_clkctrl_bit_data dra7_timer10_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, dra7_timer10_gfclk_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_timer11_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, dra7_timer10_gfclk_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_timer2_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, dra7_timer10_gfclk_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_timer3_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, dra7_timer10_gfclk_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_timer4_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, dra7_timer10_gfclk_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_timer9_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, dra7_timer10_gfclk_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_gpio2_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, dra7_dss_32khz_clk_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_gpio3_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, dra7_dss_32khz_clk_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_gpio4_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, dra7_dss_32khz_clk_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_gpio5_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, dra7_dss_32khz_clk_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_gpio6_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, dra7_dss_32khz_clk_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_gpio7_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, dra7_dss_32khz_clk_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_gpio8_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, dra7_dss_32khz_clk_parents, NULL },
	{ 0 },
};

static const char * const dra7_mmc3_gfclk_div_parents[] __initconst = {
	"l4per-clkctrl:00f8:24",
	NULL,
};

static const struct omap_clkctrl_div_data dra7_mmc3_gfclk_div_data __initconst = {
	.max_div = 4,
	.flags = CLK_DIVIDER_POWER_OF_TWO,
};

static const struct omap_clkctrl_bit_data dra7_mmc3_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, dra7_dss_32khz_clk_parents, NULL },
	{ 24, TI_CLK_MUX, dra7_uart6_gfclk_mux_parents, NULL },
	{ 25, TI_CLK_DIVIDER, dra7_mmc3_gfclk_div_parents, &dra7_mmc3_gfclk_div_data },
	{ 0 },
};

static const char * const dra7_mmc4_gfclk_div_parents[] __initconst = {
	"l4per-clkctrl:0100:24",
	NULL,
};

static const struct omap_clkctrl_div_data dra7_mmc4_gfclk_div_data __initconst = {
	.max_div = 4,
	.flags = CLK_DIVIDER_POWER_OF_TWO,
};

static const struct omap_clkctrl_bit_data dra7_mmc4_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, dra7_dss_32khz_clk_parents, NULL },
	{ 24, TI_CLK_MUX, dra7_uart6_gfclk_mux_parents, NULL },
	{ 25, TI_CLK_DIVIDER, dra7_mmc4_gfclk_div_parents, &dra7_mmc4_gfclk_div_data },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_uart1_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, dra7_uart6_gfclk_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_uart2_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, dra7_uart6_gfclk_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_uart3_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, dra7_uart6_gfclk_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_uart4_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, dra7_uart6_gfclk_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_uart5_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, dra7_uart6_gfclk_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_reg_data dra7_l4per_clkctrl_regs[] __initconst = {
	{ DRA7_L4PER_TIMER10_CLKCTRL, dra7_timer10_bit_data, CLKF_SW_SUP, "l4per-clkctrl:0000:24" },
	{ DRA7_L4PER_TIMER11_CLKCTRL, dra7_timer11_bit_data, CLKF_SW_SUP, "l4per-clkctrl:0008:24" },
	{ DRA7_L4PER_TIMER2_CLKCTRL, dra7_timer2_bit_data, CLKF_SW_SUP, "l4per-clkctrl:0010:24" },
	{ DRA7_L4PER_TIMER3_CLKCTRL, dra7_timer3_bit_data, CLKF_SW_SUP, "l4per-clkctrl:0018:24" },
	{ DRA7_L4PER_TIMER4_CLKCTRL, dra7_timer4_bit_data, CLKF_SW_SUP, "l4per-clkctrl:0020:24" },
	{ DRA7_L4PER_TIMER9_CLKCTRL, dra7_timer9_bit_data, CLKF_SW_SUP, "l4per-clkctrl:0028:24" },
	{ DRA7_L4PER_ELM_CLKCTRL, NULL, 0, "l3_iclk_div" },
	{ DRA7_L4PER_GPIO2_CLKCTRL, dra7_gpio2_bit_data, CLKF_HW_SUP, "l3_iclk_div" },
	{ DRA7_L4PER_GPIO3_CLKCTRL, dra7_gpio3_bit_data, CLKF_HW_SUP, "l3_iclk_div" },
	{ DRA7_L4PER_GPIO4_CLKCTRL, dra7_gpio4_bit_data, CLKF_HW_SUP, "l3_iclk_div" },
	{ DRA7_L4PER_GPIO5_CLKCTRL, dra7_gpio5_bit_data, CLKF_HW_SUP, "l3_iclk_div" },
	{ DRA7_L4PER_GPIO6_CLKCTRL, dra7_gpio6_bit_data, CLKF_HW_SUP, "l3_iclk_div" },
	{ DRA7_L4PER_HDQ1W_CLKCTRL, NULL, CLKF_SW_SUP, "func_12m_fclk" },
	{ DRA7_L4PER_I2C1_CLKCTRL, NULL, CLKF_SW_SUP, "func_96m_fclk" },
	{ DRA7_L4PER_I2C2_CLKCTRL, NULL, CLKF_SW_SUP, "func_96m_fclk" },
	{ DRA7_L4PER_I2C3_CLKCTRL, NULL, CLKF_SW_SUP, "func_96m_fclk" },
	{ DRA7_L4PER_I2C4_CLKCTRL, NULL, CLKF_SW_SUP, "func_96m_fclk" },
	{ DRA7_L4PER_L4_PER1_CLKCTRL, NULL, 0, "l3_iclk_div" },
	{ DRA7_L4PER_MCSPI1_CLKCTRL, NULL, CLKF_SW_SUP, "func_48m_fclk" },
	{ DRA7_L4PER_MCSPI2_CLKCTRL, NULL, CLKF_SW_SUP, "func_48m_fclk" },
	{ DRA7_L4PER_MCSPI3_CLKCTRL, NULL, CLKF_SW_SUP, "func_48m_fclk" },
	{ DRA7_L4PER_MCSPI4_CLKCTRL, NULL, CLKF_SW_SUP, "func_48m_fclk" },
	{ DRA7_L4PER_GPIO7_CLKCTRL, dra7_gpio7_bit_data, CLKF_HW_SUP, "l3_iclk_div" },
	{ DRA7_L4PER_GPIO8_CLKCTRL, dra7_gpio8_bit_data, CLKF_HW_SUP, "l3_iclk_div" },
	{ DRA7_L4PER_MMC3_CLKCTRL, dra7_mmc3_bit_data, CLKF_SW_SUP, "l4per-clkctrl:00f8:25" },
	{ DRA7_L4PER_MMC4_CLKCTRL, dra7_mmc4_bit_data, CLKF_SW_SUP, "l4per-clkctrl:0100:25" },
	{ DRA7_L4PER_UART1_CLKCTRL, dra7_uart1_bit_data, CLKF_SW_SUP, "l4per-clkctrl:0118:24" },
	{ DRA7_L4PER_UART2_CLKCTRL, dra7_uart2_bit_data, CLKF_SW_SUP, "l4per-clkctrl:0120:24" },
	{ DRA7_L4PER_UART3_CLKCTRL, dra7_uart3_bit_data, CLKF_SW_SUP, "l4per-clkctrl:0128:24" },
	{ DRA7_L4PER_UART4_CLKCTRL, dra7_uart4_bit_data, CLKF_SW_SUP, "l4per-clkctrl:0130:24" },
	{ DRA7_L4PER_UART5_CLKCTRL, dra7_uart5_bit_data, CLKF_SW_SUP, "l4per-clkctrl:0148:24" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data dra7_l4sec_clkctrl_regs[] __initconst = {
	{ DRA7_L4SEC_AES1_CLKCTRL, NULL, CLKF_HW_SUP, "l3_iclk_div" },
	{ DRA7_L4SEC_AES2_CLKCTRL, NULL, CLKF_HW_SUP, "l3_iclk_div" },
	{ DRA7_L4SEC_DES_CLKCTRL, NULL, CLKF_HW_SUP, "l3_iclk_div" },
	{ DRA7_L4SEC_RNG_CLKCTRL, NULL, CLKF_HW_SUP | CLKF_SOC_NONSEC, "" },
	{ DRA7_L4SEC_SHAM_CLKCTRL, NULL, CLKF_HW_SUP, "l3_iclk_div" },
	{ 0 },
};

static const char * const dra7_qspi_gfclk_mux_parents[] __initconst = {
	"func_128m_clk",
	"dpll_per_h13x2_ck",
	NULL,
};

static const char * const dra7_qspi_gfclk_div_parents[] __initconst = {
	"l4per2-clkctrl:012c:24",
	NULL,
};

static const struct omap_clkctrl_div_data dra7_qspi_gfclk_div_data __initconst = {
	.max_div = 4,
	.flags = CLK_DIVIDER_POWER_OF_TWO,
};

static const struct omap_clkctrl_bit_data dra7_qspi_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, dra7_qspi_gfclk_mux_parents, NULL },
	{ 25, TI_CLK_DIVIDER, dra7_qspi_gfclk_div_parents, &dra7_qspi_gfclk_div_data },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_mcasp2_bit_data[] __initconst = {
	{ 22, TI_CLK_MUX, dra7_mcasp1_aux_gfclk_mux_parents, NULL },
	{ 24, TI_CLK_MUX, dra7_mcasp1_ahclkx_mux_parents, NULL },
	{ 28, TI_CLK_MUX, dra7_mcasp1_ahclkx_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_mcasp3_bit_data[] __initconst = {
	{ 22, TI_CLK_MUX, dra7_mcasp1_aux_gfclk_mux_parents, NULL },
	{ 24, TI_CLK_MUX, dra7_mcasp1_ahclkx_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_mcasp5_bit_data[] __initconst = {
	{ 22, TI_CLK_MUX, dra7_mcasp1_aux_gfclk_mux_parents, NULL },
	{ 24, TI_CLK_MUX, dra7_mcasp1_ahclkx_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_mcasp8_bit_data[] __initconst = {
	{ 22, TI_CLK_MUX, dra7_mcasp1_aux_gfclk_mux_parents, NULL },
	{ 24, TI_CLK_MUX, dra7_mcasp1_ahclkx_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_mcasp4_bit_data[] __initconst = {
	{ 22, TI_CLK_MUX, dra7_mcasp1_aux_gfclk_mux_parents, NULL },
	{ 24, TI_CLK_MUX, dra7_mcasp1_ahclkx_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_uart7_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, dra7_uart6_gfclk_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_uart8_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, dra7_uart6_gfclk_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_uart9_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, dra7_uart6_gfclk_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_mcasp6_bit_data[] __initconst = {
	{ 22, TI_CLK_MUX, dra7_mcasp1_aux_gfclk_mux_parents, NULL },
	{ 24, TI_CLK_MUX, dra7_mcasp1_ahclkx_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_mcasp7_bit_data[] __initconst = {
	{ 22, TI_CLK_MUX, dra7_mcasp1_aux_gfclk_mux_parents, NULL },
	{ 24, TI_CLK_MUX, dra7_mcasp1_ahclkx_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_reg_data dra7_l4per2_clkctrl_regs[] __initconst = {
	{ DRA7_L4PER2_L4_PER2_CLKCTRL, NULL, 0, "l3_iclk_div" },
	{ DRA7_L4PER2_PRUSS1_CLKCTRL, NULL, CLKF_SW_SUP, "" },
	{ DRA7_L4PER2_PRUSS2_CLKCTRL, NULL, CLKF_SW_SUP, "" },
	{ DRA7_L4PER2_EPWMSS1_CLKCTRL, NULL, CLKF_SW_SUP, "l4_root_clk_div" },
	{ DRA7_L4PER2_EPWMSS2_CLKCTRL, NULL, CLKF_SW_SUP, "l4_root_clk_div" },
	{ DRA7_L4PER2_EPWMSS0_CLKCTRL, NULL, CLKF_SW_SUP, "l4_root_clk_div" },
	{ DRA7_L4PER2_QSPI_CLKCTRL, dra7_qspi_bit_data, CLKF_SW_SUP, "l4per2-clkctrl:012c:25" },
	{ DRA7_L4PER2_MCASP2_CLKCTRL, dra7_mcasp2_bit_data, CLKF_SW_SUP, "l4per2-clkctrl:0154:22" },
	{ DRA7_L4PER2_MCASP3_CLKCTRL, dra7_mcasp3_bit_data, CLKF_SW_SUP, "l4per2-clkctrl:015c:22" },
	{ DRA7_L4PER2_MCASP5_CLKCTRL, dra7_mcasp5_bit_data, CLKF_SW_SUP, "l4per2-clkctrl:016c:22" },
	{ DRA7_L4PER2_MCASP8_CLKCTRL, dra7_mcasp8_bit_data, CLKF_SW_SUP, "l4per2-clkctrl:0184:22" },
	{ DRA7_L4PER2_MCASP4_CLKCTRL, dra7_mcasp4_bit_data, CLKF_SW_SUP, "l4per2-clkctrl:018c:22" },
	{ DRA7_L4PER2_UART7_CLKCTRL, dra7_uart7_bit_data, CLKF_SW_SUP, "l4per2-clkctrl:01c4:24" },
	{ DRA7_L4PER2_UART8_CLKCTRL, dra7_uart8_bit_data, CLKF_SW_SUP, "l4per2-clkctrl:01d4:24" },
	{ DRA7_L4PER2_UART9_CLKCTRL, dra7_uart9_bit_data, CLKF_SW_SUP, "l4per2-clkctrl:01dc:24" },
	{ DRA7_L4PER2_DCAN2_CLKCTRL, NULL, CLKF_SW_SUP, "sys_clkin1" },
	{ DRA7_L4PER2_MCASP6_CLKCTRL, dra7_mcasp6_bit_data, CLKF_SW_SUP, "l4per2-clkctrl:01f8:22" },
	{ DRA7_L4PER2_MCASP7_CLKCTRL, dra7_mcasp7_bit_data, CLKF_SW_SUP, "l4per2-clkctrl:01fc:22" },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_timer13_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, dra7_timer10_gfclk_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_timer14_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, dra7_timer10_gfclk_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_timer15_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, dra7_timer10_gfclk_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_timer16_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, dra7_timer10_gfclk_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_reg_data dra7_l4per3_clkctrl_regs[] __initconst = {
	{ DRA7_L4PER3_L4_PER3_CLKCTRL, NULL, 0, "l3_iclk_div" },
	{ DRA7_L4PER3_TIMER13_CLKCTRL, dra7_timer13_bit_data, CLKF_SW_SUP, "l4per3-clkctrl:00b4:24" },
	{ DRA7_L4PER3_TIMER14_CLKCTRL, dra7_timer14_bit_data, CLKF_SW_SUP, "l4per3-clkctrl:00bc:24" },
	{ DRA7_L4PER3_TIMER15_CLKCTRL, dra7_timer15_bit_data, CLKF_SW_SUP, "l4per3-clkctrl:00c4:24" },
	{ DRA7_L4PER3_TIMER16_CLKCTRL, dra7_timer16_bit_data, CLKF_SW_SUP, "l4per3-clkctrl:011c:24" },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_gpio1_bit_data[] __initconst = {
	{ 8, TI_CLK_GATE, dra7_dss_32khz_clk_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_timer1_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, dra7_timer10_gfclk_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_bit_data dra7_uart10_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, dra7_uart6_gfclk_mux_parents, NULL },
	{ 0 },
};

static const char * const dra7_dcan1_sys_clk_mux_parents[] __initconst = {
	"sys_clkin1",
	"sys_clkin2",
	NULL,
};

static const struct omap_clkctrl_bit_data dra7_dcan1_bit_data[] __initconst = {
	{ 24, TI_CLK_MUX, dra7_dcan1_sys_clk_mux_parents, NULL },
	{ 0 },
};

static const struct omap_clkctrl_reg_data dra7_wkupaon_clkctrl_regs[] __initconst = {
	{ DRA7_WKUPAON_L4_WKUP_CLKCTRL, NULL, 0, "wkupaon_iclk_mux" },
	{ DRA7_WKUPAON_WD_TIMER2_CLKCTRL, NULL, CLKF_SW_SUP, "sys_32k_ck" },
	{ DRA7_WKUPAON_GPIO1_CLKCTRL, dra7_gpio1_bit_data, CLKF_HW_SUP, "wkupaon_iclk_mux" },
	{ DRA7_WKUPAON_TIMER1_CLKCTRL, dra7_timer1_bit_data, CLKF_SW_SUP, "wkupaon-clkctrl:0020:24" },
	{ DRA7_WKUPAON_TIMER12_CLKCTRL, NULL, CLKF_SOC_NONSEC, "secure_32k_clk_src_ck" },
	{ DRA7_WKUPAON_COUNTER_32K_CLKCTRL, NULL, 0, "wkupaon_iclk_mux" },
	{ DRA7_WKUPAON_UART10_CLKCTRL, dra7_uart10_bit_data, CLKF_SW_SUP, "wkupaon-clkctrl:0060:24" },
	{ DRA7_WKUPAON_DCAN1_CLKCTRL, dra7_dcan1_bit_data, CLKF_SW_SUP, "wkupaon-clkctrl:0068:24" },
	{ DRA7_WKUPAON_ADC_CLKCTRL, NULL, CLKF_SW_SUP, "mcan_clk" },
	{ 0 },
};

const struct omap_clkctrl_data dra7_clkctrl_data[] __initconst = {
	{ 0x4a005320, dra7_mpu_clkctrl_regs },
	{ 0x4a005420, dra7_dsp1_clkctrl_regs },
	{ 0x4a005520, dra7_ipu1_clkctrl_regs },
	{ 0x4a005550, dra7_ipu_clkctrl_regs },
	{ 0x4a005620, dra7_dsp2_clkctrl_regs },
	{ 0x4a005720, dra7_rtc_clkctrl_regs },
	{ 0x4a008620, dra7_coreaon_clkctrl_regs },
	{ 0x4a008720, dra7_l3main1_clkctrl_regs },
	{ 0x4a008920, dra7_ipu2_clkctrl_regs },
	{ 0x4a008a20, dra7_dma_clkctrl_regs },
	{ 0x4a008b20, dra7_emif_clkctrl_regs },
	{ 0x4a008c00, dra7_atl_clkctrl_regs },
	{ 0x4a008d20, dra7_l4cfg_clkctrl_regs },
	{ 0x4a008e20, dra7_l3instr_clkctrl_regs },
	{ 0x4a009120, dra7_dss_clkctrl_regs },
	{ 0x4a009320, dra7_l3init_clkctrl_regs },
	{ 0x4a0093b0, dra7_pcie_clkctrl_regs },
	{ 0x4a0093d0, dra7_gmac_clkctrl_regs },
	{ 0x4a009728, dra7_l4per_clkctrl_regs },
	{ 0x4a0098a0, dra7_l4sec_clkctrl_regs },
	{ 0x4a00970c, dra7_l4per2_clkctrl_regs },
	{ 0x4a009714, dra7_l4per3_clkctrl_regs },
	{ 0x4ae07820, dra7_wkupaon_clkctrl_regs },
	{ 0 },
};

static struct ti_dt_clk dra7xx_clks[] = {
	DT_CLK(NULL, "timer_32k_ck", "sys_32k_ck"),
	DT_CLK(NULL, "sys_clkin_ck", "timer_sys_clk_div"),
	DT_CLK(NULL, "sys_clkin", "sys_clkin1"),
	DT_CLK(NULL, "atl_dpll_clk_mux", "atl-clkctrl:0000:24"),
	DT_CLK(NULL, "atl_gfclk_mux", "atl-clkctrl:0000:26"),
	DT_CLK(NULL, "dcan1_sys_clk_mux", "wkupaon-clkctrl:0068:24"),
	DT_CLK(NULL, "dss_32khz_clk", "dss-clkctrl:0000:11"),
	DT_CLK(NULL, "dss_48mhz_clk", "dss-clkctrl:0000:9"),
	DT_CLK(NULL, "dss_dss_clk", "dss-clkctrl:0000:8"),
	DT_CLK(NULL, "dss_hdmi_clk", "dss-clkctrl:0000:10"),
	DT_CLK(NULL, "dss_video1_clk", "dss-clkctrl:0000:12"),
	DT_CLK(NULL, "dss_video2_clk", "dss-clkctrl:0000:13"),
	DT_CLK(NULL, "gmac_rft_clk_mux", "gmac-clkctrl:0000:25"),
	DT_CLK(NULL, "gpio1_dbclk", "wkupaon-clkctrl:0018:8"),
	DT_CLK(NULL, "gpio2_dbclk", "l4per-clkctrl:0038:8"),
	DT_CLK(NULL, "gpio3_dbclk", "l4per-clkctrl:0040:8"),
	DT_CLK(NULL, "gpio4_dbclk", "l4per-clkctrl:0048:8"),
	DT_CLK(NULL, "gpio5_dbclk", "l4per-clkctrl:0050:8"),
	DT_CLK(NULL, "gpio6_dbclk", "l4per-clkctrl:0058:8"),
	DT_CLK(NULL, "gpio7_dbclk", "l4per-clkctrl:00e8:8"),
	DT_CLK(NULL, "gpio8_dbclk", "l4per-clkctrl:00f0:8"),
	DT_CLK(NULL, "ipu1_gfclk_mux", "ipu1-clkctrl:0000:24"),
	DT_CLK(NULL, "mcasp1_ahclkr_mux", "ipu-clkctrl:0000:28"),
	DT_CLK(NULL, "mcasp1_ahclkx_mux", "ipu-clkctrl:0000:24"),
	DT_CLK(NULL, "mcasp1_aux_gfclk_mux", "ipu-clkctrl:0000:22"),
	DT_CLK(NULL, "mcasp2_ahclkr_mux", "l4per2-clkctrl:0154:28"),
	DT_CLK(NULL, "mcasp2_ahclkx_mux", "l4per2-clkctrl:0154:24"),
	DT_CLK(NULL, "mcasp2_aux_gfclk_mux", "l4per2-clkctrl:0154:22"),
	DT_CLK(NULL, "mcasp3_ahclkx_mux", "l4per2-clkctrl:015c:24"),
	DT_CLK(NULL, "mcasp3_aux_gfclk_mux", "l4per2-clkctrl:015c:22"),
	DT_CLK(NULL, "mcasp4_ahclkx_mux", "l4per2-clkctrl:018c:24"),
	DT_CLK(NULL, "mcasp4_aux_gfclk_mux", "l4per2-clkctrl:018c:22"),
	DT_CLK(NULL, "mcasp5_ahclkx_mux", "l4per2-clkctrl:016c:24"),
	DT_CLK(NULL, "mcasp5_aux_gfclk_mux", "l4per2-clkctrl:016c:22"),
	DT_CLK(NULL, "mcasp6_ahclkx_mux", "l4per2-clkctrl:01f8:24"),
	DT_CLK(NULL, "mcasp6_aux_gfclk_mux", "l4per2-clkctrl:01f8:22"),
	DT_CLK(NULL, "mcasp7_ahclkx_mux", "l4per2-clkctrl:01fc:24"),
	DT_CLK(NULL, "mcasp7_aux_gfclk_mux", "l4per2-clkctrl:01fc:22"),
	DT_CLK(NULL, "mcasp8_ahclkx_mux", "l4per2-clkctrl:0184:24"),
	DT_CLK(NULL, "mcasp8_aux_gfclk_mux", "l4per2-clkctrl:0184:22"),
	DT_CLK(NULL, "mmc1_clk32k", "l3init-clkctrl:0008:8"),
	DT_CLK(NULL, "mmc1_fclk_div", "l3init-clkctrl:0008:25"),
	DT_CLK(NULL, "mmc1_fclk_mux", "l3init-clkctrl:0008:24"),
	DT_CLK(NULL, "mmc2_clk32k", "l3init-clkctrl:0010:8"),
	DT_CLK(NULL, "mmc2_fclk_div", "l3init-clkctrl:0010:25"),
	DT_CLK(NULL, "mmc2_fclk_mux", "l3init-clkctrl:0010:24"),
	DT_CLK(NULL, "mmc3_clk32k", "l4per-clkctrl:00f8:8"),
	DT_CLK(NULL, "mmc3_gfclk_div", "l4per-clkctrl:00f8:25"),
	DT_CLK(NULL, "mmc3_gfclk_mux", "l4per-clkctrl:00f8:24"),
	DT_CLK(NULL, "mmc4_clk32k", "l4per-clkctrl:0100:8"),
	DT_CLK(NULL, "mmc4_gfclk_div", "l4per-clkctrl:0100:25"),
	DT_CLK(NULL, "mmc4_gfclk_mux", "l4per-clkctrl:0100:24"),
	DT_CLK(NULL, "optfclk_pciephy1_32khz", "pcie-clkctrl:0000:8"),
	DT_CLK(NULL, "optfclk_pciephy1_clk", "pcie-clkctrl:0000:9"),
	DT_CLK(NULL, "optfclk_pciephy1_div_clk", "pcie-clkctrl:0000:10"),
	DT_CLK(NULL, "optfclk_pciephy2_32khz", "pcie-clkctrl:0008:8"),
	DT_CLK(NULL, "optfclk_pciephy2_clk", "pcie-clkctrl:0008:9"),
	DT_CLK(NULL, "optfclk_pciephy2_div_clk", "pcie-clkctrl:0008:10"),
	DT_CLK(NULL, "qspi_gfclk_div", "l4per2-clkctrl:012c:25"),
	DT_CLK(NULL, "qspi_gfclk_mux", "l4per2-clkctrl:012c:24"),
	DT_CLK(NULL, "rmii_50mhz_clk_mux", "gmac-clkctrl:0000:24"),
	DT_CLK(NULL, "sata_ref_clk", "l3init-clkctrl:0068:8"),
	DT_CLK(NULL, "timer10_gfclk_mux", "l4per-clkctrl:0000:24"),
	DT_CLK(NULL, "timer11_gfclk_mux", "l4per-clkctrl:0008:24"),
	DT_CLK(NULL, "timer13_gfclk_mux", "l4per3-clkctrl:00b4:24"),
	DT_CLK(NULL, "timer14_gfclk_mux", "l4per3-clkctrl:00bc:24"),
	DT_CLK(NULL, "timer15_gfclk_mux", "l4per3-clkctrl:00c4:24"),
	DT_CLK(NULL, "timer16_gfclk_mux", "l4per3-clkctrl:011c:24"),
	DT_CLK(NULL, "timer1_gfclk_mux", "wkupaon-clkctrl:0020:24"),
	DT_CLK(NULL, "timer2_gfclk_mux", "l4per-clkctrl:0010:24"),
	DT_CLK(NULL, "timer3_gfclk_mux", "l4per-clkctrl:0018:24"),
	DT_CLK(NULL, "timer4_gfclk_mux", "l4per-clkctrl:0020:24"),
	DT_CLK(NULL, "timer5_gfclk_mux", "ipu-clkctrl:0008:24"),
	DT_CLK(NULL, "timer6_gfclk_mux", "ipu-clkctrl:0010:24"),
	DT_CLK(NULL, "timer7_gfclk_mux", "ipu-clkctrl:0018:24"),
	DT_CLK(NULL, "timer8_gfclk_mux", "ipu-clkctrl:0020:24"),
	DT_CLK(NULL, "timer9_gfclk_mux", "l4per-clkctrl:0028:24"),
	DT_CLK(NULL, "uart10_gfclk_mux", "wkupaon-clkctrl:0060:24"),
	DT_CLK(NULL, "uart1_gfclk_mux", "l4per-clkctrl:0118:24"),
	DT_CLK(NULL, "uart2_gfclk_mux", "l4per-clkctrl:0120:24"),
	DT_CLK(NULL, "uart3_gfclk_mux", "l4per-clkctrl:0128:24"),
	DT_CLK(NULL, "uart4_gfclk_mux", "l4per-clkctrl:0130:24"),
	DT_CLK(NULL, "uart5_gfclk_mux", "l4per-clkctrl:0148:24"),
	DT_CLK(NULL, "uart6_gfclk_mux", "ipu-clkctrl:0030:24"),
	DT_CLK(NULL, "uart7_gfclk_mux", "l4per2-clkctrl:01c4:24"),
	DT_CLK(NULL, "uart8_gfclk_mux", "l4per2-clkctrl:01d4:24"),
	DT_CLK(NULL, "uart9_gfclk_mux", "l4per2-clkctrl:01dc:24"),
	DT_CLK(NULL, "usb_otg_ss1_refclk960m", "l3init-clkctrl:00d0:8"),
	DT_CLK(NULL, "usb_otg_ss2_refclk960m", "l3init-clkctrl:0020:8"),
	{ .node_name = NULL },
};

int __init dra7xx_dt_clk_init(void)
{
	int rc;
	struct clk *dpll_ck, *hdcp_ck;

	if (ti_clk_get_features()->flags & TI_CLK_CLKCTRL_COMPAT)
		ti_dt_clocks_register(dra7xx_compat_clks);
	else
		ti_dt_clocks_register(dra7xx_clks);

	omap2_clk_disable_autoidle_all();

	ti_clk_add_aliases();

	dpll_ck = clk_get_sys(NULL, "dpll_gmac_ck");
	rc = clk_set_rate(dpll_ck, DRA7_DPLL_GMAC_DEFFREQ);
	if (rc)
		pr_err("%s: failed to configure GMAC DPLL!\n", __func__);

	dpll_ck = clk_get_sys(NULL, "dpll_usb_ck");
	rc = clk_set_rate(dpll_ck, DRA7_DPLL_USB_DEFFREQ);
	if (rc)
		pr_err("%s: failed to configure USB DPLL!\n", __func__);

	dpll_ck = clk_get_sys(NULL, "dpll_usb_m2_ck");
	rc = clk_set_rate(dpll_ck, DRA7_DPLL_USB_DEFFREQ/2);
	if (rc)
		pr_err("%s: failed to set USB_DPLL M2 OUT\n", __func__);

	hdcp_ck = clk_get_sys(NULL, "dss_deshdcp_clk");
	rc = clk_prepare_enable(hdcp_ck);
	if (rc)
		pr_err("%s: failed to set dss_deshdcp_clk\n", __func__);

	return rc;
}
