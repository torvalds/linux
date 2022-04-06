// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2018 Rockchip Electronics Co. Ltd.
 * Author: Elaine Zhang<zhangqing@rock-chips.com>
 */

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/syscore_ops.h>
#include <dt-bindings/clock/px30-cru.h>
#include "clk.h"

#define PX30_GRF_SOC_STATUS0		0x480

enum px30_plls {
	apll, dpll, cpll, npll, apll_b_h, apll_b_l,
};

enum px30_pmu_plls {
	gpll,
};

static struct rockchip_pll_rate_table px30_pll_rates[] = {
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
	RK3036_PLL_RATE(984000000, 1, 82, 2, 1, 1, 0),
	RK3036_PLL_RATE(960000000, 1, 80, 2, 1, 1, 0),
	RK3036_PLL_RATE(936000000, 1, 78, 2, 1, 1, 0),
	RK3036_PLL_RATE(912000000, 1, 76, 2, 1, 1, 0),
	RK3036_PLL_RATE(900000000, 4, 300, 2, 1, 1, 0),
	RK3036_PLL_RATE(888000000, 1, 74, 2, 1, 1, 0),
	RK3036_PLL_RATE(864000000, 1, 72, 2, 1, 1, 0),
	RK3036_PLL_RATE(840000000, 1, 70, 2, 1, 1, 0),
	RK3036_PLL_RATE(816000000, 1, 68, 2, 1, 1, 0),
	RK3036_PLL_RATE(800000000, 6, 400, 2, 1, 1, 0),
	RK3036_PLL_RATE(700000000, 6, 350, 2, 1, 1, 0),
	RK3036_PLL_RATE(696000000, 1, 58, 2, 1, 1, 0),
	RK3036_PLL_RATE(624000000, 1, 52, 2, 1, 1, 0),
	RK3036_PLL_RATE(600000000, 1, 75, 3, 1, 1, 0),
	RK3036_PLL_RATE(594000000, 2, 99, 2, 1, 1, 0),
	RK3036_PLL_RATE(504000000, 1, 63, 3, 1, 1, 0),
	RK3036_PLL_RATE(500000000, 6, 250, 2, 1, 1, 0),
	RK3036_PLL_RATE(408000000, 1, 68, 2, 2, 1, 0),
	RK3036_PLL_RATE(312000000, 1, 52, 2, 2, 1, 0),
	RK3036_PLL_RATE(216000000, 1, 72, 4, 2, 1, 0),
	RK3036_PLL_RATE(96000000, 1, 64, 4, 4, 1, 0),
	{ /* sentinel */ },
};

#define PX30_DIV_ACLKM_MASK		0x7
#define PX30_DIV_ACLKM_SHIFT		12
#define PX30_DIV_PCLK_DBG_MASK	0xf
#define PX30_DIV_PCLK_DBG_SHIFT	8

#define PX30_CLKSEL0(_aclk_core, _pclk_dbg)				\
{									\
	.reg = PX30_CLKSEL_CON(0),					\
	.val = HIWORD_UPDATE(_aclk_core, PX30_DIV_ACLKM_MASK,		\
			     PX30_DIV_ACLKM_SHIFT) |			\
	       HIWORD_UPDATE(_pclk_dbg, PX30_DIV_PCLK_DBG_MASK,	\
			     PX30_DIV_PCLK_DBG_SHIFT),		\
}

#define PX30_CPUCLK_RATE(_prate, _aclk_core, _pclk_dbg)		\
{									\
	.prate = _prate,						\
	.divs = {							\
		PX30_CLKSEL0(_aclk_core, _pclk_dbg),			\
	},								\
}

static struct rockchip_cpuclk_rate_table px30_cpuclk_rates[] __initdata = {
	PX30_CPUCLK_RATE(1608000000, 1, 7),
	PX30_CPUCLK_RATE(1584000000, 1, 7),
	PX30_CPUCLK_RATE(1560000000, 1, 7),
	PX30_CPUCLK_RATE(1536000000, 1, 7),
	PX30_CPUCLK_RATE(1512000000, 1, 7),
	PX30_CPUCLK_RATE(1488000000, 1, 5),
	PX30_CPUCLK_RATE(1464000000, 1, 5),
	PX30_CPUCLK_RATE(1440000000, 1, 5),
	PX30_CPUCLK_RATE(1416000000, 1, 5),
	PX30_CPUCLK_RATE(1392000000, 1, 5),
	PX30_CPUCLK_RATE(1368000000, 1, 5),
	PX30_CPUCLK_RATE(1344000000, 1, 5),
	PX30_CPUCLK_RATE(1320000000, 1, 5),
	PX30_CPUCLK_RATE(1296000000, 1, 5),
	PX30_CPUCLK_RATE(1272000000, 1, 5),
	PX30_CPUCLK_RATE(1248000000, 1, 5),
	PX30_CPUCLK_RATE(1224000000, 1, 5),
	PX30_CPUCLK_RATE(1200000000, 1, 5),
	PX30_CPUCLK_RATE(1104000000, 1, 5),
	PX30_CPUCLK_RATE(1008000000, 1, 5),
	PX30_CPUCLK_RATE(912000000, 1, 5),
	PX30_CPUCLK_RATE(816000000, 1, 3),
	PX30_CPUCLK_RATE(696000000, 1, 3),
	PX30_CPUCLK_RATE(600000000, 1, 3),
	PX30_CPUCLK_RATE(408000000, 1, 1),
	PX30_CPUCLK_RATE(312000000, 1, 1),
	PX30_CPUCLK_RATE(216000000,  1, 1),
	PX30_CPUCLK_RATE(96000000, 1, 1),
};

static const struct rockchip_cpuclk_reg_data px30_cpuclk_data = {
	.core_reg[0] = PX30_CLKSEL_CON(0),
	.div_core_shift[0] = 0,
	.div_core_mask[0] = 0xf,
	.num_cores = 1,
	.mux_core_alt = 1,
	.mux_core_main = 0,
	.mux_core_shift = 7,
	.mux_core_mask = 0x1,
	.pll_name = "pll_apll",
};

PNAME(mux_pll_p)		= { "xin24m"};
PNAME(mux_usb480m_p)		= { "xin24m", "usb480m_phy", "clk_rtc32k_pmu" };
PNAME(mux_ddrphy_p)		= { "dpll_ddr", "gpll_ddr" };
PNAME(mux_ddrstdby_p)		= { "clk_ddrphy1x", "clk_stdby_2wrap" };
PNAME(mux_gpll_dmycpll_usb480m_npll_p)		= { "gpll", "dummy_cpll", "usb480m", "npll" };
PNAME(mux_gpll_dmycpll_usb480m_dmynpll_p)	= { "gpll", "dummy_cpll", "usb480m", "dummy_npll" };
PNAME(mux_cpll_npll_p)		= { "cpll", "npll" };
PNAME(mux_npll_cpll_p)		= { "npll", "cpll" };
PNAME(mux_gpll_cpll_p)		= { "gpll", "dummy_cpll" };
PNAME(mux_gpll_npll_p)		= { "gpll", "dummy_npll" };
PNAME(mux_gpll_xin24m_p)		= { "gpll", "xin24m"};
PNAME(mux_gpll_cpll_npll_p)		= { "gpll", "dummy_cpll", "dummy_npll" };
PNAME(mux_gpll_cpll_npll_xin24m_p)	= { "gpll", "dummy_cpll", "dummy_npll", "xin24m" };
PNAME(mux_gpll_xin24m_npll_p)		= { "gpll", "xin24m", "dummy_npll"};
PNAME(mux_pdm_p)		= { "clk_pdm_src", "clk_pdm_frac" };
PNAME(mux_i2s0_tx_p)		= { "clk_i2s0_tx_src", "clk_i2s0_tx_frac", "mclk_i2s0_tx_in", "xin12m"};
PNAME(mux_i2s0_rx_p)		= { "clk_i2s0_rx_src", "clk_i2s0_rx_frac", "mclk_i2s0_rx_in", "xin12m"};
PNAME(mux_i2s1_p)		= { "clk_i2s1_src", "clk_i2s1_frac", "i2s1_clkin", "xin12m"};
PNAME(mux_i2s2_p)		= { "clk_i2s2_src", "clk_i2s2_frac", "i2s2_clkin", "xin12m"};
PNAME(mux_i2s0_tx_out_p)	= { "clk_i2s0_tx", "xin12m", "clk_i2s0_rx"};
PNAME(mux_i2s0_rx_out_p)	= { "clk_i2s0_rx", "xin12m", "clk_i2s0_tx"};
PNAME(mux_i2s1_out_p)		= { "clk_i2s1", "xin12m"};
PNAME(mux_i2s2_out_p)		= { "clk_i2s2", "xin12m"};
PNAME(mux_i2s0_tx_rx_p)		= { "clk_i2s0_tx_mux", "clk_i2s0_rx_mux"};
PNAME(mux_i2s0_rx_tx_p)		= { "clk_i2s0_rx_mux", "clk_i2s0_tx_mux"};
PNAME(mux_uart_src_p)		= { "gpll", "xin24m", "usb480m", "dummy_npll" };
PNAME(mux_uart1_p)		= { "clk_uart1_src", "clk_uart1_np5", "clk_uart1_frac" };
PNAME(mux_uart2_p)		= { "clk_uart2_src", "clk_uart2_np5", "clk_uart2_frac" };
PNAME(mux_uart3_p)		= { "clk_uart3_src", "clk_uart3_np5", "clk_uart3_frac" };
PNAME(mux_uart4_p)		= { "clk_uart4_src", "clk_uart4_np5", "clk_uart4_frac" };
PNAME(mux_uart5_p)		= { "clk_uart5_src", "clk_uart5_np5", "clk_uart5_frac" };
PNAME(mux_cif_out_p)		= { "xin24m", "dummy_cpll", "dummy_npll", "usb480m" };
PNAME(mux_dclk_vopb_p)		= { "dclk_vopb_src", "dclk_vopb_frac", "xin24m" };
PNAME(mux_dclk_vopl_p)		= { "dclk_vopl_src", "dclk_vopl_frac", "xin24m" };
PNAME(mux_nandc_p)		= { "clk_nandc_div", "clk_nandc_div50" };
PNAME(mux_sdio_p)		= { "clk_sdio_div", "clk_sdio_div50" };
PNAME(mux_emmc_p)		= { "clk_emmc_div", "clk_emmc_div50" };
PNAME(mux_sdmmc_p)		= { "clk_sdmmc_div", "clk_sdmmc_div50" };
PNAME(mux_gmac_p)		= { "clk_gmac_src", "gmac_clkin" };
PNAME(mux_gmac_rmii_sel_p)	= { "clk_gmac_rx_tx_div20", "clk_gmac_rx_tx_div2" };
PNAME(mux_rtc32k_pmu_p)		= { "xin32k", "pmu_pvtm_32k", "clk_rtc32k_frac", };
PNAME(mux_wifi_pmu_p)		= { "xin24m", "clk_wifi_pmu_src" };
PNAME(mux_uart0_pmu_p)		= { "clk_uart0_pmu_src", "clk_uart0_np5", "clk_uart0_frac" };
PNAME(mux_usbphy_ref_p)		= { "xin24m", "clk_ref24m_pmu" };
PNAME(mux_mipidsiphy_ref_p)	= { "xin24m", "clk_ref24m_pmu" };
PNAME(mux_gpu_p)		= { "clk_gpu_div", "clk_gpu_np5" };

