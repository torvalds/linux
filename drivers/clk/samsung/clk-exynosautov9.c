// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Samsung Electronics Co., Ltd.
 * Author: Chanho Park <chanho61.park@samsung.com>
 *
 * Common Clock Framework support for ExynosAuto V9 SoC.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <dt-bindings/clock/samsung,exynosautov9.h>

#include "clk.h"
#include "clk-exynos-arm64.h"

/* NOTE: Must be equal to the last clock ID increased by one */
#define CLKS_NR_TOP			(GOUT_CLKCMU_PERIS_BUS + 1)
#define CLKS_NR_BUSMC			(CLK_GOUT_BUSMC_SPDMA_PCLK + 1)
#define CLKS_NR_CORE			(CLK_GOUT_CORE_CMU_CORE_PCLK + 1)
#define CLKS_NR_FSYS0			(CLK_GOUT_FSYS0_PCIE_GEN3B_4L_CLK + 1)
#define CLKS_NR_FSYS1			(CLK_GOUT_FSYS1_USB30_1_ACLK + 1)
#define CLKS_NR_FSYS2			(CLK_GOUT_FSYS2_UFS_EMBD1_UNIPRO + 1)
#define CLKS_NR_PERIC0			(CLK_GOUT_PERIC0_PCLK_11 + 1)
#define CLKS_NR_PERIC1			(CLK_GOUT_PERIC1_PCLK_11 + 1)
#define CLKS_NR_PERIS			(CLK_GOUT_WDT_CLUSTER1 + 1)

/* ---- CMU_TOP ------------------------------------------------------------ */

/* Register Offset definitions for CMU_TOP (0x1b240000) */
#define PLL_LOCKTIME_PLL_SHARED0		0x0000
#define PLL_LOCKTIME_PLL_SHARED1		0x0004
#define PLL_LOCKTIME_PLL_SHARED2		0x0008
#define PLL_LOCKTIME_PLL_SHARED3		0x000c
#define PLL_LOCKTIME_PLL_SHARED4		0x0010
#define PLL_CON0_PLL_SHARED0			0x0100
#define PLL_CON3_PLL_SHARED0			0x010c
#define PLL_CON0_PLL_SHARED1			0x0140
#define PLL_CON3_PLL_SHARED1			0x014c
#define PLL_CON0_PLL_SHARED2			0x0180
#define PLL_CON3_PLL_SHARED2			0x018c
#define PLL_CON0_PLL_SHARED3			0x01c0
#define PLL_CON3_PLL_SHARED3			0x01cc
#define PLL_CON0_PLL_SHARED4			0x0200
#define PLL_CON3_PLL_SHARED4			0x020c

/* MUX */
#define CLK_CON_MUX_MUX_CLKCMU_ACC_BUS		0x1000
#define CLK_CON_MUX_MUX_CLKCMU_APM_BUS		0x1004
#define CLK_CON_MUX_MUX_CLKCMU_AUD_BUS		0x1008
#define CLK_CON_MUX_MUX_CLKCMU_AUD_CPU		0x100c
#define CLK_CON_MUX_MUX_CLKCMU_BUSC_BUS		0x1010
#define CLK_CON_MUX_MUX_CLKCMU_BUSMC_BUS	0x1018
#define CLK_CON_MUX_MUX_CLKCMU_CMU_BOOST	0x101c
#define CLK_CON_MUX_MUX_CLKCMU_CORE_BUS		0x1020
#define CLK_CON_MUX_MUX_CLKCMU_CPUCL0_CLUSTER	0x1024
#define CLK_CON_MUX_MUX_CLKCMU_CPUCL0_SWITCH	0x102c
#define CLK_CON_MUX_MUX_CLKCMU_CPUCL1_CLUSTER	0x1030
#define CLK_CON_MUX_MUX_CLKCMU_CPUCL1_SWITCH	0x1034
#define CLK_CON_MUX_MUX_CLKCMU_DPTX_BUS		0x1040
#define CLK_CON_MUX_MUX_CLKCMU_DPTX_DPGTC	0x1044
#define CLK_CON_MUX_MUX_CLKCMU_DPUM_BUS		0x1048
#define CLK_CON_MUX_MUX_CLKCMU_DPUS0_BUS	0x104c
#define CLK_CON_MUX_MUX_CLKCMU_DPUS1_BUS	0x1050
#define CLK_CON_MUX_MUX_CLKCMU_FSYS0_BUS	0x1054
#define CLK_CON_MUX_MUX_CLKCMU_FSYS0_PCIE	0x1058
#define CLK_CON_MUX_MUX_CLKCMU_FSYS1_BUS	0x105c
#define CLK_CON_MUX_MUX_CLKCMU_FSYS1_MMC_CARD	0x1060
#define CLK_CON_MUX_MUX_CLKCMU_FSYS1_USBDRD	0x1064
#define CLK_CON_MUX_MUX_CLKCMU_FSYS2_BUS	0x1068
#define CLK_CON_MUX_MUX_CLKCMU_FSYS2_ETHERNET	0x106c
#define CLK_CON_MUX_MUX_CLKCMU_FSYS2_UFS_EMBD	0x1070
#define CLK_CON_MUX_MUX_CLKCMU_G2D_G2D		0x1074
#define CLK_CON_MUX_MUX_CLKCMU_G2D_MSCL		0x1078
#define CLK_CON_MUX_MUX_CLKCMU_G3D00_SWITCH	0x107c
#define CLK_CON_MUX_MUX_CLKCMU_G3D01_SWITCH	0x1080
#define CLK_CON_MUX_MUX_CLKCMU_G3D1_SWITCH	0x1084
#define CLK_CON_MUX_MUX_CLKCMU_ISPB_BUS		0x108c
#define CLK_CON_MUX_MUX_CLKCMU_MFC_MFC		0x1090
#define CLK_CON_MUX_MUX_CLKCMU_MFC_WFD		0x1094
#define CLK_CON_MUX_MUX_CLKCMU_MIF_SWITCH	0x109c
#define CLK_CON_MUX_MUX_CLKCMU_MIF_BUSP		0x1098
#define CLK_CON_MUX_MUX_CLKCMU_MIF_SWITCH	0x109c
#define CLK_CON_MUX_MUX_CLKCMU_NPU_BUS		0x10a0
#define CLK_CON_MUX_MUX_CLKCMU_PERIC0_BUS	0x10a4
#define CLK_CON_MUX_MUX_CLKCMU_PERIC0_IP	0x10a8
#define CLK_CON_MUX_MUX_CLKCMU_PERIC1_BUS	0x10ac
#define CLK_CON_MUX_MUX_CLKCMU_PERIC1_IP	0x10b0
#define CLK_CON_MUX_MUX_CLKCMU_PERIS_BUS	0x10b4
#define CLK_CON_MUX_MUX_CMU_CMUREF		0x10c0

/* DIV */
#define CLK_CON_DIV_CLKCMU_ACC_BUS		0x1800
#define CLK_CON_DIV_CLKCMU_APM_BUS		0x1804
#define CLK_CON_DIV_CLKCMU_AUD_BUS		0x1808
#define CLK_CON_DIV_CLKCMU_AUD_CPU		0x180c
#define CLK_CON_DIV_CLKCMU_BUSC_BUS		0x1810
#define CLK_CON_DIV_CLKCMU_BUSMC_BUS		0x1818
#define CLK_CON_DIV_CLKCMU_CORE_BUS		0x181c
#define CLK_CON_DIV_CLKCMU_CPUCL0_CLUSTER	0x1820
#define CLK_CON_DIV_CLKCMU_CPUCL0_SWITCH	0x1828
#define CLK_CON_DIV_CLKCMU_CPUCL1_CLUSTER	0x182c
#define CLK_CON_DIV_CLKCMU_CPUCL1_SWITCH	0x1830
#define CLK_CON_DIV_CLKCMU_DPTX_BUS		0x183c
#define CLK_CON_DIV_CLKCMU_DPTX_DPGTC		0x1840
#define CLK_CON_DIV_CLKCMU_DPUM_BUS		0x1844
#define CLK_CON_DIV_CLKCMU_DPUS0_BUS		0x1848
#define CLK_CON_DIV_CLKCMU_DPUS1_BUS		0x184c
#define CLK_CON_DIV_CLKCMU_FSYS0_BUS		0x1850
#define CLK_CON_DIV_CLKCMU_FSYS0_PCIE		0x1854
#define CLK_CON_DIV_CLKCMU_FSYS1_BUS		0x1858
#define CLK_CON_DIV_CLKCMU_FSYS1_USBDRD		0x185c
#define CLK_CON_DIV_CLKCMU_FSYS2_BUS		0x1860
#define CLK_CON_DIV_CLKCMU_FSYS2_ETHERNET	0x1864
#define CLK_CON_DIV_CLKCMU_FSYS2_UFS_EMBD	0x1868
#define CLK_CON_DIV_CLKCMU_G2D_G2D		0x186c
#define CLK_CON_DIV_CLKCMU_G2D_MSCL		0x1870
#define CLK_CON_DIV_CLKCMU_G3D00_SWITCH		0x1874
#define CLK_CON_DIV_CLKCMU_G3D01_SWITCH		0x1878
#define CLK_CON_DIV_CLKCMU_G3D1_SWITCH		0x187c
#define CLK_CON_DIV_CLKCMU_ISPB_BUS		0x1884
#define CLK_CON_DIV_CLKCMU_MFC_MFC		0x1888
#define CLK_CON_DIV_CLKCMU_MFC_WFD		0x188c
#define CLK_CON_DIV_CLKCMU_MIF_BUSP		0x1890
#define CLK_CON_DIV_CLKCMU_NPU_BUS		0x1894
#define CLK_CON_DIV_CLKCMU_PERIC0_BUS		0x1898
#define CLK_CON_DIV_CLKCMU_PERIC0_IP		0x189c
#define CLK_CON_DIV_CLKCMU_PERIC1_BUS		0x18a0
#define CLK_CON_DIV_CLKCMU_PERIC1_IP		0x18a4
#define CLK_CON_DIV_CLKCMU_PERIS_BUS		0x18a8
#define CLK_CON_DIV_DIV_CLKCMU_CMU_BOOST	0x18b4

#define CLK_CON_DIV_PLL_SHARED0_DIV2		0x18b8
#define CLK_CON_DIV_PLL_SHARED0_DIV3		0x18bc
#define CLK_CON_DIV_PLL_SHARED1_DIV2		0x18c0
#define CLK_CON_DIV_PLL_SHARED1_DIV3		0x18c4
#define CLK_CON_DIV_PLL_SHARED1_DIV4		0x18c8
#define CLK_CON_DIV_PLL_SHARED2_DIV2		0x18cc
#define CLK_CON_DIV_PLL_SHARED2_DIV3		0x18d0
#define CLK_CON_DIV_PLL_SHARED2_DIV4		0x18d4
#define CLK_CON_DIV_PLL_SHARED4_DIV2		0x18d4
#define CLK_CON_DIV_PLL_SHARED4_DIV4		0x18d8

/* GATE */
#define CLK_CON_GAT_CLKCMU_CMU_BUSC_BOOST	0x2000
#define CLK_CON_GAT_CLKCMU_CMU_BUSMC_BOOST	0x2004
#define CLK_CON_GAT_CLKCMU_CMU_CORE_BOOST	0x2008
#define CLK_CON_GAT_CLKCMU_CMU_CPUCL0_BOOST	0x2010
#define CLK_CON_GAT_CLKCMU_CMU_CPUCL1_BOOST	0x2018
#define CLK_CON_GAT_CLKCMU_CMU_MIF_BOOST	0x2020
#define CLK_CON_GAT_GATE_CLKCMU_FSYS1_MMC_CARD	0x2024
#define CLK_CON_GAT_GATE_CLKCMU_MIF_SWITCH	0x2028
#define CLK_CON_GAT_GATE_CLKCMU_ACC_BUS		0x202c
#define CLK_CON_GAT_GATE_CLKCMU_APM_BUS		0x2030
#define CLK_CON_GAT_GATE_CLKCMU_AUD_BUS		0x2034
#define CLK_CON_GAT_GATE_CLKCMU_AUD_CPU		0x2038
#define CLK_CON_GAT_GATE_CLKCMU_BUSC_BUS	0x203c
#define CLK_CON_GAT_GATE_CLKCMU_BUSMC_BUS	0x2044
#define CLK_CON_GAT_GATE_CLKCMU_CMU_BOOST	0x2048
#define CLK_CON_GAT_GATE_CLKCMU_CORE_BUS	0x204c
#define CLK_CON_GAT_GATE_CLKCMU_CPUCL0_CLUSTER	0x2050
#define CLK_CON_GAT_GATE_CLKCMU_CPUCL0_SWITCH	0x2058
#define CLK_CON_GAT_GATE_CLKCMU_CPUCL1_CLUSTER	0x205c
#define CLK_CON_GAT_GATE_CLKCMU_CPUCL1_SWITCH	0x2060
#define CLK_CON_GAT_GATE_CLKCMU_DPTX_BUS	0x206c
#define CLK_CON_GAT_GATE_CLKCMU_DPTX_DPGTC	0x2070
#define CLK_CON_GAT_GATE_CLKCMU_DPUM_BUS	0x2060
#define CLK_CON_GAT_GATE_CLKCMU_DPUS0_BUS	0x2064
#define CLK_CON_GAT_GATE_CLKCMU_DPUS1_BUS	0x207c
#define CLK_CON_GAT_GATE_CLKCMU_FSYS0_BUS	0x2080
#define CLK_CON_GAT_GATE_CLKCMU_FSYS0_PCIE	0x2084
#define CLK_CON_GAT_GATE_CLKCMU_FSYS1_BUS	0x2088
#define CLK_CON_GAT_GATE_CLKCMU_FSYS1_USBDRD	0x208c
#define CLK_CON_GAT_GATE_CLKCMU_FSYS2_BUS	0x2090
#define CLK_CON_GAT_GATE_CLKCMU_FSYS2_ETHERNET	0x2094
#define CLK_CON_GAT_GATE_CLKCMU_FSYS2_UFS_EMBD	0x2098
#define CLK_CON_GAT_GATE_CLKCMU_G2D_G2D		0x209c
#define CLK_CON_GAT_GATE_CLKCMU_G2D_MSCL	0x20a0
#define CLK_CON_GAT_GATE_CLKCMU_G3D00_SWITCH	0x20a4
#define CLK_CON_GAT_GATE_CLKCMU_G3D01_SWITCH	0x20a8
#define CLK_CON_GAT_GATE_CLKCMU_G3D1_SWITCH	0x20ac
#define CLK_CON_GAT_GATE_CLKCMU_ISPB_BUS	0x20b4
#define CLK_CON_GAT_GATE_CLKCMU_MFC_MFC		0x20b8
#define CLK_CON_GAT_GATE_CLKCMU_MFC_WFD		0x20bc
#define CLK_CON_GAT_GATE_CLKCMU_MIF_BUSP	0x20c0
#define CLK_CON_GAT_GATE_CLKCMU_NPU_BUS		0x20c4
#define CLK_CON_GAT_GATE_CLKCMU_PERIC0_BUS	0x20c8
#define CLK_CON_GAT_GATE_CLKCMU_PERIC0_IP	0x20cc
#define CLK_CON_GAT_GATE_CLKCMU_PERIC1_BUS	0x20d0
#define CLK_CON_GAT_GATE_CLKCMU_PERIC1_IP	0x20d4
#define CLK_CON_GAT_GATE_CLKCMU_PERIS_BUS	0x20d8

