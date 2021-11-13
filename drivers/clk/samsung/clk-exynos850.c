// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Linaro Ltd.
 * Author: Sam Protsenko <semen.protsenko@linaro.org>
 *
 * Common Clock Framework support for Exynos850 SoC.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include <dt-bindings/clock/exynos850.h>

#include "clk.h"

/* Gate register bits */
#define GATE_MANUAL		BIT(20)
#define GATE_ENABLE_HWACG	BIT(28)

/* Gate register offsets range */
#define GATE_OFF_START		0x2000
#define GATE_OFF_END		0x2fff

/**
 * exynos850_init_clocks - Set clocks initial configuration
 * @np:			CMU device tree node with "reg" property (CMU addr)
 * @reg_offs:		Register offsets array for clocks to init
 * @reg_offs_len:	Number of register offsets in reg_offs array
 *
 * Set manual control mode for all gate clocks.
 */
static void __init exynos850_init_clocks(struct device_node *np,
		const unsigned long *reg_offs, size_t reg_offs_len)
{
	void __iomem *reg_base;
	size_t i;

	reg_base = of_iomap(np, 0);
	if (!reg_base)
		panic("%s: failed to map registers\n", __func__);

	for (i = 0; i < reg_offs_len; ++i) {
		void __iomem *reg = reg_base + reg_offs[i];
		u32 val;

		/* Modify only gate clock registers */
		if (reg_offs[i] < GATE_OFF_START || reg_offs[i] > GATE_OFF_END)
			continue;

		val = readl(reg);
		val |= GATE_MANUAL;
		val &= ~GATE_ENABLE_HWACG;
		writel(val, reg);
	}

	iounmap(reg_base);
}

/* ---- CMU_TOP ------------------------------------------------------------- */

/* Register Offset definitions for CMU_TOP (0x120e0000) */
#define PLL_LOCKTIME_PLL_MMC			0x0000
#define PLL_LOCKTIME_PLL_SHARED0		0x0004
#define PLL_LOCKTIME_PLL_SHARED1		0x0008
#define PLL_CON0_PLL_MMC			0x0100
#define PLL_CON3_PLL_MMC			0x010c
#define PLL_CON0_PLL_SHARED0			0x0140
#define PLL_CON3_PLL_SHARED0			0x014c
#define PLL_CON0_PLL_SHARED1			0x0180
#define PLL_CON3_PLL_SHARED1			0x018c
#define CLK_CON_MUX_MUX_CLKCMU_CORE_BUS		0x1014
#define CLK_CON_MUX_MUX_CLKCMU_CORE_CCI		0x1018
#define CLK_CON_MUX_MUX_CLKCMU_CORE_MMC_EMBD	0x101c
#define CLK_CON_MUX_MUX_CLKCMU_CORE_SSS		0x1020
#define CLK_CON_MUX_MUX_CLKCMU_DPU		0x1034
#define CLK_CON_MUX_MUX_CLKCMU_HSI_BUS		0x103c
#define CLK_CON_MUX_MUX_CLKCMU_HSI_MMC_CARD	0x1040
#define CLK_CON_MUX_MUX_CLKCMU_HSI_USB20DRD	0x1044
#define CLK_CON_MUX_MUX_CLKCMU_PERI_BUS		0x1070
#define CLK_CON_MUX_MUX_CLKCMU_PERI_IP		0x1074
#define CLK_CON_MUX_MUX_CLKCMU_PERI_UART	0x1078
#define CLK_CON_DIV_CLKCMU_CORE_BUS		0x1820
#define CLK_CON_DIV_CLKCMU_CORE_CCI		0x1824
#define CLK_CON_DIV_CLKCMU_CORE_MMC_EMBD	0x1828
#define CLK_CON_DIV_CLKCMU_CORE_SSS		0x182c
#define CLK_CON_DIV_CLKCMU_DPU			0x1840
#define CLK_CON_DIV_CLKCMU_HSI_BUS		0x1848
#define CLK_CON_DIV_CLKCMU_HSI_MMC_CARD		0x184c
#define CLK_CON_DIV_CLKCMU_HSI_USB20DRD		0x1850
#define CLK_CON_DIV_CLKCMU_PERI_BUS		0x187c
#define CLK_CON_DIV_CLKCMU_PERI_IP		0x1880
#define CLK_CON_DIV_CLKCMU_PERI_UART		0x1884
#define CLK_CON_DIV_PLL_SHARED0_DIV2		0x188c
#define CLK_CON_DIV_PLL_SHARED0_DIV3		0x1890
#define CLK_CON_DIV_PLL_SHARED0_DIV4		0x1894
#define CLK_CON_DIV_PLL_SHARED1_DIV2		0x1898
#define CLK_CON_DIV_PLL_SHARED1_DIV3		0x189c
#define CLK_CON_DIV_PLL_SHARED1_DIV4		0x18a0
#define CLK_CON_GAT_GATE_CLKCMU_CORE_BUS	0x201c
#define CLK_CON_GAT_GATE_CLKCMU_CORE_CCI	0x2020
#define CLK_CON_GAT_GATE_CLKCMU_CORE_MMC_EMBD	0x2024
#define CLK_CON_GAT_GATE_CLKCMU_CORE_SSS	0x2028
#define CLK_CON_GAT_GATE_CLKCMU_DPU		0x203c
#define CLK_CON_GAT_GATE_CLKCMU_HSI_BUS		0x2044
#define CLK_CON_GAT_GATE_CLKCMU_HSI_MMC_CARD	0x2048
#define CLK_CON_GAT_GATE_CLKCMU_HSI_USB20DRD	0x204c
#define CLK_CON_GAT_GATE_CLKCMU_PERI_BUS	0x2080
#define CLK_CON_GAT_GATE_CLKCMU_PERI_IP		0x2084
#define CLK_CON_GAT_GATE_CLKCMU_PERI_UART	0x2088

