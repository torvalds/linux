// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2022 MediaTek Inc.
// Author: Chun-Jie Chen <chun-jie.chen@mediatek.com>

#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/mt8186-clk.h>

#include "clk-mtk.h"
#include "clk-mux.h"

static DEFINE_SPINLOCK(mt8186_clk_lock);

static const struct mtk_fixed_clk top_fixed_clks[] = {
	FIXED_CLK(CLK_TOP_ULPOSC1, "ulposc1", NULL, 250000000),
	FIXED_CLK(CLK_TOP_466M_FMEM, "hd_466m_fmem_ck", NULL, 466000000),
	FIXED_CLK(CLK_TOP_MPLL, "mpll", NULL, 208000000),
};

static const struct mtk_fixed_factor top_divs[] = {
	FACTOR(CLK_TOP_MAINPLL_D2, "mainpll_d2", "mainpll", 1, 2),
	FACTOR(CLK_TOP_MAINPLL_D2_D2, "mainpll_d2_d2", "mainpll_d2", 1, 2),
	FACTOR(CLK_TOP_MAINPLL_D2_D4, "mainpll_d2_d4", "mainpll_d2", 1, 4),
	FACTOR(CLK_TOP_MAINPLL_D2_D16, "mainpll_d2_d16", "mainpll_d2", 1, 16),
	FACTOR(CLK_TOP_MAINPLL_D3, "mainpll_d3", "mainpll", 1, 3),
	FACTOR(CLK_TOP_MAINPLL_D3_D2, "mainpll_d3_d2", "mainpll_d3", 1, 2),
	FACTOR(CLK_TOP_MAINPLL_D3_D4, "mainpll_d3_d4", "mainpll_d3", 1, 4),
	FACTOR(CLK_TOP_MAINPLL_D5, "mainpll_d5", "mainpll", 1, 5),
	FACTOR(CLK_TOP_MAINPLL_D5_D2, "mainpll_d5_d2", "mainpll_d5", 1, 2),
	FACTOR(CLK_TOP_MAINPLL_D5_D4, "mainpll_d5_d4", "mainpll_d5", 1, 4),
	FACTOR(CLK_TOP_MAINPLL_D7, "mainpll_d7", "mainpll", 1, 7),
	FACTOR(CLK_TOP_MAINPLL_D7_D2, "mainpll_d7_d2", "mainpll_d7", 1, 2),
	FACTOR(CLK_TOP_MAINPLL_D7_D4, "mainpll_d7_d4", "mainpll_d7", 1, 4),
	FACTOR(CLK_TOP_UNIVPLL, "univpll", "univ2pll", 1, 2),
	FACTOR(CLK_TOP_UNIVPLL_D2, "univpll_d2", "univpll", 1, 2),
	FACTOR(CLK_TOP_UNIVPLL_D2_D2, "univpll_d2_d2", "univpll_d2", 1, 2),
	FACTOR(CLK_TOP_UNIVPLL_D2_D4, "univpll_d2_d4", "univpll_d2", 1, 4),
	FACTOR(CLK_TOP_UNIVPLL_D3, "univpll_d3", "univpll", 1, 3),
	FACTOR(CLK_TOP_UNIVPLL_D3_D2, "univpll_d3_d2", "univpll_d3", 1, 2),
	FACTOR(CLK_TOP_UNIVPLL_D3_D4, "univpll_d3_d4", "univpll_d3", 1, 4),
	FACTOR(CLK_TOP_UNIVPLL_D3_D8, "univpll_d3_d8", "univpll_d3", 1, 8),
	FACTOR(CLK_TOP_UNIVPLL_D3_D32, "univpll_d3_d32", "univpll_d3", 1, 32),
	FACTOR(CLK_TOP_UNIVPLL_D5, "univpll_d5", "univpll", 1, 5),
	FACTOR(CLK_TOP_UNIVPLL_D5_D2, "univpll_d5_d2", "univpll_d5", 1, 2),
	FACTOR(CLK_TOP_UNIVPLL_D5_D4, "univpll_d5_d4", "univpll_d5", 1, 4),
	FACTOR(CLK_TOP_UNIVPLL_D7, "univpll_d7", "univpll", 1, 7),
	FACTOR(CLK_TOP_UNIVPLL_192M, "univpll_192m", "univ2pll", 1, 13),
	FACTOR(CLK_TOP_UNIVPLL_192M_D4, "univpll_192m_d4", "univpll_192m", 1, 4),
	FACTOR(CLK_TOP_UNIVPLL_192M_D8, "univpll_192m_d8", "univpll_192m", 1, 8),
	FACTOR(CLK_TOP_UNIVPLL_192M_D16, "univpll_192m_d16", "univpll_192m", 1, 16),
	FACTOR(CLK_TOP_UNIVPLL_192M_D32, "univpll_192m_d32", "univpll_192m", 1, 32),
	FACTOR(CLK_TOP_APLL1_D2, "apll1_d2", "apll1", 1, 2),
	FACTOR(CLK_TOP_APLL1_D4, "apll1_d4", "apll1", 1, 4),
	FACTOR(CLK_TOP_APLL1_D8, "apll1_d8", "apll1", 1, 8),
	FACTOR(CLK_TOP_APLL2_D2, "apll2_d2", "apll2", 1, 2),
	FACTOR(CLK_TOP_APLL2_D4, "apll2_d4", "apll2", 1, 4),
	FACTOR(CLK_TOP_APLL2_D8, "apll2_d8", "apll2", 1, 8),
	FACTOR(CLK_TOP_MMPLL_D2, "mmpll_d2", "mmpll", 1, 2),
	FACTOR(CLK_TOP_TVDPLL_D2, "tvdpll_d2", "tvdpll", 1, 2),
	FACTOR(CLK_TOP_TVDPLL_D4, "tvdpll_d4", "tvdpll", 1, 4),
	FACTOR(CLK_TOP_TVDPLL_D8, "tvdpll_d8", "tvdpll", 1, 8),
	FACTOR(CLK_TOP_TVDPLL_D16, "tvdpll_d16", "tvdpll", 1, 16),
	FACTOR(CLK_TOP_TVDPLL_D32, "tvdpll_d32", "tvdpll", 1, 32),
	FACTOR(CLK_TOP_MSDCPLL_D2, "msdcpll_d2", "msdcpll", 1, 2),
	FACTOR(CLK_TOP_ULPOSC1_D2, "ulposc1_d2", "ulposc1", 1, 2),
	FACTOR(CLK_TOP_ULPOSC1_D4, "ulposc1_d4", "ulposc1", 1, 4),
	FACTOR(CLK_TOP_ULPOSC1_D8, "ulposc1_d8", "ulposc1", 1, 8),
	FACTOR(CLK_TOP_ULPOSC1_D10, "ulposc1_d10", "ulposc1", 1, 10),
	FACTOR(CLK_TOP_ULPOSC1_D16, "ulposc1_d16", "ulposc1", 1, 16),
	FACTOR(CLK_TOP_ULPOSC1_D32, "ulposc1_d32", "ulposc1", 1, 32),
	FACTOR(CLK_TOP_ADSPPLL_D2, "adsppll_d2", "adsppll", 1, 2),
	FACTOR(CLK_TOP_ADSPPLL_D4, "adsppll_d4", "adsppll", 1, 4),
	FACTOR(CLK_TOP_ADSPPLL_D8, "adsppll_d8", "adsppll", 1, 8),
	FACTOR(CLK_TOP_NNAPLL_D2, "nnapll_d2", "nnapll", 1, 2),
	FACTOR(CLK_TOP_NNAPLL_D4, "nnapll_d4", "nnapll", 1, 4),
	FACTOR(CLK_TOP_NNAPLL_D8, "nnapll_d8", "nnapll", 1, 8),
	FACTOR(CLK_TOP_NNA2PLL_D2, "nna2pll_d2", "nna2pll", 1, 2),
	FACTOR(CLK_TOP_NNA2PLL_D4, "nna2pll_d4", "nna2pll", 1, 4),
	FACTOR(CLK_TOP_NNA2PLL_D8, "nna2pll_d8", "nna2pll", 1, 8),
	FACTOR(CLK_TOP_F_BIST2FPC, "f_bist2fpc_ck", "univpll_d3_d2", 1, 1),
};

