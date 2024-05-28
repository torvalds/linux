// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 MediaTek Inc.
 * Author: Sam Shih <sam.shih@mediatek.com>
 * Author: Xiufeng Li <Xiufeng.Li@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include "clk-mtk.h"
#include "clk-gate.h"
#include "clk-mux.h"
#include <dt-bindings/clock/mediatek,mt7988-clk.h>

static DEFINE_SPINLOCK(mt7988_clk_lock);

static const struct mtk_fixed_clk top_fixed_clks[] = {
	FIXED_CLK(CLK_TOP_XTAL, "top_xtal", "clkxtal", 40000000),
};

static const struct mtk_fixed_factor top_divs[] = {
	FACTOR(CLK_TOP_XTAL_D2, "top_xtal_d2", "top_xtal", 1, 2),
	FACTOR(CLK_TOP_RTC_32K, "top_rtc_32k", "top_xtal", 1, 1250),
	FACTOR(CLK_TOP_RTC_32P7K, "top_rtc_32p7k", "top_xtal", 1, 1220),
	FACTOR(CLK_TOP_MPLL_D2, "mpll_d2", "mpll", 1, 2),
	FACTOR(CLK_TOP_MPLL_D3_D2, "mpll_d3_d2", "mpll", 1, 2),
	FACTOR(CLK_TOP_MPLL_D4, "mpll_d4", "mpll", 1, 4),
	FACTOR(CLK_TOP_MPLL_D8, "mpll_d8", "mpll", 1, 8),
	FACTOR(CLK_TOP_MPLL_D8_D2, "mpll_d8_d2", "mpll", 1, 16),
	FACTOR(CLK_TOP_MMPLL_D2, "mmpll_d2", "mmpll", 1, 2),
	FACTOR(CLK_TOP_MMPLL_D3_D5, "mmpll_d3_d5", "mmpll", 1, 15),
	FACTOR(CLK_TOP_MMPLL_D4, "mmpll_d4", "mmpll", 1, 4),
	FACTOR(CLK_TOP_MMPLL_D6_D2, "mmpll_d6_d2", "mmpll", 1, 12),
	FACTOR(CLK_TOP_MMPLL_D8, "mmpll_d8", "mmpll", 1, 8),
	FACTOR(CLK_TOP_APLL2_D4, "apll2_d4", "apll2", 1, 4),
	FACTOR(CLK_TOP_NET1PLL_D4, "net1pll_d4", "net1pll", 1, 4),
	FACTOR(CLK_TOP_NET1PLL_D5, "net1pll_d5", "net1pll", 1, 5),
	FACTOR(CLK_TOP_NET1PLL_D5_D2, "net1pll_d5_d2", "net1pll", 1, 10),
	FACTOR(CLK_TOP_NET1PLL_D5_D4, "net1pll_d5_d4", "net1pll", 1, 20),
	FACTOR(CLK_TOP_NET1PLL_D8, "net1pll_d8", "net1pll", 1, 8),
	FACTOR(CLK_TOP_NET1PLL_D8_D2, "net1pll_d8_d2", "net1pll", 1, 16),
	FACTOR(CLK_TOP_NET1PLL_D8_D4, "net1pll_d8_d4", "net1pll", 1, 32),
	FACTOR(CLK_TOP_NET1PLL_D8_D8, "net1pll_d8_d8", "net1pll", 1, 64),
	FACTOR(CLK_TOP_NET1PLL_D8_D16, "net1pll_d8_d16", "net1pll", 1, 128),
	FACTOR(CLK_TOP_NET2PLL_D2, "net2pll_d2", "net2pll", 1, 2),
	FACTOR(CLK_TOP_NET2PLL_D4, "net2pll_d4", "net2pll", 1, 4),
	FACTOR(CLK_TOP_NET2PLL_D4_D4, "net2pll_d4_d4", "net2pll", 1, 16),
	FACTOR(CLK_TOP_NET2PLL_D4_D8, "net2pll_d4_d8", "net2pll", 1, 32),
	FACTOR(CLK_TOP_NET2PLL_D6, "net2pll_d6", "net2pll", 1, 6),
	FACTOR(CLK_TOP_NET2PLL_D8, "net2pll_d8", "net2pll", 1, 8),
};

