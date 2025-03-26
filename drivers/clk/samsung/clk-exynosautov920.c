// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024 Samsung Electronics Co., Ltd.
 * Author: Sunyeal Hong <sunyeal.hong@samsung.com>
 *
 * Common Clock Framework support for ExynosAuto v920 SoC.
 */

#include <linux/clk-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <dt-bindings/clock/samsung,exynosautov920.h>

#include "clk.h"
#include "clk-exynos-arm64.h"

/* NOTE: Must be equal to the last clock ID increased by one */
#define CLKS_NR_TOP			(DOUT_CLKCMU_TAA_NOC + 1)
#define CLKS_NR_PERIC0			(CLK_DOUT_PERIC0_I3C + 1)
#define CLKS_NR_PERIC1			(CLK_DOUT_PERIC1_I3C + 1)
#define CLKS_NR_MISC			(CLK_DOUT_MISC_OSC_DIV2 + 1)
#define CLKS_NR_HSI0			(CLK_DOUT_HSI0_PCIE_APB + 1)
#define CLKS_NR_HSI1			(CLK_MOUT_HSI1_USBDRD + 1)

/* ---- CMU_TOP ------------------------------------------------------------ */

/* Register Offset definitions for CMU_TOP (0x11000000) */
#define PLL_LOCKTIME_PLL_MMC			0x0004
#define PLL_LOCKTIME_PLL_SHARED0		0x0008
#define PLL_LOCKTIME_PLL_SHARED1		0x000c
#define PLL_LOCKTIME_PLL_SHARED2		0x0010
#define PLL_LOCKTIME_PLL_SHARED3		0x0014
#define PLL_LOCKTIME_PLL_SHARED4		0x0018
#define PLL_LOCKTIME_PLL_SHARED5		0x0018
#define PLL_CON0_PLL_MMC			0x0140
#define PLL_CON3_PLL_MMC			0x014c
#define PLL_CON0_PLL_SHARED0			0x0180
#define PLL_CON3_PLL_SHARED0			0x018c
#define PLL_CON0_PLL_SHARED1			0x01c0
#define PLL_CON3_PLL_SHARED1			0x01cc
#define PLL_CON0_PLL_SHARED2			0x0200
#define PLL_CON3_PLL_SHARED2			0x020c
#define PLL_CON0_PLL_SHARED3			0x0240
#define PLL_CON3_PLL_SHARED3			0x024c
#define PLL_CON0_PLL_SHARED4			0x0280
#define PLL_CON3_PLL_SHARED4			0x028c
#define PLL_CON0_PLL_SHARED5			0x02c0
#define PLL_CON3_PLL_SHARED5			0x02cc

/* MUX */
#define CLK_CON_MUX_MUX_CLKCMU_ACC_NOC		0x1000
#define CLK_CON_MUX_MUX_CLKCMU_APM_NOC		0x1004
#define CLK_CON_MUX_MUX_CLKCMU_AUD_CPU		0x1008
#define CLK_CON_MUX_MUX_CLKCMU_AUD_NOC		0x100c
#define CLK_CON_MUX_MUX_CLKCMU_CIS_MCLK0	0x1010
#define CLK_CON_MUX_MUX_CLKCMU_CIS_MCLK1	0x1014
#define CLK_CON_MUX_MUX_CLKCMU_CIS_MCLK2	0x1018
#define CLK_CON_MUX_MUX_CLKCMU_CIS_MCLK3	0x101c
#define CLK_CON_MUX_MUX_CLKCMU_CMU_BOOST	0x1020
#define CLK_CON_MUX_MUX_CLKCMU_CPUCL0_CLUSTER	0x1024
#define CLK_CON_MUX_MUX_CLKCMU_CPUCL0_DBG	0x1028
#define CLK_CON_MUX_MUX_CLKCMU_CPUCL0_SWITCH	0x102c
#define CLK_CON_MUX_MUX_CLKCMU_CPUCL1_CLUSTER	0x1030
#define CLK_CON_MUX_MUX_CLKCMU_CPUCL1_SWITCH	0x1034
#define CLK_CON_MUX_MUX_CLKCMU_CPUCL2_CLUSTER	0x1038
#define CLK_CON_MUX_MUX_CLKCMU_CPUCL2_SWITCH	0x103c
#define CLK_CON_MUX_MUX_CLKCMU_DNC_NOC		0x1040
#define CLK_CON_MUX_MUX_CLKCMU_DPTX_DPGTC	0x1044
#define CLK_CON_MUX_MUX_CLKCMU_DPTX_DPOSC	0x1048
#define CLK_CON_MUX_MUX_CLKCMU_DPTX_NOC		0x104c
#define CLK_CON_MUX_MUX_CLKCMU_DPUB_DSIM	0x1050
#define CLK_CON_MUX_MUX_CLKCMU_DPUB_NOC		0x1054
#define CLK_CON_MUX_MUX_CLKCMU_DPUF0_NOC	0x1058
#define CLK_CON_MUX_MUX_CLKCMU_DPUF1_NOC	0x105c
#define CLK_CON_MUX_MUX_CLKCMU_DPUF2_NOC	0x1060
#define CLK_CON_MUX_MUX_CLKCMU_DSP_NOC		0x1064
#define CLK_CON_MUX_MUX_CLKCMU_G3D_NOCP		0x1068
#define CLK_CON_MUX_MUX_CLKCMU_G3D_SWITCH	0x106c
#define CLK_CON_MUX_MUX_CLKCMU_GNPU_NOC		0x1070
#define CLK_CON_MUX_MUX_CLKCMU_HSI0_NOC		0x1074
#define CLK_CON_MUX_MUX_CLKCMU_ACC_ORB		0x1078
#define CLK_CON_MUX_MUX_CLKCMU_GNPU_XMAA	0x107c
#define CLK_CON_MUX_MUX_CLKCMU_HSI1_MMC_CARD	0x1080
#define CLK_CON_MUX_MUX_CLKCMU_HSI1_NOC		0x1084
#define CLK_CON_MUX_MUX_CLKCMU_HSI1_USBDRD	0x1088
#define CLK_CON_MUX_MUX_CLKCMU_HSI2_ETHERNET	0x108c
#define CLK_CON_MUX_MUX_CLKCMU_HSI2_NOC		0x1090
#define CLK_CON_MUX_MUX_CLKCMU_HSI2_NOC_UFS	0x1094
#define CLK_CON_MUX_MUX_CLKCMU_HSI2_UFS_EMBD	0x1098
#define CLK_CON_MUX_MUX_CLKCMU_ISP_NOC		0x109c
#define CLK_CON_MUX_MUX_CLKCMU_M2M_JPEG		0x10a0
#define CLK_CON_MUX_MUX_CLKCMU_M2M_NOC		0x10a4
#define CLK_CON_MUX_MUX_CLKCMU_MFC_MFC		0x10a8
#define CLK_CON_MUX_MUX_CLKCMU_MFC_WFD		0x10ac
#define CLK_CON_MUX_MUX_CLKCMU_MFD_NOC		0x10b0
#define CLK_CON_MUX_MUX_CLKCMU_MIF_NOCP		0x10b4
#define CLK_CON_MUX_MUX_CLKCMU_MIF_SWITCH	0x10b8
#define CLK_CON_MUX_MUX_CLKCMU_MISC_NOC		0x10bc
#define CLK_CON_MUX_MUX_CLKCMU_NOCL0_NOC	0x10c0
#define CLK_CON_MUX_MUX_CLKCMU_NOCL1_NOC	0x10c4
#define CLK_CON_MUX_MUX_CLKCMU_NOCL2_NOC	0x10c8
#define CLK_CON_MUX_MUX_CLKCMU_PERIC0_IP	0x10cc
#define CLK_CON_MUX_MUX_CLKCMU_PERIC0_NOC	0x10d0
#define CLK_CON_MUX_MUX_CLKCMU_PERIC1_IP	0x10d4
#define CLK_CON_MUX_MUX_CLKCMU_PERIC1_NOC	0x10d8
#define CLK_CON_MUX_MUX_CLKCMU_SDMA_NOC		0x10dc
#define CLK_CON_MUX_MUX_CLKCMU_SNW_NOC		0x10e0
#define CLK_CON_MUX_MUX_CLKCMU_SSP_NOC		0x10e4
#define CLK_CON_MUX_MUX_CLKCMU_TAA_NOC		0x10e8
#define CLK_CON_MUX_MUX_CLK_CMU_NOCP		0x10ec
#define CLK_CON_MUX_MUX_CLK_CMU_PLLCLKOUT	0x10f0
#define CLK_CON_MUX_MUX_CMU_CMUREF		0x10f4

/* DIV */
#define CLK_CON_DIV_CLKCMU_ACC_NOC		0x1800
#define CLK_CON_DIV_CLKCMU_APM_NOC		0x1804
#define CLK_CON_DIV_CLKCMU_AUD_CPU		0x1808
#define CLK_CON_DIV_CLKCMU_AUD_NOC		0x180c
#define CLK_CON_DIV_CLKCMU_CIS_MCLK0		0x1810
#define CLK_CON_DIV_CLKCMU_CIS_MCLK1		0x1814
#define CLK_CON_DIV_CLKCMU_CIS_MCLK2		0x1818
#define CLK_CON_DIV_CLKCMU_CIS_MCLK3		0x181c
#define CLK_CON_DIV_CLKCMU_CPUCL0_CLUSTER	0x1820
#define CLK_CON_DIV_CLKCMU_CPUCL0_DBG		0x1824
#define CLK_CON_DIV_CLKCMU_CPUCL0_SWITCH	0x1828
#define CLK_CON_DIV_CLKCMU_CPUCL1_CLUSTER	0x182c
#define CLK_CON_DIV_CLKCMU_CPUCL1_SWITCH	0x1830
#define CLK_CON_DIV_CLKCMU_CPUCL2_CLUSTER	0x1834
#define CLK_CON_DIV_CLKCMU_CPUCL2_SWITCH	0x1838
#define CLK_CON_DIV_CLKCMU_DNC_NOC		0x183c
#define CLK_CON_DIV_CLKCMU_DPTX_DPGTC		0x1840
#define CLK_CON_DIV_CLKCMU_DPTX_DPOSC		0x1844
#define CLK_CON_DIV_CLKCMU_DPTX_NOC		0x1848
#define CLK_CON_DIV_CLKCMU_DPUB_DSIM		0x184c
#define CLK_CON_DIV_CLKCMU_DPUB_NOC		0x1850
#define CLK_CON_DIV_CLKCMU_DPUF0_NOC		0x1854
#define CLK_CON_DIV_CLKCMU_DPUF1_NOC		0x1858
#define CLK_CON_DIV_CLKCMU_DPUF2_NOC		0x185c
#define CLK_CON_DIV_CLKCMU_DSP_NOC		0x1860
#define CLK_CON_DIV_CLKCMU_G3D_NOCP		0x1864
#define CLK_CON_DIV_CLKCMU_G3D_SWITCH		0x1868
#define CLK_CON_DIV_CLKCMU_GNPU_NOC		0x186c
#define CLK_CON_DIV_CLKCMU_HSI0_NOC		0x1870
#define CLK_CON_DIV_CLKCMU_ACC_ORB		0x1874
#define CLK_CON_DIV_CLKCMU_GNPU_XMAA		0x1878
#define CLK_CON_DIV_CLKCMU_HSI1_MMC_CARD	0x187c
#define CLK_CON_DIV_CLKCMU_HSI1_NOC		0x1880
#define CLK_CON_DIV_CLKCMU_HSI1_USBDRD		0x1884
#define CLK_CON_DIV_CLKCMU_HSI2_ETHERNET	0x1888
#define CLK_CON_DIV_CLKCMU_HSI2_NOC		0x188c
#define CLK_CON_DIV_CLKCMU_HSI2_NOC_UFS		0x1890
#define CLK_CON_DIV_CLKCMU_HSI2_UFS_EMBD	0x1894
#define CLK_CON_DIV_CLKCMU_ISP_NOC		0x1898
#define CLK_CON_DIV_CLKCMU_M2M_JPEG		0x189c
#define CLK_CON_DIV_CLKCMU_M2M_NOC		0x18a0
#define CLK_CON_DIV_CLKCMU_MFC_MFC		0x18a4
#define CLK_CON_DIV_CLKCMU_MFC_WFD		0x18a8
#define CLK_CON_DIV_CLKCMU_MFD_NOC		0x18ac
#define CLK_CON_DIV_CLKCMU_MIF_NOCP		0x18b0
#define CLK_CON_DIV_CLKCMU_MISC_NOC		0x18b4
#define CLK_CON_DIV_CLKCMU_NOCL0_NOC		0x18b8
#define CLK_CON_DIV_CLKCMU_NOCL1_NOC		0x18bc
#define CLK_CON_DIV_CLKCMU_NOCL2_NOC		0x18c0
#define CLK_CON_DIV_CLKCMU_PERIC0_IP		0x18c4
#define CLK_CON_DIV_CLKCMU_PERIC0_NOC		0x18c8
#define CLK_CON_DIV_CLKCMU_PERIC1_IP		0x18cc
#define CLK_CON_DIV_CLKCMU_PERIC1_NOC		0x18d0
#define CLK_CON_DIV_CLKCMU_SDMA_NOC		0x18d4
#define CLK_CON_DIV_CLKCMU_SNW_NOC		0x18d8
#define CLK_CON_DIV_CLKCMU_SSP_NOC		0x18dc
#define CLK_CON_DIV_CLKCMU_TAA_NOC		0x18e0
#define CLK_CON_DIV_CLK_ADD_CH_CLK		0x18e4
#define CLK_CON_DIV_CLK_CMU_PLLCLKOUT		0x18e8
#define CLK_CON_DIV_DIV_CLKCMU_CMU_BOOST	0x18ec
#define CLK_CON_DIV_DIV_CLK_CMU_NOCP		0x18f0

