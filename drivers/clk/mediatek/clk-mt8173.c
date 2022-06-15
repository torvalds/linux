// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: James Liao <jamesjj.liao@mediatek.com>
 */

#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include "clk-cpumux.h"
#include "clk-gate.h"
#include "clk-mtk.h"
#include "clk-pll.h"

#include <dt-bindings/clock/mt8173-clk.h>

/*
 * For some clocks, we don't care what their actual rates are. And these
 * clocks may change their rate on different products or different scenarios.
 * So we model these clocks' rate as 0, to denote it's not an actual rate.
 */
#define DUMMY_RATE		0

static DEFINE_SPINLOCK(mt8173_clk_lock);

static const struct mtk_fixed_clk fixed_clks[] __initconst = {
	FIXED_CLK(CLK_TOP_CLKPH_MCK_O, "clkph_mck_o", "clk26m", DUMMY_RATE),
	FIXED_CLK(CLK_TOP_USB_SYSPLL_125M, "usb_syspll_125m", "clk26m", 125 * MHZ),
	FIXED_CLK(CLK_TOP_DSI0_DIG, "dsi0_dig", "clk26m", DUMMY_RATE),
	FIXED_CLK(CLK_TOP_DSI1_DIG, "dsi1_dig", "clk26m", DUMMY_RATE),
	FIXED_CLK(CLK_TOP_LVDS_PXL, "lvds_pxl", "lvdspll", DUMMY_RATE),
	FIXED_CLK(CLK_TOP_LVDS_CTS, "lvds_cts", "lvdspll", DUMMY_RATE),
};

static const struct mtk_fixed_factor top_divs[] __initconst = {
	FACTOR(CLK_TOP_ARMCA7PLL_754M, "armca7pll_754m", "armca7pll", 1, 2),
	FACTOR(CLK_TOP_ARMCA7PLL_502M, "armca7pll_502m", "armca7pll", 1, 3),

	FACTOR(CLK_TOP_MAIN_H546M, "main_h546m", "mainpll", 1, 2),
	FACTOR(CLK_TOP_MAIN_H364M, "main_h364m", "mainpll", 1, 3),
	FACTOR(CLK_TOP_MAIN_H218P4M, "main_h218p4m", "mainpll", 1, 5),
	FACTOR(CLK_TOP_MAIN_H156M, "main_h156m", "mainpll", 1, 7),

	FACTOR(CLK_TOP_TVDPLL_445P5M, "tvdpll_445p5m", "tvdpll", 1, 4),
	FACTOR(CLK_TOP_TVDPLL_594M, "tvdpll_594m", "tvdpll", 1, 3),

	FACTOR(CLK_TOP_UNIV_624M, "univ_624m", "univpll", 1, 2),
	FACTOR(CLK_TOP_UNIV_416M, "univ_416m", "univpll", 1, 3),
	FACTOR(CLK_TOP_UNIV_249P6M, "univ_249p6m", "univpll", 1, 5),
	FACTOR(CLK_TOP_UNIV_178P3M, "univ_178p3m", "univpll", 1, 7),
	FACTOR(CLK_TOP_UNIV_48M, "univ_48m", "univpll", 1, 26),

	FACTOR(CLK_TOP_CLKRTC_EXT, "clkrtc_ext", "clk32k", 1, 1),
	FACTOR(CLK_TOP_CLKRTC_INT, "clkrtc_int", "clk26m", 1, 793),
	FACTOR(CLK_TOP_FPC, "fpc_ck", "clk26m", 1, 1),

	FACTOR(CLK_TOP_HDMITXPLL_D2, "hdmitxpll_d2", "hdmitx_dig_cts", 1, 2),
	FACTOR(CLK_TOP_HDMITXPLL_D3, "hdmitxpll_d3", "hdmitx_dig_cts", 1, 3),

	FACTOR(CLK_TOP_ARMCA7PLL_D2, "armca7pll_d2", "armca7pll_754m", 1, 1),
	FACTOR(CLK_TOP_ARMCA7PLL_D3, "armca7pll_d3", "armca7pll_502m", 1, 1),

	FACTOR(CLK_TOP_APLL1, "apll1_ck", "apll1", 1, 1),
	FACTOR(CLK_TOP_APLL2, "apll2_ck", "apll2", 1, 1),

	FACTOR(CLK_TOP_DMPLL, "dmpll_ck", "clkph_mck_o", 1, 1),
	FACTOR(CLK_TOP_DMPLL_D2, "dmpll_d2", "clkph_mck_o", 1, 2),
	FACTOR(CLK_TOP_DMPLL_D4, "dmpll_d4", "clkph_mck_o", 1, 4),
	FACTOR(CLK_TOP_DMPLL_D8, "dmpll_d8", "clkph_mck_o", 1, 8),
	FACTOR(CLK_TOP_DMPLL_D16, "dmpll_d16", "clkph_mck_o", 1, 16),

	FACTOR(CLK_TOP_LVDSPLL_D2, "lvdspll_d2", "lvdspll", 1, 2),
	FACTOR(CLK_TOP_LVDSPLL_D4, "lvdspll_d4", "lvdspll", 1, 4),
	FACTOR(CLK_TOP_LVDSPLL_D8, "lvdspll_d8", "lvdspll", 1, 8),

	FACTOR(CLK_TOP_MMPLL, "mmpll_ck", "mmpll", 1, 1),
	FACTOR(CLK_TOP_MMPLL_D2, "mmpll_d2", "mmpll", 1, 2),

	FACTOR(CLK_TOP_MSDCPLL, "msdcpll_ck", "msdcpll", 1, 1),
	FACTOR(CLK_TOP_MSDCPLL_D2, "msdcpll_d2", "msdcpll", 1, 2),
	FACTOR(CLK_TOP_MSDCPLL_D4, "msdcpll_d4", "msdcpll", 1, 4),
	FACTOR(CLK_TOP_MSDCPLL2, "msdcpll2_ck", "msdcpll2", 1, 1),
	FACTOR(CLK_TOP_MSDCPLL2_D2, "msdcpll2_d2", "msdcpll2", 1, 2),
	FACTOR(CLK_TOP_MSDCPLL2_D4, "msdcpll2_d4", "msdcpll2", 1, 4),

	FACTOR(CLK_TOP_SYSPLL_D2, "syspll_d2", "main_h546m", 1, 1),
	FACTOR(CLK_TOP_SYSPLL1_D2, "syspll1_d2", "main_h546m", 1, 2),
	FACTOR(CLK_TOP_SYSPLL1_D4, "syspll1_d4", "main_h546m", 1, 4),
	FACTOR(CLK_TOP_SYSPLL1_D8, "syspll1_d8", "main_h546m", 1, 8),
	FACTOR(CLK_TOP_SYSPLL1_D16, "syspll1_d16", "main_h546m", 1, 16),
	FACTOR(CLK_TOP_SYSPLL_D3, "syspll_d3", "main_h364m", 1, 1),
	FACTOR(CLK_TOP_SYSPLL2_D2, "syspll2_d2", "main_h364m", 1, 2),
	FACTOR(CLK_TOP_SYSPLL2_D4, "syspll2_d4", "main_h364m", 1, 4),
	FACTOR(CLK_TOP_SYSPLL_D5, "syspll_d5", "main_h218p4m", 1, 1),
	FACTOR(CLK_TOP_SYSPLL3_D2, "syspll3_d2", "main_h218p4m", 1, 2),
	FACTOR(CLK_TOP_SYSPLL3_D4, "syspll3_d4", "main_h218p4m", 1, 4),
	FACTOR(CLK_TOP_SYSPLL_D7, "syspll_d7", "main_h156m", 1, 1),
	FACTOR(CLK_TOP_SYSPLL4_D2, "syspll4_d2", "main_h156m", 1, 2),
	FACTOR(CLK_TOP_SYSPLL4_D4, "syspll4_d4", "main_h156m", 1, 4),

	FACTOR(CLK_TOP_TVDPLL, "tvdpll_ck", "tvdpll_594m", 1, 1),
	FACTOR(CLK_TOP_TVDPLL_D2, "tvdpll_d2", "tvdpll_594m", 1, 2),
	FACTOR(CLK_TOP_TVDPLL_D4, "tvdpll_d4", "tvdpll_594m", 1, 4),
	FACTOR(CLK_TOP_TVDPLL_D8, "tvdpll_d8", "tvdpll_594m", 1, 8),
	FACTOR(CLK_TOP_TVDPLL_D16, "tvdpll_d16", "tvdpll_594m", 1, 16),

	FACTOR(CLK_TOP_UNIVPLL_D2, "univpll_d2", "univ_624m", 1, 1),
	FACTOR(CLK_TOP_UNIVPLL1_D2, "univpll1_d2", "univ_624m", 1, 2),
	FACTOR(CLK_TOP_UNIVPLL1_D4, "univpll1_d4", "univ_624m", 1, 4),
	FACTOR(CLK_TOP_UNIVPLL1_D8, "univpll1_d8", "univ_624m", 1, 8),
	FACTOR(CLK_TOP_UNIVPLL_D3, "univpll_d3", "univ_416m", 1, 1),
	FACTOR(CLK_TOP_UNIVPLL2_D2, "univpll2_d2", "univ_416m", 1, 2),
	FACTOR(CLK_TOP_UNIVPLL2_D4, "univpll2_d4", "univ_416m", 1, 4),
	FACTOR(CLK_TOP_UNIVPLL2_D8, "univpll2_d8", "univ_416m", 1, 8),
	FACTOR(CLK_TOP_UNIVPLL_D5, "univpll_d5", "univ_249p6m", 1, 1),
	FACTOR(CLK_TOP_UNIVPLL3_D2, "univpll3_d2", "univ_249p6m", 1, 2),
	FACTOR(CLK_TOP_UNIVPLL3_D4, "univpll3_d4", "univ_249p6m", 1, 4),
	FACTOR(CLK_TOP_UNIVPLL3_D8, "univpll3_d8", "univ_249p6m", 1, 8),
	FACTOR(CLK_TOP_UNIVPLL_D7, "univpll_d7", "univ_178p3m", 1, 1),
	FACTOR(CLK_TOP_UNIVPLL_D26, "univpll_d26", "univ_48m", 1, 1),
	FACTOR(CLK_TOP_UNIVPLL_D52, "univpll_d52", "univ_48m", 1, 2),

	FACTOR(CLK_TOP_VCODECPLL, "vcodecpll_ck", "vcodecpll", 1, 3),
	FACTOR(CLK_TOP_VCODECPLL_370P5, "vcodecpll_370p5", "vcodecpll", 1, 4),

	FACTOR(CLK_TOP_VENCPLL, "vencpll_ck", "vencpll", 1, 1),
	FACTOR(CLK_TOP_VENCPLL_D2, "vencpll_d2", "vencpll", 1, 2),
	FACTOR(CLK_TOP_VENCPLL_D4, "vencpll_d4", "vencpll", 1, 4),
};