static const char * const axi_parents[] = {
	"clk26m",
	"mainpll_d7",
	"mainpll_d2_d4",
	"univpll_d7"
};

static const char * const scp_parents[] = {
	"clk26m",
	"mainpll_d2_d4",
	"mainpll_d5",
	"mainpll_d2_d2",
	"mainpll_d3",
	"univpll_d3"
};

static const char * const mfg_parents[] = {
	"clk26m",
	"mfgpll",
	"mainpll_d3",
	"mainpll_d5"
};

static const char * const camtg_parents[] = {
	"clk26m",
	"univpll_192m_d8",
	"univpll_d3_d8",
	"univpll_192m_d4",
	"univpll_d3_d32",
	"univpll_192m_d16",
	"univpll_192m_d32"
};

static const char * const uart_parents[] = {
	"clk26m",
	"univpll_d3_d8"
};

static const char * const spi_parents[] = {
	"clk26m",
	"mainpll_d5_d4",
	"mainpll_d3_d4",
	"mainpll_d5_d2",
	"mainpll_d2_d4",
	"mainpll_d7",
	"mainpll_d3_d2",
	"mainpll_d5"
};

static const char * const msdc5hclk_parents[] = {
	"clk26m",
	"mainpll_d2_d2",
	"mainpll_d7",
	"mainpll_d3_d2"
};

