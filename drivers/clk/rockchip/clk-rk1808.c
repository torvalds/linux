// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Fuzhou Rockchip Electronics Co., Ltd
 * Author: Elaine Zhang <zhangqing@rock-chips.com>
 */
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/syscore_ops.h>
#include <dt-bindings/clock/rk1808-cru.h>
#include "clk.h"

#define RK1808_GRF_SOC_STATUS0		0x480
#define RK1808_UART_FRAC_MAX_PRATE	800000000
#define RK1808_PDM_FRAC_MAX_PRATE	300000000
#define RK1808_I2S_FRAC_MAX_PRATE	600000000
#define RK1808_VOP_RAW_FRAC_MAX_PRATE	300000000
#define RK1808_VOP_LITE_FRAC_MAX_PRATE	400000000

enum rk1808_plls {
	apll, dpll, cpll, gpll, npll, ppll,
};

static struct rockchip_pll_rate_table rk1808_pll_rates[] = {
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
	RK3036_PLL_RATE(1188000000, 1, 99, 2, 1, 1, 0),
	RK3036_PLL_RATE(1104000000, 1, 46, 1, 1, 1, 0),
	RK3036_PLL_RATE(1100000000, 2, 275, 3, 1, 1, 0),
	RK3036_PLL_RATE(1008000000, 1, 84, 2, 1, 1, 0),
	RK3036_PLL_RATE(1000000000, 1, 250, 6, 1, 1, 0),
	RK3036_PLL_RATE(984000000, 1, 82, 2, 1, 1, 0),
	RK3036_PLL_RATE(960000000, 1, 80, 2, 1, 1, 0),
	RK3036_PLL_RATE(936000000, 1, 78, 2, 1, 1, 0),
	RK3036_PLL_RATE(912000000, 1, 76, 2, 1, 1, 0),
	RK3036_PLL_RATE(900000000, 1, 75, 2, 1, 1, 0),
	RK3036_PLL_RATE(888000000, 1, 74, 2, 1, 1, 0),
	RK3036_PLL_RATE(864000000, 1, 72, 2, 1, 1, 0),
	RK3036_PLL_RATE(840000000, 1, 70, 2, 1, 1, 0),
	RK3036_PLL_RATE(816000000, 1, 68, 2, 1, 1, 0),
	RK3036_PLL_RATE(800000000, 1, 100, 3, 1, 1, 0),
	RK3036_PLL_RATE(700000000, 1, 175, 2, 1, 1, 0),
	RK3036_PLL_RATE(696000000, 1, 58, 2, 1, 1, 0),
	RK3036_PLL_RATE(624000000, 1, 52, 2, 1, 1, 0),
	RK3036_PLL_RATE(600000000, 1, 75, 3, 1, 1, 0),
	RK3036_PLL_RATE(594000000, 1, 99, 4, 1, 1, 0),
	RK3036_PLL_RATE(504000000, 1, 63, 3, 1, 1, 0),
	RK3036_PLL_RATE(500000000, 1, 125, 6, 1, 1, 0),
	RK3036_PLL_RATE(416000000, 1, 52, 3, 1, 1, 0),
	RK3036_PLL_RATE(408000000, 1, 68, 2, 2, 1, 0),
	RK3036_PLL_RATE(312000000, 1, 52, 2, 2, 1, 0),
	RK3036_PLL_RATE(216000000, 1, 72, 4, 2, 1, 0),
	RK3036_PLL_RATE(200000000, 1, 200, 6, 4, 1, 0),
	RK3036_PLL_RATE(100000000, 1, 150, 6, 6, 1, 0),
	RK3036_PLL_RATE(96000000, 1, 64, 4, 4, 1, 0),
	{ /* sentinel */ },
};

#define RK1808_DIV_ACLKM_MASK		0x7
#define RK1808_DIV_ACLKM_SHIFT		12
#define RK1808_DIV_PCLK_DBG_MASK	0xf
#define RK1808_DIV_PCLK_DBG_SHIFT	8

#define RK1808_CLKSEL0(_aclk_core, _pclk_dbg)				\
{									\
	.reg = RK1808_CLKSEL_CON(0),					\
	.val = HIWORD_UPDATE(_aclk_core, RK1808_DIV_ACLKM_MASK,		\
			     RK1808_DIV_ACLKM_SHIFT) |			\
	       HIWORD_UPDATE(_pclk_dbg, RK1808_DIV_PCLK_DBG_MASK,	\
			     RK1808_DIV_PCLK_DBG_SHIFT),		\
}

#define RK1808_CPUCLK_RATE(_prate, _aclk_core, _pclk_dbg)		\
{									\
	.prate = _prate,						\
	.divs = {							\
		RK1808_CLKSEL0(_aclk_core, _pclk_dbg),			\
	},								\
}

static struct rockchip_cpuclk_rate_table rk1808_cpuclk_rates[] __initdata = {
	RK1808_CPUCLK_RATE(1608000000, 1, 7),
	RK1808_CPUCLK_RATE(1512000000, 1, 7),
	RK1808_CPUCLK_RATE(1488000000, 1, 5),
	RK1808_CPUCLK_RATE(1416000000, 1, 5),
	RK1808_CPUCLK_RATE(1392000000, 1, 5),
	RK1808_CPUCLK_RATE(1296000000, 1, 5),
	RK1808_CPUCLK_RATE(1200000000, 1, 5),
	RK1808_CPUCLK_RATE(1104000000, 1, 5),
	RK1808_CPUCLK_RATE(1008000000, 1, 5),
	RK1808_CPUCLK_RATE(912000000, 1, 5),
	RK1808_CPUCLK_RATE(816000000, 1, 3),
	RK1808_CPUCLK_RATE(696000000, 1, 3),
	RK1808_CPUCLK_RATE(600000000, 1, 3),
	RK1808_CPUCLK_RATE(408000000, 1, 1),
	RK1808_CPUCLK_RATE(312000000, 1, 1),
	RK1808_CPUCLK_RATE(216000000,  1, 1),
	RK1808_CPUCLK_RATE(96000000, 1, 1),
};

static const struct rockchip_cpuclk_reg_data rk1808_cpuclk_data = {
	.core_reg = RK1808_CLKSEL_CON(0),
	.div_core_shift = 0,
	.div_core_mask = 0xf,
	.mux_core_alt = 2,
	.mux_core_main = 0,
	.mux_core_shift = 6,
	.mux_core_mask = 0x3,
	.pll_name = "pll_apll",
};

PNAME(mux_pll_p)		= { "xin24m", "xin32k"};
PNAME(mux_usb480m_p)		= { "xin24m", "usb480m_phy", "xin32k" };
PNAME(mux_armclk_p)		= { "apll_core", "cpll_core", "gpll_core" };
PNAME(mux_gpll_cpll_p)		= { "gpll", "cpll" };
PNAME(mux_gpll_cpll_apll_p)		= { "gpll", "cpll", "apll" };
PNAME(mux_npu_p)		= { "clk_npu_div", "clk_npu_np5" };
PNAME(mux_ddr_p)	= { "dpll_ddr", "gpll_ddr" };
PNAME(mux_cpll_gpll_npll_p)		= { "cpll", "gpll", "npll" };
PNAME(mux_gpll_cpll_npll_p)		= { "gpll", "cpll", "npll" };
PNAME(mux_dclk_vopraw_p)		= { "dclk_vopraw_src", "dclk_vopraw_frac", "xin24m" };
PNAME(mux_dclk_voplite_p)		= { "dclk_voplite_src", "dclk_voplite_frac", "xin24m" };
PNAME(mux_24m_npll_gpll_usb480m_p)	= { "xin24m", "npll", "gpll", "usb480m" };
PNAME(mux_usb3_otg0_suspend_p)	= { "xin32k", "xin24m" };
PNAME(mux_pcie_aux_p)	= { "xin24m", "clk_pcie_src" };
PNAME(mux_gpll_cpll_npll_24m_p)	= { "gpll", "cpll", "npll", "xin24m" };
PNAME(mux_sdio_p)	= { "clk_sdio_div", "clk_sdio_div50" };
PNAME(mux_sdmmc_p)		= { "clk_sdmmc_div", "clk_sdmmc_div50" };
PNAME(mux_emmc_p)		= { "clk_emmc_div", "clk_emmc_div50" };
PNAME(mux_cpll_npll_ppll_p)	= { "cpll", "npll", "ppll" };
PNAME(mux_gmac_p)	= { "clk_gmac_src", "gmac_clkin" };
PNAME(mux_gmac_rgmii_speed_p)	= { "clk_gmac_tx_src", "clk_gmac_tx_src", "clk_gmac_tx_div50", "clk_gmac_tx_div5" };
PNAME(mux_gmac_rmii_speed_p)	= { "clk_gmac_rx_div20", "clk_gmac_rx_div2" };
PNAME(mux_gmac_rx_tx_p)	= { "clk_gmac_rgmii_speed", "clk_gmac_rmii_speed" };
PNAME(mux_gpll_usb480m_cpll_npll_p)	= { "gpll", "usb480m", "cpll", "npll" };
PNAME(mux_uart1_p)		= { "clk_uart1_src", "clk_uart1_np5", "clk_uart1_frac", "xin24m" };
PNAME(mux_uart2_p)		= { "clk_uart2_src", "clk_uart2_np5", "clk_uart2_frac", "xin24m" };
PNAME(mux_uart3_p)		= { "clk_uart3_src", "clk_uart3_np5", "clk_uart3_frac", "xin24m" };
PNAME(mux_uart4_p)		= { "clk_uart4_src", "clk_uart4_np5", "clk_uart4_frac", "xin24m" };
PNAME(mux_uart5_p)		= { "clk_uart5_src", "clk_uart5_np5", "clk_uart5_frac", "xin24m" };
PNAME(mux_uart6_p)		= { "clk_uart6_src", "clk_uart6_np5", "clk_uart6_frac", "xin24m" };
PNAME(mux_uart7_p)		= { "clk_uart7_src", "clk_uart7_np5", "clk_uart7_frac", "xin24m" };
PNAME(mux_gpll_xin24m_p)		= { "gpll", "xin24m" };
PNAME(mux_gpll_cpll_xin24m_p)		= { "gpll", "cpll", "xin24m" };
PNAME(mux_gpll_xin24m_cpll_npll_p)	= { "gpll", "xin24m", "cpll", "npll" };
PNAME(mux_pdm_p)		= { "clk_pdm_src", "clk_pdm_frac" };
PNAME(mux_i2s0_8ch_tx_p)	= { "clk_i2s0_8ch_tx_src", "clk_i2s0_8ch_tx_frac", "mclk_i2s0_8ch_in", "xin12m" };
PNAME(mux_i2s0_8ch_tx_rx_p)	= { "clk_i2s0_8ch_tx_mux", "clk_i2s0_8ch_rx_mux"};
PNAME(mux_i2s0_8ch_tx_out_p)	= { "clk_i2s0_8ch_tx", "xin12m", "clk_i2s0_8ch_rx" };
PNAME(mux_i2s0_8ch_rx_p)	= { "clk_i2s0_8ch_rx_src", "clk_i2s0_8ch_rx_frac", "mclk_i2s0_8ch_in", "xin12m" };
PNAME(mux_i2s0_8ch_rx_tx_p)	= { "clk_i2s0_8ch_rx_mux", "clk_i2s0_8ch_tx_mux"};
PNAME(mux_i2s0_8ch_rx_out_p)	= { "clk_i2s0_8ch_rx", "xin12m", "clk_i2s0_8ch_tx" };
PNAME(mux_i2s1_2ch_p)		= { "clk_i2s1_2ch_src", "clk_i2s1_2ch_frac", "mclk_i2s1_2ch_in", "xin12m" };
PNAME(mux_i2s1_2ch_out_p)	= { "clk_i2s1_2ch", "xin12m" };
PNAME(mux_rtc32k_pmu_p)		= { "xin32k", "pmu_pvtm_32k", "clk_rtc32k_frac" };
PNAME(mux_wifi_pmu_p)		= { "xin24m", "clk_wifi_pmu_src" };
PNAME(mux_gpll_usb480m_cpll_ppll_p)	= { "gpll", "usb480m", "cpll", "ppll" };
PNAME(mux_uart0_pmu_p)		= { "clk_uart0_pmu_src", "clk_uart0_np5", "clk_uart0_frac", "xin24m" };
PNAME(mux_usbphy_ref_p)		= { "xin24m", "clk_ref24m_pmu" };
PNAME(mux_mipidsiphy_ref_p)	= { "xin24m", "clk_ref24m_pmu" };
PNAME(mux_pciephy_ref_p)		= { "xin24m", "clk_pciephy_src" };
PNAME(mux_ppll_xin24m_p)		= { "ppll", "xin24m" };
PNAME(mux_xin24m_32k_p)		= { "xin24m", "xin32k" };

