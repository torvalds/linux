// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2001-2021, Huawei Tech. Co., Ltd.
 * Author: chenjun <chenjun14@huawei.com>
 *
 * Copyright (c) 2018, Linaro Ltd.
 * Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>
 */

#include <dt-bindings/clock/hi3670-clock.h>
#include <linux/clk-provider.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include "clk.h"

static const struct hisi_fixed_rate_clock hi3670_fixed_rate_clks[] = {
	{ HI3670_CLKIN_SYS, "clkin_sys", NULL, 0, 19200000, },
	{ HI3670_CLKIN_REF, "clkin_ref", NULL, 0, 32764, },
	{ HI3670_CLK_FLL_SRC, "clk_fll_src", NULL, 0, 134400000, },
	{ HI3670_CLK_PPLL0, "clk_ppll0", NULL, 0, 1660000000, },
	{ HI3670_CLK_PPLL1, "clk_ppll1", NULL, 0, 1866000000, },
	{ HI3670_CLK_PPLL2, "clk_ppll2", NULL, 0, 1920000000, },
	{ HI3670_CLK_PPLL3, "clk_ppll3", NULL, 0, 1200000000, },
	{ HI3670_CLK_PPLL4, "clk_ppll4", NULL, 0, 900000000, },
	{ HI3670_CLK_PPLL6, "clk_ppll6", NULL, 0, 393216000, },
	{ HI3670_CLK_PPLL7, "clk_ppll7", NULL, 0, 1008000000, },
	{ HI3670_CLK_PPLL_PCIE, "clk_ppll_pcie", NULL, 0, 100000000, },
	{ HI3670_CLK_PCIEPLL_REV, "clk_pciepll_rev", NULL, 0, 100000000, },
	{ HI3670_CLK_SCPLL, "clk_scpll", NULL, 0, 245760000, },
	{ HI3670_PCLK, "pclk", NULL, 0, 20000000, },
	{ HI3670_CLK_UART0_DBG, "clk_uart0_dbg", NULL, 0, 19200000, },
	{ HI3670_CLK_UART6, "clk_uart6", NULL, 0, 19200000, },
	{ HI3670_OSC32K, "osc32k", NULL, 0, 32764, },
	{ HI3670_OSC19M, "osc19m", NULL, 0, 19200000, },
	{ HI3670_CLK_480M, "clk_480m", NULL, 0, 480000000, },
	{ HI3670_CLK_INVALID, "clk_invalid", NULL, 0, 10000000, },
};

/* crgctrl */
static const struct hisi_fixed_factor_clock hi3670_crg_fixed_factor_clks[] = {
	{ HI3670_CLK_DIV_SYSBUS, "clk_div_sysbus", "clk_mux_sysbus",
	  1, 7, 0, },
	{ HI3670_CLK_FACTOR_MMC, "clk_factor_mmc", "clkin_sys",
	  1, 6, 0, },
	{ HI3670_CLK_SD_SYS, "clk_sd_sys", "clk_sd_sys_gt",
	  1, 6, 0, },
	{ HI3670_CLK_SDIO_SYS, "clk_sdio_sys", "clk_sdio_sys_gt",
	  1, 6, 0, },
	{ HI3670_CLK_DIV_A53HPM, "clk_div_a53hpm", "clk_a53hpm_andgt",
	  1, 4, 0, },
	{ HI3670_CLK_DIV_320M, "clk_div_320m", "clk_320m_pll_gt",
	  1, 5, 0, },
	{ HI3670_PCLK_GATE_UART0, "pclk_gate_uart0", "clk_mux_uartl",
	  1, 1, 0, },
	{ HI3670_CLK_FACTOR_UART0, "clk_factor_uart0", "clk_mux_uart0",
	  1, 1, 0, },
	{ HI3670_CLK_FACTOR_USB3PHY_PLL, "clk_factor_usb3phy_pll", "clk_ppll0",
	  1, 60, 0, },
	{ HI3670_CLK_GATE_ABB_USB, "clk_gate_abb_usb", "clk_gate_usb_tcxo_en",
	  1, 1, 0, },
	{ HI3670_CLK_GATE_UFSPHY_REF, "clk_gate_ufsphy_ref", "clkin_sys",
	  1, 1, 0, },
	{ HI3670_ICS_VOLT_HIGH, "ics_volt_high", "peri_volt_hold",
	  1, 1, 0, },
	{ HI3670_ICS_VOLT_MIDDLE, "ics_volt_middle", "peri_volt_middle",
	  1, 1, 0, },
	{ HI3670_VENC_VOLT_HOLD, "venc_volt_hold", "peri_volt_hold",
	  1, 1, 0, },
	{ HI3670_VDEC_VOLT_HOLD, "vdec_volt_hold", "peri_volt_hold",
	  1, 1, 0, },
	{ HI3670_EDC_VOLT_HOLD, "edc_volt_hold", "peri_volt_hold",
	  1, 1, 0, },
	{ HI3670_CLK_ISP_SNCLK_FAC, "clk_isp_snclk_fac", "clk_isp_snclk_angt",
	  1, 10, 0, },
	{ HI3670_CLK_FACTOR_RXDPHY, "clk_factor_rxdphy", "clk_andgt_rxdphy",
	  1, 6, 0, },
};

