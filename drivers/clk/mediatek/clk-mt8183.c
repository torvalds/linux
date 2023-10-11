// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2018 MediaTek Inc.
// Author: Weiyi Lu <weiyi.lu@mediatek.com>

#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "clk-gate.h"
#include "clk-mtk.h"
#include "clk-mux.h"

#include <dt-bindings/clock/mt8183-clk.h>

static DEFINE_SPINLOCK(mt8183_clk_lock);

static const struct mtk_fixed_clk top_fixed_clks[] = {
	FIXED_CLK(CLK_TOP_CLK26M, "f_f26m_ck", "clk26m", 26000000),
	FIXED_CLK(CLK_TOP_ULPOSC, "osc", NULL, 250000),
	FIXED_CLK(CLK_TOP_UNIVP_192M, "univpll_192m", "univpll", 192000000),
};

/*
 * To retain compatibility with older devicetrees, we keep CLK_TOP_CLK13M
 * valid, but renamed from "clk13m" (defined as fixed clock in the new
 * devicetrees) to "clk26m_d2", satisfying the older clock assignments.
 * This means that on new devicetrees "clk26m_d2" is unused.
 */
static const struct mtk_fixed_factor top_divs[] = {
	FACTOR(CLK_TOP_CLK13M, "clk26m_d2", "clk26m", 1, 2),
	FACTOR(CLK_TOP_F26M_CK_D2, "csw_f26m_ck_d2", "clk26m", 1, 2),
	FACTOR_FLAGS(CLK_TOP_SYSPLL_CK, "syspll_ck", "mainpll", 1, 1, 0),
	FACTOR_FLAGS(CLK_TOP_SYSPLL_D2, "syspll_d2", "syspll_ck", 1, 2, 0),
	FACTOR_FLAGS(CLK_TOP_SYSPLL_D2_D2, "syspll_d2_d2", "syspll_d2", 1, 2, 0),
	FACTOR_FLAGS(CLK_TOP_SYSPLL_D2_D4, "syspll_d2_d4", "syspll_d2", 1, 4, 0),
	FACTOR_FLAGS(CLK_TOP_SYSPLL_D2_D8, "syspll_d2_d8", "syspll_d2", 1, 8, 0),
	FACTOR_FLAGS(CLK_TOP_SYSPLL_D2_D16, "syspll_d2_d16", "syspll_d2", 1, 16, 0),
	FACTOR_FLAGS(CLK_TOP_SYSPLL_D3, "syspll_d3", "mainpll", 1, 3, 0),
	FACTOR_FLAGS(CLK_TOP_SYSPLL_D3_D2, "syspll_d3_d2", "syspll_d3", 1, 2, 0),
	FACTOR_FLAGS(CLK_TOP_SYSPLL_D3_D4, "syspll_d3_d4", "syspll_d3", 1, 4, 0),
	FACTOR_FLAGS(CLK_TOP_SYSPLL_D3_D8, "syspll_d3_d8", "syspll_d3", 1, 8, 0),
	FACTOR_FLAGS(CLK_TOP_SYSPLL_D5, "syspll_d5", "mainpll", 1, 5, 0),
	FACTOR_FLAGS(CLK_TOP_SYSPLL_D5_D2, "syspll_d5_d2", "syspll_d5", 1, 2, 0),
	FACTOR_FLAGS(CLK_TOP_SYSPLL_D5_D4, "syspll_d5_d4", "syspll_d5", 1, 4, 0),
	FACTOR_FLAGS(CLK_TOP_SYSPLL_D7, "syspll_d7", "mainpll", 1, 7, 0),
	FACTOR_FLAGS(CLK_TOP_SYSPLL_D7_D2, "syspll_d7_d2", "syspll_d7", 1, 2, 0),
	FACTOR_FLAGS(CLK_TOP_SYSPLL_D7_D4, "syspll_d7_d4", "syspll_d7", 1, 4, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_CK, "univpll_ck", "univpll", 1, 1, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D2, "univpll_d2", "univpll_ck", 1, 2, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D2_D2, "univpll_d2_d2", "univpll_d2", 1, 2, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D2_D4, "univpll_d2_d4", "univpll_d2", 1, 4, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D2_D8, "univpll_d2_d8", "univpll_d2", 1, 8, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D3, "univpll_d3", "univpll", 1, 3, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D3_D2, "univpll_d3_d2", "univpll_d3", 1, 2, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D3_D4, "univpll_d3_d4", "univpll_d3", 1, 4, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D3_D8, "univpll_d3_d8", "univpll_d3", 1, 8, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D5, "univpll_d5", "univpll", 1, 5, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D5_D2, "univpll_d5_d2", "univpll_d5", 1, 2, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D5_D4, "univpll_d5_d4", "univpll_d5", 1, 4, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D5_D8, "univpll_d5_d8", "univpll_d5", 1, 8, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D7, "univpll_d7", "univpll", 1, 7, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVP_192M_CK, "univ_192m_ck", "univpll_192m", 1, 1, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVP_192M_D2, "univ_192m_d2", "univ_192m_ck", 1, 2, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVP_192M_D4, "univ_192m_d4", "univ_192m_ck", 1, 4, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVP_192M_D8, "univ_192m_d8", "univ_192m_ck", 1, 8, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVP_192M_D16, "univ_192m_d16", "univ_192m_ck", 1, 16, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVP_192M_D32, "univ_192m_d32", "univ_192m_ck", 1, 32, 0),
	FACTOR(CLK_TOP_APLL1_CK, "apll1_ck", "apll1", 1, 1),
	FACTOR(CLK_TOP_APLL1_D2, "apll1_d2", "apll1", 1, 2),
	FACTOR(CLK_TOP_APLL1_D4, "apll1_d4", "apll1", 1, 4),
	FACTOR(CLK_TOP_APLL1_D8, "apll1_d8", "apll1", 1, 8),
	FACTOR(CLK_TOP_APLL2_CK, "apll2_ck", "apll2", 1, 1),
	FACTOR(CLK_TOP_APLL2_D2, "apll2_d2", "apll2", 1, 2),
	FACTOR(CLK_TOP_APLL2_D4, "apll2_d4", "apll2", 1, 4),
	FACTOR(CLK_TOP_APLL2_D8, "apll2_d8", "apll2", 1, 8),
	FACTOR(CLK_TOP_TVDPLL_CK, "tvdpll_ck", "tvdpll", 1, 1),
	FACTOR(CLK_TOP_TVDPLL_D2, "tvdpll_d2", "tvdpll_ck", 1, 2),
	FACTOR(CLK_TOP_TVDPLL_D4, "tvdpll_d4", "tvdpll", 1, 4),
	FACTOR(CLK_TOP_TVDPLL_D8, "tvdpll_d8", "tvdpll", 1, 8),
	FACTOR(CLK_TOP_TVDPLL_D16, "tvdpll_d16", "tvdpll", 1, 16),
	FACTOR(CLK_TOP_MMPLL_CK, "mmpll_ck", "mmpll", 1, 1),
	FACTOR(CLK_TOP_MMPLL_D4, "mmpll_d4", "mmpll", 1, 4),
	FACTOR(CLK_TOP_MMPLL_D4_D2, "mmpll_d4_d2", "mmpll_d4", 1, 2),
	FACTOR(CLK_TOP_MMPLL_D4_D4, "mmpll_d4_d4", "mmpll_d4", 1, 4),
	FACTOR(CLK_TOP_MMPLL_D5, "mmpll_d5", "mmpll", 1, 5),
	FACTOR(CLK_TOP_MMPLL_D5_D2, "mmpll_d5_d2", "mmpll_d5", 1, 2),
	FACTOR(CLK_TOP_MMPLL_D5_D4, "mmpll_d5_d4", "mmpll_d5", 1, 4),
	FACTOR(CLK_TOP_MMPLL_D6, "mmpll_d6", "mmpll", 1, 6),
	FACTOR(CLK_TOP_MMPLL_D7, "mmpll_d7", "mmpll", 1, 7),
	FACTOR(CLK_TOP_MFGPLL_CK, "mfgpll_ck", "mfgpll", 1, 1),
	FACTOR(CLK_TOP_MSDCPLL_CK, "msdcpll_ck", "msdcpll", 1, 1),
	FACTOR(CLK_TOP_MSDCPLL_D2, "msdcpll_d2", "msdcpll", 1, 2),
	FACTOR(CLK_TOP_MSDCPLL_D4, "msdcpll_d4", "msdcpll", 1, 4),
	FACTOR(CLK_TOP_MSDCPLL_D8, "msdcpll_d8", "msdcpll", 1, 8),
	FACTOR(CLK_TOP_MSDCPLL_D16, "msdcpll_d16", "msdcpll", 1, 16),
	FACTOR(CLK_TOP_AD_OSC_CK, "ad_osc_ck", "osc", 1, 1),
	FACTOR(CLK_TOP_OSC_D2, "osc_d2", "osc", 1, 2),
	FACTOR(CLK_TOP_OSC_D4, "osc_d4", "osc", 1, 4),
	FACTOR(CLK_TOP_OSC_D8, "osc_d8", "osc", 1, 8),
	FACTOR(CLK_TOP_OSC_D16, "osc_d16", "osc", 1, 16),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL, "univpll", "univ2pll", 1, 2, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D3_D16, "univpll_d3_d16", "univpll_d3", 1, 16, 0),
};

