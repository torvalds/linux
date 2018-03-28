/*
 * Copyright (c) 2017 Rockchip Electronics Co. Ltd.
 * Author: Finley Xiao <finley.xiao@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/syscore_ops.h>
#include <dt-bindings/clock/rk3308-cru.h>
#include "clk.h"

#define RK3308_GRF_SOC_STATUS0		0x380

enum rk3308_plls {
	apll, dpll, vpll0, vpll1,
};

static struct rockchip_pll_rate_table rk3308_pll_rates[] = {
	/* _mhz, _refdiv, _fbdiv, _postdiv1, _postdiv2, _dsmpd, _frac */
	RK3036_PLL_RATE(1608000000, 1, 67, 1, 1, 1, 0),
	RK3036_PLL_RATE(1584000000, 1, 66, 1, 1, 1, 0),
	RK3036_PLL_RATE(1560000000, 1, 65, 1, 1, 1, 0),
	RK3036_PLL_RATE(1536000000, 1, 64, 1, 1, 1, 0),
	RK3036_PLL_RATE(1512000000, 1, 63, 1, 1, 1, 0),
	RK3036_PLL_RATE(1488000000, 1, 62, 1, 1, 1, 0),
	RK3036_PLL_RATE(1464000000, 1, 61, 1, 1, 1, 0),
	RK3036_PLL_RATE(1440000000, 1, 60, 1, 1, 1, 0),
	RK3036_PLL_RATE(1416000000, 1, 59, 1, 1, 1, 0),
	RK3036_PLL_RATE(1392000000, 1, 58, 1, 1, 1, 0),
	RK3036_PLL_RATE(1368000000, 1, 57, 1, 1, 1, 0),
	RK3036_PLL_RATE(1344000000, 1, 56, 1, 1, 1, 0),
	RK3036_PLL_RATE(1320000000, 1, 55, 1, 1, 1, 0),
	RK3036_PLL_RATE(1296000000, 1, 54, 1, 1, 1, 0),
	RK3036_PLL_RATE(1272000000, 1, 53, 1, 1, 1, 0),
	RK3036_PLL_RATE(1248000000, 1, 52, 1, 1, 1, 0),
	RK3036_PLL_RATE(1200000000, 1, 50, 1, 1, 1, 0),
	RK3036_PLL_RATE(1188000000, 2, 99, 1, 1, 1, 0),
	RK3036_PLL_RATE(1104000000, 1, 46, 1, 1, 1, 0),
	RK3036_PLL_RATE(1100000000, 12, 550, 1, 1, 1, 0),
	RK3036_PLL_RATE(1008000000, 1, 84, 2, 1, 1, 0),
	RK3036_PLL_RATE(1000000000, 6, 500, 2, 1, 1, 0),
	RK3036_PLL_RATE(984000000, 1, 82, 2, 1, 1, 0),
	RK3036_PLL_RATE(960000000, 1, 80, 2, 1, 1, 0),
	RK3036_PLL_RATE(936000000, 1, 78, 2, 1, 1, 0),
	RK3036_PLL_RATE(912000000, 1, 76, 2, 1, 1, 0),
	RK3036_PLL_RATE(900000000, 4, 300, 2, 1, 1, 0),
	RK3036_PLL_RATE(888000000, 1, 74, 2, 1, 1, 0),
	RK3036_PLL_RATE(864000000, 1, 72, 2, 1, 1, 0),
	RK3036_PLL_RATE(840000000, 1, 70, 2, 1, 1, 0),
	RK3036_PLL_RATE(816000000, 1, 68, 2, 1, 1, 0),
	RK3036_PLL_RATE(800000000, 6, 400, 2, 1, 1, 0),
	RK3036_PLL_RATE(700000000, 6, 350, 2, 1, 1, 0),
	RK3036_PLL_RATE(696000000, 1, 58, 2, 1, 1, 0),
	RK3036_PLL_RATE(624000000, 1, 52, 2, 1, 1, 0),
	RK3036_PLL_RATE(600000000, 1, 75, 3, 1, 1, 0),
	RK3036_PLL_RATE(594000000, 2, 99, 2, 1, 1, 0),
	RK3036_PLL_RATE(504000000, 1, 63, 3, 1, 1, 0),
	RK3036_PLL_RATE(500000000, 6, 250, 2, 1, 1, 0),
	RK3036_PLL_RATE(408000000, 1, 68, 2, 2, 1, 0),
	RK3036_PLL_RATE(312000000, 1, 52, 2, 2, 1, 0),
	RK3036_PLL_RATE(216000000, 1, 72, 4, 2, 1, 0),
	RK3036_PLL_RATE(96000000, 1, 64, 4, 4, 1, 0),
	{ /* sentinel */ },
};

#define RK3308_DIV_ACLKM_MASK		0x7
#define RK3308_DIV_ACLKM_SHIFT		12
#define RK3308_DIV_PCLK_DBG_MASK	0xf
#define RK3308_DIV_PCLK_DBG_SHIFT	8

#define RK3308_CLKSEL0(_aclk_core, _pclk_dbg)				\
{									\
	.reg = RK3308_CLKSEL_CON(0),					\
	.val = HIWORD_UPDATE(_aclk_core, RK3308_DIV_ACLKM_MASK,		\
			     RK3308_DIV_ACLKM_SHIFT) |			\
	       HIWORD_UPDATE(_pclk_dbg, RK3308_DIV_PCLK_DBG_MASK,	\
			     RK3308_DIV_PCLK_DBG_SHIFT),		\
}

#define RK3308_CPUCLK_RATE(_prate, _aclk_core, _pclk_dbg)		\
{									\
	.prate = _prate,						\
	.divs = {							\
		RK3308_CLKSEL0(_aclk_core, _pclk_dbg),			\
	},								\
}

static struct rockchip_cpuclk_rate_table rk3308_cpuclk_rates[] __initdata = {
	RK3308_CPUCLK_RATE(1608000000, 1, 7),
	RK3308_CPUCLK_RATE(1512000000, 1, 7),
	RK3308_CPUCLK_RATE(1488000000, 1, 5),
	RK3308_CPUCLK_RATE(1416000000, 1, 5),
	RK3308_CPUCLK_RATE(1392000000, 1, 5),
	RK3308_CPUCLK_RATE(1296000000, 1, 5),
	RK3308_CPUCLK_RATE(1200000000, 1, 5),
	RK3308_CPUCLK_RATE(1104000000, 1, 5),
	RK3308_CPUCLK_RATE(1008000000, 1, 5),
	RK3308_CPUCLK_RATE(912000000, 1, 5),
	RK3308_CPUCLK_RATE(816000000, 1, 3),
	RK3308_CPUCLK_RATE(696000000, 1, 3),
	RK3308_CPUCLK_RATE(600000000, 1, 3),
	RK3308_CPUCLK_RATE(408000000, 1, 1),
	RK3308_CPUCLK_RATE(312000000, 1, 1),
	RK3308_CPUCLK_RATE(216000000,  1, 1),
	RK3308_CPUCLK_RATE(96000000, 1, 1),
};

static const struct rockchip_cpuclk_reg_data rk3308_cpuclk_data = {
	.core_reg = RK3308_CLKSEL_CON(0),
	.div_core_shift = 0,
	.div_core_mask = 0xf,
	.mux_core_alt = 1,
	.mux_core_main = 0,
	.mux_core_shift = 6,
	.mux_core_mask = 0x3,
};

