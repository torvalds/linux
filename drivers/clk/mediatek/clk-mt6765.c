// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 MediaTek Inc.
 * Author: Owen Chen <owen.chen@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/mfd/syscon.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "clk-gate.h"
#include "clk-mtk.h"
#include "clk-mux.h"
#include "clk-pll.h"

#include <dt-bindings/clock/mt6765-clk.h>

/*fmeter div select 4*/
#define _DIV4_ 1

static DEFINE_SPINLOCK(mt6765_clk_lock);

/* Total 12 subsys */
static void __iomem *cksys_base;
static void __iomem *apmixed_base;

/* CKSYS */
#define CLK_SCP_CFG_0		(cksys_base + 0x200)
#define CLK_SCP_CFG_1		(cksys_base + 0x204)

/* CG */
#define AP_PLL_CON3		(apmixed_base + 0x0C)
#define PLLON_CON0		(apmixed_base + 0x44)
#define PLLON_CON1		(apmixed_base + 0x48)

/* clk cfg update */
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
#define CLK_CFG_8		0xc0
#define CLK_CFG_8_SET		0xc4
#define CLK_CFG_8_CLR		0xc8
#define CLK_CFG_9		0xd0
#define CLK_CFG_9_SET		0xd4
#define CLK_CFG_9_CLR		0xd8
#define CLK_CFG_10		0xe0
#define CLK_CFG_10_SET		0xe4
#define CLK_CFG_10_CLR		0xe8
#define CLK_CFG_UPDATE		0x004

static const struct mtk_fixed_clk fixed_clks[] = {
	FIXED_CLK(CLK_TOP_F_FRTC, "f_frtc_ck", "clk32k", 32768),
	FIXED_CLK(CLK_TOP_CLK26M, "clk_26m_ck", "clk26m", 26000000),
	FIXED_CLK(CLK_TOP_DMPLL, "dmpll_ck", NULL, 466000000),
};