static const char * const msdc50_0_parents[] = {
	"clk26m",
	"msdcpll",
	"univpll_d3",
	"msdcpll_d2",
	"mainpll_d7",
	"mainpll_d3_d2",
	"univpll_d2_d2"
};

static const char * const msdc30_1_parents[] = {
	"clk26m",
	"msdcpll_d2",
	"univpll_d3_d2",
	"mainpll_d3_d2",
	"mainpll_d7"
};

static const char * const audio_parents[] = {
	"clk26m",
	"mainpll_d5_d4",
	"mainpll_d7_d4",
	"mainpll_d2_d16"
};

static const char * const aud_intbus_parents[] = {
	"clk26m",
	"mainpll_d2_d4",
	"mainpll_d7_d2"
};

static const char * const aud_1_parents[] = {
	"clk26m",
	"apll1"
};

static const char * const aud_2_parents[] = {
	"clk26m",
	"apll2"
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

static const char * const disp_pwm_parents[] = {
	"clk26m",
	"univpll_d5_d2",
	"univpll_d3_d4",
	"ulposc1_d2",
	"ulposc1_d8"
};

static const char * const sspm_parents[] = {
	"clk26m",
	"mainpll_d2_d2",
	"mainpll_d3_d2",
	"mainpll_d5",
	"mainpll_d3"
};

static const char * const dxcc_parents[] = {
	"clk26m",
	"mainpll_d2_d2",
	"mainpll_d2_d4"
};

static const char * const usb_parents[] = {
	"clk26m",
	"univpll_d5_d4",
	"univpll_d5_d2"
};

static const char * const srck_parents[] = {
	"clk32k",
	"clk26m",
	"ulposc1_d10"
};

static const char * const spm_parents[] = {
	"clk32k",
	"ulposc1_d10",
	"clk26m",
	"mainpll_d7_d2"
};

static const char * const i2c_parents[] = {
	"clk26m",
	"univpll_d5_d4",
	"univpll_d3_d4",
	"univpll_d5_d2"
};

static const char * const pwm_parents[] = {
	"clk26m",
	"univpll_d3_d8",
	"univpll_d3_d4",
	"univpll_d2_d4"
};

static const char * const seninf_parents[] = {
	"clk26m",
	"univpll_d2_d4",
	"univpll_d2_d2",
	"univpll_d3_d2"
};

static const char * const aes_msdcfde_parents[] = {
	"clk26m",
	"univpll_d3",
	"mainpll_d3",
	"univpll_d2_d2",
	"mainpll_d2_d2",
	"mainpll_d2_d4"
};

static const char * const pwrap_ulposc_parents[] = {
	"clk26m",
	"univpll_d5_d4",
	"ulposc1_d4",
	"ulposc1_d8",
	"ulposc1_d10",
	"ulposc1_d16",
	"ulposc1_d32"
};

static const char * const camtm_parents[] = {
	"clk26m",
	"univpll_d2_d4",
	"univpll_d3_d2"
};

static const char * const venc_parents[] = {
	"clk26m",
	"mmpll",
	"mainpll_d2_d2",
	"mainpll_d2",
	"univpll_d3",
	"univpll_d2_d2",
	"mainpll_d3",
	"mmpll"
};

static const char * const isp_parents[] = {
	"clk26m",
	"mainpll_d2",
	"mainpll_d2_d2",
	"univpll_d3",
	"mainpll_d3",
	"mmpll",
	"univpll_d5",
	"univpll_d2_d2",
	"mmpll_d2"
};

static const char * const dpmaif_parents[] = {
	"clk26m",
	"univpll_d2_d2",
	"mainpll_d3",
	"mainpll_d2_d2",
	"univpll_d3_d2"
};

static const char * const vdec_parents[] = {
	"clk26m",
	"mainpll_d3",
	"mainpll_d2_d2",
	"univpll_d5",
	"mainpll_d2",
	"univpll_d3",
	"univpll_d2_d2"
};

static const char * const disp_parents[] = {
	"clk26m",
	"univpll_d3_d2",
	"mainpll_d5",
	"univpll_d5",
	"univpll_d2_d2",
	"mainpll_d3",
	"univpll_d3",
	"mainpll_d2",
	"mmpll"
};

static const char * const mdp_parents[] = {
	"clk26m",
	"mainpll_d5",
	"univpll_d5",
	"mainpll_d2_d2",
	"univpll_d2_d2",
	"mainpll_d3",
	"univpll_d3",
	"mainpll_d2",
	"mmpll"
};

static const char * const audio_h_parents[] = {
	"clk26m",
	"univpll_d7",
	"apll1",
	"apll2"
};

static const char * const ufs_parents[] = {
	"clk26m",
	"mainpll_d7",
	"univpll_d2_d4",
	"mainpll_d2_d4"
};

static const char * const aes_fde_parents[] = {
	"clk26m",
	"univpll_d3",
	"mainpll_d2_d2",
	"univpll_d5"
};

static const char * const audiodsp_parents[] = {
	"clk26m",
	"ulposc1_d10",
	"adsppll",
	"adsppll_d2",
	"adsppll_d4",
	"adsppll_d8"
};

static const char * const dvfsrc_parents[] = {
	"clk26m",
	"ulposc1_d10",
};

static const char * const dsi_occ_parents[] = {
	"clk26m",
	"univpll_d3_d2",
	"mpll",
	"mainpll_d5"
};

static const char * const spmi_mst_parents[] = {
	"clk26m",
	"univpll_d5_d4",
	"ulposc1_d4",
	"ulposc1_d8",
	"ulposc1_d10",
	"ulposc1_d16",
	"ulposc1_d32"
};

static const char * const spinor_parents[] = {
	"clk26m",
	"clk13m",
	"mainpll_d7_d4",
	"univpll_d3_d8",
	"univpll_d5_d4",
	"mainpll_d7_d2"
};

static const char * const nna_parents[] = {
	"clk26m",
	"univpll_d3_d8",
	"mainpll_d2_d4",
	"univpll_d3_d2",
	"mainpll_d2_d2",
	"univpll_d2_d2",
	"mainpll_d3",
	"univpll_d3",
	"mmpll",
	"mainpll_d2",
	"univpll_d2",
	"nnapll_d2",
	"nnapll_d4",
	"nnapll_d8",
	"nnapll",
	"nna2pll"
};

static const char * const nna2_parents[] = {
	"clk26m",
	"univpll_d3_d8",
	"mainpll_d2_d4",
	"univpll_d3_d2",
	"mainpll_d2_d2",
	"univpll_d2_d2",
	"mainpll_d3",
	"univpll_d3",
	"mmpll",
	"mainpll_d2",
	"univpll_d2",
	"nna2pll_d2",
	"nna2pll_d4",
	"nna2pll_d8",
	"nnapll",
	"nna2pll"
};

static const char * const ssusb_parents[] = {
	"clk26m",
	"univpll_d5_d4",
	"univpll_d5_d2"
};

static const char * const wpe_parents[] = {
	"clk26m",
	"univpll_d3_d2",
	"mainpll_d5",
	"univpll_d5",
	"univpll_d2_d2",
	"mainpll_d3",
	"univpll_d3",
	"mainpll_d2",
	"mmpll"
};

static const char * const dpi_parents[] = {
	"clk26m",
	"tvdpll",
	"tvdpll_d2",
	"tvdpll_d4",
	"tvdpll_d8",
	"tvdpll_d16",
	"tvdpll_d32"
};

static const char * const u3_occ_250m_parents[] = {
	"clk26m",
	"univpll_d5"
};

static const char * const u3_occ_500m_parents[] = {
	"clk26m",
	"nna2pll_d2"
};

static const char * const adsp_bus_parents[] = {
	"clk26m",
	"ulposc1_d2",
	"mainpll_d5",
	"mainpll_d2_d2",
	"mainpll_d3",
	"mainpll_d2",
	"univpll_d3"
};

static const char * const apll_mck_parents[] = {
	"top_aud_1",
	"top_aud_2"
};

static const struct mtk_mux top_mtk_muxes[] = {
	/*
	 * CLK_CFG_0
	 * top_axi is bus clock, should not be closed by Linux.
	 * top_scp is main clock in always-on co-processor.
	 */
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_AXI, "top_axi", axi_parents,
				   0x0040, 0x0044, 0x0048, 0, 2, 7, 0x0004, 0,
				   CLK_IS_CRITICAL),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_SCP, "top_scp", scp_parents,
				   0x0040, 0x0044, 0x0048, 8, 3, 15, 0x0004, 1,
				   CLK_IS_CRITICAL),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MFG, "top_mfg",
		mfg_parents, 0x0040, 0x0044, 0x0048, 16, 2, 23, 0x0004, 2),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CAMTG, "top_camtg",
		camtg_parents, 0x0040, 0x0044, 0x0048, 24, 3, 31, 0x0004, 3),
	/* CLK_CFG_1 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CAMTG1, "top_camtg1",
		camtg_parents, 0x0050, 0x0054, 0x0058, 0, 3, 7, 0x0004, 4),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CAMTG2, "top_camtg2",
		camtg_parents, 0x0050, 0x0054, 0x0058, 8, 3, 15, 0x0004, 5),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CAMTG3, "top_camtg3",
		camtg_parents, 0x0050, 0x0054, 0x0058, 16, 3, 23, 0x0004, 6),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CAMTG4, "top_camtg4",
		camtg_parents, 0x0050, 0x0054, 0x0058, 24, 3, 31, 0x0004, 7),
	/* CLK_CFG_2 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CAMTG5, "top_camtg5",
		camtg_parents, 0x0060, 0x0064, 0x0068, 0, 3, 7, 0x0004, 8),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CAMTG6, "top_camtg6",
		camtg_parents, 0x0060, 0x0064, 0x0068, 8, 3, 15, 0x0004, 9),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_UART, "top_uart",
		uart_parents, 0x0060, 0x0064, 0x0068, 16, 1, 23, 0x0004, 10),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SPI, "top_spi",
		spi_parents, 0x0060, 0x0064, 0x0068, 24, 3, 31, 0x0004, 11),
	/* CLK_CFG_3 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MSDC50_0_HCLK, "top_msdc5hclk",
		msdc5hclk_parents, 0x0070, 0x0074, 0x0078, 0, 2, 7, 0x0004, 12),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MSDC50_0, "top_msdc50_0",
		msdc50_0_parents, 0x0070, 0x0074, 0x0078, 8, 3, 15, 0x0004, 13),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MSDC30_1, "top_msdc30_1",
		msdc30_1_parents, 0x0070, 0x0074, 0x0078, 16, 3, 23, 0x0004, 14),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUDIO, "top_audio",
		audio_parents, 0x0070, 0x0074, 0x0078, 24, 2, 31, 0x0004, 15),
	/* CLK_CFG_4 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUD_INTBUS, "top_aud_intbus",
		aud_intbus_parents, 0x0080, 0x0084, 0x0088, 0, 2, 7, 0x0004, 16),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUD_1, "top_aud_1",
		aud_1_parents, 0x0080, 0x0084, 0x0088, 8, 1, 15, 0x0004, 17),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUD_2, "top_aud_2",
		aud_2_parents, 0x0080, 0x0084, 0x0088, 16, 1, 23, 0x0004, 18),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUD_ENGEN1, "top_aud_engen1",
		aud_engen1_parents, 0x0080, 0x0084, 0x0088, 24, 2, 31, 0x0004, 19),
	/*
	 * CLK_CFG_5
	 * top_sspm is main clock in always-on co-processor, should not be closed
	 * in Linux.
	 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUD_ENGEN2, "top_aud_engen2",
		aud_engen2_parents, 0x0090, 0x0094, 0x0098, 0, 2, 7, 0x0004, 20),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DISP_PWM, "top_disp_pwm",
		disp_pwm_parents, 0x0090, 0x0094, 0x0098, 8, 3, 15, 0x0004, 21),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_SSPM, "top_sspm", sspm_parents,
				   0x0090, 0x0094, 0x0098, 16, 3, 23, 0x0004, 22,
				   CLK_IS_CRITICAL),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DXCC, "top_dxcc",
		dxcc_parents, 0x0090, 0x0094, 0x0098, 24, 2, 31, 0x0004, 23),
	/*
	 * CLK_CFG_6
	 * top_spm and top_srck are main clocks in always-on co-processor.
	 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_USB_TOP, "top_usb",
		usb_parents, 0x00a0, 0x00a4, 0x00a8, 0, 2, 7, 0x0004, 24),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_SRCK, "top_srck", srck_parents,
				   0x00a0, 0x00a4, 0x00a8, 8, 2, 15, 0x0004, 25,
				   CLK_IS_CRITICAL),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_SPM, "top_spm", spm_parents,
				   0x00a0, 0x00a4, 0x00a8, 16, 2, 23, 0x0004, 26,
				   CLK_IS_CRITICAL),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_I2C, "top_i2c",
		i2c_parents, 0x00a0, 0x00a4, 0x00a8, 24, 2, 31, 0x0004, 27),
	/* CLK_CFG_7 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_PWM, "top_pwm",
		pwm_parents, 0x00b0, 0x00b4, 0x00b8, 0, 2, 7, 0x0004, 28),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SENINF, "top_seninf",
		seninf_parents, 0x00b0, 0x00b4, 0x00b8, 8, 2, 15, 0x0004, 29),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SENINF1, "top_seninf1",
		seninf_parents, 0x00b0, 0x00b4, 0x00b8, 16, 2, 23, 0x0004, 30),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SENINF2, "top_seninf2",
		seninf_parents, 0x00b0, 0x00b4, 0x00b8, 24, 2, 31, 0x0008, 0),
	/* CLK_CFG_8 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SENINF3, "top_seninf3",
		seninf_parents, 0x00c0, 0x00c4, 0x00c8, 0, 2, 7, 0x0008, 1),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AES_MSDCFDE, "top_aes_msdcfde",
		aes_msdcfde_parents, 0x00c0, 0x00c4, 0x00c8, 8, 3, 15, 0x0008, 2),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_PWRAP_ULPOSC, "top_pwrap_ulposc",
		pwrap_ulposc_parents, 0x00c0, 0x00c4, 0x00c8, 16, 3, 23, 0x0008, 3),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CAMTM, "top_camtm",
		camtm_parents, 0x00c0, 0x00c4, 0x00c8, 24, 2, 31, 0x0008, 4),
	/* CLK_CFG_9 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_VENC, "top_venc",
		venc_parents, 0x00d0, 0x00d4, 0x00d8, 0, 3, 7, 0x0008, 5),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CAM, "top_cam",
		isp_parents, 0x00d0, 0x00d4, 0x00d8, 8, 4, 15, 0x0008, 6),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_IMG1, "top_img1",
		isp_parents, 0x00d0, 0x00d4, 0x00d8, 16, 4, 23, 0x0008, 7),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_IPE, "top_ipe",
		isp_parents, 0x00d0, 0x00d4, 0x00d8, 24, 4, 31, 0x0008, 8),
	/* CLK_CFG_10 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DPMAIF, "top_dpmaif",
		dpmaif_parents, 0x00e0, 0x00e4, 0x00e8, 0, 3, 7, 0x0008, 9),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_VDEC, "top_vdec",
		vdec_parents, 0x00e0, 0x00e4, 0x00e8, 8, 3, 15, 0x0008, 10),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DISP, "top_disp",
		disp_parents, 0x00e0, 0x00e4, 0x00e8, 16, 4, 23, 0x0008, 11),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MDP, "top_mdp",
		mdp_parents, 0x00e0, 0x00e4, 0x00e8, 24, 4, 31, 0x0008, 12),
	/* CLK_CFG_11 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUDIO_H, "top_audio_h",
		audio_h_parents, 0x00ec, 0x00f0, 0x00f4, 0, 2, 7, 0x0008, 13),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_UFS, "top_ufs",
		ufs_parents, 0x00ec, 0x00f0, 0x00f4, 8, 2, 15, 0x0008, 14),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AES_FDE, "top_aes_fde",
		aes_fde_parents, 0x00ec, 0x00f0, 0x00f4, 16, 2, 23, 0x0008, 15),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUDIODSP, "top_audiodsp",
		audiodsp_parents, 0x00ec, 0x00f0, 0x00f4, 24, 3, 31, 0x0008, 16),
	/*
	 * CLK_CFG_12
	 * dvfsrc is for internal DVFS usage, should not be closed in Linux.
	 */
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_DVFSRC, "top_dvfsrc", dvfsrc_parents,
				   0x0100, 0x0104, 0x0108, 0, 1, 7, 0x0008, 17,
				   CLK_IS_CRITICAL),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DSI_OCC, "top_dsi_occ",
		dsi_occ_parents, 0x0100, 0x0104, 0x0108, 8, 2, 15, 0x0008, 18),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SPMI_MST, "top_spmi_mst",
		spmi_mst_parents, 0x0100, 0x0104, 0x0108, 16, 3, 23, 0x0008, 19),
	/* CLK_CFG_13 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SPINOR, "top_spinor",
		spinor_parents, 0x0110, 0x0114, 0x0118, 0, 3, 6, 0x0008, 20),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_NNA, "top_nna",
		nna_parents, 0x0110, 0x0114, 0x0118, 7, 4, 14, 0x0008, 21),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_NNA1, "top_nna1",
		nna_parents, 0x0110, 0x0114, 0x0118, 15, 4, 22, 0x0008, 22),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_NNA2, "top_nna2",
		nna2_parents, 0x0110, 0x0114, 0x0118, 23, 4, 30, 0x0008, 23),
	/* CLK_CFG_14 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SSUSB_XHCI, "top_ssusb_xhci",
		ssusb_parents, 0x0120, 0x0124, 0x0128, 0, 2, 5, 0x0008, 24),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SSUSB_TOP_1P, "top_ssusb_1p",
		ssusb_parents, 0x0120, 0x0124, 0x0128, 6, 2, 11, 0x0008, 25),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SSUSB_XHCI_1P, "top_ssusb_xhci_1p",
		ssusb_parents, 0x0120, 0x0124, 0x0128, 12, 2, 17, 0x0008, 26),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_WPE, "top_wpe",
		wpe_parents, 0x0120, 0x0124, 0x0128, 18, 4, 25, 0x0008, 27),
	/* CLK_CFG_15 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DPI, "top_dpi",
		dpi_parents, 0x0180, 0x0184, 0x0188, 0, 3, 6, 0x0008, 28),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_U3_OCC_250M, "top_u3_occ_250m",
		u3_occ_250m_parents, 0x0180, 0x0184, 0x0188, 7, 1, 11, 0x0008, 29),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_U3_OCC_500M, "top_u3_occ_500m",
		u3_occ_500m_parents, 0x0180, 0x0184, 0x0188, 12, 1, 16, 0x0008, 30),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_ADSP_BUS, "top_adsp_bus",
		adsp_bus_parents, 0x0180, 0x0184, 0x0188, 17, 3, 23, 0x0008, 31),
};

static struct mtk_composite top_muxes[] = {
	/* CLK_AUDDIV_0 */
	MUX(CLK_TOP_APLL_I2S0_MCK_SEL, "apll_i2s0_mck_sel", apll_mck_parents, 0x0320, 16, 1),
	MUX(CLK_TOP_APLL_I2S1_MCK_SEL, "apll_i2s1_mck_sel", apll_mck_parents, 0x0320, 17, 1),
	MUX(CLK_TOP_APLL_I2S2_MCK_SEL, "apll_i2s2_mck_sel", apll_mck_parents, 0x0320, 18, 1),
	MUX(CLK_TOP_APLL_I2S4_MCK_SEL, "apll_i2s4_mck_sel", apll_mck_parents, 0x0320, 19, 1),
	MUX(CLK_TOP_APLL_TDMOUT_MCK_SEL, "apll_tdmout_mck_sel", apll_mck_parents,
		0x0320, 20, 1),
};

