// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 MediaTek Inc.
 * Copyright (C) 2023 Collabora Ltd.
 *                    AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <dt-bindings/clock/mediatek,mt8365-clk.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "clk-gate.h"
#include "clk-mtk.h"
#include "clk-mux.h"

static DEFINE_SPINLOCK(mt8365_clk_lock);

static const struct mtk_fixed_clk top_fixed_clks[] = {
	FIXED_CLK(CLK_TOP_CLK_NULL, "clk_null", NULL, 0),
	FIXED_CLK(CLK_TOP_I2S0_BCK, "i2s0_bck", NULL, 26000000),
	FIXED_CLK(CLK_TOP_DSI0_LNTC_DSICK, "dsi0_lntc_dsick", "clk26m",
		  75000000),
	FIXED_CLK(CLK_TOP_VPLL_DPIX, "vpll_dpix", "clk26m", 75000000),
	FIXED_CLK(CLK_TOP_LVDSTX_CLKDIG_CTS, "lvdstx_dig_cts", "clk26m",
		  52500000),
};

static const struct mtk_fixed_factor top_divs[] = {
	FACTOR(CLK_TOP_SYS_26M_D2, "sys_26m_d2", "clk26m", 1, 2),
	FACTOR(CLK_TOP_SYSPLL_D2, "syspll_d2", "mainpll", 1, 2),
	FACTOR(CLK_TOP_SYSPLL1_D2, "syspll1_d2", "mainpll", 1, 4),
	FACTOR(CLK_TOP_SYSPLL1_D4, "syspll1_d4", "mainpll", 1, 8),
	FACTOR(CLK_TOP_SYSPLL1_D8, "syspll1_d8", "mainpll", 1, 16),
	FACTOR(CLK_TOP_SYSPLL1_D16, "syspll1_d16", "mainpll", 1, 32),
	FACTOR(CLK_TOP_SYSPLL_D3, "syspll_d3", "mainpll", 1, 3),
	FACTOR(CLK_TOP_SYSPLL2_D2, "syspll2_d2", "mainpll", 1, 6),
	FACTOR(CLK_TOP_SYSPLL2_D4, "syspll2_d4", "mainpll", 1, 12),
	FACTOR(CLK_TOP_SYSPLL2_D8, "syspll2_d8", "mainpll", 1, 24),
	FACTOR(CLK_TOP_SYSPLL_D5, "syspll_d5", "mainpll", 1, 5),
	FACTOR(CLK_TOP_SYSPLL3_D2, "syspll3_d2", "mainpll", 1, 10),
	FACTOR(CLK_TOP_SYSPLL3_D4, "syspll3_d4", "mainpll", 1, 20),
	FACTOR(CLK_TOP_SYSPLL_D7, "syspll_d7", "mainpll", 1, 7),
	FACTOR(CLK_TOP_SYSPLL4_D2, "syspll4_d2", "mainpll", 1, 14),
	FACTOR(CLK_TOP_SYSPLL4_D4, "syspll4_d4", "mainpll", 1, 28),
	FACTOR(CLK_TOP_UNIVPLL, "univpll", "univ_en", 1, 2),
	FACTOR(CLK_TOP_UNIVPLL_D2, "univpll_d2", "univpll", 1, 2),
	FACTOR(CLK_TOP_UNIVPLL1_D2, "univpll1_d2", "univpll", 1, 4),
	FACTOR(CLK_TOP_UNIVPLL1_D4, "univpll1_d4", "univpll", 1, 8),
	FACTOR(CLK_TOP_UNIVPLL_D3, "univpll_d3", "univpll", 1, 3),
	FACTOR(CLK_TOP_UNIVPLL2_D2, "univpll2_d2", "univpll", 1, 6),
	FACTOR(CLK_TOP_UNIVPLL2_D4, "univpll2_d4", "univpll", 1, 12),
	FACTOR(CLK_TOP_UNIVPLL2_D8, "univpll2_d8", "univpll", 1, 24),
	FACTOR(CLK_TOP_UNIVPLL2_D32, "univpll2_d32", "univpll", 1, 96),
	FACTOR(CLK_TOP_UNIVPLL_D5, "univpll_d5", "univpll", 1, 5),
	FACTOR(CLK_TOP_UNIVPLL3_D2, "univpll3_d2", "univpll", 1, 10),
	FACTOR(CLK_TOP_UNIVPLL3_D4, "univpll3_d4", "univpll", 1, 20),
	FACTOR(CLK_TOP_MMPLL, "mmpll_ck", "mmpll", 1, 1),
	FACTOR(CLK_TOP_MMPLL_D2, "mmpll_d2", "mmpll", 1, 2),
	FACTOR(CLK_TOP_MFGPLL, "mfgpll_ck", "mfgpll", 1, 1),
	FACTOR(CLK_TOP_LVDSPLL_D2, "lvdspll_d2", "lvdspll", 1, 2),
	FACTOR(CLK_TOP_LVDSPLL_D4, "lvdspll_d4", "lvdspll", 1, 4),
	FACTOR(CLK_TOP_LVDSPLL_D8, "lvdspll_d8", "lvdspll", 1, 8),
	FACTOR(CLK_TOP_LVDSPLL_D16, "lvdspll_d16", "lvdspll", 1, 16),
	FACTOR(CLK_TOP_USB20_192M, "usb20_192m_ck", "usb20_en", 1, 13),
	FACTOR(CLK_TOP_USB20_192M_D4, "usb20_192m_d4", "usb20_192m_ck", 1, 4),
	FACTOR(CLK_TOP_USB20_192M_D8, "usb20_192m_d8", "usb20_192m_ck", 1, 8),
	FACTOR(CLK_TOP_USB20_192M_D16, "usb20_192m_d16", "usb20_192m_ck",
	       1, 16),
	FACTOR(CLK_TOP_USB20_192M_D32, "usb20_192m_d32", "usb20_192m_ck",
	       1, 32),
	FACTOR(CLK_TOP_APLL1, "apll1_ck", "apll1", 1, 1),
	FACTOR(CLK_TOP_APLL1_D2, "apll1_d2", "apll1_ck", 1, 2),
	FACTOR(CLK_TOP_APLL1_D4, "apll1_d4", "apll1_ck", 1, 4),
	FACTOR(CLK_TOP_APLL1_D8, "apll1_d8", "apll1_ck", 1, 8),
	FACTOR(CLK_TOP_APLL2, "apll2_ck", "apll2", 1, 1),
	FACTOR(CLK_TOP_APLL2_D2, "apll2_d2", "apll2_ck", 1, 2),
	FACTOR(CLK_TOP_APLL2_D4, "apll2_d4", "apll2_ck", 1, 4),
	FACTOR(CLK_TOP_APLL2_D8, "apll2_d8", "apll2_ck", 1, 8),
	FACTOR(CLK_TOP_MSDCPLL, "msdcpll_ck", "msdcpll", 1, 1),
	FACTOR(CLK_TOP_MSDCPLL_D2, "msdcpll_d2", "msdcpll", 1, 2),
	FACTOR(CLK_TOP_DSPPLL, "dsppll_ck", "dsppll", 1, 1),
	FACTOR(CLK_TOP_DSPPLL_D2, "dsppll_d2", "dsppll", 1, 2),
	FACTOR(CLK_TOP_DSPPLL_D4, "dsppll_d4", "dsppll", 1, 4),
	FACTOR(CLK_TOP_DSPPLL_D8, "dsppll_d8", "dsppll", 1, 8),
	FACTOR(CLK_TOP_APUPLL, "apupll_ck", "apupll", 1, 1),
	FACTOR(CLK_TOP_CLK26M_D52, "clk26m_d52", "clk26m", 1, 52),
};

