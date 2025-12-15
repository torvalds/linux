// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023-2025 Rockchip Electronics Co., Ltd.
 * Author: Finley Xiao <finley.xiao@rock-chips.com>
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/syscore_ops.h>
#include <dt-bindings/clock/rockchip,rk3506-cru.h>
#include "clk.h"

#define PVTPLL_SRC_SEL_PVTPLL		(BIT(7) | BIT(23))

enum rk3506_plls {
	gpll, v0pll, v1pll,
};

/*
 * [FRAC PLL]: GPLL, V0PLL, V1PLL
 *   - VCO Frequency: 950MHz to 3800MHZ
 *   - Output Frequency: 19MHz to 3800MHZ
 *   - refdiv: 1 to 63 (Int Mode), 1 to 2 (Frac Mode)
 *   - fbdiv: 16 to 3800 (Int Mode), 20 to 380 (Frac Mode)
 *   - post1div: 1 to 7
 *   - post2div: 1 to 7
 */
static struct rockchip_pll_rate_table rk3506_pll_rates[] = {
	/* _mhz, _refdiv, _fbdiv, _postdiv1, _postdiv2, _dsmpd, _frac */
	RK3036_PLL_RATE(1896000000, 1, 79, 1, 1, 1, 0),
	RK3036_PLL_RATE(1800000000, 1, 75, 1, 1, 1, 0),
	RK3036_PLL_RATE(1704000000, 1, 71, 1, 1, 1, 0),
	RK3036_PLL_RATE(1608000000, 1, 67, 1, 1, 1, 0),
	RK3036_PLL_RATE(1512000000, 1, 63, 1, 1, 1, 0),
	RK3036_PLL_RATE(1416000000, 1, 59, 1, 1, 1, 0),
	RK3036_PLL_RATE(1350000000, 4, 225, 1, 1, 1, 0),
	RK3036_PLL_RATE(1296000000, 1, 54, 1, 1, 1, 0),
	RK3036_PLL_RATE(1200000000, 1, 50, 1, 1, 1, 0),
	RK3036_PLL_RATE(1188000000, 1, 99, 2, 1, 1, 0),
	RK3036_PLL_RATE(1179648000, 1, 49, 1, 1, 0, 2550137),
	RK3036_PLL_RATE(1008000000, 1, 84, 2, 1, 1, 0),
	RK3036_PLL_RATE(1000000000, 3, 125, 1, 1, 1, 0),
	RK3036_PLL_RATE(993484800, 1, 41, 1, 1, 0, 6630355),
	RK3036_PLL_RATE(983040000, 1, 40, 1, 1, 0, 16106127),
	RK3036_PLL_RATE(960000000, 1, 80, 2, 1, 1, 0),
	RK3036_PLL_RATE(912000000, 1, 76, 2, 1, 1, 0),
	RK3036_PLL_RATE(903168000, 1, 75, 2, 1, 0, 4429185),
	RK3036_PLL_RATE(816000000, 1, 68, 2, 1, 1, 0),
	RK3036_PLL_RATE(800000000, 3, 200, 2, 1, 1, 0),
	RK3036_PLL_RATE(600000000, 1, 50, 2, 1, 1, 0),
	RK3036_PLL_RATE(594000000, 2, 99, 2, 1, 1, 0),
	RK3036_PLL_RATE(408000000, 1, 68, 2, 2, 1, 0),
	RK3036_PLL_RATE(312000000, 1, 78, 6, 1, 1, 0),
	RK3036_PLL_RATE(216000000, 1, 72, 4, 2, 1, 0),
	RK3036_PLL_RATE(96000000, 1, 48, 6, 2, 1, 0),
	{ /* sentinel */ },
};

#define RK3506_DIV_ACLK_CORE_MASK	0xf
#define RK3506_DIV_ACLK_CORE_SHIFT	9
#define RK3506_DIV_PCLK_CORE_MASK	0xf
#define RK3506_DIV_PCLK_CORE_SHIFT	0

#define RK3506_CLKSEL15(_aclk_core_div)					\
{									\
	.reg = RK3506_CLKSEL_CON(15),					\
	.val = HIWORD_UPDATE(_aclk_core_div, RK3506_DIV_ACLK_CORE_MASK,	\
			     RK3506_DIV_ACLK_CORE_SHIFT),		\
}

#define RK3506_CLKSEL16(_pclk_core_div)					\
{									\
	.reg = RK3506_CLKSEL_CON(16),					\
	.val = HIWORD_UPDATE(_pclk_core_div, RK3506_DIV_PCLK_CORE_MASK,	\
			     RK3506_DIV_PCLK_CORE_SHIFT),		\
}

/* SIGN-OFF: aclk_core: 500M, pclk_core: 125M, */
#define RK3506_CPUCLK_RATE(_prate, _aclk_core_div, _pclk_core_div)	\
{									\
	.prate = _prate,						\
	.divs = {							\
		RK3506_CLKSEL15(_aclk_core_div),			\
		RK3506_CLKSEL16(_pclk_core_div),			\
	},								\
}

static struct rockchip_cpuclk_rate_table rk3506_cpuclk_rates[] __initdata = {
	RK3506_CPUCLK_RATE(1608000000, 3, 12),
	RK3506_CPUCLK_RATE(1512000000, 3, 12),
	RK3506_CPUCLK_RATE(1416000000, 2, 11),
	RK3506_CPUCLK_RATE(1296000000, 2, 10),
	RK3506_CPUCLK_RATE(1200000000, 2, 9),
	RK3506_CPUCLK_RATE(1179648000, 2, 9),
	RK3506_CPUCLK_RATE(1008000000, 1, 7),
	RK3506_CPUCLK_RATE(903168000, 1, 7),
	RK3506_CPUCLK_RATE(800000000, 1, 6),
	RK3506_CPUCLK_RATE(750000000, 1, 5),
	RK3506_CPUCLK_RATE(589824000, 1, 4),
	RK3506_CPUCLK_RATE(400000000, 1, 3),
	RK3506_CPUCLK_RATE(200000000, 1, 1),
};

PNAME(mux_pll_p)				= { "xin24m" };
PNAME(gpll_v0pll_v1pll_parents_p)		= { "gpll", "v0pll", "v1pll" };
PNAME(gpll_v0pll_v1pll_g_parents_p)		= { "clk_gpll_gate", "clk_v0pll_gate", "clk_v1pll_gate" };
PNAME(gpll_v0pll_v1pll_div_parents_p)		= { "clk_gpll_div", "clk_v0pll_div", "clk_v1pll_div" };
PNAME(xin24m_gpll_v0pll_v1pll_g_parents_p)	= { "xin24m", "clk_gpll_gate", "clk_v0pll_gate", "clk_v1pll_gate" };
PNAME(xin24m_g_gpll_v0pll_v1pll_g_parents_p)	= { "xin24m_gate", "clk_gpll_gate", "clk_v0pll_gate", "clk_v1pll_gate" };
PNAME(xin24m_g_gpll_v0pll_v1pll_div_parents_p)	= { "xin24m_gate", "clk_gpll_div", "clk_v0pll_div", "clk_v1pll_div" };
PNAME(xin24m_400k_32k_parents_p)		= { "xin24m", "clk_rc", "clk_32k" };
PNAME(clk_frac_uart_matrix0_mux_parents_p)	= { "xin24m", "gpll", "clk_v0pll_gate", "clk_v1pll_gate" };
PNAME(clk_timer0_parents_p)			= { "xin24m", "clk_gpll_div_100m", "clk_32k", "clk_core_pvtpll", "sai0_mclk_in", "sai0_sclk_in" };
PNAME(clk_timer1_parents_p)			= { "xin24m", "clk_gpll_div_100m", "clk_32k", "clk_core_pvtpll", "sai1_mclk_in", "sai1_sclk_in" };
PNAME(clk_timer2_parents_p)			= { "xin24m", "clk_gpll_div_100m", "clk_32k", "clk_core_pvtpll", "sai2_mclk_in", "sai2_sclk_in" };
PNAME(clk_timer3_parents_p)			= { "xin24m", "clk_gpll_div_100m", "clk_32k", "clk_core_pvtpll", "sai3_mclk_in", "sai3_sclk_in" };
PNAME(clk_timer4_parents_p)			= { "xin24m", "clk_gpll_div_100m", "clk_32k", "clk_core_pvtpll", "mclk_asrc0" };
PNAME(clk_timer5_parents_p)			= { "xin24m", "clk_gpll_div_100m", "clk_32k", "clk_core_pvtpll", "mclk_asrc1" };
PNAME(sclk_uart_parents_p)			= { "xin24m", "clk_gpll_gate", "clk_v0pll_gate", "clk_frac_uart_matrix0", "clk_frac_uart_matrix1",
						    "clk_frac_common_matrix0", "clk_frac_common_matrix1", "clk_frac_common_matrix2" };
PNAME(clk_mac_ptp_root_parents_p)		= { "gpll", "v0pll", "v1pll" };
PNAME(clk_pwm_parents_p)			= { "clk_rc", "sai0_mclk_in", "sai1_mclk_in", "sai2_mclk_in", "sai3_mclk_in", "sai0_sclk_in", "sai1_sclk_in",
						    "sai2_sclk_in", "sai3_sclk_in", "mclk_asrc0", "mclk_asrc1" };
