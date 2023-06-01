// SPDX-License-Identifier: GPL-2.0
/*
 * StarFive JH7110 sys Clock Generator Driver
 *
 * Copyright (C) 2022 StarFive Technology Co., Ltd.
 * Author: Xingyu Wu <xingyu.wu@starfivetech.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include <dt-bindings/clock/starfive-jh7110-clkgen.h>
#include "clk-starfive-jh7110.h"

/* sys external clocks */
#define JH7110_OSC				(JH7110_CLK_END + 0)
#define JH7110_GMAC1_RMII_REFIN			(JH7110_CLK_END + 1)
#define JH7110_GMAC1_RGMII_RXIN			(JH7110_CLK_END + 2)
#define JH7110_I2STX_BCLK_EXT			(JH7110_CLK_END + 3)
#define JH7110_I2STX_LRCK_EXT			(JH7110_CLK_END + 4)
#define JH7110_I2SRX_BCLK_EXT			(JH7110_CLK_END + 5)
#define JH7110_I2SRX_LRCK_EXT			(JH7110_CLK_END + 6)
#define JH7110_TDM_EXT				(JH7110_CLK_END + 7)
#define JH7110_MCLK_EXT				(JH7110_CLK_END + 8)
#define JH7110_JTAG_TCK_INNER			(JH7110_CLK_END + 9)
#define JH7110_BIST_APB				(JH7110_CLK_END + 10)