static struct rockchip_pll_clock px30_pll_clks[] __initdata = {
	[apll] = PLL(pll_rk3328, PLL_APLL, "apll", mux_pll_p,
		     0, PX30_PLL_CON(0),
		     PX30_MODE_CON, 0, 0, 0, px30_pll_rates),
	[dpll] = PLL(pll_rk3328, PLL_DPLL, "dpll", mux_pll_p,
		     0, PX30_PLL_CON(8),
		     PX30_MODE_CON, 4, 1, 0, NULL),
	[cpll] = PLL(pll_rk3328, PLL_CPLL, "cpll", mux_pll_p,
		     0, PX30_PLL_CON(16),
		     PX30_MODE_CON, 2, 2, 0, px30_pll_rates),
	[npll] = PLL(pll_rk3328, PLL_NPLL, "npll", mux_pll_p,
		     CLK_IS_CRITICAL, PX30_PLL_CON(24),
		     PX30_MODE_CON, 6, 4, 0, px30_pll_rates),
};

static struct rockchip_pll_clock px30_pmu_pll_clks[] __initdata = {
	[gpll] = PLL(pll_rk3328, PLL_GPLL, "gpll",  mux_pll_p, 0, PX30_PMU_PLL_CON(0),
		     PX30_PMU_MODE, 0, 3, 0, px30_pll_rates),
};

#define MFLAGS CLK_MUX_HIWORD_MASK
#define DFLAGS CLK_DIVIDER_HIWORD_MASK
#define GFLAGS (CLK_GATE_HIWORD_MASK | CLK_GATE_SET_TO_DISABLE)

static struct rockchip_clk_branch px30_pdm_fracmux __initdata =
	MUX(0, "clk_pdm_mux", mux_pdm_p, CLK_SET_RATE_PARENT,
			PX30_CLKSEL_CON(26), 15, 1, MFLAGS);

static struct rockchip_clk_branch px30_i2s0_tx_fracmux __initdata =
	MUX(SCLK_I2S0_TX_MUX, "clk_i2s0_tx_mux", mux_i2s0_tx_p, CLK_SET_RATE_PARENT,
			PX30_CLKSEL_CON(28), 10, 2, MFLAGS);

static struct rockchip_clk_branch px30_i2s0_rx_fracmux __initdata =
	MUX(SCLK_I2S0_RX_MUX, "clk_i2s0_rx_mux", mux_i2s0_rx_p, CLK_SET_RATE_PARENT,
			PX30_CLKSEL_CON(58), 10, 2, MFLAGS);

static struct rockchip_clk_branch px30_i2s1_fracmux __initdata =
	MUX(0, "clk_i2s1_mux", mux_i2s1_p, CLK_SET_RATE_PARENT,
			PX30_CLKSEL_CON(30), 10, 2, MFLAGS);

static struct rockchip_clk_branch px30_i2s2_fracmux __initdata =
	MUX(0, "clk_i2s2_mux", mux_i2s2_p, CLK_SET_RATE_PARENT,
			PX30_CLKSEL_CON(32), 10, 2, MFLAGS);

static struct rockchip_clk_branch px30_uart1_fracmux __initdata =
	MUX(0, "clk_uart1_mux", mux_uart1_p, CLK_SET_RATE_PARENT,
			PX30_CLKSEL_CON(35), 14, 2, MFLAGS);

static struct rockchip_clk_branch px30_uart2_fracmux __initdata =
	MUX(0, "clk_uart2_mux", mux_uart2_p, CLK_SET_RATE_PARENT,
			PX30_CLKSEL_CON(38), 14, 2, MFLAGS);

static struct rockchip_clk_branch px30_uart3_fracmux __initdata =
	MUX(0, "clk_uart3_mux", mux_uart3_p, CLK_SET_RATE_PARENT,
			PX30_CLKSEL_CON(41), 14, 2, MFLAGS);

static struct rockchip_clk_branch px30_uart4_fracmux __initdata =
	MUX(0, "clk_uart4_mux", mux_uart4_p, CLK_SET_RATE_PARENT,
			PX30_CLKSEL_CON(44), 14, 2, MFLAGS);

static struct rockchip_clk_branch px30_uart5_fracmux __initdata =
	MUX(0, "clk_uart5_mux", mux_uart5_p, CLK_SET_RATE_PARENT,
			PX30_CLKSEL_CON(47), 14, 2, MFLAGS);

static struct rockchip_clk_branch px30_dclk_vopb_fracmux __initdata =
	MUX(0, "dclk_vopb_mux", mux_dclk_vopb_p, CLK_SET_RATE_PARENT,
			PX30_CLKSEL_CON(5), 14, 2, MFLAGS);

static struct rockchip_clk_branch px30_dclk_vopl_fracmux __initdata =
	MUX(0, "dclk_vopl_mux", mux_dclk_vopl_p, CLK_SET_RATE_PARENT,
			PX30_CLKSEL_CON(8), 14, 2, MFLAGS);

static struct rockchip_clk_branch px30_rtc32k_pmu_fracmux __initdata =
	MUX(SCLK_RTC32K_PMU, "clk_rtc32k_pmu", mux_rtc32k_pmu_p, CLK_SET_RATE_PARENT,
			PX30_PMU_CLKSEL_CON(0), 14, 2, MFLAGS);

static struct rockchip_clk_branch px30_uart0_pmu_fracmux __initdata =
	MUX(0, "clk_uart0_pmu_mux", mux_uart0_pmu_p, CLK_SET_RATE_PARENT,
			PX30_PMU_CLKSEL_CON(4), 14, 2, MFLAGS);

static struct rockchip_clk_branch px30_clk_branches[] __initdata = {
	/*
	 * Clock-Architecture Diagram 1
	 */

	MUX(USB480M, "usb480m", mux_usb480m_p, CLK_SET_RATE_PARENT,
			PX30_MODE_CON, 8, 2, MFLAGS),
	FACTOR(0, "xin12m", "xin24m", 0, 1, 2),

	/*
	 * Clock-Architecture Diagram 3
	 */

	/* PD_CORE */
	GATE(0, "apll_core", "apll", CLK_IGNORE_UNUSED,
			PX30_CLKGATE_CON(0), 0, GFLAGS),
	GATE(0, "gpll_core", "gpll", CLK_IGNORE_UNUSED,
			PX30_CLKGATE_CON(0), 0, GFLAGS),
	COMPOSITE_NOMUX(0, "pclk_dbg", "armclk", CLK_IGNORE_UNUSED,
			PX30_CLKSEL_CON(0), 8, 4, DFLAGS | CLK_DIVIDER_READ_ONLY,
			PX30_CLKGATE_CON(0), 2, GFLAGS),
	COMPOSITE_NOMUX(0, "aclk_core", "armclk", CLK_IGNORE_UNUSED,
			PX30_CLKSEL_CON(0), 12, 3, DFLAGS | CLK_DIVIDER_READ_ONLY,
			PX30_CLKGATE_CON(0), 1, GFLAGS),
	GATE(0, "aclk_core_niu", "aclk_core", CLK_IGNORE_UNUSED,
			PX30_CLKGATE_CON(0), 4, GFLAGS),
	GATE(0, "aclk_core_prf", "aclk_core", CLK_IGNORE_UNUSED,
			PX30_CLKGATE_CON(17), 5, GFLAGS),
	GATE(0, "pclk_dbg_niu", "pclk_dbg", CLK_IGNORE_UNUSED,
			PX30_CLKGATE_CON(0), 5, GFLAGS),
	GATE(0, "pclk_core_dbg", "pclk_dbg", CLK_IGNORE_UNUSED,
			PX30_CLKGATE_CON(0), 6, GFLAGS),
	GATE(0, "pclk_core_grf", "pclk_dbg", CLK_IGNORE_UNUSED,
			PX30_CLKGATE_CON(17), 6, GFLAGS),

	GATE(0, "clk_jtag", "jtag_clkin", CLK_IGNORE_UNUSED,
			PX30_CLKGATE_CON(0), 3, GFLAGS),
	GATE(SCLK_PVTM, "clk_pvtm", "xin24m", 0,
			PX30_CLKGATE_CON(17), 4, GFLAGS),

	/* PD_GPU */
	GATE(SCLK_GPU, "clk_gpu", "clk_gpu_src", 0,
			PX30_CLKGATE_CON(0), 10, GFLAGS),
	COMPOSITE_NOMUX(0, "aclk_gpu", "clk_gpu", CLK_IGNORE_UNUSED,
			PX30_CLKSEL_CON(1), 13, 2, DFLAGS,
			PX30_CLKGATE_CON(17), 10, GFLAGS),
	GATE(0, "aclk_gpu_niu", "aclk_gpu", CLK_IS_CRITICAL,
			PX30_CLKGATE_CON(0), 11, GFLAGS),
	GATE(0, "aclk_gpu_prf", "aclk_gpu", CLK_IGNORE_UNUSED,
			PX30_CLKGATE_CON(17), 8, GFLAGS),
	GATE(0, "pclk_gpu_grf", "aclk_gpu", CLK_IGNORE_UNUSED,
			PX30_CLKGATE_CON(17), 9, GFLAGS),

