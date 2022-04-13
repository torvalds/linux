// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 Rockchip Electronics Co. Ltd.
 * Author: Finley Xiao <finley.xiao@rock-chips.com>
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/syscore_ops.h>
#include <dt-bindings/clock/rv1126-cru.h>
#include "clk.h"

#define RV1126_GMAC_CON			0x460
#define RV1126_GRF_IOFUNC_CON1		0x10264
#define RV1126_GRF_SOC_STATUS0		0x10
#define RV1126_PMUGRF_SOC_CON0		0x100

#define RV1126_FRAC_MAX_PRATE		1200000000
#define RV1126_CSIOUT_FRAC_MAX_PRATE	300000000

enum rv1126_pmu_plls {
	gpll,
};

enum rv1126_plls {
	apll, dpll, cpll, hpll,
};

static struct rockchip_pll_rate_table rv1126_pll_rates[] = {
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
#ifdef CONFIG_ROCKCHIP_LOW_PERFORMANCE
	RK3036_PLL_RATE(600000000, 1, 50, 2, 1, 1, 0),
#else
	RK3036_PLL_RATE(600000000, 1, 100, 4, 1, 1, 0),
#endif
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

#define RV1126_DIV_ACLK_CORE_MASK	0xf
#define RV1126_DIV_ACLK_CORE_SHIFT	4
#define RV1126_DIV_PCLK_DBG_MASK	0x7
#define RV1126_DIV_PCLK_DBG_SHIFT	0

#define RV1126_CLKSEL1(_aclk_core, _pclk_dbg)				\
{									\
	.reg = RV1126_CLKSEL_CON(1),					\
	.val = HIWORD_UPDATE(_aclk_core, RV1126_DIV_ACLK_CORE_MASK,	\
			     RV1126_DIV_ACLK_CORE_SHIFT) |		\
	       HIWORD_UPDATE(_pclk_dbg, RV1126_DIV_PCLK_DBG_MASK,	\
			     RV1126_DIV_PCLK_DBG_SHIFT),		\
}

#define RV1126_CPUCLK_RATE(_prate, _aclk_core, _pclk_dbg)		\
{									\
	.prate = _prate,						\
	.divs = {							\
		RV1126_CLKSEL1(_aclk_core, _pclk_dbg),			\
	},								\
}

static struct rockchip_cpuclk_rate_table rv1126_cpuclk_rates[] __initdata = {
	RV1126_CPUCLK_RATE(1608000000, 1, 7),
	RV1126_CPUCLK_RATE(1584000000, 1, 7),
	RV1126_CPUCLK_RATE(1560000000, 1, 7),
	RV1126_CPUCLK_RATE(1536000000, 1, 7),
	RV1126_CPUCLK_RATE(1512000000, 1, 7),
	RV1126_CPUCLK_RATE(1488000000, 1, 5),
	RV1126_CPUCLK_RATE(1464000000, 1, 5),
	RV1126_CPUCLK_RATE(1440000000, 1, 5),
	RV1126_CPUCLK_RATE(1416000000, 1, 5),
	RV1126_CPUCLK_RATE(1392000000, 1, 5),
	RV1126_CPUCLK_RATE(1368000000, 1, 5),
	RV1126_CPUCLK_RATE(1344000000, 1, 5),
	RV1126_CPUCLK_RATE(1320000000, 1, 5),
	RV1126_CPUCLK_RATE(1296000000, 1, 5),
	RV1126_CPUCLK_RATE(1272000000, 1, 5),
	RV1126_CPUCLK_RATE(1248000000, 1, 5),
	RV1126_CPUCLK_RATE(1224000000, 1, 5),
	RV1126_CPUCLK_RATE(1200000000, 1, 5),
	RV1126_CPUCLK_RATE(1104000000, 1, 5),
	RV1126_CPUCLK_RATE(1008000000, 1, 5),
	RV1126_CPUCLK_RATE(912000000, 1, 5),
	RV1126_CPUCLK_RATE(816000000, 1, 3),
	RV1126_CPUCLK_RATE(696000000, 1, 3),
	RV1126_CPUCLK_RATE(600000000, 1, 3),
	RV1126_CPUCLK_RATE(408000000, 1, 1),
	RV1126_CPUCLK_RATE(312000000, 1, 1),
	RV1126_CPUCLK_RATE(216000000,  1, 1),
	RV1126_CPUCLK_RATE(96000000, 1, 1),
};

static const struct rockchip_cpuclk_reg_data rv1126_cpuclk_data = {
	.core_reg[0] = RV1126_CLKSEL_CON(0),
	.div_core_shift[0] = 0,
	.div_core_mask[0] = 0x1f,
	.num_cores = 1,
	.mux_core_alt = 0,
	.mux_core_main = 2,
	.mux_core_shift = 6,
	.mux_core_mask = 0x3,
};

PNAME(mux_pll_p)			= { "xin24m" };
PNAME(mux_rtc32k_p)			= { "clk_pmupvtm_divout", "xin32k", "clk_osc0_div32k" };
PNAME(mux_clk_32k_ioe_p)		= { "xin32k", "clk_rtc32k" };
PNAME(mux_wifi_p)			= { "clk_wifi_osc0", "clk_wifi_div" };
PNAME(mux_uart1_p)			= { "sclk_uart1_div", "sclk_uart1_fracdiv", "xin24m" };
PNAME(mux_xin24m_gpll_p)		= { "xin24m", "gpll" };
PNAME(mux_gpll_xin24m_p)		= { "gpll", "xin24m" };
PNAME(mux_xin24m_32k_p)			= { "xin24m", "clk_rtc32k" };
PNAME(mux_usbphy_otg_ref_p)		= { "clk_ref12m", "xin_osc0_div2_usbphyref_otg" };
PNAME(mux_usbphy_host_ref_p)		= { "clk_ref12m", "xin_osc0_div2_usbphyref_host" };
PNAME(mux_mipidsiphy_ref_p)		= { "clk_ref24m", "xin_osc0_mipiphyref" };
PNAME(mux_usb480m_p)			= { "xin24m", "usb480m_phy", "clk_rtc32k" };
PNAME(mux_hclk_pclk_pdbus_p)		= { "gpll", "dummy_cpll" };
PNAME(mux_uart0_p)			= { "sclk_uart0_div", "sclk_uart0_frac", "xin24m" };
PNAME(mux_uart2_p)			= { "sclk_uart2_div", "sclk_uart2_frac", "xin24m" };
PNAME(mux_uart3_p)			= { "sclk_uart3_div", "sclk_uart3_frac", "xin24m" };
PNAME(mux_uart4_p)			= { "sclk_uart4_div", "sclk_uart4_frac", "xin24m" };
PNAME(mux_uart5_p)			= { "sclk_uart5_div", "sclk_uart5_frac", "xin24m" };
PNAME(mux_i2s0_tx_p)			= { "mclk_i2s0_tx_div", "mclk_i2s0_tx_fracdiv", "i2s0_mclkin", "xin12m" };
PNAME(mux_i2s0_rx_p)			= { "mclk_i2s0_rx_div", "mclk_i2s0_rx_fracdiv", "i2s0_mclkin", "xin12m" };
PNAME(mux_i2s0_tx_out2io_p)		= { "mclk_i2s0_tx", "xin12m" };
PNAME(mux_i2s0_rx_out2io_p)		= { "mclk_i2s0_rx", "xin12m" };
PNAME(mux_i2s1_p)			= { "mclk_i2s1_div", "mclk_i2s1_fracdiv", "i2s1_mclkin", "xin12m" };
PNAME(mux_i2s1_out2io_p)		= { "mclk_i2s1", "xin12m" };
PNAME(mux_i2s2_p)			= { "mclk_i2s2_div", "mclk_i2s2_fracdiv", "i2s2_mclkin", "xin12m" };
PNAME(mux_i2s2_out2io_p)		= { "mclk_i2s2", "xin12m" };
PNAME(mux_audpwm_p)			= { "sclk_audpwm_div", "sclk_audpwm_fracdiv", "xin24m" };
PNAME(mux_dclk_vop_p)			= { "dclk_vop_div", "dclk_vop_fracdiv", "xin24m" };
PNAME(mux_aclk_pdvi_p)			= { "aclk_pdvi_div", "aclk_pdvi_np5" };
PNAME(mux_clk_isp_p)			= { "clk_isp_div", "clk_isp_np5" };
PNAME(mux_gpll_usb480m_p)		= { "gpll", "usb480m" };
PNAME(mux_cif_out2io_p)			= { "xin24m", "clk_cif_out2io_div", "clk_cif_out2io_fracdiv" };
PNAME(mux_mipicsi_out2io_p)		= { "xin24m", "clk_mipicsi_out2io_div", "clk_mipicsi_out2io_fracdiv" };
PNAME(mux_aclk_pdispp_p)		= { "aclk_pdispp_div", "aclk_pdispp_np5" };
PNAME(mux_clk_ispp_p)			= { "clk_ispp_div", "clk_ispp_np5" };
PNAME(mux_usb480m_gpll_p)		= { "usb480m", "gpll" };
PNAME(clk_gmac_src_m0_p)		= { "clk_gmac_div", "clk_gmac_rgmii_m0" };
PNAME(clk_gmac_src_m1_p)		= { "clk_gmac_div", "clk_gmac_rgmii_m1" };
PNAME(mux_clk_gmac_src_p)		= { "clk_gmac_src_m0", "clk_gmac_src_m1" };
PNAME(mux_rgmii_clk_p)			= { "clk_gmac_tx_div50", "clk_gmac_tx_div5", "clk_gmac_tx_src", "clk_gmac_tx_src"};
PNAME(mux_rmii_clk_p)			= { "clk_gmac_rx_div20", "clk_gmac_rx_div2" };
PNAME(mux_gmac_tx_rx_p)			= { "rgmii_mode_clk", "rmii_mode_clk" };
PNAME(mux_dpll_gpll_p)			= { "dpll", "gpll" };
PNAME(mux_aclk_pdnpu_p)			= { "aclk_pdnpu_div", "aclk_pdnpu_np5" };
PNAME(mux_clk_npu_p)			= { "clk_npu_div", "clk_npu_np5" };


#ifndef CONFIG_ROCKCHIP_LOW_PERFORMANCE
PNAME(mux_gpll_usb480m_cpll_xin24m_p)	= { "gpll", "usb480m", "cpll", "xin24m" };
PNAME(mux_gpll_cpll_dpll_p)		= { "gpll", "cpll", "dummy_dpll" };
PNAME(mux_gpll_cpll_p)			= { "gpll", "cpll" };
PNAME(mux_gpll_cpll_usb480m_xin24m_p)	= { "gpll", "cpll", "usb480m", "xin24m" };
PNAME(mux_cpll_gpll_p)			= { "cpll", "gpll" };
PNAME(mux_gpll_cpll_xin24m_p)		= { "gpll", "cpll", "xin24m" };
PNAME(mux_cpll_hpll_gpll_p)		= { "cpll", "hpll", "gpll" };
PNAME(mux_cpll_gpll_hpll_p)		= { "cpll", "gpll", "hpll" };
PNAME(mux_gpll_cpll_hpll_p)		= { "gpll", "cpll", "hpll" };
PNAME(mux_gpll_cpll_apll_hpll_p)	= { "gpll", "cpll", "dummy_apll", "hpll" };
#else
PNAME(mux_gpll_usb480m_cpll_xin24m_p)	= { "gpll", "usb480m", "dummy_cpll", "xin24m" };
PNAME(mux_gpll_cpll_dpll_p)		= { "gpll", "dummy_cpll", "dummy_dpll" };
PNAME(mux_gpll_cpll_p)			= { "gpll", "dummy_cpll" };
PNAME(mux_gpll_cpll_usb480m_xin24m_p)	= { "gpll", "dummy_cpll", "usb480m", "xin24m" };
PNAME(mux_cpll_gpll_p)			= { "dummy_cpll", "gpll" };
PNAME(mux_gpll_cpll_xin24m_p)		= { "gpll", "dummy_cpll", "xin24m" };
PNAME(mux_cpll_hpll_gpll_p)		= { "dummy_cpll", "dummy_hpll", "gpll" };
PNAME(mux_cpll_gpll_hpll_p)		= { "dummy_cpll", "gpll", "dummy_hpll" };
PNAME(mux_gpll_cpll_hpll_p)		= { "gpll", "dummy_cpll", "dummy_hpll" };
PNAME(mux_gpll_cpll_apll_hpll_p)	= { "gpll", "dummy_cpll", "dummy_apll", "dummy_hpll" };
#endif

static u32 rgmii_mux_idx[]		= { 2, 3, 0, 1 };

static struct rockchip_pll_clock rv1126_pmu_pll_clks[] __initdata = {
	[gpll] = PLL(pll_rk3328, PLL_GPLL, "gpll",  mux_pll_p,
		     CLK_IS_CRITICAL, RV1126_PMU_PLL_CON(0),
		     RV1126_PMU_MODE, 0, 3, 0, rv1126_pll_rates),
};

static struct rockchip_pll_clock rv1126_pll_clks[] __initdata = {
	[apll] = PLL(pll_rk3328, PLL_APLL, "apll", mux_pll_p,
		     CLK_IGNORE_UNUSED, RV1126_PLL_CON(0),
		     RV1126_MODE_CON, 0, 0, 0, rv1126_pll_rates),
	[dpll] = PLL(pll_rk3328, PLL_DPLL, "dpll", mux_pll_p,
		     CLK_IGNORE_UNUSED, RV1126_PLL_CON(8),
		     RV1126_MODE_CON, 2, 1, 0, NULL),
#ifndef CONFIG_ROCKCHIP_LOW_PERFORMANCE
	[cpll] = PLL(pll_rk3328, PLL_CPLL, "cpll", mux_pll_p,
		     CLK_IS_CRITICAL, RV1126_PLL_CON(16),
		     RV1126_MODE_CON, 4, 2, 0, rv1126_pll_rates),
	[hpll] = PLL(pll_rk3328, PLL_HPLL, "hpll", mux_pll_p,
		     CLK_IS_CRITICAL, RV1126_PLL_CON(24),
		     RV1126_MODE_CON, 6, 4, 0, rv1126_pll_rates),
#else
	[cpll] = PLL(pll_rk3328, PLL_CPLL, "cpll", mux_pll_p,
		     0, RV1126_PLL_CON(16),
		     RV1126_MODE_CON, 4, 2, 0, rv1126_pll_rates),
	[hpll] = PLL(pll_rk3328, PLL_HPLL, "hpll", mux_pll_p,
		     0, RV1126_PLL_CON(24),
		     RV1126_MODE_CON, 6, 4, 0, rv1126_pll_rates),
#endif
};

#define MFLAGS CLK_MUX_HIWORD_MASK
#define DFLAGS CLK_DIVIDER_HIWORD_MASK
#define GFLAGS (CLK_GATE_HIWORD_MASK | CLK_GATE_SET_TO_DISABLE)

static struct rockchip_clk_branch rv1126_rtc32k_fracmux __initdata =
	MUX(CLK_RTC32K, "clk_rtc32k", mux_rtc32k_p, CLK_SET_RATE_PARENT,
			RV1126_PMU_CLKSEL_CON(0), 7, 2, MFLAGS);

static struct rockchip_clk_branch rv1126_uart1_fracmux __initdata =
	MUX(SCLK_UART1_MUX, "sclk_uart1_mux", mux_uart1_p, CLK_SET_RATE_PARENT,
			RV1126_PMU_CLKSEL_CON(4), 10, 2, MFLAGS);

static struct rockchip_clk_branch rv1126_uart0_fracmux __initdata =
	MUX(SCLK_UART0_MUX, "sclk_uart0_mux", mux_uart0_p, CLK_SET_RATE_PARENT,
			RV1126_CLKSEL_CON(10), 10, 2, MFLAGS);

static struct rockchip_clk_branch rv1126_uart2_fracmux __initdata =
	MUX(SCLK_UART2_MUX, "sclk_uart2_mux", mux_uart2_p, CLK_SET_RATE_PARENT,
			RV1126_CLKSEL_CON(12), 10, 2, MFLAGS);

static struct rockchip_clk_branch rv1126_uart3_fracmux __initdata =
	MUX(SCLK_UART3_MUX, "sclk_uart3_mux", mux_uart3_p, CLK_SET_RATE_PARENT,
			RV1126_CLKSEL_CON(14), 10, 2, MFLAGS);

static struct rockchip_clk_branch rv1126_uart4_fracmux __initdata =
	MUX(SCLK_UART4_MUX, "sclk_uart4_mux", mux_uart4_p, CLK_SET_RATE_PARENT,
			RV1126_CLKSEL_CON(16), 10, 2, MFLAGS);

static struct rockchip_clk_branch rv1126_uart5_fracmux __initdata =
	MUX(SCLK_UART5_MUX, "sclk_uart5_mux", mux_uart5_p, CLK_SET_RATE_PARENT,
			RV1126_CLKSEL_CON(18), 10, 2, MFLAGS);

static struct rockchip_clk_branch rv1126_i2s0_tx_fracmux __initdata =
	MUX(MCLK_I2S0_TX_MUX, "mclk_i2s0_tx_mux", mux_i2s0_tx_p, CLK_SET_RATE_PARENT,
			RV1126_CLKSEL_CON(30), 0, 2, MFLAGS);

static struct rockchip_clk_branch rv1126_i2s0_rx_fracmux __initdata =
	MUX(MCLK_I2S0_RX_MUX, "mclk_i2s0_rx_mux", mux_i2s0_rx_p, CLK_SET_RATE_PARENT,
			RV1126_CLKSEL_CON(30), 2, 2, MFLAGS);

static struct rockchip_clk_branch rv1126_i2s1_fracmux __initdata =
	MUX(MCLK_I2S1_MUX, "mclk_i2s1_mux", mux_i2s1_p, CLK_SET_RATE_PARENT,
			RV1126_CLKSEL_CON(31), 8, 2, MFLAGS);

static struct rockchip_clk_branch rv1126_i2s2_fracmux __initdata =
	MUX(MCLK_I2S2_MUX, "mclk_i2s2_mux", mux_i2s2_p, CLK_SET_RATE_PARENT,
			RV1126_CLKSEL_CON(33), 8, 2, MFLAGS);

static struct rockchip_clk_branch rv1126_audpwm_fracmux __initdata =
	MUX(SCLK_AUDPWM_MUX, "mclk_audpwm_mux", mux_audpwm_p, CLK_SET_RATE_PARENT,
			RV1126_CLKSEL_CON(36), 8, 2, MFLAGS);

static struct rockchip_clk_branch rv1126_dclk_vop_fracmux __initdata =
	MUX(DCLK_VOP_MUX, "dclk_vop_mux", mux_dclk_vop_p, CLK_SET_RATE_PARENT,
			RV1126_CLKSEL_CON(47), 10, 2, MFLAGS);

static struct rockchip_clk_branch rv1126_cif_out2io_fracmux __initdata =
	MUX(CLK_CIF_OUT_MUX, "clk_cif_out2io_mux", mux_cif_out2io_p, CLK_SET_RATE_PARENT,
			RV1126_CLKSEL_CON(50), 14, 2, MFLAGS);

static struct rockchip_clk_branch rv1126_mipicsi_out2io_fracmux __initdata =
	MUX(CLK_MIPICSI_OUT_MUX, "clk_mipicsi_out2io_mux", mux_mipicsi_out2io_p, CLK_SET_RATE_PARENT,
			RV1126_CLKSEL_CON(73), 10, 2, MFLAGS);

static struct rockchip_clk_branch rv1126_clk_pmu_branches[] __initdata = {
	/*
	 * Clock-Architecture Diagram 2
	 */
	/* PD_PMU */
	COMPOSITE_NOMUX(PCLK_PDPMU, "pclk_pdpmu", "gpll", CLK_IS_CRITICAL,
			RV1126_PMU_CLKSEL_CON(1), 0, 5, DFLAGS,
			RV1126_PMU_CLKGATE_CON(0), 0, GFLAGS),

