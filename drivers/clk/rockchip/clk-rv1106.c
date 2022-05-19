// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 * Author: Elaine Zhang <zhangqing@rock-chips.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/syscore_ops.h>
#include <dt-bindings/clock/rv1106-cru.h>
#include "clk.h"

#define CRU_PVTPLL0_CON0_L		0x11000
#define CRU_PVTPLL0_CON0_H		0x11004
#define CRU_PVTPLL0_CON1_L		0x11008
#define CRU_PVTPLL0_CON1_H		0x1100c
#define CRU_PVTPLL0_CON2_L		0x11010
#define CRU_PVTPLL0_CON2_H		0x11014
#define CRU_PVTPLL0_CON3_L		0x11018
#define CRU_PVTPLL0_CON3_H		0x1101c
#define CRU_PVTPLL0_OSC_CNT		0x11020
#define CRU_PVTPLL0_OSC_CNT_AVG		0x11024

#define CRU_PVTPLL1_CON0_L		0x11030
#define CRU_PVTPLL1_CON0_H		0x11034
#define CRU_PVTPLL1_CON1_L		0x11038
#define CRU_PVTPLL1_CON1_H		0x1103c
#define CRU_PVTPLL1_CON2_L		0x11040
#define CRU_PVTPLL1_CON2_H		0x11044
#define CRU_PVTPLL1_CON3_L		0x11048
#define CRU_PVTPLL1_CON3_H		0x1104c
#define CRU_PVTPLL1_OSC_CNT		0x11050
#define CRU_PVTPLL1_OSC_CNT_AVG		0x11054

#define RV1106_GRF_SOC_STATUS0		0x10
#define CPU_PVTPLL_CON0_L		0x40000
#define CPU_PVTPLL_CON0_H		0x40004
#define CPU_PVTPLL_CON1			0x40008
#define CPU_PVTPLL_CON2			0x4000c
#define CPU_PVTPLL_CON3			0x40010
#define CPU_PVTPLL_OSC_CNT		0x40018
#define CPU_PVTPLL_OSC_CNT_AVG		0x4001c

#define PVTPLL_RING_SEL_MASK		0x7
#define PVTPLL_RING_SEL_SHIFT		8
#define PVTPLL_EN_MASK			0x3
#define PVTPLL_EN_SHIFT			0
#define PVTPLL_LENGTH_SEL_MASK		0x7f
#define PVTPLL_LENGTH_SEL_SHIFT		0

#define CPU_CLK_PATH_BASE		(0x18300)
#define CPU_PVTPLL_PATH_CORE		((1 << 12) | (1 << 28))

#define RV1106_FRAC_MAX_PRATE		1200000000

enum rv1106_plls {
	apll, dpll, cpll, gpll,
};

static struct rockchip_pll_rate_table rv1106_pll_rates[] = {
	/* _mhz, _refdiv, _fbdiv, _postdiv1, _postdiv2, _dsmpd, _frac */
	RK3036_PLL_RATE(1608000000, 1, 67, 1, 1, 1, 0),
	RK3036_PLL_RATE(1600000000, 3, 200, 1, 1, 1, 0),
	RK3036_PLL_RATE(1584000000, 1, 132, 2, 1, 1, 0),
	RK3036_PLL_RATE(1560000000, 1, 130, 2, 1, 1, 0),
	RK3036_PLL_RATE(1536000000, 1, 128, 2, 1, 1, 0),
	RK3036_PLL_RATE(1512000000, 1, 126, 2, 1, 1, 0),
	RK3036_PLL_RATE(1488000000, 1, 124, 2, 1, 1, 0),
	RK3036_PLL_RATE(1464000000, 1, 122, 2, 1, 1, 0),
	RK3036_PLL_RATE(1440000000, 1, 120, 2, 1, 1, 0),
	RK3036_PLL_RATE(1416000000, 1, 118, 2, 1, 1, 0),
	RK3036_PLL_RATE(1400000000, 3, 350, 2, 1, 1, 0),
	RK3036_PLL_RATE(1392000000, 1, 116, 2, 1, 1, 0),
	RK3036_PLL_RATE(1368000000, 1, 114, 2, 1, 1, 0),
	RK3036_PLL_RATE(1344000000, 1, 112, 2, 1, 1, 0),
	RK3036_PLL_RATE(1320000000, 1, 110, 2, 1, 1, 0),
	RK3036_PLL_RATE(1296000000, 1, 108, 2, 1, 1, 0),
	RK3036_PLL_RATE(1272000000, 1, 106, 2, 1, 1, 0),
	RK3036_PLL_RATE(1248000000, 1, 104, 2, 1, 1, 0),
	RK3036_PLL_RATE(1200000000, 1, 100, 2, 1, 1, 0),
	RK3036_PLL_RATE(1188000000, 1, 99, 2, 1, 1, 0),
	RK3036_PLL_RATE(1104000000, 1, 92, 2, 1, 1, 0),
	RK3036_PLL_RATE(1100000000, 3, 275, 2, 1, 1, 0),
	RK3036_PLL_RATE(1008000000, 1, 84, 2, 1, 1, 0),
	RK3036_PLL_RATE(1000000000, 3, 250, 2, 1, 1, 0),
	RK3036_PLL_RATE(984000000, 1, 82, 2, 1, 1, 0),
	RK3036_PLL_RATE(960000000, 1, 80, 2, 1, 1, 0),
	RK3036_PLL_RATE(936000000, 1, 78, 2, 1, 1, 0),
	RK3036_PLL_RATE(912000000, 1, 76, 2, 1, 1, 0),
	RK3036_PLL_RATE(900000000, 1, 75, 2, 1, 1, 0),
	RK3036_PLL_RATE(888000000, 1, 74, 2, 1, 1, 0),
	RK3036_PLL_RATE(864000000, 1, 72, 2, 1, 1, 0),
	RK3036_PLL_RATE(840000000, 1, 70, 2, 1, 1, 0),
	RK3036_PLL_RATE(816000000, 1, 68, 2, 1, 1, 0),
	RK3036_PLL_RATE(800000000, 3, 200, 2, 1, 1, 0),
	RK3036_PLL_RATE(700000000, 3, 350, 4, 1, 1, 0),
	RK3036_PLL_RATE(696000000, 1, 116, 4, 1, 1, 0),
	RK3036_PLL_RATE(624000000, 1, 104, 4, 1, 1, 0),
	RK3036_PLL_RATE(600000000, 1, 100, 4, 1, 1, 0),
	RK3036_PLL_RATE(594000000, 1, 99, 4, 1, 1, 0),
	RK3036_PLL_RATE(504000000, 1, 84, 4, 1, 1, 0),
	RK3036_PLL_RATE(500000000, 1, 125, 6, 1, 1, 0),
	RK3036_PLL_RATE(496742400, 1, 124, 6, 1, 0, 3113851),
	RK3036_PLL_RATE(491520000, 1, 40, 2, 1, 0, 16106127),
	RK3036_PLL_RATE(408000000, 1, 68, 2, 2, 1, 0),
	RK3036_PLL_RATE(312000000, 1, 78, 6, 1, 1, 0),
	RK3036_PLL_RATE(216000000, 1, 72, 4, 2, 1, 0),
	RK3036_PLL_RATE(96000000, 1, 96, 6, 4, 1, 0),
	{ /* sentinel */ },
};

#define RV1106_DIV_ACLK_CORE_MASK	0x1f
#define RV1106_DIV_ACLK_CORE_SHIFT	7
#define RV1106_DIV_PCLK_DBG_MASK	0x1f
#define RV1106_DIV_PCLK_DBG_SHIFT	0
#define RV1106_CORE_SEL_MASK		0x3
#define RV1106_CORE_SEL_SHIFT		5
#define RV1106_ALT_DIV_MASK		0x1f
#define RV1106_ALT_DIV_SHIFT		0

#define RV1106_CLKSEL0(_aclk_core)					\
{									\
	.reg = RV1106_CORECLKSEL_CON(0),				\
	.val = HIWORD_UPDATE(_aclk_core, RV1106_DIV_ACLK_CORE_MASK,	\
			     RV1106_DIV_ACLK_CORE_SHIFT),		\
}

#define RV1106_CLKSEL1(_pclk_dbg)					\
{									\
	.reg = RV1106_CORECLKSEL_CON(1),				\
	.val = HIWORD_UPDATE(_pclk_dbg, RV1106_DIV_PCLK_DBG_MASK,	\
			     RV1106_DIV_PCLK_DBG_SHIFT),		\
}

#define RV1106_CLKSEL2(_is_pvtpll)					\
{									\
	.reg = RV1106_CORECLKSEL_CON(0),				\
	.val = HIWORD_UPDATE(_is_pvtpll, RV1106_CORE_SEL_MASK,		\
			     RV1106_CORE_SEL_SHIFT),			\
}

#define RV1106_CLKSEL3(_alt_div)					\
{									\
	.reg = RV1106_CORECLKSEL_CON(0),				\
	.val = HIWORD_UPDATE(_alt_div, RV1106_ALT_DIV_MASK,		\
			     RV1106_ALT_DIV_SHIFT),			\
}

#define RV1106_CPUCLK_RATE(_prate, _aclk_core, _pclk_dbg, _is_pvtpll)	\
{									\
	.prate = _prate,						\
	.divs = {							\
		RV1106_CLKSEL0(_aclk_core),				\
		RV1106_CLKSEL1(_pclk_dbg),				\
	},								\
	.pre_muxs = {							\
		RV1106_CLKSEL3(1),					\
		RV1106_CLKSEL2(2),					\
	},								\
	.post_muxs = {							\
		RV1106_CLKSEL2(_is_pvtpll),				\
		RV1106_CLKSEL3(0),					\
	},								\
}

static struct rockchip_cpuclk_rate_table rv1106_cpuclk_rates[] __initdata = {
	RV1106_CPUCLK_RATE(1608000000, 3, 7, 1),
	RV1106_CPUCLK_RATE(1584000000, 3, 7, 1),
	RV1106_CPUCLK_RATE(1560000000, 3, 7, 1),
	RV1106_CPUCLK_RATE(1536000000, 3, 7, 1),
	RV1106_CPUCLK_RATE(1512000000, 3, 7, 1),
	RV1106_CPUCLK_RATE(1488000000, 2, 5, 1),
	RV1106_CPUCLK_RATE(1464000000, 2, 5, 1),
	RV1106_CPUCLK_RATE(1440000000, 2, 5, 1),
	RV1106_CPUCLK_RATE(1416000000, 2, 5, 1),
	RV1106_CPUCLK_RATE(1392000000, 2, 5, 1),
	RV1106_CPUCLK_RATE(1368000000, 2, 5, 1),
	RV1106_CPUCLK_RATE(1344000000, 2, 5, 1),
	RV1106_CPUCLK_RATE(1320000000, 2, 5, 1),
	RV1106_CPUCLK_RATE(1296000000, 2, 5, 1),
	RV1106_CPUCLK_RATE(1272000000, 2, 5, 1),
	RV1106_CPUCLK_RATE(1248000000, 2, 5, 1),
	RV1106_CPUCLK_RATE(1224000000, 2, 5, 1),
	RV1106_CPUCLK_RATE(1200000000, 2, 5, 1),
	RV1106_CPUCLK_RATE(1104000000, 2, 5, 1),
	RV1106_CPUCLK_RATE(1096000000, 2, 5, 1),
	RV1106_CPUCLK_RATE(1008000000, 1, 5, 1),
	RV1106_CPUCLK_RATE(912000000, 1, 5, 1),
	RV1106_CPUCLK_RATE(816000000, 1, 3, 1),
	RV1106_CPUCLK_RATE(696000000, 1, 3, 0),
	RV1106_CPUCLK_RATE(600000000, 1, 3, 0),
	RV1106_CPUCLK_RATE(408000000, 1, 1, 0),
	RV1106_CPUCLK_RATE(312000000, 1, 1, 0),
	RV1106_CPUCLK_RATE(216000000,  1, 1, 0),
	RV1106_CPUCLK_RATE(96000000, 1, 1, 0),
};

