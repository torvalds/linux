// SPDX-License-Identifier: GPL-2.0
/*
 * StarFive JH7110 System Clock Driver
 *
 * Copyright (C) 2022 Emil Renner Berthing <kernel@esmil.dk>
 * Copyright (C) 2022 StarFive Technology Co., Ltd.
 */

#include <linux/auxiliary_bus.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <soc/starfive/reset-starfive-jh71x0.h>

#include <dt-bindings/clock/starfive,jh7110-crg.h>

#include "clk-starfive-jh7110.h"

/* external clocks */
#define JH7110_SYSCLK_OSC			(JH7110_SYSCLK_END + 0)
#define JH7110_SYSCLK_GMAC1_RMII_REFIN		(JH7110_SYSCLK_END + 1)
#define JH7110_SYSCLK_GMAC1_RGMII_RXIN		(JH7110_SYSCLK_END + 2)
#define JH7110_SYSCLK_I2STX_BCLK_EXT		(JH7110_SYSCLK_END + 3)
#define JH7110_SYSCLK_I2STX_LRCK_EXT		(JH7110_SYSCLK_END + 4)
#define JH7110_SYSCLK_I2SRX_BCLK_EXT		(JH7110_SYSCLK_END + 5)
#define JH7110_SYSCLK_I2SRX_LRCK_EXT		(JH7110_SYSCLK_END + 6)
#define JH7110_SYSCLK_TDM_EXT			(JH7110_SYSCLK_END + 7)
#define JH7110_SYSCLK_MCLK_EXT			(JH7110_SYSCLK_END + 8)
#define JH7110_SYSCLK_PLL0_OUT			(JH7110_SYSCLK_END + 9)
#define JH7110_SYSCLK_PLL1_OUT			(JH7110_SYSCLK_END + 10)
#define JH7110_SYSCLK_PLL2_OUT			(JH7110_SYSCLK_END + 11)

