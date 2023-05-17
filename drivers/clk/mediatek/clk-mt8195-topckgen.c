// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2021 MediaTek Inc.
// Author: Chun-Jie Chen <chun-jie.chen@mediatek.com>

#include "clk-gate.h"
#include "clk-mtk.h"
#include "clk-mux.h"

#include <dt-bindings/clock/mt8195-clk.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

static DEFINE_SPINLOCK(mt8195_clk_lock);

static const struct mtk_fixed_clk top_fixed_clks[] = {
	FIXED_CLK(CLK_TOP_IN_DGI, "in_dgi", NULL, 165000000),
	FIXED_CLK(CLK_TOP_ULPOSC1, "ulposc1", NULL, 248000000),
	FIXED_CLK(CLK_TOP_ULPOSC2, "ulposc2", NULL, 326000000),
	FIXED_CLK(CLK_TOP_MEM_466M, "mem_466m", NULL, 533000000),
	FIXED_CLK(CLK_TOP_MPHONE_SLAVE_B, "mphone_slave_b", NULL, 49152000),
	FIXED_CLK(CLK_TOP_PEXTP_PIPE, "pextp_pipe", NULL, 250000000),
	FIXED_CLK(CLK_TOP_UFS_RX_SYMBOL, "ufs_rx_symbol", NULL, 166000000),
	FIXED_CLK(CLK_TOP_UFS_TX_SYMBOL, "ufs_tx_symbol", NULL, 166000000),
	FIXED_CLK(CLK_TOP_SSUSB_U3PHY_P1_P_P0, "ssusb_u3phy_p1_p_p0", NULL, 131000000),
	FIXED_CLK(CLK_TOP_UFS_RX_SYMBOL1, "ufs_rx_symbol1", NULL, 166000000),
	FIXED_CLK(CLK_TOP_FPC, "fpc", NULL, 50000000),
	FIXED_CLK(CLK_TOP_HDMIRX_P, "hdmirx_p", NULL, 594000000),
};

static const struct mtk_fixed_factor top_divs[] = {
	FACTOR(CLK_TOP_CLK26M_D2, "clk26m_d2", "clk26m", 1, 2),
	FACTOR(CLK_TOP_CLK26M_D52, "clk26m_d52", "clk26m", 1, 52),
	FACTOR(CLK_TOP_IN_DGI_D2, "in_dgi_d2", "in_dgi", 1, 2),
	FACTOR(CLK_TOP_IN_DGI_D4, "in_dgi_d4", "in_dgi", 1, 4),
	FACTOR(CLK_TOP_IN_DGI_D6, "in_dgi_d6", "in_dgi", 1, 6),
	FACTOR(CLK_TOP_IN_DGI_D8, "in_dgi_d8", "in_dgi", 1, 8),
	FACTOR_FLAGS(CLK_TOP_MAINPLL_D3, "mainpll_d3", "mainpll", 1, 3, 0),
	FACTOR_FLAGS(CLK_TOP_MAINPLL_D4, "mainpll_d4", "mainpll", 1, 4, 0),
	FACTOR_FLAGS(CLK_TOP_MAINPLL_D4_D2, "mainpll_d4_d2", "mainpll_d4", 1, 2, 0),
	FACTOR_FLAGS(CLK_TOP_MAINPLL_D4_D4, "mainpll_d4_d4", "mainpll_d4", 1, 4, 0),
	FACTOR_FLAGS(CLK_TOP_MAINPLL_D4_D8, "mainpll_d4_d8", "mainpll_d4", 1, 8, 0),
	FACTOR_FLAGS(CLK_TOP_MAINPLL_D5, "mainpll_d5", "mainpll", 1, 5, 0),
	FACTOR_FLAGS(CLK_TOP_MAINPLL_D5_D2, "mainpll_d5_d2", "mainpll_d5", 1, 2, 0),
	FACTOR_FLAGS(CLK_TOP_MAINPLL_D5_D4, "mainpll_d5_d4", "mainpll_d5", 1, 4, 0),
	FACTOR_FLAGS(CLK_TOP_MAINPLL_D5_D8, "mainpll_d5_d8", "mainpll_d5", 1, 8, 0),
	FACTOR_FLAGS(CLK_TOP_MAINPLL_D6, "mainpll_d6", "mainpll", 1, 6, 0),
	FACTOR_FLAGS(CLK_TOP_MAINPLL_D6_D2, "mainpll_d6_d2", "mainpll_d6", 1, 2, 0),
	FACTOR_FLAGS(CLK_TOP_MAINPLL_D6_D4, "mainpll_d6_d4", "mainpll_d6", 1, 4, 0),
	FACTOR_FLAGS(CLK_TOP_MAINPLL_D6_D8, "mainpll_d6_d8", "mainpll_d6", 1, 8, 0),
	FACTOR_FLAGS(CLK_TOP_MAINPLL_D7, "mainpll_d7", "mainpll", 1, 7, 0),
	FACTOR_FLAGS(CLK_TOP_MAINPLL_D7_D2, "mainpll_d7_d2", "mainpll_d7", 1, 2, 0),
	FACTOR_FLAGS(CLK_TOP_MAINPLL_D7_D4, "mainpll_d7_d4", "mainpll_d7", 1, 4, 0),
	FACTOR_FLAGS(CLK_TOP_MAINPLL_D7_D8, "mainpll_d7_d8", "mainpll_d7", 1, 8, 0),
	FACTOR_FLAGS(CLK_TOP_MAINPLL_D9, "mainpll_d9", "mainpll", 1, 9, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D2, "univpll_d2", "univpll", 1, 2, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D3, "univpll_d3", "univpll", 1, 3, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D4, "univpll_d4", "univpll", 1, 4, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D4_D2, "univpll_d4_d2", "univpll_d4", 1, 2, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D4_D4, "univpll_d4_d4", "univpll_d4", 1, 4, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D4_D8, "univpll_d4_d8", "univpll_d4", 1, 8, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D5, "univpll_d5", "univpll", 1, 5, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D5_D2, "univpll_d5_d2", "univpll_d5", 1, 2, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D5_D4, "univpll_d5_d4", "univpll_d5", 1, 4, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D5_D8, "univpll_d5_d8", "univpll_d5", 1, 8, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D6, "univpll_d6", "univpll", 1, 6, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D6_D2, "univpll_d6_d2", "univpll_d6", 1, 2, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D6_D4, "univpll_d6_d4", "univpll_d6", 1, 4, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D6_D8, "univpll_d6_d8", "univpll_d6", 1, 8, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D6_D16, "univpll_d6_d16", "univpll_d6", 1, 16, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D7, "univpll_d7", "univpll", 1, 7, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_192M, "univpll_192m", "univpll", 1, 13, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_192M_D4, "univpll_192m_d4", "univpll_192m", 1, 4, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_192M_D8, "univpll_192m_d8", "univpll_192m", 1, 8, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_192M_D16, "univpll_192m_d16", "univpll_192m", 1, 16, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_192M_D32, "univpll_192m_d32", "univpll_192m", 1, 32, 0),
	FACTOR(CLK_TOP_APLL1_D3, "apll1_d3", "apll1", 1, 3),
	FACTOR(CLK_TOP_APLL1_D4, "apll1_d4", "apll1", 1, 4),
	FACTOR(CLK_TOP_APLL2_D3, "apll2_d3", "apll2", 1, 3),
	FACTOR(CLK_TOP_APLL2_D4, "apll2_d4", "apll2", 1, 4),
	FACTOR(CLK_TOP_APLL3_D4, "apll3_d4", "apll3", 1, 4),
	FACTOR(CLK_TOP_APLL4_D4, "apll4_d4", "apll4", 1, 4),
	FACTOR(CLK_TOP_APLL5_D4, "apll5_d4", "apll5", 1, 4),
	FACTOR(CLK_TOP_HDMIRX_APLL_D3, "hdmirx_apll_d3", "hdmirx_apll", 1, 3),
	FACTOR(CLK_TOP_HDMIRX_APLL_D4, "hdmirx_apll_d4", "hdmirx_apll", 1, 4),
	FACTOR(CLK_TOP_HDMIRX_APLL_D6, "hdmirx_apll_d6", "hdmirx_apll", 1, 6),
	FACTOR(CLK_TOP_MMPLL_D4, "mmpll_d4", "mmpll", 1, 4),
	FACTOR(CLK_TOP_MMPLL_D4_D2, "mmpll_d4_d2", "mmpll_d4", 1, 2),
	FACTOR(CLK_TOP_MMPLL_D4_D4, "mmpll_d4_d4", "mmpll_d4", 1, 4),
	FACTOR(CLK_TOP_MMPLL_D5, "mmpll_d5", "mmpll", 1, 5),
	FACTOR(CLK_TOP_MMPLL_D5_D2, "mmpll_d5_d2", "mmpll_d5", 1, 2),
	FACTOR(CLK_TOP_MMPLL_D5_D4, "mmpll_d5_d4", "mmpll_d5", 1, 4),
	FACTOR(CLK_TOP_MMPLL_D6, "mmpll_d6", "mmpll", 1, 6),
	FACTOR(CLK_TOP_MMPLL_D6_D2, "mmpll_d6_d2", "mmpll_d6", 1, 2),
	FACTOR(CLK_TOP_MMPLL_D7, "mmpll_d7", "mmpll", 1, 7),
	FACTOR(CLK_TOP_MMPLL_D9, "mmpll_d9", "mmpll", 1, 9),
	FACTOR(CLK_TOP_TVDPLL1_D2, "tvdpll1_d2", "tvdpll1", 1, 2),
	FACTOR(CLK_TOP_TVDPLL1_D4, "tvdpll1_d4", "tvdpll1", 1, 4),
	FACTOR(CLK_TOP_TVDPLL1_D8, "tvdpll1_d8", "tvdpll1", 1, 8),
	FACTOR(CLK_TOP_TVDPLL1_D16, "tvdpll1_d16", "tvdpll1", 1, 16),
	FACTOR(CLK_TOP_TVDPLL2_D2, "tvdpll2_d2", "tvdpll2", 1, 2),
	FACTOR(CLK_TOP_TVDPLL2_D4, "tvdpll2_d4", "tvdpll2", 1, 4),
	FACTOR(CLK_TOP_TVDPLL2_D8, "tvdpll2_d8", "tvdpll2", 1, 8),
	FACTOR(CLK_TOP_TVDPLL2_D16, "tvdpll2_d16", "tvdpll2", 1, 16),
	FACTOR(CLK_TOP_MSDCPLL_D2, "msdcpll_d2", "msdcpll", 1, 2),
	FACTOR(CLK_TOP_MSDCPLL_D4, "msdcpll_d4", "msdcpll", 1, 4),
	FACTOR(CLK_TOP_MSDCPLL_D16, "msdcpll_d16", "msdcpll", 1, 16),
	FACTOR(CLK_TOP_ETHPLL_D2, "ethpll_d2", "ethpll", 1, 2),
	FACTOR(CLK_TOP_ETHPLL_D8, "ethpll_d8", "ethpll", 1, 8),
	FACTOR(CLK_TOP_ETHPLL_D10, "ethpll_d10", "ethpll", 1, 10),
	FACTOR(CLK_TOP_DGIPLL_D2, "dgipll_d2", "dgipll", 1, 2),
	FACTOR(CLK_TOP_ULPOSC1_D2, "ulposc1_d2", "ulposc1", 1, 2),
	FACTOR(CLK_TOP_ULPOSC1_D4, "ulposc1_d4", "ulposc1", 1, 4),
	FACTOR(CLK_TOP_ULPOSC1_D7, "ulposc1_d7", "ulposc1", 1, 7),
	FACTOR(CLK_TOP_ULPOSC1_D8, "ulposc1_d8", "ulposc1", 1, 8),
	FACTOR(CLK_TOP_ULPOSC1_D10, "ulposc1_d10", "ulposc1", 1, 10),
	FACTOR(CLK_TOP_ULPOSC1_D16, "ulposc1_d16", "ulposc1", 1, 16),
	FACTOR(CLK_TOP_ADSPPLL_D2, "adsppll_d2", "adsppll", 1, 2),
	FACTOR(CLK_TOP_ADSPPLL_D4, "adsppll_d4", "adsppll", 1, 4),
	FACTOR(CLK_TOP_ADSPPLL_D8, "adsppll_d8", "adsppll", 1, 8),
};