	/*
	 * Clock-Architecture Diagram 4
	 */

	/* PD_DDR */
	GATE(0, "dpll_ddr", "dpll", CLK_IGNORE_UNUSED,
			PX30_CLKGATE_CON(0), 7, GFLAGS),
	GATE(0, "gpll_ddr", "gpll", CLK_IGNORE_UNUSED,
			PX30_CLKGATE_CON(0), 13, GFLAGS),
	COMPOSITE_NOGATE(SCLK_DDRCLK, "sclk_ddrc", mux_ddrphy_p, CLK_IGNORE_UNUSED,
			PX30_CLKSEL_CON(2), 7, 1, MFLAGS, 0, 3, DFLAGS | CLK_DIVIDER_POWER_OF_TWO),
	COMPOSITE_NOGATE(0, "clk_ddrphy4x", mux_ddrphy_p, CLK_IGNORE_UNUSED,
			PX30_CLKSEL_CON(2), 7, 1, MFLAGS, 0, 3, DFLAGS),
	FACTOR_GATE(0, "clk_ddrphy1x", "clk_ddrphy4x", CLK_IGNORE_UNUSED, 1, 4,
			PX30_CLKGATE_CON(0), 14, GFLAGS),
	FACTOR_GATE(0, "clk_stdby_2wrap", "clk_ddrphy4x", CLK_IGNORE_UNUSED, 1, 4,
			PX30_CLKGATE_CON(1), 0, GFLAGS),
	COMPOSITE_NODIV(0, "clk_ddrstdby", mux_ddrstdby_p, CLK_IGNORE_UNUSED,
			PX30_CLKSEL_CON(2), 4, 1, MFLAGS,
			PX30_CLKGATE_CON(1), 13, GFLAGS),
	GATE(0, "aclk_split", "clk_ddrphy1x", CLK_IGNORE_UNUSED,
			PX30_CLKGATE_CON(1), 15, GFLAGS),
	GATE(0, "clk_msch", "clk_ddrphy1x", CLK_IGNORE_UNUSED,
			PX30_CLKGATE_CON(1), 8, GFLAGS),
	GATE(0, "aclk_ddrc", "clk_ddrphy1x", CLK_IGNORE_UNUSED,
			PX30_CLKGATE_CON(1), 5, GFLAGS),
	GATE(0, "clk_core_ddrc", "clk_ddrphy1x", CLK_IGNORE_UNUSED,
			PX30_CLKGATE_CON(1), 6, GFLAGS),
	GATE(0, "aclk_cmd_buff", "clk_ddrphy1x", CLK_IGNORE_UNUSED,
			PX30_CLKGATE_CON(1), 6, GFLAGS),
	GATE(0, "clk_ddrmon", "clk_ddrphy1x", CLK_IGNORE_UNUSED,
			PX30_CLKGATE_CON(1), 11, GFLAGS),

	GATE(0, "clk_ddrmon_timer", "xin24m", CLK_IGNORE_UNUSED,
			PX30_CLKGATE_CON(0), 15, GFLAGS),

	COMPOSITE_NOMUX(PCLK_DDR, "pclk_ddr", "gpll", CLK_IGNORE_UNUSED,
			PX30_CLKSEL_CON(2), 8, 5, DFLAGS,
			PX30_CLKGATE_CON(1), 1, GFLAGS),
	GATE(0, "pclk_ddrmon", "pclk_ddr", CLK_IGNORE_UNUSED,
			PX30_CLKGATE_CON(1), 10, GFLAGS),
	GATE(0, "pclk_ddrc", "pclk_ddr", CLK_IGNORE_UNUSED,
			PX30_CLKGATE_CON(1), 7, GFLAGS),
	GATE(0, "pclk_msch", "pclk_ddr", CLK_IGNORE_UNUSED,
			PX30_CLKGATE_CON(1), 9, GFLAGS),
	GATE(0, "pclk_stdby", "pclk_ddr", CLK_IGNORE_UNUSED,
			PX30_CLKGATE_CON(1), 12, GFLAGS),
	GATE(0, "pclk_ddr_grf", "pclk_ddr", CLK_IGNORE_UNUSED,
			PX30_CLKGATE_CON(1), 14, GFLAGS),
	GATE(0, "pclk_cmdbuff", "pclk_ddr", CLK_IGNORE_UNUSED,
			PX30_CLKGATE_CON(1), 3, GFLAGS),

	/*
	 * Clock-Architecture Diagram 5
	 */

	/* PD_VI */
	COMPOSITE(ACLK_VI_PRE, "aclk_vi_pre", mux_gpll_cpll_npll_p, 0,
			PX30_CLKSEL_CON(11), 6, 2, MFLAGS, 0, 5, DFLAGS,
			PX30_CLKGATE_CON(4), 8, GFLAGS),
	COMPOSITE_NOMUX(HCLK_VI_PRE, "hclk_vi_pre", "aclk_vi_pre", 0,
			PX30_CLKSEL_CON(11), 8, 4, DFLAGS,
			PX30_CLKGATE_CON(4), 12, GFLAGS),
	COMPOSITE(SCLK_ISP, "clk_isp", mux_gpll_cpll_npll_p, 0,
			PX30_CLKSEL_CON(12), 6, 2, MFLAGS, 0, 5, DFLAGS,
			PX30_CLKGATE_CON(4), 9, GFLAGS),
	COMPOSITE(SCLK_CIF_OUT, "clk_cif_out", mux_cif_out_p, 0,
			PX30_CLKSEL_CON(13), 6, 2, MFLAGS, 0, 6, DFLAGS,
			PX30_CLKGATE_CON(4), 11, GFLAGS),
	GATE(PCLK_ISP, "pclkin_isp", "ext_pclkin", 0,
			PX30_CLKGATE_CON(4), 13, GFLAGS),
	GATE(PCLK_CIF, "pclkin_cif", "ext_pclkin", 0,
			PX30_CLKGATE_CON(4), 14, GFLAGS),

	/*
	 * Clock-Architecture Diagram 6
	 */

	/* PD_VO */
	COMPOSITE(ACLK_VO_PRE, "aclk_vo_pre", mux_gpll_cpll_npll_p, 0,
			PX30_CLKSEL_CON(3), 6, 2, MFLAGS, 0, 5, DFLAGS,
			PX30_CLKGATE_CON(2), 0, GFLAGS),
	COMPOSITE_NOMUX(HCLK_VO_PRE, "hclk_vo_pre", "aclk_vo_pre", 0,
			PX30_CLKSEL_CON(3), 8, 4, DFLAGS,
			PX30_CLKGATE_CON(2), 12, GFLAGS),
	COMPOSITE_NOMUX(PCLK_VO_PRE, "pclk_vo_pre", "aclk_vo_pre", 0,
			PX30_CLKSEL_CON(3), 12, 4, DFLAGS,
			PX30_CLKGATE_CON(2), 13, GFLAGS),
	COMPOSITE(SCLK_RGA_CORE, "clk_rga_core", mux_gpll_cpll_npll_p, 0,
			PX30_CLKSEL_CON(4), 6, 2, MFLAGS, 0, 5, DFLAGS,
			PX30_CLKGATE_CON(2), 1, GFLAGS),

	COMPOSITE(SCLK_VOPB_PWM, "clk_vopb_pwm", mux_gpll_xin24m_p, 0,
			PX30_CLKSEL_CON(7), 7, 1, MFLAGS, 0, 7, DFLAGS,
			PX30_CLKGATE_CON(2), 5, GFLAGS),
	COMPOSITE(0, "dclk_vopb_src", mux_cpll_npll_p, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			PX30_CLKSEL_CON(5), 11, 1, MFLAGS, 0, 8, DFLAGS,
			PX30_CLKGATE_CON(2), 2, GFLAGS),
	COMPOSITE_FRACMUX(0, "dclk_vopb_frac", "dclk_vopb_src", CLK_SET_RATE_PARENT,
			PX30_CLKSEL_CON(6), 0,
			PX30_CLKGATE_CON(2), 3, GFLAGS,
			&px30_dclk_vopb_fracmux),
	GATE(DCLK_VOPB, "dclk_vopb", "dclk_vopb_mux", CLK_SET_RATE_PARENT,
			PX30_CLKGATE_CON(2), 4, GFLAGS),
	COMPOSITE(0, "dclk_vopl_src", mux_npll_cpll_p, CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			PX30_CLKSEL_CON(8), 11, 1, MFLAGS, 0, 8, DFLAGS,
			PX30_CLKGATE_CON(2), 6, GFLAGS),
	COMPOSITE_FRACMUX(0, "dclk_vopl_frac", "dclk_vopl_src", CLK_SET_RATE_PARENT,
			PX30_CLKSEL_CON(9), 0,
			PX30_CLKGATE_CON(2), 7, GFLAGS,
			&px30_dclk_vopl_fracmux),
	GATE(DCLK_VOPL, "dclk_vopl", "dclk_vopl_mux", CLK_SET_RATE_PARENT,
			PX30_CLKGATE_CON(2), 8, GFLAGS),

	/* PD_VPU */
	COMPOSITE(0, "aclk_vpu_pre", mux_gpll_cpll_npll_p, 0,
			PX30_CLKSEL_CON(10), 6, 2, MFLAGS, 0, 5, DFLAGS,
			PX30_CLKGATE_CON(4), 0, GFLAGS),
	COMPOSITE_NOMUX(0, "hclk_vpu_pre", "aclk_vpu_pre", 0,
			PX30_CLKSEL_CON(10), 8, 4, DFLAGS,
			PX30_CLKGATE_CON(4), 2, GFLAGS),
	COMPOSITE(SCLK_CORE_VPU, "sclk_core_vpu", mux_gpll_cpll_npll_p, 0,
			PX30_CLKSEL_CON(13), 14, 2, MFLAGS, 8, 5, DFLAGS,
			PX30_CLKGATE_CON(4), 1, GFLAGS),

	/*
	 * Clock-Architecture Diagram 7
	 */

