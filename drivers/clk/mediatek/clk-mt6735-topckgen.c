// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Yassine Oudjana <y.oudjana@protonmail.com>
 */

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-mtk.h"
#include "clk-mux.h"

#include <dt-bindings/clock/mediatek,mt6735-topckgen.h>

#define CLK_CFG_0		0x40
#define CLK_CFG_0_SET		0x44
#define CLK_CFG_0_CLR		0x48
#define CLK_CFG_1		0x50
#define CLK_CFG_1_SET		0x54
#define CLK_CFG_1_CLR		0x58
#define CLK_CFG_2		0x60
#define CLK_CFG_2_SET		0x64
#define CLK_CFG_2_CLR		0x68
#define CLK_CFG_3		0x70
#define CLK_CFG_3_SET		0x74
#define CLK_CFG_3_CLR		0x78
#define CLK_CFG_4		0x80
#define CLK_CFG_4_SET		0x84
#define CLK_CFG_4_CLR		0x88
#define CLK_CFG_5		0x90
#define CLK_CFG_5_SET		0x94
#define CLK_CFG_5_CLR		0x98
#define CLK_CFG_6		0xa0
#define CLK_CFG_6_SET		0xa4
#define CLK_CFG_6_CLR		0xa8
#define CLK_CFG_7		0xb0
#define CLK_CFG_7_SET		0xb4
#define CLK_CFG_7_CLR		0xb8

static DEFINE_SPINLOCK(mt6735_topckgen_lock);

/* Some clocks with unknown details are modeled as fixed clocks */
static const struct mtk_fixed_clk topckgen_fixed_clks[] = {
	/*
	 * This clock is available as a parent option for multiple
	 * muxes and seems like an alternative name for clk26m at first,
	 * but it appears alongside it in several muxes which should
	 * mean it is a separate clock.
	 */
	FIXED_CLK(CLK_TOP_AD_SYS_26M_CK, "ad_sys_26m_ck", "clk26m", 26 * MHZ),
	/*
	 * This clock is the parent of DMPLL divisors. It might be MEMPLL
	 * or its parent, as DMPLL appears to be an alternative name for
	 * MEMPLL.
	 */
	FIXED_CLK(CLK_TOP_CLKPH_MCK_O, "clkph_mck_o", NULL, 0),
	/*
	 * DMPLL clock (dmpll_ck), controlled by DDRPHY.
	 */
	FIXED_CLK(CLK_TOP_DMPLL, "dmpll", "clkph_mck_o", 0),
	/*
	 * MIPI DPI clock. Parent option for dpi0_sel. Unknown parent.
	 */
	FIXED_CLK(CLK_TOP_DPI_CK, "dpi_ck", NULL, 0),
	/*
	 * This clock is a child of WHPLL which is controlled by
	 * the modem.
	 */
	FIXED_CLK(CLK_TOP_WHPLL_AUDIO_CK, "whpll_audio_ck", NULL, 0)
};

