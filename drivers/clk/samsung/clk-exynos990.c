// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Igor Belwon <igor.belwon@mentallysanemainliners.org>
 *
 * Common Clock Framework support for Exynos990.
 */

#include <linux/clk-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <dt-bindings/clock/samsung,exynos990.h>

#include "clk.h"
#include "clk-exynos-arm64.h"
#include "clk-pll.h"

/* NOTE: Must be equal to the last clock ID increased by one */
#define CLKS_NR_TOP (CLK_DOUT_CMU_CLK_CMUREF + 1)
#define CLKS_NR_HSI0 (CLK_GOUT_HSI0_LHS_ACEL_D_HSI0_CLK + 1)
#define CLKS_NR_PERIC0 (CLK_GOUT_PERIC0_SYSREG_PCLK + 1)
#define CLKS_NR_PERIC1 (CLK_GOUT_PERIC1_XIU_P_ACLK + 1)
#define CLKS_NR_PERIS (CLK_GOUT_PERIS_OTP_CON_TOP_OSCCLK + 1)

/* ---- CMU_TOP ------------------------------------------------------------- */

/* Register Offset definitions for CMU_TOP (0x1a330000) */
#define PLL_LOCKTIME_PLL_G3D				0x0000
#define PLL_LOCKTIME_PLL_MMC				0x0004
#define PLL_LOCKTIME_PLL_SHARED0			0x0008
#define PLL_LOCKTIME_PLL_SHARED1			0x000c
#define PLL_LOCKTIME_PLL_SHARED2			0x0010
#define PLL_LOCKTIME_PLL_SHARED3			0x0014
#define PLL_LOCKTIME_PLL_SHARED4			0x0018
#define PLL_CON0_PLL_G3D				0x0100
#define PLL_CON3_PLL_G3D				0x010c
#define PLL_CON0_PLL_MMC				0x0140
#define PLL_CON3_PLL_MMC				0x014c
#define PLL_CON0_PLL_SHARED0				0x0180
#define PLL_CON3_PLL_SHARED0				0x018c
#define PLL_CON0_PLL_SHARED1				0x01c0
#define PLL_CON3_PLL_SHARED1				0x01cc
#define PLL_CON0_PLL_SHARED2				0x0200
#define PLL_CON3_PLL_SHARED2				0x020c
#define PLL_CON0_PLL_SHARED3				0x0240
#define PLL_CON3_PLL_SHARED3				0x024c
#define PLL_CON0_PLL_SHARED4				0x0280
#define PLL_CON3_PLL_SHARED4				0x028c
#define CLK_CON_MUX_CLKCMU_DPU_BUS			0x1000
#define CLK_CON_MUX_MUX_CLKCMU_APM_BUS			0x1004
#define CLK_CON_MUX_MUX_CLKCMU_AUD_CPU			0x1008
#define CLK_CON_MUX_MUX_CLKCMU_BUS0_BUS			0x100c
#define CLK_CON_MUX_MUX_CLKCMU_BUS1_BUS			0x1010
#define CLK_CON_MUX_MUX_CLKCMU_BUS1_SSS			0x1014
#define CLK_CON_MUX_MUX_CLKCMU_CIS_CLK0			0x1018
#define CLK_CON_MUX_MUX_CLKCMU_CIS_CLK1			0x101c
#define CLK_CON_MUX_MUX_CLKCMU_CIS_CLK2			0x1020
#define CLK_CON_MUX_MUX_CLKCMU_CIS_CLK3			0x1024
#define CLK_CON_MUX_MUX_CLKCMU_CIS_CLK4			0x1028
#define CLK_CON_MUX_MUX_CLKCMU_CIS_CLK5			0x102c
#define CLK_CON_MUX_MUX_CLKCMU_CMU_BOOST		0x1030
#define CLK_CON_MUX_MUX_CLKCMU_CORE_BUS			0x1034
#define CLK_CON_MUX_MUX_CLKCMU_CPUCL0_DBG_BUS		0x1038
#define CLK_CON_MUX_MUX_CLKCMU_CPUCL0_SWITCH		0x103c
#define CLK_CON_MUX_MUX_CLKCMU_CPUCL1_SWITCH		0x1040
#define CLK_CON_MUX_MUX_CLKCMU_CPUCL2_BUSP		0x1044
#define CLK_CON_MUX_MUX_CLKCMU_CPUCL2_SWITCH		0x1048
#define CLK_CON_MUX_MUX_CLKCMU_CSIS_BUS			0x104c
#define CLK_CON_MUX_MUX_CLKCMU_CSIS_OIS_MCU		0x1050
#define CLK_CON_MUX_MUX_CLKCMU_DNC_BUS			0x1054
#define CLK_CON_MUX_MUX_CLKCMU_DNC_BUSM			0x1058
#define CLK_CON_MUX_MUX_CLKCMU_DNS_BUS			0x105c
#define CLK_CON_MUX_MUX_CLKCMU_DPU			0x1060
#define CLK_CON_MUX_MUX_CLKCMU_DPU_ALT			0x1064
#define CLK_CON_MUX_MUX_CLKCMU_DSP_BUS			0x1068
#define CLK_CON_MUX_MUX_CLKCMU_G2D_G2D			0x106c
#define CLK_CON_MUX_MUX_CLKCMU_G2D_MSCL			0x1070
#define CLK_CON_MUX_MUX_CLKCMU_HPM			0x1074
#define CLK_CON_MUX_MUX_CLKCMU_HSI0_BUS			0x1078
#define CLK_CON_MUX_MUX_CLKCMU_HSI0_DPGTC		0x107c
#define CLK_CON_MUX_MUX_CLKCMU_HSI0_USB31DRD		0x1080
#define CLK_CON_MUX_MUX_CLKCMU_HSI0_USBDP_DEBUG		0x1084
#define CLK_CON_MUX_MUX_CLKCMU_HSI1_BUS			0x1088
#define CLK_CON_MUX_MUX_CLKCMU_HSI1_MMC_CARD		0x108c
#define CLK_CON_MUX_MUX_CLKCMU_HSI1_PCIE		0x1090
#define CLK_CON_MUX_MUX_CLKCMU_HSI1_UFS_CARD		0x1094
#define CLK_CON_MUX_MUX_CLKCMU_HSI1_UFS_EMBD		0x1098
#define CLK_CON_MUX_MUX_CLKCMU_HSI2_BUS			0x109c
#define CLK_CON_MUX_MUX_CLKCMU_HSI2_PCIE		0x10a0
#define CLK_CON_MUX_MUX_CLKCMU_IPP_BUS			0x10a4
#define CLK_CON_MUX_MUX_CLKCMU_ITP_BUS			0x10a8
#define CLK_CON_MUX_MUX_CLKCMU_MCSC_BUS			0x10ac
#define CLK_CON_MUX_MUX_CLKCMU_MCSC_GDC			0x10b0
#define CLK_CON_MUX_MUX_CLKCMU_CMU_BOOST_CPU		0x10b4
#define CLK_CON_MUX_MUX_CLKCMU_MFC0_MFC0		0x10b8
#define CLK_CON_MUX_MUX_CLKCMU_MFC0_WFD			0x10bc
#define CLK_CON_MUX_MUX_CLKCMU_MIF_BUSP			0x10c0
#define CLK_CON_MUX_MUX_CLKCMU_MIF_SWITCH		0x10c4
#define CLK_CON_MUX_MUX_CLKCMU_NPU_BUS			0x10c8
#define CLK_CON_MUX_MUX_CLKCMU_PERIC0_BUS		0x10cc
#define CLK_CON_MUX_MUX_CLKCMU_PERIC0_IP		0x10d0
#define CLK_CON_MUX_MUX_CLKCMU_PERIC1_BUS		0x10d4
#define CLK_CON_MUX_MUX_CLKCMU_PERIC1_IP		0x10d8
#define CLK_CON_MUX_MUX_CLKCMU_PERIS_BUS		0x10dc
#define CLK_CON_MUX_MUX_CLKCMU_SSP_BUS			0x10e0
#define CLK_CON_MUX_MUX_CLKCMU_TNR_BUS			0x10e4
#define CLK_CON_MUX_MUX_CLKCMU_VRA_BUS			0x10e8
#define CLK_CON_MUX_MUX_CLK_CMU_CMUREF			0x10f0
#define CLK_CON_MUX_MUX_CMU_CMUREF			0x10f4
#define CLK_CON_DIV_CLKCMU_APM_BUS			0x1800
#define CLK_CON_DIV_CLKCMU_AUD_CPU			0x1804
#define CLK_CON_DIV_CLKCMU_BUS0_BUS			0x1808
#define CLK_CON_DIV_CLKCMU_BUS1_BUS			0x180c
#define CLK_CON_DIV_CLKCMU_BUS1_SSS			0x1810
#define CLK_CON_DIV_CLKCMU_CIS_CLK0			0x1814
#define CLK_CON_DIV_CLKCMU_CIS_CLK1			0x1818
#define CLK_CON_DIV_CLKCMU_CIS_CLK2			0x181c
#define CLK_CON_DIV_CLKCMU_CIS_CLK3			0x1820
#define CLK_CON_DIV_CLKCMU_CIS_CLK4			0x1824
#define CLK_CON_DIV_CLKCMU_CIS_CLK5			0x1828
#define CLK_CON_DIV_CLKCMU_CMU_BOOST			0x182c
#define CLK_CON_DIV_CLKCMU_CORE_BUS			0x1830
#define CLK_CON_DIV_CLKCMU_CPUCL0_DBG_BUS		0x1834
#define CLK_CON_DIV_CLKCMU_CPUCL0_SWITCH		0x1838
#define CLK_CON_DIV_CLKCMU_CPUCL1_SWITCH		0x183c
#define CLK_CON_DIV_CLKCMU_CPUCL2_BUSP			0x1840
#define CLK_CON_DIV_CLKCMU_CPUCL2_SWITCH		0x1844
#define CLK_CON_DIV_CLKCMU_CSIS_BUS			0x1848
#define CLK_CON_DIV_CLKCMU_CSIS_OIS_MCU			0x184c
#define CLK_CON_DIV_CLKCMU_DNC_BUS			0x1850
#define CLK_CON_DIV_CLKCMU_DNC_BUSM			0x1854
#define CLK_CON_DIV_CLKCMU_DNS_BUS			0x1858
#define CLK_CON_DIV_CLKCMU_DSP_BUS			0x185c
#define CLK_CON_DIV_CLKCMU_G2D_G2D			0x1860
#define CLK_CON_DIV_CLKCMU_G2D_MSCL			0x1864
#define CLK_CON_DIV_CLKCMU_G3D_SWITCH			0x1868
#define CLK_CON_DIV_CLKCMU_HPM				0x186c
#define CLK_CON_DIV_CLKCMU_HSI0_BUS			0x1870
#define CLK_CON_DIV_CLKCMU_HSI0_DPGTC			0x1874
#define CLK_CON_DIV_CLKCMU_HSI0_USB31DRD		0x1878
#define CLK_CON_DIV_CLKCMU_HSI0_USBDP_DEBUG		0x187c
#define CLK_CON_DIV_CLKCMU_HSI1_BUS			0x1880
#define CLK_CON_DIV_CLKCMU_HSI1_MMC_CARD		0x1884
#define CLK_CON_DIV_CLKCMU_HSI1_PCIE			0x1888
#define CLK_CON_DIV_CLKCMU_HSI1_UFS_CARD		0x188c
#define CLK_CON_DIV_CLKCMU_HSI1_UFS_EMBD		0x1890
#define CLK_CON_DIV_CLKCMU_HSI2_BUS			0x1894
#define CLK_CON_DIV_CLKCMU_HSI2_PCIE			0x1898
#define CLK_CON_DIV_CLKCMU_IPP_BUS			0x189c
#define CLK_CON_DIV_CLKCMU_ITP_BUS			0x18a0
#define CLK_CON_DIV_CLKCMU_MCSC_BUS			0x18a4
#define CLK_CON_DIV_CLKCMU_MCSC_GDC			0x18a8
#define CLK_CON_DIV_CLKCMU_CMU_BOOST_CPU		0x18ac
#define CLK_CON_DIV_CLKCMU_MFC0_MFC0			0x18b0
#define CLK_CON_DIV_CLKCMU_MFC0_WFD			0x18b4
#define CLK_CON_DIV_CLKCMU_MIF_BUSP			0x18b8
#define CLK_CON_DIV_CLKCMU_NPU_BUS			0x18bc
#define CLK_CON_DIV_CLKCMU_OTP				0x18c0
#define CLK_CON_DIV_CLKCMU_PERIC0_BUS			0x18c4
#define CLK_CON_DIV_CLKCMU_PERIC0_IP			0x18c8
#define CLK_CON_DIV_CLKCMU_PERIC1_BUS			0x18cc
#define CLK_CON_DIV_CLKCMU_PERIC1_IP			0x18d0
#define CLK_CON_DIV_CLKCMU_PERIS_BUS			0x18d4
#define CLK_CON_DIV_CLKCMU_SSP_BUS			0x18d8
#define CLK_CON_DIV_CLKCMU_TNR_BUS			0x18dc
#define CLK_CON_DIV_CLKCMU_VRA_BUS			0x18e0
#define CLK_CON_DIV_DIV_CLKCMU_DPU			0x18e8
#define CLK_CON_DIV_DIV_CLKCMU_DPU_ALT			0x18ec
#define CLK_CON_DIV_DIV_CLK_CMU_CMUREF			0x18f0
#define CLK_CON_DIV_PLL_SHARED0_DIV2			0x18f4
#define CLK_CON_DIV_PLL_SHARED0_DIV3			0x18f8
#define CLK_CON_DIV_PLL_SHARED0_DIV4			0x18fc
#define CLK_CON_DIV_PLL_SHARED1_DIV2			0x1900
#define CLK_CON_DIV_PLL_SHARED1_DIV3			0x1904
#define CLK_CON_DIV_PLL_SHARED1_DIV4			0x1908
#define CLK_CON_DIV_PLL_SHARED2_DIV2			0x190c
#define CLK_CON_DIV_PLL_SHARED4_DIV2			0x1910
#define CLK_CON_DIV_PLL_SHARED4_DIV3			0x1914
#define CLK_CON_DIV_PLL_SHARED4_DIV4			0x1918
#define CLK_CON_GAT_CLKCMU_G3D_BUS			0x2000
#define CLK_CON_GAT_CLKCMU_MIF_SWITCH			0x2004
#define CLK_CON_GAT_GATE_CLKCMU_APM_BUS			0x2008
#define CLK_CON_GAT_GATE_CLKCMU_AUD_CPU			0x200c
#define CLK_CON_GAT_GATE_CLKCMU_BUS0_BUS		0x2010
#define CLK_CON_GAT_GATE_CLKCMU_BUS1_BUS		0x2014
#define CLK_CON_GAT_GATE_CLKCMU_BUS1_SSS		0x2018
#define CLK_CON_GAT_GATE_CLKCMU_CIS_CLK0		0x201c
#define CLK_CON_GAT_GATE_CLKCMU_CIS_CLK1		0x2020
#define CLK_CON_GAT_GATE_CLKCMU_CIS_CLK2		0x2024
#define CLK_CON_GAT_GATE_CLKCMU_CIS_CLK3		0x2028
#define CLK_CON_GAT_GATE_CLKCMU_CIS_CLK4		0x202c
#define CLK_CON_GAT_GATE_CLKCMU_CIS_CLK5		0x2030
#define CLK_CON_GAT_GATE_CLKCMU_CORE_BUS		0x2034
#define CLK_CON_GAT_GATE_CLKCMU_CPUCL0_DBG_BUS		0x2038
#define CLK_CON_GAT_GATE_CLKCMU_CPUCL0_SWITCH		0x203c
#define CLK_CON_GAT_GATE_CLKCMU_CPUCL1_SWITCH		0x2040
#define CLK_CON_GAT_GATE_CLKCMU_CPUCL2_BUSP		0x2044
#define CLK_CON_GAT_GATE_CLKCMU_CPUCL2_SWITCH		0x2048
#define CLK_CON_GAT_GATE_CLKCMU_CSIS_BUS		0x204c
#define CLK_CON_GAT_GATE_CLKCMU_CSIS_OIS_MCU		0x2050
#define CLK_CON_GAT_GATE_CLKCMU_DNC_BUS			0x2054
#define CLK_CON_GAT_GATE_CLKCMU_DNC_BUSM		0x2058
#define CLK_CON_GAT_GATE_CLKCMU_DNS_BUS			0x205c
#define CLK_CON_GAT_GATE_CLKCMU_DPU			0x2060
#define CLK_CON_GAT_GATE_CLKCMU_DPU_BUS			0x2064
#define CLK_CON_GAT_GATE_CLKCMU_DSP_BUS			0x2068
#define CLK_CON_GAT_GATE_CLKCMU_G2D_G2D			0x206c
#define CLK_CON_GAT_GATE_CLKCMU_G2D_MSCL		0x2070
#define CLK_CON_GAT_GATE_CLKCMU_G3D_SWITCH		0x2074
#define CLK_CON_GAT_GATE_CLKCMU_HPM			0x2078
#define CLK_CON_GAT_GATE_CLKCMU_HSI0_BUS		0x207c
#define CLK_CON_GAT_GATE_CLKCMU_HSI0_DPGTC		0x2080
#define CLK_CON_GAT_GATE_CLKCMU_HSI0_USB31DRD		0x2084
#define CLK_CON_GAT_GATE_CLKCMU_HSI0_USBDP_DEBUG	0x2088
#define CLK_CON_GAT_GATE_CLKCMU_HSI1_BUS		0x208c
#define CLK_CON_GAT_GATE_CLKCMU_HSI1_MMC_CARD		0x2090
#define CLK_CON_GAT_GATE_CLKCMU_HSI1_PCIE		0x2094
#define CLK_CON_GAT_GATE_CLKCMU_HSI1_UFS_CARD		0x2098
#define CLK_CON_GAT_GATE_CLKCMU_HSI1_UFS_EMBD		0x209c
#define CLK_CON_GAT_GATE_CLKCMU_HSI2_BUS		0x20a0
#define CLK_CON_GAT_GATE_CLKCMU_HSI2_PCIE		0x20a4
#define CLK_CON_GAT_GATE_CLKCMU_IPP_BUS			0x20a8
#define CLK_CON_GAT_GATE_CLKCMU_ITP_BUS			0x20ac
#define CLK_CON_GAT_GATE_CLKCMU_MCSC_BUS		0x20b0
#define CLK_CON_GAT_GATE_CLKCMU_MCSC_GDC		0x20b4
#define CLK_CON_GAT_GATE_CLKCMU_MFC0_MFC0		0x20bc
#define CLK_CON_GAT_GATE_CLKCMU_MFC0_WFD		0x20c0
#define CLK_CON_GAT_GATE_CLKCMU_MIF_BUSP		0x20c4
#define CLK_CON_GAT_GATE_CLKCMU_NPU_BUS			0x20c8
#define CLK_CON_GAT_GATE_CLKCMU_PERIC0_BUS		0x20cc
#define CLK_CON_GAT_GATE_CLKCMU_PERIC0_IP		0x20d0
#define CLK_CON_GAT_GATE_CLKCMU_PERIC1_BUS		0x20d4
#define CLK_CON_GAT_GATE_CLKCMU_PERIC1_IP		0x20d8
#define CLK_CON_GAT_GATE_CLKCMU_PERIS_BUS		0x20dc
#define CLK_CON_GAT_GATE_CLKCMU_SSP_BUS			0x20e0
#define CLK_CON_GAT_GATE_CLKCMU_TNR_BUS			0x20e4
#define CLK_CON_GAT_GATE_CLKCMU_VRA_BUS			0x20e8