	COMPOSITE_FRACMUX(CLK_OSC0_DIV32K, "clk_osc0_div32k", "xin24m", CLK_IGNORE_UNUSED,
			RV1126_PMU_CLKSEL_CON(13), 0,
			RV1126_PMU_CLKGATE_CON(2), 9, GFLAGS,
			&rv1126_rtc32k_fracmux),

	MUXPMUGRF(CLK_32K_IOE, "clk_32k_ioe", mux_clk_32k_ioe_p,  0,
			RV1126_PMUGRF_SOC_CON0, 0, 1, MFLAGS),

	COMPOSITE_NOMUX(CLK_WIFI_DIV, "clk_wifi_div", "gpll", 0,
			RV1126_PMU_CLKSEL_CON(12), 0, 6, DFLAGS,
			RV1126_PMU_CLKGATE_CON(2), 10, GFLAGS),
	GATE(CLK_WIFI_OSC0, "clk_wifi_osc0", "xin24m", 0,
			RV1126_PMU_CLKGATE_CON(2), 11, GFLAGS),
	MUX(CLK_WIFI, "clk_wifi", mux_wifi_p, CLK_SET_RATE_PARENT,
			RV1126_PMU_CLKSEL_CON(12), 8, 1, MFLAGS),

	GATE(PCLK_PMU, "pclk_pmu", "pclk_pdpmu", CLK_IGNORE_UNUSED,
			RV1126_PMU_CLKGATE_CON(0), 1, GFLAGS),

	GATE(PCLK_UART1, "pclk_uart1", "pclk_pdpmu", 0,
			RV1126_PMU_CLKGATE_CON(0), 11, GFLAGS),
	COMPOSITE(SCLK_UART1_DIV, "sclk_uart1_div", mux_gpll_usb480m_cpll_xin24m_p, 0,
			RV1126_PMU_CLKSEL_CON(4), 8, 2, MFLAGS, 0, 7, DFLAGS,
			RV1126_PMU_CLKGATE_CON(0), 12, GFLAGS),
	COMPOSITE_FRACMUX(SCLK_UART1_FRACDIV, "sclk_uart1_fracdiv", "sclk_uart1_div", CLK_SET_RATE_PARENT,
			RV1126_PMU_CLKSEL_CON(5), 0,
			RV1126_PMU_CLKGATE_CON(0), 13, GFLAGS,
			&rv1126_uart1_fracmux),
	GATE(SCLK_UART1, "sclk_uart1", "sclk_uart1_mux", 0,
			RV1126_PMU_CLKGATE_CON(0), 14, GFLAGS),

	GATE(PCLK_I2C0, "pclk_i2c0", "pclk_pdpmu", 0,
			RV1126_PMU_CLKGATE_CON(0), 5, GFLAGS),
	COMPOSITE_NOMUX(CLK_I2C0, "clk_i2c0", "gpll", 0,
			RV1126_PMU_CLKSEL_CON(2), 0, 7, DFLAGS,
			RV1126_PMU_CLKGATE_CON(0), 6, GFLAGS),
	GATE(PCLK_I2C2, "pclk_i2c2", "pclk_pdpmu", 0,
			RV1126_PMU_CLKGATE_CON(0), 9, GFLAGS),
	COMPOSITE_NOMUX(CLK_I2C2, "clk_i2c2", "gpll", 0,
			RV1126_PMU_CLKSEL_CON(3), 0, 7, DFLAGS,
			RV1126_PMU_CLKGATE_CON(0), 10, GFLAGS),

	GATE(CLK_CAPTURE_PWM0, "clk_capture_pwm0", "xin24m", 0,
			RV1126_PMU_CLKGATE_CON(1), 2, GFLAGS),
	GATE(PCLK_PWM0, "pclk_pwm0", "pclk_pdpmu", 0,
			RV1126_PMU_CLKGATE_CON(1), 0, GFLAGS),
	COMPOSITE(CLK_PWM0, "clk_pwm0", mux_xin24m_gpll_p, 0,
			RV1126_PMU_CLKSEL_CON(6), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RV1126_PMU_CLKGATE_CON(1), 1, GFLAGS),
	GATE(CLK_CAPTURE_PWM1, "clk_capture_pwm1", "xin24m", 0,
			RV1126_PMU_CLKGATE_CON(1), 5, GFLAGS),
	GATE(PCLK_PWM1, "pclk_pwm1", "pclk_pdpmu", 0,
			RV1126_PMU_CLKGATE_CON(1), 3, GFLAGS),
	COMPOSITE(CLK_PWM1, "clk_pwm1", mux_xin24m_gpll_p, 0,
			RV1126_PMU_CLKSEL_CON(6), 15, 1, MFLAGS, 8, 7, DFLAGS,
			RV1126_PMU_CLKGATE_CON(1), 4, GFLAGS),

	GATE(PCLK_SPI0, "pclk_spi0", "pclk_pdpmu", 0,
			RV1126_PMU_CLKGATE_CON(1), 11, GFLAGS),
	COMPOSITE(CLK_SPI0, "clk_spi0", mux_gpll_xin24m_p, 0,
			RV1126_PMU_CLKSEL_CON(9), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RV1126_PMU_CLKGATE_CON(1), 12, GFLAGS),

	GATE(PCLK_GPIO0, "pclk_gpio0", "pclk_pdpmu", 0,
			RV1126_PMU_CLKGATE_CON(1), 9, GFLAGS),
	COMPOSITE_NODIV(DBCLK_GPIO0, "dbclk_gpio0", mux_xin24m_32k_p, 0,
			RV1126_PMU_CLKSEL_CON(8), 15, 1, MFLAGS,
			RV1126_PMU_CLKGATE_CON(1), 10, GFLAGS),

	GATE(PCLK_PMUPVTM, "pclk_pmupvtm", "pclk_pdpmu", 0,
			RV1126_PMU_CLKGATE_CON(2), 6, GFLAGS),
	GATE(CLK_PMUPVTM, "clk_pmupvtm", "xin24m", 0,
			RV1126_PMU_CLKGATE_CON(2), 5, GFLAGS),
	GATE(CLK_CORE_PMUPVTM, "clk_core_pmupvtm", "xin24m", 0,
			RV1126_PMU_CLKGATE_CON(2), 7, GFLAGS),

