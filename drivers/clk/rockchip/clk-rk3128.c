/*
 * Copyright (c) 2017 Rockchip Electronics Co. Ltd.
 * Author: Elaine <zhangqing@rock-chips.com>
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
#include <dt-bindings/clock/rk3128-cru.h>
#include "clk.h"

#define RK3128_GRF_SOC_STATUS0	0x14c

enum rk3128_plls {
	apll, dpll, cpll, gpll,
};

static struct rockchip_pll_rate_table rk3128_pll_rates[] = {
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

#define RK3128_DIV_CPU_MASK		0x1f
#define RK3128_DIV_CPU_SHIFT		8

#define RK3128_DIV_PERI_MASK		0xf
#define RK3128_DIV_PERI_SHIFT		0
#define RK3128_DIV_ACLK_MASK		0x7
#define RK3128_DIV_ACLK_SHIFT		4
#define RK3128_DIV_HCLK_MASK		0x3
#define RK3128_DIV_HCLK_SHIFT		8
#define RK3128_DIV_PCLK_MASK		0x7
#define RK3128_DIV_PCLK_SHIFT		12

#define RK3128_CLKSEL1(_core_aclk_div, _pclk_dbg_div)			\
{									\
	.reg = RK2928_CLKSEL_CON(1),					\
	.val = HIWORD_UPDATE(_pclk_dbg_div, RK3128_DIV_PERI_MASK,	\
			     RK3128_DIV_PERI_SHIFT) |			\
	       HIWORD_UPDATE(_core_aclk_div, RK3128_DIV_ACLK_MASK,	\
			     RK3128_DIV_ACLK_SHIFT),			\
}

#define RK3128_CPUCLK_RATE(_prate, _core_aclk_div, _pclk_dbg_div)	\
{									\
	.prate = _prate,						\
	.divs = {							\
		RK3128_CLKSEL1(_core_aclk_div, _pclk_dbg_div),		\
	},								\
}

static struct rockchip_cpuclk_rate_table rk3128_cpuclk_rates[] __initdata = {
	RK3128_CPUCLK_RATE(1800000000, 1, 7),
	RK3128_CPUCLK_RATE(1704000000, 1, 7),
	RK3128_CPUCLK_RATE(1608000000, 1, 7),
	RK3128_CPUCLK_RATE(1512000000, 1, 7),
	RK3128_CPUCLK_RATE(1488000000, 1, 5),
	RK3128_CPUCLK_RATE(1416000000, 1, 5),
	RK3128_CPUCLK_RATE(1392000000, 1, 5),
	RK3128_CPUCLK_RATE(1296000000, 1, 5),
	RK3128_CPUCLK_RATE(1200000000, 1, 5),
	RK3128_CPUCLK_RATE(1104000000, 1, 5),
	RK3128_CPUCLK_RATE(1008000000, 1, 5),
	RK3128_CPUCLK_RATE(912000000, 1, 5),
	RK3128_CPUCLK_RATE(816000000, 1, 3),
	RK3128_CPUCLK_RATE(696000000, 1, 3),
	RK3128_CPUCLK_RATE(600000000, 1, 3),
	RK3128_CPUCLK_RATE(408000000, 1, 1),
	RK3128_CPUCLK_RATE(312000000, 1, 1),
	RK3128_CPUCLK_RATE(216000000,  1, 1),
	RK3128_CPUCLK_RATE(96000000, 1, 1),
};

static const struct rockchip_cpuclk_reg_data rk3128_cpuclk_data = {
	.core_reg = RK2928_CLKSEL_CON(0),
	.div_core_shift = 0,
	.div_core_mask = 0x1f,
	.mux_core_alt = 1,
	.mux_core_main = 0,
	.mux_core_shift = 7,
	.mux_core_mask = 0x1,
};

PNAME(mux_pll_p)		= { "clk_24m", "xin24m" };

PNAME(mux_ddrphy_p)		= { "dpll_ddr", "gpll_div2_ddr" };
PNAME(mux_armclk_p)		= { "apll_core", "gpll_div2_core" };
PNAME(mux_usb480m_p)		= { "usb480m_phy", "xin24m" };
PNAME(mux_aclk_cpu_src_p)	= { "cpll", "gpll", "gpll_div2", "gpll_div3" };

PNAME(mux_pll_src_5plls_p)	= { "cpll", "gpll", "gpll_div2", "gpll_div3", "usb480m" };
PNAME(mux_pll_src_4plls_p)	= { "cpll", "gpll", "gpll_div2", "usb480m" };
PNAME(mux_pll_src_3plls_p)	= { "cpll", "gpll", "gpll_div2" };

PNAME(mux_aclk_peri_src_p)	= { "gpll_peri", "cpll_peri", "gpll_div2_peri", "gpll_div3_peri" };
PNAME(mux_mmc_src_p)		= { "cpll", "gpll", "gpll_div2", "xin24m" };
PNAME(mux_clk_cif_out_src_p)		= { "clk_cif_src", "xin24m" };
PNAME(mux_sclk_vop_src_p)	= { "cpll", "gpll", "gpll_div2", "gpll_div3" };

PNAME(mux_i2s0_p)		= { "i2s0_src", "i2s0_frac", "ext_i2s", "xin12m" };
PNAME(mux_i2s1_pre_p)		= { "i2s1_src", "i2s1_frac", "ext_i2s", "xin12m" };
PNAME(mux_i2s_out_p)		= { "i2s1_pre", "xin12m" };
PNAME(mux_sclk_spdif_p)		= { "sclk_spdif_src", "spdif_frac", "xin12m" };

PNAME(mux_uart0_p)		= { "uart0_src", "uart0_frac", "xin24m" };
PNAME(mux_uart1_p)		= { "uart1_src", "uart1_frac", "xin24m" };
PNAME(mux_uart2_p)		= { "uart2_src", "uart2_frac", "xin24m" };

PNAME(mux_sclk_gmac_p)	= { "sclk_gmac_src", "gmac_clkin" };
PNAME(mux_sclk_sfc_src_p)	= { "cpll", "gpll", "gpll_div2", "xin24m" };

static struct rockchip_pll_clock rk3128_pll_clks[] __initdata = {
	[apll] = PLL(pll_rk3036, PLL_APLL, "apll", mux_pll_p, 0, RK2928_PLL_CON(0),
		     RK2928_MODE_CON, 0, 1, 0, rk3128_pll_rates),
	[dpll] = PLL(pll_rk3036, PLL_DPLL, "dpll", mux_pll_p, 0, RK2928_PLL_CON(4),
		     RK2928_MODE_CON, 4, 0, 0, NULL),
	[cpll] = PLL(pll_rk3036, PLL_CPLL, "cpll", mux_pll_p, 0, RK2928_PLL_CON(8),
		     RK2928_MODE_CON, 8, 2, 0, rk3128_pll_rates),
	[gpll] = PLL(pll_rk3036, PLL_GPLL, "gpll", mux_pll_p, 0, RK2928_PLL_CON(12),
		     RK2928_MODE_CON, 12, 3, ROCKCHIP_PLL_SYNC_RATE, rk3128_pll_rates),
};

#define MFLAGS CLK_MUX_HIWORD_MASK
#define DFLAGS CLK_DIVIDER_HIWORD_MASK
#define GFLAGS (CLK_GATE_HIWORD_MASK | CLK_GATE_SET_TO_DISABLE)

static struct rockchip_clk_branch rk3128_i2s0_fracmux __initdata =
	MUX(0, "i2s0_pre", mux_i2s0_p, CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(9), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3128_i2s1_fracmux __initdata =
	MUX(0, "i2s1_pre", mux_i2s1_pre_p, CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(3), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3128_spdif_fracmux __initdata =
	MUX(SCLK_SPDIF, "sclk_spdif", mux_sclk_spdif_p, CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(6), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3128_uart0_fracmux __initdata =
	MUX(SCLK_UART0, "sclk_uart0", mux_uart0_p, CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(13), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3128_uart1_fracmux __initdata =
	MUX(SCLK_UART1, "sclk_uart1", mux_uart1_p, CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(14), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3128_uart2_fracmux __initdata =
	MUX(SCLK_UART2, "sclk_uart2", mux_uart2_p, CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(15), 8, 2, MFLAGS);

static struct rockchip_clk_branch common_clk_branches[] __initdata = {
	/*
	 * Clock-Architecture Diagram 1
	 */

	FACTOR(PLL_GPLL_DIV2, "gpll_div2", "gpll", 0, 1, 2),
	FACTOR(PLL_GPLL_DIV3, "gpll_div3", "gpll", 0, 1, 3),

	DIV(0, "clk_24m", "xin24m", CLK_IGNORE_UNUSED,
			RK2928_CLKSEL_CON(4), 8, 5, DFLAGS),

	/* PD_DDR */
	GATE(0, "dpll_ddr", "dpll", CLK_IGNORE_UNUSED,
			RK2928_CLKGATE_CON(0), 2, GFLAGS),
	GATE(0, "gpll_div2_ddr", "gpll_div2", CLK_IGNORE_UNUSED,
			RK2928_CLKGATE_CON(0), 2, GFLAGS),
	COMPOSITE_NOGATE(0, "ddrphy2x", mux_ddrphy_p, CLK_IGNORE_UNUSED,
			RK2928_CLKSEL_CON(26), 8, 2, MFLAGS, 0, 2, DFLAGS | CLK_DIVIDER_POWER_OF_TWO),
	FACTOR(SCLK_DDRC, "clk_ddrc", "ddrphy2x", 0, 1, 2),
	FACTOR(0, "clk_ddrphy", "ddrphy2x", 0, 1, 2),

	/* PD_CORE */
	GATE(0, "apll_core", "apll", CLK_IGNORE_UNUSED,
			RK2928_CLKGATE_CON(0), 6, GFLAGS),
	GATE(0, "gpll_div2_core", "gpll_div2", CLK_IGNORE_UNUSED,
			RK2928_CLKGATE_CON(0), 6, GFLAGS),
	COMPOSITE_NOMUX(0, "pclk_dbg", "armclk", CLK_IGNORE_UNUSED,
			RK2928_CLKSEL_CON(1), 0, 4, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RK2928_CLKGATE_CON(0), 0, GFLAGS),
	COMPOSITE_NOMUX(0, "armcore", "armclk", CLK_IGNORE_UNUSED,
			RK2928_CLKSEL_CON(1), 4, 3, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RK2928_CLKGATE_CON(0), 7, GFLAGS),

	/* PD_MISC */
	MUX(SCLK_USB480M, "usb480m", mux_usb480m_p, CLK_SET_RATE_PARENT,
			RK2928_MISC_CON, 15, 1, MFLAGS),

	/* PD_CPU */
	COMPOSITE(0, "aclk_cpu_src", mux_aclk_cpu_src_p, 0,
			RK2928_CLKSEL_CON(0), 13, 2, MFLAGS, 8, 5, DFLAGS,
			RK2928_CLKGATE_CON(0), 1, GFLAGS),
	GATE(ACLK_CPU, "aclk_cpu", "aclk_cpu_src", 0,
			RK2928_CLKGATE_CON(0), 3, GFLAGS),
	COMPOSITE_NOMUX(HCLK_CPU, "hclk_cpu", "aclk_cpu_src", 0,
			RK2928_CLKSEL_CON(1), 8, 2, DFLAGS,
			RK2928_CLKGATE_CON(0), 4, GFLAGS),
	COMPOSITE_NOMUX(PCLK_CPU, "pclk_cpu", "aclk_cpu_src", 0,
			RK2928_CLKSEL_CON(1), 12, 2, DFLAGS,
			RK2928_CLKGATE_CON(0), 5, GFLAGS),
	COMPOSITE_NOMUX(SCLK_CRYPTO, "clk_crypto", "aclk_cpu_src", 0,
			RK2928_CLKSEL_CON(24), 0, 2, DFLAGS,
			RK2928_CLKGATE_CON(0), 12, GFLAGS),

	/* PD_VIDEO */
	COMPOSITE(ACLK_VEPU, "aclk_vepu", mux_pll_src_5plls_p, 0,
			RK2928_CLKSEL_CON(32), 5, 3, MFLAGS, 0, 5, DFLAGS,
			RK2928_CLKGATE_CON(3), 9, GFLAGS),
	FACTOR(HCLK_VEPU, "hclk_vepu", "aclk_vepu", 0, 1, 4),

	COMPOSITE(ACLK_VDPU, "aclk_vdpu", mux_pll_src_5plls_p, 0,
			RK2928_CLKSEL_CON(32), 13, 3, MFLAGS, 8, 5, DFLAGS,
			RK2928_CLKGATE_CON(3), 11, GFLAGS),
	FACTOR_GATE(HCLK_VDPU, "hclk_vdpu", "aclk_vdpu", 0, 1, 4,
			RK2928_CLKGATE_CON(3), 12, GFLAGS),

	COMPOSITE(SCLK_HEVC_CORE, "sclk_hevc_core", mux_pll_src_5plls_p, 0,
			RK2928_CLKSEL_CON(34), 13, 3, MFLAGS, 8, 5, DFLAGS,
			RK2928_CLKGATE_CON(3), 10, GFLAGS),

	/* PD_VIO */
	COMPOSITE(ACLK_VIO0, "aclk_vio0", mux_pll_src_5plls_p, 0,
			RK2928_CLKSEL_CON(31), 5, 3, MFLAGS, 0, 5, DFLAGS,
			RK2928_CLKGATE_CON(3), 0, GFLAGS),
	COMPOSITE(ACLK_VIO1, "aclk_vio1", mux_pll_src_5plls_p, 0,
			RK2928_CLKSEL_CON(31), 13, 3, MFLAGS, 8, 5, DFLAGS,
			RK2928_CLKGATE_CON(1), 4, GFLAGS),
	COMPOSITE(HCLK_VIO, "hclk_vio", mux_pll_src_4plls_p, 0,
			RK2928_CLKSEL_CON(30), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RK2928_CLKGATE_CON(0), 11, GFLAGS),

	/* PD_PERI */
	GATE(0, "gpll_peri", "gpll", CLK_IGNORE_UNUSED,
			RK2928_CLKGATE_CON(2), 0, GFLAGS),
	GATE(0, "cpll_peri", "cpll", CLK_IGNORE_UNUSED,
			RK2928_CLKGATE_CON(2), 0, GFLAGS),
	GATE(0, "gpll_div2_peri", "gpll_div2", CLK_IGNORE_UNUSED,
			RK2928_CLKGATE_CON(2), 0, GFLAGS),
	GATE(0, "gpll_div3_peri", "gpll_div3", CLK_IGNORE_UNUSED,
			RK2928_CLKGATE_CON(2), 0, GFLAGS),
	COMPOSITE_NOGATE(0, "aclk_peri_src", mux_aclk_peri_src_p, 0,
			RK2928_CLKSEL_CON(10), 14, 2, MFLAGS, 0, 5, DFLAGS),
	COMPOSITE_NOMUX(PCLK_PERI, "pclk_peri", "aclk_peri_src", 0,
			RK2928_CLKSEL_CON(10), 12, 2, DFLAGS | CLK_DIVIDER_POWER_OF_TWO,
			RK2928_CLKGATE_CON(2), 3, GFLAGS),
	COMPOSITE_NOMUX(HCLK_PERI, "hclk_peri", "aclk_peri_src", 0,
			RK2928_CLKSEL_CON(10), 8, 2, DFLAGS | CLK_DIVIDER_POWER_OF_TWO,
			RK2928_CLKGATE_CON(2), 2, GFLAGS),
	GATE(ACLK_PERI, "aclk_peri", "aclk_peri_src", 0,
			RK2928_CLKGATE_CON(2), 1, GFLAGS),

	GATE(SCLK_TIMER0, "sclk_timer0", "xin24m", 0,
			RK2928_CLKGATE_CON(10), 3, GFLAGS),
	GATE(SCLK_TIMER1, "sclk_timer1", "xin24m", 0,
			RK2928_CLKGATE_CON(10), 4, GFLAGS),
	GATE(SCLK_TIMER2, "sclk_timer2", "xin24m", 0,
			RK2928_CLKGATE_CON(10), 5, GFLAGS),
	GATE(SCLK_TIMER3, "sclk_timer3", "xin24m", 0,
			RK2928_CLKGATE_CON(10), 6, GFLAGS),
	GATE(SCLK_TIMER4, "sclk_timer4", "xin24m", 0,
			RK2928_CLKGATE_CON(10), 7, GFLAGS),
	GATE(SCLK_TIMER5, "sclk_timer5", "xin24m", 0,
			RK2928_CLKGATE_CON(10), 8, GFLAGS),

	GATE(SCLK_PVTM_CORE, "clk_pvtm_core", "xin24m", 0,
			RK2928_CLKGATE_CON(10), 8, GFLAGS),
	GATE(SCLK_PVTM_GPU, "clk_pvtm_gpu", "xin24m", 0,
			RK2928_CLKGATE_CON(10), 8, GFLAGS),
	GATE(SCLK_PVTM_FUNC, "clk_pvtm_func", "xin24m", 0,
			RK2928_CLKGATE_CON(10), 8, GFLAGS),
	GATE(SCLK_MIPI_24M, "clk_mipi_24m", "xin24m", CLK_IGNORE_UNUSED,
			RK2928_CLKGATE_CON(10), 8, GFLAGS),

	COMPOSITE(SCLK_SDMMC, "sclk_sdmmc0", mux_mmc_src_p, 0,
			RK2928_CLKSEL_CON(11), 6, 2, MFLAGS, 0, 6, DFLAGS,
			RK2928_CLKGATE_CON(2), 11, GFLAGS),

	COMPOSITE(SCLK_SDIO, "sclk_sdio", mux_mmc_src_p, 0,
			RK2928_CLKSEL_CON(12), 6, 2, MFLAGS, 0, 6, DFLAGS,
			RK2928_CLKGATE_CON(2), 13, GFLAGS),

	COMPOSITE(SCLK_EMMC, "sclk_emmc", mux_mmc_src_p, 0,
			RK2928_CLKSEL_CON(12), 14, 2, MFLAGS, 8, 6, DFLAGS,
			RK2928_CLKGATE_CON(2), 14, GFLAGS),

	DIV(SCLK_PVTM, "clk_pvtm", "clk_pvtm_func", 0,
			RK2928_CLKSEL_CON(2), 0, 7, DFLAGS),

	/*
	 * Clock-Architecture Diagram 2
	 */
	COMPOSITE(DCLK_VOP, "dclk_vop", mux_sclk_vop_src_p, 0,
			RK2928_CLKSEL_CON(27), 0, 2, MFLAGS, 8, 8, DFLAGS,
			RK2928_CLKGATE_CON(3), 1, GFLAGS),
	COMPOSITE(SCLK_VOP, "sclk_vop", mux_sclk_vop_src_p, 0,
			RK2928_CLKSEL_CON(28), 0, 2, MFLAGS, 8, 8, DFLAGS,
			RK2928_CLKGATE_CON(3), 2, GFLAGS),
	COMPOSITE(DCLK_EBC, "dclk_ebc", mux_pll_src_3plls_p, 0,
			RK2928_CLKSEL_CON(23), 0, 2, MFLAGS, 8, 8, DFLAGS,
			RK2928_CLKGATE_CON(3), 4, GFLAGS),

	FACTOR(0, "xin12m", "xin24m", 0, 1, 2),

	COMPOSITE_NODIV(SCLK_CIF_SRC, "sclk_cif_src", mux_pll_src_4plls_p, 0,
			RK2928_CLKSEL_CON(29), 0, 2, MFLAGS,
			RK2928_CLKGATE_CON(3), 7, GFLAGS),
	MUX(SCLK_CIF_OUT_SRC, "sclk_cif_out_src", mux_clk_cif_out_src_p, 0,
			RK2928_CLKSEL_CON(13), 14, 2, MFLAGS),
	DIV(SCLK_CIF_OUT, "sclk_cif_out", "sclk_cif_out_src", 0,
			RK2928_CLKSEL_CON(29), 2, 5, DFLAGS),

	COMPOSITE(0, "i2s0_src", mux_pll_src_3plls_p, 0,
			RK2928_CLKSEL_CON(9), 14, 2, MFLAGS, 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(4), 4, GFLAGS),
	COMPOSITE_FRACMUX(0, "i2s0_frac", "i2s0_src", CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(8), 0,
			RK2928_CLKGATE_CON(4), 5, GFLAGS,
			&rk3128_i2s0_fracmux),
	GATE(SCLK_I2S0, "sclk_i2s0", "i2s0_pre", CLK_SET_RATE_PARENT,
			RK2928_CLKGATE_CON(4), 6, GFLAGS),

	COMPOSITE(0, "i2s1_src", mux_pll_src_3plls_p, 0,
			RK2928_CLKSEL_CON(3), 14, 2, MFLAGS, 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(0), 9, GFLAGS),
	COMPOSITE_FRACMUX(0, "i2s1_frac", "i2s1_src", CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(7), 0,
			RK2928_CLKGATE_CON(0), 10, GFLAGS,
			&rk3128_i2s1_fracmux),
	GATE(SCLK_I2S1, "sclk_i2s1", "i2s1_pre", CLK_SET_RATE_PARENT,
			RK2928_CLKGATE_CON(0), 14, GFLAGS),
	COMPOSITE_NODIV(SCLK_I2S_OUT, "i2s_out", mux_i2s_out_p, 0,
			RK2928_CLKSEL_CON(3), 12, 1, MFLAGS,
			RK2928_CLKGATE_CON(0), 13, GFLAGS),

	COMPOSITE(0, "sclk_spdif_src", mux_pll_src_3plls_p, 0,
			RK2928_CLKSEL_CON(6), 14, 2, MFLAGS, 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(2), 10, GFLAGS),
	COMPOSITE_FRACMUX(0, "spdif_frac", "sclk_spdif_src", CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(20), 0,
			RK2928_CLKGATE_CON(2), 12, GFLAGS,
			&rk3128_spdif_fracmux),

	GATE(0, "jtag", "ext_jtag", CLK_IGNORE_UNUSED,
			RK2928_CLKGATE_CON(1), 3, GFLAGS),

	GATE(SCLK_OTGPHY0, "sclk_otgphy0", "xin12m", 0,
			RK2928_CLKGATE_CON(1), 5, GFLAGS),
	GATE(SCLK_OTGPHY1, "sclk_otgphy1", "xin12m", 0,
			RK2928_CLKGATE_CON(1), 6, GFLAGS),

	COMPOSITE_NOMUX(SCLK_SARADC, "sclk_saradc", "xin24m", 0,
			RK2928_CLKSEL_CON(24), 8, 8, DFLAGS,
			RK2928_CLKGATE_CON(2), 8, GFLAGS),

	COMPOSITE(ACLK_GPU, "aclk_gpu", mux_pll_src_5plls_p, 0,
			RK2928_CLKSEL_CON(34), 5, 3, MFLAGS, 0, 5, DFLAGS,
			RK2928_CLKGATE_CON(3), 13, GFLAGS),

	COMPOSITE(SCLK_SPI0, "sclk_spi0", mux_pll_src_3plls_p, 0,
			RK2928_CLKSEL_CON(25), 8, 2, MFLAGS, 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(2), 9, GFLAGS),

	/* PD_UART */
	COMPOSITE(0, "uart0_src", mux_pll_src_4plls_p, 0,
			RK2928_CLKSEL_CON(13), 12, 2, MFLAGS, 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(1), 8, GFLAGS),
	MUX(0, "uart12_src", mux_pll_src_4plls_p, 0,
			RK2928_CLKSEL_CON(13), 14, 2, MFLAGS),
	COMPOSITE_NOMUX(0, "uart1_src", "uart12_src", 0,
			RK2928_CLKSEL_CON(14), 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(1), 10, GFLAGS),
	COMPOSITE_NOMUX(0, "uart2_src", "uart12_src", 0,
			RK2928_CLKSEL_CON(15), 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(1), 13, GFLAGS),
	COMPOSITE_FRACMUX(0, "uart0_frac", "uart0_src", CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(17), 0,
			RK2928_CLKGATE_CON(1), 9, GFLAGS,
			&rk3128_uart0_fracmux),
	COMPOSITE_FRACMUX(0, "uart1_frac", "uart1_src", CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(18), 0,
			RK2928_CLKGATE_CON(1), 11, GFLAGS,
			&rk3128_uart1_fracmux),
	COMPOSITE_FRACMUX(0, "uart2_frac", "uart2_src", CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(19), 0,
			RK2928_CLKGATE_CON(1), 13, GFLAGS,
			&rk3128_uart2_fracmux),

	COMPOSITE(SCLK_MAC_SRC, "sclk_gmac_src", mux_pll_src_3plls_p, 0,
			RK2928_CLKSEL_CON(5), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK2928_CLKGATE_CON(1), 7, GFLAGS),
	MUX(SCLK_MAC, "sclk_gmac", mux_sclk_gmac_p, 0,
			RK2928_CLKSEL_CON(5), 15, 1, MFLAGS),
	GATE(SCLK_MAC_REFOUT, "sclk_mac_refout", "sclk_gmac", 0,
			RK2928_CLKGATE_CON(2), 5, GFLAGS),
	GATE(SCLK_MAC_REF, "sclk_mac_ref", "sclk_gmac", 0,
			RK2928_CLKGATE_CON(2), 4, GFLAGS),
	GATE(SCLK_MAC_RX, "sclk_mac_rx", "sclk_gmac", 0,
			RK2928_CLKGATE_CON(2), 6, GFLAGS),
	GATE(SCLK_MAC_TX, "sclk_mac_tx", "sclk_gmac", 0,
			RK2928_CLKGATE_CON(2), 7, GFLAGS),

	COMPOSITE(SCLK_TSP, "sclk_tsp", mux_pll_src_3plls_p, 0,
			RK2928_CLKSEL_CON(4), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK2928_CLKGATE_CON(1), 14, GFLAGS),

	COMPOSITE(SCLK_NANDC, "sclk_nandc", mux_pll_src_3plls_p, 0,
			RK2928_CLKSEL_CON(2), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RK2928_CLKGATE_CON(10), 15, GFLAGS),

	COMPOSITE_NOMUX(PCLK_PMU_PRE, "pclk_pmu_pre", "cpll", 0,
			RK2928_CLKSEL_CON(29), 8, 6, DFLAGS,
			RK2928_CLKGATE_CON(1), 0, GFLAGS),

	/*
	 * Clock-Architecture Diagram 3
	 */

	/* PD_VOP */
	GATE(ACLK_LCDC0, "aclk_lcdc0", "aclk_vio0", 0, RK2928_CLKGATE_CON(6), 0, GFLAGS),
	GATE(ACLK_CIF, "aclk_cif", "aclk_vio0", 0, RK2928_CLKGATE_CON(6), 5, GFLAGS),
	GATE(ACLK_RGA, "aclk_rga", "aclk_vio0", 0, RK2928_CLKGATE_CON(6), 11, GFLAGS),
	GATE(0, "aclk_vio0_niu", "aclk_vio0", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(6), 13, GFLAGS),

	GATE(ACLK_IEP, "aclk_iep", "aclk_vio1", 0, RK2928_CLKGATE_CON(9), 8, GFLAGS),
	GATE(0, "aclk_vio1_niu", "aclk_vio1", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(9), 10, GFLAGS),

	GATE(HCLK_VIO_H2P, "hclk_vio_h2p", "hclk_vio", 0, RK2928_CLKGATE_CON(9), 5, GFLAGS),
	GATE(PCLK_MIPI, "pclk_mipi", "hclk_vio", 0, RK2928_CLKGATE_CON(9), 6, GFLAGS),
	GATE(HCLK_RGA, "hclk_rga", "hclk_vio", 0, RK2928_CLKGATE_CON(6), 10, GFLAGS),
	GATE(HCLK_LCDC0, "hclk_lcdc0", "hclk_vio", 0, RK2928_CLKGATE_CON(6), 1, GFLAGS),
	GATE(HCLK_IEP, "hclk_iep", "hclk_vio", 0, RK2928_CLKGATE_CON(9), 7, GFLAGS),
	GATE(0, "hclk_vio_niu", "hclk_vio", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(6), 12, GFLAGS),
	GATE(HCLK_CIF, "hclk_cif", "hclk_vio", 0, RK2928_CLKGATE_CON(6), 4, GFLAGS),
	GATE(HCLK_EBC, "hclk_ebc", "hclk_vio", 0, RK2928_CLKGATE_CON(9), 9, GFLAGS),

	/* PD_PERI */
	GATE(0, "aclk_peri_axi", "aclk_peri", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(4), 3, GFLAGS),
	GATE(ACLK_GMAC, "aclk_gmac", "aclk_peri", 0, RK2928_CLKGATE_CON(10), 10, GFLAGS),
	GATE(ACLK_DMAC, "aclk_dmac", "aclk_peri", 0, RK2928_CLKGATE_CON(5), 1, GFLAGS),
	GATE(0, "aclk_peri_niu", "aclk_peri", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(9), 15, GFLAGS),
	GATE(0, "aclk_cpu_to_peri", "aclk_peri", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(4), 2, GFLAGS),

	GATE(HCLK_I2S_8CH, "hclk_i2s_8ch", "hclk_peri", 0, RK2928_CLKGATE_CON(7), 4, GFLAGS),
	GATE(0, "hclk_peri_matrix", "hclk_peri", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(4), 0, GFLAGS),
	GATE(HCLK_I2S_2CH, "hclk_i2s_2ch", "hclk_peri", 0, RK2928_CLKGATE_CON(7), 2, GFLAGS),
	GATE(0, "hclk_usb_peri", "hclk_peri", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(9), 13, GFLAGS),
	GATE(HCLK_HOST2, "hclk_host2", "hclk_peri", 0, RK2928_CLKGATE_CON(7), 3, GFLAGS),
	GATE(HCLK_OTG, "hclk_otg", "hclk_peri", 0, RK2928_CLKGATE_CON(3), 13, GFLAGS),
	GATE(0, "hclk_peri_ahb", "hclk_peri", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(9), 14, GFLAGS),
	GATE(HCLK_SPDIF, "hclk_spdif", "hclk_peri", 0, RK2928_CLKGATE_CON(10), 9, GFLAGS),
	GATE(HCLK_TSP, "hclk_tsp", "hclk_peri", 0, RK2928_CLKGATE_CON(10), 12, GFLAGS),
	GATE(HCLK_SDMMC, "hclk_sdmmc", "hclk_peri", 0, RK2928_CLKGATE_CON(5), 10, GFLAGS),
	GATE(HCLK_SDIO, "hclk_sdio", "hclk_peri", 0, RK2928_CLKGATE_CON(5), 11, GFLAGS),
	GATE(HCLK_EMMC, "hclk_emmc", "hclk_peri", 0, RK2928_CLKGATE_CON(7), 0, GFLAGS),
	GATE(0, "hclk_emmc_peri", "hclk_peri", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(3), 6, GFLAGS),
	GATE(HCLK_NANDC, "hclk_nandc", "hclk_peri", 0, RK2928_CLKGATE_CON(5), 9, GFLAGS),
	GATE(HCLK_USBHOST, "hclk_usbhost", "hclk_peri", 0, RK2928_CLKGATE_CON(10), 14, GFLAGS),

	GATE(PCLK_SIM_CARD, "pclk_sim_card", "pclk_peri", 0, RK2928_CLKGATE_CON(9), 12, GFLAGS),
	GATE(PCLK_GMAC, "pclk_gmac", "pclk_peri", 0, RK2928_CLKGATE_CON(10), 11, GFLAGS),
	GATE(0, "pclk_peri_axi", "pclk_peri", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(4), 1, GFLAGS),
	GATE(PCLK_SPI0, "pclk_spi0", "pclk_peri", 0, RK2928_CLKGATE_CON(7), 12, GFLAGS),
	GATE(PCLK_UART0, "pclk_uart0", "pclk_peri", 0, RK2928_CLKGATE_CON(8), 0, GFLAGS),
	GATE(PCLK_UART1, "pclk_uart1", "pclk_peri", 0, RK2928_CLKGATE_CON(8), 1, GFLAGS),
	GATE(PCLK_UART2, "pclk_uart2", "pclk_peri", 0, RK2928_CLKGATE_CON(8), 2, GFLAGS),
	GATE(PCLK_PWM, "pclk_pwm", "pclk_peri", 0, RK2928_CLKGATE_CON(7), 10, GFLAGS),
	GATE(PCLK_WDT, "pclk_wdt", "pclk_peri", 0, RK2928_CLKGATE_CON(7), 15, GFLAGS),
	GATE(PCLK_I2C0, "pclk_i2c0", "pclk_peri", 0, RK2928_CLKGATE_CON(8), 4, GFLAGS),
	GATE(PCLK_I2C1, "pclk_i2c1", "pclk_peri", 0, RK2928_CLKGATE_CON(8), 5, GFLAGS),
	GATE(PCLK_I2C2, "pclk_i2c2", "pclk_peri", 0, RK2928_CLKGATE_CON(8), 6, GFLAGS),
	GATE(PCLK_I2C3, "pclk_i2c3", "pclk_peri", 0, RK2928_CLKGATE_CON(8), 7, GFLAGS),
	GATE(PCLK_SARADC, "pclk_saradc", "pclk_peri", 0, RK2928_CLKGATE_CON(7), 14, GFLAGS),
	GATE(PCLK_EFUSE, "pclk_efuse", "pclk_peri", 0, RK2928_CLKGATE_CON(5), 2, GFLAGS),
	GATE(PCLK_TIMER, "pclk_timer", "pclk_peri", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(7), 7, GFLAGS),
	GATE(PCLK_GPIO0, "pclk_gpio0", "pclk_peri", 0, RK2928_CLKGATE_CON(8), 9, GFLAGS),
	GATE(PCLK_GPIO1, "pclk_gpio1", "pclk_peri", 0, RK2928_CLKGATE_CON(8), 10, GFLAGS),
	GATE(PCLK_GPIO2, "pclk_gpio2", "pclk_peri", 0, RK2928_CLKGATE_CON(8), 11, GFLAGS),
	GATE(PCLK_GPIO3, "pclk_gpio3", "pclk_peri", 0, RK2928_CLKGATE_CON(8), 12, GFLAGS),

	/* PD_BUS */
	GATE(0, "aclk_initmem", "aclk_cpu", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(4), 12, GFLAGS),
	GATE(0, "aclk_strc_sys", "aclk_cpu", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(4), 10, GFLAGS),

	GATE(0, "hclk_rom", "hclk_cpu", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(5), 6, GFLAGS),
	GATE(HCLK_CRYPTO, "hclk_crypto", "hclk_cpu", 0, RK2928_CLKGATE_CON(3), 5, GFLAGS),

	GATE(PCLK_ACODEC, "pclk_acodec", "pclk_cpu", 0, RK2928_CLKGATE_CON(5), 14, GFLAGS),
	GATE(0, "pclk_ddrupctl", "pclk_cpu", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(5), 7, GFLAGS),
	GATE(0, "pclk_grf", "pclk_cpu", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(5), 4, GFLAGS),
	GATE(0, "pclk_mipiphy", "pclk_cpu", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(5), 0, GFLAGS),

	GATE(0, "pclk_pmu", "pclk_pmu_pre", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(9), 2, GFLAGS),
	GATE(0, "pclk_pmu_niu", "pclk_pmu_pre", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(9), 3, GFLAGS),

	/* PD_MMC */
	MMC(SCLK_SDMMC_DRV,    "sdmmc_drv",    "sclk_sdmmc", RK3228_SDMMC_CON0, 1),
	MMC(SCLK_SDMMC_SAMPLE, "sdmmc_sample", "sclk_sdmmc", RK3228_SDMMC_CON1, 0),

	MMC(SCLK_SDIO_DRV,     "sdio_drv",     "sclk_sdio",  RK3228_SDIO_CON0,  1),
	MMC(SCLK_SDIO_SAMPLE,  "sdio_sample",  "sclk_sdio",  RK3228_SDIO_CON1,  0),

	MMC(SCLK_EMMC_DRV,     "emmc_drv",     "sclk_emmc",  RK3228_EMMC_CON0,  1),
	MMC(SCLK_EMMC_SAMPLE,  "emmc_sample",  "sclk_emmc",  RK3228_EMMC_CON1,  0),
};