static const char * const axi_parents[] = {
	"clk26m",
	"syspll_d2_d4",
	"syspll_d7",
	"osc_d4"
};

static const char * const mm_parents[] = {
	"clk26m",
	"mmpll_d7",
	"syspll_d3",
	"univpll_d2_d2",
	"syspll_d2_d2",
	"syspll_d3_d2"
};

static const char * const img_parents[] = {
	"clk26m",
	"mmpll_d6",
	"univpll_d3",
	"syspll_d3",
	"univpll_d2_d2",
	"syspll_d2_d2",
	"univpll_d3_d2",
	"syspll_d3_d2"
};

static const char * const cam_parents[] = {
	"clk26m",
	"syspll_d2",
	"mmpll_d6",
	"syspll_d3",
	"mmpll_d7",
	"univpll_d3",
	"univpll_d2_d2",
	"syspll_d2_d2",
	"syspll_d3_d2",
	"univpll_d3_d2"
};

static const char * const dsp_parents[] = {
	"clk26m",
	"mmpll_d6",
	"mmpll_d7",
	"univpll_d3",
	"syspll_d3",
	"univpll_d2_d2",
	"syspll_d2_d2",
	"univpll_d3_d2",
	"syspll_d3_d2"
};

static const char * const dsp1_parents[] = {
	"clk26m",
	"mmpll_d6",
	"mmpll_d7",
	"univpll_d3",
	"syspll_d3",
	"univpll_d2_d2",
	"syspll_d2_d2",
	"univpll_d3_d2",
	"syspll_d3_d2"
};

static const char * const dsp2_parents[] = {
	"clk26m",
	"mmpll_d6",
	"mmpll_d7",
	"univpll_d3",
	"syspll_d3",
	"univpll_d2_d2",
	"syspll_d2_d2",
	"univpll_d3_d2",
	"syspll_d3_d2"
};

static const char * const ipu_if_parents[] = {
	"clk26m",
	"mmpll_d6",
	"mmpll_d7",
	"univpll_d3",
	"syspll_d3",
	"univpll_d2_d2",
	"syspll_d2_d2",
	"univpll_d3_d2",
	"syspll_d3_d2"
};

static const char * const mfg_parents[] = {
	"clk26m",
	"mfgpll_ck",
	"univpll_d3",
	"syspll_d3"
};

static const char * const f52m_mfg_parents[] = {
	"clk26m",
	"univpll_d3_d2",
	"univpll_d3_d4",
	"univpll_d3_d8"
};

static const char * const camtg_parents[] = {
	"clk26m",
	"univ_192m_d8",
	"univpll_d3_d8",
	"univ_192m_d4",
	"univpll_d3_d16",
	"csw_f26m_ck_d2",
	"univ_192m_d16",
	"univ_192m_d32"
};

