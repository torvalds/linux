/*
 * Copyright (c) 2014 MundoReader S.L.
 * Author: Heiko Stuebner <heiko@sntech.de>
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
#include <dt-bindings/clock/rk3188-cru-common.h>
#include "clk.h"

#define RK3066_GRF_SOC_STATUS	0x15c
#define RK3188_GRF_SOC_STATUS	0xac

enum rk3188_plls {
	apll, cpll, dpll, gpll,
};

struct rockchip_pll_rate_table rk3188_pll_rates[] = {
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
	RK3066_PLL_RATE(1188000000, 2, 99, 1),
	RK3066_PLL_RATE(1176000000, 1, 49, 1),
	RK3066_PLL_RATE(1128000000, 1, 47, 1),
	RK3066_PLL_RATE(1104000000, 1, 46, 1),
	RK3066_PLL_RATE(1008000000, 1, 84, 2),
	RK3066_PLL_RATE( 912000000, 1, 76, 2),
	RK3066_PLL_RATE( 891000000, 8, 594, 2),
	RK3066_PLL_RATE( 888000000, 1, 74, 2),
	RK3066_PLL_RATE( 816000000, 1, 68, 2),
	RK3066_PLL_RATE( 798000000, 2, 133, 2),
	RK3066_PLL_RATE( 792000000, 1, 66, 2),
	RK3066_PLL_RATE( 768000000, 1, 64, 2),
	RK3066_PLL_RATE( 742500000, 8, 495, 2),
	RK3066_PLL_RATE( 696000000, 1, 58, 2),
	RK3066_PLL_RATE( 600000000, 1, 50, 2),
	RK3066_PLL_RATE( 594000000, 2, 198, 4),
	RK3066_PLL_RATE( 552000000, 1, 46, 2),
	RK3066_PLL_RATE( 504000000, 1, 84, 4),
	RK3066_PLL_RATE( 456000000, 1, 76, 4),
	RK3066_PLL_RATE( 408000000, 1, 68, 4),
	RK3066_PLL_RATE( 384000000, 2, 128, 4),
	RK3066_PLL_RATE( 360000000, 1, 60, 4),
	RK3066_PLL_RATE( 312000000, 1, 52, 4),
	RK3066_PLL_RATE( 300000000, 1, 50, 4),
	RK3066_PLL_RATE( 297000000, 2, 198, 8),
	RK3066_PLL_RATE( 252000000, 1, 84, 8),
	RK3066_PLL_RATE( 216000000, 1, 72, 8),
	RK3066_PLL_RATE( 148500000, 2, 99, 8),
	RK3066_PLL_RATE( 126000000, 1, 84, 16),
	RK3066_PLL_RATE(  48000000, 1, 64, 32),
	{ /* sentinel */ },
};

#define RK3066_DIV_CORE_PERIPH_MASK	0x3
#define RK3066_DIV_CORE_PERIPH_SHIFT	6
#define RK3066_DIV_ACLK_CORE_MASK	0x7
#define RK3066_DIV_ACLK_CORE_SHIFT	0
#define RK3066_DIV_ACLK_HCLK_MASK	0x3
#define RK3066_DIV_ACLK_HCLK_SHIFT	8
#define RK3066_DIV_ACLK_PCLK_MASK	0x3
#define RK3066_DIV_ACLK_PCLK_SHIFT	12
#define RK3066_DIV_AHB2APB_MASK		0x3
#define RK3066_DIV_AHB2APB_SHIFT	14

#define RK3066_CLKSEL0(_core_peri)					\
	{								\
		.reg = RK2928_CLKSEL_CON(0),				\
		.val = HIWORD_UPDATE(_core_peri, RK3066_DIV_CORE_PERIPH_MASK, \
				RK3066_DIV_CORE_PERIPH_SHIFT)		\
	}
#define RK3066_CLKSEL1(_aclk_core, _aclk_hclk, _aclk_pclk, _ahb2apb)	\
	{								\
		.reg = RK2928_CLKSEL_CON(1),				\
		.val = HIWORD_UPDATE(_aclk_core, RK3066_DIV_ACLK_CORE_MASK, \
				RK3066_DIV_ACLK_CORE_SHIFT) |		\
		       HIWORD_UPDATE(_aclk_hclk, RK3066_DIV_ACLK_HCLK_MASK, \
				RK3066_DIV_ACLK_HCLK_SHIFT) |		\
		       HIWORD_UPDATE(_aclk_pclk, RK3066_DIV_ACLK_PCLK_MASK, \
				RK3066_DIV_ACLK_PCLK_SHIFT) |		\
		       HIWORD_UPDATE(_ahb2apb, RK3066_DIV_AHB2APB_MASK,	\
				RK3066_DIV_AHB2APB_SHIFT),		\
	}

#define RK3066_CPUCLK_RATE(_prate, _core_peri, _acore, _ahclk, _apclk, _h2p) \
	{								\
		.prate = _prate,					\
		.divs = {						\
			RK3066_CLKSEL0(_core_peri),			\
			RK3066_CLKSEL1(_acore, _ahclk, _apclk, _h2p),	\
		},							\
	}

static struct rockchip_cpuclk_rate_table rk3066_cpuclk_rates[] __initdata = {
	RK3066_CPUCLK_RATE(1416000000, 2, 3, 1, 2, 1),
	RK3066_CPUCLK_RATE(1200000000, 2, 3, 1, 2, 1),
	RK3066_CPUCLK_RATE(1008000000, 2, 2, 1, 2, 1),
	RK3066_CPUCLK_RATE( 816000000, 2, 2, 1, 2, 1),
	RK3066_CPUCLK_RATE( 600000000, 1, 2, 1, 2, 1),
	RK3066_CPUCLK_RATE( 504000000, 1, 1, 1, 2, 1),
	RK3066_CPUCLK_RATE( 312000000, 0, 1, 1, 1, 0),
};

static const struct rockchip_cpuclk_reg_data rk3066_cpuclk_data = {
	.core_reg = RK2928_CLKSEL_CON(0),
	.div_core_shift = 0,
	.div_core_mask = 0x1f,
	.mux_core_shift = 8,
};

#define RK3188_DIV_ACLK_CORE_MASK	0x7
#define RK3188_DIV_ACLK_CORE_SHIFT	3

#define RK3188_CLKSEL1(_aclk_core)		\
	{					\
		.reg = RK2928_CLKSEL_CON(1),	\
		.val = HIWORD_UPDATE(_aclk_core, RK3188_DIV_ACLK_CORE_MASK,\
				 RK3188_DIV_ACLK_CORE_SHIFT) \
	}
