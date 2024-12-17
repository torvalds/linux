// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 * Author: Sam Shih <sam.shih@mediatek.com>
 * Author: Wenzhen Yu <wenzhen.yu@mediatek.com>
 * Author: Jianhui Zhao <zhaojh329@gmail.com>
 */


#include <linux/clk-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include "clk-mtk.h"
#include "clk-gate.h"
#include "clk-mux.h"

#include <dt-bindings/clock/mediatek,mt7981-clk.h>
#include <linux/clk.h>

static DEFINE_SPINLOCK(mt7981_clk_lock);

static const struct mtk_fixed_factor top_divs[] = {
	FACTOR(CLK_TOP_CB_CKSQ_40M, "cb_cksq_40m", "clkxtal", 1, 1),
	FACTOR(CLK_TOP_CB_M_416M, "cb_m_416m", "mpll", 1, 1),
	FACTOR(CLK_TOP_CB_M_D2, "cb_m_d2", "mpll", 1, 2),
	FACTOR(CLK_TOP_CB_M_D3, "cb_m_d3", "mpll", 1, 3),
	FACTOR(CLK_TOP_M_D3_D2, "m_d3_d2", "mpll", 1, 2),
	FACTOR(CLK_TOP_CB_M_D4, "cb_m_d4", "mpll", 1, 4),
	FACTOR(CLK_TOP_CB_M_D8, "cb_m_d8", "mpll", 1, 8),
	FACTOR(CLK_TOP_M_D8_D2, "m_d8_d2", "mpll", 1, 16),
	FACTOR(CLK_TOP_CB_MM_720M, "cb_mm_720m", "mmpll", 1, 1),
	FACTOR(CLK_TOP_CB_MM_D2, "cb_mm_d2", "mmpll", 1, 2),
	FACTOR(CLK_TOP_CB_MM_D3, "cb_mm_d3", "mmpll", 1, 3),
	FACTOR(CLK_TOP_CB_MM_D3_D5, "cb_mm_d3_d5", "mmpll", 1, 15),
	FACTOR(CLK_TOP_CB_MM_D4, "cb_mm_d4", "mmpll", 1, 4),
	FACTOR(CLK_TOP_CB_MM_D6, "cb_mm_d6", "mmpll", 1, 6),
	FACTOR(CLK_TOP_MM_D6_D2, "mm_d6_d2", "mmpll", 1, 12),
	FACTOR(CLK_TOP_CB_MM_D8, "cb_mm_d8", "mmpll", 1, 8),
	FACTOR(CLK_TOP_CB_APLL2_196M, "cb_apll2_196m", "apll2", 1, 1),
	FACTOR(CLK_TOP_APLL2_D2, "apll2_d2", "apll2", 1, 2),
	FACTOR(CLK_TOP_APLL2_D4, "apll2_d4", "apll2", 1, 4),
	FACTOR(CLK_TOP_NET1_2500M, "net1_2500m", "net1pll", 1, 1),
	FACTOR(CLK_TOP_CB_NET1_D4, "cb_net1_d4", "net1pll", 1, 4),
	FACTOR(CLK_TOP_CB_NET1_D5, "cb_net1_d5", "net1pll", 1, 5),
	FACTOR(CLK_TOP_NET1_D5_D2, "net1_d5_d2", "net1pll", 1, 10),
	FACTOR(CLK_TOP_NET1_D5_D4, "net1_d5_d4", "net1pll", 1, 20),
	FACTOR(CLK_TOP_CB_NET1_D8, "cb_net1_d8", "net1pll", 1, 8),
	FACTOR(CLK_TOP_NET1_D8_D2, "net1_d8_d2", "net1pll", 1, 16),
	FACTOR(CLK_TOP_NET1_D8_D4, "net1_d8_d4", "net1pll", 1, 32),
	FACTOR(CLK_TOP_CB_NET2_800M, "cb_net2_800m", "net2pll", 1, 1),
	FACTOR(CLK_TOP_CB_NET2_D2, "cb_net2_d2", "net2pll", 1, 2),
	FACTOR(CLK_TOP_CB_NET2_D4, "cb_net2_d4", "net2pll", 1, 4),
	FACTOR(CLK_TOP_NET2_D4_D2, "net2_d4_d2", "net2pll", 1, 8),
	FACTOR(CLK_TOP_NET2_D4_D4, "net2_d4_d4", "net2pll", 1, 16),
	FACTOR(CLK_TOP_CB_NET2_D6, "cb_net2_d6", "net2pll", 1, 6),
	FACTOR(CLK_TOP_CB_WEDMCU_208M, "cb_wedmcu_208m", "wedmcupll", 1, 1),
	FACTOR(CLK_TOP_CB_SGM_325M, "cb_sgm_325m", "sgmpll", 1, 1),
	FACTOR(CLK_TOP_CKSQ_40M_D2, "cksq_40m_d2", "cb_cksq_40m", 1, 2),
	FACTOR(CLK_TOP_CB_RTC_32K, "cb_rtc_32k", "cb_cksq_40m", 1, 1250),
	FACTOR(CLK_TOP_CB_RTC_32P7K, "cb_rtc_32p7k", "cb_cksq_40m", 1, 1220),
	FACTOR(CLK_TOP_USB_TX250M, "usb_tx250m", "cb_cksq_40m", 1, 1),
	FACTOR(CLK_TOP_FAUD, "faud", "aud_sel", 1, 1),
	FACTOR(CLK_TOP_NFI1X, "nfi1x", "nfi1x_sel", 1, 1),
	FACTOR(CLK_TOP_USB_EQ_RX250M, "usb_eq_rx250m", "cb_cksq_40m", 1, 1),
	FACTOR(CLK_TOP_USB_CDR_CK, "usb_cdr", "cb_cksq_40m", 1, 1),
	FACTOR(CLK_TOP_USB_LN0_CK, "usb_ln0", "cb_cksq_40m", 1, 1),
	FACTOR(CLK_TOP_SPINFI_BCK, "spinfi_bck", "spinfi_sel", 1, 1),
	FACTOR(CLK_TOP_SPI, "spi", "spi_sel", 1, 1),
	FACTOR(CLK_TOP_SPIM_MST, "spim_mst", "spim_mst_sel", 1, 1),
	FACTOR(CLK_TOP_UART_BCK, "uart_bck", "uart_sel", 1, 1),
	FACTOR(CLK_TOP_PWM_BCK, "pwm_bck", "pwm_sel", 1, 1),
	FACTOR(CLK_TOP_I2C_BCK, "i2c_bck", "i2c_sel", 1, 1),
	FACTOR(CLK_TOP_PEXTP_TL, "pextp_tl", "pextp_tl_ck_sel", 1, 1),
	FACTOR(CLK_TOP_EMMC_208M, "emmc_208m", "emmc_208m_sel", 1, 1),
	FACTOR(CLK_TOP_EMMC_400M, "emmc_400m", "emmc_400m_sel", 1, 1),
	FACTOR(CLK_TOP_DRAMC_REF, "dramc_ref", "dramc_sel", 1, 1),
	FACTOR(CLK_TOP_DRAMC_MD32, "dramc_md32", "dramc_md32_sel", 1, 1),
	FACTOR(CLK_TOP_SYSAXI, "sysaxi", "sysaxi_sel", 1, 1),
	FACTOR(CLK_TOP_SYSAPB, "sysapb", "sysapb_sel", 1, 1),
	FACTOR(CLK_TOP_ARM_DB_MAIN, "arm_db_main", "arm_db_main_sel", 1, 1),
	FACTOR(CLK_TOP_AP2CNN_HOST, "ap2cnn_host", "ap2cnn_host_sel", 1, 1),
	FACTOR(CLK_TOP_NETSYS, "netsys", "netsys_sel", 1, 1),
	FACTOR(CLK_TOP_NETSYS_500M, "netsys_500m", "netsys_500m_sel", 1, 1),
	FACTOR(CLK_TOP_NETSYS_WED_MCU, "netsys_wed_mcu", "netsys_mcu_sel", 1, 1),
	FACTOR(CLK_TOP_NETSYS_2X, "netsys_2x", "netsys_2x_sel", 1, 1),
	FACTOR(CLK_TOP_SGM_325M, "sgm_325m", "sgm_325m_sel", 1, 1),
	FACTOR(CLK_TOP_SGM_REG, "sgm_reg", "sgm_reg_sel", 1, 1),
	FACTOR(CLK_TOP_F26M, "csw_f26m", "csw_f26m_sel", 1, 1),
	FACTOR(CLK_TOP_EIP97B, "eip97b", "eip97b_sel", 1, 1),
	FACTOR(CLK_TOP_USB3_PHY, "usb3_phy", "usb3_phy_sel", 1, 1),
	FACTOR(CLK_TOP_AUD, "aud", "faud", 1, 1),
	FACTOR(CLK_TOP_A1SYS, "a1sys", "a1sys_sel", 1, 1),
	FACTOR(CLK_TOP_AUD_L, "aud_l", "aud_l_sel", 1, 1),
	FACTOR(CLK_TOP_A_TUNER, "a_tuner", "a_tuner_sel", 1, 1),
	FACTOR(CLK_TOP_U2U3_REF, "u2u3_ref", "u2u3_sel", 1, 1),
	FACTOR(CLK_TOP_U2U3_SYS, "u2u3_sys", "u2u3_sys_sel", 1, 1),
	FACTOR(CLK_TOP_U2U3_XHCI, "u2u3_xhci", "u2u3_xhci_sel", 1, 1),
	FACTOR(CLK_TOP_USB_FRMCNT, "usb_frmcnt", "usb_frmcnt_sel", 1, 1),
};