static const unsigned long top_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_MMC,
	PLL_LOCKTIME_PLL_SHARED0,
	PLL_LOCKTIME_PLL_SHARED1,
	PLL_CON0_PLL_MMC,
	PLL_CON3_PLL_MMC,
	PLL_CON0_PLL_SHARED0,
	PLL_CON3_PLL_SHARED0,
	PLL_CON0_PLL_SHARED1,
	PLL_CON3_PLL_SHARED1,
	CLK_CON_MUX_MUX_CLKCMU_CORE_BUS,
	CLK_CON_MUX_MUX_CLKCMU_CORE_CCI,
	CLK_CON_MUX_MUX_CLKCMU_CORE_MMC_EMBD,
	CLK_CON_MUX_MUX_CLKCMU_CORE_SSS,
	CLK_CON_MUX_MUX_CLKCMU_DPU,
	CLK_CON_MUX_MUX_CLKCMU_HSI_BUS,
	CLK_CON_MUX_MUX_CLKCMU_HSI_MMC_CARD,
	CLK_CON_MUX_MUX_CLKCMU_HSI_USB20DRD,
	CLK_CON_MUX_MUX_CLKCMU_PERI_BUS,
	CLK_CON_MUX_MUX_CLKCMU_PERI_IP,
	CLK_CON_MUX_MUX_CLKCMU_PERI_UART,
	CLK_CON_DIV_CLKCMU_CORE_BUS,
	CLK_CON_DIV_CLKCMU_CORE_CCI,
	CLK_CON_DIV_CLKCMU_CORE_MMC_EMBD,
	CLK_CON_DIV_CLKCMU_CORE_SSS,
	CLK_CON_DIV_CLKCMU_DPU,
	CLK_CON_DIV_CLKCMU_HSI_BUS,
	CLK_CON_DIV_CLKCMU_HSI_MMC_CARD,
	CLK_CON_DIV_CLKCMU_HSI_USB20DRD,
	CLK_CON_DIV_CLKCMU_PERI_BUS,
	CLK_CON_DIV_CLKCMU_PERI_IP,
	CLK_CON_DIV_CLKCMU_PERI_UART,
	CLK_CON_DIV_PLL_SHARED0_DIV2,
	CLK_CON_DIV_PLL_SHARED0_DIV3,
	CLK_CON_DIV_PLL_SHARED0_DIV4,
	CLK_CON_DIV_PLL_SHARED1_DIV2,
	CLK_CON_DIV_PLL_SHARED1_DIV3,
	CLK_CON_DIV_PLL_SHARED1_DIV4,
	CLK_CON_GAT_GATE_CLKCMU_CORE_BUS,
	CLK_CON_GAT_GATE_CLKCMU_CORE_CCI,
	CLK_CON_GAT_GATE_CLKCMU_CORE_MMC_EMBD,
	CLK_CON_GAT_GATE_CLKCMU_CORE_SSS,
	CLK_CON_GAT_GATE_CLKCMU_DPU,
	CLK_CON_GAT_GATE_CLKCMU_HSI_BUS,
	CLK_CON_GAT_GATE_CLKCMU_HSI_MMC_CARD,
	CLK_CON_GAT_GATE_CLKCMU_HSI_USB20DRD,
	CLK_CON_GAT_GATE_CLKCMU_PERI_BUS,
	CLK_CON_GAT_GATE_CLKCMU_PERI_IP,
	CLK_CON_GAT_GATE_CLKCMU_PERI_UART,
};

/*
 * Do not provide PLL tables to core PLLs, as MANUAL_PLL_CTRL bit is not set
 * for those PLLs by default, so set_rate operation would fail.
 */
static const struct samsung_pll_clock top_pll_clks[] __initconst = {
	/* CMU_TOP_PURECLKCOMP */
	PLL(pll_0822x, CLK_FOUT_SHARED0_PLL, "fout_shared0_pll", "oscclk",
	    PLL_LOCKTIME_PLL_SHARED0, PLL_CON3_PLL_SHARED0,
	    NULL),
	PLL(pll_0822x, CLK_FOUT_SHARED1_PLL, "fout_shared1_pll", "oscclk",
	    PLL_LOCKTIME_PLL_SHARED1, PLL_CON3_PLL_SHARED1,
	    NULL),
	PLL(pll_0831x, CLK_FOUT_MMC_PLL, "fout_mmc_pll", "oscclk",
	    PLL_LOCKTIME_PLL_MMC, PLL_CON3_PLL_MMC, NULL),
};

/* List of parent clocks for Muxes in CMU_TOP */
PNAME(mout_shared0_pll_p)	= { "oscclk", "fout_shared0_pll" };
PNAME(mout_shared1_pll_p)	= { "oscclk", "fout_shared1_pll" };
PNAME(mout_mmc_pll_p)		= { "oscclk", "fout_mmc_pll" };
/* List of parent clocks for Muxes in CMU_TOP: for CMU_CORE */
PNAME(mout_core_bus_p)		= { "dout_shared1_div2", "dout_shared0_div3",
				    "dout_shared1_div3", "dout_shared0_div4" };
PNAME(mout_core_cci_p)		= { "dout_shared0_div2", "dout_shared1_div2",
				    "dout_shared0_div3", "dout_shared1_div3" };
PNAME(mout_core_mmc_embd_p)	= { "oscclk", "dout_shared0_div2",
				    "dout_shared1_div2", "dout_shared0_div3",
				    "dout_shared1_div3", "mout_mmc_pll",
				    "oscclk", "oscclk" };
PNAME(mout_core_sss_p)		= { "dout_shared0_div3", "dout_shared1_div3",
				    "dout_shared0_div4", "dout_shared1_div4" };
/* List of parent clocks for Muxes in CMU_TOP: for CMU_HSI */
PNAME(mout_hsi_bus_p)		= { "dout_shared0_div2", "dout_shared1_div2" };
PNAME(mout_hsi_mmc_card_p)	= { "oscclk", "dout_shared0_div2",
				    "dout_shared1_div2", "dout_shared0_div3",
				    "dout_shared1_div3", "mout_mmc_pll",
				    "oscclk", "oscclk" };
PNAME(mout_hsi_usb20drd_p)	= { "oscclk", "dout_shared0_div4",
				    "dout_shared1_div4", "oscclk" };
/* List of parent clocks for Muxes in CMU_TOP: for CMU_PERI */
PNAME(mout_peri_bus_p)		= { "dout_shared0_div4", "dout_shared1_div4" };
PNAME(mout_peri_uart_p)		= { "oscclk", "dout_shared0_div4",
				    "dout_shared1_div4", "oscclk" };
PNAME(mout_peri_ip_p)		= { "oscclk", "dout_shared0_div4",
				    "dout_shared1_div4", "oscclk" };

/* List of parent clocks for Muxes in CMU_TOP: for CMU_DPU */
PNAME(mout_dpu_p)		= { "dout_shared0_div3", "dout_shared1_div3",
				    "dout_shared0_div4", "dout_shared1_div4" };

