/*
 * Copyright 2012-2013 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/of_address.h>
#include <linux/clk.h>
#include <dt-bindings/clock/vf610-clock.h>

#include "clk.h"

#define CCM_CCR			(ccm_base + 0x00)
#define CCM_CSR			(ccm_base + 0x04)
#define CCM_CCSR		(ccm_base + 0x08)
#define CCM_CACRR		(ccm_base + 0x0c)
#define CCM_CSCMR1		(ccm_base + 0x10)
#define CCM_CSCDR1		(ccm_base + 0x14)
#define CCM_CSCDR2		(ccm_base + 0x18)
#define CCM_CSCDR3		(ccm_base + 0x1c)
#define CCM_CSCMR2		(ccm_base + 0x20)
#define CCM_CSCDR4		(ccm_base + 0x24)
#define CCM_CLPCR		(ccm_base + 0x2c)
#define CCM_CISR		(ccm_base + 0x30)
#define CCM_CIMR		(ccm_base + 0x34)
#define CCM_CGPR		(ccm_base + 0x3c)
#define CCM_CCGR0		(ccm_base + 0x40)
#define CCM_CCGR1		(ccm_base + 0x44)
#define CCM_CCGR2		(ccm_base + 0x48)
#define CCM_CCGR3		(ccm_base + 0x4c)
#define CCM_CCGR4		(ccm_base + 0x50)
#define CCM_CCGR5		(ccm_base + 0x54)
#define CCM_CCGR6		(ccm_base + 0x58)
#define CCM_CCGR7		(ccm_base + 0x5c)
#define CCM_CCGR8		(ccm_base + 0x60)
#define CCM_CCGR9		(ccm_base + 0x64)
#define CCM_CCGR10		(ccm_base + 0x68)
#define CCM_CCGR11		(ccm_base + 0x6c)
#define CCM_CMEOR0		(ccm_base + 0x70)
#define CCM_CMEOR1		(ccm_base + 0x74)
#define CCM_CMEOR2		(ccm_base + 0x78)
#define CCM_CMEOR3		(ccm_base + 0x7c)
#define CCM_CMEOR4		(ccm_base + 0x80)
#define CCM_CMEOR5		(ccm_base + 0x84)
#define CCM_CPPDSR		(ccm_base + 0x88)
#define CCM_CCOWR		(ccm_base + 0x8c)
#define CCM_CCPGR0		(ccm_base + 0x90)
#define CCM_CCPGR1		(ccm_base + 0x94)
#define CCM_CCPGR2		(ccm_base + 0x98)
#define CCM_CCPGR3		(ccm_base + 0x9c)

#define CCM_CCGRx_CGn(n)	((n) * 2)

#define PFD_PLL1_BASE		(anatop_base + 0x2b0)
#define PFD_PLL2_BASE		(anatop_base + 0x100)
#define PFD_PLL3_BASE		(anatop_base + 0xf0)

static void __iomem *anatop_base;
static void __iomem *ccm_base;

/* sources for multiplexer clocks, this is used multiple times */
static const char *fast_sels[]	= { "firc", "fxosc", };
static const char *slow_sels[]	= { "sirc_32k", "sxosc", };
static const char *pll1_sels[]	= { "pll1_main", "pll1_pfd1", "pll1_pfd2", "pll1_pfd3", "pll1_pfd4", };
static const char *pll2_sels[]	= { "pll2_main", "pll2_pfd1", "pll2_pfd2", "pll2_pfd3", "pll2_pfd4", };
static const char *sys_sels[]	= { "fast_clk_sel", "slow_clk_sel", "pll2_pfd_sel", "pll2_main", "pll1_pfd_sel", "pll3_main", };
static const char *ddr_sels[]	= { "pll2_pfd2", "sys_sel", };
static const char *rmii_sels[]	= { "enet_ext", "audio_ext", "enet_50m", "enet_25m", };
static const char *enet_ts_sels[]	= { "enet_ext", "fxosc", "audio_ext", "usb", "enet_ts", "enet_25m", "enet_50m", };
static const char *esai_sels[]	= { "audio_ext", "mlb", "spdif_rx", "pll4_main_div", };
static const char *sai_sels[]	= { "audio_ext", "mlb", "spdif_rx", "pll4_main_div", };
static const char *nfc_sels[]	= { "platform_bus", "pll1_pfd1", "pll3_pfd1", "pll3_pfd3", };
static const char *qspi_sels[]	= { "pll3_main", "pll3_pfd4", "pll2_pfd4", "pll1_pfd4", };
static const char *esdhc_sels[]	= { "pll3_main", "pll3_pfd3", "pll1_pfd3", "platform_bus", };
static const char *dcu_sels[]	= { "pll1_pfd2", "pll3_main", };
static const char *gpu_sels[]	= { "pll2_pfd2", "pll3_pfd2", };
static const char *vadc_sels[]	= { "pll6_main_div", "pll3_main_div", "pll3_main", };
/* FTM counter clock source, not module clock */
static const char *ftm_ext_sels[]	= {"sirc_128k", "sxosc", "fxosc_half", "audio_ext", };
static const char *ftm_fix_sels[]	= { "sxosc", "ipg_bus", };

