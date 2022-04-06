// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2016 Rockchip Electronics Co. Ltd.
 * Author: Shawn Lin <shawn.lin@rock-chips.com>
 *         Andy Yan <andy.yan@rock-chips.com>
 */

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
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
	RV1108_CPUCLK_RATE(1608000000, 7),
	RV1108_CPUCLK_RATE(1512000000, 7),
	RV1108_CPUCLK_RATE(1488000000, 5),
	RV1108_CPUCLK_RATE(1416000000, 5),
	RV1108_CPUCLK_RATE(1392000000, 5),
	RV1108_CPUCLK_RATE(1296000000, 5),
	RV1108_CPUCLK_RATE(1200000000, 5),
	RV1108_CPUCLK_RATE(1104000000, 5),
	RV1108_CPUCLK_RATE(1008000000, 5),
	RV1108_CPUCLK_RATE(912000000, 5),
	RV1108_CPUCLK_RATE(816000000, 3),
	RV1108_CPUCLK_RATE(696000000, 3),
	RV1108_CPUCLK_RATE(600000000, 3),
	RV1108_CPUCLK_RATE(500000000, 3),
	RV1108_CPUCLK_RATE(408000000, 1),
	RV1108_CPUCLK_RATE(312000000, 1),
	RV1108_CPUCLK_RATE(216000000, 1),
	RV1108_CPUCLK_RATE(96000000, 1),
};

static const struct rockchip_cpuclk_reg_data rv1108_cpuclk_data = {
	.core_reg[0] = RV1108_CLKSEL_CON(0),
	.div_core_shift[0] = 0,
	.div_core_mask[0] = 0x1f,
	.num_cores = 1,
	.mux_core_alt = 1,
	.mux_core_main = 0,
	.mux_core_shift = 8,
	.mux_core_mask = 0x3,
};

PNAME(mux_pll_p)		= { "xin24m", "xin24m"};
PNAME(mux_ddrphy_p)		= { "dpll_ddr", "gpll_ddr", "apll_ddr" };
PNAME(mux_usb480m_pre_p)	= { "usbphy", "xin24m" };
PNAME(mux_hdmiphy_phy_p)	= { "hdmiphy", "xin24m" };
PNAME(mux_dclk_hdmiphy_pre_p)	= { "dclk_hdmiphy_src_gpll", "dclk_hdmiphy_src_dpll" };
PNAME(mux_pll_src_4plls_p)	= { "dpll", "gpll", "hdmiphy", "usb480m" };
PNAME(mux_pll_src_2plls_p)	= { "dpll", "gpll" };
PNAME(mux_pll_src_apll_gpll_p)	= { "apll", "gpll" };
PNAME(mux_aclk_peri_src_p)	= { "aclk_peri_src_gpll", "aclk_peri_src_dpll" };
PNAME(mux_aclk_bus_src_p)	= { "aclk_bus_src_gpll", "aclk_bus_src_apll", "aclk_bus_src_dpll" };
PNAME(mux_mmc_src_p)		= { "dpll", "gpll", "xin24m", "usb480m" };
PNAME(mux_pll_src_dpll_gpll_usb480m_p)	= { "dpll", "gpll", "usb480m" };
PNAME(mux_uart0_p)		= { "uart0_src", "uart0_frac", "xin24m" };
PNAME(mux_uart1_p)		= { "uart1_src", "uart1_frac", "xin24m" };
PNAME(mux_uart2_p)		= { "uart2_src", "uart2_frac", "xin24m" };
PNAME(mux_sclk_mac_p)		= { "sclk_mac_pre", "ext_gmac" };
PNAME(mux_i2s0_pre_p)		= { "i2s0_src", "i2s0_frac", "ext_i2s", "xin12m" };
PNAME(mux_i2s_out_p)		= { "i2s0_pre", "xin12m" };
PNAME(mux_i2s1_p)		= { "i2s1_src", "i2s1_frac", "dummy", "xin12m" };
PNAME(mux_i2s2_p)		= { "i2s2_src", "i2s2_frac", "dummy", "xin12m" };
PNAME(mux_wifi_src_p)		= { "gpll", "xin24m" };
PNAME(mux_cifout_src_p)	= { "hdmiphy", "gpll" };
PNAME(mux_cifout_p)		= { "sclk_cifout_src", "xin24m" };
PNAME(mux_sclk_cif0_src_p)	= { "pclk_vip", "clk_cif0_chn_out", "pclkin_cvbs2cif" };
PNAME(mux_sclk_cif1_src_p)	= { "pclk_vip", "clk_cif1_chn_out", "pclkin_cvbs2cif" };
PNAME(mux_sclk_cif2_src_p)	= { "pclk_vip", "clk_cif2_chn_out", "pclkin_cvbs2cif" };
PNAME(mux_sclk_cif3_src_p)	= { "pclk_vip", "clk_cif3_chn_out", "pclkin_cvbs2cif" };
PNAME(mux_dsp_src_p)		= { "dpll", "gpll", "apll", "usb480m" };
PNAME(mux_dclk_hdmiphy_p)	= { "hdmiphy", "xin24m" };
PNAME(mux_dclk_vop_p)		= { "dclk_hdmiphy", "dclk_vop_src" };
PNAME(mux_hdmi_cec_src_p)		= { "dpll", "gpll", "xin24m" };
PNAME(mux_cvbs_src_p)		= { "apll", "io_cvbs_clkin", "hdmiphy", "gpll" };