	COMPOSITE_NODIV(ACLK_PERI_SRC, "aclk_peri_src", mux_gpll_cpll_p, CLK_IS_CRITICAL,
			PX30_CLKSEL_CON(14), 15, 1, MFLAGS,
			PX30_CLKGATE_CON(5), 7, GFLAGS),
	COMPOSITE_NOMUX(ACLK_PERI_PRE, "aclk_peri_pre", "aclk_peri_src", CLK_IS_CRITICAL,
			PX30_CLKSEL_CON(14), 0, 5, DFLAGS,
			PX30_CLKGATE_CON(5), 8, GFLAGS),
	DIV(HCLK_PERI_PRE, "hclk_peri_pre", "aclk_peri_src", CLK_IS_CRITICAL,
			PX30_CLKSEL_CON(14), 8, 5, DFLAGS),

	/* PD_MMC_NAND */
	GATE(HCLK_MMC_NAND, "hclk_mmc_nand", "hclk_peri_pre", 0,
			PX30_CLKGATE_CON(6), 0, GFLAGS),
	COMPOSITE(SCLK_NANDC_DIV, "clk_nandc_div", mux_gpll_cpll_npll_p, 0,
			PX30_CLKSEL_CON(15), 6, 2, MFLAGS, 0, 5, DFLAGS,
			PX30_CLKGATE_CON(5), 11, GFLAGS),
	COMPOSITE(SCLK_NANDC_DIV50, "clk_nandc_div50", mux_gpll_cpll_npll_p, 0,
			PX30_CLKSEL_CON(15), 6, 2, MFLAGS, 8, 5, DFLAGS,
			PX30_CLKGATE_CON(5), 12, GFLAGS),
	COMPOSITE_NODIV(SCLK_NANDC, "clk_nandc", mux_nandc_p,
			CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			PX30_CLKSEL_CON(15), 15, 1, MFLAGS,
			PX30_CLKGATE_CON(5), 13, GFLAGS),

	COMPOSITE(SCLK_SDIO_DIV, "clk_sdio_div", mux_gpll_cpll_npll_xin24m_p, 0,
			PX30_CLKSEL_CON(18), 14, 2, MFLAGS, 0, 8, DFLAGS,
			PX30_CLKGATE_CON(6), 1, GFLAGS),
	COMPOSITE_DIV_OFFSET(SCLK_SDIO_DIV50, "clk_sdio_div50",
			mux_gpll_cpll_npll_xin24m_p, 0,
			PX30_CLKSEL_CON(18), 14, 2, MFLAGS,
			PX30_CLKSEL_CON(19), 0, 8, DFLAGS,
			PX30_CLKGATE_CON(6), 2, GFLAGS),
	COMPOSITE_NODIV(SCLK_SDIO, "clk_sdio", mux_sdio_p,
			CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			PX30_CLKSEL_CON(19), 15, 1, MFLAGS,
			PX30_CLKGATE_CON(6), 3, GFLAGS),

	COMPOSITE(SCLK_EMMC_DIV, "clk_emmc_div", mux_gpll_cpll_npll_xin24m_p, 0,
			PX30_CLKSEL_CON(20), 14, 2, MFLAGS, 0, 8, DFLAGS,
			PX30_CLKGATE_CON(6), 4, GFLAGS),
	COMPOSITE_DIV_OFFSET(SCLK_EMMC_DIV50, "clk_emmc_div50", mux_gpll_cpll_npll_xin24m_p, 0,
			PX30_CLKSEL_CON(20), 14, 2, MFLAGS,
			PX30_CLKSEL_CON(21), 0, 8, DFLAGS,
			PX30_CLKGATE_CON(6), 5, GFLAGS),
	COMPOSITE_NODIV(SCLK_EMMC, "clk_emmc", mux_emmc_p,
			CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			PX30_CLKSEL_CON(21), 15, 1, MFLAGS,
			PX30_CLKGATE_CON(6), 6, GFLAGS),

	COMPOSITE(SCLK_SFC, "clk_sfc", mux_gpll_cpll_p, 0,
			PX30_CLKSEL_CON(22), 7, 1, MFLAGS, 0, 7, DFLAGS,
			PX30_CLKGATE_CON(6), 7, GFLAGS),

	MMC(SCLK_SDMMC_DRV, "sdmmc_drv", "clk_sdmmc",
	    PX30_SDMMC_CON0, 1),
	MMC(SCLK_SDMMC_SAMPLE, "sdmmc_sample", "clk_sdmmc",
	    PX30_SDMMC_CON1, 1),

	MMC(SCLK_SDIO_DRV, "sdio_drv", "clk_sdio",
	    PX30_SDIO_CON0, 1),
	MMC(SCLK_SDIO_SAMPLE, "sdio_sample", "clk_sdio",
	    PX30_SDIO_CON1, 1),

	MMC(SCLK_EMMC_DRV, "emmc_drv", "clk_emmc",
	    PX30_EMMC_CON0, 1),
	MMC(SCLK_EMMC_SAMPLE, "emmc_sample", "clk_emmc",
	    PX30_EMMC_CON1, 1),

	/* PD_SDCARD */
	GATE(0, "hclk_sdmmc_pre", "hclk_peri_pre", 0,
			PX30_CLKGATE_CON(6), 12, GFLAGS),
	COMPOSITE(SCLK_SDMMC_DIV, "clk_sdmmc_div", mux_gpll_cpll_npll_xin24m_p, 0,
			PX30_CLKSEL_CON(16), 14, 2, MFLAGS, 0, 8, DFLAGS,
			PX30_CLKGATE_CON(6), 13, GFLAGS),
	COMPOSITE_DIV_OFFSET(SCLK_SDMMC_DIV50, "clk_sdmmc_div50", mux_gpll_cpll_npll_xin24m_p, 0,
			PX30_CLKSEL_CON(16), 14, 2, MFLAGS,
			PX30_CLKSEL_CON(17), 0, 8, DFLAGS,
			PX30_CLKGATE_CON(6), 14, GFLAGS),
	COMPOSITE_NODIV(SCLK_SDMMC, "clk_sdmmc", mux_sdmmc_p,
			CLK_SET_RATE_PARENT | CLK_SET_RATE_NO_REPARENT,
			PX30_CLKSEL_CON(17), 15, 1, MFLAGS,
			PX30_CLKGATE_CON(6), 15, GFLAGS),

	/* PD_USB */
	GATE(HCLK_USB, "hclk_usb", "hclk_peri_pre", CLK_IS_CRITICAL,
			PX30_CLKGATE_CON(7), 2, GFLAGS),
	GATE(SCLK_OTG_ADP, "clk_otg_adp", "clk_rtc32k_pmu", 0,
			PX30_CLKGATE_CON(7), 3, GFLAGS),

	/* PD_GMAC */
	COMPOSITE(SCLK_GMAC_SRC, "clk_gmac_src", mux_gpll_cpll_npll_p, 0,
			PX30_CLKSEL_CON(22), 14, 2, MFLAGS, 8, 5, DFLAGS,
			PX30_CLKGATE_CON(7), 11, GFLAGS),
	MUX(SCLK_GMAC, "clk_gmac", mux_gmac_p,  CLK_SET_RATE_PARENT,
			PX30_CLKSEL_CON(23), 6, 1, MFLAGS),
	GATE(SCLK_MAC_REF, "clk_mac_ref", "clk_gmac", 0,
			PX30_CLKGATE_CON(7), 15, GFLAGS),
	GATE(SCLK_GMAC_RX_TX, "clk_gmac_rx_tx", "clk_gmac", 0,
			PX30_CLKGATE_CON(7), 13, GFLAGS),
	FACTOR(0, "clk_gmac_rx_tx_div2", "clk_gmac_rx_tx", 0, 1, 2),
	FACTOR(0, "clk_gmac_rx_tx_div20", "clk_gmac_rx_tx", 0, 1, 20),
	MUX(SCLK_GMAC_RMII, "clk_gmac_rmii_sel", mux_gmac_rmii_sel_p,  CLK_SET_RATE_PARENT,
			PX30_CLKSEL_CON(23), 7, 1, MFLAGS),

	GATE(0, "aclk_gmac_pre", "aclk_peri_pre", 0,
			PX30_CLKGATE_CON(7), 10, GFLAGS),
	COMPOSITE_NOMUX(0, "pclk_gmac_pre", "aclk_gmac_pre", 0,
			PX30_CLKSEL_CON(23), 0, 4, DFLAGS,
			PX30_CLKGATE_CON(7), 12, GFLAGS),

	COMPOSITE(SCLK_MAC_OUT, "clk_mac_out", mux_gpll_cpll_npll_p, 0,
			PX30_CLKSEL_CON(12), 14, 2, MFLAGS, 8, 5, DFLAGS,
			PX30_CLKGATE_CON(8), 5, GFLAGS),

	/*
	 * Clock-Architecture Diagram 8
	 */

	/* PD_BUS */
	COMPOSITE_NODIV(ACLK_BUS_SRC, "aclk_bus_src", mux_gpll_cpll_p, CLK_IS_CRITICAL,
			PX30_CLKSEL_CON(23), 15, 1, MFLAGS,
			PX30_CLKGATE_CON(8), 6, GFLAGS),
	COMPOSITE_NOMUX(HCLK_BUS_PRE, "hclk_bus_pre", "aclk_bus_src", CLK_IS_CRITICAL,
			PX30_CLKSEL_CON(24), 0, 5, DFLAGS,
			PX30_CLKGATE_CON(8), 8, GFLAGS),
	COMPOSITE_NOMUX(ACLK_BUS_PRE, "aclk_bus_pre", "aclk_bus_src", CLK_IS_CRITICAL,
			PX30_CLKSEL_CON(23), 8, 5, DFLAGS,
			PX30_CLKGATE_CON(8), 7, GFLAGS),
	COMPOSITE_NOMUX(PCLK_BUS_PRE, "pclk_bus_pre", "aclk_bus_pre", CLK_IS_CRITICAL,
			PX30_CLKSEL_CON(24), 8, 2, DFLAGS,
			PX30_CLKGATE_CON(8), 9, GFLAGS),
	GATE(0, "pclk_top_pre", "pclk_bus_pre", CLK_IS_CRITICAL,
			PX30_CLKGATE_CON(8), 10, GFLAGS),