static struct rockchip_pll_clock rk1808_pll_clks[] __initdata = {
	[apll] = PLL(pll_rk3036, PLL_APLL, "apll", mux_pll_p,
		     0, RK1808_PLL_CON(0),
		     RK1808_MODE_CON, 0, 0, 0, rk1808_pll_rates),
	[dpll] = PLL(pll_rk3036, PLL_DPLL, "dpll", mux_pll_p,
		     0, RK1808_PLL_CON(8),
		     RK1808_MODE_CON, 2, 1, 0, NULL),
	[cpll] = PLL(pll_rk3036, PLL_CPLL, "cpll", mux_pll_p,
		     0, RK1808_PLL_CON(16),
		     RK1808_MODE_CON, 4, 2, 0, rk1808_pll_rates),
	[gpll] = PLL(pll_rk3036, PLL_GPLL, "gpll", mux_pll_p,
		     0, RK1808_PLL_CON(24),
		     RK1808_MODE_CON, 6, 3, 0, rk1808_pll_rates),
	[npll] = PLL(pll_rk3036, PLL_NPLL, "npll", mux_pll_p,
		     0, RK1808_PLL_CON(32),
		     RK1808_MODE_CON, 8, 5, 0, rk1808_pll_rates),
	[ppll] = PLL(pll_rk3036, PLL_PPLL, "ppll",  mux_pll_p,
		     0, RK1808_PMU_PLL_CON(0),
		     RK1808_PMU_MODE_CON, 0, 4, 0, rk1808_pll_rates),
};

#define MFLAGS CLK_MUX_HIWORD_MASK
#define DFLAGS CLK_DIVIDER_HIWORD_MASK
#define GFLAGS (CLK_GATE_HIWORD_MASK | CLK_GATE_SET_TO_DISABLE)

static struct rockchip_clk_branch rk1808_uart1_fracmux __initdata =
	MUX(0, "clk_uart1_mux", mux_uart1_p, CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(39), 14, 2, MFLAGS);

static struct rockchip_clk_branch rk1808_uart2_fracmux __initdata =
	MUX(0, "clk_uart2_mux", mux_uart2_p, CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(42), 14, 2, MFLAGS);

static struct rockchip_clk_branch rk1808_uart3_fracmux __initdata =
	MUX(0, "clk_uart3_mux", mux_uart3_p, CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(45), 14, 2, MFLAGS);

static struct rockchip_clk_branch rk1808_uart4_fracmux __initdata =
	MUX(0, "clk_uart4_mux", mux_uart4_p, CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(48), 14, 2, MFLAGS);

static struct rockchip_clk_branch rk1808_uart5_fracmux __initdata =
	MUX(0, "clk_uart5_mux", mux_uart5_p, CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(51), 14, 2, MFLAGS);

static struct rockchip_clk_branch rk1808_uart6_fracmux __initdata =
	MUX(0, "clk_uart6_mux", mux_uart6_p, CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(54), 14, 2, MFLAGS);

static struct rockchip_clk_branch rk1808_uart7_fracmux __initdata =
	MUX(0, "clk_uart7_mux", mux_uart7_p, CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(57), 14, 2, MFLAGS);

static struct rockchip_clk_branch rk1808_dclk_vopraw_fracmux __initdata =
	MUX(0, "dclk_vopraw_mux", mux_dclk_vopraw_p, CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(5), 14, 2, MFLAGS);

static struct rockchip_clk_branch rk1808_dclk_voplite_fracmux __initdata =
	MUX(0, "dclk_voplite_mux", mux_dclk_voplite_p, CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(7), 14, 2, MFLAGS);

static struct rockchip_clk_branch rk1808_pdm_fracmux __initdata =
	MUX(0, "clk_pdm_mux", mux_pdm_p, CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(30), 15, 1, MFLAGS);

static struct rockchip_clk_branch rk1808_i2s0_8ch_tx_fracmux __initdata =
	MUX(SCLK_I2S0_8CH_TX_MUX, "clk_i2s0_8ch_tx_mux", mux_i2s0_8ch_tx_p, CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(32), 10, 2, MFLAGS);

static struct rockchip_clk_branch rk1808_i2s0_8ch_rx_fracmux __initdata =
	MUX(SCLK_I2S0_8CH_RX_MUX, "clk_i2s0_8ch_rx_mux", mux_i2s0_8ch_rx_p, CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(34), 10, 2, MFLAGS);

static struct rockchip_clk_branch rk1808_i2s1_2ch_fracmux __initdata =
	MUX(0, "clk_i2s1_2ch_mux", mux_i2s1_2ch_p, CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(36), 10, 2, MFLAGS);

static struct rockchip_clk_branch rk1808_rtc32k_pmu_fracmux __initdata =
	MUX(SCLK_RTC32K_PMU, "clk_rtc32k_pmu", mux_rtc32k_pmu_p, CLK_SET_RATE_PARENT,
			RK1808_PMU_CLKSEL_CON(0), 14, 2, MFLAGS);

static struct rockchip_clk_branch rk1808_uart0_pmu_fracmux __initdata =
	MUX(0, "clk_uart0_pmu_mux", mux_uart0_pmu_p, CLK_SET_RATE_PARENT,
			RK1808_PMU_CLKSEL_CON(4), 14, 2, MFLAGS);

static struct rockchip_clk_branch rk1808_clk_branches[] __initdata = {
	/*
	 * Clock-Architecture Diagram 1
	 */

	MUX(USB480M, "usb480m", mux_usb480m_p, CLK_SET_RATE_PARENT,
			RK1808_MODE_CON, 10, 2, MFLAGS),
	FACTOR(0, "xin12m", "xin24m", 0, 1, 2),

	/*
	 * Clock-Architecture Diagram 2
	 */

	GATE(0, "apll_core", "apll", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(0), 0, GFLAGS),
	GATE(0, "cpll_core", "cpll", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(0), 0, GFLAGS),
	GATE(0, "gpll_core", "gpll", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(0), 0, GFLAGS),
	COMPOSITE_NOMUX(0, "pclk_core_dbg", "armclk", CLK_IGNORE_UNUSED,
			RK1808_CLKSEL_CON(0), 8, 4, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RK1808_CLKGATE_CON(0), 3, GFLAGS),
	COMPOSITE_NOMUX(0, "aclk_core", "armclk", CLK_IGNORE_UNUSED,
			RK1808_CLKSEL_CON(0), 12, 3, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RK1808_CLKGATE_CON(0), 2, GFLAGS),

	GATE(0, "clk_jtag", "jtag_clkin", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(0), 4, GFLAGS),

	GATE(SCLK_PVTM_CORE, "clk_pvtm_core", "xin24m", 0,
			RK1808_CLKGATE_CON(0), 5, GFLAGS),

	COMPOSITE_NOMUX(MSCLK_CORE_NIU, "msclk_core_niu", "gpll", 0,
			RK1808_CLKSEL_CON(18), 0, 5, DFLAGS,
			RK1808_CLKGATE_CON(0), 1, GFLAGS),

	/*
	 * Clock-Architecture Diagram 3
	 */

	COMPOSITE(ACLK_GIC_PRE, "aclk_gic_pre", mux_gpll_cpll_p, 0,
			RK1808_CLKSEL_CON(15), 11, 1, MFLAGS, 12, 4, DFLAGS,
			RK1808_CLKGATE_CON(1), 0, GFLAGS),
	GATE(0, "aclk_gic_niu", "aclk_gic_pre", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(1), 1, GFLAGS),
	GATE(ACLK_GIC, "aclk_gic", "aclk_gic_pre", 0,
			RK1808_CLKGATE_CON(1), 2, GFLAGS),
	GATE(0, "aclk_core2gic", "aclk_gic_pre", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(1), 3, GFLAGS),
	GATE(0, "aclk_gic2core", "aclk_gic_pre", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(1), 4, GFLAGS),
	GATE(0, "aclk_spinlock", "aclk_gic_pre", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(1), 4, GFLAGS),

	COMPOSITE(0, "aclk_vpu_pre", mux_gpll_cpll_p, 0,
			RK1808_CLKSEL_CON(16), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RK1808_CLKGATE_CON(8), 8, GFLAGS),
	COMPOSITE_NOMUX(0, "hclk_vpu_pre", "aclk_vpu_pre", 0,
			RK1808_CLKSEL_CON(16), 8, 4, DFLAGS,
			RK1808_CLKGATE_CON(8), 9, GFLAGS),
	GATE(ACLK_VPU, "aclk_vpu", "aclk_vpu_pre", 0,
			RK1808_CLKGATE_CON(8), 12, GFLAGS),
	GATE(0, "aclk_vpu_niu", "aclk_vpu_pre", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(8), 10, GFLAGS),
	GATE(HCLK_VPU, "hclk_vpu", "hclk_vpu_pre", 0,
			RK1808_CLKGATE_CON(8), 13, GFLAGS),
	GATE(0, "hclk_vpu_niu", "hclk_vpu_pre", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(8), 11, GFLAGS),