PNAME(clk_can_parents_p)			= { "xin24m", "gpll", "clk_v0pll_gate", "clk_v1pll_gate", "clk_frac_voice_matrix1",
						    "clk_frac_common_matrix0", "clk_frac_common_matrix1", "clk_frac_common_matrix2" };
PNAME(clk_pdm_parents_p)			= { "xin24m_gate", "clk_int_voice_matrix0", "clk_int_voice_matrix1", "clk_int_voice_matrix2",
						    "clk_frac_voice_matrix0", "clk_frac_voice_matrix1", "clk_frac_common_matrix0", "clk_frac_common_matrix1",
						    "clk_frac_common_matrix2", "sai0_mclk_in", "sai1_mclk_in", "sai2_mclk_in", "sai3_mclk_in", "clk_gpll_div" };
PNAME(mclk_sai_asrc_parents_p)			= { "xin24m_gate", "clk_int_voice_matrix0", "clk_int_voice_matrix1", "clk_int_voice_matrix2",
						    "clk_frac_voice_matrix0", "clk_frac_voice_matrix1", "clk_frac_common_matrix0", "clk_frac_common_matrix1",
						    "clk_frac_common_matrix2", "sai0_mclk_in", "sai1_mclk_in", "sai2_mclk_in", "sai3_mclk_in" };
PNAME(lrck_asrc_parents_p)			= { "mclk_asrc0", "mclk_asrc1", "mclk_asrc2", "mclk_asrc3", "mclk_spdiftx", "clk_spdifrx_to_asrc", "clkout_pdm",
						    "sai0_fs", "sai1_fs", "sai2_fs", "sai3_fs", "sai4_fs" };
PNAME(cclk_src_sdmmc_parents_p)			= { "xin24m_gate", "gpll", "clk_v0pll_gate", "clk_v1pll_gate" };
PNAME(dclk_vop_parents_p)			= { "xin24m_gate", "clk_gpll_gate", "clk_v0pll_gate", "clk_v1pll_gate", "dummy_vop_dclk",
						    "dummy_vop_dclk", "dummy_vop_dclk", "dummy_vop_dclk" };
PNAME(dbclk_gpio0_parents_p)			= { "xin24m", "clk_rc", "clk_32k_pmu" };
PNAME(clk_pmu_hp_timer_parents_p)		= { "xin24m", "gpll_div_100m", "clk_core_pvtpll" };
PNAME(clk_ref_out_parents_p)			= { "xin24m", "gpll", "v0pll", "v1pll" };
PNAME(clk_32k_frac_parents_p)			= { "xin24m", "v0pll", "v1pll", "clk_rc" };
PNAME(clk_32k_parents_p)			= { "xin32k", "clk_32k_rc", "clk_32k_frac" };
PNAME(clk_ref_phy_pmu_mux_parents_p)		= { "xin24m", "clk_ref_phy_pll" };
PNAME(clk_vpll_ref_parents_p)			= { "xin24m", "clk_pll_ref_io" };
PNAME(mux_armclk_p)				= { "armclk_pll", "clk_core_pvtpll" };

#define MFLAGS CLK_MUX_HIWORD_MASK
#define DFLAGS CLK_DIVIDER_HIWORD_MASK
#define GFLAGS (CLK_GATE_HIWORD_MASK | CLK_GATE_SET_TO_DISABLE)

static struct rockchip_pll_clock rk3506_pll_clks[] __initdata = {
	[gpll] = PLL(pll_rk3328, PLL_GPLL, "gpll", mux_pll_p,
		     CLK_IS_CRITICAL, RK3506_PLL_CON(0),
		     RK3506_MODE_CON, 0, 2, 0, rk3506_pll_rates),
	[v0pll] = PLL(pll_rk3328, PLL_V0PLL, "v0pll", mux_pll_p,
		     CLK_IS_CRITICAL, RK3506_PLL_CON(8),
		     RK3506_MODE_CON, 2, 0, 0, rk3506_pll_rates),
	[v1pll] = PLL(pll_rk3328, PLL_V1PLL, "v1pll", mux_pll_p,
		     CLK_IS_CRITICAL, RK3506_PLL_CON(16),
		     RK3506_MODE_CON, 4, 1, 0, rk3506_pll_rates),
};

static struct rockchip_clk_branch rk3506_armclk __initdata =
	MUX(ARMCLK, "armclk", mux_armclk_p, CLK_IS_CRITICAL | CLK_SET_RATE_PARENT,
			RK3506_CLKSEL_CON(15), 8, 1, MFLAGS);

static struct rockchip_clk_branch rk3506_clk_branches[] __initdata = {
	/*
	 * CRU Clock-Architecture
	 */
	/* top */
	GATE(XIN24M_GATE, "xin24m_gate", "xin24m", CLK_IS_CRITICAL,
			RK3506_CLKGATE_CON(0), 1, GFLAGS),
	GATE(CLK_GPLL_GATE, "clk_gpll_gate", "gpll", CLK_IS_CRITICAL,
			RK3506_CLKGATE_CON(0), 2, GFLAGS),
	GATE(CLK_V0PLL_GATE, "clk_v0pll_gate", "v0pll", CLK_IS_CRITICAL,
			RK3506_CLKGATE_CON(0), 3, GFLAGS),
	GATE(CLK_V1PLL_GATE, "clk_v1pll_gate", "v1pll", 0,
			RK3506_CLKGATE_CON(0), 4, GFLAGS),
	COMPOSITE_NOMUX(CLK_GPLL_DIV, "clk_gpll_div", "clk_gpll_gate", CLK_IS_CRITICAL,
			RK3506_CLKSEL_CON(0), 6, 4, DFLAGS,
			RK3506_CLKGATE_CON(0), 5, GFLAGS),
	COMPOSITE_NOMUX(CLK_GPLL_DIV_100M, "clk_gpll_div_100m", "clk_gpll_div", 0,
			RK3506_CLKSEL_CON(0), 10, 4, DFLAGS,
			RK3506_CLKGATE_CON(0), 6, GFLAGS),
	COMPOSITE_NOMUX(CLK_V0PLL_DIV, "clk_v0pll_div", "clk_v0pll_gate", CLK_IS_CRITICAL,
			RK3506_CLKSEL_CON(1), 0, 4, DFLAGS,
			RK3506_CLKGATE_CON(0), 7, GFLAGS),
	COMPOSITE_NOMUX(CLK_V1PLL_DIV, "clk_v1pll_div", "clk_v1pll_gate", 0,
			RK3506_CLKSEL_CON(1), 4, 4, DFLAGS,
			RK3506_CLKGATE_CON(0), 8, GFLAGS),
	COMPOSITE_NOMUX(CLK_INT_VOICE_MATRIX0, "clk_int_voice_matrix0", "clk_v0pll_gate", 0,
			RK3506_CLKSEL_CON(1), 8, 5, DFLAGS,
			RK3506_CLKGATE_CON(0), 9, GFLAGS),
	COMPOSITE_NOMUX(CLK_INT_VOICE_MATRIX1, "clk_int_voice_matrix1", "clk_v1pll_gate", 0,
			RK3506_CLKSEL_CON(2), 0, 5, DFLAGS,
			RK3506_CLKGATE_CON(0), 10, GFLAGS),
	COMPOSITE_NOMUX(CLK_INT_VOICE_MATRIX2, "clk_int_voice_matrix2", "clk_v0pll_gate", 0,
			RK3506_CLKSEL_CON(2), 5, 5, DFLAGS,
			RK3506_CLKGATE_CON(0), 11, GFLAGS),
	MUX(CLK_FRAC_UART_MATRIX0_MUX, "clk_frac_uart_matrix0_mux", clk_frac_uart_matrix0_mux_parents_p, 0,
			RK3506_CLKSEL_CON(3), 9, 2, MFLAGS),
	MUX(CLK_FRAC_UART_MATRIX1_MUX, "clk_frac_uart_matrix1_mux", xin24m_gpll_v0pll_v1pll_g_parents_p, 0,
			RK3506_CLKSEL_CON(3), 11, 2, MFLAGS),
	MUX(CLK_FRAC_VOICE_MATRIX0_MUX, "clk_frac_voice_matrix0_mux", xin24m_g_gpll_v0pll_v1pll_g_parents_p, 0,
			RK3506_CLKSEL_CON(3), 13, 2, MFLAGS),
	MUX(CLK_FRAC_VOICE_MATRIX1_MUX, "clk_frac_voice_matrix1_mux", xin24m_g_gpll_v0pll_v1pll_g_parents_p, 0,
			RK3506_CLKSEL_CON(4), 0, 2, MFLAGS),
	MUX(CLK_FRAC_COMMON_MATRIX0_MUX, "clk_frac_common_matrix0_mux", xin24m_gpll_v0pll_v1pll_g_parents_p, 0,
			RK3506_CLKSEL_CON(4), 2, 2, MFLAGS),
	MUX(CLK_FRAC_COMMON_MATRIX1_MUX, "clk_frac_common_matrix1_mux", xin24m_g_gpll_v0pll_v1pll_g_parents_p, 0,
			RK3506_CLKSEL_CON(4), 4, 2, MFLAGS),
	MUX(CLK_FRAC_COMMON_MATRIX2_MUX, "clk_frac_common_matrix2_mux", xin24m_g_gpll_v0pll_v1pll_g_parents_p, 0,
			RK3506_CLKSEL_CON(4), 6, 2, MFLAGS),
	COMPOSITE_FRAC(CLK_FRAC_UART_MATRIX0, "clk_frac_uart_matrix0", "clk_frac_uart_matrix0_mux", 0,
			RK3506_CLKSEL_CON(5), 0,
			RK3506_CLKGATE_CON(0), 13, GFLAGS),
	COMPOSITE_FRAC(CLK_FRAC_UART_MATRIX1, "clk_frac_uart_matrix1", "clk_frac_uart_matrix1_mux", 0,
			RK3506_CLKSEL_CON(6), 0,
			RK3506_CLKGATE_CON(0), 14, GFLAGS),
	COMPOSITE_FRAC(CLK_FRAC_VOICE_MATRIX0, "clk_frac_voice_matrix0", "clk_frac_voice_matrix0_mux", 0,
			RK3506_CLKSEL_CON(7), 0,
			RK3506_CLKGATE_CON(0), 15, GFLAGS),
	COMPOSITE_FRAC(CLK_FRAC_VOICE_MATRIX1, "clk_frac_voice_matrix1", "clk_frac_voice_matrix1_mux", 0,
			RK3506_CLKSEL_CON(9), 0,
			RK3506_CLKGATE_CON(1), 0, GFLAGS),
	COMPOSITE_FRAC(CLK_FRAC_COMMON_MATRIX0, "clk_frac_common_matrix0", "clk_frac_common_matrix0_mux", 0,
			RK3506_CLKSEL_CON(11), 0,
			RK3506_CLKGATE_CON(1), 1, GFLAGS),
	COMPOSITE_FRAC(CLK_FRAC_COMMON_MATRIX1, "clk_frac_common_matrix1", "clk_frac_common_matrix1_mux", 0,
			RK3506_CLKSEL_CON(12), 0,
			RK3506_CLKGATE_CON(1), 2, GFLAGS),
	COMPOSITE_FRAC(CLK_FRAC_COMMON_MATRIX2, "clk_frac_common_matrix2", "clk_frac_common_matrix2_mux", 0,
			RK3506_CLKSEL_CON(13), 0,
			RK3506_CLKGATE_CON(1), 3, GFLAGS),
	GATE(CLK_REF_USBPHY_TOP, "clk_ref_usbphy_top", "xin24m", 0,
			RK3506_CLKGATE_CON(1), 4, GFLAGS),
	GATE(CLK_REF_DPHY_TOP, "clk_ref_dphy_top", "xin24m", 0,
			RK3506_CLKGATE_CON(1), 5, GFLAGS),

