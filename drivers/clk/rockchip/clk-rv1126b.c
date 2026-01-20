// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024 Rockchip Electronics Co., Ltd.
 * Author: Elaine Zhang <zhangqing@rock-chips.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/syscore_ops.h>
#include <dt-bindings/clock/rockchip,rv1126b-cru.h>
#include "clk.h"

#define RV1126B_FRAC_MAX_PRATE		1200000000

#define PVTPLL_SRC_SEL_PVTPLL		(BIT(0) | BIT(16))

enum rv1126b_plls {
	gpll, cpll, aupll, dpll
};

static struct rockchip_pll_rate_table rv1126b_pll_rates[] = {
	/* _mhz, _refdiv, _fbdiv, _postdiv1, _postdiv2, _dsmpd, _frac */
	RK3036_PLL_RATE(1200000000, 1, 100, 2, 1, 1, 0),
	RK3036_PLL_RATE(1188000000, 1, 99, 2, 1, 1, 0),
	RK3036_PLL_RATE(1179648000, 1, 49, 1, 1, 0, 2550137),
	RK3036_PLL_RATE(1000000000, 3, 250, 2, 1, 1, 0),
	RK3036_PLL_RATE(993484800, 1, 41, 1, 1, 0, 6630355),
	RK3036_PLL_RATE(983040000, 1, 40, 1, 1, 0, 16106127),
	RK3036_PLL_RATE(903168000, 1, 75, 2, 1, 0, 4429185),
	{ /* sentinel */ },
};

#define RV1126B_DIV_ACLK_CORE_MASK	0x1f
#define RV1126B_DIV_ACLK_CORE_SHIFT	0
#define RV1126B_DIV_PCLK_CORE_MASK	0x1f
#define RV1126B_DIV_PCLK_CORE_SHIFT	8
#define RV1126B_CORE_SEL_MASK		0x1
#define RV1126B_CORE_SEL_SHIFT		1

#define RV1126B_CLKSEL0(_aclk_core)						\
{										\
	.reg = RV1126B_CORECLKSEL_CON(2),					\
	.val = HIWORD_UPDATE(_aclk_core - 1, RV1126B_DIV_ACLK_CORE_MASK,	\
			     RV1126B_DIV_ACLK_CORE_SHIFT),			\
}

#define RV1126B_CLKSEL1(_pclk_dbg)						\
{										\
	.reg = RV1126B_CORECLKSEL_CON(2),					\
	.val = HIWORD_UPDATE(_pclk_dbg - 1, RV1126B_DIV_PCLK_CORE_MASK,		\
			     RV1126B_DIV_PCLK_CORE_SHIFT),			\
}

#define RV1126B_CPUCLK_RATE(_prate, _aclk_core, _pclk_dbg)			\
{										\
	.prate = _prate,							\
	.divs = {								\
		RV1126B_CLKSEL0(_aclk_core),					\
		RV1126B_CLKSEL1(_pclk_dbg),					\
	},									\
}

static struct rockchip_cpuclk_rate_table rv1126b_cpuclk_rates[] __initdata = {
	RV1126B_CPUCLK_RATE(1608000000, 4, 10),
	RV1126B_CPUCLK_RATE(1512000000, 4, 10),
	RV1126B_CPUCLK_RATE(1416000000, 4, 10),
	RV1126B_CPUCLK_RATE(1296000000, 3, 10),
	RV1126B_CPUCLK_RATE(1200000000, 3, 10),
	RV1126B_CPUCLK_RATE(1188000000, 3, 8),
	RV1126B_CPUCLK_RATE(1104000000, 2, 8),
	RV1126B_CPUCLK_RATE(1008000000, 2, 8),
	RV1126B_CPUCLK_RATE(816000000, 2, 6),
	RV1126B_CPUCLK_RATE(600000000, 2, 4),
	RV1126B_CPUCLK_RATE(594000000, 2, 4),
	RV1126B_CPUCLK_RATE(408000000, 1, 3),
	RV1126B_CPUCLK_RATE(396000000, 1, 3),
};

PNAME(mux_pll_p)			= { "xin24m" };
PNAME(mux_gpll_cpll_p)			= { "gpll", "cpll" };
PNAME(mux_gpll_aupll_p)			= { "gpll", "aupll" };
PNAME(mux_gpll_aupll_cpll_p)		= { "gpll", "aupll", "cpll" };
PNAME(mux_gpll_cpll_24m_p)		= { "gpll", "cpll", "xin24m" };
PNAME(mux_cpll_24m_p)			= { "cpll", "xin24m" };
PNAME(mux_24m_gpll_aupll_cpll_p)	= { "xin24m", "gpll", "aupll", "cpll" };
PNAME(mux_24m_gpll_cpll_p)		= { "xin24m", "gpll", "cpll" };
PNAME(mux_24m_gpll_aupll_p)		= { "xin24m", "gpll", "aupll" };
PNAME(mux_sclk_uart_src_p)		= { "xin24m", "clk_cm_frac0", "clk_cm_frac1",
					    "clk_cm_frac2", "clk_uart_frac0", "clk_uart_frac1" };
PNAME(mclk_sai0_src_p)			= { "xin24m", "clk_cm_frac0", "clk_cm_frac1",
					    "clk_cm_frac2", "clk_audio_frac0", "clk_audio_frac1",
					    "clk_audio_int0", "clk_audio_int1",
					    "mclk_sai0_from_io" };
PNAME(mclk_sai1_src_p)			= { "xin24m", "clk_cm_frac0", "clk_cm_frac1",
					    "clk_cm_frac2", "clk_audio_frac0", "clk_audio_frac1",
					    "clk_audio_int0", "clk_audio_int1",
					    "mclk_sai1_from_io" };
PNAME(mclk_sai2_src_p)			= { "xin24m", "clk_cm_frac0", "clk_cm_frac1",
					    "clk_cm_frac2", "clk_audio_frac0", "clk_audio_frac1",
					    "clk_audio_int0", "clk_audio_int1",
					    "mclk_sai2_from_io" };
PNAME(mux_sai_src_p)			= { "xin24m", "clk_cm_frac0", "clk_cm_frac1",
					    "clk_cm_frac2", "clk_audio_frac0", "clk_audio_frac1",
					    "clk_audio_int0", "clk_audio_int1", "mclk_sai0_from_io",
					    "mclk_sai1_from_io", "mclk_sai2_from_io"};
PNAME(mux_100m_24m_p)			= { "clk_cpll_div10", "xin24m" };
PNAME(mux_200m_24m_p)			= { "clk_gpll_div6", "xin24m" };
PNAME(mux_500m_400m_200m_p)		= { "clk_cpll_div2", "clk_gpll_div3", "clk_gpll_div6" };
PNAME(mux_300m_200m_p)			= { "clk_gpll_div4", "clk_gpll_div6" };
PNAME(mux_500m_400m_300m_p)		= { "clk_cpll_div2", "clk_gpll_div3", "clk_gpll_div4" };
PNAME(mux_333m_200m_p)			= { "clk_cpll_div3", "clk_gpll_div6" };
PNAME(mux_600m_400m_200m_p)		= { "clk_gpll_div2", "clk_gpll_div3", "clk_gpll_div6" };
PNAME(mux_400m_300m_200m_p)		= { "clk_gpll_div3", "clk_gpll_div4", "clk_gpll_div6" };
PNAME(mux_200m_100m_p)			= { "clk_gpll_div6", "clk_cpll_div10" };
PNAME(mux_200m_100m_50m_24m_p)		= { "clk_gpll_div6", "clk_cpll_div10", "clk_cpll_div20",
					    "xin24m" };
PNAME(mux_600m_24m_p)			= { "clk_gpll_div2", "xin24m" };
PNAME(mux_armclk_p)			= { "clk_core_pll", "clk_core_pvtpll" };
PNAME(aclk_npu_root_p)			= { "clk_npu_pll", "clk_npu_pvtpll" };
PNAME(clk_saradc0_p)			= { "clk_saradc0_src", "clk_saradc0_rcosc_io" };
PNAME(clk_core_vepu_p)			= { "clk_vepu_pll", "clk_vepu_pvtpll" };
PNAME(clk_core_fec_p)			= { "clk_core_fec_src", "clk_vcp_pvtpll" };
PNAME(clk_core_aisp_p)			= { "clk_aisp_pll", "clk_vcp_pvtpll" };
PNAME(clk_core_isp_root_p)		= { "clk_isp_pll", "clk_isp_pvtpll" };
PNAME(clk_gmac_ptp_ref_p)		= { "clk_gmac_ptp_ref_src", "clk_gmac_ptp_from_io" };
PNAME(clk_saradc1_p)			= { "clk_saradc1_src", "clk_saradc1_rcosc_io" };
PNAME(clk_saradc2_p)			= { "clk_saradc2_src", "clk_saradc2_rcosc_io" };
PNAME(clk_rcosc_src_p)			= { "xin24m", "clk_rcosc", "clk_rcosc_div2",
					    "clk_rcosc_div3", "clk_rcosc_div4" };
PNAME(busclk_pmu_mux_p)			= { "clk_cpll_div10", "clk_rcosc_src" };
PNAME(clk_xin_rc_div_p)			= { "xin24m", "clk_rcosc_src" };
PNAME(clk_32k_p)			= { "clk_xin_rc_div", "clk_32k_rtc", "clk_32k_io" };
PNAME(mux_24m_32k_p)			= { "xin24m", "clk_32k" };
PNAME(mux_24m_rcosc_buspmu_p)		= { "xin24m", "clk_rcosc_src", "busclk_pmu_src" };
PNAME(mux_24m_rcosc_buspmu_32k_p)	= { "xin24m", "clk_rcosc_src", "busclk_pmu_src",
					    "clk_32k" };
PNAME(sclk_uart0_p)			= { "sclk_uart0_src", "xin24m", "clk_rcosc_src" };
PNAME(clk_osc_rcosc_ctrl_p)		= { "clk_rcosc_src", "clk_testout_out" };
PNAME(lrck_src_asrc_p)			= { "mclk_asrc0", "mclk_asrc1", "mclk_asrc2", "mclk_asrc3",
					    "fs_inter_from_sai0", "fs_inter_from_sai1",
					    "fs_inter_from_sai2", "clkout_pdm"};
PNAME(clk_ref_pipephy_p)		= { "clk_ref_pipephy_cpll_src", "xin24m" };
PNAME(clk_timer0_parents_p)		= { "clk_timer_root", "mclk_sai0_from_io",
					    "sclk_sai0_from_io" };
PNAME(clk_timer1_parents_p)		= { "clk_timer_root", "mclk_sai1_from_io",
					    "sclk_sai1_from_io" };
PNAME(clk_timer2_parents_p)		= { "clk_timer_root", "mclk_sai2_from_io",
					    "sclk_sai2_from_io" };
PNAME(clk_timer3_parents_p)		= { "clk_timer_root", "mclk_asrc0", "mclk_asrc1" };
PNAME(clk_timer4_parents_p)		= { "clk_timer_root", "mclk_asrc2", "mclk_asrc3" };
PNAME(clk_macphy_p)			= { "xin24m", "clk_cpll_div20" };
PNAME(clk_cpll_div10_p)			= { "gpll", "clk_aisp_pll_src" };