static const struct hisi_gate_clock hi3670_crgctrl_gate_sep_clks[] = {
	{ HI3670_PPLL1_EN_ACPU, "ppll1_en_acpu", "clk_ppll1",
	  CLK_SET_RATE_PARENT, 0x0, 0, 0, },
	{ HI3670_PPLL2_EN_ACPU, "ppll2_en_acpu", "clk_ppll2",
	  CLK_SET_RATE_PARENT, 0x0, 3, 0, },
	{ HI3670_PPLL3_EN_ACPU, "ppll3_en_acpu", "clk_ppll3",
	  CLK_SET_RATE_PARENT, 0x0, 27, 0, },
	{ HI3670_PPLL1_GT_CPU, "ppll1_gt_cpu", "clk_ppll1",
	  CLK_SET_RATE_PARENT, 0x460, 16, 0, },
	{ HI3670_PPLL2_GT_CPU, "ppll2_gt_cpu", "clk_ppll2",
	  CLK_SET_RATE_PARENT, 0x460, 18, 0, },
	{ HI3670_PPLL3_GT_CPU, "ppll3_gt_cpu", "clk_ppll3",
	  CLK_SET_RATE_PARENT, 0x460, 20, 0, },
	{ HI3670_CLK_GATE_PPLL2_MEDIA, "clk_gate_ppll2_media", "clk_ppll2",
	  CLK_SET_RATE_PARENT, 0x410, 27, 0, },
	{ HI3670_CLK_GATE_PPLL3_MEDIA, "clk_gate_ppll3_media", "clk_ppll3",
	  CLK_SET_RATE_PARENT, 0x410, 28, 0, },
	{ HI3670_CLK_GATE_PPLL4_MEDIA, "clk_gate_ppll4_media", "clk_ppll4",
	  CLK_SET_RATE_PARENT, 0x410, 26, 0, },
	{ HI3670_CLK_GATE_PPLL6_MEDIA, "clk_gate_ppll6_media", "clk_ppll6",
	  CLK_SET_RATE_PARENT, 0x410, 30, 0, },
	{ HI3670_CLK_GATE_PPLL7_MEDIA, "clk_gate_ppll7_media", "clk_ppll7",
	  CLK_SET_RATE_PARENT, 0x410, 29, 0, },
	{ HI3670_PCLK_GPIO0, "pclk_gpio0", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 0, 0, },
	{ HI3670_PCLK_GPIO1, "pclk_gpio1", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 1, 0, },
	{ HI3670_PCLK_GPIO2, "pclk_gpio2", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 2, 0, },
	{ HI3670_PCLK_GPIO3, "pclk_gpio3", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 3, 0, },
	{ HI3670_PCLK_GPIO4, "pclk_gpio4", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 4, 0, },
	{ HI3670_PCLK_GPIO5, "pclk_gpio5", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 5, 0, },
	{ HI3670_PCLK_GPIO6, "pclk_gpio6", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 6, 0, },
	{ HI3670_PCLK_GPIO7, "pclk_gpio7", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 7, 0, },
	{ HI3670_PCLK_GPIO8, "pclk_gpio8", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 8, 0, },
	{ HI3670_PCLK_GPIO9, "pclk_gpio9", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 9, 0, },
	{ HI3670_PCLK_GPIO10, "pclk_gpio10", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 10, 0, },
	{ HI3670_PCLK_GPIO11, "pclk_gpio11", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 11, 0, },
	{ HI3670_PCLK_GPIO12, "pclk_gpio12", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 12, 0, },
	{ HI3670_PCLK_GPIO13, "pclk_gpio13", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 13, 0, },
	{ HI3670_PCLK_GPIO14, "pclk_gpio14", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 14, 0, },
	{ HI3670_PCLK_GPIO15, "pclk_gpio15", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 15, 0, },
	{ HI3670_PCLK_GPIO16, "pclk_gpio16", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 16, 0, },
	{ HI3670_PCLK_GPIO17, "pclk_gpio17", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 17, 0, },
	{ HI3670_PCLK_GPIO20, "pclk_gpio20", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 20, 0, },
	{ HI3670_PCLK_GPIO21, "pclk_gpio21", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 21, 0, },
	{ HI3670_PCLK_GATE_DSI0, "pclk_gate_dsi0", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x50, 28, 0, },
	{ HI3670_PCLK_GATE_DSI1, "pclk_gate_dsi1", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x50, 29, 0, },
	{ HI3670_HCLK_GATE_USB3OTG, "hclk_gate_usb3otg", "clk_div_sysbus",
	  CLK_SET_RATE_PARENT, 0x0, 25, 0, },
	{ HI3670_ACLK_GATE_USB3DVFS, "aclk_gate_usb3dvfs", "autodiv_emmc0bus",
	  CLK_SET_RATE_PARENT, 0x40, 1, 0, },
	{ HI3670_HCLK_GATE_SDIO, "hclk_gate_sdio", "clk_div_sysbus",
	  CLK_SET_RATE_PARENT, 0x0, 21, 0, },
	{ HI3670_PCLK_GATE_PCIE_SYS, "pclk_gate_pcie_sys", "clk_div_mmc1bus",
	  CLK_SET_RATE_PARENT, 0x420, 7, 0, },
	{ HI3670_PCLK_GATE_PCIE_PHY, "pclk_gate_pcie_phy", "pclk_gate_mmc1_pcie",
	  CLK_SET_RATE_PARENT, 0x420, 9, 0, },
	{ HI3670_PCLK_GATE_MMC1_PCIE, "pclk_gate_mmc1_pcie", "pclk_div_mmc1_pcie",
	  CLK_SET_RATE_PARENT, 0x30, 12, 0, },
	{ HI3670_PCLK_GATE_MMC0_IOC, "pclk_gate_mmc0_ioc", "clk_div_mmc0bus",
	  CLK_SET_RATE_PARENT, 0x40, 13, 0, },
	{ HI3670_PCLK_GATE_MMC1_IOC, "pclk_gate_mmc1_ioc", "clk_div_mmc1bus",
	  CLK_SET_RATE_PARENT, 0x420, 21, 0, },
	{ HI3670_CLK_GATE_DMAC, "clk_gate_dmac", "clk_div_sysbus",
	  CLK_SET_RATE_PARENT, 0x30, 1, 0, },
	{ HI3670_CLK_GATE_VCODECBUS2DDR, "clk_gate_vcodecbus2ddr", "clk_div_vcodecbus",
	  CLK_SET_RATE_PARENT, 0x0, 5, 0, },
	{ HI3670_CLK_CCI400_BYPASS, "clk_cci400_bypass", "clk_ddrc_freq",
	  CLK_SET_RATE_PARENT, 0x22C, 28, 0, },
	{ HI3670_CLK_GATE_CCI400, "clk_gate_cci400", "clk_ddrc_freq",
	  CLK_SET_RATE_PARENT, 0x50, 14, 0, },
	{ HI3670_CLK_GATE_SD, "clk_gate_sd", "clk_mux_sd_sys",
	  CLK_SET_RATE_PARENT, 0x40, 17, 0, },
	{ HI3670_HCLK_GATE_SD, "hclk_gate_sd", "clk_div_sysbus",
	  CLK_SET_RATE_PARENT, 0x0, 30, 0, },
	{ HI3670_CLK_GATE_SDIO, "clk_gate_sdio", "clk_mux_sdio_sys",
	  CLK_SET_RATE_PARENT, 0x40, 19, 0, },
	{ HI3670_CLK_GATE_A57HPM, "clk_gate_a57hpm", "clk_div_a53hpm",
	  CLK_SET_RATE_PARENT, 0x050, 9, 0, },
	{ HI3670_CLK_GATE_A53HPM, "clk_gate_a53hpm", "clk_div_a53hpm",
	  CLK_SET_RATE_PARENT, 0x050, 13, 0, },
	{ HI3670_CLK_GATE_PA_A53, "clk_gate_pa_a53", "clk_div_a53hpm",
	  CLK_SET_RATE_PARENT, 0x480, 10, 0, },
	{ HI3670_CLK_GATE_PA_A57, "clk_gate_pa_a57", "clk_div_a53hpm",
	  CLK_SET_RATE_PARENT, 0x480, 9, 0, },
	{ HI3670_CLK_GATE_PA_G3D, "clk_gate_pa_g3d", "clk_div_a53hpm",
	  CLK_SET_RATE_PARENT, 0x480, 15, 0, },
	{ HI3670_CLK_GATE_GPUHPM, "clk_gate_gpuhpm", "clk_div_a53hpm",
	  CLK_SET_RATE_PARENT, 0x050, 15, 0, },
	{ HI3670_CLK_GATE_PERIHPM, "clk_gate_perihpm", "clk_div_a53hpm",
	  CLK_SET_RATE_PARENT, 0x050, 12, 0, },
	{ HI3670_CLK_GATE_AOHPM, "clk_gate_aohpm", "clk_div_a53hpm",
	  CLK_SET_RATE_PARENT, 0x050, 11, 0, },
	{ HI3670_CLK_GATE_UART1, "clk_gate_uart1", "clk_mux_uarth",
	  CLK_SET_RATE_PARENT, 0x20, 11, 0, },
	{ HI3670_CLK_GATE_UART4, "clk_gate_uart4", "clk_mux_uarth",
	  CLK_SET_RATE_PARENT, 0x20, 14, 0, },
	{ HI3670_PCLK_GATE_UART1, "pclk_gate_uart1", "clk_mux_uarth",
	  CLK_SET_RATE_PARENT, 0x20, 11, 0, },
	{ HI3670_PCLK_GATE_UART4, "pclk_gate_uart4", "clk_mux_uarth",
	  CLK_SET_RATE_PARENT, 0x20, 14, 0, },
	{ HI3670_CLK_GATE_UART2, "clk_gate_uart2", "clk_mux_uartl",
	  CLK_SET_RATE_PARENT, 0x20, 12, 0, },
	{ HI3670_CLK_GATE_UART5, "clk_gate_uart5", "clk_mux_uartl",
	  CLK_SET_RATE_PARENT, 0x20, 15, 0, },
	{ HI3670_PCLK_GATE_UART2, "pclk_gate_uart2", "clk_mux_uartl",
	  CLK_SET_RATE_PARENT, 0x20, 12, 0, },
	{ HI3670_PCLK_GATE_UART5, "pclk_gate_uart5", "clk_mux_uartl",
	  CLK_SET_RATE_PARENT, 0x20, 15, 0, },
	{ HI3670_CLK_GATE_UART0, "clk_gate_uart0", "clk_mux_uart0",
	  CLK_SET_RATE_PARENT, 0x20, 10, 0, },
	{ HI3670_CLK_GATE_I2C3, "clk_gate_i2c3", "clk_mux_i2c",
	  CLK_SET_RATE_PARENT, 0x20, 7, 0, },
	{ HI3670_CLK_GATE_I2C4, "clk_gate_i2c4", "clk_mux_i2c",
	  CLK_SET_RATE_PARENT, 0x20, 27, 0, },
	{ HI3670_CLK_GATE_I2C7, "clk_gate_i2c7", "clk_mux_i2c",
	  CLK_SET_RATE_PARENT, 0x10, 31, 0, },
	{ HI3670_PCLK_GATE_I2C3, "pclk_gate_i2c3", "clk_mux_i2c",
	  CLK_SET_RATE_PARENT, 0x20, 7, 0, },
	{ HI3670_PCLK_GATE_I2C4, "pclk_gate_i2c4", "clk_mux_i2c",
	  CLK_SET_RATE_PARENT, 0x20, 27, 0, },
	{ HI3670_PCLK_GATE_I2C7, "pclk_gate_i2c7", "clk_mux_i2c",
	  CLK_SET_RATE_PARENT, 0x10, 31, 0, },
	{ HI3670_CLK_GATE_SPI1, "clk_gate_spi1", "clk_mux_spi",
	  CLK_SET_RATE_PARENT, 0x20, 9, 0, },
	{ HI3670_CLK_GATE_SPI4, "clk_gate_spi4", "clk_mux_spi",
	  CLK_SET_RATE_PARENT, 0x40, 4, 0, },
	{ HI3670_PCLK_GATE_SPI1, "pclk_gate_spi1", "clk_mux_spi",
	  CLK_SET_RATE_PARENT, 0x20, 9, 0, },
	{ HI3670_PCLK_GATE_SPI4, "pclk_gate_spi4", "clk_mux_spi",
	  CLK_SET_RATE_PARENT, 0x40, 4, 0, },
	{ HI3670_CLK_GATE_USB3OTG_REF, "clk_gate_usb3otg_ref", "clkin_sys",
	  CLK_SET_RATE_PARENT, 0x40, 0, 0, },
	{ HI3670_CLK_GATE_USB2PHY_REF, "clk_gate_usb2phy_ref", "clkin_sys",
	  CLK_SET_RATE_PARENT, 0x410, 19, 0, },
	{ HI3670_CLK_GATE_PCIEAUX, "clk_gate_pcieaux", "clkin_sys",
	  CLK_SET_RATE_PARENT, 0x420, 8, 0, },
	{ HI3670_ACLK_GATE_PCIE, "aclk_gate_pcie", "clk_gate_mmc1_pcieaxi",
	  CLK_SET_RATE_PARENT, 0x420, 5, 0, },
	{ HI3670_CLK_GATE_MMC1_PCIEAXI, "clk_gate_mmc1_pcieaxi", "clk_div_pcieaxi",
	  CLK_SET_RATE_PARENT, 0x050, 4, 0, },
	{ HI3670_CLK_GATE_PCIEPHY_REF, "clk_gate_pciephy_ref", "clk_ppll_pcie",
	  CLK_SET_RATE_PARENT, 0x470, 14, 0, },
	{ HI3670_CLK_GATE_PCIE_DEBOUNCE, "clk_gate_pcie_debounce", "clk_ppll_pcie",
	  CLK_SET_RATE_PARENT, 0x470, 12, 0, },
	{ HI3670_CLK_GATE_PCIEIO, "clk_gate_pcieio", "clk_ppll_pcie",
	  CLK_SET_RATE_PARENT, 0x470, 13, 0, },
	{ HI3670_CLK_GATE_PCIE_HP, "clk_gate_pcie_hp", "clk_ppll_pcie",
	  CLK_SET_RATE_PARENT, 0x470, 15, 0, },
	{ HI3670_CLK_GATE_AO_ASP, "clk_gate_ao_asp", "clk_div_ao_asp",
	  CLK_SET_RATE_PARENT, 0x0, 26, 0, },
	{ HI3670_PCLK_GATE_PCTRL, "pclk_gate_pctrl", "clk_div_ptp",
	  CLK_SET_RATE_PARENT, 0x20, 31, 0, },
	{ HI3670_CLK_CSI_TRANS_GT, "clk_csi_trans_gt", "clk_div_csi_trans",
	  CLK_SET_RATE_PARENT, 0x30, 24, 0, },
	{ HI3670_CLK_DSI_TRANS_GT, "clk_dsi_trans_gt", "clk_div_dsi_trans",
	  CLK_SET_RATE_PARENT, 0x30, 25, 0, },
	{ HI3670_CLK_GATE_PWM, "clk_gate_pwm", "clk_div_ptp",
	  CLK_SET_RATE_PARENT, 0x20, 0, 0, },
	{ HI3670_ABB_AUDIO_EN0, "abb_audio_en0", "clk_gate_abb_192",
	  CLK_SET_RATE_PARENT, 0x30, 8, 0, },
	{ HI3670_ABB_AUDIO_EN1, "abb_audio_en1", "clk_gate_abb_192",
	  CLK_SET_RATE_PARENT, 0x30, 9, 0, },
	{ HI3670_ABB_AUDIO_GT_EN0, "abb_audio_gt_en0", "abb_audio_en0",
	  CLK_SET_RATE_PARENT, 0x30, 19, 0, },
	{ HI3670_ABB_AUDIO_GT_EN1, "abb_audio_gt_en1", "abb_audio_en1",
	  CLK_SET_RATE_PARENT, 0x40, 20, 0, },
	{ HI3670_CLK_GATE_DP_AUDIO_PLL_AO, "clk_gate_dp_audio_pll_ao", "clkdiv_dp_audio_pll_ao",
	  CLK_SET_RATE_PARENT, 0x00, 13, 0, },
	{ HI3670_PERI_VOLT_HOLD, "peri_volt_hold", "clkin_sys",
	  CLK_SET_RATE_PARENT, 0, 1, 0, },
	{ HI3670_PERI_VOLT_MIDDLE, "peri_volt_middle", "clkin_sys",
	  CLK_SET_RATE_PARENT, 0, 1, 0, },
	{ HI3670_CLK_GATE_ISP_SNCLK0, "clk_gate_isp_snclk0", "clk_isp_snclk_mux0",
	  CLK_SET_RATE_PARENT, 0x50, 16, 0, },
	{ HI3670_CLK_GATE_ISP_SNCLK1, "clk_gate_isp_snclk1", "clk_isp_snclk_mux1",
	  CLK_SET_RATE_PARENT, 0x50, 17, 0, },
	{ HI3670_CLK_GATE_ISP_SNCLK2, "clk_gate_isp_snclk2", "clk_isp_snclk_mux2",
	  CLK_SET_RATE_PARENT, 0x50, 18, 0, },
	{ HI3670_CLK_GATE_RXDPHY0_CFG, "clk_gate_rxdphy0_cfg", "clk_mux_rxdphy_cfg",
	  CLK_SET_RATE_PARENT, 0x030, 20, 0, },
	{ HI3670_CLK_GATE_RXDPHY1_CFG, "clk_gate_rxdphy1_cfg", "clk_mux_rxdphy_cfg",
	  CLK_SET_RATE_PARENT, 0x030, 21, 0, },
	{ HI3670_CLK_GATE_RXDPHY2_CFG, "clk_gate_rxdphy2_cfg", "clk_mux_rxdphy_cfg",
	  CLK_SET_RATE_PARENT, 0x030, 22, 0, },
	{ HI3670_CLK_GATE_TXDPHY0_CFG, "clk_gate_txdphy0_cfg", "clkin_sys",
	  CLK_SET_RATE_PARENT, 0x030, 28, 0, },
	{ HI3670_CLK_GATE_TXDPHY0_REF, "clk_gate_txdphy0_ref", "clkin_sys",
	  CLK_SET_RATE_PARENT, 0x030, 29, 0, },
	{ HI3670_CLK_GATE_TXDPHY1_CFG, "clk_gate_txdphy1_cfg", "clkin_sys",
	  CLK_SET_RATE_PARENT, 0x030, 30, 0, },
	{ HI3670_CLK_GATE_TXDPHY1_REF, "clk_gate_txdphy1_ref", "clkin_sys",
	  CLK_SET_RATE_PARENT, 0x030, 31, 0, },
	{ HI3670_CLK_GATE_MEDIA_TCXO, "clk_gate_media_tcxo", "clkin_sys",
	  CLK_SET_RATE_PARENT, 0x40, 6, 0, },
};