static const struct jh71x0_clk_data jh7110_sysclk_data[] __initconst = {
	/* root */
	JH71X0__MUX(JH7110_SYSCLK_CPU_ROOT, "cpu_root", 0, 2,
		    JH7110_SYSCLK_OSC,
		    JH7110_SYSCLK_PLL0_OUT),
	JH71X0__DIV(JH7110_SYSCLK_CPU_CORE, "cpu_core", 7, JH7110_SYSCLK_CPU_ROOT),
	JH71X0__DIV(JH7110_SYSCLK_CPU_BUS, "cpu_bus", 2, JH7110_SYSCLK_CPU_CORE),
	JH71X0__MUX(JH7110_SYSCLK_GPU_ROOT, "gpu_root", 0, 2,
		    JH7110_SYSCLK_PLL2_OUT,
		    JH7110_SYSCLK_PLL1_OUT),
	JH71X0_MDIV(JH7110_SYSCLK_PERH_ROOT, "perh_root", 2, 2,
		    JH7110_SYSCLK_PLL0_OUT,
		    JH7110_SYSCLK_PLL2_OUT),
	JH71X0__MUX(JH7110_SYSCLK_BUS_ROOT, "bus_root", 0, 2,
		    JH7110_SYSCLK_OSC,
		    JH7110_SYSCLK_PLL2_OUT),
	JH71X0__DIV(JH7110_SYSCLK_NOCSTG_BUS, "nocstg_bus", 3, JH7110_SYSCLK_BUS_ROOT),
	JH71X0__DIV(JH7110_SYSCLK_AXI_CFG0, "axi_cfg0", 3, JH7110_SYSCLK_BUS_ROOT),
	JH71X0__DIV(JH7110_SYSCLK_STG_AXIAHB, "stg_axiahb", 2, JH7110_SYSCLK_AXI_CFG0),
	JH71X0_GATE(JH7110_SYSCLK_AHB0, "ahb0", CLK_IS_CRITICAL, JH7110_SYSCLK_STG_AXIAHB),
	JH71X0_GATE(JH7110_SYSCLK_AHB1, "ahb1", CLK_IS_CRITICAL, JH7110_SYSCLK_STG_AXIAHB),
	JH71X0__DIV(JH7110_SYSCLK_APB_BUS, "apb_bus", 8, JH7110_SYSCLK_STG_AXIAHB),
	JH71X0_GATE(JH7110_SYSCLK_APB0, "apb0", CLK_IS_CRITICAL, JH7110_SYSCLK_APB_BUS),
	JH71X0__DIV(JH7110_SYSCLK_PLL0_DIV2, "pll0_div2", 2, JH7110_SYSCLK_PLL0_OUT),
	JH71X0__DIV(JH7110_SYSCLK_PLL1_DIV2, "pll1_div2", 2, JH7110_SYSCLK_PLL1_OUT),
	JH71X0__DIV(JH7110_SYSCLK_PLL2_DIV2, "pll2_div2", 2, JH7110_SYSCLK_PLL2_OUT),
	JH71X0__DIV(JH7110_SYSCLK_AUDIO_ROOT, "audio_root", 8, JH7110_SYSCLK_PLL2_OUT),
	JH71X0__DIV(JH7110_SYSCLK_MCLK_INNER, "mclk_inner", 64, JH7110_SYSCLK_AUDIO_ROOT),
	JH71X0__MUX(JH7110_SYSCLK_MCLK, "mclk", 0, 2,
		    JH7110_SYSCLK_MCLK_INNER,
		    JH7110_SYSCLK_MCLK_EXT),
	JH71X0_GATE(JH7110_SYSCLK_MCLK_OUT, "mclk_out", 0, JH7110_SYSCLK_MCLK_INNER),
	JH71X0_MDIV(JH7110_SYSCLK_ISP_2X, "isp_2x", 8, 2,
		    JH7110_SYSCLK_PLL2_OUT,
		    JH7110_SYSCLK_PLL1_OUT),
	JH71X0__DIV(JH7110_SYSCLK_ISP_AXI, "isp_axi", 4, JH7110_SYSCLK_ISP_2X),
	JH71X0_GDIV(JH7110_SYSCLK_GCLK0, "gclk0", 0, 62, JH7110_SYSCLK_PLL0_DIV2),
	JH71X0_GDIV(JH7110_SYSCLK_GCLK1, "gclk1", 0, 62, JH7110_SYSCLK_PLL1_DIV2),
	JH71X0_GDIV(JH7110_SYSCLK_GCLK2, "gclk2", 0, 62, JH7110_SYSCLK_PLL2_DIV2),
	/* cores */
	JH71X0_GATE(JH7110_SYSCLK_CORE, "core", CLK_IS_CRITICAL, JH7110_SYSCLK_CPU_CORE),
	JH71X0_GATE(JH7110_SYSCLK_CORE1, "core1", CLK_IS_CRITICAL, JH7110_SYSCLK_CPU_CORE),
	JH71X0_GATE(JH7110_SYSCLK_CORE2, "core2", CLK_IS_CRITICAL, JH7110_SYSCLK_CPU_CORE),
	JH71X0_GATE(JH7110_SYSCLK_CORE3, "core3", CLK_IS_CRITICAL, JH7110_SYSCLK_CPU_CORE),
	JH71X0_GATE(JH7110_SYSCLK_CORE4, "core4", CLK_IS_CRITICAL, JH7110_SYSCLK_CPU_CORE),
	JH71X0_GATE(JH7110_SYSCLK_DEBUG, "debug", 0, JH7110_SYSCLK_CPU_BUS),
	JH71X0__DIV(JH7110_SYSCLK_RTC_TOGGLE, "rtc_toggle", 6, JH7110_SYSCLK_OSC),
	JH71X0_GATE(JH7110_SYSCLK_TRACE0, "trace0", 0, JH7110_SYSCLK_CPU_CORE),
	JH71X0_GATE(JH7110_SYSCLK_TRACE1, "trace1", 0, JH7110_SYSCLK_CPU_CORE),
	JH71X0_GATE(JH7110_SYSCLK_TRACE2, "trace2", 0, JH7110_SYSCLK_CPU_CORE),
	JH71X0_GATE(JH7110_SYSCLK_TRACE3, "trace3", 0, JH7110_SYSCLK_CPU_CORE),
	JH71X0_GATE(JH7110_SYSCLK_TRACE4, "trace4", 0, JH7110_SYSCLK_CPU_CORE),
	JH71X0_GATE(JH7110_SYSCLK_TRACE_COM, "trace_com", 0, JH7110_SYSCLK_CPU_BUS),
	/* noc */
	JH71X0_GATE(JH7110_SYSCLK_NOC_BUS_CPU_AXI, "noc_bus_cpu_axi", CLK_IS_CRITICAL,
		    JH7110_SYSCLK_CPU_BUS),
	JH71X0_GATE(JH7110_SYSCLK_NOC_BUS_AXICFG0_AXI, "noc_bus_axicfg0_axi", CLK_IS_CRITICAL,
		    JH7110_SYSCLK_AXI_CFG0),
	/* ddr */
	JH71X0__DIV(JH7110_SYSCLK_OSC_DIV2, "osc_div2", 2, JH7110_SYSCLK_OSC),
	JH71X0__DIV(JH7110_SYSCLK_PLL1_DIV4, "pll1_div4", 2, JH7110_SYSCLK_PLL1_DIV2),
	JH71X0__DIV(JH7110_SYSCLK_PLL1_DIV8, "pll1_div8", 2, JH7110_SYSCLK_PLL1_DIV4),
	JH71X0__MUX(JH7110_SYSCLK_DDR_BUS, "ddr_bus", 0, 4,
		    JH7110_SYSCLK_OSC_DIV2,
		    JH7110_SYSCLK_PLL1_DIV2,
		    JH7110_SYSCLK_PLL1_DIV4,
		    JH7110_SYSCLK_PLL1_DIV8),
	JH71X0_GATE(JH7110_SYSCLK_DDR_AXI, "ddr_axi", CLK_IS_CRITICAL, JH7110_SYSCLK_DDR_BUS),
	/* gpu */
	JH71X0__DIV(JH7110_SYSCLK_GPU_CORE, "gpu_core", 7, JH7110_SYSCLK_GPU_ROOT),
	JH71X0_GATE(JH7110_SYSCLK_GPU_CORE_CLK, "gpu_core_clk", 0, JH7110_SYSCLK_GPU_CORE),
	JH71X0_GATE(JH7110_SYSCLK_GPU_SYS_CLK, "gpu_sys_clk", 0, JH7110_SYSCLK_ISP_AXI),
	JH71X0_GATE(JH7110_SYSCLK_GPU_APB, "gpu_apb", 0, JH7110_SYSCLK_APB_BUS),
	JH71X0_GDIV(JH7110_SYSCLK_GPU_RTC_TOGGLE, "gpu_rtc_toggle", 0, 12, JH7110_SYSCLK_OSC),
	JH71X0_GATE(JH7110_SYSCLK_NOC_BUS_GPU_AXI, "noc_bus_gpu_axi", 0, JH7110_SYSCLK_GPU_CORE),
	/* isp */
	JH71X0_GATE(JH7110_SYSCLK_ISP_TOP_CORE, "isp_top_core", 0, JH7110_SYSCLK_ISP_2X),
	JH71X0_GATE(JH7110_SYSCLK_ISP_TOP_AXI, "isp_top_axi", 0, JH7110_SYSCLK_ISP_AXI),
	JH71X0_GATE(JH7110_SYSCLK_NOC_BUS_ISP_AXI, "noc_bus_isp_axi", CLK_IS_CRITICAL,
		    JH7110_SYSCLK_ISP_AXI),
	/* hifi4 */
	JH71X0__DIV(JH7110_SYSCLK_HIFI4_CORE, "hifi4_core", 15, JH7110_SYSCLK_BUS_ROOT),
	JH71X0__DIV(JH7110_SYSCLK_HIFI4_AXI, "hifi4_axi", 2, JH7110_SYSCLK_HIFI4_CORE),
	/* axi_cfg1 */
	JH71X0_GATE(JH7110_SYSCLK_AXI_CFG1_MAIN, "axi_cfg1_main", CLK_IS_CRITICAL,
		    JH7110_SYSCLK_ISP_AXI),
	JH71X0_GATE(JH7110_SYSCLK_AXI_CFG1_AHB, "axi_cfg1_ahb", CLK_IS_CRITICAL,
		    JH7110_SYSCLK_AHB0),
	/* vout */
	JH71X0_GATE(JH7110_SYSCLK_VOUT_SRC, "vout_src", 0, JH7110_SYSCLK_PLL2_OUT),
	JH71X0__DIV(JH7110_SYSCLK_VOUT_AXI, "vout_axi", 7, JH7110_SYSCLK_PLL2_OUT),
	JH71X0_GATE(JH7110_SYSCLK_NOC_BUS_DISP_AXI, "noc_bus_disp_axi", 0, JH7110_SYSCLK_VOUT_AXI),
	JH71X0_GATE(JH7110_SYSCLK_VOUT_TOP_AHB, "vout_top_ahb", 0, JH7110_SYSCLK_AHB1),
	JH71X0_GATE(JH7110_SYSCLK_VOUT_TOP_AXI, "vout_top_axi", 0, JH7110_SYSCLK_VOUT_AXI),
	JH71X0_GATE(JH7110_SYSCLK_VOUT_TOP_HDMITX0_MCLK, "vout_top_hdmitx0_mclk", 0,
		    JH7110_SYSCLK_MCLK),
	JH71X0__DIV(JH7110_SYSCLK_VOUT_TOP_MIPIPHY_REF, "vout_top_mipiphy_ref", 2,
		    JH7110_SYSCLK_OSC),
	/* jpegc */
	JH71X0__DIV(JH7110_SYSCLK_JPEGC_AXI, "jpegc_axi", 16, JH7110_SYSCLK_PLL2_OUT),
	JH71X0_GATE(JH7110_SYSCLK_CODAJ12_AXI, "codaj12_axi", 0, JH7110_SYSCLK_JPEGC_AXI),
	JH71X0_GDIV(JH7110_SYSCLK_CODAJ12_CORE, "codaj12_core", 0, 16, JH7110_SYSCLK_PLL2_OUT),
	JH71X0_GATE(JH7110_SYSCLK_CODAJ12_APB, "codaj12_apb", 0, JH7110_SYSCLK_APB_BUS),
	/* vdec */
	JH71X0__DIV(JH7110_SYSCLK_VDEC_AXI, "vdec_axi", 7, JH7110_SYSCLK_BUS_ROOT),
	JH71X0_GATE(JH7110_SYSCLK_WAVE511_AXI, "wave511_axi", 0, JH7110_SYSCLK_VDEC_AXI),
	JH71X0_GDIV(JH7110_SYSCLK_WAVE511_BPU, "wave511_bpu", 0, 7, JH7110_SYSCLK_BUS_ROOT),
	JH71X0_GDIV(JH7110_SYSCLK_WAVE511_VCE, "wave511_vce", 0, 7, JH7110_SYSCLK_PLL0_OUT),
	JH71X0_GATE(JH7110_SYSCLK_WAVE511_APB, "wave511_apb", 0, JH7110_SYSCLK_APB_BUS),
	JH71X0_GATE(JH7110_SYSCLK_VDEC_JPG, "vdec_jpg", 0, JH7110_SYSCLK_JPEGC_AXI),
	JH71X0_GATE(JH7110_SYSCLK_VDEC_MAIN, "vdec_main", 0, JH7110_SYSCLK_VDEC_AXI),
	JH71X0_GATE(JH7110_SYSCLK_NOC_BUS_VDEC_AXI, "noc_bus_vdec_axi", 0, JH7110_SYSCLK_VDEC_AXI),
	/* venc */
	JH71X0__DIV(JH7110_SYSCLK_VENC_AXI, "venc_axi", 15, JH7110_SYSCLK_PLL2_OUT),
	JH71X0_GATE(JH7110_SYSCLK_WAVE420L_AXI, "wave420l_axi", 0, JH7110_SYSCLK_VENC_AXI),
	JH71X0_GDIV(JH7110_SYSCLK_WAVE420L_BPU, "wave420l_bpu", 0, 15, JH7110_SYSCLK_PLL2_OUT),
	JH71X0_GDIV(JH7110_SYSCLK_WAVE420L_VCE, "wave420l_vce", 0, 15, JH7110_SYSCLK_PLL2_OUT),
	JH71X0_GATE(JH7110_SYSCLK_WAVE420L_APB, "wave420l_apb", 0, JH7110_SYSCLK_APB_BUS),
	JH71X0_GATE(JH7110_SYSCLK_NOC_BUS_VENC_AXI, "noc_bus_venc_axi", 0, JH7110_SYSCLK_VENC_AXI),
	/* axi_cfg0 */
	JH71X0_GATE(JH7110_SYSCLK_AXI_CFG0_MAIN_DIV, "axi_cfg0_main_div", CLK_IS_CRITICAL,
		    JH7110_SYSCLK_AHB1),
	JH71X0_GATE(JH7110_SYSCLK_AXI_CFG0_MAIN, "axi_cfg0_main", CLK_IS_CRITICAL,
		    JH7110_SYSCLK_AXI_CFG0),
	JH71X0_GATE(JH7110_SYSCLK_AXI_CFG0_HIFI4, "axi_cfg0_hifi4", CLK_IS_CRITICAL,
		    JH7110_SYSCLK_HIFI4_AXI),
	/* intmem */
	JH71X0_GATE(JH7110_SYSCLK_AXIMEM2_AXI, "aximem2_axi", 0, JH7110_SYSCLK_AXI_CFG0),
	/* qspi */
	JH71X0_GATE(JH7110_SYSCLK_QSPI_AHB, "qspi_ahb", 0, JH7110_SYSCLK_AHB1),
	JH71X0_GATE(JH7110_SYSCLK_QSPI_APB, "qspi_apb", 0, JH7110_SYSCLK_APB_BUS),
	JH71X0__DIV(JH7110_SYSCLK_QSPI_REF_SRC, "qspi_ref_src", 16, JH7110_SYSCLK_PLL0_OUT),
	JH71X0_GMUX(JH7110_SYSCLK_QSPI_REF, "qspi_ref", 0, 2,
		    JH7110_SYSCLK_OSC,
		    JH7110_SYSCLK_QSPI_REF_SRC),
	/* sdio */
	JH71X0_GATE(JH7110_SYSCLK_SDIO0_AHB, "sdio0_ahb", 0, JH7110_SYSCLK_AHB0),
	JH71X0_GATE(JH7110_SYSCLK_SDIO1_AHB, "sdio1_ahb", 0, JH7110_SYSCLK_AHB0),
	JH71X0_GDIV(JH7110_SYSCLK_SDIO0_SDCARD, "sdio0_sdcard", 0, 15, JH7110_SYSCLK_AXI_CFG0),
	JH71X0_GDIV(JH7110_SYSCLK_SDIO1_SDCARD, "sdio1_sdcard", 0, 15, JH7110_SYSCLK_AXI_CFG0),
	/* stg */
	JH71X0__DIV(JH7110_SYSCLK_USB_125M, "usb_125m", 15, JH7110_SYSCLK_PLL0_OUT),
	JH71X0_GATE(JH7110_SYSCLK_NOC_BUS_STG_AXI, "noc_bus_stg_axi", CLK_IS_CRITICAL,
		    JH7110_SYSCLK_NOCSTG_BUS),
	/* gmac1 */
	JH71X0_GATE(JH7110_SYSCLK_GMAC1_AHB, "gmac1_ahb", 0, JH7110_SYSCLK_AHB0),
	JH71X0_GATE(JH7110_SYSCLK_GMAC1_AXI, "gmac1_axi", 0, JH7110_SYSCLK_STG_AXIAHB),
	JH71X0__DIV(JH7110_SYSCLK_GMAC_SRC, "gmac_src", 7, JH7110_SYSCLK_PLL0_OUT),
	JH71X0__DIV(JH7110_SYSCLK_GMAC1_GTXCLK, "gmac1_gtxclk", 15, JH7110_SYSCLK_PLL0_OUT),
	JH71X0__DIV(JH7110_SYSCLK_GMAC1_RMII_RTX, "gmac1_rmii_rtx", 30,
		    JH7110_SYSCLK_GMAC1_RMII_REFIN),
	JH71X0_GDIV(JH7110_SYSCLK_GMAC1_PTP, "gmac1_ptp", 0, 31, JH7110_SYSCLK_GMAC_SRC),
	JH71X0__MUX(JH7110_SYSCLK_GMAC1_RX, "gmac1_rx", 0, 2,
		    JH7110_SYSCLK_GMAC1_RGMII_RXIN,
		    JH7110_SYSCLK_GMAC1_RMII_RTX),
	JH71X0__INV(JH7110_SYSCLK_GMAC1_RX_INV, "gmac1_rx_inv", JH7110_SYSCLK_GMAC1_RX),
	JH71X0_GMUX(JH7110_SYSCLK_GMAC1_TX, "gmac1_tx",
		    CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT, 2,
		    JH7110_SYSCLK_GMAC1_GTXCLK,
		    JH7110_SYSCLK_GMAC1_RMII_RTX),
	JH71X0__INV(JH7110_SYSCLK_GMAC1_TX_INV, "gmac1_tx_inv", JH7110_SYSCLK_GMAC1_TX),
	JH71X0_GATE(JH7110_SYSCLK_GMAC1_GTXC, "gmac1_gtxc", 0, JH7110_SYSCLK_GMAC1_GTXCLK),
	/* gmac0 */
	JH71X0_GDIV(JH7110_SYSCLK_GMAC0_GTXCLK, "gmac0_gtxclk", 0, 15, JH7110_SYSCLK_PLL0_OUT),
	JH71X0_GDIV(JH7110_SYSCLK_GMAC0_PTP, "gmac0_ptp", 0, 31, JH7110_SYSCLK_GMAC_SRC),
	JH71X0_GDIV(JH7110_SYSCLK_GMAC_PHY, "gmac_phy", 0, 31, JH7110_SYSCLK_GMAC_SRC),
	JH71X0_GATE(JH7110_SYSCLK_GMAC0_GTXC, "gmac0_gtxc", 0, JH7110_SYSCLK_GMAC0_GTXCLK),
	/* apb misc */
	JH71X0_GATE(JH7110_SYSCLK_IOMUX_APB, "iomux_apb", 0, JH7110_SYSCLK_APB_BUS),
	JH71X0_GATE(JH7110_SYSCLK_MAILBOX_APB, "mailbox_apb", 0, JH7110_SYSCLK_APB_BUS),
	JH71X0_GATE(JH7110_SYSCLK_INT_CTRL_APB, "int_ctrl_apb", 0, JH7110_SYSCLK_APB_BUS),
	/* can0 */
	JH71X0_GATE(JH7110_SYSCLK_CAN0_APB, "can0_apb", 0, JH7110_SYSCLK_APB_BUS),
	JH71X0_GDIV(JH7110_SYSCLK_CAN0_TIMER, "can0_timer", 0, 24, JH7110_SYSCLK_OSC),
	JH71X0_GDIV(JH7110_SYSCLK_CAN0_CAN, "can0_can", 0, 63, JH7110_SYSCLK_PERH_ROOT),
	/* can1 */
	JH71X0_GATE(JH7110_SYSCLK_CAN1_APB, "can1_apb", 0, JH7110_SYSCLK_APB_BUS),
	JH71X0_GDIV(JH7110_SYSCLK_CAN1_TIMER, "can1_timer", 0, 24, JH7110_SYSCLK_OSC),
	JH71X0_GDIV(JH7110_SYSCLK_CAN1_CAN, "can1_can", 0, 63, JH7110_SYSCLK_PERH_ROOT),
	/* pwm */
	JH71X0_GATE(JH7110_SYSCLK_PWM_APB, "pwm_apb", 0, JH7110_SYSCLK_APB_BUS),
	/* wdt */
	JH71X0_GATE(JH7110_SYSCLK_WDT_APB, "wdt_apb", 0, JH7110_SYSCLK_APB_BUS),
	JH71X0_GATE(JH7110_SYSCLK_WDT_CORE, "wdt_core", 0, JH7110_SYSCLK_OSC),
	/* timer */
	JH71X0_GATE(JH7110_SYSCLK_TIMER_APB, "timer_apb", 0, JH7110_SYSCLK_APB_BUS),
	JH71X0_GATE(JH7110_SYSCLK_TIMER0, "timer0", 0, JH7110_SYSCLK_OSC),
	JH71X0_GATE(JH7110_SYSCLK_TIMER1, "timer1", 0, JH7110_SYSCLK_OSC),
	JH71X0_GATE(JH7110_SYSCLK_TIMER2, "timer2", 0, JH7110_SYSCLK_OSC),
	JH71X0_GATE(JH7110_SYSCLK_TIMER3, "timer3", 0, JH7110_SYSCLK_OSC),
	/* temp sensor */
	JH71X0_GATE(JH7110_SYSCLK_TEMP_APB, "temp_apb", 0, JH7110_SYSCLK_APB_BUS),
	JH71X0_GDIV(JH7110_SYSCLK_TEMP_CORE, "temp_core", 0, 24, JH7110_SYSCLK_OSC),
	/* spi */
	JH71X0_GATE(JH7110_SYSCLK_SPI0_APB, "spi0_apb", 0, JH7110_SYSCLK_APB0),
	JH71X0_GATE(JH7110_SYSCLK_SPI1_APB, "spi1_apb", 0, JH7110_SYSCLK_APB0),
	JH71X0_GATE(JH7110_SYSCLK_SPI2_APB, "spi2_apb", 0, JH7110_SYSCLK_APB0),
	JH71X0_GATE(JH7110_SYSCLK_SPI3_APB, "spi3_apb", 0, JH7110_SYSCLK_APB_BUS),
	JH71X0_GATE(JH7110_SYSCLK_SPI4_APB, "spi4_apb", 0, JH7110_SYSCLK_APB_BUS),
	JH71X0_GATE(JH7110_SYSCLK_SPI5_APB, "spi5_apb", 0, JH7110_SYSCLK_APB_BUS),
	JH71X0_GATE(JH7110_SYSCLK_SPI6_APB, "spi6_apb", 0, JH7110_SYSCLK_APB_BUS),
	/* i2c */
	JH71X0_GATE(JH7110_SYSCLK_I2C0_APB, "i2c0_apb", 0, JH7110_SYSCLK_APB0),
	JH71X0_GATE(JH7110_SYSCLK_I2C1_APB, "i2c1_apb", 0, JH7110_SYSCLK_APB0),
	JH71X0_GATE(JH7110_SYSCLK_I2C2_APB, "i2c2_apb", 0, JH7110_SYSCLK_APB0),
	JH71X0_GATE(JH7110_SYSCLK_I2C3_APB, "i2c3_apb", 0, JH7110_SYSCLK_APB_BUS),
	JH71X0_GATE(JH7110_SYSCLK_I2C4_APB, "i2c4_apb", 0, JH7110_SYSCLK_APB_BUS),
	JH71X0_GATE(JH7110_SYSCLK_I2C5_APB, "i2c5_apb", 0, JH7110_SYSCLK_APB_BUS),
	JH71X0_GATE(JH7110_SYSCLK_I2C6_APB, "i2c6_apb", 0, JH7110_SYSCLK_APB_BUS),
	/* uart */
	JH71X0_GATE(JH7110_SYSCLK_UART0_APB, "uart0_apb", 0, JH7110_SYSCLK_APB0),
	JH71X0_GATE(JH7110_SYSCLK_UART0_CORE, "uart0_core", 0, JH7110_SYSCLK_OSC),
	JH71X0_GATE(JH7110_SYSCLK_UART1_APB, "uart1_apb", 0, JH7110_SYSCLK_APB0),
	JH71X0_GATE(JH7110_SYSCLK_UART1_CORE, "uart1_core", 0, JH7110_SYSCLK_OSC),
	JH71X0_GATE(JH7110_SYSCLK_UART2_APB, "uart2_apb", 0, JH7110_SYSCLK_APB0),
	JH71X0_GATE(JH7110_SYSCLK_UART2_CORE, "uart2_core", 0, JH7110_SYSCLK_OSC),
	JH71X0_GATE(JH7110_SYSCLK_UART3_APB, "uart3_apb", 0, JH7110_SYSCLK_APB0),
	JH71X0_GDIV(JH7110_SYSCLK_UART3_CORE, "uart3_core", 0, 10, JH7110_SYSCLK_PERH_ROOT),
	JH71X0_GATE(JH7110_SYSCLK_UART4_APB, "uart4_apb", 0, JH7110_SYSCLK_APB0),
	JH71X0_GDIV(JH7110_SYSCLK_UART4_CORE, "uart4_core", 0, 10, JH7110_SYSCLK_PERH_ROOT),
	JH71X0_GATE(JH7110_SYSCLK_UART5_APB, "uart5_apb", 0, JH7110_SYSCLK_APB0),
	JH71X0_GDIV(JH7110_SYSCLK_UART5_CORE, "uart5_core", 0, 10, JH7110_SYSCLK_PERH_ROOT),
	/* pwmdac */
	JH71X0_GATE(JH7110_SYSCLK_PWMDAC_APB, "pwmdac_apb", 0, JH7110_SYSCLK_APB0),
	JH71X0_GDIV(JH7110_SYSCLK_PWMDAC_CORE, "pwmdac_core", 0, 256, JH7110_SYSCLK_AUDIO_ROOT),
	/* spdif */
	JH71X0_GATE(JH7110_SYSCLK_SPDIF_APB, "spdif_apb", 0, JH7110_SYSCLK_APB0),
	JH71X0_GATE(JH7110_SYSCLK_SPDIF_CORE, "spdif_core", 0, JH7110_SYSCLK_MCLK),
	/* i2stx0 */
	JH71X0_GATE(JH7110_SYSCLK_I2STX0_APB, "i2stx0_apb", 0, JH7110_SYSCLK_APB0),
	JH71X0_GDIV(JH7110_SYSCLK_I2STX0_BCLK_MST, "i2stx0_bclk_mst", 0, 32, JH7110_SYSCLK_MCLK),
	JH71X0__INV(JH7110_SYSCLK_I2STX0_BCLK_MST_INV, "i2stx0_bclk_mst_inv",
		    JH7110_SYSCLK_I2STX0_BCLK_MST),
	JH71X0_MDIV(JH7110_SYSCLK_I2STX0_LRCK_MST, "i2stx0_lrck_mst", 64, 2,
		    JH7110_SYSCLK_I2STX0_BCLK_MST_INV,
		    JH7110_SYSCLK_I2STX0_BCLK_MST),
	JH71X0__MUX(JH7110_SYSCLK_I2STX0_BCLK, "i2stx0_bclk", 0, 2,
		    JH7110_SYSCLK_I2STX0_BCLK_MST,
		    JH7110_SYSCLK_I2STX_BCLK_EXT),
	JH71X0__INV(JH7110_SYSCLK_I2STX0_BCLK_INV, "i2stx0_bclk_inv", JH7110_SYSCLK_I2STX0_BCLK),
	JH71X0__MUX(JH7110_SYSCLK_I2STX0_LRCK, "i2stx0_lrck", 0, 2,
		    JH7110_SYSCLK_I2STX0_LRCK_MST,
		    JH7110_SYSCLK_I2STX_LRCK_EXT),
	/* i2stx1 */
	JH71X0_GATE(JH7110_SYSCLK_I2STX1_APB, "i2stx1_apb", 0, JH7110_SYSCLK_APB0),
	JH71X0_GDIV(JH7110_SYSCLK_I2STX1_BCLK_MST, "i2stx1_bclk_mst", 0, 32, JH7110_SYSCLK_MCLK),
	JH71X0__INV(JH7110_SYSCLK_I2STX1_BCLK_MST_INV, "i2stx1_bclk_mst_inv",
		    JH7110_SYSCLK_I2STX1_BCLK_MST),
	JH71X0_MDIV(JH7110_SYSCLK_I2STX1_LRCK_MST, "i2stx1_lrck_mst", 64, 2,
		    JH7110_SYSCLK_I2STX1_BCLK_MST_INV,
		    JH7110_SYSCLK_I2STX1_BCLK_MST),
	JH71X0__MUX(JH7110_SYSCLK_I2STX1_BCLK, "i2stx1_bclk", 0, 2,
		    JH7110_SYSCLK_I2STX1_BCLK_MST,
		    JH7110_SYSCLK_I2STX_BCLK_EXT),
	JH71X0__INV(JH7110_SYSCLK_I2STX1_BCLK_INV, "i2stx1_bclk_inv", JH7110_SYSCLK_I2STX1_BCLK),
	JH71X0__MUX(JH7110_SYSCLK_I2STX1_LRCK, "i2stx1_lrck", 0, 2,
		    JH7110_SYSCLK_I2STX1_LRCK_MST,
		    JH7110_SYSCLK_I2STX_LRCK_EXT),
	/* i2srx */
	JH71X0_GATE(JH7110_SYSCLK_I2SRX_APB, "i2srx_apb", 0, JH7110_SYSCLK_APB0),
	JH71X0_GDIV(JH7110_SYSCLK_I2SRX_BCLK_MST, "i2srx_bclk_mst", 0, 32, JH7110_SYSCLK_MCLK),
	JH71X0__INV(JH7110_SYSCLK_I2SRX_BCLK_MST_INV, "i2srx_bclk_mst_inv",
		    JH7110_SYSCLK_I2SRX_BCLK_MST),
	JH71X0_MDIV(JH7110_SYSCLK_I2SRX_LRCK_MST, "i2srx_lrck_mst", 64, 2,
		    JH7110_SYSCLK_I2SRX_BCLK_MST_INV,
		    JH7110_SYSCLK_I2SRX_BCLK_MST),
	JH71X0__MUX(JH7110_SYSCLK_I2SRX_BCLK, "i2srx_bclk", 0, 2,
		    JH7110_SYSCLK_I2SRX_BCLK_MST,
		    JH7110_SYSCLK_I2SRX_BCLK_EXT),
	JH71X0__INV(JH7110_SYSCLK_I2SRX_BCLK_INV, "i2srx_bclk_inv", JH7110_SYSCLK_I2SRX_BCLK),
	JH71X0__MUX(JH7110_SYSCLK_I2SRX_LRCK, "i2srx_lrck", 0, 2,
		    JH7110_SYSCLK_I2SRX_LRCK_MST,
		    JH7110_SYSCLK_I2SRX_LRCK_EXT),
	/* pdm */
	JH71X0_GDIV(JH7110_SYSCLK_PDM_DMIC, "pdm_dmic", 0, 64, JH7110_SYSCLK_MCLK),
	JH71X0_GATE(JH7110_SYSCLK_PDM_APB, "pdm_apb", 0, JH7110_SYSCLK_APB0),
	/* tdm */
	JH71X0_GATE(JH7110_SYSCLK_TDM_AHB, "tdm_ahb", 0, JH7110_SYSCLK_AHB0),
	JH71X0_GATE(JH7110_SYSCLK_TDM_APB, "tdm_apb", 0, JH7110_SYSCLK_APB0),
	JH71X0_GDIV(JH7110_SYSCLK_TDM_INTERNAL, "tdm_internal", 0, 64, JH7110_SYSCLK_MCLK),
	JH71X0__MUX(JH7110_SYSCLK_TDM_TDM, "tdm_tdm", 0, 2,
		    JH7110_SYSCLK_TDM_INTERNAL,
		    JH7110_SYSCLK_TDM_EXT),
	JH71X0__INV(JH7110_SYSCLK_TDM_TDM_INV, "tdm_tdm_inv", JH7110_SYSCLK_TDM_TDM),
	/* jtag */
	JH71X0__DIV(JH7110_SYSCLK_JTAG_CERTIFICATION_TRNG, "jtag_certification_trng", 4,
		    JH7110_SYSCLK_OSC),
};