static const struct samsung_mux_clock top_mux_clks[] __initconst = {
	/* CMU_TOP_PURECLKCOMP */
	MUX(CLK_MOUT_SHARED0_PLL, "mout_shared0_pll", mout_shared0_pll_p,
	    PLL_CON0_PLL_SHARED0, 4, 1),
	MUX(CLK_MOUT_SHARED1_PLL, "mout_shared1_pll", mout_shared1_pll_p,
	    PLL_CON0_PLL_SHARED1, 4, 1),
	MUX(CLK_MOUT_MMC_PLL, "mout_mmc_pll", mout_mmc_pll_p,
	    PLL_CON0_PLL_MMC, 4, 1),

	/* CORE */
	MUX(CLK_MOUT_CORE_BUS, "mout_core_bus", mout_core_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_CORE_BUS, 0, 2),
	MUX(CLK_MOUT_CORE_CCI, "mout_core_cci", mout_core_cci_p,
	    CLK_CON_MUX_MUX_CLKCMU_CORE_CCI, 0, 2),
	MUX(CLK_MOUT_CORE_MMC_EMBD, "mout_core_mmc_embd", mout_core_mmc_embd_p,
	    CLK_CON_MUX_MUX_CLKCMU_CORE_MMC_EMBD, 0, 3),
	MUX(CLK_MOUT_CORE_SSS, "mout_core_sss", mout_core_sss_p,
	    CLK_CON_MUX_MUX_CLKCMU_CORE_SSS, 0, 2),

	/* DPU */
	MUX(CLK_MOUT_DPU, "mout_dpu", mout_dpu_p,
	    CLK_CON_MUX_MUX_CLKCMU_DPU, 0, 2),

	/* HSI */
	MUX(CLK_MOUT_HSI_BUS, "mout_hsi_bus", mout_hsi_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_HSI_BUS, 0, 1),
	MUX(CLK_MOUT_HSI_MMC_CARD, "mout_hsi_mmc_card", mout_hsi_mmc_card_p,
	    CLK_CON_MUX_MUX_CLKCMU_HSI_MMC_CARD, 0, 3),
	MUX(CLK_MOUT_HSI_USB20DRD, "mout_hsi_usb20drd", mout_hsi_usb20drd_p,
	    CLK_CON_MUX_MUX_CLKCMU_HSI_USB20DRD, 0, 2),

	/* PERI */
	MUX(CLK_MOUT_PERI_BUS, "mout_peri_bus", mout_peri_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_PERI_BUS, 0, 1),
	MUX(CLK_MOUT_PERI_UART, "mout_peri_uart", mout_peri_uart_p,
	    CLK_CON_MUX_MUX_CLKCMU_PERI_UART, 0, 2),
	MUX(CLK_MOUT_PERI_IP, "mout_peri_ip", mout_peri_ip_p,
	    CLK_CON_MUX_MUX_CLKCMU_PERI_IP, 0, 2),
};

static const struct samsung_div_clock top_div_clks[] __initconst = {
	/* CMU_TOP_PURECLKCOMP */
	DIV(CLK_DOUT_SHARED0_DIV3, "dout_shared0_div3", "mout_shared0_pll",
	    CLK_CON_DIV_PLL_SHARED0_DIV3, 0, 2),
	DIV(CLK_DOUT_SHARED0_DIV2, "dout_shared0_div2", "mout_shared0_pll",
	    CLK_CON_DIV_PLL_SHARED0_DIV2, 0, 1),
	DIV(CLK_DOUT_SHARED1_DIV3, "dout_shared1_div3", "mout_shared1_pll",
	    CLK_CON_DIV_PLL_SHARED1_DIV3, 0, 2),
	DIV(CLK_DOUT_SHARED1_DIV2, "dout_shared1_div2", "mout_shared1_pll",
	    CLK_CON_DIV_PLL_SHARED1_DIV2, 0, 1),
	DIV(CLK_DOUT_SHARED0_DIV4, "dout_shared0_div4", "dout_shared0_div2",
	    CLK_CON_DIV_PLL_SHARED0_DIV4, 0, 1),
	DIV(CLK_DOUT_SHARED1_DIV4, "dout_shared1_div4", "dout_shared1_div2",
	    CLK_CON_DIV_PLL_SHARED1_DIV4, 0, 1),

	/* CORE */
	DIV(CLK_DOUT_CORE_BUS, "dout_core_bus", "gout_core_bus",
	    CLK_CON_DIV_CLKCMU_CORE_BUS, 0, 4),
	DIV(CLK_DOUT_CORE_CCI, "dout_core_cci", "gout_core_cci",
	    CLK_CON_DIV_CLKCMU_CORE_CCI, 0, 4),
	DIV(CLK_DOUT_CORE_MMC_EMBD, "dout_core_mmc_embd", "gout_core_mmc_embd",
	    CLK_CON_DIV_CLKCMU_CORE_MMC_EMBD, 0, 9),
	DIV(CLK_DOUT_CORE_SSS, "dout_core_sss", "gout_core_sss",
	    CLK_CON_DIV_CLKCMU_CORE_SSS, 0, 4),

	/* DPU */
	DIV(CLK_DOUT_DPU, "dout_dpu", "gout_dpu",
	    CLK_CON_DIV_CLKCMU_DPU, 0, 4),

	/* HSI */
	DIV(CLK_DOUT_HSI_BUS, "dout_hsi_bus", "gout_hsi_bus",
	    CLK_CON_DIV_CLKCMU_HSI_BUS, 0, 4),
	DIV(CLK_DOUT_HSI_MMC_CARD, "dout_hsi_mmc_card", "gout_hsi_mmc_card",
	    CLK_CON_DIV_CLKCMU_HSI_MMC_CARD, 0, 9),
	DIV(CLK_DOUT_HSI_USB20DRD, "dout_hsi_usb20drd", "gout_hsi_usb20drd",
	    CLK_CON_DIV_CLKCMU_HSI_USB20DRD, 0, 4),

	/* PERI */
	DIV(CLK_DOUT_PERI_BUS, "dout_peri_bus", "gout_peri_bus",
	    CLK_CON_DIV_CLKCMU_PERI_BUS, 0, 4),
	DIV(CLK_DOUT_PERI_UART, "dout_peri_uart", "gout_peri_uart",
	    CLK_CON_DIV_CLKCMU_PERI_UART, 0, 4),
	DIV(CLK_DOUT_PERI_IP, "dout_peri_ip", "gout_peri_ip",
	    CLK_CON_DIV_CLKCMU_PERI_IP, 0, 4),
};