static const struct rockchip_cpuclk_reg_data rv1106_cpuclk_data = {
	.core_reg[0] = RV1106_CORECLKSEL_CON(0),
	.div_core_shift[0] = 0,
	.div_core_mask[0] = 0x1f,
	.num_cores = 1,
	.mux_core_alt = 2,
	.mux_core_main = 2,
	.mux_core_shift = 5,
	.mux_core_mask = 0x3,
};

PNAME(mux_pll_p)			= { "xin24m" };
PNAME(mux_24m_32k_p)			= { "xin24m", "clk_rtc_32k" };
PNAME(mux_gpll_cpll_p)			= { "gpll", "cpll" };
PNAME(mux_gpll_24m_p)			= { "gpll", "xin24m" };
PNAME(mux_100m_50m_24m_p)		= { "clk_100m_src", "clk_50m_src", "xin24m" };
PNAME(mux_150m_100m_50m_24m_p)		= { "clk_150m_src", "clk_100m_src", "clk_50m_src", "xin24m" };
PNAME(mux_500m_300m_100m_24m_p)		= { "clk_500m_src", "clk_300m_src", "clk_100m_src", "xin24m" };
PNAME(mux_400m_300m_pvtpll0_pvtpll1_p)	= { "clk_400m_src", "clk_300m_src", "clk_pvtpll_0", "clk_pvtpll_1" };
PNAME(mux_500m_300m_pvtpll0_pvtpll1_p)	= { "clk_500m_src", "clk_300m_src", "clk_pvtpll_0", "clk_pvtpll_1" };
PNAME(mux_339m_200m_pvtpll0_pvtpll1_p)	= { "clk_339m_src", "clk_200m_src", "clk_pvtpll_0", "clk_pvtpll_1" };
PNAME(mux_400m_200m_100m_24m_p)		= { "clk_400m_src", "clk_200m_src", "clk_100m_src", "xin24m" };
PNAME(mux_200m_100m_50m_24m_p)		= { "clk_200m_src", "clk_100m_src", "clk_50m_src", "xin24m" };
PNAME(mux_300m_200m_100m_24m_p)		= { "clk_300m_src", "clk_200m_src", "clk_100m_src", "xin24m" };
PNAME(mux_500m_300m_200m_24m_p)		= { "clk_500m_src", "clk_300m_src", "clk_200m_src", "xin24m" };
PNAME(mux_50m_24m_p)			= { "clk_50m_src", "xin24m" };
PNAME(mux_400m_24m_p)			= { "clk_400m_src", "xin24m" };
PNAME(clk_rtc32k_pmu_p)			= { "clk_rtc32k_frac", "xin32k", "clk_pvtm_32k" };
PNAME(mux_200m_100m_24m_32k_p)		= { "clk_200m_src", "clk_100m_src", "xin24m", "clk_rtc_32k" };
PNAME(mux_100m_pmu_24m_p)		= { "clk_100m_pmu", "xin24m" };
PNAME(mux_200m_100m_24m_p)		= { "clk_200m_src", "clk_100m_pmu", "xin24m" };
PNAME(mux_339m_200m_100m_24m_p)		= { "clk_339m_src", "clk_200m_src", "clk_100m_pmu", "xin24m" };
PNAME(mux_dpll_300m_p)			= { "dpll", "clk_300m_src" };
PNAME(clk_i2s0_8ch_tx_p)		= { "clk_i2s0_8ch_tx_src", "clk_i2s0_8ch_tx_frac", "i2s0_mclkin", "xin_osc0_half" };
PNAME(clk_i2s0_8ch_rx_p)		= { "clk_i2s0_8ch_rx_src", "clk_i2s0_8ch_rx_frac", "i2s0_mclkin", "xin_osc0_half" };
PNAME(i2s0_8ch_mclkout_p)		= { "mclk_i2s0_8ch_tx", "mclk_i2s0_8ch_rx", "xin_osc0_half" };
PNAME(clk_ref_mipi0_p)			= { "clk_ref_mipi0_src", "clk_ref_mipi0_frac", "xin24m" };
PNAME(clk_ref_mipi1_p)			= { "clk_ref_mipi1_src", "clk_ref_mipi1_frac", "xin24m" };
PNAME(clk_uart0_p)			= { "clk_uart0_src", "clk_uart0_frac", "xin24m" };
PNAME(clk_uart1_p)			= { "clk_uart1_src", "clk_uart1_frac", "xin24m" };
PNAME(clk_uart2_p)			= { "clk_uart2_src", "clk_uart2_frac", "xin24m" };
PNAME(clk_uart3_p)			= { "clk_uart3_src", "clk_uart3_frac", "xin24m" };
PNAME(clk_uart4_p)			= { "clk_uart4_src", "clk_uart4_frac", "xin24m" };
PNAME(clk_uart5_p)			= { "clk_uart5_src", "clk_uart5_frac", "xin24m" };
PNAME(clk_vicap_m0_p)			= { "clk_vicap_m0_src", "clk_vicap_m0_frac", "xin24m" };
PNAME(clk_vicap_m1_p)			= { "clk_vicap_m1_src", "clk_vicap_m1_frac", "xin24m" };

static struct rockchip_pll_clock rv1106_pll_clks[] __initdata = {
	[apll] = PLL(pll_rk3328, PLL_APLL, "apll", mux_pll_p,
		     CLK_IGNORE_UNUSED, RV1106_PLL_CON(0),
		     RV1106_MODE_CON, 0, 10, 0, rv1106_pll_rates),
	[cpll] = PLL(pll_rk3328, PLL_CPLL, "cpll", mux_pll_p,
		     0, RV1106_PLL_CON(8),
		     RV1106_MODE_CON, 2, 10, 0, rv1106_pll_rates),
	[dpll] = PLL(pll_rk3328, PLL_DPLL, "dpll", mux_pll_p,
		     CLK_IGNORE_UNUSED, RV1106_PLL_CON(16),
		     RV1106_SUBDDRMODE_CON, 0, 10, 0, NULL),
	[gpll] = PLL(pll_rk3328, PLL_GPLL, "gpll", mux_pll_p,
		     0, RV1106_PLL_CON(24),
		     RV1106_MODE_CON, 4, 10, 0, rv1106_pll_rates),
};

#define MFLAGS CLK_MUX_HIWORD_MASK
#define DFLAGS CLK_DIVIDER_HIWORD_MASK
#define GFLAGS (CLK_GATE_HIWORD_MASK | CLK_GATE_SET_TO_DISABLE)

static struct rockchip_clk_branch rv1106_rtc32k_pmu_fracmux __initdata =
	MUX(CLK_RTC_32K, "clk_rtc_32k", clk_rtc32k_pmu_p, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			RV1106_PMUCLKSEL_CON(0), 6, 2, MFLAGS);

static struct rockchip_clk_branch rv1106_i2s0_8ch_tx_fracmux __initdata =
	MUX(CLK_I2S0_8CH_TX, "clk_i2s0_8ch_tx", clk_i2s0_8ch_tx_p, CLK_SET_RATE_PARENT,
			RV1106_CLKSEL_CON(19), 0, 2, MFLAGS);

static struct rockchip_clk_branch rv1106_i2s0_8ch_rx_fracmux __initdata =
	MUX(CLK_I2S0_8CH_RX, "clk_i2s0_8ch_rx", clk_i2s0_8ch_rx_p, CLK_SET_RATE_PARENT,
			RV1106_CLKSEL_CON(21), 0, 2, MFLAGS);

static struct rockchip_clk_branch rv1106_clk_ref_mipi0_fracmux __initdata =
	MUX(CLK_REF_MIPI0, "clk_ref_mipi0", clk_ref_mipi0_p, CLK_SET_RATE_PARENT,
			RV1106_CLKSEL_CON(27), 0, 2, MFLAGS);

static struct rockchip_clk_branch rv1106_clk_ref_mipi1_fracmux __initdata =
	MUX(CLK_REF_MIPI1, "clk_ref_mipi1", clk_ref_mipi1_p, CLK_SET_RATE_PARENT,
			RV1106_CLKSEL_CON(29), 0, 2, MFLAGS);

static struct rockchip_clk_branch rv1106_clk_uart0_fracmux __initdata =
	MUX(CLK_UART0, "clk_uart0", clk_uart0_p, CLK_SET_RATE_PARENT,
			RV1106_CLKSEL_CON(7), 0, 2, MFLAGS);

static struct rockchip_clk_branch rv1106_clk_uart1_fracmux __initdata =
	MUX(CLK_UART1, "clk_uart1", clk_uart1_p, CLK_SET_RATE_PARENT,
			RV1106_CLKSEL_CON(9), 0, 2, MFLAGS);

static struct rockchip_clk_branch rv1106_clk_uart2_fracmux __initdata =
	MUX(CLK_UART2, "clk_uart2", clk_uart2_p, CLK_SET_RATE_PARENT,
			RV1106_CLKSEL_CON(11), 0, 2, MFLAGS);

static struct rockchip_clk_branch rv1106_clk_uart3_fracmux __initdata =
	MUX(CLK_UART3, "clk_uart3", clk_uart3_p, CLK_SET_RATE_PARENT,
			RV1106_CLKSEL_CON(13), 0, 2, MFLAGS);

static struct rockchip_clk_branch rv1106_clk_uart4_fracmux __initdata =
	MUX(CLK_UART4, "clk_uart4", clk_uart4_p, CLK_SET_RATE_PARENT,
			RV1106_CLKSEL_CON(15), 0, 2, MFLAGS);

static struct rockchip_clk_branch rv1106_clk_uart5_fracmux __initdata =
	MUX(CLK_UART5, "clk_uart5", clk_uart5_p, CLK_SET_RATE_PARENT,
			RV1106_CLKSEL_CON(17), 0, 2, MFLAGS);

static struct rockchip_clk_branch rv1106_clk_vicap_m0_fracmux __initdata =
	MUX(CLK_VICAP_M0, "clk_vicap_m0", clk_vicap_m0_p, CLK_SET_RATE_PARENT,
			RV1106_CLKSEL_CON(31), 0, 2, MFLAGS);

static struct rockchip_clk_branch rv1106_clk_vicap_m1_fracmux __initdata =
	MUX(CLK_VICAP_M1, "clk_vicap_m1", clk_vicap_m1_p, CLK_SET_RATE_PARENT,
			RV1106_CLKSEL_CON(33), 0, 2, MFLAGS);

static struct rockchip_clk_branch rv1106_clk_branches[] __initdata = {

	FACTOR(0, "xin_osc0_half", "xin24m", 0, 1, 2),