static const unsigned long top_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_SHARED0,
	PLL_LOCKTIME_PLL_SHARED1,
	PLL_LOCKTIME_PLL_SHARED2,
	PLL_LOCKTIME_PLL_SHARED3,
	PLL_LOCKTIME_PLL_SHARED4,
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
	CLK_CON_MUX_MUX_CLKCMU_ACC_BUS,
	CLK_CON_MUX_MUX_CLKCMU_APM_BUS,
	CLK_CON_MUX_MUX_CLKCMU_AUD_BUS,
	CLK_CON_MUX_MUX_CLKCMU_AUD_CPU,
	CLK_CON_MUX_MUX_CLKCMU_BUSC_BUS,
	CLK_CON_MUX_MUX_CLKCMU_CMU_BOOST,
	CLK_CON_MUX_MUX_CLKCMU_CORE_BUS,
	CLK_CON_MUX_MUX_CLKCMU_CPUCL0_CLUSTER,
	CLK_CON_MUX_MUX_CLKCMU_CPUCL0_SWITCH,
	CLK_CON_MUX_MUX_CLKCMU_CPUCL1_CLUSTER,
	CLK_CON_MUX_MUX_CLKCMU_CPUCL1_SWITCH,
	CLK_CON_MUX_MUX_CLKCMU_DPTX_BUS,
	CLK_CON_MUX_MUX_CLKCMU_DPTX_DPGTC,
	CLK_CON_MUX_MUX_CLKCMU_DPUM_BUS,
	CLK_CON_MUX_MUX_CLKCMU_DPUS0_BUS,
	CLK_CON_MUX_MUX_CLKCMU_DPUS1_BUS,
	CLK_CON_MUX_MUX_CLKCMU_FSYS0_BUS,
	CLK_CON_MUX_MUX_CLKCMU_FSYS0_PCIE,
	CLK_CON_MUX_MUX_CLKCMU_FSYS1_BUS,
	CLK_CON_MUX_MUX_CLKCMU_FSYS1_MMC_CARD,
	CLK_CON_MUX_MUX_CLKCMU_FSYS1_USBDRD,
	CLK_CON_MUX_MUX_CLKCMU_FSYS2_BUS,
	CLK_CON_MUX_MUX_CLKCMU_FSYS2_ETHERNET,
	CLK_CON_MUX_MUX_CLKCMU_FSYS2_UFS_EMBD,
	CLK_CON_MUX_MUX_CLKCMU_G2D_G2D,
	CLK_CON_MUX_MUX_CLKCMU_G2D_MSCL,
	CLK_CON_MUX_MUX_CLKCMU_G3D00_SWITCH,
	CLK_CON_MUX_MUX_CLKCMU_G3D01_SWITCH,
	CLK_CON_MUX_MUX_CLKCMU_G3D1_SWITCH,
	CLK_CON_MUX_MUX_CLKCMU_ISPB_BUS,
	CLK_CON_MUX_MUX_CLKCMU_MFC_MFC,
	CLK_CON_MUX_MUX_CLKCMU_MFC_WFD,
	CLK_CON_MUX_MUX_CLKCMU_MIF_SWITCH,
	CLK_CON_MUX_MUX_CLKCMU_MIF_BUSP,
	CLK_CON_MUX_MUX_CLKCMU_MIF_SWITCH,
	CLK_CON_MUX_MUX_CLKCMU_NPU_BUS,
	CLK_CON_MUX_MUX_CLKCMU_PERIC0_BUS,
	CLK_CON_MUX_MUX_CLKCMU_PERIC0_IP,
	CLK_CON_MUX_MUX_CLKCMU_PERIC1_BUS,
	CLK_CON_MUX_MUX_CLKCMU_PERIC1_IP,
	CLK_CON_MUX_MUX_CLKCMU_PERIS_BUS,
	CLK_CON_MUX_MUX_CMU_CMUREF,
	CLK_CON_DIV_CLKCMU_ACC_BUS,
	CLK_CON_DIV_CLKCMU_APM_BUS,
	CLK_CON_DIV_CLKCMU_AUD_BUS,
	CLK_CON_DIV_CLKCMU_AUD_CPU,
	CLK_CON_DIV_CLKCMU_BUSC_BUS,
	CLK_CON_DIV_CLKCMU_BUSMC_BUS,
	CLK_CON_DIV_CLKCMU_CORE_BUS,
	CLK_CON_DIV_CLKCMU_CPUCL0_CLUSTER,
	CLK_CON_DIV_CLKCMU_CPUCL0_SWITCH,
	CLK_CON_DIV_CLKCMU_CPUCL1_CLUSTER,
	CLK_CON_DIV_CLKCMU_CPUCL1_SWITCH,
	CLK_CON_DIV_CLKCMU_DPTX_BUS,
	CLK_CON_DIV_CLKCMU_DPTX_DPGTC,
	CLK_CON_DIV_CLKCMU_DPUM_BUS,
	CLK_CON_DIV_CLKCMU_DPUS0_BUS,
	CLK_CON_DIV_CLKCMU_DPUS1_BUS,
	CLK_CON_DIV_CLKCMU_FSYS0_BUS,
	CLK_CON_DIV_CLKCMU_FSYS0_PCIE,
	CLK_CON_DIV_CLKCMU_FSYS1_BUS,
	CLK_CON_DIV_CLKCMU_FSYS1_USBDRD,
	CLK_CON_DIV_CLKCMU_FSYS2_BUS,
	CLK_CON_DIV_CLKCMU_FSYS2_ETHERNET,
	CLK_CON_DIV_CLKCMU_FSYS2_UFS_EMBD,
	CLK_CON_DIV_CLKCMU_G2D_G2D,
	CLK_CON_DIV_CLKCMU_G2D_MSCL,
	CLK_CON_DIV_CLKCMU_G3D00_SWITCH,
	CLK_CON_DIV_CLKCMU_G3D01_SWITCH,
	CLK_CON_DIV_CLKCMU_G3D1_SWITCH,
	CLK_CON_DIV_CLKCMU_ISPB_BUS,
	CLK_CON_DIV_CLKCMU_MFC_MFC,
	CLK_CON_DIV_CLKCMU_MFC_WFD,
	CLK_CON_DIV_CLKCMU_MIF_BUSP,
	CLK_CON_DIV_CLKCMU_NPU_BUS,
	CLK_CON_DIV_CLKCMU_PERIC0_BUS,
	CLK_CON_DIV_CLKCMU_PERIC0_IP,
	CLK_CON_DIV_CLKCMU_PERIC1_BUS,
	CLK_CON_DIV_CLKCMU_PERIC1_IP,
	CLK_CON_DIV_CLKCMU_PERIS_BUS,
	CLK_CON_DIV_DIV_CLKCMU_CMU_BOOST,
	CLK_CON_DIV_PLL_SHARED0_DIV2,
	CLK_CON_DIV_PLL_SHARED0_DIV3,
	CLK_CON_DIV_PLL_SHARED1_DIV2,
	CLK_CON_DIV_PLL_SHARED1_DIV3,
	CLK_CON_DIV_PLL_SHARED1_DIV4,
	CLK_CON_DIV_PLL_SHARED2_DIV2,
	CLK_CON_DIV_PLL_SHARED2_DIV3,
	CLK_CON_DIV_PLL_SHARED2_DIV4,
	CLK_CON_DIV_PLL_SHARED4_DIV2,
	CLK_CON_DIV_PLL_SHARED4_DIV4,
	CLK_CON_GAT_CLKCMU_CMU_BUSC_BOOST,
	CLK_CON_GAT_CLKCMU_CMU_BUSMC_BOOST,
	CLK_CON_GAT_CLKCMU_CMU_CORE_BOOST,
	CLK_CON_GAT_CLKCMU_CMU_CPUCL0_BOOST,
	CLK_CON_GAT_CLKCMU_CMU_CPUCL1_BOOST,
	CLK_CON_GAT_CLKCMU_CMU_MIF_BOOST,
	CLK_CON_GAT_GATE_CLKCMU_FSYS1_MMC_CARD,
	CLK_CON_GAT_GATE_CLKCMU_MIF_SWITCH,
	CLK_CON_GAT_GATE_CLKCMU_ACC_BUS,
	CLK_CON_GAT_GATE_CLKCMU_APM_BUS,
	CLK_CON_GAT_GATE_CLKCMU_AUD_BUS,
	CLK_CON_GAT_GATE_CLKCMU_AUD_CPU,
	CLK_CON_GAT_GATE_CLKCMU_BUSC_BUS,
	CLK_CON_GAT_GATE_CLKCMU_BUSMC_BUS,
	CLK_CON_GAT_GATE_CLKCMU_CMU_BOOST,
	CLK_CON_GAT_GATE_CLKCMU_CORE_BUS,
	CLK_CON_GAT_GATE_CLKCMU_CPUCL0_CLUSTER,
	CLK_CON_GAT_GATE_CLKCMU_CPUCL0_SWITCH,
	CLK_CON_GAT_GATE_CLKCMU_CPUCL1_CLUSTER,
	CLK_CON_GAT_GATE_CLKCMU_CPUCL1_SWITCH,
	CLK_CON_GAT_GATE_CLKCMU_DPTX_BUS,
	CLK_CON_GAT_GATE_CLKCMU_DPTX_DPGTC,
	CLK_CON_GAT_GATE_CLKCMU_DPUM_BUS,
	CLK_CON_GAT_GATE_CLKCMU_DPUS0_BUS,
	CLK_CON_GAT_GATE_CLKCMU_DPUS1_BUS,
	CLK_CON_GAT_GATE_CLKCMU_FSYS0_BUS,
	CLK_CON_GAT_GATE_CLKCMU_FSYS0_PCIE,
	CLK_CON_GAT_GATE_CLKCMU_FSYS1_BUS,
	CLK_CON_GAT_GATE_CLKCMU_FSYS1_USBDRD,
	CLK_CON_GAT_GATE_CLKCMU_FSYS2_BUS,
	CLK_CON_GAT_GATE_CLKCMU_FSYS2_ETHERNET,
	CLK_CON_GAT_GATE_CLKCMU_FSYS2_UFS_EMBD,
	CLK_CON_GAT_GATE_CLKCMU_G2D_G2D,
	CLK_CON_GAT_GATE_CLKCMU_G2D_MSCL,
	CLK_CON_GAT_GATE_CLKCMU_G3D00_SWITCH,
	CLK_CON_GAT_GATE_CLKCMU_G3D01_SWITCH,
	CLK_CON_GAT_GATE_CLKCMU_G3D1_SWITCH,
	CLK_CON_GAT_GATE_CLKCMU_ISPB_BUS,
	CLK_CON_GAT_GATE_CLKCMU_MFC_MFC,
	CLK_CON_GAT_GATE_CLKCMU_MFC_WFD,
	CLK_CON_GAT_GATE_CLKCMU_MIF_BUSP,
	CLK_CON_GAT_GATE_CLKCMU_NPU_BUS,
	CLK_CON_GAT_GATE_CLKCMU_PERIC0_BUS,
	CLK_CON_GAT_GATE_CLKCMU_PERIC0_IP,
	CLK_CON_GAT_GATE_CLKCMU_PERIC1_BUS,
	CLK_CON_GAT_GATE_CLKCMU_PERIC1_IP,
	CLK_CON_GAT_GATE_CLKCMU_PERIS_BUS,
};

static const struct samsung_pll_clock top_pll_clks[] __initconst = {
	/* CMU_TOP_PURECLKCOMP */
	PLL(pll_0822x, FOUT_SHARED0_PLL, "fout_shared0_pll", "oscclk",
	    PLL_LOCKTIME_PLL_SHARED0, PLL_CON3_PLL_SHARED0, NULL),
	PLL(pll_0822x, FOUT_SHARED0_PLL, "fout_shared1_pll", "oscclk",
	    PLL_LOCKTIME_PLL_SHARED1, PLL_CON3_PLL_SHARED1, NULL),
	PLL(pll_0822x, FOUT_SHARED0_PLL, "fout_shared2_pll", "oscclk",
	    PLL_LOCKTIME_PLL_SHARED2, PLL_CON3_PLL_SHARED2, NULL),
	PLL(pll_0822x, FOUT_SHARED0_PLL, "fout_shared3_pll", "oscclk",
	    PLL_LOCKTIME_PLL_SHARED3, PLL_CON3_PLL_SHARED3, NULL),
	PLL(pll_0822x, FOUT_SHARED0_PLL, "fout_shared4_pll", "oscclk",
	    PLL_LOCKTIME_PLL_SHARED4, PLL_CON3_PLL_SHARED4, NULL),
};

/* List of parent clocks for Muxes in CMU_TOP */
PNAME(mout_shared0_pll_p) = { "oscclk", "fout_shared0_pll" };
PNAME(mout_shared1_pll_p) = { "oscclk", "fout_shared1_pll" };
PNAME(mout_shared2_pll_p) = { "oscclk", "fout_shared2_pll" };
PNAME(mout_shared3_pll_p) = { "oscclk", "fout_shared3_pll" };
PNAME(mout_shared4_pll_p) = { "oscclk", "fout_shared4_pll" };

PNAME(mout_clkcmu_cmu_boost_p) = { "dout_shared2_div3", "dout_shared1_div4",
				   "dout_shared2_div4", "dout_shared4_div4" };
PNAME(mout_clkcmu_cmu_cmuref_p) = { "oscclk", "dout_cmu_boost" };
PNAME(mout_clkcmu_acc_bus_p) = { "dout_shared1_div3", "dout_shared2_div3",
				 "dout_shared1_div4", "dout_shared2_div4" };
PNAME(mout_clkcmu_apm_bus_p) = { "dout_shared2_div3", "dout_shared1_div4",
				 "dout_shared2_div4", "dout_shared4_div4" };
PNAME(mout_clkcmu_aud_cpu_p) = { "dout_shared0_div2", "dout_shared1_div2",
				 "dout_shared2_div2", "dout_shared0_div3",
				 "dout_shared4_div2", "dout_shared1_div3",
				 "fout_shared3_pll" };
PNAME(mout_clkcmu_aud_bus_p) = { "dout_shared4_div2", "dout_shared1_div3",
				  "dout_shared2_div3", "dout_shared1_div4" };
PNAME(mout_clkcmu_busc_bus_p) = { "dout_shared2_div3", "dout_shared1_div4",
				  "dout_shared2_div4", "dout_shared4_div4" };
PNAME(mout_clkcmu_core_bus_p) = { "dout_shared0_div2", "dout_shared1_div2",
				  "dout_shared2_div2", "dout_shared0_div3",
				  "dout_shared4_div2", "dout_shared1_div3",
				  "dout_shared2_div3", "fout_shared3_pll" };
PNAME(mout_clkcmu_cpucl0_switch_p) = {
	"dout_shared0_div2", "dout_shared1_div2",
	"dout_shared2_div2", "dout_shared4_div2" };
PNAME(mout_clkcmu_cpucl0_cluster_p) = {
	"fout_shared2_pll", "fout_shared4_pll",
	"dout_shared0_div2", "dout_shared1_div2",
	"dout_shared2_div2", "dout_shared4_div2",
	"dout_shared2_div3", "fout_shared3_pll" };
PNAME(mout_clkcmu_dptx_bus_p) = { "dout_shared4_div2", "dout_shared2_div3",
				  "dout_shared1_div4", "dout_shared2_div4" };
PNAME(mout_clkcmu_dptx_dpgtc_p) = { "oscclk", "dout_shared2_div3",
				    "dout_shared2_div4", "dout_shared4_div4" };
PNAME(mout_clkcmu_dpum_bus_p) = { "dout_shared1_div3", "dout_shared2_div3",
				  "dout_shared1_div4", "dout_shared2_div4",
				  "dout_shared4_div4", "fout_shared3_pll" };
PNAME(mout_clkcmu_fsys0_bus_p)	= {
	"dout_shared4_div2", "dout_shared2_div3",
	"dout_shared1_div4", "dout_shared2_div4" };
