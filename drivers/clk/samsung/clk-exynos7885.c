// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Dávid Virág <virag.david003@gmail.com>
 * Author: Dávid Virág <virag.david003@gmail.com>
 *
 * Common Clock Framework support for Exynos7885 SoC.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <dt-bindings/clock/exynos7885.h>

#include "clk.h"
#include "clk-exynos-arm64.h"

/* NOTE: Must be equal to the last clock ID increased by one */
#define CLKS_NR_TOP			(CLK_GOUT_FSYS_USB30DRD + 1)
#define CLKS_NR_CORE			(CLK_GOUT_TREX_P_CORE_PCLK_P_CORE + 1)
#define CLKS_NR_PERI			(CLK_GOUT_WDT1_PCLK + 1)
#define CLKS_NR_FSYS			(CLK_GOUT_MMC_SDIO_SDCLKIN + 1)

/* ---- CMU_TOP ------------------------------------------------------------- */

/* Register Offset definitions for CMU_TOP (0x12060000) */
#define PLL_LOCKTIME_PLL_SHARED0		0x0000
#define PLL_LOCKTIME_PLL_SHARED1		0x0004
#define PLL_CON0_PLL_SHARED0			0x0100
#define PLL_CON0_PLL_SHARED1			0x0120
#define CLK_CON_MUX_MUX_CLKCMU_CORE_BUS		0x1014
#define CLK_CON_MUX_MUX_CLKCMU_CORE_CCI		0x1018
#define CLK_CON_MUX_MUX_CLKCMU_CORE_G3D		0x101c
#define CLK_CON_MUX_MUX_CLKCMU_FSYS_BUS		0x1028
#define CLK_CON_MUX_MUX_CLKCMU_FSYS_MMC_CARD	0x102c
#define CLK_CON_MUX_MUX_CLKCMU_FSYS_MMC_EMBD	0x1030
#define CLK_CON_MUX_MUX_CLKCMU_FSYS_MMC_SDIO	0x1034
#define CLK_CON_MUX_MUX_CLKCMU_FSYS_USB30DRD	0x1038
#define CLK_CON_MUX_MUX_CLKCMU_PERI_BUS		0x1058
#define CLK_CON_MUX_MUX_CLKCMU_PERI_SPI0	0x105c
#define CLK_CON_MUX_MUX_CLKCMU_PERI_SPI1	0x1060
#define CLK_CON_MUX_MUX_CLKCMU_PERI_UART0	0x1064
#define CLK_CON_MUX_MUX_CLKCMU_PERI_UART1	0x1068
#define CLK_CON_MUX_MUX_CLKCMU_PERI_UART2	0x106c
#define CLK_CON_MUX_MUX_CLKCMU_PERI_USI0	0x1070
#define CLK_CON_MUX_MUX_CLKCMU_PERI_USI1	0x1074
#define CLK_CON_MUX_MUX_CLKCMU_PERI_USI2	0x1078
#define CLK_CON_DIV_CLKCMU_CORE_BUS		0x181c
#define CLK_CON_DIV_CLKCMU_CORE_CCI		0x1820
#define CLK_CON_DIV_CLKCMU_CORE_G3D		0x1824
#define CLK_CON_DIV_CLKCMU_FSYS_BUS		0x1844
#define CLK_CON_DIV_CLKCMU_FSYS_MMC_CARD	0x1848
#define CLK_CON_DIV_CLKCMU_FSYS_MMC_EMBD	0x184c
#define CLK_CON_DIV_CLKCMU_FSYS_MMC_SDIO	0x1850
#define CLK_CON_DIV_CLKCMU_FSYS_USB30DRD	0x1854
#define CLK_CON_DIV_CLKCMU_PERI_BUS		0x1874
#define CLK_CON_DIV_CLKCMU_PERI_SPI0		0x1878
#define CLK_CON_DIV_CLKCMU_PERI_SPI1		0x187c
#define CLK_CON_DIV_CLKCMU_PERI_UART0		0x1880
#define CLK_CON_DIV_CLKCMU_PERI_UART1		0x1884
#define CLK_CON_DIV_CLKCMU_PERI_UART2		0x1888
#define CLK_CON_DIV_CLKCMU_PERI_USI0		0x188c
#define CLK_CON_DIV_CLKCMU_PERI_USI1		0x1890
#define CLK_CON_DIV_CLKCMU_PERI_USI2		0x1894
#define CLK_CON_DIV_PLL_SHARED0_DIV2		0x189c
#define CLK_CON_DIV_PLL_SHARED0_DIV3		0x18a0
#define CLK_CON_DIV_PLL_SHARED0_DIV4		0x18a4
#define CLK_CON_DIV_PLL_SHARED0_DIV5		0x18a8
#define CLK_CON_DIV_PLL_SHARED1_DIV2		0x18ac
#define CLK_CON_DIV_PLL_SHARED1_DIV3		0x18b0
#define CLK_CON_DIV_PLL_SHARED1_DIV4		0x18b4
#define CLK_CON_GAT_GATE_CLKCMUC_PERI_UART1	0x2004
#define CLK_CON_GAT_GATE_CLKCMU_CORE_BUS	0x201c
#define CLK_CON_GAT_GATE_CLKCMU_CORE_CCI	0x2020
#define CLK_CON_GAT_GATE_CLKCMU_CORE_G3D	0x2024
#define CLK_CON_GAT_GATE_CLKCMU_FSYS_BUS	0x2044
#define CLK_CON_GAT_GATE_CLKCMU_FSYS_MMC_CARD	0x2048
#define CLK_CON_GAT_GATE_CLKCMU_FSYS_MMC_EMBD	0x204c
#define CLK_CON_GAT_GATE_CLKCMU_FSYS_MMC_SDIO	0x2050
#define CLK_CON_GAT_GATE_CLKCMU_FSYS_USB30DRD	0x2054
#define CLK_CON_GAT_GATE_CLKCMU_PERI_BUS	0x207c
#define CLK_CON_GAT_GATE_CLKCMU_PERI_SPI0	0x2080
#define CLK_CON_GAT_GATE_CLKCMU_PERI_SPI1	0x2084
#define CLK_CON_GAT_GATE_CLKCMU_PERI_UART0	0x2088
#define CLK_CON_GAT_GATE_CLKCMU_PERI_UART2	0x208c
#define CLK_CON_GAT_GATE_CLKCMU_PERI_USI0	0x2090
#define CLK_CON_GAT_GATE_CLKCMU_PERI_USI1	0x2094
#define CLK_CON_GAT_GATE_CLKCMU_PERI_USI2	0x2098

