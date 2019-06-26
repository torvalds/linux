/*
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 * Author: Tarek Dakhran <t.dakhran@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Common Clock Framework support for Exynos5410 SoC.
*/

#include <dt-bindings/clock/exynos5410.h>

#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/clk.h>

#include "clk.h"

#define APLL_LOCK               0x0
#define APLL_CON0               0x100
#define CPLL_LOCK               0x10020
#define CPLL_CON0               0x10120
#define EPLL_LOCK               0x10040
#define EPLL_CON0               0x10130
#define MPLL_LOCK               0x4000
#define MPLL_CON0               0x4100
#define BPLL_LOCK               0x20010
#define BPLL_CON0               0x20110
#define KPLL_LOCK               0x28000
#define KPLL_CON0               0x28100

#define SRC_CPU			0x200
#define DIV_CPU0		0x500
#define SRC_CPERI1		0x4204
#define GATE_IP_G2D		0x8800
#define DIV_TOP0		0x10510
#define DIV_TOP1		0x10514
#define DIV_FSYS0		0x10548
#define DIV_FSYS1		0x1054c
#define DIV_FSYS2		0x10550
#define DIV_PERIC0		0x10558
#define DIV_PERIC3		0x10564
#define SRC_TOP0		0x10210
#define SRC_TOP1		0x10214
#define SRC_TOP2		0x10218
#define SRC_FSYS		0x10244
#define SRC_PERIC0		0x10250
#define SRC_MASK_FSYS		0x10340
#define SRC_MASK_PERIC0		0x10350
#define GATE_BUS_FSYS0		0x10740
#define GATE_TOP_SCLK_FSYS	0x10840
#define GATE_TOP_SCLK_PERIC	0x10850
#define GATE_IP_FSYS		0x10944
#define GATE_IP_PERIC		0x10950
#define GATE_IP_PERIS		0x10960
#define SRC_CDREX		0x20200
#define SRC_KFC			0x28200
#define DIV_KFC0		0x28500

/* list of PLLs */
enum exynos5410_plls {
	apll, cpll, epll, mpll,
	bpll, kpll,
	nr_plls                 /* number of PLLs */
};

/* list of all parent clocks */
PNAME(apll_p)		= { "fin_pll", "fout_apll", };
PNAME(bpll_p)		= { "fin_pll", "fout_bpll", };
PNAME(cpll_p)		= { "fin_pll", "fout_cpll" };
PNAME(epll_p)		= { "fin_pll", "fout_epll" };
PNAME(mpll_p)		= { "fin_pll", "fout_mpll", };
PNAME(kpll_p)		= { "fin_pll", "fout_kpll", };

PNAME(mout_cpu_p)	= { "mout_apll", "sclk_mpll", };
PNAME(mout_kfc_p)	= { "mout_kpll", "sclk_mpll", };

PNAME(mpll_user_p)	= { "fin_pll", "sclk_mpll", };
PNAME(bpll_user_p)	= { "fin_pll", "sclk_bpll", };
PNAME(mpll_bpll_p)	= { "sclk_mpll_muxed", "sclk_bpll_muxed", };
PNAME(sclk_mpll_bpll_p)	= { "sclk_mpll_bpll", "fin_pll", };

PNAME(group2_p)		= { "fin_pll", "fin_pll", "none", "none",
			"none", "none", "sclk_mpll_bpll",
			 "none", "none", "sclk_cpll" };