PNAME(mout_clkcmu_fsys0_pcie_p) = { "oscclk", "dout_shared2_div4" };
PNAME(mout_clkcmu_fsys1_bus_p)	= { "dout_shared2_div3", "dout_shared1_div4",
				    "dout_shared2_div4", "dout_shared4_div4" };
PNAME(mout_clkcmu_fsys1_usbdrd_p) = {
	"oscclk", "dout_shared2_div3",
	"dout_shared2_div4", "dout_shared4_div4" };
PNAME(mout_clkcmu_fsys1_mmc_card_p) = {
	"oscclk", "dout_shared2_div2",
	"dout_shared4_div2", "dout_shared2_div3" };
PNAME(mout_clkcmu_fsys2_ethernet_p) = {
	"oscclk", "dout_shared2_div2",
	"dout_shared0_div3", "dout_shared2_div3",
	"dout_shared1_div4", "fout_shared3_pll" };
PNAME(mout_clkcmu_g2d_g2d_p) = { "dout_shared2_div2", "dout_shared0_div3",
				 "dout_shared4_div2", "dout_shared1_div3",
				 "dout_shared2_div3", "dout_shared1_div4",
				 "dout_shared2_div4", "dout_shared4_div4" };
PNAME(mout_clkcmu_g3d0_switch_p) = { "dout_shared0_div2", "dout_shared1_div2",
				     "dout_shared2_div2", "dout_shared4_div2" };
PNAME(mout_clkcmu_g3d1_switch_p) = { "dout_shared2_div2", "dout_shared4_div2",
				     "dout_shared2_div3", "dout_shared1_div4" };
PNAME(mout_clkcmu_mif_switch_p) = { "fout_shared0_pll", "fout_shared1_pll",
				    "fout_shared2_pll", "fout_shared4_pll",
				    "dout_shared0_div2", "dout_shared1_div2",
				    "dout_shared2_div2", "fout_shared3_pll" };
PNAME(mout_clkcmu_npu_bus_p) = { "dout_shared1_div2", "dout_shared2_div2",
				 "dout_shared0_div3", "dout_shared4_div2",
				 "dout_shared1_div3", "dout_shared2_div3",
				 "dout_shared1_div4", "fout_shared3_pll" };
PNAME(mout_clkcmu_peric0_bus_p) = { "dout_shared2_div3", "dout_shared2_div4" };

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

	/* BOOST */
	MUX(MOUT_CLKCMU_CMU_BOOST, "mout_clkcmu_cmu_boost",
	    mout_clkcmu_cmu_boost_p, CLK_CON_MUX_MUX_CLKCMU_CMU_BOOST, 0, 2),
	MUX(MOUT_CLKCMU_CMU_CMUREF, "mout_clkcmu_cmu_cmuref",
	    mout_clkcmu_cmu_cmuref_p, CLK_CON_MUX_MUX_CMU_CMUREF, 0, 1),

	/* ACC */
	MUX(MOUT_CLKCMU_ACC_BUS, "mout_clkcmu_acc_bus", mout_clkcmu_acc_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_ACC_BUS, 0, 2),

	/* APM */
	MUX(MOUT_CLKCMU_APM_BUS, "mout_clkcmu_apm_bus", mout_clkcmu_apm_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_APM_BUS, 0, 2),

	/* AUD */
	MUX(MOUT_CLKCMU_AUD_CPU, "mout_clkcmu_aud_cpu", mout_clkcmu_aud_cpu_p,
	    CLK_CON_MUX_MUX_CLKCMU_AUD_CPU, 0, 3),
	MUX(MOUT_CLKCMU_AUD_BUS, "mout_clkcmu_aud_bus", mout_clkcmu_aud_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_AUD_BUS, 0, 2),

	/* BUSC */
	MUX(MOUT_CLKCMU_BUSC_BUS, "mout_clkcmu_busc_bus",
	    mout_clkcmu_busc_bus_p, CLK_CON_MUX_MUX_CLKCMU_BUSC_BUS, 0, 2),

	/* BUSMC */
	MUX(MOUT_CLKCMU_BUSMC_BUS, "mout_clkcmu_busmc_bus",
	    mout_clkcmu_busc_bus_p, CLK_CON_MUX_MUX_CLKCMU_BUSMC_BUS, 0, 2),

	/* CORE */
	MUX(MOUT_CLKCMU_CORE_BUS, "mout_clkcmu_core_bus",
	    mout_clkcmu_core_bus_p, CLK_CON_MUX_MUX_CLKCMU_CORE_BUS, 0, 3),

	/* CPUCL0 */
	MUX(MOUT_CLKCMU_CPUCL0_SWITCH, "mout_clkcmu_cpucl0_switch",
	    mout_clkcmu_cpucl0_switch_p, CLK_CON_MUX_MUX_CLKCMU_CPUCL0_SWITCH,
	    0, 2),
	MUX(MOUT_CLKCMU_CPUCL0_CLUSTER, "mout_clkcmu_cpucl0_cluster",
	    mout_clkcmu_cpucl0_cluster_p,
	    CLK_CON_MUX_MUX_CLKCMU_CPUCL0_CLUSTER, 0, 3),

	/* CPUCL1 */
	MUX(MOUT_CLKCMU_CPUCL1_SWITCH, "mout_clkcmu_cpucl1_switch",
	    mout_clkcmu_cpucl0_switch_p, CLK_CON_MUX_MUX_CLKCMU_CPUCL1_SWITCH,
	    0, 2),
	MUX(MOUT_CLKCMU_CPUCL1_CLUSTER, "mout_clkcmu_cpucl1_cluster",
	    mout_clkcmu_cpucl0_cluster_p,
	    CLK_CON_MUX_MUX_CLKCMU_CPUCL1_CLUSTER, 0, 3),

	/* DPTX */
	MUX(MOUT_CLKCMU_DPTX_BUS, "mout_clkcmu_dptx_bus",
	    mout_clkcmu_dptx_bus_p, CLK_CON_MUX_MUX_CLKCMU_DPTX_BUS, 0, 2),
	MUX(MOUT_CLKCMU_DPTX_DPGTC, "mout_clkcmu_dptx_dpgtc",
	    mout_clkcmu_dptx_dpgtc_p, CLK_CON_MUX_MUX_CLKCMU_DPTX_DPGTC, 0, 2),

	/* DPUM */
	MUX(MOUT_CLKCMU_DPUM_BUS, "mout_clkcmu_dpum_bus",
	    mout_clkcmu_dpum_bus_p, CLK_CON_MUX_MUX_CLKCMU_DPUM_BUS, 0, 3),

	/* DPUS */
	MUX(MOUT_CLKCMU_DPUS0_BUS, "mout_clkcmu_dpus0_bus",
	    mout_clkcmu_dpum_bus_p, CLK_CON_MUX_MUX_CLKCMU_DPUS0_BUS, 0, 3),
	MUX(MOUT_CLKCMU_DPUS1_BUS, "mout_clkcmu_dpus1_bus",
	    mout_clkcmu_dpum_bus_p, CLK_CON_MUX_MUX_CLKCMU_DPUS1_BUS, 0, 3),

	/* FSYS0 */
	MUX(MOUT_CLKCMU_FSYS0_BUS, "mout_clkcmu_fsys0_bus",
	    mout_clkcmu_fsys0_bus_p, CLK_CON_MUX_MUX_CLKCMU_FSYS0_BUS, 0, 2),
	MUX(MOUT_CLKCMU_FSYS0_PCIE, "mout_clkcmu_fsys0_pcie",
	    mout_clkcmu_fsys0_pcie_p, CLK_CON_MUX_MUX_CLKCMU_FSYS0_PCIE, 0, 1),

	/* FSYS1 */
	MUX(MOUT_CLKCMU_FSYS1_BUS, "mout_clkcmu_fsys1_bus",
	    mout_clkcmu_fsys1_bus_p, CLK_CON_MUX_MUX_CLKCMU_FSYS1_BUS, 0, 2),
	MUX(MOUT_CLKCMU_FSYS1_USBDRD, "mout_clkcmu_fsys1_usbdrd",
	    mout_clkcmu_fsys1_usbdrd_p, CLK_CON_MUX_MUX_CLKCMU_FSYS1_USBDRD,
	    0, 2),
	MUX(MOUT_CLKCMU_FSYS1_MMC_CARD, "mout_clkcmu_fsys1_mmc_card",
	    mout_clkcmu_fsys1_mmc_card_p,
	    CLK_CON_MUX_MUX_CLKCMU_FSYS1_MMC_CARD, 0, 2),

	/* FSYS2 */
	MUX(MOUT_CLKCMU_FSYS2_BUS, "mout_clkcmu_fsys2_bus",
	    mout_clkcmu_fsys0_bus_p, CLK_CON_MUX_MUX_CLKCMU_FSYS2_BUS, 0, 2),
	MUX(MOUT_CLKCMU_FSYS2_UFS_EMBD, "mout_clkcmu_fsys2_ufs_embd",
	    mout_clkcmu_fsys1_usbdrd_p, CLK_CON_MUX_MUX_CLKCMU_FSYS2_UFS_EMBD,
	    0, 2),
	MUX(MOUT_CLKCMU_FSYS2_ETHERNET, "mout_clkcmu_fsys2_ethernet",
	    mout_clkcmu_fsys2_ethernet_p,
	    CLK_CON_MUX_MUX_CLKCMU_FSYS2_ETHERNET, 0, 3),

	/* G2D */
	MUX(MOUT_CLKCMU_G2D_G2D, "mout_clkcmu_g2d_g2d", mout_clkcmu_g2d_g2d_p,
	    CLK_CON_MUX_MUX_CLKCMU_G2D_G2D, 0, 3),
	MUX(MOUT_CLKCMU_G2D_MSCL, "mout_clkcmu_g2d_mscl",
	    mout_clkcmu_fsys1_bus_p, CLK_CON_MUX_MUX_CLKCMU_G2D_MSCL, 0, 2),

	/* G3D0 */
	MUX(MOUT_CLKCMU_G3D00_SWITCH, "mout_clkcmu_g3d00_switch",
	    mout_clkcmu_g3d0_switch_p, CLK_CON_MUX_MUX_CLKCMU_G3D00_SWITCH,
	    0, 2),
	MUX(MOUT_CLKCMU_G3D01_SWITCH, "mout_clkcmu_g3d01_switch",
	    mout_clkcmu_g3d0_switch_p, CLK_CON_MUX_MUX_CLKCMU_G3D01_SWITCH,
	    0, 2),

	/* G3D1 */
	MUX(MOUT_CLKCMU_G3D1_SWITCH, "mout_clkcmu_g3d1_switch",
	    mout_clkcmu_g3d1_switch_p, CLK_CON_MUX_MUX_CLKCMU_G3D1_SWITCH,
	    0, 2),

	/* ISPB */
	MUX(MOUT_CLKCMU_ISPB_BUS, "mout_clkcmu_ispb_bus",
	    mout_clkcmu_acc_bus_p, CLK_CON_MUX_MUX_CLKCMU_ISPB_BUS, 0, 2),

	/* MFC */
	MUX(MOUT_CLKCMU_MFC_MFC, "mout_clkcmu_mfc_mfc",
	    mout_clkcmu_g3d1_switch_p, CLK_CON_MUX_MUX_CLKCMU_MFC_MFC, 0, 2),
	MUX(MOUT_CLKCMU_MFC_WFD, "mout_clkcmu_mfc_wfd",
	    mout_clkcmu_fsys0_bus_p, CLK_CON_MUX_MUX_CLKCMU_MFC_WFD, 0, 2),

	/* MIF */
	MUX(MOUT_CLKCMU_MIF_SWITCH, "mout_clkcmu_mif_switch",
	    mout_clkcmu_mif_switch_p, CLK_CON_MUX_MUX_CLKCMU_MIF_SWITCH, 0, 3),
	MUX(MOUT_CLKCMU_MIF_BUSP, "mout_clkcmu_mif_busp",
	    mout_clkcmu_fsys1_bus_p, CLK_CON_MUX_MUX_CLKCMU_MIF_BUSP, 0, 2),

	/* NPU */
	MUX(MOUT_CLKCMU_NPU_BUS, "mout_clkcmu_npu_bus", mout_clkcmu_npu_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_NPU_BUS, 0, 3),

	/* PERIC0 */
	MUX(MOUT_CLKCMU_PERIC0_BUS, "mout_clkcmu_peric0_bus",
	    mout_clkcmu_peric0_bus_p, CLK_CON_MUX_MUX_CLKCMU_PERIC0_BUS, 0, 1),
	MUX(MOUT_CLKCMU_PERIC0_IP, "mout_clkcmu_peric0_ip",
	    mout_clkcmu_peric0_bus_p, CLK_CON_MUX_MUX_CLKCMU_PERIC0_IP, 0, 1),

	/* PERIC1 */
	MUX(MOUT_CLKCMU_PERIC1_BUS, "mout_clkcmu_peric1_bus",
	    mout_clkcmu_peric0_bus_p, CLK_CON_MUX_MUX_CLKCMU_PERIC1_BUS, 0, 1),
	MUX(MOUT_CLKCMU_PERIC1_IP, "mout_clkcmu_peric1_ip",
	    mout_clkcmu_peric0_bus_p, CLK_CON_MUX_MUX_CLKCMU_PERIC1_IP, 0, 1),

	/* PERIS */
	MUX(MOUT_CLKCMU_PERIS_BUS, "mout_clkcmu_peris_bus",
	    mout_clkcmu_peric0_bus_p, CLK_CON_MUX_MUX_CLKCMU_PERIS_BUS, 0, 1),
};