static const char * const nfi1x_parents[] __initconst = {
	"cb_cksq_40m",
	"cb_mm_d4",
	"net1_d8_d2",
	"cb_net2_d6",
	"cb_m_d4",
	"cb_mm_d8",
	"net1_d8_d4",
	"cb_m_d8"
};

static const char * const spinfi_parents[] __initconst = {
	"cksq_40m_d2",
	"cb_cksq_40m",
	"net1_d5_d4",
	"cb_m_d4",
	"cb_mm_d8",
	"net1_d8_d4",
	"mm_d6_d2",
	"cb_m_d8"
};

static const char * const spi_parents[] __initconst = {
	"cb_cksq_40m",
	"cb_m_d2",
	"cb_mm_d4",
	"net1_d8_d2",
	"cb_net2_d6",
	"net1_d5_d4",
	"cb_m_d4",
	"net1_d8_d4"
};

static const char * const uart_parents[] __initconst = {
	"cb_cksq_40m",
	"cb_m_d8",
	"m_d8_d2"
};

static const char * const pwm_parents[] __initconst = {
	"cb_cksq_40m",
	"net1_d8_d2",
	"net1_d5_d4",
	"cb_m_d4",
	"m_d8_d2",
	"cb_rtc_32k"
};

static const char * const i2c_parents[] __initconst = {
	"cb_cksq_40m",
	"net1_d5_d4",
	"cb_m_d4",
	"net1_d8_d4"
};