static const struct mtk_composite top_adj_divs[] = {
	DIV_GATE(CLK_TOP_APLL12_CK_DIV0, "apll12_div0", "apll_i2s0_mck_sel",
			0x0320, 0, 0x0328, 8, 0),
	DIV_GATE(CLK_TOP_APLL12_CK_DIV1, "apll12_div1", "apll_i2s1_mck_sel",
			0x0320, 1, 0x0328, 8, 8),
	DIV_GATE(CLK_TOP_APLL12_CK_DIV2, "apll12_div2", "apll_i2s2_mck_sel",
			0x0320, 2, 0x0328, 8, 16),
	DIV_GATE(CLK_TOP_APLL12_CK_DIV4, "apll12_div4", "apll_i2s4_mck_sel",
			0x0320, 3, 0x0328, 8, 24),
	DIV_GATE(CLK_TOP_APLL12_CK_DIV_TDMOUT_M, "apll12_div_tdmout_m", "apll_tdmout_mck_sel",
			0x0320, 4, 0x0334, 8, 0),
};

static const struct of_device_id of_match_clk_mt8186_topck[] = {
	{ .compatible = "mediatek,mt8186-topckgen", },
	{}
};

static int clk_mt8186_topck_probe(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data;
	struct device_node *node = pdev->dev.of_node;
	int r;
	void __iomem *base;

	clk_data = mtk_alloc_clk_data(CLK_TOP_NR_CLK);
	if (!clk_data)
		return -ENOMEM;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base)) {
		r = PTR_ERR(base);
		goto free_top_data;
	}

	r = mtk_clk_register_fixed_clks(top_fixed_clks, ARRAY_SIZE(top_fixed_clks),
					clk_data);
	if (r)
		goto free_top_data;

	r = mtk_clk_register_factors(top_divs, ARRAY_SIZE(top_divs), clk_data);
	if (r)
		goto unregister_fixed_clks;

	r = mtk_clk_register_muxes(&pdev->dev, top_mtk_muxes,
				   ARRAY_SIZE(top_mtk_muxes), node,
				   &mt8186_clk_lock, clk_data);
	if (r)
		goto unregister_factors;

	r = mtk_clk_register_composites(&pdev->dev, top_muxes,
					ARRAY_SIZE(top_muxes), base,
					&mt8186_clk_lock, clk_data);
	if (r)
		goto unregister_muxes;

	r = mtk_clk_register_composites(&pdev->dev, top_adj_divs,
					ARRAY_SIZE(top_adj_divs), base,
					&mt8186_clk_lock, clk_data);
	if (r)
		goto unregister_composite_muxes;

	r = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	if (r)
		goto unregister_composite_divs;

	platform_set_drvdata(pdev, clk_data);

	return r;