	COMPOSITE_NOMUX(CLK_REF12M, "clk_ref12m", "gpll", 0,
			RV1126_PMU_CLKSEL_CON(7), 8, 7, DFLAGS,
			RV1126_PMU_CLKGATE_CON(1), 15, GFLAGS),
	GATE(0, "xin_osc0_usbphyref_otg", "xin24m", 0,
			RV1126_PMU_CLKGATE_CON(1), 6, GFLAGS),
	GATE(0, "xin_osc0_usbphyref_host", "xin24m", 0,
			RV1126_PMU_CLKGATE_CON(1), 7, GFLAGS),
	FACTOR(0, "xin_osc0_div2_usbphyref_otg", "xin_osc0_usbphyref_otg", 0, 1, 2),
	FACTOR(0, "xin_osc0_div2_usbphyref_host", "xin_osc0_usbphyref_host", 0, 1, 2),
	MUX(CLK_USBPHY_OTG_REF, "clk_usbphy_otg_ref", mux_usbphy_otg_ref_p, CLK_SET_RATE_PARENT,
			RV1126_PMU_CLKSEL_CON(7), 6, 1, MFLAGS),
	MUX(CLK_USBPHY_HOST_REF, "clk_usbphy_host_ref", mux_usbphy_host_ref_p, CLK_SET_RATE_PARENT,
			RV1126_PMU_CLKSEL_CON(7), 7, 1, MFLAGS),

	COMPOSITE_NOMUX(CLK_REF24M, "clk_ref24m", "gpll", 0,
			RV1126_PMU_CLKSEL_CON(7), 0, 6, DFLAGS,
			RV1126_PMU_CLKGATE_CON(1), 14, GFLAGS),
	GATE(0, "xin_osc0_mipiphyref", "xin24m", 0,
			RV1126_PMU_CLKGATE_CON(1), 8, GFLAGS),
	MUX(CLK_MIPIDSIPHY_REF, "clk_mipidsiphy_ref", mux_mipidsiphy_ref_p, CLK_SET_RATE_PARENT,
			RV1126_PMU_CLKSEL_CON(7), 15, 1, MFLAGS),

#ifndef CONFIG_ROCKCHIP_LOW_PERFORMANCE
	GATE(CLK_PMU, "clk_pmu", "xin24m", CLK_IGNORE_UNUSED,
			RV1126_PMU_CLKGATE_CON(0), 15, GFLAGS),

	GATE(PCLK_PMUSGRF, "pclk_pmusgrf", "pclk_pdpmu", CLK_IGNORE_UNUSED,
			RV1126_PMU_CLKGATE_CON(0), 4, GFLAGS),
	GATE(PCLK_PMUGRF, "pclk_pmugrf", "pclk_pdpmu", CLK_IGNORE_UNUSED,
			RV1126_PMU_CLKGATE_CON(1), 13, GFLAGS),
	GATE(PCLK_PMUCRU, "pclk_pmucru", "pclk_pdpmu", CLK_IGNORE_UNUSED,
			RV1126_PMU_CLKGATE_CON(2), 4, GFLAGS),
	GATE(PCLK_CHIPVEROTP, "pclk_chipverotp", "pclk_pdpmu", CLK_IGNORE_UNUSED,
			RV1126_PMU_CLKGATE_CON(2), 0, GFLAGS),
	GATE(PCLK_PDPMU_NIU, "pclk_pdpmu_niu", "pclk_pdpmu", CLK_IGNORE_UNUSED,
			RV1126_PMU_CLKGATE_CON(0), 2, GFLAGS),

	GATE(PCLK_SCRKEYGEN, "pclk_scrkeygen", "pclk_pdpmu", 0,
			RV1126_PMU_CLKGATE_CON(0), 7, GFLAGS),
#endif
};

static struct rockchip_clk_branch rv1126_clk_branches[] __initdata = {
	/*
	 * Clock-Architecture Diagram 1
	 */
	MUX(USB480M, "usb480m", mux_usb480m_p, CLK_SET_RATE_PARENT,
			RV1126_MODE_CON, 10, 2, MFLAGS),
	FACTOR(0, "xin12m", "xin24m", 0, 1, 2),

	/*
	 * Clock-Architecture Diagram 3
	 */
	/* PD_CORE */
	COMPOSITE_NOMUX(0, "pclk_dbg", "armclk", CLK_IS_CRITICAL,
			RV1126_CLKSEL_CON(1), 0, 3, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RV1126_CLKGATE_CON(0), 6, GFLAGS),
	GATE(CLK_CORE_CPUPVTM, "clk_core_cpupvtm", "armclk", 0,
			RV1126_CLKGATE_CON(0), 12, GFLAGS),
	GATE(PCLK_CPUPVTM, "pclk_cpupvtm", "pclk_dbg", 0,
			RV1126_CLKGATE_CON(0), 10, GFLAGS),
	GATE(CLK_CPUPVTM, "clk_cpupvtm", "xin24m", 0,
			RV1126_CLKGATE_CON(0), 11, GFLAGS),
	COMPOSITE_NOMUX(HCLK_PDCORE_NIU, "hclk_pdcore_niu", "gpll", CLK_IGNORE_UNUSED,
			RV1126_CLKSEL_CON(0), 8, 5, DFLAGS,
			RV1126_CLKGATE_CON(0), 8, GFLAGS),

	/*
	 * Clock-Architecture Diagram 4
	 */
	/* PD_BUS */
	COMPOSITE(0, "aclk_pdbus_pre", mux_gpll_cpll_dpll_p, CLK_IS_CRITICAL,
			RV1126_CLKSEL_CON(2), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RV1126_CLKGATE_CON(2), 0, GFLAGS),
	GATE(ACLK_PDBUS, "aclk_pdbus", "aclk_pdbus_pre", CLK_IS_CRITICAL,
			RV1126_CLKGATE_CON(2), 11, GFLAGS),
	COMPOSITE(0, "hclk_pdbus_pre", mux_hclk_pclk_pdbus_p, CLK_IS_CRITICAL,
			RV1126_CLKSEL_CON(2), 15, 1, MFLAGS, 8, 5, DFLAGS,
			RV1126_CLKGATE_CON(2), 1, GFLAGS),
	GATE(HCLK_PDBUS, "hclk_pdbus", "hclk_pdbus_pre", CLK_IS_CRITICAL,
			RV1126_CLKGATE_CON(2), 12, GFLAGS),
	COMPOSITE(0, "pclk_pdbus_pre", mux_hclk_pclk_pdbus_p, CLK_IS_CRITICAL,
			RV1126_CLKSEL_CON(3), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RV1126_CLKGATE_CON(2), 2, GFLAGS),
	GATE(PCLK_PDBUS, "pclk_pdbus", "pclk_pdbus_pre", CLK_IS_CRITICAL,
			RV1126_CLKGATE_CON(2), 13, GFLAGS),
	/* aclk_dmac is controlled by sgrf_clkgat_con. */
	SGRF_GATE(ACLK_DMAC, "aclk_dmac", "hclk_pdbus"),
	GATE(ACLK_DCF, "aclk_dcf", "hclk_pdbus", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(3), 6, GFLAGS),
	GATE(PCLK_DCF, "pclk_dcf", "pclk_pdbus", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(3), 7, GFLAGS),
	GATE(PCLK_WDT, "pclk_wdt", "pclk_pdbus", 0,
			RV1126_CLKGATE_CON(6), 14, GFLAGS),
	GATE(PCLK_MAILBOX, "pclk_mailbox", "pclk_pdbus", 0,
			RV1126_CLKGATE_CON(7), 10, GFLAGS),

	COMPOSITE(CLK_SCR1, "clk_scr1", mux_gpll_cpll_p, 0,
			RV1126_CLKSEL_CON(3), 15, 1, MFLAGS, 8, 5, DFLAGS,
			RV1126_CLKGATE_CON(4), 7, GFLAGS),
	GATE(0, "clk_scr1_niu", "clk_scr1", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(2), 14, GFLAGS),
	GATE(CLK_SCR1_CORE, "clk_scr1_core", "clk_scr1", 0,
			RV1126_CLKGATE_CON(4), 8, GFLAGS),
	GATE(CLK_SCR1_RTC, "clk_scr1_rtc", "xin24m", 0,
			RV1126_CLKGATE_CON(4), 9, GFLAGS),
	GATE(CLK_SCR1_JTAG, "clk_scr1_jtag", "clk_scr1_jtag_io", 0,
			RV1126_CLKGATE_CON(4), 10, GFLAGS),

	GATE(PCLK_UART0, "pclk_uart0", "pclk_pdbus", 0,
			RV1126_CLKGATE_CON(5), 0, GFLAGS),
	COMPOSITE(SCLK_UART0_DIV, "sclk_uart0_div", mux_gpll_cpll_usb480m_xin24m_p, 0,
			RV1126_CLKSEL_CON(10), 8, 2, MFLAGS, 0, 7, DFLAGS,
			RV1126_CLKGATE_CON(5), 1, GFLAGS),
	COMPOSITE_FRACMUX(SCLK_UART0_FRAC, "sclk_uart0_frac", "sclk_uart0_div", CLK_SET_RATE_PARENT,
			RV1126_CLKSEL_CON(11), 0,
			RV1126_CLKGATE_CON(5), 2, GFLAGS,
			&rv1126_uart0_fracmux),
	GATE(SCLK_UART0, "sclk_uart0", "sclk_uart0_mux", 0,
			RV1126_CLKGATE_CON(5), 3, GFLAGS),
	GATE(PCLK_UART2, "pclk_uart2", "pclk_pdbus", 0,
			RV1126_CLKGATE_CON(5), 4, GFLAGS),
	COMPOSITE(SCLK_UART2_DIV, "sclk_uart2_div", mux_gpll_cpll_usb480m_xin24m_p, 0,
			RV1126_CLKSEL_CON(12), 8, 2, MFLAGS, 0, 7, DFLAGS,
			RV1126_CLKGATE_CON(5), 5, GFLAGS),
	COMPOSITE_FRACMUX(SCLK_UART2_FRAC, "sclk_uart2_frac", "sclk_uart2_div", CLK_SET_RATE_PARENT,
			RV1126_CLKSEL_CON(13), 0,
			RV1126_CLKGATE_CON(5), 6, GFLAGS,
			&rv1126_uart2_fracmux),
	GATE(SCLK_UART2, "sclk_uart2", "sclk_uart2_mux", 0,
			RV1126_CLKGATE_CON(5), 7, GFLAGS),
	GATE(PCLK_UART3, "pclk_uart3", "pclk_pdbus", 0,
			RV1126_CLKGATE_CON(5), 8, GFLAGS),
	COMPOSITE(SCLK_UART3_DIV, "sclk_uart3_div", mux_gpll_cpll_usb480m_xin24m_p, 0,
			RV1126_CLKSEL_CON(14), 8, 2, MFLAGS, 0, 7, DFLAGS,
			RV1126_CLKGATE_CON(5), 9, GFLAGS),
	COMPOSITE_FRACMUX(SCLK_UART3_FRAC, "sclk_uart3_frac", "sclk_uart3_div", CLK_SET_RATE_PARENT,
			RV1126_CLKSEL_CON(15), 0,
			RV1126_CLKGATE_CON(5), 10, GFLAGS,
			&rv1126_uart3_fracmux),
	GATE(SCLK_UART3, "sclk_uart3", "sclk_uart3_mux", 0,
			RV1126_CLKGATE_CON(5), 11, GFLAGS),
	GATE(PCLK_UART4, "pclk_uart4", "pclk_pdbus", 0,
			RV1126_CLKGATE_CON(5), 12, GFLAGS),
	COMPOSITE(SCLK_UART4_DIV, "sclk_uart4_div", mux_gpll_cpll_usb480m_xin24m_p, 0,
			RV1126_CLKSEL_CON(16), 8, 2, MFLAGS, 0, 7,
			DFLAGS, RV1126_CLKGATE_CON(5), 13, GFLAGS),
	COMPOSITE_FRACMUX(SCLK_UART4_FRAC, "sclk_uart4_frac", "sclk_uart4_div", CLK_SET_RATE_PARENT,
			RV1126_CLKSEL_CON(17), 0,
			RV1126_CLKGATE_CON(5), 14, GFLAGS,
			&rv1126_uart4_fracmux),
	GATE(SCLK_UART4, "sclk_uart4", "sclk_uart4_mux", 0,
			RV1126_CLKGATE_CON(5), 15, GFLAGS),
	GATE(PCLK_UART5, "pclk_uart5", "pclk_pdbus", 0,
			RV1126_CLKGATE_CON(6), 0, GFLAGS),
	COMPOSITE(SCLK_UART5_DIV, "sclk_uart5_div", mux_gpll_cpll_usb480m_xin24m_p, 0,
			RV1126_CLKSEL_CON(18), 8, 2, MFLAGS, 0, 7,
			DFLAGS, RV1126_CLKGATE_CON(6), 1, GFLAGS),
	COMPOSITE_FRACMUX(SCLK_UART5_FRAC, "sclk_uart5_frac", "sclk_uart5_div", CLK_SET_RATE_PARENT,
			RV1126_CLKSEL_CON(19), 0,
			RV1126_CLKGATE_CON(6), 2, GFLAGS,
			&rv1126_uart5_fracmux),
	GATE(SCLK_UART5, "sclk_uart5", "sclk_uart5_mux", 0,
			RV1126_CLKGATE_CON(6), 3, GFLAGS),

