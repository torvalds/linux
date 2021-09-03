// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 * Author: Elaine Zhang <zhangqing@rock-chips.com>
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/syscore_ops.h>
#include <dt-bindings/clock/rk3588-cru.h>
#include "clk.h"

#define RK3588_PHPGRF_CLK_CON1		0x70
#define RK3588_GRF_SOC_STATUS0		0x600
#define RK3588_FRAC_MAX_PRATE		600000000
#define RK3588_UART_FRAC_MAX_PRATE	600000000

enum rk3588_plls {
	b0pll, b1pll, lpll, v0pll, aupll, cpll, gpll, npll, ppll,
};

static struct rockchip_pll_rate_table rk3588_pll_rates[] = {
	/* _mhz, _p, _m, _s, _k */
	RK3588_PLL_RATE(2520000000, 2, 210, 0, 0),
	RK3588_PLL_RATE(2496000000, 2, 208, 0, 0),
	RK3588_PLL_RATE(2472000000, 2, 206, 0, 0),
	RK3588_PLL_RATE(2448000000, 2, 204, 0, 0),
	RK3588_PLL_RATE(2424000000, 2, 202, 0, 0),
	RK3588_PLL_RATE(2400000000, 2, 200, 0, 0),
	RK3588_PLL_RATE(2376000000, 2, 198, 0, 0),
	RK3588_PLL_RATE(2352000000, 2, 196, 0, 0),
	RK3588_PLL_RATE(2328000000, 2, 194, 0, 0),
	RK3588_PLL_RATE(2304000000, 2, 192, 0, 0),
	RK3588_PLL_RATE(2280000000, 2, 190, 0, 0),
	RK3588_PLL_RATE(2256000000, 2, 376, 1, 0),
	RK3588_PLL_RATE(2232000000, 2, 372, 1, 0),
	RK3588_PLL_RATE(2208000000, 2, 368, 1, 0),
	RK3588_PLL_RATE(2184000000, 2, 364, 1, 0),
	RK3588_PLL_RATE(2160000000, 2, 360, 1, 0),
	RK3588_PLL_RATE(2136000000, 2, 356, 1, 0),
	RK3588_PLL_RATE(2112000000, 2, 352, 1, 0),
	RK3588_PLL_RATE(2088000000, 2, 348, 1, 0),
	RK3588_PLL_RATE(2064000000, 2, 344, 1, 0),
	RK3588_PLL_RATE(2040000000, 2, 340, 1, 0),
	RK3588_PLL_RATE(2016000000, 2, 336, 1, 0),
	RK3588_PLL_RATE(1992000000, 2, 332, 1, 0),
	RK3588_PLL_RATE(1968000000, 2, 328, 1, 0),
	RK3588_PLL_RATE(1944000000, 2, 324, 1, 0),
	RK3588_PLL_RATE(1920000000, 2, 320, 1, 0),
	RK3588_PLL_RATE(1896000000, 2, 316, 1, 0),
	RK3588_PLL_RATE(1872000000, 2, 312, 1, 0),
	RK3588_PLL_RATE(1848000000, 2, 308, 1, 0),
	RK3588_PLL_RATE(1824000000, 2, 304, 1, 0),
	RK3588_PLL_RATE(1800000000, 2, 300, 1, 0),
	RK3588_PLL_RATE(1776000000, 2, 296, 1, 0),
	RK3588_PLL_RATE(1752000000, 2, 292, 1, 0),
	RK3588_PLL_RATE(1728000000, 2, 288, 1, 0),
	RK3588_PLL_RATE(1704000000, 2, 284, 1, 0),
	RK3588_PLL_RATE(1680000000, 2, 280, 1, 0),
	RK3588_PLL_RATE(1656000000, 2, 276, 1, 0),
	RK3588_PLL_RATE(1632000000, 2, 272, 1, 0),
	RK3588_PLL_RATE(1608000000, 2, 268, 1, 0),
	RK3588_PLL_RATE(1584000000, 2, 264, 1, 0),
	RK3588_PLL_RATE(1560000000, 2, 260, 1, 0),
	RK3588_PLL_RATE(1536000000, 2, 256, 1, 0),
	RK3588_PLL_RATE(1512000000, 2, 252, 1, 0),
	RK3588_PLL_RATE(1488000000, 2, 248, 1, 0),
	RK3588_PLL_RATE(1464000000, 2, 244, 1, 0),
	RK3588_PLL_RATE(1440000000, 2, 240, 1, 0),
	RK3588_PLL_RATE(1416000000, 2, 236, 1, 0),
	RK3588_PLL_RATE(1392000000, 2, 232, 1, 0),
	RK3588_PLL_RATE(1320000000, 2, 220, 1, 0),
	RK3588_PLL_RATE(1200000000, 2, 200, 1, 0),
	RK3588_PLL_RATE(1188000000, 2, 198, 1, 0),
	RK3588_PLL_RATE(1008000000, 2, 336, 2, 0),
	RK3588_PLL_RATE(1000000000, 3, 500, 2, 0),
	RK3588_PLL_RATE(900000000, 2, 300, 2, 0),
	RK3588_PLL_RATE(816000000, 2, 272, 2, 0),
	RK3588_PLL_RATE(600000000, 2, 200, 2, 0),
	RK3588_PLL_RATE(594000000, 2, 198, 2, 0),
	RK3588_PLL_RATE(408000000, 2, 272, 3, 0),
	RK3588_PLL_RATE(312000000, 2, 208, 3, 0),
	RK3588_PLL_RATE(216000000, 2, 216, 3, 0),
	RK3588_PLL_RATE(96000000, 2, 216, 3, 0),
	{ /* sentinel */ },
};

#define RK3588_CLK_CORE_B0_SEL_CLEAN_MASK	0x3
#define RK3588_CLK_CORE_B0_SEL_CLEAN_SHIFT	13
#define RK3588_CLK_CORE_B0_GPLL_DIV_MASK	0x1f
#define RK3588_CLK_CORE_B0_GPLL_DIV_SHIFT	1
#define RK3588_CLK_CORE_L_SEL_CLEAN_MASK	0x3
#define RK3588_CLK_CORE_L_SEL_CLEAN_SHIFT	5
#define RK3588_CLK_DSU_SEL_DF_MASK		0x1
#define RK3588_CLK_DSU_SEL_DF_SHIFT		15
#define RK3588_CLK_DSU_DF_SRC_MASK		0x3
#define RK3588_CLK_DSU_DF_SRC_SHIFT		5
#define RK3588_CLK_DSU_DF_DIV_MASK		0x1f
#define RK3588_CLK_DSU_DF_DIV_SHIFT		0
#define RK3588_ACLKM_DSU_DIV_MASK		0x1f
#define RK3588_ACLKM_DSU_DIV_SHIFT		1
#define RK3588_ACLKS_DSU_DIV_MASK		0x1f
#define RK3588_ACLKS_DSU_DIV_SHIFT		6
#define RK3588_ACLKMP_DSU_DIV_MASK		0x1f
#define RK3588_ACLKMP_DSU_DIV_SHIFT		11
#define RK3588_PERIPH_DSU_DIV_MASK		0x1f
#define RK3588_PERIPH_DSU_DIV_SHIFT		0
#define RK3588_ATCLK_DSU_DIV_MASK		0x1f
#define RK3588_ATCLK_DSU_DIV_SHIFT		0
#define RK3588_GICCLK_DSU_DIV_MASK		0x1f
#define RK3588_GICCLK_DSU_DIV_SHIFT		5

#define RK3588_CORE_B0_SEL(_apllcore)						\
{										\
	.reg = RK3588_BIGCORE0_CLKSEL_CON(0),					\
	.val = HIWORD_UPDATE(_apllcore, RK3588_CLK_CORE_B0_SEL_CLEAN_MASK,	\
			RK3588_CLK_CORE_B0_SEL_CLEAN_SHIFT) |			\
		HIWORD_UPDATE(0, RK3588_CLK_CORE_B0_GPLL_DIV_MASK,		\
			RK3588_CLK_CORE_B0_GPLL_DIV_SHIFT),			\
}

#define RK3588_CORE_B1_SEL(_apllcore)						\
{										\
	.reg = RK3588_BIGCORE0_CLKSEL_CON(1),					\
	.val = HIWORD_UPDATE(_apllcore, RK3588_CLK_CORE_B0_SEL_CLEAN_MASK,	\
			RK3588_CLK_CORE_B0_SEL_CLEAN_SHIFT),			\
}

#define RK3588_CORE_B2_SEL(_apllcore)						\
{										\
	.reg = RK3588_BIGCORE1_CLKSEL_CON(0),					\
	.val = HIWORD_UPDATE(_apllcore, RK3588_CLK_CORE_B0_SEL_CLEAN_MASK,	\
			RK3588_CLK_CORE_B0_SEL_CLEAN_SHIFT) |			\
		HIWORD_UPDATE(0, RK3588_CLK_CORE_B0_GPLL_DIV_MASK,		\
			RK3588_CLK_CORE_B0_GPLL_DIV_SHIFT),			\
}

#define RK3588_CORE_B3_SEL(_apllcore)						\
{										\
	.reg = RK3588_BIGCORE1_CLKSEL_CON(1),					\
	.val = HIWORD_UPDATE(_apllcore, RK3588_CLK_CORE_B0_SEL_CLEAN_MASK,	\
			RK3588_CLK_CORE_B0_SEL_CLEAN_SHIFT),			\
}

#define RK3588_CORE_L_SEL0(_offs, _apllcore)					\
{										\
	.reg = RK3588_DSU_CLKSEL_CON(6 + _offs),				\
	.val = HIWORD_UPDATE(_apllcore, RK3588_CLK_CORE_L_SEL_CLEAN_MASK,	\
			RK3588_CLK_CORE_L_SEL_CLEAN_SHIFT) |			\
		HIWORD_UPDATE(_apllcore, RK3588_CLK_CORE_L_SEL_CLEAN_MASK,	\
			RK3588_CLK_CORE_L_SEL_CLEAN_SHIFT),			\
}

#define RK3588_CORE_L_SEL1(_seldsu, _divdsu)				\
{									\
	.reg = RK3588_DSU_CLKSEL_CON(0),				\
	.val = HIWORD_UPDATE(_seldsu, RK3588_CLK_DSU_DF_SRC_MASK,	\
			RK3588_CLK_DSU_DF_SRC_SHIFT) |			\
		HIWORD_UPDATE(_divdsu - 1, RK3588_CLK_DSU_DF_DIV_MASK,	\
			RK3588_CLK_DSU_DF_DIV_SHIFT) |			\
		HIWORD_UPDATE(0, RK3588_CLK_DSU_SEL_DF_MASK,		\
			RK3588_CLK_DSU_SEL_DF_SHIFT),			\
}

#define RK3588_CORE_L_SEL2(_aclkm, _aclkmp, _aclks)			\
{									\
	.reg = RK3588_DSU_CLKSEL_CON(1),				\
	.val = HIWORD_UPDATE(_aclkm - 1, RK3588_ACLKM_DSU_DIV_MASK,	\
			RK3588_ACLKM_DSU_DIV_SHIFT) |			\
		HIWORD_UPDATE(_aclkmp - 1, RK3588_ACLKMP_DSU_DIV_MASK,	\
			RK3588_ACLKMP_DSU_DIV_SHIFT) |			\
		HIWORD_UPDATE(_aclks - 1, RK3588_ACLKS_DSU_DIV_MASK,	\
			RK3588_ACLKS_DSU_DIV_SHIFT),			\
}

#define RK3588_CORE_L_SEL3(_periph)					\
{									\
	.reg = RK3588_DSU_CLKSEL_CON(2),				\
	.val = HIWORD_UPDATE(_periph - 1, RK3588_PERIPH_DSU_DIV_MASK,	\
			RK3588_PERIPH_DSU_DIV_SHIFT),			\
}

#define RK3588_CORE_L_SEL4(_gicclk, _atclk)				\
{									\
	.reg = RK3588_DSU_CLKSEL_CON(3),				\
	.val = HIWORD_UPDATE(_gicclk - 1, RK3588_GICCLK_DSU_DIV_MASK,	\
			RK3588_GICCLK_DSU_DIV_SHIFT) |			\
		HIWORD_UPDATE(_atclk - 1, RK3588_ATCLK_DSU_DIV_MASK,	\
			RK3588_ATCLK_DSU_DIV_SHIFT),			\
}

#define RK3588_CPUB01CLK_RATE(_prate, _apllcore)		\
{								\
	.prate = _prate##U,					\
	.divs = {						\
		RK3588_CORE_B0_SEL(_apllcore),			\
		RK3588_CORE_B1_SEL(_apllcore),			\
	},							\
}

#define RK3588_CPUB23CLK_RATE(_prate, _apllcore)		\
{								\
	.prate = _prate##U,					\
	.divs = {						\
		RK3588_CORE_B2_SEL(_apllcore),			\
		RK3588_CORE_B3_SEL(_apllcore),			\
	},							\
}

#define RK3588_CPULCLK_RATE(_prate, _apllcore, _seldsu, _divdsu, _atclk, _gicclk, _aclkmp, _aclkm, _aclks, _periph) \
{								\
	.prate = _prate##U,					\
	.divs = {						\
		RK3588_CORE_L_SEL1(_seldsu, _divdsu),		\
		RK3588_CORE_L_SEL2(_aclkm, _aclkmp, _aclks),	\
		RK3588_CORE_L_SEL3(_periph),			\
		RK3588_CORE_L_SEL4(_gicclk, _atclk),		\
	},							\
	.pre_muxs = {						\
		RK3588_CORE_L_SEL0(0, 0),			\
		RK3588_CORE_L_SEL0(1, 0),			\
		RK3588_CORE_L_SEL1(3, 2),			\
	},							\
	.post_muxs = {						\
		RK3588_CORE_L_SEL0(0, _apllcore),		\
		RK3588_CORE_L_SEL0(1, _apllcore),		\
		RK3588_CORE_L_SEL1(_seldsu, _divdsu),		\
	},							\
}

static struct rockchip_cpuclk_rate_table rk3588_cpub0clk_rates[] __initdata = {
	RK3588_CPUB01CLK_RATE(2496000000, 1),
	RK3588_CPUB01CLK_RATE(2400000000, 1),
	RK3588_CPUB01CLK_RATE(2304000000, 1),
	RK3588_CPUB01CLK_RATE(2208000000, 1),
	RK3588_CPUB01CLK_RATE(2184000000, 1),
	RK3588_CPUB01CLK_RATE(2088000000, 1),
	RK3588_CPUB01CLK_RATE(2040000000, 1),
	RK3588_CPUB01CLK_RATE(2016000000, 1),
	RK3588_CPUB01CLK_RATE(1992000000, 1),
	RK3588_CPUB01CLK_RATE(1896000000, 1),
	RK3588_CPUB01CLK_RATE(1800000000, 1),
	RK3588_CPUB01CLK_RATE(1704000000, 0),
	RK3588_CPUB01CLK_RATE(1608000000, 0),
	RK3588_CPUB01CLK_RATE(1584000000, 0),
	RK3588_CPUB01CLK_RATE(1560000000, 0),
	RK3588_CPUB01CLK_RATE(1536000000, 0),
	RK3588_CPUB01CLK_RATE(1512000000, 0),
	RK3588_CPUB01CLK_RATE(1488000000, 0),
	RK3588_CPUB01CLK_RATE(1464000000, 0),
	RK3588_CPUB01CLK_RATE(1440000000, 0),
	RK3588_CPUB01CLK_RATE(1416000000, 0),
	RK3588_CPUB01CLK_RATE(1392000000, 0),
	RK3588_CPUB01CLK_RATE(1368000000, 0),
	RK3588_CPUB01CLK_RATE(1344000000, 0),
	RK3588_CPUB01CLK_RATE(1320000000, 0),
	RK3588_CPUB01CLK_RATE(1296000000, 0),
	RK3588_CPUB01CLK_RATE(1272000000, 0),
	RK3588_CPUB01CLK_RATE(1248000000, 0),
	RK3588_CPUB01CLK_RATE(1224000000, 0),
	RK3588_CPUB01CLK_RATE(1200000000, 0),
	RK3588_CPUB01CLK_RATE(1104000000, 0),
	RK3588_CPUB01CLK_RATE(1008000000, 0),
	RK3588_CPUB01CLK_RATE(912000000, 0),
	RK3588_CPUB01CLK_RATE(816000000, 0),
	RK3588_CPUB01CLK_RATE(696000000, 0),
	RK3588_CPUB01CLK_RATE(600000000, 0),
	RK3588_CPUB01CLK_RATE(408000000, 0),
	RK3588_CPUB01CLK_RATE(312000000, 0),
	RK3588_CPUB01CLK_RATE(216000000, 0),
	RK3588_CPUB01CLK_RATE(96000000, 0),
};

static const struct rockchip_cpuclk_reg_data rk3588_cpub0clk_data = {
	.core_reg[0] = RK3588_BIGCORE0_CLKSEL_CON(0),
	.div_core_shift[0] = 8,
	.div_core_mask[0] = 0x1f,
	.core_reg[1] = RK3588_BIGCORE0_CLKSEL_CON(1),
	.div_core_shift[1] = 0,
	.div_core_mask[1] = 0x1f,
	.num_cores = 2,
	.mux_core_alt = 1,
	.mux_core_main = 2,
	.mux_core_shift = 6,
	.mux_core_mask = 0x3,
};

static struct rockchip_cpuclk_rate_table rk3588_cpub1clk_rates[] __initdata = {
	RK3588_CPUB23CLK_RATE(2496000000, 1),
	RK3588_CPUB23CLK_RATE(2400000000, 1),
	RK3588_CPUB23CLK_RATE(2304000000, 1),
	RK3588_CPUB23CLK_RATE(2208000000, 1),
	RK3588_CPUB23CLK_RATE(2184000000, 1),
	RK3588_CPUB23CLK_RATE(2088000000, 1),
	RK3588_CPUB23CLK_RATE(2040000000, 1),
	RK3588_CPUB23CLK_RATE(2016000000, 1),
	RK3588_CPUB23CLK_RATE(1992000000, 1),
	RK3588_CPUB23CLK_RATE(1896000000, 1),
	RK3588_CPUB23CLK_RATE(1800000000, 1),
	RK3588_CPUB23CLK_RATE(1704000000, 0),
	RK3588_CPUB23CLK_RATE(1608000000, 0),
	RK3588_CPUB23CLK_RATE(1584000000, 0),
	RK3588_CPUB23CLK_RATE(1560000000, 0),
	RK3588_CPUB23CLK_RATE(1536000000, 0),
	RK3588_CPUB23CLK_RATE(1512000000, 0),
	RK3588_CPUB23CLK_RATE(1488000000, 0),
	RK3588_CPUB23CLK_RATE(1464000000, 0),
	RK3588_CPUB23CLK_RATE(1440000000, 0),
	RK3588_CPUB23CLK_RATE(1416000000, 0),
	RK3588_CPUB23CLK_RATE(1392000000, 0),
	RK3588_CPUB23CLK_RATE(1368000000, 0),
	RK3588_CPUB23CLK_RATE(1344000000, 0),
	RK3588_CPUB23CLK_RATE(1320000000, 0),
	RK3588_CPUB23CLK_RATE(1296000000, 0),
	RK3588_CPUB23CLK_RATE(1272000000, 0),
	RK3588_CPUB23CLK_RATE(1248000000, 0),
	RK3588_CPUB23CLK_RATE(1224000000, 0),
	RK3588_CPUB23CLK_RATE(1200000000, 0),
	RK3588_CPUB23CLK_RATE(1104000000, 0),
	RK3588_CPUB23CLK_RATE(1008000000, 0),
	RK3588_CPUB23CLK_RATE(912000000, 0),
	RK3588_CPUB23CLK_RATE(816000000, 0),
	RK3588_CPUB23CLK_RATE(696000000, 0),
	RK3588_CPUB23CLK_RATE(600000000, 0),
	RK3588_CPUB23CLK_RATE(408000000, 0),
	RK3588_CPUB23CLK_RATE(312000000, 0),
	RK3588_CPUB23CLK_RATE(216000000, 0),
	RK3588_CPUB23CLK_RATE(96000000, 0),
};

static const struct rockchip_cpuclk_reg_data rk3588_cpub1clk_data = {
	.core_reg[0] = RK3588_BIGCORE1_CLKSEL_CON(0),
	.div_core_shift[0] = 8,
	.div_core_mask[0] = 0x1f,
	.core_reg[1] = RK3588_BIGCORE1_CLKSEL_CON(1),
	.div_core_shift[1] = 0,
	.div_core_mask[1] = 0x1f,
	.num_cores = 2,
	.mux_core_alt = 1,
	.mux_core_main = 2,
	.mux_core_shift = 6,
	.mux_core_mask = 0x3,
};

static struct rockchip_cpuclk_rate_table rk3588_cpulclk_rates[] __initdata = {
	RK3588_CPULCLK_RATE(2208000000, 1, 3, 1, 4, 4, 4, 4, 4, 4),
	RK3588_CPULCLK_RATE(2184000000, 1, 3, 1, 4, 4, 4, 4, 4, 4),
	RK3588_CPULCLK_RATE(2088000000, 1, 3, 1, 4, 4, 4, 4, 4, 4),
	RK3588_CPULCLK_RATE(2040000000, 1, 3, 1, 4, 4, 4, 4, 4, 4),
	RK3588_CPULCLK_RATE(2016000000, 1, 3, 1, 4, 4, 4, 4, 4, 4),
	RK3588_CPULCLK_RATE(1992000000, 1, 3, 1, 4, 4, 4, 4, 4, 4),
	RK3588_CPULCLK_RATE(1896000000, 1, 3, 1, 4, 4, 4, 4, 4, 4),
	RK3588_CPULCLK_RATE(1800000000, 1, 3, 1, 4, 4, 4, 4, 4, 4),
	RK3588_CPULCLK_RATE(1704000000, 0, 3, 1, 3, 3, 3, 3, 3, 3),
	RK3588_CPULCLK_RATE(1608000000, 0, 3, 1, 3, 3, 3, 2, 3, 3),
	RK3588_CPULCLK_RATE(1584000000, 0, 3, 1, 3, 3, 3, 2, 3, 3),
	RK3588_CPULCLK_RATE(1560000000, 0, 2, 2, 3, 3, 3, 2, 3, 3),
	RK3588_CPULCLK_RATE(1536000000, 0, 2, 2, 3, 3, 3, 2, 3, 3),
	RK3588_CPULCLK_RATE(1512000000, 0, 2, 2, 3, 3, 3, 2, 3, 3),
	RK3588_CPULCLK_RATE(1488000000, 0, 2, 2, 3, 3, 3, 2, 3, 3),
	RK3588_CPULCLK_RATE(1464000000, 0, 2, 2, 3, 3, 3, 2, 3, 3),
	RK3588_CPULCLK_RATE(1440000000, 0, 2, 2, 3, 3, 3, 2, 3, 3),
	RK3588_CPULCLK_RATE(1416000000, 0, 2, 2, 3, 3, 3, 2, 3, 3),
	RK3588_CPULCLK_RATE(1392000000, 0, 2, 2, 3, 3, 3, 2, 3, 3),
	RK3588_CPULCLK_RATE(1368000000, 0, 2, 2, 3, 3, 3, 2, 3, 3),
	RK3588_CPULCLK_RATE(1344000000, 0, 2, 2, 3, 3, 3, 2, 3, 3),
	RK3588_CPULCLK_RATE(1320000000, 0, 2, 2, 3, 3, 3, 2, 3, 3),
	RK3588_CPULCLK_RATE(1296000000, 0, 2, 2, 3, 3, 3, 2, 3, 3),
	RK3588_CPULCLK_RATE(1272000000, 0, 2, 2, 2, 2, 2, 2, 2, 2),
	RK3588_CPULCLK_RATE(1248000000, 0, 2, 2, 2, 2, 2, 2, 2, 2),
	RK3588_CPULCLK_RATE(1224000000, 0, 2, 2, 2, 2, 2, 2, 2, 2),
	RK3588_CPULCLK_RATE(1200000000, 0, 2, 2, 2, 2, 2, 2, 2, 2),
	RK3588_CPULCLK_RATE(1104000000, 0, 2, 2, 2, 2, 2, 2, 2, 2),
	RK3588_CPULCLK_RATE(1008000000, 0, 2, 2, 2, 2, 2, 2, 2, 2),
	RK3588_CPULCLK_RATE(912000000, 0, 2, 2, 2, 2, 2, 1, 2, 2),
	RK3588_CPULCLK_RATE(816000000, 0, 2, 2, 2, 2, 2, 1, 2, 2),
	RK3588_CPULCLK_RATE(696000000, 0, 2, 2, 2, 2, 2, 1, 2, 2),
	RK3588_CPULCLK_RATE(600000000, 0, 2, 1, 1, 1, 1, 1, 1, 1),
	RK3588_CPULCLK_RATE(408000000, 0, 2, 1, 1, 1, 1, 1, 1, 1),
	RK3588_CPULCLK_RATE(312000000, 0, 2, 1, 1, 1, 1, 1, 1, 1),
	RK3588_CPULCLK_RATE(216000000, 0, 2, 1, 1, 1, 1, 1, 1, 1),
	RK3588_CPULCLK_RATE(96000000, 0, 2, 1, 1, 1, 1, 1, 1, 1),
};