	/* core */
	COMPOSITE_NOGATE(0, "armclk_pll", gpll_v0pll_v1pll_parents_p, CLK_IS_CRITICAL,
			RK3506_CLKSEL_CON(15), 5, 2, MFLAGS, 0, 5, DFLAGS),
	COMPOSITE_NOMUX(ACLK_CORE_ROOT, "aclk_core_root", "armclk", CLK_IGNORE_UNUSED,
			RK3506_CLKSEL_CON(15), 9, 4, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RK3506_CLKGATE_CON(2), 11, GFLAGS),
	COMPOSITE_NOMUX(PCLK_CORE_ROOT, "pclk_core_root", "armclk", CLK_IGNORE_UNUSED,
			RK3506_CLKSEL_CON(16), 0, 4, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RK3506_CLKGATE_CON(2), 12, GFLAGS),
	GATE(PCLK_DBG, "pclk_dbg", "pclk_core_root", CLK_IGNORE_UNUSED,
			RK3506_CLKGATE_CON(3), 1, GFLAGS),
	GATE(PCLK_CORE_GRF, "pclk_core_grf", "pclk_core_root", CLK_IGNORE_UNUSED,
			RK3506_CLKGATE_CON(3), 4, GFLAGS),
	GATE(PCLK_CORE_CRU, "pclk_core_cru", "pclk_core_root", CLK_IGNORE_UNUSED,
			RK3506_CLKGATE_CON(3), 5, GFLAGS),
	GATE(CLK_CORE_EMA_DETECT, "clk_core_ema_detect", "xin24m_gate", CLK_IGNORE_UNUSED,
			RK3506_CLKGATE_CON(3), 6, GFLAGS),
	GATE(PCLK_GPIO1, "pclk_gpio1", "aclk_core_root", 0,
			RK3506_CLKGATE_CON(3), 8, GFLAGS),
	GATE(DBCLK_GPIO1, "dbclk_gpio1", "xin24m_gate", 0,
			RK3506_CLKGATE_CON(3), 9, GFLAGS),

	/* core peri */
	COMPOSITE(ACLK_CORE_PERI_ROOT, "aclk_core_peri_root", gpll_v0pll_v1pll_g_parents_p, 0,
			RK3506_CLKSEL_CON(18), 5, 2, MFLAGS, 0, 5, DFLAGS,
			RK3506_CLKGATE_CON(4), 0, GFLAGS),
	GATE(HCLK_CORE_PERI_ROOT, "hclk_core_peri_root", "aclk_core_peri_root", 0,
			RK3506_CLKGATE_CON(4), 1, GFLAGS),
	GATE(PCLK_CORE_PERI_ROOT, "pclk_core_peri_root", "aclk_core_peri_root", 0,
			RK3506_CLKGATE_CON(4), 2, GFLAGS),
	COMPOSITE(CLK_DSMC, "clk_dsmc", xin24m_gpll_v0pll_v1pll_g_parents_p, 0,
			RK3506_CLKSEL_CON(18), 12, 2, MFLAGS, 7, 5, DFLAGS,
			RK3506_CLKGATE_CON(4), 4, GFLAGS),
	GATE(ACLK_DSMC, "aclk_dsmc", "aclk_core_peri_root", 0,
			RK3506_CLKGATE_CON(4), 5, GFLAGS),
	GATE(PCLK_DSMC, "pclk_dsmc", "pclk_core_peri_root", 0,
			RK3506_CLKGATE_CON(4), 6, GFLAGS),
	COMPOSITE(CLK_FLEXBUS_TX, "clk_flexbus_tx", xin24m_gpll_v0pll_v1pll_g_parents_p, 0,
			RK3506_CLKSEL_CON(19), 5, 2, MFLAGS, 0, 5, DFLAGS,
			RK3506_CLKGATE_CON(4), 7, GFLAGS),
	COMPOSITE(CLK_FLEXBUS_RX, "clk_flexbus_rx", xin24m_gpll_v0pll_v1pll_g_parents_p, 0,
			RK3506_CLKSEL_CON(19), 12, 2, MFLAGS, 7, 5, DFLAGS,
			RK3506_CLKGATE_CON(4), 8, GFLAGS),
	GATE(ACLK_FLEXBUS, "aclk_flexbus", "aclk_core_peri_root", 0,
			RK3506_CLKGATE_CON(4), 9, GFLAGS),
	GATE(HCLK_FLEXBUS, "hclk_flexbus", "hclk_core_peri_root", 0,
			RK3506_CLKGATE_CON(4), 10, GFLAGS),
	GATE(ACLK_DSMC_SLV, "aclk_dsmc_slv", "aclk_core_peri_root", 0,
			RK3506_CLKGATE_CON(4), 11, GFLAGS),
	GATE(HCLK_DSMC_SLV, "hclk_dsmc_slv", "hclk_core_peri_root", 0,
			RK3506_CLKGATE_CON(4), 12, GFLAGS),

