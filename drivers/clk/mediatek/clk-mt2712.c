// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017 MediaTek Inc.
 * Author: Weiyi Lu <weiyi.lu@mediatek.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "clk-gate.h"
#include "clk-mtk.h"

#include <dt-bindings/clock/mt2712-clk.h>

static DEFINE_SPINLOCK(mt2712_clk_lock);

static const struct mtk_fixed_clk top_fixed_clks[] = {
	FIXED_CLK(CLK_TOP_VPLL3_DPIX, "vpll3_dpix", NULL, 200000000),
	FIXED_CLK(CLK_TOP_VPLL_DPIX, "vpll_dpix", NULL, 200000000),
	FIXED_CLK(CLK_TOP_LTEPLL_FS26M, "ltepll_fs26m", NULL, 26000000),
	FIXED_CLK(CLK_TOP_DMPLL, "dmpll_ck", NULL, 350000000),
	FIXED_CLK(CLK_TOP_DSI0_LNTC, "dsi0_lntc", NULL, 143000000),
	FIXED_CLK(CLK_TOP_DSI1_LNTC, "dsi1_lntc", NULL, 143000000),
	FIXED_CLK(CLK_TOP_LVDSTX3_CLKDIG_CTS, "lvdstx3", NULL, 140000000),
	FIXED_CLK(CLK_TOP_LVDSTX_CLKDIG_CTS, "lvdstx", NULL, 140000000),
	FIXED_CLK(CLK_TOP_CLKRTC_EXT, "clkrtc_ext", NULL, 32768),
	FIXED_CLK(CLK_TOP_CLKRTC_INT, "clkrtc_int", NULL, 32747),
	FIXED_CLK(CLK_TOP_CSI0, "csi0", NULL, 26000000),
	FIXED_CLK(CLK_TOP_CVBSPLL, "cvbspll", NULL, 108000000),
};

