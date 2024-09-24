// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 * Author: Sam Shih <sam.shih@mediatek.com>
 * Author: Wenzhen Yu <wenzhen.yu@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include "clk-mtk.h"
#include "clk-gate.h"
#include "clk-mux.h"

#include <dt-bindings/clock/mt7986-clk.h>
#include <linux/clk.h>

static DEFINE_SPINLOCK(mt7986_clk_lock);

static const struct mtk_fixed_clk top_fixed_clks[] = {
	FIXED_CLK(CLK_TOP_XTAL, "top_xtal", "clkxtal", 40000000),
	FIXED_CLK(CLK_TOP_JTAG, "top_jtag", "clkxtal", 50000000),
};

static const struct mtk_fixed_factor top_divs[] = {
	/* XTAL */
	FACTOR(CLK_TOP_XTAL_D2, "top_xtal_d2", "top_xtal", 1, 2),
	FACTOR(CLK_TOP_RTC_32K, "top_rtc_32k", "top_xtal", 1, 1250),
	FACTOR(CLK_TOP_RTC_32P7K, "top_rtc_32p7k", "top_xtal", 1, 1220),
	/* MPLL */
	FACTOR(CLK_TOP_MPLL_D2, "top_mpll_d2", "mpll", 1, 2),
	FACTOR(CLK_TOP_MPLL_D4, "top_mpll_d4", "mpll", 1, 4),
	FACTOR(CLK_TOP_MPLL_D8, "top_mpll_d8", "mpll", 1, 8),
	FACTOR(CLK_TOP_MPLL_D8_D2, "top_mpll_d8_d2", "mpll", 1, 16),
	FACTOR(CLK_TOP_MPLL_D3_D2, "top_mpll_d3_d2", "mpll", 1, 6),
	/* MMPLL */
	FACTOR(CLK_TOP_MMPLL_D2, "top_mmpll_d2", "mmpll", 1, 2),
	FACTOR(CLK_TOP_MMPLL_D4, "top_mmpll_d4", "mmpll", 1, 4),
	FACTOR(CLK_TOP_MMPLL_D8, "top_mmpll_d8", "mmpll", 1, 8),
	FACTOR(CLK_TOP_MMPLL_D8_D2, "top_mmpll_d8_d2", "mmpll", 1, 16),
	FACTOR(CLK_TOP_MMPLL_D3_D8, "top_mmpll_d3_d8", "mmpll", 1, 24),
	FACTOR(CLK_TOP_MMPLL_U2PHY, "top_mmpll_u2phy", "mmpll", 1, 30),
	/* APLL2 */
	FACTOR(CLK_TOP_APLL2_D4, "top_apll2_d4", "apll2", 1, 4),
	/* NET1PLL */
	FACTOR(CLK_TOP_NET1PLL_D4, "top_net1pll_d4", "net1pll", 1, 4),
	FACTOR(CLK_TOP_NET1PLL_D5, "top_net1pll_d5", "net1pll", 1, 5),
	FACTOR(CLK_TOP_NET1PLL_D5_D2, "top_net1pll_d5_d2", "net1pll", 1, 10),
	FACTOR(CLK_TOP_NET1PLL_D5_D4, "top_net1pll_d5_d4", "net1pll", 1, 20),
	FACTOR(CLK_TOP_NET1PLL_D8_D2, "top_net1pll_d8_d2", "net1pll", 1, 16),
	FACTOR(CLK_TOP_NET1PLL_D8_D4, "top_net1pll_d8_d4", "net1pll", 1, 32),
	/* NET2PLL */
	FACTOR(CLK_TOP_NET2PLL_D4, "top_net2pll_d4", "net2pll", 1, 4),
	FACTOR(CLK_TOP_NET2PLL_D4_D2, "top_net2pll_d4_d2", "net2pll", 1, 8),
	FACTOR(CLK_TOP_NET2PLL_D3_D2, "top_net2pll_d3_d2", "net2pll", 1, 2),
	/* WEDMCUPLL */
	FACTOR(CLK_TOP_WEDMCUPLL_D5_D2, "top_wedmcupll_d5_d2", "wedmcupll", 1,
	       10),
};