static struct rockchip_pll_clock rv1126b_pll_clks[] __initdata = {
	[gpll] = PLL(pll_rk3328, PLL_GPLL, "gpll", mux_pll_p,
		     CLK_IS_CRITICAL, RV1126B_PLL_CON(8),
		     RV1126B_MODE_CON, 2, 10, 0, rv1126b_pll_rates),
	[aupll] = PLL(pll_rk3328, PLL_AUPLL, "aupll", mux_pll_p,
		     CLK_IS_CRITICAL, RV1126B_PLL_CON(0),
		     RV1126B_MODE_CON, 0, 10, 0, rv1126b_pll_rates),
	[cpll] = PLL(pll_rk3328, PLL_CPLL, "cpll", mux_pll_p,
		     CLK_IS_CRITICAL, RV1126B_PERIPLL_CON(0),
		     RV1126B_MODE_CON, 4, 10, 0, rv1126b_pll_rates),
	[dpll] = PLL(pll_rk3328, 0, "dpll", mux_pll_p,
		     CLK_IS_CRITICAL, RV1126B_SUBDDRPLL_CON(0),
		     RV1126B_MODE_CON, 2, 10, 0, rv1126b_pll_rates),
};

#define MFLAGS CLK_MUX_HIWORD_MASK
#define DFLAGS CLK_DIVIDER_HIWORD_MASK
#define GFLAGS (CLK_GATE_HIWORD_MASK | CLK_GATE_SET_TO_DISABLE)

static struct rockchip_clk_branch rv1126b_rcdiv_pmu_fracmux __initdata =
	MUX(CLK_32K, "clk_32k", clk_32k_p, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			RV1126B_PMUCLKSEL_CON(2), 1, 2, MFLAGS);

static struct rockchip_clk_branch rv1126b_clk_branches[] __initdata = {

	FACTOR(0, "clk_rcosc_div2", "clk_rcosc", 0, 1, 2),
	FACTOR(0, "clk_rcosc_div3", "clk_rcosc", 0, 1, 3),
	FACTOR(0, "clk_rcosc_div4", "clk_rcosc", 0, 1, 4),

	/*       Clock Definition       */
	COMPOSITE_NODIV(CLK_AISP_PLL_SRC, "clk_aisp_pll_src", mux_gpll_aupll_cpll_p, 0,
			RV1126B_CLKSEL_CON(62), 4, 2, MFLAGS,
			RV1126B_CLKGATE_CON(5), 4, GFLAGS),
	DIV(CLK_AISP_PLL, "clk_aisp_pll", "clk_aisp_pll_src", 0,
			RV1126B_CLKSEL_CON(62), 0, 3, DFLAGS),

