/*
 * Copyright (c) 2016 Rockchip Electronics Co. Ltd.
 * Author: Xiao Feng <xf@rock-chips.com>
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
#include <dt-bindings/clock/rk3366-cru.h>
#include "clk.h"

#define RK3366_GRF_SOC_STATUS0	0x480

enum rk3366_plls {
	apll, dpll, cpll, gpll, npll, mpll, wpll, bpll,
};

static struct rockchip_pll_rate_table rk3366_pll_rates[] = {
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
	RK3036_PLL_RATE( 984000000, 1, 82, 2, 1, 1, 0),
	RK3036_PLL_RATE( 960000000, 1, 80, 2, 1, 1, 0),
	RK3036_PLL_RATE( 936000000, 1, 78, 2, 1, 1, 0),
	RK3036_PLL_RATE( 912000000, 1, 76, 2, 1, 1, 0),
	RK3036_PLL_RATE( 900000000, 4, 300, 2, 1, 1, 0),
	RK3036_PLL_RATE( 888000000, 1, 74, 2, 1, 1, 0),
	RK3036_PLL_RATE( 864000000, 1, 72, 2, 1, 1, 0),
	RK3036_PLL_RATE( 840000000, 1, 70, 2, 1, 1, 0),
	RK3036_PLL_RATE( 816000000, 1, 68, 2, 1, 1, 0),
	RK3036_PLL_RATE( 800000000, 6, 400, 2, 1, 1, 0),
	RK3036_PLL_RATE( 750000000, 2, 125, 2, 1, 1, 0),
	RK3036_PLL_RATE( 700000000, 6, 350, 2, 1, 1, 0),
	RK3036_PLL_RATE( 696000000, 1, 58, 2, 1, 1, 0),
	RK3036_PLL_RATE( 600000000, 1, 75, 3, 1, 1, 0),
	RK3036_PLL_RATE( 594000000, 2, 99, 2, 1, 1, 0),
	RK3036_PLL_RATE( 576000000, 1, 96, 4, 1, 1, 0),
	RK3036_PLL_RATE( 520000000, 1, 65, 3, 1, 1, 0),
	RK3036_PLL_RATE( 504000000, 1, 63, 3, 1, 1, 0),
	RK3036_PLL_RATE( 500000000, 6, 250, 2, 1, 1, 0),
	RK3036_PLL_RATE( 480000000, 1, 80, 4, 1, 1, 0),
	RK3036_PLL_RATE( 408000000, 1, 68, 2, 2, 1, 0),
	RK3036_PLL_RATE( 312000000, 1, 52, 2, 2, 1, 0),
	RK3036_PLL_RATE( 216000000, 1, 72, 4, 2, 1, 0),
	RK3036_PLL_RATE(  96000000, 1, 64, 4, 4, 1, 0),

	{ /* sentinel */ },
};

PNAME(mux_pll_p)		= { "xin24m", "xin32k" };
PNAME(mux_armclk_p)		= { "apll_core", "gpll_core", "dpll_core" };
PNAME(mux_ddrphy_p)		= { "dpll_ddr", "gpll_ddr", "apll_ddr" };
PNAME(mux_aclk_bus_src_p)	= { "cpll_aclk_bus", "gpll_aclk_bus" };
PNAME(mux_pll_src_cpll_gpll_p)		= { "cpll", "gpll" };
PNAME(mux_pll_src_dmynpll_cpll_gpll_gpll_p)	= { "dummy_npll", "cpll", "gpll", "gpll" };
PNAME(mux_pll_src_cpll_gpll_usb_p)	= { "cpll", "gpll", "usbphy_480m" };
PNAME(mux_pll_src_cpll_gpll_usb_usb_p)	= { "cpll", "gpll", "usbphy_480m",
					    "usbphy_480m" };
PNAME(mux_pll_src_cpll_gpll_usb_dmynpll_p)	= { "cpll", "gpll", "usbphy_480m",
					    "dummy_npll" };
PNAME(mux_pll_src_cpll_gpll_dmynpll_dmynpll_p) = { "cpll", "gpll", "dummy_npll", "dummy_npll" };
PNAME(mux_pll_src_cpll_gpll_dmynpll_usb_p) = { "cpll", "gpll", "dummy_npll",
					    "usbphy_480m" };
PNAME(mux_pll_src_cpll_gpll_npll_mpll_p) = { "cpll", "gpll", "npll", "mpll_src" };
PNAME(mux_vop_full_pwm_p) = { "xin24m", "cpll", "gpll", "dummy_npll" };
PNAME(mux_clk_32k_p)		= { "xin32k", "clk_32k_intr" };
PNAME(mux_i2s_8ch_pre_p)	= { "i2s_8ch_src", "i2s_8ch_frac",
				    "ext_i2s", "xin12m" };
PNAME(mux_i2s_8ch_clkout_p)	= { "i2s_8ch_pre", "xin12m" };
PNAME(mux_i2s_2ch_p)		= { "i2s_2ch_src", "i2s_2ch_frac",
				    "dummy", "xin12m" };
PNAME(mux_spdif_8ch_p)		= { "spdif_8ch_pre", "spdif_8ch_frac",
				    "ext_i2s", "xin12m" };
PNAME(mux_vip_out_p)		= { "vip_src", "xin24m" };
PNAME(mux_usb3_suspend_p)	= { "clk_32k", "xin24m" };
PNAME(mux_usbphy480m_p)		= { "xin24m", "sclk_otgphy0_480m" };
PNAME(mux_uart0_p)		= { "uart0_src", "uart0_frac", "xin24m", "xin24m" };
PNAME(mux_uart2_p)		= { "uart2_src", "xin24m" };
PNAME(mux_uart3_p)		= { "uart3_src", "uart3_frac", "xin24m", "xin24m"  };
PNAME(mux_mac_p)		= { "mac_pll_src", "ext_gmac" };
PNAME(mux_mmc_src_p)		= { "cpll", "gpll", "usbphy_480m", "xin24m" };
PNAME(mux_bt_p)			= { "bpll", "btclk520_pll" };
PNAME(mux_wifi_pll_p)		= { "wpll_wiff", "usbphy_480m_wifi" };

