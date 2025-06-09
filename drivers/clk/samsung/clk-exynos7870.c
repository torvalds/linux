// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Samsung Electronics Co., Ltd.
 * Author: Kaustabh Chakraborty <kauschluss@disroot.org>
 *
 * Common Clock Framework support for Exynos7870.
 */

#include <linux/clk-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <dt-bindings/clock/samsung,exynos7870-cmu.h>

#include "clk.h"
#include "clk-exynos-arm64.h"

/*
 * Register offsets for CMU_MIF (0x10460000)
 */
#define PLL_LOCKTIME_MIF_MEM_PLL			0x0000
#define PLL_LOCKTIME_MIF_MEDIA_PLL			0x0020
#define PLL_LOCKTIME_MIF_BUS_PLL			0x0040
#define PLL_CON0_MIF_MEM_PLL				0x0100
#define PLL_CON0_MIF_MEDIA_PLL				0x0120
#define PLL_CON0_MIF_BUS_PLL				0x0140
#define CLK_CON_GAT_MIF_MUX_MEM_PLL			0x0200
#define CLK_CON_GAT_MIF_MUX_MEM_PLL_CON			0x0200
#define CLK_CON_GAT_MIF_MUX_MEDIA_PLL			0x0204
#define CLK_CON_GAT_MIF_MUX_MEDIA_PLL_CON		0x0204
#define CLK_CON_GAT_MIF_MUX_BUS_PLL			0x0208
#define CLK_CON_GAT_MIF_MUX_BUS_PLL_CON			0x0208
#define CLK_CON_GAT_MIF_MUX_BUSD			0x0220
#define CLK_CON_MUX_MIF_BUSD				0x0220
#define CLK_CON_GAT_MIF_MUX_CMU_ISP_VRA			0x0264
#define CLK_CON_MUX_MIF_CMU_ISP_VRA			0x0264
#define CLK_CON_GAT_MIF_MUX_CMU_ISP_CAM			0x0268
#define CLK_CON_MUX_MIF_CMU_ISP_CAM			0x0268
#define CLK_CON_GAT_MIF_MUX_CMU_ISP_ISP			0x026c
#define CLK_CON_MUX_MIF_CMU_ISP_ISP			0x026c
#define CLK_CON_GAT_MIF_MUX_CMU_DISPAUD_BUS		0x0270
#define CLK_CON_MUX_MIF_CMU_DISPAUD_BUS			0x0270
#define CLK_CON_GAT_MIF_MUX_CMU_DISPAUD_DECON_VCLK	0x0274
#define CLK_CON_MUX_MIF_CMU_DISPAUD_DECON_VCLK		0x0274
#define CLK_CON_GAT_MIF_MUX_CMU_DISPAUD_DECON_ECLK	0x0278
#define CLK_CON_MUX_MIF_CMU_DISPAUD_DECON_ECLK		0x0278
#define CLK_CON_GAT_MIF_MUX_CMU_MFCMSCL_MSCL		0x027c
#define CLK_CON_MUX_MIF_CMU_MFCMSCL_MSCL		0x027c
#define CLK_CON_GAT_MIF_MUX_CMU_MFCMSCL_MFC		0x0280
#define CLK_CON_MUX_MIF_CMU_MFCMSCL_MFC			0x0280
#define CLK_CON_GAT_MIF_MUX_CMU_FSYS_BUS		0x0284
#define CLK_CON_MUX_MIF_CMU_FSYS_BUS			0x0284
#define CLK_CON_GAT_MIF_MUX_CMU_FSYS_MMC0		0x0288
#define CLK_CON_MUX_MIF_CMU_FSYS_MMC0			0x0288
#define CLK_CON_GAT_MIF_MUX_CMU_FSYS_MMC1		0x028c
#define CLK_CON_MUX_MIF_CMU_FSYS_MMC1			0x028c
#define CLK_CON_GAT_MIF_MUX_CMU_FSYS_MMC2		0x0290
#define CLK_CON_MUX_MIF_CMU_FSYS_MMC2			0x0290
#define CLK_CON_GAT_MIF_MUX_CMU_FSYS_USB20DRD_REFCLK	0x029c
#define CLK_CON_MUX_MIF_CMU_FSYS_USB20DRD_REFCLK	0x029c
#define CLK_CON_GAT_MIF_MUX_CMU_PERI_BUS		0x02a0
#define CLK_CON_MUX_MIF_CMU_PERI_BUS			0x02a0
#define CLK_CON_GAT_MIF_MUX_CMU_PERI_UART1		0x02a4
#define CLK_CON_MUX_MIF_CMU_PERI_UART1			0x02a4
#define CLK_CON_GAT_MIF_MUX_CMU_PERI_UART2		0x02a8
#define CLK_CON_MUX_MIF_CMU_PERI_UART2			0x02a8
#define CLK_CON_GAT_MIF_MUX_CMU_PERI_UART0		0x02ac
#define CLK_CON_MUX_MIF_CMU_PERI_UART0			0x02ac
#define CLK_CON_GAT_MIF_MUX_CMU_PERI_SPI2		0x02b0
#define CLK_CON_MUX_MIF_CMU_PERI_SPI2			0x02b0
#define CLK_CON_GAT_MIF_MUX_CMU_PERI_SPI1		0x02b4
#define CLK_CON_MUX_MIF_CMU_PERI_SPI1			0x02b4
#define CLK_CON_GAT_MIF_MUX_CMU_PERI_SPI0		0x02b8
#define CLK_CON_MUX_MIF_CMU_PERI_SPI0			0x02b8
#define CLK_CON_GAT_MIF_MUX_CMU_PERI_SPI3		0x02bc
#define CLK_CON_MUX_MIF_CMU_PERI_SPI3			0x02bc
#define CLK_CON_GAT_MIF_MUX_CMU_PERI_SPI4		0x02c0
#define CLK_CON_MUX_MIF_CMU_PERI_SPI4			0x02c0
#define CLK_CON_GAT_MIF_MUX_CMU_ISP_SENSOR0		0x02c4
#define CLK_CON_MUX_MIF_CMU_ISP_SENSOR0			0x02c4
#define CLK_CON_GAT_MIF_MUX_CMU_ISP_SENSOR1		0x02c8
#define CLK_CON_MUX_MIF_CMU_ISP_SENSOR1			0x02c8
#define CLK_CON_GAT_MIF_MUX_CMU_ISP_SENSOR2		0x02cc
#define CLK_CON_MUX_MIF_CMU_ISP_SENSOR2			0x02cc
#define CLK_CON_DIV_MIF_BUSD				0x0420
#define CLK_CON_DIV_MIF_APB				0x0424
#define CLK_CON_DIV_MIF_HSI2C				0x0430
#define CLK_CON_DIV_MIF_CMU_G3D_SWITCH			0x0460
#define CLK_CON_DIV_MIF_CMU_ISP_VRA			0x0464
#define CLK_CON_DIV_MIF_CMU_ISP_CAM			0x0468
#define CLK_CON_DIV_MIF_CMU_ISP_ISP			0x046c
#define CLK_CON_DIV_MIF_CMU_DISPAUD_BUS			0x0470
#define CLK_CON_DIV_MIF_CMU_DISPAUD_DECON_VCLK		0x0474
#define CLK_CON_DIV_MIF_CMU_DISPAUD_DECON_ECLK		0x0478
#define CLK_CON_DIV_MIF_CMU_MFCMSCL_MSCL		0x047c
#define CLK_CON_DIV_MIF_CMU_MFCMSCL_MFC			0x0480
#define CLK_CON_DIV_MIF_CMU_FSYS_BUS			0x0484
#define CLK_CON_DIV_MIF_CMU_FSYS_MMC0			0x0488
#define CLK_CON_DIV_MIF_CMU_FSYS_MMC1			0x048c
#define CLK_CON_DIV_MIF_CMU_FSYS_MMC2			0x0490
#define CLK_CON_DIV_MIF_CMU_FSYS_USB20DRD_REFCLK	0x049c
#define CLK_CON_DIV_MIF_CMU_PERI_BUS			0x04a0
#define CLK_CON_DIV_MIF_CMU_PERI_UART1			0x04a4
#define CLK_CON_DIV_MIF_CMU_PERI_UART2			0x04a8
#define CLK_CON_DIV_MIF_CMU_PERI_UART0			0x04ac
#define CLK_CON_DIV_MIF_CMU_PERI_SPI2			0x04b0
#define CLK_CON_DIV_MIF_CMU_PERI_SPI1			0x04b4
#define CLK_CON_DIV_MIF_CMU_PERI_SPI0			0x04b8
#define CLK_CON_DIV_MIF_CMU_PERI_SPI3			0x04bc
#define CLK_CON_DIV_MIF_CMU_PERI_SPI4			0x04c0
#define CLK_CON_DIV_MIF_CMU_ISP_SENSOR0			0x04c4
#define CLK_CON_DIV_MIF_CMU_ISP_SENSOR1			0x04c8
#define CLK_CON_DIV_MIF_CMU_ISP_SENSOR2			0x04cc
#define CLK_CON_GAT_MIF_WRAP_ADC_IF_OSC_SYS		0x080c
#define CLK_CON_GAT_MIF_HSI2C_AP_PCLKS			0x0828
#define CLK_CON_GAT_MIF_HSI2C_CP_PCLKS			0x0828
#define CLK_CON_GAT_MIF_WRAP_ADC_IF_PCLK_S0		0x0828
#define CLK_CON_GAT_MIF_WRAP_ADC_IF_PCLK_S1		0x0828
#define CLK_CON_GAT_MIF_HSI2C_AP_PCLKM			0x0840
#define CLK_CON_GAT_MIF_HSI2C_CP_PCLKM			0x0840
#define CLK_CON_GAT_MIF_HSI2C_IPCLK			0x0840
#define CLK_CON_GAT_MIF_HSI2C_ITCLK			0x0840
#define CLK_CON_GAT_MIF_CP_PCLK_HSI2C			0x0840
#define CLK_CON_GAT_MIF_CP_PCLK_HSI2C_BAT_0		0x0840
#define CLK_CON_GAT_MIF_CP_PCLK_HSI2C_BAT_1		0x0840
#define CLK_CON_GAT_MIF_CMU_G3D_SWITCH			0x0860
#define CLK_CON_GAT_MIF_CMU_ISP_VRA			0x0864
#define CLK_CON_GAT_MIF_CMU_ISP_CAM			0x0868
#define CLK_CON_GAT_MIF_CMU_ISP_ISP			0x086c
#define CLK_CON_GAT_MIF_CMU_DISPAUD_BUS			0x0870
#define CLK_CON_GAT_MIF_CMU_DISPAUD_DECON_VCLK		0x0874
#define CLK_CON_GAT_MIF_CMU_DISPAUD_DECON_ECLK		0x0878
#define CLK_CON_GAT_MIF_CMU_MFCMSCL_MSCL		0x087c
#define CLK_CON_GAT_MIF_CMU_MFCMSCL_MFC			0x0880
#define CLK_CON_GAT_MIF_CMU_FSYS_BUS			0x0884
#define CLK_CON_GAT_MIF_CMU_FSYS_MMC0			0x0888
#define CLK_CON_GAT_MIF_CMU_FSYS_MMC1			0x088c
#define CLK_CON_GAT_MIF_CMU_FSYS_MMC2			0x0890
#define CLK_CON_GAT_MIF_CMU_FSYS_USB20DRD_REFCLK	0x089c
#define CLK_CON_GAT_MIF_CMU_PERI_BUS			0x08a0
#define CLK_CON_GAT_MIF_CMU_PERI_UART1			0x08a4
#define CLK_CON_GAT_MIF_CMU_PERI_UART2			0x08a8
#define CLK_CON_GAT_MIF_CMU_PERI_UART0			0x08ac
#define CLK_CON_GAT_MIF_CMU_PERI_SPI2			0x08b0
#define CLK_CON_GAT_MIF_CMU_PERI_SPI1			0x08b4
#define CLK_CON_GAT_MIF_CMU_PERI_SPI0			0x08b8
#define CLK_CON_GAT_MIF_CMU_PERI_SPI3			0x08bc
#define CLK_CON_GAT_MIF_CMU_PERI_SPI4			0x08c0
#define CLK_CON_GAT_MIF_CMU_ISP_SENSOR0			0x08c4
#define CLK_CON_GAT_MIF_CMU_ISP_SENSOR1			0x08c8
#define CLK_CON_GAT_MIF_CMU_ISP_SENSOR2			0x08cc