static const struct hisi_gate_clock hi3670_crgctrl_gate_clks[] = {
	{ HI3670_AUTODIV_SYSBUS, "autodiv_sysbus", "clk_div_sysbus",
	  CLK_SET_RATE_PARENT, 0x404, 5, CLK_GATE_HIWORD_MASK, },
	{ HI3670_AUTODIV_EMMC0BUS, "autodiv_emmc0bus", "autodiv_sysbus",
	  CLK_SET_RATE_PARENT, 0x404, 1, CLK_GATE_HIWORD_MASK, },
	{ HI3670_PCLK_ANDGT_MMC1_PCIE, "pclk_andgt_mmc1_pcie", "clk_div_320m",
	  CLK_SET_RATE_PARENT, 0xf8, 13, CLK_GATE_HIWORD_MASK, },
	{ HI3670_CLK_GATE_VCODECBUS_GT, "clk_gate_vcodecbus_gt", "clk_mux_vcodecbus",
	  CLK_SET_RATE_PARENT, 0x0F0, 8, CLK_GATE_HIWORD_MASK, },
	{ HI3670_CLK_ANDGT_SD, "clk_andgt_sd", "clk_mux_sd_pll",
	  CLK_SET_RATE_PARENT, 0xF4, 3, CLK_GATE_HIWORD_MASK, },
	{ HI3670_CLK_SD_SYS_GT, "clk_sd_sys_gt", "clkin_sys",
	  CLK_SET_RATE_PARENT, 0xF4, 5, CLK_GATE_HIWORD_MASK, },
	{ HI3670_CLK_ANDGT_SDIO, "clk_andgt_sdio", "clk_mux_sdio_pll",
	  CLK_SET_RATE_PARENT, 0xF4, 8, CLK_GATE_HIWORD_MASK, },
	{ HI3670_CLK_SDIO_SYS_GT, "clk_sdio_sys_gt", "clkin_sys",
	  CLK_SET_RATE_PARENT, 0xF4, 6, CLK_GATE_HIWORD_MASK, },
	{ HI3670_CLK_A53HPM_ANDGT, "clk_a53hpm_andgt", "clk_mux_a53hpm",
	  CLK_SET_RATE_PARENT, 0x0F4, 7, CLK_GATE_HIWORD_MASK, },
	{ HI3670_CLK_320M_PLL_GT, "clk_320m_pll_gt", "clk_mux_320m",
	  CLK_SET_RATE_PARENT, 0xF8, 10, CLK_GATE_HIWORD_MASK, },
	{ HI3670_CLK_ANDGT_UARTH, "clk_andgt_uarth", "clk_div_320m",
	  CLK_SET_RATE_PARENT, 0xF4, 11, CLK_GATE_HIWORD_MASK, },
	{ HI3670_CLK_ANDGT_UARTL, "clk_andgt_uartl", "clk_div_320m",
	  CLK_SET_RATE_PARENT, 0xF4, 10, CLK_GATE_HIWORD_MASK, },
	{ HI3670_CLK_ANDGT_UART0, "clk_andgt_uart0", "clk_div_320m",
	  CLK_SET_RATE_PARENT, 0xF4, 9, CLK_GATE_HIWORD_MASK, },
	{ HI3670_CLK_ANDGT_SPI, "clk_andgt_spi", "clk_div_320m",
	  CLK_SET_RATE_PARENT, 0xF4, 13, CLK_GATE_HIWORD_MASK, },
	{ HI3670_CLK_ANDGT_PCIEAXI, "clk_andgt_pcieaxi", "clk_mux_pcieaxi",
	  CLK_SET_RATE_PARENT, 0xfc, 15, CLK_GATE_HIWORD_MASK, },
	{ HI3670_CLK_DIV_AO_ASP_GT, "clk_div_ao_asp_gt", "clk_mux_ao_asp",
	  CLK_SET_RATE_PARENT, 0xF4, 4, CLK_GATE_HIWORD_MASK, },
	{ HI3670_CLK_GATE_CSI_TRANS, "clk_gate_csi_trans", "clk_ppll2",
	  CLK_SET_RATE_PARENT, 0xF4, 14, CLK_GATE_HIWORD_MASK, },
	{ HI3670_CLK_GATE_DSI_TRANS, "clk_gate_dsi_trans", "clk_ppll2",
	  CLK_SET_RATE_PARENT, 0xF4, 1, CLK_GATE_HIWORD_MASK, },
	{ HI3670_CLK_ANDGT_PTP, "clk_andgt_ptp", "clk_div_320m",
	  CLK_SET_RATE_PARENT, 0xF8, 5, CLK_GATE_HIWORD_MASK, },
	{ HI3670_CLK_ANDGT_OUT0, "clk_andgt_out0", "clk_ppll0",
	  CLK_SET_RATE_PARENT, 0xF0, 10, CLK_GATE_HIWORD_MASK, },
	{ HI3670_CLK_ANDGT_OUT1, "clk_andgt_out1", "clk_ppll0",
	  CLK_SET_RATE_PARENT, 0xF0, 11, CLK_GATE_HIWORD_MASK, },
	{ HI3670_CLKGT_DP_AUDIO_PLL_AO, "clkgt_dp_audio_pll_ao", "clk_ppll6",
	  CLK_SET_RATE_PARENT, 0xF8, 15, CLK_GATE_HIWORD_MASK, },
	{ HI3670_CLK_ANDGT_VDEC, "clk_andgt_vdec", "clk_mux_vdec",
	  CLK_SET_RATE_PARENT, 0xF0, 13, CLK_GATE_HIWORD_MASK, },
	{ HI3670_CLK_ANDGT_VENC, "clk_andgt_venc", "clk_mux_venc",
	  CLK_SET_RATE_PARENT, 0xF0, 9, CLK_GATE_HIWORD_MASK, },
	{ HI3670_CLK_ISP_SNCLK_ANGT, "clk_isp_snclk_angt", "clk_div_a53hpm",
	  CLK_SET_RATE_PARENT, 0x108, 2, CLK_GATE_HIWORD_MASK, },
	{ HI3670_CLK_ANDGT_RXDPHY, "clk_andgt_rxdphy", "clk_div_a53hpm",
	  CLK_SET_RATE_PARENT, 0x0F0, 12, CLK_GATE_HIWORD_MASK, },
	{ HI3670_CLK_ANDGT_ICS, "clk_andgt_ics", "clk_mux_ics",
	  CLK_SET_RATE_PARENT, 0xf0, 14, CLK_GATE_HIWORD_MASK, },
	{ HI3670_AUTODIV_DMABUS, "autodiv_dmabus", "autodiv_sysbus",
	  CLK_SET_RATE_PARENT, 0x404, 3, CLK_GATE_HIWORD_MASK, },
};

