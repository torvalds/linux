/*
 * Copyright (c) 2014 MundoReader S.L.
 * Author: Heiko Stuebner <heiko@sntech.de>
 *
 * Copyright (c) 2015 Rockchip Electronics Co. Ltd.
 * Author: Xing Zheng <zhengxing@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/syscore_ops.h>
#include <dt-bindings/clock/rk3036-cru.h>
#include "clk.h"

#define RK3036_GRF_SOC_STATUS0	0x14c

enum rk3036_plls {
	apll, dpll, gpll,
};

static struct rockchip_pll_rate_table rk3036_pll_rates[] = {
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

#define RK3036_DIV_CPU_MASK		0x1f
#define RK3036_DIV_CPU_SHIFT		8

#define RK3036_DIV_PERI_MASK		0xf
#define RK3036_DIV_PERI_SHIFT		0
#define RK3036_DIV_ACLK_MASK		0x7
#define RK3036_DIV_ACLK_SHIFT		4
#define RK3036_DIV_HCLK_MASK		0x3
#define RK3036_DIV_HCLK_SHIFT		8
#define RK3036_DIV_PCLK_MASK		0x7
#define RK3036_DIV_PCLK_SHIFT		12

#define RK3036_CLKSEL1(_core_periph_div)					\
	{									\
		.reg = RK2928_CLKSEL_CON(1),					\
		.val = HIWORD_UPDATE(_core_periph_div, RK3036_DIV_PERI_MASK,	\
				RK3036_DIV_PERI_SHIFT)				\
	}

#define RK3036_CPUCLK_RATE(_prate, _core_periph_div)			\
	{								\
		.prate = _prate,					\
		.divs = {						\
			RK3036_CLKSEL1(_core_periph_div),		\
		},							\
	}

static struct rockchip_cpuclk_rate_table rk3036_cpuclk_rates[] __initdata = {
	RK3036_CPUCLK_RATE(816000000, 4),
	RK3036_CPUCLK_RATE(600000000, 4),
	RK3036_CPUCLK_RATE(312000000, 4),
};

static const struct rockchip_cpuclk_reg_data rk3036_cpuclk_data = {
	.core_reg = RK2928_CLKSEL_CON(0),
	.div_core_shift = 0,
	.div_core_mask = 0x1f,
	.mux_core_alt = 1,
	.mux_core_main = 0,
	.mux_core_shift = 7,
	.mux_core_mask = 0x1,
};

PNAME(mux_pll_p)		= { "xin24m", "xin24m" };

PNAME(mux_armclk_p)		= { "apll", "gpll_armclk" };
PNAME(mux_busclk_p)		= { "apll", "dpll_cpu", "gpll_cpu" };
PNAME(mux_ddrphy_p)		= { "dpll_ddr", "gpll_ddr" };
PNAME(mux_pll_src_3plls_p)	= { "apll", "dpll", "gpll" };
PNAME(mux_timer_p)		= { "xin24m", "pclk_peri_src" };

PNAME(mux_pll_src_apll_dpll_gpll_usb480m_p)	= { "apll", "dpll", "gpll", "usb480m" };

PNAME(mux_mmc_src_p)	= { "apll", "dpll", "gpll", "xin24m" };
PNAME(mux_i2s_pre_p)	= { "i2s_src", "i2s_frac", "ext_i2s", "xin12m" };
PNAME(mux_i2s_clkout_p)	= { "i2s_pre", "xin12m" };
PNAME(mux_spdif_p)	= { "spdif_src", "spdif_frac", "xin12m" };
PNAME(mux_uart0_p)	= { "uart0_src", "uart0_frac", "xin24m" };
PNAME(mux_uart1_p)	= { "uart1_src", "uart1_frac", "xin24m" };
PNAME(mux_uart2_p)	= { "uart2_src", "uart2_frac", "xin24m" };
PNAME(mux_mac_p)	= { "mac_pll_src", "rmii_clkin" };
PNAME(mux_dclk_p)	= { "dclk_lcdc", "dclk_cru" };

static struct rockchip_pll_clock rk3036_pll_clks[] __initdata = {
	[apll] = PLL(pll_rk3036, PLL_APLL, "apll", mux_pll_p, 0, RK2928_PLL_CON(0),
		     RK2928_MODE_CON, 0, 5, 0, rk3036_pll_rates),
	[dpll] = PLL(pll_rk3036, PLL_DPLL, "dpll", mux_pll_p, 0, RK2928_PLL_CON(4),
		     RK2928_MODE_CON, 4, 4, 0, NULL),
	[gpll] = PLL(pll_rk3036, PLL_GPLL, "gpll", mux_pll_p, 0, RK2928_PLL_CON(12),
		     RK2928_MODE_CON, 12, 6, ROCKCHIP_PLL_SYNC_RATE, rk3036_pll_rates),
};

#define MFLAGS CLK_MUX_HIWORD_MASK
#define DFLAGS CLK_DIVIDER_HIWORD_MASK
#define GFLAGS (CLK_GATE_HIWORD_MASK | CLK_GATE_SET_TO_DISABLE)

static struct rockchip_clk_branch rk3036_uart0_fracmux __initdata =
	MUX(SCLK_UART0, "sclk_uart0", mux_uart0_p, CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(13), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3036_uart1_fracmux __initdata =
	MUX(SCLK_UART1, "sclk_uart1", mux_uart1_p, CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(14), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3036_uart2_fracmux __initdata =
	MUX(SCLK_UART2, "sclk_uart2", mux_uart2_p, CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(15), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3036_i2s_fracmux __initdata =
	MUX(0, "i2s_pre", mux_i2s_pre_p, CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(3), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3036_spdif_fracmux __initdata =
	MUX(SCLK_SPDIF, "sclk_spdif", mux_spdif_p, 0,
			RK2928_CLKSEL_CON(5), 8, 2, MFLAGS);

static struct rockchip_clk_branch rk3036_clk_branches[] __initdata = {
	/*
	 * Clock-Architecture Diagram 1
	 */

	GATE(0, "gpll_armclk", "gpll", CLK_IGNORE_UNUSED,
			RK2928_CLKGATE_CON(0), 6, GFLAGS),

	FACTOR(0, "xin12m", "xin24m", 0, 1, 2),

	/*
	 * Clock-Architecture Diagram 2
	 */

	GATE(0, "dpll_ddr", "dpll", CLK_IGNORE_UNUSED,
			RK2928_CLKGATE_CON(0), 2, GFLAGS),
	GATE(0, "gpll_ddr", "gpll", CLK_IGNORE_UNUSED,
			RK2928_CLKGATE_CON(0), 8, GFLAGS),
	COMPOSITE_NOGATE(0, "ddrphy2x", mux_ddrphy_p, CLK_IGNORE_UNUSED,
			RK2928_CLKSEL_CON(26), 8, 1, MFLAGS, 0, 2, DFLAGS | CLK_DIVIDER_POWER_OF_TWO),
	FACTOR(0, "ddrphy", "ddrphy2x", 0, 1, 2),

	COMPOSITE_NOMUX(0, "pclk_dbg", "armclk", CLK_IGNORE_UNUSED,
			RK2928_CLKSEL_CON(1), 0, 4, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RK2928_CLKGATE_CON(0), 7, GFLAGS),
	COMPOSITE_NOMUX(0, "aclk_core_pre", "armclk", CLK_IGNORE_UNUSED,
			RK2928_CLKSEL_CON(1), 4, 3, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RK2928_CLKGATE_CON(0), 7, GFLAGS),

	GATE(0, "dpll_cpu", "dpll", 0, RK2928_CLKGATE_CON(10), 8, GFLAGS),
	GATE(0, "gpll_cpu", "gpll", 0, RK2928_CLKGATE_CON(0), 1, GFLAGS),
	COMPOSITE_NOGATE(0, "aclk_cpu_src", mux_busclk_p, 0,
			RK2928_CLKSEL_CON(0), 14, 2, MFLAGS, 8, 5, DFLAGS),
	GATE(ACLK_CPU, "aclk_cpu", "aclk_cpu_src", CLK_IGNORE_UNUSED,
			RK2928_CLKGATE_CON(0), 3, GFLAGS),
	COMPOSITE_NOMUX(PCLK_CPU, "pclk_cpu", "aclk_cpu_src", CLK_IGNORE_UNUSED,
			RK2928_CLKSEL_CON(1), 12, 3, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RK2928_CLKGATE_CON(0), 5, GFLAGS),
	COMPOSITE_NOMUX(HCLK_CPU, "hclk_cpu", "aclk_cpu_src", CLK_IGNORE_UNUSED,
			RK2928_CLKSEL_CON(1), 8, 2, DFLAGS | CLK_DIVIDER_READ_ONLY,
			RK2928_CLKGATE_CON(0), 4, GFLAGS),

	COMPOSITE(0, "aclk_peri_src", mux_pll_src_3plls_p, 0,
			RK2928_CLKSEL_CON(10), 14, 2, MFLAGS, 0, 5, DFLAGS,
			RK2928_CLKGATE_CON(2), 0, GFLAGS),

	GATE(ACLK_PERI, "aclk_peri", "aclk_peri_src", 0,
			RK2928_CLKGATE_CON(2), 1, GFLAGS),
	DIV(0, "pclk_peri_src", "aclk_peri_src", CLK_IGNORE_UNUSED,
			RK2928_CLKSEL_CON(10), 12, 2, DFLAGS | CLK_DIVIDER_POWER_OF_TWO),
	GATE(PCLK_PERI, "pclk_peri", "pclk_peri_src", 0,
			RK2928_CLKGATE_CON(2), 3, GFLAGS),
	DIV(0, "hclk_peri_src", "aclk_peri_src", CLK_IGNORE_UNUSED,
			RK2928_CLKSEL_CON(10), 8, 2, DFLAGS | CLK_DIVIDER_POWER_OF_TWO),
	GATE(HCLK_PERI, "hclk_peri", "hclk_peri_src", 0,
			RK2928_CLKGATE_CON(2), 2, GFLAGS),

	COMPOSITE_NODIV(SCLK_TIMER0, "sclk_timer0", mux_timer_p, CLK_IGNORE_UNUSED,
			RK2928_CLKSEL_CON(2), 4, 1, MFLAGS,
			RK2928_CLKGATE_CON(1), 0, GFLAGS),
	COMPOSITE_NODIV(SCLK_TIMER1, "sclk_timer1", mux_timer_p, CLK_IGNORE_UNUSED,
			RK2928_CLKSEL_CON(2), 5, 1, MFLAGS,
			RK2928_CLKGATE_CON(1), 1, GFLAGS),
	COMPOSITE_NODIV(SCLK_TIMER2, "sclk_timer2", mux_timer_p, CLK_IGNORE_UNUSED,
			RK2928_CLKSEL_CON(2), 6, 1, MFLAGS,
			RK2928_CLKGATE_CON(2), 4, GFLAGS),
	COMPOSITE_NODIV(SCLK_TIMER3, "sclk_timer3", mux_timer_p, CLK_IGNORE_UNUSED,
			RK2928_CLKSEL_CON(2), 7, 1, MFLAGS,
			RK2928_CLKGATE_CON(2), 5, GFLAGS),

	MUX(0, "uart_pll_clk", mux_pll_src_apll_dpll_gpll_usb480m_p, 0,
			RK2928_CLKSEL_CON(13), 10, 2, MFLAGS),
	COMPOSITE_NOMUX(0, "uart0_src", "uart_pll_clk", 0,
			RK2928_CLKSEL_CON(13), 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(1), 8, GFLAGS),
	COMPOSITE_NOMUX(0, "uart1_src", "uart_pll_clk", 0,
			RK2928_CLKSEL_CON(14), 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(1), 10, GFLAGS),
	COMPOSITE_NOMUX(0, "uart2_src", "uart_pll_clk", 0,
			RK2928_CLKSEL_CON(15), 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(1), 12, GFLAGS),
	COMPOSITE_FRACMUX(0, "uart0_frac", "uart0_src", CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(17), 0,
			RK2928_CLKGATE_CON(1), 9, GFLAGS,
			&rk3036_uart0_fracmux),
	COMPOSITE_FRACMUX(0, "uart1_frac", "uart1_src", CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(18), 0,
			RK2928_CLKGATE_CON(1), 11, GFLAGS,
			&rk3036_uart1_fracmux),
	COMPOSITE_FRACMUX(0, "uart2_frac", "uart2_src", CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(19), 0,
			RK2928_CLKGATE_CON(1), 13, GFLAGS,
			&rk3036_uart2_fracmux),

	COMPOSITE(0, "aclk_vcodec", mux_pll_src_3plls_p, 0,
			RK2928_CLKSEL_CON(32), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RK2928_CLKGATE_CON(3), 11, GFLAGS),
	FACTOR_GATE(HCLK_VCODEC, "hclk_vcodec", "aclk_vcodec", 0, 1, 4,
			RK2928_CLKGATE_CON(3), 12, GFLAGS),

	COMPOSITE(0, "aclk_hvec", mux_pll_src_3plls_p, 0,
			RK2928_CLKSEL_CON(20), 0, 2, MFLAGS, 2, 5, DFLAGS,
			RK2928_CLKGATE_CON(10), 6, GFLAGS),

	COMPOSITE(0, "aclk_disp1_pre", mux_pll_src_3plls_p, 0,
			RK2928_CLKSEL_CON(31), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RK2928_CLKGATE_CON(1), 4, GFLAGS),
	COMPOSITE(0, "hclk_disp_pre", mux_pll_src_3plls_p, 0,
			RK2928_CLKSEL_CON(30), 14, 2, MFLAGS, 8, 5, DFLAGS,
			RK2928_CLKGATE_CON(0), 11, GFLAGS),
	COMPOSITE(SCLK_LCDC, "dclk_lcdc", mux_pll_src_3plls_p, 0,
			RK2928_CLKSEL_CON(28), 0, 2, MFLAGS, 8, 8, DFLAGS,
			RK2928_CLKGATE_CON(3), 2, GFLAGS),

	COMPOSITE_NODIV(0, "sclk_sdmmc_src", mux_mmc_src_p, 0,
			RK2928_CLKSEL_CON(12), 8, 2, MFLAGS,
			RK2928_CLKGATE_CON(2), 11, GFLAGS),
	DIV(SCLK_SDMMC, "sclk_sdmmc", "sclk_sdmmc_src", 0,
			RK2928_CLKSEL_CON(11), 0, 7, DFLAGS),

	COMPOSITE_NODIV(0, "sclk_sdio_src", mux_mmc_src_p, 0,
			RK2928_CLKSEL_CON(12), 10, 2, MFLAGS,
			RK2928_CLKGATE_CON(2), 13, GFLAGS),
	DIV(SCLK_SDIO, "sclk_sdio", "sclk_sdio_src", 0,
			RK2928_CLKSEL_CON(11), 8, 7, DFLAGS),

	COMPOSITE(SCLK_EMMC, "sclk_emmc", mux_mmc_src_p, 0,
			RK2928_CLKSEL_CON(12), 12, 2, MFLAGS, 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(2), 14, GFLAGS),

	MMC(SCLK_SDMMC_DRV,    "sdmmc_drv",    "sclk_sdmmc", RK3036_SDMMC_CON0, 1),
	MMC(SCLK_SDMMC_SAMPLE, "sdmmc_sample", "sclk_sdmmc", RK3036_SDMMC_CON1, 0),

	MMC(SCLK_SDIO_DRV,     "sdio_drv",     "sclk_sdio",  RK3036_SDIO_CON0, 1),
	MMC(SCLK_SDIO_SAMPLE,  "sdio_sample",  "sclk_sdio",  RK3036_SDIO_CON1, 0),

	MMC(SCLK_EMMC_DRV,     "emmc_drv",     "sclk_emmc",  RK3036_EMMC_CON0,  1),
	MMC(SCLK_EMMC_SAMPLE,  "emmc_sample",  "sclk_emmc",  RK3036_EMMC_CON1,  0),

	COMPOSITE(0, "i2s_src", mux_pll_src_3plls_p, 0,
			RK2928_CLKSEL_CON(3), 14, 2, MFLAGS, 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(0), 9, GFLAGS),
	COMPOSITE_FRACMUX(0, "i2s_frac", "i2s_src", CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(7), 0,
			RK2928_CLKGATE_CON(0), 10, GFLAGS,
			&rk3036_i2s_fracmux),
	COMPOSITE_NODIV(SCLK_I2S_OUT, "i2s_clkout", mux_i2s_clkout_p, 0,
			RK2928_CLKSEL_CON(3), 12, 1, MFLAGS,
			RK2928_CLKGATE_CON(0), 13, GFLAGS),
	GATE(SCLK_I2S, "sclk_i2s", "i2s_pre", CLK_SET_RATE_PARENT,
			RK2928_CLKGATE_CON(0), 14, GFLAGS),

	COMPOSITE(0, "spdif_src", mux_pll_src_3plls_p, 0,
			RK2928_CLKSEL_CON(5), 10, 2, MFLAGS, 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(2), 10, GFLAGS),
	COMPOSITE_FRACMUX(0, "spdif_frac", "spdif_src", 0,
			RK2928_CLKSEL_CON(9), 0,
			RK2928_CLKGATE_CON(2), 12, GFLAGS,
			&rk3036_spdif_fracmux),

	GATE(SCLK_OTGPHY0, "sclk_otgphy0", "xin12m", CLK_IGNORE_UNUSED,
			RK2928_CLKGATE_CON(1), 5, GFLAGS),

	COMPOSITE(SCLK_GPU, "sclk_gpu", mux_pll_src_3plls_p, 0,
			RK2928_CLKSEL_CON(34), 8, 2, MFLAGS, 0, 5, DFLAGS,
			RK2928_CLKGATE_CON(3), 13, GFLAGS),

	COMPOSITE(SCLK_SPI, "sclk_spi", mux_pll_src_3plls_p, 0,
			RK2928_CLKSEL_CON(25), 8, 2, MFLAGS, 0, 7, DFLAGS,
			RK2928_CLKGATE_CON(2), 9, GFLAGS),

	COMPOSITE(SCLK_NANDC, "sclk_nandc", mux_pll_src_3plls_p, 0,
			RK2928_CLKSEL_CON(16), 8, 2, MFLAGS, 10, 5, DFLAGS,
			RK2928_CLKGATE_CON(10), 4, GFLAGS),

	COMPOSITE(SCLK_SFC, "sclk_sfc", mux_pll_src_apll_dpll_gpll_usb480m_p, 0,
			RK2928_CLKSEL_CON(16), 0, 2, MFLAGS, 2, 5, DFLAGS,
			RK2928_CLKGATE_CON(10), 5, GFLAGS),

	COMPOSITE_NOGATE(SCLK_MACPLL, "mac_pll_src", mux_pll_src_3plls_p, CLK_SET_RATE_NO_REPARENT,
			RK2928_CLKSEL_CON(21), 0, 2, MFLAGS, 9, 5, DFLAGS),
	MUX(SCLK_MACREF, "mac_clk_ref", mux_mac_p, CLK_SET_RATE_PARENT,
			RK2928_CLKSEL_CON(21), 3, 1, MFLAGS),

	COMPOSITE_NOMUX(SCLK_MAC, "mac_clk", "mac_clk_ref", 0,
			RK2928_CLKSEL_CON(21), 4, 5, DFLAGS,
			RK2928_CLKGATE_CON(2), 6, GFLAGS),
	FACTOR(0, "sclk_macref_out", "hclk_peri_src", 0, 1, 2),

	MUX(SCLK_HDMI, "dclk_hdmi", mux_dclk_p, 0,
			RK2928_CLKSEL_CON(31), 0, 1, MFLAGS),

	/*
	 * Clock-Architecture Diagram 3
	 */

	/* aclk_cpu gates */
	GATE(0, "sclk_intmem", "aclk_cpu", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(4), 12, GFLAGS),
	GATE(0, "aclk_strc_sys", "aclk_cpu", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(4), 10, GFLAGS),

	/* hclk_cpu gates */
	GATE(HCLK_ROM, "hclk_rom", "hclk_cpu", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(5), 6, GFLAGS),

	/* pclk_cpu gates */
	GATE(PCLK_GRF, "pclk_grf", "pclk_cpu", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(5), 4, GFLAGS),
	GATE(PCLK_DDRUPCTL, "pclk_ddrupctl", "pclk_cpu", 0, RK2928_CLKGATE_CON(5), 7, GFLAGS),
	GATE(PCLK_ACODEC, "pclk_acodec", "pclk_cpu", 0, RK2928_CLKGATE_CON(5), 14, GFLAGS),
	GATE(PCLK_HDMI, "pclk_hdmi", "pclk_cpu", 0, RK2928_CLKGATE_CON(3), 8, GFLAGS),

	/* aclk_vio gates */
	GATE(ACLK_VIO, "aclk_vio", "aclk_disp1_pre", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(6), 13, GFLAGS),
	GATE(ACLK_LCDC, "aclk_lcdc", "aclk_disp1_pre", 0, RK2928_CLKGATE_CON(9), 6, GFLAGS),

	GATE(HCLK_VIO_BUS, "hclk_vio_bus", "hclk_disp_pre", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(6), 12, GFLAGS),
	GATE(HCLK_LCDC, "hclk_lcdc", "hclk_disp_pre", 0, RK2928_CLKGATE_CON(9), 5, GFLAGS),


	/* xin24m gates */
	GATE(SCLK_PVTM_CORE, "sclk_pvtm_core", "xin24m", 0, RK2928_CLKGATE_CON(10), 0, GFLAGS),
	GATE(SCLK_PVTM_GPU, "sclk_pvtm_gpu", "xin24m", 0, RK2928_CLKGATE_CON(10), 1, GFLAGS),

	/* aclk_peri gates */
	GATE(0, "aclk_peri_axi_matrix", "aclk_peri", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(4), 3, GFLAGS),
	GATE(0, "aclk_cpu_peri", "aclk_peri", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(4), 2, GFLAGS),
	GATE(ACLK_DMAC2, "aclk_dmac2", "aclk_peri", 0, RK2928_CLKGATE_CON(5), 1, GFLAGS),
	GATE(0, "aclk_peri_niu", "aclk_peri", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(9), 15, GFLAGS),

	/* hclk_peri gates */
	GATE(0, "hclk_peri_matrix", "hclk_peri", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(4), 0, GFLAGS),
	GATE(0, "hclk_usb_peri", "hclk_peri", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(9), 13, GFLAGS),
	GATE(0, "hclk_peri_arbi", "hclk_peri", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(9), 14, GFLAGS),
	GATE(HCLK_NANDC, "hclk_nandc", "hclk_peri", 0, RK2928_CLKGATE_CON(5), 9, GFLAGS),
	GATE(HCLK_SDMMC, "hclk_sdmmc", "hclk_peri", 0, RK2928_CLKGATE_CON(5), 10, GFLAGS),
	GATE(HCLK_SDIO, "hclk_sdio", "hclk_peri", 0, RK2928_CLKGATE_CON(5), 11, GFLAGS),
	GATE(HCLK_EMMC, "hclk_emmc", "hclk_peri", 0, RK2928_CLKGATE_CON(7), 0, GFLAGS),
	GATE(HCLK_OTG0, "hclk_otg0", "hclk_peri", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(5), 13, GFLAGS),
	GATE(HCLK_OTG1, "hclk_otg1", "hclk_peri", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(7), 3, GFLAGS),
	GATE(HCLK_I2S, "hclk_i2s", "hclk_peri", 0, RK2928_CLKGATE_CON(7), 2, GFLAGS),
	GATE(0, "hclk_sfc", "hclk_peri", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(3), 14, GFLAGS),
	GATE(HCLK_MAC, "hclk_mac", "hclk_peri", 0, RK2928_CLKGATE_CON(3), 5, GFLAGS),

	/* pclk_peri gates */
	GATE(0, "pclk_peri_matrix", "pclk_peri", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(4), 1, GFLAGS),
	GATE(0, "pclk_efuse", "pclk_peri", CLK_IGNORE_UNUSED, RK2928_CLKGATE_CON(5), 2, GFLAGS),
	GATE(PCLK_TIMER, "pclk_timer", "pclk_peri", 0, RK2928_CLKGATE_CON(7), 7, GFLAGS),
	GATE(PCLK_PWM, "pclk_pwm", "pclk_peri", 0, RK2928_CLKGATE_CON(7), 10, GFLAGS),
	GATE(PCLK_SPI, "pclk_spi", "pclk_peri", 0, RK2928_CLKGATE_CON(7), 12, GFLAGS),
	GATE(PCLK_WDT, "pclk_wdt", "pclk_peri", 0, RK2928_CLKGATE_CON(7), 15, GFLAGS),
	GATE(PCLK_UART0, "pclk_uart0", "pclk_peri", 0, RK2928_CLKGATE_CON(8), 0, GFLAGS),
	GATE(PCLK_UART1, "pclk_uart1", "pclk_peri", 0, RK2928_CLKGATE_CON(8), 1, GFLAGS),
	GATE(PCLK_UART2, "pclk_uart2", "pclk_peri", 0, RK2928_CLKGATE_CON(8), 2, GFLAGS),
	GATE(PCLK_I2C0, "pclk_i2c0", "pclk_peri", 0, RK2928_CLKGATE_CON(8), 4, GFLAGS),
	GATE(PCLK_I2C1, "pclk_i2c1", "pclk_peri", 0, RK2928_CLKGATE_CON(8), 5, GFLAGS),
	GATE(PCLK_I2C2, "pclk_i2c2", "pclk_peri", 0, RK2928_CLKGATE_CON(8), 6, GFLAGS),
	GATE(PCLK_GPIO0, "pclk_gpio0", "pclk_peri", 0, RK2928_CLKGATE_CON(8), 9, GFLAGS),
	GATE(PCLK_GPIO1, "pclk_gpio1", "pclk_peri", 0, RK2928_CLKGATE_CON(8), 10, GFLAGS),
	GATE(PCLK_GPIO2, "pclk_gpio2", "pclk_peri", 0, RK2928_CLKGATE_CON(8), 11, GFLAGS),
};