	/*
	 * Clock-Architecture Diagram 4
	 */
	COMPOSITE_NOGATE(0, "clk_npu_div", mux_gpll_cpll_p, CLK_KEEP_REQ_RATE,
			RK1808_CLKSEL_CON(1), 8, 2, MFLAGS, 0, 4, DFLAGS),
	COMPOSITE_NOGATE_HALFDIV(0, "clk_npu_np5", mux_gpll_cpll_p, CLK_KEEP_REQ_RATE,
			RK1808_CLKSEL_CON(1), 10, 2, MFLAGS, 4, 4, DFLAGS),
	MUX(0, "clk_npu_pre", mux_npu_p, CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(1), 15, 1, MFLAGS),
	FACTOR(0, "clk_npu_scan", "clk_npu_pre", 0, 1, 2),
	GATE(SCLK_NPU, "clk_npu", "clk_npu_pre", 0,
			RK1808_CLKGATE_CON(1), 10, GFLAGS),

	COMPOSITE(0, "aclk_npu_pre", mux_gpll_cpll_p, 0,
			RK1808_CLKSEL_CON(2), 14, 1, MFLAGS, 0, 4, DFLAGS,
			RK1808_CLKGATE_CON(1), 8, GFLAGS),
	COMPOSITE(0, "hclk_npu_pre", mux_gpll_cpll_p, 0,
			RK1808_CLKSEL_CON(2), 15, 1, MFLAGS, 8, 4, DFLAGS,
			RK1808_CLKGATE_CON(1), 9, GFLAGS),
	GATE(ACLK_NPU, "aclk_npu", "aclk_npu_pre", 0,
			RK1808_CLKGATE_CON(1), 11, GFLAGS),
	GATE(0, "aclk_npu_niu", "aclk_npu_pre", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(1), 13, GFLAGS),
	COMPOSITE_NOMUX(0, "aclk_npu2mem", "aclk_npu_pre", CLK_IGNORE_UNUSED,
			RK1808_CLKSEL_CON(2), 4, 4, DFLAGS,
			RK1808_CLKGATE_CON(1), 15, GFLAGS),
	GATE(HCLK_NPU, "hclk_npu", "hclk_npu_pre", 0,
			RK1808_CLKGATE_CON(1), 12, GFLAGS),
	GATE(0, "hclk_npu_niu", "hclk_npu_pre", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(1), 14, GFLAGS),

	GATE(SCLK_PVTM_NPU, "clk_pvtm_npu", "xin24m", 0,
			RK1808_CLKGATE_CON(0), 15, GFLAGS),

	COMPOSITE(ACLK_IMEM_PRE, "aclk_imem_pre", mux_gpll_cpll_p, CLK_IGNORE_UNUSED,
			RK1808_CLKSEL_CON(17), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RK1808_CLKGATE_CON(7), 0, GFLAGS),
	GATE(ACLK_IMEM0, "aclk_imem0", "aclk_imem_pre", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(7), 6, GFLAGS),
	GATE(0, "aclk_imem0_niu", "aclk_imem_pre", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(7), 10, GFLAGS),
	GATE(ACLK_IMEM1, "aclk_imem1", "aclk_imem_pre", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(7), 7, GFLAGS),
	GATE(0, "aclk_imem1_niu", "aclk_imem_pre", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(7), 11, GFLAGS),
	GATE(ACLK_IMEM2, "aclk_imem2", "aclk_imem_pre", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(7), 8, GFLAGS),
	GATE(0, "aclk_imem2_niu", "aclk_imem_pre", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(7), 12, GFLAGS),
	GATE(ACLK_IMEM3, "aclk_imem3", "aclk_imem_pre", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(7), 9, GFLAGS),
	GATE(0, "aclk_imem3_niu", "aclk_imem_pre", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(7), 13, GFLAGS),

	COMPOSITE(HSCLK_IMEM, "hsclk_imem", mux_gpll_cpll_p, 0,
			RK1808_CLKSEL_CON(17), 15, 1, MFLAGS, 8, 5, DFLAGS,
			RK1808_CLKGATE_CON(7), 5, GFLAGS),

	/*
	 * Clock-Architecture Diagram 5
	 */
	 GATE(0, "clk_ddr_mon_timer", "xin24m", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(2), 0, GFLAGS),

	GATE(0, "clk_ddr_mon", "clk_ddrphy1x_out", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(2), 11, GFLAGS),
	GATE(0, "aclk_split", "clk_ddrphy1x_out", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(2), 15, GFLAGS),
	GATE(0, "clk_ddr_msch", "clk_ddrphy1x_out", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(2), 8, GFLAGS),
	GATE(0, "clk_ddrdfi_ctl", "clk_ddrphy1x_out", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(2), 3, GFLAGS),
	GATE(0, "clk_stdby", "clk_ddrphy1x_out", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(2), 13, GFLAGS),
	GATE(0, "aclk_ddrc", "clk_ddrphy1x_out", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(2), 5, GFLAGS),
	GATE(0, "clk_core_ddrc", "clk_ddrphy1x_out", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(2), 6, GFLAGS),

	GATE(0, "dpll_ddr", "dpll", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(8), 5, GFLAGS),
	GATE(0, "gpll_ddr", "gpll", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(8), 6, GFLAGS),
	COMPOSITE_DDRCLK(SCLK_DDRCLK, "sclk_ddrc", mux_ddr_p, CLK_IGNORE_UNUSED,
			RK1808_CLKSEL_CON(3), 7, 1, 0, 5,
			ROCKCHIP_DDRCLK_SIP_V2),
	FACTOR(0, "clk_ddrphy1x_out", "sclk_ddrc", CLK_IGNORE_UNUSED, 1, 1),

	COMPOSITE_NOMUX(PCLK_DDR, "pclk_ddr", "gpll", 0,
			RK1808_CLKSEL_CON(3), 8, 5, DFLAGS,
			RK1808_CLKGATE_CON(2), 1, GFLAGS),
	GATE(PCLK_DDRMON, "pclk_ddrmon", "pclk_ddr", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(2), 10, GFLAGS),
	GATE(PCLK_DDRC, "pclk_ddrc", "pclk_ddr", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(2), 7, GFLAGS),
	GATE(PCLK_MSCH, "pclk_msch", "pclk_ddr", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(2), 9, GFLAGS),
	GATE(PCLK_STDBY, "pclk_stdby", "pclk_ddr", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(2), 12, GFLAGS),
	GATE(0, "pclk_ddr_grf", "pclk_ddr", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(2), 14, GFLAGS),
	GATE(0, "pclk_ddrdfi_ctl", "pclk_ddr", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(2), 2, GFLAGS),

	/*
	 * Clock-Architecture Diagram 6
	 */

	COMPOSITE(HSCLK_VIO, "hsclk_vio", mux_gpll_cpll_p, 0,
			RK1808_CLKSEL_CON(4), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RK1808_CLKGATE_CON(3), 0, GFLAGS),
	COMPOSITE_NOMUX(LSCLK_VIO, "lsclk_vio", "hsclk_vio", 0,
			RK1808_CLKSEL_CON(4), 8, 4, DFLAGS,
			RK1808_CLKGATE_CON(3), 12, GFLAGS),
	GATE(0, "hsclk_vio_niu", "hsclk_vio", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(4), 0, GFLAGS),
	GATE(0, "lsclk_vio_niu", "lsclk_vio", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(4), 1, GFLAGS),
	GATE(ACLK_VOPRAW, "aclk_vopraw", "hsclk_vio", 0,
			RK1808_CLKGATE_CON(4), 2, GFLAGS),
	GATE(HCLK_VOPRAW, "hclk_vopraw", "lsclk_vio", 0,
			RK1808_CLKGATE_CON(4), 3, GFLAGS),
	GATE(ACLK_VOPLITE, "aclk_voplite", "hsclk_vio", 0,
			RK1808_CLKGATE_CON(4), 4, GFLAGS),
	GATE(HCLK_VOPLITE, "hclk_voplite", "lsclk_vio", 0,
			RK1808_CLKGATE_CON(4), 5, GFLAGS),
	GATE(PCLK_DSI_TX, "pclk_dsi_tx", "lsclk_vio", 0,
			RK1808_CLKGATE_CON(4), 6, GFLAGS),
	GATE(PCLK_CSI_TX, "pclk_csi_tx", "lsclk_vio", 0,
			RK1808_CLKGATE_CON(4), 7, GFLAGS),
	GATE(ACLK_RGA, "aclk_rga", "hsclk_vio", 0,
			RK1808_CLKGATE_CON(4), 8, GFLAGS),
	GATE(HCLK_RGA, "hclk_rga", "lsclk_vio", 0,
			RK1808_CLKGATE_CON(4), 9, GFLAGS),
	GATE(ACLK_ISP, "aclk_isp", "hsclk_vio", 0,
			RK1808_CLKGATE_CON(4), 13, GFLAGS),
	GATE(HCLK_ISP, "hclk_isp", "lsclk_vio", 0,
			RK1808_CLKGATE_CON(4), 14, GFLAGS),
	GATE(ACLK_CIF, "aclk_cif", "hsclk_vio", 0,
			RK1808_CLKGATE_CON(4), 10, GFLAGS),
	GATE(HCLK_CIF, "hclk_cif", "lsclk_vio", 0,
			RK1808_CLKGATE_CON(4), 11, GFLAGS),
	GATE(PCLK_CSI2HOST, "pclk_csi2host", "lsclk_vio", 0,
			RK1808_CLKGATE_CON(4), 12, GFLAGS),

	COMPOSITE(0, "dclk_vopraw_src", mux_cpll_gpll_npll_p, 0,
			RK1808_CLKSEL_CON(5), 10, 2, MFLAGS, 0, 8, DFLAGS,
			RK1808_CLKGATE_CON(3), 1, GFLAGS),
	COMPOSITE_FRACMUX(0, "dclk_vopraw_frac", "dclk_vopraw_src", CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(6), 0,
			RK1808_CLKGATE_CON(3), 2, GFLAGS,
			&rk1808_dclk_vopraw_fracmux, RK1808_VOP_RAW_FRAC_MAX_PRATE),
	GATE(DCLK_VOPRAW, "dclk_vopraw", "dclk_vopraw_mux", 0,
			RK1808_CLKGATE_CON(3), 3, GFLAGS),