static struct rockchip_clk_branch rk3126_clk_branches[] __initdata = {
	GATE(0, "pclk_stimer", "pclk_cpu", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(3), 15, GFLAGS),
	GATE(0, "pclk_s_efuse", "pclk_cpu", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(3), 14, GFLAGS),
	GATE(0, "pclk_sgrf", "pclk_cpu", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(3), 8, GFLAGS),
};

static struct rockchip_clk_branch rk3128_clk_branches[] __initdata = {
	COMPOSITE(SCLK_SFC, "sclk_sfc", mux_sclk_sfc_src_p, 0,
			RK2928_CLKSEL_CON(11), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RK2928_CLKGATE_CON(3), 15, GFLAGS),

	GATE(HCLK_GPS, "hclk_gps", "aclk_peri", 0, RK2928_CLKGATE_CON(3), 14, GFLAGS),
	GATE(PCLK_HDMI, "pclk_hdmi", "pclk_cpu", 0, RK2928_CLKGATE_CON(3), 8, GFLAGS),
};

static const char *const rk3128_critical_clocks[] __initconst = {
	"aclk_cpu",
	"hclk_cpu",
	"pclk_cpu",
	"aclk_peri",
	"hclk_peri",
	"pclk_peri",
};

static struct rockchip_clk_provider *__init rk3128_common_clk_init(struct device_node *np)
{
	struct rockchip_clk_provider *ctx;
	void __iomem *reg_base;

