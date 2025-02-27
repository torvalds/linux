// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 * Copyright (c) 2024 Yao Zi <ziyao@disroot.org>
 * Author: Joseph Chen <chenjh@rock-chips.com>
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include <dt-bindings/clock/rockchip,rk3528-cru.h>

#include "clk.h"

#define RK3528_GRF_SOC_STATUS0		0x1a0

enum rk3528_plls {
	apll, cpll, gpll, ppll, dpll,
};

static struct rockchip_pll_rate_table rk3528_pll_rates[] = {
	RK3036_PLL_RATE(1896000000, 1, 79, 1, 1, 1, 0),
	RK3036_PLL_RATE(1800000000, 1, 75, 1, 1, 1, 0),
	RK3036_PLL_RATE(1704000000, 1, 71, 1, 1, 1, 0),
	RK3036_PLL_RATE(1608000000, 1, 67, 1, 1, 1, 0),
	RK3036_PLL_RATE(1512000000, 1, 63, 1, 1, 1, 0),
	RK3036_PLL_RATE(1416000000, 1, 59, 1, 1, 1, 0),
	RK3036_PLL_RATE(1296000000, 1, 54, 1, 1, 1, 0),
	RK3036_PLL_RATE(1200000000, 1, 50, 1, 1, 1, 0),
	RK3036_PLL_RATE(1188000000, 1, 99, 2, 1, 1, 0),		/* GPLL */
	RK3036_PLL_RATE(1092000000, 2, 91, 1, 1, 1, 0),
	RK3036_PLL_RATE(1008000000, 1, 42, 1, 1, 1, 0),
	RK3036_PLL_RATE(1000000000, 1, 125, 3, 1, 1, 0),	/* PPLL */
	RK3036_PLL_RATE(996000000, 2, 83, 1, 1, 1, 0),		/* CPLL */
	RK3036_PLL_RATE(960000000, 1, 40, 1, 1, 1, 0),
	RK3036_PLL_RATE(912000000, 1, 76, 2, 1, 1, 0),
	RK3036_PLL_RATE(816000000, 1, 68, 2, 1, 1, 0),
	RK3036_PLL_RATE(600000000, 1, 50, 2, 1, 1, 0),
	RK3036_PLL_RATE(594000000, 2, 99, 2, 1, 1, 0),
	RK3036_PLL_RATE(408000000, 1, 68, 2, 2, 1, 0),
	RK3036_PLL_RATE(312000000, 1, 78, 6, 1, 1, 0),
	RK3036_PLL_RATE(216000000, 1, 72, 4, 2, 1, 0),
	RK3036_PLL_RATE(96000000, 1, 24, 3, 2, 1, 0),
	{ /* sentinel */ },
};

#define RK3528_DIV_ACLK_M_CORE_MASK	0x1f
#define RK3528_DIV_ACLK_M_CORE_SHIFT	11
#define RK3528_DIV_PCLK_DBG_MASK	0x1f
#define RK3528_DIV_PCLK_DBG_SHIFT	1

#define RK3528_CLKSEL39(_aclk_m_core)					\
{									\
	.reg = RK3528_CLKSEL_CON(39),					\
	.val = HIWORD_UPDATE(_aclk_m_core, RK3528_DIV_ACLK_M_CORE_MASK,	\
			     RK3528_DIV_ACLK_M_CORE_SHIFT),		\
}

#define RK3528_CLKSEL40(_pclk_dbg)					\
{									\
	.reg = RK3528_CLKSEL_CON(40),					\
	.val = HIWORD_UPDATE(_pclk_dbg, RK3528_DIV_PCLK_DBG_MASK,	\
			     RK3528_DIV_PCLK_DBG_SHIFT),		\
}

#define RK3528_CPUCLK_RATE(_prate, _aclk_m_core, _pclk_dbg)		\
{									\
	.prate = _prate,						\
	.divs = {							\
		RK3528_CLKSEL39(_aclk_m_core),				\
		RK3528_CLKSEL40(_pclk_dbg),				\
	},								\
}

static struct rockchip_cpuclk_rate_table rk3528_cpuclk_rates[] __initdata = {
	RK3528_CPUCLK_RATE(1896000000, 1, 13),
	RK3528_CPUCLK_RATE(1800000000, 1, 12),
	RK3528_CPUCLK_RATE(1704000000, 1, 11),
	RK3528_CPUCLK_RATE(1608000000, 1, 11),
	RK3528_CPUCLK_RATE(1512000000, 1, 11),
	RK3528_CPUCLK_RATE(1416000000, 1, 9),
	RK3528_CPUCLK_RATE(1296000000, 1, 8),
	RK3528_CPUCLK_RATE(1200000000, 1, 8),
	RK3528_CPUCLK_RATE(1188000000, 1, 8),
	RK3528_CPUCLK_RATE(1092000000, 1, 7),
	RK3528_CPUCLK_RATE(1008000000, 1, 6),
	RK3528_CPUCLK_RATE(1000000000, 1, 6),
	RK3528_CPUCLK_RATE(996000000, 1, 6),
	RK3528_CPUCLK_RATE(960000000, 1, 6),
	RK3528_CPUCLK_RATE(912000000, 1, 6),
	RK3528_CPUCLK_RATE(816000000, 1, 5),
	RK3528_CPUCLK_RATE(600000000, 1, 3),
	RK3528_CPUCLK_RATE(594000000, 1, 3),
	RK3528_CPUCLK_RATE(408000000, 1, 2),
	RK3528_CPUCLK_RATE(312000000, 1, 2),
	RK3528_CPUCLK_RATE(216000000, 1, 1),
	RK3528_CPUCLK_RATE(96000000, 1, 0),
};

static const struct rockchip_cpuclk_reg_data rk3528_cpuclk_data = {
	.core_reg[0] = RK3528_CLKSEL_CON(39),
	.div_core_shift[0] = 5,
	.div_core_mask[0] = 0x1f,
	.num_cores = 1,
	.mux_core_alt = 1,
	.mux_core_main = 0,
	.mux_core_shift = 10,
	.mux_core_mask = 0x1,
};

PNAME(mux_pll_p)                        = { "xin24m" };
PNAME(mux_armclk)			= { "apll", "gpll" };
PNAME(mux_24m_32k_p)                    = { "xin24m", "clk_32k" };
PNAME(mux_gpll_cpll_p)                  = { "gpll", "cpll" };
PNAME(mux_gpll_cpll_xin24m_p)           = { "gpll", "cpll", "xin24m" };
PNAME(mux_100m_50m_24m_p)               = { "clk_100m_src", "clk_50m_src",
					    "xin24m" };
PNAME(mux_150m_100m_24m_p)              = { "clk_150m_src", "clk_100m_src",
					    "xin24m" };
PNAME(mux_200m_100m_24m_p)              = { "clk_200m_src", "clk_100m_src",
					    "xin24m" };
PNAME(mux_200m_100m_50m_24m_p)          = { "clk_200m_src", "clk_100m_src",
					    "clk_50m_src", "xin24m" };
PNAME(mux_300m_200m_100m_24m_p)         = { "clk_300m_src", "clk_200m_src",
					    "clk_100m_src", "xin24m" };
PNAME(mux_339m_200m_100m_24m_p)         = { "clk_339m_src", "clk_200m_src",
					    "clk_100m_src", "xin24m" };
PNAME(mux_500m_200m_100m_24m_p)         = { "clk_500m_src", "clk_200m_src",
					    "clk_100m_src", "xin24m" };
PNAME(mux_500m_300m_100m_24m_p)         = { "clk_500m_src", "clk_300m_src",
					    "clk_100m_src", "xin24m" };
PNAME(mux_600m_300m_200m_24m_p)         = { "clk_600m_src", "clk_300m_src",
					    "clk_200m_src", "xin24m" };
PNAME(aclk_gpu_p)                       = { "aclk_gpu_root",
					    "clk_gpu_pvtpll_src" };
PNAME(aclk_rkvdec_pvtmux_root_p)        = { "aclk_rkvdec_root",
					    "clk_rkvdec_pvtpll_src" };
PNAME(clk_i2c2_p)                       = { "clk_200m_src", "clk_100m_src",
					    "xin24m", "clk_32k" };
PNAME(clk_ref_pcie_inner_phy_p)         = { "clk_ppll_100m_src", "xin24m" };
PNAME(dclk_vop0_p)                      = { "dclk_vop_src0",
					    "clk_hdmiphy_pixel_io" };
PNAME(mclk_i2s0_2ch_sai_src_p)          = { "clk_i2s0_2ch_src",
					    "clk_i2s0_2ch_frac", "xin12m" };
PNAME(mclk_i2s1_8ch_sai_src_p)          = { "clk_i2s1_8ch_src",
					    "clk_i2s1_8ch_frac", "xin12m" };
PNAME(mclk_i2s2_2ch_sai_src_p)          = { "clk_i2s2_2ch_src",
					    "clk_i2s2_2ch_frac", "xin12m" };
PNAME(mclk_i2s3_8ch_sai_src_p)          = { "clk_i2s3_8ch_src",
					    "clk_i2s3_8ch_frac", "xin12m" };
PNAME(mclk_sai_i2s0_p)                  = { "mclk_i2s0_2ch_sai_src",
					    "i2s0_mclkin" };
PNAME(mclk_sai_i2s1_p)                  = { "mclk_i2s1_8ch_sai_src",
					    "i2s1_mclkin" };
PNAME(mclk_spdif_src_p)                 = { "clk_spdif_src", "clk_spdif_frac",
					    "xin12m" };
PNAME(sclk_uart0_src_p)                 = { "clk_uart0_src", "clk_uart0_frac",
					    "xin24m" };
PNAME(sclk_uart1_src_p)                 = { "clk_uart1_src", "clk_uart1_frac",
					    "xin24m" };
PNAME(sclk_uart2_src_p)                 = { "clk_uart2_src", "clk_uart2_frac",
					    "xin24m" };
PNAME(sclk_uart3_src_p)                 = { "clk_uart3_src", "clk_uart3_frac",
					    "xin24m" };
PNAME(sclk_uart4_src_p)                 = { "clk_uart4_src", "clk_uart4_frac",
					    "xin24m" };
PNAME(sclk_uart5_src_p)                 = { "clk_uart5_src", "clk_uart5_frac",
					    "xin24m" };
PNAME(sclk_uart6_src_p)                 = { "clk_uart6_src", "clk_uart6_frac",
					     "xin24m" };
PNAME(sclk_uart7_src_p)                 = { "clk_uart7_src", "clk_uart7_frac",
					    "xin24m" };