static const char * const axi_parents[] = {
	"clk26m",
	"mainpll_d4_d4",
	"mainpll_d7_d2",
	"mainpll_d4_d2",
	"mainpll_d5_d2",
	"mainpll_d6_d2",
	"ulposc1_d4"
};

static const char * const spm_parents[] = {
	"clk26m",
	"ulposc1_d10",
	"mainpll_d7_d4",
	"clk32k"
};

static const char * const scp_parents[] = {
	"clk26m",
	"univpll_d4",
	"mainpll_d6",
	"univpll_d6",
	"univpll_d4_d2",
	"mainpll_d4_d2",
	"mainpll_d4",
	"mainpll_d6_d2"
};

static const char * const bus_aximem_parents[] = {
	"clk26m",
	"mainpll_d7_d2",
	"mainpll_d4_d2",
	"mainpll_d5_d2",
	"mainpll_d6"
};

static const char * const vpp_parents[] = {
	"clk26m",
	"univpll_d6_d2",
	"mainpll_d5_d2",
	"mmpll_d6_d2",
	"univpll_d5_d2",
	"univpll_d4_d2",
	"mmpll_d4_d2",
	"mmpll_d7",
	"univpll_d6",
	"mainpll_d4",
	"mmpll_d5",
	"tvdpll1",
	"tvdpll2",
	"univpll_d4",
	"mmpll_d4"
};

static const char * const ethdr_parents[] = {
	"clk26m",
	"univpll_d6_d2",
	"mainpll_d5_d2",
	"mmpll_d6_d2",
	"univpll_d5_d2",
	"univpll_d4_d2",
	"mmpll_d4_d2",
	"mmpll_d7",
	"univpll_d6",
	"mainpll_d4",
	"mmpll_d5_d4",
	"tvdpll1",
	"tvdpll2",
	"univpll_d4",
	"mmpll_d4"
};

static const char * const ipe_parents[] = {
	"clk26m",
	"imgpll",
	"mainpll_d4",
	"mmpll_d6",
	"univpll_d6",
	"mainpll_d6",
	"mmpll_d4_d2",
	"univpll_d4_d2",
	"mainpll_d4_d2",
	"mmpll_d6_d2",
	"univpll_d5_d2"
};

static const char * const cam_parents[] = {
	"clk26m",
	"mainpll_d4",
	"mmpll_d4",
	"univpll_d4",
	"univpll_d5",
	"univpll_d6",
	"mmpll_d7",
	"univpll_d4_d2",
	"mainpll_d4_d2",
	"imgpll"
};

static const char * const ccu_parents[] = {
	"clk26m",
	"univpll_d6",
	"mainpll_d4_d2",
	"mainpll_d4",
	"univpll_d5",
	"mainpll_d6",
	"mmpll_d6",
	"mmpll_d7",
	"univpll_d4_d2",
	"univpll_d7"
};