static const char *const nfi1x_parents[] __initconst = { "top_xtal",
							 "top_mmpll_d8",
							 "top_net1pll_d8_d2",
							 "top_net2pll_d3_d2",
							 "top_mpll_d4",
							 "top_mmpll_d8_d2",
							 "top_wedmcupll_d5_d2",
							 "top_mpll_d8" };

static const char *const spinfi_parents[] __initconst = {
	"top_xtal_d2",     "top_xtal",	"top_net1pll_d5_d4",
	"top_mpll_d4",     "top_mmpll_d8_d2", "top_wedmcupll_d5_d2",
	"top_mmpll_d3_d8", "top_mpll_d8"
};

static const char *const spi_parents[] __initconst = {
	"top_xtal",	  "top_mpll_d2",	"top_mmpll_d8",
	"top_net1pll_d8_d2", "top_net2pll_d3_d2",  "top_net1pll_d5_d4",
	"top_mpll_d4",       "top_wedmcupll_d5_d2"
};

static const char *const uart_parents[] __initconst = { "top_xtal",
							"top_mpll_d8",
							"top_mpll_d8_d2" };

static const char *const pwm_parents[] __initconst = {
	"top_xtal", "top_net1pll_d8_d2", "top_net1pll_d5_d4", "top_mpll_d4"
};

static const char *const i2c_parents[] __initconst = {
	"top_xtal", "top_net1pll_d5_d4", "top_mpll_d4", "top_net1pll_d8_d4"
};

static const char *const pextp_tl_ck_parents[] __initconst = {
	"top_xtal", "top_net1pll_d5_d4", "top_net2pll_d4_d2", "top_rtc_32k"
};

static const char *const emmc_250m_parents[] __initconst = {
	"top_xtal", "top_net1pll_d5_d2"
};

static const char *const emmc_416m_parents[] __initconst = { "top_xtal",
							     "mpll" };

static const char *const f_26m_adc_parents[] __initconst = { "top_xtal",
							     "top_mpll_d8_d2" };

static const char *const dramc_md32_parents[] __initconst = { "top_xtal",
							      "top_mpll_d2" };

static const char *const sysaxi_parents[] __initconst = { "top_xtal",
							  "top_net1pll_d8_d2",
							  "top_net2pll_d4" };

static const char *const sysapb_parents[] __initconst = { "top_xtal",
							  "top_mpll_d3_d2",
							  "top_net2pll_d4_d2" };

static const char *const arm_db_main_parents[] __initconst = {
	"top_xtal", "top_net2pll_d3_d2"
};

static const char *const arm_db_jtsel_parents[] __initconst = { "top_jtag",
								"top_xtal" };

static const char *const netsys_parents[] __initconst = { "top_xtal",
							  "top_mmpll_d4" };

static const char *const netsys_500m_parents[] __initconst = {
	"top_xtal", "top_net1pll_d5"
};

static const char *const netsys_mcu_parents[] __initconst = {
	"top_xtal", "wedmcupll", "top_mmpll_d2", "top_net1pll_d4",
	"top_net1pll_d5"
};

static const char *const netsys_2x_parents[] __initconst = {
	"top_xtal", "net2pll", "wedmcupll", "top_mmpll_d2"
};

static const char *const sgm_325m_parents[] __initconst = { "top_xtal",
							    "sgmpll" };

static const char *const sgm_reg_parents[] __initconst = {
	"top_xtal", "top_net1pll_d8_d4"
};

static const char *const a1sys_parents[] __initconst = { "top_xtal",
							 "top_apll2_d4" };

static const char *const conn_mcusys_parents[] __initconst = { "top_xtal",
							       "top_mmpll_d2" };

static const char *const eip_b_parents[] __initconst = { "top_xtal",
							 "net2pll" };

static const char *const aud_l_parents[] __initconst = { "top_xtal", "apll2",
							 "top_mpll_d8_d2" };

static const char *const a_tuner_parents[] __initconst = { "top_xtal",
							   "top_apll2_d4",
							   "top_mpll_d8_d2" };