static const unsigned long top_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_G3D,
	PLL_LOCKTIME_PLL_MMC,
	PLL_LOCKTIME_PLL_SHARED0,
	PLL_LOCKTIME_PLL_SHARED1,
	PLL_LOCKTIME_PLL_SHARED2,
	PLL_LOCKTIME_PLL_SHARED3,
	PLL_LOCKTIME_PLL_SHARED4,
	PLL_CON0_PLL_G3D,
	PLL_CON3_PLL_G3D,
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
	CLK_CON_MUX_CLKCMU_DPU_BUS,
	CLK_CON_MUX_MUX_CLKCMU_APM_BUS,
	CLK_CON_MUX_MUX_CLKCMU_AUD_CPU,
	CLK_CON_MUX_MUX_CLKCMU_BUS0_BUS,
	CLK_CON_MUX_MUX_CLKCMU_BUS1_BUS,
	CLK_CON_MUX_MUX_CLKCMU_BUS1_SSS,
	CLK_CON_MUX_MUX_CLKCMU_CIS_CLK0,
	CLK_CON_MUX_MUX_CLKCMU_CIS_CLK1,
	CLK_CON_MUX_MUX_CLKCMU_CIS_CLK2,
	CLK_CON_MUX_MUX_CLKCMU_CIS_CLK3,
	CLK_CON_MUX_MUX_CLKCMU_CIS_CLK4,
	CLK_CON_MUX_MUX_CLKCMU_CIS_CLK5,
	CLK_CON_MUX_MUX_CLKCMU_CMU_BOOST,
	CLK_CON_MUX_MUX_CLKCMU_CORE_BUS,
	CLK_CON_MUX_MUX_CLKCMU_CPUCL0_DBG_BUS,
	CLK_CON_MUX_MUX_CLKCMU_CPUCL0_SWITCH,
	CLK_CON_MUX_MUX_CLKCMU_CPUCL1_SWITCH,
	CLK_CON_MUX_MUX_CLKCMU_CPUCL2_BUSP,
	CLK_CON_MUX_MUX_CLKCMU_CPUCL2_SWITCH,
	CLK_CON_MUX_MUX_CLKCMU_CSIS_BUS,
	CLK_CON_MUX_MUX_CLKCMU_CSIS_OIS_MCU,
	CLK_CON_MUX_MUX_CLKCMU_DNC_BUS,
	CLK_CON_MUX_MUX_CLKCMU_DNC_BUSM,
	CLK_CON_MUX_MUX_CLKCMU_DNS_BUS,
	CLK_CON_MUX_MUX_CLKCMU_DPU,
	CLK_CON_MUX_MUX_CLKCMU_DPU_ALT,
	CLK_CON_MUX_MUX_CLKCMU_DSP_BUS,
	CLK_CON_MUX_MUX_CLKCMU_G2D_G2D,
	CLK_CON_MUX_MUX_CLKCMU_G2D_MSCL,
	CLK_CON_MUX_MUX_CLKCMU_HPM,
	CLK_CON_MUX_MUX_CLKCMU_HSI0_BUS,
	CLK_CON_MUX_MUX_CLKCMU_HSI0_DPGTC,
	CLK_CON_MUX_MUX_CLKCMU_HSI0_USB31DRD,
	CLK_CON_MUX_MUX_CLKCMU_HSI0_USBDP_DEBUG,
	CLK_CON_MUX_MUX_CLKCMU_HSI1_BUS,
	CLK_CON_MUX_MUX_CLKCMU_HSI1_MMC_CARD,
	CLK_CON_MUX_MUX_CLKCMU_HSI1_PCIE,
	CLK_CON_MUX_MUX_CLKCMU_HSI1_UFS_CARD,
	CLK_CON_MUX_MUX_CLKCMU_HSI1_UFS_EMBD,
	CLK_CON_MUX_MUX_CLKCMU_HSI2_BUS,
	CLK_CON_MUX_MUX_CLKCMU_HSI2_PCIE,
	CLK_CON_MUX_MUX_CLKCMU_IPP_BUS,
	CLK_CON_MUX_MUX_CLKCMU_ITP_BUS,
	CLK_CON_MUX_MUX_CLKCMU_MCSC_BUS,
	CLK_CON_MUX_MUX_CLKCMU_MCSC_GDC,
	CLK_CON_MUX_MUX_CLKCMU_CMU_BOOST_CPU,
	CLK_CON_MUX_MUX_CLKCMU_MFC0_MFC0,
	CLK_CON_MUX_MUX_CLKCMU_MFC0_WFD,
	CLK_CON_MUX_MUX_CLKCMU_MIF_BUSP,
	CLK_CON_MUX_MUX_CLKCMU_MIF_SWITCH,
	CLK_CON_MUX_MUX_CLKCMU_NPU_BUS,
	CLK_CON_MUX_MUX_CLKCMU_PERIC0_BUS,
	CLK_CON_MUX_MUX_CLKCMU_PERIC0_IP,
	CLK_CON_MUX_MUX_CLKCMU_PERIC1_BUS,
	CLK_CON_MUX_MUX_CLKCMU_PERIC1_IP,
	CLK_CON_MUX_MUX_CLKCMU_PERIS_BUS,
	CLK_CON_MUX_MUX_CLKCMU_SSP_BUS,
	CLK_CON_MUX_MUX_CLKCMU_TNR_BUS,
	CLK_CON_MUX_MUX_CLKCMU_VRA_BUS,
	CLK_CON_MUX_MUX_CLK_CMU_CMUREF,
	CLK_CON_MUX_MUX_CMU_CMUREF,
	CLK_CON_DIV_CLKCMU_APM_BUS,
	CLK_CON_DIV_CLKCMU_AUD_CPU,
	CLK_CON_DIV_CLKCMU_BUS0_BUS,
	CLK_CON_DIV_CLKCMU_BUS1_BUS,
	CLK_CON_DIV_CLKCMU_BUS1_SSS,
	CLK_CON_DIV_CLKCMU_CIS_CLK0,
	CLK_CON_DIV_CLKCMU_CIS_CLK1,
	CLK_CON_DIV_CLKCMU_CIS_CLK2,
	CLK_CON_DIV_CLKCMU_CIS_CLK3,
	CLK_CON_DIV_CLKCMU_CIS_CLK4,
	CLK_CON_DIV_CLKCMU_CIS_CLK5,
	CLK_CON_DIV_CLKCMU_CMU_BOOST,
	CLK_CON_DIV_CLKCMU_CORE_BUS,
	CLK_CON_DIV_CLKCMU_CPUCL0_DBG_BUS,
	CLK_CON_DIV_CLKCMU_CPUCL0_SWITCH,
	CLK_CON_DIV_CLKCMU_CPUCL1_SWITCH,
	CLK_CON_DIV_CLKCMU_CPUCL2_BUSP,
	CLK_CON_DIV_CLKCMU_CPUCL2_SWITCH,
	CLK_CON_DIV_CLKCMU_CSIS_BUS,
	CLK_CON_DIV_CLKCMU_CSIS_OIS_MCU,
	CLK_CON_DIV_CLKCMU_DNC_BUS,
	CLK_CON_DIV_CLKCMU_DNC_BUSM,
	CLK_CON_DIV_CLKCMU_DNS_BUS,
	CLK_CON_DIV_CLKCMU_DSP_BUS,
	CLK_CON_DIV_CLKCMU_G2D_G2D,
	CLK_CON_DIV_CLKCMU_G2D_MSCL,
	CLK_CON_DIV_CLKCMU_G3D_SWITCH,
	CLK_CON_DIV_CLKCMU_HPM,
	CLK_CON_DIV_CLKCMU_HSI0_BUS,
	CLK_CON_DIV_CLKCMU_HSI0_DPGTC,
	CLK_CON_DIV_CLKCMU_HSI0_USB31DRD,
	CLK_CON_DIV_CLKCMU_HSI0_USBDP_DEBUG,
	CLK_CON_DIV_CLKCMU_HSI1_BUS,
	CLK_CON_DIV_CLKCMU_HSI1_MMC_CARD,
	CLK_CON_DIV_CLKCMU_HSI1_PCIE,
	CLK_CON_DIV_CLKCMU_HSI1_UFS_CARD,
	CLK_CON_DIV_CLKCMU_HSI1_UFS_EMBD,
	CLK_CON_DIV_CLKCMU_HSI2_BUS,
	CLK_CON_DIV_CLKCMU_HSI2_PCIE,
	CLK_CON_DIV_CLKCMU_IPP_BUS,
	CLK_CON_DIV_CLKCMU_ITP_BUS,
	CLK_CON_DIV_CLKCMU_MCSC_BUS,
	CLK_CON_DIV_CLKCMU_MCSC_GDC,
	CLK_CON_DIV_CLKCMU_CMU_BOOST_CPU,
	CLK_CON_DIV_CLKCMU_MFC0_MFC0,
	CLK_CON_DIV_CLKCMU_MFC0_WFD,
	CLK_CON_DIV_CLKCMU_MIF_BUSP,
	CLK_CON_DIV_CLKCMU_NPU_BUS,
	CLK_CON_DIV_CLKCMU_OTP,
	CLK_CON_DIV_CLKCMU_PERIC0_BUS,
	CLK_CON_DIV_CLKCMU_PERIC0_IP,
	CLK_CON_DIV_CLKCMU_PERIC1_BUS,
	CLK_CON_DIV_CLKCMU_PERIC1_IP,
	CLK_CON_DIV_CLKCMU_PERIS_BUS,
	CLK_CON_DIV_CLKCMU_SSP_BUS,
	CLK_CON_DIV_CLKCMU_TNR_BUS,
	CLK_CON_DIV_CLKCMU_VRA_BUS,
	CLK_CON_DIV_DIV_CLKCMU_DPU,
	CLK_CON_DIV_DIV_CLKCMU_DPU_ALT,
	CLK_CON_DIV_DIV_CLK_CMU_CMUREF,
	CLK_CON_DIV_PLL_SHARED0_DIV2,
	CLK_CON_DIV_PLL_SHARED0_DIV3,
	CLK_CON_DIV_PLL_SHARED0_DIV4,
	CLK_CON_DIV_PLL_SHARED1_DIV2,
	CLK_CON_DIV_PLL_SHARED1_DIV3,
	CLK_CON_DIV_PLL_SHARED1_DIV4,
	CLK_CON_DIV_PLL_SHARED2_DIV2,
	CLK_CON_DIV_PLL_SHARED4_DIV2,
	CLK_CON_DIV_PLL_SHARED4_DIV3,
	CLK_CON_DIV_PLL_SHARED4_DIV4,
	CLK_CON_GAT_CLKCMU_G3D_BUS,
	CLK_CON_GAT_CLKCMU_MIF_SWITCH,
	CLK_CON_GAT_GATE_CLKCMU_APM_BUS,
	CLK_CON_GAT_GATE_CLKCMU_AUD_CPU,
	CLK_CON_GAT_GATE_CLKCMU_BUS0_BUS,
	CLK_CON_GAT_GATE_CLKCMU_BUS1_BUS,
	CLK_CON_GAT_GATE_CLKCMU_BUS1_SSS,
	CLK_CON_GAT_GATE_CLKCMU_CIS_CLK0,
	CLK_CON_GAT_GATE_CLKCMU_CIS_CLK1,
	CLK_CON_GAT_GATE_CLKCMU_CIS_CLK2,
	CLK_CON_GAT_GATE_CLKCMU_CIS_CLK3,
	CLK_CON_GAT_GATE_CLKCMU_CIS_CLK4,
	CLK_CON_GAT_GATE_CLKCMU_CIS_CLK5,
	CLK_CON_GAT_GATE_CLKCMU_CORE_BUS,
	CLK_CON_GAT_GATE_CLKCMU_CPUCL0_DBG_BUS,
	CLK_CON_GAT_GATE_CLKCMU_CPUCL0_SWITCH,
	CLK_CON_GAT_GATE_CLKCMU_CPUCL1_SWITCH,
	CLK_CON_GAT_GATE_CLKCMU_CPUCL2_BUSP,
	CLK_CON_GAT_GATE_CLKCMU_CPUCL2_SWITCH,
	CLK_CON_GAT_GATE_CLKCMU_CSIS_BUS,
	CLK_CON_GAT_GATE_CLKCMU_CSIS_OIS_MCU,
	CLK_CON_GAT_GATE_CLKCMU_DNC_BUS,
	CLK_CON_GAT_GATE_CLKCMU_DNC_BUSM,
	CLK_CON_GAT_GATE_CLKCMU_DNS_BUS,
	CLK_CON_GAT_GATE_CLKCMU_DPU,
	CLK_CON_GAT_GATE_CLKCMU_DPU_BUS,
	CLK_CON_GAT_GATE_CLKCMU_DSP_BUS,
	CLK_CON_GAT_GATE_CLKCMU_G2D_G2D,
	CLK_CON_GAT_GATE_CLKCMU_G2D_MSCL,
	CLK_CON_GAT_GATE_CLKCMU_G3D_SWITCH,
	CLK_CON_GAT_GATE_CLKCMU_HPM,
	CLK_CON_GAT_GATE_CLKCMU_HSI0_BUS,
	CLK_CON_GAT_GATE_CLKCMU_HSI0_DPGTC,
	CLK_CON_GAT_GATE_CLKCMU_HSI0_USB31DRD,
	CLK_CON_GAT_GATE_CLKCMU_HSI0_USBDP_DEBUG,
	CLK_CON_GAT_GATE_CLKCMU_HSI1_BUS,
	CLK_CON_GAT_GATE_CLKCMU_HSI1_MMC_CARD,
	CLK_CON_GAT_GATE_CLKCMU_HSI1_PCIE,
	CLK_CON_GAT_GATE_CLKCMU_HSI1_UFS_CARD,
	CLK_CON_GAT_GATE_CLKCMU_HSI1_UFS_EMBD,
	CLK_CON_GAT_GATE_CLKCMU_HSI2_BUS,
	CLK_CON_GAT_GATE_CLKCMU_HSI2_PCIE,
	CLK_CON_GAT_GATE_CLKCMU_IPP_BUS,
	CLK_CON_GAT_GATE_CLKCMU_ITP_BUS,
	CLK_CON_GAT_GATE_CLKCMU_MCSC_BUS,
	CLK_CON_GAT_GATE_CLKCMU_MCSC_GDC,
	CLK_CON_GAT_GATE_CLKCMU_MFC0_MFC0,
	CLK_CON_GAT_GATE_CLKCMU_MFC0_WFD,
	CLK_CON_GAT_GATE_CLKCMU_MIF_BUSP,
	CLK_CON_GAT_GATE_CLKCMU_NPU_BUS,
	CLK_CON_GAT_GATE_CLKCMU_PERIC0_BUS,
	CLK_CON_GAT_GATE_CLKCMU_PERIC0_IP,
	CLK_CON_GAT_GATE_CLKCMU_PERIC1_BUS,
	CLK_CON_GAT_GATE_CLKCMU_PERIC1_IP,
	CLK_CON_GAT_GATE_CLKCMU_PERIS_BUS,
	CLK_CON_GAT_GATE_CLKCMU_SSP_BUS,
	CLK_CON_GAT_GATE_CLKCMU_TNR_BUS,
	CLK_CON_GAT_GATE_CLKCMU_VRA_BUS,
};

static const struct samsung_pll_clock top_pll_clks[] __initconst = {
	PLL(pll_0717x, CLK_FOUT_SHARED0_PLL, "fout_shared0_pll", "oscclk",
	    PLL_LOCKTIME_PLL_SHARED0, PLL_CON3_PLL_SHARED0, NULL),
	PLL(pll_0717x, CLK_FOUT_SHARED1_PLL, "fout_shared1_pll", "oscclk",
	    PLL_LOCKTIME_PLL_SHARED1, PLL_CON3_PLL_SHARED1, NULL),
	PLL(pll_0718x, CLK_FOUT_SHARED2_PLL, "fout_shared2_pll", "oscclk",
	    PLL_LOCKTIME_PLL_SHARED2, PLL_CON3_PLL_SHARED2, NULL),
	PLL(pll_0718x, CLK_FOUT_SHARED3_PLL, "fout_shared3_pll", "oscclk",
	    PLL_LOCKTIME_PLL_SHARED3, PLL_CON3_PLL_SHARED3, NULL),
	PLL(pll_0717x, CLK_FOUT_SHARED4_PLL, "fout_shared4_pll", "oscclk",
	    PLL_LOCKTIME_PLL_SHARED4, PLL_CON3_PLL_SHARED4, NULL),
	PLL(pll_0732x, CLK_FOUT_MMC_PLL, "fout_mmc_pll", "oscclk",
	    PLL_LOCKTIME_PLL_MMC, PLL_CON3_PLL_MMC, NULL),
	PLL(pll_0718x, CLK_FOUT_G3D_PLL, "fout_g3d_pll", "oscclk",
	    PLL_LOCKTIME_PLL_G3D, PLL_CON3_PLL_G3D, NULL),
};

/* Parent clock list for CMU_TOP muxes */
PNAME(mout_pll_shared0_p)		= { "oscclk", "fout_shared0_pll" };
PNAME(mout_pll_shared1_p)		= { "oscclk", "fout_shared1_pll" };
PNAME(mout_pll_shared2_p)		= { "oscclk", "fout_shared2_pll" };
PNAME(mout_pll_shared3_p)		= { "oscclk", "fout_shared3_pll" };
PNAME(mout_pll_shared4_p)		= { "oscclk", "fout_shared4_pll" };
PNAME(mout_pll_mmc_p)			= { "oscclk", "fout_mmc_pll" };
PNAME(mout_pll_g3d_p)			= { "oscclk", "fout_g3d_pll" };
PNAME(mout_cmu_dpu_bus_p)		= { "dout_cmu_dpu",
					    "dout_cmu_dpu_alt" };
PNAME(mout_cmu_apm_bus_p)		= { "dout_cmu_shared0_div2",
					    "dout_cmu_shared2_div2" };
PNAME(mout_cmu_aud_cpu_p)		= { "dout_cmu_shared0_div2",
					    "fout_shared2_pll",
					    "dout_cmu_shared4_div2",
					    "dout_cmu_shared0_div4" };
PNAME(mout_cmu_bus0_bus_p)		= { "dout_cmu_shared0_div4",
					    "dout_cmu_shared1_div4",
					    "dout_cmu_shared2_div2",
					    "oscclk" };
PNAME(mout_cmu_bus1_bus_p)		= { "dout_cmu_shared0_div4",
					    "dout_cmu_shared1_div4",
					    "dout_cmu_shared2_div2",
					    "oscclk" };
PNAME(mout_cmu_bus1_sss_p)		= { "dout_cmu_shared0_div4",
					    "dout_cmu_shared1_div4",
					    "dout_cmu_shared2_div2",
					    "oscclk" };
PNAME(mout_cmu_cis_clk0_p)		= { "oscclk",
					    "dout_cmu_shared2_div2" };
PNAME(mout_cmu_cis_clk1_p)		= { "oscclk",
					    "dout_cmu_shared2_div2" };
PNAME(mout_cmu_cis_clk2_p)		= { "oscclk",
					    "dout_cmu_shared2_div2" };
PNAME(mout_cmu_cis_clk3_p)		= { "oscclk",
					    "dout_cmu_shared2_div2" };
PNAME(mout_cmu_cis_clk4_p)		= { "oscclk",
					    "dout_cmu_shared2_div2" };
PNAME(mout_cmu_cis_clk5_p)		= { "oscclk",
					    "dout_cmu_shared2_div2" };
PNAME(mout_cmu_cmu_boost_p)		= { "dout_cmu_shared0_div4",
					    "dout_cmu_shared1_div4",
					    "dout_cmu_shared2_div2",
					    "oscclk" };
PNAME(mout_cmu_core_bus_p)		= { "dout_cmu_shared0_div2",
					    "dout_cmu_shared1_div2",
					    "fout_shared2_pll",
					    "dout_cmu_shared0_div3",
					    "dout_cmu_shared1_div3",
					    "dout_cmu_shared0_div4",
					    "fout_shared3_pll", "oscclk" };
PNAME(mout_cmu_cpucl0_dbg_bus_p)	= { "fout_shared2_pll",
					    "dout_cmu_shared0_div3",
					    "dout_cmu_shared0_div4",
					    "oscclk" };
PNAME(mout_cmu_cpucl0_switch_p)		= { "fout_shared4_pll",
					    "dout_cmu_shared0_div2",
					    "fout_shared2_pll",
					    "dout_cmu_shared0_div4" };
PNAME(mout_cmu_cpucl1_switch_p)	= { "fout_shared4_pll",
					    "dout_cmu_shared0_div2",
					    "fout_shared2_pll",
					    "dout_cmu_shared0_div4" };
PNAME(mout_cmu_cpucl2_busp_p)		= { "dout_cmu_shared0_div4",
					    "dout_cmu_shared2_div2" };
PNAME(mout_cmu_cpucl2_switch_p)		= { "fout_shared4_pll",
					    "dout_cmu_shared0_div2",
					    "fout_shared2_pll",
					    "dout_cmu_shared0_div4" };
PNAME(mout_cmu_csis_bus_p)		= { "dout_cmu_shared0_div3",
					    "dout_cmu_shared4_div2",
					    "dout_cmu_shared0_div4",
					    "dout_cmu_shared4_div3" };
PNAME(mout_cmu_csis_ois_mcu_p)		= { "dout_cmu_shared0_div4",
					    "dout_cmu_shared2_div2" };
PNAME(mout_cmu_dnc_bus_p)		= { "dout_cmu_shared1_div2",
					    "fout_shared2_pll",
					    "dout_cmu_shared4_div2",
					    "dout_cmu_shared0_div4" };
PNAME(mout_cmu_dnc_busm_p)		= { "dout_cmu_shared0_div4",
					    "dout_cmu_shared1_div4",
					    "dout_cmu_shared2_div2",
					    "dout_cmu_shared4_div4" };