	/* bus */
	COMPOSITE(ACLK_BUS_ROOT, "aclk_bus_root", gpll_v0pll_v1pll_div_parents_p, CLK_IS_CRITICAL,
			RK3506_CLKSEL_CON(21), 5, 2, MFLAGS, 0, 5, DFLAGS,
			RK3506_CLKGATE_CON(5), 0, GFLAGS),
	COMPOSITE(HCLK_BUS_ROOT, "hclk_bus_root", gpll_v0pll_v1pll_div_parents_p, CLK_IS_CRITICAL,
			RK3506_CLKSEL_CON(21), 12, 2, MFLAGS, 7, 5, DFLAGS,
			RK3506_CLKGATE_CON(5), 1, GFLAGS),
	COMPOSITE(PCLK_BUS_ROOT, "pclk_bus_root", gpll_v0pll_v1pll_div_parents_p, CLK_IS_CRITICAL,
			RK3506_CLKSEL_CON(22), 5, 2, MFLAGS, 0, 5, DFLAGS,
			RK3506_CLKGATE_CON(5), 2, GFLAGS),
	GATE(ACLK_SYSRAM, "aclk_sysram", "aclk_bus_root", CLK_IGNORE_UNUSED,
			RK3506_CLKGATE_CON(5), 6, GFLAGS),
	GATE(HCLK_SYSRAM, "hclk_sysram", "aclk_bus_root", CLK_IGNORE_UNUSED,
			RK3506_CLKGATE_CON(5), 7, GFLAGS),
	GATE(ACLK_DMAC0, "aclk_dmac0", "aclk_bus_root", 0,
			RK3506_CLKGATE_CON(5), 8, GFLAGS),
	GATE(ACLK_DMAC1, "aclk_dmac1", "aclk_bus_root", 0,
			RK3506_CLKGATE_CON(5), 9, GFLAGS),
	GATE(HCLK_M0, "hclk_m0", "aclk_bus_root", 0,
			RK3506_CLKGATE_CON(5), 10, GFLAGS),
	GATE(ACLK_CRYPTO_NS, "aclk_crypto_ns", "aclk_bus_root", 0,
			RK3506_CLKGATE_CON(5), 14, GFLAGS),
	GATE(HCLK_CRYPTO_NS, "hclk_crypto_ns", "hclk_bus_root", 0,
			RK3506_CLKGATE_CON(5), 15, GFLAGS),
	GATE(HCLK_RNG, "hclk_rng", "hclk_bus_root", 0,
			RK3506_CLKGATE_CON(6), 0, GFLAGS),
	GATE(PCLK_BUS_GRF, "pclk_bus_grf", "pclk_bus_root", CLK_IGNORE_UNUSED,
			RK3506_CLKGATE_CON(6), 1, GFLAGS),
	GATE(PCLK_TIMER, "pclk_timer", "pclk_bus_root", 0,
			RK3506_CLKGATE_CON(6), 2, GFLAGS),
	COMPOSITE_NODIV(CLK_TIMER0_CH0, "clk_timer0_ch0", clk_timer0_parents_p, 0,
			RK3506_CLKSEL_CON(22), 7, 3, MFLAGS,
			RK3506_CLKGATE_CON(6), 3, GFLAGS),
	COMPOSITE_NODIV(CLK_TIMER0_CH1, "clk_timer0_ch1", clk_timer1_parents_p, 0,
			RK3506_CLKSEL_CON(22), 10, 3, MFLAGS,
			RK3506_CLKGATE_CON(6), 4, GFLAGS),
	COMPOSITE_NODIV(CLK_TIMER0_CH2, "clk_timer0_ch2", clk_timer2_parents_p, 0,
			RK3506_CLKSEL_CON(22), 13, 3, MFLAGS,
			RK3506_CLKGATE_CON(6), 5, GFLAGS),
	COMPOSITE_NODIV(CLK_TIMER0_CH3, "clk_timer0_ch3", clk_timer3_parents_p, 0,
			RK3506_CLKSEL_CON(23), 0, 3, MFLAGS,
			RK3506_CLKGATE_CON(6), 6, GFLAGS),
	COMPOSITE_NODIV(CLK_TIMER0_CH4, "clk_timer0_ch4", clk_timer4_parents_p, 0,
			RK3506_CLKSEL_CON(23), 3, 3, MFLAGS,
			RK3506_CLKGATE_CON(6), 7, GFLAGS),
	COMPOSITE_NODIV(CLK_TIMER0_CH5, "clk_timer0_ch5", clk_timer5_parents_p, 0,
			RK3506_CLKSEL_CON(23), 6, 3, MFLAGS,
			RK3506_CLKGATE_CON(6), 8, GFLAGS),
	GATE(PCLK_WDT0, "pclk_wdt0", "pclk_bus_root", 0,
			RK3506_CLKGATE_CON(6), 9, GFLAGS),
	GATE(TCLK_WDT0, "tclk_wdt0", "xin24m_gate", 0,
			RK3506_CLKGATE_CON(6), 10, GFLAGS),
	GATE(PCLK_WDT1, "pclk_wdt1", "pclk_bus_root", 0,
			RK3506_CLKGATE_CON(6), 11, GFLAGS),
	GATE(TCLK_WDT1, "tclk_wdt1", "xin24m_gate", 0,
			RK3506_CLKGATE_CON(6), 12, GFLAGS),
	GATE(PCLK_MAILBOX, "pclk_mailbox", "pclk_bus_root", 0,
			RK3506_CLKGATE_CON(6), 13, GFLAGS),
	GATE(PCLK_INTMUX, "pclk_intmux", "pclk_bus_root", 0,
			RK3506_CLKGATE_CON(6), 14, GFLAGS),
	GATE(PCLK_SPINLOCK, "pclk_spinlock", "pclk_bus_root", 0,
			RK3506_CLKGATE_CON(6), 15, GFLAGS),
	GATE(PCLK_DDRC, "pclk_ddrc", "pclk_bus_root", CLK_IGNORE_UNUSED,
			RK3506_CLKGATE_CON(7), 0, GFLAGS),
	GATE(HCLK_DDRPHY, "hclk_ddrphy", "hclk_bus_root", CLK_IGNORE_UNUSED,
			RK3506_CLKGATE_CON(7), 1, GFLAGS),
	GATE(PCLK_DDRMON, "pclk_ddrmon", "pclk_bus_root", CLK_IGNORE_UNUSED,
			RK3506_CLKGATE_CON(7), 2, GFLAGS),
	GATE(CLK_DDRMON_OSC, "clk_ddrmon_osc", "xin24m_gate", CLK_IGNORE_UNUSED,
			RK3506_CLKGATE_CON(7), 3, GFLAGS),
	GATE(PCLK_STDBY, "pclk_stdby", "pclk_bus_root", CLK_IGNORE_UNUSED,
			RK3506_CLKGATE_CON(7), 4, GFLAGS),
	GATE(HCLK_USBOTG0, "hclk_usbotg0", "hclk_bus_root", 0,
			RK3506_CLKGATE_CON(7), 5, GFLAGS),
	GATE(HCLK_USBOTG0_PMU, "hclk_usbotg0_pmu", "hclk_bus_root", 0,
			RK3506_CLKGATE_CON(7), 6, GFLAGS),
	GATE(CLK_USBOTG0_ADP, "clk_usbotg0_adp", "clk_32k", 0,
			RK3506_CLKGATE_CON(7), 7, GFLAGS),
	GATE(HCLK_USBOTG1, "hclk_usbotg1", "hclk_bus_root", 0,
			RK3506_CLKGATE_CON(7), 8, GFLAGS),
	GATE(HCLK_USBOTG1_PMU, "hclk_usbotg1_pmu", "hclk_bus_root", 0,
			RK3506_CLKGATE_CON(7), 9, GFLAGS),
	GATE(CLK_USBOTG1_ADP, "clk_usbotg1_adp", "clk_32k", 0,
			RK3506_CLKGATE_CON(7), 10, GFLAGS),
	GATE(PCLK_USBPHY, "pclk_usbphy", "pclk_bus_root", 0,
			RK3506_CLKGATE_CON(7), 11, GFLAGS),
	GATE(ACLK_DMA2DDR, "aclk_dma2ddr", "aclk_bus_root", CLK_IGNORE_UNUSED,
			RK3506_CLKGATE_CON(8), 0, GFLAGS),
	GATE(PCLK_DMA2DDR, "pclk_dma2ddr", "pclk_bus_root", CLK_IGNORE_UNUSED,
			RK3506_CLKGATE_CON(8), 1, GFLAGS),
	COMPOSITE_NOMUX(STCLK_M0, "stclk_m0", "xin24m_gate", 0,
			RK3506_CLKSEL_CON(23), 9, 6, DFLAGS,
			RK3506_CLKGATE_CON(8), 2, GFLAGS),
	COMPOSITE(CLK_DDRPHY, "clk_ddrphy", gpll_v0pll_v1pll_parents_p, CLK_IGNORE_UNUSED,
			RK3506_PMU_CLKSEL_CON(4), 4, 2, MFLAGS, 0, 4, DFLAGS,
			RK3506_PMU_CLKGATE_CON(1), 10, GFLAGS),
	FACTOR(CLK_DDRC_SRC, "clk_ddrc_src", "clk_ddrphy", 0, 1, 4),
	GATE(ACLK_DDRC_0, "aclk_ddrc_0", "clk_ddrc_src", CLK_IGNORE_UNUSED,
			RK3506_CLKGATE_CON(10), 0, GFLAGS),
	GATE(ACLK_DDRC_1, "aclk_ddrc_1", "clk_ddrc_src", CLK_IGNORE_UNUSED,
			RK3506_CLKGATE_CON(10), 1, GFLAGS),
	GATE(CLK_DDRC, "clk_ddrc", "clk_ddrc_src", CLK_IS_CRITICAL,
			RK3506_CLKGATE_CON(10), 3, GFLAGS),
	GATE(CLK_DDRMON, "clk_ddrmon", "clk_ddrc_src", CLK_IGNORE_UNUSED,
			RK3506_CLKGATE_CON(10), 4, GFLAGS),