static const struct rockchip_cpuclk_reg_data rk3588_cpulclk_data = {
	.core_reg[0] = RK3588_DSU_CLKSEL_CON(6),
	.div_core_shift[0] = 0,
	.div_core_mask[0] = 0x1f,
	.core_reg[1] = RK3588_DSU_CLKSEL_CON(6),
	.div_core_shift[1] = 7,
	.div_core_mask[1] = 0x1f,
	.core_reg[2] = RK3588_DSU_CLKSEL_CON(7),
	.div_core_shift[2] = 0,
	.div_core_mask[2] = 0x1f,
	.core_reg[3] = RK3588_DSU_CLKSEL_CON(7),
	.div_core_shift[3] = 7,
	.div_core_mask[3] = 0x1f,
	.num_cores = 4,
	.mux_core_reg = RK3588_DSU_CLKSEL_CON(5),
	.mux_core_alt = 1,
	.mux_core_main = 2,
	.mux_core_shift = 14,
	.mux_core_mask = 0x3,
};

PNAME(mux_pll_p)			= { "xin24m", "xin32k" };
PNAME(mux_armclkl_p)			= { "xin24m", "gpll", "lpll" };
PNAME(mux_armclkb01_p)			= { "xin24m", "gpll", "b0pll",};
PNAME(mux_armclkb23_p)			= { "xin24m", "gpll", "b1pll",};
PNAME(b0pll_b1pll_lpll_gpll_p)		= { "b0pll", "b1pll", "lpll", "gpll" };
PNAME(gpll_24m_p)			= { "gpll", "xin24m" };
PNAME(gpll_aupll_p)			= { "gpll", "aupll" };
PNAME(gpll_lpll_p)			= { "gpll", "lpll" };
PNAME(gpll_cpll_p)			= { "gpll", "cpll" };
PNAME(gpll_spll_p)			= { "gpll", "spll" };
PNAME(gpll_cpll_24m_p)			= { "gpll", "cpll", "xin24m"};
PNAME(gpll_cpll_aupll_p)		= { "gpll", "cpll", "aupll"};
PNAME(gpll_cpll_npll_p)			= { "gpll", "cpll", "npll"};
PNAME(gpll_cpll_npll_v0pll_p)		= { "gpll", "cpll", "npll", "v0pll"};
PNAME(gpll_cpll_24m_spll_p)		= { "gpll", "cpll", "xin24m", "spll" };
PNAME(gpll_cpll_aupll_spll_p)		= { "gpll", "cpll", "aupll", "spll" };
PNAME(gpll_cpll_aupll_npll_p)		= { "gpll", "cpll", "aupll", "npll" };
PNAME(gpll_cpll_v0pll_aupll_p)		= { "gpll", "cpll", "v0pll", "aupll" };
PNAME(gpll_cpll_v0pll_spll_p)		= { "gpll", "cpll", "v0pll", "spll" };
PNAME(gpll_cpll_aupll_npll_spll_p)	= { "gpll", "cpll", "aupll", "npll", "spll" };
PNAME(gpll_cpll_npll_aupll_spll_p)	= { "gpll", "cpll", "npll", "aupll", "spll" };
PNAME(gpll_cpll_npll_1000m_p)		= { "gpll", "cpll", "npll", "clk_1000m_src" };
PNAME(mux_24m_spll_gpll_cpll_p)		= { "xin24m", "spll", "gpll", "cpll" };
PNAME(mux_24m_32k_p)			= { "xin24m", "xin32k" };
PNAME(mux_24m_100m_p)			= { "xin24m", "clk_100m_src" };
PNAME(mux_200m_100m_p)			= { "clk_200m_src", "clk_100m_src" };
PNAME(mux_100m_50m_24m_p)		= { "clk_100m_src", "clk_50m_src", "xin24m" };
PNAME(mux_150m_50m_24m_p)		= { "clk_150m_src", "clk_50m_src", "xin24m" };
PNAME(mux_150m_100m_24m_p)		= { "clk_150m_src", "clk_100m_src", "xin24m" };
PNAME(mux_200m_150m_24m_p)		= { "clk_200m_src", "clk_150m_src", "xin24m" };
PNAME(mux_150m_100m_50m_24m_p)		= { "clk_150m_src", "clk_100m_src", "clk_50m_src", "xin24m" };
PNAME(mux_200m_100m_50m_24m_p)		= { "clk_200m_src", "clk_100m_src", "clk_50m_src", "xin24m" };
PNAME(mux_300m_200m_100m_24m_p)		= { "clk_300m_src", "clk_200m_src", "clk_100m_src", "xin24m" };
PNAME(mux_700m_400m_200m_24m_p)		= { "clk_700m_src", "clk_400m_src", "clk_200m_src", "xin_osc0_func" };
PNAME(mux_500m_250m_100m_24m_p)		= { "clk_500m_src", "clk_250m_src", "clk_100m_src", "xin_osc0_func" };
PNAME(mux_500m_300m_100m_24m_p)		= { "clk_500m_src", "clk_300m_src", "clk_100m_src", "xin_osc0_func" };
PNAME(mux_400m_200m_100m_24m_p)		= {"clk_400m_src", "clk_200m_src", "clk_100m_src", "xin_osc0_func" };
PNAME(clk_i2s2_2ch_p)			= { "clk_i2s2_2ch_src", "clk_i2s2_2ch_frac", "i2s2_mclkin", "xin12m" };
PNAME(i2s2_2ch_mclkout_p)		= { "mclk_i2s2_2ch", "xin12m" };
PNAME(clk_i2s3_2ch_p)			= { "clk_i2s3_2ch_src", "clk_i2s3_2ch_frac", "i2s3_mclkin", "xin12m" };
PNAME(i2s3_2ch_mclkout_p)		= { "mclk_i2s3_2ch", "xin12m" };
PNAME(clk_i2s0_8ch_tx_p)		= { "clk_i2s0_8ch_tx_src", "clk_i2s0_8ch_tx_frac", "i2s0_mclkin", "xin12m" };
PNAME(clk_i2s0_8ch_rx_p)		= { "clk_i2s0_8ch_rx_src", "clk_i2s0_8ch_rx_frac", "i2s0_mclkin", "xin12m" };
PNAME(i2s0_8ch_mclkout_p)		= { "mclk_i2s0_8ch_tx", "mclk_i2s0_8ch_rx", "xin12m" };
PNAME(clk_i2s1_8ch_tx_p)		= { "clk_i2s1_8ch_tx_src", "clk_i2s1_8ch_tx_frac", "i2s1_mclkin", "xin12m" };
PNAME(clk_i2s1_8ch_rx_p)		= { "clk_i2s1_8ch_rx_src", "clk_i2s1_8ch_rx_frac", "i2s1_mclkin", "xin12m" };
PNAME(i2s1_8ch_mclkout_p)		= { "mclk_i2s1_8ch_tx", "mclk_i2s1_8ch_rx", "xin12m" };
PNAME(clk_i2s4_8ch_tx_p)		= { "clk_i2s4_8ch_tx_src", "clk_i2s4_8ch_tx_frac", "i2s4_mclkin", "xin12m" };
PNAME(clk_i2s5_8ch_tx_p)		= { "clk_i2s5_8ch_tx_src", "clk_i2s5_8ch_tx_frac", "i2s5_mclkin", "xin12m" };
PNAME(clk_i2s6_8ch_tx_p)		= { "clk_i2s6_8ch_tx_src", "clk_i2s6_8ch_tx_frac", "i2s6_mclkin", "xin12m" };
PNAME(clk_i2s6_8ch_rx_p)		= { "clk_i2s6_8ch_rx_src", "clk_i2s6_8ch_rx_frac", "i2s6_mclkin", "xin12m" };
PNAME(i2s6_8ch_mclkout_p)		= { "mclk_i2s6_8ch_tx", "mclk_i2s6_8ch_rx", "xin12m" };
PNAME(clk_i2s7_8ch_rx_p)		= { "clk_i2s7_8ch_rx_src", "clk_i2s7_8ch_rx_frac", "i2s7_mclkin", "xin12m" };
PNAME(clk_i2s8_8ch_tx_p)		= { "clk_i2s8_8ch_tx_src", "clk_i2s8_8ch_tx_frac", "i2s8_mclkin", "xin12m" };
PNAME(clk_i2s9_8ch_rx_p)		= { "clk_i2s9_8ch_rx_src", "clk_i2s9_8ch_rx_frac", "i2s9_mclkin", "xin12m" };
PNAME(clk_i2s10_8ch_rx_p)		= { "clk_i2s10_8ch_rx_src", "clk_i2s10_8ch_rx_frac", "i2s10_mclkin", "xin12m" };
PNAME(clk_spdif0_p)			= { "clk_spdif0_src", "clk_spdif0_frac", "xin12m" };
PNAME(clk_spdif1_p)			= { "clk_spdif1_src", "clk_spdif1_frac", "xin12m" };
PNAME(clk_spdif2_dp0_p)			= { "clk_spdif2_dp0_src", "clk_spdif2_dp0_frac", "xin12m" };
PNAME(clk_spdif3_p)			= { "clk_spdif3_src", "clk_spdif3_frac", "xin12m" };
PNAME(clk_spdif4_p)			= { "clk_spdif4_src", "clk_spdif4_frac", "xin12m" };
PNAME(clk_spdif5_dp1_p)			= { "clk_spdif5_dp1_src", "clk_spdif5_dp1_frac", "xin12m" };
PNAME(clk_uart0_p)			= { "clk_uart0_src", "clk_uart0_frac", "xin24m" };
PNAME(clk_uart1_p)			= { "clk_uart1_src", "clk_uart1_frac", "xin24m" };
PNAME(clk_uart2_p)			= { "clk_uart2_src", "clk_uart2_frac", "xin24m" };
PNAME(clk_uart3_p)			= { "clk_uart3_src", "clk_uart3_frac", "xin24m" };
PNAME(clk_uart4_p)			= { "clk_uart4_src", "clk_uart4_frac", "xin24m" };
PNAME(clk_uart5_p)			= { "clk_uart5_src", "clk_uart5_frac", "xin24m" };
PNAME(clk_uart6_p)			= { "clk_uart6_src", "clk_uart6_frac", "xin24m" };
PNAME(clk_uart7_p)			= { "clk_uart7_src", "clk_uart7_frac", "xin24m" };
PNAME(clk_uart8_p)			= { "clk_uart8_src", "clk_uart8_frac", "xin24m" };
PNAME(clk_uart9_p)			= { "clk_uart9_src", "clk_uart9_frac", "xin24m" };
PNAME(clk_gmac0_ptp_ref_p)		= { "cpll", "clk_gmac0_ptpref_io" };
PNAME(clk_gmac1_ptp_ref_p)		= { "cpll", "clk_gmac1_ptpref_io" };
PNAME(clk_hdmirx_aud_p)			= { "clk_hdmirx_aud_src", "clk_hdmirx_aud_frac" };
PNAME(aclk_hdcp1_root_p)		= { "gpll", "cpll", "clk_hdmitrx_refsrc" };
PNAME(aclk_vop_sub_src_p)		= { "aclk_vop_root", "aclk_vop_div2_src" };
PNAME(dclk_vop0_p)			= { "dclk_vop0_src", "clk_hdmiphy_pixel0", "clk_hdmiphy_pixel1" };
PNAME(dclk_vop1_p)			= { "dclk_vop1_src", "clk_hdmiphy_pixel0", "clk_hdmiphy_pixel1" };
PNAME(dclk_vop2_p)			= { "dclk_vop2_src", "clk_hdmiphy_pixel0", "clk_hdmiphy_pixel1" };
PNAME(pmu_200m_100m_p)			= { "clk_pmu1_200m_src", "clk_pmu1_100m_src" };
PNAME(pmu_300m_24m_p)			= { "clk_300m_src", "xin24m" };
PNAME(pmu_400m_24m_p)			= { "clk_400m_src", "xin24m" };
PNAME(pmu_100m_50m_24m_src_p)		= { "clk_pmu1_100m_src", "clk_pmu1_50m_src", "xin24m" };
PNAME(pmu_24m_32k_100m_src_p)		= { "xin24m", "32k", "clk_pmu1_100m_src" };
PNAME(hclk_pmu1_root_p)			= { "clk_pmu1_200m_src", "clk_pmu1_100m_src", "clk_pmu1_50m_src", "xin24m" };
PNAME(hclk_pmu_cm0_root_p)		= { "clk_pmu1_400m_src", "clk_pmu1_200m_src", "clk_pmu1_100m_src", "xin24m" };
PNAME(mclk_pdm0_p)			= { "clk_pmu1_300m_src", "clk_pmu1_200m_src" };
PNAME(mux_24m_ppll_spll_p)		= { "xin24m", "ppll", "spll" };
PNAME(mux_24m_ppll_p)			= { "xin24m", "ppll" };
PNAME(clk_ref_pipe_phy0_p)		= { "clk_ref_pipe_phy0_osc_src", "clk_ref_pipe_phy0_pll_src" };
PNAME(clk_ref_pipe_phy1_p)		= { "clk_ref_pipe_phy1_osc_src", "clk_ref_pipe_phy1_pll_src" };
PNAME(clk_ref_pipe_phy2_p)		= { "clk_ref_pipe_phy2_osc_src", "clk_ref_pipe_phy2_pll_src" };
PNAME(mux_clk_gmac_p)			= { "clk_gmac_125m", "clk_gmac_50m" };

#define MFLAGS CLK_MUX_HIWORD_MASK
#define DFLAGS CLK_DIVIDER_HIWORD_MASK
#define GFLAGS (CLK_GATE_HIWORD_MASK | CLK_GATE_SET_TO_DISABLE)