	COMPOSITE(CLK_CPLL_DIV10, "clk_cpll_div10", clk_cpll_div10_p, 0,
			RV1126B_CLKSEL_CON(1), 15, 1, MFLAGS, 5, 5, DFLAGS,
			RV1126B_CLKGATE_CON(0), 1, GFLAGS),
	COMPOSITE_NOMUX(CLK_CPLL_DIV20, "clk_cpll_div20", "cpll", 0,
			RV1126B_CLKSEL_CON(1), 0, 5, DFLAGS,
			RV1126B_CLKGATE_CON(0), 0, GFLAGS),
	COMPOSITE_NOMUX(CLK_CPLL_DIV8, "clk_cpll_div8", "cpll", 0,
			RV1126B_CLKSEL_CON(1), 10, 5, DFLAGS,
			RV1126B_CLKGATE_CON(0), 2, GFLAGS),
	COMPOSITE_NOMUX(CLK_GPLL_DIV8, "clk_gpll_div8", "gpll", 0,
			RV1126B_CLKSEL_CON(2), 0, 5, DFLAGS,
			RV1126B_CLKGATE_CON(0), 3, GFLAGS),
	COMPOSITE_NOMUX(CLK_GPLL_DIV6, "clk_gpll_div6", "gpll", 0,
			RV1126B_CLKSEL_CON(2), 5, 5, DFLAGS,
			RV1126B_CLKGATE_CON(0), 4, GFLAGS),
	COMPOSITE_NOMUX(CLK_GPLL_DIV4, "clk_gpll_div4", "gpll", 0,
			RV1126B_CLKSEL_CON(2), 10, 5, DFLAGS,
			RV1126B_CLKGATE_CON(0), 5, GFLAGS),
	COMPOSITE_NOMUX(CLK_CPLL_DIV3, "clk_cpll_div3", "cpll", 0,
			RV1126B_CLKSEL_CON(3), 0, 5, DFLAGS,
			RV1126B_CLKGATE_CON(0), 6, GFLAGS),
	COMPOSITE_NOMUX(CLK_GPLL_DIV3, "clk_gpll_div3", "gpll", 0,
			RV1126B_CLKSEL_CON(3), 5, 5, DFLAGS,
			RV1126B_CLKGATE_CON(0), 7, GFLAGS),
	COMPOSITE_NOMUX(CLK_CPLL_DIV2, "clk_cpll_div2", "cpll", 0,
			RV1126B_CLKSEL_CON(3), 10, 5, DFLAGS,
			RV1126B_CLKGATE_CON(0), 8, GFLAGS),
	COMPOSITE_NOMUX(CLK_GPLL_DIV2, "clk_gpll_div2", "gpll", 0,
			RV1126B_CLKSEL_CON(4), 0, 5, DFLAGS,
			RV1126B_CLKGATE_CON(0), 9, GFLAGS),
	MUX(CLK_CM_FRAC0_SRC, "clk_cm_frac0_src", mux_24m_gpll_aupll_cpll_p, 0,
			RV1126B_CLKSEL_CON(10), 0, 2, MFLAGS),
	COMPOSITE_FRAC(CLK_CM_FRAC0, "clk_cm_frac0", "clk_cm_frac0_src", 0,
			RV1126B_CLKSEL_CON(25), 0,
			RV1126B_CLKGATE_CON(1), 0, GFLAGS),
	MUX(CLK_CM_FRAC1_SRC, "clk_cm_frac1_src", mux_24m_gpll_aupll_cpll_p, 0,
			RV1126B_CLKSEL_CON(10), 2, 2, MFLAGS),
	COMPOSITE_FRAC(CLK_CM_FRAC1, "clk_cm_frac1", "clk_cm_frac1_src", 0,
			RV1126B_CLKSEL_CON(26), 0,
			RV1126B_CLKGATE_CON(1), 1, GFLAGS),
	MUX(CLK_CM_FRAC2_SRC, "clk_cm_frac2_src", mux_24m_gpll_aupll_cpll_p, 0,
			RV1126B_CLKSEL_CON(10), 4, 2, MFLAGS),
	COMPOSITE_FRAC(CLK_CM_FRAC2, "clk_cm_frac2", "clk_cm_frac2_src", 0,
			RV1126B_CLKSEL_CON(27), 0,
			RV1126B_CLKGATE_CON(1), 2, GFLAGS),
	MUX(CLK_UART_FRAC0_SRC, "clk_uart_frac0_src", mux_24m_gpll_cpll_p, 0,
			RV1126B_CLKSEL_CON(10), 6, 2, MFLAGS),
	COMPOSITE_FRAC(CLK_UART_FRAC0, "clk_uart_frac0", "clk_uart_frac0_src", 0,
			RV1126B_CLKSEL_CON(28), 0,
			RV1126B_CLKGATE_CON(1), 3, GFLAGS),
	MUX(CLK_UART_FRAC1_SRC, "clk_uart_frac1_src", mux_24m_gpll_cpll_p, 0,
			RV1126B_CLKSEL_CON(10), 8, 2, MFLAGS),
	COMPOSITE_FRAC(CLK_UART_FRAC1, "clk_uart_frac1", "clk_uart_frac1_src", 0,
			RV1126B_CLKSEL_CON(29), 0,
			RV1126B_CLKGATE_CON(1), 4, GFLAGS),
	MUX(CLK_AUDIO_FRAC0_SRC, "clk_audio_frac0_src", mux_24m_gpll_aupll_p, 0,
			RV1126B_CLKSEL_CON(10), 10, 2, MFLAGS),
	COMPOSITE_FRAC(CLK_AUDIO_FRAC0, "clk_audio_frac0", "clk_audio_frac0_src", 0,
			RV1126B_CLKSEL_CON(30), 0,
			RV1126B_CLKGATE_CON(1), 5, GFLAGS),
	MUX(CLK_AUDIO_FRAC1_SRC, "clk_audio_frac1_src", mux_24m_gpll_aupll_p, 0,
			RV1126B_CLKSEL_CON(10), 12, 2, MFLAGS),
	COMPOSITE_FRAC(CLK_AUDIO_FRAC1, "clk_audio_frac1", "clk_audio_frac1_src", 0,
			RV1126B_CLKSEL_CON(31), 0,
			RV1126B_CLKGATE_CON(1), 6, GFLAGS),
	COMPOSITE(CLK_AUDIO_INT0, "clk_audio_int0", mux_24m_gpll_aupll_p, 0,
			RV1126B_CLKSEL_CON(11), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RV1126B_CLKGATE_CON(1), 7, GFLAGS),
	COMPOSITE(CLK_AUDIO_INT1, "clk_audio_int1", mux_24m_gpll_aupll_p, 0,
			RV1126B_CLKSEL_CON(11), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RV1126B_CLKGATE_CON(1), 8, GFLAGS),
	COMPOSITE(SCLK_UART0_SRC, "sclk_uart0_src", mux_sclk_uart_src_p, 0,
			RV1126B_CLKSEL_CON(12), 5, 3, MFLAGS, 0, 5, DFLAGS,
			RV1126B_CLKGATE_CON(1), 9, GFLAGS),
	COMPOSITE(SCLK_UART1, "sclk_uart1", mux_sclk_uart_src_p, 0,
			RV1126B_CLKSEL_CON(12), 13, 3, MFLAGS, 8, 5, DFLAGS,
			RV1126B_CLKGATE_CON(1), 10, GFLAGS),
	COMPOSITE(SCLK_UART2, "sclk_uart2", mux_sclk_uart_src_p, 0,
			RV1126B_CLKSEL_CON(13), 5, 3, MFLAGS, 0, 5, DFLAGS,
			RV1126B_CLKGATE_CON(1), 11, GFLAGS),
	COMPOSITE(SCLK_UART3, "sclk_uart3", mux_sclk_uart_src_p, 0,
			RV1126B_CLKSEL_CON(13), 13, 3, MFLAGS, 8, 5, DFLAGS,
			RV1126B_CLKGATE_CON(1), 12, GFLAGS),
	COMPOSITE(SCLK_UART4, "sclk_uart4", mux_sclk_uart_src_p, 0,
			RV1126B_CLKSEL_CON(14), 5, 3, MFLAGS, 0, 5, DFLAGS,
			RV1126B_CLKGATE_CON(1), 13, GFLAGS),
	COMPOSITE(SCLK_UART5, "sclk_uart5", mux_sclk_uart_src_p, 0,
			RV1126B_CLKSEL_CON(14), 13, 3, MFLAGS, 8, 5, DFLAGS,
			RV1126B_CLKGATE_CON(1), 14, GFLAGS),
	COMPOSITE(SCLK_UART6, "sclk_uart6", mux_sclk_uart_src_p, 0,
			RV1126B_CLKSEL_CON(15), 5, 3, MFLAGS, 0, 5, DFLAGS,
			RV1126B_CLKGATE_CON(2), 0, GFLAGS),
	COMPOSITE(SCLK_UART7, "sclk_uart7", mux_sclk_uart_src_p, 0,
			RV1126B_CLKSEL_CON(15), 13, 3, MFLAGS, 8, 5, DFLAGS,
			RV1126B_CLKGATE_CON(2), 1, GFLAGS),
	COMPOSITE(MCLK_SAI0, "mclk_sai0", mclk_sai0_src_p, 0,
			RV1126B_CLKSEL_CON(16), 8, 4, MFLAGS, 0, 8, DFLAGS,
			RV1126B_CLKGATE_CON(2), 2, GFLAGS),
	COMPOSITE(MCLK_SAI1, "mclk_sai1", mclk_sai1_src_p, 0,
			RV1126B_CLKSEL_CON(17), 8, 4, MFLAGS, 0, 8, DFLAGS,
			RV1126B_CLKGATE_CON(2), 3, GFLAGS),
	COMPOSITE(MCLK_SAI2, "mclk_sai2", mclk_sai2_src_p, 0,
			RV1126B_CLKSEL_CON(18), 8, 4, MFLAGS, 0, 8, DFLAGS,
			RV1126B_CLKGATE_CON(2), 4, GFLAGS),
	COMPOSITE(MCLK_PDM, "mclk_pdm", mux_sai_src_p, 0,
			RV1126B_CLKSEL_CON(19), 6, 4, MFLAGS, 0, 5, DFLAGS,
			RV1126B_CLKGATE_CON(2), 5, GFLAGS),
	COMPOSITE_NOGATE(0, "clkout_pdm_src", mux_sai_src_p, 0,
			RV1126B_CLKSEL_CON(20), 8, 4, MFLAGS, 0, 8, DFLAGS),
	GATE(CLKOUT_PDM, "clkout_pdm", "clkout_pdm_src", 0,
			RV1126B_CLKGATE_CON(2), 6, GFLAGS),
	COMPOSITE_NODIV(MCLK_ASRC0, "mclk_asrc0", mux_sai_src_p, 0,
			RV1126B_CLKSEL_CON(16), 12, 4, MFLAGS,
			RV1126B_CLKGATE_CON(2), 7, GFLAGS),
	COMPOSITE_NODIV(MCLK_ASRC1, "mclk_asrc1", mux_sai_src_p, 0,
			RV1126B_CLKSEL_CON(17), 12, 4, MFLAGS,
			RV1126B_CLKGATE_CON(2), 8, GFLAGS),
	COMPOSITE_NODIV(MCLK_ASRC2, "mclk_asrc2", mux_sai_src_p, 0,
			RV1126B_CLKSEL_CON(18), 12, 4, MFLAGS,
			RV1126B_CLKGATE_CON(2), 9, GFLAGS),
	COMPOSITE_NODIV(MCLK_ASRC3, "mclk_asrc3", mux_sai_src_p, 0,
			RV1126B_CLKSEL_CON(19), 12, 4, MFLAGS,
			RV1126B_CLKGATE_CON(2), 10, GFLAGS),
	COMPOSITE(CLK_ASRC0, "clk_asrc0", mux_gpll_aupll_p, 0,
			RV1126B_CLKSEL_CON(21), 6, 1, MFLAGS, 0, 5, DFLAGS,
			RV1126B_CLKGATE_CON(2), 11, GFLAGS),
	COMPOSITE(CLK_ASRC1, "clk_asrc1", mux_gpll_aupll_p, 0,
			RV1126B_CLKSEL_CON(21), 14, 1, MFLAGS, 8, 5, DFLAGS,
			RV1126B_CLKGATE_CON(2), 12, GFLAGS),
	COMPOSITE_NOMUX(CLK_CORE_PLL, "clk_core_pll", "gpll", CLK_IS_CRITICAL,
			RV1126B_CLKSEL_CON(60), 0, 3, DFLAGS,
			RV1126B_CLKGATE_CON(5), 0, GFLAGS),
	COMPOSITE_NOMUX(CLK_NPU_PLL, "clk_npu_pll", "gpll", CLK_IS_CRITICAL,
			RV1126B_CLKSEL_CON(60), 6, 3, DFLAGS,
			RV1126B_CLKGATE_CON(5), 1, GFLAGS),
	COMPOSITE(CLK_VEPU_PLL, "clk_vepu_pll", mux_gpll_aupll_cpll_p, 0,
			RV1126B_CLKSEL_CON(61), 4, 2, MFLAGS, 0, 3, DFLAGS,
			RV1126B_CLKGATE_CON(5), 2, GFLAGS),
	COMPOSITE(CLK_ISP_PLL, "clk_isp_pll", mux_gpll_aupll_cpll_p, 0,
			RV1126B_CLKSEL_CON(61), 10, 2, MFLAGS, 6, 4, DFLAGS,
			RV1126B_CLKGATE_CON(5), 3, GFLAGS),
	COMPOSITE(CLK_SARADC0_SRC, "clk_saradc0_src", mux_200m_24m_p, 0,
			RV1126B_CLKSEL_CON(63), 12, 1, MFLAGS, 0, 3, DFLAGS,
			RV1126B_CLKGATE_CON(5), 6, GFLAGS),
	COMPOSITE(CLK_SARADC1_SRC, "clk_saradc1_src", mux_200m_24m_p, 0,
			RV1126B_CLKSEL_CON(63), 13, 1, MFLAGS, 4, 3, DFLAGS,
			RV1126B_CLKGATE_CON(5), 7, GFLAGS),
	COMPOSITE(CLK_SARADC2_SRC, "clk_saradc2_src", mux_200m_24m_p, 0,
			RV1126B_CLKSEL_CON(63), 14, 1, MFLAGS, 8, 3, DFLAGS,
			RV1126B_CLKGATE_CON(5), 8, GFLAGS),
	GATE(HCLK_RKNN, "hclk_rknn", "clk_gpll_div8", CLK_IS_CRITICAL,
			RV1126B_CLKGATE_CON(5), 10, GFLAGS),
	GATE(PCLK_NPU_ROOT, "pclk_npu_root", "clk_cpll_div10", CLK_IS_CRITICAL,
			RV1126B_CLKGATE_CON(5), 11, GFLAGS),
	COMPOSITE_NODIV(ACLK_VEPU_ROOT, "aclk_vepu_root", mux_500m_400m_200m_p, CLK_IS_CRITICAL,
			RV1126B_CLKSEL_CON(40), 0, 2, MFLAGS,
			RV1126B_CLKGATE_CON(5), 12, GFLAGS),
	GATE(HCLK_VEPU_ROOT, "hclk_vepu_root", "clk_gpll_div8", CLK_IS_CRITICAL,
			RV1126B_CLKGATE_CON(5), 13, GFLAGS),
	GATE(PCLK_VEPU_ROOT, "pclk_vepu_root", "clk_cpll_div10", 0,
			RV1126B_CLKGATE_CON(5), 14, GFLAGS),
	COMPOSITE(CLK_CORE_RGA_SRC, "clk_core_rga_src", mux_gpll_cpll_p, 0,
			RV1126B_CLKSEL_CON(40), 5, 1, MFLAGS, 2, 3, DFLAGS,
			RV1126B_CLKGATE_CON(6), 0, GFLAGS),
	COMPOSITE_NODIV(ACLK_GMAC_ROOT, "aclk_gmac_root", mux_300m_200m_p, 0,
			RV1126B_CLKSEL_CON(40), 6, 1, MFLAGS,
			RV1126B_CLKGATE_CON(6), 1, GFLAGS),
	COMPOSITE_NODIV(ACLK_VI_ROOT, "aclk_vi_root", mux_500m_400m_300m_p, CLK_IS_CRITICAL,
			RV1126B_CLKSEL_CON(40), 7, 2, MFLAGS,
			RV1126B_CLKGATE_CON(6), 2, GFLAGS),
	GATE(HCLK_VI_ROOT, "hclk_vi_root", "clk_gpll_div8", CLK_IS_CRITICAL,
			RV1126B_CLKGATE_CON(6), 3, GFLAGS),
	GATE(PCLK_VI_ROOT, "pclk_vi_root", "clk_cpll_div10", CLK_IS_CRITICAL,
			RV1126B_CLKGATE_CON(6), 4, GFLAGS),
	COMPOSITE_NODIV(DCLK_VICAP_ROOT, "dclk_vicap_root", mux_333m_200m_p, 0,
			RV1126B_CLKSEL_CON(42), 5, 1, MFLAGS,
			RV1126B_CLKGATE_CON(6), 5, GFLAGS),
	COMPOSITE(CLK_SYS_DSMC_ROOT, "clk_sys_dsmc_root", mux_24m_gpll_cpll_p, 0,
			RV1126B_CLKSEL_CON(40), 14, 2, MFLAGS, 9, 5, DFLAGS,
			RV1126B_CLKGATE_CON(6), 6, GFLAGS),
	COMPOSITE(ACLK_VDO_ROOT, "aclk_vdo_root", mux_gpll_cpll_p, CLK_IS_CRITICAL,
			RV1126B_CLKSEL_CON(42), 4, 1, MFLAGS, 0, 4, DFLAGS,
			RV1126B_CLKGATE_CON(6), 7, GFLAGS),
	COMPOSITE(ACLK_RKVDEC_ROOT, "aclk_rkvdec_root", mux_gpll_cpll_p, 0,
			RV1126B_CLKSEL_CON(42), 10, 1, MFLAGS, 6, 4, DFLAGS,
			RV1126B_CLKGATE_CON(6), 8, GFLAGS),
	GATE(HCLK_VDO_ROOT, "hclk_vdo_root", "clk_gpll_div8", CLK_IS_CRITICAL,
			RV1126B_CLKGATE_CON(6), 9, GFLAGS),
	GATE(PCLK_VDO_ROOT, "pclk_vdo_root", "clk_cpll_div10", CLK_IS_CRITICAL,
			RV1126B_CLKGATE_CON(6), 10, GFLAGS),
	COMPOSITE(DCLK_VOP, "dclk_vop", mux_gpll_cpll_p, 0,
			RV1126B_CLKSEL_CON(43), 8, 1, MFLAGS, 0, 8, DFLAGS,
			RV1126B_CLKGATE_CON(6), 12, GFLAGS),
	COMPOSITE(DCLK_OOC_SRC, "dclk_ooc_src", mux_gpll_cpll_p, 0,
			RV1126B_CLKSEL_CON(62), 7, 1, MFLAGS, 8, 8, DFLAGS,
			RV1126B_CLKGATE_CON(6), 13, GFLAGS),
	GATE(DCLK_DECOM_SRC, "dclk_decom_src", "clk_gpll_div3", 0,
			RV1126B_CLKGATE_CON(6), 14, GFLAGS),
	GATE(PCLK_DDR_ROOT, "pclk_ddr_root", "clk_cpll_div10", 0,
			RV1126B_CLKGATE_CON(7), 0, GFLAGS),
	COMPOSITE(ACLK_SYSMEM, "aclk_sysmem", mux_gpll_cpll_p, CLK_IS_CRITICAL,
			RV1126B_CLKSEL_CON(44), 3, 1, MFLAGS, 0, 3, DFLAGS,
			RV1126B_CLKGATE_CON(7), 1, GFLAGS),
	COMPOSITE_NODIV(ACLK_TOP_ROOT, "aclk_top_root", mux_600m_400m_200m_p, CLK_IS_CRITICAL,
			RV1126B_CLKSEL_CON(44), 6, 2, MFLAGS,
			RV1126B_CLKGATE_CON(7), 3, GFLAGS),
	COMPOSITE_NODIV(ACLK_BUS_ROOT, "aclk_bus_root", mux_400m_300m_200m_p, CLK_IS_CRITICAL,
			RV1126B_CLKSEL_CON(44), 8, 2, MFLAGS,
			RV1126B_CLKGATE_CON(7), 4, GFLAGS),
	COMPOSITE_NODIV(HCLK_BUS_ROOT, "hclk_bus_root", mux_200m_100m_p, CLK_IS_CRITICAL,
			RV1126B_CLKSEL_CON(44), 10, 1, MFLAGS,
			RV1126B_CLKGATE_CON(7), 5, GFLAGS),
	GATE(PCLK_BUS_ROOT, "pclk_bus_root", "clk_cpll_div10", CLK_IS_CRITICAL,
			RV1126B_CLKGATE_CON(7), 6, GFLAGS),
	COMPOSITE(CCLK_SDMMC0, "cclk_sdmmc0", mux_gpll_cpll_24m_p, 0,
			RV1126B_CLKSEL_CON(45), 8, 2, MFLAGS, 0, 8, DFLAGS,
			RV1126B_CLKGATE_CON(7), 7, GFLAGS),
	COMPOSITE(CCLK_SDMMC1, "cclk_sdmmc1", mux_gpll_cpll_24m_p, 0,
			RV1126B_CLKSEL_CON(46), 8, 2, MFLAGS, 0, 8, DFLAGS,
			RV1126B_CLKGATE_CON(7), 8, GFLAGS),
	COMPOSITE(CCLK_EMMC, "cclk_emmc", mux_gpll_cpll_24m_p, 0,
			RV1126B_CLKSEL_CON(47), 8, 2, MFLAGS, 0, 8, DFLAGS,
			RV1126B_CLKGATE_CON(7), 9, GFLAGS),
	COMPOSITE(SCLK_2X_FSPI0, "sclk_2x_fspi0", mux_gpll_cpll_24m_p, 0,
			RV1126B_CLKSEL_CON(48), 8, 2, MFLAGS, 0, 8, DFLAGS,
			RV1126B_CLKGATE_CON(7), 10, GFLAGS),
	COMPOSITE(CLK_GMAC_PTP_REF_SRC, "clk_gmac_ptp_ref_src", mux_cpll_24m_p, 0,
			RV1126B_CLKSEL_CON(45), 10, 1, MFLAGS, 11, 5, DFLAGS,
			RV1126B_CLKGATE_CON(7), 11, GFLAGS),
	GATE(CLK_GMAC_125M, "clk_gmac_125m", "clk_cpll_div8", 0,
			RV1126B_CLKGATE_CON(7), 12, GFLAGS),
	COMPOSITE_NODIV(CLK_TIMER_ROOT, "clk_timer_root", mux_100m_24m_p, 0,
			RV1126B_CLKSEL_CON(46), 11, 1, MFLAGS,
			RV1126B_CLKGATE_CON(7), 13, GFLAGS),
	COMPOSITE_NODIV(TCLK_WDT_NS_SRC, "tclk_wdt_ns_src", mux_100m_24m_p, 0,
			RV1126B_CLKSEL_CON(46), 12, 1, MFLAGS,
			RV1126B_CLKGATE_CON(8), 0, GFLAGS),
	COMPOSITE_NODIV(TCLK_WDT_S_SRC, "tclk_wdt_s_src", mux_100m_24m_p, 0,
			RV1126B_CLKSEL_CON(46), 13, 1, MFLAGS,
			RV1126B_CLKGATE_CON(8), 1, GFLAGS),
	COMPOSITE_NODIV(TCLK_WDT_HPMCU, "tclk_wdt_hpmcu", mux_100m_24m_p, 0,
			RV1126B_CLKSEL_CON(46), 14, 1, MFLAGS,
			RV1126B_CLKGATE_CON(8), 2, GFLAGS),
	COMPOSITE(CLK_CAN0, "clk_can0", mux_gpll_cpll_24m_p, 0,
			RV1126B_CLKSEL_CON(49), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RV1126B_CLKGATE_CON(8), 4, GFLAGS),
	COMPOSITE(CLK_CAN1, "clk_can1", mux_gpll_cpll_24m_p, 0,
			RV1126B_CLKSEL_CON(49), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RV1126B_CLKGATE_CON(8), 5, GFLAGS),
	COMPOSITE_NODIV(PCLK_PERI_ROOT, "pclk_peri_root", mux_100m_24m_p, CLK_IS_CRITICAL,
			RV1126B_CLKSEL_CON(47), 12, 1, MFLAGS,
			RV1126B_CLKGATE_CON(8), 6, GFLAGS),
	COMPOSITE_NODIV(ACLK_PERI_ROOT, "aclk_peri_root", mux_200m_24m_p, CLK_IS_CRITICAL,
			RV1126B_CLKSEL_CON(47), 13, 1, MFLAGS,
			RV1126B_CLKGATE_CON(8), 7, GFLAGS),
	COMPOSITE_NODIV(CLK_I2C_BUS_SRC, "clk_i2c_bus_src", mux_200m_24m_p, 0,
			RV1126B_CLKSEL_CON(50), 1, 1, MFLAGS,
			RV1126B_CLKGATE_CON(8), 9, GFLAGS),
	COMPOSITE_NODIV(CLK_SPI0, "clk_spi0", mux_200m_100m_50m_24m_p, 0,
			RV1126B_CLKSEL_CON(50), 2, 2, MFLAGS,
			RV1126B_CLKGATE_CON(8), 10, GFLAGS),
	COMPOSITE_NODIV(CLK_SPI1, "clk_spi1", mux_200m_100m_50m_24m_p, 0,
			RV1126B_CLKSEL_CON(50), 4, 2, MFLAGS,
			RV1126B_CLKGATE_CON(8), 11, GFLAGS),
	GATE(BUSCLK_PMU_SRC, "busclk_pmu_src", "clk_cpll_div10", CLK_IS_CRITICAL,
			RV1126B_CLKGATE_CON(8), 12, GFLAGS),
	COMPOSITE_NODIV(CLK_PWM0, "clk_pwm0", mux_100m_24m_p, 0,
			RV1126B_CLKSEL_CON(50), 8, 1, MFLAGS,
			RV1126B_CLKGATE_CON(9), 0, GFLAGS),
	COMPOSITE_NODIV(CLK_PWM2, "clk_pwm2", mux_100m_24m_p, 0,
			RV1126B_CLKSEL_CON(50), 10, 1, MFLAGS,
			RV1126B_CLKGATE_CON(9), 2, GFLAGS),
	COMPOSITE_NODIV(CLK_PWM3, "clk_pwm3", mux_100m_24m_p, 0,
			RV1126B_CLKSEL_CON(50), 11, 1, MFLAGS,
			RV1126B_CLKGATE_CON(9), 3, GFLAGS),
	COMPOSITE_NODIV(CLK_PKA_RKCE_SRC, "clk_pka_rkce_src", mux_300m_200m_p, CLK_IS_CRITICAL,
			RV1126B_CLKSEL_CON(50), 12, 1, MFLAGS,
			RV1126B_CLKGATE_CON(9), 4, GFLAGS),
	COMPOSITE_NODIV(ACLK_RKCE_SRC, "aclk_rkce_src", mux_200m_24m_p, CLK_IS_CRITICAL,
			RV1126B_CLKSEL_CON(50), 13, 1, MFLAGS,
			RV1126B_CLKGATE_CON(9), 5, GFLAGS),
	COMPOSITE_NODIV(ACLK_VCP_ROOT, "aclk_vcp_root", mux_500m_400m_200m_p, CLK_IS_CRITICAL,
			RV1126B_CLKSEL_CON(48), 12, 2, MFLAGS,
			RV1126B_CLKGATE_CON(9), 6, GFLAGS),
	GATE(HCLK_VCP_ROOT, "hclk_vcp_root", "clk_gpll_div8", CLK_IS_CRITICAL,
			RV1126B_CLKGATE_CON(9), 7, GFLAGS),
	GATE(PCLK_VCP_ROOT, "pclk_vcp_root", "clk_cpll_div10", CLK_IS_CRITICAL,
			RV1126B_CLKGATE_CON(9), 8, GFLAGS),
	COMPOSITE(CLK_CORE_FEC_SRC, "clk_core_fec_src", mux_gpll_cpll_p, 0,
			RV1126B_CLKSEL_CON(51), 3, 1, MFLAGS, 0, 3, DFLAGS,
			RV1126B_CLKGATE_CON(9), 9, GFLAGS),
	GATE(CLK_50M_GMAC_IOBUF_VI, "clk_50m_gmac_iobuf_vi", "clk_cpll_div20", 0,
			RV1126B_CLKGATE_CON(9), 11, GFLAGS),
	GATE(PCLK_TOP_ROOT, "pclk_top_root", "clk_cpll_div10", CLK_IS_CRITICAL,
			RV1126B_CLKGATE_CON(15), 0, GFLAGS),
	COMPOSITE(CLK_MIPI0_OUT2IO, "clk_mipi0_out2io", mux_600m_24m_p, 0,
			RV1126B_CLKSEL_CON(67), 11, 1, MFLAGS, 0, 5, DFLAGS,
			RV1126B_CLKGATE_CON(15), 3, GFLAGS),
	COMPOSITE(CLK_MIPI1_OUT2IO, "clk_mipi1_out2io", mux_600m_24m_p, 0,
			RV1126B_CLKSEL_CON(67), 12, 1, MFLAGS, 6, 5, DFLAGS,
			RV1126B_CLKGATE_CON(15), 4, GFLAGS),
	COMPOSITE(CLK_MIPI2_OUT2IO, "clk_mipi2_out2io", mux_600m_24m_p, 0,
			RV1126B_CLKSEL_CON(68), 11, 1, MFLAGS, 0, 5, DFLAGS,
			RV1126B_CLKGATE_CON(15), 5, GFLAGS),
	COMPOSITE(CLK_MIPI3_OUT2IO, "clk_mipi3_out2io", mux_600m_24m_p, 0,
			RV1126B_CLKSEL_CON(68), 12, 1, MFLAGS, 6, 5, DFLAGS,
			RV1126B_CLKGATE_CON(15), 6, GFLAGS),
	COMPOSITE(CLK_CIF_OUT2IO, "clk_cif_out2io", mux_600m_24m_p, 0,
			RV1126B_CLKSEL_CON(69), 5, 1, MFLAGS, 0, 5, DFLAGS,
			RV1126B_CLKGATE_CON(15), 7, GFLAGS),
	COMPOSITE(CLK_MAC_OUT2IO, "clk_mac_out2io", mux_gpll_cpll_24m_p, 0,
			RV1126B_CLKSEL_CON(69), 6, 2, MFLAGS, 8, 7, DFLAGS,
			RV1126B_CLKGATE_CON(15), 8, GFLAGS),
	COMPOSITE_NOMUX(MCLK_SAI0_OUT2IO, "mclk_sai0_out2io", "mclk_sai0", CLK_SET_RATE_PARENT,
			RV1126B_CLKSEL_CON(70), 0, 4, DFLAGS,
			RV1126B_CLKGATE_CON(15), 9, GFLAGS),
	COMPOSITE_NOMUX(MCLK_SAI1_OUT2IO, "mclk_sai1_out2io", "mclk_sai1", CLK_SET_RATE_PARENT,
			RV1126B_CLKSEL_CON(70), 5, 4, DFLAGS,
			RV1126B_CLKGATE_CON(15), 10, GFLAGS),
	COMPOSITE_NOMUX(MCLK_SAI2_OUT2IO, "mclk_sai2_out2io", "mclk_sai2", CLK_SET_RATE_PARENT,
			RV1126B_CLKSEL_CON(70), 10, 4, DFLAGS,
			RV1126B_CLKGATE_CON(15), 11, GFLAGS),