static void jh7110_reset_unregister_adev(void *_adev)
{
	struct auxiliary_device *adev = _adev;

	auxiliary_device_delete(adev);
	auxiliary_device_uninit(adev);
}

static void jh7110_reset_adev_release(struct device *dev)
{
	struct auxiliary_device *adev = to_auxiliary_dev(dev);
	struct jh71x0_reset_adev *rdev = to_jh71x0_reset_adev(adev);

	kfree(rdev);
}

int jh7110_reset_controller_register(struct jh71x0_clk_priv *priv,
				     const char *adev_name,
				     u32 adev_id)
{
	struct jh71x0_reset_adev *rdev;
	struct auxiliary_device *adev;
	int ret;

	rdev = kzalloc(sizeof(*rdev), GFP_KERNEL);
	if (!rdev)
		return -ENOMEM;

	rdev->base = priv->base;

	adev = &rdev->adev;
	adev->name = adev_name;
	adev->dev.parent = priv->dev;
	adev->dev.release = jh7110_reset_adev_release;
	adev->id = adev_id;

	ret = auxiliary_device_init(adev);
	if (ret)
		return ret;

	ret = auxiliary_device_add(adev);
	if (ret) {
		auxiliary_device_uninit(adev);
		return ret;
	}

	return devm_add_action_or_reset(priv->dev,
					jh7110_reset_unregister_adev, adev);
}
EXPORT_SYMBOL_GPL(jh7110_reset_controller_register);