static const char * const axi_parents[] __initconst = {
	"clk26m",
	"syspll1_d2",
	"syspll_d5",
	"syspll1_d4",
	"univpll_d5",
	"univpll2_d2",
	"dmpll_d2",
	"dmpll_d4"
};

static const char * const mem_parents[] __initconst = {
	"clk26m",
	"dmpll_ck"
};

static const char * const ddrphycfg_parents[] __initconst = {
	"clk26m",
	"syspll1_d8"
};

static const char * const mm_parents[] __initconst = {
	"clk26m",
	"vencpll_d2",
	"main_h364m",
	"syspll1_d2",
	"syspll_d5",
	"syspll1_d4",
	"univpll1_d2",
	"univpll2_d2",
	"dmpll_d2"
};

static const char * const pwm_parents[] __initconst = {
	"clk26m",
	"univpll2_d4",
	"univpll3_d2",
	"univpll1_d4"
};

static const char * const vdec_parents[] __initconst = {
	"clk26m",
	"vcodecpll_ck",
	"tvdpll_445p5m",
	"univpll_d3",
	"vencpll_d2",
	"syspll_d3",
	"univpll1_d2",
	"mmpll_d2",
	"dmpll_d2",
	"dmpll_d4"
};

static const char * const venc_parents[] __initconst = {
	"clk26m",
	"vcodecpll_ck",
	"tvdpll_445p5m",
	"univpll_d3",
	"vencpll_d2",
	"syspll_d3",
	"univpll1_d2",
	"univpll2_d2",
	"dmpll_d2",
	"dmpll_d4"
};

static const char * const mfg_parents[] __initconst = {
	"clk26m",
	"mmpll_ck",
	"dmpll_ck",
	"clk26m",
	"clk26m",
	"clk26m",
	"clk26m",
	"clk26m",
	"clk26m",
	"syspll_d3",
	"syspll1_d2",
	"syspll_d5",
	"univpll_d3",
	"univpll1_d2",
	"univpll_d5",
	"univpll2_d2"
};

static const char * const camtg_parents[] __initconst = {
	"clk26m",
	"univpll_d26",
	"univpll2_d2",
	"syspll3_d2",
	"syspll3_d4",
	"univpll1_d4"
};

static const char * const uart_parents[] __initconst = {
	"clk26m",
	"univpll2_d8"
};

static const char * const spi_parents[] __initconst = {
	"clk26m",
	"syspll3_d2",
	"syspll1_d4",
	"syspll4_d2",
	"univpll3_d2",
	"univpll2_d4",
	"univpll1_d8"
};

static const char * const usb20_parents[] __initconst = {
	"clk26m",
	"univpll1_d8",
	"univpll3_d4"
};