	/* pd _npu */
	MUX(ACLK_RKNN, "aclk_rknn", aclk_npu_root_p, CLK_SET_RATE_PARENT,
			RV1126B_NPUCLKSEL_CON(0), 1, 1, MFLAGS),

	/* pd_vepu */
	GATE(PCLK_GPIO3, "pclk_gpio3", "pclk_vepu_root", 0,
			RV1126B_VEPUCLKGATE_CON(0), 7, GFLAGS),
	GATE(DBCLK_GPIO3, "dbclk_gpio3", "xin24m", 0,
			RV1126B_VEPUCLKGATE_CON(0), 8, GFLAGS),
	GATE(PCLK_IOC_VCCIO3, "pclk_ioc_vccio3", "pclk_vepu_root", CLK_IS_CRITICAL,
			RV1126B_VEPUCLKGATE_CON(0), 9, GFLAGS),
	GATE(PCLK_SARADC0, "pclk_saradc0", "pclk_vepu_root", 0,
			RV1126B_VEPUCLKGATE_CON(0), 10, GFLAGS),
	MUX(CLK_SARADC0, "clk_saradc0", clk_saradc0_p, CLK_SET_RATE_PARENT,
			RV1126B_VEPUCLKSEL_CON(0), 2, 1, MFLAGS),
	GATE(HCLK_SDMMC1, "hclk_sdmmc1", "hclk_vepu_root", 0,
			RV1126B_VEPUCLKGATE_CON(0), 12, GFLAGS),
	GATE(HCLK_VEPU, "hclk_vepu", "hclk_vepu_root", 0,
			RV1126B_VEPUCLKGATE_CON(1), 1, GFLAGS),
	GATE(ACLK_VEPU, "aclk_vepu", "aclk_vepu_root", 0,
			RV1126B_VEPUCLKGATE_CON(1), 2, GFLAGS),
	COMPOSITE_NODIV(CLK_CORE_VEPU, "clk_core_vepu", clk_core_vepu_p, CLK_SET_RATE_PARENT,
			RV1126B_VEPUCLKSEL_CON(0), 1, 1, MFLAGS,
			RV1126B_VEPUCLKGATE_CON(1), 3, GFLAGS),

