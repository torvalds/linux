// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2015 Rockchip Electronics Co. Ltd.
 * Author: Xing Zheng <zhengxing@rock-chips.com>
 *         Jeffy Chen <jeffy.chen@rock-chips.com>
 */

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/syscore_ops.h>
#include <dt-bindings/clock/rk3228-cru.h>
#include "clk.h"

#define RK3228_GRF_SOC_STATUS0	0x480

enum rk3228_plls {
	apll, dpll, cpll, gpll,
};

static struct rockchip_pll_rate_table rk3228_pll_rates[] = {
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

#define RK3228_DIV_CPU_MASK		0x1f
#define RK3228_DIV_CPU_SHIFT		8

#define RK3228_DIV_PERI_MASK		0xf
#define RK3228_DIV_PERI_SHIFT		0
#define RK3228_DIV_ACLK_MASK		0x7
#define RK3228_DIV_ACLK_SHIFT		4
#define RK3228_DIV_HCLK_MASK		0x3
#define RK3228_DIV_HCLK_SHIFT		8
#define RK3228_DIV_PCLK_MASK		0x7
#define RK3228_DIV_PCLK_SHIFT		12

#define RK3228_CLKSEL1(_core_aclk_div, _core_peri_div)				\
	{									\
		.reg = RK2928_CLKSEL_CON(1),					\
		.val = HIWORD_UPDATE(_core_peri_div, RK3228_DIV_PERI_MASK,	\
				     RK3228_DIV_PERI_SHIFT) |			\
		       HIWORD_UPDATE(_core_aclk_div, RK3228_DIV_ACLK_MASK,	\
				     RK3228_DIV_ACLK_SHIFT),			\
}

#define RK3228_CPUCLK_RATE(_prate, _core_aclk_div, _core_peri_div)		\
	{									\
		.prate = _prate,						\
		.divs = {							\
			RK3228_CLKSEL1(_core_aclk_div, _core_peri_div),		\
		},								\
	}

static struct rockchip_cpuclk_rate_table rk3228_cpuclk_rates[] __initdata = {
	RK3228_CPUCLK_RATE(1800000000, 1, 7),
	RK3228_CPUCLK_RATE(1704000000, 1, 7),
	RK3228_CPUCLK_RATE(1608000000, 1, 7),
	RK3228_CPUCLK_RATE(1512000000, 1, 7),
	RK3228_CPUCLK_RATE(1488000000, 1, 5),
	RK3228_CPUCLK_RATE(1464000000, 1, 5),
	RK3228_CPUCLK_RATE(1416000000, 1, 5),
	RK3228_CPUCLK_RATE(1392000000, 1, 5),
	RK3228_CPUCLK_RATE(1296000000, 1, 5),
	RK3228_CPUCLK_RATE(1200000000, 1, 5),
	RK3228_CPUCLK_RATE(1104000000, 1, 5),
	RK3228_CPUCLK_RATE(1008000000, 1, 5),
	RK3228_CPUCLK_RATE(912000000, 1, 5),
	RK3228_CPUCLK_RATE(816000000, 1, 3),
	RK3228_CPUCLK_RATE(696000000, 1, 3),
	RK3228_CPUCLK_RATE(600000000, 1, 3),
	RK3228_CPUCLK_RATE(408000000, 1, 1),
	RK3228_CPUCLK_RATE(312000000, 1, 1),
	RK3228_CPUCLK_RATE(216000000,  1, 1),
	RK3228_CPUCLK_RATE(96000000, 1, 1),
};

static const struct rockchip_cpuclk_reg_data rk3228_cpuclk_data = {
	.core_reg = RK2928_CLKSEL_CON(0),
	.div_core_shift = 0,
	.div_core_mask = 0x1f,
	.mux_core_alt = 1,
	.mux_core_main = 0,
	.mux_core_shift = 6,
	.mux_core_mask = 0x1,
};

PNAME(mux_pll_p)		= { "clk_24m", "xin24m" };

PNAME(mux_ddrphy_p)		= { "dpll_ddr", "gpll_ddr", "apll_ddr" };
PNAME(mux_armclk_p)		= { "apll_core", "gpll_core", "dpll_core" };
PNAME(mux_usb480m_phy_p)	= { "usb480m_phy0", "usb480m_phy1" };
PNAME(mux_usb480m_p)		= { "usb480m_phy", "xin24m" };
PNAME(mux_hdmiphy_p)		= { "hdmiphy_phy", "xin24m" };
PNAME(mux_aclk_cpu_src_p)	= { "cpll_aclk_cpu", "gpll_aclk_cpu", "hdmiphy_aclk_cpu" };

PNAME(mux_pll_src_4plls_p)	= { "cpll", "gpll", "hdmiphy", "usb480m" };
PNAME(mux_pll_src_3plls_p)	= { "cpll", "gpll", "hdmiphy" };
PNAME(mux_pll_src_2plls_p)	= { "cpll", "gpll" };
PNAME(mux_sclk_hdmi_cec_p)	= { "cpll", "gpll", "xin24m" };
PNAME(mux_aclk_peri_src_p)	= { "cpll_peri", "gpll_peri", "hdmiphy_peri" };
PNAME(mux_mmc_src_p)		= { "cpll", "gpll", "xin24m", "usb480m" };
PNAME(mux_pll_src_cpll_gpll_usb480m_p)	= { "cpll", "gpll", "usb480m" };

PNAME(mux_sclk_rga_p)		= { "gpll", "cpll", "sclk_rga_src" };

PNAME(mux_sclk_vop_src_p)	= { "gpll_vop", "cpll_vop" };
PNAME(mux_dclk_vop_p)		= { "hdmiphy", "sclk_vop_pre" };

PNAME(mux_i2s0_p)		= { "i2s0_src", "i2s0_frac", "ext_i2s", "xin12m" };
PNAME(mux_i2s1_pre_p)		= { "i2s1_src", "i2s1_frac", "ext_i2s", "xin12m" };
PNAME(mux_i2s_out_p)		= { "i2s1_pre", "xin12m" };
PNAME(mux_i2s2_p)		= { "i2s2_src", "i2s2_frac", "xin12m" };
PNAME(mux_sclk_spdif_p)		= { "sclk_spdif_src", "spdif_frac", "xin12m" };

PNAME(mux_uart0_p)		= { "uart0_src", "uart0_frac", "xin24m" };
PNAME(mux_uart1_p)		= { "uart1_src", "uart1_frac", "xin24m" };
PNAME(mux_uart2_p)		= { "uart2_src", "uart2_frac", "xin24m" };

PNAME(mux_sclk_mac_extclk_p)	= { "ext_gmac", "phy_50m_out" };
PNAME(mux_sclk_gmac_pre_p)	= { "sclk_gmac_src", "sclk_mac_extclk" };
PNAME(mux_sclk_macphy_p)	= { "sclk_gmac_src", "ext_gmac" };

static struct rockchip_pll_clock rk3228_pll_clks[] __initdata = {
	[apll] = PLL(pll_rk3036, PLL_APLL, "apll", mux_pll_p, 0, RK2928_PLL_CON(0),
		     RK2928_MODE_CON, 0, 7, 0, rk3228_pll_rates),
	[dpll] = PLL(pll_rk3036, PLL_DPLL, "dpll", mux_pll_p, 0, RK2928_PLL_CON(3),
		     RK2928_MODE_CON, 4, 6, 0, NULL),
	[cpll] = PLL(pll_rk3036, PLL_CPLL, "cpll", mux_pll_p, 0, RK2928_PLL_CON(6),
		     RK2928_MODE_CON, 8, 8, 0, NULL),
	[gpll] = PLL(pll_rk3036, PLL_GPLL, "gpll", mux_pll_p, 0, RK2928_PLL_CON(9),
		     RK2928_MODE_CON, 12, 9, ROCKCHIP_PLL_SYNC_RATE, rk3228_pll_rates),
};

#define MFLAGS CLK_MUX_HIWORD_MASK
#define DFLAGS CLK_DIVIDER_HIWORD_MASK
#define GFLAGS (CLK_GATE_HIWORD_MASK | CLK_GATE_SET_TO_DISABLE)

static struct rockchip_clk_branch rk3228_i2s0_fracmux __initdata =
	MUX(0, "i2s0_pre", mux_i2s0_p, CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(9), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3228_i2s1_fracmux __initdata =
	MUX(0, "i2s1_pre", mux_i2s1_pre_p, CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(3), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3228_i2s2_fracmux __initdata =
	MUX(0, "i2s2_pre", mux_i2s2_p, CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(16), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3228_spdif_fracmux __initdata =
	MUX(SCLK_SPDIF, "sclk_spdif", mux_sclk_spdif_p, CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(6), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3228_uart0_fracmux __initdata =
	MUX(SCLK_UART0, "sclk_uart0", mux_uart0_p, CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(13), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3228_uart1_fracmux __initdata =
	MUX(SCLK_UART1, "sclk_uart1", mux_uart1_p, CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(14), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3228_uart2_fracmux __initdata =
	MUX(SCLK_UART2, "sclk_uart2", mux_uart2_p, CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(15), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3228_clk_branches[] __initdata = {
	/*
	 * Clock-Architecture Diagram 1
	 */

	DIV(0, "clk_24m", "xin24m", CLK_IGNORE_UNUSED,
			RK2928_CLKSEL_CON(4), 8, 5, DFLAGS),

	/* PD_DDR */
	GATE(0, "apll_ddr", "apll", CLK_IGNORE_UNUSED,
			RK2928_CLKGATE_CON(0), 2, GFLAGS),
	GATE(0, "dpll_ddr", "dpll", CLK_IGNORE_UNUSED,
			RK2928_CLKGATE_CON(0), 2, GFLAGS),
	GATE(0, "gpll_ddr", "gpll", CLK_IGNORE_UNUSED,
			RK2928_CLKGATE_CON(0), 2, GFLAGS),
	COMPOSITE(0, "ddrphy4x", mux_ddrphy_p, CLK_IGNORE_UNUSED,
			RK2928_CLKSEL_CON(26), 8, 2, MFLAGS, 0, 3, DFLAGS | CLK_DIVIDER_POWER_OF_TWO,
			RK2928_CLKGATE_CON(7), 1, GFLAGS),
	GATE(0, "ddrc", "ddrphy_pre", CLK_IGNORE_UNUSED,
			RK2928_CLKGATE_CON(8), 5, GFLAGS),
	FACTOR_GATE(0, "ddrphy", "ddrphy4x", CLK_IGNORE_UNUSED, 1, 4,
			RK2928_CLKGATE_CON(7), 0, GFLAGS),

	/* PD_CORE */
	GATE(0, "dpll_core", "dpll", CLK_IGNORE_UNUSED,
			RK2928_CLKGATE_CON(0), 6, GFLAGS),
	GATE(0, "apll_core", "apll", CLK_IGNORE_UNUSED,
			RK2928_CLKGATE_CON(0), 6, GFLAGS),
	GATE(0, "gpll_core", "gpll", CLK_IGNORE_UNUSED,
			RK2928_CLKGATE_CON(0), 6, GFLAGS),
	COMPOSITE_NOMUX(0, "pclk_dbg", "armclk", CLK_IGNORE_UNUSED,
			RK2928_CLKSEL_CON(1), 0, 4, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RK2928_CLKGATE_CON(4), 1, GFLAGS),
	COMPOSITE_NOMUX(0, "armcore", "armclk", CLK_IGNORE_UNUSED,
			RK2928_CLKSEL_CON(1), 4, 3, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RK2928_CLKGATE_CON(4), 0, GFLAGS),

	/* PD_MISC */
	MUX(SCLK_HDMI_PHY, "hdmiphy", mux_hdmiphy_p, CLK_SET_RATE_PARENT,
			RK2928_MISC_CON, 13, 1, MFLAGS),
	MUX(0, "usb480m_phy", mux_usb480m_phy_p, CLK_SET_RATE_PARENT,
			RK2928_MISC_CON, 14, 1, MFLAGS),
	MUX(0, "usb480m", mux_usb480m_p, CLK_SET_RATE_PARENT,
			RK2928_MISC_CON, 15, 1, MFLAGS),

	/* PD_BUS */
	GATE(0, "hdmiphy_aclk_cpu", "hdmiphy", CLK_IGNORE_UNUSED,
			RK2928_CLKGATE_CON(0), 1, GFLAGS),
	GATE(0, "gpll_aclk_cpu", "gpll", CLK_IGNORE_UNUSED,
			RK2928_CLKGATE_CON(0), 1, GFLAGS),
	GATE(0, "cpll_aclk_cpu", "cpll", CLK_IGNORE_UNUSED,
			RK2928_CLKGATE_CON(0), 1, GFLAGS),
	COMPOSITE_NOGATE(0, "aclk_cpu_src", mux_aclk_cpu_src_p, 0,
			RK2928_CLKSEL_CON(0), 13, 2, MFLAGS, 8, 5, DFLAGS),
	GATE(ACLK_CPU, "aclk_cpu", "aclk_cpu_src", 0,
			RK2928_CLKGATE_CON(6), 0, GFLAGS),
	COMPOSITE_NOMUX(HCLK_CPU, "hclk_cpu", "aclk_cpu_src", 0,
			RK2928_CLKSEL_CON(1), 8, 2, DFLAGS,
			RK2928_CLKGATE_CON(6), 1, GFLAGS),
	COMPOSITE_NOMUX(0, "pclk_bus_src", "aclk_cpu_src", 0,
			RK2928_CLKSEL_CON(1), 12, 3, DFLAGS,
			RK2928_CLKGATE_CON(6), 2, GFLAGS),
	GATE(PCLK_CPU, "pclk_cpu", "pclk_bus_src", 0,
			RK2928_CLKGATE_CON(6), 3, GFLAGS),
	GATE(0, "pclk_phy_pre", "pclk_bus_src", 0,
			RK2928_CLKGATE_CON(6), 4, GFLAGS),
	GATE(0, "pclk_ddr_pre", "pclk_bus_src", 0,
			RK2928_CLKGATE_CON(6), 13, GFLAGS),

	/* PD_VIDEO */
	COMPOSITE(ACLK_VPU_PRE, "aclk_vpu_pre", mux_pll_src_4plls_p, 0,
			RK2928_CLKSEL_CON(32), 5, 2, MFLAGS, 0, 5, DFLAGS,
			RK2928_CLKGATE_CON(3), 11, GFLAGS),
	FACTOR_GATE(HCLK_VPU_PRE, "hclk_vpu_pre", "aclk_vpu_pre", 0, 1, 4,
			RK2928_CLKGATE_CON(4), 4, GFLAGS),

	COMPOSITE(ACLK_RKVDEC_PRE, "aclk_rkvdec_pre", mux_pll_src_4plls_p, 0,
			RK2928_CLKSEL_CON(28), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK2928_CLKGATE_CON(3), 2, GFLAGS),
	FACTOR_GATE(HCLK_RKVDEC_PRE, "hclk_rkvdec_pre", "aclk_rkvdec_pre", 0, 1, 4,
			RK2928_CLKGATE_CON(4), 5, GFLAGS),

	COMPOSITE(SCLK_VDEC_CABAC, "sclk_vdec_cabac", mux_pll_src_4plls_p, 0,
			RK2928_CLKSEL_CON(28), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RK2928_CLKGATE_CON(3), 3, GFLAGS),

	COMPOSITE(SCLK_VDEC_CORE, "sclk_vdec_core", mux_pll_src_4plls_p, 0,
			RK2928_CLKSEL_CON(34), 13, 2, MFLAGS, 8, 5, DFLAGS,
			RK2928_CLKGATE_CON(3), 4, GFLAGS),

	/* PD_VIO */
	COMPOSITE(ACLK_IEP_PRE, "aclk_iep_pre", mux_pll_src_4plls_p, 0,
			RK2928_CLKSEL_CON(31), 5, 2, MFLAGS, 0, 5, DFLAGS,
			RK2928_CLKGATE_CON(3), 0, GFLAGS),
	DIV(HCLK_VIO_PRE, "hclk_vio_pre", "aclk_iep_pre", 0,
			RK2928_CLKSEL_CON(2), 0, 5, DFLAGS),

	COMPOSITE(ACLK_HDCP_PRE, "aclk_hdcp_pre", mux_pll_src_4plls_p, 0,
			RK2928_CLKSEL_CON(31), 13, 2, MFLAGS, 8, 5, DFLAGS,
			RK2928_CLKGATE_CON(1), 4, GFLAGS),

	MUX(0, "sclk_rga_src", mux_pll_src_4plls_p, 0,
			RK2928_CLKSEL_CON(33), 13, 2, MFLAGS),
	COMPOSITE_NOMUX(ACLK_RGA_PRE, "aclk_rga_pre", "sclk_rga_src", 0,
			RK2928_CLKSEL_CON(33), 8, 5, DFLAGS,
			RK2928_CLKGATE_CON(1), 2, GFLAGS),
	COMPOSITE(SCLK_RGA, "sclk_rga", mux_sclk_rga_p, 0,
			RK2928_CLKSEL_CON(22), 5, 2, MFLAGS, 0, 5, DFLAGS,
			RK2928_CLKGATE_CON(3), 6, GFLAGS),

	COMPOSITE(ACLK_VOP_PRE, "aclk_vop_pre", mux_pll_src_4plls_p, 0,
			RK2928_CLKSEL_CON(33), 5, 2, MFLAGS, 0, 5, DFLAGS,
			RK2928_CLKGATE_CON(1), 1, GFLAGS),

	COMPOSITE(SCLK_HDCP, "sclk_hdcp", mux_pll_src_3plls_p, 0,
			RK2928_CLKSEL_CON(23), 14, 2, MFLAGS, 8, 6, DFLAGS,
			RK2928_CLKGATE_CON(3), 5, GFLAGS),

	GATE(SCLK_HDMI_HDCP, "sclk_hdmi_hdcp", "xin24m", 0,
			RK2928_CLKGATE_CON(3), 7, GFLAGS),

	COMPOSITE(SCLK_HDMI_CEC, "sclk_hdmi_cec", mux_sclk_hdmi_cec_p, 0,
			RK2928_CLKSEL_CON(21), 14, 2, MFLAGS, 0, 14, DFLAGS,
			RK2928_CLKGATE_CON(3), 8, GFLAGS),

	/* PD_PERI */
	GATE(0, "cpll_peri", "cpll", CLK_IGNORE_UNUSED,
			RK2928_CLKGATE_CON(2), 0, GFLAGS),
	GATE(0, "gpll_peri", "gpll", CLK_IGNORE_UNUSED,
			RK2928_CLKGATE_CON(2), 0, GFLAGS),
	GATE(0, "hdmiphy_peri", "hdmiphy", CLK_IGNORE_UNUSED,
			RK2928_CLKGATE_CON(2), 0, GFLAGS),
	COMPOSITE_NOGATE(0, "aclk_peri_src", mux_aclk_peri_src_p, 0,
			RK2928_CLKSEL_CON(10), 10, 2, MFLAGS, 0, 5, DFLAGS),
	COMPOSITE_NOMUX(PCLK_PERI, "pclk_peri", "aclk_peri_src", 0,
			RK2928_CLKSEL_CON(10), 12, 3, DFLAGS,
			RK2928_CLKGATE_CON(5), 2, GFLAGS),
	COMPOSITE_NOMUX(HCLK_PERI, "hclk_peri", "aclk_peri_src", 0,
			RK2928_CLKSEL_CON(10), 8, 2, DFLAGS,
			RK2928_CLKGATE_CON(5), 1, GFLAGS),
	GATE(ACLK_PERI, "aclk_peri", "aclk_peri_src", 0,
			RK2928_CLKGATE_CON(5), 0, GFLAGS),

	GATE(SCLK_TIMER0, "sclk_timer0", "xin24m", 0,
			RK2928_CLKGATE_CON(6), 5, GFLAGS),
	GATE(SCLK_TIMER1, "sclk_timer1", "xin24m", 0,
			RK2928_CLKGATE_CON(6), 6, GFLAGS),
	GATE(SCLK_TIMER2, "sclk_timer2", "xin24m", 0,
			RK2928_CLKGATE_CON(6), 7, GFLAGS),
	GATE(SCLK_TIMER3, "sclk_timer3", "xin24m", 0,
			RK2928_CLKGATE_CON(6), 8, GFLAGS),
	GATE(SCLK_TIMER4, "sclk_timer4", "xin24m", 0,
			RK2928_CLKGATE_CON(6), 9, GFLAGS),
	GATE(SCLK_TIMER5, "sclk_timer5", "xin24m", 0,
			RK2928_CLKGATE_CON(6), 10, GFLAGS),

	COMPOSITE(SCLK_CRYPTO, "sclk_crypto", mux_pll_src_2plls_p, 0,
			RK2928_CLKSEL_CON(24), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RK2928_CLKGATE_CON(2), 7, GFLAGS),

	COMPOSITE(SCLK_TSP, "sclk_tsp", mux_pll_src_2plls_p, 0,
			RK2928_CLKSEL_CON(22), 15, 1, MFLAGS, 8, 5, DFLAGS,
			RK2928_CLKGATE_CON(2), 6, GFLAGS),

	GATE(SCLK_HSADC, "sclk_hsadc", "ext_hsadc", 0,
			RK2928_CLKGATE_CON(10), 12, GFLAGS),

	COMPOSITE(SCLK_WIFI, "sclk_wifi", mux_pll_src_cpll_gpll_usb480m_p, 0,
			RK2928_CLKSEL_CON(23), 5, 2, MFLAGS, 0, 6, DFLAGS,
			RK2928_CLKGATE_CON(2), 15, GFLAGS),

	COMPOSITE(SCLK_SDMMC, "sclk_sdmmc", mux_mmc_src_p, 0,
			RK2928_CLKSEL_CON(11), 8, 2, MFLAGS, 0, 8, DFLAGS,
			RK2928_CLKGATE_CON(2), 11, GFLAGS),

	COMPOSITE_NODIV(SCLK_SDIO_SRC, "sclk_sdio_src", mux_mmc_src_p, 0,
			RK2928_CLKSEL_CON(11), 10, 2, MFLAGS,
			RK2928_CLKGATE_CON(2), 13, GFLAGS),
	DIV(SCLK_SDIO, "sclk_sdio", "sclk_sdio_src", 0,
			RK2928_CLKSEL_CON(12), 0, 8, DFLAGS),

	COMPOSITE_NODIV(0, "sclk_emmc_src", mux_mmc_src_p, 0,
			RK2928_CLKSEL_CON(11), 12, 2, MFLAGS,
			RK2928_CLKGATE_CON(2), 14, GFLAGS),
	DIV(SCLK_EMMC, "sclk_emmc", "sclk_emmc_src", 0,
			RK2928_CLKSEL_CON(12), 8, 8, DFLAGS),

	/*
	 * Clock-Architecture Diagram 2
	 */

	GATE(0, "gpll_vop", "gpll", 0,
			RK2928_CLKGATE_CON(3), 1, GFLAGS),
	GATE(0, "cpll_vop", "cpll", 0,
			RK2928_CLKGATE_CON(3), 1, GFLAGS),
	MUX(0, "sclk_vop_src", mux_sclk_vop_src_p, 0,
			RK2928_CLKSEL_CON(27), 0, 1, MFLAGS),
	DIV(DCLK_HDMI_PHY, "dclk_hdmiphy", "sclk_vop_src", 0,
			RK2928_CLKSEL_CON(29), 0, 3, DFLAGS),
	DIV(0, "sclk_vop_pre", "sclk_vop_src", 0,
			RK2928_CLKSEL_CON(27), 8, 8, DFLAGS),
	MUX(DCLK_VOP, "dclk_vop", mux_dclk_vop_p, 0,
			RK2928_CLKSEL_CON(27), 1, 1, MFLAGS),

	FACTOR(0, "xin12m", "xin24m", 0, 1, 2),

	COMPOSITE(0, "i2s0_src", mux_pll_src_2plls_p, 0,
			RK2928_CLKSEL_CON(9), 15, 1, MFLAGS, 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(0), 3, GFLAGS),
	COMPOSITE_FRACMUX(0, "i2s0_frac", "i2s0_src", CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(8), 0,
			RK2928_CLKGATE_CON(0), 4, GFLAGS,
			&rk3228_i2s0_fracmux),
	GATE(SCLK_I2S0, "sclk_i2s0", "i2s0_pre", CLK_SET_RATE_PARENT,
			RK2928_CLKGATE_CON(0), 5, GFLAGS),

	COMPOSITE(0, "i2s1_src", mux_pll_src_2plls_p, 0,
			RK2928_CLKSEL_CON(3), 15, 1, MFLAGS, 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(0), 10, GFLAGS),
	COMPOSITE_FRACMUX(0, "i2s1_frac", "i2s1_src", CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(7), 0,
			RK2928_CLKGATE_CON(0), 11, GFLAGS,
			&rk3228_i2s1_fracmux),
	GATE(SCLK_I2S1, "sclk_i2s1", "i2s1_pre", CLK_SET_RATE_PARENT,
			RK2928_CLKGATE_CON(0), 14, GFLAGS),
	COMPOSITE_NODIV(SCLK_I2S_OUT, "i2s_out", mux_i2s_out_p, 0,
			RK2928_CLKSEL_CON(3), 12, 1, MFLAGS,
			RK2928_CLKGATE_CON(0), 13, GFLAGS),

	COMPOSITE(0, "i2s2_src", mux_pll_src_2plls_p, 0,
			RK2928_CLKSEL_CON(16), 15, 1, MFLAGS, 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(0), 7, GFLAGS),
	COMPOSITE_FRACMUX(0, "i2s2_frac", "i2s2_src", CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(30), 0,
			RK2928_CLKGATE_CON(0), 8, GFLAGS,
			&rk3228_i2s2_fracmux),
	GATE(SCLK_I2S2, "sclk_i2s2", "i2s2_pre", CLK_SET_RATE_PARENT,
			RK2928_CLKGATE_CON(0), 9, GFLAGS),

	COMPOSITE(0, "sclk_spdif_src", mux_pll_src_2plls_p, 0,
			RK2928_CLKSEL_CON(6), 15, 1, MFLAGS, 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(2), 10, GFLAGS),
	COMPOSITE_FRACMUX(0, "spdif_frac", "sclk_spdif_src", CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(20), 0,
			RK2928_CLKGATE_CON(2), 12, GFLAGS,
			&rk3228_spdif_fracmux),

	GATE(0, "jtag", "ext_jtag", CLK_IGNORE_UNUSED,
			RK2928_CLKGATE_CON(1), 3, GFLAGS),

	GATE(SCLK_OTGPHY0, "sclk_otgphy0", "xin24m", 0,
			RK2928_CLKGATE_CON(1), 5, GFLAGS),
	GATE(SCLK_OTGPHY1, "sclk_otgphy1", "xin24m", 0,
			RK2928_CLKGATE_CON(1), 6, GFLAGS),

	COMPOSITE_NOMUX(SCLK_TSADC, "sclk_tsadc", "xin24m", 0,
			RK2928_CLKSEL_CON(24), 6, 10, DFLAGS,
			RK2928_CLKGATE_CON(2), 8, GFLAGS),

	COMPOSITE(0, "aclk_gpu_pre", mux_pll_src_4plls_p, 0,
			RK2928_CLKSEL_CON(34), 5, 2, MFLAGS, 0, 5, DFLAGS,
			RK2928_CLKGATE_CON(3), 13, GFLAGS),

	COMPOSITE(SCLK_SPI0, "sclk_spi0", mux_pll_src_2plls_p, 0,
			RK2928_CLKSEL_CON(25), 8, 1, MFLAGS, 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(2), 9, GFLAGS),

	/* PD_UART */
	COMPOSITE(0, "uart0_src", mux_pll_src_cpll_gpll_usb480m_p, 0,
			RK2928_CLKSEL_CON(13), 12, 2, MFLAGS, 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(1), 8, GFLAGS),
	COMPOSITE(0, "uart1_src", mux_pll_src_cpll_gpll_usb480m_p, 0,
			RK2928_CLKSEL_CON(14), 12, 2, MFLAGS, 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(1), 10, GFLAGS),
	COMPOSITE(0, "uart2_src", mux_pll_src_cpll_gpll_usb480m_p,
			0, RK2928_CLKSEL_CON(15), 12, 2,
			MFLAGS, 0, 7, DFLAGS, RK2928_CLKGATE_CON(1), 12, GFLAGS),
	COMPOSITE_FRACMUX(0, "uart0_frac", "uart0_src", CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(17), 0,
			RK2928_CLKGATE_CON(1), 9, GFLAGS,
			&rk3228_uart0_fracmux),
	COMPOSITE_FRACMUX(0, "uart1_frac", "uart1_src", CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(18), 0,
			RK2928_CLKGATE_CON(1), 11, GFLAGS,
			&rk3228_uart1_fracmux),
	COMPOSITE_FRACMUX(0, "uart2_frac", "uart2_src", CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(19), 0,
			RK2928_CLKGATE_CON(1), 13, GFLAGS,
			&rk3228_uart2_fracmux),

	COMPOSITE(SCLK_NANDC, "sclk_nandc", mux_pll_src_2plls_p, 0,
			RK2928_CLKSEL_CON(2), 14, 1, MFLAGS, 8, 5, DFLAGS,
			RK2928_CLKGATE_CON(1), 0, GFLAGS),

	COMPOSITE(SCLK_MAC_SRC, "sclk_gmac_src", mux_pll_src_2plls_p, 0,
			RK2928_CLKSEL_CON(5), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RK2928_CLKGATE_CON(1), 7, GFLAGS),
	MUX(SCLK_MAC_EXTCLK, "sclk_mac_extclk", mux_sclk_mac_extclk_p, 0,
			RK2928_CLKSEL_CON(29), 10, 1, MFLAGS),
	MUX(SCLK_MAC, "sclk_gmac_pre", mux_sclk_gmac_pre_p, 0,
			RK2928_CLKSEL_CON(5), 5, 1, MFLAGS),
	GATE(SCLK_MAC_REFOUT, "sclk_mac_refout", "sclk_gmac_pre", 0,
			RK2928_CLKGATE_CON(5), 4, GFLAGS),
	GATE(SCLK_MAC_REF, "sclk_mac_ref", "sclk_gmac_pre", 0,
			RK2928_CLKGATE_CON(5), 3, GFLAGS),
	GATE(SCLK_MAC_RX, "sclk_mac_rx", "sclk_gmac_pre", 0,
			RK2928_CLKGATE_CON(5), 5, GFLAGS),
	GATE(SCLK_MAC_TX, "sclk_mac_tx", "sclk_gmac_pre", 0,
			RK2928_CLKGATE_CON(5), 6, GFLAGS),
	COMPOSITE(SCLK_MAC_PHY, "sclk_macphy", mux_sclk_macphy_p, 0,
			RK2928_CLKSEL_CON(29), 12, 1, MFLAGS, 8, 2, DFLAGS,
			RK2928_CLKGATE_CON(5), 7, GFLAGS),
	COMPOSITE(SCLK_MAC_OUT, "sclk_gmac_out", mux_pll_src_2plls_p, 0,
			RK2928_CLKSEL_CON(5), 15, 1, MFLAGS, 8, 5, DFLAGS,
			RK2928_CLKGATE_CON(2), 2, GFLAGS),

	/*
	 * Clock-Architecture Diagram 3
	 */

	/* PD_VOP */
	GATE(ACLK_RGA, "aclk_rga", "aclk_rga_pre", 0, RK2928_CLKGATE_CON(13), 0, GFLAGS),
	GATE(0, "aclk_rga_noc", "aclk_rga_pre", 0, RK2928_CLKGATE_CON(13), 11, GFLAGS),
	GATE(ACLK_IEP, "aclk_iep", "aclk_iep_pre", 0, RK2928_CLKGATE_CON(13), 2, GFLAGS),
	GATE(0, "aclk_iep_noc", "aclk_iep_pre", 0, RK2928_CLKGATE_CON(13), 9, GFLAGS),

	GATE(ACLK_VOP, "aclk_vop", "aclk_vop_pre", 0, RK2928_CLKGATE_CON(13), 5, GFLAGS),
	GATE(0, "aclk_vop_noc", "aclk_vop_pre", 0, RK2928_CLKGATE_CON(13), 12, GFLAGS),

	GATE(ACLK_HDCP, "aclk_hdcp", "aclk_hdcp_pre", 0, RK2928_CLKGATE_CON(14), 10, GFLAGS),
	GATE(0, "aclk_hdcp_noc", "aclk_hdcp_pre", 0, RK2928_CLKGATE_CON(13), 10, GFLAGS),

	GATE(HCLK_RGA, "hclk_rga", "hclk_vio_pre", 0, RK2928_CLKGATE_CON(13), 1, GFLAGS),
	GATE(HCLK_IEP, "hclk_iep", "hclk_vio_pre", 0, RK2928_CLKGATE_CON(13), 3, GFLAGS),
	GATE(HCLK_VOP, "hclk_vop", "hclk_vio_pre", 0, RK2928_CLKGATE_CON(13), 6, GFLAGS),
	GATE(0, "hclk_vio_ahb_arbi", "hclk_vio_pre", 0, RK2928_CLKGATE_CON(13), 7, GFLAGS),
	GATE(0, "hclk_vio_noc", "hclk_vio_pre", 0, RK2928_CLKGATE_CON(13), 8, GFLAGS),
	GATE(0, "hclk_vop_noc", "hclk_vio_pre", 0, RK2928_CLKGATE_CON(13), 13, GFLAGS),
	GATE(HCLK_VIO_H2P, "hclk_vio_h2p", "hclk_vio_pre", 0, RK2928_CLKGATE_CON(14), 7, GFLAGS),
	GATE(HCLK_HDCP_MMU, "hclk_hdcp_mmu", "hclk_vio_pre", 0, RK2928_CLKGATE_CON(14), 12, GFLAGS),
	GATE(PCLK_HDMI_CTRL, "pclk_hdmi_ctrl", "hclk_vio_pre", 0, RK2928_CLKGATE_CON(14), 6, GFLAGS),
	GATE(PCLK_VIO_H2P, "pclk_vio_h2p", "hclk_vio_pre", 0, RK2928_CLKGATE_CON(14), 8, GFLAGS),
	GATE(PCLK_HDCP, "pclk_hdcp", "hclk_vio_pre", 0, RK2928_CLKGATE_CON(14), 11, GFLAGS),

	/* PD_PERI */
	GATE(0, "aclk_peri_noc", "aclk_peri", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(12), 0, GFLAGS),
	GATE(ACLK_GMAC, "aclk_gmac", "aclk_peri", 0, RK2928_CLKGATE_CON(11), 4, GFLAGS),

	GATE(HCLK_SDMMC, "hclk_sdmmc", "hclk_peri", 0, RK2928_CLKGATE_CON(11), 0, GFLAGS),
	GATE(HCLK_SDIO, "hclk_sdio", "hclk_peri", 0, RK2928_CLKGATE_CON(11), 1, GFLAGS),
	GATE(HCLK_EMMC, "hclk_emmc", "hclk_peri", 0, RK2928_CLKGATE_CON(11), 2, GFLAGS),
	GATE(HCLK_NANDC, "hclk_nandc", "hclk_peri", 0, RK2928_CLKGATE_CON(11), 3, GFLAGS),
	GATE(HCLK_HOST0, "hclk_host0", "hclk_peri", 0, RK2928_CLKGATE_CON(11), 6, GFLAGS),
	GATE(0, "hclk_host0_arb", "hclk_peri", 0, RK2928_CLKGATE_CON(11), 7, GFLAGS),
	GATE(HCLK_HOST1, "hclk_host1", "hclk_peri", 0, RK2928_CLKGATE_CON(11), 8, GFLAGS),
	GATE(0, "hclk_host1_arb", "hclk_peri", 0, RK2928_CLKGATE_CON(11), 9, GFLAGS),
	GATE(HCLK_HOST2, "hclk_host2", "hclk_peri", 0, RK2928_CLKGATE_CON(11), 10, GFLAGS),
	GATE(HCLK_OTG, "hclk_otg", "hclk_peri", 0, RK2928_CLKGATE_CON(11), 12, GFLAGS),
	GATE(0, "hclk_otg_pmu", "hclk_peri", 0, RK2928_CLKGATE_CON(11), 13, GFLAGS),
	GATE(0, "hclk_host2_arb", "hclk_peri", 0, RK2928_CLKGATE_CON(11), 14, GFLAGS),
	GATE(0, "hclk_peri_noc", "hclk_peri", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(12), 1, GFLAGS),

	GATE(PCLK_GMAC, "pclk_gmac", "pclk_peri", 0, RK2928_CLKGATE_CON(11), 5, GFLAGS),
	GATE(0, "pclk_peri_noc", "pclk_peri", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(12), 2, GFLAGS),

	/* PD_GPU */
	GATE(ACLK_GPU, "aclk_gpu", "aclk_gpu_pre", 0, RK2928_CLKGATE_CON(7), 14, GFLAGS),
	GATE(0, "aclk_gpu_noc", "aclk_gpu_pre", 0, RK2928_CLKGATE_CON(7), 15, GFLAGS),

	/* PD_BUS */
	GATE(0, "sclk_initmem_mbist", "aclk_cpu", 0, RK2928_CLKGATE_CON(8), 1, GFLAGS),
	GATE(0, "aclk_initmem", "aclk_cpu", 0, RK2928_CLKGATE_CON(8), 0, GFLAGS),
	GATE(ACLK_DMAC, "aclk_dmac_bus", "aclk_cpu", 0, RK2928_CLKGATE_CON(8), 2, GFLAGS),
	GATE(0, "aclk_bus_noc", "aclk_cpu", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(10), 1, GFLAGS),

	GATE(0, "hclk_rom", "hclk_cpu", 0, RK2928_CLKGATE_CON(8), 3, GFLAGS),
	GATE(HCLK_I2S0_8CH, "hclk_i2s0_8ch", "hclk_cpu", 0, RK2928_CLKGATE_CON(8), 7, GFLAGS),
	GATE(HCLK_I2S1_8CH, "hclk_i2s1_8ch", "hclk_cpu", 0, RK2928_CLKGATE_CON(8), 8, GFLAGS),
	GATE(HCLK_I2S2_2CH, "hclk_i2s2_2ch", "hclk_cpu", 0, RK2928_CLKGATE_CON(8), 9, GFLAGS),
	GATE(HCLK_SPDIF_8CH, "hclk_spdif_8ch", "hclk_cpu", 0, RK2928_CLKGATE_CON(8), 10, GFLAGS),
	GATE(HCLK_TSP, "hclk_tsp", "hclk_cpu", 0, RK2928_CLKGATE_CON(10), 11, GFLAGS),
	GATE(HCLK_M_CRYPTO, "hclk_crypto_mst", "hclk_cpu", 0, RK2928_CLKGATE_CON(8), 11, GFLAGS),
	GATE(HCLK_S_CRYPTO, "hclk_crypto_slv", "hclk_cpu", 0, RK2928_CLKGATE_CON(8), 12, GFLAGS),

	GATE(0, "pclk_ddrupctl", "pclk_ddr_pre", 0, RK2928_CLKGATE_CON(8), 4, GFLAGS),
	GATE(0, "pclk_ddrmon", "pclk_ddr_pre", 0, RK2928_CLKGATE_CON(8), 6, GFLAGS),
	GATE(0, "pclk_msch_noc", "pclk_ddr_pre", 0, RK2928_CLKGATE_CON(10), 2, GFLAGS),

	GATE(PCLK_EFUSE_1024, "pclk_efuse_1024", "pclk_cpu", 0, RK2928_CLKGATE_CON(8), 13, GFLAGS),
	GATE(PCLK_EFUSE_256, "pclk_efuse_256", "pclk_cpu", 0, RK2928_CLKGATE_CON(8), 14, GFLAGS),
	GATE(PCLK_I2C0, "pclk_i2c0", "pclk_cpu", 0, RK2928_CLKGATE_CON(8), 15, GFLAGS),
	GATE(PCLK_I2C1, "pclk_i2c1", "pclk_cpu", 0, RK2928_CLKGATE_CON(9), 0, GFLAGS),
	GATE(PCLK_I2C2, "pclk_i2c2", "pclk_cpu", 0, RK2928_CLKGATE_CON(9), 1, GFLAGS),
	GATE(PCLK_I2C3, "pclk_i2c3", "pclk_cpu", 0, RK2928_CLKGATE_CON(9), 2, GFLAGS),
	GATE(PCLK_TIMER, "pclk_timer0", "pclk_cpu", 0, RK2928_CLKGATE_CON(9), 4, GFLAGS),
	GATE(0, "pclk_stimer", "pclk_cpu", 0, RK2928_CLKGATE_CON(9), 5, GFLAGS),
	GATE(PCLK_SPI0, "pclk_spi0", "pclk_cpu", 0, RK2928_CLKGATE_CON(9), 6, GFLAGS),
	GATE(PCLK_PWM, "pclk_rk_pwm", "pclk_cpu", 0, RK2928_CLKGATE_CON(9), 7, GFLAGS),
	GATE(PCLK_GPIO0, "pclk_gpio0", "pclk_cpu", 0, RK2928_CLKGATE_CON(9), 8, GFLAGS),
	GATE(PCLK_GPIO1, "pclk_gpio1", "pclk_cpu", 0, RK2928_CLKGATE_CON(9), 9, GFLAGS),
	GATE(PCLK_GPIO2, "pclk_gpio2", "pclk_cpu", 0, RK2928_CLKGATE_CON(9), 10, GFLAGS),
	GATE(PCLK_GPIO3, "pclk_gpio3", "pclk_cpu", 0, RK2928_CLKGATE_CON(9), 11, GFLAGS),
	GATE(PCLK_UART0, "pclk_uart0", "pclk_cpu", 0, RK2928_CLKGATE_CON(9), 12, GFLAGS),
	GATE(PCLK_UART1, "pclk_uart1", "pclk_cpu", 0, RK2928_CLKGATE_CON(9), 13, GFLAGS),
	GATE(PCLK_UART2, "pclk_uart2", "pclk_cpu", 0, RK2928_CLKGATE_CON(9), 14, GFLAGS),
	GATE(PCLK_TSADC, "pclk_tsadc", "pclk_cpu", 0, RK2928_CLKGATE_CON(9), 15, GFLAGS),
	GATE(PCLK_GRF, "pclk_grf", "pclk_cpu", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(10), 0, GFLAGS),
	GATE(0, "pclk_cru", "pclk_cpu", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(10), 1, GFLAGS),
	GATE(0, "pclk_sgrf", "pclk_cpu", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(10), 2, GFLAGS),
	GATE(0, "pclk_sim", "pclk_cpu", 0, RK2928_CLKGATE_CON(10), 3, GFLAGS),

	GATE(0, "pclk_ddrphy", "pclk_phy_pre", 0, RK2928_CLKGATE_CON(10), 3, GFLAGS),
	GATE(0, "pclk_acodecphy", "pclk_phy_pre", 0, RK2928_CLKGATE_CON(10), 5, GFLAGS),
	GATE(PCLK_HDMI_PHY, "pclk_hdmiphy", "pclk_phy_pre", 0, RK2928_CLKGATE_CON(10), 7, GFLAGS),
	GATE(0, "pclk_vdacphy", "pclk_phy_pre", 0, RK2928_CLKGATE_CON(10), 8, GFLAGS),
	GATE(0, "pclk_phy_noc", "pclk_phy_pre", 0, RK2928_CLKGATE_CON(10), 9, GFLAGS),

	GATE(ACLK_VPU, "aclk_vpu", "aclk_vpu_pre", 0, RK2928_CLKGATE_CON(15), 0, GFLAGS),
	GATE(0, "aclk_vpu_noc", "aclk_vpu_pre", 0, RK2928_CLKGATE_CON(15), 4, GFLAGS),
	GATE(ACLK_RKVDEC, "aclk_rkvdec", "aclk_rkvdec_pre", 0, RK2928_CLKGATE_CON(15), 2, GFLAGS),
	GATE(0, "aclk_rkvdec_noc", "aclk_rkvdec_pre", 0, RK2928_CLKGATE_CON(15), 6, GFLAGS),
	GATE(HCLK_VPU, "hclk_vpu", "hclk_vpu_pre", 0, RK2928_CLKGATE_CON(15), 1, GFLAGS),
	GATE(0, "hclk_vpu_noc", "hclk_vpu_pre", 0, RK2928_CLKGATE_CON(15), 5, GFLAGS),
	GATE(HCLK_RKVDEC, "hclk_rkvdec", "hclk_rkvdec_pre", 0, RK2928_CLKGATE_CON(15), 3, GFLAGS),
	GATE(0, "hclk_rkvdec_noc", "hclk_rkvdec_pre", 0, RK2928_CLKGATE_CON(15), 7, GFLAGS),

	/* PD_MMC */
	MMC(SCLK_SDMMC_DRV,    "sdmmc_drv",    "sclk_sdmmc", RK3228_SDMMC_CON0, 1),
	MMC(SCLK_SDMMC_SAMPLE, "sdmmc_sample", "sclk_sdmmc", RK3228_SDMMC_CON1, 0),

	MMC(SCLK_SDIO_DRV,     "sdio_drv",     "sclk_sdio",  RK3228_SDIO_CON0,  1),
	MMC(SCLK_SDIO_SAMPLE,  "sdio_sample",  "sclk_sdio",  RK3228_SDIO_CON1,  0),

	MMC(SCLK_EMMC_DRV,     "emmc_drv",     "sclk_emmc",  RK3228_EMMC_CON0,  1),
	MMC(SCLK_EMMC_SAMPLE,  "emmc_sample",  "sclk_emmc",  RK3228_EMMC_CON1,  0),
};

static const char *const rk3228_critical_clocks[] __initconst = {
	"aclk_cpu",
	"pclk_cpu",
	"hclk_cpu",
	"aclk_peri",
	"hclk_peri",
	"pclk_peri",
	"aclk_rga_noc",
	"aclk_iep_noc",
	"aclk_vop_noc",
	"aclk_hdcp_noc",
	"hclk_vio_ahb_arbi",
	"hclk_vio_noc",
	"hclk_vop_noc",
	"hclk_host0_arb",
	"hclk_host1_arb",
	"hclk_host2_arb",
	"hclk_otg_pmu",
	"aclk_gpu_noc",
	"sclk_initmem_mbist",
	"aclk_initmem",
	"hclk_rom",
	"pclk_ddrupctl",
	"pclk_ddrmon",
	"pclk_msch_noc",
	"pclk_stimer",
	"pclk_ddrphy",
	"pclk_acodecphy",
	"pclk_phy_noc",
	"aclk_vpu_noc",
	"aclk_rkvdec_noc",
	"hclk_vpu_noc",
	"hclk_rkvdec_noc",
};

static void __init rk3228_clk_init(struct device_node *np)
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

	rockchip_clk_register_plls(ctx, rk3228_pll_clks,
				   ARRAY_SIZE(rk3228_pll_clks),
				   RK3228_GRF_SOC_STATUS0);
	rockchip_clk_register_branches(ctx, rk3228_clk_branches,
				  ARRAY_SIZE(rk3228_clk_branches));
	rockchip_clk_protect_critical(rk3228_critical_clocks,
				      ARRAY_SIZE(rk3228_critical_clocks));

	rockchip_clk_register_armclk(ctx, ARMCLK, "armclk",
			mux_armclk_p, ARRAY_SIZE(mux_armclk_p),
			&rk3228_cpuclk_data, rk3228_cpuclk_rates,
			ARRAY_SIZE(rk3228_cpuclk_rates));

	rockchip_register_softrst(np, 9, reg_base + RK2928_SOFTRST_CON(0),
				  ROCKCHIP_SOFTRST_HIWORD_MASK);

	rockchip_register_restart_notifier(ctx, RK3228_GLB_SRST_FST, NULL);

	rockchip_clk_of_add_provider(np, ctx);
}
CLK_OF_DECLARE(rk3228_cru, "rockchip,rk3228-cru", rk3228_clk_init);