static const char * const img_parents[] = {
	"clk26m",
	"imgpll",
	"univpll_d4",
	"mainpll_d4",
	"univpll_d5",
	"mmpll_d6",
	"univpll_d6",
	"mainpll_d6",
	"mmpll_d4_d2",
	"univpll_d4_d2",
	"mainpll_d4_d2",
	"univpll_d5_d2"
};

static const char * const camtm_parents[] = {
	"clk26m",
	"univpll_d4_d4",
	"univpll_d6_d2",
	"univpll_d6_d4"
};

static const char * const dsp_parents[] = {
	"clk26m",
	"univpll_d6_d2",
	"univpll_d4_d2",
	"univpll_d5",
	"univpll_d4",
	"mmpll_d4",
	"mainpll_d3",
	"univpll_d3"
};

static const char * const dsp1_parents[] = {
	"clk26m",
	"univpll_d6_d2",
	"mainpll_d4_d2",
	"univpll_d5",
	"mmpll_d5",
	"univpll_d4",
	"mainpll_d3",
	"univpll_d3"
};

static const char * const dsp2_parents[] = {
	"clk26m",
	"univpll_d6_d2",
	"univpll_d4_d2",
	"mainpll_d4",
	"univpll_d4",
	"mmpll_d4",
	"mainpll_d3",
	"univpll_d3"
};

static const char * const ipu_if_parents[] = {
	"clk26m",
	"univpll_d6_d2",
	"univpll_d5_d2",
	"mainpll_d4_d2",
	"mainpll_d6",
	"univpll_d5",
	"univpll_d4",
	"mmpll_d4"
};

/*
 * MFG can be also parented to "univpll_d6" and "univpll_d7":
 * these have been removed from the parents list to let us
 * achieve GPU DVFS without any special clock handlers.
 */
static const char * const mfg_parents[] = {
	"clk26m",
	"mainpll_d5_d2"
};

static const char * const camtg_parents[] = {
	"clk26m",
	"univpll_192m_d8",
	"univpll_d6_d8",
	"univpll_192m_d4",
	"univpll_d6_d16",
	"clk26m_d2",
	"univpll_192m_d16",
	"univpll_192m_d32"
};

static const char * const uart_parents[] = {
	"clk26m",
	"univpll_d6_d8"
};

static const char * const spi_parents[] = {
	"clk26m",
	"mainpll_d5_d4",
	"mainpll_d6_d4",
	"msdcpll_d4",
	"univpll_d6_d2",
	"mainpll_d6_d2",
	"mainpll_d4_d4",
	"univpll_d5_d4"
};

static const char * const spis_parents[] = {
	"clk26m",
	"univpll_d6",
	"mainpll_d6",
	"univpll_d4_d2",
	"univpll_d6_d2",
	"univpll_d4_d4",
	"univpll_d6_d4",
	"mainpll_d7_d4"
};

static const char * const msdc50_0_h_parents[] = {
	"clk26m",
	"mainpll_d4_d2",
	"mainpll_d6_d2"
};

static const char * const msdc50_0_parents[] = {
	"clk26m",
	"msdcpll",
	"msdcpll_d2",
	"univpll_d4_d4",
	"mainpll_d6_d2",
	"univpll_d4_d2"
};

static const char * const msdc30_parents[] = {
	"clk26m",
	"univpll_d6_d2",
	"mainpll_d6_d2",
	"mainpll_d7_d2",
	"msdcpll_d2"
};

static const char * const intdir_parents[] = {
	"clk26m",
	"univpll_d6",
	"mainpll_d4",
	"univpll_d4"
};

static const char * const aud_intbus_parents[] = {
	"clk26m",
	"mainpll_d4_d4",
	"mainpll_d7_d4"
};

static const char * const audio_h_parents[] = {
	"clk26m",
	"univpll_d7",
	"apll1",
	"apll2"
};

static const char * const pwrap_ulposc_parents[] = {
	"ulposc1_d10",
	"clk26m",
	"ulposc1_d4",
	"ulposc1_d7",
	"ulposc1_d8",
	"ulposc1_d16",
	"mainpll_d4_d8",
	"univpll_d5_d8"
};

static const char * const atb_parents[] = {
	"clk26m",
	"mainpll_d4_d2",
	"mainpll_d5_d2"
};

static const char * const pwrmcu_parents[] = {
	"clk26m",
	"mainpll_d7_d2",
	"mainpll_d6_d2",
	"mainpll_d5_d2",
	"mainpll_d9",
	"mainpll_d4_d2"
};

static const char * const dp_parents[] = {
	"clk26m",
	"tvdpll1_d2",
	"tvdpll2_d2",
	"tvdpll1_d4",
	"tvdpll2_d4",
	"tvdpll1_d8",
	"tvdpll2_d8",
	"tvdpll1_d16",
	"tvdpll2_d16"
};

static const char * const disp_pwm_parents[] = {
	"clk26m",
	"univpll_d6_d4",
	"ulposc1_d2",
	"ulposc1_d4",
	"ulposc1_d16"
};

static const char * const usb_parents[] = {
	"clk26m",
	"univpll_d5_d4",
	"univpll_d6_d4",
	"univpll_d5_d2"
};

static const char * const i2c_parents[] = {
	"clk26m",
	"mainpll_d4_d8",
	"univpll_d5_d4"
};

static const char * const seninf_parents[] = {
	"clk26m",
	"univpll_d4_d4",
	"univpll_d6_d2",
	"univpll_d4_d2",
	"univpll_d7",
	"univpll_d6",
	"mmpll_d6",
	"univpll_d5"
};

static const char * const gcpu_parents[] = {
	"clk26m",
	"mainpll_d6",
	"univpll_d4_d2",
	"mmpll_d5_d2",
	"univpll_d5_d2"
};

static const char * const dxcc_parents[] = {
	"clk26m",
	"mainpll_d4_d2",
	"mainpll_d4_d4",
	"mainpll_d4_d8"
};

static const char * const dpmaif_parents[] = {
	"clk26m",
	"univpll_d4_d4",
	"mainpll_d6",
	"mainpll_d4_d2",
	"univpll_d4_d2"
};

static const char * const aes_fde_parents[] = {
	"clk26m",
	"mainpll_d4_d2",
	"mainpll_d6",
	"mainpll_d4_d4",
	"univpll_d4_d2",
	"univpll_d6"
};

static const char * const ufs_parents[] = {
	"clk26m",
	"mainpll_d4_d4",
	"mainpll_d4_d8",
	"univpll_d4_d4",
	"mainpll_d6_d2",
	"univpll_d6_d2",
	"msdcpll_d2"
};

static const char * const ufs_tick1us_parents[] = {
	"clk26m_d52",
	"clk26m"
};

static const char * const ufs_mp_sap_parents[] = {
	"clk26m",
	"msdcpll_d16"
};

static const char * const venc_parents[] = {
	"clk26m",
	"mmpll_d4_d2",
	"mainpll_d6",
	"univpll_d4_d2",
	"mainpll_d4_d2",
	"univpll_d6",
	"mmpll_d6",
	"mainpll_d5_d2",
	"mainpll_d6_d2",
	"mmpll_d9",
	"univpll_d4_d4",
	"mainpll_d4",
	"univpll_d4",
	"univpll_d5",
	"univpll_d5_d2",
	"mainpll_d5"
};

static const char * const vdec_parents[] = {
	"clk26m",
	"mainpll_d5_d2",
	"mmpll_d6_d2",
	"univpll_d4_d2",
	"mmpll_d4_d2",
	"mainpll_d5",
	"mmpll_d6",
	"mmpll_d5",
	"vdecpll",
	"univpll_d4",
	"mmpll_d4",
	"univpll_d6_d2",
	"mmpll_d9",
	"univpll_d6",
	"univpll_d5",
	"mainpll_d4"
};

