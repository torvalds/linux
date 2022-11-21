// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2016 Rockchip Electronics Co. Ltd.
 * Author: Xing Zheng <zhengxing@rock-chips.com>
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <dt-bindings/clock/rk3399-cru.h>
#include "clk.h"

enum rk3399_plls {
	lpll, bpll, dpll, cpll, gpll, npll, vpll,
};

enum rk3399_pmu_plls {
	ppll,
};

static struct rockchip_pll_rate_table rk3399_pll_rates[] = {
	/* _mhz, _refdiv, _fbdiv, _postdiv1, _postdiv2, _dsmpd, _frac */
	RK3036_PLL_RATE(2208000000, 1, 92, 1, 1, 1, 0),
	RK3036_PLL_RATE(2184000000, 1, 91, 1, 1, 1, 0),
	RK3036_PLL_RATE(2160000000, 1, 90, 1, 1, 1, 0),
	RK3036_PLL_RATE(2136000000, 1, 89, 1, 1, 1, 0),
	RK3036_PLL_RATE(2112000000, 1, 88, 1, 1, 1, 0),
	RK3036_PLL_RATE(2088000000, 1, 87, 1, 1, 1, 0),
	RK3036_PLL_RATE(2064000000, 1, 86, 1, 1, 1, 0),
	RK3036_PLL_RATE(2040000000, 1, 85, 1, 1, 1, 0),
	RK3036_PLL_RATE(2016000000, 1, 84, 1, 1, 1, 0),
	RK3036_PLL_RATE(1992000000, 1, 83, 1, 1, 1, 0),
	RK3036_PLL_RATE(1968000000, 1, 82, 1, 1, 1, 0),
	RK3036_PLL_RATE(1944000000, 1, 81, 1, 1, 1, 0),
	RK3036_PLL_RATE(1920000000, 1, 80, 1, 1, 1, 0),
	RK3036_PLL_RATE(1896000000, 1, 79, 1, 1, 1, 0),
	RK3036_PLL_RATE(1872000000, 1, 78, 1, 1, 1, 0),
	RK3036_PLL_RATE(1848000000, 1, 77, 1, 1, 1, 0),
	RK3036_PLL_RATE(1824000000, 1, 76, 1, 1, 1, 0),
	RK3036_PLL_RATE(1800000000, 1, 75, 1, 1, 1, 0),
	RK3036_PLL_RATE(1776000000, 1, 74, 1, 1, 1, 0),
	RK3036_PLL_RATE(1752000000, 1, 73, 1, 1, 1, 0),
	RK3036_PLL_RATE(1728000000, 1, 72, 1, 1, 1, 0),
	RK3036_PLL_RATE(1704000000, 1, 71, 1, 1, 1, 0),
	RK3036_PLL_RATE(1680000000, 1, 70, 1, 1, 1, 0),
	RK3036_PLL_RATE(1656000000, 1, 69, 1, 1, 1, 0),
	RK3036_PLL_RATE(1632000000, 1, 68, 1, 1, 1, 0),
	RK3036_PLL_RATE(1608000000, 1, 67, 1, 1, 1, 0),
	RK3036_PLL_RATE(1600000000, 3, 200, 1, 1, 1, 0),
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
	RK3036_PLL_RATE(1000000000, 1, 125, 3, 1, 1, 0),
	RK3036_PLL_RATE( 984000000, 1, 82, 2, 1, 1, 0),
	RK3036_PLL_RATE( 960000000, 1, 80, 2, 1, 1, 0),
	RK3036_PLL_RATE( 936000000, 1, 78, 2, 1, 1, 0),
	RK3036_PLL_RATE( 912000000, 1, 76, 2, 1, 1, 0),
	RK3036_PLL_RATE( 900000000, 4, 300, 2, 1, 1, 0),
	RK3036_PLL_RATE( 888000000, 1, 74, 2, 1, 1, 0),
	RK3036_PLL_RATE( 864000000, 1, 72, 2, 1, 1, 0),
	RK3036_PLL_RATE( 840000000, 1, 70, 2, 1, 1, 0),
	RK3036_PLL_RATE( 816000000, 1, 68, 2, 1, 1, 0),
	RK3036_PLL_RATE( 800000000, 1, 100, 3, 1, 1, 0),
	RK3036_PLL_RATE( 700000000, 6, 350, 2, 1, 1, 0),
	RK3036_PLL_RATE( 696000000, 1, 58, 2, 1, 1, 0),
	RK3036_PLL_RATE( 676000000, 3, 169, 2, 1, 1, 0),
	RK3036_PLL_RATE( 600000000, 1, 75, 3, 1, 1, 0),
	RK3036_PLL_RATE( 594000000, 1, 99, 4, 1, 1, 0),
	RK3036_PLL_RATE( 533250000, 8, 711, 4, 1, 1, 0),
	RK3036_PLL_RATE( 504000000, 1, 63, 3, 1, 1, 0),
	RK3036_PLL_RATE( 500000000, 6, 250, 2, 1, 1, 0),
	RK3036_PLL_RATE( 408000000, 1, 68, 2, 2, 1, 0),
	RK3036_PLL_RATE( 312000000, 1, 52, 2, 2, 1, 0),
	RK3036_PLL_RATE( 297000000, 1, 99, 4, 2, 1, 0),
	RK3036_PLL_RATE( 216000000, 1, 72, 4, 2, 1, 0),
	RK3036_PLL_RATE( 148500000, 1, 99, 4, 4, 1, 0),
	RK3036_PLL_RATE( 106500000, 1, 71, 4, 4, 1, 0),
	RK3036_PLL_RATE(  96000000, 1, 64, 4, 4, 1, 0),
	RK3036_PLL_RATE(  74250000, 2, 99, 4, 4, 1, 0),
	RK3036_PLL_RATE(  65000000, 1, 65, 6, 4, 1, 0),
	RK3036_PLL_RATE(  54000000, 1, 54, 6, 4, 1, 0),
	RK3036_PLL_RATE(  27000000, 1, 27, 6, 4, 1, 0),
	{ /* sentinel */ },
};

/* CRU parents */
PNAME(mux_pll_p)				= { "xin24m", "xin32k" };

PNAME(mux_armclkl_p)				= { "clk_core_l_lpll_src",
						    "clk_core_l_bpll_src",
						    "clk_core_l_dpll_src",
						    "clk_core_l_gpll_src" };
PNAME(mux_armclkb_p)				= { "clk_core_b_lpll_src",
						    "clk_core_b_bpll_src",
						    "clk_core_b_dpll_src",
						    "clk_core_b_gpll_src" };
PNAME(mux_ddrclk_p)				= { "clk_ddrc_lpll_src",
						    "clk_ddrc_bpll_src",
						    "clk_ddrc_dpll_src",
						    "clk_ddrc_gpll_src" };
PNAME(mux_aclk_cci_p)				= { "cpll_aclk_cci_src",
						    "gpll_aclk_cci_src",
						    "npll_aclk_cci_src",
						    "vpll_aclk_cci_src" };
PNAME(mux_cci_trace_p)				= { "cpll_cci_trace",
						    "gpll_cci_trace" };
PNAME(mux_cs_p)					= { "cpll_cs", "gpll_cs",
						    "npll_cs"};
PNAME(mux_aclk_perihp_p)			= { "cpll_aclk_perihp_src",
						    "gpll_aclk_perihp_src" };

PNAME(mux_pll_src_cpll_gpll_p)			= { "cpll", "gpll" };
PNAME(mux_pll_src_cpll_gpll_npll_p)		= { "cpll", "gpll", "npll" };
PNAME(mux_pll_src_cpll_gpll_ppll_p)		= { "cpll", "gpll", "ppll" };
PNAME(mux_pll_src_cpll_gpll_upll_p)		= { "cpll", "gpll", "upll" };
PNAME(mux_pll_src_npll_cpll_gpll_p)		= { "npll", "cpll", "gpll" };
PNAME(mux_pll_src_cpll_gpll_npll_ppll_p)	= { "cpll", "gpll", "npll",
						    "ppll" };
PNAME(mux_pll_src_cpll_gpll_npll_24m_p)		= { "cpll", "gpll", "npll",
						    "xin24m" };
PNAME(mux_pll_src_cpll_gpll_npll_usbphy480m_p)	= { "cpll", "gpll", "npll",
						    "clk_usbphy_480m" };
PNAME(mux_pll_src_ppll_cpll_gpll_npll_p)	= { "ppll", "cpll", "gpll",
						    "npll", "upll" };
PNAME(mux_pll_src_cpll_gpll_npll_upll_24m_p)	= { "cpll", "gpll", "npll",
						    "upll", "xin24m" };
PNAME(mux_pll_src_cpll_gpll_npll_ppll_upll_24m_p) = { "cpll", "gpll", "npll",
						    "ppll", "upll", "xin24m" };

PNAME(mux_pll_src_vpll_cpll_gpll_p)		= { "vpll", "cpll", "gpll" };
PNAME(mux_pll_src_vpll_cpll_gpll_npll_p)	= { "vpll", "cpll", "gpll",
						    "npll" };
PNAME(mux_pll_src_vpll_cpll_gpll_24m_p)		= { "vpll", "cpll", "gpll",
						    "xin24m" };

PNAME(mux_dclk_vop0_p)			= { "dclk_vop0_div",
					    "dclk_vop0_frac" };
PNAME(mux_dclk_vop1_p)			= { "dclk_vop1_div",
					    "dclk_vop1_frac" };

PNAME(mux_clk_cif_p)			= { "clk_cifout_src", "xin24m" };

PNAME(mux_pll_src_24m_usbphy480m_p)	= { "xin24m", "clk_usbphy_480m" };
PNAME(mux_pll_src_24m_pciephy_p)	= { "xin24m", "clk_pciephy_ref100m" };
PNAME(mux_pll_src_24m_32k_cpll_gpll_p)	= { "xin24m", "xin32k",
					    "cpll", "gpll" };
PNAME(mux_pciecore_cru_phy_p)		= { "clk_pcie_core_cru",
					    "clk_pcie_core_phy" };

PNAME(mux_aclk_emmc_p)			= { "cpll_aclk_emmc_src",
					    "gpll_aclk_emmc_src" };

PNAME(mux_aclk_perilp0_p)		= { "cpll_aclk_perilp0_src",
					    "gpll_aclk_perilp0_src" };

PNAME(mux_fclk_cm0s_p)			= { "cpll_fclk_cm0s_src",
					    "gpll_fclk_cm0s_src" };

PNAME(mux_hclk_perilp1_p)		= { "cpll_hclk_perilp1_src",
					    "gpll_hclk_perilp1_src" };

PNAME(mux_clk_testout1_p)		= { "clk_testout1_pll_src", "xin24m" };
PNAME(mux_clk_testout2_p)		= { "clk_testout2_pll_src", "xin24m" };

PNAME(mux_usbphy_480m_p)		= { "clk_usbphy0_480m_src",
					    "clk_usbphy1_480m_src" };
PNAME(mux_aclk_gmac_p)			= { "cpll_aclk_gmac_src",
					    "gpll_aclk_gmac_src" };
PNAME(mux_rmii_p)			= { "clk_gmac", "clkin_gmac" };
PNAME(mux_spdif_p)			= { "clk_spdif_div", "clk_spdif_frac",
					    "clkin_i2s", "xin12m" };
PNAME(mux_i2s0_p)			= { "clk_i2s0_div", "clk_i2s0_frac",
					    "clkin_i2s", "xin12m" };
PNAME(mux_i2s1_p)			= { "clk_i2s1_div", "clk_i2s1_frac",
					    "clkin_i2s", "xin12m" };
PNAME(mux_i2s2_p)			= { "clk_i2s2_div", "clk_i2s2_frac",
					    "clkin_i2s", "xin12m" };
PNAME(mux_i2sch_p)			= { "clk_i2s0", "clk_i2s1",
					    "clk_i2s2" };
PNAME(mux_i2sout_p)			= { "clk_i2sout_src", "xin12m" };

PNAME(mux_uart0_p)	= { "clk_uart0_div", "clk_uart0_frac", "xin24m" };
PNAME(mux_uart1_p)	= { "clk_uart1_div", "clk_uart1_frac", "xin24m" };
PNAME(mux_uart2_p)	= { "clk_uart2_div", "clk_uart2_frac", "xin24m" };
PNAME(mux_uart3_p)	= { "clk_uart3_div", "clk_uart3_frac", "xin24m" };

/* PMU CRU parents */
PNAME(mux_ppll_24m_p)		= { "ppll", "xin24m" };
PNAME(mux_24m_ppll_p)		= { "xin24m", "ppll" };
PNAME(mux_fclk_cm0s_pmu_ppll_p)	= { "fclk_cm0s_pmu_ppll_src", "xin24m" };
PNAME(mux_wifi_pmu_p)		= { "clk_wifi_div", "clk_wifi_frac" };
PNAME(mux_uart4_pmu_p)		= { "clk_uart4_div", "clk_uart4_frac",
				    "xin24m" };
PNAME(mux_clk_testout2_2io_p)	= { "clk_testout2", "clk_32k_suspend_pmu" };

static struct rockchip_pll_clock rk3399_pll_clks[] __initdata = {
	[lpll] = PLL(pll_rk3399, PLL_APLLL, "lpll", mux_pll_p, 0, RK3399_PLL_CON(0),
		     RK3399_PLL_CON(3), 8, 31, 0, rk3399_pll_rates),
	[bpll] = PLL(pll_rk3399, PLL_APLLB, "bpll", mux_pll_p, 0, RK3399_PLL_CON(8),
		     RK3399_PLL_CON(11), 8, 31, 0, rk3399_pll_rates),
	[dpll] = PLL(pll_rk3399, PLL_DPLL, "dpll", mux_pll_p, 0, RK3399_PLL_CON(16),
		     RK3399_PLL_CON(19), 8, 31, 0, NULL),
	[cpll] = PLL(pll_rk3399, PLL_CPLL, "cpll", mux_pll_p, 0, RK3399_PLL_CON(24),
		     RK3399_PLL_CON(27), 8, 31, ROCKCHIP_PLL_SYNC_RATE, rk3399_pll_rates),
	[gpll] = PLL(pll_rk3399, PLL_GPLL, "gpll", mux_pll_p, 0, RK3399_PLL_CON(32),
		     RK3399_PLL_CON(35), 8, 31, ROCKCHIP_PLL_SYNC_RATE, rk3399_pll_rates),
	[npll] = PLL(pll_rk3399, PLL_NPLL, "npll",  mux_pll_p, 0, RK3399_PLL_CON(40),
		     RK3399_PLL_CON(43), 8, 31, ROCKCHIP_PLL_SYNC_RATE, rk3399_pll_rates),
	[vpll] = PLL(pll_rk3399, PLL_VPLL, "vpll",  mux_pll_p, 0, RK3399_PLL_CON(48),
		     RK3399_PLL_CON(51), 8, 31, ROCKCHIP_PLL_SYNC_RATE, rk3399_pll_rates),
};