static const struct mtk_fixed_factor top_divs[] = {
	FACTOR(CLK_TOP_SYSPLL, "syspll_ck", "mainpll", 1, 1),
	FACTOR(CLK_TOP_SYSPLL_D2, "syspll_d2", "mainpll", 1, 2),
	FACTOR(CLK_TOP_SYSPLL1_D2, "syspll1_d2", "syspll_d2", 1, 2),
	FACTOR(CLK_TOP_SYSPLL1_D4, "syspll1_d4", "syspll_d2", 1, 4),
	FACTOR(CLK_TOP_SYSPLL1_D8, "syspll1_d8", "syspll_d2", 1, 8),
	FACTOR(CLK_TOP_SYSPLL1_D16, "syspll1_d16", "syspll_d2", 1, 16),
	FACTOR(CLK_TOP_SYSPLL_D3, "syspll_d3", "mainpll", 1, 3),
	FACTOR(CLK_TOP_SYSPLL2_D2, "syspll2_d2", "syspll_d3", 1, 2),
	FACTOR(CLK_TOP_SYSPLL2_D4, "syspll2_d4", "syspll_d3", 1, 4),
	FACTOR(CLK_TOP_SYSPLL2_D8, "syspll2_d8", "syspll_d3", 1, 8),
	FACTOR(CLK_TOP_SYSPLL_D5, "syspll_d5", "mainpll", 1, 5),
	FACTOR(CLK_TOP_SYSPLL3_D2, "syspll3_d2", "syspll_d5", 1, 2),
	FACTOR(CLK_TOP_SYSPLL3_D4, "syspll3_d4", "syspll_d5", 1, 4),
	FACTOR(CLK_TOP_SYSPLL_D7, "syspll_d7", "mainpll", 1, 7),
	FACTOR(CLK_TOP_SYSPLL4_D2, "syspll4_d2", "syspll_d7", 1, 2),
	FACTOR(CLK_TOP_SYSPLL4_D4, "syspll4_d4", "syspll_d7", 1, 4),
	FACTOR(CLK_TOP_UNIVPLL, "univpll", "univ2pll", 1, 2),
	FACTOR(CLK_TOP_USB20_192M, "usb20_192m_ck", "univpll", 2, 13),
	FACTOR(CLK_TOP_USB20_192M_D4, "usb20_192m_d4", "usb20_192m_ck", 1, 4),
	FACTOR(CLK_TOP_USB20_192M_D8, "usb20_192m_d8", "usb20_192m_ck", 1, 8),
	FACTOR(CLK_TOP_USB20_192M_D16,
	       "usb20_192m_d16", "usb20_192m_ck", 1, 16),
	FACTOR(CLK_TOP_USB20_192M_D32,
	       "usb20_192m_d32", "usb20_192m_ck", 1, 32),
	FACTOR(CLK_TOP_UNIVPLL_D2, "univpll_d2", "univpll", 1, 2),
	FACTOR(CLK_TOP_UNIVPLL1_D2, "univpll1_d2", "univpll_d2", 1, 2),
	FACTOR(CLK_TOP_UNIVPLL1_D4, "univpll1_d4", "univpll_d2", 1, 4),
	FACTOR(CLK_TOP_UNIVPLL_D3, "univpll_d3", "univpll", 1, 3),
	FACTOR(CLK_TOP_UNIVPLL2_D2, "univpll2_d2", "univpll_d3", 1, 2),
	FACTOR(CLK_TOP_UNIVPLL2_D4, "univpll2_d4", "univpll_d3", 1, 4),
	FACTOR(CLK_TOP_UNIVPLL2_D8, "univpll2_d8", "univpll_d3", 1, 8),
	FACTOR(CLK_TOP_UNIVPLL2_D32, "univpll2_d32", "univpll_d3", 1, 32),
	FACTOR(CLK_TOP_UNIVPLL_D5, "univpll_d5", "univpll", 1, 5),
	FACTOR(CLK_TOP_UNIVPLL3_D2, "univpll3_d2", "univpll_d5", 1, 2),
	FACTOR(CLK_TOP_UNIVPLL3_D4, "univpll3_d4", "univpll_d5", 1, 4),
	FACTOR(CLK_TOP_MMPLL, "mmpll_ck", "mmpll", 1, 1),
	FACTOR(CLK_TOP_MMPLL_D2, "mmpll_d2", "mmpll_ck", 1, 2),
	FACTOR(CLK_TOP_MPLL, "mpll_ck", "mpll", 1, 1),
	FACTOR(CLK_TOP_DA_MPLL_104M_DIV, "mpll_104m_div", "mpll_ck", 1, 2),
	FACTOR(CLK_TOP_DA_MPLL_52M_DIV, "mpll_52m_div", "mpll_ck", 1, 4),
	FACTOR(CLK_TOP_MFGPLL, "mfgpll_ck", "mfgpll", 1, 1),
	FACTOR(CLK_TOP_MSDCPLL, "msdcpll_ck", "msdcpll", 1, 1),
	FACTOR(CLK_TOP_MSDCPLL_D2, "msdcpll_d2", "msdcpll_ck", 1, 2),
	FACTOR(CLK_TOP_APLL1, "apll1_ck", "apll1", 1, 1),
	FACTOR(CLK_TOP_APLL1_D2, "apll1_d2", "apll1_ck", 1, 2),
	FACTOR(CLK_TOP_APLL1_D4, "apll1_d4", "apll1_ck", 1, 4),
	FACTOR(CLK_TOP_APLL1_D8, "apll1_d8", "apll1_ck", 1, 8),
	FACTOR(CLK_TOP_ULPOSC1, "ulposc1_ck", "ulposc1", 1, 1),
	FACTOR(CLK_TOP_ULPOSC1_D2, "ulposc1_d2", "ulposc1_ck", 1, 2),
	FACTOR(CLK_TOP_ULPOSC1_D4, "ulposc1_d4", "ulposc1_ck", 1, 4),
	FACTOR(CLK_TOP_ULPOSC1_D8, "ulposc1_d8", "ulposc1_ck", 1, 8),
	FACTOR(CLK_TOP_ULPOSC1_D16, "ulposc1_d16", "ulposc1_ck", 1, 16),
	FACTOR(CLK_TOP_ULPOSC1_D32, "ulposc1_d32", "ulposc1_ck", 1, 32),
	FACTOR(CLK_TOP_F_F26M, "f_f26m_ck", "clk_26m_ck", 1, 1),
	FACTOR(CLK_TOP_AXI, "axi_ck", "axi_sel", 1, 1),
	FACTOR(CLK_TOP_MM, "mm_ck", "mm_sel", 1, 1),
	FACTOR(CLK_TOP_SCP, "scp_ck", "scp_sel", 1, 1),
	FACTOR(CLK_TOP_MFG, "mfg_ck", "mfg_sel", 1, 1),
	FACTOR(CLK_TOP_F_FUART, "f_fuart_ck", "uart_sel", 1, 1),
	FACTOR(CLK_TOP_SPI, "spi_ck", "spi_sel", 1, 1),
	FACTOR(CLK_TOP_MSDC50_0, "msdc50_0_ck", "msdc50_0_sel", 1, 1),
	FACTOR(CLK_TOP_MSDC30_1, "msdc30_1_ck", "msdc30_1_sel", 1, 1),
	FACTOR(CLK_TOP_AUDIO, "audio_ck", "audio_sel", 1, 1),
	FACTOR(CLK_TOP_AUD_1, "aud_1_ck", "aud_1_sel", 1, 1),
	FACTOR(CLK_TOP_AUD_ENGEN1, "aud_engen1_ck", "aud_engen1_sel", 1, 1),
	FACTOR(CLK_TOP_F_FDISP_PWM, "f_fdisp_pwm_ck", "disp_pwm_sel", 1, 1),
	FACTOR(CLK_TOP_SSPM, "sspm_ck", "sspm_sel", 1, 1),
	FACTOR(CLK_TOP_DXCC, "dxcc_ck", "dxcc_sel", 1, 1),
	FACTOR(CLK_TOP_I2C, "i2c_ck", "i2c_sel", 1, 1),
	FACTOR(CLK_TOP_F_FPWM, "f_fpwm_ck", "pwm_sel", 1, 1),
	FACTOR(CLK_TOP_F_FSENINF, "f_fseninf_ck", "seninf_sel", 1, 1),
	FACTOR(CLK_TOP_AES_FDE, "aes_fde_ck", "aes_fde_sel", 1, 1),
	FACTOR(CLK_TOP_F_BIST2FPC, "f_bist2fpc_ck", "univpll2_d2", 1, 1),
	FACTOR(CLK_TOP_ARMPLL_DIVIDER_PLL0, "arm_div_pll0", "syspll_d2", 1, 1),
	FACTOR(CLK_TOP_ARMPLL_DIVIDER_PLL1, "arm_div_pll1", "syspll_ck", 1, 1),
	FACTOR(CLK_TOP_ARMPLL_DIVIDER_PLL2, "arm_div_pll2", "univpll_d2", 1, 1),
	FACTOR(CLK_TOP_DA_USB20_48M_DIV,
	       "usb20_48m_div", "usb20_192m_d4", 1, 1),
	FACTOR(CLK_TOP_DA_UNIV_48M_DIV, "univ_48m_div", "usb20_192m_d4", 1, 1),
};

static const char * const axi_parents[] = {
	"clk26m",
	"syspll_d7",
	"syspll1_d4",
	"syspll3_d2"
};

static const char * const mem_parents[] = {
	"clk26m",
	"dmpll_ck",
	"apll1_ck"
};