static const struct mtk_fixed_factor top_divs[] = {
	FACTOR(CLK_TOP_SYS_26M, "sys_26m", "clk26m", 1, 1),
	FACTOR(CLK_TOP_CLK26M_D2, "clk26m_d2", "sys_26m", 1, 2),
	FACTOR(CLK_TOP_ARMCA35PLL, "armca35pll_ck", "armca35pll", 1, 1),
	FACTOR(CLK_TOP_ARMCA35PLL_600M, "armca35pll_600m", "armca35pll_ck", 1, 2),
	FACTOR(CLK_TOP_ARMCA35PLL_400M, "armca35pll_400m", "armca35pll_ck", 1, 3),
	FACTOR(CLK_TOP_ARMCA72PLL, "armca72pll_ck", "armca72pll", 1, 1),
	FACTOR(CLK_TOP_SYSPLL, "syspll_ck", "mainpll", 1, 1),
	FACTOR(CLK_TOP_SYSPLL_D2, "syspll_d2", "syspll_ck", 1, 2),
	FACTOR(CLK_TOP_SYSPLL1_D2, "syspll1_d2", "syspll_d2", 1, 2),
	FACTOR(CLK_TOP_SYSPLL1_D4, "syspll1_d4", "syspll_d2", 1, 4),
	FACTOR(CLK_TOP_SYSPLL1_D8, "syspll1_d8", "syspll_d2", 1, 8),
	FACTOR(CLK_TOP_SYSPLL1_D16, "syspll1_d16", "syspll_d2", 1, 16),
	FACTOR(CLK_TOP_SYSPLL_D3, "syspll_d3", "syspll_ck", 1, 3),
	FACTOR(CLK_TOP_SYSPLL2_D2, "syspll2_d2", "syspll_d3", 1, 2),
	FACTOR(CLK_TOP_SYSPLL2_D4, "syspll2_d4", "syspll_d3", 1, 4),
	FACTOR(CLK_TOP_SYSPLL_D5, "syspll_d5", "syspll_ck", 1, 5),
	FACTOR(CLK_TOP_SYSPLL3_D2, "syspll3_d2", "syspll_d5", 1, 2),
	FACTOR(CLK_TOP_SYSPLL3_D4, "syspll3_d4", "syspll_d5", 1, 4),
	FACTOR(CLK_TOP_SYSPLL_D7, "syspll_d7", "syspll_ck", 1, 7),
	FACTOR(CLK_TOP_SYSPLL4_D2, "syspll4_d2", "syspll_d7", 1, 2),
	FACTOR(CLK_TOP_SYSPLL4_D4, "syspll4_d4", "syspll_d7", 1, 4),
	FACTOR(CLK_TOP_UNIVPLL, "univpll_ck", "univpll", 1, 1),
	FACTOR(CLK_TOP_UNIVPLL_D7, "univpll_d7", "univpll_ck", 1, 7),
	FACTOR(CLK_TOP_UNIVPLL_D26, "univpll_d26", "univpll_ck", 1, 26),
	FACTOR(CLK_TOP_UNIVPLL_D52, "univpll_d52", "univpll_ck", 1, 52),
	FACTOR(CLK_TOP_UNIVPLL_D104, "univpll_d104", "univpll_ck", 1, 104),
	FACTOR(CLK_TOP_UNIVPLL_D208, "univpll_d208", "univpll_ck", 1, 208),
	FACTOR(CLK_TOP_UNIVPLL_D2, "univpll_d2", "univpll_ck", 1, 2),
	FACTOR(CLK_TOP_UNIVPLL1_D2, "univpll1_d2", "univpll_d2", 1, 2),
	FACTOR(CLK_TOP_UNIVPLL1_D4, "univpll1_d4", "univpll_d2", 1, 4),
	FACTOR(CLK_TOP_UNIVPLL1_D8, "univpll1_d8", "univpll_d2", 1, 8),
	FACTOR(CLK_TOP_UNIVPLL_D3, "univpll_d3", "univpll_ck", 1, 3),
	FACTOR(CLK_TOP_UNIVPLL2_D2, "univpll2_d2", "univpll_d3", 1, 2),
	FACTOR(CLK_TOP_UNIVPLL2_D4, "univpll2_d4", "univpll_d3", 1, 4),
	FACTOR(CLK_TOP_UNIVPLL2_D8, "univpll2_d8", "univpll_d3", 1, 8),
	FACTOR(CLK_TOP_UNIVPLL_D5, "univpll_d5", "univpll_ck", 1, 5),
	FACTOR(CLK_TOP_UNIVPLL3_D2, "univpll3_d2", "univpll_d5", 1, 2),
	FACTOR(CLK_TOP_UNIVPLL3_D4, "univpll3_d4", "univpll_d5", 1, 4),
	FACTOR(CLK_TOP_UNIVPLL3_D8, "univpll3_d8", "univpll_d5", 1, 8),
	FACTOR(CLK_TOP_F_MP0_PLL1, "f_mp0_pll1_ck", "univpll_d2", 1, 1),
	FACTOR(CLK_TOP_F_MP0_PLL2, "f_mp0_pll2_ck", "univpll1_d2", 1, 1),
	FACTOR(CLK_TOP_F_BIG_PLL1, "f_big_pll1_ck", "univpll_d2", 1, 1),
	FACTOR(CLK_TOP_F_BIG_PLL2, "f_big_pll2_ck", "univpll1_d2", 1, 1),
	FACTOR(CLK_TOP_F_BUS_PLL1, "f_bus_pll1_ck", "univpll_d2", 1, 1),
	FACTOR(CLK_TOP_F_BUS_PLL2, "f_bus_pll2_ck", "univpll1_d2", 1, 1),
	FACTOR(CLK_TOP_APLL1, "apll1_ck", "apll1", 1, 1),
	FACTOR(CLK_TOP_APLL1_D2, "apll1_d2", "apll1_ck", 1, 2),
	FACTOR(CLK_TOP_APLL1_D4, "apll1_d4", "apll1_ck", 1, 4),
	FACTOR(CLK_TOP_APLL1_D8, "apll1_d8", "apll1_ck", 1, 8),
	FACTOR(CLK_TOP_APLL1_D16, "apll1_d16", "apll1_ck", 1, 16),
	FACTOR(CLK_TOP_APLL2, "apll2_ck", "apll2", 1, 1),
	FACTOR(CLK_TOP_APLL2_D2, "apll2_d2", "apll2_ck", 1, 2),
	FACTOR(CLK_TOP_APLL2_D4, "apll2_d4", "apll2_ck", 1, 4),
	FACTOR(CLK_TOP_APLL2_D8, "apll2_d8", "apll2_ck", 1, 8),
	FACTOR(CLK_TOP_APLL2_D16, "apll2_d16", "apll2_ck", 1, 16),
	FACTOR(CLK_TOP_LVDSPLL, "lvdspll_ck", "lvdspll", 1, 1),
	FACTOR(CLK_TOP_LVDSPLL_D2, "lvdspll_d2", "lvdspll_ck", 1, 2),
	FACTOR(CLK_TOP_LVDSPLL_D4, "lvdspll_d4", "lvdspll_ck", 1, 4),
	FACTOR(CLK_TOP_LVDSPLL_D8, "lvdspll_d8", "lvdspll_ck", 1, 8),
	FACTOR(CLK_TOP_LVDSPLL2, "lvdspll2_ck", "lvdspll2", 1, 1),
	FACTOR(CLK_TOP_LVDSPLL2_D2, "lvdspll2_d2", "lvdspll2_ck", 1, 2),
	FACTOR(CLK_TOP_LVDSPLL2_D4, "lvdspll2_d4", "lvdspll2_ck", 1, 4),
	FACTOR(CLK_TOP_LVDSPLL2_D8, "lvdspll2_d8", "lvdspll2_ck", 1, 8),
	FACTOR(CLK_TOP_ETHERPLL_125M, "etherpll_125m", "etherpll", 1, 1),
	FACTOR(CLK_TOP_ETHERPLL_50M, "etherpll_50m", "etherpll", 1, 1),
	FACTOR(CLK_TOP_CVBS, "cvbs", "cvbspll", 1, 1),
	FACTOR(CLK_TOP_CVBS_D2, "cvbs_d2", "cvbs", 1, 2),
	FACTOR(CLK_TOP_MMPLL, "mmpll_ck", "mmpll", 1, 1),
	FACTOR(CLK_TOP_MMPLL_D2, "mmpll_d2", "mmpll_ck", 1, 2),
	FACTOR(CLK_TOP_VENCPLL, "vencpll_ck", "vencpll", 1, 1),
	FACTOR(CLK_TOP_VENCPLL_D2, "vencpll_d2", "vencpll_ck", 1, 2),
	FACTOR(CLK_TOP_VCODECPLL, "vcodecpll_ck", "vcodecpll", 1, 1),
	FACTOR(CLK_TOP_VCODECPLL_D2, "vcodecpll_d2", "vcodecpll_ck", 1, 2),
	FACTOR(CLK_TOP_TVDPLL, "tvdpll_ck", "tvdpll", 1, 1),
	FACTOR(CLK_TOP_TVDPLL_D2, "tvdpll_d2", "tvdpll_ck", 1, 2),
	FACTOR(CLK_TOP_TVDPLL_D4, "tvdpll_d4", "tvdpll_ck", 1, 4),
	FACTOR(CLK_TOP_TVDPLL_D8, "tvdpll_d8", "tvdpll_ck", 1, 8),
	FACTOR(CLK_TOP_TVDPLL_429M, "tvdpll_429m", "tvdpll", 1, 1),
	FACTOR(CLK_TOP_TVDPLL_429M_D2, "tvdpll_429m_d2", "tvdpll_429m", 1, 2),
	FACTOR(CLK_TOP_TVDPLL_429M_D4, "tvdpll_429m_d4", "tvdpll_429m", 1, 4),
	FACTOR(CLK_TOP_MSDCPLL, "msdcpll_ck", "msdcpll", 1, 1),
	FACTOR(CLK_TOP_MSDCPLL_D2, "msdcpll_d2", "msdcpll_ck", 1, 2),
	FACTOR(CLK_TOP_MSDCPLL_D4, "msdcpll_d4", "msdcpll_ck", 1, 4),
	FACTOR(CLK_TOP_MSDCPLL2, "msdcpll2_ck", "msdcpll2", 1, 1),
	FACTOR(CLK_TOP_MSDCPLL2_D2, "msdcpll2_d2", "msdcpll2_ck", 1, 2),
	FACTOR(CLK_TOP_MSDCPLL2_D4, "msdcpll2_d4", "msdcpll2_ck", 1, 4),
	FACTOR(CLK_TOP_D2A_ULCLK_6P5M, "d2a_ulclk_6p5m", "clk26m", 1, 4),
	FACTOR(CLK_TOP_APLL1_D3, "apll1_d3", "apll1_ck", 1, 3),
	FACTOR(CLK_TOP_APLL2_D3, "apll2_d3", "apll2_ck", 1, 3),
};