static const char * const axi_parents[] = {
	"clk26m",
	"syspll_d7",
	"syspll1_d4",
	"syspll3_d2"
};

static const char * const mem_parents[] = {
	"clk26m",
	"mmpll_ck",
	"syspll_d3",
	"syspll1_d2"
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
	"univpll2_d2",
	"univpll2_d4",
	"univpll2_d8"
};

static const char * const msdc50_0_hc_parents[] = {
	"clk26m",
	"syspll1_d2",
	"univpll1_d4",
	"syspll2_d2"
};

static const char * const msdc50_0_parents[] = {
	"clk26m",
	"msdcpll_ck",
	"univpll1_d2",
	"syspll1_d2",
	"univpll_d5",
	"syspll2_d2",
	"univpll1_d4",
	"syspll4_d2"
};

static const char * const msdc50_2_parents[] = {
	"clk26m",
	"msdcpll_ck",
	"univpll_d3",
	"univpll1_d2",
	"syspll1_d2",
	"univpll2_d2",
	"syspll2_d2",
	"univpll1_d4"
};

static const char * const msdc30_1_parents[] = {
	"clk26m",
	"msdcpll_d2",
	"univpll2_d2",
	"syspll2_d2",
	"univpll1_d4",
	"syspll1_d4",
	"syspll2_d4",
	"univpll2_d8"
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

static const char * const aud_2_parents[] = {
	"clk26m",
	"apll2_ck"
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

static const char * const aud_spdif_parents[] = {
	"clk26m",
	"univpll_d2"
};

static const char * const disp_pwm_parents[] = {
	"clk26m",
	"univpll2_d4"
};

static const char * const dxcc_parents[] = {
	"clk26m",
	"syspll1_d2",
	"syspll1_d4",
	"syspll1_d8"
};

static const char * const ssusb_sys_parents[] = {
	"clk26m",
	"univpll3_d4",
	"univpll2_d4",
	"univpll3_d2"
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

static const char * const senif_parents[] = {
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

static const char * const dpi0_parents[] = {
	"clk26m",
	"lvdspll_d2",
	"lvdspll_d4",
	"lvdspll_d8",
	"lvdspll_d16"
};

static const char * const dsp_parents[] = {
	"clk26m",
	"sys_26m_d2",
	"dsppll_ck",
	"dsppll_d2",
	"dsppll_d4",
	"dsppll_d8"
};

static const char * const nfi2x_parents[] = {
	"clk26m",
	"syspll2_d2",
	"syspll_d7",
	"syspll_d3",
	"syspll2_d4",
	"msdcpll_d2",
	"univpll1_d2",
	"univpll_d5"
};

static const char * const nfiecc_parents[] = {
	"clk26m",
	"syspll4_d2",
	"univpll2_d4",
	"syspll_d7",
	"univpll1_d2",
	"syspll1_d2",
	"univpll2_d2",
	"syspll_d5"
};

static const char * const ecc_parents[] = {
	"clk26m",
	"univpll2_d2",
	"univpll1_d2",
	"univpll_d3",
	"syspll_d2"
};

static const char * const eth_parents[] = {
	"clk26m",
	"univpll2_d8",
	"syspll4_d4",
	"syspll1_d8",
	"syspll4_d2"
};

static const char * const gcpu_parents[] = {
	"clk26m",
	"univpll_d3",
	"univpll2_d2",
	"syspll_d3",
	"syspll2_d2"
};

static const char * const gcpu_cpm_parents[] = {
	"clk26m",
	"univpll2_d2",
	"syspll2_d2"
};

static const char * const apu_parents[] = {
	"clk26m",
	"univpll_d2",
	"apupll_ck",
	"mmpll_ck",
	"syspll_d3",
	"univpll1_d2",
	"syspll1_d2",
	"syspll1_d4"
};

static const char * const mbist_diag_parents[] = {
	"clk26m",
	"syspll4_d4",
	"univpll2_d8"
};

static const char * const apll_i2s_parents[] = {
	"aud_1_sel",
	"aud_2_sel"
};

static struct mtk_composite top_misc_muxes[] = {
	/* CLK_CFG_11 */
	MUX_GATE(CLK_TOP_MBIST_DIAG_SEL, "mbist_diag_sel", mbist_diag_parents,
		 0x0ec, 0, 2, 7),
	/* Audio MUX */
	MUX(CLK_TOP_APLL_I2S0_SEL, "apll_i2s0_sel", apll_i2s_parents, 0x0320, 11, 1),
	MUX(CLK_TOP_APLL_I2S1_SEL, "apll_i2s1_sel", apll_i2s_parents, 0x0320, 12, 1),
	MUX(CLK_TOP_APLL_I2S2_SEL, "apll_i2s2_sel", apll_i2s_parents, 0x0320, 13, 1),
	MUX(CLK_TOP_APLL_I2S3_SEL, "apll_i2s3_sel", apll_i2s_parents, 0x0320, 14, 1),
	MUX(CLK_TOP_APLL_TDMOUT_SEL, "apll_tdmout_sel", apll_i2s_parents, 0x0320, 15, 1),
	MUX(CLK_TOP_APLL_TDMIN_SEL, "apll_tdmin_sel", apll_i2s_parents, 0x0320, 16, 1),
	MUX(CLK_TOP_APLL_SPDIF_SEL, "apll_spdif_sel", apll_i2s_parents, 0x0320, 17, 1),
};

#define CLK_CFG_UPDATE 0x004
#define CLK_CFG_UPDATE1 0x008

static const struct mtk_mux top_muxes[] = {
	/* CLK_CFG_0 */
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_AXI_SEL, "axi_sel", axi_parents,
				   0x040, 0x044, 0x048, 0, 2, 7, CLK_CFG_UPDATE,
				   0, CLK_IS_CRITICAL),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MEM_SEL, "mem_sel", mem_parents, 0x040,
			     0x044, 0x048, 8, 2, 15, CLK_CFG_UPDATE, 1),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MM_SEL, "mm_sel", mm_parents, 0x040, 0x044,
			     0x048, 16, 3, 23, CLK_CFG_UPDATE, 2),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SCP_SEL, "scp_sel", scp_parents, 0x040,
			     0x044, 0x048, 24, 3, 31, CLK_CFG_UPDATE, 3),
	/* CLK_CFG_1 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MFG_SEL, "mfg_sel", mfg_parents, 0x050,
			     0x054, 0x058, 0, 2, 7, CLK_CFG_UPDATE, 4),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_ATB_SEL, "atb_sel", atb_parents, 0x050,
			     0x054, 0x058, 8, 2, 15, CLK_CFG_UPDATE, 5),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CAMTG_SEL, "camtg_sel", camtg_parents,
			     0x050, 0x054, 0x058, 16, 3, 23, CLK_CFG_UPDATE, 6),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CAMTG1_SEL, "camtg1_sel", camtg_parents,
			     0x050, 0x054, 0x058, 24, 3, 31, CLK_CFG_UPDATE, 7),
	/* CLK_CFG_2 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_UART_SEL, "uart_sel", uart_parents, 0x060,
			     0x064, 0x068, 0, 1, 7, CLK_CFG_UPDATE, 8),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SPI_SEL, "spi_sel", spi_parents, 0x060,
			     0x064, 0x068, 8, 2, 15, CLK_CFG_UPDATE, 9),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MSDC50_0_HC_SEL, "msdc50_0_hc_sel",
			     msdc50_0_hc_parents, 0x060, 0x064, 0x068, 16, 2,
			     23, CLK_CFG_UPDATE, 10),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MSDC2_2_HC_SEL, "msdc2_2_hc_sel",
			     msdc50_0_hc_parents, 0x060, 0x064, 0x068, 24, 2,
			     31, CLK_CFG_UPDATE, 11),
	/* CLK_CFG_3 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MSDC50_0_SEL, "msdc50_0_sel",
			     msdc50_0_parents, 0x070, 0x074, 0x078, 0, 3, 7,
			     CLK_CFG_UPDATE, 12),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MSDC50_2_SEL, "msdc50_2_sel",
			     msdc50_2_parents, 0x070, 0x074, 0x078, 8, 3, 15,
			     CLK_CFG_UPDATE, 13),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MSDC30_1_SEL, "msdc30_1_sel",
			     msdc30_1_parents, 0x070, 0x074, 0x078, 16, 3, 23,
			     CLK_CFG_UPDATE, 14),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUDIO_SEL, "audio_sel", audio_parents,
			     0x070, 0x074, 0x078, 24, 2, 31, CLK_CFG_UPDATE,
			     15),
	/* CLK_CFG_4 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUD_INTBUS_SEL, "aud_intbus_sel",
			     aud_intbus_parents, 0x080, 0x084, 0x088, 0, 2, 7,
			     CLK_CFG_UPDATE, 16),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUD_1_SEL, "aud_1_sel", aud_1_parents,
			     0x080, 0x084, 0x088, 8, 1, 15, CLK_CFG_UPDATE, 17),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUD_2_SEL, "aud_2_sel", aud_2_parents,
			     0x080, 0x084, 0x088, 16, 1, 23, CLK_CFG_UPDATE,
			     18),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUD_ENGEN1_SEL, "aud_engen1_sel",
			     aud_engen1_parents, 0x080, 0x084, 0x088, 24, 2, 31,
			     CLK_CFG_UPDATE, 19),
	/* CLK_CFG_5 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUD_ENGEN2_SEL, "aud_engen2_sel",
			     aud_engen2_parents, 0x090, 0x094, 0x098, 0, 2, 7,
			     CLK_CFG_UPDATE, 20),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUD_SPDIF_SEL, "aud_spdif_sel",
			     aud_spdif_parents, 0x090, 0x094, 0x098, 8, 1, 15,
			     CLK_CFG_UPDATE, 21),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DISP_PWM_SEL, "disp_pwm_sel",
			     disp_pwm_parents, 0x090, 0x094, 0x098, 16, 2, 23,
			     CLK_CFG_UPDATE, 22),
	/* CLK_CFG_6 */
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_DXCC_SEL, "dxcc_sel", dxcc_parents,
				   0x0a0, 0x0a4, 0x0a8, 0, 2, 7, CLK_CFG_UPDATE,
				   24, CLK_IS_CRITICAL),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SSUSB_SYS_SEL, "ssusb_sys_sel",
			     ssusb_sys_parents, 0x0a0, 0x0a4, 0x0a8, 8, 2, 15,
			     CLK_CFG_UPDATE, 25),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SSUSB_XHCI_SEL, "ssusb_xhci_sel",
			     ssusb_sys_parents, 0x0a0, 0x0a4, 0x0a8, 16, 2, 23,
			     CLK_CFG_UPDATE, 26),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_SPM_SEL, "spm_sel", spm_parents,
				   0x0a0, 0x0a4, 0x0a8, 24, 1, 31,
				   CLK_CFG_UPDATE, 27, CLK_IS_CRITICAL),
	/* CLK_CFG_7 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_I2C_SEL, "i2c_sel", i2c_parents, 0x0b0,
			     0x0b4, 0x0b8, 0, 3, 7, CLK_CFG_UPDATE, 28),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_PWM_SEL, "pwm_sel", pwm_parents, 0x0b0,
			     0x0b4, 0x0b8, 8, 2, 15, CLK_CFG_UPDATE, 29),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SENIF_SEL, "senif_sel", senif_parents,
			     0x0b0, 0x0b4, 0x0b8, 16, 2, 23, CLK_CFG_UPDATE,
			     30),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AES_FDE_SEL, "aes_fde_sel",
			     aes_fde_parents, 0x0b0, 0x0b4, 0x0b8, 24, 3, 31,
			     CLK_CFG_UPDATE, 31),
	/* CLK_CFG_8 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CAMTM_SEL, "camtm_sel", senif_parents,
			     0x0c0, 0x0c4, 0x0c8, 0, 2, 7, CLK_CFG_UPDATE1, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DPI0_SEL, "dpi0_sel", dpi0_parents, 0x0c0,
			     0x0c4, 0x0c8, 8, 3, 15, CLK_CFG_UPDATE1, 1),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DPI1_SEL, "dpi1_sel", dpi0_parents, 0x0c0,
			     0x0c4, 0x0c8, 16, 3, 23, CLK_CFG_UPDATE1, 2),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DSP_SEL, "dsp_sel", dsp_parents, 0x0c0,
			     0x0c4, 0x0c8, 24, 3, 31, CLK_CFG_UPDATE1, 3),
	/* CLK_CFG_9 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_NFI2X_SEL, "nfi2x_sel", nfi2x_parents,
			     0x0d0, 0x0d4, 0x0d8, 0, 3, 7, CLK_CFG_UPDATE1, 4),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_NFIECC_SEL, "nfiecc_sel", nfiecc_parents,
			     0x0d0, 0x0d4, 0x0d8, 8, 3, 15, CLK_CFG_UPDATE1, 5),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_ECC_SEL, "ecc_sel", ecc_parents, 0x0d0,
			     0x0d4, 0x0d8, 16, 3, 23, CLK_CFG_UPDATE1, 6),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_ETH_SEL, "eth_sel", eth_parents, 0x0d0,
			     0x0d4, 0x0d8, 24, 3, 31, CLK_CFG_UPDATE1, 7),
	/* CLK_CFG_10 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_GCPU_SEL, "gcpu_sel", gcpu_parents, 0x0e0,
			     0x0e4, 0x0e8, 0, 3, 7, CLK_CFG_UPDATE1, 8),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_GCPU_CPM_SEL, "gcpu_cpm_sel",
			     gcpu_cpm_parents, 0x0e0, 0x0e4, 0x0e8, 8, 2, 15,
			     CLK_CFG_UPDATE1, 9),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_APU_SEL, "apu_sel", apu_parents, 0x0e0,
			     0x0e4, 0x0e8, 16, 3, 23, CLK_CFG_UPDATE1, 10),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_APU_IF_SEL, "apu_if_sel", apu_parents,
			     0x0e0, 0x0e4, 0x0e8, 24, 3, 31, CLK_CFG_UPDATE1,
			     11),
};

static const char * const mcu_bus_parents[] = {
	"clk26m",
	"armpll",
	"mainpll",
	"univpll_d2"
};

static struct mtk_composite mcu_muxes[] = {
	/* bus_pll_divider_cfg */
	MUX_GATE_FLAGS(CLK_MCU_BUS_SEL, "mcu_bus_sel", mcu_bus_parents, 0x7C0,
		       9, 2, -1, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL),
};

