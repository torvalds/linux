/*
 * Copyright (c) 2016 Rockchip Electronics Co. Ltd.
 * Author: Shawn Lin <shawn.lin@rock-chips.com>
 *         Andy Yan <andy.yan@rock-chips.com>
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
#include <dt-bindings/clock/rv1108-cru.h>
#include "clk.h"

#define RV1108_GRF_SOC_STATUS0	0x480

enum rv1108_plls {
	apll, dpll, gpll,
};

static struct rockchip_pll_rate_table rv1108_pll_rates[] = {
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
	RK3036_PLL_RATE( 700000000, 6, 350, 2, 1, 1, 0),
	RK3036_PLL_RATE( 696000000, 1, 58, 2, 1, 1, 0),
	RK3036_PLL_RATE( 600000000, 1, 75, 3, 1, 1, 0),
	RK3036_PLL_RATE( 594000000, 2, 99, 2, 1, 1, 0),
	RK3036_PLL_RATE( 504000000, 1, 63, 3, 1, 1, 0),
	RK3036_PLL_RATE( 500000000, 6, 250, 2, 1, 1, 0),
	RK3036_PLL_RATE( 408000000, 1, 68, 2, 2, 1, 0),
	RK3036_PLL_RATE( 312000000, 1, 52, 2, 2, 1, 0),
	RK3036_PLL_RATE( 216000000, 1, 72, 4, 2, 1, 0),
	RK3036_PLL_RATE(  96000000, 1, 64, 4, 4, 1, 0),
	{ /* sentinel */ },
};

#define RV1108_DIV_CORE_MASK		0xf
#define RV1108_DIV_CORE_SHIFT		4

#define RV1108_CLKSEL0(_core_peri_div)	\
	{				\
		.reg = RV1108_CLKSEL_CON(1),	\
		.val = HIWORD_UPDATE(_core_peri_div, RV1108_DIV_CORE_MASK,\
				RV1108_DIV_CORE_SHIFT)	\
	}

#define RV1108_CPUCLK_RATE(_prate, _core_peri_div)			\
	{								\
		.prate = _prate,					\
		.divs = {						\
			RV1108_CLKSEL0(_core_peri_div),		\
		},							\
	}

static struct rockchip_cpuclk_rate_table rv1108_cpuclk_rates[] __initdata = {
	RV1108_CPUCLK_RATE(816000000, 4),
	RV1108_CPUCLK_RATE(600000000, 4),
	RV1108_CPUCLK_RATE(312000000, 4),
};

static const struct rockchip_cpuclk_reg_data rv1108_cpuclk_data = {
	.core_reg = RV1108_CLKSEL_CON(0),
	.div_core_shift = 0,
	.div_core_mask = 0x1f,
	.mux_core_alt = 1,
	.mux_core_main = 0,
	.mux_core_shift = 8,
	.mux_core_mask = 0x1,
};

PNAME(mux_pll_p)		= { "xin24m", "xin24m"};
PNAME(mux_ddrphy_p)		= { "dpll_ddr", "gpll_ddr", "apll_ddr" };
PNAME(mux_armclk_p)		= { "apll_core", "gpll_core", "dpll_core" };
PNAME(mux_usb480m_pre_p)	= { "usbphy", "xin24m" };
PNAME(mux_hdmiphy_phy_p)	= { "hdmiphy", "xin24m" };
PNAME(mux_dclk_hdmiphy_pre_p)	= { "dclk_hdmiphy_src_gpll", "dclk_hdmiphy_src_dpll" };
PNAME(mux_pll_src_4plls_p)	= { "dpll", "hdmiphy", "gpll", "usb480m" };
PNAME(mux_pll_src_3plls_p)	= { "apll", "gpll", "dpll" };
PNAME(mux_pll_src_2plls_p)	= { "dpll", "gpll" };
PNAME(mux_pll_src_apll_gpll_p)	= { "apll", "gpll" };
PNAME(mux_aclk_peri_src_p)	= { "aclk_peri_src_dpll", "aclk_peri_src_gpll" };
PNAME(mux_aclk_bus_src_p)	= { "aclk_bus_src_gpll", "aclk_bus_src_apll", "aclk_bus_src_dpll" };
PNAME(mux_mmc_src_p)		= { "dpll", "gpll", "xin24m", "usb480m" };
PNAME(mux_pll_src_dpll_gpll_usb480m_p)	= { "dpll", "gpll", "usb480m" };
PNAME(mux_uart0_p)		= { "uart0_src", "uart0_frac", "xin24m" };
PNAME(mux_uart1_p)		= { "uart1_src", "uart1_frac", "xin24m" };
PNAME(mux_uart2_p)		= { "uart2_src", "uart2_frac", "xin24m" };
PNAME(mux_sclk_macphy_p)	= { "sclk_macphy_pre", "ext_gmac" };
PNAME(mux_i2s0_pre_p)		= { "i2s0_src", "i2s0_frac", "ext_i2s", "xin12m" };
PNAME(mux_i2s_out_p)		= { "i2s0_pre", "xin12m" };
PNAME(mux_i2s1_p)		= { "i2s1_src", "i2s1_frac", "xin12m" };
PNAME(mux_i2s2_p)		= { "i2s2_src", "i2s2_frac", "xin12m" };