	/* pd_vcp */
	GATE(HCLK_FEC, "hclk_fec", "hclk_vcp_root", 0,
			RV1126B_VCPCLKGATE_CON(1), 0, GFLAGS),
	GATE(ACLK_FEC, "aclk_fec", "aclk_vcp_root", 0,
			RV1126B_VCPCLKGATE_CON(1), 1, GFLAGS),
	COMPOSITE_NODIV(CLK_CORE_FEC, "clk_core_fec", clk_core_fec_p, CLK_SET_RATE_PARENT,
			RV1126B_VCPCLKSEL_CON(0), 13, 1, MFLAGS,
			RV1126B_VCPCLKGATE_CON(1), 2, GFLAGS),
	GATE(HCLK_AVSP, "hclk_avsp", "hclk_vcp_root", 0,
			RV1126B_VCPCLKGATE_CON(1), 3, GFLAGS),
	GATE(ACLK_AVSP, "aclk_avsp", "aclk_vcp_root", 0,
			RV1126B_VCPCLKGATE_CON(1), 4, GFLAGS),
	GATE(HCLK_AISP, "hclk_aisp", "hclk_vcp_root", 0,
			RV1126B_VCPCLKGATE_CON(0), 11, GFLAGS),
	GATE(ACLK_AISP, "aclk_aisp", "aclk_vcp_root", 0,
			RV1126B_VCPCLKGATE_CON(0), 12, GFLAGS),
	COMPOSITE_NODIV(CLK_CORE_AISP, "clk_core_aisp", clk_core_aisp_p, CLK_SET_RATE_PARENT,
			RV1126B_VCPCLKSEL_CON(0), 15, 1, MFLAGS,
			RV1126B_VCPCLKGATE_CON(0), 13, GFLAGS),

	/* pd_vi */
	MUX(CLK_CORE_ISP_ROOT, "clk_core_isp_root", clk_core_isp_root_p, CLK_SET_RATE_PARENT,
			RV1126B_VICLKSEL_CON(0), 1, 1, MFLAGS),
	GATE(PCLK_DSMC, "pclk_dsmc", "pclk_vi_root", 0,
			RV1126B_VICLKGATE_CON(0), 8, GFLAGS),
	GATE(ACLK_DSMC, "aclk_dsmc", "hclk_vi_root", 0,
			RV1126B_VICLKGATE_CON(0), 9, GFLAGS),
	GATE(HCLK_CAN0, "hclk_can0", "hclk_vi_root", 0,
			RV1126B_VICLKGATE_CON(0), 10, GFLAGS),
	GATE(HCLK_CAN1, "hclk_can1", "hclk_vi_root", 0,
			RV1126B_VICLKGATE_CON(0), 11, GFLAGS),
	GATE(PCLK_GPIO2, "pclk_gpio2", "pclk_vi_root", 0,
			RV1126B_VICLKGATE_CON(1), 0, GFLAGS),
	GATE(DBCLK_GPIO2, "dbclk_gpio2", "xin24m", 0,
			RV1126B_VICLKGATE_CON(1), 1, GFLAGS),
	GATE(PCLK_GPIO4, "pclk_gpio4", "pclk_vi_root", 0,
			RV1126B_VICLKGATE_CON(1), 2, GFLAGS),
	GATE(DBCLK_GPIO4, "dbclk_gpio4", "xin24m", 0,
			RV1126B_VICLKGATE_CON(1), 3, GFLAGS),
	GATE(PCLK_GPIO5, "pclk_gpio5", "pclk_vi_root", 0,
			RV1126B_VICLKGATE_CON(1), 4, GFLAGS),
	GATE(DBCLK_GPIO5, "dbclk_gpio5", "xin24m", 0,
			RV1126B_VICLKGATE_CON(1), 5, GFLAGS),
	GATE(PCLK_GPIO6, "pclk_gpio6", "pclk_vi_root", 0,
			RV1126B_VICLKGATE_CON(1), 6, GFLAGS),
	GATE(DBCLK_GPIO6, "dbclk_gpio6", "xin24m", 0,
			RV1126B_VICLKGATE_CON(1), 7, GFLAGS),
	GATE(PCLK_GPIO7, "pclk_gpio7", "pclk_vi_root", 0,
			RV1126B_VICLKGATE_CON(1), 8, GFLAGS),
	GATE(DBCLK_GPIO7, "dbclk_gpio7", "xin24m", 0,
			RV1126B_VICLKGATE_CON(1), 9, GFLAGS),
	GATE(PCLK_IOC_VCCIO2, "pclk_ioc_vccio2", "pclk_vi_root", CLK_IS_CRITICAL,
			RV1126B_VICLKGATE_CON(1), 10, GFLAGS),
	GATE(PCLK_IOC_VCCIO4, "pclk_ioc_vccio4", "pclk_vi_root", CLK_IS_CRITICAL,
			RV1126B_VICLKGATE_CON(1), 11, GFLAGS),
	GATE(PCLK_IOC_VCCIO5, "pclk_ioc_vccio5", "pclk_vi_root", CLK_IS_CRITICAL,
			RV1126B_VICLKGATE_CON(1), 12, GFLAGS),
	GATE(PCLK_IOC_VCCIO6, "pclk_ioc_vccio6", "pclk_vi_root", CLK_IS_CRITICAL,
			RV1126B_VICLKGATE_CON(1), 13, GFLAGS),
	GATE(PCLK_IOC_VCCIO7, "pclk_ioc_vccio7", "pclk_vi_root", CLK_IS_CRITICAL,
			RV1126B_VICLKGATE_CON(1), 14, GFLAGS),
	GATE(HCLK_ISP, "hclk_isp", "hclk_vi_root", 0,
			RV1126B_VICLKGATE_CON(2), 0, GFLAGS),
	GATE(ACLK_ISP, "aclk_isp", "aclk_vi_root", 0,
			RV1126B_VICLKGATE_CON(2), 1, GFLAGS),
	GATE(CLK_CORE_ISP, "clk_core_isp", "clk_core_isp_root", 0,
			RV1126B_VICLKGATE_CON(2), 2, GFLAGS),
	GATE(HCLK_VICAP, "hclk_vicap", "hclk_vi_root", 0,
			RV1126B_VICLKGATE_CON(2), 3, GFLAGS),
	GATE(ACLK_VICAP, "aclk_vicap", "aclk_vi_root", 0,
			RV1126B_VICLKGATE_CON(2), 4, GFLAGS),
	GATE(DCLK_VICAP, "dclk_vicap", "dclk_vicap_root", 0,
			RV1126B_VICLKGATE_CON(2), 5, GFLAGS),
	GATE(ISP0CLK_VICAP, "isp0clk_vicap", "clk_core_isp_root", 0,
			RV1126B_VICLKGATE_CON(2), 6, GFLAGS),
	GATE(HCLK_VPSS, "hclk_vpss", "hclk_vi_root", 0,
			RV1126B_VICLKGATE_CON(2), 7, GFLAGS),
	GATE(ACLK_VPSS, "aclk_vpss", "aclk_vi_root", 0,
			RV1126B_VICLKGATE_CON(2), 8, GFLAGS),
	GATE(CLK_CORE_VPSS, "clk_core_vpss", "clk_core_isp_root", 0,
			RV1126B_VICLKGATE_CON(2), 9, GFLAGS),
	GATE(HCLK_VPSL, "hclk_vpsl", "hclk_vi_root", 0,
			RV1126B_VICLKGATE_CON(2), 10, GFLAGS),
	GATE(ACLK_VPSL, "aclk_vpsl", "aclk_vi_root", 0,
			RV1126B_VICLKGATE_CON(2), 11, GFLAGS),
	GATE(CLK_CORE_VPSL, "clk_core_vpsl", "clk_core_isp_root", 0,
			RV1126B_VICLKGATE_CON(2), 12, GFLAGS),
	GATE(PCLK_CSI2HOST0, "pclk_csi2host0", "pclk_vi_root", 0,
			RV1126B_VICLKGATE_CON(3), 0, GFLAGS),
	GATE(DCLK_CSI2HOST0, "dclk_csi2host0", "dclk_vicap_root", 0,
			RV1126B_VICLKGATE_CON(3), 1, GFLAGS),
	GATE(PCLK_CSI2HOST1, "pclk_csi2host1", "pclk_vi_root", 0,
			RV1126B_VICLKGATE_CON(3), 2, GFLAGS),
	GATE(DCLK_CSI2HOST1, "dclk_csi2host1", "dclk_vicap_root", 0,
			RV1126B_VICLKGATE_CON(3), 3, GFLAGS),
	GATE(PCLK_CSI2HOST2, "pclk_csi2host2", "pclk_vi_root", 0,
			RV1126B_VICLKGATE_CON(3), 4, GFLAGS),
	GATE(DCLK_CSI2HOST2, "dclk_csi2host2", "dclk_vicap_root", 0,
			RV1126B_VICLKGATE_CON(3), 5, GFLAGS),
	GATE(PCLK_CSI2HOST3, "pclk_csi2host3", "pclk_vi_root", 0,
			RV1126B_VICLKGATE_CON(3), 6, GFLAGS),
	GATE(DCLK_CSI2HOST3, "dclk_csi2host3", "dclk_vicap_root", 0,
			RV1126B_VICLKGATE_CON(3), 7, GFLAGS),
	GATE(HCLK_SDMMC0, "hclk_sdmmc0", "hclk_vi_root", 0,
			RV1126B_VICLKGATE_CON(3), 8, GFLAGS),
	GATE(ACLK_GMAC, "aclk_gmac", "aclk_gmac_root", 0,
			RV1126B_VICLKGATE_CON(3), 9, GFLAGS),
	GATE(PCLK_GMAC, "pclk_gmac", "pclk_vi_root", 0,
			RV1126B_VICLKGATE_CON(3), 10, GFLAGS),
	MUX(CLK_GMAC_PTP_REF, "clk_gmac_ptp_ref", clk_gmac_ptp_ref_p, CLK_SET_RATE_PARENT,
			RV1126B_VICLKSEL_CON(0), 14, 1, MFLAGS),
	GATE(PCLK_CSIPHY0, "pclk_csiphy0", "pclk_vi_root", 0,
			RV1126B_VICLKGATE_CON(3), 11, GFLAGS),
	GATE(PCLK_CSIPHY1, "pclk_csiphy1", "pclk_vi_root", 0,
			RV1126B_VICLKGATE_CON(3), 12, GFLAGS),
	GATE(PCLK_MACPHY, "pclk_macphy", "pclk_vi_root", 0,
			RV1126B_VICLKGATE_CON(3), 13, GFLAGS),
	GATE(PCLK_SARADC1, "pclk_saradc1", "pclk_vi_root", 0,
			RV1126B_VICLKGATE_CON(4), 0, GFLAGS),
	MUX(CLK_SARADC1, "clk_saradc1", clk_saradc1_p, CLK_SET_RATE_PARENT,
			RV1126B_VICLKSEL_CON(0), 2, 1, MFLAGS),
	GATE(PCLK_SARADC2, "pclk_saradc2", "pclk_vi_root", 0,
			RV1126B_VICLKGATE_CON(4), 2, GFLAGS),
	MUX(CLK_SARADC2, "clk_saradc2", clk_saradc2_p, CLK_SET_RATE_PARENT,
			RV1126B_VICLKSEL_CON(0), 3, 1, MFLAGS),
	COMPOSITE_NODIV(CLK_MACPHY, "clk_macphy", clk_macphy_p, 0,
			RV1126B_VICLKSEL_CON(1), 1, 1, MFLAGS,
			RV1126B_VICLKGATE_CON(0), 12, GFLAGS),