	GATE(PCLK_I2C1, "pclk_i2c1", "pclk_pdbus", 0,
			RV1126_CLKGATE_CON(3), 10, GFLAGS),
	COMPOSITE_NOMUX(CLK_I2C1, "clk_i2c1", "gpll", 0,
			RV1126_CLKSEL_CON(5), 0, 7, DFLAGS,
			RV1126_CLKGATE_CON(3), 11, GFLAGS),
	GATE(PCLK_I2C3, "pclk_i2c3", "pclk_pdbus", 0,
			RV1126_CLKGATE_CON(3), 12, GFLAGS),
	COMPOSITE_NOMUX(CLK_I2C3, "clk_i2c3", "gpll", 0,
			RV1126_CLKSEL_CON(5), 8, 7, DFLAGS,
			RV1126_CLKGATE_CON(3), 13, GFLAGS),
	GATE(PCLK_I2C4, "pclk_i2c4", "pclk_pdbus", 0,
			RV1126_CLKGATE_CON(3), 14, GFLAGS),
	COMPOSITE_NOMUX(CLK_I2C4, "clk_i2c4", "gpll", 0,
			RV1126_CLKSEL_CON(6), 0, 7, DFLAGS,
			RV1126_CLKGATE_CON(3), 15, GFLAGS),
	GATE(PCLK_I2C5, "pclk_i2c5", "pclk_pdbus", 0,
			RV1126_CLKGATE_CON(4), 0, GFLAGS),
	COMPOSITE_NOMUX(CLK_I2C5, "clk_i2c5", "gpll", 0,
			RV1126_CLKSEL_CON(6), 8, 7, DFLAGS,
			RV1126_CLKGATE_CON(4), 1, GFLAGS),

	GATE(PCLK_SPI1, "pclk_spi1", "pclk_pdbus", 0,
			RV1126_CLKGATE_CON(4), 2, GFLAGS),
	COMPOSITE(CLK_SPI1, "clk_spi1", mux_gpll_xin24m_p, 0,
			RV1126_CLKSEL_CON(8), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RV1126_CLKGATE_CON(4), 3, GFLAGS),

	GATE(CLK_CAPTURE_PWM2, "clk_capture_pwm2", "xin24m", 0,
			RV1126_CLKGATE_CON(4), 6, GFLAGS),
	GATE(PCLK_PWM2, "pclk_pwm2", "pclk_pdbus", 0,
			RV1126_CLKGATE_CON(4), 4, GFLAGS),
	COMPOSITE(CLK_PWM2, "clk_pwm2", mux_xin24m_gpll_p, 0,
			RV1126_CLKSEL_CON(9), 15, 1, MFLAGS, 8, 7, DFLAGS,
			RV1126_CLKGATE_CON(4), 5, GFLAGS),

	GATE(PCLK_GPIO1, "pclk_gpio1", "pclk_pdbus", 0,
			RV1126_CLKGATE_CON(7), 0, GFLAGS),
	COMPOSITE_NODIV(DBCLK_GPIO1, "dbclk_gpio1", mux_xin24m_32k_p, 0,
			RV1126_CLKSEL_CON(21), 15, 1, MFLAGS,
			RV1126_CLKGATE_CON(7), 1, GFLAGS),
	GATE(PCLK_GPIO2, "pclk_gpio2", "pclk_pdbus", 0,
			RV1126_CLKGATE_CON(7), 2, GFLAGS),
	COMPOSITE_NODIV(DBCLK_GPIO2, "dbclk_gpio2", mux_xin24m_32k_p, 0,
			RV1126_CLKSEL_CON(22), 15, 1, MFLAGS,
			RV1126_CLKGATE_CON(7), 3, GFLAGS),
	GATE(PCLK_GPIO3, "pclk_gpio3", "pclk_pdbus", 0,
			RV1126_CLKGATE_CON(7), 4, GFLAGS),
	COMPOSITE_NODIV(DBCLK_GPIO3, "dbclk_gpio3", mux_xin24m_32k_p, 0,
			RV1126_CLKSEL_CON(23), 15, 1, MFLAGS,
			RV1126_CLKGATE_CON(7), 5, GFLAGS),
	GATE(PCLK_GPIO4, "pclk_gpio4", "pclk_pdbus", 0,
			RV1126_CLKGATE_CON(7), 6, GFLAGS),
	COMPOSITE_NODIV(DBCLK_GPIO4, "dbclk_gpio4", mux_xin24m_32k_p, 0,
			RV1126_CLKSEL_CON(24), 15, 1, MFLAGS,
			RV1126_CLKGATE_CON(7), 7, GFLAGS),

	GATE(PCLK_SARADC, "pclk_saradc", "pclk_pdbus", 0,
			RV1126_CLKGATE_CON(6), 4, GFLAGS),
	COMPOSITE_NOMUX(CLK_SARADC, "clk_saradc", "xin24m", 0,
			RV1126_CLKSEL_CON(20), 0, 11, DFLAGS,
			RV1126_CLKGATE_CON(6), 5, GFLAGS),

	GATE(PCLK_TIMER, "pclk_timer", "pclk_pdbus", 0,
			RV1126_CLKGATE_CON(6), 7, GFLAGS),
	GATE(CLK_TIMER0, "clk_timer0", "xin24m", 0,
			RV1126_CLKGATE_CON(6), 8, GFLAGS),
	GATE(CLK_TIMER1, "clk_timer1", "xin24m", 0,
			RV1126_CLKGATE_CON(6), 9, GFLAGS),
	GATE(CLK_TIMER2, "clk_timer2", "xin24m", 0,
			RV1126_CLKGATE_CON(6), 10, GFLAGS),
	GATE(CLK_TIMER3, "clk_timer3", "xin24m", 0,
			RV1126_CLKGATE_CON(6), 11, GFLAGS),
	GATE(CLK_TIMER4, "clk_timer4", "xin24m", 0,
			RV1126_CLKGATE_CON(6), 12, GFLAGS),
	GATE(CLK_TIMER5, "clk_timer5", "xin24m", 0,
			RV1126_CLKGATE_CON(6), 13, GFLAGS),

	GATE(ACLK_SPINLOCK, "aclk_spinlock", "hclk_pdbus", 0,
			RV1126_CLKGATE_CON(6), 6, GFLAGS),

	GATE(ACLK_DECOM, "aclk_decom", "aclk_pdbus", 0,
			RV1126_CLKGATE_CON(7), 11, GFLAGS),
	GATE(PCLK_DECOM, "pclk_decom", "pclk_pdbus", 0,
			RV1126_CLKGATE_CON(7), 12, GFLAGS),
	COMPOSITE(DCLK_DECOM, "dclk_decom", mux_gpll_cpll_p, 0,
			RV1126_CLKSEL_CON(25), 15, 1, MFLAGS, 8, 7, DFLAGS,
			RV1126_CLKGATE_CON(7), 13, GFLAGS),

	GATE(PCLK_CAN, "pclk_can", "pclk_pdbus", 0,
			RV1126_CLKGATE_CON(7), 8, GFLAGS),
	COMPOSITE(CLK_CAN, "clk_can", mux_gpll_xin24m_p, 0,
			RV1126_CLKSEL_CON(25), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RV1126_CLKGATE_CON(7), 9, GFLAGS),
	/* pclk_otp and clk_otp are controlled by sgrf_clkgat_con. */
	SGRF_GATE(CLK_OTP, "clk_otp", "xin24m"),
	SGRF_GATE(PCLK_OTP, "pclk_otp", "pclk_pdbus"),

	GATE(PCLK_NPU_TSADC, "pclk_npu_tsadc", "pclk_pdbus", 0,
			RV1126_CLKGATE_CON(24), 3, GFLAGS),
	COMPOSITE_NOMUX(CLK_NPU_TSADC, "clk_npu_tsadc", "xin24m", 0,
			RV1126_CLKSEL_CON(71), 0, 11, DFLAGS,
			RV1126_CLKGATE_CON(24), 4, GFLAGS),
	GATE(CLK_NPU_TSADCPHY, "clk_npu_tsadcphy", "clk_npu_tsadc", 0,
			RV1126_CLKGATE_CON(24), 5, GFLAGS),
	GATE(PCLK_CPU_TSADC, "pclk_cpu_tsadc", "pclk_pdbus", 0,
			RV1126_CLKGATE_CON(24), 0, GFLAGS),
	COMPOSITE_NOMUX(CLK_CPU_TSADC, "clk_cpu_tsadc", "xin24m", 0,
			RV1126_CLKSEL_CON(70), 0, 11, DFLAGS,
			RV1126_CLKGATE_CON(24), 1, GFLAGS),
	GATE(CLK_CPU_TSADCPHY, "clk_cpu_tsadcphy", "clk_cpu_tsadc", 0,
			RV1126_CLKGATE_CON(24), 2, GFLAGS),

	/*
	 * Clock-Architecture Diagram 5
	 */
	/* PD_CRYPTO */
	COMPOSITE(ACLK_PDCRYPTO, "aclk_pdcrypto", mux_gpll_cpll_p, 0,
			RV1126_CLKSEL_CON(4), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RV1126_CLKGATE_CON(4), 11, GFLAGS),
	COMPOSITE_NOMUX(HCLK_PDCRYPTO, "hclk_pdcrypto", "aclk_pdcrypto", 0,
			RV1126_CLKSEL_CON(4), 8, 5, DFLAGS,
			RV1126_CLKGATE_CON(4), 12, GFLAGS),
	GATE(ACLK_CRYPTO, "aclk_crypto", "aclk_pdcrypto", 0,
			RV1126_CLKGATE_CON(3), 2, GFLAGS),
	GATE(HCLK_CRYPTO, "hclk_crypto", "hclk_pdcrypto", 0,
			RV1126_CLKGATE_CON(3), 3, GFLAGS),
	COMPOSITE(CLK_CRYPTO_CORE, "aclk_crypto_core", mux_gpll_cpll_p, 0,
			RV1126_CLKSEL_CON(7), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RV1126_CLKGATE_CON(3), 4, GFLAGS),
	COMPOSITE(CLK_CRYPTO_PKA, "aclk_crypto_pka", mux_gpll_cpll_p, 0,
			RV1126_CLKSEL_CON(7), 15, 1, MFLAGS, 8, 5, DFLAGS,
			RV1126_CLKGATE_CON(3), 5, GFLAGS),

	/*
	 * Clock-Architecture Diagram 6
	 */
	/* PD_AUDIO */
	COMPOSITE_NOMUX(HCLK_PDAUDIO, "hclk_pdaudio", "gpll", 0,
			RV1126_CLKSEL_CON(26), 0, 5, DFLAGS,
			RV1126_CLKGATE_CON(9), 0, GFLAGS),

	GATE(HCLK_I2S0, "hclk_i2s0", "hclk_pdaudio", 0,
			RV1126_CLKGATE_CON(9), 4, GFLAGS),
	COMPOSITE(MCLK_I2S0_TX_DIV, "mclk_i2s0_tx_div", mux_cpll_gpll_p, 0,
			RV1126_CLKSEL_CON(27), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RV1126_CLKGATE_CON(9), 5, GFLAGS),
	COMPOSITE_FRACMUX(MCLK_I2S0_TX_FRACDIV, "mclk_i2s0_tx_fracdiv", "mclk_i2s0_tx_div", CLK_SET_RATE_PARENT,
			RV1126_CLKSEL_CON(28), 0,
			RV1126_CLKGATE_CON(9), 6, GFLAGS,
			&rv1126_i2s0_tx_fracmux),
	GATE(MCLK_I2S0_TX, "mclk_i2s0_tx", "mclk_i2s0_tx_mux", 0,
			RV1126_CLKGATE_CON(9), 9, GFLAGS),
	COMPOSITE(MCLK_I2S0_RX_DIV, "mclk_i2s0_rx_div", mux_cpll_gpll_p, 0,
			RV1126_CLKSEL_CON(27), 15, 1, MFLAGS, 8, 7, DFLAGS,
			RV1126_CLKGATE_CON(9), 7, GFLAGS),
	COMPOSITE_FRACMUX(MCLK_I2S0_RX_FRACDIV, "mclk_i2s0_rx_fracdiv", "mclk_i2s0_rx_div", CLK_SET_RATE_PARENT,
			RV1126_CLKSEL_CON(29), 0,
			RV1126_CLKGATE_CON(9), 8, GFLAGS,
			&rv1126_i2s0_rx_fracmux),
	GATE(MCLK_I2S0_RX, "mclk_i2s0_rx", "mclk_i2s0_rx_mux", 0,
			RV1126_CLKGATE_CON(9), 10, GFLAGS),
	COMPOSITE_NODIV(MCLK_I2S0_TX_OUT2IO, "mclk_i2s0_tx_out2io", mux_i2s0_tx_out2io_p, CLK_SET_RATE_PARENT,
			RV1126_CLKSEL_CON(30), 6, 1, MFLAGS,
			RV1126_CLKGATE_CON(9), 13, GFLAGS),
	COMPOSITE_NODIV(MCLK_I2S0_RX_OUT2IO, "mclk_i2s0_rx_out2io", mux_i2s0_rx_out2io_p, CLK_SET_RATE_PARENT,
			RV1126_CLKSEL_CON(30), 8, 1, MFLAGS,
			RV1126_CLKGATE_CON(9), 14, GFLAGS),