static const struct samsung_gate_clock top_gate_clks[] __initconst = {
	/* CORE */
	GATE(CLK_GOUT_CORE_BUS, "gout_core_bus", "mout_core_bus",
	     CLK_CON_GAT_GATE_CLKCMU_CORE_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CORE_CCI, "gout_core_cci", "mout_core_cci",
	     CLK_CON_GAT_GATE_CLKCMU_CORE_CCI, 21, 0, 0),
	GATE(CLK_GOUT_CORE_MMC_EMBD, "gout_core_mmc_embd", "mout_core_mmc_embd",
	     CLK_CON_GAT_GATE_CLKCMU_CORE_MMC_EMBD, 21, 0, 0),
	GATE(CLK_GOUT_CORE_SSS, "gout_core_sss", "mout_core_sss",
	     CLK_CON_GAT_GATE_CLKCMU_CORE_SSS, 21, 0, 0),

	/* DPU */
	GATE(CLK_GOUT_DPU, "gout_dpu", "mout_dpu",
	     CLK_CON_GAT_GATE_CLKCMU_DPU, 21, 0, 0),

	/* HSI */
	GATE(CLK_GOUT_HSI_BUS, "gout_hsi_bus", "mout_hsi_bus",
	     CLK_CON_GAT_GATE_CLKCMU_HSI_BUS, 21, 0, 0),
	GATE(CLK_GOUT_HSI_MMC_CARD, "gout_hsi_mmc_card", "mout_hsi_mmc_card",
	     CLK_CON_GAT_GATE_CLKCMU_HSI_MMC_CARD, 21, 0, 0),
	GATE(CLK_GOUT_HSI_USB20DRD, "gout_hsi_usb20drd", "mout_hsi_usb20drd",
	     CLK_CON_GAT_GATE_CLKCMU_HSI_USB20DRD, 21, 0, 0),

	/* PERI */
	GATE(CLK_GOUT_PERI_BUS, "gout_peri_bus", "mout_peri_bus",
	     CLK_CON_GAT_GATE_CLKCMU_PERI_BUS, 21, 0, 0),
	GATE(CLK_GOUT_PERI_UART, "gout_peri_uart", "mout_peri_uart",
	     CLK_CON_GAT_GATE_CLKCMU_PERI_UART, 21, 0, 0),
	GATE(CLK_GOUT_PERI_IP, "gout_peri_ip", "mout_peri_ip",
	     CLK_CON_GAT_GATE_CLKCMU_PERI_IP, 21, 0, 0),
};

static const struct samsung_cmu_info top_cmu_info __initconst = {
	.pll_clks		= top_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(top_pll_clks),
	.mux_clks		= top_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(top_mux_clks),
	.div_clks		= top_div_clks,
	.nr_div_clks		= ARRAY_SIZE(top_div_clks),
	.gate_clks		= top_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(top_gate_clks),
	.nr_clk_ids		= TOP_NR_CLK,
	.clk_regs		= top_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(top_clk_regs),
};

static void __init exynos850_cmu_top_init(struct device_node *np)
{
	exynos850_init_clocks(np, top_clk_regs, ARRAY_SIZE(top_clk_regs));
	samsung_cmu_register_one(np, &top_cmu_info);
}

CLK_OF_DECLARE(exynos850_cmu_top, "samsung,exynos850-cmu-top",
	       exynos850_cmu_top_init);

/* ---- CMU_HSI ------------------------------------------------------------- */

/* Register Offset definitions for CMU_HSI (0x13400000) */
#define PLL_CON0_MUX_CLKCMU_HSI_BUS_USER			0x0600
#define PLL_CON0_MUX_CLKCMU_HSI_MMC_CARD_USER			0x0610
#define PLL_CON0_MUX_CLKCMU_HSI_USB20DRD_USER			0x0620
#define CLK_CON_MUX_MUX_CLK_HSI_RTC				0x1000
#define CLK_CON_GAT_HSI_USB20DRD_TOP_I_RTC_CLK__ALV		0x2008
#define CLK_CON_GAT_HSI_USB20DRD_TOP_I_REF_CLK_50		0x200c
#define CLK_CON_GAT_HSI_USB20DRD_TOP_I_PHY_REFCLK_26		0x2010
#define CLK_CON_GAT_GOUT_HSI_GPIO_HSI_PCLK			0x2018
#define CLK_CON_GAT_GOUT_HSI_MMC_CARD_I_ACLK			0x2024
#define CLK_CON_GAT_GOUT_HSI_MMC_CARD_SDCLKIN			0x2028
#define CLK_CON_GAT_GOUT_HSI_SYSREG_HSI_PCLK			0x2038
#define CLK_CON_GAT_GOUT_HSI_USB20DRD_TOP_ACLK_PHYCTRL_20	0x203c
#define CLK_CON_GAT_GOUT_HSI_USB20DRD_TOP_BUS_CLK_EARLY		0x2040

static const unsigned long hsi_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_HSI_BUS_USER,
	PLL_CON0_MUX_CLKCMU_HSI_MMC_CARD_USER,
	PLL_CON0_MUX_CLKCMU_HSI_USB20DRD_USER,
	CLK_CON_MUX_MUX_CLK_HSI_RTC,
	CLK_CON_GAT_HSI_USB20DRD_TOP_I_RTC_CLK__ALV,
	CLK_CON_GAT_HSI_USB20DRD_TOP_I_REF_CLK_50,
	CLK_CON_GAT_HSI_USB20DRD_TOP_I_PHY_REFCLK_26,
	CLK_CON_GAT_GOUT_HSI_GPIO_HSI_PCLK,
	CLK_CON_GAT_GOUT_HSI_MMC_CARD_I_ACLK,
	CLK_CON_GAT_GOUT_HSI_MMC_CARD_SDCLKIN,
	CLK_CON_GAT_GOUT_HSI_SYSREG_HSI_PCLK,
	CLK_CON_GAT_GOUT_HSI_USB20DRD_TOP_ACLK_PHYCTRL_20,
	CLK_CON_GAT_GOUT_HSI_USB20DRD_TOP_BUS_CLK_EARLY,
};

/* List of parent clocks for Muxes in CMU_PERI */
PNAME(mout_hsi_bus_user_p)	= { "oscclk", "dout_hsi_bus" };
PNAME(mout_hsi_mmc_card_user_p)	= { "oscclk", "dout_hsi_mmc_card" };
PNAME(mout_hsi_usb20drd_user_p)	= { "oscclk", "dout_hsi_usb20drd" };
PNAME(mout_hsi_rtc_p)		= { "rtcclk", "oscclk" };

static const struct samsung_mux_clock hsi_mux_clks[] __initconst = {
	MUX(CLK_MOUT_HSI_BUS_USER, "mout_hsi_bus_user", mout_hsi_bus_user_p,
	    PLL_CON0_MUX_CLKCMU_HSI_BUS_USER, 4, 1),
	MUX_F(CLK_MOUT_HSI_MMC_CARD_USER, "mout_hsi_mmc_card_user",
	      mout_hsi_mmc_card_user_p, PLL_CON0_MUX_CLKCMU_HSI_MMC_CARD_USER,
	      4, 1, CLK_SET_RATE_PARENT, 0),
	MUX(CLK_MOUT_HSI_USB20DRD_USER, "mout_hsi_usb20drd_user",
	    mout_hsi_usb20drd_user_p, PLL_CON0_MUX_CLKCMU_HSI_USB20DRD_USER,
	    4, 1),
	MUX(CLK_MOUT_HSI_RTC, "mout_hsi_rtc", mout_hsi_rtc_p,
	    CLK_CON_MUX_MUX_CLK_HSI_RTC, 0, 1),
};