static const struct mtk_fixed_factor topckgen_factors[] = {
	FACTOR(CLK_TOP_SYSPLL_D2, "syspll_d2", "mainpll", 1, 2),
	FACTOR(CLK_TOP_SYSPLL_D3, "syspll_d3", "mainpll", 1, 3),
	FACTOR(CLK_TOP_SYSPLL_D5, "syspll_d5", "mainpll", 1, 5),
	FACTOR(CLK_TOP_SYSPLL1_D2, "syspll1_d2", "mainpll", 1, 2),
	FACTOR(CLK_TOP_SYSPLL1_D4, "syspll1_d4", "mainpll", 1, 4),
	FACTOR(CLK_TOP_SYSPLL1_D8, "syspll1_d8", "mainpll", 1, 8),
	FACTOR(CLK_TOP_SYSPLL1_D16, "syspll1_d16", "mainpll", 1, 16),
	FACTOR(CLK_TOP_SYSPLL2_D2, "syspll2_d2", "mainpll", 1, 2),
	FACTOR(CLK_TOP_SYSPLL2_D4, "syspll2_d4", "mainpll", 1, 4),
	FACTOR(CLK_TOP_SYSPLL3_D2, "syspll3_d2", "mainpll", 1, 2),
	FACTOR(CLK_TOP_SYSPLL3_D4, "syspll3_d4", "mainpll", 1, 4),
	FACTOR(CLK_TOP_SYSPLL4_D2, "syspll4_d2", "mainpll", 1, 2),
	FACTOR(CLK_TOP_SYSPLL4_D4, "syspll4_d4", "mainpll", 1, 4),
	FACTOR(CLK_TOP_UNIVPLL_D2, "univpll_d2", "univpll", 1, 2),
	FACTOR(CLK_TOP_UNIVPLL_D3, "univpll_d3", "univpll", 1, 3),
	FACTOR(CLK_TOP_UNIVPLL_D5, "univpll_d5", "univpll", 1, 5),
	FACTOR(CLK_TOP_UNIVPLL_D26, "univpll_d26", "univpll", 1, 26),
	FACTOR(CLK_TOP_UNIVPLL1_D2, "univpll1_d2", "univpll", 1, 2),
	FACTOR(CLK_TOP_UNIVPLL1_D4, "univpll1_d4", "univpll", 1, 4),
	FACTOR(CLK_TOP_UNIVPLL1_D8, "univpll1_d8", "univpll", 1, 8),
	FACTOR(CLK_TOP_UNIVPLL2_D2, "univpll2_d2", "univpll", 1, 2),
	FACTOR(CLK_TOP_UNIVPLL2_D4, "univpll2_d4", "univpll", 1, 4),
	FACTOR(CLK_TOP_UNIVPLL2_D8, "univpll2_d8", "univpll", 1, 8),
	FACTOR(CLK_TOP_UNIVPLL3_D2, "univpll3_d2", "univpll", 1, 2),
	FACTOR(CLK_TOP_UNIVPLL3_D4, "univpll3_d4", "univpll", 1, 4),
	FACTOR(CLK_TOP_MSDCPLL_D2, "msdcpll_d2", "msdcpll", 1, 2),
	FACTOR(CLK_TOP_MSDCPLL_D4, "msdcpll_d4", "msdcpll", 1, 4),
	FACTOR(CLK_TOP_MSDCPLL_D8, "msdcpll_d8", "msdcpll", 1, 8),
	FACTOR(CLK_TOP_MSDCPLL_D16, "msdcpll_d16", "msdcpll", 1, 16),
	FACTOR(CLK_TOP_VENCPLL_D3, "vencpll_d3", "vencpll", 1, 3),
	FACTOR(CLK_TOP_TVDPLL_D2, "tvdpll_d2", "tvdpll", 1, 2),
	FACTOR(CLK_TOP_TVDPLL_D4, "tvdpll_d4", "tvdpll", 1, 4),
	FACTOR(CLK_TOP_DMPLL_D2, "dmpll_d2", "clkph_mck_o", 1, 2),
	FACTOR(CLK_TOP_DMPLL_D4, "dmpll_d4", "clkph_mck_o", 1, 4),
	FACTOR(CLK_TOP_DMPLL_D8, "dmpll_d8", "clkph_mck_o", 1, 8),
	FACTOR(CLK_TOP_AD_SYS_26M_D2, "ad_sys_26m_d2", "clk26m", 1, 2)
};

static const char * const axi_sel_parents[] = {
	"clk26m",
	"syspll1_d2",
	"syspll_d5",
	"syspll1_d4",
	"univpll_d5",
	"univpll2_d2",
	"dmpll",
	"dmpll_d2"
};

static const char * const mem_sel_parents[] = {
	"clk26m",
	"dmpll"
};

static const char * const ddrphycfg_parents[] = {
	"clk26m",
	"syspll1_d8"
};