#define RK3188_CPUCLK_RATE(_prate, _core_peri, _aclk_core)	\
	{							\
		.prate = _prate,				\
		.divs = {					\
			RK3066_CLKSEL0(_core_peri),		\
			RK3188_CLKSEL1(_aclk_core),		\
		},						\
	}

static struct rockchip_cpuclk_rate_table rk3188_cpuclk_rates[] __initdata = {
	RK3188_CPUCLK_RATE(1608000000, 2, 3),
	RK3188_CPUCLK_RATE(1416000000, 2, 3),
	RK3188_CPUCLK_RATE(1200000000, 2, 3),
	RK3188_CPUCLK_RATE(1008000000, 2, 3),
	RK3188_CPUCLK_RATE( 816000000, 2, 3),
	RK3188_CPUCLK_RATE( 600000000, 1, 3),
	RK3188_CPUCLK_RATE( 504000000, 1, 3),
	RK3188_CPUCLK_RATE( 312000000, 0, 1),
};

static const struct rockchip_cpuclk_reg_data rk3188_cpuclk_data = {
	.core_reg = RK2928_CLKSEL_CON(0),
	.div_core_shift = 9,
	.div_core_mask = 0x1f,
	.mux_core_shift = 8,
};

PNAME(mux_pll_p)		= { "xin24m", "xin32k" };
PNAME(mux_armclk_p)		= { "apll", "gpll_armclk" };
PNAME(mux_ddrphy_p)		= { "dpll", "gpll_ddr" };
PNAME(mux_pll_src_gpll_cpll_p)	= { "gpll", "cpll" };
PNAME(mux_pll_src_cpll_gpll_p)	= { "cpll", "gpll" };
PNAME(mux_aclk_cpu_p)		= { "apll", "gpll" };
PNAME(mux_sclk_cif0_p)		= { "cif0_pre", "xin24m" };
PNAME(mux_sclk_i2s0_p)		= { "i2s0_pre", "i2s0_frac", "xin12m" };
PNAME(mux_sclk_spdif_p)		= { "spdif_src", "spdif_frac", "xin12m" };
PNAME(mux_sclk_uart0_p)		= { "uart0_pre", "uart0_frac", "xin24m" };
PNAME(mux_sclk_uart1_p)		= { "uart1_pre", "uart1_frac", "xin24m" };
PNAME(mux_sclk_uart2_p)		= { "uart2_pre", "uart2_frac", "xin24m" };
PNAME(mux_sclk_uart3_p)		= { "uart3_pre", "uart3_frac", "xin24m" };
PNAME(mux_sclk_hsadc_p)		= { "hsadc_src", "hsadc_frac", "ext_hsadc" };
PNAME(mux_mac_p)		= { "gpll", "dpll" };
PNAME(mux_sclk_macref_p)	= { "mac_src", "ext_rmii" };

static struct rockchip_pll_clock rk3188_pll_clks[] __initdata = {
	[apll] = PLL(pll_rk3066, PLL_APLL, "apll", mux_pll_p, 0, RK2928_PLL_CON(0),
		     RK2928_MODE_CON, 0, 6, rk3188_pll_rates),
	[dpll] = PLL(pll_rk3066, PLL_DPLL, "dpll", mux_pll_p, 0, RK2928_PLL_CON(4),
		     RK2928_MODE_CON, 4, 5, NULL),
	[cpll] = PLL(pll_rk3066, PLL_CPLL, "cpll", mux_pll_p, 0, RK2928_PLL_CON(8),
		     RK2928_MODE_CON, 8, 7, rk3188_pll_rates),
	[gpll] = PLL(pll_rk3066, PLL_GPLL, "gpll", mux_pll_p, 0, RK2928_PLL_CON(12),
		     RK2928_MODE_CON, 12, 8, rk3188_pll_rates),
};

#define MFLAGS CLK_MUX_HIWORD_MASK
#define DFLAGS CLK_DIVIDER_HIWORD_MASK
#define GFLAGS (CLK_GATE_HIWORD_MASK | CLK_GATE_SET_TO_DISABLE)

/* 2 ^ (val + 1) */
static struct clk_div_table div_core_peri_t[] = {
	{ .val = 0, .div = 2 },
	{ .val = 1, .div = 4 },
	{ .val = 2, .div = 8 },
	{ .val = 3, .div = 16 },
	{ /* sentinel */ },
};

static struct rockchip_clk_branch common_clk_branches[] __initdata = {
	/*
	 * Clock-Architecture Diagram 2
	 */

	GATE(0, "gpll_armclk", "gpll", 0, RK2928_CLKGATE_CON(0), 1, GFLAGS),

	/* these two are set by the cpuclk and should not be changed */
	COMPOSITE_NOMUX_DIVTBL(CORE_PERI, "core_peri", "armclk", 0,
			RK2928_CLKSEL_CON(0), 6, 2, DFLAGS | CLK_DIVIDER_READ_ONLY,
			div_core_peri_t, RK2928_CLKGATE_CON(0), 0, GFLAGS),

	COMPOSITE(0, "aclk_vepu", mux_pll_src_cpll_gpll_p, 0,
			RK2928_CLKSEL_CON(32), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RK2928_CLKGATE_CON(3), 9, GFLAGS),
	GATE(0, "hclk_vepu", "aclk_vepu", 0,
			RK2928_CLKGATE_CON(3), 10, GFLAGS),
	COMPOSITE(0, "aclk_vdpu", mux_pll_src_cpll_gpll_p, 0,
			RK2928_CLKSEL_CON(32), 15, 1, MFLAGS, 8, 5, DFLAGS,
			RK2928_CLKGATE_CON(3), 11, GFLAGS),
	GATE(0, "hclk_vdpu", "aclk_vdpu", 0,
			RK2928_CLKGATE_CON(3), 12, GFLAGS),

	GATE(0, "gpll_ddr", "gpll", 0,
			RK2928_CLKGATE_CON(1), 7, GFLAGS),
	COMPOSITE(0, "ddrphy", mux_ddrphy_p, 0,
			RK2928_CLKSEL_CON(26), 8, 1, MFLAGS, 0, 2, DFLAGS | CLK_DIVIDER_POWER_OF_TWO,
			RK2928_CLKGATE_CON(0), 2, GFLAGS),

	GATE(0, "aclk_cpu", "aclk_cpu_pre", 0,
			RK2928_CLKGATE_CON(0), 3, GFLAGS),

	GATE(0, "atclk_cpu", "pclk_cpu_pre", 0,
			RK2928_CLKGATE_CON(0), 6, GFLAGS),
	GATE(0, "pclk_cpu", "pclk_cpu_pre", 0,
			RK2928_CLKGATE_CON(0), 5, GFLAGS),
	GATE(0, "hclk_cpu", "hclk_cpu_pre", 0,
			RK2928_CLKGATE_CON(0), 4, GFLAGS),