static const unsigned long top_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_MMC,
	PLL_LOCKTIME_PLL_SHARED0,
	PLL_LOCKTIME_PLL_SHARED1,
	PLL_LOCKTIME_PLL_SHARED2,
	PLL_LOCKTIME_PLL_SHARED3,
	PLL_LOCKTIME_PLL_SHARED4,
	PLL_LOCKTIME_PLL_SHARED5,
	PLL_CON0_PLL_MMC,
	PLL_CON3_PLL_MMC,
	PLL_CON0_PLL_SHARED0,
	PLL_CON3_PLL_SHARED0,
	PLL_CON0_PLL_SHARED1,
	PLL_CON3_PLL_SHARED1,
	PLL_CON0_PLL_SHARED2,
	PLL_CON3_PLL_SHARED2,
	PLL_CON0_PLL_SHARED3,
	PLL_CON3_PLL_SHARED3,
	PLL_CON0_PLL_SHARED4,
	PLL_CON3_PLL_SHARED4,
	PLL_CON0_PLL_SHARED5,
	PLL_CON3_PLL_SHARED5,
	CLK_CON_MUX_MUX_CLKCMU_ACC_NOC,
	CLK_CON_MUX_MUX_CLKCMU_APM_NOC,
	CLK_CON_MUX_MUX_CLKCMU_AUD_CPU,
	CLK_CON_MUX_MUX_CLKCMU_AUD_NOC,
	CLK_CON_MUX_MUX_CLKCMU_CIS_MCLK0,
	CLK_CON_MUX_MUX_CLKCMU_CIS_MCLK1,
	CLK_CON_MUX_MUX_CLKCMU_CIS_MCLK2,
	CLK_CON_MUX_MUX_CLKCMU_CIS_MCLK3,
	CLK_CON_MUX_MUX_CLKCMU_CMU_BOOST,
	CLK_CON_MUX_MUX_CLKCMU_CPUCL0_CLUSTER,
	CLK_CON_MUX_MUX_CLKCMU_CPUCL0_DBG,
	CLK_CON_MUX_MUX_CLKCMU_CPUCL0_SWITCH,
	CLK_CON_MUX_MUX_CLKCMU_CPUCL1_CLUSTER,
	CLK_CON_MUX_MUX_CLKCMU_CPUCL1_SWITCH,
	CLK_CON_MUX_MUX_CLKCMU_CPUCL2_CLUSTER,
	CLK_CON_MUX_MUX_CLKCMU_CPUCL2_SWITCH,
	CLK_CON_MUX_MUX_CLKCMU_DNC_NOC,
	CLK_CON_MUX_MUX_CLKCMU_DPTX_DPGTC,
	CLK_CON_MUX_MUX_CLKCMU_DPTX_DPOSC,
	CLK_CON_MUX_MUX_CLKCMU_DPTX_NOC,
	CLK_CON_MUX_MUX_CLKCMU_DPUB_DSIM,
	CLK_CON_MUX_MUX_CLKCMU_DPUB_NOC,
	CLK_CON_MUX_MUX_CLKCMU_DPUF0_NOC,
	CLK_CON_MUX_MUX_CLKCMU_DPUF1_NOC,
	CLK_CON_MUX_MUX_CLKCMU_DPUF2_NOC,
	CLK_CON_MUX_MUX_CLKCMU_DSP_NOC,
	CLK_CON_MUX_MUX_CLKCMU_G3D_NOCP,
	CLK_CON_MUX_MUX_CLKCMU_G3D_SWITCH,
	CLK_CON_MUX_MUX_CLKCMU_GNPU_NOC,
	CLK_CON_MUX_MUX_CLKCMU_HSI0_NOC,
	CLK_CON_MUX_MUX_CLKCMU_ACC_ORB,
	CLK_CON_MUX_MUX_CLKCMU_GNPU_XMAA,
	CLK_CON_MUX_MUX_CLKCMU_HSI1_MMC_CARD,
	CLK_CON_MUX_MUX_CLKCMU_HSI1_NOC,
	CLK_CON_MUX_MUX_CLKCMU_HSI1_USBDRD,
	CLK_CON_MUX_MUX_CLKCMU_HSI2_ETHERNET,
	CLK_CON_MUX_MUX_CLKCMU_HSI2_NOC,
	CLK_CON_MUX_MUX_CLKCMU_HSI2_NOC_UFS,
	CLK_CON_MUX_MUX_CLKCMU_HSI2_UFS_EMBD,
	CLK_CON_MUX_MUX_CLKCMU_ISP_NOC,
	CLK_CON_MUX_MUX_CLKCMU_M2M_JPEG,
	CLK_CON_MUX_MUX_CLKCMU_M2M_NOC,
	CLK_CON_MUX_MUX_CLKCMU_MFC_MFC,
	CLK_CON_MUX_MUX_CLKCMU_MFC_WFD,
	CLK_CON_MUX_MUX_CLKCMU_MFD_NOC,
	CLK_CON_MUX_MUX_CLKCMU_MIF_NOCP,
	CLK_CON_MUX_MUX_CLKCMU_MIF_SWITCH,
	CLK_CON_MUX_MUX_CLKCMU_MISC_NOC,
	CLK_CON_MUX_MUX_CLKCMU_NOCL0_NOC,
	CLK_CON_MUX_MUX_CLKCMU_NOCL1_NOC,
	CLK_CON_MUX_MUX_CLKCMU_NOCL2_NOC,
	CLK_CON_MUX_MUX_CLKCMU_PERIC0_IP,
	CLK_CON_MUX_MUX_CLKCMU_PERIC0_NOC,
	CLK_CON_MUX_MUX_CLKCMU_PERIC1_IP,
	CLK_CON_MUX_MUX_CLKCMU_PERIC1_NOC,
	CLK_CON_MUX_MUX_CLKCMU_SDMA_NOC,
	CLK_CON_MUX_MUX_CLKCMU_SNW_NOC,
	CLK_CON_MUX_MUX_CLKCMU_SSP_NOC,
	CLK_CON_MUX_MUX_CLKCMU_TAA_NOC,
	CLK_CON_MUX_MUX_CLK_CMU_NOCP,
	CLK_CON_MUX_MUX_CLK_CMU_PLLCLKOUT,
	CLK_CON_MUX_MUX_CMU_CMUREF,
	CLK_CON_DIV_CLKCMU_ACC_NOC,
	CLK_CON_DIV_CLKCMU_APM_NOC,
	CLK_CON_DIV_CLKCMU_AUD_CPU,
	CLK_CON_DIV_CLKCMU_AUD_NOC,
	CLK_CON_DIV_CLKCMU_CIS_MCLK0,
	CLK_CON_DIV_CLKCMU_CIS_MCLK1,
	CLK_CON_DIV_CLKCMU_CIS_MCLK2,
	CLK_CON_DIV_CLKCMU_CIS_MCLK3,
	CLK_CON_DIV_CLKCMU_CPUCL0_CLUSTER,
	CLK_CON_DIV_CLKCMU_CPUCL0_DBG,
	CLK_CON_DIV_CLKCMU_CPUCL0_SWITCH,
	CLK_CON_DIV_CLKCMU_CPUCL1_CLUSTER,
	CLK_CON_DIV_CLKCMU_CPUCL1_SWITCH,
	CLK_CON_DIV_CLKCMU_CPUCL2_CLUSTER,
	CLK_CON_DIV_CLKCMU_CPUCL2_SWITCH,
	CLK_CON_DIV_CLKCMU_DNC_NOC,
	CLK_CON_DIV_CLKCMU_DPTX_DPGTC,
	CLK_CON_DIV_CLKCMU_DPTX_DPOSC,
	CLK_CON_DIV_CLKCMU_DPTX_NOC,
	CLK_CON_DIV_CLKCMU_DPUB_DSIM,
	CLK_CON_DIV_CLKCMU_DPUB_NOC,
	CLK_CON_DIV_CLKCMU_DPUF0_NOC,
	CLK_CON_DIV_CLKCMU_DPUF1_NOC,
	CLK_CON_DIV_CLKCMU_DPUF2_NOC,
	CLK_CON_DIV_CLKCMU_DSP_NOC,
	CLK_CON_DIV_CLKCMU_G3D_NOCP,
	CLK_CON_DIV_CLKCMU_G3D_SWITCH,
	CLK_CON_DIV_CLKCMU_GNPU_NOC,
	CLK_CON_DIV_CLKCMU_HSI0_NOC,
	CLK_CON_DIV_CLKCMU_ACC_ORB,
	CLK_CON_DIV_CLKCMU_GNPU_XMAA,
	CLK_CON_DIV_CLKCMU_HSI1_MMC_CARD,
	CLK_CON_DIV_CLKCMU_HSI1_NOC,
	CLK_CON_DIV_CLKCMU_HSI1_USBDRD,
	CLK_CON_DIV_CLKCMU_HSI2_ETHERNET,
	CLK_CON_DIV_CLKCMU_HSI2_NOC,
	CLK_CON_DIV_CLKCMU_HSI2_NOC_UFS,
	CLK_CON_DIV_CLKCMU_HSI2_UFS_EMBD,
	CLK_CON_DIV_CLKCMU_ISP_NOC,
	CLK_CON_DIV_CLKCMU_M2M_JPEG,
	CLK_CON_DIV_CLKCMU_M2M_NOC,
	CLK_CON_DIV_CLKCMU_MFC_MFC,
	CLK_CON_DIV_CLKCMU_MFC_WFD,
	CLK_CON_DIV_CLKCMU_MFD_NOC,
	CLK_CON_DIV_CLKCMU_MIF_NOCP,
	CLK_CON_DIV_CLKCMU_MISC_NOC,
	CLK_CON_DIV_CLKCMU_NOCL0_NOC,
	CLK_CON_DIV_CLKCMU_NOCL1_NOC,
	CLK_CON_DIV_CLKCMU_NOCL2_NOC,
	CLK_CON_DIV_CLKCMU_PERIC0_IP,
	CLK_CON_DIV_CLKCMU_PERIC0_NOC,
	CLK_CON_DIV_CLKCMU_PERIC1_IP,
	CLK_CON_DIV_CLKCMU_PERIC1_NOC,
	CLK_CON_DIV_CLKCMU_SDMA_NOC,
	CLK_CON_DIV_CLKCMU_SNW_NOC,
	CLK_CON_DIV_CLKCMU_SSP_NOC,
	CLK_CON_DIV_CLKCMU_TAA_NOC,
	CLK_CON_DIV_CLK_ADD_CH_CLK,
	CLK_CON_DIV_CLK_CMU_PLLCLKOUT,
	CLK_CON_DIV_DIV_CLKCMU_CMU_BOOST,
	CLK_CON_DIV_DIV_CLK_CMU_NOCP,
};