static struct rockchip_pll_clock rk3399_pmu_pll_clks[] __initdata = {
	[ppll] = PLL(pll_rk3399, PLL_PPLL, "ppll",  mux_pll_p, 0, RK3399_PMU_PLL_CON(0),
		     RK3399_PMU_PLL_CON(3), 8, 31, ROCKCHIP_PLL_SYNC_RATE, rk3399_pll_rates),
};

#define MFLAGS CLK_MUX_HIWORD_MASK
#define DFLAGS CLK_DIVIDER_HIWORD_MASK
#define GFLAGS (CLK_GATE_HIWORD_MASK | CLK_GATE_SET_TO_DISABLE)
#define IFLAGS ROCKCHIP_INVERTER_HIWORD_MASK

static struct rockchip_clk_branch rk3399_spdif_fracmux __initdata =
	MUX(0, "clk_spdif_mux", mux_spdif_p, CLK_SET_RATE_PARENT,
			RK3399_CLKSEL_CON(32), 13, 2, MFLAGS);

static struct rockchip_clk_branch rk3399_i2s0_fracmux __initdata =
	MUX(0, "clk_i2s0_mux", mux_i2s0_p, CLK_SET_RATE_PARENT,
			RK3399_CLKSEL_CON(28), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3399_i2s1_fracmux __initdata =
	MUX(0, "clk_i2s1_mux", mux_i2s1_p, CLK_SET_RATE_PARENT,
			RK3399_CLKSEL_CON(29), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3399_i2s2_fracmux __initdata =
	MUX(0, "clk_i2s2_mux", mux_i2s2_p, CLK_SET_RATE_PARENT,
			RK3399_CLKSEL_CON(30), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3399_uart0_fracmux __initdata =
	MUX(SCLK_UART0, "clk_uart0", mux_uart0_p, CLK_SET_RATE_PARENT,
			RK3399_CLKSEL_CON(33), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3399_uart1_fracmux __initdata =
	MUX(SCLK_UART1, "clk_uart1", mux_uart1_p, CLK_SET_RATE_PARENT,
			RK3399_CLKSEL_CON(34), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3399_uart2_fracmux __initdata =
	MUX(SCLK_UART2, "clk_uart2", mux_uart2_p, CLK_SET_RATE_PARENT,
			RK3399_CLKSEL_CON(35), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3399_uart3_fracmux __initdata =
	MUX(SCLK_UART3, "clk_uart3", mux_uart3_p, CLK_SET_RATE_PARENT,
			RK3399_CLKSEL_CON(36), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3399_uart4_pmu_fracmux __initdata =
	MUX(SCLK_UART4_PMU, "clk_uart4_pmu", mux_uart4_pmu_p, CLK_SET_RATE_PARENT,
			RK3399_PMU_CLKSEL_CON(5), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3399_dclk_vop0_fracmux __initdata =
	MUX(DCLK_VOP0, "dclk_vop0", mux_dclk_vop0_p, CLK_SET_RATE_PARENT,
			RK3399_CLKSEL_CON(49), 11, 1, MFLAGS);

static struct rockchip_clk_branch rk3399_dclk_vop1_fracmux __initdata =
	MUX(DCLK_VOP1, "dclk_vop1", mux_dclk_vop1_p, CLK_SET_RATE_PARENT,
			RK3399_CLKSEL_CON(50), 11, 1, MFLAGS);

static struct rockchip_clk_branch rk3399_pmuclk_wifi_fracmux __initdata =
	MUX(SCLK_WIFI_PMU, "clk_wifi_pmu", mux_wifi_pmu_p, CLK_SET_RATE_PARENT,
			RK3399_PMU_CLKSEL_CON(1), 14, 1, MFLAGS);

static const struct rockchip_cpuclk_reg_data rk3399_cpuclkl_data = {
	.core_reg[0] = RK3399_CLKSEL_CON(0),
	.div_core_shift[0] = 0,
	.div_core_mask[0] = 0x1f,
	.num_cores = 1,
	.mux_core_alt = 3,
	.mux_core_main = 0,
	.mux_core_shift = 6,
	.mux_core_mask = 0x3,
};

static const struct rockchip_cpuclk_reg_data rk3399_cpuclkb_data = {
	.core_reg[0] = RK3399_CLKSEL_CON(2),
	.div_core_shift[0] = 0,
	.div_core_mask[0] = 0x1f,
	.num_cores = 1,
	.mux_core_alt = 3,
	.mux_core_main = 1,
	.mux_core_shift = 6,
	.mux_core_mask = 0x3,
};

#define RK3399_DIV_ACLKM_MASK		0x1f
#define RK3399_DIV_ACLKM_SHIFT		8
#define RK3399_DIV_ATCLK_MASK		0x1f
#define RK3399_DIV_ATCLK_SHIFT		0
#define RK3399_DIV_PCLK_DBG_MASK	0x1f
#define RK3399_DIV_PCLK_DBG_SHIFT	8

#define RK3399_CLKSEL0(_offs, _aclkm)					\
	{								\
		.reg = RK3399_CLKSEL_CON(0 + _offs),			\
		.val = HIWORD_UPDATE(_aclkm, RK3399_DIV_ACLKM_MASK,	\
				RK3399_DIV_ACLKM_SHIFT),		\
	}
#define RK3399_CLKSEL1(_offs, _atclk, _pdbg)				\
	{								\
		.reg = RK3399_CLKSEL_CON(1 + _offs),			\
		.val = HIWORD_UPDATE(_atclk, RK3399_DIV_ATCLK_MASK,	\
				RK3399_DIV_ATCLK_SHIFT) |		\
		       HIWORD_UPDATE(_pdbg, RK3399_DIV_PCLK_DBG_MASK,	\
				RK3399_DIV_PCLK_DBG_SHIFT),		\
	}

/* cluster_l: aclkm in clksel0, rest in clksel1 */
#define RK3399_CPUCLKL_RATE(_prate, _aclkm, _atclk, _pdbg)		\
	{								\
		.prate = _prate##U,					\
		.divs = {						\
			RK3399_CLKSEL0(0, _aclkm),			\
			RK3399_CLKSEL1(0, _atclk, _pdbg),		\
		},							\
	}

/* cluster_b: aclkm in clksel2, rest in clksel3 */
#define RK3399_CPUCLKB_RATE(_prate, _aclkm, _atclk, _pdbg)		\
	{								\
		.prate = _prate##U,					\
		.divs = {						\
			RK3399_CLKSEL0(2, _aclkm),			\
			RK3399_CLKSEL1(2, _atclk, _pdbg),		\
		},							\
	}

static struct rockchip_cpuclk_rate_table rk3399_cpuclkl_rates[] __initdata = {
	RK3399_CPUCLKL_RATE(1800000000, 1, 8, 8),
	RK3399_CPUCLKL_RATE(1704000000, 1, 8, 8),
	RK3399_CPUCLKL_RATE(1608000000, 1, 7, 7),
	RK3399_CPUCLKL_RATE(1512000000, 1, 7, 7),
	RK3399_CPUCLKL_RATE(1488000000, 1, 6, 6),
	RK3399_CPUCLKL_RATE(1416000000, 1, 6, 6),
	RK3399_CPUCLKL_RATE(1200000000, 1, 5, 5),
	RK3399_CPUCLKL_RATE(1008000000, 1, 5, 5),
	RK3399_CPUCLKL_RATE( 816000000, 1, 4, 4),
	RK3399_CPUCLKL_RATE( 696000000, 1, 3, 3),
	RK3399_CPUCLKL_RATE( 600000000, 1, 3, 3),
	RK3399_CPUCLKL_RATE( 408000000, 1, 2, 2),
	RK3399_CPUCLKL_RATE( 312000000, 1, 1, 1),
	RK3399_CPUCLKL_RATE( 216000000, 1, 1, 1),
	RK3399_CPUCLKL_RATE(  96000000, 1, 1, 1),
};

static struct rockchip_cpuclk_rate_table rk3399_cpuclkb_rates[] __initdata = {
	RK3399_CPUCLKB_RATE(2208000000, 1, 11, 11),
	RK3399_CPUCLKB_RATE(2184000000, 1, 11, 11),
	RK3399_CPUCLKB_RATE(2088000000, 1, 10, 10),
	RK3399_CPUCLKB_RATE(2040000000, 1, 10, 10),
	RK3399_CPUCLKB_RATE(2016000000, 1, 9, 9),
	RK3399_CPUCLKB_RATE(1992000000, 1, 9, 9),
	RK3399_CPUCLKB_RATE(1896000000, 1, 9, 9),
	RK3399_CPUCLKB_RATE(1800000000, 1, 8, 8),
	RK3399_CPUCLKB_RATE(1704000000, 1, 8, 8),
	RK3399_CPUCLKB_RATE(1608000000, 1, 7, 7),
	RK3399_CPUCLKB_RATE(1512000000, 1, 7, 7),
	RK3399_CPUCLKB_RATE(1488000000, 1, 6, 6),
	RK3399_CPUCLKB_RATE(1416000000, 1, 6, 6),
	RK3399_CPUCLKB_RATE(1200000000, 1, 5, 5),
	RK3399_CPUCLKB_RATE(1008000000, 1, 5, 5),
	RK3399_CPUCLKB_RATE( 816000000, 1, 4, 4),
	RK3399_CPUCLKB_RATE( 696000000, 1, 3, 3),
	RK3399_CPUCLKB_RATE( 600000000, 1, 3, 3),
	RK3399_CPUCLKB_RATE( 408000000, 1, 2, 2),
	RK3399_CPUCLKB_RATE( 312000000, 1, 1, 1),
	RK3399_CPUCLKB_RATE( 216000000, 1, 1, 1),
	RK3399_CPUCLKB_RATE(  96000000, 1, 1, 1),
};

static struct rockchip_clk_branch rk3399_clk_branches[] __initdata = {
	/*
	 * CRU Clock-Architecture
	 */

	/* usbphy */
	GATE(SCLK_USB2PHY0_REF, "clk_usb2phy0_ref", "xin24m", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(6), 5, GFLAGS),
	GATE(SCLK_USB2PHY1_REF, "clk_usb2phy1_ref", "xin24m", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(6), 6, GFLAGS),

	GATE(0, "clk_usbphy0_480m_src", "clk_usbphy0_480m", 0,
			RK3399_CLKGATE_CON(13), 12, GFLAGS),
	GATE(0, "clk_usbphy1_480m_src", "clk_usbphy1_480m", 0,
			RK3399_CLKGATE_CON(13), 12, GFLAGS),
	MUX(0, "clk_usbphy_480m", mux_usbphy_480m_p, 0,
			RK3399_CLKSEL_CON(14), 6, 1, MFLAGS),

	MUX(0, "upll", mux_pll_src_24m_usbphy480m_p, 0,
			RK3399_CLKSEL_CON(14), 15, 1, MFLAGS),

	COMPOSITE_NODIV(SCLK_HSICPHY, "clk_hsicphy", mux_pll_src_cpll_gpll_npll_usbphy480m_p, 0,
			RK3399_CLKSEL_CON(19), 0, 2, MFLAGS,
			RK3399_CLKGATE_CON(6), 4, GFLAGS),

	COMPOSITE(ACLK_USB3, "aclk_usb3", mux_pll_src_cpll_gpll_npll_p, 0,
			RK3399_CLKSEL_CON(39), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3399_CLKGATE_CON(12), 0, GFLAGS),
	GATE(ACLK_USB3_NOC, "aclk_usb3_noc", "aclk_usb3", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(30), 0, GFLAGS),
	GATE(ACLK_USB3OTG0, "aclk_usb3otg0", "aclk_usb3", 0,
			RK3399_CLKGATE_CON(30), 1, GFLAGS),
	GATE(ACLK_USB3OTG1, "aclk_usb3otg1", "aclk_usb3", 0,
			RK3399_CLKGATE_CON(30), 2, GFLAGS),
	GATE(ACLK_USB3_RKSOC_AXI_PERF, "aclk_usb3_rksoc_axi_perf", "aclk_usb3", 0,
			RK3399_CLKGATE_CON(30), 3, GFLAGS),
	GATE(ACLK_USB3_GRF, "aclk_usb3_grf", "aclk_usb3", 0,
			RK3399_CLKGATE_CON(30), 4, GFLAGS),

	GATE(SCLK_USB3OTG0_REF, "clk_usb3otg0_ref", "xin24m", 0,
			RK3399_CLKGATE_CON(12), 1, GFLAGS),
	GATE(SCLK_USB3OTG1_REF, "clk_usb3otg1_ref", "xin24m", 0,
			RK3399_CLKGATE_CON(12), 2, GFLAGS),

	COMPOSITE(SCLK_USB3OTG0_SUSPEND, "clk_usb3otg0_suspend", mux_pll_p, 0,
			RK3399_CLKSEL_CON(40), 15, 1, MFLAGS, 0, 10, DFLAGS,
			RK3399_CLKGATE_CON(12), 3, GFLAGS),

	COMPOSITE(SCLK_USB3OTG1_SUSPEND, "clk_usb3otg1_suspend", mux_pll_p, 0,
			RK3399_CLKSEL_CON(41), 15, 1, MFLAGS, 0, 10, DFLAGS,
			RK3399_CLKGATE_CON(12), 4, GFLAGS),

	COMPOSITE(SCLK_UPHY0_TCPDPHY_REF, "clk_uphy0_tcpdphy_ref", mux_pll_p, 0,
			RK3399_CLKSEL_CON(64), 15, 1, MFLAGS, 8, 5, DFLAGS,
			RK3399_CLKGATE_CON(13), 4, GFLAGS),

	COMPOSITE(SCLK_UPHY0_TCPDCORE, "clk_uphy0_tcpdcore", mux_pll_src_24m_32k_cpll_gpll_p, 0,
			RK3399_CLKSEL_CON(64), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3399_CLKGATE_CON(13), 5, GFLAGS),

	COMPOSITE(SCLK_UPHY1_TCPDPHY_REF, "clk_uphy1_tcpdphy_ref", mux_pll_p, 0,
			RK3399_CLKSEL_CON(65), 15, 1, MFLAGS, 8, 5, DFLAGS,
			RK3399_CLKGATE_CON(13), 6, GFLAGS),

	COMPOSITE(SCLK_UPHY1_TCPDCORE, "clk_uphy1_tcpdcore", mux_pll_src_24m_32k_cpll_gpll_p, 0,
			RK3399_CLKSEL_CON(65), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3399_CLKGATE_CON(13), 7, GFLAGS),

	/* little core */
	GATE(0, "clk_core_l_lpll_src", "lpll", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(0), 0, GFLAGS),
	GATE(0, "clk_core_l_bpll_src", "bpll", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(0), 1, GFLAGS),
	GATE(0, "clk_core_l_dpll_src", "dpll", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(0), 2, GFLAGS),
	GATE(0, "clk_core_l_gpll_src", "gpll", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(0), 3, GFLAGS),

	COMPOSITE_NOMUX(0, "aclkm_core_l", "armclkl", CLK_IGNORE_UNUSED,
			RK3399_CLKSEL_CON(0), 8, 5, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RK3399_CLKGATE_CON(0), 4, GFLAGS),
	COMPOSITE_NOMUX(0, "atclk_core_l", "armclkl", CLK_IGNORE_UNUSED,
			RK3399_CLKSEL_CON(1), 0, 5, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RK3399_CLKGATE_CON(0), 5, GFLAGS),
	COMPOSITE_NOMUX(PCLK_COREDBG_L, "pclk_dbg_core_l", "armclkl", CLK_IGNORE_UNUSED,
			RK3399_CLKSEL_CON(1), 8, 5, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RK3399_CLKGATE_CON(0), 6, GFLAGS),

	GATE(ACLK_CORE_ADB400_CORE_L_2_CCI500, "aclk_core_adb400_core_l_2_cci500", "aclkm_core_l", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(14), 12, GFLAGS),
	GATE(ACLK_PERF_CORE_L, "aclk_perf_core_l", "aclkm_core_l", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(14), 13, GFLAGS),

	GATE(0, "clk_dbg_pd_core_l", "armclkl", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(14), 9, GFLAGS),
	GATE(ACLK_GIC_ADB400_GIC_2_CORE_L, "aclk_core_adb400_gic_2_core_l", "armclkl", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(14), 10, GFLAGS),
	GATE(ACLK_GIC_ADB400_CORE_L_2_GIC, "aclk_core_adb400_core_l_2_gic", "armclkl", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(14), 11, GFLAGS),
	GATE(SCLK_PVTM_CORE_L, "clk_pvtm_core_l", "xin24m", 0,
			RK3399_CLKGATE_CON(0), 7, GFLAGS),

	/* big core */
	GATE(0, "clk_core_b_lpll_src", "lpll", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(1), 0, GFLAGS),
	GATE(0, "clk_core_b_bpll_src", "bpll", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(1), 1, GFLAGS),
	GATE(0, "clk_core_b_dpll_src", "dpll", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(1), 2, GFLAGS),
	GATE(0, "clk_core_b_gpll_src", "gpll", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(1), 3, GFLAGS),

	COMPOSITE_NOMUX(0, "aclkm_core_b", "armclkb", CLK_IGNORE_UNUSED,
			RK3399_CLKSEL_CON(2), 8, 5, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RK3399_CLKGATE_CON(1), 4, GFLAGS),
	COMPOSITE_NOMUX(0, "atclk_core_b", "armclkb", CLK_IGNORE_UNUSED,
			RK3399_CLKSEL_CON(3), 0, 5, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RK3399_CLKGATE_CON(1), 5, GFLAGS),
	COMPOSITE_NOMUX(0, "pclk_dbg_core_b", "armclkb", CLK_IGNORE_UNUSED,
			RK3399_CLKSEL_CON(3), 8, 5, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RK3399_CLKGATE_CON(1), 6, GFLAGS),

	GATE(ACLK_CORE_ADB400_CORE_B_2_CCI500, "aclk_core_adb400_core_b_2_cci500", "aclkm_core_b", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(14), 5, GFLAGS),
	GATE(ACLK_PERF_CORE_B, "aclk_perf_core_b", "aclkm_core_b", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(14), 6, GFLAGS),

	GATE(0, "clk_dbg_pd_core_b", "armclkb", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(14), 1, GFLAGS),
	GATE(ACLK_GIC_ADB400_GIC_2_CORE_B, "aclk_core_adb400_gic_2_core_b", "armclkb", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(14), 3, GFLAGS),
	GATE(ACLK_GIC_ADB400_CORE_B_2_GIC, "aclk_core_adb400_core_b_2_gic", "armclkb", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(14), 4, GFLAGS),

	DIV(PCLK_COREDBG_B, "pclken_dbg_core_b", "pclk_dbg_core_b", CLK_IGNORE_UNUSED,
			RK3399_CLKSEL_CON(3), 13, 2, DFLAGS | CLK_DIVIDER_READ_ONLY),

	GATE(0, "pclk_dbg_cxcs_pd_core_b", "pclk_dbg_core_b", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(14), 2, GFLAGS),

	GATE(SCLK_PVTM_CORE_B, "clk_pvtm_core_b", "xin24m", 0,
			RK3399_CLKGATE_CON(1), 7, GFLAGS),

	/* gmac */
	GATE(0, "cpll_aclk_gmac_src", "cpll", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(6), 9, GFLAGS),
	GATE(0, "gpll_aclk_gmac_src", "gpll", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(6), 8, GFLAGS),
	COMPOSITE(0, "aclk_gmac_pre", mux_aclk_gmac_p, 0,
			RK3399_CLKSEL_CON(20), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RK3399_CLKGATE_CON(6), 10, GFLAGS),

	GATE(ACLK_GMAC, "aclk_gmac", "aclk_gmac_pre", 0,
			RK3399_CLKGATE_CON(32), 0, GFLAGS),
	GATE(ACLK_GMAC_NOC, "aclk_gmac_noc", "aclk_gmac_pre", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(32), 1, GFLAGS),
	GATE(ACLK_PERF_GMAC, "aclk_perf_gmac", "aclk_gmac_pre", 0,
			RK3399_CLKGATE_CON(32), 4, GFLAGS),

	COMPOSITE_NOMUX(0, "pclk_gmac_pre", "aclk_gmac_pre", 0,
			RK3399_CLKSEL_CON(19), 8, 3, DFLAGS,
			RK3399_CLKGATE_CON(6), 11, GFLAGS),
	GATE(PCLK_GMAC, "pclk_gmac", "pclk_gmac_pre", 0,
			RK3399_CLKGATE_CON(32), 2, GFLAGS),
	GATE(PCLK_GMAC_NOC, "pclk_gmac_noc", "pclk_gmac_pre", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(32), 3, GFLAGS),

	COMPOSITE(SCLK_MAC, "clk_gmac", mux_pll_src_cpll_gpll_npll_p, 0,
			RK3399_CLKSEL_CON(20), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RK3399_CLKGATE_CON(5), 5, GFLAGS),

	MUX(SCLK_RMII_SRC, "clk_rmii_src", mux_rmii_p, CLK_SET_RATE_PARENT,
			RK3399_CLKSEL_CON(19), 4, 1, MFLAGS),
	GATE(SCLK_MACREF_OUT, "clk_mac_refout", "clk_rmii_src", 0,
			RK3399_CLKGATE_CON(5), 6, GFLAGS),
	GATE(SCLK_MACREF, "clk_mac_ref", "clk_rmii_src", 0,
			RK3399_CLKGATE_CON(5), 7, GFLAGS),
	GATE(SCLK_MAC_RX, "clk_rmii_rx", "clk_rmii_src", 0,
			RK3399_CLKGATE_CON(5), 8, GFLAGS),
	GATE(SCLK_MAC_TX, "clk_rmii_tx", "clk_rmii_src", 0,
			RK3399_CLKGATE_CON(5), 9, GFLAGS),

	/* spdif */
	COMPOSITE(0, "clk_spdif_div", mux_pll_src_cpll_gpll_p, 0,
			RK3399_CLKSEL_CON(32), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RK3399_CLKGATE_CON(8), 13, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_spdif_frac", "clk_spdif_div", 0,
			RK3399_CLKSEL_CON(99), 0,
			RK3399_CLKGATE_CON(8), 14, GFLAGS,
			&rk3399_spdif_fracmux),
	GATE(SCLK_SPDIF_8CH, "clk_spdif", "clk_spdif_mux", CLK_SET_RATE_PARENT,
			RK3399_CLKGATE_CON(8), 15, GFLAGS),

	COMPOSITE(SCLK_SPDIF_REC_DPTX, "clk_spdif_rec_dptx", mux_pll_src_cpll_gpll_p, 0,
			RK3399_CLKSEL_CON(32), 15, 1, MFLAGS, 8, 5, DFLAGS,
			RK3399_CLKGATE_CON(10), 6, GFLAGS),
	/* i2s */
	COMPOSITE(0, "clk_i2s0_div", mux_pll_src_cpll_gpll_p, 0,
			RK3399_CLKSEL_CON(28), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RK3399_CLKGATE_CON(8), 3, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_i2s0_frac", "clk_i2s0_div", 0,
			RK3399_CLKSEL_CON(96), 0,
			RK3399_CLKGATE_CON(8), 4, GFLAGS,
			&rk3399_i2s0_fracmux),
	GATE(SCLK_I2S0_8CH, "clk_i2s0", "clk_i2s0_mux", CLK_SET_RATE_PARENT,
			RK3399_CLKGATE_CON(8), 5, GFLAGS),

	COMPOSITE(0, "clk_i2s1_div", mux_pll_src_cpll_gpll_p, 0,
			RK3399_CLKSEL_CON(29), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RK3399_CLKGATE_CON(8), 6, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_i2s1_frac", "clk_i2s1_div", 0,
			RK3399_CLKSEL_CON(97), 0,
			RK3399_CLKGATE_CON(8), 7, GFLAGS,
			&rk3399_i2s1_fracmux),
	GATE(SCLK_I2S1_8CH, "clk_i2s1", "clk_i2s1_mux", CLK_SET_RATE_PARENT,
			RK3399_CLKGATE_CON(8), 8, GFLAGS),

	COMPOSITE(0, "clk_i2s2_div", mux_pll_src_cpll_gpll_p, 0,
			RK3399_CLKSEL_CON(30), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RK3399_CLKGATE_CON(8), 9, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_i2s2_frac", "clk_i2s2_div", 0,
			RK3399_CLKSEL_CON(98), 0,
			RK3399_CLKGATE_CON(8), 10, GFLAGS,
			&rk3399_i2s2_fracmux),
	GATE(SCLK_I2S2_8CH, "clk_i2s2", "clk_i2s2_mux", CLK_SET_RATE_PARENT,
			RK3399_CLKGATE_CON(8), 11, GFLAGS),

	MUX(0, "clk_i2sout_src", mux_i2sch_p, CLK_SET_RATE_PARENT,
			RK3399_CLKSEL_CON(31), 0, 2, MFLAGS),
	COMPOSITE_NODIV(SCLK_I2S_8CH_OUT, "clk_i2sout", mux_i2sout_p, CLK_SET_RATE_PARENT,
			RK3399_CLKSEL_CON(31), 2, 1, MFLAGS,
			RK3399_CLKGATE_CON(8), 12, GFLAGS),

	/* uart */
	MUX(0, "clk_uart0_src", mux_pll_src_cpll_gpll_upll_p, 0,
			RK3399_CLKSEL_CON(33), 12, 2, MFLAGS),
	COMPOSITE_NOMUX(0, "clk_uart0_div", "clk_uart0_src", 0,
			RK3399_CLKSEL_CON(33), 0, 7, DFLAGS,
			RK3399_CLKGATE_CON(9), 0, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_uart0_frac", "clk_uart0_div", 0,
			RK3399_CLKSEL_CON(100), 0,
			RK3399_CLKGATE_CON(9), 1, GFLAGS,
			&rk3399_uart0_fracmux),

	MUX(0, "clk_uart_src", mux_pll_src_cpll_gpll_p, 0,
			RK3399_CLKSEL_CON(33), 15, 1, MFLAGS),
	COMPOSITE_NOMUX(0, "clk_uart1_div", "clk_uart_src", 0,
			RK3399_CLKSEL_CON(34), 0, 7, DFLAGS,
			RK3399_CLKGATE_CON(9), 2, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_uart1_frac", "clk_uart1_div", 0,
			RK3399_CLKSEL_CON(101), 0,
			RK3399_CLKGATE_CON(9), 3, GFLAGS,
			&rk3399_uart1_fracmux),

	COMPOSITE_NOMUX(0, "clk_uart2_div", "clk_uart_src", 0,
			RK3399_CLKSEL_CON(35), 0, 7, DFLAGS,
			RK3399_CLKGATE_CON(9), 4, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_uart2_frac", "clk_uart2_div", 0,
			RK3399_CLKSEL_CON(102), 0,
			RK3399_CLKGATE_CON(9), 5, GFLAGS,
			&rk3399_uart2_fracmux),

	COMPOSITE_NOMUX(0, "clk_uart3_div", "clk_uart_src", 0,
			RK3399_CLKSEL_CON(36), 0, 7, DFLAGS,
			RK3399_CLKGATE_CON(9), 6, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_uart3_frac", "clk_uart3_div", 0,
			RK3399_CLKSEL_CON(103), 0,
			RK3399_CLKGATE_CON(9), 7, GFLAGS,
			&rk3399_uart3_fracmux),

	COMPOSITE(PCLK_DDR, "pclk_ddr", mux_pll_src_cpll_gpll_p, CLK_IGNORE_UNUSED,
			RK3399_CLKSEL_CON(6), 15, 1, MFLAGS, 8, 5, DFLAGS,
			RK3399_CLKGATE_CON(3), 4, GFLAGS),

	GATE(PCLK_CENTER_MAIN_NOC, "pclk_center_main_noc", "pclk_ddr", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(18), 10, GFLAGS),
	GATE(PCLK_DDR_MON, "pclk_ddr_mon", "pclk_ddr", 0,
			RK3399_CLKGATE_CON(18), 12, GFLAGS),
	GATE(PCLK_CIC, "pclk_cic", "pclk_ddr", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(18), 15, GFLAGS),
	GATE(PCLK_DDR_SGRF, "pclk_ddr_sgrf", "pclk_ddr", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(19), 2, GFLAGS),

	GATE(SCLK_PVTM_DDR, "clk_pvtm_ddr", "xin24m", 0,
			RK3399_CLKGATE_CON(4), 11, GFLAGS),
	GATE(SCLK_DFIMON0_TIMER, "clk_dfimon0_timer", "xin24m", 0,
			RK3399_CLKGATE_CON(3), 5, GFLAGS),
	GATE(SCLK_DFIMON1_TIMER, "clk_dfimon1_timer", "xin24m", 0,
			RK3399_CLKGATE_CON(3), 6, GFLAGS),

	/* cci */
	GATE(0, "cpll_aclk_cci_src", "cpll", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(2), 0, GFLAGS),
	GATE(0, "gpll_aclk_cci_src", "gpll", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(2), 1, GFLAGS),
	GATE(0, "npll_aclk_cci_src", "npll", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(2), 2, GFLAGS),
	GATE(0, "vpll_aclk_cci_src", "vpll", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(2), 3, GFLAGS),

	COMPOSITE(0, "aclk_cci_pre", mux_aclk_cci_p, CLK_IGNORE_UNUSED,
			RK3399_CLKSEL_CON(5), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3399_CLKGATE_CON(2), 4, GFLAGS),

	GATE(ACLK_ADB400M_PD_CORE_L, "aclk_adb400m_pd_core_l", "aclk_cci_pre", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(15), 0, GFLAGS),
	GATE(ACLK_ADB400M_PD_CORE_B, "aclk_adb400m_pd_core_b", "aclk_cci_pre", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(15), 1, GFLAGS),
	GATE(ACLK_CCI, "aclk_cci", "aclk_cci_pre", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(15), 2, GFLAGS),
	GATE(ACLK_CCI_NOC0, "aclk_cci_noc0", "aclk_cci_pre", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(15), 3, GFLAGS),
	GATE(ACLK_CCI_NOC1, "aclk_cci_noc1", "aclk_cci_pre", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(15), 4, GFLAGS),
	GATE(ACLK_CCI_GRF, "aclk_cci_grf", "aclk_cci_pre", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(15), 7, GFLAGS),

	GATE(0, "cpll_cci_trace", "cpll", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(2), 5, GFLAGS),
	GATE(0, "gpll_cci_trace", "gpll", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(2), 6, GFLAGS),
	COMPOSITE(SCLK_CCI_TRACE, "clk_cci_trace", mux_cci_trace_p, CLK_IGNORE_UNUSED,
			RK3399_CLKSEL_CON(5), 15, 2, MFLAGS, 8, 5, DFLAGS,
			RK3399_CLKGATE_CON(2), 7, GFLAGS),

	GATE(0, "cpll_cs", "cpll", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(2), 8, GFLAGS),
	GATE(0, "gpll_cs", "gpll", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(2), 9, GFLAGS),
	GATE(0, "npll_cs", "npll", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(2), 10, GFLAGS),
	COMPOSITE_NOGATE(0, "clk_cs", mux_cs_p, CLK_IGNORE_UNUSED,
			RK3399_CLKSEL_CON(4), 6, 2, MFLAGS, 0, 5, DFLAGS),
	GATE(0, "clk_dbg_cxcs", "clk_cs", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(15), 5, GFLAGS),
	GATE(0, "clk_dbg_noc", "clk_cs", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(15), 6, GFLAGS),

	/* vcodec */
	COMPOSITE(0, "aclk_vcodec_pre", mux_pll_src_cpll_gpll_npll_ppll_p, 0,
			RK3399_CLKSEL_CON(7), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3399_CLKGATE_CON(4), 0, GFLAGS),
	COMPOSITE_NOMUX(0, "hclk_vcodec_pre", "aclk_vcodec_pre", 0,
			RK3399_CLKSEL_CON(7), 8, 5, DFLAGS,
			RK3399_CLKGATE_CON(4), 1, GFLAGS),
	GATE(HCLK_VCODEC, "hclk_vcodec", "hclk_vcodec_pre", 0,
			RK3399_CLKGATE_CON(17), 2, GFLAGS),
	GATE(0, "hclk_vcodec_noc", "hclk_vcodec_pre", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(17), 3, GFLAGS),

	GATE(ACLK_VCODEC, "aclk_vcodec", "aclk_vcodec_pre", 0,
			RK3399_CLKGATE_CON(17), 0, GFLAGS),
	GATE(0, "aclk_vcodec_noc", "aclk_vcodec_pre", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(17), 1, GFLAGS),

	/* vdu */
	COMPOSITE(SCLK_VDU_CORE, "clk_vdu_core", mux_pll_src_cpll_gpll_npll_p, 0,
			RK3399_CLKSEL_CON(9), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3399_CLKGATE_CON(4), 4, GFLAGS),
	COMPOSITE(SCLK_VDU_CA, "clk_vdu_ca", mux_pll_src_cpll_gpll_npll_p, 0,
			RK3399_CLKSEL_CON(9), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RK3399_CLKGATE_CON(4), 5, GFLAGS),

	COMPOSITE(0, "aclk_vdu_pre", mux_pll_src_cpll_gpll_npll_ppll_p, 0,
			RK3399_CLKSEL_CON(8), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3399_CLKGATE_CON(4), 2, GFLAGS),
	COMPOSITE_NOMUX(0, "hclk_vdu_pre", "aclk_vdu_pre", 0,
			RK3399_CLKSEL_CON(8), 8, 5, DFLAGS,
			RK3399_CLKGATE_CON(4), 3, GFLAGS),
	GATE(HCLK_VDU, "hclk_vdu", "hclk_vdu_pre", 0,
			RK3399_CLKGATE_CON(17), 10, GFLAGS),
	GATE(HCLK_VDU_NOC, "hclk_vdu_noc", "hclk_vdu_pre", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(17), 11, GFLAGS),

	GATE(ACLK_VDU, "aclk_vdu", "aclk_vdu_pre", 0,
			RK3399_CLKGATE_CON(17), 8, GFLAGS),
	GATE(ACLK_VDU_NOC, "aclk_vdu_noc", "aclk_vdu_pre", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(17), 9, GFLAGS),

	/* iep */
	COMPOSITE(0, "aclk_iep_pre", mux_pll_src_cpll_gpll_npll_ppll_p, 0,
			RK3399_CLKSEL_CON(10), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3399_CLKGATE_CON(4), 6, GFLAGS),
	COMPOSITE_NOMUX(0, "hclk_iep_pre", "aclk_iep_pre", 0,
			RK3399_CLKSEL_CON(10), 8, 5, DFLAGS,
			RK3399_CLKGATE_CON(4), 7, GFLAGS),
	GATE(HCLK_IEP, "hclk_iep", "hclk_iep_pre", 0,
			RK3399_CLKGATE_CON(16), 2, GFLAGS),
	GATE(HCLK_IEP_NOC, "hclk_iep_noc", "hclk_iep_pre", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(16), 3, GFLAGS),

	GATE(ACLK_IEP, "aclk_iep", "aclk_iep_pre", 0,
			RK3399_CLKGATE_CON(16), 0, GFLAGS),
	GATE(ACLK_IEP_NOC, "aclk_iep_noc", "aclk_iep_pre", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(16), 1, GFLAGS),

	/* rga */
	COMPOSITE(SCLK_RGA_CORE, "clk_rga_core", mux_pll_src_cpll_gpll_npll_ppll_p, 0,
			RK3399_CLKSEL_CON(12), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3399_CLKGATE_CON(4), 10, GFLAGS),

	COMPOSITE(0, "aclk_rga_pre", mux_pll_src_cpll_gpll_npll_ppll_p, 0,
			RK3399_CLKSEL_CON(11), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3399_CLKGATE_CON(4), 8, GFLAGS),
	COMPOSITE_NOMUX(0, "hclk_rga_pre", "aclk_rga_pre", 0,
			RK3399_CLKSEL_CON(11), 8, 5, DFLAGS,
			RK3399_CLKGATE_CON(4), 9, GFLAGS),
	GATE(HCLK_RGA, "hclk_rga", "hclk_rga_pre", 0,
			RK3399_CLKGATE_CON(16), 10, GFLAGS),
	GATE(HCLK_RGA_NOC, "hclk_rga_noc", "hclk_rga_pre", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(16), 11, GFLAGS),

	GATE(ACLK_RGA, "aclk_rga", "aclk_rga_pre", 0,
			RK3399_CLKGATE_CON(16), 8, GFLAGS),
	GATE(ACLK_RGA_NOC, "aclk_rga_noc", "aclk_rga_pre", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(16), 9, GFLAGS),

	/* center */
	COMPOSITE(0, "aclk_center", mux_pll_src_cpll_gpll_npll_p, CLK_IGNORE_UNUSED,
			RK3399_CLKSEL_CON(12), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RK3399_CLKGATE_CON(3), 7, GFLAGS),
	GATE(ACLK_CENTER_MAIN_NOC, "aclk_center_main_noc", "aclk_center", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(19), 0, GFLAGS),
	GATE(ACLK_CENTER_PERI_NOC, "aclk_center_peri_noc", "aclk_center", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(19), 1, GFLAGS),

	/* gpu */
	COMPOSITE(0, "aclk_gpu_pre", mux_pll_src_ppll_cpll_gpll_npll_p, CLK_IGNORE_UNUSED,
			RK3399_CLKSEL_CON(13), 5, 3, MFLAGS, 0, 5, DFLAGS,
			RK3399_CLKGATE_CON(13), 0, GFLAGS),
	GATE(ACLK_GPU, "aclk_gpu", "aclk_gpu_pre", 0,
			RK3399_CLKGATE_CON(30), 8, GFLAGS),
	GATE(ACLK_PERF_GPU, "aclk_perf_gpu", "aclk_gpu_pre", 0,
			RK3399_CLKGATE_CON(30), 10, GFLAGS),
	GATE(ACLK_GPU_GRF, "aclk_gpu_grf", "aclk_gpu_pre", 0,
			RK3399_CLKGATE_CON(30), 11, GFLAGS),
	GATE(SCLK_PVTM_GPU, "aclk_pvtm_gpu", "xin24m", 0,
			RK3399_CLKGATE_CON(13), 1, GFLAGS),

	/* perihp */
	GATE(0, "cpll_aclk_perihp_src", "cpll", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(5), 1, GFLAGS),
	GATE(0, "gpll_aclk_perihp_src", "gpll", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(5), 0, GFLAGS),
	COMPOSITE(ACLK_PERIHP, "aclk_perihp", mux_aclk_perihp_p, CLK_IGNORE_UNUSED,
			RK3399_CLKSEL_CON(14), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RK3399_CLKGATE_CON(5), 2, GFLAGS),
	COMPOSITE_NOMUX(HCLK_PERIHP, "hclk_perihp", "aclk_perihp", CLK_IGNORE_UNUSED,
			RK3399_CLKSEL_CON(14), 8, 2, DFLAGS,
			RK3399_CLKGATE_CON(5), 3, GFLAGS),
	COMPOSITE_NOMUX(PCLK_PERIHP, "pclk_perihp", "aclk_perihp", CLK_IGNORE_UNUSED,
			RK3399_CLKSEL_CON(14), 12, 2, DFLAGS,
			RK3399_CLKGATE_CON(5), 4, GFLAGS),

	GATE(ACLK_PERF_PCIE, "aclk_perf_pcie", "aclk_perihp", 0,
			RK3399_CLKGATE_CON(20), 2, GFLAGS),
	GATE(ACLK_PCIE, "aclk_pcie", "aclk_perihp", 0,
			RK3399_CLKGATE_CON(20), 10, GFLAGS),
	GATE(0, "aclk_perihp_noc", "aclk_perihp", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(20), 12, GFLAGS),

	GATE(HCLK_HOST0, "hclk_host0", "hclk_perihp", 0,
			RK3399_CLKGATE_CON(20), 5, GFLAGS),
	GATE(HCLK_HOST0_ARB, "hclk_host0_arb", "hclk_perihp", 0,
			RK3399_CLKGATE_CON(20), 6, GFLAGS),
	GATE(HCLK_HOST1, "hclk_host1", "hclk_perihp", 0,
			RK3399_CLKGATE_CON(20), 7, GFLAGS),
	GATE(HCLK_HOST1_ARB, "hclk_host1_arb", "hclk_perihp", 0,
			RK3399_CLKGATE_CON(20), 8, GFLAGS),
	GATE(HCLK_HSIC, "hclk_hsic", "hclk_perihp", 0,
			RK3399_CLKGATE_CON(20), 9, GFLAGS),
	GATE(0, "hclk_perihp_noc", "hclk_perihp", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(20), 13, GFLAGS),
	GATE(0, "hclk_ahb1tom", "hclk_perihp", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(20), 15, GFLAGS),

	GATE(PCLK_PERIHP_GRF, "pclk_perihp_grf", "pclk_perihp", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(20), 4, GFLAGS),
	GATE(PCLK_PCIE, "pclk_pcie", "pclk_perihp", 0,
			RK3399_CLKGATE_CON(20), 11, GFLAGS),
	GATE(0, "pclk_perihp_noc", "pclk_perihp", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(20), 14, GFLAGS),
	GATE(PCLK_HSICPHY, "pclk_hsicphy", "pclk_perihp", 0,
			RK3399_CLKGATE_CON(31), 8, GFLAGS),

	/* sdio & sdmmc */
	COMPOSITE(HCLK_SD, "hclk_sd", mux_pll_src_cpll_gpll_p, 0,
			RK3399_CLKSEL_CON(13), 15, 1, MFLAGS, 8, 5, DFLAGS,
			RK3399_CLKGATE_CON(12), 13, GFLAGS),
	GATE(HCLK_SDMMC, "hclk_sdmmc", "hclk_sd", 0,
			RK3399_CLKGATE_CON(33), 8, GFLAGS),
	GATE(0, "hclk_sdmmc_noc", "hclk_sd", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(33), 9, GFLAGS),

	COMPOSITE(SCLK_SDIO, "clk_sdio", mux_pll_src_cpll_gpll_npll_ppll_upll_24m_p, 0,
			RK3399_CLKSEL_CON(15), 8, 3, MFLAGS, 0, 7, DFLAGS,
			RK3399_CLKGATE_CON(6), 0, GFLAGS),

	COMPOSITE(SCLK_SDMMC, "clk_sdmmc", mux_pll_src_cpll_gpll_npll_ppll_upll_24m_p, 0,
			RK3399_CLKSEL_CON(16), 8, 3, MFLAGS, 0, 7, DFLAGS,
			RK3399_CLKGATE_CON(6), 1, GFLAGS),

	MMC(SCLK_SDMMC_DRV,     "sdmmc_drv",    "clk_sdmmc", RK3399_SDMMC_CON0, 1),
	MMC(SCLK_SDMMC_SAMPLE,  "sdmmc_sample", "clk_sdmmc", RK3399_SDMMC_CON1, 1),

	MMC(SCLK_SDIO_DRV,      "sdio_drv",    "clk_sdio",  RK3399_SDIO_CON0,  1),
	MMC(SCLK_SDIO_SAMPLE,   "sdio_sample", "clk_sdio",  RK3399_SDIO_CON1,  1),

	/* pcie */
	COMPOSITE(SCLK_PCIE_PM, "clk_pcie_pm", mux_pll_src_cpll_gpll_npll_24m_p, 0,
			RK3399_CLKSEL_CON(17), 8, 3, MFLAGS, 0, 7, DFLAGS,
			RK3399_CLKGATE_CON(6), 2, GFLAGS),

	COMPOSITE_NOMUX(SCLK_PCIEPHY_REF100M, "clk_pciephy_ref100m", "npll", 0,
			RK3399_CLKSEL_CON(18), 11, 5, DFLAGS,
			RK3399_CLKGATE_CON(12), 6, GFLAGS),
	MUX(SCLK_PCIEPHY_REF, "clk_pciephy_ref", mux_pll_src_24m_pciephy_p, CLK_SET_RATE_PARENT,
			RK3399_CLKSEL_CON(18), 10, 1, MFLAGS),

	COMPOSITE(0, "clk_pcie_core_cru", mux_pll_src_cpll_gpll_npll_p, 0,
			RK3399_CLKSEL_CON(18), 8, 2, MFLAGS, 0, 7, DFLAGS,
			RK3399_CLKGATE_CON(6), 3, GFLAGS),
	MUX(SCLK_PCIE_CORE, "clk_pcie_core", mux_pciecore_cru_phy_p, CLK_SET_RATE_PARENT,
			RK3399_CLKSEL_CON(18), 7, 1, MFLAGS),

	/* emmc */
	COMPOSITE(SCLK_EMMC, "clk_emmc", mux_pll_src_cpll_gpll_npll_upll_24m_p, 0,
			RK3399_CLKSEL_CON(22), 8, 3, MFLAGS, 0, 7, DFLAGS,
			RK3399_CLKGATE_CON(6), 14, GFLAGS),

	GATE(0, "cpll_aclk_emmc_src", "cpll", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(6), 13, GFLAGS),
	GATE(0, "gpll_aclk_emmc_src", "gpll", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(6), 12, GFLAGS),
	COMPOSITE_NOGATE(ACLK_EMMC, "aclk_emmc", mux_aclk_emmc_p, CLK_IGNORE_UNUSED,
			RK3399_CLKSEL_CON(21), 7, 1, MFLAGS, 0, 5, DFLAGS),
	GATE(ACLK_EMMC_CORE, "aclk_emmccore", "aclk_emmc", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(32), 8, GFLAGS),
	GATE(ACLK_EMMC_NOC, "aclk_emmc_noc", "aclk_emmc", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(32), 9, GFLAGS),
	GATE(ACLK_EMMC_GRF, "aclk_emmcgrf", "aclk_emmc", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(32), 10, GFLAGS),

	/* perilp0 */
	GATE(0, "cpll_aclk_perilp0_src", "cpll", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(7), 1, GFLAGS),
	GATE(0, "gpll_aclk_perilp0_src", "gpll", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(7), 0, GFLAGS),
	COMPOSITE(ACLK_PERILP0, "aclk_perilp0", mux_aclk_perilp0_p, CLK_IGNORE_UNUSED,
			RK3399_CLKSEL_CON(23), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RK3399_CLKGATE_CON(7), 2, GFLAGS),
	COMPOSITE_NOMUX(HCLK_PERILP0, "hclk_perilp0", "aclk_perilp0", CLK_IGNORE_UNUSED,
			RK3399_CLKSEL_CON(23), 8, 2, DFLAGS,
			RK3399_CLKGATE_CON(7), 3, GFLAGS),
	COMPOSITE_NOMUX(PCLK_PERILP0, "pclk_perilp0", "aclk_perilp0", 0,
			RK3399_CLKSEL_CON(23), 12, 3, DFLAGS,
			RK3399_CLKGATE_CON(7), 4, GFLAGS),

	/* aclk_perilp0 gates */
	GATE(ACLK_INTMEM, "aclk_intmem", "aclk_perilp0", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(23), 0, GFLAGS),
	GATE(ACLK_TZMA, "aclk_tzma", "aclk_perilp0", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(23), 1, GFLAGS),
	GATE(SCLK_INTMEM0, "clk_intmem0", "aclk_perilp0", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(23), 2, GFLAGS),
	GATE(SCLK_INTMEM1, "clk_intmem1", "aclk_perilp0", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(23), 3, GFLAGS),
	GATE(SCLK_INTMEM2, "clk_intmem2", "aclk_perilp0", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(23), 4, GFLAGS),
	GATE(SCLK_INTMEM3, "clk_intmem3", "aclk_perilp0", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(23), 5, GFLAGS),
	GATE(SCLK_INTMEM4, "clk_intmem4", "aclk_perilp0", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(23), 6, GFLAGS),
	GATE(SCLK_INTMEM5, "clk_intmem5", "aclk_perilp0", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(23), 7, GFLAGS),
	GATE(ACLK_DCF, "aclk_dcf", "aclk_perilp0", 0, RK3399_CLKGATE_CON(23), 8, GFLAGS),
	GATE(ACLK_DMAC0_PERILP, "aclk_dmac0_perilp", "aclk_perilp0", 0, RK3399_CLKGATE_CON(25), 5, GFLAGS),
	GATE(ACLK_DMAC1_PERILP, "aclk_dmac1_perilp", "aclk_perilp0", 0, RK3399_CLKGATE_CON(25), 6, GFLAGS),
	GATE(ACLK_PERILP0_NOC, "aclk_perilp0_noc", "aclk_perilp0", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(25), 7, GFLAGS),

	/* hclk_perilp0 gates */
	GATE(HCLK_ROM, "hclk_rom", "hclk_perilp0", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(24), 4, GFLAGS),
	GATE(HCLK_M_CRYPTO0, "hclk_m_crypto0", "hclk_perilp0", 0, RK3399_CLKGATE_CON(24), 5, GFLAGS),
	GATE(HCLK_S_CRYPTO0, "hclk_s_crypto0", "hclk_perilp0", 0, RK3399_CLKGATE_CON(24), 6, GFLAGS),
	GATE(HCLK_M_CRYPTO1, "hclk_m_crypto1", "hclk_perilp0", 0, RK3399_CLKGATE_CON(24), 14, GFLAGS),
	GATE(HCLK_S_CRYPTO1, "hclk_s_crypto1", "hclk_perilp0", 0, RK3399_CLKGATE_CON(24), 15, GFLAGS),
	GATE(HCLK_PERILP0_NOC, "hclk_perilp0_noc", "hclk_perilp0", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(25), 8, GFLAGS),

	/* pclk_perilp0 gates */
	GATE(PCLK_DCF, "pclk_dcf", "pclk_perilp0", 0, RK3399_CLKGATE_CON(23), 9, GFLAGS),

	/* crypto */
	COMPOSITE(SCLK_CRYPTO0, "clk_crypto0", mux_pll_src_cpll_gpll_ppll_p, 0,
			RK3399_CLKSEL_CON(24), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3399_CLKGATE_CON(7), 7, GFLAGS),

	COMPOSITE(SCLK_CRYPTO1, "clk_crypto1", mux_pll_src_cpll_gpll_ppll_p, 0,
			RK3399_CLKSEL_CON(26), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3399_CLKGATE_CON(7), 8, GFLAGS),

	/* cm0s_perilp */
	GATE(0, "cpll_fclk_cm0s_src", "cpll", 0,
			RK3399_CLKGATE_CON(7), 6, GFLAGS),
	GATE(0, "gpll_fclk_cm0s_src", "gpll", 0,
			RK3399_CLKGATE_CON(7), 5, GFLAGS),
	COMPOSITE(FCLK_CM0S, "fclk_cm0s", mux_fclk_cm0s_p, 0,
			RK3399_CLKSEL_CON(24), 15, 1, MFLAGS, 8, 5, DFLAGS,
			RK3399_CLKGATE_CON(7), 9, GFLAGS),

	/* fclk_cm0s gates */
	GATE(SCLK_M0_PERILP, "sclk_m0_perilp", "fclk_cm0s", 0, RK3399_CLKGATE_CON(24), 8, GFLAGS),
	GATE(HCLK_M0_PERILP, "hclk_m0_perilp", "fclk_cm0s", 0, RK3399_CLKGATE_CON(24), 9, GFLAGS),
	GATE(DCLK_M0_PERILP, "dclk_m0_perilp", "fclk_cm0s", 0, RK3399_CLKGATE_CON(24), 10, GFLAGS),
	GATE(SCLK_M0_PERILP_DEC, "clk_m0_perilp_dec", "fclk_cm0s", 0, RK3399_CLKGATE_CON(24), 11, GFLAGS),
	GATE(HCLK_M0_PERILP_NOC, "hclk_m0_perilp_noc", "fclk_cm0s", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(25), 11, GFLAGS),

	/* perilp1 */
	GATE(0, "cpll_hclk_perilp1_src", "cpll", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(8), 1, GFLAGS),
	GATE(0, "gpll_hclk_perilp1_src", "gpll", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(8), 0, GFLAGS),
	COMPOSITE_NOGATE(HCLK_PERILP1, "hclk_perilp1", mux_hclk_perilp1_p, CLK_IGNORE_UNUSED,
			RK3399_CLKSEL_CON(25), 7, 1, MFLAGS, 0, 5, DFLAGS),
	COMPOSITE_NOMUX(PCLK_PERILP1, "pclk_perilp1", "hclk_perilp1", CLK_IGNORE_UNUSED,
			RK3399_CLKSEL_CON(25), 8, 3, DFLAGS,
			RK3399_CLKGATE_CON(8), 2, GFLAGS),

	/* hclk_perilp1 gates */
	GATE(0, "hclk_perilp1_noc", "hclk_perilp1", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(25), 9, GFLAGS),
	GATE(0, "hclk_sdio_noc", "hclk_perilp1", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(25), 12, GFLAGS),
	GATE(HCLK_I2S0_8CH, "hclk_i2s0", "hclk_perilp1", 0, RK3399_CLKGATE_CON(34), 0, GFLAGS),
	GATE(HCLK_I2S1_8CH, "hclk_i2s1", "hclk_perilp1", 0, RK3399_CLKGATE_CON(34), 1, GFLAGS),
	GATE(HCLK_I2S2_8CH, "hclk_i2s2", "hclk_perilp1", 0, RK3399_CLKGATE_CON(34), 2, GFLAGS),
	GATE(HCLK_SPDIF, "hclk_spdif", "hclk_perilp1", 0, RK3399_CLKGATE_CON(34), 3, GFLAGS),
	GATE(HCLK_SDIO, "hclk_sdio", "hclk_perilp1", 0, RK3399_CLKGATE_CON(34), 4, GFLAGS),
	GATE(PCLK_SPI5, "pclk_spi5", "hclk_perilp1", 0, RK3399_CLKGATE_CON(34), 5, GFLAGS),
	GATE(0, "hclk_sdioaudio_noc", "hclk_perilp1", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(34), 6, GFLAGS),

	/* pclk_perilp1 gates */
	GATE(PCLK_UART0, "pclk_uart0", "pclk_perilp1", 0, RK3399_CLKGATE_CON(22), 0, GFLAGS),
	GATE(PCLK_UART1, "pclk_uart1", "pclk_perilp1", 0, RK3399_CLKGATE_CON(22), 1, GFLAGS),
	GATE(PCLK_UART2, "pclk_uart2", "pclk_perilp1", 0, RK3399_CLKGATE_CON(22), 2, GFLAGS),
	GATE(PCLK_UART3, "pclk_uart3", "pclk_perilp1", 0, RK3399_CLKGATE_CON(22), 3, GFLAGS),
	GATE(PCLK_I2C7, "pclk_rki2c7", "pclk_perilp1", 0, RK3399_CLKGATE_CON(22), 5, GFLAGS),
	GATE(PCLK_I2C1, "pclk_rki2c1", "pclk_perilp1", 0, RK3399_CLKGATE_CON(22), 6, GFLAGS),
	GATE(PCLK_I2C5, "pclk_rki2c5", "pclk_perilp1", 0, RK3399_CLKGATE_CON(22), 7, GFLAGS),
	GATE(PCLK_I2C6, "pclk_rki2c6", "pclk_perilp1", 0, RK3399_CLKGATE_CON(22), 8, GFLAGS),
	GATE(PCLK_I2C2, "pclk_rki2c2", "pclk_perilp1", 0, RK3399_CLKGATE_CON(22), 9, GFLAGS),
	GATE(PCLK_I2C3, "pclk_rki2c3", "pclk_perilp1", 0, RK3399_CLKGATE_CON(22), 10, GFLAGS),
	GATE(PCLK_MAILBOX0, "pclk_mailbox0", "pclk_perilp1", 0, RK3399_CLKGATE_CON(22), 11, GFLAGS),
	GATE(PCLK_SARADC, "pclk_saradc", "pclk_perilp1", 0, RK3399_CLKGATE_CON(22), 12, GFLAGS),
	GATE(PCLK_TSADC, "pclk_tsadc", "pclk_perilp1", 0, RK3399_CLKGATE_CON(22), 13, GFLAGS),
	GATE(PCLK_EFUSE1024NS, "pclk_efuse1024ns", "pclk_perilp1", 0, RK3399_CLKGATE_CON(22), 14, GFLAGS),
	GATE(PCLK_EFUSE1024S, "pclk_efuse1024s", "pclk_perilp1", 0, RK3399_CLKGATE_CON(22), 15, GFLAGS),
	GATE(PCLK_SPI0, "pclk_spi0", "pclk_perilp1", 0, RK3399_CLKGATE_CON(23), 10, GFLAGS),
	GATE(PCLK_SPI1, "pclk_spi1", "pclk_perilp1", 0, RK3399_CLKGATE_CON(23), 11, GFLAGS),
	GATE(PCLK_SPI2, "pclk_spi2", "pclk_perilp1", 0, RK3399_CLKGATE_CON(23), 12, GFLAGS),
	GATE(PCLK_SPI4, "pclk_spi4", "pclk_perilp1", 0, RK3399_CLKGATE_CON(23), 13, GFLAGS),
	GATE(PCLK_PERIHP_GRF, "pclk_perilp_sgrf", "pclk_perilp1", 0, RK3399_CLKGATE_CON(24), 13, GFLAGS),
	GATE(0, "pclk_perilp1_noc", "pclk_perilp1", 0, RK3399_CLKGATE_CON(25), 10, GFLAGS),

	/* saradc */
	COMPOSITE_NOMUX(SCLK_SARADC, "clk_saradc", "xin24m", 0,
			RK3399_CLKSEL_CON(26), 8, 8, DFLAGS,
			RK3399_CLKGATE_CON(9), 11, GFLAGS),

	/* tsadc */
	COMPOSITE(SCLK_TSADC, "clk_tsadc", mux_pll_p, 0,
			RK3399_CLKSEL_CON(27), 15, 1, MFLAGS, 0, 10, DFLAGS,
			RK3399_CLKGATE_CON(9), 10, GFLAGS),

	/* cif_testout */
	MUX(0, "clk_testout1_pll_src", mux_pll_src_cpll_gpll_npll_p, 0,
			RK3399_CLKSEL_CON(38), 6, 2, MFLAGS),
	COMPOSITE(SCLK_TESTCLKOUT1, "clk_testout1", mux_clk_testout1_p, 0,
			RK3399_CLKSEL_CON(38), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RK3399_CLKGATE_CON(13), 14, GFLAGS),

	MUX(0, "clk_testout2_pll_src", mux_pll_src_cpll_gpll_npll_p, 0,
			RK3399_CLKSEL_CON(38), 14, 2, MFLAGS),
	COMPOSITE(SCLK_TESTCLKOUT2, "clk_testout2", mux_clk_testout2_p, 0,
			RK3399_CLKSEL_CON(38), 13, 1, MFLAGS, 8, 5, DFLAGS,
			RK3399_CLKGATE_CON(13), 15, GFLAGS),

	/* vio */
	COMPOSITE(ACLK_VIO, "aclk_vio", mux_pll_src_cpll_gpll_ppll_p, CLK_IGNORE_UNUSED,
			RK3399_CLKSEL_CON(42), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3399_CLKGATE_CON(11), 0, GFLAGS),
	COMPOSITE_NOMUX(PCLK_VIO, "pclk_vio", "aclk_vio", 0,
			RK3399_CLKSEL_CON(43), 0, 5, DFLAGS,
			RK3399_CLKGATE_CON(11), 1, GFLAGS),

	GATE(ACLK_VIO_NOC, "aclk_vio_noc", "aclk_vio", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(29), 0, GFLAGS),

	GATE(PCLK_MIPI_DSI0, "pclk_mipi_dsi0", "pclk_vio", 0,
			RK3399_CLKGATE_CON(29), 1, GFLAGS),
	GATE(PCLK_MIPI_DSI1, "pclk_mipi_dsi1", "pclk_vio", 0,
			RK3399_CLKGATE_CON(29), 2, GFLAGS),
	GATE(PCLK_VIO_GRF, "pclk_vio_grf", "pclk_vio", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(29), 12, GFLAGS),

	/* hdcp */
	COMPOSITE(ACLK_HDCP, "aclk_hdcp", mux_pll_src_cpll_gpll_ppll_p, 0,
			RK3399_CLKSEL_CON(42), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RK3399_CLKGATE_CON(11), 12, GFLAGS),
	COMPOSITE_NOMUX(HCLK_HDCP, "hclk_hdcp", "aclk_hdcp", 0,
			RK3399_CLKSEL_CON(43), 5, 5, DFLAGS,
			RK3399_CLKGATE_CON(11), 3, GFLAGS),
	COMPOSITE_NOMUX(PCLK_HDCP, "pclk_hdcp", "aclk_hdcp", 0,
			RK3399_CLKSEL_CON(43), 10, 5, DFLAGS,
			RK3399_CLKGATE_CON(11), 10, GFLAGS),

	GATE(ACLK_HDCP_NOC, "aclk_hdcp_noc", "aclk_hdcp", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(29), 4, GFLAGS),
	GATE(ACLK_HDCP22, "aclk_hdcp22", "aclk_hdcp", 0,
			RK3399_CLKGATE_CON(29), 10, GFLAGS),

	GATE(HCLK_HDCP_NOC, "hclk_hdcp_noc", "hclk_hdcp", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(29), 5, GFLAGS),
	GATE(HCLK_HDCP22, "hclk_hdcp22", "hclk_hdcp", 0,
			RK3399_CLKGATE_CON(29), 9, GFLAGS),

	GATE(PCLK_HDCP_NOC, "pclk_hdcp_noc", "pclk_hdcp", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(29), 3, GFLAGS),
	GATE(PCLK_HDMI_CTRL, "pclk_hdmi_ctrl", "pclk_hdcp", 0,
			RK3399_CLKGATE_CON(29), 6, GFLAGS),
	GATE(PCLK_DP_CTRL, "pclk_dp_ctrl", "pclk_hdcp", 0,
			RK3399_CLKGATE_CON(29), 7, GFLAGS),
	GATE(PCLK_HDCP22, "pclk_hdcp22", "pclk_hdcp", 0,
			RK3399_CLKGATE_CON(29), 8, GFLAGS),
	GATE(PCLK_GASKET, "pclk_gasket", "pclk_hdcp", 0,
			RK3399_CLKGATE_CON(29), 11, GFLAGS),

	/* edp */
	COMPOSITE(SCLK_DP_CORE, "clk_dp_core", mux_pll_src_npll_cpll_gpll_p, 0,
			RK3399_CLKSEL_CON(46), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3399_CLKGATE_CON(11), 8, GFLAGS),

	COMPOSITE(PCLK_EDP, "pclk_edp", mux_pll_src_cpll_gpll_p, 0,
			RK3399_CLKSEL_CON(44), 15, 1, MFLAGS, 8, 6, DFLAGS,
			RK3399_CLKGATE_CON(11), 11, GFLAGS),
	GATE(PCLK_EDP_NOC, "pclk_edp_noc", "pclk_edp", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(32), 12, GFLAGS),
	GATE(PCLK_EDP_CTRL, "pclk_edp_ctrl", "pclk_edp", 0,
			RK3399_CLKGATE_CON(32), 13, GFLAGS),

	/* hdmi */
	GATE(SCLK_HDMI_SFR, "clk_hdmi_sfr", "xin24m", 0,
			RK3399_CLKGATE_CON(11), 6, GFLAGS),

	COMPOSITE(SCLK_HDMI_CEC, "clk_hdmi_cec", mux_pll_p, 0,
			RK3399_CLKSEL_CON(45), 15, 1, MFLAGS, 0, 10, DFLAGS,
			RK3399_CLKGATE_CON(11), 7, GFLAGS),

	/* vop0 */
	COMPOSITE(ACLK_VOP0_PRE, "aclk_vop0_pre", mux_pll_src_vpll_cpll_gpll_npll_p, 0,
			RK3399_CLKSEL_CON(47), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3399_CLKGATE_CON(10), 8, GFLAGS),
	COMPOSITE_NOMUX(0, "hclk_vop0_pre", "aclk_vop0_pre", 0,
			RK3399_CLKSEL_CON(47), 8, 5, DFLAGS,
			RK3399_CLKGATE_CON(10), 9, GFLAGS),

	GATE(ACLK_VOP0, "aclk_vop0", "aclk_vop0_pre", 0,
			RK3399_CLKGATE_CON(28), 3, GFLAGS),
	GATE(ACLK_VOP0_NOC, "aclk_vop0_noc", "aclk_vop0_pre", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(28), 1, GFLAGS),

	GATE(HCLK_VOP0, "hclk_vop0", "hclk_vop0_pre", 0,
			RK3399_CLKGATE_CON(28), 2, GFLAGS),
	GATE(HCLK_VOP0_NOC, "hclk_vop0_noc", "hclk_vop0_pre", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(28), 0, GFLAGS),

	COMPOSITE(DCLK_VOP0_DIV, "dclk_vop0_div", mux_pll_src_vpll_cpll_gpll_p, 0,
			RK3399_CLKSEL_CON(49), 8, 2, MFLAGS, 0, 8, DFLAGS,
			RK3399_CLKGATE_CON(10), 12, GFLAGS),

	COMPOSITE_FRACMUX_NOGATE(DCLK_VOP0_FRAC, "dclk_vop0_frac", "dclk_vop0_div", 0,
			RK3399_CLKSEL_CON(106), 0,
			&rk3399_dclk_vop0_fracmux),

	COMPOSITE(SCLK_VOP0_PWM, "clk_vop0_pwm", mux_pll_src_vpll_cpll_gpll_24m_p, 0,
			RK3399_CLKSEL_CON(51), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3399_CLKGATE_CON(10), 14, GFLAGS),

	/* vop1 */
	COMPOSITE(ACLK_VOP1_PRE, "aclk_vop1_pre", mux_pll_src_vpll_cpll_gpll_npll_p, 0,
			RK3399_CLKSEL_CON(48), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3399_CLKGATE_CON(10), 10, GFLAGS),
	COMPOSITE_NOMUX(0, "hclk_vop1_pre", "aclk_vop1_pre", 0,
			RK3399_CLKSEL_CON(48), 8, 5, DFLAGS,
			RK3399_CLKGATE_CON(10), 11, GFLAGS),

	GATE(ACLK_VOP1, "aclk_vop1", "aclk_vop1_pre", 0,
			RK3399_CLKGATE_CON(28), 7, GFLAGS),
	GATE(ACLK_VOP1_NOC, "aclk_vop1_noc", "aclk_vop1_pre", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(28), 5, GFLAGS),

	GATE(HCLK_VOP1, "hclk_vop1", "hclk_vop1_pre", 0,
			RK3399_CLKGATE_CON(28), 6, GFLAGS),
	GATE(HCLK_VOP1_NOC, "hclk_vop1_noc", "hclk_vop1_pre", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(28), 4, GFLAGS),

	COMPOSITE(DCLK_VOP1_DIV, "dclk_vop1_div", mux_pll_src_vpll_cpll_gpll_p, 0,
			RK3399_CLKSEL_CON(50), 8, 2, MFLAGS, 0, 8, DFLAGS,
			RK3399_CLKGATE_CON(10), 13, GFLAGS),

	COMPOSITE_FRACMUX_NOGATE(DCLK_VOP1_FRAC, "dclk_vop1_frac", "dclk_vop1_div", 0,
			RK3399_CLKSEL_CON(107), 0,
			&rk3399_dclk_vop1_fracmux),

	COMPOSITE(SCLK_VOP1_PWM, "clk_vop1_pwm", mux_pll_src_vpll_cpll_gpll_24m_p, CLK_IGNORE_UNUSED,
			RK3399_CLKSEL_CON(52), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3399_CLKGATE_CON(10), 15, GFLAGS),

	/* isp */
	COMPOSITE(ACLK_ISP0, "aclk_isp0", mux_pll_src_cpll_gpll_ppll_p, 0,
			RK3399_CLKSEL_CON(53), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3399_CLKGATE_CON(12), 8, GFLAGS),
	COMPOSITE_NOMUX(HCLK_ISP0, "hclk_isp0", "aclk_isp0", 0,
			RK3399_CLKSEL_CON(53), 8, 5, DFLAGS,
			RK3399_CLKGATE_CON(12), 9, GFLAGS),

	GATE(ACLK_ISP0_NOC, "aclk_isp0_noc", "aclk_isp0", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(27), 1, GFLAGS),
	GATE(ACLK_ISP0_WRAPPER, "aclk_isp0_wrapper", "aclk_isp0", 0,
			RK3399_CLKGATE_CON(27), 5, GFLAGS),
	GATE(HCLK_ISP1_WRAPPER, "hclk_isp1_wrapper", "aclk_isp0", 0,
			RK3399_CLKGATE_CON(27), 7, GFLAGS),

	GATE(HCLK_ISP0_NOC, "hclk_isp0_noc", "hclk_isp0", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(27), 0, GFLAGS),
	GATE(HCLK_ISP0_WRAPPER, "hclk_isp0_wrapper", "hclk_isp0", 0,
			RK3399_CLKGATE_CON(27), 4, GFLAGS),

	COMPOSITE(SCLK_ISP0, "clk_isp0", mux_pll_src_cpll_gpll_npll_p, 0,
			RK3399_CLKSEL_CON(55), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3399_CLKGATE_CON(11), 4, GFLAGS),

	COMPOSITE(ACLK_ISP1, "aclk_isp1", mux_pll_src_cpll_gpll_ppll_p, 0,
			RK3399_CLKSEL_CON(54), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK3399_CLKGATE_CON(12), 10, GFLAGS),
	COMPOSITE_NOMUX(HCLK_ISP1, "hclk_isp1", "aclk_isp1", 0,
			RK3399_CLKSEL_CON(54), 8, 5, DFLAGS,
			RK3399_CLKGATE_CON(12), 11, GFLAGS),

	GATE(ACLK_ISP1_NOC, "aclk_isp1_noc", "aclk_isp1", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(27), 3, GFLAGS),

	GATE(HCLK_ISP1_NOC, "hclk_isp1_noc", "hclk_isp1", CLK_IGNORE_UNUSED,
			RK3399_CLKGATE_CON(27), 2, GFLAGS),
	GATE(ACLK_ISP1_WRAPPER, "aclk_isp1_wrapper", "hclk_isp1", 0,
			RK3399_CLKGATE_CON(27), 8, GFLAGS),

	COMPOSITE(SCLK_ISP1, "clk_isp1", mux_pll_src_cpll_gpll_npll_p, 0,
			RK3399_CLKSEL_CON(55), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RK3399_CLKGATE_CON(11), 5, GFLAGS),

	/*
	 * We use pclkin_cifinv by default GRF_SOC_CON20[9] (GSC20_9) setting in system,
	 * so we ignore the mux and make clocks nodes as following,
	 *
	 * pclkin_cifinv --|-------\
	 *                 |GSC20_9|-- pclkin_cifmux -- |G27_6| -- pclkin_isp1_wrapper
	 * pclkin_cif    --|-------/
	 */
	GATE(PCLK_ISP1_WRAPPER, "pclkin_isp1_wrapper", "pclkin_cif", 0,
			RK3399_CLKGATE_CON(27), 6, GFLAGS),

	/* cif */
	COMPOSITE_NODIV(0, "clk_cifout_src", mux_pll_src_cpll_gpll_npll_p, 0,
			RK3399_CLKSEL_CON(56), 6, 2, MFLAGS,
			RK3399_CLKGATE_CON(10), 7, GFLAGS),

	COMPOSITE_NOGATE(SCLK_CIF_OUT, "clk_cifout", mux_clk_cif_p, 0,
			 RK3399_CLKSEL_CON(56), 5, 1, MFLAGS, 0, 5, DFLAGS),

	/* gic */
	COMPOSITE(ACLK_GIC_PRE, "aclk_gic_pre", mux_pll_src_cpll_gpll_p, CLK_IGNORE_UNUSED,
			RK3399_CLKSEL_CON(56), 15, 1, MFLAGS, 8, 5, DFLAGS,
			RK3399_CLKGATE_CON(12), 12, GFLAGS),

	GATE(ACLK_GIC, "aclk_gic", "aclk_gic_pre", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(33), 0, GFLAGS),
	GATE(ACLK_GIC_NOC, "aclk_gic_noc", "aclk_gic_pre", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(33), 1, GFLAGS),
	GATE(ACLK_GIC_ADB400_CORE_L_2_GIC, "aclk_gic_adb400_core_l_2_gic", "aclk_gic_pre", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(33), 2, GFLAGS),
	GATE(ACLK_GIC_ADB400_CORE_B_2_GIC, "aclk_gic_adb400_core_b_2_gic", "aclk_gic_pre", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(33), 3, GFLAGS),
	GATE(ACLK_GIC_ADB400_GIC_2_CORE_L, "aclk_gic_adb400_gic_2_core_l", "aclk_gic_pre", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(33), 4, GFLAGS),
	GATE(ACLK_GIC_ADB400_GIC_2_CORE_B, "aclk_gic_adb400_gic_2_core_b", "aclk_gic_pre", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(33), 5, GFLAGS),

	/* alive */
	/* pclk_alive_gpll_src is controlled by PMUGRF_SOC_CON0[6] */
	DIV(PCLK_ALIVE, "pclk_alive", "gpll", 0,
			RK3399_CLKSEL_CON(57), 0, 5, DFLAGS),

	GATE(PCLK_USBPHY_MUX_G, "pclk_usbphy_mux_g", "pclk_alive", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(21), 4, GFLAGS),
	GATE(PCLK_UPHY0_TCPHY_G, "pclk_uphy0_tcphy_g", "pclk_alive", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(21), 5, GFLAGS),
	GATE(PCLK_UPHY0_TCPD_G, "pclk_uphy0_tcpd_g", "pclk_alive", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(21), 6, GFLAGS),
	GATE(PCLK_UPHY1_TCPHY_G, "pclk_uphy1_tcphy_g", "pclk_alive", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(21), 8, GFLAGS),
	GATE(PCLK_UPHY1_TCPD_G, "pclk_uphy1_tcpd_g", "pclk_alive", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(21), 9, GFLAGS),

	GATE(PCLK_GRF, "pclk_grf", "pclk_alive", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(31), 1, GFLAGS),
	GATE(PCLK_INTR_ARB, "pclk_intr_arb", "pclk_alive", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(31), 2, GFLAGS),
	GATE(PCLK_GPIO2, "pclk_gpio2", "pclk_alive", 0, RK3399_CLKGATE_CON(31), 3, GFLAGS),
	GATE(PCLK_GPIO3, "pclk_gpio3", "pclk_alive", 0, RK3399_CLKGATE_CON(31), 4, GFLAGS),
	GATE(PCLK_GPIO4, "pclk_gpio4", "pclk_alive", 0, RK3399_CLKGATE_CON(31), 5, GFLAGS),
	GATE(PCLK_TIMER0, "pclk_timer0", "pclk_alive", 0, RK3399_CLKGATE_CON(31), 6, GFLAGS),
	GATE(PCLK_TIMER1, "pclk_timer1", "pclk_alive", 0, RK3399_CLKGATE_CON(31), 7, GFLAGS),
	GATE(PCLK_PMU_INTR_ARB, "pclk_pmu_intr_arb", "pclk_alive", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(31), 9, GFLAGS),
	GATE(PCLK_SGRF, "pclk_sgrf", "pclk_alive", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(31), 10, GFLAGS),

	/* Watchdog pclk is controlled by RK3399 SECURE_GRF_SOC_CON3[8]. */
	SGRF_GATE(PCLK_WDT, "pclk_wdt", "pclk_alive"),

	GATE(SCLK_MIPIDPHY_REF, "clk_mipidphy_ref", "xin24m", 0, RK3399_CLKGATE_CON(11), 14, GFLAGS),
	GATE(SCLK_DPHY_PLL, "clk_dphy_pll", "clk_mipidphy_ref", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(21), 0, GFLAGS),

	GATE(SCLK_MIPIDPHY_CFG, "clk_mipidphy_cfg", "xin24m", 0, RK3399_CLKGATE_CON(11), 15, GFLAGS),
	GATE(SCLK_DPHY_TX0_CFG, "clk_dphy_tx0_cfg", "clk_mipidphy_cfg", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(21), 1, GFLAGS),
	GATE(SCLK_DPHY_TX1RX1_CFG, "clk_dphy_tx1rx1_cfg", "clk_mipidphy_cfg", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(21), 2, GFLAGS),
	GATE(SCLK_DPHY_RX0_CFG, "clk_dphy_rx0_cfg", "clk_mipidphy_cfg", CLK_IGNORE_UNUSED, RK3399_CLKGATE_CON(21), 3, GFLAGS),

	/* testout */
	MUX(0, "clk_test_pre", mux_pll_src_cpll_gpll_p, CLK_SET_RATE_PARENT,
			RK3399_CLKSEL_CON(58), 7, 1, MFLAGS),
	COMPOSITE_FRAC(0, "clk_test_frac", "clk_test_pre", 0,
			RK3399_CLKSEL_CON(105), 0,
			RK3399_CLKGATE_CON(13), 9, GFLAGS),

	DIV(0, "clk_test_24m", "xin24m", 0,
			RK3399_CLKSEL_CON(57), 6, 10, DFLAGS),

	/* spi */
	COMPOSITE(SCLK_SPI0, "clk_spi0", mux_pll_src_cpll_gpll_p, 0,
			RK3399_CLKSEL_CON(59), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RK3399_CLKGATE_CON(9), 12, GFLAGS),

	COMPOSITE(SCLK_SPI1, "clk_spi1", mux_pll_src_cpll_gpll_p, 0,
			RK3399_CLKSEL_CON(59), 15, 1, MFLAGS, 8, 7, DFLAGS,
			RK3399_CLKGATE_CON(9), 13, GFLAGS),

	COMPOSITE(SCLK_SPI2, "clk_spi2", mux_pll_src_cpll_gpll_p, 0,
			RK3399_CLKSEL_CON(60), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RK3399_CLKGATE_CON(9), 14, GFLAGS),

	COMPOSITE(SCLK_SPI4, "clk_spi4", mux_pll_src_cpll_gpll_p, 0,
			RK3399_CLKSEL_CON(60), 15, 1, MFLAGS, 8, 7, DFLAGS,
			RK3399_CLKGATE_CON(9), 15, GFLAGS),

	COMPOSITE(SCLK_SPI5, "clk_spi5", mux_pll_src_cpll_gpll_p, 0,
			RK3399_CLKSEL_CON(58), 15, 1, MFLAGS, 8, 7, DFLAGS,
			RK3399_CLKGATE_CON(13), 13, GFLAGS),

	/* i2c */
	COMPOSITE(SCLK_I2C1, "clk_i2c1", mux_pll_src_cpll_gpll_p, 0,
			RK3399_CLKSEL_CON(61), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RK3399_CLKGATE_CON(10), 0, GFLAGS),

	COMPOSITE(SCLK_I2C2, "clk_i2c2", mux_pll_src_cpll_gpll_p, 0,
			RK3399_CLKSEL_CON(62), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RK3399_CLKGATE_CON(10), 2, GFLAGS),

	COMPOSITE(SCLK_I2C3, "clk_i2c3", mux_pll_src_cpll_gpll_p, 0,
			RK3399_CLKSEL_CON(63), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RK3399_CLKGATE_CON(10), 4, GFLAGS),

	COMPOSITE(SCLK_I2C5, "clk_i2c5", mux_pll_src_cpll_gpll_p, 0,
			RK3399_CLKSEL_CON(61), 15, 1, MFLAGS, 8, 7, DFLAGS,
			RK3399_CLKGATE_CON(10), 1, GFLAGS),

	COMPOSITE(SCLK_I2C6, "clk_i2c6", mux_pll_src_cpll_gpll_p, 0,
			RK3399_CLKSEL_CON(62), 15, 1, MFLAGS, 8, 7, DFLAGS,
			RK3399_CLKGATE_CON(10), 3, GFLAGS),

	COMPOSITE(SCLK_I2C7, "clk_i2c7", mux_pll_src_cpll_gpll_p, 0,
			RK3399_CLKSEL_CON(63), 15, 1, MFLAGS, 8, 7, DFLAGS,
			RK3399_CLKGATE_CON(10), 5, GFLAGS),

	/* timer */
	GATE(SCLK_TIMER00, "clk_timer00", "xin24m", 0, RK3399_CLKGATE_CON(26), 0, GFLAGS),
	GATE(SCLK_TIMER01, "clk_timer01", "xin24m", 0, RK3399_CLKGATE_CON(26), 1, GFLAGS),
	GATE(SCLK_TIMER02, "clk_timer02", "xin24m", 0, RK3399_CLKGATE_CON(26), 2, GFLAGS),
	GATE(SCLK_TIMER03, "clk_timer03", "xin24m", 0, RK3399_CLKGATE_CON(26), 3, GFLAGS),
	GATE(SCLK_TIMER04, "clk_timer04", "xin24m", 0, RK3399_CLKGATE_CON(26), 4, GFLAGS),
	GATE(SCLK_TIMER05, "clk_timer05", "xin24m", 0, RK3399_CLKGATE_CON(26), 5, GFLAGS),
	GATE(SCLK_TIMER06, "clk_timer06", "xin24m", 0, RK3399_CLKGATE_CON(26), 6, GFLAGS),
	GATE(SCLK_TIMER07, "clk_timer07", "xin24m", 0, RK3399_CLKGATE_CON(26), 7, GFLAGS),
	GATE(SCLK_TIMER08, "clk_timer08", "xin24m", 0, RK3399_CLKGATE_CON(26), 8, GFLAGS),
	GATE(SCLK_TIMER09, "clk_timer09", "xin24m", 0, RK3399_CLKGATE_CON(26), 9, GFLAGS),
	GATE(SCLK_TIMER10, "clk_timer10", "xin24m", 0, RK3399_CLKGATE_CON(26), 10, GFLAGS),
	GATE(SCLK_TIMER11, "clk_timer11", "xin24m", 0, RK3399_CLKGATE_CON(26), 11, GFLAGS),

	/* clk_test */
	/* clk_test_pre is controlled by CRU_MISC_CON[3] */
	COMPOSITE_NOMUX(0, "clk_test", "clk_test_pre", CLK_IGNORE_UNUSED,
			RK3399_CLKSEL_CON(58), 0, 5, DFLAGS,
			RK3399_CLKGATE_CON(13), 11, GFLAGS),

	/* ddrc */
	GATE(0, "clk_ddrc_lpll_src", "lpll", 0, RK3399_CLKGATE_CON(3),
	     0, GFLAGS),
	GATE(0, "clk_ddrc_bpll_src", "bpll", 0, RK3399_CLKGATE_CON(3),
	     1, GFLAGS),
	GATE(0, "clk_ddrc_dpll_src", "dpll", 0, RK3399_CLKGATE_CON(3),
	     2, GFLAGS),
	GATE(0, "clk_ddrc_gpll_src", "gpll", 0, RK3399_CLKGATE_CON(3),
	     3, GFLAGS),
	COMPOSITE_DDRCLK(SCLK_DDRC, "sclk_ddrc", mux_ddrclk_p, 0,
		       RK3399_CLKSEL_CON(6), 4, 2, 0, 0, ROCKCHIP_DDRCLK_SIP),
};

static struct rockchip_clk_branch rk3399_clk_pmu_branches[] __initdata = {
	/*
	 * PMU CRU Clock-Architecture
	 */

	GATE(0, "fclk_cm0s_pmu_ppll_src", "ppll", 0,
			RK3399_PMU_CLKGATE_CON(0), 1, GFLAGS),

	COMPOSITE_NOGATE(FCLK_CM0S_SRC_PMU, "fclk_cm0s_src_pmu", mux_fclk_cm0s_pmu_ppll_p, 0,
			RK3399_PMU_CLKSEL_CON(0), 15, 1, MFLAGS, 8, 5, DFLAGS),

	COMPOSITE(SCLK_SPI3_PMU, "clk_spi3_pmu", mux_24m_ppll_p, 0,
			RK3399_PMU_CLKSEL_CON(1), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RK3399_PMU_CLKGATE_CON(0), 2, GFLAGS),

	COMPOSITE(0, "clk_wifi_div", mux_ppll_24m_p, CLK_IGNORE_UNUSED,
			RK3399_PMU_CLKSEL_CON(1), 13, 1, MFLAGS, 8, 5, DFLAGS,
			RK3399_PMU_CLKGATE_CON(0), 8, GFLAGS),

	COMPOSITE_FRACMUX_NOGATE(0, "clk_wifi_frac", "clk_wifi_div", 0,
			RK3399_PMU_CLKSEL_CON(7), 0,
			&rk3399_pmuclk_wifi_fracmux),

	MUX(0, "clk_timer_src_pmu", mux_pll_p, CLK_IGNORE_UNUSED,
			RK3399_PMU_CLKSEL_CON(1), 15, 1, MFLAGS),

	COMPOSITE_NOMUX(SCLK_I2C0_PMU, "clk_i2c0_pmu", "ppll", 0,
			RK3399_PMU_CLKSEL_CON(2), 0, 7, DFLAGS,
			RK3399_PMU_CLKGATE_CON(0), 9, GFLAGS),

	COMPOSITE_NOMUX(SCLK_I2C4_PMU, "clk_i2c4_pmu", "ppll", 0,
			RK3399_PMU_CLKSEL_CON(3), 0, 7, DFLAGS,
			RK3399_PMU_CLKGATE_CON(0), 10, GFLAGS),

	COMPOSITE_NOMUX(SCLK_I2C8_PMU, "clk_i2c8_pmu", "ppll", 0,
			RK3399_PMU_CLKSEL_CON(2), 8, 7, DFLAGS,
			RK3399_PMU_CLKGATE_CON(0), 11, GFLAGS),

	DIV(0, "clk_32k_suspend_pmu", "xin24m", CLK_IGNORE_UNUSED,
			RK3399_PMU_CLKSEL_CON(4), 0, 10, DFLAGS),
	MUX(0, "clk_testout_2io", mux_clk_testout2_2io_p, CLK_IGNORE_UNUSED,
			RK3399_PMU_CLKSEL_CON(4), 15, 1, MFLAGS),

	COMPOSITE(0, "clk_uart4_div", mux_24m_ppll_p, 0,
			RK3399_PMU_CLKSEL_CON(5), 10, 1, MFLAGS, 0, 7, DFLAGS,
			RK3399_PMU_CLKGATE_CON(0), 5, GFLAGS),

	COMPOSITE_FRACMUX(0, "clk_uart4_frac", "clk_uart4_div", 0,
			RK3399_PMU_CLKSEL_CON(6), 0,
			RK3399_PMU_CLKGATE_CON(0), 6, GFLAGS,
			&rk3399_uart4_pmu_fracmux),

	DIV(PCLK_SRC_PMU, "pclk_pmu_src", "ppll", CLK_IGNORE_UNUSED,
			RK3399_PMU_CLKSEL_CON(0), 0, 5, DFLAGS),

	/* pmu clock gates */
	GATE(SCLK_TIMER12_PMU, "clk_timer0_pmu", "clk_timer_src_pmu", 0, RK3399_PMU_CLKGATE_CON(0), 3, GFLAGS),
	GATE(SCLK_TIMER13_PMU, "clk_timer1_pmu", "clk_timer_src_pmu", 0, RK3399_PMU_CLKGATE_CON(0), 4, GFLAGS),

	GATE(SCLK_PVTM_PMU, "clk_pvtm_pmu", "xin24m", CLK_IGNORE_UNUSED, RK3399_PMU_CLKGATE_CON(0), 7, GFLAGS),

	GATE(PCLK_PMU, "pclk_pmu", "pclk_pmu_src", CLK_IGNORE_UNUSED, RK3399_PMU_CLKGATE_CON(1), 0, GFLAGS),
	GATE(PCLK_PMUGRF_PMU, "pclk_pmugrf_pmu", "pclk_pmu_src", CLK_IGNORE_UNUSED, RK3399_PMU_CLKGATE_CON(1), 1, GFLAGS),
	GATE(PCLK_INTMEM1_PMU, "pclk_intmem1_pmu", "pclk_pmu_src", CLK_IGNORE_UNUSED, RK3399_PMU_CLKGATE_CON(1), 2, GFLAGS),
	GATE(PCLK_GPIO0_PMU, "pclk_gpio0_pmu", "pclk_pmu_src", 0, RK3399_PMU_CLKGATE_CON(1), 3, GFLAGS),
	GATE(PCLK_GPIO1_PMU, "pclk_gpio1_pmu", "pclk_pmu_src", 0, RK3399_PMU_CLKGATE_CON(1), 4, GFLAGS),
	GATE(PCLK_SGRF_PMU, "pclk_sgrf_pmu", "pclk_pmu_src", CLK_IGNORE_UNUSED, RK3399_PMU_CLKGATE_CON(1), 5, GFLAGS),
	GATE(PCLK_NOC_PMU, "pclk_noc_pmu", "pclk_pmu_src", CLK_IGNORE_UNUSED, RK3399_PMU_CLKGATE_CON(1), 6, GFLAGS),
	GATE(PCLK_I2C0_PMU, "pclk_i2c0_pmu", "pclk_pmu_src", 0, RK3399_PMU_CLKGATE_CON(1), 7, GFLAGS),
	GATE(PCLK_I2C4_PMU, "pclk_i2c4_pmu", "pclk_pmu_src", 0, RK3399_PMU_CLKGATE_CON(1), 8, GFLAGS),
	GATE(PCLK_I2C8_PMU, "pclk_i2c8_pmu", "pclk_pmu_src", 0, RK3399_PMU_CLKGATE_CON(1), 9, GFLAGS),
	GATE(PCLK_RKPWM_PMU, "pclk_rkpwm_pmu", "pclk_pmu_src", 0, RK3399_PMU_CLKGATE_CON(1), 10, GFLAGS),
	GATE(PCLK_SPI3_PMU, "pclk_spi3_pmu", "pclk_pmu_src", 0, RK3399_PMU_CLKGATE_CON(1), 11, GFLAGS),
	GATE(PCLK_TIMER_PMU, "pclk_timer_pmu", "pclk_pmu_src", 0, RK3399_PMU_CLKGATE_CON(1), 12, GFLAGS),
	GATE(PCLK_MAILBOX_PMU, "pclk_mailbox_pmu", "pclk_pmu_src", 0, RK3399_PMU_CLKGATE_CON(1), 13, GFLAGS),
	GATE(PCLK_UART4_PMU, "pclk_uart4_pmu", "pclk_pmu_src", 0, RK3399_PMU_CLKGATE_CON(1), 14, GFLAGS),
	GATE(PCLK_WDT_M0_PMU, "pclk_wdt_m0_pmu", "pclk_pmu_src", 0, RK3399_PMU_CLKGATE_CON(1), 15, GFLAGS),

	GATE(FCLK_CM0S_PMU, "fclk_cm0s_pmu", "fclk_cm0s_src_pmu", CLK_IGNORE_UNUSED, RK3399_PMU_CLKGATE_CON(2), 0, GFLAGS),
	GATE(SCLK_CM0S_PMU, "sclk_cm0s_pmu", "fclk_cm0s_src_pmu", CLK_IGNORE_UNUSED, RK3399_PMU_CLKGATE_CON(2), 1, GFLAGS),
	GATE(HCLK_CM0S_PMU, "hclk_cm0s_pmu", "fclk_cm0s_src_pmu", CLK_IGNORE_UNUSED, RK3399_PMU_CLKGATE_CON(2), 2, GFLAGS),
	GATE(DCLK_CM0S_PMU, "dclk_cm0s_pmu", "fclk_cm0s_src_pmu", CLK_IGNORE_UNUSED, RK3399_PMU_CLKGATE_CON(2), 3, GFLAGS),
	GATE(HCLK_NOC_PMU, "hclk_noc_pmu", "fclk_cm0s_src_pmu", CLK_IGNORE_UNUSED, RK3399_PMU_CLKGATE_CON(2), 5, GFLAGS),
};

static const char *const rk3399_cru_critical_clocks[] __initconst = {
	"aclk_cci_pre",
	"aclk_gic",
	"aclk_gic_noc",
	"aclk_hdcp_noc",
	"hclk_hdcp_noc",
	"pclk_hdcp_noc",
	"pclk_perilp0",
	"pclk_perilp0",
	"hclk_perilp0",
	"hclk_perilp0_noc",
	"pclk_perilp1",
	"pclk_perilp1_noc",
	"pclk_perihp",
	"pclk_perihp_noc",
	"hclk_perihp",
	"aclk_perihp",
	"aclk_perihp_noc",
	"aclk_perilp0",
	"aclk_perilp0_noc",
	"hclk_perilp1",
	"hclk_perilp1_noc",
	"aclk_dmac0_perilp",
	"aclk_emmc_noc",
	"gpll_hclk_perilp1_src",
	"gpll_aclk_perilp0_src",
	"gpll_aclk_perihp_src",
	"aclk_vio_noc",

	/* ddrc */
	"sclk_ddrc",

	"armclkl",
	"armclkb",
};

static const char *const rk3399_pmucru_critical_clocks[] __initconst = {
	"ppll",
	"pclk_pmu_src",
	"fclk_cm0s_src_pmu",
	"clk_timer_src_pmu",
	"pclk_rkpwm_pmu",
};

static void __init rk3399_clk_init(struct device_node *np)
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

	rockchip_clk_register_plls(ctx, rk3399_pll_clks,
				   ARRAY_SIZE(rk3399_pll_clks), -1);

	rockchip_clk_register_branches(ctx, rk3399_clk_branches,
				  ARRAY_SIZE(rk3399_clk_branches));

	rockchip_clk_register_armclk(ctx, ARMCLKL, "armclkl",
			mux_armclkl_p, ARRAY_SIZE(mux_armclkl_p),
			&rk3399_cpuclkl_data, rk3399_cpuclkl_rates,
			ARRAY_SIZE(rk3399_cpuclkl_rates));

	rockchip_clk_register_armclk(ctx, ARMCLKB, "armclkb",
			mux_armclkb_p, ARRAY_SIZE(mux_armclkb_p),
			&rk3399_cpuclkb_data, rk3399_cpuclkb_rates,
			ARRAY_SIZE(rk3399_cpuclkb_rates));

	rockchip_clk_protect_critical(rk3399_cru_critical_clocks,
				      ARRAY_SIZE(rk3399_cru_critical_clocks));

	rockchip_register_softrst(np, 21, reg_base + RK3399_SOFTRST_CON(0),
				  ROCKCHIP_SOFTRST_HIWORD_MASK);

	rockchip_register_restart_notifier(ctx, RK3399_GLB_SRST_FST, NULL);

	rockchip_clk_of_add_provider(np, ctx);
}
CLK_OF_DECLARE(rk3399_cru, "rockchip,rk3399-cru", rk3399_clk_init);

static void __init rk3399_pmu_clk_init(struct device_node *np)
{
	struct rockchip_clk_provider *ctx;
	void __iomem *reg_base;

	reg_base = of_iomap(np, 0);
	if (!reg_base) {
		pr_err("%s: could not map cru pmu region\n", __func__);
		return;
	}

	ctx = rockchip_clk_init(np, reg_base, CLKPMU_NR_CLKS);
	if (IS_ERR(ctx)) {
		pr_err("%s: rockchip pmu clk init failed\n", __func__);
		iounmap(reg_base);
		return;
	}

	rockchip_clk_register_plls(ctx, rk3399_pmu_pll_clks,
				   ARRAY_SIZE(rk3399_pmu_pll_clks), -1);

	rockchip_clk_register_branches(ctx, rk3399_clk_pmu_branches,
				  ARRAY_SIZE(rk3399_clk_pmu_branches));

	rockchip_clk_protect_critical(rk3399_pmucru_critical_clocks,
				  ARRAY_SIZE(rk3399_pmucru_critical_clocks));

	rockchip_register_softrst(np, 2, reg_base + RK3399_PMU_SOFTRST_CON(0),
				  ROCKCHIP_SOFTRST_HIWORD_MASK);

	rockchip_clk_of_add_provider(np, ctx);
}
CLK_OF_DECLARE(rk3399_cru_pmu, "rockchip,rk3399-pmucru", rk3399_pmu_clk_init);

struct clk_rk3399_inits {
	void (*inits)(struct device_node *np);
};

static const struct clk_rk3399_inits clk_rk3399_pmucru_init = {
	.inits = rk3399_pmu_clk_init,
};

static const struct clk_rk3399_inits clk_rk3399_cru_init = {
	.inits = rk3399_clk_init,
};

static const struct of_device_id clk_rk3399_match_table[] = {
	{
		.compatible = "rockchip,rk3399-cru",
		.data = &clk_rk3399_cru_init,
	},  {
		.compatible = "rockchip,rk3399-pmucru",
		.data = &clk_rk3399_pmucru_init,
	},
	{ }
};

static int __init clk_rk3399_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match;
	const struct clk_rk3399_inits *init_data;

	match = of_match_device(clk_rk3399_match_table, &pdev->dev);
	if (!match || !match->data)
		return -EINVAL;

	init_data = match->data;
	if (init_data->inits)
		init_data->inits(np);

	return 0;
}

static struct platform_driver clk_rk3399_driver = {
	.driver		= {
		.name	= "clk-rk3399",
		.of_match_table = clk_rk3399_match_table,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver_probe(clk_rk3399_driver, clk_rk3399_probe);