	COMPOSITE(0, "dclk_voplite_src", mux_cpll_gpll_npll_p, 0,
			RK1808_CLKSEL_CON(7), 10, 2, MFLAGS, 0, 8, DFLAGS,
			RK1808_CLKGATE_CON(3), 4, GFLAGS),
	COMPOSITE_FRACMUX(0, "dclk_voplite_frac", "dclk_voplite_src", CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(8), 0,
			RK1808_CLKGATE_CON(3), 5, GFLAGS,
			&rk1808_dclk_voplite_fracmux, RK1808_VOP_LITE_FRAC_MAX_PRATE),
	GATE(DCLK_VOPLITE, "dclk_voplite", "dclk_voplite_mux", 0,
			RK1808_CLKGATE_CON(3), 6, GFLAGS),

	COMPOSITE_NOMUX(SCLK_TXESC, "clk_txesc", "gpll", 0,
			RK1808_CLKSEL_CON(9), 0, 12, DFLAGS,
			RK1808_CLKGATE_CON(3), 7, GFLAGS),

	COMPOSITE(SCLK_RGA, "clk_rga", mux_gpll_cpll_npll_p, 0,
			RK1808_CLKSEL_CON(10), 6, 2, MFLAGS, 0, 5, DFLAGS,
			RK1808_CLKGATE_CON(3), 8, GFLAGS),

	COMPOSITE(SCLK_ISP, "clk_isp", mux_gpll_cpll_npll_p, 0,
			RK1808_CLKSEL_CON(10), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RK1808_CLKGATE_CON(3), 10, GFLAGS),

	COMPOSITE(DCLK_CIF, "dclk_cif", mux_cpll_gpll_npll_p, 0,
			RK1808_CLKSEL_CON(11), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RK1808_CLKGATE_CON(3), 11, GFLAGS),

	COMPOSITE(SCLK_CIF_OUT, "clk_cif_out", mux_24m_npll_gpll_usb480m_p, 0,
			RK1808_CLKSEL_CON(11), 6, 2, MFLAGS, 0, 6, DFLAGS,
			RK1808_CLKGATE_CON(3), 9, GFLAGS),

	/*
	 * Clock-Architecture Diagram 7
	 */

	/* PD_PCIE */
	COMPOSITE_NODIV(0, "clk_pcie_src", mux_gpll_cpll_p, 0,
			RK1808_CLKSEL_CON(12), 15, 1, MFLAGS,
			RK1808_CLKGATE_CON(5), 0, GFLAGS),
	DIV(HSCLK_PCIE, "hsclk_pcie", "clk_pcie_src", 0,
			RK1808_CLKSEL_CON(12), 0, 5, DFLAGS),
	DIV(LSCLK_PCIE, "lsclk_pcie", "clk_pcie_src", 0,
			RK1808_CLKSEL_CON(12), 8, 5, DFLAGS),
	GATE(0, "hsclk_pcie_niu", "hsclk_pcie", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(6), 0, GFLAGS),
	GATE(0, "lsclk_pcie_niu", "lsclk_pcie", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(6), 1, GFLAGS),
	GATE(0, "pclk_pcie_grf", "lsclk_pcie", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(6), 5, GFLAGS),
	GATE(ACLK_USB3OTG, "aclk_usb3otg", "hsclk_pcie", 0,
			RK1808_CLKGATE_CON(6), 6, GFLAGS),
	GATE(HCLK_HOST, "hclk_host", "lsclk_pcie", 0,
			RK1808_CLKGATE_CON(6), 7, GFLAGS),
	GATE(HCLK_HOST_ARB, "hclk_host_arb", "lsclk_pcie", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(6), 8, GFLAGS),

	COMPOSITE(ACLK_PCIE, "aclk_pcie", mux_gpll_cpll_p, 0,
			RK1808_CLKSEL_CON(15), 8, 1, MFLAGS, 0, 4, DFLAGS,
			RK1808_CLKGATE_CON(5), 5, GFLAGS),
	DIV(0, "pclk_pcie_pre", "aclk_pcie", 0,
			RK1808_CLKSEL_CON(15), 4, 4, DFLAGS),
	GATE(0, "aclk_pcie_niu", "aclk_pcie", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(6), 10, GFLAGS),
	GATE(ACLK_PCIE_MST, "aclk_pcie_mst", "aclk_pcie", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(6), 2, GFLAGS),
	GATE(ACLK_PCIE_SLV, "aclk_pcie_slv", "aclk_pcie", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(6), 3, GFLAGS),
	GATE(0, "pclk_pcie_niu", "pclk_pcie_pre", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(6), 11, GFLAGS),
	GATE(0, "pclk_pcie_dbi", "pclk_pcie_pre", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(6), 4, GFLAGS),
	GATE(PCLK_PCIE, "pclk_pcie", "pclk_pcie_pre", 0,
			RK1808_CLKGATE_CON(6), 9, GFLAGS),

	COMPOSITE(0, "clk_pcie_aux_src", mux_cpll_gpll_npll_p, 0,
			RK1808_CLKSEL_CON(14), 8, 2, MFLAGS, 0, 7, DFLAGS,
			RK1808_CLKGATE_CON(5), 3, GFLAGS),
	COMPOSITE_NODIV(SCLK_PCIE_AUX, "clk_pcie_aux", mux_pcie_aux_p, CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(14), 12, 1, MFLAGS,
			RK1808_CLKGATE_CON(5), 4, GFLAGS),

	GATE(SCLK_USB3_OTG0_REF, "clk_usb3_otg0_ref", "xin24m", 0,
			RK1808_CLKGATE_CON(5), 1, GFLAGS),

	COMPOSITE(SCLK_USB3_OTG0_SUSPEND, "clk_usb3_otg0_suspend", mux_usb3_otg0_suspend_p, 0,
			RK1808_CLKSEL_CON(13), 12, 1, MFLAGS, 0, 10, DFLAGS,
			RK1808_CLKGATE_CON(5), 2, GFLAGS),

	/*
	 * Clock-Architecture Diagram 8
	 */

	/* PD_PHP */

	COMPOSITE_NODIV(0, "clk_peri_src", mux_gpll_cpll_p, 0,
			RK1808_CLKSEL_CON(19), 15, 1, MFLAGS,
			RK1808_CLKGATE_CON(8), 0, GFLAGS),
	COMPOSITE_NOMUX(MSCLK_PERI, "msclk_peri", "clk_peri_src", 0,
			RK1808_CLKSEL_CON(19), 0, 5, DFLAGS,
			RK1808_CLKGATE_CON(8), 1, GFLAGS),
	COMPOSITE_NOMUX(LSCLK_PERI, "lsclk_peri", "clk_peri_src", 0,
			RK1808_CLKSEL_CON(19), 8, 5, DFLAGS,
			RK1808_CLKGATE_CON(8), 2, GFLAGS),
	GATE(0, "msclk_peri_niu", "msclk_peri", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(8), 3, GFLAGS),
	GATE(0, "lsclk_peri_niu", "lsclk_peri", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(8), 4, GFLAGS),

	/* PD_MMC */

	GATE(0, "hclk_mmc_sfc", "msclk_peri", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(9), 0, GFLAGS),
	GATE(0, "hclk_mmc_sfc_niu", "hclk_mmc_sfc", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(9), 11, GFLAGS),
	GATE(HCLK_EMMC, "hclk_emmc", "hclk_mmc_sfc", 0,
			RK1808_CLKGATE_CON(9), 12, GFLAGS),
	GATE(HCLK_SFC, "hclk_sfc", "hclk_mmc_sfc", 0,
			RK1808_CLKGATE_CON(9), 13, GFLAGS),

	COMPOSITE(SCLK_SDIO_DIV, "clk_sdio_div", mux_gpll_cpll_npll_24m_p, CLK_IGNORE_UNUSED,
			RK1808_CLKSEL_CON(22), 14, 2, MFLAGS, 0, 8, DFLAGS,
			RK1808_CLKGATE_CON(9), 1, GFLAGS),
	COMPOSITE_DIV_OFFSET(SCLK_SDIO_DIV50, "clk_sdio_div50",
			mux_gpll_cpll_npll_24m_p, CLK_IGNORE_UNUSED,
			RK1808_CLKSEL_CON(22), 14, 2, MFLAGS,
			RK1808_CLKSEL_CON(23), 0, 8, DFLAGS,
			RK1808_CLKGATE_CON(9), 2, GFLAGS),
	COMPOSITE_NODIV(SCLK_SDIO, "clk_sdio", mux_sdio_p, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			RK1808_CLKSEL_CON(23), 15, 1, MFLAGS,
			RK1808_CLKGATE_CON(9), 3, GFLAGS),

	MMC(SCLK_SDIO_DRV,     "sdio_drv",    "clk_sdio", RK1808_SDIO_CON0, 1),
	MMC(SCLK_SDIO_SAMPLE,  "sdio_sample", "clk_sdio", RK1808_SDIO_CON1, 1),

	COMPOSITE(SCLK_EMMC_DIV, "clk_emmc_div",
			mux_gpll_cpll_npll_24m_p, CLK_IGNORE_UNUSED,
			RK1808_CLKSEL_CON(24), 14, 2, MFLAGS, 0, 8, DFLAGS,
			RK1808_CLKGATE_CON(9), 4, GFLAGS),
	COMPOSITE_DIV_OFFSET(SCLK_EMMC_DIV50, "clk_emmc_div50", mux_gpll_cpll_npll_24m_p, CLK_IGNORE_UNUSED,
			RK1808_CLKSEL_CON(24), 14, 2, MFLAGS,
			RK1808_CLKSEL_CON(25), 0, 8, DFLAGS,
			RK1808_CLKGATE_CON(9), 5, GFLAGS),
	COMPOSITE_NODIV(SCLK_EMMC, "clk_emmc", mux_emmc_p, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			RK1808_CLKSEL_CON(25), 15, 1, MFLAGS,
			RK1808_CLKGATE_CON(9), 6, GFLAGS),
	MMC(SCLK_EMMC_DRV,     "emmc_drv",    "clk_emmc", RK1808_EMMC_CON0, 1),
	MMC(SCLK_EMMC_SAMPLE,  "emmc_sample", "clk_emmc", RK1808_EMMC_CON1, 1),

	COMPOSITE(SCLK_SDMMC_DIV, "clk_sdmmc_div", mux_gpll_cpll_npll_24m_p, CLK_IGNORE_UNUSED,
			RK1808_CLKSEL_CON(20), 14, 2, MFLAGS, 0, 8, DFLAGS,
			RK1808_CLKGATE_CON(9), 7, GFLAGS),
	COMPOSITE_DIV_OFFSET(SCLK_SDMMC_DIV50, "clk_sdmmc_div50",
			mux_gpll_cpll_npll_24m_p, CLK_IGNORE_UNUSED,
			RK1808_CLKSEL_CON(20), 14, 2, MFLAGS,
			RK1808_CLKSEL_CON(21), 0, 8, DFLAGS,
			RK1808_CLKGATE_CON(9), 8, GFLAGS),
	COMPOSITE_NODIV(SCLK_SDMMC, "clk_sdmmc", mux_sdmmc_p, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			RK1808_CLKSEL_CON(21), 15, 1, MFLAGS,
			RK1808_CLKGATE_CON(9), 10, GFLAGS),
	MMC(SCLK_SDMMC_DRV,     "sdmmc_drv",    "clk_sdmmc", RK1808_SDMMC_CON0, 1),
	MMC(SCLK_SDMMC_SAMPLE,  "sdmmc_sample", "clk_sdmmc", RK1808_SDMMC_CON1, 1),