#define DIV_ADJ_F(_id, _name, _parent, _reg, _shift, _width, _flags) {	\
		.id = _id,					\
		.name = _name,					\
		.parent_name = _parent,				\
		.div_reg = _reg,				\
		.div_shift = _shift,				\
		.div_width = _width,				\
		.clk_divider_flags = _flags,			\
}

static const struct mtk_clk_divider top_adj_divs[] = {
	DIV_ADJ_F(CLK_TOP_APLL12_CK_DIV0, "apll12_ck_div0", "apll_i2s0_sel",
		  0x324, 0, 8, CLK_DIVIDER_ROUND_CLOSEST),
	DIV_ADJ_F(CLK_TOP_APLL12_CK_DIV1, "apll12_ck_div1", "apll_i2s1_sel",
		  0x324, 8, 8, CLK_DIVIDER_ROUND_CLOSEST),
	DIV_ADJ_F(CLK_TOP_APLL12_CK_DIV2, "apll12_ck_div2", "apll_i2s2_sel",
		  0x324, 16, 8, CLK_DIVIDER_ROUND_CLOSEST),
	DIV_ADJ_F(CLK_TOP_APLL12_CK_DIV3, "apll12_ck_div3", "apll_i2s3_sel",
		  0x324, 24, 8, CLK_DIVIDER_ROUND_CLOSEST),
	DIV_ADJ_F(CLK_TOP_APLL12_CK_DIV4, "apll12_ck_div4", "apll_tdmout_sel",
		  0x328, 0, 8, CLK_DIVIDER_ROUND_CLOSEST),
	DIV_ADJ_F(CLK_TOP_APLL12_CK_DIV4B, "apll12_ck_div4b", "apll_tdmout_sel",
		  0x328, 8, 8, CLK_DIVIDER_ROUND_CLOSEST),
	DIV_ADJ_F(CLK_TOP_APLL12_CK_DIV5, "apll12_ck_div5", "apll_tdmin_sel",
		  0x328, 16, 8, CLK_DIVIDER_ROUND_CLOSEST),
	DIV_ADJ_F(CLK_TOP_APLL12_CK_DIV5B, "apll12_ck_div5b", "apll_tdmin_sel",
		  0x328, 24, 8, CLK_DIVIDER_ROUND_CLOSEST),
	DIV_ADJ_F(CLK_TOP_APLL12_CK_DIV6, "apll12_ck_div6", "apll_spdif_sel",
		  0x32c, 0, 8, CLK_DIVIDER_ROUND_CLOSEST),
};