static struct rockchip_pll_clock rv1108_pll_clks[] __initdata = {
	[apll] = PLL(pll_rk3399, PLL_APLL, "apll", mux_pll_p, 0, RV1108_PLL_CON(0),
		     RV1108_PLL_CON(3), 8, 31, 0, rv1108_pll_rates),
	[dpll] = PLL(pll_rk3399, PLL_DPLL, "dpll", mux_pll_p, 0, RV1108_PLL_CON(8),
		     RV1108_PLL_CON(11), 8, 31, 0, NULL),
	[gpll] = PLL(pll_rk3399, PLL_GPLL, "gpll", mux_pll_p, 0, RV1108_PLL_CON(16),
		     RV1108_PLL_CON(19), 8, 31, ROCKCHIP_PLL_SYNC_RATE, rv1108_pll_rates),
};

#define MFLAGS CLK_MUX_HIWORD_MASK
#define DFLAGS CLK_DIVIDER_HIWORD_MASK
#define GFLAGS (CLK_GATE_HIWORD_MASK | CLK_GATE_SET_TO_DISABLE)
#define IFLAGS ROCKCHIP_INVERTER_HIWORD_MASK

static struct rockchip_clk_branch rv1108_uart0_fracmux __initdata =
	MUX(SCLK_UART0, "sclk_uart0", mux_uart0_p, CLK_SET_RATE_PARENT,
			RV1108_CLKSEL_CON(13), 8, 2, MFLAGS);

static struct rockchip_clk_branch rv1108_uart1_fracmux __initdata =
	MUX(SCLK_UART1, "sclk_uart1", mux_uart1_p, CLK_SET_RATE_PARENT,
			RV1108_CLKSEL_CON(14), 8, 2, MFLAGS);

static struct rockchip_clk_branch rv1108_uart2_fracmux __initdata =
	MUX(SCLK_UART2, "sclk_uart2", mux_uart2_p, CLK_SET_RATE_PARENT,
			RV1108_CLKSEL_CON(15), 8, 2, MFLAGS);

static struct rockchip_clk_branch rv1108_i2s0_fracmux __initdata =
	MUX(0, "i2s0_pre", mux_i2s0_pre_p, CLK_SET_RATE_PARENT,
			RV1108_CLKSEL_CON(5), 12, 2, MFLAGS);

static struct rockchip_clk_branch rv1108_i2s1_fracmux __initdata =
	MUX(0, "i2s1_pre", mux_i2s1_p, CLK_SET_RATE_PARENT,
			RV1108_CLKSEL_CON(6), 12, 2, MFLAGS);

static struct rockchip_clk_branch rv1108_i2s2_fracmux __initdata =
	MUX(0, "i2s2_pre", mux_i2s2_p, CLK_SET_RATE_PARENT,
			RV1108_CLKSEL_CON(7), 12, 2, MFLAGS);

static struct rockchip_clk_branch rv1108_clk_branches[] __initdata = {
	MUX(0, "hdmi_phy", mux_hdmiphy_phy_p, CLK_SET_RATE_PARENT,
			RV1108_MISC_CON, 13, 2, MFLAGS),
	MUX(0, "usb480m", mux_usb480m_pre_p, CLK_SET_RATE_PARENT,
			RV1108_MISC_CON, 15, 2, MFLAGS),
	/*
	 * Clock-Architecture Diagram 2
	 */