static const struct samsung_pll_clock top_pll_clks[] __initconst = {
	/* CMU_TOP_PURECLKCOMP */
	PLL(pll_531x, FOUT_SHARED0_PLL, "fout_shared0_pll", "oscclk",
	    PLL_LOCKTIME_PLL_SHARED0, PLL_CON3_PLL_SHARED0, NULL),
	PLL(pll_531x, FOUT_SHARED1_PLL, "fout_shared1_pll", "oscclk",
	    PLL_LOCKTIME_PLL_SHARED1, PLL_CON3_PLL_SHARED1, NULL),
	PLL(pll_531x, FOUT_SHARED2_PLL, "fout_shared2_pll", "oscclk",
	    PLL_LOCKTIME_PLL_SHARED2, PLL_CON3_PLL_SHARED2, NULL),
	PLL(pll_531x, FOUT_SHARED3_PLL, "fout_shared3_pll", "oscclk",
	    PLL_LOCKTIME_PLL_SHARED3, PLL_CON3_PLL_SHARED3, NULL),
	PLL(pll_531x, FOUT_SHARED4_PLL, "fout_shared4_pll", "oscclk",
	    PLL_LOCKTIME_PLL_SHARED4, PLL_CON3_PLL_SHARED4, NULL),
	PLL(pll_531x, FOUT_SHARED5_PLL, "fout_shared5_pll", "oscclk",
	    PLL_LOCKTIME_PLL_SHARED5, PLL_CON3_PLL_SHARED5, NULL),
	PLL(pll_531x, FOUT_MMC_PLL, "fout_mmc_pll", "oscclk",
	    PLL_LOCKTIME_PLL_MMC, PLL_CON3_PLL_MMC, NULL),
};

/* List of parent clocks for Muxes in CMU_TOP */
PNAME(mout_shared0_pll_p) = { "oscclk", "fout_shared0_pll" };
PNAME(mout_shared1_pll_p) = { "oscclk", "fout_shared1_pll" };
PNAME(mout_shared2_pll_p) = { "oscclk", "fout_shared2_pll" };
PNAME(mout_shared3_pll_p) = { "oscclk", "fout_shared3_pll" };
PNAME(mout_shared4_pll_p) = { "oscclk", "fout_shared4_pll" };
PNAME(mout_shared5_pll_p) = { "oscclk", "fout_shared5_pll" };
PNAME(mout_mmc_pll_p) = { "oscclk", "fout_mmc_pll" };

PNAME(mout_clkcmu_cmu_boost_p) = { "dout_shared2_div3", "dout_shared1_div4",
				   "dout_shared2_div4", "dout_shared4_div4" };

PNAME(mout_clkcmu_cmu_cmuref_p) = { "oscclk", "dout_cmu_boost" };

PNAME(mout_clkcmu_acc_noc_p) = { "dout_shared2_div2", "dout_shared0_div3",
				 "dout_shared4_div2", "dout_shared1_div3",
				 "dout_shared2_div3", "dout_shared5_div1",
				 "dout_shared3_div1", "oscclk" };

PNAME(mout_clkcmu_acc_orb_p) = { "dout_shared2_div2", "dout_shared0_div3",
				 "dout_shared1_div2", "dout_shared1_div3",
				 "dout_shared2_div3", "fout_shared5_pll",
				 "fout_shared3_pll", "oscclk" };

PNAME(mout_clkcmu_apm_noc_p) = { "dout_shared2_div2", "dout_shared1_div4",
				 "dout_shared2_div4", "dout_shared4_div4" };

PNAME(mout_clkcmu_aud_cpu_p) = { "dout_shared0_div2", "dout_shared1_div2",
				 "dout_shared2_div2", "dout_shared0_div3",
				 "dout_shared4_div2", "dout_shared1_div3",
				 "dout_shared2_div3", "dout_shared4_div3" };

PNAME(mout_clkcmu_aud_noc_p) = { "dout_shared2_div2", "dout_shared4_div2",
				 "dout_shared1_div2", "dout_shared2_div3" };

PNAME(mout_clkcmu_cpucl0_switch_p) = { "dout_shared0_div2", "dout_shared1_div2",
				       "dout_shared2_div2", "dout_shared4_div2" };

PNAME(mout_clkcmu_cpucl0_cluster_p) = { "fout_shared2_pll", "fout_shared4_pll",
					"dout_shared0_div2", "dout_shared1_div2",
					"dout_shared2_div2", "dout_shared4_div2",
					"dout_shared2_div3", "fout_shared3_pll" };

PNAME(mout_clkcmu_cpucl0_dbg_p) = { "dout_shared2_div2", "dout_shared0_div3",
				    "dout_shared4_div2", "dout_shared0_div4" };

PNAME(mout_clkcmu_cpucl1_switch_p) = { "dout_shared0_div2", "dout_shared1_div2",
				       "dout_shared2_div2", "dout_shared4_div2" };

PNAME(mout_clkcmu_cpucl1_cluster_p) = { "fout_shared2_pll", "fout_shared4_pll",
					"dout_shared0_div2", "dout_shared1_div2",
					"dout_shared2_div2", "dout_shared4_div2",
					"dout_shared2_div3", "fout_shared3_pll" };

PNAME(mout_clkcmu_cpucl2_switch_p) = { "dout_shared0_div2", "dout_shared1_div2",
				       "dout_shared2_div2", "dout_shared4_div2" };

PNAME(mout_clkcmu_cpucl2_cluster_p) = { "fout_shared2_pll", "fout_shared4_pll",
					"dout_shared0_div2", "dout_shared1_div2",
					"dout_shared2_div2", "dout_shared4_div2",
					"dout_shared2_div3", "fout_shared3_pll" };

PNAME(mout_clkcmu_dnc_noc_p) = { "dout_shared1_div2", "dout_shared2_div2",
				 "dout_shared0_div3", "dout_shared4_div2",
				 "dout_shared1_div3", "dout_shared2_div3",
				 "dout_shared1_div4", "fout_shared3_pll" };

PNAME(mout_clkcmu_dptx_noc_p) = { "dout_shared4_div2", "dout_shared2_div3",
				  "dout_shared1_div4", "dout_shared2_div4" };

PNAME(mout_clkcmu_dptx_dpgtc_p) = { "oscclk", "dout_shared2_div3",
				    "dout_shared2_div4", "dout_shared4_div4" };

PNAME(mout_clkcmu_dptx_dposc_p) = { "oscclk", "dout_shared2_div4" };

PNAME(mout_clkcmu_dpub_noc_p) = { "dout_shared4_div2", "dout_shared1_div3",
				 "dout_shared2_div3", "dout_shared1_div4",
				 "dout_shared2_div4", "dout_shared4_div4",
				 "fout_shared3_pll" };

PNAME(mout_clkcmu_dpub_dsim_p) = { "dout_shared2_div3", "dout_shared2_div4" };

PNAME(mout_clkcmu_dpuf_noc_p) = { "dout_shared4_div2", "dout_shared1_div3",
				   "dout_shared2_div3", "dout_shared1_div4",
				   "dout_shared2_div4", "dout_shared4_div4",
				   "fout_shared3_pll" };

PNAME(mout_clkcmu_dsp_noc_p) = { "dout_shared0_div2", "dout_shared1_div2",
				 "dout_shared2_div2", "dout_shared0_div3",
				 "dout_shared4_div2", "dout_shared1_div3",
				 "fout_shared5_pll", "fout_shared3_pll" };

PNAME(mout_clkcmu_g3d_switch_p) = { "dout_shared0_div2", "dout_shared1_div2",
				    "dout_shared2_div2", "dout_shared4_div2" };

PNAME(mout_clkcmu_g3d_nocp_p) = { "dout_shared2_div3", "dout_shared1_div4",
				  "dout_shared2_div4", "dout_shared4_div4" };

PNAME(mout_clkcmu_gnpu_noc_p) = { "dout_shared0_div2", "dout_shared1_div2",
				  "dout_shared2_div2", "dout_shared0_div3",
				  "dout_shared4_div2", "dout_shared2_div3",
				  "fout_shared5_pll", "fout_shared3_pll" };

PNAME(mout_clkcmu_hsi0_noc_p) = { "dout_shared4_div2", "dout_shared2_div3",
				  "dout_shared1_div4", "dout_shared2_div4" };

PNAME(mout_clkcmu_hsi1_noc_p) = { "dout_shared2_div3", "dout_shared1_div4",
				  "dout_shared2_div4", "dout_shared4_div4" };

PNAME(mout_clkcmu_hsi1_usbdrd_p) = { "oscclk", "dout_shared2_div3",
				     "dout_shared2_div4", "dout_shared4_div4" };

PNAME(mout_clkcmu_hsi1_mmc_card_p) = { "oscclk", "dout_shared2_div2",
				       "dout_shared4_div2", "fout_mmc_pll" };

PNAME(mout_clkcmu_hsi2_noc_p) = { "dout_shared4_div2", "dout_shared2_div3",
				  "dout_shared1_div4", "dout_shared2_div4" };

PNAME(mout_clkcmu_hsi2_noc_ufs_p) = { "dout_shared4_div2", "dout_shared2_div3",
				      "dout_shared1_div4", "dout_shared2_div2" };

PNAME(mout_clkcmu_hsi2_ufs_embd_p) = { "oscclk", "dout_shared2_div3",
				       "dout_shared2_div4", "dout_shared4_div4" };

PNAME(mout_clkcmu_hsi2_ethernet_p) = { "oscclk", "dout_shared2_div2",
				       "dout_shared0_div3", "dout_shared1_div3" };

PNAME(mout_clkcmu_isp_noc_p) = { "dout_shared2_div2", "dout_shared0_div3",
				 "dout_shared4_div2", "dout_shared1_div3",
				 "dout_shared2_div3", "fout_shared5_pll",
				 "fout_shared3_pll", "oscclk" };

PNAME(mout_clkcmu_m2m_noc_p) = { "dout_shared0_div3", "dout_shared4_div2",
				 "dout_shared2_div3", "dout_shared1_div4" };

PNAME(mout_clkcmu_m2m_jpeg_p) = { "dout_shared0_div3", "dout_shared4_div2",
				  "dout_shared2_div3", "dout_shared1_div4" };

PNAME(mout_clkcmu_mfc_mfc_p) = { "dout_shared0_div3", "dout_shared4_div2",
				 "dout_shared2_div3", "dout_shared1_div4" };

PNAME(mout_clkcmu_mfc_wfd_p) = { "dout_shared0_div3", "dout_shared4_div2",
				 "dout_shared2_div3", "dout_shared1_div4" };

PNAME(mout_clkcmu_mfd_noc_p) = { "dout_shared2_div2", "dout_shared0_div3",
				 "dout_shared4_div2", "dout_shared1_div3",
				 "dout_shared2_div3", "fout_shared5_pll",
				 "fout_shared3_pll", "oscclk" };

PNAME(mout_clkcmu_mif_switch_p) = { "fout_shared0_pll", "fout_shared1_pll",
				    "fout_shared2_pll", "fout_shared4_pll",
				    "dout_shared0_div2", "dout_shared1_div2",
				    "dout_shared2_div2", "fout_shared5_pll" };

PNAME(mout_clkcmu_mif_nocp_p) = { "dout_shared2_div3", "dout_shared1_div4",
				  "dout_shared2_div4", "dout_shared4_div4" };

PNAME(mout_clkcmu_misc_noc_p) = { "dout_shared4_div2", "dout_shared2_div3",
				  "dout_shared1_div4", "dout_shared2_div4" };

PNAME(mout_clkcmu_nocl0_noc_p) = { "dout_shared0_div2", "dout_shared1_div2",
				   "dout_shared2_div2", "dout_shared0_div3",
				   "dout_shared4_div2", "dout_shared1_div3",
				   "dout_shared2_div3", "fout_shared3_pll" };

PNAME(mout_clkcmu_nocl1_noc_p) = { "dout_shared2_div2", "dout_shared0_div3",
				   "dout_shared4_div2", "dout_shared1_div3",
				   "dout_shared2_div3", "fout_shared5_pll",
				   "fout_shared3_pll", "oscclk" };

PNAME(mout_clkcmu_nocl2_noc_p) = { "dout_shared2_div2", "dout_shared0_div3",
				   "dout_shared4_div2", "dout_shared1_div3",
				   "dout_shared2_div3", "fout_shared5_pll",
				   "fout_shared3_pll", "oscclk" };

PNAME(mout_clkcmu_peric0_noc_p) = { "dout_shared2_div3", "dout_shared2_div4" };

PNAME(mout_clkcmu_peric0_ip_p) = { "dout_shared2_div3", "dout_shared2_div4" };

PNAME(mout_clkcmu_peric1_noc_p) = { "dout_shared2_div3", "dout_shared2_div4" };

PNAME(mout_clkcmu_peric1_ip_p) = { "dout_shared2_div3", "dout_shared2_div4" };

PNAME(mout_clkcmu_sdma_noc_p) = { "dout_shared1_div2", "dout_shared2_div2",
				  "dout_shared0_div3", "dout_shared4_div2",
				  "dout_shared1_div3", "dout_shared2_div3",
				  "dout_shared1_div4", "fout_shared3_pll" };

PNAME(mout_clkcmu_snw_noc_p) = { "dout_shared2_div2", "dout_shared0_div3",
				 "dout_shared4_div2", "dout_shared1_div3",
				 "dout_shared2_div3", "fout_shared5_pll",
				 "fout_shared3_pll", "oscclk" };