	reg_base = of_iomap(np, 0);
	if (!reg_base) {
		pr_err("%s: could not map cru region\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	ctx = rockchip_clk_init(np, reg_base, CLK_NR_CLKS);
	if (IS_ERR(ctx)) {
		pr_err("%s: rockchip clk init failed\n", __func__);
		iounmap(reg_base);
		return ERR_PTR(-ENOMEM);
	}

	rockchip_clk_register_plls(ctx, rk3128_pll_clks,
				   ARRAY_SIZE(rk3128_pll_clks),
				   RK3128_GRF_SOC_STATUS0);
	rockchip_clk_register_branches(ctx, common_clk_branches,
				  ARRAY_SIZE(common_clk_branches));

	rockchip_clk_register_armclk(ctx, ARMCLK, "armclk",
			mux_armclk_p, ARRAY_SIZE(mux_armclk_p),
			&rk3128_cpuclk_data, rk3128_cpuclk_rates,
			ARRAY_SIZE(rk3128_cpuclk_rates));

	rockchip_register_softrst(np, 9, reg_base + RK2928_SOFTRST_CON(0),
				  ROCKCHIP_SOFTRST_HIWORD_MASK);

	rockchip_register_restart_notifier(ctx, RK2928_GLB_SRST_FST, NULL);

	return ctx;
}

static void __init rk3126_clk_init(struct device_node *np)
{
	struct rockchip_clk_provider *ctx;

	ctx = rk3128_common_clk_init(np);
	if (IS_ERR(ctx))
		return;

	rockchip_clk_register_branches(ctx, rk3126_clk_branches,
				       ARRAY_SIZE(rk3126_clk_branches));
	rockchip_clk_protect_critical(rk3128_critical_clocks,
				      ARRAY_SIZE(rk3128_critical_clocks));

	rockchip_clk_of_add_provider(np, ctx);
}

CLK_OF_DECLARE(rk3126_cru, "rockchip,rk3126-cru", rk3126_clk_init);

static void __init rk3128_clk_init(struct device_node *np)
{
	struct rockchip_clk_provider *ctx;

	ctx = rk3128_common_clk_init(np);
	if (IS_ERR(ctx))
		return;

	rockchip_clk_register_branches(ctx, rk3128_clk_branches,
				       ARRAY_SIZE(rk3128_clk_branches));
	rockchip_clk_protect_critical(rk3128_critical_clocks,
				      ARRAY_SIZE(rk3128_critical_clocks));

	rockchip_clk_of_add_provider(np, ctx);
}

CLK_OF_DECLARE(rk3128_cru, "rockchip,rk3128-cru", rk3128_clk_init);