static const char * const pwm_parents[] = {
	"clk26m",
	"univpll_d4_d8"
};

static const char * const mcupm_parents[] = {
	"clk26m",
	"mainpll_d6_d2",
	"mainpll_d7_d4",
};

static const char * const spmi_parents[] = {
	"clk26m",
	"clk26m_d2",
	"ulposc1_d8",
	"ulposc1_d10",
	"ulposc1_d16",
	"ulposc1_d7",
	"clk32k",
	"mainpll_d7_d8",
	"mainpll_d6_d8",
	"mainpll_d5_d8"
};

static const char * const dvfsrc_parents[] = {
	"clk26m",
	"ulposc1_d10",
	"univpll_d6_d8",
	"msdcpll_d16"
};

static const char * const tl_parents[] = {
	"clk26m",
	"univpll_d5_d4",
	"mainpll_d4_d4"
};

static const char * const dsi_occ_parents[] = {
	"clk26m",
	"mainpll_d6_d2",
	"univpll_d5_d2",
	"univpll_d4_d2"
};

static const char * const wpe_vpp_parents[] = {
	"clk26m",
	"mainpll_d5_d2",
	"mmpll_d6_d2",
	"univpll_d5_d2",
	"mainpll_d4_d2",
	"univpll_d4_d2",
	"mmpll_d4_d2",
	"mainpll_d6",
	"mmpll_d7",
	"univpll_d6",
	"mainpll_d5",
	"univpll_d5",
	"mainpll_d4",
	"tvdpll1",
	"univpll_d4"
};

static const char * const hdcp_parents[] = {
	"clk26m",
	"univpll_d4_d8",
	"mainpll_d5_d8",
	"univpll_d6_d4"
};

static const char * const hdcp_24m_parents[] = {
	"clk26m",
	"univpll_192m_d4",
	"univpll_192m_d8",
	"univpll_d6_d8"
};

static const char * const hd20_dacr_ref_parents[] = {
	"clk26m",
	"univpll_d4_d2",
	"univpll_d4_d4",
	"univpll_d4_d8"
};

static const char * const hd20_hdcp_c_parents[] = {
	"clk26m",
	"msdcpll_d4",
	"univpll_d4_d8",
	"univpll_d6_d8"
};

static const char * const hdmi_xtal_parents[] = {
	"clk26m",
	"clk26m_d2"
};

static const char * const hdmi_apb_parents[] = {
	"clk26m",
	"univpll_d6_d4",
	"msdcpll_d2"
};

static const char * const snps_eth_250m_parents[] = {
	"clk26m",
	"ethpll_d2"
};

static const char * const snps_eth_62p4m_ptp_parents[] = {
	"apll2_d3",
	"apll1_d3",
	"clk26m",
	"ethpll_d8"
};

static const char * const snps_eth_50m_rmii_parents[] = {
	"clk26m",
	"ethpll_d10"
};

static const char * const dgi_out_parents[] = {
	"clk26m",
	"dgipll",
	"dgipll_d2",
	"in_dgi",
	"in_dgi_d2",
	"mmpll_d4_d4"
};

static const char * const nna_parents[] = {
	"clk26m",
	"nnapll",
	"univpll_d4",
	"mainpll_d4",
	"univpll_d5",
	"mmpll_d6",
	"univpll_d6",
	"mainpll_d6",
	"mmpll_d4_d2",
	"univpll_d4_d2",
	"mainpll_d4_d2",
	"mmpll_d6_d2"
};

static const char * const adsp_parents[] = {
	"clk26m",
	"clk26m_d2",
	"mainpll_d6",
	"mainpll_d5_d2",
	"univpll_d4_d4",
	"univpll_d4",
	"univpll_d6",
	"ulposc1",
	"adsppll",
	"adsppll_d2",
	"adsppll_d4",
	"adsppll_d8"
};

static const char * const asm_parents[] = {
	"clk26m",
	"univpll_d6_d4",
	"univpll_d6_d2",
	"mainpll_d5_d2"
};

static const char * const apll1_parents[] = {
	"clk26m",
	"apll1_d4"
};

static const char * const apll2_parents[] = {
	"clk26m",
	"apll2_d4"
};

static const char * const apll3_parents[] = {
	"clk26m",
	"apll3_d4"
};

static const char * const apll4_parents[] = {
	"clk26m",
	"apll4_d4"
};

static const char * const apll5_parents[] = {
	"clk26m",
	"apll5_d4"
};

static const char * const i2s_parents[] = {
	"clk26m",
	"apll1",
	"apll2",
	"apll3",
	"apll4",
	"apll5",
	"hdmirx_apll"
};

static const char * const a1sys_hp_parents[] = {
	"clk26m",
	"apll1_d4"
};

static const char * const a2sys_parents[] = {
	"clk26m",
	"apll2_d4"
};

static const char * const a3sys_parents[] = {
	"clk26m",
	"apll3_d4",
	"apll4_d4",
	"apll5_d4",
	"hdmirx_apll_d3",
	"hdmirx_apll_d4",
	"hdmirx_apll_d6"
};

static const char * const spinfi_b_parents[] = {
	"clk26m",
	"univpll_d6_d8",
	"univpll_d5_d8",
	"mainpll_d4_d8",
	"mainpll_d7_d4",
	"mainpll_d6_d4",
	"univpll_d6_d4",
	"univpll_d5_d4"
};

static const char * const nfi1x_parents[] = {
	"clk26m",
	"univpll_d5_d4",
	"mainpll_d7_d4",
	"mainpll_d6_d4",
	"univpll_d6_d4",
	"mainpll_d4_d4",
	"mainpll_d7_d2",
	"mainpll_d6_d2"
};

static const char * const ecc_parents[] = {
	"clk26m",
	"mainpll_d4_d4",
	"mainpll_d5_d2",
	"mainpll_d4_d2",
	"mainpll_d6",
	"univpll_d6"
};

static const char * const audio_local_bus_parents[] = {
	"clk26m",
	"clk26m_d2",
	"mainpll_d4_d4",
	"mainpll_d7_d2",
	"mainpll_d4_d2",
	"mainpll_d5_d2",
	"mainpll_d6_d2",
	"mainpll_d7",
	"univpll_d6",
	"ulposc1",
	"ulposc1_d4",
	"ulposc1_d2"
};

static const char * const spinor_parents[] = {
	"clk26m",
	"clk26m_d2",
	"mainpll_d7_d8",
	"univpll_d6_d8"
};

static const char * const dvio_dgi_ref_parents[] = {
	"clk26m",
	"in_dgi",
	"in_dgi_d2",
	"in_dgi_d4",
	"in_dgi_d6",
	"in_dgi_d8",
	"mmpll_d4_d4"
};

static const char * const ulposc_parents[] = {
	"ulposc1",
	"ethpll_d2",
	"mainpll_d4_d2",
	"ethpll_d10"
};

static const char * const ulposc_core_parents[] = {
	"ulposc2",
	"univpll_d7",
	"mainpll_d6",
	"ethpll_d10"
};

static const char * const srck_parents[] = {
	"ulposc1_d10",
	"clk26m"
};

static const char * const mfg_fast_parents[] = {
	"top_mfg_core_tmp",
	"mfgpll"
};

