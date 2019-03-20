/*
 * Copyright (c) 2016-2017 Linaro Ltd.
 * Copyright (c) 2016-2017 HiSilicon Technologies Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <dt-bindings/clock/hi3660-clock.h>
#include <linux/clk-provider.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include "clk.h"

static const struct hisi_fixed_rate_clock hi3660_fixed_rate_clks[] = {
	{ HI3660_CLKIN_SYS, "clkin_sys", NULL, 0, 19200000, },
	{ HI3660_CLKIN_REF, "clkin_ref", NULL, 0, 32764, },
	{ HI3660_CLK_FLL_SRC, "clk_fll_src", NULL, 0, 128000000, },
	{ HI3660_CLK_PPLL0, "clk_ppll0", NULL, 0, 1600000000, },
	{ HI3660_CLK_PPLL1, "clk_ppll1", NULL, 0, 1866000000, },
	{ HI3660_CLK_PPLL2, "clk_ppll2", NULL, 0, 2880000000UL, },
	{ HI3660_CLK_PPLL3, "clk_ppll3", NULL, 0, 1290000000, },
	{ HI3660_CLK_SCPLL, "clk_scpll", NULL, 0, 245760000, },
	{ HI3660_PCLK, "pclk", NULL, 0, 20000000, },
	{ HI3660_CLK_UART0_DBG, "clk_uart0_dbg", NULL, 0, 19200000, },
	{ HI3660_CLK_UART6, "clk_uart6", NULL, 0, 19200000, },
	{ HI3660_OSC32K, "osc32k", NULL, 0, 32764, },
	{ HI3660_OSC19M, "osc19m", NULL, 0, 19200000, },
	{ HI3660_CLK_480M, "clk_480m", NULL, 0, 480000000, },
	{ HI3660_CLK_INV, "clk_inv", NULL, 0, 10000000, },
};

/* crgctrl */
static const struct hisi_fixed_factor_clock hi3660_crg_fixed_factor_clks[] = {
	{ HI3660_FACTOR_UART3, "clk_factor_uart3", "iomcu_peri0", 1, 16, 0, },
	{ HI3660_CLK_FACTOR_MMC, "clk_factor_mmc", "clkin_sys", 1, 6, 0, },
	{ HI3660_CLK_GATE_I2C0, "clk_gate_i2c0", "clk_i2c0_iomcu", 1, 4, 0, },
	{ HI3660_CLK_GATE_I2C1, "clk_gate_i2c1", "clk_i2c1_iomcu", 1, 4, 0, },
	{ HI3660_CLK_GATE_I2C2, "clk_gate_i2c2", "clk_i2c2_iomcu", 1, 4, 0, },
	{ HI3660_CLK_GATE_I2C6, "clk_gate_i2c6", "clk_i2c6_iomcu", 1, 4, 0, },
	{ HI3660_CLK_DIV_SYSBUS, "clk_div_sysbus", "clk_mux_sysbus", 1, 7, 0, },
	{ HI3660_CLK_DIV_320M, "clk_div_320m", "clk_320m_pll_gt", 1, 5, 0, },
	{ HI3660_CLK_DIV_A53, "clk_div_a53hpm", "clk_a53hpm_andgt", 1, 6, 0, },
	{ HI3660_CLK_GATE_SPI0, "clk_gate_spi0", "clk_ppll0", 1, 8, 0, },
	{ HI3660_CLK_GATE_SPI2, "clk_gate_spi2", "clk_ppll0", 1, 8, 0, },
	{ HI3660_PCIEPHY_REF, "clk_pciephy_ref", "clk_div_pciephy", 1, 1, 0, },
	{ HI3660_CLK_ABB_USB, "clk_abb_usb", "clk_gate_usb_tcxo_en", 1, 1, 0 },
	{ HI3660_VENC_VOLT_HOLD, "venc_volt_hold", "peri_volt_hold", 1, 1, 0, },
	{ HI3660_CLK_FAC_ISP_SNCLK, "clk_isp_snclk_fac", "clk_isp_snclk_angt",
	  1, 10, 0, },
};