unregister_composite_divs:
	mtk_clk_unregister_composites(top_adj_divs, ARRAY_SIZE(top_adj_divs), clk_data);
unregister_composite_muxes:
	mtk_clk_unregister_composites(top_muxes, ARRAY_SIZE(top_muxes), clk_data);
unregister_muxes:
	mtk_clk_unregister_muxes(top_mtk_muxes, ARRAY_SIZE(top_mtk_muxes), clk_data);
unregister_factors:
	mtk_clk_unregister_factors(top_divs, ARRAY_SIZE(top_divs), clk_data);
unregister_fixed_clks:
	mtk_clk_unregister_fixed_clks(top_fixed_clks, ARRAY_SIZE(top_fixed_clks), clk_data);
free_top_data:
	mtk_free_clk_data(clk_data);
	return r;
}

static int clk_mt8186_topck_remove(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data = platform_get_drvdata(pdev);
	struct device_node *node = pdev->dev.of_node;

	of_clk_del_provider(node);
	mtk_clk_unregister_composites(top_adj_divs, ARRAY_SIZE(top_adj_divs), clk_data);
	mtk_clk_unregister_composites(top_muxes, ARRAY_SIZE(top_muxes), clk_data);
	mtk_clk_unregister_muxes(top_mtk_muxes, ARRAY_SIZE(top_mtk_muxes), clk_data);
	mtk_clk_unregister_factors(top_divs, ARRAY_SIZE(top_divs), clk_data);
	mtk_clk_unregister_fixed_clks(top_fixed_clks, ARRAY_SIZE(top_fixed_clks), clk_data);
	mtk_free_clk_data(clk_data);

	return 0;
}

static struct platform_driver clk_mt8186_topck_drv = {
	.probe = clk_mt8186_topck_probe,
	.remove = clk_mt8186_topck_remove,
	.driver = {
		.name = "clk-mt8186-topck",
		.of_match_table = of_match_clk_mt8186_topck,
	},
};
module_platform_driver(clk_mt8186_topck_drv);
MODULE_LICENSE("GPL");