PNAME(mux_pll_p)		= { "xin24m" };
PNAME(mux_usb480m_p)		= { "xin24m", "usb480m_phy", "clk_rtc32k" };
PNAME(mux_armclk_p)		= { "apll_core", "vpll0_core", "vpll1_core" };
PNAME(mux_dpll_vpll0_p)		= { "dpll", "vpll0" };
PNAME(mux_dpll_vpll0_xin24m_p)	= { "dpll", "vpll0", "xin24m" };
PNAME(mux_dpll_vpll0_vpll1_p)	= { "dpll", "vpll0", "vpll1" };
PNAME(mux_dpll_vpll0_vpll1_xin24m_p)	= { "dpll", "vpll0", "vpll1", "xin24m" };
PNAME(mux_dpll_vpll0_vpll1_usb480m_xin24m_p)	= { "dpll", "vpll0", "vpll1", "usb480m", "xin24m" };
PNAME(mux_vpll0_vpll1_p)	= { "vpll0", "vpll1" };
PNAME(mux_vpll0_vpll1_xin24m_p)	= { "vpll0", "vpll1", "xin24m" };
PNAME(mux_uart0_p)		= { "clk_uart0_src", "dummy", "clk_uart0_frac" };
PNAME(mux_uart1_p)		= { "clk_uart1_src", "dummy", "clk_uart1_frac" };
PNAME(mux_uart2_p)		= { "clk_uart2_src", "dummy", "clk_uart2_frac" };
PNAME(mux_uart3_p)		= { "clk_uart3_src", "dummy", "clk_uart3_frac" };
PNAME(mux_uart4_p)		= { "clk_uart4_src", "dummy", "clk_uart4_frac" };
PNAME(mux_timer_src_p)		= { "xin24m", "clk_rtc32k" };
PNAME(mux_dclk_vop_p)		= { "dclk_vop_src", "dclk_vop_frac", "xin24m" };
PNAME(mux_nandc_p)		= { "clk_nandc_div", "clk_nandc_div50" };
PNAME(mux_sdmmc_p)		= { "clk_sdmmc_div", "clk_sdmmc_div50" };
PNAME(mux_sdio_p)		= { "clk_sdio_div", "clk_sdio_div50" };
PNAME(mux_emmc_p)		= { "clk_emmc_div", "clk_emmc_div50" };
PNAME(mux_mac_p)		= { "clk_mac_src", "mac_clkin" };
PNAME(mux_mac_rmii_sel_p)	= { "clk_mac_rx_tx_div20", "clk_mac_rx_tx_div2" };
PNAME(mux_ddrstdby_p)		= { "clk_ddrphy1x_out", "clk_ddr_stdby_div4" };
PNAME(mux_rtc32k_p)		= { "xin32k", "clk_pvtm_32k", "clk_rtc32k_frac", "clk_rtc32k_div" };
PNAME(mux_usbphy_ref_p)		= { "xin24m", "clk_usbphy_ref_src" };
PNAME(mux_wifi_p)		= { "xin24m", "clk_wifi_src" };
PNAME(mux_pdm_p)		= { "clk_pdm_src", "clk_pdm_frac" };
PNAME(mux_i2s0_8ch_tx_p)	= { "clk_i2s0_8ch_tx_src", "clk_i2s0_8ch_tx_frac", "mclk_i2s0_8ch_in" };
PNAME(mux_i2s0_8ch_tx_rx_p)	= { "clk_i2s0_8ch_tx_mux", "clk_i2s0_8ch_rx_mux"};
PNAME(mux_i2s0_8ch_tx_out_p)	= { "clk_i2s0_8ch_tx", "xin12m" };
PNAME(mux_i2s0_8ch_rx_p)	= { "clk_i2s0_8ch_rx_src", "clk_i2s0_8ch_rx_frac", "mclk_i2s0_8ch_in" };
PNAME(mux_i2s0_8ch_rx_tx_p)	= { "clk_i2s0_8ch_rx_mux", "clk_i2s0_8ch_tx_mux"};
PNAME(mux_i2s1_8ch_tx_p)	= { "clk_i2s1_8ch_tx_src", "clk_i2s1_8ch_tx_frac", "mclk_i2s1_8ch_in" };
PNAME(mux_i2s1_8ch_tx_rx_p)	= { "clk_i2s1_8ch_tx_mux", "clk_i2s1_8ch_rx_mux"};
PNAME(mux_i2s1_8ch_tx_out_p)	= { "clk_i2s1_8ch_tx", "xin12m" };
PNAME(mux_i2s1_8ch_rx_p)	= { "clk_i2s1_8ch_rx_src", "clk_i2s1_8ch_rx_frac", "mclk_i2s1_8ch_in" };
PNAME(mux_i2s1_8ch_rx_tx_p)	= { "clk_i2s1_8ch_rx_mux", "clk_i2s1_8ch_tx_mux"};
PNAME(mux_i2s2_8ch_tx_p)	= { "clk_i2s2_8ch_tx_src", "clk_i2s2_8ch_tx_frac", "mclk_i2s2_8ch_in" };
PNAME(mux_i2s2_8ch_tx_rx_p)	= { "clk_i2s2_8ch_tx_mux", "clk_i2s2_8ch_rx_mux"};
PNAME(mux_i2s2_8ch_tx_out_p)	= { "clk_i2s2_8ch_tx", "xin12m" };
PNAME(mux_i2s2_8ch_rx_p)	= { "clk_i2s2_8ch_rx_src", "clk_i2s2_8ch_rx_frac", "mclk_i2s2_8ch_in" };
PNAME(mux_i2s2_8ch_rx_tx_p)	= { "clk_i2s2_8ch_rx_mux", "clk_i2s2_8ch_tx_mux"};
PNAME(mux_i2s3_8ch_tx_p)	= { "clk_i2s3_8ch_tx_src", "clk_i2s3_8ch_tx_frac", "mclk_i2s3_8ch_in" };
PNAME(mux_i2s3_8ch_tx_rx_p)	= { "clk_i2s3_8ch_tx_mux", "clk_i2s3_8ch_rx_mux"};
PNAME(mux_i2s3_8ch_tx_out_p)	= { "clk_i2s3_8ch_tx", "xin12m" };
PNAME(mux_i2s3_8ch_rx_p)	= { "clk_i2s3_8ch_rx_src", "clk_i2s3_8ch_rx_frac", "mclk_i2s3_8ch_in" };
PNAME(mux_i2s3_8ch_rx_tx_p)	= { "clk_i2s3_8ch_rx_mux", "clk_i2s3_8ch_tx_mux"};
PNAME(mux_i2s0_2ch_p)		= { "clk_i2s0_2ch_src", "clk_i2s0_2ch_frac", "mclk_i2s0_2ch_in" };
PNAME(mux_i2s0_2ch_out_p)	= { "clk_i2s0_2ch", "xin12m" };
PNAME(mux_i2s1_2ch_p)		= { "clk_i2s1_2ch_src", "clk_i2s1_2ch_frac", "mclk_i2s1_2ch_in"};
PNAME(mux_i2s1_2ch_out_p)	= { "clk_i2s1_2ch", "xin12m" };
PNAME(mux_spdif_tx_src_p)	= { "clk_spdif_tx_div", "clk_spdif_tx_div50" };
PNAME(mux_spdif_tx_p)		= { "clk_spdif_tx_src", "clk_spdif_tx_frac", "mclk_i2s0_2ch_in" };
PNAME(mux_spdif_rx_src_p)	= { "clk_spdif_rx_div", "clk_spdif_rx_div50" };
PNAME(mux_spdif_rx_p)		= { "clk_spdif_rx_src", "clk_spdif_rx_frac" };

static struct rockchip_pll_clock rk3308_pll_clks[] __initdata = {
	[apll] = PLL(pll_rk3328, PLL_APLL, "apll", mux_pll_p,
		     0, RK3308_PLL_CON(0),
		     RK3308_MODE_CON, 0, 0, 0, rk3308_pll_rates),
	[dpll] = PLL(pll_rk3328, PLL_DPLL, "dpll", mux_pll_p,
		     0, RK3308_PLL_CON(8),
		     RK3308_MODE_CON, 2, 1, 0, NULL),
	[vpll0] = PLL(pll_rk3328, PLL_VPLL0, "vpll0", mux_pll_p,
		     0, RK3308_PLL_CON(16),
		     RK3308_MODE_CON, 4, 2, 0, rk3308_pll_rates),
	[vpll1] = PLL(pll_rk3328, PLL_VPLL1, "vpll1", mux_pll_p,
		     0, RK3308_PLL_CON(24),
		     RK3308_MODE_CON, 6, 3, 0, rk3308_pll_rates),
};

#define MFLAGS CLK_MUX_HIWORD_MASK
#define DFLAGS CLK_DIVIDER_HIWORD_MASK
#define GFLAGS (CLK_GATE_HIWORD_MASK | CLK_GATE_SET_TO_DISABLE)

static struct rockchip_clk_branch rk3308_uart0_fracmux __initdata =
	MUX(0, "clk_uart0_mux", mux_uart0_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(11), 14, 2, MFLAGS);

static struct rockchip_clk_branch rk3308_uart1_fracmux __initdata =
	MUX(0, "clk_uart1_mux", mux_uart1_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(14), 14, 2, MFLAGS);

static struct rockchip_clk_branch rk3308_uart2_fracmux __initdata =
	MUX(0, "clk_uart2_mux", mux_uart2_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(17), 14, 2, MFLAGS);

static struct rockchip_clk_branch rk3308_uart3_fracmux __initdata =
	MUX(0, "clk_uart3_mux", mux_uart3_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(20), 14, 2, MFLAGS);

static struct rockchip_clk_branch rk3308_uart4_fracmux __initdata =
	MUX(0, "clk_uart4_mux", mux_uart4_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(23), 14, 2, MFLAGS);

static struct rockchip_clk_branch rk3308_dclk_vop_fracmux __initdata =
	MUX(0, "dclk_vop_mux", mux_dclk_vop_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(8), 14, 2, MFLAGS);