static const struct hisi_gate_clock hi3660_crgctrl_gate_sep_clks[] = {
	{ HI3660_PERI_VOLT_HOLD, "peri_volt_hold", "clkin_sys",
	  CLK_SET_RATE_PARENT, 0x0, 0, 0, },
	{ HI3660_HCLK_GATE_SDIO0, "hclk_gate_sdio0", "clk_div_sysbus",
	  CLK_SET_RATE_PARENT, 0x0, 21, 0, },
	{ HI3660_HCLK_GATE_SD, "hclk_gate_sd", "clk_div_sysbus",
	  CLK_SET_RATE_PARENT, 0x0, 30, 0, },
	{ HI3660_CLK_GATE_AOMM, "clk_gate_aomm", "clk_div_aomm",
	  CLK_SET_RATE_PARENT, 0x0, 31, 0, },
	{ HI3660_PCLK_GPIO0, "pclk_gpio0", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 0, 0, },
	{ HI3660_PCLK_GPIO1, "pclk_gpio1", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 1, 0, },
	{ HI3660_PCLK_GPIO2, "pclk_gpio2", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 2, 0, },
	{ HI3660_PCLK_GPIO3, "pclk_gpio3", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 3, 0, },
	{ HI3660_PCLK_GPIO4, "pclk_gpio4", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 4, 0, },
	{ HI3660_PCLK_GPIO5, "pclk_gpio5", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 5, 0, },
	{ HI3660_PCLK_GPIO6, "pclk_gpio6", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 6, 0, },
	{ HI3660_PCLK_GPIO7, "pclk_gpio7", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 7, 0, },
	{ HI3660_PCLK_GPIO8, "pclk_gpio8", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 8, 0, },
	{ HI3660_PCLK_GPIO9, "pclk_gpio9", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 9, 0, },
	{ HI3660_PCLK_GPIO10, "pclk_gpio10", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 10, 0, },
	{ HI3660_PCLK_GPIO11, "pclk_gpio11", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 11, 0, },
	{ HI3660_PCLK_GPIO12, "pclk_gpio12", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 12, 0, },
	{ HI3660_PCLK_GPIO13, "pclk_gpio13", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 13, 0, },
	{ HI3660_PCLK_GPIO14, "pclk_gpio14", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 14, 0, },
	{ HI3660_PCLK_GPIO15, "pclk_gpio15", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 15, 0, },
	{ HI3660_PCLK_GPIO16, "pclk_gpio16", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 16, 0, },
	{ HI3660_PCLK_GPIO17, "pclk_gpio17", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 17, 0, },
	{ HI3660_PCLK_GPIO18, "pclk_gpio18", "clk_div_ioperi",
	  CLK_SET_RATE_PARENT, 0x10, 18, 0, },
	{ HI3660_PCLK_GPIO19, "pclk_gpio19", "clk_div_ioperi",
	  CLK_SET_RATE_PARENT, 0x10, 19, 0, },
	{ HI3660_PCLK_GPIO20, "pclk_gpio20", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 20, 0, },
	{ HI3660_PCLK_GPIO21, "pclk_gpio21", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x10, 21, 0, },
	{ HI3660_CLK_GATE_SPI3, "clk_gate_spi3", "clk_div_ioperi",
	  CLK_SET_RATE_PARENT, 0x10, 30, 0, },
	{ HI3660_CLK_GATE_I2C7, "clk_gate_i2c7", "clk_mux_i2c",
	  CLK_SET_RATE_PARENT, 0x10, 31, 0, },
	{ HI3660_CLK_GATE_I2C3, "clk_gate_i2c3", "clk_mux_i2c",
	  CLK_SET_RATE_PARENT, 0x20, 7, 0, },
	{ HI3660_CLK_GATE_SPI1, "clk_gate_spi1", "clk_mux_spi",
	  CLK_SET_RATE_PARENT, 0x20, 9, 0, },
	{ HI3660_CLK_GATE_UART1, "clk_gate_uart1", "clk_mux_uarth",
	  CLK_SET_RATE_PARENT, 0x20, 11, 0, },
	{ HI3660_CLK_GATE_UART2, "clk_gate_uart2", "clk_mux_uart1",
	  CLK_SET_RATE_PARENT, 0x20, 12, 0, },
	{ HI3660_CLK_GATE_UART4, "clk_gate_uart4", "clk_mux_uarth",
	  CLK_SET_RATE_PARENT, 0x20, 14, 0, },
	{ HI3660_CLK_GATE_UART5, "clk_gate_uart5", "clk_mux_uart1",
	  CLK_SET_RATE_PARENT, 0x20, 15, 0, },
	{ HI3660_CLK_GATE_I2C4, "clk_gate_i2c4", "clk_mux_i2c",
	  CLK_SET_RATE_PARENT, 0x20, 27, 0, },
	{ HI3660_CLK_GATE_DMAC, "clk_gate_dmac", "clk_div_sysbus",
	  CLK_SET_RATE_PARENT, 0x30, 1, 0, },
	{ HI3660_CLK_GATE_VENC, "clk_gate_venc", "clk_div_venc",
	  CLK_SET_RATE_PARENT, 0x30, 10, 0, },
	{ HI3660_CLK_GATE_VDEC, "clk_gate_vdec", "clk_div_vdec",
	  CLK_SET_RATE_PARENT, 0x30, 11, 0, },
	{ HI3660_PCLK_GATE_DSS, "pclk_gate_dss", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x30, 12, 0, },
	{ HI3660_ACLK_GATE_DSS, "aclk_gate_dss", "clk_gate_vivobus",
	  CLK_SET_RATE_PARENT, 0x30, 13, 0, },
	{ HI3660_CLK_GATE_LDI1, "clk_gate_ldi1", "clk_div_ldi1",
	  CLK_SET_RATE_PARENT, 0x30, 14, 0, },
	{ HI3660_CLK_GATE_LDI0, "clk_gate_ldi0", "clk_div_ldi0",
	  CLK_SET_RATE_PARENT, 0x30, 15, 0, },
	{ HI3660_CLK_GATE_VIVOBUS, "clk_gate_vivobus", "clk_div_vivobus",
	  CLK_SET_RATE_PARENT, 0x30, 16, 0, },
	{ HI3660_CLK_GATE_EDC0, "clk_gate_edc0", "clk_div_edc0",
	  CLK_SET_RATE_PARENT, 0x30, 17, 0, },
	{ HI3660_CLK_GATE_TXDPHY0_CFG, "clk_gate_txdphy0_cfg", "clkin_sys",
	  CLK_SET_RATE_PARENT, 0x30, 28, 0, },
	{ HI3660_CLK_GATE_TXDPHY0_REF, "clk_gate_txdphy0_ref", "clkin_sys",
	  CLK_SET_RATE_PARENT, 0x30, 29, 0, },
	{ HI3660_CLK_GATE_TXDPHY1_CFG, "clk_gate_txdphy1_cfg", "clkin_sys",
	  CLK_SET_RATE_PARENT, 0x30, 30, 0, },
	{ HI3660_CLK_GATE_TXDPHY1_REF, "clk_gate_txdphy1_ref", "clkin_sys",
	  CLK_SET_RATE_PARENT, 0x30, 31, 0, },
	{ HI3660_ACLK_GATE_USB3OTG, "aclk_gate_usb3otg", "clk_div_mmc0bus",
	  CLK_SET_RATE_PARENT, 0x40, 1, 0, },
	{ HI3660_CLK_GATE_SPI4, "clk_gate_spi4", "clk_mux_spi",
	  CLK_SET_RATE_PARENT, 0x40, 4, 0, },
	{ HI3660_CLK_GATE_SD, "clk_gate_sd", "clk_mux_sd_sys",
	  CLK_SET_RATE_PARENT, 0x40, 17, 0, },
	{ HI3660_CLK_GATE_SDIO0, "clk_gate_sdio0", "clk_mux_sdio_sys",
	  CLK_SET_RATE_PARENT, 0x40, 19, 0, },
	{ HI3660_CLK_GATE_ISP_SNCLK0, "clk_gate_isp_snclk0",
	  "clk_isp_snclk_mux", CLK_SET_RATE_PARENT, 0x50, 16, 0, },
	{ HI3660_CLK_GATE_ISP_SNCLK1, "clk_gate_isp_snclk1",
	  "clk_isp_snclk_mux", CLK_SET_RATE_PARENT, 0x50, 17, 0, },
	{ HI3660_CLK_GATE_ISP_SNCLK2, "clk_gate_isp_snclk2",
	  "clk_isp_snclk_mux", CLK_SET_RATE_PARENT, 0x50, 18, 0, },
	/*
	 * clk_gate_ufs_subsys is a system bus clock, mark it as critical
	 * clock and keep it on for system suspend and resume.
	 */
	{ HI3660_CLK_GATE_UFS_SUBSYS, "clk_gate_ufs_subsys", "clk_div_sysbus",
	  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0x50, 21, 0, },
	{ HI3660_PCLK_GATE_DSI0, "pclk_gate_dsi0", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x50, 28, 0, },
	{ HI3660_PCLK_GATE_DSI1, "pclk_gate_dsi1", "clk_div_cfgbus",
	  CLK_SET_RATE_PARENT, 0x50, 29, 0, },
	{ HI3660_ACLK_GATE_PCIE, "aclk_gate_pcie", "clk_div_mmc1bus",
	  CLK_SET_RATE_PARENT, 0x420, 5, 0, },
	{ HI3660_PCLK_GATE_PCIE_SYS, "pclk_gate_pcie_sys", "clk_div_mmc1bus",
	  CLK_SET_RATE_PARENT, 0x420, 7, 0, },
	{ HI3660_CLK_GATE_PCIEAUX, "clk_gate_pcieaux", "clkin_sys",
	  CLK_SET_RATE_PARENT, 0x420, 8, 0, },
	{ HI3660_PCLK_GATE_PCIE_PHY, "pclk_gate_pcie_phy", "clk_div_mmc1bus",
	  CLK_SET_RATE_PARENT, 0x420, 9, 0, },
};