	/* ls peri */
	COMPOSITE(HCLK_LSPERI_ROOT, "hclk_lsperi_root", gpll_v0pll_v1pll_div_parents_p, 0,
			RK3506_CLKSEL_CON(29), 5, 2, MFLAGS, 0, 5, DFLAGS,
			RK3506_CLKGATE_CON(11), 0, GFLAGS),
	GATE(PCLK_LSPERI_ROOT, "pclk_lsperi_root", "hclk_lsperi_root", 0,
			RK3506_CLKGATE_CON(11), 1, GFLAGS),
	GATE(PCLK_UART0, "pclk_uart0", "pclk_lsperi_root", 0,
			RK3506_CLKGATE_CON(11), 4, GFLAGS),
	GATE(PCLK_UART1, "pclk_uart1", "pclk_lsperi_root", 0,
			RK3506_CLKGATE_CON(11), 5, GFLAGS),
	GATE(PCLK_UART2, "pclk_uart2", "pclk_lsperi_root", 0,
			RK3506_CLKGATE_CON(11), 6, GFLAGS),
	GATE(PCLK_UART3, "pclk_uart3", "pclk_lsperi_root", 0,
			RK3506_CLKGATE_CON(11), 7, GFLAGS),
	GATE(PCLK_UART4, "pclk_uart4", "pclk_lsperi_root", 0,
			RK3506_CLKGATE_CON(11), 8, GFLAGS),
	COMPOSITE(SCLK_UART0, "sclk_uart0", sclk_uart_parents_p, 0,
			RK3506_CLKSEL_CON(29), 12, 3, MFLAGS, 7, 5, DFLAGS,
			RK3506_CLKGATE_CON(11), 9, GFLAGS),
	COMPOSITE(SCLK_UART1, "sclk_uart1", sclk_uart_parents_p, 0,
			RK3506_CLKSEL_CON(30), 5, 3, MFLAGS, 0, 5, DFLAGS,
			RK3506_CLKGATE_CON(11), 10, GFLAGS),
	COMPOSITE(SCLK_UART2, "sclk_uart2", sclk_uart_parents_p, 0,
			RK3506_CLKSEL_CON(30), 13, 3, MFLAGS, 8, 5, DFLAGS,
			RK3506_CLKGATE_CON(11), 11, GFLAGS),
	COMPOSITE(SCLK_UART3, "sclk_uart3", sclk_uart_parents_p, 0,
			RK3506_CLKSEL_CON(31), 5, 3, MFLAGS, 0, 5, DFLAGS,
			RK3506_CLKGATE_CON(11), 12, GFLAGS),
	COMPOSITE(SCLK_UART4, "sclk_uart4", sclk_uart_parents_p, 0,
			RK3506_CLKSEL_CON(31), 13, 3, MFLAGS, 8, 5, DFLAGS,
			RK3506_CLKGATE_CON(11), 13, GFLAGS),
	GATE(PCLK_I2C0, "pclk_i2c0", "pclk_lsperi_root", 0,
			RK3506_CLKGATE_CON(11), 14, GFLAGS),
	COMPOSITE(CLK_I2C0, "clk_i2c0", xin24m_g_gpll_v0pll_v1pll_div_parents_p, 0,
			RK3506_CLKSEL_CON(32), 4, 2, MFLAGS, 0, 4, DFLAGS,
			RK3506_CLKGATE_CON(11), 15, GFLAGS),
	GATE(PCLK_I2C1, "pclk_i2c1", "pclk_lsperi_root", 0,
			RK3506_CLKGATE_CON(12), 0, GFLAGS),
	COMPOSITE(CLK_I2C1, "clk_i2c1", xin24m_g_gpll_v0pll_v1pll_div_parents_p, 0,
			RK3506_CLKSEL_CON(32), 10, 2, MFLAGS, 6, 4, DFLAGS,
			RK3506_CLKGATE_CON(12), 1, GFLAGS),
	GATE(PCLK_I2C2, "pclk_i2c2", "pclk_lsperi_root", 0,
			RK3506_CLKGATE_CON(12), 2, GFLAGS),
	COMPOSITE(CLK_I2C2, "clk_i2c2", xin24m_g_gpll_v0pll_v1pll_div_parents_p, 0,
			RK3506_CLKSEL_CON(33), 4, 2, MFLAGS, 0, 4, DFLAGS,
			RK3506_CLKGATE_CON(12), 3, GFLAGS),
	GATE(PCLK_PWM1, "pclk_pwm1", "pclk_lsperi_root", 0,
			RK3506_CLKGATE_CON(12), 4, GFLAGS),
	COMPOSITE(CLK_PWM1, "clk_pwm1", gpll_v0pll_v1pll_div_parents_p, 0,
			RK3506_CLKSEL_CON(33), 10, 2, MFLAGS, 6, 4, DFLAGS,
			RK3506_CLKGATE_CON(12), 5, GFLAGS),
	GATE(CLK_OSC_PWM1, "clk_osc_pwm1", "xin24m", 0,
			RK3506_CLKGATE_CON(12), 6, GFLAGS),
	GATE(CLK_RC_PWM1, "clk_rc_pwm1", "clk_rc", 0,
			RK3506_CLKGATE_CON(12), 7, GFLAGS),
	COMPOSITE_NODIV(CLK_FREQ_PWM1, "clk_freq_pwm1", clk_pwm_parents_p, 0,
			RK3506_CLKSEL_CON(33), 12, 4, MFLAGS,
			RK3506_CLKGATE_CON(12), 8, GFLAGS),
	COMPOSITE_NODIV(CLK_COUNTER_PWM1, "clk_counter_pwm1", clk_pwm_parents_p, 0,
			RK3506_CLKSEL_CON(34), 0, 4, MFLAGS,
			RK3506_CLKGATE_CON(12), 9, GFLAGS),
	GATE(PCLK_SPI0, "pclk_spi0", "pclk_lsperi_root", 0,
			RK3506_CLKGATE_CON(12), 10, GFLAGS),
	COMPOSITE(CLK_SPI0, "clk_spi0", xin24m_g_gpll_v0pll_v1pll_div_parents_p, 0,
			RK3506_CLKSEL_CON(34), 8, 2, MFLAGS, 4, 4, DFLAGS,
			RK3506_CLKGATE_CON(12), 11, GFLAGS),
	GATE(PCLK_SPI1, "pclk_spi1", "pclk_lsperi_root", 0,
			RK3506_CLKGATE_CON(12), 12, GFLAGS),
	COMPOSITE(CLK_SPI1, "clk_spi1", xin24m_g_gpll_v0pll_v1pll_div_parents_p, 0,
			RK3506_CLKSEL_CON(34), 14, 2, MFLAGS, 10, 4, DFLAGS,
			RK3506_CLKGATE_CON(12), 13, GFLAGS),
	GATE(PCLK_GPIO2, "pclk_gpio2", "pclk_lsperi_root", 0,
			RK3506_CLKGATE_CON(12), 14, GFLAGS),
	COMPOSITE_NODIV(DBCLK_GPIO2, "dbclk_gpio2", xin24m_400k_32k_parents_p, 0,
			RK3506_CLKSEL_CON(35), 0, 2, MFLAGS,
			RK3506_CLKGATE_CON(12), 15, GFLAGS),
	GATE(PCLK_GPIO3, "pclk_gpio3", "pclk_lsperi_root", 0,
			RK3506_CLKGATE_CON(13), 0, GFLAGS),
	COMPOSITE_NODIV(DBCLK_GPIO3, "dbclk_gpio3", xin24m_400k_32k_parents_p, 0,
			RK3506_CLKSEL_CON(35), 2, 2, MFLAGS,
			RK3506_CLKGATE_CON(13), 1, GFLAGS),
	GATE(PCLK_GPIO4, "pclk_gpio4", "pclk_lsperi_root", 0,
			RK3506_CLKGATE_CON(13), 2, GFLAGS),
	COMPOSITE_NODIV(DBCLK_GPIO4, "dbclk_gpio4", xin24m_400k_32k_parents_p, 0,
			RK3506_CLKSEL_CON(35), 4, 2, MFLAGS,
			RK3506_CLKGATE_CON(13), 3, GFLAGS),
	GATE(HCLK_CAN0, "hclk_can0", "hclk_lsperi_root", 0,
			RK3506_CLKGATE_CON(13), 4, GFLAGS),
	COMPOSITE(CLK_CAN0, "clk_can0", clk_can_parents_p, 0,
			RK3506_CLKSEL_CON(35), 11, 3, MFLAGS, 6, 5, DFLAGS,
			RK3506_CLKGATE_CON(13), 5, GFLAGS),
	GATE(HCLK_CAN1, "hclk_can1", "hclk_lsperi_root", 0,
			RK3506_CLKGATE_CON(13), 6, GFLAGS),
	COMPOSITE(CLK_CAN1, "clk_can1", clk_can_parents_p, 0,
			RK3506_CLKSEL_CON(36), 5, 3, MFLAGS, 0, 5, DFLAGS,
			RK3506_CLKGATE_CON(13), 7, GFLAGS),
	GATE(HCLK_PDM, "hclk_pdm", "hclk_lsperi_root", 0,
			RK3506_CLKGATE_CON(13), 8, GFLAGS),
	COMPOSITE(MCLK_PDM, "mclk_pdm", clk_pdm_parents_p, 0,
			RK3506_CLKSEL_CON(37), 5, 4, MFLAGS, 0, 5, DFLAGS,
			RK3506_CLKGATE_CON(13), 9, GFLAGS),
	COMPOSITE(CLKOUT_PDM, "clkout_pdm", clk_pdm_parents_p, 0,
			RK3506_CLKSEL_CON(38), 10, 4, MFLAGS, 0, 10, DFLAGS,
			RK3506_CLKGATE_CON(13), 10, GFLAGS),
	COMPOSITE(MCLK_SPDIFTX, "mclk_spdiftx", mclk_sai_asrc_parents_p, 0,
			RK3506_CLKSEL_CON(39), 5, 4, MFLAGS, 0, 5, DFLAGS,
			RK3506_CLKGATE_CON(13), 11, GFLAGS),
	GATE(HCLK_SPDIFTX, "hclk_spdiftx", "hclk_lsperi_root", 0,
			RK3506_CLKGATE_CON(13), 12, GFLAGS),
	GATE(HCLK_SPDIFRX, "hclk_spdifrx", "hclk_lsperi_root", 0,
			RK3506_CLKGATE_CON(13), 13, GFLAGS),
	COMPOSITE(MCLK_SPDIFRX, "mclk_spdifrx", gpll_v0pll_v1pll_g_parents_p, 0,
			RK3506_CLKSEL_CON(39), 14, 2, MFLAGS, 9, 5, DFLAGS,
			RK3506_CLKGATE_CON(13), 14, GFLAGS),
	COMPOSITE(MCLK_SAI0, "mclk_sai0", mclk_sai_asrc_parents_p, 0,
			RK3506_CLKSEL_CON(40), 8, 4, MFLAGS, 0, 8, DFLAGS,
			RK3506_CLKGATE_CON(13), 15, GFLAGS),
	GATE(HCLK_SAI0, "hclk_sai0", "hclk_lsperi_root", 0,
			RK3506_CLKGATE_CON(14), 0, GFLAGS),
	GATE(MCLK_OUT_SAI0, "mclk_out_sai0", "mclk_sai0", 0,
			RK3506_CLKGATE_CON(14), 1, GFLAGS),
	COMPOSITE(MCLK_SAI1, "mclk_sai1", mclk_sai_asrc_parents_p, 0,
			RK3506_CLKSEL_CON(41), 8, 4, MFLAGS, 0, 8, DFLAGS,
			RK3506_CLKGATE_CON(14), 2, GFLAGS),
	GATE(HCLK_SAI1, "hclk_sai1", "hclk_lsperi_root", 0,
			RK3506_CLKGATE_CON(14), 3, GFLAGS),
	GATE(MCLK_OUT_SAI1, "mclk_out_sai1", "mclk_sai1", 0,
			RK3506_CLKGATE_CON(14), 4, GFLAGS),
	GATE(HCLK_ASRC0, "hclk_asrc0", "hclk_lsperi_root", 0,
			RK3506_CLKGATE_CON(14), 5, GFLAGS),
	COMPOSITE(CLK_ASRC0, "clk_asrc0", gpll_v0pll_v1pll_g_parents_p, 0,
			RK3506_CLKSEL_CON(42), 5, 2, MFLAGS, 0, 5, DFLAGS,
			RK3506_CLKGATE_CON(14), 6, GFLAGS),
	GATE(HCLK_ASRC1, "hclk_asrc1", "hclk_lsperi_root", 0,
			RK3506_CLKGATE_CON(14), 7, GFLAGS),
	COMPOSITE(CLK_ASRC1, "clk_asrc1", gpll_v0pll_v1pll_g_parents_p, 0,
			RK3506_CLKSEL_CON(42), 12, 2, MFLAGS, 7, 5, DFLAGS,
			RK3506_CLKGATE_CON(14), 8, GFLAGS),
	GATE(PCLK_CRU, "pclk_cru", "pclk_lsperi_root", CLK_IS_CRITICAL,
			RK3506_CLKGATE_CON(14), 9, GFLAGS),
	GATE(PCLK_PMU_ROOT, "pclk_pmu_root", "pclk_lsperi_root", CLK_IS_CRITICAL,
			RK3506_CLKGATE_CON(14), 10, GFLAGS),
	COMPOSITE_NODIV(MCLK_ASRC0, "mclk_asrc0", mclk_sai_asrc_parents_p, 0,
			RK3506_CLKSEL_CON(46), 0, 4, MFLAGS,
			RK3506_CLKGATE_CON(16), 0, GFLAGS),
	COMPOSITE_NODIV(MCLK_ASRC1, "mclk_asrc1", mclk_sai_asrc_parents_p, 0,
			RK3506_CLKSEL_CON(46), 4, 4, MFLAGS,
			RK3506_CLKGATE_CON(16), 1, GFLAGS),
	COMPOSITE_NODIV(MCLK_ASRC2, "mclk_asrc2", mclk_sai_asrc_parents_p, 0,
			RK3506_CLKSEL_CON(46), 8, 4, MFLAGS,
			RK3506_CLKGATE_CON(16), 2, GFLAGS),
	COMPOSITE_NODIV(MCLK_ASRC3, "mclk_asrc3", mclk_sai_asrc_parents_p, 0,
			RK3506_CLKSEL_CON(46), 12, 4, MFLAGS,
			RK3506_CLKGATE_CON(16), 3, GFLAGS),
	COMPOSITE_NODIV(LRCK_ASRC0_SRC, "lrck_asrc0_src", lrck_asrc_parents_p, 0,
			RK3506_CLKSEL_CON(47), 0, 4, MFLAGS,
			RK3506_CLKGATE_CON(16), 4, GFLAGS),
	COMPOSITE_NODIV(LRCK_ASRC0_DST, "lrck_asrc0_dst", lrck_asrc_parents_p, 0,
			RK3506_CLKSEL_CON(47), 4, 4, MFLAGS,
			RK3506_CLKGATE_CON(16), 5, GFLAGS),
	COMPOSITE_NODIV(LRCK_ASRC1_SRC, "lrck_asrc1_src", lrck_asrc_parents_p, 0,
			RK3506_CLKSEL_CON(47), 8, 4, MFLAGS,
			RK3506_CLKGATE_CON(16), 6, GFLAGS),
	COMPOSITE_NODIV(LRCK_ASRC1_DST, "lrck_asrc1_dst", lrck_asrc_parents_p, 0,
			RK3506_CLKSEL_CON(47), 12, 4, MFLAGS,
			RK3506_CLKGATE_CON(16), 7, GFLAGS),