static const struct samsung_div_clock top_div_clks[] __initconst = {
	/* CMU_TOP_PURECLKCOMP */
	DIV(DOUT_SHARED0_DIV3, "dout_shared0_div3", "mout_shared0_pll",
	    CLK_CON_DIV_PLL_SHARED0_DIV3, 0, 2),
	DIV(DOUT_SHARED0_DIV2, "dout_shared0_div2", "mout_shared0_pll",
	    CLK_CON_DIV_PLL_SHARED0_DIV2, 0, 1),

	DIV(DOUT_SHARED1_DIV3, "dout_shared1_div3", "mout_shared1_pll",
	    CLK_CON_DIV_PLL_SHARED1_DIV3, 0, 2),
	DIV(DOUT_SHARED1_DIV2, "dout_shared1_div2", "mout_shared1_pll",
	    CLK_CON_DIV_PLL_SHARED1_DIV2, 0, 1),
	DIV(DOUT_SHARED1_DIV4, "dout_shared1_div4", "dout_shared1_div2",
	    CLK_CON_DIV_PLL_SHARED1_DIV4, 0, 1),

	DIV(DOUT_SHARED2_DIV3, "dout_shared2_div3", "mout_shared2_pll",
	    CLK_CON_DIV_PLL_SHARED2_DIV3, 0, 2),
	DIV(DOUT_SHARED2_DIV2, "dout_shared2_div2", "mout_shared2_pll",
	    CLK_CON_DIV_PLL_SHARED2_DIV2, 0, 1),
	DIV(DOUT_SHARED2_DIV4, "dout_shared2_div4", "dout_shared2_div2",
	    CLK_CON_DIV_PLL_SHARED2_DIV4, 0, 1),

	DIV(DOUT_SHARED4_DIV2, "dout_shared4_div2", "mout_shared4_pll",
	    CLK_CON_DIV_PLL_SHARED4_DIV2, 0, 1),
	DIV(DOUT_SHARED4_DIV4, "dout_shared4_div4", "dout_shared4_div2",
	    CLK_CON_DIV_PLL_SHARED4_DIV4, 0, 1),

	/* BOOST */
	DIV(DOUT_CLKCMU_CMU_BOOST, "dout_clkcmu_cmu_boost",
	    "gout_clkcmu_cmu_boost", CLK_CON_DIV_DIV_CLKCMU_CMU_BOOST, 0, 2),

	/* ACC */
	DIV(DOUT_CLKCMU_ACC_BUS, "dout_clkcmu_acc_bus", "gout_clkcmu_acc_bus",
	    CLK_CON_DIV_CLKCMU_ACC_BUS, 0, 4),

	/* APM */
	DIV(DOUT_CLKCMU_APM_BUS, "dout_clkcmu_apm_bus", "gout_clkcmu_apm_bus",
	    CLK_CON_DIV_CLKCMU_APM_BUS, 0, 3),

	/* AUD */
	DIV(DOUT_CLKCMU_AUD_CPU, "dout_clkcmu_aud_cpu", "gout_clkcmu_aud_cpu",
	    CLK_CON_DIV_CLKCMU_AUD_CPU, 0, 3),
	DIV(DOUT_CLKCMU_AUD_BUS, "dout_clkcmu_aud_bus", "gout_clkcmu_aud_bus",
	    CLK_CON_DIV_CLKCMU_AUD_BUS, 0, 4),

	/* BUSC */
	DIV(DOUT_CLKCMU_BUSC_BUS, "dout_clkcmu_busc_bus",
	    "gout_clkcmu_busc_bus", CLK_CON_DIV_CLKCMU_BUSC_BUS, 0, 4),

	/* BUSMC */
	DIV(DOUT_CLKCMU_BUSMC_BUS, "dout_clkcmu_busmc_bus",
	    "gout_clkcmu_busmc_bus", CLK_CON_DIV_CLKCMU_BUSMC_BUS, 0, 4),

	/* CORE */
	DIV(DOUT_CLKCMU_CORE_BUS, "dout_clkcmu_core_bus",
	    "gout_clkcmu_core_bus", CLK_CON_DIV_CLKCMU_CORE_BUS, 0, 4),

	/* CPUCL0 */
	DIV(DOUT_CLKCMU_CPUCL0_SWITCH, "dout_clkcmu_cpucl0_switch",
	    "gout_clkcmu_cpucl0_switch", CLK_CON_DIV_CLKCMU_CPUCL0_SWITCH,
	    0, 3),
	DIV(DOUT_CLKCMU_CPUCL0_CLUSTER, "dout_clkcmu_cpucl0_cluster",
	    "gout_clkcmu_cpucl0_cluster", CLK_CON_DIV_CLKCMU_CPUCL0_CLUSTER,
	    0, 3),

	/* CPUCL1 */
	DIV(DOUT_CLKCMU_CPUCL1_SWITCH, "dout_clkcmu_cpucl1_switch",
	    "gout_clkcmu_cpucl1_switch", CLK_CON_DIV_CLKCMU_CPUCL1_SWITCH,
	    0, 3),
	DIV(DOUT_CLKCMU_CPUCL1_CLUSTER, "dout_clkcmu_cpucl1_cluster",
	    "gout_clkcmu_cpucl1_cluster", CLK_CON_DIV_CLKCMU_CPUCL1_CLUSTER,
	    0, 3),

	/* DPTX */
	DIV(DOUT_CLKCMU_DPTX_BUS, "dout_clkcmu_dptx_bus",
	    "gout_clkcmu_dptx_bus", CLK_CON_DIV_CLKCMU_DPTX_BUS, 0, 4),
	DIV(DOUT_CLKCMU_DPTX_DPGTC, "dout_clkcmu_dptx_dpgtc",
	    "gout_clkcmu_dptx_dpgtc", CLK_CON_DIV_CLKCMU_DPTX_DPGTC, 0, 3),

	/* DPUM */
	DIV(DOUT_CLKCMU_DPUM_BUS, "dout_clkcmu_dpum_bus",
	    "gout_clkcmu_dpum_bus", CLK_CON_DIV_CLKCMU_DPUM_BUS, 0, 4),

	/* DPUS */
	DIV(DOUT_CLKCMU_DPUS0_BUS, "dout_clkcmu_dpus0_bus",
	    "gout_clkcmu_dpus0_bus", CLK_CON_DIV_CLKCMU_DPUS0_BUS, 0, 4),
	DIV(DOUT_CLKCMU_DPUS1_BUS, "dout_clkcmu_dpus1_bus",
	    "gout_clkcmu_dpus1_bus", CLK_CON_DIV_CLKCMU_DPUS1_BUS, 0, 4),

	/* FSYS0 */
	DIV(DOUT_CLKCMU_FSYS0_BUS, "dout_clkcmu_fsys0_bus",
	    "gout_clkcmu_fsys0_bus", CLK_CON_DIV_CLKCMU_FSYS0_BUS, 0, 4),

	/* FSYS1 */
	DIV(DOUT_CLKCMU_FSYS1_BUS, "dout_clkcmu_fsys1_bus",
	    "gout_clkcmu_fsys1_bus", CLK_CON_DIV_CLKCMU_FSYS1_BUS, 0, 4),
	DIV(DOUT_CLKCMU_FSYS1_USBDRD, "dout_clkcmu_fsys1_usbdrd",
	    "gout_clkcmu_fsys1_usbdrd", CLK_CON_DIV_CLKCMU_FSYS1_USBDRD, 0, 4),

	/* FSYS2 */
	DIV(DOUT_CLKCMU_FSYS2_BUS, "dout_clkcmu_fsys2_bus",
	    "gout_clkcmu_fsys2_bus", CLK_CON_DIV_CLKCMU_FSYS2_BUS, 0, 4),
	DIV(DOUT_CLKCMU_FSYS2_UFS_EMBD, "dout_clkcmu_fsys2_ufs_embd",
	    "gout_clkcmu_fsys2_ufs_embd", CLK_CON_DIV_CLKCMU_FSYS2_UFS_EMBD,
	    0, 3),
	DIV(DOUT_CLKCMU_FSYS2_ETHERNET, "dout_clkcmu_fsys2_ethernet",
	    "gout_clkcmu_fsys2_ethernet", CLK_CON_DIV_CLKCMU_FSYS2_ETHERNET,
	    0, 3),

	/* G2D */
	DIV(DOUT_CLKCMU_G2D_G2D, "dout_clkcmu_g2d_g2d", "gout_clkcmu_g2d_g2d",
	    CLK_CON_DIV_CLKCMU_G2D_G2D, 0, 4),
	DIV(DOUT_CLKCMU_G2D_MSCL, "dout_clkcmu_g2d_mscl",
	    "gout_clkcmu_g2d_mscl", CLK_CON_DIV_CLKCMU_G2D_MSCL, 0, 4),

	/* G3D0 */
	DIV(DOUT_CLKCMU_G3D00_SWITCH, "dout_clkcmu_g3d00_switch",
	    "gout_clkcmu_g3d00_switch", CLK_CON_DIV_CLKCMU_G3D00_SWITCH, 0, 3),
	DIV(DOUT_CLKCMU_G3D01_SWITCH, "dout_clkcmu_g3d01_switch",
	    "gout_clkcmu_g3d01_switch", CLK_CON_DIV_CLKCMU_G3D01_SWITCH, 0, 3),

	/* G3D1 */
	DIV(DOUT_CLKCMU_G3D1_SWITCH, "dout_clkcmu_g3d1_switch",
	    "gout_clkcmu_g3d1_switch", CLK_CON_DIV_CLKCMU_G3D1_SWITCH, 0, 3),

	/* ISPB */
	DIV(DOUT_CLKCMU_ISPB_BUS, "dout_clkcmu_ispb_bus",
	    "gout_clkcmu_ispb_bus", CLK_CON_DIV_CLKCMU_ISPB_BUS, 0, 4),

	/* MFC */
	DIV(DOUT_CLKCMU_MFC_MFC, "dout_clkcmu_mfc_mfc", "gout_clkcmu_mfc_mfc",
	    CLK_CON_DIV_CLKCMU_MFC_MFC, 0, 4),
	DIV(DOUT_CLKCMU_MFC_WFD, "dout_clkcmu_mfc_wfd", "gout_clkcmu_mfc_wfd",
	    CLK_CON_DIV_CLKCMU_MFC_WFD, 0, 4),

	/* MIF */
	DIV(DOUT_CLKCMU_MIF_BUSP, "dout_clkcmu_mif_busp",
	    "gout_clkcmu_mif_busp", CLK_CON_DIV_CLKCMU_MIF_BUSP, 0, 4),

	/* NPU */
	DIV(DOUT_CLKCMU_NPU_BUS, "dout_clkcmu_npu_bus", "gout_clkcmu_npu_bus",
	    CLK_CON_DIV_CLKCMU_NPU_BUS, 0, 4),

	/* PERIC0 */
	DIV(DOUT_CLKCMU_PERIC0_BUS, "dout_clkcmu_peric0_bus",
	    "gout_clkcmu_peric0_bus", CLK_CON_DIV_CLKCMU_PERIC0_BUS, 0, 4),
	DIV(DOUT_CLKCMU_PERIC0_IP, "dout_clkcmu_peric0_ip",
	    "gout_clkcmu_peric0_ip", CLK_CON_DIV_CLKCMU_PERIC0_IP, 0, 4),

	/* PERIC1 */
	DIV(DOUT_CLKCMU_PERIC1_BUS, "dout_clkcmu_peric1_bus",
	    "gout_clkcmu_peric1_bus", CLK_CON_DIV_CLKCMU_PERIC1_BUS, 0, 4),
	DIV(DOUT_CLKCMU_PERIC1_IP, "dout_clkcmu_peric1_ip",
	    "gout_clkcmu_peric1_ip", CLK_CON_DIV_CLKCMU_PERIC1_IP, 0, 4),

	/* PERIS */
	DIV(DOUT_CLKCMU_PERIS_BUS, "dout_clkcmu_peris_bus",
	    "gout_clkcmu_peris_bus", CLK_CON_DIV_CLKCMU_PERIS_BUS, 0, 4),
};

static const struct samsung_fixed_factor_clock top_fixed_factor_clks[] __initconst = {
	FFACTOR(DOUT_CLKCMU_FSYS0_PCIE, "dout_clkcmu_fsys0_pcie",
		"gout_clkcmu_fsys0_pcie", 1, 4, 0),
};