static const unsigned long top_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_SHARED0,
	PLL_LOCKTIME_PLL_SHARED1,
	PLL_CON0_PLL_SHARED0,
	PLL_CON0_PLL_SHARED1,
	CLK_CON_MUX_MUX_CLKCMU_CORE_BUS,
	CLK_CON_MUX_MUX_CLKCMU_CORE_CCI,
	CLK_CON_MUX_MUX_CLKCMU_CORE_G3D,
	CLK_CON_MUX_MUX_CLKCMU_FSYS_BUS,
	CLK_CON_MUX_MUX_CLKCMU_FSYS_MMC_CARD,
	CLK_CON_MUX_MUX_CLKCMU_FSYS_MMC_EMBD,
	CLK_CON_MUX_MUX_CLKCMU_FSYS_MMC_SDIO,
	CLK_CON_MUX_MUX_CLKCMU_FSYS_USB30DRD,
	CLK_CON_MUX_MUX_CLKCMU_PERI_BUS,
	CLK_CON_MUX_MUX_CLKCMU_PERI_SPI0,
	CLK_CON_MUX_MUX_CLKCMU_PERI_SPI1,
	CLK_CON_MUX_MUX_CLKCMU_PERI_UART0,
	CLK_CON_MUX_MUX_CLKCMU_PERI_UART1,
	CLK_CON_MUX_MUX_CLKCMU_PERI_UART2,
	CLK_CON_MUX_MUX_CLKCMU_PERI_USI0,
	CLK_CON_MUX_MUX_CLKCMU_PERI_USI1,
	CLK_CON_MUX_MUX_CLKCMU_PERI_USI2,
	CLK_CON_DIV_CLKCMU_CORE_BUS,
	CLK_CON_DIV_CLKCMU_CORE_CCI,
	CLK_CON_DIV_CLKCMU_CORE_G3D,
	CLK_CON_DIV_CLKCMU_FSYS_BUS,
	CLK_CON_DIV_CLKCMU_FSYS_MMC_CARD,
	CLK_CON_DIV_CLKCMU_FSYS_MMC_EMBD,
	CLK_CON_DIV_CLKCMU_FSYS_MMC_SDIO,
	CLK_CON_DIV_CLKCMU_FSYS_USB30DRD,
	CLK_CON_DIV_CLKCMU_PERI_BUS,
	CLK_CON_DIV_CLKCMU_PERI_SPI0,
	CLK_CON_DIV_CLKCMU_PERI_SPI1,
	CLK_CON_DIV_CLKCMU_PERI_UART0,
	CLK_CON_DIV_CLKCMU_PERI_UART1,
	CLK_CON_DIV_CLKCMU_PERI_UART2,
	CLK_CON_DIV_CLKCMU_PERI_USI0,
	CLK_CON_DIV_CLKCMU_PERI_USI1,
	CLK_CON_DIV_CLKCMU_PERI_USI2,
	CLK_CON_DIV_PLL_SHARED0_DIV2,
	CLK_CON_DIV_PLL_SHARED0_DIV3,
	CLK_CON_DIV_PLL_SHARED0_DIV4,
	CLK_CON_DIV_PLL_SHARED0_DIV5,
	CLK_CON_DIV_PLL_SHARED1_DIV2,
	CLK_CON_DIV_PLL_SHARED1_DIV3,
	CLK_CON_DIV_PLL_SHARED1_DIV4,
	CLK_CON_GAT_GATE_CLKCMUC_PERI_UART1,
	CLK_CON_GAT_GATE_CLKCMU_CORE_BUS,
	CLK_CON_GAT_GATE_CLKCMU_CORE_CCI,
	CLK_CON_GAT_GATE_CLKCMU_CORE_G3D,
	CLK_CON_GAT_GATE_CLKCMU_FSYS_BUS,
	CLK_CON_GAT_GATE_CLKCMU_FSYS_MMC_CARD,
	CLK_CON_GAT_GATE_CLKCMU_FSYS_MMC_EMBD,
	CLK_CON_GAT_GATE_CLKCMU_FSYS_MMC_SDIO,
	CLK_CON_GAT_GATE_CLKCMU_FSYS_USB30DRD,
	CLK_CON_GAT_GATE_CLKCMU_PERI_BUS,
	CLK_CON_GAT_GATE_CLKCMU_PERI_SPI0,
	CLK_CON_GAT_GATE_CLKCMU_PERI_SPI1,
	CLK_CON_GAT_GATE_CLKCMU_PERI_UART0,
	CLK_CON_GAT_GATE_CLKCMU_PERI_UART2,
	CLK_CON_GAT_GATE_CLKCMU_PERI_USI0,
	CLK_CON_GAT_GATE_CLKCMU_PERI_USI1,
	CLK_CON_GAT_GATE_CLKCMU_PERI_USI2,
};

static const struct samsung_pll_clock top_pll_clks[] __initconst = {
	PLL(pll_1417x, CLK_FOUT_SHARED0_PLL, "fout_shared0_pll", "oscclk",
	    PLL_LOCKTIME_PLL_SHARED0, PLL_CON0_PLL_SHARED0,
	    NULL),
	PLL(pll_1417x, CLK_FOUT_SHARED1_PLL, "fout_shared1_pll", "oscclk",
	    PLL_LOCKTIME_PLL_SHARED1, PLL_CON0_PLL_SHARED1,
	    NULL),
};

/* List of parent clocks for Muxes in CMU_TOP: for CMU_CORE */
PNAME(mout_core_bus_p)		= { "dout_shared0_div2", "dout_shared1_div2",
				    "dout_shared0_div3", "dout_shared0_div3" };
PNAME(mout_core_cci_p)		= { "dout_shared0_div2", "dout_shared1_div2",
				    "dout_shared0_div3", "dout_shared0_div3" };
PNAME(mout_core_g3d_p)		= { "dout_shared0_div2", "dout_shared1_div2",
				    "dout_shared0_div3", "dout_shared0_div3" };

/* List of parent clocks for Muxes in CMU_TOP: for CMU_PERI */
PNAME(mout_peri_bus_p)		= { "dout_shared0_div4", "dout_shared1_div4" };
PNAME(mout_peri_spi0_p)		= { "oscclk", "dout_shared0_div4" };
PNAME(mout_peri_spi1_p)		= { "oscclk", "dout_shared0_div4" };
PNAME(mout_peri_uart0_p)	= { "oscclk", "dout_shared0_div4" };
PNAME(mout_peri_uart1_p)	= { "oscclk", "dout_shared0_div4" };
PNAME(mout_peri_uart2_p)	= { "oscclk", "dout_shared0_div4" };
PNAME(mout_peri_usi0_p)		= { "oscclk", "dout_shared0_div4" };
PNAME(mout_peri_usi1_p)		= { "oscclk", "dout_shared0_div4" };
PNAME(mout_peri_usi2_p)		= { "oscclk", "dout_shared0_div4" };

/* List of parent clocks for Muxes in CMU_TOP: for CMU_FSYS */
PNAME(mout_fsys_bus_p)		= { "dout_shared0_div2", "dout_shared1_div2" };
PNAME(mout_fsys_mmc_card_p)	= { "dout_shared0_div2", "dout_shared1_div2" };
PNAME(mout_fsys_mmc_embd_p)	= { "dout_shared0_div2", "dout_shared1_div2" };
PNAME(mout_fsys_mmc_sdio_p)	= { "dout_shared0_div2", "dout_shared1_div2" };
PNAME(mout_fsys_usb30drd_p)	= { "dout_shared0_div4", "dout_shared1_div4" };