	COMPOSITE(0, "aclk_lcdc0_pre", mux_pll_src_cpll_gpll_p, 0,
			RK2928_CLKSEL_CON(31), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RK2928_CLKGATE_CON(3), 0, GFLAGS),
	COMPOSITE(0, "aclk_lcdc1_pre", mux_pll_src_cpll_gpll_p, 0,
			RK2928_CLKSEL_CON(31), 15, 1, MFLAGS, 8, 5, DFLAGS,
			RK2928_CLKGATE_CON(1), 4, GFLAGS),

	GATE(0, "aclk_peri", "aclk_peri_pre", 0,
			RK2928_CLKGATE_CON(2), 1, GFLAGS),
	COMPOSITE_NOMUX(0, "hclk_peri", "aclk_peri_pre", 0,
			RK2928_CLKSEL_CON(10), 8, 2, DFLAGS | CLK_DIVIDER_POWER_OF_TWO,
			RK2928_CLKGATE_CON(2), 2, GFLAGS),
	COMPOSITE_NOMUX(0, "pclk_peri", "aclk_peri_pre", 0,
			RK2928_CLKSEL_CON(10), 12, 2, DFLAGS | CLK_DIVIDER_POWER_OF_TWO,
			RK2928_CLKGATE_CON(2), 3, GFLAGS),

	MUX(0, "cif_src", mux_pll_src_cpll_gpll_p, 0,
			RK2928_CLKSEL_CON(29), 0, 1, MFLAGS),
	COMPOSITE_NOMUX(0, "cif0_pre", "cif_src", 0,
			RK2928_CLKSEL_CON(29), 1, 5, DFLAGS,
			RK2928_CLKGATE_CON(3), 7, GFLAGS),
	MUX(SCLK_CIF0, "sclk_cif0", mux_sclk_cif0_p, 0,
			RK2928_CLKSEL_CON(29), 7, 1, MFLAGS),

	GATE(0, "pclkin_cif0", "ext_cif0", 0,
			RK2928_CLKGATE_CON(3), 3, GFLAGS),

	/*
	 * the 480m are generated inside the usb block from these clocks,
	 * but they are also a source for the hsicphy clock.
	 */
	GATE(SCLK_OTGPHY0, "sclk_otgphy0", "usb480m", 0,
			RK2928_CLKGATE_CON(1), 5, GFLAGS),
	GATE(SCLK_OTGPHY1, "sclk_otgphy1", "usb480m", 0,
			RK2928_CLKGATE_CON(1), 6, GFLAGS),

	COMPOSITE(0, "mac_src", mux_mac_p, 0,
			RK2928_CLKSEL_CON(21), 0, 1, MFLAGS, 8, 5, DFLAGS,
			RK2928_CLKGATE_CON(2), 5, GFLAGS),
	MUX(SCLK_MAC, "sclk_macref", mux_sclk_macref_p, CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(21), 4, 1, MFLAGS),
	GATE(0, "sclk_mac_lbtest", "sclk_macref",
			RK2928_CLKGATE_CON(2), 12, 0, GFLAGS),

	COMPOSITE(0, "hsadc_src", mux_pll_src_gpll_cpll_p, 0,
			RK2928_CLKSEL_CON(22), 0, 1, MFLAGS, 8, 8, DFLAGS,
			RK2928_CLKGATE_CON(2), 6, GFLAGS),
	COMPOSITE_FRAC(0, "hsadc_frac", "hsadc_src",
			RK2928_CLKSEL_CON(23), 0,
			RK2928_CLKGATE_CON(2), 7, 0, GFLAGS),
	MUX(SCLK_HSADC, "sclk_hsadc", mux_sclk_hsadc_p, 0,
			RK2928_CLKSEL_CON(22), 4, 2, MFLAGS),

	COMPOSITE_NOMUX(SCLK_SARADC, "sclk_saradc", "xin24m", 0,
			RK2928_CLKSEL_CON(24), 8, 8, DFLAGS,
			RK2928_CLKGATE_CON(2), 8, GFLAGS),

	/*
	 * Clock-Architecture Diagram 4
	 */

	GATE(SCLK_SMC, "sclk_smc", "hclk_peri",
			RK2928_CLKGATE_CON(2), 4, 0, GFLAGS),

	COMPOSITE_NOMUX(SCLK_SPI0, "sclk_spi0", "pclk_peri", 0,
			RK2928_CLKSEL_CON(25), 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(2), 9, GFLAGS),
	COMPOSITE_NOMUX(SCLK_SPI1, "sclk_spi1", "pclk_peri", 0,
			RK2928_CLKSEL_CON(25), 8, 7, DFLAGS,
			RK2928_CLKGATE_CON(2), 10, GFLAGS),

	COMPOSITE_NOMUX(SCLK_SDMMC, "sclk_sdmmc", "hclk_peri", 0,
			RK2928_CLKSEL_CON(11), 0, 6, DFLAGS,
			RK2928_CLKGATE_CON(2), 11, GFLAGS),
	COMPOSITE_NOMUX(SCLK_SDIO, "sclk_sdio", "hclk_peri", 0,
			RK2928_CLKSEL_CON(12), 0, 6, DFLAGS,
			RK2928_CLKGATE_CON(2), 13, GFLAGS),
	COMPOSITE_NOMUX(SCLK_EMMC, "sclk_emmc", "hclk_peri", 0,
			RK2928_CLKSEL_CON(12), 8, 6, DFLAGS,
			RK2928_CLKGATE_CON(2), 14, GFLAGS),