static const struct samsung_mux_clock exynos5410_mux_clks[] __initconst = {
	MUX(0, "mout_apll", apll_p, SRC_CPU, 0, 1),
	MUX(0, "mout_cpu", mout_cpu_p, SRC_CPU, 16, 1),

	MUX(0, "mout_kpll", kpll_p, SRC_KFC, 0, 1),
	MUX(0, "mout_kfc", mout_kfc_p, SRC_KFC, 16, 1),

	MUX(0, "sclk_mpll", mpll_p, SRC_CPERI1, 8, 1),
	MUX(0, "sclk_mpll_muxed", mpll_user_p, SRC_TOP2, 20, 1),

	MUX(0, "sclk_bpll", bpll_p, SRC_CDREX, 0, 1),
	MUX(0, "sclk_bpll_muxed", bpll_user_p, SRC_TOP2, 24, 1),

	MUX(0, "sclk_epll", epll_p, SRC_TOP2, 12, 1),

	MUX(0, "sclk_cpll", cpll_p, SRC_TOP2, 8, 1),

	MUX(0, "sclk_mpll_bpll", mpll_bpll_p, SRC_TOP1, 20, 1),

	MUX(0, "mout_mmc0", group2_p, SRC_FSYS, 0, 4),
	MUX(0, "mout_mmc1", group2_p, SRC_FSYS, 4, 4),
	MUX(0, "mout_mmc2", group2_p, SRC_FSYS, 8, 4),
	MUX(0, "mout_usbd300", sclk_mpll_bpll_p, SRC_FSYS, 28, 1),
	MUX(0, "mout_usbd301", sclk_mpll_bpll_p, SRC_FSYS, 29, 1),

	MUX(0, "mout_uart0", group2_p, SRC_PERIC0, 0, 4),
	MUX(0, "mout_uart1", group2_p, SRC_PERIC0, 4, 4),
	MUX(0, "mout_uart2", group2_p, SRC_PERIC0, 8, 4),
	MUX(0, "mout_uart3", group2_p, SRC_PERIC0, 12, 4),
	MUX(0, "mout_pwm", group2_p, SRC_PERIC0, 24, 4),

	MUX(0, "mout_aclk200", mpll_bpll_p, SRC_TOP0, 12, 1),
	MUX(0, "mout_aclk400", mpll_bpll_p, SRC_TOP0, 20, 1),
};

static const struct samsung_div_clock exynos5410_div_clks[] __initconst = {
	DIV(0, "div_arm", "mout_cpu", DIV_CPU0, 0, 3),
	DIV(0, "div_arm2", "div_arm", DIV_CPU0, 28, 3),

	DIV(0, "div_acp", "div_arm2", DIV_CPU0, 8, 3),
	DIV(0, "div_cpud", "div_arm2", DIV_CPU0, 4, 3),
	DIV(0, "div_atb", "div_arm2", DIV_CPU0, 16, 3),
	DIV(0, "pclk_dbg", "div_arm2", DIV_CPU0, 20, 3),

	DIV(0, "div_kfc", "mout_kfc", DIV_KFC0, 0, 3),
	DIV(0, "div_aclk", "div_kfc", DIV_KFC0, 4, 3),
	DIV(0, "div_pclk", "div_kfc", DIV_KFC0, 20, 3),

	DIV(0, "aclk66_pre", "sclk_mpll_muxed", DIV_TOP1, 24, 3),
	DIV(0, "aclk66", "aclk66_pre", DIV_TOP0, 0, 3),

	DIV(0, "dout_usbphy300", "mout_usbd300", DIV_FSYS0, 16, 4),
	DIV(0, "dout_usbphy301", "mout_usbd301", DIV_FSYS0, 20, 4),
	DIV(0, "dout_usbd300", "mout_usbd300", DIV_FSYS0, 24, 4),
	DIV(0, "dout_usbd301", "mout_usbd301", DIV_FSYS0, 28, 4),

	DIV(0, "div_mmc0", "mout_mmc0", DIV_FSYS1, 0, 4),
	DIV(0, "div_mmc1", "mout_mmc1", DIV_FSYS1, 16, 4),
	DIV(0, "div_mmc2", "mout_mmc2", DIV_FSYS2, 0, 4),

	DIV_F(0, "div_mmc_pre0", "div_mmc0",
			DIV_FSYS1, 8, 8, CLK_SET_RATE_PARENT, 0),
	DIV_F(0, "div_mmc_pre1", "div_mmc1",
			DIV_FSYS1, 24, 8, CLK_SET_RATE_PARENT, 0),
	DIV_F(0, "div_mmc_pre2", "div_mmc2",
			DIV_FSYS2, 8, 8, CLK_SET_RATE_PARENT, 0),

	DIV(0, "div_uart0", "mout_uart0", DIV_PERIC0, 0, 4),
	DIV(0, "div_uart1", "mout_uart1", DIV_PERIC0, 4, 4),
	DIV(0, "div_uart2", "mout_uart2", DIV_PERIC0, 8, 4),
	DIV(0, "div_uart3", "mout_uart3", DIV_PERIC0, 12, 4),

	DIV(0, "dout_pwm", "mout_pwm", DIV_PERIC3, 0, 4),

	DIV(0, "aclk200", "mout_aclk200", DIV_TOP0, 12, 3),
	DIV(0, "aclk266", "mpll_user_p", DIV_TOP0, 16, 3),
	DIV(0, "aclk400", "mout_aclk400", DIV_TOP0, 24, 3),
};