static const char *const
clk_mux_sysbus_p[] = { "clk_ppll1", "clk_ppll0", };
static const char *const
clk_mux_vcodecbus_p[] = { "clk_invalid", "clk_ppll4", "clk_ppll0",
			  "clk_invalid", "clk_ppll2", "clk_invalid",
			  "clk_invalid", "clk_invalid", "clk_ppll3",
			  "clk_invalid", "clk_invalid", "clk_invalid",
			  "clk_invalid", "clk_invalid", "clk_invalid",
			  "clk_invalid", };
static const char *const
clk_mux_sd_sys_p[] = { "clk_sd_sys", "clk_div_sd", };
static const char *const
clk_mux_sd_pll_p[] = { "clk_ppll0", "clk_ppll3", "clk_ppll2", "clk_ppll2", };
static const char *const
clk_mux_sdio_sys_p[] = { "clk_sdio_sys", "clk_div_sdio", };
static const char *const
clk_mux_sdio_pll_p[] = { "clk_ppll0", "clk_ppll3", "clk_ppll2", "clk_ppll2", };
static const char *const
clk_mux_a53hpm_p[] = { "clk_ppll0", "clk_ppll2", };
static const char *const
clk_mux_320m_p[] = { "clk_ppll2", "clk_ppll0", };
static const char *const
clk_mux_uarth_p[] = { "clkin_sys", "clk_div_uarth", };
static const char *const
clk_mux_uartl_p[] = { "clkin_sys", "clk_div_uartl", };
static const char *const
clk_mux_uart0_p[] = { "clkin_sys", "clk_div_uart0", };
static const char *const
clk_mux_i2c_p[] = { "clkin_sys", "clk_div_i2c", };
static const char *const
clk_mux_spi_p[] = { "clkin_sys", "clk_div_spi", };
static const char *const
clk_mux_pcieaxi_p[] = { "clkin_sys", "clk_ppll0", };
static const char *const
clk_mux_ao_asp_p[] = { "clk_ppll2", "clk_ppll3", };
static const char *const
clk_mux_vdec_p[] = { "clk_invalid", "clk_ppll4", "clk_ppll0", "clk_invalid",
		     "clk_invalid", "clk_invalid", "clk_invalid", "clk_invalid",
		     "clk_invalid", "clk_invalid", "clk_invalid", "clk_invalid",
		     "clk_invalid", "clk_invalid", "clk_invalid",
		     "clk_invalid", };
static const char *const
clk_mux_venc_p[] = { "clk_invalid", "clk_ppll4", "clk_ppll0", "clk_invalid",
		     "clk_invalid", "clk_invalid", "clk_invalid", "clk_invalid",
		     "clk_invalid", "clk_invalid", "clk_invalid", "clk_invalid",
		     "clk_invalid", "clk_invalid", "clk_invalid",
		     "clk_invalid", };
static const char *const
clk_isp_snclk_mux0_p[] = { "clkin_sys", "clk_isp_snclk_div0", };
static const char *const
clk_isp_snclk_mux1_p[] = { "clkin_sys", "clk_isp_snclk_div1", };
static const char *const
clk_isp_snclk_mux2_p[] = { "clkin_sys", "clk_isp_snclk_div2", };
static const char *const
clk_mux_rxdphy_cfg_p[] = { "clk_factor_rxdphy", "clkin_sys", };
static const char *const
clk_mux_ics_p[] = { "clk_invalid", "clk_ppll4", "clk_ppll0", "clk_invalid",
		    "clk_ppll2", "clk_invalid", "clk_invalid", "clk_invalid",
		    "clk_ppll3", "clk_invalid", "clk_invalid", "clk_invalid",
		    "clk_invalid", "clk_invalid", "clk_invalid",
		    "clk_invalid", };

static const struct hisi_mux_clock hi3670_crgctrl_mux_clks[] = {
	{ HI3670_CLK_MUX_SYSBUS, "clk_mux_sysbus", clk_mux_sysbus_p,
	  ARRAY_SIZE(clk_mux_sysbus_p), CLK_SET_RATE_PARENT,
	  0xAC, 0, 1, CLK_MUX_HIWORD_MASK, },
	{ HI3670_CLK_MUX_VCODECBUS, "clk_mux_vcodecbus", clk_mux_vcodecbus_p,
	  ARRAY_SIZE(clk_mux_vcodecbus_p), CLK_SET_RATE_PARENT,
	  0x0C8, 0, 4, CLK_MUX_HIWORD_MASK, },
	{ HI3670_CLK_MUX_SD_SYS, "clk_mux_sd_sys", clk_mux_sd_sys_p,
	  ARRAY_SIZE(clk_mux_sd_sys_p), CLK_SET_RATE_PARENT,
	  0x0B8, 6, 1, CLK_MUX_HIWORD_MASK, },
	{ HI3670_CLK_MUX_SD_PLL, "clk_mux_sd_pll", clk_mux_sd_pll_p,
	  ARRAY_SIZE(clk_mux_sd_pll_p), CLK_SET_RATE_PARENT,
	  0x0B8, 4, 2, CLK_MUX_HIWORD_MASK, },
	{ HI3670_CLK_MUX_SDIO_SYS, "clk_mux_sdio_sys", clk_mux_sdio_sys_p,
	  ARRAY_SIZE(clk_mux_sdio_sys_p), CLK_SET_RATE_PARENT,
	  0x0C0, 6, 1, CLK_MUX_HIWORD_MASK, },
	{ HI3670_CLK_MUX_SDIO_PLL, "clk_mux_sdio_pll", clk_mux_sdio_pll_p,
	  ARRAY_SIZE(clk_mux_sdio_pll_p), CLK_SET_RATE_PARENT,
	  0x0C0, 4, 2, CLK_MUX_HIWORD_MASK, },
	{ HI3670_CLK_MUX_A53HPM, "clk_mux_a53hpm", clk_mux_a53hpm_p,
	  ARRAY_SIZE(clk_mux_a53hpm_p), CLK_SET_RATE_PARENT,
	  0x0D4, 9, 1, CLK_MUX_HIWORD_MASK, },
	{ HI3670_CLK_MUX_320M, "clk_mux_320m", clk_mux_320m_p,
	  ARRAY_SIZE(clk_mux_320m_p), CLK_SET_RATE_PARENT,
	  0x100, 0, 1, CLK_MUX_HIWORD_MASK, },
	{ HI3670_CLK_MUX_UARTH, "clk_mux_uarth", clk_mux_uarth_p,
	  ARRAY_SIZE(clk_mux_uarth_p), CLK_SET_RATE_PARENT,
	  0xAC, 4, 1, CLK_MUX_HIWORD_MASK, },
	{ HI3670_CLK_MUX_UARTL, "clk_mux_uartl", clk_mux_uartl_p,
	  ARRAY_SIZE(clk_mux_uartl_p), CLK_SET_RATE_PARENT,
	  0xAC, 3, 1, CLK_MUX_HIWORD_MASK, },
	{ HI3670_CLK_MUX_UART0, "clk_mux_uart0", clk_mux_uart0_p,
	  ARRAY_SIZE(clk_mux_uart0_p), CLK_SET_RATE_PARENT,
	  0xAC, 2, 1, CLK_MUX_HIWORD_MASK, },
	{ HI3670_CLK_MUX_I2C, "clk_mux_i2c", clk_mux_i2c_p,
	  ARRAY_SIZE(clk_mux_i2c_p), CLK_SET_RATE_PARENT,
	  0xAC, 13, 1, CLK_MUX_HIWORD_MASK, },
	{ HI3670_CLK_MUX_SPI, "clk_mux_spi", clk_mux_spi_p,
	  ARRAY_SIZE(clk_mux_spi_p), CLK_SET_RATE_PARENT,
	  0xAC, 8, 1, CLK_MUX_HIWORD_MASK, },
	{ HI3670_CLK_MUX_PCIEAXI, "clk_mux_pcieaxi", clk_mux_pcieaxi_p,
	  ARRAY_SIZE(clk_mux_pcieaxi_p), CLK_SET_RATE_PARENT,
	  0xb4, 5, 1, CLK_MUX_HIWORD_MASK, },
	{ HI3670_CLK_MUX_AO_ASP, "clk_mux_ao_asp", clk_mux_ao_asp_p,
	  ARRAY_SIZE(clk_mux_ao_asp_p), CLK_SET_RATE_PARENT,
	  0x100, 6, 1, CLK_MUX_HIWORD_MASK, },
	{ HI3670_CLK_MUX_VDEC, "clk_mux_vdec", clk_mux_vdec_p,
	  ARRAY_SIZE(clk_mux_vdec_p), CLK_SET_RATE_PARENT,
	  0xC8, 8, 4, CLK_MUX_HIWORD_MASK, },
	{ HI3670_CLK_MUX_VENC, "clk_mux_venc", clk_mux_venc_p,
	  ARRAY_SIZE(clk_mux_venc_p), CLK_SET_RATE_PARENT,
	  0xC8, 4, 4, CLK_MUX_HIWORD_MASK, },
	{ HI3670_CLK_ISP_SNCLK_MUX0, "clk_isp_snclk_mux0", clk_isp_snclk_mux0_p,
	  ARRAY_SIZE(clk_isp_snclk_mux0_p), CLK_SET_RATE_PARENT,
	  0x108, 3, 1, CLK_MUX_HIWORD_MASK, },
	{ HI3670_CLK_ISP_SNCLK_MUX1, "clk_isp_snclk_mux1", clk_isp_snclk_mux1_p,
	  ARRAY_SIZE(clk_isp_snclk_mux1_p), CLK_SET_RATE_PARENT,
	  0x10C, 13, 1, CLK_MUX_HIWORD_MASK, },
	{ HI3670_CLK_ISP_SNCLK_MUX2, "clk_isp_snclk_mux2", clk_isp_snclk_mux2_p,
	  ARRAY_SIZE(clk_isp_snclk_mux2_p), CLK_SET_RATE_PARENT,
	  0x10C, 10, 1, CLK_MUX_HIWORD_MASK, },
	{ HI3670_CLK_MUX_RXDPHY_CFG, "clk_mux_rxdphy_cfg", clk_mux_rxdphy_cfg_p,
	  ARRAY_SIZE(clk_mux_rxdphy_cfg_p), CLK_SET_RATE_PARENT,
	  0x0C4, 8, 1, CLK_MUX_HIWORD_MASK, },
	{ HI3670_CLK_MUX_ICS, "clk_mux_ics", clk_mux_ics_p,
	  ARRAY_SIZE(clk_mux_ics_p), CLK_SET_RATE_PARENT,
	  0xc8, 12, 4, CLK_MUX_HIWORD_MASK, },
};