	MUX(0, "uart_src", mux_pll_src_gpll_cpll_p, 0,
			RK2928_CLKSEL_CON(12), 15, 1, MFLAGS),
	COMPOSITE_NOMUX(0, "uart0_pre", "uart_src", 0,
			RK2928_CLKSEL_CON(13), 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(1), 8, GFLAGS),
	COMPOSITE_FRAC(0, "uart0_frac", "uart0_pre", 0,
			RK2928_CLKSEL_CON(17), 0,
			RK2928_CLKGATE_CON(1), 9, GFLAGS),
	MUX(SCLK_UART0, "sclk_uart0", mux_sclk_uart0_p, 0,
			RK2928_CLKSEL_CON(13), 8, 2, MFLAGS),
	COMPOSITE_NOMUX(0, "uart1_pre", "uart_src", 0,
			RK2928_CLKSEL_CON(14), 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(1), 10, GFLAGS),
	COMPOSITE_FRAC(0, "uart1_frac", "uart1_pre", 0,
			RK2928_CLKSEL_CON(18), 0,
			RK2928_CLKGATE_CON(1), 11, GFLAGS),
	MUX(SCLK_UART1, "sclk_uart1", mux_sclk_uart1_p, 0,
			RK2928_CLKSEL_CON(14), 8, 2, MFLAGS),
	COMPOSITE_NOMUX(0, "uart2_pre", "uart_src", 0,
			RK2928_CLKSEL_CON(15), 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(1), 12, GFLAGS),
	COMPOSITE_FRAC(0, "uart2_frac", "uart2_pre", 0,
			RK2928_CLKSEL_CON(19), 0,
			RK2928_CLKGATE_CON(1), 13, GFLAGS),
	MUX(SCLK_UART2, "sclk_uart2", mux_sclk_uart2_p, 0,
			RK2928_CLKSEL_CON(15), 8, 2, MFLAGS),
	COMPOSITE_NOMUX(0, "uart3_pre", "uart_src", 0,
			RK2928_CLKSEL_CON(16), 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(1), 14, GFLAGS),
	COMPOSITE_FRAC(0, "uart3_frac", "uart3_pre", 0,
			RK2928_CLKSEL_CON(20), 0,
			RK2928_CLKGATE_CON(1), 15, GFLAGS),
	MUX(SCLK_UART3, "sclk_uart3", mux_sclk_uart3_p, 0,
			RK2928_CLKSEL_CON(16), 8, 2, MFLAGS),

	GATE(SCLK_JTAG, "jtag", "ext_jtag", 0, RK2928_CLKGATE_CON(1), 3, GFLAGS),

	GATE(SCLK_TIMER0, "timer0", "xin24m", 0, RK2928_CLKGATE_CON(1), 0, GFLAGS),
	GATE(SCLK_TIMER1, "timer1", "xin24m", 0, RK2928_CLKGATE_CON(1), 1, GFLAGS),

	/* clk_core_pre gates */
	GATE(0, "core_dbg", "armclk", 0, RK2928_CLKGATE_CON(9), 0, GFLAGS),

	/* aclk_cpu gates */
	GATE(ACLK_DMA1, "aclk_dma1", "aclk_cpu", 0, RK2928_CLKGATE_CON(5), 0, GFLAGS),
	GATE(0, "aclk_intmem", "aclk_cpu", 0, RK2928_CLKGATE_CON(4), 12, GFLAGS),
	GATE(0, "aclk_strc_sys", "aclk_cpu", 0, RK2928_CLKGATE_CON(4), 10, GFLAGS),

	/* hclk_cpu gates */
	GATE(HCLK_ROM, "hclk_rom", "hclk_cpu", 0, RK2928_CLKGATE_CON(5), 6, GFLAGS),
	GATE(HCLK_I2S0, "hclk_i2s0", "hclk_cpu", 0, RK2928_CLKGATE_CON(7), 2, GFLAGS),
	GATE(HCLK_SPDIF, "hclk_spdif", "hclk_cpu", 0, RK2928_CLKGATE_CON(7), 1, GFLAGS),
	GATE(0, "hclk_cpubus", "hclk_cpu", 0, RK2928_CLKGATE_CON(4), 8, GFLAGS),
	/* hclk_ahb2apb is part of a clk branch */
	GATE(0, "hclk_vio_bus", "hclk_cpu", 0, RK2928_CLKGATE_CON(6), 12, GFLAGS),
	GATE(HCLK_LCDC0, "hclk_lcdc0", "hclk_cpu", 0, RK2928_CLKGATE_CON(6), 1, GFLAGS),
	GATE(HCLK_LCDC1, "hclk_lcdc1", "aclk_cpu", 0, RK2928_CLKGATE_CON(6), 2, GFLAGS),
	GATE(HCLK_CIF0, "hclk_cif0", "hclk_cpu", 0, RK2928_CLKGATE_CON(6), 4, GFLAGS),
	GATE(HCLK_IPP, "hclk_ipp", "hclk_cpu", 0, RK2928_CLKGATE_CON(6), 9, GFLAGS),
	GATE(HCLK_RGA, "hclk_rga", "hclk_cpu", 0, RK2928_CLKGATE_CON(6), 10, GFLAGS),

	/* hclk_peri gates */
	GATE(0, "hclk_peri_axi_matrix", "hclk_peri", 0, RK2928_CLKGATE_CON(4), 0, GFLAGS),
	GATE(0, "hclk_peri_ahb_arbi", "hclk_peri", 0, RK2928_CLKGATE_CON(4), 6, GFLAGS),
	GATE(0, "hclk_emem_peri", "hclk_peri", 0, RK2928_CLKGATE_CON(4), 7, GFLAGS),
	GATE(HCLK_EMAC, "hclk_emac", "hclk_peri", 0, RK2928_CLKGATE_CON(7), 0, GFLAGS),
	GATE(HCLK_NANDC0, "hclk_nandc0", "hclk_peri", 0, RK2928_CLKGATE_CON(5), 9, GFLAGS),
	GATE(0, "hclk_usb_peri", "hclk_peri", 0, RK2928_CLKGATE_CON(4), 5, GFLAGS),
	GATE(HCLK_OTG0, "hclk_usbotg0", "hclk_peri", 0, RK2928_CLKGATE_CON(5), 13, GFLAGS),
	GATE(HCLK_HSADC, "hclk_hsadc", "hclk_peri", 0, RK2928_CLKGATE_CON(7), 5, GFLAGS),
	GATE(HCLK_PIDF, "hclk_pidfilter", "hclk_peri", 0, RK2928_CLKGATE_CON(7), 6, GFLAGS),
	GATE(HCLK_SDMMC, "hclk_sdmmc", "hclk_peri", 0, RK2928_CLKGATE_CON(5), 10, GFLAGS),
	GATE(HCLK_SDIO, "hclk_sdio", "hclk_peri", 0, RK2928_CLKGATE_CON(5), 11, GFLAGS),
	GATE(HCLK_EMMC, "hclk_emmc", "hclk_peri", 0, RK2928_CLKGATE_CON(5), 12, GFLAGS),

	/* aclk_lcdc0_pre gates */
	GATE(0, "aclk_vio0", "aclk_lcdc0_pre", 0, RK2928_CLKGATE_CON(6), 13, GFLAGS),
	GATE(ACLK_LCDC0, "aclk_lcdc0", "aclk_vio0", 0, RK2928_CLKGATE_CON(6), 0, GFLAGS),
	GATE(ACLK_CIF0, "aclk_cif0", "aclk_vio0", 0, RK2928_CLKGATE_CON(6), 5, GFLAGS),
	GATE(ACLK_IPP, "aclk_ipp", "aclk_vio0", 0, RK2928_CLKGATE_CON(6), 8, GFLAGS),