static const struct hisi_gate_clock hi3660_crgctrl_gate_clks[] = {
	{ HI3660_CLK_ANDGT_LDI0, "clk_andgt_ldi0", "clk_mux_ldi0",
	  CLK_SET_RATE_PARENT, 0xf0, 6, CLK_GATE_HIWORD_MASK, },
	{ HI3660_CLK_ANDGT_LDI1, "clk_andgt_ldi1", "clk_mux_ldi1",
	  CLK_SET_RATE_PARENT, 0xf0, 7, CLK_GATE_HIWORD_MASK, },
	{ HI3660_CLK_ANDGT_EDC0, "clk_andgt_edc0", "clk_mux_edc0",
	  CLK_SET_RATE_PARENT, 0xf0, 8, CLK_GATE_HIWORD_MASK, },
	{ HI3660_CLK_ANDGT_VDEC, "clk_andgt_vdec", "clk_mux_vdec",
	  CLK_SET_RATE_PARENT, 0xf0, 15, CLK_GATE_HIWORD_MASK, },
	{ HI3660_CLK_ANDGT_VENC, "clk_andgt_venc", "clk_mux_venc",
	  CLK_SET_RATE_PARENT, 0xf4, 0, CLK_GATE_HIWORD_MASK, },
	{ HI3660_CLK_GATE_UFSPHY_GT, "clk_gate_ufsphy_gt", "clk_div_ufsperi",
	  CLK_SET_RATE_PARENT, 0xf4, 1, CLK_GATE_HIWORD_MASK, },
	{ HI3660_CLK_ANDGT_MMC, "clk_andgt_mmc", "clk_mux_mmc_pll",
	  CLK_SET_RATE_PARENT, 0xf4, 2, CLK_GATE_HIWORD_MASK, },
	{ HI3660_CLK_ANDGT_SD, "clk_andgt_sd", "clk_mux_sd_pll",
	  CLK_SET_RATE_PARENT, 0xf4, 3, CLK_GATE_HIWORD_MASK, },
	{ HI3660_CLK_A53HPM_ANDGT, "clk_a53hpm_andgt", "clk_mux_a53hpm",
	  CLK_SET_RATE_PARENT, 0xf4, 7, CLK_GATE_HIWORD_MASK, },
	{ HI3660_CLK_ANDGT_SDIO, "clk_andgt_sdio", "clk_mux_sdio_pll",
	  CLK_SET_RATE_PARENT, 0xf4, 8, CLK_GATE_HIWORD_MASK, },
	{ HI3660_CLK_ANDGT_UART0, "clk_andgt_uart0", "clk_div_320m",
	  CLK_SET_RATE_PARENT, 0xf4, 9, CLK_GATE_HIWORD_MASK, },
	{ HI3660_CLK_ANDGT_UART1, "clk_andgt_uart1", "clk_div_320m",
	  CLK_SET_RATE_PARENT, 0xf4, 10, CLK_GATE_HIWORD_MASK, },
	{ HI3660_CLK_ANDGT_UARTH, "clk_andgt_uarth", "clk_div_320m",
	  CLK_SET_RATE_PARENT, 0xf4, 11, CLK_GATE_HIWORD_MASK, },
	{ HI3660_CLK_ANDGT_SPI, "clk_andgt_spi", "clk_div_320m",
	  CLK_SET_RATE_PARENT, 0xf4, 13, CLK_GATE_HIWORD_MASK, },
	{ HI3660_CLK_VIVOBUS_ANDGT, "clk_vivobus_andgt", "clk_mux_vivobus",
	  CLK_SET_RATE_PARENT, 0xf8, 1, CLK_GATE_HIWORD_MASK, },
	{ HI3660_CLK_AOMM_ANDGT, "clk_aomm_andgt", "clk_ppll2",
	  CLK_SET_RATE_PARENT, 0xf8, 3, CLK_GATE_HIWORD_MASK, },
	{ HI3660_CLK_320M_PLL_GT, "clk_320m_pll_gt", "clk_mux_320m",
	  CLK_SET_RATE_PARENT, 0xf8, 10, 0, },
	{ HI3660_CLK_ANGT_ISP_SNCLK, "clk_isp_snclk_angt", "clk_div_a53hpm",
	  CLK_SET_RATE_PARENT, 0x108, 2, CLK_GATE_HIWORD_MASK, },
	{ HI3660_AUTODIV_EMMC0BUS, "autodiv_emmc0bus", "autodiv_sysbus",
	  CLK_SET_RATE_PARENT, 0x404, 1, CLK_GATE_HIWORD_MASK, },
	{ HI3660_AUTODIV_SYSBUS, "autodiv_sysbus", "clk_div_sysbus",
	  CLK_SET_RATE_PARENT, 0x404, 5, CLK_GATE_HIWORD_MASK, },
	{ HI3660_CLK_GATE_UFSPHY_CFG, "clk_gate_ufsphy_cfg",
	  "clk_div_ufsphy_cfg", CLK_SET_RATE_PARENT, 0x420, 12, 0, },
	{ HI3660_CLK_GATE_UFSIO_REF, "clk_gate_ufsio_ref",
	  "clk_gate_ufs_tcxo_en", CLK_SET_RATE_PARENT, 0x420, 14, 0, },
};