static const struct samsung_mux_clock top_mux_clks[] __initconst = {
	/* CORE */
	MUX(CLK_MOUT_CORE_BUS, "mout_core_bus", mout_core_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_CORE_BUS, 0, 2),
	MUX(CLK_MOUT_CORE_CCI, "mout_core_cci", mout_core_cci_p,
	    CLK_CON_MUX_MUX_CLKCMU_CORE_CCI, 0, 2),
	MUX(CLK_MOUT_CORE_G3D, "mout_core_g3d", mout_core_g3d_p,
	    CLK_CON_MUX_MUX_CLKCMU_CORE_G3D, 0, 2),

	/* PERI */
	MUX(CLK_MOUT_PERI_BUS, "mout_peri_bus", mout_peri_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_PERI_BUS, 0, 1),
	MUX(CLK_MOUT_PERI_SPI0, "mout_peri_spi0", mout_peri_spi0_p,
	    CLK_CON_MUX_MUX_CLKCMU_PERI_SPI0, 0, 1),
	MUX(CLK_MOUT_PERI_SPI1, "mout_peri_spi1", mout_peri_spi1_p,
	    CLK_CON_MUX_MUX_CLKCMU_PERI_SPI1, 0, 1),
	MUX(CLK_MOUT_PERI_UART0, "mout_peri_uart0", mout_peri_uart0_p,
	    CLK_CON_MUX_MUX_CLKCMU_PERI_UART0, 0, 1),
	MUX(CLK_MOUT_PERI_UART1, "mout_peri_uart1", mout_peri_uart1_p,
	    CLK_CON_MUX_MUX_CLKCMU_PERI_UART1, 0, 1),
	MUX(CLK_MOUT_PERI_UART2, "mout_peri_uart2", mout_peri_uart2_p,
	    CLK_CON_MUX_MUX_CLKCMU_PERI_UART2, 0, 1),
	MUX(CLK_MOUT_PERI_USI0, "mout_peri_usi0", mout_peri_usi0_p,
	    CLK_CON_MUX_MUX_CLKCMU_PERI_USI0, 0, 1),
	MUX(CLK_MOUT_PERI_USI1, "mout_peri_usi1", mout_peri_usi1_p,
	    CLK_CON_MUX_MUX_CLKCMU_PERI_USI1, 0, 1),
	MUX(CLK_MOUT_PERI_USI2, "mout_peri_usi2", mout_peri_usi2_p,
	    CLK_CON_MUX_MUX_CLKCMU_PERI_USI2, 0, 1),

	/* FSYS */
	MUX(CLK_MOUT_FSYS_BUS, "mout_fsys_bus", mout_fsys_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_FSYS_BUS, 0, 1),
	MUX(CLK_MOUT_FSYS_MMC_CARD, "mout_fsys_mmc_card", mout_fsys_mmc_card_p,
	    CLK_CON_MUX_MUX_CLKCMU_FSYS_MMC_CARD, 0, 1),
	MUX(CLK_MOUT_FSYS_MMC_EMBD, "mout_fsys_mmc_embd", mout_fsys_mmc_embd_p,
	    CLK_CON_MUX_MUX_CLKCMU_FSYS_MMC_EMBD, 0, 1),
	MUX(CLK_MOUT_FSYS_MMC_SDIO, "mout_fsys_mmc_sdio", mout_fsys_mmc_sdio_p,
	    CLK_CON_MUX_MUX_CLKCMU_FSYS_MMC_SDIO, 0, 1),
	MUX(CLK_MOUT_FSYS_USB30DRD, "mout_fsys_usb30drd", mout_fsys_usb30drd_p,
	    CLK_CON_MUX_MUX_CLKCMU_FSYS_USB30DRD, 0, 1),
};

static const struct samsung_div_clock top_div_clks[] __initconst = {
	/* TOP */
	DIV(CLK_DOUT_SHARED0_DIV2, "dout_shared0_div2", "fout_shared0_pll",
	    CLK_CON_DIV_PLL_SHARED0_DIV2, 0, 1),
	DIV(CLK_DOUT_SHARED0_DIV3, "dout_shared0_div3", "fout_shared0_pll",
	    CLK_CON_DIV_PLL_SHARED0_DIV3, 0, 2),
	DIV(CLK_DOUT_SHARED0_DIV4, "dout_shared0_div4", "dout_shared0_div2",
	    CLK_CON_DIV_PLL_SHARED0_DIV4, 0, 1),
	DIV(CLK_DOUT_SHARED0_DIV5, "dout_shared0_div5", "fout_shared0_pll",
	    CLK_CON_DIV_PLL_SHARED0_DIV5, 0, 3),
	DIV(CLK_DOUT_SHARED1_DIV2, "dout_shared1_div2", "fout_shared1_pll",
	    CLK_CON_DIV_PLL_SHARED1_DIV2, 0, 1),
	DIV(CLK_DOUT_SHARED1_DIV3, "dout_shared1_div3", "fout_shared1_pll",
	    CLK_CON_DIV_PLL_SHARED1_DIV3, 0, 2),
	DIV(CLK_DOUT_SHARED1_DIV4, "dout_shared1_div4", "dout_shared1_div2",
	    CLK_CON_DIV_PLL_SHARED1_DIV4, 0, 1),

	/* CORE */
	DIV(CLK_DOUT_CORE_BUS, "dout_core_bus", "gout_core_bus",
	    CLK_CON_DIV_CLKCMU_CORE_BUS, 0, 3),
	DIV(CLK_DOUT_CORE_CCI, "dout_core_cci", "gout_core_cci",
	    CLK_CON_DIV_CLKCMU_CORE_CCI, 0, 3),
	DIV(CLK_DOUT_CORE_G3D, "dout_core_g3d", "gout_core_g3d",
	    CLK_CON_DIV_CLKCMU_CORE_G3D, 0, 3),

	/* PERI */
	DIV(CLK_DOUT_PERI_BUS, "dout_peri_bus", "gout_peri_bus",
	    CLK_CON_DIV_CLKCMU_PERI_BUS, 0, 4),
	DIV(CLK_DOUT_PERI_SPI0, "dout_peri_spi0", "gout_peri_spi0",
	    CLK_CON_DIV_CLKCMU_PERI_SPI0, 0, 6),
	DIV(CLK_DOUT_PERI_SPI1, "dout_peri_spi1", "gout_peri_spi1",
	    CLK_CON_DIV_CLKCMU_PERI_SPI1, 0, 6),
	DIV(CLK_DOUT_PERI_UART0, "dout_peri_uart0", "gout_peri_uart0",
	    CLK_CON_DIV_CLKCMU_PERI_UART0, 0, 4),
	DIV(CLK_DOUT_PERI_UART1, "dout_peri_uart1", "gout_peri_uart1",
	    CLK_CON_DIV_CLKCMU_PERI_UART1, 0, 4),
	DIV(CLK_DOUT_PERI_UART2, "dout_peri_uart2", "gout_peri_uart2",
	    CLK_CON_DIV_CLKCMU_PERI_UART2, 0, 4),
	DIV(CLK_DOUT_PERI_USI0, "dout_peri_usi0", "gout_peri_usi0",
	    CLK_CON_DIV_CLKCMU_PERI_USI0, 0, 4),
	DIV(CLK_DOUT_PERI_USI1, "dout_peri_usi1", "gout_peri_usi1",
	    CLK_CON_DIV_CLKCMU_PERI_USI1, 0, 4),
	DIV(CLK_DOUT_PERI_USI2, "dout_peri_usi2", "gout_peri_usi2",
	    CLK_CON_DIV_CLKCMU_PERI_USI2, 0, 4),

	/* FSYS */
	DIV(CLK_DOUT_FSYS_BUS, "dout_fsys_bus", "gout_fsys_bus",
	    CLK_CON_DIV_CLKCMU_FSYS_BUS, 0, 4),
	DIV(CLK_DOUT_FSYS_MMC_CARD, "dout_fsys_mmc_card", "gout_fsys_mmc_card",
	    CLK_CON_DIV_CLKCMU_FSYS_MMC_CARD, 0, 9),
	DIV(CLK_DOUT_FSYS_MMC_EMBD, "dout_fsys_mmc_embd", "gout_fsys_mmc_embd",
	    CLK_CON_DIV_CLKCMU_FSYS_MMC_EMBD, 0, 9),
	DIV(CLK_DOUT_FSYS_MMC_SDIO, "dout_fsys_mmc_sdio", "gout_fsys_mmc_sdio",
	    CLK_CON_DIV_CLKCMU_FSYS_MMC_SDIO, 0, 9),
	DIV(CLK_DOUT_FSYS_USB30DRD, "dout_fsys_usb30drd", "gout_fsys_usb30drd",
	    CLK_CON_DIV_CLKCMU_FSYS_USB30DRD, 0, 4),
};