static const char * const pextp_tl_ck_parents[] __initconst = {
	"cb_cksq_40m",
	"net1_d5_d4",
	"cb_m_d4",
	"cb_rtc_32k"
};

static const char * const emmc_208m_parents[] __initconst = {
	"cb_cksq_40m",
	"cb_m_d2",
	"cb_net2_d4",
	"cb_apll2_196m",
	"cb_mm_d4",
	"net1_d8_d2",
	"cb_mm_d6"
};

static const char * const emmc_400m_parents[] __initconst = {
	"cb_cksq_40m",
	"cb_net2_d2",
	"cb_mm_d2",
	"cb_net2_d2"
};

static const char * const csw_f26m_parents[] __initconst = {
	"cksq_40m_d2",
	"m_d8_d2"
};

static const char * const dramc_md32_parents[] __initconst = {
	"cb_cksq_40m",
	"cb_m_d2",
	"cb_wedmcu_208m"
};

static const char * const sysaxi_parents[] __initconst = {
	"cb_cksq_40m",
	"net1_d8_d2"
};

static const char * const sysapb_parents[] __initconst = {
	"cb_cksq_40m",
	"m_d3_d2"
};

static const char * const arm_db_main_parents[] __initconst = {
	"cb_cksq_40m",
	"cb_net2_d6"
};

static const char * const ap2cnn_host_parents[] __initconst = {
	"cb_cksq_40m",
	"net1_d8_d4"
};

static const char * const netsys_parents[] __initconst = {
	"cb_cksq_40m",
	"cb_mm_d2"
};

static const char * const netsys_500m_parents[] __initconst = {
	"cb_cksq_40m",
	"cb_net1_d5"
};

static const char * const netsys_mcu_parents[] __initconst = {
	"cb_cksq_40m",
	"cb_mm_720m",
	"cb_net1_d4",
	"cb_net1_d5",
	"cb_m_416m"
};

static const char * const netsys_2x_parents[] __initconst = {
	"cb_cksq_40m",
	"cb_net2_800m",
	"cb_mm_720m"
};

static const char * const sgm_325m_parents[] __initconst = {
	"cb_cksq_40m",
	"cb_sgm_325m"
};

static const char * const sgm_reg_parents[] __initconst = {
	"cb_cksq_40m",
	"cb_net2_d4"
};