	/* aclk_lcdc1_pre gates */
	GATE(0, "aclk_vio1", "aclk_lcdc1_pre", 0, RK2928_CLKGATE_CON(9), 5, GFLAGS),
	GATE(ACLK_LCDC1, "aclk_lcdc1", "aclk_vio1", 0, RK2928_CLKGATE_CON(6), 3, GFLAGS),
	GATE(ACLK_RGA, "aclk_rga", "aclk_vio1", 0, RK2928_CLKGATE_CON(6), 11, GFLAGS),

	/* atclk_cpu gates */
	GATE(0, "atclk", "atclk_cpu", 0, RK2928_CLKGATE_CON(9), 3, GFLAGS),
	GATE(0, "trace", "atclk_cpu", 0, RK2928_CLKGATE_CON(9), 2, GFLAGS),

	/* pclk_cpu gates */
	GATE(PCLK_PWM01, "pclk_pwm01", "pclk_cpu", 0, RK2928_CLKGATE_CON(7), 10, GFLAGS),
	GATE(PCLK_TIMER0, "pclk_timer0", "pclk_cpu", 0, RK2928_CLKGATE_CON(7), 7, GFLAGS),
	GATE(PCLK_I2C0, "pclk_i2c0", "pclk_cpu", 0, RK2928_CLKGATE_CON(8), 4, GFLAGS),
	GATE(PCLK_I2C1, "pclk_i2c1", "pclk_cpu", 0, RK2928_CLKGATE_CON(8), 5, GFLAGS),
	GATE(PCLK_GPIO0, "pclk_gpio0", "pclk_cpu", 0, RK2928_CLKGATE_CON(8), 9, GFLAGS),
	GATE(PCLK_GPIO1, "pclk_gpio1", "pclk_cpu", 0, RK2928_CLKGATE_CON(8), 10, GFLAGS),
	GATE(PCLK_GPIO2, "pclk_gpio2", "pclk_cpu", 0, RK2928_CLKGATE_CON(8), 11, GFLAGS),
	GATE(PCLK_EFUSE, "pclk_efuse", "pclk_cpu", 0, RK2928_CLKGATE_CON(5), 2, GFLAGS),
	GATE(PCLK_TZPC, "pclk_tzpc", "pclk_cpu", 0, RK2928_CLKGATE_CON(5), 3, GFLAGS),
	GATE(0, "pclk_ddrupctl", "pclk_cpu", 0, RK2928_CLKGATE_CON(5), 7, GFLAGS),
	GATE(0, "pclk_ddrpubl", "pclk_cpu", 0, RK2928_CLKGATE_CON(9), 6, GFLAGS),
	GATE(0, "pclk_dbg", "pclk_cpu", 0, RK2928_CLKGATE_CON(9), 1, GFLAGS),
	GATE(PCLK_GRF, "pclk_grf", "pclk_cpu", 0, RK2928_CLKGATE_CON(5), 4, GFLAGS),
	GATE(PCLK_PMU, "pclk_pmu", "pclk_cpu", 0, RK2928_CLKGATE_CON(5), 5, GFLAGS),

	/* aclk_peri */
	GATE(ACLK_DMA2, "aclk_dma2", "aclk_peri", 0, RK2928_CLKGATE_CON(5), 1, GFLAGS),
	GATE(ACLK_SMC, "aclk_smc", "aclk_peri", 0, RK2928_CLKGATE_CON(5), 8, GFLAGS),
	GATE(0, "aclk_peri_niu", "aclk_peri", 0, RK2928_CLKGATE_CON(4), 4, GFLAGS),
	GATE(0, "aclk_cpu_peri", "aclk_peri", 0, RK2928_CLKGATE_CON(4), 2, GFLAGS),
	GATE(0, "aclk_peri_axi_matrix", "aclk_peri", 0, RK2928_CLKGATE_CON(4), 3, GFLAGS),

	/* pclk_peri gates */
	GATE(0, "pclk_peri_axi_matrix", "pclk_peri", 0, RK2928_CLKGATE_CON(4), 1, GFLAGS),
	GATE(PCLK_PWM23, "pclk_pwm23", "pclk_peri", 0, RK2928_CLKGATE_CON(7), 11, GFLAGS),
	GATE(PCLK_WDT, "pclk_wdt", "pclk_peri", 0, RK2928_CLKGATE_CON(7), 15, GFLAGS),
	GATE(PCLK_SPI0, "pclk_spi0", "pclk_peri", 0, RK2928_CLKGATE_CON(7), 12, GFLAGS),
	GATE(PCLK_SPI1, "pclk_spi1", "pclk_peri", 0, RK2928_CLKGATE_CON(7), 13, GFLAGS),
	GATE(PCLK_UART2, "pclk_uart2", "pclk_peri", 0, RK2928_CLKGATE_CON(8), 2, GFLAGS),
	GATE(PCLK_UART3, "pclk_uart3", "pclk_peri", 0, RK2928_CLKGATE_CON(8), 3, GFLAGS),
	GATE(PCLK_I2C2, "pclk_i2c2", "pclk_peri", 0, RK2928_CLKGATE_CON(8), 6, GFLAGS),
	GATE(PCLK_I2C3, "pclk_i2c3", "pclk_peri", 0, RK2928_CLKGATE_CON(8), 7, GFLAGS),
	GATE(PCLK_I2C4, "pclk_i2c4", "pclk_peri", 0, RK2928_CLKGATE_CON(8), 8, GFLAGS),
	GATE(PCLK_GPIO3, "pclk_gpio3", "pclk_peri", 0, RK2928_CLKGATE_CON(8), 12, GFLAGS),
	GATE(PCLK_SARADC, "pclk_saradc", "pclk_peri", 0, RK2928_CLKGATE_CON(7), 14, GFLAGS),
};

PNAME(mux_rk3066_lcdc0_p)	= { "dclk_lcdc0_src", "xin27m" };
PNAME(mux_rk3066_lcdc1_p)	= { "dclk_lcdc1_src", "xin27m" };
PNAME(mux_sclk_cif1_p)		= { "cif1_pre", "xin24m" };
PNAME(mux_sclk_i2s1_p)		= { "i2s1_pre", "i2s1_frac", "xin12m" };
PNAME(mux_sclk_i2s2_p)		= { "i2s2_pre", "i2s2_frac", "xin12m" };

static struct clk_div_table div_aclk_cpu_t[] = {
	{ .val = 0, .div = 1 },
	{ .val = 1, .div = 2 },
	{ .val = 2, .div = 3 },
	{ .val = 3, .div = 4 },
	{ .val = 4, .div = 8 },
	{ /* sentinel */ },
};

