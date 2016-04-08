/*
 * Copyright (c) 2015 Heiko Stuebner <heiko@sntech.de>
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
#include <linux/platform_device.h>
#include <dt-bindings/clock/rk3368-cru.h>
#include "clk.h"

#define RK3368_GRF_SOC_STATUS0	0x480

enum rk3368_plls {
	apllb, aplll, dpll, cpll, gpll, npll,
};

static struct rockchip_pll_rate_table rk3368_pll_rates[] = {
	RK3066_PLL_RATE(2208000000, 1, 92, 1),
	RK3066_PLL_RATE(2184000000, 1, 91, 1),
	RK3066_PLL_RATE(2160000000, 1, 90, 1),
	RK3066_PLL_RATE(2136000000, 1, 89, 1),
	RK3066_PLL_RATE(2112000000, 1, 88, 1),
	RK3066_PLL_RATE(2088000000, 1, 87, 1),
	RK3066_PLL_RATE(2064000000, 1, 86, 1),
	RK3066_PLL_RATE(2040000000, 1, 85, 1),
	RK3066_PLL_RATE(2016000000, 1, 84, 1),
	RK3066_PLL_RATE(1992000000, 1, 83, 1),
	RK3066_PLL_RATE(1968000000, 1, 82, 1),
	RK3066_PLL_RATE(1944000000, 1, 81, 1),
	RK3066_PLL_RATE(1920000000, 1, 80, 1),
	RK3066_PLL_RATE(1896000000, 1, 79, 1),
	RK3066_PLL_RATE(1872000000, 1, 78, 1),
	RK3066_PLL_RATE(1848000000, 1, 77, 1),
	RK3066_PLL_RATE(1824000000, 1, 76, 1),
	RK3066_PLL_RATE(1800000000, 1, 75, 1),
	RK3066_PLL_RATE(1776000000, 1, 74, 1),
	RK3066_PLL_RATE(1752000000, 1, 73, 1),
	RK3066_PLL_RATE(1728000000, 1, 72, 1),
	RK3066_PLL_RATE(1704000000, 1, 71, 1),
	RK3066_PLL_RATE(1680000000, 1, 70, 1),
	RK3066_PLL_RATE(1656000000, 1, 69, 1),
	RK3066_PLL_RATE(1632000000, 1, 68, 1),
	RK3066_PLL_RATE(1608000000, 1, 67, 1),
	RK3066_PLL_RATE(1560000000, 1, 65, 1),
	RK3066_PLL_RATE(1512000000, 1, 63, 1),
	RK3066_PLL_RATE(1488000000, 1, 62, 1),
	RK3066_PLL_RATE(1464000000, 1, 61, 1),
	RK3066_PLL_RATE(1440000000, 1, 60, 1),
	RK3066_PLL_RATE(1416000000, 1, 59, 1),
	RK3066_PLL_RATE(1392000000, 1, 58, 1),
	RK3066_PLL_RATE(1368000000, 1, 57, 1),
	RK3066_PLL_RATE(1344000000, 1, 56, 1),
	RK3066_PLL_RATE(1320000000, 1, 55, 1),
	RK3066_PLL_RATE(1296000000, 1, 54, 1),
	RK3066_PLL_RATE(1272000000, 1, 53, 1),
	RK3066_PLL_RATE(1248000000, 1, 52, 1),
	RK3066_PLL_RATE(1224000000, 1, 51, 1),
	RK3066_PLL_RATE(1200000000, 1, 50, 1),
	RK3066_PLL_RATE(1176000000, 1, 49, 1),
	RK3066_PLL_RATE(1128000000, 1, 47, 1),
	RK3066_PLL_RATE(1104000000, 1, 46, 1),
	RK3066_PLL_RATE(1008000000, 1, 84, 2),
	RK3066_PLL_RATE( 912000000, 1, 76, 2),
	RK3066_PLL_RATE( 888000000, 1, 74, 2),
	RK3066_PLL_RATE( 816000000, 1, 68, 2),
	RK3066_PLL_RATE( 792000000, 1, 66, 2),
	RK3066_PLL_RATE( 696000000, 1, 58, 2),
	RK3066_PLL_RATE( 672000000, 1, 56, 2),
	RK3066_PLL_RATE( 648000000, 1, 54, 2),
	RK3066_PLL_RATE( 624000000, 1, 52, 2),
	RK3066_PLL_RATE( 600000000, 1, 50, 2),
	RK3066_PLL_RATE( 576000000, 1, 48, 2),
	RK3066_PLL_RATE( 552000000, 1, 46, 2),
	RK3066_PLL_RATE( 528000000, 1, 88, 4),
	RK3066_PLL_RATE( 504000000, 1, 84, 4),
	RK3066_PLL_RATE( 480000000, 1, 80, 4),
	RK3066_PLL_RATE( 456000000, 1, 76, 4),
	RK3066_PLL_RATE( 408000000, 1, 68, 4),
	RK3066_PLL_RATE( 312000000, 1, 52, 4),
	RK3066_PLL_RATE( 252000000, 1, 84, 8),
	RK3066_PLL_RATE( 216000000, 1, 72, 8),
	RK3066_PLL_RATE( 126000000, 2, 84, 8),
	RK3066_PLL_RATE(  48000000, 2, 32, 8),
	{ /* sentinel */ },
};

PNAME(mux_pll_p)		= { "xin24m", "xin32k" };
PNAME(mux_armclkb_p)		= { "apllb_core", "gpllb_core" };
PNAME(mux_armclkl_p)		= { "aplll_core", "gplll_core" };
PNAME(mux_ddrphy_p)		= { "dpll_ddr", "gpll_ddr" };
PNAME(mux_cs_src_p)		= { "apllb_cs", "aplll_cs", "gpll_cs"};
PNAME(mux_aclk_bus_src_p)	= { "cpll_aclk_bus", "gpll_aclk_bus" };

PNAME(mux_pll_src_cpll_gpll_p)		= { "cpll", "gpll" };
PNAME(mux_pll_src_cpll_gpll_npll_p)	= { "cpll", "gpll", "npll" };
PNAME(mux_pll_src_npll_cpll_gpll_p)	= { "npll", "cpll", "gpll" };
PNAME(mux_pll_src_cpll_gpll_usb_p)	= { "cpll", "gpll", "usbphy_480m" };
PNAME(mux_pll_src_cpll_gpll_usb_usb_p)	= { "cpll", "gpll", "usbphy_480m",
					    "usbphy_480m" };
PNAME(mux_pll_src_cpll_gpll_usb_npll_p)	= { "cpll", "gpll", "usbphy_480m",
					    "npll" };
PNAME(mux_pll_src_cpll_gpll_npll_npll_p) = { "cpll", "gpll", "npll", "npll" };
PNAME(mux_pll_src_cpll_gpll_npll_usb_p) = { "cpll", "gpll", "npll",
					    "usbphy_480m" };

PNAME(mux_i2s_8ch_pre_p)	= { "i2s_8ch_src", "i2s_8ch_frac",
				    "ext_i2s", "xin12m" };
PNAME(mux_i2s_8ch_clkout_p)	= { "i2s_8ch_pre", "xin12m" };
PNAME(mux_i2s_2ch_p)		= { "i2s_2ch_src", "i2s_2ch_frac",
				    "dummy", "xin12m" };
PNAME(mux_spdif_8ch_p)		= { "spdif_8ch_pre", "spdif_8ch_frac",
				    "ext_i2s", "xin12m" };
PNAME(mux_edp_24m_p)		= { "xin24m", "dummy" };
PNAME(mux_vip_out_p)		= { "vip_src", "xin24m" };
PNAME(mux_usbphy480m_p)		= { "usbotg_out", "xin24m" };
PNAME(mux_hsic_usbphy480m_p)	= { "usbotg_out", "dummy" };
PNAME(mux_hsicphy480m_p)	= { "cpll", "gpll", "usbphy_480m" };
PNAME(mux_uart0_p)		= { "uart0_src", "uart0_frac", "xin24m" };
PNAME(mux_uart1_p)		= { "uart1_src", "uart1_frac", "xin24m" };
PNAME(mux_uart2_p)		= { "uart2_src", "xin24m" };
PNAME(mux_uart3_p)		= { "uart3_src", "uart3_frac", "xin24m" };
PNAME(mux_uart4_p)		= { "uart4_src", "uart4_frac", "xin24m" };
PNAME(mux_mac_p)		= { "mac_pll_src", "ext_gmac" };
PNAME(mux_mmc_src_p)		= { "cpll", "gpll", "usbphy_480m", "xin24m" };