static const struct hisi_divider_clock hi3670_crgctrl_divider_clks[] = {
	{ HI3670_CLK_DIV_CFGBUS, "clk_div_cfgbus", "clk_div_sysbus",
	  CLK_SET_RATE_PARENT, 0xEC, 0, 2, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_CLK_DIV_MMC0BUS, "clk_div_mmc0bus", "autodiv_emmc0bus",
	  CLK_SET_RATE_PARENT, 0x0EC, 2, 1, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_CLK_DIV_MMC1BUS, "clk_div_mmc1bus", "clk_div_sysbus",
	  CLK_SET_RATE_PARENT, 0x0EC, 3, 1, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_PCLK_DIV_MMC1_PCIE, "pclk_div_mmc1_pcie", "pclk_andgt_mmc1_pcie",
	  CLK_SET_RATE_PARENT, 0xb4, 6, 4, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_CLK_DIV_VCODECBUS, "clk_div_vcodecbus", "clk_gate_vcodecbus_gt",
	  CLK_SET_RATE_PARENT, 0x0BC, 0, 6, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_CLK_DIV_SD, "clk_div_sd", "clk_andgt_sd",
	  CLK_SET_RATE_PARENT, 0xB8, 0, 4, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_CLK_DIV_SDIO, "clk_div_sdio", "clk_andgt_sdio",
	  CLK_SET_RATE_PARENT, 0xC0, 0, 4, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_CLK_DIV_UARTH, "clk_div_uarth", "clk_andgt_uarth",
	  CLK_SET_RATE_PARENT, 0xB0, 12, 4, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_CLK_DIV_UARTL, "clk_div_uartl", "clk_andgt_uartl",
	  CLK_SET_RATE_PARENT, 0xB0, 8, 4, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_CLK_DIV_UART0, "clk_div_uart0", "clk_andgt_uart0",
	  CLK_SET_RATE_PARENT, 0xB0, 4, 4, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_CLK_DIV_I2C, "clk_div_i2c", "clk_div_320m",
	  CLK_SET_RATE_PARENT, 0xE8, 4, 4, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_CLK_DIV_SPI, "clk_div_spi", "clk_andgt_spi",
	  CLK_SET_RATE_PARENT, 0xC4, 12, 4, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_CLK_DIV_PCIEAXI, "clk_div_pcieaxi", "clk_andgt_pcieaxi",
	  CLK_SET_RATE_PARENT, 0xb4, 0, 5, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_CLK_DIV_AO_ASP, "clk_div_ao_asp", "clk_div_ao_asp_gt",
	  CLK_SET_RATE_PARENT, 0x108, 6, 4, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_CLK_DIV_CSI_TRANS, "clk_div_csi_trans", "clk_gate_csi_trans",
	  CLK_SET_RATE_PARENT, 0xD4, 0, 5, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_CLK_DIV_DSI_TRANS, "clk_div_dsi_trans", "clk_gate_dsi_trans",
	  CLK_SET_RATE_PARENT, 0xD4, 10, 5, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_CLK_DIV_PTP, "clk_div_ptp", "clk_andgt_ptp",
	  CLK_SET_RATE_PARENT, 0xD8, 0, 4, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_CLK_DIV_CLKOUT0_PLL, "clk_div_clkout0_pll", "clk_andgt_out0",
	  CLK_SET_RATE_PARENT, 0xe0, 4, 6, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_CLK_DIV_CLKOUT1_PLL, "clk_div_clkout1_pll", "clk_andgt_out1",
	  CLK_SET_RATE_PARENT, 0xe0, 10, 6, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_CLKDIV_DP_AUDIO_PLL_AO, "clkdiv_dp_audio_pll_ao", "clkgt_dp_audio_pll_ao",
	  CLK_SET_RATE_PARENT, 0xBC, 11, 4, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_CLK_DIV_VDEC, "clk_div_vdec", "clk_andgt_vdec",
	  CLK_SET_RATE_PARENT, 0xC4, 0, 6, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_CLK_DIV_VENC, "clk_div_venc", "clk_andgt_venc",
	  CLK_SET_RATE_PARENT, 0xC0, 8, 6, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_CLK_ISP_SNCLK_DIV0, "clk_isp_snclk_div0", "clk_isp_snclk_fac",
	  CLK_SET_RATE_PARENT, 0x108, 0, 2, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_CLK_ISP_SNCLK_DIV1, "clk_isp_snclk_div1", "clk_isp_snclk_fac",
	  CLK_SET_RATE_PARENT, 0x10C, 14, 2, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_CLK_ISP_SNCLK_DIV2, "clk_isp_snclk_div2", "clk_isp_snclk_fac",
	  CLK_SET_RATE_PARENT, 0x10C, 11, 2, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_CLK_DIV_ICS, "clk_div_ics", "clk_andgt_ics",
	  CLK_SET_RATE_PARENT, 0xE4, 9, 6, CLK_DIVIDER_HIWORD_MASK, },
};

/* clk_pmuctrl */
static const struct hisi_gate_clock hi3670_pmu_gate_clks[] = {
	{ HI3670_GATE_ABB_192, "clk_gate_abb_192", "clkin_sys",
	  CLK_SET_RATE_PARENT, (0x037 << 2), 0, 0, },
};

/* clk_pctrl */
static const struct hisi_gate_clock hi3670_pctrl_gate_clks[] = {
	{ HI3670_GATE_UFS_TCXO_EN, "clk_gate_ufs_tcxo_en", "clk_gate_abb_192",
	  CLK_SET_RATE_PARENT, 0x10, 0, CLK_GATE_HIWORD_MASK, },
	{ HI3670_GATE_USB_TCXO_EN, "clk_gate_usb_tcxo_en", "clk_gate_abb_192",
	  CLK_SET_RATE_PARENT, 0x10, 1, CLK_GATE_HIWORD_MASK, },
};