static const char * const camtg2_parents[] = {
	"clk26m",
	"univ_192m_d8",
	"univpll_d3_d8",
	"univ_192m_d4",
	"univpll_d3_d16",
	"csw_f26m_ck_d2",
	"univ_192m_d16",
	"univ_192m_d32"
};

static const char * const camtg3_parents[] = {
	"clk26m",
	"univ_192m_d8",
	"univpll_d3_d8",
	"univ_192m_d4",
	"univpll_d3_d16",
	"csw_f26m_ck_d2",
	"univ_192m_d16",
	"univ_192m_d32"
};

static const char * const camtg4_parents[] = {
	"clk26m",
	"univ_192m_d8",
	"univpll_d3_d8",
	"univ_192m_d4",
	"univpll_d3_d16",
	"csw_f26m_ck_d2",
	"univ_192m_d16",
	"univ_192m_d32"
};

static const char * const uart_parents[] = {
	"clk26m",
	"univpll_d3_d8"
};

static const char * const spi_parents[] = {
	"clk26m",
	"syspll_d5_d2",
	"syspll_d3_d4",
	"msdcpll_d4"
};

static const char * const msdc50_hclk_parents[] = {
	"clk26m",
	"syspll_d2_d2",
	"syspll_d3_d2"
};

static const char * const msdc50_0_parents[] = {
	"clk26m",
	"msdcpll_ck",
	"msdcpll_d2",
	"univpll_d2_d4",
	"syspll_d3_d2",
	"univpll_d2_d2"
};

static const char * const msdc30_1_parents[] = {
	"clk26m",
	"univpll_d3_d2",
	"syspll_d3_d2",
	"syspll_d7",
	"msdcpll_d2"
};

static const char * const msdc30_2_parents[] = {
	"clk26m",
	"univpll_d3_d2",
	"syspll_d3_d2",
	"syspll_d7",
	"msdcpll_d2"
};

static const char * const audio_parents[] = {
	"clk26m",
	"syspll_d5_d4",
	"syspll_d7_d4",
	"syspll_d2_d16"
};

static const char * const aud_intbus_parents[] = {
	"clk26m",
	"syspll_d2_d4",
	"syspll_d7_d2"
};

static const char * const pmicspi_parents[] = {
	"clk26m",
	"syspll_d2_d8",
	"osc_d8"
};

static const char * const fpwrap_ulposc_parents[] = {
	"clk26m",
	"osc_d16",
	"osc_d4",
	"osc_d8"
};

static const char * const atb_parents[] = {
	"clk26m",
	"syspll_d2_d2",
	"syspll_d5"
};

static const char * const sspm_parents[] = {
	"clk26m",
	"univpll_d2_d4",
	"syspll_d2_d2",
	"univpll_d2_d2",
	"syspll_d3"
};

static const char * const dpi0_parents[] = {
	"clk26m",
	"tvdpll_d2",
	"tvdpll_d4",
	"tvdpll_d8",
	"tvdpll_d16",
	"univpll_d5_d2",
	"univpll_d3_d4",
	"syspll_d3_d4",
	"univpll_d3_d8"
};

static const char * const scam_parents[] = {
	"clk26m",
	"syspll_d5_d2"
};

static const char * const disppwm_parents[] = {
	"clk26m",
	"univpll_d3_d4",
	"osc_d2",
	"osc_d4",
	"osc_d16"
};

static const char * const usb_top_parents[] = {
	"clk26m",
	"univpll_d5_d4",
	"univpll_d3_d4",
	"univpll_d5_d2"
};


static const char * const ssusb_top_xhci_parents[] = {
	"clk26m",
	"univpll_d5_d4",
	"univpll_d3_d4",
	"univpll_d5_d2"
};

static const char * const spm_parents[] = {
	"clk26m",
	"syspll_d2_d8"
};

static const char * const i2c_parents[] = {
	"clk26m",
	"syspll_d2_d8",
	"univpll_d5_d2"
};

static const char * const scp_parents[] = {
	"clk26m",
	"univpll_d2_d8",
	"syspll_d5",
	"syspll_d2_d2",
	"univpll_d2_d2",
	"syspll_d3",
	"univpll_d3"
};

static const char * const seninf_parents[] = {
	"clk26m",
	"univpll_d2_d2",
	"univpll_d3_d2",
	"univpll_d2_d4"
};

static const char * const dxcc_parents[] = {
	"clk26m",
	"syspll_d2_d2",
	"syspll_d2_d4",
	"syspll_d2_d8"
};

static const char * const aud_engen1_parents[] = {
	"clk26m",
	"apll1_d2",
	"apll1_d4",
	"apll1_d8"
};

static const char * const aud_engen2_parents[] = {
	"clk26m",
	"apll2_d2",
	"apll2_d4",
	"apll2_d8"
};

static const char * const faes_ufsfde_parents[] = {
	"clk26m",
	"syspll_d2",
	"syspll_d2_d2",
	"syspll_d3",
	"syspll_d2_d4",
	"univpll_d3"
};

static const char * const fufs_parents[] = {
	"clk26m",
	"syspll_d2_d4",
	"syspll_d2_d8",
	"syspll_d2_d16"
};

static const char * const aud_1_parents[] = {
	"clk26m",
	"apll1_ck"
};

static const char * const aud_2_parents[] = {
	"clk26m",
	"apll2_ck"
};

/*
 * CRITICAL CLOCK:
 * axi_sel is the main bus clock of whole SOC.
 * spm_sel is the clock of the always-on co-processor.
 */