	COMPOSITE(SCLK_SFC, "clk_sfc", mux_gpll_cpll_p, 0,
			RK1808_CLKSEL_CON(26), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RK1808_CLKGATE_CON(9), 10, GFLAGS),

	/* PD_MAC */

	GATE(0, "pclk_sd_gmac", "lsclk_peri", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(10), 2, GFLAGS),
	GATE(0, "aclk_sd_gmac", "msclk_peri", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(10), 0, GFLAGS),
	GATE(0, "hclk_sd_gmac", "msclk_peri", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(10), 1, GFLAGS),
	GATE(0, "pclk_gmac_niu", "pclk_sd_gmac", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(10), 10, GFLAGS),
	GATE(PCLK_GMAC, "pclk_gmac", "pclk_sd_gmac", 0,
			RK1808_CLKGATE_CON(10), 12, GFLAGS),
	GATE(0, "aclk_gmac_niu", "aclk_sd_gmac", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(10), 8, GFLAGS),
	GATE(ACLK_GMAC, "aclk_gmac", "aclk_sd_gmac", 0,
			RK1808_CLKGATE_CON(10), 11, GFLAGS),
	GATE(0, "hclk_gmac_niu", "hclk_sd_gmac", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(10), 9, GFLAGS),
	GATE(HCLK_SDIO, "hclk_sdio", "hclk_sd_gmac", 0,
			RK1808_CLKGATE_CON(10), 13, GFLAGS),
	GATE(HCLK_SDMMC, "hclk_sdmmc", "hclk_sd_gmac", 0,
			RK1808_CLKGATE_CON(10), 14, GFLAGS),

	COMPOSITE(SCLK_GMAC_OUT, "clk_gmac_out", mux_cpll_npll_ppll_p, 0,
			RK1808_CLKSEL_CON(18), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RK1808_CLKGATE_CON(10), 15, GFLAGS),

	COMPOSITE(SCLK_GMAC_SRC, "clk_gmac_src", mux_cpll_npll_ppll_p, 0,
			RK1808_CLKSEL_CON(26), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RK1808_CLKGATE_CON(10), 3, GFLAGS),
	MUX(SCLK_GMAC, "clk_gmac", mux_gmac_p, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			RK1808_CLKSEL_CON(27), 0, 1, MFLAGS),
	GATE(SCLK_GMAC_REF, "clk_gmac_ref", "clk_gmac", 0,
			RK1808_CLKGATE_CON(10), 4, GFLAGS),
	GATE(0, "clk_gmac_tx_src", "clk_gmac", 0,
			RK1808_CLKGATE_CON(10), 7, GFLAGS),
	GATE(0, "clk_gmac_rx_src", "clk_gmac", 0,
			RK1808_CLKGATE_CON(10), 6, GFLAGS),
	GATE(SCLK_GMAC_REFOUT, "clk_gmac_refout", "clk_gmac", 0,
			RK1808_CLKGATE_CON(10), 5, GFLAGS),
	FACTOR(0, "clk_gmac_tx_div5", "clk_gmac_tx_src", 0, 1, 5),
	FACTOR(0, "clk_gmac_tx_div50", "clk_gmac_tx_src", 0, 1, 50),
	FACTOR(0, "clk_gmac_rx_div2", "clk_gmac_rx_src", 0, 1, 2),
	FACTOR(0, "clk_gmac_rx_div20", "clk_gmac_rx_src", 0, 1, 20),
	MUX(SCLK_GMAC_RGMII_SPEED, "clk_gmac_rgmii_speed", mux_gmac_rgmii_speed_p,  CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(27), 2, 2, MFLAGS),
	MUX(SCLK_GMAC_RMII_SPEED, "clk_gmac_rmii_speed", mux_gmac_rmii_speed_p,  CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(27), 1, 1, MFLAGS),
	MUX(SCLK_GMAC_RX_TX, "clk_gmac_rx_tx", mux_gmac_rx_tx_p,  CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(27), 4, 1, MFLAGS),

	/*
	 * Clock-Architecture Diagram 9
	 */

	/* PD_BUS */

	COMPOSITE_NODIV(0, "clk_bus_src", mux_gpll_cpll_p, 0,
			RK1808_CLKSEL_CON(27), 15, 1, MFLAGS,
			RK1808_CLKGATE_CON(11), 0, GFLAGS),
	COMPOSITE_NOMUX(HSCLK_BUS_PRE, "hsclk_bus_pre", "clk_bus_src", 0,
			RK1808_CLKSEL_CON(27), 8, 5, DFLAGS,
			RK1808_CLKGATE_CON(11), 1, GFLAGS),
	COMPOSITE_NOMUX(MSCLK_BUS_PRE, "msclk_bus_pre", "clk_bus_src", 0,
			RK1808_CLKSEL_CON(28), 0, 5, DFLAGS,
			RK1808_CLKGATE_CON(11), 2, GFLAGS),
	COMPOSITE_NOMUX(LSCLK_BUS_PRE, "lsclk_bus_pre", "clk_bus_src", 0,
			RK1808_CLKSEL_CON(28), 8, 5, DFLAGS,
			RK1808_CLKGATE_CON(11), 3, GFLAGS),
	GATE(0, "hsclk_bus_niu", "hsclk_bus_pre", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(15), 0, GFLAGS),
	GATE(0, "msclk_bus_niu", "msclk_bus_pre", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(15), 1, GFLAGS),
	GATE(0, "msclk_sub", "msclk_bus_pre", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(15), 2, GFLAGS),
	GATE(ACLK_DMAC, "aclk_dmac", "msclk_bus_pre", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(14), 15, GFLAGS),
	GATE(HCLK_ROM, "hclk_rom", "msclk_bus_pre", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(15), 4, GFLAGS),
	GATE(ACLK_CRYPTO, "aclk_crypto", "msclk_bus_pre", 0,
			RK1808_CLKGATE_CON(15), 5, GFLAGS),
	GATE(HCLK_CRYPTO, "hclk_crypto", "msclk_bus_pre", 0,
			RK1808_CLKGATE_CON(15), 6, GFLAGS),
	GATE(ACLK_DCF, "aclk_dcf", "msclk_bus_pre", 0,
			RK1808_CLKGATE_CON(15), 7, GFLAGS),
	GATE(0, "lsclk_bus_niu", "lsclk_bus_pre", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(15), 3, GFLAGS),
	GATE(PCLK_DCF, "pclk_dcf", "lsclk_bus_pre", 0,
			RK1808_CLKGATE_CON(15), 8, GFLAGS),
	GATE(PCLK_UART1, "pclk_uart1", "lsclk_bus_pre", 0,
			RK1808_CLKGATE_CON(15), 9, GFLAGS),
	GATE(PCLK_UART2, "pclk_uart2", "lsclk_bus_pre", 0,
			RK1808_CLKGATE_CON(15), 10, GFLAGS),
	GATE(PCLK_UART3, "pclk_uart3", "lsclk_bus_pre", 0,
			RK1808_CLKGATE_CON(15), 11, GFLAGS),
	GATE(PCLK_UART4, "pclk_uart4", "lsclk_bus_pre", 0,
			RK1808_CLKGATE_CON(15), 12, GFLAGS),
	GATE(PCLK_UART5, "pclk_uart5", "lsclk_bus_pre", 0,
			RK1808_CLKGATE_CON(15), 13, GFLAGS),
	GATE(PCLK_UART6, "pclk_uart6", "lsclk_bus_pre", 0,
			RK1808_CLKGATE_CON(15), 14, GFLAGS),
	GATE(PCLK_UART7, "pclk_uart7", "lsclk_bus_pre", 0,
			RK1808_CLKGATE_CON(15), 15, GFLAGS),
	GATE(PCLK_I2C1, "pclk_i2c1", "lsclk_bus_pre", 0,
			RK1808_CLKGATE_CON(16), 0, GFLAGS),
	GATE(PCLK_I2C2, "pclk_i2c2", "lsclk_bus_pre", 0,
			RK1808_CLKGATE_CON(16), 1, GFLAGS),
	GATE(PCLK_I2C3, "pclk_i2c3", "lsclk_bus_pre", 0,
			RK1808_CLKGATE_CON(16), 2, GFLAGS),
	GATE(PCLK_I2C4, "pclk_i2c4", "lsclk_bus_pre", 0,
			RK1808_CLKGATE_CON(17), 4, GFLAGS),
	GATE(PCLK_I2C5, "pclk_i2c5", "lsclk_bus_pre", 0,
			RK1808_CLKGATE_CON(17), 5, GFLAGS),
	GATE(PCLK_SPI0, "pclk_spi0", "lsclk_bus_pre", 0,
			RK1808_CLKGATE_CON(16), 3, GFLAGS),
	GATE(PCLK_SPI1, "pclk_spi1", "lsclk_bus_pre", 0,
			RK1808_CLKGATE_CON(16), 4, GFLAGS),
	GATE(PCLK_SPI2, "pclk_spi2", "lsclk_bus_pre", 0,
			RK1808_CLKGATE_CON(16), 5, GFLAGS),
	GATE(PCLK_TSADC, "pclk_tsadc", "lsclk_bus_pre", 0,
			RK1808_CLKGATE_CON(16), 9, GFLAGS),
	GATE(PCLK_SARADC, "pclk_saradc", "lsclk_bus_pre", 0,
			RK1808_CLKGATE_CON(16), 10, GFLAGS),
	GATE(PCLK_EFUSE, "pclk_efuse", "lsclk_bus_pre", 0,
			RK1808_CLKGATE_CON(16), 11, GFLAGS),
	GATE(PCLK_GPIO1, "pclk_gpio1", "lsclk_bus_pre", 0,
			RK1808_CLKGATE_CON(16), 12, GFLAGS),
	GATE(PCLK_GPIO2, "pclk_gpio2", "lsclk_bus_pre", 0,
			RK1808_CLKGATE_CON(16), 13, GFLAGS),
	GATE(PCLK_GPIO3, "pclk_gpio3", "lsclk_bus_pre", 0,
			RK1808_CLKGATE_CON(16), 14, GFLAGS),
	GATE(PCLK_GPIO4, "pclk_gpio4", "lsclk_bus_pre", 0,
			RK1808_CLKGATE_CON(16), 15, GFLAGS),
	GATE(PCLK_PWM0, "pclk_pwm0", "lsclk_bus_pre", 0,
			RK1808_CLKGATE_CON(16), 6, GFLAGS),
	GATE(PCLK_PWM1, "pclk_pwm1", "lsclk_bus_pre", 0,
			RK1808_CLKGATE_CON(16), 7, GFLAGS),
	GATE(PCLK_PWM2, "pclk_pwm2", "lsclk_bus_pre", 0,
			RK1808_CLKGATE_CON(16), 8, GFLAGS),
	GATE(PCLK_TIMER, "pclk_timer", "lsclk_bus_pre", 0,
			RK1808_CLKGATE_CON(17), 0, GFLAGS),
	GATE(PCLK_WDT, "pclk_wdt", "lsclk_bus_pre", 0,
			RK1808_CLKGATE_CON(17), 1, GFLAGS),
	GATE(0, "pclk_grf", "lsclk_bus_pre", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(17), 2, GFLAGS),
	GATE(0, "pclk_sgrf", "lsclk_bus_pre", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(17), 3, GFLAGS),
	GATE(0, "hclk_audio_pre", "msclk_bus_pre", 0,
			RK1808_CLKGATE_CON(17), 8, GFLAGS),
	GATE(0, "pclk_top_pre", "lsclk_bus_pre", 0,
			RK1808_CLKGATE_CON(11), 4, GFLAGS),