static const char * const usb30_parents[] __initconst = {
	"clk26m",
	"univpll3_d2",
	"usb_syspll_125m",
	"univpll2_d4"
};

static const char * const msdc50_0_h_parents[] __initconst = {
	"clk26m",
	"syspll1_d2",
	"syspll2_d2",
	"syspll4_d2",
	"univpll_d5",
	"univpll1_d4"
};

static const char * const msdc50_0_parents[] __initconst = {
	"clk26m",
	"msdcpll_ck",
	"msdcpll_d2",
	"univpll1_d4",
	"syspll2_d2",
	"syspll_d7",
	"msdcpll_d4",
	"vencpll_d4",
	"tvdpll_ck",
	"univpll_d2",
	"univpll1_d2",
	"mmpll_ck",
	"msdcpll2_ck",
	"msdcpll2_d2",
	"msdcpll2_d4"
};

static const char * const msdc30_1_parents[] __initconst = {
	"clk26m",
	"univpll2_d2",
	"msdcpll_d4",
	"univpll1_d4",
	"syspll2_d2",
	"syspll_d7",
	"univpll_d7",
	"vencpll_d4"
};

static const char * const msdc30_2_parents[] __initconst = {
	"clk26m",
	"univpll2_d2",
	"msdcpll_d4",
	"univpll1_d4",
	"syspll2_d2",
	"syspll_d7",
	"univpll_d7",
	"vencpll_d2"
};

static const char * const msdc30_3_parents[] __initconst = {
	"clk26m",
	"msdcpll2_ck",
	"msdcpll2_d2",
	"univpll2_d2",
	"msdcpll2_d4",
	"msdcpll_d4",
	"univpll1_d4",
	"syspll2_d2",
	"syspll_d7",
	"univpll_d7",
	"vencpll_d4",
	"msdcpll_ck",
	"msdcpll_d2",
	"msdcpll_d4"
};

static const char * const audio_parents[] __initconst = {
	"clk26m",
	"syspll3_d4",
	"syspll4_d4",
	"syspll1_d16"
};

static const char * const aud_intbus_parents[] __initconst = {
	"clk26m",
	"syspll1_d4",
	"syspll4_d2",
	"univpll3_d2",
	"univpll2_d8",
	"dmpll_d4",
	"dmpll_d8"
};

static const char * const pmicspi_parents[] __initconst = {
	"clk26m",
	"syspll1_d8",
	"syspll3_d4",
	"syspll1_d16",
	"univpll3_d4",
	"univpll_d26",
	"dmpll_d8",
	"dmpll_d16"
};

static const char * const scp_parents[] __initconst = {
	"clk26m",
	"syspll1_d2",
	"univpll_d5",
	"syspll_d5",
	"dmpll_d2",
	"dmpll_d4"
};

static const char * const atb_parents[] __initconst = {
	"clk26m",
	"syspll1_d2",
	"univpll_d5",
	"dmpll_d2"
};

static const char * const venc_lt_parents[] __initconst = {
	"clk26m",
	"univpll_d3",
	"vcodecpll_ck",
	"tvdpll_445p5m",
	"vencpll_d2",
	"syspll_d3",
	"univpll1_d2",
	"univpll2_d2",
	"syspll1_d2",
	"univpll_d5",
	"vcodecpll_370p5",
	"dmpll_ck"
};

static const char * const dpi0_parents[] __initconst = {
	"clk26m",
	"tvdpll_d2",
	"tvdpll_d4",
	"clk26m",
	"clk26m",
	"tvdpll_d8",
	"tvdpll_d16"
};

static const char * const irda_parents[] __initconst = {
	"clk26m",
	"univpll2_d4",
	"syspll2_d4"
};

static const char * const cci400_parents[] __initconst = {
	"clk26m",
	"vencpll_ck",
	"armca7pll_754m",
	"armca7pll_502m",
	"univpll_d2",
	"syspll_d2",
	"msdcpll_ck",
	"dmpll_ck"
};

static const char * const aud_1_parents[] __initconst = {
	"clk26m",
	"apll1_ck",
	"univpll2_d4",
	"univpll2_d8"
};

static const char * const aud_2_parents[] __initconst = {
	"clk26m",
	"apll2_ck",
	"univpll2_d4",
	"univpll2_d8"
};

static const char * const mem_mfg_in_parents[] __initconst = {
	"clk26m",
	"mmpll_ck",
	"dmpll_ck",
	"clk26m"
};

static const char * const axi_mfg_in_parents[] __initconst = {
	"clk26m",
	"axi_sel",
	"dmpll_d2"
};

static const char * const scam_parents[] __initconst = {
	"clk26m",
	"syspll3_d2",
	"univpll2_d4",
	"dmpll_d4"
};

static const char * const spinfi_ifr_parents[] __initconst = {
	"clk26m",
	"univpll2_d8",
	"univpll3_d4",
	"syspll4_d2",
	"univpll2_d4",
	"univpll3_d2",
	"syspll1_d4",
	"univpll1_d4"
};

static const char * const hdmi_parents[] __initconst = {
	"clk26m",
	"hdmitx_dig_cts",
	"hdmitxpll_d2",
	"hdmitxpll_d3"
};

static const char * const dpilvds_parents[] __initconst = {
	"clk26m",
	"lvdspll",
	"lvdspll_d2",
	"lvdspll_d4",
	"lvdspll_d8",
	"fpc_ck"
};

static const char * const msdc50_2_h_parents[] __initconst = {
	"clk26m",
	"syspll1_d2",
	"syspll2_d2",
	"syspll4_d2",
	"univpll_d5",
	"univpll1_d4"
};

static const char * const hdcp_parents[] __initconst = {
	"clk26m",
	"syspll4_d2",
	"syspll3_d4",
	"univpll2_d4"
};

static const char * const hdcp_24m_parents[] __initconst = {
	"clk26m",
	"univpll_d26",
	"univpll_d52",
	"univpll2_d8"
};

static const char * const rtc_parents[] __initconst = {
	"clkrtc_int",
	"clkrtc_ext",
	"clk26m",
	"univpll3_d8"
};

static const char * const i2s0_m_ck_parents[] __initconst = {
	"apll1_div1",
	"apll2_div1"
};

static const char * const i2s1_m_ck_parents[] __initconst = {
	"apll1_div2",
	"apll2_div2"
};

static const char * const i2s2_m_ck_parents[] __initconst = {
	"apll1_div3",
	"apll2_div3"
};

static const char * const i2s3_m_ck_parents[] __initconst = {
	"apll1_div4",
	"apll2_div4"
};

static const char * const i2s3_b_ck_parents[] __initconst = {
	"apll1_div5",
	"apll2_div5"
};

static const char * const ca53_parents[] __initconst = {
	"clk26m",
	"armca7pll",
	"mainpll",
	"univpll"
};

static const char * const ca72_parents[] __initconst = {
	"clk26m",
	"armca15pll",
	"mainpll",
	"univpll"
};