static const char *const
clk_mux_sysbus_p[] = {"clk_ppll1", "clk_ppll0"};
static const char *const
clk_mux_sdio_sys_p[] = {"clk_factor_mmc", "clk_div_sdio",};
static const char *const
clk_mux_sd_sys_p[] = {"clk_factor_mmc", "clk_div_sd",};
static const char *const
clk_mux_pll_p[] = {"clk_ppll0", "clk_ppll1", "clk_ppll2", "clk_ppll2",};
static const char *const
clk_mux_pll0123_p[] = {"clk_ppll0", "clk_ppll1", "clk_ppll2", "clk_ppll3",};
static const char *const
clk_mux_edc0_p[] = {"clk_inv", "clk_ppll0", "clk_ppll1", "clk_inv",
		    "clk_ppll2", "clk_inv", "clk_inv", "clk_inv",
		    "clk_ppll3", "clk_inv", "clk_inv", "clk_inv",
		    "clk_inv", "clk_inv", "clk_inv", "clk_inv",};
static const char *const
clk_mux_ldi0_p[] = {"clk_inv", "clk_ppll0", "clk_ppll2", "clk_inv",
		    "clk_ppll1", "clk_inv", "clk_inv", "clk_inv",
		    "clk_ppll3", "clk_inv", "clk_inv", "clk_inv",
		    "clk_inv", "clk_inv", "clk_inv", "clk_inv",};
static const char *const
clk_mux_uart0_p[] = {"clkin_sys", "clk_div_uart0",};
static const char *const
clk_mux_uart1_p[] = {"clkin_sys", "clk_div_uart1",};
static const char *const
clk_mux_uarth_p[] = {"clkin_sys", "clk_div_uarth",};
static const char *const
clk_mux_pll02p[] = {"clk_ppll0", "clk_ppll2",};
static const char *const
clk_mux_ioperi_p[] = {"clk_div_320m", "clk_div_a53hpm",};
static const char *const
clk_mux_spi_p[] = {"clkin_sys", "clk_div_spi",};
static const char *const
clk_mux_i2c_p[] = {"clkin_sys", "clk_div_i2c",};
static const char *const
clk_mux_venc_p[] = {"clk_ppll0", "clk_ppll1", "clk_ppll3", "clk_ppll3",};
static const char *const
clk_mux_isp_snclk_p[] = {"clkin_sys", "clk_isp_snclk_div"};