	COMPOSITE(0, "clk_pdm_src", mux_gpll_xin24m_npll_p, 0,
			PX30_CLKSEL_CON(26), 8, 2, MFLAGS, 0, 7, DFLAGS,
			PX30_CLKGATE_CON(9), 9, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_pdm_frac", "clk_pdm_src", CLK_SET_RATE_PARENT,
			PX30_CLKSEL_CON(27), 0,
			PX30_CLKGATE_CON(9), 10, GFLAGS,
			&px30_pdm_fracmux),
	GATE(SCLK_PDM, "clk_pdm", "clk_pdm_mux", CLK_SET_RATE_PARENT,
			PX30_CLKGATE_CON(9), 11, GFLAGS),

	COMPOSITE(0, "clk_i2s0_tx_src", mux_gpll_npll_p, 0,
			PX30_CLKSEL_CON(28), 8, 1, MFLAGS, 0, 7, DFLAGS,
			PX30_CLKGATE_CON(9), 12, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_i2s0_tx_frac", "clk_i2s0_tx_src", CLK_SET_RATE_PARENT,
			PX30_CLKSEL_CON(29), 0,
			PX30_CLKGATE_CON(9), 13, GFLAGS,
			&px30_i2s0_tx_fracmux),
	COMPOSITE_NODIV(SCLK_I2S0_TX, "clk_i2s0_tx", mux_i2s0_tx_rx_p, CLK_SET_RATE_PARENT,
			PX30_CLKSEL_CON(28), 12, 1, MFLAGS,
			PX30_CLKGATE_CON(9), 14, GFLAGS),
	COMPOSITE_NODIV(0, "clk_i2s0_tx_out_pre", mux_i2s0_tx_out_p, CLK_SET_RATE_PARENT,
			PX30_CLKSEL_CON(28), 14, 2, MFLAGS,
			PX30_CLKGATE_CON(9), 15, GFLAGS),
	GATE(SCLK_I2S0_TX_OUT, "clk_i2s0_tx_out", "clk_i2s0_tx_out_pre", CLK_SET_RATE_PARENT,
			PX30_CLKGATE_CON(10), 8, CLK_GATE_HIWORD_MASK),

	COMPOSITE(0, "clk_i2s0_rx_src", mux_gpll_npll_p, 0,
			PX30_CLKSEL_CON(58), 8, 1, MFLAGS, 0, 7, DFLAGS,
			PX30_CLKGATE_CON(17), 0, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_i2s0_rx_frac", "clk_i2s0_rx_src", CLK_SET_RATE_PARENT,
			PX30_CLKSEL_CON(59), 0,
			PX30_CLKGATE_CON(17), 1, GFLAGS,
			&px30_i2s0_rx_fracmux),
	COMPOSITE_NODIV(SCLK_I2S0_RX, "clk_i2s0_rx", mux_i2s0_rx_tx_p, CLK_SET_RATE_PARENT,
			PX30_CLKSEL_CON(58), 12, 1, MFLAGS,
			PX30_CLKGATE_CON(17), 2, GFLAGS),
	COMPOSITE_NODIV(0, "clk_i2s0_rx_out_pre", mux_i2s0_rx_out_p, 0,
			PX30_CLKSEL_CON(58), 14, 2, MFLAGS,
			PX30_CLKGATE_CON(17), 3, GFLAGS),
	GATE(SCLK_I2S0_RX_OUT, "clk_i2s0_rx_out", "clk_i2s0_rx_out_pre", CLK_SET_RATE_PARENT,
			PX30_CLKGATE_CON(10), 11, CLK_GATE_HIWORD_MASK),

	COMPOSITE(0, "clk_i2s1_src", mux_gpll_npll_p, 0,
			PX30_CLKSEL_CON(30), 8, 1, MFLAGS, 0, 7, DFLAGS,
			PX30_CLKGATE_CON(10), 0, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_i2s1_frac", "clk_i2s1_src", CLK_SET_RATE_PARENT,
			PX30_CLKSEL_CON(31), 0,
			PX30_CLKGATE_CON(10), 1, GFLAGS,
			&px30_i2s1_fracmux),
	GATE(SCLK_I2S1, "clk_i2s1", "clk_i2s1_mux", CLK_SET_RATE_PARENT,
			PX30_CLKGATE_CON(10), 2, GFLAGS),
	COMPOSITE_NODIV(0, "clk_i2s1_out_pre", mux_i2s1_out_p, CLK_SET_RATE_PARENT,
			PX30_CLKSEL_CON(30), 15, 1, MFLAGS,
			PX30_CLKGATE_CON(10), 3, GFLAGS),
	GATE(SCLK_I2S1_OUT, "clk_i2s1_out", "clk_i2s1_out_pre", CLK_SET_RATE_PARENT,
			PX30_CLKGATE_CON(10), 9, CLK_GATE_HIWORD_MASK),

	COMPOSITE(0, "clk_i2s2_src", mux_gpll_npll_p, 0,
			PX30_CLKSEL_CON(32), 8, 1, MFLAGS, 0, 7, DFLAGS,
			PX30_CLKGATE_CON(10), 4, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_i2s2_frac", "clk_i2s2_src", CLK_SET_RATE_PARENT,
			PX30_CLKSEL_CON(33), 0,
			PX30_CLKGATE_CON(10), 5, GFLAGS,
			&px30_i2s2_fracmux),
	GATE(SCLK_I2S2, "clk_i2s2", "clk_i2s2_mux", CLK_SET_RATE_PARENT,
			PX30_CLKGATE_CON(10), 6, GFLAGS),
	COMPOSITE_NODIV(0, "clk_i2s2_out_pre", mux_i2s2_out_p, 0,
			PX30_CLKSEL_CON(32), 15, 1, MFLAGS,
			PX30_CLKGATE_CON(10), 7, GFLAGS),
	GATE(SCLK_I2S2_OUT, "clk_i2s2_out", "clk_i2s2_out_pre", CLK_SET_RATE_PARENT,
			PX30_CLKGATE_CON(10), 10, CLK_GATE_HIWORD_MASK),

	COMPOSITE(SCLK_UART1_SRC, "clk_uart1_src", mux_uart_src_p, CLK_SET_RATE_NO_REPARENT,
			PX30_CLKSEL_CON(34), 14, 2, MFLAGS, 0, 5, DFLAGS,
			PX30_CLKGATE_CON(10), 12, GFLAGS),
	COMPOSITE_NOMUX_HALFDIV(0, "clk_uart1_np5", "clk_uart1_src", 0,
			PX30_CLKSEL_CON(35), 0, 5, DFLAGS,
			PX30_CLKGATE_CON(10), 13, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_uart1_frac", "clk_uart1_src", CLK_SET_RATE_PARENT,
			PX30_CLKSEL_CON(36), 0,
			PX30_CLKGATE_CON(10), 14, GFLAGS,
			&px30_uart1_fracmux),
	GATE(SCLK_UART1, "clk_uart1", "clk_uart1_mux", CLK_SET_RATE_PARENT,
			PX30_CLKGATE_CON(10), 15, GFLAGS),

	COMPOSITE(SCLK_UART2_SRC, "clk_uart2_src", mux_uart_src_p, 0,
			PX30_CLKSEL_CON(37), 14, 2, MFLAGS, 0, 5, DFLAGS,
			PX30_CLKGATE_CON(11), 0, GFLAGS),
	COMPOSITE_NOMUX_HALFDIV(0, "clk_uart2_np5", "clk_uart2_src", 0,
			PX30_CLKSEL_CON(38), 0, 5, DFLAGS,
			PX30_CLKGATE_CON(11), 1, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_uart2_frac", "clk_uart2_src", CLK_SET_RATE_PARENT,
			PX30_CLKSEL_CON(39), 0,
			PX30_CLKGATE_CON(11), 2, GFLAGS,
			&px30_uart2_fracmux),
	GATE(SCLK_UART2, "clk_uart2", "clk_uart2_mux", CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
			PX30_CLKGATE_CON(11), 3, GFLAGS),

	COMPOSITE(0, "clk_uart3_src", mux_uart_src_p, 0,
			PX30_CLKSEL_CON(40), 14, 2, MFLAGS, 0, 5, DFLAGS,
			PX30_CLKGATE_CON(11), 4, GFLAGS),
	COMPOSITE_NOMUX_HALFDIV(0, "clk_uart3_np5", "clk_uart3_src", 0,
			PX30_CLKSEL_CON(41), 0, 5, DFLAGS,
			PX30_CLKGATE_CON(11), 5, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_uart3_frac", "clk_uart3_src", CLK_SET_RATE_PARENT,
			PX30_CLKSEL_CON(42), 0,
			PX30_CLKGATE_CON(11), 6, GFLAGS,
			&px30_uart3_fracmux),
	GATE(SCLK_UART3, "clk_uart3", "clk_uart3_mux", CLK_SET_RATE_PARENT,
			PX30_CLKGATE_CON(11), 7, GFLAGS),

	COMPOSITE(0, "clk_uart4_src", mux_uart_src_p, 0,
			PX30_CLKSEL_CON(43), 14, 2, MFLAGS, 0, 5, DFLAGS,
			PX30_CLKGATE_CON(11), 8, GFLAGS),
	COMPOSITE_NOMUX_HALFDIV(0, "clk_uart4_np5", "clk_uart4_src", 0,
			PX30_CLKSEL_CON(44), 0, 5, DFLAGS,
			PX30_CLKGATE_CON(11), 9, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_uart4_frac", "clk_uart4_src", CLK_SET_RATE_PARENT,
			PX30_CLKSEL_CON(45), 0,
			PX30_CLKGATE_CON(11), 10, GFLAGS,
			&px30_uart4_fracmux),
	GATE(SCLK_UART4, "clk_uart4", "clk_uart4_mux", CLK_SET_RATE_PARENT,
			PX30_CLKGATE_CON(11), 11, GFLAGS),

	COMPOSITE(0, "clk_uart5_src", mux_uart_src_p, 0,
			PX30_CLKSEL_CON(46), 14, 2, MFLAGS, 0, 5, DFLAGS,
			PX30_CLKGATE_CON(11), 12, GFLAGS),
	COMPOSITE_NOMUX_HALFDIV(0, "clk_uart5_np5", "clk_uart5_src", 0,
			PX30_CLKSEL_CON(47), 0, 5, DFLAGS,
			PX30_CLKGATE_CON(11), 13, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_uart5_frac", "clk_uart5_src", CLK_SET_RATE_PARENT,
			PX30_CLKSEL_CON(48), 0,
			PX30_CLKGATE_CON(11), 14, GFLAGS,
			&px30_uart5_fracmux),
	GATE(SCLK_UART5, "clk_uart5", "clk_uart5_mux", CLK_SET_RATE_PARENT,
			PX30_CLKGATE_CON(11), 15, GFLAGS),