static const struct samsung_gate_clock exynos5410_gate_clks[] __initconst = {
	GATE(CLK_SSS, "sss", "aclk266", GATE_IP_G2D, 2, 0, 0),
	GATE(CLK_MCT, "mct", "aclk66", GATE_IP_PERIS, 18, 0, 0),
	GATE(CLK_WDT, "wdt", "aclk66", GATE_IP_PERIS, 19, 0, 0),
	GATE(CLK_RTC, "rtc", "aclk66", GATE_IP_PERIS, 20, 0, 0),
	GATE(CLK_TMU, "tmu", "aclk66", GATE_IP_PERIS, 21, 0, 0),

	GATE(CLK_SCLK_MMC0, "sclk_mmc0", "div_mmc_pre0",
			SRC_MASK_FSYS, 0, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_MMC1, "sclk_mmc1", "div_mmc_pre1",
			SRC_MASK_FSYS, 4, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_MMC2, "sclk_mmc2", "div_mmc_pre2",
			SRC_MASK_FSYS, 8, CLK_SET_RATE_PARENT, 0),

	GATE(CLK_MMC0, "sdmmc0", "aclk200", GATE_BUS_FSYS0, 12, 0, 0),
	GATE(CLK_MMC1, "sdmmc1", "aclk200", GATE_BUS_FSYS0, 13, 0, 0),
	GATE(CLK_MMC2, "sdmmc2", "aclk200", GATE_BUS_FSYS0, 14, 0, 0),
	GATE(CLK_PDMA1, "pdma1", "aclk200", GATE_BUS_FSYS0, 2, 0, 0),
	GATE(CLK_PDMA0, "pdma0", "aclk200", GATE_BUS_FSYS0, 1, 0, 0),

	GATE(CLK_SCLK_USBPHY301, "sclk_usbphy301", "dout_usbphy301",
	     GATE_TOP_SCLK_FSYS, 7, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_USBPHY300, "sclk_usbphy300", "dout_usbphy300",
	     GATE_TOP_SCLK_FSYS, 8, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_USBD300, "sclk_usbd300", "dout_usbd300",
	     GATE_TOP_SCLK_FSYS, 9, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_USBD301, "sclk_usbd301", "dout_usbd301",
	     GATE_TOP_SCLK_FSYS, 10, CLK_SET_RATE_PARENT, 0),

	GATE(CLK_SCLK_PWM, "sclk_pwm", "dout_pwm",
	     GATE_TOP_SCLK_PERIC, 11, CLK_SET_RATE_PARENT, 0),

	GATE(CLK_UART0, "uart0", "aclk66", GATE_IP_PERIC, 0, 0, 0),
	GATE(CLK_UART1, "uart1", "aclk66", GATE_IP_PERIC, 1, 0, 0),
	GATE(CLK_UART2, "uart2", "aclk66", GATE_IP_PERIC, 2, 0, 0),
	GATE(CLK_UART3, "uart3", "aclk66", GATE_IP_PERIC, 3, 0, 0),
	GATE(CLK_I2C0, "i2c0", "aclk66", GATE_IP_PERIC, 6, 0, 0),
	GATE(CLK_I2C1, "i2c1", "aclk66", GATE_IP_PERIC, 7, 0, 0),
	GATE(CLK_I2C2, "i2c2", "aclk66", GATE_IP_PERIC, 8, 0, 0),
	GATE(CLK_I2C3, "i2c3", "aclk66", GATE_IP_PERIC, 9, 0, 0),
	GATE(CLK_USI0, "usi0", "aclk66", GATE_IP_PERIC, 10, 0, 0),
	GATE(CLK_USI1, "usi1", "aclk66", GATE_IP_PERIC, 11, 0, 0),
	GATE(CLK_USI2, "usi2", "aclk66", GATE_IP_PERIC, 12, 0, 0),
	GATE(CLK_USI3, "usi3", "aclk66", GATE_IP_PERIC, 13, 0, 0),
	GATE(CLK_TSADC, "tsadc", "aclk66", GATE_IP_PERIC, 15, 0, 0),
	GATE(CLK_PWM, "pwm", "aclk66", GATE_IP_PERIC, 24, 0, 0),

	GATE(CLK_SCLK_UART0, "sclk_uart0", "div_uart0",
			SRC_MASK_PERIC0, 0, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_UART1, "sclk_uart1", "div_uart1",
			SRC_MASK_PERIC0, 4, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_UART2, "sclk_uart2", "div_uart2",
			SRC_MASK_PERIC0, 8, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_SCLK_UART3, "sclk_uart3", "div_uart3",
			SRC_MASK_PERIC0, 12, CLK_SET_RATE_PARENT, 0),

	GATE(CLK_USBH20, "usbh20", "aclk200_fsys", GATE_IP_FSYS, 18, 0, 0),
	GATE(CLK_USBD300, "usbd300", "aclk200_fsys", GATE_IP_FSYS, 19, 0, 0),
	GATE(CLK_USBD301, "usbd301", "aclk200_fsys", GATE_IP_FSYS, 20, 0, 0),
};