static const struct mtk_gate_regs top0_cg_regs = {
	.set_ofs = 0,
	.clr_ofs = 0,
	.sta_ofs = 0,
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

#define GATE_TOP0(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &top0_cg_regs,		\
		 _shift, &mtk_clk_gate_ops_no_setclr)

#define GATE_TOP1(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &top1_cg_regs,		\
		 _shift, &mtk_clk_gate_ops_no_setclr_inv)

#define GATE_TOP2(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &top2_cg_regs,		\
		 _shift, &mtk_clk_gate_ops_no_setclr_inv)

static const struct mtk_gate top_clk_gates[] = {
	GATE_TOP0(CLK_TOP_CONN_32K, "conn_32k", "clk32k", 10),
	GATE_TOP0(CLK_TOP_CONN_26M, "conn_26m", "clk26m", 11),
	GATE_TOP0(CLK_TOP_DSP_32K, "dsp_32k", "clk32k", 16),
	GATE_TOP0(CLK_TOP_DSP_26M, "dsp_26m", "clk26m", 17),
	GATE_TOP1(CLK_TOP_USB20_48M_EN, "usb20_48m_en", "usb20_192m_d4", 8),
	GATE_TOP1(CLK_TOP_UNIVPLL_48M_EN, "univpll_48m_en", "usb20_192m_d4", 9),
	GATE_TOP1(CLK_TOP_LVDSTX_CLKDIG_EN, "lvdstx_dig_en", "lvdstx_dig_cts", 20),
	GATE_TOP1(CLK_TOP_VPLL_DPIX_EN, "vpll_dpix_en", "vpll_dpix", 21),
	GATE_TOP1(CLK_TOP_SSUSB_TOP_CK_EN, "ssusb_top_ck_en", NULL, 22),
	GATE_TOP1(CLK_TOP_SSUSB_PHY_CK_EN, "ssusb_phy_ck_en", NULL, 23),
	GATE_TOP2(CLK_TOP_AUD_I2S0_M, "aud_i2s0_m_ck", "apll12_ck_div0", 0),
	GATE_TOP2(CLK_TOP_AUD_I2S1_M, "aud_i2s1_m_ck", "apll12_ck_div1", 1),
	GATE_TOP2(CLK_TOP_AUD_I2S2_M, "aud_i2s2_m_ck", "apll12_ck_div2", 2),
	GATE_TOP2(CLK_TOP_AUD_I2S3_M, "aud_i2s3_m_ck", "apll12_ck_div3", 3),
	GATE_TOP2(CLK_TOP_AUD_TDMOUT_M, "aud_tdmout_m_ck", "apll12_ck_div4", 4),
	GATE_TOP2(CLK_TOP_AUD_TDMOUT_B, "aud_tdmout_b_ck", "apll12_ck_div4b", 5),
	GATE_TOP2(CLK_TOP_AUD_TDMIN_M, "aud_tdmin_m_ck", "apll12_ck_div5", 6),
	GATE_TOP2(CLK_TOP_AUD_TDMIN_B, "aud_tdmin_b_ck", "apll12_ck_div5b", 7),
	GATE_TOP2(CLK_TOP_AUD_SPDIF_M, "aud_spdif_m_ck", "apll12_ck_div6", 8),
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

static const struct mtk_gate_regs ifr6_cg_regs = {
	.set_ofs = 0xd0,
	.clr_ofs = 0xd4,
	.sta_ofs = 0xd8,
};

#define GATE_IFRX(_id, _name, _parent, _shift, _regs)	\
	GATE_MTK(_id, _name, _parent, _regs, _shift,	\
		 &mtk_clk_gate_ops_setclr)

#define GATE_IFR2(_id, _name, _parent, _shift)		\
	GATE_IFRX(_id, _name, _parent, _shift, &ifr2_cg_regs)

#define GATE_IFR3(_id, _name, _parent, _shift)		\
	GATE_IFRX(_id, _name, _parent, _shift, &ifr3_cg_regs)

#define GATE_IFR4(_id, _name, _parent, _shift)		\
	GATE_IFRX(_id, _name, _parent, _shift, &ifr4_cg_regs)

#define GATE_IFR5(_id, _name, _parent, _shift)		\
	GATE_IFRX(_id, _name, _parent, _shift, &ifr5_cg_regs)

#define GATE_IFR6(_id, _name, _parent, _shift)		\
	GATE_IFRX(_id, _name, _parent, _shift, &ifr6_cg_regs)

static const struct mtk_gate ifr_clks[] = {
	/* IFR2 */
	GATE_IFR2(CLK_IFR_PMIC_TMR, "ifr_pmic_tmr", "clk26m", 0),
	GATE_IFR2(CLK_IFR_PMIC_AP, "ifr_pmic_ap", "clk26m", 1),
	GATE_IFR2(CLK_IFR_PMIC_MD, "ifr_pmic_md", "clk26m", 2),
	GATE_IFR2(CLK_IFR_PMIC_CONN, "ifr_pmic_conn", "clk26m", 3),
	GATE_IFR2(CLK_IFR_ICUSB, "ifr_icusb", "axi_sel", 8),
	GATE_IFR2(CLK_IFR_GCE, "ifr_gce", "axi_sel", 9),
	GATE_IFR2(CLK_IFR_THERM, "ifr_therm", "axi_sel", 10),
	GATE_IFR2(CLK_IFR_PWM_HCLK, "ifr_pwm_hclk", "axi_sel", 15),
	GATE_IFR2(CLK_IFR_PWM1, "ifr_pwm1", "pwm_sel", 16),
	GATE_IFR2(CLK_IFR_PWM2, "ifr_pwm2", "pwm_sel", 17),
	GATE_IFR2(CLK_IFR_PWM3, "ifr_pwm3", "pwm_sel", 18),
	GATE_IFR2(CLK_IFR_PWM4, "ifr_pwm4", "pwm_sel", 19),
	GATE_IFR2(CLK_IFR_PWM5, "ifr_pwm5", "pwm_sel", 20),
	GATE_IFR2(CLK_IFR_PWM, "ifr_pwm", "pwm_sel", 21),
	GATE_IFR2(CLK_IFR_UART0, "ifr_uart0", "uart_sel", 22),
	GATE_IFR2(CLK_IFR_UART1, "ifr_uart1", "uart_sel", 23),
	GATE_IFR2(CLK_IFR_UART2, "ifr_uart2", "uart_sel", 24),
	GATE_IFR2(CLK_IFR_DSP_UART, "ifr_dsp_uart", "uart_sel", 26),
	GATE_IFR2(CLK_IFR_GCE_26M, "ifr_gce_26m", "clk26m", 27),
	GATE_IFR2(CLK_IFR_CQ_DMA_FPC, "ifr_cq_dma_fpc", "axi_sel", 28),
	GATE_IFR2(CLK_IFR_BTIF, "ifr_btif", "axi_sel", 31),
	/* IFR3 */
	GATE_IFR3(CLK_IFR_SPI0, "ifr_spi0", "spi_sel", 1),
	GATE_IFR3(CLK_IFR_MSDC0_HCLK, "ifr_msdc0", "msdc50_0_hc_sel", 2),
	GATE_IFR3(CLK_IFR_MSDC2_HCLK, "ifr_msdc2", "msdc2_2_hc_sel", 3),
	GATE_IFR3(CLK_IFR_MSDC1_HCLK, "ifr_msdc1", "axi_sel", 4),
	GATE_IFR3(CLK_IFR_DVFSRC, "ifr_dvfsrc", "clk26m", 7),
	GATE_IFR3(CLK_IFR_GCPU, "ifr_gcpu", "axi_sel", 8),
	GATE_IFR3(CLK_IFR_TRNG, "ifr_trng", "axi_sel", 9),
	GATE_IFR3(CLK_IFR_AUXADC, "ifr_auxadc", "clk26m", 10),
	GATE_IFR3(CLK_IFR_CPUM, "ifr_cpum", "clk26m", 11),
	GATE_IFR3(CLK_IFR_AUXADC_MD, "ifr_auxadc_md", "clk26m", 14),
	GATE_IFR3(CLK_IFR_AP_DMA, "ifr_ap_dma", "axi_sel", 18),
	GATE_IFR3(CLK_IFR_DEBUGSYS, "ifr_debugsys", "axi_sel", 24),
	GATE_IFR3(CLK_IFR_AUDIO, "ifr_audio", "axi_sel", 25),
	/* IFR4 */
	GATE_IFR4(CLK_IFR_PWM_FBCLK6, "ifr_pwm_fbclk6", "pwm_sel", 0),
	GATE_IFR4(CLK_IFR_DISP_PWM, "ifr_disp_pwm", "disp_pwm_sel", 2),
	GATE_IFR4(CLK_IFR_AUD_26M_BK, "ifr_aud_26m_bk", "clk26m", 4),
	GATE_IFR4(CLK_IFR_CQ_DMA, "ifr_cq_dma", "axi_sel", 27),
	/* IFR5 */
	GATE_IFR5(CLK_IFR_MSDC0_SF, "ifr_msdc0_sf", "msdc50_0_sel", 0),
	GATE_IFR5(CLK_IFR_MSDC1_SF, "ifr_msdc1_sf", "msdc50_0_sel", 1),
	GATE_IFR5(CLK_IFR_MSDC2_SF, "ifr_msdc2_sf", "msdc50_0_sel", 2),
	GATE_IFR5(CLK_IFR_AP_MSDC0, "ifr_ap_msdc0", "msdc50_0_sel", 7),
	GATE_IFR5(CLK_IFR_MD_MSDC0, "ifr_md_msdc0", "msdc50_0_sel", 8),
	GATE_IFR5(CLK_IFR_MSDC0_SRC, "ifr_msdc0_src", "msdc50_0_sel", 9),
	GATE_IFR5(CLK_IFR_MSDC1_SRC, "ifr_msdc1_src", "msdc30_1_sel", 10),
	GATE_IFR5(CLK_IFR_MSDC2_SRC, "ifr_msdc2_src", "msdc50_2_sel", 11),
	GATE_IFR5(CLK_IFR_PWRAP_TMR, "ifr_pwrap_tmr", "clk26m", 12),
	GATE_IFR5(CLK_IFR_PWRAP_SPI, "ifr_pwrap_spi", "clk26m", 13),
	GATE_IFR5(CLK_IFR_PWRAP_SYS, "ifr_pwrap_sys", "clk26m", 14),
	GATE_MTK_FLAGS(CLK_IFR_MCU_PM_BK, "ifr_mcu_pm_bk", NULL, &ifr5_cg_regs,
			17, &mtk_clk_gate_ops_setclr, CLK_IGNORE_UNUSED),
	GATE_IFR5(CLK_IFR_IRRX_26M, "ifr_irrx_26m", "clk26m", 22),
	GATE_IFR5(CLK_IFR_IRRX_32K, "ifr_irrx_32k", "clk32k", 23),
	GATE_IFR5(CLK_IFR_I2C0_AXI, "ifr_i2c0_axi", "i2c_sel", 24),
	GATE_IFR5(CLK_IFR_I2C1_AXI, "ifr_i2c1_axi", "i2c_sel", 25),
	GATE_IFR5(CLK_IFR_I2C2_AXI, "ifr_i2c2_axi", "i2c_sel", 26),
	GATE_IFR5(CLK_IFR_I2C3_AXI, "ifr_i2c3_axi", "i2c_sel", 27),
	GATE_IFR5(CLK_IFR_NIC_AXI, "ifr_nic_axi", "axi_sel", 28),
	GATE_IFR5(CLK_IFR_NIC_SLV_AXI, "ifr_nic_slv_axi", "axi_sel", 29),
	GATE_IFR5(CLK_IFR_APU_AXI, "ifr_apu_axi", "axi_sel", 30),
	/* IFR6 */
	GATE_IFR6(CLK_IFR_NFIECC, "ifr_nfiecc", "nfiecc_sel", 0),
	GATE_IFR6(CLK_IFR_NFI1X_BK, "ifr_nfi1x_bk", "nfi2x_sel", 1),
	GATE_IFR6(CLK_IFR_NFIECC_BK, "ifr_nfiecc_bk", "nfi2x_sel", 2),
	GATE_IFR6(CLK_IFR_NFI_BK, "ifr_nfi_bk", "axi_sel", 3),
	GATE_IFR6(CLK_IFR_MSDC2_AP_BK, "ifr_msdc2_ap_bk", "axi_sel", 4),
	GATE_IFR6(CLK_IFR_MSDC2_MD_BK, "ifr_msdc2_md_bk", "axi_sel", 5),
	GATE_IFR6(CLK_IFR_MSDC2_BK, "ifr_msdc2_bk", "axi_sel", 6),
	GATE_IFR6(CLK_IFR_SUSB_133_BK, "ifr_susb_133_bk", "axi_sel", 7),
	GATE_IFR6(CLK_IFR_SUSB_66_BK, "ifr_susb_66_bk", "axi_sel", 8),
	GATE_IFR6(CLK_IFR_SSUSB_SYS, "ifr_ssusb_sys", "ssusb_sys_sel", 9),
	GATE_IFR6(CLK_IFR_SSUSB_REF, "ifr_ssusb_ref", "ssusb_sys_sel", 10),
	GATE_IFR6(CLK_IFR_SSUSB_XHCI, "ifr_ssusb_xhci", "ssusb_xhci_sel", 11),
};

static const struct mtk_gate_regs peri_cg_regs = {
	.set_ofs = 0x20c,
	.clr_ofs = 0x20c,
	.sta_ofs = 0x20c,
};

static const struct mtk_gate peri_clks[] = {
	GATE_MTK(CLK_PERIAXI, "periaxi", "axi_sel", &peri_cg_regs, 31,
		 &mtk_clk_gate_ops_no_setclr),
};

static const struct mtk_clk_desc topck_desc = {
	.clks = top_clk_gates,
	.num_clks = ARRAY_SIZE(top_clk_gates),
	.fixed_clks = top_fixed_clks,
	.num_fixed_clks = ARRAY_SIZE(top_fixed_clks),
	.factor_clks = top_divs,
	.num_factor_clks = ARRAY_SIZE(top_divs),
	.mux_clks = top_muxes,
	.num_mux_clks = ARRAY_SIZE(top_muxes),
	.composite_clks = top_misc_muxes,
	.num_composite_clks = ARRAY_SIZE(top_misc_muxes),
	.divider_clks = top_adj_divs,
	.num_divider_clks = ARRAY_SIZE(top_adj_divs),
	.clk_lock = &mt8365_clk_lock,
};

static const struct mtk_clk_desc infra_desc = {
	.clks = ifr_clks,
	.num_clks = ARRAY_SIZE(ifr_clks),
};

static const struct mtk_clk_desc peri_desc = {
	.clks = peri_clks,
	.num_clks = ARRAY_SIZE(peri_clks),
};

static const struct mtk_clk_desc mcu_desc = {
	.composite_clks = mcu_muxes,
	.num_composite_clks = ARRAY_SIZE(mcu_muxes),
	.clk_lock = &mt8365_clk_lock,
};

static const struct of_device_id of_match_clk_mt8365[] = {
	{ .compatible = "mediatek,mt8365-topckgen", .data = &topck_desc },
	{ .compatible = "mediatek,mt8365-infracfg", .data = &infra_desc },
	{ .compatible = "mediatek,mt8365-pericfg", .data = &peri_desc },
	{ .compatible = "mediatek,mt8365-mcucfg", .data = &mcu_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8365);

static struct platform_driver clk_mt8365_drv = {
	.driver = {
		.name = "clk-mt8365",
		.of_match_table = of_match_clk_mt8365,
	},
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
};
module_platform_driver(clk_mt8365_drv);
MODULE_LICENSE("GPL");