static const char * const mm_parents[] = {
	"clk26m",
	"mmpll_ck",
	"syspll1_d2",
	"syspll_d5",
	"syspll1_d4",
	"univpll_d5",
	"univpll1_d2",
	"mmpll_d2"
};

static const char * const scp_parents[] = {
	"clk26m",
	"syspll4_d2",
	"univpll2_d2",
	"syspll1_d2",
	"univpll1_d2",
	"syspll_d3",
	"univpll_d3"
};

static const char * const mfg_parents[] = {
	"clk26m",
	"mfgpll_ck",
	"syspll_d3",
	"univpll_d3"
};

static const char * const atb_parents[] = {
	"clk26m",
	"syspll1_d4",
	"syspll1_d2"
};

static const char * const camtg_parents[] = {
	"clk26m",
	"usb20_192m_d8",
	"univpll2_d8",
	"usb20_192m_d4",
	"univpll2_d32",
	"usb20_192m_d16",
	"usb20_192m_d32"
};

static const char * const uart_parents[] = {
	"clk26m",
	"univpll2_d8"
};

static const char * const spi_parents[] = {
	"clk26m",
	"syspll3_d2",
	"syspll4_d2",
	"syspll2_d4"
};

static const char * const msdc5hclk_parents[] = {
	"clk26m",
	"syspll1_d2",
	"univpll1_d4",
	"syspll2_d2"
};

static const char * const msdc50_0_parents[] = {
	"clk26m",
	"msdcpll_ck",
	"syspll2_d2",
	"syspll4_d2",
	"univpll1_d2",
	"syspll1_d2",
	"univpll_d5",
	"univpll1_d4"
};

static const char * const msdc30_1_parents[] = {
	"clk26m",
	"msdcpll_d2",
	"univpll2_d2",
	"syspll2_d2",
	"syspll1_d4",
	"univpll1_d4",
	"usb20_192m_d4",
	"syspll2_d4"
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
	"syspll4_d2"
};

static const char * const aud_1_parents[] = {
	"clk26m",
	"apll1_ck"
};

static const char * const aud_engen1_parents[] = {
	"clk26m",
	"apll1_d2",
	"apll1_d4",
	"apll1_d8"
};

static const char * const disp_pwm_parents[] = {
	"clk26m",
	"univpll2_d4",
	"ulposc1_d2",
	"ulposc1_d8"
};

static const char * const sspm_parents[] = {
	"clk26m",
	"syspll1_d2",
	"syspll_d3"
};

static const char * const dxcc_parents[] = {
	"clk26m",
	"syspll1_d2",
	"syspll1_d4",
	"syspll1_d8"
};

static const char * const usb_top_parents[] = {
	"clk26m",
	"univpll3_d4"
};

static const char * const spm_parents[] = {
	"clk26m",
	"syspll1_d8"
};

static const char * const i2c_parents[] = {
	"clk26m",
	"univpll3_d4",
	"univpll3_d2",
	"syspll1_d8",
	"syspll2_d8"
};

static const char * const pwm_parents[] = {
	"clk26m",
	"univpll3_d4",
	"syspll1_d8"
};

static const char * const seninf_parents[] = {
	"clk26m",
	"univpll1_d4",
	"univpll1_d2",
	"univpll2_d2"
};

static const char * const aes_fde_parents[] = {
	"clk26m",
	"msdcpll_ck",
	"univpll_d3",
	"univpll2_d2",
	"univpll1_d2",
	"syspll1_d2"
};

static const char * const ulposc_parents[] = {
	"clk26m",
	"ulposc1_d4",
	"ulposc1_d8",
	"ulposc1_d16",
	"ulposc1_d32"
};

static const char * const camtm_parents[] = {
	"clk26m",
	"univpll1_d4",
	"univpll1_d2",
	"univpll2_d2"
};

#define INVALID_UPDATE_REG 0xFFFFFFFF
#define INVALID_UPDATE_SHIFT -1
#define INVALID_MUX_GATE -1