	/* pd_vdo */
	GATE(ACLK_RKVDEC, "aclk_rkvdec", "aclk_rkvdec_root", 0,
			RV1126B_VDOCLKGATE_CON(0), 7, GFLAGS),
	GATE(HCLK_RKVDEC, "hclk_rkvdec", "hclk_vdo_root", 0,
			RV1126B_VDOCLKGATE_CON(0), 8, GFLAGS),
	GATE(CLK_HEVC_CA_RKVDEC, "clk_hevc_ca_rkvdec", "aclk_rkvdec_root", 0,
			RV1126B_VDOCLKGATE_CON(0), 9, GFLAGS),
	GATE(ACLK_VOP, "aclk_vop", "aclk_vdo_root", 0,
			RV1126B_VDOCLKGATE_CON(0), 10, GFLAGS),
	GATE(HCLK_VOP, "hclk_vop", "hclk_vdo_root", 0,
			RV1126B_VDOCLKGATE_CON(0), 11, GFLAGS),
	GATE(ACLK_OOC, "aclk_ooc", "aclk_vdo_root", 0,
			RV1126B_VDOCLKGATE_CON(0), 13, GFLAGS),
	GATE(HCLK_OOC, "hclk_ooc", "hclk_vdo_root", 0,
			RV1126B_VDOCLKGATE_CON(0), 14, GFLAGS),
	GATE(HCLK_RKJPEG, "hclk_rkjpeg", "hclk_vdo_root", 0,
			RV1126B_VDOCLKGATE_CON(1), 3, GFLAGS),
	GATE(ACLK_RKJPEG, "aclk_rkjpeg", "aclk_vdo_root", 0,
			RV1126B_VDOCLKGATE_CON(1), 4, GFLAGS),
	GATE(ACLK_RKMMU_DECOM, "aclk_rkmmu_decom", "aclk_vdo_root", 0,
			RV1126B_VDOCLKGATE_CON(1), 5, GFLAGS),
	GATE(HCLK_RKMMU_DECOM, "hclk_rkmmu_decom", "hclk_vdo_root", 0,
			RV1126B_VDOCLKGATE_CON(1), 6, GFLAGS),
	GATE(DCLK_DECOM, "dclk_decom", "dclk_decom_src", 0,
			RV1126B_VDOCLKGATE_CON(1), 8, GFLAGS),
	GATE(ACLK_DECOM, "aclk_decom", "aclk_vdo_root", 0,
			RV1126B_VDOCLKGATE_CON(1), 9, GFLAGS),
	GATE(PCLK_DECOM, "pclk_decom", "pclk_vdo_root", 0,
			RV1126B_VDOCLKGATE_CON(1), 10, GFLAGS),
	GATE(PCLK_MIPI_DSI, "pclk_mipi_dsi", "pclk_vdo_root", 0,
			RV1126B_VDOCLKGATE_CON(1), 12, GFLAGS),
	GATE(PCLK_DSIPHY, "pclk_dsiphy", "pclk_vdo_root", 0,
			RV1126B_VDOCLKGATE_CON(1), 13, GFLAGS),

	/* pd_ddr */
	GATE(PCLK_DDRC, "pclk_ddrc", "pclk_ddr_root", CLK_IS_CRITICAL,
			RV1126B_DDRCLKGATE_CON(0), 2, GFLAGS),
	GATE(PCLK_DDRMON, "pclk_ddrmon", "pclk_ddr_root", CLK_IS_CRITICAL,
			RV1126B_DDRCLKGATE_CON(0), 3, GFLAGS),
	GATE(CLK_TIMER_DDRMON, "clk_timer_ddrmon", "xin24m", 0,
			RV1126B_DDRCLKGATE_CON(0), 4, GFLAGS),
	GATE(PCLK_DFICTRL, "pclk_dfictrl", "pclk_ddr_root", CLK_IS_CRITICAL,
			RV1126B_DDRCLKGATE_CON(0), 5, GFLAGS),
	GATE(PCLK_DDRPHY, "pclk_ddrphy", "pclk_ddr_root", CLK_IS_CRITICAL,
			RV1126B_DDRCLKGATE_CON(0), 8, GFLAGS),
	GATE(PCLK_DMA2DDR, "pclk_dma2ddr", "pclk_ddr_root", CLK_IS_CRITICAL,
			RV1126B_DDRCLKGATE_CON(0), 9, GFLAGS),

	/* pd_pmu*/
	COMPOSITE_NODIV(CLK_RCOSC_SRC, "clk_rcosc_src", clk_rcosc_src_p, 0,
			RV1126B_PMUCLKSEL_CON(1), 0, 3, MFLAGS,
			RV1126B_PMUCLKGATE_CON(0), 0, GFLAGS),
	COMPOSITE_NOGATE(BUSCLK_PMU_MUX, "busclk_pmu_mux", busclk_pmu_mux_p, 0,
			RV1126B_PMUCLKSEL_CON(1), 3, 1, MFLAGS, 4, 2, DFLAGS),
	GATE(BUSCLK_PMU_ROOT, "busclk_pmu_root", "busclk_pmu_mux", 0,
			RV1126B_PMUCLKGATE_CON(0), 1, GFLAGS),
	GATE(BUSCLK_PMU1_ROOT, "busclk_pmu1_root", "busclk_pmu_mux", CLK_IS_CRITICAL,
			RV1126B_PMUCLKGATE_CON(3), 11, GFLAGS),
	GATE(PCLK_PMU, "pclk_pmu", "busclk_pmu_root", CLK_IS_CRITICAL,
			RV1126B_PMUCLKGATE_CON(0), 6, GFLAGS),
	MUX(0, "xin_rc_src", clk_xin_rc_div_p, 0,
			RV1126B_PMUCLKSEL_CON(2), 0, 1, MFLAGS),
	COMPOSITE_FRACMUX_NOGATE(CLK_XIN_RC_DIV, "clk_xin_rc_div", "xin_rc_src", CLK_SET_RATE_PARENT,
			RV1126B_PMUCLKSEL_CON(8), 0,
			&rv1126b_rcdiv_pmu_fracmux),
	GATE(PCLK_PMU_GPIO0, "pclk_pmu_gpio0", "busclk_pmu_root", 0,
			RV1126B_PMUCLKGATE_CON(0), 7, GFLAGS),
	COMPOSITE_NODIV(DBCLK_PMU_GPIO0, "dbclk_pmu_gpio0", mux_24m_32k_p, 0,
			RV1126B_PMUCLKSEL_CON(2), 4, 1, MFLAGS,
			RV1126B_PMUCLKGATE_CON(0), 8, GFLAGS),
	GATE(PCLK_PMU_HP_TIMER, "pclk_pmu_hp_timer", "busclk_pmu_root", CLK_IS_CRITICAL,
			RV1126B_PMUCLKGATE_CON(0), 10, GFLAGS),
	COMPOSITE(CLK_PMU_HP_TIMER, "clk_pmu_hp_timer", mux_cpll_24m_p, CLK_IS_CRITICAL,
			RV1126B_PMUCLKSEL_CON(1), 13, 1, MFLAGS, 8, 5, DFLAGS,
			RV1126B_PMUCLKGATE_CON(0), 11, GFLAGS),
	GATE(CLK_PMU_32K_HP_TIMER, "clk_pmu_32k_hp_timer", "clk_32k", CLK_IS_CRITICAL,
			RV1126B_PMUCLKGATE_CON(0), 13, GFLAGS),
	GATE(PCLK_PWM1, "pclk_pwm1", "busclk_pmu_root", 0,
			RV1126B_PMUCLKGATE_CON(1), 0, GFLAGS),
	COMPOSITE(CLK_PWM1, "clk_pwm1", mux_24m_rcosc_buspmu_p, 0,
			RV1126B_PMUCLKSEL_CON(2), 8, 2, MFLAGS, 6, 2, DFLAGS,
			RV1126B_PMUCLKGATE_CON(1), 1, GFLAGS),
	GATE(CLK_OSC_PWM1, "clk_osc_pwm1", "xin24m", 0,
			RV1126B_PMUCLKGATE_CON(1), 2, GFLAGS),
	GATE(CLK_RC_PWM1, "clk_rc_pwm1", "clk_32k", 0,
			RV1126B_PMUCLKGATE_CON(1), 3, GFLAGS),
	GATE(PCLK_I2C2, "pclk_i2c2", "busclk_pmu_root", 0,
			RV1126B_PMUCLKGATE_CON(1), 6, GFLAGS),
	COMPOSITE(CLK_I2C2, "clk_i2c2", mux_24m_rcosc_buspmu_p, 0,
			RV1126B_PMUCLKSEL_CON(2), 14, 2, MFLAGS, 12, 2, DFLAGS,
			RV1126B_PMUCLKGATE_CON(1), 7, GFLAGS),
	GATE(PCLK_UART0, "pclk_uart0", "busclk_pmu_root", 0,
			RV1126B_PMUCLKGATE_CON(1), 8, GFLAGS),
	COMPOSITE_NODIV(SCLK_UART0, "sclk_uart0", sclk_uart0_p, CLK_SET_RATE_PARENT,
			RV1126B_PMUCLKSEL_CON(3), 0, 2, MFLAGS,
			RV1126B_PMUCLKGATE_CON(1), 11, GFLAGS),
	GATE(PCLK_RCOSC_CTRL, "pclk_rcosc_ctrl", "busclk_pmu_root", CLK_IS_CRITICAL,
			RV1126B_PMUCLKGATE_CON(2), 0, GFLAGS),
	COMPOSITE_NODIV(CLK_OSC_RCOSC_CTRL, "clk_osc_rcosc_ctrl", clk_osc_rcosc_ctrl_p, CLK_IS_CRITICAL,
			RV1126B_PMUCLKSEL_CON(3), 2, 1, MFLAGS,
			RV1126B_PMUCLKGATE_CON(2), 1, GFLAGS),
	GATE(CLK_REF_RCOSC_CTRL, "clk_ref_rcosc_ctrl", "xin24m", CLK_IS_CRITICAL,
			RV1126B_PMUCLKGATE_CON(2), 2, GFLAGS),
	GATE(PCLK_IOC_PMUIO0, "pclk_ioc_pmuio0", "busclk_pmu_root", CLK_IS_CRITICAL,
			RV1126B_PMUCLKGATE_CON(2), 3, GFLAGS),
	GATE(CLK_REFOUT, "clk_refout", "xin24m", 0,
			RV1126B_PMUCLKGATE_CON(2), 6, GFLAGS),
	GATE(CLK_PREROLL, "clk_preroll", "busclk_pmu_root", 0,
			RV1126B_PMUCLKGATE_CON(2), 7, GFLAGS),
	GATE(CLK_PREROLL_32K, "clk_preroll_32k", "clk_32k", 0,
			RV1126B_PMUCLKGATE_CON(2), 8, GFLAGS),
	GATE(HCLK_PMU_SRAM, "hclk_pmu_sram", "busclk_pmu_root", CLK_IS_CRITICAL,
			RV1126B_PMUCLKGATE_CON(2), 9, GFLAGS),
	GATE(PCLK_WDT_LPMCU, "pclk_wdt_lpmcu", "busclk_pmu_root", 0,
			RV1126B_PMUCLKGATE_CON(3), 0, GFLAGS),
	COMPOSITE_NODIV(TCLK_WDT_LPMCU, "tclk_wdt_lpmcu", mux_24m_rcosc_buspmu_32k_p, 0,
			RV1126B_PMUCLKSEL_CON(3), 6, 2, MFLAGS,
			RV1126B_PMUCLKGATE_CON(3), 1, GFLAGS),
	GATE(CLK_LPMCU, "clk_lpmcu", "busclk_pmu_root", 0,
			RV1126B_PMUCLKGATE_CON(3), 2, GFLAGS),
	GATE(CLK_LPMCU_RTC, "clk_lpmcu_rtc", "xin24m", 0,
			RV1126B_PMUCLKGATE_CON(3), 3, GFLAGS),
	GATE(PCLK_LPMCU_MAILBOX, "pclk_lpmcu_mailbox", "busclk_pmu_root", 0,
			RV1126B_PMUCLKGATE_CON(3), 4, GFLAGS),