static const unsigned long mif_clk_regs[] __initconst = {
	PLL_LOCKTIME_MIF_MEM_PLL,
	PLL_LOCKTIME_MIF_MEDIA_PLL,
	PLL_LOCKTIME_MIF_BUS_PLL,
	PLL_CON0_MIF_MEM_PLL,
	PLL_CON0_MIF_MEDIA_PLL,
	PLL_CON0_MIF_BUS_PLL,
	CLK_CON_GAT_MIF_MUX_MEM_PLL,
	CLK_CON_GAT_MIF_MUX_MEM_PLL_CON,
	CLK_CON_GAT_MIF_MUX_MEDIA_PLL,
	CLK_CON_GAT_MIF_MUX_MEDIA_PLL_CON,
	CLK_CON_GAT_MIF_MUX_BUS_PLL,
	CLK_CON_GAT_MIF_MUX_BUS_PLL_CON,
	CLK_CON_GAT_MIF_MUX_BUSD,
	CLK_CON_MUX_MIF_BUSD,
	CLK_CON_GAT_MIF_MUX_CMU_ISP_VRA,
	CLK_CON_MUX_MIF_CMU_ISP_VRA,
	CLK_CON_GAT_MIF_MUX_CMU_ISP_CAM,
	CLK_CON_MUX_MIF_CMU_ISP_CAM,
	CLK_CON_GAT_MIF_MUX_CMU_ISP_ISP,
	CLK_CON_MUX_MIF_CMU_ISP_ISP,
	CLK_CON_GAT_MIF_MUX_CMU_DISPAUD_BUS,
	CLK_CON_MUX_MIF_CMU_DISPAUD_BUS,
	CLK_CON_GAT_MIF_MUX_CMU_DISPAUD_DECON_VCLK,
	CLK_CON_MUX_MIF_CMU_DISPAUD_DECON_VCLK,
	CLK_CON_GAT_MIF_MUX_CMU_DISPAUD_DECON_ECLK,
	CLK_CON_MUX_MIF_CMU_DISPAUD_DECON_ECLK,
	CLK_CON_GAT_MIF_MUX_CMU_MFCMSCL_MSCL,
	CLK_CON_MUX_MIF_CMU_MFCMSCL_MSCL,
	CLK_CON_GAT_MIF_MUX_CMU_MFCMSCL_MFC,
	CLK_CON_MUX_MIF_CMU_MFCMSCL_MFC,
	CLK_CON_GAT_MIF_MUX_CMU_FSYS_BUS,
	CLK_CON_MUX_MIF_CMU_FSYS_BUS,
	CLK_CON_GAT_MIF_MUX_CMU_FSYS_MMC0,
	CLK_CON_MUX_MIF_CMU_FSYS_MMC0,
	CLK_CON_GAT_MIF_MUX_CMU_FSYS_MMC1,
	CLK_CON_MUX_MIF_CMU_FSYS_MMC1,
	CLK_CON_GAT_MIF_MUX_CMU_FSYS_MMC2,
	CLK_CON_MUX_MIF_CMU_FSYS_MMC2,
	CLK_CON_GAT_MIF_MUX_CMU_FSYS_USB20DRD_REFCLK,
	CLK_CON_MUX_MIF_CMU_FSYS_USB20DRD_REFCLK,
	CLK_CON_GAT_MIF_MUX_CMU_PERI_BUS,
	CLK_CON_MUX_MIF_CMU_PERI_BUS,
	CLK_CON_GAT_MIF_MUX_CMU_PERI_UART1,
	CLK_CON_MUX_MIF_CMU_PERI_UART1,
	CLK_CON_GAT_MIF_MUX_CMU_PERI_UART2,
	CLK_CON_MUX_MIF_CMU_PERI_UART2,
	CLK_CON_GAT_MIF_MUX_CMU_PERI_UART0,
	CLK_CON_MUX_MIF_CMU_PERI_UART0,
	CLK_CON_GAT_MIF_MUX_CMU_PERI_SPI2,
	CLK_CON_MUX_MIF_CMU_PERI_SPI2,
	CLK_CON_GAT_MIF_MUX_CMU_PERI_SPI1,
	CLK_CON_MUX_MIF_CMU_PERI_SPI1,
	CLK_CON_GAT_MIF_MUX_CMU_PERI_SPI0,
	CLK_CON_MUX_MIF_CMU_PERI_SPI0,
	CLK_CON_GAT_MIF_MUX_CMU_PERI_SPI3,
	CLK_CON_MUX_MIF_CMU_PERI_SPI3,
	CLK_CON_GAT_MIF_MUX_CMU_PERI_SPI4,
	CLK_CON_MUX_MIF_CMU_PERI_SPI4,
	CLK_CON_GAT_MIF_MUX_CMU_ISP_SENSOR0,
	CLK_CON_MUX_MIF_CMU_ISP_SENSOR0,
	CLK_CON_GAT_MIF_MUX_CMU_ISP_SENSOR1,
	CLK_CON_MUX_MIF_CMU_ISP_SENSOR1,
	CLK_CON_GAT_MIF_MUX_CMU_ISP_SENSOR2,
	CLK_CON_MUX_MIF_CMU_ISP_SENSOR2,
	CLK_CON_DIV_MIF_BUSD,
	CLK_CON_DIV_MIF_APB,
	CLK_CON_DIV_MIF_HSI2C,
	CLK_CON_DIV_MIF_CMU_G3D_SWITCH,
	CLK_CON_DIV_MIF_CMU_ISP_VRA,
	CLK_CON_DIV_MIF_CMU_ISP_CAM,
	CLK_CON_DIV_MIF_CMU_ISP_ISP,
	CLK_CON_DIV_MIF_CMU_DISPAUD_BUS,
	CLK_CON_DIV_MIF_CMU_DISPAUD_DECON_VCLK,
	CLK_CON_DIV_MIF_CMU_DISPAUD_DECON_ECLK,
	CLK_CON_DIV_MIF_CMU_MFCMSCL_MSCL,
	CLK_CON_DIV_MIF_CMU_MFCMSCL_MFC,
	CLK_CON_DIV_MIF_CMU_FSYS_BUS,
	CLK_CON_DIV_MIF_CMU_FSYS_MMC0,
	CLK_CON_DIV_MIF_CMU_FSYS_MMC1,
	CLK_CON_DIV_MIF_CMU_FSYS_MMC2,
	CLK_CON_DIV_MIF_CMU_FSYS_USB20DRD_REFCLK,
	CLK_CON_DIV_MIF_CMU_PERI_BUS,
	CLK_CON_DIV_MIF_CMU_PERI_UART1,
	CLK_CON_DIV_MIF_CMU_PERI_UART2,
	CLK_CON_DIV_MIF_CMU_PERI_UART0,
	CLK_CON_DIV_MIF_CMU_PERI_SPI2,
	CLK_CON_DIV_MIF_CMU_PERI_SPI1,
	CLK_CON_DIV_MIF_CMU_PERI_SPI0,
	CLK_CON_DIV_MIF_CMU_PERI_SPI3,
	CLK_CON_DIV_MIF_CMU_PERI_SPI4,
	CLK_CON_DIV_MIF_CMU_ISP_SENSOR0,
	CLK_CON_DIV_MIF_CMU_ISP_SENSOR1,
	CLK_CON_DIV_MIF_CMU_ISP_SENSOR2,
	CLK_CON_GAT_MIF_WRAP_ADC_IF_OSC_SYS,
	CLK_CON_GAT_MIF_HSI2C_AP_PCLKS,
	CLK_CON_GAT_MIF_HSI2C_CP_PCLKS,
	CLK_CON_GAT_MIF_WRAP_ADC_IF_PCLK_S0,
	CLK_CON_GAT_MIF_WRAP_ADC_IF_PCLK_S1,
	CLK_CON_GAT_MIF_HSI2C_AP_PCLKM,
	CLK_CON_GAT_MIF_HSI2C_CP_PCLKM,
	CLK_CON_GAT_MIF_HSI2C_IPCLK,
	CLK_CON_GAT_MIF_HSI2C_ITCLK,
	CLK_CON_GAT_MIF_CP_PCLK_HSI2C,
	CLK_CON_GAT_MIF_CP_PCLK_HSI2C_BAT_0,
	CLK_CON_GAT_MIF_CP_PCLK_HSI2C_BAT_1,
	CLK_CON_GAT_MIF_CMU_G3D_SWITCH,
	CLK_CON_GAT_MIF_CMU_ISP_VRA,
	CLK_CON_GAT_MIF_CMU_ISP_CAM,
	CLK_CON_GAT_MIF_CMU_ISP_ISP,
	CLK_CON_GAT_MIF_CMU_DISPAUD_BUS,
	CLK_CON_GAT_MIF_CMU_DISPAUD_DECON_VCLK,
	CLK_CON_GAT_MIF_CMU_DISPAUD_DECON_ECLK,
	CLK_CON_GAT_MIF_CMU_MFCMSCL_MSCL,
	CLK_CON_GAT_MIF_CMU_MFCMSCL_MFC,
	CLK_CON_GAT_MIF_CMU_FSYS_BUS,
	CLK_CON_GAT_MIF_CMU_FSYS_MMC0,
	CLK_CON_GAT_MIF_CMU_FSYS_MMC1,
	CLK_CON_GAT_MIF_CMU_FSYS_MMC2,
	CLK_CON_GAT_MIF_CMU_FSYS_USB20DRD_REFCLK,
	CLK_CON_GAT_MIF_CMU_PERI_BUS,
	CLK_CON_GAT_MIF_CMU_PERI_UART1,
	CLK_CON_GAT_MIF_CMU_PERI_UART2,
	CLK_CON_GAT_MIF_CMU_PERI_UART0,
	CLK_CON_GAT_MIF_CMU_PERI_SPI2,
	CLK_CON_GAT_MIF_CMU_PERI_SPI1,
	CLK_CON_GAT_MIF_CMU_PERI_SPI0,
	CLK_CON_GAT_MIF_CMU_PERI_SPI3,
	CLK_CON_GAT_MIF_CMU_PERI_SPI4,
	CLK_CON_GAT_MIF_CMU_ISP_SENSOR0,
	CLK_CON_GAT_MIF_CMU_ISP_SENSOR1,
	CLK_CON_GAT_MIF_CMU_ISP_SENSOR2,
};

static const struct samsung_fixed_factor_clock mif_fixed_factor_clks[] __initconst = {
	FFACTOR(0, "ffac_mif_mux_bus_pll_div2", "gout_mif_mux_bus_pll_con", 1, 2,
		0),
	FFACTOR(0, "ffac_mif_mux_media_pll_div2", "gout_mif_mux_media_pll_con",
		1, 2, 0),
	FFACTOR(0, "ffac_mif_mux_mem_pll_div2", "gout_mif_mux_mem_pll_con", 1, 2,
		0),
};

static const struct samsung_pll_clock mif_pll_clks[] __initconst = {
	PLL(pll_1417x, CLK_FOUT_MIF_BUS_PLL, "fout_mif_bus_pll", "oscclk",
	    PLL_LOCKTIME_MIF_BUS_PLL, PLL_CON0_MIF_BUS_PLL, NULL),
	PLL(pll_1417x, CLK_FOUT_MIF_MEDIA_PLL, "fout_mif_media_pll", "oscclk",
	    PLL_LOCKTIME_MIF_MEDIA_PLL, PLL_CON0_MIF_MEDIA_PLL, NULL),
	PLL(pll_1417x, CLK_FOUT_MIF_MEM_PLL, "fout_mif_mem_pll", "oscclk",
	    PLL_LOCKTIME_MIF_MEM_PLL, PLL_CON0_MIF_MEM_PLL, NULL),
};

/* List of parent clocks for muxes in CMU_MIF */
PNAME(mout_mif_cmu_dispaud_bus_p)		= { "ffac_mif_mux_bus_pll_div2",
						    "ffac_mif_mux_media_pll_div2" };
PNAME(mout_mif_cmu_dispaud_decon_eclk_p)	= { "ffac_mif_mux_bus_pll_div2",
						    "ffac_mif_mux_media_pll_div2" };
PNAME(mout_mif_cmu_dispaud_decon_vclk_p)	= { "ffac_mif_mux_bus_pll_div2",
						    "ffac_mif_mux_media_pll_div2" };
PNAME(mout_mif_cmu_fsys_bus_p)			= { "ffac_mif_mux_bus_pll_div2",
						    "ffac_mif_mux_media_pll_div2" };
PNAME(mout_mif_cmu_fsys_mmc0_p)			= { "ffac_mif_mux_bus_pll_div2",
						    "ffac_mif_mux_media_pll_div2" };
PNAME(mout_mif_cmu_fsys_mmc1_p)			= { "ffac_mif_mux_bus_pll_div2",
						    "ffac_mif_mux_media_pll_div2" };
PNAME(mout_mif_cmu_fsys_mmc2_p)			= { "ffac_mif_mux_bus_pll_div2",
						    "ffac_mif_mux_media_pll_div2" };
PNAME(mout_mif_cmu_fsys_usb20drd_refclk_p)	= { "ffac_mif_mux_bus_pll_div2",
						    "ffac_mif_mux_media_pll_div2" };
PNAME(mout_mif_cmu_isp_cam_p)			= { "ffac_mif_mux_bus_pll_div2",
						    "ffac_mif_mux_media_pll_div2" };
PNAME(mout_mif_cmu_isp_isp_p)			= { "ffac_mif_mux_bus_pll_div2",
						    "ffac_mif_mux_media_pll_div2" };
PNAME(mout_mif_cmu_isp_sensor0_p)		= { "ffac_mif_mux_bus_pll_div2",
						    "oscclk" };
PNAME(mout_mif_cmu_isp_sensor1_p)		= { "ffac_mif_mux_bus_pll_div2",
						    "oscclk" };
PNAME(mout_mif_cmu_isp_sensor2_p)		= { "ffac_mif_mux_bus_pll_div2",
						    "oscclk" };
PNAME(mout_mif_cmu_isp_vra_p)			= { "ffac_mif_mux_bus_pll_div2",
						    "ffac_mif_mux_media_pll_div2",
						    "gout_mif_mux_bus_pll_con" };
PNAME(mout_mif_cmu_mfcmscl_mfc_p)		= { "ffac_mif_mux_bus_pll_div2",
						    "ffac_mif_mux_media_pll_div2",
						    "gout_mif_mux_bus_pll_con" };
PNAME(mout_mif_cmu_mfcmscl_mscl_p)		= { "ffac_mif_mux_bus_pll_div2",
						    "ffac_mif_mux_media_pll_div2",
						    "gout_mif_mux_bus_pll_con" };
PNAME(mout_mif_cmu_peri_bus_p)			= { "ffac_mif_mux_bus_pll_div2",
						    "ffac_mif_mux_media_pll_div2" };
PNAME(mout_mif_cmu_peri_spi0_p)			= { "ffac_mif_mux_bus_pll_div2",
						    "oscclk" };
PNAME(mout_mif_cmu_peri_spi2_p)			= { "ffac_mif_mux_bus_pll_div2",
						    "oscclk" };
PNAME(mout_mif_cmu_peri_spi1_p)			= { "ffac_mif_mux_bus_pll_div2",
						    "oscclk" };
PNAME(mout_mif_cmu_peri_spi4_p)			= { "ffac_mif_mux_bus_pll_div2",
						    "oscclk" };
PNAME(mout_mif_cmu_peri_spi3_p)			= { "ffac_mif_mux_bus_pll_div2",
						    "oscclk" };
PNAME(mout_mif_cmu_peri_uart1_p)		= { "ffac_mif_mux_bus_pll_div2",
						    "ffac_mif_mux_media_pll_div2" };
PNAME(mout_mif_cmu_peri_uart2_p)		= { "ffac_mif_mux_bus_pll_div2",
						    "ffac_mif_mux_media_pll_div2" };
PNAME(mout_mif_cmu_peri_uart0_p)		= { "ffac_mif_mux_bus_pll_div2",
						    "ffac_mif_mux_media_pll_div2" };
PNAME(mout_mif_busd_p)				= { "ffac_mif_mux_bus_pll_div2",
						    "ffac_mif_mux_media_pll_div2",
						    "ffac_mif_mux_mem_pll_div2" };

static const struct samsung_mux_clock mif_mux_clks[] __initconst = {
	MUX(CLK_MOUT_MIF_CMU_DISPAUD_BUS, "mout_mif_cmu_dispaud_bus",
	    mout_mif_cmu_dispaud_bus_p, CLK_CON_MUX_MIF_CMU_DISPAUD_BUS, 12, 1),
	MUX(CLK_MOUT_MIF_CMU_DISPAUD_DECON_ECLK,
	    "mout_mif_cmu_dispaud_decon_eclk",
	    mout_mif_cmu_dispaud_decon_eclk_p,
	    CLK_CON_MUX_MIF_CMU_DISPAUD_DECON_ECLK, 12, 1),
	MUX(CLK_MOUT_MIF_CMU_DISPAUD_DECON_VCLK,
	    "mout_mif_cmu_dispaud_decon_vclk",
	    mout_mif_cmu_dispaud_decon_vclk_p,
	    CLK_CON_MUX_MIF_CMU_DISPAUD_DECON_VCLK, 12, 1),
	MUX(CLK_MOUT_MIF_CMU_FSYS_BUS, "mout_mif_cmu_fsys_bus",
	    mout_mif_cmu_fsys_bus_p, CLK_CON_MUX_MIF_CMU_FSYS_BUS, 12, 1),
	MUX(CLK_MOUT_MIF_CMU_FSYS_MMC0, "mout_mif_cmu_fsys_mmc0",
	    mout_mif_cmu_fsys_mmc0_p, CLK_CON_MUX_MIF_CMU_FSYS_MMC0, 12, 1),
	MUX(CLK_MOUT_MIF_CMU_FSYS_MMC1, "mout_mif_cmu_fsys_mmc1",
	    mout_mif_cmu_fsys_mmc1_p, CLK_CON_MUX_MIF_CMU_FSYS_MMC1, 12, 1),
	MUX(CLK_MOUT_MIF_CMU_FSYS_MMC2, "mout_mif_cmu_fsys_mmc2",
	    mout_mif_cmu_fsys_mmc2_p, CLK_CON_MUX_MIF_CMU_FSYS_MMC2, 12, 1),
	MUX(CLK_MOUT_MIF_CMU_FSYS_USB20DRD_REFCLK,
	    "mout_mif_cmu_fsys_usb20drd_refclk",
	    mout_mif_cmu_fsys_usb20drd_refclk_p,
	    CLK_CON_MUX_MIF_CMU_FSYS_USB20DRD_REFCLK, 12, 1),
	MUX(CLK_MOUT_MIF_CMU_ISP_CAM, "mout_mif_cmu_isp_cam",
	    mout_mif_cmu_isp_cam_p, CLK_CON_MUX_MIF_CMU_ISP_CAM, 12, 1),
	MUX(CLK_MOUT_MIF_CMU_ISP_ISP, "mout_mif_cmu_isp_isp",
	    mout_mif_cmu_isp_isp_p, CLK_CON_MUX_MIF_CMU_ISP_ISP, 12, 1),
	MUX(CLK_MOUT_MIF_CMU_ISP_SENSOR0, "mout_mif_cmu_isp_sensor0",
	    mout_mif_cmu_isp_sensor0_p, CLK_CON_MUX_MIF_CMU_ISP_SENSOR0, 12, 1),
	MUX(CLK_MOUT_MIF_CMU_ISP_SENSOR1, "mout_mif_cmu_isp_sensor1",
	    mout_mif_cmu_isp_sensor1_p, CLK_CON_MUX_MIF_CMU_ISP_SENSOR1, 12, 1),
	MUX(CLK_MOUT_MIF_CMU_ISP_SENSOR2, "mout_mif_cmu_isp_sensor2",
	    mout_mif_cmu_isp_sensor2_p, CLK_CON_MUX_MIF_CMU_ISP_SENSOR2, 12, 1),
	MUX(CLK_MOUT_MIF_CMU_ISP_VRA, "mout_mif_cmu_isp_vra",
	    mout_mif_cmu_isp_vra_p, CLK_CON_MUX_MIF_CMU_ISP_VRA, 12, 2),
	MUX(CLK_MOUT_MIF_CMU_MFCMSCL_MFC, "mout_mif_cmu_mfcmscl_mfc",
	    mout_mif_cmu_mfcmscl_mfc_p, CLK_CON_MUX_MIF_CMU_MFCMSCL_MFC, 12, 2),
	MUX(CLK_MOUT_MIF_CMU_MFCMSCL_MSCL, "mout_mif_cmu_mfcmscl_mscl",
	    mout_mif_cmu_mfcmscl_mscl_p, CLK_CON_MUX_MIF_CMU_MFCMSCL_MSCL, 12,
	    2),
	MUX(CLK_MOUT_MIF_CMU_PERI_BUS, "mout_mif_cmu_peri_bus",
	    mout_mif_cmu_peri_bus_p, CLK_CON_MUX_MIF_CMU_PERI_BUS, 12, 1),
	MUX(CLK_MOUT_MIF_CMU_PERI_SPI0, "mout_mif_cmu_peri_spi0",
	    mout_mif_cmu_peri_spi0_p, CLK_CON_MUX_MIF_CMU_PERI_SPI0, 12, 1),
	MUX(CLK_MOUT_MIF_CMU_PERI_SPI2, "mout_mif_cmu_peri_spi2",
	    mout_mif_cmu_peri_spi2_p, CLK_CON_MUX_MIF_CMU_PERI_SPI2, 12, 1),
	MUX(CLK_MOUT_MIF_CMU_PERI_SPI1, "mout_mif_cmu_peri_spi1",
	    mout_mif_cmu_peri_spi1_p, CLK_CON_MUX_MIF_CMU_PERI_SPI1, 12, 1),
	MUX(CLK_MOUT_MIF_CMU_PERI_SPI4, "mout_mif_cmu_peri_spi4",
	    mout_mif_cmu_peri_spi4_p, CLK_CON_MUX_MIF_CMU_PERI_SPI4, 12, 1),
	MUX(CLK_MOUT_MIF_CMU_PERI_SPI3, "mout_mif_cmu_peri_spi3",
	    mout_mif_cmu_peri_spi3_p, CLK_CON_MUX_MIF_CMU_PERI_SPI3, 12, 1),
	MUX(CLK_MOUT_MIF_CMU_PERI_UART1, "mout_mif_cmu_peri_uart1",
	    mout_mif_cmu_peri_uart1_p, CLK_CON_MUX_MIF_CMU_PERI_UART1, 12, 1),
	MUX(CLK_MOUT_MIF_CMU_PERI_UART2, "mout_mif_cmu_peri_uart2",
	    mout_mif_cmu_peri_uart2_p, CLK_CON_MUX_MIF_CMU_PERI_UART2, 12, 1),
	MUX(CLK_MOUT_MIF_CMU_PERI_UART0, "mout_mif_cmu_peri_uart0",
	    mout_mif_cmu_peri_uart0_p, CLK_CON_MUX_MIF_CMU_PERI_UART0, 12, 1),
	MUX(CLK_MOUT_MIF_BUSD, "mout_mif_busd", mout_mif_busd_p,
	    CLK_CON_MUX_MIF_BUSD, 12, 2),
};