static const struct mtk_mux top_mtk_muxes[] = {
	/*
	 * CLK_CFG_0
	 * top_axi and top_bus_aximem are bus clocks, should not be closed by Linux.
	 * top_spm and top_scp are main clocks in always-on co-processor.
	 */
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_AXI, "top_axi",
		axi_parents, 0x020, 0x024, 0x028, 0, 3, 7, 0x04, 0, CLK_IS_CRITICAL),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_SPM, "top_spm",
		spm_parents, 0x020, 0x024, 0x028, 8, 2, 15, 0x04, 1, CLK_IS_CRITICAL),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_SCP, "top_scp",
		scp_parents, 0x020, 0x024, 0x028, 16, 3, 23, 0x04, 2, CLK_IS_CRITICAL),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_BUS_AXIMEM, "top_bus_aximem",
		bus_aximem_parents, 0x020, 0x024, 0x028, 24, 3, 31, 0x04, 3, CLK_IS_CRITICAL),
	/* CLK_CFG_1 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_VPP, "top_vpp",
		vpp_parents, 0x02C, 0x030, 0x034, 0, 4, 7, 0x04, 4),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_ETHDR, "top_ethdr",
		ethdr_parents, 0x02C, 0x030, 0x034, 8, 4, 15, 0x04, 5),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_IPE, "top_ipe",
		ipe_parents, 0x02C, 0x030, 0x034, 16, 4, 23, 0x04, 6),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CAM, "top_cam",
		cam_parents, 0x02C, 0x030, 0x034, 24, 4, 31, 0x04, 7),
	/* CLK_CFG_2 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CCU, "top_ccu",
		ccu_parents, 0x038, 0x03C, 0x040, 0, 4, 7, 0x04, 8),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_IMG, "top_img",
		img_parents, 0x038, 0x03C, 0x040, 8, 4, 15, 0x04, 9),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CAMTM, "top_camtm",
		camtm_parents, 0x038, 0x03C, 0x040, 16, 2, 23, 0x04, 10),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DSP, "top_dsp",
		dsp_parents, 0x038, 0x03C, 0x040, 24, 3, 31, 0x04, 11),
	/* CLK_CFG_3 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DSP1, "top_dsp1",
		dsp1_parents, 0x044, 0x048, 0x04C, 0, 3, 7, 0x04, 12),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DSP2, "top_dsp2",
		dsp1_parents, 0x044, 0x048, 0x04C, 8, 3, 15, 0x04, 13),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DSP3, "top_dsp3",
		dsp1_parents, 0x044, 0x048, 0x04C, 16, 3, 23, 0x04, 14),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DSP4, "top_dsp4",
		dsp2_parents, 0x044, 0x048, 0x04C, 24, 3, 31, 0x04, 15),
	/* CLK_CFG_4 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DSP5, "top_dsp5",
		dsp2_parents, 0x050, 0x054, 0x058, 0, 3, 7, 0x04, 16),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DSP6, "top_dsp6",
		dsp2_parents, 0x050, 0x054, 0x058, 8, 3, 15, 0x04, 17),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DSP7, "top_dsp7",
		dsp_parents, 0x050, 0x054, 0x058, 16, 3, 23, 0x04, 18),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_IPU_IF, "top_ipu_if",
		ipu_if_parents, 0x050, 0x054, 0x058, 24, 3, 31, 0x04, 19),
	/* CLK_CFG_5 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MFG_CORE_TMP, "top_mfg_core_tmp",
		mfg_parents, 0x05C, 0x060, 0x064, 0, 2, 7, 0x04, 20),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CAMTG, "top_camtg",
		camtg_parents, 0x05C, 0x060, 0x064, 8, 3, 15, 0x04, 21),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CAMTG2, "top_camtg2",
		camtg_parents, 0x05C, 0x060, 0x064, 16, 3, 23, 0x04, 22),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CAMTG3, "top_camtg3",
		camtg_parents, 0x05C, 0x060, 0x064, 24, 3, 31, 0x04, 23),
	/* CLK_CFG_6 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CAMTG4, "top_camtg4",
		camtg_parents, 0x068, 0x06C, 0x070, 0, 3, 7, 0x04, 24),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CAMTG5, "top_camtg5",
		camtg_parents, 0x068, 0x06C, 0x070, 8, 3, 15, 0x04, 25),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_UART, "top_uart",
		uart_parents, 0x068, 0x06C, 0x070, 16, 1, 23, 0x04, 26),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SPI, "top_spi",
		spi_parents, 0x068, 0x06C, 0x070, 24, 3, 31, 0x04, 27),
	/* CLK_CFG_7 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SPIS, "top_spis",
		spis_parents, 0x074, 0x078, 0x07C, 0, 3, 7, 0x04, 28),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MSDC50_0_HCLK, "top_msdc50_0_hclk",
		msdc50_0_h_parents, 0x074, 0x078, 0x07C, 8, 2, 15, 0x04, 29),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MSDC50_0, "top_msdc50_0",
		msdc50_0_parents, 0x074, 0x078, 0x07C, 16, 3, 23, 0x04, 30),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MSDC30_1, "top_msdc30_1",
		msdc30_parents, 0x074, 0x078, 0x07C, 24, 3, 31, 0x04, 31),
	/* CLK_CFG_8 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MSDC30_2, "top_msdc30_2",
		msdc30_parents, 0x080, 0x084, 0x088, 0, 3, 7, 0x08, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_INTDIR, "top_intdir",
		intdir_parents, 0x080, 0x084, 0x088, 8, 2, 15, 0x08, 1),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUD_INTBUS, "top_aud_intbus",
		aud_intbus_parents, 0x080, 0x084, 0x088, 16, 2, 23, 0x08, 2),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUDIO_H, "top_audio_h",
		audio_h_parents, 0x080, 0x084, 0x088, 24, 2, 31, 0x08, 3),
	/*
	 * CLK_CFG_9
	 * top_pwrmcu is main clock in other co-processor, should not be
	 * handled by Linux.
	 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_PWRAP_ULPOSC, "top_pwrap_ulposc",
		pwrap_ulposc_parents, 0x08C, 0x090, 0x094, 0, 3, 7, 0x08, 4),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_ATB, "top_atb",
		atb_parents, 0x08C, 0x090, 0x094, 8, 2, 15, 0x08, 5),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_PWRMCU, "top_pwrmcu",
		pwrmcu_parents, 0x08C, 0x090, 0x094, 16, 3, 23, 0x08, 6, CLK_IS_CRITICAL),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DP, "top_dp",
		dp_parents, 0x08C, 0x090, 0x094, 24, 4, 31, 0x08, 7),
	/* CLK_CFG_10 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_EDP, "top_edp",
		dp_parents, 0x098, 0x09C, 0x0A0, 0, 4, 7, 0x08, 8),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DPI, "top_dpi",
		dp_parents, 0x098, 0x09C, 0x0A0, 8, 4, 15, 0x08, 9),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DISP_PWM0, "top_disp_pwm0",
		disp_pwm_parents, 0x098, 0x09C, 0x0A0, 16, 3, 23, 0x08, 10),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DISP_PWM1, "top_disp_pwm1",
		disp_pwm_parents, 0x098, 0x09C, 0x0A0, 24, 3, 31, 0x08, 11),
	/* CLK_CFG_11 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_USB_TOP, "top_usb_top",
		usb_parents, 0x0A4, 0x0A8, 0x0AC, 0, 2, 7, 0x08, 12),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SSUSB_XHCI, "top_ssusb_xhci",
		usb_parents, 0x0A4, 0x0A8, 0x0AC, 8, 2, 15, 0x08, 13),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_USB_TOP_1P, "top_usb_top_1p",
		usb_parents, 0x0A4, 0x0A8, 0x0AC, 16, 2, 23, 0x08, 14),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SSUSB_XHCI_1P, "top_ssusb_xhci_1p",
		usb_parents, 0x0A4, 0x0A8, 0x0AC, 24, 2, 31, 0x08, 15),
	/* CLK_CFG_12 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_USB_TOP_2P, "top_usb_top_2p",
		usb_parents, 0x0B0, 0x0B4, 0x0B8, 0, 2, 7, 0x08, 16),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SSUSB_XHCI_2P, "top_ssusb_xhci_2p",
		usb_parents, 0x0B0, 0x0B4, 0x0B8, 8, 2, 15, 0x08, 17),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_USB_TOP_3P, "top_usb_top_3p",
		usb_parents, 0x0B0, 0x0B4, 0x0B8, 16, 2, 23, 0x08, 18),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SSUSB_XHCI_3P, "top_ssusb_xhci_3p",
		usb_parents, 0x0B0, 0x0B4, 0x0B8, 24, 2, 31, 0x08, 19),
	/* CLK_CFG_13 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_I2C, "top_i2c",
		i2c_parents, 0x0BC, 0x0C0, 0x0C4, 0, 2, 7, 0x08, 20),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SENINF, "top_seninf",
		seninf_parents, 0x0BC, 0x0C0, 0x0C4, 8, 3, 15, 0x08, 21),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SENINF1, "top_seninf1",
		seninf_parents, 0x0BC, 0x0C0, 0x0C4, 16, 3, 23, 0x08, 22),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SENINF2, "top_seninf2",
		seninf_parents, 0x0BC, 0x0C0, 0x0C4, 24, 3, 31, 0x08, 23),
	/* CLK_CFG_14 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SENINF3, "top_seninf3",
		seninf_parents, 0x0C8, 0x0CC, 0x0D0, 0, 3, 7, 0x08, 24),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_GCPU, "top_gcpu",
		gcpu_parents, 0x0C8, 0x0CC, 0x0D0, 8, 3, 15, 0x08, 25),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DXCC, "top_dxcc",
		dxcc_parents, 0x0C8, 0x0CC, 0x0D0, 16, 2, 23, 0x08, 26),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DPMAIF_MAIN, "top_dpmaif_main",
		dpmaif_parents, 0x0C8, 0x0CC, 0x0D0, 24, 3, 31, 0x08, 27),
	/* CLK_CFG_15 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AES_UFSFDE, "top_aes_ufsfde",
		aes_fde_parents, 0x0D4, 0x0D8, 0x0DC, 0, 3, 7, 0x08, 28),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_UFS, "top_ufs",
		ufs_parents, 0x0D4, 0x0D8, 0x0DC, 8, 3, 15, 0x08, 29),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_UFS_TICK1US, "top_ufs_tick1us",
		ufs_tick1us_parents, 0x0D4, 0x0D8, 0x0DC, 16, 1, 23, 0x08, 30),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_UFS_MP_SAP_CFG, "top_ufs_mp_sap_cfg",
		ufs_mp_sap_parents, 0x0D4, 0x0D8, 0x0DC, 24, 1, 31, 0x08, 31),
	/*
	 * CLK_CFG_16
	 * top_mcupm is main clock in other co-processor, should not be
	 * handled by Linux.
	 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_VENC, "top_venc",
		venc_parents, 0x0E0, 0x0E4, 0x0E8, 0, 4, 7, 0x0C, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_VDEC, "top_vdec",
		vdec_parents, 0x0E0, 0x0E4, 0x0E8, 8, 4, 15, 0x0C, 1),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_PWM, "top_pwm",
		pwm_parents, 0x0E0, 0x0E4, 0x0E8, 16, 1, 23, 0x0C, 2),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_MCUPM, "top_mcupm",
		mcupm_parents, 0x0E0, 0x0E4, 0x0E8, 24, 2, 31, 0x0C, 3, CLK_IS_CRITICAL),
	/*
	 * CLK_CFG_17
	 * top_dvfsrc is for internal DVFS usage, should not be handled by Linux.
	 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SPMI_P_MST, "top_spmi_p_mst",
		spmi_parents, 0x0EC, 0x0F0, 0x0F4, 0, 4, 7, 0x0C, 4),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SPMI_M_MST, "top_spmi_m_mst",
		spmi_parents, 0x0EC, 0x0F0, 0x0F4, 8, 4, 15, 0x0C, 5),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_DVFSRC, "top_dvfsrc",
		dvfsrc_parents, 0x0EC, 0x0F0, 0x0F4, 16, 2, 23, 0x0C, 6, CLK_IS_CRITICAL),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_TL, "top_tl",
		tl_parents, 0x0EC, 0x0F0, 0x0F4, 24, 2, 31, 0x0C, 7),
	/* CLK_CFG_18 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_TL_P1, "top_tl_p1",
		tl_parents, 0x0F8, 0x0FC, 0x0100, 0, 2, 7, 0x0C, 8),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AES_MSDCFDE, "top_aes_msdcfde",
		aes_fde_parents, 0x0F8, 0x0FC, 0x0100, 8, 3, 15, 0x0C, 9),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DSI_OCC, "top_dsi_occ",
		dsi_occ_parents, 0x0F8, 0x0FC, 0x0100, 16, 2, 23, 0x0C, 10),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_WPE_VPP, "top_wpe_vpp",
		wpe_vpp_parents, 0x0F8, 0x0FC, 0x0100, 24, 4, 31, 0x0C, 11),
	/* CLK_CFG_19 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_HDCP, "top_hdcp",
		hdcp_parents, 0x0104, 0x0108, 0x010C, 0, 2, 7, 0x0C, 12),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_HDCP_24M, "top_hdcp_24m",
		hdcp_24m_parents, 0x0104, 0x0108, 0x010C, 8, 2, 15, 0x0C, 13),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_HD20_DACR_REF_CLK, "top_hd20_dacr_ref_clk",
		hd20_dacr_ref_parents, 0x0104, 0x0108, 0x010C, 16, 2, 23, 0x0C, 14),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_HD20_HDCP_CCLK, "top_hd20_hdcp_cclk",
		hd20_hdcp_c_parents, 0x0104, 0x0108, 0x010C, 24, 2, 31, 0x0C, 15),
	/* CLK_CFG_20 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_HDMI_XTAL, "top_hdmi_xtal",
		hdmi_xtal_parents, 0x0110, 0x0114, 0x0118, 0, 1, 7, 0x0C, 16),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_HDMI_APB, "top_hdmi_apb",
		hdmi_apb_parents, 0x0110, 0x0114, 0x0118, 8, 2, 15, 0x0C, 17),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SNPS_ETH_250M, "top_snps_eth_250m",
		snps_eth_250m_parents, 0x0110, 0x0114, 0x0118, 16, 1, 23, 0x0C, 18),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SNPS_ETH_62P4M_PTP, "top_snps_eth_62p4m_ptp",
		snps_eth_62p4m_ptp_parents, 0x0110, 0x0114, 0x0118, 24, 2, 31, 0x0C, 19),
	/* CLK_CFG_21 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SNPS_ETH_50M_RMII, "snps_eth_50m_rmii",
		snps_eth_50m_rmii_parents, 0x011C, 0x0120, 0x0124, 0, 1, 7, 0x0C, 20),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DGI_OUT, "top_dgi_out",
		dgi_out_parents, 0x011C, 0x0120, 0x0124, 8, 3, 15, 0x0C, 21),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_NNA0, "top_nna0",
		nna_parents, 0x011C, 0x0120, 0x0124, 16, 4, 23, 0x0C, 22),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_NNA1, "top_nna1",
		nna_parents, 0x011C, 0x0120, 0x0124, 24, 4, 31, 0x0C, 23),
	/* CLK_CFG_22 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_ADSP, "top_adsp",
		adsp_parents, 0x0128, 0x012C, 0x0130, 0, 4, 7, 0x0C, 24),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_ASM_H, "top_asm_h",
		asm_parents, 0x0128, 0x012C, 0x0130, 8, 2, 15, 0x0C, 25),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_ASM_M, "top_asm_m",
		asm_parents, 0x0128, 0x012C, 0x0130, 16, 2, 23, 0x0C, 26),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_ASM_L, "top_asm_l",
		asm_parents, 0x0128, 0x012C, 0x0130, 24, 2, 31, 0x0C, 27),
	/* CLK_CFG_23 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_APLL1, "top_apll1",
		apll1_parents, 0x0134, 0x0138, 0x013C, 0, 1, 7, 0x0C, 28),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_APLL2, "top_apll2",
		apll2_parents, 0x0134, 0x0138, 0x013C, 8, 1, 15, 0x0C, 29),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_APLL3, "top_apll3",
		apll3_parents, 0x0134, 0x0138, 0x013C, 16, 1, 23, 0x0C, 30),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_APLL4, "top_apll4",
		apll4_parents, 0x0134, 0x0138, 0x013C, 24, 1, 31, 0x0C, 31),
	/*
	 * CLK_CFG_24
	 * i2so4_mck is not used in MT8195.
	 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_APLL5, "top_apll5",
		apll5_parents, 0x0140, 0x0144, 0x0148, 0, 1, 7, 0x010, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_I2SO1_MCK, "top_i2so1_mck",
		i2s_parents, 0x0140, 0x0144, 0x0148, 8, 3, 15, 0x010, 1),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_I2SO2_MCK, "top_i2so2_mck",
		i2s_parents, 0x0140, 0x0144, 0x0148, 16, 3, 23, 0x010, 2),
	/*
	 * CLK_CFG_25
	 * i2so5_mck and i2si4_mck are not used in MT8195.
	 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_I2SI1_MCK, "top_i2si1_mck",
		i2s_parents, 0x014C, 0x0150, 0x0154, 8, 3, 15, 0x010, 5),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_I2SI2_MCK, "top_i2si2_mck",
		i2s_parents, 0x014C, 0x0150, 0x0154, 16, 3, 23, 0x010, 6),
	/*
	 * CLK_CFG_26
	 * i2si5_mck is not used in MT8195.
	 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DPTX_MCK, "top_dptx_mck",
		i2s_parents, 0x0158, 0x015C, 0x0160, 8, 3, 15, 0x010, 9),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUD_IEC_CLK, "top_aud_iec_clk",
		i2s_parents, 0x0158, 0x015C, 0x0160, 16, 3, 23, 0x010, 10),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_A1SYS_HP, "top_a1sys_hp",
		a1sys_hp_parents, 0x0158, 0x015C, 0x0160, 24, 1, 31, 0x010, 11),
	/* CLK_CFG_27 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_A2SYS_HF, "top_a2sys_hf",
		a2sys_parents, 0x0164, 0x0168, 0x016C, 0, 1, 7, 0x010, 12),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_A3SYS_HF, "top_a3sys_hf",
		a3sys_parents, 0x0164, 0x0168, 0x016C, 8, 3, 15, 0x010, 13),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_A4SYS_HF, "top_a4sys_hf",
		a3sys_parents, 0x0164, 0x0168, 0x016C, 16, 3, 23, 0x010, 14),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SPINFI_BCLK, "top_spinfi_bclk",
		spinfi_b_parents, 0x0164, 0x0168, 0x016C, 24, 3, 31, 0x010, 15),
	/* CLK_CFG_28 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_NFI1X, "top_nfi1x",
		nfi1x_parents, 0x0170, 0x0174, 0x0178, 0, 3, 7, 0x010, 16),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_ECC, "top_ecc",
		ecc_parents, 0x0170, 0x0174, 0x0178, 8, 3, 15, 0x010, 17),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUDIO_LOCAL_BUS, "top_audio_local_bus",
		audio_local_bus_parents, 0x0170, 0x0174, 0x0178, 16, 4, 23, 0x010, 18),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SPINOR, "top_spinor",
		spinor_parents, 0x0170, 0x0174, 0x0178, 24, 2, 31, 0x010, 19),
	/*
	 * CLK_CFG_29
	 * top_ulposc/top_ulposc_core/top_srck are clock source of always on co-processor,
	 * should not be closed by Linux.
	 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DVIO_DGI_REF, "top_dvio_dgi_ref",
		dvio_dgi_ref_parents, 0x017C, 0x0180, 0x0184, 0, 3, 7, 0x010, 20),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_ULPOSC, "top_ulposc",
		ulposc_parents, 0x017C, 0x0180, 0x0184, 8, 2, 15, 0x010, 21, CLK_IS_CRITICAL),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_ULPOSC_CORE, "top_ulposc_core",
		ulposc_core_parents, 0x017C, 0x0180, 0x0184, 16, 2, 23, 0x010, 22, CLK_IS_CRITICAL),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_SRCK, "top_srck",
		srck_parents, 0x017C, 0x0180, 0x0184, 24, 1, 31, 0x010, 23, CLK_IS_CRITICAL),
	/*
	 * the clocks in CLK_CFG_30 ~ 37 are backup clock source, no need to handled
	 * by Linux.
	 */
};