static const struct mtk_mux top_muxes[] = {
	/* CLK_CFG_0 */
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_AXI_SEL, "axi_sel", axi_parents,
			      CLK_CFG_0, CLK_CFG_0_SET, CLK_CFG_0_CLR,
			      0, 2, 7, CLK_CFG_UPDATE, 0, CLK_IS_CRITICAL),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_MEM_SEL, "mem_sel", mem_parents,
			      CLK_CFG_0, CLK_CFG_0_SET, CLK_CFG_0_CLR,
			      8, 2, 15, CLK_CFG_UPDATE, 1, CLK_IS_CRITICAL),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MM_SEL, "mm_sel", mm_parents, CLK_CFG_0,
			CLK_CFG_0_SET, CLK_CFG_0_CLR, 16, 3, 23,
			CLK_CFG_UPDATE, 2),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SCP_SEL, "scp_sel", scp_parents, CLK_CFG_0,
			CLK_CFG_0_SET, CLK_CFG_0_CLR, 24, 3, 31,
			CLK_CFG_UPDATE, 3),
	/* CLK_CFG_1 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MFG_SEL, "mfg_sel", mfg_parents, CLK_CFG_1,
			CLK_CFG_1_SET, CLK_CFG_1_CLR, 0, 2, 7,
			CLK_CFG_UPDATE, 4),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_ATB_SEL, "atb_sel", atb_parents, CLK_CFG_1,
			CLK_CFG_1_SET, CLK_CFG_1_CLR, 8, 2, 15,
			CLK_CFG_UPDATE, 5),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CAMTG_SEL, "camtg_sel",
			camtg_parents, CLK_CFG_1, CLK_CFG_1_SET,
			CLK_CFG_1_CLR, 16, 3, 23, CLK_CFG_UPDATE, 6),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CAMTG1_SEL, "camtg1_sel", camtg_parents,
			CLK_CFG_1, CLK_CFG_1_SET, CLK_CFG_1_CLR,
			24, 3, 31, CLK_CFG_UPDATE, 7),
	/* CLK_CFG_2 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CAMTG2_SEL, "camtg2_sel",
			camtg_parents, CLK_CFG_2, CLK_CFG_2_SET,
			CLK_CFG_2_CLR, 0, 3, 7, CLK_CFG_UPDATE, 8),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CAMTG3_SEL, "camtg3_sel", camtg_parents,
			CLK_CFG_2, CLK_CFG_2_SET, CLK_CFG_2_CLR,
			8, 3, 15, CLK_CFG_UPDATE, 9),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_UART_SEL, "uart_sel", uart_parents,
			CLK_CFG_2, CLK_CFG_2_SET, CLK_CFG_2_CLR, 16, 1, 23,
			CLK_CFG_UPDATE, 10),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SPI_SEL, "spi_sel", spi_parents, CLK_CFG_2,
			CLK_CFG_2_SET, CLK_CFG_2_CLR, 24, 2, 31,
			CLK_CFG_UPDATE, 11),
	/* CLK_CFG_3 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MSDC50_0_HCLK_SEL, "msdc5hclk",
			msdc5hclk_parents, CLK_CFG_3, CLK_CFG_3_SET,
			CLK_CFG_3_CLR, 0, 2, 7, CLK_CFG_UPDATE, 12),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MSDC50_0_SEL, "msdc50_0_sel",
			msdc50_0_parents, CLK_CFG_3, CLK_CFG_3_SET,
			CLK_CFG_3_CLR, 8, 3, 15, CLK_CFG_UPDATE, 13),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MSDC30_1_SEL, "msdc30_1_sel",
			msdc30_1_parents, CLK_CFG_3, CLK_CFG_3_SET,
			CLK_CFG_3_CLR, 16, 3, 23, CLK_CFG_UPDATE, 14),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUDIO_SEL, "audio_sel", audio_parents,
			CLK_CFG_3, CLK_CFG_3_SET, CLK_CFG_3_CLR,
			24, 2, 31, CLK_CFG_UPDATE, 15),
	/* CLK_CFG_4 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUD_INTBUS_SEL, "aud_intbus_sel",
			aud_intbus_parents, CLK_CFG_4, CLK_CFG_4_SET,
			CLK_CFG_4_CLR, 0, 2, 7, CLK_CFG_UPDATE, 16),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUD_1_SEL, "aud_1_sel", aud_1_parents,
			CLK_CFG_4, CLK_CFG_4_SET, CLK_CFG_4_CLR,
			8, 1, 15, CLK_CFG_UPDATE, 17),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUD_ENGEN1_SEL, "aud_engen1_sel",
			aud_engen1_parents, CLK_CFG_4, CLK_CFG_4_SET,
			CLK_CFG_4_CLR, 16, 2, 23, CLK_CFG_UPDATE, 18),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DISP_PWM_SEL, "disp_pwm_sel",
			disp_pwm_parents, CLK_CFG_4, CLK_CFG_4_SET,
			CLK_CFG_4_CLR, 24, 2, 31, CLK_CFG_UPDATE, 19),
	/* CLK_CFG_5 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SSPM_SEL, "sspm_sel", sspm_parents,
			CLK_CFG_5, CLK_CFG_5_SET, CLK_CFG_5_CLR, 0, 2, 7,
			CLK_CFG_UPDATE, 20),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DXCC_SEL, "dxcc_sel", dxcc_parents,
			CLK_CFG_5, CLK_CFG_5_SET, CLK_CFG_5_CLR, 8, 2, 15,
			CLK_CFG_UPDATE, 21),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_USB_TOP_SEL, "usb_top_sel",
			usb_top_parents, CLK_CFG_5, CLK_CFG_5_SET,
			CLK_CFG_5_CLR, 16, 1, 23, CLK_CFG_UPDATE, 22),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SPM_SEL, "spm_sel", spm_parents, CLK_CFG_5,
			CLK_CFG_5_SET, CLK_CFG_5_CLR, 24, 1, 31,
			CLK_CFG_UPDATE, 23),
	/* CLK_CFG_6 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_I2C_SEL, "i2c_sel", i2c_parents, CLK_CFG_6,
			CLK_CFG_6_SET, CLK_CFG_6_CLR, 0, 3, 7, CLK_CFG_UPDATE,
			24),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_PWM_SEL, "pwm_sel", pwm_parents, CLK_CFG_6,
			CLK_CFG_6_SET, CLK_CFG_6_CLR, 8, 2, 15, CLK_CFG_UPDATE,
			25),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SENINF_SEL, "seninf_sel", seninf_parents,
			CLK_CFG_6, CLK_CFG_6_SET, CLK_CFG_6_CLR, 16, 2, 23,
			CLK_CFG_UPDATE, 26),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AES_FDE_SEL, "aes_fde_sel",
			aes_fde_parents, CLK_CFG_6, CLK_CFG_6_SET,
			CLK_CFG_6_CLR, 24, 3, 31, CLK_CFG_UPDATE, 27),
	/* CLK_CFG_7 */
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_PWRAP_ULPOSC_SEL, "ulposc_sel",
			      ulposc_parents, CLK_CFG_7, CLK_CFG_7_SET,
			      CLK_CFG_7_CLR, 0, 3, 7, CLK_CFG_UPDATE, 28,
			      CLK_IS_CRITICAL),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CAMTM_SEL, "camtm_sel", camtm_parents,
			CLK_CFG_7, CLK_CFG_7_SET, CLK_CFG_7_CLR, 8, 2, 15,
			CLK_CFG_UPDATE, 29),
};

static const struct mtk_gate_regs top0_cg_regs = {
	.set_ofs = 0x0,
	.clr_ofs = 0x0,
	.sta_ofs = 0x0,
};