static const struct mtk_mux top_muxes[] = {
	/* CLK_CFG_0 */
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_MUX_AXI, "axi_sel",
		axi_parents, 0x40, 0x44, 0x48, 0, 2, 7, 0x004, 0,
				   CLK_IS_CRITICAL | CLK_SET_RATE_PARENT),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_MM, "mm_sel",
		mm_parents, 0x40, 0x44, 0x48, 8, 3, 15, 0x004, 1),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_IMG, "img_sel",
		img_parents, 0x40, 0x44, 0x48, 16, 3, 23, 0x004, 2),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_CAM, "cam_sel",
		cam_parents, 0x40, 0x44, 0x48, 24, 4, 31, 0x004, 3),
	/* CLK_CFG_1 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_DSP, "dsp_sel",
		dsp_parents, 0x50, 0x54, 0x58, 0, 4, 7, 0x004, 4),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_DSP1, "dsp1_sel",
		dsp1_parents, 0x50, 0x54, 0x58, 8, 4, 15, 0x004, 5),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_DSP2, "dsp2_sel",
		dsp2_parents, 0x50, 0x54, 0x58, 16, 4, 23, 0x004, 6),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_IPU_IF, "ipu_if_sel",
		ipu_if_parents, 0x50, 0x54, 0x58, 24, 4, 31, 0x004, 7),
	/* CLK_CFG_2 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_MFG, "mfg_sel",
		mfg_parents, 0x60, 0x64, 0x68, 0, 2, 7, 0x004, 8),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_F52M_MFG, "f52m_mfg_sel",
		f52m_mfg_parents, 0x60, 0x64, 0x68, 8, 2, 15, 0x004, 9),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_CAMTG, "camtg_sel",
		camtg_parents, 0x60, 0x64, 0x68, 16, 3, 23, 0x004, 10),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_CAMTG2, "camtg2_sel",
		camtg2_parents, 0x60, 0x64, 0x68, 24, 3, 31, 0x004, 11),
	/* CLK_CFG_3 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_CAMTG3, "camtg3_sel",
		camtg3_parents, 0x70, 0x74, 0x78, 0, 3, 7, 0x004, 12),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_CAMTG4, "camtg4_sel",
		camtg4_parents, 0x70, 0x74, 0x78, 8, 3, 15, 0x004, 13),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_UART, "uart_sel",
		uart_parents, 0x70, 0x74, 0x78, 16, 1, 23, 0x004, 14),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_SPI, "spi_sel",
		spi_parents, 0x70, 0x74, 0x78, 24, 2, 31, 0x004, 15),
	/* CLK_CFG_4 */
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_MUX_MSDC50_0_HCLK, "msdc50_hclk_sel",
		msdc50_hclk_parents, 0x80, 0x84, 0x88, 0, 2, 7, 0x004, 16, 0),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_MUX_MSDC50_0, "msdc50_0_sel",
		msdc50_0_parents, 0x80, 0x84, 0x88, 8, 3, 15, 0x004, 17, 0),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_MUX_MSDC30_1, "msdc30_1_sel",
		msdc30_1_parents, 0x80, 0x84, 0x88, 16, 3, 23, 0x004, 18, 0),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_MUX_MSDC30_2, "msdc30_2_sel",
		msdc30_2_parents, 0x80, 0x84, 0x88, 24, 3, 31, 0x004, 19, 0),
	/* CLK_CFG_5 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_AUDIO, "audio_sel",
		audio_parents, 0x90, 0x94, 0x98, 0, 2, 7, 0x004, 20),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_AUD_INTBUS, "aud_intbus_sel",
		aud_intbus_parents, 0x90, 0x94, 0x98, 8, 2, 15, 0x004, 21),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_PMICSPI, "pmicspi_sel",
		pmicspi_parents, 0x90, 0x94, 0x98, 16, 2, 23, 0x004, 22),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_FPWRAP_ULPOSC, "fpwrap_ulposc_sel",
		fpwrap_ulposc_parents, 0x90, 0x94, 0x98, 24, 2, 31, 0x004, 23),
	/* CLK_CFG_6 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_ATB, "atb_sel",
		atb_parents, 0xa0, 0xa4, 0xa8, 0, 2, 7, 0x004, 24),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_MUX_SSPM, "sspm_sel",
				   sspm_parents, 0xa0, 0xa4, 0xa8, 8, 3, 15, 0x004, 25,
				   CLK_IS_CRITICAL | CLK_SET_RATE_PARENT),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_DPI0, "dpi0_sel",
		dpi0_parents, 0xa0, 0xa4, 0xa8, 16, 4, 23, 0x004, 26),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_SCAM, "scam_sel",
		scam_parents, 0xa0, 0xa4, 0xa8, 24, 1, 31, 0x004, 27),
	/* CLK_CFG_7 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_DISP_PWM, "disppwm_sel",
		disppwm_parents, 0xb0, 0xb4, 0xb8, 0, 3, 7, 0x004, 28),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_USB_TOP, "usb_top_sel",
		usb_top_parents, 0xb0, 0xb4, 0xb8, 8, 2, 15, 0x004, 29),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_SSUSB_TOP_XHCI, "ssusb_top_xhci_sel",
		ssusb_top_xhci_parents, 0xb0, 0xb4, 0xb8, 16, 2, 23, 0x004, 30),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_MUX_SPM, "spm_sel",
		spm_parents, 0xb0, 0xb4, 0xb8, 24, 1, 31, 0x008, 0,
				   CLK_IS_CRITICAL | CLK_SET_RATE_PARENT),
	/* CLK_CFG_8 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_I2C, "i2c_sel",
		i2c_parents, 0xc0, 0xc4, 0xc8, 0, 2, 7, 0x008, 1),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_SCP, "scp_sel",
		scp_parents, 0xc0, 0xc4, 0xc8, 8, 3, 15, 0x008, 2),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_SENINF, "seninf_sel",
		seninf_parents, 0xc0, 0xc4, 0xc8, 16, 2, 23, 0x008, 3),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_DXCC, "dxcc_sel",
		dxcc_parents, 0xc0, 0xc4, 0xc8, 24, 2, 31, 0x008, 4),
	/* CLK_CFG_9 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_AUD_ENG1, "aud_eng1_sel",
		aud_engen1_parents, 0xd0, 0xd4, 0xd8, 0, 2, 7, 0x008, 5),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_AUD_ENG2, "aud_eng2_sel",
		aud_engen2_parents, 0xd0, 0xd4, 0xd8, 8, 2, 15, 0x008, 6),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_FAES_UFSFDE, "faes_ufsfde_sel",
		faes_ufsfde_parents, 0xd0, 0xd4, 0xd8, 16, 3, 23, 0x008, 7),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_FUFS, "fufs_sel",
		fufs_parents, 0xd0, 0xd4, 0xd8, 24, 2, 31, 0x008, 8),
	/* CLK_CFG_10 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_AUD_1, "aud_1_sel",
		aud_1_parents, 0xe0, 0xe4, 0xe8, 0, 1, 7, 0x008, 9),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MUX_AUD_2, "aud_2_sel",
		aud_2_parents, 0xe0, 0xe4, 0xe8, 8, 1, 15, 0x008, 10),
};

static const char * const apll_i2s0_parents[] = {
	"aud_1_sel",
	"aud_2_sel"
};

static const char * const apll_i2s1_parents[] = {
	"aud_1_sel",
	"aud_2_sel"
};

static const char * const apll_i2s2_parents[] = {
	"aud_1_sel",
	"aud_2_sel"
};

static const char * const apll_i2s3_parents[] = {
	"aud_1_sel",
	"aud_2_sel"
};

static const char * const apll_i2s4_parents[] = {
	"aud_1_sel",
	"aud_2_sel"
};

static const char * const apll_i2s5_parents[] = {
	"aud_1_sel",
	"aud_2_sel"
};

static const char * const mcu_mp0_parents[] = {
	"clk26m",
	"armpll_ll",
	"armpll_div_pll1",
	"armpll_div_pll2"
};

static const char * const mcu_mp2_parents[] = {
	"clk26m",
	"armpll_l",
	"armpll_div_pll1",
	"armpll_div_pll2"
};

static const char * const mcu_bus_parents[] = {
	"clk26m",
	"ccipll",
	"armpll_div_pll1",
	"armpll_div_pll2"
};

static struct mtk_composite mcu_muxes[] = {
	/* mp0_pll_divider_cfg */
	MUX(CLK_MCU_MP0_SEL, "mcu_mp0_sel", mcu_mp0_parents, 0x7A0, 9, 2),
	/* mp2_pll_divider_cfg */
	MUX(CLK_MCU_MP2_SEL, "mcu_mp2_sel", mcu_mp2_parents, 0x7A8, 9, 2),
	/* bus_pll_divider_cfg */
	MUX(CLK_MCU_BUS_SEL, "mcu_bus_sel", mcu_bus_parents, 0x7C0, 9, 2),
};