	/* hs peri */
	COMPOSITE(ACLK_HSPERI_ROOT, "aclk_hsperi_root", gpll_v0pll_v1pll_div_parents_p, CLK_IS_CRITICAL,
			RK3506_CLKSEL_CON(49), 5, 2, MFLAGS, 0, 5, DFLAGS,
			RK3506_CLKGATE_CON(17), 0, GFLAGS),
	GATE(HCLK_HSPERI_ROOT, "hclk_hsperi_root", "aclk_hsperi_root", CLK_IS_CRITICAL,
			RK3506_CLKGATE_CON(17), 1, GFLAGS),
	GATE(PCLK_HSPERI_ROOT, "pclk_hsperi_root", "hclk_hsperi_root", CLK_IS_CRITICAL,
			RK3506_CLKGATE_CON(17), 2, GFLAGS),
	COMPOSITE(CCLK_SRC_SDMMC, "cclk_src_sdmmc", cclk_src_sdmmc_parents_p, 0,
			RK3506_CLKSEL_CON(49), 13, 2, MFLAGS, 7, 6, DFLAGS,
			RK3506_CLKGATE_CON(17), 6, GFLAGS),
	GATE(HCLK_SDMMC, "hclk_sdmmc", "hclk_hsperi_root", 0,
			RK3506_CLKGATE_CON(17), 7, GFLAGS),
	GATE(HCLK_FSPI, "hclk_fspi", "hclk_hsperi_root", 0,
			RK3506_CLKGATE_CON(17), 8, GFLAGS),
	COMPOSITE(SCLK_FSPI, "sclk_fspi", xin24m_g_gpll_v0pll_v1pll_g_parents_p, 0,
			RK3506_CLKSEL_CON(50), 5, 2, MFLAGS, 0, 5, DFLAGS,
			RK3506_CLKGATE_CON(17), 9, GFLAGS),
	GATE(PCLK_SPI2, "pclk_spi2", "pclk_hsperi_root", 0,
			RK3506_CLKGATE_CON(17), 10, GFLAGS),
	GATE(ACLK_MAC0, "aclk_mac0", "aclk_hsperi_root", 0,
			RK3506_CLKGATE_CON(17), 11, GFLAGS),
	GATE(ACLK_MAC1, "aclk_mac1", "aclk_hsperi_root", 0,
			RK3506_CLKGATE_CON(17), 12, GFLAGS),
	GATE(PCLK_MAC0, "pclk_mac0", "pclk_hsperi_root", 0,
			RK3506_CLKGATE_CON(17), 13, GFLAGS),
	GATE(PCLK_MAC1, "pclk_mac1", "pclk_hsperi_root", 0,
			RK3506_CLKGATE_CON(17), 14, GFLAGS),
	COMPOSITE_NOMUX(CLK_MAC_ROOT, "clk_mac_root", "gpll", 0,
			RK3506_CLKSEL_CON(50), 7, 5, DFLAGS,
			RK3506_CLKGATE_CON(17), 15, GFLAGS),
	GATE(CLK_MAC0, "clk_mac0", "clk_mac_root", 0,
			RK3506_CLKGATE_CON(18), 0, GFLAGS),
	GATE(CLK_MAC1, "clk_mac1", "clk_mac_root", 0,
			RK3506_CLKGATE_CON(18), 1, GFLAGS),
	COMPOSITE(MCLK_SAI2, "mclk_sai2", mclk_sai_asrc_parents_p, 0,
			RK3506_CLKSEL_CON(51), 8, 4, MFLAGS, 0, 8, DFLAGS,
			RK3506_CLKGATE_CON(18), 2, GFLAGS),
	GATE(HCLK_SAI2, "hclk_sai2", "hclk_hsperi_root", 0,
			RK3506_CLKGATE_CON(18), 3, GFLAGS),
	GATE(MCLK_OUT_SAI2, "mclk_out_sai2", "mclk_sai2", 0,
			RK3506_CLKGATE_CON(18), 4, GFLAGS),
	COMPOSITE(MCLK_SAI3_SRC, "mclk_sai3_src", mclk_sai_asrc_parents_p, 0,
			RK3506_CLKSEL_CON(52), 8, 4, MFLAGS, 0, 8, DFLAGS,
			RK3506_CLKGATE_CON(18), 5, GFLAGS),
	GATE(HCLK_SAI3, "hclk_sai3", "hclk_hsperi_root", 0,
			RK3506_CLKGATE_CON(18), 6, GFLAGS),
	GATE(MCLK_SAI3, "mclk_sai3", "mclk_sai3_src", 0,
			RK3506_CLKGATE_CON(18), 7, GFLAGS),
	GATE(MCLK_OUT_SAI3, "mclk_out_sai3", "mclk_sai3_src", 0,
			RK3506_CLKGATE_CON(18), 8, GFLAGS),
	COMPOSITE(MCLK_SAI4_SRC, "mclk_sai4_src", mclk_sai_asrc_parents_p, 0,
			RK3506_CLKSEL_CON(53), 8, 4, MFLAGS, 0, 8, DFLAGS,
			RK3506_CLKGATE_CON(18), 9, GFLAGS),
	GATE(HCLK_SAI4, "hclk_sai4", "hclk_hsperi_root", 0,
			RK3506_CLKGATE_CON(18), 10, GFLAGS),
	GATE(MCLK_SAI4, "mclk_sai4", "mclk_sai4_src", 0,
			RK3506_CLKGATE_CON(18), 11, GFLAGS),
	GATE(HCLK_DSM, "hclk_dsm", "hclk_hsperi_root", 0,
			RK3506_CLKGATE_CON(18), 12, GFLAGS),
	GATE(MCLK_DSM, "mclk_dsm", "mclk_sai3_src", 0,
			RK3506_CLKGATE_CON(18), 13, GFLAGS),
	GATE(PCLK_AUDIO_ADC, "pclk_audio_adc", "pclk_hsperi_root", 0,
			RK3506_CLKGATE_CON(18), 14, GFLAGS),
	GATE(MCLK_AUDIO_ADC, "mclk_audio_adc", "mclk_sai4_src", 0,
			RK3506_CLKGATE_CON(18), 15, GFLAGS),
	FACTOR(MCLK_AUDIO_ADC_DIV4, "mclk_audio_adc_div4", "mclk_audio_adc", 0, 1, 4),
	GATE(PCLK_SARADC, "pclk_saradc", "pclk_hsperi_root", 0,
			RK3506_CLKGATE_CON(19), 0, GFLAGS),
	COMPOSITE(CLK_SARADC, "clk_saradc", xin24m_400k_32k_parents_p, 0,
			RK3506_CLKSEL_CON(54), 4, 2, MFLAGS, 0, 4, DFLAGS,
			RK3506_CLKGATE_CON(19), 1, GFLAGS),
	GATE(PCLK_OTPC_NS, "pclk_otpc_ns", "pclk_hsperi_root", 0,
			RK3506_CLKGATE_CON(19), 3, GFLAGS),
	GATE(CLK_SBPI_OTPC_NS, "clk_sbpi_otpc_ns", "xin24m_gate", 0,
			RK3506_CLKGATE_CON(19), 4, GFLAGS),
	FACTOR(CLK_USER_OTPC_NS, "clk_user_otpc_ns", "clk_sbpi_otpc_ns", 0, 1, 2),
	GATE(PCLK_UART5, "pclk_uart5", "pclk_hsperi_root", 0,
			RK3506_CLKGATE_CON(19), 6, GFLAGS),
	COMPOSITE(SCLK_UART5, "sclk_uart5", sclk_uart_parents_p, 0,
			RK3506_CLKSEL_CON(54), 11, 3, MFLAGS, 6, 5, DFLAGS,
			RK3506_CLKGATE_CON(19), 7, GFLAGS),
	GATE(PCLK_GPIO234_IOC, "pclk_gpio234_ioc", "pclk_hsperi_root", CLK_IS_CRITICAL,
			RK3506_CLKGATE_CON(19), 8, GFLAGS),
	COMPOSITE(CLK_MAC_PTP_ROOT, "clk_mac_ptp_root", clk_mac_ptp_root_parents_p, 0,
			RK3506_CLKSEL_CON(55), 5, 2, MFLAGS, 0, 5, DFLAGS,
			RK3506_CLKGATE_CON(19), 9, GFLAGS),
	GATE(CLK_MAC0_PTP, "clk_mac0_ptp", "clk_mac_ptp_root", 0,
			RK3506_CLKGATE_CON(19), 10, GFLAGS),
	GATE(CLK_MAC1_PTP, "clk_mac1_ptp", "clk_mac_ptp_root", 0,
			RK3506_CLKGATE_CON(19), 11, GFLAGS),
	COMPOSITE(ACLK_VIO_ROOT, "aclk_vio_root", gpll_v0pll_v1pll_g_parents_p, 0,
			RK3506_CLKSEL_CON(58), 5, 2, MFLAGS, 0, 5, DFLAGS,
			RK3506_CLKGATE_CON(21), 0, GFLAGS),
	COMPOSITE(HCLK_VIO_ROOT, "hclk_vio_root", gpll_v0pll_v1pll_div_parents_p, 0,
			RK3506_CLKSEL_CON(58), 12, 2, MFLAGS, 7, 5, DFLAGS,
			RK3506_CLKGATE_CON(21), 1, GFLAGS),
	GATE(PCLK_VIO_ROOT, "pclk_vio_root", "hclk_vio_root", 0,
			RK3506_CLKGATE_CON(21), 2, GFLAGS),
	GATE(HCLK_RGA, "hclk_rga", "hclk_vio_root", 0,
			RK3506_CLKGATE_CON(21), 6, GFLAGS),
	GATE(ACLK_RGA, "aclk_rga", "aclk_vio_root", 0,
			RK3506_CLKGATE_CON(21), 7, GFLAGS),
	COMPOSITE(CLK_CORE_RGA, "clk_core_rga", gpll_v0pll_v1pll_g_parents_p, 0,
			RK3506_CLKSEL_CON(59), 5, 2, MFLAGS, 0, 5, DFLAGS,
			RK3506_CLKGATE_CON(21), 8, GFLAGS),
	GATE(ACLK_VOP, "aclk_vop", "aclk_vio_root", 0,
			RK3506_CLKGATE_CON(21), 9, GFLAGS),
	GATE(HCLK_VOP, "hclk_vop", "hclk_vio_root", 0,
			RK3506_CLKGATE_CON(21), 10, GFLAGS),
	COMPOSITE(DCLK_VOP, "dclk_vop", dclk_vop_parents_p, 0,
			RK3506_CLKSEL_CON(60), 8, 3, MFLAGS, 0, 8, DFLAGS,
			RK3506_CLKGATE_CON(21), 11, GFLAGS),
	GATE(PCLK_DPHY, "pclk_dphy", "pclk_vio_root", 0,
			RK3506_CLKGATE_CON(21), 12, GFLAGS),
	GATE(PCLK_DSI_HOST, "pclk_dsi_host", "pclk_vio_root", 0,
			RK3506_CLKGATE_CON(21), 13, GFLAGS),
	GATE(PCLK_TSADC, "pclk_tsadc", "pclk_vio_root", 0,
			RK3506_CLKGATE_CON(21), 14, GFLAGS),
	COMPOSITE_NOMUX(CLK_TSADC, "clk_tsadc", "xin24m_gate", 0,
			RK3506_CLKSEL_CON(61), 0, 8, DFLAGS,
			RK3506_CLKGATE_CON(21), 15, GFLAGS),
	COMPOSITE_NOMUX(CLK_TSADC_TSEN, "clk_tsadc_tsen", "xin24m_gate", 0,
			RK3506_CLKSEL_CON(61), 8, 3, DFLAGS,
			RK3506_CLKGATE_CON(22), 0, GFLAGS),
	GATE(PCLK_GPIO1_IOC, "pclk_gpio1_ioc", "pclk_vio_root", CLK_IS_CRITICAL,
			RK3506_CLKGATE_CON(22), 1, GFLAGS),