	/* pd_pmu1 */
	GATE(PCLK_SPI2AHB, "pclk_spi2ahb", "busclk_pmu_root", 0,
			RV1126B_PMU1CLKGATE_CON(0), 0, GFLAGS),
	GATE(HCLK_SPI2AHB, "hclk_spi2ahb", "busclk_pmu_root", 0,
			RV1126B_PMU1CLKGATE_CON(0), 1, GFLAGS),
	GATE(HCLK_FSPI1, "hclk_fspi1", "busclk_pmu_root", 0,
			RV1126B_PMU1CLKGATE_CON(0), 2, GFLAGS),
	GATE(HCLK_XIP_FSPI1, "hclk_xip_fspi1", "busclk_pmu_root", 0,
			RV1126B_PMU1CLKGATE_CON(0), 3, GFLAGS),
	COMPOSITE(SCLK_1X_FSPI1, "sclk_1x_fspi1", mux_24m_rcosc_buspmu_p, 0,
			RV1126B_PMU1CLKSEL_CON(0), 0, 2, MFLAGS, 2, 3, DFLAGS,
			RV1126B_PMU1CLKGATE_CON(0), 4, GFLAGS),
	GATE(PCLK_IOC_PMUIO1, "pclk_ioc_pmuio1", "busclk_pmu_root", CLK_IS_CRITICAL,
			RV1126B_PMU1CLKGATE_CON(0), 5, GFLAGS),
	GATE(PCLK_AUDIO_ADC_PMU, "pclk_audio_adc_pmu", "busclk_pmu_root", 0,
			RV1126B_PMU1CLKGATE_CON(0), 8, GFLAGS),

	COMPOSITE(MCLK_LPSAI, "mclk_lpsai", mux_24m_rcosc_buspmu_p, 0,
			RV1126B_PMU1CLKSEL_CON(0), 6, 2, MFLAGS, 8, 5, DFLAGS,
			RV1126B_PMU1CLKGATE_CON(1), 3, GFLAGS),
	GATE(MCLK_AUDIO_ADC_PMU, "mclk_audio_adc_pmu", "mclk_lpsai", CLK_IS_CRITICAL,
			RV1126B_PMU1CLKGATE_CON(0), 9, GFLAGS),
	FACTOR(MCLK_AUDIO_ADC_DIV4_PMU, "mclk_audio_adc_div4_pmu", "mclk_audio_adc_pmu", 0, 1, 4),