static struct mtk_composite top_aud_comp[] = {
	MUX(CLK_TOP_MUX_APLL_I2S0, "apll_i2s0_sel", apll_i2s0_parents, 0x320, 8, 1),
	MUX(CLK_TOP_MUX_APLL_I2S1, "apll_i2s1_sel", apll_i2s1_parents, 0x320, 9, 1),
	MUX(CLK_TOP_MUX_APLL_I2S2, "apll_i2s2_sel", apll_i2s2_parents, 0x320, 10, 1),
	MUX(CLK_TOP_MUX_APLL_I2S3, "apll_i2s3_sel", apll_i2s3_parents, 0x320, 11, 1),
	MUX(CLK_TOP_MUX_APLL_I2S4, "apll_i2s4_sel", apll_i2s4_parents, 0x320, 12, 1),
	MUX(CLK_TOP_MUX_APLL_I2S5, "apll_i2s5_sel", apll_i2s5_parents, 0x328, 20, 1),
	DIV_GATE(CLK_TOP_APLL12_DIV0, "apll12_div0", "apll_i2s0_sel", 0x320, 2, 0x324, 8, 0),
	DIV_GATE(CLK_TOP_APLL12_DIV1, "apll12_div1", "apll_i2s1_sel", 0x320, 3, 0x324, 8, 8),
	DIV_GATE(CLK_TOP_APLL12_DIV2, "apll12_div2", "apll_i2s2_sel", 0x320, 4, 0x324, 8, 16),
	DIV_GATE(CLK_TOP_APLL12_DIV3, "apll12_div3", "apll_i2s3_sel", 0x320, 5, 0x324, 8, 24),
	DIV_GATE(CLK_TOP_APLL12_DIV4, "apll12_div4", "apll_i2s4_sel", 0x320, 6, 0x328, 8, 0),
	DIV_GATE(CLK_TOP_APLL12_DIVB, "apll12_divb", "apll12_div4", 0x320, 7, 0x328, 8, 8),
};

static const struct mtk_gate_regs top_cg_regs = {
	.set_ofs = 0x104,
	.clr_ofs = 0x104,
	.sta_ofs = 0x104,
};

#define GATE_TOP(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &top_cg_regs, _shift,	\
		&mtk_clk_gate_ops_no_setclr_inv)

static const struct mtk_gate top_clks[] = {
	/* TOP */
	GATE_TOP(CLK_TOP_ARMPLL_DIV_PLL1, "armpll_div_pll1", "mainpll", 4),
	GATE_TOP(CLK_TOP_ARMPLL_DIV_PLL2, "armpll_div_pll2", "univpll", 5),
};

static const struct mtk_gate_regs infra0_cg_regs = {
	.set_ofs = 0x80,
	.clr_ofs = 0x84,
	.sta_ofs = 0x90,
};

static const struct mtk_gate_regs infra1_cg_regs = {
	.set_ofs = 0x88,
	.clr_ofs = 0x8c,
	.sta_ofs = 0x94,
};

static const struct mtk_gate_regs infra2_cg_regs = {
	.set_ofs = 0xa4,
	.clr_ofs = 0xa8,
	.sta_ofs = 0xac,
};

static const struct mtk_gate_regs infra3_cg_regs = {
	.set_ofs = 0xc0,
	.clr_ofs = 0xc4,
	.sta_ofs = 0xc8,
};

#define GATE_INFRA0(_id, _name, _parent, _shift)		\
	GATE_MTK(_id, _name, _parent, &infra0_cg_regs, _shift,	\
		&mtk_clk_gate_ops_setclr)

#define GATE_INFRA1(_id, _name, _parent, _shift)		\
	GATE_MTK(_id, _name, _parent, &infra1_cg_regs, _shift,	\
		&mtk_clk_gate_ops_setclr)