static const struct mtk_gate_regs top1_cg_regs = {
	.set_ofs = 0x104,
	.clr_ofs = 0x104,
	.sta_ofs = 0x104,
};

static const struct mtk_gate_regs top2_cg_regs = {
	.set_ofs = 0x320,
	.clr_ofs = 0x320,
	.sta_ofs = 0x320,
};

#define GATE_TOP0(_id, _name, _parent, _shift)				\
	GATE_MTK(_id, _name, _parent, &top0_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr)

#define GATE_TOP1(_id, _name, _parent, _shift)				\
	GATE_MTK(_id, _name, _parent, &top1_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr_inv)

#define GATE_TOP2(_id, _name, _parent, _shift)				\
	GATE_MTK(_id, _name, _parent, &top2_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr)

static const struct mtk_gate top_clks[] = {
	/* TOP0 */
	GATE_TOP0(CLK_TOP_MD_32K, "md_32k", "f_frtc_ck", 8),
	GATE_TOP0(CLK_TOP_MD_26M, "md_26m", "f_f26m_ck", 9),
	GATE_TOP0(CLK_TOP_MD2_32K, "md2_32k", "f_frtc_ck", 10),
	GATE_TOP0(CLK_TOP_MD2_26M, "md2_26m", "f_f26m_ck", 11),
	/* TOP1 */
	GATE_TOP1(CLK_TOP_ARMPLL_DIVIDER_PLL0_EN,
		  "arm_div_pll0_en", "arm_div_pll0", 3),
	GATE_TOP1(CLK_TOP_ARMPLL_DIVIDER_PLL1_EN,
		  "arm_div_pll1_en", "arm_div_pll1", 4),
	GATE_TOP1(CLK_TOP_ARMPLL_DIVIDER_PLL2_EN,
		  "arm_div_pll2_en", "arm_div_pll2", 5),
	GATE_TOP1(CLK_TOP_FMEM_OCC_DRC_EN, "drc_en", "univpll2_d2", 6),
	GATE_TOP1(CLK_TOP_USB20_48M_EN, "usb20_48m_en", "usb20_48m_div", 8),
	GATE_TOP1(CLK_TOP_UNIVPLL_48M_EN, "univpll_48m_en", "univ_48m_div", 9),
	GATE_TOP1(CLK_TOP_F_UFS_MP_SAP_CFG_EN, "ufs_sap", "f_f26m_ck", 12),
	GATE_TOP1(CLK_TOP_F_BIST2FPC_EN, "bist2fpc", "f_bist2fpc_ck", 16),
	/* TOP2 */
	GATE_TOP2(CLK_TOP_APLL12_DIV0, "apll12_div0", "aud_1_ck", 2),
	GATE_TOP2(CLK_TOP_APLL12_DIV1, "apll12_div1", "aud_1_ck", 3),
	GATE_TOP2(CLK_TOP_APLL12_DIV2, "apll12_div2", "aud_1_ck", 4),
	GATE_TOP2(CLK_TOP_APLL12_DIV3, "apll12_div3", "aud_1_ck", 5),
};

static const struct mtk_gate_regs ifr2_cg_regs = {
	.set_ofs = 0x80,
	.clr_ofs = 0x84,
	.sta_ofs = 0x90,
};

static const struct mtk_gate_regs ifr3_cg_regs = {
	.set_ofs = 0x88,
	.clr_ofs = 0x8c,
	.sta_ofs = 0x94,
};

static const struct mtk_gate_regs ifr4_cg_regs = {
	.set_ofs = 0xa4,
	.clr_ofs = 0xa8,
	.sta_ofs = 0xac,
};

static const struct mtk_gate_regs ifr5_cg_regs = {
	.set_ofs = 0xc0,
	.clr_ofs = 0xc4,
	.sta_ofs = 0xc8,
};

