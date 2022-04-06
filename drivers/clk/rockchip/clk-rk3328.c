// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2016 Rockchip Electronics Co. Ltd.
 * Author: Elaine <zhangqing@rock-chips.com>
 */

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/syscore_ops.h>
#include <dt-bindings/clock/rk3328-cru.h>
#include "clk.h"

#define RK3328_GRF_SOC_CON4		0x410
#define RK3328_GRF_SOC_STATUS0		0x480
#define RK3328_GRF_MAC_CON1		0x904
#define RK3328_GRF_MAC_CON2		0x908

enum rk3328_plls {
	apll, dpll, cpll, gpll, npll,
};

static struct rockchip_pll_rate_table rk3328_pll_rates[] = {
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

static struct rockchip_pll_rate_table rk3328_pll_frac_rates[] = {
	/* _mhz, _refdiv, _fbdiv, _postdiv1, _postdiv2, _dsmpd, _frac */
	RK3036_PLL_RATE(1016064000, 3, 127, 1, 1, 0, 134218),
	/* vco = 1016064000 */
	RK3036_PLL_RATE(983040000, 24, 983, 1, 1, 0, 671089),
	/* vco = 983040000 */
	RK3036_PLL_RATE(491520000, 24, 983, 2, 1, 0, 671089),
	/* vco = 983040000 */
	RK3036_PLL_RATE(61440000, 6, 215, 7, 2, 0, 671089),
	/* vco = 860156000 */
	RK3036_PLL_RATE(56448000, 12, 451, 4, 4, 0, 9797895),
	/* vco = 903168000 */
	RK3036_PLL_RATE(40960000, 12, 409, 4, 5, 0, 10066330),
	/* vco = 819200000 */
	{ /* sentinel */ },
};

#define RK3328_DIV_ACLKM_MASK		0x7
#define RK3328_DIV_ACLKM_SHIFT		4
#define RK3328_DIV_PCLK_DBG_MASK	0xf
#define RK3328_DIV_PCLK_DBG_SHIFT	0

#define RK3328_CLKSEL1(_aclk_core, _pclk_dbg)				\
{									\
	.reg = RK3328_CLKSEL_CON(1),					\
	.val = HIWORD_UPDATE(_aclk_core, RK3328_DIV_ACLKM_MASK,		\
			     RK3328_DIV_ACLKM_SHIFT) |			\
	       HIWORD_UPDATE(_pclk_dbg, RK3328_DIV_PCLK_DBG_MASK,	\
			     RK3328_DIV_PCLK_DBG_SHIFT),		\
}

#define RK3328_CPUCLK_RATE(_prate, _aclk_core, _pclk_dbg)		\
{									\
	.prate = _prate,						\
	.divs = {							\
		RK3328_CLKSEL1(_aclk_core, _pclk_dbg),			\
	},								\
}

static struct rockchip_cpuclk_rate_table rk3328_cpuclk_rates[] __initdata = {
	RK3328_CPUCLK_RATE(1800000000, 1, 7),
	RK3328_CPUCLK_RATE(1704000000, 1, 7),
	RK3328_CPUCLK_RATE(1608000000, 1, 7),
	RK3328_CPUCLK_RATE(1512000000, 1, 7),
	RK3328_CPUCLK_RATE(1488000000, 1, 5),
	RK3328_CPUCLK_RATE(1416000000, 1, 5),
	RK3328_CPUCLK_RATE(1392000000, 1, 5),
	RK3328_CPUCLK_RATE(1296000000, 1, 5),
	RK3328_CPUCLK_RATE(1200000000, 1, 5),
	RK3328_CPUCLK_RATE(1104000000, 1, 5),
	RK3328_CPUCLK_RATE(1008000000, 1, 5),
	RK3328_CPUCLK_RATE(912000000, 1, 5),
	RK3328_CPUCLK_RATE(816000000, 1, 3),
	RK3328_CPUCLK_RATE(696000000, 1, 3),
	RK3328_CPUCLK_RATE(600000000, 1, 3),
	RK3328_CPUCLK_RATE(408000000, 1, 1),
	RK3328_CPUCLK_RATE(312000000, 1, 1),
	RK3328_CPUCLK_RATE(216000000,  1, 1),
	RK3328_CPUCLK_RATE(96000000, 1, 1),
};

static const struct rockchip_cpuclk_reg_data rk3328_cpuclk_data = {
	.core_reg[0] = RK3328_CLKSEL_CON(0),
	.div_core_shift[0] = 0,
	.div_core_mask[0] = 0x1f,
	.num_cores = 1,
	.mux_core_alt = 1,
	.mux_core_main = 3,
	.mux_core_shift = 6,
	.mux_core_mask = 0x3,
};

PNAME(mux_pll_p)		= { "xin24m" };

PNAME(mux_2plls_p)		= { "cpll", "gpll" };
PNAME(mux_gpll_cpll_p)		= { "gpll", "cpll" };
PNAME(mux_cpll_gpll_apll_p)	= { "cpll", "gpll", "apll" };
PNAME(mux_2plls_xin24m_p)	= { "cpll", "gpll", "xin24m" };
PNAME(mux_2plls_hdmiphy_p)	= { "cpll", "gpll",
				    "dummy_hdmiphy" };
PNAME(mux_4plls_p)		= { "cpll", "gpll",
				    "dummy_hdmiphy",
				    "usb480m" };
PNAME(mux_2plls_u480m_p)	= { "cpll", "gpll",
				    "usb480m" };
PNAME(mux_2plls_24m_u480m_p)	= { "cpll", "gpll",
				     "xin24m", "usb480m" };

PNAME(mux_ddrphy_p)		= { "dpll", "apll", "cpll" };
PNAME(mux_armclk_p)		= { "apll_core",
				    "gpll_core",
				    "dpll_core",
				    "npll_core"};
PNAME(mux_hdmiphy_p)		= { "hdmi_phy", "xin24m" };
PNAME(mux_usb480m_p)		= { "usb480m_phy",
				    "xin24m" };

PNAME(mux_i2s0_p)		= { "clk_i2s0_div",
				    "clk_i2s0_frac",
				    "xin12m",
				    "xin12m" };
PNAME(mux_i2s1_p)		= { "clk_i2s1_div",
				    "clk_i2s1_frac",
				    "clkin_i2s1",
				    "xin12m" };
PNAME(mux_i2s2_p)		= { "clk_i2s2_div",
				    "clk_i2s2_frac",
				    "clkin_i2s2",
				    "xin12m" };
PNAME(mux_i2s1out_p)		= { "clk_i2s1", "xin12m"};
PNAME(mux_i2s2out_p)		= { "clk_i2s2", "xin12m" };
PNAME(mux_spdif_p)		= { "clk_spdif_div",
				    "clk_spdif_frac",
				    "xin12m",
				    "xin12m" };
PNAME(mux_uart0_p)		= { "clk_uart0_div",
				    "clk_uart0_frac",
				    "xin24m" };
PNAME(mux_uart1_p)		= { "clk_uart1_div",
				    "clk_uart1_frac",
				    "xin24m" };
PNAME(mux_uart2_p)		= { "clk_uart2_div",
				    "clk_uart2_frac",
				    "xin24m" };

PNAME(mux_sclk_cif_p)		= { "clk_cif_src",
				    "xin24m" };
PNAME(mux_dclk_lcdc_p)		= { "hdmiphy",
				    "dclk_lcdc_src" };
PNAME(mux_aclk_peri_pre_p)	= { "cpll_peri",
				    "gpll_peri",
				    "hdmiphy_peri" };
PNAME(mux_ref_usb3otg_src_p)	= { "xin24m",
				    "clk_usb3otg_ref" };
PNAME(mux_xin24m_32k_p)		= { "xin24m",
				    "clk_rtc32k" };
PNAME(mux_mac2io_src_p)		= { "clk_mac2io_src",
				    "gmac_clkin" };
PNAME(mux_mac2phy_src_p)	= { "clk_mac2phy_src",
				    "phy_50m_out" };
PNAME(mux_mac2io_ext_p)		= { "clk_mac2io",
				    "gmac_clkin" };

static struct rockchip_pll_clock rk3328_pll_clks[] __initdata = {
	[apll] = PLL(pll_rk3328, PLL_APLL, "apll", mux_pll_p,
		     0, RK3328_PLL_CON(0),
		     RK3328_MODE_CON, 0, 4, 0, rk3328_pll_frac_rates),
	[dpll] = PLL(pll_rk3328, PLL_DPLL, "dpll", mux_pll_p,
		     0, RK3328_PLL_CON(8),
		     RK3328_MODE_CON, 4, 3, 0, NULL),
	[cpll] = PLL(pll_rk3328, PLL_CPLL, "cpll", mux_pll_p,
		     0, RK3328_PLL_CON(16),
		     RK3328_MODE_CON, 8, 2, 0, rk3328_pll_rates),
	[gpll] = PLL(pll_rk3328, PLL_GPLL, "gpll", mux_pll_p,
		     0, RK3328_PLL_CON(24),
		     RK3328_MODE_CON, 12, 1, 0, rk3328_pll_frac_rates),
	[npll] = PLL(pll_rk3328, PLL_NPLL, "npll", mux_pll_p,
		     0, RK3328_PLL_CON(40),
		     RK3328_MODE_CON, 1, 0, 0, rk3328_pll_rates),
};

#define MFLAGS CLK_MUX_HIWORD_MASK
#define DFLAGS CLK_DIVIDER_HIWORD_MASK
#define GFLAGS (CLK_GATE_HIWORD_MASK | CLK_GATE_SET_TO_DISABLE)

static struct rockchip_clk_branch rk3328_i2s0_fracmux __initdata =
	MUX(0, "i2s0_pre", mux_i2s0_p, CLK_SET_RATE_PARENT,
			RK3328_CLKSEL_CON(6), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3328_i2s1_fracmux __initdata =
	MUX(0, "i2s1_pre", mux_i2s1_p, CLK_SET_RATE_PARENT,
			RK3328_CLKSEL_CON(8), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3328_i2s2_fracmux __initdata =
	MUX(0, "i2s2_pre", mux_i2s2_p, CLK_SET_RATE_PARENT,
			RK3328_CLKSEL_CON(10), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3328_spdif_fracmux __initdata =
	MUX(SCLK_SPDIF, "sclk_spdif", mux_spdif_p, CLK_SET_RATE_PARENT,
			RK3328_CLKSEL_CON(12), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3328_uart0_fracmux __initdata =
	MUX(SCLK_UART0, "sclk_uart0", mux_uart0_p, CLK_SET_RATE_PARENT,
			RK3328_CLKSEL_CON(14), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3328_uart1_fracmux __initdata =
	MUX(SCLK_UART1, "sclk_uart1", mux_uart1_p, CLK_SET_RATE_PARENT,
			RK3328_CLKSEL_CON(16), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3328_uart2_fracmux __initdata =
	MUX(SCLK_UART2, "sclk_uart2", mux_uart2_p, CLK_SET_RATE_PARENT,
			RK3328_CLKSEL_CON(18), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3328_clk_branches[] __initdata = {
	/*
	 * Clock-Architecture Diagram 1
	 */

	DIV(0, "clk_24m", "xin24m", CLK_IGNORE_UNUSED,
			RK3328_CLKSEL_CON(2), 8, 5, DFLAGS),
	COMPOSITE(SCLK_RTC32K, "clk_rtc32k", mux_2plls_xin24m_p, 0,
			RK3328_CLKSEL_CON(38), 14, 2, MFLAGS, 0, 14, DFLAGS,
			RK3328_CLKGATE_CON(0), 11, GFLAGS),

	/* PD_MISC */
	MUX(HDMIPHY, "hdmiphy", mux_hdmiphy_p, CLK_SET_RATE_PARENT,
			RK3328_MISC_CON, 13, 1, MFLAGS),
	MUX(USB480M, "usb480m", mux_usb480m_p, CLK_SET_RATE_PARENT,
			RK3328_MISC_CON, 15, 1, MFLAGS),

	/*
	 * Clock-Architecture Diagram 2
	 */

	/* PD_CORE */
	GATE(0, "apll_core", "apll", CLK_IGNORE_UNUSED,
			RK3328_CLKGATE_CON(0), 0, GFLAGS),
	GATE(0, "gpll_core", "gpll", CLK_IGNORE_UNUSED,
			RK3328_CLKGATE_CON(0), 2, GFLAGS),
	GATE(0, "dpll_core", "dpll", CLK_IGNORE_UNUSED,
			RK3328_CLKGATE_CON(0), 1, GFLAGS),
	GATE(0, "npll_core", "npll", CLK_IGNORE_UNUSED,
			RK3328_CLKGATE_CON(0), 12, GFLAGS),
	COMPOSITE_NOMUX(0, "pclk_dbg", "armclk", CLK_IS_CRITICAL,
			RK3328_CLKSEL_CON(1), 0, 4, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RK3328_CLKGATE_CON(7), 0, GFLAGS),
	COMPOSITE_NOMUX(0, "aclk_core", "armclk", CLK_IS_CRITICAL,
			RK3328_CLKSEL_CON(1), 4, 3, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RK3328_CLKGATE_CON(7), 1, GFLAGS),
	GATE(0, "aclk_core_niu", "aclk_core", CLK_IS_CRITICAL,
			RK3328_CLKGATE_CON(13), 0, GFLAGS),
	GATE(0, "aclk_gic400", "aclk_core", CLK_IS_CRITICAL,
			RK3328_CLKGATE_CON(13), 1, GFLAGS),

	GATE(0, "clk_jtag", "jtag_clkin", CLK_IS_CRITICAL,
			RK3328_CLKGATE_CON(7), 2, GFLAGS),

	/* PD_GPU */
	COMPOSITE(0, "aclk_gpu_pre", mux_4plls_p, 0,
			RK3328_CLKSEL_CON(44), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3328_CLKGATE_CON(6), 6, GFLAGS),
	GATE(ACLK_GPU, "aclk_gpu", "aclk_gpu_pre", CLK_SET_RATE_PARENT,
			RK3328_CLKGATE_CON(14), 0, GFLAGS),
	GATE(0, "aclk_gpu_niu", "aclk_gpu_pre", CLK_IS_CRITICAL,
			RK3328_CLKGATE_CON(14), 1, GFLAGS),

	/* PD_DDR */
	COMPOSITE(0, "clk_ddr", mux_ddrphy_p, CLK_IS_CRITICAL,
			RK3328_CLKSEL_CON(3), 8, 2, MFLAGS, 0, 3, DFLAGS | CLK_DIVIDER_POWER_OF_TWO,
			RK3328_CLKGATE_CON(0), 4, GFLAGS),
	GATE(0, "clk_ddrmsch", "clk_ddr", CLK_IS_CRITICAL,
			RK3328_CLKGATE_CON(18), 6, GFLAGS),
	GATE(0, "clk_ddrupctl", "clk_ddr", CLK_IS_CRITICAL,
			RK3328_CLKGATE_CON(18), 5, GFLAGS),
	GATE(0, "aclk_ddrupctl", "clk_ddr", CLK_IGNORE_UNUSED,
			RK3328_CLKGATE_CON(18), 4, GFLAGS),
	GATE(0, "clk_ddrmon", "xin24m", CLK_IGNORE_UNUSED,
			RK3328_CLKGATE_CON(0), 6, GFLAGS),

	COMPOSITE(PCLK_DDR, "pclk_ddr", mux_2plls_hdmiphy_p, CLK_IS_CRITICAL,
			RK3328_CLKSEL_CON(4), 13, 2, MFLAGS, 8, 3, DFLAGS,
			RK3328_CLKGATE_CON(7), 4, GFLAGS),
	GATE(0, "pclk_ddrupctl", "pclk_ddr", CLK_IS_CRITICAL,
			RK3328_CLKGATE_CON(18), 1, GFLAGS),
	GATE(0, "pclk_ddr_msch", "pclk_ddr", CLK_IS_CRITICAL,
			RK3328_CLKGATE_CON(18), 2, GFLAGS),
	GATE(0, "pclk_ddr_mon", "pclk_ddr", CLK_IS_CRITICAL,
			RK3328_CLKGATE_CON(18), 3, GFLAGS),
	GATE(0, "pclk_ddrstdby", "pclk_ddr", CLK_IGNORE_UNUSED,
			RK3328_CLKGATE_CON(18), 7, GFLAGS),
	GATE(0, "pclk_ddr_grf", "pclk_ddr", CLK_IS_CRITICAL,
			RK3328_CLKGATE_CON(18), 9, GFLAGS),

	/*
	 * Clock-Architecture Diagram 3
	 */

	/* PD_BUS */
	COMPOSITE(ACLK_BUS_PRE, "aclk_bus_pre", mux_2plls_hdmiphy_p, CLK_IS_CRITICAL,
			RK3328_CLKSEL_CON(0), 13, 2, MFLAGS, 8, 5, DFLAGS,
			RK3328_CLKGATE_CON(8), 0, GFLAGS),
	COMPOSITE_NOMUX(HCLK_BUS_PRE, "hclk_bus_pre", "aclk_bus_pre", CLK_IS_CRITICAL,
			RK3328_CLKSEL_CON(1), 8, 2, DFLAGS,
			RK3328_CLKGATE_CON(8), 1, GFLAGS),
	COMPOSITE_NOMUX(PCLK_BUS_PRE, "pclk_bus_pre", "aclk_bus_pre", CLK_IS_CRITICAL,
			RK3328_CLKSEL_CON(1), 12, 3, DFLAGS,
			RK3328_CLKGATE_CON(8), 2, GFLAGS),
	GATE(0, "pclk_bus", "pclk_bus_pre", CLK_IS_CRITICAL,
			RK3328_CLKGATE_CON(8), 3, GFLAGS),
	GATE(0, "pclk_phy_pre", "pclk_bus_pre", CLK_IS_CRITICAL,
			RK3328_CLKGATE_CON(8), 4, GFLAGS),

	COMPOSITE(SCLK_TSP, "clk_tsp", mux_2plls_p, 0,
			RK3328_CLKSEL_CON(21), 15, 1, MFLAGS, 8, 5, DFLAGS,
			RK3328_CLKGATE_CON(2), 5, GFLAGS),
	GATE(0, "clk_hsadc_tsp", "ext_gpio3a2", 0,
			RK3328_CLKGATE_CON(17), 13, GFLAGS),

	/* PD_I2S */
	COMPOSITE(0, "clk_i2s0_div", mux_2plls_p, 0,
			RK3328_CLKSEL_CON(6), 15, 1, MFLAGS, 0, 7, DFLAGS,
			RK3328_CLKGATE_CON(1), 1, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_i2s0_frac", "clk_i2s0_div", CLK_SET_RATE_PARENT,
			RK3328_CLKSEL_CON(7), 0,
			RK3328_CLKGATE_CON(1), 2, GFLAGS,
			&rk3328_i2s0_fracmux),
	GATE(SCLK_I2S0, "clk_i2s0", "i2s0_pre", CLK_SET_RATE_PARENT,
			RK3328_CLKGATE_CON(1), 3, GFLAGS),

	COMPOSITE(0, "clk_i2s1_div", mux_2plls_p, 0,
			RK3328_CLKSEL_CON(8), 15, 1, MFLAGS, 0, 7, DFLAGS,
			RK3328_CLKGATE_CON(1), 4, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_i2s1_frac", "clk_i2s1_div", CLK_SET_RATE_PARENT,
			RK3328_CLKSEL_CON(9), 0,
			RK3328_CLKGATE_CON(1), 5, GFLAGS,
			&rk3328_i2s1_fracmux),
	GATE(SCLK_I2S1, "clk_i2s1", "i2s1_pre", CLK_SET_RATE_PARENT,
			RK3328_CLKGATE_CON(1), 6, GFLAGS),
	COMPOSITE_NODIV(SCLK_I2S1_OUT, "i2s1_out", mux_i2s1out_p, 0,
			RK3328_CLKSEL_CON(8), 12, 1, MFLAGS,
			RK3328_CLKGATE_CON(1), 7, GFLAGS),

	COMPOSITE(0, "clk_i2s2_div", mux_2plls_p, 0,
			RK3328_CLKSEL_CON(10), 15, 1, MFLAGS, 0, 7, DFLAGS,
			RK3328_CLKGATE_CON(1), 8, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_i2s2_frac", "clk_i2s2_div", CLK_SET_RATE_PARENT,
			RK3328_CLKSEL_CON(11), 0,
			RK3328_CLKGATE_CON(1), 9, GFLAGS,
			&rk3328_i2s2_fracmux),
	GATE(SCLK_I2S2, "clk_i2s2", "i2s2_pre", CLK_SET_RATE_PARENT,
			RK3328_CLKGATE_CON(1), 10, GFLAGS),
	COMPOSITE_NODIV(SCLK_I2S2_OUT, "i2s2_out", mux_i2s2out_p, 0,
			RK3328_CLKSEL_CON(10), 12, 1, MFLAGS,
			RK3328_CLKGATE_CON(1), 11, GFLAGS),

	COMPOSITE(0, "clk_spdif_div", mux_2plls_p, 0,
			RK3328_CLKSEL_CON(12), 15, 1, MFLAGS, 0, 7, DFLAGS,
			RK3328_CLKGATE_CON(1), 12, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_spdif_frac", "clk_spdif_div", CLK_SET_RATE_PARENT,
			RK3328_CLKSEL_CON(13), 0,
			RK3328_CLKGATE_CON(1), 13, GFLAGS,
			&rk3328_spdif_fracmux),

	/* PD_UART */
	COMPOSITE(0, "clk_uart0_div", mux_2plls_u480m_p, 0,
			RK3328_CLKSEL_CON(14), 12, 2, MFLAGS, 0, 7, DFLAGS,
			RK3328_CLKGATE_CON(1), 14, GFLAGS),
	COMPOSITE(0, "clk_uart1_div", mux_2plls_u480m_p, 0,
			RK3328_CLKSEL_CON(16), 12, 2, MFLAGS, 0, 7, DFLAGS,
			RK3328_CLKGATE_CON(2), 0, GFLAGS),
	COMPOSITE(0, "clk_uart2_div", mux_2plls_u480m_p, 0,
			RK3328_CLKSEL_CON(18), 12, 2, MFLAGS, 0, 7, DFLAGS,
			RK3328_CLKGATE_CON(2), 2, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_uart0_frac", "clk_uart0_div", CLK_SET_RATE_PARENT,
			RK3328_CLKSEL_CON(15), 0,
			RK3328_CLKGATE_CON(1), 15, GFLAGS,
			&rk3328_uart0_fracmux),
	COMPOSITE_FRACMUX(0, "clk_uart1_frac", "clk_uart1_div", CLK_SET_RATE_PARENT,
			RK3328_CLKSEL_CON(17), 0,
			RK3328_CLKGATE_CON(2), 1, GFLAGS,
			&rk3328_uart1_fracmux),
	COMPOSITE_FRACMUX(0, "clk_uart2_frac", "clk_uart2_div", CLK_SET_RATE_PARENT,
			RK3328_CLKSEL_CON(19), 0,
			RK3328_CLKGATE_CON(2), 3, GFLAGS,
			&rk3328_uart2_fracmux),

	/*
	 * Clock-Architecture Diagram 4
	 */

	COMPOSITE(SCLK_I2C0, "clk_i2c0", mux_2plls_p, 0,
			RK3328_CLKSEL_CON(34), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RK3328_CLKGATE_CON(2), 9, GFLAGS),
	COMPOSITE(SCLK_I2C1, "clk_i2c1", mux_2plls_p, 0,
			RK3328_CLKSEL_CON(34), 15, 1, MFLAGS, 8, 7, DFLAGS,
			RK3328_CLKGATE_CON(2), 10, GFLAGS),
	COMPOSITE(SCLK_I2C2, "clk_i2c2", mux_2plls_p, 0,
			RK3328_CLKSEL_CON(35), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RK3328_CLKGATE_CON(2), 11, GFLAGS),
	COMPOSITE(SCLK_I2C3, "clk_i2c3", mux_2plls_p, 0,
			RK3328_CLKSEL_CON(35), 15, 1, MFLAGS, 8, 7, DFLAGS,
			RK3328_CLKGATE_CON(2), 12, GFLAGS),
	COMPOSITE(SCLK_CRYPTO, "clk_crypto", mux_2plls_p, 0,
			RK3328_CLKSEL_CON(20), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RK3328_CLKGATE_CON(2), 4, GFLAGS),
	COMPOSITE_NOMUX(SCLK_TSADC, "clk_tsadc", "clk_24m", 0,
			RK3328_CLKSEL_CON(22), 0, 10, DFLAGS,
			RK3328_CLKGATE_CON(2), 6, GFLAGS),
	COMPOSITE_NOMUX(SCLK_SARADC, "clk_saradc", "clk_24m", 0,
			RK3328_CLKSEL_CON(23), 0, 10, DFLAGS,
			RK3328_CLKGATE_CON(2), 14, GFLAGS),
	COMPOSITE(SCLK_SPI, "clk_spi", mux_2plls_p, 0,
			RK3328_CLKSEL_CON(24), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RK3328_CLKGATE_CON(2), 7, GFLAGS),
	COMPOSITE(SCLK_PWM, "clk_pwm", mux_2plls_p, 0,
			RK3328_CLKSEL_CON(24), 15, 1, MFLAGS, 8, 7, DFLAGS,
			RK3328_CLKGATE_CON(2), 8, GFLAGS),
	COMPOSITE(SCLK_OTP, "clk_otp", mux_2plls_xin24m_p, 0,
			RK3328_CLKSEL_CON(4), 6, 2, MFLAGS, 0, 6, DFLAGS,
			RK3328_CLKGATE_CON(3), 8, GFLAGS),
	COMPOSITE(SCLK_EFUSE, "clk_efuse", mux_2plls_xin24m_p, 0,
			RK3328_CLKSEL_CON(5), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RK3328_CLKGATE_CON(2), 13, GFLAGS),
	COMPOSITE(SCLK_PDM, "clk_pdm", mux_cpll_gpll_apll_p, CLK_SET_RATE_NO_REPARENT | CLK_SET_RATE_PARENT,
			RK3328_CLKSEL_CON(20), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RK3328_CLKGATE_CON(2), 15, GFLAGS),

	GATE(SCLK_TIMER0, "sclk_timer0", "xin24m", 0,
			RK3328_CLKGATE_CON(8), 5, GFLAGS),
	GATE(SCLK_TIMER1, "sclk_timer1", "xin24m", 0,
			RK3328_CLKGATE_CON(8), 6, GFLAGS),
	GATE(SCLK_TIMER2, "sclk_timer2", "xin24m", 0,
			RK3328_CLKGATE_CON(8), 7, GFLAGS),
	GATE(SCLK_TIMER3, "sclk_timer3", "xin24m", 0,
			RK3328_CLKGATE_CON(8), 8, GFLAGS),
	GATE(SCLK_TIMER4, "sclk_timer4", "xin24m", 0,
			RK3328_CLKGATE_CON(8), 9, GFLAGS),
	GATE(SCLK_TIMER5, "sclk_timer5", "xin24m", 0,
			RK3328_CLKGATE_CON(8), 10, GFLAGS),

	COMPOSITE(SCLK_WIFI, "clk_wifi", mux_2plls_u480m_p, 0,
			RK3328_CLKSEL_CON(52), 6, 2, MFLAGS, 0, 6, DFLAGS,
			RK3328_CLKGATE_CON(0), 10, GFLAGS),

	/*
	 * Clock-Architecture Diagram 5
	 */

	/* PD_VIDEO */
	COMPOSITE(ACLK_RKVDEC_PRE, "aclk_rkvdec_pre", mux_4plls_p, 0,
			RK3328_CLKSEL_CON(48), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3328_CLKGATE_CON(6), 0, GFLAGS),
	FACTOR_GATE(HCLK_RKVDEC_PRE, "hclk_rkvdec_pre", "aclk_rkvdec_pre", 0, 1, 4,
			RK3328_CLKGATE_CON(11), 0, GFLAGS),
	GATE(ACLK_RKVDEC, "aclk_rkvdec", "aclk_rkvdec_pre", CLK_SET_RATE_PARENT,
			RK3328_CLKGATE_CON(24), 0, GFLAGS),
	GATE(HCLK_RKVDEC, "hclk_rkvdec", "hclk_rkvdec_pre", CLK_SET_RATE_PARENT,
			RK3328_CLKGATE_CON(24), 1, GFLAGS),
	GATE(0, "aclk_rkvdec_niu", "aclk_rkvdec_pre", CLK_IS_CRITICAL,
			RK3328_CLKGATE_CON(24), 2, GFLAGS),
	GATE(0, "hclk_rkvdec_niu", "hclk_rkvdec_pre", CLK_IS_CRITICAL,
			RK3328_CLKGATE_CON(24), 3, GFLAGS),

	COMPOSITE(SCLK_VDEC_CABAC, "sclk_vdec_cabac", mux_4plls_p, 0,
			RK3328_CLKSEL_CON(48), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RK3328_CLKGATE_CON(6), 1, GFLAGS),

	COMPOSITE(SCLK_VDEC_CORE, "sclk_vdec_core", mux_4plls_p, 0,
			RK3328_CLKSEL_CON(49), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3328_CLKGATE_CON(6), 2, GFLAGS),

	COMPOSITE(ACLK_VPU_PRE, "aclk_vpu_pre", mux_4plls_p, 0,
			RK3328_CLKSEL_CON(50), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3328_CLKGATE_CON(6), 5, GFLAGS),
	FACTOR_GATE(HCLK_VPU_PRE, "hclk_vpu_pre", "aclk_vpu_pre", 0, 1, 4,
			RK3328_CLKGATE_CON(11), 8, GFLAGS),
	GATE(ACLK_VPU, "aclk_vpu", "aclk_vpu_pre", CLK_SET_RATE_PARENT,
			RK3328_CLKGATE_CON(23), 0, GFLAGS),
	GATE(HCLK_VPU, "hclk_vpu", "hclk_vpu_pre", CLK_SET_RATE_PARENT,
			RK3328_CLKGATE_CON(23), 1, GFLAGS),
	GATE(0, "aclk_vpu_niu", "aclk_vpu_pre", CLK_IS_CRITICAL,
			RK3328_CLKGATE_CON(23), 2, GFLAGS),
	GATE(0, "hclk_vpu_niu", "hclk_vpu_pre", CLK_IS_CRITICAL,
			RK3328_CLKGATE_CON(23), 3, GFLAGS),

	COMPOSITE(ACLK_RKVENC, "aclk_rkvenc", mux_4plls_p, 0,
			RK3328_CLKSEL_CON(51), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3328_CLKGATE_CON(6), 3, GFLAGS),

	COMPOSITE(SCLK_VENC_CORE, "sclk_venc_core", mux_4plls_p, 0,
			RK3328_CLKSEL_CON(51), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RK3328_CLKGATE_CON(6), 4, GFLAGS),
	FACTOR_GATE(0, "hclk_venc", "sclk_venc_core", 0, 1, 4,
			RK3328_CLKGATE_CON(11), 4, GFLAGS),

	GATE(0, "aclk_rkvenc_niu", "sclk_venc_core", CLK_IS_CRITICAL,
			RK3328_CLKGATE_CON(25), 0, GFLAGS),
	GATE(0, "hclk_rkvenc_niu", "hclk_venc", CLK_IS_CRITICAL,
			RK3328_CLKGATE_CON(25), 1, GFLAGS),
	GATE(ACLK_H265, "aclk_h265", "sclk_venc_core", 0,
			RK3328_CLKGATE_CON(25), 2, GFLAGS),
	GATE(PCLK_H265, "pclk_h265", "hclk_venc", 0,
			RK3328_CLKGATE_CON(25), 3, GFLAGS),
	GATE(ACLK_H264, "aclk_h264", "sclk_venc_core", 0,
			RK3328_CLKGATE_CON(25), 4, GFLAGS),
	GATE(HCLK_H264, "hclk_h264", "hclk_venc", 0,
			RK3328_CLKGATE_CON(25), 5, GFLAGS),
	GATE(ACLK_AXISRAM, "aclk_axisram", "sclk_venc_core", CLK_IGNORE_UNUSED,
			RK3328_CLKGATE_CON(25), 6, GFLAGS),

	COMPOSITE(SCLK_VENC_DSP, "sclk_venc_dsp", mux_4plls_p, 0,
			RK3328_CLKSEL_CON(52), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RK3328_CLKGATE_CON(6), 7, GFLAGS),

	/*
	 * Clock-Architecture Diagram 6
	 */

	/* PD_VIO */
	COMPOSITE(ACLK_VIO_PRE, "aclk_vio_pre", mux_4plls_p, 0,
			RK3328_CLKSEL_CON(37), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3328_CLKGATE_CON(5), 2, GFLAGS),
	DIV(HCLK_VIO_PRE, "hclk_vio_pre", "aclk_vio_pre", 0,
			RK3328_CLKSEL_CON(37), 8, 5, DFLAGS),

	COMPOSITE(ACLK_RGA_PRE, "aclk_rga_pre", mux_4plls_p, 0,
			RK3328_CLKSEL_CON(36), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RK3328_CLKGATE_CON(5), 0, GFLAGS),
	COMPOSITE(SCLK_RGA, "clk_rga", mux_4plls_p, 0,
			RK3328_CLKSEL_CON(36), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3328_CLKGATE_CON(5), 1, GFLAGS),
	COMPOSITE(ACLK_VOP_PRE, "aclk_vop_pre", mux_4plls_p, 0,
			RK3328_CLKSEL_CON(39), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3328_CLKGATE_CON(5), 5, GFLAGS),
	GATE(SCLK_HDMI_SFC, "sclk_hdmi_sfc", "xin24m", 0,
			RK3328_CLKGATE_CON(5), 4, GFLAGS),

	COMPOSITE_NODIV(0, "clk_cif_src", mux_2plls_p, 0,
			RK3328_CLKSEL_CON(42), 7, 1, MFLAGS,
			RK3328_CLKGATE_CON(5), 3, GFLAGS),
	COMPOSITE_NOGATE(SCLK_CIF_OUT, "clk_cif_out", mux_sclk_cif_p, CLK_SET_RATE_PARENT,
			RK3328_CLKSEL_CON(42), 5, 1, MFLAGS, 0, 5, DFLAGS),

	COMPOSITE(DCLK_LCDC_SRC, "dclk_lcdc_src", mux_gpll_cpll_p, 0,
			RK3328_CLKSEL_CON(40), 0, 1, MFLAGS, 8, 8, DFLAGS,
			RK3328_CLKGATE_CON(5), 6, GFLAGS),
	DIV(DCLK_HDMIPHY, "dclk_hdmiphy", "dclk_lcdc_src", 0,
			RK3328_CLKSEL_CON(40), 3, 3, DFLAGS),
	MUX(DCLK_LCDC, "dclk_lcdc", mux_dclk_lcdc_p,  CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			RK3328_CLKSEL_CON(40), 1, 1, MFLAGS),

	/*
	 * Clock-Architecture Diagram 7
	 */

	/* PD_PERI */
	GATE(0, "gpll_peri", "gpll", CLK_IS_CRITICAL,
			RK3328_CLKGATE_CON(4), 0, GFLAGS),
	GATE(0, "cpll_peri", "cpll", CLK_IS_CRITICAL,
			RK3328_CLKGATE_CON(4), 1, GFLAGS),
	GATE(0, "hdmiphy_peri", "hdmiphy", CLK_IS_CRITICAL,
			RK3328_CLKGATE_CON(4), 2, GFLAGS),
	COMPOSITE_NOGATE(ACLK_PERI_PRE, "aclk_peri_pre", mux_aclk_peri_pre_p, CLK_IS_CRITICAL,
			RK3328_CLKSEL_CON(28), 6, 2, MFLAGS, 0, 5, DFLAGS),
	COMPOSITE_NOMUX(PCLK_PERI, "pclk_peri", "aclk_peri_pre", CLK_IS_CRITICAL,
			RK3328_CLKSEL_CON(29), 0, 2, DFLAGS,
			RK3328_CLKGATE_CON(10), 2, GFLAGS),
	COMPOSITE_NOMUX(HCLK_PERI, "hclk_peri", "aclk_peri_pre", CLK_IS_CRITICAL,
			RK3328_CLKSEL_CON(29), 4, 3, DFLAGS,
			RK3328_CLKGATE_CON(10), 1, GFLAGS),
	GATE(ACLK_PERI, "aclk_peri", "aclk_peri_pre", CLK_IS_CRITICAL | CLK_SET_RATE_PARENT,
			RK3328_CLKGATE_CON(10), 0, GFLAGS),

	COMPOSITE(SCLK_SDMMC, "clk_sdmmc", mux_2plls_24m_u480m_p, 0,
			RK3328_CLKSEL_CON(30), 8, 2, MFLAGS, 0, 8, DFLAGS,
			RK3328_CLKGATE_CON(4), 3, GFLAGS),

	COMPOSITE(SCLK_SDIO, "clk_sdio", mux_2plls_24m_u480m_p, 0,
			RK3328_CLKSEL_CON(31), 8, 2, MFLAGS, 0, 8, DFLAGS,
			RK3328_CLKGATE_CON(4), 4, GFLAGS),

	COMPOSITE(SCLK_EMMC, "clk_emmc", mux_2plls_24m_u480m_p, 0,
			RK3328_CLKSEL_CON(32), 8, 2, MFLAGS, 0, 8, DFLAGS,
			RK3328_CLKGATE_CON(4), 5, GFLAGS),

	COMPOSITE(SCLK_SDMMC_EXT, "clk_sdmmc_ext", mux_2plls_24m_u480m_p, 0,
			RK3328_CLKSEL_CON(43), 8, 2, MFLAGS, 0, 8, DFLAGS,
			RK3328_CLKGATE_CON(4), 10, GFLAGS),

	COMPOSITE(SCLK_REF_USB3OTG_SRC, "clk_ref_usb3otg_src", mux_2plls_p, 0,
			RK3328_CLKSEL_CON(45), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RK3328_CLKGATE_CON(4), 9, GFLAGS),

	MUX(SCLK_REF_USB3OTG, "clk_ref_usb3otg", mux_ref_usb3otg_src_p, CLK_SET_RATE_PARENT,
			RK3328_CLKSEL_CON(45), 8, 1, MFLAGS),

	GATE(SCLK_USB3OTG_REF, "clk_usb3otg_ref", "xin24m", 0,
			RK3328_CLKGATE_CON(4), 7, GFLAGS),

	COMPOSITE(SCLK_USB3OTG_SUSPEND, "clk_usb3otg_suspend", mux_xin24m_32k_p, 0,
			RK3328_CLKSEL_CON(33), 15, 1, MFLAGS, 0, 10, DFLAGS,
			RK3328_CLKGATE_CON(4), 8, GFLAGS),

	/*
	 * Clock-Architecture Diagram 8
	 */

	/* PD_GMAC */
	COMPOSITE(ACLK_GMAC, "aclk_gmac", mux_2plls_hdmiphy_p, 0,
			RK3328_CLKSEL_CON(25), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3328_CLKGATE_CON(3), 2, GFLAGS),
	COMPOSITE_NOMUX(PCLK_GMAC, "pclk_gmac", "aclk_gmac", 0,
			RK3328_CLKSEL_CON(25), 8, 3, DFLAGS,
			RK3328_CLKGATE_CON(9), 0, GFLAGS),

	COMPOSITE(SCLK_MAC2IO_SRC, "clk_mac2io_src", mux_2plls_p, 0,
			RK3328_CLKSEL_CON(27), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RK3328_CLKGATE_CON(3), 1, GFLAGS),
	GATE(SCLK_MAC2IO_REF, "clk_mac2io_ref", "clk_mac2io", 0,
			RK3328_CLKGATE_CON(9), 7, GFLAGS),
	GATE(SCLK_MAC2IO_RX, "clk_mac2io_rx", "clk_mac2io", 0,
			RK3328_CLKGATE_CON(9), 4, GFLAGS),
	GATE(SCLK_MAC2IO_TX, "clk_mac2io_tx", "clk_mac2io", 0,
			RK3328_CLKGATE_CON(9), 5, GFLAGS),
	GATE(SCLK_MAC2IO_REFOUT, "clk_mac2io_refout", "clk_mac2io", 0,
			RK3328_CLKGATE_CON(9), 6, GFLAGS),
	COMPOSITE(SCLK_MAC2IO_OUT, "clk_mac2io_out", mux_2plls_p, 0,
			RK3328_CLKSEL_CON(27), 15, 1, MFLAGS, 8, 5, DFLAGS,
			RK3328_CLKGATE_CON(3), 5, GFLAGS),
	MUXGRF(SCLK_MAC2IO, "clk_mac2io", mux_mac2io_src_p, CLK_SET_RATE_NO_REPARENT,
			RK3328_GRF_MAC_CON1, 10, 1, MFLAGS),
	MUXGRF(SCLK_MAC2IO_EXT, "clk_mac2io_ext", mux_mac2io_ext_p, CLK_SET_RATE_NO_REPARENT,
			RK3328_GRF_SOC_CON4, 14, 1, MFLAGS),

	COMPOSITE(SCLK_MAC2PHY_SRC, "clk_mac2phy_src", mux_2plls_p, 0,
			RK3328_CLKSEL_CON(26), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RK3328_CLKGATE_CON(3), 0, GFLAGS),
	GATE(SCLK_MAC2PHY_REF, "clk_mac2phy_ref", "clk_mac2phy", 0,
			RK3328_CLKGATE_CON(9), 3, GFLAGS),
	GATE(SCLK_MAC2PHY_RXTX, "clk_mac2phy_rxtx", "clk_mac2phy", 0,
			RK3328_CLKGATE_CON(9), 1, GFLAGS),
	COMPOSITE_NOMUX(SCLK_MAC2PHY_OUT, "clk_mac2phy_out", "clk_mac2phy", 0,
			RK3328_CLKSEL_CON(26), 8, 2, DFLAGS,
			RK3328_CLKGATE_CON(9), 2, GFLAGS),
	MUXGRF(SCLK_MAC2PHY, "clk_mac2phy", mux_mac2phy_src_p, CLK_SET_RATE_NO_REPARENT,
			RK3328_GRF_MAC_CON2, 10, 1, MFLAGS),

	FACTOR(0, "xin12m", "xin24m", 0, 1, 2),

	/*
	 * Clock-Architecture Diagram 9
	 */

	/* PD_VOP */
	GATE(ACLK_RGA, "aclk_rga", "aclk_rga_pre", 0, RK3328_CLKGATE_CON(21), 10, GFLAGS),
	GATE(0, "aclk_rga_niu", "aclk_rga_pre", CLK_IS_CRITICAL, RK3328_CLKGATE_CON(22), 3, GFLAGS),
	GATE(ACLK_VOP, "aclk_vop", "aclk_vop_pre", 0, RK3328_CLKGATE_CON(21), 2, GFLAGS),
	GATE(0, "aclk_vop_niu", "aclk_vop_pre", CLK_IS_CRITICAL, RK3328_CLKGATE_CON(21), 4, GFLAGS),

	GATE(ACLK_IEP, "aclk_iep", "aclk_vio_pre", 0, RK3328_CLKGATE_CON(21), 6, GFLAGS),
	GATE(ACLK_CIF, "aclk_cif", "aclk_vio_pre", 0, RK3328_CLKGATE_CON(21), 8, GFLAGS),
	GATE(ACLK_HDCP, "aclk_hdcp", "aclk_vio_pre", 0, RK3328_CLKGATE_CON(21), 15, GFLAGS),
	GATE(0, "aclk_vio_niu", "aclk_vio_pre", CLK_IS_CRITICAL, RK3328_CLKGATE_CON(22), 2, GFLAGS),

	GATE(HCLK_VOP, "hclk_vop", "hclk_vio_pre", 0, RK3328_CLKGATE_CON(21), 3, GFLAGS),
	GATE(0, "hclk_vop_niu", "hclk_vio_pre", CLK_IS_CRITICAL, RK3328_CLKGATE_CON(21), 5, GFLAGS),
	GATE(HCLK_IEP, "hclk_iep", "hclk_vio_pre", 0, RK3328_CLKGATE_CON(21), 7, GFLAGS),
	GATE(HCLK_CIF, "hclk_cif", "hclk_vio_pre", 0, RK3328_CLKGATE_CON(21), 9, GFLAGS),
	GATE(HCLK_RGA, "hclk_rga", "hclk_vio_pre", 0, RK3328_CLKGATE_CON(21), 11, GFLAGS),
	GATE(0, "hclk_ahb1tom", "hclk_vio_pre", CLK_IS_CRITICAL, RK3328_CLKGATE_CON(21), 12, GFLAGS),
	GATE(0, "pclk_vio_h2p", "hclk_vio_pre", CLK_IS_CRITICAL, RK3328_CLKGATE_CON(21), 13, GFLAGS),
	GATE(0, "hclk_vio_h2p", "hclk_vio_pre", CLK_IS_CRITICAL, RK3328_CLKGATE_CON(21), 14, GFLAGS),
	GATE(HCLK_HDCP, "hclk_hdcp", "hclk_vio_pre", 0, RK3328_CLKGATE_CON(22), 0, GFLAGS),
	GATE(0, "hclk_vio_niu", "hclk_vio_pre", CLK_IS_CRITICAL, RK3328_CLKGATE_CON(22), 1, GFLAGS),
	GATE(PCLK_HDMI, "pclk_hdmi", "hclk_vio_pre", 0, RK3328_CLKGATE_CON(22), 4, GFLAGS),
	GATE(PCLK_HDCP, "pclk_hdcp", "hclk_vio_pre", 0, RK3328_CLKGATE_CON(22), 5, GFLAGS),

	/* PD_PERI */
	GATE(0, "aclk_peri_noc", "aclk_peri", CLK_IS_CRITICAL, RK3328_CLKGATE_CON(19), 11, GFLAGS),
	GATE(ACLK_USB3OTG, "aclk_usb3otg", "aclk_peri", 0, RK3328_CLKGATE_CON(19), 14, GFLAGS),

	GATE(HCLK_SDMMC, "hclk_sdmmc", "hclk_peri", 0, RK3328_CLKGATE_CON(19), 0, GFLAGS),
	GATE(HCLK_SDIO, "hclk_sdio", "hclk_peri", 0, RK3328_CLKGATE_CON(19), 1, GFLAGS),
	GATE(HCLK_EMMC, "hclk_emmc", "hclk_peri", 0, RK3328_CLKGATE_CON(19), 2, GFLAGS),
	GATE(HCLK_SDMMC_EXT, "hclk_sdmmc_ext", "hclk_peri", 0, RK3328_CLKGATE_CON(19), 15, GFLAGS),
	GATE(HCLK_HOST0, "hclk_host0", "hclk_peri", 0, RK3328_CLKGATE_CON(19), 6, GFLAGS),
	GATE(HCLK_HOST0_ARB, "hclk_host0_arb", "hclk_peri", CLK_IGNORE_UNUSED, RK3328_CLKGATE_CON(19), 7, GFLAGS),
	GATE(HCLK_OTG, "hclk_otg", "hclk_peri", 0, RK3328_CLKGATE_CON(19), 8, GFLAGS),
	GATE(HCLK_OTG_PMU, "hclk_otg_pmu", "hclk_peri", CLK_IS_CRITICAL, RK3328_CLKGATE_CON(19), 9, GFLAGS),
	GATE(0, "hclk_peri_niu", "hclk_peri", CLK_IS_CRITICAL, RK3328_CLKGATE_CON(19), 12, GFLAGS),
	GATE(0, "pclk_peri_niu", "hclk_peri", CLK_IS_CRITICAL, RK3328_CLKGATE_CON(19), 13, GFLAGS),

	/* PD_GMAC */
	GATE(ACLK_MAC2PHY, "aclk_mac2phy", "aclk_gmac", 0, RK3328_CLKGATE_CON(26), 0, GFLAGS),
	GATE(ACLK_MAC2IO, "aclk_mac2io", "aclk_gmac", 0, RK3328_CLKGATE_CON(26), 2, GFLAGS),
	GATE(0, "aclk_gmac_niu", "aclk_gmac", CLK_IS_CRITICAL, RK3328_CLKGATE_CON(26), 4, GFLAGS),
	GATE(PCLK_MAC2PHY, "pclk_mac2phy", "pclk_gmac", 0, RK3328_CLKGATE_CON(26), 1, GFLAGS),
	GATE(PCLK_MAC2IO, "pclk_mac2io", "pclk_gmac", 0, RK3328_CLKGATE_CON(26), 3, GFLAGS),
	GATE(0, "pclk_gmac_niu", "pclk_gmac", CLK_IS_CRITICAL, RK3328_CLKGATE_CON(26), 5, GFLAGS),

	/* PD_BUS */
	GATE(0, "aclk_bus_niu", "aclk_bus_pre", CLK_IS_CRITICAL, RK3328_CLKGATE_CON(15), 12, GFLAGS),
	GATE(ACLK_DCF, "aclk_dcf", "aclk_bus_pre", 0, RK3328_CLKGATE_CON(15), 11, GFLAGS),
	GATE(ACLK_TSP, "aclk_tsp", "aclk_bus_pre", 0, RK3328_CLKGATE_CON(17), 12, GFLAGS),
	GATE(0, "aclk_intmem", "aclk_bus_pre", CLK_IS_CRITICAL, RK3328_CLKGATE_CON(15), 0, GFLAGS),
	GATE(ACLK_DMAC, "aclk_dmac_bus", "aclk_bus_pre", CLK_IGNORE_UNUSED, RK3328_CLKGATE_CON(15), 1, GFLAGS),

	GATE(0, "hclk_rom", "hclk_bus_pre", CLK_IS_CRITICAL, RK3328_CLKGATE_CON(15), 2, GFLAGS),
	GATE(HCLK_I2S0_8CH, "hclk_i2s0_8ch", "hclk_bus_pre", 0, RK3328_CLKGATE_CON(15), 3, GFLAGS),
	GATE(HCLK_I2S1_8CH, "hclk_i2s1_8ch", "hclk_bus_pre", 0, RK3328_CLKGATE_CON(15), 4, GFLAGS),
	GATE(HCLK_I2S2_2CH, "hclk_i2s2_2ch", "hclk_bus_pre", 0, RK3328_CLKGATE_CON(15), 5, GFLAGS),
	GATE(HCLK_SPDIF_8CH, "hclk_spdif_8ch", "hclk_bus_pre", 0, RK3328_CLKGATE_CON(15), 6, GFLAGS),
	GATE(HCLK_TSP, "hclk_tsp", "hclk_bus_pre", 0, RK3328_CLKGATE_CON(17), 11, GFLAGS),
	GATE(HCLK_CRYPTO_MST, "hclk_crypto_mst", "hclk_bus_pre", 0, RK3328_CLKGATE_CON(15), 7, GFLAGS),
	GATE(HCLK_CRYPTO_SLV, "hclk_crypto_slv", "hclk_bus_pre", 0, RK3328_CLKGATE_CON(15), 8, GFLAGS),
	GATE(0, "hclk_bus_niu", "hclk_bus_pre", CLK_IS_CRITICAL, RK3328_CLKGATE_CON(15), 13, GFLAGS),
	GATE(HCLK_PDM, "hclk_pdm", "hclk_bus_pre", 0, RK3328_CLKGATE_CON(28), 0, GFLAGS),

	GATE(0, "pclk_bus_niu", "pclk_bus", CLK_IS_CRITICAL, RK3328_CLKGATE_CON(15), 14, GFLAGS),
	GATE(0, "pclk_efuse", "pclk_bus", CLK_IGNORE_UNUSED, RK3328_CLKGATE_CON(15), 9, GFLAGS),
	GATE(0, "pclk_otp", "pclk_bus", CLK_IGNORE_UNUSED, RK3328_CLKGATE_CON(28), 4, GFLAGS),
	GATE(PCLK_I2C0, "pclk_i2c0", "pclk_bus", 0, RK3328_CLKGATE_CON(15), 10, GFLAGS),
	GATE(PCLK_I2C1, "pclk_i2c1", "pclk_bus", 0, RK3328_CLKGATE_CON(16), 0, GFLAGS),
	GATE(PCLK_I2C2, "pclk_i2c2", "pclk_bus", 0, RK3328_CLKGATE_CON(16), 1, GFLAGS),
	GATE(PCLK_I2C3, "pclk_i2c3", "pclk_bus", 0, RK3328_CLKGATE_CON(16), 2, GFLAGS),
	GATE(PCLK_TIMER, "pclk_timer0", "pclk_bus", CLK_IS_CRITICAL, RK3328_CLKGATE_CON(16), 3, GFLAGS),
	GATE(0, "pclk_stimer", "pclk_bus", 0, RK3328_CLKGATE_CON(16), 4, GFLAGS),
	GATE(PCLK_SPI, "pclk_spi", "pclk_bus", 0, RK3328_CLKGATE_CON(16), 5, GFLAGS),
	GATE(PCLK_PWM, "pclk_rk_pwm", "pclk_bus", 0, RK3328_CLKGATE_CON(16), 6, GFLAGS),
	GATE(PCLK_GPIO0, "pclk_gpio0", "pclk_bus", 0, RK3328_CLKGATE_CON(16), 7, GFLAGS),
	GATE(PCLK_GPIO1, "pclk_gpio1", "pclk_bus", 0, RK3328_CLKGATE_CON(16), 8, GFLAGS),
	GATE(PCLK_GPIO2, "pclk_gpio2", "pclk_bus", 0, RK3328_CLKGATE_CON(16), 9, GFLAGS),
	GATE(PCLK_GPIO3, "pclk_gpio3", "pclk_bus", 0, RK3328_CLKGATE_CON(16), 10, GFLAGS),
	GATE(PCLK_UART0, "pclk_uart0", "pclk_bus", 0, RK3328_CLKGATE_CON(16), 11, GFLAGS),
	GATE(PCLK_UART1, "pclk_uart1", "pclk_bus", 0, RK3328_CLKGATE_CON(16), 12, GFLAGS),
	GATE(PCLK_UART2, "pclk_uart2", "pclk_bus", 0, RK3328_CLKGATE_CON(16), 13, GFLAGS),
	GATE(PCLK_TSADC, "pclk_tsadc", "pclk_bus", 0, RK3328_CLKGATE_CON(16), 14, GFLAGS),
	GATE(PCLK_DCF, "pclk_dcf", "pclk_bus", 0, RK3328_CLKGATE_CON(16), 15, GFLAGS),
	GATE(PCLK_GRF, "pclk_grf", "pclk_bus", CLK_IS_CRITICAL, RK3328_CLKGATE_CON(17), 0, GFLAGS),
	GATE(0, "pclk_cru", "pclk_bus", CLK_IS_CRITICAL, RK3328_CLKGATE_CON(17), 4, GFLAGS),
	GATE(0, "pclk_sgrf", "pclk_bus", CLK_IS_CRITICAL, RK3328_CLKGATE_CON(17), 6, GFLAGS),
	GATE(0, "pclk_sim", "pclk_bus", CLK_IGNORE_UNUSED, RK3328_CLKGATE_CON(17), 10, GFLAGS),
	GATE(PCLK_SARADC, "pclk_saradc", "pclk_bus", 0, RK3328_CLKGATE_CON(17), 15, GFLAGS),
	GATE(0, "pclk_pmu", "pclk_bus", CLK_IS_CRITICAL, RK3328_CLKGATE_CON(28), 3, GFLAGS),

	/* Watchdog pclk is controlled from the secure GRF */
	SGRF_GATE(PCLK_WDT, "pclk_wdt", "pclk_bus"),

	GATE(PCLK_USB3PHY_OTG, "pclk_usb3phy_otg", "pclk_phy_pre", 0, RK3328_CLKGATE_CON(28), 1, GFLAGS),
	GATE(PCLK_USB3PHY_PIPE, "pclk_usb3phy_pipe", "pclk_phy_pre", 0, RK3328_CLKGATE_CON(28), 2, GFLAGS),
	GATE(PCLK_USB3_GRF, "pclk_usb3_grf", "pclk_phy_pre", CLK_IGNORE_UNUSED, RK3328_CLKGATE_CON(17), 2, GFLAGS),
	GATE(PCLK_USB2_GRF, "pclk_usb2_grf", "pclk_phy_pre", CLK_IGNORE_UNUSED, RK3328_CLKGATE_CON(17), 14, GFLAGS),
	GATE(0, "pclk_ddrphy", "pclk_phy_pre", CLK_IS_CRITICAL, RK3328_CLKGATE_CON(17), 13, GFLAGS),
	GATE(PCLK_ACODECPHY, "pclk_acodecphy", "pclk_phy_pre", 0, RK3328_CLKGATE_CON(17), 5, GFLAGS),
	GATE(PCLK_HDMIPHY, "pclk_hdmiphy", "pclk_phy_pre", CLK_IGNORE_UNUSED, RK3328_CLKGATE_CON(17), 7, GFLAGS),
	GATE(0, "pclk_vdacphy", "pclk_phy_pre", CLK_IGNORE_UNUSED, RK3328_CLKGATE_CON(17), 8, GFLAGS),
	GATE(0, "pclk_phy_niu", "pclk_phy_pre", CLK_IS_CRITICAL, RK3328_CLKGATE_CON(15), 15, GFLAGS),

	/* PD_MMC */
	MMC(SCLK_SDMMC_DRV, "sdmmc_drv", "clk_sdmmc",
	    RK3328_SDMMC_CON0, 1),
	MMC(SCLK_SDMMC_SAMPLE, "sdmmc_sample", "clk_sdmmc",
	    RK3328_SDMMC_CON1, 1),

	MMC(SCLK_SDIO_DRV, "sdio_drv", "clk_sdio",
	    RK3328_SDIO_CON0, 1),
	MMC(SCLK_SDIO_SAMPLE, "sdio_sample", "clk_sdio",
	    RK3328_SDIO_CON1, 1),

	MMC(SCLK_EMMC_DRV, "emmc_drv", "clk_emmc",
	    RK3328_EMMC_CON0, 1),
	MMC(SCLK_EMMC_SAMPLE, "emmc_sample", "clk_emmc",
	    RK3328_EMMC_CON1, 1),

	MMC(SCLK_SDMMC_EXT_DRV, "sdmmc_ext_drv", "clk_sdmmc_ext",
	    RK3328_SDMMC_EXT_CON0, 1),
	MMC(SCLK_SDMMC_EXT_SAMPLE, "sdmmc_ext_sample", "clk_sdmmc_ext",
	    RK3328_SDMMC_EXT_CON1, 1),
};

static void __init rk3328_clk_init(struct device_node *np)
{
	struct rockchip_clk_provider *ctx;
	void __iomem *reg_base;
	struct clk **clks;

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
	clks = ctx->clk_data.clks;

	rockchip_clk_register_plls(ctx, rk3328_pll_clks,
				   ARRAY_SIZE(rk3328_pll_clks),
				   RK3328_GRF_SOC_STATUS0);
	rockchip_clk_register_branches(ctx, rk3328_clk_branches,
				       ARRAY_SIZE(rk3328_clk_branches));

	rockchip_clk_register_armclk(ctx, ARMCLK, "armclk",
				     4, clks[PLL_APLL], clks[PLL_GPLL],
				     &rk3328_cpuclk_data, rk3328_cpuclk_rates,
				     ARRAY_SIZE(rk3328_cpuclk_rates));

	rockchip_register_softrst(np, 12, reg_base + RK3328_SOFTRST_CON(0),
				  ROCKCHIP_SOFTRST_HIWORD_MASK);

	rockchip_register_restart_notifier(ctx, RK3328_GLB_SRST_FST, NULL);

	rockchip_clk_of_add_provider(np, ctx);
}
CLK_OF_DECLARE(rk3328_cru, "rockchip,rk3328-cru", rk3328_clk_init);

static int __init clk_rk3328_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;

	rk3328_clk_init(np);

	return 0;
}

static const struct of_device_id clk_rk3328_match_table[] = {
	{
		.compatible = "rockchip,rk3328-cru",
	},
	{ }
};
MODULE_DEVICE_TABLE(of, clk_rk3328_match_table);

static struct platform_driver clk_rk3328_driver = {
	.driver		= {
		.name	= "clk-rk3328",
		.of_match_table = clk_rk3328_match_table,
	},
};
builtin_platform_driver_probe(clk_rk3328_driver, clk_rk3328_probe);

MODULE_DESCRIPTION("Rockchip RK3328 Clock Driver");
MODULE_LICENSE("GPL");