PNAME(mout_cmu_dns_bus_p)		= { "dout_cmu_shared0_div3",
					    "dout_cmu_shared4_div2",
					    "dout_cmu_shared0_div4",
					    "dout_cmu_shared1_div4",
					    "dout_cmu_shared4_div3",
					    "dout_cmu_shared2_div2",
					    "oscclk", "oscclk" };
PNAME(mout_cmu_dpu_p)			= { "dout_cmu_shared0_div3",
					    "dout_cmu_shared0_div4" };
PNAME(mout_cmu_dpu_alt_p)		= { "dout_cmu_shared4_div2",
					    "dout_cmu_shared4_div3",
					    "dout_cmu_shared2_div2",
					    "oscclk" };
PNAME(mout_cmu_dsp_bus_p)		= { "dout_cmu_shared0_div2",
					    "dout_cmu_shared1_div2",
					    "fout_shared2_pll",
					    "dout_cmu_shared4_div2",
					    "fout_shared3_pll", "oscclk",
					    "oscclk", "oscclk" };
PNAME(mout_cmu_g2d_g2d_p)		= { "dout_cmu_shared0_div3",
					    "dout_cmu_shared4_div2",
					    "dout_cmu_shared0_div4",
					    "dout_cmu_shared2_div2" };
PNAME(mout_cmu_g2d_mscl_p)		= { "dout_cmu_shared0_div4",
					    "dout_cmu_shared2_div2",
					    "dout_cmu_shared4_div4",
					    "oscclk" };
PNAME(mout_cmu_hpm_p)			= { "oscclk",
					    "dout_cmu_shared0_div4",
					    "dout_cmu_shared2_div2",
					    "oscclk" };
PNAME(mout_cmu_hsi0_bus_p)		= { "dout_cmu_shared0_div4",
					    "dout_cmu_shared2_div2" };
PNAME(mout_cmu_hsi0_dpgtc_p)		= { "oscclk", "dout_cmu_shared0_div4",
					    "dout_cmu_shared2_div2",
					    "oscclk" };
PNAME(mout_cmu_hsi0_usb31drd_p)		= { "oscclk", "dout_cmu_shared0_div4",
					    "dout_cmu_shared2_div2",
					    "oscclk" };
PNAME(mout_cmu_hsi0_usbdp_debug_p)	= { "oscclk", "fout_shared2_pll" };
PNAME(mout_cmu_hsi1_bus_p)		= { "dout_cmu_shared0_div3",
					    "dout_cmu_shared0_div4",
					    "dout_cmu_shared1_div4",
					    "dout_cmu_shared4_div3",
					    "dout_cmu_shared2_div2",
					    "fout_mmc_pll", "oscclk", "oscclk" };
PNAME(mout_cmu_hsi1_mmc_card_p)	= { "oscclk", "fout_shared2_pll",
					    "fout_mmc_pll",
					    "dout_cmu_shared0_div4" };
PNAME(mout_cmu_hsi1_pcie_p)		= { "oscclk", "fout_shared2_pll" };
PNAME(mout_cmu_hsi1_ufs_card_p)		= { "oscclk", "dout_cmu_shared0_div4",
					    "dout_cmu_shared2_div2",
					    "oscclk" };
PNAME(mout_cmu_hsi1_ufs_embd_p)		= { "oscclk", "dout_cmu_shared0_div4",
					    "dout_cmu_shared2_div2",
					    "oscclk" };
PNAME(mout_cmu_hsi2_bus_p)		= { "dout_cmu_shared0_div3",
					    "dout_cmu_shared2_div2" };
PNAME(mout_cmu_hsi2_pcie_p)		= { "oscclk", "fout_shared2_pll" };
PNAME(mout_cmu_ipp_bus_p)		= { "dout_cmu_shared0_div3",
					    "dout_cmu_shared4_div2",
					    "dout_cmu_shared0_div4",
					    "dout_cmu_shared1_div4",
					    "dout_cmu_shared4_div3",
					    "oscclk", "oscclk", "oscclk" };
PNAME(mout_cmu_itp_bus_p)		= { "dout_cmu_shared0_div3",
					    "dout_cmu_shared4_div2",
					    "dout_cmu_shared0_div4",
					    "dout_cmu_shared1_div4",
					    "dout_cmu_shared4_div3",
					    "dout_cmu_shared2_div2",
					    "oscclk", "oscclk" };
PNAME(mout_cmu_mcsc_bus_p)		= { "dout_cmu_shared0_div3",
					    "dout_cmu_shared4_div2",
					    "dout_cmu_shared0_div4",
					    "dout_cmu_shared1_div4",
					    "dout_cmu_shared4_div3",
					    "dout_cmu_shared2_div2",
					    "oscclk", "oscclk" };
PNAME(mout_cmu_mcsc_gdc_p)		= { "dout_cmu_shared0_div3",
					    "dout_cmu_shared4_div2",
					    "dout_cmu_shared0_div4",
					    "dout_cmu_shared1_div4",
					    "dout_cmu_shared4_div3",
					    "dout_cmu_shared2_div2",
					    "oscclk", "oscclk" };
PNAME(mout_cmu_cmu_boost_cpu_p)		= { "dout_cmu_shared0_div4",
					    "dout_cmu_shared1_div4",
					    "dout_cmu_shared2_div2",
					    "oscclk" };
PNAME(mout_cmu_mfc0_mfc0_p)		= { "dout_cmu_shared4_div2",
					    "dout_cmu_shared0_div4",
					    "dout_cmu_shared4_div3",
					    "dout_cmu_shared2_div2" };
PNAME(mout_cmu_mfc0_wfd_p)		= { "dout_cmu_shared4_div2",
					    "dout_cmu_shared0_div4",
					    "dout_cmu_shared4_div3",
					    "dout_cmu_shared2_div2" };
PNAME(mout_cmu_mif_busp_p)		= { "dout_cmu_shared0_div4",
					    "dout_cmu_shared1_div4",
					    "dout_cmu_shared2_div2",
					    "oscclk" };
PNAME(mout_cmu_mif_switch_p)		= { "fout_shared0_pll",
					    "fout_shared1_pll",
					    "dout_cmu_shared0_div2",
					    "dout_cmu_shared1_div2",
					    "fout_shared2_pll",
					    "dout_cmu_shared0_div4",
					    "dout_cmu_shared2_div2",
					    "oscclk" };
PNAME(mout_cmu_npu_bus_p)		= { "dout_cmu_shared0_div2",
					    "dout_cmu_shared1_div2",
					    "fout_shared2_pll",
					    "dout_cmu_shared4_div2",
					    "fout_shared3_pll", "oscclk",
					    "oscclk", "oscclk" };
PNAME(mout_cmu_peric0_bus_p)		= { "dout_cmu_shared0_div4",
					    "dout_cmu_shared2_div2" };
PNAME(mout_cmu_peric0_ip_p)		= { "dout_cmu_shared0_div4",
					    "dout_cmu_shared2_div2" };
PNAME(mout_cmu_peric1_bus_p)		= { "dout_cmu_shared0_div4",
					    "dout_cmu_shared2_div2" };
PNAME(mout_cmu_peric1_ip_p)		= { "dout_cmu_shared0_div4",
					    "dout_cmu_shared2_div2" };
PNAME(mout_cmu_peris_bus_p)		= { "dout_cmu_shared0_div4",
					    "dout_cmu_shared2_div2" };
PNAME(mout_cmu_ssp_bus_p)		= { "dout_cmu_shared4_div2",
					    "dout_cmu_shared0_div4",
					    "dout_cmu_shared4_div3",
					    "dout_cmu_shared2_div2" };
PNAME(mout_cmu_tnr_bus_p)		= { "dout_cmu_shared0_div3",
					    "dout_cmu_shared4_div2",
					    "dout_cmu_shared0_div4",
					    "dout_cmu_shared1_div4",
					    "dout_cmu_shared4_div3",
					    "dout_cmu_shared2_div2",
					    "oscclk", "oscclk" };
PNAME(mout_cmu_vra_bus_p)		= { "dout_cmu_shared0_div3",
					    "dout_cmu_shared4_div2",
					    "dout_cmu_shared0_div4",
					    "dout_cmu_shared4_div3" };
PNAME(mout_cmu_cmuref_p)		= { "oscclk",
					    "dout_cmu_clk_cmuref" };
PNAME(mout_cmu_clk_cmuref_p)		= { "dout_cmu_shared0_div4",
					    "dout_cmu_shared1_div4",
					    "dout_cmu_shared2_div2",
					    "oscclk" };

/*
 * Register name to clock name mangling strategy used in this file
 *
 * Replace PLL_CON{0,3}_PLL	   with CLK_MOUT_PLL and mout_pll
 * Replace CLK_CON_MUX_MUX_CLKCMU  with CLK_MOUT_CMU and mout_cmu
 * Replace CLK_CON_DIV_CLKCMU      with CLK_DOUT_CMU_CMU and dout_cmu_cmu
 * Replace CLK_CON_DIV_DIV_CLKCMU  with CLK_DOUT_CMU_CMU and dout_cmu_cmu
 * Replace CLK_CON_DIV_PLL_CLKCMU  with CLK_DOUT_CMU_CMU and dout_cmu_cmu
 * Replace CLK_CON_GAT_CLKCMU      with CLK_GOUT_CMU and gout_cmu
 * Replace CLK_CON_GAT_GATE_CLKCMU with CLK_GOUT_CMU and gout_cmu
 *
 * For gates remove _UID _BLK _IPCLKPORT, _I and _RSTNSYNC
 */

static const struct samsung_mux_clock top_mux_clks[] __initconst = {
	MUX(CLK_MOUT_PLL_SHARED0, "mout_pll_shared0", mout_pll_shared0_p,
	    PLL_CON0_PLL_SHARED0, 4, 1),
	MUX(CLK_MOUT_PLL_SHARED1, "mout_pll_shared1", mout_pll_shared1_p,
	    PLL_CON0_PLL_SHARED1, 4, 1),
	MUX(CLK_MOUT_PLL_SHARED2, "mout_pll_shared2", mout_pll_shared2_p,
	    PLL_CON0_PLL_SHARED2, 4, 1),
	MUX(CLK_MOUT_PLL_SHARED3, "mout_pll_shared3", mout_pll_shared3_p,
	    PLL_CON0_PLL_SHARED3, 4, 1),
	MUX(CLK_MOUT_PLL_SHARED4, "mout_pll_shared4", mout_pll_shared4_p,
	    PLL_CON0_PLL_SHARED4, 4, 1),
	MUX(CLK_MOUT_PLL_MMC, "mout_pll_mmc", mout_pll_mmc_p,
	    PLL_CON0_PLL_MMC, 4, 1),
	MUX(CLK_MOUT_PLL_G3D, "mout_pll_g3d", mout_pll_g3d_p,
	    PLL_CON0_PLL_G3D, 4, 1),
	MUX(CLK_MOUT_CMU_DPU_BUS, "mout_cmu_dpu_bus",
	    mout_cmu_dpu_bus_p, CLK_CON_MUX_CLKCMU_DPU_BUS, 0, 1),
	MUX(CLK_MOUT_CMU_APM_BUS, "mout_cmu_apm_bus",
	    mout_cmu_apm_bus_p, CLK_CON_MUX_MUX_CLKCMU_APM_BUS, 0, 1),
	MUX(CLK_MOUT_CMU_AUD_CPU, "mout_cmu_aud_cpu",
	    mout_cmu_aud_cpu_p, CLK_CON_MUX_MUX_CLKCMU_AUD_CPU, 0, 2),
	MUX(CLK_MOUT_CMU_BUS0_BUS, "mout_cmu_bus0_bus",
	    mout_cmu_bus0_bus_p, CLK_CON_MUX_MUX_CLKCMU_BUS0_BUS, 0, 2),
	MUX(CLK_MOUT_CMU_BUS1_BUS, "mout_cmu_bus1_bus",
	    mout_cmu_bus1_bus_p, CLK_CON_MUX_MUX_CLKCMU_BUS1_BUS, 0, 2),
	MUX(CLK_MOUT_CMU_BUS1_SSS, "mout_cmu_bus1_sss",
	    mout_cmu_bus1_sss_p, CLK_CON_MUX_MUX_CLKCMU_BUS1_SSS, 0, 2),
	MUX(CLK_MOUT_CMU_CIS_CLK0, "mout_cmu_cis_clk0",
	    mout_cmu_cis_clk0_p, CLK_CON_MUX_MUX_CLKCMU_CIS_CLK0, 0, 1),
	MUX(CLK_MOUT_CMU_CIS_CLK1, "mout_cmu_cis_clk1",
	    mout_cmu_cis_clk1_p, CLK_CON_MUX_MUX_CLKCMU_CIS_CLK1, 0, 1),
	MUX(CLK_MOUT_CMU_CIS_CLK2, "mout_cmu_cis_clk2",
	    mout_cmu_cis_clk2_p, CLK_CON_MUX_MUX_CLKCMU_CIS_CLK2, 0, 1),
	MUX(CLK_MOUT_CMU_CIS_CLK3, "mout_cmu_cis_clk3",
	    mout_cmu_cis_clk3_p, CLK_CON_MUX_MUX_CLKCMU_CIS_CLK3, 0, 1),
	MUX(CLK_MOUT_CMU_CIS_CLK4, "mout_cmu_cis_clk4",
	    mout_cmu_cis_clk4_p, CLK_CON_MUX_MUX_CLKCMU_CIS_CLK4, 0, 1),
	MUX(CLK_MOUT_CMU_CIS_CLK5, "mout_cmu_cis_clk5",
	    mout_cmu_cis_clk5_p, CLK_CON_MUX_MUX_CLKCMU_CIS_CLK5, 0, 1),
	MUX(CLK_MOUT_CMU_CMU_BOOST, "mout_cmu_cmu_boost",
	    mout_cmu_cmu_boost_p, CLK_CON_MUX_MUX_CLKCMU_CMU_BOOST, 0, 2),
	MUX(CLK_MOUT_CMU_CORE_BUS, "mout_cmu_core_bus",
	    mout_cmu_core_bus_p, CLK_CON_MUX_MUX_CLKCMU_CORE_BUS, 0, 3),
	MUX(CLK_MOUT_CMU_CPUCL0_DBG_BUS, "mout_cmu_cpucl0_dbg_bus",
	    mout_cmu_cpucl0_dbg_bus_p, CLK_CON_MUX_MUX_CLKCMU_CPUCL0_DBG_BUS,
	    0, 2),
	MUX(CLK_MOUT_CMU_CPUCL0_SWITCH, "mout_cmu_cpucl0_switch",
	    mout_cmu_cpucl0_switch_p, CLK_CON_MUX_MUX_CLKCMU_CPUCL0_SWITCH,
	    0, 2),
	MUX(CLK_MOUT_CMU_CPUCL1_SWITCH, "mout_cmu_cpucl1_switch",
	    mout_cmu_cpucl1_switch_p, CLK_CON_MUX_MUX_CLKCMU_CPUCL1_SWITCH,
	    0, 2),
	MUX(CLK_MOUT_CMU_CPUCL2_BUSP, "mout_cmu_cpucl2_busp",
	    mout_cmu_cpucl2_busp_p, CLK_CON_MUX_MUX_CLKCMU_CPUCL2_BUSP,
	    0, 1),
	MUX(CLK_MOUT_CMU_CPUCL2_SWITCH, "mout_cmu_cpucl2_switch",
	    mout_cmu_cpucl2_switch_p, CLK_CON_MUX_MUX_CLKCMU_CPUCL2_SWITCH,
	    0, 2),
	MUX(CLK_MOUT_CMU_CSIS_BUS, "mout_cmu_csis_bus",
	    mout_cmu_csis_bus_p, CLK_CON_MUX_MUX_CLKCMU_CSIS_BUS, 0, 2),
	MUX(CLK_MOUT_CMU_CSIS_OIS_MCU, "mout_cmu_csis_ois_mcu",
	    mout_cmu_csis_ois_mcu_p, CLK_CON_MUX_MUX_CLKCMU_CSIS_OIS_MCU,
	    0, 1),
	MUX(CLK_MOUT_CMU_DNC_BUS, "mout_cmu_dnc_bus",
	    mout_cmu_dnc_bus_p, CLK_CON_MUX_MUX_CLKCMU_DNC_BUS, 0, 2),
	MUX(CLK_MOUT_CMU_DNC_BUSM, "mout_cmu_dnc_busm",
	    mout_cmu_dnc_busm_p, CLK_CON_MUX_MUX_CLKCMU_DNC_BUSM, 0, 2),
	MUX(CLK_MOUT_CMU_DNS_BUS, "mout_cmu_dns_bus",
	    mout_cmu_dns_bus_p, CLK_CON_MUX_MUX_CLKCMU_DNS_BUS, 0, 3),
	MUX(CLK_MOUT_CMU_DPU, "mout_cmu_dpu",
	    mout_cmu_dpu_p, CLK_CON_MUX_MUX_CLKCMU_DPU, 0, 1),
	MUX(CLK_MOUT_CMU_DPU_ALT, "mout_cmu_dpu_alt",
	    mout_cmu_dpu_alt_p, CLK_CON_MUX_MUX_CLKCMU_DPU_ALT, 0, 2),
	MUX(CLK_MOUT_CMU_DSP_BUS, "mout_cmu_dsp_bus",
	    mout_cmu_dsp_bus_p, CLK_CON_MUX_MUX_CLKCMU_DSP_BUS, 0, 3),
	MUX(CLK_MOUT_CMU_G2D_G2D, "mout_cmu_g2d_g2d",
	    mout_cmu_g2d_g2d_p, CLK_CON_MUX_MUX_CLKCMU_G2D_G2D, 0, 2),
	MUX(CLK_MOUT_CMU_G2D_MSCL, "mout_cmu_g2d_mscl",
	    mout_cmu_g2d_mscl_p, CLK_CON_MUX_MUX_CLKCMU_G2D_MSCL, 0, 2),
	MUX(CLK_MOUT_CMU_HPM, "mout_cmu_hpm",
	    mout_cmu_hpm_p, CLK_CON_MUX_MUX_CLKCMU_HPM, 0, 2),
	MUX(CLK_MOUT_CMU_HSI0_BUS, "mout_cmu_hsi0_bus",
	    mout_cmu_hsi0_bus_p, CLK_CON_MUX_MUX_CLKCMU_HSI0_BUS, 0, 1),
	MUX(CLK_MOUT_CMU_HSI0_DPGTC, "mout_cmu_hsi0_dpgtc",
	    mout_cmu_hsi0_dpgtc_p, CLK_CON_MUX_MUX_CLKCMU_HSI0_DPGTC, 0, 2),
	MUX(CLK_MOUT_CMU_HSI0_USB31DRD, "mout_cmu_hsi0_usb31drd",
	    mout_cmu_hsi0_usb31drd_p, CLK_CON_MUX_MUX_CLKCMU_HSI0_USB31DRD,
	    0, 2),
	MUX(CLK_MOUT_CMU_HSI0_USBDP_DEBUG, "mout_cmu_hsi0_usbdp_debug",
	    mout_cmu_hsi0_usbdp_debug_p,
	    CLK_CON_MUX_MUX_CLKCMU_HSI0_USBDP_DEBUG, 0, 1),
	MUX(CLK_MOUT_CMU_HSI1_BUS, "mout_cmu_hsi1_bus",
	    mout_cmu_hsi1_bus_p, CLK_CON_MUX_MUX_CLKCMU_HSI1_BUS, 0, 3),
	MUX(CLK_MOUT_CMU_HSI1_MMC_CARD, "mout_cmu_hsi1_mmc_card",
	    mout_cmu_hsi1_mmc_card_p, CLK_CON_MUX_MUX_CLKCMU_HSI1_MMC_CARD,
	    0, 2),
	MUX(CLK_MOUT_CMU_HSI1_PCIE, "mout_cmu_hsi1_pcie",
	    mout_cmu_hsi1_pcie_p, CLK_CON_MUX_MUX_CLKCMU_HSI1_PCIE, 0, 1),
	MUX(CLK_MOUT_CMU_HSI1_UFS_CARD, "mout_cmu_hsi1_ufs_card",
	    mout_cmu_hsi1_ufs_card_p, CLK_CON_MUX_MUX_CLKCMU_HSI1_UFS_CARD,
	    0, 2),
	MUX(CLK_MOUT_CMU_HSI1_UFS_EMBD, "mout_cmu_hsi1_ufs_embd",
	    mout_cmu_hsi1_ufs_embd_p, CLK_CON_MUX_MUX_CLKCMU_HSI1_UFS_EMBD,
	    0, 2),
	MUX(CLK_MOUT_CMU_HSI2_BUS, "mout_cmu_hsi2_bus",
	    mout_cmu_hsi2_bus_p, CLK_CON_MUX_MUX_CLKCMU_HSI2_BUS, 0, 1),
	MUX(CLK_MOUT_CMU_HSI2_PCIE, "mout_cmu_hsi2_pcie",
	    mout_cmu_hsi2_pcie_p, CLK_CON_MUX_MUX_CLKCMU_HSI2_PCIE, 0, 1),
	MUX(CLK_MOUT_CMU_IPP_BUS, "mout_cmu_ipp_bus",
	    mout_cmu_ipp_bus_p, CLK_CON_MUX_MUX_CLKCMU_IPP_BUS, 0, 3),
	MUX(CLK_MOUT_CMU_ITP_BUS, "mout_cmu_itp_bus",
	    mout_cmu_itp_bus_p, CLK_CON_MUX_MUX_CLKCMU_ITP_BUS, 0, 3),
	MUX(CLK_MOUT_CMU_MCSC_BUS, "mout_cmu_mcsc_bus",
	    mout_cmu_mcsc_bus_p, CLK_CON_MUX_MUX_CLKCMU_MCSC_BUS, 0, 3),
	MUX(CLK_MOUT_CMU_MCSC_GDC, "mout_cmu_mcsc_gdc",
	    mout_cmu_mcsc_gdc_p, CLK_CON_MUX_MUX_CLKCMU_MCSC_GDC, 0, 3),
	MUX(CLK_MOUT_CMU_CMU_BOOST_CPU, "mout_cmu_cmu_boost_cpu",
	    mout_cmu_cmu_boost_cpu_p, CLK_CON_MUX_MUX_CLKCMU_CMU_BOOST_CPU,
	    0, 2),
	MUX(CLK_MOUT_CMU_MFC0_MFC0, "mout_cmu_mfc0_mfc0",
	    mout_cmu_mfc0_mfc0_p, CLK_CON_MUX_MUX_CLKCMU_MFC0_MFC0, 0, 2),
	MUX(CLK_MOUT_CMU_MFC0_WFD, "mout_cmu_mfc0_wfd",
	    mout_cmu_mfc0_wfd_p, CLK_CON_MUX_MUX_CLKCMU_MFC0_WFD, 0, 2),
	MUX(CLK_MOUT_CMU_MIF_BUSP, "mout_cmu_mif_busp",
	    mout_cmu_mif_busp_p, CLK_CON_MUX_MUX_CLKCMU_MIF_BUSP, 0, 2),
	MUX(CLK_MOUT_CMU_MIF_SWITCH, "mout_cmu_mif_switch",
	    mout_cmu_mif_switch_p, CLK_CON_MUX_MUX_CLKCMU_MIF_SWITCH, 0, 3),
	MUX(CLK_MOUT_CMU_NPU_BUS, "mout_cmu_npu_bus",
	    mout_cmu_npu_bus_p, CLK_CON_MUX_MUX_CLKCMU_NPU_BUS, 0, 3),
	MUX(CLK_MOUT_CMU_PERIC0_BUS, "mout_cmu_peric0_bus",
	    mout_cmu_peric0_bus_p, CLK_CON_MUX_MUX_CLKCMU_PERIC0_BUS, 0, 1),
	MUX(CLK_MOUT_CMU_PERIC0_IP, "mout_cmu_peric0_ip",
	    mout_cmu_peric0_ip_p, CLK_CON_MUX_MUX_CLKCMU_PERIC0_IP, 0, 1),
	MUX(CLK_MOUT_CMU_PERIC1_BUS, "mout_cmu_peric1_bus",
	    mout_cmu_peric1_bus_p, CLK_CON_MUX_MUX_CLKCMU_PERIC1_BUS, 0, 1),
	MUX(CLK_MOUT_CMU_PERIC1_IP, "mout_cmu_peric1_ip",
	    mout_cmu_peric1_ip_p, CLK_CON_MUX_MUX_CLKCMU_PERIC1_IP, 0, 1),
	MUX(CLK_MOUT_CMU_PERIS_BUS, "mout_cmu_peris_bus",
	    mout_cmu_peris_bus_p, CLK_CON_MUX_MUX_CLKCMU_PERIS_BUS, 0, 1),
	MUX(CLK_MOUT_CMU_SSP_BUS, "mout_cmu_ssp_bus",
	    mout_cmu_ssp_bus_p, CLK_CON_MUX_MUX_CLKCMU_SSP_BUS, 0, 2),
	MUX(CLK_MOUT_CMU_TNR_BUS, "mout_cmu_tnr_bus",
	    mout_cmu_tnr_bus_p, CLK_CON_MUX_MUX_CLKCMU_TNR_BUS, 0, 3),
	MUX(CLK_MOUT_CMU_VRA_BUS, "mout_cmu_vra_bus",
	    mout_cmu_vra_bus_p, CLK_CON_MUX_MUX_CLKCMU_VRA_BUS, 0, 2),
	MUX(CLK_MOUT_CMU_CMUREF, "mout_cmu_cmuref",
	    mout_cmu_cmuref_p, CLK_CON_MUX_MUX_CMU_CMUREF, 0, 1),
	MUX(CLK_MOUT_CMU_CLK_CMUREF, "mout_cmu_clk_cmuref",
	    mout_cmu_clk_cmuref_p, CLK_CON_MUX_MUX_CLK_CMU_CMUREF, 0, 2),
};