#define GATE_IFR2(_id, _name, _parent, _shift)				\
	GATE_MTK(_id, _name, _parent, &ifr2_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

#define GATE_IFR3(_id, _name, _parent, _shift)				\
	GATE_MTK(_id, _name, _parent, &ifr3_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

#define GATE_IFR4(_id, _name, _parent, _shift)				\
	GATE_MTK(_id, _name, _parent, &ifr4_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

#define GATE_IFR5(_id, _name, _parent, _shift)				\
	GATE_MTK(_id, _name, _parent, &ifr5_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate ifr_clks[] = {
	/* INFRA_TOPAXI */
	/* INFRA PERI */
	/* INFRA mode 0 */
	GATE_IFR2(CLK_IFR_ICUSB, "ifr_icusb", "axi_ck", 8),
	GATE_IFR2(CLK_IFR_GCE, "ifr_gce", "axi_ck", 9),
	GATE_IFR2(CLK_IFR_THERM, "ifr_therm", "axi_ck", 10),
	GATE_IFR2(CLK_IFR_I2C_AP, "ifr_i2c_ap", "i2c_ck", 11),
	GATE_IFR2(CLK_IFR_I2C_CCU, "ifr_i2c_ccu", "i2c_ck", 12),
	GATE_IFR2(CLK_IFR_I2C_SSPM, "ifr_i2c_sspm", "i2c_ck", 13),
	GATE_IFR2(CLK_IFR_I2C_RSV, "ifr_i2c_rsv", "i2c_ck", 14),
	GATE_IFR2(CLK_IFR_PWM_HCLK, "ifr_pwm_hclk", "axi_ck", 15),
	GATE_IFR2(CLK_IFR_PWM1, "ifr_pwm1", "f_fpwm_ck", 16),
	GATE_IFR2(CLK_IFR_PWM2, "ifr_pwm2", "f_fpwm_ck", 17),
	GATE_IFR2(CLK_IFR_PWM3, "ifr_pwm3", "f_fpwm_ck", 18),
	GATE_IFR2(CLK_IFR_PWM4, "ifr_pwm4", "f_fpwm_ck", 19),
	GATE_IFR2(CLK_IFR_PWM5, "ifr_pwm5", "f_fpwm_ck", 20),
	GATE_IFR2(CLK_IFR_PWM, "ifr_pwm", "f_fpwm_ck", 21),
	GATE_IFR2(CLK_IFR_UART0, "ifr_uart0", "f_fuart_ck", 22),
	GATE_IFR2(CLK_IFR_UART1, "ifr_uart1", "f_fuart_ck", 23),
	GATE_IFR2(CLK_IFR_GCE_26M, "ifr_gce_26m", "f_f26m_ck", 27),
	GATE_IFR2(CLK_IFR_CQ_DMA_FPC, "ifr_dma", "axi_ck", 28),
	GATE_IFR2(CLK_IFR_BTIF, "ifr_btif", "axi_ck", 31),
	/* INFRA mode 1 */
	GATE_IFR3(CLK_IFR_SPI0, "ifr_spi0", "spi_ck", 1),
	GATE_IFR3(CLK_IFR_MSDC0, "ifr_msdc0", "msdc5hclk", 2),
	GATE_IFR3(CLK_IFR_MSDC1, "ifr_msdc1", "axi_ck", 4),
	GATE_IFR3(CLK_IFR_TRNG, "ifr_trng", "axi_ck", 9),
	GATE_IFR3(CLK_IFR_AUXADC, "ifr_auxadc", "f_f26m_ck", 10),
	GATE_IFR3(CLK_IFR_CCIF1_AP, "ifr_ccif1_ap", "axi_ck", 12),
	GATE_IFR3(CLK_IFR_CCIF1_MD, "ifr_ccif1_md", "axi_ck", 13),
	GATE_IFR3(CLK_IFR_AUXADC_MD, "ifr_auxadc_md", "f_f26m_ck", 14),
	GATE_IFR3(CLK_IFR_AP_DMA, "ifr_ap_dma", "axi_ck", 18),
	GATE_IFR3(CLK_IFR_DEVICE_APC, "ifr_dapc", "axi_ck", 20),
	GATE_IFR3(CLK_IFR_CCIF_AP, "ifr_ccif_ap", "axi_ck", 23),
	GATE_IFR3(CLK_IFR_AUDIO, "ifr_audio", "axi_ck", 25),
	GATE_IFR3(CLK_IFR_CCIF_MD, "ifr_ccif_md", "axi_ck", 26),
	/* INFRA mode 2 */
	GATE_IFR4(CLK_IFR_RG_PWM_FBCLK6, "ifr_pwmfb", "f_f26m_ck", 0),
	GATE_IFR4(CLK_IFR_DISP_PWM, "ifr_disp_pwm", "f_fdisp_pwm_ck", 2),
	GATE_IFR4(CLK_IFR_CLDMA_BCLK, "ifr_cldmabclk", "axi_ck", 3),
	GATE_IFR4(CLK_IFR_AUDIO_26M_BCLK, "ifr_audio26m", "f_f26m_ck", 4),
	GATE_IFR4(CLK_IFR_SPI1, "ifr_spi1", "spi_ck", 6),
	GATE_IFR4(CLK_IFR_I2C4, "ifr_i2c4", "i2c_ck", 7),
	GATE_IFR4(CLK_IFR_SPI2, "ifr_spi2", "spi_ck", 9),
	GATE_IFR4(CLK_IFR_SPI3, "ifr_spi3", "spi_ck", 10),
	GATE_IFR4(CLK_IFR_I2C5, "ifr_i2c5", "i2c_ck", 18),
	GATE_IFR4(CLK_IFR_I2C5_ARBITER, "ifr_i2c5a", "i2c_ck", 19),
	GATE_IFR4(CLK_IFR_I2C5_IMM, "ifr_i2c5_imm", "i2c_ck", 20),
	GATE_IFR4(CLK_IFR_I2C1_ARBITER, "ifr_i2c1a", "i2c_ck", 21),
	GATE_IFR4(CLK_IFR_I2C1_IMM, "ifr_i2c1_imm", "i2c_ck", 22),
	GATE_IFR4(CLK_IFR_I2C2_ARBITER, "ifr_i2c2a", "i2c_ck", 23),
	GATE_IFR4(CLK_IFR_I2C2_IMM, "ifr_i2c2_imm", "i2c_ck", 24),
	GATE_IFR4(CLK_IFR_SPI4, "ifr_spi4", "spi_ck", 25),
	GATE_IFR4(CLK_IFR_SPI5, "ifr_spi5", "spi_ck", 26),
	GATE_IFR4(CLK_IFR_CQ_DMA, "ifr_cq_dma", "axi_ck", 27),
	GATE_IFR4(CLK_IFR_FAES_FDE, "ifr_faes_fde_ck", "aes_fde_ck", 29),
	/* INFRA mode 3 */
	GATE_IFR5(CLK_IFR_MSDC0_SELF, "ifr_msdc0sf", "msdc50_0_ck", 0),
	GATE_IFR5(CLK_IFR_MSDC1_SELF, "ifr_msdc1sf", "msdc50_0_ck", 1),
	GATE_IFR5(CLK_IFR_I2C6, "ifr_i2c6", "i2c_ck", 6),
	GATE_IFR5(CLK_IFR_AP_MSDC0, "ifr_ap_msdc0", "msdc50_0_ck", 7),
	GATE_IFR5(CLK_IFR_MD_MSDC0, "ifr_md_msdc0", "msdc50_0_ck", 8),
	GATE_IFR5(CLK_IFR_MSDC0_SRC, "ifr_msdc0_clk", "msdc50_0_ck", 9),
	GATE_IFR5(CLK_IFR_MSDC1_SRC, "ifr_msdc1_clk", "msdc30_1_ck", 10),
	GATE_IFR5(CLK_IFR_MCU_PM_BCLK, "ifr_mcu_pm_bclk", "axi_ck", 17),
	GATE_IFR5(CLK_IFR_CCIF2_AP, "ifr_ccif2_ap", "axi_ck", 18),
	GATE_IFR5(CLK_IFR_CCIF2_MD, "ifr_ccif2_md", "axi_ck", 19),
	GATE_IFR5(CLK_IFR_CCIF3_AP, "ifr_ccif3_ap", "axi_ck", 20),
	GATE_IFR5(CLK_IFR_CCIF3_MD, "ifr_ccif3_md", "axi_ck", 21),
};

/* additional CCF control for mipi26M race condition(disp/camera) */
static const struct mtk_gate_regs apmixed_cg_regs = {
	.set_ofs = 0x14,
	.clr_ofs = 0x14,
	.sta_ofs = 0x14,
};

#define GATE_APMIXED(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &apmixed_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr_inv)

static const struct mtk_gate apmixed_clks[] = {
	/* AUDIO0 */
	GATE_APMIXED(CLK_APMIXED_SSUSB26M, "apmixed_ssusb26m", "f_f26m_ck",
		     4),
	GATE_APMIXED(CLK_APMIXED_APPLL26M, "apmixed_appll26m", "f_f26m_ck",
		     5),
	GATE_APMIXED(CLK_APMIXED_MIPIC0_26M, "apmixed_mipic026m", "f_f26m_ck",
		     6),
	GATE_APMIXED(CLK_APMIXED_MDPLLGP26M, "apmixed_mdpll26m", "f_f26m_ck",
		     7),
	GATE_APMIXED(CLK_APMIXED_MMSYS_F26M, "apmixed_mmsys26m", "f_f26m_ck",
		     8),
	GATE_APMIXED(CLK_APMIXED_UFS26M, "apmixed_ufs26m", "f_f26m_ck",
		     9),
	GATE_APMIXED(CLK_APMIXED_MIPIC1_26M, "apmixed_mipic126m", "f_f26m_ck",
		     11),
	GATE_APMIXED(CLK_APMIXED_MEMPLL26M, "apmixed_mempll26m", "f_f26m_ck",
		     13),
	GATE_APMIXED(CLK_APMIXED_CLKSQ_LVPLL_26M, "apmixed_lvpll26m",
		     "f_f26m_ck", 14),
	GATE_APMIXED(CLK_APMIXED_MIPID0_26M, "apmixed_mipid026m", "f_f26m_ck",
		     16),
};

#define MT6765_PLL_FMAX		(3800UL * MHZ)
#define MT6765_PLL_FMIN		(1500UL * MHZ)

#define CON0_MT6765_RST_BAR	BIT(23)

#define PLL_INFO_NULL		(0xFF)

#define PLL_B(_id, _name, _reg, _pwr_reg, _en_mask, _flags, _pcwbits,	\
		_pcwibits, _pd_reg, _pd_shift, _tuner_reg, _tuner_en_reg,\
		_tuner_en_bit, _pcw_reg, _pcw_shift, _div_table) {\
		.id = _id,						\
		.name = _name,						\
		.reg = _reg,						\
		.pwr_reg = _pwr_reg,					\
		.en_mask = _en_mask,					\
		.flags = _flags,					\
		.rst_bar_mask = CON0_MT6765_RST_BAR,			\
		.fmax = MT6765_PLL_FMAX,				\
		.fmin = MT6765_PLL_FMIN,				\
		.pcwbits = _pcwbits,					\
		.pcwibits = _pcwibits,					\
		.pd_reg = _pd_reg,					\
		.pd_shift = _pd_shift,					\
		.tuner_reg = _tuner_reg,				\
		.tuner_en_reg = _tuner_en_reg,				\
		.tuner_en_bit = _tuner_en_bit,				\
		.pcw_reg = _pcw_reg,					\
		.pcw_shift = _pcw_shift,				\
		.div_table = _div_table,				\
	}

#define PLL(_id, _name, _reg, _pwr_reg, _en_mask, _flags, _pcwbits,	\
			_pcwibits, _pd_reg, _pd_shift, _tuner_reg,	\
			_tuner_en_reg, _tuner_en_bit, _pcw_reg,	\
			_pcw_shift)	\
		PLL_B(_id, _name, _reg, _pwr_reg, _en_mask, _flags,	\
			_pcwbits, _pcwibits, _pd_reg, _pd_shift,	\
			_tuner_reg, _tuner_en_reg, _tuner_en_bit,	\
			_pcw_reg, _pcw_shift, NULL)	\

static const struct mtk_pll_data plls[] = {
	PLL(CLK_APMIXED_ARMPLL_L, "armpll_l", 0x021C, 0x0228, 0,
	    PLL_AO, 22, 8, 0x0220, 24, 0, 0, 0, 0x0220, 0),
	PLL(CLK_APMIXED_ARMPLL, "armpll", 0x020C, 0x0218, 0,
	    PLL_AO, 22, 8, 0x0210, 24, 0, 0, 0, 0x0210, 0),
	PLL(CLK_APMIXED_CCIPLL, "ccipll", 0x022C, 0x0238, 0,
	    PLL_AO, 22, 8, 0x0230, 24, 0, 0, 0, 0x0230, 0),
	PLL(CLK_APMIXED_MAINPLL, "mainpll", 0x023C, 0x0248, 0,
	    (HAVE_RST_BAR | PLL_AO), 22, 8, 0x0240, 24, 0, 0, 0, 0x0240,
	    0),
	PLL(CLK_APMIXED_MFGPLL, "mfgpll", 0x024C, 0x0258, 0,
	    0, 22, 8, 0x0250, 24, 0, 0, 0, 0x0250, 0),
	PLL(CLK_APMIXED_MMPLL, "mmpll", 0x025C, 0x0268, 0,
	    0, 22, 8, 0x0260, 24, 0, 0, 0, 0x0260, 0),
	PLL(CLK_APMIXED_UNIV2PLL, "univ2pll", 0x026C, 0x0278, 0,
	    HAVE_RST_BAR, 22, 8, 0x0270, 24, 0, 0, 0, 0x0270, 0),
	PLL(CLK_APMIXED_MSDCPLL, "msdcpll", 0x027C, 0x0288, 0,
	    0, 22, 8, 0x0280, 24, 0, 0, 0, 0x0280, 0),
	PLL(CLK_APMIXED_APLL1, "apll1", 0x028C, 0x029C, 0,
	    0, 32, 8, 0x0290, 24, 0x0040, 0x000C, 0, 0x0294, 0),
	PLL(CLK_APMIXED_MPLL, "mpll", 0x02A0, 0x02AC, 0,
	    PLL_AO, 22, 8, 0x02A4, 24, 0, 0, 0, 0x02A4, 0),
};

static int clk_mt6765_apmixed_probe(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data;
	int r;
	struct device_node *node = pdev->dev.of_node;
	void __iomem *base;
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base)) {
		pr_err("%s(): ioremap failed\n", __func__);
		return PTR_ERR(base);
	}

	clk_data = mtk_alloc_clk_data(CLK_APMIXED_NR_CLK);
	if (!clk_data)
		return -ENOMEM;

	mtk_clk_register_plls(node, plls, ARRAY_SIZE(plls), clk_data);

	mtk_clk_register_gates(&pdev->dev, node, apmixed_clks,
			       ARRAY_SIZE(apmixed_clks), clk_data);
	r = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);

	if (r)
		pr_err("%s(): could not register clock provider: %d\n",
		       __func__, r);

	apmixed_base = base;
	/* MPLL, CCIPLL, MAINPLL set HW mode, TDCLKSQ, CLKSQ1 */
	writel(readl(AP_PLL_CON3) & 0xFFFFFFE1, AP_PLL_CON3);
	writel(readl(PLLON_CON0) & 0x01041041, PLLON_CON0);
	writel(readl(PLLON_CON1) & 0x01041041, PLLON_CON1);

	return r;
}