/* clk_sctrl */
static const struct hisi_gate_clock hi3670_sctrl_gate_sep_clks[] = {
	{ HI3670_PPLL0_EN_ACPU, "ppll0_en_acpu", "clk_ppll0",
	  CLK_SET_RATE_PARENT, 0x190, 26, 0, },
	{ HI3670_PPLL0_GT_CPU, "ppll0_gt_cpu", "clk_ppll0",
	  CLK_SET_RATE_PARENT, 0x190, 15, 0, },
	{ HI3670_CLK_GATE_PPLL0_MEDIA, "clk_gate_ppll0_media", "clk_ppll0",
	  CLK_SET_RATE_PARENT, 0x1b0, 6, 0, },
	{ HI3670_PCLK_GPIO18, "pclk_gpio18", "clk_div_aobus",
	  CLK_SET_RATE_PARENT, 0x1B0, 9, 0, },
	{ HI3670_PCLK_GPIO19, "pclk_gpio19", "clk_div_aobus",
	  CLK_SET_RATE_PARENT, 0x1B0, 8, 0, },
	{ HI3670_CLK_GATE_SPI, "clk_gate_spi", "clk_div_ioperi",
	  CLK_SET_RATE_PARENT, 0x1B0, 10, 0, },
	{ HI3670_PCLK_GATE_SPI, "pclk_gate_spi", "clk_div_ioperi",
	  CLK_SET_RATE_PARENT, 0x1B0, 10, 0, },
	{ HI3670_CLK_GATE_UFS_SUBSYS, "clk_gate_ufs_subsys", "clk_div_ufs_subsys",
	  CLK_SET_RATE_PARENT, 0x1B0, 14, 0, },
	{ HI3670_CLK_GATE_UFSIO_REF, "clk_gate_ufsio_ref", "clkin_sys",
	  CLK_SET_RATE_PARENT, 0x1b0, 12, 0, },
	{ HI3670_PCLK_AO_GPIO0, "pclk_ao_gpio0", "clk_div_aobus",
	  CLK_SET_RATE_PARENT, 0x160, 11, 0, },
	{ HI3670_PCLK_AO_GPIO1, "pclk_ao_gpio1", "clk_div_aobus",
	  CLK_SET_RATE_PARENT, 0x160, 12, 0, },
	{ HI3670_PCLK_AO_GPIO2, "pclk_ao_gpio2", "clk_div_aobus",
	  CLK_SET_RATE_PARENT, 0x160, 13, 0, },
	{ HI3670_PCLK_AO_GPIO3, "pclk_ao_gpio3", "clk_div_aobus",
	  CLK_SET_RATE_PARENT, 0x160, 14, 0, },
	{ HI3670_PCLK_AO_GPIO4, "pclk_ao_gpio4", "clk_div_aobus",
	  CLK_SET_RATE_PARENT, 0x160, 21, 0, },
	{ HI3670_PCLK_AO_GPIO5, "pclk_ao_gpio5", "clk_div_aobus",
	  CLK_SET_RATE_PARENT, 0x160, 22, 0, },
	{ HI3670_PCLK_AO_GPIO6, "pclk_ao_gpio6", "clk_div_aobus",
	  CLK_SET_RATE_PARENT, 0x160, 25, 0, },
	{ HI3670_CLK_GATE_OUT0, "clk_gate_out0", "clk_mux_clkout0",
	  CLK_SET_RATE_PARENT, 0x160, 16, 0, },
	{ HI3670_CLK_GATE_OUT1, "clk_gate_out1", "clk_mux_clkout1",
	  CLK_SET_RATE_PARENT, 0x160, 17, 0, },
	{ HI3670_PCLK_GATE_SYSCNT, "pclk_gate_syscnt", "clk_div_aobus",
	  CLK_SET_RATE_PARENT, 0x160, 19, 0, },
	{ HI3670_CLK_GATE_SYSCNT, "clk_gate_syscnt", "clkin_sys",
	  CLK_SET_RATE_PARENT, 0x160, 20, 0, },
	{ HI3670_CLK_GATE_ASP_SUBSYS_PERI, "clk_gate_asp_subsys_peri",
	  "clk_mux_asp_subsys_peri",
	  CLK_SET_RATE_PARENT, 0x170, 6, 0, },
	{ HI3670_CLK_GATE_ASP_SUBSYS, "clk_gate_asp_subsys", "clk_mux_asp_pll",
	  CLK_SET_RATE_PARENT, 0x170, 4, 0, },
	{ HI3670_CLK_GATE_ASP_TCXO, "clk_gate_asp_tcxo", "clkin_sys",
	  CLK_SET_RATE_PARENT, 0x160, 27, 0, },
	{ HI3670_CLK_GATE_DP_AUDIO_PLL, "clk_gate_dp_audio_pll",
	  "clk_gate_dp_audio_pll_ao",
	  CLK_SET_RATE_PARENT, 0x1B0, 7, 0, },
};

static const struct hisi_gate_clock hi3670_sctrl_gate_clks[] = {
	{ HI3670_CLK_ANDGT_IOPERI, "clk_andgt_ioperi", "clk_ppll0",
	  CLK_SET_RATE_PARENT, 0x270, 6, CLK_GATE_HIWORD_MASK, },
	{ HI3670_CLKANDGT_ASP_SUBSYS_PERI, "clkandgt_asp_subsys_peri",
	  "clk_ppll0",
	  CLK_SET_RATE_PARENT, 0x268, 3, CLK_GATE_HIWORD_MASK, },
	{ HI3670_CLK_ANGT_ASP_SUBSYS, "clk_angt_asp_subsys", "clk_ppll0",
	  CLK_SET_RATE_PARENT, 0x258, 0, CLK_GATE_HIWORD_MASK, },
};

static const char *const
clk_mux_ufs_subsys_p[] = { "clkin_sys", "clk_ppll0", };
static const char *const
clk_mux_clkout0_p[] = { "clkin_ref", "clk_div_clkout0_tcxo",
			"clk_div_clkout0_pll", "clk_div_clkout0_pll", };
static const char *const
clk_mux_clkout1_p[] = { "clkin_ref", "clk_div_clkout1_tcxo",
			"clk_div_clkout1_pll", "clk_div_clkout1_pll", };
static const char *const
clk_mux_asp_subsys_peri_p[] = { "clk_ppll0", "clk_fll_src", };
static const char *const
clk_mux_asp_pll_p[] = { "clk_ppll0", "clk_fll_src", "clk_gate_ao_asp",
			"clk_pciepll_rev", };

static const struct hisi_mux_clock hi3670_sctrl_mux_clks[] = {
	{ HI3670_CLK_MUX_UFS_SUBSYS, "clk_mux_ufs_subsys", clk_mux_ufs_subsys_p,
	  ARRAY_SIZE(clk_mux_ufs_subsys_p), CLK_SET_RATE_PARENT,
	  0x274, 8, 1, CLK_MUX_HIWORD_MASK, },
	{ HI3670_CLK_MUX_CLKOUT0, "clk_mux_clkout0", clk_mux_clkout0_p,
	  ARRAY_SIZE(clk_mux_clkout0_p), CLK_SET_RATE_PARENT,
	  0x254, 12, 2, CLK_MUX_HIWORD_MASK, },
	{ HI3670_CLK_MUX_CLKOUT1, "clk_mux_clkout1", clk_mux_clkout1_p,
	  ARRAY_SIZE(clk_mux_clkout1_p), CLK_SET_RATE_PARENT,
	  0x254, 14, 2, CLK_MUX_HIWORD_MASK, },
	{ HI3670_CLK_MUX_ASP_SUBSYS_PERI, "clk_mux_asp_subsys_peri",
	  clk_mux_asp_subsys_peri_p, ARRAY_SIZE(clk_mux_asp_subsys_peri_p),
	  CLK_SET_RATE_PARENT, 0x268, 8, 1, CLK_MUX_HIWORD_MASK, },
	{ HI3670_CLK_MUX_ASP_PLL, "clk_mux_asp_pll", clk_mux_asp_pll_p,
	  ARRAY_SIZE(clk_mux_asp_pll_p), CLK_SET_RATE_PARENT,
	  0x268, 9, 2, CLK_MUX_HIWORD_MASK, },
};

static const struct hisi_divider_clock hi3670_sctrl_divider_clks[] = {
	{ HI3670_CLK_DIV_AOBUS, "clk_div_aobus", "clk_ppll0",
	  CLK_SET_RATE_PARENT, 0x254, 0, 6, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_CLK_DIV_UFS_SUBSYS, "clk_div_ufs_subsys", "clk_mux_ufs_subsys",
	  CLK_SET_RATE_PARENT, 0x274, 0, 6, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_CLK_DIV_IOPERI, "clk_div_ioperi", "clk_andgt_ioperi",
	  CLK_SET_RATE_PARENT, 0x270, 0, 6, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_CLK_DIV_CLKOUT0_TCXO, "clk_div_clkout0_tcxo", "clkin_sys",
	  CLK_SET_RATE_PARENT, 0x254, 6, 3, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_CLK_DIV_CLKOUT1_TCXO, "clk_div_clkout1_tcxo", "clkin_sys",
	  CLK_SET_RATE_PARENT, 0x254, 9, 3, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_CLK_ASP_SUBSYS_PERI_DIV, "clk_asp_subsys_peri_div", "clkandgt_asp_subsys_peri",
	  CLK_SET_RATE_PARENT, 0x268, 0, 3, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_CLK_DIV_ASP_SUBSYS, "clk_div_asp_subsys", "clk_angt_asp_subsys",
	  CLK_SET_RATE_PARENT, 0x250, 0, 3, CLK_DIVIDER_HIWORD_MASK, },
};

/* clk_iomcu */
static const struct hisi_fixed_factor_clock hi3670_iomcu_fixed_factor_clks[] = {
	{ HI3670_CLK_GATE_I2C0, "clk_gate_i2c0", "clk_i2c0_gate_iomcu", 1, 4, 0, },
	{ HI3670_CLK_GATE_I2C1, "clk_gate_i2c1", "clk_i2c1_gate_iomcu", 1, 4, 0, },
	{ HI3670_CLK_GATE_I2C2, "clk_gate_i2c2", "clk_i2c2_gate_iomcu", 1, 4, 0, },
	{ HI3670_CLK_GATE_SPI0, "clk_gate_spi0", "clk_spi0_gate_iomcu", 1, 1, 0, },
	{ HI3670_CLK_GATE_SPI2, "clk_gate_spi2", "clk_spi2_gate_iomcu", 1, 1, 0, },
	{ HI3670_CLK_GATE_UART3, "clk_gate_uart3", "clk_uart3_gate_iomcu", 1, 16, 0, },
};