	COMPOSITE(SCLK_I2C0, "clk_i2c0", mux_gpll_xin24m_p, 0,
			PX30_CLKSEL_CON(49), 7, 1, MFLAGS, 0, 7, DFLAGS,
			PX30_CLKGATE_CON(12), 0, GFLAGS),
	COMPOSITE(SCLK_I2C1, "clk_i2c1", mux_gpll_xin24m_p, 0,
			PX30_CLKSEL_CON(49), 15, 1, MFLAGS, 8, 7, DFLAGS,
			PX30_CLKGATE_CON(12), 1, GFLAGS),
	COMPOSITE(SCLK_I2C2, "clk_i2c2", mux_gpll_xin24m_p, 0,
			PX30_CLKSEL_CON(50), 7, 1, MFLAGS, 0, 7, DFLAGS,
			PX30_CLKGATE_CON(12), 2, GFLAGS),
	COMPOSITE(SCLK_I2C3, "clk_i2c3", mux_gpll_xin24m_p, 0,
			PX30_CLKSEL_CON(50), 15, 1, MFLAGS, 8, 7, DFLAGS,
			PX30_CLKGATE_CON(12), 3, GFLAGS),
	COMPOSITE(SCLK_PWM0, "clk_pwm0", mux_gpll_xin24m_p, 0,
			PX30_CLKSEL_CON(52), 7, 1, MFLAGS, 0, 7, DFLAGS,
			PX30_CLKGATE_CON(12), 5, GFLAGS),
	COMPOSITE(SCLK_PWM1, "clk_pwm1", mux_gpll_xin24m_p, 0,
			PX30_CLKSEL_CON(52), 15, 1, MFLAGS, 8, 7, DFLAGS,
			PX30_CLKGATE_CON(12), 6, GFLAGS),
	COMPOSITE(SCLK_SPI0, "clk_spi0", mux_gpll_xin24m_p, 0,
			PX30_CLKSEL_CON(53), 7, 1, MFLAGS, 0, 7, DFLAGS,
			PX30_CLKGATE_CON(12), 7, GFLAGS),
	COMPOSITE(SCLK_SPI1, "clk_spi1", mux_gpll_xin24m_p, 0,
			PX30_CLKSEL_CON(53), 15, 1, MFLAGS, 8, 7, DFLAGS,
			PX30_CLKGATE_CON(12), 8, GFLAGS),

	GATE(SCLK_TIMER0, "sclk_timer0", "xin24m", 0,
			PX30_CLKGATE_CON(13), 0, GFLAGS),
	GATE(SCLK_TIMER1, "sclk_timer1", "xin24m", 0,
			PX30_CLKGATE_CON(13), 1, GFLAGS),
	GATE(SCLK_TIMER2, "sclk_timer2", "xin24m", 0,
			PX30_CLKGATE_CON(13), 2, GFLAGS),
	GATE(SCLK_TIMER3, "sclk_timer3", "xin24m", 0,
			PX30_CLKGATE_CON(13), 3, GFLAGS),
	GATE(SCLK_TIMER4, "sclk_timer4", "xin24m", 0,
			PX30_CLKGATE_CON(13), 4, GFLAGS),
	GATE(SCLK_TIMER5, "sclk_timer5", "xin24m", 0,
			PX30_CLKGATE_CON(13), 5, GFLAGS),

	COMPOSITE_NOMUX(SCLK_TSADC, "clk_tsadc", "xin24m", 0,
			PX30_CLKSEL_CON(54), 0, 11, DFLAGS,
			PX30_CLKGATE_CON(12), 9, GFLAGS),
	COMPOSITE_NOMUX(SCLK_SARADC, "clk_saradc", "xin24m", 0,
			PX30_CLKSEL_CON(55), 0, 11, DFLAGS,
			PX30_CLKGATE_CON(12), 10, GFLAGS),
	COMPOSITE_NOMUX(SCLK_OTP, "clk_otp", "xin24m", 0,
			PX30_CLKSEL_CON(56), 0, 3, DFLAGS,
			PX30_CLKGATE_CON(12), 11, GFLAGS),
	COMPOSITE_NOMUX(SCLK_OTP_USR, "clk_otp_usr", "clk_otp", 0,
			PX30_CLKSEL_CON(56), 4, 2, DFLAGS,
			PX30_CLKGATE_CON(13), 6, GFLAGS),

	GATE(0, "clk_cpu_boost", "xin24m", CLK_IGNORE_UNUSED,
			PX30_CLKGATE_CON(12), 12, GFLAGS),

	/* PD_CRYPTO */
	GATE(0, "aclk_crypto_pre", "aclk_bus_pre", 0,
			PX30_CLKGATE_CON(8), 12, GFLAGS),
	GATE(0, "hclk_crypto_pre", "hclk_bus_pre", 0,
			PX30_CLKGATE_CON(8), 13, GFLAGS),
	COMPOSITE(SCLK_CRYPTO, "clk_crypto", mux_gpll_cpll_npll_p, 0,
			PX30_CLKSEL_CON(25), 6, 2, MFLAGS, 0, 5, DFLAGS,
			PX30_CLKGATE_CON(8), 14, GFLAGS),
	COMPOSITE(SCLK_CRYPTO_APK, "clk_crypto_apk", mux_gpll_cpll_npll_p, 0,
			PX30_CLKSEL_CON(25), 14, 2, MFLAGS, 8, 5, DFLAGS,
			PX30_CLKGATE_CON(8), 15, GFLAGS),

	/*
	 * Clock-Architecture Diagram 9
	 */

	/* PD_BUS_TOP */
	GATE(0, "pclk_top_niu", "pclk_top_pre", CLK_IGNORE_UNUSED, PX30_CLKGATE_CON(16), 0, GFLAGS),
	GATE(0, "pclk_top_cru", "pclk_top_pre", CLK_IGNORE_UNUSED, PX30_CLKGATE_CON(16), 1, GFLAGS),
	GATE(PCLK_OTP_PHY, "pclk_otp_phy", "pclk_top_pre", CLK_IGNORE_UNUSED, PX30_CLKGATE_CON(16), 2, GFLAGS),
	GATE(0, "pclk_ddrphy", "pclk_top_pre", CLK_IGNORE_UNUSED, PX30_CLKGATE_CON(16), 3, GFLAGS),
	GATE(PCLK_MIPIDSIPHY, "pclk_mipidsiphy", "pclk_top_pre", 0, PX30_CLKGATE_CON(16), 4, GFLAGS),
	GATE(PCLK_MIPICSIPHY, "pclk_mipicsiphy", "pclk_top_pre", 0, PX30_CLKGATE_CON(16), 5, GFLAGS),
	GATE(PCLK_USB_GRF, "pclk_usb_grf", "pclk_top_pre", CLK_IS_CRITICAL, PX30_CLKGATE_CON(16), 6, GFLAGS),
	GATE(0, "pclk_cpu_hoost", "pclk_top_pre", CLK_IGNORE_UNUSED, PX30_CLKGATE_CON(16), 7, GFLAGS),

	/* PD_VI */
	GATE(0, "aclk_vi_niu", "aclk_vi_pre", CLK_IS_CRITICAL, PX30_CLKGATE_CON(4), 15, GFLAGS),
	GATE(ACLK_CIF, "aclk_cif", "aclk_vi_pre", 0, PX30_CLKGATE_CON(5), 1, GFLAGS),
	GATE(ACLK_ISP, "aclk_isp", "aclk_vi_pre", 0, PX30_CLKGATE_CON(5), 3, GFLAGS),
	GATE(0, "hclk_vi_niu", "hclk_vi_pre", CLK_IS_CRITICAL, PX30_CLKGATE_CON(5), 0, GFLAGS),
	GATE(HCLK_CIF, "hclk_cif", "hclk_vi_pre", 0, PX30_CLKGATE_CON(5), 2, GFLAGS),
	GATE(HCLK_ISP, "hclk_isp", "hclk_vi_pre", 0, PX30_CLKGATE_CON(5), 4, GFLAGS),

	/* PD_VO */
	GATE(0, "aclk_vo_niu", "aclk_vo_pre", CLK_IS_CRITICAL, PX30_CLKGATE_CON(3), 0, GFLAGS),
	GATE(ACLK_VOPB, "aclk_vopb", "aclk_vo_pre", 0, PX30_CLKGATE_CON(3), 3, GFLAGS),
	GATE(ACLK_RGA, "aclk_rga", "aclk_vo_pre", 0, PX30_CLKGATE_CON(3), 7, GFLAGS),
	GATE(ACLK_VOPL, "aclk_vopl", "aclk_vo_pre", 0, PX30_CLKGATE_CON(3), 5, GFLAGS),

	GATE(0, "hclk_vo_niu", "hclk_vo_pre", CLK_IS_CRITICAL, PX30_CLKGATE_CON(3), 1, GFLAGS),
	GATE(HCLK_VOPB, "hclk_vopb", "hclk_vo_pre", 0, PX30_CLKGATE_CON(3), 4, GFLAGS),
	GATE(HCLK_RGA, "hclk_rga", "hclk_vo_pre", 0, PX30_CLKGATE_CON(3), 8, GFLAGS),
	GATE(HCLK_VOPL, "hclk_vopl", "hclk_vo_pre", 0, PX30_CLKGATE_CON(3), 6, GFLAGS),

	GATE(0, "pclk_vo_niu", "pclk_vo_pre", CLK_IS_CRITICAL, PX30_CLKGATE_CON(3), 2, GFLAGS),
	GATE(PCLK_MIPI_DSI, "pclk_mipi_dsi", "pclk_vo_pre", 0, PX30_CLKGATE_CON(3), 9, GFLAGS),

	/* PD_BUS */
	GATE(0, "aclk_bus_niu", "aclk_bus_pre", CLK_IGNORE_UNUSED, PX30_CLKGATE_CON(13), 8, GFLAGS),
	GATE(0, "aclk_intmem", "aclk_bus_pre", CLK_IGNORE_UNUSED, PX30_CLKGATE_CON(13), 11, GFLAGS),
	GATE(ACLK_GIC, "aclk_gic", "aclk_bus_pre", CLK_IGNORE_UNUSED, PX30_CLKGATE_CON(13), 12, GFLAGS),
	GATE(ACLK_DCF, "aclk_dcf", "aclk_bus_pre", 0, PX30_CLKGATE_CON(13), 15, GFLAGS),