static const char *const netsys_parents[] = { "top_xtal", "net2pll_d2", "mmpll_d2" };
static const char *const netsys_500m_parents[] = { "top_xtal", "net1pll_d5", "net1pll_d5_d2" };
static const char *const netsys_2x_parents[] = { "top_xtal", "net2pll", "mmpll" };
static const char *const netsys_gsw_parents[] = { "top_xtal", "net1pll_d4", "net1pll_d5" };
static const char *const eth_gmii_parents[] = { "top_xtal", "net1pll_d5_d4" };
static const char *const netsys_mcu_parents[] = { "top_xtal",	"net2pll",    "mmpll",
						  "net1pll_d4", "net1pll_d5", "mpll" };
static const char *const eip197_parents[] = { "top_xtal", "netsyspll",	"net2pll",
					      "mmpll",	  "net1pll_d4", "net1pll_d5" };
static const char *const axi_infra_parents[] = { "top_xtal", "net1pll_d8_d2" };
static const char *const uart_parents[] = { "top_xtal", "mpll_d8", "mpll_d8_d2" };
static const char *const emmc_250m_parents[] = { "top_xtal", "net1pll_d5_d2", "mmpll_d4" };
static const char *const emmc_400m_parents[] = { "top_xtal", "msdcpll",	 "mmpll_d2",
						 "mpll_d2",  "mmpll_d4", "net1pll_d8_d2" };
static const char *const spi_parents[] = { "top_xtal",	    "mpll_d2",	    "mmpll_d4",
					   "net1pll_d8_d2", "net2pll_d6",   "net1pll_d5_d4",
					   "mpll_d4",	    "net1pll_d8_d4" };
static const char *const nfi1x_parents[] = { "top_xtal", "mmpll_d4", "net1pll_d8_d2", "net2pll_d6",
					     "mpll_d4",	 "mmpll_d8", "net1pll_d8_d4", "mpll_d8" };
static const char *const spinfi_parents[] = { "top_xtal_d2", "top_xtal", "net1pll_d5_d4",
					      "mpll_d4",     "mmpll_d8", "net1pll_d8_d4",
					      "mmpll_d6_d2", "mpll_d8" };
static const char *const pwm_parents[] = { "top_xtal", "net1pll_d8_d2", "net1pll_d5_d4",
					   "mpll_d4",  "mpll_d8_d2",	"top_rtc_32k" };
static const char *const i2c_parents[] = { "top_xtal", "net1pll_d5_d4", "mpll_d4",
					   "net1pll_d8_d4" };
static const char *const pcie_mbist_250m_parents[] = { "top_xtal", "net1pll_d5_d2" };
static const char *const pextp_tl_ck_parents[] = { "top_xtal", "net2pll_d6", "mmpll_d8",
						   "mpll_d8_d2", "top_rtc_32k" };
static const char *const usb_frmcnt_parents[] = { "top_xtal", "mmpll_d3_d5" };
static const char *const aud_parents[] = { "top_xtal", "apll2" };
static const char *const a1sys_parents[] = { "top_xtal", "apll2_d4" };
static const char *const aud_l_parents[] = { "top_xtal", "apll2", "mpll_d8_d2" };
static const char *const sspxtp_parents[] = { "top_xtal_d2", "mpll_d8_d2" };
static const char *const usxgmii_sbus_0_parents[] = { "top_xtal", "net1pll_d8_d4" };
static const char *const sgm_0_parents[] = { "top_xtal", "sgmpll" };
static const char *const sysapb_parents[] = { "top_xtal", "mpll_d3_d2" };
static const char *const eth_refck_50m_parents[] = { "top_xtal", "net2pll_d4_d4" };
static const char *const eth_sys_200m_parents[] = { "top_xtal", "net2pll_d4" };
static const char *const eth_xgmii_parents[] = { "top_xtal_d2", "net1pll_d8_d8", "net1pll_d8_d16" };
static const char *const bus_tops_parents[] = { "top_xtal", "net1pll_d5", "net2pll_d2" };
static const char *const npu_tops_parents[] = { "top_xtal", "net2pll" };
static const char *const dramc_md32_parents[] = { "top_xtal", "mpll_d2", "wedmcupll" };
static const char *const da_xtp_glb_p0_parents[] = { "top_xtal", "net2pll_d8" };
static const char *const mcusys_backup_625m_parents[] = { "top_xtal", "net1pll_d4" };
static const char *const macsec_parents[] = { "top_xtal", "sgmpll", "net1pll_d8" };
static const char *const netsys_tops_400m_parents[] = { "top_xtal", "net2pll_d2" };
static const char *const eth_mii_parents[] = { "top_xtal_d2", "net2pll_d4_d8" };

