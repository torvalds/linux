/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2016 AmLogic, Inc.
 * Author: Michael Turquette <mturquette@baylibre.com>
 *
 * Copyright (c) 2017 Amlogic, inc.
 * Author: Qiufang Dai <qiufang.dai@amlogic.com>
 *
 */
#ifndef __AXG_H
#define __AXG_H

/*
 * Clock controller register offsets
 *
 * Register offsets from the data sheet must be multiplied by 4 before
 * adding them to the base address to get the right value.
 */
#define HHI_MIPI_CNTL0			0x00
#define HHI_GP0_PLL_CNTL		0x40
#define HHI_GP0_PLL_CNTL2		0x44
#define HHI_GP0_PLL_CNTL3		0x48
#define HHI_GP0_PLL_CNTL4		0x4c
#define HHI_GP0_PLL_CNTL5		0x50
#define HHI_GP0_PLL_STS			0x54
#define HHI_GP0_PLL_CNTL1		0x58
#define HHI_HIFI_PLL_CNTL		0x80
#define HHI_HIFI_PLL_CNTL2		0x84
#define HHI_HIFI_PLL_CNTL3		0x88
#define HHI_HIFI_PLL_CNTL4		0x8C
#define HHI_HIFI_PLL_CNTL5		0x90
#define HHI_HIFI_PLL_STS		0x94
#define HHI_HIFI_PLL_CNTL1		0x98

#define HHI_XTAL_DIVN_CNTL		0xbc
#define HHI_GCLK2_MPEG0			0xc0
#define HHI_GCLK2_MPEG1			0xc4
#define HHI_GCLK2_MPEG2			0xc8
#define HHI_GCLK2_OTHER			0xd0
#define HHI_GCLK2_AO			0xd4
#define HHI_PCIE_PLL_CNTL		0xd8
#define HHI_PCIE_PLL_CNTL1		0xdC
#define HHI_PCIE_PLL_CNTL2		0xe0
#define HHI_PCIE_PLL_CNTL3		0xe4
#define HHI_PCIE_PLL_CNTL4		0xe8
#define HHI_PCIE_PLL_CNTL5		0xec
#define HHI_PCIE_PLL_CNTL6		0xf0
#define HHI_PCIE_PLL_STS		0xf4

#define HHI_MEM_PD_REG0			0x100
#define HHI_VPU_MEM_PD_REG0		0x104
#define HHI_VIID_CLK_DIV		0x128
#define HHI_VIID_CLK_CNTL		0x12c

#define HHI_GCLK_MPEG0			0x140
#define HHI_GCLK_MPEG1			0x144
#define HHI_GCLK_MPEG2			0x148
#define HHI_GCLK_OTHER			0x150
#define HHI_GCLK_AO			0x154
#define HHI_SYS_CPU_CLK_CNTL1		0x15c
#define HHI_SYS_CPU_RESET_CNTL		0x160
#define HHI_VID_CLK_DIV			0x164
#define HHI_SPICC_HCLK_CNTL		0x168

#define HHI_MPEG_CLK_CNTL		0x174
#define HHI_VID_CLK_CNTL		0x17c
#define HHI_TS_CLK_CNTL			0x190
#define HHI_VID_CLK_CNTL2		0x194
#define HHI_SYS_CPU_CLK_CNTL0		0x19c
#define HHI_VID_PLL_CLK_DIV		0x1a0
#define HHI_VPU_CLK_CNTL		0x1bC

#define HHI_VAPBCLK_CNTL		0x1F4

#define HHI_GEN_CLK_CNTL		0x228

#define HHI_VDIN_MEAS_CLK_CNTL		0x250
#define HHI_NAND_CLK_CNTL		0x25C
#define HHI_SD_EMMC_CLK_CNTL		0x264

#define HHI_MPLL_CNTL			0x280
#define HHI_MPLL_CNTL2			0x284
#define HHI_MPLL_CNTL3			0x288
#define HHI_MPLL_CNTL4			0x28C
#define HHI_MPLL_CNTL5			0x290
#define HHI_MPLL_CNTL6			0x294
#define HHI_MPLL_CNTL7			0x298
#define HHI_MPLL_CNTL8			0x29C
#define HHI_MPLL_CNTL9			0x2A0
#define HHI_MPLL_CNTL10			0x2A4

#define HHI_MPLL3_CNTL0			0x2E0
#define HHI_MPLL3_CNTL1			0x2E4
#define HHI_PLL_TOP_MISC		0x2E8

#define HHI_SYS_PLL_CNTL1		0x2FC
#define HHI_SYS_PLL_CNTL		0x300
#define HHI_SYS_PLL_CNTL2		0x304
#define HHI_SYS_PLL_CNTL3		0x308
#define HHI_SYS_PLL_CNTL4		0x30c
#define HHI_SYS_PLL_CNTL5		0x310
#define HHI_SYS_PLL_STS			0x314
#define HHI_DPLL_TOP_I			0x318
#define HHI_DPLL_TOP2_I			0x31C

/*
 * CLKID index values
 *
 * These indices are entirely contrived and do not map onto the hardware.
 * It has now been decided to expose everything by default in the DT header:
 * include/dt-bindings/clock/axg-clkc.h. Only the clocks ids we don't want
 * to expose, such as the internal muxes and dividers of composite clocks,
 * will remain defined here.
 */
#define CLKID_MPEG_SEL				8
#define CLKID_MPEG_DIV				9
#define CLKID_SD_EMMC_B_CLK0_SEL		61
#define CLKID_SD_EMMC_B_CLK0_DIV		62
#define CLKID_SD_EMMC_C_CLK0_SEL		63
#define CLKID_SD_EMMC_C_CLK0_DIV		64
#define CLKID_MPLL0_DIV				65
#define CLKID_MPLL1_DIV				66
#define CLKID_MPLL2_DIV				67
#define CLKID_MPLL3_DIV				68
#define CLKID_MPLL_PREDIV			70
#define CLKID_FCLK_DIV2_DIV			71
#define CLKID_FCLK_DIV3_DIV			72
#define CLKID_FCLK_DIV4_DIV			73
#define CLKID_FCLK_DIV5_DIV			74
#define CLKID_FCLK_DIV7_DIV			75
#define CLKID_PCIE_PLL				76
#define CLKID_PCIE_MUX				77
#define CLKID_PCIE_REF				78
#define CLKID_GEN_CLK_SEL			82
#define CLKID_GEN_CLK_DIV			83

#define NR_CLKS					85

/* include the CLKIDs that have been made part of the DT binding */
#include <dt-bindings/clock/axg-clkc.h>

#endif /* __AXG_H */