static const struct hisi_mux_clock hi3660_crgctrl_mux_clks[] = {
	{ HI3660_CLK_MUX_SYSBUS, "clk_mux_sysbus", clk_mux_sysbus_p,
	  ARRAY_SIZE(clk_mux_sysbus_p), CLK_SET_RATE_PARENT, 0xac, 0, 1,
	  CLK_MUX_HIWORD_MASK, },
	{ HI3660_CLK_MUX_UART0, "clk_mux_uart0", clk_mux_uart0_p,
	  ARRAY_SIZE(clk_mux_uart0_p), CLK_SET_RATE_PARENT, 0xac, 2, 1,
	  CLK_MUX_HIWORD_MASK, },
	{ HI3660_CLK_MUX_UART1, "clk_mux_uart1", clk_mux_uart1_p,
	  ARRAY_SIZE(clk_mux_uart1_p), CLK_SET_RATE_PARENT, 0xac, 3, 1,
	  CLK_MUX_HIWORD_MASK, },
	{ HI3660_CLK_MUX_UARTH, "clk_mux_uarth", clk_mux_uarth_p,
	  ARRAY_SIZE(clk_mux_uarth_p), CLK_SET_RATE_PARENT, 0xac, 4, 1,
	  CLK_MUX_HIWORD_MASK, },
	{ HI3660_CLK_MUX_SPI, "clk_mux_spi", clk_mux_spi_p,
	  ARRAY_SIZE(clk_mux_spi_p), CLK_SET_RATE_PARENT, 0xac, 8, 1,
	  CLK_MUX_HIWORD_MASK, },
	{ HI3660_CLK_MUX_I2C, "clk_mux_i2c", clk_mux_i2c_p,
	  ARRAY_SIZE(clk_mux_i2c_p), CLK_SET_RATE_PARENT, 0xac, 13, 1,
	  CLK_MUX_HIWORD_MASK, },
	{ HI3660_CLK_MUX_MMC_PLL, "clk_mux_mmc_pll", clk_mux_pll02p,
	  ARRAY_SIZE(clk_mux_pll02p), CLK_SET_RATE_PARENT, 0xb4, 0, 1,
	  CLK_MUX_HIWORD_MASK, },
	{ HI3660_CLK_MUX_LDI1, "clk_mux_ldi1", clk_mux_ldi0_p,
	  ARRAY_SIZE(clk_mux_ldi0_p), CLK_SET_RATE_PARENT, 0xb4, 8, 4,
	  CLK_MUX_HIWORD_MASK, },
	{ HI3660_CLK_MUX_LDI0, "clk_mux_ldi0", clk_mux_ldi0_p,
	  ARRAY_SIZE(clk_mux_ldi0_p), CLK_SET_RATE_PARENT, 0xb4, 12, 4,
	  CLK_MUX_HIWORD_MASK, },
	{ HI3660_CLK_MUX_SD_PLL, "clk_mux_sd_pll", clk_mux_pll_p,
	  ARRAY_SIZE(clk_mux_pll_p), CLK_SET_RATE_PARENT, 0xb8, 4, 2,
	  CLK_MUX_HIWORD_MASK, },
	{ HI3660_CLK_MUX_SD_SYS, "clk_mux_sd_sys", clk_mux_sd_sys_p,
	  ARRAY_SIZE(clk_mux_sd_sys_p), CLK_SET_RATE_PARENT, 0xb8, 6, 1,
	  CLK_MUX_HIWORD_MASK, },
	{ HI3660_CLK_MUX_EDC0, "clk_mux_edc0", clk_mux_edc0_p,
	  ARRAY_SIZE(clk_mux_edc0_p), CLK_SET_RATE_PARENT, 0xbc, 6, 4,
	  CLK_MUX_HIWORD_MASK, },
	{ HI3660_CLK_MUX_SDIO_SYS, "clk_mux_sdio_sys", clk_mux_sdio_sys_p,
	  ARRAY_SIZE(clk_mux_sdio_sys_p), CLK_SET_RATE_PARENT, 0xc0, 6, 1,
	  CLK_MUX_HIWORD_MASK, },
	{ HI3660_CLK_MUX_SDIO_PLL, "clk_mux_sdio_pll", clk_mux_pll_p,
	  ARRAY_SIZE(clk_mux_pll_p), CLK_SET_RATE_PARENT, 0xc0, 4, 2,
	  CLK_MUX_HIWORD_MASK, },
	{ HI3660_CLK_MUX_VENC, "clk_mux_venc", clk_mux_venc_p,
	  ARRAY_SIZE(clk_mux_venc_p), CLK_SET_RATE_PARENT, 0xc8, 11, 2,
	  CLK_MUX_HIWORD_MASK, },
	{ HI3660_CLK_MUX_VDEC, "clk_mux_vdec", clk_mux_pll0123_p,
	  ARRAY_SIZE(clk_mux_pll0123_p), CLK_SET_RATE_PARENT, 0xcc, 5, 2,
	  CLK_MUX_HIWORD_MASK, },
	{ HI3660_CLK_MUX_VIVOBUS, "clk_mux_vivobus", clk_mux_pll0123_p,
	  ARRAY_SIZE(clk_mux_pll0123_p), CLK_SET_RATE_PARENT, 0xd0, 12, 2,
	  CLK_MUX_HIWORD_MASK, },
	{ HI3660_CLK_MUX_A53HPM, "clk_mux_a53hpm", clk_mux_pll02p,
	  ARRAY_SIZE(clk_mux_pll02p), CLK_SET_RATE_PARENT, 0xd4, 9, 1,
	  CLK_MUX_HIWORD_MASK, },
	{ HI3660_CLK_MUX_320M, "clk_mux_320m", clk_mux_pll02p,
	  ARRAY_SIZE(clk_mux_pll02p), CLK_SET_RATE_PARENT, 0x100, 0, 1,
	  CLK_MUX_HIWORD_MASK, },
	{ HI3660_CLK_MUX_ISP_SNCLK, "clk_isp_snclk_mux", clk_mux_isp_snclk_p,
	  ARRAY_SIZE(clk_mux_isp_snclk_p), CLK_SET_RATE_PARENT, 0x108, 3, 1,
	  CLK_MUX_HIWORD_MASK, },
	{ HI3660_CLK_MUX_IOPERI, "clk_mux_ioperi", clk_mux_ioperi_p,
	  ARRAY_SIZE(clk_mux_ioperi_p), CLK_SET_RATE_PARENT, 0x108, 10, 1,
	  CLK_MUX_HIWORD_MASK, },
};