	GATE(HCLK_I2S1, "hclk_i2s1", "hclk_pdaudio", 0,
			RV1126_CLKGATE_CON(10), 0, GFLAGS),
	COMPOSITE(MCLK_I2S1_DIV, "mclk_i2s1_div", mux_cpll_gpll_p, 0,
			RV1126_CLKSEL_CON(31), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RV1126_CLKGATE_CON(10), 1, GFLAGS),
	COMPOSITE_FRACMUX(MCLK_I2S1_FRACDIV, "mclk_i2s1_fracdiv", "mclk_i2s1_div", CLK_SET_RATE_PARENT,
			RV1126_CLKSEL_CON(32), 0,
			RV1126_CLKGATE_CON(10), 2, GFLAGS,
			&rv1126_i2s1_fracmux),
	GATE(MCLK_I2S1, "mclk_i2s1", "mclk_i2s1_mux", 0,
			RV1126_CLKGATE_CON(10), 3, GFLAGS),
	COMPOSITE_NODIV(MCLK_I2S1_OUT2IO, "mclk_i2s1_out2io", mux_i2s1_out2io_p, CLK_SET_RATE_PARENT,
			RV1126_CLKSEL_CON(31), 12, 1, MFLAGS,
			RV1126_CLKGATE_CON(10), 4, GFLAGS),
	GATE(HCLK_I2S2, "hclk_i2s2", "hclk_pdaudio", 0,
			RV1126_CLKGATE_CON(10), 5, GFLAGS),
	COMPOSITE(MCLK_I2S2_DIV, "mclk_i2s2_div", mux_cpll_gpll_p, 0,
			RV1126_CLKSEL_CON(33), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RV1126_CLKGATE_CON(10), 6, GFLAGS),
	COMPOSITE_FRACMUX(MCLK_I2S2_FRACDIV, "mclk_i2s2_fracdiv", "mclk_i2s2_div", CLK_SET_RATE_PARENT,
			RV1126_CLKSEL_CON(34), 0,
			RV1126_CLKGATE_CON(10), 7, GFLAGS,
			&rv1126_i2s2_fracmux),
	GATE(MCLK_I2S2, "mclk_i2s2", "mclk_i2s2_mux", 0,
			RV1126_CLKGATE_CON(10), 8, GFLAGS),
	COMPOSITE_NODIV(MCLK_I2S2_OUT2IO, "mclk_i2s2_out2io", mux_i2s2_out2io_p, CLK_SET_RATE_PARENT,
			RV1126_CLKSEL_CON(33), 10, 1, MFLAGS,
			RV1126_CLKGATE_CON(10), 9, GFLAGS),

	GATE(HCLK_PDM, "hclk_pdm", "hclk_pdaudio", 0,
			RV1126_CLKGATE_CON(10), 10, GFLAGS),
	COMPOSITE(MCLK_PDM, "mclk_pdm", mux_gpll_cpll_xin24m_p, 0,
			RV1126_CLKSEL_CON(35), 8, 2, MFLAGS, 0, 7, DFLAGS,
			RV1126_CLKGATE_CON(10), 11, GFLAGS),

	GATE(HCLK_AUDPWM, "hclk_audpwm", "hclk_pdaudio", 0,
			RV1126_CLKGATE_CON(10), 12, GFLAGS),
	COMPOSITE(SCLK_ADUPWM_DIV, "sclk_audpwm_div", mux_gpll_cpll_p, 0,
			RV1126_CLKSEL_CON(36), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RV1126_CLKGATE_CON(10), 13, GFLAGS),
	COMPOSITE_FRACMUX(SCLK_AUDPWM_FRACDIV, "sclk_audpwm_fracdiv", "sclk_audpwm_div", CLK_SET_RATE_PARENT,
			RV1126_CLKSEL_CON(37), 0,
			RV1126_CLKGATE_CON(10), 14, GFLAGS,
			&rv1126_audpwm_fracmux),
	GATE(SCLK_AUDPWM, "sclk_audpwm", "mclk_audpwm_mux", 0,
			RV1126_CLKGATE_CON(10), 15, GFLAGS),

	GATE(PCLK_ACDCDIG, "pclk_acdcdig", "hclk_pdaudio", 0,
			RV1126_CLKGATE_CON(11), 0, GFLAGS),
	GATE(CLK_ACDCDIG_ADC, "clk_acdcdig_adc", "mclk_i2s0_rx", 0,
			RV1126_CLKGATE_CON(11), 2, GFLAGS),
	GATE(CLK_ACDCDIG_DAC, "clk_acdcdig_dac", "mclk_i2s0_tx", 0,
			RV1126_CLKGATE_CON(11), 3, GFLAGS),
	COMPOSITE(CLK_ACDCDIG_I2C, "clk_acdcdig_i2c", mux_gpll_xin24m_p, 0,
			RV1126_CLKSEL_CON(72), 8, 1, MFLAGS, 0, 7, DFLAGS,
			RV1126_CLKGATE_CON(11), 1, GFLAGS),

	/*
	 * Clock-Architecture Diagram 7
	 */
	/* PD_VEPU */
	COMPOSITE(ACLK_PDVEPU, "aclk_pdvepu", mux_cpll_hpll_gpll_p, 0,
			RV1126_CLKSEL_CON(40), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RV1126_CLKGATE_CON(12), 0, GFLAGS),
	COMPOSITE_NOMUX(HCLK_PDVEPU, "hclk_pdvepu", "aclk_pdvepu", 0,
			RV1126_CLKSEL_CON(41), 0, 5, DFLAGS,
			RV1126_CLKGATE_CON(12), 2, GFLAGS),
	GATE(ACLK_VENC, "aclk_venc", "aclk_pdvepu", 0,
			RV1126_CLKGATE_CON(12), 5, GFLAGS),
	GATE(HCLK_VENC, "hclk_venc", "hclk_pdvepu", 0,
			RV1126_CLKGATE_CON(12), 6, GFLAGS),
	COMPOSITE(CLK_VENC_CORE, "clk_venc_core", mux_cpll_gpll_hpll_p, 0,
			RV1126_CLKSEL_CON(40), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RV1126_CLKGATE_CON(12), 1, GFLAGS),

	/*
	 * Clock-Architecture Diagram 8
	 */
	/* PD_VDPU */
#if IS_ENABLED(CONFIG_ROCKCHIP_MPP_VDPU2) || IS_ENABLED(CONFIG_ROCKCHIP_MPP_RKVDEC)
	COMPOSITE(ACLK_PDVDEC, "aclk_pdvdec", mux_cpll_hpll_gpll_p, CLK_IS_CRITICAL,
			RV1126_CLKSEL_CON(42), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RV1126_CLKGATE_CON(13), 0, GFLAGS),
	COMPOSITE_NOMUX(HCLK_PDVDEC, "hclk_pdvdec", "aclk_pdvdec", CLK_IS_CRITICAL,
			RV1126_CLKSEL_CON(41), 8, 5, DFLAGS,
			RV1126_CLKGATE_CON(13), 4, GFLAGS),
	GATE(0, "aclk_pdvdec_niu", "aclk_pdvdec", CLK_IS_CRITICAL,
			RV1126_CLKGATE_CON(13), 5, GFLAGS),
	GATE(0, "hclk_pdvdec_niu", "hclk_pdvdec", CLK_IS_CRITICAL,
			RV1126_CLKGATE_CON(13), 6, GFLAGS),
	COMPOSITE(ACLK_PDJPEG, "aclk_pdjpeg", mux_cpll_hpll_gpll_p, CLK_IS_CRITICAL,
			RV1126_CLKSEL_CON(44), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RV1126_CLKGATE_CON(13), 9, GFLAGS),
	COMPOSITE_NOMUX(HCLK_PDJPEG, "hclk_pdjpeg", "aclk_pdjpeg", CLK_IS_CRITICAL,
			RV1126_CLKSEL_CON(44), 8, 5, DFLAGS,
			RV1126_CLKGATE_CON(13), 10, GFLAGS),
	GATE(0, "aclk_pdjpeg_niu", "aclk_pdjpeg", CLK_IS_CRITICAL,
			RV1126_CLKGATE_CON(13), 11, GFLAGS),
	GATE(0, "hclk_pdjpeg_niu", "hclk_pdjpeg", CLK_IS_CRITICAL,
			RV1126_CLKGATE_CON(13), 12, GFLAGS),
#else
	COMPOSITE(ACLK_PDVDEC, "aclk_pdvdec", mux_cpll_hpll_gpll_p, 0,
			RV1126_CLKSEL_CON(42), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RV1126_CLKGATE_CON(13), 0, GFLAGS),
	COMPOSITE_NOMUX(HCLK_PDVDEC, "hclk_pdvdec", "aclk_pdvdec", 0,
			RV1126_CLKSEL_CON(41), 8, 5, DFLAGS,
			RV1126_CLKGATE_CON(13), 4, GFLAGS),
	GATE(0, "aclk_pdvdec_niu", "aclk_pdvdec", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(13), 5, GFLAGS),
	GATE(0, "hclk_pdvdec_niu", "hclk_pdvdec", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(13), 6, GFLAGS),
	COMPOSITE(ACLK_PDJPEG, "aclk_pdjpeg", mux_cpll_hpll_gpll_p, 0,
			RV1126_CLKSEL_CON(44), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RV1126_CLKGATE_CON(13), 9, GFLAGS),
	COMPOSITE_NOMUX(HCLK_PDJPEG, "hclk_pdjpeg", "aclk_pdjpeg", 0,
			RV1126_CLKSEL_CON(44), 8, 5, DFLAGS,
			RV1126_CLKGATE_CON(13), 10, GFLAGS),
	GATE(0, "aclk_pdjpeg_niu", "aclk_pdjpeg", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(13), 11, GFLAGS),
	GATE(0, "hclk_pdjpeg_niu", "hclk_pdjpeg", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(13), 12, GFLAGS),
#endif
	GATE(ACLK_VDEC, "aclk_vdec", "aclk_pdvdec", 0,
			RV1126_CLKGATE_CON(13), 7, GFLAGS),
	GATE(HCLK_VDEC, "hclk_vdec", "hclk_pdvdec", 0,
			RV1126_CLKGATE_CON(13), 8, GFLAGS),
	COMPOSITE(CLK_VDEC_CORE, "clk_vdec_core", mux_cpll_hpll_gpll_p, 0,
			RV1126_CLKSEL_CON(42), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RV1126_CLKGATE_CON(13), 1, GFLAGS),
	COMPOSITE(CLK_VDEC_CA, "clk_vdec_ca", mux_cpll_hpll_gpll_p, 0,
			RV1126_CLKSEL_CON(43), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RV1126_CLKGATE_CON(13), 2, GFLAGS),
	COMPOSITE(CLK_VDEC_HEVC_CA, "clk_vdec_hevc_ca", mux_cpll_hpll_gpll_p, 0,
			RV1126_CLKSEL_CON(43), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RV1126_CLKGATE_CON(13), 3, GFLAGS),
	GATE(ACLK_JPEG, "aclk_jpeg", "aclk_pdjpeg", 0,
			RV1126_CLKGATE_CON(13), 13, GFLAGS),
	GATE(HCLK_JPEG, "hclk_jpeg", "hclk_pdjpeg", 0,
			RV1126_CLKGATE_CON(13), 14, GFLAGS),

	/*
	 * Clock-Architecture Diagram 9
	 */
	/* PD_VO */
	COMPOSITE(ACLK_PDVO, "aclk_pdvo", mux_gpll_cpll_p, 0,
			RV1126_CLKSEL_CON(45), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RV1126_CLKGATE_CON(14), 0, GFLAGS),
	COMPOSITE_NOMUX(HCLK_PDVO, "hclk_pdvo", "aclk_pdvo", 0,
			RV1126_CLKSEL_CON(45), 8, 5, DFLAGS,
			RV1126_CLKGATE_CON(14), 1, GFLAGS),
	COMPOSITE_NOMUX(PCLK_PDVO, "pclk_pdvo", "aclk_pdvo", 0,
			RV1126_CLKSEL_CON(46), 8, 5, DFLAGS,
			RV1126_CLKGATE_CON(14), 2, GFLAGS),
	GATE(ACLK_RGA, "aclk_rga", "aclk_pdvo", 0,
			RV1126_CLKGATE_CON(14), 6, GFLAGS),
	GATE(HCLK_RGA, "hclk_rga", "hclk_pdvo", 0,
			RV1126_CLKGATE_CON(14), 7, GFLAGS),
	COMPOSITE(CLK_RGA_CORE, "clk_rga_core", mux_gpll_cpll_p, 0,
			RV1126_CLKSEL_CON(46), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RV1126_CLKGATE_CON(14), 8, GFLAGS),
	GATE(ACLK_VOP, "aclk_vop", "aclk_pdvo", 0,
			RV1126_CLKGATE_CON(14), 9, GFLAGS),
	GATE(HCLK_VOP, "hclk_vop", "hclk_pdvo", 0,
			RV1126_CLKGATE_CON(14), 10, GFLAGS),
	COMPOSITE(DCLK_VOP_DIV, "dclk_vop_div", mux_gpll_cpll_p, 0,
			RV1126_CLKSEL_CON(47), 8, 1, MFLAGS, 0, 8, DFLAGS,
			RV1126_CLKGATE_CON(14), 11, GFLAGS),
	COMPOSITE_FRACMUX(DCLK_VOP_FRACDIV, "dclk_vop_fracdiv", "dclk_vop_div", CLK_SET_RATE_PARENT,
			RV1126_CLKSEL_CON(48), 0,
			RV1126_CLKGATE_CON(14), 12, GFLAGS,
			&rv1126_dclk_vop_fracmux),
	GATE(DCLK_VOP, "dclk_vop", "dclk_vop_mux", 0,
			RV1126_CLKGATE_CON(14), 13, GFLAGS),
	GATE(PCLK_DSIHOST, "pclk_dsihost", "pclk_pdvo", 0,
			RV1126_CLKGATE_CON(14), 14, GFLAGS),
	GATE(ACLK_IEP, "aclk_iep", "aclk_pdvo", 0,
			RV1126_CLKGATE_CON(12), 7, GFLAGS),
	GATE(HCLK_IEP, "hclk_iep", "hclk_pdvo", 0,
			RV1126_CLKGATE_CON(12), 8, GFLAGS),
	COMPOSITE(CLK_IEP_CORE, "clk_iep_core", mux_gpll_cpll_p, 0,
			RV1126_CLKSEL_CON(54), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RV1126_CLKGATE_CON(12), 9, GFLAGS),