/*
 * This clock notifier is called when the rate of PLL0 clock is to be changed.
 * The cpu_root clock should save the current parent clock and switch its parent
 * clock to osc before PLL0 rate will be changed. Then switch its parent clock
 * back after the PLL0 rate is completed.
 */
static int jh7110_pll0_clk_notifier_cb(struct notifier_block *nb,
				       unsigned long action, void *data)
{
	struct jh71x0_clk_priv *priv = container_of(nb, struct jh71x0_clk_priv, pll_clk_nb);
	struct clk *cpu_root = priv->reg[JH7110_SYSCLK_CPU_ROOT].hw.clk;
	int ret = 0;

	if (action == PRE_RATE_CHANGE) {
		struct clk *osc = clk_get(priv->dev, "osc");

		priv->original_clk = clk_get_parent(cpu_root);
		ret = clk_set_parent(cpu_root, osc);
		clk_put(osc);
	} else if (action == POST_RATE_CHANGE) {
		ret = clk_set_parent(cpu_root, priv->original_clk);
	}

	return notifier_from_errno(ret);
}

static int __init jh7110_syscrg_probe(struct platform_device *pdev)
{
	struct jh71x0_clk_priv *priv;
	unsigned int idx;
	int ret;
	struct clk *pllclk;

	priv = devm_kzalloc(&pdev->dev,
			    struct_size(priv, reg, JH7110_SYSCLK_END),
			    GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	spin_lock_init(&priv->rmw_lock);
	priv->num_reg = JH7110_SYSCLK_END;
	priv->dev = &pdev->dev;
	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	/* Use fixed factor clocks if can not get the PLL clocks from DTS */
	pllclk = clk_get(priv->dev, "pll0_out");
	if (IS_ERR(pllclk)) {
		/* 24MHz -> 1000.0MHz */
		priv->pll[0] = devm_clk_hw_register_fixed_factor(priv->dev, "pll0_out",
								 "osc", 0, 125, 3);
		if (IS_ERR(priv->pll[0]))
			return PTR_ERR(priv->pll[0]);
	} else {
		priv->pll_clk_nb.notifier_call = jh7110_pll0_clk_notifier_cb;
		ret = clk_notifier_register(pllclk, &priv->pll_clk_nb);
		if (ret)
			return ret;
		priv->pll[0] = NULL;
	}

	pllclk = clk_get(priv->dev, "pll1_out");
	if (IS_ERR(pllclk)) {
		/* 24MHz -> 1066.0MHz */
		priv->pll[1] = devm_clk_hw_register_fixed_factor(priv->dev, "pll1_out",
								 "osc", 0, 533, 12);
		if (IS_ERR(priv->pll[1]))
			return PTR_ERR(priv->pll[1]);
	} else {
		clk_put(pllclk);
		priv->pll[1] = NULL;
	}

	pllclk = clk_get(priv->dev, "pll2_out");
	if (IS_ERR(pllclk)) {
		/* 24MHz -> 1188.0MHz */
		priv->pll[2] = devm_clk_hw_register_fixed_factor(priv->dev, "pll2_out",
								 "osc", 0, 99, 2);
		if (IS_ERR(priv->pll[2]))
			return PTR_ERR(priv->pll[2]);
	} else {
		clk_put(pllclk);
		priv->pll[2] = NULL;
	}

	for (idx = 0; idx < JH7110_SYSCLK_END; idx++) {
		u32 max = jh7110_sysclk_data[idx].max;
		struct clk_parent_data parents[4] = {};
		struct clk_init_data init = {
			.name = jh7110_sysclk_data[idx].name,
			.ops = starfive_jh71x0_clk_ops(max),
			.parent_data = parents,
			.num_parents =
				((max & JH71X0_CLK_MUX_MASK) >> JH71X0_CLK_MUX_SHIFT) + 1,
			.flags = jh7110_sysclk_data[idx].flags,
		};
		struct jh71x0_clk *clk = &priv->reg[idx];
		unsigned int i;

		for (i = 0; i < init.num_parents; i++) {
			unsigned int pidx = jh7110_sysclk_data[idx].parents[i];

			if (pidx < JH7110_SYSCLK_END)
				parents[i].hw = &priv->reg[pidx].hw;
			else if (pidx == JH7110_SYSCLK_OSC)
				parents[i].fw_name = "osc";
			else if (pidx == JH7110_SYSCLK_GMAC1_RMII_REFIN)
				parents[i].fw_name = "gmac1_rmii_refin";
			else if (pidx == JH7110_SYSCLK_GMAC1_RGMII_RXIN)
				parents[i].fw_name = "gmac1_rgmii_rxin";
			else if (pidx == JH7110_SYSCLK_I2STX_BCLK_EXT)
				parents[i].fw_name = "i2stx_bclk_ext";
			else if (pidx == JH7110_SYSCLK_I2STX_LRCK_EXT)
				parents[i].fw_name = "i2stx_lrck_ext";
			else if (pidx == JH7110_SYSCLK_I2SRX_BCLK_EXT)
				parents[i].fw_name = "i2srx_bclk_ext";
			else if (pidx == JH7110_SYSCLK_I2SRX_LRCK_EXT)
				parents[i].fw_name = "i2srx_lrck_ext";
			else if (pidx == JH7110_SYSCLK_TDM_EXT)
				parents[i].fw_name = "tdm_ext";
			else if (pidx == JH7110_SYSCLK_MCLK_EXT)
				parents[i].fw_name = "mclk_ext";
			else if (pidx == JH7110_SYSCLK_PLL0_OUT && !priv->pll[0])
				parents[i].fw_name = "pll0_out";
			else if (pidx == JH7110_SYSCLK_PLL1_OUT && !priv->pll[1])
				parents[i].fw_name = "pll1_out";
			else if (pidx == JH7110_SYSCLK_PLL2_OUT && !priv->pll[2])
				parents[i].fw_name = "pll2_out";
			else
				parents[i].hw = priv->pll[pidx - JH7110_SYSCLK_PLL0_OUT];
		}

		clk->hw.init = &init;
		clk->idx = idx;
		clk->max_div = max & JH71X0_CLK_DIV_MASK;

		ret = devm_clk_hw_register(&pdev->dev, &clk->hw);
		if (ret)
			return ret;
	}

	ret = devm_of_clk_add_hw_provider(&pdev->dev, jh71x0_clk_get, priv);
	if (ret)
		return ret;

	return jh7110_reset_controller_register(priv, "rst-sys", 0);
}

static const struct of_device_id jh7110_syscrg_match[] = {
	{ .compatible = "starfive,jh7110-syscrg" },
	{ /* sentinel */ }
};

static struct platform_driver jh7110_syscrg_driver = {
	.driver = {
		.name = "clk-starfive-jh7110-sys",
		.of_match_table = jh7110_syscrg_match,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver_probe(jh7110_syscrg_driver, jh7110_syscrg_probe);