	COMPOSITE(SCLK_CRYPTO, "clk_crypto", mux_gpll_cpll_p, 0,
			RK1808_CLKSEL_CON(29), 7, 1, MFLAGS, 0, 5, DFLAGS,
			RK1808_CLKGATE_CON(11), 5, GFLAGS),
	COMPOSITE(SCLK_CRYPTO_APK, "clk_crypto_apk", mux_gpll_cpll_p, 0,
			RK1808_CLKSEL_CON(29), 15, 1, MFLAGS, 8, 5, DFLAGS,
			RK1808_CLKGATE_CON(11), 6, GFLAGS),

	COMPOSITE(0, "clk_uart1_src", mux_gpll_usb480m_cpll_npll_p, 0,
			RK1808_CLKSEL_CON(38), 14, 2, MFLAGS, 0, 7, DFLAGS,
			RK1808_CLKGATE_CON(11), 8, GFLAGS),
	COMPOSITE_NOMUX_HALFDIV(0, "clk_uart1_np5", "clk_uart1_src", 0,
			RK1808_CLKSEL_CON(39), 0, 7, DFLAGS,
			RK1808_CLKGATE_CON(11), 9, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_uart1_frac", "clk_uart1_src", CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(40), 0,
			RK1808_CLKGATE_CON(11), 10, GFLAGS,
			&rk1808_uart1_fracmux, RK1808_UART_FRAC_MAX_PRATE),
	GATE(SCLK_UART1, "clk_uart1", "clk_uart1_mux", 0,
			RK1808_CLKGATE_CON(11), 11, GFLAGS),

	COMPOSITE(0, "clk_uart2_src", mux_gpll_usb480m_cpll_npll_p, 0,
			RK1808_CLKSEL_CON(41), 14, 2, MFLAGS, 0, 7, DFLAGS,
			RK1808_CLKGATE_CON(11), 12, GFLAGS),
	COMPOSITE_NOMUX_HALFDIV(0, "clk_uart2_np5", "clk_uart2_src", 0,
			RK1808_CLKSEL_CON(42), 0, 7, DFLAGS,
			RK1808_CLKGATE_CON(11), 13, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_uart2_frac", "clk_uart2_src", CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(43), 0,
			RK1808_CLKGATE_CON(11), 14, GFLAGS,
			&rk1808_uart2_fracmux, RK1808_UART_FRAC_MAX_PRATE),
	GATE(SCLK_UART2, "clk_uart2", "clk_uart2_mux", 0,
			RK1808_CLKGATE_CON(11), 15, GFLAGS),

	COMPOSITE(0, "clk_uart3_src", mux_gpll_usb480m_cpll_npll_p, 0,
			RK1808_CLKSEL_CON(44), 14, 2, MFLAGS, 0, 7, DFLAGS,
			RK1808_CLKGATE_CON(12), 0, GFLAGS),
	COMPOSITE_NOMUX_HALFDIV(0, "clk_uart3_np5", "clk_uart3_src", 0,
			RK1808_CLKSEL_CON(45), 0, 7, DFLAGS,
			RK1808_CLKGATE_CON(12), 1, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_uart3_frac", "clk_uart3_src", CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(46), 0,
			RK1808_CLKGATE_CON(12), 2, GFLAGS,
			&rk1808_uart3_fracmux, RK1808_UART_FRAC_MAX_PRATE),
	GATE(SCLK_UART3, "clk_uart3", "clk_uart3_mux", 0,
			RK1808_CLKGATE_CON(12), 3, GFLAGS),

	COMPOSITE(0, "clk_uart4_src", mux_gpll_usb480m_cpll_npll_p, 0,
			RK1808_CLKSEL_CON(47), 14, 2, MFLAGS, 0, 7, DFLAGS,
			RK1808_CLKGATE_CON(12), 4, GFLAGS),
	COMPOSITE_NOMUX_HALFDIV(0, "clk_uart4_np5", "clk_uart4_src", 0,
			RK1808_CLKSEL_CON(48), 0, 7, DFLAGS,
			RK1808_CLKGATE_CON(12), 5, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_uart4_frac", "clk_uart4_src", CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(49), 0,
			RK1808_CLKGATE_CON(12), 6, GFLAGS,
			&rk1808_uart4_fracmux, RK1808_UART_FRAC_MAX_PRATE),
	GATE(SCLK_UART4, "clk_uart4", "clk_uart4_mux", 0,
			RK1808_CLKGATE_CON(12), 7, GFLAGS),

	COMPOSITE(0, "clk_uart5_src", mux_gpll_usb480m_cpll_npll_p, 0,
			RK1808_CLKSEL_CON(50), 14, 2, MFLAGS, 0, 7, DFLAGS,
			RK1808_CLKGATE_CON(12), 8, GFLAGS),
	COMPOSITE_NOMUX_HALFDIV(0, "clk_uart5_np5", "clk_uart5_src", 0,
			RK1808_CLKSEL_CON(51), 0, 7, DFLAGS,
			RK1808_CLKGATE_CON(12), 9, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_uart5_frac", "clk_uart5_src", CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(52), 0,
			RK1808_CLKGATE_CON(12), 10, GFLAGS,
			&rk1808_uart5_fracmux, RK1808_UART_FRAC_MAX_PRATE),
	GATE(SCLK_UART5, "clk_uart5", "clk_uart5_mux", 0,
			RK1808_CLKGATE_CON(12), 11, GFLAGS),

	COMPOSITE(0, "clk_uart6_src", mux_gpll_usb480m_cpll_npll_p, 0,
			RK1808_CLKSEL_CON(53), 14, 2, MFLAGS, 0, 7, DFLAGS,
			RK1808_CLKGATE_CON(12), 12, GFLAGS),
	COMPOSITE_NOMUX_HALFDIV(0, "clk_uart6_np5", "clk_uart6_src", 0,
			RK1808_CLKSEL_CON(54), 0, 7, DFLAGS,
			RK1808_CLKGATE_CON(12), 13, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_uart6_frac", "clk_uart6_src", CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(55), 0,
			RK1808_CLKGATE_CON(12), 14, GFLAGS,
			&rk1808_uart6_fracmux, RK1808_UART_FRAC_MAX_PRATE),
	GATE(SCLK_UART6, "clk_uart6", "clk_uart6_mux", 0,
			RK1808_CLKGATE_CON(12), 15, GFLAGS),

	COMPOSITE(0, "clk_uart7_src", mux_gpll_usb480m_cpll_npll_p, 0,
			RK1808_CLKSEL_CON(56), 14, 2, MFLAGS, 0, 7, DFLAGS,
			RK1808_CLKGATE_CON(13), 0, GFLAGS),
	COMPOSITE_NOMUX_HALFDIV(0, "clk_uart7_np5", "clk_uart7_src", 0,
			RK1808_CLKSEL_CON(57), 0, 7, DFLAGS,
			RK1808_CLKGATE_CON(13), 1, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_uart7_frac", "clk_uart7_src", CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(58), 0,
			RK1808_CLKGATE_CON(13), 2, GFLAGS,
			&rk1808_uart7_fracmux, RK1808_UART_FRAC_MAX_PRATE),
	GATE(SCLK_UART7, "clk_uart7", "clk_uart7_mux", 0,
			RK1808_CLKGATE_CON(13), 3, GFLAGS),

	COMPOSITE(SCLK_I2C1, "clk_i2c1", mux_gpll_xin24m_p, 0,
			RK1808_CLKSEL_CON(59), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RK1808_CLKGATE_CON(13), 4, GFLAGS),
	COMPOSITE(SCLK_I2C2, "clk_i2c2", mux_gpll_xin24m_p, 0,
			RK1808_CLKSEL_CON(59), 15, 1, MFLAGS, 8, 7, DFLAGS,
			RK1808_CLKGATE_CON(13), 5, GFLAGS),
	COMPOSITE(SCLK_I2C3, "clk_i2c3", mux_gpll_xin24m_p, 0,
			RK1808_CLKSEL_CON(60), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RK1808_CLKGATE_CON(13), 6, GFLAGS),
	COMPOSITE(SCLK_I2C4, "clk_i2c4", mux_gpll_xin24m_p, 0,
			RK1808_CLKSEL_CON(71), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RK1808_CLKGATE_CON(14), 6, GFLAGS),
	COMPOSITE(SCLK_I2C5, "clk_i2c5", mux_gpll_xin24m_p, 0,
			RK1808_CLKSEL_CON(71), 15, 1, MFLAGS, 8, 7, DFLAGS,
			RK1808_CLKGATE_CON(14), 7, GFLAGS),

	COMPOSITE(SCLK_SPI0, "clk_spi0", mux_gpll_xin24m_p, 0,
			RK1808_CLKSEL_CON(60), 15, 1, MFLAGS, 8, 7, DFLAGS,
			RK1808_CLKGATE_CON(13), 7, GFLAGS),
	COMPOSITE(SCLK_SPI1, "clk_spi1", mux_gpll_xin24m_p, 0,
			RK1808_CLKSEL_CON(61), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RK1808_CLKGATE_CON(13), 8, GFLAGS),
	COMPOSITE(SCLK_SPI2, "clk_spi2", mux_gpll_xin24m_p, 0,
			RK1808_CLKSEL_CON(61), 15, 1, MFLAGS, 8, 7, DFLAGS,
			RK1808_CLKGATE_CON(13), 9, GFLAGS),