PNAME(clk_32k_p)                        = { "xin_osc0_div", "clk_pvtm_32k" };

static struct rockchip_pll_clock rk3528_pll_clks[] __initdata = {
	[apll] = PLL(pll_rk3328, PLL_APLL, "apll", mux_pll_p,
			CLK_IS_CRITICAL, RK3528_PLL_CON(0),
			RK3528_MODE_CON, 0, 0, 0, rk3528_pll_rates),

	[cpll] = PLL(pll_rk3328, PLL_CPLL, "cpll", mux_pll_p,
			CLK_IS_CRITICAL, RK3528_PLL_CON(8),
			RK3528_MODE_CON, 2, 0, 0, rk3528_pll_rates),

	[gpll] = PLL(pll_rk3328, PLL_GPLL, "gpll", mux_pll_p,
			CLK_IS_CRITICAL, RK3528_PLL_CON(24),
			RK3528_MODE_CON, 4, 0, 0, rk3528_pll_rates),

	[ppll] = PLL(pll_rk3328, PLL_PPLL, "ppll", mux_pll_p,
			CLK_IS_CRITICAL, RK3528_PCIE_PLL_CON(32),
			RK3528_MODE_CON, 6, 0, ROCKCHIP_PLL_FIXED_MODE, rk3528_pll_rates),

	[dpll] = PLL(pll_rk3328, PLL_DPLL, "dpll", mux_pll_p,
			CLK_IGNORE_UNUSED, RK3528_DDRPHY_PLL_CON(16),
			RK3528_DDRPHY_MODE_CON, 0, 0, 0, rk3528_pll_rates),
};

#define MFLAGS CLK_MUX_HIWORD_MASK
#define DFLAGS CLK_DIVIDER_HIWORD_MASK
#define GFLAGS (CLK_GATE_HIWORD_MASK | CLK_GATE_SET_TO_DISABLE)