static const struct mtk_composite cpu_muxes[] __initconst = {
	MUX(CLK_INFRA_CA53SEL, "infra_ca53_sel", ca53_parents, 0x0000, 0, 2),
	MUX(CLK_INFRA_CA72SEL, "infra_ca72_sel", ca72_parents, 0x0000, 2, 2),
};

static const struct mtk_composite top_muxes[] __initconst = {
	/* CLK_CFG_0 */
	MUX(CLK_TOP_AXI_SEL, "axi_sel", axi_parents, 0x0040, 0, 3),
	MUX(CLK_TOP_MEM_SEL, "mem_sel", mem_parents, 0x0040, 8, 1),
	MUX_GATE(CLK_TOP_DDRPHYCFG_SEL, "ddrphycfg_sel", ddrphycfg_parents, 0x0040, 16, 1, 23),
	MUX_GATE(CLK_TOP_MM_SEL, "mm_sel", mm_parents, 0x0040, 24, 4, 31),
	/* CLK_CFG_1 */
	MUX_GATE(CLK_TOP_PWM_SEL, "pwm_sel", pwm_parents, 0x0050, 0, 2, 7),
	MUX_GATE(CLK_TOP_VDEC_SEL, "vdec_sel", vdec_parents, 0x0050, 8, 4, 15),
	MUX_GATE(CLK_TOP_VENC_SEL, "venc_sel", venc_parents, 0x0050, 16, 4, 23),
	MUX_GATE(CLK_TOP_MFG_SEL, "mfg_sel", mfg_parents, 0x0050, 24, 4, 31),
	/* CLK_CFG_2 */
	MUX_GATE(CLK_TOP_CAMTG_SEL, "camtg_sel", camtg_parents, 0x0060, 0, 3, 7),
	MUX_GATE(CLK_TOP_UART_SEL, "uart_sel", uart_parents, 0x0060, 8, 1, 15),
	MUX_GATE(CLK_TOP_SPI_SEL, "spi_sel", spi_parents, 0x0060, 16, 3, 23),
	MUX_GATE(CLK_TOP_USB20_SEL, "usb20_sel", usb20_parents, 0x0060, 24, 2, 31),
	/* CLK_CFG_3 */
	MUX_GATE(CLK_TOP_USB30_SEL, "usb30_sel", usb30_parents, 0x0070, 0, 2, 7),
	MUX_GATE(CLK_TOP_MSDC50_0_H_SEL, "msdc50_0_h_sel", msdc50_0_h_parents, 0x0070, 8, 3, 15),
	MUX_GATE(CLK_TOP_MSDC50_0_SEL, "msdc50_0_sel", msdc50_0_parents, 0x0070, 16, 4, 23),
	MUX_GATE(CLK_TOP_MSDC30_1_SEL, "msdc30_1_sel", msdc30_1_parents, 0x0070, 24, 3, 31),
	/* CLK_CFG_4 */
	MUX_GATE(CLK_TOP_MSDC30_2_SEL, "msdc30_2_sel", msdc30_2_parents, 0x0080, 0, 3, 7),
	MUX_GATE(CLK_TOP_MSDC30_3_SEL, "msdc30_3_sel", msdc30_3_parents, 0x0080, 8, 4, 15),
	MUX_GATE(CLK_TOP_AUDIO_SEL, "audio_sel", audio_parents, 0x0080, 16, 2, 23),
	MUX_GATE(CLK_TOP_AUD_INTBUS_SEL, "aud_intbus_sel", aud_intbus_parents, 0x0080, 24, 3, 31),
	/* CLK_CFG_5 */
	MUX_GATE(CLK_TOP_PMICSPI_SEL, "pmicspi_sel", pmicspi_parents, 0x0090, 0, 3, 7 /* 7:5 */),
	MUX_GATE(CLK_TOP_SCP_SEL, "scp_sel", scp_parents, 0x0090, 8, 3, 15),
	MUX_GATE(CLK_TOP_ATB_SEL, "atb_sel", atb_parents, 0x0090, 16, 2, 23),
	MUX_GATE(CLK_TOP_VENC_LT_SEL, "venclt_sel", venc_lt_parents, 0x0090, 24, 4, 31),
	/* CLK_CFG_6 */
	/*
	 * The dpi0_sel clock should not propagate rate changes to its parent
	 * clock so the dpi driver can have full control over PLL and divider.
	 */
	MUX_GATE_FLAGS(CLK_TOP_DPI0_SEL, "dpi0_sel", dpi0_parents, 0x00a0, 0, 3, 7, 0),
	MUX_GATE(CLK_TOP_IRDA_SEL, "irda_sel", irda_parents, 0x00a0, 8, 2, 15),
	MUX_GATE(CLK_TOP_CCI400_SEL, "cci400_sel", cci400_parents, 0x00a0, 16, 3, 23),
	MUX_GATE(CLK_TOP_AUD_1_SEL, "aud_1_sel", aud_1_parents, 0x00a0, 24, 2, 31),
	/* CLK_CFG_7 */
	MUX_GATE(CLK_TOP_AUD_2_SEL, "aud_2_sel", aud_2_parents, 0x00b0, 0, 2, 7),
	MUX_GATE(CLK_TOP_MEM_MFG_IN_SEL, "mem_mfg_in_sel", mem_mfg_in_parents, 0x00b0, 8, 2, 15),
	MUX_GATE(CLK_TOP_AXI_MFG_IN_SEL, "axi_mfg_in_sel", axi_mfg_in_parents, 0x00b0, 16, 2, 23),
	MUX_GATE(CLK_TOP_SCAM_SEL, "scam_sel", scam_parents, 0x00b0, 24, 2, 31),
	/* CLK_CFG_12 */
	MUX_GATE(CLK_TOP_SPINFI_IFR_SEL, "spinfi_ifr_sel", spinfi_ifr_parents, 0x00c0, 0, 3, 7),
	MUX_GATE(CLK_TOP_HDMI_SEL, "hdmi_sel", hdmi_parents, 0x00c0, 8, 2, 15),
	MUX_GATE(CLK_TOP_DPILVDS_SEL, "dpilvds_sel", dpilvds_parents, 0x00c0, 24, 3, 31),
	/* CLK_CFG_13 */
	MUX_GATE(CLK_TOP_MSDC50_2_H_SEL, "msdc50_2_h_sel", msdc50_2_h_parents, 0x00d0, 0, 3, 7),
	MUX_GATE(CLK_TOP_HDCP_SEL, "hdcp_sel", hdcp_parents, 0x00d0, 8, 2, 15),
	MUX_GATE(CLK_TOP_HDCP_24M_SEL, "hdcp_24m_sel", hdcp_24m_parents, 0x00d0, 16, 2, 23),
	MUX(CLK_TOP_RTC_SEL, "rtc_sel", rtc_parents, 0x00d0, 24, 2),

	DIV_GATE(CLK_TOP_APLL1_DIV0, "apll1_div0", "aud_1_sel", 0x12c, 8, 0x120, 4, 24),
	DIV_GATE(CLK_TOP_APLL1_DIV1, "apll1_div1", "aud_1_sel", 0x12c, 9, 0x124, 8, 0),
	DIV_GATE(CLK_TOP_APLL1_DIV2, "apll1_div2", "aud_1_sel", 0x12c, 10, 0x124, 8, 8),
	DIV_GATE(CLK_TOP_APLL1_DIV3, "apll1_div3", "aud_1_sel", 0x12c, 11, 0x124, 8, 16),
	DIV_GATE(CLK_TOP_APLL1_DIV4, "apll1_div4", "aud_1_sel", 0x12c, 12, 0x124, 8, 24),
	DIV_GATE(CLK_TOP_APLL1_DIV5, "apll1_div5", "apll1_div4", 0x12c, 13, 0x12c, 4, 0),

	DIV_GATE(CLK_TOP_APLL2_DIV0, "apll2_div0", "aud_2_sel", 0x12c, 16, 0x120, 4, 28),
	DIV_GATE(CLK_TOP_APLL2_DIV1, "apll2_div1", "aud_2_sel", 0x12c, 17, 0x128, 8, 0),
	DIV_GATE(CLK_TOP_APLL2_DIV2, "apll2_div2", "aud_2_sel", 0x12c, 18, 0x128, 8, 8),
	DIV_GATE(CLK_TOP_APLL2_DIV3, "apll2_div3", "aud_2_sel", 0x12c, 19, 0x128, 8, 16),
	DIV_GATE(CLK_TOP_APLL2_DIV4, "apll2_div4", "aud_2_sel", 0x12c, 20, 0x128, 8, 24),
	DIV_GATE(CLK_TOP_APLL2_DIV5, "apll2_div5", "apll2_div4", 0x12c, 21, 0x12c, 4, 4),

	MUX(CLK_TOP_I2S0_M_SEL, "i2s0_m_ck_sel", i2s0_m_ck_parents, 0x120, 4, 1),
	MUX(CLK_TOP_I2S1_M_SEL, "i2s1_m_ck_sel", i2s1_m_ck_parents, 0x120, 5, 1),
	MUX(CLK_TOP_I2S2_M_SEL, "i2s2_m_ck_sel", i2s2_m_ck_parents, 0x120, 6, 1),
	MUX(CLK_TOP_I2S3_M_SEL, "i2s3_m_ck_sel", i2s3_m_ck_parents, 0x120, 7, 1),
	MUX(CLK_TOP_I2S3_B_SEL, "i2s3_b_ck_sel", i2s3_b_ck_parents, 0x120, 8, 1),
};