static struct rockchip_clk_branch rk3588_i2s0_8ch_tx_fracmux __initdata =
	MUX(CLK_I2S0_8CH_TX, "clk_i2s0_8ch_tx", clk_i2s0_8ch_tx_p, CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(26), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3588_i2s0_8ch_rx_fracmux __initdata =
	MUX(CLK_I2S0_8CH_RX, "clk_i2s0_8ch_rx", clk_i2s0_8ch_rx_p, CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(28), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3588_i2s1_8ch_tx_fracmux __initdata =
	MUX(CLK_I2S1_8CH_TX, "clk_i2s1_8ch_tx", clk_i2s1_8ch_tx_p, CLK_SET_RATE_PARENT,
			 RK3588_PMU_CLKSEL_CON(7), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3588_i2s1_8ch_rx_fracmux __initdata =
	MUX(CLK_I2S1_8CH_RX, "clk_i2s1_8ch_rx", clk_i2s1_8ch_rx_p, CLK_SET_RATE_PARENT,
			 RK3588_PMU_CLKSEL_CON(9), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3588_i2s2_2ch_fracmux __initdata =
	MUX(CLK_I2S2_2CH, "clk_i2s2_2ch", clk_i2s2_2ch_p, CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(30), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3588_i2s3_2ch_fracmux __initdata =
	MUX(CLK_I2S3_2CH, "clk_i2s3_2ch", clk_i2s3_2ch_p, CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(32), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3588_i2s4_8ch_tx_fracmux __initdata =
	MUX(CLK_I2S4_8CH_TX, "clk_i2s4_8ch_tx", clk_i2s4_8ch_tx_p, CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(120), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3588_i2s5_8ch_tx_fracmux __initdata =
	MUX(CLK_I2S5_8CH_TX, "clk_i2s5_8ch_tx", clk_i2s5_8ch_tx_p, CLK_SET_RATE_PARENT,
			 RK3588_CLKSEL_CON(142), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3588_i2s6_8ch_tx_fracmux __initdata =
	MUX(CLK_I2S6_8CH_TX, "clk_i2s6_8ch_tx", clk_i2s6_8ch_tx_p, CLK_SET_RATE_PARENT,
			 RK3588_CLKSEL_CON(146), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3588_i2s6_8ch_rx_fracmux __initdata =
	MUX(CLK_I2S6_8CH_RX, "clk_i2s6_8ch_rx", clk_i2s6_8ch_rx_p, CLK_SET_RATE_PARENT,
			 RK3588_CLKSEL_CON(148), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3588_i2s7_8ch_rx_fracmux __initdata =
	MUX(CLK_I2S7_8CH_RX, "clk_i2s7_8ch_rx", clk_i2s7_8ch_rx_p, CLK_SET_RATE_PARENT,
			 RK3588_CLKSEL_CON(131), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3588_i2s8_8ch_tx_fracmux __initdata =
	MUX(CLK_I2S8_8CH_TX, "clk_i2s8_8ch_tx", clk_i2s8_8ch_tx_p, CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(122), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3588_i2s9_8ch_rx_fracmux __initdata =
	MUX(CLK_I2S9_8CH_RX, "clk_i2s9_8ch_rx", clk_i2s9_8ch_rx_p, CLK_SET_RATE_PARENT,
			 RK3588_CLKSEL_CON(155), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3588_i2s10_8ch_rx_fracmux __initdata =
	MUX(CLK_I2S10_8CH_RX, "clk_i2s10_8ch_rx", clk_i2s10_8ch_rx_p, CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(157), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3588_spdif0_fracmux __initdata =
	MUX(CLK_SPDIF0, "clk_spdif0", clk_spdif0_p, CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(34), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3588_spdif1_fracmux __initdata =
	MUX(CLK_SPDIF1, "clk_spdif1", clk_spdif1_p, CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(36), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3588_spdif2_dp0_fracmux __initdata =
	MUX(CLK_SPDIF2_DP0, "clk_spdif2_dp0", clk_spdif2_dp0_p, CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(124), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3588_spdif3_fracmux __initdata =
	MUX(CLK_SPDIF3, "clk_spdif3", clk_spdif3_p, CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(150), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3588_spdif4_fracmux __initdata =
	MUX(CLK_SPDIF4, "clk_spdif4", clk_spdif4_p, CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(152), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3588_spdif5_dp1_fracmux __initdata =
	MUX(CLK_SPDIF5_DP1, "clk_spdif5_dp1", clk_spdif5_dp1_p, CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(126), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3588_uart0_fracmux __initdata =
	MUX(CLK_UART0, "clk_uart0", clk_uart0_p, CLK_SET_RATE_PARENT,
			RK3588_PMU_CLKSEL_CON(5), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3588_uart1_fracmux __initdata =
	MUX(CLK_UART1, "clk_uart1", clk_uart1_p, CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(43), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3588_uart2_fracmux __initdata =
	MUX(CLK_UART2, "clk_uart2", clk_uart2_p, CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(45), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3588_uart3_fracmux __initdata =
	MUX(CLK_UART3, "clk_uart3", clk_uart3_p, CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(47), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3588_uart4_fracmux __initdata =
	MUX(CLK_UART4, "clk_uart4", clk_uart4_p, CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(49), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3588_uart5_fracmux __initdata =
	MUX(CLK_UART5, "clk_uart5", clk_uart5_p, CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(51), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3588_uart6_fracmux __initdata =
	MUX(CLK_UART6, "clk_uart6", clk_uart6_p, CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(53), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3588_uart7_fracmux __initdata =
	MUX(CLK_UART7, "clk_uart7", clk_uart7_p, CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(55), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3588_uart8_fracmux __initdata =
	MUX(CLK_UART8, "clk_uart8", clk_uart8_p, CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(57), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3588_uart9_fracmux __initdata =
	MUX(CLK_UART9, "clk_uart9", clk_uart9_p, CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(59), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3588_hdmirx_aud_fracmux __initdata =
	MUX(CLK_HDMIRX_AUD_P_MUX, "clk_hdmirx_aud_mux", clk_hdmirx_aud_p, CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(140), 0, 1, MFLAGS);

static struct rockchip_pll_clock rk3588_pll_clks[] __initdata = {
	[b0pll] = PLL(pll_rk3588_core, PLL_B0PLL, "b0pll", mux_pll_p,
		     0, RK3588_B0_PLL_CON(0),
		     RK3588_BIGCORE0_CLKSEL_CON(0), 6, 15, 0, rk3588_pll_rates),
	[b1pll] = PLL(pll_rk3588_core, PLL_B1PLL, "b1pll", mux_pll_p,
		     0, RK3588_B1_PLL_CON(8),
		     RK3588_BIGCORE1_CLKSEL_CON(0), 6, 15, 0, rk3588_pll_rates),
	[lpll] = PLL(pll_rk3588_core, PLL_LPLL, "lpll", mux_pll_p,
		     0, RK3588_LPLL_CON(16),
		     RK3588_DSU_CLKSEL_CON(5), 14, 15, 0, rk3588_pll_rates),
	[v0pll] = PLL(pll_rk3588, PLL_V0PLL, "v0pll", mux_pll_p,
		     0, RK3588_PLL_CON(88),
		     RK3588_MODE_CON0, 4, 15, 0, rk3588_pll_rates),
	[aupll] = PLL(pll_rk3588, PLL_AUPLL, "aupll", mux_pll_p,
		     0, RK3588_PLL_CON(96),
		     RK3588_MODE_CON0, 6, 15, 0, rk3588_pll_rates),
	[cpll] = PLL(pll_rk3588, PLL_CPLL, "cpll", mux_pll_p,
		     0, RK3588_PLL_CON(104),
		     RK3588_MODE_CON0, 8, 15, 0, rk3588_pll_rates),
	[gpll] = PLL(pll_rk3588, PLL_GPLL, "gpll", mux_pll_p,
		     0, RK3588_PLL_CON(112),
		     RK3588_MODE_CON0, 2, 15, 0, rk3588_pll_rates),
	[npll] = PLL(pll_rk3588, PLL_NPLL, "npll", mux_pll_p,
		     0, RK3588_PLL_CON(120),
		     RK3588_MODE_CON0, 0, 15, 0, rk3588_pll_rates),
	[ppll] = PLL(pll_rk3588_core, PLL_PPLL, "ppll", mux_pll_p,
		     0, RK3588_PMU_PLL_CON(128),
		     RK3588_MODE_CON0, 10, 15, 0, rk3588_pll_rates),
};

static struct rockchip_clk_branch rk3588_clk_branches[] __initdata = {
	/*
	 * CRU Clock-Architecture
	 */
	/* fixed */
	FACTOR(0, "xin12m", "xin24m", 0, 1, 2),

	/* bigcore0 */
	COMPOSITE_NODIV(PCLK_BIGCORE0_ROOT, "pclk_bigcore0_root", mux_100m_50m_24m_p, CLK_IS_CRITICAL,
			RK3588_BIGCORE0_CLKSEL_CON(2), 0, 2, MFLAGS,
			RK3588_BIGCORE0_CLKGATE_CON(0), 14, GFLAGS),
	GATE(PCLK_BIGCORE0_PVTM, "pclk_bigcore0_pvtm", "pclk_bigcore0_root", 0,
			RK3588_BIGCORE0_CLKGATE_CON(1), 0, GFLAGS),
	GATE(CLK_BIGCORE0_PVTM, "clk_bigcore0_pvtm", "xin24m", 0,
			RK3588_BIGCORE0_CLKGATE_CON(0), 12, GFLAGS),
	GATE(CLK_CORE_BIGCORE0_PVTM, "clk_core_bigcore0_pvtm", "armclk_b01", 0,
			RK3588_BIGCORE0_CLKGATE_CON(0), 13, GFLAGS),

	/* bigcore1 */
	COMPOSITE_NODIV(PCLK_BIGCORE1_ROOT, "pclk_bigcore1_root", mux_100m_50m_24m_p, CLK_IS_CRITICAL,
			RK3588_BIGCORE1_CLKSEL_CON(2), 0, 2, MFLAGS,
			RK3588_BIGCORE1_CLKGATE_CON(0), 14, GFLAGS),
	GATE(PCLK_BIGCORE1_PVTM, "pclk_bigcore1_pvtm", "pclk_bigcore1_root", 0,
			RK3588_BIGCORE1_CLKGATE_CON(1), 0, GFLAGS),
	GATE(CLK_BIGCORE1_PVTM, "clk_bigcore1_pvtm", "xin24m", 0,
			RK3588_BIGCORE1_CLKGATE_CON(0), 12, GFLAGS),
	GATE(CLK_CORE_BIGCORE1_PVTM, "clk_core_bigcore1_pvtm", "armclk_b23", 0,
			RK3588_BIGCORE1_CLKGATE_CON(0), 13, GFLAGS),

	/* dsu */
	COMPOSITE(0, "sclk_dsu", b0pll_b1pll_lpll_gpll_p, CLK_IS_CRITICAL,
			RK3588_DSU_CLKSEL_CON(0), 5, 2, MFLAGS, 0, 5, DFLAGS,
			RK3588_DSU_CLKGATE_CON(0), 4, GFLAGS),
	COMPOSITE_NOMUX(0, "atclk_dsu", "sclk_dsu", CLK_IS_CRITICAL,
			RK3588_DSU_CLKSEL_CON(3), 0, 5, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RK3588_DSU_CLKGATE_CON(1), 0, GFLAGS),
	COMPOSITE_NOMUX(0, "gicclk_dsu", "sclk_dsu", CLK_IS_CRITICAL,
			RK3588_DSU_CLKSEL_CON(3), 5, 5, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RK3588_DSU_CLKGATE_CON(1), 1, GFLAGS),
	COMPOSITE_NOMUX(0, "aclkmp_dsu", "sclk_dsu", CLK_IS_CRITICAL,
			RK3588_DSU_CLKSEL_CON(1), 11, 5, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RK3588_DSU_CLKGATE_CON(0), 12, GFLAGS),
	COMPOSITE_NOMUX(0, "aclkm_dsu", "sclk_dsu", CLK_IS_CRITICAL,
			RK3588_DSU_CLKSEL_CON(1), 1, 5, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RK3588_DSU_CLKGATE_CON(0), 8, GFLAGS),
	COMPOSITE_NOMUX(0, "aclks_dsu", "sclk_dsu", CLK_IS_CRITICAL,
			RK3588_DSU_CLKSEL_CON(1), 6, 5, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RK3588_DSU_CLKGATE_CON(0), 9, GFLAGS),
	COMPOSITE_NOMUX(0, "periph_dsu", "sclk_dsu", CLK_IS_CRITICAL,
			RK3588_DSU_CLKSEL_CON(2), 0, 5, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RK3588_DSU_CLKGATE_CON(0), 13, GFLAGS),
	COMPOSITE_NOMUX(0, "cntclk_dsu", "periph_dsu", CLK_IS_CRITICAL,
			RK3588_DSU_CLKSEL_CON(2), 5, 5, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RK3588_DSU_CLKGATE_CON(0), 14, GFLAGS),
	COMPOSITE_NOMUX(0, "tsclk_dsu", "periph_dsu", CLK_IS_CRITICAL,
			RK3588_DSU_CLKSEL_CON(2), 10, 5, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RK3588_DSU_CLKGATE_CON(0), 15, GFLAGS),
	COMPOSITE_NODIV(PCLK_DSU_S_ROOT, "pclk_dsu_s_root", mux_100m_50m_24m_p, CLK_IS_CRITICAL,
			RK3588_DSU_CLKSEL_CON(4), 11, 2, MFLAGS,
			RK3588_DSU_CLKGATE_CON(2), 2, GFLAGS),
	COMPOSITE(PCLK_DSU_ROOT, "pclk_dsu_root", b0pll_b1pll_lpll_gpll_p, CLK_IS_CRITICAL,
			RK3588_DSU_CLKSEL_CON(4), 5, 2, MFLAGS, 0, 5, DFLAGS,
			RK3588_DSU_CLKGATE_CON(1), 3, GFLAGS),
	COMPOSITE_NODIV(PCLK_DSU_NS_ROOT, "pclk_dsu_ns_root", mux_100m_50m_24m_p, CLK_IS_CRITICAL,
			RK3588_DSU_CLKSEL_CON(4), 7, 2, MFLAGS,
			RK3588_DSU_CLKGATE_CON(1), 4, GFLAGS),
	GATE(PCLK_LITCORE_PVTM, "pclk_litcore_pvtm", "pclk_dsu_ns_root", 0,
			RK3588_DSU_CLKGATE_CON(2), 6, GFLAGS),
	GATE(PCLK_DBG, "pclk_dbg", "pclk_dsu_root", CLK_IS_CRITICAL,
			RK3588_DSU_CLKGATE_CON(1), 7, GFLAGS),
	GATE(PCLK_DSU, "pclk_dsu", "pclk_dsu_root", CLK_IS_CRITICAL,
			RK3588_DSU_CLKGATE_CON(1), 6, GFLAGS),
	GATE(PCLK_S_DAPLITE, "pclk_s_daplite", "pclk_dsu_ns_root", CLK_IGNORE_UNUSED,
			RK3588_DSU_CLKGATE_CON(1), 8, GFLAGS),
	GATE(PCLK_M_DAPLITE, "pclk_m_daplite", "pclk_dsu_root", CLK_IGNORE_UNUSED,
			RK3588_DSU_CLKGATE_CON(1), 9, GFLAGS),
	GATE(CLK_LITCORE_PVTM, "clk_litcore_pvtm", "xin24m", 0,
			RK3588_DSU_CLKGATE_CON(2), 0, GFLAGS),
	GATE(CLK_CORE_LITCORE_PVTM, "clk_core_litcore_pvtm", "armclk_l", 0,
			RK3588_DSU_CLKGATE_CON(2), 1, GFLAGS),

	/* audio */
	GATE(HCLK_I2S2_2CH, "hclk_i2s2_2ch", "hclk_audio_root", 0,
			RK3588_CLKGATE_CON(7), 12, GFLAGS),
	GATE(HCLK_I2S3_2CH, "hclk_i2s3_2ch", "hclk_audio_root", 0,
			RK3588_CLKGATE_CON(7), 13, GFLAGS),
	COMPOSITE(CLK_I2S2_2CH_SRC, "clk_i2s2_2ch_src", gpll_aupll_p, 0,
			RK3588_CLKSEL_CON(28), 9, 1, MFLAGS, 4, 5, DFLAGS,
			RK3588_CLKGATE_CON(7), 14, GFLAGS),
	COMPOSITE_FRACMUX(CLK_I2S2_2CH_FRAC, "clk_i2s2_2ch_frac", "clk_i2s2_2ch_src", CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(29), 0,
			RK3588_CLKGATE_CON(7), 15, GFLAGS,
			&rk3588_i2s2_2ch_fracmux, RK3588_FRAC_MAX_PRATE),
	GATE(MCLK_I2S2_2CH, "mclk_i2s2_2ch", "clk_i2s2_2ch", 0,
			RK3588_CLKGATE_CON(8), 0, GFLAGS),
	MUX(I2S2_2CH_MCLKOUT, "i2s2_2ch_mclkout", i2s2_2ch_mclkout_p, CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(30), 2, 1, MFLAGS),

	COMPOSITE(CLK_I2S3_2CH_SRC, "clk_i2s3_2ch_src", gpll_aupll_p, 0,
			RK3588_CLKSEL_CON(30), 8, 1, MFLAGS, 3, 5, DFLAGS,
			RK3588_CLKGATE_CON(8), 1, GFLAGS),
	COMPOSITE_FRACMUX(CLK_I2S3_2CH_FRAC, "clk_i2s3_2ch_frac", "clk_i2s3_2ch_src", CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(31), 0,
			RK3588_CLKGATE_CON(8), 2, GFLAGS,
			&rk3588_i2s3_2ch_fracmux, RK3588_FRAC_MAX_PRATE),
	GATE(MCLK_I2S3_2CH, "mclk_i2s3_2ch", "clk_i2s3_2ch", 0,
			RK3588_CLKGATE_CON(8), 3, GFLAGS),
	GATE(CLK_DAC_ACDCDIG, "clk_dac_acdcdig", "mclk_i2s3_2ch", 0,
			RK3588_CLKGATE_CON(8), 4, GFLAGS),
	MUX(I2S3_2CH_MCLKOUT, "i2s3_2ch_mclkout", i2s3_2ch_mclkout_p, CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(32), 2, 1, MFLAGS),
	GATE(PCLK_ACDCDIG, "pclk_acdcdig", "pclk_audio_root", 0,
			RK3588_CLKGATE_CON(7), 11, GFLAGS),
	GATE(HCLK_I2S0_8CH, "hclk_i2s0_8ch", "hclk_audio_root", 0,
			RK3588_CLKGATE_CON(7), 4, GFLAGS),

	COMPOSITE(CLK_I2S0_8CH_TX_SRC, "clk_i2s0_8ch_tx_src", gpll_aupll_p, 0,
			RK3588_CLKSEL_CON(24), 9, 1, MFLAGS, 4, 5, DFLAGS,
			RK3588_CLKGATE_CON(7), 5, GFLAGS),
	COMPOSITE_FRACMUX(CLK_I2S0_8CH_TX_FRAC, "clk_i2s0_8ch_tx_frac", "clk_i2s0_8ch_tx_src", CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(25), 0,
			RK3588_CLKGATE_CON(7), 6, GFLAGS,
			&rk3588_i2s0_8ch_tx_fracmux, RK3588_FRAC_MAX_PRATE),
	GATE(MCLK_I2S0_8CH_TX, "mclk_i2s0_8ch_tx", "clk_i2s0_8ch_tx", 0,
			RK3588_CLKGATE_CON(7), 7, GFLAGS),

	COMPOSITE(CLK_I2S0_8CH_RX_SRC, "clk_i2s0_8ch_rx_src", gpll_aupll_p, 0,
			RK3588_CLKSEL_CON(26), 7, 1, MFLAGS, 2, 5, DFLAGS,
			RK3588_CLKGATE_CON(7), 8, GFLAGS),
	COMPOSITE_FRACMUX(CLK_I2S0_8CH_RX_FRAC, "clk_i2s0_8ch_rx_frac", "clk_i2s0_8ch_rx_src", CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(27), 0,
			RK3588_CLKGATE_CON(7), 9, GFLAGS,
			&rk3588_i2s0_8ch_rx_fracmux, RK3588_FRAC_MAX_PRATE),
	GATE(MCLK_I2S0_8CH_RX, "mclk_i2s0_8ch_rx", "clk_i2s0_8ch_rx", 0,
			RK3588_CLKGATE_CON(7), 10, GFLAGS),
	MUX(I2S0_8CH_MCLKOUT, "i2s0_8ch_mclkout", i2s0_8ch_mclkout_p, CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(28), 2, 2, MFLAGS),

	GATE(HCLK_PDM1, "hclk_pdm1", "hclk_audio_root", 0,
			RK3588_CLKGATE_CON(9), 6, GFLAGS),
	COMPOSITE(MCLK_PDM1, "mclk_pdm1", gpll_cpll_aupll_p, 0,
			RK3588_CLKSEL_CON(36), 7, 2, MFLAGS, 2, 5, DFLAGS,
			RK3588_CLKGATE_CON(9), 7, GFLAGS),
	COMPOSITE_NODIV(HCLK_AUDIO_ROOT, "hclk_audio_root", mux_200m_100m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(24), 0, 2, MFLAGS,
			RK3588_CLKGATE_CON(7), 0, GFLAGS),
	COMPOSITE_NODIV(PCLK_AUDIO_ROOT, "pclk_audio_root", mux_100m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(24), 2, 2, MFLAGS,
			RK3588_CLKGATE_CON(7), 1, GFLAGS),

	GATE(HCLK_SPDIF0, "hclk_spdif0", "hclk_audio_root", 0,
			RK3588_CLKGATE_CON(8), 14, GFLAGS),
	COMPOSITE(CLK_SPDIF0_SRC, "clk_spdif0_src", gpll_aupll_p, 0,
			RK3588_CLKSEL_CON(32), 8, 1, MFLAGS, 3, 5, DFLAGS,
			RK3588_CLKGATE_CON(8), 15, GFLAGS),
	COMPOSITE_FRACMUX(CLK_SPDIF0_FRAC, "clk_spdif0_frac", "clk_spdif0_src", CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(33), 0,
			RK3588_CLKGATE_CON(9), 0, GFLAGS,
			&rk3588_spdif0_fracmux, RK3588_FRAC_MAX_PRATE),
	GATE(MCLK_SPDIF0, "mclk_spdif0", "clk_spdif0", 0,
			RK3588_CLKGATE_CON(9), 1, GFLAGS),

	GATE(HCLK_SPDIF1, "hclk_spdif1", "hclk_audio_root", 0,
			RK3588_CLKGATE_CON(9), 2, GFLAGS),
	COMPOSITE(CLK_SPDIF1_SRC, "clk_spdif1_src", gpll_aupll_p, 0,
			RK3588_CLKSEL_CON(34), 7, 1, MFLAGS, 2, 5, DFLAGS,
			RK3588_CLKGATE_CON(9), 3, GFLAGS),
	COMPOSITE_FRACMUX(CLK_SPDIF1_FRAC, "clk_spdif1_frac", "clk_spdif1_src", CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(35), 0,
			RK3588_CLKGATE_CON(9), 4, GFLAGS,
			&rk3588_spdif1_fracmux, RK3588_FRAC_MAX_PRATE),
	GATE(MCLK_SPDIF1, "mclk_spdif1", "clk_spdif1", 0,
			RK3588_CLKGATE_CON(9), 5, GFLAGS),

	COMPOSITE(ACLK_AV1_ROOT, "aclk_av1_root", gpll_cpll_aupll_p, 0,
			RK3588_CLKSEL_CON(163), 5, 2, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(68), 0, GFLAGS),
	GATE(ACLK_AV1, "aclk_av1", "aclk_av1_root", 0,
			RK3588_CLKGATE_CON(68), 2, GFLAGS),
	COMPOSITE_NODIV(PCLK_AV1_ROOT, "pclk_av1_root", mux_200m_100m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(163), 7, 2, MFLAGS,
			RK3588_CLKGATE_CON(68), 3, GFLAGS),
	GATE(PCLK_AV1, "pclk_av1", "pclk_av1_root", 0,
			RK3588_CLKGATE_CON(68), 5, GFLAGS),

	/* bus */
	GATE(PCLK_MAILBOX0, "pclk_mailbox0", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(16), 11, GFLAGS),
	GATE(PCLK_MAILBOX1, "pclk_mailbox1", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(16), 12, GFLAGS),
	GATE(PCLK_MAILBOX2, "pclk_mailbox2", "pclk_top_root", 0,
		RK3588_CLKGATE_CON(16), 13, GFLAGS),
	GATE(PCLK_PMU2, "pclk_pmu2", "pclk_top_root", CLK_IS_CRITICAL,
			RK3588_CLKGATE_CON(19), 3, GFLAGS),
	GATE(PCLK_PMUCM0_INTMUX, "pclk_pmucm0_intmux", "pclk_top_root", CLK_IS_CRITICAL,
			RK3588_CLKGATE_CON(19), 4, GFLAGS),
	GATE(PCLK_DDRCM0_INTMUX, "pclk_ddrcm0_intmux", "pclk_top_root", CLK_IS_CRITICAL,
			RK3588_CLKGATE_CON(19), 5, GFLAGS),

	GATE(PCLK_PWM1, "pclk_pwm1", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(15), 3, GFLAGS),
	COMPOSITE_NODIV(CLK_PWM1, "clk_pwm1", mux_100m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(59), 12, 2, MFLAGS,
			RK3588_CLKGATE_CON(15), 4, GFLAGS),
	GATE(CLK_PWM1_CAPTURE, "clk_pwm1_capture", "xin24m", 0,
			RK3588_CLKGATE_CON(15), 5, GFLAGS),
	GATE(PCLK_PWM2, "pclk_pwm2", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(15), 6, GFLAGS),
	COMPOSITE_NODIV(CLK_PWM2, "clk_pwm2", mux_100m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(59), 14, 2, MFLAGS,
			RK3588_CLKGATE_CON(15), 7, GFLAGS),
	GATE(CLK_PWM2_CAPTURE, "clk_pwm2_capture", "xin24m", 0,
			RK3588_CLKGATE_CON(15), 8, GFLAGS),
	GATE(PCLK_PWM3, "pclk_pwm3", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(15), 9, GFLAGS),
	COMPOSITE_NODIV(CLK_PWM3, "clk_pwm3", mux_100m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(60), 0, 2, MFLAGS,
			RK3588_CLKGATE_CON(15), 10, GFLAGS),
	GATE(CLK_PWM3_CAPTURE, "clk_pwm3_capture", "xin24m", 0,
			RK3588_CLKGATE_CON(15), 11, GFLAGS),

	GATE(PCLK_BUSTIMER0, "pclk_bustimer0", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(15), 12, GFLAGS),
	GATE(PCLK_BUSTIMER1, "pclk_bustimer1", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(15), 13, GFLAGS),
	COMPOSITE_NODIV(CLK_BUS_TIMER_ROOT, "clk_bus_timer_root", mux_24m_100m_p, 0,
			RK3588_CLKSEL_CON(60), 2, 1, MFLAGS,
			RK3588_CLKGATE_CON(15), 14, GFLAGS),
	GATE(CLK_BUSTIMER0, "clk_bustimer0", "clk_bus_timer_root", 0,
			RK3588_CLKGATE_CON(15), 15, GFLAGS),
	GATE(CLK_BUSTIMER1, "clk_bustimer1", "clk_bus_timer_root", 0,
			RK3588_CLKGATE_CON(16), 0, GFLAGS),
	GATE(CLK_BUSTIMER2, "clk_bustimer2", "clk_bus_timer_root", 0,
			RK3588_CLKGATE_CON(16), 1, GFLAGS),
	GATE(CLK_BUSTIMER3, "clk_bustimer3", "clk_bus_timer_root", 0,
			RK3588_CLKGATE_CON(16), 2, GFLAGS),
	GATE(CLK_BUSTIMER4, "clk_bustimer4", "clk_bus_timer_root", 0,
			RK3588_CLKGATE_CON(16), 3, GFLAGS),
	GATE(CLK_BUSTIMER5, "clk_bustimer5", "clk_bus_timer_root", 0,
			RK3588_CLKGATE_CON(16), 4, GFLAGS),
	GATE(CLK_BUSTIMER6, "clk_bustimer6", "clk_bus_timer_root", 0,
			RK3588_CLKGATE_CON(16), 5, GFLAGS),
	GATE(CLK_BUSTIMER7, "clk_bustimer7", "clk_bus_timer_root", 0,
			RK3588_CLKGATE_CON(16), 6, GFLAGS),
	GATE(CLK_BUSTIMER8, "clk_bustimer8", "clk_bus_timer_root", 0,
			RK3588_CLKGATE_CON(16), 7, GFLAGS),
	GATE(CLK_BUSTIMER9, "clk_bustimer9", "clk_bus_timer_root", 0,
			RK3588_CLKGATE_CON(16), 8, GFLAGS),
	GATE(CLK_BUSTIMER10, "clk_bustimer10", "clk_bus_timer_root", 0,
			RK3588_CLKGATE_CON(16), 9, GFLAGS),
	GATE(CLK_BUSTIMER11, "clk_bustimer11", "clk_bus_timer_root", 0,
			RK3588_CLKGATE_CON(16), 10, GFLAGS),

	GATE(PCLK_WDT0, "pclk_wdt0", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(15), 0, GFLAGS),
	GATE(TCLK_WDT0, "tclk_wdt0", "xin24m", 0,
			RK3588_CLKGATE_CON(15), 1, GFLAGS),

	GATE(PCLK_CAN0, "pclk_can0", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(11), 8, GFLAGS),
	COMPOSITE(CLK_CAN0, "clk_can0", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(39), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(11), 9, GFLAGS),
	GATE(PCLK_CAN1, "pclk_can1", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(11), 10, GFLAGS),
	COMPOSITE(CLK_CAN1, "clk_can1", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(39), 11, 1, MFLAGS, 6, 5, DFLAGS,
			RK3588_CLKGATE_CON(11), 11, GFLAGS),
	GATE(PCLK_CAN2, "pclk_can2", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(11), 12, GFLAGS),
	COMPOSITE(CLK_CAN2, "clk_can2", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(40), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(11), 13, GFLAGS),

	GATE(ACLK_DECOM, "aclk_decom", "aclk_bus_root", 0,
			RK3588_CLKGATE_CON(17), 6, GFLAGS),
	GATE(PCLK_DECOM, "pclk_decom", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(17), 7, GFLAGS),
	COMPOSITE(DCLK_DECOM, "dclk_decom", gpll_spll_p, 0,
			RK3588_CLKSEL_CON(62), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(17), 8, GFLAGS),
	GATE(ACLK_DMAC0, "aclk_dmac0", "aclk_bus_root", 0,
			RK3588_CLKGATE_CON(10), 5, GFLAGS),
	GATE(ACLK_DMAC1, "aclk_dmac1", "aclk_bus_root", 0,
			RK3588_CLKGATE_CON(10), 6, GFLAGS),
	GATE(ACLK_DMAC2, "aclk_dmac2", "aclk_bus_root", 0,
			RK3588_CLKGATE_CON(10), 7, GFLAGS),

	COMPOSITE(ACLK_BUS_ROOT, "aclk_bus_root", gpll_cpll_p, CLK_IS_CRITICAL,
			RK3588_CLKSEL_CON(38), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(10), 0, GFLAGS),
	GATE(ACLK_GIC, "aclk_gic", "aclk_bus_root", CLK_IS_CRITICAL,
			RK3588_CLKGATE_CON(10), 3, GFLAGS),

	GATE(PCLK_GPIO1, "pclk_gpio1", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(16), 14, GFLAGS),
	COMPOSITE(DBCLK_GPIO1, "dbclk_gpio1", mux_24m_32k_p, 0,
			RK3588_CLKSEL_CON(60), 8, 1, MFLAGS, 3, 5, DFLAGS,
			RK3588_CLKGATE_CON(16), 15, GFLAGS),
	GATE(PCLK_GPIO2, "pclk_gpio2", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(17), 0, GFLAGS),
	COMPOSITE(DBCLK_GPIO2, "dbclk_gpio2", mux_24m_32k_p, 0,
			RK3588_CLKSEL_CON(60), 14, 1, MFLAGS, 9, 5, DFLAGS,
			RK3588_CLKGATE_CON(17), 1, GFLAGS),
	GATE(PCLK_GPIO3, "pclk_gpio3", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(17), 2, GFLAGS),
	COMPOSITE(DBCLK_GPIO3, "dbclk_gpio3", mux_24m_32k_p, 0,
			RK3588_CLKSEL_CON(61), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(17), 3, GFLAGS),
	GATE(PCLK_GPIO4, "pclk_gpio4", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(17), 4, GFLAGS),
	COMPOSITE(DBCLK_GPIO4, "dbclk_gpio4", mux_24m_32k_p, 0,
			RK3588_CLKSEL_CON(61), 11, 1, MFLAGS, 6, 5, DFLAGS,
			RK3588_CLKGATE_CON(17), 5, GFLAGS),

	GATE(PCLK_I2C1, "pclk_i2c1", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(10), 8, GFLAGS),
	GATE(PCLK_I2C2, "pclk_i2c2", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(10), 9, GFLAGS),
	GATE(PCLK_I2C3, "pclk_i2c3", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(10), 10, GFLAGS),
	GATE(PCLK_I2C4, "pclk_i2c4", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(10), 11, GFLAGS),
	GATE(PCLK_I2C5, "pclk_i2c5", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(10), 12, GFLAGS),
	GATE(PCLK_I2C6, "pclk_i2c6", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(10), 13, GFLAGS),
	GATE(PCLK_I2C7, "pclk_i2c7", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(10), 14, GFLAGS),
	GATE(PCLK_I2C8, "pclk_i2c8", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(10), 15, GFLAGS),
	COMPOSITE_NODIV(CLK_I2C1, "clk_i2c1", mux_200m_100m_p, 0,
			RK3588_CLKSEL_CON(38), 6, 1, MFLAGS,
			RK3588_CLKGATE_CON(11), 0, GFLAGS),
	COMPOSITE_NODIV(CLK_I2C2, "clk_i2c2", mux_200m_100m_p, 0,
			RK3588_CLKSEL_CON(38), 7, 1, MFLAGS,
			RK3588_CLKGATE_CON(11), 1, GFLAGS),
	COMPOSITE_NODIV(CLK_I2C3, "clk_i2c3", mux_200m_100m_p, 0,
			RK3588_CLKSEL_CON(38), 8, 1, MFLAGS,
			RK3588_CLKGATE_CON(11), 2, GFLAGS),
	COMPOSITE_NODIV(CLK_I2C4, "clk_i2c4", mux_200m_100m_p, 0,
			RK3588_CLKSEL_CON(38), 9, 1, MFLAGS,
			RK3588_CLKGATE_CON(11), 3, GFLAGS),
	COMPOSITE_NODIV(CLK_I2C5, "clk_i2c5", mux_200m_100m_p, 0,
			RK3588_CLKSEL_CON(38), 10, 1, MFLAGS,
			RK3588_CLKGATE_CON(11), 4, GFLAGS),
	COMPOSITE_NODIV(CLK_I2C6, "clk_i2c6", mux_200m_100m_p, 0,
			RK3588_CLKSEL_CON(38), 11, 1, MFLAGS,
			RK3588_CLKGATE_CON(11), 5, GFLAGS),
	COMPOSITE_NODIV(CLK_I2C7, "clk_i2c7", mux_200m_100m_p, 0,
			RK3588_CLKSEL_CON(38), 12, 1, MFLAGS,
			RK3588_CLKGATE_CON(11), 6, GFLAGS),
	COMPOSITE_NODIV(CLK_I2C8, "clk_i2c8", mux_200m_100m_p, 0,
			RK3588_CLKSEL_CON(38), 13, 1, MFLAGS,
			RK3588_CLKGATE_CON(11), 7, GFLAGS),

	GATE(PCLK_OTPC_NS, "pclk_otpc_ns", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(18), 9, GFLAGS),
	GATE(CLK_OTPC_NS, "clk_otpc_ns", "xin24m", 0,
			RK3588_CLKGATE_CON(18), 10, GFLAGS),
	GATE(CLK_OTPC_ARB, "clk_otpc_arb", "xin24m", 0,
			RK3588_CLKGATE_CON(18), 11, GFLAGS),
	GATE(CLK_OTP_PHY_G, "clk_otp_phy_g", "xin24m", 0,
			RK3588_CLKGATE_CON(18), 13, GFLAGS),
	GATE(CLK_OTPC_AUTO_RD_G, "clk_otpc_auto_rd_g", "xin24m", 0,
			RK3588_CLKGATE_CON(18), 12, GFLAGS),

	GATE(PCLK_SARADC, "pclk_saradc", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(11), 14, GFLAGS),
	COMPOSITE(CLK_SARADC, "clk_saradc", gpll_24m_p, 0,
			RK3588_CLKSEL_CON(40), 14, 1, MFLAGS, 6, 8, DFLAGS,
			RK3588_CLKGATE_CON(11), 15, GFLAGS),

	GATE(PCLK_SPI0, "pclk_spi0", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(14), 6, GFLAGS),
	GATE(PCLK_SPI1, "pclk_spi1", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(14), 7, GFLAGS),
	GATE(PCLK_SPI2, "pclk_spi2", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(14), 8, GFLAGS),
	GATE(PCLK_SPI3, "pclk_spi3", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(14), 9, GFLAGS),
	GATE(PCLK_SPI4, "pclk_spi4", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(14), 10, GFLAGS),
	COMPOSITE_NODIV(CLK_SPI0, "clk_spi0", mux_200m_150m_24m_p, 0,
			RK3588_CLKSEL_CON(59), 2, 2, MFLAGS,
			RK3588_CLKGATE_CON(14), 11, GFLAGS),
	COMPOSITE_NODIV(CLK_SPI1, "clk_spi1", mux_200m_150m_24m_p, 0,
			RK3588_CLKSEL_CON(59), 4, 2, MFLAGS,
			RK3588_CLKGATE_CON(14), 12, GFLAGS),
	COMPOSITE_NODIV(CLK_SPI2, "clk_spi2", mux_200m_150m_24m_p, 0,
			RK3588_CLKSEL_CON(59), 6, 2, MFLAGS,
			RK3588_CLKGATE_CON(14), 13, GFLAGS),
	COMPOSITE_NODIV(CLK_SPI3, "clk_spi3", mux_200m_150m_24m_p, 0,
			RK3588_CLKSEL_CON(59), 8, 2, MFLAGS,
			RK3588_CLKGATE_CON(14), 14, GFLAGS),
	COMPOSITE_NODIV(CLK_SPI4, "clk_spi4", mux_200m_150m_24m_p, 0,
			RK3588_CLKSEL_CON(59), 10, 2, MFLAGS,
			RK3588_CLKGATE_CON(14), 15, GFLAGS),

	GATE(ACLK_SPINLOCK, "aclk_spinlock", "aclk_bus_root", CLK_IGNORE_UNUSED,
			RK3588_CLKGATE_CON(18), 6, GFLAGS),
	GATE(PCLK_TSADC, "pclk_tsadc", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(12), 0, GFLAGS),
	COMPOSITE(CLK_TSADC, "clk_tsadc", gpll_24m_p, 0,
			RK3588_CLKSEL_CON(41), 8, 1, MFLAGS, 0, 8, DFLAGS,
			RK3588_CLKGATE_CON(12), 1, GFLAGS),

	GATE(PCLK_UART1, "pclk_uart1", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(12), 2, GFLAGS),
	GATE(PCLK_UART2, "pclk_uart2", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(12), 3, GFLAGS),
	GATE(PCLK_UART3, "pclk_uart3", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(12), 4, GFLAGS),
	GATE(PCLK_UART4, "pclk_uart4", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(12), 5, GFLAGS),
	GATE(PCLK_UART5, "pclk_uart5", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(12), 6, GFLAGS),
	GATE(PCLK_UART6, "pclk_uart6", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(12), 7, GFLAGS),
	GATE(PCLK_UART7, "pclk_uart7", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(12), 8, GFLAGS),
	GATE(PCLK_UART8, "pclk_uart8", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(12), 9, GFLAGS),
	GATE(PCLK_UART9, "pclk_uart9", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(12), 10, GFLAGS),

	COMPOSITE(CLK_UART1_SRC, "clk_uart1_src", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(41), 14, 1, MFLAGS, 9, 5, DFLAGS,
			RK3588_CLKGATE_CON(12), 11, GFLAGS),
	COMPOSITE_FRACMUX(CLK_UART1_FRAC, "clk_uart1_frac", "clk_uart1_src", CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(42), CLK_FRAC_DIVIDER_NO_LIMIT,
			RK3588_CLKGATE_CON(12), 12, GFLAGS,
			&rk3588_uart1_fracmux, RK3588_UART_FRAC_MAX_PRATE),
	GATE(SCLK_UART1, "sclk_uart1", "clk_uart1", 0,
			RK3588_CLKGATE_CON(12), 13, GFLAGS),
	COMPOSITE(CLK_UART2_SRC, "clk_uart2_src", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(43), 7, 1, MFLAGS, 2, 5, DFLAGS,
			RK3588_CLKGATE_CON(12), 14, GFLAGS),
	COMPOSITE_FRACMUX(CLK_UART2_FRAC, "clk_uart2_frac", "clk_uart2_src", CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(44), CLK_FRAC_DIVIDER_NO_LIMIT,
			RK3588_CLKGATE_CON(12), 15, GFLAGS,
			&rk3588_uart2_fracmux, RK3588_UART_FRAC_MAX_PRATE),
	GATE(SCLK_UART2, "sclk_uart2", "clk_uart2", 0,
			RK3588_CLKGATE_CON(13), 0, GFLAGS),
	COMPOSITE(CLK_UART3_SRC, "clk_uart3_src", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(45), 7, 1, MFLAGS, 2, 5, DFLAGS,
			RK3588_CLKGATE_CON(13), 1, GFLAGS),
	COMPOSITE_FRACMUX(CLK_UART3_FRAC, "clk_uart3_frac", "clk_uart3_src", CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(46), CLK_FRAC_DIVIDER_NO_LIMIT,
			RK3588_CLKGATE_CON(13), 2, GFLAGS,
			&rk3588_uart3_fracmux, RK3588_UART_FRAC_MAX_PRATE),
	GATE(SCLK_UART3, "sclk_uart3", "clk_uart3", 0,
			RK3588_CLKGATE_CON(13), 3, GFLAGS),
	COMPOSITE(CLK_UART4_SRC, "clk_uart4_src", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(47), 7, 1, MFLAGS, 2, 5, DFLAGS,
			RK3588_CLKGATE_CON(13), 4, GFLAGS),
	COMPOSITE_FRACMUX(CLK_UART4_FRAC, "clk_uart4_frac", "clk_uart4_src", CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(48), CLK_FRAC_DIVIDER_NO_LIMIT,
			RK3588_CLKGATE_CON(13), 5, GFLAGS,
			&rk3588_uart4_fracmux, RK3588_UART_FRAC_MAX_PRATE),
	GATE(SCLK_UART4, "sclk_uart4", "clk_uart4", 0,
			RK3588_CLKGATE_CON(13), 6, GFLAGS),
	COMPOSITE(CLK_UART5_SRC, "clk_uart5_src", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(49), 7, 1, MFLAGS, 2, 5, DFLAGS,
			RK3588_CLKGATE_CON(13), 7, GFLAGS),
	COMPOSITE_FRACMUX(CLK_UART5_FRAC, "clk_uart5_frac", "clk_uart5_src", CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(50), CLK_FRAC_DIVIDER_NO_LIMIT,
			RK3588_CLKGATE_CON(13), 8, GFLAGS,
			&rk3588_uart5_fracmux, RK3588_UART_FRAC_MAX_PRATE),
	GATE(SCLK_UART5, "sclk_uart5", "clk_uart5", 0,
			RK3588_CLKGATE_CON(13), 9, GFLAGS),
	COMPOSITE(CLK_UART6_SRC, "clk_uart6_src", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(51), 7, 1, MFLAGS, 2, 5, DFLAGS,
			RK3588_CLKGATE_CON(13), 10, GFLAGS),
	COMPOSITE_FRACMUX(CLK_UART6_FRAC, "clk_uart6_frac", "clk_uart6_src", CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(52), CLK_FRAC_DIVIDER_NO_LIMIT,
			RK3588_CLKGATE_CON(13), 11, GFLAGS,
			&rk3588_uart6_fracmux, RK3588_UART_FRAC_MAX_PRATE),
	GATE(SCLK_UART6, "sclk_uart6", "clk_uart6", 0,
			RK3588_CLKGATE_CON(13), 12, GFLAGS),
	COMPOSITE(CLK_UART7_SRC, "clk_uart7_src", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(53), 7, 1, MFLAGS, 2, 5, DFLAGS,
			RK3588_CLKGATE_CON(13), 13, GFLAGS),
	COMPOSITE_FRACMUX(CLK_UART7_FRAC, "clk_uart7_frac", "clk_uart7_src", CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(54), CLK_FRAC_DIVIDER_NO_LIMIT,
			RK3588_CLKGATE_CON(13), 14, GFLAGS,
			&rk3588_uart7_fracmux, RK3588_UART_FRAC_MAX_PRATE),
	GATE(SCLK_UART7, "sclk_uart7", "clk_uart7", 0,
			RK3588_CLKGATE_CON(13), 15, GFLAGS),
	COMPOSITE(CLK_UART8_SRC, "clk_uart8_src", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(55), 7, 1, MFLAGS, 2, 5, DFLAGS,
			RK3588_CLKGATE_CON(14), 0, GFLAGS),
	COMPOSITE_FRACMUX(CLK_UART8_FRAC, "clk_uart8_frac", "clk_uart8_src", CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(56), CLK_FRAC_DIVIDER_NO_LIMIT,
			RK3588_CLKGATE_CON(14), 1, GFLAGS,
			&rk3588_uart8_fracmux, RK3588_UART_FRAC_MAX_PRATE),
	GATE(SCLK_UART8, "sclk_uart8", "clk_uart8", 0,
			RK3588_CLKGATE_CON(14), 2, GFLAGS),
	COMPOSITE(CLK_UART9_SRC, "clk_uart9_src", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(57), 7, 1, MFLAGS, 2, 5, DFLAGS,
			RK3588_CLKGATE_CON(14), 3, GFLAGS),
	COMPOSITE_FRACMUX(CLK_UART9_FRAC, "clk_uart9_frac", "clk_uart9_src", CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(58), CLK_FRAC_DIVIDER_NO_LIMIT,
			RK3588_CLKGATE_CON(14), 4, GFLAGS,
			&rk3588_uart9_fracmux, RK3588_UART_FRAC_MAX_PRATE),
	GATE(SCLK_UART9, "sclk_uart9", "clk_uart9", 0,
			RK3588_CLKGATE_CON(14), 5, GFLAGS),

	/* center */
	COMPOSITE_NODIV(ACLK_CENTER_ROOT, "aclk_center_root", mux_700m_400m_200m_24m_p, CLK_IS_CRITICAL,
			RK3588_CLKSEL_CON(165), 0, 2, MFLAGS,
			RK3588_CLKGATE_CON(69), 0, GFLAGS),
	COMPOSITE_NODIV(ACLK_CENTER_LOW_ROOT, "aclk_center_low_root", mux_500m_250m_100m_24m_p, CLK_IS_CRITICAL,
			RK3588_CLKSEL_CON(165), 2, 2, MFLAGS,
			RK3588_CLKGATE_CON(69), 1, GFLAGS),
	COMPOSITE_NODIV(HCLK_CENTER_ROOT, "hclk_center_root", mux_400m_200m_100m_24m_p, CLK_IS_CRITICAL,
			RK3588_CLKSEL_CON(165), 4, 2, MFLAGS,
			RK3588_CLKGATE_CON(69), 2, GFLAGS),
	COMPOSITE_NODIV(PCLK_CENTER_ROOT, "pclk_center_root", mux_200m_100m_50m_24m_p, CLK_IS_CRITICAL,
			RK3588_CLKSEL_CON(165), 6, 2, MFLAGS,
			RK3588_CLKGATE_CON(69), 3, GFLAGS),
	GATE(ACLK_DMA2DDR, "aclk_dma2ddr", "aclk_center_root", CLK_IS_CRITICAL,
			RK3588_CLKGATE_CON(69), 5, GFLAGS),
	GATE(ACLK_DDR_SHAREMEM, "aclk_ddr_sharemem", "aclk_center_low_root", CLK_IS_CRITICAL,
			RK3588_CLKGATE_CON(69), 6, GFLAGS),
	COMPOSITE_NODIV(ACLK_CENTER_S200_ROOT, "aclk_center_s200_root", mux_200m_100m_50m_24m_p, CLK_IS_CRITICAL,
			RK3588_CLKSEL_CON(165), 8, 2, MFLAGS,
			RK3588_CLKGATE_CON(69), 8, GFLAGS),
	COMPOSITE_NODIV(ACLK_CENTER_S400_ROOT, "aclk_center_s400_root", mux_400m_200m_100m_24m_p, CLK_IS_CRITICAL,
			RK3588_CLKSEL_CON(165), 10, 2, MFLAGS,
			RK3588_CLKGATE_CON(69), 9, GFLAGS),
	GATE(FCLK_DDR_CM0_CORE, "fclk_ddr_cm0_core", "hclk_center_root", CLK_IS_CRITICAL,
			RK3588_CLKGATE_CON(69), 14, GFLAGS),
	COMPOSITE_NODIV(CLK_DDR_TIMER_ROOT, "clk_ddr_timer_root", mux_24m_100m_p, CLK_IGNORE_UNUSED,
			RK3588_CLKSEL_CON(165), 12, 1, MFLAGS,
			RK3588_CLKGATE_CON(69), 15, GFLAGS),
	GATE(CLK_DDR_TIMER0, "clk_ddr_timer0", "clk_ddr_timer_root", 0,
			RK3588_CLKGATE_CON(70), 0, GFLAGS),
	GATE(CLK_DDR_TIMER1, "clk_ddr_timer1", "clk_ddr_timer_root", 0,
			RK3588_CLKGATE_CON(70), 1, GFLAGS),
	GATE(TCLK_WDT_DDR, "tclk_wdt_ddr", "xin24m", 0,
			RK3588_CLKGATE_CON(70), 2, GFLAGS),
	COMPOSITE(CLK_DDR_CM0_RTC, "clk_ddr_cm0_rtc", mux_24m_32k_p, CLK_IS_CRITICAL,
			RK3588_CLKSEL_CON(166), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(70), 4, GFLAGS),
	GATE(PCLK_WDT, "pclk_wdt", "pclk_center_root", 0,
			RK3588_CLKGATE_CON(70), 7, GFLAGS),
	GATE(PCLK_TIMER, "pclk_timer", "pclk_center_root", 0,
			RK3588_CLKGATE_CON(70), 8, GFLAGS),
	GATE(PCLK_DMA2DDR, "pclk_dma2ddr", "pclk_center_root", CLK_IS_CRITICAL,
			RK3588_CLKGATE_CON(70), 9, GFLAGS),
	GATE(PCLK_SHAREMEM, "pclk_sharemem", "pclk_center_root", CLK_IS_CRITICAL,
			RK3588_CLKGATE_CON(70), 10, GFLAGS),

	/* top */
	COMPOSITE(CLK_50M_SRC, "clk_50m_src", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(0), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(0), 0, GFLAGS),
	COMPOSITE(CLK_100M_SRC, "clk_100m_src", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(0), 11, 1, MFLAGS, 6, 5, DFLAGS,
			RK3588_CLKGATE_CON(0), 1, GFLAGS),
	COMPOSITE(CLK_150M_SRC, "clk_150m_src", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(1), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(0), 2, GFLAGS),
	COMPOSITE(CLK_200M_SRC, "clk_200m_src", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(1), 11, 1, MFLAGS, 6, 5, DFLAGS,
			RK3588_CLKGATE_CON(0), 3, GFLAGS),
	COMPOSITE(CLK_250M_SRC, "clk_250m_src", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(2), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(0), 4, GFLAGS),
	COMPOSITE(CLK_300M_SRC, "clk_300m_src", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(2), 11, 1, MFLAGS, 6, 5, DFLAGS,
			RK3588_CLKGATE_CON(0), 5, GFLAGS),
	COMPOSITE(CLK_350M_SRC, "clk_350m_src", gpll_spll_p, 0,
			RK3588_CLKSEL_CON(3), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(0), 6, GFLAGS),
	COMPOSITE(CLK_400M_SRC, "clk_400m_src", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(3), 11, 1, MFLAGS, 6, 5, DFLAGS,
			RK3588_CLKGATE_CON(0), 7, GFLAGS),
	COMPOSITE_HALFDIV(CLK_450M_SRC, "clk_450m_src", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(4), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(0), 8, GFLAGS),
	COMPOSITE(CLK_500M_SRC, "clk_500m_src", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(4), 11, 1, MFLAGS, 6, 5, DFLAGS,
			RK3588_CLKGATE_CON(0), 9, GFLAGS),
	COMPOSITE(CLK_600M_SRC, "clk_600m_src", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(5), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(0), 10, GFLAGS),
	COMPOSITE(CLK_650M_SRC, "clk_650m_src", gpll_lpll_p, 0,
			RK3588_CLKSEL_CON(5), 11, 1, MFLAGS, 6, 5, DFLAGS,
			RK3588_CLKGATE_CON(0), 11, GFLAGS),
	COMPOSITE(CLK_700M_SRC, "clk_700m_src", gpll_spll_p, 0,
			RK3588_CLKSEL_CON(6), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(0), 12, GFLAGS),
	COMPOSITE(CLK_800M_SRC, "clk_800m_src", gpll_aupll_p, 0,
			RK3588_CLKSEL_CON(6), 11, 1, MFLAGS, 6, 5, DFLAGS,
			RK3588_CLKGATE_CON(0), 13, GFLAGS),
	COMPOSITE_HALFDIV(CLK_1000M_SRC, "clk_1000m_src", gpll_cpll_npll_v0pll_p, 0,
			RK3588_CLKSEL_CON(7), 5, 2, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(0), 14, GFLAGS),
	COMPOSITE(CLK_1200M_SRC, "clk_1200m_src", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(7), 12, 1, MFLAGS, 7, 5, DFLAGS,
			RK3588_CLKGATE_CON(0), 15, GFLAGS),
	COMPOSITE_NODIV(ACLK_TOP_M300_ROOT, "aclk_top_m300_root", mux_300m_200m_100m_24m_p, CLK_IS_CRITICAL,
			RK3588_CLKSEL_CON(9), 0, 2, MFLAGS,
			RK3588_CLKGATE_CON(1), 10, GFLAGS),
	COMPOSITE_NODIV(ACLK_TOP_M500_ROOT, "aclk_top_m500_root", mux_500m_300m_100m_24m_p, CLK_IS_CRITICAL,
			RK3588_CLKSEL_CON(9), 2, 2, MFLAGS,
			RK3588_CLKGATE_CON(1), 11, GFLAGS),
	COMPOSITE_NODIV(ACLK_TOP_M400_ROOT, "aclk_top_m400_root", mux_400m_200m_100m_24m_p, CLK_IS_CRITICAL,
			RK3588_CLKSEL_CON(9), 4, 2, MFLAGS,
			RK3588_CLKGATE_CON(1), 12, GFLAGS),
	COMPOSITE_NODIV(ACLK_TOP_S200_ROOT, "aclk_top_s200_root", mux_200m_100m_50m_24m_p, CLK_IS_CRITICAL,
			RK3588_CLKSEL_CON(9), 6, 2, MFLAGS,
			RK3588_CLKGATE_CON(1), 13, GFLAGS),
	COMPOSITE_NODIV(ACLK_TOP_S400_ROOT, "aclk_top_s400_root", mux_400m_200m_100m_24m_p, CLK_IS_CRITICAL,
			RK3588_CLKSEL_CON(9), 8, 2, MFLAGS,
			RK3588_CLKGATE_CON(1), 14, GFLAGS),
	COMPOSITE(CLK_MIPI_CAMARAOUT_M0, "clk_mipi_camaraout_m0", mux_24m_spll_gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(18), 8, 2, MFLAGS, 0, 8, DFLAGS,
			RK3588_CLKGATE_CON(5), 9, GFLAGS),
	COMPOSITE(CLK_MIPI_CAMARAOUT_M1, "clk_mipi_camaraout_m1", mux_24m_spll_gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(19), 8, 2, MFLAGS, 0, 8, DFLAGS,
			RK3588_CLKGATE_CON(5), 10, GFLAGS),
	COMPOSITE(CLK_MIPI_CAMARAOUT_M2, "clk_mipi_camaraout_m2", mux_24m_spll_gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(20), 8, 2, MFLAGS, 0, 8, DFLAGS,
			RK3588_CLKGATE_CON(5), 11, GFLAGS),
	COMPOSITE(CLK_MIPI_CAMARAOUT_M3, "clk_mipi_camaraout_m3", mux_24m_spll_gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(21), 8, 2, MFLAGS, 0, 8, DFLAGS,
			RK3588_CLKGATE_CON(5), 12, GFLAGS),
	COMPOSITE(CLK_MIPI_CAMARAOUT_M4, "clk_mipi_camaraout_m4", mux_24m_spll_gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(22), 8, 2, MFLAGS, 0, 8, DFLAGS,
			RK3588_CLKGATE_CON(5), 13, GFLAGS),
	COMPOSITE(MCLK_GMAC0_OUT, "mclk_gmac0_out", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(15), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RK3588_CLKGATE_CON(5), 3, GFLAGS),
	COMPOSITE(REFCLKO25M_ETH0_OUT, "refclko25m_eth0_out", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(15), 15, 1, MFLAGS, 8, 7, DFLAGS,
			RK3588_CLKGATE_CON(5), 4, GFLAGS),
	COMPOSITE(REFCLKO25M_ETH1_OUT, "refclko25m_eth1_out", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(16), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RK3588_CLKGATE_CON(5), 5, GFLAGS),
	COMPOSITE(CLK_CIFOUT_OUT, "clk_cifout_out", gpll_cpll_24m_spll_p, 0,
			RK3588_CLKSEL_CON(17), 8, 2, MFLAGS, 0, 8, DFLAGS,
			RK3588_CLKGATE_CON(5), 6, GFLAGS),
	GATE(PCLK_MIPI_DCPHY0, "pclk_mipi_dcphy0", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(3), 14, GFLAGS),
	GATE(PCLK_MIPI_DCPHY1, "pclk_mipi_dcphy1", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(4), 3, GFLAGS),
	GATE(PCLK_CSIPHY0, "pclk_csiphy0", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(1), 6, GFLAGS),
	GATE(PCLK_CSIPHY1, "pclk_csiphy1", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(1), 8, GFLAGS),
	COMPOSITE(ACLK_TOP_ROOT, "aclk_top_root", gpll_cpll_aupll_p, CLK_IS_CRITICAL,
			RK3588_CLKSEL_CON(8), 5, 2, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(1), 0, GFLAGS),
	COMPOSITE_NODIV(PCLK_TOP_ROOT, "pclk_top_root", mux_100m_50m_24m_p, CLK_IS_CRITICAL,
			RK3588_CLKSEL_CON(8), 7, 2, MFLAGS,
			RK3588_CLKGATE_CON(1), 1, GFLAGS),
	COMPOSITE(ACLK_LOW_TOP_ROOT, "aclk_low_top_root", gpll_cpll_p, CLK_IS_CRITICAL,
			RK3588_CLKSEL_CON(8), 14, 1, MFLAGS, 9, 5, DFLAGS,
			RK3588_CLKGATE_CON(1), 2, GFLAGS),
	GATE(PCLK_CRU, "pclk_cru", "pclk_top_root", CLK_IS_CRITICAL,
			RK3588_CLKGATE_CON(5), 0, GFLAGS),

	/* gpu */
	COMPOSITE_NODIV(PCLK_GPU_ROOT, "pclk_gpu_root", mux_100m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(161), 0, 2, MFLAGS,
			RK3588_CLKGATE_CON(66), 13, GFLAGS),
	COMPOSITE(CLK_GPU_SRC, "clk_gpu_src", gpll_cpll_aupll_npll_spll_p, 0,
			RK3588_CLKSEL_CON(158), 5, 3, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(66), 1, GFLAGS),
	GATE(CLK_GPU, "clk_gpu", "clk_gpu_src", 0,
			RK3588_CLKGATE_CON(66), 4, GFLAGS),
	GATE(CLK_GPU_COREGROUP, "clk_gpu_coregroup", "clk_gpu_src", 0,
			RK3588_CLKGATE_CON(66), 6, GFLAGS),
	COMPOSITE_NOMUX(CLK_GPU_STACKS, "clk_gpu_stacks", "clk_gpu_src", 0,
			RK3588_CLKSEL_CON(159), 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(66), 7, GFLAGS),
	GATE(PCLK_GPU_PVTM, "pclk_gpu_pvtm", "pclk_gpu_root", 0,
			RK3588_CLKGATE_CON(66), 15, GFLAGS),
	GATE(CLK_GPU_PVTM, "clk_gpu_pvtm", "xin24m", 0,
			RK3588_CLKGATE_CON(67), 0, GFLAGS),
	GATE(CLK_CORE_GPU_PVTM, "clk_core_gpu_pvtm", "clk_gpu_src", 0,
			RK3588_CLKGATE_CON(67), 1, GFLAGS),
	GATE(PCLK_GPU_GRF, "pclk_gpu_grf", "pclk_gpu_root", CLK_IGNORE_UNUSED,
			RK3588_CLKGATE_CON(67), 2, GFLAGS),

	/* isp1 */
	COMPOSITE(ACLK_ISP1_ROOT, "aclk_isp1_root", gpll_cpll_aupll_spll_p, 0,
			RK3588_CLKSEL_CON(67), 5, 2, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(26), 0, GFLAGS),
	COMPOSITE_NODIV(HCLK_ISP1_ROOT, "hclk_isp1_root", mux_200m_100m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(67), 7, 2, MFLAGS,
			RK3588_CLKGATE_CON(26), 1, GFLAGS),
	COMPOSITE(CLK_ISP1_CORE, "clk_isp1_core", gpll_cpll_aupll_spll_p, 0,
			RK3588_CLKSEL_CON(67), 14, 2, MFLAGS, 9, 5, DFLAGS,
			RK3588_CLKGATE_CON(26), 2, GFLAGS),
	GATE(CLK_ISP1_CORE_MARVIN, "clk_isp1_core_marvin", "clk_isp1_core", 0,
			RK3588_CLKGATE_CON(26), 3, GFLAGS),
	GATE(CLK_ISP1_CORE_VICAP, "clk_isp1_core_vicap", "clk_isp1_core", 0,
			RK3588_CLKGATE_CON(26), 4, GFLAGS),
	GATE(ACLK_ISP1, "aclk_isp1", "aclk_isp1_root", 0,
			RK3588_CLKGATE_CON(26), 5, GFLAGS),
	GATE(HCLK_ISP1, "hclk_isp1", "hclk_isp1_root", 0,
			RK3588_CLKGATE_CON(26), 7, GFLAGS),

	/* npu */
	GATE(ACLK_NPU1, "aclk_npu1", "clk_npu_dsu0", 0,
			RK3588_CLKGATE_CON(27), 0, GFLAGS),
	GATE(HCLK_NPU1, "hclk_npu1", "hclk_npu_root", 0,
			RK3588_CLKGATE_CON(27), 2, GFLAGS),
	GATE(ACLK_NPU2, "aclk_npu2", "clk_npu_dsu0", 0,
			RK3588_CLKGATE_CON(28), 0, GFLAGS),
	GATE(HCLK_NPU2, "hclk_npu2", "hclk_npu_root", 0,
			RK3588_CLKGATE_CON(28), 2, GFLAGS),
	COMPOSITE_NODIV(HCLK_NPU_CM0_ROOT, "hclk_npu_cm0_root", mux_400m_200m_100m_24m_p, 0,
			RK3588_CLKSEL_CON(74), 5, 2, MFLAGS,
			RK3588_CLKGATE_CON(30), 1, GFLAGS),
	GATE(FCLK_NPU_CM0_CORE, "fclk_npu_cm0_core", "hclk_npu_cm0_root", 0,
			RK3588_CLKGATE_CON(30), 3, GFLAGS),
	COMPOSITE(CLK_NPU_CM0_RTC, "clk_npu_cm0_rtc", mux_24m_32k_p, 0,
			RK3588_CLKSEL_CON(74), 12, 1, MFLAGS, 7, 5, DFLAGS,
			RK3588_CLKGATE_CON(30), 5, GFLAGS),
	GATE(PCLK_NPU_PVTM, "pclk_npu_pvtm", "pclk_npu_root", 0,
			RK3588_CLKGATE_CON(29), 12, GFLAGS),
	GATE(PCLK_NPU_GRF, "pclk_npu_grf", "pclk_npu_root", CLK_IGNORE_UNUSED,
			RK3588_CLKGATE_CON(29), 13, GFLAGS),
	GATE(CLK_NPU_PVTM, "clk_npu_pvtm", "xin24m", 0,
			RK3588_CLKGATE_CON(29), 14, GFLAGS),
	GATE(CLK_CORE_NPU_PVTM, "clk_core_npu_pvtm", "clk_npu_dsu0", 0,
			RK3588_CLKGATE_CON(29), 15, GFLAGS),
	GATE(ACLK_NPU0, "aclk_npu0", "clk_npu_dsu0", 0,
			RK3588_CLKGATE_CON(30), 6, GFLAGS),
	GATE(HCLK_NPU0, "hclk_npu0", "hclk_npu_root", 0,
			RK3588_CLKGATE_CON(30), 8, GFLAGS),
	COMPOSITE_NODIV(HCLK_NPU_ROOT, "hclk_npu_root", mux_200m_100m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(73), 0, 2, MFLAGS,
			RK3588_CLKGATE_CON(29), 0, GFLAGS),
	COMPOSITE(CLK_NPU_DSU0, "clk_npu_dsu0", gpll_cpll_aupll_npll_spll_p, 0,
			RK3588_CLKSEL_CON(73), 7, 3, MFLAGS, 2, 5, DFLAGS,
			RK3588_CLKGATE_CON(29), 1, GFLAGS),
	COMPOSITE_NODIV(PCLK_NPU_ROOT, "pclk_npu_root", mux_100m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(74), 1, 2, MFLAGS,
			RK3588_CLKGATE_CON(29), 4, GFLAGS),
	GATE(PCLK_NPU_TIMER, "pclk_npu_timer", "pclk_npu_root", 0,
			RK3588_CLKGATE_CON(29), 6, GFLAGS),
	COMPOSITE_NODIV(CLK_NPUTIMER_ROOT, "clk_nputimer_root", mux_24m_100m_p, 0,
			RK3588_CLKSEL_CON(74), 3, 1, MFLAGS,
			RK3588_CLKGATE_CON(29), 7, GFLAGS),
	GATE(CLK_NPUTIMER0, "clk_nputimer0", "clk_nputimer_root", 0,
			RK3588_CLKGATE_CON(29), 8, GFLAGS),
	GATE(CLK_NPUTIMER1, "clk_nputimer1", "clk_nputimer_root", 0,
			RK3588_CLKGATE_CON(29), 9, GFLAGS),
	GATE(PCLK_NPU_WDT, "pclk_npu_wdt", "pclk_npu_root", 0,
			RK3588_CLKGATE_CON(29), 10, GFLAGS),
	GATE(TCLK_NPU_WDT, "tclk_npu_wdt", "xin24m", 0,
			RK3588_CLKGATE_CON(29), 11, GFLAGS),

	/* nvm */
	GATE(HCLK_EMMC, "hclk_emmc", "hclk_nvm_root", 0,
			RK3588_CLKGATE_CON(31), 4, GFLAGS),
	GATE(ACLK_EMMC, "aclk_emmc", "aclk_nvm_root", 0,
			RK3588_CLKGATE_CON(31), 5, GFLAGS),
	COMPOSITE(CCLK_EMMC, "cclk_emmc", gpll_cpll_24m_p, 0,
			RK3588_CLKSEL_CON(77), 14, 2, MFLAGS, 8, 6, DFLAGS,
			RK3588_CLKGATE_CON(31), 6, GFLAGS),
	COMPOSITE(BCLK_EMMC, "bclk_emmc", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(78), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(31), 7, GFLAGS),
	GATE(TMCLK_EMMC, "tmclk_emmc", "xin24m", 0,
			RK3588_CLKGATE_CON(31), 8, GFLAGS),

	COMPOSITE(SCLK_SFC, "sclk_sfc", gpll_cpll_24m_p, 0,
			RK3588_CLKSEL_CON(78), 12, 2, MFLAGS, 6, 6, DFLAGS,
			RK3588_CLKGATE_CON(31), 9, GFLAGS),
	GATE(HCLK_SFC, "hclk_sfc", "hclk_nvm_root", 0,
			RK3588_CLKGATE_CON(31), 10, GFLAGS),
	GATE(HCLK_SFC_XIP, "hclk_sfc_xip", "hclk_nvm_root", 0,
			RK3588_CLKGATE_CON(31), 11, GFLAGS),
	COMPOSITE_NODIV(HCLK_NVM_ROOT,  "hclk_nvm_root", mux_200m_100m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(77), 0, 2, MFLAGS,
			RK3588_CLKGATE_CON(31), 0, GFLAGS),
	COMPOSITE(ACLK_NVM_ROOT, "aclk_nvm_root", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(77), 7, 1, MFLAGS, 2, 5, DFLAGS,
			RK3588_CLKGATE_CON(31), 1, GFLAGS),

	/* php */
	COMPOSITE(CLK_GMAC0_PTP_REF, "clk_gmac0_ptp_ref", clk_gmac0_ptp_ref_p, 0,
			RK3588_CLKSEL_CON(81), 6, 1, MFLAGS, 0, 6, DFLAGS,
			RK3588_CLKGATE_CON(34), 10, GFLAGS),
	COMPOSITE(CLK_GMAC1_PTP_REF, "clk_gmac1_ptp_ref", clk_gmac1_ptp_ref_p, 0,
			RK3588_CLKSEL_CON(81), 13, 1, MFLAGS, 7, 6, DFLAGS,
			RK3588_CLKGATE_CON(34), 11, GFLAGS),
	COMPOSITE(CLK_GMAC_125M, "clk_gmac_125m", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(83), 15, 1, MFLAGS, 8, 7, DFLAGS,
			RK3588_CLKGATE_CON(35), 5, GFLAGS),
	COMPOSITE(CLK_GMAC_50M, "clk_gmac_50m", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(84), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RK3588_CLKGATE_CON(35), 6, GFLAGS),
	MUXGRF(CLK_GMAC0, "clk_gmac0", mux_clk_gmac_p, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			RK3588_PHPGRF_CLK_CON1, 0, 1, MFLAGS),
	MUXGRF(CLK_GMAC1, "clk_gmac1", mux_clk_gmac_p, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			RK3588_PHPGRF_CLK_CON1, 5, 1, MFLAGS),

	GATE(ACLK_PHP_GIC_ITS, "aclk_php_gic_its", "aclk_pcie_root", 0,
			RK3588_CLKGATE_CON(34), 6, GFLAGS),
	GATE(ACLK_MMU_PCIE, "aclk_mmu_pcie", "aclk_pcie_root", 0,
			RK3588_CLKGATE_CON(34), 7, GFLAGS),
	GATE(ACLK_MMU_PHP, "aclk_mmu_php", "aclk_php_root", 0,
			RK3588_CLKGATE_CON(34), 8, GFLAGS),
	GATE(ACLK_PCIE_4L_DBI, "aclk_pcie_4l_dbi", "aclk_php_root", 0,
			RK3588_CLKGATE_CON(32), 13, GFLAGS),
	GATE(ACLK_PCIE_2L_DBI, "aclk_pcie_2l_dbi", "aclk_php_root", 0,
			RK3588_CLKGATE_CON(32), 14, GFLAGS),
	GATE(ACLK_PCIE_1L0_DBI, "aclk_pcie_1l0_dbi", "aclk_php_root", 0,
			RK3588_CLKGATE_CON(32), 15, GFLAGS),
	GATE(ACLK_PCIE_1L1_DBI, "aclk_pcie_1l1_dbi", "aclk_php_root", 0,
			RK3588_CLKGATE_CON(33), 0, GFLAGS),
	GATE(ACLK_PCIE_1L2_DBI, "aclk_pcie_1l2_dbi", "aclk_php_root", 0,
			RK3588_CLKGATE_CON(33), 1, GFLAGS),
	GATE(ACLK_PCIE_4L_MSTR, "aclk_pcie_4l_mstr", "aclk_pcie_root", 0,
			RK3588_CLKGATE_CON(33), 2, GFLAGS),
	GATE(ACLK_PCIE_2L_MSTR, "aclk_pcie_2l_mstr", "aclk_pcie_root", 0,
			RK3588_CLKGATE_CON(33), 3, GFLAGS),
	GATE(ACLK_PCIE_1L0_MSTR, "aclk_pcie_1l0_mstr", "aclk_pcie_root", 0,
			RK3588_CLKGATE_CON(33), 4, GFLAGS),
	GATE(ACLK_PCIE_1L1_MSTR, "aclk_pcie_1l1_mstr", "aclk_pcie_root", 0,
			RK3588_CLKGATE_CON(33), 5, GFLAGS),
	GATE(ACLK_PCIE_1L2_MSTR, "aclk_pcie_1l2_mstr", "aclk_pcie_root", 0,
			RK3588_CLKGATE_CON(33), 6, GFLAGS),
	GATE(ACLK_PCIE_4L_SLV, "aclk_pcie_4l_slv", "aclk_php_root", 0,
			RK3588_CLKGATE_CON(33), 7, GFLAGS),
	GATE(ACLK_PCIE_2L_SLV, "aclk_pcie_2l_slv", "aclk_php_root", 0,
			RK3588_CLKGATE_CON(33), 8, GFLAGS),
	GATE(ACLK_PCIE_1L0_SLV, "aclk_pcie_1l0_slv", "aclk_php_root", 0,
			RK3588_CLKGATE_CON(33), 9, GFLAGS),
	GATE(ACLK_PCIE_1L1_SLV, "aclk_pcie_1l1_slv", "aclk_php_root", 0,
			RK3588_CLKGATE_CON(33), 10, GFLAGS),
	GATE(ACLK_PCIE_1L2_SLV, "aclk_pcie_1l2_slv", "aclk_php_root", 0,
			RK3588_CLKGATE_CON(33), 11, GFLAGS),
	GATE(PCLK_PCIE_4L, "pclk_pcie_4l", "pclk_php_root", 0,
			RK3588_CLKGATE_CON(33), 12, GFLAGS),
	GATE(PCLK_PCIE_2L, "pclk_pcie_2l", "pclk_php_root", 0,
			RK3588_CLKGATE_CON(33), 13, GFLAGS),
	GATE(PCLK_PCIE_1L0, "pclk_pcie_1l0", "pclk_php_root", 0,
			RK3588_CLKGATE_CON(33), 14, GFLAGS),
	GATE(PCLK_PCIE_1L1, "pclk_pcie_1l1", "pclk_php_root", 0,
			RK3588_CLKGATE_CON(33), 15, GFLAGS),
	GATE(PCLK_PCIE_1L2, "pclk_pcie_1l2", "pclk_php_root", 0,
			RK3588_CLKGATE_CON(34), 0, GFLAGS),
	GATE(CLK_PCIE_AUX0, "clk_pcie_aux0", "xin24m", 0,
			RK3588_CLKGATE_CON(34), 1, GFLAGS),
	GATE(CLK_PCIE_AUX1, "clk_pcie_aux1", "xin24m", 0,
			RK3588_CLKGATE_CON(34), 2, GFLAGS),
	GATE(CLK_PCIE_AUX2, "clk_pcie_aux2", "xin24m", 0,
			RK3588_CLKGATE_CON(34), 3, GFLAGS),
	GATE(CLK_PCIE_AUX3, "clk_pcie_aux3", "xin24m", 0,
			RK3588_CLKGATE_CON(34), 4, GFLAGS),
	GATE(CLK_PCIE_AUX4, "clk_pcie_aux4", "xin24m", 0,
			RK3588_CLKGATE_CON(34), 5, GFLAGS),
	GATE(CLK_PIPEPHY0_REF, "clk_pipephy0_ref", "xin24m", 0,
			RK3588_CLKGATE_CON(37), 0, GFLAGS),
	GATE(CLK_PIPEPHY1_REF, "clk_pipephy1_ref", "xin24m", 0,
			RK3588_CLKGATE_CON(37), 1, GFLAGS),
	GATE(CLK_PIPEPHY2_REF, "clk_pipephy2_ref", "xin24m", 0,
			RK3588_CLKGATE_CON(37), 2, GFLAGS),
	COMPOSITE_NODIV(PCLK_PHP_ROOT, "pclk_php_root", mux_150m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(80), 0, 2, MFLAGS,
			RK3588_CLKGATE_CON(32), 0, GFLAGS),
	GATE(PCLK_GMAC0, "pclk_gmac0", "pclk_php_root", 0,
			RK3588_CLKGATE_CON(32), 3, GFLAGS),
	GATE(PCLK_GMAC1, "pclk_gmac1", "pclk_php_root", 0,
			RK3588_CLKGATE_CON(32), 4, GFLAGS),
	COMPOSITE(ACLK_PCIE_ROOT, "aclk_pcie_root", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(80), 7, 1, MFLAGS, 2, 5, DFLAGS,
			RK3588_CLKGATE_CON(32), 6, GFLAGS),
	COMPOSITE(ACLK_PHP_ROOT, "aclk_php_root", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(80), 13, 1, MFLAGS, 8, 5, DFLAGS,
			RK3588_CLKGATE_CON(32), 7, GFLAGS),
	GATE(ACLK_PCIE_BRIDGE, "aclk_pcie_bridge", "aclk_pcie_root", 0,
			RK3588_CLKGATE_CON(32), 8, GFLAGS),
	GATE(ACLK_GMAC0, "aclk_gmac0", "aclk_php_root", 0,
			RK3588_CLKGATE_CON(32), 10, GFLAGS),
	GATE(ACLK_GMAC1, "aclk_gmac1", "aclk_php_root", 0,
			RK3588_CLKGATE_CON(32), 11, GFLAGS),
	GATE(CLK_PMALIVE0, "clk_pmalive0", "xin24m", 0,
			RK3588_CLKGATE_CON(37), 4, GFLAGS),
	GATE(CLK_PMALIVE1, "clk_pmalive1", "xin24m", 0,
			RK3588_CLKGATE_CON(37), 5, GFLAGS),
	GATE(CLK_PMALIVE2, "clk_pmalive2", "xin24m", 0,
			RK3588_CLKGATE_CON(37), 6, GFLAGS),
	GATE(ACLK_SATA0, "aclk_sata0", "aclk_php_root", 0,
			RK3588_CLKGATE_CON(37), 7, GFLAGS),
	GATE(ACLK_SATA1, "aclk_sata1", "aclk_php_root", 0,
			RK3588_CLKGATE_CON(37), 8, GFLAGS),
	GATE(ACLK_SATA2, "aclk_sata2", "aclk_php_root", 0,
			RK3588_CLKGATE_CON(37), 9, GFLAGS),
	COMPOSITE(CLK_RXOOB0, "clk_rxoob0", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(82), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RK3588_CLKGATE_CON(37), 10, GFLAGS),
	COMPOSITE(CLK_RXOOB1, "clk_rxoob1", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(82), 15, 1, MFLAGS, 8, 7, DFLAGS,
			RK3588_CLKGATE_CON(37), 11, GFLAGS),
	COMPOSITE(CLK_RXOOB2, "clk_rxoob2", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(83), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RK3588_CLKGATE_CON(37), 12, GFLAGS),
	GATE(ACLK_USB3OTG2, "aclk_usb3otg2", "aclk_php_root", 0,
			RK3588_CLKGATE_CON(35), 7, GFLAGS),
	GATE(SUSPEND_CLK_USB3OTG2, "suspend_clk_usb3otg2", "xin24m", 0,
			RK3588_CLKGATE_CON(35), 8, GFLAGS),
	GATE(REF_CLK_USB3OTG2, "ref_clk_usb3otg2", "xin24m", 0,
			RK3588_CLKGATE_CON(35), 9, GFLAGS),
	COMPOSITE(CLK_UTMI_OTG2, "clk_utmi_otg2", mux_150m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(84), 12, 2, MFLAGS, 8, 4, DFLAGS,
			RK3588_CLKGATE_CON(35), 10, GFLAGS),
	GATE(CLK_PIPEPHY0_PIPE_G, "clk_pipephy0_pipe_g", "clk_pipephy0_pipe_i", 0,
			RK3588_CLKGATE_CON(38), 3, GFLAGS),
	GATE(CLK_PIPEPHY1_PIPE_G, "clk_pipephy1_pipe_g", "clk_pipephy1_pipe_i", 0,
			RK3588_CLKGATE_CON(38), 4, GFLAGS),
	GATE(CLK_PIPEPHY2_PIPE_G, "clk_pipephy2_pipe_g", "clk_pipephy2_pipe_i", 0,
			RK3588_CLKGATE_CON(38), 5, GFLAGS),
	GATE(CLK_PIPEPHY0_PIPE_ASIC_G, "clk_pipephy0_pipe_asic_g", "clk_pipephy0_pipe_i", 0,
			RK3588_CLKGATE_CON(38), 6, GFLAGS),
	GATE(CLK_PIPEPHY1_PIPE_ASIC_G, "clk_pipephy1_pipe_asic_g", "clk_pipephy1_pipe_i", 0,
			RK3588_CLKGATE_CON(38), 7, GFLAGS),
	GATE(CLK_PIPEPHY2_PIPE_ASIC_G, "clk_pipephy2_pipe_asic_g", "clk_pipephy2_pipe_i", 0,
			RK3588_CLKGATE_CON(38), 8, GFLAGS),
	GATE(CLK_PIPEPHY2_PIPE_U3_G, "clk_pipephy2_pipe_u3_g", "clk_pipephy2_pipe_i", 0,
			RK3588_CLKGATE_CON(38), 9, GFLAGS),
	GATE(CLK_PCIE1L2_PIPE, "clk_pcie1l2_pipe", "clk_pipephy0_pipe_g", 0,
			RK3588_CLKGATE_CON(38), 13, GFLAGS),
	GATE(CLK_PCIE1L0_PIPE, "clk_pcie1l0_pipe", "clk_pipephy1_pipe_g", 0,
			RK3588_CLKGATE_CON(38), 14, GFLAGS),
	GATE(CLK_PCIE1L1_PIPE, "clk_pcie1l1_pipe", "clk_pipephy2_pipe_g", 0,
			RK3588_CLKGATE_CON(38), 15, GFLAGS),
	GATE(CLK_PCIE4L_PIPE, "clk_pcie4l_pipe", "clk_pipe30phy_pipe0_i", 0,
			RK3588_CLKGATE_CON(39), 0, GFLAGS),
	GATE(CLK_PCIE2L_PIPE, "clk_pcie2l_pipe", "clk_pipe30phy_pipe2_i", 0,
			RK3588_CLKGATE_CON(39), 1, GFLAGS),
	GATE(PCLK_PCIE_COMBO_PIPE_PHY0, "pclk_pcie_combo_pipe_phy0", "pclk_top_root", 0,
			RK3588_PHP_CLKGATE_CON(0), 5, GFLAGS),
	GATE(PCLK_PCIE_COMBO_PIPE_PHY1, "pclk_pcie_combo_pipe_phy1", "pclk_top_root", 0,
			RK3588_PHP_CLKGATE_CON(0), 6, GFLAGS),
	GATE(PCLK_PCIE_COMBO_PIPE_PHY2, "pclk_pcie_combo_pipe_phy2", "pclk_top_root", 0,
			RK3588_PHP_CLKGATE_CON(0), 7, GFLAGS),
	GATE(PCLK_PCIE_COMBO_PIPE_PHY, "pclk_pcie_combo_pipe_phy", "pclk_top_root", 0,
			RK3588_PHP_CLKGATE_CON(0), 8, GFLAGS),

	/* rga */
	GATE(HCLK_RGA3_1, "hclk_rga3_1", "hclk_rga3_root", 0,
			RK3588_CLKGATE_CON(76), 4, GFLAGS),
	GATE(ACLK_RGA3_1, "aclk_rga3_1", "aclk_rga3_root", 0,
			RK3588_CLKGATE_CON(76), 5, GFLAGS),
	COMPOSITE(CLK_RGA3_1_CORE, "clk_rga3_1_core", gpll_cpll_aupll_spll_p, 0,
			RK3588_CLKSEL_CON(174), 14, 2, MFLAGS, 9, 5, DFLAGS,
			RK3588_CLKGATE_CON(76), 6, GFLAGS),
	COMPOSITE(ACLK_RGA3_ROOT, "aclk_rga3_root", gpll_cpll_aupll_p, 0,
			RK3588_CLKSEL_CON(174), 5, 2, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(76), 0, GFLAGS),
	COMPOSITE_NODIV(HCLK_RGA3_ROOT, "hclk_rga3_root", mux_200m_100m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(174), 7, 2, MFLAGS,
			RK3588_CLKGATE_CON(76), 1, GFLAGS),

	/* vdec */
	COMPOSITE_NODIV(0, "hclk_rkvdec0_root", mux_200m_100m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(89), 0, 2, MFLAGS,
			RK3588_CLKGATE_CON(40), 0, GFLAGS),
	COMPOSITE(0, "aclk_rkvdec0_root", gpll_cpll_aupll_spll_p, 0,
			RK3588_CLKSEL_CON(89), 7, 2, MFLAGS, 2, 5, DFLAGS,
			RK3588_CLKGATE_CON(40), 1, GFLAGS),
	COMPOSITE(ACLK_RKVDEC_CCU, "aclk_rkvdec_ccu", gpll_cpll_aupll_spll_p, 0,
			RK3588_CLKSEL_CON(89), 14, 2, MFLAGS, 9, 5, DFLAGS,
			RK3588_CLKGATE_CON(40), 2, GFLAGS),
	GATE(HCLK_RKVDEC0, "hclk_rkvdec0", "hclk_rkvdec0_root", 0,
			RK3588_CLKGATE_CON(40), 3, GFLAGS),
	GATE(ACLK_RKVDEC0, "aclk_rkvdec0", "aclk_rkvdec0_root", 0,
			RK3588_CLKGATE_CON(40), 4, GFLAGS),
	COMPOSITE(CLK_RKVDEC0_CA, "clk_rkvdec0_ca", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(90), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(40), 7, GFLAGS),
	COMPOSITE(CLK_RKVDEC0_HEVC_CA, "clk_rkvdec0_hevc_ca", gpll_cpll_npll_1000m_p, 0,
			RK3588_CLKSEL_CON(90), 11, 2, MFLAGS, 6, 5, DFLAGS,
			RK3588_CLKGATE_CON(40), 8, GFLAGS),
	COMPOSITE(CLK_RKVDEC0_CORE, "clk_rkvdec0_core", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(91), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(40), 9, GFLAGS),
	COMPOSITE_NODIV(0, "hclk_rkvdec1_root", mux_200m_100m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(93), 0, 2, MFLAGS,
			RK3588_CLKGATE_CON(41), 0, GFLAGS),
	COMPOSITE(0, "aclk_rkvdec1_root", gpll_cpll_aupll_npll_p, 0,
			RK3588_CLKSEL_CON(93), 7, 2, MFLAGS, 2, 5, DFLAGS,
			RK3588_CLKGATE_CON(41), 1, GFLAGS),
	GATE(HCLK_RKVDEC1, "hclk_rkvdec1", "hclk_rkvdec1_root", 0,
			RK3588_CLKGATE_CON(41), 2, GFLAGS),
	GATE(ACLK_RKVDEC1, "aclk_rkvdec1", "aclk_rkvdec1_root", 0,
			RK3588_CLKGATE_CON(41), 3, GFLAGS),
	COMPOSITE(CLK_RKVDEC1_CA, "clk_rkvdec1_ca", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(93), 14, 1, MFLAGS, 9, 5, DFLAGS,
			RK3588_CLKGATE_CON(41), 6, GFLAGS),
	COMPOSITE(CLK_RKVDEC1_HEVC_CA, "clk_rkvdec1_hevc_ca", gpll_cpll_npll_1000m_p, 0,
			RK3588_CLKSEL_CON(94), 5, 2, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(41), 7, GFLAGS),
	COMPOSITE(CLK_RKVDEC1_CORE, "clk_rkvdec1_core", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(94), 12, 1, MFLAGS, 7, 5, DFLAGS,
			RK3588_CLKGATE_CON(41), 8, GFLAGS),

	/* sdio */
	COMPOSITE_NODIV(0, "hclk_sdio_root", mux_200m_100m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(172), 0, 2, MFLAGS,
			RK3588_CLKGATE_CON(75), 0, GFLAGS),
	GATE(HCLK_SDIO, "hclk_sdio", "hclk_sdio_root", 0,
			RK3588_CLKGATE_CON(75), 2, GFLAGS),
	COMPOSITE(CCLK_SRC_SDIO, "cclk_src_sdio", gpll_cpll_24m_p, 0,
			RK3588_CLKSEL_CON(172), 8, 2, MFLAGS, 2, 6, DFLAGS,
			RK3588_CLKGATE_CON(75), 3, GFLAGS),
	MMC(SCLK_SDIO_DRV, "sdio_drv", "cclk_src_sdio", RK3588_SDIO_CON0, 1),
	MMC(SCLK_SDIO_SAMPLE, "sdio_sample", "cclk_src_sdio", RK3588_SDIO_CON1, 1),

	MMC(SCLK_SDMMC_DRV, "sdmmc_drv", "scmi_cclk_sd", RK3588_SDMMC_CON0, 1),
	MMC(SCLK_SDMMC_SAMPLE, "sdmmc_sample", "scmi_cclk_sd", RK3588_SDMMC_CON1, 1),

	/* usb */
	COMPOSITE(ACLK_USB_ROOT, "aclk_usb_root", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(96), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(42), 0, GFLAGS),
	COMPOSITE_NODIV(HCLK_USB_ROOT, "hclk_usb_root", mux_150m_100m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(96), 6, 2, MFLAGS,
			RK3588_CLKGATE_CON(42), 1, GFLAGS),
	GATE(HCLK_HOST0, "hclk_host0", "hclk_usb_root", 0,
			RK3588_CLKGATE_CON(42), 10, GFLAGS),
	GATE(HCLK_HOST_ARB0, "hclk_host_arb0", "hclk_usb_root", 0,
			RK3588_CLKGATE_CON(42), 11, GFLAGS),
	GATE(HCLK_HOST1, "hclk_host1", "hclk_usb_root", 0,
			RK3588_CLKGATE_CON(42), 12, GFLAGS),
	GATE(HCLK_HOST_ARB1, "hclk_host_arb1", "hclk_usb_root", 0,
			RK3588_CLKGATE_CON(42), 13, GFLAGS),
	GATE(ACLK_USB3OTG0, "aclk_usb3otg0", "aclk_usb_root", 0,
			RK3588_CLKGATE_CON(42), 4, GFLAGS),
	GATE(SUSPEND_CLK_USB3OTG0, "suspend_clk_usb3otg0", "xin24m", 0,
			RK3588_CLKGATE_CON(42), 5, GFLAGS),
	GATE(REF_CLK_USB3OTG0, "ref_clk_usb3otg0", "xin24m", 0,
			RK3588_CLKGATE_CON(42), 6, GFLAGS),
	GATE(ACLK_USB3OTG1, "aclk_usb3otg1", "aclk_usb_root", 0,
			RK3588_CLKGATE_CON(42), 7, GFLAGS),
	GATE(SUSPEND_CLK_USB3OTG1, "suspend_clk_usb3otg1", "xin24m", 0,
			RK3588_CLKGATE_CON(42), 8, GFLAGS),
	GATE(REF_CLK_USB3OTG1, "ref_clk_usb3otg1", "xin24m", 0,
			RK3588_CLKGATE_CON(42), 9, GFLAGS),

	/* vdpu */
	GATE(HCLK_IEP2P0, "hclk_iep2p0", "hclk_vdpu_root", 0,
			RK3588_CLKGATE_CON(45), 4, GFLAGS),
	GATE(ACLK_IEP2P0, "aclk_iep2p0", "aclk_vdpu_low_root", 0,
			RK3588_CLKGATE_CON(45), 5, GFLAGS),
	COMPOSITE(CLK_IEP2P0_CORE, "clk_iep2p0_core", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(99), 12, 1, MFLAGS, 7, 5, DFLAGS,
			RK3588_CLKGATE_CON(45), 6, GFLAGS),
	GATE(ACLK_JPEG_ENCODER0, "aclk_jpeg_encoder0", "aclk_vdpu_low_root", 0,
			RK3588_CLKGATE_CON(44), 10, GFLAGS),
	GATE(HCLK_JPEG_ENCODER0, "hclk_jpeg_encoder0", "hclk_vdpu_root", 0,
			RK3588_CLKGATE_CON(44), 11, GFLAGS),
	GATE(ACLK_JPEG_ENCODER1, "aclk_jpeg_encoder1", "aclk_vdpu_low_root", 0,
			RK3588_CLKGATE_CON(44), 12, GFLAGS),
	GATE(HCLK_JPEG_ENCODER1, "hclk_jpeg_encoder1", "hclk_vdpu_root", 0,
			RK3588_CLKGATE_CON(44), 13, GFLAGS),
	GATE(ACLK_JPEG_ENCODER2, "aclk_jpeg_encoder2", "aclk_vdpu_low_root", 0,
			RK3588_CLKGATE_CON(44), 14, GFLAGS),
	GATE(HCLK_JPEG_ENCODER2, "hclk_jpeg_encoder2", "hclk_vdpu_root", 0,
			RK3588_CLKGATE_CON(44), 15, GFLAGS),
	GATE(ACLK_JPEG_ENCODER3, "aclk_jpeg_encoder3", "aclk_vdpu_low_root", 0,
			RK3588_CLKGATE_CON(45), 0, GFLAGS),
	GATE(HCLK_JPEG_ENCODER3, "hclk_jpeg_encoder3", "hclk_vdpu_root", 0,
			RK3588_CLKGATE_CON(45), 1, GFLAGS),
	GATE(ACLK_JPEG_DECODER, "aclk_jpeg_decoder", "aclk_jpeg_decoder_root", 0,
			RK3588_CLKGATE_CON(45), 2, GFLAGS),
	GATE(HCLK_JPEG_DECODER, "hclk_jpeg_decoder", "hclk_vdpu_root", 0,
			RK3588_CLKGATE_CON(45), 3, GFLAGS),
	GATE(HCLK_RGA2, "hclk_rga2", "hclk_vdpu_root", 0,
			RK3588_CLKGATE_CON(45), 7, GFLAGS),
	GATE(ACLK_RGA2, "aclk_rga2", "aclk_vdpu_root", 0,
			RK3588_CLKGATE_CON(45), 8, GFLAGS),
	COMPOSITE(CLK_RGA2_CORE, "clk_rga2_core", gpll_cpll_npll_aupll_spll_p, 0,
			RK3588_CLKSEL_CON(100), 5, 3, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(45), 9, GFLAGS),
	GATE(HCLK_RGA3_0, "hclk_rga3_0", "hclk_vdpu_root", 0,
			RK3588_CLKGATE_CON(45), 10, GFLAGS),
	GATE(ACLK_RGA3_0, "aclk_rga3_0", "aclk_vdpu_root", 0,
			RK3588_CLKGATE_CON(45), 11, GFLAGS),
	COMPOSITE(CLK_RGA3_0_CORE, "clk_rga3_0_core", gpll_cpll_npll_aupll_spll_p, 0,
			RK3588_CLKSEL_CON(100), 13, 3, MFLAGS, 8, 5, DFLAGS,
			RK3588_CLKGATE_CON(45), 12, GFLAGS),
	COMPOSITE(ACLK_VDPU_ROOT, "aclk_vdpu_root", gpll_cpll_aupll_p, 0,
			RK3588_CLKSEL_CON(98), 5, 2, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(44), 0, GFLAGS),
	COMPOSITE_NODIV(ACLK_VDPU_LOW_ROOT, "aclk_vdpu_low_root", mux_400m_200m_100m_24m_p, 0,
			RK3588_CLKSEL_CON(98), 7, 2, MFLAGS,
			RK3588_CLKGATE_CON(44), 1, GFLAGS),
	COMPOSITE_NODIV(HCLK_VDPU_ROOT, "hclk_vdpu_root", mux_200m_100m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(98), 9, 2, MFLAGS,
			RK3588_CLKGATE_CON(44), 2, GFLAGS),
	COMPOSITE(ACLK_JPEG_DECODER_ROOT, "aclk_jpeg_decoder_root", gpll_cpll_aupll_spll_p, 0,
			RK3588_CLKSEL_CON(99), 5, 2, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(44), 3, GFLAGS),
	GATE(ACLK_VPU, "aclk_vpu", "aclk_vdpu_low_root", 0,
			RK3588_CLKGATE_CON(44), 8, GFLAGS),
	GATE(HCLK_VPU, "hclk_vpu", "hclk_vdpu_root", 0,
			RK3588_CLKGATE_CON(44), 9, GFLAGS),

	/* venc */
	COMPOSITE_NODIV(HCLK_RKVENC0_ROOT, "hclk_rkvenc0_root", mux_200m_100m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(102), 0, 2, MFLAGS,
			RK3588_CLKGATE_CON(47), 0, GFLAGS),
	COMPOSITE(ACLK_RKVENC0_ROOT, "aclk_rkvenc0_root", gpll_cpll_npll_p, 0,
			RK3588_CLKSEL_CON(102), 7, 2, MFLAGS, 2, 5, DFLAGS,
			RK3588_CLKGATE_CON(47), 1, GFLAGS),
	GATE(HCLK_RKVENC0, "hclk_rkvenc0", "hclk_rkvenc0_root", 0,
			RK3588_CLKGATE_CON(47), 4, GFLAGS),
	GATE(ACLK_RKVENC0, "aclk_rkvenc0", "aclk_rkvenc0_root", 0,
			RK3588_CLKGATE_CON(47), 5, GFLAGS),
	COMPOSITE(CLK_RKVENC0_CORE, "clk_rkvenc0_core", gpll_cpll_aupll_npll_p, 0,
			RK3588_CLKSEL_CON(102), 14, 2, MFLAGS, 9, 5, DFLAGS,
			RK3588_CLKGATE_CON(47), 6, GFLAGS),
	COMPOSITE_NODIV(HCLK_RKVENC1_ROOT, "hclk_rkvenc1_root", mux_200m_100m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(104), 0, 2, MFLAGS,
			RK3588_CLKGATE_CON(48), 0, GFLAGS),
	COMPOSITE(ACLK_RKVENC1_ROOT, "aclk_rkvenc1_root", gpll_cpll_npll_p, 0,
			RK3588_CLKSEL_CON(104), 7, 2, MFLAGS, 2, 5, DFLAGS,
			RK3588_CLKGATE_CON(48), 1, GFLAGS),
	GATE(HCLK_RKVENC1, "hclk_rkvenc1", "hclk_rkvenc1_root", 0,
			RK3588_CLKGATE_CON(48), 4, GFLAGS),
	GATE(ACLK_RKVENC1, "aclk_rkvenc1", "aclk_rkvenc1_root", 0,
			RK3588_CLKGATE_CON(48), 5, GFLAGS),
	COMPOSITE(CLK_RKVENC1_CORE, "clk_rkvenc1_core", gpll_cpll_aupll_npll_p, 0,
			RK3588_CLKSEL_CON(104), 14, 2, MFLAGS, 9, 5, DFLAGS,
			RK3588_CLKGATE_CON(48), 6, GFLAGS),

	/* vi */
	COMPOSITE_NODIV(ICLK_CSIHOST01, "iclk_csihost01", mux_400m_200m_100m_24m_p, 0,
			RK3588_CLKSEL_CON(108), 14, 2, MFLAGS,
			RK3588_CLKGATE_CON(51), 10, GFLAGS),
	GATE(ICLK_CSIHOST0, "iclk_csihost0", "iclk_csihost01", 0,
			RK3588_CLKGATE_CON(51), 11, GFLAGS),
	GATE(ICLK_CSIHOST1, "iclk_csihost1", "iclk_csihost01", 0,
			RK3588_CLKGATE_CON(51), 12, GFLAGS),
	GATE(PCLK_CSI_HOST_0, "pclk_csi_host_0", "pclk_vi_root", 0,
			RK3588_CLKGATE_CON(50), 4, GFLAGS),
	GATE(PCLK_CSI_HOST_1, "pclk_csi_host_1", "pclk_vi_root", 0,
			RK3588_CLKGATE_CON(50), 5, GFLAGS),
	GATE(PCLK_CSI_HOST_2, "pclk_csi_host_2", "pclk_vi_root", 0,
			RK3588_CLKGATE_CON(50), 6, GFLAGS),
	GATE(PCLK_CSI_HOST_3, "pclk_csi_host_3", "pclk_vi_root", 0,
			RK3588_CLKGATE_CON(50), 7, GFLAGS),
	GATE(PCLK_CSI_HOST_4, "pclk_csi_host_4", "pclk_vi_root", 0,
			RK3588_CLKGATE_CON(50), 8, GFLAGS),
	GATE(PCLK_CSI_HOST_5, "pclk_csi_host_5", "pclk_vi_root", 0,
			RK3588_CLKGATE_CON(50), 9, GFLAGS),
	GATE(ACLK_FISHEYE0, "aclk_fisheye0", "aclk_vi_root", 0,
			RK3588_CLKGATE_CON(49), 14, GFLAGS),
	GATE(HCLK_FISHEYE0, "hclk_fisheye0", "hclk_vi_root", 0,
			RK3588_CLKGATE_CON(49), 15, GFLAGS),
	COMPOSITE(CLK_FISHEYE0_CORE, "clk_fisheye0_core", gpll_cpll_aupll_spll_p, 0,
			RK3588_CLKSEL_CON(108), 5, 2, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(50), 0, GFLAGS),
	GATE(ACLK_FISHEYE1, "aclk_fisheye1", "aclk_vi_root", 0,
			RK3588_CLKGATE_CON(50), 1, GFLAGS),
	GATE(HCLK_FISHEYE1, "hclk_fisheye1", "hclk_vi_root", 0,
			RK3588_CLKGATE_CON(50), 2, GFLAGS),
	COMPOSITE(CLK_FISHEYE1_CORE, "clk_fisheye1_core", gpll_cpll_aupll_spll_p, 0,
			RK3588_CLKSEL_CON(108), 12, 2, MFLAGS, 7, 5, DFLAGS,
			RK3588_CLKGATE_CON(50), 3, GFLAGS),
	COMPOSITE(CLK_ISP0_CORE, "clk_isp0_core", gpll_cpll_aupll_spll_p, 0,
			RK3588_CLKSEL_CON(107), 11, 2, MFLAGS, 6, 5, DFLAGS,
			RK3588_CLKGATE_CON(49), 9, GFLAGS),
	GATE(CLK_ISP0_CORE_MARVIN, "clk_isp0_core_marvin", "clk_isp0_core", 0,
			RK3588_CLKGATE_CON(49), 10, GFLAGS),
	GATE(CLK_ISP0_CORE_VICAP, "clk_isp0_core_vicap", "clk_isp0_core", 0,
			RK3588_CLKGATE_CON(49), 11, GFLAGS),
	GATE(ACLK_ISP0, "aclk_isp0", "aclk_vi_root", 0,
			RK3588_CLKGATE_CON(49), 12, GFLAGS),
	GATE(HCLK_ISP0, "hclk_isp0", "hclk_vi_root", 0,
			RK3588_CLKGATE_CON(49), 13, GFLAGS),
	COMPOSITE(ACLK_VI_ROOT, "aclk_vi_root", gpll_cpll_npll_aupll_spll_p, 0,
			RK3588_CLKSEL_CON(106), 5, 3, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(49), 0, GFLAGS),
	COMPOSITE_NODIV(HCLK_VI_ROOT, "hclk_vi_root", mux_200m_100m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(106), 8, 2, MFLAGS,
			RK3588_CLKGATE_CON(49), 1, GFLAGS),
	COMPOSITE_NODIV(PCLK_VI_ROOT, "pclk_vi_root", mux_100m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(106), 10, 2, MFLAGS,
			RK3588_CLKGATE_CON(49), 2, GFLAGS),
	COMPOSITE(DCLK_VICAP, "dclk_vicap", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(107), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(49), 6, GFLAGS),
	GATE(ACLK_VICAP, "aclk_vicap", "aclk_vi_root", 0,
			RK3588_CLKGATE_CON(49), 7, GFLAGS),
	GATE(HCLK_VICAP, "hclk_vicap", "hclk_vi_root", 0,
			RK3588_CLKGATE_CON(49), 8, GFLAGS),

	/* vo0 */
	GATE(PCLK_DP0, "pclk_dp0", "pclk_vo0_root", 0,
			RK3588_CLKGATE_CON(56), 4, GFLAGS),
	GATE(PCLK_DP1, "pclk_dp1", "pclk_vo0_root", 0,
			RK3588_CLKGATE_CON(56), 5, GFLAGS),
	GATE(PCLK_S_DP0, "pclk_s_dp0", "pclk_vo0_s_root", 0,
			RK3588_CLKGATE_CON(56), 6, GFLAGS),
	GATE(PCLK_S_DP1, "pclk_s_dp1", "pclk_vo0_s_root", 0,
			RK3588_CLKGATE_CON(56), 7, GFLAGS),
	GATE(CLK_DP0, "clk_dp0", "aclk_vo0_root", 0,
			RK3588_CLKGATE_CON(56), 8, GFLAGS),
	GATE(CLK_DP1, "clk_dp1", "aclk_vo0_root", 0,
			RK3588_CLKGATE_CON(56), 9, GFLAGS),
	GATE(HCLK_HDCP_KEY0, "hclk_hdcp_key0", "hclk_vo0_s_root", 0,
			RK3588_CLKGATE_CON(55), 11, GFLAGS),
	GATE(ACLK_HDCP0, "aclk_hdcp0", "aclk_vo0_root", 0,
			RK3588_CLKGATE_CON(55), 12, GFLAGS),
	GATE(HCLK_HDCP0, "hclk_hdcp0", "hclk_vo0_root", 0,
			RK3588_CLKGATE_CON(55), 13, GFLAGS),
	GATE(PCLK_HDCP0, "pclk_hdcp0", "pclk_vo0_root", 0,
			RK3588_CLKGATE_CON(55), 14, GFLAGS),
	GATE(HCLK_I2S4_8CH, "hclk_i2s4_8ch", "hclk_vo0_root", 0,
			RK3588_CLKGATE_CON(56), 10, GFLAGS),
	GATE(ACLK_TRNG0, "aclk_trng0", "aclk_vo0_root", 0,
			RK3588_CLKGATE_CON(56), 0, GFLAGS),
	GATE(PCLK_TRNG0, "pclk_trng0", "pclk_vo0_root", 0,
			RK3588_CLKGATE_CON(56), 1, GFLAGS),
	COMPOSITE(ACLK_VO0_ROOT, "aclk_vo0_root", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(116), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(55), 0, GFLAGS),
	COMPOSITE_NODIV(HCLK_VO0_ROOT, "hclk_vo0_root", mux_200m_100m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(116), 6, 2, MFLAGS,
			RK3588_CLKGATE_CON(55), 1, GFLAGS),
	COMPOSITE_NODIV(HCLK_VO0_S_ROOT, "hclk_vo0_s_root", mux_200m_100m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(116), 8, 2, MFLAGS,
			RK3588_CLKGATE_CON(55), 2, GFLAGS),
	COMPOSITE_NODIV(PCLK_VO0_ROOT, "pclk_vo0_root", mux_100m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(116), 10, 2, MFLAGS,
			RK3588_CLKGATE_CON(55), 3, GFLAGS),
	COMPOSITE_NODIV(PCLK_VO0_S_ROOT, "pclk_vo0_s_root", mux_100m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(116), 12, 2, MFLAGS,
			RK3588_CLKGATE_CON(55), 4, GFLAGS),
	GATE(PCLK_VO0GRF, "pclk_vo0grf", "pclk_vo0_root", CLK_IGNORE_UNUSED,
			RK3588_CLKGATE_CON(55), 10, GFLAGS),
	COMPOSITE(CLK_I2S4_8CH_TX_SRC, "clk_i2s4_8ch_tx_src", gpll_aupll_p, 0,
			RK3588_CLKSEL_CON(118), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(56), 11, GFLAGS),
	COMPOSITE_FRACMUX(CLK_I2S4_8CH_TX_FRAC, "clk_i2s4_8ch_tx_frac", "clk_i2s4_8ch_tx_src", CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(119), 0,
			RK3588_CLKGATE_CON(56), 12, GFLAGS,
			&rk3588_i2s4_8ch_tx_fracmux, RK3588_FRAC_MAX_PRATE),
	GATE(MCLK_I2S4_8CH_TX, "mclk_i2s4_8ch_tx", "clk_i2s4_8ch_tx", 0,
			RK3588_CLKGATE_CON(56), 13, GFLAGS),
	GATE(HCLK_I2S8_8CH, "hclk_i2s8_8ch", "hclk_vo0_root", 0,
			RK3588_CLKGATE_CON(56), 14, GFLAGS),
	COMPOSITE(CLK_I2S8_8CH_TX_SRC, "clk_i2s8_8ch_tx_src", gpll_aupll_p, 0,
			RK3588_CLKSEL_CON(120), 8, 1, MFLAGS, 3, 5, DFLAGS,
			RK3588_CLKGATE_CON(56), 15, GFLAGS),
	COMPOSITE_FRACMUX(CLK_I2S8_8CH_TX_FRAC, "clk_i2s8_8ch_tx_frac", "clk_i2s8_8ch_tx_src", CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(121), 0,
			RK3588_CLKGATE_CON(57), 0, GFLAGS,
			&rk3588_i2s8_8ch_tx_fracmux, RK3588_FRAC_MAX_PRATE),
	GATE(MCLK_I2S8_8CH_TX, "mclk_i2s8_8ch_tx", "clk_i2s8_8ch_tx", 0,
			RK3588_CLKGATE_CON(57), 1, GFLAGS),
	GATE(HCLK_SPDIF2_DP0, "hclk_spdif2_dp0", "hclk_vo0_root", 0,
			RK3588_CLKGATE_CON(57), 2, GFLAGS),
	COMPOSITE(CLK_SPDIF2_DP0_SRC, "clk_spdif2_dp0_src", gpll_aupll_p, 0,
			RK3588_CLKSEL_CON(122), 8, 1, MFLAGS, 3, 5, DFLAGS,
			RK3588_CLKGATE_CON(57), 3, GFLAGS),
	COMPOSITE_FRACMUX(CLK_SPDIF2_DP0_FRAC, "clk_spdif2_dp0_frac", "clk_spdif2_dp0_src", CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(123), 0,
			RK3588_CLKGATE_CON(57), 4, GFLAGS,
			&rk3588_spdif2_dp0_fracmux, RK3588_FRAC_MAX_PRATE),
	GATE(MCLK_SPDIF2_DP0, "mclk_spdif2_dp0", "clk_spdif2_dp0", 0,
			RK3588_CLKGATE_CON(57), 5, GFLAGS),
	GATE(MCLK_SPDIF2, "mclk_spdif2", "clk_spdif2_dp0", 0,
			RK3588_CLKGATE_CON(57), 6, GFLAGS),
	GATE(HCLK_SPDIF5_DP1, "hclk_spdif5_dp1", "hclk_vo0_root", 0,
			RK3588_CLKGATE_CON(57), 7, GFLAGS),
	COMPOSITE(CLK_SPDIF5_DP1_SRC, "clk_spdif5_dp1_src", gpll_aupll_p, 0,
			RK3588_CLKSEL_CON(124), 7, 1, MFLAGS, 2, 5, DFLAGS,
			RK3588_CLKGATE_CON(57), 8, GFLAGS),
	COMPOSITE_FRACMUX(CLK_SPDIF5_DP1_FRAC, "clk_spdif5_dp1_frac", "clk_spdif5_dp1_src", CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(125), 0,
			RK3588_CLKGATE_CON(57), 9, GFLAGS,
			&rk3588_spdif5_dp1_fracmux, RK3588_FRAC_MAX_PRATE),
	GATE(MCLK_SPDIF5_DP1, "mclk_spdif5_dp1", "clk_spdif5_dp1", 0,
			RK3588_CLKGATE_CON(57), 10, GFLAGS),
	GATE(MCLK_SPDIF5, "mclk_spdif5", "clk_spdif5_dp1", 0,
			RK3588_CLKGATE_CON(57), 11, GFLAGS),

	/* vo1 */
	GATE(PCLK_EDP0, "pclk_edp0", "pclk_vo1_root", 0,
			RK3588_CLKGATE_CON(62), 0, GFLAGS),
	GATE(CLK_EDP0_24M, "clk_edp0_24m", "xin24m", 0,
			RK3588_CLKGATE_CON(62), 1, GFLAGS),
	COMPOSITE_NODIV(CLK_EDP0_200M, "clk_edp0_200m", mux_200m_100m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(140), 1, 2, MFLAGS,
			RK3588_CLKGATE_CON(62), 2, GFLAGS),
	GATE(PCLK_EDP1, "pclk_edp1", "pclk_vo1_root", 0,
			RK3588_CLKGATE_CON(62), 3, GFLAGS),
	GATE(CLK_EDP1_24M, "clk_edp1_24m", "xin24m", 0,
			RK3588_CLKGATE_CON(62), 4, GFLAGS),
	COMPOSITE_NODIV(CLK_EDP1_200M, "clk_edp1_200m", mux_200m_100m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(140), 3, 2, MFLAGS,
			RK3588_CLKGATE_CON(62), 5, GFLAGS),
	GATE(HCLK_HDCP_KEY1, "hclk_hdcp_key1", "hclk_vo1_s_root", 0,
			RK3588_CLKGATE_CON(60), 4, GFLAGS),
	GATE(ACLK_HDCP1, "aclk_hdcp1", "aclk_hdcp1_root", 0,
			RK3588_CLKGATE_CON(60), 5, GFLAGS),
	GATE(HCLK_HDCP1, "hclk_hdcp1", "hclk_vo1_root", 0,
			RK3588_CLKGATE_CON(60), 6, GFLAGS),
	GATE(PCLK_HDCP1, "pclk_hdcp1", "pclk_vo1_root", 0,
			RK3588_CLKGATE_CON(60), 7, GFLAGS),
	GATE(ACLK_HDMIRX, "aclk_hdmirx", "aclk_hdmirx_root", 0,
			RK3588_CLKGATE_CON(61), 9, GFLAGS),
	GATE(PCLK_HDMIRX, "pclk_hdmirx", "pclk_vo1_root", 0,
			RK3588_CLKGATE_CON(61), 10, GFLAGS),
	GATE(CLK_HDMIRX_REF, "clk_hdmirx_ref", "aclk_hdcp1_root", 0,
			RK3588_CLKGATE_CON(61), 11, GFLAGS),
	COMPOSITE(CLK_HDMIRX_AUD_SRC, "clk_hdmirx_aud_src", gpll_aupll_p, 0,
			RK3588_CLKSEL_CON(138), 8, 1, MFLAGS, 0, 8, DFLAGS,
			RK3588_CLKGATE_CON(61), 12, GFLAGS),
	COMPOSITE_FRACMUX(CLK_HDMIRX_AUD_FRAC, "clk_hdmirx_aud_frac", "clk_hdmirx_aud_src", CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(139), 0,
			RK3588_CLKGATE_CON(61), 13, GFLAGS,
			&rk3588_hdmirx_aud_fracmux, RK3588_FRAC_MAX_PRATE),
	GATE(CLK_HDMIRX_AUD, "clk_hdmirx_aud", "clk_hdmirx_aud_mux", 0,
			RK3588_CLKGATE_CON(61), 14, GFLAGS),
	GATE(PCLK_HDMITX0, "pclk_hdmitx0", "pclk_vo1_root", 0,
			RK3588_CLKGATE_CON(60), 11, GFLAGS),
	COMPOSITE(CLK_HDMITX0_EARC, "clk_hdmitx0_earc", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(133), 6, 1, MFLAGS, 1, 5, DFLAGS,
			RK3588_CLKGATE_CON(60), 15, GFLAGS),
	GATE(CLK_HDMITX0_REF, "clk_hdmitx0_ref", "aclk_hdcp1_root", 0,
			RK3588_CLKGATE_CON(61), 0, GFLAGS),
	GATE(PCLK_HDMITX1, "pclk_hdmitx1", "pclk_vo1_root", 0,
			RK3588_CLKGATE_CON(61), 2, GFLAGS),
	COMPOSITE(CLK_HDMITX1_EARC, "clk_hdmitx1_earc", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(136), 6, 1, MFLAGS, 1, 5, DFLAGS,
			RK3588_CLKGATE_CON(61), 6, GFLAGS),
	GATE(CLK_HDMITX1_REF, "clk_hdmitx1_ref", "aclk_hdcp1_root", 0,
			RK3588_CLKGATE_CON(61), 7, GFLAGS),
	COMPOSITE_HALFDIV(CLK_HDMITRX_REFSRC, "clk_hdmitrx_refsrc", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(157), 7, 1, MFLAGS, 2, 5, DFLAGS,
			RK3588_CLKGATE_CON(65), 9, GFLAGS),
	GATE(ACLK_TRNG1, "aclk_trng1", "aclk_hdcp1_root", 0,
			RK3588_CLKGATE_CON(60), 9, GFLAGS),
	GATE(PCLK_TRNG1, "pclk_trng1", "pclk_vo1_root", 0,
			RK3588_CLKGATE_CON(60), 10, GFLAGS),
	COMPOSITE(ACLK_HDCP1_ROOT, "aclk_hdcp1_root", aclk_hdcp1_root_p, 0,
			RK3588_CLKSEL_CON(128), 5, 2, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(59), 0, GFLAGS),
	COMPOSITE(ACLK_HDMIRX_ROOT, "aclk_hdmirx_root", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(128), 12, 1, MFLAGS, 7, 5, DFLAGS,
			RK3588_CLKGATE_CON(59), 1, GFLAGS),
	COMPOSITE_NODIV(HCLK_VO1_ROOT, "hclk_vo1_root", mux_200m_100m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(128), 13, 2, MFLAGS,
			RK3588_CLKGATE_CON(59), 2, GFLAGS),
	COMPOSITE_NODIV(HCLK_VO1_S_ROOT, "hclk_vo1_s_root", mux_200m_100m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(129), 0, 2, MFLAGS,
			RK3588_CLKGATE_CON(59), 3, GFLAGS),
	COMPOSITE_NODIV(PCLK_VO1_ROOT, "pclk_vo1_root", mux_150m_100m_24m_p, 0,
			RK3588_CLKSEL_CON(129), 2, 2, MFLAGS,
			RK3588_CLKGATE_CON(59), 4, GFLAGS),
	COMPOSITE_NODIV(PCLK_VO1_S_ROOT, "pclk_vo1_s_root", mux_100m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(129), 4, 2, MFLAGS,
			RK3588_CLKGATE_CON(59), 5, GFLAGS),
	GATE(0, "pclk_vo1grf", "pclk_vo1_root", CLK_IGNORE_UNUSED,
			RK3588_CLKGATE_CON(59), 12, GFLAGS),
	GATE(PCLK_S_EDP0, "pclk_s_edp0", "pclk_vo1_s_root", 0,
			RK3588_CLKGATE_CON(59), 14, GFLAGS),
	GATE(PCLK_S_EDP1, "pclk_s_edp1", "pclk_vo1_s_root", 0,
			RK3588_CLKGATE_CON(59), 15, GFLAGS),
	GATE(PCLK_S_HDMIRX, "pclk_s_hdmirx", "pclk_vo1_s_root", 0,
			RK3588_CLKGATE_CON(65), 8, GFLAGS),
	GATE(HCLK_I2S10_8CH, "hclk_i2s10_8ch", "hclk_vo1_root", 0,
			RK3588_CLKGATE_CON(65), 4, GFLAGS),
	COMPOSITE(CLK_I2S10_8CH_RX_SRC, "clk_i2s10_8ch_rx_src", gpll_aupll_p, 0,
			RK3588_CLKSEL_CON(155), 8, 1, MFLAGS, 3, 5, DFLAGS,
			RK3588_CLKGATE_CON(65), 5, GFLAGS),
	COMPOSITE_FRACMUX(CLK_I2S10_8CH_RX_FRAC, "clk_i2s10_8ch_rx_frac", "clk_i2s10_8ch_rx_src", CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(156), 0,
			RK3588_CLKGATE_CON(65), 6, GFLAGS,
			&rk3588_i2s10_8ch_rx_fracmux, RK3588_FRAC_MAX_PRATE),
	GATE(MCLK_I2S10_8CH_RX, "mclk_i2s10_8ch_rx", "clk_i2s10_8ch_rx", 0,
			RK3588_CLKGATE_CON(65), 7, GFLAGS),
	GATE(HCLK_I2S7_8CH, "hclk_i2s7_8ch", "hclk_vo1_root", 0,
			RK3588_CLKGATE_CON(60), 0, GFLAGS),
	COMPOSITE(CLK_I2S7_8CH_RX_SRC, "clk_i2s7_8ch_rx_src", gpll_aupll_p, 0,
			RK3588_CLKSEL_CON(129), 11, 1, MFLAGS, 6, 5, DFLAGS,
			RK3588_CLKGATE_CON(60), 1, GFLAGS),
	COMPOSITE_FRACMUX(CLK_I2S7_8CH_RX_FRAC, "clk_i2s7_8ch_rx_frac", "clk_i2s7_8ch_rx_src", CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(130), 0,
			RK3588_CLKGATE_CON(60), 2, GFLAGS,
			&rk3588_i2s7_8ch_rx_fracmux, RK3588_FRAC_MAX_PRATE),
	GATE(MCLK_I2S7_8CH_RX, "mclk_i2s7_8ch_rx", "clk_i2s7_8ch_rx", 0,
			RK3588_CLKGATE_CON(60), 3, GFLAGS),
	GATE(HCLK_I2S9_8CH, "hclk_i2s9_8ch", "hclk_vo1_root", 0,
			RK3588_CLKGATE_CON(65), 0, GFLAGS),
	COMPOSITE(CLK_I2S9_8CH_RX_SRC, "clk_i2s9_8ch_rx_src", gpll_aupll_p, 0,
			RK3588_CLKSEL_CON(153), 12, 1, MFLAGS, 7, 5, DFLAGS,
			RK3588_CLKGATE_CON(65), 1, GFLAGS),
	COMPOSITE_FRACMUX(CLK_I2S9_8CH_RX_FRAC, "clk_i2s9_8ch_rx_frac", "clk_i2s9_8ch_rx_src", CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(154), 0,
			RK3588_CLKGATE_CON(65), 2, GFLAGS,
			&rk3588_i2s9_8ch_rx_fracmux, RK3588_FRAC_MAX_PRATE),
	GATE(MCLK_I2S9_8CH_RX, "mclk_i2s9_8ch_rx", "clk_i2s9_8ch_rx", 0,
			RK3588_CLKGATE_CON(65), 3, GFLAGS),
	COMPOSITE(CLK_I2S5_8CH_TX_SRC, "clk_i2s5_8ch_tx_src", gpll_aupll_p, 0,
			RK3588_CLKSEL_CON(140), 10, 1, MFLAGS, 5, 5, DFLAGS,
			RK3588_CLKGATE_CON(62), 6, GFLAGS),
	COMPOSITE_FRACMUX(CLK_I2S5_8CH_TX_FRAC, "clk_i2s5_8ch_tx_frac", "clk_i2s5_8ch_tx_src", 0,
			RK3588_CLKSEL_CON(141), 0,
			RK3588_CLKGATE_CON(62), 7, GFLAGS,
			&rk3588_i2s5_8ch_tx_fracmux, RK3588_FRAC_MAX_PRATE),
	GATE(MCLK_I2S5_8CH_TX, "mclk_i2s5_8ch_tx", "clk_i2s5_8ch_tx", 0,
			RK3588_CLKGATE_CON(62), 8, GFLAGS),
	GATE(HCLK_I2S5_8CH, "hclk_i2s5_8ch", "hclk_vo1_root", 0,
			RK3588_CLKGATE_CON(62), 12, GFLAGS),
	COMPOSITE(CLK_I2S6_8CH_TX_SRC, "clk_i2s6_8ch_tx_src", gpll_aupll_p, 0,
			RK3588_CLKSEL_CON(144), 8, 1, MFLAGS, 3, 5, DFLAGS,
			RK3588_CLKGATE_CON(62), 13, GFLAGS),
	COMPOSITE_FRACMUX(CLK_I2S6_8CH_TX_FRAC, "clk_i2s6_8ch_tx_frac", "clk_i2s6_8ch_tx_src", CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(145), 0,
			RK3588_CLKGATE_CON(62), 14, GFLAGS,
			&rk3588_i2s6_8ch_tx_fracmux, RK3588_FRAC_MAX_PRATE),
	GATE(MCLK_I2S6_8CH_TX, "mclk_i2s6_8ch_tx", "clk_i2s6_8ch_tx", 0,
			RK3588_CLKGATE_CON(62), 15, GFLAGS),
	COMPOSITE(CLK_I2S6_8CH_RX_SRC, "clk_i2s6_8ch_rx_src", gpll_aupll_p, 0,
			RK3588_CLKSEL_CON(146), 7, 1, MFLAGS, 2, 5, DFLAGS,
			RK3588_CLKGATE_CON(63), 0, GFLAGS),
	COMPOSITE_FRACMUX(CLK_I2S6_8CH_RX_FRAC, "clk_i2s6_8ch_rx_frac", "clk_i2s6_8ch_rx_src", 0,
			RK3588_CLKSEL_CON(147), 0,
			RK3588_CLKGATE_CON(63), 1, GFLAGS,
			&rk3588_i2s6_8ch_rx_fracmux, RK3588_FRAC_MAX_PRATE),
	GATE(MCLK_I2S6_8CH_RX, "mclk_i2s6_8ch_rx", "clk_i2s6_8ch_rx", 0,
			RK3588_CLKGATE_CON(63), 2, GFLAGS),
	MUX(I2S6_8CH_MCLKOUT, "i2s6_8ch_mclkout", i2s6_8ch_mclkout_p, CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(148), 2, 2, MFLAGS),
	GATE(HCLK_I2S6_8CH, "hclk_i2s6_8ch", "hclk_vo1_root", 0,
			RK3588_CLKGATE_CON(63), 3, GFLAGS),
	GATE(HCLK_SPDIF3, "hclk_spdif3", "hclk_vo1_root", 0,
			RK3588_CLKGATE_CON(63), 4, GFLAGS),
	COMPOSITE(CLK_SPDIF3_SRC, "clk_spdif3_src", gpll_aupll_p, 0,
			RK3588_CLKSEL_CON(148), 9, 1, MFLAGS, 4, 5, DFLAGS,
			RK3588_CLKGATE_CON(63), 5, GFLAGS),
	COMPOSITE_FRACMUX(CLK_SPDIF3_FRAC, "clk_spdif3_frac", "clk_spdif3_src", CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(149), 0,
			RK3588_CLKGATE_CON(63), 6, GFLAGS,
			&rk3588_spdif3_fracmux, RK3588_FRAC_MAX_PRATE),
	GATE(MCLK_SPDIF3, "mclk_spdif3", "clk_spdif3", 0,
			RK3588_CLKGATE_CON(63), 7, GFLAGS),
	GATE(HCLK_SPDIF4, "hclk_spdif4", "hclk_vo1_root", 0,
			RK3588_CLKGATE_CON(63), 8, GFLAGS),
	COMPOSITE(CLK_SPDIF4_SRC, "clk_spdif4_src", gpll_aupll_p, 0,
			RK3588_CLKSEL_CON(150), 7, 1, MFLAGS, 2, 5, DFLAGS,
			RK3588_CLKGATE_CON(63), 9, GFLAGS),
	COMPOSITE_FRACMUX(CLK_SPDIF4_FRAC, "clk_spdif4_frac", "clk_spdif4_src", CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(151), 0,
			RK3588_CLKGATE_CON(63), 10, GFLAGS,
			&rk3588_spdif4_fracmux, RK3588_FRAC_MAX_PRATE),
	GATE(MCLK_SPDIF4, "mclk_spdif4", "clk_spdif4", 0,
			RK3588_CLKGATE_CON(63), 11, GFLAGS),
	GATE(HCLK_SPDIFRX0, "hclk_spdifrx0", "hclk_vo1_root", 0,
			RK3588_CLKGATE_CON(63), 12, GFLAGS),
	COMPOSITE(MCLK_SPDIFRX0, "mclk_spdifrx0", gpll_cpll_aupll_p, 0,
			RK3588_CLKSEL_CON(152), 7, 2, MFLAGS, 2, 5, DFLAGS,
			RK3588_CLKGATE_CON(63), 13, GFLAGS),
	GATE(HCLK_SPDIFRX1, "hclk_spdifrx1", "hclk_vo1_root", 0,
			RK3588_CLKGATE_CON(63), 14, GFLAGS),
	COMPOSITE(MCLK_SPDIFRX1, "mclk_spdifrx1", gpll_cpll_aupll_p, 0,
			RK3588_CLKSEL_CON(152), 14, 2, MFLAGS, 9, 5, DFLAGS,
			RK3588_CLKGATE_CON(63), 15, GFLAGS),
	GATE(HCLK_SPDIFRX2, "hclk_spdifrx2", "hclk_vo1_root", 0,
			RK3588_CLKGATE_CON(64), 0, GFLAGS),
	COMPOSITE(MCLK_SPDIFRX2, "mclk_spdifrx2", gpll_cpll_aupll_p, 0,
			RK3588_CLKSEL_CON(153), 5, 2, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(64), 1, GFLAGS),
	COMPOSITE(ACLK_VO1USB_TOP_ROOT, "aclk_vo1usb_top_root", gpll_cpll_p, 0,
			RK3588_CLKSEL_CON(170), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(74), 0, GFLAGS),
	COMPOSITE_NODIV(HCLK_VO1USB_TOP_ROOT, "hclk_vo1usb_top_root", mux_200m_100m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(170), 6, 2, MFLAGS,
			RK3588_CLKGATE_CON(74), 2, GFLAGS),
	GATE(CLK_HDMIHDP0, "clk_hdmihdp0", "xin24m", 0,
			RK3588_CLKGATE_CON(73), 12, GFLAGS),
	GATE(CLK_HDMIHDP1, "clk_hdmihdp1", "xin24m", 0,
			RK3588_CLKGATE_CON(73), 13, GFLAGS),
	GATE(PCLK_HDPTX0, "pclk_hdptx0", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(72), 5, GFLAGS),
	GATE(PCLK_HDPTX1, "pclk_hdptx1", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(72), 6, GFLAGS),
	GATE(PCLK_USBDPPHY0, "pclk_usbdpphy0", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(72), 2, GFLAGS),
	GATE(PCLK_USBDPPHY1, "pclk_usbdpphy1", "pclk_top_root", 0,
			RK3588_CLKGATE_CON(72), 4, GFLAGS),
	COMPOSITE(ACLK_VOP_ROOT, "aclk_vop_root", gpll_cpll_aupll_npll_spll_p, 0,
			RK3588_CLKSEL_CON(110), 5, 3, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(52), 0, GFLAGS),
	COMPOSITE_NODIV(ACLK_VOP_LOW_ROOT, "aclk_vop_low_root", mux_400m_200m_100m_24m_p, 0,
			RK3588_CLKSEL_CON(110), 8, 2, MFLAGS,
			RK3588_CLKGATE_CON(52), 1, GFLAGS),
	COMPOSITE_NODIV(HCLK_VOP_ROOT, "hclk_vop_root", mux_200m_100m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(110), 10, 2, MFLAGS,
			RK3588_CLKGATE_CON(52), 2, GFLAGS),
	COMPOSITE_NODIV(PCLK_VOP_ROOT, "pclk_vop_root", mux_100m_50m_24m_p, 0,
			RK3588_CLKSEL_CON(110), 12, 2, MFLAGS,
			RK3588_CLKGATE_CON(52), 3, GFLAGS),
	GATE(HCLK_VOP, "hclk_vop", "hclk_vop_root", 0,
			RK3588_CLKGATE_CON(52), 8, GFLAGS),
	GATE(ACLK_VOP, "aclk_vop", "aclk_vop_sub_src", 0,
			RK3588_CLKGATE_CON(52), 9, GFLAGS),
	COMPOSITE(DCLK_VOP0_SRC, "dclk_vop0_src", gpll_cpll_v0pll_aupll_p, 0,
			RK3588_CLKSEL_CON(111), 7, 2, MFLAGS, 0, 7, DFLAGS,
			RK3588_CLKGATE_CON(52), 10, GFLAGS),
	COMPOSITE(DCLK_VOP1_SRC, "dclk_vop1_src", gpll_cpll_v0pll_aupll_p, 0,
			RK3588_CLKSEL_CON(111), 14, 2, MFLAGS, 9, 5, DFLAGS,
			RK3588_CLKGATE_CON(52), 11, GFLAGS),
	COMPOSITE(DCLK_VOP2_SRC, "dclk_vop2_src", gpll_cpll_v0pll_aupll_p, 0,
			RK3588_CLKSEL_CON(112), 5, 2, MFLAGS, 0, 5, DFLAGS,
			RK3588_CLKGATE_CON(52), 12, GFLAGS),
	COMPOSITE_NODIV(DCLK_VOP0, "dclk_vop0", dclk_vop0_p, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			RK3588_CLKSEL_CON(112), 7, 2, MFLAGS,
			RK3588_CLKGATE_CON(52), 13, GFLAGS),
	COMPOSITE_NODIV(DCLK_VOP1, "dclk_vop1", dclk_vop1_p, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			RK3588_CLKSEL_CON(112), 9, 2, MFLAGS,
			RK3588_CLKGATE_CON(53), 0, GFLAGS),
	COMPOSITE_NODIV(DCLK_VOP2, "dclk_vop2", dclk_vop2_p, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			RK3588_CLKSEL_CON(112), 11, 2, MFLAGS,
			RK3588_CLKGATE_CON(53), 1, GFLAGS),
	COMPOSITE(DCLK_VOP3, "dclk_vop3", gpll_cpll_v0pll_aupll_p, 0,
			RK3588_CLKSEL_CON(113), 7, 2, MFLAGS, 0, 7, DFLAGS,
			RK3588_CLKGATE_CON(53), 2, GFLAGS),
	GATE(PCLK_DSIHOST0, "pclk_dsihost0", "pclk_vop_root", 0,
			RK3588_CLKGATE_CON(53), 4, GFLAGS),
	GATE(PCLK_DSIHOST1, "pclk_dsihost1", "pclk_vop_root", 0,
			RK3588_CLKGATE_CON(53), 5, GFLAGS),
	COMPOSITE(CLK_DSIHOST0, "clk_dsihost0", gpll_cpll_v0pll_spll_p, 0,
			RK3588_CLKSEL_CON(114), 7, 2, MFLAGS, 0, 7, DFLAGS,
			RK3588_CLKGATE_CON(53), 6, GFLAGS),
	COMPOSITE(CLK_DSIHOST1, "clk_dsihost1", gpll_cpll_v0pll_spll_p, 0,
			RK3588_CLKSEL_CON(115), 7, 2, MFLAGS, 0, 7, DFLAGS,
			RK3588_CLKGATE_CON(53), 7, GFLAGS),
	GATE(CLK_VOP_PMU, "clk_vop_pmu", "xin24m", CLK_IGNORE_UNUSED,
			RK3588_CLKGATE_CON(53), 8, GFLAGS),
	GATE(ACLK_VOP_DOBY, "aclk_vop_doby", "aclk_vop_root", 0,
			RK3588_CLKGATE_CON(53), 10, GFLAGS),
	MUX(ACLK_VOP_SUB_SRC, "aclk_vop_sub_src", aclk_vop_sub_src_p, CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(115), 9, 1, MFLAGS),
	GATE(CLK_USBDP_PHY0_IMMORTAL, "clk_usbdp_phy0_immortal", "xin24m", CLK_IGNORE_UNUSED,
			RK3588_CLKGATE_CON(2), 8, GFLAGS),
	GATE(CLK_USBDP_PHY1_IMMORTAL, "clk_usbdp_phy1_immortal", "xin24m", CLK_IGNORE_UNUSED,
			RK3588_CLKGATE_CON(2), 15, GFLAGS),

	GATE(CLK_REF_PIPE_PHY0_OSC_SRC, "clk_ref_pipe_phy0_osc_src", "xin24m", 0,
			RK3588_CLKGATE_CON(77), 0, GFLAGS),
	GATE(CLK_REF_PIPE_PHY1_OSC_SRC, "clk_ref_pipe_phy1_osc_src", "xin24m", 0,
			RK3588_CLKGATE_CON(77), 1, GFLAGS),
	GATE(CLK_REF_PIPE_PHY2_OSC_SRC, "clk_ref_pipe_phy2_osc_src", "xin24m", 0,
			RK3588_CLKGATE_CON(77), 2, GFLAGS),
	COMPOSITE_NOMUX(CLK_REF_PIPE_PHY0_PLL_SRC, "clk_ref_pipe_phy0_pll_src", "ppll", 0,
			RK3588_CLKSEL_CON(176), 0, 6, DFLAGS,
			RK3588_CLKGATE_CON(77), 3, GFLAGS),
	COMPOSITE_NOMUX(CLK_REF_PIPE_PHY1_PLL_SRC, "clk_ref_pipe_phy1_pll_src", "ppll", 0,
			RK3588_CLKSEL_CON(176), 6, 6, DFLAGS,
			RK3588_CLKGATE_CON(77), 4, GFLAGS),
	COMPOSITE_NOMUX(CLK_REF_PIPE_PHY2_PLL_SRC, "clk_ref_pipe_phy2_pll_src", "ppll", 0,
			RK3588_CLKSEL_CON(177), 0, 6, DFLAGS,
			RK3588_CLKGATE_CON(77), 5, GFLAGS),
	MUX(CLK_REF_PIPE_PHY0, "clk_ref_pipe_phy0", clk_ref_pipe_phy0_p, CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(177), 6, 1, MFLAGS),
	MUX(CLK_REF_PIPE_PHY1, "clk_ref_pipe_phy1", clk_ref_pipe_phy1_p, CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(177), 7, 1, MFLAGS),
	MUX(CLK_REF_PIPE_PHY2, "clk_ref_pipe_phy2", clk_ref_pipe_phy2_p, CLK_SET_RATE_PARENT,
			RK3588_CLKSEL_CON(177), 8, 1, MFLAGS),

	/* pmu */
	GATE(CLK_PMU0, "clk_pmu0", "xin24m", CLK_IS_CRITICAL,
			RK3588_PMU_CLKGATE_CON(5), 1, GFLAGS),
	GATE(PCLK_PMU0, "pclk_pmu0", "pclk_pmu0_root", CLK_IS_CRITICAL,
			RK3588_PMU_CLKGATE_CON(5), 2, GFLAGS),
	GATE(PCLK_PMU0IOC, "pclk_pmu0ioc", "pclk_pmu0_root", CLK_IS_CRITICAL,
			RK3588_PMU_CLKGATE_CON(5), 4, GFLAGS),
	GATE(PCLK_GPIO0, "pclk_gpio0", "pclk_pmu0_root", 0,
			RK3588_PMU_CLKGATE_CON(5), 5, GFLAGS),
	COMPOSITE_NODIV(DBCLK_GPIO0, "dbclk_gpio0", mux_24m_32k_p, 0,
			RK3588_PMU_CLKSEL_CON(17), 0, 1, MFLAGS,
			RK3588_PMU_CLKGATE_CON(5), 6, GFLAGS),
	GATE(PCLK_I2C0, "pclk_i2c0", "pclk_pmu0_root", 0,
			RK3588_PMU_CLKGATE_CON(2), 1, GFLAGS),
	COMPOSITE_NODIV(CLK_I2C0, "clk_i2c0", pmu_200m_100m_p, 0,
			RK3588_PMU_CLKSEL_CON(3), 6, 1, MFLAGS,
			RK3588_PMU_CLKGATE_CON(2), 2, GFLAGS),
	GATE(HCLK_I2S1_8CH, "hclk_i2s1_8ch", "hclk_pmu1_root", 0,
			RK3588_PMU_CLKGATE_CON(2), 7, GFLAGS),
	COMPOSITE_NOMUX(CLK_I2S1_8CH_TX_SRC, "clk_i2s1_8ch_tx_src", "cpll", 0,
			RK3588_PMU_CLKSEL_CON(5), 2, 5, DFLAGS,
			RK3588_PMU_CLKGATE_CON(2), 8, GFLAGS),
	COMPOSITE_FRACMUX(CLK_I2S1_8CH_TX_FRAC, "clk_i2s1_8ch_tx_frac", "clk_i2s1_8ch_tx_src", CLK_SET_RATE_PARENT,
			RK3588_PMU_CLKSEL_CON(6), 0,
			RK3588_PMU_CLKGATE_CON(2), 9, GFLAGS,
			&rk3588_i2s1_8ch_tx_fracmux, RK3588_FRAC_MAX_PRATE),
	GATE(MCLK_I2S1_8CH_TX, "mclk_i2s1_8ch_tx", "clk_i2s1_8ch_tx", 0,
			RK3588_PMU_CLKGATE_CON(2), 10, GFLAGS),
	COMPOSITE_NOMUX(CLK_I2S1_8CH_RX_SRC, "clk_i2s1_8ch_rx_src", "cpll", 0,
			RK3588_PMU_CLKSEL_CON(7), 2, 5, DFLAGS,
			RK3588_PMU_CLKGATE_CON(2), 11, GFLAGS),
	COMPOSITE_FRACMUX(CLK_I2S1_8CH_RX_FRAC, "clk_i2s1_8ch_rx_frac", "clk_i2s1_8ch_rx_src", CLK_SET_RATE_PARENT,
			RK3588_PMU_CLKSEL_CON(8), 0,
			RK3588_PMU_CLKGATE_CON(2), 12, GFLAGS,
			&rk3588_i2s1_8ch_rx_fracmux, RK3588_FRAC_MAX_PRATE),
	GATE(MCLK_I2S1_8CH_RX, "mclk_i2s1_8ch_rx", "clk_i2s1_8ch_rx", 0,
			RK3588_PMU_CLKGATE_CON(2), 13, GFLAGS),
	MUX(I2S1_8CH_MCLKOUT, "i2s1_8ch_mclkout", i2s1_8ch_mclkout_p, CLK_SET_RATE_PARENT,
			RK3588_PMU_CLKSEL_CON(9), 2, 2, MFLAGS),
	COMPOSITE_NOMUX(CLK_PMU1_50M_SRC, "clk_pmu1_50m_src", "clk_pmu1_400m_src", 0,
			RK3588_PMU_CLKSEL_CON(0), 0, 4, DFLAGS,
			RK3588_PMU_CLKGATE_CON(0), 0, GFLAGS),
	COMPOSITE_NOMUX(CLK_PMU1_100M_SRC, "clk_pmu1_100m_src", "clk_pmu1_400m_src", 0,
			RK3588_PMU_CLKSEL_CON(0), 4, 3, DFLAGS,
			RK3588_PMU_CLKGATE_CON(0), 1, GFLAGS),
	COMPOSITE_NOMUX(CLK_PMU1_200M_SRC, "clk_pmu1_200m_src", "clk_pmu1_400m_src", 0,
			RK3588_PMU_CLKSEL_CON(0), 7, 3, DFLAGS,
			RK3588_PMU_CLKGATE_CON(0), 2, GFLAGS),
	COMPOSITE(CLK_PMU1_300M_SRC, "clk_pmu1_300m_src", pmu_300m_24m_p, 0,
			RK3588_PMU_CLKSEL_CON(0), 15, 1, MFLAGS, 10, 5, DFLAGS,
			RK3588_PMU_CLKGATE_CON(0), 3, GFLAGS),
	COMPOSITE(CLK_PMU1_400M_SRC, "clk_pmu1_400m_src", pmu_400m_24m_p, 0,
			RK3588_PMU_CLKSEL_CON(1), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RK3588_PMU_CLKGATE_CON(0), 4, GFLAGS),
	COMPOSITE_NODIV(HCLK_PMU1_ROOT, "hclk_pmu1_root", hclk_pmu1_root_p, CLK_IS_CRITICAL,
			RK3588_PMU_CLKSEL_CON(1), 6, 2, MFLAGS,
			RK3588_PMU_CLKGATE_CON(0), 5, GFLAGS),
	COMPOSITE_NODIV(PCLK_PMU1_ROOT, "pclk_pmu1_root", pmu_100m_50m_24m_src_p, CLK_IS_CRITICAL,
			RK3588_PMU_CLKSEL_CON(1), 8, 2, MFLAGS,
			RK3588_PMU_CLKGATE_CON(0), 7, GFLAGS),
	GATE(PCLK_PMU0_ROOT, "pclk_pmu0_root", "pclk_pmu1_root", CLK_IS_CRITICAL,
			RK3588_PMU_CLKGATE_CON(5), 0, GFLAGS),
	COMPOSITE_NODIV(HCLK_PMU_CM0_ROOT, "hclk_pmu_cm0_root", hclk_pmu_cm0_root_p, CLK_IS_CRITICAL,
			RK3588_PMU_CLKSEL_CON(1), 10, 2, MFLAGS,
			RK3588_PMU_CLKGATE_CON(0), 8, GFLAGS),
	GATE(PCLK_PMU1, "pclk_pmu1", "pclk_pmu0_root", CLK_IS_CRITICAL,
			RK3588_PMU_CLKGATE_CON(1), 0, GFLAGS),
	GATE(CLK_DDR_FAIL_SAFE, "clk_ddr_fail_safe", "clk_pmu0", CLK_IGNORE_UNUSED,
			RK3588_PMU_CLKGATE_CON(1), 1, GFLAGS),
	GATE(CLK_PMU1, "clk_pmu1", "clk_pmu0", CLK_IS_CRITICAL,
			RK3588_PMU_CLKGATE_CON(1), 3, GFLAGS),
	GATE(HCLK_PDM0, "hclk_pdm0", "hclk_pmu1_root", 0,
			RK3588_PMU_CLKGATE_CON(2), 14, GFLAGS),
	COMPOSITE_NODIV(MCLK_PDM0, "mclk_pdm0", mclk_pdm0_p, 0,
			RK3588_PMU_CLKSEL_CON(9), 4, 1, MFLAGS,
			RK3588_PMU_CLKGATE_CON(2), 15, GFLAGS),
	GATE(HCLK_VAD, "hclk_vad", "hclk_pmu1_root", 0,
			RK3588_PMU_CLKGATE_CON(3), 0, GFLAGS),
	GATE(FCLK_PMU_CM0_CORE, "fclk_pmu_cm0_core", "hclk_pmu_cm0_root", CLK_IS_CRITICAL,
			RK3588_PMU_CLKGATE_CON(0), 13, GFLAGS),
	COMPOSITE(CLK_PMU_CM0_RTC, "clk_pmu_cm0_rtc", mux_24m_32k_p, CLK_IS_CRITICAL,
			RK3588_PMU_CLKSEL_CON(2), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RK3588_PMU_CLKGATE_CON(0), 15, GFLAGS),
	GATE(PCLK_PMU1_IOC, "pclk_pmu1_ioc", "pclk_pmu0_root", CLK_IGNORE_UNUSED,
			RK3588_PMU_CLKGATE_CON(1), 5, GFLAGS),
	GATE(PCLK_PMU1PWM, "pclk_pmu1pwm", "pclk_pmu0_root", 0,
			RK3588_PMU_CLKGATE_CON(1), 12, GFLAGS),
	COMPOSITE_NODIV(CLK_PMU1PWM, "clk_pmu1pwm", pmu_100m_50m_24m_src_p, 0,
			RK3588_PMU_CLKSEL_CON(2), 9, 2, MFLAGS,
			RK3588_PMU_CLKGATE_CON(1), 13, GFLAGS),
	GATE(CLK_PMU1PWM_CAPTURE, "clk_pmu1pwm_capture", "xin24m", 0,
			RK3588_PMU_CLKGATE_CON(1), 14, GFLAGS),
	GATE(PCLK_PMU1TIMER, "pclk_pmu1timer", "pclk_pmu0_root", 0,
			RK3588_PMU_CLKGATE_CON(1), 8, GFLAGS),
	COMPOSITE_NODIV(CLK_PMU1TIMER_ROOT, "clk_pmu1timer_root", pmu_24m_32k_100m_src_p, 0,
			RK3588_PMU_CLKSEL_CON(2), 7, 2, MFLAGS,
			RK3588_PMU_CLKGATE_CON(1), 9, GFLAGS),
	GATE(CLK_PMU1TIMER0, "clk_pmu1timer0", "clk_pmu1timer_root", 0,
			RK3588_PMU_CLKGATE_CON(1), 10, GFLAGS),
	GATE(CLK_PMU1TIMER1, "clk_pmu1timer1", "clk_pmu1timer_root", 0,
			RK3588_PMU_CLKGATE_CON(1), 11, GFLAGS),
	COMPOSITE_NOMUX(CLK_UART0_SRC, "clk_uart0_src", "cpll", 0,
			RK3588_PMU_CLKSEL_CON(3), 7, 5, DFLAGS,
			RK3588_PMU_CLKGATE_CON(2), 3, GFLAGS),
	COMPOSITE_FRACMUX(CLK_UART0_FRAC, "clk_uart0_frac", "clk_uart0_src", CLK_SET_RATE_PARENT,
			RK3588_PMU_CLKSEL_CON(4), CLK_FRAC_DIVIDER_NO_LIMIT,
			RK3588_PMU_CLKGATE_CON(2), 4, GFLAGS,
			&rk3588_uart0_fracmux, RK3588_UART_FRAC_MAX_PRATE),
	GATE(SCLK_UART0, "sclk_uart0", "clk_uart0", 0,
			RK3588_PMU_CLKGATE_CON(2), 5, GFLAGS),
	GATE(PCLK_UART0, "pclk_uart0", "pclk_pmu0_root", 0,
			RK3588_PMU_CLKGATE_CON(2), 6, GFLAGS),
	GATE(PCLK_PMU1WDT, "pclk_pmu1wdt", "pclk_pmu0_root", 0,
			RK3588_PMU_CLKGATE_CON(1), 6, GFLAGS),
	COMPOSITE_NODIV(TCLK_PMU1WDT, "tclk_pmu1wdt", mux_24m_32k_p, 0,
			RK3588_PMU_CLKSEL_CON(2), 6, 1, MFLAGS,
			RK3588_PMU_CLKGATE_CON(1), 7, GFLAGS),
	COMPOSITE(CLK_CR_PARA, "clk_cr_para", mux_24m_ppll_spll_p, 0,
			RK3588_PMU_CLKSEL_CON(15), 5, 2, MFLAGS, 0, 5, DFLAGS,
			RK3588_PMU_CLKGATE_CON(4), 11, GFLAGS),
	COMPOSITE(CLK_USB2PHY_HDPTXRXPHY_REF, "clk_usb2phy_hdptxrxphy_ref", mux_24m_ppll_p, CLK_IS_CRITICAL,
			RK3588_PMU_CLKSEL_CON(14), 14, 1, MFLAGS, 9, 5, DFLAGS,
			RK3588_PMU_CLKGATE_CON(4), 7, GFLAGS),
	COMPOSITE(CLK_USBDPPHY_MIPIDCPPHY_REF, "clk_usbdpphy_mipidcpphy_ref", mux_24m_ppll_p, CLK_IS_CRITICAL,
			RK3588_PMU_CLKSEL_CON(9), 10, 1, MFLAGS, 5, 5, DFLAGS,
			RK3588_PMU_CLKGATE_CON(3), 5, GFLAGS),
};

static void __iomem *rk3588_cru_base;

static void rk3588_dump_cru(void)
{
	if (rk3588_cru_base) {
		pr_warn("DSU CRU:\n");
		print_hex_dump(KERN_WARNING, "", DUMP_PREFIX_OFFSET,
			       32, 4, rk3588_cru_base + RK3588_DSU_CRU_BASE,
			       0x330, false);
		pr_warn("BIGCORE0 CRU:\n");
		print_hex_dump(KERN_WARNING, "", DUMP_PREFIX_OFFSET,
			       32, 4, rk3588_cru_base + RK3588_BIGCORE0_CRU_BASE,
			       0x300, false);
		pr_warn("BIGCORE1 CRU:\n");
		print_hex_dump(KERN_WARNING, "", DUMP_PREFIX_OFFSET,
			       32, 4, rk3588_cru_base + RK3588_BIGCORE1_CRU_BASE,
			       0x300, false);
	}
}

static void __init rk3588_clk_init(struct device_node *np)
{
	struct rockchip_clk_provider *ctx;
	void __iomem *reg_base;
	struct clk **clks;

	reg_base = of_iomap(np, 0);
	if (!reg_base) {
		pr_err("%s: could not map cru region\n", __func__);
		return;
	}

	rk3588_cru_base = reg_base;

	ctx = rockchip_clk_init(np, reg_base, CLK_NR_CLKS);
	if (IS_ERR(ctx)) {
		pr_err("%s: rockchip clk init failed\n", __func__);
		iounmap(reg_base);
		return;
	}
	clks = ctx->clk_data.clks;

	rockchip_clk_register_plls(ctx, rk3588_pll_clks,
				   ARRAY_SIZE(rk3588_pll_clks),
				   RK3588_GRF_SOC_STATUS0);

	rockchip_clk_register_armclk(ctx, ARMCLK_L, "armclk_l",
			3, clks[PLL_LPLL], clks[PLL_GPLL],
			&rk3588_cpulclk_data, rk3588_cpulclk_rates,
			ARRAY_SIZE(rk3588_cpulclk_rates));
	rockchip_clk_register_armclk(ctx, ARMCLK_B01, "armclk_b01",
			3, clks[PLL_B0PLL], clks[PLL_GPLL],
			&rk3588_cpub0clk_data, rk3588_cpub0clk_rates,
			ARRAY_SIZE(rk3588_cpub0clk_rates));
	rockchip_clk_register_armclk(ctx, ARMCLK_B23, "armclk_b23",
			3, clks[PLL_B1PLL], clks[PLL_GPLL],
			&rk3588_cpub1clk_data, rk3588_cpub1clk_rates,
			ARRAY_SIZE(rk3588_cpub1clk_rates));

	rockchip_clk_register_branches(ctx, rk3588_clk_branches,
				       ARRAY_SIZE(rk3588_clk_branches));

	rockchip_register_softrst(np, 49158, reg_base + RK3588_SOFTRST_CON(0),
				  ROCKCHIP_SOFTRST_HIWORD_MASK);

	rockchip_register_restart_notifier(ctx, RK3588_GLB_SRST_FST, NULL);

	rockchip_clk_of_add_provider(np, ctx);

	if (!rk_dump_cru)
		rk_dump_cru = rk3588_dump_cru;
}

CLK_OF_DECLARE(rk3588_cru, "rockchip,rk3588-cru", rk3588_clk_init);

struct clk_rk3588_inits {
	void (*inits)(struct device_node *np);
};

static const struct clk_rk3588_inits clk_3588_cru_init = {
	.inits = rk3588_clk_init,
};

static const struct of_device_id clk_rk3588_match_table[] = {
	{
		.compatible = "rockchip,rk3588-cru",
		.data = &clk_3588_cru_init,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, clk_rk3588_match_table);

static int __init clk_rk3588_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match;
	const struct clk_rk3588_inits *init_data;

	match = of_match_device(clk_rk3588_match_table, &pdev->dev);
	if (!match || !match->data)
		return -EINVAL;

	init_data = match->data;
	if (init_data->inits)
		init_data->inits(np);

	return 0;
}

static struct platform_driver clk_rk3588_driver = {
	.driver		= {
		.name	= "clk-rk3588",
		.of_match_table = clk_rk3588_match_table,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver_probe(clk_rk3588_driver, clk_rk3588_probe);

MODULE_DESCRIPTION("Rockchip RK3588 Clock Driver");
MODULE_LICENSE("GPL");