static const struct samsung_gate_clock hsi_gate_clks[] __initconst = {
	GATE(CLK_GOUT_USB_RTC_CLK, "gout_usb_rtc", "mout_hsi_rtc",
	     CLK_CON_GAT_HSI_USB20DRD_TOP_I_RTC_CLK__ALV, 21, 0, 0),
	GATE(CLK_GOUT_USB_REF_CLK, "gout_usb_ref", "mout_hsi_usb20drd_user",
	     CLK_CON_GAT_HSI_USB20DRD_TOP_I_REF_CLK_50, 21, 0, 0),
	GATE(CLK_GOUT_USB_PHY_REF_CLK, "gout_usb_phy_ref", "oscclk",
	     CLK_CON_GAT_HSI_USB20DRD_TOP_I_PHY_REFCLK_26, 21, 0, 0),
	GATE(CLK_GOUT_GPIO_HSI_PCLK, "gout_gpio_hsi_pclk", "mout_hsi_bus_user",
	     CLK_CON_GAT_GOUT_HSI_GPIO_HSI_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_MMC_CARD_ACLK, "gout_mmc_card_aclk", "mout_hsi_bus_user",
	     CLK_CON_GAT_GOUT_HSI_MMC_CARD_I_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_MMC_CARD_SDCLKIN, "gout_mmc_card_sdclkin",
	     "mout_hsi_mmc_card_user",
	     CLK_CON_GAT_GOUT_HSI_MMC_CARD_SDCLKIN, 21, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_SYSREG_HSI_PCLK, "gout_sysreg_hsi_pclk",
	     "mout_hsi_bus_user",
	     CLK_CON_GAT_GOUT_HSI_SYSREG_HSI_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_USB_PHY_ACLK, "gout_usb_phy_aclk", "mout_hsi_bus_user",
	     CLK_CON_GAT_GOUT_HSI_USB20DRD_TOP_ACLK_PHYCTRL_20, 21, 0, 0),
	GATE(CLK_GOUT_USB_BUS_EARLY_CLK, "gout_usb_bus_early",
	     "mout_hsi_bus_user",
	     CLK_CON_GAT_GOUT_HSI_USB20DRD_TOP_BUS_CLK_EARLY, 21, 0, 0),
};

static const struct samsung_cmu_info hsi_cmu_info __initconst = {
	.mux_clks		= hsi_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(hsi_mux_clks),
	.gate_clks		= hsi_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(hsi_gate_clks),
	.nr_clk_ids		= HSI_NR_CLK,
	.clk_regs		= hsi_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(hsi_clk_regs),
	.clk_name		= "dout_hsi_bus",
};

/* ---- CMU_PERI ------------------------------------------------------------ */

/* Register Offset definitions for CMU_PERI (0x10030000) */
#define PLL_CON0_MUX_CLKCMU_PERI_BUS_USER	0x0600
#define PLL_CON0_MUX_CLKCMU_PERI_HSI2C_USER	0x0610
#define PLL_CON0_MUX_CLKCMU_PERI_SPI_USER	0x0620
#define PLL_CON0_MUX_CLKCMU_PERI_UART_USER	0x0630
#define CLK_CON_DIV_DIV_CLK_PERI_HSI2C_0	0x1800
#define CLK_CON_DIV_DIV_CLK_PERI_HSI2C_1	0x1804
#define CLK_CON_DIV_DIV_CLK_PERI_HSI2C_2	0x1808
#define CLK_CON_DIV_DIV_CLK_PERI_SPI_0		0x180c
#define CLK_CON_GAT_GATE_CLK_PERI_HSI2C_0	0x200c
#define CLK_CON_GAT_GATE_CLK_PERI_HSI2C_1	0x2010
#define CLK_CON_GAT_GATE_CLK_PERI_HSI2C_2	0x2014
#define CLK_CON_GAT_GOUT_PERI_GPIO_PERI_PCLK	0x2020
#define CLK_CON_GAT_GOUT_PERI_HSI2C_0_IPCLK	0x2024
#define CLK_CON_GAT_GOUT_PERI_HSI2C_0_PCLK	0x2028
#define CLK_CON_GAT_GOUT_PERI_HSI2C_1_IPCLK	0x202c
#define CLK_CON_GAT_GOUT_PERI_HSI2C_1_PCLK	0x2030
#define CLK_CON_GAT_GOUT_PERI_HSI2C_2_IPCLK	0x2034
#define CLK_CON_GAT_GOUT_PERI_HSI2C_2_PCLK	0x2038
#define CLK_CON_GAT_GOUT_PERI_I2C_0_PCLK	0x203c
#define CLK_CON_GAT_GOUT_PERI_I2C_1_PCLK	0x2040
#define CLK_CON_GAT_GOUT_PERI_I2C_2_PCLK	0x2044
#define CLK_CON_GAT_GOUT_PERI_I2C_3_PCLK	0x2048
#define CLK_CON_GAT_GOUT_PERI_I2C_4_PCLK	0x204c
#define CLK_CON_GAT_GOUT_PERI_I2C_5_PCLK	0x2050
#define CLK_CON_GAT_GOUT_PERI_I2C_6_PCLK	0x2054
#define CLK_CON_GAT_GOUT_PERI_MCT_PCLK		0x205c
#define CLK_CON_GAT_GOUT_PERI_PWM_MOTOR_PCLK	0x2064
#define CLK_CON_GAT_GOUT_PERI_SPI_0_IPCLK	0x209c
#define CLK_CON_GAT_GOUT_PERI_SPI_0_PCLK	0x20a0
#define CLK_CON_GAT_GOUT_PERI_SYSREG_PERI_PCLK	0x20a4
#define CLK_CON_GAT_GOUT_PERI_UART_IPCLK	0x20a8
#define CLK_CON_GAT_GOUT_PERI_UART_PCLK		0x20ac
#define CLK_CON_GAT_GOUT_PERI_WDT_0_PCLK	0x20b0
#define CLK_CON_GAT_GOUT_PERI_WDT_1_PCLK	0x20b4