	/* PD_CORE */
	GATE(CLK_PVTM_CORE, "clk_pvtm_core", "xin24m", 0,
			RV1106_CORECLKGATE_CON(0), 14, GFLAGS),
	GATE(CLK_CORE_MCU_RTC, "clk_core_mcu_rtc", "xin24m", 0,
			RV1106_CORECLKGATE_CON(1), 6, GFLAGS),
	COMPOSITE(HCLK_CPU, "hclk_cpu", mux_gpll_24m_p, CLK_IS_CRITICAL,
			RV1106_CORECLKSEL_CON(2), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RV1106_CORECLKGATE_CON(0), 12, GFLAGS),
	COMPOSITE(CLK_CORE_MCU, "clk_core_mcu", mux_gpll_24m_p, 0,
			RV1106_CORECLKSEL_CON(3), 11, 1, MFLAGS, 6, 5, DFLAGS,
			RV1106_CORECLKGATE_CON(1), 1, GFLAGS),
	COMPOSITE_NOMUX(0, "pclk_dbg", "armclk", CLK_IS_CRITICAL,
			RV1106_CORECLKSEL_CON(1), 0, 5, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RV1106_CORECLKGATE_CON(0), 6, GFLAGS),
	GATE(0, "pclk_cpu_root", "pclk_dbg", CLK_IS_CRITICAL,
			RV1106_CORECLKGATE_CON(0), 10, GFLAGS),
	GATE(PCLK_MAILBOX, "pclk_mailbox", "pclk_cpu_root", 0,
			RV1106_CORECLKGATE_CON(1), 8, GFLAGS),

	/* PD _TOP */
	COMPOSITE(CLK_50M_SRC, "clk_50m_src", mux_gpll_cpll_p, CLK_IS_CRITICAL,
			RV1106_CLKSEL_CON(0), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RV1106_CLKGATE_CON(0), 1, GFLAGS),
	COMPOSITE(CLK_100M_SRC, "clk_100m_src", mux_gpll_cpll_p, CLK_IS_CRITICAL,
			RV1106_CLKSEL_CON(0), 11, 1, MFLAGS, 6, 5, DFLAGS,
			RV1106_CLKGATE_CON(0), 2, GFLAGS),
	COMPOSITE(CLK_150M_SRC, "clk_150m_src", mux_gpll_cpll_p, CLK_IS_CRITICAL,
			RV1106_CLKSEL_CON(1), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RV1106_CLKGATE_CON(0), 3, GFLAGS),
	COMPOSITE(CLK_200M_SRC, "clk_200m_src", mux_gpll_cpll_p, CLK_IS_CRITICAL,
			RV1106_CLKSEL_CON(1), 11, 1, MFLAGS, 6, 5, DFLAGS,
			RV1106_CLKGATE_CON(0), 4, GFLAGS),
	COMPOSITE(CLK_250M_SRC, "clk_250m_src", mux_gpll_cpll_p, CLK_IS_CRITICAL,
			RV1106_CLKSEL_CON(2), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RV1106_CLKGATE_CON(0), 5, GFLAGS),
	COMPOSITE(CLK_300M_SRC, "clk_300m_src", mux_gpll_cpll_p, CLK_IS_CRITICAL,
			RV1106_CLKSEL_CON(2), 11, 1, MFLAGS, 6, 5, DFLAGS,
			RV1106_CLKGATE_CON(0), 6, GFLAGS),
	COMPOSITE_HALFDIV(CLK_339M_SRC, "clk_339m_src", mux_gpll_cpll_p, CLK_IS_CRITICAL,
			RV1106_CLKSEL_CON(3), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RV1106_CLKGATE_CON(0), 7, GFLAGS),
	COMPOSITE(CLK_400M_SRC, "clk_400m_src", mux_gpll_cpll_p, CLK_IS_CRITICAL,
			RV1106_CLKSEL_CON(3), 11, 1, MFLAGS, 6, 5, DFLAGS,
			RV1106_CLKGATE_CON(0), 8, GFLAGS),
	COMPOSITE_HALFDIV(CLK_450M_SRC, "clk_450m_src", mux_gpll_cpll_p, CLK_IS_CRITICAL,
			RV1106_CLKSEL_CON(4), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RV1106_CLKGATE_CON(0), 9, GFLAGS),
	COMPOSITE(CLK_500M_SRC, "clk_500m_src", mux_gpll_cpll_p, CLK_IS_CRITICAL,
			RV1106_CLKSEL_CON(4), 11, 1, MFLAGS, 6, 5, DFLAGS,
			RV1106_CLKGATE_CON(0), 10, GFLAGS),

	COMPOSITE_NODIV(PCLK_TOP_ROOT, "pclk_top_root", mux_100m_50m_24m_p, CLK_IS_CRITICAL,
			RV1106_CLKSEL_CON(24), 5, 2, MFLAGS,
			RV1106_CLKGATE_CON(2), 9, GFLAGS),

	COMPOSITE(CLK_I2S0_8CH_TX_SRC, "clk_i2s0_8ch_tx_src", mux_gpll_cpll_p, 0,
			RV1106_CLKSEL_CON(17), 7, 1, MFLAGS, 2, 5, DFLAGS,
			RV1106_CLKGATE_CON(1), 13, GFLAGS),
	COMPOSITE_FRACMUX(CLK_I2S0_8CH_TX_FRAC, "clk_i2s0_8ch_tx_frac", "clk_i2s0_8ch_tx_src", CLK_SET_RATE_PARENT,
			RV1106_CLKSEL_CON(18), 0,
			RV1106_CLKGATE_CON(1), 14, GFLAGS,
			&rv1106_i2s0_8ch_tx_fracmux),
	GATE(MCLK_I2S0_8CH_TX, "mclk_i2s0_8ch_tx", "clk_i2s0_8ch_tx", 0,
			RV1106_CLKGATE_CON(1), 15, GFLAGS),
	COMPOSITE(CLK_I2S0_8CH_RX_SRC, "clk_i2s0_8ch_rx_src", mux_gpll_cpll_p, 0,
			RV1106_CLKSEL_CON(19), 7, 1, MFLAGS, 2, 5, DFLAGS,
			RV1106_CLKGATE_CON(2), 0, GFLAGS),
	COMPOSITE_FRACMUX(CLK_I2S0_8CH_RX_FRAC, "clk_i2s0_8ch_rx_frac", "clk_i2s0_8ch_rx_src", CLK_SET_RATE_PARENT,
			RV1106_CLKSEL_CON(20), 0,
			RV1106_CLKGATE_CON(2), 1, GFLAGS,
			&rv1106_i2s0_8ch_rx_fracmux),
	GATE(MCLK_I2S0_8CH_RX, "mclk_i2s0_8ch_rx", "clk_i2s0_8ch_rx", 0,
			RV1106_CLKGATE_CON(2), 2, GFLAGS),
	MUX(I2S0_8CH_MCLKOUT, "i2s0_8ch_mclkout", i2s0_8ch_mclkout_p, CLK_SET_RATE_PARENT,
			RV1106_CLKSEL_CON(21), 2, 2, MFLAGS),
	COMPOSITE(CLK_REF_MIPI0_SRC, "clk_ref_mipi0_src", mux_gpll_cpll_p, 0,
			RV1106_CLKSEL_CON(25), 7, 1, MFLAGS, 2, 5, DFLAGS,
			RV1106_CLKGATE_CON(3), 4, GFLAGS),
	COMPOSITE_FRACMUX(CLK_REF_MIPI0_FRAC, "clk_ref_mipi0_frac", "clk_ref_mipi0_src", CLK_SET_RATE_PARENT,
			RV1106_CLKSEL_CON(26), 0,
			RV1106_CLKGATE_CON(3), 5, GFLAGS,
			&rv1106_clk_ref_mipi0_fracmux),
	GATE(MCLK_REF_MIPI0, "mclk_ref_mipi0", "clk_ref_mipi0", 0,
			 RV1106_CLKGATE_CON(3), 6, GFLAGS),
	COMPOSITE(CLK_REF_MIPI1_SRC, "clk_ref_mipi1_src", mux_gpll_cpll_p, 0,
			RV1106_CLKSEL_CON(27), 7, 1, MFLAGS, 2, 5, DFLAGS,
			RV1106_CLKGATE_CON(3), 7, GFLAGS),
	COMPOSITE_FRACMUX(CLK_REF_MIPI1_FRAC, "clk_ref_mipi1_frac", "clk_ref_mipi1_src", CLK_SET_RATE_PARENT,
			RV1106_CLKSEL_CON(28), 0,
			RV1106_CLKGATE_CON(3), 8, GFLAGS,
			&rv1106_clk_ref_mipi1_fracmux),
	GATE(MCLK_REF_MIPI1, "mclk_ref_mipi1", "clk_ref_mipi1", 0,
			 RV1106_CLKGATE_CON(3), 9, GFLAGS),
	COMPOSITE(CLK_UART0_SRC, "clk_uart0_src", mux_gpll_cpll_p, 0,
			RV1106_CLKSEL_CON(5), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RV1106_CLKGATE_CON(0), 11, GFLAGS),
	COMPOSITE_FRACMUX(CLK_UART0_FRAC, "clk_uart0_frac", "clk_uart0_src", CLK_SET_RATE_PARENT,
			RV1106_CLKSEL_CON(6), CLK_FRAC_DIVIDER_NO_LIMIT,
			RV1106_CLKGATE_CON(0), 12, GFLAGS,
			&rv1106_clk_uart0_fracmux),
	GATE(SCLK_UART0, "sclk_uart0", "clk_uart0", 0,
			RV1106_CLKGATE_CON(0), 13, GFLAGS),
	COMPOSITE(CLK_UART1_SRC, "clk_uart1_src", mux_gpll_cpll_p, 0,
			RV1106_CLKSEL_CON(7), 7, 1, MFLAGS, 2, 5, DFLAGS,
			RV1106_CLKGATE_CON(0), 14, GFLAGS),
	COMPOSITE_FRACMUX(CLK_UART1_FRAC, "clk_uart1_frac", "clk_uart1_src", CLK_SET_RATE_PARENT,
			RV1106_CLKSEL_CON(8), CLK_FRAC_DIVIDER_NO_LIMIT,
			RV1106_CLKGATE_CON(0), 15, GFLAGS,
			&rv1106_clk_uart1_fracmux),
	GATE(SCLK_UART1, "sclk_uart1", "clk_uart1", 0,
			 RV1106_CLKGATE_CON(1), 0, GFLAGS),
	COMPOSITE(CLK_UART2_SRC, "clk_uart2_src", mux_gpll_cpll_p, 0,
			RV1106_CLKSEL_CON(9), 7, 1, MFLAGS, 2, 5, DFLAGS,
			RV1106_CLKGATE_CON(1), 1, GFLAGS),
	COMPOSITE_FRACMUX(CLK_UART2_FRAC, "clk_uart2_frac", "clk_uart2_src", CLK_SET_RATE_PARENT,
			RV1106_CLKSEL_CON(10), CLK_FRAC_DIVIDER_NO_LIMIT,
			RV1106_CLKGATE_CON(1), 2, GFLAGS,
			&rv1106_clk_uart2_fracmux),
	GATE(SCLK_UART2, "sclk_uart2", "clk_uart2", 0,
			 RV1106_CLKGATE_CON(1), 3, GFLAGS),
	COMPOSITE(CLK_UART3_SRC, "clk_uart3_src", mux_gpll_cpll_p, 0,
			RV1106_CLKSEL_CON(11), 7, 1, MFLAGS, 2, 5, DFLAGS,
			RV1106_CLKGATE_CON(1), 4, GFLAGS),
	COMPOSITE_FRACMUX(CLK_UART3_FRAC, "clk_uart3_frac", "clk_uart3_src", CLK_SET_RATE_PARENT,
			RV1106_CLKSEL_CON(12), CLK_FRAC_DIVIDER_NO_LIMIT,
			RV1106_CLKGATE_CON(1), 5, GFLAGS,
			&rv1106_clk_uart3_fracmux),
	GATE(SCLK_UART3, "sclk_uart3", "clk_uart3", 0,
			 RV1106_CLKGATE_CON(1), 6, GFLAGS),
	COMPOSITE(CLK_UART4_SRC, "clk_uart4_src", mux_gpll_cpll_p, 0,
			RV1106_CLKSEL_CON(13), 7, 1, MFLAGS, 2, 5, DFLAGS,
			RV1106_CLKGATE_CON(1), 7, GFLAGS),
	COMPOSITE_FRACMUX(CLK_UART4_FRAC, "clk_uart4_frac", "clk_uart4_src", CLK_SET_RATE_PARENT,
			RV1106_CLKSEL_CON(14), CLK_FRAC_DIVIDER_NO_LIMIT,
			RV1106_CLKGATE_CON(1), 8, GFLAGS,
			&rv1106_clk_uart4_fracmux),
	GATE(SCLK_UART4, "sclk_uart4", "clk_uart4", 0,
			 RV1106_CLKGATE_CON(1), 9, GFLAGS),
	COMPOSITE(CLK_UART5_SRC, "clk_uart5_src", mux_gpll_cpll_p, 0,
			RV1106_CLKSEL_CON(15), 7, 1, MFLAGS, 2, 5, DFLAGS,
			RV1106_CLKGATE_CON(1), 10, GFLAGS),
	COMPOSITE_FRACMUX(CLK_UART5_FRAC, "clk_uart5_frac", "clk_uart5_src", CLK_SET_RATE_PARENT,
			RV1106_CLKSEL_CON(16), CLK_FRAC_DIVIDER_NO_LIMIT,
			RV1106_CLKGATE_CON(1), 11, GFLAGS,
			&rv1106_clk_uart5_fracmux),
	GATE(SCLK_UART5, "sclk_uart5", "clk_uart5", 0,
			 RV1106_CLKGATE_CON(1), 12, GFLAGS),
	COMPOSITE(CLK_VICAP_M0_SRC, "clk_vicap_m0_src", mux_gpll_cpll_p, 0,
			RV1106_CLKSEL_CON(29), 7, 1, MFLAGS, 2, 5, DFLAGS,
			RV1106_CLKGATE_CON(3), 10, GFLAGS),
	COMPOSITE_FRACMUX(CLK_VICAP_M0_FRAC, "clk_vicap_m0_frac", "clk_vicap_m0_src", CLK_SET_RATE_PARENT,
			RV1106_CLKSEL_CON(30), 0,
			RV1106_CLKGATE_CON(3), 11, GFLAGS,
			&rv1106_clk_vicap_m0_fracmux),
	GATE(SCLK_VICAP_M0, "sclk_vicap_m0", "clk_vicap_m0", 0,
			 RV1106_CLKGATE_CON(3), 12, GFLAGS),
	COMPOSITE(CLK_VICAP_M1_SRC, "clk_vicap_m1_src", mux_gpll_cpll_p, 0,
			RV1106_CLKSEL_CON(31), 7, 1, MFLAGS, 2, 5, DFLAGS,
			RV1106_CLKGATE_CON(3), 13, GFLAGS),
	COMPOSITE_FRACMUX(CLK_VICAP_M1_FRAC, "clk_vicap_m1_frac", "clk_vicap_m1_src", 0,
			RV1106_CLKSEL_CON(32), 0,
			RV1106_CLKGATE_CON(3), 14, GFLAGS,
			&rv1106_clk_vicap_m1_fracmux),
	GATE(SCLK_VICAP_M1, "sclk_vicap_m1", "clk_vicap_m1", 0,
			 RV1106_CLKGATE_CON(3), 15, GFLAGS),
	COMPOSITE(DCLK_VOP_SRC, "dclk_vop_src", mux_gpll_cpll_p, 0,
			RV1106_CLKSEL_CON(23), 8, 1, MFLAGS, 3, 5, DFLAGS,
			RV1106_CLKGATE_CON(2), 6, GFLAGS),