static const struct mtk_composite top_adj_divs[] = {
	DIV_GATE(CLK_TOP_APLL12_DIV0, "apll12_div0", "top_i2si1_mck", 0x0320, 0, 0x0328, 8, 0),
	DIV_GATE(CLK_TOP_APLL12_DIV1, "apll12_div1", "top_i2si2_mck", 0x0320, 1, 0x0328, 8, 8),
	DIV_GATE(CLK_TOP_APLL12_DIV2, "apll12_div2", "top_i2so1_mck", 0x0320, 2, 0x0328, 8, 16),
	DIV_GATE(CLK_TOP_APLL12_DIV3, "apll12_div3", "top_i2so2_mck", 0x0320, 3, 0x0328, 8, 24),
	DIV_GATE(CLK_TOP_APLL12_DIV4, "apll12_div4", "top_aud_iec_clk", 0x0320, 4, 0x0334, 8, 0),
	/* apll12_div5 ~ 8 are not used in MT8195. */
	DIV_GATE(CLK_TOP_APLL12_DIV9, "apll12_div9", "top_dptx_mck", 0x0320, 9, 0x0338, 8, 8),
};

static const struct mtk_gate_regs top0_cg_regs = {
	.set_ofs = 0x238,
	.clr_ofs = 0x238,
	.sta_ofs = 0x238,
};