static const unsigned long peri_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_PERI_BUS_USER,
	PLL_CON0_MUX_CLKCMU_PERI_HSI2C_USER,
	PLL_CON0_MUX_CLKCMU_PERI_SPI_USER,
	PLL_CON0_MUX_CLKCMU_PERI_UART_USER,
	CLK_CON_DIV_DIV_CLK_PERI_HSI2C_0,
	CLK_CON_DIV_DIV_CLK_PERI_HSI2C_1,
	CLK_CON_DIV_DIV_CLK_PERI_HSI2C_2,
	CLK_CON_DIV_DIV_CLK_PERI_SPI_0,
	CLK_CON_GAT_GATE_CLK_PERI_HSI2C_0,
	CLK_CON_GAT_GATE_CLK_PERI_HSI2C_1,
	CLK_CON_GAT_GATE_CLK_PERI_HSI2C_2,
	CLK_CON_GAT_GOUT_PERI_GPIO_PERI_PCLK,
	CLK_CON_GAT_GOUT_PERI_HSI2C_0_IPCLK,
	CLK_CON_GAT_GOUT_PERI_HSI2C_0_PCLK,
	CLK_CON_GAT_GOUT_PERI_HSI2C_1_IPCLK,
	CLK_CON_GAT_GOUT_PERI_HSI2C_1_PCLK,
	CLK_CON_GAT_GOUT_PERI_HSI2C_2_IPCLK,
	CLK_CON_GAT_GOUT_PERI_HSI2C_2_PCLK,
	CLK_CON_GAT_GOUT_PERI_I2C_0_PCLK,
	CLK_CON_GAT_GOUT_PERI_I2C_1_PCLK,
	CLK_CON_GAT_GOUT_PERI_I2C_2_PCLK,
	CLK_CON_GAT_GOUT_PERI_I2C_3_PCLK,
	CLK_CON_GAT_GOUT_PERI_I2C_4_PCLK,
	CLK_CON_GAT_GOUT_PERI_I2C_5_PCLK,
	CLK_CON_GAT_GOUT_PERI_I2C_6_PCLK,
	CLK_CON_GAT_GOUT_PERI_MCT_PCLK,
	CLK_CON_GAT_GOUT_PERI_PWM_MOTOR_PCLK,
	CLK_CON_GAT_GOUT_PERI_SPI_0_IPCLK,
	CLK_CON_GAT_GOUT_PERI_SPI_0_PCLK,
	CLK_CON_GAT_GOUT_PERI_SYSREG_PERI_PCLK,
	CLK_CON_GAT_GOUT_PERI_UART_IPCLK,
	CLK_CON_GAT_GOUT_PERI_UART_PCLK,
	CLK_CON_GAT_GOUT_PERI_WDT_0_PCLK,
	CLK_CON_GAT_GOUT_PERI_WDT_1_PCLK,
};

/* List of parent clocks for Muxes in CMU_PERI */
PNAME(mout_peri_bus_user_p)	= { "oscclk", "dout_peri_bus" };
PNAME(mout_peri_uart_user_p)	= { "oscclk", "dout_peri_uart" };
PNAME(mout_peri_hsi2c_user_p)	= { "oscclk", "dout_peri_ip" };
PNAME(mout_peri_spi_user_p)	= { "oscclk", "dout_peri_ip" };

static const struct samsung_mux_clock peri_mux_clks[] __initconst = {
	MUX(CLK_MOUT_PERI_BUS_USER, "mout_peri_bus_user", mout_peri_bus_user_p,
	    PLL_CON0_MUX_CLKCMU_PERI_BUS_USER, 4, 1),
	MUX(CLK_MOUT_PERI_UART_USER, "mout_peri_uart_user",
	    mout_peri_uart_user_p, PLL_CON0_MUX_CLKCMU_PERI_UART_USER, 4, 1),
	MUX(CLK_MOUT_PERI_HSI2C_USER, "mout_peri_hsi2c_user",
	    mout_peri_hsi2c_user_p, PLL_CON0_MUX_CLKCMU_PERI_HSI2C_USER, 4, 1),
	MUX(CLK_MOUT_PERI_SPI_USER, "mout_peri_spi_user", mout_peri_spi_user_p,
	    PLL_CON0_MUX_CLKCMU_PERI_SPI_USER, 4, 1),
};

static const struct samsung_div_clock peri_div_clks[] __initconst = {
	DIV(CLK_DOUT_PERI_HSI2C0, "dout_peri_hsi2c0", "gout_peri_hsi2c0",
	    CLK_CON_DIV_DIV_CLK_PERI_HSI2C_0, 0, 5),
	DIV(CLK_DOUT_PERI_HSI2C1, "dout_peri_hsi2c1", "gout_peri_hsi2c1",
	    CLK_CON_DIV_DIV_CLK_PERI_HSI2C_1, 0, 5),
	DIV(CLK_DOUT_PERI_HSI2C2, "dout_peri_hsi2c2", "gout_peri_hsi2c2",
	    CLK_CON_DIV_DIV_CLK_PERI_HSI2C_2, 0, 5),
	DIV(CLK_DOUT_PERI_SPI0, "dout_peri_spi0", "mout_peri_spi_user",
	    CLK_CON_DIV_DIV_CLK_PERI_SPI_0, 0, 5),
};