static const struct samsung_div_clock top_div_clks[] __initconst = {
	/* SHARED0 region*/
	DIV(CLK_DOUT_CMU_SHARED0_DIV2, "dout_cmu_shared0_div2", "mout_pll_shared0",
	    CLK_CON_DIV_PLL_SHARED0_DIV2, 0, 1),
	DIV(CLK_DOUT_CMU_SHARED0_DIV3, "dout_cmu_shared0_div3", "mout_pll_shared0",
	    CLK_CON_DIV_PLL_SHARED0_DIV3, 0, 2),
	DIV(CLK_DOUT_CMU_SHARED0_DIV4, "dout_cmu_shared0_div4", "dout_cmu_shared0_div2",
	    CLK_CON_DIV_PLL_SHARED0_DIV4, 0, 1),

	/* SHARED1 region*/
	DIV(CLK_DOUT_CMU_SHARED1_DIV2, "dout_cmu_shared1_div2", "mout_pll_shared1",
	    CLK_CON_DIV_PLL_SHARED1_DIV2, 0, 1),
	DIV(CLK_DOUT_CMU_SHARED1_DIV3, "dout_cmu_shared1_div3", "mout_pll_shared1",
	    CLK_CON_DIV_PLL_SHARED1_DIV3, 0, 2),
	DIV(CLK_DOUT_CMU_SHARED1_DIV4, "dout_cmu_shared1_div4", "dout_cmu_shared1_div2",
	    CLK_CON_DIV_PLL_SHARED1_DIV4, 0, 1),

	/* SHARED2 region */
	DIV(CLK_DOUT_CMU_SHARED2_DIV2, "dout_cmu_shared2_div2", "mout_pll_shared2",
	    CLK_CON_DIV_PLL_SHARED2_DIV2, 0, 1),

	/* SHARED4 region*/
	DIV(CLK_DOUT_CMU_SHARED4_DIV2, "dout_cmu_shared4_div2", "mout_pll_shared4",
	    CLK_CON_DIV_PLL_SHARED4_DIV2, 0, 1),
	DIV(CLK_DOUT_CMU_SHARED4_DIV3, "dout_cmu_shared4_div3", "mout_pll_shared4",
	    CLK_CON_DIV_PLL_SHARED4_DIV3, 0, 2),
	DIV(CLK_DOUT_CMU_SHARED4_DIV4, "dout_cmu_shared4_div4", "mout_pll_shared4",
	    CLK_CON_DIV_PLL_SHARED4_DIV4, 0, 1),

	DIV(CLK_DOUT_CMU_APM_BUS, "dout_cmu_apm_bus", "gout_cmu_apm_bus",
	    CLK_CON_DIV_CLKCMU_APM_BUS, 0, 2),
	DIV(CLK_DOUT_CMU_AUD_CPU, "dout_cmu_aud_cpu", "gout_cmu_aud_cpu",
	    CLK_CON_DIV_CLKCMU_AUD_CPU, 0, 3),
	DIV(CLK_DOUT_CMU_BUS0_BUS, "dout_cmu_bus0_bus", "gout_cmu_bus0_bus",
	    CLK_CON_DIV_CLKCMU_BUS0_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_BUS1_BUS, "dout_cmu_bus1_bus", "gout_cmu_bus1_bus",
	    CLK_CON_DIV_CLKCMU_BUS1_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_BUS1_SSS, "dout_cmu_bus1_sss", "gout_cmu_bus1_sss",
	    CLK_CON_DIV_CLKCMU_BUS1_SSS, 0, 4),
	DIV(CLK_DOUT_CMU_CIS_CLK0, "dout_cmu_cis_clk0", "gout_cmu_cis_clk0",
	    CLK_CON_DIV_CLKCMU_CIS_CLK0, 0, 5),
	DIV(CLK_DOUT_CMU_CIS_CLK1, "dout_cmu_cis_clk1", "gout_cmu_cis_clk1",
	    CLK_CON_DIV_CLKCMU_CIS_CLK1, 0, 5),
	DIV(CLK_DOUT_CMU_CIS_CLK2, "dout_cmu_cis_clk2", "gout_cmu_cis_clk2",
	    CLK_CON_DIV_CLKCMU_CIS_CLK2, 0, 5),
	DIV(CLK_DOUT_CMU_CIS_CLK3, "dout_cmu_cis_clk3", "gout_cmu_cis_clk3",
	    CLK_CON_DIV_CLKCMU_CIS_CLK3, 0, 5),
	DIV(CLK_DOUT_CMU_CIS_CLK4, "dout_cmu_cis_clk4", "gout_cmu_cis_clk4",
	    CLK_CON_DIV_CLKCMU_CIS_CLK4, 0, 5),
	DIV(CLK_DOUT_CMU_CIS_CLK5, "dout_cmu_cis_clk5", "gout_cmu_cis_clk5",
	    CLK_CON_DIV_CLKCMU_CIS_CLK5, 0, 5),
	DIV(CLK_DOUT_CMU_CMU_BOOST, "dout_cmu_cmu_boost", "mout_cmu_cmu_boost",
	    CLK_CON_DIV_CLKCMU_CMU_BOOST, 0, 2),
	DIV(CLK_DOUT_CMU_CORE_BUS, "dout_cmu_core_bus", "gout_cmu_core_bus",
	    CLK_CON_DIV_CLKCMU_CORE_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_CPUCL0_DBG_BUS, "dout_cmu_cpucl0_dbg_bus",
	    "gout_cmu_cpucl0_dbg_bus", CLK_CON_DIV_CLKCMU_CPUCL0_DBG_BUS,
	    0, 4),
	DIV(CLK_DOUT_CMU_CPUCL0_SWITCH, "dout_cmu_cpucl0_switch",
	    "gout_cmu_cpucl0_switch", CLK_CON_DIV_CLKCMU_CPUCL0_SWITCH, 0, 3),
	DIV(CLK_DOUT_CMU_CPUCL1_SWITCH, "dout_cmu_cpucl1_switch",
	    "gout_cmu_cpucl1_switch", CLK_CON_DIV_CLKCMU_CPUCL1_SWITCH, 0, 3),
	DIV(CLK_DOUT_CMU_CPUCL2_BUSP, "dout_cmu_cpucl2_busp",
	    "gout_cmu_cpucl2_busp", CLK_CON_DIV_CLKCMU_CPUCL2_BUSP, 0, 4),
	DIV(CLK_DOUT_CMU_CPUCL2_SWITCH, "dout_cmu_cpucl2_switch",
	    "gout_cmu_cpucl2_switch", CLK_CON_DIV_CLKCMU_CPUCL2_SWITCH, 0, 3),
	DIV(CLK_DOUT_CMU_CSIS_BUS, "dout_cmu_csis_bus", "gout_cmu_csis_bus",
	    CLK_CON_DIV_CLKCMU_CSIS_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_CSIS_OIS_MCU, "dout_cmu_csis_ois_mcu",
	    "gout_cmu_csis_ois_mcu", CLK_CON_DIV_CLKCMU_CSIS_OIS_MCU, 0, 4),
	DIV(CLK_DOUT_CMU_DNC_BUS, "dout_cmu_dnc_bus", "gout_cmu_dnc_bus",
	    CLK_CON_DIV_CLKCMU_DNC_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_DNC_BUSM, "dout_cmu_dnc_busm", "gout_cmu_dnc_busm",
	    CLK_CON_DIV_CLKCMU_DNC_BUSM, 0, 4),
	DIV(CLK_DOUT_CMU_DNS_BUS, "dout_cmu_dns_bus", "gout_cmu_dns_bus",
	    CLK_CON_DIV_CLKCMU_DNS_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_DSP_BUS, "dout_cmu_dsp_bus", "gout_cmu_dsp_bus",
	    CLK_CON_DIV_CLKCMU_DSP_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_G2D_G2D, "dout_cmu_g2d_g2d", "gout_cmu_g2d_g2d",
	    CLK_CON_DIV_CLKCMU_G2D_G2D, 0, 4),
	DIV(CLK_DOUT_CMU_G2D_MSCL, "dout_cmu_g2d_mscl", "gout_cmu_g2d_mscl",
	    CLK_CON_DIV_CLKCMU_G2D_MSCL, 0, 4),
	DIV(CLK_DOUT_CMU_G3D_SWITCH, "dout_cmu_g3d_switch",
	    "gout_cmu_g3d_switch", CLK_CON_DIV_CLKCMU_G3D_SWITCH, 0, 3),
	DIV(CLK_DOUT_CMU_HPM, "dout_cmu_hpm", "gout_cmu_hpm",
	    CLK_CON_DIV_CLKCMU_HPM, 0, 2),
	DIV(CLK_DOUT_CMU_HSI0_BUS, "dout_cmu_hsi0_bus", "gout_cmu_hsi0_bus",
	    CLK_CON_DIV_CLKCMU_HSI0_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_HSI0_DPGTC, "dout_cmu_hsi0_dpgtc", "gout_cmu_hsi0_dpgtc",
	    CLK_CON_DIV_CLKCMU_HSI0_DPGTC, 0, 3),
	DIV(CLK_DOUT_CMU_HSI0_USB31DRD, "dout_cmu_hsi0_usb31drd",
	    "gout_cmu_hsi0_usb31drd", CLK_CON_DIV_CLKCMU_HSI0_USB31DRD, 0, 4),
	DIV(CLK_DOUT_CMU_HSI1_BUS, "dout_cmu_hsi1_bus", "gout_cmu_hsi1_bus",
	    CLK_CON_DIV_CLKCMU_HSI1_BUS, 0, 3),
	DIV(CLK_DOUT_CMU_HSI1_MMC_CARD, "dout_cmu_hsi1_mmc_card",
	    "gout_cmu_hsi1_mmc_card", CLK_CON_DIV_CLKCMU_HSI1_MMC_CARD,
	    0, 9),
	DIV(CLK_DOUT_CMU_HSI1_UFS_CARD, "dout_cmu_hsi1_ufs_card",
	    "gout_cmu_hsi1_ufs_card", CLK_CON_DIV_CLKCMU_HSI1_UFS_CARD,
	    0, 3),
	DIV(CLK_DOUT_CMU_HSI1_UFS_EMBD, "dout_cmu_hsi1_ufs_embd",
	    "gout_cmu_hsi1_ufs_embd", CLK_CON_DIV_CLKCMU_HSI1_UFS_EMBD,
	    0, 3),
	DIV(CLK_DOUT_CMU_HSI2_BUS, "dout_cmu_hsi2_bus", "gout_cmu_hsi2_bus",
	    CLK_CON_DIV_CLKCMU_HSI2_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_IPP_BUS, "dout_cmu_ipp_bus", "gout_cmu_ipp_bus",
	    CLK_CON_DIV_CLKCMU_IPP_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_ITP_BUS, "dout_cmu_itp_bus", "gout_cmu_itp_bus",
	    CLK_CON_DIV_CLKCMU_ITP_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_MCSC_BUS, "dout_cmu_mcsc_bus", "gout_cmu_mcsc_bus",
	    CLK_CON_DIV_CLKCMU_MCSC_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_MCSC_GDC, "dout_cmu_mcsc_gdc", "gout_cmu_mcsc_gdc",
	    CLK_CON_DIV_CLKCMU_MCSC_GDC, 0, 4),
	DIV(CLK_DOUT_CMU_CMU_BOOST_CPU, "dout_cmu_cmu_boost_cpu",
	    "mout_cmu_cmu_boost_cpu", CLK_CON_DIV_CLKCMU_CMU_BOOST_CPU,
	    0, 2),
	DIV(CLK_DOUT_CMU_MFC0_MFC0, "dout_cmu_mfc0_mfc0", "gout_cmu_mfc0_mfc0",
	    CLK_CON_DIV_CLKCMU_MFC0_MFC0, 0, 4),
	DIV(CLK_DOUT_CMU_MFC0_WFD, "dout_cmu_mfc0_wfd", "gout_cmu_mfc0_wfd",
	    CLK_CON_DIV_CLKCMU_MFC0_WFD, 0, 4),
	DIV(CLK_DOUT_CMU_MIF_BUSP, "dout_cmu_mif_busp", "gout_cmu_mif_busp",
	    CLK_CON_DIV_CLKCMU_MIF_BUSP, 0, 4),
	DIV(CLK_DOUT_CMU_NPU_BUS, "dout_cmu_npu_bus", "gout_cmu_npu_bus",
	    CLK_CON_DIV_CLKCMU_NPU_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_PERIC0_BUS, "dout_cmu_peric0_bus", "gout_cmu_peric0_bus",
	    CLK_CON_DIV_CLKCMU_PERIC0_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_PERIC0_IP, "dout_cmu_peric0_ip", "gout_cmu_peric0_ip",
	    CLK_CON_DIV_CLKCMU_PERIC0_IP, 0, 4),
	DIV(CLK_DOUT_CMU_PERIC1_BUS, "dout_cmu_peric1_bus", "gout_cmu_peric1_bus",
	    CLK_CON_DIV_CLKCMU_PERIC1_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_PERIC1_IP, "dout_cmu_peric1_ip", "gout_cmu_peric1_ip",
	    CLK_CON_DIV_CLKCMU_PERIC1_IP, 0, 4),
	DIV(CLK_DOUT_CMU_PERIS_BUS, "dout_cmu_peris_bus", "gout_cmu_peris_bus",
	    CLK_CON_DIV_CLKCMU_PERIS_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_SSP_BUS, "dout_cmu_ssp_bus", "gout_cmu_ssp_bus",
	    CLK_CON_DIV_CLKCMU_SSP_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_TNR_BUS, "dout_cmu_tnr_bus", "gout_cmu_tnr_bus",
	    CLK_CON_DIV_CLKCMU_TNR_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_VRA_BUS, "dout_cmu_vra_bus", "gout_cmu_vra_bus",
	    CLK_CON_DIV_CLKCMU_VRA_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_DPU, "dout_cmu_dpu", "gout_cmu_dpu",
	    CLK_CON_DIV_DIV_CLKCMU_DPU, 0, 3),
	DIV(CLK_DOUT_CMU_DPU_ALT, "dout_cmu_dpu_alt", "gout_cmu_dpu_bus",
	    CLK_CON_DIV_DIV_CLKCMU_DPU_ALT, 0, 4),
	DIV(CLK_DOUT_CMU_CLK_CMUREF, "dout_cmu_clk_cmuref", "mout_cmu_clk_cmuref",
	    CLK_CON_DIV_DIV_CLK_CMU_CMUREF, 0, 2),
};

static const struct samsung_fixed_factor_clock cmu_top_ffactor[] __initconst = {
	FFACTOR(CLK_DOUT_CMU_HSI1_PCIE, "dout_cmu_hsi1_pcie",
		"gout_cmu_hsi1_pcie", 1, 8, 0),
	FFACTOR(CLK_DOUT_CMU_OTP, "dout_cmu_otp", "oscclk", 1, 8, 0),
	FFACTOR(CLK_DOUT_CMU_HSI0_USBDP_DEBUG, "dout_cmu_hsi0_usbdp_debug",
		"gout_cmu_hsi0_usbdp_debug", 1, 8, 0),
	FFACTOR(CLK_DOUT_CMU_HSI2_PCIE, "dout_cmu_hsi2_pcie",
		"gout_cmu_hsi2_pcie", 1, 8, 0),
};