static const char * const axi_parents[] = {
	"clk26m",
	"syspll1_d2",
	"syspll_d5",
	"syspll1_d4",
	"univpll_d5",
	"univpll2_d2",
	"msdcpll2_ck"
};

static const char * const mem_parents[] = {
	"clk26m",
	"dmpll_ck"
};

static const char * const mm_parents[] = {
	"clk26m",
	"vencpll_ck",
	"syspll_d3",
	"syspll1_d2",
	"syspll_d5",
	"syspll1_d4",
	"univpll1_d2",
	"univpll2_d2"
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
	"tvdpll_429m",
	"univpll_d3",
	"vencpll_ck",
	"syspll_d3",
	"univpll1_d2",
	"mmpll_d2",
	"syspll3_d2",
	"tvdpll_ck"
};

static const char * const venc_parents[] = {
	"clk26m",
	"univpll1_d2",
	"mmpll_d2",
	"tvdpll_d2",
	"syspll1_d2",
	"univpll_d5",
	"vcodecpll_d2",
	"univpll2_d2",
	"syspll3_d2"
};

static const char * const mfg_parents[] = {
	"clk26m",
	"mmpll_ck",
	"univpll_d3",
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
	"univpll_d52",
	"univpll_d208",
	"univpll_d104",
	"clk26m_d2",
	"univpll_d26",
	"univpll2_d8",
	"syspll3_d4",
	"syspll3_d2",
	"univpll1_d4",
	"univpll2_d2"
};

static const char * const uart_parents[] = {
	"clk26m",
	"univpll2_d8"
};

static const char * const spi_parents[] = {
	"clk26m",
	"univpll2_d4",
	"univpll1_d4",
	"univpll2_d2",
	"univpll3_d2",
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
	"univpll3_d4",
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
	"msdcpll_d4",
	"vencpll_d2",
	"univpll1_d2",
	"msdcpll2_ck",
	"msdcpll2_d2",
	"msdcpll2_d4"
};

static const char * const msdc30_1_parents[] = {
	"clk26m",
	"univpll2_d2",
	"msdcpll_d2",
	"univpll1_d4",
	"syspll2_d2",
	"univpll_d7",
	"vencpll_d2"
};

static const char * const msdc30_3_parents[] = {
	"clk26m",
	"msdcpll2_ck",
	"msdcpll2_d2",
	"univpll2_d2",
	"msdcpll2_d4",
	"univpll1_d4",
	"syspll2_d2",
	"syspll_d7",
	"univpll_d7",
	"vencpll_d2",
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
	"syspll3_d2",
	"syspll3_d4"
};

static const char * const pmicspi_parents[] = {
	"clk26m",
	"syspll1_d8",
	"syspll3_d4",
	"syspll1_d16",
	"univpll3_d4",
	"univpll_d26",
	"syspll3_d4"
};

static const char * const dpilvds1_parents[] = {
	"clk26m",
	"lvdspll2_ck",
	"lvdspll2_d2",
	"lvdspll2_d4",
	"lvdspll2_d8",
	"clkfpc"
};

static const char * const atb_parents[] = {
	"clk26m",
	"syspll1_d2",
	"univpll_d5",
	"syspll_d5"
};

static const char * const nr_parents[] = {
	"clk26m",
	"univpll1_d4",
	"syspll2_d2",
	"syspll1_d4",
	"univpll1_d8",
	"univpll3_d2",
	"univpll2_d2",
	"syspll_d5"
};

static const char * const nfi2x_parents[] = {
	"clk26m",
	"syspll4_d4",
	"univpll3_d4",
	"univpll1_d8",
	"syspll2_d4",
	"univpll3_d2",
	"syspll_d7",
	"syspll2_d2",
	"univpll2_d2",
	"syspll_d5",
	"syspll1_d2"
};

static const char * const irda_parents[] = {
	"clk26m",
	"univpll2_d4",
	"syspll2_d4",
	"univpll2_d8"
};