static const struct samsung_gate_clock top_gate_clks[] __initconst = {
	/* CORE */
	GATE(CLK_GOUT_CORE_BUS, "gout_core_bus", "mout_core_bus",
	     CLK_CON_GAT_GATE_CLKCMU_CORE_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CORE_CCI, "gout_core_cci", "mout_core_cci",
	     CLK_CON_GAT_GATE_CLKCMU_CORE_CCI, 21, 0, 0),
	GATE(CLK_GOUT_CORE_G3D, "gout_core_g3d", "mout_core_g3d",
	     CLK_CON_GAT_GATE_CLKCMU_CORE_G3D, 21, 0, 0),

	/* PERI */
	GATE(CLK_GOUT_PERI_BUS, "gout_peri_bus", "mout_peri_bus",
	     CLK_CON_GAT_GATE_CLKCMU_PERI_BUS, 21, 0, 0),
	GATE(CLK_GOUT_PERI_SPI0, "gout_peri_spi0", "mout_peri_spi0",
	     CLK_CON_GAT_GATE_CLKCMU_PERI_SPI0, 21, 0, 0),
	GATE(CLK_GOUT_PERI_SPI1, "gout_peri_spi1", "mout_peri_spi1",
	     CLK_CON_GAT_GATE_CLKCMU_PERI_SPI1, 21, 0, 0),
	GATE(CLK_GOUT_PERI_UART0, "gout_peri_uart0", "mout_peri_uart0",
	     CLK_CON_GAT_GATE_CLKCMU_PERI_UART0, 21, 0, 0),
	GATE(CLK_GOUT_PERI_UART1, "gout_peri_uart1", "mout_peri_uart1",
	     CLK_CON_GAT_GATE_CLKCMUC_PERI_UART1, 21, 0, 0),
	GATE(CLK_GOUT_PERI_UART2, "gout_peri_uart2", "mout_peri_uart2",
	     CLK_CON_GAT_GATE_CLKCMU_PERI_UART2, 21, 0, 0),
	GATE(CLK_GOUT_PERI_USI0, "gout_peri_usi0", "mout_peri_usi0",
	     CLK_CON_GAT_GATE_CLKCMU_PERI_USI0, 21, 0, 0),
	GATE(CLK_GOUT_PERI_USI1, "gout_peri_usi1", "mout_peri_usi1",
	     CLK_CON_GAT_GATE_CLKCMU_PERI_USI1, 21, 0, 0),
	GATE(CLK_GOUT_PERI_USI2, "gout_peri_usi2", "mout_peri_usi2",
	     CLK_CON_GAT_GATE_CLKCMU_PERI_USI2, 21, 0, 0),

	/* FSYS */
	GATE(CLK_GOUT_FSYS_BUS, "gout_fsys_bus", "mout_fsys_bus",
	     CLK_CON_GAT_GATE_CLKCMU_FSYS_BUS, 21, 0, 0),
	GATE(CLK_GOUT_FSYS_MMC_CARD, "gout_fsys_mmc_card", "mout_fsys_mmc_card",
	     CLK_CON_GAT_GATE_CLKCMU_FSYS_MMC_CARD, 21, 0, 0),
	GATE(CLK_GOUT_FSYS_MMC_EMBD, "gout_fsys_mmc_embd", "mout_fsys_mmc_embd",
	     CLK_CON_GAT_GATE_CLKCMU_FSYS_MMC_EMBD, 21, 0, 0),
	GATE(CLK_GOUT_FSYS_MMC_SDIO, "gout_fsys_mmc_sdio", "mout_fsys_mmc_sdio",
	     CLK_CON_GAT_GATE_CLKCMU_FSYS_MMC_SDIO, 21, 0, 0),
	GATE(CLK_GOUT_FSYS_USB30DRD, "gout_fsys_usb30drd", "mout_fsys_usb30drd",
	     CLK_CON_GAT_GATE_CLKCMU_FSYS_USB30DRD, 21, 0, 0),
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
	.nr_clk_ids		= CLKS_NR_TOP,
	.clk_regs		= top_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(top_clk_regs),
};

static void __init exynos7885_cmu_top_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &top_cmu_info);
}

/* Register CMU_TOP early, as it's a dependency for other early domains */
CLK_OF_DECLARE(exynos7885_cmu_top, "samsung,exynos7885-cmu-top",
	       exynos7885_cmu_top_init);

/* ---- CMU_PERI ------------------------------------------------------------ */