static const struct samsung_gate_clock top_gate_clks[] __initconst = {
	GATE(CLK_GOUT_CMU_APM_BUS, "gout_cmu_apm_bus", "mout_cmu_apm_bus",
	     CLK_CON_GAT_GATE_CLKCMU_APM_BUS, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_CMU_AUD_CPU, "gout_cmu_aud_cpu", "mout_cmu_aud_cpu",
	     CLK_CON_GAT_GATE_CLKCMU_AUD_CPU, 21, 0, 0),
	GATE(CLK_GOUT_CMU_BUS0_BUS, "gout_cmu_bus0_bus", "mout_cmu_bus0_bus",
	     CLK_CON_GAT_GATE_CLKCMU_BUS0_BUS, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_CMU_BUS1_BUS, "gout_cmu_bus1_bus", "mout_cmu_bus1_bus",
	     CLK_CON_GAT_GATE_CLKCMU_BUS1_BUS, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_CMU_BUS1_SSS, "gout_cmu_bus1_sss", "mout_cmu_bus1_sss",
	     CLK_CON_GAT_GATE_CLKCMU_BUS1_SSS, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_CMU_CIS_CLK0, "gout_cmu_cis_clk0", "mout_cmu_cis_clk0",
	     CLK_CON_GAT_GATE_CLKCMU_CIS_CLK0, 21, 0, 0),
	GATE(CLK_GOUT_CMU_CIS_CLK1, "gout_cmu_cis_clk1", "mout_cmu_cis_clk1",
	     CLK_CON_GAT_GATE_CLKCMU_CIS_CLK1, 21, 0, 0),
	GATE(CLK_GOUT_CMU_CIS_CLK2, "gout_cmu_cis_clk2", "mout_cmu_cis_clk2",
	     CLK_CON_GAT_GATE_CLKCMU_CIS_CLK2, 21, 0, 0),
	GATE(CLK_GOUT_CMU_CIS_CLK3, "gout_cmu_cis_clk3", "mout_cmu_cis_clk3",
	     CLK_CON_GAT_GATE_CLKCMU_CIS_CLK3, 21, 0, 0),
	GATE(CLK_GOUT_CMU_CIS_CLK4, "gout_cmu_cis_clk4", "mout_cmu_cis_clk4",
	     CLK_CON_GAT_GATE_CLKCMU_CIS_CLK4, 21, 0, 0),
	GATE(CLK_GOUT_CMU_CIS_CLK5, "gout_cmu_cis_clk5", "mout_cmu_cis_clk5",
	     CLK_CON_GAT_GATE_CLKCMU_CIS_CLK5, 21, 0, 0),
	GATE(CLK_GOUT_CMU_CORE_BUS, "gout_cmu_core_bus", "mout_cmu_core_bus",
	     CLK_CON_GAT_GATE_CLKCMU_CORE_BUS, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_CMU_CPUCL0_DBG_BUS, "gout_cmu_cpucl0_dbg_bus",
	     "mout_cmu_cpucl0_dbg_bus", CLK_CON_GAT_GATE_CLKCMU_CPUCL0_DBG_BUS,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_CPUCL0_SWITCH, "gout_cmu_cpucl0_switch",
	     "mout_cmu_cpucl0_switch", CLK_CON_GAT_GATE_CLKCMU_CPUCL0_SWITCH,
	     21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_CMU_CPUCL1_SWITCH, "gout_cmu_cpucl1_switch",
	     "mout_cmu_cpucl1_switch", CLK_CON_GAT_GATE_CLKCMU_CPUCL1_SWITCH,
	     21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_CMU_CPUCL2_BUSP, "gout_cmu_cpucl2_busp",
	     "mout_cmu_cpucl2_busp", CLK_CON_GAT_GATE_CLKCMU_CPUCL2_BUSP,
	     21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_CMU_CPUCL2_SWITCH, "gout_cmu_cpucl2_switch",
	     "mout_cmu_cpucl2_switch", CLK_CON_GAT_GATE_CLKCMU_CPUCL2_SWITCH,
	     21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_CMU_CSIS_BUS, "gout_cmu_csis_bus", "mout_cmu_csis_bus",
	     CLK_CON_GAT_GATE_CLKCMU_CSIS_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CMU_CSIS_OIS_MCU, "gout_cmu_csis_ois_mcu",
	     "mout_cmu_csis_ois_mcu", CLK_CON_GAT_GATE_CLKCMU_CSIS_OIS_MCU,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_DNC_BUS, "gout_cmu_dnc_bus", "mout_cmu_dnc_bus",
	     CLK_CON_GAT_GATE_CLKCMU_DNC_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CMU_DNC_BUSM, "gout_cmu_dnc_busm", "mout_cmu_dnc_busm",
	     CLK_CON_GAT_GATE_CLKCMU_DNC_BUSM, 21, 0, 0),
	GATE(CLK_GOUT_CMU_DNS_BUS, "gout_cmu_dns_bus", "mout_cmu_dns_bus",
	     CLK_CON_GAT_GATE_CLKCMU_DNS_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CMU_DPU, "gout_cmu_dpu", "mout_cmu_dpu",
	     CLK_CON_GAT_GATE_CLKCMU_DPU, 21, 0, 0),
	GATE(CLK_GOUT_CMU_DPU_BUS, "gout_cmu_dpu_bus", "mout_cmu_dpu_alt",
	     CLK_CON_GAT_GATE_CLKCMU_DPU_BUS, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_CMU_DSP_BUS, "gout_cmu_dsp_bus", "mout_cmu_dsp_bus",
	     CLK_CON_GAT_GATE_CLKCMU_DSP_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CMU_G2D_G2D, "gout_cmu_g2d_g2d", "mout_cmu_g2d_g2d",
	     CLK_CON_GAT_GATE_CLKCMU_G2D_G2D, 21, 0, 0),
	GATE(CLK_GOUT_CMU_G2D_MSCL, "gout_cmu_g2d_mscl", "mout_cmu_g2d_mscl",
	     CLK_CON_GAT_GATE_CLKCMU_G2D_MSCL, 21, 0, 0),
	GATE(CLK_GOUT_CMU_G3D_SWITCH, "gout_cmu_g3d_switch",
	     "fout_shared2_pll", CLK_CON_GAT_GATE_CLKCMU_G3D_SWITCH,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_HPM, "gout_cmu_hpm", "mout_cmu_hpm",
	     CLK_CON_GAT_GATE_CLKCMU_HPM, 21, 0, 0),
	GATE(CLK_GOUT_CMU_HSI0_BUS, "gout_cmu_hsi0_bus",
	     "mout_cmu_hsi0_bus", CLK_CON_GAT_GATE_CLKCMU_HSI0_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CMU_HSI0_DPGTC, "gout_cmu_hsi0_dpgtc",
	     "mout_cmu_hsi0_dpgtc", CLK_CON_GAT_GATE_CLKCMU_HSI0_DPGTC,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_HSI0_USB31DRD, "gout_cmu_hsi0_usb31drd",
	     "mout_cmu_hsi0_usb31drd", CLK_CON_GAT_GATE_CLKCMU_HSI0_USB31DRD,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_HSI0_USBDP_DEBUG, "gout_cmu_hsi0_usbdp_debug",
	     "mout_cmu_hsi0_usbdp_debug", CLK_CON_GAT_GATE_CLKCMU_HSI0_USBDP_DEBUG,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_HSI1_BUS, "gout_cmu_hsi1_bus", "mout_cmu_hsi1_bus",
	     CLK_CON_GAT_GATE_CLKCMU_HSI1_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CMU_HSI1_MMC_CARD, "gout_cmu_hsi1_mmc_card",
	     "mout_cmu_hsi1_mmc_card", CLK_CON_GAT_GATE_CLKCMU_HSI1_MMC_CARD,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_HSI1_PCIE, "gout_cmu_hsi1_pcie",
	     "mout_cmu_hsi1_pcie", CLK_CON_GAT_GATE_CLKCMU_HSI1_PCIE,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_HSI1_UFS_CARD, "gout_cmu_hsi1_ufs_card",
	     "mout_cmu_hsi1_ufs_card", CLK_CON_GAT_GATE_CLKCMU_HSI1_UFS_CARD,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_HSI1_UFS_EMBD, "gout_cmu_hsi1_ufs_embd",
	     "mout_cmu_hsi1_ufs_embd", CLK_CON_GAT_GATE_CLKCMU_HSI1_UFS_EMBD,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_HSI2_BUS, "gout_cmu_hsi2_bus", "mout_cmu_hsi2_bus",
	     CLK_CON_GAT_GATE_CLKCMU_HSI2_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CMU_HSI2_PCIE, "gout_cmu_hsi2_pcie",
	     "mout_cmu_hsi2_pcie", CLK_CON_GAT_GATE_CLKCMU_HSI2_PCIE,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_IPP_BUS, "gout_cmu_ipp_bus", "mout_cmu_ipp_bus",
	     CLK_CON_GAT_GATE_CLKCMU_IPP_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CMU_ITP_BUS, "gout_cmu_itp_bus", "mout_cmu_itp_bus",
	     CLK_CON_GAT_GATE_CLKCMU_ITP_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CMU_MCSC_BUS, "gout_cmu_mcsc_bus", "mout_cmu_mcsc_bus",
	     CLK_CON_GAT_GATE_CLKCMU_MCSC_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CMU_MCSC_GDC, "gout_cmu_mcsc_gdc", "mout_cmu_mcsc_gdc",
	     CLK_CON_GAT_GATE_CLKCMU_MCSC_GDC, 21, 0, 0),
	GATE(CLK_GOUT_CMU_MFC0_MFC0, "gout_cmu_mfc0_mfc0",
	     "mout_cmu_mfc0_mfc0", CLK_CON_GAT_GATE_CLKCMU_MFC0_MFC0,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_MFC0_WFD, "gout_cmu_mfc0_wfd", "mout_cmu_mfc0_wfd",
	     CLK_CON_GAT_GATE_CLKCMU_MFC0_WFD, 21, 0, 0),
	GATE(CLK_GOUT_CMU_MIF_BUSP, "gout_cmu_mif_busp", "mout_cmu_mif_busp",
	     CLK_CON_GAT_GATE_CLKCMU_MIF_BUSP, 21, 0, 0),
	GATE(CLK_GOUT_CMU_NPU_BUS, "gout_cmu_npu_bus", "mout_cmu_npu_bus",
	     CLK_CON_GAT_GATE_CLKCMU_NPU_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CMU_PERIC0_BUS, "gout_cmu_peric0_bus",
	     "mout_cmu_peric0_bus", CLK_CON_GAT_GATE_CLKCMU_PERIC0_BUS,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_PERIC0_IP, "gout_cmu_peric0_ip",
	     "mout_cmu_peric0_ip", CLK_CON_GAT_GATE_CLKCMU_PERIC0_IP,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_PERIC1_BUS, "gout_cmu_peric1_bus",
	     "mout_cmu_peric1_bus", CLK_CON_GAT_GATE_CLKCMU_PERIC1_BUS,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_PERIC1_IP, "gout_cmu_peric1_ip",
	     "mout_cmu_peric1_ip", CLK_CON_GAT_GATE_CLKCMU_PERIC1_IP,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_PERIS_BUS, "gout_cmu_peris_bus",
	     "mout_cmu_peris_bus", CLK_CON_GAT_GATE_CLKCMU_PERIS_BUS,
	     21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_CMU_SSP_BUS, "gout_cmu_ssp_bus", "mout_cmu_ssp_bus",
	     CLK_CON_GAT_GATE_CLKCMU_SSP_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CMU_TNR_BUS, "gout_cmu_tnr_bus", "mout_cmu_tnr_bus",
	     CLK_CON_GAT_GATE_CLKCMU_TNR_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CMU_VRA_BUS, "gout_cmu_vra_bus", "mout_cmu_vra_bus",
	     CLK_CON_GAT_GATE_CLKCMU_VRA_BUS, 21, 0, 0),
};

static const struct samsung_cmu_info top_cmu_info __initconst = {
	.pll_clks = top_pll_clks,
	.nr_pll_clks = ARRAY_SIZE(top_pll_clks),
	.mux_clks = top_mux_clks,
	.nr_mux_clks = ARRAY_SIZE(top_mux_clks),
	.div_clks = top_div_clks,
	.nr_div_clks = ARRAY_SIZE(top_div_clks),
	.fixed_factor_clks = cmu_top_ffactor,
	.nr_fixed_factor_clks = ARRAY_SIZE(cmu_top_ffactor),
	.gate_clks = top_gate_clks,
	.nr_gate_clks = ARRAY_SIZE(top_gate_clks),
	.nr_clk_ids = CLKS_NR_TOP,
	.clk_regs = top_clk_regs,
	.nr_clk_regs = ARRAY_SIZE(top_clk_regs),
};

static void __init exynos990_cmu_top_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &top_cmu_info);
}

/* Register CMU_TOP early, as it's a dependency for other early domains */
CLK_OF_DECLARE(exynos990_cmu_top, "samsung,exynos990-cmu-top",
	       exynos990_cmu_top_init);

/* ---- CMU_HSI0 ------------------------------------------------------------ */

/* Register Offset definitions for CMU_HSI0 (0x10a00000) */
#define PLL_CON0_MUX_CLKCMU_HSI0_BUS_USER						0x0600
#define PLL_CON0_MUX_CLKCMU_HSI0_USB31DRD_USER						0x0620
#define PLL_CON0_MUX_CLKCMU_HSI0_USBDP_DEBUG_USER					0x0630
#define PLL_CON0_MUX_CLKCMU_HSI0_DPGTC_USER						0x0610
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_DP_LINK_IPCLKPORT_I_DP_GTC_CLK			0x2004
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_PPMU_HSI0_BUS1_IPCLKPORT_ACLK			0x2018
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_LHS_ACEL_D_HSI0_IPCLKPORT_I_CLK			0x2014
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_RSTNSYNC_CLK_HSI0_BUS_IPCLKPORT_CLK		0x2020
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_VGEN_LITE_HSI0_IPCLKPORT_CLK			0x2044
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_DP_LINK_IPCLKPORT_I_PCLK				0x2008
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_D_TZPC_HSI0_IPCLKPORT_PCLK			0x200c
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_LHM_AXI_P_HSI0_IPCLKPORT_I_CLK			0x2010
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_PPMU_HSI0_BUS1_IPCLKPORT_PCLK			0x201c
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_SYSMMU_USB_IPCLKPORT_CLK_S2			0x2024
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_SYSREG_HSI0_IPCLKPORT_PCLK			0x2028
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_ACLK_PHYCTRL			0x202c
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_I_USB31DRD_REF_CLK_40		0x2034
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_I_USBDPPHY_SCL_APB_PCLK	0x203c
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_I_USBPCS_APB_CLK		0x2040
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_BUS_CLK_EARLY			0x2030
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_HSI0_CMU_HSI0_IPCLKPORT_PCLK			0x2000
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_XIU_D_HSI0_IPCLKPORT_ACLK				0x2048
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_I_USBDPPHY_REF_SOC_PLL		0x2038

static const unsigned long hsi0_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_HSI0_BUS_USER,
	PLL_CON0_MUX_CLKCMU_HSI0_USB31DRD_USER,
	PLL_CON0_MUX_CLKCMU_HSI0_USBDP_DEBUG_USER,
	PLL_CON0_MUX_CLKCMU_HSI0_DPGTC_USER,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_DP_LINK_IPCLKPORT_I_DP_GTC_CLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_DP_LINK_IPCLKPORT_I_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_PPMU_HSI0_BUS1_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_LHS_ACEL_D_HSI0_IPCLKPORT_I_CLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_RSTNSYNC_CLK_HSI0_BUS_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_VGEN_LITE_HSI0_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_D_TZPC_HSI0_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_LHM_AXI_P_HSI0_IPCLKPORT_I_CLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_PPMU_HSI0_BUS1_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_SYSMMU_USB_IPCLKPORT_CLK_S2,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_SYSREG_HSI0_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_ACLK_PHYCTRL,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_I_USB31DRD_REF_CLK_40,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_I_USBDPPHY_REF_SOC_PLL,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_I_USBDPPHY_SCL_APB_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_I_USBPCS_APB_CLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_BUS_CLK_EARLY,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_HSI0_CMU_HSI0_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_XIU_D_HSI0_IPCLKPORT_ACLK,
};

/* Parent clock list for CMU_HSI0 muxes */
PNAME(mout_hsi0_bus_user_p)		= { "oscclk", "dout_cmu_hsi0_bus" };
PNAME(mout_hsi0_usb31drd_user_p)	= { "oscclk", "dout_cmu_hsi0_usb31drd" };
PNAME(mout_hsi0_usbdp_debug_user_p)	= { "oscclk",
					    "dout_cmu_hsi0_usbdp_debug" };
PNAME(mout_hsi0_dpgtc_user_p)		= { "oscclk", "dout_cmu_hsi0_dpgtc" };

static const struct samsung_mux_clock hsi0_mux_clks[] __initconst = {
	MUX(CLK_MOUT_HSI0_BUS_USER, "mout_hsi0_bus_user",
	    mout_hsi0_bus_user_p, PLL_CON0_MUX_CLKCMU_HSI0_BUS_USER,
	    4, 1),
	MUX(CLK_MOUT_HSI0_USB31DRD_USER, "mout_hsi0_usb31drd_user",
	    mout_hsi0_usb31drd_user_p, PLL_CON0_MUX_CLKCMU_HSI0_USB31DRD_USER,
	    4, 1),
	MUX(CLK_MOUT_HSI0_USBDP_DEBUG_USER, "mout_hsi0_usbdp_debug_user",
	    mout_hsi0_usbdp_debug_user_p,
	    PLL_CON0_MUX_CLKCMU_HSI0_USBDP_DEBUG_USER,
	    4, 1),
	MUX(CLK_MOUT_HSI0_DPGTC_USER, "mout_hsi0_dpgtc_user",
	    mout_hsi0_dpgtc_user_p, PLL_CON0_MUX_CLKCMU_HSI0_DPGTC_USER,
	    4, 1),
};

static const struct samsung_gate_clock hsi0_gate_clks[] __initconst = {
	GATE(CLK_GOUT_HSI0_DP_LINK_DP_GTC_CLK,
	     "gout_hsi0_dp_link_dp_gtc_clk", "mout_hsi0_dpgtc_user",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_DP_LINK_IPCLKPORT_I_DP_GTC_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_DP_LINK_PCLK,
	     "gout_hsi0_dp_link_pclk", "mout_hsi0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_DP_LINK_IPCLKPORT_I_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_D_TZPC_HSI0_PCLK,
	     "gout_hsi0_d_tzpc_hsi0_pclk", "mout_hsi0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_D_TZPC_HSI0_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_LHM_AXI_P_HSI0_CLK,
	     "gout_hsi0_lhm_axi_p_hsi0_clk", "mout_hsi0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_LHM_AXI_P_HSI0_IPCLKPORT_I_CLK,
	     21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_HSI0_PPMU_HSI0_BUS1_ACLK,
	     "gout_hsi0_ppmu_hsi0_bus1_aclk", "mout_hsi0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_PPMU_HSI0_BUS1_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_PPMU_HSI0_BUS1_PCLK,
	     "gout_hsi0_ppmu_hsi0_bus1_pclk", "mout_hsi0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_PPMU_HSI0_BUS1_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_CLK_HSI0_BUS_CLK,
	     "gout_hsi0_clk_hsi0_bus_clk", "mout_hsi0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_RSTNSYNC_CLK_HSI0_BUS_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_SYSMMU_USB_CLK_S2,
	     "gout_hsi0_sysmmu_usb_clk_s2", "mout_hsi0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_SYSMMU_USB_IPCLKPORT_CLK_S2,
	     21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_HSI0_SYSREG_HSI0_PCLK,
	     "gout_hsi0_sysreg_hsi0_pclk", "mout_hsi0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_SYSREG_HSI0_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_USB31DRD_ACLK_PHYCTRL,
	     "gout_hsi0_usb31drd_aclk_phyctrl", "mout_hsi0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_ACLK_PHYCTRL,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_USB31DRD_BUS_CLK_EARLY,
	     "gout_hsi0_usb31drd_bus_clk_early",
	     "mout_hsi0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_BUS_CLK_EARLY,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_USB31DRD_USB31DRD_REF_CLK_40,
	     "gout_hsi0_usb31drd_usb31drd_ref_clk_40",
	     "mout_hsi0_usb31drd_user",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_I_USB31DRD_REF_CLK_40,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_USB31DRD_USBDPPHY_REF_SOC_PLL,
	     "gout_hsi0_usb31drd_usbdpphy_ref_soc_pll",
	     "mout_hsi0_usbdp_debug_user",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_I_USBDPPHY_REF_SOC_PLL,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_USB31DRD_USBDPPHY_SCL_APB,
	     "gout_hsi0_usb31drd_ipclkport_i_usbdpphy_scl_apb_pclk",
	     "mout_hsi0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_I_USBDPPHY_SCL_APB_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_USB31DRD_USBPCS_APB_CLK,
	     "gout_hsi0_usb31drd_usbpcs_apb_clk",
	     "mout_hsi0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_I_USBPCS_APB_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_VGEN_LITE_HSI0_CLK,
	     "gout_hsi0_vgen_lite_ipclkport_clk", "mout_hsi0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_VGEN_LITE_HSI0_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_CMU_HSI0_PCLK,
	     "gout_hsi0_cmu_hsi0_pclk", "mout_hsi0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_HSI0_CMU_HSI0_IPCLKPORT_PCLK,
	     21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_HSI0_XIU_D_HSI0_ACLK,
	     "gout_hsi0_xiu_d_hsi0_aclk", "mout_hsi0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_XIU_D_HSI0_IPCLKPORT_ACLK,
	     21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_HSI0_LHS_ACEL_D_HSI0_CLK,
	     "gout_hsi0_lhs_acel_d_hsi0_clk", "mout_hsi0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_LHS_ACEL_D_HSI0_IPCLKPORT_I_CLK,
	     21, CLK_IS_CRITICAL, 0),
};