static const char * const mm_sel_parents[] = {
	"clk26m",
	"vencpll",
	"syspll1_d2",
	"syspll_d5",
	"syspll1_d4",
	"univpll_d5",
	"univpll2_d2",
	"dmpll"
};

static const char * const pwm_sel_parents[] = {
	"clk26m",
	"univpll2_d4",
	"univpll3_d2",
	"univpll1_d4"
};

static const char * const vdec_sel_parents[] = {
	"clk26m",
	"syspll1_d2",
	"syspll_d5",
	"syspll1_d4",
	"univpll_d5",
	"syspll_d2",
	"syspll2_d2",
	"msdcpll_d2"
};

static const char * const mfg_sel_parents[] = {
	"clk26m",
	"mmpll",
	"clk26m",
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
	"univpll1_d2"
};

static const char * const camtg_sel_parents[] = {
	"clk26m",
	"univpll_d26",
	"univpll2_d2",
	"syspll3_d2",
	"syspll3_d4",
	"msdcpll_d4"
};

static const char * const uart_sel_parents[] = {
	"clk26m",
	"univpll2_d8"
};

static const char * const spi_sel_parents[] = {
	"clk26m",
	"syspll3_d2",
	"msdcpll_d8",
	"syspll2_d4",
	"syspll4_d2",
	"univpll2_d4",
	"univpll1_d8"
};

static const char * const usb20_sel_parents[] = {
	"clk26m",
	"univpll1_d8",
	"univpll3_d4"
};

static const char * const msdc50_0_sel_parents[] = {
	"clk26m",
	"syspll1_d2",
	"syspll2_d2",
	"syspll4_d2",
	"univpll_d5",
	"univpll1_d4"
};

static const char * const msdc30_0_sel_parents[] = {
	"clk26m",
	"msdcpll",
	"msdcpll_d2",
	"msdcpll_d4",
	"syspll2_d2",
	"syspll1_d4",
	"univpll1_d4",
	"univpll_d3",
	"univpll_d26",
	"syspll2_d4",
	"univpll_d2"
};

static const char * const msdc30_1_2_sel_parents[] = {
	"clk26m",
	"univpll2_d2",
	"msdcpll_d4",
	"syspll2_d2",
	"syspll1_d4",
	"univpll1_d4",
	"univpll_d26",
	"syspll2_d4"
};

static const char * const msdc30_3_sel_parents[] = {
	"clk26m",
	"univpll2_d2",
	"msdcpll_d4",
	"syspll2_d2",
	"syspll1_d4",
	"univpll1_d4",
	"univpll_d26",
	"msdcpll_d16",
	"syspll2_d4"
};

static const char * const audio_sel_parents[] = {
	"clk26m",
	"syspll3_d4",
	"syspll4_d4",
	"syspll1_d16"
};

static const char * const aud_intbus_sel_parents[] = {
	"clk26m",
	"syspll1_d4",
	"syspll4_d2",
	"dmpll_d4"
};

static const char * const pmicspi_sel_parents[] = {
	"clk26m",
	"syspll1_d8",
	"syspll3_d4",
	"syspll1_d16",
	"univpll3_d4",
	"univpll_d26",
	"dmpll_d4",
	"dmpll_d8"
};

static const char * const scp_sel_parents[] = {
	"clk26m",
	"syspll1_d8",
	"dmpll_d2",
	"dmpll_d4"
};

static const char * const atb_sel_parents[] = {
	"clk26m",
	"syspll1_d2",
	"syspll_d5",
	"dmpll"
};

static const char * const dpi0_sel_parents[] = {
	"clk26m",
	"tvdpll",
	"tvdpll_d2",
	"tvdpll_d4",
	"dpi_ck"
};

static const char * const scam_sel_parents[] = {
	"clk26m",
	"syspll3_d2",
	"univpll2_d4",
	"vencpll_d3"
};