static const struct samsung_gate_clock top_gate_clks[] __initconst = {
	/* BOOST */
	GATE(GOUT_CLKCMU_CMU_BOOST, "gout_clkcmu_cmu_boost",
	     "mout_clkcmu_cmu_boost", CLK_CON_GAT_GATE_CLKCMU_CMU_BOOST,
	     21, 0, 0),

	GATE(GOUT_CLKCMU_CPUCL0_BOOST, "gout_clkcmu_cpucl0_boost",
	     "dout_cmu_boost", CLK_CON_GAT_CLKCMU_CMU_CPUCL0_BOOST, 21, 0, 0),
	GATE(GOUT_CLKCMU_CPUCL1_BOOST, "gout_clkcmu_cpucl1_boost",
	     "dout_cmu_boost", CLK_CON_GAT_CLKCMU_CMU_CPUCL1_BOOST, 21, 0, 0),
	GATE(GOUT_CLKCMU_CORE_BOOST, "gout_clkcmu_core_boost",
	     "dout_cmu_boost", CLK_CON_GAT_CLKCMU_CMU_CORE_BOOST, 21, 0, 0),
	GATE(GOUT_CLKCMU_BUSC_BOOST, "gout_clkcmu_busc_boost",
	     "dout_cmu_boost", CLK_CON_GAT_CLKCMU_CMU_BUSC_BOOST, 21, 0, 0),

	GATE(GOUT_CLKCMU_BUSMC_BOOST, "gout_clkcmu_busmc_boost",
	     "dout_cmu_boost", CLK_CON_GAT_CLKCMU_CMU_BUSMC_BOOST, 21, 0, 0),
	GATE(GOUT_CLKCMU_MIF_BOOST, "gout_clkcmu_mif_boost", "dout_cmu_boost",
	     CLK_CON_GAT_CLKCMU_CMU_MIF_BOOST, 21, 0, 0),

	/* ACC */
	GATE(GOUT_CLKCMU_ACC_BUS, "gout_clkcmu_acc_bus", "mout_clkcmu_acc_bus",
	     CLK_CON_GAT_GATE_CLKCMU_ACC_BUS, 21, 0, 0),

	/* APM */
	GATE(GOUT_CLKCMU_APM_BUS, "gout_clkcmu_apm_bus", "mout_clkcmu_apm_bus",
	     CLK_CON_GAT_GATE_CLKCMU_APM_BUS, 21, CLK_IGNORE_UNUSED, 0),

	/* AUD */
	GATE(GOUT_CLKCMU_AUD_CPU, "gout_clkcmu_aud_cpu", "mout_clkcmu_aud_cpu",
	     CLK_CON_GAT_GATE_CLKCMU_AUD_CPU, 21, 0, 0),
	GATE(GOUT_CLKCMU_AUD_BUS, "gout_clkcmu_aud_bus", "mout_clkcmu_aud_bus",
	     CLK_CON_GAT_GATE_CLKCMU_AUD_BUS, 21, 0, 0),

	/* BUSC */
	GATE(GOUT_CLKCMU_BUSC_BUS, "gout_clkcmu_busc_bus",
	     "mout_clkcmu_busc_bus", CLK_CON_GAT_GATE_CLKCMU_BUSC_BUS, 21,
	     CLK_IS_CRITICAL, 0),

	/* BUSMC */
	GATE(GOUT_CLKCMU_BUSMC_BUS, "gout_clkcmu_busmc_bus",
	     "mout_clkcmu_busmc_bus", CLK_CON_GAT_GATE_CLKCMU_BUSMC_BUS, 21,
	     CLK_IS_CRITICAL, 0),

	/* CORE */
	GATE(GOUT_CLKCMU_CORE_BUS, "gout_clkcmu_core_bus",
	     "mout_clkcmu_core_bus", CLK_CON_GAT_GATE_CLKCMU_CORE_BUS,
	     21, 0, 0),

	/* CPUCL0 */
	GATE(GOUT_CLKCMU_CPUCL0_SWITCH, "gout_clkcmu_cpucl0_switch",
	     "mout_clkcmu_cpucl0_switch",
	     CLK_CON_GAT_GATE_CLKCMU_CPUCL0_SWITCH, 21, CLK_IGNORE_UNUSED, 0),
	GATE(GOUT_CLKCMU_CPUCL0_CLUSTER, "gout_clkcmu_cpucl0_cluster",
	     "mout_clkcmu_cpucl0_cluster",
	     CLK_CON_GAT_GATE_CLKCMU_CPUCL0_CLUSTER, 21, CLK_IGNORE_UNUSED, 0),

	/* CPUCL1 */
	GATE(GOUT_CLKCMU_CPUCL1_SWITCH, "gout_clkcmu_cpucl1_switch",
	     "mout_clkcmu_cpucl1_switch",
	     CLK_CON_GAT_GATE_CLKCMU_CPUCL1_SWITCH, 21, CLK_IGNORE_UNUSED, 0),
	GATE(GOUT_CLKCMU_CPUCL1_CLUSTER, "gout_clkcmu_cpucl1_cluster",
	     "mout_clkcmu_cpucl1_cluster",
	     CLK_CON_GAT_GATE_CLKCMU_CPUCL1_CLUSTER, 21, CLK_IGNORE_UNUSED, 0),

	/* DPTX */
	GATE(GOUT_CLKCMU_DPTX_BUS, "gout_clkcmu_dptx_bus",
	     "mout_clkcmu_dptx_bus", CLK_CON_GAT_GATE_CLKCMU_DPTX_BUS,
	     21, 0, 0),
	GATE(GOUT_CLKCMU_DPTX_DPGTC, "gout_clkcmu_dptx_dpgtc",
	     "mout_clkcmu_dptx_dpgtc", CLK_CON_GAT_GATE_CLKCMU_DPTX_DPGTC,
	     21, 0, 0),

	/* DPUM */
	GATE(GOUT_CLKCMU_DPUM_BUS, "gout_clkcmu_dpum_bus",
	     "mout_clkcmu_dpum_bus", CLK_CON_GAT_GATE_CLKCMU_DPUM_BUS,
	     21, 0, 0),

	/* DPUS */
	GATE(GOUT_CLKCMU_DPUS0_BUS, "gout_clkcmu_dpus0_bus",
	     "mout_clkcmu_dpus0_bus", CLK_CON_GAT_GATE_CLKCMU_DPUS0_BUS,
	     21, 0, 0),
	GATE(GOUT_CLKCMU_DPUS1_BUS, "gout_clkcmu_dpus1_bus",
	     "mout_clkcmu_dpus1_bus", CLK_CON_GAT_GATE_CLKCMU_DPUS1_BUS,
	     21, 0, 0),

	/* FSYS0 */
	GATE(GOUT_CLKCMU_FSYS0_BUS, "gout_clkcmu_fsys0_bus",
	     "mout_clkcmu_fsys0_bus", CLK_CON_GAT_GATE_CLKCMU_FSYS0_BUS,
	     21, 0, 0),
	GATE(GOUT_CLKCMU_FSYS0_PCIE, "gout_clkcmu_fsys0_pcie",
	     "mout_clkcmu_fsys0_pcie", CLK_CON_GAT_GATE_CLKCMU_FSYS0_PCIE,
	     21, 0, 0),

	/* FSYS1 */
	GATE(GOUT_CLKCMU_FSYS1_BUS, "gout_clkcmu_fsys1_bus",
	     "mout_clkcmu_fsys1_bus", CLK_CON_GAT_GATE_CLKCMU_FSYS1_BUS,
	     21, 0, 0),
	GATE(GOUT_CLKCMU_FSYS1_USBDRD, "gout_clkcmu_fsys1_usbdrd",
	     "mout_clkcmu_fsys1_usbdrd", CLK_CON_GAT_GATE_CLKCMU_FSYS1_USBDRD,
	     21, 0, 0),
	GATE(GOUT_CLKCMU_FSYS1_MMC_CARD, "gout_clkcmu_fsys1_mmc_card",
	     "mout_clkcmu_fsys1_mmc_card",
	     CLK_CON_GAT_GATE_CLKCMU_FSYS1_MMC_CARD, 21, 0, 0),

	/* FSYS2 */
	GATE(GOUT_CLKCMU_FSYS2_BUS, "gout_clkcmu_fsys2_bus",
	     "mout_clkcmu_fsys2_bus", CLK_CON_GAT_GATE_CLKCMU_FSYS2_BUS,
	     21, 0, 0),
	GATE(GOUT_CLKCMU_FSYS2_UFS_EMBD, "gout_clkcmu_fsys2_ufs_embd",
	     "mout_clkcmu_fsys2_ufs_embd",
	     CLK_CON_GAT_GATE_CLKCMU_FSYS2_UFS_EMBD, 21, 0, 0),
	GATE(GOUT_CLKCMU_FSYS2_ETHERNET, "gout_clkcmu_fsys2_ethernet",
	     "mout_clkcmu_fsys2_ethernet",
	     CLK_CON_GAT_GATE_CLKCMU_FSYS2_ETHERNET, 21, 0, 0),

	/* G2D */
	GATE(GOUT_CLKCMU_G2D_G2D, "gout_clkcmu_g2d_g2d",
	     "mout_clkcmu_g2d_g2d", CLK_CON_GAT_GATE_CLKCMU_G2D_G2D, 21, 0, 0),
	GATE(GOUT_CLKCMU_G2D_MSCL, "gout_clkcmu_g2d_mscl",
	     "mout_clkcmu_g2d_mscl", CLK_CON_GAT_GATE_CLKCMU_G2D_MSCL,
	     21, 0, 0),

	/* G3D0 */
	GATE(GOUT_CLKCMU_G3D00_SWITCH, "gout_clkcmu_g3d00_switch",
	     "mout_clkcmu_g3d00_switch", CLK_CON_GAT_GATE_CLKCMU_G3D00_SWITCH,
	     21, 0, 0),
	GATE(GOUT_CLKCMU_G3D01_SWITCH, "gout_clkcmu_g3d01_switch",
	     "mout_clkcmu_g3d01_switch", CLK_CON_GAT_GATE_CLKCMU_G3D01_SWITCH,
	     21, 0, 0),

	/* G3D1 */
	GATE(GOUT_CLKCMU_G3D1_SWITCH, "gout_clkcmu_g3d1_switch",
	     "mout_clkcmu_g3d1_switch", CLK_CON_GAT_GATE_CLKCMU_G3D1_SWITCH,
	     21, 0, 0),

	/* ISPB */
	GATE(GOUT_CLKCMU_ISPB_BUS, "gout_clkcmu_ispb_bus",
	     "mout_clkcmu_ispb_bus", CLK_CON_GAT_GATE_CLKCMU_ISPB_BUS,
	     21, 0, 0),

	/* MFC */
	GATE(GOUT_CLKCMU_MFC_MFC, "gout_clkcmu_mfc_mfc", "mout_clkcmu_mfc_mfc",
	     CLK_CON_GAT_GATE_CLKCMU_MFC_MFC, 21, 0, 0),
	GATE(GOUT_CLKCMU_MFC_WFD, "gout_clkcmu_mfc_wfd", "mout_clkcmu_mfc_wfd",
	     CLK_CON_GAT_GATE_CLKCMU_MFC_WFD, 21, 0, 0),

	/* MIF */
	GATE(GOUT_CLKCMU_MIF_SWITCH, "gout_clkcmu_mif_switch",
	     "mout_clkcmu_mif_switch", CLK_CON_GAT_GATE_CLKCMU_MIF_SWITCH,
	     21, CLK_IGNORE_UNUSED, 0),
	GATE(GOUT_CLKCMU_MIF_BUSP, "gout_clkcmu_mif_busp",
	     "mout_clkcmu_mif_busp", CLK_CON_GAT_GATE_CLKCMU_MIF_BUSP,
	     21, CLK_IGNORE_UNUSED, 0),

	/* NPU */
	GATE(GOUT_CLKCMU_NPU_BUS, "gout_clkcmu_npu_bus", "mout_clkcmu_npu_bus",
	     CLK_CON_GAT_GATE_CLKCMU_NPU_BUS, 21, 0, 0),

	/* PERIC0 */
	GATE(GOUT_CLKCMU_PERIC0_BUS, "gout_clkcmu_peric0_bus",
	     "mout_clkcmu_peric0_bus", CLK_CON_GAT_GATE_CLKCMU_PERIC0_BUS,
	     21, 0, 0),
	GATE(GOUT_CLKCMU_PERIC0_IP, "gout_clkcmu_peric0_ip",
	     "mout_clkcmu_peric0_ip", CLK_CON_GAT_GATE_CLKCMU_PERIC0_IP,
	     21, 0, 0),

	/* PERIC1 */
	GATE(GOUT_CLKCMU_PERIC1_BUS, "gout_clkcmu_peric1_bus",
	     "mout_clkcmu_peric1_bus", CLK_CON_GAT_GATE_CLKCMU_PERIC1_BUS,
	     21, 0, 0),
	GATE(GOUT_CLKCMU_PERIC1_IP, "gout_clkcmu_peric1_ip",
	     "mout_clkcmu_peric1_ip", CLK_CON_GAT_GATE_CLKCMU_PERIC1_IP,
	     21, 0, 0),

	/* PERIS */
	GATE(GOUT_CLKCMU_PERIS_BUS, "gout_clkcmu_peris_bus",
	     "mout_clkcmu_peris_bus", CLK_CON_GAT_GATE_CLKCMU_PERIS_BUS,
	     21, CLK_IGNORE_UNUSED, 0),
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
	.gate_clks		= top_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(top_gate_clks),
	.nr_clk_ids		= CLKS_NR_TOP,
	.clk_regs		= top_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(top_clk_regs),
};

static void __init exynosautov9_cmu_top_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &top_cmu_info);
}

/* Register CMU_TOP early, as it's a dependency for other early domains */
CLK_OF_DECLARE(exynosautov9_cmu_top, "samsung,exynosautov9-cmu-top",
	       exynosautov9_cmu_top_init);

/* ---- CMU_BUSMC ---------------------------------------------------------- */

/* Register Offset definitions for CMU_BUSMC (0x1b200000) */
#define PLL_CON0_MUX_CLKCMU_BUSMC_BUS_USER				0x0600
#define CLK_CON_DIV_DIV_CLK_BUSMC_BUSP					0x1800
#define CLK_CON_GAT_GOUT_BLK_BUSMC_UID_QE_PDMA0_IPCLKPORT_PCLK		0x2078
#define CLK_CON_GAT_GOUT_BLK_BUSMC_UID_QE_SPDMA_IPCLKPORT_PCLK		0x2080

static const unsigned long busmc_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_BUSMC_BUS_USER,
	CLK_CON_DIV_DIV_CLK_BUSMC_BUSP,
	CLK_CON_GAT_GOUT_BLK_BUSMC_UID_QE_PDMA0_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_BUSMC_UID_QE_SPDMA_IPCLKPORT_PCLK,
};

/* List of parent clocks for Muxes in CMU_BUSMC */
PNAME(mout_busmc_bus_user_p) = { "oscclk", "dout_clkcmu_busmc_bus" };

static const struct samsung_mux_clock busmc_mux_clks[] __initconst = {
	MUX(CLK_MOUT_BUSMC_BUS_USER, "mout_busmc_bus_user",
	    mout_busmc_bus_user_p, PLL_CON0_MUX_CLKCMU_BUSMC_BUS_USER, 4, 1),
};

static const struct samsung_div_clock busmc_div_clks[] __initconst = {
	DIV(CLK_DOUT_BUSMC_BUSP, "dout_busmc_busp", "mout_busmc_bus_user",
	    CLK_CON_DIV_DIV_CLK_BUSMC_BUSP, 0, 3),
};

static const struct samsung_gate_clock busmc_gate_clks[] __initconst = {
	GATE(CLK_GOUT_BUSMC_PDMA0_PCLK, "gout_busmc_pdma0_pclk",
	     "dout_busmc_busp",
	     CLK_CON_GAT_GOUT_BLK_BUSMC_UID_QE_PDMA0_IPCLKPORT_PCLK, 21,
	     0, 0),
	GATE(CLK_GOUT_BUSMC_SPDMA_PCLK, "gout_busmc_spdma_pclk",
	     "dout_busmc_busp",
	     CLK_CON_GAT_GOUT_BLK_BUSMC_UID_QE_SPDMA_IPCLKPORT_PCLK, 21,
	     0, 0),
};

static const struct samsung_cmu_info busmc_cmu_info __initconst = {
	.mux_clks		= busmc_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(busmc_mux_clks),
	.div_clks		= busmc_div_clks,
	.nr_div_clks		= ARRAY_SIZE(busmc_div_clks),
	.gate_clks		= busmc_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(busmc_gate_clks),
	.nr_clk_ids		= CLKS_NR_BUSMC,
	.clk_regs		= busmc_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(busmc_clk_regs),
	.clk_name		= "dout_clkcmu_busmc_bus",
};

/* ---- CMU_CORE ----------------------------------------------------------- */

/* Register Offset definitions for CMU_CORE (0x1b030000) */
#define PLL_CON0_MUX_CLKCMU_CORE_BUS_USER				0x0600
#define CLK_CON_MUX_MUX_CORE_CMUREF					0x1000
#define CLK_CON_DIV_DIV_CLK_CORE_BUSP					0x1800
#define CLK_CON_GAT_CLK_BLK_CORE_UID_CCI_IPCLKPORT_CLK			0x2000
#define CLK_CON_GAT_CLK_BLK_CORE_UID_CCI_IPCLKPORT_PCLK			0x2004
#define CLK_CON_GAT_CLK_BLK_CORE_UID_CORE_CMU_CORE_IPCLKPORT_PCLK	0x2008

static const unsigned long core_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_CORE_BUS_USER,
	CLK_CON_MUX_MUX_CORE_CMUREF,
	CLK_CON_DIV_DIV_CLK_CORE_BUSP,
	CLK_CON_GAT_CLK_BLK_CORE_UID_CCI_IPCLKPORT_CLK,
	CLK_CON_GAT_CLK_BLK_CORE_UID_CCI_IPCLKPORT_PCLK,
	CLK_CON_GAT_CLK_BLK_CORE_UID_CORE_CMU_CORE_IPCLKPORT_PCLK,
};

/* List of parent clocks for Muxes in CMU_CORE */
PNAME(mout_core_bus_user_p) = { "oscclk", "dout_clkcmu_core_bus" };

static const struct samsung_mux_clock core_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CORE_BUS_USER, "mout_core_bus_user", mout_core_bus_user_p,
	    PLL_CON0_MUX_CLKCMU_CORE_BUS_USER, 4, 1),
};

static const struct samsung_div_clock core_div_clks[] __initconst = {
	DIV(CLK_DOUT_CORE_BUSP, "dout_core_busp", "mout_core_bus_user",
	    CLK_CON_DIV_DIV_CLK_CORE_BUSP, 0, 3),
};