static const char *const rk3036_critical_clocks[] __initconst = {
	"aclk_cpu",
	"aclk_peri",
	"hclk_peri",
	"pclk_peri",
};

static void __init rk3036_clk_init(struct device_node *np)
{
	struct rockchip_clk_provider *ctx;
	void __iomem *reg_base;
	struct clk *clk;

	reg_base = of_iomap(np, 0);
	if (!reg_base) {
		pr_err("%s: could not map cru region\n", __func__);
		return;
	}

	/*
	 * Make uart_pll_clk a child of the gpll, as all other sources are
	 * not that usable / stable.
	 */
	writel_relaxed(HIWORD_UPDATE(0x2, 0x3, 10),
		       reg_base + RK2928_CLKSEL_CON(13));

	ctx = rockchip_clk_init(np, reg_base, CLK_NR_CLKS);
	if (IS_ERR(ctx)) {
		pr_err("%s: rockchip clk init failed\n", __func__);
		iounmap(reg_base);
		return;
	}

	clk = clk_register_fixed_factor(NULL, "usb480m", "xin24m", 0, 20, 1);
	if (IS_ERR(clk))
		pr_warn("%s: could not register clock usb480m: %ld\n",
			__func__, PTR_ERR(clk));

	rockchip_clk_register_plls(ctx, rk3036_pll_clks,
				   ARRAY_SIZE(rk3036_pll_clks),
				   RK3036_GRF_SOC_STATUS0);
	rockchip_clk_register_branches(ctx, rk3036_clk_branches,
				  ARRAY_SIZE(rk3036_clk_branches));
	rockchip_clk_protect_critical(rk3036_critical_clocks,
				      ARRAY_SIZE(rk3036_critical_clocks));

	rockchip_clk_register_armclk(ctx, ARMCLK, "armclk",
			mux_armclk_p, ARRAY_SIZE(mux_armclk_p),
			&rk3036_cpuclk_data, rk3036_cpuclk_rates,
			ARRAY_SIZE(rk3036_cpuclk_rates));

	rockchip_register_softrst(np, 9, reg_base + RK2928_SOFTRST_CON(0),
				  ROCKCHIP_SOFTRST_HIWORD_MASK);

	rockchip_register_restart_notifier(ctx, RK2928_GLB_SRST_FST, NULL);

	rockchip_clk_of_add_provider(np, ctx);
}
CLK_OF_DECLARE(rk3036_cru, "rockchip,rk3036-cru", rk3036_clk_init);