	/* pmu */
	GATE(CLK_PMU, "clk_pmu", "xin24m", CLK_IGNORE_UNUSED,
			RK3506_PMU_CLKGATE_CON(0), 1, GFLAGS),
	GATE(PCLK_PMU, "pclk_pmu", "pclk_pmu_root", CLK_IGNORE_UNUSED,
			RK3506_PMU_CLKGATE_CON(0), 2, GFLAGS),
	GATE(PCLK_PMU_CRU, "pclk_pmu_cru", "pclk_pmu_root", CLK_IGNORE_UNUSED,
			RK3506_PMU_CLKGATE_CON(0), 4, GFLAGS),
	GATE(PCLK_PMU_GRF, "pclk_pmu_grf", "pclk_pmu_root", CLK_IGNORE_UNUSED,
			RK3506_PMU_CLKGATE_CON(0), 5, GFLAGS),
	GATE(PCLK_GPIO0_IOC, "pclk_gpio0_ioc", "pclk_pmu_root", CLK_IS_CRITICAL,
			RK3506_PMU_CLKGATE_CON(0), 7, GFLAGS),
	GATE(PCLK_GPIO0, "pclk_gpio0", "pclk_pmu_root", 0,
			RK3506_PMU_CLKGATE_CON(0), 8, GFLAGS),
	COMPOSITE_NODIV(DBCLK_GPIO0, "dbclk_gpio0", dbclk_gpio0_parents_p, 0,
			RK3506_PMU_CLKSEL_CON(0), 0, 2, MFLAGS,
			RK3506_PMU_CLKGATE_CON(0), 9, GFLAGS),
	GATE(PCLK_GPIO1_SHADOW, "pclk_gpio1_shadow", "pclk_pmu_root", 0,
			RK3506_PMU_CLKGATE_CON(0), 10, GFLAGS),
	COMPOSITE_NODIV(DBCLK_GPIO1_SHADOW, "dbclk_gpio1_shadow", dbclk_gpio0_parents_p, 0,
			RK3506_PMU_CLKSEL_CON(0), 2, 2, MFLAGS,
			RK3506_PMU_CLKGATE_CON(0), 11, GFLAGS),
	GATE(PCLK_PMU_HP_TIMER, "pclk_pmu_hp_timer", "pclk_pmu_root", CLK_IGNORE_UNUSED,
			RK3506_PMU_CLKGATE_CON(0), 12, GFLAGS),
	MUX(CLK_PMU_HP_TIMER, "clk_pmu_hp_timer", clk_pmu_hp_timer_parents_p, CLK_IGNORE_UNUSED,
			RK3506_PMU_CLKSEL_CON(0), 4, 2, MFLAGS),
	GATE(PCLK_PWM0, "pclk_pwm0", "pclk_pmu_root", 0,
			RK3506_PMU_CLKGATE_CON(0), 15, GFLAGS),
	COMPOSITE_NOMUX(CLK_PWM0, "clk_pwm0", "clk_gpll_div_100m", 0,
			RK3506_PMU_CLKSEL_CON(0), 6, 4, DFLAGS,
			RK3506_PMU_CLKGATE_CON(1), 0, GFLAGS),
	GATE(CLK_OSC_PWM0, "clk_osc_pwm0", "xin24m", 0,
			RK3506_PMU_CLKGATE_CON(1), 1, GFLAGS),
	GATE(CLK_RC_PWM0, "clk_rc_pwm0", "clk_rc", 0,
			RK3506_PMU_CLKGATE_CON(1), 2, GFLAGS),
	COMPOSITE_NOMUX(CLK_MAC_OUT, "clk_mac_out", "gpll", 0,
			RK3506_PMU_CLKSEL_CON(0), 10, 6, DFLAGS,
			RK3506_PMU_CLKGATE_CON(1), 3, GFLAGS),
	COMPOSITE(CLK_REF_OUT0, "clk_ref_out0", clk_ref_out_parents_p, 0,
			RK3506_PMU_CLKSEL_CON(1), 6, 2, MFLAGS, 0, 6, DFLAGS,
			RK3506_PMU_CLKGATE_CON(1), 4, GFLAGS),
	COMPOSITE(CLK_REF_OUT1, "clk_ref_out1", clk_ref_out_parents_p, 0,
			RK3506_PMU_CLKSEL_CON(1), 14, 2, MFLAGS, 8, 6, DFLAGS,
			RK3506_PMU_CLKGATE_CON(1), 5, GFLAGS),
	MUX(CLK_32K_FRAC_MUX, "clk_32k_frac_mux", clk_32k_frac_parents_p, 0,
			RK3506_PMU_CLKSEL_CON(3), 0, 2, MFLAGS),
	COMPOSITE_FRAC(CLK_32K_FRAC, "clk_32k_frac", "clk_32k_frac_mux", 0,
			RK3506_PMU_CLKSEL_CON(2), 0,
			RK3506_PMU_CLKGATE_CON(1), 6, GFLAGS),
	COMPOSITE_NOMUX(CLK_32K_RC, "clk_32k_rc", "clk_rc", CLK_IS_CRITICAL,
			RK3506_PMU_CLKSEL_CON(3), 2, 5, DFLAGS,
			RK3506_PMU_CLKGATE_CON(1), 7, GFLAGS),
	COMPOSITE_NODIV(CLK_32K, "clk_32k", clk_32k_parents_p, CLK_IS_CRITICAL,
			RK3506_PMU_CLKSEL_CON(3), 7, 2, MFLAGS,
			RK3506_PMU_CLKGATE_CON(1), 8, GFLAGS),
	COMPOSITE_NODIV(CLK_32K_PMU, "clk_32k_pmu", clk_32k_parents_p, CLK_IS_CRITICAL,
			RK3506_PMU_CLKSEL_CON(3), 9, 2, MFLAGS,
			RK3506_PMU_CLKGATE_CON(1), 9, GFLAGS),
	GATE(CLK_PMU_32K, "clk_pmu_32k", "clk_32k_pmu", CLK_IGNORE_UNUSED,
			RK3506_PMU_CLKGATE_CON(0), 3, GFLAGS),
	GATE(CLK_PMU_HP_TIMER_32K, "clk_pmu_hp_timer_32k", "clk_32k_pmu", CLK_IGNORE_UNUSED,
			RK3506_PMU_CLKGATE_CON(0), 14, GFLAGS),
	GATE(PCLK_TOUCH_KEY, "pclk_touch_key", "pclk_pmu_root", CLK_IGNORE_UNUSED,
			RK3506_PMU_CLKGATE_CON(1), 12, GFLAGS),
	GATE(CLK_TOUCH_KEY, "clk_touch_key", "xin24m", CLK_IGNORE_UNUSED,
			RK3506_PMU_CLKGATE_CON(1), 13, GFLAGS),
	COMPOSITE(CLK_REF_PHY_PLL, "clk_ref_phy_pll", gpll_v0pll_v1pll_parents_p, 0,
			RK3506_PMU_CLKSEL_CON(4), 13, 2, MFLAGS, 6, 7, DFLAGS,
			RK3506_PMU_CLKGATE_CON(1), 14, GFLAGS),
	MUX(CLK_REF_PHY_PMU_MUX, "clk_ref_phy_pmu_mux", clk_ref_phy_pmu_mux_parents_p, 0,
			RK3506_PMU_CLKSEL_CON(4), 15, 1, MFLAGS),
	GATE(CLK_WIFI_OUT, "clk_wifi_out", "xin24m", 0,
			RK3506_PMU_CLKGATE_CON(2), 0, GFLAGS),
	MUX(CLK_V0PLL_REF, "clk_v0pll_ref", clk_vpll_ref_parents_p, CLK_IGNORE_UNUSED,
			RK3506_PMU_CLKSEL_CON(6), 0, 1, MFLAGS),
	MUX(CLK_V1PLL_REF, "clk_v1pll_ref", clk_vpll_ref_parents_p, CLK_IGNORE_UNUSED,
			RK3506_PMU_CLKSEL_CON(6), 1, 1, MFLAGS),