static const struct hisi_divider_clock hi3660_crgctrl_divider_clks[] = {
	{ HI3660_CLK_DIV_UART0, "clk_div_uart0", "clk_andgt_uart0",
	  CLK_SET_RATE_PARENT, 0xb0, 4, 4, CLK_DIVIDER_HIWORD_MASK, 0, },
	{ HI3660_CLK_DIV_UART1, "clk_div_uart1", "clk_andgt_uart1",
	  CLK_SET_RATE_PARENT, 0xb0, 8, 4, CLK_DIVIDER_HIWORD_MASK, 0, },
	{ HI3660_CLK_DIV_UARTH, "clk_div_uarth", "clk_andgt_uarth",
	  CLK_SET_RATE_PARENT, 0xb0, 12, 4, CLK_DIVIDER_HIWORD_MASK, 0, },
	{ HI3660_CLK_DIV_MMC, "clk_div_mmc", "clk_andgt_mmc",
	  CLK_SET_RATE_PARENT, 0xb4, 3, 4, CLK_DIVIDER_HIWORD_MASK, 0, },
	{ HI3660_CLK_DIV_SD, "clk_div_sd", "clk_andgt_sd",
	  CLK_SET_RATE_PARENT, 0xb8, 0, 4, CLK_DIVIDER_HIWORD_MASK, 0, },
	{ HI3660_CLK_DIV_EDC0, "clk_div_edc0", "clk_andgt_edc0",
	  CLK_SET_RATE_PARENT, 0xbc, 0, 6, CLK_DIVIDER_HIWORD_MASK, 0, },
	{ HI3660_CLK_DIV_LDI0, "clk_div_ldi0", "clk_andgt_ldi0",
	  CLK_SET_RATE_PARENT, 0xbc, 10, 6, CLK_DIVIDER_HIWORD_MASK, 0, },
	{ HI3660_CLK_DIV_SDIO, "clk_div_sdio", "clk_andgt_sdio",
	  CLK_SET_RATE_PARENT, 0xc0, 0, 4, CLK_DIVIDER_HIWORD_MASK, 0, },
	{ HI3660_CLK_DIV_LDI1, "clk_div_ldi1", "clk_andgt_ldi1",
	  CLK_SET_RATE_PARENT, 0xc0, 8, 6, CLK_DIVIDER_HIWORD_MASK, 0, },
	{ HI3660_CLK_DIV_SPI, "clk_div_spi", "clk_andgt_spi",
	  CLK_SET_RATE_PARENT, 0xc4, 12, 4, CLK_DIVIDER_HIWORD_MASK, 0, },
	{ HI3660_CLK_DIV_VENC, "clk_div_venc", "clk_andgt_venc",
	  CLK_SET_RATE_PARENT, 0xc8, 6, 5, CLK_DIVIDER_HIWORD_MASK, 0, },
	{ HI3660_CLK_DIV_VDEC, "clk_div_vdec", "clk_andgt_vdec",
	  CLK_SET_RATE_PARENT, 0xcc, 0, 5, CLK_DIVIDER_HIWORD_MASK, 0, },
	{ HI3660_CLK_DIV_VIVOBUS, "clk_div_vivobus", "clk_vivobus_andgt",
	  CLK_SET_RATE_PARENT, 0xd0, 7, 5, CLK_DIVIDER_HIWORD_MASK, 0, },
	{ HI3660_CLK_DIV_I2C, "clk_div_i2c", "clk_div_320m",
	  CLK_SET_RATE_PARENT, 0xe8, 4, 4, CLK_DIVIDER_HIWORD_MASK, 0, },
	{ HI3660_CLK_DIV_UFSPHY, "clk_div_ufsphy_cfg", "clk_gate_ufsphy_gt",
	  CLK_SET_RATE_PARENT, 0xe8, 9, 2, CLK_DIVIDER_HIWORD_MASK, 0, },
	{ HI3660_CLK_DIV_CFGBUS, "clk_div_cfgbus", "clk_div_sysbus",
	  CLK_SET_RATE_PARENT, 0xec, 0, 2, CLK_DIVIDER_HIWORD_MASK, 0, },
	{ HI3660_CLK_DIV_MMC0BUS, "clk_div_mmc0bus", "autodiv_emmc0bus",
	  CLK_SET_RATE_PARENT, 0xec, 2, 1, CLK_DIVIDER_HIWORD_MASK, 0, },
	{ HI3660_CLK_DIV_MMC1BUS, "clk_div_mmc1bus", "clk_div_sysbus",
	  CLK_SET_RATE_PARENT, 0xec, 3, 1, CLK_DIVIDER_HIWORD_MASK, 0, },
	{ HI3660_CLK_DIV_UFSPERI, "clk_div_ufsperi", "clk_gate_ufs_subsys",
	  CLK_SET_RATE_PARENT, 0xec, 14, 1, CLK_DIVIDER_HIWORD_MASK, 0, },
	{ HI3660_CLK_DIV_AOMM, "clk_div_aomm", "clk_aomm_andgt",
	  CLK_SET_RATE_PARENT, 0x100, 7, 4, CLK_DIVIDER_HIWORD_MASK, 0, },
	{ HI3660_CLK_DIV_ISP_SNCLK, "clk_isp_snclk_div", "clk_isp_snclk_fac",
	  CLK_SET_RATE_PARENT, 0x108, 0, 2, CLK_DIVIDER_HIWORD_MASK, 0, },
	{ HI3660_CLK_DIV_IOPERI, "clk_div_ioperi", "clk_mux_ioperi",
	  CLK_SET_RATE_PARENT, 0x108, 11, 4, CLK_DIVIDER_HIWORD_MASK, 0, },
};

/* clk_pmuctrl */
/* pmu register need shift 2 bits */
static const struct hisi_gate_clock hi3660_pmu_gate_clks[] = {
	{ HI3660_GATE_ABB_192, "clk_gate_abb_192", "clkin_sys",
	  CLK_SET_RATE_PARENT, (0x10a << 2), 3, 0, },
};

/* clk_pctrl */
static const struct hisi_gate_clock hi3660_pctrl_gate_clks[] = {
	{ HI3660_GATE_UFS_TCXO_EN, "clk_gate_ufs_tcxo_en",
	  "clk_gate_abb_192", CLK_SET_RATE_PARENT, 0x10, 0,
	  CLK_GATE_HIWORD_MASK, },
	{ HI3660_GATE_USB_TCXO_EN, "clk_gate_usb_tcxo_en", "clk_gate_abb_192",
	  CLK_SET_RATE_PARENT, 0x10, 1, CLK_GATE_HIWORD_MASK, },
};

/* clk_sctrl */
static const struct hisi_gate_clock hi3660_sctrl_gate_sep_clks[] = {
	{ HI3660_PCLK_AO_GPIO0, "pclk_ao_gpio0", "clk_div_aobus",
	  CLK_SET_RATE_PARENT, 0x160, 11, 0, },
	{ HI3660_PCLK_AO_GPIO1, "pclk_ao_gpio1", "clk_div_aobus",
	  CLK_SET_RATE_PARENT, 0x160, 12, 0, },
	{ HI3660_PCLK_AO_GPIO2, "pclk_ao_gpio2", "clk_div_aobus",
	  CLK_SET_RATE_PARENT, 0x160, 13, 0, },
	{ HI3660_PCLK_AO_GPIO3, "pclk_ao_gpio3", "clk_div_aobus",
	  CLK_SET_RATE_PARENT, 0x160, 14, 0, },
	{ HI3660_PCLK_AO_GPIO4, "pclk_ao_gpio4", "clk_div_aobus",
	  CLK_SET_RATE_PARENT, 0x160, 21, 0, },
	{ HI3660_PCLK_AO_GPIO5, "pclk_ao_gpio5", "clk_div_aobus",
	  CLK_SET_RATE_PARENT, 0x160, 22, 0, },
	{ HI3660_PCLK_AO_GPIO6, "pclk_ao_gpio6", "clk_div_aobus",
	  CLK_SET_RATE_PARENT, 0x160, 25, 0, },
	{ HI3660_PCLK_GATE_MMBUF, "pclk_gate_mmbuf", "pclk_div_mmbuf",
	  CLK_SET_RATE_PARENT, 0x170, 23, 0, },
	{ HI3660_CLK_GATE_DSS_AXI_MM, "clk_gate_dss_axi_mm", "aclk_mux_mmbuf",
	  CLK_SET_RATE_PARENT, 0x170, 24, 0, },
};