static const struct hisi_gate_clock hi3670_iomcu_gate_sep_clks[] = {
	{ HI3670_CLK_I2C0_GATE_IOMCU, "clk_i2c0_gate_iomcu", "clk_fll_src",
	  CLK_SET_RATE_PARENT, 0x10, 3, 0, },
	{ HI3670_CLK_I2C1_GATE_IOMCU, "clk_i2c1_gate_iomcu", "clk_fll_src",
	  CLK_SET_RATE_PARENT, 0x10, 4, 0, },
	{ HI3670_CLK_I2C2_GATE_IOMCU, "clk_i2c2_gate_iomcu", "clk_fll_src",
	  CLK_SET_RATE_PARENT, 0x10, 5, 0, },
	{ HI3670_CLK_SPI0_GATE_IOMCU, "clk_spi0_gate_iomcu", "clk_fll_src",
	  CLK_SET_RATE_PARENT, 0x10, 10, 0, },
	{ HI3670_CLK_SPI2_GATE_IOMCU, "clk_spi2_gate_iomcu", "clk_fll_src",
	  CLK_SET_RATE_PARENT, 0x10, 30, 0, },
	{ HI3670_CLK_UART3_GATE_IOMCU, "clk_uart3_gate_iomcu", "clk_gate_iomcu_peri0",
	  CLK_SET_RATE_PARENT, 0x10, 11, 0, },
	{ HI3670_CLK_GATE_PERI0_IOMCU, "clk_gate_iomcu_peri0", "clk_ppll0",
	  CLK_SET_RATE_PARENT, 0x90, 0, 0, },
};

/* clk_media1 */
static const struct hisi_gate_clock hi3670_media1_gate_sep_clks[] = {
	{ HI3670_ACLK_GATE_NOC_DSS, "aclk_gate_noc_dss", "aclk_gate_disp_noc_subsys",
	  CLK_SET_RATE_PARENT, 0x10, 21, 0, },
	{ HI3670_PCLK_GATE_NOC_DSS_CFG, "pclk_gate_noc_dss_cfg", "pclk_gate_disp_noc_subsys",
	  CLK_SET_RATE_PARENT, 0x10, 22, 0, },
	{ HI3670_PCLK_GATE_MMBUF_CFG, "pclk_gate_mmbuf_cfg", "pclk_gate_disp_noc_subsys",
	  CLK_SET_RATE_PARENT, 0x20, 5, 0, },
	{ HI3670_PCLK_GATE_DISP_NOC_SUBSYS, "pclk_gate_disp_noc_subsys", "clk_div_sysbus",
	  CLK_SET_RATE_PARENT, 0x10, 18, 0, },
	{ HI3670_ACLK_GATE_DISP_NOC_SUBSYS, "aclk_gate_disp_noc_subsys", "clk_gate_vivobusfreq",
	  CLK_SET_RATE_PARENT, 0x10, 17, 0, },
	{ HI3670_PCLK_GATE_DSS, "pclk_gate_dss", "pclk_gate_disp_noc_subsys",
	  CLK_SET_RATE_PARENT, 0x00, 14, 0, },
	{ HI3670_ACLK_GATE_DSS, "aclk_gate_dss", "aclk_gate_disp_noc_subsys",
	  CLK_SET_RATE_PARENT, 0x00, 19, 0, },
	{ HI3670_CLK_GATE_VIVOBUSFREQ, "clk_gate_vivobusfreq", "clk_div_vivobus",
	  CLK_SET_RATE_PARENT, 0x00, 18, 0, },
	{ HI3670_CLK_GATE_EDC0, "clk_gate_edc0", "clk_div_edc0",
	  CLK_SET_RATE_PARENT, 0x00, 15, 0, },
	{ HI3670_CLK_GATE_LDI0, "clk_gate_ldi0", "clk_div_ldi0",
	  CLK_SET_RATE_PARENT, 0x00, 16, 0, },
	{ HI3670_CLK_GATE_LDI1FREQ, "clk_gate_ldi1freq", "clk_div_ldi1",
	  CLK_SET_RATE_PARENT, 0x00, 17, 0, },
	{ HI3670_CLK_GATE_BRG, "clk_gate_brg", "clk_media_common_div",
	  CLK_SET_RATE_PARENT, 0x00, 29, 0, },
	{ HI3670_ACLK_GATE_ASC, "aclk_gate_asc", "clk_gate_mmbuf",
	  CLK_SET_RATE_PARENT, 0x20, 3, 0, },
	{ HI3670_CLK_GATE_DSS_AXI_MM, "clk_gate_dss_axi_mm", "clk_gate_mmbuf",
	  CLK_SET_RATE_PARENT, 0x20, 4, 0, },
	{ HI3670_CLK_GATE_MMBUF, "clk_gate_mmbuf", "aclk_div_mmbuf",
	  CLK_SET_RATE_PARENT, 0x20, 0, 0, },
	{ HI3670_PCLK_GATE_MMBUF, "pclk_gate_mmbuf", "pclk_div_mmbuf",
	  CLK_SET_RATE_PARENT, 0x20, 1, 0, },
	{ HI3670_CLK_GATE_ATDIV_VIVO, "clk_gate_atdiv_vivo", "clk_div_vivobus",
	  CLK_SET_RATE_PARENT, 0x010, 1, 0, },
};

static const struct hisi_gate_clock hi3670_media1_gate_clks[] = {
	{ HI3670_CLK_GATE_VIVOBUS_ANDGT, "clk_gate_vivobus_andgt", "clk_mux_vivobus",
	  CLK_SET_RATE_PARENT, 0x84, 3, CLK_GATE_HIWORD_MASK, },
	{ HI3670_CLK_ANDGT_EDC0, "clk_andgt_edc0", "clk_mux_edc0",
	  CLK_SET_RATE_PARENT, 0x84, 7, CLK_GATE_HIWORD_MASK, },
	{ HI3670_CLK_ANDGT_LDI0, "clk_andgt_ldi0", "clk_mux_ldi0",
	  CLK_SET_RATE_PARENT, 0x84, 9, CLK_GATE_HIWORD_MASK, },
	{ HI3670_CLK_ANDGT_LDI1, "clk_andgt_ldi1", "clk_mux_ldi1",
	  CLK_SET_RATE_PARENT, 0x84, 8, CLK_GATE_HIWORD_MASK, },
	{ HI3670_CLK_MMBUF_PLL_ANDGT, "clk_mmbuf_pll_andgt", "clk_sw_mmbuf",
	  CLK_SET_RATE_PARENT, 0x84, 14, CLK_GATE_HIWORD_MASK, },
	{ HI3670_PCLK_MMBUF_ANDGT, "pclk_mmbuf_andgt", "aclk_div_mmbuf",
	  CLK_SET_RATE_PARENT, 0x84, 15, CLK_GATE_HIWORD_MASK, },
};

static const char *const
clk_mux_vivobus_p[] = { "clk_invalid", "clk_invalid", "clk_gate_ppll0_media",
			"clk_invalid", "clk_gate_ppll2_media", "clk_invalid",
			"clk_invalid", "clk_invalid", "clk_gate_ppll3_media",
			"clk_invalid", "clk_invalid", "clk_invalid",
			"clk_invalid", "clk_invalid", "clk_invalid",
			"clk_invalid", };
static const char *const
clk_mux_edc0_p[] = { "clk_invalid", "clk_invalid", "clk_gate_ppll0_media",
		     "clk_invalid", "clk_gate_ppll2_media", "clk_invalid",
		     "clk_invalid", "clk_invalid", "clk_gate_ppll3_media",
		     "clk_invalid", "clk_invalid", "clk_invalid", "clk_invalid",
		     "clk_invalid", "clk_invalid", "clk_invalid", };
static const char *const
clk_mux_ldi0_p[] = { "clk_invalid", "clk_gate_ppll7_media",
		     "clk_gate_ppll0_media", "clk_invalid",
		     "clk_gate_ppll2_media", "clk_invalid", "clk_invalid",
		     "clk_invalid", "clk_gate_ppll3_media", "clk_invalid",
		     "clk_invalid", "clk_invalid", "clk_invalid", "clk_invalid",
		     "clk_invalid", "clk_invalid", };
static const char *const
clk_mux_ldi1_p[] = { "clk_invalid", "clk_gate_ppll7_media",
		     "clk_gate_ppll0_media", "clk_invalid",
		     "clk_gate_ppll2_media", "clk_invalid", "clk_invalid",
		     "clk_invalid", "clk_gate_ppll3_media", "clk_invalid",
		     "clk_invalid", "clk_invalid", "clk_invalid", "clk_invalid",
		     "clk_invalid", "clk_invalid", };
static const char *const
clk_sw_mmbuf_p[] = { "clk_invalid", "clk_invalid", "clk_gate_ppll0_media",
		     "clk_invalid", "clk_gate_ppll2_media", "clk_invalid",
		     "clk_invalid", "clk_invalid", "clk_gate_ppll3_media",
		     "clk_invalid", "clk_invalid", "clk_invalid", "clk_invalid",
		     "clk_invalid", "clk_invalid", "clk_invalid", };

static const struct hisi_mux_clock hi3670_media1_mux_clks[] = {
	{ HI3670_CLK_MUX_VIVOBUS, "clk_mux_vivobus", clk_mux_vivobus_p,
	  ARRAY_SIZE(clk_mux_vivobus_p), CLK_SET_RATE_PARENT,
	  0x74, 6, 4, CLK_MUX_HIWORD_MASK, },
	{ HI3670_CLK_MUX_EDC0, "clk_mux_edc0", clk_mux_edc0_p,
	  ARRAY_SIZE(clk_mux_edc0_p), CLK_SET_RATE_PARENT,
	  0x68, 6, 4, CLK_MUX_HIWORD_MASK, },
	{ HI3670_CLK_MUX_LDI0, "clk_mux_ldi0", clk_mux_ldi0_p,
	  ARRAY_SIZE(clk_mux_ldi0_p), CLK_SET_RATE_PARENT,
	  0x60, 6, 4, CLK_MUX_HIWORD_MASK, },
	{ HI3670_CLK_MUX_LDI1, "clk_mux_ldi1", clk_mux_ldi1_p,
	  ARRAY_SIZE(clk_mux_ldi1_p), CLK_SET_RATE_PARENT,
	  0x64, 6, 4, CLK_MUX_HIWORD_MASK, },
	{ HI3670_CLK_SW_MMBUF, "clk_sw_mmbuf", clk_sw_mmbuf_p,
	  ARRAY_SIZE(clk_sw_mmbuf_p), CLK_SET_RATE_PARENT,
	  0x88, 0, 4, CLK_MUX_HIWORD_MASK, },
};