	COMPOSITE_NOMUX(SCLK_TSADC, "clk_tsadc", "xin24m", 0,
			RK1808_CLKSEL_CON(62), 0, 11, DFLAGS,
			RK1808_CLKGATE_CON(13), 13, GFLAGS),
	COMPOSITE_NOMUX(SCLK_SARADC, "clk_saradc", "xin24m", 0,
			RK1808_CLKSEL_CON(63), 0, 11, DFLAGS,
			RK1808_CLKGATE_CON(13), 14, GFLAGS),

	COMPOSITE(SCLK_EFUSE_S, "clk_efuse_s", mux_gpll_cpll_xin24m_p, 0,
			RK1808_CLKSEL_CON(64), 6, 2, MFLAGS, 0, 6, DFLAGS,
			RK1808_CLKGATE_CON(14), 0, GFLAGS),
	COMPOSITE(SCLK_EFUSE_NS, "clk_efuse_ns", mux_gpll_cpll_xin24m_p, 0,
			RK1808_CLKSEL_CON(64), 14, 2, MFLAGS, 8, 6, DFLAGS,
			RK1808_CLKGATE_CON(14), 1, GFLAGS),

	COMPOSITE(DBCLK_GPIO1, "dbclk_gpio1", mux_xin24m_32k_p, 0,
			RK1808_CLKSEL_CON(65), 15, 1, MFLAGS, 0, 11, DFLAGS,
			RK1808_CLKGATE_CON(14), 2, GFLAGS),
	COMPOSITE(DBCLK_GPIO2, "dbclk_gpio2", mux_xin24m_32k_p, 0,
			RK1808_CLKSEL_CON(66), 15, 1, MFLAGS, 0, 11, DFLAGS,
			RK1808_CLKGATE_CON(14), 3, GFLAGS),
	COMPOSITE(DBCLK_GPIO3, "dbclk_gpio3", mux_xin24m_32k_p, 0,
			RK1808_CLKSEL_CON(67), 15, 1, MFLAGS, 0, 11, DFLAGS,
			RK1808_CLKGATE_CON(14), 4, GFLAGS),
	COMPOSITE(DBCLK_GPIO4, "dbclk_gpio4", mux_xin24m_32k_p, 0,
			RK1808_CLKSEL_CON(68), 15, 1, MFLAGS, 0, 11, DFLAGS,
			RK1808_CLKGATE_CON(14), 5, GFLAGS),

	COMPOSITE(SCLK_PWM0, "clk_pwm0", mux_gpll_xin24m_p, 0,
			RK1808_CLKSEL_CON(69), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RK1808_CLKGATE_CON(13), 10, GFLAGS),
	COMPOSITE(SCLK_PWM1, "clk_pwm1", mux_gpll_xin24m_p, 0,
			RK1808_CLKSEL_CON(69), 15, 1, MFLAGS, 8, 7, DFLAGS,
			RK1808_CLKGATE_CON(13), 11, GFLAGS),
	COMPOSITE(SCLK_PWM2, "clk_pwm2", mux_gpll_xin24m_p, 0,
			RK1808_CLKSEL_CON(70), 7, 1, MFLAGS, 0, 7, DFLAGS,
			RK1808_CLKGATE_CON(13), 12, GFLAGS),

	GATE(SCLK_TIMER0, "sclk_timer0", "xin24m", 0,
			RK1808_CLKGATE_CON(14), 8, GFLAGS),
	GATE(SCLK_TIMER1, "sclk_timer1", "xin24m", 0,
			RK1808_CLKGATE_CON(14), 9, GFLAGS),
	GATE(SCLK_TIMER2, "sclk_timer2", "xin24m", 0,
			RK1808_CLKGATE_CON(14), 10, GFLAGS),
	GATE(SCLK_TIMER3, "sclk_timer3", "xin24m", 0,
			RK1808_CLKGATE_CON(14), 11, GFLAGS),
	GATE(SCLK_TIMER4, "sclk_timer4", "xin24m", 0,
			RK1808_CLKGATE_CON(14), 12, GFLAGS),
	GATE(SCLK_TIMER5, "sclk_timer5", "xin24m", 0,
			RK1808_CLKGATE_CON(14), 13, GFLAGS),

	/*
	 * Clock-Architecture Diagram 10
	 */

	/* PD_AUDIO */

	GATE(0, "hclk_audio_niu", "hclk_audio_pre", CLK_IGNORE_UNUSED,
			RK1808_CLKGATE_CON(18), 11, GFLAGS),
	GATE(HCLK_VAD, "hclk_vad", "hclk_audio_pre", 0,
			RK1808_CLKGATE_CON(18), 12, GFLAGS),
	GATE(HCLK_PDM, "hclk_pdm", "hclk_audio_pre", 0,
			RK1808_CLKGATE_CON(18), 13, GFLAGS),
	GATE(HCLK_I2S0_8CH, "hclk_i2s0_8ch", "hclk_audio_pre", 0,
			RK1808_CLKGATE_CON(18), 14, GFLAGS),
	GATE(HCLK_I2S1_2CH, "hclk_i2s1_2ch", "hclk_audio_pre", 0,
			RK1808_CLKGATE_CON(18), 15, GFLAGS),

	COMPOSITE(0, "clk_pdm_src", mux_gpll_xin24m_cpll_npll_p, 0,
			RK1808_CLKSEL_CON(30), 8, 2, MFLAGS, 0, 7, DFLAGS,
			RK1808_CLKGATE_CON(17), 9, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_pdm_frac", "clk_pdm_src", CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(31), 0,
			RK1808_CLKGATE_CON(17), 10, GFLAGS,
			&rk1808_pdm_fracmux, RK1808_PDM_FRAC_MAX_PRATE),
	GATE(SCLK_PDM, "clk_pdm", "clk_pdm_mux", 0,
			RK1808_CLKGATE_CON(17), 11, GFLAGS),

	COMPOSITE(SCLK_I2S0_8CH_TX_SRC, "clk_i2s0_8ch_tx_src", mux_gpll_cpll_npll_p, 0,
			RK1808_CLKSEL_CON(32), 8, 2, MFLAGS, 0, 7, DFLAGS,
			RK1808_CLKGATE_CON(17), 12, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_i2s0_8ch_tx_frac", "clk_i2s0_8ch_tx_src", CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(33), 0,
			RK1808_CLKGATE_CON(17), 13, GFLAGS,
			&rk1808_i2s0_8ch_tx_fracmux, RK1808_I2S_FRAC_MAX_PRATE),
	COMPOSITE_NODIV(SCLK_I2S0_8CH_TX, "clk_i2s0_8ch_tx", mux_i2s0_8ch_tx_rx_p, CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(32), 12, 1, MFLAGS,
			RK1808_CLKGATE_CON(17), 14, GFLAGS),
	COMPOSITE_NODIV(SCLK_I2S0_8CH_TX_OUT, "clk_i2s0_8ch_tx_out", mux_i2s0_8ch_tx_out_p, CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(32), 14, 2, MFLAGS,
			RK1808_CLKGATE_CON(17), 15, GFLAGS),

	COMPOSITE(SCLK_I2S0_8CH_RX_SRC, "clk_i2s0_8ch_rx_src", mux_gpll_cpll_npll_p, 0,
			RK1808_CLKSEL_CON(34), 8, 2, MFLAGS, 0, 7, DFLAGS,
			RK1808_CLKGATE_CON(18), 0, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_i2s0_8ch_rx_frac", "clk_i2s0_8ch_rx_src", CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(35), 0,
			RK1808_CLKGATE_CON(18), 1, GFLAGS,
			&rk1808_i2s0_8ch_rx_fracmux, RK1808_I2S_FRAC_MAX_PRATE),
	COMPOSITE_NODIV(SCLK_I2S0_8CH_RX, "clk_i2s0_8ch_rx", mux_i2s0_8ch_rx_tx_p, CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(34), 12, 1, MFLAGS,
			RK1808_CLKGATE_CON(18), 2, GFLAGS),
	COMPOSITE_NODIV(SCLK_I2S0_8CH_RX_OUT, "clk_i2s0_8ch_rx_out", mux_i2s0_8ch_rx_out_p, CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(34), 14, 2, MFLAGS,
			RK1808_CLKGATE_CON(18), 3, GFLAGS),

	COMPOSITE(SCLK_I2S1_2CH_SRC, "clk_i2s1_2ch_src", mux_gpll_cpll_npll_p, 0,
			RK1808_CLKSEL_CON(36), 8, 2, MFLAGS, 0, 7, DFLAGS,
			RK1808_CLKGATE_CON(18), 4, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_i2s1_2ch_frac", "clk_i2s1_2ch_src", CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(37), 0,
			RK1808_CLKGATE_CON(18), 5, GFLAGS,
			&rk1808_i2s1_2ch_fracmux, RK1808_I2S_FRAC_MAX_PRATE),
	GATE(SCLK_I2S1_2CH, "clk_i2s1_2ch", "clk_i2s1_2ch_mux", 0,
			RK1808_CLKGATE_CON(18), 6, GFLAGS),
	COMPOSITE_NODIV(SCLK_I2S1_2CH_OUT, "clk_i2s1_2ch_out", mux_i2s1_2ch_out_p, CLK_SET_RATE_PARENT,
			RK1808_CLKSEL_CON(36), 15, 1, MFLAGS,
			RK1808_CLKGATE_CON(18), 7, GFLAGS),

	/*
	 * Clock-Architecture Diagram 10
	 */

	/* PD_BUS */

	GATE(0, "pclk_top_niu", "pclk_top_pre", CLK_IGNORE_UNUSED, RK1808_CLKGATE_CON(19), 0, GFLAGS),
	GATE(0, "pclk_top_cru", "pclk_top_pre", CLK_IGNORE_UNUSED, RK1808_CLKGATE_CON(19), 1, GFLAGS),
	GATE(0, "pclk_ddrphy", "pclk_top_pre", CLK_IGNORE_UNUSED, RK1808_CLKGATE_CON(19), 2, GFLAGS),
	GATE(PCLK_MIPIDSIPHY, "pclk_mipidsiphy", "pclk_top_pre", 0, RK1808_CLKGATE_CON(19), 3, GFLAGS),
	GATE(PCLK_MIPICSIPHY, "pclk_mipicsiphy", "pclk_top_pre", 0, RK1808_CLKGATE_CON(19), 4, GFLAGS),

	GATE(PCLK_USB3PHY_PIPE, "pclk_usb3phy_pipe", "pclk_top_pre", CLK_IGNORE_UNUSED, RK1808_CLKGATE_CON(19), 6, GFLAGS),
	GATE(0, "pclk_usb3_grf", "pclk_top_pre", CLK_IGNORE_UNUSED, RK1808_CLKGATE_CON(19), 7, GFLAGS),
	GATE(0, "pclk_usb_grf", "pclk_top_pre", CLK_IGNORE_UNUSED, RK1808_CLKGATE_CON(19), 8, GFLAGS),