static const char * const eip97b_parents[] __initconst = {
	"cb_cksq_40m",
	"cb_net1_d5",
	"cb_m_416m",
	"cb_mm_d2",
	"net1_d5_d2"
};

static const char * const aud_parents[] __initconst = {
	"cb_cksq_40m",
	"cb_apll2_196m"
};

static const char * const a1sys_parents[] __initconst = {
	"cb_cksq_40m",
	"apll2_d4"
};

static const char * const aud_l_parents[] __initconst = {
	"cb_cksq_40m",
	"cb_apll2_196m",
	"m_d8_d2"
};

static const char * const a_tuner_parents[] __initconst = {
	"cb_cksq_40m",
	"apll2_d4",
	"m_d8_d2"
};

static const char * const u2u3_parents[] __initconst = {
	"cb_cksq_40m",
	"m_d8_d2"
};

static const char * const u2u3_sys_parents[] __initconst = {
	"cb_cksq_40m",
	"net1_d5_d4"
};

static const char * const usb_frmcnt_parents[] __initconst = {
	"cb_cksq_40m",
	"cb_mm_d3_d5"
};

static const struct mtk_mux top_muxes[] = {
	/* CLK_CFG_0 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_NFI1X_SEL, "nfi1x_sel", nfi1x_parents,
			     0x000, 0x004, 0x008, 0, 3, 7, 0x1C0, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SPINFI_SEL, "spinfi_sel", spinfi_parents,
			     0x000, 0x004, 0x008, 8, 3, 15, 0x1C0, 1),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SPI_SEL, "spi_sel", spi_parents,
			     0x000, 0x004, 0x008, 16, 3, 23, 0x1C0, 2),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SPIM_MST_SEL, "spim_mst_sel", spi_parents,
			     0x000, 0x004, 0x008, 24, 3, 31, 0x1C0, 3),
	/* CLK_CFG_1 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_UART_SEL, "uart_sel", uart_parents,
			     0x010, 0x014, 0x018, 0, 2, 7, 0x1C0, 4),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_PWM_SEL, "pwm_sel", pwm_parents,
			     0x010, 0x014, 0x018, 8, 3, 15, 0x1C0, 5),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_I2C_SEL, "i2c_sel", i2c_parents,
			     0x010, 0x014, 0x018, 16, 2, 23, 0x1C0, 6),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_PEXTP_TL_SEL, "pextp_tl_ck_sel",
			     pextp_tl_ck_parents, 0x010, 0x014, 0x018, 24, 2, 31,
			     0x1C0, 7),
	/* CLK_CFG_2 */
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_EMMC_208M_SEL, "emmc_208m_sel",
				   emmc_208m_parents, 0x020, 0x024, 0x028, 0, 3, 7,
				   0x1C0, 8, 0),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_EMMC_400M_SEL, "emmc_400m_sel",
				   emmc_400m_parents, 0x020, 0x024, 0x028, 8, 2, 15,
				   0x1C0, 9, 0),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_F26M_SEL, "csw_f26m_sel",
				   csw_f26m_parents, 0x020, 0x024, 0x028, 16, 1, 23,
				   0x1C0, 10,
				   CLK_IS_CRITICAL | CLK_SET_RATE_PARENT),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_DRAMC_SEL, "dramc_sel",
				   csw_f26m_parents, 0x020, 0x024, 0x028, 24, 1,
				   31, 0x1C0, 11,
				   CLK_IS_CRITICAL | CLK_SET_RATE_PARENT),
	/* CLK_CFG_3 */
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_DRAMC_MD32_SEL, "dramc_md32_sel",
				   dramc_md32_parents, 0x030, 0x034, 0x038, 0, 2,
				   7, 0x1C0, 12,
				   CLK_IS_CRITICAL | CLK_SET_RATE_PARENT),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_SYSAXI_SEL, "sysaxi_sel",
				   sysaxi_parents, 0x030, 0x034, 0x038, 8, 1, 15,
				   0x1C0, 13,
				   CLK_IS_CRITICAL | CLK_SET_RATE_PARENT),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_SYSAPB_SEL, "sysapb_sel",
				   sysapb_parents, 0x030, 0x034, 0x038, 16, 1,
				   23, 0x1C0, 14,
				   CLK_IS_CRITICAL | CLK_SET_RATE_PARENT),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_ARM_DB_MAIN_SEL, "arm_db_main_sel",
			     arm_db_main_parents, 0x030, 0x034, 0x038, 24, 1, 31,
			     0x1C0, 15),
	/* CLK_CFG_4 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AP2CNN_HOST_SEL, "ap2cnn_host_sel",
			     ap2cnn_host_parents, 0x040, 0x044, 0x048, 0, 1, 7,
			     0x1C0, 16),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_NETSYS_SEL, "netsys_sel", netsys_parents,
			     0x040, 0x044, 0x048, 8, 1, 15, 0x1C0, 17),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_NETSYS_500M_SEL, "netsys_500m_sel",
			     netsys_500m_parents, 0x040, 0x044, 0x048, 16, 1, 23,
			     0x1C0, 18),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_NETSYS_MCU_SEL, "netsys_mcu_sel",
			     netsys_mcu_parents, 0x040, 0x044, 0x048, 24, 3, 31,
			     0x1C0, 19),
	/* CLK_CFG_5 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_NETSYS_2X_SEL, "netsys_2x_sel",
			     netsys_2x_parents, 0x050, 0x054, 0x058, 0, 2, 7,
			     0x1C0, 20),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SGM_325M_SEL, "sgm_325m_sel",
			     sgm_325m_parents, 0x050, 0x054, 0x058, 8, 1, 15,
			     0x1C0, 21),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_SGM_REG_SEL, "sgm_reg_sel", sgm_reg_parents,
				   0x050, 0x054, 0x058, 16, 1, 23, 0x1C0, 22,
				   CLK_IS_CRITICAL | CLK_SET_RATE_PARENT),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_EIP97B_SEL, "eip97b_sel", eip97b_parents,
			     0x050, 0x054, 0x058, 24, 3, 31, 0x1C0, 23),
	/* CLK_CFG_6 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_USB3_PHY_SEL, "usb3_phy_sel",
			     csw_f26m_parents, 0x060, 0x064, 0x068, 0, 1,
			     7, 0x1C0, 24),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUD_SEL, "aud_sel", aud_parents, 0x060,
			     0x064, 0x068, 8, 1, 15, 0x1C0, 25),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_A1SYS_SEL, "a1sys_sel", a1sys_parents,
			     0x060, 0x064, 0x068, 16, 1, 23, 0x1C0, 26),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUD_L_SEL, "aud_l_sel", aud_l_parents,
			     0x060, 0x064, 0x068, 24, 2, 31, 0x1C0, 27),
	/* CLK_CFG_7 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_A_TUNER_SEL, "a_tuner_sel",
			     a_tuner_parents, 0x070, 0x074, 0x078, 0, 2, 7,
			     0x1C0, 28),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_U2U3_SEL, "u2u3_sel", u2u3_parents, 0x070,
			     0x074, 0x078, 8, 1, 15, 0x1C0, 29),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_U2U3_SYS_SEL, "u2u3_sys_sel",
			     u2u3_sys_parents, 0x070, 0x074, 0x078, 16, 1, 23,
			     0x1C0, 30),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_U2U3_XHCI_SEL, "u2u3_xhci_sel",
			     u2u3_sys_parents, 0x070, 0x074, 0x078, 24, 1, 31,
			     0x1C4, 0),
	/* CLK_CFG_8 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_USB_FRMCNT_SEL, "usb_frmcnt_sel",
			     usb_frmcnt_parents, 0x080, 0x084, 0x088, 0, 1, 7,
			     0x1C4, 1),
};

static struct mtk_composite top_aud_divs[] = {
	DIV_GATE(CLK_TOP_AUD_I2S_M, "aud_i2s_m", "aud",
		0x0420, 0, 0x0420, 8, 8),
};

static const struct mtk_clk_desc topck_desc = {
	.factor_clks = top_divs,
	.num_factor_clks = ARRAY_SIZE(top_divs),
	.mux_clks = top_muxes,
	.num_mux_clks = ARRAY_SIZE(top_muxes),
	.composite_clks = top_aud_divs,
	.num_composite_clks = ARRAY_SIZE(top_aud_divs),
	.clk_lock = &mt7981_clk_lock,
};

static const struct of_device_id of_match_clk_mt7981_topckgen[] = {
	{ .compatible = "mediatek,mt7981-topckgen", .data = &topck_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt7981_topckgen);

static struct platform_driver clk_mt7981_topckgen_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt7981-topckgen",
		.of_match_table = of_match_clk_mt7981_topckgen,
	},
};
module_platform_driver(clk_mt7981_topckgen_drv);

MODULE_DESCRIPTION("MediaTek MT7981 top clock generators driver");
MODULE_LICENSE("GPL");