static int clk_mt6765_top_probe(struct platform_device *pdev)
{
	int r;
	struct device_node *node = pdev->dev.of_node;
	void __iomem *base;
	struct clk_hw_onecell_data *clk_data;
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base)) {
		pr_err("%s(): ioremap failed\n", __func__);
		return PTR_ERR(base);
	}

	clk_data = mtk_alloc_clk_data(CLK_TOP_NR_CLK);
	if (!clk_data)
		return -ENOMEM;

	mtk_clk_register_fixed_clks(fixed_clks, ARRAY_SIZE(fixed_clks),
				    clk_data);
	mtk_clk_register_factors(top_divs, ARRAY_SIZE(top_divs),
				 clk_data);
	mtk_clk_register_muxes(&pdev->dev, top_muxes,
			       ARRAY_SIZE(top_muxes), node,
			       &mt6765_clk_lock, clk_data);
	mtk_clk_register_gates(&pdev->dev, node, top_clks,
			       ARRAY_SIZE(top_clks), clk_data);

	r = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);

	if (r)
		pr_err("%s(): could not register clock provider: %d\n",
		       __func__, r);

	cksys_base = base;
	/* [4]:no need */
	writel(readl(CLK_SCP_CFG_0) | 0x3EF, CLK_SCP_CFG_0);
	/*[1,2,3,8]: no need*/
	writel(readl(CLK_SCP_CFG_1) | 0x1, CLK_SCP_CFG_1);

	return r;
}