PNAME(mout_clkcmu_ssp_noc_p) = { "dout_shared2_div3", "dout_shared1_div4",
				  "dout_shared2_div2", "dout_shared4_div4" };

PNAME(mout_clkcmu_taa_noc_p) = { "dout_shared2_div2", "dout_shared0_div3",
				 "dout_shared4_div2", "dout_shared1_div3",
				 "dout_shared2_div3", "fout_shared5_pll",
				 "fout_shared3_pll", "oscclk" };

static const struct samsung_mux_clock top_mux_clks[] __initconst = {
	/* CMU_TOP_PURECLKCOMP */
	MUX(MOUT_SHARED0_PLL, "mout_shared0_pll", mout_shared0_pll_p,
	    PLL_CON0_PLL_SHARED0, 4, 1),
	MUX(MOUT_SHARED1_PLL, "mout_shared1_pll", mout_shared1_pll_p,
	    PLL_CON0_PLL_SHARED1, 4, 1),
	MUX(MOUT_SHARED2_PLL, "mout_shared2_pll", mout_shared2_pll_p,
	    PLL_CON0_PLL_SHARED2, 4, 1),
	MUX(MOUT_SHARED3_PLL, "mout_shared3_pll", mout_shared3_pll_p,
	    PLL_CON0_PLL_SHARED3, 4, 1),
	MUX(MOUT_SHARED4_PLL, "mout_shared4_pll", mout_shared4_pll_p,
	    PLL_CON0_PLL_SHARED4, 4, 1),
	MUX(MOUT_SHARED5_PLL, "mout_shared5_pll", mout_shared5_pll_p,
	    PLL_CON0_PLL_SHARED5, 4, 1),
	MUX(MOUT_MMC_PLL, "mout_mmc_pll", mout_mmc_pll_p,
	    PLL_CON0_PLL_MMC, 4, 1),

	/* BOOST */
	MUX(MOUT_CLKCMU_CMU_BOOST, "mout_clkcmu_cmu_boost",
	    mout_clkcmu_cmu_boost_p, CLK_CON_MUX_MUX_CLKCMU_CMU_BOOST, 0, 2),
	MUX(MOUT_CLKCMU_CMU_CMUREF, "mout_clkcmu_cmu_cmuref",
	    mout_clkcmu_cmu_cmuref_p, CLK_CON_MUX_MUX_CMU_CMUREF, 0, 1),

	/* ACC */
	MUX(MOUT_CLKCMU_ACC_NOC, "mout_clkcmu_acc_noc",
	    mout_clkcmu_acc_noc_p, CLK_CON_MUX_MUX_CLKCMU_ACC_NOC, 0, 3),
	MUX(MOUT_CLKCMU_ACC_ORB, "mout_clkcmu_acc_orb",
	    mout_clkcmu_acc_orb_p, CLK_CON_MUX_MUX_CLKCMU_ACC_ORB, 0, 3),

	/* APM */
	MUX(MOUT_CLKCMU_APM_NOC, "mout_clkcmu_apm_noc",
	    mout_clkcmu_apm_noc_p, CLK_CON_MUX_MUX_CLKCMU_APM_NOC, 0, 2),

	/* AUD */
	MUX(MOUT_CLKCMU_AUD_CPU, "mout_clkcmu_aud_cpu",
	    mout_clkcmu_aud_cpu_p, CLK_CON_MUX_MUX_CLKCMU_AUD_CPU, 0, 3),
	MUX(MOUT_CLKCMU_AUD_NOC, "mout_clkcmu_aud_noc",
	    mout_clkcmu_aud_noc_p, CLK_CON_MUX_MUX_CLKCMU_AUD_NOC, 0, 2),

	/* CPUCL0 */
	MUX(MOUT_CLKCMU_CPUCL0_SWITCH, "mout_clkcmu_cpucl0_switch",
	    mout_clkcmu_cpucl0_switch_p, CLK_CON_MUX_MUX_CLKCMU_CPUCL0_SWITCH,
	    0, 2),
	MUX(MOUT_CLKCMU_CPUCL0_CLUSTER, "mout_clkcmu_cpucl0_cluster",
	    mout_clkcmu_cpucl0_cluster_p, CLK_CON_MUX_MUX_CLKCMU_CPUCL0_CLUSTER,
	    0, 3),
	MUX(MOUT_CLKCMU_CPUCL0_DBG, "mout_clkcmu_cpucl0_dbg",
	    mout_clkcmu_cpucl0_dbg_p, CLK_CON_MUX_MUX_CLKCMU_CPUCL0_DBG,
	    0, 2),

	/* CPUCL1 */
	MUX(MOUT_CLKCMU_CPUCL1_SWITCH, "mout_clkcmu_cpucl1_switch",
	    mout_clkcmu_cpucl1_switch_p, CLK_CON_MUX_MUX_CLKCMU_CPUCL1_SWITCH,
	    0, 2),
	MUX(MOUT_CLKCMU_CPUCL1_CLUSTER, "mout_clkcmu_cpucl1_cluster",
	    mout_clkcmu_cpucl1_cluster_p, CLK_CON_MUX_MUX_CLKCMU_CPUCL1_CLUSTER,
	    0, 3),

	/* CPUCL2 */
	MUX(MOUT_CLKCMU_CPUCL2_SWITCH, "mout_clkcmu_cpucl2_switch",
	    mout_clkcmu_cpucl2_switch_p, CLK_CON_MUX_MUX_CLKCMU_CPUCL2_SWITCH,
	    0, 2),
	MUX(MOUT_CLKCMU_CPUCL2_CLUSTER, "mout_clkcmu_cpucl2_cluster",
	    mout_clkcmu_cpucl2_cluster_p, CLK_CON_MUX_MUX_CLKCMU_CPUCL2_CLUSTER,
	    0, 3),

	/* DNC */
	MUX(MOUT_CLKCMU_DNC_NOC, "mout_clkcmu_dnc_noc",
	    mout_clkcmu_dnc_noc_p, CLK_CON_MUX_MUX_CLKCMU_DNC_NOC, 0, 3),

	/* DPTX */
	MUX(MOUT_CLKCMU_DPTX_NOC, "mout_clkcmu_dptx_noc",
	    mout_clkcmu_dptx_noc_p, CLK_CON_MUX_MUX_CLKCMU_DPTX_NOC, 0, 2),
	MUX(MOUT_CLKCMU_DPTX_DPGTC, "mout_clkcmu_dptx_dpgtc",
	    mout_clkcmu_dptx_dpgtc_p, CLK_CON_MUX_MUX_CLKCMU_DPTX_DPGTC, 0, 2),
	MUX(MOUT_CLKCMU_DPTX_DPOSC, "mout_clkcmu_dptx_dposc",
	    mout_clkcmu_dptx_dposc_p, CLK_CON_MUX_MUX_CLKCMU_DPTX_DPOSC, 0, 1),

	/* DPUB */
	MUX(MOUT_CLKCMU_DPUB_NOC, "mout_clkcmu_dpub_noc",
	    mout_clkcmu_dpub_noc_p, CLK_CON_MUX_MUX_CLKCMU_DPUB_NOC, 0, 3),
	MUX(MOUT_CLKCMU_DPUB_DSIM, "mout_clkcmu_dpub_dsim",
	    mout_clkcmu_dpub_dsim_p, CLK_CON_MUX_MUX_CLKCMU_DPUB_DSIM, 0, 1),

	/* DPUF */
	MUX(MOUT_CLKCMU_DPUF0_NOC, "mout_clkcmu_dpuf0_noc",
	    mout_clkcmu_dpuf_noc_p, CLK_CON_MUX_MUX_CLKCMU_DPUF0_NOC, 0, 3),
	MUX(MOUT_CLKCMU_DPUF1_NOC, "mout_clkcmu_dpuf1_noc",
	    mout_clkcmu_dpuf_noc_p, CLK_CON_MUX_MUX_CLKCMU_DPUF1_NOC, 0, 3),
	MUX(MOUT_CLKCMU_DPUF2_NOC, "mout_clkcmu_dpuf2_noc",
	    mout_clkcmu_dpuf_noc_p, CLK_CON_MUX_MUX_CLKCMU_DPUF2_NOC, 0, 3),

	/* DSP */
	MUX(MOUT_CLKCMU_DSP_NOC, "mout_clkcmu_dsp_noc",
	    mout_clkcmu_dsp_noc_p, CLK_CON_MUX_MUX_CLKCMU_DSP_NOC, 0, 3),

	/* G3D */
	MUX(MOUT_CLKCMU_G3D_SWITCH, "mout_clkcmu_g3d_switch",
	    mout_clkcmu_g3d_switch_p, CLK_CON_MUX_MUX_CLKCMU_G3D_SWITCH, 0, 2),
	MUX(MOUT_CLKCMU_G3D_NOCP, "mout_clkcmu_g3d_nocp",
	    mout_clkcmu_g3d_nocp_p, CLK_CON_MUX_MUX_CLKCMU_G3D_NOCP, 0, 2),

	/* GNPU */
	MUX(MOUT_CLKCMU_GNPU_NOC, "mout_clkcmu_gnpu_noc",
	    mout_clkcmu_gnpu_noc_p, CLK_CON_MUX_MUX_CLKCMU_GNPU_NOC, 0, 3),

	/* HSI0 */
	MUX(MOUT_CLKCMU_HSI0_NOC, "mout_clkcmu_hsi0_noc",
	    mout_clkcmu_hsi0_noc_p, CLK_CON_MUX_MUX_CLKCMU_HSI0_NOC, 0, 2),

	/* HSI1 */
	MUX(MOUT_CLKCMU_HSI1_NOC, "mout_clkcmu_hsi1_noc",
	    mout_clkcmu_hsi1_noc_p, CLK_CON_MUX_MUX_CLKCMU_HSI1_NOC,
	    0, 2),
	MUX(MOUT_CLKCMU_HSI1_USBDRD, "mout_clkcmu_hsi1_usbdrd",
	    mout_clkcmu_hsi1_usbdrd_p, CLK_CON_MUX_MUX_CLKCMU_HSI1_USBDRD,
	    0, 2),
	MUX(MOUT_CLKCMU_HSI1_MMC_CARD, "mout_clkcmu_hsi1_mmc_card",
	    mout_clkcmu_hsi1_mmc_card_p, CLK_CON_MUX_MUX_CLKCMU_HSI1_MMC_CARD,
	    0, 2),

	/* HSI2 */
	MUX(MOUT_CLKCMU_HSI2_NOC, "mout_clkcmu_hsi2_noc",
	    mout_clkcmu_hsi2_noc_p, CLK_CON_MUX_MUX_CLKCMU_HSI2_NOC,
	    0, 2),
	MUX(MOUT_CLKCMU_HSI2_NOC_UFS, "mout_clkcmu_hsi2_noc_ufs",
	    mout_clkcmu_hsi2_noc_ufs_p, CLK_CON_MUX_MUX_CLKCMU_HSI2_NOC_UFS,
	    0, 2),
	MUX(MOUT_CLKCMU_HSI2_UFS_EMBD, "mout_clkcmu_hsi2_ufs_embd",
	    mout_clkcmu_hsi2_ufs_embd_p, CLK_CON_MUX_MUX_CLKCMU_HSI2_UFS_EMBD,
	    0, 2),
	MUX(MOUT_CLKCMU_HSI2_ETHERNET, "mout_clkcmu_hsi2_ethernet",
	    mout_clkcmu_hsi2_ethernet_p, CLK_CON_MUX_MUX_CLKCMU_HSI2_ETHERNET,
	    0, 2),

	/* ISP */
	MUX(MOUT_CLKCMU_ISP_NOC, "mout_clkcmu_isp_noc",
	    mout_clkcmu_isp_noc_p, CLK_CON_MUX_MUX_CLKCMU_ISP_NOC, 0, 3),

	/* M2M */
	MUX(MOUT_CLKCMU_M2M_NOC, "mout_clkcmu_m2m_noc",
	    mout_clkcmu_m2m_noc_p, CLK_CON_MUX_MUX_CLKCMU_M2M_NOC, 0, 2),
	MUX(MOUT_CLKCMU_M2M_JPEG, "mout_clkcmu_m2m_jpeg",
	    mout_clkcmu_m2m_jpeg_p, CLK_CON_MUX_MUX_CLKCMU_M2M_JPEG, 0, 2),

	/* MFC */
	MUX(MOUT_CLKCMU_MFC_MFC, "mout_clkcmu_mfc_mfc",
	    mout_clkcmu_mfc_mfc_p, CLK_CON_MUX_MUX_CLKCMU_MFC_MFC, 0, 2),
	MUX(MOUT_CLKCMU_MFC_WFD, "mout_clkcmu_mfc_wfd",
	    mout_clkcmu_mfc_wfd_p, CLK_CON_MUX_MUX_CLKCMU_MFC_WFD, 0, 2),

	/* MFD */
	MUX(MOUT_CLKCMU_MFD_NOC, "mout_clkcmu_mfd_noc",
	    mout_clkcmu_mfd_noc_p, CLK_CON_MUX_MUX_CLKCMU_MFD_NOC, 0, 3),

	/* MIF */
	MUX(MOUT_CLKCMU_MIF_SWITCH, "mout_clkcmu_mif_switch",
	    mout_clkcmu_mif_switch_p, CLK_CON_MUX_MUX_CLKCMU_MIF_SWITCH, 0, 3),
	MUX(MOUT_CLKCMU_MIF_NOCP, "mout_clkcmu_mif_nocp",
	    mout_clkcmu_mif_nocp_p, CLK_CON_MUX_MUX_CLKCMU_MIF_NOCP, 0, 2),

	/* MISC */
	MUX(MOUT_CLKCMU_MISC_NOC, "mout_clkcmu_misc_noc",
	    mout_clkcmu_misc_noc_p, CLK_CON_MUX_MUX_CLKCMU_MISC_NOC, 0, 2),

	/* NOCL0 */
	MUX(MOUT_CLKCMU_NOCL0_NOC, "mout_clkcmu_nocl0_noc",
	    mout_clkcmu_nocl0_noc_p, CLK_CON_MUX_MUX_CLKCMU_NOCL0_NOC, 0, 3),

	/* NOCL1 */
	MUX(MOUT_CLKCMU_NOCL1_NOC, "mout_clkcmu_nocl1_noc",
	    mout_clkcmu_nocl1_noc_p, CLK_CON_MUX_MUX_CLKCMU_NOCL1_NOC, 0, 3),

	/* NOCL2 */
	MUX(MOUT_CLKCMU_NOCL2_NOC, "mout_clkcmu_nocl2_noc",
	    mout_clkcmu_nocl2_noc_p, CLK_CON_MUX_MUX_CLKCMU_NOCL2_NOC, 0, 3),

	/* PERIC0 */
	MUX(MOUT_CLKCMU_PERIC0_NOC, "mout_clkcmu_peric0_noc",
	    mout_clkcmu_peric0_noc_p, CLK_CON_MUX_MUX_CLKCMU_PERIC0_NOC, 0, 1),
	MUX(MOUT_CLKCMU_PERIC0_IP, "mout_clkcmu_peric0_ip",
	    mout_clkcmu_peric0_ip_p, CLK_CON_MUX_MUX_CLKCMU_PERIC0_IP, 0, 1),

	/* PERIC1 */
	MUX(MOUT_CLKCMU_PERIC1_NOC, "mout_clkcmu_peric1_noc",
	    mout_clkcmu_peric1_noc_p, CLK_CON_MUX_MUX_CLKCMU_PERIC1_NOC, 0, 1),
	MUX(MOUT_CLKCMU_PERIC1_IP, "mout_clkcmu_peric1_ip",
	    mout_clkcmu_peric1_ip_p, CLK_CON_MUX_MUX_CLKCMU_PERIC1_IP, 0, 1),

	/* SDMA */
	MUX(MOUT_CLKCMU_SDMA_NOC, "mout_clkcmu_sdma_noc",
	    mout_clkcmu_sdma_noc_p, CLK_CON_MUX_MUX_CLKCMU_SDMA_NOC, 0, 3),

	/* SNW */
	MUX(MOUT_CLKCMU_SNW_NOC, "mout_clkcmu_snw_noc",
	    mout_clkcmu_snw_noc_p, CLK_CON_MUX_MUX_CLKCMU_SNW_NOC, 0, 3),

	/* SSP */
	MUX(MOUT_CLKCMU_SSP_NOC, "mout_clkcmu_ssp_noc",
	    mout_clkcmu_ssp_noc_p, CLK_CON_MUX_MUX_CLKCMU_SSP_NOC, 0, 2),

	/* TAA */
	MUX(MOUT_CLKCMU_TAA_NOC, "mout_clkcmu_taa_noc",
	    mout_clkcmu_taa_noc_p, CLK_CON_MUX_MUX_CLKCMU_TAA_NOC, 0, 3),
};