	/* PD_CORE */
	GATE(0, "dpll_core", "dpll", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(0), 1, GFLAGS),
	GATE(0, "apll_core", "apll", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(0), 0, GFLAGS),
	GATE(0, "gpll_core", "gpll", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(0), 2, GFLAGS),
	COMPOSITE_NOMUX(0, "pclken_dbg", "armclk", CLK_IGNORE_UNUSED,
			RV1108_CLKSEL_CON(1), 4, 4, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RV1108_CLKGATE_CON(0), 5, GFLAGS),
	COMPOSITE_NOMUX(ACLK_ENMCORE, "aclkenm_core", "armclk", CLK_IGNORE_UNUSED,
			RV1108_CLKSEL_CON(1), 0, 3, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RV1108_CLKGATE_CON(0), 4, GFLAGS),
	GATE(ACLK_CORE, "aclk_core", "aclkenm_core", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(11), 0, GFLAGS),
	GATE(0, "pclk_dbg", "pclken_dbg", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(11), 1, GFLAGS),

	/* PD_RKVENC */

	/* PD_RKVDEC */

	/* PD_PMU_wrapper */
	COMPOSITE_NOMUX(0, "pmu_24m_ena", "gpll", CLK_IGNORE_UNUSED,
			RV1108_CLKSEL_CON(38), 0, 5, DFLAGS,
			RV1108_CLKGATE_CON(8), 12, GFLAGS),
	GATE(0, "pmu", "pmu_24m_ena", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(10), 0, GFLAGS),
	GATE(0, "intmem1", "pmu_24m_ena", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(10), 1, GFLAGS),
	GATE(0, "gpio0_pmu", "pmu_24m_ena", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(10), 2, GFLAGS),
	GATE(0, "pmugrf", "pmu_24m_ena", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(10), 3, GFLAGS),
	GATE(0, "pmu_noc", "pmu_24m_ena", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(10), 4, GFLAGS),
	GATE(0, "i2c0_pmu_pclk", "pmu_24m_ena", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(10), 5, GFLAGS),
	GATE(0, "pwm0_pmu_pclk", "pmu_24m_ena", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(10), 6, GFLAGS),
	COMPOSITE(0, "pwm0_pmu_clk", mux_pll_src_2plls_p, CLK_IGNORE_UNUSED,
			RV1108_CLKSEL_CON(12), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RV1108_CLKGATE_CON(8), 15, GFLAGS),
	COMPOSITE(0, "i2c0_pmu_clk", mux_pll_src_2plls_p, CLK_IGNORE_UNUSED,
			RV1108_CLKSEL_CON(19), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RV1108_CLKGATE_CON(8), 14, GFLAGS),
	GATE(0, "pvtm_pmu", "xin24m", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(8), 13, GFLAGS),

	/*
	 * Clock-Architecture Diagram 4
	 */
	COMPOSITE(0, "aclk_vio0_2wrap_occ", mux_pll_src_4plls_p, CLK_IGNORE_UNUSED,
			RV1108_CLKSEL_CON(28), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RV1108_CLKGATE_CON(6), 0, GFLAGS),
	GATE(0, "aclk_vio0_pre", "aclk_vio0_2wrap_occ", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(17), 0, GFLAGS),
	COMPOSITE_NOMUX(0, "hclk_vio_pre", "aclk_vio0_pre", 0,
			RV1108_CLKSEL_CON(29), 0, 5, DFLAGS,
			RV1108_CLKGATE_CON(7), 2, GFLAGS),
	COMPOSITE_NOMUX(0, "pclk_vio_pre", "aclk_vio0_pre", 0,
			RV1108_CLKSEL_CON(29), 8, 5, DFLAGS,
			RV1108_CLKGATE_CON(7), 3, GFLAGS),