/* Register Offset definitions for CMU_PERI (0x10010000) */
#define PLL_CON0_MUX_CLKCMU_PERI_BUS_USER	0x0100
#define PLL_CON0_MUX_CLKCMU_PERI_SPI0_USER	0x0120
#define PLL_CON0_MUX_CLKCMU_PERI_SPI1_USER	0x0140
#define PLL_CON0_MUX_CLKCMU_PERI_UART0_USER	0x0160
#define PLL_CON0_MUX_CLKCMU_PERI_UART1_USER	0x0180
#define PLL_CON0_MUX_CLKCMU_PERI_UART2_USER	0x01a0
#define PLL_CON0_MUX_CLKCMU_PERI_USI0_USER	0x01c0
#define PLL_CON0_MUX_CLKCMU_PERI_USI1_USER	0x01e0
#define PLL_CON0_MUX_CLKCMU_PERI_USI2_USER	0x0200
#define CLK_CON_GAT_GOUT_PERI_GPIO_TOP_PCLK	0x2024
#define CLK_CON_GAT_GOUT_PERI_HSI2C_0_PCLK	0x2028
#define CLK_CON_GAT_GOUT_PERI_HSI2C_1_PCLK	0x202c
#define CLK_CON_GAT_GOUT_PERI_HSI2C_2_PCLK	0x2030
#define CLK_CON_GAT_GOUT_PERI_HSI2C_3_PCLK	0x2034
#define CLK_CON_GAT_GOUT_PERI_I2C_0_PCLK	0x2038
#define CLK_CON_GAT_GOUT_PERI_I2C_1_PCLK	0x203c
#define CLK_CON_GAT_GOUT_PERI_I2C_2_PCLK	0x2040
#define CLK_CON_GAT_GOUT_PERI_I2C_3_PCLK	0x2044
#define CLK_CON_GAT_GOUT_PERI_I2C_4_PCLK	0x2048
#define CLK_CON_GAT_GOUT_PERI_I2C_5_PCLK	0x204c
#define CLK_CON_GAT_GOUT_PERI_I2C_6_PCLK	0x2050
#define CLK_CON_GAT_GOUT_PERI_I2C_7_PCLK	0x2054
#define CLK_CON_GAT_GOUT_PERI_PWM_MOTOR_PCLK	0x2058
#define CLK_CON_GAT_GOUT_PERI_SPI_0_PCLK	0x205c
#define CLK_CON_GAT_GOUT_PERI_SPI_0_EXT_CLK	0x2060
#define CLK_CON_GAT_GOUT_PERI_SPI_1_PCLK	0x2064
#define CLK_CON_GAT_GOUT_PERI_SPI_1_EXT_CLK	0x2068
#define CLK_CON_GAT_GOUT_PERI_UART_0_EXT_UCLK	0x206c
#define CLK_CON_GAT_GOUT_PERI_UART_0_PCLK	0x2070
#define CLK_CON_GAT_GOUT_PERI_UART_1_EXT_UCLK	0x2074
#define CLK_CON_GAT_GOUT_PERI_UART_1_PCLK	0x2078
#define CLK_CON_GAT_GOUT_PERI_UART_2_EXT_UCLK	0x207c
#define CLK_CON_GAT_GOUT_PERI_UART_2_PCLK	0x2080
#define CLK_CON_GAT_GOUT_PERI_USI0_PCLK		0x2084
#define CLK_CON_GAT_GOUT_PERI_USI0_SCLK		0x2088
#define CLK_CON_GAT_GOUT_PERI_USI1_PCLK		0x208c
#define CLK_CON_GAT_GOUT_PERI_USI1_SCLK		0x2090
#define CLK_CON_GAT_GOUT_PERI_USI2_PCLK		0x2094
#define CLK_CON_GAT_GOUT_PERI_USI2_SCLK		0x2098
#define CLK_CON_GAT_GOUT_PERI_MCT_PCLK		0x20a0
#define CLK_CON_GAT_GOUT_PERI_SYSREG_PERI_PCLK	0x20b0
#define CLK_CON_GAT_GOUT_PERI_WDT_CLUSTER0_PCLK	0x20b4
#define CLK_CON_GAT_GOUT_PERI_WDT_CLUSTER1_PCLK	0x20b8

static const unsigned long peri_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_PERI_BUS_USER,
	PLL_CON0_MUX_CLKCMU_PERI_SPI0_USER,
	PLL_CON0_MUX_CLKCMU_PERI_SPI1_USER,
	PLL_CON0_MUX_CLKCMU_PERI_UART0_USER,
	PLL_CON0_MUX_CLKCMU_PERI_UART1_USER,
	PLL_CON0_MUX_CLKCMU_PERI_UART2_USER,
	PLL_CON0_MUX_CLKCMU_PERI_USI0_USER,
	PLL_CON0_MUX_CLKCMU_PERI_USI1_USER,
	PLL_CON0_MUX_CLKCMU_PERI_USI2_USER,
	CLK_CON_GAT_GOUT_PERI_GPIO_TOP_PCLK,
	CLK_CON_GAT_GOUT_PERI_HSI2C_0_PCLK,
	CLK_CON_GAT_GOUT_PERI_HSI2C_1_PCLK,
	CLK_CON_GAT_GOUT_PERI_HSI2C_2_PCLK,
	CLK_CON_GAT_GOUT_PERI_HSI2C_3_PCLK,
	CLK_CON_GAT_GOUT_PERI_I2C_0_PCLK,
	CLK_CON_GAT_GOUT_PERI_I2C_1_PCLK,
	CLK_CON_GAT_GOUT_PERI_I2C_2_PCLK,
	CLK_CON_GAT_GOUT_PERI_I2C_3_PCLK,
	CLK_CON_GAT_GOUT_PERI_I2C_4_PCLK,
	CLK_CON_GAT_GOUT_PERI_I2C_5_PCLK,
	CLK_CON_GAT_GOUT_PERI_I2C_6_PCLK,
	CLK_CON_GAT_GOUT_PERI_I2C_7_PCLK,
	CLK_CON_GAT_GOUT_PERI_PWM_MOTOR_PCLK,
	CLK_CON_GAT_GOUT_PERI_SPI_0_PCLK,
	CLK_CON_GAT_GOUT_PERI_SPI_0_EXT_CLK,
	CLK_CON_GAT_GOUT_PERI_SPI_1_PCLK,
	CLK_CON_GAT_GOUT_PERI_SPI_1_EXT_CLK,
	CLK_CON_GAT_GOUT_PERI_UART_0_EXT_UCLK,
	CLK_CON_GAT_GOUT_PERI_UART_0_PCLK,
	CLK_CON_GAT_GOUT_PERI_UART_1_EXT_UCLK,
	CLK_CON_GAT_GOUT_PERI_UART_1_PCLK,
	CLK_CON_GAT_GOUT_PERI_UART_2_EXT_UCLK,
	CLK_CON_GAT_GOUT_PERI_UART_2_PCLK,
	CLK_CON_GAT_GOUT_PERI_USI0_PCLK,
	CLK_CON_GAT_GOUT_PERI_USI0_SCLK,
	CLK_CON_GAT_GOUT_PERI_USI1_PCLK,
	CLK_CON_GAT_GOUT_PERI_USI1_SCLK,
	CLK_CON_GAT_GOUT_PERI_USI2_PCLK,
	CLK_CON_GAT_GOUT_PERI_USI2_SCLK,
	CLK_CON_GAT_GOUT_PERI_MCT_PCLK,
	CLK_CON_GAT_GOUT_PERI_SYSREG_PERI_PCLK,
	CLK_CON_GAT_GOUT_PERI_WDT_CLUSTER0_PCLK,
	CLK_CON_GAT_GOUT_PERI_WDT_CLUSTER1_PCLK,
};

/* List of parent clocks for Muxes in CMU_PERI */
PNAME(mout_peri_bus_user_p)	= { "oscclk", "dout_peri_bus" };
PNAME(mout_peri_spi0_user_p)	= { "oscclk", "dout_peri_spi0" };
PNAME(mout_peri_spi1_user_p)	= { "oscclk", "dout_peri_spi1" };
PNAME(mout_peri_uart0_user_p)	= { "oscclk", "dout_peri_uart0" };
PNAME(mout_peri_uart1_user_p)	= { "oscclk", "dout_peri_uart1" };
PNAME(mout_peri_uart2_user_p)	= { "oscclk", "dout_peri_uart2" };
PNAME(mout_peri_usi0_user_p)	= { "oscclk", "dout_peri_usi0" };
PNAME(mout_peri_usi1_user_p)	= { "oscclk", "dout_peri_usi1" };
PNAME(mout_peri_usi2_user_p)	= { "oscclk", "dout_peri_usi2" };