static const struct samsung_div_clock top_div_clks[] __initconst = {
	/* CMU_TOP_PURECLKCOMP */

	/* BOOST */
	DIV(DOUT_CLKCMU_CMU_BOOST, "dout_clkcmu_cmu_boost",
	    "mout_clkcmu_cmu_boost", CLK_CON_DIV_DIV_CLKCMU_CMU_BOOST, 0, 2),

	/* ACC */
	DIV(DOUT_CLKCMU_ACC_NOC, "dout_clkcmu_acc_noc",
	    "mout_clkcmu_acc_noc", CLK_CON_DIV_CLKCMU_ACC_NOC, 0, 4),
	DIV(DOUT_CLKCMU_ACC_ORB, "dout_clkcmu_acc_orb",
	    "mout_clkcmu_acc_orb", CLK_CON_DIV_CLKCMU_ACC_ORB, 0, 4),

	/* APM */
	DIV(DOUT_CLKCMU_APM_NOC, "dout_clkcmu_apm_noc",
	    "mout_clkcmu_apm_noc", CLK_CON_DIV_CLKCMU_APM_NOC, 0, 3),

	/* AUD */
	DIV(DOUT_CLKCMU_AUD_CPU, "dout_clkcmu_aud_cpu",
	    "mout_clkcmu_aud_cpu", CLK_CON_DIV_CLKCMU_AUD_CPU, 0, 3),
	DIV(DOUT_CLKCMU_AUD_NOC, "dout_clkcmu_aud_noc",
	    "mout_clkcmu_aud_noc", CLK_CON_DIV_CLKCMU_AUD_NOC, 0, 4),

	/* CPUCL0 */
	DIV(DOUT_CLKCMU_CPUCL0_SWITCH, "dout_clkcmu_cpucl0_switch",
	    "mout_clkcmu_cpucl0_switch",
	    CLK_CON_DIV_CLKCMU_CPUCL0_SWITCH, 0, 3),
	DIV(DOUT_CLKCMU_CPUCL0_CLUSTER, "dout_clkcmu_cpucl0_cluster",
	    "mout_clkcmu_cpucl0_cluster",
	    CLK_CON_DIV_CLKCMU_CPUCL0_CLUSTER, 0, 3),
	DIV(DOUT_CLKCMU_CPUCL0_DBG, "dout_clkcmu_cpucl0_dbg",
	    "mout_clkcmu_cpucl0_dbg",
	    CLK_CON_DIV_CLKCMU_CPUCL0_DBG, 0, 4),

	/* CPUCL1 */
	DIV(DOUT_CLKCMU_CPUCL1_SWITCH, "dout_clkcmu_cpucl1_switch",
	    "mout_clkcmu_cpucl1_switch",
	    CLK_CON_DIV_CLKCMU_CPUCL1_SWITCH, 0, 3),
	DIV(DOUT_CLKCMU_CPUCL1_CLUSTER, "dout_clkcmu_cpucl1_cluster",
	    "mout_clkcmu_cpucl1_cluster",
	    CLK_CON_DIV_CLKCMU_CPUCL1_CLUSTER, 0, 3),

	/* CPUCL2 */
	DIV(DOUT_CLKCMU_CPUCL2_SWITCH, "dout_clkcmu_cpucl2_switch",
	    "mout_clkcmu_cpucl2_switch",
	    CLK_CON_DIV_CLKCMU_CPUCL2_SWITCH, 0, 3),
	DIV(DOUT_CLKCMU_CPUCL2_CLUSTER, "dout_clkcmu_cpucl2_cluster",
	    "mout_clkcmu_cpucl2_cluster",
	    CLK_CON_DIV_CLKCMU_CPUCL2_CLUSTER, 0, 3),

	/* DNC */
	DIV(DOUT_CLKCMU_DNC_NOC, "dout_clkcmu_dnc_noc",
	    "mout_clkcmu_dnc_noc", CLK_CON_DIV_CLKCMU_DNC_NOC, 0, 4),

	/* DPTX */
	DIV(DOUT_CLKCMU_DPTX_NOC, "dout_clkcmu_dptx_noc",
	    "mout_clkcmu_dptx_noc", CLK_CON_DIV_CLKCMU_DPTX_NOC, 0, 4),
	DIV(DOUT_CLKCMU_DPTX_DPGTC, "dout_clkcmu_dptx_dpgtc",
	    "mout_clkcmu_dptx_dpgtc", CLK_CON_DIV_CLKCMU_DPTX_DPGTC, 0, 3),
	DIV(DOUT_CLKCMU_DPTX_DPOSC, "dout_clkcmu_dptx_dposc",
	    "mout_clkcmu_dptx_dposc", CLK_CON_DIV_CLKCMU_DPTX_DPOSC, 0, 5),

	/* DPUB */
	DIV(DOUT_CLKCMU_DPUB_NOC, "dout_clkcmu_dpub_noc",
	    "mout_clkcmu_dpub_noc", CLK_CON_DIV_CLKCMU_DPUB_NOC, 0, 4),
	DIV(DOUT_CLKCMU_DPUB_DSIM, "dout_clkcmu_dpub_dsim",
	    "mout_clkcmu_dpub_dsim", CLK_CON_DIV_CLKCMU_DPUB_DSIM, 0, 4),

	/* DPUF */
	DIV(DOUT_CLKCMU_DPUF0_NOC, "dout_clkcmu_dpuf0_noc",
	    "mout_clkcmu_dpuf0_noc", CLK_CON_DIV_CLKCMU_DPUF0_NOC, 0, 4),
	DIV(DOUT_CLKCMU_DPUF1_NOC, "dout_clkcmu_dpuf1_noc",
	    "mout_clkcmu_dpuf1_noc", CLK_CON_DIV_CLKCMU_DPUF1_NOC, 0, 4),
	DIV(DOUT_CLKCMU_DPUF2_NOC, "dout_clkcmu_dpuf2_noc",
	    "mout_clkcmu_dpuf2_noc", CLK_CON_DIV_CLKCMU_DPUF2_NOC, 0, 4),

	/* DSP */
	DIV(DOUT_CLKCMU_DSP_NOC, "dout_clkcmu_dsp_noc",
	    "mout_clkcmu_dsp_noc", CLK_CON_DIV_CLKCMU_DSP_NOC, 0, 4),

	/* G3D */
	DIV(DOUT_CLKCMU_G3D_SWITCH, "dout_clkcmu_g3d_switch",
	    "mout_clkcmu_g3d_switch", CLK_CON_DIV_CLKCMU_G3D_SWITCH, 0, 3),
	DIV(DOUT_CLKCMU_G3D_NOCP, "dout_clkcmu_g3d_nocp",
	    "mout_clkcmu_g3d_nocp", CLK_CON_DIV_CLKCMU_G3D_NOCP, 0, 3),

	/* GNPU */
	DIV(DOUT_CLKCMU_GNPU_NOC, "dout_clkcmu_gnpu_noc",
	    "mout_clkcmu_gnpu_noc", CLK_CON_DIV_CLKCMU_GNPU_NOC, 0, 4),

	/* HSI0 */
	DIV(DOUT_CLKCMU_HSI0_NOC, "dout_clkcmu_hsi0_noc",
	    "mout_clkcmu_hsi0_noc", CLK_CON_DIV_CLKCMU_HSI0_NOC, 0, 4),

	/* HSI1 */
	DIV(DOUT_CLKCMU_HSI1_NOC, "dout_clkcmu_hsi1_noc",
	    "mout_clkcmu_hsi1_noc", CLK_CON_DIV_CLKCMU_HSI1_NOC, 0, 4),
	DIV(DOUT_CLKCMU_HSI1_USBDRD, "dout_clkcmu_hsi1_usbdrd",
	    "mout_clkcmu_hsi1_usbdrd", CLK_CON_DIV_CLKCMU_HSI1_USBDRD, 0, 4),
	DIV(DOUT_CLKCMU_HSI1_MMC_CARD, "dout_clkcmu_hsi1_mmc_card",
	    "mout_clkcmu_hsi1_mmc_card", CLK_CON_DIV_CLKCMU_HSI1_MMC_CARD, 0, 9),

	/* HSI2 */
	DIV(DOUT_CLKCMU_HSI2_NOC, "dout_clkcmu_hsi2_noc",
	    "mout_clkcmu_hsi2_noc", CLK_CON_DIV_CLKCMU_HSI2_NOC, 0, 4),
	DIV(DOUT_CLKCMU_HSI2_NOC_UFS, "dout_clkcmu_hsi2_noc_ufs",
	    "mout_clkcmu_hsi2_noc_ufs", CLK_CON_DIV_CLKCMU_HSI2_NOC_UFS, 0, 4),
	DIV(DOUT_CLKCMU_HSI2_UFS_EMBD, "dout_clkcmu_hsi2_ufs_embd",
	    "mout_clkcmu_hsi2_ufs_embd", CLK_CON_DIV_CLKCMU_HSI2_UFS_EMBD, 0, 3),
	DIV(DOUT_CLKCMU_HSI2_ETHERNET, "dout_clkcmu_hsi2_ethernet",
	    "mout_clkcmu_hsi2_ethernet", CLK_CON_DIV_CLKCMU_HSI2_ETHERNET, 0, 3),

	/* ISP */
	DIV(DOUT_CLKCMU_ISP_NOC, "dout_clkcmu_isp_noc",
	    "mout_clkcmu_isp_noc", CLK_CON_DIV_CLKCMU_ISP_NOC, 0, 4),

	/* M2M */
	DIV(DOUT_CLKCMU_M2M_NOC, "dout_clkcmu_m2m_noc",
	    "mout_clkcmu_m2m_noc", CLK_CON_DIV_CLKCMU_M2M_NOC, 0, 4),
	DIV(DOUT_CLKCMU_M2M_JPEG, "dout_clkcmu_m2m_jpeg",
	    "mout_clkcmu_m2m_jpeg", CLK_CON_DIV_CLKCMU_M2M_JPEG, 0, 4),

	/* MFC */
	DIV(DOUT_CLKCMU_MFC_MFC, "dout_clkcmu_mfc_mfc",
	    "mout_clkcmu_mfc_mfc", CLK_CON_DIV_CLKCMU_MFC_MFC, 0, 4),
	DIV(DOUT_CLKCMU_MFC_WFD, "dout_clkcmu_mfc_wfd",
	    "mout_clkcmu_mfc_wfd", CLK_CON_DIV_CLKCMU_MFC_WFD, 0, 4),

	/* MFD */
	DIV(DOUT_CLKCMU_MFD_NOC, "dout_clkcmu_mfd_noc",
	    "mout_clkcmu_mfd_noc", CLK_CON_DIV_CLKCMU_MFD_NOC, 0, 4),

	/* MIF */
	DIV(DOUT_CLKCMU_MIF_NOCP, "dout_clkcmu_mif_nocp",
	    "mout_clkcmu_mif_nocp", CLK_CON_DIV_CLKCMU_MIF_NOCP, 0, 4),

	/* MISC */
	DIV(DOUT_CLKCMU_MISC_NOC, "dout_clkcmu_misc_noc",
	    "mout_clkcmu_misc_noc", CLK_CON_DIV_CLKCMU_MISC_NOC, 0, 4),

	/* NOCL0 */
	DIV(DOUT_CLKCMU_NOCL0_NOC, "dout_clkcmu_nocl0_noc",
	    "mout_clkcmu_nocl0_noc", CLK_CON_DIV_CLKCMU_NOCL0_NOC, 0, 4),

	/* NOCL1 */
	DIV(DOUT_CLKCMU_NOCL1_NOC, "dout_clkcmu_nocl1_noc",
	    "mout_clkcmu_nocl1_noc", CLK_CON_DIV_CLKCMU_NOCL1_NOC, 0, 4),

	/* NOCL2 */
	DIV(DOUT_CLKCMU_NOCL2_NOC, "dout_clkcmu_nocl2_noc",
	    "mout_clkcmu_nocl2_noc", CLK_CON_DIV_CLKCMU_NOCL2_NOC, 0, 4),

	/* PERIC0 */
	DIV(DOUT_CLKCMU_PERIC0_NOC, "dout_clkcmu_peric0_noc",
	    "mout_clkcmu_peric0_noc", CLK_CON_DIV_CLKCMU_PERIC0_NOC, 0, 4),
	DIV(DOUT_CLKCMU_PERIC0_IP, "dout_clkcmu_peric0_ip",
	    "mout_clkcmu_peric0_ip", CLK_CON_DIV_CLKCMU_PERIC0_IP, 0, 4),

	/* PERIC1 */
	DIV(DOUT_CLKCMU_PERIC1_NOC, "dout_clkcmu_peric1_noc",
	    "mout_clkcmu_peric1_noc", CLK_CON_DIV_CLKCMU_PERIC1_NOC, 0, 4),
	DIV(DOUT_CLKCMU_PERIC1_IP, "dout_clkcmu_peric1_ip",
	    "mout_clkcmu_peric1_ip", CLK_CON_DIV_CLKCMU_PERIC1_IP, 0, 4),

	/* SDMA */
	DIV(DOUT_CLKCMU_SDMA_NOC, "dout_clkcmu_sdma_noc",
	    "mout_clkcmu_sdma_noc", CLK_CON_DIV_CLKCMU_SDMA_NOC, 0, 4),

	/* SNW */
	DIV(DOUT_CLKCMU_SNW_NOC, "dout_clkcmu_snw_noc",
	    "mout_clkcmu_snw_noc", CLK_CON_DIV_CLKCMU_SNW_NOC, 0, 4),

	/* SSP */
	DIV(DOUT_CLKCMU_SSP_NOC, "dout_clkcmu_ssp_noc",
	    "mout_clkcmu_ssp_noc", CLK_CON_DIV_CLKCMU_SSP_NOC, 0, 4),

	/* TAA */
	DIV(DOUT_CLKCMU_TAA_NOC, "dout_clkcmu_taa_noc",
	    "mout_clkcmu_taa_noc", CLK_CON_DIV_CLKCMU_TAA_NOC, 0, 4),
};