	/* aclk_dmac is controlled by sgrf_soc_con1[11]. */
	SGRF_GATE(ACLK_DMAC, "aclk_dmac", "aclk_bus_pre"),

	GATE(0, "hclk_bus_niu", "hclk_bus_pre", CLK_IGNORE_UNUSED, PX30_CLKGATE_CON(13), 9, GFLAGS),
	GATE(0, "hclk_rom", "hclk_bus_pre", CLK_IGNORE_UNUSED, PX30_CLKGATE_CON(13), 14, GFLAGS),
	GATE(HCLK_PDM, "hclk_pdm", "hclk_bus_pre", 0, PX30_CLKGATE_CON(14), 1, GFLAGS),
	GATE(HCLK_I2S0, "hclk_i2s0", "hclk_bus_pre", 0, PX30_CLKGATE_CON(14), 2, GFLAGS),
	GATE(HCLK_I2S1, "hclk_i2s1", "hclk_bus_pre", 0, PX30_CLKGATE_CON(14), 3, GFLAGS),
	GATE(HCLK_I2S2, "hclk_i2s2", "hclk_bus_pre", 0, PX30_CLKGATE_CON(14), 4, GFLAGS),

	GATE(0, "pclk_bus_niu", "pclk_bus_pre", CLK_IGNORE_UNUSED, PX30_CLKGATE_CON(13), 10, GFLAGS),
	GATE(PCLK_DCF, "pclk_dcf", "pclk_bus_pre", 0, PX30_CLKGATE_CON(14), 0, GFLAGS),
	GATE(PCLK_UART1, "pclk_uart1", "pclk_bus_pre", 0, PX30_CLKGATE_CON(14), 5, GFLAGS),
	GATE(PCLK_UART2, "pclk_uart2", "pclk_bus_pre", CLK_IS_CRITICAL, PX30_CLKGATE_CON(14), 6, GFLAGS),
	GATE(PCLK_UART3, "pclk_uart3", "pclk_bus_pre", 0, PX30_CLKGATE_CON(14), 7, GFLAGS),
	GATE(PCLK_UART4, "pclk_uart4", "pclk_bus_pre", 0, PX30_CLKGATE_CON(14), 8, GFLAGS),
	GATE(PCLK_UART5, "pclk_uart5", "pclk_bus_pre", 0, PX30_CLKGATE_CON(14), 9, GFLAGS),
	GATE(PCLK_I2C0, "pclk_i2c0", "pclk_bus_pre", 0, PX30_CLKGATE_CON(14), 10, GFLAGS),
	GATE(PCLK_I2C1, "pclk_i2c1", "pclk_bus_pre", 0, PX30_CLKGATE_CON(14), 11, GFLAGS),
	GATE(PCLK_I2C2, "pclk_i2c2", "pclk_bus_pre", 0, PX30_CLKGATE_CON(14), 12, GFLAGS),
	GATE(PCLK_I2C3, "pclk_i2c3", "pclk_bus_pre", 0, PX30_CLKGATE_CON(14), 13, GFLAGS),
	GATE(PCLK_I2C4, "pclk_i2c4", "pclk_bus_pre", 0, PX30_CLKGATE_CON(14), 14, GFLAGS),
	GATE(PCLK_PWM0, "pclk_pwm0", "pclk_bus_pre", 0, PX30_CLKGATE_CON(14), 15, GFLAGS),
	GATE(PCLK_PWM1, "pclk_pwm1", "pclk_bus_pre", 0, PX30_CLKGATE_CON(15), 0, GFLAGS),
	GATE(PCLK_SPI0, "pclk_spi0", "pclk_bus_pre", 0, PX30_CLKGATE_CON(15), 1, GFLAGS),
	GATE(PCLK_SPI1, "pclk_spi1", "pclk_bus_pre", 0, PX30_CLKGATE_CON(15), 2, GFLAGS),
	GATE(PCLK_SARADC, "pclk_saradc", "pclk_bus_pre", 0, PX30_CLKGATE_CON(15), 3, GFLAGS),
	GATE(PCLK_TSADC, "pclk_tsadc", "pclk_bus_pre", 0, PX30_CLKGATE_CON(15), 4, GFLAGS),
	GATE(PCLK_TIMER, "pclk_timer", "pclk_bus_pre", 0, PX30_CLKGATE_CON(15), 5, GFLAGS),
	GATE(PCLK_OTP_NS, "pclk_otp_ns", "pclk_bus_pre", CLK_IGNORE_UNUSED, PX30_CLKGATE_CON(15), 6, GFLAGS),
	GATE(PCLK_WDT_NS, "pclk_wdt_ns", "pclk_bus_pre", CLK_IGNORE_UNUSED, PX30_CLKGATE_CON(15), 7, GFLAGS),
	GATE(PCLK_GPIO1, "pclk_gpio1", "pclk_bus_pre", 0, PX30_CLKGATE_CON(15), 8, GFLAGS),
	GATE(PCLK_GPIO2, "pclk_gpio2", "pclk_bus_pre", 0, PX30_CLKGATE_CON(15), 9, GFLAGS),
	GATE(PCLK_GPIO3, "pclk_gpio3", "pclk_bus_pre", 0, PX30_CLKGATE_CON(15), 10, GFLAGS),
	GATE(0, "pclk_grf", "pclk_bus_pre", CLK_IGNORE_UNUSED, PX30_CLKGATE_CON(15), 11, GFLAGS),
	GATE(0, "pclk_sgrf", "pclk_bus_pre", CLK_IGNORE_UNUSED, PX30_CLKGATE_CON(15), 12, GFLAGS),

	/* PD_VPU */
	GATE(0, "hclk_vpu_niu", "hclk_vpu_pre", CLK_IGNORE_UNUSED, PX30_CLKGATE_CON(4), 7, GFLAGS),
	GATE(HCLK_VPU, "hclk_vpu", "hclk_vpu_pre", 0, PX30_CLKGATE_CON(4), 6, GFLAGS),
	GATE(0, "aclk_vpu_niu", "aclk_vpu_pre", CLK_IGNORE_UNUSED, PX30_CLKGATE_CON(4), 5, GFLAGS),
	GATE(ACLK_VPU, "aclk_vpu", "aclk_vpu_pre", 0, PX30_CLKGATE_CON(4), 4, GFLAGS),

	/* PD_CRYPTO */
	GATE(0, "hclk_crypto_niu", "hclk_crypto_pre", CLK_IGNORE_UNUSED, PX30_CLKGATE_CON(9), 3, GFLAGS),
	GATE(HCLK_CRYPTO, "hclk_crypto", "hclk_crypto_pre", 0, PX30_CLKGATE_CON(9), 5, GFLAGS),
	GATE(0, "aclk_crypto_niu", "aclk_crypto_pre", CLK_IGNORE_UNUSED, PX30_CLKGATE_CON(9), 2, GFLAGS),
	GATE(ACLK_CRYPTO, "aclk_crypto", "aclk_crypto_pre", 0, PX30_CLKGATE_CON(9), 4, GFLAGS),

	/* PD_SDCARD */
	GATE(0, "hclk_sdmmc_niu", "hclk_sdmmc_pre", CLK_IGNORE_UNUSED, PX30_CLKGATE_CON(7), 0, GFLAGS),
	GATE(HCLK_SDMMC, "hclk_sdmmc", "hclk_sdmmc_pre", 0, PX30_CLKGATE_CON(7), 1, GFLAGS),

	/* PD_PERI */
	GATE(0, "aclk_peri_niu", "aclk_peri_pre", CLK_IS_CRITICAL, PX30_CLKGATE_CON(5), 9, GFLAGS),

	/* PD_MMC_NAND */
	GATE(HCLK_NANDC, "hclk_nandc", "hclk_mmc_nand", 0, PX30_CLKGATE_CON(5), 15, GFLAGS),
	GATE(0, "hclk_mmc_nand_niu", "hclk_mmc_nand", CLK_IGNORE_UNUSED, PX30_CLKGATE_CON(6), 8, GFLAGS),
	GATE(HCLK_SDIO, "hclk_sdio", "hclk_mmc_nand", 0, PX30_CLKGATE_CON(6), 9, GFLAGS),
	GATE(HCLK_EMMC, "hclk_emmc", "hclk_mmc_nand", 0, PX30_CLKGATE_CON(6), 10, GFLAGS),
	GATE(HCLK_SFC, "hclk_sfc", "hclk_mmc_nand", 0, PX30_CLKGATE_CON(6), 11, GFLAGS),

	/* PD_USB */
	GATE(0, "hclk_usb_niu", "hclk_usb", CLK_IS_CRITICAL, PX30_CLKGATE_CON(7), 4, GFLAGS),
	GATE(HCLK_OTG, "hclk_otg", "hclk_usb", 0, PX30_CLKGATE_CON(7), 5, GFLAGS),
	GATE(HCLK_HOST, "hclk_host", "hclk_usb", 0, PX30_CLKGATE_CON(7), 6, GFLAGS),
	GATE(HCLK_HOST_ARB, "hclk_host_arb", "hclk_usb", CLK_IGNORE_UNUSED, PX30_CLKGATE_CON(7), 8, GFLAGS),

	/* PD_GMAC */
	GATE(0, "aclk_gmac_niu", "aclk_gmac_pre", CLK_IGNORE_UNUSED,
			PX30_CLKGATE_CON(8), 0, GFLAGS),
	GATE(ACLK_GMAC, "aclk_gmac", "aclk_gmac_pre", 0,
			PX30_CLKGATE_CON(8), 2, GFLAGS),
	GATE(0, "pclk_gmac_niu", "pclk_gmac_pre", CLK_IGNORE_UNUSED,
			PX30_CLKGATE_CON(8), 1, GFLAGS),
	GATE(PCLK_GMAC, "pclk_gmac", "pclk_gmac_pre", 0,
			PX30_CLKGATE_CON(8), 3, GFLAGS),
};