static const struct samsung_mux_clock peri_mux_clks[] __initconst = {
	MUX(CLK_MOUT_PERI_BUS_USER, "mout_peri_bus_user", mout_peri_bus_user_p,
	    PLL_CON0_MUX_CLKCMU_PERI_BUS_USER, 4, 1),
	MUX(CLK_MOUT_PERI_SPI0_USER, "mout_peri_spi0_user", mout_peri_spi0_user_p,
	    PLL_CON0_MUX_CLKCMU_PERI_SPI0_USER, 4, 1),
	MUX(CLK_MOUT_PERI_SPI1_USER, "mout_peri_spi1_user", mout_peri_spi1_user_p,
	    PLL_CON0_MUX_CLKCMU_PERI_SPI1_USER, 4, 1),
	MUX(CLK_MOUT_PERI_UART0_USER, "mout_peri_uart0_user",
	    mout_peri_uart0_user_p, PLL_CON0_MUX_CLKCMU_PERI_UART0_USER, 4, 1),
	MUX(CLK_MOUT_PERI_UART1_USER, "mout_peri_uart1_user",
	    mout_peri_uart1_user_p, PLL_CON0_MUX_CLKCMU_PERI_UART1_USER, 4, 1),
	MUX(CLK_MOUT_PERI_UART2_USER, "mout_peri_uart2_user",
	    mout_peri_uart2_user_p, PLL_CON0_MUX_CLKCMU_PERI_UART2_USER, 4, 1),
	MUX(CLK_MOUT_PERI_USI0_USER, "mout_peri_usi0_user",
	    mout_peri_usi0_user_p, PLL_CON0_MUX_CLKCMU_PERI_USI0_USER, 4, 1),
	MUX(CLK_MOUT_PERI_USI1_USER, "mout_peri_usi1_user",
	    mout_peri_usi1_user_p, PLL_CON0_MUX_CLKCMU_PERI_USI1_USER, 4, 1),
	MUX(CLK_MOUT_PERI_USI2_USER, "mout_peri_usi2_user",
	    mout_peri_usi2_user_p, PLL_CON0_MUX_CLKCMU_PERI_USI2_USER, 4, 1),
};

static const struct samsung_gate_clock peri_gate_clks[] __initconst = {
	/* TODO: Should be enabled in GPIO driver (or made CLK_IS_CRITICAL) */
	GATE(CLK_GOUT_GPIO_TOP_PCLK, "gout_gpio_top_pclk",
	     "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_GPIO_TOP_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_HSI2C0_PCLK, "gout_hsi2c0_pclk", "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_HSI2C_0_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_HSI2C1_PCLK, "gout_hsi2c1_pclk", "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_HSI2C_1_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_HSI2C2_PCLK, "gout_hsi2c2_pclk", "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_HSI2C_2_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_HSI2C3_PCLK, "gout_hsi2c3_pclk", "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_HSI2C_3_PCLK, 21, 0, 0),
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
	GATE(CLK_GOUT_I2C7_PCLK, "gout_i2c7_pclk", "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_I2C_7_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_PWM_MOTOR_PCLK, "gout_pwm_motor_pclk",
	     "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_PWM_MOTOR_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_SPI0_PCLK, "gout_spi0_pclk", "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_SPI_0_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_SPI0_EXT_CLK, "gout_spi0_ipclk", "mout_peri_spi0_user",
	     CLK_CON_GAT_GOUT_PERI_SPI_0_EXT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_SPI1_PCLK, "gout_spi1_pclk", "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_SPI_1_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_SPI1_EXT_CLK, "gout_spi1_ipclk", "mout_peri_spi1_user",
	     CLK_CON_GAT_GOUT_PERI_SPI_1_EXT_CLK, 21, 0, 0),
	GATE(CLK_GOUT_UART0_EXT_UCLK, "gout_uart0_ext_uclk", "mout_peri_uart0_user",
	     CLK_CON_GAT_GOUT_PERI_UART_0_EXT_UCLK, 21, 0, 0),
	GATE(CLK_GOUT_UART0_PCLK, "gout_uart0_pclk", "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_UART_0_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_UART1_EXT_UCLK, "gout_uart1_ext_uclk", "mout_peri_uart1_user",
	     CLK_CON_GAT_GOUT_PERI_UART_1_EXT_UCLK, 21, 0, 0),
	GATE(CLK_GOUT_UART1_PCLK, "gout_uart1_pclk", "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_UART_1_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_UART2_EXT_UCLK, "gout_uart2_ext_uclk", "mout_peri_uart2_user",
	     CLK_CON_GAT_GOUT_PERI_UART_2_EXT_UCLK, 21, 0, 0),
	GATE(CLK_GOUT_UART2_PCLK, "gout_uart2_pclk", "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_UART_2_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_USI0_PCLK, "gout_usi0_pclk", "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_USI0_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_USI0_SCLK, "gout_usi0_sclk", "mout_peri_usi0_user",
	     CLK_CON_GAT_GOUT_PERI_USI0_SCLK, 21, 0, 0),
	GATE(CLK_GOUT_USI1_PCLK, "gout_usi1_pclk", "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_USI1_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_USI1_SCLK, "gout_usi1_sclk", "mout_peri_usi1_user",
	     CLK_CON_GAT_GOUT_PERI_USI1_SCLK, 21, 0, 0),
	GATE(CLK_GOUT_USI2_PCLK, "gout_usi2_pclk", "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_USI2_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_USI2_SCLK, "gout_usi2_sclk", "mout_peri_usi2_user",
	     CLK_CON_GAT_GOUT_PERI_USI2_SCLK, 21, 0, 0),
	GATE(CLK_GOUT_MCT_PCLK, "gout_mct_pclk", "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_MCT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_SYSREG_PERI_PCLK, "gout_sysreg_peri_pclk",
	     "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_SYSREG_PERI_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_WDT0_PCLK, "gout_wdt0_pclk", "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_WDT_CLUSTER0_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_WDT1_PCLK, "gout_wdt1_pclk", "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_WDT_CLUSTER1_PCLK, 21, 0, 0),
};

static const struct samsung_cmu_info peri_cmu_info __initconst = {
	.mux_clks		= peri_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(peri_mux_clks),
	.gate_clks		= peri_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(peri_gate_clks),
	.nr_clk_ids		= CLKS_NR_PERI,
	.clk_regs		= peri_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(peri_clk_regs),
	.clk_name		= "dout_peri_bus",
};

static void __init exynos7885_cmu_peri_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &peri_cmu_info);
}

/* Register CMU_PERI early, as it's needed for MCT timer */
CLK_OF_DECLARE(exynos7885_cmu_peri, "samsung,exynos7885-cmu-peri",
	       exynos7885_cmu_peri_init);

/* ---- CMU_CORE ------------------------------------------------------------ */