	/*
	 * Clock-Architecture Diagram 10
	 */
	/* PD_VI */
	COMPOSITE(ACLK_PDVI_DIV, "aclk_pdvi_div", mux_cpll_gpll_hpll_p, 0,
			RV1126_CLKSEL_CON(49), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RV1126_CLKGATE_CON(15), 0, GFLAGS),
	COMPOSITE_HALFDIV_OFFSET(ACLK_PDVI_NP5, "aclk_pdvi_np5", mux_cpll_gpll_hpll_p, 0,
			RV1126_CLKSEL_CON(49), 6, 2, MFLAGS,
			RV1126_CLKSEL_CON(76), 0, 5, DFLAGS,
			RV1126_CLKGATE_CON(16), 13, GFLAGS),
	MUX(ACLK_PDVI, "aclk_pdvi", mux_aclk_pdvi_p, CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
			RV1126_CLKSEL_CON(76), 5, 1, MFLAGS),
	COMPOSITE_NOMUX(HCLK_PDVI, "hclk_pdvi", "aclk_pdvi", 0,
			RV1126_CLKSEL_CON(49), 8, 5, DFLAGS,
			RV1126_CLKGATE_CON(15), 1, GFLAGS),
	COMPOSITE_NOMUX(PCLK_PDVI, "pclk_pdvi", "aclk_pdvi", 0,
			RV1126_CLKSEL_CON(50), 8, 5, DFLAGS,
			RV1126_CLKGATE_CON(15), 2, GFLAGS),
	GATE(ACLK_ISP, "aclk_isp", "aclk_pdvi", 0,
			RV1126_CLKGATE_CON(15), 6, GFLAGS),
	GATE(HCLK_ISP, "hclk_isp", "hclk_pdvi", 0,
			RV1126_CLKGATE_CON(15), 7, GFLAGS),
	COMPOSITE(CLK_ISP_DIV, "clk_isp_div", mux_gpll_cpll_hpll_p, 0,
			RV1126_CLKSEL_CON(50), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RV1126_CLKGATE_CON(15), 8, GFLAGS),
	COMPOSITE_HALFDIV_OFFSET(CLK_ISP_NP5, "clk_isp_np5", mux_gpll_cpll_hpll_p, 0,
			RV1126_CLKSEL_CON(50), 6, 2, MFLAGS,
			RV1126_CLKSEL_CON(76), 8, 5, DFLAGS,
			RV1126_CLKGATE_CON(16), 14, GFLAGS),
	MUX(CLK_ISP, "clk_isp", mux_clk_isp_p, CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
			RV1126_CLKSEL_CON(76), 13, 1, MFLAGS),
	GATE(ACLK_CIF, "aclk_cif", "aclk_pdvi", 0,
			RV1126_CLKGATE_CON(15), 9, GFLAGS),
	GATE(HCLK_CIF, "hclk_cif", "hclk_pdvi", 0,
			RV1126_CLKGATE_CON(15), 10, GFLAGS),
	COMPOSITE(DCLK_CIF, "dclk_cif", mux_gpll_cpll_hpll_p, 0,
			RV1126_CLKSEL_CON(51), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RV1126_CLKGATE_CON(15), 11, GFLAGS),
	COMPOSITE(CLK_CIF_OUT_DIV, "clk_cif_out2io_div", mux_gpll_usb480m_p, 0,
			RV1126_CLKSEL_CON(51), 15, 1, MFLAGS, 8, 6, DFLAGS,
			RV1126_CLKGATE_CON(15), 12, GFLAGS),
	COMPOSITE_FRACMUX(CLK_CIF_OUT_FRACDIV, "clk_cif_out2io_fracdiv", "clk_cif_out2io_div", CLK_SET_RATE_PARENT,
			RV1126_CLKSEL_CON(52), 0,
			RV1126_CLKGATE_CON(15), 13, GFLAGS,
			&rv1126_cif_out2io_fracmux),
	GATE(CLK_CIF_OUT, "clk_cif_out2io", "clk_cif_out2io_mux", 0,
			RV1126_CLKGATE_CON(15), 14, GFLAGS),
	COMPOSITE(CLK_MIPICSI_OUT_DIV, "clk_mipicsi_out2io_div", mux_gpll_usb480m_p, 0,
			RV1126_CLKSEL_CON(73), 8, 1, MFLAGS, 0, 5, DFLAGS,
			RV1126_CLKGATE_CON(23), 5, GFLAGS),
	COMPOSITE_FRACMUX(CLK_MIPICSI_OUT_FRACDIV, "clk_mipicsi_out2io_fracdiv", "clk_mipicsi_out2io_div", CLK_SET_RATE_PARENT,
			RV1126_CLKSEL_CON(74), 0,
			RV1126_CLKGATE_CON(23), 6, GFLAGS,
			&rv1126_mipicsi_out2io_fracmux),
	GATE(CLK_MIPICSI_OUT, "clk_mipicsi_out2io", "clk_mipicsi_out2io_mux", 0,
			RV1126_CLKGATE_CON(23), 7, GFLAGS),
	GATE(PCLK_CSIHOST, "pclk_csihost", "pclk_pdvi", 0,
			RV1126_CLKGATE_CON(15), 15, GFLAGS),
	GATE(ACLK_CIFLITE, "aclk_ciflite", "aclk_pdvi", 0,
			RV1126_CLKGATE_CON(16), 10, GFLAGS),
	GATE(HCLK_CIFLITE, "hclk_ciflite", "hclk_pdvi", 0,
			RV1126_CLKGATE_CON(16), 11, GFLAGS),
	COMPOSITE(DCLK_CIFLITE, "dclk_ciflite", mux_gpll_cpll_hpll_p, 0,
			RV1126_CLKSEL_CON(54), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RV1126_CLKGATE_CON(16), 12, GFLAGS),

	/*
	 * Clock-Architecture Diagram 11
	 */
	/* PD_ISPP */
	COMPOSITE(ACLK_PDISPP_DIV, "aclk_pdispp_div", mux_cpll_gpll_hpll_p, 0,
			RV1126_CLKSEL_CON(68), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RV1126_CLKGATE_CON(16), 0, GFLAGS),
	COMPOSITE_HALFDIV_OFFSET(ACLK_PDISPP_NP5, "aclk_pdispp_np5", mux_cpll_gpll_hpll_p, 0,
			RV1126_CLKSEL_CON(68), 6, 2, MFLAGS,
			RV1126_CLKSEL_CON(77), 0, 5, DFLAGS,
			RV1126_CLKGATE_CON(16), 8, GFLAGS),
	MUX(ACLK_PDISPP, "aclk_pdispp", mux_aclk_pdispp_p, CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
			RV1126_CLKSEL_CON(77), 5, 1, MFLAGS),
	COMPOSITE_NOMUX(HCLK_PDISPP, "hclk_pdispp", "aclk_pdispp", 0,
			RV1126_CLKSEL_CON(69), 8, 5, DFLAGS,
			RV1126_CLKGATE_CON(16), 1, GFLAGS),
	GATE(ACLK_ISPP, "aclk_ispp", "aclk_pdispp", 0,
			RV1126_CLKGATE_CON(16), 4, GFLAGS),
	GATE(HCLK_ISPP, "hclk_ispp", "hclk_pdispp", 0,
			RV1126_CLKGATE_CON(16), 5, GFLAGS),
	COMPOSITE(CLK_ISPP_DIV, "clk_ispp_div", mux_cpll_gpll_hpll_p, 0,
			RV1126_CLKSEL_CON(69), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RV1126_CLKGATE_CON(16), 6, GFLAGS),
	COMPOSITE_HALFDIV_OFFSET(CLK_ISPP_NP5, "clk_ispp_np5", mux_cpll_gpll_hpll_p, 0,
			RV1126_CLKSEL_CON(69), 6, 2, MFLAGS,
			RV1126_CLKSEL_CON(77), 8, 5, DFLAGS,
			RV1126_CLKGATE_CON(16), 7, GFLAGS),
	MUX(CLK_ISPP, "clk_ispp", mux_clk_ispp_p, CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
			RV1126_CLKSEL_CON(77), 13, 1, MFLAGS),

	/*
	 * Clock-Architecture Diagram 12
	 */
	/* PD_PHP */
	COMPOSITE(ACLK_PDPHP, "aclk_pdphp", mux_gpll_cpll_p, CLK_IS_CRITICAL,
			RV1126_CLKSEL_CON(53), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RV1126_CLKGATE_CON(17), 0, GFLAGS),
	COMPOSITE_NOMUX(HCLK_PDPHP, "hclk_pdphp", "gpll", CLK_IS_CRITICAL,
			RV1126_CLKSEL_CON(53), 8, 5, DFLAGS,
			RV1126_CLKGATE_CON(17), 1, GFLAGS),
	/* PD_SDCARD */
	GATE(HCLK_PDSDMMC, "hclk_pdsdmmc", "hclk_pdphp", 0,
			RV1126_CLKGATE_CON(17), 6, GFLAGS),
	GATE(HCLK_SDMMC, "hclk_sdmmc", "hclk_pdsdmmc", 0,
			RV1126_CLKGATE_CON(18), 4, GFLAGS),
	COMPOSITE(CLK_SDMMC, "clk_sdmmc", mux_gpll_cpll_xin24m_p, 0,
			RV1126_CLKSEL_CON(55), 14, 2, MFLAGS, 0, 8,
			DFLAGS, RV1126_CLKGATE_CON(18), 5, GFLAGS),
	MMC(SCLK_SDMMC_DRV,     "sdmmc_drv",    "clk_sdmmc", RV1126_SDMMC_CON0, 1),
	MMC(SCLK_SDMMC_SAMPLE,  "sdmmc_sample", "clk_sdmmc", RV1126_SDMMC_CON1, 1),

	/* PD_SDIO */
	GATE(HCLK_PDSDIO, "hclk_pdsdio", "hclk_pdphp", 0,
			RV1126_CLKGATE_CON(17), 8, GFLAGS),
	GATE(HCLK_SDIO, "hclk_sdio", "hclk_pdsdio", 0,
			RV1126_CLKGATE_CON(18), 6, GFLAGS),
	COMPOSITE(CLK_SDIO, "clk_sdio", mux_gpll_cpll_xin24m_p, 0,
			RV1126_CLKSEL_CON(56), 14, 2, MFLAGS, 0, 8, DFLAGS,
			RV1126_CLKGATE_CON(18), 7, GFLAGS),
	MMC(SCLK_SDIO_DRV, "sdio_drv", "clk_sdio", RV1126_SDIO_CON0, 1),
	MMC(SCLK_SDIO_SAMPLE, "sdio_sample", "clk_sdio", RV1126_SDIO_CON1, 1),

	/* PD_NVM */
	GATE(HCLK_PDNVM, "hclk_pdnvm", "hclk_pdphp", 0,
			RV1126_CLKGATE_CON(18), 1, GFLAGS),
	GATE(HCLK_EMMC, "hclk_emmc", "hclk_pdnvm", 0,
			RV1126_CLKGATE_CON(18), 8, GFLAGS),
	COMPOSITE(CLK_EMMC, "clk_emmc", mux_gpll_cpll_xin24m_p, 0,
			RV1126_CLKSEL_CON(57), 14, 2, MFLAGS, 0, 8, DFLAGS,
			RV1126_CLKGATE_CON(18), 9, GFLAGS),
	GATE(HCLK_NANDC, "hclk_nandc", "hclk_pdnvm", 0,
			RV1126_CLKGATE_CON(18), 13, GFLAGS),
	COMPOSITE(CLK_NANDC, "clk_nandc", mux_gpll_cpll_p, 0,
			RV1126_CLKSEL_CON(59), 15, 1, MFLAGS, 0, 8, DFLAGS,
			RV1126_CLKGATE_CON(18), 14, GFLAGS),
	GATE(HCLK_SFC, "hclk_sfc", "hclk_pdnvm", 0,
			RV1126_CLKGATE_CON(18), 10, GFLAGS),
	GATE(HCLK_SFCXIP, "hclk_sfcxip", "hclk_pdnvm", 0,
			RV1126_CLKGATE_CON(18), 11, GFLAGS),
	COMPOSITE(SCLK_SFC, "sclk_sfc", mux_cpll_gpll_p, 0,
			RV1126_CLKSEL_CON(58), 15, 1, MFLAGS, 0, 8, DFLAGS,
			RV1126_CLKGATE_CON(18), 12, GFLAGS),
	MMC(SCLK_EMMC_DRV, "emmc_drv", "clk_emmc", RV1126_EMMC_CON0, 1),
	MMC(SCLK_EMMC_SAMPLE, "emmc_sample", "clk_emmc", RV1126_EMMC_CON1, 1),