static const struct mtk_gate_regs infra_cg_regs __initconst = {
	.set_ofs = 0x0040,
	.clr_ofs = 0x0044,
	.sta_ofs = 0x0048,
};

#define GATE_ICG(_id, _name, _parent, _shift) {	\
		.id = _id,					\
		.name = _name,					\
		.parent_name = _parent,				\
		.regs = &infra_cg_regs,				\
		.shift = _shift,				\
		.ops = &mtk_clk_gate_ops_setclr,		\
	}

static const struct mtk_gate infra_clks[] __initconst = {
	GATE_ICG(CLK_INFRA_DBGCLK, "infra_dbgclk", "axi_sel", 0),
	GATE_ICG(CLK_INFRA_SMI, "infra_smi", "mm_sel", 1),
	GATE_ICG(CLK_INFRA_AUDIO, "infra_audio", "aud_intbus_sel", 5),
	GATE_ICG(CLK_INFRA_GCE, "infra_gce", "axi_sel", 6),
	GATE_ICG(CLK_INFRA_L2C_SRAM, "infra_l2c_sram", "axi_sel", 7),
	GATE_ICG(CLK_INFRA_M4U, "infra_m4u", "mem_sel", 8),
	GATE_ICG(CLK_INFRA_CPUM, "infra_cpum", "cpum_ck", 15),
	GATE_ICG(CLK_INFRA_KP, "infra_kp", "axi_sel", 16),
	GATE_ICG(CLK_INFRA_CEC, "infra_cec", "clk26m", 18),
	GATE_ICG(CLK_INFRA_PMICSPI, "infra_pmicspi", "pmicspi_sel", 22),
	GATE_ICG(CLK_INFRA_PMICWRAP, "infra_pmicwrap", "axi_sel", 23),
};

static const struct mtk_fixed_factor infra_divs[] __initconst = {
	FACTOR(CLK_INFRA_CLK_13M, "clk13m", "clk26m", 1, 2),
};

static const struct mtk_gate_regs peri0_cg_regs __initconst = {
	.set_ofs = 0x0008,
	.clr_ofs = 0x0010,
	.sta_ofs = 0x0018,
};

static const struct mtk_gate_regs peri1_cg_regs __initconst = {
	.set_ofs = 0x000c,
	.clr_ofs = 0x0014,
	.sta_ofs = 0x001c,
};

#define GATE_PERI0(_id, _name, _parent, _shift) {	\
		.id = _id,					\
		.name = _name,					\
		.parent_name = _parent,				\
		.regs = &peri0_cg_regs,				\
		.shift = _shift,				\
		.ops = &mtk_clk_gate_ops_setclr,		\
	}

#define GATE_PERI1(_id, _name, _parent, _shift) {	\
		.id = _id,					\
		.name = _name,					\
		.parent_name = _parent,				\
		.regs = &peri1_cg_regs,				\
		.shift = _shift,				\
		.ops = &mtk_clk_gate_ops_setclr,		\
	}

