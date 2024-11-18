// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Copyright (c) 2022 Collabora Ltd.
 * Author: AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <dt-bindings/clock/mt8173-clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include "clk-gate.h"
#include "clk-mtk.h"
#include "clk-mux.h"

/*
 * For some clocks, we don't care what their actual rates are. And these
 * clocks may change their rate on different products or different scenarios.
 * So we model these clocks' rate as 0, to denote it's not an actual rate.
 */
#define DUMMY_RATE	0

#define TOP_MUX_GATE_NOSR(_id, _name, _parents, _reg, _shift, _width, _gate, _flags) \
		MUX_GATE_CLR_SET_UPD_FLAGS(_id, _name, _parents, _reg,		\
			(_reg + 0x4), (_reg + 0x8), _shift, _width,		\
			_gate, 0, -1, _flags)

#define TOP_MUX_GATE(_id, _name, _parents, _reg, _shift, _width, _gate, _flags)	\
		TOP_MUX_GATE_NOSR(_id, _name, _parents, _reg, _shift, _width,	\
				  _gate, CLK_SET_RATE_PARENT | _flags)

static DEFINE_SPINLOCK(mt8173_top_clk_lock);

static const char * const axi_parents[] = {
	"clk26m",
	"syspll1_d2",
	"syspll_d5",
	"syspll1_d4",
	"univpll_d5",
	"univpll2_d2",
	"dmpll_d2",
	"dmpll_d4"
};

static const char * const mem_parents[] = {
	"clk26m",
	"dmpll_ck"
};

static const char * const ddrphycfg_parents[] = {
	"clk26m",
	"syspll1_d8"
};