static const struct hisi_gate_clock hi3660_sctrl_gate_clks[] = {
	{ HI3660_PCLK_MMBUF_ANDGT, "pclk_mmbuf_andgt", "clk_sw_mmbuf",
	  CLK_SET_RATE_PARENT, 0x258, 7, CLK_GATE_HIWORD_MASK, },
	{ HI3660_CLK_MMBUF_PLL_ANDGT, "clk_mmbuf_pll_andgt", "clk_ppll0",
	  CLK_SET_RATE_PARENT, 0x260, 11, CLK_DIVIDER_HIWORD_MASK, 0, },
	{ HI3660_CLK_FLL_MMBUF_ANDGT, "clk_fll_mmbuf_andgt", "clk_fll_src",
	  CLK_SET_RATE_PARENT, 0x260, 12, CLK_DIVIDER_HIWORD_MASK, 0, },
	{ HI3660_CLK_SYS_MMBUF_ANDGT, "clk_sys_mmbuf_andgt", "clkin_sys",
	  CLK_SET_RATE_PARENT, 0x260, 13, CLK_DIVIDER_HIWORD_MASK, 0, },
	{ HI3660_CLK_GATE_PCIEPHY_GT, "clk_gate_pciephy_gt", "clk_ppll0",
	  CLK_SET_RATE_PARENT, 0x268, 11, CLK_DIVIDER_HIWORD_MASK, 0, },
};

static const char *const
aclk_mux_mmbuf_p[] = {"aclk_div_mmbuf", "clk_gate_aomm",};
static const char *const
clk_sw_mmbuf_p[] = {"clk_sys_mmbuf_andgt", "clk_fll_mmbuf_andgt",
		    "aclk_mux_mmbuf", "aclk_mux_mmbuf"};

static const struct hisi_mux_clock hi3660_sctrl_mux_clks[] = {
	{ HI3660_ACLK_MUX_MMBUF, "aclk_mux_mmbuf", aclk_mux_mmbuf_p,
	  ARRAY_SIZE(aclk_mux_mmbuf_p), CLK_SET_RATE_PARENT, 0x250, 12, 1,
	  CLK_MUX_HIWORD_MASK, },
	{ HI3660_CLK_SW_MMBUF, "clk_sw_mmbuf", clk_sw_mmbuf_p,
	  ARRAY_SIZE(clk_sw_mmbuf_p), CLK_SET_RATE_PARENT, 0x258, 8, 2,
	  CLK_MUX_HIWORD_MASK, },
};

static const struct hisi_divider_clock hi3660_sctrl_divider_clks[] = {
	{ HI3660_CLK_DIV_AOBUS, "clk_div_aobus", "clk_ppll0",
	  CLK_SET_RATE_PARENT, 0x254, 0, 6, CLK_DIVIDER_HIWORD_MASK, 0, },
	{ HI3660_PCLK_DIV_MMBUF, "pclk_div_mmbuf", "pclk_mmbuf_andgt",
	  CLK_SET_RATE_PARENT, 0x258, 10, 2, CLK_DIVIDER_HIWORD_MASK, 0, },
	{ HI3660_ACLK_DIV_MMBUF, "aclk_div_mmbuf", "clk_mmbuf_pll_andgt",
	  CLK_SET_RATE_PARENT, 0x258, 12, 4, CLK_DIVIDER_HIWORD_MASK, 0, },
	{ HI3660_CLK_DIV_PCIEPHY, "clk_div_pciephy", "clk_gate_pciephy_gt",
	  CLK_SET_RATE_PARENT, 0x268, 12, 4, CLK_DIVIDER_HIWORD_MASK, 0, },
};

/* clk_iomcu */
static const struct hisi_gate_clock hi3660_iomcu_gate_sep_clks[] = {
	{ HI3660_CLK_I2C0_IOMCU, "clk_i2c0_iomcu", "clk_fll_src",
	  CLK_SET_RATE_PARENT, 0x10, 3, 0, },
	{ HI3660_CLK_I2C1_IOMCU, "clk_i2c1_iomcu", "clk_fll_src",
	  CLK_SET_RATE_PARENT, 0x10, 4, 0, },
	{ HI3660_CLK_I2C2_IOMCU, "clk_i2c2_iomcu", "clk_fll_src",
	  CLK_SET_RATE_PARENT, 0x10, 5, 0, },
	{ HI3660_CLK_I2C6_IOMCU, "clk_i2c6_iomcu", "clk_fll_src",
	  CLK_SET_RATE_PARENT, 0x10, 27, 0, },
	{ HI3660_CLK_IOMCU_PERI0, "iomcu_peri0", "clk_ppll0",
	  CLK_SET_RATE_PARENT, 0x90, 0, 0, },
};

static struct hisi_clock_data *clk_crgctrl_data;

static void hi3660_clk_iomcu_init(struct device_node *np)
{
	struct hisi_clock_data *clk_data;
	int nr = ARRAY_SIZE(hi3660_iomcu_gate_sep_clks);

	clk_data = hisi_clk_init(np, nr);
	if (!clk_data)
		return;

	hisi_clk_register_gate_sep(hi3660_iomcu_gate_sep_clks,
				   ARRAY_SIZE(hi3660_iomcu_gate_sep_clks),
				   clk_data);
}