static const struct samsung_cmu_info hsi0_cmu_info __initconst = {
	.mux_clks = hsi0_mux_clks,
	.nr_mux_clks = ARRAY_SIZE(hsi0_mux_clks),
	.gate_clks = hsi0_gate_clks,
	.nr_gate_clks = ARRAY_SIZE(hsi0_gate_clks),
	.nr_clk_ids = CLKS_NR_HSI0,
	.clk_regs = hsi0_clk_regs,
	.nr_clk_regs = ARRAY_SIZE(hsi0_clk_regs),
	.clk_name		= "bus",
};

/* ---- CMU_PERIC0 --------------------------------------------------------- */

/* Register Offset definitions for CMU_PERIC0 (0x10400000) */
#define PLL_CON0_MUX_CLKCMU_PERIC0_BUS_USER						0x0600
#define PLL_CON1_MUX_CLKCMU_PERIC0_BUS_USER						0x0604
#define PLL_CON0_MUX_CLKCMU_PERIC0_UART_DBG						0x0610
#define PLL_CON1_MUX_CLKCMU_PERIC0_UART_DBG						0x0614
#define PLL_CON0_MUX_CLKCMU_PERIC0_USI00_USI_USER					0x0620
#define PLL_CON1_MUX_CLKCMU_PERIC0_USI00_USI_USER					0x0624
#define PLL_CON0_MUX_CLKCMU_PERIC0_USI01_USI_USER					0x0630
#define PLL_CON1_MUX_CLKCMU_PERIC0_USI01_USI_USER					0x0634
#define PLL_CON0_MUX_CLKCMU_PERIC0_USI02_USI_USER					0x0640
#define PLL_CON1_MUX_CLKCMU_PERIC0_USI02_USI_USER					0x0644
#define PLL_CON0_MUX_CLKCMU_PERIC0_USI03_USI_USER					0x0650
#define PLL_CON1_MUX_CLKCMU_PERIC0_USI03_USI_USER					0x0654
#define PLL_CON0_MUX_CLKCMU_PERIC0_USI04_USI_USER					0x0660
#define PLL_CON1_MUX_CLKCMU_PERIC0_USI04_USI_USER					0x0664
#define PLL_CON0_MUX_CLKCMU_PERIC0_USI05_USI_USER					0x0670
#define PLL_CON1_MUX_CLKCMU_PERIC0_USI05_USI_USER					0x0674
#define PLL_CON0_MUX_CLKCMU_PERIC0_USI13_USI_USER					0x0680
#define PLL_CON1_MUX_CLKCMU_PERIC0_USI13_USI_USER					0x0684
#define PLL_CON0_MUX_CLKCMU_PERIC0_USI14_USI_USER					0x0690
#define PLL_CON1_MUX_CLKCMU_PERIC0_USI14_USI_USER					0x0694
#define PLL_CON0_MUX_CLKCMU_PERIC0_USI15_USI_USER					0x06a0
#define PLL_CON1_MUX_CLKCMU_PERIC0_USI15_USI_USER					0x06a4
#define PLL_CON0_MUX_CLKCMU_PERIC0_USI_I2C_USER						0x06b0
#define PLL_CON1_MUX_CLKCMU_PERIC0_USI_I2C_USER						0x06b4
#define CLK_CON_DIV_DIV_CLK_PERIC0_UART_DBG						0x1800
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI00_USI						0x1804
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI01_USI						0x1808
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI02_USI						0x180c
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI03_USI						0x1810
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI04_USI						0x1814
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI05_USI						0x1818
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI13_USI						0x181c
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI14_USI						0x1820
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI15_USI						0x1824
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI_I2C						0x1828
#define CLK_CON_GAT_CLK_BLK_PERIC0_UID_PERIC0_CMU_PERIC0_IPCLKPORT_PCLK			0x2004
#define CLK_CON_GAT_CLK_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_OSCCLK_IPCLKPORT_CLK		0x2008
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_D_TZPC_PERIC0_IPCLKPORT_PCLK			0x200c
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_GPIO_PERIC0_IPCLKPORT_PCLK			0x2010
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_LHM_AXI_P_PERIC0_IPCLKPORT_I_CLK		0x2014
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_10			0x2018
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_11			0x201c
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_12			0x2020
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_13			0x2024
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_14			0x2028
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_15			0x202c
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_4			0x2030
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_5			0x2034
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_6			0x2038
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_7			0x203c
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_8			0x2040
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_9			0x2044
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_10			0x2048
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_11			0x204c
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_12			0x2050
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_13			0x2054
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_14			0x2058
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_15			0x205c
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_4			0x2060
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_5			0x2064
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_6			0x2068
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_7			0x206c
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_8			0x2070
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_9			0x2074
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_IPCLK_0			0x2078
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_IPCLK_3			0x207c
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_IPCLK_4			0x2080
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_IPCLK_5			0x2084
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_IPCLK_6			0x2088
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_IPCLK_7			0x208c
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_IPCLK_8			0x2090
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_PCLK_0			0x2094
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_PCLK_15			0x2098
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_PCLK_3			0x209c
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_PCLK_4			0x20a0
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_PCLK_5			0x20a4
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_PCLK_6			0x20a8
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_PCLK_7			0x20ac
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_PCLK_8			0x20b0
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_BUSP_IPCLKPORT_CLK		0x20b4
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_UART_DBG_IPCLKPORT_CLK	0x20b8
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI00_USI_IPCLKPORT_CLK	0x20bc
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI01_USI_IPCLKPORT_CLK	0x20c0
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI02_USI_IPCLKPORT_CLK	0x20c4
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI03_USI_IPCLKPORT_CLK	0x20c8
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI04_USI_IPCLKPORT_CLK	0x20cc
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI05_USI_IPCLKPORT_CLK	0x20d0
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI13_USI_IPCLKPORT_CLK	0x20d4
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI14_USI_IPCLKPORT_CLK	0x20d8
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI15_USI_IPCLKPORT_CLK	0x20dc
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI_I2C_IPCLKPORT_CLK	0x20e0
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_SYSREG_PERIC0_IPCLKPORT_PCLK			0x20e4

static const unsigned long peric0_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_PERIC0_BUS_USER,
	PLL_CON1_MUX_CLKCMU_PERIC0_BUS_USER,
	PLL_CON0_MUX_CLKCMU_PERIC0_UART_DBG,
	PLL_CON1_MUX_CLKCMU_PERIC0_UART_DBG,
	PLL_CON0_MUX_CLKCMU_PERIC0_USI00_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC0_USI00_USI_USER,
	PLL_CON0_MUX_CLKCMU_PERIC0_USI01_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC0_USI01_USI_USER,
	PLL_CON0_MUX_CLKCMU_PERIC0_USI02_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC0_USI02_USI_USER,
	PLL_CON0_MUX_CLKCMU_PERIC0_USI03_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC0_USI03_USI_USER,
	PLL_CON0_MUX_CLKCMU_PERIC0_USI04_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC0_USI04_USI_USER,
	PLL_CON0_MUX_CLKCMU_PERIC0_USI05_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC0_USI05_USI_USER,
	PLL_CON0_MUX_CLKCMU_PERIC0_USI13_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC0_USI13_USI_USER,
	PLL_CON0_MUX_CLKCMU_PERIC0_USI14_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC0_USI14_USI_USER,
	PLL_CON0_MUX_CLKCMU_PERIC0_USI15_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC0_USI15_USI_USER,
	PLL_CON0_MUX_CLKCMU_PERIC0_USI_I2C_USER,
	PLL_CON1_MUX_CLKCMU_PERIC0_USI_I2C_USER,
	CLK_CON_DIV_DIV_CLK_PERIC0_UART_DBG,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI00_USI,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI01_USI,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI02_USI,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI03_USI,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI04_USI,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI05_USI,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI13_USI,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI14_USI,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI15_USI,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI_I2C,
	CLK_CON_GAT_CLK_BLK_PERIC0_UID_PERIC0_CMU_PERIC0_IPCLKPORT_PCLK,
	CLK_CON_GAT_CLK_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_OSCCLK_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_D_TZPC_PERIC0_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_GPIO_PERIC0_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_LHM_AXI_P_PERIC0_IPCLKPORT_I_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_10,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_11,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_12,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_13,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_14,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_15,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_4,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_5,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_6,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_7,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_8,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_9,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_10,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_11,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_12,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_13,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_14,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_15,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_4,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_5,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_6,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_7,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_8,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_9,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_IPCLK_0,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_IPCLK_3,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_IPCLK_4,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_IPCLK_5,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_IPCLK_6,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_IPCLK_7,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_IPCLK_8,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_PCLK_0,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_PCLK_15,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_PCLK_3,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_PCLK_4,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_PCLK_5,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_PCLK_6,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_PCLK_7,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_PCLK_8,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_BUSP_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_UART_DBG_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI00_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI01_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI02_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI03_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI04_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI05_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI13_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI14_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI15_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI_I2C_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_SYSREG_PERIC0_IPCLKPORT_PCLK,
};

/* Parent clock list for CMU_PERIC0 muxes */
PNAME(mout_peric0_bus_user_p) = { "oscclk", "dout_cmu_peric0_bus" };
PNAME(mout_peric0_uart_dbg_p) = { "oscclk", "dout_cmu_peric0_ip" };
PNAME(mout_peric0_usi00_user_p) = { "oscclk", "dout_cmu_peric0_ip" };
PNAME(mout_peric0_usi01_user_p) = { "oscclk", "dout_cmu_peric0_ip" };
PNAME(mout_peric0_usi02_user_p) = { "oscclk", "dout_cmu_peric0_ip" };
PNAME(mout_peric0_usi03_user_p) = { "oscclk", "dout_cmu_peric0_ip" };
PNAME(mout_peric0_usi04_user_p) = { "oscclk", "dout_cmu_peric0_ip" };
PNAME(mout_peric0_usi05_user_p) = { "oscclk", "dout_cmu_peric0_ip" };
PNAME(mout_peric0_usi13_user_p) = { "oscclk", "dout_cmu_peric0_ip" };
PNAME(mout_peric0_usi14_user_p) = { "oscclk", "dout_cmu_peric0_ip" };
PNAME(mout_peric0_usi15_user_p) = { "oscclk", "dout_cmu_peric0_ip" };
PNAME(mout_peric0_usi_i2c_user_p) = { "oscclk", "dout_cmu_peric0_ip" };

static const struct samsung_mux_clock peric0_mux_clks[] __initconst = {
	MUX(CLK_MOUT_PERIC0_BUS_USER, "mout_peric0_bus_user",
	    mout_peric0_bus_user_p, PLL_CON0_MUX_CLKCMU_PERIC0_BUS_USER,
	    4, 1),
	MUX(CLK_MOUT_PERIC0_UART_DBG, "mout_peric0_uart_dbg",
	    mout_peric0_uart_dbg_p, PLL_CON0_MUX_CLKCMU_PERIC0_UART_DBG,
	    4, 1),
	MUX(CLK_MOUT_PERIC0_USI00_USI_USER, "mout_peric0_usi00_usi_user",
	    mout_peric0_usi00_user_p, PLL_CON0_MUX_CLKCMU_PERIC0_USI00_USI_USER,
	    4, 1),
	MUX(CLK_MOUT_PERIC0_USI01_USI_USER, "mout_peric0_usi01_usi_user",
	    mout_peric0_usi01_user_p, PLL_CON0_MUX_CLKCMU_PERIC0_USI01_USI_USER,
	    4, 1),
	MUX(CLK_MOUT_PERIC0_USI02_USI_USER, "mout_peric0_usi02_usi_user",
	    mout_peric0_usi02_user_p, PLL_CON0_MUX_CLKCMU_PERIC0_USI02_USI_USER,
	    4, 1),
	MUX(CLK_MOUT_PERIC0_USI03_USI_USER, "mout_peric0_usi03_usi_user",
	    mout_peric0_usi03_user_p, PLL_CON0_MUX_CLKCMU_PERIC0_USI03_USI_USER,
	    4, 1),
	MUX(CLK_MOUT_PERIC0_USI04_USI_USER, "mout_peric0_usi04_usi_user",
	    mout_peric0_usi04_user_p, PLL_CON0_MUX_CLKCMU_PERIC0_USI04_USI_USER,
	    4, 1),
	MUX(CLK_MOUT_PERIC0_USI05_USI_USER, "mout_peric0_usi05_usi_user",
	    mout_peric0_usi05_user_p, PLL_CON0_MUX_CLKCMU_PERIC0_USI05_USI_USER,
	    4, 1),
	MUX(CLK_MOUT_PERIC0_USI13_USI_USER, "mout_peric0_usi13_usi_user",
	    mout_peric0_usi13_user_p, PLL_CON0_MUX_CLKCMU_PERIC0_USI13_USI_USER,
	    4, 1),
	MUX(CLK_MOUT_PERIC0_USI14_USI_USER, "mout_peric0_usi14_usi_user",
	    mout_peric0_usi14_user_p, PLL_CON0_MUX_CLKCMU_PERIC0_USI14_USI_USER,
	    4, 1),
	MUX(CLK_MOUT_PERIC0_USI15_USI_USER, "mout_peric0_usi15_usi_user",
	    mout_peric0_usi15_user_p, PLL_CON0_MUX_CLKCMU_PERIC0_USI15_USI_USER,
	    4, 1),
	MUX(CLK_MOUT_PERIC0_USI_I2C_USER, "mout_peric0_usi_i2c_user",
	    mout_peric0_usi_i2c_user_p, PLL_CON0_MUX_CLKCMU_PERIC0_USI_I2C_USER,
	    4, 1),
};

static const struct samsung_div_clock peric0_div_clks[] __initconst = {
	DIV(CLK_DOUT_PERIC0_UART_DBG, "dout_peric0_uart_dbg",
	    "mout_peric0_uart_dbg",
	    CLK_CON_DIV_DIV_CLK_PERIC0_UART_DBG,
	    0, 4),
	DIV(CLK_DOUT_PERIC0_USI00_USI, "dout_peric0_usi00_usi",
	    "mout_peric0_usi00_usi_user",
	    CLK_CON_DIV_DIV_CLK_PERIC0_USI00_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC0_USI01_USI, "dout_peric0_usi01_usi",
	    "mout_peric0_usi01_usi_user",
	    CLK_CON_DIV_DIV_CLK_PERIC0_USI01_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC0_USI02_USI, "dout_peric0_usi02_usi",
	    "mout_peric0_usi02_usi_user",
	    CLK_CON_DIV_DIV_CLK_PERIC0_USI02_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC0_USI03_USI, "dout_peric0_usi03_usi",
	    "mout_peric0_usi03_usi_user",
	    CLK_CON_DIV_DIV_CLK_PERIC0_USI03_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC0_USI04_USI, "dout_peric0_usi04_usi",
	    "mout_peric0_usi04_usi_user",
	    CLK_CON_DIV_DIV_CLK_PERIC0_USI04_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC0_USI05_USI, "dout_peric0_usi05_usi",
	    "mout_peric0_usi05_usi_user",
	    CLK_CON_DIV_DIV_CLK_PERIC0_USI05_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC0_USI13_USI, "dout_peric0_usi13_usi",
	    "mout_peric0_usi13_usi_user",
	    CLK_CON_DIV_DIV_CLK_PERIC0_USI13_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC0_USI14_USI, "dout_peric0_usi14_usi",
	    "mout_peric0_usi14_usi_user",
	    CLK_CON_DIV_DIV_CLK_PERIC0_USI14_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC0_USI15_USI, "dout_peric0_usi15_usi",
	    "mout_peric0_usi15_usi_user",
	    CLK_CON_DIV_DIV_CLK_PERIC0_USI15_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC0_USI_I2C, "dout_peric0_usi_i2c",
	    "mout_peric0_usi_i2c_user",
	    CLK_CON_DIV_DIV_CLK_PERIC0_USI_I2C,
	    0, 4),
};

static const struct samsung_gate_clock peric0_gate_clks[] __initconst = {
	GATE(CLK_GOUT_PERIC0_CMU_PCLK, "gout_peric0_cmu_pclk",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_CLK_BLK_PERIC0_UID_PERIC0_CMU_PERIC0_IPCLKPORT_PCLK,
	     21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_PERIC0_OSCCLK_CLK, "gout_peric0_oscclk_clk",
	     "oscclk",
	     CLK_CON_GAT_CLK_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_OSCCLK_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_D_TZPC_PCLK, "gout_peric0_d_tpzc_pclk",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_D_TZPC_PERIC0_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_GPIO_PCLK, "gout_peric0_gpio_pclk",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_GPIO_PERIC0_IPCLKPORT_PCLK,
	     21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_PERIC0_LHM_AXI_P_CLK, "gout_peric0_lhm_axi_p_clk",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_LHM_AXI_P_PERIC0_IPCLKPORT_I_CLK,
	     21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_PERIC0_TOP0_IPCLK_10, "gout_peric0_top0_ipclk_10",
	     "dout_peric0_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_10,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP0_IPCLK_11, "gout_peric0_top0_ipclk_11",
	     "dout_peric0_usi03_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_11,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP0_IPCLK_12, "gout_peric0_top0_ipclk_12",
	     "dout_peric0_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_12,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP0_IPCLK_13, "gout_peric0_top0_ipclk_13",
	     "dout_peric0_usi04_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_13,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP0_IPCLK_14, "gout_peric0_top0_ipclk_14",
	     "dout_peric0_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_14,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP0_IPCLK_15, "gout_peric0_top0_ipclk_15",
	     "dout_peric0_usi05_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_15,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP0_IPCLK_4, "gout_peric0_top0_ipclk_4",
	     "dout_peric0_uart_dbg",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_4,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP0_IPCLK_5, "gout_peric0_top0_ipclk_5",
	     "dout_peric0_usi00_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_5,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP0_IPCLK_6, "gout_peric0_top0_ipclk_6",
	     "dout_peric0_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_6,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP0_IPCLK_7, "gout_peric0_top0_ipclk_7",
	     "dout_peric0_usi01_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_7,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP0_IPCLK_8, "gout_peric0_top0_ipclk_8",
	     "dout_peric0_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_8,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP0_IPCLK_9, "gout_peric0_top0_ipclk_9",
	     "dout_peric0_usi02_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_9,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP0_PCLK_10, "gout_peric0_top0_pclk_10",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_10,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP0_PCLK_11, "gout_peric0_top0_pclk_11",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_11,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP0_PCLK_12, "gout_peric0_top0_pclk_12",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_12,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP0_PCLK_13, "gout_peric0_top0_pclk_13",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_13,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP0_PCLK_14, "gout_peric0_top0_pclk_14",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_14,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP0_PCLK_15, "gout_peric0_top0_pclk_15",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_15,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP0_PCLK_4, "gout_peric0_top0_pclk_4",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_4,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP0_PCLK_5, "gout_peric0_top0_pclk_5",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_5,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP0_PCLK_6, "gout_peric0_top0_pclk_6",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_6,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP0_PCLK_7, "gout_peric0_top0_pclk_7",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_7,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP0_PCLK_8, "gout_peric0_top0_pclk_8",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_8,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP0_PCLK_9, "gout_peric0_top0_pclk_9",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_9,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP1_IPCLK_0, "gout_peric0_top1_ipclk_0",
	     "dout_peric0_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_IPCLK_0,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP1_IPCLK_3, "gout_peric0_top1_ipclk_3",
	     "dout_peric0_usi13_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_IPCLK_3,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP1_IPCLK_4, "gout_peric0_top1_ipclk_4",
	     "dout_peric0_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_IPCLK_4,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP1_IPCLK_5, "gout_peric0_top1_ipclk_5",
	     "dout_peric0_usi14_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_IPCLK_5,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP1_IPCLK_6, "gout_peric0_top1_ipclk_6",
	     "dout_peric0_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_IPCLK_6,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP1_IPCLK_7, "gout_peric0_top1_ipclk_7",
	     "dout_peric0_usi15_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_IPCLK_7,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP1_IPCLK_8, "gout_peric0_top1_ipclk_8",
	     "dout_peric0_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_IPCLK_8,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP1_PCLK_0, "gout_peric0_top1_pclk_0",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_PCLK_0,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP1_PCLK_15, "gout_peric0_top1_pclk_15",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_PCLK_15,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP1_PCLK_3, "gout_peric0_top1_pclk_3",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_PCLK_3,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP1_PCLK_4, "gout_peric0_top1_pclk_4",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_PCLK_4,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP1_PCLK_5, "gout_peric0_top1_pclk_5",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_PCLK_5,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP1_PCLK_6, "gout_peric0_top1_pclk_6",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_PCLK_6,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP1_PCLK_7, "gout_peric0_top1_pclk_7",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_PCLK_7,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_TOP1_PCLK_8, "gout_peric0_top1_pclk_8",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_PCLK_8,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_BUSP_CLK, "gout_peric0_busp_clk",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_BUSP_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_UART_DBG_CLK, "gout_peric0_uart_dbg_clk",
	     "dout_peric0_uart_dbg",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_UART_DBG_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI00_USI_CLK, "gout_peric0_usi00_usi_clk",
	     "dout_peric0_usi00_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI00_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI01_USI_CLK, "gout_peric0_usi01_usi_clk",
	     "dout_peric0_usi01_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI01_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI02_USI_CLK, "gout_peric0_usi02_usi_clk",
	     "dout_peric0_usi02_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI02_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI03_USI_CLK, "gout_peric0_usi03_usi_clk",
	     "dout_peric0_usi03_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI03_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI04_USI_CLK, "gout_peric0_usi04_usi_clk",
	     "dout_peric0_usi04_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI04_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI05_USI_CLK, "gout_peric0_usi05_usi_clk",
	     "dout_peric0_usi05_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI05_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI13_USI_CLK, "gout_peric0_usi13_usi_clk",
	     "dout_peric0_usi13_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI13_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI14_USI_CLK, "gout_peric0_usi14_usi_clk",
	     "dout_peric0_usi14_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI14_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI15_USI_CLK, "gout_peric0_usi15_usi_clk",
	     "dout_peric0_usi15_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI15_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_USI_I2C_CLK, "gout_peric0_usi_i2c_clk",
	     "dout_peric0_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI_I2C_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_SYSREG_PCLK, "gout_peric0_sysreg_pclk",
	     "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_SYSREG_PERIC0_IPCLKPORT_PCLK,
	     21, 0, 0)
};