static struct rockchip_clk_branch rk3066a_clk_branches[] __initdata = {
	DIVTBL(0, "aclk_cpu_pre", "armclk", 0,
			RK2928_CLKSEL_CON(1), 0, 3, DFLAGS | CLK_DIVIDER_READ_ONLY, div_aclk_cpu_t),
	DIV(0, "pclk_cpu_pre", "aclk_cpu_pre", 0,
			RK2928_CLKSEL_CON(1), 12, 2, DFLAGS | CLK_DIVIDER_POWER_OF_TWO
							    | CLK_DIVIDER_READ_ONLY),
	DIV(0, "hclk_cpu_pre", "aclk_cpu_pre", 0,
			RK2928_CLKSEL_CON(1), 8, 2, DFLAGS | CLK_DIVIDER_POWER_OF_TWO
							   | CLK_DIVIDER_READ_ONLY),
	COMPOSITE_NOMUX(0, "hclk_ahb2apb", "hclk_cpu_pre", 0,
			RK2928_CLKSEL_CON(1), 14, 2, DFLAGS | CLK_DIVIDER_POWER_OF_TWO
							    | CLK_DIVIDER_READ_ONLY,
			RK2928_CLKGATE_CON(4), 9, GFLAGS),

	GATE(CORE_L2C, "core_l2c", "aclk_cpu", 0,
			RK2928_CLKGATE_CON(9), 4, GFLAGS),

	COMPOSITE(0, "aclk_peri_pre", mux_pll_src_gpll_cpll_p, 0,
			RK2928_CLKSEL_CON(10), 15, 1, MFLAGS, 0, 5, DFLAGS,
			RK2928_CLKGATE_CON(2), 0, GFLAGS),

	COMPOSITE(0, "dclk_lcdc0_src", mux_pll_src_cpll_gpll_p, 0,
			RK2928_CLKSEL_CON(27), 0, 1, MFLAGS, 8, 8, DFLAGS,
			RK2928_CLKGATE_CON(3), 1, GFLAGS),
	MUX(DCLK_LCDC0, "dclk_lcdc0", mux_rk3066_lcdc0_p, 0,
			RK2928_CLKSEL_CON(27), 4, 1, MFLAGS),
	COMPOSITE(0, "dclk_lcdc1_src", mux_pll_src_cpll_gpll_p, 0,
			RK2928_CLKSEL_CON(28), 0, 1, MFLAGS, 8, 8, DFLAGS,
			RK2928_CLKGATE_CON(3), 2, GFLAGS),
	MUX(DCLK_LCDC1, "dclk_lcdc1", mux_rk3066_lcdc1_p, 0,
			RK2928_CLKSEL_CON(28), 4, 1, MFLAGS),

	COMPOSITE_NOMUX(0, "cif1_pre", "cif_src", 0,
			RK2928_CLKSEL_CON(29), 8, 5, DFLAGS,
			RK2928_CLKGATE_CON(3), 8, GFLAGS),
	MUX(SCLK_CIF1, "sclk_cif1", mux_sclk_cif1_p, 0,
			RK2928_CLKSEL_CON(29), 15, 1, MFLAGS),

	GATE(0, "pclkin_cif1", "ext_cif1", 0,
			RK2928_CLKGATE_CON(3), 4, GFLAGS),

	COMPOSITE(0, "aclk_gpu_src", mux_pll_src_cpll_gpll_p, 0,
			RK2928_CLKSEL_CON(33), 8, 1, MFLAGS, 0, 5, DFLAGS,
			RK2928_CLKGATE_CON(3), 13, GFLAGS),
	GATE(ACLK_GPU, "aclk_gpu", "aclk_gpu_src", 0,
			RK2928_CLKGATE_CON(5), 15, GFLAGS),

	GATE(SCLK_TIMER2, "timer2", "xin24m", 0,
			RK2928_CLKGATE_CON(3), 2, GFLAGS),

	COMPOSITE_NOMUX(0, "sclk_tsadc", "xin24m", 0,
			RK2928_CLKSEL_CON(34), 0, 16, DFLAGS,
			RK2928_CLKGATE_CON(2), 15, GFLAGS),

	MUX(0, "i2s_src", mux_pll_src_gpll_cpll_p, 0,
			RK2928_CLKSEL_CON(2), 15, 1, MFLAGS),
	COMPOSITE_NOMUX(0, "i2s0_pre", "i2s_src", 0,
			RK2928_CLKSEL_CON(2), 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(0), 7, GFLAGS),
	COMPOSITE_FRAC(0, "i2s0_frac", "i2s0_pre", 0,
			RK2928_CLKSEL_CON(6), 0,
			RK2928_CLKGATE_CON(0), 8, GFLAGS),
	MUX(SCLK_I2S0, "sclk_i2s0", mux_sclk_i2s0_p, 0,
			RK2928_CLKSEL_CON(2), 8, 2, MFLAGS),
	COMPOSITE_NOMUX(0, "i2s1_pre", "i2s_src", 0,
			RK2928_CLKSEL_CON(3), 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(0), 9, GFLAGS),
	COMPOSITE_FRAC(0, "i2s1_frac", "i2s1_pre", 0,
			RK2928_CLKSEL_CON(7), 0,
			RK2928_CLKGATE_CON(0), 10, GFLAGS),
	MUX(SCLK_I2S1, "sclk_i2s1", mux_sclk_i2s1_p, 0,
			RK2928_CLKSEL_CON(3), 8, 2, MFLAGS),
	COMPOSITE_NOMUX(0, "i2s2_pre", "i2s_src", 0,
			RK2928_CLKSEL_CON(4), 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(0), 11, GFLAGS),
	COMPOSITE_FRAC(0, "i2s2_frac", "i2s2_pre", 0,
			RK2928_CLKSEL_CON(8), 0,
			RK2928_CLKGATE_CON(0), 12, GFLAGS),
	MUX(SCLK_I2S2, "sclk_i2s2", mux_sclk_i2s2_p, 0,
			RK2928_CLKSEL_CON(4), 8, 2, MFLAGS),
	COMPOSITE_NOMUX(0, "spdif_pre", "i2s_src", 0,
			RK2928_CLKSEL_CON(5), 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(0), 13, GFLAGS),
	COMPOSITE_FRAC(0, "spdif_frac", "spdif_pll", 0,
			RK2928_CLKSEL_CON(9), 0,
			RK2928_CLKGATE_CON(0), 14, GFLAGS),
	MUX(SCLK_SPDIF, "sclk_spdif", mux_sclk_spdif_p, 0,
			RK2928_CLKSEL_CON(5), 8, 2, MFLAGS),