static const struct samsung_fixed_factor_clock top_fixed_factor_clks[] __initconst = {
	FFACTOR(DOUT_SHARED0_DIV1, "dout_shared0_div1",
		"mout_shared0_pll", 1, 1, 0),
	FFACTOR(DOUT_SHARED0_DIV2, "dout_shared0_div2",
		"mout_shared0_pll", 1, 2, 0),
	FFACTOR(DOUT_SHARED0_DIV3, "dout_shared0_div3",
		"mout_shared0_pll", 1, 3, 0),
	FFACTOR(DOUT_SHARED0_DIV4, "dout_shared0_div4",
		"mout_shared0_pll", 1, 4, 0),
	FFACTOR(DOUT_SHARED1_DIV1, "dout_shared1_div1",
		"mout_shared1_pll", 1, 1, 0),
	FFACTOR(DOUT_SHARED1_DIV2, "dout_shared1_div2",
		"mout_shared1_pll", 1, 2, 0),
	FFACTOR(DOUT_SHARED1_DIV3, "dout_shared1_div3",
		"mout_shared1_pll", 1, 3, 0),
	FFACTOR(DOUT_SHARED1_DIV4, "dout_shared1_div4",
		"mout_shared1_pll", 1, 4, 0),
	FFACTOR(DOUT_SHARED2_DIV1, "dout_shared2_div1",
		"mout_shared2_pll", 1, 1, 0),
	FFACTOR(DOUT_SHARED2_DIV2, "dout_shared2_div2",
		"mout_shared2_pll", 1, 2, 0),
	FFACTOR(DOUT_SHARED2_DIV3, "dout_shared2_div3",
		"mout_shared2_pll", 1, 3, 0),
	FFACTOR(DOUT_SHARED2_DIV4, "dout_shared2_div4",
		"mout_shared2_pll", 1, 4, 0),
	FFACTOR(DOUT_SHARED3_DIV1, "dout_shared3_div1",
		"mout_shared3_pll", 1, 1, 0),
	FFACTOR(DOUT_SHARED3_DIV2, "dout_shared3_div2",
		"mout_shared3_pll", 1, 2, 0),
	FFACTOR(DOUT_SHARED3_DIV3, "dout_shared3_div3",
		"mout_shared3_pll", 1, 3, 0),
	FFACTOR(DOUT_SHARED3_DIV4, "dout_shared3_div4",
		"mout_shared3_pll", 1, 4, 0),
	FFACTOR(DOUT_SHARED4_DIV1, "dout_shared4_div1",
		"mout_shared4_pll", 1, 1, 0),
	FFACTOR(DOUT_SHARED4_DIV2, "dout_shared4_div2",
		"mout_shared4_pll", 1, 2, 0),
	FFACTOR(DOUT_SHARED4_DIV3, "dout_shared4_div3",
		"mout_shared4_pll", 1, 3, 0),
	FFACTOR(DOUT_SHARED4_DIV4, "dout_shared4_div4",
		"mout_shared4_pll", 1, 4, 0),
	FFACTOR(DOUT_SHARED5_DIV1, "dout_shared5_div1",
		"mout_shared5_pll", 1, 1, 0),
	FFACTOR(DOUT_SHARED5_DIV2, "dout_shared5_div2",
		"mout_shared5_pll", 1, 2, 0),
	FFACTOR(DOUT_SHARED5_DIV3, "dout_shared5_div3",
		"mout_shared5_pll", 1, 3, 0),
	FFACTOR(DOUT_SHARED5_DIV4, "dout_shared5_div4",
		"mout_shared5_pll", 1, 4, 0),
	FFACTOR(DOUT_TCXO_DIV2, "dout_tcxo_div2",
		"oscclk", 1, 2, 0),
};

static const struct samsung_cmu_info top_cmu_info __initconst = {
	.pll_clks		= top_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(top_pll_clks),
	.mux_clks		= top_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(top_mux_clks),
	.div_clks		= top_div_clks,
	.nr_div_clks		= ARRAY_SIZE(top_div_clks),
	.fixed_factor_clks	= top_fixed_factor_clks,
	.nr_fixed_factor_clks	= ARRAY_SIZE(top_fixed_factor_clks),
	.nr_clk_ids		= CLKS_NR_TOP,
	.clk_regs		= top_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(top_clk_regs),
};

static void __init exynosautov920_cmu_top_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &top_cmu_info);
}

/* Register CMU_TOP early, as it's a dependency for other early domains */
CLK_OF_DECLARE(exynosautov920_cmu_top, "samsung,exynosautov920-cmu-top",
	       exynosautov920_cmu_top_init);

/* ---- CMU_PERIC0 --------------------------------------------------------- */

/* Register Offset definitions for CMU_PERIC0 (0x10800000) */
#define PLL_CON0_MUX_CLKCMU_PERIC0_IP_USER	0x0600
#define PLL_CON0_MUX_CLKCMU_PERIC0_NOC_USER	0x0610
#define CLK_CON_MUX_MUX_CLK_PERIC0_I3C		0x1000
#define CLK_CON_MUX_MUX_CLK_PERIC0_USI00_USI	0x1004
#define CLK_CON_MUX_MUX_CLK_PERIC0_USI01_USI	0x1008
#define CLK_CON_MUX_MUX_CLK_PERIC0_USI02_USI	0x100c
#define CLK_CON_MUX_MUX_CLK_PERIC0_USI03_USI	0x1010
#define CLK_CON_MUX_MUX_CLK_PERIC0_USI04_USI	0x1014
#define CLK_CON_MUX_MUX_CLK_PERIC0_USI05_USI	0x1018
#define CLK_CON_MUX_MUX_CLK_PERIC0_USI06_USI	0x101c
#define CLK_CON_MUX_MUX_CLK_PERIC0_USI07_USI	0x1020
#define CLK_CON_MUX_MUX_CLK_PERIC0_USI08_USI	0x1024
#define CLK_CON_MUX_MUX_CLK_PERIC0_USI_I2C	0x1028
#define CLK_CON_DIV_DIV_CLK_PERIC0_I3C		0x1800
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI00_USI	0x1804
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI01_USI	0x1808
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI02_USI	0x180c
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI03_USI	0x1810
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI04_USI	0x1814
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI05_USI	0x1818
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI06_USI	0x181c
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI07_USI	0x1820
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI08_USI	0x1824
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI_I2C	0x1828