static const struct samsung_gate_clock peri_gate_clks[] __initconst = {
	GATE(CLK_GOUT_PERI_HSI2C0, "gout_peri_hsi2c0", "mout_peri_hsi2c_user",
	     CLK_CON_GAT_GATE_CLK_PERI_HSI2C_0, 21, 0, 0),
	GATE(CLK_GOUT_PERI_HSI2C1, "gout_peri_hsi2c1", "mout_peri_hsi2c_user",
	     CLK_CON_GAT_GATE_CLK_PERI_HSI2C_1, 21, 0, 0),
	GATE(CLK_GOUT_PERI_HSI2C2, "gout_peri_hsi2c2", "mout_peri_hsi2c_user",
	     CLK_CON_GAT_GATE_CLK_PERI_HSI2C_2, 21, 0, 0),
	GATE(CLK_GOUT_HSI2C0_IPCLK, "gout_hsi2c0_ipclk", "dout_peri_hsi2c0",
	     CLK_CON_GAT_GOUT_PERI_HSI2C_0_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_HSI2C0_PCLK, "gout_hsi2c0_pclk", "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_HSI2C_0_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_HSI2C1_IPCLK, "gout_hsi2c1_ipclk", "dout_peri_hsi2c1",
	     CLK_CON_GAT_GOUT_PERI_HSI2C_1_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_HSI2C1_PCLK, "gout_hsi2c1_pclk", "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_HSI2C_1_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_HSI2C2_IPCLK, "gout_hsi2c2_ipclk", "dout_peri_hsi2c2",
	     CLK_CON_GAT_GOUT_PERI_HSI2C_2_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_HSI2C2_PCLK, "gout_hsi2c2_pclk", "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_HSI2C_2_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_I2C0_PCLK, "gout_i2c0_pclk", "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_I2C_0_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_I2C1_PCLK, "gout_i2c1_pclk", "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_I2C_1_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_I2C2_PCLK, "gout_i2c2_pclk", "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_I2C_2_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_I2C3_PCLK, "gout_i2c3_pclk", "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_I2C_3_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_I2C4_PCLK, "gout_i2c4_pclk", "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_I2C_4_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_I2C5_PCLK, "gout_i2c5_pclk", "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_I2C_5_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_I2C6_PCLK, "gout_i2c6_pclk", "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_I2C_6_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_MCT_PCLK, "gout_mct_pclk", "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_MCT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PWM_MOTOR_PCLK, "gout_pwm_motor_pclk",
	     "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_PWM_MOTOR_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_SPI0_IPCLK, "gout_spi0_ipclk", "dout_peri_spi0",
	     CLK_CON_GAT_GOUT_PERI_SPI_0_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_SPI0_PCLK, "gout_spi0_pclk", "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_SPI_0_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_SYSREG_PERI_PCLK, "gout_sysreg_peri_pclk",
	     "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_SYSREG_PERI_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_UART_IPCLK, "gout_uart_ipclk", "mout_peri_uart_user",
	     CLK_CON_GAT_GOUT_PERI_UART_IPCLK, 21, 0, 0),
	GATE(CLK_GOUT_UART_PCLK, "gout_uart_pclk", "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_UART_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_WDT0_PCLK, "gout_wdt0_pclk", "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_WDT_0_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_WDT1_PCLK, "gout_wdt1_pclk", "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_WDT_1_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_GPIO_PERI_PCLK, "gout_gpio_peri_pclk",
	     "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_GPIO_PERI_PCLK, 21, 0, 0),
};

static const struct samsung_cmu_info peri_cmu_info __initconst = {
	.mux_clks		= peri_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(peri_mux_clks),
	.div_clks		= peri_div_clks,
	.nr_div_clks		= ARRAY_SIZE(peri_div_clks),
	.gate_clks		= peri_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(peri_gate_clks),
	.nr_clk_ids		= PERI_NR_CLK,
	.clk_regs		= peri_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(peri_clk_regs),
	.clk_name		= "dout_peri_bus",
};

/* ---- CMU_CORE ------------------------------------------------------------ */

/* Register Offset definitions for CMU_CORE (0x12000000) */
#define PLL_CON0_MUX_CLKCMU_CORE_BUS_USER	0x0600
#define PLL_CON0_MUX_CLKCMU_CORE_CCI_USER	0x0610
#define PLL_CON0_MUX_CLKCMU_CORE_MMC_EMBD_USER	0x0620
#define PLL_CON0_MUX_CLKCMU_CORE_SSS_USER	0x0630
#define CLK_CON_MUX_MUX_CLK_CORE_GIC		0x1000
#define CLK_CON_DIV_DIV_CLK_CORE_BUSP		0x1800
#define CLK_CON_GAT_GOUT_CORE_CCI_550_ACLK	0x2038
#define CLK_CON_GAT_GOUT_CORE_GIC_CLK		0x2040
#define CLK_CON_GAT_GOUT_CORE_MMC_EMBD_I_ACLK	0x20e8
#define CLK_CON_GAT_GOUT_CORE_MMC_EMBD_SDCLKIN	0x20ec
#define CLK_CON_GAT_GOUT_CORE_SSS_I_ACLK	0x2128
#define CLK_CON_GAT_GOUT_CORE_SSS_I_PCLK	0x212c

static const unsigned long core_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_CORE_BUS_USER,
	PLL_CON0_MUX_CLKCMU_CORE_CCI_USER,
	PLL_CON0_MUX_CLKCMU_CORE_MMC_EMBD_USER,
	PLL_CON0_MUX_CLKCMU_CORE_SSS_USER,
	CLK_CON_MUX_MUX_CLK_CORE_GIC,
	CLK_CON_DIV_DIV_CLK_CORE_BUSP,
	CLK_CON_GAT_GOUT_CORE_CCI_550_ACLK,
	CLK_CON_GAT_GOUT_CORE_GIC_CLK,
	CLK_CON_GAT_GOUT_CORE_MMC_EMBD_I_ACLK,
	CLK_CON_GAT_GOUT_CORE_MMC_EMBD_SDCLKIN,
	CLK_CON_GAT_GOUT_CORE_SSS_I_ACLK,
	CLK_CON_GAT_GOUT_CORE_SSS_I_PCLK,
};

/* List of parent clocks for Muxes in CMU_CORE */
PNAME(mout_core_bus_user_p)		= { "oscclk", "dout_core_bus" };
PNAME(mout_core_cci_user_p)		= { "oscclk", "dout_core_cci" };
PNAME(mout_core_mmc_embd_user_p)	= { "oscclk", "dout_core_mmc_embd" };
PNAME(mout_core_sss_user_p)		= { "oscclk", "dout_core_sss" };
PNAME(mout_core_gic_p)			= { "dout_core_busp", "oscclk" };

static const struct samsung_mux_clock core_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CORE_BUS_USER, "mout_core_bus_user", mout_core_bus_user_p,
	    PLL_CON0_MUX_CLKCMU_CORE_BUS_USER, 4, 1),
	MUX(CLK_MOUT_CORE_CCI_USER, "mout_core_cci_user", mout_core_cci_user_p,
	    PLL_CON0_MUX_CLKCMU_CORE_CCI_USER, 4, 1),
	MUX_F(CLK_MOUT_CORE_MMC_EMBD_USER, "mout_core_mmc_embd_user",
	      mout_core_mmc_embd_user_p, PLL_CON0_MUX_CLKCMU_CORE_MMC_EMBD_USER,
	      4, 1, CLK_SET_RATE_PARENT, 0),
	MUX(CLK_MOUT_CORE_SSS_USER, "mout_core_sss_user", mout_core_sss_user_p,
	    PLL_CON0_MUX_CLKCMU_CORE_SSS_USER, 4, 1),
	MUX(CLK_MOUT_CORE_GIC, "mout_core_gic", mout_core_gic_p,
	    CLK_CON_MUX_MUX_CLK_CORE_GIC, 0, 1),
};

static const struct samsung_div_clock core_div_clks[] __initconst = {
	DIV(CLK_DOUT_CORE_BUSP, "dout_core_busp", "mout_core_bus_user",
	    CLK_CON_DIV_DIV_CLK_CORE_BUSP, 0, 2),
};

static const struct samsung_gate_clock core_gate_clks[] __initconst = {
	GATE(CLK_GOUT_CCI_ACLK, "gout_cci_aclk", "mout_core_cci_user",
	     CLK_CON_GAT_GOUT_CORE_CCI_550_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_GIC_CLK, "gout_gic_clk", "mout_core_gic",
	     CLK_CON_GAT_GOUT_CORE_GIC_CLK, 21, 0, 0),
	GATE(CLK_GOUT_MMC_EMBD_ACLK, "gout_mmc_embd_aclk", "dout_core_busp",
	     CLK_CON_GAT_GOUT_CORE_MMC_EMBD_I_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_MMC_EMBD_SDCLKIN, "gout_mmc_embd_sdclkin",
	     "mout_core_mmc_embd_user", CLK_CON_GAT_GOUT_CORE_MMC_EMBD_SDCLKIN,
	     21, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_SSS_ACLK, "gout_sss_aclk", "mout_core_sss_user",
	     CLK_CON_GAT_GOUT_CORE_SSS_I_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_SSS_PCLK, "gout_sss_pclk", "dout_core_busp",
	     CLK_CON_GAT_GOUT_CORE_SSS_I_PCLK, 21, 0, 0),
};

static const struct samsung_cmu_info core_cmu_info __initconst = {
	.mux_clks		= core_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(core_mux_clks),
	.div_clks		= core_div_clks,
	.nr_div_clks		= ARRAY_SIZE(core_div_clks),
	.gate_clks		= core_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(core_gate_clks),
	.nr_clk_ids		= CORE_NR_CLK,
	.clk_regs		= core_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(core_clk_regs),
	.clk_name		= "dout_core_bus",
};