static const struct samsung_cmu_info peric0_cmu_info __initconst = {
	.mux_clks	= peric0_mux_clks,
	.nr_mux_clks	= ARRAY_SIZE(peric0_mux_clks),
	.div_clks	= peric0_div_clks,
	.nr_div_clks	= ARRAY_SIZE(peric0_div_clks),
	.gate_clks	= peric0_gate_clks,
	.nr_gate_clks	= ARRAY_SIZE(peric0_gate_clks),
	.nr_clk_ids	= CLKS_NR_PERIC0,
	.clk_regs	= peric0_clk_regs,
	.nr_clk_regs	= ARRAY_SIZE(peric0_clk_regs),
	.clk_name	= "bus",
};

/* ---- CMU_PERIC1 --------------------------------------------------------- */

/* Register Offset definitions for CMU_PERIC1 (0x10700000) */
#define PLL_CON0_MUX_CLKCMU_PERIC1_BUS_USER						0x0600
#define PLL_CON1_MUX_CLKCMU_PERIC1_BUS_USER						0x0604
#define PLL_CON0_MUX_CLKCMU_PERIC1_UART_BT_USER						0x0610
#define PLL_CON1_MUX_CLKCMU_PERIC1_UART_BT_USER						0x0614
#define PLL_CON0_MUX_CLKCMU_PERIC1_USI06_USI_USER					0x0620
#define PLL_CON1_MUX_CLKCMU_PERIC1_USI06_USI_USER					0x0624
#define PLL_CON0_MUX_CLKCMU_PERIC1_USI07_USI_USER					0x0630
#define PLL_CON1_MUX_CLKCMU_PERIC1_USI07_USI_USER					0x0634
#define PLL_CON0_MUX_CLKCMU_PERIC1_USI08_USI_USER					0x0640
#define PLL_CON1_MUX_CLKCMU_PERIC1_USI08_USI_USER					0x0644
#define PLL_CON0_MUX_CLKCMU_PERIC1_USI09_USI_USER					0x0650
#define PLL_CON1_MUX_CLKCMU_PERIC1_USI09_USI_USER					0x0654
#define PLL_CON0_MUX_CLKCMU_PERIC1_USI10_USI_USER					0x0660
#define PLL_CON1_MUX_CLKCMU_PERIC1_USI10_USI_USER					0x0664
#define PLL_CON0_MUX_CLKCMU_PERIC1_USI11_USI_USER					0x0670
#define PLL_CON1_MUX_CLKCMU_PERIC1_USI11_USI_USER					0x0674
#define PLL_CON0_MUX_CLKCMU_PERIC1_USI12_USI_USER					0x0680
#define PLL_CON1_MUX_CLKCMU_PERIC1_USI12_USI_USER					0x0684
#define PLL_CON0_MUX_CLKCMU_PERIC1_USI16_USI_USER					0x0690
#define PLL_CON1_MUX_CLKCMU_PERIC1_USI16_USI_USER					0x0694
#define PLL_CON0_MUX_CLKCMU_PERIC1_USI17_USI_USER					0x06a0
#define PLL_CON1_MUX_CLKCMU_PERIC1_USI17_USI_USER					0x06a4
#define PLL_CON0_MUX_CLKCMU_PERIC1_USI18_USI_USER					0x06b0
#define PLL_CON1_MUX_CLKCMU_PERIC1_USI18_USI_USER					0x06b4
#define PLL_CON0_MUX_CLKCMU_PERIC1_USI_I2C_USER						0x06c0
#define PLL_CON1_MUX_CLKCMU_PERIC1_USI_I2C_USER						0x06c4
#define CLK_CON_DIV_DIV_CLK_PERIC1_UART_BT						0x1800
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI06_USI						0x1804
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI07_USI						0x1808
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI08_USI						0x180c
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI09_USI						0x1810
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI10_USI						0x1814
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI11_USI						0x1818
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI12_USI						0x181c
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI16_USI						0x1820
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI17_USI						0x1824
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI18_USI						0x1828
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI_I2C						0x182c
#define CLK_CON_GAT_CLK_BLK_PERIC1_UID_PERIC1_CMU_PERIC1_IPCLKPORT_PCLK			0x2004
#define CLK_CON_GAT_CLK_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_UART_BT_IPCLKPORT_CLK	0x2008
#define CLK_CON_GAT_CLK_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI12_USI_IPCLKPORT_CLK	0x200c
#define CLK_CON_GAT_CLK_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI18_USI_IPCLKPORT_CLK	0x2010
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_D_TZPC_PERIC1_IPCLKPORT_PCLK			0x2014
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_GPIO_PERIC1_IPCLKPORT_PCLK			0x2018
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_LHM_AXI_P_CSISPERIC1_IPCLKPORT_I_CLK		0x201c
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_LHM_AXI_P_PERIC1_IPCLKPORT_I_CLK		0x2020
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_10			0x2024
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_11			0x2028
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_12			0x202c
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_13			0x2030
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_14			0x2034
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_15			0x2038
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_4			0x203c
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_10			0x2040
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_11			0x2044
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_12			0x2048
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_13			0x204c
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_14			0x2050
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_15			0x2054
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_4			0x2058
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_0			0x205c
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_1			0x2060
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_10			0x2064
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_12			0x206c
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_13			0x2070
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_14			0x2074
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_15			0x2078
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_2			0x207c
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_3			0x2080
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_4			0x2084
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_5			0x2088
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_6			0x208c
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_7			0x2090
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_9			0x2098
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_0			0x209c
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_1			0x20a0
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_10			0x20a4
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_12			0x20ac
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_13			0x20b0
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_14			0x20b4
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_15			0x20b8
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_2			0x20bc
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_3			0x20c0
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_4			0x20c4
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_5			0x20c8
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_6			0x20cc
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_7			0x20d0
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_9			0x20d8
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_BUSP_IPCLKPORT_CLK		0x20dc
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_OSCCLK_IPCLKPORT_CLK	0x20e0
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI06_USI_IPCLKPORT_CLK	0x20e4
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI07_USI_IPCLKPORT_CLK	0x20e8
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI08_USI_IPCLKPORT_CLK	0x20ec
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI09_USI_IPCLKPORT_CLK	0x20f0
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI10_USI_IPCLKPORT_CLK	0x20f4
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI11_USI_IPCLKPORT_CLK	0x20f8
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI16_USI_IPCLKPORT_CLK	0x20fc
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI17_USI_IPCLKPORT_CLK	0x2100
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI_I2C_IPCLKPORT_CLK	0x2104
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_SYSREG_PERIC1_IPCLKPORT_PCLK			0x2108
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_USI16_I3C_IPCLKPORT_I_PCLK			0x210c
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_USI16_I3C_IPCLKPORT_I_SCLK			0x2110
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_USI17_I3C_IPCLKPORT_I_PCLK			0x2114
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_USI17_I3C_IPCLKPORT_I_SCLK			0x2118
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_XIU_P_PERIC1_IPCLKPORT_ACLK			0x211c

static const unsigned long peric1_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_PERIC1_BUS_USER,
	PLL_CON1_MUX_CLKCMU_PERIC1_BUS_USER,
	PLL_CON0_MUX_CLKCMU_PERIC1_UART_BT_USER,
	PLL_CON1_MUX_CLKCMU_PERIC1_UART_BT_USER,
	PLL_CON0_MUX_CLKCMU_PERIC1_USI06_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC1_USI06_USI_USER,
	PLL_CON0_MUX_CLKCMU_PERIC1_USI07_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC1_USI07_USI_USER,
	PLL_CON0_MUX_CLKCMU_PERIC1_USI08_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC1_USI08_USI_USER,
	PLL_CON0_MUX_CLKCMU_PERIC1_USI09_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC1_USI09_USI_USER,
	PLL_CON0_MUX_CLKCMU_PERIC1_USI10_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC1_USI10_USI_USER,
	PLL_CON0_MUX_CLKCMU_PERIC1_USI11_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC1_USI11_USI_USER,
	PLL_CON0_MUX_CLKCMU_PERIC1_USI12_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC1_USI12_USI_USER,
	PLL_CON0_MUX_CLKCMU_PERIC1_USI16_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC1_USI16_USI_USER,
	PLL_CON0_MUX_CLKCMU_PERIC1_USI17_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC1_USI17_USI_USER,
	PLL_CON0_MUX_CLKCMU_PERIC1_USI18_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC1_USI18_USI_USER,
	PLL_CON0_MUX_CLKCMU_PERIC1_USI_I2C_USER,
	PLL_CON1_MUX_CLKCMU_PERIC1_USI_I2C_USER,
	CLK_CON_DIV_DIV_CLK_PERIC1_UART_BT,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI06_USI,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI07_USI,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI08_USI,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI09_USI,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI10_USI,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI11_USI,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI12_USI,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI16_USI,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI17_USI,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI18_USI,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI_I2C,
	CLK_CON_GAT_CLK_BLK_PERIC1_UID_PERIC1_CMU_PERIC1_IPCLKPORT_PCLK,
	CLK_CON_GAT_CLK_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_UART_BT_IPCLKPORT_CLK,
	CLK_CON_GAT_CLK_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI12_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_CLK_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI18_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_D_TZPC_PERIC1_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_GPIO_PERIC1_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_LHM_AXI_P_CSISPERIC1_IPCLKPORT_I_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_LHM_AXI_P_PERIC1_IPCLKPORT_I_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_10,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_11,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_12,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_13,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_14,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_15,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_4,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_10,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_11,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_12,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_13,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_14,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_15,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_4,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_0,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_1,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_10,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_12,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_13,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_14,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_15,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_2,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_3,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_4,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_5,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_6,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_7,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_9,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_0,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_1,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_10,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_12,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_13,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_14,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_15,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_2,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_3,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_4,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_5,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_6,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_7,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_9,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_BUSP_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_OSCCLK_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI06_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI07_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI08_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI09_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI10_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI11_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI16_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI17_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI_I2C_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_SYSREG_PERIC1_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_USI16_I3C_IPCLKPORT_I_PCLK,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_USI16_I3C_IPCLKPORT_I_SCLK,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_USI17_I3C_IPCLKPORT_I_PCLK,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_USI17_I3C_IPCLKPORT_I_SCLK,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_XIU_P_PERIC1_IPCLKPORT_ACLK,
};

/* Parent clock list for CMU_PERIC1 muxes */
PNAME(mout_peric1_bus_user_p)  = { "oscclk", "dout_cmu_peric1_bus" };
PNAME(mout_peric1_uart_bt_user_p) = { "oscclk", "dout_cmu_peric1_ip" };
PNAME(mout_peric1_usi06_user_p)  = { "oscclk", "dout_cmu_peric1_ip" };
PNAME(mout_peric1_usi07_user_p)  = { "oscclk", "dout_cmu_peric1_ip" };
PNAME(mout_peric1_usi08_user_p)  = { "oscclk", "dout_cmu_peric1_ip" };
PNAME(mout_peric1_usi09_user_p)  = { "oscclk", "dout_cmu_peric1_ip" };
PNAME(mout_peric1_usi10_user_p)  = { "oscclk", "dout_cmu_peric1_ip" };
PNAME(mout_peric1_usi11_user_p)  = { "oscclk", "dout_cmu_peric1_ip" };
PNAME(mout_peric1_usi12_user_p)  = { "oscclk", "dout_cmu_peric1_ip" };
PNAME(mout_peric1_usi18_user_p)  = { "oscclk", "dout_cmu_peric1_ip" };
PNAME(mout_peric1_usi16_user_p)  = { "oscclk", "dout_cmu_peric1_ip" };
PNAME(mout_peric1_usi17_user_p)  = { "oscclk", "dout_cmu_peric1_ip" };
PNAME(mout_peric1_usi_i2c_user_p) = { "oscclk", "dout_cmu_peric1_ip" };

static const struct samsung_mux_clock peric1_mux_clks[] __initconst = {
	MUX(CLK_MOUT_PERIC1_BUS_USER, "mout_peric1_bus_user",
	    mout_peric1_bus_user_p, PLL_CON0_MUX_CLKCMU_PERIC1_BUS_USER,
	    4, 1),
	MUX(CLK_MOUT_PERIC1_UART_BT_USER, "mout_peric1_uart_bt_user",
	    mout_peric1_uart_bt_user_p, PLL_CON0_MUX_CLKCMU_PERIC1_UART_BT_USER,
	    4, 1),
	MUX(CLK_MOUT_PERIC1_USI06_USI_USER, "mout_peric1_usi06_usi_user",
	    mout_peric1_usi06_user_p, PLL_CON0_MUX_CLKCMU_PERIC1_USI06_USI_USER,
	    4, 1),
	MUX(CLK_MOUT_PERIC1_USI07_USI_USER, "mout_peric1_usi07_usi_user",
	    mout_peric1_usi07_user_p, PLL_CON0_MUX_CLKCMU_PERIC1_USI07_USI_USER,
	    4, 1),
	MUX(CLK_MOUT_PERIC1_USI08_USI_USER, "mout_peric1_usi08_usi_user",
	    mout_peric1_usi08_user_p, PLL_CON0_MUX_CLKCMU_PERIC1_USI08_USI_USER,
	    4, 1),
	MUX(CLK_MOUT_PERIC1_USI09_USI_USER, "mout_peric1_usi09_usi_user",
	    mout_peric1_usi09_user_p, PLL_CON0_MUX_CLKCMU_PERIC1_USI09_USI_USER,
	    4, 1),
	MUX(CLK_MOUT_PERIC1_USI10_USI_USER, "mout_peric1_usi10_usi_user",
	    mout_peric1_usi10_user_p, PLL_CON0_MUX_CLKCMU_PERIC1_USI10_USI_USER,
	    4, 1),
	MUX(CLK_MOUT_PERIC1_USI11_USI_USER, "mout_peric1_usi11_usi_user",
	    mout_peric1_usi11_user_p, PLL_CON0_MUX_CLKCMU_PERIC1_USI11_USI_USER,
	    4, 1),
	MUX(CLK_MOUT_PERIC1_USI12_USI_USER, "mout_peric1_usi12_usi_user",
	    mout_peric1_usi12_user_p, PLL_CON0_MUX_CLKCMU_PERIC1_USI12_USI_USER,
	    4, 1),
	MUX(CLK_MOUT_PERIC1_USI18_USI_USER, "mout_peric1_usi18_usi_user",
	    mout_peric1_usi18_user_p, PLL_CON0_MUX_CLKCMU_PERIC1_USI18_USI_USER,
	    4, 1),
	MUX(CLK_MOUT_PERIC1_USI16_USI_USER, "mout_peric1_usi16_usi_user",
	    mout_peric1_usi16_user_p, PLL_CON0_MUX_CLKCMU_PERIC1_USI16_USI_USER,
	    4, 1),
	MUX(CLK_MOUT_PERIC1_USI17_USI_USER, "mout_peric1_usi17_usi_user",
	    mout_peric1_usi17_user_p, PLL_CON0_MUX_CLKCMU_PERIC1_USI17_USI_USER,
	    4, 1),
	MUX(CLK_MOUT_PERIC1_USI_I2C_USER, "mout_peric1_usi_i2c_user",
	    mout_peric1_usi_i2c_user_p, PLL_CON0_MUX_CLKCMU_PERIC1_USI_I2C_USER,
	    4, 1),
};

static const struct samsung_div_clock peric1_div_clks[] __initconst = {
	DIV(CLK_DOUT_PERIC1_UART_BT, "dout_peric1_uart_bt",
	    "mout_peric1_uart_bt_user",
	    CLK_CON_DIV_DIV_CLK_PERIC1_UART_BT,
	    0, 4),
	DIV(CLK_DOUT_PERIC1_USI06_USI, "dout_peric1_usi06_usi",
	    "mout_peric1_usi06_usi_user",
	    CLK_CON_DIV_DIV_CLK_PERIC1_USI06_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC1_USI07_USI, "dout_peric1_usi07_usi",
	    "mout_peric1_usi07_usi_user",
	    CLK_CON_DIV_DIV_CLK_PERIC1_USI07_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC1_USI08_USI, "dout_peric1_usi08_usi",
	    "mout_peric1_usi08_usi_user",
	    CLK_CON_DIV_DIV_CLK_PERIC1_USI08_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC1_USI18_USI, "dout_peric1_usi18_usi",
	    "mout_peric1_usi18_usi_user",
	    CLK_CON_DIV_DIV_CLK_PERIC1_USI18_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC1_USI12_USI, "dout_peric1_usi12_usi",
	    "mout_peric1_usi12_usi_user",
	    CLK_CON_DIV_DIV_CLK_PERIC1_USI12_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC1_USI09_USI, "dout_peric1_usi09_usi",
	    "mout_peric1_usi09_usi_user",
	    CLK_CON_DIV_DIV_CLK_PERIC1_USI09_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC1_USI10_USI, "dout_peric1_usi10_usi",
	    "mout_peric1_usi10_usi_user",
	    CLK_CON_DIV_DIV_CLK_PERIC1_USI10_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC1_USI11_USI, "dout_peric1_usi11_usi",
	    "mout_peric1_usi11_usi_user",
	    CLK_CON_DIV_DIV_CLK_PERIC1_USI11_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC1_USI16_USI, "dout_peric1_usi16_usi",
	    "mout_peric1_usi16_usi_user",
	    CLK_CON_DIV_DIV_CLK_PERIC1_USI16_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC1_USI17_USI, "dout_peric1_usi17_usi",
	    "mout_peric1_usi17_usi_user",
	    CLK_CON_DIV_DIV_CLK_PERIC1_USI17_USI,
	    0, 4),
	DIV(CLK_DOUT_PERIC1_USI_I2C, "dout_peric1_usi_i2c",
	    "mout_peric1_usi_i2c_user",
	    CLK_CON_DIV_DIV_CLK_PERIC1_USI_I2C,
	    0, 4),
};