static const struct samsung_gate_clock core_gate_clks[] __initconst = {
	GATE(CLK_GOUT_CORE_CCI_CLK, "gout_core_cci_clk", "mout_core_bus_user",
	     CLK_CON_GAT_CLK_BLK_CORE_UID_CCI_IPCLKPORT_CLK, 21,
	     CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_CORE_CCI_PCLK, "gout_core_cci_pclk", "dout_core_busp",
	     CLK_CON_GAT_CLK_BLK_CORE_UID_CCI_IPCLKPORT_PCLK, 21,
	     CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_CORE_CMU_CORE_PCLK, "gout_core_cmu_core_pclk",
	     "dout_core_busp",
	     CLK_CON_GAT_CLK_BLK_CORE_UID_CORE_CMU_CORE_IPCLKPORT_PCLK, 21,
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
	.clk_name		= "dout_clkcmu_core_bus",
};

/* ---- CMU_FSYS0 ---------------------------------------------------------- */

/* Register Offset definitions for CMU_FSYS2 (0x17700000) */
#define PLL_CON0_MUX_CLKCMU_FSYS0_BUS_USER	0x0600
#define PLL_CON0_MUX_CLKCMU_FSYS0_PCIE_USER	0x0610
#define CLK_CON_GAT_CLK_BLK_FSYS0_UID_FSYS0_CMU_FSYS0_IPCLKPORT_PCLK	0x2000

#define CLK_CON_GAT_CLK_BLK_FSYS0_UID_PCIE_GEN3_2L0_X1_PHY_REFCLK_IN	0x2004
#define CLK_CON_GAT_CLK_BLK_FSYS0_UID_PCIE_GEN3_2L0_X2_PHY_REFCLK_IN	0x2008
#define CLK_CON_GAT_CLK_BLK_FSYS0_UID_PCIE_GEN3_2L1_X1_PHY_REFCLK_IN	0x200c
#define CLK_CON_GAT_CLK_BLK_FSYS0_UID_PCIE_GEN3_2L1_X2_PHY_REFCLK_IN	0x2010
#define CLK_CON_GAT_CLK_BLK_FSYS0_UID_PCIE_GEN3_4L_X2_PHY_REFCLK_IN	0x2014
#define CLK_CON_GAT_CLK_BLK_FSYS0_UID_PCIE_GEN3_4L_X4_PHY_REFCLK_IN	0x2018

#define CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L0_X1_DBI_ACLK	0x205c
#define CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L0_X1_MSTR_ACLK	0x2060
#define CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L0_X1_SLV_ACLK	0x2064
#define CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L0_X2_DBI_ACLK	0x206c
#define CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L0_X2_MSTR_ACLK	0x2070
#define CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L0_X2_SLV_ACLK	0x2074
#define CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L0_X2_PIPE_CLK	0x207c

#define CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L1_X1_DBI_ACLK	0x2084
#define CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L1_X1_MSTR_ACLK	0x2088
#define CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L1_X1_SLV_ACLK	0x208c
#define CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L1_X2_DBI_ACLK	0x2094
#define CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L1_X2_MSTR_ACLK	0x2098
#define CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L1_X2_SLV_ACLK	0x209c
#define CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L1_X2_PIPE_CLK	0x20a4

#define CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_4L_X2_DBI_ACLK		0x20ac
#define CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_4L_X2_MSTR_ACLK	0x20b0
#define CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_4L_X2_SLV_ACLK		0x20b4
#define CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_4L_X4_DBI_ACLK		0x20bc
#define CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_4L_X4_MSTR_ACLK	0x20c0
#define CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_4L_X4_SLV_ACLK		0x20c4
#define CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_4L_X4_PIPE_CLK		0x20cc

#define CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3A_2L0_CLK		0x20d4
#define CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3A_2L1_CLK		0x20d8
#define CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3A_4L_CLK		0x20dc
#define CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3B_2L0_CLK		0x20e0
#define CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3B_2L1_CLK		0x20e4
#define CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3B_4L_CLK		0x20e8


static const unsigned long fsys0_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_FSYS0_BUS_USER,
	PLL_CON0_MUX_CLKCMU_FSYS0_PCIE_USER,
	CLK_CON_GAT_CLK_BLK_FSYS0_UID_FSYS0_CMU_FSYS0_IPCLKPORT_PCLK,
	CLK_CON_GAT_CLK_BLK_FSYS0_UID_PCIE_GEN3_2L0_X1_PHY_REFCLK_IN,
	CLK_CON_GAT_CLK_BLK_FSYS0_UID_PCIE_GEN3_2L0_X2_PHY_REFCLK_IN,
	CLK_CON_GAT_CLK_BLK_FSYS0_UID_PCIE_GEN3_2L1_X1_PHY_REFCLK_IN,
	CLK_CON_GAT_CLK_BLK_FSYS0_UID_PCIE_GEN3_2L1_X2_PHY_REFCLK_IN,
	CLK_CON_GAT_CLK_BLK_FSYS0_UID_PCIE_GEN3_4L_X2_PHY_REFCLK_IN,
	CLK_CON_GAT_CLK_BLK_FSYS0_UID_PCIE_GEN3_4L_X4_PHY_REFCLK_IN,
	CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L0_X1_DBI_ACLK,
	CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L0_X1_MSTR_ACLK,
	CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L0_X1_SLV_ACLK,
	CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L0_X2_DBI_ACLK,
	CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L0_X2_MSTR_ACLK,
	CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L0_X2_SLV_ACLK,
	CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L0_X2_PIPE_CLK,
	CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L1_X1_DBI_ACLK,
	CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L1_X1_MSTR_ACLK,
	CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L1_X1_SLV_ACLK,
	CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L1_X2_DBI_ACLK,
	CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L1_X2_MSTR_ACLK,
	CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L1_X2_SLV_ACLK,
	CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L1_X2_PIPE_CLK,
	CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_4L_X2_DBI_ACLK,
	CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_4L_X2_MSTR_ACLK,
	CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_4L_X2_SLV_ACLK,
	CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_4L_X4_DBI_ACLK,
	CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_4L_X4_MSTR_ACLK,
	CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_4L_X4_SLV_ACLK,
	CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_4L_X4_PIPE_CLK,
	CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3A_2L0_CLK,
	CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3A_2L1_CLK,
	CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3A_4L_CLK,
	CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3B_2L0_CLK,
	CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3B_2L1_CLK,
	CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3B_4L_CLK,
};

/* List of parent clocks for Muxes in CMU_FSYS0 */
PNAME(mout_fsys0_bus_user_p) = { "oscclk", "dout_clkcmu_fsys0_bus" };
PNAME(mout_fsys0_pcie_user_p) = { "oscclk", "dout_clkcmu_fsys0_pcie" };

static const struct samsung_mux_clock fsys0_mux_clks[] __initconst = {
	MUX(CLK_MOUT_FSYS0_BUS_USER, "mout_fsys0_bus_user",
	    mout_fsys0_bus_user_p, PLL_CON0_MUX_CLKCMU_FSYS0_BUS_USER, 4, 1),
	MUX(CLK_MOUT_FSYS0_PCIE_USER, "mout_fsys0_pcie_user",
	    mout_fsys0_pcie_user_p, PLL_CON0_MUX_CLKCMU_FSYS0_PCIE_USER, 4, 1),
};

static const struct samsung_gate_clock fsys0_gate_clks[] __initconst = {
	GATE(CLK_GOUT_FSYS0_BUS_PCLK, "gout_fsys0_bus_pclk",
	     "mout_fsys0_bus_user",
	     CLK_CON_GAT_CLK_BLK_FSYS0_UID_FSYS0_CMU_FSYS0_IPCLKPORT_PCLK,
	     21, CLK_IGNORE_UNUSED, 0),

	/* Gen3 2L0 */
	GATE(CLK_GOUT_FSYS0_PCIE_GEN3_2L0_X1_REFCLK,
	     "gout_fsys0_pcie_gen3_2l0_x1_refclk", "mout_fsys0_pcie_user",
	     CLK_CON_GAT_CLK_BLK_FSYS0_UID_PCIE_GEN3_2L0_X1_PHY_REFCLK_IN,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS0_PCIE_GEN3_2L0_X2_REFCLK,
	     "gout_fsys0_pcie_gen3_2l0_x2_refclk", "mout_fsys0_pcie_user",
	     CLK_CON_GAT_CLK_BLK_FSYS0_UID_PCIE_GEN3_2L0_X2_PHY_REFCLK_IN,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS0_PCIE_GEN3_2L0_X1_DBI_ACLK,
	     "gout_fsys0_pcie_gen3_2l0_x1_dbi_aclk", "mout_fsys0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L0_X1_DBI_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS0_PCIE_GEN3_2L0_X1_MSTR_ACLK,
	     "gout_fsys0_pcie_gen3_2l0_x1_mstr_aclk", "mout_fsys0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L0_X1_MSTR_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS0_PCIE_GEN3_2L0_X1_SLV_ACLK,
	     "gout_fsys0_pcie_gen3_2l0_x1_slv_aclk", "mout_fsys0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L0_X1_SLV_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS0_PCIE_GEN3_2L0_X2_DBI_ACLK,
	     "gout_fsys0_pcie_gen3_2l0_x2_dbi_aclk", "mout_fsys0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L0_X2_DBI_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS0_PCIE_GEN3_2L0_X2_MSTR_ACLK,
	     "gout_fsys0_pcie_gen3_2l0_x2_mstr_aclk", "mout_fsys0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L0_X2_MSTR_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS0_PCIE_GEN3_2L0_X2_SLV_ACLK,
	     "gout_fsys0_pcie_gen3_2l0_x2_slv_aclk", "mout_fsys0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L0_X2_SLV_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS0_PCIE_GEN3A_2L0_CLK,
	     "gout_fsys0_pcie_gen3a_2l0_clk", "mout_fsys0_pcie_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3A_2L0_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS0_PCIE_GEN3B_2L0_CLK,
	     "gout_fsys0_pcie_gen3b_2l0_clk", "mout_fsys0_pcie_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3B_2L0_CLK,
	     21, 0, 0),

	/* Gen3 2L1 */
	GATE(CLK_GOUT_FSYS0_PCIE_GEN3_2L1_X1_REFCLK,
	     "gout_fsys0_pcie_gen3_2l1_x1_refclk", "mout_fsys0_pcie_user",
	     CLK_CON_GAT_CLK_BLK_FSYS0_UID_PCIE_GEN3_2L1_X1_PHY_REFCLK_IN,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS0_PCIE_GEN3_2L1_X2_REFCLK,
	     "gout_fsys0_pcie_gen3_2l1_x2_refclk", "mout_fsys0_pcie_user",
	     CLK_CON_GAT_CLK_BLK_FSYS0_UID_PCIE_GEN3_2L1_X2_PHY_REFCLK_IN,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS0_PCIE_GEN3_2L1_X1_DBI_ACLK,
	     "gout_fsys0_pcie_gen3_2l1_x1_dbi_aclk", "mout_fsys0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L1_X1_DBI_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS0_PCIE_GEN3_2L1_X1_MSTR_ACLK,
	     "gout_fsys0_pcie_gen3_2l1_x1_mstr_aclk", "mout_fsys0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L1_X1_MSTR_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS0_PCIE_GEN3_2L1_X1_SLV_ACLK,
	     "gout_fsys0_pcie_gen3_2l1_x1_slv_aclk", "mout_fsys0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L1_X1_SLV_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS0_PCIE_GEN3_2L1_X2_DBI_ACLK,
	     "gout_fsys0_pcie_gen3_2l1_x2_dbi_aclk", "mout_fsys0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L1_X2_DBI_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS0_PCIE_GEN3_2L1_X2_MSTR_ACLK,
	     "gout_fsys0_pcie_gen3_2l1_x2_mstr_aclk", "mout_fsys0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L1_X2_MSTR_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS0_PCIE_GEN3_2L1_X2_SLV_ACLK,
	     "gout_fsys0_pcie_gen3_2l1_x2_slv_aclk", "mout_fsys0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_2L1_X2_SLV_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS0_PCIE_GEN3A_2L1_CLK,
	     "gout_fsys0_pcie_gen3a_2l1_clk", "mout_fsys0_pcie_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3A_2L1_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS0_PCIE_GEN3B_2L1_CLK,
	     "gout_fsys0_pcie_gen3b_2l1_clk", "mout_fsys0_pcie_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3B_2L1_CLK,
	     21, 0, 0),

	/* Gen3 4L */
	GATE(CLK_GOUT_FSYS0_PCIE_GEN3_4L_X2_REFCLK,
	     "gout_fsys0_pcie_gen3_4l_x2_refclk", "mout_fsys0_pcie_user",
	     CLK_CON_GAT_CLK_BLK_FSYS0_UID_PCIE_GEN3_4L_X2_PHY_REFCLK_IN,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS0_PCIE_GEN3_4L_X4_REFCLK,
	     "gout_fsys0_pcie_gen3_4l_x4_refclk", "mout_fsys0_pcie_user",
	     CLK_CON_GAT_CLK_BLK_FSYS0_UID_PCIE_GEN3_4L_X4_PHY_REFCLK_IN,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS0_PCIE_GEN3_4L_X2_DBI_ACLK,
	     "gout_fsys0_pcie_gen3_4l_x2_dbi_aclk", "mout_fsys0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_4L_X2_DBI_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS0_PCIE_GEN3_4L_X2_MSTR_ACLK,
	     "gout_fsys0_pcie_gen3_4l_x2_mstr_aclk", "mout_fsys0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_4L_X2_MSTR_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS0_PCIE_GEN3_4L_X2_SLV_ACLK,
	     "gout_fsys0_pcie_gen3_4l_x2_slv_aclk", "mout_fsys0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_4L_X2_SLV_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS0_PCIE_GEN3_4L_X4_DBI_ACLK,
	     "gout_fsys0_pcie_gen3_4l_x4_dbi_aclk", "mout_fsys0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_4L_X4_DBI_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS0_PCIE_GEN3_4L_X4_MSTR_ACLK,
	     "gout_fsys0_pcie_gen3_4l_x4_mstr_aclk", "mout_fsys0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_4L_X4_MSTR_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS0_PCIE_GEN3_4L_X4_SLV_ACLK,
	     "gout_fsys0_pcie_gen3_4l_x4_slv_aclk", "mout_fsys0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3_4L_X4_SLV_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS0_PCIE_GEN3A_4L_CLK,
	     "gout_fsys0_pcie_gen3a_4l_clk", "mout_fsys0_pcie_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3A_4L_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS0_PCIE_GEN3B_4L_CLK,
	     "gout_fsys0_pcie_gen3b_4l_clk", "mout_fsys0_pcie_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS0_UID_PCIE_GEN3B_4L_CLK,
	     21, 0, 0),
};

static const struct samsung_cmu_info fsys0_cmu_info __initconst = {
	.mux_clks		= fsys0_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(fsys0_mux_clks),
	.gate_clks		= fsys0_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(fsys0_gate_clks),
	.nr_clk_ids		= CLKS_NR_FSYS0,
	.clk_regs		= fsys0_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(fsys0_clk_regs),
	.clk_name		= "dout_clkcmu_fsys0_bus",
};

/* ---- CMU_FSYS1 ---------------------------------------------------------- */

/* Register Offset definitions for CMU_FSYS1 (0x17040000) */
#define PLL_LOCKTIME_PLL_MMC			0x0000
#define PLL_CON0_PLL_MMC			0x0100
#define PLL_CON3_PLL_MMC			0x010c
#define PLL_CON0_MUX_CLKCMU_FSYS1_BUS_USER	0x0600
#define PLL_CON0_MUX_CLKCMU_FSYS1_MMC_CARD_USER	0x0610
#define PLL_CON0_MUX_CLKCMU_FSYS1_USBDRD_USER	0x0620

#define CLK_CON_MUX_MUX_CLK_FSYS1_MMC_CARD	0x1000
#define CLK_CON_DIV_DIV_CLK_FSYS1_MMC_CARD	0x1800

#define CLK_CON_GAT_GOUT_BLK_FSYS1_UID_FSYS1_CMU_FSYS1_IPCLKPORT_PCLK	0x2018
#define CLK_CON_GAT_GOUT_BLK_FSYS1_UID_MMC_CARD_IPCLKPORT_SDCLKIN	0x202c
#define CLK_CON_GAT_GOUT_BLK_FSYS1_UID_MMC_CARD_IPCLKPORT_I_ACLK	0x2028

#define CLK_CON_GAT_GOUT_BLK_FSYS1_UID_USB20DRD_0_REF_CLK_40		0x204c
#define CLK_CON_GAT_GOUT_BLK_FSYS1_UID_USB20DRD_1_REF_CLK_40		0x2058
#define CLK_CON_GAT_GOUT_BLK_FSYS1_UID_USB30DRD_0_REF_CLK_40		0x2064
#define CLK_CON_GAT_GOUT_BLK_FSYS1_UID_USB30DRD_1_REF_CLK_40		0x2070

#define CLK_CON_GAT_GOUT_BLK_FSYS1_UID_US_D_USB2_0_IPCLKPORT_ACLK	0x2074
#define CLK_CON_GAT_GOUT_BLK_FSYS1_UID_US_D_USB2_1_IPCLKPORT_ACLK	0x2078
#define CLK_CON_GAT_GOUT_BLK_FSYS1_UID_US_D_USB3_0_IPCLKPORT_ACLK	0x207c
#define CLK_CON_GAT_GOUT_BLK_FSYS1_UID_US_D_USB3_1_IPCLKPORT_ACLK	0x2080

static const unsigned long fsys1_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_FSYS1_BUS_USER,
};

static const struct samsung_pll_clock fsys1_pll_clks[] __initconst = {
	PLL(pll_0831x, FOUT_MMC_PLL, "fout_mmc_pll", "oscclk",
	    PLL_LOCKTIME_PLL_MMC, PLL_CON3_PLL_MMC, NULL),
};