/* ---- CMU_DPU ------------------------------------------------------------- */

/* Register Offset definitions for CMU_DPU (0x13000000) */
#define PLL_CON0_MUX_CLKCMU_DPU_USER		0x0600
#define CLK_CON_DIV_DIV_CLK_DPU_BUSP		0x1800
#define CLK_CON_GAT_CLK_DPU_CMU_DPU_PCLK	0x2004
#define CLK_CON_GAT_GOUT_DPU_ACLK_DECON0	0x2010
#define CLK_CON_GAT_GOUT_DPU_ACLK_DMA		0x2014
#define CLK_CON_GAT_GOUT_DPU_ACLK_DPP		0x2018
#define CLK_CON_GAT_GOUT_DPU_PPMU_ACLK		0x2028
#define CLK_CON_GAT_GOUT_DPU_PPMU_PCLK		0x202c
#define CLK_CON_GAT_GOUT_DPU_SMMU_CLK		0x2038
#define CLK_CON_GAT_GOUT_DPU_SYSREG_PCLK	0x203c

static const unsigned long dpu_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_DPU_USER,
	CLK_CON_DIV_DIV_CLK_DPU_BUSP,
	CLK_CON_GAT_CLK_DPU_CMU_DPU_PCLK,
	CLK_CON_GAT_GOUT_DPU_ACLK_DECON0,
	CLK_CON_GAT_GOUT_DPU_ACLK_DMA,
	CLK_CON_GAT_GOUT_DPU_ACLK_DPP,
	CLK_CON_GAT_GOUT_DPU_PPMU_ACLK,
	CLK_CON_GAT_GOUT_DPU_PPMU_PCLK,
	CLK_CON_GAT_GOUT_DPU_SMMU_CLK,
	CLK_CON_GAT_GOUT_DPU_SYSREG_PCLK,
};

/* List of parent clocks for Muxes in CMU_CORE */
PNAME(mout_dpu_user_p)		= { "oscclk", "dout_dpu" };

static const struct samsung_mux_clock dpu_mux_clks[] __initconst = {
	MUX(CLK_MOUT_DPU_USER, "mout_dpu_user", mout_dpu_user_p,
	    PLL_CON0_MUX_CLKCMU_DPU_USER, 4, 1),
};

static const struct samsung_div_clock dpu_div_clks[] __initconst = {
	DIV(CLK_DOUT_DPU_BUSP, "dout_dpu_busp", "mout_dpu_user",
	    CLK_CON_DIV_DIV_CLK_DPU_BUSP, 0, 3),
};

static const struct samsung_gate_clock dpu_gate_clks[] __initconst = {
	GATE(CLK_GOUT_DPU_CMU_DPU_PCLK, "gout_dpu_cmu_dpu_pclk",
	     "dout_dpu_busp", CLK_CON_GAT_CLK_DPU_CMU_DPU_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_DPU_DECON0_ACLK, "gout_dpu_decon0_aclk", "mout_dpu_user",
	     CLK_CON_GAT_GOUT_DPU_ACLK_DECON0, 21, 0, 0),
	GATE(CLK_GOUT_DPU_DMA_ACLK, "gout_dpu_dma_aclk", "mout_dpu_user",
	     CLK_CON_GAT_GOUT_DPU_ACLK_DMA, 21, 0, 0),
	GATE(CLK_GOUT_DPU_DPP_ACLK, "gout_dpu_dpp_aclk", "mout_dpu_user",
	     CLK_CON_GAT_GOUT_DPU_ACLK_DPP, 21, 0, 0),
	GATE(CLK_GOUT_DPU_PPMU_ACLK, "gout_dpu_ppmu_aclk", "mout_dpu_user",
	     CLK_CON_GAT_GOUT_DPU_PPMU_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_DPU_PPMU_PCLK, "gout_dpu_ppmu_pclk", "dout_dpu_busp",
	     CLK_CON_GAT_GOUT_DPU_PPMU_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_DPU_SMMU_CLK, "gout_dpu_smmu_clk", "mout_dpu_user",
	     CLK_CON_GAT_GOUT_DPU_SMMU_CLK, 21, 0, 0),
	GATE(CLK_GOUT_DPU_SYSREG_PCLK, "gout_dpu_sysreg_pclk", "dout_dpu_busp",
	     CLK_CON_GAT_GOUT_DPU_SYSREG_PCLK, 21, 0, 0),
};

static const struct samsung_cmu_info dpu_cmu_info __initconst = {
	.mux_clks		= dpu_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(dpu_mux_clks),
	.div_clks		= dpu_div_clks,
	.nr_div_clks		= ARRAY_SIZE(dpu_div_clks),
	.gate_clks		= dpu_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(dpu_gate_clks),
	.nr_clk_ids		= DPU_NR_CLK,
	.clk_regs		= dpu_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(dpu_clk_regs),
	.clk_name		= "dout_dpu",
};

/* ---- platform_driver ----------------------------------------------------- */

static int __init exynos850_cmu_probe(struct platform_device *pdev)
{
	const struct samsung_cmu_info *info;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;

	info = of_device_get_match_data(dev);
	exynos850_init_clocks(np, info->clk_regs, info->nr_clk_regs);
	samsung_cmu_register_one(np, info);

	/* Keep bus clock running, so it's possible to access CMU registers */
	if (info->clk_name) {
		struct clk *bus_clk;

		bus_clk = clk_get(dev, info->clk_name);
		if (IS_ERR(bus_clk)) {
			pr_err("%s: could not find bus clock %s; err = %ld\n",
			       __func__, info->clk_name, PTR_ERR(bus_clk));
		} else {
			clk_prepare_enable(bus_clk);
		}
	}

	return 0;
}

/* CMUs which belong to Power Domains and need runtime PM to be implemented */
static const struct of_device_id exynos850_cmu_of_match[] = {
	{
		.compatible = "samsung,exynos850-cmu-hsi",
		.data = &hsi_cmu_info,
	}, {
		.compatible = "samsung,exynos850-cmu-peri",
		.data = &peri_cmu_info,
	}, {
		.compatible = "samsung,exynos850-cmu-core",
		.data = &core_cmu_info,
	}, {
		.compatible = "samsung,exynos850-cmu-dpu",
		.data = &dpu_cmu_info,
	}, {
	},
};

static struct platform_driver exynos850_cmu_driver __refdata = {
	.driver	= {
		.name = "exynos850-cmu",
		.of_match_table = exynos850_cmu_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = exynos850_cmu_probe,
};

static int __init exynos850_cmu_init(void)
{
	return platform_driver_register(&exynos850_cmu_driver);
}
core_initcall(exynos850_cmu_init);