static struct rockchip_pll_clock rk3366_pll_clks[] __initdata = {
	[apll] = PLL(pll_rk3366, PLL_APLL, "apll", mux_pll_p, 0, RK3368_PLL_CON(0),
		     RK3368_PLL_CON(3), 8, 0, 0, rk3366_pll_rates),
	[dpll] = PLL(pll_rk3366, PLL_DPLL, "dpll", mux_pll_p, 0, RK3368_PLL_CON(8),
		     RK3368_PLL_CON(11), 8, 1, 0, NULL),
	[cpll] = PLL(pll_rk3366, PLL_CPLL, "cpll", mux_pll_p, 0, RK3368_PLL_CON(12),
		     RK3368_PLL_CON(15), 8, 2, ROCKCHIP_PLL_SYNC_RATE, rk3366_pll_rates),
	[gpll] = PLL(pll_rk3366, PLL_GPLL, "gpll", mux_pll_p, 0, RK3368_PLL_CON(16),
		     RK3368_PLL_CON(19), 8, 3, ROCKCHIP_PLL_SYNC_RATE, rk3366_pll_rates),
	[npll] = PLL(pll_rk3366, PLL_NPLL, "npll",  mux_pll_p, 0, RK3368_PLL_CON(20),
		     RK3368_PLL_CON(23), 8, 4, ROCKCHIP_PLL_SYNC_RATE, rk3366_pll_rates),
	[mpll] = PLL(pll_rk3366, PLL_MPLL, "mpll",  mux_pll_p, 0, RK3368_PLL_CON(24),
		     RK3368_PLL_CON(27), 8, 5, ROCKCHIP_PLL_SYNC_RATE, rk3366_pll_rates),
	[wpll] = PLL(pll_rk3366, PLL_WPLL, "wpll",  mux_pll_p, 0, RK3368_PLL_CON(28),
		     RK3368_PLL_CON(31), 8, 6, ROCKCHIP_PLL_SYNC_RATE, rk3366_pll_rates),
	[bpll] = PLL(pll_rk3366, PLL_BPLL, "bpll",  mux_pll_p, 0, RK3368_PLL_CON(32),
		     RK3368_PLL_CON(35), 8, 7, ROCKCHIP_PLL_SYNC_RATE, rk3366_pll_rates),
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

static const struct rockchip_cpuclk_reg_data rk3366_cpuclk_data = {
	.core_reg = RK3368_CLKSEL_CON(0),
	.div_core_shift = 0,
	.div_core_mask = 0x1f,
	.mux_core_alt = 1,
	.mux_core_main = 0,
	.mux_core_shift = 6,
	.mux_core_mask = 0x1,
};

#define RK3366_DIV_ACLKM_MASK		0x1f
#define RK3366_DIV_ACLKM_SHIFT		8
#define RK3366_DIV_ATCLK_MASK		0x1f
#define RK3366_DIV_ATCLK_SHIFT		0
#define RK3366_DIV_PCLK_DBG_MASK	0x1f
#define RK3366_DIV_PCLK_DBG_SHIFT	8

#define RK3366_CLKSEL0(_offs, _aclkm)					\
	{								\
		.reg = RK3368_CLKSEL_CON(0 + _offs),			\
		.val = HIWORD_UPDATE(_aclkm, RK3366_DIV_ACLKM_MASK,	\
				RK3366_DIV_ACLKM_SHIFT),		\
	}
#define RK3366_CLKSEL1(_offs, _atclk, _pdbg)				\
	{								\
		.reg = RK3368_CLKSEL_CON(1 + _offs),			\
		.val = HIWORD_UPDATE(_atclk, RK3366_DIV_ATCLK_MASK,	\
				RK3366_DIV_ATCLK_SHIFT) |		\
		       HIWORD_UPDATE(_pdbg, RK3366_DIV_PCLK_DBG_MASK,	\
				RK3366_DIV_PCLK_DBG_SHIFT),		\
	}

/* cluster_b: aclkm in clksel0, rest in clksel1 */
#define RK3366_CPUCLK_RATE(_prate, _aclkm, _atclk, _pdbg)		\
	{								\
		.prate = _prate,					\
		.divs = {						\
			RK3366_CLKSEL0(0, _aclkm),			\
			RK3366_CLKSEL1(0, _atclk, _pdbg),		\
		},							\
	}

static struct rockchip_cpuclk_rate_table rk3366_cpuclk_rates[] __initdata = {
	RK3366_CPUCLK_RATE(1512000000, 1, 5, 5),
	RK3366_CPUCLK_RATE(1488000000, 1, 4, 4),
	RK3366_CPUCLK_RATE(1416000000, 1, 4, 4),
	RK3366_CPUCLK_RATE(1296000000, 1, 4, 4),
	RK3366_CPUCLK_RATE(1200000000, 1, 3, 3),
	RK3366_CPUCLK_RATE(1104000000, 1, 3, 3),
	RK3366_CPUCLK_RATE(1008000000, 1, 3, 3),
	RK3366_CPUCLK_RATE( 816000000, 1, 2, 2),
	RK3366_CPUCLK_RATE( 696000000, 1, 2, 2),
	RK3366_CPUCLK_RATE( 600000000, 1, 1, 1),
	RK3366_CPUCLK_RATE( 408000000, 1, 1, 1),
	RK3366_CPUCLK_RATE( 312000000, 1, 1, 1),
	RK3366_CPUCLK_RATE( 216000000, 1, 1, 1),
};

static struct rockchip_clk_branch rk3366_i2s_8ch_fracmux __initdata =
	MUX(0, "i2s_8ch_pre", mux_i2s_8ch_pre_p, CLK_SET_RATE_PARENT,
			RK3368_CLKSEL_CON(27), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3366_spdif_8ch_fracmux __initdata =
	MUX(0, "spdif_8ch_mux", mux_spdif_8ch_p, CLK_SET_RATE_PARENT,
			RK3368_CLKSEL_CON(31), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3366_i2s_2ch_fracmux __initdata =
	MUX(0, "i2s_2ch_mux", mux_i2s_2ch_p, CLK_SET_RATE_PARENT,
			RK3368_CLKSEL_CON(53), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3366_uart0_fracmux __initdata =
	MUX(SCLK_UART0, "sclk_uart0", mux_uart0_p, CLK_SET_RATE_PARENT,
			RK3368_CLKSEL_CON(33), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3366_uart3_fracmux __initdata =
	MUX(SCLK_UART3, "sclk_uart3", mux_uart3_p, CLK_SET_RATE_PARENT,
			RK3368_CLKSEL_CON(39), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3366_clk_branches[] __initdata = {
	/*
	 * Clock-Architecture Diagram 1
	 */

	GATE(SCLK_MPLL_SRC, "mpll_src", "mpll", CLK_IGNORE_UNUSED,
			RK3368_CLKGATE_CON(2), 11, GFLAGS),

	/*
	 * Clock-Architecture Diagram 2
	 */

	MUX(SCLK_USBPHY480M, "usbphy_480m", mux_usbphy480m_p, 0,
			RK3368_CLKSEL_CON(13), 6, 1, MFLAGS),

	DIV(SCLK_32K_INTR, "clk_32k_intr", "xin24m", 0,
			RK3368_CLKSEL_CON(7), 0, 10, DFLAGS),
	MUX(SCLK_32K, "clk_32k", mux_clk_32k_p, CLK_SET_RATE_PARENT,
			RK3368_CLKSEL_CON(7), 15, 1, MFLAGS),

	GATE(0, "apll_core", "apll", CLK_IGNORE_UNUSED,
			RK3368_CLKGATE_CON(0), 0, GFLAGS),
	GATE(0, "gpll_core", "gpll", CLK_IGNORE_UNUSED,
			RK3368_CLKGATE_CON(0), 1, GFLAGS),
	GATE(0, "dpll_core", "dpll", CLK_IGNORE_UNUSED,
			RK3368_CLKGATE_CON(0), 2, GFLAGS),

	DIV(0, "aclkm_core", "armclk", 0,
			RK3368_CLKSEL_CON(0), 8, 5, DFLAGS | CLK_DIVIDER_READ_ONLY),
	DIV(0, "atclk_core", "armclk", 0,
			RK3368_CLKSEL_CON(1), 0, 5, DFLAGS | CLK_DIVIDER_READ_ONLY),
	DIV(0, "pclk_dbg", "armclk", 0,
			RK3368_CLKSEL_CON(1), 8, 5, DFLAGS | CLK_DIVIDER_READ_ONLY),

	COMPOSITE_NOMUX(0, "sclk_cs_pre", "armclk", CLK_IGNORE_UNUSED,
			RK3368_CLKSEL_CON(4), 0, 5, DFLAGS,
			RK3368_CLKGATE_CON(0), 11, GFLAGS),
	COMPOSITE_NOMUX(0, "clkin_trace", "armclk", CLK_IGNORE_UNUSED,
			RK3368_CLKSEL_CON(14), 8, 5, DFLAGS,
			RK3368_CLKGATE_CON(0), 13, GFLAGS),

	GATE(SCLK_PVTM_CORE, "sclk_pvtm_core", "xin24m", 0,
			RK3368_CLKGATE_CON(7), 10, GFLAGS),

	GATE(0, "dpll_ddr", "dpll", CLK_IGNORE_UNUSED,
			RK3368_CLKGATE_CON(1), 8, GFLAGS),
	GATE(0, "gpll_ddr", "gpll", 0,
			RK3368_CLKGATE_CON(1), 9, GFLAGS),
	GATE(0, "apll_ddr", "apll", 0,
			RK3368_CLKGATE_CON(1), 7, GFLAGS),
	COMPOSITE_NOGATE_DIVTBL(0, "ddrphy_src", mux_ddrphy_p, CLK_IGNORE_UNUSED,
			RK3368_CLKSEL_CON(13), 4, 2, MFLAGS, 0, 2, DFLAGS, div_ddrphy_t),

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

	COMPOSITE(SCLK_CRYPTO, "sclk_crypto", mux_pll_src_cpll_gpll_p, 0,
			RK3368_CLKSEL_CON(6), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RK3368_CLKGATE_CON(7), 2, GFLAGS),

	COMPOSITE(0, "fclk_mcu_src", mux_pll_src_cpll_gpll_p, CLK_IGNORE_UNUSED,
			RK3368_CLKSEL_CON(12), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RK3368_CLKGATE_CON(1), 3, GFLAGS),

	COMPOSITE(SCLK_I2S_8CH_SRC, "i2s_8ch_src", mux_pll_src_cpll_gpll_p, 0,
			RK3368_CLKSEL_CON(27), 12, 1, MFLAGS, 0, 7, DFLAGS,
			RK3368_CLKGATE_CON(6), 1, GFLAGS),
	COMPOSITE_FRACMUX(0, "i2s_8ch_frac", "i2s_8ch_src", CLK_SET_RATE_PARENT,
			RK3368_CLKSEL_CON(28), 0,
			RK3368_CLKGATE_CON(6), 2, GFLAGS,
			&rk3366_i2s_8ch_fracmux),
	COMPOSITE_NODIV(SCLK_I2S_8CH_OUT, "i2s_8ch_clkout", mux_i2s_8ch_clkout_p, 0,
			RK3368_CLKSEL_CON(27), 15, 1, MFLAGS,
			RK3368_CLKGATE_CON(6), 0, GFLAGS),
	GATE(SCLK_I2S_8CH, "sclk_i2s_8ch", "i2s_8ch_pre", CLK_SET_RATE_PARENT,
			RK3368_CLKGATE_CON(6), 3, GFLAGS),

	COMPOSITE(SCLK_SPDIF_8CH_SRC, "spdif_8ch_src", mux_pll_src_cpll_gpll_p, 0,
			RK3368_CLKSEL_CON(31), 12, 1, MFLAGS, 0, 7, DFLAGS,
			RK3368_CLKGATE_CON(6), 4, GFLAGS),
	COMPOSITE_FRACMUX(0, "spdif_8ch_frac", "spdif_8ch_src", CLK_SET_RATE_PARENT,
			RK3368_CLKSEL_CON(32), 0,
			RK3368_CLKGATE_CON(6), 5, GFLAGS,
			&rk3366_spdif_8ch_fracmux),
	GATE(SCLK_SPDIF_8CH, "sclk_spdif_8ch", "spdif_8ch_mux", CLK_SET_RATE_PARENT,
			RK3368_CLKGATE_CON(6), 6, GFLAGS),

	COMPOSITE(SCLK_I2S_2CH_SRC, "i2s_2ch_src", mux_pll_src_cpll_gpll_p, 0,
			RK3368_CLKSEL_CON(53), 12, 1, MFLAGS, 0, 7, DFLAGS,
			RK3368_CLKGATE_CON(5), 13, GFLAGS),
	COMPOSITE_FRACMUX(0, "i2s_2ch_frac", "i2s_2ch_src", CLK_SET_RATE_PARENT,
			RK3368_CLKSEL_CON(54), 0,
			RK3368_CLKGATE_CON(5), 14, GFLAGS,
			&rk3366_i2s_2ch_fracmux),
	GATE(SCLK_I2S_2CH, "sclk_i2s_2ch", "i2s_2ch_mux", CLK_SET_RATE_PARENT,
			RK3368_CLKGATE_CON(5), 15, GFLAGS),

	/*
	 * Clock-Architecture Diagram 3
	 */

	MUX(0, "uart_src", mux_pll_src_cpll_gpll_p, 0,
			RK3368_CLKSEL_CON(35), 12, 1, MFLAGS),
	COMPOSITE_NOMUX(0, "uart2_src", "uart_src", 0,
			RK3368_CLKSEL_CON(37), 0, 7, DFLAGS,
			RK3368_CLKGATE_CON(2), 4, GFLAGS),
	MUX(SCLK_UART2, "sclk_uart2", mux_uart2_p, CLK_SET_RATE_PARENT,
			RK3368_CLKSEL_CON(37), 8, 1, MFLAGS),

	COMPOSITE(0, "aclk_vepu", mux_pll_src_cpll_gpll_dmynpll_usb_p, 0,
			RK3368_CLKSEL_CON(15), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3368_CLKGATE_CON(4), 6, GFLAGS),
	COMPOSITE(0, "aclk_vdpu", mux_pll_src_cpll_gpll_dmynpll_usb_p, 0,
			RK3368_CLKSEL_CON(15), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RK3368_CLKGATE_CON(4), 7, GFLAGS),

	/*
	 * We introduce a virtual node of hclk_vodec_pre_v to split one clock
	 * struct with a gate and a fix divider into two node in software.
	 */
	GATE(0, "hclk_video_pre_v", "aclk_vdpu", 0,
			RK3368_CLKGATE_CON(4), 8, GFLAGS),

	COMPOSITE(0, "aclk_rkvdec_pre", mux_pll_src_cpll_gpll_dmynpll_usb_p, 0,
			RK3368_CLKSEL_CON(4), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RK3368_CLKGATE_CON(5), 8, GFLAGS),

	/*
	 * We introduce a virtual node of hclk_rkvdec_pre_v to split one clock
	 * struct with a gate and a fix divider into two node in software.
	 */
	GATE(0, "hclk_rkvdec_pre_v", "aclk_rkvdec_pre", 0,
			RK3368_CLKGATE_CON(5), 9, GFLAGS),

	COMPOSITE(SCLK_HEVC_CABAC, "sclk_hevc_cabac_src", mux_pll_src_cpll_gpll_dmynpll_usb_p, 0,
			RK3368_CLKSEL_CON(17), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3368_CLKGATE_CON(5), 1, GFLAGS),
	COMPOSITE(SCLK_HEVC_CORE, "sclk_hevc_core_src", mux_pll_src_cpll_gpll_dmynpll_usb_p, 0,
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

	COMPOSITE(0, "aclk_hdcp_pre", mux_pll_src_cpll_gpll_usb_p, 0,
			RK3368_CLKSEL_CON(16), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3368_CLKGATE_CON(4), 15, GFLAGS),

	COMPOSITE(DCLK_VOP_FULL, "dclk_vop_full", mux_pll_src_cpll_gpll_npll_mpll_p, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			RK3368_CLKSEL_CON(20), 8, 2, MFLAGS, 0, 8, DFLAGS,
			RK3368_CLKGATE_CON(4), 1, GFLAGS),

	COMPOSITE(SCLK_VOP_FULL_PWM, "sclk_vop_full_pwm", mux_vop_full_pwm_p, 0,
			RK3368_CLKSEL_CON(23), 6, 2, MFLAGS, 0, 6, DFLAGS,
			RK3368_CLKGATE_CON(4), 2, GFLAGS),

	COMPOSITE(DCLK_VOP_LITE, "dclk_vop_lite", mux_pll_src_cpll_gpll_npll_mpll_p, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			RK3368_CLKSEL_CON(24), 8, 2, MFLAGS, 0, 8, DFLAGS,
			RK3368_CLKGATE_CON(5), 6, GFLAGS),

	COMPOSITE_NOMUX(DCLK_HDMIPHY, "dclk_hdmiphy", "mpll_src", 0,
			RK3368_CLKSEL_CON(16), 8, 8, DFLAGS,
			RK3368_CLKGATE_CON(5), 7, GFLAGS),

	COMPOSITE(SCLK_ISP, "sclk_isp", mux_pll_src_cpll_gpll_dmynpll_dmynpll_p, 0,
			RK3368_CLKSEL_CON(22), 6, 2, MFLAGS, 0, 6, DFLAGS,
			RK3368_CLKGATE_CON(4), 9, GFLAGS),

	GATE(PCLK_ISP, "pclk_isp", "ext_isp", 0,
			RK3368_CLKGATE_CON(17), 2, GFLAGS),

	GATE(SCLK_HDMI_HDCP, "sclk_hdmi_hdcp", "xin24m", 0,
			RK3368_CLKGATE_CON(4), 13, GFLAGS),
	GATE(SCLK_HDMI_CEC, "sclk_hdmi_cec", "clk_32k", 0,
			RK3368_CLKGATE_CON(4), 12, GFLAGS),

	MUX(SCLK_VIP_SRC, "vip_src", mux_pll_src_cpll_gpll_p, 0,
			RK3368_CLKSEL_CON(21), 15, 1, MFLAGS),
	COMPOSITE(SCLK_VIP_OUT, "sclk_vip_out", mux_vip_out_p, 0,
			RK3368_CLKSEL_CON(21), 14, 1, MFLAGS, 8, 5, DFLAGS,
			RK3368_CLKGATE_CON(4), 5, GFLAGS),

	GATE(SCLK_MIPIDSI_24M, "sclk_mipidsi_24m", "xin24m", 0, RK3368_CLKGATE_CON(4), 14, GFLAGS),

	/*
	 * Clock-Architecture Diagram 4
	 */

	COMPOSITE(SCLK_HDCP, "sclk_hdcp", mux_pll_src_cpll_gpll_dmynpll_dmynpll_p, 0,
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

	COMPOSITE(0, "sclk_gpu_core_src", mux_pll_src_cpll_gpll_usb_dmynpll_p, 0,
			RK3368_CLKSEL_CON(14), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3368_CLKGATE_CON(4), 11, GFLAGS),
	GATE(SCLK_PVTM_GPU, "sclk_pvtm_gpu", "xin24m", 0,
			RK3368_CLKGATE_CON(7), 11, GFLAGS),

	COMPOSITE(0, "aclk_peri0_src", mux_pll_src_cpll_gpll_p, CLK_IGNORE_UNUSED,
			RK3368_CLKSEL_CON(9), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RK3368_CLKGATE_CON(3), 0, GFLAGS),
	COMPOSITE_NOMUX(PCLK_PERI0, "pclk_peri0", "aclk_peri0_src", 0,
			RK3368_CLKSEL_CON(9), 12, 2, DFLAGS | CLK_DIVIDER_POWER_OF_TWO,
			RK3368_CLKGATE_CON(3), 3, GFLAGS),
	COMPOSITE_NOMUX(HCLK_PERI0, "hclk_peri0", "aclk_peri0_src", CLK_IGNORE_UNUSED,
			RK3368_CLKSEL_CON(9), 8, 2, DFLAGS | CLK_DIVIDER_POWER_OF_TWO,
			RK3368_CLKGATE_CON(3), 2, GFLAGS),
	GATE(ACLK_PERI0, "aclk_peri0", "aclk_peri0_src", CLK_IGNORE_UNUSED,
			RK3368_CLKGATE_CON(3), 1, GFLAGS),

	COMPOSITE(0, "aclk_peri1_src", mux_pll_src_cpll_gpll_p, CLK_IGNORE_UNUSED,
			RK3368_CLKSEL_CON(11), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RK3368_CLKGATE_CON(3), 10, GFLAGS),
	COMPOSITE_NOMUX(PCLK_PERI1, "pclk_peri1", "aclk_peri1_src", 0,
			RK3368_CLKSEL_CON(11), 12, 2, DFLAGS | CLK_DIVIDER_POWER_OF_TWO,
			RK3368_CLKGATE_CON(3), 13, GFLAGS),
	COMPOSITE_NOMUX(HCLK_PERI1, "hclk_peri1", "aclk_peri1_src", CLK_IGNORE_UNUSED,
			RK3368_CLKSEL_CON(11), 8, 2, DFLAGS | CLK_DIVIDER_POWER_OF_TWO,
			RK3368_CLKGATE_CON(3), 12, GFLAGS),
	GATE(ACLK_PERI1, "aclk_peri1", "aclk_peri1_src", CLK_IGNORE_UNUSED,
			RK3368_CLKGATE_CON(3), 11, GFLAGS),

	GATE(SCLK_USB3_REF, "sclk_usb3_ref", "xin24m", 0,
			RK3368_CLKGATE_CON(3), 15, GFLAGS),

	COMPOSITE(SCLK_USB3_SUSPEND, "sclk_usb3_suspend", mux_usb3_suspend_p, 0,
			RK3368_CLKSEL_CON(29), 8, 1, MFLAGS, 0, 8, DFLAGS,
			RK3368_CLKGATE_CON(3), 14, GFLAGS),

	/* ref_alt_clk_p has a mux in the grf */

	/*
	 * Clock-Architecture Diagram 5
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

	GATE(SCLK_OTG_PHY0, "sclk_otg_phy0", "xin24m", CLK_IGNORE_UNUSED,
			RK3368_CLKGATE_CON(8), 1, GFLAGS),

	GATE(SCLK_OTG_ADP, "sclk_otg_adp", "clk_32k", CLK_IGNORE_UNUSED,
			RK3368_CLKGATE_CON(8), 4, GFLAGS),

	GATE(SCLK_TSADC, "sclk_tsadc", "clk_32k", 0,
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
			&rk3366_uart0_fracmux),

	COMPOSITE_NOMUX(0, "uart3_src", "uart_src", 0,
			RK3368_CLKSEL_CON(39), 0, 7, DFLAGS,
			RK3368_CLKGATE_CON(2), 6, GFLAGS),
	COMPOSITE_FRACMUX(0, "uart3_frac", "uart3_src", CLK_SET_RATE_PARENT,
			RK3368_CLKSEL_CON(40), 0,
			RK3368_CLKGATE_CON(2), 7, GFLAGS,
			&rk3366_uart3_fracmux),

	/*
	 * Clock-Architecture Diagram 6
	 */

	COMPOSITE(0, "mac_pll_src", mux_pll_src_dmynpll_cpll_gpll_gpll_p, 0,
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

	GATE(0, "jtag", "ext_jtag", CLK_IGNORE_UNUSED,
			RK3368_CLKGATE_CON(7), 0, GFLAGS),

	/*
	 * Clock-Architecture Diagram 7
	 */

	COMPOSITE_NODIV(0, "btclk520_pll", mux_pll_src_cpll_gpll_dmynpll_dmynpll_p, 0,
			RK3368_CLKSEL_CON(5), 13, 2, MFLAGS,
			RK3368_CLKGATE_CON(2), 10, GFLAGS),
	MUX(0, "clk_bt_pll", mux_bt_p, 0,
			RK3368_CLKSEL_CON(5), 15, 1, MFLAGS),
	COMPOSITE_NOMUX(SCLK_BT_52, "sclk_bt_520", "clk_bt_pll", 0,
			RK3368_CLKSEL_CON(5), 0, 5, DFLAGS,
			RK3368_CLKGATE_CON(8), 13, GFLAGS),
	DIV(0, "pclk_btbb", "sclk_bt_520", 0,
			RK3368_CLKSEL_CON(5), 10, 3, DFLAGS),
	COMPOSITE_NOMUX(SCLK_BT_M0, "sclk_bt_m0", "clk_bt_pll", 0,
			RK3368_CLKSEL_CON(5), 5, 5, DFLAGS,
			RK3368_CLKGATE_CON(8), 14, GFLAGS),

	GATE(SCLK_WIFI_WPLL, "wpll_wiff", "wpll", 0,
			RK3368_CLKGATE_CON(8), 11, GFLAGS),
	GATE(SCLK_WIFI_USBPHY480M, "usbphy_480m_wifi", "usbphy_480m", 0,
			RK3368_CLKGATE_CON(8), 11, GFLAGS),
	COMPOSITE(SCLK_WIFIDSP, "sclk_wifidsp", mux_wifi_pll_p, 0,
			RK3368_CLKSEL_CON(13), 15, 1, MFLAGS, 10, 5, DFLAGS,
			RK3368_CLKGATE_CON(8), 12, GFLAGS),
	DIV(0, "hclk_wifi", "sclk_wifidsp", CLK_SET_RATE_PARENT,
			RK3368_CLKSEL_CON(13), 7, 3, DFLAGS),

	/*
	 * Clock-Architecture Diagram 8
	 */

	/* pclk_pd_pmu gates*/
	GATE(PCLK_PMU, "pclk_pmu", "pclk_pd_pmu", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(23), 0, GFLAGS),
	GATE(0, "pclk_intmem1", "pclk_pd_pmu", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(23), 1, GFLAGS),
	GATE(0, "pclk_pmu_noc", "pclk_pd_pmu", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(23), 2, GFLAGS),
	GATE(PCLK_SGRF, "pclk_sgrf", "pclk_pd_pmu", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(23), 3, GFLAGS),
	GATE(PCLK_GPIO0, "pclk_gpio0", "pclk_pd_pmu", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(23), 4, GFLAGS),
	GATE(PCLK_PMUGRF, "pclk_pmugrf", "pclk_pd_pmu", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(23), 5, GFLAGS),
	GATE(PCLK_I2C0, "pclk_i2c0", "pclk_pd_pmu", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(23), 6, GFLAGS),

	/* fclk_mcu_src gates */
	GATE(0, "fclk_mcu", "fclk_mcu_src", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(23), 7, GFLAGS),
	GATE(0, "hclk_mcu", "fclk_mcu_src", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(23), 8, GFLAGS),
	GATE(0, "hclk_mcu_noc", "fclk_mcu_src", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(23), 9, GFLAGS),

	/* pclk_pd_alive gates */
	GATE(PCLK_GPIO1, "pclk_gpio1", "pclk_pd_alive", 0, RK3368_CLKGATE_CON(22), 1, GFLAGS),
	GATE(PCLK_GPIO2, "pclk_gpio2", "pclk_pd_alive", 0, RK3368_CLKGATE_CON(22), 2, GFLAGS),
	GATE(PCLK_GPIO3, "pclk_gpio3", "pclk_pd_alive", 0, RK3368_CLKGATE_CON(22), 3, GFLAGS),
	GATE(PCLK_GPIO4, "pclk_gpio4", "pclk_pd_alive", 0, RK3368_CLKGATE_CON(22), 4, GFLAGS),
	GATE(PCLK_GPIO5, "pclk_gpio5", "pclk_pd_alive", 0, RK3368_CLKGATE_CON(22), 5, GFLAGS),
	GATE(PCLK_GRF, "pclk_grf", "pclk_pd_alive", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(22), 8, GFLAGS),
	GATE(0, "pclk_alive_niu", "pclk_pd_alive", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(22), 9, GFLAGS),
	GATE(PCLK_DPHYTX, "pclk_dphytx", "pclk_pd_alive", 0, RK3368_CLKGATE_CON(22), 10, GFLAGS),
	GATE(PCLK_DPHYRX, "pclk_dphyrx", "pclk_pd_alive", 0, RK3368_CLKGATE_CON(22), 11, GFLAGS),
	GATE(PCLK_TIMER0, "pclk_timer0", "pclk_pd_alive", 0, RK3368_CLKGATE_CON(22), 12, GFLAGS),

	/* pclk_cpu gates */
	GATE(PCLK_DMFIMON, "pclk_dmfimon", "pclk_bus", 0, RK3368_CLKGATE_CON(12), 0, GFLAGS),
	GATE(PCLK_MAILBOX, "pclk_mailbox", "pclk_bus", 0, RK3368_CLKGATE_CON(12), 1, GFLAGS),
	GATE(PCLK_DFC, "pclk_dfc", "pclk_bus", 0, RK3368_CLKGATE_CON(12), 2, GFLAGS),
	GATE(PCLK_I2C1, "pclk_i2c1", "pclk_bus", 0, RK3368_CLKGATE_CON(12), 3, GFLAGS),
	GATE(PCLK_DDRUPCTL, "pclk_ddrupctl", "pclk_bus", 0, RK3368_CLKGATE_CON(12), 13, GFLAGS),
	GATE(PCLK_DDRPHY, "pclk_ddrphy", "pclk_bus", 0, RK3368_CLKGATE_CON(12), 14, GFLAGS),
	GATE(PCLK_EFUSE_1024, "pclk_efuse_1024", "pclk_bus", 0, RK3368_CLKGATE_CON(13), 0, GFLAGS),
	GATE(PCLK_EFUSE_256, "pclk_efuse_256", "pclk_bus", 0, RK3368_CLKGATE_CON(13), 1, GFLAGS),
	GATE(PCLK_UART2, "pclk_uart2", "pclk_bus", 0, RK3368_CLKGATE_CON(13), 5, GFLAGS),
	GATE(PCLK_RKPWM, "pclk_rk_pwm", "pclk_bus", 0, RK3368_CLKGATE_CON(13), 6, GFLAGS),
	GATE(0, "pclk_ddrnoc", "pclk_bus", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(13), 10, GFLAGS),
	GATE(0, "pclk_ddr_sgrf", "pclk_bus", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(13), 11, GFLAGS),

	/* hclk_cpu gates */
	GATE(HCLK_I2S_8CH, "hclk_i2s_8ch", "hclk_bus", 0, RK3368_CLKGATE_CON(12), 7, GFLAGS),
	GATE(HCLK_I2S_2CH, "hclk_i2s_2ch", "hclk_bus", 0, RK3368_CLKGATE_CON(12), 8, GFLAGS),
	GATE(HCLK_ROM, "hclk_rom", "hclk_bus", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(12), 9, GFLAGS),
	GATE(HCLK_SPDIF, "hclk_spdif", "hclk_bus", 0, RK3368_CLKGATE_CON(12), 10, GFLAGS),
	GATE(MCLK_CRYPTO, "mclk_crypto", "hclk_bus", 0, RK3368_CLKGATE_CON(13), 3, GFLAGS),
	GATE(HCLK_CRYPTO, "hclk_crypto", "hclk_bus", 0, RK3368_CLKGATE_CON(13), 4, GFLAGS),

	/* aclk_bus gates */
	GATE(0, "aclk_intmem", "aclk_bus", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(12), 4, GFLAGS),
	GATE(0, "sclk_intmem0", "aclk_bus", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(12), 5, GFLAGS),
	GATE(0, "sclk_intmem1", "aclk_bus", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(12), 6, GFLAGS),
	GATE(ACLK_DMAC_BUS, "aclk_dmac_bus", "aclk_bus", 0, RK3368_CLKGATE_CON(12), 11, GFLAGS),
	GATE(0, "aclk_strc_sys", "aclk_bus", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(12), 12, GFLAGS),
	GATE(ACLK_DFC, "aclk_dfc", "aclk_bus", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(12), 15, GFLAGS),
	GATE(0, "aclk_gic400", "aclk_bus", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(13), 9, GFLAGS),

	/* clk_ddrphy gates */
	GATE(0, "clk_ddrupctl", "ddrphy_div4", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(13), 2, GFLAGS),

	/* clk_cs_pre gates */
	GATE(0, "sclk_cs_dbg", "sclk_cs_pre", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(9), 0, GFLAGS),
	GATE(0, "hclk_cs_dbg", "sclk_cs_pre", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(9), 1, GFLAGS),
	GATE(0, "pclk_cs_dbg", "sclk_cs_pre", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(9), 2, GFLAGS),

	/* armclk gates */
	GATE(0, "clk_core_cxcs", "armclk", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(9), 3, GFLAGS),

	/* aclkm_core gates */
	GATE(0, "aclk_core_noc", "aclkm_core", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(9), 4, GFLAGS),

	/* gpu gates */
	GATE(ACLK_GPU, "aclk_gpu", "sclk_gpu_core_src", 0, RK3368_CLKGATE_CON(18), 0, GFLAGS),
	GATE(ACLK_GPU_NOC, "aclk_gpu_noc", "sclk_gpu_core_src", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(18), 1, GFLAGS),

	/* aclk_peri0 gates */
	GATE(0, "aclk_peri0_axi_matrix", "aclk_peri0", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(19), 0, GFLAGS),
	GATE(ACLK_USB3, "aclk_usb3", "aclk_peri0", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(20), 6, GFLAGS),
	GATE(0, "aclk_peri0_noc", "aclk_peri0", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(20), 9, GFLAGS),
	GATE(ACLK_GMAC, "aclk_gmac", "aclk_peri0", 0, RK3368_CLKGATE_CON(20), 13, GFLAGS),

	/* hclk_peri0 gates */
	GATE(HCLK_OTG, "hclk_otg", "hclk_peri0", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(20), 1, GFLAGS),
	GATE(HCLK_HOST, "hclk_host", "hclk_peri0", 0, RK3368_CLKGATE_CON(20), 3, GFLAGS),
	GATE(0, "hclk_host_arbiter", "hclk_peri0", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(20), 4, GFLAGS),
	GATE(0, "hclk_peri0_ahb_arbiter", "hclk_peri0", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(20), 7, GFLAGS),
	GATE(HCLK_SDMMC, "hclk_sdmmc", "hclk_peri0", 0, RK3368_CLKGATE_CON(21), 0, GFLAGS),
	GATE(HCLK_SDIO, "hclk_sdio", "hclk_peri0", 0, RK3368_CLKGATE_CON(21), 1, GFLAGS),
	GATE(HCLK_EMMC, "hclk_emmc", "hclk_peri0", 0, RK3368_CLKGATE_CON(21), 2, GFLAGS),

	/* pclk_peri0 gates */
	GATE(PCLK_GMAC, "pclk_gmac", "pclk_peri0", 0, RK3368_CLKGATE_CON(20), 14, GFLAGS),

	/* aclk_peri1 gates */
	GATE(0, "aclk_peri1_axi_matrix", "aclk_peri1", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(19), 1, GFLAGS),
	GATE(ACLK_DMAC_PERI, "aclk_dmac_peri", "aclk_peri1", 0, RK3368_CLKGATE_CON(19), 3, GFLAGS),

	/* hclk_peri1 gates */
	GATE(0, "hclk_peri1_ahb_arbiter", "hclk_peri1", 0, RK3368_CLKGATE_CON(20), 8, GFLAGS),
	GATE(HCLK_NANDC0, "hclk_nandc0", "hclk_peri1", 0, RK3368_CLKGATE_CON(20), 11, GFLAGS),
	GATE(HCLK_SFC, "hclk_sfc", "hclk_peri1", 0, RK3368_CLKGATE_CON(20), 15, GFLAGS),

	/* pclk_peri1 gates */
	GATE(PCLK_SPI0, "pclk_spi0", "pclk_peri1", 0, RK3368_CLKGATE_CON(19), 4, GFLAGS),
	GATE(PCLK_SPI1, "pclk_spi1", "pclk_peri1", 0, RK3368_CLKGATE_CON(19), 5, GFLAGS),
	GATE(PCLK_UART0, "pclk_uart0", "pclk_peri1", 0, RK3368_CLKGATE_CON(19), 7, GFLAGS),
	GATE(PCLK_UART3, "pclk_uart3", "pclk_peri1", 0, RK3368_CLKGATE_CON(19), 9, GFLAGS),
	GATE(PCLK_I2C2, "pclk_i2c2", "pclk_peri1", 0, RK3368_CLKGATE_CON(19), 11, GFLAGS),
	GATE(PCLK_I2C3, "pclk_i2c3", "pclk_peri1", 0, RK3368_CLKGATE_CON(19), 12, GFLAGS),
	GATE(PCLK_I2C4, "pclk_i2c4", "pclk_peri1", 0, RK3368_CLKGATE_CON(19), 13, GFLAGS),
	GATE(PCLK_I2C5, "pclk_i2c5", "pclk_peri1", 0, RK3368_CLKGATE_CON(19), 14, GFLAGS),
	GATE(PCLK_SARADC, "pclk_saradc", "pclk_peri1", 0, RK3368_CLKGATE_CON(19), 15, GFLAGS),
	GATE(PCLK_TSADC, "pclk_tsadc", "pclk_peri1", 0, RK3368_CLKGATE_CON(20), 0, GFLAGS),
	GATE(PCLK_SIM, "pclk_sim", "pclk_peri1", 0, RK3368_CLKGATE_CON(21), 7, GFLAGS),

	/*
	 * video clk gates
	 * aclk_video(_pre) can actually select between parents of aclk_vdpu
	 * and aclk_vepu by setting bit GRF_SOC_CON0[7].
	 */
	GATE(ACLK_VIDEO, "aclk_video", "aclk_vdpu", 0, RK3368_CLKGATE_CON(15), 0, GFLAGS),
	GATE(0, "aclk_video_noc", "aclk_vdpu", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(15), 4, GFLAGS),
	GATE(HCLK_VIDEO, "hclk_video", "hclk_video_pre", 0, RK3368_CLKGATE_CON(15), 1, GFLAGS),
	GATE(0, "hclk_video_noc", "hclk_video_pre", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(15), 5, GFLAGS),

	GATE(ACLK_RKVDEC, "aclk_rkvdec", "aclk_rkvdec_pre", 0, RK3368_CLKGATE_CON(15), 6, GFLAGS),
	GATE(0, "aclk_rkvdec_noc", "aclk_rkvdec_pre", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(15), 2, GFLAGS),
	GATE(0, "hclk_rkvdec_noc", "hclk_rkvdec_pre", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(15), 3, GFLAGS),
	GATE(HCLK_RKVDEC, "hclk_rkvdec", "hclk_rkvdec_pre", 0, RK3368_CLKGATE_CON(15), 7, GFLAGS),

	/* aclk_rga_pre gates */
	GATE(ACLK_RGA, "aclk_rga", "aclk_rga_pre", 0, RK3368_CLKGATE_CON(16), 0, GFLAGS),
	GATE(0, "aclk_vio1_noc", "aclk_rga_pre", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(16), 10, GFLAGS),
	GATE(ACLK_VOP_LITE, "aclk_vop_lite", "aclk_rga_pre", 0, RK3368_CLKGATE_CON(17), 13, GFLAGS),

	/* aclk_vio0 gates */
	GATE(ACLK_IEP, "aclk_iep", "aclk_vio0", 0, RK3368_CLKGATE_CON(16), 2, GFLAGS),
	GATE(ACLK_VOP_FULL, "aclk_vop_full", "aclk_vio0", 0, RK3368_CLKGATE_CON(16), 5, GFLAGS),
	GATE(0, "aclk_vio0_noc", "aclk_vio0", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(16), 9, GFLAGS),
	GATE(ACLK_VOP_IEP, "aclk_vop_iep", "aclk_vio0", 0, RK3368_CLKGATE_CON(16), 4, GFLAGS),

	/* sclk_isp gates */
	GATE(ACLK_ISP, "aclk_isp", "sclk_isp", 0, RK3368_CLKGATE_CON(17), 0, GFLAGS),
	GATE(0, "hclk_isp_noc", "sclk_isp", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(17), 1, GFLAGS),
	GATE(HCLK_ISP, "hclk_isp", "sclk_isp", 0, RK3368_CLKGATE_CON(16), 14, GFLAGS),

	/* aclk_hdcp_pre gates */
	GATE(ACLK_HDCP, "aclk_hdcp", "aclk_hdcp_pre", 0, RK3368_CLKGATE_CON(17), 10, GFLAGS),
	GATE(0, "aclk_hdcp_noc", "aclk_hdcp_pre", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(17), 15, GFLAGS),

	/* hclk_vio gates */
	GATE(HCLK_RGA, "hclk_rga", "hclk_vio", 0, RK3368_CLKGATE_CON(16), 1, GFLAGS),
	GATE(HCLK_IEP, "hclk_iep", "hclk_vio", 0, RK3368_CLKGATE_CON(16), 3, GFLAGS),
	GATE(HCLK_VOP_FULL, "hclk_vop_full", "hclk_vio", 0, RK3368_CLKGATE_CON(16), 6, GFLAGS),
	GATE(HCLK_VIO_AHB_ARBITER, "hclk_vio_ahb_arbiter", "hclk_vio", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(16), 7, GFLAGS),
	GATE(HCLK_VIO_NOC, "hclk_vio_noc", "hclk_vio", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(16), 8, GFLAGS),
	GATE(HCLK_VIO_H2P, "hclk_vio_h2p", "hclk_vio", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(17), 7, GFLAGS),
	GATE(HCLK_VOP_LITE, "hclk_vop_lite", "hclk_vio", 0, RK3368_CLKGATE_CON(17), 14, GFLAGS),
	GATE(PCLK_HDCP, "pclk_hdcp", "hclk_vio", 0, RK3368_CLKGATE_CON(17), 11, GFLAGS),
	GATE(HCLK_VIO_HDCPMMU, "hclk_hdcpmmu", "hclk_vio", 0, RK3368_CLKGATE_CON(17), 12, GFLAGS),
	GATE(PCLK_HDMI_CTRL, "pclk_hdmi_ctrl", "hclk_vio", 0, RK3368_CLKGATE_CON(17), 6, GFLAGS),
	GATE(PCLK_VIO_H2P, "pclk_vio_h2p", "hclk_vio", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(17), 8, GFLAGS),
	GATE(PCLK_MIPI_DSI0, "pclk_mipi_dsi0", "hclk_vio", 0, RK3368_CLKGATE_CON(17), 3, GFLAGS),

	/* timer gates */
	GATE(SCLK_TIMER5, "sclk_timer05", "xin24m", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(24), 5, GFLAGS),
	GATE(SCLK_TIMER4, "sclk_timer04", "xin24m", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(24), 4, GFLAGS),
	GATE(SCLK_TIMER3, "sclk_timer03", "xin24m", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(24), 3, GFLAGS),
	GATE(SCLK_TIMER2, "sclk_timer02", "xin24m", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(24), 2, GFLAGS),
	GATE(SCLK_TIMER1, "sclk_timer01", "xin24m", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(24), 1, GFLAGS),
	GATE(SCLK_TIMER0, "sclk_timer00", "xin24m", CLK_IGNORE_UNUSED, RK3368_CLKGATE_CON(24), 0, GFLAGS),
};

static const char *const rk3366_critical_clocks[] __initconst = {
	"aclk_bus",
	"aclk_peri0",
	"aclk_peri1",
	"aclk_video_noc",
	"aclk_rkvdec_noc",
	"hclk_peri0",
	"hclk_peri1",
	"hclk_video_noc",
	"hclk_rkvdec_noc",
	"pclk_peri0",
	"pclk_peri1",
	"pclk_rk_pwm",
	"pclk_pd_pmu",
};

static void __init rk3366_clk_init(struct device_node *np)
{
	struct rockchip_clk_provider *ctx;
	void __iomem *reg_base;
	struct clk *clk;

	reg_base = of_iomap(np, 0);
	if (!reg_base) {
		pr_err("%s: could not map cru region\n", __func__);
		return;
	}

	ctx = rockchip_clk_init(np, reg_base, CLK_NR_CLKS);
	if (IS_ERR(ctx)) {
		pr_err("%s: rockchip clk init failed\n", __func__);
		iounmap(reg_base);
		return;
	}

	/* xin12m is created by a cru-internal divider */
	clk = clk_register_fixed_factor(NULL, "xin12m", "xin24m", 0, 1, 2);
	if (IS_ERR(clk))
		pr_warn("%s: could not register clock xin12m: %ld\n",
			__func__, PTR_ERR(clk));

	/* ddrphy_div4 is created by a cru-internal divider */
	clk = clk_register_fixed_factor(NULL, "ddrphy_div4", "ddrphy_src", 0, 1, 4);
	if (IS_ERR(clk))
		pr_warn("%s: could not register clock xin12m: %ld\n",
			__func__, PTR_ERR(clk));

	clk = clk_register_fixed_factor(NULL, "hclk_video_pre",
					"hclk_video_pre_v", 0, 1, 4);
	if (IS_ERR(clk))
		pr_warn("%s: could not register clock hclk_vcodec_pre: %ld\n",
			__func__, PTR_ERR(clk));

	clk = clk_register_fixed_factor(NULL, "hclk_rkvdec_pre",
					"hclk_rkvdec_pre_v", 0, 1, 4);
	if (IS_ERR(clk))
		pr_warn("%s: could not register clock hclk_rkvdec_pre: %ld\n",
			__func__, PTR_ERR(clk));

	/* Watchdog pclk is controlled by sgrf_soc_con3[7]. */
	clk = clk_register_fixed_factor(NULL, "pclk_wdt", "pclk_pd_alive", 0, 1, 1);
	if (IS_ERR(clk))
		pr_warn("%s: could not register clock pclk_wdt: %ld\n",
			__func__, PTR_ERR(clk));
	else
		rockchip_clk_add_lookup(ctx, clk, PCLK_WDT);

	rockchip_clk_register_plls(ctx, rk3366_pll_clks,
				   ARRAY_SIZE(rk3366_pll_clks),
				   RK3366_GRF_SOC_STATUS0);
	rockchip_clk_register_branches(ctx, rk3366_clk_branches,
				  ARRAY_SIZE(rk3366_clk_branches));
	rockchip_clk_protect_critical(rk3366_critical_clocks,
				      ARRAY_SIZE(rk3366_critical_clocks));

	rockchip_clk_register_armclk(ctx, ARMCLK, "armclk",
			mux_armclk_p, ARRAY_SIZE(mux_armclk_p),
			&rk3366_cpuclk_data, rk3366_cpuclk_rates,
			ARRAY_SIZE(rk3366_cpuclk_rates));

	rockchip_register_softrst(np, 15, reg_base + RK3368_SOFTRST_CON(0),
				  ROCKCHIP_SOFTRST_HIWORD_MASK);

	rockchip_register_restart_notifier(ctx, RK3368_GLB_SRST_FST, NULL);

	rockchip_clk_of_add_provider(np, ctx);
}
CLK_OF_DECLARE(rk3368_cru, "rockchip,rk3366-cru", rk3366_clk_init);