	/* PD_DDR */
	COMPOSITE_NODIV(PCLK_DDR_ROOT, "pclk_ddr_root", mux_100m_50m_24m_p, CLK_IS_CRITICAL,
			RV1106_DDRCLKSEL_CON(0), 0, 2, MFLAGS,
			RV1106_DDRCLKGATE_CON(0), 0, GFLAGS),
	COMPOSITE_NODIV(ACLK_DDR_ROOT, "aclk_ddr_root", mux_500m_300m_100m_24m_p, CLK_IS_CRITICAL,
			RV1106_DDRCLKSEL_CON(0), 8, 2, MFLAGS,
			RV1106_DDRCLKGATE_CON(0), 12, GFLAGS),
	GATE(PCLK_DDRPHY, "pclk_ddrphy", "pclk_ddr_root", CLK_IGNORE_UNUSED,
			RV1106_DDRCLKGATE_CON(1), 3, GFLAGS),
	GATE(PCLK_DDR_HWLP, "pclk_ddr_hwlp", "pclk_ddr_root", CLK_IGNORE_UNUSED,
			RV1106_DDRCLKGATE_CON(1), 2, GFLAGS),
	GATE(PCLK_DDRMON, "pclk_ddrmon", "pclk_ddr_root", 0,
			RV1106_DDRCLKGATE_CON(0), 7, GFLAGS),
	GATE(CLK_TIMER_DDRMON, "clk_timer_ddrmon", "xin24m", 0,
			RV1106_DDRCLKGATE_CON(0), 8, GFLAGS),
	GATE(PCLK_DDRC, "pclk_ddrc", "pclk_ddr_root", CLK_IGNORE_UNUSED,
			RV1106_DDRCLKGATE_CON(0), 5, GFLAGS),
	GATE(PCLK_DFICTRL, "pclk_dfictrl", "pclk_ddr_root", CLK_IS_CRITICAL,
			RV1106_DDRCLKGATE_CON(0), 11, GFLAGS),
	GATE(ACLK_SYS_SHRM, "aclk_sys_shrm", "aclk_ddr_root", CLK_IS_CRITICAL,
			RV1106_DDRCLKGATE_CON(0), 13, GFLAGS),

	/* PD_NPU */
	COMPOSITE_NODIV(HCLK_NPU_ROOT, "hclk_npu_root", mux_150m_100m_50m_24m_p, CLK_IS_CRITICAL,
			RV1106_NPUCLKSEL_CON(0), 0, 2, MFLAGS,
			RV1106_NPUCLKGATE_CON(0), 0, GFLAGS),
	COMPOSITE_NODIV(ACLK_NPU_ROOT, "aclk_npu_root", mux_500m_300m_pvtpll0_pvtpll1_p, CLK_IS_CRITICAL,
			RV1106_NPUCLKSEL_CON(0), 2, 2, MFLAGS,
			RV1106_NPUCLKGATE_CON(0), 1, GFLAGS),
	COMPOSITE_NODIV(PCLK_NPU_ROOT, "pclk_npu_root", mux_100m_50m_24m_p, CLK_IS_CRITICAL,
			RV1106_NPUCLKSEL_CON(0), 4, 2, MFLAGS,
			RV1106_NPUCLKGATE_CON(0), 2, GFLAGS),
	GATE(HCLK_RKNN, "hclk_rknn", "hclk_npu_root", 0,
			RV1106_NPUCLKGATE_CON(0), 9, GFLAGS),
	GATE(ACLK_RKNN, "aclk_rknn", "aclk_npu_root", 0,
			RV1106_NPUCLKGATE_CON(0), 10, GFLAGS),