	/* secure ns */
	GATE(CLK_CORE_CRYPTO_NS, "clk_core_crypto_ns", "clk_core_crypto", 0,
			RK3506_CLKGATE_CON(5), 12, GFLAGS),
	GATE(CLK_PKA_CRYPTO_NS, "clk_pka_crypto_ns", "clk_pka_crypto", 0,
			RK3506_CLKGATE_CON(5), 13, GFLAGS),

	/* io */
	GATE(CLK_SPI2, "clk_spi2", "clk_spi2_io", 0,
			RK3506_CLKGATE_CON(20), 0, GFLAGS),
};

static void __init rk3506_clk_init(struct device_node *np)
{
	struct rockchip_clk_provider *ctx;
	unsigned long clk_nr_clks;
	void __iomem *reg_base;

	clk_nr_clks = rockchip_clk_find_max_clk_id(rk3506_clk_branches,
						   ARRAY_SIZE(rk3506_clk_branches)) + 1;

	reg_base = of_iomap(np, 0);
	if (!reg_base) {
		pr_err("%s: could not map cru region\n", __func__);
		return;
	}

	ctx = rockchip_clk_init(np, reg_base, clk_nr_clks);
	if (IS_ERR(ctx)) {
		pr_err("%s: rockchip clk init failed\n", __func__);
		iounmap(reg_base);
		return;
	}

	rockchip_clk_register_plls(ctx, rk3506_pll_clks,
				   ARRAY_SIZE(rk3506_pll_clks),
				   0);

	rockchip_clk_register_armclk_multi_pll(ctx, &rk3506_armclk,
					       rk3506_cpuclk_rates,
					       ARRAY_SIZE(rk3506_cpuclk_rates));

	rockchip_clk_register_branches(ctx, rk3506_clk_branches,
				       ARRAY_SIZE(rk3506_clk_branches));

	rk3506_rst_init(np, reg_base);

	rockchip_register_restart_notifier(ctx, RK3506_GLB_SRST_FST, NULL);

	rockchip_clk_of_add_provider(np, ctx);

	/* pvtpll src init */
	writel_relaxed(PVTPLL_SRC_SEL_PVTPLL, reg_base + RK3506_CLKSEL_CON(15));
}

CLK_OF_DECLARE(rk3506_cru, "rockchip,rk3506-cru", rk3506_clk_init);

struct clk_rk3506_inits {
	void (*inits)(struct device_node *np);
};

static const struct clk_rk3506_inits clk_rk3506_cru_init = {
	.inits = rk3506_clk_init,
};

static const struct of_device_id clk_rk3506_match_table[] = {
	{
		.compatible = "rockchip,rk3506-cru",
		.data = &clk_rk3506_cru_init,
	},
	{ }
};

static int clk_rk3506_probe(struct platform_device *pdev)
{
	const struct clk_rk3506_inits *init_data;
	struct device *dev = &pdev->dev;

	init_data = device_get_match_data(dev);
	if (!init_data)
		return -EINVAL;

	if (init_data->inits)
		init_data->inits(dev->of_node);

	return 0;
}

static struct platform_driver clk_rk3506_driver = {
	.probe		= clk_rk3506_probe,
	.driver		= {
		.name	= "clk-rk3506",
		.of_match_table = clk_rk3506_match_table,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver_probe(clk_rk3506_driver, clk_rk3506_probe);