static const char * const mfg13m_sel_parents[] = {
	"clk26m",
	"ad_sys_26m_d2"
};

static const char * const aud_1_2_sel_parents[] = {
	"clk26m",
	"apll1"
};

static const char * const irda_sel_parents[] = {
	"clk26m",
	"univpll2_d4"
};

static const char * const irtx_sel_parents[] = {
	"clk26m",
	"ad_sys_26m_ck"
};

static const char * const disppwm_sel_parents[] = {
	"clk26m",
	"univpll2_d4",
	"syspll4_d2_d8",
	"ad_sys_26m_ck"
};

static const struct mtk_mux topckgen_muxes[] = {
	MUX_CLR_SET_UPD(CLK_TOP_AXI_SEL, "axi_sel", axi_sel_parents, CLK_CFG_0, CLK_CFG_0_SET, CLK_CFG_0_CLR, 0, 3, 0, 0),
	MUX_CLR_SET_UPD(CLK_TOP_MEM_SEL, "mem_sel", mem_sel_parents, CLK_CFG_0, CLK_CFG_0_SET, CLK_CFG_0_CLR, 8, 1, 0, 0),
	MUX_CLR_SET_UPD(CLK_TOP_DDRPHY_SEL, "ddrphycfg_sel", ddrphycfg_parents, CLK_CFG_0, CLK_CFG_0_SET, CLK_CFG_0_CLR, 16, 1, 0, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MM_SEL, "mm_sel", mm_sel_parents, CLK_CFG_0, CLK_CFG_0_SET, CLK_CFG_0_CLR, 24, 3, 31, 0, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_PWM_SEL, "pwm_sel", pwm_sel_parents, CLK_CFG_1, CLK_CFG_1_SET, CLK_CFG_1_CLR, 0, 2, 7, 0, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_VDEC_SEL, "vdec_sel", vdec_sel_parents, CLK_CFG_1, CLK_CFG_1_SET, CLK_CFG_1_CLR, 8, 3, 15, 0, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MFG_SEL, "mfg_sel", mfg_sel_parents, CLK_CFG_1, CLK_CFG_1_SET, CLK_CFG_1_CLR, 16, 4, 23, 0, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CAMTG_SEL, "camtg_sel", camtg_sel_parents, CLK_CFG_1, CLK_CFG_1_SET, CLK_CFG_1_CLR, 24, 3, 31, 0, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_UART_SEL, "uart_sel", uart_sel_parents, CLK_CFG_2, CLK_CFG_2_SET, CLK_CFG_2_CLR, 0, 1, 7, 0, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SPI_SEL, "spi_sel", spi_sel_parents, CLK_CFG_2, CLK_CFG_2_SET, CLK_CFG_2_CLR, 8, 3, 15, 0, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_USB20_SEL, "usb20_sel", usb20_sel_parents, CLK_CFG_2, CLK_CFG_2_SET, CLK_CFG_2_CLR, 16, 2, 23, 0, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MSDC50_0_SEL, "msdc50_0_sel", msdc50_0_sel_parents, CLK_CFG_2, CLK_CFG_2_SET, CLK_CFG_2_CLR, 24, 3, 31, 0, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MSDC30_0_SEL, "msdc30_0_sel", msdc30_0_sel_parents, CLK_CFG_3, CLK_CFG_3_SET, CLK_CFG_3_CLR, 0, 4, 7, 0, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MSDC30_1_SEL, "msdc30_1_sel", msdc30_1_2_sel_parents, CLK_CFG_3, CLK_CFG_3_SET, CLK_CFG_3_CLR, 8, 3, 15, 0, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MSDC30_2_SEL, "msdc30_2_sel", msdc30_1_2_sel_parents, CLK_CFG_3, CLK_CFG_3_SET, CLK_CFG_3_CLR, 16, 3, 23, 0, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MSDC30_3_SEL, "msdc30_3_sel", msdc30_3_sel_parents, CLK_CFG_3, CLK_CFG_3_SET, CLK_CFG_3_CLR, 24, 4, 31, 0, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUDIO_SEL, "audio_sel", audio_sel_parents, CLK_CFG_4, CLK_CFG_4_SET, CLK_CFG_4_CLR, 0, 2, 7, 0, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUDINTBUS_SEL, "aud_intbus_sel", aud_intbus_sel_parents, CLK_CFG_4, CLK_CFG_4_SET, CLK_CFG_4_CLR, 8, 2, 15, 0, 0),
	MUX_CLR_SET_UPD(CLK_TOP_PMICSPI_SEL, "pmicspi_sel", pmicspi_sel_parents, CLK_CFG_4, CLK_CFG_4_SET, CLK_CFG_4_CLR, 16, 3, 0, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SCP_SEL, "scp_sel", scp_sel_parents, CLK_CFG_4, CLK_CFG_4_SET, CLK_CFG_4_CLR, 24, 2, 31, 0, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_ATB_SEL, "atb_sel", atb_sel_parents, CLK_CFG_5, CLK_CFG_5_SET, CLK_CFG_5_CLR, 0, 2, 7, 0, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DPI0_SEL, "dpi0_sel", dpi0_sel_parents, CLK_CFG_5, CLK_CFG_5_SET, CLK_CFG_5_CLR, 8, 3, 15, 0, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SCAM_SEL, "scam_sel", scam_sel_parents, CLK_CFG_5, CLK_CFG_5_SET, CLK_CFG_5_CLR, 16, 2, 23, 0, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MFG13M_SEL, "mfg13m_sel", mfg13m_sel_parents, CLK_CFG_5, CLK_CFG_5_SET, CLK_CFG_5_CLR, 24, 1, 31, 0, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUD1_SEL, "aud_1_sel", aud_1_2_sel_parents, CLK_CFG_6, CLK_CFG_6_SET, CLK_CFG_6_CLR, 0, 1, 7, 0, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUD2_SEL, "aud_2_sel", aud_1_2_sel_parents, CLK_CFG_6, CLK_CFG_6_SET, CLK_CFG_6_CLR, 8, 1, 15, 0, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_IRDA_SEL, "irda_sel", irda_sel_parents, CLK_CFG_6, CLK_CFG_6_SET, CLK_CFG_6_CLR, 16, 1, 23, 0, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_IRTX_SEL, "irtx_sel", irtx_sel_parents, CLK_CFG_6, CLK_CFG_6_SET, CLK_CFG_6_CLR, 24, 1, 31, 0, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DISPPWM_SEL, "disppwm_sel", disppwm_sel_parents, CLK_CFG_7, CLK_CFG_7_SET, CLK_CFG_7_CLR, 0, 2, 7, 0, 0),
};

static const struct mtk_clk_desc topckgen_desc = {
	.fixed_clks = topckgen_fixed_clks,
	.num_fixed_clks = ARRAY_SIZE(topckgen_fixed_clks),
	.factor_clks = topckgen_factors,
	.num_factor_clks = ARRAY_SIZE(topckgen_factors),
	.mux_clks = topckgen_muxes,
	.num_mux_clks = ARRAY_SIZE(topckgen_muxes),
	.clk_lock = &mt6735_topckgen_lock,
};

static const struct of_device_id of_match_mt6735_topckgen[] = {
	{ .compatible = "mediatek,mt6735-topckgen", .data = &topckgen_desc},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_mt6735_topckgen);

static struct platform_driver clk_mt6735_topckgen = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt6735-topckgen",
		.of_match_table = of_match_mt6735_topckgen,
	},
};
module_platform_driver(clk_mt6735_topckgen);

MODULE_AUTHOR("Yassine Oudjana <y.oudjana@protonmail.com>");
MODULE_DESCRIPTION("MediaTek MT6735 topckgen clock driver");
MODULE_LICENSE("GPL");