static const struct mtk_gate peri_gates[] __initconst = {
	/* PERI0 */
	GATE_PERI0(CLK_PERI_NFI, "peri_nfi", "axi_sel", 0),
	GATE_PERI0(CLK_PERI_THERM, "peri_therm", "axi_sel", 1),
	GATE_PERI0(CLK_PERI_PWM1, "peri_pwm1", "axi_sel", 2),
	GATE_PERI0(CLK_PERI_PWM2, "peri_pwm2", "axi_sel", 3),
	GATE_PERI0(CLK_PERI_PWM3, "peri_pwm3", "axi_sel", 4),
	GATE_PERI0(CLK_PERI_PWM4, "peri_pwm4", "axi_sel", 5),
	GATE_PERI0(CLK_PERI_PWM5, "peri_pwm5", "axi_sel", 6),
	GATE_PERI0(CLK_PERI_PWM6, "peri_pwm6", "axi_sel", 7),
	GATE_PERI0(CLK_PERI_PWM7, "peri_pwm7", "axi_sel", 8),
	GATE_PERI0(CLK_PERI_PWM, "peri_pwm", "axi_sel", 9),
	GATE_PERI0(CLK_PERI_USB0, "peri_usb0", "usb20_sel", 10),
	GATE_PERI0(CLK_PERI_USB1, "peri_usb1", "usb20_sel", 11),
	GATE_PERI0(CLK_PERI_AP_DMA, "peri_ap_dma", "axi_sel", 12),
	GATE_PERI0(CLK_PERI_MSDC30_0, "peri_msdc30_0", "msdc50_0_sel", 13),
	GATE_PERI0(CLK_PERI_MSDC30_1, "peri_msdc30_1", "msdc30_1_sel", 14),
	GATE_PERI0(CLK_PERI_MSDC30_2, "peri_msdc30_2", "msdc30_2_sel", 15),
	GATE_PERI0(CLK_PERI_MSDC30_3, "peri_msdc30_3", "msdc30_3_sel", 16),
	GATE_PERI0(CLK_PERI_NLI_ARB, "peri_nli_arb", "axi_sel", 17),
	GATE_PERI0(CLK_PERI_IRDA, "peri_irda", "irda_sel", 18),
	GATE_PERI0(CLK_PERI_UART0, "peri_uart0", "axi_sel", 19),
	GATE_PERI0(CLK_PERI_UART1, "peri_uart1", "axi_sel", 20),
	GATE_PERI0(CLK_PERI_UART2, "peri_uart2", "axi_sel", 21),
	GATE_PERI0(CLK_PERI_UART3, "peri_uart3", "axi_sel", 22),
	GATE_PERI0(CLK_PERI_I2C0, "peri_i2c0", "axi_sel", 23),
	GATE_PERI0(CLK_PERI_I2C1, "peri_i2c1", "axi_sel", 24),
	GATE_PERI0(CLK_PERI_I2C2, "peri_i2c2", "axi_sel", 25),
	GATE_PERI0(CLK_PERI_I2C3, "peri_i2c3", "axi_sel", 26),
	GATE_PERI0(CLK_PERI_I2C4, "peri_i2c4", "axi_sel", 27),
	GATE_PERI0(CLK_PERI_AUXADC, "peri_auxadc", "clk26m", 28),
	GATE_PERI0(CLK_PERI_SPI0, "peri_spi0", "spi_sel", 29),
	GATE_PERI0(CLK_PERI_I2C5, "peri_i2c5", "axi_sel", 30),
	GATE_PERI0(CLK_PERI_NFIECC, "peri_nfiecc", "axi_sel", 31),
	/* PERI1 */
	GATE_PERI1(CLK_PERI_SPI, "peri_spi", "spi_sel", 0),
	GATE_PERI1(CLK_PERI_IRRX, "peri_irrx", "spi_sel", 1),
	GATE_PERI1(CLK_PERI_I2C6, "peri_i2c6", "axi_sel", 2),
};

static const char * const uart_ck_sel_parents[] __initconst = {
	"clk26m",
	"uart_sel",
};

static const struct mtk_composite peri_clks[] __initconst = {
	MUX(CLK_PERI_UART0_SEL, "uart0_ck_sel", uart_ck_sel_parents, 0x40c, 0, 1),
	MUX(CLK_PERI_UART1_SEL, "uart1_ck_sel", uart_ck_sel_parents, 0x40c, 1, 1),
	MUX(CLK_PERI_UART2_SEL, "uart2_ck_sel", uart_ck_sel_parents, 0x40c, 2, 1),
	MUX(CLK_PERI_UART3_SEL, "uart3_ck_sel", uart_ck_sel_parents, 0x40c, 3, 1),
};

static const struct mtk_gate_regs cg_regs_4_8_0 __initconst = {
	.set_ofs = 0x0004,
	.clr_ofs = 0x0008,
	.sta_ofs = 0x0000,
};

#define GATE_IMG(_id, _name, _parent, _shift) {			\
		.id = _id,					\
		.name = _name,					\
		.parent_name = _parent,				\
		.regs = &cg_regs_4_8_0,				\
		.shift = _shift,				\
		.ops = &mtk_clk_gate_ops_setclr,		\
	}

static const struct mtk_gate img_clks[] __initconst = {
	GATE_IMG(CLK_IMG_LARB2_SMI, "img_larb2_smi", "mm_sel", 0),
	GATE_IMG(CLK_IMG_CAM_SMI, "img_cam_smi", "mm_sel", 5),
	GATE_IMG(CLK_IMG_CAM_CAM, "img_cam_cam", "mm_sel", 6),
	GATE_IMG(CLK_IMG_SEN_TG, "img_sen_tg", "camtg_sel", 7),
	GATE_IMG(CLK_IMG_SEN_CAM, "img_sen_cam", "mm_sel", 8),
	GATE_IMG(CLK_IMG_CAM_SV, "img_cam_sv", "mm_sel", 9),
	GATE_IMG(CLK_IMG_FD, "img_fd", "mm_sel", 11),
};

static const struct mtk_gate_regs vdec0_cg_regs __initconst = {
	.set_ofs = 0x0000,
	.clr_ofs = 0x0004,
	.sta_ofs = 0x0000,
};

static const struct mtk_gate_regs vdec1_cg_regs __initconst = {
	.set_ofs = 0x0008,
	.clr_ofs = 0x000c,
	.sta_ofs = 0x0008,
};

#define GATE_VDEC0(_id, _name, _parent, _shift) {		\
		.id = _id,					\
		.name = _name,					\
		.parent_name = _parent,				\
		.regs = &vdec0_cg_regs,				\
		.shift = _shift,				\
		.ops = &mtk_clk_gate_ops_setclr_inv,		\
	}

#define GATE_VDEC1(_id, _name, _parent, _shift) {		\
		.id = _id,					\
		.name = _name,					\
		.parent_name = _parent,				\
		.regs = &vdec1_cg_regs,				\
		.shift = _shift,				\
		.ops = &mtk_clk_gate_ops_setclr_inv,		\
	}

static const struct mtk_gate vdec_clks[] __initconst = {
	GATE_VDEC0(CLK_VDEC_CKEN, "vdec_cken", "vdec_sel", 0),
	GATE_VDEC1(CLK_VDEC_LARB_CKEN, "vdec_larb_cken", "mm_sel", 0),
};

#define GATE_VENC(_id, _name, _parent, _shift) {		\
		.id = _id,					\
		.name = _name,					\
		.parent_name = _parent,				\
		.regs = &cg_regs_4_8_0,				\
		.shift = _shift,				\
		.ops = &mtk_clk_gate_ops_setclr_inv,		\
	}

static const struct mtk_gate venc_clks[] __initconst = {
	GATE_VENC(CLK_VENC_CKE0, "venc_cke0", "mm_sel", 0),
	GATE_VENC(CLK_VENC_CKE1, "venc_cke1", "venc_sel", 4),
	GATE_VENC(CLK_VENC_CKE2, "venc_cke2", "venc_sel", 8),
	GATE_VENC(CLK_VENC_CKE3, "venc_cke3", "venc_sel", 12),
};