static struct rockchip_clk_branch px30_gpu_src_clk[] __initdata = {
	COMPOSITE(0, "clk_gpu_src", mux_gpll_dmycpll_usb480m_dmynpll_p, 0,
			PX30_CLKSEL_CON(1), 6, 2, MFLAGS, 0, 4, DFLAGS,
			PX30_CLKGATE_CON(0), 8, GFLAGS),
};

static struct rockchip_clk_branch rk3326_gpu_src_clk[] __initdata = {
	COMPOSITE(0, "clk_gpu_src", mux_gpll_dmycpll_usb480m_npll_p, 0,
			PX30_CLKSEL_CON(1), 6, 2, MFLAGS, 0, 4, DFLAGS,
			PX30_CLKGATE_CON(0), 8, GFLAGS),
};

static struct rockchip_clk_branch px30_clk_pmu_branches[] __initdata = {
	/*
	 * Clock-Architecture Diagram 2
	 */

	COMPOSITE_FRACMUX(0, "clk_rtc32k_frac", "xin24m", CLK_IGNORE_UNUSED,
			PX30_PMU_CLKSEL_CON(1), 0,
			PX30_PMU_CLKGATE_CON(0), 13, GFLAGS,
			&px30_rtc32k_pmu_fracmux),

	COMPOSITE_NOMUX(XIN24M_DIV, "xin24m_div", "xin24m", CLK_IGNORE_UNUSED,
			PX30_PMU_CLKSEL_CON(0), 8, 5, DFLAGS,
			PX30_PMU_CLKGATE_CON(0), 12, GFLAGS),

	COMPOSITE_NOMUX(0, "clk_wifi_pmu_src", "gpll", 0,
			PX30_PMU_CLKSEL_CON(2), 8, 6, DFLAGS,
			PX30_PMU_CLKGATE_CON(0), 14, GFLAGS),
	COMPOSITE_NODIV(SCLK_WIFI_PMU, "clk_wifi_pmu", mux_wifi_pmu_p, CLK_SET_RATE_PARENT,
			PX30_PMU_CLKSEL_CON(2), 15, 1, MFLAGS,
			PX30_PMU_CLKGATE_CON(0), 15, GFLAGS),

	COMPOSITE(0, "clk_uart0_pmu_src", mux_uart_src_p, 0,
			PX30_PMU_CLKSEL_CON(3), 14, 2, MFLAGS, 0, 5, DFLAGS,
			PX30_PMU_CLKGATE_CON(1), 0, GFLAGS),
	COMPOSITE_NOMUX_HALFDIV(0, "clk_uart0_np5", "clk_uart0_pmu_src", 0,
			PX30_PMU_CLKSEL_CON(4), 0, 5, DFLAGS,
			PX30_PMU_CLKGATE_CON(1), 1, GFLAGS),
	COMPOSITE_FRACMUX(0, "clk_uart0_frac", "clk_uart0_pmu_src", CLK_SET_RATE_PARENT,
			PX30_PMU_CLKSEL_CON(5), 0,
			PX30_PMU_CLKGATE_CON(1), 2, GFLAGS,
			&px30_uart0_pmu_fracmux),
	GATE(SCLK_UART0_PMU, "clk_uart0_pmu", "clk_uart0_pmu_mux", CLK_SET_RATE_PARENT,
			PX30_PMU_CLKGATE_CON(1), 3, GFLAGS),

	GATE(SCLK_PVTM_PMU, "clk_pvtm_pmu", "xin24m", 0,
			PX30_PMU_CLKGATE_CON(1), 4, GFLAGS),

	COMPOSITE_NOMUX(PCLK_PMU_PRE, "pclk_pmu_pre", "gpll", CLK_IS_CRITICAL,
			PX30_PMU_CLKSEL_CON(0), 0, 5, DFLAGS,
			PX30_PMU_CLKGATE_CON(0), 0, GFLAGS),

	COMPOSITE_NOMUX(SCLK_REF24M_PMU, "clk_ref24m_pmu", "gpll", 0,
			PX30_PMU_CLKSEL_CON(2), 0, 6, DFLAGS,
			PX30_PMU_CLKGATE_CON(1), 8, GFLAGS),
	COMPOSITE_NODIV(SCLK_USBPHY_REF, "clk_usbphy_ref", mux_usbphy_ref_p, CLK_SET_RATE_PARENT,
			PX30_PMU_CLKSEL_CON(2), 6, 1, MFLAGS,
			PX30_PMU_CLKGATE_CON(1), 9, GFLAGS),
	COMPOSITE_NODIV(SCLK_MIPIDSIPHY_REF, "clk_mipidsiphy_ref", mux_mipidsiphy_ref_p, CLK_SET_RATE_PARENT,
			PX30_PMU_CLKSEL_CON(2), 7, 1, MFLAGS,
			PX30_PMU_CLKGATE_CON(1), 10, GFLAGS),

	/*
	 * Clock-Architecture Diagram 9
	 */

	/* PD_PMU */
	GATE(0, "pclk_pmu_niu", "pclk_pmu_pre", CLK_IGNORE_UNUSED, PX30_PMU_CLKGATE_CON(0), 1, GFLAGS),
	GATE(0, "pclk_pmu_sgrf", "pclk_pmu_pre", CLK_IGNORE_UNUSED, PX30_PMU_CLKGATE_CON(0), 2, GFLAGS),
	GATE(0, "pclk_pmu_grf", "pclk_pmu_pre", CLK_IGNORE_UNUSED, PX30_PMU_CLKGATE_CON(0), 3, GFLAGS),
	GATE(0, "pclk_pmu", "pclk_pmu_pre", CLK_IGNORE_UNUSED, PX30_PMU_CLKGATE_CON(0), 4, GFLAGS),
	GATE(0, "pclk_pmu_mem", "pclk_pmu_pre", CLK_IGNORE_UNUSED, PX30_PMU_CLKGATE_CON(0), 5, GFLAGS),
	GATE(PCLK_GPIO0_PMU, "pclk_gpio0_pmu", "pclk_pmu_pre", 0, PX30_PMU_CLKGATE_CON(0), 6, GFLAGS),
	GATE(PCLK_UART0_PMU, "pclk_uart0_pmu", "pclk_pmu_pre", 0, PX30_PMU_CLKGATE_CON(0), 7, GFLAGS),
	GATE(0, "pclk_cru_pmu", "pclk_pmu_pre", CLK_IGNORE_UNUSED, PX30_PMU_CLKGATE_CON(0), 8, GFLAGS),
};

static struct rockchip_clk_provider *cru_ctx;
static void __init px30_clk_init(struct device_node *np)
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

	rockchip_clk_register_plls(ctx, px30_pll_clks,
				   ARRAY_SIZE(px30_pll_clks),
				   PX30_GRF_SOC_STATUS0);
	rockchip_clk_register_branches(ctx, px30_clk_branches,
				       ARRAY_SIZE(px30_clk_branches));
	if (of_machine_is_compatible("rockchip,px30"))
		rockchip_clk_register_branches(ctx, px30_gpu_src_clk,
				       ARRAY_SIZE(px30_gpu_src_clk));
	else
		rockchip_clk_register_branches(ctx, rk3326_gpu_src_clk,
				       ARRAY_SIZE(rk3326_gpu_src_clk));

	rockchip_register_softrst(np, 12, reg_base + PX30_SOFTRST_CON(0),
				  ROCKCHIP_SOFTRST_HIWORD_MASK);

	rockchip_register_restart_notifier(ctx, PX30_GLB_SRST_FST, NULL);

	rockchip_clk_of_add_provider(np, ctx);

	cru_ctx = ctx;
}
CLK_OF_DECLARE(px30_cru, "rockchip,px30-cru", px30_clk_init);

static void __init px30_pmu_clk_init(struct device_node *np)
{
	struct rockchip_clk_provider *ctx;
	void __iomem *reg_base;
	struct clk **pmucru_clks, **cru_clks;

	reg_base = of_iomap(np, 0);
	if (!reg_base) {
		pr_err("%s: could not map cru pmu region\n", __func__);
		return;
	}

	ctx = rockchip_clk_init(np, reg_base, CLKPMU_NR_CLKS);
	if (IS_ERR(ctx)) {
		pr_err("%s: rockchip pmu clk init failed\n", __func__);
		return;
	}
	pmucru_clks = ctx->clk_data.clks;
	cru_clks = cru_ctx->clk_data.clks;

	rockchip_clk_register_plls(ctx, px30_pmu_pll_clks,
				   ARRAY_SIZE(px30_pmu_pll_clks), PX30_GRF_SOC_STATUS0);

	rockchip_clk_register_armclk(cru_ctx, ARMCLK, "armclk",
				     2, cru_clks[PLL_APLL], pmucru_clks[PLL_GPLL],
				     &px30_cpuclk_data, px30_cpuclk_rates,
				     ARRAY_SIZE(px30_cpuclk_rates));

	rockchip_clk_register_branches(ctx, px30_clk_pmu_branches,
				       ARRAY_SIZE(px30_clk_pmu_branches));

	rockchip_clk_of_add_provider(np, ctx);
}
CLK_OF_DECLARE(px30_cru_pmu, "rockchip,px30-pmucru", px30_pmu_clk_init);

struct clk_px30_inits {
	void (*inits)(struct device_node *np);
};

static const struct clk_px30_inits clk_px30_init = {
	.inits = px30_clk_init,
};

static const struct clk_px30_inits clk_px30_pmu_init = {
	.inits = px30_pmu_clk_init,
};

static const struct of_device_id clk_px30_match_table[] = {
	{
		.compatible = "rockchip,px30-cru",
		.data = &clk_px30_init,
	}, {
		.compatible = "rockchip,px30-pmucru",
		.data = &clk_px30_pmu_init,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, clk_px30_match_table);

static int __init clk_px30_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match;
	const struct clk_px30_inits *init_data;

	match = of_match_device(clk_px30_match_table, &pdev->dev);
	if (!match || !match->data)
		return -EINVAL;

	init_data = match->data;
	if (init_data->inits)
		init_data->inits(np);

	return 0;
}

static struct platform_driver clk_px30_driver = {
	.driver		= {
		.name	= "clk-px30",
		.of_match_table = clk_px30_match_table,
	},
};
builtin_platform_driver_probe(clk_px30_driver, clk_px30_probe);

MODULE_DESCRIPTION("Rockchip PX30 Clock Driver");
MODULE_LICENSE("GPL");