static const struct samsung_div_clock mif_div_clks[] __initconst = {
	DIV(CLK_DOUT_MIF_CMU_DISPAUD_BUS, "dout_mif_cmu_dispaud_bus",
	    "gout_mif_mux_cmu_dispaud_bus", CLK_CON_DIV_MIF_CMU_DISPAUD_BUS, 0,
	    4),
	DIV(CLK_DOUT_MIF_CMU_DISPAUD_DECON_ECLK,
	    "dout_mif_cmu_dispaud_decon_eclk",
	    "gout_mif_mux_cmu_dispaud_decon_eclk",
	    CLK_CON_DIV_MIF_CMU_DISPAUD_DECON_ECLK, 0, 4),
	DIV(CLK_DOUT_MIF_CMU_DISPAUD_DECON_VCLK,
	    "dout_mif_cmu_dispaud_decon_vclk",
	    "gout_mif_mux_cmu_dispaud_decon_vclk",
	    CLK_CON_DIV_MIF_CMU_DISPAUD_DECON_VCLK, 0, 4),
	DIV(CLK_DOUT_MIF_CMU_FSYS_BUS, "dout_mif_cmu_fsys_bus",
	    "gout_mif_mux_cmu_fsys_bus", CLK_CON_DIV_MIF_CMU_FSYS_BUS, 0, 4),
	DIV(CLK_DOUT_MIF_CMU_FSYS_MMC0, "dout_mif_cmu_fsys_mmc0",
	    "gout_mif_mux_cmu_fsys_mmc0", CLK_CON_DIV_MIF_CMU_FSYS_MMC0, 0, 10),
	DIV(CLK_DOUT_MIF_CMU_FSYS_MMC1, "dout_mif_cmu_fsys_mmc1",
	    "gout_mif_mux_cmu_fsys_mmc1", CLK_CON_DIV_MIF_CMU_FSYS_MMC1, 0, 10),
	DIV(CLK_DOUT_MIF_CMU_FSYS_MMC2, "dout_mif_cmu_fsys_mmc2",
	    "gout_mif_mux_cmu_fsys_mmc2", CLK_CON_DIV_MIF_CMU_FSYS_MMC2, 0, 10),
	DIV(CLK_DOUT_MIF_CMU_FSYS_USB20DRD_REFCLK,
	    "dout_mif_cmu_fsys_usb20drd_refclk",
	    "gout_mif_mux_cmu_fsys_usb20drd_refclk",
	    CLK_CON_DIV_MIF_CMU_FSYS_USB20DRD_REFCLK, 0, 4),
	DIV(CLK_DOUT_MIF_CMU_G3D_SWITCH, "dout_mif_cmu_g3d_switch",
	    "ffac_mif_mux_bus_pll_div2", CLK_CON_DIV_MIF_CMU_G3D_SWITCH, 0, 2),
	DIV(CLK_DOUT_MIF_CMU_ISP_CAM, "dout_mif_cmu_isp_cam",
	    "gout_mif_mux_cmu_isp_cam", CLK_CON_DIV_MIF_CMU_ISP_CAM, 0, 4),
	DIV(CLK_DOUT_MIF_CMU_ISP_ISP, "dout_mif_cmu_isp_isp",
	    "gout_mif_mux_cmu_isp_isp", CLK_CON_DIV_MIF_CMU_ISP_ISP, 0, 4),
	DIV(CLK_DOUT_MIF_CMU_ISP_SENSOR0, "dout_mif_cmu_isp_sensor0",
	    "gout_mif_mux_cmu_isp_sensor0", CLK_CON_DIV_MIF_CMU_ISP_SENSOR0, 0,
	    6),
	DIV(CLK_DOUT_MIF_CMU_ISP_SENSOR1, "dout_mif_cmu_isp_sensor1",
	    "gout_mif_mux_cmu_isp_sensor1", CLK_CON_DIV_MIF_CMU_ISP_SENSOR1, 0,
	    6),
	DIV(CLK_DOUT_MIF_CMU_ISP_SENSOR2, "dout_mif_cmu_isp_sensor2",
	    "gout_mif_mux_cmu_isp_sensor2", CLK_CON_DIV_MIF_CMU_ISP_SENSOR2, 0,
	    6),
	DIV(CLK_DOUT_MIF_CMU_ISP_VRA, "dout_mif_cmu_isp_vra",
	    "gout_mif_mux_cmu_isp_vra", CLK_CON_DIV_MIF_CMU_ISP_VRA, 0, 4),
	DIV(CLK_DOUT_MIF_CMU_MFCMSCL_MFC, "dout_mif_cmu_mfcmscl_mfc",
	    "gout_mif_mux_cmu_mfcmscl_mfc", CLK_CON_DIV_MIF_CMU_MFCMSCL_MFC, 0,
	    4),
	DIV(CLK_DOUT_MIF_CMU_MFCMSCL_MSCL, "dout_mif_cmu_mfcmscl_mscl",
	    "gout_mif_mux_cmu_mfcmscl_mscl", CLK_CON_DIV_MIF_CMU_MFCMSCL_MSCL,
	    0, 4),
	DIV(CLK_DOUT_MIF_CMU_PERI_BUS, "dout_mif_cmu_peri_bus",
	    "gout_mif_mux_cmu_peri_bus", CLK_CON_DIV_MIF_CMU_PERI_BUS, 0, 4),
	DIV(CLK_DOUT_MIF_CMU_PERI_SPI0, "dout_mif_cmu_peri_spi0",
	    "gout_mif_mux_cmu_peri_spi0", CLK_CON_DIV_MIF_CMU_PERI_SPI0, 0, 6),
	DIV(CLK_DOUT_MIF_CMU_PERI_SPI2, "dout_mif_cmu_peri_spi2",
	    "gout_mif_mux_cmu_peri_spi2", CLK_CON_DIV_MIF_CMU_PERI_SPI2, 0, 6),
	DIV(CLK_DOUT_MIF_CMU_PERI_SPI1, "dout_mif_cmu_peri_spi1",
	    "gout_mif_mux_cmu_peri_spi1", CLK_CON_DIV_MIF_CMU_PERI_SPI1, 0, 6),
	DIV(CLK_DOUT_MIF_CMU_PERI_SPI4, "dout_mif_cmu_peri_spi4",
	    "gout_mif_mux_cmu_peri_spi4", CLK_CON_DIV_MIF_CMU_PERI_SPI4, 0, 6),
	DIV(CLK_DOUT_MIF_CMU_PERI_SPI3, "dout_mif_cmu_peri_spi3",
	    "gout_mif_mux_cmu_peri_spi3", CLK_CON_DIV_MIF_CMU_PERI_SPI3, 0, 6),
	DIV(CLK_DOUT_MIF_CMU_PERI_UART1, "dout_mif_cmu_peri_uart1",
	    "gout_mif_mux_cmu_peri_uart1", CLK_CON_DIV_MIF_CMU_PERI_UART1, 0, 4),
	DIV(CLK_DOUT_MIF_CMU_PERI_UART2, "dout_mif_cmu_peri_uart2",
	    "gout_mif_mux_cmu_peri_uart2", CLK_CON_DIV_MIF_CMU_PERI_UART2, 0, 4),
	DIV(CLK_DOUT_MIF_CMU_PERI_UART0, "dout_mif_cmu_peri_uart0",
	    "gout_mif_mux_cmu_peri_uart0", CLK_CON_DIV_MIF_CMU_PERI_UART0, 0, 4),
	DIV(CLK_DOUT_MIF_APB, "dout_mif_apb", "dout_mif_busd",
	    CLK_CON_DIV_MIF_APB, 0, 2),
	DIV(CLK_DOUT_MIF_BUSD, "dout_mif_busd", "gout_mif_mux_busd",
	    CLK_CON_DIV_MIF_BUSD, 0, 4),
	DIV(CLK_DOUT_MIF_HSI2C, "dout_mif_hsi2c", "ffac_mif_mux_media_pll_div2",
	    CLK_CON_DIV_MIF_HSI2C, 0, 4),
};