static void hi3660_clk_pmuctrl_init(struct device_node *np)
{
	struct hisi_clock_data *clk_data;
	int nr = ARRAY_SIZE(hi3660_pmu_gate_clks);

	clk_data = hisi_clk_init(np, nr);
	if (!clk_data)
		return;

	hisi_clk_register_gate(hi3660_pmu_gate_clks,
			       ARRAY_SIZE(hi3660_pmu_gate_clks), clk_data);
}

static void hi3660_clk_pctrl_init(struct device_node *np)
{
	struct hisi_clock_data *clk_data;
	int nr = ARRAY_SIZE(hi3660_pctrl_gate_clks);

	clk_data = hisi_clk_init(np, nr);
	if (!clk_data)
		return;
	hisi_clk_register_gate(hi3660_pctrl_gate_clks,
			       ARRAY_SIZE(hi3660_pctrl_gate_clks), clk_data);
}

static void hi3660_clk_sctrl_init(struct device_node *np)
{
	struct hisi_clock_data *clk_data;
	int nr = ARRAY_SIZE(hi3660_sctrl_gate_clks) +
		 ARRAY_SIZE(hi3660_sctrl_gate_sep_clks) +
		 ARRAY_SIZE(hi3660_sctrl_mux_clks) +
		 ARRAY_SIZE(hi3660_sctrl_divider_clks);

	clk_data = hisi_clk_init(np, nr);
	if (!clk_data)
		return;
	hisi_clk_register_gate(hi3660_sctrl_gate_clks,
			       ARRAY_SIZE(hi3660_sctrl_gate_clks), clk_data);
	hisi_clk_register_gate_sep(hi3660_sctrl_gate_sep_clks,
				   ARRAY_SIZE(hi3660_sctrl_gate_sep_clks),
				   clk_data);
	hisi_clk_register_mux(hi3660_sctrl_mux_clks,
			      ARRAY_SIZE(hi3660_sctrl_mux_clks), clk_data);
	hisi_clk_register_divider(hi3660_sctrl_divider_clks,
				  ARRAY_SIZE(hi3660_sctrl_divider_clks),
				  clk_data);
}

static void hi3660_clk_crgctrl_early_init(struct device_node *np)
{
	int nr = ARRAY_SIZE(hi3660_fixed_rate_clks) +
		 ARRAY_SIZE(hi3660_crgctrl_gate_sep_clks) +
		 ARRAY_SIZE(hi3660_crgctrl_gate_clks) +
		 ARRAY_SIZE(hi3660_crgctrl_mux_clks) +
		 ARRAY_SIZE(hi3660_crg_fixed_factor_clks) +
		 ARRAY_SIZE(hi3660_crgctrl_divider_clks);
	int i;

	clk_crgctrl_data = hisi_clk_init(np, nr);
	if (!clk_crgctrl_data)
		return;

	for (i = 0; i < nr; i++)
		clk_crgctrl_data->clk_data.clks[i] = ERR_PTR(-EPROBE_DEFER);

	hisi_clk_register_fixed_rate(hi3660_fixed_rate_clks,
				     ARRAY_SIZE(hi3660_fixed_rate_clks),
				     clk_crgctrl_data);
}
CLK_OF_DECLARE_DRIVER(hi3660_clk_crgctrl, "hisilicon,hi3660-crgctrl",
		      hi3660_clk_crgctrl_early_init);

static void hi3660_clk_crgctrl_init(struct device_node *np)
{
	struct clk **clks;
	int i;

	if (!clk_crgctrl_data)
		hi3660_clk_crgctrl_early_init(np);

	/* clk_crgctrl_data initialization failed */
	if (!clk_crgctrl_data)
		return;

	hisi_clk_register_gate_sep(hi3660_crgctrl_gate_sep_clks,
				   ARRAY_SIZE(hi3660_crgctrl_gate_sep_clks),
				   clk_crgctrl_data);
	hisi_clk_register_gate(hi3660_crgctrl_gate_clks,
			       ARRAY_SIZE(hi3660_crgctrl_gate_clks),
			       clk_crgctrl_data);
	hisi_clk_register_mux(hi3660_crgctrl_mux_clks,
			      ARRAY_SIZE(hi3660_crgctrl_mux_clks),
			      clk_crgctrl_data);
	hisi_clk_register_fixed_factor(hi3660_crg_fixed_factor_clks,
				       ARRAY_SIZE(hi3660_crg_fixed_factor_clks),
				       clk_crgctrl_data);
	hisi_clk_register_divider(hi3660_crgctrl_divider_clks,
				  ARRAY_SIZE(hi3660_crgctrl_divider_clks),
				  clk_crgctrl_data);

	clks = clk_crgctrl_data->clk_data.clks;
	for (i = 0; i < clk_crgctrl_data->clk_data.clk_num; i++) {
		if (IS_ERR(clks[i]) && PTR_ERR(clks[i]) != -EPROBE_DEFER)
			pr_err("Failed to register crgctrl clock[%d] err=%ld\n",
			       i, PTR_ERR(clks[i]));
	}
}

static const struct of_device_id hi3660_clk_match_table[] = {
	{ .compatible = "hisilicon,hi3660-crgctrl",
	  .data = hi3660_clk_crgctrl_init },
	{ .compatible = "hisilicon,hi3660-pctrl",
	  .data = hi3660_clk_pctrl_init },
	{ .compatible = "hisilicon,hi3660-pmuctrl",
	  .data = hi3660_clk_pmuctrl_init },
	{ .compatible = "hisilicon,hi3660-sctrl",
	  .data = hi3660_clk_sctrl_init },
	{ .compatible = "hisilicon,hi3660-iomcu",
	  .data = hi3660_clk_iomcu_init },
	{ }
};

static int hi3660_clk_probe(struct platform_device *pdev)
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

static struct platform_driver hi3660_clk_driver = {
	.probe          = hi3660_clk_probe,
	.driver         = {
		.name   = "hi3660-clk",
		.of_match_table = hi3660_clk_match_table,
	},
};

static int __init hi3660_clk_init(void)
{
	return platform_driver_register(&hi3660_clk_driver);
}
core_initcall(hi3660_clk_init);