/* List of parent clocks for Muxes in CMU_FSYS1 */
PNAME(mout_fsys1_bus_user_p) = { "oscclk", "dout_clkcmu_fsys1_bus" };
PNAME(mout_fsys1_mmc_pll_p) = { "oscclk", "fout_mmc_pll" };
PNAME(mout_fsys1_mmc_card_user_p) = { "oscclk", "gout_clkcmu_fsys1_mmc_card" };
PNAME(mout_fsys1_usbdrd_user_p) = { "oscclk", "dout_clkcmu_fsys1_usbdrd" };
PNAME(mout_fsys1_mmc_card_p) = { "mout_fsys1_mmc_card_user",
				 "mout_fsys1_mmc_pll" };

static const struct samsung_mux_clock fsys1_mux_clks[] __initconst = {
	MUX(CLK_MOUT_FSYS1_BUS_USER, "mout_fsys1_bus_user",
	    mout_fsys1_bus_user_p, PLL_CON0_MUX_CLKCMU_FSYS1_BUS_USER, 4, 1),
	MUX(CLK_MOUT_FSYS1_MMC_PLL, "mout_fsys1_mmc_pll", mout_fsys1_mmc_pll_p,
	    PLL_CON0_PLL_MMC, 4, 1),
	MUX(CLK_MOUT_FSYS1_MMC_CARD_USER, "mout_fsys1_mmc_card_user",
	    mout_fsys1_mmc_card_user_p, PLL_CON0_MUX_CLKCMU_FSYS1_MMC_CARD_USER,
	    4, 1),
	MUX(CLK_MOUT_FSYS1_USBDRD_USER, "mout_fsys1_usbdrd_user",
	    mout_fsys1_usbdrd_user_p, PLL_CON0_MUX_CLKCMU_FSYS1_USBDRD_USER,
	    4, 1),
	MUX(CLK_MOUT_FSYS1_MMC_CARD, "mout_fsys1_mmc_card",
	    mout_fsys1_mmc_card_p, CLK_CON_MUX_MUX_CLK_FSYS1_MMC_CARD,
	    0, 1),
};

static const struct samsung_div_clock fsys1_div_clks[] __initconst = {
	DIV(CLK_DOUT_FSYS1_MMC_CARD, "dout_fsys1_mmc_card",
	    "mout_fsys1_mmc_card",
	    CLK_CON_DIV_DIV_CLK_FSYS1_MMC_CARD, 0, 9),
};

static const struct samsung_gate_clock fsys1_gate_clks[] __initconst = {
	GATE(CLK_GOUT_FSYS1_PCLK, "gout_fsys1_pclk", "mout_fsys1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS1_UID_FSYS1_CMU_FSYS1_IPCLKPORT_PCLK,
	     21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_FSYS1_MMC_CARD_SDCLKIN, "gout_fsys1_mmc_card_sdclkin",
	     "dout_fsys1_mmc_card",
	     CLK_CON_GAT_GOUT_BLK_FSYS1_UID_MMC_CARD_IPCLKPORT_SDCLKIN,
	     21, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_FSYS1_MMC_CARD_ACLK, "gout_fsys1_mmc_card_aclk",
	     "dout_fsys1_mmc_card",
	     CLK_CON_GAT_GOUT_BLK_FSYS1_UID_MMC_CARD_IPCLKPORT_I_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS1_USB20DRD_0_REFCLK, "gout_fsys1_usb20drd_0_refclk",
	     "mout_fsys1_usbdrd_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS1_UID_USB20DRD_0_REF_CLK_40,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS1_USB20DRD_1_REFCLK, "gout_fsys1_usb20drd_1_refclk",
	     "mout_fsys1_usbdrd_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS1_UID_USB20DRD_1_REF_CLK_40,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS1_USB30DRD_0_REFCLK, "gout_fsys1_usb30drd_0_refclk",
	     "mout_fsys1_usbdrd_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS1_UID_USB30DRD_0_REF_CLK_40,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS1_USB30DRD_1_REFCLK, "gout_fsys1_usb30drd_1_refclk",
	     "mout_fsys1_usbdrd_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS1_UID_USB30DRD_1_REF_CLK_40,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS1_USB20_0_ACLK, "gout_fsys1_usb20_0_aclk",
	     "mout_fsys1_usbdrd_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS1_UID_US_D_USB2_0_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS1_USB20_1_ACLK, "gout_fsys1_usb20_1_aclk",
	     "mout_fsys1_usbdrd_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS1_UID_US_D_USB2_1_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS1_USB30_0_ACLK, "gout_fsys1_usb30_0_aclk",
	     "mout_fsys1_usbdrd_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS1_UID_US_D_USB3_0_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS1_USB30_1_ACLK, "gout_fsys1_usb30_1_aclk",
	     "mout_fsys1_usbdrd_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS1_UID_US_D_USB3_1_IPCLKPORT_ACLK,
	     21, 0, 0),
};

static const struct samsung_cmu_info fsys1_cmu_info __initconst = {
	.pll_clks		= fsys1_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(fsys1_pll_clks),
	.mux_clks		= fsys1_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(fsys1_mux_clks),
	.div_clks		= fsys1_div_clks,
	.nr_div_clks		= ARRAY_SIZE(fsys1_div_clks),
	.gate_clks		= fsys1_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(fsys1_gate_clks),
	.nr_clk_ids		= CLKS_NR_FSYS1,
	.clk_regs		= fsys1_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(fsys1_clk_regs),
	.clk_name		= "dout_clkcmu_fsys1_bus",
};

/* ---- CMU_FSYS2 ---------------------------------------------------------- */

/* Register Offset definitions for CMU_FSYS2 (0x17c00000) */
#define PLL_CON0_MUX_CLKCMU_FSYS2_BUS_USER	0x0600
#define PLL_CON0_MUX_CLKCMU_FSYS2_UFS_EMBD_USER	0x0620
#define PLL_CON0_MUX_CLKCMU_FSYS2_ETHERNET_USER	0x0610
#define CLK_CON_GAT_GOUT_BLK_FSYS2_UID_UFS_EMBD0_IPCLKPORT_I_ACLK	0x2098
#define CLK_CON_GAT_GOUT_BLK_FSYS2_UID_UFS_EMBD0_IPCLKPORT_I_CLK_UNIPRO	0x209c
#define CLK_CON_GAT_GOUT_BLK_FSYS2_UID_UFS_EMBD1_IPCLKPORT_I_ACLK	0x20a4
#define CLK_CON_GAT_GOUT_BLK_FSYS2_UID_UFS_EMBD1_IPCLKPORT_I_CLK_UNIPRO	0x20a8

static const unsigned long fsys2_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_FSYS2_BUS_USER,
	PLL_CON0_MUX_CLKCMU_FSYS2_UFS_EMBD_USER,
	PLL_CON0_MUX_CLKCMU_FSYS2_ETHERNET_USER,
	CLK_CON_GAT_GOUT_BLK_FSYS2_UID_UFS_EMBD0_IPCLKPORT_I_ACLK,
	CLK_CON_GAT_GOUT_BLK_FSYS2_UID_UFS_EMBD0_IPCLKPORT_I_CLK_UNIPRO,
	CLK_CON_GAT_GOUT_BLK_FSYS2_UID_UFS_EMBD1_IPCLKPORT_I_ACLK,
	CLK_CON_GAT_GOUT_BLK_FSYS2_UID_UFS_EMBD1_IPCLKPORT_I_CLK_UNIPRO,
};

/* List of parent clocks for Muxes in CMU_FSYS2 */
PNAME(mout_fsys2_bus_user_p) = { "oscclk", "dout_clkcmu_fsys2_bus" };
PNAME(mout_fsys2_ufs_embd_user_p) = { "oscclk", "dout_clkcmu_fsys2_ufs_embd" };
PNAME(mout_fsys2_ethernet_user_p) = { "oscclk", "dout_clkcmu_fsys2_ethernet" };

static const struct samsung_mux_clock fsys2_mux_clks[] __initconst = {
	MUX(CLK_MOUT_FSYS2_BUS_USER, "mout_fsys2_bus_user",
	    mout_fsys2_bus_user_p, PLL_CON0_MUX_CLKCMU_FSYS2_BUS_USER, 4, 1),
	MUX(CLK_MOUT_FSYS2_UFS_EMBD_USER, "mout_fsys2_ufs_embd_user",
	    mout_fsys2_ufs_embd_user_p,
	    PLL_CON0_MUX_CLKCMU_FSYS2_UFS_EMBD_USER, 4, 1),
	MUX(CLK_MOUT_FSYS2_ETHERNET_USER, "mout_fsys2_ethernet_user",
	    mout_fsys2_ethernet_user_p,
	    PLL_CON0_MUX_CLKCMU_FSYS2_ETHERNET_USER, 4, 1),
};

static const struct samsung_gate_clock fsys2_gate_clks[] __initconst = {
	GATE(CLK_GOUT_FSYS2_UFS_EMBD0_ACLK, "gout_fsys2_ufs_embd0_aclk",
	     "mout_fsys2_ufs_embd_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS2_UID_UFS_EMBD0_IPCLKPORT_I_ACLK, 21,
	     0, 0),
	GATE(CLK_GOUT_FSYS2_UFS_EMBD0_UNIPRO, "gout_fsys2_ufs_embd0_unipro",
	     "mout_fsys2_ufs_embd_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS2_UID_UFS_EMBD0_IPCLKPORT_I_CLK_UNIPRO,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS2_UFS_EMBD1_ACLK, "gout_fsys2_ufs_embd1_aclk",
	     "mout_fsys2_ufs_embd_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS2_UID_UFS_EMBD1_IPCLKPORT_I_ACLK, 21,
	     0, 0),
	GATE(CLK_GOUT_FSYS2_UFS_EMBD1_UNIPRO, "gout_fsys2_ufs_embd1_unipro",
	     "mout_fsys2_ufs_embd_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS2_UID_UFS_EMBD1_IPCLKPORT_I_CLK_UNIPRO,
	     21, 0, 0),
};

static const struct samsung_cmu_info fsys2_cmu_info __initconst = {
	.mux_clks		= fsys2_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(fsys2_mux_clks),
	.gate_clks		= fsys2_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(fsys2_gate_clks),
	.nr_clk_ids		= CLKS_NR_FSYS2,
	.clk_regs		= fsys2_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(fsys2_clk_regs),
	.clk_name		= "dout_clkcmu_fsys2_bus",
};

/* ---- CMU_PERIC0 --------------------------------------------------------- */

/* Register Offset definitions for CMU_PERIC0 (0x10200000) */
#define PLL_CON0_MUX_CLKCMU_PERIC0_BUS_USER	0x0600
#define PLL_CON0_MUX_CLKCMU_PERIC0_IP_USER	0x0610
#define CLK_CON_MUX_MUX_CLK_PERIC0_USI00_USI	0x1000
#define CLK_CON_MUX_MUX_CLK_PERIC0_USI01_USI	0x1004
#define CLK_CON_MUX_MUX_CLK_PERIC0_USI02_USI	0x1008
#define CLK_CON_MUX_MUX_CLK_PERIC0_USI03_USI	0x100c
#define CLK_CON_MUX_MUX_CLK_PERIC0_USI04_USI	0x1010
#define CLK_CON_MUX_MUX_CLK_PERIC0_USI05_USI	0x1014
#define CLK_CON_MUX_MUX_CLK_PERIC0_USI_I2C	0x1018
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI00_USI	0x1800
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI01_USI	0x1804
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI02_USI	0x1808
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI03_USI	0x180c
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI04_USI	0x1810
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI05_USI	0x1814
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI_I2C	0x1818
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_0	0x2014
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_1	0x2018
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_2	0x2024
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_3	0x2028
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_4	0x202c
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_5	0x2030
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_6	0x2034
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_7	0x2038
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_8	0x203c
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_9	0x2040
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_10	0x201c
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_11	0x2020
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_0	0x2044
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_1	0x2048
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_2	0x2058
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_3	0x205c
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_4	0x2060
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_5	0x2064
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_6	0x2068
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_7	0x206c
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_8	0x2070
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_9	0x2074
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_10	0x204c
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_11	0x2050

static const unsigned long peric0_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_PERIC0_BUS_USER,
	PLL_CON0_MUX_CLKCMU_PERIC0_IP_USER,
	CLK_CON_MUX_MUX_CLK_PERIC0_USI00_USI,
	CLK_CON_MUX_MUX_CLK_PERIC0_USI01_USI,
	CLK_CON_MUX_MUX_CLK_PERIC0_USI02_USI,
	CLK_CON_MUX_MUX_CLK_PERIC0_USI03_USI,
	CLK_CON_MUX_MUX_CLK_PERIC0_USI04_USI,
	CLK_CON_MUX_MUX_CLK_PERIC0_USI05_USI,
	CLK_CON_MUX_MUX_CLK_PERIC0_USI_I2C,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI00_USI,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI01_USI,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI02_USI,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI03_USI,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI04_USI,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI05_USI,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI_I2C,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_0,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_1,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_2,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_3,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_4,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_5,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_6,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_7,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_8,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_9,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_10,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_11,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_0,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_1,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_2,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_3,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_4,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_7,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_5,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_6,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_8,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_9,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_10,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_11,
};

/* List of parent clocks for Muxes in CMU_PERIC0 */
PNAME(mout_peric0_bus_user_p) = { "oscclk", "dout_clkcmu_peric0_bus" };
PNAME(mout_peric0_ip_user_p) = { "oscclk", "dout_clkcmu_peric0_ip" };
PNAME(mout_peric0_usi_p) = { "oscclk", "mout_peric0_ip_user" };

static const struct samsung_mux_clock peric0_mux_clks[] __initconst = {
	MUX(CLK_MOUT_PERIC0_BUS_USER, "mout_peric0_bus_user",
	    mout_peric0_bus_user_p, PLL_CON0_MUX_CLKCMU_PERIC0_BUS_USER, 4, 1),
	MUX(CLK_MOUT_PERIC0_IP_USER, "mout_peric0_ip_user",
	    mout_peric0_ip_user_p, PLL_CON0_MUX_CLKCMU_PERIC0_IP_USER, 4, 1),
	/* USI00 ~ USI05 */
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
	/* USI_I2C */
	MUX(CLK_MOUT_PERIC0_USI_I2C, "mout_peric0_usi_i2c",
	    mout_peric0_usi_p, CLK_CON_MUX_MUX_CLK_PERIC0_USI_I2C, 0, 1),
};

static const struct samsung_div_clock peric0_div_clks[] __initconst = {
	/* USI00 ~ USI05 */
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
	/* USI_I2C */
	DIV(CLK_DOUT_PERIC0_USI_I2C, "dout_peric0_usi_i2c",
	    "mout_peric0_usi_i2c", CLK_CON_DIV_DIV_CLK_PERIC0_USI_I2C, 0, 4),
};