	/* PD_PERI */
	COMPOSITE_NODIV(PCLK_PERI_ROOT, "pclk_peri_root", mux_100m_50m_24m_p, CLK_IS_CRITICAL,
			RV1106_PERICLKSEL_CON(1), 0, 2, MFLAGS,
			RV1106_PERICLKGATE_CON(0), 0, GFLAGS),
	COMPOSITE_NODIV(ACLK_PERI_ROOT, "aclk_peri_root", mux_400m_200m_100m_24m_p, CLK_IS_CRITICAL,
			RV1106_PERICLKSEL_CON(1), 2, 2, MFLAGS,
			RV1106_PERICLKGATE_CON(0), 1, GFLAGS),
	COMPOSITE_NODIV(HCLK_PERI_ROOT, "hclk_peri_root", mux_200m_100m_50m_24m_p, CLK_IS_CRITICAL,
			RV1106_PERICLKSEL_CON(1), 4, 2, MFLAGS,
			RV1106_PERICLKGATE_CON(0), 2, GFLAGS),
	COMPOSITE_NODIV(ACLK_BUS_ROOT, "aclk_bus_root", mux_300m_200m_100m_24m_p, CLK_IS_CRITICAL,
			RV1106_PERICLKSEL_CON(9), 0, 2, MFLAGS,
			RV1106_PERICLKGATE_CON(6), 8, GFLAGS),
	GATE(PCLK_ACODEC, "pclk_acodec", "pclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(6), 3, GFLAGS),
	COMPOSITE_NOMUX(MCLK_ACODEC_TX, "mclk_acodec_tx", "mclk_i2s0_8ch_tx", 0,
			RV1106_PERICLKSEL_CON(8), 0, 8, DFLAGS,
			RV1106_PERICLKGATE_CON(6), 4, GFLAGS),
	COMPOSITE_NODIV(CLK_CORE_CRYPTO, "clk_core_crypto", mux_300m_200m_100m_24m_p, 0,
			RV1106_PERICLKSEL_CON(6), 5, 2, MFLAGS,
			RV1106_PERICLKGATE_CON(3), 11, GFLAGS),
	COMPOSITE_NODIV(CLK_PKA_CRYPTO, "clk_pka_crypto", mux_300m_200m_100m_24m_p, 0,
			RV1106_PERICLKSEL_CON(6), 7, 2, MFLAGS,
			RV1106_PERICLKGATE_CON(3), 12, GFLAGS),
	GATE(ACLK_CRYPTO, "aclk_crypto", "aclk_bus_root", 0,
			RV1106_PERICLKGATE_CON(3), 13, GFLAGS),
	GATE(HCLK_CRYPTO, "hclk_crypto", "hclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(3), 14, GFLAGS),
	GATE(ACLK_DECOM, "aclk_decom", "aclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(5), 9, GFLAGS),
	GATE(PCLK_DECOM, "pclk_decom", "pclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(5), 10, GFLAGS),
	COMPOSITE_NODIV(DCLK_DECOM, "dclk_decom", mux_400m_200m_100m_24m_p, 0,
			RV1106_PERICLKSEL_CON(7), 14, 2, MFLAGS,
			RV1106_PERICLKGATE_CON(5), 11, GFLAGS),
	GATE(ACLK_DMAC, "aclk_dmac", "aclk_bus_root", 0,
			RV1106_PERICLKGATE_CON(5), 8, GFLAGS),
	GATE(PCLK_DSM, "pclk_dsm", "pclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(6), 2, GFLAGS),
	GATE(MCLK_DSM, "mclk_dsm", "mclk_i2s0_8ch_tx", 0,
			RV1106_PERICLKGATE_CON(6), 1, GFLAGS),
	COMPOSITE(CCLK_SRC_EMMC, "cclk_src_emmc", mux_400m_24m_p, 0,
			RV1106_PERICLKSEL_CON(7), 6, 1, MFLAGS, 0, 6, DFLAGS,
			RV1106_PERICLKGATE_CON(4), 12, GFLAGS),
	GATE(HCLK_EMMC, "hclk_emmc", "hclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(4), 13, GFLAGS),
	GATE(PCLK_GPIO4, "pclk_gpio4", "pclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(2), 0, GFLAGS),
	GATE(DBCLK_GPIO4, "dbclk_gpio4", "xin24m", 0,
			RV1106_PERICLKGATE_CON(2), 1, GFLAGS),
	GATE(PCLK_I2C0, "pclk_i2c0", "pclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(1), 6, GFLAGS),
	COMPOSITE_NODIV(CLK_I2C0, "clk_i2c0", mux_200m_100m_50m_24m_p, 0,
			RV1106_PERICLKSEL_CON(1), 8, 2, MFLAGS,
			RV1106_PERICLKGATE_CON(1), 7, GFLAGS),
	GATE(PCLK_I2C2, "pclk_i2c2", "pclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(1), 10, GFLAGS),
	COMPOSITE_NODIV(CLK_I2C2, "clk_i2c2", mux_200m_100m_50m_24m_p, 0,
			RV1106_PERICLKSEL_CON(1), 12, 2, MFLAGS,
			RV1106_PERICLKGATE_CON(1), 11, GFLAGS),
	GATE(PCLK_I2C3, "pclk_i2c3", "pclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(1), 12, GFLAGS),
	COMPOSITE_NODIV(CLK_I2C3, "clk_i2c3", mux_200m_100m_50m_24m_p, 0,
			RV1106_PERICLKSEL_CON(1), 14, 2, MFLAGS,
			RV1106_PERICLKGATE_CON(1), 13, GFLAGS),
	GATE(PCLK_I2C4, "pclk_i2c4", "pclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(1), 14, GFLAGS),
	COMPOSITE_NODIV(CLK_I2C4, "clk_i2c4", mux_200m_100m_50m_24m_p, 0,
			RV1106_PERICLKSEL_CON(2), 0, 2, MFLAGS,
			RV1106_PERICLKGATE_CON(1), 15, GFLAGS),
	GATE(HCLK_I2S0, "hclk_i2s0", "hclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(6), 0, GFLAGS),
	GATE(PCLK_DFT2APB, "pclk_dft2apb", "pclk_peri_root", CLK_IGNORE_UNUSED,
			RV1106_PERICLKGATE_CON(6), 7, GFLAGS),
	GATE(HCLK_IVE, "hclk_ive", "hclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(6), 9, GFLAGS),
	GATE(ACLK_IVE, "aclk_ive", "aclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(6), 10, GFLAGS),
	GATE(PCLK_PWM0_PERI, "pclk_pwm0_peri", "pclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(7), 3, GFLAGS),
	COMPOSITE_NODIV(CLK_PWM0_PERI, "clk_pwm0_peri", mux_100m_50m_24m_p, 0,
			RV1106_PERICLKSEL_CON(11), 0, 2, MFLAGS,
			RV1106_PERICLKGATE_CON(7), 4, GFLAGS),
	GATE(CLK_CAPTURE_PWM0_PERI, "clk_capture_pwm0_peri", "xin24m", 0,
			RV1106_PERICLKGATE_CON(7), 5, GFLAGS),
	GATE(CLK_TIMER_ROOT, "clk_timer_root", "xin24m", 0,
			RV1106_PERICLKGATE_CON(0), 3, GFLAGS),
	GATE(HCLK_SFC, "hclk_sfc", "hclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(4), 14, GFLAGS),
	COMPOSITE(SCLK_SFC, "sclk_sfc", mux_500m_300m_200m_24m_p, 0,
			RV1106_PERICLKSEL_CON(7), 12, 2, MFLAGS, 7, 5, DFLAGS,
			RV1106_PERICLKGATE_CON(5), 0, GFLAGS),
	GATE(PCLK_UART0, "pclk_uart0", "pclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(6), 11, GFLAGS),
	GATE(PCLK_UART1, "pclk_uart1", "pclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(6), 15, GFLAGS),
	GATE(PCLK_PWM1_PERI, "pclk_pwm1_peri", "pclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(3), 15, GFLAGS),
	COMPOSITE_NODIV(CLK_PWM1_PERI, "clk_pwm1_peri", mux_100m_50m_24m_p, 0,
			RV1106_PERICLKSEL_CON(6), 9, 2, MFLAGS,
			RV1106_PERICLKGATE_CON(4), 0, GFLAGS),
	GATE(CLK_CAPTURE_PWM1_PERI, "clk_capture_pwm1_peri", "xin24m", 0,
			RV1106_PERICLKGATE_CON(4), 1, GFLAGS),
	GATE(PCLK_PWM2_PERI, "pclk_pwm2_peri", "pclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(4), 2, GFLAGS),
	COMPOSITE_NODIV(CLK_PWM2_PERI, "clk_pwm2_peri", mux_100m_50m_24m_p, 0,
			RV1106_PERICLKSEL_CON(6), 11, 2, MFLAGS,
			RV1106_PERICLKGATE_CON(4), 3, GFLAGS),
	GATE(CLK_CAPTURE_PWM2_PERI, "clk_capture_pwm2_peri", "xin24m", 0,
			RV1106_PERICLKGATE_CON(4), 4, GFLAGS),
	GATE(HCLK_BOOTROM, "hclk_bootrom", "hclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(0), 7, GFLAGS),
	GATE(HCLK_SAI, "hclk_sai", "hclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(5), 13, GFLAGS),
	GATE(MCLK_SAI, "mclk_sai", "mclk_i2s0_8ch_tx", 0,
			RV1106_PERICLKGATE_CON(5), 14, GFLAGS),
	GATE(PCLK_SARADC, "pclk_saradc", "pclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(3), 3, GFLAGS),
	COMPOSITE_NOMUX(CLK_SARADC, "clk_saradc", "xin24m", 0,
			RV1106_PERICLKSEL_CON(6), 0, 3, DFLAGS,
			RV1106_PERICLKGATE_CON(3), 4, GFLAGS),
	GATE(PCLK_SPI1, "pclk_spi1", "pclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(3), 6, GFLAGS),
	COMPOSITE_NODIV(CLK_SPI1, "clk_spi1", mux_200m_100m_50m_24m_p, 0,
			RV1106_PERICLKSEL_CON(6), 3, 2, MFLAGS,
			RV1106_PERICLKGATE_CON(3), 7, GFLAGS),
	GATE(PCLK_STIMER, "pclk_stimer", "pclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(0), 15, GFLAGS),
	GATE(CLK_STIMER0, "clk_stimer0", "clk_timer_root", 0,
			RV1106_PERICLKGATE_CON(1), 0, GFLAGS),
	GATE(CLK_STIMER1, "clk_stimer1", "clk_timer_root", 0,
			RV1106_PERICLKGATE_CON(1), 1, GFLAGS),
	GATE(PCLK_TIMER, "pclk_timer", "pclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(0), 8, GFLAGS),
	GATE(CLK_TIMER0, "clk_timer0", "clk_timer_root", 0,
			RV1106_PERICLKGATE_CON(0), 9, GFLAGS),
	GATE(CLK_TIMER1, "clk_timer1", "clk_timer_root", 0,
			RV1106_PERICLKGATE_CON(0), 10, GFLAGS),
	GATE(CLK_TIMER2, "clk_timer2", "clk_timer_root", 0,
			RV1106_PERICLKGATE_CON(0), 11, GFLAGS),
	GATE(CLK_TIMER3, "clk_timer3", "clk_timer_root", 0,
			RV1106_PERICLKGATE_CON(0), 12, GFLAGS),
	GATE(CLK_TIMER4, "clk_timer4", "clk_timer_root", 0,
			RV1106_PERICLKGATE_CON(0), 13, GFLAGS),
	GATE(CLK_TIMER5, "clk_timer5", "clk_timer_root", 0,
			RV1106_PERICLKGATE_CON(0), 14, GFLAGS),
	GATE(HCLK_TRNG_NS, "hclk_trng_ns", "hclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(3), 9, GFLAGS),
	GATE(HCLK_TRNG_S, "hclk_trng_s", "hclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(3), 10, GFLAGS),
	GATE(PCLK_UART2, "pclk_uart2", "pclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(2), 3, GFLAGS),
	GATE(PCLK_UART3, "pclk_uart3", "pclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(2), 7, GFLAGS),
	GATE(PCLK_UART4, "pclk_uart4", "pclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(2), 11, GFLAGS),
	GATE(PCLK_UART5, "pclk_uart5", "pclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(2), 15, GFLAGS),
	GATE(ACLK_USBOTG, "aclk_usbotg", "aclk_bus_root", 0,
			RV1106_PERICLKGATE_CON(4), 7, GFLAGS),
	GATE(CLK_REF_USBOTG, "clk_ref_usbotg", "xin24m", 0,
			RV1106_PERICLKGATE_CON(4), 8, GFLAGS),
	GATE(PCLK_USBPHY, "pclk_usbphy", "pclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(5), 1, GFLAGS),
	GATE(CLK_REF_USBPHY, "clk_ref_usbphy", "xin24m", 0,
			RV1106_PERICLKGATE_CON(5), 2, GFLAGS),
	GATE(PCLK_WDT_NS, "pclk_wdt_ns", "pclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(1), 2, GFLAGS),
	GATE(TCLK_WDT_NS, "tclk_wdt_ns", "xin24m", 0,
			RV1106_PERICLKGATE_CON(1), 3, GFLAGS),
	GATE(PCLK_WDT_S, "pclk_wdt_s", "pclk_peri_root", 0,
			RV1106_PERICLKGATE_CON(1), 4, GFLAGS),
	GATE(TCLK_WDT_S, "tclk_wdt_s", "xin24m", 0,
			RV1106_PERICLKGATE_CON(1), 5, GFLAGS),

	/* PD_PMU */
	COMPOSITE_FRACMUX(0, "clk_rtc32k_frac", "xin24m", CLK_IGNORE_UNUSED,
			RV1106_PMUCLKSEL_CON(6), 0,
			RV1106_PMUCLKGATE_CON(1), 14, GFLAGS,
			&rv1106_rtc32k_pmu_fracmux),
	DIV(CLK_100M_PMU, "clk_100m_pmu", "clk_200m_src", 0,
			RV1106_PMUCLKSEL_CON(0), 0, 3, DFLAGS),
	COMPOSITE_NODIV(PCLK_PMU_ROOT, "pclk_pmu_root", mux_100m_pmu_24m_p, CLK_IS_CRITICAL,
			RV1106_PMUCLKSEL_CON(0), 3, 1, MFLAGS,
			RV1106_PMUCLKGATE_CON(0), 1, GFLAGS),
	COMPOSITE_NODIV(HCLK_PMU_ROOT, "hclk_pmu_root", mux_200m_100m_24m_p, CLK_IS_CRITICAL,
			RV1106_PMUCLKSEL_CON(0), 4, 2, MFLAGS,
			RV1106_PMUCLKGATE_CON(0), 2, GFLAGS),
	GATE(CLK_PMU, "clk_pmu", "xin24m", CLK_IS_CRITICAL,
			RV1106_PMUCLKGATE_CON(1), 0, GFLAGS),
	GATE(PCLK_PMU, "pclk_pmu", "pclk_pmu_root", CLK_IS_CRITICAL,
			RV1106_PMUCLKGATE_CON(1), 1, GFLAGS),
	GATE(CLK_DDR_FAIL_SAFE, "clk_ddr_fail_safe", "clk_pmu", 0,
			RV1106_PMUCLKGATE_CON(1), 15, GFLAGS),
	GATE(PCLK_PMU_GPIO0, "pclk_pmu_gpio0", "pclk_pmu_root", 0,
			RV1106_PMUCLKGATE_CON(1), 2, GFLAGS),
	COMPOSITE_NODIV(DBCLK_PMU_GPIO0, "dbclk_pmu_gpio0", mux_24m_32k_p, 0,
			RV1106_PMUCLKSEL_CON(0), 15, 1, MFLAGS,
			RV1106_PMUCLKGATE_CON(1), 3, GFLAGS),
	GATE(PCLK_I2C1, "pclk_i2c1", "pclk_pmu_root", 0,
			RV1106_PMUCLKGATE_CON(0), 3, GFLAGS),
	COMPOSITE_NODIV(CLK_I2C1, "clk_i2c1", mux_200m_100m_24m_32k_p, 0,
			RV1106_PMUCLKSEL_CON(0), 6, 2, MFLAGS,
			RV1106_PMUCLKGATE_CON(0), 4, GFLAGS),
	GATE(PCLK_PMU_MAILBOX, "pclk_pmu_mailbox", "pclk_pmu_root", 0,
			RV1106_PMUCLKGATE_CON(2), 10, GFLAGS),
	GATE(CLK_PMU_MCU, "clk_pmu_mcu", "hclk_pmu_root", 0,
			RV1106_PMUCLKGATE_CON(0), 9, GFLAGS),
	GATE(CLK_PMU_MCU_RTC, "clk_pmu_mcu_rtc", "xin24m", 0,
			RV1106_PMUCLKGATE_CON(0), 13, GFLAGS),
	COMPOSITE_NOMUX(CLK_PVTM_PMU, "clk_pvtm_pmu", "xin24m", 0,
			RV1106_PMUCLKSEL_CON(1), 0, 5, DFLAGS,
			RV1106_PMUCLKGATE_CON(1), 4, GFLAGS),
	GATE(PCLK_PVTM_PMU, "pclk_pvtm_pmu", "pclk_pmu_root", 0,
			RV1106_PMUCLKGATE_CON(1), 5, GFLAGS),
	GATE(CLK_REFOUT, "clk_refout", "xin24m", 0,
			RV1106_PMUCLKGATE_CON(2), 13, GFLAGS),
	GATE(HCLK_PMU_SRAM, "hclk_pmu_sram", "hclk_pmu_root", CLK_IGNORE_UNUSED,
			RV1106_PMUCLKGATE_CON(0), 8, GFLAGS),
	GATE(PCLK_PMU_WDT, "pclk_pmu_wdt", "pclk_pmu_root", 0,
			RV1106_PMUCLKGATE_CON(2), 8, GFLAGS),
	COMPOSITE_NODIV(TCLK_PMU_WDT, "tclk_pmu_wdt", mux_24m_32k_p, 0,
			RV1106_PMUCLKSEL_CON(7), 2, 1, MFLAGS,
			RV1106_PMUCLKGATE_CON(2), 9, GFLAGS),

	/* PD_SUBDDR */
	COMPOSITE(CLK_CORE_DDRC_SRC, "clk_core_ddrc_src", mux_dpll_300m_p, CLK_IGNORE_UNUSED,
			RV1106_SUBDDRCLKSEL_CON(0), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RV1106_SUBDDRCLKGATE_CON(0), 2, GFLAGS),
	GATE(CLK_DFICTRL, "clk_dfictrl", "clk_core_ddrc_src", CLK_IGNORE_UNUSED,
			RV1106_SUBDDRCLKGATE_CON(0), 5, GFLAGS),
	GATE(CLK_DDRMON, "clk_ddrmon", "clk_core_ddrc_src", CLK_IGNORE_UNUSED,
			RV1106_SUBDDRCLKGATE_CON(0), 4, GFLAGS),
	GATE(CLK_DDR_PHY, "clk_ddr_phy", "clk_core_ddrc_src", CLK_IGNORE_UNUSED,
			RV1106_SUBDDRCLKGATE_CON(0), 6, GFLAGS),
	GATE(ACLK_DDRC, "aclk_ddrc", "clk_core_ddrc_src", CLK_IS_CRITICAL,
			RV1106_SUBDDRCLKGATE_CON(0), 1, GFLAGS),
	GATE(CLK_CORE_DDRC, "clk_core_ddrc", "clk_core_ddrc_src", CLK_IS_CRITICAL,
			RV1106_SUBDDRCLKGATE_CON(0), 3, GFLAGS),


	/* PD_VEPU */
	COMPOSITE_NODIV(HCLK_VEPU_ROOT, "hclk_vepu_root", mux_200m_100m_50m_24m_p, CLK_IS_CRITICAL,
			RV1106_VEPUCLKSEL_CON(0), 0, 2, MFLAGS,
			RV1106_VEPUCLKGATE_CON(0), 0, GFLAGS),
	COMPOSITE_NODIV(ACLK_VEPU_COM_ROOT, "aclk_vepu_com_root", mux_400m_200m_100m_24m_p, CLK_IS_CRITICAL,
			RV1106_VEPUCLKSEL_CON(0), 2, 2, MFLAGS,
			RV1106_VEPUCLKGATE_CON(0), 1, GFLAGS),
	COMPOSITE_NODIV(ACLK_VEPU_ROOT, "aclk_vepu_root", mux_300m_200m_100m_24m_p, CLK_IS_CRITICAL,
			RV1106_VEPUCLKSEL_CON(0), 4, 2, MFLAGS,
			RV1106_VEPUCLKGATE_CON(0), 2, GFLAGS),
	COMPOSITE_NODIV(PCLK_VEPU_ROOT, "pclk_vepu_root", mux_100m_50m_24m_p, CLK_IS_CRITICAL,
			RV1106_VEPUCLKSEL_CON(0), 6, 2, MFLAGS,
			RV1106_VEPUCLKGATE_CON(0), 3, GFLAGS),
	GATE(PCLK_SPI0, "pclk_spi0", "pclk_vepu_root", 0,
			RV1106_VEPUCLKGATE_CON(1), 2, GFLAGS),
	COMPOSITE_NODIV(CLK_SPI0, "clk_spi0", mux_200m_100m_50m_24m_p, 0,
			RV1106_VEPUCLKSEL_CON(0), 12, 2, MFLAGS,
			RV1106_VEPUCLKGATE_CON(1), 3, GFLAGS),
	GATE(CLK_UART_DETN_FLT, "clk_uart_detn_flt", "xin24m", 0,
			RV1106_VEPUCLKGATE_CON(1), 8, GFLAGS),
	GATE(HCLK_VEPU, "hclk_vepu", "hclk_vepu_root", 0,
			RV1106_VEPUCLKGATE_CON(0), 8, GFLAGS),
	GATE(ACLK_VEPU, "aclk_vepu", "aclk_vepu_root", 0,
			RV1106_VEPUCLKGATE_CON(0), 9, GFLAGS),
	COMPOSITE_NODIV(CLK_CORE_VEPU, "clk_core_vepu", mux_400m_300m_pvtpll0_pvtpll1_p, 0,
			RV1106_VEPUCLKSEL_CON(0), 8, 2, MFLAGS,
			RV1106_VEPUCLKGATE_CON(0), 10, GFLAGS),
	COMPOSITE_NODIV(CLK_CORE_VEPU_DVBM, "clk_core_vepu_dvbm", mux_200m_100m_50m_24m_p, 0,
			RV1106_VEPUCLKSEL_CON(0), 10, 2, MFLAGS,
			RV1106_VEPUCLKGATE_CON(0), 13, GFLAGS),
	GATE(PCLK_GPIO1, "pclk_gpio1", "pclk_vepu_root", 0,
			RV1106_VEPUCLKGATE_CON(0), 15, GFLAGS),
	GATE(DBCLK_GPIO1, "dbclk_gpio1", "xin24m", 0,
			RV1106_VEPUCLKGATE_CON(1), 0, GFLAGS),
	GATE(HCLK_VEPU_PP, "hclk_vepu_pp", "hclk_vepu_root", 0,
			RV1106_VEPUCLKGATE_CON(0), 11, GFLAGS),
	GATE(ACLK_VEPU_PP, "aclk_vepu_pp", "aclk_vepu_root", 0,
			RV1106_VEPUCLKGATE_CON(0), 12, GFLAGS),

	/* PD_VI */
	COMPOSITE_NODIV(HCLK_VI_ROOT, "hclk_vi_root", mux_150m_100m_50m_24m_p, CLK_IS_CRITICAL,
			RV1106_VICLKSEL_CON(0), 0, 2, MFLAGS,
			RV1106_VICLKGATE_CON(0), 0, GFLAGS),
	COMPOSITE_NODIV(ACLK_VI_ROOT, "aclk_vi_root", mux_339m_200m_100m_24m_p, CLK_IS_CRITICAL,
			RV1106_VICLKSEL_CON(0), 2, 2, MFLAGS,
			RV1106_VICLKGATE_CON(0), 1, GFLAGS),
	COMPOSITE_NODIV(PCLK_VI_ROOT, "pclk_vi_root", mux_150m_100m_50m_24m_p, CLK_IS_CRITICAL,
			RV1106_VICLKSEL_CON(0), 4, 2, MFLAGS,
			RV1106_VICLKGATE_CON(0), 2, GFLAGS),
	COMPOSITE_NODIV(PCLK_VI_RTC_ROOT, "pclk_vi_rtc_root", mux_50m_24m_p, 0,
			RV1106_VICLKSEL_CON(0), 6, 1, MFLAGS,
			RV1106_VICLKGATE_CON(0), 3, GFLAGS),

	GATE(PCLK_CSIHOST0, "pclk_csihost0", "pclk_vi_root", 0,
			RV1106_VICLKGATE_CON(1), 3, GFLAGS),
	GATE(PCLK_CSIHOST1, "pclk_csihost1", "pclk_vi_root", 0,
			RV1106_VICLKGATE_CON(1), 5, GFLAGS),
	GATE(PCLK_GPIO3, "pclk_gpio3", "pclk_vi_root", 0,
			RV1106_VICLKGATE_CON(1), 15, GFLAGS),
	GATE(DBCLK_GPIO3, "dbclk_gpio3", "xin24m", 0,
			RV1106_VICLKGATE_CON(2), 0, GFLAGS),
	GATE(HCLK_ISP3P2, "hclk_isp3p2", "hclk_vi_root", 0,
			RV1106_VICLKGATE_CON(0), 7, GFLAGS),
	GATE(ACLK_ISP3P2, "aclk_isp3p2", "aclk_vi_root", 0,
			RV1106_VICLKGATE_CON(0), 8, GFLAGS),
	COMPOSITE_NODIV(CLK_CORE_ISP3P2, "clk_core_isp3p2", mux_339m_200m_pvtpll0_pvtpll1_p, 0,
			RV1106_VICLKSEL_CON(0), 7, 2, MFLAGS,
			RV1106_VICLKGATE_CON(0), 9, GFLAGS),
	GATE(PCLK_MIPICSIPHY, "pclk_mipicsiphy", "pclk_vi_root", 0,
			RV1106_VICLKGATE_CON(1), 14, GFLAGS),
	COMPOSITE(CCLK_SRC_SDMMC, "cclk_src_sdmmc", mux_400m_24m_p, 0,
			RV1106_VICLKSEL_CON(1), 14, 1, MFLAGS, 8, 6, DFLAGS,
			RV1106_VICLKGATE_CON(1), 11, GFLAGS),
	GATE(HCLK_SDMMC, "hclk_sdmmc", "hclk_vi_root", 0,
			RV1106_VICLKGATE_CON(1), 12, GFLAGS),
	GATE(CLK_SDMMC_DETN_FLT, "clk_sdmmc_detn_flt", "xin24m", 0,
			RV1106_VICLKGATE_CON(1), 13, GFLAGS),
	GATE(PCLK_VI_RTC_TEST, "pclk_vi_rtc_test", "pclk_vi_rtc_root", 0,
			RV1106_VICLKGATE_CON(2), 5, GFLAGS),
	GATE(PCLK_VI_RTC_PHY, "pclk_vi_rtc_phy", "pclk_vi_rtc_root", 0,
			RV1106_VICLKGATE_CON(2), 6, GFLAGS),
	COMPOSITE_NODIV(DCLK_VICAP, "dclk_vicap", mux_339m_200m_100m_24m_p, 0,
			RV1106_VICLKSEL_CON(0), 9, 2, MFLAGS,
			RV1106_VICLKGATE_CON(0), 10, GFLAGS),
	GATE(ACLK_VICAP, "aclk_vicap", "aclk_vi_root", 0,
			RV1106_VICLKGATE_CON(0), 12, GFLAGS),
	GATE(HCLK_VICAP, "hclk_vicap", "hclk_vi_root", 0,
			RV1106_VICLKGATE_CON(0), 13, GFLAGS),

	/* PD_VO */
	COMPOSITE_NODIV(ACLK_MAC_ROOT, "aclk_mac_root", mux_300m_200m_100m_24m_p, 0,
			RV1106_VOCLKSEL_CON(1), 12, 2, MFLAGS,
			RV1106_VOCLKGATE_CON(1), 4, GFLAGS),
	COMPOSITE_NODIV(ACLK_VO_ROOT, "aclk_vo_root", mux_400m_200m_100m_24m_p, CLK_IS_CRITICAL,
			RV1106_VOCLKSEL_CON(0), 0, 2, MFLAGS,
			RV1106_VOCLKGATE_CON(0), 0, GFLAGS),
	COMPOSITE_NODIV(HCLK_VO_ROOT, "hclk_vo_root", mux_200m_100m_50m_24m_p, CLK_IS_CRITICAL,
			RV1106_VOCLKSEL_CON(0), 2, 2, MFLAGS,
			RV1106_VOCLKGATE_CON(0), 1, GFLAGS),
	COMPOSITE_NODIV(PCLK_VO_ROOT, "pclk_vo_root", mux_150m_100m_50m_24m_p, CLK_IS_CRITICAL,
			RV1106_VOCLKSEL_CON(0), 4, 2, MFLAGS,
			RV1106_VOCLKGATE_CON(0), 2, GFLAGS),
	COMPOSITE_NODIV(ACLK_VOP_ROOT, "aclk_vop_root", mux_300m_200m_100m_24m_p, 0,
			RV1106_VOCLKSEL_CON(1), 10, 2, MFLAGS,
			RV1106_VOCLKGATE_CON(0), 11, GFLAGS),

	GATE(PCLK_GPIO2, "pclk_gpio2", "pclk_vo_root", 0,
			RV1106_VOCLKGATE_CON(3), 0, GFLAGS),
	GATE(DBCLK_GPIO2, "dbclk_gpio2", "xin24m", 0,
			RV1106_VOCLKGATE_CON(3), 1, GFLAGS),
	GATE(ACLK_MAC, "aclk_mac", "aclk_mac_root", 0,
			RV1106_VOCLKGATE_CON(1), 8, GFLAGS),
	GATE(PCLK_MAC, "pclk_mac", "pclk_vo_root", 0,
			RV1106_VOCLKGATE_CON(1), 9, GFLAGS),
	FACTOR(CLK_GMAC0_50M_O, "clk_gmac0_50m_o", "clk_50m_src", 0, 1, 1),
	FACTOR(CLK_GMAC0_REF_50M, "clk_gmac0_ref_50m", "clk_gmac0_50m_o", 0, 1, 1),
	DIV(CLK_GMAC0_TX_50M_O, "clk_gmac0_tx_50m_o", "clk_gmac0_50m_o", 0,
			RV1106_VOCLKSEL_CON(2), 1, 6, DFLAGS),
	GATE(CLK_MACPHY, "clk_macphy", "xin24m", 0,
			RV1106_VOCLKGATE_CON(2), 13, GFLAGS),
	GATE(CLK_OTPC_ARB, "clk_otpc_arb", "xin24m", 0,
			RV1106_VOCLKGATE_CON(2), 11, GFLAGS),
	GATE(PCLK_OTPC_NS, "pclk_otpc_ns", "pclk_vo_root", 0,
			RV1106_VOCLKGATE_CON(2), 3, GFLAGS),
	GATE(CLK_SBPI_OTPC_NS, "clk_sbpi_otpc_ns", "xin24m", 0,
			RV1106_VOCLKGATE_CON(2), 5, GFLAGS),
	COMPOSITE_NOMUX(CLK_USER_OTPC_NS, "clk_user_otpc_ns", "xin24m", 0,
			RV1106_VOCLKSEL_CON(3), 10, 3, DFLAGS,
			RV1106_VOCLKGATE_CON(2), 6, GFLAGS),
	GATE(PCLK_OTPC_S, "pclk_otpc_s", "pclk_vo_root", 0,
			RV1106_VOCLKGATE_CON(2), 7, GFLAGS),
	GATE(CLK_SBPI_OTPC_S, "clk_sbpi_otpc_s", "xin24m", 0,
			RV1106_VOCLKGATE_CON(2), 9, GFLAGS),
	COMPOSITE_NOMUX(CLK_USER_OTPC_S, "clk_user_otpc_s", "xin24m", 0,
			RV1106_VOCLKSEL_CON(3), 13, 3, DFLAGS,
			RV1106_VOCLKGATE_CON(2), 10, GFLAGS),
	GATE(PCLK_OTP_MASK, "pclk_otp_mask", "pclk_vo_root", 0,
			RV1106_VOCLKGATE_CON(2), 14, GFLAGS),
	GATE(CLK_PMC_OTP, "clk_pmc_otp", "clk_sbpi_otpc_s", 0,
			RV1106_VOCLKGATE_CON(2), 15, GFLAGS),
	GATE(HCLK_RGA2E, "hclk_rga2e", "hclk_vo_root", 0,
			RV1106_VOCLKGATE_CON(0), 7, GFLAGS),
	GATE(ACLK_RGA2E, "aclk_rga2e", "aclk_vo_root", 0,
			RV1106_VOCLKGATE_CON(0), 8, GFLAGS),
	COMPOSITE_NODIV(CLK_CORE_RGA2E, "clk_core_rga2e", mux_400m_200m_100m_24m_p, 0,
			RV1106_VOCLKSEL_CON(1), 8, 2, MFLAGS,
			RV1106_VOCLKGATE_CON(0), 9, GFLAGS),
	COMPOSITE(CCLK_SRC_SDIO, "cclk_src_sdio", mux_400m_24m_p, 0,
			RV1106_VOCLKSEL_CON(2), 13, 1, MFLAGS, 7, 6, DFLAGS,
			RV1106_VOCLKGATE_CON(1), 14, GFLAGS),
	GATE(HCLK_SDIO, "hclk_sdio", "hclk_vo_root", 0,
			RV1106_VOCLKGATE_CON(1), 15, GFLAGS),
	GATE(PCLK_TSADC, "pclk_tsadc", "pclk_vo_root", 0,
			RV1106_VOCLKGATE_CON(2), 0, GFLAGS),
	COMPOSITE_NOMUX(CLK_TSADC, "clk_tsadc", "xin24m", 0,
			RV1106_VOCLKSEL_CON(3), 0, 5, DFLAGS,
			RV1106_VOCLKGATE_CON(2), 1, GFLAGS),
	COMPOSITE_NOMUX(CLK_TSADC_TSEN, "clk_tsadc_tsen", "xin24m", 0,
			RV1106_VOCLKSEL_CON(3), 5, 5, DFLAGS,
			RV1106_VOCLKGATE_CON(2), 2, GFLAGS),
	GATE(HCLK_VOP, "hclk_vop", "hclk_vo_root", 0,
			RV1106_VOCLKGATE_CON(0), 13, GFLAGS),
	GATE(DCLK_VOP, "dclk_vop", "dclk_vop_src", 0,
			RV1106_VOCLKGATE_CON(0), 14, GFLAGS),
	GATE(ACLK_VOP, "aclk_vop", "aclk_vop_root", 0,
			RV1106_VOCLKGATE_CON(0), 15, GFLAGS),

	/* IO CLK */
	GATE(RX0PCLK_VICAP, "rx0pclk_vicap", "rx0pclk_vicap_io", 0,
			RV1106_VICLKGATE_CON(1), 0, GFLAGS),
	GATE(RX1PCLK_VICAP, "rx1pclk_vicap", "rx1pclk_vicap_io", 0,
			RV1106_VICLKGATE_CON(1), 1, GFLAGS),
	GATE(ISP0CLK_VICAP, "isp0clk_vicap", "isp0clk_vicap_io", 0,
			RV1106_VICLKGATE_CON(1), 2, GFLAGS),
	GATE(I0CLK_VICAP, "i0clk_vicap", "i0clk_vicap_io", 0,
			RV1106_VICLKGATE_CON(0), 14, GFLAGS),
	GATE(I1CLK_VICAP, "i1clk_vicap", "i1clk_vicap_io", 0,
			RV1106_VICLKGATE_CON(0), 15, GFLAGS),
	GATE(PCLK_VICAP, "pclk_vicap", "pclk_vicap_io", 0,
			RV1106_VICLKGATE_CON(0), 11, GFLAGS),
	GATE(CLK_RXBYTECLKHS_0, "clk_rxbyteclkhs_0", "clk_rxbyteclkhs_0_io", 0,
			RV1106_VICLKGATE_CON(1), 4, GFLAGS),
	GATE(CLK_RXBYTECLKHS_1, "clk_rxbyteclkhs_1", "clk_rxbyteclkhs_1_io", 0,
			RV1106_VICLKGATE_CON(1), 6, GFLAGS),

	GATE(PCLK_VICAP_VEPU, "pclk_vicap_vepu", "pclk_vicap_vepu_io", 0,
			RV1106_VEPUCLKGATE_CON(0), 14, GFLAGS),
	GATE(SCLK_IN_SPI0, "sclk_in_spi0", "sclk_in_spi0_io", 0,
			RV1106_VEPUCLKGATE_CON(1), 4, GFLAGS),

	GATE(CLK_UTMI_USBOTG, "clk_utmi_usbotg", "clk_utmi_usbotg_io", 0,
			RV1106_PERICLKGATE_CON(4), 9, GFLAGS),

};

static struct rockchip_clk_branch rv1106_grf_clk_branches[] __initdata = {
	MMC(SCLK_EMMC_DRV, "emmc_drv", "cclk_src_emmc", RV1106_EMMC_CON0, 1),
	MMC(SCLK_EMMC_SAMPLE, "emmc_sample", "cclk_src_emmc", RV1106_EMMC_CON1, 1),
	MMC(SCLK_SDMMC_DRV,     "sdmmc_drv",    "cclk_src_sdmmc", RV1106_SDMMC_CON0, 1),
	MMC(SCLK_SDMMC_SAMPLE,  "sdmmc_sample", "cclk_src_sdmmc", RV1106_SDMMC_CON1, 1),
	MMC(SCLK_SDIO_DRV, "sdio_drv", "cclk_src_sdio", RV1106_SDIO_CON0, 1),
	MMC(SCLK_SDIO_SAMPLE, "sdio_sample", "cclk_src_sdio", RV1106_SDIO_CON1, 1),
};

static void __iomem *rv1106_cru_base;
static struct rockchip_clk_provider *grf_ctx, *cru_ctx;

void rv1106_dump_cru(void)
{
	if (rv1106_cru_base) {
		pr_warn("CRU:\n");
		print_hex_dump(KERN_WARNING, "", DUMP_PREFIX_OFFSET,
			       32, 4, rv1106_cru_base,
			       0x588, false);
	}
}
EXPORT_SYMBOL_GPL(rv1106_dump_cru);

static void _cru_pvtpll_calibrate(int count_offset, int length_offset, int target_rate)
{
	unsigned int rate0, rate1, delta, length_ori, length, step, val, i = 0;

	rate0 = readl_relaxed(rv1106_cru_base + count_offset);
	if (rate0 < target_rate)
		return;
	/* delta < (3.125% * target_rate) */
	if ((rate0 - target_rate) < (target_rate >> 5))
		return;

	length_ori = readl_relaxed(rv1106_cru_base + length_offset) & PVTPLL_LENGTH_SEL_MASK;
	length = length_ori;
	length++;
	val = HIWORD_UPDATE(length, PVTPLL_LENGTH_SEL_MASK, PVTPLL_LENGTH_SEL_SHIFT);
	writel_relaxed(val, rv1106_cru_base + length_offset);
	usleep_range(2000, 2100);
	rate1 = readl_relaxed(rv1106_cru_base + count_offset);
	if ((rate1 < target_rate) || (rate1 >= rate0))
		return;
	if (abs(rate1 - target_rate) < (target_rate >> 5))
		return;

	step = rate0 - rate1;
	delta = rate1 - target_rate;
	length += delta / step;
	val = HIWORD_UPDATE(length, PVTPLL_LENGTH_SEL_MASK, PVTPLL_LENGTH_SEL_SHIFT);
	writel_relaxed(val, rv1106_cru_base + length_offset);
	usleep_range(2000, 2100);
	rate0 = readl_relaxed(rv1106_cru_base + count_offset);

	while (abs(rate0 - target_rate) >= (target_rate >> 5)) {
		if (i++ > 20)
			break;
		if (rate0 > target_rate)
			length++;
		else
			length--;
		if (length <= length_ori)
			break;
		val = HIWORD_UPDATE(length, PVTPLL_LENGTH_SEL_MASK, PVTPLL_LENGTH_SEL_SHIFT);
		writel_relaxed(val, rv1106_cru_base + length_offset);
		usleep_range(2000, 2100);
		rate0 = readl_relaxed(rv1106_cru_base + count_offset);
	}
}

static void _grf_pvtpll_calibrate(int count_offset, int length_offset, int target_rate)
{
	unsigned int rate0, rate1, delta, length_ori, length, step, val, i = 0;

	regmap_read(cru_ctx->grf, count_offset, &rate0);
	if (rate0 < target_rate)
		return;
	/* delta < (3.125% * target_rate) */
	if ((rate0 - target_rate) < (target_rate >> 5))
		return;

	regmap_read(cru_ctx->grf, length_offset, &length_ori);
	length = length_ori;
	length_ori = length;
	length &= PVTPLL_LENGTH_SEL_MASK;
	length++;
	val = HIWORD_UPDATE(length, PVTPLL_LENGTH_SEL_MASK, PVTPLL_LENGTH_SEL_SHIFT);
	regmap_write(cru_ctx->grf, length_offset, val);
	usleep_range(2000, 2100);
	regmap_read(cru_ctx->grf, count_offset, &rate1);
	if ((rate1 < target_rate) || (rate1 >= rate0))
		return;
	if (abs(rate1 - target_rate) < (target_rate >> 5))
		return;

	step = rate0 - rate1;
	delta = rate1 - target_rate;
	length += delta / step;
	val = HIWORD_UPDATE(length, PVTPLL_LENGTH_SEL_MASK, PVTPLL_LENGTH_SEL_SHIFT);
	regmap_write(cru_ctx->grf, length_offset, val);
	usleep_range(2000, 2100);
	regmap_read(cru_ctx->grf, count_offset, &rate0);

	while (abs(rate0 - target_rate) >= (target_rate >> 5)) {
		if (i++ > 20)
			break;
		if (rate0 > target_rate)
			length++;
		else
			length--;
		if (length <= length_ori)
			break;
		val = HIWORD_UPDATE(length, PVTPLL_LENGTH_SEL_MASK, PVTPLL_LENGTH_SEL_SHIFT);
		regmap_write(cru_ctx->grf, length_offset, val);
		usleep_range(2000, 2100);
		regmap_read(cru_ctx->grf, count_offset, &rate0);
	}
}

static void rockchip_rv1106_pvtpll_calibrate(struct work_struct *w)
{
	struct clk *clk;
	unsigned long rate;

	clk = __clk_lookup("clk_pvtpll_0");
	if (clk) {
		rate = clk_get_rate(clk);
		_cru_pvtpll_calibrate(CRU_PVTPLL0_OSC_CNT_AVG,
				      CRU_PVTPLL0_CON0_H, rate / 1000000);
	}

	clk = __clk_lookup("clk_pvtpll_1");
	if (clk) {
		rate = clk_get_rate(clk);
		_cru_pvtpll_calibrate(CRU_PVTPLL1_OSC_CNT_AVG,
				      CRU_PVTPLL1_CON0_H, rate / 1000000);
	}

	clk = __clk_lookup("cpu_pvtpll");
	if (clk) {
		rate = clk_get_rate(clk);
		_grf_pvtpll_calibrate(CPU_PVTPLL_OSC_CNT_AVG,
				      CPU_PVTPLL_CON0_H, rate / 1000000);
	}
}
static DECLARE_DEFERRABLE_WORK(pvtpll_calibrate_work, rockchip_rv1106_pvtpll_calibrate);

static void rockchip_rv1106_pvtpll_init(struct rockchip_clk_provider *ctx)
{
	/* set pvtpll ref clk mux */
	writel_relaxed(CPU_PVTPLL_PATH_CORE, ctx->reg_base + CPU_CLK_PATH_BASE);

	regmap_write(ctx->grf, CPU_PVTPLL_CON0_H, HIWORD_UPDATE(0x6, PVTPLL_LENGTH_SEL_MASK,
		     PVTPLL_LENGTH_SEL_SHIFT));
	regmap_write(ctx->grf, CPU_PVTPLL_CON0_L, HIWORD_UPDATE(0x1, PVTPLL_RING_SEL_MASK,
		     PVTPLL_RING_SEL_SHIFT));
	regmap_write(ctx->grf, CPU_PVTPLL_CON0_L, HIWORD_UPDATE(0x3, PVTPLL_EN_MASK,
		     PVTPLL_EN_SHIFT));

	writel_relaxed(0x007f0000, ctx->reg_base + CRU_PVTPLL0_CON0_H);
	writel_relaxed(0xffff0018, ctx->reg_base + CRU_PVTPLL0_CON1_L);
	writel_relaxed(0xffff0004, ctx->reg_base + CRU_PVTPLL0_CON2_H);
	writel_relaxed(0x00030003, ctx->reg_base + CRU_PVTPLL0_CON0_L);

	writel_relaxed(0x007f0000, ctx->reg_base + CRU_PVTPLL1_CON0_H);
	writel_relaxed(0xffff0018, ctx->reg_base + CRU_PVTPLL1_CON1_L);
	writel_relaxed(0xffff0004, ctx->reg_base + CRU_PVTPLL1_CON2_H);
	writel_relaxed(0x00030003, ctx->reg_base + CRU_PVTPLL1_CON0_L);

	schedule_delayed_work(&pvtpll_calibrate_work, msecs_to_jiffies(3000));
}

static int rv1106_clk_panic(struct notifier_block *this,
			    unsigned long ev, void *ptr)
{
	rv1106_dump_cru();
	return NOTIFY_DONE;
}

static struct notifier_block rv1106_clk_panic_block = {
	.notifier_call = rv1106_clk_panic,
};

static void __init rv1106_clk_init(struct device_node *np)
{
	struct rockchip_clk_provider *ctx;
	void __iomem *reg_base;
	struct clk **cru_clks;

	reg_base = of_iomap(np, 0);
	if (!reg_base) {
		pr_err("%s: could not map cru region\n", __func__);
		return;
	}

	rv1106_cru_base = reg_base;

	ctx = rockchip_clk_init(np, reg_base, CLK_NR_CLKS);
	if (IS_ERR(ctx)) {
		pr_err("%s: rockchip clk init failed\n", __func__);
		iounmap(reg_base);
		return;
	}
	cru_ctx = ctx;

	rockchip_rv1106_pvtpll_init(ctx);

	cru_clks = ctx->clk_data.clks;

	rockchip_clk_register_plls(ctx, rv1106_pll_clks,
				   ARRAY_SIZE(rv1106_pll_clks),
				   RV1106_GRF_SOC_STATUS0);

	rockchip_clk_register_armclk(ctx, ARMCLK, "armclk",
				     3, cru_clks[PLL_APLL], cru_clks[PLL_GPLL],
				     &rv1106_cpuclk_data, rv1106_cpuclk_rates,
				     ARRAY_SIZE(rv1106_cpuclk_rates));

	rockchip_clk_register_branches(ctx, rv1106_clk_branches,
				       ARRAY_SIZE(rv1106_clk_branches));

	rockchip_clk_register_branches(grf_ctx, rv1106_grf_clk_branches,
				       ARRAY_SIZE(rv1106_grf_clk_branches));

	rockchip_register_softrst(np, 31745, reg_base + RV1106_PMUSOFTRST_CON(0),
				  ROCKCHIP_SOFTRST_HIWORD_MASK);

	rockchip_register_restart_notifier(ctx, RV1106_GLB_SRST_FST, NULL);

	rockchip_clk_of_add_provider(np, ctx);

	atomic_notifier_chain_register(&panic_notifier_list,
				       &rv1106_clk_panic_block);
}

CLK_OF_DECLARE(rv1106_cru, "rockchip,rv1106-cru", rv1106_clk_init);

static void __init rv1106_grf_clk_init(struct device_node *np)
{
	struct rockchip_clk_provider *ctx;
	void __iomem *reg_base;

	reg_base = of_iomap(of_get_parent(np), 0);
	if (!reg_base) {
		pr_err("%s: could not map cru grf region\n", __func__);
		return;
	}

	ctx = rockchip_clk_init(np, reg_base, CLK_NR_GRF_CLKS);
	if (IS_ERR(ctx)) {
		pr_err("%s: rockchip grf clk init failed\n", __func__);
		return;
	}
	grf_ctx = ctx;

	rockchip_clk_of_add_provider(np, ctx);
}
CLK_OF_DECLARE(rv1106_grf_cru, "rockchip,rv1106-grf-cru", rv1106_grf_clk_init);

#ifdef MODULE
struct clk_rv1106_inits {
	void (*inits)(struct device_node *np);
};

static const struct clk_rv1106_inits clk_rv1106_init = {
	.inits = rv1106_clk_init,
};

static const struct clk_rv1106_inits clk_rv1106_grf_init = {
	.inits = rv1106_grf_clk_init,
};

static const struct of_device_id clk_rv1106_match_table[] = {
	{
		.compatible = "rockchip,rv1106-cru",
		.data = &clk_rv1106_init,
	}, {
		.compatible = "rockchip,rv1106-grf-cru",
		.data = &clk_rv1106_grf_init,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, clk_rv1106_match_table);

static int __init clk_rv1106_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match;
	const struct clk_rv1106_inits *init_data;

	match = of_match_device(clk_rv1106_match_table, &pdev->dev);
	if (!match || !match->data)
		return -EINVAL;

	init_data = match->data;
	if (init_data->inits)
		init_data->inits(np);

	return 0;
}

static struct platform_driver clk_rv1106_driver = {
	.driver		= {
		.name	= "clk-rv1106",
		.of_match_table = clk_rv1106_match_table,
	},
};
builtin_platform_driver_probe(clk_rv1106_driver, clk_rv1106_probe);

MODULE_DESCRIPTION("Rockchip RV1106 Clock Driver");
MODULE_LICENSE("GPL");
#endif /* MODULE */