static const struct mtk_gate_regs top1_cg_regs = {
	.set_ofs = 0x250,
	.clr_ofs = 0x250,
	.sta_ofs = 0x250,
};

#define GATE_TOP0_FLAGS(_id, _name, _parent, _shift, _flag)		\
	GATE_MTK_FLAGS(_id, _name, _parent, &top0_cg_regs, _shift,	\
		&mtk_clk_gate_ops_no_setclr_inv, _flag)

#define GATE_TOP0(_id, _name, _parent, _shift)			\
	GATE_TOP0_FLAGS(_id, _name, _parent, _shift, 0)

#define GATE_TOP1(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &top1_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr_inv)

static const struct mtk_gate top_clks[] = {
	/* TOP0 */
	GATE_TOP0(CLK_TOP_CFG_VPP0, "cfg_vpp0", "top_vpp", 0),
	GATE_TOP0(CLK_TOP_CFG_VPP1, "cfg_vpp1", "top_vpp", 1),
	GATE_TOP0(CLK_TOP_CFG_VDO0, "cfg_vdo0", "top_vpp", 2),
	GATE_TOP0(CLK_TOP_CFG_VDO1, "cfg_vdo1", "top_vpp", 3),
	GATE_TOP0(CLK_TOP_CFG_UNIPLL_SES, "cfg_unipll_ses", "univpll_d2", 4),
	GATE_TOP0(CLK_TOP_CFG_26M_VPP0, "cfg_26m_vpp0", "clk26m", 5),
	GATE_TOP0(CLK_TOP_CFG_26M_VPP1, "cfg_26m_vpp1", "clk26m", 6),
	GATE_TOP0(CLK_TOP_CFG_26M_AUD, "cfg_26m_aud", "clk26m", 9),
	/*
	 * cfg_axi_east, cfg_axi_east_north, cfg_axi_north and cfg_axi_south
	 * are peripheral bus clock branches.
	 */
	GATE_TOP0_FLAGS(CLK_TOP_CFG_AXI_EAST, "cfg_axi_east", "top_axi", 10, CLK_IS_CRITICAL),
	GATE_TOP0_FLAGS(CLK_TOP_CFG_AXI_EAST_NORTH, "cfg_axi_east_north", "top_axi", 11,
		CLK_IS_CRITICAL),
	GATE_TOP0_FLAGS(CLK_TOP_CFG_AXI_NORTH, "cfg_axi_north", "top_axi", 12, CLK_IS_CRITICAL),
	GATE_TOP0_FLAGS(CLK_TOP_CFG_AXI_SOUTH, "cfg_axi_south", "top_axi", 13, CLK_IS_CRITICAL),
	GATE_TOP0(CLK_TOP_CFG_EXT_TEST, "cfg_ext_test", "msdcpll_d2", 15),
	/* TOP1 */
	GATE_TOP1(CLK_TOP_SSUSB_REF, "ssusb_ref", "clk26m", 0),
	GATE_TOP1(CLK_TOP_SSUSB_PHY_REF, "ssusb_phy_ref", "clk26m", 1),
	GATE_TOP1(CLK_TOP_SSUSB_P1_REF, "ssusb_p1_ref", "clk26m", 2),
	GATE_TOP1(CLK_TOP_SSUSB_PHY_P1_REF, "ssusb_phy_p1_ref", "clk26m", 3),
	GATE_TOP1(CLK_TOP_SSUSB_P2_REF, "ssusb_p2_ref", "clk26m", 4),
	GATE_TOP1(CLK_TOP_SSUSB_PHY_P2_REF, "ssusb_phy_p2_ref", "clk26m", 5),
	GATE_TOP1(CLK_TOP_SSUSB_P3_REF, "ssusb_p3_ref", "clk26m", 6),
	GATE_TOP1(CLK_TOP_SSUSB_PHY_P3_REF, "ssusb_phy_p3_ref", "clk26m", 7),
};