#define GATE_INFRA2(_id, _name, _parent, _shift)		\
	GATE_MTK(_id, _name, _parent, &infra2_cg_regs, _shift,	\
		&mtk_clk_gate_ops_setclr)

#define GATE_INFRA2_FLAGS(_id, _name, _parent, _shift, _flag)	\
	GATE_MTK_FLAGS(_id, _name, _parent, &infra2_cg_regs, 	\
		       _shift, &mtk_clk_gate_ops_setclr, _flag)

#define GATE_INFRA3(_id, _name, _parent, _shift)		\
	GATE_MTK(_id, _name, _parent, &infra3_cg_regs, _shift,	\
		&mtk_clk_gate_ops_setclr)

#define GATE_INFRA3_FLAGS(_id, _name, _parent, _shift, _flag)	\
	GATE_MTK_FLAGS(_id, _name, _parent, &infra3_cg_regs, 	\
		       _shift, &mtk_clk_gate_ops_setclr, _flag)

static const struct mtk_gate infra_clks[] = {
	/* INFRA0 */
	GATE_INFRA0(CLK_INFRA_PMIC_TMR, "infra_pmic_tmr", "axi_sel", 0),
	GATE_INFRA0(CLK_INFRA_PMIC_AP, "infra_pmic_ap", "axi_sel", 1),
	GATE_INFRA0(CLK_INFRA_PMIC_MD, "infra_pmic_md", "axi_sel", 2),
	GATE_INFRA0(CLK_INFRA_PMIC_CONN, "infra_pmic_conn", "axi_sel", 3),
	GATE_INFRA0(CLK_INFRA_SCPSYS, "infra_scp", "scp_sel", 4),
	GATE_INFRA0(CLK_INFRA_SEJ, "infra_sej", "f_f26m_ck", 5),
	GATE_INFRA0(CLK_INFRA_APXGPT, "infra_apxgpt", "axi_sel", 6),
	GATE_INFRA0(CLK_INFRA_ICUSB, "infra_icusb", "axi_sel", 8),
	GATE_INFRA0(CLK_INFRA_GCE, "infra_gce", "axi_sel", 9),
	GATE_INFRA0(CLK_INFRA_THERM, "infra_therm", "axi_sel", 10),
	GATE_INFRA0(CLK_INFRA_I2C0, "infra_i2c0", "i2c_sel", 11),
	GATE_INFRA0(CLK_INFRA_I2C1, "infra_i2c1", "i2c_sel", 12),
	GATE_INFRA0(CLK_INFRA_I2C2, "infra_i2c2", "i2c_sel", 13),
	GATE_INFRA0(CLK_INFRA_I2C3, "infra_i2c3", "i2c_sel", 14),
	GATE_INFRA0(CLK_INFRA_PWM_HCLK, "infra_pwm_hclk", "axi_sel", 15),
	GATE_INFRA0(CLK_INFRA_PWM1, "infra_pwm1", "i2c_sel", 16),
	GATE_INFRA0(CLK_INFRA_PWM2, "infra_pwm2", "i2c_sel", 17),
	GATE_INFRA0(CLK_INFRA_PWM3, "infra_pwm3", "i2c_sel", 18),
	GATE_INFRA0(CLK_INFRA_PWM4, "infra_pwm4", "i2c_sel", 19),
	GATE_INFRA0(CLK_INFRA_PWM, "infra_pwm", "i2c_sel", 21),
	GATE_INFRA0(CLK_INFRA_UART0, "infra_uart0", "uart_sel", 22),
	GATE_INFRA0(CLK_INFRA_UART1, "infra_uart1", "uart_sel", 23),
	GATE_INFRA0(CLK_INFRA_UART2, "infra_uart2", "uart_sel", 24),
	GATE_INFRA0(CLK_INFRA_UART3, "infra_uart3", "uart_sel", 25),
	GATE_INFRA0(CLK_INFRA_GCE_26M, "infra_gce_26m", "axi_sel", 27),
	GATE_INFRA0(CLK_INFRA_CQ_DMA_FPC, "infra_cqdma_fpc", "axi_sel", 28),
	GATE_INFRA0(CLK_INFRA_BTIF, "infra_btif", "axi_sel", 31),
	/* INFRA1 */
	GATE_INFRA1(CLK_INFRA_SPI0, "infra_spi0", "spi_sel", 1),
	GATE_INFRA1(CLK_INFRA_MSDC0, "infra_msdc0", "msdc50_hclk_sel", 2),
	GATE_INFRA1(CLK_INFRA_MSDC1, "infra_msdc1", "axi_sel", 4),
	GATE_INFRA1(CLK_INFRA_MSDC2, "infra_msdc2", "axi_sel", 5),
	GATE_INFRA1(CLK_INFRA_MSDC0_SCK, "infra_msdc0_sck", "msdc50_0_sel", 6),
	GATE_INFRA1(CLK_INFRA_DVFSRC, "infra_dvfsrc", "f_f26m_ck", 7),
	GATE_INFRA1(CLK_INFRA_GCPU, "infra_gcpu", "axi_sel", 8),
	GATE_INFRA1(CLK_INFRA_TRNG, "infra_trng", "axi_sel", 9),
	GATE_INFRA1(CLK_INFRA_AUXADC, "infra_auxadc", "f_f26m_ck", 10),
	GATE_INFRA1(CLK_INFRA_CPUM, "infra_cpum", "axi_sel", 11),
	GATE_INFRA1(CLK_INFRA_CCIF1_AP, "infra_ccif1_ap", "axi_sel", 12),
	GATE_INFRA1(CLK_INFRA_CCIF1_MD, "infra_ccif1_md", "axi_sel", 13),
	GATE_INFRA1(CLK_INFRA_AUXADC_MD, "infra_auxadc_md", "f_f26m_ck", 14),
	GATE_INFRA1(CLK_INFRA_MSDC1_SCK, "infra_msdc1_sck", "msdc30_1_sel", 16),
	GATE_INFRA1(CLK_INFRA_MSDC2_SCK, "infra_msdc2_sck", "msdc30_2_sel", 17),
	GATE_INFRA1(CLK_INFRA_AP_DMA, "infra_apdma", "axi_sel", 18),
	GATE_INFRA1(CLK_INFRA_XIU, "infra_xiu", "axi_sel", 19),
	GATE_INFRA1(CLK_INFRA_DEVICE_APC, "infra_device_apc", "axi_sel", 20),
	GATE_INFRA1(CLK_INFRA_CCIF_AP, "infra_ccif_ap", "axi_sel", 23),
	GATE_INFRA1(CLK_INFRA_DEBUGSYS, "infra_debugsys", "axi_sel", 24),
	GATE_INFRA1(CLK_INFRA_AUDIO, "infra_audio", "axi_sel", 25),
	GATE_INFRA1(CLK_INFRA_CCIF_MD, "infra_ccif_md", "axi_sel", 26),
	GATE_INFRA1(CLK_INFRA_DXCC_SEC_CORE, "infra_dxcc_sec_core", "dxcc_sel", 27),
	GATE_INFRA1(CLK_INFRA_DXCC_AO, "infra_dxcc_ao", "dxcc_sel", 28),
	GATE_INFRA1(CLK_INFRA_DEVMPU_BCLK, "infra_devmpu_bclk", "axi_sel", 30),
	GATE_INFRA1(CLK_INFRA_DRAMC_F26M, "infra_dramc_f26m", "f_f26m_ck", 31),
	/* INFRA2 */
	GATE_INFRA2(CLK_INFRA_IRTX, "infra_irtx", "f_f26m_ck", 0),
	GATE_INFRA2(CLK_INFRA_USB, "infra_usb", "usb_top_sel", 1),
	GATE_INFRA2(CLK_INFRA_DISP_PWM, "infra_disppwm", "axi_sel", 2),
	GATE_INFRA2(CLK_INFRA_CLDMA_BCLK, "infra_cldma_bclk", "axi_sel", 3),
	GATE_INFRA2(CLK_INFRA_AUDIO_26M_BCLK, "infra_audio_26m_bclk", "f_f26m_ck", 4),
	GATE_INFRA2(CLK_INFRA_SPI1, "infra_spi1", "spi_sel", 6),
	GATE_INFRA2(CLK_INFRA_I2C4, "infra_i2c4", "i2c_sel", 7),
	GATE_INFRA2(CLK_INFRA_MODEM_TEMP_SHARE, "infra_md_tmp_share", "f_f26m_ck", 8),
	GATE_INFRA2(CLK_INFRA_SPI2, "infra_spi2", "spi_sel", 9),
	GATE_INFRA2(CLK_INFRA_SPI3, "infra_spi3", "spi_sel", 10),
	GATE_INFRA2(CLK_INFRA_UNIPRO_SCK, "infra_unipro_sck", "ssusb_top_xhci_sel", 11),
	GATE_INFRA2(CLK_INFRA_UNIPRO_TICK, "infra_unipro_tick", "fufs_sel", 12),
	GATE_INFRA2(CLK_INFRA_UFS_MP_SAP_BCLK, "infra_ufs_mp_sap_bck", "fufs_sel", 13),
	GATE_INFRA2(CLK_INFRA_MD32_BCLK, "infra_md32_bclk", "axi_sel", 14),
	/* infra_sspm is main clock in co-processor, should not be closed in Linux. */
	GATE_INFRA2_FLAGS(CLK_INFRA_SSPM, "infra_sspm", "sspm_sel", 15, CLK_IS_CRITICAL),
	GATE_INFRA2(CLK_INFRA_UNIPRO_MBIST, "infra_unipro_mbist", "axi_sel", 16),
	/* infra_sspm_bus_hclk is main clock in co-processor, should not be closed in Linux. */
	GATE_INFRA2_FLAGS(CLK_INFRA_SSPM_BUS_HCLK, "infra_sspm_bus_hclk", "axi_sel", 17, CLK_IS_CRITICAL),
	GATE_INFRA2(CLK_INFRA_I2C5, "infra_i2c5", "i2c_sel", 18),
	GATE_INFRA2(CLK_INFRA_I2C5_ARBITER, "infra_i2c5_arbiter", "i2c_sel", 19),
	GATE_INFRA2(CLK_INFRA_I2C5_IMM, "infra_i2c5_imm", "i2c_sel", 20),
	GATE_INFRA2(CLK_INFRA_I2C1_ARBITER, "infra_i2c1_arbiter", "i2c_sel", 21),
	GATE_INFRA2(CLK_INFRA_I2C1_IMM, "infra_i2c1_imm", "i2c_sel", 22),
	GATE_INFRA2(CLK_INFRA_I2C2_ARBITER, "infra_i2c2_arbiter", "i2c_sel", 23),
	GATE_INFRA2(CLK_INFRA_I2C2_IMM, "infra_i2c2_imm", "i2c_sel", 24),
	GATE_INFRA2(CLK_INFRA_SPI4, "infra_spi4", "spi_sel", 25),
	GATE_INFRA2(CLK_INFRA_SPI5, "infra_spi5", "spi_sel", 26),
	GATE_INFRA2(CLK_INFRA_CQ_DMA, "infra_cqdma", "axi_sel", 27),
	GATE_INFRA2(CLK_INFRA_UFS, "infra_ufs", "fufs_sel", 28),
	GATE_INFRA2(CLK_INFRA_AES_UFSFDE, "infra_aes_ufsfde", "faes_ufsfde_sel", 29),
	GATE_INFRA2(CLK_INFRA_UFS_TICK, "infra_ufs_tick", "fufs_sel", 30),
	/* INFRA3 */
	GATE_INFRA3(CLK_INFRA_MSDC0_SELF, "infra_msdc0_self", "msdc50_0_sel", 0),
	GATE_INFRA3(CLK_INFRA_MSDC1_SELF, "infra_msdc1_self", "msdc50_0_sel", 1),
	GATE_INFRA3(CLK_INFRA_MSDC2_SELF, "infra_msdc2_self", "msdc50_0_sel", 2),
	/* infra_sspm_26m_self is main clock in co-processor, should not be closed in Linux. */
	GATE_INFRA3_FLAGS(CLK_INFRA_SSPM_26M_SELF, "infra_sspm_26m_self", "f_f26m_ck", 3, CLK_IS_CRITICAL),
	/* infra_sspm_32k_self is main clock in co-processor, should not be closed in Linux. */
	GATE_INFRA3_FLAGS(CLK_INFRA_SSPM_32K_SELF, "infra_sspm_32k_self", "f_f26m_ck", 4, CLK_IS_CRITICAL),
	GATE_INFRA3(CLK_INFRA_UFS_AXI, "infra_ufs_axi", "axi_sel", 5),
	GATE_INFRA3(CLK_INFRA_I2C6, "infra_i2c6", "i2c_sel", 6),
	GATE_INFRA3(CLK_INFRA_AP_MSDC0, "infra_ap_msdc0", "msdc50_hclk_sel", 7),
	GATE_INFRA3(CLK_INFRA_MD_MSDC0, "infra_md_msdc0", "msdc50_hclk_sel", 8),
	GATE_INFRA3(CLK_INFRA_CCIF2_AP, "infra_ccif2_ap", "axi_sel", 16),
	GATE_INFRA3(CLK_INFRA_CCIF2_MD, "infra_ccif2_md", "axi_sel", 17),
	GATE_INFRA3(CLK_INFRA_CCIF3_AP, "infra_ccif3_ap", "axi_sel", 18),
	GATE_INFRA3(CLK_INFRA_CCIF3_MD, "infra_ccif3_md", "axi_sel", 19),
	GATE_INFRA3(CLK_INFRA_SEJ_F13M, "infra_sej_f13m", "f_f26m_ck", 20),
	GATE_INFRA3(CLK_INFRA_AES_BCLK, "infra_aes_bclk", "axi_sel", 21),
	GATE_INFRA3(CLK_INFRA_I2C7, "infra_i2c7", "i2c_sel", 22),
	GATE_INFRA3(CLK_INFRA_I2C8, "infra_i2c8", "i2c_sel", 23),
	GATE_INFRA3(CLK_INFRA_FBIST2FPC, "infra_fbist2fpc", "msdc50_0_sel", 24),
};