	/* pd_bus */
	GATE(ACLK_GIC400, "aclk_gic400", "hclk_bus_root", CLK_IS_CRITICAL,
			RV1126B_BUSCLKGATE_CON(0), 8, GFLAGS),
	GATE(PCLK_WDT_NS, "pclk_wdt_ns", "pclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(0), 10, GFLAGS),
	GATE(TCLK_WDT_NS, "tclk_wdt_ns", "tclk_wdt_ns_src", 0,
			RV1126B_BUSCLKGATE_CON(0), 11, GFLAGS),
	GATE(PCLK_WDT_HPMCU, "pclk_wdt_hpmcu", "pclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(1), 0, GFLAGS),
	GATE(HCLK_CACHE, "hclk_cache", "aclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(1), 2, GFLAGS),
	GATE(PCLK_HPMCU_MAILBOX, "pclk_hpmcu_mailbox", "pclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(1), 3, GFLAGS),
	GATE(PCLK_HPMCU_INTMUX, "pclk_hpmcu_intmux", "pclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(1), 4, GFLAGS),
	GATE(CLK_HPMCU, "clk_hpmcu", "aclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(1), 5, GFLAGS),
	GATE(CLK_HPMCU_RTC, "clk_hpmcu_rtc", "xin24m", 0,
			RV1126B_BUSCLKGATE_CON(1), 10, GFLAGS),
	GATE(PCLK_RKDMA, "pclk_rkdma", "pclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(1), 11, GFLAGS),
	GATE(ACLK_RKDMA, "aclk_rkdma", "aclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(1), 12, GFLAGS),
	GATE(PCLK_DCF, "pclk_dcf", "pclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(2), 0, GFLAGS),
	GATE(ACLK_DCF, "aclk_dcf", "aclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(2), 1, GFLAGS),
	GATE(HCLK_RGA, "hclk_rga", "hclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(2), 2, GFLAGS),
	GATE(ACLK_RGA, "aclk_rga", "aclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(2), 3, GFLAGS),
	GATE(CLK_CORE_RGA, "clk_core_rga", "clk_core_rga_src", 0,
			RV1126B_BUSCLKGATE_CON(2), 4, GFLAGS),
	GATE(PCLK_TIMER, "pclk_timer", "pclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(2), 5, GFLAGS),
	COMPOSITE_NODIV(CLK_TIMER0, "clk_timer0", clk_timer0_parents_p, 0,
			RV1126B_BUSCLKSEL_CON(2), 0, 2, MFLAGS,
			RV1126B_BUSCLKGATE_CON(2), 6, GFLAGS),
	COMPOSITE_NODIV(CLK_TIMER1, "clk_timer1", clk_timer1_parents_p, 0,
			RV1126B_BUSCLKSEL_CON(2), 2, 2, MFLAGS,
			RV1126B_BUSCLKGATE_CON(2), 7, GFLAGS),
	COMPOSITE_NODIV(CLK_TIMER2, "clk_timer2", clk_timer2_parents_p, 0,
			RV1126B_BUSCLKSEL_CON(2), 4, 2, MFLAGS,
			RV1126B_BUSCLKGATE_CON(2), 8, GFLAGS),
	COMPOSITE_NODIV(CLK_TIMER3, "clk_timer3", clk_timer3_parents_p, 0,
			RV1126B_BUSCLKSEL_CON(2), 6, 2, MFLAGS,
			RV1126B_BUSCLKGATE_CON(2), 9, GFLAGS),
	COMPOSITE_NODIV(CLK_TIMER4, "clk_timer4", clk_timer4_parents_p, 0,
			RV1126B_BUSCLKSEL_CON(2), 8, 2, MFLAGS,
			RV1126B_BUSCLKGATE_CON(2), 10, GFLAGS),
	GATE(HCLK_RKRNG_S_NS, "hclk_rkrng_s_ns", "hclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(2), 14, GFLAGS),
	GATE(HCLK_RKRNG_NS, "hclk_rkrng_ns", "hclk_rkrng_s_ns", 0,
			RV1126B_BUSCLKGATE_CON(2), 15, GFLAGS),
	GATE(CLK_TIMER5, "clk_timer5", "clk_timer_root", CLK_IS_CRITICAL,
			RV1126B_BUSCLKGATE_CON(2), 11, GFLAGS),
	GATE(PCLK_I2C0, "pclk_i2c0", "pclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(3), 0, GFLAGS),
	GATE(CLK_I2C0, "clk_i2c0", "clk_i2c_bus_src", 0,
			RV1126B_BUSCLKGATE_CON(3), 1, GFLAGS),
	GATE(PCLK_I2C1, "pclk_i2c1", "pclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(3), 2, GFLAGS),
	GATE(CLK_I2C1, "clk_i2c1", "clk_i2c_bus_src", 0,
			RV1126B_BUSCLKGATE_CON(3), 3, GFLAGS),
	GATE(PCLK_I2C3, "pclk_i2c3", "pclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(3), 4, GFLAGS),
	GATE(CLK_I2C3, "clk_i2c3", "clk_i2c_bus_src", 0,
			RV1126B_BUSCLKGATE_CON(3), 5, GFLAGS),
	GATE(PCLK_I2C4, "pclk_i2c4", "pclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(3), 6, GFLAGS),
	GATE(CLK_I2C4, "clk_i2c4", "clk_i2c_bus_src", 0,
			RV1126B_BUSCLKGATE_CON(3), 7, GFLAGS),
	GATE(PCLK_I2C5, "pclk_i2c5", "pclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(3), 8, GFLAGS),
	GATE(CLK_I2C5, "clk_i2c5", "clk_i2c_bus_src", 0,
			RV1126B_BUSCLKGATE_CON(3), 9, GFLAGS),
	GATE(PCLK_SPI0, "pclk_spi0", "pclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(3), 10, GFLAGS),
	GATE(PCLK_SPI1, "pclk_spi1", "pclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(3), 12, GFLAGS),
	GATE(PCLK_PWM0, "pclk_pwm0", "pclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(4), 0, GFLAGS),
	GATE(CLK_OSC_PWM0, "clk_osc_pwm0", "xin24m", 0,
			RV1126B_BUSCLKGATE_CON(4), 1, GFLAGS),
	GATE(CLK_RC_PWM0, "clk_rc_pwm0", "xin24m", 0,
			RV1126B_BUSCLKGATE_CON(4), 2, GFLAGS),
	GATE(PCLK_PWM2, "pclk_pwm2", "pclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(4), 3, GFLAGS),
	GATE(CLK_OSC_PWM2, "clk_osc_pwm2", "xin24m", 0,
			RV1126B_BUSCLKGATE_CON(4), 4, GFLAGS),
	GATE(CLK_RC_PWM2, "clk_rc_pwm2", "xin24m", 0,
			RV1126B_BUSCLKGATE_CON(4), 5, GFLAGS),
	GATE(PCLK_PWM3, "pclk_pwm3", "pclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(4), 6, GFLAGS),
	GATE(CLK_OSC_PWM3, "clk_osc_pwm3", "xin24m", 0,
			RV1126B_BUSCLKGATE_CON(4), 7, GFLAGS),
	GATE(CLK_RC_PWM3, "clk_rc_pwm3", "xin24m", 0,
			RV1126B_BUSCLKGATE_CON(4), 8, GFLAGS),
	GATE(PCLK_UART1, "pclk_uart1", "pclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(4), 9, GFLAGS),
	GATE(PCLK_UART2, "pclk_uart2", "pclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(4), 10, GFLAGS),
	GATE(PCLK_UART3, "pclk_uart3", "pclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(4), 11, GFLAGS),
	GATE(PCLK_UART4, "pclk_uart4", "pclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(4), 12, GFLAGS),
	GATE(PCLK_UART5, "pclk_uart5", "pclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(4), 13, GFLAGS),
	GATE(PCLK_UART6, "pclk_uart6", "pclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(4), 14, GFLAGS),
	GATE(PCLK_UART7, "pclk_uart7", "pclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(4), 15, GFLAGS),
	GATE(PCLK_TSADC, "pclk_tsadc", "pclk_bus_root", CLK_IS_CRITICAL,
			RV1126B_BUSCLKGATE_CON(5), 0, GFLAGS),
	GATE(CLK_TSADC, "clk_tsadc", "xin24m", CLK_IS_CRITICAL,
			RV1126B_BUSCLKGATE_CON(5), 1, GFLAGS),
	GATE(HCLK_SAI0, "hclk_sai0", "hclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(5), 2, GFLAGS),
	GATE(HCLK_SAI1, "hclk_sai1", "hclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(5), 4, GFLAGS),
	GATE(HCLK_SAI2, "hclk_sai2", "hclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(5), 6, GFLAGS),
	GATE(HCLK_RKDSM, "hclk_rkdsm", "hclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(5), 8, GFLAGS),
	GATE(MCLK_RKDSM, "mclk_rkdsm", "mclk_sai2", 0,
			RV1126B_BUSCLKGATE_CON(5), 9, GFLAGS),
	GATE(HCLK_PDM, "hclk_pdm", "hclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(5), 10, GFLAGS),
	GATE(HCLK_ASRC0, "hclk_asrc0", "hclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(5), 11, GFLAGS),
	GATE(HCLK_ASRC1, "hclk_asrc1", "hclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(5), 12, GFLAGS),
	GATE(PCLK_AUDIO_ADC_BUS, "pclk_audio_adc_bus", "pclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(5), 13, GFLAGS),
	GATE(MCLK_AUDIO_ADC_BUS, "mclk_audio_adc_bus", "mclk_sai2", 0,
			RV1126B_BUSCLKGATE_CON(5), 14, GFLAGS),
	FACTOR(MCLK_AUDIO_ADC_DIV4_BUS, "mclk_audio_adc_div4_bus", "mclk_audio_adc_bus", 0, 1, 4),
	GATE(PCLK_RKCE, "pclk_rkce", "pclk_bus_root", CLK_IS_CRITICAL,
			RV1126B_BUSCLKGATE_CON(6), 0, GFLAGS),
	GATE(HCLK_NS_RKCE, "hclk_ns_rkce", "hclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(6), 1, GFLAGS),
	GATE(PCLK_OTPC_NS, "pclk_otpc_ns", "pclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(6), 2, GFLAGS),
	GATE(CLK_SBPI_OTPC_NS, "clk_sbpi_otpc_ns", "xin24m", 0,
			RV1126B_BUSCLKGATE_CON(6), 3, GFLAGS),
	COMPOSITE_NOMUX(CLK_USER_OTPC_NS, "clk_user_otpc_ns", "xin24m", 0,
			RV1126B_BUSCLKSEL_CON(2), 12, 3, DFLAGS,
			RV1126B_BUSCLKGATE_CON(6), 4, GFLAGS),
	GATE(PCLK_OTP_MASK, "pclk_otp_mask", "pclk_bus_root", 0,
			RV1126B_BUSCLKGATE_CON(6), 6, GFLAGS),
	GATE(CLK_TSADC_PHYCTRL, "clk_tsadc_phyctrl", "xin24m", CLK_IS_CRITICAL,
			RV1126B_BUSCLKGATE_CON(6), 8, GFLAGS),
	MUX(LRCK_SRC_ASRC0, "lrck_src_asrc0", lrck_src_asrc_p, 0,
			RV1126B_BUSCLKSEL_CON(3), 0, 3, MFLAGS),
	MUX(LRCK_DST_ASRC0, "lrck_dst_asrc0", lrck_src_asrc_p, 0,
			RV1126B_BUSCLKSEL_CON(3), 4, 3, MFLAGS),
	MUX(LRCK_SRC_ASRC1, "lrck_src_asrc1", lrck_src_asrc_p, 0,
			RV1126B_BUSCLKSEL_CON(3), 8, 3, MFLAGS),
	MUX(LRCK_DST_ASRC1, "lrck_dst_asrc1", lrck_src_asrc_p, 0,
			RV1126B_BUSCLKSEL_CON(3), 12, 3, MFLAGS),
	GATE(ACLK_NSRKCE, "aclk_nsrkce", "aclk_rkce_src", 0,
			RV1126B_BUSCLKGATE_CON(2), 12, GFLAGS),
	GATE(CLK_PKA_NSRKCE, "clk_pka_nsrkce", "clk_pka_rkce_src", 0,
			RV1126B_BUSCLKGATE_CON(2), 13, GFLAGS),

	/* pd_peri */
	DIV(PCLK_RTC_ROOT, "pclk_rtc_root", "pclk_peri_root", 0,
			RV1126B_PERICLKSEL_CON(0), 0, 2, DFLAGS),
	GATE(PCLK_GPIO1, "pclk_gpio1", "pclk_peri_root", 0,
			RV1126B_PERICLKGATE_CON(0), 5, GFLAGS),
	GATE(DBCLK_GPIO1, "dbclk_gpio1", "xin24m", 0,
			RV1126B_PERICLKGATE_CON(0), 6, GFLAGS),
	GATE(PCLK_IOC_VCCIO1, "pclk_ioc_vccio1", "pclk_peri_root", CLK_IS_CRITICAL,
			RV1126B_PERICLKGATE_CON(0), 7, GFLAGS),
	GATE(ACLK_USB3OTG, "aclk_usb3otg", "aclk_peri_root", 0,
			RV1126B_PERICLKGATE_CON(0), 8, GFLAGS),
	GATE(CLK_REF_USB3OTG, "clk_ref_usb3otg", "xin24m", 0,
			RV1126B_PERICLKGATE_CON(0), 9, GFLAGS),
	GATE(CLK_SUSPEND_USB3OTG, "clk_suspend_usb3otg", "xin24m", 0,
			RV1126B_PERICLKGATE_CON(0), 10, GFLAGS),
	GATE(HCLK_USB2HOST, "hclk_usb2host", "aclk_peri_root", 0,
			RV1126B_PERICLKGATE_CON(0), 11, GFLAGS),
	GATE(HCLK_ARB_USB2HOST, "hclk_arb_usb2host", "aclk_peri_root", 0,
			RV1126B_PERICLKGATE_CON(0), 12, GFLAGS),
	GATE(PCLK_RTC_TEST, "pclk_rtc_test", "pclk_rtc_root", 0,
			RV1126B_PERICLKGATE_CON(0), 13, GFLAGS),
	GATE(HCLK_EMMC, "hclk_emmc", "aclk_peri_root", 0,
			RV1126B_PERICLKGATE_CON(1), 0, GFLAGS),
	GATE(HCLK_FSPI0, "hclk_fspi0", "aclk_peri_root", 0,
			RV1126B_PERICLKGATE_CON(1), 1, GFLAGS),
	GATE(HCLK_XIP_FSPI0, "hclk_xip_fspi0", "aclk_peri_root", 0,
			RV1126B_PERICLKGATE_CON(1), 2, GFLAGS),
	GATE(PCLK_PIPEPHY, "pclk_pipephy", "pclk_peri_root", 0,
			RV1126B_PERICLKGATE_CON(1), 8, GFLAGS),
	GATE(PCLK_USB2PHY, "pclk_usb2phy", "pclk_peri_root", 0,
			RV1126B_PERICLKGATE_CON(1), 10, GFLAGS),
	COMPOSITE_NOMUX(CLK_REF_PIPEPHY_CPLL_SRC, "clk_ref_pipephy_cpll_src", "cpll", 0,
			RV1126B_PERICLKSEL_CON(1), 0, 6, DFLAGS,
			RV1126B_PERICLKGATE_CON(1), 14, GFLAGS),
	MUX(CLK_REF_PIPEPHY, "clk_ref_pipephy", clk_ref_pipephy_p, 0,
			RV1126B_PERICLKSEL_CON(1), 12, 1, MFLAGS),
};

static struct rockchip_clk_branch rv1126b_armclk __initdata =
	MUX(ARMCLK, "armclk", mux_armclk_p, CLK_IS_CRITICAL | CLK_SET_RATE_PARENT,
			RV1126B_CORECLKSEL_CON(0), 1, 1, MFLAGS);

static void __init rv1126b_clk_init(struct device_node *np)
{
	struct rockchip_clk_provider *ctx;
	void __iomem *reg_base;
	unsigned long clk_nr_clks;

	clk_nr_clks = rockchip_clk_find_max_clk_id(rv1126b_clk_branches,
						   ARRAY_SIZE(rv1126b_clk_branches)) + 1;

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

	rockchip_clk_register_plls(ctx, rv1126b_pll_clks,
				   ARRAY_SIZE(rv1126b_pll_clks),
				   0);

	rockchip_clk_register_branches(ctx, rv1126b_clk_branches,
				       ARRAY_SIZE(rv1126b_clk_branches));

	rockchip_clk_register_armclk_multi_pll(ctx, &rv1126b_armclk,
					       rv1126b_cpuclk_rates,
					       ARRAY_SIZE(rv1126b_cpuclk_rates));

	rv1126b_rst_init(np, reg_base);

	rockchip_register_restart_notifier(ctx, RV1126B_GLB_SRST_FST, NULL);

	rockchip_clk_of_add_provider(np, ctx);

	/* pvtpll src init */
	writel_relaxed(PVTPLL_SRC_SEL_PVTPLL, reg_base + RV1126B_CORECLKSEL_CON(0));
	writel_relaxed(PVTPLL_SRC_SEL_PVTPLL, reg_base + RV1126B_NPUCLKSEL_CON(0));
	writel_relaxed(PVTPLL_SRC_SEL_PVTPLL, reg_base + RV1126B_VICLKSEL_CON(0));
	writel_relaxed(PVTPLL_SRC_SEL_PVTPLL, reg_base + RV1126B_VEPUCLKSEL_CON(0));
	writel_relaxed(PVTPLL_SRC_SEL_PVTPLL, reg_base + RV1126B_VCPCLKSEL_CON(0));
}

CLK_OF_DECLARE(rv1126b_cru, "rockchip,rv1126b-cru", rv1126b_clk_init);

struct clk_rv1126b_inits {
	void (*inits)(struct device_node *np);
};

static const struct clk_rv1126b_inits clk_rv1126b_init = {
	.inits = rv1126b_clk_init,
};

static const struct of_device_id clk_rv1126b_match_table[] = {
	{
		.compatible = "rockchip,rv1126b-cru",
		.data = &clk_rv1126b_init,
	},
	{ }
};

static int clk_rv1126b_probe(struct platform_device *pdev)
{
	const struct clk_rv1126b_inits *init_data;
	struct device *dev = &pdev->dev;

	init_data = device_get_match_data(dev);
	if (!init_data)
		return -EINVAL;

	if (init_data->inits)
		init_data->inits(dev->of_node);

	return 0;
}

static struct platform_driver clk_rv1126b_driver = {
	.probe		= clk_rv1126b_probe,
	.driver		= {
		.name	= "clk-rv1126b",
		.of_match_table = clk_rv1126b_match_table,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver_probe(clk_rv1126b_driver, clk_rv1126b_probe);