static const struct jh7110_clk_data jh7110_clk_sys_data[] __initconst = {
	/*root*/
	JH7110__MUX(JH7110_CPU_ROOT, "cpu_root", PARENT_NUMS_2,
			JH7110_OSC,
			JH7110_PLL0_OUT),
	JH7110__DIV(JH7110_CPU_CORE, "cpu_core", 7, JH7110_CPU_ROOT),
	JH7110__DIV(JH7110_CPU_BUS, "cpu_bus", 2, JH7110_CPU_CORE),
	JH7110__MUX(JH7110_GPU_ROOT, "gpu_root", PARENT_NUMS_2,
			JH7110_PLL2_OUT,
			JH7110_PLL1_OUT),
	JH7110_MDIV(JH7110_PERH_ROOT, "perh_root", 2, PARENT_NUMS_2,
			JH7110_PLL0_OUT,
			JH7110_PLL2_OUT),
	JH7110__MUX(JH7110_BUS_ROOT, "bus_root", PARENT_NUMS_2,
			JH7110_OSC,
			JH7110_PLL2_OUT),
	JH7110__DIV(JH7110_NOCSTG_BUS, "nocstg_bus", 3, JH7110_BUS_ROOT),
	JH7110__DIV(JH7110_AXI_CFG0, "axi_cfg0", 3, JH7110_BUS_ROOT),
	JH7110__DIV(JH7110_STG_AXIAHB, "stg_axiahb", 2, JH7110_AXI_CFG0),
	JH7110_GATE(JH7110_AHB0, "ahb0", CLK_IS_CRITICAL, JH7110_STG_AXIAHB),
	JH7110_GATE(JH7110_AHB1, "ahb1", CLK_IS_CRITICAL, JH7110_STG_AXIAHB),
	JH7110__DIV(JH7110_APB_BUS_FUNC, "apb_bus_func",
			8, JH7110_STG_AXIAHB),
	JH7110_GATE(JH7110_APB0, "apb0", CLK_IS_CRITICAL, JH7110_APB_BUS),
	JH7110__DIV(JH7110_PLL0_DIV2, "pll0_div2", 2, JH7110_PLL0_OUT),
	JH7110__DIV(JH7110_PLL1_DIV2, "pll1_div2", 2, JH7110_PLL1_OUT),
	JH7110__DIV(JH7110_PLL2_DIV2, "pll2_div2", 2, JH7110_PLL2_OUT),
	JH7110__DIV(JH7110_AUDIO_ROOT, "audio_root", 8, JH7110_PLL2_OUT),
	JH7110__DIV(JH7110_MCLK_INNER, "mclk_inner", 64, JH7110_AUDIO_ROOT),
	JH7110__MUX(JH7110_MCLK, "mclk", PARENT_NUMS_2,
			JH7110_MCLK_INNER,
			JH7110_MCLK_EXT),
	JH7110_GATE(JH7110_MCLK_OUT, "mclk_out", GATE_FLAG_NORMAL,
			JH7110_MCLK_INNER),
	JH7110_MDIV(JH7110_ISP_2X, "isp_2x", 8, PARENT_NUMS_2,
			JH7110_PLL2_OUT,
			JH7110_PLL1_OUT),
	JH7110__DIV(JH7110_ISP_AXI, "isp_axi", 4, JH7110_ISP_2X),
	JH7110_GDIV(JH7110_GCLK0, "gclk0", GATE_FLAG_NORMAL,
			62, JH7110_PLL0_DIV2),
	JH7110_GDIV(JH7110_GCLK1, "gclk1", GATE_FLAG_NORMAL,
			62, JH7110_PLL1_DIV2),
	JH7110_GDIV(JH7110_GCLK2, "gclk2", GATE_FLAG_NORMAL,
			62, JH7110_PLL2_DIV2),
	/*u0_u7mc_sft7110*/
	JH7110_GATE(JH7110_U7_CORE_CLK, "u0_u7mc_sft7110_core_clk",
			CLK_IGNORE_UNUSED, JH7110_CPU_CORE),
	JH7110_GATE(JH7110_U7_CORE_CLK1, "u0_u7mc_sft7110_core_clk1",
			CLK_IGNORE_UNUSED, JH7110_CPU_CORE),
	JH7110_GATE(JH7110_U7_CORE_CLK2, "u0_u7mc_sft7110_core_clk2",
			CLK_IGNORE_UNUSED, JH7110_CPU_CORE),
	JH7110_GATE(JH7110_U7_CORE_CLK3, "u0_u7mc_sft7110_core_clk3",
			CLK_IGNORE_UNUSED, JH7110_CPU_CORE),
	JH7110_GATE(JH7110_U7_CORE_CLK4, "u0_u7mc_sft7110_core_clk4",
			CLK_IGNORE_UNUSED, JH7110_CPU_CORE),
	JH7110_GATE(JH7110_U7_DEBUG_CLK, "u0_u7mc_sft7110_debug_clk",
			CLK_IGNORE_UNUSED, JH7110_CPU_BUS),
	JH7110__DIV(JH7110_U7_RTC_TOGGLE, "u0_u7mc_sft7110_rtc_toggle",
			6, JH7110_OSC),
	JH7110_GATE(JH7110_U7_TRACE_CLK0, "u0_u7mc_sft7110_trace_clk0",
			CLK_IGNORE_UNUSED, JH7110_CPU_CORE),
	JH7110_GATE(JH7110_U7_TRACE_CLK1, "u0_u7mc_sft7110_trace_clk1",
			CLK_IGNORE_UNUSED, JH7110_CPU_CORE),
	JH7110_GATE(JH7110_U7_TRACE_CLK2, "u0_u7mc_sft7110_trace_clk2",
			CLK_IGNORE_UNUSED, JH7110_CPU_CORE),
	JH7110_GATE(JH7110_U7_TRACE_CLK3, "u0_u7mc_sft7110_trace_clk3",
			CLK_IGNORE_UNUSED, JH7110_CPU_CORE),
	JH7110_GATE(JH7110_U7_TRACE_CLK4, "u0_u7mc_sft7110_trace_clk4",
			CLK_IGNORE_UNUSED, JH7110_CPU_CORE),
	JH7110_GATE(JH7110_U7_TRACE_COM_CLK, "u0_u7mc_sft7110_trace_com_clk",
			CLK_IGNORE_UNUSED, JH7110_CPU_BUS),
	//NOC
	JH7110_GATE(JH7110_NOC_BUS_CLK_CPU_AXI,
			"u0_sft7110_noc_bus_clk_cpu_axi",
			CLK_IS_CRITICAL, JH7110_CPU_BUS),
	JH7110_GATE(JH7110_NOC_BUS_CLK_AXICFG0_AXI,
			"u0_sft7110_noc_bus_clk_axicfg0_axi",
			CLK_IS_CRITICAL, JH7110_AXI_CFG0),
	//DDRC
	JH7110__DIV(JH7110_OSC_DIV2, "osc_div2", 2, JH7110_OSC),
	JH7110__DIV(JH7110_PLL1_DIV4, "pll1_div4", 2, JH7110_PLL1_DIV2),
	JH7110__DIV(JH7110_PLL1_DIV8, "pll1_div8", 2, JH7110_PLL1_DIV4),
	JH7110__MUX(JH7110_DDR_BUS, "ddr_bus", PARENT_NUMS_4,
			JH7110_OSC_DIV2,
			JH7110_PLL1_DIV2,
			JH7110_PLL1_DIV4,
			JH7110_PLL1_DIV8),
	JH7110_GATE(JH7110_DDR_CLK_AXI, "u0_ddr_sft7110_clk_axi",
			CLK_IGNORE_UNUSED, JH7110_DDR_BUS),
	//GPU
	JH7110__DIV(JH7110_GPU_CORE, "gpu_core", 7, JH7110_GPU_ROOT),
	JH7110_GATE(JH7110_GPU_CORE_CLK, "u0_img_gpu_core_clk",
			GATE_FLAG_NORMAL, JH7110_GPU_CORE),
	JH7110_GATE(JH7110_GPU_SYS_CLK, "u0_img_gpu_sys_clk",
			GATE_FLAG_NORMAL, JH7110_AXI_CFG1),
	JH7110_GATE(JH7110_GPU_CLK_APB, "u0_img_gpu_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB12),
	JH7110_GDIV(JH7110_GPU_RTC_TOGGLE, "u0_img_gpu_rtc_toggle",
			GATE_FLAG_NORMAL, 12, JH7110_OSC),
	JH7110_GATE(JH7110_NOC_BUS_CLK_GPU_AXI,
			"u0_sft7110_noc_bus_clk_gpu_axi",
			GATE_FLAG_NORMAL, JH7110_GPU_CORE),
	//ISP
	JH7110_GATE(JH7110_ISP_TOP_CLK_ISPCORE_2X,
			"u0_dom_isp_top_clk_dom_isp_top_clk_ispcore_2x",
			GATE_FLAG_NORMAL, JH7110_ISP_2X),
	JH7110_GATE(JH7110_ISP_TOP_CLK_ISP_AXI,
			"u0_dom_isp_top_clk_dom_isp_top_clk_isp_axi",
			GATE_FLAG_NORMAL, JH7110_ISP_AXI),
	JH7110_GATE(JH7110_NOC_BUS_CLK_ISP_AXI,
			"u0_sft7110_noc_bus_clk_isp_axi",
			CLK_IS_CRITICAL, JH7110_ISP_AXI),
	//HIFI4
	JH7110__DIV(JH7110_HIFI4_CORE, "hifi4_core", 15, JH7110_BUS_ROOT),
	JH7110__DIV(JH7110_HIFI4_AXI, "hifi4_axi", 2, JH7110_HIFI4_CORE),
	//AXICFG1_DEC
	JH7110_GATE(JH7110_AXI_CFG1_DEC_CLK_MAIN, "u0_axi_cfg1_dec_clk_main",
			CLK_IGNORE_UNUSED, JH7110_AXI_CFG1),
	JH7110_GATE(JH7110_AXI_CFG1_DEC_CLK_AHB, "u0_axi_cfg1_dec_clk_ahb",
			CLK_IGNORE_UNUSED, JH7110_AHB0),
	//VOUT
	JH7110_GATE(JH7110_VOUT_SRC,
			"u0_dom_vout_top_clk_dom_vout_top_clk_vout_src",
			GATE_FLAG_NORMAL, JH7110_VOUT_ROOT),
	JH7110__DIV(JH7110_VOUT_AXI, "vout_axi", 7, JH7110_VOUT_ROOT),
	JH7110_GATE(JH7110_NOC_BUS_CLK_DISP_AXI,
			"u0_sft7110_noc_bus_clk_disp_axi",
			GATE_FLAG_NORMAL, JH7110_VOUT_AXI),
	JH7110_GATE(JH7110_VOUT_TOP_CLK_VOUT_AHB,
			"u0_dom_vout_top_clk_dom_vout_top_clk_vout_ahb",
			GATE_FLAG_NORMAL, JH7110_AHB1),
	JH7110_GATE(JH7110_VOUT_TOP_CLK_VOUT_AXI,
			"u0_dom_vout_top_clk_dom_vout_top_clk_vout_axi",
			GATE_FLAG_NORMAL, JH7110_VOUT_AXI),
	JH7110_GATE(JH7110_VOUT_TOP_CLK_HDMITX0_MCLK,
			"u0_dom_vout_top_clk_dom_vout_top_clk_hdmitx0_mclk",
			GATE_FLAG_NORMAL, JH7110_MCLK),
	JH7110__DIV(JH7110_VOUT_TOP_CLK_MIPIPHY_REF,
			"u0_dom_vout_top_clk_dom_vout_top_clk_mipiphy_ref",
			2, JH7110_OSC),
	//JPEGC
	JH7110__DIV(JH7110_JPEGC_AXI, "jpegc_axi", 16, JH7110_VENC_ROOT),
	JH7110_GATE(JH7110_CODAJ12_CLK_AXI, "u0_CODAJ12_clk_axi",
			GATE_FLAG_NORMAL, JH7110_JPEGC_AXI),
	JH7110_GDIV(JH7110_CODAJ12_CLK_CORE, "u0_CODAJ12_clk_core",
			GATE_FLAG_NORMAL, 16, JH7110_VENC_ROOT),
	JH7110_GATE(JH7110_CODAJ12_CLK_APB, "u0_CODAJ12_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB12),
	//VDEC
	JH7110__DIV(JH7110_VDEC_AXI, "vdec_axi", 7, JH7110_BUS_ROOT),
	JH7110_GATE(JH7110_WAVE511_CLK_AXI, "u0_WAVE511_clk_axi",
			GATE_FLAG_NORMAL, JH7110_VDEC_AXI),
	JH7110_GDIV(JH7110_WAVE511_CLK_BPU, "u0_WAVE511_clk_bpu",
			GATE_FLAG_NORMAL, 7, JH7110_BUS_ROOT),
	JH7110_GDIV(JH7110_WAVE511_CLK_VCE, "u0_WAVE511_clk_vce",
			GATE_FLAG_NORMAL, 7, JH7110_VDEC_ROOT),
	JH7110_GATE(JH7110_WAVE511_CLK_APB, "u0_WAVE511_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB12),
	JH7110_GATE(JH7110_VDEC_JPG_ARB_JPGCLK, "u0_vdec_jpg_arb_jpgclk",
			CLK_IGNORE_UNUSED, JH7110_JPEGC_AXI),
	JH7110_GATE(JH7110_VDEC_JPG_ARB_MAINCLK, "u0_vdec_jpg_arb_mainclk",
			CLK_IGNORE_UNUSED, JH7110_VDEC_AXI),
	JH7110_GATE(JH7110_NOC_BUS_CLK_VDEC_AXI,
			"u0_sft7110_noc_bus_clk_vdec_axi",
			GATE_FLAG_NORMAL, JH7110_VDEC_AXI),
	//VENC
	JH7110__DIV(JH7110_VENC_AXI, "venc_axi", 15, JH7110_VENC_ROOT),
	JH7110_GATE(JH7110_WAVE420L_CLK_AXI, "u0_wave420l_clk_axi",
			GATE_FLAG_NORMAL, JH7110_VENC_AXI),
	JH7110_GDIV(JH7110_WAVE420L_CLK_BPU, "u0_wave420l_clk_bpu",
			GATE_FLAG_NORMAL, 15, JH7110_VENC_ROOT),
	JH7110_GDIV(JH7110_WAVE420L_CLK_VCE, "u0_wave420l_clk_vce",
			GATE_FLAG_NORMAL, 15, JH7110_VENC_ROOT),
	JH7110_GATE(JH7110_WAVE420L_CLK_APB, "u0_wave420l_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB12),
	JH7110_GATE(JH7110_NOC_BUS_CLK_VENC_AXI,
			"u0_sft7110_noc_bus_clk_venc_axi",
			GATE_FLAG_NORMAL, JH7110_VENC_AXI),
	//INTMEM
	JH7110_GATE(JH7110_AXI_CFG0_DEC_CLK_MAIN_DIV,
			"u0_axi_cfg0_dec_clk_main_div",
			CLK_IGNORE_UNUSED, JH7110_AHB1),
	JH7110_GATE(JH7110_AXI_CFG0_DEC_CLK_MAIN, "u0_axi_cfg0_dec_clk_main",
			CLK_IGNORE_UNUSED, JH7110_AXI_CFG0),
	JH7110_GATE(JH7110_AXI_CFG0_DEC_CLK_HIFI4, "u0_axi_cfg0_dec_clk_hifi4",
			CLK_IGNORE_UNUSED, JH7110_HIFI4_AXI),
	JH7110_GATE(JH7110_AXIMEM2_128B_CLK_AXI, "u2_aximem_128b_clk_axi",
			CLK_IGNORE_UNUSED, JH7110_AXI_CFG0),
	//QSPI
	JH7110_GATE(JH7110_QSPI_CLK_AHB, "u0_cdns_qspi_clk_ahb",
			CLK_IGNORE_UNUSED, JH7110_AHB1),
	JH7110_GATE(JH7110_QSPI_CLK_APB, "u0_cdns_qspi_clk_apb",
			CLK_IGNORE_UNUSED, JH7110_APB12),
	JH7110__DIV(JH7110_QSPI_REF_SRC, "u0_cdns_qspi_ref_src",
			16, JH7110_GMACUSB_ROOT),
	JH7110_GMUX(JH7110_QSPI_CLK_REF, "u0_cdns_qspi_clk_ref",
			CLK_IGNORE_UNUSED, PARENT_NUMS_2,
			JH7110_OSC,
			JH7110_QSPI_REF_SRC),
	//SDIO
	JH7110_GATE(JH7110_SDIO0_CLK_AHB, "u0_dw_sdio_clk_ahb",
			CLK_IGNORE_UNUSED, JH7110_AHB0),
	JH7110_GATE(JH7110_SDIO1_CLK_AHB, "u1_dw_sdio_clk_ahb",
			CLK_IGNORE_UNUSED, JH7110_AHB0),
	JH7110_GDIV(JH7110_SDIO0_CLK_SDCARD, "u0_dw_sdio_clk_sdcard",
			CLK_IGNORE_UNUSED, 15, JH7110_AXI_CFG0),
	JH7110_GDIV(JH7110_SDIO1_CLK_SDCARD, "u1_dw_sdio_clk_sdcard",
			CLK_IGNORE_UNUSED, 15, JH7110_AXI_CFG0),
	//STG
	JH7110__DIV(JH7110_USB_125M, "usb_125m", 15, JH7110_GMACUSB_ROOT),
	JH7110_GATE(JH7110_NOC_BUS_CLK_STG_AXI,
			"u0_sft7110_noc_bus_clk_stg_axi",
			CLK_IGNORE_UNUSED, JH7110_NOCSTG_BUS),
	//GMAC1
	JH7110_GATE(JH7110_GMAC5_CLK_AHB, "u1_dw_gmac5_axi64_clk_ahb",
			GATE_FLAG_NORMAL, JH7110_AHB0),
	JH7110_GATE(JH7110_GMAC5_CLK_AXI, "u1_dw_gmac5_axi64_clk_axi",
			GATE_FLAG_NORMAL, JH7110_STG_AXIAHB),
	JH7110__DIV(JH7110_GMAC_SRC, "gmac_src", 7, JH7110_GMACUSB_ROOT),
	JH7110__DIV(JH7110_GMAC1_GTXCLK, "gmac1_gtxclk",
			15, JH7110_GMACUSB_ROOT),
	JH7110__DIV(JH7110_GMAC1_RMII_RTX, "gmac1_rmii_rtx",
			30, JH7110_GMAC1_RMII_REFIN),
	JH7110_GDIV(JH7110_GMAC5_CLK_PTP, "u1_dw_gmac5_axi64_clk_ptp",
			GATE_FLAG_NORMAL, 31, JH7110_GMAC_SRC),
	JH7110__MUX(JH7110_GMAC5_CLK_RX, "u1_dw_gmac5_axi64_clk_rx",
			PARENT_NUMS_2,
			JH7110_GMAC1_RGMII_RXIN,
			JH7110_GMAC1_RMII_RTX),
	JH7110__INV(JH7110_GMAC5_CLK_RX_INV, "u1_dw_gmac5_axi64_clk_rx_inv",
			JH7110_GMAC5_CLK_RX),
	JH7110_GMUX(JH7110_GMAC5_CLK_TX, "u1_dw_gmac5_axi64_clk_tx",
			GATE_FLAG_NORMAL, PARENT_NUMS_2,
			JH7110_GMAC1_GTXCLK,
			JH7110_GMAC1_RMII_RTX),
	JH7110__INV(JH7110_GMAC5_CLK_TX_INV, "u1_dw_gmac5_axi64_clk_tx_inv",
			JH7110_GMAC5_CLK_TX),
	JH7110_GATE(JH7110_GMAC1_GTXC, "gmac1_gtxc",
			GATE_FLAG_NORMAL, JH7110_GMAC1_GTXCLK),
	//GMAC0
	JH7110_GDIV(JH7110_GMAC0_GTXCLK, "gmac0_gtxclk",
			GATE_FLAG_NORMAL, 15, JH7110_GMACUSB_ROOT),
	JH7110_GDIV(JH7110_GMAC0_PTP, "gmac0_ptp",
			GATE_FLAG_NORMAL, 31, JH7110_GMAC_SRC),
	JH7110_GDIV(JH7110_GMAC_PHY, "gmac_phy",
			GATE_FLAG_NORMAL, 31, JH7110_GMAC_SRC),
	JH7110_GATE(JH7110_GMAC0_GTXC, "gmac0_gtxc",
			GATE_FLAG_NORMAL, JH7110_GMAC0_GTXCLK),
	//SYS MISC
	JH7110_GATE(JH7110_SYS_IOMUX_PCLK, "u0_sys_iomux_pclk",
			CLK_IGNORE_UNUSED, JH7110_APB12),
	JH7110_GATE(JH7110_MAILBOX_CLK_APB, "u0_mailbox_clk_apb",
			CLK_IGNORE_UNUSED, JH7110_APB12),
	JH7110_GATE(JH7110_INT_CTRL_CLK_APB, "u0_int_ctrl_clk_apb",
			CLK_IGNORE_UNUSED, JH7110_APB12),
	//CAN
	JH7110_GATE(JH7110_CAN0_CTRL_CLK_APB, "u0_can_ctrl_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB12),
	JH7110_GDIV(JH7110_CAN0_CTRL_CLK_TIMER, "u0_can_ctrl_clk_timer",
			GATE_FLAG_NORMAL, 24, JH7110_OSC),
	JH7110_GDIV(JH7110_CAN0_CTRL_CLK_CAN, "u0_can_ctrl_clk_can",
			GATE_FLAG_NORMAL, 63, JH7110_PERH_ROOT),
	JH7110_GATE(JH7110_CAN1_CTRL_CLK_APB, "u1_can_ctrl_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB12),
	JH7110_GDIV(JH7110_CAN1_CTRL_CLK_TIMER, "u1_can_ctrl_clk_timer",
			GATE_FLAG_NORMAL, 24, JH7110_OSC),
	JH7110_GDIV(JH7110_CAN1_CTRL_CLK_CAN, "u1_can_ctrl_clk_can",
			GATE_FLAG_NORMAL, 63, JH7110_PERH_ROOT),
	//PWM
	JH7110_GATE(JH7110_PWM_CLK_APB, "u0_pwm_8ch_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB12),
	//WDT
	JH7110_GATE(JH7110_DSKIT_WDT_CLK_APB, "u0_dskit_wdt_clk_apb",
			CLK_IGNORE_UNUSED, JH7110_APB12),
	JH7110_GATE(JH7110_DSKIT_WDT_CLK_WDT, "u0_dskit_wdt_clk_wdt",
			CLK_IGNORE_UNUSED, JH7110_OSC),
	//TIMER
	JH7110_GATE(JH7110_TIMER_CLK_APB, "u0_si5_timer_clk_apb",
			CLK_IGNORE_UNUSED, JH7110_APB12),
	JH7110_GATE(JH7110_TIMER_CLK_TIMER0, "u0_si5_timer_clk_timer0",
			CLK_IGNORE_UNUSED, JH7110_OSC),
	JH7110_GATE(JH7110_TIMER_CLK_TIMER1, "u0_si5_timer_clk_timer1",
			CLK_IGNORE_UNUSED, JH7110_OSC),
	JH7110_GATE(JH7110_TIMER_CLK_TIMER2, "u0_si5_timer_clk_timer2",
			CLK_IGNORE_UNUSED, JH7110_OSC),
	JH7110_GATE(JH7110_TIMER_CLK_TIMER3, "u0_si5_timer_clk_timer3",
			CLK_IGNORE_UNUSED, JH7110_OSC),
	//TEMP SENSOR
	JH7110_GATE(JH7110_TEMP_SENSOR_CLK_APB, "u0_temp_sensor_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB12),
	JH7110_GDIV(JH7110_TEMP_SENSOR_CLK_TEMP, "u0_temp_sensor_clk_temp",
			GATE_FLAG_NORMAL, 24, JH7110_OSC),
	//SPI
	JH7110_GATE(JH7110_SPI0_CLK_APB, "u0_ssp_spi_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB0),
	JH7110_GATE(JH7110_SPI1_CLK_APB, "u1_ssp_spi_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB0),
	JH7110_GATE(JH7110_SPI2_CLK_APB, "u2_ssp_spi_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB0),
	JH7110_GATE(JH7110_SPI3_CLK_APB, "u3_ssp_spi_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB12),
	JH7110_GATE(JH7110_SPI4_CLK_APB, "u4_ssp_spi_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB12),
	JH7110_GATE(JH7110_SPI5_CLK_APB, "u5_ssp_spi_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB12),
	JH7110_GATE(JH7110_SPI6_CLK_APB, "u6_ssp_spi_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB12),
	//I2C
	JH7110_GATE(JH7110_I2C0_CLK_APB, "u0_dw_i2c_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB0),
	JH7110_GATE(JH7110_I2C1_CLK_APB, "u1_dw_i2c_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB0),
	JH7110_GATE(JH7110_I2C2_CLK_APB, "u2_dw_i2c_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB0),
	JH7110_GATE(JH7110_I2C3_CLK_APB, "u3_dw_i2c_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB12),
	JH7110_GATE(JH7110_I2C4_CLK_APB, "u4_dw_i2c_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB12),
	JH7110_GATE(JH7110_I2C5_CLK_APB, "u5_dw_i2c_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB12),
	JH7110_GATE(JH7110_I2C6_CLK_APB, "u6_dw_i2c_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB12),
	//UART
	JH7110_GATE(JH7110_UART0_CLK_APB, "u0_dw_uart_clk_apb",
			CLK_IGNORE_UNUSED, JH7110_APB0),
	JH7110_GATE(JH7110_UART0_CLK_CORE, "u0_dw_uart_clk_core",
			CLK_IGNORE_UNUSED, JH7110_OSC),
	JH7110_GATE(JH7110_UART1_CLK_APB, "u1_dw_uart_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB0),
	JH7110_GATE(JH7110_UART1_CLK_CORE, "u1_dw_uart_clk_core",
			GATE_FLAG_NORMAL, JH7110_OSC),
	JH7110_GATE(JH7110_UART2_CLK_APB, "u2_dw_uart_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB0),
	JH7110_GATE(JH7110_UART2_CLK_CORE, "u2_dw_uart_clk_core",
			GATE_FLAG_NORMAL, JH7110_OSC),
	JH7110_GATE(JH7110_UART3_CLK_APB, "u3_dw_uart_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB0),
	JH7110_GDIV(JH7110_UART3_CLK_CORE, "u3_dw_uart_clk_core",
			GATE_FLAG_NORMAL, 10, JH7110_PERH_ROOT),
	JH7110_GATE(JH7110_UART4_CLK_APB, "u4_dw_uart_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB0),
	JH7110_GDIV(JH7110_UART4_CLK_CORE, "u4_dw_uart_clk_core",
			GATE_FLAG_NORMAL, 10, JH7110_PERH_ROOT),
	JH7110_GATE(JH7110_UART5_CLK_APB, "u5_dw_uart_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB0),
	JH7110_GDIV(JH7110_UART5_CLK_CORE, "u5_dw_uart_clk_core",
			GATE_FLAG_NORMAL, 10, JH7110_PERH_ROOT),
	//PWMDAC
	JH7110_GATE(JH7110_PWMDAC_CLK_APB, "u0_pwmdac_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB0),
	JH7110_GDIV(JH7110_PWMDAC_CLK_CORE, "u0_pwmdac_clk_core",
			GATE_FLAG_NORMAL, 256, JH7110_AUDIO_ROOT),
	//SPDIF
	JH7110_GATE(JH7110_SPDIF_CLK_APB, "u0_cdns_spdif_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB0),
	JH7110_GATE(JH7110_SPDIF_CLK_CORE, "u0_cdns_spdif_clk_core",
			GATE_FLAG_NORMAL, JH7110_MCLK),
	//I2STX0_4CH0
	JH7110_GATE(JH7110_I2STX0_4CHCLK_APB, "u0_i2stx_4ch_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB0),
	JH7110_GDIV(JH7110_I2STX_4CH0_BCLK_MST, "i2stx_4ch0_bclk_mst",
			GATE_FLAG_NORMAL, 32, JH7110_MCLK),
	JH7110__INV(JH7110_I2STX_4CH0_BCLK_MST_INV, "i2stx_4ch0_bclk_mst_inv",
			JH7110_I2STX_4CH0_BCLK_MST),
	JH7110_MDIV(JH7110_I2STX_4CH0_LRCK_MST, "i2stx_4ch0_lrck_mst",
			64, PARENT_NUMS_2,
			JH7110_I2STX_4CH0_BCLK_MST_INV,
			JH7110_I2STX_4CH0_BCLK_MST),
	JH7110__MUX(JH7110_I2STX0_4CHBCLK, "u0_i2stx_4ch_bclk",
			PARENT_NUMS_2,
			JH7110_I2STX_4CH0_BCLK_MST,
			JH7110_I2STX_BCLK_EXT),
	JH7110__INV(JH7110_I2STX0_4CHBCLK_N, "u0_i2stx_4ch_bclk_n",
			JH7110_I2STX0_4CHBCLK),
	JH7110__MUX(JH7110_I2STX0_4CHLRCK, "u0_i2stx_4ch_lrck",
			PARENT_NUMS_2,
			JH7110_I2STX_4CH0_LRCK_MST,
			JH7110_I2STX_LRCK_EXT),
	//I2STX1_4CH0
	JH7110_GATE(JH7110_I2STX1_4CHCLK_APB, "u1_i2stx_4ch_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB0),
	JH7110_GDIV(JH7110_I2STX_4CH1_BCLK_MST, "i2stx_4ch1_bclk_mst",
			GATE_FLAG_NORMAL, 32, JH7110_MCLK),
	JH7110__INV(JH7110_I2STX_4CH1_BCLK_MST_INV, "i2stx_4ch1_bclk_mst_inv",
			JH7110_I2STX_4CH1_BCLK_MST),
	JH7110_MDIV(JH7110_I2STX_4CH1_LRCK_MST, "i2stx_4ch1_lrck_mst",
			64, PARENT_NUMS_2,
			JH7110_I2STX_4CH1_BCLK_MST_INV,
			JH7110_I2STX_4CH1_BCLK_MST),
	JH7110__MUX(JH7110_I2STX1_4CHBCLK, "u1_i2stx_4ch_bclk",
			PARENT_NUMS_2,
			JH7110_I2STX_4CH1_BCLK_MST,
			JH7110_I2STX_BCLK_EXT),
	JH7110__INV(JH7110_I2STX1_4CHBCLK_N, "u1_i2stx_4ch_bclk_n",
			JH7110_I2STX1_4CHBCLK),
	JH7110__MUX(JH7110_I2STX1_4CHLRCK, "u1_i2stx_4ch_lrck",
			PARENT_NUMS_2,
			JH7110_I2STX_4CH1_LRCK_MST,
			JH7110_I2STX_LRCK_EXT),
	//I2SRX_3CH
	JH7110_GATE(JH7110_I2SRX0_3CH_CLK_APB, "u0_i2srx_3ch_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB0),
	JH7110_GDIV(JH7110_I2SRX_3CH_BCLK_MST, "i2srx_3ch_bclk_mst",
			GATE_FLAG_NORMAL, 32, JH7110_MCLK),
	JH7110__INV(JH7110_I2SRX_3CH_BCLK_MST_INV, "i2srx_3ch_bclk_mst_inv",
			JH7110_I2SRX_3CH_BCLK_MST),
	JH7110_MDIV(JH7110_I2SRX_3CH_LRCK_MST, "i2srx_3ch_lrck_mst",
			64, PARENT_NUMS_2,
			JH7110_I2SRX_3CH_BCLK_MST_INV,
			JH7110_I2SRX_3CH_BCLK_MST),
	JH7110__MUX(JH7110_I2SRX0_3CH_BCLK, "u0_i2srx_3ch_bclk",
			PARENT_NUMS_2,
			JH7110_I2SRX_3CH_BCLK_MST,
			JH7110_I2SRX_BCLK_EXT),
	JH7110__INV(JH7110_I2SRX0_3CH_BCLK_N, "u0_i2srx_3ch_bclk_n",
			JH7110_I2SRX0_3CH_BCLK),
	JH7110__MUX(JH7110_I2SRX0_3CH_LRCK, "u0_i2srx_3ch_lrck",
			PARENT_NUMS_2,
			JH7110_I2SRX_3CH_LRCK_MST,
			JH7110_I2SRX_LRCK_EXT),
	//PDM_4MIC
	JH7110_GDIV(JH7110_PDM_CLK_DMIC, "u0_pdm_4mic_clk_dmic",
			GATE_FLAG_NORMAL, 64, JH7110_MCLK),
	JH7110_GATE(JH7110_PDM_CLK_APB, "u0_pdm_4mic_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB0),
	//TDM
	JH7110_GATE(JH7110_TDM_CLK_AHB, "u0_tdm16slot_clk_ahb",
			GATE_FLAG_NORMAL, JH7110_AHB0),
	JH7110_GATE(JH7110_TDM_CLK_APB, "u0_tdm16slot_clk_apb",
			GATE_FLAG_NORMAL, JH7110_APB0),
	JH7110_GDIV(JH7110_TDM_INTERNAL, "tdm_internal",
			GATE_FLAG_NORMAL, 64, JH7110_MCLK),
	JH7110__MUX(JH7110_TDM_CLK_TDM, "u0_tdm16slot_clk_tdm",
			PARENT_NUMS_2,
			JH7110_TDM_INTERNAL,
			JH7110_TDM_EXT),
	JH7110__INV(JH7110_TDM_CLK_TDM_N, "u0_tdm16slot_clk_tdm_n",
			JH7110_TDM_CLK_TDM),
	JH7110__DIV(JH7110_JTAG_CERTIFICATION_TRNG_CLK,
			"u0_jtag_certification_trng_clk", 4, JH7110_OSC),
};

int __init clk_starfive_jh7110_sys_init(struct platform_device *pdev,
						struct jh7110_clk_priv *priv)
{
	unsigned int idx;
	int ret = 0;

	priv->sys_base = devm_platform_ioremap_resource_byname(pdev, "sys");
	if (IS_ERR(priv->sys_base))
		return PTR_ERR(priv->sys_base);

#ifndef CONFIG_CLK_STARFIVE_JH7110_PLL
	priv->pll[PLL_OF(JH7110_PLL0_OUT)] =
			clk_hw_register_fixed_rate(priv->dev,
			"pll0_out", "osc", 0, 1250000000);
	if (IS_ERR(priv->pll[PLL_OF(JH7110_PLL0_OUT)]))
		return PTR_ERR(priv->pll[PLL_OF(JH7110_PLL0_OUT)]);

	priv->pll[PLL_OF(JH7110_PLL1_OUT)] =
			clk_hw_register_fixed_rate(priv->dev,
			"pll1_out", "osc", 0, 1066000000);
	if (IS_ERR(priv->pll[PLL_OF(JH7110_PLL1_OUT)]))
		return PTR_ERR(priv->pll[PLL_OF(JH7110_PLL1_OUT)]);

	priv->pll[PLL_OF(JH7110_PLL2_OUT)] =
			clk_hw_register_fixed_rate(priv->dev,
			"pll2_out", "osc", 0, 1228800000);
	if (IS_ERR(priv->pll[PLL_OF(JH7110_PLL2_OUT)]))
		return PTR_ERR(priv->pll[PLL_OF(JH7110_PLL2_OUT)]);
#endif

	priv->pll[PLL_OF(JH7110_AON_APB)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"aon_apb", "apb_bus_func", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_RESET1_CTRL_CLK_SRC)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u1_reset_ctrl_clk_src", "osc", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_DDR_ROOT)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"ddr_root", "pll1_out", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_VDEC_ROOT)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"vdec_root", "pll0_out", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_VENC_ROOT)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"venc_root", "pll2_out", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_VOUT_ROOT)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"vout_root", "pll2_out", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_GMACUSB_ROOT)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"gmacusb_root", "pll0_out", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_PCLK2_MUX_FUNC_PCLK)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u2_pclk_mux_func_pclk", "apb_bus_func", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_PCLK2_MUX_BIST_PCLK)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u2_pclk_mux_bist_pclk", "bist_apb", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_APB_BUS)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"apb_bus", "u2_pclk_mux_pclk", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_APB12)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"apb12", "apb_bus", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_AXI_CFG1)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"axi_cfg1", "isp_axi", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_PLL_WRAP_CRG_GCLK0)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_pll_wrap_crg_gclk0", "gclk0", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_PLL_WRAP_CRG_GCLK1)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_pll_wrap_crg_gclk1", "gclk1", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_PLL_WRAP_CRG_GCLK2)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_pll_wrap_crg_gclk2", "gclk2", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_JTAG2APB_PCLK)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_jtag2apb_pclk", "bist_apb", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_U7_BUS_CLK)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_u7mc_sft7110_bus_clk", "cpu_bus", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_U7_IRQ_SYNC_BUS_CLK)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_u7mc_sft7110_irq_sync_bus_clk", "cpu_bus", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_NOC_BUS_CLK2_CPU_AXI)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_sft7110_noc_bus_clk2_cpu_axi",
			"u0_sft7110_noc_bus_clk_cpu_axi", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_NOC_BUS_CLK_APB_BUS)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_sft7110_noc_bus_clk_apb_bus", "apb_bus", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_NOC_BUS_CLK2_APB_BUS)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_sft7110_noc_bus_clk2_apb_bus",
			"u0_sft7110_noc_bus_clk_apb_bus", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_NOC_BUS_CLK2_AXICFG0_AXI)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_sft7110_noc_bus_clk2_axicfg0_axi",
			"u0_sft7110_noc_bus_clk_axicfg0_axi", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_DDR_CLK_DDRPHY_PLL_BYPASS)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_ddr_sft7110_clk_ddrphy_pll_bypass",
			"pll1_out", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_DDR_CLK_OSC)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_ddr_sft7110_clk_osc", "osc", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_DDR_CLK_APB)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_ddr_sft7110_clk_apb", "apb12", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_NOC_BUS_CLK_DDRC)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_sft7110_noc_bus_clk_ddrc", "ddr_bus", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_NOC_BUS_CLK2_DDRC)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_sft7110_noc_bus_clk2_ddrc",
			"u0_sft7110_noc_bus_clk_ddrc", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_SYS_AHB_DEC_CLK_AHB)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_saif_amba_sys_ahb_dec_clk_ahb", "ahb0", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_STG_AHB_DEC_CLK_AHB)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_saif_amba_stg_ahb_dec_clk_ahb", "ahb0", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_NOC_BUS_CLK2_GPU_AXI)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_sft7110_noc_bus_clk2_gpu_axi",
			"u0_sft7110_noc_bus_clk_gpu_axi", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_ISP_TOP_CLK_DVP)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_dom_isp_top_clk_dom_isp_top_clk_dvp",
			"dvp_clk", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_NOC_BUS_CLK2_ISP_AXI)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_sft7110_noc_bus_clk2_isp_axi",
			"u0_sft7110_noc_bus_clk_isp_axi", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_ISP_TOP_CLK_BIST_APB)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_dom_isp_top_clk_dom_isp_top_clk_bist_apb",
			"bist_apb", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_NOC_BUS_CLK2_DISP_AXI)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_sft7110_noc_bus_clk2_disp_axi",
			"u0_sft7110_noc_bus_clk_disp_axi", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_VOUT_TOP_CLK_HDMITX0_BCLK)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_dom_vout_top_clk_dom_vout_top_clk_hdmitx0_bclk",
			"u0_i2stx_4ch_bclk", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_VOUT_TOP_U0_HDMI_TX_PIN_WS)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_dom_vout_top_u0_hdmi_tx_pin_ws",
			"u0_i2stx_4ch_lrck", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_VOUT_TOP_CLK_HDMIPHY_REF)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_dom_vout_top_clk_dom_vout_top_clk_hdmiphy_ref",
			"osc", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_VOUT_TOP_BIST_PCLK)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_dom_vout_top_clk_dom_vout_top_bist_pclk",
			"bist_apb", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_AXIMEM0_128B_CLK_AXI)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_aximem_128b_clk_axi",
			"u0_WAVE511_clk_axi", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_VDEC_INTSRAM_CLK_VDEC_AXI)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_vdec_intsram_clk_vdec_axi",
			"u0_aximem_128b_clk_axi", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_NOC_BUS_CLK2_VDEC_AXI)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_sft7110_noc_bus_clk2_vdec_axi",
			"u0_sft7110_noc_bus_clk_vdec_axi", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_AXIMEM1_128B_CLK_AXI)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u1_aximem_128b_clk_axi",
			"u0_wave420l_clk_axi", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_VENC_INTSRAM_CLK_VENC_AXI)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_venc_intsram_clk_venc_axi",
			"u0_wave420l_clk_axi", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_NOC_BUS_CLK2_VENC_AXI)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_sft7110_noc_bus_clk2_venc_axi",
			"u0_sft7110_noc_bus_clk_venc_axi", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_SRAM_CLK_ROM)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_intmem_rom_sram_clk_rom",
			"u2_aximem_128b_clk_axi", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_NOC_BUS_CLK2_STG_AXI)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_sft7110_noc_bus_clk2_stg_axi",
			"u0_sft7110_noc_bus_clk_stg_axi", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_GMAC5_CLK_RMII)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u1_dw_gmac5_axi64_clk_rmii",
			"gmac1_rmii_refin", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_AON_AHB)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"aon_ahb", "stg_axiahb", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_SYS_CRG_PCLK)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_sys_crg_pclk", "apb12", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_SYS_SYSCON_PCLK)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_sys_syscon_pclk", "apb12", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_SPI0_CLK_CORE)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_ssp_spi_clk_core", "u0_ssp_spi_clk_apb", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_SPI1_CLK_CORE)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u1_ssp_spi_clk_core", "u1_ssp_spi_clk_apb", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_SPI2_CLK_CORE)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u2_ssp_spi_clk_core", "u2_ssp_spi_clk_apb", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_SPI3_CLK_CORE)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u3_ssp_spi_clk_core", "u3_ssp_spi_clk_apb", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_SPI4_CLK_CORE)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u4_ssp_spi_clk_core", "u4_ssp_spi_clk_apb", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_SPI5_CLK_CORE)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u5_ssp_spi_clk_core", "u5_ssp_spi_clk_apb", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_SPI6_CLK_CORE)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u6_ssp_spi_clk_core", "u6_ssp_spi_clk_apb", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_I2C0_CLK_CORE)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_dw_i2c_clk_core", "u0_dw_i2c_clk_apb", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_I2C1_CLK_CORE)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u1_dw_i2c_clk_core", "u1_dw_i2c_clk_apb", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_I2C2_CLK_CORE)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u2_dw_i2c_clk_core", "u2_dw_i2c_clk_apb", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_I2C3_CLK_CORE)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u3_dw_i2c_clk_core", "u3_dw_i2c_clk_apb", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_I2C4_CLK_CORE)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u4_dw_i2c_clk_core", "u4_dw_i2c_clk_apb", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_I2C5_CLK_CORE)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u5_dw_i2c_clk_core", "u5_dw_i2c_clk_apb", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_I2C6_CLK_CORE)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u6_dw_i2c_clk_core", "u6_dw_i2c_clk_apb", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_I2STX_BCLK_MST)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"i2stx_bclk_mst", "i2stx_4ch1_bclk_mst", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_I2STX_LRCK_MST)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"i2stx_lrck_mst", "i2stx_4ch1_lrck_mst", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_I2SRX_BCLK_MST)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"i2srx_bclk_mst", "i2srx_3ch_bclk_mst", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_I2SRX_LRCK_MST)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"i2srx_lrck_mst", "i2srx_3ch_lrck_mst", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_PDM_CLK_DMIC0_BCLK_SLV)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_pdm_4mic_clk_dmic0_bclk_slv",
			"u0_i2srx_3ch_bclk", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_PDM_CLK_DMIC0_LRCK_SLV)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_pdm_4mic_clk_dmic0_lrck_slv",
			"u0_i2srx_3ch_lrck", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_PDM_CLK_DMIC1_BCLK_SLV)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_pdm_4mic_clk_dmic1_bclk_slv",
			"u0_i2srx_3ch_bclk", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_PDM_CLK_DMIC1_LRCK_SLV)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_pdm_4mic_clk_dmic1_lrck_slv",
			"u0_i2srx_3ch_lrck", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_TDM_CLK_MST)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"tdm_clk_mst", "ahb0", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_AHB2APB_CLK_AHB)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u1_ahb2apb_clk_ahb", "tdm_internal", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_P2P_ASYNC_CLK_APBS)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u1_p2p_async_clk_apbs", "apb0", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_P2P_ASYNC_CLK_APBM)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u1_p2p_async_clk_apbm", "aon_apb", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_JTAG_DAISY_CHAIN_JTAG_TCK)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_jtag_daisy_chain_JTAG_TCK",
			"jtag_tck_inner", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_U7_DEBUG_SYSTEMJTAG_JTAG_TCK)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_u7mc_sft7110_debug_systemjtag_jtag_TCK",
			"u0_jtag_daisy_chain_jtag_tck_0", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_E2_DEBUG_SYSTEMJTAG_TCK)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_e2_sft7110_debug_systemjtag_jtag_TCK",
			"u0_jtag_daisy_chain_jtag_tck_1", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_JTAG_CERTIFICATION_TCK)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_jtag_certification_tck",
			"jtag_tck_inner", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_SEC_SKP_CLK)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u0_sec_top_skp_clk",
			"u0_jtag_certification_trng_clk", 0, 1, 1);
	priv->pll[PLL_OF(JH7110_U2_PCLK_MUX_PCLK)] =
			devm_clk_hw_register_fixed_factor(priv->dev,
			"u2_pclk_mux_pclk",
			"u2_pclk_mux_func_pclk", 0, 1, 1);


	for (idx = 0; idx < JH7110_CLK_SYS_REG_END; idx++) {
		u32 max = jh7110_clk_sys_data[idx].max;
		struct clk_parent_data parents[4] = {};
		struct clk_init_data init = {
			.name = jh7110_clk_sys_data[idx].name,
			.ops = starfive_jh7110_clk_ops(max),
			.parent_data = parents,
			.num_parents = ((max & JH7110_CLK_MUX_MASK) >>
					JH7110_CLK_MUX_SHIFT) + 1,
			.flags = jh7110_clk_sys_data[idx].flags,
		};
		struct jh7110_clk *clk = &priv->reg[idx];
		unsigned int i;

		for (i = 0; i < init.num_parents; i++) {
			unsigned int pidx = jh7110_clk_sys_data[idx].parents[i];

			if (pidx < JH7110_CLK_SYS_REG_END)
				parents[i].hw = &priv->reg[pidx].hw;
#ifdef CONFIG_CLK_STARFIVE_JH7110_PLL
			else if ((pidx == JH7110_PLL0_OUT) || (pidx == JH7110_PLL2_OUT))
				parents[i].hw = &priv->pll_priv[PLL_OF(pidx)].hw;
#endif
			else if ((pidx < JH7110_CLK_SYS_END) &&
				(pidx > JH7110_CLK_SYS_REG_END))
				parents[i].hw = priv->pll[PLL_OF(pidx)];
			else if (pidx == JH7110_OSC)
				parents[i].fw_name = "osc";
			else if (pidx == JH7110_GMAC1_RMII_REFIN)
				parents[i].fw_name = "gmac1_rmii_refin";
			else if (pidx == JH7110_GMAC1_RGMII_RXIN)
				parents[i].fw_name = "gmac1_rgmii_rxin";
			else if (pidx == JH7110_I2STX_BCLK_EXT)
				parents[i].fw_name = "i2stx_bclk_ext";
			else if (pidx == JH7110_I2STX_LRCK_EXT)
				parents[i].fw_name = "i2stx_lrck_ext";
			else if (pidx == JH7110_I2SRX_BCLK_EXT)
				parents[i].fw_name = "i2srx_bclk_ext";
			else if (pidx == JH7110_I2SRX_LRCK_EXT)
				parents[i].fw_name = "i2srx_lrck_ext";
			else if (pidx == JH7110_TDM_EXT)
				parents[i].fw_name = "tdm_ext";
			else if (pidx == JH7110_MCLK_EXT)
				parents[i].fw_name = "mclk_ext";
			else if (pidx == JH7110_JTAG_TCK_INNER)
				parents[i].fw_name = "jtag_tclk_inner";
			else if (pidx == JH7110_BIST_APB)
				parents[i].fw_name = "bist_apb";
		}

		clk->hw.init = &init;
		clk->idx = idx;
		clk->max_div = max & JH7110_CLK_DIV_MASK;
		clk->reg_flags = JH7110_CLK_SYS_FLAG;

		ret = devm_clk_hw_register(priv->dev, &clk->hw);
		if (ret)
			return ret;
	}

	dev_dbg(&pdev->dev, "starfive JH7110 clk_sys init successfully.");
	return 0;
}