static struct rockchip_clk_branch rk3308_rtc32k_fracmux __initdata =
	MUX(SCLK_RTC32K, "clk_rtc32k", mux_rtc32k_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(2), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3308_pdm_fracmux __initdata =
	MUX(0, "clk_pdm_mux", mux_pdm_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(46), 15, 1, MFLAGS);

static struct rockchip_clk_branch rk3308_i2s0_8ch_tx_fracmux __initdata =
	MUX(0, "clk_i2s0_8ch_tx_mux", mux_i2s0_8ch_tx_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(52), 10, 2, MFLAGS);

static struct rockchip_clk_branch rk3308_i2s0_8ch_rx_fracmux __initdata =
	MUX(0, "clk_i2s0_8ch_rx_mux", mux_i2s0_8ch_rx_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(54), 10, 2, MFLAGS);

static struct rockchip_clk_branch rk3308_i2s1_8ch_tx_fracmux __initdata =
	MUX(0, "clk_i2s1_8ch_tx_mux", mux_i2s1_8ch_tx_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(56), 10, 2, MFLAGS);

static struct rockchip_clk_branch rk3308_i2s1_8ch_rx_fracmux __initdata =
	MUX(0, "clk_i2s1_8ch_rx_mux", mux_i2s1_8ch_rx_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(58), 10, 2, MFLAGS);

static struct rockchip_clk_branch rk3308_i2s2_8ch_tx_fracmux __initdata =
	MUX(0, "clk_i2s2_8ch_tx_mux", mux_i2s2_8ch_tx_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(60), 10, 2, MFLAGS);

static struct rockchip_clk_branch rk3308_i2s2_8ch_rx_fracmux __initdata =
	MUX(0, "clk_i2s2_8ch_rx_mux", mux_i2s2_8ch_rx_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(62), 10, 2, MFLAGS);

static struct rockchip_clk_branch rk3308_i2s3_8ch_tx_fracmux __initdata =
	MUX(0, "clk_i2s3_8ch_tx_mux", mux_i2s3_8ch_tx_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(64), 10, 2, MFLAGS);

static struct rockchip_clk_branch rk3308_i2s3_8ch_rx_fracmux __initdata =
	MUX(0, "clk_i2s3_8ch_rx_mux", mux_i2s3_8ch_rx_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(66), 10, 2, MFLAGS);

static struct rockchip_clk_branch rk3308_i2s0_2ch_fracmux __initdata =
	MUX(0, "clk_i2s0_2ch_mux", mux_i2s0_2ch_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(68), 10, 2, MFLAGS);

static struct rockchip_clk_branch rk3308_i2s1_2ch_fracmux __initdata =
	MUX(0, "clk_i2s1_2ch_mux", mux_i2s1_2ch_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(70), 10, 2, MFLAGS);

static struct rockchip_clk_branch rk3308_spdif_tx_fracmux __initdata =
	MUX(0, "clk_spdif_tx_mux", mux_spdif_tx_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(48), 14, 2, MFLAGS);

static struct rockchip_clk_branch rk3308_spdif_rx_fracmux __initdata =
	MUX(0, "clk_spdif_rx_mux", mux_spdif_rx_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(50), 15, 1, MFLAGS);


static struct rockchip_clk_branch rk3308_clk_branches[] __initdata = {
	/*
	 * Clock-Architecture Diagram 1
	 */

	MUX(USB480M, "usb480m", mux_usb480m_p, CLK_SET_RATE_PARENT,
			RK3308_MODE_CON, 8, 2, MFLAGS),
	FACTOR(0, "xin12m", "xin24m", 0, 1, 2),

	/*
	 * Clock-Architecture Diagram 2
	 */

	GATE(0, "apll_core", "apll", CLK_IGNORE_UNUSED,
			RK3308_CLKGATE_CON(0), 0, GFLAGS),
	GATE(0, "vpll0_core", "vpll0", CLK_IGNORE_UNUSED,
			RK3308_CLKGATE_CON(0), 0, GFLAGS),
	GATE(0, "vpll1_core", "vpll1", CLK_IGNORE_UNUSED,
			RK3308_CLKGATE_CON(0), 0, GFLAGS),
	COMPOSITE_NOMUX(0, "pclk_core_dbg", "armclk", CLK_IGNORE_UNUSED,
			RK3308_CLKSEL_CON(0), 8, 4, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RK3308_CLKGATE_CON(0), 2, GFLAGS),
	COMPOSITE_NOMUX(0, "aclk_core", "armclk", CLK_IGNORE_UNUSED,
			RK3308_CLKSEL_CON(0), 12, 3, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RK3308_CLKGATE_CON(0), 1, GFLAGS),

	GATE(0, "clk_jtag", "jtag_clkin", CLK_IGNORE_UNUSED,
			RK3308_CLKGATE_CON(0), 3, GFLAGS),

	GATE(SCLK_PVTM_CORE, "clk_pvtm_core", "xin24m", 0,
			RK3308_CLKGATE_CON(0), 4, GFLAGS),

	/*
	 * Clock-Architecture Diagram 3
	 */

	COMPOSITE_NODIV(ACLK_BUS_SRC, "clk_bus_src", mux_dpll_vpll0_vpll1_p, CLK_IGNORE_UNUSED,
			RK3308_CLKSEL_CON(5), 6, 2, MFLAGS,
			RK3308_CLKGATE_CON(1), 0, GFLAGS),
	COMPOSITE_NOMUX(PCLK_BUS, "pclk_bus", "clk_bus_src", CLK_IGNORE_UNUSED,
			RK3308_CLKSEL_CON(6), 8, 5, DFLAGS,
			RK3308_CLKGATE_CON(1), 3, GFLAGS),
	GATE(PCLK_DDR, "pclk_ddr", "pclk_bus", CLK_IGNORE_UNUSED,
			RK3308_CLKGATE_CON(4), 15, GFLAGS),
	COMPOSITE_NOMUX(HCLK_BUS, "hclk_bus", "clk_bus_src", CLK_IGNORE_UNUSED,
			RK3308_CLKSEL_CON(6), 0, 5, DFLAGS,
			RK3308_CLKGATE_CON(1), 2, GFLAGS),
	COMPOSITE_NOMUX(ACLK_BUS, "aclk_bus", "clk_bus_src", CLK_IGNORE_UNUSED,
			RK3308_CLKSEL_CON(5), 0, 5, DFLAGS,
			RK3308_CLKGATE_CON(1), 1, GFLAGS),

	COMPOSITE(0, "clk_uart0_src", mux_dpll_vpll0_vpll1_usb480m_xin24m_p, 0,
			RK3308_CLKSEL_CON(10), 13, 3, MFLAGS, 0, 5, DFLAGS,
			RK3308_CLKGATE_CON(1), 9, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_uart0_frac", "clk_uart0_src", CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(12), 0,
			RK3308_CLKGATE_CON(1), 11, GFLAGS,
			&rk3308_uart0_fracmux, 0),
	GATE(SCLK_UART0, "clk_uart0", "clk_uart0_mux", 0,
			RK3308_CLKGATE_CON(1), 12, GFLAGS),

	COMPOSITE(0, "clk_uart1_src", mux_dpll_vpll0_vpll1_usb480m_xin24m_p, 0,
			RK3308_CLKSEL_CON(13), 13, 3, MFLAGS, 0, 5, DFLAGS,
			RK3308_CLKGATE_CON(1), 13, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_uart1_frac", "clk_uart1_src", CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(15), 0,
			RK3308_CLKGATE_CON(1), 15, GFLAGS,
			&rk3308_uart1_fracmux, 0),
	GATE(SCLK_UART1, "clk_uart1", "clk_uart1_mux", 0,
			RK3308_CLKGATE_CON(2), 0, GFLAGS),

	COMPOSITE(0, "clk_uart2_src", mux_dpll_vpll0_vpll1_usb480m_xin24m_p, 0,
			RK3308_CLKSEL_CON(16), 13, 3, MFLAGS, 0, 5, DFLAGS,
			RK3308_CLKGATE_CON(2), 1, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_uart2_frac", "clk_uart2_src", CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(18), 0,
			RK3308_CLKGATE_CON(2), 3, GFLAGS,
			&rk3308_uart2_fracmux, 0),
	GATE(SCLK_UART2, "clk_uart2", "clk_uart2_mux", CLK_SET_RATE_PARENT,
			RK3308_CLKGATE_CON(2), 4, GFLAGS),

	COMPOSITE(0, "clk_uart3_src", mux_dpll_vpll0_vpll1_usb480m_xin24m_p, 0,
			RK3308_CLKSEL_CON(19), 13, 3, MFLAGS, 0, 5, DFLAGS,
			RK3308_CLKGATE_CON(2), 5, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_uart3_frac", "clk_uart3_src", CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(21), 0,
			RK3308_CLKGATE_CON(2), 7, GFLAGS,
			&rk3308_uart3_fracmux, 0),
	GATE(SCLK_UART3, "clk_uart3", "clk_uart3_mux", 0,
			RK3308_CLKGATE_CON(2), 8, GFLAGS),

	COMPOSITE(0, "clk_uart4_src", mux_dpll_vpll0_vpll1_usb480m_xin24m_p, 0,
			RK3308_CLKSEL_CON(22), 13, 3, MFLAGS, 0, 5, DFLAGS,
			RK3308_CLKGATE_CON(2), 9, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_uart4_frac", "clk_uart4_src", CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(24), 0,
			RK3308_CLKGATE_CON(2), 11, GFLAGS,
			&rk3308_uart4_fracmux, 0),
	GATE(SCLK_UART4, "clk_uart4", "clk_uart4_mux", 0,
			RK3308_CLKGATE_CON(2), 12, GFLAGS),

	COMPOSITE(SCLK_I2C0, "clk_i2c0", mux_dpll_vpll0_xin24m_p, 0,
			RK3308_CLKSEL_CON(25), 14, 2, MFLAGS, 0, 7, DFLAGS,
			RK3308_CLKGATE_CON(2), 13, GFLAGS),
	COMPOSITE(SCLK_I2C1, "clk_i2c1", mux_dpll_vpll0_xin24m_p, 0,
			RK3308_CLKSEL_CON(26), 14, 2, MFLAGS, 0, 7, DFLAGS,
			RK3308_CLKGATE_CON(2), 14, GFLAGS),
	COMPOSITE(SCLK_I2C2, "clk_i2c2", mux_dpll_vpll0_xin24m_p, 0,
			RK3308_CLKSEL_CON(27), 14, 2, MFLAGS, 0, 7, DFLAGS,
			RK3308_CLKGATE_CON(2), 15, GFLAGS),
	COMPOSITE(SCLK_I2C3, "clk_i2c3", mux_dpll_vpll0_xin24m_p, 0,
			RK3308_CLKSEL_CON(28), 14, 2, MFLAGS, 0, 7, DFLAGS,
			RK3308_CLKGATE_CON(3), 0, GFLAGS),

	COMPOSITE(SCLK_PWM, "clk_pwm", mux_dpll_vpll0_xin24m_p, 0,
			RK3308_CLKSEL_CON(29), 14, 2, MFLAGS, 0, 7, DFLAGS,
			RK3308_CLKGATE_CON(3), 1, GFLAGS),

	COMPOSITE(SCLK_SPI0, "clk_isp0", mux_dpll_vpll0_xin24m_p, 0,
			RK3308_CLKSEL_CON(30), 14, 2, MFLAGS, 0, 7, DFLAGS,
			RK3308_CLKGATE_CON(3), 2, GFLAGS),
	COMPOSITE(SCLK_SPI1, "clk_isp1", mux_dpll_vpll0_xin24m_p, 0,
			RK3308_CLKSEL_CON(31), 14, 2, MFLAGS, 0, 7, DFLAGS,
			RK3308_CLKGATE_CON(3), 3, GFLAGS),
	COMPOSITE(SCLK_SPI2, "clk_isp2", mux_dpll_vpll0_xin24m_p, 0,
			RK3308_CLKSEL_CON(32), 14, 2, MFLAGS, 0, 7, DFLAGS,
			RK3308_CLKGATE_CON(3), 4, GFLAGS),

	GATE(SCLK_TIMER0, "sclk_timer0", "xin24m", 0,
			RK3308_CLKGATE_CON(3), 10, GFLAGS),
	GATE(SCLK_TIMER1, "sclk_timer1", "xin24m", 0,
			RK3308_CLKGATE_CON(3), 11, GFLAGS),
	GATE(SCLK_TIMER2, "sclk_timer2", "xin24m", 0,
			RK3308_CLKGATE_CON(3), 12, GFLAGS),
	GATE(SCLK_TIMER3, "sclk_timer3", "xin24m", 0,
			RK3308_CLKGATE_CON(3), 13, GFLAGS),
	GATE(SCLK_TIMER4, "sclk_timer4", "xin24m", 0,
			RK3308_CLKGATE_CON(3), 14, GFLAGS),
	GATE(SCLK_TIMER5, "sclk_timer5", "xin24m", 0,
			RK3308_CLKGATE_CON(3), 15, GFLAGS),

	COMPOSITE_NOMUX(SCLK_TSADC, "clk_tsadc", "xin24m", 0,
			RK3308_CLKSEL_CON(33), 0, 11, DFLAGS,
			RK3308_CLKGATE_CON(3), 5, GFLAGS),
	COMPOSITE_NOMUX(SCLK_SARADC, "clk_saradc", "xin24m", 0,
			RK3308_CLKSEL_CON(34), 0, 11, DFLAGS,
			RK3308_CLKGATE_CON(3), 6, GFLAGS),

	COMPOSITE_NOMUX(SCLK_OTP, "clk_otp", "xin24m", 0,
			RK3308_CLKSEL_CON(35), 0, 4, DFLAGS,
			RK3308_CLKGATE_CON(3), 7, GFLAGS),
	COMPOSITE_NOMUX(SCLK_OTP_USR, "clk_otp_usr", "clk_otp", 0,
			RK3308_CLKSEL_CON(35), 4, 2, DFLAGS,
			RK3308_CLKGATE_CON(3), 8, GFLAGS),

	GATE(SCLK_CPU_BOOST, "clk_cpu_boost", "xin24m", CLK_IGNORE_UNUSED,
			RK3308_CLKGATE_CON(3), 9, GFLAGS),

	COMPOSITE(SCLK_CRYPTO, "clk_crypto", mux_dpll_vpll0_vpll1_p, 0,
			RK3308_CLKSEL_CON(7), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3308_CLKGATE_CON(1), 4, GFLAGS),
	COMPOSITE(SCLK_CRYPTO_APK, "clk_crypto_apk", mux_dpll_vpll0_vpll1_p, 0,
			RK3308_CLKSEL_CON(7), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RK3308_CLKGATE_CON(1), 5, GFLAGS),

	COMPOSITE(0, "dclk_vop_src", mux_dpll_vpll0_vpll1_p, 0,
			RK3308_CLKSEL_CON(8), 10, 2, MFLAGS, 0, 8, DFLAGS,
			RK3308_CLKGATE_CON(1), 6, GFLAGS),
	COMPOSITE_FRACMUX(0, "dclk_vop_frac", "dclk_vop_src", CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(9), 0,
			RK3308_CLKGATE_CON(1), 7, GFLAGS,
			&rk3308_dclk_vop_fracmux, 0),
	GATE(DCLK_VOP, "dclk_vop", "dclk_vop_mux", 0,
			RK3308_CLKGATE_CON(1), 8, GFLAGS),

	/*
	 * Clock-Architecture Diagram 4
	 */

	COMPOSITE_NODIV(ACLK_PERI_SRC, "clk_peri_src", mux_dpll_vpll0_vpll1_p, CLK_IGNORE_UNUSED,
			RK3308_CLKSEL_CON(36), 6, 2, MFLAGS,
			RK3308_CLKGATE_CON(8), 0, GFLAGS),
	COMPOSITE_NOMUX(ACLK_PERI, "aclk_peri", "clk_peri_src", CLK_IGNORE_UNUSED,
			RK3308_CLKSEL_CON(36), 0, 5, DFLAGS,
			RK3308_CLKGATE_CON(8), 1, GFLAGS),
	COMPOSITE_NOMUX(HCLK_PERI, "hclk_peri", "clk_peri_src", CLK_IGNORE_UNUSED,
			RK3308_CLKSEL_CON(37), 0, 5, DFLAGS,
			RK3308_CLKGATE_CON(8), 2, GFLAGS),
	COMPOSITE_NOMUX(PCLK_PERI, "pclk_peri", "clk_peri_src", CLK_IGNORE_UNUSED,
			RK3308_CLKSEL_CON(37), 8, 5, DFLAGS,
			RK3308_CLKGATE_CON(8), 3, GFLAGS),

	COMPOSITE(SCLK_NANDC_DIV, "clk_nandc_div", mux_dpll_vpll0_vpll1_p, CLK_IGNORE_UNUSED,
			RK3308_CLKSEL_CON(38), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3308_CLKGATE_CON(8), 4, GFLAGS),
	COMPOSITE(SCLK_NANDC_DIV50, "clk_nandc_div50", mux_dpll_vpll0_vpll1_p, CLK_IGNORE_UNUSED,
			RK3308_CLKSEL_CON(38), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3308_CLKGATE_CON(8), 4, GFLAGS),
	COMPOSITE_NODIV(SCLK_NANDC, "clk_nandc", mux_nandc_p, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			RK3308_CLKSEL_CON(38), 15, 1, MFLAGS,
			RK3308_CLKGATE_CON(8), 5, GFLAGS),

	COMPOSITE(SCLK_SDMMC_DIV, "clk_sdmmc_div", mux_dpll_vpll0_vpll1_xin24m_p, CLK_IGNORE_UNUSED,
			RK3308_CLKSEL_CON(39), 8, 2, MFLAGS, 0, 8, DFLAGS,
			RK3308_CLKGATE_CON(8), 6, GFLAGS),
	COMPOSITE(SCLK_SDMMC_DIV50, "clk_sdmmc_div50", mux_dpll_vpll0_vpll1_xin24m_p, CLK_IGNORE_UNUSED,
			RK3308_CLKSEL_CON(39), 8, 2, MFLAGS, 0, 8, DFLAGS,
			RK3308_CLKGATE_CON(8), 6, GFLAGS),
	COMPOSITE_NODIV(SCLK_SDMMC, "clk_sdmmc", mux_sdmmc_p, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			RK3308_CLKSEL_CON(39), 15, 1, MFLAGS,
			RK3308_CLKGATE_CON(8), 7, GFLAGS),
	MMC(SCLK_SDMMC_DRV,     "sdmmc_drv",    "clk_sdmmc", RK3308_SDMMC_CON0, 1),
	MMC(SCLK_SDMMC_SAMPLE,  "sdmmc_sample", "clk_sdmmc", RK3308_SDMMC_CON1, 1),

	COMPOSITE(SCLK_SDIO_DIV, "clk_sdio_div", mux_dpll_vpll0_vpll1_xin24m_p, CLK_IGNORE_UNUSED,
			RK3308_CLKSEL_CON(40), 8, 2, MFLAGS, 0, 8, DFLAGS,
			RK3308_CLKGATE_CON(8), 8, GFLAGS),
	COMPOSITE(SCLK_SDIO_DIV50, "clk_sdio_div50", mux_dpll_vpll0_vpll1_xin24m_p, CLK_IGNORE_UNUSED,
			RK3308_CLKSEL_CON(40), 8, 2, MFLAGS, 0, 8, DFLAGS,
			RK3308_CLKGATE_CON(8), 8, GFLAGS),
	COMPOSITE_NODIV(SCLK_SDIO, "clk_sdio", mux_sdio_p, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			RK3308_CLKSEL_CON(40), 15, 1, MFLAGS,
			RK3308_CLKGATE_CON(8), 9, GFLAGS),
	MMC(SCLK_SDIO_DRV,		"sdio_drv",    "clk_sdio",	RK3308_SDIO_CON0,  1),
	MMC(SCLK_SDIO_SAMPLE,	"sdio_sample", "clk_sdio",	RK3308_SDIO_CON1,  1),

	COMPOSITE(SCLK_EMMC_DIV, "clk_emmc_div", mux_dpll_vpll0_vpll1_xin24m_p, CLK_IGNORE_UNUSED,
			RK3308_CLKSEL_CON(41), 8, 2, MFLAGS, 0, 8, DFLAGS,
			RK3308_CLKGATE_CON(8), 10, GFLAGS),
	COMPOSITE(SCLK_EMMC_DIV50, "clk_emmc_div50", mux_dpll_vpll0_vpll1_xin24m_p, CLK_IGNORE_UNUSED,
			RK3308_CLKSEL_CON(41), 8, 2, MFLAGS, 0, 8, DFLAGS,
			RK3308_CLKGATE_CON(8), 10, GFLAGS),
	COMPOSITE_NODIV(SCLK_EMMC, "clk_emmc", mux_emmc_p, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			RK3308_CLKSEL_CON(41), 15, 1, MFLAGS,
			RK3308_CLKGATE_CON(8), 11, GFLAGS),
	MMC(SCLK_EMMC_DRV,     "emmc_drv",     "clk_emmc",  RK3308_EMMC_CON0,  1),
	MMC(SCLK_EMMC_SAMPLE,  "emmc_sample",  "clk_emmc",  RK3308_EMMC_CON1,  1),

	COMPOSITE(SCLK_SFC, "clk_sfc", mux_dpll_vpll0_vpll1_p, 0,
			RK3308_CLKSEL_CON(42), 14, 2, MFLAGS, 0, 7, DFLAGS,
			RK3308_CLKGATE_CON(8), 12, GFLAGS),

	GATE(SCLK_OTG_ADP, "clk_otg_adp", "clk_rtc32k", 0,
			RK3308_CLKGATE_CON(8), 13, GFLAGS),

	COMPOSITE(SCLK_MAC_SRC, "clk_mac_src", mux_dpll_vpll0_vpll1_p, 0,
			RK3308_CLKSEL_CON(43), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3308_CLKGATE_CON(8), 14, GFLAGS),
	MUX(SCLK_MAC, "clk_mac", mux_mac_p,  CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(43), 14, 1, MFLAGS),
	GATE(SCLK_MAC_REF, "clk_mac_ref", "clk_mac", 0,
			RK3308_CLKGATE_CON(9), 1, GFLAGS),
	GATE(SCLK_MAC_RX_TX, "clk_mac_rx_tx", "clk_mac", 0,
			RK3308_CLKGATE_CON(9), 0, GFLAGS),
	FACTOR(0, "clk_mac_rx_tx_div2", "clk_mac_rx_tx", 0, 1, 2),
	FACTOR(0, "clk_mac_rx_tx_div20", "clk_mac_rx_tx", 0, 1, 20),
	MUX(SCLK_MAC_RMII, "clk_mac_rmii_sel", mux_mac_rmii_sel_p,  CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(43), 15, 1, MFLAGS),

	/*
	 * Clock-Architecture Diagram 5
	 */

	GATE(0, "clk_ddr_mon_timer", "xin24m", CLK_IGNORE_UNUSED,
			RK3308_CLKGATE_CON(0), 12, GFLAGS),

	GATE(0, "clk_ddr_mon", "clk_ddrphy1x_out", CLK_IGNORE_UNUSED,
			RK3308_CLKGATE_CON(4), 10, GFLAGS),
	GATE(0, "clk_ddr_upctrl", "clk_ddrphy1x_out", CLK_IGNORE_UNUSED,
			RK3308_CLKGATE_CON(4), 11, GFLAGS),
	GATE(0, "clk_ddr_msch", "clk_ddrphy1x_out", CLK_IGNORE_UNUSED,
			RK3308_CLKGATE_CON(4), 12, GFLAGS),
	GATE(0, "clk_ddr_msch_peribus", "clk_ddrphy1x_out", CLK_IGNORE_UNUSED,
			RK3308_CLKGATE_CON(4), 13, GFLAGS),

	COMPOSITE(SCLK_DDRCLK, "clk_ddrphy4x_src", mux_dpll_vpll0_vpll1_p, CLK_IGNORE_UNUSED,
			RK3308_CLKSEL_CON(1), 6, 2, MFLAGS, 0, 3, DFLAGS,
			RK3308_CLKGATE_CON(0), 10, GFLAGS),
	GATE(0, "clk_ddrphy4x", "clk_ddrphy4x_src", CLK_IGNORE_UNUSED,
			RK3308_CLKGATE_CON(0), 11, GFLAGS),
	FACTOR_GATE(0, "clk_ddr_stdby_div4", "clk_ddrphy4x", CLK_IGNORE_UNUSED, 1, 4,
			RK3308_CLKGATE_CON(0), 13, GFLAGS),
	COMPOSITE_NODIV(0, "clk_ddrstdby", mux_ddrstdby_p, CLK_IGNORE_UNUSED,
			RK3308_CLKSEL_CON(1), 8, 1, MFLAGS,
			RK3308_CLKGATE_CON(4), 14, GFLAGS),

	/*
	 * Clock-Architecture Diagram 6
	 */

	GATE(PCLK_PMU, "pclk_pmu", "pclk_bus", CLK_IGNORE_UNUSED,
			RK3308_CLKGATE_CON(4), 5, GFLAGS),
	GATE(SCLK_PMU, "clk_pmu", "pclk_bus", CLK_IGNORE_UNUSED,
			RK3308_CLKGATE_CON(4), 6, GFLAGS),

	COMPOSITE_FRACMUX(0, "clk_rtc32k_frac", "xin24m", CLK_IGNORE_UNUSED,
			RK3308_CLKSEL_CON(3), 0,
			RK3308_CLKGATE_CON(4), 3, GFLAGS,
			&rk3308_rtc32k_fracmux, 0),
	MUX(0, "clk_rtc32k_div_src", mux_vpll0_vpll1_p, 0,
			RK3308_CLKSEL_CON(2), 10, 1, MFLAGS),
	COMPOSITE_NOMUX(0, "clk_rtc32k_div", "clk_rtc32k_div_src", CLK_IGNORE_UNUSED | CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(4), 0, 16, DFLAGS,
			RK3308_CLKGATE_CON(4), 2, GFLAGS),

	COMPOSITE(0, "clk_usbphy_ref_src", mux_dpll_vpll0_p, 0,
			RK3308_CLKSEL_CON(72), 6, 1, MFLAGS, 0, 6, DFLAGS,
			RK3308_CLKGATE_CON(4), 7, GFLAGS),
	COMPOSITE_NODIV(SCLK_USBPHY_REF, "clk_usbphy_ref", mux_usbphy_ref_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(72), 7, 1, MFLAGS,
			RK3308_CLKGATE_CON(4), 8, GFLAGS),

	COMPOSITE(0, "clk_wifi_src", mux_dpll_vpll0_p, 0,
			RK3308_CLKSEL_CON(44), 6, 1, MFLAGS, 0, 6, DFLAGS,
			RK3308_CLKGATE_CON(4), 0, GFLAGS),
	COMPOSITE_NODIV(SCLK_WIFI, "clk_wifi", mux_wifi_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(44), 7, 1, MFLAGS,
			RK3308_CLKGATE_CON(4), 1, GFLAGS),

	GATE(SCLK_PVTM_PMU, "clk_pvtm_pmu", "xin24m", 0,
			RK3308_CLKGATE_CON(4), 4, GFLAGS),

	/*
	 * Clock-Architecture Diagram 7
	 */

	COMPOSITE_NODIV(0, "clk_audio_src", mux_vpll0_vpll1_xin24m_p, 0,
			RK3308_CLKSEL_CON(45), 6, 1, MFLAGS,
			RK3308_CLKGATE_CON(10), 0, GFLAGS),
	COMPOSITE_NOMUX(HCLK_AUDIO, "hclk_audio", "clk_audio_src", 0,
			RK3308_CLKSEL_CON(45), 0, 5, DFLAGS,
			RK3308_CLKGATE_CON(10), 1, GFLAGS),
	COMPOSITE_NOMUX(PCLK_AUDIO, "pclk_audio", "clk_audio_src", 0,
			RK3308_CLKSEL_CON(45), 8, 5, DFLAGS,
			RK3308_CLKGATE_CON(10), 2, GFLAGS),

	COMPOSITE(0, "clk_pdm_src", mux_vpll0_vpll1_xin24m_p, 0,
			RK3308_CLKSEL_CON(46), 8, 2, MFLAGS, 0, 7, DFLAGS,
			RK3308_CLKGATE_CON(10), 3, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_pdm_frac", "clk_pdm_src", CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(47), 0,
			RK3308_CLKGATE_CON(10), 4, GFLAGS,
			&rk3308_pdm_fracmux, 0),
	GATE(SCLK_PDM, "clk_pdm", "clk_pdm_mux", 0,
			RK3308_CLKGATE_CON(10), 5, GFLAGS),

	COMPOSITE(0, "clk_i2s0_8ch_tx_src", mux_vpll0_vpll1_xin24m_p, 0,
			RK3308_CLKSEL_CON(52), 8, 2, MFLAGS, 0, 7, DFLAGS,
			RK3308_CLKGATE_CON(10), 12, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_i2s0_8ch_tx_frac", "clk_i2s0_8ch_tx_src", CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(53), 0,
			RK3308_CLKGATE_CON(10), 13, GFLAGS,
			&rk3308_i2s0_8ch_tx_fracmux, 0),
	COMPOSITE_NODIV(SCLK_I2S0_8CH_TX, "clk_i2s0_8ch_tx", mux_i2s0_8ch_tx_rx_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(52), 12, 1, MFLAGS,
			RK3308_CLKGATE_CON(10), 14, GFLAGS),
	COMPOSITE_NODIV(SCLK_I2S0_8CH_TX_OUT, "clk_i2s0_8ch_tx_out", mux_i2s0_8ch_tx_out_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(52), 15, 1, MFLAGS,
			RK3308_CLKGATE_CON(10), 15, GFLAGS),

	COMPOSITE(0, "clk_i2s0_8ch_rx_src", mux_vpll0_vpll1_xin24m_p, 0,
			RK3308_CLKSEL_CON(54), 8, 2, MFLAGS, 0, 7, DFLAGS,
			RK3308_CLKGATE_CON(11), 0, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_i2s0_8ch_rx_frac", "clk_i2s0_8ch_rx_src", CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(55), 0,
			RK3308_CLKGATE_CON(11), 1, GFLAGS,
			&rk3308_i2s0_8ch_rx_fracmux, 0),
	COMPOSITE_NODIV(SCLK_I2S0_8CH_RX, "clk_i2s0_8ch_rx", mux_i2s0_8ch_rx_tx_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(54), 12, 1, MFLAGS,
			RK3308_CLKGATE_CON(11), 2, GFLAGS),
	GATE(SCLK_I2S0_8CH_RX_OUT, "clk_i2s0_8ch_rx_out", "clk_i2s0_8ch_rx", 0,
			RK3308_CLKGATE_CON(11), 3, GFLAGS),

	COMPOSITE(0, "clk_i2s1_8ch_tx_src", mux_vpll0_vpll1_xin24m_p, 0,
			RK3308_CLKSEL_CON(56), 8, 2, MFLAGS, 0, 7, DFLAGS,
			RK3308_CLKGATE_CON(11), 4, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_i2s1_8ch_tx_frac", "clk_i2s1_8ch_tx_src", CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(57), 0,
			RK3308_CLKGATE_CON(11), 5, GFLAGS,
			&rk3308_i2s1_8ch_tx_fracmux, 0),
	COMPOSITE_NODIV(SCLK_I2S1_8CH_TX, "clk_i2s1_8ch_tx", mux_i2s1_8ch_tx_rx_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(56), 12, 1, MFLAGS,
			RK3308_CLKGATE_CON(11), 6, GFLAGS),
	COMPOSITE_NODIV(SCLK_I2S1_8CH_TX_OUT, "clk_i2s1_8ch_tx_out", mux_i2s1_8ch_tx_out_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(56), 15, 1, MFLAGS,
			RK3308_CLKGATE_CON(11), 7, GFLAGS),

	COMPOSITE(0, "clk_i2s1_8ch_rx_src", mux_vpll0_vpll1_xin24m_p, 0,
			RK3308_CLKSEL_CON(58), 8, 2, MFLAGS, 0, 7, DFLAGS,
			RK3308_CLKGATE_CON(11), 8, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_i2s1_8ch_rx_frac", "clk_i2s1_8ch_rx_src", CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(59), 0,
			RK3308_CLKGATE_CON(11), 9, GFLAGS,
			&rk3308_i2s1_8ch_rx_fracmux, 0),
	COMPOSITE_NODIV(SCLK_I2S1_8CH_RX, "clk_i2s1_8ch_rx", mux_i2s1_8ch_rx_tx_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(58), 12, 1, MFLAGS,
			RK3308_CLKGATE_CON(11), 10, GFLAGS),
	GATE(SCLK_I2S1_8CH_RX_OUT, "clk_i2s1_8ch_rx_out", "clk_i2s1_8ch_rx", 0,
			RK3308_CLKGATE_CON(11), 11, GFLAGS),

	COMPOSITE(0, "clk_i2s2_8ch_tx_src", mux_vpll0_vpll1_xin24m_p, 0,
			RK3308_CLKSEL_CON(60), 8, 2, MFLAGS, 0, 7, DFLAGS,
			RK3308_CLKGATE_CON(11), 12, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_i2s2_8ch_tx_frac", "clk_i2s2_8ch_tx_src", CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(61), 0,
			RK3308_CLKGATE_CON(11), 13, GFLAGS,
			&rk3308_i2s2_8ch_tx_fracmux, 0),
	COMPOSITE_NODIV(SCLK_I2S2_8CH_TX, "clk_i2s2_8ch_tx", mux_i2s2_8ch_tx_rx_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(60), 12, 1, MFLAGS,
			RK3308_CLKGATE_CON(11), 14, GFLAGS),
	COMPOSITE_NODIV(SCLK_I2S2_8CH_TX_OUT, "clk_i2s2_8ch_tx_out", mux_i2s2_8ch_tx_out_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(60), 15, 1, MFLAGS,
			RK3308_CLKGATE_CON(11), 15, GFLAGS),

	COMPOSITE(0, "clk_i2s2_8ch_rx_src", mux_vpll0_vpll1_xin24m_p, 0,
			RK3308_CLKSEL_CON(62), 8, 2, MFLAGS, 0, 7, DFLAGS,
			RK3308_CLKGATE_CON(12), 0, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_i2s2_8ch_rx_frac", "clk_i2s2_8ch_rx_src", CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(63), 0,
			RK3308_CLKGATE_CON(12), 1, GFLAGS,
			&rk3308_i2s2_8ch_rx_fracmux, 0),
	COMPOSITE_NODIV(SCLK_I2S2_8CH_RX, "clk_i2s2_8ch_rx", mux_i2s2_8ch_rx_tx_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(62), 12, 1, MFLAGS,
			RK3308_CLKGATE_CON(12), 2, GFLAGS),
	GATE(SCLK_I2S2_8CH_RX_OUT, "clk_i2s2_8ch_rx_out", "clk_i2s2_8ch_rx", 0,
			RK3308_CLKGATE_CON(12), 3, GFLAGS),

	COMPOSITE(0, "clk_i2s3_8ch_tx_src", mux_vpll0_vpll1_xin24m_p, 0,
			RK3308_CLKSEL_CON(64), 8, 2, MFLAGS, 0, 7, DFLAGS,
			RK3308_CLKGATE_CON(12), 4, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_i2s3_8ch_tx_frac", "clk_i2s3_8ch_tx_src", CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(65), 0,
			RK3308_CLKGATE_CON(12), 5, GFLAGS,
			&rk3308_i2s3_8ch_tx_fracmux, 0),
	COMPOSITE_NODIV(SCLK_I2S3_8CH_TX, "clk_i2s3_8ch_tx", mux_i2s3_8ch_tx_rx_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(64), 12, 1, MFLAGS,
			RK3308_CLKGATE_CON(12), 6, GFLAGS),
	COMPOSITE_NODIV(SCLK_I2S3_8CH_TX_OUT, "clk_i2s3_8ch_tx_out", mux_i2s3_8ch_tx_out_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(64), 15, 1, MFLAGS,
			RK3308_CLKGATE_CON(12), 7, GFLAGS),

	COMPOSITE(0, "clk_i2s3_8ch_rx_src", mux_vpll0_vpll1_xin24m_p, 0,
			RK3308_CLKSEL_CON(66), 8, 2, MFLAGS, 0, 7, DFLAGS,
			RK3308_CLKGATE_CON(12), 8, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_i2s3_8ch_rx_frac", "clk_i2s3_8ch_rx_src", CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(67), 0,
			RK3308_CLKGATE_CON(12), 9, GFLAGS,
			&rk3308_i2s3_8ch_rx_fracmux, 0),
	COMPOSITE_NODIV(SCLK_I2S3_8CH_RX, "clk_i2s3_8ch_rx", mux_i2s3_8ch_rx_tx_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(66), 12, 1, MFLAGS,
			RK3308_CLKGATE_CON(12), 10, GFLAGS),
	GATE(SCLK_I2S3_8CH_RX_OUT, "clk_i2s3_8ch_rx_out", "clk_i2s3_8ch_rx", 0,
			RK3308_CLKGATE_CON(12), 11, GFLAGS),

	COMPOSITE(0, "clk_i2s0_2ch_src", mux_vpll0_vpll1_xin24m_p, 0,
			RK3308_CLKSEL_CON(68), 8, 2, MFLAGS, 0, 7, DFLAGS,
			RK3308_CLKGATE_CON(12), 12, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_i2s0_2ch_frac", "clk_i2s0_2ch_src", CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(69), 0,
			RK3308_CLKGATE_CON(12), 13, GFLAGS,
			&rk3308_i2s0_2ch_fracmux, 0),
	GATE(SCLK_I2S0_2CH, "clk_i2s0_2ch", "clk_i2s0_2ch_mux", 0,
			RK3308_CLKGATE_CON(12), 14, GFLAGS),
	COMPOSITE_NODIV(SCLK_I2S0_2CH_OUT, "clk_i2s0_2ch_out", mux_i2s0_2ch_out_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(68), 15, 1, MFLAGS,
			RK3308_CLKGATE_CON(12), 15, GFLAGS),

	COMPOSITE(0, "clk_i2s1_2ch_src", mux_vpll0_vpll1_xin24m_p, 0,
			RK3308_CLKSEL_CON(70), 8, 2, MFLAGS, 0, 7, DFLAGS,
			RK3308_CLKGATE_CON(13), 0, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_i2s1_2ch_frac", "clk_i2s1_2ch_src", CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(71), 0,
			RK3308_CLKGATE_CON(13), 1, GFLAGS,
			&rk3308_i2s1_2ch_fracmux, 0),
	GATE(SCLK_I2S1_2CH, "clk_i2s1_2ch", "clk_i2s1_2ch_mux", 0,
			RK3308_CLKGATE_CON(13), 2, GFLAGS),
	COMPOSITE_NODIV(SCLK_I2S1_2CH_OUT, "clk_i2s1_2ch_out", mux_i2s1_2ch_out_p, CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(70), 15, 1, MFLAGS,
			RK3308_CLKGATE_CON(13), 3, GFLAGS),

	COMPOSITE(SCLK_SPDIF_TX_DIV, "clk_spdif_tx_div", mux_vpll0_vpll1_xin24m_p, CLK_IGNORE_UNUSED,
			RK3308_CLKSEL_CON(48), 8, 2, MFLAGS, 0, 7, DFLAGS,
			RK3308_CLKGATE_CON(10), 6, GFLAGS),
	COMPOSITE(SCLK_SPDIF_TX_DIV50, "clk_spdif_tx_div50", mux_vpll0_vpll1_xin24m_p, CLK_IGNORE_UNUSED,
			RK3308_CLKSEL_CON(48), 8, 2, MFLAGS, 0, 7, DFLAGS,
			RK3308_CLKGATE_CON(10), 6, GFLAGS),
	MUX(0, "clk_spdif_tx_src", mux_spdif_tx_src_p, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			RK3308_CLKSEL_CON(48), 12, 1, MFLAGS),
	COMPOSITE_FRACMUX(0, "clk_spdif_tx_frac", "clk_spdif_tx_src", CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(49), 0,
			RK3308_CLKGATE_CON(10), 7, GFLAGS,
			&rk3308_spdif_tx_fracmux, 0),
	GATE(SCLK_SPDIF_TX, "clk_spdif_tx", "clk_spdif_tx_mux", 0,
			RK3308_CLKGATE_CON(10), 8, GFLAGS),

	COMPOSITE(SCLK_SPDIF_RX_DIV, "clk_spdif_rx_div", mux_vpll0_vpll1_xin24m_p, CLK_IGNORE_UNUSED,
			RK3308_CLKSEL_CON(50), 8, 2, MFLAGS, 0, 7, DFLAGS,
			RK3308_CLKGATE_CON(10), 9, GFLAGS),
	COMPOSITE(SCLK_SPDIF_RX_DIV50, "clk_spdif_rx_div50", mux_vpll0_vpll1_xin24m_p, CLK_IGNORE_UNUSED,
			RK3308_CLKSEL_CON(50), 8, 2, MFLAGS, 0, 7, DFLAGS,
			RK3308_CLKGATE_CON(10), 9, GFLAGS),
	MUX(0, "clk_spdif_rx_src", mux_spdif_rx_src_p, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			RK3308_CLKSEL_CON(50), 14, 1, MFLAGS),
	COMPOSITE_FRACMUX(0, "clk_spdif_rx_frac", "clk_spdif_rx_src", CLK_SET_RATE_PARENT,
			RK3308_CLKSEL_CON(51), 0,
			RK3308_CLKGATE_CON(10), 10, GFLAGS,
			&rk3308_spdif_rx_fracmux, 0),
	GATE(SCLK_SPDIF_RX, "clk_spdif_rx", "clk_spdif_rx_mux", 0,
			RK3308_CLKGATE_CON(10), 11, GFLAGS),

	/*
	 * Clock-Architecture Diagram 8
	 */

	GATE(0, "aclk_core_niu", "aclk_core", CLK_IGNORE_UNUSED, RK3308_CLKGATE_CON(0), 5, GFLAGS),
	GATE(0, "pclk_core_dbg_niu", "aclk_core", CLK_IGNORE_UNUSED, RK3308_CLKGATE_CON(0), 6, GFLAGS),
	GATE(0, "pclk_core_dbg_daplite", "pclk_core_dbg", CLK_IGNORE_UNUSED, RK3308_CLKGATE_CON(0), 7, GFLAGS),
	GATE(0, "aclk_core_perf", "pclk_core_dbg", CLK_IGNORE_UNUSED, RK3308_CLKGATE_CON(0), 8, GFLAGS),
	GATE(0, "pclk_core_grf", "pclk_core_dbg", CLK_IGNORE_UNUSED, RK3308_CLKGATE_CON(0), 9, GFLAGS),

	GATE(0, "aclk_peri_niu", "aclk_peri", CLK_IGNORE_UNUSED, RK3308_CLKGATE_CON(9), 2, GFLAGS),
	GATE(0, "aclk_peribus_niu", "aclk_peri", CLK_IGNORE_UNUSED, RK3308_CLKGATE_CON(9), 3, GFLAGS),
	GATE(ACLK_MAC, "aclk_mac", "aclk_peri", 0, RK3308_CLKGATE_CON(9), 4, GFLAGS),

	GATE(0, "hclk_peri_niu", "hclk_peri", CLK_IGNORE_UNUSED, RK3308_CLKGATE_CON(9), 5, GFLAGS),
	GATE(HCLK_NANDC, "hclk_nandc", "hclk_peri", 0, RK3308_CLKGATE_CON(9), 6, GFLAGS),
	GATE(HCLK_SDMMC, "hclk_sdmmc", "hclk_peri", 0, RK3308_CLKGATE_CON(9), 7, GFLAGS),
	GATE(HCLK_SDIO, "hclk_sdio", "hclk_peri", 0, RK3308_CLKGATE_CON(9), 8, GFLAGS),
	GATE(HCLK_EMMC, "hclk_emmc", "hclk_peri", 0, RK3308_CLKGATE_CON(9), 9, GFLAGS),
	GATE(HCLK_SFC, "hclk_sfc", "hclk_peri", 0, RK3308_CLKGATE_CON(9), 10, GFLAGS),
	GATE(HCLK_OTG, "hclk_otg", "hclk_peri", 0, RK3308_CLKGATE_CON(9), 11, GFLAGS),
	GATE(HCLK_HOST, "hclk_host", "hclk_peri", 0, RK3308_CLKGATE_CON(9), 12, GFLAGS),
	GATE(HCLK_HOST_ARB, "hclk_host_arb", "hclk_peri", 0, RK3308_CLKGATE_CON(9), 13, GFLAGS),

	GATE(0, "pclk_peri_niu", "pclk_peri", CLK_IGNORE_UNUSED, RK3308_CLKGATE_CON(9), 14, GFLAGS),
	GATE(PCLK_MAC, "pclk_mac", "pclk_peri", 0, RK3308_CLKGATE_CON(9), 15, GFLAGS),

	GATE(0, "hclk_audio_niu", "hclk_audio", CLK_IGNORE_UNUSED, RK3308_CLKGATE_CON(14), 0, GFLAGS),
	GATE(HCLK_PDM, "hclk_pdm", "hclk_audio", 0, RK3308_CLKGATE_CON(14), 1, GFLAGS),
	GATE(HCLK_SPDIFTX, "hclk_spdiftx", "hclk_audio", 0, RK3308_CLKGATE_CON(14), 2, GFLAGS),
	GATE(HCLK_SPDIFRX, "hclk_spdifrx", "hclk_audio", 0, RK3308_CLKGATE_CON(14), 3, GFLAGS),
	GATE(HCLK_I2S0_8CH, "hclk_i2s0_8ch", "hclk_audio", 0, RK3308_CLKGATE_CON(14), 4, GFLAGS),
	GATE(HCLK_I2S1_8CH, "hclk_i2s1_8ch", "hclk_audio", 0, RK3308_CLKGATE_CON(14), 5, GFLAGS),
	GATE(HCLK_I2S2_8CH, "hclk_i2s2_8ch", "hclk_audio", 0, RK3308_CLKGATE_CON(14), 6, GFLAGS),
	GATE(HCLK_I2S3_8CH, "hclk_i2s3_8ch", "hclk_audio", 0, RK3308_CLKGATE_CON(14), 7, GFLAGS),
	GATE(HCLK_I2S0_2CH, "hclk_i2s0_2ch", "hclk_audio", 0, RK3308_CLKGATE_CON(14), 8, GFLAGS),
	GATE(HCLK_I2S1_2CH, "hclk_i2s1_2ch", "hclk_audio", 0, RK3308_CLKGATE_CON(14), 9, GFLAGS),
	GATE(HCLK_VAD, "hclk_vad", "hclk_audio", 0, RK3308_CLKGATE_CON(14), 10, GFLAGS),

	GATE(0, "pclk_audio_niu", "pclk_audio", CLK_IGNORE_UNUSED, RK3308_CLKGATE_CON(14), 11, GFLAGS),
	GATE(PCLK_ACODEC, "pclk_acodec", "pclk_audio", 0, RK3308_CLKGATE_CON(14), 12, GFLAGS),

	GATE(0, "aclk_bus_niu", "aclk_bus", CLK_IGNORE_UNUSED, RK3308_CLKGATE_CON(5), 0, GFLAGS),
	GATE(0, "aclk_intmem", "aclk_bus", CLK_IGNORE_UNUSED, RK3308_CLKGATE_CON(5), 1, GFLAGS),
	GATE(ACLK_CRYPTO, "aclk_crypto", "aclk_bus", 0, RK3308_CLKGATE_CON(5), 2, GFLAGS),
	GATE(ACLK_VOP, "aclk_vop", "aclk_bus", 0, RK3308_CLKGATE_CON(5), 3, GFLAGS),
	GATE(0, "aclk_gic", "aclk_bus", CLK_IGNORE_UNUSED, RK3308_CLKGATE_CON(5), 4, GFLAGS),

	GATE(0, "hclk_bus_niu", "hclk_bus", CLK_IGNORE_UNUSED, RK3308_CLKGATE_CON(5), 5, GFLAGS),
	GATE(0, "hclk_rom", "hclk_bus", CLK_IGNORE_UNUSED, RK3308_CLKGATE_CON(5), 6, GFLAGS),
	GATE(HCLK_CRYPTO, "hclk_crypto", "hclk_bus", 0, RK3308_CLKGATE_CON(5), 7, GFLAGS),
	GATE(HCLK_VOP, "hclk_vop", "hclk_bus", 0, RK3308_CLKGATE_CON(5), 8, GFLAGS),

	GATE(0, "pclk_bus_niu", "pclk_bus", CLK_IGNORE_UNUSED, RK3308_CLKGATE_CON(5), 9, GFLAGS),
	GATE(PCLK_UART0, "pclk_uart0", "pclk_bus", 0, RK3308_CLKGATE_CON(5), 10, GFLAGS),
	GATE(PCLK_UART1, "pclk_uart1", "pclk_bus", 0, RK3308_CLKGATE_CON(5), 11, GFLAGS),
	GATE(PCLK_UART2, "pclk_uart2", "pclk_bus", 0, RK3308_CLKGATE_CON(5), 12, GFLAGS),
	GATE(PCLK_UART3, "pclk_uart3", "pclk_bus", 0, RK3308_CLKGATE_CON(5), 13, GFLAGS),
	GATE(PCLK_UART4, "pclk_uart4", "pclk_bus", 0, RK3308_CLKGATE_CON(5), 14, GFLAGS),
	GATE(PCLK_I2C0, "pclk_i2c0", "pclk_bus", 0, RK3308_CLKGATE_CON(5), 15, GFLAGS),
	GATE(PCLK_I2C1, "pclk_i2c1", "pclk_bus", 0, RK3308_CLKGATE_CON(6), 0, GFLAGS),
	GATE(PCLK_I2C2, "pclk_i2c2", "pclk_bus", 0, RK3308_CLKGATE_CON(6), 1, GFLAGS),
	GATE(PCLK_I2C3, "pclk_i2c3", "pclk_bus", 0, RK3308_CLKGATE_CON(6), 2, GFLAGS),
	GATE(PCLK_PWM, "pclk_pwm", "pclk_bus", 0, RK3308_CLKGATE_CON(6), 3, GFLAGS),
	GATE(PCLK_SPI0, "pclk_spi0", "pclk_bus", 0, RK3308_CLKGATE_CON(6), 4, GFLAGS),
	GATE(PCLK_SPI1, "pclk_spi1", "pclk_bus", 0, RK3308_CLKGATE_CON(6), 5, GFLAGS),
	GATE(PCLK_SPI2, "pclk_spi2", "pclk_bus", 0, RK3308_CLKGATE_CON(6), 6, GFLAGS),
	GATE(PCLK_SARADC, "pclk_saradc", "pclk_bus", 0, RK3308_CLKGATE_CON(6), 7, GFLAGS),
	GATE(PCLK_TSADC, "pclk_tsadc", "pclk_bus", 0, RK3308_CLKGATE_CON(6), 8, GFLAGS),
	GATE(PCLK_TIMER, "pclk_timer", "pclk_bus", 0, RK3308_CLKGATE_CON(6), 9, GFLAGS),
	GATE(PCLK_OTP_NS, "pclk_otp_ns", "pclk_bus", 0, RK3308_CLKGATE_CON(6), 10, GFLAGS),
	GATE(PCLK_GPIO0, "pclk_gpio0", "pclk_bus", 0, RK3308_CLKGATE_CON(6), 12, GFLAGS),
	GATE(PCLK_GPIO1, "pclk_gpio1", "pclk_bus", 0, RK3308_CLKGATE_CON(6), 13, GFLAGS),
	GATE(PCLK_GPIO2, "pclk_gpio2", "pclk_bus", 0, RK3308_CLKGATE_CON(6), 14, GFLAGS),
	GATE(PCLK_GPIO3, "pclk_gpio3", "pclk_bus", 0, RK3308_CLKGATE_CON(6), 15, GFLAGS),
	GATE(PCLK_GPIO4, "pclk_gpio4", "pclk_bus", 0, RK3308_CLKGATE_CON(7), 0, GFLAGS),
	GATE(PCLK_SGRF, "pclk_sgrf", "pclk_bus", CLK_IGNORE_UNUSED, RK3308_CLKGATE_CON(7), 1, GFLAGS),
	GATE(PCLK_GRF, "pclk_grf", "pclk_bus", CLK_IGNORE_UNUSED, RK3308_CLKGATE_CON(7), 2, GFLAGS),
	GATE(PCLK_USBSD_DET, "pclk_usbsd_det", "pclk_bus", CLK_IGNORE_UNUSED, RK3308_CLKGATE_CON(7), 3, GFLAGS),
	GATE(PCLK_DDR_UPCTL, "pclk_ddr_upctl", "pclk_bus", CLK_IGNORE_UNUSED, RK3308_CLKGATE_CON(7), 4, GFLAGS),
	GATE(PCLK_DDR_MON, "pclk_ddr_mon", "pclk_bus", CLK_IGNORE_UNUSED, RK3308_CLKGATE_CON(7), 5, GFLAGS),
	GATE(PCLK_DDRPHY, "pclk_ddrphy", "pclk_bus", CLK_IGNORE_UNUSED, RK3308_CLKGATE_CON(7), 6, GFLAGS),
	GATE(PCLK_DDR_STDBY, "pclk_ddr_stdby", "pclk_bus", CLK_IGNORE_UNUSED, RK3308_CLKGATE_CON(7), 7, GFLAGS),
	GATE(PCLK_USB_GRF, "pclk_usb_grf", "pclk_bus", CLK_IGNORE_UNUSED, RK3308_CLKGATE_CON(7), 8, GFLAGS),
	GATE(PCLK_CRU, "pclk_cru", "pclk_bus", CLK_IGNORE_UNUSED, RK3308_CLKGATE_CON(7), 9, GFLAGS),
	GATE(PCLK_OTP_PHY, "pclk_otp_phy", "pclk_bus", 0, RK3308_CLKGATE_CON(7), 10, GFLAGS),
	GATE(PCLK_CPU_BOOST, "pclk_cpu_boost", "pclk_bus", CLK_IGNORE_UNUSED, RK3308_CLKGATE_CON(7), 11, GFLAGS),
};

static const char *const rk3308_critical_clocks[] __initconst = {
	"aclk_bus",
	"hclk_bus",
	"pclk_bus",
	"aclk_peri",
	"hclk_peri",
	"pclk_peri",
	"hclk_audio",
	"pclk_audio",
};

static void __iomem *rk3308_cru_base;

void rk3308_dump_cru(void)
{
	if (rk3308_cru_base) {
		pr_warn("CRU:\n");
		print_hex_dump(KERN_WARNING, "", DUMP_PREFIX_OFFSET,
			       32, 4, rk3308_cru_base,
			       0x500, false);
	}
}
EXPORT_SYMBOL_GPL(rk3308_dump_cru);

static int rk3308_clk_panic(struct notifier_block *this,
			    unsigned long ev, void *ptr)
{
	rk3308_dump_cru();
	return NOTIFY_DONE;
}

static struct notifier_block rk3308_clk_panic_block = {
	.notifier_call = rk3308_clk_panic,
};

static void __init rk3308_clk_init(struct device_node *np)
{
	struct rockchip_clk_provider *ctx;
	void __iomem *reg_base;
	struct clk *clk;

	reg_base = of_iomap(np, 0);
	if (!reg_base) {
		pr_err("%s: could not map cru region\n", __func__);
		return;
	}

	rk3308_cru_base = reg_base;

	ctx = rockchip_clk_init(np, reg_base, CLK_NR_CLKS);
	if (IS_ERR(ctx)) {
		pr_err("%s: rockchip clk init failed\n", __func__);
		iounmap(reg_base);
		return;
	}

	/* aclk_dmac0 is controlled by sgrf. */
	clk = clk_register_fixed_factor(NULL, "aclk_dmac0", "aclk_bus", 0, 1, 1);
	if (IS_ERR(clk))
		pr_warn("%s: could not register clock aclk_dmac0: %ld\n",
			__func__, PTR_ERR(clk));
	else
		rockchip_clk_add_lookup(ctx, clk, ACLK_DMAC0);

	/* aclk_dmac1 is controlled by sgrf. */
	clk = clk_register_fixed_factor(NULL, "aclk_dmac1", "aclk_bus", 0, 1, 1);
	if (IS_ERR(clk))
		pr_warn("%s: could not register clock aclk_dmac1: %ld\n",
			__func__, PTR_ERR(clk));
	else
		rockchip_clk_add_lookup(ctx, clk, ACLK_DMAC1);

	/* watchdog pclk is controlled by sgrf. */
	clk = clk_register_fixed_factor(NULL, "pclk_wdt", "pclk_bus", 0, 1, 1);
	if (IS_ERR(clk))
		pr_warn("%s: could not register clock pclk_wdt: %ld\n",
			__func__, PTR_ERR(clk));
	else
		rockchip_clk_add_lookup(ctx, clk, PCLK_WDT);

	rockchip_clk_register_plls(ctx, rk3308_pll_clks,
				   ARRAY_SIZE(rk3308_pll_clks),
				   RK3308_GRF_SOC_STATUS0);
	rockchip_clk_register_branches(ctx, rk3308_clk_branches,
				       ARRAY_SIZE(rk3308_clk_branches));
	rockchip_clk_protect_critical(rk3308_critical_clocks,
				      ARRAY_SIZE(rk3308_critical_clocks));

	rockchip_clk_register_armclk(ctx, ARMCLK, "armclk",
				     mux_armclk_p, ARRAY_SIZE(mux_armclk_p),
				     &rk3308_cpuclk_data, rk3308_cpuclk_rates,
				     ARRAY_SIZE(rk3308_cpuclk_rates));

	rockchip_register_softrst(np, 10, reg_base + RK3308_SOFTRST_CON(0),
				  ROCKCHIP_SOFTRST_HIWORD_MASK);

	rockchip_register_restart_notifier(ctx, RK3308_GLB_SRST_FST, NULL);

	rockchip_clk_of_add_provider(np, ctx);

	atomic_notifier_chain_register(&panic_notifier_list,
				       &rk3308_clk_panic_block);
}

CLK_OF_DECLARE(rk3308_cru, "rockchip,rk3308-cru", rk3308_clk_init);