static const struct samsung_gate_clock mif_gate_clks[] __initconst = {
	GATE(CLK_GOUT_MIF_CMU_DISPAUD_BUS, "gout_mif_cmu_dispaud_bus",
	     "dout_mif_cmu_dispaud_bus", CLK_CON_GAT_MIF_CMU_DISPAUD_BUS, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_CMU_DISPAUD_DECON_ECLK,
	     "gout_mif_cmu_dispaud_decon_eclk",
	     "dout_mif_cmu_dispaud_decon_eclk",
	     CLK_CON_GAT_MIF_CMU_DISPAUD_DECON_ECLK, 0, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_CMU_DISPAUD_DECON_VCLK,
	     "gout_mif_cmu_dispaud_decon_vclk",
	     "dout_mif_cmu_dispaud_decon_vclk",
	     CLK_CON_GAT_MIF_CMU_DISPAUD_DECON_VCLK, 0, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_CMU_FSYS_BUS, "gout_mif_cmu_fsys_bus",
	     "dout_mif_cmu_fsys_bus", CLK_CON_GAT_MIF_CMU_FSYS_BUS, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_CMU_FSYS_MMC0, "gout_mif_cmu_fsys_mmc0",
	     "dout_mif_cmu_fsys_mmc0", CLK_CON_GAT_MIF_CMU_FSYS_MMC0, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_CMU_FSYS_MMC1, "gout_mif_cmu_fsys_mmc1",
	     "dout_mif_cmu_fsys_mmc1", CLK_CON_GAT_MIF_CMU_FSYS_MMC1, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_CMU_FSYS_MMC2, "gout_mif_cmu_fsys_mmc2",
	     "dout_mif_cmu_fsys_mmc2", CLK_CON_GAT_MIF_CMU_FSYS_MMC2, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_CMU_FSYS_USB20DRD_REFCLK,
	     "gout_mif_cmu_fsys_usb20drd_refclk",
	     "dout_mif_cmu_fsys_usb20drd_refclk",
	     CLK_CON_GAT_MIF_CMU_FSYS_USB20DRD_REFCLK, 0, CLK_SET_RATE_PARENT,
	     0),
	GATE(CLK_GOUT_MIF_CMU_G3D_SWITCH, "gout_mif_cmu_g3d_switch",
	     "dout_mif_cmu_g3d_switch", CLK_CON_GAT_MIF_CMU_G3D_SWITCH, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_CMU_ISP_CAM, "gout_mif_cmu_isp_cam",
	     "dout_mif_cmu_isp_cam", CLK_CON_GAT_MIF_CMU_ISP_CAM, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_CMU_ISP_ISP, "gout_mif_cmu_isp_isp",
	     "dout_mif_cmu_isp_isp", CLK_CON_GAT_MIF_CMU_ISP_ISP, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_CMU_ISP_SENSOR0, "gout_mif_cmu_isp_sensor0",
	     "dout_mif_cmu_isp_sensor0", CLK_CON_GAT_MIF_CMU_ISP_SENSOR0, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_CMU_ISP_SENSOR1, "gout_mif_cmu_isp_sensor1",
	     "dout_mif_cmu_isp_sensor1", CLK_CON_GAT_MIF_CMU_ISP_SENSOR1, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_CMU_ISP_SENSOR2, "gout_mif_cmu_isp_sensor2",
	     "dout_mif_cmu_isp_sensor2", CLK_CON_GAT_MIF_CMU_ISP_SENSOR2, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_CMU_ISP_VRA, "gout_mif_cmu_isp_vra",
	     "dout_mif_cmu_isp_vra", CLK_CON_GAT_MIF_CMU_ISP_VRA, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_CMU_MFCMSCL_MFC, "gout_mif_cmu_mfcmscl_mfc",
	     "dout_mif_cmu_mfcmscl_mfc", CLK_CON_GAT_MIF_CMU_MFCMSCL_MFC, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_CMU_MFCMSCL_MSCL, "gout_mif_cmu_mfcmscl_mscl",
	     "dout_mif_cmu_mfcmscl_mscl", CLK_CON_GAT_MIF_CMU_MFCMSCL_MSCL, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_CMU_PERI_BUS, "gout_mif_cmu_peri_bus",
	     "dout_mif_cmu_peri_bus", CLK_CON_GAT_MIF_CMU_PERI_BUS, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_CMU_PERI_SPI0, "gout_mif_cmu_peri_spi0",
	     "dout_mif_cmu_peri_spi0", CLK_CON_GAT_MIF_CMU_PERI_SPI0, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_CMU_PERI_SPI2, "gout_mif_cmu_peri_spi2",
	     "dout_mif_cmu_peri_spi2", CLK_CON_GAT_MIF_CMU_PERI_SPI2, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_CMU_PERI_SPI1, "gout_mif_cmu_peri_spi1",
	     "dout_mif_cmu_peri_spi1", CLK_CON_GAT_MIF_CMU_PERI_SPI1, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_CMU_PERI_SPI4, "gout_mif_cmu_peri_spi4",
	     "dout_mif_cmu_peri_spi4", CLK_CON_GAT_MIF_CMU_PERI_SPI4, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_CMU_PERI_SPI3, "gout_mif_cmu_peri_spi3",
	     "dout_mif_cmu_peri_spi3", CLK_CON_GAT_MIF_CMU_PERI_SPI3, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_CMU_PERI_UART1, "gout_mif_cmu_peri_uart1",
	     "dout_mif_cmu_peri_uart1", CLK_CON_GAT_MIF_CMU_PERI_UART1, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_CMU_PERI_UART2, "gout_mif_cmu_peri_uart2",
	     "dout_mif_cmu_peri_uart2", CLK_CON_GAT_MIF_CMU_PERI_UART2, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_CMU_PERI_UART0, "gout_mif_cmu_peri_uart0",
	     "dout_mif_cmu_peri_uart0", CLK_CON_GAT_MIF_CMU_PERI_UART0, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_HSI2C_AP_PCLKM, "gout_mif_hsi2c_ap_pclkm",
	     "dout_mif_hsi2c", CLK_CON_GAT_MIF_HSI2C_AP_PCLKM, 0,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_HSI2C_AP_PCLKS, "gout_mif_hsi2c_ap_pclks",
	     "dout_mif_apb", CLK_CON_GAT_MIF_HSI2C_AP_PCLKS, 14, CLK_IS_CRITICAL
	     | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_HSI2C_CP_PCLKM, "gout_mif_hsi2c_cp_pclkm",
	     "dout_mif_hsi2c", CLK_CON_GAT_MIF_HSI2C_CP_PCLKM, 1,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_HSI2C_CP_PCLKS, "gout_mif_hsi2c_cp_pclks",
	     "dout_mif_apb", CLK_CON_GAT_MIF_HSI2C_CP_PCLKS, 15, CLK_IS_CRITICAL
	     | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_HSI2C_IPCLK, "gout_mif_hsi2c_ipclk", "dout_mif_hsi2c",
	     CLK_CON_GAT_MIF_HSI2C_IPCLK, 2, CLK_IS_CRITICAL |
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_HSI2C_ITCLK, "gout_mif_hsi2c_itclk", "dout_mif_hsi2c",
	     CLK_CON_GAT_MIF_HSI2C_ITCLK, 3, CLK_IS_CRITICAL |
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_CP_PCLK_HSI2C, "gout_mif_cp_pclk_hsi2c",
	     "dout_mif_hsi2c", CLK_CON_GAT_MIF_CP_PCLK_HSI2C, 6, CLK_IS_CRITICAL
	     | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_CP_PCLK_HSI2C_BAT_0, "gout_mif_cp_pclk_hsi2c_bat_0",
	     "dout_mif_hsi2c", CLK_CON_GAT_MIF_CP_PCLK_HSI2C_BAT_0, 4,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_CP_PCLK_HSI2C_BAT_1, "gout_mif_cp_pclk_hsi2c_bat_1",
	     "dout_mif_hsi2c", CLK_CON_GAT_MIF_CP_PCLK_HSI2C_BAT_1, 5,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_WRAP_ADC_IF_OSC_SYS, "gout_mif_wrap_adc_if_osc_sys",
	     "oscclk", CLK_CON_GAT_MIF_WRAP_ADC_IF_OSC_SYS, 3,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_WRAP_ADC_IF_PCLK_S0, "gout_mif_wrap_adc_if_pclk_s0",
	     "dout_mif_apb", CLK_CON_GAT_MIF_WRAP_ADC_IF_PCLK_S0, 20,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_WRAP_ADC_IF_PCLK_S1, "gout_mif_wrap_adc_if_pclk_s1",
	     "dout_mif_apb", CLK_CON_GAT_MIF_WRAP_ADC_IF_PCLK_S1, 21,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_MUX_BUS_PLL, "gout_mif_mux_bus_pll",
	     "gout_mif_mux_bus_pll_con", CLK_CON_GAT_MIF_MUX_BUS_PLL, 21,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_MUX_BUS_PLL_CON, "gout_mif_mux_bus_pll_con",
	     "fout_mif_bus_pll", CLK_CON_GAT_MIF_MUX_BUS_PLL_CON, 12,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_MUX_CMU_DISPAUD_BUS, "gout_mif_mux_cmu_dispaud_bus",
	     "mout_mif_cmu_dispaud_bus", CLK_CON_GAT_MIF_MUX_CMU_DISPAUD_BUS,
	     21, CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_MUX_CMU_DISPAUD_DECON_ECLK,
	     "gout_mif_mux_cmu_dispaud_decon_eclk",
	     "mout_mif_cmu_dispaud_decon_eclk",
	     CLK_CON_GAT_MIF_MUX_CMU_DISPAUD_DECON_ECLK, 21, CLK_IS_CRITICAL |
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_MUX_CMU_DISPAUD_DECON_VCLK,
	     "gout_mif_mux_cmu_dispaud_decon_vclk",
	     "mout_mif_cmu_dispaud_decon_vclk",
	     CLK_CON_GAT_MIF_MUX_CMU_DISPAUD_DECON_VCLK, 21, CLK_IS_CRITICAL |
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_MUX_CMU_FSYS_BUS, "gout_mif_mux_cmu_fsys_bus",
	     "mout_mif_cmu_fsys_bus", CLK_CON_GAT_MIF_MUX_CMU_FSYS_BUS, 21,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_MUX_CMU_FSYS_MMC0, "gout_mif_mux_cmu_fsys_mmc0",
	     "mout_mif_cmu_fsys_mmc0", CLK_CON_GAT_MIF_MUX_CMU_FSYS_MMC0, 21,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_MUX_CMU_FSYS_MMC1, "gout_mif_mux_cmu_fsys_mmc1",
	     "mout_mif_cmu_fsys_mmc1", CLK_CON_GAT_MIF_MUX_CMU_FSYS_MMC1, 21,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_MUX_CMU_FSYS_MMC2, "gout_mif_mux_cmu_fsys_mmc2",
	     "mout_mif_cmu_fsys_mmc2", CLK_CON_GAT_MIF_MUX_CMU_FSYS_MMC2, 21,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_MUX_CMU_FSYS_USB20DRD_REFCLK,
	     "gout_mif_mux_cmu_fsys_usb20drd_refclk",
	     "mout_mif_cmu_fsys_usb20drd_refclk",
	     CLK_CON_GAT_MIF_MUX_CMU_FSYS_USB20DRD_REFCLK, 21, CLK_IS_CRITICAL |
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_MUX_CMU_ISP_CAM, "gout_mif_mux_cmu_isp_cam",
	     "mout_mif_cmu_isp_cam", CLK_CON_GAT_MIF_MUX_CMU_ISP_CAM, 21,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_MUX_CMU_ISP_ISP, "gout_mif_mux_cmu_isp_isp",
	     "mout_mif_cmu_isp_isp", CLK_CON_GAT_MIF_MUX_CMU_ISP_ISP, 21,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_MUX_CMU_ISP_SENSOR0, "gout_mif_mux_cmu_isp_sensor0",
	     "mout_mif_cmu_isp_sensor0", CLK_CON_GAT_MIF_MUX_CMU_ISP_SENSOR0,
	     21, CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_MUX_CMU_ISP_SENSOR1, "gout_mif_mux_cmu_isp_sensor1",
	     "mout_mif_cmu_isp_sensor1", CLK_CON_GAT_MIF_MUX_CMU_ISP_SENSOR1,
	     21, CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_MUX_CMU_ISP_SENSOR2, "gout_mif_mux_cmu_isp_sensor2",
	     "mout_mif_cmu_isp_sensor2", CLK_CON_GAT_MIF_MUX_CMU_ISP_SENSOR2,
	     21, CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_MUX_CMU_ISP_VRA, "gout_mif_mux_cmu_isp_vra",
	     "mout_mif_cmu_isp_vra", CLK_CON_GAT_MIF_MUX_CMU_ISP_VRA, 21,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_MUX_CMU_MFCMSCL_MFC, "gout_mif_mux_cmu_mfcmscl_mfc",
	     "mout_mif_cmu_mfcmscl_mfc", CLK_CON_GAT_MIF_MUX_CMU_MFCMSCL_MFC,
	     21, CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_MUX_CMU_MFCMSCL_MSCL, "gout_mif_mux_cmu_mfcmscl_mscl",
	     "mout_mif_cmu_mfcmscl_mscl", CLK_CON_GAT_MIF_MUX_CMU_MFCMSCL_MSCL,
	     21, CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_MUX_CMU_PERI_BUS, "gout_mif_mux_cmu_peri_bus",
	     "mout_mif_cmu_peri_bus", CLK_CON_GAT_MIF_MUX_CMU_PERI_BUS, 21,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_MUX_CMU_PERI_SPI0, "gout_mif_mux_cmu_peri_spi0",
	     "mout_mif_cmu_peri_spi0", CLK_CON_GAT_MIF_MUX_CMU_PERI_SPI0, 21,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_MUX_CMU_PERI_SPI2, "gout_mif_mux_cmu_peri_spi2",
	     "mout_mif_cmu_peri_spi2", CLK_CON_GAT_MIF_MUX_CMU_PERI_SPI2, 21,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_MUX_CMU_PERI_SPI1, "gout_mif_mux_cmu_peri_spi1",
	     "mout_mif_cmu_peri_spi1", CLK_CON_GAT_MIF_MUX_CMU_PERI_SPI1, 21,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_MUX_CMU_PERI_SPI4, "gout_mif_mux_cmu_peri_spi4",
	     "mout_mif_cmu_peri_spi4", CLK_CON_GAT_MIF_MUX_CMU_PERI_SPI4, 21,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_MUX_CMU_PERI_SPI3, "gout_mif_mux_cmu_peri_spi3",
	     "mout_mif_cmu_peri_spi3", CLK_CON_GAT_MIF_MUX_CMU_PERI_SPI3, 21,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_MUX_CMU_PERI_UART1, "gout_mif_mux_cmu_peri_uart1",
	     "mout_mif_cmu_peri_uart1", CLK_CON_GAT_MIF_MUX_CMU_PERI_UART1, 21,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_MUX_CMU_PERI_UART2, "gout_mif_mux_cmu_peri_uart2",
	     "mout_mif_cmu_peri_uart2", CLK_CON_GAT_MIF_MUX_CMU_PERI_UART2, 21,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_MUX_CMU_PERI_UART0, "gout_mif_mux_cmu_peri_uart0",
	     "mout_mif_cmu_peri_uart0", CLK_CON_GAT_MIF_MUX_CMU_PERI_UART0, 21,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_MUX_BUSD, "gout_mif_mux_busd", "mout_mif_busd",
	     CLK_CON_GAT_MIF_MUX_BUSD, 21, CLK_IS_CRITICAL |
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_MUX_MEDIA_PLL, "gout_mif_mux_media_pll",
	     "gout_mif_mux_media_pll_con", CLK_CON_GAT_MIF_MUX_MEDIA_PLL, 21,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_MUX_MEDIA_PLL_CON, "gout_mif_mux_media_pll_con",
	     "fout_mif_media_pll", CLK_CON_GAT_MIF_MUX_MEDIA_PLL_CON, 12,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_MUX_MEM_PLL, "gout_mif_mux_mem_pll",
	     "gout_mif_mux_mem_pll_con", CLK_CON_GAT_MIF_MUX_MEM_PLL, 21,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MIF_MUX_MEM_PLL_CON, "gout_mif_mux_mem_pll_con",
	     "fout_mif_mem_pll", CLK_CON_GAT_MIF_MUX_MEM_PLL_CON, 12,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
};

static const struct samsung_cmu_info mif_cmu_info __initconst = {
	.fixed_factor_clks	= mif_fixed_factor_clks,
	.nr_fixed_factor_clks	= ARRAY_SIZE(mif_fixed_factor_clks),
	.pll_clks		= mif_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(mif_pll_clks),
	.mux_clks		= mif_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(mif_mux_clks),
	.div_clks		= mif_div_clks,
	.nr_div_clks		= ARRAY_SIZE(mif_div_clks),
	.gate_clks		= mif_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(mif_gate_clks),
	.clk_regs		= mif_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(mif_clk_regs),
	.nr_clk_ids		= MIF_NR_CLK,
};

/*
 * Register offsets for CMU_DISPAUD (0x148d0000)
 */
#define PLL_LOCKTIME_DISPAUD_PLL				0x0000
#define PLL_LOCKTIME_DISPAUD_AUD_PLL				0x00c0
#define PLL_CON0_DISPAUD_PLL					0x0100
#define PLL_CON0_DISPAUD_AUD_PLL				0x01c0
#define CLK_CON_GAT_DISPAUD_MUX_PLL				0x0200
#define CLK_CON_GAT_DISPAUD_MUX_PLL_CON				0x0200
#define CLK_CON_GAT_DISPAUD_MUX_AUD_PLL				0x0204
#define CLK_CON_GAT_DISPAUD_MUX_AUD_PLL_CON			0x0204
#define CLK_CON_GAT_DISPAUD_MUX_BUS_USER			0x0210
#define CLK_CON_MUX_DISPAUD_BUS_USER				0x0210
#define CLK_CON_GAT_DISPAUD_MUX_DECON_VCLK_USER			0x0214
#define CLK_CON_MUX_DISPAUD_DECON_VCLK_USER			0x0214
#define CLK_CON_GAT_DISPAUD_MUX_DECON_ECLK_USER			0x0218
#define CLK_CON_MUX_DISPAUD_DECON_ECLK_USER			0x0218
#define CLK_CON_GAT_DISPAUD_MUX_DECON_VCLK			0x021c
#define CLK_CON_MUX_DISPAUD_DECON_VCLK				0x021c
#define CLK_CON_GAT_DISPAUD_MUX_DECON_ECLK			0x0220
#define CLK_CON_MUX_DISPAUD_DECON_ECLK				0x0220
#define CLK_CON_GAT_DISPAUD_MUX_MIPIPHY_TXBYTECLKHS_USER	0x0224
#define CLK_CON_GAT_DISPAUD_MUX_MIPIPHY_TXBYTECLKHS_USER_CON	0x0224
#define CLK_CON_GAT_DISPAUD_MUX_MIPIPHY_RXCLKESC0_USER		0x0228
#define CLK_CON_GAT_DISPAUD_MUX_MIPIPHY_RXCLKESC0_USER_CON	0x0228
#define CLK_CON_GAT_DISPAUD_MUX_MI2S				0x022c
#define CLK_CON_MUX_DISPAUD_MI2S				0x022c
#define CLK_CON_DIV_DISPAUD_APB					0x0400
#define CLK_CON_DIV_DISPAUD_DECON_VCLK				0x0404
#define CLK_CON_DIV_DISPAUD_DECON_ECLK				0x0408
#define CLK_CON_DIV_DISPAUD_MI2S				0x040c
#define CLK_CON_DIV_DISPAUD_MIXER				0x0410
#define CLK_CON_GAT_DISPAUD_BUS					0x0810
#define CLK_CON_GAT_DISPAUD_BUS_DISP				0x0810
#define CLK_CON_GAT_DISPAUD_BUS_PPMU				0x0810
#define CLK_CON_GAT_DISPAUD_APB_AUD				0x0814
#define CLK_CON_GAT_DISPAUD_APB_AUD_AMP				0x0814
#define CLK_CON_GAT_DISPAUD_APB_DISP				0x0814
#define CLK_CON_GAT_DISPAUD_DECON_VCLK				0x081c
#define CLK_CON_GAT_DISPAUD_DECON_ECLK				0x0820
#define CLK_CON_GAT_DISPAUD_MI2S_AMP_I2SCODCLKI			0x082c
#define CLK_CON_GAT_DISPAUD_MI2S_AUD_I2SCODCLKI			0x082c
#define CLK_CON_GAT_DISPAUD_MIXER_AUD_SYSCLK			0x0830
#define CLK_CON_GAT_DISPAUD_CON_EXT2AUD_BCK_GPIO_I2S		0x0834
#define CLK_CON_GAT_DISPAUD_CON_AUD_I2S_BCLK_BT_IN		0x0838
#define CLK_CON_GAT_DISPAUD_CON_CP2AUD_BCK			0x083c
#define CLK_CON_GAT_DISPAUD_CON_AUD_I2S_BCLK_FM_IN		0x0840

static const unsigned long dispaud_clk_regs[] __initconst = {
	PLL_LOCKTIME_DISPAUD_PLL,
	PLL_LOCKTIME_DISPAUD_AUD_PLL,
	PLL_CON0_DISPAUD_PLL,
	PLL_CON0_DISPAUD_AUD_PLL,
	CLK_CON_GAT_DISPAUD_MUX_PLL,
	CLK_CON_GAT_DISPAUD_MUX_PLL_CON,
	CLK_CON_GAT_DISPAUD_MUX_AUD_PLL,
	CLK_CON_GAT_DISPAUD_MUX_AUD_PLL_CON,
	CLK_CON_GAT_DISPAUD_MUX_BUS_USER,
	CLK_CON_MUX_DISPAUD_BUS_USER,
	CLK_CON_GAT_DISPAUD_MUX_DECON_VCLK_USER,
	CLK_CON_MUX_DISPAUD_DECON_VCLK_USER,
	CLK_CON_GAT_DISPAUD_MUX_DECON_ECLK_USER,
	CLK_CON_MUX_DISPAUD_DECON_ECLK_USER,
	CLK_CON_GAT_DISPAUD_MUX_DECON_VCLK,
	CLK_CON_MUX_DISPAUD_DECON_VCLK,
	CLK_CON_GAT_DISPAUD_MUX_DECON_ECLK,
	CLK_CON_MUX_DISPAUD_DECON_ECLK,
	CLK_CON_GAT_DISPAUD_MUX_MIPIPHY_TXBYTECLKHS_USER,
	CLK_CON_GAT_DISPAUD_MUX_MIPIPHY_TXBYTECLKHS_USER_CON,
	CLK_CON_GAT_DISPAUD_MUX_MIPIPHY_RXCLKESC0_USER,
	CLK_CON_GAT_DISPAUD_MUX_MIPIPHY_RXCLKESC0_USER_CON,
	CLK_CON_GAT_DISPAUD_MUX_MI2S,
	CLK_CON_MUX_DISPAUD_MI2S,
	CLK_CON_DIV_DISPAUD_APB,
	CLK_CON_DIV_DISPAUD_DECON_VCLK,
	CLK_CON_DIV_DISPAUD_DECON_ECLK,
	CLK_CON_DIV_DISPAUD_MI2S,
	CLK_CON_DIV_DISPAUD_MIXER,
	CLK_CON_GAT_DISPAUD_BUS,
	CLK_CON_GAT_DISPAUD_BUS_DISP,
	CLK_CON_GAT_DISPAUD_BUS_PPMU,
	CLK_CON_GAT_DISPAUD_APB_AUD,
	CLK_CON_GAT_DISPAUD_APB_AUD_AMP,
	CLK_CON_GAT_DISPAUD_APB_DISP,
	CLK_CON_GAT_DISPAUD_DECON_VCLK,
	CLK_CON_GAT_DISPAUD_DECON_ECLK,
	CLK_CON_GAT_DISPAUD_MI2S_AMP_I2SCODCLKI,
	CLK_CON_GAT_DISPAUD_MI2S_AUD_I2SCODCLKI,
	CLK_CON_GAT_DISPAUD_MIXER_AUD_SYSCLK,
	CLK_CON_GAT_DISPAUD_CON_EXT2AUD_BCK_GPIO_I2S,
	CLK_CON_GAT_DISPAUD_CON_AUD_I2S_BCLK_BT_IN,
	CLK_CON_GAT_DISPAUD_CON_CP2AUD_BCK,
	CLK_CON_GAT_DISPAUD_CON_AUD_I2S_BCLK_FM_IN,
};

static const struct samsung_fixed_rate_clock dispaud_fixed_clks[] __initconst = {
	FRATE(0, "frat_dispaud_audiocdclk0", NULL, 0, 100000000),
	FRATE(0, "frat_dispaud_mixer_bclk_bt", NULL, 0, 12500000),
	FRATE(0, "frat_dispaud_mixer_bclk_cp", NULL, 0, 12500000),
	FRATE(0, "frat_dispaud_mixer_bclk_fm", NULL, 0, 12500000),
	FRATE(0, "frat_dispaud_mixer_sclk_ap", NULL, 0, 12500000),
	FRATE(0, "frat_dispaud_mipiphy_rxclkesc0", NULL, 0, 188000000),
	FRATE(0, "frat_dispaud_mipiphy_txbyteclkhs", NULL, 0, 188000000),
};

static const struct samsung_pll_clock dispaud_pll_clks[] __initconst = {
	PLL(pll_1417x, CLK_FOUT_DISPAUD_AUD_PLL, "fout_dispaud_aud_pll",
	    "oscclk", PLL_LOCKTIME_DISPAUD_AUD_PLL, PLL_CON0_DISPAUD_AUD_PLL,
	    NULL),
	PLL(pll_1417x, CLK_FOUT_DISPAUD_PLL, "fout_dispaud_pll", "oscclk",
	    PLL_LOCKTIME_DISPAUD_PLL, PLL_CON0_DISPAUD_PLL, NULL),
};

/* List of parent clocks for muxes in CMU_DISPAUD */
PNAME(mout_dispaud_bus_user_p)		= { "oscclk", "gout_mif_cmu_dispaud_bus" };
PNAME(mout_dispaud_decon_eclk_user_p)	= { "oscclk",
					    "gout_mif_cmu_dispaud_decon_eclk" };
PNAME(mout_dispaud_decon_vclk_user_p)	= { "oscclk",
					    "gout_mif_cmu_dispaud_decon_vclk" };
PNAME(mout_dispaud_decon_eclk_p)	= { "gout_dispaud_mux_decon_eclk_user",
					    "gout_dispaud_mux_pll_con" };
PNAME(mout_dispaud_decon_vclk_p)	= { "gout_dispaud_mux_decon_vclk_user",
					    "gout_dispaud_mux_pll_con" };
PNAME(mout_dispaud_mi2s_p)		= { "gout_dispaud_mux_aud_pll_con",
					    "frat_dispaud_audiocdclk0" };

static const struct samsung_mux_clock dispaud_mux_clks[] __initconst = {
	MUX(CLK_MOUT_DISPAUD_BUS_USER, "mout_dispaud_bus_user",
	    mout_dispaud_bus_user_p, CLK_CON_MUX_DISPAUD_BUS_USER, 12, 1),
	MUX(CLK_MOUT_DISPAUD_DECON_ECLK_USER, "mout_dispaud_decon_eclk_user",
	    mout_dispaud_decon_eclk_user_p, CLK_CON_MUX_DISPAUD_DECON_ECLK_USER,
	    12, 1),
	MUX(CLK_MOUT_DISPAUD_DECON_VCLK_USER, "mout_dispaud_decon_vclk_user",
	    mout_dispaud_decon_vclk_user_p, CLK_CON_MUX_DISPAUD_DECON_VCLK_USER,
	    12, 1),
	MUX(CLK_MOUT_DISPAUD_DECON_ECLK, "mout_dispaud_decon_eclk",
	    mout_dispaud_decon_eclk_p, CLK_CON_MUX_DISPAUD_DECON_ECLK, 12, 1),
	MUX(CLK_MOUT_DISPAUD_DECON_VCLK, "mout_dispaud_decon_vclk",
	    mout_dispaud_decon_vclk_p, CLK_CON_MUX_DISPAUD_DECON_VCLK, 12, 1),
	MUX(CLK_MOUT_DISPAUD_MI2S, "mout_dispaud_mi2s", mout_dispaud_mi2s_p,
	    CLK_CON_MUX_DISPAUD_MI2S, 12, 1),
};

static const struct samsung_div_clock dispaud_div_clks[] __initconst = {
	DIV(CLK_DOUT_DISPAUD_APB, "dout_dispaud_apb",
	    "gout_dispaud_mux_bus_user", CLK_CON_DIV_DISPAUD_APB, 0, 2),
	DIV(CLK_DOUT_DISPAUD_DECON_ECLK, "dout_dispaud_decon_eclk",
	    "gout_dispaud_mux_decon_eclk", CLK_CON_DIV_DISPAUD_DECON_ECLK, 0, 3),
	DIV(CLK_DOUT_DISPAUD_DECON_VCLK, "dout_dispaud_decon_vclk",
	    "gout_dispaud_mux_decon_vclk", CLK_CON_DIV_DISPAUD_DECON_VCLK, 0, 3),
	DIV(CLK_DOUT_DISPAUD_MI2S, "dout_dispaud_mi2s", "gout_dispaud_mux_mi2s",
	    CLK_CON_DIV_DISPAUD_MI2S, 0, 4),
	DIV(CLK_DOUT_DISPAUD_MIXER, "dout_dispaud_mixer",
	    "gout_dispaud_mux_aud_pll_con", CLK_CON_DIV_DISPAUD_MIXER, 0, 4),
};

static const struct samsung_gate_clock dispaud_gate_clks[] __initconst = {
	GATE(CLK_GOUT_DISPAUD_BUS, "gout_dispaud_bus",
	     "gout_dispaud_mux_bus_user", CLK_CON_GAT_DISPAUD_BUS, 0,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_DISPAUD_BUS_DISP, "gout_dispaud_bus_disp",
	     "gout_dispaud_mux_bus_user", CLK_CON_GAT_DISPAUD_BUS_DISP, 2,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_DISPAUD_BUS_PPMU, "gout_dispaud_bus_ppmu",
	     "gout_dispaud_mux_bus_user", CLK_CON_GAT_DISPAUD_BUS_PPMU, 3,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_DISPAUD_APB_AUD, "gout_dispaud_apb_aud",
	     "dout_dispaud_apb", CLK_CON_GAT_DISPAUD_APB_AUD, 2,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_DISPAUD_APB_AUD_AMP, "gout_dispaud_apb_aud_amp",
	     "dout_dispaud_apb", CLK_CON_GAT_DISPAUD_APB_AUD_AMP, 3,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_DISPAUD_APB_DISP, "gout_dispaud_apb_disp",
	     "dout_dispaud_apb", CLK_CON_GAT_DISPAUD_APB_DISP, 1,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_DISPAUD_CON_AUD_I2S_BCLK_BT_IN,
	     "gout_dispaud_con_aud_i2s_bclk_bt_in",
	     "frat_dispaud_mixer_bclk_bt",
	     CLK_CON_GAT_DISPAUD_CON_AUD_I2S_BCLK_BT_IN, 0, CLK_SET_RATE_PARENT,
	     0),
	GATE(CLK_GOUT_DISPAUD_CON_AUD_I2S_BCLK_FM_IN,
	     "gout_dispaud_con_aud_i2s_bclk_fm_in",
	     "frat_dispaud_mixer_bclk_fm",
	     CLK_CON_GAT_DISPAUD_CON_AUD_I2S_BCLK_FM_IN, 0, CLK_SET_RATE_PARENT,
	     0),
	GATE(CLK_GOUT_DISPAUD_CON_CP2AUD_BCK, "gout_dispaud_con_cp2aud_bck",
	     "frat_dispaud_mixer_bclk_cp", CLK_CON_GAT_DISPAUD_CON_CP2AUD_BCK,
	     0, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_DISPAUD_CON_EXT2AUD_BCK_GPIO_I2S,
	     "gout_dispaud_con_ext2aud_bck_gpio_i2s",
	     "frat_dispaud_mixer_sclk_ap",
	     CLK_CON_GAT_DISPAUD_CON_EXT2AUD_BCK_GPIO_I2S, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_DISPAUD_DECON_ECLK, "gout_dispaud_decon_eclk",
	     "dout_dispaud_decon_eclk", CLK_CON_GAT_DISPAUD_DECON_ECLK, 0,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_DISPAUD_DECON_VCLK, "gout_dispaud_decon_vclk",
	     "dout_dispaud_decon_vclk", CLK_CON_GAT_DISPAUD_DECON_VCLK, 0,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_DISPAUD_MI2S_AMP_I2SCODCLKI,
	     "gout_dispaud_mi2s_amp_i2scodclki", "dout_dispaud_mi2s",
	     CLK_CON_GAT_DISPAUD_MI2S_AMP_I2SCODCLKI, 1, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_DISPAUD_MI2S_AUD_I2SCODCLKI,
	     "gout_dispaud_mi2s_aud_i2scodclki", "dout_dispaud_mi2s",
	     CLK_CON_GAT_DISPAUD_MI2S_AUD_I2SCODCLKI, 0, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_DISPAUD_MIXER_AUD_SYSCLK, "gout_dispaud_mixer_aud_sysclk",
	     "dout_dispaud_mixer", CLK_CON_GAT_DISPAUD_MIXER_AUD_SYSCLK, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_DISPAUD_MUX_AUD_PLL, "gout_dispaud_mux_aud_pll",
	     "gout_dispaud_mux_aud_pll_con", CLK_CON_GAT_DISPAUD_MUX_AUD_PLL,
	     21, CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_DISPAUD_MUX_AUD_PLL_CON, "gout_dispaud_mux_aud_pll_con",
	     "fout_dispaud_aud_pll", CLK_CON_GAT_DISPAUD_MUX_AUD_PLL_CON, 12,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_DISPAUD_MUX_BUS_USER, "gout_dispaud_mux_bus_user",
	     "mout_dispaud_bus_user", CLK_CON_GAT_DISPAUD_MUX_BUS_USER, 21,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_DISPAUD_MUX_DECON_ECLK_USER,
	     "gout_dispaud_mux_decon_eclk_user", "mout_dispaud_decon_eclk_user",
	     CLK_CON_GAT_DISPAUD_MUX_DECON_ECLK_USER, 21, CLK_IS_CRITICAL |
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_DISPAUD_MUX_DECON_VCLK_USER,
	     "gout_dispaud_mux_decon_vclk_user", "mout_dispaud_decon_vclk_user",
	     CLK_CON_GAT_DISPAUD_MUX_DECON_VCLK_USER, 21, CLK_IS_CRITICAL |
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_DISPAUD_MUX_MIPIPHY_RXCLKESC0_USER,
	     "gout_dispaud_mux_mipiphy_rxclkesc0_user",
	     "gout_dispaud_mux_mipiphy_rxclkesc0_user_con",
	     CLK_CON_GAT_DISPAUD_MUX_MIPIPHY_RXCLKESC0_USER, 21, CLK_IS_CRITICAL
	     | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_DISPAUD_MUX_MIPIPHY_RXCLKESC0_USER_CON,
	     "gout_dispaud_mux_mipiphy_rxclkesc0_user_con",
	     "frat_dispaud_mipiphy_rxclkesc0",
	     CLK_CON_GAT_DISPAUD_MUX_MIPIPHY_RXCLKESC0_USER_CON, 12,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_DISPAUD_MUX_MIPIPHY_TXBYTECLKHS_USER,
	     "gout_dispaud_mux_mipiphy_txbyteclkhs_user",
	     "gout_dispaud_mux_mipiphy_txbyteclkhs_user_con",
	     CLK_CON_GAT_DISPAUD_MUX_MIPIPHY_TXBYTECLKHS_USER, 21,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_DISPAUD_MUX_MIPIPHY_TXBYTECLKHS_USER_CON,
	     "gout_dispaud_mux_mipiphy_txbyteclkhs_user_con",
	     "frat_dispaud_mipiphy_txbyteclkhs",
	     CLK_CON_GAT_DISPAUD_MUX_MIPIPHY_TXBYTECLKHS_USER_CON, 12,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_DISPAUD_MUX_DECON_ECLK, "gout_dispaud_mux_decon_eclk",
	     "mout_dispaud_decon_eclk", CLK_CON_GAT_DISPAUD_MUX_DECON_ECLK, 21,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_DISPAUD_MUX_DECON_VCLK, "gout_dispaud_mux_decon_vclk",
	     "mout_dispaud_decon_vclk", CLK_CON_GAT_DISPAUD_MUX_DECON_VCLK, 21,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_DISPAUD_MUX_MI2S, "gout_dispaud_mux_mi2s",
	     "mout_dispaud_mi2s", CLK_CON_GAT_DISPAUD_MUX_MI2S, 21,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_DISPAUD_MUX_PLL, "gout_dispaud_mux_pll",
	     "gout_dispaud_mux_pll_con", CLK_CON_GAT_DISPAUD_MUX_PLL, 21,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_DISPAUD_MUX_PLL_CON, "gout_dispaud_mux_pll_con",
	     "fout_dispaud_pll", CLK_CON_GAT_DISPAUD_MUX_PLL_CON, 12,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
};

static const struct samsung_cmu_info dispaud_cmu_info __initconst = {
	.fixed_clks	= dispaud_fixed_clks,
	.nr_fixed_clks	= ARRAY_SIZE(dispaud_fixed_clks),
	.pll_clks		= dispaud_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(dispaud_pll_clks),
	.mux_clks		= dispaud_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(dispaud_mux_clks),
	.div_clks		= dispaud_div_clks,
	.nr_div_clks		= ARRAY_SIZE(dispaud_div_clks),
	.gate_clks		= dispaud_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(dispaud_gate_clks),
	.clk_regs		= dispaud_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(dispaud_clk_regs),
	.nr_clk_ids		= DISPAUD_NR_CLK,
};

/*
 * Register offsets for CMU_FSYS (0x13730000)
 */
#define PLL_LOCKTIME_FSYS_USB_PLL			0x0000
#define PLL_CON0_FSYS_USB_PLL				0x0100
#define CLK_CON_GAT_FSYS_MUX_USB_PLL			0x0200
#define CLK_CON_GAT_FSYS_MUX_USB_PLL_CON		0x0200
#define CLK_CON_GAT_FSYS_MUX_USB20DRD_PHYCLOCK_USER	0x0230
#define CLK_CON_GAT_FSYS_MUX_USB20DRD_PHYCLOCK_USER_CON	0x0230
#define CLK_CON_GAT_FSYS_BUSP3_HCLK			0x0804
#define CLK_CON_GAT_FSYS_MMC0_ACLK			0x0804
#define CLK_CON_GAT_FSYS_MMC1_ACLK			0x0804
#define CLK_CON_GAT_FSYS_MMC2_ACLK			0x0804
#define CLK_CON_GAT_FSYS_PDMA0_ACLK_PDMA0		0x0804
#define CLK_CON_GAT_FSYS_PPMU_ACLK			0x0804
#define CLK_CON_GAT_FSYS_PPMU_PCLK			0x0804
#define CLK_CON_GAT_FSYS_SROMC_HCLK			0x0804
#define CLK_CON_GAT_FSYS_UPSIZER_BUS1_ACLK		0x0804
#define CLK_CON_GAT_FSYS_USB20DRD_ACLK_HSDRD		0x0804
#define CLK_CON_GAT_FSYS_USB20DRD_HCLK_USB20_CTRL	0x0804
#define CLK_CON_GAT_FSYS_USB20DRD_HSDRD_REF_CLK		0x0828

static const unsigned long fsys_clk_regs[] __initconst = {
	PLL_LOCKTIME_FSYS_USB_PLL,
	PLL_CON0_FSYS_USB_PLL,
	CLK_CON_GAT_FSYS_MUX_USB_PLL,
	CLK_CON_GAT_FSYS_MUX_USB_PLL_CON,
	CLK_CON_GAT_FSYS_MUX_USB20DRD_PHYCLOCK_USER,
	CLK_CON_GAT_FSYS_MUX_USB20DRD_PHYCLOCK_USER_CON,
	CLK_CON_GAT_FSYS_BUSP3_HCLK,
	CLK_CON_GAT_FSYS_MMC0_ACLK,
	CLK_CON_GAT_FSYS_MMC1_ACLK,
	CLK_CON_GAT_FSYS_MMC2_ACLK,
	CLK_CON_GAT_FSYS_PDMA0_ACLK_PDMA0,
	CLK_CON_GAT_FSYS_PPMU_ACLK,
	CLK_CON_GAT_FSYS_PPMU_PCLK,
	CLK_CON_GAT_FSYS_SROMC_HCLK,
	CLK_CON_GAT_FSYS_UPSIZER_BUS1_ACLK,
	CLK_CON_GAT_FSYS_USB20DRD_ACLK_HSDRD,
	CLK_CON_GAT_FSYS_USB20DRD_HCLK_USB20_CTRL,
	CLK_CON_GAT_FSYS_USB20DRD_HSDRD_REF_CLK,
};

static const struct samsung_fixed_rate_clock fsys_fixed_clks[] __initconst = {
	FRATE(0, "frat_fsys_usb20drd_phyclock", NULL, 0, 60000000),
};

static const struct samsung_pll_clock fsys_pll_clks[] __initconst = {
	PLL(pll_1417x, CLK_FOUT_FSYS_USB_PLL, "fout_fsys_usb_pll", "oscclk",
	    PLL_LOCKTIME_FSYS_USB_PLL, PLL_CON0_FSYS_USB_PLL, NULL),
};

static const struct samsung_gate_clock fsys_gate_clks[] __initconst = {
	GATE(CLK_GOUT_FSYS_BUSP3_HCLK, "gout_fsys_busp3_hclk",
	     "gout_mif_cmu_fsys_bus", CLK_CON_GAT_FSYS_BUSP3_HCLK, 2,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_FSYS_MMC0_ACLK, "gout_fsys_mmc0_aclk",
	     "gout_fsys_busp3_hclk", CLK_CON_GAT_FSYS_MMC0_ACLK, 8,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_FSYS_MMC1_ACLK, "gout_fsys_mmc1_aclk",
	     "gout_fsys_busp3_hclk", CLK_CON_GAT_FSYS_MMC1_ACLK, 9,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_FSYS_MMC2_ACLK, "gout_fsys_mmc2_aclk",
	     "gout_fsys_busp3_hclk", CLK_CON_GAT_FSYS_MMC2_ACLK, 10,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_FSYS_PDMA0_ACLK_PDMA0, "gout_fsys_pdma0_aclk_pdma0",
	     "gout_fsys_upsizer_bus1_aclk", CLK_CON_GAT_FSYS_PDMA0_ACLK_PDMA0,
	     7, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_FSYS_PPMU_ACLK, "gout_fsys_ppmu_aclk",
	     "gout_mif_cmu_fsys_bus", CLK_CON_GAT_FSYS_PPMU_ACLK, 17,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_FSYS_PPMU_PCLK, "gout_fsys_ppmu_pclk",
	     "gout_mif_cmu_fsys_bus", CLK_CON_GAT_FSYS_PPMU_PCLK, 18,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_FSYS_SROMC_HCLK, "gout_fsys_sromc_hclk",
	     "gout_fsys_busp3_hclk", CLK_CON_GAT_FSYS_SROMC_HCLK, 6,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_FSYS_UPSIZER_BUS1_ACLK, "gout_fsys_upsizer_bus1_aclk",
	     "gout_mif_cmu_fsys_bus", CLK_CON_GAT_FSYS_UPSIZER_BUS1_ACLK, 12,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_FSYS_USB20DRD_ACLK_HSDRD, "gout_fsys_usb20drd_aclk_hsdrd",
	     "gout_fsys_busp3_hclk", CLK_CON_GAT_FSYS_USB20DRD_ACLK_HSDRD, 20,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_FSYS_USB20DRD_HCLK_USB20_CTRL,
	     "gout_fsys_usb20drd_hclk_usb20_ctrl", "gout_fsys_busp3_hclk",
	     CLK_CON_GAT_FSYS_USB20DRD_HCLK_USB20_CTRL, 21, CLK_IS_CRITICAL |
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_FSYS_USB20DRD_HSDRD_REF_CLK,
	     "gout_fsys_usb20drd_hsdrd_ref_clk",
	     "gout_mif_cmu_fsys_usb20drd_refclk",
	     CLK_CON_GAT_FSYS_USB20DRD_HSDRD_REF_CLK, 0, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_FSYS_MUX_USB20DRD_PHYCLOCK_USER,
	     "gout_fsys_mux_usb20drd_phyclock_user",
	     "gout_fsys_mux_usb20drd_phyclock_user_con",
	     CLK_CON_GAT_FSYS_MUX_USB20DRD_PHYCLOCK_USER, 21, CLK_IS_CRITICAL |
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_FSYS_MUX_USB20DRD_PHYCLOCK_USER_CON,
	     "gout_fsys_mux_usb20drd_phyclock_user_con",
	     "frat_fsys_usb20drd_phyclock",
	     CLK_CON_GAT_FSYS_MUX_USB20DRD_PHYCLOCK_USER_CON, 12,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_FSYS_MUX_USB_PLL, "gout_fsys_mux_usb_pll",
	     "gout_fsys_mux_usb_pll_con", CLK_CON_GAT_FSYS_MUX_USB_PLL, 21,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_FSYS_MUX_USB_PLL_CON, "gout_fsys_mux_usb_pll_con",
	     "fout_fsys_usb_pll", CLK_CON_GAT_FSYS_MUX_USB_PLL_CON, 12,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
};

static const struct samsung_cmu_info fsys_cmu_info __initconst = {
	.fixed_clks	= fsys_fixed_clks,
	.nr_fixed_clks	= ARRAY_SIZE(fsys_fixed_clks),
	.pll_clks		= fsys_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(fsys_pll_clks),
	.gate_clks		= fsys_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(fsys_gate_clks),
	.clk_regs		= fsys_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(fsys_clk_regs),
	.nr_clk_ids		= FSYS_NR_CLK,
};

/*
 * Register offsets for CMU_G3D (0x11460000)
 */
#define PLL_LOCKTIME_G3D_PLL		0x0000
#define PLL_CON0_G3D_PLL		0x0100
#define CLK_CON_GAT_G3D_MUX_PLL		0x0200
#define CLK_CON_GAT_G3D_MUX_PLL_CON	0x0200
#define CLK_CON_GAT_G3D_MUX_SWITCH_USER	0x0204
#define CLK_CON_MUX_G3D_SWITCH_USER	0x0204
#define CLK_CON_GAT_G3D_MUX		0x0208
#define CLK_CON_MUX_G3D			0x0208
#define CLK_CON_DIV_G3D_BUS		0x0400
#define CLK_CON_DIV_G3D_APB		0x0404
#define CLK_CON_GAT_G3D_ASYNCS_D0_CLK	0x0804
#define CLK_CON_GAT_G3D_ASYNC_PCLKM	0x0804
#define CLK_CON_GAT_G3D_CLK		0x0804
#define CLK_CON_GAT_G3D_PPMU_ACLK	0x0804
#define CLK_CON_GAT_G3D_QE_ACLK		0x0804
#define CLK_CON_GAT_G3D_PPMU_PCLK	0x0808
#define CLK_CON_GAT_G3D_QE_PCLK		0x0808
#define CLK_CON_GAT_G3D_SYSREG_PCLK	0x0808

static const unsigned long g3d_clk_regs[] __initconst = {
	PLL_LOCKTIME_G3D_PLL,
	PLL_CON0_G3D_PLL,
	CLK_CON_GAT_G3D_MUX_PLL,
	CLK_CON_GAT_G3D_MUX_PLL_CON,
	CLK_CON_GAT_G3D_MUX_SWITCH_USER,
	CLK_CON_MUX_G3D_SWITCH_USER,
	CLK_CON_GAT_G3D_MUX,
	CLK_CON_MUX_G3D,
	CLK_CON_DIV_G3D_BUS,
	CLK_CON_DIV_G3D_APB,
	CLK_CON_GAT_G3D_ASYNCS_D0_CLK,
	CLK_CON_GAT_G3D_ASYNC_PCLKM,
	CLK_CON_GAT_G3D_CLK,
	CLK_CON_GAT_G3D_PPMU_ACLK,
	CLK_CON_GAT_G3D_QE_ACLK,
	CLK_CON_GAT_G3D_PPMU_PCLK,
	CLK_CON_GAT_G3D_QE_PCLK,
	CLK_CON_GAT_G3D_SYSREG_PCLK,
};

static const struct samsung_pll_clock g3d_pll_clks[] __initconst = {
	PLL(pll_1417x, CLK_FOUT_G3D_PLL, "fout_g3d_pll", "oscclk",
	    PLL_LOCKTIME_G3D_PLL, PLL_CON0_G3D_PLL, NULL),
};

/* List of parent clocks for muxes in CMU_G3D */
PNAME(mout_g3d_switch_user_p)	= { "oscclk", "gout_mif_cmu_g3d_switch" };
PNAME(mout_g3d_p)		= { "gout_g3d_mux_pll_con",
				    "gout_g3d_mux_switch_user" };

static const struct samsung_mux_clock g3d_mux_clks[] __initconst = {
	MUX(CLK_MOUT_G3D_SWITCH_USER, "mout_g3d_switch_user",
	    mout_g3d_switch_user_p, CLK_CON_MUX_G3D_SWITCH_USER, 12, 1),
	MUX(CLK_MOUT_G3D, "mout_g3d", mout_g3d_p, CLK_CON_MUX_G3D, 12, 1),
};

static const struct samsung_div_clock g3d_div_clks[] __initconst = {
	DIV(CLK_DOUT_G3D_APB, "dout_g3d_apb", "dout_g3d_bus",
	    CLK_CON_DIV_G3D_APB, 0, 3),
	DIV(CLK_DOUT_G3D_BUS, "dout_g3d_bus", "gout_g3d_mux",
	    CLK_CON_DIV_G3D_BUS, 0, 3),
};

static const struct samsung_gate_clock g3d_gate_clks[] __initconst = {
	GATE(CLK_GOUT_G3D_ASYNCS_D0_CLK, "gout_g3d_asyncs_d0_clk",
	     "dout_g3d_bus", CLK_CON_GAT_G3D_ASYNCS_D0_CLK, 1, CLK_IS_CRITICAL |
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_G3D_ASYNC_PCLKM, "gout_g3d_async_pclkm", "dout_g3d_bus",
	     CLK_CON_GAT_G3D_ASYNC_PCLKM, 0, CLK_IS_CRITICAL |
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_G3D_CLK, "gout_g3d_clk", "dout_g3d_bus",
	     CLK_CON_GAT_G3D_CLK, 6, CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_G3D_PPMU_ACLK, "gout_g3d_ppmu_aclk", "dout_g3d_bus",
	     CLK_CON_GAT_G3D_PPMU_ACLK, 7, CLK_IS_CRITICAL |
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_G3D_PPMU_PCLK, "gout_g3d_ppmu_pclk", "dout_g3d_apb",
	     CLK_CON_GAT_G3D_PPMU_PCLK, 4, CLK_IS_CRITICAL |
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_G3D_QE_ACLK, "gout_g3d_qe_aclk", "dout_g3d_bus",
	     CLK_CON_GAT_G3D_QE_ACLK, 8, CLK_IS_CRITICAL | CLK_SET_RATE_PARENT,
	     0),
	GATE(CLK_GOUT_G3D_QE_PCLK, "gout_g3d_qe_pclk", "dout_g3d_apb",
	     CLK_CON_GAT_G3D_QE_PCLK, 5, CLK_IS_CRITICAL | CLK_SET_RATE_PARENT,
	     0),
	GATE(CLK_GOUT_G3D_SYSREG_PCLK, "gout_g3d_sysreg_pclk", "dout_g3d_apb",
	     CLK_CON_GAT_G3D_SYSREG_PCLK, 6, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_G3D_MUX_SWITCH_USER, "gout_g3d_mux_switch_user",
	     "mout_g3d_switch_user", CLK_CON_GAT_G3D_MUX_SWITCH_USER, 21,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_G3D_MUX, "gout_g3d_mux", "mout_g3d", CLK_CON_GAT_G3D_MUX,
	     21, CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_G3D_MUX_PLL, "gout_g3d_mux_pll", "gout_g3d_mux_pll_con",
	     CLK_CON_GAT_G3D_MUX_PLL, 21, CLK_IS_CRITICAL | CLK_SET_RATE_PARENT,
	     0),
	GATE(CLK_GOUT_G3D_MUX_PLL_CON, "gout_g3d_mux_pll_con", "fout_g3d_pll",
	     CLK_CON_GAT_G3D_MUX_PLL_CON, 12, CLK_IS_CRITICAL |
	     CLK_SET_RATE_PARENT, 0),
};

static const struct samsung_cmu_info g3d_cmu_info __initconst = {
	.pll_clks		= g3d_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(g3d_pll_clks),
	.mux_clks		= g3d_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(g3d_mux_clks),
	.div_clks		= g3d_div_clks,
	.nr_div_clks		= ARRAY_SIZE(g3d_div_clks),
	.gate_clks		= g3d_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(g3d_gate_clks),
	.clk_regs		= g3d_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(g3d_clk_regs),
	.nr_clk_ids		= G3D_NR_CLK,
};

/*
 * Register offsets for CMU_ISP (0x144d0000)
 */
#define PLL_LOCKTIME_ISP_PLL					0x0000
#define PLL_CON0_ISP_PLL					0x0100
#define CLK_CON_GAT_ISP_MUX_PLL					0x0200
#define CLK_CON_GAT_ISP_MUX_PLL_CON				0x0200
#define CLK_CON_GAT_ISP_MUX_VRA_USER				0x0210
#define CLK_CON_MUX_ISP_VRA_USER				0x0210
#define CLK_CON_GAT_ISP_MUX_CAM_USER				0x0214
#define CLK_CON_MUX_ISP_CAM_USER				0x0214
#define CLK_CON_GAT_ISP_MUX_USER				0x0218
#define CLK_CON_MUX_ISP_USER					0x0218
#define CLK_CON_GAT_ISP_MUX_VRA					0x0220
#define CLK_CON_MUX_ISP_VRA					0x0220
#define CLK_CON_GAT_ISP_MUX_CAM					0x0224
#define CLK_CON_MUX_ISP_CAM					0x0224
#define CLK_CON_GAT_ISP_MUX_ISP					0x0228
#define CLK_CON_MUX_ISP_ISP					0x0228
#define CLK_CON_GAT_ISP_MUX_ISPD				0x022c
#define CLK_CON_MUX_ISP_ISPD					0x022c
#define CLK_CON_GAT_ISP_MUX_RXBYTECLKHS0_SENSOR0_USER		0x0230
#define CLK_CON_GAT_ISP_MUX_RXBYTECLKHS0_SENSOR0_USER_CON	0x0230
#define CLK_CON_GAT_ISP_MUX_RXBYTECLKHS0_SENSOR1_USER		0x0234
#define CLK_CON_GAT_ISP_MUX_RXBYTECLKHS0_SENSOR1_USER_CON	0x0234
#define CLK_CON_DIV_ISP_APB					0x0400
#define CLK_CON_DIV_ISP_CAM_HALF				0x0404
#define CLK_CON_GAT_ISP_VRA					0x0810
#define CLK_CON_GAT_ISP_ISPD					0x0818
#define CLK_CON_GAT_ISP_ISPD_PPMU				0x0818
#define CLK_CON_GAT_ISP_CAM					0x081c
#define CLK_CON_GAT_ISP_CAM_HALF				0x0820

static const unsigned long isp_clk_regs[] __initconst = {
	PLL_LOCKTIME_ISP_PLL,
	PLL_CON0_ISP_PLL,
	CLK_CON_GAT_ISP_MUX_PLL,
	CLK_CON_GAT_ISP_MUX_PLL_CON,
	CLK_CON_GAT_ISP_MUX_VRA_USER,
	CLK_CON_MUX_ISP_VRA_USER,
	CLK_CON_GAT_ISP_MUX_CAM_USER,
	CLK_CON_MUX_ISP_CAM_USER,
	CLK_CON_GAT_ISP_MUX_USER,
	CLK_CON_MUX_ISP_USER,
	CLK_CON_GAT_ISP_MUX_VRA,
	CLK_CON_MUX_ISP_VRA,
	CLK_CON_GAT_ISP_MUX_CAM,
	CLK_CON_MUX_ISP_CAM,
	CLK_CON_GAT_ISP_MUX_ISP,
	CLK_CON_MUX_ISP_ISP,
	CLK_CON_GAT_ISP_MUX_ISPD,
	CLK_CON_MUX_ISP_ISPD,
	CLK_CON_GAT_ISP_MUX_RXBYTECLKHS0_SENSOR0_USER,
	CLK_CON_GAT_ISP_MUX_RXBYTECLKHS0_SENSOR0_USER_CON,
	CLK_CON_GAT_ISP_MUX_RXBYTECLKHS0_SENSOR1_USER,
	CLK_CON_GAT_ISP_MUX_RXBYTECLKHS0_SENSOR1_USER_CON,
	CLK_CON_DIV_ISP_APB,
	CLK_CON_DIV_ISP_CAM_HALF,
	CLK_CON_GAT_ISP_VRA,
	CLK_CON_GAT_ISP_ISPD,
	CLK_CON_GAT_ISP_ISPD_PPMU,
	CLK_CON_GAT_ISP_CAM,
	CLK_CON_GAT_ISP_CAM_HALF,
};

static const struct samsung_fixed_rate_clock isp_fixed_clks[] __initconst = {
	FRATE(0, "frat_isp_rxbyteclkhs0_sensor0", NULL, 0, 188000000),
	FRATE(0, "frat_isp_rxbyteclkhs0_sensor1", NULL, 0, 188000000),
};

static const struct samsung_pll_clock isp_pll_clks[] __initconst = {
	PLL(pll_1417x, CLK_FOUT_ISP_PLL, "fout_isp_pll", "oscclk",
	    PLL_LOCKTIME_ISP_PLL, PLL_CON0_ISP_PLL, NULL),
};

/* List of parent clocks for muxes in CMU_ISP */
PNAME(mout_isp_cam_user_p)	= { "oscclk", "gout_mif_cmu_isp_cam" };
PNAME(mout_isp_user_p)		= { "oscclk", "gout_mif_cmu_isp_isp" };
PNAME(mout_isp_vra_user_p)	= { "oscclk", "gout_mif_cmu_isp_vra" };
PNAME(mout_isp_cam_p)		= { "gout_isp_mux_cam_user",
				    "gout_isp_mux_pll_con" };
PNAME(mout_isp_isp_p)		= { "gout_isp_mux_user", "gout_isp_mux_pll_con" };
PNAME(mout_isp_ispd_p)		= { "gout_isp_mux_vra", "gout_isp_mux_cam" };
PNAME(mout_isp_vra_p)		= { "gout_isp_mux_vra_user",
				    "gout_isp_mux_pll_con" };

static const struct samsung_mux_clock isp_mux_clks[] __initconst = {
	MUX(CLK_MOUT_ISP_CAM_USER, "mout_isp_cam_user", mout_isp_cam_user_p,
	    CLK_CON_MUX_ISP_CAM_USER, 12, 1),
	MUX(CLK_MOUT_ISP_USER, "mout_isp_user", mout_isp_user_p,
	    CLK_CON_MUX_ISP_USER, 12, 1),
	MUX(CLK_MOUT_ISP_VRA_USER, "mout_isp_vra_user", mout_isp_vra_user_p,
	    CLK_CON_MUX_ISP_VRA_USER, 12, 1),
	MUX(CLK_MOUT_ISP_CAM, "mout_isp_cam", mout_isp_cam_p,
	    CLK_CON_MUX_ISP_CAM, 12, 1),
	MUX(CLK_MOUT_ISP_ISP, "mout_isp_isp", mout_isp_isp_p,
	    CLK_CON_MUX_ISP_ISP, 12, 1),
	MUX(CLK_MOUT_ISP_ISPD, "mout_isp_ispd", mout_isp_ispd_p,
	    CLK_CON_MUX_ISP_ISPD, 12, 1),
	MUX(CLK_MOUT_ISP_VRA, "mout_isp_vra", mout_isp_vra_p,
	    CLK_CON_MUX_ISP_VRA, 12, 1),
};

static const struct samsung_div_clock isp_div_clks[] __initconst = {
	DIV(CLK_DOUT_ISP_APB, "dout_isp_apb", "gout_isp_mux_vra",
	    CLK_CON_DIV_ISP_APB, 0, 2),
	DIV(CLK_DOUT_ISP_CAM_HALF, "dout_isp_cam_half", "gout_isp_mux_cam",
	    CLK_CON_DIV_ISP_CAM_HALF, 0, 2),
};

static const struct samsung_gate_clock isp_gate_clks[] __initconst = {
	GATE(CLK_GOUT_ISP_CAM, "gout_isp_cam", "gout_isp_mux_cam",
	     CLK_CON_GAT_ISP_CAM, 0, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_ISP_CAM_HALF, "gout_isp_cam_half", "dout_isp_cam_half",
	     CLK_CON_GAT_ISP_CAM_HALF, 0, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_ISP_ISPD, "gout_isp_ispd", "gout_isp_mux_ispd",
	     CLK_CON_GAT_ISP_ISPD, 0, CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_ISP_ISPD_PPMU, "gout_isp_ispd_ppmu", "gout_isp_mux_ispd",
	     CLK_CON_GAT_ISP_ISPD_PPMU, 1, CLK_IS_CRITICAL |
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_ISP_VRA, "gout_isp_vra", "gout_isp_mux_vra",
	     CLK_CON_GAT_ISP_VRA, 0, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_ISP_MUX_CAM_USER, "gout_isp_mux_cam_user",
	     "mout_isp_cam_user", CLK_CON_GAT_ISP_MUX_CAM_USER, 21,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_ISP_MUX_USER, "gout_isp_mux_user", "mout_isp_user",
	     CLK_CON_GAT_ISP_MUX_USER, 21, CLK_IS_CRITICAL |
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_ISP_MUX_VRA_USER, "gout_isp_mux_vra_user",
	     "mout_isp_vra_user", CLK_CON_GAT_ISP_MUX_VRA_USER, 21,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_ISP_MUX_RXBYTECLKHS0_SENSOR1_USER,
	     "gout_isp_mux_rxbyteclkhs0_sensor1_user",
	     "gout_isp_mux_rxbyteclkhs0_sensor1_user_con",
	     CLK_CON_GAT_ISP_MUX_RXBYTECLKHS0_SENSOR1_USER, 21, CLK_IS_CRITICAL
	     | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_ISP_MUX_RXBYTECLKHS0_SENSOR1_USER_CON,
	     "gout_isp_mux_rxbyteclkhs0_sensor1_user_con",
	     "frat_isp_rxbyteclkhs0_sensor1",
	     CLK_CON_GAT_ISP_MUX_RXBYTECLKHS0_SENSOR1_USER_CON, 12,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_ISP_MUX_RXBYTECLKHS0_SENSOR0_USER,
	     "gout_isp_mux_rxbyteclkhs0_sensor0_user",
	     "gout_isp_mux_rxbyteclkhs0_sensor0_user_con",
	     CLK_CON_GAT_ISP_MUX_RXBYTECLKHS0_SENSOR0_USER, 21, CLK_IS_CRITICAL
	     | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_ISP_MUX_RXBYTECLKHS0_SENSOR0_USER_CON,
	     "gout_isp_mux_rxbyteclkhs0_sensor0_user_con",
	     "frat_isp_rxbyteclkhs0_sensor0",
	     CLK_CON_GAT_ISP_MUX_RXBYTECLKHS0_SENSOR0_USER_CON, 12,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_ISP_MUX_CAM, "gout_isp_mux_cam", "mout_isp_cam",
	     CLK_CON_GAT_ISP_MUX_CAM, 21, CLK_IS_CRITICAL | CLK_SET_RATE_PARENT,
	     0),
	GATE(CLK_GOUT_ISP_MUX_ISP, "gout_isp_mux_isp", "mout_isp_isp",
	     CLK_CON_GAT_ISP_MUX_ISP, 21, CLK_IS_CRITICAL | CLK_SET_RATE_PARENT,
	     0),
	GATE(CLK_GOUT_ISP_MUX_ISPD, "gout_isp_mux_ispd", "mout_isp_ispd",
	     CLK_CON_GAT_ISP_MUX_ISPD, 21, CLK_IS_CRITICAL |
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_ISP_MUX_VRA, "gout_isp_mux_vra", "mout_isp_vra",
	     CLK_CON_GAT_ISP_MUX_VRA, 21, CLK_IS_CRITICAL | CLK_SET_RATE_PARENT,
	     0),
	GATE(CLK_GOUT_ISP_MUX_PLL, "gout_isp_mux_pll", "gout_isp_mux_pll_con",
	     CLK_CON_GAT_ISP_MUX_PLL, 21, CLK_IS_CRITICAL | CLK_SET_RATE_PARENT,
	     0),
	GATE(CLK_GOUT_ISP_MUX_PLL_CON, "gout_isp_mux_pll_con", "fout_isp_pll",
	     CLK_CON_GAT_ISP_MUX_PLL_CON, 12, CLK_IS_CRITICAL |
	     CLK_SET_RATE_PARENT, 0),
};

static const struct samsung_cmu_info isp_cmu_info __initconst = {
	.fixed_clks	= isp_fixed_clks,
	.nr_fixed_clks	= ARRAY_SIZE(isp_fixed_clks),
	.pll_clks		= isp_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(isp_pll_clks),
	.mux_clks		= isp_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(isp_mux_clks),
	.div_clks		= isp_div_clks,
	.nr_div_clks		= ARRAY_SIZE(isp_div_clks),
	.gate_clks		= isp_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(isp_gate_clks),
	.clk_regs		= isp_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(isp_clk_regs),
	.nr_clk_ids		= ISP_NR_CLK,
};

/*
 * Register offsets for CMU_MFCMSCL (0x12cb0000)
 */
#define CLK_CON_GAT_MFCMSCL_MUX_MSCL_USER	0x0200
#define CLK_CON_MUX_MFCMSCL_MSCL_USER		0x0200
#define CLK_CON_GAT_MFCMSCL_MUX_MFC_USER	0x0204
#define CLK_CON_MUX_MFCMSCL_MFC_USER		0x0204
#define CLK_CON_DIV_MFCMSCL_APB			0x0400
#define CLK_CON_GAT_MFCMSCL_MSCL		0x0804
#define CLK_CON_GAT_MFCMSCL_MSCL_BI		0x0804
#define CLK_CON_GAT_MFCMSCL_MSCL_D		0x0804
#define CLK_CON_GAT_MFCMSCL_MSCL_JPEG		0x0804
#define CLK_CON_GAT_MFCMSCL_MSCL_POLY		0x0804
#define CLK_CON_GAT_MFCMSCL_MSCL_PPMU		0x0804
#define CLK_CON_GAT_MFCMSCL_MFC			0x0808

static const unsigned long mfcmscl_clk_regs[] __initconst = {
	CLK_CON_GAT_MFCMSCL_MUX_MSCL_USER,
	CLK_CON_MUX_MFCMSCL_MSCL_USER,
	CLK_CON_GAT_MFCMSCL_MUX_MFC_USER,
	CLK_CON_MUX_MFCMSCL_MFC_USER,
	CLK_CON_DIV_MFCMSCL_APB,
	CLK_CON_GAT_MFCMSCL_MSCL,
	CLK_CON_GAT_MFCMSCL_MSCL_BI,
	CLK_CON_GAT_MFCMSCL_MSCL_D,
	CLK_CON_GAT_MFCMSCL_MSCL_JPEG,
	CLK_CON_GAT_MFCMSCL_MSCL_POLY,
	CLK_CON_GAT_MFCMSCL_MSCL_PPMU,
	CLK_CON_GAT_MFCMSCL_MFC,
};

/* List of parent clocks for muxes in CMU_MFCMSCL */
PNAME(mout_mfcmscl_mfc_user_p)	= { "oscclk", "gout_mif_cmu_mfcmscl_mfc" };
PNAME(mout_mfcmscl_mscl_user_p)	= { "oscclk", "gout_mif_cmu_mfcmscl_mscl" };

static const struct samsung_mux_clock mfcmscl_mux_clks[] __initconst = {
	MUX(CLK_MOUT_MFCMSCL_MFC_USER, "mout_mfcmscl_mfc_user",
	    mout_mfcmscl_mfc_user_p, CLK_CON_MUX_MFCMSCL_MFC_USER, 12, 1),
	MUX(CLK_MOUT_MFCMSCL_MSCL_USER, "mout_mfcmscl_mscl_user",
	    mout_mfcmscl_mscl_user_p, CLK_CON_MUX_MFCMSCL_MSCL_USER, 12, 1),
};

static const struct samsung_div_clock mfcmscl_div_clks[] __initconst = {
	DIV(CLK_DOUT_MFCMSCL_APB, "dout_mfcmscl_apb",
	    "gout_mfcmscl_mux_mscl_user", CLK_CON_DIV_MFCMSCL_APB, 0, 2),
};

static const struct samsung_gate_clock mfcmscl_gate_clks[] __initconst = {
	GATE(CLK_GOUT_MFCMSCL_MFC, "gout_mfcmscl_mfc",
	     "gout_mfcmscl_mux_mfc_user", CLK_CON_GAT_MFCMSCL_MFC, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MFCMSCL_MSCL, "gout_mfcmscl_mscl",
	     "gout_mfcmscl_mux_mscl_user", CLK_CON_GAT_MFCMSCL_MSCL, 0,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MFCMSCL_MSCL_BI, "gout_mfcmscl_mscl_bi",
	     "gout_mfcmscl_mscl_d", CLK_CON_GAT_MFCMSCL_MSCL_BI, 4,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MFCMSCL_MSCL_D, "gout_mfcmscl_mscl_d",
	     "gout_mfcmscl_mux_mscl_user", CLK_CON_GAT_MFCMSCL_MSCL_D, 1,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MFCMSCL_MSCL_JPEG, "gout_mfcmscl_mscl_jpeg",
	     "gout_mfcmscl_mscl_d", CLK_CON_GAT_MFCMSCL_MSCL_JPEG, 2,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MFCMSCL_MSCL_POLY, "gout_mfcmscl_mscl_poly",
	     "gout_mfcmscl_mscl_d", CLK_CON_GAT_MFCMSCL_MSCL_POLY, 3,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MFCMSCL_MSCL_PPMU, "gout_mfcmscl_mscl_ppmu",
	     "gout_mfcmscl_mux_mscl_user", CLK_CON_GAT_MFCMSCL_MSCL_PPMU, 5,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MFCMSCL_MUX_MFC_USER, "gout_mfcmscl_mux_mfc_user",
	     "mout_mfcmscl_mfc_user", CLK_CON_GAT_MFCMSCL_MUX_MFC_USER, 21,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_MFCMSCL_MUX_MSCL_USER, "gout_mfcmscl_mux_mscl_user",
	     "mout_mfcmscl_mscl_user", CLK_CON_GAT_MFCMSCL_MUX_MSCL_USER, 21,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
};

static const struct samsung_cmu_info mfcmscl_cmu_info __initconst = {
	.mux_clks		= mfcmscl_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(mfcmscl_mux_clks),
	.div_clks		= mfcmscl_div_clks,
	.nr_div_clks		= ARRAY_SIZE(mfcmscl_div_clks),
	.gate_clks		= mfcmscl_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(mfcmscl_gate_clks),
	.clk_regs		= mfcmscl_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(mfcmscl_clk_regs),
	.nr_clk_ids		= MFCMSCL_NR_CLK,
};

/*
 * Register offsets for CMU_PERI (0x101f0000)
 */
#define CLK_CON_GAT_PERI_PWM_MOTOR_OSCCLK	0x0800
#define CLK_CON_GAT_PERI_TMU_CPUCL0_CLK		0x0800
#define CLK_CON_GAT_PERI_TMU_CPUCL1_CLK		0x0800
#define CLK_CON_GAT_PERI_TMU_CLK		0x0800
#define CLK_CON_GAT_PERI_BUSP1_PERIC0_HCLK	0x0810
#define CLK_CON_GAT_PERI_GPIO2_PCLK		0x0810
#define CLK_CON_GAT_PERI_GPIO5_PCLK		0x0810
#define CLK_CON_GAT_PERI_GPIO6_PCLK		0x0810
#define CLK_CON_GAT_PERI_GPIO7_PCLK		0x0810
#define CLK_CON_GAT_PERI_HSI2C4_IPCLK		0x0810
#define CLK_CON_GAT_PERI_HSI2C6_IPCLK		0x0810
#define CLK_CON_GAT_PERI_HSI2C3_IPCLK		0x0810
#define CLK_CON_GAT_PERI_HSI2C5_IPCLK		0x0810
#define CLK_CON_GAT_PERI_HSI2C2_IPCLK		0x0810
#define CLK_CON_GAT_PERI_HSI2C1_IPCLK		0x0810
#define CLK_CON_GAT_PERI_I2C0_PCLK		0x0810
#define CLK_CON_GAT_PERI_I2C4_PCLK		0x0810
#define CLK_CON_GAT_PERI_I2C5_PCLK		0x0810
#define CLK_CON_GAT_PERI_I2C6_PCLK		0x0810
#define CLK_CON_GAT_PERI_I2C7_PCLK		0x0810
#define CLK_CON_GAT_PERI_I2C8_PCLK		0x0810
#define CLK_CON_GAT_PERI_I2C3_PCLK		0x0810
#define CLK_CON_GAT_PERI_I2C2_PCLK		0x0810
#define CLK_CON_GAT_PERI_I2C1_PCLK		0x0810
#define CLK_CON_GAT_PERI_MCT_PCLK		0x0810
#define CLK_CON_GAT_PERI_PWM_MOTOR_PCLK_S0	0x0810
#define CLK_CON_GAT_PERI_SFRIF_TMU_CPUCL0_PCLK	0x0814
#define CLK_CON_GAT_PERI_SFRIF_TMU_CPUCL1_PCLK	0x0814
#define CLK_CON_GAT_PERI_SFRIF_TMU_PCLK		0x0814
#define CLK_CON_GAT_PERI_SPI0_PCLK		0x0814
#define CLK_CON_GAT_PERI_SPI2_PCLK		0x0814
#define CLK_CON_GAT_PERI_SPI1_PCLK		0x0814
#define CLK_CON_GAT_PERI_SPI4_PCLK		0x0814
#define CLK_CON_GAT_PERI_SPI3_PCLK		0x0814
#define CLK_CON_GAT_PERI_UART1_PCLK		0x0814
#define CLK_CON_GAT_PERI_UART2_PCLK		0x0814
#define CLK_CON_GAT_PERI_UART0_PCLK		0x0814
#define CLK_CON_GAT_PERI_WDT_CPUCL0_PCLK	0x0814
#define CLK_CON_GAT_PERI_WDT_CPUCL1_PCLK	0x0814
#define CLK_CON_GAT_PERI_UART1_EXT_UCLK		0x0830
#define CLK_CON_GAT_PERI_UART2_EXT_UCLK		0x0834
#define CLK_CON_GAT_PERI_UART0_EXT_UCLK		0x0838
#define CLK_CON_GAT_PERI_SPI2_SPI_EXT_CLK	0x083c
#define CLK_CON_GAT_PERI_SPI1_SPI_EXT_CLK	0x0840
#define CLK_CON_GAT_PERI_SPI0_SPI_EXT_CLK	0x0844
#define CLK_CON_GAT_PERI_SPI3_SPI_EXT_CLK	0x0848
#define CLK_CON_GAT_PERI_SPI4_SPI_EXT_CLK	0x084c

static const unsigned long peri_clk_regs[] __initconst = {
	CLK_CON_GAT_PERI_PWM_MOTOR_OSCCLK,
	CLK_CON_GAT_PERI_TMU_CPUCL0_CLK,
	CLK_CON_GAT_PERI_TMU_CPUCL1_CLK,
	CLK_CON_GAT_PERI_TMU_CLK,
	CLK_CON_GAT_PERI_BUSP1_PERIC0_HCLK,
	CLK_CON_GAT_PERI_GPIO2_PCLK,
	CLK_CON_GAT_PERI_GPIO5_PCLK,
	CLK_CON_GAT_PERI_GPIO6_PCLK,
	CLK_CON_GAT_PERI_GPIO7_PCLK,
	CLK_CON_GAT_PERI_HSI2C4_IPCLK,
	CLK_CON_GAT_PERI_HSI2C6_IPCLK,
	CLK_CON_GAT_PERI_HSI2C3_IPCLK,
	CLK_CON_GAT_PERI_HSI2C5_IPCLK,
	CLK_CON_GAT_PERI_HSI2C2_IPCLK,
	CLK_CON_GAT_PERI_HSI2C1_IPCLK,
	CLK_CON_GAT_PERI_I2C0_PCLK,
	CLK_CON_GAT_PERI_I2C4_PCLK,
	CLK_CON_GAT_PERI_I2C5_PCLK,
	CLK_CON_GAT_PERI_I2C6_PCLK,
	CLK_CON_GAT_PERI_I2C7_PCLK,
	CLK_CON_GAT_PERI_I2C8_PCLK,
	CLK_CON_GAT_PERI_I2C3_PCLK,
	CLK_CON_GAT_PERI_I2C2_PCLK,
	CLK_CON_GAT_PERI_I2C1_PCLK,
	CLK_CON_GAT_PERI_MCT_PCLK,
	CLK_CON_GAT_PERI_PWM_MOTOR_PCLK_S0,
	CLK_CON_GAT_PERI_SFRIF_TMU_CPUCL0_PCLK,
	CLK_CON_GAT_PERI_SFRIF_TMU_CPUCL1_PCLK,
	CLK_CON_GAT_PERI_SFRIF_TMU_PCLK,
	CLK_CON_GAT_PERI_SPI0_PCLK,
	CLK_CON_GAT_PERI_SPI2_PCLK,
	CLK_CON_GAT_PERI_SPI1_PCLK,
	CLK_CON_GAT_PERI_SPI4_PCLK,
	CLK_CON_GAT_PERI_SPI3_PCLK,
	CLK_CON_GAT_PERI_UART1_PCLK,
	CLK_CON_GAT_PERI_UART2_PCLK,
	CLK_CON_GAT_PERI_UART0_PCLK,
	CLK_CON_GAT_PERI_WDT_CPUCL0_PCLK,
	CLK_CON_GAT_PERI_WDT_CPUCL1_PCLK,
	CLK_CON_GAT_PERI_UART1_EXT_UCLK,
	CLK_CON_GAT_PERI_UART2_EXT_UCLK,
	CLK_CON_GAT_PERI_UART0_EXT_UCLK,
	CLK_CON_GAT_PERI_SPI2_SPI_EXT_CLK,
	CLK_CON_GAT_PERI_SPI1_SPI_EXT_CLK,
	CLK_CON_GAT_PERI_SPI0_SPI_EXT_CLK,
	CLK_CON_GAT_PERI_SPI3_SPI_EXT_CLK,
	CLK_CON_GAT_PERI_SPI4_SPI_EXT_CLK,
};

static const struct samsung_gate_clock peri_gate_clks[] __initconst = {
	GATE(CLK_GOUT_PERI_BUSP1_PERIC0_HCLK, "gout_peri_busp1_peric0_hclk",
	     "gout_mif_cmu_peri_bus", CLK_CON_GAT_PERI_BUSP1_PERIC0_HCLK, 3,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_GPIO2_PCLK, "gout_peri_gpio2_pclk",
	     "gout_mif_cmu_peri_bus", CLK_CON_GAT_PERI_GPIO2_PCLK, 7,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_GPIO5_PCLK, "gout_peri_gpio5_pclk",
	     "gout_mif_cmu_peri_bus", CLK_CON_GAT_PERI_GPIO5_PCLK, 8,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_GPIO6_PCLK, "gout_peri_gpio6_pclk",
	     "gout_mif_cmu_peri_bus", CLK_CON_GAT_PERI_GPIO6_PCLK, 9,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_GPIO7_PCLK, "gout_peri_gpio7_pclk",
	     "gout_mif_cmu_peri_bus", CLK_CON_GAT_PERI_GPIO7_PCLK, 10,
	     CLK_IS_CRITICAL | CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_HSI2C4_IPCLK, "gout_peri_hsi2c4_ipclk",
	     "gout_peri_busp1_peric0_hclk", CLK_CON_GAT_PERI_HSI2C4_IPCLK, 14,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_HSI2C6_IPCLK, "gout_peri_hsi2c6_ipclk",
	     "gout_mif_cmu_peri_bus", CLK_CON_GAT_PERI_HSI2C6_IPCLK, 16,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_HSI2C3_IPCLK, "gout_peri_hsi2c3_ipclk",
	     "gout_peri_busp1_peric0_hclk", CLK_CON_GAT_PERI_HSI2C3_IPCLK, 13,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_HSI2C5_IPCLK, "gout_peri_hsi2c5_ipclk",
	     "gout_mif_cmu_peri_bus", CLK_CON_GAT_PERI_HSI2C5_IPCLK, 15,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_HSI2C2_IPCLK, "gout_peri_hsi2c2_ipclk",
	     "gout_peri_busp1_peric0_hclk", CLK_CON_GAT_PERI_HSI2C2_IPCLK, 12,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_HSI2C1_IPCLK, "gout_peri_hsi2c1_ipclk",
	     "gout_peri_busp1_peric0_hclk", CLK_CON_GAT_PERI_HSI2C1_IPCLK, 11,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_I2C0_PCLK, "gout_peri_i2c0_pclk",
	     "gout_peri_busp1_peric0_hclk", CLK_CON_GAT_PERI_I2C0_PCLK, 21,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_I2C4_PCLK, "gout_peri_i2c4_pclk",
	     "gout_peri_busp1_peric0_hclk", CLK_CON_GAT_PERI_I2C4_PCLK, 17,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_I2C5_PCLK, "gout_peri_i2c5_pclk",
	     "gout_peri_busp1_peric0_hclk", CLK_CON_GAT_PERI_I2C5_PCLK, 18,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_I2C6_PCLK, "gout_peri_i2c6_pclk",
	     "gout_peri_busp1_peric0_hclk", CLK_CON_GAT_PERI_I2C6_PCLK, 19,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_I2C7_PCLK, "gout_peri_i2c7_pclk",
	     "gout_peri_busp1_peric0_hclk", CLK_CON_GAT_PERI_I2C7_PCLK, 24,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_I2C8_PCLK, "gout_peri_i2c8_pclk",
	     "gout_peri_busp1_peric0_hclk", CLK_CON_GAT_PERI_I2C8_PCLK, 25,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_I2C3_PCLK, "gout_peri_i2c3_pclk",
	     "gout_peri_busp1_peric0_hclk", CLK_CON_GAT_PERI_I2C3_PCLK, 20,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_I2C2_PCLK, "gout_peri_i2c2_pclk",
	     "gout_peri_busp1_peric0_hclk", CLK_CON_GAT_PERI_I2C2_PCLK, 22,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_I2C1_PCLK, "gout_peri_i2c1_pclk",
	     "gout_peri_busp1_peric0_hclk", CLK_CON_GAT_PERI_I2C1_PCLK, 23,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_MCT_PCLK, "gout_peri_mct_pclk",
	     "gout_mif_cmu_peri_bus", CLK_CON_GAT_PERI_MCT_PCLK, 26,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_PWM_MOTOR_OSCCLK, "gout_peri_pwm_motor_oscclk",
	     "oscclk", CLK_CON_GAT_PERI_PWM_MOTOR_OSCCLK, 2,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_PWM_MOTOR_PCLK_S0, "gout_peri_pwm_motor_pclk_s0",
	     "gout_mif_cmu_peri_bus", CLK_CON_GAT_PERI_PWM_MOTOR_PCLK_S0, 29,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_SFRIF_TMU_CPUCL0_PCLK,
	     "gout_peri_sfrif_tmu_cpucl0_pclk", "gout_mif_cmu_peri_bus",
	     CLK_CON_GAT_PERI_SFRIF_TMU_CPUCL0_PCLK, 1, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_SFRIF_TMU_CPUCL1_PCLK,
	     "gout_peri_sfrif_tmu_cpucl1_pclk", "gout_mif_cmu_peri_bus",
	     CLK_CON_GAT_PERI_SFRIF_TMU_CPUCL1_PCLK, 2, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_SFRIF_TMU_PCLK, "gout_peri_sfrif_tmu_pclk",
	     "gout_mif_cmu_peri_bus", CLK_CON_GAT_PERI_SFRIF_TMU_PCLK, 3,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_SPI0_PCLK, "gout_peri_spi0_pclk",
	     "gout_mif_cmu_peri_bus", CLK_CON_GAT_PERI_SPI0_PCLK, 6,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_SPI0_SPI_EXT_CLK, "gout_peri_spi0_spi_ext_clk",
	     "gout_mif_cmu_peri_spi0", CLK_CON_GAT_PERI_SPI0_SPI_EXT_CLK, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_SPI2_PCLK, "gout_peri_spi2_pclk",
	     "gout_mif_cmu_peri_bus", CLK_CON_GAT_PERI_SPI2_PCLK, 4,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_SPI2_SPI_EXT_CLK, "gout_peri_spi2_spi_ext_clk",
	     "gout_mif_cmu_peri_spi2", CLK_CON_GAT_PERI_SPI2_SPI_EXT_CLK, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_SPI1_PCLK, "gout_peri_spi1_pclk",
	     "gout_mif_cmu_peri_bus", CLK_CON_GAT_PERI_SPI1_PCLK, 5,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_SPI1_SPI_EXT_CLK, "gout_peri_spi1_spi_ext_clk",
	     "gout_mif_cmu_peri_spi1", CLK_CON_GAT_PERI_SPI1_SPI_EXT_CLK, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_SPI4_PCLK, "gout_peri_spi4_pclk",
	     "gout_mif_cmu_peri_bus", CLK_CON_GAT_PERI_SPI4_PCLK, 8,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_SPI4_SPI_EXT_CLK, "gout_peri_spi4_spi_ext_clk",
	     "gout_mif_cmu_peri_spi4", CLK_CON_GAT_PERI_SPI4_SPI_EXT_CLK, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_SPI3_PCLK, "gout_peri_spi3_pclk",
	     "gout_mif_cmu_peri_bus", CLK_CON_GAT_PERI_SPI3_PCLK, 7,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_SPI3_SPI_EXT_CLK, "gout_peri_spi3_spi_ext_clk",
	     "gout_mif_cmu_peri_spi3", CLK_CON_GAT_PERI_SPI3_SPI_EXT_CLK, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_TMU_CPUCL0_CLK, "gout_peri_tmu_cpucl0_clk", "oscclk",
	     CLK_CON_GAT_PERI_TMU_CPUCL0_CLK, 4, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_TMU_CPUCL1_CLK, "gout_peri_tmu_cpucl1_clk", "oscclk",
	     CLK_CON_GAT_PERI_TMU_CPUCL1_CLK, 5, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_TMU_CLK, "gout_peri_tmu_clk", "oscclk",
	     CLK_CON_GAT_PERI_TMU_CLK, 6, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_UART1_EXT_UCLK, "gout_peri_uart1_ext_uclk",
	     "gout_mif_cmu_peri_uart1", CLK_CON_GAT_PERI_UART1_EXT_UCLK, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_UART1_PCLK, "gout_peri_uart1_pclk",
	     "gout_peri_busp1_peric0_hclk", CLK_CON_GAT_PERI_UART1_PCLK, 11,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_UART2_EXT_UCLK, "gout_peri_uart2_ext_uclk",
	     "gout_mif_cmu_peri_uart2", CLK_CON_GAT_PERI_UART2_EXT_UCLK, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_UART2_PCLK, "gout_peri_uart2_pclk",
	     "gout_peri_busp1_peric0_hclk", CLK_CON_GAT_PERI_UART2_PCLK, 12,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_UART0_EXT_UCLK, "gout_peri_uart0_ext_uclk",
	     "gout_mif_cmu_peri_uart0", CLK_CON_GAT_PERI_UART0_EXT_UCLK, 0,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_UART0_PCLK, "gout_peri_uart0_pclk",
	     "gout_peri_busp1_peric0_hclk", CLK_CON_GAT_PERI_UART0_PCLK, 10,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_WDT_CPUCL0_PCLK, "gout_peri_wdt_cpucl0_pclk",
	     "gout_mif_cmu_peri_bus", CLK_CON_GAT_PERI_WDT_CPUCL0_PCLK, 13,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_WDT_CPUCL1_PCLK, "gout_peri_wdt_cpucl1_pclk",
	     "gout_mif_cmu_peri_bus", CLK_CON_GAT_PERI_WDT_CPUCL1_PCLK, 14,
	     CLK_SET_RATE_PARENT, 0),
};

static const struct samsung_cmu_info peri_cmu_info __initconst = {
	.gate_clks		= peri_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(peri_gate_clks),
	.clk_regs		= peri_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(peri_clk_regs),
	.nr_clk_ids		= PERI_NR_CLK,
};

static int __init exynos7870_cmu_probe(struct platform_device *pdev)
{
	const struct samsung_cmu_info *info;
	struct device *dev = &pdev->dev;

	info = of_device_get_match_data(dev);
	exynos_arm64_register_cmu(dev, dev->of_node, info);

	return 0;
}

static const struct of_device_id exynos7870_cmu_of_match[] = {
	{
		.compatible = "samsung,exynos7870-cmu-mif",
		.data = &mif_cmu_info,
	}, {
		.compatible = "samsung,exynos7870-cmu-dispaud",
		.data = &dispaud_cmu_info,
	}, {
		.compatible = "samsung,exynos7870-cmu-fsys",
		.data = &fsys_cmu_info,
	}, {
		.compatible = "samsung,exynos7870-cmu-g3d",
		.data = &g3d_cmu_info,
	}, {
		.compatible = "samsung,exynos7870-cmu-isp",
		.data = &isp_cmu_info,
	}, {
		.compatible = "samsung,exynos7870-cmu-mfcmscl",
		.data = &mfcmscl_cmu_info,
	}, {
		.compatible = "samsung,exynos7870-cmu-peri",
		.data = &peri_cmu_info,
	}, {
	},
};

static struct platform_driver exynos7870_cmu_driver __refdata = {
	.driver = {
		.name = "exynos7870-cmu",
		.of_match_table = exynos7870_cmu_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = exynos7870_cmu_probe,
};

static int __init exynos7870_cmu_init(void)
{
	return platform_driver_register(&exynos7870_cmu_driver);
}
core_initcall(exynos7870_cmu_init);