static const unsigned long peric0_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_PERIC0_IP_USER,
	PLL_CON0_MUX_CLKCMU_PERIC0_NOC_USER,
	CLK_CON_MUX_MUX_CLK_PERIC0_I3C,
	CLK_CON_MUX_MUX_CLK_PERIC0_USI00_USI,
	CLK_CON_MUX_MUX_CLK_PERIC0_USI01_USI,
	CLK_CON_MUX_MUX_CLK_PERIC0_USI02_USI,
	CLK_CON_MUX_MUX_CLK_PERIC0_USI03_USI,
	CLK_CON_MUX_MUX_CLK_PERIC0_USI04_USI,
	CLK_CON_MUX_MUX_CLK_PERIC0_USI05_USI,
	CLK_CON_MUX_MUX_CLK_PERIC0_USI06_USI,
	CLK_CON_MUX_MUX_CLK_PERIC0_USI07_USI,
	CLK_CON_MUX_MUX_CLK_PERIC0_USI08_USI,
	CLK_CON_MUX_MUX_CLK_PERIC0_USI_I2C,
	CLK_CON_DIV_DIV_CLK_PERIC0_I3C,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI00_USI,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI01_USI,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI02_USI,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI03_USI,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI04_USI,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI05_USI,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI06_USI,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI07_USI,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI08_USI,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI_I2C,
};

/* List of parent clocks for Muxes in CMU_PERIC0 */
PNAME(mout_peric0_ip_user_p) = { "oscclk", "dout_clkcmu_peric0_ip" };
PNAME(mout_peric0_noc_user_p) = { "oscclk", "dout_clkcmu_peric0_noc" };
PNAME(mout_peric0_usi_p) = { "oscclk", "mout_peric0_ip_user" };

static const struct samsung_mux_clock peric0_mux_clks[] __initconst = {
	MUX(CLK_MOUT_PERIC0_IP_USER, "mout_peric0_ip_user",
	    mout_peric0_ip_user_p, PLL_CON0_MUX_CLKCMU_PERIC0_IP_USER, 4, 1),
	MUX(CLK_MOUT_PERIC0_NOC_USER, "mout_peric0_noc_user",
	    mout_peric0_noc_user_p, PLL_CON0_MUX_CLKCMU_PERIC0_NOC_USER, 4, 1),
	/* USI00 ~ USI08 */
	MUX(CLK_MOUT_PERIC0_USI00_USI, "mout_peric0_usi00_usi",
	    mout_peric0_usi_p, CLK_CON_MUX_MUX_CLK_PERIC0_USI00_USI, 0, 1),
	MUX(CLK_MOUT_PERIC0_USI01_USI, "mout_peric0_usi01_usi",
	    mout_peric0_usi_p, CLK_CON_MUX_MUX_CLK_PERIC0_USI01_USI, 0, 1),
	MUX(CLK_MOUT_PERIC0_USI02_USI, "mout_peric0_usi02_usi",
	    mout_peric0_usi_p, CLK_CON_MUX_MUX_CLK_PERIC0_USI02_USI, 0, 1),
	MUX(CLK_MOUT_PERIC0_USI03_USI, "mout_peric0_usi03_usi",
	    mout_peric0_usi_p, CLK_CON_MUX_MUX_CLK_PERIC0_USI03_USI, 0, 1),
	MUX(CLK_MOUT_PERIC0_USI04_USI, "mout_peric0_usi04_usi",
	    mout_peric0_usi_p, CLK_CON_MUX_MUX_CLK_PERIC0_USI04_USI, 0, 1),
	MUX(CLK_MOUT_PERIC0_USI05_USI, "mout_peric0_usi05_usi",
	    mout_peric0_usi_p, CLK_CON_MUX_MUX_CLK_PERIC0_USI05_USI, 0, 1),
	MUX(CLK_MOUT_PERIC0_USI06_USI, "mout_peric0_usi06_usi",
	    mout_peric0_usi_p, CLK_CON_MUX_MUX_CLK_PERIC0_USI06_USI, 0, 1),
	MUX(CLK_MOUT_PERIC0_USI07_USI, "mout_peric0_usi07_usi",
	    mout_peric0_usi_p, CLK_CON_MUX_MUX_CLK_PERIC0_USI07_USI, 0, 1),
	MUX(CLK_MOUT_PERIC0_USI08_USI, "mout_peric0_usi08_usi",
	    mout_peric0_usi_p, CLK_CON_MUX_MUX_CLK_PERIC0_USI08_USI, 0, 1),
	/* USI_I2C */
	MUX(CLK_MOUT_PERIC0_USI_I2C, "mout_peric0_usi_i2c",
	    mout_peric0_usi_p, CLK_CON_MUX_MUX_CLK_PERIC0_USI_I2C, 0, 1),
	/* USI_I3C */
	MUX(CLK_MOUT_PERIC0_I3C, "mout_peric0_i3c",
	    mout_peric0_usi_p, CLK_CON_MUX_MUX_CLK_PERIC0_I3C, 0, 1),
};

static const struct samsung_div_clock peric0_div_clks[] __initconst = {
	/* USI00 ~ USI08 */
	DIV(CLK_DOUT_PERIC0_USI00_USI, "dout_peric0_usi00_usi",
	    "mout_peric0_usi00_usi", CLK_CON_DIV_DIV_CLK_PERIC0_USI00_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC0_USI01_USI, "dout_peric0_usi01_usi",
	    "mout_peric0_usi01_usi", CLK_CON_DIV_DIV_CLK_PERIC0_USI01_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC0_USI02_USI, "dout_peric0_usi02_usi",
	    "mout_peric0_usi02_usi", CLK_CON_DIV_DIV_CLK_PERIC0_USI02_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC0_USI03_USI, "dout_peric0_usi03_usi",
	    "mout_peric0_usi03_usi", CLK_CON_DIV_DIV_CLK_PERIC0_USI03_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC0_USI04_USI, "dout_peric0_usi04_usi",
	    "mout_peric0_usi04_usi", CLK_CON_DIV_DIV_CLK_PERIC0_USI04_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC0_USI05_USI, "dout_peric0_usi05_usi",
	    "mout_peric0_usi05_usi", CLK_CON_DIV_DIV_CLK_PERIC0_USI05_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC0_USI06_USI, "dout_peric0_usi06_usi",
	    "mout_peric0_usi06_usi", CLK_CON_DIV_DIV_CLK_PERIC0_USI06_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC0_USI07_USI, "dout_peric0_usi07_usi",
	    "mout_peric0_usi07_usi", CLK_CON_DIV_DIV_CLK_PERIC0_USI07_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC0_USI08_USI, "dout_peric0_usi08_usi",
	    "mout_peric0_usi08_usi", CLK_CON_DIV_DIV_CLK_PERIC0_USI08_USI,
	    0, 4),
	/* USI_I2C */
	DIV(CLK_DOUT_PERIC0_USI_I2C, "dout_peric0_usi_i2c",
	    "mout_peric0_usi_i2c", CLK_CON_DIV_DIV_CLK_PERIC0_USI_I2C, 0, 4),
	/* USI_I3C */
	DIV(CLK_DOUT_PERIC0_I3C, "dout_peric0_i3c",
	    "mout_peric0_i3c", CLK_CON_DIV_DIV_CLK_PERIC0_I3C, 0, 4),
};

static const struct samsung_cmu_info peric0_cmu_info __initconst = {
	.mux_clks		= peric0_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(peric0_mux_clks),
	.div_clks		= peric0_div_clks,
	.nr_div_clks		= ARRAY_SIZE(peric0_div_clks),
	.nr_clk_ids		= CLKS_NR_PERIC0,
	.clk_regs		= peric0_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(peric0_clk_regs),
	.clk_name		= "noc",
};

/* ---- CMU_PERIC1 --------------------------------------------------------- */

/* Register Offset definitions for CMU_PERIC1 (0x10C00000) */
#define PLL_CON0_MUX_CLKCMU_PERIC1_IP_USER	0x600
#define PLL_CON0_MUX_CLKCMU_PERIC1_NOC_USER	0x610
#define CLK_CON_MUX_MUX_CLK_PERIC1_I3C		0x1000
#define CLK_CON_MUX_MUX_CLK_PERIC1_USI09_USI	0x1004
#define CLK_CON_MUX_MUX_CLK_PERIC1_USI10_USI	0x1008
#define CLK_CON_MUX_MUX_CLK_PERIC1_USI11_USI	0x100c
#define CLK_CON_MUX_MUX_CLK_PERIC1_USI12_USI	0x1010
#define CLK_CON_MUX_MUX_CLK_PERIC1_USI13_USI	0x1014
#define CLK_CON_MUX_MUX_CLK_PERIC1_USI14_USI	0x1018
#define CLK_CON_MUX_MUX_CLK_PERIC1_USI15_USI	0x101c
#define CLK_CON_MUX_MUX_CLK_PERIC1_USI16_USI	0x1020
#define CLK_CON_MUX_MUX_CLK_PERIC1_USI17_USI	0x1024
#define CLK_CON_MUX_MUX_CLK_PERIC1_USI_I2C	0x1028
#define CLK_CON_DIV_DIV_CLK_PERIC1_I3C		0x1800
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI09_USI	0x1804
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI10_USI	0x1808
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI11_USI	0x180c
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI12_USI	0x1810
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI13_USI	0x1814
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI14_USI	0x1818
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI15_USI	0x181c
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI16_USI	0x1820
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI17_USI	0x1824
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI_I2C	0x1828

static const unsigned long peric1_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_PERIC1_IP_USER,
	PLL_CON0_MUX_CLKCMU_PERIC1_NOC_USER,
	CLK_CON_MUX_MUX_CLK_PERIC1_I3C,
	CLK_CON_MUX_MUX_CLK_PERIC1_USI09_USI,
	CLK_CON_MUX_MUX_CLK_PERIC1_USI10_USI,
	CLK_CON_MUX_MUX_CLK_PERIC1_USI11_USI,
	CLK_CON_MUX_MUX_CLK_PERIC1_USI12_USI,
	CLK_CON_MUX_MUX_CLK_PERIC1_USI13_USI,
	CLK_CON_MUX_MUX_CLK_PERIC1_USI14_USI,
	CLK_CON_MUX_MUX_CLK_PERIC1_USI15_USI,
	CLK_CON_MUX_MUX_CLK_PERIC1_USI16_USI,
	CLK_CON_MUX_MUX_CLK_PERIC1_USI17_USI,
	CLK_CON_MUX_MUX_CLK_PERIC1_USI_I2C,
	CLK_CON_DIV_DIV_CLK_PERIC1_I3C,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI09_USI,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI10_USI,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI11_USI,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI12_USI,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI13_USI,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI14_USI,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI15_USI,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI16_USI,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI17_USI,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI_I2C,
};

/* List of parent clocks for Muxes in CMU_PERIC1 */
PNAME(mout_peric1_ip_user_p) = { "oscclk", "dout_clkcmu_peric1_ip" };
PNAME(mout_peric1_noc_user_p) = { "oscclk", "dout_clkcmu_peric1_noc" };
PNAME(mout_peric1_usi_p) = { "oscclk", "mout_peric1_ip_user" };

static const struct samsung_mux_clock peric1_mux_clks[] __initconst = {
	MUX(CLK_MOUT_PERIC1_IP_USER, "mout_peric1_ip_user",
	    mout_peric1_ip_user_p, PLL_CON0_MUX_CLKCMU_PERIC1_IP_USER, 4, 1),
	MUX(CLK_MOUT_PERIC1_NOC_USER, "mout_peric1_noc_user",
	    mout_peric1_noc_user_p, PLL_CON0_MUX_CLKCMU_PERIC1_NOC_USER, 4, 1),
	/* USI09 ~ USI17 */
	MUX(CLK_MOUT_PERIC1_USI09_USI, "mout_peric1_usi09_usi",
	    mout_peric1_usi_p, CLK_CON_MUX_MUX_CLK_PERIC1_USI09_USI, 0, 1),
	MUX(CLK_MOUT_PERIC1_USI10_USI, "mout_peric1_usi10_usi",
	    mout_peric1_usi_p, CLK_CON_MUX_MUX_CLK_PERIC1_USI10_USI, 0, 1),
	MUX(CLK_MOUT_PERIC1_USI11_USI, "mout_peric1_usi11_usi",
	    mout_peric1_usi_p, CLK_CON_MUX_MUX_CLK_PERIC1_USI11_USI, 0, 1),
	MUX(CLK_MOUT_PERIC1_USI12_USI, "mout_peric1_usi12_usi",
	    mout_peric1_usi_p, CLK_CON_MUX_MUX_CLK_PERIC1_USI12_USI, 0, 1),
	MUX(CLK_MOUT_PERIC1_USI13_USI, "mout_peric1_usi13_usi",
	    mout_peric1_usi_p, CLK_CON_MUX_MUX_CLK_PERIC1_USI13_USI, 0, 1),
	MUX(CLK_MOUT_PERIC1_USI14_USI, "mout_peric1_usi14_usi",
	    mout_peric1_usi_p, CLK_CON_MUX_MUX_CLK_PERIC1_USI14_USI, 0, 1),
	MUX(CLK_MOUT_PERIC1_USI15_USI, "mout_peric1_usi15_usi",
	    mout_peric1_usi_p, CLK_CON_MUX_MUX_CLK_PERIC1_USI15_USI, 0, 1),
	MUX(CLK_MOUT_PERIC1_USI16_USI, "mout_peric1_usi16_usi",
	    mout_peric1_usi_p, CLK_CON_MUX_MUX_CLK_PERIC1_USI16_USI, 0, 1),
	MUX(CLK_MOUT_PERIC1_USI17_USI, "mout_peric1_usi17_usi",
	    mout_peric1_usi_p, CLK_CON_MUX_MUX_CLK_PERIC1_USI17_USI, 0, 1),
	/* USI_I2C */
	MUX(CLK_MOUT_PERIC1_USI_I2C, "mout_peric1_usi_i2c",
	    mout_peric1_usi_p, CLK_CON_MUX_MUX_CLK_PERIC1_USI_I2C, 0, 1),
	/* USI_I3C */
	MUX(CLK_MOUT_PERIC1_I3C, "mout_peric1_i3c",
	    mout_peric1_usi_p, CLK_CON_MUX_MUX_CLK_PERIC1_I3C, 0, 1),
};