/* Register Offset definitions for CMU_CORE (0x12000000) */
#define PLL_CON0_MUX_CLKCMU_CORE_BUS_USER		0x0100
#define PLL_CON0_MUX_CLKCMU_CORE_CCI_USER		0x0120
#define PLL_CON0_MUX_CLKCMU_CORE_G3D_USER		0x0140
#define CLK_CON_MUX_MUX_CLK_CORE_GIC			0x1000
#define CLK_CON_DIV_DIV_CLK_CORE_BUSP			0x1800
#define CLK_CON_GAT_GOUT_CORE_CCI_550_ACLK		0x2054
#define CLK_CON_GAT_GOUT_CORE_GIC400_CLK		0x2058
#define CLK_CON_GAT_GOUT_CORE_TREX_D_CORE_ACLK		0x215c
#define CLK_CON_GAT_GOUT_CORE_TREX_D_CORE_GCLK		0x2160
#define CLK_CON_GAT_GOUT_CORE_TREX_D_CORE_PCLK		0x2164
#define CLK_CON_GAT_GOUT_CORE_TREX_P_CORE_ACLK_P_CORE	0x2168
#define CLK_CON_GAT_GOUT_CORE_TREX_P_CORE_CCLK_P_CORE	0x216c
#define CLK_CON_GAT_GOUT_CORE_TREX_P_CORE_PCLK		0x2170
#define CLK_CON_GAT_GOUT_CORE_TREX_P_CORE_PCLK_P_CORE	0x2174

static const unsigned long core_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_CORE_BUS_USER,
	PLL_CON0_MUX_CLKCMU_CORE_CCI_USER,
	PLL_CON0_MUX_CLKCMU_CORE_G3D_USER,
	CLK_CON_MUX_MUX_CLK_CORE_GIC,
	CLK_CON_DIV_DIV_CLK_CORE_BUSP,
	CLK_CON_GAT_GOUT_CORE_CCI_550_ACLK,
	CLK_CON_GAT_GOUT_CORE_GIC400_CLK,
	CLK_CON_GAT_GOUT_CORE_TREX_D_CORE_ACLK,
	CLK_CON_GAT_GOUT_CORE_TREX_D_CORE_GCLK,
	CLK_CON_GAT_GOUT_CORE_TREX_D_CORE_PCLK,
	CLK_CON_GAT_GOUT_CORE_TREX_P_CORE_ACLK_P_CORE,
	CLK_CON_GAT_GOUT_CORE_TREX_P_CORE_CCLK_P_CORE,
	CLK_CON_GAT_GOUT_CORE_TREX_P_CORE_PCLK,
	CLK_CON_GAT_GOUT_CORE_TREX_P_CORE_PCLK_P_CORE,
};

/* List of parent clocks for Muxes in CMU_CORE */
PNAME(mout_core_bus_user_p)		= { "oscclk", "dout_core_bus" };
PNAME(mout_core_cci_user_p)		= { "oscclk", "dout_core_cci" };
PNAME(mout_core_g3d_user_p)		= { "oscclk", "dout_core_g3d" };
PNAME(mout_core_gic_p)			= { "dout_core_busp", "oscclk" };

static const struct samsung_mux_clock core_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CORE_BUS_USER, "mout_core_bus_user", mout_core_bus_user_p,
	    PLL_CON0_MUX_CLKCMU_CORE_BUS_USER, 4, 1),
	MUX(CLK_MOUT_CORE_CCI_USER, "mout_core_cci_user", mout_core_cci_user_p,
	    PLL_CON0_MUX_CLKCMU_CORE_CCI_USER, 4, 1),
	MUX(CLK_MOUT_CORE_G3D_USER, "mout_core_g3d_user", mout_core_g3d_user_p,
	    PLL_CON0_MUX_CLKCMU_CORE_G3D_USER, 4, 1),
	MUX(CLK_MOUT_CORE_GIC, "mout_core_gic", mout_core_gic_p,
	    CLK_CON_MUX_MUX_CLK_CORE_GIC, 0, 1),
};

static const struct samsung_div_clock core_div_clks[] __initconst = {
	DIV(CLK_DOUT_CORE_BUSP, "dout_core_busp", "mout_core_bus_user",
	    CLK_CON_DIV_DIV_CLK_CORE_BUSP, 0, 2),
};

static const struct samsung_gate_clock core_gate_clks[] __initconst = {
	/* CCI (interconnect) clock must be always running */
	GATE(CLK_GOUT_CCI_ACLK, "gout_cci_aclk", "mout_core_cci_user",
	     CLK_CON_GAT_GOUT_CORE_CCI_550_ACLK, 21, CLK_IS_CRITICAL, 0),
	/* GIC (interrupt controller) clock must be always running */
	GATE(CLK_GOUT_GIC400_CLK, "gout_gic400_clk", "mout_core_gic",
	     CLK_CON_GAT_GOUT_CORE_GIC400_CLK, 21, CLK_IS_CRITICAL, 0),
	/*
	 * TREX D and P Core (seems to be related to "bus traffic shaper")
	 * clocks must always be running
	 */
	GATE(CLK_GOUT_TREX_D_CORE_ACLK, "gout_trex_d_core_aclk", "mout_core_bus_user",
	     CLK_CON_GAT_GOUT_CORE_TREX_D_CORE_ACLK, 21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_TREX_D_CORE_GCLK, "gout_trex_d_core_gclk", "mout_core_g3d_user",
	     CLK_CON_GAT_GOUT_CORE_TREX_D_CORE_GCLK, 21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_TREX_D_CORE_PCLK, "gout_trex_d_core_pclk", "dout_core_busp",
	     CLK_CON_GAT_GOUT_CORE_TREX_D_CORE_PCLK, 21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_TREX_P_CORE_ACLK_P_CORE, "gout_trex_p_core_aclk_p_core",
	     "mout_core_bus_user", CLK_CON_GAT_GOUT_CORE_TREX_P_CORE_ACLK_P_CORE, 21,
	     CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_TREX_P_CORE_CCLK_P_CORE, "gout_trex_p_core_cclk_p_core",
	     "mout_core_cci_user", CLK_CON_GAT_GOUT_CORE_TREX_P_CORE_CCLK_P_CORE, 21,
	     CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_TREX_P_CORE_PCLK, "gout_trex_p_core_pclk", "dout_core_busp",
	     CLK_CON_GAT_GOUT_CORE_TREX_P_CORE_PCLK, 21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_TREX_P_CORE_PCLK_P_CORE, "gout_trex_p_core_pclk_p_core",
	     "dout_core_busp", CLK_CON_GAT_GOUT_CORE_TREX_P_CORE_PCLK_P_CORE, 21,
	     CLK_IS_CRITICAL, 0),
};

static const struct samsung_cmu_info core_cmu_info __initconst = {
	.mux_clks		= core_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(core_mux_clks),
	.div_clks		= core_div_clks,
	.nr_div_clks		= ARRAY_SIZE(core_div_clks),
	.gate_clks		= core_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(core_gate_clks),
	.nr_clk_ids		= CLKS_NR_CORE,
	.clk_regs		= core_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(core_clk_regs),
	.clk_name		= "dout_core_bus",
};

/* ---- CMU_FSYS ------------------------------------------------------------ */