static const char * const mm_parents[] = {
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

static const char * const pwm_parents[] = {
	"clk26m",
	"univpll2_d4",
	"univpll3_d2",
	"univpll1_d4"
};

static const char * const vdec_parents[] = {
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

static const char * const venc_parents[] = {
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

static const char * const mfg_parents[] = {
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

static const char * const camtg_parents[] = {
	"clk26m",
	"univpll_d26",
	"univpll2_d2",
	"syspll3_d2",
	"syspll3_d4",
	"univpll1_d4"
};

static const char * const uart_parents[] = {
	"clk26m",
	"univpll2_d8"
};

static const char * const spi_parents[] = {
	"clk26m",
	"syspll3_d2",
	"syspll1_d4",
	"syspll4_d2",
	"univpll3_d2",
	"univpll2_d4",
	"univpll1_d8"
};

static const char * const usb20_parents[] = {
	"clk26m",
	"univpll1_d8",
	"univpll3_d4"
};

static const char * const usb30_parents[] = {
	"clk26m",
	"univpll3_d2",
	"usb_syspll_125m",
	"univpll2_d4"
};

static const char * const msdc50_0_h_parents[] = {
	"clk26m",
	"syspll1_d2",
	"syspll2_d2",
	"syspll4_d2",
	"univpll_d5",
	"univpll1_d4"
};

static const char * const msdc50_0_parents[] = {
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

static const char * const msdc30_1_parents[] = {
	"clk26m",
	"univpll2_d2",
	"msdcpll_d4",
	"univpll1_d4",
	"syspll2_d2",
	"syspll_d7",
	"univpll_d7",
	"vencpll_d4"
};

static const char * const msdc30_2_parents[] = {
	"clk26m",
	"univpll2_d2",
	"msdcpll_d4",
	"univpll1_d4",
	"syspll2_d2",
	"syspll_d7",
	"univpll_d7",
	"vencpll_d2"
};

static const char * const msdc30_3_parents[] = {
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

static const char * const audio_parents[] = {
	"clk26m",
	"syspll3_d4",
	"syspll4_d4",
	"syspll1_d16"
};

static const char * const aud_intbus_parents[] = {
	"clk26m",
	"syspll1_d4",
	"syspll4_d2",
	"univpll3_d2",
	"univpll2_d8",
	"dmpll_d4",
	"dmpll_d8"
};

static const char * const pmicspi_parents[] = {
	"clk26m",
	"syspll1_d8",
	"syspll3_d4",
	"syspll1_d16",
	"univpll3_d4",
	"univpll_d26",
	"dmpll_d8",
	"dmpll_d16"
};

static const char * const scp_parents[] = {
	"clk26m",
	"syspll1_d2",
	"univpll_d5",
	"syspll_d5",
	"dmpll_d2",
	"dmpll_d4"
};

static const char * const atb_parents[] = {
	"clk26m",
	"syspll1_d2",
	"univpll_d5",
	"dmpll_d2"
};

static const char * const venc_lt_parents[] = {
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

static const char * const dpi0_parents[] = {
	"clk26m",
	"tvdpll_d2",
	"tvdpll_d4",
	"clk26m",
	"clk26m",
	"tvdpll_d8",
	"tvdpll_d16"
};

static const char * const irda_parents[] = {
	"clk26m",
	"univpll2_d4",
	"syspll2_d4"
};

static const char * const cci400_parents[] = {
	"clk26m",
	"vencpll_ck",
	"armca7pll_754m",
	"armca7pll_502m",
	"univpll_d2",
	"syspll_d2",
	"msdcpll_ck",
	"dmpll_ck"
};

static const char * const aud_1_parents[] = {
	"clk26m",
	"apll1_ck",
	"univpll2_d4",
	"univpll2_d8"
};

static const char * const aud_2_parents[] = {
	"clk26m",
	"apll2_ck",
	"univpll2_d4",
	"univpll2_d8"
};

static const char * const mem_mfg_in_parents[] = {
	"clk26m",
	"mmpll_ck",
	"dmpll_ck",
	"clk26m"
};

static const char * const axi_mfg_in_parents[] = {
	"clk26m",
	"axi_sel",
	"dmpll_d2"
};

static const char * const scam_parents[] = {
	"clk26m",
	"syspll3_d2",
	"univpll2_d4",
	"dmpll_d4"
};

static const char * const spinfi_ifr_parents[] = {
	"clk26m",
	"univpll2_d8",
	"univpll3_d4",
	"syspll4_d2",
	"univpll2_d4",
	"univpll3_d2",
	"syspll1_d4",
	"univpll1_d4"
};

static const char * const hdmi_parents[] = {
	"clk26m",
	"hdmitx_dig_cts",
	"hdmitxpll_d2",
	"hdmitxpll_d3"
};

static const char * const dpilvds_parents[] = {
	"clk26m",
	"lvdspll",
	"lvdspll_d2",
	"lvdspll_d4",
	"lvdspll_d8",
	"fpc_ck"
};

static const char * const msdc50_2_h_parents[] = {
	"clk26m",
	"syspll1_d2",
	"syspll2_d2",
	"syspll4_d2",
	"univpll_d5",
	"univpll1_d4"
};

static const char * const hdcp_parents[] = {
	"clk26m",
	"syspll4_d2",
	"syspll3_d4",
	"univpll2_d4"
};

static const char * const hdcp_24m_parents[] = {
	"clk26m",
	"univpll_d26",
	"univpll_d52",
	"univpll2_d8"
};

static const char * const rtc_parents[] = {
	"clkrtc_int",
	"clkrtc_ext",
	"clk26m",
	"univpll3_d8"
};

static const char * const i2s0_m_ck_parents[] = {
	"apll1_div1",
	"apll2_div1"
};

static const char * const i2s1_m_ck_parents[] = {
	"apll1_div2",
	"apll2_div2"
};

static const char * const i2s2_m_ck_parents[] = {
	"apll1_div3",
	"apll2_div3"
};

static const char * const i2s3_m_ck_parents[] = {
	"apll1_div4",
	"apll2_div4"
};

static const char * const i2s3_b_ck_parents[] = {
	"apll1_div5",
	"apll2_div5"
};

static const struct mtk_fixed_clk fixed_clks[] = {
	FIXED_CLK(CLK_DUMMY, "topck_dummy", "clk26m", DUMMY_RATE),
	FIXED_CLK(CLK_TOP_CLKPH_MCK_O, "clkph_mck_o", "clk26m", DUMMY_RATE),
	FIXED_CLK(CLK_TOP_USB_SYSPLL_125M, "usb_syspll_125m", "clk26m", 125 * MHZ),
	FIXED_CLK(CLK_TOP_DSI0_DIG, "dsi0_dig", "clk26m", DUMMY_RATE),
	FIXED_CLK(CLK_TOP_DSI1_DIG, "dsi1_dig", "clk26m", DUMMY_RATE),
	FIXED_CLK(CLK_TOP_LVDS_PXL, "lvds_pxl", "lvdspll", DUMMY_RATE),
	FIXED_CLK(CLK_TOP_LVDS_CTS, "lvds_cts", "lvdspll", DUMMY_RATE),
};

static const struct mtk_fixed_factor top_divs[] = {
	FACTOR(CLK_TOP_ARMCA7PLL_754M, "armca7pll_754m", "armca7pll", 1, 2),
	FACTOR(CLK_TOP_ARMCA7PLL_502M, "armca7pll_502m", "armca7pll", 1, 3),

	FACTOR_FLAGS(CLK_TOP_MAIN_H546M, "main_h546m", "mainpll", 1, 2, 0),
	FACTOR_FLAGS(CLK_TOP_MAIN_H364M, "main_h364m", "mainpll", 1, 3, 0),
	FACTOR_FLAGS(CLK_TOP_MAIN_H218P4M, "main_h218p4m", "mainpll", 1, 5, 0),
	FACTOR_FLAGS(CLK_TOP_MAIN_H156M, "main_h156m", "mainpll", 1, 7, 0),

	FACTOR(CLK_TOP_TVDPLL_445P5M, "tvdpll_445p5m", "tvdpll", 1, 4),
	FACTOR(CLK_TOP_TVDPLL_594M, "tvdpll_594m", "tvdpll", 1, 3),

	FACTOR_FLAGS(CLK_TOP_UNIV_624M, "univ_624m", "univpll", 1, 2, 0),
	FACTOR_FLAGS(CLK_TOP_UNIV_416M, "univ_416m", "univpll", 1, 3, 0),
	FACTOR_FLAGS(CLK_TOP_UNIV_249P6M, "univ_249p6m", "univpll", 1, 5, 0),
	FACTOR_FLAGS(CLK_TOP_UNIV_178P3M, "univ_178p3m", "univpll", 1, 7, 0),
	FACTOR_FLAGS(CLK_TOP_UNIV_48M, "univ_48m", "univpll", 1, 26, 0),

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

	FACTOR_FLAGS(CLK_TOP_SYSPLL_D2, "syspll_d2", "main_h546m", 1, 1, 0),
	FACTOR_FLAGS(CLK_TOP_SYSPLL1_D2, "syspll1_d2", "main_h546m", 1, 2, 0),
	FACTOR_FLAGS(CLK_TOP_SYSPLL1_D4, "syspll1_d4", "main_h546m", 1, 4, 0),
	FACTOR_FLAGS(CLK_TOP_SYSPLL1_D8, "syspll1_d8", "main_h546m", 1, 8, 0),
	FACTOR_FLAGS(CLK_TOP_SYSPLL1_D16, "syspll1_d16", "main_h546m", 1, 16, 0),
	FACTOR_FLAGS(CLK_TOP_SYSPLL_D3, "syspll_d3", "main_h364m", 1, 1, 0),
	FACTOR_FLAGS(CLK_TOP_SYSPLL2_D2, "syspll2_d2", "main_h364m", 1, 2, 0),
	FACTOR_FLAGS(CLK_TOP_SYSPLL2_D4, "syspll2_d4", "main_h364m", 1, 4, 0),
	FACTOR_FLAGS(CLK_TOP_SYSPLL_D5, "syspll_d5", "main_h218p4m", 1, 1, 0),
	FACTOR_FLAGS(CLK_TOP_SYSPLL3_D2, "syspll3_d2", "main_h218p4m", 1, 2, 0),
	FACTOR_FLAGS(CLK_TOP_SYSPLL3_D4, "syspll3_d4", "main_h218p4m", 1, 4, 0),
	FACTOR_FLAGS(CLK_TOP_SYSPLL_D7, "syspll_d7", "main_h156m", 1, 1, 0),
	FACTOR_FLAGS(CLK_TOP_SYSPLL4_D2, "syspll4_d2", "main_h156m", 1, 2, 0),
	FACTOR_FLAGS(CLK_TOP_SYSPLL4_D4, "syspll4_d4", "main_h156m", 1, 4, 0),

	FACTOR(CLK_TOP_TVDPLL, "tvdpll_ck", "tvdpll_594m", 1, 1),
	FACTOR(CLK_TOP_TVDPLL_D2, "tvdpll_d2", "tvdpll_594m", 1, 2),
	FACTOR(CLK_TOP_TVDPLL_D4, "tvdpll_d4", "tvdpll_594m", 1, 4),
	FACTOR(CLK_TOP_TVDPLL_D8, "tvdpll_d8", "tvdpll_594m", 1, 8),
	FACTOR(CLK_TOP_TVDPLL_D16, "tvdpll_d16", "tvdpll_594m", 1, 16),

	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D2, "univpll_d2", "univ_624m", 1, 1, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL1_D2, "univpll1_d2", "univ_624m", 1, 2, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL1_D4, "univpll1_d4", "univ_624m", 1, 4, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL1_D8, "univpll1_d8", "univ_624m", 1, 8, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D3, "univpll_d3", "univ_416m", 1, 1, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL2_D2, "univpll2_d2", "univ_416m", 1, 2, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL2_D4, "univpll2_d4", "univ_416m", 1, 4, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL2_D8, "univpll2_d8", "univ_416m", 1, 8, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D5, "univpll_d5", "univ_249p6m", 1, 1, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL3_D2, "univpll3_d2", "univ_249p6m", 1, 2, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL3_D4, "univpll3_d4", "univ_249p6m", 1, 4, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL3_D8, "univpll3_d8", "univ_249p6m", 1, 8, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D7, "univpll_d7", "univ_178p3m", 1, 1, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D26, "univpll_d26", "univ_48m", 1, 1, 0),
	FACTOR_FLAGS(CLK_TOP_UNIVPLL_D52, "univpll_d52", "univ_48m", 1, 2, 0),

	FACTOR(CLK_TOP_VCODECPLL, "vcodecpll_ck", "vcodecpll", 1, 3),
	FACTOR(CLK_TOP_VCODECPLL_370P5, "vcodecpll_370p5", "vcodecpll", 1, 4),

	FACTOR(CLK_TOP_VENCPLL, "vencpll_ck", "vencpll", 1, 1),
	FACTOR(CLK_TOP_VENCPLL_D2, "vencpll_d2", "vencpll", 1, 2),
	FACTOR(CLK_TOP_VENCPLL_D4, "vencpll_d4", "vencpll", 1, 4),
};

static const struct mtk_composite top_muxes[] = {
	/* CLK_CFG_0 */
	MUX(CLK_TOP_AXI_SEL, "axi_sel", axi_parents, 0x0040, 0, 3),
	MUX_FLAGS(CLK_TOP_MEM_SEL, "mem_sel", mem_parents, 0x0040, 8, 1,
		  CLK_IS_CRITICAL | CLK_SET_RATE_PARENT),
	MUX_GATE_FLAGS(CLK_TOP_DDRPHYCFG_SEL, "ddrphycfg_sel",
		       ddrphycfg_parents, 0x0040, 16, 1, 23,
		       CLK_IS_CRITICAL | CLK_SET_RATE_PARENT),
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
	MUX_GATE_FLAGS(CLK_TOP_MSDC50_0_H_SEL, "msdc50_0_h_sel", msdc50_0_h_parents,
		 0x0070, 8, 3, 15, 0),
	MUX_GATE_FLAGS(CLK_TOP_MSDC50_0_SEL, "msdc50_0_sel", msdc50_0_parents,
		 0x0070, 16, 4, 23, 0),
	MUX_GATE_FLAGS(CLK_TOP_MSDC30_1_SEL, "msdc30_1_sel", msdc30_1_parents,
		 0x0070, 24, 3, 31, 0),
	/* CLK_CFG_4 */
	MUX_GATE_FLAGS(CLK_TOP_MSDC30_2_SEL, "msdc30_2_sel", msdc30_2_parents,
		 0x0080, 0, 3, 7, 0),
	MUX_GATE_FLAGS(CLK_TOP_MSDC30_3_SEL, "msdc30_3_sel", msdc30_3_parents,
		 0x0080, 8, 4, 15, 0),
	MUX_GATE(CLK_TOP_AUDIO_SEL, "audio_sel", audio_parents,
		 0x0080, 16, 2, 23),
	MUX_GATE(CLK_TOP_AUD_INTBUS_SEL, "aud_intbus_sel", aud_intbus_parents,
		 0x0080, 24, 3, 31),
	/* CLK_CFG_5 */
	MUX_GATE(CLK_TOP_PMICSPI_SEL, "pmicspi_sel", pmicspi_parents,
		 0x0090, 0, 3, 7 /* 7:5 */),
	MUX_GATE(CLK_TOP_SCP_SEL, "scp_sel", scp_parents, 0x0090, 8, 3, 15),
	MUX_GATE(CLK_TOP_ATB_SEL, "atb_sel", atb_parents, 0x0090, 16, 2, 23),
	MUX_GATE(CLK_TOP_VENC_LT_SEL, "venclt_sel", venc_lt_parents,
		 0x0090, 24, 4, 31),
	/* CLK_CFG_6 */
	/*
	 * The dpi0_sel clock should not propagate rate changes to its parent
	 * clock so the dpi driver can have full control over PLL and divider.
	 */
	MUX_GATE_FLAGS(CLK_TOP_DPI0_SEL, "dpi0_sel", dpi0_parents,
		       0x00a0, 0, 3, 7, 0),
	MUX_GATE(CLK_TOP_IRDA_SEL, "irda_sel", irda_parents, 0x00a0, 8, 2, 15),
	MUX_GATE_FLAGS(CLK_TOP_CCI400_SEL, "cci400_sel",
		       cci400_parents, 0x00a0, 16, 3, 23,
		       CLK_IS_CRITICAL | CLK_SET_RATE_PARENT),
	MUX_GATE(CLK_TOP_AUD_1_SEL, "aud_1_sel", aud_1_parents, 0x00a0, 24, 2, 31),
	/* CLK_CFG_7 */
	MUX_GATE(CLK_TOP_AUD_2_SEL, "aud_2_sel", aud_2_parents, 0x00b0, 0, 2, 7),
	MUX_GATE(CLK_TOP_MEM_MFG_IN_SEL, "mem_mfg_in_sel", mem_mfg_in_parents,
		 0x00b0, 8, 2, 15),
	MUX_GATE(CLK_TOP_AXI_MFG_IN_SEL, "axi_mfg_in_sel", axi_mfg_in_parents,
		 0x00b0, 16, 2, 23),
	MUX_GATE(CLK_TOP_SCAM_SEL, "scam_sel", scam_parents, 0x00b0, 24, 2, 31),
	/* CLK_CFG_12 */
	MUX_GATE(CLK_TOP_SPINFI_IFR_SEL, "spinfi_ifr_sel", spinfi_ifr_parents,
		 0x00c0, 0, 3, 7),
	MUX_GATE(CLK_TOP_HDMI_SEL, "hdmi_sel", hdmi_parents, 0x00c0, 8, 2, 15),
	MUX_GATE(CLK_TOP_DPILVDS_SEL, "dpilvds_sel", dpilvds_parents,
		 0x00c0, 24, 3, 31),
	/* CLK_CFG_13 */
	MUX_GATE_FLAGS(CLK_TOP_MSDC50_2_H_SEL, "msdc50_2_h_sel", msdc50_2_h_parents,
		 0x00d0, 0, 3, 7, 0),
	MUX_GATE(CLK_TOP_HDCP_SEL, "hdcp_sel", hdcp_parents, 0x00d0, 8, 2, 15),
	MUX_GATE(CLK_TOP_HDCP_24M_SEL, "hdcp_24m_sel", hdcp_24m_parents,
		 0x00d0, 16, 2, 23),
	MUX_FLAGS(CLK_TOP_RTC_SEL, "rtc_sel", rtc_parents, 0x00d0, 24, 2,
		  CLK_IS_CRITICAL | CLK_SET_RATE_PARENT),

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

static const struct mtk_clk_desc topck_desc = {
	.fixed_clks = fixed_clks,
	.num_fixed_clks = ARRAY_SIZE(fixed_clks),
	.factor_clks = top_divs,
	.num_factor_clks = ARRAY_SIZE(top_divs),
	.composite_clks = top_muxes,
	.num_composite_clks = ARRAY_SIZE(top_muxes),
	.clk_lock = &mt8173_top_clk_lock,
};

static const struct of_device_id of_match_clk_mt8173_topckgen[] = {
	{ .compatible = "mediatek,mt8173-topckgen", .data = &topck_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8173_topckgen);

static struct platform_driver clk_mt8173_topckgen_drv = {
	.driver = {
		.name = "clk-mt8173-topckgen",
		.of_match_table = of_match_clk_mt8173_topckgen,
	},
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
};
module_platform_driver(clk_mt8173_topckgen_drv);

MODULE_DESCRIPTION("MediaTek MT8173 topckgen clocks driver");
MODULE_LICENSE("GPL");