static const struct samsung_div_clock peric1_div_clks[] __initconst = {
	/* USI09 ~ USI17 */
	DIV(CLK_DOUT_PERIC1_USI09_USI, "dout_peric1_usi09_usi",
	    "mout_peric1_usi09_usi", CLK_CON_DIV_DIV_CLK_PERIC1_USI09_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC1_USI10_USI, "dout_peric1_usi10_usi",
	    "mout_peric1_usi10_usi", CLK_CON_DIV_DIV_CLK_PERIC1_USI10_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC1_USI11_USI, "dout_peric1_usi11_usi",
	    "mout_peric1_usi11_usi", CLK_CON_DIV_DIV_CLK_PERIC1_USI11_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC1_USI12_USI, "dout_peric1_usi12_usi",
	    "mout_peric1_usi12_usi", CLK_CON_DIV_DIV_CLK_PERIC1_USI12_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC1_USI13_USI, "dout_peric1_usi13_usi",
	    "mout_peric1_usi13_usi", CLK_CON_DIV_DIV_CLK_PERIC1_USI13_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC1_USI14_USI, "dout_peric1_usi14_usi",
	    "mout_peric1_usi14_usi", CLK_CON_DIV_DIV_CLK_PERIC1_USI14_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC1_USI15_USI, "dout_peric1_usi15_usi",
	    "mout_peric1_usi15_usi", CLK_CON_DIV_DIV_CLK_PERIC1_USI15_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC1_USI16_USI, "dout_peric1_usi16_usi",
	    "mout_peric1_usi16_usi", CLK_CON_DIV_DIV_CLK_PERIC1_USI16_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC1_USI17_USI, "dout_peric1_usi17_usi",
	    "mout_peric1_usi17_usi", CLK_CON_DIV_DIV_CLK_PERIC1_USI17_USI,
	    0, 4),
	/* USI_I2C */
	DIV(CLK_DOUT_PERIC1_USI_I2C, "dout_peric1_usi_i2c",
	    "mout_peric1_usi_i2c", CLK_CON_DIV_DIV_CLK_PERIC1_USI_I2C, 0, 4),
	/* USI_I3C */
	DIV(CLK_DOUT_PERIC1_I3C, "dout_peric1_i3c",
	    "mout_peric1_i3c", CLK_CON_DIV_DIV_CLK_PERIC1_I3C, 0, 4),
};

static const struct samsung_cmu_info peric1_cmu_info __initconst = {
	.mux_clks		= peric1_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(peric1_mux_clks),
	.div_clks		= peric1_div_clks,
	.nr_div_clks		= ARRAY_SIZE(peric1_div_clks),
	.nr_clk_ids		= CLKS_NR_PERIC1,
	.clk_regs		= peric1_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(peric1_clk_regs),
	.clk_name		= "noc",
};

/* ---- CMU_MISC --------------------------------------------------------- */

/* Register Offset definitions for CMU_MISC (0x10020000) */
#define PLL_CON0_MUX_CLKCMU_MISC_NOC_USER	0x600
#define CLK_CON_MUX_MUX_CLK_MISC_GIC		0x1000
#define CLK_CON_DIV_CLKCMU_OTP			0x1800
#define CLK_CON_DIV_DIV_CLK_MISC_NOCP		0x1804
#define CLK_CON_DIV_DIV_CLK_MISC_OSC_DIV2	0x1808

static const unsigned long misc_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_MISC_NOC_USER,
	CLK_CON_MUX_MUX_CLK_MISC_GIC,
	CLK_CON_DIV_CLKCMU_OTP,
	CLK_CON_DIV_DIV_CLK_MISC_NOCP,
	CLK_CON_DIV_DIV_CLK_MISC_OSC_DIV2,
};

/* List of parent clocks for Muxes in CMU_MISC */
PNAME(mout_misc_noc_user_p) = { "oscclk", "dout_clkcmu_misc_noc" };
PNAME(mout_misc_gic_p) = { "dout_misc_nocp", "oscclk" };

static const struct samsung_mux_clock misc_mux_clks[] __initconst = {
	MUX(CLK_MOUT_MISC_NOC_USER, "mout_misc_noc_user",
	    mout_misc_noc_user_p, PLL_CON0_MUX_CLKCMU_MISC_NOC_USER, 4, 1),
	MUX(CLK_MOUT_MISC_GIC, "mout_misc_gic",
	    mout_misc_gic_p, CLK_CON_MUX_MUX_CLK_MISC_GIC, 0, 1),
};

static const struct samsung_div_clock misc_div_clks[] __initconst = {
	DIV(CLK_DOUT_MISC_NOCP, "dout_misc_nocp",
	    "mout_misc_noc_user", CLK_CON_DIV_DIV_CLK_MISC_NOCP,
	    0, 3),
};

static const struct samsung_fixed_factor_clock misc_fixed_factor_clks[] __initconst = {
	FFACTOR(CLK_DOUT_MISC_OTP, "dout_misc_otp",
		"oscclk", 1, 10, 0),
	FFACTOR(CLK_DOUT_MISC_OSC_DIV2, "dout_misc_osc_div2",
		"oscclk", 1, 2, 0),
};

static const struct samsung_cmu_info misc_cmu_info __initconst = {
	.mux_clks		= misc_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(misc_mux_clks),
	.div_clks		= misc_div_clks,
	.nr_div_clks		= ARRAY_SIZE(misc_div_clks),
	.fixed_factor_clks	= misc_fixed_factor_clks,
	.nr_fixed_factor_clks	= ARRAY_SIZE(misc_fixed_factor_clks),
	.nr_clk_ids		= CLKS_NR_MISC,
	.clk_regs		= misc_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(misc_clk_regs),
	.clk_name		= "noc",
};

/* ---- CMU_HSI0 --------------------------------------------------------- */

/* Register Offset definitions for CMU_HSI0 (0x16000000) */
#define PLL_CON0_MUX_CLKCMU_HSI0_NOC_USER	0x600
#define CLK_CON_DIV_DIV_CLK_HSI0_PCIE_APB	0x1800

static const unsigned long hsi0_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_HSI0_NOC_USER,
	CLK_CON_DIV_DIV_CLK_HSI0_PCIE_APB,
};

/* List of parent clocks for Muxes in CMU_HSI0 */
PNAME(mout_hsi0_noc_user_p) = { "oscclk", "dout_clkcmu_hsi0_noc" };

static const struct samsung_mux_clock hsi0_mux_clks[] __initconst = {
	MUX(CLK_MOUT_HSI0_NOC_USER, "mout_hsi0_noc_user",
	    mout_hsi0_noc_user_p, PLL_CON0_MUX_CLKCMU_HSI0_NOC_USER, 4, 1),
};

static const struct samsung_div_clock hsi0_div_clks[] __initconst = {
	DIV(CLK_DOUT_HSI0_PCIE_APB, "dout_hsi0_pcie_apb",
	    "mout_hsi0_noc_user", CLK_CON_DIV_DIV_CLK_HSI0_PCIE_APB,
	    0, 4),
};

static const struct samsung_cmu_info hsi0_cmu_info __initconst = {
	.mux_clks		= hsi0_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(hsi0_mux_clks),
	.div_clks		= hsi0_div_clks,
	.nr_div_clks		= ARRAY_SIZE(hsi0_div_clks),
	.nr_clk_ids		= CLKS_NR_HSI0,
	.clk_regs		= hsi0_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(hsi0_clk_regs),
	.clk_name		= "noc",
};

/* ---- CMU_HSI1 --------------------------------------------------------- */

/* Register Offset definitions for CMU_HSI1 (0x16400000) */
#define PLL_CON0_MUX_CLKCMU_HSI1_MMC_CARD_USER	0x600
#define PLL_CON0_MUX_CLKCMU_HSI1_NOC_USER	0x610
#define PLL_CON0_MUX_CLKCMU_HSI1_USBDRD_USER	0x620
#define CLK_CON_MUX_MUX_CLK_HSI1_USBDRD		0x1000

static const unsigned long hsi1_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_HSI1_MMC_CARD_USER,
	PLL_CON0_MUX_CLKCMU_HSI1_NOC_USER,
	PLL_CON0_MUX_CLKCMU_HSI1_USBDRD_USER,
	CLK_CON_MUX_MUX_CLK_HSI1_USBDRD,
};

/* List of parent clocks for Muxes in CMU_HSI1 */
PNAME(mout_hsi1_mmc_card_user_p) = {"oscclk", "dout_clkcmu_hsi1_mmc_card"};
PNAME(mout_hsi1_noc_user_p) = { "oscclk", "dout_clkcmu_hsi1_noc" };
PNAME(mout_hsi1_usbdrd_user_p) = { "oscclk", "mout_clkcmu_hsi1_usbdrd" };
PNAME(mout_hsi1_usbdrd_p) = { "dout_tcxo_div2", "mout_hsi1_usbdrd_user" };

static const struct samsung_mux_clock hsi1_mux_clks[] __initconst = {
	MUX(CLK_MOUT_HSI1_MMC_CARD_USER, "mout_hsi1_mmc_card_user",
	    mout_hsi1_mmc_card_user_p, PLL_CON0_MUX_CLKCMU_HSI1_MMC_CARD_USER, 4, 1),
	MUX(CLK_MOUT_HSI1_NOC_USER, "mout_hsi1_noc_user",
	    mout_hsi1_noc_user_p, PLL_CON0_MUX_CLKCMU_HSI1_NOC_USER, 4, 1),
	MUX(CLK_MOUT_HSI1_USBDRD_USER, "mout_hsi1_usbdrd_user",
	    mout_hsi1_usbdrd_user_p, PLL_CON0_MUX_CLKCMU_HSI1_USBDRD_USER, 4, 1),
	MUX(CLK_MOUT_HSI1_USBDRD, "mout_hsi1_usbdrd",
	    mout_hsi1_usbdrd_p, CLK_CON_MUX_MUX_CLK_HSI1_USBDRD, 4, 1),
};

static const struct samsung_cmu_info hsi1_cmu_info __initconst = {
	.mux_clks		= hsi1_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(hsi1_mux_clks),
	.nr_clk_ids		= CLKS_NR_HSI1,
	.clk_regs		= hsi1_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(hsi1_clk_regs),
	.clk_name		= "noc",
};

static int __init exynosautov920_cmu_probe(struct platform_device *pdev)
{
	const struct samsung_cmu_info *info;
	struct device *dev = &pdev->dev;

	info = of_device_get_match_data(dev);
	exynos_arm64_register_cmu(dev, dev->of_node, info);

	return 0;
}

static const struct of_device_id exynosautov920_cmu_of_match[] = {
	{
		.compatible = "samsung,exynosautov920-cmu-peric0",
		.data = &peric0_cmu_info,
	}, {
		 .compatible = "samsung,exynosautov920-cmu-peric1",
		 .data = &peric1_cmu_info,
	}, {
		 .compatible = "samsung,exynosautov920-cmu-misc",
		 .data = &misc_cmu_info,
	}, {
		.compatible = "samsung,exynosautov920-cmu-hsi0",
		.data = &hsi0_cmu_info,
	}, {
		.compatible = "samsung,exynosautov920-cmu-hsi1",
		.data = &hsi1_cmu_info,
	},
	{ }
};

static struct platform_driver exynosautov920_cmu_driver __refdata = {
	.driver = {
		.name = "exynosautov920-cmu",
		.of_match_table = exynosautov920_cmu_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = exynosautov920_cmu_probe,
};

static int __init exynosautov920_cmu_init(void)
{
	return platform_driver_register(&exynosautov920_cmu_driver);
}
core_initcall(exynosautov920_cmu_init);