static const struct samsung_pll_rate_table exynos5410_pll2550x_24mhz_tbl[] __initconst = {
	PLL_36XX_RATE(24 * MHZ, 400000000U, 200, 3, 2, 0),
	PLL_36XX_RATE(24 * MHZ, 333000000U, 111, 2, 2, 0),
	PLL_36XX_RATE(24 * MHZ, 300000000U, 100, 2, 2, 0),
	PLL_36XX_RATE(24 * MHZ, 266000000U, 266, 3, 3, 0),
	PLL_36XX_RATE(24 * MHZ, 200000000U, 200, 3, 3, 0),
	PLL_36XX_RATE(24 * MHZ, 192000000U, 192, 3, 3, 0),
	PLL_36XX_RATE(24 * MHZ, 166000000U, 166, 3, 3, 0),
	PLL_36XX_RATE(24 * MHZ, 133000000U, 266, 3, 4, 0),
	PLL_36XX_RATE(24 * MHZ, 100000000U, 200, 3, 4, 0),
	PLL_36XX_RATE(24 * MHZ, 66000000U,  176, 2, 5, 0),
};

static struct samsung_pll_clock exynos5410_plls[nr_plls] __initdata = {
	[apll] = PLL(pll_35xx, CLK_FOUT_APLL, "fout_apll", "fin_pll", APLL_LOCK,
		APLL_CON0, NULL),
	[cpll] = PLL(pll_35xx, CLK_FOUT_CPLL, "fout_cpll", "fin_pll", CPLL_LOCK,
		CPLL_CON0, NULL),
	[epll] = PLL(pll_2650x, CLK_FOUT_EPLL, "fout_epll", "fin_pll", EPLL_LOCK,
		EPLL_CON0, NULL),
	[mpll] = PLL(pll_35xx, CLK_FOUT_MPLL, "fout_mpll", "fin_pll", MPLL_LOCK,
		MPLL_CON0, NULL),
	[bpll] = PLL(pll_35xx, CLK_FOUT_BPLL, "fout_bpll", "fin_pll", BPLL_LOCK,
		BPLL_CON0, NULL),
	[kpll] = PLL(pll_35xx, CLK_FOUT_KPLL, "fout_kpll", "fin_pll", KPLL_LOCK,
		KPLL_CON0, NULL),
};

static const struct samsung_cmu_info cmu __initconst = {
	.pll_clks	= exynos5410_plls,
	.nr_pll_clks	= ARRAY_SIZE(exynos5410_plls),
	.mux_clks	= exynos5410_mux_clks,
	.nr_mux_clks	= ARRAY_SIZE(exynos5410_mux_clks),
	.div_clks	= exynos5410_div_clks,
	.nr_div_clks	= ARRAY_SIZE(exynos5410_div_clks),
	.gate_clks	= exynos5410_gate_clks,
	.nr_gate_clks	= ARRAY_SIZE(exynos5410_gate_clks),
	.nr_clk_ids	= CLK_NR_CLKS,
};

/* register exynos5410 clocks */
static void __init exynos5410_clk_init(struct device_node *np)
{
	struct clk *xxti = of_clk_get(np, 0);

	if (!IS_ERR(xxti) && clk_get_rate(xxti) == 24 * MHZ)
		exynos5410_plls[epll].rate_table = exynos5410_pll2550x_24mhz_tbl;

	samsung_cmu_register_one(np, &cmu);

	pr_debug("Exynos5410: clock setup completed.\n");
}
CLK_OF_DECLARE(exynos5410_clk, "samsung,exynos5410-clock", exynos5410_clk_init);