static const struct hisi_divider_clock hi3670_media1_divider_clks[] = {
	{ HI3670_CLK_DIV_VIVOBUS, "clk_div_vivobus", "clk_gate_vivobus_andgt",
	  CLK_SET_RATE_PARENT, 0x74, 0, 6, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_CLK_DIV_EDC0, "clk_div_edc0", "clk_andgt_edc0",
	  CLK_SET_RATE_PARENT, 0x68, 0, 6, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_CLK_DIV_LDI0, "clk_div_ldi0", "clk_andgt_ldi0",
	  CLK_SET_RATE_PARENT, 0x60, 0, 6, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_CLK_DIV_LDI1, "clk_div_ldi1", "clk_andgt_ldi1",
	  CLK_SET_RATE_PARENT, 0x64, 0, 6, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_ACLK_DIV_MMBUF, "aclk_div_mmbuf", "clk_mmbuf_pll_andgt",
	  CLK_SET_RATE_PARENT, 0x7C, 10, 6, CLK_DIVIDER_HIWORD_MASK, },
	{ HI3670_PCLK_DIV_MMBUF, "pclk_div_mmbuf", "pclk_mmbuf_andgt",
	  CLK_SET_RATE_PARENT, 0x78, 0, 2, CLK_DIVIDER_HIWORD_MASK, },
};

/* clk_media2 */
static const struct hisi_gate_clock hi3670_media2_gate_sep_clks[] = {
	{ HI3670_CLK_GATE_VDECFREQ, "clk_gate_vdecfreq", "clk_div_vdec",
	  CLK_SET_RATE_PARENT, 0x00, 8, 0, },
	{ HI3670_CLK_GATE_VENCFREQ, "clk_gate_vencfreq", "clk_div_venc",
	  CLK_SET_RATE_PARENT, 0x00, 5, 0, },
	{ HI3670_CLK_GATE_ICSFREQ, "clk_gate_icsfreq", "clk_div_ics",
	  CLK_SET_RATE_PARENT, 0x00, 2, 0, },
};

static void hi3670_clk_crgctrl_init(struct device_node *np)
{
	struct hisi_clock_data *clk_data;

	int nr = ARRAY_SIZE(hi3670_fixed_rate_clks) +
		 ARRAY_SIZE(hi3670_crgctrl_gate_sep_clks) +
		 ARRAY_SIZE(hi3670_crgctrl_gate_clks) +
		 ARRAY_SIZE(hi3670_crgctrl_mux_clks) +
		 ARRAY_SIZE(hi3670_crg_fixed_factor_clks) +
		 ARRAY_SIZE(hi3670_crgctrl_divider_clks);

	clk_data = hisi_clk_init(np, nr);
	if (!clk_data)
		return;

	hisi_clk_register_fixed_rate(hi3670_fixed_rate_clks,
				     ARRAY_SIZE(hi3670_fixed_rate_clks),
				     clk_data);
	hisi_clk_register_gate_sep(hi3670_crgctrl_gate_sep_clks,
				   ARRAY_SIZE(hi3670_crgctrl_gate_sep_clks),
				   clk_data);
	hisi_clk_register_gate(hi3670_crgctrl_gate_clks,
			       ARRAY_SIZE(hi3670_crgctrl_gate_clks),
			       clk_data);
	hisi_clk_register_mux(hi3670_crgctrl_mux_clks,
			      ARRAY_SIZE(hi3670_crgctrl_mux_clks),
			      clk_data);
	hisi_clk_register_fixed_factor(hi3670_crg_fixed_factor_clks,
				       ARRAY_SIZE(hi3670_crg_fixed_factor_clks),
				       clk_data);
	hisi_clk_register_divider(hi3670_crgctrl_divider_clks,
				  ARRAY_SIZE(hi3670_crgctrl_divider_clks),
				  clk_data);
}

static void hi3670_clk_pctrl_init(struct device_node *np)
{
	struct hisi_clock_data *clk_data;
	int nr = ARRAY_SIZE(hi3670_pctrl_gate_clks);

	clk_data = hisi_clk_init(np, nr);
	if (!clk_data)
		return;
	hisi_clk_register_gate(hi3670_pctrl_gate_clks,
			       ARRAY_SIZE(hi3670_pctrl_gate_clks), clk_data);
}

static void hi3670_clk_pmuctrl_init(struct device_node *np)
{
	struct hisi_clock_data *clk_data;
	int nr = ARRAY_SIZE(hi3670_pmu_gate_clks);

	clk_data = hisi_clk_init(np, nr);
	if (!clk_data)
		return;

	hisi_clk_register_gate(hi3670_pmu_gate_clks,
			       ARRAY_SIZE(hi3670_pmu_gate_clks), clk_data);
}

static void hi3670_clk_sctrl_init(struct device_node *np)
{
	struct hisi_clock_data *clk_data;
	int nr = ARRAY_SIZE(hi3670_sctrl_gate_sep_clks) +
		 ARRAY_SIZE(hi3670_sctrl_gate_clks) +
		 ARRAY_SIZE(hi3670_sctrl_mux_clks) +
		 ARRAY_SIZE(hi3670_sctrl_divider_clks);

	clk_data = hisi_clk_init(np, nr);
	if (!clk_data)
		return;

	hisi_clk_register_gate_sep(hi3670_sctrl_gate_sep_clks,
				   ARRAY_SIZE(hi3670_sctrl_gate_sep_clks),
				   clk_data);
	hisi_clk_register_gate(hi3670_sctrl_gate_clks,
			       ARRAY_SIZE(hi3670_sctrl_gate_clks),
			       clk_data);
	hisi_clk_register_mux(hi3670_sctrl_mux_clks,
			      ARRAY_SIZE(hi3670_sctrl_mux_clks),
			      clk_data);
	hisi_clk_register_divider(hi3670_sctrl_divider_clks,
				  ARRAY_SIZE(hi3670_sctrl_divider_clks),
				  clk_data);
}

static void hi3670_clk_iomcu_init(struct device_node *np)
{
	struct hisi_clock_data *clk_data;
	int nr = ARRAY_SIZE(hi3670_iomcu_gate_sep_clks) +
			ARRAY_SIZE(hi3670_iomcu_fixed_factor_clks);

	clk_data = hisi_clk_init(np, nr);
	if (!clk_data)
		return;

	hisi_clk_register_gate(hi3670_iomcu_gate_sep_clks,
			       ARRAY_SIZE(hi3670_iomcu_gate_sep_clks), clk_data);

	hisi_clk_register_fixed_factor(hi3670_iomcu_fixed_factor_clks,
				       ARRAY_SIZE(hi3670_iomcu_fixed_factor_clks),
				       clk_data);
}

static void hi3670_clk_media1_init(struct device_node *np)
{
	struct hisi_clock_data *clk_data;

	int nr = ARRAY_SIZE(hi3670_media1_gate_sep_clks) +
		 ARRAY_SIZE(hi3670_media1_gate_clks) +
		 ARRAY_SIZE(hi3670_media1_mux_clks) +
		 ARRAY_SIZE(hi3670_media1_divider_clks);

	clk_data = hisi_clk_init(np, nr);
	if (!clk_data)
		return;

	hisi_clk_register_gate_sep(hi3670_media1_gate_sep_clks,
				   ARRAY_SIZE(hi3670_media1_gate_sep_clks),
				   clk_data);
	hisi_clk_register_gate(hi3670_media1_gate_clks,
			       ARRAY_SIZE(hi3670_media1_gate_clks),
			       clk_data);
	hisi_clk_register_mux(hi3670_media1_mux_clks,
			      ARRAY_SIZE(hi3670_media1_mux_clks),
			      clk_data);
	hisi_clk_register_divider(hi3670_media1_divider_clks,
				  ARRAY_SIZE(hi3670_media1_divider_clks),
				  clk_data);
}

static void hi3670_clk_media2_init(struct device_node *np)
{
	struct hisi_clock_data *clk_data;

	int nr = ARRAY_SIZE(hi3670_media2_gate_sep_clks);

	clk_data = hisi_clk_init(np, nr);
	if (!clk_data)
		return;

	hisi_clk_register_gate_sep(hi3670_media2_gate_sep_clks,
				   ARRAY_SIZE(hi3670_media2_gate_sep_clks),
				   clk_data);
}

static const struct of_device_id hi3670_clk_match_table[] = {
	{ .compatible = "hisilicon,hi3670-crgctrl",
	  .data = hi3670_clk_crgctrl_init },
	{ .compatible = "hisilicon,hi3670-pctrl",
	  .data = hi3670_clk_pctrl_init },
	{ .compatible = "hisilicon,hi3670-pmuctrl",
	  .data = hi3670_clk_pmuctrl_init },
	{ .compatible = "hisilicon,hi3670-sctrl",
	  .data = hi3670_clk_sctrl_init },
	{ .compatible = "hisilicon,hi3670-iomcu",
	  .data = hi3670_clk_iomcu_init },
	{ .compatible = "hisilicon,hi3670-media1-crg",
	  .data = hi3670_clk_media1_init },
	{ .compatible = "hisilicon,hi3670-media2-crg",
	  .data = hi3670_clk_media2_init },
	{ }
};

static int hi3670_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	void (*init_func)(struct device_node *np);

	init_func = of_device_get_match_data(dev);
	if (!init_func)
		return -ENODEV;

	init_func(np);

	return 0;
}

static struct platform_driver hi3670_clk_driver = {
	.probe          = hi3670_clk_probe,
	.driver         = {
		.name   = "hi3670-clk",
		.of_match_table = hi3670_clk_match_table,
	},
};

static int __init hi3670_clk_init(void)
{
	return platform_driver_register(&hi3670_clk_driver);
}
core_initcall(hi3670_clk_init);