static struct rockchip_pll_clock rv1108_pll_clks[] __initdata = {
	[apll] = PLL(pll_rk3399, PLL_APLL, "apll", mux_pll_p, 0, RV1108_PLL_CON(0),
		     RV1108_PLL_CON(3), 8, 0, 0, rv1108_pll_rates),
	[dpll] = PLL(pll_rk3399, PLL_DPLL, "dpll", mux_pll_p, 0, RV1108_PLL_CON(8),
		     RV1108_PLL_CON(11), 8, 1, 0, NULL),
	[gpll] = PLL(pll_rk3399, PLL_GPLL, "gpll", mux_pll_p, 0, RV1108_PLL_CON(16),
		     RV1108_PLL_CON(19), 8, 2, 0, rv1108_pll_rates),
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
	MUX(0, "hdmiphy", mux_hdmiphy_phy_p, CLK_SET_RATE_PARENT,
			RV1108_MISC_CON, 13, 1, MFLAGS),
	MUX(0, "usb480m", mux_usb480m_pre_p, CLK_SET_RATE_PARENT,
			RV1108_MISC_CON, 15, 1, MFLAGS),
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
	GATE(ACLK_CORE, "aclk_core", "aclkenm_core", CLK_IS_CRITICAL,
			RV1108_CLKGATE_CON(11), 0, GFLAGS),
	GATE(0, "pclk_dbg", "pclken_dbg", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(11), 1, GFLAGS),