static const struct mtk_mux top_muxes[] = {
	/* CLK_CFG_0 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_NETSYS_SEL, "netsys_sel", netsys_parents, 0x000, 0x004, 0x008,
			     0, 2, 7, 0x1c0, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_NETSYS_500M_SEL, "netsys_500m_sel", netsys_500m_parents, 0x000,
			     0x004, 0x008, 8, 2, 15, 0x1C0, 1),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_NETSYS_2X_SEL, "netsys_2x_sel", netsys_2x_parents, 0x000,
			     0x004, 0x008, 16, 2, 23, 0x1C0, 2),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_NETSYS_GSW_SEL, "netsys_gsw_sel", netsys_gsw_parents, 0x000,
			     0x004, 0x008, 24, 2, 31, 0x1C0, 3),
	/* CLK_CFG_1 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_ETH_GMII_SEL, "eth_gmii_sel", eth_gmii_parents, 0x010, 0x014,
			     0x018, 0, 1, 7, 0x1C0, 4),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_NETSYS_MCU_SEL, "netsys_mcu_sel", netsys_mcu_parents, 0x010,
			     0x014, 0x018, 8, 3, 15, 0x1C0, 5),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_NETSYS_PAO_2X_SEL, "netsys_pao_2x_sel", netsys_mcu_parents,
			     0x010, 0x014, 0x018, 16, 3, 23, 0x1C0, 6),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_EIP197_SEL, "eip197_sel", eip197_parents, 0x010, 0x014, 0x018,
			     24, 3, 31, 0x1c0, 7),
	/* CLK_CFG_2 */
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_AXI_INFRA_SEL, "axi_infra_sel", axi_infra_parents, 0x020,
				   0x024, 0x028, 0, 1, 7, 0x1C0, 8, CLK_IS_CRITICAL),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_UART_SEL, "uart_sel", uart_parents, 0x020, 0x024, 0x028, 8, 2,
			     15, 0x1c0, 9),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_EMMC_250M_SEL, "emmc_250m_sel", emmc_250m_parents, 0x020,
			     0x024, 0x028, 16, 2, 23, 0x1C0, 10),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_EMMC_400M_SEL, "emmc_400m_sel", emmc_400m_parents, 0x020,
			     0x024, 0x028, 24, 3, 31, 0x1C0, 11),
	/* CLK_CFG_3 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SPI_SEL, "spi_sel", spi_parents, 0x030, 0x034, 0x038, 0, 3, 7,
			     0x1c0, 12),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SPIM_MST_SEL, "spim_mst_sel", spi_parents, 0x030, 0x034, 0x038,
			     8, 3, 15, 0x1c0, 13),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_NFI1X_SEL, "nfi1x_sel", nfi1x_parents, 0x030, 0x034, 0x038, 16,
			     3, 23, 0x1c0, 14),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SPINFI_SEL, "spinfi_sel", spinfi_parents, 0x030, 0x034, 0x038,
			     24, 3, 31, 0x1c0, 15),
	/* CLK_CFG_4 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_PWM_SEL, "pwm_sel", pwm_parents, 0x040, 0x044, 0x048, 0, 3, 7,
			     0x1c0, 16),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_I2C_SEL, "i2c_sel", i2c_parents, 0x040, 0x044, 0x048, 8, 2, 15,
			     0x1c0, 17),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_PCIE_MBIST_250M_SEL, "pcie_mbist_250m_sel",
			     pcie_mbist_250m_parents, 0x040, 0x044, 0x048, 16, 1, 23, 0x1C0, 18),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_PEXTP_TL_SEL, "pextp_tl_sel", pextp_tl_ck_parents, 0x040,
			     0x044, 0x048, 24, 3, 31, 0x1C0, 19),
	/* CLK_CFG_5 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_PEXTP_TL_P1_SEL, "pextp_tl_p1_sel", pextp_tl_ck_parents, 0x050,
			     0x054, 0x058, 0, 3, 7, 0x1C0, 20),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_PEXTP_TL_P2_SEL, "pextp_tl_p2_sel", pextp_tl_ck_parents, 0x050,
			     0x054, 0x058, 8, 3, 15, 0x1C0, 21),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_PEXTP_TL_P3_SEL, "pextp_tl_p3_sel", pextp_tl_ck_parents, 0x050,
			     0x054, 0x058, 16, 3, 23, 0x1C0, 22),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_USB_SYS_SEL, "usb_sys_sel", eth_gmii_parents, 0x050, 0x054,
			     0x058, 24, 1, 31, 0x1C0, 23),
	/* CLK_CFG_6 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_USB_SYS_P1_SEL, "usb_sys_p1_sel", eth_gmii_parents, 0x060,
			     0x064, 0x068, 0, 1, 7, 0x1C0, 24),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_USB_XHCI_SEL, "usb_xhci_sel", eth_gmii_parents, 0x060, 0x064,
			     0x068, 8, 1, 15, 0x1C0, 25),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_USB_XHCI_P1_SEL, "usb_xhci_p1_sel", eth_gmii_parents, 0x060,
			     0x064, 0x068, 16, 1, 23, 0x1C0, 26),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_USB_FRMCNT_SEL, "usb_frmcnt_sel", usb_frmcnt_parents, 0x060,
			     0x064, 0x068, 24, 1, 31, 0x1C0, 27),
	/* CLK_CFG_7 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_USB_FRMCNT_P1_SEL, "usb_frmcnt_p1_sel", usb_frmcnt_parents,
			     0x070, 0x074, 0x078, 0, 1, 7, 0x1C0, 28),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUD_SEL, "aud_sel", aud_parents, 0x070, 0x074, 0x078, 8, 1, 15,
			     0x1c0, 29),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_A1SYS_SEL, "a1sys_sel", a1sys_parents, 0x070, 0x074, 0x078, 16,
			     1, 23, 0x1c0, 30),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_AUD_L_SEL, "aud_l_sel", aud_l_parents, 0x070, 0x074, 0x078, 24,
			     2, 31, 0x1c4, 0),
	/* CLK_CFG_8 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_A_TUNER_SEL, "a_tuner_sel", a1sys_parents, 0x080, 0x084, 0x088,
			     0, 1, 7, 0x1c4, 1),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SSPXTP_SEL, "sspxtp_sel", sspxtp_parents, 0x080, 0x084, 0x088,
			     8, 1, 15, 0x1c4, 2),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_USB_PHY_SEL, "usb_phy_sel", sspxtp_parents, 0x080, 0x084,
			     0x088, 16, 1, 23, 0x1c4, 3),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_USXGMII_SBUS_0_SEL, "usxgmii_sbus_0_sel",
			     usxgmii_sbus_0_parents, 0x080, 0x084, 0x088, 24, 1, 31, 0x1C4, 4),
	/* CLK_CFG_9 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_USXGMII_SBUS_1_SEL, "usxgmii_sbus_1_sel",
			     usxgmii_sbus_0_parents, 0x090, 0x094, 0x098, 0, 1, 7, 0x1C4, 5),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SGM_0_SEL, "sgm_0_sel", sgm_0_parents, 0x090, 0x094, 0x098, 8,
			     1, 15, 0x1c4, 6),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_SGM_SBUS_0_SEL, "sgm_sbus_0_sel", usxgmii_sbus_0_parents,
				   0x090, 0x094, 0x098, 16, 1, 23, 0x1C4, 7, CLK_IS_CRITICAL),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_SGM_1_SEL, "sgm_1_sel", sgm_0_parents, 0x090, 0x094, 0x098, 24,
			     1, 31, 0x1c4, 8),
	/* CLK_CFG_10 */
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_SGM_SBUS_1_SEL, "sgm_sbus_1_sel", usxgmii_sbus_0_parents,
				   0x0a0, 0x0a4, 0x0a8, 0, 1, 7, 0x1C4, 9, CLK_IS_CRITICAL),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_XFI_PHY_0_XTAL_SEL, "xfi_phy_0_xtal_sel", sspxtp_parents,
			     0x0a0, 0x0a4, 0x0a8, 8, 1, 15, 0x1C4, 10),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_XFI_PHY_1_XTAL_SEL, "xfi_phy_1_xtal_sel", sspxtp_parents,
			     0x0a0, 0x0a4, 0x0a8, 16, 1, 23, 0x1C4, 11),
	/* CLK_CFG_11 */
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_SYSAXI_SEL, "sysaxi_sel", axi_infra_parents, 0x0a0,
				   0x0a4, 0x0a8, 24, 1, 31, 0x1C4, 12, CLK_IS_CRITICAL),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_SYSAPB_SEL, "sysapb_sel", sysapb_parents, 0x0b0, 0x0b4,
				   0x0b8, 0, 1, 7, 0x1c4, 13, CLK_IS_CRITICAL),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_ETH_REFCK_50M_SEL, "eth_refck_50m_sel", eth_refck_50m_parents,
			     0x0b0, 0x0b4, 0x0b8, 8, 1, 15, 0x1C4, 14),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_ETH_SYS_200M_SEL, "eth_sys_200m_sel", eth_sys_200m_parents,
			     0x0b0, 0x0b4, 0x0b8, 16, 1, 23, 0x1C4, 15),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_ETH_SYS_SEL, "eth_sys_sel", pcie_mbist_250m_parents, 0x0b0,
			     0x0b4, 0x0b8, 24, 1, 31, 0x1C4, 16),
	/* CLK_CFG_12 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_ETH_XGMII_SEL, "eth_xgmii_sel", eth_xgmii_parents, 0x0c0,
			     0x0c4, 0x0c8, 0, 2, 7, 0x1C4, 17),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_BUS_TOPS_SEL, "bus_tops_sel", bus_tops_parents, 0x0c0, 0x0c4,
			     0x0c8, 8, 2, 15, 0x1C4, 18),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_NPU_TOPS_SEL, "npu_tops_sel", npu_tops_parents, 0x0c0, 0x0c4,
			     0x0c8, 16, 1, 23, 0x1C4, 19),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_DRAMC_SEL, "dramc_sel", sspxtp_parents, 0x0c0, 0x0c4,
				   0x0c8, 24, 1, 31, 0x1C4, 20, CLK_IS_CRITICAL),
	/* CLK_CFG_13 */
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_DRAMC_MD32_SEL, "dramc_md32_sel", dramc_md32_parents,
				   0x0d0, 0x0d4, 0x0d8, 0, 2, 7, 0x1C4, 21, CLK_IS_CRITICAL),
	MUX_GATE_CLR_SET_UPD_FLAGS(CLK_TOP_INFRA_F26M_SEL, "csw_infra_f26m_sel", sspxtp_parents,
				   0x0d0, 0x0d4, 0x0d8, 8, 1, 15, 0x1C4, 22, CLK_IS_CRITICAL),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_PEXTP_P0_SEL, "pextp_p0_sel", sspxtp_parents, 0x0d0, 0x0d4,
			     0x0d8, 16, 1, 23, 0x1C4, 23),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_PEXTP_P1_SEL, "pextp_p1_sel", sspxtp_parents, 0x0d0, 0x0d4,
			     0x0d8, 24, 1, 31, 0x1C4, 24),
	/* CLK_CFG_14 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_PEXTP_P2_SEL, "pextp_p2_sel", sspxtp_parents, 0x0e0, 0x0e4,
			     0x0e8, 0, 1, 7, 0x1C4, 25),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_PEXTP_P3_SEL, "pextp_p3_sel", sspxtp_parents, 0x0e0, 0x0e4,
			     0x0e8, 8, 1, 15, 0x1C4, 26),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DA_XTP_GLB_P0_SEL, "da_xtp_glb_p0_sel", da_xtp_glb_p0_parents,
			     0x0e0, 0x0e4, 0x0e8, 16, 1, 23, 0x1C4, 27),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DA_XTP_GLB_P1_SEL, "da_xtp_glb_p1_sel", da_xtp_glb_p0_parents,
			     0x0e0, 0x0e4, 0x0e8, 24, 1, 31, 0x1C4, 28),
	/* CLK_CFG_15 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DA_XTP_GLB_P2_SEL, "da_xtp_glb_p2_sel", da_xtp_glb_p0_parents,
			     0x0f0, 0x0f4, 0x0f8, 0, 1, 7, 0x1C4, 29),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DA_XTP_GLB_P3_SEL, "da_xtp_glb_p3_sel", da_xtp_glb_p0_parents,
			     0x0f0, 0x0f4, 0x0f8, 8, 1, 15, 0x1C4, 30),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_CKM_SEL, "ckm_sel", sspxtp_parents, 0x0F0, 0x0f4, 0x0f8, 16, 1,
			     23, 0x1c8, 0),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_DA_SEL, "da_sel", sspxtp_parents, 0x0f0, 0x0f4, 0x0f8, 24, 1,
			     31, 0x1C8, 1),
	/* CLK_CFG_16 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_PEXTP_SEL, "pextp_sel", sspxtp_parents, 0x0100, 0x104, 0x108,
			     0, 1, 7, 0x1c8, 2),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_TOPS_P2_26M_SEL, "tops_p2_26m_sel", sspxtp_parents, 0x0100,
			     0x104, 0x108, 8, 1, 15, 0x1C8, 3),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MCUSYS_BACKUP_625M_SEL, "mcusys_backup_625m_sel",
			     mcusys_backup_625m_parents, 0x0100, 0x104, 0x108, 16, 1, 23, 0x1C8, 4),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_NETSYS_SYNC_250M_SEL, "netsys_sync_250m_sel",
			     pcie_mbist_250m_parents, 0x0100, 0x104, 0x108, 24, 1, 31, 0x1c8, 5),
	/* CLK_CFG_17 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_MACSEC_SEL, "macsec_sel", macsec_parents, 0x0110, 0x114, 0x118,
			     0, 2, 7, 0x1c8, 6),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_NETSYS_TOPS_400M_SEL, "netsys_tops_400m_sel",
			     netsys_tops_400m_parents, 0x0110, 0x114, 0x118, 8, 1, 15, 0x1c8, 7),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_NETSYS_PPEFB_250M_SEL, "netsys_ppefb_250m_sel",
			     pcie_mbist_250m_parents, 0x0110, 0x114, 0x118, 16, 1, 23, 0x1c8, 8),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_NETSYS_WARP_SEL, "netsys_warp_sel", netsys_parents, 0x0110,
			     0x114, 0x118, 24, 2, 31, 0x1C8, 9),
	/* CLK_CFG_18 */
	MUX_GATE_CLR_SET_UPD(CLK_TOP_ETH_MII_SEL, "eth_mii_sel", eth_mii_parents, 0x0120, 0x124,
			     0x128, 0, 1, 7, 0x1c8, 10),
	MUX_GATE_CLR_SET_UPD(CLK_TOP_NPU_SEL, "ck_npu_sel", netsys_2x_parents, 0x0120, 0x124, 0x128,
			     8, 2, 15, 0x1c8, 11),
};