	/* PD_USB */
	GATE(ACLK_PDUSB, "aclk_pdusb", "aclk_pdphp", 0,
			RV1126_CLKGATE_CON(19), 0, GFLAGS),
	GATE(HCLK_PDUSB, "hclk_pdusb", "hclk_pdphp", 0,
			RV1126_CLKGATE_CON(19), 1, GFLAGS),
	GATE(HCLK_USBHOST, "hclk_usbhost", "hclk_pdusb", 0,
			RV1126_CLKGATE_CON(19), 4, GFLAGS),
	GATE(HCLK_USBHOST_ARB, "hclk_usbhost_arb", "hclk_pdusb", 0,
			RV1126_CLKGATE_CON(19), 5, GFLAGS),
#if IS_ENABLED(CONFIG_USB_EHCI_HCD_PLATFORM) || IS_ENABLED(CONFIG_USB_OHCI_HCD_PLATFORM)
	COMPOSITE(CLK_USBHOST_UTMI_OHCI, "clk_usbhost_utmi_ohci", mux_usb480m_gpll_p, CLK_IS_CRITICAL,
			RV1126_CLKSEL_CON(61), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RV1126_CLKGATE_CON(19), 6, GFLAGS),
#else
	COMPOSITE(CLK_USBHOST_UTMI_OHCI, "clk_usbhost_utmi_ohci", mux_usb480m_gpll_p, 0,
			RV1126_CLKSEL_CON(61), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RV1126_CLKGATE_CON(19), 6, GFLAGS),
#endif
	GATE(ACLK_USBOTG, "aclk_usbotg", "aclk_pdusb", 0,
			RV1126_CLKGATE_CON(19), 7, GFLAGS),
	GATE(CLK_USBOTG_REF, "clk_usbotg_ref", "xin24m", 0,
			RV1126_CLKGATE_CON(19), 8, GFLAGS),
	/* PD_GMAC */
	GATE(ACLK_PDGMAC, "aclk_pdgmac", "aclk_pdphp", 0,
			RV1126_CLKGATE_CON(20), 0, GFLAGS),
	COMPOSITE_NOMUX(PCLK_PDGMAC, "pclk_pdgmac", "aclk_pdgmac", 0,
			RV1126_CLKSEL_CON(63), 8, 5, DFLAGS,
			RV1126_CLKGATE_CON(20), 1, GFLAGS),
	GATE(ACLK_GMAC, "aclk_gmac", "aclk_pdgmac", 0,
			RV1126_CLKGATE_CON(20), 4, GFLAGS),
	GATE(PCLK_GMAC, "pclk_gmac", "pclk_pdgmac", 0,
			RV1126_CLKGATE_CON(20), 5, GFLAGS),

	COMPOSITE(CLK_GMAC_DIV, "clk_gmac_div", mux_cpll_gpll_p, 0,
			RV1126_CLKSEL_CON(63), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RV1126_CLKGATE_CON(20), 6, GFLAGS),
	GATE(CLK_GMAC_RGMII_M0, "clk_gmac_rgmii_m0", "clk_gmac_rgmii_clkin_m0", 0,
			RV1126_CLKGATE_CON(20), 12, GFLAGS),
	MUX(CLK_GMAC_SRC_M0, "clk_gmac_src_m0", clk_gmac_src_m0_p, CLK_SET_RATE_PARENT,
			RV1126_GMAC_CON, 0, 1, MFLAGS),
	GATE(CLK_GMAC_RGMII_M1, "clk_gmac_rgmii_m1", "clk_gmac_rgmii_clkin_m1", 0,
			RV1126_CLKGATE_CON(20), 13, GFLAGS),
	MUX(CLK_GMAC_SRC_M1, "clk_gmac_src_m1", clk_gmac_src_m1_p, CLK_SET_RATE_PARENT,
			RV1126_GMAC_CON, 5, 1, MFLAGS),
	MUXGRF(CLK_GMAC_SRC, "clk_gmac_src", mux_clk_gmac_src_p, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			RV1126_GRF_IOFUNC_CON1, 12, 1, MFLAGS),

	GATE(CLK_GMAC_REF, "clk_gmac_ref", "clk_gmac_src", 0,
			RV1126_CLKGATE_CON(20), 7, GFLAGS),

	GATE(CLK_GMAC_TX_SRC, "clk_gmac_tx_src", "clk_gmac_src", 0,
			RV1126_CLKGATE_CON(20), 9, GFLAGS),
	FACTOR(CLK_GMAC_TX_DIV5, "clk_gmac_tx_div5", "clk_gmac_tx_src", 0, 1, 5),
	FACTOR(CLK_GMAC_TX_DIV50, "clk_gmac_tx_div50", "clk_gmac_tx_src", 0, 1, 50),
	MUXTBL(RGMII_MODE_CLK, "rgmii_mode_clk", mux_rgmii_clk_p, CLK_SET_RATE_PARENT,
			RV1126_GMAC_CON, 2, 2, MFLAGS, rgmii_mux_idx),
	GATE(CLK_GMAC_RX_SRC, "clk_gmac_rx_src", "clk_gmac_src", 0,
			RV1126_CLKGATE_CON(20), 8, GFLAGS),
	FACTOR(CLK_GMAC_RX_DIV2, "clk_gmac_rx_div2", "clk_gmac_rx_src", 0, 1, 2),
	FACTOR(CLK_GMAC_RX_DIV20, "clk_gmac_rx_div20", "clk_gmac_rx_src", 0, 1, 20),
	MUX(RMII_MODE_CLK, "rmii_mode_clk", mux_rmii_clk_p, CLK_SET_RATE_PARENT,
			RV1126_GMAC_CON, 1, 1, MFLAGS),
	MUX(CLK_GMAC_TX_RX, "clk_gmac_tx_rx", mux_gmac_tx_rx_p, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			RV1126_GMAC_CON, 4, 1, MFLAGS),

	GATE(CLK_GMAC_PTPREF, "clk_gmac_ptpref", "xin24m", 0,
			RV1126_CLKGATE_CON(20), 10, GFLAGS),
	COMPOSITE(CLK_GMAC_ETHERNET_OUT, "clk_gmac_ethernet_out2io", mux_cpll_gpll_p, 0,
			RV1126_CLKSEL_CON(61), 15, 1, MFLAGS, 8, 5, DFLAGS,
			RV1126_CLKGATE_CON(20), 11, GFLAGS),


	/*
	 * Clock-Architecture Diagram 14
	 */
	/* PD_NPU */
	COMPOSITE(ACLK_PDNPU_DIV, "aclk_pdnpu_div", mux_gpll_cpll_apll_hpll_p, 0,
			RV1126_CLKSEL_CON(65), 8, 2, MFLAGS, 0, 4, DFLAGS,
			RV1126_CLKGATE_CON(22), 0, GFLAGS),
	COMPOSITE_HALFDIV(ACLK_PDNPU_NP5, "aclk_pdnpu_np5", mux_gpll_cpll_apll_hpll_p, 0,
			RV1126_CLKSEL_CON(65), 8, 2, MFLAGS, 4, 4, DFLAGS,
			RV1126_CLKGATE_CON(22), 1, GFLAGS),
	MUX(ACLK_PDNPU, "aclk_pdnpu", mux_aclk_pdnpu_p, CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
			RV1126_CLKSEL_CON(65), 12, 1, MFLAGS),
	COMPOSITE_NOMUX(HCLK_PDNPU, "hclk_pdnpu", "gpll", 0,
			RV1126_CLKSEL_CON(66), 8, 4, DFLAGS,
			RV1126_CLKGATE_CON(22), 2, GFLAGS),
	COMPOSITE_NOMUX(PCLK_PDNPU, "pclk_pdnpu", "hclk_pdnpu", 0,
			RV1126_CLKSEL_CON(66), 0, 5, DFLAGS,
			RV1126_CLKGATE_CON(22), 3, GFLAGS),
	GATE(ACLK_NPU, "aclk_npu", "aclk_pdnpu", 0,
			RV1126_CLKGATE_CON(22), 7, GFLAGS),
	GATE(HCLK_NPU, "hclk_npu", "hclk_pdnpu", 0,
			RV1126_CLKGATE_CON(22), 8, GFLAGS),
	COMPOSITE(CLK_NPU_DIV, "clk_npu_div", mux_gpll_cpll_apll_hpll_p, 0,
			RV1126_CLKSEL_CON(67), 8, 2, MFLAGS, 0, 4, DFLAGS,
			RV1126_CLKGATE_CON(22), 9, GFLAGS),
	COMPOSITE_HALFDIV(CLK_NPU_NP5, "clk_npu_np5", mux_gpll_cpll_apll_hpll_p, 0,
			RV1126_CLKSEL_CON(67), 8, 2, MFLAGS, 4, 4, DFLAGS,
			RV1126_CLKGATE_CON(22), 10, GFLAGS),
	MUX(CLK_CORE_NPU, "clk_core_npu", mux_clk_npu_p, CLK_SET_RATE_PARENT | CLK_OPS_PARENT_ENABLE,
			RV1126_CLKSEL_CON(67), 12, 1, MFLAGS),
	GATE(CLK_CORE_NPUPVTM, "clk_core_npupvtm", "clk_core_npu", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(22), 14, GFLAGS),
	GATE(CLK_NPUPVTM, "clk_npupvtm", "xin24m", 0,
			RV1126_CLKGATE_CON(22), 13, GFLAGS),
	GATE(PCLK_NPUPVTM, "pclk_npupvtm", "pclk_pdnpu", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(22), 12, GFLAGS),

	/*
	 * Clock-Architecture Diagram 15
	 */
	GATE(PCLK_PDTOP, "pclk_pdtop", "pclk_pdbus", CLK_IS_CRITICAL,
			RV1126_CLKGATE_CON(23), 8, GFLAGS),
	GATE(PCLK_DSIPHY, "pclk_dsiphy", "pclk_pdtop", 0,
			RV1126_CLKGATE_CON(23), 4, GFLAGS),
	GATE(PCLK_CSIPHY0, "pclk_csiphy0", "pclk_pdtop", 0,
			RV1126_CLKGATE_CON(23), 2, GFLAGS),
	GATE(PCLK_CSIPHY1, "pclk_csiphy1", "pclk_pdtop", 0,
			RV1126_CLKGATE_CON(23), 3, GFLAGS),
	GATE(PCLK_USBPHY_HOST, "pclk_usbphy_host", "pclk_pdtop", 0,
			RV1126_CLKGATE_CON(19), 13, GFLAGS),
	GATE(PCLK_USBPHY_OTG, "pclk_usbphy_otg", "pclk_pdtop", 0,
			RV1126_CLKGATE_CON(19), 12, GFLAGS),

#ifndef CONFIG_ROCKCHIP_LOW_PERFORMANCE
	/*
	 * Clock-Architecture Diagram 3
	 */
	/* PD_CORE */
	COMPOSITE_NOMUX(0, "aclk_core", "armclk", CLK_IS_CRITICAL,
			RV1126_CLKSEL_CON(1), 4, 4, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RV1126_CLKGATE_CON(0), 2, GFLAGS),
	GATE(0, "pclk_dbg_daplite", "pclk_dbg", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(0), 5, GFLAGS),
	GATE(0, "clk_a7_jtag", "clk_jtag_ori", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(0), 9, GFLAGS),
	GATE(0, "aclk_core_niu", "aclk_core", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(0), 3, GFLAGS),
	GATE(0, "pclk_dbg_niu", "pclk_dbg", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(0), 4, GFLAGS),
	/*
	 * Clock-Architecture Diagram 4
	 */
	/* PD_BUS */
	GATE(0, "aclk_pdbus_hold_niu1", "aclk_pdbus", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(2), 10, GFLAGS),
	GATE(0, "aclk_pdbus_niu1", "aclk_pdbus", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(2), 3, GFLAGS),
	GATE(0, "hclk_pdbus_niu1", "hclk_pdbus", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(2), 4, GFLAGS),
	GATE(0, "pclk_pdbus_niu1", "pclk_pdbus", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(2), 5, GFLAGS),
	GATE(0, "aclk_pdbus_niu2", "aclk_pdbus", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(2), 6, GFLAGS),
	GATE(0, "hclk_pdbus_niu2", "hclk_pdbus", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(2), 7, GFLAGS),
	GATE(0, "aclk_pdbus_niu3", "aclk_pdbus", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(2), 8, GFLAGS),
	GATE(0, "hclk_pdbus_niu3", "hclk_pdbus", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(2), 9, GFLAGS),
	GATE(0, "pclk_grf", "pclk_pdbus", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(6), 15, GFLAGS),
	GATE(0, "pclk_sgrf", "pclk_pdbus", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(8), 4, GFLAGS),
	GATE(0, "aclk_sysram", "hclk_pdbus", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(3), 9, GFLAGS),
	GATE(0, "pclk_intmux", "pclk_pdbus", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(7), 14, GFLAGS),