	GATE(HCLK_I2S1, "hclk_i2s1", "hclk_cpu", 0, RK2928_CLKGATE_CON(7), 3, GFLAGS),
	GATE(HCLK_I2S2, "hclk_i2s2", "hclk_cpu", 0, RK2928_CLKGATE_CON(7), 4, GFLAGS),
	GATE(0, "hclk_cif1", "hclk_cpu", 0, RK2928_CLKGATE_CON(6), 6, GFLAGS),
	GATE(0, "hclk_hdmi", "hclk_cpu", 0, RK2928_CLKGATE_CON(4), 14, GFLAGS),

	GATE(HCLK_OTG1, "hclk_usbotg1", "hclk_peri", 0, RK2928_CLKGATE_CON(5), 14, GFLAGS),

	GATE(0, "aclk_cif1", "aclk_vio1", 0, RK2928_CLKGATE_CON(6), 7, GFLAGS),

	GATE(PCLK_TIMER1, "pclk_timer1", "pclk_cpu", 0, RK2928_CLKGATE_CON(7), 8, GFLAGS),
	GATE(PCLK_TIMER2, "pclk_timer2", "pclk_cpu", 0, RK2928_CLKGATE_CON(7), 9, GFLAGS),
	GATE(PCLK_GPIO6, "pclk_gpio6", "pclk_cpu", 0, RK2928_CLKGATE_CON(8), 15, GFLAGS),
	GATE(PCLK_UART0, "pclk_uart0", "pclk_cpu", 0, RK2928_CLKGATE_CON(8), 0, GFLAGS),
	GATE(PCLK_UART1, "pclk_uart1", "pclk_cpu", 0, RK2928_CLKGATE_CON(8), 1, GFLAGS),

	GATE(PCLK_GPIO4, "pclk_gpio4", "pclk_peri", 0, RK2928_CLKGATE_CON(8), 13, GFLAGS),
	GATE(PCLK_TSADC, "pclk_tsadc", "pclk_peri", 0, RK2928_CLKGATE_CON(4), 13, GFLAGS),
};

static struct clk_div_table div_rk3188_aclk_core_t[] = {
	{ .val = 0, .div = 1 },
	{ .val = 1, .div = 2 },
	{ .val = 2, .div = 3 },
	{ .val = 3, .div = 4 },
	{ .val = 4, .div = 8 },
	{ /* sentinel */ },
};

PNAME(mux_hsicphy_p)		= { "sclk_otgphy0", "sclk_otgphy1",
				    "gpll", "cpll" };

static struct rockchip_clk_branch rk3188_clk_branches[] __initdata = {
	COMPOSITE_NOMUX_DIVTBL(0, "aclk_core", "armclk", 0,
			RK2928_CLKSEL_CON(1), 3, 3, DFLAGS | CLK_DIVIDER_READ_ONLY,
			div_rk3188_aclk_core_t, RK2928_CLKGATE_CON(0), 7, GFLAGS),

	/* do not source aclk_cpu_pre from the apll, to keep complexity down */
	COMPOSITE_NOGATE(0, "aclk_cpu_pre", mux_aclk_cpu_p, CLK_SET_RATE_NO_REPARENT,
			RK2928_CLKSEL_CON(0), 5, 1, MFLAGS, 0, 5, DFLAGS),
	DIV(0, "pclk_cpu_pre", "aclk_cpu_pre", 0,
			RK2928_CLKSEL_CON(1), 12, 2, DFLAGS | CLK_DIVIDER_POWER_OF_TWO),
	DIV(0, "hclk_cpu_pre", "aclk_cpu_pre", 0,
			RK2928_CLKSEL_CON(1), 8, 2, DFLAGS | CLK_DIVIDER_POWER_OF_TWO),
	COMPOSITE_NOMUX(0, "hclk_ahb2apb", "hclk_cpu_pre", 0,
			RK2928_CLKSEL_CON(1), 14, 2, DFLAGS | CLK_DIVIDER_POWER_OF_TWO,
			RK2928_CLKGATE_CON(4), 9, GFLAGS),

	GATE(CORE_L2C, "core_l2c", "armclk", 0,
			RK2928_CLKGATE_CON(9), 4, GFLAGS),

	COMPOSITE(0, "aclk_peri_pre", mux_pll_src_cpll_gpll_p, 0,
			RK2928_CLKSEL_CON(10), 15, 1, MFLAGS, 0, 5, DFLAGS,
			RK2928_CLKGATE_CON(2), 0, GFLAGS),

	COMPOSITE(DCLK_LCDC0, "dclk_lcdc0", mux_pll_src_cpll_gpll_p, 0,
			RK2928_CLKSEL_CON(27), 0, 1, MFLAGS, 8, 8, DFLAGS,
			RK2928_CLKGATE_CON(3), 1, GFLAGS),
	COMPOSITE(DCLK_LCDC1, "dclk_lcdc1", mux_pll_src_cpll_gpll_p, 0,
			RK2928_CLKSEL_CON(28), 0, 1, MFLAGS, 8, 8, DFLAGS,
			RK2928_CLKGATE_CON(3), 2, GFLAGS),

	COMPOSITE(0, "aclk_gpu_src", mux_pll_src_cpll_gpll_p, 0,
			RK2928_CLKSEL_CON(34), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RK2928_CLKGATE_CON(3), 15, GFLAGS),
	GATE(ACLK_GPU, "aclk_gpu", "aclk_gpu_src", 0,
			RK2928_CLKGATE_CON(9), 7, GFLAGS),

	GATE(SCLK_TIMER2, "timer2", "xin24m", 0, RK2928_CLKGATE_CON(3), 4, GFLAGS),
	GATE(SCLK_TIMER3, "timer3", "xin24m", 0, RK2928_CLKGATE_CON(1), 2, GFLAGS),
	GATE(SCLK_TIMER4, "timer4", "xin24m", 0, RK2928_CLKGATE_CON(3), 5, GFLAGS),
	GATE(SCLK_TIMER5, "timer5", "xin24m", 0, RK2928_CLKGATE_CON(3), 8, GFLAGS),
	GATE(SCLK_TIMER6, "timer6", "xin24m", 0, RK2928_CLKGATE_CON(3), 14, GFLAGS),

	COMPOSITE_NODIV(0, "sclk_hsicphy_480m", mux_hsicphy_p, 0,
			RK2928_CLKSEL_CON(30), 0, 2, DFLAGS,
			RK2928_CLKGATE_CON(3), 6, GFLAGS),
	DIV(0, "sclk_hsicphy_12m", "sclk_hsicphy_480m", 0,
			RK2928_CLKGATE_CON(11), 8, 6, DFLAGS),