	/*
	 * Clock-Architecture Diagram 11
	 */

	/* PD_PMU */

	COMPOSITE_FRACMUX(SCLK_RTC32K_FRAC, "clk_rtc32k_frac", "xin24m", CLK_IGNORE_UNUSED,
			RK1808_PMU_CLKSEL_CON(1), 0,
			RK1808_PMU_CLKGATE_CON(0), 13, GFLAGS,
			&rk1808_rtc32k_pmu_fracmux, 0),

	COMPOSITE_NOMUX(XIN24M_DIV, "xin24m_div", "xin24m", CLK_IGNORE_UNUSED,
			RK1808_PMU_CLKSEL_CON(0), 8, 5, DFLAGS,
			RK1808_PMU_CLKGATE_CON(0), 12, GFLAGS),

	COMPOSITE_NOMUX(0, "clk_wifi_pmu_src", "ppll", 0,
			RK1808_PMU_CLKSEL_CON(2), 8, 6, DFLAGS,
			RK1808_PMU_CLKGATE_CON(0), 14, GFLAGS),
	COMPOSITE_NODIV(SCLK_WIFI_PMU, "clk_wifi_pmu", mux_wifi_pmu_p, CLK_SET_RATE_PARENT,
			RK1808_PMU_CLKSEL_CON(2), 15, 1, MFLAGS,
			RK1808_PMU_CLKGATE_CON(0), 15, GFLAGS),

	COMPOSITE(0, "clk_uart0_pmu_src", mux_gpll_usb480m_cpll_ppll_p, 0,
			RK1808_PMU_CLKSEL_CON(3), 14, 2, MFLAGS, 0, 7, DFLAGS,
			RK1808_PMU_CLKGATE_CON(1), 0, GFLAGS),
	COMPOSITE_NOMUX_HALFDIV(0, "clk_uart0_np5", "clk_uart0_pmu_src", 0,
			RK1808_PMU_CLKSEL_CON(4), 0, 7, DFLAGS,
			RK1808_PMU_CLKGATE_CON(1), 1, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_uart0_frac", "clk_uart0_pmu_src", CLK_SET_RATE_PARENT,
			RK1808_PMU_CLKSEL_CON(5), 0,
			RK1808_PMU_CLKGATE_CON(1), 2, GFLAGS,
			&rk1808_uart0_pmu_fracmux, RK1808_UART_FRAC_MAX_PRATE),
	GATE(SCLK_UART0_PMU, "clk_uart0_pmu", "clk_uart0_pmu_mux", CLK_SET_RATE_PARENT,
			RK1808_PMU_CLKGATE_CON(1), 3, GFLAGS),

	GATE(SCLK_PVTM_PMU, "clk_pvtm_pmu", "xin24m", 0,
			RK1808_PMU_CLKGATE_CON(1), 4, GFLAGS),

	COMPOSITE(SCLK_PMU_I2C0, "clk_pmu_i2c0", mux_ppll_xin24m_p, 0,
			RK1808_PMU_CLKSEL_CON(7), 15, 1, MFLAGS, 8, 7, DFLAGS,
			RK1808_PMU_CLKGATE_CON(1), 5, GFLAGS),

	COMPOSITE(DBCLK_PMU_GPIO0, "dbclk_gpio0", mux_xin24m_32k_p, 0,
			RK1808_PMU_CLKSEL_CON(6), 15, 1, MFLAGS, 0, 11, DFLAGS,
			RK1808_PMU_CLKGATE_CON(1), 6, GFLAGS),

	COMPOSITE_NOMUX(SCLK_REF24M_PMU, "clk_ref24m_pmu", "ppll", 0,
			RK1808_PMU_CLKSEL_CON(2), 0, 6, DFLAGS,
			RK1808_PMU_CLKGATE_CON(1), 8, GFLAGS),
	COMPOSITE_NODIV(SCLK_USBPHY_REF, "clk_usbphy_ref", mux_usbphy_ref_p, CLK_SET_RATE_PARENT,
			RK1808_PMU_CLKSEL_CON(2), 6, 1, MFLAGS,
			RK1808_PMU_CLKGATE_CON(1), 9, GFLAGS),
	COMPOSITE_NODIV(SCLK_MIPIDSIPHY_REF, "clk_mipidsiphy_ref", mux_mipidsiphy_ref_p, CLK_SET_RATE_PARENT,
			RK1808_PMU_CLKSEL_CON(2), 7, 1, MFLAGS,
			RK1808_PMU_CLKGATE_CON(1), 10, GFLAGS),

	FACTOR(0, "clk_ppll_ph0", "ppll", 0, 1, 2),
	COMPOSITE_NOMUX(0, "clk_pciephy_src", "clk_ppll_ph0", 0,
			RK1808_PMU_CLKSEL_CON(7), 0, 2, DFLAGS,
			RK1808_PMU_CLKGATE_CON(1), 11, GFLAGS),
	COMPOSITE_NODIV(SCLK_PCIEPHY_REF, "clk_pciephy_ref", mux_pciephy_ref_p, CLK_SET_RATE_PARENT,
			RK1808_PMU_CLKSEL_CON(7), 4, 1, MFLAGS,
			RK1808_PMU_CLKGATE_CON(1), 12, GFLAGS),

	COMPOSITE_NOMUX(PCLK_PMU_PRE, "pclk_pmu_pre", "ppll", 0,
			RK1808_PMU_CLKSEL_CON(0), 0, 5, DFLAGS,
			RK1808_PMU_CLKGATE_CON(0), 0, GFLAGS),

	GATE(0, "pclk_pmu_niu", "pclk_pmu_pre", CLK_IGNORE_UNUSED, RK1808_PMU_CLKGATE_CON(0), 1, GFLAGS),
	GATE(0, "pclk_pmu_sgrf", "pclk_pmu_pre", CLK_IGNORE_UNUSED, RK1808_PMU_CLKGATE_CON(0), 2, GFLAGS),
	GATE(0, "pclk_pmu_grf", "pclk_pmu_pre", CLK_IGNORE_UNUSED, RK1808_PMU_CLKGATE_CON(0), 3, GFLAGS),
	GATE(0, "pclk_pmu", "pclk_pmu_pre", CLK_IGNORE_UNUSED, RK1808_PMU_CLKGATE_CON(0), 4, GFLAGS),
	GATE(0, "pclk_pmu_mem", "pclk_pmu_pre", CLK_IGNORE_UNUSED, RK1808_PMU_CLKGATE_CON(0), 5, GFLAGS),
	GATE(PCLK_GPIO0_PMU, "pclk_gpio0_pmu", "pclk_pmu_pre", 0, RK1808_PMU_CLKGATE_CON(0), 6, GFLAGS),
	GATE(PCLK_UART0_PMU, "pclk_uart0_pmu", "pclk_pmu_pre", 0, RK1808_PMU_CLKGATE_CON(0), 7, GFLAGS),
	GATE(0, "pclk_cru_pmu", "pclk_pmu_pre", CLK_IGNORE_UNUSED, RK1808_PMU_CLKGATE_CON(0), 8, GFLAGS),
	GATE(PCLK_I2C0_PMU, "pclk_i2c0_pmu", "pclk_pmu_pre", 0, RK1808_PMU_CLKGATE_CON(0), 9, GFLAGS),
};

static const char *const rk1808_critical_clocks[] __initconst = {
	"msclk_core_niu",
	"aclk_gic_niu",
	"aclk_npu_niu",
	"hclk_npu_niu",
	"aclk_imem0_niu",
	"aclk_imem1_niu",
	"aclk_imem2_niu",
	"aclk_imem3_niu",
	"msclk_peri_niu",
	"lsclk_peri_niu",
	"hsclk_bus_niu",
	"msclk_bus_niu",
	"lsclk_bus_niu",
	"pclk_pmu_niu",
	"pclk_top_pre",
	"pclk_ddr_grf",
	"aclk_gic",
	"hsclk_imem",
};

static void __iomem *rk1808_cru_base;

void rk1808_dump_cru(void)
{
	if (rk1808_cru_base) {
		pr_warn("CRU:\n");
		print_hex_dump(KERN_WARNING, "", DUMP_PREFIX_OFFSET,
			       32, 4, rk1808_cru_base,
			       0x500, false);
		print_hex_dump(KERN_WARNING, "", DUMP_PREFIX_OFFSET,
			       32, 4, rk1808_cru_base + 0x4000,
			       0x100, false);
	}
}
EXPORT_SYMBOL_GPL(rk1808_dump_cru);

static int rk1808_clk_panic(struct notifier_block *this,
			    unsigned long ev, void *ptr)
{
	rk1808_dump_cru();
	return NOTIFY_DONE;
}

static struct notifier_block rk1808_clk_panic_block = {
	.notifier_call = rk1808_clk_panic,
};

static void __init rk1808_clk_init(struct device_node *np)
{
	struct rockchip_clk_provider *ctx;
	void __iomem *reg_base;

	reg_base = of_iomap(np, 0);
	if (!reg_base) {
		pr_err("%s: could not map cru region\n", __func__);
		return;
	}

	rk1808_cru_base = reg_base;

	ctx = rockchip_clk_init(np, reg_base, CLK_NR_CLKS);
	if (IS_ERR(ctx)) {
		pr_err("%s: rockchip clk init failed\n", __func__);
		iounmap(reg_base);
		return;
	}

	rockchip_clk_register_plls(ctx, rk1808_pll_clks,
				   ARRAY_SIZE(rk1808_pll_clks),
				   RK1808_GRF_SOC_STATUS0);
	rockchip_clk_register_branches(ctx, rk1808_clk_branches,
				       ARRAY_SIZE(rk1808_clk_branches));
	rockchip_clk_protect_critical(rk1808_critical_clocks,
				      ARRAY_SIZE(rk1808_critical_clocks));

	rockchip_clk_register_armclk(ctx, ARMCLK, "armclk",
				     mux_armclk_p, ARRAY_SIZE(mux_armclk_p),
				     &rk1808_cpuclk_data, rk1808_cpuclk_rates,
				     ARRAY_SIZE(rk1808_cpuclk_rates));

	rockchip_register_softrst(np, 16, reg_base + RK1808_SOFTRST_CON(0),
				  ROCKCHIP_SOFTRST_HIWORD_MASK);

	rockchip_register_restart_notifier(ctx, RK1808_GLB_SRST_FST, NULL);

	rockchip_clk_of_add_provider(np, ctx);

	atomic_notifier_chain_register(&panic_notifier_list,
				       &rk1808_clk_panic_block);
}

CLK_OF_DECLARE(rk1808_cru, "rockchip,rk1808-cru", rk1808_clk_init);