/* Register Offset definitions for CMU_FSYS (0x13400000) */
#define PLL_CON0_MUX_CLKCMU_FSYS_BUS_USER	0x0100
#define PLL_CON0_MUX_CLKCMU_FSYS_MMC_CARD_USER	0x0120
#define PLL_CON0_MUX_CLKCMU_FSYS_MMC_EMBD_USER	0x0140
#define PLL_CON0_MUX_CLKCMU_FSYS_MMC_SDIO_USER	0x0160
#define PLL_CON0_MUX_CLKCMU_FSYS_USB30DRD_USER	0x0180
#define CLK_CON_GAT_GOUT_FSYS_MMC_CARD_I_ACLK	0x2030
#define CLK_CON_GAT_GOUT_FSYS_MMC_CARD_SDCLKIN	0x2034
#define CLK_CON_GAT_GOUT_FSYS_MMC_EMBD_I_ACLK	0x2038
#define CLK_CON_GAT_GOUT_FSYS_MMC_EMBD_SDCLKIN	0x203c
#define CLK_CON_GAT_GOUT_FSYS_MMC_SDIO_I_ACLK	0x2040
#define CLK_CON_GAT_GOUT_FSYS_MMC_SDIO_SDCLKIN	0x2044

static const unsigned long fsys_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_FSYS_BUS_USER,
	PLL_CON0_MUX_CLKCMU_FSYS_MMC_CARD_USER,
	PLL_CON0_MUX_CLKCMU_FSYS_MMC_EMBD_USER,
	PLL_CON0_MUX_CLKCMU_FSYS_MMC_SDIO_USER,
	PLL_CON0_MUX_CLKCMU_FSYS_USB30DRD_USER,
	CLK_CON_GAT_GOUT_FSYS_MMC_CARD_I_ACLK,
	CLK_CON_GAT_GOUT_FSYS_MMC_CARD_SDCLKIN,
	CLK_CON_GAT_GOUT_FSYS_MMC_EMBD_I_ACLK,
	CLK_CON_GAT_GOUT_FSYS_MMC_EMBD_SDCLKIN,
	CLK_CON_GAT_GOUT_FSYS_MMC_SDIO_I_ACLK,
	CLK_CON_GAT_GOUT_FSYS_MMC_SDIO_SDCLKIN,
};

/* List of parent clocks for Muxes in CMU_FSYS */
PNAME(mout_fsys_bus_user_p)		= { "oscclk", "dout_fsys_bus" };
PNAME(mout_fsys_mmc_card_user_p)	= { "oscclk", "dout_fsys_mmc_card" };
PNAME(mout_fsys_mmc_embd_user_p)	= { "oscclk", "dout_fsys_mmc_embd" };
PNAME(mout_fsys_mmc_sdio_user_p)	= { "oscclk", "dout_fsys_mmc_sdio" };
PNAME(mout_fsys_usb30drd_user_p)	= { "oscclk", "dout_fsys_usb30drd" };

static const struct samsung_mux_clock fsys_mux_clks[] __initconst = {
	MUX(CLK_MOUT_FSYS_BUS_USER, "mout_fsys_bus_user", mout_fsys_bus_user_p,
	    PLL_CON0_MUX_CLKCMU_FSYS_BUS_USER, 4, 1),
	MUX_F(CLK_MOUT_FSYS_MMC_CARD_USER, "mout_fsys_mmc_card_user",
	      mout_fsys_mmc_card_user_p, PLL_CON0_MUX_CLKCMU_FSYS_MMC_CARD_USER,
	      4, 1, CLK_SET_RATE_PARENT, 0),
	MUX_F(CLK_MOUT_FSYS_MMC_EMBD_USER, "mout_fsys_mmc_embd_user",
	      mout_fsys_mmc_embd_user_p, PLL_CON0_MUX_CLKCMU_FSYS_MMC_EMBD_USER,
	      4, 1, CLK_SET_RATE_PARENT, 0),
	MUX_F(CLK_MOUT_FSYS_MMC_SDIO_USER, "mout_fsys_mmc_sdio_user",
	      mout_fsys_mmc_sdio_user_p, PLL_CON0_MUX_CLKCMU_FSYS_MMC_SDIO_USER,
	      4, 1, CLK_SET_RATE_PARENT, 0),
	MUX_F(CLK_MOUT_FSYS_USB30DRD_USER, "mout_fsys_usb30drd_user",
	      mout_fsys_usb30drd_user_p, PLL_CON0_MUX_CLKCMU_FSYS_USB30DRD_USER,
	      4, 1, CLK_SET_RATE_PARENT, 0),
};

static const struct samsung_gate_clock fsys_gate_clks[] __initconst = {
	GATE(CLK_GOUT_MMC_CARD_ACLK, "gout_mmc_card_aclk", "mout_fsys_bus_user",
	     CLK_CON_GAT_GOUT_FSYS_MMC_CARD_I_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_MMC_CARD_SDCLKIN, "gout_mmc_card_sdclkin",
	     "mout_fsys_mmc_card_user", CLK_CON_GAT_GOUT_FSYS_MMC_CARD_SDCLKIN,
	     21, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MMC_EMBD_ACLK, "gout_mmc_embd_aclk", "mout_fsys_bus_user",
	     CLK_CON_GAT_GOUT_FSYS_MMC_EMBD_I_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_MMC_EMBD_SDCLKIN, "gout_mmc_embd_sdclkin",
	     "mout_fsys_mmc_embd_user", CLK_CON_GAT_GOUT_FSYS_MMC_EMBD_SDCLKIN,
	     21, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MMC_SDIO_ACLK, "gout_mmc_sdio_aclk", "mout_fsys_bus_user",
	     CLK_CON_GAT_GOUT_FSYS_MMC_SDIO_I_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_MMC_SDIO_SDCLKIN, "gout_mmc_sdio_sdclkin",
	     "mout_fsys_mmc_sdio_user", CLK_CON_GAT_GOUT_FSYS_MMC_SDIO_SDCLKIN,
	     21, CLK_SET_RATE_PARENT, 0),
};

static const struct samsung_cmu_info fsys_cmu_info __initconst = {
	.mux_clks		= fsys_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(fsys_mux_clks),
	.gate_clks		= fsys_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(fsys_gate_clks),
	.nr_clk_ids		= CLKS_NR_FSYS,
	.clk_regs		= fsys_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(fsys_clk_regs),
	.clk_name		= "dout_fsys_bus",
};

/* ---- platform_driver ----------------------------------------------------- */

static int __init exynos7885_cmu_probe(struct platform_device *pdev)
{
	const struct samsung_cmu_info *info;
	struct device *dev = &pdev->dev;

	info = of_device_get_match_data(dev);
	exynos_arm64_register_cmu(dev, dev->of_node, info);

	return 0;
}

static const struct of_device_id exynos7885_cmu_of_match[] = {
	{
		.compatible = "samsung,exynos7885-cmu-core",
		.data = &core_cmu_info,
	}, {
		.compatible = "samsung,exynos7885-cmu-fsys",
		.data = &fsys_cmu_info,
	}, {
	},
};

static struct platform_driver exynos7885_cmu_driver __refdata = {
	.driver	= {
		.name = "exynos7885-cmu",
		.of_match_table = exynos7885_cmu_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = exynos7885_cmu_probe,
};

static int __init exynos7885_cmu_init(void)
{
	return platform_driver_register(&exynos7885_cmu_driver);
}
core_initcall(exynos7885_cmu_init);