static const struct samsung_gate_clock peric0_gate_clks[] __initconst = {
	/* IPCLK */
	GATE(CLK_GOUT_PERIC0_IPCLK_0, "gout_peric0_ipclk_0",
	     "dout_peric0_usi00_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_0,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_IPCLK_1, "gout_peric0_ipclk_1",
	     "dout_peric0_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_1,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_IPCLK_2, "gout_peric0_ipclk_2",
	     "dout_peric0_usi01_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_2,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_IPCLK_3, "gout_peric0_ipclk_3",
	     "dout_peric0_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_3,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_IPCLK_4, "gout_peric0_ipclk_4",
	     "dout_peric0_usi02_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_4,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_IPCLK_5, "gout_peric0_ipclk_5",
	     "dout_peric0_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_5,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_IPCLK_6, "gout_peric0_ipclk_6",
	     "dout_peric0_usi03_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_6,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_IPCLK_7, "gout_peric0_ipclk_7",
	     "dout_peric0_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_7,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_IPCLK_8, "gout_peric0_ipclk_8",
	     "dout_peric0_usi04_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_8,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_IPCLK_9, "gout_peric0_ipclk_9",
	     "dout_peric0_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_9,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_IPCLK_10, "gout_peric0_ipclk_10",
	     "dout_peric0_usi05_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_10,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_IPCLK_11, "gout_peric0_ipclk_11",
	     "dout_peric0_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_11,
	     21, 0, 0),

	/* PCLK */
	GATE(CLK_GOUT_PERIC0_PCLK_0, "gout_peric0_pclk_0",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_0,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PCLK_1, "gout_peric0_pclk_1",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_1,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PCLK_2, "gout_peric0_pclk_2",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_2,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PCLK_3, "gout_peric0_pclk_3",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_3,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PCLK_4, "gout_peric0_pclk_4",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_4,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PCLK_5, "gout_peric0_pclk_5",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_5,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PCLK_6, "gout_peric0_pclk_6",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_6,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PCLK_7, "gout_peric0_pclk_7",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_7,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PCLK_8, "gout_peric0_pclk_8",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_8,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PCLK_9, "gout_peric0_pclk_9",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_9,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PCLK_10, "gout_peric0_pclk_10",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_10,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PCLK_11, "gout_peric0_pclk_11",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_11,
	     21, 0, 0),
};

static const struct samsung_cmu_info peric0_cmu_info __initconst = {
	.mux_clks		= peric0_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(peric0_mux_clks),
	.div_clks		= peric0_div_clks,
	.nr_div_clks		= ARRAY_SIZE(peric0_div_clks),
	.gate_clks		= peric0_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(peric0_gate_clks),
	.nr_clk_ids		= CLKS_NR_PERIC0,
	.clk_regs		= peric0_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(peric0_clk_regs),
	.clk_name		= "dout_clkcmu_peric0_bus",
};

/* ---- CMU_PERIC1 --------------------------------------------------------- */

/* Register Offset definitions for CMU_PERIC1 (0x10800000) */
#define PLL_CON0_MUX_CLKCMU_PERIC1_BUS_USER	0x0600
#define PLL_CON0_MUX_CLKCMU_PERIC1_IP_USER	0x0610
#define CLK_CON_MUX_MUX_CLK_PERIC1_USI06_USI	0x1000
#define CLK_CON_MUX_MUX_CLK_PERIC1_USI07_USI	0x1004
#define CLK_CON_MUX_MUX_CLK_PERIC1_USI08_USI	0x1008
#define CLK_CON_MUX_MUX_CLK_PERIC1_USI09_USI	0x100c
#define CLK_CON_MUX_MUX_CLK_PERIC1_USI10_USI	0x1010
#define CLK_CON_MUX_MUX_CLK_PERIC1_USI11_USI	0x1014
#define CLK_CON_MUX_MUX_CLK_PERIC1_USI_I2C	0x1018
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI06_USI	0x1800
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI07_USI	0x1804
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI08_USI	0x1808
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI09_USI	0x180c
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI10_USI	0x1810
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI11_USI	0x1814
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI_I2C	0x1818
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_0	0x2014
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_1	0x2018
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_2	0x2024
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_3	0x2028
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_4	0x202c
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_5	0x2030
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_6	0x2034
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_7	0x2038
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_8	0x203c
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_9	0x2040
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_10	0x201c
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_11	0x2020
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_0	0x2044
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_1	0x2048
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_2	0x2054
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_3	0x2058
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_4	0x205c
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_5	0x2060
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_6	0x2064
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_7	0x2068
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_8	0x206c
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_9	0x2070
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_10	0x204c
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_11	0x2050

static const unsigned long peric1_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_PERIC1_BUS_USER,
	PLL_CON0_MUX_CLKCMU_PERIC1_IP_USER,
	CLK_CON_MUX_MUX_CLK_PERIC1_USI06_USI,
	CLK_CON_MUX_MUX_CLK_PERIC1_USI07_USI,
	CLK_CON_MUX_MUX_CLK_PERIC1_USI08_USI,
	CLK_CON_MUX_MUX_CLK_PERIC1_USI09_USI,
	CLK_CON_MUX_MUX_CLK_PERIC1_USI10_USI,
	CLK_CON_MUX_MUX_CLK_PERIC1_USI11_USI,
	CLK_CON_MUX_MUX_CLK_PERIC1_USI_I2C,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI06_USI,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI07_USI,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI08_USI,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI09_USI,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI10_USI,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI11_USI,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI_I2C,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_0,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_1,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_2,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_3,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_4,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_5,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_6,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_7,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_8,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_9,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_10,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_11,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_0,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_1,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_2,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_3,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_4,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_5,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_6,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_7,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_8,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_9,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_10,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_11,
};

/* List of parent clocks for Muxes in CMU_PERIC1 */
PNAME(mout_peric1_bus_user_p) = { "oscclk", "dout_clkcmu_peric1_bus" };
PNAME(mout_peric1_ip_user_p) = { "oscclk", "dout_clkcmu_peric1_ip" };
PNAME(mout_peric1_usi_p) = { "oscclk", "mout_peric1_ip_user" };

static const struct samsung_mux_clock peric1_mux_clks[] __initconst = {
	MUX(CLK_MOUT_PERIC1_BUS_USER, "mout_peric1_bus_user",
	    mout_peric1_bus_user_p, PLL_CON0_MUX_CLKCMU_PERIC1_BUS_USER, 4, 1),
	MUX(CLK_MOUT_PERIC1_IP_USER, "mout_peric1_ip_user",
	    mout_peric1_ip_user_p, PLL_CON0_MUX_CLKCMU_PERIC1_IP_USER, 4, 1),
	/* USI06 ~ USI11 */
	MUX(CLK_MOUT_PERIC1_USI06_USI, "mout_peric1_usi06_usi",
	    mout_peric1_usi_p, CLK_CON_MUX_MUX_CLK_PERIC1_USI06_USI, 0, 1),
	MUX(CLK_MOUT_PERIC1_USI07_USI, "mout_peric1_usi07_usi",
	    mout_peric1_usi_p, CLK_CON_MUX_MUX_CLK_PERIC1_USI07_USI, 0, 1),
	MUX(CLK_MOUT_PERIC1_USI08_USI, "mout_peric1_usi08_usi",
	    mout_peric1_usi_p, CLK_CON_MUX_MUX_CLK_PERIC1_USI08_USI, 0, 1),
	MUX(CLK_MOUT_PERIC1_USI09_USI, "mout_peric1_usi09_usi",
	    mout_peric1_usi_p, CLK_CON_MUX_MUX_CLK_PERIC1_USI09_USI, 0, 1),
	MUX(CLK_MOUT_PERIC1_USI10_USI, "mout_peric1_usi10_usi",
	    mout_peric1_usi_p, CLK_CON_MUX_MUX_CLK_PERIC1_USI10_USI, 0, 1),
	MUX(CLK_MOUT_PERIC1_USI11_USI, "mout_peric1_usi11_usi",
	    mout_peric1_usi_p, CLK_CON_MUX_MUX_CLK_PERIC1_USI11_USI, 0, 1),
	/* USI_I2C */
	MUX(CLK_MOUT_PERIC1_USI_I2C, "mout_peric1_usi_i2c",
	    mout_peric1_usi_p, CLK_CON_MUX_MUX_CLK_PERIC1_USI_I2C, 0, 1),
};

static const struct samsung_div_clock peric1_div_clks[] __initconst = {
	/* USI06 ~ USI11 */
	DIV(CLK_DOUT_PERIC1_USI06_USI, "dout_peric1_usi06_usi",
	    "mout_peric1_usi06_usi", CLK_CON_DIV_DIV_CLK_PERIC1_USI06_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC1_USI07_USI, "dout_peric1_usi07_usi",
	    "mout_peric1_usi07_usi", CLK_CON_DIV_DIV_CLK_PERIC1_USI07_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC1_USI08_USI, "dout_peric1_usi08_usi",
	    "mout_peric1_usi08_usi", CLK_CON_DIV_DIV_CLK_PERIC1_USI08_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC1_USI09_USI, "dout_peric1_usi09_usi",
	    "mout_peric1_usi09_usi", CLK_CON_DIV_DIV_CLK_PERIC1_USI09_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC1_USI10_USI, "dout_peric1_usi10_usi",
	    "mout_peric1_usi10_usi", CLK_CON_DIV_DIV_CLK_PERIC1_USI10_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC1_USI11_USI, "dout_peric1_usi11_usi",
	    "mout_peric1_usi11_usi", CLK_CON_DIV_DIV_CLK_PERIC1_USI11_USI,
	    0, 4),
	/* USI_I2C */
	DIV(CLK_DOUT_PERIC1_USI_I2C, "dout_peric1_usi_i2c",
	    "mout_peric1_usi_i2c", CLK_CON_DIV_DIV_CLK_PERIC1_USI_I2C, 0, 4),
};

static const struct samsung_gate_clock peric1_gate_clks[] __initconst = {
	/* IPCLK */
	GATE(CLK_GOUT_PERIC1_IPCLK_0, "gout_peric1_ipclk_0",
	     "dout_peric1_usi06_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_0,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_IPCLK_1, "gout_peric1_ipclk_1",
	     "dout_peric1_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_1,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_IPCLK_2, "gout_peric1_ipclk_2",
	     "dout_peric1_usi07_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_2,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_IPCLK_3, "gout_peric1_ipclk_3",
	     "dout_peric1_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_3,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_IPCLK_4, "gout_peric1_ipclk_4",
	     "dout_peric1_usi08_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_4,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_IPCLK_5, "gout_peric1_ipclk_5",
	     "dout_peric1_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_5,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_IPCLK_6, "gout_peric1_ipclk_6",
	     "dout_peric1_usi09_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_6,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_IPCLK_7, "gout_peric1_ipclk_7",
	     "dout_peric1_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_7,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_IPCLK_8, "gout_peric1_ipclk_8",
	     "dout_peric1_usi10_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_8,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_IPCLK_9, "gout_peric1_ipclk_9",
	     "dout_peric1_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_9,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_IPCLK_10, "gout_peric1_ipclk_10",
	     "dout_peric1_usi11_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_10,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_IPCLK_11, "gout_peric1_ipclk_11",
	     "dout_peric1_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_11,
	     21, 0, 0),

	/* PCLK */
	GATE(CLK_GOUT_PERIC1_PCLK_0, "gout_peric1_pclk_0",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_0,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_PCLK_1, "gout_peric1_pclk_1",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_1,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_PCLK_2, "gout_peric1_pclk_2",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_2,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_PCLK_3, "gout_peric1_pclk_3",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_3,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_PCLK_4, "gout_peric1_pclk_4",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_4,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_PCLK_5, "gout_peric1_pclk_5",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_5,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_PCLK_6, "gout_peric1_pclk_6",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_6,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_PCLK_7, "gout_peric1_pclk_7",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_7,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_PCLK_8, "gout_peric1_pclk_8",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_8,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_PCLK_9, "gout_peric1_pclk_9",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_9,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_PCLK_10, "gout_peric1_pclk_10",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_10,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_PCLK_11, "gout_peric1_pclk_11",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_11,
	     21, 0, 0),
};

static const struct samsung_cmu_info peric1_cmu_info __initconst = {
	.mux_clks		= peric1_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(peric1_mux_clks),
	.div_clks		= peric1_div_clks,
	.nr_div_clks		= ARRAY_SIZE(peric1_div_clks),
	.gate_clks		= peric1_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(peric1_gate_clks),
	.nr_clk_ids		= CLKS_NR_PERIC1,
	.clk_regs		= peric1_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(peric1_clk_regs),
	.clk_name		= "dout_clkcmu_peric1_bus",
};

/* ---- CMU_PERIS ---------------------------------------------------------- */

/* Register Offset definitions for CMU_PERIS (0x10020000) */
#define PLL_CON0_MUX_CLKCMU_PERIS_BUS_USER	0x0600
#define CLK_CON_GAT_GOUT_BLK_PERIS_UID_SYSREG_PERIS_IPCLKPORT_PCLK	0x2058
#define CLK_CON_GAT_GOUT_BLK_PERIS_UID_WDT_CLUSTER0_IPCLKPORT_PCLK	0x205c
#define CLK_CON_GAT_GOUT_BLK_PERIS_UID_WDT_CLUSTER1_IPCLKPORT_PCLK	0x2060

static const unsigned long peris_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_PERIS_BUS_USER,
	CLK_CON_GAT_GOUT_BLK_PERIS_UID_SYSREG_PERIS_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_PERIS_UID_WDT_CLUSTER0_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_PERIS_UID_WDT_CLUSTER1_IPCLKPORT_PCLK,
};

/* List of parent clocks for Muxes in CMU_PERIS */
PNAME(mout_peris_bus_user_p) = { "oscclk", "dout_clkcmu_peris_bus" };

static const struct samsung_mux_clock peris_mux_clks[] __initconst = {
	MUX(CLK_MOUT_PERIS_BUS_USER, "mout_peris_bus_user",
	    mout_peris_bus_user_p, PLL_CON0_MUX_CLKCMU_PERIS_BUS_USER, 4, 1),
};

static const struct samsung_gate_clock peris_gate_clks[] __initconst = {
	GATE(CLK_GOUT_SYSREG_PERIS_PCLK, "gout_sysreg_peris_pclk",
	     "mout_peris_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIS_UID_SYSREG_PERIS_IPCLKPORT_PCLK,
	     21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_WDT_CLUSTER0, "gout_wdt_cluster0", "mout_peris_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIS_UID_WDT_CLUSTER0_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_WDT_CLUSTER1, "gout_wdt_cluster1", "mout_peris_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIS_UID_WDT_CLUSTER1_IPCLKPORT_PCLK,
	     21, 0, 0),
};

static const struct samsung_cmu_info peris_cmu_info __initconst = {
	.mux_clks		= peris_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(peris_mux_clks),
	.gate_clks		= peris_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(peris_gate_clks),
	.nr_clk_ids		= CLKS_NR_PERIS,
	.clk_regs		= peris_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(peris_clk_regs),
	.clk_name		= "dout_clkcmu_peris_bus",
};

static int __init exynosautov9_cmu_probe(struct platform_device *pdev)
{
	const struct samsung_cmu_info *info;
	struct device *dev = &pdev->dev;

	info = of_device_get_match_data(dev);
	exynos_arm64_register_cmu(dev, dev->of_node, info);

	return 0;
}

static const struct of_device_id exynosautov9_cmu_of_match[] = {
	{
		.compatible = "samsung,exynosautov9-cmu-busmc",
		.data = &busmc_cmu_info,
	}, {
		.compatible = "samsung,exynosautov9-cmu-core",
		.data = &core_cmu_info,
	}, {
		.compatible = "samsung,exynosautov9-cmu-fsys0",
		.data = &fsys0_cmu_info,
	}, {
		.compatible = "samsung,exynosautov9-cmu-fsys1",
		.data = &fsys1_cmu_info,
	}, {
		.compatible = "samsung,exynosautov9-cmu-fsys2",
		.data = &fsys2_cmu_info,
	}, {
		.compatible = "samsung,exynosautov9-cmu-peric0",
		.data = &peric0_cmu_info,
	}, {
		.compatible = "samsung,exynosautov9-cmu-peric1",
		.data = &peric1_cmu_info,
	}, {
		.compatible = "samsung,exynosautov9-cmu-peris",
		.data = &peris_cmu_info,
	}, {
	},
};

static struct platform_driver exynosautov9_cmu_driver __refdata = {
	.driver = {
		.name = "exynosautov9-cmu",
		.of_match_table = exynosautov9_cmu_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = exynosautov9_cmu_probe,
};

static int __init exynosautov9_cmu_init(void)
{
	return platform_driver_register(&exynosautov9_cmu_driver);
}
core_initcall(exynosautov9_cmu_init);