#define GATE_VENCLT(_id, _name, _parent, _shift) {		\
		.id = _id,					\
		.name = _name,					\
		.parent_name = _parent,				\
		.regs = &cg_regs_4_8_0,				\
		.shift = _shift,				\
		.ops = &mtk_clk_gate_ops_setclr_inv,		\
	}

static const struct mtk_gate venclt_clks[] __initconst = {
	GATE_VENCLT(CLK_VENCLT_CKE0, "venclt_cke0", "mm_sel", 0),
	GATE_VENCLT(CLK_VENCLT_CKE1, "venclt_cke1", "venclt_sel", 4),
};

static struct clk_hw_onecell_data *mt8173_top_clk_data __initdata;
static struct clk_hw_onecell_data *mt8173_pll_clk_data __initdata;

static void __init mtk_clk_enable_critical(void)
{
	if (!mt8173_top_clk_data || !mt8173_pll_clk_data)
		return;

	clk_prepare_enable(mt8173_pll_clk_data->hws[CLK_APMIXED_ARMCA15PLL]->clk);
	clk_prepare_enable(mt8173_pll_clk_data->hws[CLK_APMIXED_ARMCA7PLL]->clk);
	clk_prepare_enable(mt8173_top_clk_data->hws[CLK_TOP_MEM_SEL]->clk);
	clk_prepare_enable(mt8173_top_clk_data->hws[CLK_TOP_DDRPHYCFG_SEL]->clk);
	clk_prepare_enable(mt8173_top_clk_data->hws[CLK_TOP_CCI400_SEL]->clk);
	clk_prepare_enable(mt8173_top_clk_data->hws[CLK_TOP_RTC_SEL]->clk);
}

static void __init mtk_topckgen_init(struct device_node *node)
{
	struct clk_hw_onecell_data *clk_data;
	void __iomem *base;
	int r;

	base = of_iomap(node, 0);
	if (!base) {
		pr_err("%s(): ioremap failed\n", __func__);
		return;
	}

	mt8173_top_clk_data = clk_data = mtk_alloc_clk_data(CLK_TOP_NR_CLK);

	mtk_clk_register_fixed_clks(fixed_clks, ARRAY_SIZE(fixed_clks), clk_data);
	mtk_clk_register_factors(top_divs, ARRAY_SIZE(top_divs), clk_data);
	mtk_clk_register_composites(top_muxes, ARRAY_SIZE(top_muxes), base,
			&mt8173_clk_lock, clk_data);

	r = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	if (r)
		pr_err("%s(): could not register clock provider: %d\n",
			__func__, r);

	mtk_clk_enable_critical();
}
CLK_OF_DECLARE(mtk_topckgen, "mediatek,mt8173-topckgen", mtk_topckgen_init);

static void __init mtk_infrasys_init(struct device_node *node)
{
	struct clk_hw_onecell_data *clk_data;
	int r;

	clk_data = mtk_alloc_clk_data(CLK_INFRA_NR_CLK);

	mtk_clk_register_gates(node, infra_clks, ARRAY_SIZE(infra_clks),
						clk_data);
	mtk_clk_register_factors(infra_divs, ARRAY_SIZE(infra_divs), clk_data);

	mtk_clk_register_cpumuxes(node, cpu_muxes, ARRAY_SIZE(cpu_muxes),
				  clk_data);

	r = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	if (r)
		pr_err("%s(): could not register clock provider: %d\n",
			__func__, r);

	mtk_register_reset_controller(node, 2, 0x30);
}
CLK_OF_DECLARE(mtk_infrasys, "mediatek,mt8173-infracfg", mtk_infrasys_init);

static void __init mtk_pericfg_init(struct device_node *node)
{
	struct clk_hw_onecell_data *clk_data;
	int r;
	void __iomem *base;

	base = of_iomap(node, 0);
	if (!base) {
		pr_err("%s(): ioremap failed\n", __func__);
		return;
	}

	clk_data = mtk_alloc_clk_data(CLK_PERI_NR_CLK);

	mtk_clk_register_gates(node, peri_gates, ARRAY_SIZE(peri_gates),
						clk_data);
	mtk_clk_register_composites(peri_clks, ARRAY_SIZE(peri_clks), base,
			&mt8173_clk_lock, clk_data);

	r = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	if (r)
		pr_err("%s(): could not register clock provider: %d\n",
			__func__, r);

	mtk_register_reset_controller(node, 2, 0);
}
CLK_OF_DECLARE(mtk_pericfg, "mediatek,mt8173-pericfg", mtk_pericfg_init);

struct mtk_clk_usb {
	int id;
	const char *name;
	const char *parent;
	u32 reg_ofs;
};

#define APMIXED_USB(_id, _name, _parent, _reg_ofs) {			\
		.id = _id,						\
		.name = _name,						\
		.parent = _parent,					\
		.reg_ofs = _reg_ofs,					\
	}

static const struct mtk_clk_usb apmixed_usb[] __initconst = {
	APMIXED_USB(CLK_APMIXED_REF2USB_TX, "ref2usb_tx", "clk26m", 0x8),
};

#define MT8173_PLL_FMAX		(3000UL * MHZ)

#define CON0_MT8173_RST_BAR	BIT(24)

#define PLL_B(_id, _name, _reg, _pwr_reg, _en_mask, _flags, _pcwbits,	\
			_pd_reg, _pd_shift, _tuner_reg, _pcw_reg,	\
			_pcw_shift, _div_table) {			\
		.id = _id,						\
		.name = _name,						\
		.reg = _reg,						\
		.pwr_reg = _pwr_reg,					\
		.en_mask = _en_mask,					\
		.flags = _flags,					\
		.rst_bar_mask = CON0_MT8173_RST_BAR,			\
		.fmax = MT8173_PLL_FMAX,				\
		.pcwbits = _pcwbits,					\
		.pd_reg = _pd_reg,					\
		.pd_shift = _pd_shift,					\
		.tuner_reg = _tuner_reg,				\
		.pcw_reg = _pcw_reg,					\
		.pcw_shift = _pcw_shift,				\
		.div_table = _div_table,				\
	}

#define PLL(_id, _name, _reg, _pwr_reg, _en_mask, _flags, _pcwbits,	\
			_pd_reg, _pd_shift, _tuner_reg, _pcw_reg,	\
			_pcw_shift)					\
		PLL_B(_id, _name, _reg, _pwr_reg, _en_mask, _flags, _pcwbits, \
			_pd_reg, _pd_shift, _tuner_reg, _pcw_reg, _pcw_shift, \
			NULL)

static const struct mtk_pll_div_table mmpll_div_table[] = {
	{ .div = 0, .freq = MT8173_PLL_FMAX },
	{ .div = 1, .freq = 1000000000 },
	{ .div = 2, .freq = 702000000 },
	{ .div = 3, .freq = 253500000 },
	{ .div = 4, .freq = 126750000 },
	{ } /* sentinel */
};