	/*
	 * Clock-Architecture Diagram 5
	 */
	/* PD_CRYPTO */
	GATE(0, "aclk_pdcrypto_niu", "aclk_pdcrypto", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(4), 13, GFLAGS),
	GATE(0, "hclk_pdcrypto_niu", "hclk_pdcrypto", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(4), 14, GFLAGS),

	/*
	 * Clock-Architecture Diagram 6
	 */
	/* PD_AUDIO */
	GATE(0, "hclk_pdaudio_niu", "hclk_pdaudio", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(9), 2, GFLAGS),
	GATE(0, "pclk_pdaudio_niu", "hclk_pdaudio", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(9), 3, GFLAGS),

	/*
	 * Clock-Architecture Diagram 7
	 */
	/* PD_VEPU */
	GATE(0, "aclk_pdvepu_niu", "aclk_pdvepu", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(12), 3, GFLAGS),
	GATE(0, "hclk_pdvepu_niu", "hclk_pdvepu", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(12), 4, GFLAGS),

	/*
	 * Clock-Architecture Diagram 9
	 */
	/* PD_VO */
	GATE(0, "aclk_pdvo_niu", "aclk_pdvo", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(14), 3, GFLAGS),
	GATE(0, "hclk_pdvo_niu", "hclk_pdvo", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(14), 4, GFLAGS),
	GATE(0, "pclk_pdvo_niu", "pclk_pdvo", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(14), 5, GFLAGS),

	/*
	 * Clock-Architecture Diagram 10
	 */
	/* PD_VI */
	GATE(0, "aclk_pdvi_niu", "aclk_pdvi", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(15), 3, GFLAGS),
	GATE(0, "hclk_pdvi_niu", "hclk_pdvi", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(15), 4, GFLAGS),
	GATE(0, "pclk_pdvi_niu", "pclk_pdvi", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(15), 5, GFLAGS),
	/*
	 * Clock-Architecture Diagram 11
	 */
	/* PD_ISPP */
	GATE(0, "aclk_pdispp_niu", "aclk_pdispp", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(16), 2, GFLAGS),
	GATE(0, "hclk_pdispp_niu", "hclk_pdispp", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(16), 3, GFLAGS),

	/*
	 * Clock-Architecture Diagram 12
	 */
	/* PD_PHP */
	GATE(0, "aclk_pdphpmid", "aclk_pdphp", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(17), 2, GFLAGS),
	GATE(0, "hclk_pdphpmid", "hclk_pdphp", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(17), 3, GFLAGS),
	GATE(0, "aclk_pdphpmid_niu", "aclk_pdphpmid", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(17), 4, GFLAGS),
	GATE(0, "hclk_pdphpmid_niu", "hclk_pdphpmid", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(17), 5, GFLAGS),

	/* PD_SDCARD */
	GATE(0, "hclk_pdsdmmc_niu", "hclk_pdsdmmc", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(17), 7, GFLAGS),

	/* PD_SDIO */
	GATE(0, "hclk_pdsdio_niu", "hclk_pdsdio", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(17), 9, GFLAGS),

	/* PD_NVM */
	GATE(0, "hclk_pdnvm_niu", "hclk_pdnvm", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(18), 3, GFLAGS),

	/* PD_USB */
	GATE(0, "aclk_pdusb_niu", "aclk_pdusb", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(19), 2, GFLAGS),
	GATE(0, "hclk_pdusb_niu", "hclk_pdusb", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(19), 3, GFLAGS),

	/* PD_GMAC */
	GATE(0, "aclk_pdgmac_niu", "aclk_pdgmac", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(20), 2, GFLAGS),
	GATE(0, "pclk_pdgmac_niu", "pclk_pdgmac", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(20), 3, GFLAGS),

	/*
	 * Clock-Architecture Diagram 13
	 */
	/* PD_DDR */
	COMPOSITE_NOMUX(0, "pclk_pdddr_pre", "gpll", CLK_IS_CRITICAL,
			RV1126_CLKSEL_CON(64), 0, 5, DFLAGS,
			RV1126_CLKGATE_CON(21), 0, GFLAGS),
	GATE(PCLK_PDDDR, "pclk_pdddr", "pclk_pdddr_pre", CLK_IS_CRITICAL,
			RV1126_CLKGATE_CON(21), 15, GFLAGS),
	GATE(0, "pclk_ddr_msch", "pclk_pdddr", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(21), 6, GFLAGS),
	COMPOSITE_NOGATE(SCLK_DDRCLK, "sclk_ddrc", mux_dpll_gpll_p, CLK_IS_CRITICAL,
			RV1126_CLKSEL_CON(64), 15, 1, MFLAGS, 8, 5, DFLAGS),
	COMPOSITE(CLK_DDRPHY, "clk_ddrphy", mux_dpll_gpll_p, CLK_IS_CRITICAL,
			RV1126_CLKSEL_CON(64), 15, 1, MFLAGS, 8, 5, DFLAGS,
			RV1126_CLKGATE_CON(21), 8, GFLAGS),
	GATE(0, "clk1x_phy", "clk_ddrphy", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(23), 1, GFLAGS),
	GATE(0, "clk_ddr_msch", "clk_ddrphy", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(21), 10, GFLAGS),
	GATE(0, "pclk_ddr_dfictl", "pclk_pdddr", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(21), 2, GFLAGS),
	GATE(0, "clk_ddr_dfictl", "clk_ddrphy", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(21), 13, GFLAGS),
	GATE(0, "pclk_ddr_standby", "pclk_pdddr", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(21), 4, GFLAGS),
	GATE(0, "clk_ddr_standby", "clk_ddrphy", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(21), 14, GFLAGS),
	GATE(0, "aclk_ddr_split", "clk_ddrphy", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(21), 9, GFLAGS),
	GATE(0, "pclk_ddr_grf", "pclk_pdddr", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(21), 5, GFLAGS),
	GATE(PCLK_DDR_MON, "pclk_ddr_mon", "pclk_pdddr", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(21), 3, GFLAGS),
	GATE(CLK_DDR_MON, "clk_ddr_mon", "clk_ddrphy", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(20), 15, GFLAGS),
	GATE(TMCLK_DDR_MON, "tmclk_ddr_mon", "xin24m", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(21), 7, GFLAGS),

	/*
	 * Clock-Architecture Diagram 14
	 */
	/* PD_NPU */
	GATE(0, "aclk_pdnpu_niu", "aclk_pdnpu", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(22), 4, GFLAGS),
	GATE(0, "hclk_pdnpu_niu", "hclk_pdnpu", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(22), 5, GFLAGS),
	GATE(0, "pclk_pdnpu_niu", "pclk_pdnpu", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(22), 6, GFLAGS),

	/*
	 * Clock-Architecture Diagram 15
	 */
	GATE(0, "pclk_topniu", "pclk_pdtop", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(23), 9, GFLAGS),
	GATE(PCLK_TOPCRU, "pclk_topcru", "pclk_pdtop", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(23), 10, GFLAGS),
	GATE(PCLK_TOPGRF, "pclk_topgrf", "pclk_pdtop", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(23), 11, GFLAGS),
	GATE(PCLK_CPUEMADET, "pclk_cpuemadet", "pclk_pdtop", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(23), 12, GFLAGS),
	GATE(PCLK_DDRPHY, "pclk_ddrphy", "pclk_pdtop", CLK_IGNORE_UNUSED,
			RV1126_CLKGATE_CON(23), 0, GFLAGS),
#endif
};

static void __iomem *rv1126_cru_base;
static void __iomem *rv1126_pmucru_base;

void rv1126_dump_cru(void)
{
	if (rv1126_pmucru_base) {
		pr_warn("PMU CRU:\n");
		print_hex_dump(KERN_WARNING, "", DUMP_PREFIX_OFFSET,
			       32, 4, rv1126_pmucru_base,
			       0x248, false);
	}
	if (rv1126_cru_base) {
		pr_warn("CRU:\n");
		print_hex_dump(KERN_WARNING, "", DUMP_PREFIX_OFFSET,
			       32, 4, rv1126_cru_base,
			       0x588, false);
	}
}
EXPORT_SYMBOL_GPL(rv1126_dump_cru);

static int rv1126_clk_panic(struct notifier_block *this,
			  unsigned long ev, void *ptr)
{
	rv1126_dump_cru();
	return NOTIFY_DONE;
}

static struct notifier_block rv1126_clk_panic_block = {
	.notifier_call = rv1126_clk_panic,
};

static struct rockchip_clk_provider *pmucru_ctx;
static void __init rv1126_pmu_clk_init(struct device_node *np)
{
	struct rockchip_clk_provider *ctx;
	void __iomem *reg_base;

	reg_base = of_iomap(np, 0);
	if (!reg_base) {
		pr_err("%s: could not map cru pmu region\n", __func__);
		return;
	}

	rv1126_pmucru_base = reg_base;

	ctx = rockchip_clk_init(np, reg_base, CLKPMU_NR_CLKS);
	if (IS_ERR(ctx)) {
		pr_err("%s: rockchip pmu clk init failed\n", __func__);
		return;
	}

	rockchip_clk_register_plls(ctx, rv1126_pmu_pll_clks,
				   ARRAY_SIZE(rv1126_pmu_pll_clks),
				   RV1126_GRF_SOC_STATUS0);

	rockchip_clk_register_branches(ctx, rv1126_clk_pmu_branches,
				       ARRAY_SIZE(rv1126_clk_pmu_branches));

	rockchip_register_softrst(np, 2, reg_base + RV1126_PMU_SOFTRST_CON(0),
				  ROCKCHIP_SOFTRST_HIWORD_MASK);

	rockchip_clk_of_add_provider(np, ctx);

	pmucru_ctx = ctx;
}

CLK_OF_DECLARE(rv1126_cru_pmu, "rockchip,rv1126-pmucru", rv1126_pmu_clk_init);

static void __init rv1126_clk_init(struct device_node *np)
{
	struct rockchip_clk_provider *ctx;
	void __iomem *reg_base;
	struct clk **cru_clks, **pmucru_clks;

	reg_base = of_iomap(np, 0);
	if (!reg_base) {
		pr_err("%s: could not map cru region\n", __func__);
		return;
	}

	rv1126_cru_base = reg_base;

	ctx = rockchip_clk_init(np, reg_base, CLK_NR_CLKS);
	if (IS_ERR(ctx)) {
		pr_err("%s: rockchip clk init failed\n", __func__);
		iounmap(reg_base);
		return;
	}
	cru_clks = ctx->clk_data.clks;
	pmucru_clks = pmucru_ctx->clk_data.clks;

	rockchip_clk_register_plls(ctx, rv1126_pll_clks,
				   ARRAY_SIZE(rv1126_pll_clks),
				   RV1126_GRF_SOC_STATUS0);

	rockchip_clk_register_armclk(ctx, ARMCLK, "armclk",
				     3, cru_clks[PLL_APLL], pmucru_clks[PLL_GPLL],
				     &rv1126_cpuclk_data, rv1126_cpuclk_rates,
				     ARRAY_SIZE(rv1126_cpuclk_rates));

	rockchip_clk_register_branches(ctx, rv1126_clk_branches,
				       ARRAY_SIZE(rv1126_clk_branches));

	rockchip_register_softrst(np, 15, reg_base + RV1126_SOFTRST_CON(0),
				  ROCKCHIP_SOFTRST_HIWORD_MASK);

	rockchip_register_restart_notifier(ctx, RV1126_GLB_SRST_FST, NULL);

	rockchip_clk_of_add_provider(np, ctx);

	atomic_notifier_chain_register(&panic_notifier_list,
				       &rv1126_clk_panic_block);
}

CLK_OF_DECLARE(rv1126_cru, "rockchip,rv1126-cru", rv1126_clk_init);

struct clk_rv1126_inits {
	void (*inits)(struct device_node *np);
};

static const struct clk_rv1126_inits clk_rv1126_pmu_init = {
	.inits = rv1126_pmu_clk_init,
};

static const struct clk_rv1126_inits clk_rv1126_init = {
	.inits = rv1126_clk_init,
};

static const struct of_device_id clk_rv1126_match_table[] = {
	{
		.compatible = "rockchip,rv1126-cru",
		.data = &clk_rv1126_init,
	}, {
		.compatible = "rockchip,rv1126-pmucru",
		.data = &clk_rv1126_pmu_init,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, clk_rv1126_match_table);

static int __init clk_rv1126_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match;
	const struct clk_rv1126_inits *init_data;

	match = of_match_device(clk_rv1126_match_table, &pdev->dev);
	if (!match || !match->data)
		return -EINVAL;

	init_data = match->data;
	if (init_data->inits)
		init_data->inits(np);

	return 0;
}

static struct platform_driver clk_rv1126_driver = {
	.driver		= {
		.name	= "clk-rv1126",
		.of_match_table = clk_rv1126_match_table,
	},
};
builtin_platform_driver_probe(clk_rv1126_driver, clk_rv1126_probe);

MODULE_DESCRIPTION("Rockchip RV1126 Clock Driver");
MODULE_LICENSE("GPL");