static struct clk_div_table pll4_main_div_table[] = {
	{ .val = 0, .div = 1 },
	{ .val = 1, .div = 2 },
	{ .val = 2, .div = 6 },
	{ .val = 3, .div = 8 },
	{ .val = 4, .div = 10 },
	{ .val = 5, .div = 12 },
	{ .val = 6, .div = 14 },
	{ .val = 7, .div = 16 },
	{ }
};

static struct clk *clk[VF610_CLK_END];
static struct clk_onecell_data clk_data;

static void __init vf610_clocks_init(struct device_node *ccm_node)
{
	struct device_node *np;

	clk[VF610_CLK_DUMMY] = imx_clk_fixed("dummy", 0);
	clk[VF610_CLK_SIRC_128K] = imx_clk_fixed("sirc_128k", 128000);
	clk[VF610_CLK_SIRC_32K] = imx_clk_fixed("sirc_32k", 32000);
	clk[VF610_CLK_FIRC] = imx_clk_fixed("firc", 24000000);

	clk[VF610_CLK_SXOSC] = imx_obtain_fixed_clock("sxosc", 0);
	clk[VF610_CLK_FXOSC] = imx_obtain_fixed_clock("fxosc", 0);
	clk[VF610_CLK_AUDIO_EXT] = imx_obtain_fixed_clock("audio_ext", 0);
	clk[VF610_CLK_ENET_EXT] = imx_obtain_fixed_clock("enet_ext", 0);

	clk[VF610_CLK_FXOSC_HALF] = imx_clk_fixed_factor("fxosc_half", "fxosc", 1, 2);

	np = of_find_compatible_node(NULL, NULL, "fsl,vf610-anatop");
	anatop_base = of_iomap(np, 0);
	BUG_ON(!anatop_base);

	np = ccm_node;
	ccm_base = of_iomap(np, 0);
	BUG_ON(!ccm_base);

	clk[VF610_CLK_SLOW_CLK_SEL] = imx_clk_mux("slow_clk_sel", CCM_CCSR, 4, 1, slow_sels, ARRAY_SIZE(slow_sels));
	clk[VF610_CLK_FASK_CLK_SEL] = imx_clk_mux("fast_clk_sel", CCM_CCSR, 5, 1, fast_sels, ARRAY_SIZE(fast_sels));

	clk[VF610_CLK_PLL1_MAIN] = imx_clk_fixed_factor("pll1_main", "fast_clk_sel", 22, 1);
	clk[VF610_CLK_PLL1_PFD1] = imx_clk_pfd("pll1_pfd1", "pll1_main", PFD_PLL1_BASE, 0);
	clk[VF610_CLK_PLL1_PFD2] = imx_clk_pfd("pll1_pfd2", "pll1_main", PFD_PLL1_BASE, 1);
	clk[VF610_CLK_PLL1_PFD3] = imx_clk_pfd("pll1_pfd3", "pll1_main", PFD_PLL1_BASE, 2);
	clk[VF610_CLK_PLL1_PFD4] = imx_clk_pfd("pll1_pfd4", "pll1_main", PFD_PLL1_BASE, 3);

	clk[VF610_CLK_PLL2_MAIN] = imx_clk_fixed_factor("pll2_main", "fast_clk_sel", 22, 1);
	clk[VF610_CLK_PLL2_PFD1] = imx_clk_pfd("pll2_pfd1", "pll2_main", PFD_PLL2_BASE, 0);
	clk[VF610_CLK_PLL2_PFD2] = imx_clk_pfd("pll2_pfd2", "pll2_main", PFD_PLL2_BASE, 1);
	clk[VF610_CLK_PLL2_PFD3] = imx_clk_pfd("pll2_pfd3", "pll2_main", PFD_PLL2_BASE, 2);
	clk[VF610_CLK_PLL2_PFD4] = imx_clk_pfd("pll2_pfd4", "pll2_main", PFD_PLL2_BASE, 3);

	clk[VF610_CLK_PLL3_MAIN] = imx_clk_fixed_factor("pll3_main", "fast_clk_sel", 20, 1);
	clk[VF610_CLK_PLL3_PFD1] = imx_clk_pfd("pll3_pfd1", "pll3_main", PFD_PLL3_BASE, 0);
	clk[VF610_CLK_PLL3_PFD2] = imx_clk_pfd("pll3_pfd2", "pll3_main", PFD_PLL3_BASE, 1);
	clk[VF610_CLK_PLL3_PFD3] = imx_clk_pfd("pll3_pfd3", "pll3_main", PFD_PLL3_BASE, 2);
	clk[VF610_CLK_PLL3_PFD4] = imx_clk_pfd("pll3_pfd4", "pll3_main", PFD_PLL3_BASE, 3);

	clk[VF610_CLK_PLL4_MAIN] = imx_clk_fixed_factor("pll4_main", "fast_clk_sel", 25, 1);
	/* Enet pll: fixed 50Mhz */
	clk[VF610_CLK_PLL5_MAIN] = imx_clk_fixed_factor("pll5_main", "fast_clk_sel", 125, 6);
	/* pll6: default 960Mhz */
	clk[VF610_CLK_PLL6_MAIN] = imx_clk_fixed_factor("pll6_main", "fast_clk_sel", 40, 1);
	clk[VF610_CLK_PLL1_PFD_SEL] = imx_clk_mux("pll1_pfd_sel", CCM_CCSR, 16, 3, pll1_sels, 5);
	clk[VF610_CLK_PLL2_PFD_SEL] = imx_clk_mux("pll2_pfd_sel", CCM_CCSR, 19, 3, pll2_sels, 5);
	clk[VF610_CLK_SYS_SEL] = imx_clk_mux("sys_sel", CCM_CCSR, 0, 3, sys_sels, ARRAY_SIZE(sys_sels));
	clk[VF610_CLK_DDR_SEL] = imx_clk_mux("ddr_sel", CCM_CCSR, 6, 1, ddr_sels, ARRAY_SIZE(ddr_sels));
	clk[VF610_CLK_SYS_BUS] = imx_clk_divider("sys_bus", "sys_sel", CCM_CACRR, 0, 3);
	clk[VF610_CLK_PLATFORM_BUS] = imx_clk_divider("platform_bus", "sys_bus", CCM_CACRR, 3, 3);
	clk[VF610_CLK_IPG_BUS] = imx_clk_divider("ipg_bus", "platform_bus", CCM_CACRR, 11, 2);

	clk[VF610_CLK_PLL3_MAIN_DIV] = imx_clk_divider("pll3_main_div", "pll3_main", CCM_CACRR, 20, 1);
	clk[VF610_CLK_PLL4_MAIN_DIV] = clk_register_divider_table(NULL, "pll4_main_div", "pll4_main", 0, CCM_CACRR, 6, 3, 0, pll4_main_div_table, &imx_ccm_lock);
	clk[VF610_CLK_PLL6_MAIN_DIV] = imx_clk_divider("pll6_main_div", "pll6_main", CCM_CACRR, 21, 1);

	clk[VF610_CLK_USBC0] = imx_clk_gate2("usbc0", "pll3_main", CCM_CCGR1, CCM_CCGRx_CGn(4));
	clk[VF610_CLK_USBC1] = imx_clk_gate2("usbc1", "pll3_main", CCM_CCGR7, CCM_CCGRx_CGn(4));

	clk[VF610_CLK_QSPI0_SEL] = imx_clk_mux("qspi0_sel", CCM_CSCMR1, 22, 2, qspi_sels, 4);
	clk[VF610_CLK_QSPI0_EN] = imx_clk_gate("qspi0_en", "qspi0_sel", CCM_CSCDR3, 4);
	clk[VF610_CLK_QSPI0_X4_DIV] = imx_clk_divider("qspi0_x4", "qspi0_en", CCM_CSCDR3, 0, 2);
	clk[VF610_CLK_QSPI0_X2_DIV] = imx_clk_divider("qspi0_x2", "qspi0_x4", CCM_CSCDR3, 2, 1);
	clk[VF610_CLK_QSPI0_X1_DIV] = imx_clk_divider("qspi0_x1", "qspi0_x2", CCM_CSCDR3, 3, 1);
	clk[VF610_CLK_QSPI0] = imx_clk_gate2("qspi0", "qspi0_x1", CCM_CCGR2, CCM_CCGRx_CGn(4));

	clk[VF610_CLK_QSPI1_SEL] = imx_clk_mux("qspi1_sel", CCM_CSCMR1, 24, 2, qspi_sels, 4);
	clk[VF610_CLK_QSPI1_EN] = imx_clk_gate("qspi1_en", "qspi1_sel", CCM_CSCDR3, 12);
	clk[VF610_CLK_QSPI1_X4_DIV] = imx_clk_divider("qspi1_x4", "qspi1_en", CCM_CSCDR3, 8, 2);
	clk[VF610_CLK_QSPI1_X2_DIV] = imx_clk_divider("qspi1_x2", "qspi1_x4", CCM_CSCDR3, 10, 1);
	clk[VF610_CLK_QSPI1_X1_DIV] = imx_clk_divider("qspi1_x1", "qspi1_x2", CCM_CSCDR3, 11, 1);
	clk[VF610_CLK_QSPI1] = imx_clk_gate2("qspi1", "qspi1_x1", CCM_CCGR8, CCM_CCGRx_CGn(4));

	clk[VF610_CLK_ENET_50M] = imx_clk_fixed_factor("enet_50m", "pll5_main", 1, 10);
	clk[VF610_CLK_ENET_25M] = imx_clk_fixed_factor("enet_25m", "pll5_main", 1, 20);
	clk[VF610_CLK_ENET_SEL] = imx_clk_mux("enet_sel", CCM_CSCMR2, 4, 2, rmii_sels, 4);
	clk[VF610_CLK_ENET_TS_SEL] = imx_clk_mux("enet_ts_sel", CCM_CSCMR2, 0, 3, enet_ts_sels, 7);
	clk[VF610_CLK_ENET] = imx_clk_gate("enet", "enet_sel", CCM_CSCDR1, 24);
	clk[VF610_CLK_ENET_TS] = imx_clk_gate("enet_ts", "enet_ts_sel", CCM_CSCDR1, 23);
	clk[VF610_CLK_ENET0] = imx_clk_gate2("enet0", "ipg_bus", CCM_CCGR9, CCM_CCGRx_CGn(0));
	clk[VF610_CLK_ENET1] = imx_clk_gate2("enet1", "ipg_bus", CCM_CCGR9, CCM_CCGRx_CGn(1));

	clk[VF610_CLK_PIT] = imx_clk_gate2("pit", "ipg_bus", CCM_CCGR1, CCM_CCGRx_CGn(7));

	clk[VF610_CLK_UART0] = imx_clk_gate2("uart0", "ipg_bus", CCM_CCGR0, CCM_CCGRx_CGn(7));
	clk[VF610_CLK_UART1] = imx_clk_gate2("uart1", "ipg_bus", CCM_CCGR0, CCM_CCGRx_CGn(8));
	clk[VF610_CLK_UART2] = imx_clk_gate2("uart2", "ipg_bus", CCM_CCGR0, CCM_CCGRx_CGn(9));
	clk[VF610_CLK_UART3] = imx_clk_gate2("uart3", "ipg_bus", CCM_CCGR0, CCM_CCGRx_CGn(10));

	clk[VF610_CLK_I2C0] = imx_clk_gate2("i2c0", "ipg_bus", CCM_CCGR4, CCM_CCGRx_CGn(6));
	clk[VF610_CLK_I2C1] = imx_clk_gate2("i2c1", "ipg_bus", CCM_CCGR4, CCM_CCGRx_CGn(7));

	clk[VF610_CLK_DSPI0] = imx_clk_gate2("dspi0", "ipg_bus", CCM_CCGR0, CCM_CCGRx_CGn(12));
	clk[VF610_CLK_DSPI1] = imx_clk_gate2("dspi1", "ipg_bus", CCM_CCGR0, CCM_CCGRx_CGn(13));
	clk[VF610_CLK_DSPI2] = imx_clk_gate2("dspi2", "ipg_bus", CCM_CCGR6, CCM_CCGRx_CGn(12));
	clk[VF610_CLK_DSPI3] = imx_clk_gate2("dspi3", "ipg_bus", CCM_CCGR6, CCM_CCGRx_CGn(13));

	clk[VF610_CLK_WDT] = imx_clk_gate2("wdt", "ipg_bus", CCM_CCGR1, CCM_CCGRx_CGn(14));

	clk[VF610_CLK_ESDHC0_SEL] = imx_clk_mux("esdhc0_sel", CCM_CSCMR1, 16, 2, esdhc_sels, 4);
	clk[VF610_CLK_ESDHC0_EN] = imx_clk_gate("esdhc0_en", "esdhc0_sel", CCM_CSCDR2, 28);
	clk[VF610_CLK_ESDHC0_DIV] = imx_clk_divider("esdhc0_div", "esdhc0_en", CCM_CSCDR2, 16, 4);
	clk[VF610_CLK_ESDHC0] = imx_clk_gate2("eshc0", "esdhc0_div", CCM_CCGR7, CCM_CCGRx_CGn(1));

	clk[VF610_CLK_ESDHC1_SEL] = imx_clk_mux("esdhc1_sel", CCM_CSCMR1, 18, 2, esdhc_sels, 4);
	clk[VF610_CLK_ESDHC1_EN] = imx_clk_gate("esdhc1_en", "esdhc1_sel", CCM_CSCDR2, 29);
	clk[VF610_CLK_ESDHC1_DIV] = imx_clk_divider("esdhc1_div", "esdhc1_en", CCM_CSCDR2, 20, 4);
	clk[VF610_CLK_ESDHC1] = imx_clk_gate2("eshc1", "esdhc1_div", CCM_CCGR7, CCM_CCGRx_CGn(2));

	/*
	 * ftm_ext_clk and ftm_fix_clk are FTM timer counter's
	 * selectable clock sources, both use a common enable bit
	 * in CCM_CSCDR1, selecting "dummy" clock as parent of
	 * "ftm0_ext_fix" make it serve only for enable/disable.
	 */
	clk[VF610_CLK_FTM0_EXT_SEL] = imx_clk_mux("ftm0_ext_sel", CCM_CSCMR2, 6, 2, ftm_ext_sels, 4);
	clk[VF610_CLK_FTM0_FIX_SEL] = imx_clk_mux("ftm0_fix_sel", CCM_CSCMR2, 14, 1, ftm_fix_sels, 2);
	clk[VF610_CLK_FTM0_EXT_FIX_EN] = imx_clk_gate("ftm0_ext_fix_en", "dummy", CCM_CSCDR1, 25);
	clk[VF610_CLK_FTM1_EXT_SEL] = imx_clk_mux("ftm1_ext_sel", CCM_CSCMR2, 8, 2, ftm_ext_sels, 4);
	clk[VF610_CLK_FTM1_FIX_SEL] = imx_clk_mux("ftm1_fix_sel", CCM_CSCMR2, 15, 1, ftm_fix_sels, 2);
	clk[VF610_CLK_FTM1_EXT_FIX_EN] = imx_clk_gate("ftm1_ext_fix_en", "dummy", CCM_CSCDR1, 26);
	clk[VF610_CLK_FTM2_EXT_SEL] = imx_clk_mux("ftm2_ext_sel", CCM_CSCMR2, 10, 2, ftm_ext_sels, 4);
	clk[VF610_CLK_FTM2_FIX_SEL] = imx_clk_mux("ftm2_fix_sel", CCM_CSCMR2, 16, 1, ftm_fix_sels, 2);
	clk[VF610_CLK_FTM2_EXT_FIX_EN] = imx_clk_gate("ftm2_ext_fix_en", "dummy", CCM_CSCDR1, 27);
	clk[VF610_CLK_FTM3_EXT_SEL] = imx_clk_mux("ftm3_ext_sel", CCM_CSCMR2, 12, 2, ftm_ext_sels, 4);
	clk[VF610_CLK_FTM3_FIX_SEL] = imx_clk_mux("ftm3_fix_sel", CCM_CSCMR2, 17, 1, ftm_fix_sels, 2);
	clk[VF610_CLK_FTM3_EXT_FIX_EN] = imx_clk_gate("ftm3_ext_fix_en", "dummy", CCM_CSCDR1, 28);

	/* ftm(n)_clk are FTM module operation clock */
	clk[VF610_CLK_FTM0] = imx_clk_gate2("ftm0", "ipg_bus", CCM_CCGR1, CCM_CCGRx_CGn(8));
	clk[VF610_CLK_FTM1] = imx_clk_gate2("ftm1", "ipg_bus", CCM_CCGR1, CCM_CCGRx_CGn(9));
	clk[VF610_CLK_FTM2] = imx_clk_gate2("ftm2", "ipg_bus", CCM_CCGR7, CCM_CCGRx_CGn(8));
	clk[VF610_CLK_FTM3] = imx_clk_gate2("ftm3", "ipg_bus", CCM_CCGR7, CCM_CCGRx_CGn(9));

	clk[VF610_CLK_DCU0_SEL] = imx_clk_mux("dcu0_sel", CCM_CSCMR1, 28, 1, dcu_sels, 2);
	clk[VF610_CLK_DCU0_EN] = imx_clk_gate("dcu0_en", "dcu0_sel", CCM_CSCDR3, 19);
	clk[VF610_CLK_DCU0_DIV] = imx_clk_divider("dcu0_div", "dcu0_en", CCM_CSCDR3, 16, 3);
	clk[VF610_CLK_DCU0] = imx_clk_gate2("dcu0", "dcu0_div", CCM_CCGR3, CCM_CCGRx_CGn(8));
	clk[VF610_CLK_DCU1_SEL] = imx_clk_mux("dcu1_sel", CCM_CSCMR1, 29, 1, dcu_sels, 2);
	clk[VF610_CLK_DCU1_EN] = imx_clk_gate("dcu1_en", "dcu1_sel", CCM_CSCDR3, 23);
	clk[VF610_CLK_DCU1_DIV] = imx_clk_divider("dcu1_div", "dcu1_en", CCM_CSCDR3, 20, 3);
	clk[VF610_CLK_DCU1] = imx_clk_gate2("dcu1", "dcu1_div", CCM_CCGR9, CCM_CCGRx_CGn(8));

	clk[VF610_CLK_ESAI_SEL] = imx_clk_mux("esai_sel", CCM_CSCMR1, 20, 2, esai_sels, 4);
	clk[VF610_CLK_ESAI_EN] = imx_clk_gate("esai_en", "esai_sel", CCM_CSCDR2, 30);
	clk[VF610_CLK_ESAI_DIV] = imx_clk_divider("esai_div", "esai_en", CCM_CSCDR2, 24, 4);
	clk[VF610_CLK_ESAI] = imx_clk_gate2("esai", "esai_div", CCM_CCGR4, CCM_CCGRx_CGn(2));

	clk[VF610_CLK_SAI0_SEL] = imx_clk_mux("sai0_sel", CCM_CSCMR1, 0, 2, sai_sels, 4);
	clk[VF610_CLK_SAI0_EN] = imx_clk_gate("sai0_en", "sai0_sel", CCM_CSCDR1, 16);
	clk[VF610_CLK_SAI0_DIV] = imx_clk_divider("sai0_div", "sai0_en", CCM_CSCDR1, 0, 4);
	clk[VF610_CLK_SAI0] = imx_clk_gate2("sai0", "sai0_div", CCM_CCGR0, CCM_CCGRx_CGn(15));

	clk[VF610_CLK_SAI1_SEL] = imx_clk_mux("sai1_sel", CCM_CSCMR1, 2, 2, sai_sels, 4);
	clk[VF610_CLK_SAI1_EN] = imx_clk_gate("sai1_en", "sai1_sel", CCM_CSCDR1, 17);
	clk[VF610_CLK_SAI1_DIV] = imx_clk_divider("sai1_div", "sai1_en", CCM_CSCDR1, 4, 4);
	clk[VF610_CLK_SAI1] = imx_clk_gate2("sai1", "sai1_div", CCM_CCGR1, CCM_CCGRx_CGn(0));

	clk[VF610_CLK_SAI2_SEL] = imx_clk_mux("sai2_sel", CCM_CSCMR1, 4, 2, sai_sels, 4);
	clk[VF610_CLK_SAI2_EN] = imx_clk_gate("sai2_en", "sai2_sel", CCM_CSCDR1, 18);
	clk[VF610_CLK_SAI2_DIV] = imx_clk_divider("sai2_div", "sai2_en", CCM_CSCDR1, 8, 4);
	clk[VF610_CLK_SAI2] = imx_clk_gate2("sai2", "sai2_div", CCM_CCGR1, CCM_CCGRx_CGn(1));

	clk[VF610_CLK_SAI3_SEL] = imx_clk_mux("sai3_sel", CCM_CSCMR1, 6, 2, sai_sels, 4);
	clk[VF610_CLK_SAI3_EN] = imx_clk_gate("sai3_en", "sai3_sel", CCM_CSCDR1, 19);
	clk[VF610_CLK_SAI3_DIV] = imx_clk_divider("sai3_div", "sai3_en", CCM_CSCDR1, 12, 4);
	clk[VF610_CLK_SAI3] = imx_clk_gate2("sai3", "sai3_div", CCM_CCGR1, CCM_CCGRx_CGn(2));

	clk[VF610_CLK_NFC_SEL] = imx_clk_mux("nfc_sel", CCM_CSCMR1, 12, 2, nfc_sels, 4);
	clk[VF610_CLK_NFC_EN] = imx_clk_gate("nfc_en", "nfc_sel", CCM_CSCDR2, 9);
	clk[VF610_CLK_NFC_PRE_DIV] = imx_clk_divider("nfc_pre_div", "nfc_en", CCM_CSCDR3, 13, 3);
	clk[VF610_CLK_NFC_FRAC_DIV] = imx_clk_divider("nfc_frac_div", "nfc_pre_div", CCM_CSCDR2, 4, 4);
	clk[VF610_CLK_NFC] = imx_clk_gate2("nfc", "nfc_frac_div", CCM_CCGR10, CCM_CCGRx_CGn(0));

	clk[VF610_CLK_GPU_SEL] = imx_clk_mux("gpu_sel", CCM_CSCMR1, 14, 1, gpu_sels, 2);
	clk[VF610_CLK_GPU_EN] = imx_clk_gate("gpu_en", "gpu_sel", CCM_CSCDR2, 10);
	clk[VF610_CLK_GPU2D] = imx_clk_gate2("gpu", "gpu_en", CCM_CCGR8, CCM_CCGRx_CGn(15));

	clk[VF610_CLK_VADC_SEL] = imx_clk_mux("vadc_sel", CCM_CSCMR1, 8, 2, vadc_sels, 3);
	clk[VF610_CLK_VADC_EN] = imx_clk_gate("vadc_en", "vadc_sel", CCM_CSCDR1, 22);
	clk[VF610_CLK_VADC_DIV] = imx_clk_divider("vadc_div", "vadc_en", CCM_CSCDR1, 20, 2);
	clk[VF610_CLK_VADC_DIV_HALF] = imx_clk_fixed_factor("vadc_div_half", "vadc_div", 1, 2);
	clk[VF610_CLK_VADC] = imx_clk_gate2("vadc", "vadc_div", CCM_CCGR8, CCM_CCGRx_CGn(7));

	clk[VF610_CLK_ADC0] = imx_clk_gate2("adc0", "ipg_bus", CCM_CCGR1, CCM_CCGRx_CGn(11));
	clk[VF610_CLK_ADC1] = imx_clk_gate2("adc1", "ipg_bus", CCM_CCGR7, CCM_CCGRx_CGn(11));
	clk[VF610_CLK_DAC0] = imx_clk_gate2("dac0", "ipg_bus", CCM_CCGR8, CCM_CCGRx_CGn(12));
	clk[VF610_CLK_DAC1] = imx_clk_gate2("dac1", "ipg_bus", CCM_CCGR8, CCM_CCGRx_CGn(13));

	clk[VF610_CLK_ASRC] = imx_clk_gate2("asrc", "ipg_bus", CCM_CCGR4, CCM_CCGRx_CGn(1));

	clk[VF610_CLK_FLEXCAN0] = imx_clk_gate2("flexcan0", "ipg_bus", CCM_CCGR0, CCM_CCGRx_CGn(0));
	clk[VF610_CLK_FLEXCAN1] = imx_clk_gate2("flexcan1", "ipg_bus", CCM_CCGR9, CCM_CCGRx_CGn(4));

	clk[VF610_CLK_DMAMUX0] = imx_clk_gate2("dmamux0", "platform_bus", CCM_CCGR0, CCM_CCGRx_CGn(4));
	clk[VF610_CLK_DMAMUX1] = imx_clk_gate2("dmamux1", "platform_bus", CCM_CCGR0, CCM_CCGRx_CGn(5));
	clk[VF610_CLK_DMAMUX2] = imx_clk_gate2("dmamux2", "platform_bus", CCM_CCGR6, CCM_CCGRx_CGn(1));
	clk[VF610_CLK_DMAMUX3] = imx_clk_gate2("dmamux3", "platform_bus", CCM_CCGR6, CCM_CCGRx_CGn(2));

	imx_check_clocks(clk, ARRAY_SIZE(clk));

	clk_set_parent(clk[VF610_CLK_QSPI0_SEL], clk[VF610_CLK_PLL1_PFD4]);
	clk_set_rate(clk[VF610_CLK_QSPI0_X4_DIV], clk_get_rate(clk[VF610_CLK_QSPI0_SEL]) / 2);
	clk_set_rate(clk[VF610_CLK_QSPI0_X2_DIV], clk_get_rate(clk[VF610_CLK_QSPI0_X4_DIV]) / 2);
	clk_set_rate(clk[VF610_CLK_QSPI0_X1_DIV], clk_get_rate(clk[VF610_CLK_QSPI0_X2_DIV]) / 2);

	clk_set_parent(clk[VF610_CLK_QSPI1_SEL], clk[VF610_CLK_PLL1_PFD4]);
	clk_set_rate(clk[VF610_CLK_QSPI1_X4_DIV], clk_get_rate(clk[VF610_CLK_QSPI1_SEL]) / 2);
	clk_set_rate(clk[VF610_CLK_QSPI1_X2_DIV], clk_get_rate(clk[VF610_CLK_QSPI1_X4_DIV]) / 2);
	clk_set_rate(clk[VF610_CLK_QSPI1_X1_DIV], clk_get_rate(clk[VF610_CLK_QSPI1_X2_DIV]) / 2);

	clk_set_parent(clk[VF610_CLK_SAI0_SEL], clk[VF610_CLK_AUDIO_EXT]);
	clk_set_parent(clk[VF610_CLK_SAI1_SEL], clk[VF610_CLK_AUDIO_EXT]);
	clk_set_parent(clk[VF610_CLK_SAI2_SEL], clk[VF610_CLK_AUDIO_EXT]);
	clk_set_parent(clk[VF610_CLK_SAI3_SEL], clk[VF610_CLK_AUDIO_EXT]);

	/* Add the clocks to provider list */
	clk_data.clks = clk;
	clk_data.clk_num = ARRAY_SIZE(clk);
	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);
}
CLK_OF_DECLARE(vf610, "fsl,vf610-ccm", vf610_clocks_init);