	/* PD_RKVENC */
	COMPOSITE(0, "aclk_rkvenc_pre", mux_pll_src_4plls_p, 0,
			RV1108_CLKSEL_CON(37), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RV1108_CLKGATE_CON(8), 8, GFLAGS),
	FACTOR_GATE(0, "hclk_rkvenc_pre", "aclk_rkvenc_pre", 0, 1, 4,
			RV1108_CLKGATE_CON(8), 10, GFLAGS),
	COMPOSITE(SCLK_VENC_CORE, "clk_venc_core", mux_pll_src_4plls_p, 0,
			RV1108_CLKSEL_CON(37), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RV1108_CLKGATE_CON(8), 9, GFLAGS),
	GATE(ACLK_RKVENC, "aclk_rkvenc", "aclk_rkvenc_pre", 0,
			RV1108_CLKGATE_CON(19), 8, GFLAGS),
	GATE(HCLK_RKVENC, "hclk_rkvenc", "hclk_rkvenc_pre", 0,
			RV1108_CLKGATE_CON(19), 9, GFLAGS),
	GATE(0, "aclk_rkvenc_niu", "aclk_rkvenc_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(19), 11, GFLAGS),
	GATE(0, "hclk_rkvenc_niu", "hclk_rkvenc_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(19), 10, GFLAGS),

	/* PD_RKVDEC */
	COMPOSITE(SCLK_HEVC_CORE, "sclk_hevc_core", mux_pll_src_4plls_p, 0,
			RV1108_CLKSEL_CON(36), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RV1108_CLKGATE_CON(8), 2, GFLAGS),
	FACTOR_GATE(0, "hclk_rkvdec_pre", "sclk_hevc_core", 0, 1, 4,
			RV1108_CLKGATE_CON(8), 10, GFLAGS),
	COMPOSITE(SCLK_HEVC_CABAC, "clk_hevc_cabac", mux_pll_src_4plls_p, 0,
			RV1108_CLKSEL_CON(35), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RV1108_CLKGATE_CON(8), 1, GFLAGS),

	COMPOSITE(0, "aclk_rkvdec_pre", mux_pll_src_4plls_p, 0,
			RV1108_CLKSEL_CON(35), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RV1108_CLKGATE_CON(8), 0, GFLAGS),
	COMPOSITE(0, "aclk_vpu_pre", mux_pll_src_4plls_p, 0,
			RV1108_CLKSEL_CON(36), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RV1108_CLKGATE_CON(8), 3, GFLAGS),
	GATE(ACLK_RKVDEC, "aclk_rkvdec", "aclk_rkvdec_pre", 0,
			RV1108_CLKGATE_CON(19), 0, GFLAGS),
	GATE(ACLK_VPU, "aclk_vpu", "aclk_vpu_pre", 0,
			RV1108_CLKGATE_CON(19), 1, GFLAGS),
	GATE(HCLK_RKVDEC, "hclk_rkvdec", "hclk_rkvdec_pre", 0,
			RV1108_CLKGATE_CON(19), 2, GFLAGS),
	GATE(HCLK_VPU, "hclk_vpu", "hclk_rkvdec_pre", 0,
			RV1108_CLKGATE_CON(19), 3, GFLAGS),
	GATE(0, "aclk_rkvdec_niu", "aclk_rkvdec_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(19), 4, GFLAGS),
	GATE(0, "hclk_rkvdec_niu", "hclk_rkvdec_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(19), 5, GFLAGS),
	GATE(0, "aclk_vpu_niu", "aclk_vpu_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(19), 6, GFLAGS),

	/* PD_PMU_wrapper */
	COMPOSITE_NOMUX(0, "pmu_24m_ena", "gpll", CLK_IS_CRITICAL,
			RV1108_CLKSEL_CON(38), 0, 5, DFLAGS,
			RV1108_CLKGATE_CON(8), 12, GFLAGS),
	GATE(0, "pclk_pmu", "pmu_24m_ena", CLK_IS_CRITICAL,
			RV1108_CLKGATE_CON(10), 0, GFLAGS),
	GATE(0, "pclk_intmem1", "pmu_24m_ena", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(10), 1, GFLAGS),
	GATE(PCLK_GPIO0_PMU, "pclk_gpio0_pmu", "pmu_24m_ena", 0,
			RV1108_CLKGATE_CON(10), 2, GFLAGS),
	GATE(0, "pclk_pmugrf", "pmu_24m_ena", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(10), 3, GFLAGS),
	GATE(0, "pclk_pmu_niu", "pmu_24m_ena", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(10), 4, GFLAGS),
	GATE(PCLK_I2C0_PMU, "pclk_i2c0_pmu", "pmu_24m_ena", 0,
			RV1108_CLKGATE_CON(10), 5, GFLAGS),
	GATE(PCLK_PWM0_PMU, "pclk_pwm0_pmu", "pmu_24m_ena", 0,
			RV1108_CLKGATE_CON(10), 6, GFLAGS),
	COMPOSITE(SCLK_PWM0_PMU, "sclk_pwm0_pmu", mux_pll_src_2plls_p, 0,
			RV1108_CLKSEL_CON(12), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RV1108_CLKGATE_CON(8), 15, GFLAGS),
	COMPOSITE(SCLK_I2C0_PMU, "sclk_i2c0_pmu", mux_pll_src_2plls_p, 0,
			RV1108_CLKSEL_CON(19), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RV1108_CLKGATE_CON(8), 14, GFLAGS),
	GATE(0, "pvtm_pmu", "xin24m", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(8), 13, GFLAGS),

	/*
	 * Clock-Architecture Diagram 3
	 */
	COMPOSITE(SCLK_WIFI, "sclk_wifi", mux_wifi_src_p, 0,
			RV1108_CLKSEL_CON(28), 15, 1, MFLAGS, 8, 6, DFLAGS,
			RV1108_CLKGATE_CON(9), 8, GFLAGS),
	COMPOSITE_NODIV(0, "sclk_cifout_src", mux_cifout_src_p, 0,
			RV1108_CLKSEL_CON(40), 8, 1, MFLAGS,
			RV1108_CLKGATE_CON(9), 11, GFLAGS),
	COMPOSITE_NOGATE(SCLK_CIFOUT, "sclk_cifout", mux_cifout_p, 0,
			RV1108_CLKSEL_CON(40), 12, 1, MFLAGS, 0, 5, DFLAGS),
	COMPOSITE_NOMUX(SCLK_MIPI_CSI_OUT, "sclk_mipi_csi_out", "xin24m", 0,
			RV1108_CLKSEL_CON(41), 0, 5, DFLAGS,
			RV1108_CLKGATE_CON(9), 12, GFLAGS),

	GATE(0, "pclk_acodecphy", "pclk_top_pre", CLK_IS_CRITICAL,
			RV1108_CLKGATE_CON(14), 6, GFLAGS),
	GATE(0, "pclk_usbgrf", "pclk_top_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(14), 14, GFLAGS),

	GATE(ACLK_CIF0, "aclk_cif0", "aclk_vio1_pre", 0,
			RV1108_CLKGATE_CON(18), 10, GFLAGS),
	GATE(HCLK_CIF0, "hclk_cif0", "hclk_vio_pre", 0,
			RV1108_CLKGATE_CON(18), 10, GFLAGS),
	COMPOSITE_NODIV(SCLK_CIF0, "sclk_cif0", mux_sclk_cif0_src_p, 0,
			RV1108_CLKSEL_CON(31), 0, 2, MFLAGS,
			RV1108_CLKGATE_CON(7), 9, GFLAGS),
	GATE(ACLK_CIF1, "aclk_cif1", "aclk_vio1_pre", 0,
			RV1108_CLKGATE_CON(17), 6, GFLAGS),
	GATE(HCLK_CIF1, "hclk_cif1", "hclk_vio_pre", 0,
			RV1108_CLKGATE_CON(17), 7, GFLAGS),
	COMPOSITE_NODIV(SCLK_CIF1, "sclk_cif1", mux_sclk_cif1_src_p, 0,
			RV1108_CLKSEL_CON(31), 2, 2, MFLAGS,
			RV1108_CLKGATE_CON(7), 10, GFLAGS),
	GATE(ACLK_CIF2, "aclk_cif2", "aclk_vio1_pre", 0,
			RV1108_CLKGATE_CON(17), 8, GFLAGS),
	GATE(HCLK_CIF2, "hclk_cif2", "hclk_vio_pre", 0,
			RV1108_CLKGATE_CON(17), 9, GFLAGS),
	COMPOSITE_NODIV(SCLK_CIF2, "sclk_cif2", mux_sclk_cif2_src_p, 0,
			RV1108_CLKSEL_CON(31), 4, 2, MFLAGS,
			RV1108_CLKGATE_CON(7), 11, GFLAGS),
	GATE(ACLK_CIF3, "aclk_cif3", "aclk_vio1_pre", 0,
			RV1108_CLKGATE_CON(17), 10, GFLAGS),
	GATE(HCLK_CIF3, "hclk_cif3", "hclk_vio_pre", 0,
			RV1108_CLKGATE_CON(17), 11, GFLAGS),
	COMPOSITE_NODIV(SCLK_CIF3, "sclk_cif3", mux_sclk_cif3_src_p, 0,
			RV1108_CLKSEL_CON(31), 6, 2, MFLAGS,
			RV1108_CLKGATE_CON(7), 12, GFLAGS),
	GATE(0, "pclk_cif1to4", "pclk_vip", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(7), 8, GFLAGS),

	/* PD_DSP_wrapper */
	COMPOSITE(SCLK_DSP, "sclk_dsp", mux_dsp_src_p, 0,
			RV1108_CLKSEL_CON(42), 8, 2, MFLAGS, 0, 5, DFLAGS,
			RV1108_CLKGATE_CON(9), 0, GFLAGS),
	GATE(0, "clk_dsp_sys_wd", "sclk_dsp", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(16), 0, GFLAGS),
	GATE(0, "clk_dsp_epp_wd", "sclk_dsp", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(16), 1, GFLAGS),
	GATE(0, "clk_dsp_edp_wd", "sclk_dsp", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(16), 2, GFLAGS),
	GATE(0, "clk_dsp_iop_wd", "sclk_dsp", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(16), 3, GFLAGS),
	GATE(0, "clk_dsp_free", "sclk_dsp", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(16), 13, GFLAGS),
	COMPOSITE_NOMUX(SCLK_DSP_IOP, "sclk_dsp_iop", "sclk_dsp", 0,
			RV1108_CLKSEL_CON(44), 0, 5, DFLAGS,
			RV1108_CLKGATE_CON(9), 1, GFLAGS),
	COMPOSITE_NOMUX(SCLK_DSP_EPP, "sclk_dsp_epp", "sclk_dsp", 0,
			RV1108_CLKSEL_CON(44), 8, 5, DFLAGS,
			RV1108_CLKGATE_CON(9), 2, GFLAGS),
	COMPOSITE_NOMUX(SCLK_DSP_EDP, "sclk_dsp_edp", "sclk_dsp", 0,
			RV1108_CLKSEL_CON(45), 0, 5, DFLAGS,
			RV1108_CLKGATE_CON(9), 3, GFLAGS),
	COMPOSITE_NOMUX(SCLK_DSP_EDAP, "sclk_dsp_edap", "sclk_dsp", 0,
			RV1108_CLKSEL_CON(45), 8, 5, DFLAGS,
			RV1108_CLKGATE_CON(9), 4, GFLAGS),
	GATE(0, "pclk_dsp_iop_niu", "sclk_dsp_iop", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(16), 4, GFLAGS),
	GATE(0, "aclk_dsp_epp_niu", "sclk_dsp_epp", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(16), 5, GFLAGS),
	GATE(0, "aclk_dsp_edp_niu", "sclk_dsp_edp", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(16), 6, GFLAGS),
	GATE(0, "pclk_dsp_dbg_niu", "sclk_dsp", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(16), 7, GFLAGS),
	GATE(0, "aclk_dsp_edap_niu", "sclk_dsp_edap", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(16), 14, GFLAGS),
	COMPOSITE_NOMUX(SCLK_DSP_PFM, "sclk_dsp_pfm", "sclk_dsp", 0,
			RV1108_CLKSEL_CON(43), 0, 5, DFLAGS,
			RV1108_CLKGATE_CON(9), 5, GFLAGS),
	COMPOSITE_NOMUX(PCLK_DSP_CFG, "pclk_dsp_cfg", "sclk_dsp", 0,
			RV1108_CLKSEL_CON(43), 8, 5, DFLAGS,
			RV1108_CLKGATE_CON(9), 6, GFLAGS),
	GATE(0, "pclk_dsp_cfg_niu", "pclk_dsp_cfg", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(16), 8, GFLAGS),
	GATE(0, "pclk_dsp_pfm_mon", "pclk_dsp_cfg", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(16), 9, GFLAGS),
	GATE(0, "pclk_intc", "pclk_dsp_cfg", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(16), 10, GFLAGS),
	GATE(0, "pclk_dsp_grf", "pclk_dsp_cfg", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(16), 11, GFLAGS),
	GATE(0, "pclk_mailbox", "pclk_dsp_cfg", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(16), 12, GFLAGS),
	GATE(0, "aclk_dsp_epp_perf", "sclk_dsp_epp", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(16), 15, GFLAGS),
	GATE(0, "aclk_dsp_edp_perf", "sclk_dsp_edp", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(11), 8, GFLAGS),

	/*
	 * Clock-Architecture Diagram 4
	 */
	COMPOSITE(0, "aclk_vio0_pre", mux_pll_src_4plls_p, CLK_IGNORE_UNUSED,
			RV1108_CLKSEL_CON(28), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RV1108_CLKGATE_CON(6), 0, GFLAGS),
	GATE(ACLK_VIO0, "aclk_vio0", "aclk_vio0_pre", 0,
			RV1108_CLKGATE_CON(17), 0, GFLAGS),
	COMPOSITE_NOMUX(0, "hclk_vio_pre", "aclk_vio0_pre", 0,
			RV1108_CLKSEL_CON(29), 0, 5, DFLAGS,
			RV1108_CLKGATE_CON(7), 2, GFLAGS),
	GATE(HCLK_VIO, "hclk_vio", "hclk_vio_pre", 0,
			RV1108_CLKGATE_CON(17), 2, GFLAGS),
	COMPOSITE_NOMUX(0, "pclk_vio_pre", "aclk_vio0_pre", 0,
			RV1108_CLKSEL_CON(29), 8, 5, DFLAGS,
			RV1108_CLKGATE_CON(7), 3, GFLAGS),
	GATE(PCLK_VIO, "pclk_vio", "pclk_vio_pre", 0,
			RV1108_CLKGATE_CON(17), 3, GFLAGS),
	COMPOSITE(0, "aclk_vio1_pre", mux_pll_src_4plls_p, CLK_IGNORE_UNUSED,
			RV1108_CLKSEL_CON(28), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RV1108_CLKGATE_CON(6), 1, GFLAGS),
	GATE(ACLK_VIO1, "aclk_vio1", "aclk_vio1_pre", 0,
			RV1108_CLKGATE_CON(17), 1, GFLAGS),

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
	COMPOSITE_NOGATE(0, "dclk_hdmiphy_pre", mux_dclk_hdmiphy_pre_p, 0,
			RV1108_CLKSEL_CON(32), 6, 1, MFLAGS, 8, 6, DFLAGS),
	COMPOSITE_NOGATE(DCLK_VOP_SRC, "dclk_vop_src", mux_dclk_hdmiphy_pre_p, 0,
			RV1108_CLKSEL_CON(32), 6, 1, MFLAGS, 0, 6, DFLAGS),
	MUX(DCLK_HDMIPHY, "dclk_hdmiphy", mux_dclk_hdmiphy_p, CLK_SET_RATE_PARENT,
			RV1108_CLKSEL_CON(32), 15, 1, MFLAGS),
	MUX(DCLK_VOP, "dclk_vop", mux_dclk_vop_p, CLK_SET_RATE_PARENT,
			RV1108_CLKSEL_CON(32), 7, 1, MFLAGS),
	GATE(ACLK_VOP, "aclk_vop", "aclk_vio0_pre", 0,
			RV1108_CLKGATE_CON(18), 0, GFLAGS),
	GATE(HCLK_VOP, "hclk_vop", "hclk_vio_pre", 0,
			RV1108_CLKGATE_CON(18), 1, GFLAGS),
	GATE(ACLK_IEP, "aclk_iep", "aclk_vio0_pre", 0,
			RV1108_CLKGATE_CON(18), 2, GFLAGS),
	GATE(HCLK_IEP, "hclk_iep", "hclk_vio_pre", 0,
			RV1108_CLKGATE_CON(18), 3, GFLAGS),

	GATE(ACLK_RGA, "aclk_rga", "aclk_vio1_pre", 0,
			RV1108_CLKGATE_CON(18), 4, GFLAGS),
	GATE(HCLK_RGA, "hclk_rga", "hclk_vio_pre", 0,
			RV1108_CLKGATE_CON(18), 5, GFLAGS),
	COMPOSITE(SCLK_RGA, "sclk_rga", mux_pll_src_4plls_p, 0,
			RV1108_CLKSEL_CON(33), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RV1108_CLKGATE_CON(6), 6, GFLAGS),

	COMPOSITE(SCLK_CVBS_HOST, "sclk_cvbs_host", mux_cvbs_src_p, 0,
			RV1108_CLKSEL_CON(33), 13, 2, MFLAGS, 8, 5, DFLAGS,
			RV1108_CLKGATE_CON(6), 7, GFLAGS),
	FACTOR(0, "sclk_cvbs_27m", "sclk_cvbs_host", 0, 1, 2),

	GATE(SCLK_HDMI_SFR, "sclk_hdmi_sfr", "xin24m", 0,
			RV1108_CLKGATE_CON(6), 8, GFLAGS),

	COMPOSITE(SCLK_HDMI_CEC, "sclk_hdmi_cec", mux_hdmi_cec_src_p, 0,
			RV1108_CLKSEL_CON(34), 14, 2, MFLAGS, 0, 14, DFLAGS,
			RV1108_CLKGATE_CON(6), 9, GFLAGS),
	GATE(PCLK_MIPI_DSI, "pclk_mipi_dsi", "pclk_vio_pre", 0,
			RV1108_CLKGATE_CON(18), 8, GFLAGS),
	GATE(PCLK_HDMI_CTRL, "pclk_hdmi_ctrl", "pclk_vio_pre", 0,
			RV1108_CLKGATE_CON(18), 9, GFLAGS),

	GATE(ACLK_ISP, "aclk_isp", "aclk_vio1_pre", 0,
			RV1108_CLKGATE_CON(18), 12, GFLAGS),
	GATE(HCLK_ISP, "hclk_isp", "hclk_vio_pre", 0,
			RV1108_CLKGATE_CON(18), 11, GFLAGS),
	COMPOSITE(SCLK_ISP, "sclk_isp", mux_pll_src_4plls_p, 0,
			RV1108_CLKSEL_CON(30), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RV1108_CLKGATE_CON(6), 3, GFLAGS),

	GATE(0, "clk_dsiphy24m", "xin24m", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(9), 10, GFLAGS),
	GATE(0, "pclk_vdacphy", "pclk_top_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(14), 9, GFLAGS),
	GATE(0, "pclk_mipi_dsiphy", "pclk_top_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(14), 11, GFLAGS),
	GATE(0, "pclk_mipi_csiphy", "pclk_top_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(14), 12, GFLAGS),

	/*
	 * Clock-Architecture Diagram 5
	 */

	FACTOR(0, "xin12m", "xin24m", 0, 1, 2),


	COMPOSITE(SCLK_I2S0_SRC, "i2s0_src", mux_pll_src_2plls_p, 0,
			RV1108_CLKSEL_CON(5), 8, 1, MFLAGS, 0, 7, DFLAGS,
			RV1108_CLKGATE_CON(2), 0, GFLAGS),
	COMPOSITE_FRACMUX(0, "i2s0_frac", "i2s0_src", CLK_SET_RATE_PARENT,
			RV1108_CLKSEL_CON(8), 0,
			RV1108_CLKGATE_CON(2), 1, GFLAGS,
			&rv1108_i2s0_fracmux),
	GATE(SCLK_I2S0, "sclk_i2s0", "i2s0_pre", CLK_SET_RATE_PARENT,
			RV1108_CLKGATE_CON(2), 2, GFLAGS),
	COMPOSITE_NODIV(0, "i2s_out", mux_i2s_out_p, 0,
			RV1108_CLKSEL_CON(5), 15, 1, MFLAGS,
			RV1108_CLKGATE_CON(2), 3, GFLAGS),

	COMPOSITE(SCLK_I2S1_SRC, "i2s1_src", mux_pll_src_2plls_p, 0,
			RV1108_CLKSEL_CON(6), 8, 1, MFLAGS, 0, 7, DFLAGS,
			RV1108_CLKGATE_CON(2), 4, GFLAGS),
	COMPOSITE_FRACMUX(0, "i2s1_frac", "i2s1_src", CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(9), 0,
			RK2928_CLKGATE_CON(2), 5, GFLAGS,
			&rv1108_i2s1_fracmux),
	GATE(SCLK_I2S1, "sclk_i2s1", "i2s1_pre", CLK_SET_RATE_PARENT,
			RV1108_CLKGATE_CON(2), 6, GFLAGS),

	COMPOSITE(SCLK_I2S2_SRC, "i2s2_src", mux_pll_src_2plls_p, 0,
			RV1108_CLKSEL_CON(7), 8, 1, MFLAGS, 0, 7, DFLAGS,
			RV1108_CLKGATE_CON(3), 8, GFLAGS),
	COMPOSITE_FRACMUX(0, "i2s2_frac", "i2s2_src", CLK_SET_RATE_PARENT,
			RV1108_CLKSEL_CON(10), 0,
			RV1108_CLKGATE_CON(2), 9, GFLAGS,
			&rv1108_i2s2_fracmux),
	GATE(SCLK_I2S2, "sclk_i2s2", "i2s2_pre", CLK_SET_RATE_PARENT,
			RV1108_CLKGATE_CON(2), 10, GFLAGS),

	/* PD_BUS */
	GATE(0, "aclk_bus_src_gpll", "gpll", CLK_IS_CRITICAL,
			RV1108_CLKGATE_CON(1), 0, GFLAGS),
	GATE(0, "aclk_bus_src_apll", "apll", CLK_IS_CRITICAL,
			RV1108_CLKGATE_CON(1), 1, GFLAGS),
	GATE(0, "aclk_bus_src_dpll", "dpll", CLK_IS_CRITICAL,
			RV1108_CLKGATE_CON(1), 2, GFLAGS),
	COMPOSITE_NOGATE(ACLK_PRE, "aclk_bus_pre", mux_aclk_bus_src_p, CLK_IS_CRITICAL,
			RV1108_CLKSEL_CON(2), 8, 2, MFLAGS, 0, 5, DFLAGS),
	COMPOSITE_NOMUX(HCLK_BUS, "hclk_bus_pre", "aclk_bus_pre", CLK_IS_CRITICAL,
			RV1108_CLKSEL_CON(3), 0, 5, DFLAGS,
			RV1108_CLKGATE_CON(1), 4, GFLAGS),
	COMPOSITE_NOMUX(0, "pclk_bus_pre", "aclk_bus_pre", CLK_IS_CRITICAL,
			RV1108_CLKSEL_CON(3), 8, 5, DFLAGS,
			RV1108_CLKGATE_CON(1), 5, GFLAGS),
	GATE(PCLK_BUS, "pclk_bus", "pclk_bus_pre", CLK_IS_CRITICAL,
			RV1108_CLKGATE_CON(1), 6, GFLAGS),
	GATE(0, "pclk_top_pre", "pclk_bus_pre", CLK_IS_CRITICAL,
			RV1108_CLKGATE_CON(1), 7, GFLAGS),
	GATE(0, "pclk_ddr_pre", "pclk_bus_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(1), 8, GFLAGS),
	GATE(SCLK_TIMER0, "clk_timer0", "xin24m", 0,
			RV1108_CLKGATE_CON(1), 9, GFLAGS),
	GATE(SCLK_TIMER1, "clk_timer1", "xin24m", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(1), 10, GFLAGS),
	GATE(PCLK_TIMER, "pclk_timer", "pclk_bus_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(13), 4, GFLAGS),

	GATE(HCLK_I2S0_8CH, "hclk_i2s0_8ch", "hclk_bus_pre", 0,
			RV1108_CLKGATE_CON(12), 7, GFLAGS),
	GATE(HCLK_I2S1_2CH, "hclk_i2s1_2ch", "hclk_bus_pre", 0,
			RV1108_CLKGATE_CON(12), 8, GFLAGS),
	GATE(HCLK_I2S2_2CH, "hclk_i2s2_2ch", "hclk_bus_pre", 0,
			RV1108_CLKGATE_CON(12), 9, GFLAGS),

	GATE(HCLK_CRYPTO_MST, "hclk_crypto_mst", "hclk_bus_pre", 0,
			RV1108_CLKGATE_CON(12), 10, GFLAGS),
	GATE(HCLK_CRYPTO_SLV, "hclk_crypto_slv", "hclk_bus_pre", 0,
			RV1108_CLKGATE_CON(12), 11, GFLAGS),
	COMPOSITE(SCLK_CRYPTO, "sclk_crypto", mux_pll_src_2plls_p, 0,
			RV1108_CLKSEL_CON(11), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RV1108_CLKGATE_CON(2), 12, GFLAGS),

	COMPOSITE(SCLK_SPI, "sclk_spi", mux_pll_src_2plls_p, 0,
			RV1108_CLKSEL_CON(11), 15, 1, MFLAGS, 8, 5, DFLAGS,
			RV1108_CLKGATE_CON(3), 0, GFLAGS),
	GATE(PCLK_SPI, "pclk_spi", "pclk_bus_pre", 0,
			RV1108_CLKGATE_CON(13), 5, GFLAGS),

	COMPOSITE(SCLK_UART0_SRC, "uart0_src", mux_pll_src_dpll_gpll_usb480m_p, CLK_IGNORE_UNUSED,
			RV1108_CLKSEL_CON(13), 12, 2, MFLAGS, 0, 7, DFLAGS,
			RV1108_CLKGATE_CON(3), 1, GFLAGS),
	COMPOSITE(SCLK_UART1_SRC, "uart1_src", mux_pll_src_dpll_gpll_usb480m_p, CLK_IGNORE_UNUSED,
			RV1108_CLKSEL_CON(14), 12, 2, MFLAGS, 0, 7, DFLAGS,
			RV1108_CLKGATE_CON(3), 3, GFLAGS),
	COMPOSITE(SCLK_UART2_SRC, "uart2_src", mux_pll_src_dpll_gpll_usb480m_p, CLK_IGNORE_UNUSED,
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
	GATE(PCLK_UART0, "pclk_uart0", "pclk_bus_pre", 0,
			RV1108_CLKGATE_CON(13), 10, GFLAGS),
	GATE(PCLK_UART1, "pclk_uart1", "pclk_bus_pre", 0,
			RV1108_CLKGATE_CON(13), 11, GFLAGS),
	GATE(PCLK_UART2, "pclk_uart2", "pclk_bus_pre", 0,
			RV1108_CLKGATE_CON(13), 12, GFLAGS),

	COMPOSITE(SCLK_I2C1, "clk_i2c1", mux_pll_src_2plls_p, 0,
			RV1108_CLKSEL_CON(19), 15, 1, MFLAGS, 8, 7, DFLAGS,
			RV1108_CLKGATE_CON(3), 7, GFLAGS),
	COMPOSITE(SCLK_I2C2, "clk_i2c2", mux_pll_src_2plls_p, 0,
			RV1108_CLKSEL_CON(20), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RV1108_CLKGATE_CON(3), 8, GFLAGS),
	COMPOSITE(SCLK_I2C3, "clk_i2c3", mux_pll_src_2plls_p, 0,
			RV1108_CLKSEL_CON(20), 15, 1, MFLAGS, 8, 7, DFLAGS,
			RV1108_CLKGATE_CON(3), 9, GFLAGS),
	GATE(PCLK_I2C1, "pclk_i2c1", "pclk_bus_pre", 0,
			RV1108_CLKGATE_CON(13), 0, GFLAGS),
	GATE(PCLK_I2C2, "pclk_i2c2", "pclk_bus_pre", 0,
			RV1108_CLKGATE_CON(13), 1, GFLAGS),
	GATE(PCLK_I2C3, "pclk_i2c3", "pclk_bus_pre", 0,
			RV1108_CLKGATE_CON(13), 2, GFLAGS),
	COMPOSITE(SCLK_PWM, "clk_pwm", mux_pll_src_2plls_p, 0,
			RV1108_CLKSEL_CON(12), 15, 2, MFLAGS, 8, 7, DFLAGS,
			RV1108_CLKGATE_CON(3), 10, GFLAGS),
	GATE(PCLK_PWM, "pclk_pwm", "pclk_bus_pre", 0,
			RV1108_CLKGATE_CON(13), 6, GFLAGS),
	GATE(PCLK_WDT, "pclk_wdt", "pclk_bus_pre", 0,
			RV1108_CLKGATE_CON(13), 3, GFLAGS),
	GATE(PCLK_GPIO1, "pclk_gpio1", "pclk_bus_pre", 0,
			RV1108_CLKGATE_CON(13), 7, GFLAGS),
	GATE(PCLK_GPIO2, "pclk_gpio2", "pclk_bus_pre", 0,
			RV1108_CLKGATE_CON(13), 8, GFLAGS),
	GATE(PCLK_GPIO3, "pclk_gpio3", "pclk_bus_pre", 0,
			RV1108_CLKGATE_CON(13), 9, GFLAGS),

	GATE(0, "pclk_grf", "pclk_bus_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(14), 0, GFLAGS),
	GATE(PCLK_EFUSE0, "pclk_efuse0", "pclk_bus_pre", 0,
			RV1108_CLKGATE_CON(12), 12, GFLAGS),
	GATE(PCLK_EFUSE1, "pclk_efuse1", "pclk_bus_pre", 0,
			RV1108_CLKGATE_CON(12), 13, GFLAGS),
	GATE(PCLK_TSADC, "pclk_tsadc", "pclk_bus_pre", 0,
			RV1108_CLKGATE_CON(13), 13, GFLAGS),
	COMPOSITE_NOMUX(SCLK_TSADC, "sclk_tsadc", "xin24m", 0,
			RV1108_CLKSEL_CON(21), 0, 10, DFLAGS,
			RV1108_CLKGATE_CON(3), 11, GFLAGS),
	GATE(PCLK_SARADC, "pclk_saradc", "pclk_bus_pre", 0,
			RV1108_CLKGATE_CON(13), 14, GFLAGS),
	COMPOSITE_NOMUX(SCLK_SARADC, "sclk_saradc", "xin24m", 0,
			RV1108_CLKSEL_CON(22), 0, 10, DFLAGS,
			RV1108_CLKGATE_CON(3), 12, GFLAGS),

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
	COMPOSITE_NOGATE(0, "clk_ddrphy_src", mux_ddrphy_p, CLK_IS_CRITICAL,
			RV1108_CLKSEL_CON(4), 8, 2, MFLAGS, 0, 3,
			DFLAGS | CLK_DIVIDER_POWER_OF_TWO),
	FACTOR(0, "clk_ddr", "clk_ddrphy_src", 0, 1, 2),
	GATE(0, "clk_ddrphy4x", "clk_ddr", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(10), 9, GFLAGS),
	GATE(0, "pclk_ddrupctl", "pclk_ddr_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(12), 4, GFLAGS),
	GATE(0, "nclk_ddrupctl", "clk_ddr", CLK_IS_CRITICAL,
			RV1108_CLKGATE_CON(12), 5, GFLAGS),
	GATE(0, "pclk_ddrmon", "pclk_ddr_pre", CLK_IS_CRITICAL,
			RV1108_CLKGATE_CON(12), 6, GFLAGS),
	GATE(0, "timer_clk", "xin24m", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(0), 11, GFLAGS),
	GATE(0, "pclk_mschniu", "pclk_ddr_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(14), 2, GFLAGS),
	GATE(0, "pclk_ddrphy", "pclk_ddr_pre", CLK_IGNORE_UNUSED,
			RV1108_CLKGATE_CON(14), 4, GFLAGS),

	/*
	 * Clock-Architecture Diagram 6
	 */

	/* PD_PERI */
	COMPOSITE_NOMUX(0, "pclk_periph_pre", "gpll", CLK_IS_CRITICAL,
			RV1108_CLKSEL_CON(23), 10, 5, DFLAGS,
			RV1108_CLKGATE_CON(4), 5, GFLAGS),
	GATE(PCLK_PERI, "pclk_periph", "pclk_periph_pre", CLK_IS_CRITICAL,
			RV1108_CLKGATE_CON(15), 13, GFLAGS),
	COMPOSITE_NOMUX(0, "hclk_periph_pre", "gpll", CLK_IS_CRITICAL,
			RV1108_CLKSEL_CON(23), 5, 5, DFLAGS,
			RV1108_CLKGATE_CON(4), 4, GFLAGS),
	GATE(HCLK_PERI, "hclk_periph", "hclk_periph_pre", CLK_IS_CRITICAL,
			RV1108_CLKGATE_CON(15), 12, GFLAGS),

	GATE(0, "aclk_peri_src_dpll", "dpll", CLK_IS_CRITICAL,
			RV1108_CLKGATE_CON(4), 1, GFLAGS),
	GATE(0, "aclk_peri_src_gpll", "gpll", CLK_IS_CRITICAL,
			RV1108_CLKGATE_CON(4), 2, GFLAGS),
	COMPOSITE(ACLK_PERI, "aclk_periph", mux_aclk_peri_src_p, CLK_IS_CRITICAL,
			RV1108_CLKSEL_CON(23), 15, 1, MFLAGS, 0, 5, DFLAGS,
			RV1108_CLKGATE_CON(15), 11, GFLAGS),

	COMPOSITE(SCLK_SDMMC, "sclk_sdmmc", mux_mmc_src_p, 0,
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
			RV1108_CLKSEL_CON(27), 14, 1, MFLAGS, 8, 5, DFLAGS,
			RV1108_CLKGATE_CON(5), 3, GFLAGS),
	GATE(HCLK_NANDC, "hclk_nandc", "hclk_periph", 0, RV1108_CLKGATE_CON(15), 3, GFLAGS),

	GATE(HCLK_HOST0, "hclk_host0", "hclk_periph", 0, RV1108_CLKGATE_CON(15), 6, GFLAGS),
	GATE(0, "hclk_host0_arb", "hclk_periph", CLK_IGNORE_UNUSED, RV1108_CLKGATE_CON(15), 7, GFLAGS),
	GATE(HCLK_OTG, "hclk_otg", "hclk_periph", 0, RV1108_CLKGATE_CON(15), 8, GFLAGS),
	GATE(0, "hclk_otg_pmu", "hclk_periph", CLK_IGNORE_UNUSED, RV1108_CLKGATE_CON(15), 9, GFLAGS),
	GATE(SCLK_USBPHY, "clk_usbphy", "xin24m", CLK_IGNORE_UNUSED, RV1108_CLKGATE_CON(5), 5, GFLAGS),

	COMPOSITE(SCLK_SFC, "sclk_sfc", mux_pll_src_2plls_p, 0,
			RV1108_CLKSEL_CON(27), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RV1108_CLKGATE_CON(5), 4, GFLAGS),
	GATE(HCLK_SFC, "hclk_sfc", "hclk_periph", 0, RV1108_CLKGATE_CON(15), 10, GFLAGS),

	COMPOSITE(SCLK_MAC_PRE, "sclk_mac_pre", mux_pll_src_apll_gpll_p, 0,
			RV1108_CLKSEL_CON(24), 12, 1, MFLAGS, 0, 5, DFLAGS,
			RV1108_CLKGATE_CON(4), 10, GFLAGS),
	MUX(SCLK_MAC, "sclk_mac", mux_sclk_mac_p, CLK_SET_RATE_PARENT,
			RV1108_CLKSEL_CON(24), 8, 1, MFLAGS),
	GATE(SCLK_MAC_RX, "sclk_mac_rx", "sclk_mac", 0, RV1108_CLKGATE_CON(4), 8, GFLAGS),
	GATE(SCLK_MAC_REF, "sclk_mac_ref", "sclk_mac", 0, RV1108_CLKGATE_CON(4), 6, GFLAGS),
	GATE(SCLK_MAC_REFOUT, "sclk_mac_refout", "sclk_mac", 0, RV1108_CLKGATE_CON(4), 7, GFLAGS),
	GATE(ACLK_GMAC, "aclk_gmac", "aclk_periph", 0, RV1108_CLKGATE_CON(15), 4, GFLAGS),
	GATE(PCLK_GMAC, "pclk_gmac", "pclk_periph", 0, RV1108_CLKGATE_CON(15), 5, GFLAGS),

	MMC(SCLK_SDMMC_DRV,    "sdmmc_drv",    "sclk_sdmmc", RV1108_SDMMC_CON0, 1),
	MMC(SCLK_SDMMC_SAMPLE, "sdmmc_sample", "sclk_sdmmc", RV1108_SDMMC_CON1, 1),

	MMC(SCLK_SDIO_DRV,     "sdio_drv",     "sclk_sdio",  RV1108_SDIO_CON0,  1),
	MMC(SCLK_SDIO_SAMPLE,  "sdio_sample",  "sclk_sdio",  RV1108_SDIO_CON1,  1),

	MMC(SCLK_EMMC_DRV,     "emmc_drv",     "sclk_emmc",  RV1108_EMMC_CON0,  1),
	MMC(SCLK_EMMC_SAMPLE,  "emmc_sample",  "sclk_emmc",  RV1108_EMMC_CON1,  1),
};

static void __iomem *rv1108_cru_base;

static void rv1108_dump_cru(void)
{
	if (rv1108_cru_base) {
		pr_warn("CRU:\n");
		print_hex_dump(KERN_WARNING, "", DUMP_PREFIX_OFFSET,
			       32, 4, rv1108_cru_base,
			       0x1f8, false);
	}
}

static void __init rv1108_clk_init(struct device_node *np)
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

	rockchip_clk_register_plls(ctx, rv1108_pll_clks,
				   ARRAY_SIZE(rv1108_pll_clks),
				   RV1108_GRF_SOC_STATUS0);
	rockchip_clk_register_branches(ctx, rv1108_clk_branches,
				  ARRAY_SIZE(rv1108_clk_branches));

	rockchip_clk_register_armclk(ctx, ARMCLK, "armclk",
			3, clks[PLL_APLL], clks[PLL_GPLL],
			&rv1108_cpuclk_data, rv1108_cpuclk_rates,
			ARRAY_SIZE(rv1108_cpuclk_rates));

	rockchip_register_softrst(np, 13, reg_base + RV1108_SOFTRST_CON(0),
				  ROCKCHIP_SOFTRST_HIWORD_MASK);

	rockchip_register_restart_notifier(ctx, RV1108_GLB_SRST_FST, NULL);

	rockchip_clk_of_add_provider(np, ctx);

	if (!rk_dump_cru) {
		rv1108_cru_base = reg_base;
		rk_dump_cru = rv1108_dump_cru;
	}
}
CLK_OF_DECLARE(rv1108_cru, "rockchip,rv1108-cru", rv1108_clk_init);

static int __init clk_rv1108_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;

	rv1108_clk_init(np);

	return 0;
}

static const struct of_device_id clk_rv1108_match_table[] = {
	{
		.compatible = "rockchip,rv1108-cru",
	},
	{ }
};
MODULE_DEVICE_TABLE(of, clk_rv1108_match_table);

static struct platform_driver clk_rv1108_driver = {
	.driver		= {
		.name	= "clk-rv1108",
		.of_match_table = clk_rv1108_match_table,
	},
};
builtin_platform_driver_probe(clk_rv1108_driver, clk_rv1108_probe);

MODULE_DESCRIPTION("Rockchip RV1108 Clock Driver");
MODULE_LICENSE("GPL");