static const char *const u2u3_sys_parents[] __initconst = {
	"top_xtal", "top_net1pll_d5_d4"
};

static const char *const da_u2_refsel_parents[] __initconst = {
	"top_xtal", "top_mmpll_u2phy"
};

static const struct mtk_mux top_muxes[] = {
	/* CLK_CFG_0 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_NFI1X_SEL, "nfi1x_sel", nfi1x_parents,
			     0x000, 0x004, 0x008, 0, 3, 7, 0x1C0, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SPINFI_SEL, "spinfi_sel", spinfi_parents,
			     0x000, 0x004, 0x008, 8, 3, 15, 0x1C0, 1),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SPI_SEL, "spi_sel", spi_parents, 0x000,
			     0x004, 0x008, 16, 3, 23, 0x1C0, 2),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SPIM_MST_SEL, "spim_mst_sel", spi_parents,
			     0x000, 0x004, 0x008, 24, 3, 31, 0x1C0, 3),
	/* CLK_CFG_1 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_UART_SEL, "uart_sel", uart_parents, 0x010,
			     0x014, 0x018, 0, 2, 7, 0x1C0, 4),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_PWM_SEL, "pwm_sel", pwm_parents, 0x010,
			     0x014, 0x018, 8, 2, 15, 0x1C0, 5),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_I2C_SEL, "i2c_sel", i2c_parents, 0x010,
			     0x014, 0x018, 16, 2, 23, 0x1C0, 6),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_PEXTP_TL_SEL, "pextp_tl_ck_sel",
			     pextp_tl_ck_parents, 0x010, 0x014, 0x018, 24, 2,
			     31, 0x1C0, 7),
	/* CLK_CFG_2 */
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_EMMC_250M_SEL, "emmc_250m_sel",
				   emmc_250m_parents, 0x020, 0x024, 0x028, 0, 1, 7,
				   0x1C0, 8, 0),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_EMMC_416M_SEL, "emmc_416m_sel",
				   emmc_416m_parents, 0x020, 0x024, 0x028, 8, 1, 15,
				   0x1C0, 9, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_F_26M_ADC_SEL, "f_26m_adc_sel",
			     f_26m_adc_parents, 0x020, 0x024, 0x028, 16, 1, 23,
			     0x1C0, 10),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_DRAMC_SEL, "dramc_sel",
				   f_26m_adc_parents, 0x020, 0x024, 0x028,
				   24, 1, 31, 0x1C0, 11,
				   CLK_IS_CRITICAL | CLK_SET_RATE_PARENT),
	/* CLK_CFG_3 */
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_DRAMC_MD32_SEL, "dramc_md32_sel",
				   dramc_md32_parents, 0x030, 0x034, 0x038,
				   0, 1, 7, 0x1C0, 12,
				   CLK_IS_CRITICAL | CLK_SET_RATE_PARENT),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_SYSAXI_SEL, "sysaxi_sel",
				   sysaxi_parents, 0x030, 0x034, 0x038,
				   8, 2, 15, 0x1C0, 13,
				   CLK_IS_CRITICAL | CLK_SET_RATE_PARENT),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_SYSAPB_SEL, "sysapb_sel",
				   sysapb_parents, 0x030, 0x034, 0x038,
				   16, 2, 23, 0x1C0, 14,
				   CLK_IS_CRITICAL | CLK_SET_RATE_PARENT),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_ARM_DB_MAIN_SEL, "arm_db_main_sel",
			     arm_db_main_parents, 0x030, 0x034, 0x038, 24, 1,
			     31, 0x1C0, 15),
	/* CLK_CFG_4 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_ARM_DB_JTSEL, "arm_db_jtsel",
			     arm_db_jtsel_parents, 0x040, 0x044, 0x048, 0, 1, 7,
			     0x1C0, 16),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_NETSYS_SEL, "netsys_sel", netsys_parents,
			     0x040, 0x044, 0x048, 8, 1, 15, 0x1C0, 17),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_NETSYS_500M_SEL, "netsys_500m_sel",
			     netsys_500m_parents, 0x040, 0x044, 0x048, 16, 1,
			     23, 0x1C0, 18),
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
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_SGM_REG_SEL, "sgm_reg_sel",
				   sgm_reg_parents, 0x050, 0x054, 0x058,
				   16, 1, 23, 0x1C0, 22,
				   CLK_IS_CRITICAL | CLK_SET_RATE_PARENT),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_A1SYS_SEL, "a1sys_sel", a1sys_parents,
			     0x050, 0x054, 0x058, 24, 1, 31, 0x1C0, 23),
	/* CLK_CFG_6 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CONN_MCUSYS_SEL, "conn_mcusys_sel",
			     conn_mcusys_parents, 0x060, 0x064, 0x068, 0, 1, 7,
			     0x1C0, 24),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_EIP_B_SEL, "eip_b_sel", eip_b_parents,
			     0x060, 0x064, 0x068, 8, 1, 15, 0x1C0, 25),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_PCIE_PHY_SEL, "pcie_phy_sel",
			     f_26m_adc_parents, 0x060, 0x064, 0x068, 16, 1, 23,
			     0x1C0, 26),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_USB3_PHY_SEL, "usb3_phy_sel",
			     f_26m_adc_parents, 0x060, 0x064, 0x068, 24, 1, 31,
			     0x1C0, 27),
	/* CLK_CFG_7 */
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_F26M_SEL, "csw_f26m_sel",
				   f_26m_adc_parents, 0x070, 0x074, 0x078,
				   0, 1, 7, 0x1C0, 28,
				   CLK_IS_CRITICAL | CLK_SET_RATE_PARENT),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUD_L_SEL, "aud_l_sel", aud_l_parents,
			     0x070, 0x074, 0x078, 8, 2, 15, 0x1C0, 29),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_A_TUNER_SEL, "a_tuner_sel",
			     a_tuner_parents, 0x070, 0x074, 0x078, 16, 2, 23,
			     0x1C0, 30),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_U2U3_SEL, "u2u3_sel", f_26m_adc_parents,
			     0x070, 0x074, 0x078, 24, 1, 31, 0x1C4, 0),
	/* CLK_CFG_8 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_U2U3_SYS_SEL, "u2u3_sys_sel",
			     u2u3_sys_parents, 0x080, 0x084, 0x088, 0, 1, 7,
			     0x1C4, 1),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_U2U3_XHCI_SEL, "u2u3_xhci_sel",
			     u2u3_sys_parents, 0x080, 0x084, 0x088, 8, 1, 15,
			     0x1C4, 2),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DA_U2_REFSEL, "da_u2_refsel",
			     da_u2_refsel_parents, 0x080, 0x084, 0x088, 16, 1,
			     23, 0x1C4, 3),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DA_U2_CK_1P_SEL, "da_u2_ck_1p_sel",
			     da_u2_refsel_parents, 0x080, 0x084, 0x088, 24, 1,
			     31, 0x1C4, 4),
	/* CLK_CFG_9 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AP2CNN_HOST_SEL, "ap2cnn_host_sel",
			     sgm_reg_parents, 0x090, 0x094, 0x098, 0, 1, 7,
			     0x1C4, 5),
};

static const struct mtk_clk_desc topck_desc = {
	.fixed_clks = top_fixed_clks,
	.num_fixed_clks = ARRAY_SIZE(top_fixed_clks),
	.factor_clks = top_divs,
	.num_factor_clks = ARRAY_SIZE(top_divs),
	.mux_clks = top_muxes,
	.num_mux_clks = ARRAY_SIZE(top_muxes),
	.clk_lock = &mt7986_clk_lock,
};

static const struct of_device_id of_match_clk_mt7986_topckgen[] = {
	{ .compatible = "mediatek,mt7986-topckgen", .data = &topck_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt7986_topckgen);

static struct platform_driver clk_mt7986_topckgen_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt7986-topckgen",
		.of_match_table = of_match_clk_mt7986_topckgen,
	},
};
module_platform_driver(clk_mt7986_topckgen_drv);

MODULE_DESCRIPTION("MediaTek MT7986 top clock generators driver");
MODULE_LICENSE("GPL");