static struct rockchip_clk_branch rk3528_uart0_fracmux __initdata =
	MUX(CLK_UART0, "clk_uart0", sclk_uart0_src_p, CLK_SET_RATE_PARENT,
			RK3528_CLKSEL_CON(6), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3528_uart1_fracmux __initdata =
	MUX(CLK_UART1, "clk_uart1", sclk_uart1_src_p, CLK_SET_RATE_PARENT,
			RK3528_CLKSEL_CON(8), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3528_uart2_fracmux __initdata =
	MUX(CLK_UART2, "clk_uart2", sclk_uart2_src_p, CLK_SET_RATE_PARENT,
			RK3528_CLKSEL_CON(10), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3528_uart3_fracmux __initdata =
	MUX(CLK_UART3, "clk_uart3", sclk_uart3_src_p, CLK_SET_RATE_PARENT,
			RK3528_CLKSEL_CON(12), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3528_uart4_fracmux __initdata =
	MUX(CLK_UART4, "clk_uart4", sclk_uart4_src_p, CLK_SET_RATE_PARENT,
			RK3528_CLKSEL_CON(14), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3528_uart5_fracmux __initdata =
	MUX(CLK_UART5, "clk_uart5", sclk_uart5_src_p, CLK_SET_RATE_PARENT,
			RK3528_CLKSEL_CON(16), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3528_uart6_fracmux __initdata =
	MUX(CLK_UART6, "clk_uart6", sclk_uart6_src_p, CLK_SET_RATE_PARENT,
			RK3528_CLKSEL_CON(18), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3528_uart7_fracmux __initdata =
	MUX(CLK_UART7, "clk_uart7", sclk_uart7_src_p, CLK_SET_RATE_PARENT,
			RK3528_CLKSEL_CON(20), 0, 2, MFLAGS);

static struct rockchip_clk_branch mclk_i2s0_2ch_sai_src_fracmux __initdata =
	MUX(MCLK_I2S0_2CH_SAI_SRC_PRE, "mclk_i2s0_2ch_sai_src_pre", mclk_i2s0_2ch_sai_src_p, CLK_SET_RATE_PARENT,
			RK3528_CLKSEL_CON(22), 0, 2, MFLAGS);

static struct rockchip_clk_branch mclk_i2s1_8ch_sai_src_fracmux __initdata =
	MUX(MCLK_I2S1_8CH_SAI_SRC_PRE, "mclk_i2s1_8ch_sai_src_pre", mclk_i2s1_8ch_sai_src_p, CLK_SET_RATE_PARENT,
			RK3528_CLKSEL_CON(26), 0, 2, MFLAGS);

static struct rockchip_clk_branch mclk_i2s2_2ch_sai_src_fracmux __initdata =
	MUX(MCLK_I2S2_2CH_SAI_SRC_PRE, "mclk_i2s2_2ch_sai_src_pre", mclk_i2s2_2ch_sai_src_p, CLK_SET_RATE_PARENT,
			RK3528_CLKSEL_CON(28), 0, 2, MFLAGS);

static struct rockchip_clk_branch mclk_i2s3_8ch_sai_src_fracmux __initdata =
	MUX(MCLK_I2S3_8CH_SAI_SRC_PRE, "mclk_i2s3_8ch_sai_src_pre", mclk_i2s3_8ch_sai_src_p, CLK_SET_RATE_PARENT,
			RK3528_CLKSEL_CON(24), 0, 2, MFLAGS);

static struct rockchip_clk_branch mclk_spdif_src_fracmux __initdata =
	MUX(MCLK_SDPDIF_SRC_PRE, "mclk_spdif_src_pre", mclk_spdif_src_p, CLK_SET_RATE_PARENT,
			RK3528_CLKSEL_CON(32), 0, 2, MFLAGS);

static struct rockchip_clk_branch rk3528_clk_branches[] __initdata = {
	/* top */
	FACTOR(0, "xin12m", "xin24m", 0, 1, 2),

	COMPOSITE(CLK_MATRIX_250M_SRC, "clk_250m_src", mux_gpll_cpll_p, CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(1), 15, 1, MFLAGS, 10, 5, DFLAGS,
			RK3528_CLKGATE_CON(0), 5, GFLAGS),
	COMPOSITE(CLK_MATRIX_500M_SRC, "clk_500m_src", mux_gpll_cpll_p, CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(3), 11, 1, MFLAGS, 6, 5, DFLAGS,
			RK3528_CLKGATE_CON(0), 10, GFLAGS),
	COMPOSITE_NOMUX(CLK_MATRIX_50M_SRC, "clk_50m_src", "cpll", CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(0), 2, 5, DFLAGS,
			RK3528_CLKGATE_CON(0), 1, GFLAGS),
	COMPOSITE_NOMUX(CLK_MATRIX_100M_SRC, "clk_100m_src", "cpll", CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(0), 7, 5, DFLAGS,
			RK3528_CLKGATE_CON(0), 2, GFLAGS),
	COMPOSITE_NOMUX(CLK_MATRIX_150M_SRC, "clk_150m_src", "gpll", CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(1), 0, 5, DFLAGS,
			RK3528_CLKGATE_CON(0), 3, GFLAGS),
	COMPOSITE_NOMUX(CLK_MATRIX_200M_SRC, "clk_200m_src", "gpll", CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(1), 5, 5, DFLAGS,
			RK3528_CLKGATE_CON(0), 4, GFLAGS),
	COMPOSITE_NOMUX(CLK_MATRIX_300M_SRC, "clk_300m_src", "gpll", CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(2), 0, 5, DFLAGS,
			RK3528_CLKGATE_CON(0), 6, GFLAGS),
	COMPOSITE_NOMUX_HALFDIV(CLK_MATRIX_339M_SRC, "clk_339m_src", "gpll", CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(2), 5, 5, DFLAGS,
			RK3528_CLKGATE_CON(0), 7, GFLAGS),
	COMPOSITE_NOMUX(CLK_MATRIX_400M_SRC, "clk_400m_src", "gpll", CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(2), 10, 5, DFLAGS,
			RK3528_CLKGATE_CON(0), 8, GFLAGS),
	COMPOSITE_NOMUX(CLK_MATRIX_600M_SRC, "clk_600m_src", "gpll", CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(4), 0, 5, DFLAGS,
			RK3528_CLKGATE_CON(0), 11, GFLAGS),
	COMPOSITE(DCLK_VOP_SRC0, "dclk_vop_src0", mux_gpll_cpll_p, 0,
			RK3528_CLKSEL_CON(32), 10, 1, MFLAGS, 2, 8, DFLAGS,
			RK3528_CLKGATE_CON(3), 7, GFLAGS),
	COMPOSITE(DCLK_VOP_SRC1, "dclk_vop_src1", mux_gpll_cpll_p, 0,
			RK3528_CLKSEL_CON(33), 8, 1, MFLAGS, 0, 8, DFLAGS,
			RK3528_CLKGATE_CON(3), 8, GFLAGS),
	COMPOSITE_NOMUX(CLK_HSM, "clk_hsm", "xin24m", 0,
			RK3528_CLKSEL_CON(36), 5, 5, DFLAGS,
			RK3528_CLKGATE_CON(3), 13, GFLAGS),

	COMPOSITE_NOMUX(CLK_UART0_SRC, "clk_uart0_src", "gpll", 0,
			RK3528_CLKSEL_CON(4), 5, 5, DFLAGS,
			RK3528_CLKGATE_CON(0), 12, GFLAGS),
	COMPOSITE_FRACMUX(CLK_UART0_FRAC, "clk_uart0_frac", "clk_uart0_src", CLK_SET_RATE_PARENT,
			RK3528_CLKSEL_CON(5), 0,
			RK3528_CLKGATE_CON(0), 13, GFLAGS,
			&rk3528_uart0_fracmux),
	GATE(SCLK_UART0, "sclk_uart0", "clk_uart0", 0,
			RK3528_CLKGATE_CON(0), 14, GFLAGS),

	COMPOSITE_NOMUX(CLK_UART1_SRC, "clk_uart1_src", "gpll", 0,
			RK3528_CLKSEL_CON(6), 2, 5, DFLAGS,
			RK3528_CLKGATE_CON(0), 15, GFLAGS),
	COMPOSITE_FRACMUX(CLK_UART1_FRAC, "clk_uart1_frac", "clk_uart1_src", CLK_SET_RATE_PARENT,
			RK3528_CLKSEL_CON(7), 0,
			RK3528_CLKGATE_CON(1), 0, GFLAGS,
			&rk3528_uart1_fracmux),
	GATE(SCLK_UART1, "sclk_uart1", "clk_uart1", 0,
			RK3528_CLKGATE_CON(1), 1, GFLAGS),

	COMPOSITE_NOMUX(CLK_UART2_SRC, "clk_uart2_src", "gpll", 0,
			RK3528_CLKSEL_CON(8), 2, 5, DFLAGS,
			RK3528_CLKGATE_CON(1), 2, GFLAGS),
	COMPOSITE_FRACMUX(CLK_UART2_FRAC, "clk_uart2_frac", "clk_uart2_src", CLK_SET_RATE_PARENT,
			RK3528_CLKSEL_CON(9), 0,
			RK3528_CLKGATE_CON(1), 3, GFLAGS,
			&rk3528_uart2_fracmux),
	GATE(SCLK_UART2, "sclk_uart2", "clk_uart2", 0,
			RK3528_CLKGATE_CON(1), 4, GFLAGS),

	COMPOSITE_NOMUX(CLK_UART3_SRC, "clk_uart3_src", "gpll", 0,
			RK3528_CLKSEL_CON(10), 2, 5, DFLAGS,
			RK3528_CLKGATE_CON(1), 5, GFLAGS),
	COMPOSITE_FRACMUX(CLK_UART3_FRAC, "clk_uart3_frac", "clk_uart3_src", CLK_SET_RATE_PARENT,
			RK3528_CLKSEL_CON(11), 0,
			RK3528_CLKGATE_CON(1), 6, GFLAGS,
			&rk3528_uart3_fracmux),
	GATE(SCLK_UART3, "sclk_uart3", "clk_uart3", 0,
			RK3528_CLKGATE_CON(1), 7, GFLAGS),

	COMPOSITE_NOMUX(CLK_UART4_SRC, "clk_uart4_src", "gpll", 0,
			RK3528_CLKSEL_CON(12), 2, 5, DFLAGS,
			RK3528_CLKGATE_CON(1), 8, GFLAGS),
	COMPOSITE_FRACMUX(CLK_UART4_FRAC, "clk_uart4_frac", "clk_uart4_src", CLK_SET_RATE_PARENT,
			RK3528_CLKSEL_CON(13), 0,
			RK3528_CLKGATE_CON(1), 9, GFLAGS,
			&rk3528_uart4_fracmux),
	GATE(SCLK_UART4, "sclk_uart4", "clk_uart4", 0,
			RK3528_CLKGATE_CON(1), 10, GFLAGS),

	COMPOSITE_NOMUX(CLK_UART5_SRC, "clk_uart5_src", "gpll", 0,
			RK3528_CLKSEL_CON(14), 2, 5, DFLAGS,
			RK3528_CLKGATE_CON(1), 11, GFLAGS),
	COMPOSITE_FRACMUX(CLK_UART5_FRAC, "clk_uart5_frac", "clk_uart5_src", CLK_SET_RATE_PARENT,
			RK3528_CLKSEL_CON(15), 0,
			RK3528_CLKGATE_CON(1), 12, GFLAGS,
			&rk3528_uart5_fracmux),
	GATE(SCLK_UART5, "sclk_uart5", "clk_uart5", 0,
			RK3528_CLKGATE_CON(1), 13, GFLAGS),

	COMPOSITE_NOMUX(CLK_UART6_SRC, "clk_uart6_src", "gpll", 0,
			RK3528_CLKSEL_CON(16), 2, 5, DFLAGS,
			RK3528_CLKGATE_CON(1), 14, GFLAGS),
	COMPOSITE_FRACMUX(CLK_UART6_FRAC, "clk_uart6_frac", "clk_uart6_src", CLK_SET_RATE_PARENT,
			RK3528_CLKSEL_CON(17), 0,
			RK3528_CLKGATE_CON(1), 15, GFLAGS,
			&rk3528_uart6_fracmux),
	GATE(SCLK_UART6, "sclk_uart6", "clk_uart6", 0,
			RK3528_CLKGATE_CON(2), 0, GFLAGS),

	COMPOSITE_NOMUX(CLK_UART7_SRC, "clk_uart7_src", "gpll", 0,
			RK3528_CLKSEL_CON(18), 2, 5, DFLAGS,
			RK3528_CLKGATE_CON(2), 1, GFLAGS),
	COMPOSITE_FRACMUX(CLK_UART7_FRAC, "clk_uart7_frac", "clk_uart7_src", CLK_SET_RATE_PARENT,
			RK3528_CLKSEL_CON(19), 0,
			RK3528_CLKGATE_CON(2), 2, GFLAGS,
			&rk3528_uart7_fracmux),
	GATE(SCLK_UART7, "sclk_uart7", "clk_uart7", 0,
			RK3528_CLKGATE_CON(2), 3, GFLAGS),

	COMPOSITE_NOMUX(CLK_I2S0_2CH_SRC, "clk_i2s0_2ch_src", "gpll", 0,
			RK3528_CLKSEL_CON(20), 8, 5, DFLAGS,
			RK3528_CLKGATE_CON(2), 5, GFLAGS),
	COMPOSITE_FRACMUX(CLK_I2S0_2CH_FRAC, "clk_i2s0_2ch_frac", "clk_i2s0_2ch_src", CLK_SET_RATE_PARENT,
			RK3528_CLKSEL_CON(21), 0,
			RK3528_CLKGATE_CON(2), 6, GFLAGS,
			&mclk_i2s0_2ch_sai_src_fracmux),
	GATE(MCLK_I2S0_2CH_SAI_SRC, "mclk_i2s0_2ch_sai_src", "mclk_i2s0_2ch_sai_src_pre", 0,
			RK3528_CLKGATE_CON(2), 7, GFLAGS),

	COMPOSITE_NOMUX(CLK_I2S1_8CH_SRC, "clk_i2s1_8ch_src", "gpll", 0,
			RK3528_CLKSEL_CON(24), 3, 5, DFLAGS,
			RK3528_CLKGATE_CON(2), 11, GFLAGS),
	COMPOSITE_FRACMUX(CLK_I2S1_8CH_FRAC, "clk_i2s1_8ch_frac", "clk_i2s1_8ch_src", CLK_SET_RATE_PARENT,
			RK3528_CLKSEL_CON(25), 0,
			RK3528_CLKGATE_CON(2), 12, GFLAGS,
			&mclk_i2s1_8ch_sai_src_fracmux),
	GATE(MCLK_I2S1_8CH_SAI_SRC, "mclk_i2s1_8ch_sai_src", "mclk_i2s1_8ch_sai_src_pre", 0,
			RK3528_CLKGATE_CON(2), 13, GFLAGS),

	COMPOSITE_NOMUX(CLK_I2S2_2CH_SRC, "clk_i2s2_2ch_src", "gpll", 0,
			RK3528_CLKSEL_CON(26), 3, 5, DFLAGS,
			RK3528_CLKGATE_CON(2), 14, GFLAGS),
	COMPOSITE_FRACMUX(CLK_I2S2_2CH_FRAC, "clk_i2s2_2ch_frac", "clk_i2s2_2ch_src", CLK_SET_RATE_PARENT,
			RK3528_CLKSEL_CON(27), 0,
			RK3528_CLKGATE_CON(2), 15, GFLAGS,
			&mclk_i2s2_2ch_sai_src_fracmux),
	GATE(MCLK_I2S2_2CH_SAI_SRC, "mclk_i2s2_2ch_sai_src", "mclk_i2s2_2ch_sai_src_pre", 0,
			RK3528_CLKGATE_CON(3), 0, GFLAGS),

	COMPOSITE_NOMUX(CLK_I2S3_8CH_SRC, "clk_i2s3_8ch_src", "gpll", 0,
			RK3528_CLKSEL_CON(22), 3, 5, DFLAGS,
			RK3528_CLKGATE_CON(2), 8, GFLAGS),
	COMPOSITE_FRACMUX(CLK_I2S3_8CH_FRAC, "clk_i2s3_8ch_frac", "clk_i2s3_8ch_src", CLK_SET_RATE_PARENT,
			RK3528_CLKSEL_CON(23), 0,
			RK3528_CLKGATE_CON(2), 9, GFLAGS,
			&mclk_i2s3_8ch_sai_src_fracmux),
	GATE(MCLK_I2S3_8CH_SAI_SRC, "mclk_i2s3_8ch_sai_src", "mclk_i2s3_8ch_sai_src_pre", 0,
			RK3528_CLKGATE_CON(2), 10, GFLAGS),

	COMPOSITE_NOMUX(CLK_SPDIF_SRC, "clk_spdif_src", "gpll", 0,
			RK3528_CLKSEL_CON(30), 2, 5, DFLAGS,
			RK3528_CLKGATE_CON(3), 4, GFLAGS),
	COMPOSITE_FRACMUX(CLK_SPDIF_FRAC, "clk_spdif_frac", "clk_spdif_src", CLK_SET_RATE_PARENT,
			RK3528_CLKSEL_CON(31), 0,
			RK3528_CLKGATE_CON(3), 5, GFLAGS,
			&mclk_spdif_src_fracmux),
	GATE(MCLK_SPDIF_SRC, "mclk_spdif_src", "mclk_spdif_src_pre", 0,
			RK3528_CLKGATE_CON(3), 6, GFLAGS),

	/* bus */
	COMPOSITE_NODIV(ACLK_BUS_M_ROOT, "aclk_bus_m_root", mux_300m_200m_100m_24m_p, CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(43), 12, 2, MFLAGS,
			RK3528_CLKGATE_CON(8), 7, GFLAGS),
	GATE(ACLK_GIC, "aclk_gic", "aclk_bus_m_root", CLK_IS_CRITICAL,
			RK3528_CLKGATE_CON(9), 1, GFLAGS),

	COMPOSITE_NODIV(ACLK_BUS_ROOT, "aclk_bus_root", mux_200m_100m_24m_p, CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(43), 6, 2, MFLAGS,
			RK3528_CLKGATE_CON(8), 4, GFLAGS),
	GATE(ACLK_SPINLOCK, "aclk_spinlock", "aclk_bus_root", 0,
			RK3528_CLKGATE_CON(9), 2, GFLAGS),
	GATE(ACLK_DMAC, "aclk_dmac", "aclk_bus_root", 0,
			RK3528_CLKGATE_CON(9), 4, GFLAGS),
	GATE(ACLK_DCF, "aclk_dcf", "aclk_bus_root", 0,
			RK3528_CLKGATE_CON(11), 11, GFLAGS),
	COMPOSITE(ACLK_BUS_VOPGL_ROOT, "aclk_bus_vopgl_root", mux_gpll_cpll_p, CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(43), 3, 1, MFLAGS, 0, 3, DFLAGS,
			RK3528_CLKGATE_CON(8), 0, GFLAGS),
	COMPOSITE_NODIV(ACLK_BUS_H_ROOT, "aclk_bus_h_root", mux_500m_200m_100m_24m_p, CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(43), 4, 2, MFLAGS,
			RK3528_CLKGATE_CON(8), 2, GFLAGS),
	GATE(ACLK_DMA2DDR, "aclk_dma2ddr", "aclk_bus_h_root", 0,
			RK3528_CLKGATE_CON(10), 14, GFLAGS),

	COMPOSITE_NODIV(HCLK_BUS_ROOT, "hclk_bus_root", mux_200m_100m_50m_24m_p, CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(43), 8, 2, MFLAGS,
			RK3528_CLKGATE_CON(8), 5, GFLAGS),

	COMPOSITE_NODIV(PCLK_BUS_ROOT, "pclk_bus_root", mux_100m_50m_24m_p, CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(43), 10, 2, MFLAGS,
			RK3528_CLKGATE_CON(8), 6, GFLAGS),
	GATE(PCLK_DFT2APB, "pclk_dft2apb", "pclk_bus_root", 0,
			RK3528_CLKGATE_CON(8), 13, GFLAGS),
	GATE(PCLK_BUS_GRF, "pclk_bus_grf", "pclk_bus_root", CLK_IS_CRITICAL,
			RK3528_CLKGATE_CON(8), 15, GFLAGS),
	GATE(PCLK_TIMER, "pclk_timer", "pclk_bus_root", 0,
			RK3528_CLKGATE_CON(9), 5, GFLAGS),
	GATE(PCLK_JDBCK_DAP, "pclk_jdbck_dap", "pclk_bus_root", 0,
			RK3528_CLKGATE_CON(9), 12, GFLAGS),
	GATE(PCLK_WDT_NS, "pclk_wdt_ns", "pclk_bus_root", 0,
			RK3528_CLKGATE_CON(9), 15, GFLAGS),
	GATE(PCLK_UART0, "pclk_uart0", "pclk_bus_root", 0,
			RK3528_CLKGATE_CON(10), 7, GFLAGS),
	GATE(PCLK_PWM0, "pclk_pwm0", "pclk_bus_root", 0,
			RK3528_CLKGATE_CON(11), 4, GFLAGS),
	GATE(PCLK_PWM1, "pclk_pwm1", "pclk_bus_root", 0,
			RK3528_CLKGATE_CON(11), 7, GFLAGS),
	GATE(PCLK_DMA2DDR, "pclk_dma2ddr", "pclk_bus_root", 0,
			RK3528_CLKGATE_CON(10), 13, GFLAGS),
	GATE(PCLK_SCR, "pclk_scr", "pclk_bus_root", 0,
			RK3528_CLKGATE_CON(11), 10, GFLAGS),
	GATE(PCLK_INTMUX, "pclk_intmux", "pclk_bus_root", CLK_IGNORE_UNUSED,
			RK3528_CLKGATE_CON(11), 12, GFLAGS),

	COMPOSITE_NODIV(CLK_PWM0, "clk_pwm0", mux_100m_50m_24m_p, 0,
			RK3528_CLKSEL_CON(44), 6, 2, MFLAGS,
			RK3528_CLKGATE_CON(11), 5, GFLAGS),
	COMPOSITE_NODIV(CLK_PWM1, "clk_pwm1", mux_100m_50m_24m_p, 0,
			RK3528_CLKSEL_CON(44), 8, 2, MFLAGS,
			RK3528_CLKGATE_CON(11), 8, GFLAGS),

	GATE(CLK_CAPTURE_PWM1, "clk_capture_pwm1", "xin24m", 0,
			RK3528_CLKGATE_CON(11), 9, GFLAGS),
	GATE(CLK_CAPTURE_PWM0, "clk_capture_pwm0", "xin24m", 0,
			RK3528_CLKGATE_CON(11), 6, GFLAGS),
	GATE(CLK_JDBCK_DAP, "clk_jdbck_dap", "xin24m", 0,
			RK3528_CLKGATE_CON(9), 13, GFLAGS),
	GATE(TCLK_WDT_NS, "tclk_wdt_ns", "xin24m", 0,
			RK3528_CLKGATE_CON(10), 0, GFLAGS),

	GATE(CLK_TIMER_ROOT, "clk_timer_root", "xin24m", 0,
			RK3528_CLKGATE_CON(8), 9, GFLAGS),
	GATE(CLK_TIMER0, "clk_timer0", "clk_timer_root", 0,
			RK3528_CLKGATE_CON(9), 6, GFLAGS),
	GATE(CLK_TIMER1, "clk_timer1", "clk_timer_root", 0,
			RK3528_CLKGATE_CON(9), 7, GFLAGS),
	GATE(CLK_TIMER2, "clk_timer2", "clk_timer_root", 0,
			RK3528_CLKGATE_CON(9), 8, GFLAGS),
	GATE(CLK_TIMER3, "clk_timer3", "clk_timer_root", 0,
			RK3528_CLKGATE_CON(9), 9, GFLAGS),
	GATE(CLK_TIMER4, "clk_timer4", "clk_timer_root", 0,
			RK3528_CLKGATE_CON(9), 10, GFLAGS),
	GATE(CLK_TIMER5, "clk_timer5", "clk_timer_root", 0,
			RK3528_CLKGATE_CON(9), 11, GFLAGS),

	/* pmu */
	GATE(HCLK_PMU_ROOT, "hclk_pmu_root", "clk_100m_src", CLK_IGNORE_UNUSED,
			RK3528_PMU_CLKGATE_CON(0), 1, GFLAGS),
	GATE(PCLK_PMU_ROOT, "pclk_pmu_root", "clk_100m_src", CLK_IGNORE_UNUSED,
			RK3528_PMU_CLKGATE_CON(0), 0, GFLAGS),

	GATE(FCLK_MCU, "fclk_mcu", "hclk_pmu_root", 0,
			RK3528_PMU_CLKGATE_CON(0), 7, GFLAGS),
	GATE(HCLK_PMU_SRAM, "hclk_pmu_sram", "hclk_pmu_root", CLK_IS_CRITICAL,
			RK3528_PMU_CLKGATE_CON(5), 4, GFLAGS),

	GATE(PCLK_I2C2, "pclk_i2c2", "pclk_pmu_root", 0,
			RK3528_PMU_CLKGATE_CON(0), 2, GFLAGS),
	GATE(PCLK_PMU_HP_TIMER, "pclk_pmu_hp_timer", "pclk_pmu_root", 0,
			RK3528_PMU_CLKGATE_CON(1), 2, GFLAGS),
	GATE(PCLK_PMU_IOC, "pclk_pmu_ioc", "pclk_pmu_root", CLK_IS_CRITICAL,
			RK3528_PMU_CLKGATE_CON(1), 5, GFLAGS),
	GATE(PCLK_PMU_CRU, "pclk_pmu_cru", "pclk_pmu_root", CLK_IS_CRITICAL,
			RK3528_PMU_CLKGATE_CON(1), 6, GFLAGS),
	GATE(PCLK_PMU_GRF, "pclk_pmu_grf", "pclk_pmu_root", CLK_IS_CRITICAL,
			RK3528_PMU_CLKGATE_CON(1), 7, GFLAGS),
	GATE(PCLK_PMU_WDT, "pclk_pmu_wdt", "pclk_pmu_root", 0,
			RK3528_PMU_CLKGATE_CON(1), 10, GFLAGS),
	GATE(PCLK_PMU, "pclk_pmu", "pclk_pmu_root", CLK_IS_CRITICAL,
			RK3528_PMU_CLKGATE_CON(0), 13, GFLAGS),
	GATE(PCLK_GPIO0, "pclk_gpio0", "pclk_pmu_root", 0,
			RK3528_PMU_CLKGATE_CON(0), 14, GFLAGS),
	GATE(PCLK_OSCCHK, "pclk_oscchk", "pclk_pmu_root", 0,
			RK3528_PMU_CLKGATE_CON(0), 9, GFLAGS),
	GATE(PCLK_PMU_MAILBOX, "pclk_pmu_mailbox", "pclk_pmu_root", 0,
			RK3528_PMU_CLKGATE_CON(1), 12, GFLAGS),
	GATE(PCLK_SCRKEYGEN, "pclk_scrkeygen", "pclk_pmu_root", 0,
			RK3528_PMU_CLKGATE_CON(1), 15, GFLAGS),
	GATE(PCLK_PVTM_PMU, "pclk_pvtm_pmu", "pclk_pmu_root", 0,
			RK3528_PMU_CLKGATE_CON(5), 1, GFLAGS),

	COMPOSITE_NODIV(CLK_I2C2, "clk_i2c2", clk_i2c2_p, 0,
			RK3528_PMU_CLKSEL_CON(0), 0, 2, MFLAGS,
			RK3528_PMU_CLKGATE_CON(0), 3, GFLAGS),

	GATE(CLK_REFOUT, "clk_refout", "xin24m", 0,
			RK3528_PMU_CLKGATE_CON(2), 4, GFLAGS),
	COMPOSITE_NOMUX(CLK_PVTM_PMU, "clk_pvtm_pmu", "xin24m", 0,
			RK3528_PMU_CLKSEL_CON(5), 0, 5, DFLAGS,
			RK3528_PMU_CLKGATE_CON(5), 0, GFLAGS),

	COMPOSITE_FRAC(XIN_OSC0_DIV, "xin_osc0_div", "xin24m", 0,
			RK3528_PMU_CLKSEL_CON(1), 0,
			RK3528_PMU_CLKGATE_CON(1), 0, GFLAGS),
	/* clk_32k: internal! No path from external osc 32k */
	MUX(CLK_DEEPSLOW, "clk_32k", clk_32k_p, CLK_IS_CRITICAL,
			RK3528_PMU_CLKSEL_CON(2), 0, 1, MFLAGS),
	GATE(RTC_CLK_MCU, "rtc_clk_mcu", "clk_32k", 0,
			RK3528_PMU_CLKGATE_CON(0), 8, GFLAGS),
	GATE(CLK_DDR_FAIL_SAFE, "clk_ddr_fail_safe", "xin24m", CLK_IGNORE_UNUSED,
			RK3528_PMU_CLKGATE_CON(1), 1, GFLAGS),

	COMPOSITE_NODIV(DBCLK_GPIO0, "dbclk_gpio0", mux_24m_32k_p, 0,
			RK3528_PMU_CLKSEL_CON(0), 2, 1, MFLAGS,
			RK3528_PMU_CLKGATE_CON(0), 15, GFLAGS),
	COMPOSITE_NODIV(TCLK_PMU_WDT, "tclk_pmu_wdt", mux_24m_32k_p, 0,
			RK3528_PMU_CLKSEL_CON(2), 1, 1, MFLAGS,
			RK3528_PMU_CLKGATE_CON(1), 11, GFLAGS),

	/* core */
	COMPOSITE_NOMUX(ACLK_M_CORE_BIU, "aclk_m_core", "armclk", CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(39), 11, 5, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RK3528_CLKGATE_CON(5), 12, GFLAGS),
	COMPOSITE_NOMUX(PCLK_DBG, "pclk_dbg", "armclk", CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(40), 1, 5, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RK3528_CLKGATE_CON(5), 13, GFLAGS),
	GATE(PCLK_CPU_ROOT, "pclk_cpu_root", "pclk_dbg", CLK_IS_CRITICAL,
			RK3528_CLKGATE_CON(6), 1, GFLAGS),
	GATE(PCLK_CORE_GRF, "pclk_core_grf", "pclk_cpu_root", CLK_IS_CRITICAL,
			RK3528_CLKGATE_CON(6), 2, GFLAGS),

	/* ddr */
	GATE(CLK_DDRC_SRC, "clk_ddrc_src", "dpll", CLK_IS_CRITICAL,
			RK3528_DDRPHY_CLKGATE_CON(0), 0, GFLAGS),
	GATE(CLK_DDR_PHY, "clk_ddr_phy", "dpll", CLK_IS_CRITICAL,
			RK3528_DDRPHY_CLKGATE_CON(0), 1, GFLAGS),

	COMPOSITE_NODIV(PCLK_DDR_ROOT, "pclk_ddr_root", mux_100m_50m_24m_p, CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(90), 0, 2, MFLAGS,
			RK3528_CLKGATE_CON(45), 0, GFLAGS),
	GATE(PCLK_DDRMON, "pclk_ddrmon", "pclk_ddr_root", CLK_IGNORE_UNUSED,
			RK3528_CLKGATE_CON(45), 3, GFLAGS),
	GATE(PCLK_DDR_HWLP, "pclk_ddr_hwlp", "pclk_ddr_root", CLK_IGNORE_UNUSED,
			RK3528_CLKGATE_CON(45), 8, GFLAGS),
	GATE(CLK_TIMER_DDRMON, "clk_timer_ddrmon", "xin24m", CLK_IGNORE_UNUSED,
			RK3528_CLKGATE_CON(45), 4, GFLAGS),

	GATE(PCLK_DDRC, "pclk_ddrc", "pclk_ddr_root", CLK_IS_CRITICAL,
			RK3528_CLKGATE_CON(45), 2, GFLAGS),
	GATE(PCLK_DDR_GRF, "pclk_ddr_grf", "pclk_ddr_root", CLK_IS_CRITICAL,
			RK3528_CLKGATE_CON(45), 6, GFLAGS),
	GATE(PCLK_DDRPHY, "pclk_ddrphy", "pclk_ddr_root", CLK_IS_CRITICAL,
			RK3528_CLKGATE_CON(45), 9, GFLAGS),

	GATE(ACLK_DDR_UPCTL, "aclk_ddr_upctl", "clk_ddrc_src", CLK_IS_CRITICAL,
			RK3528_CLKGATE_CON(45), 11, GFLAGS),
	GATE(CLK_DDR_UPCTL, "clk_ddr_upctl", "clk_ddrc_src", CLK_IS_CRITICAL,
			RK3528_CLKGATE_CON(45), 12, GFLAGS),
	GATE(CLK_DDRMON, "clk_ddrmon", "clk_ddrc_src", CLK_IS_CRITICAL,
			RK3528_CLKGATE_CON(45), 13, GFLAGS),
	GATE(ACLK_DDR_SCRAMBLE, "aclk_ddr_scramble", "clk_ddrc_src", CLK_IS_CRITICAL,
			RK3528_CLKGATE_CON(45), 14, GFLAGS),
	GATE(ACLK_SPLIT, "aclk_split", "clk_ddrc_src", CLK_IS_CRITICAL,
			RK3528_CLKGATE_CON(45), 15, GFLAGS),

	/* gpu */
	COMPOSITE_NODIV(ACLK_GPU_ROOT, "aclk_gpu_root", mux_500m_300m_100m_24m_p, CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(76), 0, 2, MFLAGS,
			RK3528_CLKGATE_CON(34), 0, GFLAGS),
	COMPOSITE_NODIV(ACLK_GPU, "aclk_gpu", aclk_gpu_p, CLK_SET_RATE_PARENT,
			RK3528_CLKSEL_CON(76), 6, 1, MFLAGS,
			RK3528_CLKGATE_CON(34), 7, GFLAGS),
	GATE(ACLK_GPU_MALI, "aclk_gpu_mali", "aclk_gpu", 0,
			RK3528_CLKGATE_CON(34), 8, GFLAGS),
	COMPOSITE_NODIV(PCLK_GPU_ROOT, "pclk_gpu_root", mux_100m_50m_24m_p, CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(76), 4, 2, MFLAGS,
			RK3528_CLKGATE_CON(34), 2, GFLAGS),

	/* rkvdec */
	COMPOSITE_NODIV(ACLK_RKVDEC_ROOT_NDFT, "aclk_rkvdec_root", mux_339m_200m_100m_24m_p, CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(88), 6, 2, MFLAGS,
			RK3528_CLKGATE_CON(44), 3, GFLAGS),
	COMPOSITE_NODIV(HCLK_RKVDEC_ROOT, "hclk_rkvdec_root", mux_200m_100m_50m_24m_p, CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(88), 4, 2, MFLAGS,
			RK3528_CLKGATE_CON(44), 2, GFLAGS),
	GATE(PCLK_DDRPHY_CRU, "pclk_ddrphy_cru", "hclk_rkvdec_root", CLK_IS_CRITICAL,
			RK3528_CLKGATE_CON(44), 4, GFLAGS),
	GATE(HCLK_RKVDEC, "hclk_rkvdec", "hclk_rkvdec_root", 0,
			RK3528_CLKGATE_CON(44), 9, GFLAGS),
	COMPOSITE_NODIV(CLK_HEVC_CA_RKVDEC, "clk_hevc_ca_rkvdec", mux_600m_300m_200m_24m_p, 0,
			RK3528_CLKSEL_CON(88), 11, 2, MFLAGS,
			RK3528_CLKGATE_CON(44), 11, GFLAGS),
	MUX(ACLK_RKVDEC_PVTMUX_ROOT, "aclk_rkvdec_pvtmux_root", aclk_rkvdec_pvtmux_root_p, CLK_IS_CRITICAL | CLK_SET_RATE_PARENT,
			RK3528_CLKSEL_CON(88), 13, 1, MFLAGS),
	GATE(ACLK_RKVDEC, "aclk_rkvdec", "aclk_rkvdec_pvtmux_root", 0,
			RK3528_CLKGATE_CON(44), 8, GFLAGS),

	/* rkvenc */
	COMPOSITE_NODIV(ACLK_RKVENC_ROOT, "aclk_rkvenc_root", mux_300m_200m_100m_24m_p, CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(79), 2, 2, MFLAGS,
			RK3528_CLKGATE_CON(36), 1, GFLAGS),
	GATE(ACLK_RKVENC, "aclk_rkvenc", "aclk_rkvenc_root", 0,
			RK3528_CLKGATE_CON(36), 7, GFLAGS),

	COMPOSITE_NODIV(PCLK_RKVENC_ROOT, "pclk_rkvenc_root", mux_100m_50m_24m_p, CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(79), 4, 2, MFLAGS,
			RK3528_CLKGATE_CON(36), 2, GFLAGS),
	GATE(PCLK_RKVENC_IOC, "pclk_rkvenc_ioc", "pclk_rkvenc_root", CLK_IS_CRITICAL,
			RK3528_CLKGATE_CON(37), 10, GFLAGS),
	GATE(PCLK_RKVENC_GRF, "pclk_rkvenc_grf", "pclk_rkvenc_root", CLK_IS_CRITICAL,
			RK3528_CLKGATE_CON(38), 6, GFLAGS),
	GATE(PCLK_I2C1, "pclk_i2c1", "pclk_rkvenc_root", 0,
			RK3528_CLKGATE_CON(36), 11, GFLAGS),
	GATE(PCLK_I2C0, "pclk_i2c0", "pclk_rkvenc_root", 0,
			RK3528_CLKGATE_CON(36), 13, GFLAGS),
	GATE(PCLK_SPI0, "pclk_spi0", "pclk_rkvenc_root", 0,
			RK3528_CLKGATE_CON(37), 2, GFLAGS),
	GATE(PCLK_GPIO4, "pclk_gpio4", "pclk_rkvenc_root", 0,
			RK3528_CLKGATE_CON(37), 8, GFLAGS),
	GATE(PCLK_UART1, "pclk_uart1", "pclk_rkvenc_root", 0,
			RK3528_CLKGATE_CON(38), 2, GFLAGS),
	GATE(PCLK_UART3, "pclk_uart3", "pclk_rkvenc_root", 0,
			RK3528_CLKGATE_CON(38), 4, GFLAGS),
	GATE(PCLK_CAN0, "pclk_can0", "pclk_rkvenc_root", 0,
			RK3528_CLKGATE_CON(38), 7, GFLAGS),
	GATE(PCLK_CAN1, "pclk_can1", "pclk_rkvenc_root", 0,
			RK3528_CLKGATE_CON(38), 9, GFLAGS),

	COMPOSITE_NODIV(MCLK_PDM, "mclk_pdm", mux_150m_100m_24m_p, 0,
			RK3528_CLKSEL_CON(80), 12, 2, MFLAGS,
			RK3528_CLKGATE_CON(38), 1, GFLAGS),
	COMPOSITE(CLK_CAN0, "clk_can0", mux_gpll_cpll_p, 0,
			RK3528_CLKSEL_CON(81), 6, 1, MFLAGS, 0, 6, DFLAGS,
			RK3528_CLKGATE_CON(38), 8, GFLAGS),
	COMPOSITE(CLK_CAN1, "clk_can1", mux_gpll_cpll_p, 0,
			RK3528_CLKSEL_CON(81), 13, 1, MFLAGS, 7, 6, DFLAGS,
			RK3528_CLKGATE_CON(38), 10, GFLAGS),

	COMPOSITE_NODIV(HCLK_RKVENC_ROOT, "hclk_rkvenc_root", mux_200m_100m_50m_24m_p, CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(79), 0, 2, MFLAGS,
			RK3528_CLKGATE_CON(36), 0, GFLAGS),
	GATE(HCLK_SAI_I2S1, "hclk_sai_i2s1", "hclk_rkvenc_root", 0,
			RK3528_CLKGATE_CON(36), 9, GFLAGS),
	GATE(HCLK_SPDIF, "hclk_spdif", "hclk_rkvenc_root", 0,
			RK3528_CLKGATE_CON(37), 14, GFLAGS),
	GATE(HCLK_PDM, "hclk_pdm", "hclk_rkvenc_root", 0,
			RK3528_CLKGATE_CON(38), 0, GFLAGS),
	GATE(HCLK_RKVENC, "hclk_rkvenc", "hclk_rkvenc_root", 0,
			RK3528_CLKGATE_CON(36), 6, GFLAGS),

	COMPOSITE_NODIV(CLK_CORE_RKVENC, "clk_core_rkvenc", mux_300m_200m_100m_24m_p, 0,
			RK3528_CLKSEL_CON(79), 6, 2, MFLAGS,
			RK3528_CLKGATE_CON(36), 8, GFLAGS),
	COMPOSITE_NODIV(CLK_I2C0, "clk_i2c0", mux_200m_100m_50m_24m_p, 0,
			RK3528_CLKSEL_CON(79), 11, 2, MFLAGS,
			RK3528_CLKGATE_CON(36), 14, GFLAGS),
	COMPOSITE_NODIV(CLK_I2C1, "clk_i2c1", mux_200m_100m_50m_24m_p, 0,
			RK3528_CLKSEL_CON(79), 9, 2, MFLAGS,
			RK3528_CLKGATE_CON(36), 12, GFLAGS),

	COMPOSITE_NODIV(CLK_SPI0, "clk_spi0", mux_200m_100m_50m_24m_p, 0,
			RK3528_CLKSEL_CON(79), 13, 2, MFLAGS,
			RK3528_CLKGATE_CON(37), 3, GFLAGS),
	COMPOSITE_NODIV(MCLK_SAI_I2S1, "mclk_sai_i2s1", mclk_sai_i2s1_p, CLK_SET_RATE_PARENT,
			RK3528_CLKSEL_CON(79), 8, 1, MFLAGS,
			RK3528_CLKGATE_CON(36), 10, GFLAGS),
	GATE(DBCLK_GPIO4, "dbclk_gpio4", "xin24m", 0,
			RK3528_CLKGATE_CON(37), 9, GFLAGS),

	/* vo */
	COMPOSITE_NODIV(HCLK_VO_ROOT, "hclk_vo_root", mux_150m_100m_24m_p, CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(83), 2, 2, MFLAGS,
			RK3528_CLKGATE_CON(39), 1, GFLAGS),
	GATE(HCLK_VOP, "hclk_vop", "hclk_vo_root", 0,
			RK3528_CLKGATE_CON(40), 2, GFLAGS),
	GATE(HCLK_USBHOST, "hclk_usbhost", "hclk_vo_root", 0,
			RK3528_CLKGATE_CON(43), 3, GFLAGS),
	GATE(HCLK_JPEG_DECODER, "hclk_jpeg_decoder", "hclk_vo_root", 0,
			RK3528_CLKGATE_CON(41), 7, GFLAGS),
	GATE(HCLK_VDPP, "hclk_vdpp", "hclk_vo_root", 0,
			RK3528_CLKGATE_CON(39), 10, GFLAGS),
	GATE(HCLK_CVBS, "hclk_cvbs", "hclk_vo_root", 0,
			RK3528_CLKGATE_CON(41), 3, GFLAGS),
	GATE(HCLK_USBHOST_ARB, "hclk_usbhost_arb", "hclk_vo_root", 0,
			RK3528_CLKGATE_CON(43), 4, GFLAGS),
	GATE(HCLK_SAI_I2S3, "hclk_sai_i2s3", "hclk_vo_root", 0,
			RK3528_CLKGATE_CON(42), 1, GFLAGS),
	GATE(HCLK_HDCP, "hclk_hdcp", "hclk_vo_root", 0,
			RK3528_CLKGATE_CON(41), 1, GFLAGS),
	GATE(HCLK_RGA2E, "hclk_rga2e", "hclk_vo_root", 0,
			RK3528_CLKGATE_CON(39), 7, GFLAGS),
	GATE(HCLK_SDMMC0, "hclk_sdmmc0", "hclk_vo_root", 0,
			RK3528_CLKGATE_CON(42), 9, GFLAGS),
	GATE(HCLK_HDCP_KEY, "hclk_hdcp_key", "hclk_vo_root", 0,
			RK3528_CLKGATE_CON(40), 15, GFLAGS),

	COMPOSITE_NODIV(ACLK_VO_L_ROOT, "aclk_vo_l_root", mux_150m_100m_24m_p, CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(84), 1, 2, MFLAGS,
			RK3528_CLKGATE_CON(41), 8, GFLAGS),
	GATE(ACLK_MAC_VO, "aclk_gmac0", "aclk_vo_l_root", 0,
			RK3528_CLKGATE_CON(41), 10, GFLAGS),

	COMPOSITE_NODIV(PCLK_VO_ROOT, "pclk_vo_root", mux_100m_50m_24m_p, CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(83), 4, 2, MFLAGS,
			RK3528_CLKGATE_CON(39), 2, GFLAGS),
	GATE(PCLK_MAC_VO, "pclk_gmac0", "pclk_vo_root", 0,
			RK3528_CLKGATE_CON(41), 11, GFLAGS),
	GATE(PCLK_VCDCPHY, "pclk_vcdcphy", "pclk_vo_root", 0,
			RK3528_CLKGATE_CON(42), 4, GFLAGS),
	GATE(PCLK_GPIO2, "pclk_gpio2", "pclk_vo_root", 0,
			RK3528_CLKGATE_CON(42), 5, GFLAGS),
	GATE(PCLK_VO_IOC, "pclk_vo_ioc", "pclk_vo_root", CLK_IS_CRITICAL,
			RK3528_CLKGATE_CON(42), 7, GFLAGS),
	GATE(PCLK_OTPC_NS, "pclk_otpc_ns", "pclk_vo_root", 0,
			RK3528_CLKGATE_CON(42), 11, GFLAGS),
	GATE(PCLK_UART4, "pclk_uart4", "pclk_vo_root", 0,
			RK3528_CLKGATE_CON(43), 7, GFLAGS),
	GATE(PCLK_I2C4, "pclk_i2c4", "pclk_vo_root", 0,
			RK3528_CLKGATE_CON(43), 9, GFLAGS),
	GATE(PCLK_I2C7, "pclk_i2c7", "pclk_vo_root", 0,
			RK3528_CLKGATE_CON(43), 11, GFLAGS),

	GATE(PCLK_USBPHY, "pclk_usbphy", "pclk_vo_root", 0,
			RK3528_CLKGATE_CON(43), 13, GFLAGS),

	GATE(PCLK_VO_GRF, "pclk_vo_grf", "pclk_vo_root", CLK_IS_CRITICAL,
			RK3528_CLKGATE_CON(39), 13, GFLAGS),
	GATE(PCLK_CRU, "pclk_cru", "pclk_vo_root", CLK_IS_CRITICAL,
			RK3528_CLKGATE_CON(39), 15, GFLAGS),
	GATE(PCLK_HDMI, "pclk_hdmi", "pclk_vo_root", 0,
			RK3528_CLKGATE_CON(40), 6, GFLAGS),
	GATE(PCLK_HDMIPHY, "pclk_hdmiphy", "pclk_vo_root", 0,
			RK3528_CLKGATE_CON(40), 14, GFLAGS),
	GATE(PCLK_HDCP, "pclk_hdcp", "pclk_vo_root", 0,
			RK3528_CLKGATE_CON(41), 2, GFLAGS),

	COMPOSITE_NODIV(CLK_CORE_VDPP, "clk_core_vdpp", mux_339m_200m_100m_24m_p, 0,
			RK3528_CLKSEL_CON(83), 10, 2, MFLAGS,
			RK3528_CLKGATE_CON(39), 12, GFLAGS),
	COMPOSITE_NODIV(CLK_CORE_RGA2E, "clk_core_rga2e", mux_339m_200m_100m_24m_p, 0,
			RK3528_CLKSEL_CON(83), 8, 2, MFLAGS,
			RK3528_CLKGATE_CON(39), 9, GFLAGS),
	COMPOSITE_NODIV(ACLK_JPEG_ROOT, "aclk_jpeg_root", mux_339m_200m_100m_24m_p, CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(84), 9, 2, MFLAGS,
			RK3528_CLKGATE_CON(41), 15, GFLAGS),
	GATE(ACLK_JPEG_DECODER, "aclk_jpeg_decoder", "aclk_jpeg_root", 0,
			RK3528_CLKGATE_CON(41), 6, GFLAGS),

	COMPOSITE_NODIV(ACLK_VO_ROOT, "aclk_vo_root", mux_339m_200m_100m_24m_p, CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(83), 0, 2, MFLAGS,
			RK3528_CLKGATE_CON(39), 0, GFLAGS),
	GATE(ACLK_RGA2E, "aclk_rga2e", "aclk_vo_root", 0,
			RK3528_CLKGATE_CON(39), 8, GFLAGS),
	GATE(ACLK_VDPP, "aclk_vdpp", "aclk_vo_root", 0,
			RK3528_CLKGATE_CON(39), 11, GFLAGS),
	GATE(ACLK_HDCP, "aclk_hdcp", "aclk_vo_root", 0,
			RK3528_CLKGATE_CON(41), 0, GFLAGS),

	COMPOSITE(CCLK_SRC_SDMMC0, "cclk_src_sdmmc0", mux_gpll_cpll_xin24m_p, 0,
			RK3528_CLKSEL_CON(85), 6, 2, MFLAGS, 0, 6, DFLAGS,
			RK3528_CLKGATE_CON(42), 8, GFLAGS),

	COMPOSITE(ACLK_VOP_ROOT, "aclk_vop_root", mux_gpll_cpll_p, CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(83), 15, 1, MFLAGS, 12, 3, DFLAGS,
			RK3528_CLKGATE_CON(40), 0, GFLAGS),
	GATE(ACLK_VOP, "aclk_vop", "aclk_vop_root", 0,
			RK3528_CLKGATE_CON(40), 5, GFLAGS),

	COMPOSITE_NODIV(CLK_I2C4, "clk_i2c4", mux_200m_100m_50m_24m_p, 0,
			RK3528_CLKSEL_CON(85), 13, 2, MFLAGS,
			RK3528_CLKGATE_CON(43), 10, GFLAGS),
	COMPOSITE_NODIV(CLK_I2C7, "clk_i2c7", mux_200m_100m_50m_24m_p, 0,
			RK3528_CLKSEL_CON(86), 0, 2, MFLAGS,
			RK3528_CLKGATE_CON(43), 12, GFLAGS),
	GATE(DBCLK_GPIO2, "dbclk_gpio2", "xin24m", 0,
			RK3528_CLKGATE_CON(42), 6, GFLAGS),

	GATE(CLK_HDMIHDP0, "clk_hdmihdp0", "xin24m", 0,
			RK3528_CLKGATE_CON(43), 2, GFLAGS),
	GATE(CLK_MACPHY, "clk_macphy", "xin24m", 0,
			RK3528_CLKGATE_CON(42), 3, GFLAGS),
	GATE(CLK_REF_USBPHY, "clk_ref_usbphy", "xin24m", 0,
			RK3528_CLKGATE_CON(43), 14, GFLAGS),
	GATE(CLK_SBPI_OTPC_NS, "clk_sbpi_otpc_ns", "xin24m", 0,
			RK3528_CLKGATE_CON(42), 12, GFLAGS),
	FACTOR(CLK_USER_OTPC_NS, "clk_user_otpc_ns", "clk_sbpi_otpc_ns",
			0, 1, 2),

	GATE(MCLK_SAI_I2S3, "mclk_sai_i2s3", "mclk_i2s3_8ch_sai_src", 0,
			RK3528_CLKGATE_CON(42), 2, GFLAGS),
	COMPOSITE_NODIV(DCLK_VOP0, "dclk_vop0", dclk_vop0_p, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			RK3528_CLKSEL_CON(84), 0, 1, MFLAGS,
			RK3528_CLKGATE_CON(40), 3, GFLAGS),
	GATE(DCLK_VOP1, "dclk_vop1", "dclk_vop_src1", CLK_SET_RATE_PARENT,
			RK3528_CLKGATE_CON(40), 4, GFLAGS),
	FACTOR_GATE(DCLK_CVBS, "dclk_cvbs", "dclk_vop1", 0, 1, 4,
			RK3528_CLKGATE_CON(41), 4, GFLAGS),
	GATE(DCLK_4X_CVBS, "dclk_4x_cvbs", "dclk_vop1", 0,
			RK3528_CLKGATE_CON(41), 5, GFLAGS),

	FACTOR_GATE(CLK_SFR_HDMI, "clk_sfr_hdmi", "dclk_vop_src1", 0, 1, 4,
			RK3528_CLKGATE_CON(40), 7, GFLAGS),

	GATE(CLK_SPDIF_HDMI, "clk_spdif_hdmi", "mclk_spdif_src", 0,
			RK3528_CLKGATE_CON(40), 10, GFLAGS),
	GATE(MCLK_SPDIF, "mclk_spdif", "mclk_spdif_src", 0,
			RK3528_CLKGATE_CON(37), 15, GFLAGS),
	GATE(CLK_CEC_HDMI, "clk_cec_hdmi", "clk_32k", 0,
			RK3528_CLKGATE_CON(40), 8, GFLAGS),

	/* vpu */
	GATE(DBCLK_GPIO1, "dbclk_gpio1", "xin24m", 0,
			RK3528_CLKGATE_CON(26), 5, GFLAGS),
	GATE(DBCLK_GPIO3, "dbclk_gpio3", "xin24m", 0,
			RK3528_CLKGATE_CON(27), 1, GFLAGS),
	GATE(CLK_SUSPEND_USB3OTG, "clk_suspend_usb3otg", "xin24m", 0,
			RK3528_CLKGATE_CON(33), 4, GFLAGS),
	GATE(CLK_PCIE_AUX, "clk_pcie_aux", "xin24m", 0,
			RK3528_CLKGATE_CON(30), 2, GFLAGS),
	GATE(TCLK_EMMC, "tclk_emmc", "xin24m", 0,
			RK3528_CLKGATE_CON(26), 3, GFLAGS),
	GATE(CLK_REF_USB3OTG, "clk_ref_usb3otg", "xin24m", 0,
			RK3528_CLKGATE_CON(33), 2, GFLAGS),
	COMPOSITE(CCLK_SRC_SDIO0, "cclk_src_sdio0", mux_gpll_cpll_xin24m_p, 0,
			RK3528_CLKSEL_CON(72), 6, 2, MFLAGS, 0, 6, DFLAGS,
			RK3528_CLKGATE_CON(32), 1, GFLAGS),

	COMPOSITE_NODIV(PCLK_VPU_ROOT, "pclk_vpu_root", mux_100m_50m_24m_p, CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(61), 4, 2, MFLAGS,
			RK3528_CLKGATE_CON(25), 5, GFLAGS),
	GATE(PCLK_VPU_GRF, "pclk_vpu_grf", "pclk_vpu_root", CLK_IS_CRITICAL,
			RK3528_CLKGATE_CON(25), 12, GFLAGS),
	GATE(PCLK_CRU_PCIE, "pclk_cru_pcie", "pclk_vpu_root", CLK_IS_CRITICAL,
			RK3528_CLKGATE_CON(25), 11, GFLAGS),
	GATE(PCLK_UART6, "pclk_uart6", "pclk_vpu_root", 0,
			RK3528_CLKGATE_CON(27), 11, GFLAGS),
	GATE(PCLK_CAN2, "pclk_can2", "pclk_vpu_root", 0,
			RK3528_CLKGATE_CON(32), 7, GFLAGS),
	GATE(PCLK_SPI1, "pclk_spi1", "pclk_vpu_root", 0,
			RK3528_CLKGATE_CON(27), 4, GFLAGS),
	GATE(PCLK_CAN3, "pclk_can3", "pclk_vpu_root", 0,
			RK3528_CLKGATE_CON(32), 9, GFLAGS),
	GATE(PCLK_GPIO3, "pclk_gpio3", "pclk_vpu_root", 0,
			RK3528_CLKGATE_CON(27), 0, GFLAGS),
	GATE(PCLK_GPIO1, "pclk_gpio1", "pclk_vpu_root", 0,
			RK3528_CLKGATE_CON(26), 4, GFLAGS),
	GATE(PCLK_SARADC, "pclk_saradc", "pclk_vpu_root", 0,
			RK3528_CLKGATE_CON(32), 11, GFLAGS),
	GATE(PCLK_ACODEC, "pclk_acodec", "pclk_vpu_root", 0,
			RK3528_CLKGATE_CON(26), 13, GFLAGS),
	GATE(PCLK_UART7, "pclk_uart7", "pclk_vpu_root", 0,
			RK3528_CLKGATE_CON(27), 13, GFLAGS),
	GATE(PCLK_UART5, "pclk_uart5", "pclk_vpu_root", 0,
			RK3528_CLKGATE_CON(27), 9, GFLAGS),
	GATE(PCLK_TSADC, "pclk_tsadc", "pclk_vpu_root", 0,
			RK3528_CLKGATE_CON(32), 14, GFLAGS),
	GATE(PCLK_PCIE, "pclk_pcie", "pclk_vpu_root", 0,
			RK3528_CLKGATE_CON(30), 1, GFLAGS),
	GATE(PCLK_UART2, "pclk_uart2", "pclk_vpu_root", 0,
			RK3528_CLKGATE_CON(27), 7, GFLAGS),
	GATE(PCLK_VPU_IOC, "pclk_vpu_ioc", "pclk_vpu_root", CLK_IS_CRITICAL,
			RK3528_CLKGATE_CON(26), 8, GFLAGS),
	GATE(PCLK_PIPE_GRF, "pclk_pipe_grf", "pclk_vpu_root", CLK_IS_CRITICAL,
			RK3528_CLKGATE_CON(30), 7, GFLAGS),
	GATE(PCLK_I2C5, "pclk_i2c5", "pclk_vpu_root", 0,
			RK3528_CLKGATE_CON(28), 1, GFLAGS),
	GATE(PCLK_PCIE_PHY, "pclk_pcie_phy", "pclk_vpu_root", 0,
			RK3528_CLKGATE_CON(30), 6, GFLAGS),
	GATE(PCLK_I2C3, "pclk_i2c3", "pclk_vpu_root", 0,
			RK3528_CLKGATE_CON(27), 15, GFLAGS),
	GATE(PCLK_MAC_VPU, "pclk_gmac1", "pclk_vpu_root", CLK_IS_CRITICAL,
			RK3528_CLKGATE_CON(28), 6, GFLAGS),
	GATE(PCLK_I2C6, "pclk_i2c6", "pclk_vpu_root", 0,
			RK3528_CLKGATE_CON(28), 3, GFLAGS),

	COMPOSITE_NODIV(ACLK_VPU_L_ROOT, "aclk_vpu_l_root", mux_200m_100m_24m_p, CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(60), 0, 2, MFLAGS,
			RK3528_CLKGATE_CON(25), 0, GFLAGS),
	GATE(ACLK_EMMC, "aclk_emmc", "aclk_vpu_l_root", 0,
			RK3528_CLKGATE_CON(26), 1, GFLAGS),
	GATE(ACLK_MAC_VPU, "aclk_gmac1", "aclk_vpu_l_root", 0,
			RK3528_CLKGATE_CON(28), 5, GFLAGS),
	GATE(ACLK_PCIE, "aclk_pcie", "aclk_vpu_l_root", 0,
			RK3528_CLKGATE_CON(30), 3, GFLAGS),

	GATE(ACLK_USB3OTG, "aclk_usb3otg", "aclk_vpu_l_root", 0,
			RK3528_CLKGATE_CON(33), 1, GFLAGS),

	COMPOSITE_NODIV(HCLK_VPU_ROOT, "hclk_vpu_root", mux_200m_100m_50m_24m_p, CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(61), 2, 2, MFLAGS,
			RK3528_CLKGATE_CON(25), 4, GFLAGS),
	GATE(HCLK_VPU, "hclk_vpu", "hclk_vpu_root", 0,
			RK3528_CLKGATE_CON(25), 10, GFLAGS),
	GATE(HCLK_SFC, "hclk_sfc", "hclk_vpu_root", 0,
			RK3528_CLKGATE_CON(25), 13, GFLAGS),
	GATE(HCLK_EMMC, "hclk_emmc", "hclk_vpu_root", 0,
			RK3528_CLKGATE_CON(26), 0, GFLAGS),
	GATE(HCLK_SAI_I2S0, "hclk_sai_i2s0", "hclk_vpu_root", 0,
			RK3528_CLKGATE_CON(26), 9, GFLAGS),
	GATE(HCLK_SAI_I2S2, "hclk_sai_i2s2", "hclk_vpu_root", 0,
			RK3528_CLKGATE_CON(26), 11, GFLAGS),

	GATE(HCLK_PCIE_SLV, "hclk_pcie_slv", "hclk_vpu_root", 0,
			RK3528_CLKGATE_CON(30), 4, GFLAGS),
	GATE(HCLK_PCIE_DBI, "hclk_pcie_dbi", "hclk_vpu_root", 0,
			RK3528_CLKGATE_CON(30), 5, GFLAGS),
	GATE(HCLK_SDIO0, "hclk_sdio0", "hclk_vpu_root", 0,
			RK3528_CLKGATE_CON(32), 2, GFLAGS),
	GATE(HCLK_SDIO1, "hclk_sdio1", "hclk_vpu_root", 0,
			RK3528_CLKGATE_CON(32), 4, GFLAGS),

	COMPOSITE_NOMUX(CLK_GMAC1_VPU_25M, "clk_gmac1_25m", "ppll", 0,
			RK3528_CLKSEL_CON(60), 2, 8, DFLAGS,
			RK3528_CLKGATE_CON(25), 1, GFLAGS),
	COMPOSITE_NOMUX(CLK_PPLL_125M_MATRIX, "clk_ppll_125m_src", "ppll", 0,
			RK3528_CLKSEL_CON(60), 10, 5, DFLAGS,
			RK3528_CLKGATE_CON(25), 2, GFLAGS),

	COMPOSITE(CLK_CAN3, "clk_can3", mux_gpll_cpll_p, 0,
			RK3528_CLKSEL_CON(73), 13, 1, MFLAGS, 7, 6, DFLAGS,
			RK3528_CLKGATE_CON(32), 10, GFLAGS),
	COMPOSITE_NODIV(CLK_I2C6, "clk_i2c6", mux_200m_100m_50m_24m_p, 0,
			RK3528_CLKSEL_CON(64), 0, 2, MFLAGS,
			RK3528_CLKGATE_CON(28), 4, GFLAGS),

	COMPOSITE(SCLK_SFC, "sclk_sfc", mux_gpll_cpll_xin24m_p, 0,
			RK3528_CLKSEL_CON(61), 12, 2, MFLAGS, 6, 6, DFLAGS,
			RK3528_CLKGATE_CON(25), 14, GFLAGS),
	COMPOSITE(CCLK_SRC_EMMC, "cclk_src_emmc", mux_gpll_cpll_xin24m_p, 0,
			RK3528_CLKSEL_CON(62), 6, 2, MFLAGS, 0, 6, DFLAGS,
			RK3528_CLKGATE_CON(25), 15, GFLAGS),

	COMPOSITE_NODIV(ACLK_VPU_ROOT, "aclk_vpu_root",
			mux_300m_200m_100m_24m_p, CLK_IS_CRITICAL,
			RK3528_CLKSEL_CON(61), 0, 2, MFLAGS,
			RK3528_CLKGATE_CON(25), 3, GFLAGS),
	GATE(ACLK_VPU, "aclk_vpu", "aclk_vpu_root", 0,
			RK3528_CLKGATE_CON(25), 9, GFLAGS),

	COMPOSITE_NODIV(CLK_SPI1, "clk_spi1", mux_200m_100m_50m_24m_p, 0,
			RK3528_CLKSEL_CON(63), 10, 2, MFLAGS,
			RK3528_CLKGATE_CON(27), 5, GFLAGS),
	COMPOSITE(CCLK_SRC_SDIO1, "cclk_src_sdio1", mux_gpll_cpll_xin24m_p, 0,
			RK3528_CLKSEL_CON(72), 14, 2, MFLAGS, 8, 6, DFLAGS,
			RK3528_CLKGATE_CON(32), 3, GFLAGS),
	COMPOSITE(CLK_CAN2, "clk_can2", mux_gpll_cpll_p, 0,
			RK3528_CLKSEL_CON(73), 6, 1, MFLAGS, 0, 6, DFLAGS,
			RK3528_CLKGATE_CON(32), 8, GFLAGS),
	COMPOSITE_NOMUX(CLK_TSADC, "clk_tsadc", "xin24m", 0,
			RK3528_CLKSEL_CON(74), 3, 5, DFLAGS,
			RK3528_CLKGATE_CON(32), 15, GFLAGS),
	COMPOSITE_NOMUX(CLK_SARADC, "clk_saradc", "xin24m", 0,
			RK3528_CLKSEL_CON(74), 0, 3, DFLAGS,
			RK3528_CLKGATE_CON(32), 12, GFLAGS),
	COMPOSITE_NOMUX(CLK_TSADC_TSEN, "clk_tsadc_tsen", "xin24m", 0,
			RK3528_CLKSEL_CON(74), 8, 5, DFLAGS,
			RK3528_CLKGATE_CON(33), 0, GFLAGS),
	COMPOSITE_NODIV(BCLK_EMMC, "bclk_emmc", mux_200m_100m_50m_24m_p, 0,
			RK3528_CLKSEL_CON(62), 8, 2, MFLAGS,
			RK3528_CLKGATE_CON(26), 2, GFLAGS),
	COMPOSITE_NOMUX(MCLK_ACODEC_TX, "mclk_acodec_tx", "mclk_i2s2_2ch_sai_src", 0,
			RK3528_CLKSEL_CON(63), 0, 8, DFLAGS,
			RK3528_CLKGATE_CON(26), 14, GFLAGS),
	COMPOSITE_NODIV(CLK_I2C3, "clk_i2c3", mux_200m_100m_50m_24m_p, 0,
			RK3528_CLKSEL_CON(63), 12, 2, MFLAGS,
			RK3528_CLKGATE_CON(28), 0, GFLAGS),
	COMPOSITE_NODIV(CLK_I2C5, "clk_i2c5", mux_200m_100m_50m_24m_p, 0,
			RK3528_CLKSEL_CON(63), 14, 2, MFLAGS,
			RK3528_CLKGATE_CON(28), 2, GFLAGS),
	COMPOSITE_NODIV(MCLK_SAI_I2S0, "mclk_sai_i2s0", mclk_sai_i2s0_p, CLK_SET_RATE_PARENT,
			RK3528_CLKSEL_CON(62), 10, 1, MFLAGS,
			RK3528_CLKGATE_CON(26), 10, GFLAGS),
	GATE(MCLK_SAI_I2S2, "mclk_sai_i2s2", "mclk_i2s2_2ch_sai_src", 0,
			RK3528_CLKGATE_CON(26), 12, GFLAGS),

	/* pcie */
	COMPOSITE_NOMUX(CLK_PPLL_100M_MATRIX, "clk_ppll_100m_src", "ppll", CLK_IS_CRITICAL,
			RK3528_PCIE_CLKSEL_CON(1), 2, 5, DFLAGS,
			RK3528_PCIE_CLKGATE_CON(0), 1, GFLAGS),
	COMPOSITE_NOMUX(CLK_PPLL_50M_MATRIX, "clk_ppll_50m_src", "ppll", CLK_IS_CRITICAL,
			RK3528_PCIE_CLKSEL_CON(1), 7, 5, DFLAGS,
			RK3528_PCIE_CLKGATE_CON(0), 2, GFLAGS),
	MUX(CLK_REF_PCIE_INNER_PHY, "clk_ref_pcie_inner_phy", clk_ref_pcie_inner_phy_p, 0,
			RK3528_PCIE_CLKSEL_CON(1), 13, 1, MFLAGS),
	FACTOR(CLK_REF_PCIE_100M_PHY, "clk_ref_pcie_100m_phy", "clk_ppll_100m_src",
			0, 1, 1),

	/* gmac */
	DIV(CLK_GMAC0_SRC, "clk_gmac0_src", "gmac0", 0,
			RK3528_CLKSEL_CON(84), 3, 6, DFLAGS),
	GATE(CLK_GMAC0_TX, "clk_gmac0_tx", "clk_gmac0_src", 0,
			RK3528_CLKGATE_CON(41), 13, GFLAGS),
	GATE(CLK_GMAC0_RX, "clk_gmac0_rx", "clk_gmac0_src", 0,
			RK3528_CLKGATE_CON(41), 14, GFLAGS),
	GATE(CLK_GMAC0_RMII_50M, "clk_gmac0_rmii_50m", "gmac0", 0,
			RK3528_CLKGATE_CON(41), 12, GFLAGS),

	FACTOR(CLK_GMAC1_RMII_VPU, "clk_gmac1_50m", "clk_ppll_50m_src",
			0, 1, 1),
	FACTOR(CLK_GMAC1_SRC_VPU, "clk_gmac1_125m", "clk_ppll_125m_src",
			0, 1, 1),
};

static int __init clk_rk3528_probe(struct platform_device *pdev)
{
	struct rockchip_clk_provider *ctx;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	unsigned long nr_branches = ARRAY_SIZE(rk3528_clk_branches);
	unsigned long nr_clks;
	void __iomem *reg_base;

	nr_clks = rockchip_clk_find_max_clk_id(rk3528_clk_branches,
					       nr_branches) + 1;

	reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(reg_base))
		return dev_err_probe(dev, PTR_ERR(reg_base),
				     "could not map cru region");

	ctx = rockchip_clk_init(np, reg_base, nr_clks);
	if (IS_ERR(ctx))
		return dev_err_probe(dev, PTR_ERR(ctx),
				     "rockchip clk init failed");

	rockchip_clk_register_plls(ctx, rk3528_pll_clks,
				   ARRAY_SIZE(rk3528_pll_clks),
				   RK3528_GRF_SOC_STATUS0);
	rockchip_clk_register_armclk(ctx, ARMCLK, "armclk",
				     mux_armclk, ARRAY_SIZE(mux_armclk),
				     &rk3528_cpuclk_data, rk3528_cpuclk_rates,
				     ARRAY_SIZE(rk3528_cpuclk_rates));
	rockchip_clk_register_branches(ctx, rk3528_clk_branches, nr_branches);

	rk3528_rst_init(np, reg_base);

	rockchip_register_restart_notifier(ctx, RK3528_GLB_SRST_FST, NULL);

	rockchip_clk_of_add_provider(np, ctx);

	return 0;
}

static const struct of_device_id clk_rk3528_match_table[] = {
	{ .compatible = "rockchip,rk3528-cru" },
	{ /* end */ }
};

static struct platform_driver clk_rk3528_driver = {
	.driver = {
		.name			= "clk-rk3528",
		.of_match_table		= clk_rk3528_match_table,
		.suppress_bind_attrs	= true,
	},
};
builtin_platform_driver_probe(clk_rk3528_driver, clk_rk3528_probe);