static const struct mtk_pll_data plls[] = {
	PLL(CLK_APMIXED_ARMCA15PLL, "armca15pll", 0x200, 0x20c, 0, 0, 21, 0x204, 24, 0x0, 0x204, 0),
	PLL(CLK_APMIXED_ARMCA7PLL, "armca7pll", 0x210, 0x21c, 0, 0, 21, 0x214, 24, 0x0, 0x214, 0),
	PLL(CLK_APMIXED_MAINPLL, "mainpll", 0x220, 0x22c, 0xf0000100, HAVE_RST_BAR, 21, 0x220, 4, 0x0, 0x224, 0),
	PLL(CLK_APMIXED_UNIVPLL, "univpll", 0x230, 0x23c, 0xfe000000, HAVE_RST_BAR, 7, 0x230, 4, 0x0, 0x234, 14),
	PLL_B(CLK_APMIXED_MMPLL, "mmpll", 0x240, 0x24c, 0, 0, 21, 0x244, 24, 0x0, 0x244, 0, mmpll_div_table),
	PLL(CLK_APMIXED_MSDCPLL, "msdcpll", 0x250, 0x25c, 0, 0, 21, 0x250, 4, 0x0, 0x254, 0),
	PLL(CLK_APMIXED_VENCPLL, "vencpll", 0x260, 0x26c, 0, 0, 21, 0x260, 4, 0x0, 0x264, 0),
	PLL(CLK_APMIXED_TVDPLL, "tvdpll", 0x270, 0x27c, 0, 0, 21, 0x270, 4, 0x0, 0x274, 0),
	PLL(CLK_APMIXED_MPLL, "mpll", 0x280, 0x28c, 0, 0, 21, 0x280, 4, 0x0, 0x284, 0),
	PLL(CLK_APMIXED_VCODECPLL, "vcodecpll", 0x290, 0x29c, 0, 0, 21, 0x290, 4, 0x0, 0x294, 0),
	PLL(CLK_APMIXED_APLL1, "apll1", 0x2a0, 0x2b0, 0, 0, 31, 0x2a0, 4, 0x2a4, 0x2a4, 0),
	PLL(CLK_APMIXED_APLL2, "apll2", 0x2b4, 0x2c4, 0, 0, 31, 0x2b4, 4, 0x2b8, 0x2b8, 0),
	PLL(CLK_APMIXED_LVDSPLL, "lvdspll", 0x2d0, 0x2dc, 0, 0, 21, 0x2d0, 4, 0x0, 0x2d4, 0),
	PLL(CLK_APMIXED_MSDCPLL2, "msdcpll2", 0x2f0, 0x2fc, 0, 0, 21, 0x2f0, 4, 0x0, 0x2f4, 0),
};

static void __init mtk_apmixedsys_init(struct device_node *node)
{
	struct clk_hw_onecell_data *clk_data;
	void __iomem *base;
	struct clk_hw *hw;
	int r, i;

	base = of_iomap(node, 0);
	if (!base) {
		pr_err("%s(): ioremap failed\n", __func__);
		return;
	}

	mt8173_pll_clk_data = clk_data = mtk_alloc_clk_data(CLK_APMIXED_NR_CLK);
	if (!clk_data) {
		iounmap(base);
		return;
	}

	mtk_clk_register_plls(node, plls, ARRAY_SIZE(plls), clk_data);

	for (i = 0; i < ARRAY_SIZE(apmixed_usb); i++) {
		const struct mtk_clk_usb *cku = &apmixed_usb[i];

		hw = mtk_clk_register_ref2usb_tx(cku->name, cku->parent, base + cku->reg_ofs);
		if (IS_ERR(hw)) {
			pr_err("Failed to register clk %s: %ld\n", cku->name, PTR_ERR(hw));
			continue;
		}

		clk_data->hws[cku->id] = hw;
	}

	hw = clk_hw_register_divider(NULL, "hdmi_ref", "tvdpll_594m", 0,
				     base + 0x40, 16, 3, CLK_DIVIDER_POWER_OF_TWO,
				     NULL);
	clk_data->hws[CLK_APMIXED_HDMI_REF] = hw;

	r = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	if (r)
		pr_err("%s(): could not register clock provider: %d\n",
			__func__, r);

	mtk_clk_enable_critical();
}
CLK_OF_DECLARE(mtk_apmixedsys, "mediatek,mt8173-apmixedsys",
		mtk_apmixedsys_init);

static void __init mtk_imgsys_init(struct device_node *node)
{
	struct clk_hw_onecell_data *clk_data;
	int r;

	clk_data = mtk_alloc_clk_data(CLK_IMG_NR_CLK);

	mtk_clk_register_gates(node, img_clks, ARRAY_SIZE(img_clks),
						clk_data);

	r = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);

	if (r)
		pr_err("%s(): could not register clock provider: %d\n",
			__func__, r);
}
CLK_OF_DECLARE(mtk_imgsys, "mediatek,mt8173-imgsys", mtk_imgsys_init);

static void __init mtk_vdecsys_init(struct device_node *node)
{
	struct clk_hw_onecell_data *clk_data;
	int r;

	clk_data = mtk_alloc_clk_data(CLK_VDEC_NR_CLK);

	mtk_clk_register_gates(node, vdec_clks, ARRAY_SIZE(vdec_clks),
						clk_data);

	r = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	if (r)
		pr_err("%s(): could not register clock provider: %d\n",
			__func__, r);
}
CLK_OF_DECLARE(mtk_vdecsys, "mediatek,mt8173-vdecsys", mtk_vdecsys_init);

static void __init mtk_vencsys_init(struct device_node *node)
{
	struct clk_hw_onecell_data *clk_data;
	int r;

	clk_data = mtk_alloc_clk_data(CLK_VENC_NR_CLK);

	mtk_clk_register_gates(node, venc_clks, ARRAY_SIZE(venc_clks),
						clk_data);

	r = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	if (r)
		pr_err("%s(): could not register clock provider: %d\n",
			__func__, r);
}
CLK_OF_DECLARE(mtk_vencsys, "mediatek,mt8173-vencsys", mtk_vencsys_init);

static void __init mtk_vencltsys_init(struct device_node *node)
{
	struct clk_hw_onecell_data *clk_data;
	int r;

	clk_data = mtk_alloc_clk_data(CLK_VENCLT_NR_CLK);

	mtk_clk_register_gates(node, venclt_clks, ARRAY_SIZE(venclt_clks),
						clk_data);

	r = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	if (r)
		pr_err("%s(): could not register clock provider: %d\n",
			__func__, r);
}
CLK_OF_DECLARE(mtk_vencltsys, "mediatek,mt8173-vencltsys", mtk_vencltsys_init);