static const struct of_device_id of_match_clk_mt8195_topck[] = {
	{ .compatible = "mediatek,mt8195-topckgen", },
	{}
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8195_topck);

/* Register mux notifier for MFG mux */
static int clk_mt8195_reg_mfg_mux_notifier(struct device *dev, struct clk *clk)
{
	struct mtk_mux_nb *mfg_mux_nb;

	mfg_mux_nb = devm_kzalloc(dev, sizeof(*mfg_mux_nb), GFP_KERNEL);
	if (!mfg_mux_nb)
		return -ENOMEM;

	mfg_mux_nb->ops = &clk_mux_ops;
	mfg_mux_nb->bypass_index = 0; /* Bypass to TOP_MFG_CORE_TMP */

	return devm_mtk_clk_mux_notifier_register(dev, clk, mfg_mux_nb);
}

static int clk_mt8195_topck_probe(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *top_clk_data;
	struct device_node *node = pdev->dev.of_node;
	struct clk_hw *hw;
	int r;
	void __iomem *base;

	top_clk_data = mtk_alloc_clk_data(CLK_TOP_NR_CLK);
	if (!top_clk_data)
		return -ENOMEM;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base)) {
		r = PTR_ERR(base);
		goto free_top_data;
	}

	r = mtk_clk_register_fixed_clks(top_fixed_clks, ARRAY_SIZE(top_fixed_clks),
					top_clk_data);
	if (r)
		goto free_top_data;

	r = mtk_clk_register_factors(top_divs, ARRAY_SIZE(top_divs), top_clk_data);
	if (r)
		goto unregister_fixed_clks;

	r = mtk_clk_register_muxes(&pdev->dev, top_mtk_muxes,
				   ARRAY_SIZE(top_mtk_muxes), node,
				   &mt8195_clk_lock, top_clk_data);
	if (r)
		goto unregister_factors;

	hw = devm_clk_hw_register_mux(&pdev->dev, "mfg_ck_fast_ref", mfg_fast_parents,
				      ARRAY_SIZE(mfg_fast_parents), CLK_SET_RATE_PARENT,
				      (base + 0x250), 8, 1, 0, &mt8195_clk_lock);
	if (IS_ERR(hw)) {
		r = PTR_ERR(hw);
		goto unregister_muxes;
	}
	top_clk_data->hws[CLK_TOP_MFG_CK_FAST_REF] = hw;

	r = clk_mt8195_reg_mfg_mux_notifier(&pdev->dev,
					    top_clk_data->hws[CLK_TOP_MFG_CK_FAST_REF]->clk);
	if (r)
		goto unregister_muxes;

	r = mtk_clk_register_composites(&pdev->dev, top_adj_divs,
					ARRAY_SIZE(top_adj_divs), base,
					&mt8195_clk_lock, top_clk_data);
	if (r)
		goto unregister_muxes;

	r = mtk_clk_register_gates(&pdev->dev, node, top_clks,
				   ARRAY_SIZE(top_clks), top_clk_data);
	if (r)
		goto unregister_composite_divs;

	r = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, top_clk_data);
	if (r)
		goto unregister_gates;

	platform_set_drvdata(pdev, top_clk_data);

	return r;

unregister_gates:
	mtk_clk_unregister_gates(top_clks, ARRAY_SIZE(top_clks), top_clk_data);
unregister_composite_divs:
	mtk_clk_unregister_composites(top_adj_divs, ARRAY_SIZE(top_adj_divs), top_clk_data);
unregister_muxes:
	mtk_clk_unregister_muxes(top_mtk_muxes, ARRAY_SIZE(top_mtk_muxes), top_clk_data);
unregister_factors:
	mtk_clk_unregister_factors(top_divs, ARRAY_SIZE(top_divs), top_clk_data);
unregister_fixed_clks:
	mtk_clk_unregister_fixed_clks(top_fixed_clks, ARRAY_SIZE(top_fixed_clks), top_clk_data);
free_top_data:
	mtk_free_clk_data(top_clk_data);
	return r;
}

static int clk_mt8195_topck_remove(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *top_clk_data = platform_get_drvdata(pdev);
	struct device_node *node = pdev->dev.of_node;

	of_clk_del_provider(node);
	mtk_clk_unregister_gates(top_clks, ARRAY_SIZE(top_clks), top_clk_data);
	mtk_clk_unregister_composites(top_adj_divs, ARRAY_SIZE(top_adj_divs), top_clk_data);
	mtk_clk_unregister_muxes(top_mtk_muxes, ARRAY_SIZE(top_mtk_muxes), top_clk_data);
	mtk_clk_unregister_factors(top_divs, ARRAY_SIZE(top_divs), top_clk_data);
	mtk_clk_unregister_fixed_clks(top_fixed_clks, ARRAY_SIZE(top_fixed_clks), top_clk_data);
	mtk_free_clk_data(top_clk_data);

	return 0;
}

static struct platform_driver clk_mt8195_topck_drv = {
	.probe = clk_mt8195_topck_probe,
	.remove = clk_mt8195_topck_remove,
	.driver = {
		.name = "clk-mt8195-topck",
		.of_match_table = of_match_clk_mt8195_topck,
	},
};
module_platform_driver(clk_mt8195_topck_drv);
MODULE_LICENSE("GPL");