static const struct samsung_gate_clock peric1_gate_clks[] __initconst = {
	GATE(CLK_GOUT_PERIC1_CMU_PCLK, "gout_peric1_cmu_pclk",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_CLK_BLK_PERIC1_UID_PERIC1_CMU_PERIC1_IPCLKPORT_PCLK,
	     21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_PERIC1_UART_BT_CLK, "gout_peric1_uart_bt_clk",
	     "dout_peric1_uart_bt",
	     CLK_CON_GAT_CLK_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_UART_BT_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI12_USI_CLK, "gout_peric1_usi12_usi_clk",
	     "dout_peric1_usi12_usi",
	     CLK_CON_GAT_CLK_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI12_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI18_USI_CLK, "gout_peric1_usi18_usi_clk",
	     "dout_peric1_usi18_usi",
	     CLK_CON_GAT_CLK_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI18_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_D_TZPC_PCLK, "gout_peric1_d_tzpc_pclk",
	     "dout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_D_TZPC_PERIC1_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_GPIO_PCLK, "gout_peric1_gpio_pclk",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_GPIO_PERIC1_IPCLKPORT_PCLK,
	     21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_PERIC1_LHM_AXI_P_CSIS_CLK, "gout_peric1_lhm_axi_p_csis_clk",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_LHM_AXI_P_CSISPERIC1_IPCLKPORT_I_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_LHM_AXI_P_CLK, "gout_peric1_lhm_axi_p_clk",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_LHM_AXI_P_PERIC1_IPCLKPORT_I_CLK,
	     21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_PERIC1_TOP0_IPCLK_10, "gout_peric1_top0_ipclk_10",
	     "dout_peric1_usi06_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_10,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP0_IPCLK_11, "gout_peric1_top0_ipclk_11",
	     "dout_peric1_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_11,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP0_IPCLK_12, "gout_peric1_top0_ipclk_12",
	     "dout_peric1_usi07_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_12,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP0_IPCLK_13, "gout_peric1_top0_ipclk_13",
	     "dout_peric1_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_13,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP0_IPCLK_14, "gout_peric1_top0_ipclk_14",
	     "dout_peric1_usi08_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_14,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP0_IPCLK_15, "gout_peric1_top0_ipclk_15",
	     "dout_peric1_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_15,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP0_IPCLK_4, "gout_peric1_top0_ipclk_4",
	     "dout_peric1_uart_bt",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_4,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP0_PCLK_10, "gout_peric1_top0_pclk_10",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_10,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP0_PCLK_11, "gout_peric1_top0_pclk_11",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_11,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP0_PCLK_12, "gout_peric1_top0_pclk_12",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_12,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP0_PCLK_13, "gout_peric1_top0_pclk_13",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_13,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP0_PCLK_14, "gout_peric1_top0_pclk_14",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_14,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP0_PCLK_15, "gout_peric1_top0_pclk_15",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_15,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP0_PCLK_4, "gout_peric1_top0_pclk_4",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_4,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP1_IPCLK_0, "gout_peric1_top1_ipclk_0",
	     "dout_peric1_usi09_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_0,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP1_IPCLK_1, "gout_peric1_top1_ipclk_1",
	     "dout_peric1_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_1,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP1_IPCLK_10, "gout_peric1_top1_ipclk_10",
	     "dout_peric1_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_10,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP1_IPCLK_12, "gout_peric1_top1_ipclk_12",
	     "dout_peric1_usi12_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_12,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP1_IPCLK_13, "gout_peric1_top1_ipclk_13",
	     "dout_peric1_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_13,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP1_IPCLK_14, "gout_peric1_top1_ipclk_14",
	     "dout_peric1_usi18_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_14,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP1_IPCLK_15, "gout_peric1_top1_ipclk_15",
	     "dout_peric1_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_15,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP1_IPCLK_2, "gout_peric1_top1_ipclk_2",
	     "dout_peric1_usi10_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_2,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP1_IPCLK_3, "gout_peric1_top1_ipclk_3",
	     "dout_peric1_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_3,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP1_IPCLK_4, "gout_peric1_top1_ipclk_4",
	     "dout_peric1_usi11_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_4,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP1_IPCLK_5, "gout_peric1_top1_ipclk_5",
	     "dout_peric1_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_5,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP1_IPCLK_6, "gout_peric1_top1_ipclk_6",
	     "dout_peric1_usi16_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_6,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP1_IPCLK_7, "gout_peric1_top1_ipclk_7",
	     "dout_peric1_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_7,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP1_IPCLK_9, "gout_peric1_top1_ipclk_9",
	     "dout_peric1_usi17_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_IPCLK_9,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP1_PCLK_0, "gout_peric1_top1_pclk_0",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_0,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP1_PCLK_1, "gout_peric1_top1_pclk_1",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_1,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP1_PCLK_10, "gout_peric1_top1_pclk_10",
	     "dout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_10,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP1_PCLK_12, "gout_peric1_top1_pclk_12",
	     "dout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_12,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP1_PCLK_13, "gout_peric1_top1_pclk_13",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_13,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP1_PCLK_14, "gout_peric1_top1_pclk_14",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_14,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP1_PCLK_15, "gout_peric1_top1_pclk_15",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_15,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP1_PCLK_2, "gout_peric1_top1_pclk_2",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_2,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP1_PCLK_3, "gout_peric1_top1_pclk_3",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_3,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP1_PCLK_4, "gout_peric1_top1_pclk_4",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_4,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP1_PCLK_5, "gout_peric1_top1_pclk_5",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_5,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP1_PCLK_6, "gout_peric1_top1_pclk_6",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_6,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP1_PCLK_7, "gout_peric1_top1_pclk_7",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_7,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_TOP1_PCLK_9, "gout_peric1_top1_pclk_9",
	     "dout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP1_IPCLKPORT_PCLK_9,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_BUSP_CLK, "gout_peric1_busp_clk",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_BUSP_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_OSCCLK_CLK, "gout_peric1_oscclk_clk",
	     "oscclk",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_OSCCLK_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI06_USI_CLK, "gout_peric1_usi06_usi_clk",
	     "dout_peric1_usi06_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI06_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI07_USI_CLK, "gout_peric1_usi07_usi_clk",
	     "dout_peric1_usi07_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI07_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI08_USI_CLK, "gout_peric1_usi08_usi_clk",
	     "dout_peric1_usi08_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI08_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI09_USI_CLK, "gout_peric1_usi09_usi_clk",
	     "dout_peric1_usi09_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI09_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI10_USI_CLK, "gout_peric1_usi10_usi_clk",
	     "dout_peric1_usi10_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI10_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI11_USI_CLK, "gout_peric1_usi11_usi_clk",
	     "dout_peric1_usi11_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI11_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI16_USI_CLK, "gout_peric1_usi16_usi_clk",
	     "dout_peric1_usi16_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI16_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI17_USI_CLK, "gout_peric1_usi17_usi_clk",
	     "dout_peric1_usi17_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI17_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI_I2C_CLK, "gout_peric1_usi_i2c_clk",
	     "dout_peric1_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI_I2C_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_SYSREG_PCLK, "gout_peric1_sysreg_pclk",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_SYSREG_PERIC1_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI16_I3C_PCLK, "gout_peric1_usi16_i3c_pclk",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_USI16_I3C_IPCLKPORT_I_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI16_I3C_SCLK, "gout_peric1_usi16_i3c_sclk",
	     "dout_peric1_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_USI16_I3C_IPCLKPORT_I_SCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI17_I3C_PCLK, "gout_peric1_usi17_i3c_pclk",
	     "dout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_USI17_I3C_IPCLKPORT_I_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_USI17_I3C_SCLK, "gout_peric1_usi17_i3c_sclk",
	     "dout_peric1_usi_i2c",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_USI17_I3C_IPCLKPORT_I_SCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_XIU_P_ACLK, "gout_peric1_xiu_p_aclk",
	     "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_XIU_P_PERIC1_IPCLKPORT_ACLK,
	     21, CLK_IGNORE_UNUSED, 0),
};

static const struct samsung_cmu_info peric1_cmu_info __initconst = {
	.mux_clks	= peric1_mux_clks,
	.nr_mux_clks	= ARRAY_SIZE(peric1_mux_clks),
	.div_clks	= peric1_div_clks,
	.nr_div_clks	= ARRAY_SIZE(peric1_div_clks),
	.gate_clks	= peric1_gate_clks,
	.nr_gate_clks	= ARRAY_SIZE(peric1_gate_clks),
	.nr_clk_ids	= CLKS_NR_PERIC1,
	.clk_regs	= peric1_clk_regs,
	.nr_clk_regs	= ARRAY_SIZE(peric1_clk_regs),
	.clk_name	= "bus",
};

/* ---- CMU_PERIS ----------------------------------------------------------- */

/* Register Offset definitions for CMU_PERIS (0x10020000) */
#define PLL_CON0_MUX_CLKCMU_PERIS_BUS_USER					0x0600
#define PLL_CON1_MUX_CLKCMU_PERIS_BUS_USER					0x0604
#define CLK_CON_MUX_MUX_CLK_PERIS_GIC						0x1000
#define CLK_CON_GAT_GOUT_BLK_PERIS_UID_SYSREG_PERIS_IPCLKPORT_PCLK		0x203c
#define CLK_CON_GAT_GOUT_BLK_PERIS_UID_WDT_CLUSTER2_IPCLKPORT_PCLK		0x204c
#define CLK_CON_GAT_GOUT_BLK_PERIS_UID_WDT_CLUSTER0_IPCLKPORT_PCLK		0x2048
#define CLK_CON_GAT_CLK_BLK_PERIS_UID_PERIS_CMU_PERIS_IPCLKPORT_PCLK		0x200c
#define CLK_CON_GAT_GOUT_BLK_PERIS_UID_RSTNSYNC_CLK_PERIS_BUSP_IPCLKPORT_CLK	0x2034
#define CLK_CON_GAT_CLK_BLK_PERIS_UID_RSTNSYNC_CLK_PERIS_OSCCLK_IPCLKPORT_CLK	0x2010
#define CLK_CON_GAT_GOUT_BLK_PERIS_UID_RSTNSYNC_CLK_PERIS_GIC_IPCLKPORT_CLK	0x2038
#define CLK_CON_GAT_GOUT_BLK_PERIS_UID_AD_AXI_P_PERIS_IPCLKPORT_ACLKM		0x2014
#define CLK_CON_GAT_GOUT_BLK_PERIS_UID_OTP_CON_BIRA_IPCLKPORT_PCLK		0x2028
#define CLK_CON_GAT_GOUT_BLK_PERIS_UID_GIC_IPCLKPORT_CLK			0x201c
#define CLK_CON_GAT_GOUT_BLK_PERIS_UID_LHM_AXI_P_PERIS_IPCLKPORT_I_CLK		0x2020
#define CLK_CON_GAT_GOUT_BLK_PERIS_UID_MCT_IPCLKPORT_PCLK			0x2024
#define CLK_CON_GAT_GOUT_BLK_PERIS_UID_OTP_CON_TOP_IPCLKPORT_PCLK		0x2030
#define CLK_CON_GAT_GOUT_BLK_PERIS_UID_D_TZPC_PERIS_IPCLKPORT_PCLK		0x2018
#define CLK_CON_GAT_GOUT_BLK_PERIS_UID_TMU_SUB_IPCLKPORT_PCLK			0x2040
#define CLK_CON_GAT_GOUT_BLK_PERIS_UID_TMU_TOP_IPCLKPORT_PCLK			0x2044
#define CLK_CON_GAT_CLK_BLK_PERIS_UID_OTP_CON_BIRA_IPCLKPORT_I_OSCCLK		0x2000
#define CLK_CON_GAT_CLK_BLK_PERIS_UID_OTP_CON_TOP_IPCLKPORT_I_OSCCLK		0x2008
#define QCH_CON_D_TZPC_PERIS_QCH						0x3004
#define QCH_CON_GIC_QCH								0x3008
#define QCH_CON_LHM_AXI_P_PERIS_QCH						0x300c
#define QCH_CON_MCT_QCH								0x3010
#define QCH_CON_OTP_CON_BIRA_QCH						0x3014
#define QCH_CON_OTP_CON_TOP_QCH							0x301c
#define QCH_CON_PERIS_CMU_PERIS_QCH						0x3020
#define QCH_CON_SYSREG_PERIS_QCH						0x3024
#define QCH_CON_TMU_SUB_QCH							0x3028
#define QCH_CON_TMU_TOP_QCH							0x302c
#define QCH_CON_WDT_CLUSTER0_QCH						0x3030
#define QCH_CON_WDT_CLUSTER2_QCH						0x3034

static const unsigned long peris_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_PERIS_BUS_USER,
	PLL_CON1_MUX_CLKCMU_PERIS_BUS_USER,
	CLK_CON_MUX_MUX_CLK_PERIS_GIC,
	CLK_CON_GAT_GOUT_BLK_PERIS_UID_SYSREG_PERIS_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_PERIS_UID_WDT_CLUSTER2_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_PERIS_UID_WDT_CLUSTER0_IPCLKPORT_PCLK,
	CLK_CON_GAT_CLK_BLK_PERIS_UID_PERIS_CMU_PERIS_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_PERIS_UID_RSTNSYNC_CLK_PERIS_BUSP_IPCLKPORT_CLK,
	CLK_CON_GAT_CLK_BLK_PERIS_UID_RSTNSYNC_CLK_PERIS_OSCCLK_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIS_UID_RSTNSYNC_CLK_PERIS_GIC_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIS_UID_AD_AXI_P_PERIS_IPCLKPORT_ACLKM,
	CLK_CON_GAT_GOUT_BLK_PERIS_UID_OTP_CON_BIRA_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_PERIS_UID_GIC_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIS_UID_LHM_AXI_P_PERIS_IPCLKPORT_I_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIS_UID_MCT_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_PERIS_UID_OTP_CON_TOP_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_PERIS_UID_D_TZPC_PERIS_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_PERIS_UID_TMU_SUB_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_PERIS_UID_TMU_TOP_IPCLKPORT_PCLK,
	CLK_CON_GAT_CLK_BLK_PERIS_UID_OTP_CON_BIRA_IPCLKPORT_I_OSCCLK,
	CLK_CON_GAT_CLK_BLK_PERIS_UID_OTP_CON_TOP_IPCLKPORT_I_OSCCLK,
	QCH_CON_D_TZPC_PERIS_QCH,
	QCH_CON_GIC_QCH,
	QCH_CON_LHM_AXI_P_PERIS_QCH,
	QCH_CON_MCT_QCH,
	QCH_CON_OTP_CON_BIRA_QCH,
	QCH_CON_OTP_CON_TOP_QCH,
	QCH_CON_PERIS_CMU_PERIS_QCH,
	QCH_CON_SYSREG_PERIS_QCH,
	QCH_CON_TMU_SUB_QCH,
	QCH_CON_TMU_TOP_QCH,
	QCH_CON_WDT_CLUSTER0_QCH,
	QCH_CON_WDT_CLUSTER2_QCH,
};

/* Parent clock list for CMU_PERIS muxes */
PNAME(mout_peris_bus_user_p)		= { "oscclk", "mout_cmu_peris_bus" };
PNAME(mout_peris_clk_peris_gic_p)	= { "oscclk", "mout_peris_bus_user" };

static const struct samsung_mux_clock peris_mux_clks[] __initconst = {
	MUX(CLK_MOUT_PERIS_BUS_USER, "mout_peris_bus_user",
	    mout_peris_bus_user_p, PLL_CON0_MUX_CLKCMU_PERIS_BUS_USER,
	    4, 1),
	MUX(CLK_MOUT_PERIS_CLK_PERIS_GIC, "mout_peris_clk_peris_gic",
	    mout_peris_clk_peris_gic_p, CLK_CON_MUX_MUX_CLK_PERIS_GIC,
	    4, 1),
};

static const struct samsung_gate_clock peris_gate_clks[] __initconst = {
	GATE(CLK_GOUT_PERIS_SYSREG_PERIS_PCLK,
	     "gout_peris_sysreg_peris_pclk", "mout_peris_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIS_UID_SYSREG_PERIS_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIS_WDT_CLUSTER2_PCLK,
	     "gout_peris_wdt_cluster2_pclk", "mout_peris_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIS_UID_WDT_CLUSTER2_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIS_WDT_CLUSTER0_PCLK,
	     "gout_peris_wdt_cluster0_pclk", "mout_peris_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIS_UID_WDT_CLUSTER0_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_CLK_PERIS_PERIS_CMU_PERIS_PCLK,
	     "clk_peris_peris_cmu_peris_pclk", "mout_peris_bus_user",
	     CLK_CON_GAT_CLK_BLK_PERIS_UID_PERIS_CMU_PERIS_IPCLKPORT_PCLK,
	     21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_PERIS_CLK_PERIS_BUSP_CLK,
	     "gout_peris_clk_peris_busp_clk", "mout_peris_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIS_UID_RSTNSYNC_CLK_PERIS_BUSP_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIS_CLK_PERIS_OSCCLK_CLK,
	     "gout_peris_clk_peris_oscclk_clk", "mout_peris_bus_user",
	     CLK_CON_GAT_CLK_BLK_PERIS_UID_RSTNSYNC_CLK_PERIS_OSCCLK_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIS_CLK_PERIS_GIC_CLK,
	     "gout_peris_clk_peris_gic_clk", "mout_peris_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIS_UID_RSTNSYNC_CLK_PERIS_GIC_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIS_AD_AXI_P_PERIS_ACLKM,
	     "gout_peris_ad_axi_p_peris_aclkm", "mout_peris_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIS_UID_AD_AXI_P_PERIS_IPCLKPORT_ACLKM,
	     21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_PERIS_OTP_CON_BIRA_PCLK,
	     "gout_peris_otp_con_bira_pclk", "mout_peris_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIS_UID_OTP_CON_BIRA_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIS_GIC_CLK,
	     "gout_peris_gic_clk", "mout_peris_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIS_UID_GIC_IPCLKPORT_CLK,
	     21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_PERIS_LHM_AXI_P_PERIS_CLK,
	     "gout_peris_lhm_axi_p_peris_clk", "oscclk",
	     CLK_CON_GAT_GOUT_BLK_PERIS_UID_LHM_AXI_P_PERIS_IPCLKPORT_I_CLK,
	     21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_PERIS_MCT_PCLK,
	     "gout_peris_mct_pclk", "mout_peris_clk_peris_gic",
	     CLK_CON_GAT_GOUT_BLK_PERIS_UID_MCT_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIS_OTP_CON_TOP_PCLK,
	     "gout_peris_otp_con_top_pclk", "mout_peris_clk_peris_gic",
	     CLK_CON_GAT_GOUT_BLK_PERIS_UID_OTP_CON_TOP_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIS_D_TZPC_PERIS_PCLK,
	     "gout_peris_d_tzpc_peris_pclk", "mout_peris_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIS_UID_D_TZPC_PERIS_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIS_TMU_TOP_PCLK,
	     "gout_peris_tmu_top_pclk", "mout_peris_clk_peris_gic",
	     CLK_CON_GAT_GOUT_BLK_PERIS_UID_TMU_TOP_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIS_OTP_CON_BIRA_OSCCLK,
	     "gout_peris_otp_con_bira_oscclk", "oscclk",
	     CLK_CON_GAT_CLK_BLK_PERIS_UID_OTP_CON_BIRA_IPCLKPORT_I_OSCCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIS_OTP_CON_TOP_OSCCLK,
	     "gout_peris_otp_con_top_oscclk", "oscclk",
	     CLK_CON_GAT_CLK_BLK_PERIS_UID_OTP_CON_TOP_IPCLKPORT_I_OSCCLK,
	     21, 0, 0),
};

static const struct samsung_cmu_info peris_cmu_info __initconst = {
	.mux_clks = peris_mux_clks,
	.nr_mux_clks = ARRAY_SIZE(peris_mux_clks),
	.gate_clks = peris_gate_clks,
	.nr_gate_clks = ARRAY_SIZE(peris_gate_clks),
	.nr_clk_ids = CLKS_NR_PERIS,
	.clk_regs = peris_clk_regs,
	.nr_clk_regs = ARRAY_SIZE(peris_clk_regs),
};

static void __init exynos990_cmu_peris_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &peris_cmu_info);
}

/* Register CMU_PERIS early, as it's a dependency for the MCT. */
CLK_OF_DECLARE(exynos990_cmu_peris, "samsung,exynos990-cmu-peris",
	       exynos990_cmu_peris_init);

/* ----- platform_driver ----- */

static int __init exynos990_cmu_probe(struct platform_device *pdev)
{
	const struct samsung_cmu_info *info;
	struct device *dev = &pdev->dev;

	info = of_device_get_match_data(dev);
	exynos_arm64_register_cmu(dev, dev->of_node, info);

	return 0;
}

static const struct of_device_id exynos990_cmu_of_match[] = {
	{
		.compatible = "samsung,exynos990-cmu-hsi0",
		.data = &hsi0_cmu_info,
	}, {
		.compatible = "samsung,exynos990-cmu-peric0",
		.data = &peric0_cmu_info,
	}, {
		.compatible = "samsung,exynos990-cmu-peric1",
		.data = &peric1_cmu_info,
	},
	{ },
};

static struct platform_driver exynos990_cmu_driver __refdata = {
	.driver	= {
		.name = "exynos990-cmu",
		.of_match_table = exynos990_cmu_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = exynos990_cmu_probe,
};

static int __init exynos990_cmu_init(void)
{
	return platform_driver_register(&exynos990_cmu_driver);
}

core_initcall(exynos990_cmu_init);