static struct rockchip_pll_clock rk3368_pll_clks[] __initdata = {
	[apllb] = PLL(pll_rk3066, PLL_APLLB, "apllb", mux_pll_p, 0, RK3368_PLL_CON(0),
		     RK3368_PLL_CON(3), 8, 1, 0, rk3368_pll_rates),
	[aplll] = PLL(pll_rk3066, PLL_APLLL, "aplll", mux_pll_p, 0, RK3368_PLL_CON(4),
		     RK3368_PLL_CON(7), 8, 0, 0, rk3368_pll_rates),
	[dpll] = PLL(pll_rk3066, PLL_DPLL, "dpll", mux_pll_p, 0, RK3368_PLL_CON(8),
		     RK3368_PLL_CON(11), 8, 2, 0, NULL),
	[cpll] = PLL(pll_rk3066, PLL_CPLL, "cpll", mux_pll_p, 0, RK3368_PLL_CON(12),
		     RK3368_PLL_CON(15), 8, 3, ROCKCHIP_PLL_SYNC_RATE, rk3368_pll_rates),
	[gpll] = PLL(pll_rk3066, PLL_GPLL, "gpll", mux_pll_p, 0, RK3368_PLL_CON(16),
		     RK3368_PLL_CON(19), 8, 4, ROCKCHIP_PLL_SYNC_RATE, rk3368_pll_rates),
	[npll] = PLL(pll_rk3066, PLL_NPLL, "npll",  mux_pll_p, 0, RK3368_PLL_CON(20),
		     RK3368_PLL_CON(23), 8, 5, ROCKCHIP_PLL_SYNC_RATE, rk3368_pll_rates),
};

static struct clk_div_table div_ddrphy_t[] = {
	{ .val = 0, .div = 1 },
	{ .val = 1, .div = 2 },
	{ .val = 3, .div = 4 },
	{ /* sentinel */ },
};

#define MFLAGS CLK_MUX_HIWORD_MASK
#define DFLAGS CLK_DIVIDER_HIWORD_MASK
#define GFLAGS (CLK_GATE_HIWORD_MASK | CLK_GATE_SET_TO_DISABLE)
#define IFLAGS ROCKCHIP_INVERTER_HIWORD_MASK

static const struct rockchip_cpuclk_reg_data rk3368_cpuclkb_data = {
	.core_reg = RK3368_CLKSEL_CON(0),
	.div_core_shift = 0,
	.div_core_mask = 0x1f,
	.mux_core_shift = 7,
};

static const struct rockchip_cpuclk_reg_data rk3368_cpuclkl_data = {
	.core_reg = RK3368_CLKSEL_CON(2),
	.div_core_shift = 0,
	.div_core_mask = 0x1f,
	.mux_core_shift = 7,
};

#define RK3368_DIV_ACLKM_MASK		0x1f
#define RK3368_DIV_ACLKM_SHIFT		8
#define RK3368_DIV_ATCLK_MASK		0x1f
#define RK3368_DIV_ATCLK_SHIFT		0
#define RK3368_DIV_PCLK_DBG_MASK	0x1f
#define RK3368_DIV_PCLK_DBG_SHIFT	8

#define RK3368_CLKSEL0(_offs, _aclkm)					\
	{								\
		.reg = RK3368_CLKSEL_CON(0 + _offs),			\
		.val = HIWORD_UPDATE(_aclkm, RK3368_DIV_ACLKM_MASK,	\
				RK3368_DIV_ACLKM_SHIFT),		\
	}
#define RK3368_CLKSEL1(_offs, _atclk, _pdbg)				\
	{								\
		.reg = RK3368_CLKSEL_CON(1 + _offs),			\
		.val = HIWORD_UPDATE(_atclk, RK3368_DIV_ATCLK_MASK,	\
				RK3368_DIV_ATCLK_SHIFT) |		\
		       HIWORD_UPDATE(_pdbg, RK3368_DIV_PCLK_DBG_MASK,	\
				RK3368_DIV_PCLK_DBG_SHIFT),		\
	}

/* cluster_b: aclkm in clksel0, rest in clksel1 */
#define RK3368_CPUCLKB_RATE(_prate, _aclkm, _atclk, _pdbg)		\
	{								\
		.prate = _prate,					\
		.divs = {						\
			RK3368_CLKSEL0(0, _aclkm),			\
			RK3368_CLKSEL1(0, _atclk, _pdbg),		\
		},							\
	}

/* cluster_l: aclkm in clksel2, rest in clksel3 */
#define RK3368_CPUCLKL_RATE(_prate, _aclkm, _atclk, _pdbg)		\
	{								\
		.prate = _prate,					\
		.divs = {						\
			RK3368_CLKSEL0(2, _aclkm),			\
			RK3368_CLKSEL1(2, _atclk, _pdbg),		\
		},							\
	}

static struct rockchip_cpuclk_rate_table rk3368_cpuclkb_rates[] __initdata = {
	RK3368_CPUCLKB_RATE(1512000000, 1, 5, 5),
	RK3368_CPUCLKB_RATE(1488000000, 1, 4, 4),
	RK3368_CPUCLKB_RATE(1416000000, 1, 4, 4),
	RK3368_CPUCLKB_RATE(1200000000, 1, 3, 3),
	RK3368_CPUCLKB_RATE(1008000000, 1, 3, 3),
	RK3368_CPUCLKB_RATE( 816000000, 1, 2, 2),
	RK3368_CPUCLKB_RATE( 696000000, 1, 2, 2),
	RK3368_CPUCLKB_RATE( 600000000, 1, 1, 1),
	RK3368_CPUCLKB_RATE( 408000000, 1, 1, 1),
	RK3368_CPUCLKB_RATE( 312000000, 1, 1, 1),
};

static struct rockchip_cpuclk_rate_table rk3368_cpuclkl_rates[] __initdata = {
	RK3368_CPUCLKL_RATE(1512000000, 1, 6, 6),
	RK3368_CPUCLKL_RATE(1488000000, 1, 5, 5),
	RK3368_CPUCLKL_RATE(1416000000, 1, 5, 5),
	RK3368_CPUCLKL_RATE(1200000000, 1, 4, 4),
	RK3368_CPUCLKL_RATE(1008000000, 1, 4, 4),
	RK3368_CPUCLKL_RATE( 816000000, 1, 3, 3),
	RK3368_CPUCLKL_RATE( 696000000, 1, 2, 2),
	RK3368_CPUCLKL_RATE( 600000000, 1, 2, 2),
	RK3368_CPUCLKL_RATE( 408000000, 1, 1, 1),
	RK3368_CPUCLKL_RATE( 312000000, 1, 1, 1),
};