static const struct mtk_composite top_aud_divs[] = {
	DIV_GATE(CLK_TOP_AUD_I2S_M, "aud_i2s_m", "aud_sel", 0x0420, 0, 0x0420, 8, 8),
};

static const struct mtk_clk_desc topck_desc = {
	.fixed_clks = top_fixed_clks,
	.num_fixed_clks = ARRAY_SIZE(top_fixed_clks),
	.factor_clks = top_divs,
	.num_factor_clks = ARRAY_SIZE(top_divs),
	.mux_clks = top_muxes,
	.num_mux_clks = ARRAY_SIZE(top_muxes),
	.composite_clks = top_aud_divs,
	.num_composite_clks = ARRAY_SIZE(top_aud_divs),
	.clk_lock = &mt7988_clk_lock,
};

static const char *const mcu_bus_div_parents[] = { "top_xtal", "ccipll2_b", "net1pll_d4" };

static const char *const mcu_arm_div_parents[] = { "top_xtal", "arm_b", "net1pll_d4" };

static struct mtk_composite mcu_muxes[] = {
	/* bus_pll_divider_cfg */
	MUX_GATE_FLAGS(CLK_MCU_BUS_DIV_SEL, "mcu_bus_div_sel", mcu_bus_div_parents, 0x7C0, 9, 2, -1,
		       CLK_IS_CRITICAL),
	/* mp2_pll_divider_cfg */
	MUX_GATE_FLAGS(CLK_MCU_ARM_DIV_SEL, "mcu_arm_div_sel", mcu_arm_div_parents, 0x7A8, 9, 2, -1,
		       CLK_IS_CRITICAL),
};

static const struct mtk_clk_desc mcusys_desc = {
	.composite_clks = mcu_muxes,
	.num_composite_clks = ARRAY_SIZE(mcu_muxes),
};

static const struct of_device_id of_match_clk_mt7988_topckgen[] = {
	{ .compatible = "mediatek,mt7988-topckgen", .data = &topck_desc },
	{ .compatible = "mediatek,mt7988-mcusys", .data = &mcusys_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt7988_topckgen);

static struct platform_driver clk_mt7988_topckgen_drv = {
	.probe = mtk_clk_simple_probe,
	.remove_new = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt7988-topckgen",
		.of_match_table = of_match_clk_mt7988_topckgen,
	},
};
module_platform_driver(clk_mt7988_topckgen_drv);

MODULE_DESCRIPTION("MediaTek MT7988 top clock generators driver");
MODULE_LICENSE("GPL");