static int clk_mt6765_ifr_probe(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data;
	int r;
	struct device_node *node = pdev->dev.of_node;
	void __iomem *base;
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base)) {
		pr_err("%s(): ioremap failed\n", __func__);
		return PTR_ERR(base);
	}

	clk_data = mtk_alloc_clk_data(CLK_IFR_NR_CLK);
	if (!clk_data)
		return -ENOMEM;

	mtk_clk_register_gates(&pdev->dev, node, ifr_clks,
			       ARRAY_SIZE(ifr_clks), clk_data);
	r = of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);

	if (r)
		pr_err("%s(): could not register clock provider: %d\n",
		       __func__, r);

	return r;
}

static const struct of_device_id of_match_clk_mt6765[] = {
	{
		.compatible = "mediatek,mt6765-apmixedsys",
		.data = clk_mt6765_apmixed_probe,
	}, {
		.compatible = "mediatek,mt6765-topckgen",
		.data = clk_mt6765_top_probe,
	}, {
		.compatible = "mediatek,mt6765-infracfg",
		.data = clk_mt6765_ifr_probe,
	}, {
		/* sentinel */
	}
};

static int clk_mt6765_probe(struct platform_device *pdev)
{
	int (*clk_probe)(struct platform_device *d);
	int r;

	clk_probe = of_device_get_match_data(&pdev->dev);
	if (!clk_probe)
		return -EINVAL;

	r = clk_probe(pdev);
	if (r)
		dev_err(&pdev->dev,
			"could not register clock provider: %s: %d\n",
			pdev->name, r);

	return r;
}

static struct platform_driver clk_mt6765_drv = {
	.probe = clk_mt6765_probe,
	.driver = {
		.name = "clk-mt6765",
		.of_match_table = of_match_clk_mt6765,
	},
};

static int __init clk_mt6765_init(void)
{
	return platform_driver_register(&clk_mt6765_drv);
}

arch_initcall(clk_mt6765_init);