static struct rockchip_clk_branch rk3368_i2s_8ch_fracmux __initdata =
	MUX(0, "i2s_8ch_pre", mux_i2s_8ch_pre_p, CLK_SET_RATE_PARENT,
	    RK3368_CLKSEL_CON(27), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3368_spdif_8ch_fracmux __initdata =
	MUX(0, "spdif_8ch_pre", mux_spdif_8ch_p, CLK_SET_RATE_PARENT,
	    RK3368_CLKSEL_CON(31), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3368_i2s_2ch_fracmux __initdata =
	MUX(0, "i2s_2ch_pre", mux_i2s_2ch_p, CLK_SET_RATE_PARENT,
	    RK3368_CLKSEL_CON(53), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3368_uart0_fracmux __initdata =
	MUX(SCLK_UART0, "sclk_uart0", mux_uart0_p, CLK_SET_RATE_PARENT,
	    RK3368_CLKSEL_CON(33), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3368_uart1_fracmux __initdata =
	MUX(SCLK_UART1, "sclk_uart1", mux_uart1_p, CLK_SET_RATE_PARENT,
	    RK3368_CLKSEL_CON(35), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3368_uart3_fracmux __initdata =
	MUX(SCLK_UART3, "sclk_uart3", mux_uart3_p, CLK_SET_RATE_PARENT,
	    RK3368_CLKSEL_CON(39), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3368_uart4_fracmux __initdata =
	MUX(SCLK_UART4, "sclk_uart4", mux_uart4_p, CLK_SET_RATE_PARENT,
	    RK3368_CLKSEL_CON(41), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3368_clk_branches[] __initdata = {
	/*
	 * Clock-Architecture Diagram 2
	 */

	FACTOR(0, "xin12m", "xin24m", 0, 1, 2),

	MUX(SCLK_USBPHY480M, "usbphy_480m", mux_usbphy480m_p, CLK_SET_RATE_PARENT,
			RK3368_CLKSEL_CON(13), 8, 1, MFLAGS),

	GATE(0, "apllb_core", "apllb", CLK_IGNORE_UNUSED,
			RK3368_CLKGATE_CON(0), 0, GFLAGS),
	GATE(0, "gpllb_core", "gpll", CLK_IGNORE_UNUSED,
			RK3368_CLKGATE_CON(0), 1, GFLAGS),

	GATE(0, "aplll_core", "aplll", CLK_IGNORE_UNUSED,
			RK3368_CLKGATE_CON(0), 4, GFLAGS),
	GATE(0, "gplll_core", "gpll", CLK_IGNORE_UNUSED,
			RK3368_CLKGATE_CON(0), 5, GFLAGS),

	DIV(0, "aclkm_core_b", "armclkb", 0,
			RK3368_CLKSEL_CON(0), 8, 5, DFLAGS | CLK_DIVIDER_READ_ONLY),
	DIV(0, "atclk_core_b", "armclkb", 0,
			RK3368_CLKSEL_CON(1), 0, 5, DFLAGS | CLK_DIVIDER_READ_ONLY),
	DIV(0, "pclk_dbg_b", "armclkb", 0,
			RK3368_CLKSEL_CON(1), 8, 5, DFLAGS | CLK_DIVIDER_READ_ONLY),

	DIV(0, "aclkm_core_l", "armclkl", 0,
			RK3368_CLKSEL_CON(2), 8, 5, DFLAGS | CLK_DIVIDER_READ_ONLY),
	DIV(0, "atclk_core_l", "armclkl", 0,
			RK3368_CLKSEL_CON(3), 0, 5, DFLAGS | CLK_DIVIDER_READ_ONLY),
	DIV(0, "pclk_dbg_l", "armclkl", 0,
			RK3368_CLKSEL_CON(3), 8, 5, DFLAGS | CLK_DIVIDER_READ_ONLY),

	GATE(0, "apllb_cs", "apllb", CLK_IGNORE_UNUSED,
			RK3368_CLKGATE_CON(0), 9, GFLAGS),
	GATE(0, "aplll_cs", "aplll", CLK_IGNORE_UNUSED,
			RK3368_CLKGATE_CON(0), 10, GFLAGS),
	GATE(0, "gpll_cs", "gpll", CLK_IGNORE_UNUSED,
			RK3368_CLKGATE_CON(0), 8, GFLAGS),
	COMPOSITE_NOGATE(0, "sclk_cs_pre", mux_cs_src_p, CLK_IGNORE_UNUSED,
			RK3368_CLKSEL_CON(4), 6, 2, MFLAGS, 0, 5, DFLAGS),
	COMPOSITE_NOMUX(0, "clkin_trace", "sclk_cs_pre", CLK_IGNORE_UNUSED,
			RK3368_CLKSEL_CON(4), 8, 5, DFLAGS,
			RK3368_CLKGATE_CON(0), 13, GFLAGS),

	COMPOSITE(0, "aclk_cci_pre", mux_pll_src_cpll_gpll_usb_npll_p, CLK_IGNORE_UNUSED,
			RK3368_CLKSEL_CON(5), 6, 2, MFLAGS, 0, 7, DFLAGS,
			RK3368_CLKGATE_CON(0), 12, GFLAGS),
	GATE(SCLK_PVTM_CORE, "sclk_pvtm_core", "xin24m", 0, RK3368_CLKGATE_CON(7), 10, GFLAGS),

	GATE(0, "dpll_ddr", "dpll", CLK_IGNORE_UNUSED,
			RK3368_CLKGATE_CON(1), 8, GFLAGS),
	GATE(0, "gpll_ddr", "gpll", 0,
			RK3368_CLKGATE_CON(1), 9, GFLAGS),
	COMPOSITE_NOGATE_DIVTBL(0, "ddrphy_src", mux_ddrphy_p, CLK_IGNORE_UNUSED,
			RK3368_CLKSEL_CON(13), 4, 1, MFLAGS, 0, 2, DFLAGS, div_ddrphy_t),

	FACTOR_GATE(0, "sclk_ddr", "ddrphy_src", CLK_IGNORE_UNUSED, 1, 4,
			RK3368_CLKGATE_CON(6), 14, GFLAGS),
	GATE(0, "sclk_ddr4x", "ddrphy_src", CLK_IGNORE_UNUSED,
			RK3368_CLKGATE_CON(6), 15, GFLAGS),

	GATE(0, "gpll_aclk_bus", "gpll", CLK_IGNORE_UNUSED,
			RK3368_CLKGATE_CON(1), 10, GFLAGS),
	GATE(0, "cpll_aclk_bus", "cpll", CLK_IGNORE_UNUSED,
			RK3368_CLKGATE_CON(1), 11, GFLAGS),
	COMPOSITE_NOGATE(0, "aclk_bus_src", mux_aclk_bus_src_p, CLK_IGNORE_UNUSED,
			RK3368_CLKSEL_CON(8), 7, 1, MFLAGS, 0, 5, DFLAGS),

	GATE(ACLK_BUS, "aclk_bus", "aclk_bus_src", CLK_IGNORE_UNUSED,
			RK3368_CLKGATE_CON(1), 0, GFLAGS),
	COMPOSITE_NOMUX(PCLK_BUS, "pclk_bus", "aclk_bus_src", CLK_IGNORE_UNUSED,
			RK3368_CLKSEL_CON(8), 12, 3, DFLAGS,
			RK3368_CLKGATE_CON(1), 2, GFLAGS),
	COMPOSITE_NOMUX(HCLK_BUS, "hclk_bus", "aclk_bus_src", CLK_IGNORE_UNUSED,
			RK3368_CLKSEL_CON(8), 8, 2, DFLAGS,
			RK3368_CLKGATE_CON(1), 1, GFLAGS),
	COMPOSITE_NOMUX(0, "sclk_crypto", "aclk_bus_src", 0,
			RK3368_CLKSEL_CON(10), 14, 2, DFLAGS,
			RK3368_CLKGATE_CON(7), 2, GFLAGS),

	COMPOSITE(0, "fclk_mcu_src", mux_pll_src_cpll_gpll_p, CLK_IGNORE_UNUSED,
			RK3368_CLKSEL_CON(12), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RK3368_CLKGATE_CON(1), 3, GFLAGS),
	/*
	 * stclk_mcu is listed as child of fclk_mcu_src in diagram 5,
	 * but stclk_mcu has an additional own divider in diagram 2
	 */
	COMPOSITE_NOMUX(0, "stclk_mcu", "fclk_mcu_src", 0,
			RK3368_CLKSEL_CON(12), 8, 3, DFLAGS,
			RK3368_CLKGATE_CON(13), 13, GFLAGS),

	COMPOSITE(0, "i2s_8ch_src", mux_pll_src_cpll_gpll_p, 0,
			RK3368_CLKSEL_CON(27), 12, 1, MFLAGS, 0, 7, DFLAGS,
			RK3368_CLKGATE_CON(6), 1, GFLAGS),
	COMPOSITE_FRACMUX(0, "i2s_8ch_frac", "i2s_8ch_src", CLK_SET_RATE_PARENT,
			  RK3368_CLKSEL_CON(28), 0,
			  RK3368_CLKGATE_CON(6), 2, GFLAGS,
			  &rk3368_i2s_8ch_fracmux),
	COMPOSITE_NODIV(SCLK_I2S_8CH_OUT, "i2s_8ch_clkout", mux_i2s_8ch_clkout_p, 0,
			RK3368_CLKSEL_CON(27), 15, 1, MFLAGS,
			RK3368_CLKGATE_CON(6), 0, GFLAGS),
	GATE(SCLK_I2S_8CH, "sclk_i2s_8ch", "i2s_8ch_pre", CLK_SET_RATE_PARENT,
			RK3368_CLKGATE_CON(6), 3, GFLAGS),
	COMPOSITE(0, "spdif_8ch_src", mux_pll_src_cpll_gpll_p, 0,
			RK3368_CLKSEL_CON(31), 12, 1, MFLAGS, 0, 7, DFLAGS,
			RK3368_CLKGATE_CON(6), 4, GFLAGS),
	COMPOSITE_FRACMUX(0, "spdif_8ch_frac", "spdif_8ch_src", CLK_SET_RATE_PARENT,
			  RK3368_CLKSEL_CON(32), 0,
			  RK3368_CLKGATE_CON(6), 5, GFLAGS,
			  &rk3368_spdif_8ch_fracmux),
	GATE(SCLK_SPDIF_8CH, "sclk_spdif_8ch", "spdif_8ch_pre", CLK_SET_RATE_PARENT,
	     RK3368_CLKGATE_CON(6), 6, GFLAGS),
	COMPOSITE(0, "i2s_2ch_src", mux_pll_src_cpll_gpll_p, 0,
			RK3368_CLKSEL_CON(53), 12, 1, MFLAGS, 0, 7, DFLAGS,
			RK3368_CLKGATE_CON(5), 13, GFLAGS),
	COMPOSITE_FRACMUX(0, "i2s_2ch_frac", "i2s_2ch_src", CLK_SET_RATE_PARENT,
			  RK3368_CLKSEL_CON(54), 0,
			  RK3368_CLKGATE_CON(5), 14, GFLAGS,
			  &rk3368_i2s_2ch_fracmux),
	GATE(SCLK_I2S_2CH, "sclk_i2s_2ch", "i2s_2ch_pre", CLK_SET_RATE_PARENT,
	     RK3368_CLKGATE_CON(5), 15, GFLAGS),

	COMPOSITE(0, "sclk_tsp", mux_pll_src_cpll_gpll_npll_p, 0,
			RK3368_CLKSEL_CON(46), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3368_CLKGATE_CON(6), 12, GFLAGS),
	GATE(0, "sclk_hsadc_tsp", "ext_hsadc_tsp", 0,
			RK3368_CLKGATE_CON(13), 7, GFLAGS),

	MUX(0, "uart_src", mux_pll_src_cpll_gpll_p, 0,
			RK3368_CLKSEL_CON(35), 12, 1, MFLAGS),
	COMPOSITE_NOMUX(0, "uart2_src", "uart_src", 0,
			RK3368_CLKSEL_CON(37), 0, 7, DFLAGS,
			RK3368_CLKGATE_CON(2), 4, GFLAGS),
	MUX(SCLK_UART2, "sclk_uart2", mux_uart2_p, CLK_SET_RATE_PARENT,
			RK3368_CLKSEL_CON(37), 8, 1, MFLAGS),

	/*
	 * Clock-Architecture Diagram 3
	 */

	COMPOSITE(0, "aclk_vepu", mux_pll_src_cpll_gpll_npll_usb_p, 0,
			RK3368_CLKSEL_CON(15), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3368_CLKGATE_CON(4), 6, GFLAGS),
	COMPOSITE(0, "aclk_vdpu", mux_pll_src_cpll_gpll_npll_usb_p, 0,
			RK3368_CLKSEL_CON(15), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RK3368_CLKGATE_CON(4), 7, GFLAGS),

	/*
	 * We use aclk_vdpu by default ---GRF_SOC_CON0[7] setting in system,
	 * so we ignore the mux and make clocks nodes as following,
	 */
	FACTOR_GATE(0, "hclk_video_pre", "aclk_vdpu", 0, 1, 4,
		RK3368_CLKGATE_CON(4), 8, GFLAGS),

	COMPOSITE(0, "sclk_hevc_cabac_src", mux_pll_src_cpll_gpll_npll_usb_p, 0,
			RK3368_CLKSEL_CON(17), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3368_CLKGATE_CON(5), 1, GFLAGS),
	COMPOSITE(0, "sclk_hevc_core_src", mux_pll_src_cpll_gpll_npll_usb_p, 0,
			RK3368_CLKSEL_CON(17), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RK3368_CLKGATE_CON(5), 2, GFLAGS),

	COMPOSITE(0, "aclk_vio0", mux_pll_src_cpll_gpll_usb_p, CLK_IGNORE_UNUSED,
			RK3368_CLKSEL_CON(19), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3368_CLKGATE_CON(4), 0, GFLAGS),
	DIV(0, "hclk_vio", "aclk_vio0", 0,
			RK3368_CLKSEL_CON(21), 0, 5, DFLAGS),

	COMPOSITE(0, "aclk_rga_pre", mux_pll_src_cpll_gpll_usb_p, 0,
			RK3368_CLKSEL_CON(18), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RK3368_CLKGATE_CON(4), 3, GFLAGS),
	COMPOSITE(SCLK_RGA, "sclk_rga", mux_pll_src_cpll_gpll_usb_p, 0,
			RK3368_CLKSEL_CON(18), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3368_CLKGATE_CON(4), 4, GFLAGS),

	COMPOSITE(DCLK_VOP, "dclk_vop", mux_pll_src_cpll_gpll_npll_p, 0,
			RK3368_CLKSEL_CON(20), 8, 2, MFLAGS, 0, 8, DFLAGS,
			RK3368_CLKGATE_CON(4), 1, GFLAGS),

	GATE(SCLK_VOP0_PWM, "sclk_vop0_pwm", "xin24m", 0,
			RK3368_CLKGATE_CON(4), 2, GFLAGS),

	COMPOSITE(SCLK_ISP, "sclk_isp", mux_pll_src_cpll_gpll_npll_npll_p, 0,
			RK3368_CLKSEL_CON(22), 6, 2, MFLAGS, 0, 6, DFLAGS,
			RK3368_CLKGATE_CON(4), 9, GFLAGS),

	GATE(0, "pclk_isp_in", "ext_isp", 0,
			RK3368_CLKGATE_CON(17), 2, GFLAGS),
	INVERTER(PCLK_ISP, "pclk_isp", "pclk_isp_in",
			RK3368_CLKSEL_CON(21), 6, IFLAGS),

	GATE(0, "pclk_vip_in", "ext_vip", 0,
			RK3368_CLKGATE_CON(16), 13, GFLAGS),
	INVERTER(PCLK_VIP, "pclk_vip", "pclk_vip_in",
			RK3368_CLKSEL_CON(21), 13, IFLAGS),

	GATE(SCLK_HDMI_HDCP, "sclk_hdmi_hdcp", "xin24m", 0,
			RK3368_CLKGATE_CON(4), 13, GFLAGS),
	GATE(SCLK_HDMI_CEC, "sclk_hdmi_cec", "xin32k", 0,
			RK3368_CLKGATE_CON(4), 12, GFLAGS),

	COMPOSITE_NODIV(0, "vip_src", mux_pll_src_cpll_gpll_p, 0,
			RK3368_CLKSEL_CON(21), 15, 1, MFLAGS,
			RK3368_CLKGATE_CON(4), 5, GFLAGS),
	COMPOSITE_NOGATE(0, "sclk_vip_out", mux_vip_out_p, 0,
			RK3368_CLKSEL_CON(21), 14, 1, MFLAGS, 8, 5, DFLAGS),

	COMPOSITE_NODIV(SCLK_EDP_24M, "sclk_edp_24m", mux_edp_24m_p, 0,
			RK3368_CLKSEL_CON(23), 8, 1, MFLAGS,
			RK3368_CLKGATE_CON(5), 4, GFLAGS),
	COMPOSITE(SCLK_EDP, "sclk_edp", mux_pll_src_cpll_gpll_npll_npll_p, 0,
			RK3368_CLKSEL_CON(23), 6, 2, MFLAGS, 0, 6, DFLAGS,
			RK3368_CLKGATE_CON(5), 3, GFLAGS),

	COMPOSITE(SCLK_HDCP, "sclk_hdcp", mux_pll_src_cpll_gpll_npll_npll_p, 0,
			RK3368_CLKSEL_CON(55), 6, 2, MFLAGS, 0, 6, DFLAGS,
			RK3368_CLKGATE_CON(5), 5, GFLAGS),

	DIV(0, "pclk_pd_alive", "gpll", 0,
			RK3368_CLKSEL_CON(10), 8, 5, DFLAGS),

	/* sclk_timer has a gate in the sgrf */

	COMPOSITE_NOMUX(0, "pclk_pd_pmu", "gpll", CLK_IGNORE_UNUSED,
			RK3368_CLKSEL_CON(10), 0, 5, DFLAGS,
			RK3368_CLKGATE_CON(7), 9, GFLAGS),
	GATE(SCLK_PVTM_PMU, "sclk_pvtm_pmu", "xin24m", 0,
			RK3368_CLKGATE_CON(7), 3, GFLAGS),
	COMPOSITE(0, "sclk_gpu_core_src", mux_pll_src_cpll_gpll_usb_npll_p, 0,
			RK3368_CLKSEL_CON(14), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3368_CLKGATE_CON(4), 11, GFLAGS),
	MUX(0, "aclk_gpu_src", mux_pll_src_cpll_gpll_p, 0,
			RK3368_CLKSEL_CON(14), 14, 1, MFLAGS),
	COMPOSITE_NOMUX(0, "aclk_gpu_mem_pre", "aclk_gpu_src", 0,
			RK3368_CLKSEL_CON(14), 8, 5, DFLAGS,
			RK3368_CLKGATE_CON(5), 8, GFLAGS),
	COMPOSITE_NOMUX(0, "aclk_gpu_cfg_pre", "aclk_gpu_src", 0,
			RK3368_CLKSEL_CON(16), 8, 5, DFLAGS,
			RK3368_CLKGATE_CON(5), 9, GFLAGS),
	GATE(SCLK_PVTM_GPU, "sclk_pvtm_gpu", "xin24m", 0,
			RK3368_CLKGATE_CON(7), 11, GFLAGS),

	COMPOSITE(0, "aclk_peri_src", mux_pll_src_cpll_gpll_p, CLK_IGNORE_UNUSED,
			RK3368_CLKSEL_CON(9), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RK3368_CLKGATE_CON(3), 0, GFLAGS),
	COMPOSITE_NOMUX(PCLK_PERI, "pclk_peri", "aclk_peri_src", 0,
			RK3368_CLKSEL_CON(9), 12, 2, DFLAGS | CLK_DIVIDER_POWER_OF_TWO,
			RK3368_CLKGATE_CON(3), 3, GFLAGS),
	COMPOSITE_NOMUX(HCLK_PERI, "hclk_peri", "aclk_peri_src", CLK_IGNORE_UNUSED,
			RK3368_CLKSEL_CON(9), 8, 2, DFLAGS | CLK_DIVIDER_POWER_OF_TWO,
			RK3368_CLKGATE_CON(3), 2, GFLAGS),
	GATE(ACLK_PERI, "aclk_peri", "aclk_peri_src", CLK_IGNORE_UNUSED,
			RK3368_CLKGATE_CON(3), 1, GFLAGS),

	GATE(0, "sclk_mipidsi_24m", "xin24m", 0, RK3368_CLKGATE_CON(4), 14, GFLAGS),

	/*
	 * Clock-Architecture Diagram 4
	 */

	COMPOSITE(SCLK_SPI0, "sclk_spi0", mux_pll_src_cpll_gpll_p, 0,
			RK3368_CLKSEL_CON(45), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RK3368_CLKGATE_CON(3), 7, GFLAGS),
	COMPOSITE(SCLK_SPI1, "sclk_spi1", mux_pll_src_cpll_gpll_p, 0,
			RK3368_CLKSEL_CON(45), 15, 1, MFLAGS, 8, 7, DFLAGS,
			RK3368_CLKGATE_CON(3), 8, GFLAGS),
	COMPOSITE(SCLK_SPI2, "sclk_spi2", mux_pll_src_cpll_gpll_p, 0,
			RK3368_CLKSEL_CON(46), 15, 1, MFLAGS, 8, 7, DFLAGS,
			RK3368_CLKGATE_CON(3), 9, GFLAGS),


	COMPOSITE(SCLK_SDMMC, "sclk_sdmmc", mux_mmc_src_p, 0,
			RK3368_CLKSEL_CON(50), 8, 2, MFLAGS, 0, 7, DFLAGS,
			RK3368_CLKGATE_CON(7), 12, GFLAGS),
	COMPOSITE(SCLK_SDIO0, "sclk_sdio0", mux_mmc_src_p, 0,
			RK3368_CLKSEL_CON(48), 8, 2, MFLAGS, 0, 7, DFLAGS,
			RK3368_CLKGATE_CON(7), 13, GFLAGS),
	COMPOSITE(SCLK_EMMC, "sclk_emmc", mux_mmc_src_p, 0,
			RK3368_CLKSEL_CON(51), 8, 2, MFLAGS, 0, 7, DFLAGS,
			RK3368_CLKGATE_CON(7), 15, GFLAGS),

	MMC(SCLK_SDMMC_DRV,    "sdmmc_drv",    "sclk_sdmmc", RK3368_SDMMC_CON0, 1),
	MMC(SCLK_SDMMC_SAMPLE, "sdmmc_sample", "sclk_sdmmc", RK3368_SDMMC_CON1, 0),

	MMC(SCLK_SDIO0_DRV,    "sdio0_drv",    "sclk_sdio0", RK3368_SDIO0_CON0, 1),
	MMC(SCLK_SDIO0_SAMPLE, "sdio0_sample", "sclk_sdio0", RK3368_SDIO0_CON1, 0),

	MMC(SCLK_EMMC_DRV,     "emmc_drv",     "sclk_emmc",  RK3368_EMMC_CON0,  1),
	MMC(SCLK_EMMC_SAMPLE,  "emmc_sample",  "sclk_emmc",  RK3368_EMMC_CON1,  0),

	GATE(SCLK_OTGPHY0, "sclk_otgphy0", "xin24m", CLK_IGNORE_UNUSED,
			RK3368_CLKGATE_CON(8), 1, GFLAGS),

	/* pmu_grf_soc_con0[6] allows to select between xin32k and pvtm_pmu */
	GATE(SCLK_OTG_ADP, "sclk_otg_adp", "xin32k", CLK_IGNORE_UNUSED,
			RK3368_CLKGATE_CON(8), 4, GFLAGS),

	/* pmu_grf_soc_con0[6] allows to select between xin32k and pvtm_pmu */
	COMPOSITE_NOMUX(SCLK_TSADC, "sclk_tsadc", "xin32k", 0,
			RK3368_CLKSEL_CON(25), 0, 6, DFLAGS,
			RK3368_CLKGATE_CON(3), 5, GFLAGS),

	COMPOSITE_NOMUX(SCLK_SARADC, "sclk_saradc", "xin24m", 0,
			RK3368_CLKSEL_CON(25), 8, 8, DFLAGS,
			RK3368_CLKGATE_CON(3), 6, GFLAGS),

	COMPOSITE(SCLK_NANDC0, "sclk_nandc0", mux_pll_src_cpll_gpll_p, 0,
			RK3368_CLKSEL_CON(47), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RK3368_CLKGATE_CON(7), 8, GFLAGS),

	COMPOSITE(SCLK_SFC, "sclk_sfc", mux_pll_src_cpll_gpll_p, 0,
			RK3368_CLKSEL_CON(52), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RK3368_CLKGATE_CON(6), 7, GFLAGS),

	COMPOSITE(0, "uart0_src", mux_pll_src_cpll_gpll_usb_usb_p, 0,
			RK3368_CLKSEL_CON(33), 12, 2, MFLAGS, 0, 7, DFLAGS,
			RK3368_CLKGATE_CON(2), 0, GFLAGS),
	COMPOSITE_FRACMUX(0, "uart0_frac", "uart0_src", CLK_SET_RATE_PARENT,
			  RK3368_CLKSEL_CON(34), 0,
			  RK3368_CLKGATE_CON(2), 1, GFLAGS,
			  &rk3368_uart0_fracmux),

	COMPOSITE_NOMUX(0, "uart1_src", "uart_src", 0,
			RK3368_CLKSEL_CON(35), 0, 7, DFLAGS,
			RK3368_CLKGATE_CON(2), 2, GFLAGS),
	COMPOSITE_FRACMUX(0, "uart1_frac", "uart1_src", CLK_SET_RATE_PARENT,
			  RK3368_CLKSEL_CON(36), 0,
			  RK3368_CLKGATE_CON(2), 3, GFLAGS,
			  &rk3368_uart1_fracmux),

	COMPOSITE_NOMUX(0, "uart3_src", "uart_src", 0,
			RK3368_CLKSEL_CON(39), 0, 7, DFLAGS,
			RK3368_CLKGATE_CON(2), 6, GFLAGS),
	COMPOSITE_FRACMUX(0, "uart3_frac", "uart3_src", CLK_SET_RATE_PARENT,
			  RK3368_CLKSEL_CON(40), 0,
			  RK3368_CLKGATE_CON(2), 7, GFLAGS,
			  &rk3368_uart3_fracmux),

	COMPOSITE_NOMUX(0, "uart4_src", "uart_src", 0,
			RK3368_CLKSEL_CON(41), 0, 7, DFLAGS,
			RK3368_CLKGATE_CON(2), 8, GFLAGS),
	COMPOSITE_FRACMUX(0, "uart4_frac", "uart4_src", CLK_SET_RATE_PARENT,
			  RK3368_CLKSEL_CON(42), 0,
			  RK3368_CLKGATE_CON(2), 9, GFLAGS,
			  &rk3368_uart4_fracmux),

	COMPOSITE(0, "mac_pll_src", mux_pll_src_npll_cpll_gpll_p, 0,
			RK3368_CLKSEL_CON(43), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3368_CLKGATE_CON(3), 4, GFLAGS),
	MUX(SCLK_MAC, "mac_clk", mux_mac_p, CLK_SET_RATE_PARENT,
			RK3368_CLKSEL_CON(43), 8, 1, MFLAGS),
	GATE(SCLK_MACREF_OUT, "sclk_macref_out", "mac_clk", 0,
			RK3368_CLKGATE_CON(7), 7, GFLAGS),
	GATE(SCLK_MACREF, "sclk_macref", "mac_clk", 0,
			RK3368_CLKGATE_CON(7), 6, GFLAGS),
	GATE(SCLK_MAC_RX, "sclk_mac_rx", "mac_clk", 0,
			RK3368_CLKGATE_CON(7), 4, GFLAGS),
	GATE(SCLK_MAC_TX, "sclk_mac_tx", "mac_clk", 0,
			RK3368_CLKGATE_CON(7), 5, GFLAGS),

	GATE(0, "jtag", "ext_jtag", 0,
			RK3368_CLKGATE_CON(7), 0, GFLAGS),

	COMPOSITE_NODIV(0, "hsic_usbphy_480m", mux_hsic_usbphy480m_p, 0,
			RK3368_CLKSEL_CON(26), 8, 2, MFLAGS,
			RK3368_CLKGATE_CON(8), 0, GFLAGS),
	COMPOSITE_NODIV(SCLK_HSICPHY480M, "sclk_hsicphy480m", mux_hsicphy480m_p, 0,
			RK3368_CLKSEL_CON(26), 12, 2, MFLAGS,
			RK3368_CLKGATE_CON(8), 7, GFLAGS),
	GATE(SCLK_HSICPHY12M, "sclk_hsicphy12m", "xin12m", 0,
			RK3368_CLKGATE_CON(8), 6, GFLAGS),

	/*
	 * Clock-Architecture Diagram 5
	 */

	/* aclk_cci_pre gates */
	GATE(0, "aclk_core_niu_cpup", "aclk_cci_pre", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(11), 4, GFLAGS),
	GATE(0, "aclk_core_niu_cci", "aclk_cci_pre", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(11), 3, GFLAGS),
	GATE(0, "aclk_cci400", "aclk_cci_pre", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(11), 2, GFLAGS),
	GATE(0, "aclk_adb400m_pd_core_b", "aclk_cci_pre", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(11), 1, GFLAGS),
	GATE(0, "aclk_adb400m_pd_core_l", "aclk_cci_pre", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(11), 0, GFLAGS),

	/* aclkm_core_* gates */
	GATE(0, "aclk_adb400s_pd_core_b", "aclkm_core_b", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(10), 0, GFLAGS),
	GATE(0, "aclk_adb400s_pd_core_l", "aclkm_core_l", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(9), 0, GFLAGS),

	/* armclk* gates */
	GATE(0, "sclk_dbg_pd_core_b", "armclkb", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(10), 1, GFLAGS),
	GATE(0, "sclk_dbg_pd_core_l", "armclkl", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(9), 1, GFLAGS),

	/* sclk_cs_pre gates */
	GATE(0, "sclk_dbg", "sclk_cs_pre", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(11), 7, GFLAGS),
	GATE(0, "pclk_core_niu_sdbg", "sclk_cs_pre", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(11), 6, GFLAGS),
	GATE(0, "hclk_core_niu_dbg", "sclk_cs_pre", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(11), 5, GFLAGS),

	/* aclk_bus gates */
	GATE(0, "aclk_strc_sys", "aclk_bus", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(12), 12, GFLAGS),
	GATE(ACLK_DMAC_BUS, "aclk_dmac_bus", "aclk_bus", 0, RK3368_CLKGATE_CON(12), 11, GFLAGS),
	GATE(0, "sclk_intmem1", "aclk_bus", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(12), 6, GFLAGS),
	GATE(0, "sclk_intmem0", "aclk_bus", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(12), 5, GFLAGS),
	GATE(0, "aclk_intmem", "aclk_bus", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(12), 4, GFLAGS),
	GATE(0, "aclk_gic400", "aclk_bus", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(13), 9, GFLAGS),

	/* sclk_ddr gates */
	GATE(0, "nclk_ddrupctl", "sclk_ddr", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(13), 2, GFLAGS),

	/* clk_hsadc_tsp is part of diagram2 */

	/* fclk_mcu_src gates */
	GATE(0, "hclk_noc_mcu", "fclk_mcu_src", 0, RK3368_CLKGATE_CON(13), 14, GFLAGS),
	GATE(0, "fclk_mcu", "fclk_mcu_src", 0, RK3368_CLKGATE_CON(13), 12, GFLAGS),
	GATE(0, "hclk_mcu", "fclk_mcu_src", 0, RK3368_CLKGATE_CON(13), 11, GFLAGS),

	/* hclk_cpu gates */
	GATE(HCLK_SPDIF, "hclk_spdif", "hclk_bus", 0, RK3368_CLKGATE_CON(12), 10, GFLAGS),
	GATE(HCLK_ROM, "hclk_rom", "hclk_bus", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(12), 9, GFLAGS),
	GATE(HCLK_I2S_2CH, "hclk_i2s_2ch", "hclk_bus", 0, RK3368_CLKGATE_CON(12), 8, GFLAGS),
	GATE(HCLK_I2S_8CH, "hclk_i2s_8ch", "hclk_bus", 0, RK3368_CLKGATE_CON(12), 7, GFLAGS),
	GATE(HCLK_TSP, "hclk_tsp", "hclk_bus", 0, RK3368_CLKGATE_CON(13), 10, GFLAGS),
	GATE(HCLK_CRYPTO, "hclk_crypto", "hclk_bus", 0, RK3368_CLKGATE_CON(13), 4, GFLAGS),
	GATE(MCLK_CRYPTO, "mclk_crypto", "hclk_bus", 0, RK3368_CLKGATE_CON(13), 3, GFLAGS),

	/* pclk_cpu gates */
	GATE(PCLK_DDRPHY, "pclk_ddrphy", "pclk_bus", 0, RK3368_CLKGATE_CON(12), 14, GFLAGS),
	GATE(PCLK_DDRUPCTL, "pclk_ddrupctl", "pclk_bus", 0, RK3368_CLKGATE_CON(12), 13, GFLAGS),
	GATE(PCLK_I2C1, "pclk_i2c1", "pclk_bus", 0, RK3368_CLKGATE_CON(12), 3, GFLAGS),
	GATE(PCLK_I2C0, "pclk_i2c0", "pclk_bus", 0, RK3368_CLKGATE_CON(12), 2, GFLAGS),
	GATE(PCLK_MAILBOX, "pclk_mailbox", "pclk_bus", 0, RK3368_CLKGATE_CON(12), 1, GFLAGS),
	GATE(PCLK_PWM0, "pclk_pwm0", "pclk_bus", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(12), 0, GFLAGS),
	GATE(PCLK_SIM, "pclk_sim", "pclk_bus", 0, RK3368_CLKGATE_CON(13), 8, GFLAGS),
	GATE(PCLK_PWM1, "pclk_pwm1", "pclk_bus", 0, RK3368_CLKGATE_CON(13), 6, GFLAGS),
	GATE(PCLK_UART2, "pclk_uart2", "pclk_bus", 0, RK3368_CLKGATE_CON(13), 5, GFLAGS),
	GATE(0, "pclk_efuse_256", "pclk_bus", 0, RK3368_CLKGATE_CON(13), 1, GFLAGS),
	GATE(0, "pclk_efuse_1024", "pclk_bus", 0, RK3368_CLKGATE_CON(13), 0, GFLAGS),

	/*
	 * video clk gates
	 * aclk_video(_pre) can actually select between parents of aclk_vdpu
	 * and aclk_vepu by setting bit GRF_SOC_CON0[7].
	 */
	GATE(ACLK_VIDEO, "aclk_video", "aclk_vdpu", 0, RK3368_CLKGATE_CON(15), 0, GFLAGS),
	GATE(SCLK_HEVC_CABAC, "sclk_hevc_cabac", "sclk_hevc_cabac_src", 0, RK3368_CLKGATE_CON(15), 3, GFLAGS),
	GATE(SCLK_HEVC_CORE, "sclk_hevc_core", "sclk_hevc_core_src", 0, RK3368_CLKGATE_CON(15), 2, GFLAGS),
	GATE(HCLK_VIDEO, "hclk_video", "hclk_video_pre", 0, RK3368_CLKGATE_CON(15), 1, GFLAGS),

	/* aclk_rga_pre gates */
	GATE(ACLK_VIO1_NOC, "aclk_vio1_noc", "aclk_rga_pre", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(16), 10, GFLAGS),
	GATE(ACLK_RGA, "aclk_rga", "aclk_rga_pre", 0, RK3368_CLKGATE_CON(16), 0, GFLAGS),
	GATE(ACLK_HDCP, "aclk_hdcp", "aclk_rga_pre", 0, RK3368_CLKGATE_CON(17), 10, GFLAGS),

	/* aclk_vio0 gates */
	GATE(ACLK_VIP, "aclk_vip", "aclk_vio0", 0, RK3368_CLKGATE_CON(16), 11, GFLAGS),
	GATE(ACLK_VIO0_NOC, "aclk_vio0_noc", "aclk_vio0", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(16), 9, GFLAGS),
	GATE(ACLK_VOP, "aclk_vop", "aclk_vio0", 0, RK3368_CLKGATE_CON(16), 5, GFLAGS),
	GATE(ACLK_VOP_IEP, "aclk_vop_iep", "aclk_vio0", 0, RK3368_CLKGATE_CON(16), 4, GFLAGS),
	GATE(ACLK_IEP, "aclk_iep", "aclk_vio0", 0, RK3368_CLKGATE_CON(16), 2, GFLAGS),

	/* sclk_isp gates */
	GATE(HCLK_ISP, "hclk_isp", "sclk_isp", 0, RK3368_CLKGATE_CON(16), 14, GFLAGS),
	GATE(ACLK_ISP, "aclk_isp", "sclk_isp", 0, RK3368_CLKGATE_CON(17), 0, GFLAGS),

	/* hclk_vio gates */
	GATE(HCLK_VIP, "hclk_vip", "hclk_vio", 0, RK3368_CLKGATE_CON(16), 12, GFLAGS),
	GATE(HCLK_VIO_NOC, "hclk_vio_noc", "hclk_vio", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(16), 8, GFLAGS),
	GATE(HCLK_VIO_AHB_ARBI, "hclk_vio_ahb_arbi", "hclk_vio", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(16), 7, GFLAGS),
	GATE(HCLK_VOP, "hclk_vop", "hclk_vio", 0, RK3368_CLKGATE_CON(16), 6, GFLAGS),
	GATE(HCLK_IEP, "hclk_iep", "hclk_vio", 0, RK3368_CLKGATE_CON(16), 3, GFLAGS),
	GATE(HCLK_RGA, "hclk_rga", "hclk_vio", 0, RK3368_CLKGATE_CON(16), 1, GFLAGS),
	GATE(HCLK_VIO_HDCPMMU, "hclk_hdcpmmu", "hclk_vio", 0, RK3368_CLKGATE_CON(17), 12, GFLAGS),
	GATE(HCLK_VIO_H2P, "hclk_vio_h2p", "hclk_vio", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(17), 7, GFLAGS),

	/*
	 * pclk_vio gates
	 * pclk_vio comes from the exactly same source as hclk_vio
	 */
	GATE(PCLK_HDCP, "pclk_hdcp", "hclk_vio", 0, RK3368_CLKGATE_CON(17), 11, GFLAGS),
	GATE(PCLK_EDP_CTRL, "pclk_edp_ctrl", "hclk_vio", 0, RK3368_CLKGATE_CON(17), 9, GFLAGS),
	GATE(PCLK_VIO_H2P, "pclk_vio_h2p", "hclk_vio", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(17), 8, GFLAGS),
	GATE(PCLK_HDMI_CTRL, "pclk_hdmi_ctrl", "hclk_vio", 0, RK3368_CLKGATE_CON(17), 6, GFLAGS),
	GATE(PCLK_MIPI_CSI, "pclk_mipi_csi", "hclk_vio", 0, RK3368_CLKGATE_CON(17), 4, GFLAGS),
	GATE(PCLK_MIPI_DSI0, "pclk_mipi_dsi0", "hclk_vio", 0, RK3368_CLKGATE_CON(17), 3, GFLAGS),

	/* ext_vip gates in diagram3 */

	/* gpu gates */
	GATE(SCLK_GPU_CORE, "sclk_gpu_core", "sclk_gpu_core_src", 0, RK3368_CLKGATE_CON(18), 2, GFLAGS),
	GATE(ACLK_GPU_MEM, "aclk_gpu_mem", "aclk_gpu_mem_pre", 0, RK3368_CLKGATE_CON(18), 1, GFLAGS),
	GATE(ACLK_GPU_CFG, "aclk_gpu_cfg", "aclk_gpu_cfg_pre", 0, RK3368_CLKGATE_CON(18), 0, GFLAGS),

	/* aclk_peri gates */
	GATE(ACLK_DMAC_PERI, "aclk_dmac_peri", "aclk_peri", 0, RK3368_CLKGATE_CON(19), 3, GFLAGS),
	GATE(0, "aclk_peri_axi_matrix", "aclk_peri", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(19), 2, GFLAGS),
	GATE(HCLK_SFC, "hclk_sfc", "aclk_peri", 0, RK3368_CLKGATE_CON(20), 15, GFLAGS),
	GATE(ACLK_GMAC, "aclk_gmac", "aclk_peri", 0, RK3368_CLKGATE_CON(20), 13, GFLAGS),
	GATE(0, "aclk_peri_niu", "aclk_peri", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(20), 8, GFLAGS),
	GATE(ACLK_PERI_MMU, "aclk_peri_mmu", "aclk_peri", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(21), 4, GFLAGS),

	/* hclk_peri gates */
	GATE(0, "hclk_peri_axi_matrix", "hclk_peri", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(19), 0, GFLAGS),
	GATE(HCLK_NANDC0, "hclk_nandc0", "hclk_peri", 0, RK3368_CLKGATE_CON(20), 11, GFLAGS),
	GATE(0, "hclk_mmc_peri", "hclk_peri", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(20), 10, GFLAGS),
	GATE(0, "hclk_emem_peri", "hclk_peri", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(20), 9, GFLAGS),
	GATE(0, "hclk_peri_ahb_arbi", "hclk_peri", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(20), 7, GFLAGS),
	GATE(0, "hclk_usb_peri", "hclk_peri", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(20), 6, GFLAGS),
	GATE(HCLK_HSIC, "hclk_hsic", "hclk_peri", 0, RK3368_CLKGATE_CON(20), 5, GFLAGS),
	GATE(HCLK_HOST1, "hclk_host1", "hclk_peri", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(20), 4, GFLAGS),
	GATE(HCLK_HOST0, "hclk_host0", "hclk_peri", 0, RK3368_CLKGATE_CON(20), 3, GFLAGS),
	GATE(0, "pmu_hclk_otg0", "hclk_peri", 0, RK3368_CLKGATE_CON(20), 2, GFLAGS),
	GATE(HCLK_OTG0, "hclk_otg0", "hclk_peri", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(20), 1, GFLAGS),
	GATE(HCLK_HSADC, "hclk_hsadc", "hclk_peri", 0, RK3368_CLKGATE_CON(21), 3, GFLAGS),
	GATE(HCLK_EMMC, "hclk_emmc", "hclk_peri", 0, RK3368_CLKGATE_CON(21), 2, GFLAGS),
	GATE(HCLK_SDIO0, "hclk_sdio0", "hclk_peri", 0, RK3368_CLKGATE_CON(21), 1, GFLAGS),
	GATE(HCLK_SDMMC, "hclk_sdmmc", "hclk_peri", 0, RK3368_CLKGATE_CON(21), 0, GFLAGS),

	/* pclk_peri gates */
	GATE(PCLK_SARADC, "pclk_saradc", "pclk_peri", 0, RK3368_CLKGATE_CON(19), 15, GFLAGS),
	GATE(PCLK_I2C5, "pclk_i2c5", "pclk_peri", 0, RK3368_CLKGATE_CON(19), 14, GFLAGS),
	GATE(PCLK_I2C4, "pclk_i2c4", "pclk_peri", 0, RK3368_CLKGATE_CON(19), 13, GFLAGS),
	GATE(PCLK_I2C3, "pclk_i2c3", "pclk_peri", 0, RK3368_CLKGATE_CON(19), 12, GFLAGS),
	GATE(PCLK_I2C2, "pclk_i2c2", "pclk_peri", 0, RK3368_CLKGATE_CON(19), 11, GFLAGS),
	GATE(PCLK_UART4, "pclk_uart4", "pclk_peri", 0, RK3368_CLKGATE_CON(19), 10, GFLAGS),
	GATE(PCLK_UART3, "pclk_uart3", "pclk_peri", 0, RK3368_CLKGATE_CON(19), 9, GFLAGS),
	GATE(PCLK_UART1, "pclk_uart1", "pclk_peri", 0, RK3368_CLKGATE_CON(19), 8, GFLAGS),
	GATE(PCLK_UART0, "pclk_uart0", "pclk_peri", 0, RK3368_CLKGATE_CON(19), 7, GFLAGS),
	GATE(PCLK_SPI2, "pclk_spi2", "pclk_peri", 0, RK3368_CLKGATE_CON(19), 6, GFLAGS),
	GATE(PCLK_SPI1, "pclk_spi1", "pclk_peri", 0, RK3368_CLKGATE_CON(19), 5, GFLAGS),
	GATE(PCLK_SPI0, "pclk_spi0", "pclk_peri", 0, RK3368_CLKGATE_CON(19), 4, GFLAGS),
	GATE(0, "pclk_peri_axi_matrix", "pclk_peri", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(19), 1, GFLAGS),
	GATE(PCLK_GMAC, "pclk_gmac", "pclk_peri", 0, RK3368_CLKGATE_CON(20), 14, GFLAGS),
	GATE(PCLK_TSADC, "pclk_tsadc", "pclk_peri", 0, RK3368_CLKGATE_CON(20), 0, GFLAGS),

	/* pclk_pd_alive gates */
	GATE(PCLK_TIMER1, "pclk_timer1", "pclk_pd_alive", 0, RK3368_CLKGATE_CON(22), 13, GFLAGS),
	GATE(PCLK_TIMER0, "pclk_timer0", "pclk_pd_alive", 0, RK3368_CLKGATE_CON(22), 12, GFLAGS),
	GATE(0, "pclk_alive_niu", "pclk_pd_alive", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(22), 9, GFLAGS),
	GATE(PCLK_GRF, "pclk_grf", "pclk_pd_alive", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(22), 8, GFLAGS),
	GATE(PCLK_GPIO3, "pclk_gpio3", "pclk_pd_alive", 0, RK3368_CLKGATE_CON(22), 3, GFLAGS),
	GATE(PCLK_GPIO2, "pclk_gpio2", "pclk_pd_alive", 0, RK3368_CLKGATE_CON(22), 2, GFLAGS),
	GATE(PCLK_GPIO1, "pclk_gpio1", "pclk_pd_alive", 0, RK3368_CLKGATE_CON(22), 1, GFLAGS),

	/*
	 * pclk_vio gates
	 * pclk_vio comes from the exactly same source as hclk_vio
	 */
	GATE(0, "pclk_dphyrx", "hclk_vio", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(14), 8, GFLAGS),
	GATE(0, "pclk_dphytx", "hclk_vio", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(14), 8, GFLAGS),

	/* pclk_pd_pmu gates */
	GATE(PCLK_PMUGRF, "pclk_pmugrf", "pclk_pd_pmu", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(23), 5, GFLAGS),
	GATE(PCLK_GPIO0, "pclk_gpio0", "pclk_pd_pmu", 0, RK3368_CLKGATE_CON(23), 4, GFLAGS),
	GATE(PCLK_SGRF, "pclk_sgrf", "pclk_pd_pmu", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(23), 3, GFLAGS),
	GATE(0, "pclk_pmu_noc", "pclk_pd_pmu", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(23), 2, GFLAGS),
	GATE(0, "pclk_intmem1", "pclk_pd_pmu", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(23), 1, GFLAGS),
	GATE(PCLK_PMU, "pclk_pmu", "pclk_pd_pmu", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(23), 0, GFLAGS),

	/* timer gates */
	GATE(0, "sclk_timer15", "xin24m", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(24), 11, GFLAGS),
	GATE(0, "sclk_timer14", "xin24m", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(24), 10, GFLAGS),
	GATE(0, "sclk_timer13", "xin24m", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(24), 9, GFLAGS),
	GATE(0, "sclk_timer12", "xin24m", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(24), 8, GFLAGS),
	GATE(0, "sclk_timer11", "xin24m", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(24), 7, GFLAGS),
	GATE(0, "sclk_timer10", "xin24m", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(24), 6, GFLAGS),
	GATE(0, "sclk_timer05", "xin24m", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(24), 5, GFLAGS),
	GATE(0, "sclk_timer04", "xin24m", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(24), 4, GFLAGS),
	GATE(0, "sclk_timer03", "xin24m", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(24), 3, GFLAGS),
	GATE(0, "sclk_timer02", "xin24m", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(24), 2, GFLAGS),
	GATE(0, "sclk_timer01", "xin24m", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(24), 1, GFLAGS),
	GATE(0, "sclk_timer00", "xin24m", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(24), 0, GFLAGS),
};

static const char *const rk3368_critical_clocks[] __initconst = {
	"aclk_bus",
	"aclk_peri",
	/*
	 * pwm1 supplies vdd_logic on a lot of boards, is currently unhandled
	 * but needs to stay enabled there (including its parents) at all times.
	 */
	"pclk_pwm1",
	"pclk_pd_pmu",
};

static void __init rk3368_clk_init(struct device_node *np)
{
	void __iomem *reg_base;
	struct clk *clk;

	reg_base = of_iomap(np, 0);
	if (!reg_base) {
		pr_err("%s: could not map cru region\n", __func__);
		return;
	}

	rockchip_clk_init(np, reg_base, CLK_NR_CLKS);

	/* Watchdog pclk is controlled by sgrf_soc_con3[7]. */
	clk = clk_register_fixed_factor(NULL, "pclk_wdt", "pclk_pd_alive", 0, 1, 1);
	if (IS_ERR(clk))
		pr_warn("%s: could not register clock pclk_wdt: %ld\n",
			__func__, PTR_ERR(clk));
	else
		rockchip_clk_add_lookup(clk, PCLK_WDT);

	rockchip_clk_register_plls(rk3368_pll_clks,
				   ARRAY_SIZE(rk3368_pll_clks),
				   RK3368_GRF_SOC_STATUS0);
	rockchip_clk_register_branches(rk3368_clk_branches,
				  ARRAY_SIZE(rk3368_clk_branches));
	rockchip_clk_protect_critical(rk3368_critical_clocks,
				      ARRAY_SIZE(rk3368_critical_clocks));

	rockchip_clk_register_armclk(ARMCLKB, "armclkb",
			mux_armclkb_p, ARRAY_SIZE(mux_armclkb_p),
			&rk3368_cpuclkb_data, rk3368_cpuclkb_rates,
			ARRAY_SIZE(rk3368_cpuclkb_rates));

	rockchip_clk_register_armclk(ARMCLKL, "armclkl",
			mux_armclkl_p, ARRAY_SIZE(mux_armclkl_p),
			&rk3368_cpuclkl_data, rk3368_cpuclkl_rates,
			ARRAY_SIZE(rk3368_cpuclkl_rates));

	rockchip_register_softrst(np, 15, reg_base + RK3368_SOFTRST_CON(0),
				  ROCKCHIP_SOFTRST_HIWORD_MASK);

	rockchip_register_restart_notifier(RK3368_GLB_SRST_FST, NULL);
}
CLK_OF_DECLARE(rk3368_cru, "rockchip,rk3368-cru", rk3368_clk_init);