static const char * const cci400_parents[] = {
	"clk26m",
	"vencpll_ck",
	"armca35pll_600m",
	"armca35pll_400m",
	"univpll_d2",
	"syspll_d2",
	"msdcpll_ck",
	"univpll_d3"
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

static const char * const mem_mfg_parents[] = {
	"clk26m",
	"mmpll_ck",
	"univpll_d3"
};

static const char * const axi_mfg_parents[] = {
	"clk26m",
	"axi_sel",
	"univpll_d5"
};

static const char * const scam_parents[] = {
	"clk26m",
	"syspll3_d2",
	"univpll2_d4",
	"syspll2_d4"
};

static const char * const nfiecc_parents[] = {
	"clk26m",
	"nfi2x_sel",
	"syspll_d7",
	"syspll2_d2",
	"univpll2_d2",
	"univpll_d5",
	"syspll1_d2"
};

static const char * const pe2_mac_p0_parents[] = {
	"clk26m",
	"syspll1_d8",
	"syspll4_d2",
	"syspll2_d4",
	"univpll2_d4",
	"syspll3_d2"
};

static const char * const dpilvds_parents[] = {
	"clk26m",
	"lvdspll_ck",
	"lvdspll_d2",
	"lvdspll_d4",
	"lvdspll_d8",
	"clkfpc"
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

static const char * const spinor_parents[] = {
	"clk26m",
	"clk26m_d2",
	"syspll4_d4",
	"univpll2_d8",
	"univpll3_d4",
	"syspll4_d2",
	"syspll2_d4",
	"univpll2_d4",
	"etherpll_125m",
	"syspll1_d4"
};

static const char * const apll_parents[] = {
	"clk26m",
	"apll1_ck",
	"apll1_d2",
	"apll1_d4",
	"apll1_d8",
	"apll1_d16",
	"apll2_ck",
	"apll2_d2",
	"apll2_d4",
	"apll2_d8",
	"apll2_d16",
	"clk26m",
	"clk26m"
};

static const char * const a1sys_hp_parents[] = {
	"clk26m",
	"apll1_ck",
	"apll1_d2",
	"apll1_d4",
	"apll1_d8",
	"apll1_d3"
};

static const char * const a2sys_hp_parents[] = {
	"clk26m",
	"apll2_ck",
	"apll2_d2",
	"apll2_d4",
	"apll2_d8",
	"apll2_d3"
};

static const char * const asm_l_parents[] = {
	"clk26m",
	"univpll2_d4",
	"univpll2_d2",
	"syspll_d5"
};

static const char * const i2so1_parents[] = {
	"clk26m",
	"apll1_ck",
	"apll2_ck"
};

static const char * const ether_125m_parents[] = {
	"clk26m",
	"etherpll_125m",
	"univpll3_d2"
};

static const char * const ether_50m_parents[] = {
	"clk26m",
	"etherpll_50m",
	"apll1_d3",
	"univpll3_d4"
};

static const char * const jpgdec_parents[] = {
	"clk26m",
	"univpll_d3",
	"tvdpll_429m",
	"vencpll_ck",
	"syspll_d3",
	"vcodecpll_ck",
	"univpll1_d2",
	"armca35pll_400m",
	"tvdpll_429m_d2",
	"tvdpll_429m_d4"
};

static const char * const spislv_parents[] = {
	"clk26m",
	"univpll2_d4",
	"univpll1_d4",
	"univpll2_d2",
	"univpll3_d2",
	"univpll1_d8",
	"univpll1_d2",
	"univpll_d5"
};

static const char * const ether_parents[] = {
	"clk26m",
	"etherpll_50m",
	"univpll_d26"
};

static const char * const di_parents[] = {
	"clk26m",
	"tvdpll_d2",
	"tvdpll_d4",
	"tvdpll_d8",
	"vencpll_ck",
	"vencpll_d2",
	"cvbs",
	"cvbs_d2"
};

static const char * const tvd_parents[] = {
	"clk26m",
	"cvbs_d2",
	"univpll2_d8"
};

static const char * const i2c_parents[] = {
	"clk26m",
	"univpll_d26",
	"univpll2_d4",
	"univpll3_d2",
	"univpll1_d4"
};

static const char * const msdc0p_aes_parents[] = {
	"clk26m",
	"syspll_d2",
	"univpll_d3",
	"vcodecpll_ck"
};

static const char * const cmsys_parents[] = {
	"clk26m",
	"univpll_d3",
	"syspll_d3",
	"syspll1_d2",
	"syspll2_d2"
};

static const char * const gcpu_parents[] = {
	"clk26m",
	"syspll_d3",
	"syspll1_d2",
	"univpll1_d2",
	"univpll_d5",
	"univpll3_d2",
	"univpll_d3"
};

static const char * const aud_apll1_parents[] = {
	"apll1",
	"clkaud_ext_i_1"
};

static const char * const aud_apll2_parents[] = {
	"apll2",
	"clkaud_ext_i_2"
};

static const char * const apll1_ref_parents[] = {
	"clkaud_ext_i_2",
	"clkaud_ext_i_1",
	"clki2si0_mck_i",
	"clki2si1_mck_i",
	"clki2si2_mck_i",
	"clktdmin_mclk_i",
	"clki2si2_mck_i",
	"clktdmin_mclk_i"
};

static const char * const audull_vtx_parents[] = {
	"d2a_ulclk_6p5m",
	"clkaud_ext_i_0"
};

static struct mtk_composite top_muxes[] = {
	/* CLK_CFG_0 */
	MUX_GATE_FLAGS(CLK_TOP_AXI_SEL, "axi_sel", axi_parents, 0x040, 0, 3,
		       7, CLK_IS_CRITICAL),
	MUX_GATE_FLAGS(CLK_TOP_MEM_SEL, "mem_sel", mem_parents, 0x040, 8, 1,
		       15, CLK_IS_CRITICAL),
	MUX_GATE(CLK_TOP_MM_SEL, "mm_sel", mm_parents, 0x040, 24, 3, 31),
	/* CLK_CFG_1 */
	MUX_GATE(CLK_TOP_PWM_SEL, "pwm_sel", pwm_parents, 0x050, 0, 2, 7),
	MUX_GATE(CLK_TOP_VDEC_SEL, "vdec_sel", vdec_parents, 0x050, 8, 4, 15),
	MUX_GATE(CLK_TOP_VENC_SEL, "venc_sel", venc_parents, 0x050, 16, 4, 23),
	MUX_GATE(CLK_TOP_MFG_SEL, "mfg_sel", mfg_parents, 0x050, 24, 4, 31),
	/* CLK_CFG_2 */
	MUX_GATE(CLK_TOP_CAMTG_SEL, "camtg_sel", camtg_parents, 0x060, 0, 4, 7),
	MUX_GATE(CLK_TOP_UART_SEL, "uart_sel", uart_parents, 0x060, 8, 1, 15),
	MUX_GATE(CLK_TOP_SPI_SEL, "spi_sel", spi_parents, 0x060, 16, 3, 23),
	MUX_GATE(CLK_TOP_USB20_SEL, "usb20_sel", usb20_parents, 0x060, 24, 2, 31),
	/* CLK_CFG_3 */
	MUX_GATE(CLK_TOP_USB30_SEL, "usb30_sel", usb30_parents, 0x070, 0, 2, 7),
	MUX_GATE(CLK_TOP_MSDC50_0_HCLK_SEL, "msdc50_0_h_sel", msdc50_0_h_parents,
		 0x070, 8, 3, 15),
	MUX_GATE(CLK_TOP_MSDC50_0_SEL, "msdc50_0_sel", msdc50_0_parents,
		 0x070, 16, 4, 23),
	MUX_GATE(CLK_TOP_MSDC30_1_SEL, "msdc30_1_sel", msdc30_1_parents,
		 0x070, 24, 3, 31),
	/* CLK_CFG_4 */
	MUX_GATE(CLK_TOP_MSDC30_2_SEL, "msdc30_2_sel", msdc30_1_parents,
		 0x080, 0, 3, 7),
	MUX_GATE(CLK_TOP_MSDC30_3_SEL, "msdc30_3_sel", msdc30_3_parents,
		 0x080, 8, 4, 15),
	MUX_GATE(CLK_TOP_AUDIO_SEL, "audio_sel", audio_parents,
		 0x080, 16, 2, 23),
	MUX_GATE(CLK_TOP_AUD_INTBUS_SEL, "aud_intbus_sel", aud_intbus_parents,
		 0x080, 24, 3, 31),
	/* CLK_CFG_5 */
	MUX_GATE(CLK_TOP_PMICSPI_SEL, "pmicspi_sel", pmicspi_parents, 0x090, 0, 3, 7),
	MUX_GATE(CLK_TOP_DPILVDS1_SEL, "dpilvds1_sel", dpilvds1_parents,
		 0x090, 8, 3, 15),
	MUX_GATE(CLK_TOP_ATB_SEL, "atb_sel", atb_parents, 0x090, 16, 2, 23),
	MUX_GATE(CLK_TOP_NR_SEL, "nr_sel", nr_parents, 0x090, 24, 3, 31),
	/* CLK_CFG_6 */
	MUX_GATE(CLK_TOP_NFI2X_SEL, "nfi2x_sel", nfi2x_parents, 0x0a0, 0, 4, 7),
	MUX_GATE(CLK_TOP_IRDA_SEL, "irda_sel", irda_parents, 0x0a0, 8, 2, 15),
	MUX_GATE(CLK_TOP_CCI400_SEL, "cci400_sel", cci400_parents, 0x0a0, 16, 3, 23),
	MUX_GATE(CLK_TOP_AUD_1_SEL, "aud_1_sel", aud_1_parents, 0x0a0, 24, 2, 31),
	/* CLK_CFG_7 */
	MUX_GATE(CLK_TOP_AUD_2_SEL, "aud_2_sel", aud_2_parents, 0x0b0, 0, 2, 7),
	MUX_GATE(CLK_TOP_MEM_MFG_IN_AS_SEL, "mem_mfg_sel", mem_mfg_parents,
		 0x0b0, 8, 2, 15),
	MUX_GATE(CLK_TOP_AXI_MFG_IN_AS_SEL, "axi_mfg_sel", axi_mfg_parents,
		 0x0b0, 16, 2, 23),
	MUX_GATE(CLK_TOP_SCAM_SEL, "scam_sel", scam_parents, 0x0b0, 24, 2, 31),
	/* CLK_CFG_8 */
	MUX_GATE(CLK_TOP_NFIECC_SEL, "nfiecc_sel", nfiecc_parents, 0x0c0, 0, 3, 7),
	MUX_GATE(CLK_TOP_PE2_MAC_P0_SEL, "pe2_mac_p0_sel", pe2_mac_p0_parents,
		 0x0c0, 8, 3, 15),
	MUX_GATE(CLK_TOP_PE2_MAC_P1_SEL, "pe2_mac_p1_sel", pe2_mac_p0_parents,
		 0x0c0, 16, 3, 23),
	MUX_GATE(CLK_TOP_DPILVDS_SEL, "dpilvds_sel", dpilvds_parents, 0x0c0, 24, 3, 31),
	/* CLK_CFG_9 */
	MUX_GATE(CLK_TOP_MSDC50_3_HCLK_SEL, "msdc50_3_h_sel", msdc50_0_h_parents,
		 0x0d0, 0, 3, 7),
	MUX_GATE(CLK_TOP_HDCP_SEL, "hdcp_sel", hdcp_parents, 0x0d0, 8, 2, 15),
	MUX_GATE(CLK_TOP_HDCP_24M_SEL, "hdcp_24m_sel", hdcp_24m_parents,
		 0x0d0, 16, 2, 23),
	MUX_GATE_FLAGS(CLK_TOP_RTC_SEL, "rtc_sel", rtc_parents,
		       0x0d0, 24, 2, 31, CLK_IS_CRITICAL),
	/* CLK_CFG_10 */
	MUX_GATE(CLK_TOP_SPINOR_SEL, "spinor_sel", spinor_parents, 0x500, 0, 4, 7),
	MUX_GATE(CLK_TOP_APLL_SEL, "apll_sel", apll_parents, 0x500, 8, 4, 15),
	MUX_GATE(CLK_TOP_APLL2_SEL, "apll2_sel", apll_parents, 0x500, 16, 4, 23),
	MUX_GATE(CLK_TOP_A1SYS_HP_SEL, "a1sys_hp_sel", a1sys_hp_parents,
		 0x500, 24, 3, 31),
	/* CLK_CFG_11 */
	MUX_GATE(CLK_TOP_A2SYS_HP_SEL, "a2sys_hp_sel", a2sys_hp_parents, 0x510, 0, 3, 7),
	MUX_GATE(CLK_TOP_ASM_L_SEL, "asm_l_sel", asm_l_parents, 0x510, 8, 2, 15),
	MUX_GATE(CLK_TOP_ASM_M_SEL, "asm_m_sel", asm_l_parents, 0x510, 16, 2, 23),
	MUX_GATE(CLK_TOP_ASM_H_SEL, "asm_h_sel", asm_l_parents, 0x510, 24, 2, 31),
	/* CLK_CFG_12 */
	MUX_GATE(CLK_TOP_I2SO1_SEL, "i2so1_sel", i2so1_parents, 0x520, 0, 2, 7),
	MUX_GATE(CLK_TOP_I2SO2_SEL, "i2so2_sel", i2so1_parents, 0x520, 8, 2, 15),
	MUX_GATE(CLK_TOP_I2SO3_SEL, "i2so3_sel", i2so1_parents, 0x520, 16, 2, 23),
	MUX_GATE(CLK_TOP_TDMO0_SEL, "tdmo0_sel", i2so1_parents, 0x520, 24, 2, 31),
	/* CLK_CFG_13 */
	MUX_GATE(CLK_TOP_TDMO1_SEL, "tdmo1_sel", i2so1_parents, 0x530, 0, 2, 7),
	MUX_GATE(CLK_TOP_I2SI1_SEL, "i2si1_sel", i2so1_parents, 0x530, 8, 2, 15),
	MUX_GATE(CLK_TOP_I2SI2_SEL, "i2si2_sel", i2so1_parents, 0x530, 16, 2, 23),
	MUX_GATE(CLK_TOP_I2SI3_SEL, "i2si3_sel", i2so1_parents, 0x530, 24, 2, 31),
	/* CLK_CFG_14 */
	MUX_GATE(CLK_TOP_ETHER_125M_SEL, "ether_125m_sel", ether_125m_parents,
		 0x540, 0, 2, 7),
	MUX_GATE(CLK_TOP_ETHER_50M_SEL, "ether_50m_sel", ether_50m_parents,
		 0x540, 8, 2, 15),
	MUX_GATE(CLK_TOP_JPGDEC_SEL, "jpgdec_sel", jpgdec_parents, 0x540, 16, 4, 23),
	MUX_GATE(CLK_TOP_SPISLV_SEL, "spislv_sel", spislv_parents, 0x540, 24, 3, 31),
	/* CLK_CFG_15 */
	MUX_GATE(CLK_TOP_ETHER_50M_RMII_SEL, "ether_sel", ether_parents, 0x550, 0, 2, 7),
	MUX_GATE(CLK_TOP_CAM2TG_SEL, "cam2tg_sel", camtg_parents, 0x550, 8, 4, 15),
	MUX_GATE(CLK_TOP_DI_SEL, "di_sel", di_parents, 0x550, 16, 3, 23),
	MUX_GATE(CLK_TOP_TVD_SEL, "tvd_sel", tvd_parents, 0x550, 24, 2, 31),
	/* CLK_CFG_16 */
	MUX_GATE(CLK_TOP_I2C_SEL, "i2c_sel", i2c_parents, 0x560, 0, 3, 7),
	MUX_GATE(CLK_TOP_PWM_INFRA_SEL, "pwm_infra_sel", pwm_parents, 0x560, 8, 2, 15),
	MUX_GATE(CLK_TOP_MSDC0P_AES_SEL, "msdc0p_aes_sel", msdc0p_aes_parents,
		 0x560, 16, 2, 23),
	MUX_GATE(CLK_TOP_CMSYS_SEL, "cmsys_sel", cmsys_parents, 0x560, 24, 3, 31),
	/* CLK_CFG_17 */
	MUX_GATE(CLK_TOP_GCPU_SEL, "gcpu_sel", gcpu_parents, 0x570, 0, 3, 7),
	/* CLK_AUDDIV_4 */
	MUX(CLK_TOP_AUD_APLL1_SEL, "aud_apll1_sel", aud_apll1_parents, 0x134, 0, 1),
	MUX(CLK_TOP_AUD_APLL2_SEL, "aud_apll2_sel", aud_apll2_parents, 0x134, 1, 1),
	MUX(CLK_TOP_DA_AUDULL_VTX_6P5M_SEL, "audull_vtx_sel", audull_vtx_parents,
	    0x134, 31, 1),
	MUX(CLK_TOP_APLL1_REF_SEL, "apll1_ref_sel", apll1_ref_parents, 0x134, 4, 3),
	MUX(CLK_TOP_APLL2_REF_SEL, "apll2_ref_sel", apll1_ref_parents, 0x134, 7, 3),
};

static const char * const mcu_mp0_parents[] = {
	"clk26m",
	"armca35pll_ck",
	"f_mp0_pll1_ck",
	"f_mp0_pll2_ck"
};

static const char * const mcu_mp2_parents[] = {
	"clk26m",
	"armca72pll_ck",
	"f_big_pll1_ck",
	"f_big_pll2_ck"
};

static const char * const mcu_bus_parents[] = {
	"clk26m",
	"cci400_sel",
	"f_bus_pll1_ck",
	"f_bus_pll2_ck"
};

static struct mtk_composite mcu_muxes[] = {
	/* mp0_pll_divider_cfg */
	MUX_GATE_FLAGS(CLK_MCU_MP0_SEL, "mcu_mp0_sel", mcu_mp0_parents, 0x7A0,
		       9, 2, -1, CLK_IS_CRITICAL),
	/* mp2_pll_divider_cfg */
	MUX_GATE_FLAGS(CLK_MCU_MP2_SEL, "mcu_mp2_sel", mcu_mp2_parents, 0x7A8,
		       9, 2, -1, CLK_IS_CRITICAL),
	/* bus_pll_divider_cfg */
	MUX_GATE_FLAGS(CLK_MCU_BUS_SEL, "mcu_bus_sel", mcu_bus_parents, 0x7C0,
		       9, 2, -1, CLK_IS_CRITICAL),
};

static const struct mtk_clk_divider top_adj_divs[] = {
	DIV_ADJ(CLK_TOP_APLL_DIV0, "apll_div0", "i2so1_sel", 0x124, 0, 8),
	DIV_ADJ(CLK_TOP_APLL_DIV1, "apll_div1", "i2so2_sel", 0x124, 8, 8),
	DIV_ADJ(CLK_TOP_APLL_DIV2, "apll_div2", "i2so3_sel", 0x124, 16, 8),
	DIV_ADJ(CLK_TOP_APLL_DIV3, "apll_div3", "tdmo0_sel", 0x124, 24, 8),
	DIV_ADJ(CLK_TOP_APLL_DIV4, "apll_div4", "tdmo1_sel", 0x128, 0, 8),
	DIV_ADJ(CLK_TOP_APLL_DIV5, "apll_div5", "i2si1_sel", 0x128, 8, 8),
	DIV_ADJ(CLK_TOP_APLL_DIV6, "apll_div6", "i2si2_sel", 0x128, 16, 8),
	DIV_ADJ(CLK_TOP_APLL_DIV7, "apll_div7", "i2si3_sel", 0x128, 24, 8),
};

static const struct mtk_gate_regs top0_cg_regs = {
	.set_ofs = 0x120,
	.clr_ofs = 0x120,
	.sta_ofs = 0x120,
};

static const struct mtk_gate_regs top1_cg_regs = {
	.set_ofs = 0x424,
	.clr_ofs = 0x424,
	.sta_ofs = 0x424,
};

#define GATE_TOP0(_id, _name, _parent, _shift)				\
	GATE_MTK(_id, _name, _parent, &top0_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr)

#define GATE_TOP1(_id, _name, _parent, _shift)				\
	GATE_MTK(_id, _name, _parent, &top1_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr_inv)

static const struct mtk_gate top_clks[] = {
	/* TOP0 */
	GATE_TOP0(CLK_TOP_APLL_DIV_PDN0, "apll_div_pdn0", "i2so1_sel", 0),
	GATE_TOP0(CLK_TOP_APLL_DIV_PDN1, "apll_div_pdn1", "i2so2_sel", 1),
	GATE_TOP0(CLK_TOP_APLL_DIV_PDN2, "apll_div_pdn2", "i2so3_sel", 2),
	GATE_TOP0(CLK_TOP_APLL_DIV_PDN3, "apll_div_pdn3", "tdmo0_sel", 3),
	GATE_TOP0(CLK_TOP_APLL_DIV_PDN4, "apll_div_pdn4", "tdmo1_sel", 4),
	GATE_TOP0(CLK_TOP_APLL_DIV_PDN5, "apll_div_pdn5", "i2si1_sel", 5),
	GATE_TOP0(CLK_TOP_APLL_DIV_PDN6, "apll_div_pdn6", "i2si2_sel", 6),
	GATE_TOP0(CLK_TOP_APLL_DIV_PDN7, "apll_div_pdn7", "i2si3_sel", 7),
	/* TOP1 */
	GATE_TOP1(CLK_TOP_NFI2X_EN, "nfi2x_en", "nfi2x_sel", 0),
	GATE_TOP1(CLK_TOP_NFIECC_EN, "nfiecc_en", "nfiecc_sel", 1),
	GATE_TOP1(CLK_TOP_NFI1X_CK_EN, "nfi1x_ck_en", "nfi2x_sel", 2),
};

static const struct mtk_gate_regs infra_cg_regs = {
	.set_ofs = 0x40,
	.clr_ofs = 0x44,
	.sta_ofs = 0x48,
};

#define GATE_INFRA(_id, _name, _parent, _shift)				\
	GATE_MTK(_id, _name, _parent, &infra_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate infra_clks[] = {
	GATE_INFRA(CLK_INFRA_DBGCLK, "infra_dbgclk", "axi_sel", 0),
	GATE_INFRA(CLK_INFRA_GCE, "infra_gce", "axi_sel", 6),
	GATE_INFRA(CLK_INFRA_M4U, "infra_m4u", "mem_sel", 8),
	GATE_INFRA(CLK_INFRA_KP, "infra_kp", "axi_sel", 16),
	GATE_INFRA(CLK_INFRA_AO_SPI0, "infra_ao_spi0", "spi_sel", 24),
	GATE_INFRA(CLK_INFRA_AO_SPI1, "infra_ao_spi1", "spislv_sel", 25),
	GATE_INFRA(CLK_INFRA_AO_UART5, "infra_ao_uart5", "axi_sel", 26),
};

static const struct mtk_gate_regs peri0_cg_regs = {
	.set_ofs = 0x8,
	.clr_ofs = 0x10,
	.sta_ofs = 0x18,
};

static const struct mtk_gate_regs peri1_cg_regs = {
	.set_ofs = 0xc,
	.clr_ofs = 0x14,
	.sta_ofs = 0x1c,
};

static const struct mtk_gate_regs peri2_cg_regs = {
	.set_ofs = 0x42c,
	.clr_ofs = 0x42c,
	.sta_ofs = 0x42c,
};

#define GATE_PERI0(_id, _name, _parent, _shift)				\
	GATE_MTK(_id, _name, _parent, &peri0_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

#define GATE_PERI1(_id, _name, _parent, _shift)				\
	GATE_MTK(_id, _name, _parent, &peri1_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

#define GATE_PERI2(_id, _name, _parent, _shift)				\
	GATE_MTK(_id, _name, _parent, &peri2_cg_regs, _shift, &mtk_clk_gate_ops_no_setclr_inv)

static const struct mtk_gate peri_clks[] = {
	/* PERI0 */
	GATE_PERI0(CLK_PERI_NFI, "per_nfi", "axi_sel", 0),
	GATE_PERI0(CLK_PERI_THERM, "per_therm", "axi_sel", 1),
	GATE_PERI0(CLK_PERI_PWM0, "per_pwm0", "pwm_sel", 2),
	GATE_PERI0(CLK_PERI_PWM1, "per_pwm1", "pwm_sel", 3),
	GATE_PERI0(CLK_PERI_PWM2, "per_pwm2", "pwm_sel", 4),
	GATE_PERI0(CLK_PERI_PWM3, "per_pwm3", "pwm_sel", 5),
	GATE_PERI0(CLK_PERI_PWM4, "per_pwm4", "pwm_sel", 6),
	GATE_PERI0(CLK_PERI_PWM5, "per_pwm5", "pwm_sel", 7),
	GATE_PERI0(CLK_PERI_PWM6, "per_pwm6", "pwm_sel", 8),
	GATE_PERI0(CLK_PERI_PWM7, "per_pwm7", "pwm_sel", 9),
	GATE_PERI0(CLK_PERI_PWM, "per_pwm", "pwm_sel", 10),
	GATE_PERI0(CLK_PERI_AP_DMA, "per_ap_dma", "axi_sel", 13),
	GATE_PERI0(CLK_PERI_MSDC30_0, "per_msdc30_0", "msdc50_0_sel", 14),
	GATE_PERI0(CLK_PERI_MSDC30_1, "per_msdc30_1", "msdc30_1_sel", 15),
	GATE_PERI0(CLK_PERI_MSDC30_2, "per_msdc30_2", "msdc30_2_sel", 16),
	GATE_PERI0(CLK_PERI_MSDC30_3, "per_msdc30_3", "msdc30_3_sel", 17),
	GATE_PERI0(CLK_PERI_UART0, "per_uart0", "uart_sel", 20),
	GATE_PERI0(CLK_PERI_UART1, "per_uart1", "uart_sel", 21),
	GATE_PERI0(CLK_PERI_UART2, "per_uart2", "uart_sel", 22),
	GATE_PERI0(CLK_PERI_UART3, "per_uart3", "uart_sel", 23),
	GATE_PERI0(CLK_PERI_I2C0, "per_i2c0", "axi_sel", 24),
	GATE_PERI0(CLK_PERI_I2C1, "per_i2c1", "axi_sel", 25),
	GATE_PERI0(CLK_PERI_I2C2, "per_i2c2", "axi_sel", 26),
	GATE_PERI0(CLK_PERI_I2C3, "per_i2c3", "axi_sel", 27),
	GATE_PERI0(CLK_PERI_I2C4, "per_i2c4", "axi_sel", 28),
	GATE_PERI0(CLK_PERI_AUXADC, "per_auxadc", "ltepll_fs26m", 29),
	GATE_PERI0(CLK_PERI_SPI0, "per_spi0", "spi_sel", 30),
	/* PERI1 */
	GATE_PERI1(CLK_PERI_SPI, "per_spi", "spinor_sel", 1),
	GATE_PERI1(CLK_PERI_I2C5, "per_i2c5", "axi_sel", 3),
	GATE_PERI1(CLK_PERI_SPI2, "per_spi2", "spi_sel", 5),
	GATE_PERI1(CLK_PERI_SPI3, "per_spi3", "spi_sel", 6),
	GATE_PERI1(CLK_PERI_SPI5, "per_spi5", "spi_sel", 8),
	GATE_PERI1(CLK_PERI_UART4, "per_uart4", "uart_sel", 9),
	GATE_PERI1(CLK_PERI_SFLASH, "per_sflash", "uart_sel", 11),
	GATE_PERI1(CLK_PERI_GMAC, "per_gmac", "uart_sel", 12),
	GATE_PERI1(CLK_PERI_PCIE0, "per_pcie0", "uart_sel", 14),
	GATE_PERI1(CLK_PERI_PCIE1, "per_pcie1", "uart_sel", 15),
	GATE_PERI1(CLK_PERI_GMAC_PCLK, "per_gmac_pclk", "uart_sel", 16),
	/* PERI2 */
	GATE_PERI2(CLK_PERI_MSDC50_0_EN, "per_msdc50_0_en", "msdc50_0_sel", 0),
	GATE_PERI2(CLK_PERI_MSDC30_1_EN, "per_msdc30_1_en", "msdc30_1_sel", 1),
	GATE_PERI2(CLK_PERI_MSDC30_2_EN, "per_msdc30_2_en", "msdc30_2_sel", 2),
	GATE_PERI2(CLK_PERI_MSDC30_3_EN, "per_msdc30_3_en", "msdc30_3_sel", 3),
	GATE_PERI2(CLK_PERI_MSDC50_0_HCLK_EN, "per_msdc50_0_h", "msdc50_0_h_sel", 4),
	GATE_PERI2(CLK_PERI_MSDC50_3_HCLK_EN, "per_msdc50_3_h", "msdc50_3_h_sel", 5),
	GATE_PERI2(CLK_PERI_MSDC30_0_QTR_EN, "per_msdc30_0_q", "axi_sel", 6),
	GATE_PERI2(CLK_PERI_MSDC30_3_QTR_EN, "per_msdc30_3_q", "mem_sel", 7),
};

static u16 infrasys_rst_ofs[] = { 0x30, 0x34, };
static u16 pericfg_rst_ofs[] = { 0x0, 0x4, };

static const struct mtk_clk_rst_desc clk_rst_desc[] = {
	/* infra */
	{
		.version = MTK_RST_SIMPLE,
		.rst_bank_ofs = infrasys_rst_ofs,
		.rst_bank_nr = ARRAY_SIZE(infrasys_rst_ofs),
	},
	/* peri */
	{
		.version = MTK_RST_SIMPLE,
		.rst_bank_ofs = pericfg_rst_ofs,
		.rst_bank_nr = ARRAY_SIZE(pericfg_rst_ofs),
	},
};

static const struct mtk_clk_desc topck_desc = {
	.clks = top_clks,
	.num_clks = ARRAY_SIZE(top_clks),
	.fixed_clks = top_fixed_clks,
	.num_fixed_clks = ARRAY_SIZE(top_fixed_clks),
	.factor_clks = top_divs,
	.num_factor_clks = ARRAY_SIZE(top_divs),
	.composite_clks = top_muxes,
	.num_composite_clks = ARRAY_SIZE(top_muxes),
	.divider_clks = top_adj_divs,
	.num_divider_clks = ARRAY_SIZE(top_adj_divs),
	.clk_lock = &mt2712_clk_lock,
};

static const struct mtk_clk_desc mcu_desc = {
	.composite_clks = mcu_muxes,
	.num_composite_clks = ARRAY_SIZE(mcu_muxes),
	.clk_lock = &mt2712_clk_lock,
};

static const struct mtk_clk_desc infra_desc = {
	.clks = infra_clks,
	.num_clks = ARRAY_SIZE(infra_clks),
	.rst_desc = &clk_rst_desc[0],
};

static const struct mtk_clk_desc peri_desc = {
	.clks = peri_clks,
	.num_clks = ARRAY_SIZE(peri_clks),
	.rst_desc = &clk_rst_desc[1],
};

static const struct of_device_id of_match_clk_mt2712[] = {
	{ .compatible = "mediatek,mt2712-infracfg", .data = &infra_desc },
	{ .compatible = "mediatek,mt2712-mcucfg", .data = &mcu_desc },
	{ .compatible = "mediatek,mt2712-pericfg", .data = &peri_desc, },
	{ .compatible = "mediatek,mt2712-topckgen", .data = &topck_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt2712);

static struct platform_driver clk_mt2712_drv = {
	.probe = mtk_clk_simple_probe,
	.remove_new = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt2712",
		.of_match_table = of_match_clk_mt2712,
	},
};
module_platform_driver(clk_mt2712_drv);
MODULE_LICENSE("GPL");