static const struct mtk_gate_regs peri_cg_regs = {
	.set_ofs = 0x20c,
	.clr_ofs = 0x20c,
	.sta_ofs = 0x20c,
};

#define GATE_PERI(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &peri_cg_regs, _shift,	\
		&mtk_clk_gate_ops_no_setclr_inv)

static const struct mtk_gate peri_clks[] = {
	GATE_PERI(CLK_PERI_AXI, "peri_axi", "axi_sel", 31),
};

static u16 infra_rst_ofs[] = {
	INFRA_RST0_SET_OFFSET,
	INFRA_RST1_SET_OFFSET,
	INFRA_RST2_SET_OFFSET,
	INFRA_RST3_SET_OFFSET,
};

static const struct mtk_clk_rst_desc clk_rst_desc = {
	.version = MTK_RST_SET_CLR,
	.rst_bank_ofs = infra_rst_ofs,
	.rst_bank_nr = ARRAY_SIZE(infra_rst_ofs),
};

/* Register mux notifier for MFG mux */
static int clk_mt8183_reg_mfg_mux_notifier(struct device *dev, struct clk *clk)
{
	struct mtk_mux_nb *mfg_mux_nb;
	int i;

	mfg_mux_nb = devm_kzalloc(dev, sizeof(*mfg_mux_nb), GFP_KERNEL);
	if (!mfg_mux_nb)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(top_muxes); i++)
		if (top_muxes[i].id == CLK_TOP_MUX_MFG)
			break;
	if (i == ARRAY_SIZE(top_muxes))
		return -EINVAL;

	mfg_mux_nb->ops = top_muxes[i].ops;
	mfg_mux_nb->bypass_index = 0; /* Bypass to 26M crystal */

	return devm_mtk_clk_mux_notifier_register(dev, clk, mfg_mux_nb);
}