	MUX(0, "i2s_src", mux_pll_src_gpll_cpll_p, 0,
			RK2928_CLKSEL_CON(2), 15, 1, MFLAGS),
	COMPOSITE_NOMUX(0, "i2s0_pre", "i2s_src", 0,
			RK2928_CLKSEL_CON(3), 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(0), 9, GFLAGS),
	COMPOSITE_FRAC(0, "i2s0_frac", "i2s0_pre", 0,
			RK2928_CLKSEL_CON(7), 0,
			RK2928_CLKGATE_CON(0), 10, GFLAGS),
	MUX(SCLK_I2S0, "sclk_i2s0", mux_sclk_i2s0_p, 0,
			RK2928_CLKSEL_CON(3), 8, 2, MFLAGS),
	COMPOSITE_NOMUX(0, "spdif_pre", "i2s_src", 0,
			RK2928_CLKSEL_CON(5), 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(13), 13, GFLAGS),
	COMPOSITE_FRAC(0, "spdif_frac", "spdif_pll", 0,
			RK2928_CLKSEL_CON(9), 0,
			RK2928_CLKGATE_CON(0), 14, GFLAGS),
	MUX(SCLK_SPDIF, "sclk_spdif", mux_sclk_spdif_p, 0,
			RK2928_CLKSEL_CON(5), 8, 2, MFLAGS),

	GATE(0, "hclk_imem0", "hclk_cpu", 0, RK2928_CLKGATE_CON(4), 14, GFLAGS),
	GATE(0, "hclk_imem1", "hclk_cpu", 0, RK2928_CLKGATE_CON(4), 15, GFLAGS),

	GATE(HCLK_OTG1, "hclk_usbotg1", "hclk_peri", 0, RK2928_CLKGATE_CON(7), 3, GFLAGS),
	GATE(HCLK_HSIC, "hclk_hsic", "hclk_peri", 0, RK2928_CLKGATE_CON(7), 4, GFLAGS),

	GATE(PCLK_TIMER3, "pclk_timer3", "pclk_cpu", 0, RK2928_CLKGATE_CON(7), 9, GFLAGS),

	GATE(PCLK_UART0, "pclk_uart0", "hclk_ahb2apb", 0, RK2928_CLKGATE_CON(8), 0, GFLAGS),
	GATE(PCLK_UART1, "pclk_uart1", "hclk_ahb2apb", 0, RK2928_CLKGATE_CON(8), 1, GFLAGS),

	GATE(ACLK_GPS, "aclk_gps", "aclk_peri", 0, RK2928_CLKGATE_CON(8), 13, GFLAGS),
};

static const char *rk3188_critical_clocks[] __initconst = {
	"aclk_cpu",
	"aclk_peri",
	"hclk_peri",
};

static void __init rk3188_common_clk_init(struct device_node *np)
{
	void __iomem *reg_base;
	struct clk *clk;

	reg_base = of_iomap(np, 0);
	if (!reg_base) {
		pr_err("%s: could not map cru region\n", __func__);
		return;
	}

	rockchip_clk_init(np, reg_base, CLK_NR_CLKS);

	/* xin12m is created by an cru-internal divider */
	clk = clk_register_fixed_factor(NULL, "xin12m", "xin24m", 0, 1, 2);
	if (IS_ERR(clk))
		pr_warn("%s: could not register clock xin12m: %ld\n",
			__func__, PTR_ERR(clk));

	clk = clk_register_fixed_factor(NULL, "usb480m", "xin24m", 0, 20, 1);
	if (IS_ERR(clk))
		pr_warn("%s: could not register clock usb480m: %ld\n",
			__func__, PTR_ERR(clk));

	rockchip_clk_register_branches(common_clk_branches,
				  ARRAY_SIZE(common_clk_branches));
	rockchip_clk_protect_critical(rk3188_critical_clocks,
				      ARRAY_SIZE(rk3188_critical_clocks));

	rockchip_register_softrst(np, 9, reg_base + RK2928_SOFTRST_CON(0),
				  ROCKCHIP_SOFTRST_HIWORD_MASK);

	rockchip_register_restart_notifier(RK2928_GLB_SRST_FST);
}

static void __init rk3066a_clk_init(struct device_node *np)
{
	rk3188_common_clk_init(np);
	rockchip_clk_register_plls(rk3188_pll_clks,
				   ARRAY_SIZE(rk3188_pll_clks),
				   RK3066_GRF_SOC_STATUS);
	rockchip_clk_register_branches(rk3066a_clk_branches,
				  ARRAY_SIZE(rk3066a_clk_branches));
	rockchip_clk_register_armclk(ARMCLK, "armclk",
			mux_armclk_p, ARRAY_SIZE(mux_armclk_p),
			&rk3066_cpuclk_data, rk3066_cpuclk_rates,
			ARRAY_SIZE(rk3066_cpuclk_rates));
}
CLK_OF_DECLARE(rk3066a_cru, "rockchip,rk3066a-cru", rk3066a_clk_init);

static void __init rk3188a_clk_init(struct device_node *np)
{
	struct clk *clk1, *clk2;
	unsigned long rate;
	int ret;

	rk3188_common_clk_init(np);
	rockchip_clk_register_plls(rk3188_pll_clks,
				   ARRAY_SIZE(rk3188_pll_clks),
				   RK3188_GRF_SOC_STATUS);
	rockchip_clk_register_branches(rk3188_clk_branches,
				  ARRAY_SIZE(rk3188_clk_branches));
	rockchip_clk_register_armclk(ARMCLK, "armclk",
				  mux_armclk_p, ARRAY_SIZE(mux_armclk_p),
				  &rk3188_cpuclk_data, rk3188_cpuclk_rates,
				  ARRAY_SIZE(rk3188_cpuclk_rates));

	/* reparent aclk_cpu_pre from apll */
	clk1 = __clk_lookup("aclk_cpu_pre");
	clk2 = __clk_lookup("gpll");
	if (clk1 && clk2) {
		rate = clk_get_rate(clk1);

		ret = clk_set_parent(clk1, clk2);
		if (ret < 0)
			pr_warn("%s: could not reparent aclk_cpu_pre to gpll\n",
				__func__);

		clk_set_rate(clk1, rate);
	} else {
		pr_warn("%s: missing clocks to reparent aclk_cpu_pre to gpll\n",
			__func__);
	}
}
CLK_OF_DECLARE(rk3188a_cru, "rockchip,rk3188a-cru", rk3188a_clk_init);

static void __init rk3188_clk_init(struct device_node *np)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rk3188_pll_clks); i++) {
		struct rockchip_pll_clock *pll = &rk3188_pll_clks[i];
		struct rockchip_pll_rate_table *rate;

		if (!pll->rate_table)
			continue;

		rate = pll->rate_table;
		while (rate->rate > 0) {
			rate->bwadj = 0;
			rate++;
		}
	}

	rk3188a_clk_init(np);
}
CLK_OF_DECLARE(rk3188_cru, "rockchip,rk3188-cru", rk3188_clk_init);