	INVERTER(0, "pclk_vip", "ext_vip",
			RV1108_CLKSEL_CON(31), 8, IFLAGS),
	GATE(0, "pclk_isp_pre", "pclk_vip", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(7), 6, GFLAGS),
	GATE(0, "pclk_isp", "pclk_isp_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(18), 10, GFLAGS),
	GATE(0, "dclk_hdmiphy_src_gpll", "gpll", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(6), 5, GFLAGS),
	GATE(0, "dclk_hdmiphy_src_dpll", "dpll", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(6), 4, GFLAGS),
	COMPOSITE_NOGATE(0, "dclk_hdmiphy", mux_dclk_hdmiphy_pre_p, 0,
			RV1108_CLKSEL_CON(32), 6, 2, MFLAGS, 8, 6, DFLAGS),

	/*
	 * Clock-Architecture Diagram 5
	 */

	FACTOR(0, "xin12m", "xin24m", 0, 1, 2),

	COMPOSITE(0, "i2s0_src", mux_pll_src_2plls_p, 0,
			RV1108_CLKSEL_CON(5), 8, 1, MFLAGS, 0, 7, DFLAGS,
			RV1108_CLKGATE_CON(2), 0, GFLAGS),
	COMPOSITE_FRACMUX(0, "i2s1_frac", "i2s1_src", CLK_SET_RATE_PARENT,
			RV1108_CLKSEL_CON(8), 0,
			RV1108_CLKGATE_CON(2), 1, GFLAGS,
			&rv1108_i2s0_fracmux),
	GATE(SCLK_I2S0, "sclk_i2s0", "i2s0_pre", CLK_SET_RATE_PARENT,
			RV1108_CLKGATE_CON(2), 2, GFLAGS),
	COMPOSITE_NODIV(0, "i2s_out", mux_i2s_out_p, 0,
			RV1108_CLKSEL_CON(5), 15, 1, MFLAGS,
			RV1108_CLKGATE_CON(2), 3, GFLAGS),

	COMPOSITE(0, "i2s1_src", mux_pll_src_2plls_p, 0,
			RV1108_CLKSEL_CON(6), 8, 1, MFLAGS, 0, 7, DFLAGS,
			RV1108_CLKGATE_CON(2), 4, GFLAGS),
	COMPOSITE_FRACMUX(0, "i2s1_frac", "i2s1_src", CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(9), 0,
			RK2928_CLKGATE_CON(2), 5, GFLAGS,
			&rv1108_i2s1_fracmux),
	GATE(SCLK_I2S1, "sclk_i2s1", "i2s1_pre", CLK_SET_RATE_PARENT,
			RV1108_CLKGATE_CON(2), 6, GFLAGS),

	COMPOSITE(0, "i2s2_src", mux_pll_src_2plls_p, 0,
			RV1108_CLKSEL_CON(7), 8, 1, MFLAGS, 0, 7, DFLAGS,
			RV1108_CLKGATE_CON(3), 8, GFLAGS),
	COMPOSITE_FRACMUX(0, "i2s2_frac", "i2s2_src", CLK_SET_RATE_PARENT,
			RV1108_CLKSEL_CON(10), 0,
			RV1108_CLKGATE_CON(2), 9, GFLAGS,
			&rv1108_i2s2_fracmux),
	GATE(SCLK_I2S2, "sclk_i2s2", "i2s2_pre", CLK_SET_RATE_PARENT,
			RV1108_CLKGATE_CON(2), 10, GFLAGS),

	/* PD_BUS */
	GATE(0, "aclk_bus_src_gpll", "gpll", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(1), 0, GFLAGS),
	GATE(0, "aclk_bus_src_apll", "apll", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(1), 1, GFLAGS),
	GATE(0, "aclk_bus_src_dpll", "dpll", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(1), 2, GFLAGS),
	COMPOSITE_NOGATE(ACLK_PRE, "aclk_bus_pre", mux_aclk_bus_src_p, 0,
			RV1108_CLKSEL_CON(2), 8, 2, MFLAGS, 0, 5, DFLAGS),
	COMPOSITE_NOMUX(0, "hclk_bus_pre", "aclk_bus_2wrap_occ", 0,
			RV1108_CLKSEL_CON(3), 0, 5, DFLAGS,
			RV1108_CLKGATE_CON(1), 4, GFLAGS),
	COMPOSITE_NOMUX(0, "pclken_bus", "aclk_bus_2wrap_occ", 0,
			RV1108_CLKSEL_CON(3), 8, 5, DFLAGS,
			RV1108_CLKGATE_CON(1), 5, GFLAGS),
	GATE(0, "pclk_bus_pre", "pclken_bus", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(1), 6, GFLAGS),
	GATE(0, "pclk_top_pre", "pclken_bus", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(1), 7, GFLAGS),
	GATE(0, "pclk_ddr_pre", "pclken_bus", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(1), 8, GFLAGS),
	GATE(0, "clk_timer0", "mux_pll_p", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(1), 9, GFLAGS),
	GATE(0, "clk_timer1", "mux_pll_p", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(1), 10, GFLAGS),
	GATE(0, "pclk_timer", "pclk_bus_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(13), 4, GFLAGS),

	COMPOSITE(0, "uart0_src", mux_pll_src_dpll_gpll_usb480m_p, CLK_IGNORE_UNUSED,
			RV1108_CLKSEL_CON(13), 12, 2, MFLAGS, 0, 7, DFLAGS,
			RV1108_CLKGATE_CON(3), 1, GFLAGS),
	COMPOSITE(0, "uart1_src", mux_pll_src_dpll_gpll_usb480m_p, CLK_IGNORE_UNUSED,
			RV1108_CLKSEL_CON(14), 12, 2, MFLAGS, 0, 7, DFLAGS,
			RV1108_CLKGATE_CON(3), 3, GFLAGS),
	COMPOSITE(0, "uart21_src", mux_pll_src_dpll_gpll_usb480m_p, CLK_IGNORE_UNUSED,
			RV1108_CLKSEL_CON(15), 12, 2, MFLAGS, 0, 7, DFLAGS,
			RV1108_CLKGATE_CON(3), 5, GFLAGS),

	COMPOSITE_FRACMUX(0, "uart0_frac", "uart0_src", CLK_SET_RATE_PARENT,
			RV1108_CLKSEL_CON(16), 0,
			RV1108_CLKGATE_CON(3), 2, GFLAGS,
			&rv1108_uart0_fracmux),
	COMPOSITE_FRACMUX(0, "uart1_frac", "uart1_src", CLK_SET_RATE_PARENT,
			RV1108_CLKSEL_CON(17), 0,
			RV1108_CLKGATE_CON(3), 4, GFLAGS,
			&rv1108_uart1_fracmux),
	COMPOSITE_FRACMUX(0, "uart2_frac", "uart2_src", CLK_SET_RATE_PARENT,
			RV1108_CLKSEL_CON(18), 0,
			RV1108_CLKGATE_CON(3), 6, GFLAGS,
			&rv1108_uart2_fracmux),
	GATE(PCLK_UART0, "pclk_uart0", "pclk_bus_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(13), 10, GFLAGS),
	GATE(PCLK_UART1, "pclk_uart1", "pclk_bus_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(13), 11, GFLAGS),
	GATE(PCLK_UART2, "pclk_uart2", "pclk_bus_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(13), 12, GFLAGS),

	COMPOSITE(0, "clk_i2c1", mux_pll_src_2plls_p, CLK_IGNORE_UNUSED,
			RV1108_CLKSEL_CON(19), 15, 2, MFLAGS, 8, 7, DFLAGS,
			RV1108_CLKGATE_CON(3), 7, GFLAGS),
	COMPOSITE(0, "clk_i2c2", mux_pll_src_2plls_p, CLK_IGNORE_UNUSED,
			RV1108_CLKSEL_CON(20), 7, 2, MFLAGS, 0, 7, DFLAGS,
			RV1108_CLKGATE_CON(3), 8, GFLAGS),
	COMPOSITE(0, "clk_i2c3", mux_pll_src_2plls_p, CLK_IGNORE_UNUSED,
			RV1108_CLKSEL_CON(20), 15, 2, MFLAGS, 8, 7, DFLAGS,
			RV1108_CLKGATE_CON(3), 9, GFLAGS),
	GATE(0, "pclk_i2c1", "pclk_bus_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(13), 0, GFLAGS),
	GATE(0, "pclk_i2c2", "pclk_bus_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(13), 1, GFLAGS),
	GATE(0, "pclk_i2c3", "pclk_bus_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(13), 2, GFLAGS),
	COMPOSITE(0, "clk_pwm1", mux_pll_src_2plls_p, CLK_IGNORE_UNUSED,
			RV1108_CLKSEL_CON(12), 15, 2, MFLAGS, 8, 7, DFLAGS,
			RV1108_CLKGATE_CON(3), 10, GFLAGS),
	GATE(0, "pclk_pwm1", "pclk_bus_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(13), 6, GFLAGS),
	GATE(0, "pclk_wdt", "pclk_bus_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(13), 3, GFLAGS),
	GATE(0, "pclk_gpio1", "pclk_bus_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(13), 7, GFLAGS),
	GATE(0, "pclk_gpio2", "pclk_bus_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(13), 8, GFLAGS),
	GATE(0, "pclk_gpio3", "pclk_bus_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(13), 9, GFLAGS),

	GATE(0, "pclk_grf", "pclk_bus_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(14), 0, GFLAGS),

	GATE(ACLK_DMAC, "aclk_dmac", "aclk_bus_pre", 0,
	     RV1108_CLKGATE_CON(12), 2, GFLAGS),
	GATE(0, "hclk_rom", "hclk_bus_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(12), 3, GFLAGS),
	GATE(0, "aclk_intmem", "aclk_bus_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(12), 1, GFLAGS),

	/* PD_DDR */
	GATE(0, "apll_ddr", "apll", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(0), 8, GFLAGS),
	GATE(0, "dpll_ddr", "dpll", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(0), 9, GFLAGS),
	GATE(0, "gpll_ddr", "gpll", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(0), 10, GFLAGS),
	COMPOSITE(0, "ddrphy4x", mux_ddrphy_p, CLK_IGNORE_UNUSED,
			RV1108_CLKSEL_CON(4), 8, 2, MFLAGS, 0, 3,
			DFLAGS | CLK_DIVIDER_POWER_OF_TWO,
			RV1108_CLKGATE_CON(10), 9, GFLAGS),
	GATE(0, "ddrupctl", "ddrphy_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(12), 4, GFLAGS),
	GATE(0, "ddrc", "ddrphy", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(12), 5, GFLAGS),
	GATE(0, "ddrmon", "ddrphy_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(12), 6, GFLAGS),
	GATE(0, "timer_clk", "xin24m", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(0), 11, GFLAGS),

	/*
	 * Clock-Architecture Diagram 6
	 */

	/* PD_PERI */
	COMPOSITE_NOMUX(0, "pclk_periph_pre", "gpll", 0,
			RV1108_CLKSEL_CON(23), 10, 5, DFLAGS,
			RV1108_CLKGATE_CON(4), 5, GFLAGS),
	GATE(0, "pclk_periph", "pclk_periph_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(15), 13, GFLAGS),
	COMPOSITE_NOMUX(0, "hclk_periph_pre", "gpll", 0,
			RV1108_CLKSEL_CON(23), 5, 5, DFLAGS,
			RV1108_CLKGATE_CON(4), 4, GFLAGS),
	GATE(0, "hclk_periph", "hclk_periph_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(15), 12, GFLAGS),

	GATE(0, "aclk_peri_src_dpll", "dpll", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(4), 1, GFLAGS),
	GATE(0, "aclk_peri_src_gpll", "gpll", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(4), 2, GFLAGS),
	COMPOSITE(0, "aclk_periph", mux_aclk_peri_src_p, CLK_IGNORE_UNUSED,
			RV1108_CLKSEL_CON(23), 15, 2, MFLAGS, 0, 5, DFLAGS,
			RV1108_CLKGATE_CON(15), 11, GFLAGS),

	COMPOSITE(SCLK_SDMMC, "sclk_sdmmc0", mux_mmc_src_p, 0,
			RV1108_CLKSEL_CON(25), 8, 2, MFLAGS, 0, 8, DFLAGS,
			RV1108_CLKGATE_CON(5), 0, GFLAGS),

	COMPOSITE_NODIV(0, "sclk_sdio_src", mux_mmc_src_p, 0,
			RV1108_CLKSEL_CON(25), 10, 2, MFLAGS,
			RV1108_CLKGATE_CON(5), 2, GFLAGS),
	DIV(SCLK_SDIO, "sclk_sdio", "sclk_sdio_src", 0,
			RV1108_CLKSEL_CON(26), 0, 8, DFLAGS),

	COMPOSITE_NODIV(0, "sclk_emmc_src", mux_mmc_src_p, 0,
			RV1108_CLKSEL_CON(25), 12, 2, MFLAGS,
			RV1108_CLKGATE_CON(5), 1, GFLAGS),
	DIV(SCLK_EMMC, "sclk_emmc", "sclk_emmc_src", 0,
			RK2928_CLKSEL_CON(26), 8, 8, DFLAGS),
	GATE(HCLK_SDMMC, "hclk_sdmmc", "hclk_periph", 0, RV1108_CLKGATE_CON(15), 0, GFLAGS),
	GATE(HCLK_SDIO, "hclk_sdio", "hclk_periph", 0, RV1108_CLKGATE_CON(15), 1, GFLAGS),
	GATE(HCLK_EMMC, "hclk_emmc", "hclk_periph", 0, RV1108_CLKGATE_CON(15), 2, GFLAGS),

	COMPOSITE(SCLK_NANDC, "sclk_nandc", mux_pll_src_2plls_p, 0,
			RV1108_CLKSEL_CON(27), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RV1108_CLKGATE_CON(5), 3, GFLAGS),
	GATE(HCLK_NANDC, "hclk_nandc", "hclk_periph", 0, RV1108_CLKGATE_CON(15), 3, GFLAGS),

	COMPOSITE(SCLK_SFC, "sclk_sfc", mux_pll_src_2plls_p, 0,
			RV1108_CLKSEL_CON(27), 7, 2, MFLAGS, 0, 7, DFLAGS,
			RV1108_CLKGATE_CON(5), 4, GFLAGS),
	GATE(HCLK_SFC, "hclk_sfc", "hclk_periph", 0, RV1108_CLKGATE_CON(15), 10, GFLAGS),

	COMPOSITE(0, "sclk_macphy_pre", mux_pll_src_apll_gpll_p, 0,
			RV1108_CLKSEL_CON(24), 12, 2, MFLAGS, 0, 5, DFLAGS,
			RV1108_CLKGATE_CON(4), 10, GFLAGS),
	MUX(0, "sclk_macphy", mux_sclk_macphy_p, CLK_SET_RATE_PARENT,
			RV1108_CLKSEL_CON(24), 8, 2, MFLAGS),
	GATE(0, "sclk_macphy_rx", "sclk_macphy", 0, RV1108_CLKGATE_CON(4), 8, GFLAGS),
	GATE(0, "sclk_mac_ref", "sclk_macphy", 0, RV1108_CLKGATE_CON(4), 6, GFLAGS),
	GATE(0, "sclk_mac_refout", "sclk_macphy", 0, RV1108_CLKGATE_CON(4), 7, GFLAGS),

	MMC(SCLK_SDMMC_DRV,    "sdmmc_drv",    "sclk_sdmmc", RV1108_SDMMC_CON0, 1),
	MMC(SCLK_SDMMC_SAMPLE, "sdmmc_sample", "sclk_sdmmc", RV1108_SDMMC_CON1, 1),

	MMC(SCLK_SDIO_DRV,     "sdio_drv",     "sclk_sdio",  RV1108_SDIO_CON0,  1),
	MMC(SCLK_SDIO_SAMPLE,  "sdio_sample",  "sclk_sdio",  RV1108_SDIO_CON1,  1),

	MMC(SCLK_EMMC_DRV,     "emmc_drv",     "sclk_emmc",  RV1108_EMMC_CON0,  1),
	MMC(SCLK_EMMC_SAMPLE,  "emmc_sample",  "sclk_emmc",  RV1108_EMMC_CON1,  1),
};

static const char *const rv1108_critical_clocks[] __initconst = {
	"aclk_core",
	"aclk_bus_src_gpll",
	"aclk_periph",
	"hclk_periph",
	"pclk_periph",
};

static void __init rv1108_clk_init(struct device_node *np)
{
	struct rockchip_clk_provider *ctx;
	void __iomem *reg_base;

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

	rockchip_clk_register_plls(ctx, rv1108_pll_clks,
				   ARRAY_SIZE(rv1108_pll_clks),
				   RV1108_GRF_SOC_STATUS0);
	rockchip_clk_register_branches(ctx, rv1108_clk_branches,
				  ARRAY_SIZE(rv1108_clk_branches));
	rockchip_clk_protect_critical(rv1108_critical_clocks,
				      ARRAY_SIZE(rv1108_critical_clocks));

	rockchip_clk_register_armclk(ctx, ARMCLK, "armclk",
			mux_armclk_p, ARRAY_SIZE(mux_armclk_p),
			&rv1108_cpuclk_data, rv1108_cpuclk_rates,
			ARRAY_SIZE(rv1108_cpuclk_rates));

	rockchip_register_softrst(np, 13, reg_base + RV1108_SOFTRST_CON(0),
				  ROCKCHIP_SOFTRST_HIWORD_MASK);

	rockchip_register_restart_notifier(ctx, RV1108_GLB_SRST_FST, NULL);

	rockchip_clk_of_add_provider(np, ctx);
}
CLK_OF_DECLARE(rv1108_cru, "rockchip,rv1108-cru", rv1108_clk_init);