static const struct mtk_clk_desc infra_desc = {
	.clks = infra_clks,
	.num_clks = ARRAY_SIZE(infra_clks),
	.rst_desc = &clk_rst_desc,
};

static const struct mtk_clk_desc mcu_desc = {
	.composite_clks = mcu_muxes,
	.num_composite_clks = ARRAY_SIZE(mcu_muxes),
	.clk_lock = &mt8183_clk_lock,
};

static const struct mtk_clk_desc peri_desc = {
	.clks = peri_clks,
	.num_clks = ARRAY_SIZE(peri_clks),
};

static const struct mtk_clk_desc topck_desc = {
	.fixed_clks = top_fixed_clks,
	.num_fixed_clks = ARRAY_SIZE(top_fixed_clks),
	.factor_clks = top_divs,
	.num_factor_clks = ARRAY_SIZE(top_divs),
	.mux_clks = top_muxes,
	.num_mux_clks = ARRAY_SIZE(top_muxes),
	.composite_clks = top_aud_comp,
	.num_composite_clks = ARRAY_SIZE(top_aud_comp),
	.clks = top_clks,
	.num_clks = ARRAY_SIZE(top_clks),
	.clk_lock = &mt8183_clk_lock,
	.clk_notifier_func = clk_mt8183_reg_mfg_mux_notifier,
	.mfg_clk_idx = CLK_TOP_MUX_MFG,
};

static const struct of_device_id of_match_clk_mt8183[] = {
	{ .compatible = "mediatek,mt8183-infracfg", .data = &infra_desc },
	{ .compatible = "mediatek,mt8183-mcucfg", .data = &mcu_desc },
	{ .compatible = "mediatek,mt8183-pericfg", .data = &peri_desc, },
	{ .compatible = "mediatek,mt8183-topckgen", .data = &topck_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8183);

static struct platform_driver clk_mt8183_drv = {
	.probe = mtk_clk_simple_probe,
	.remove_new = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8183",
		.of_match_table = of_match_clk_mt8183,
	},
};
module_platform_driver(clk_mt8183_drv)
MODULE_LICENSE("GPL");
