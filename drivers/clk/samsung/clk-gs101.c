// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Linaro Ltd.
 * Author: Peter Griffin <peter.griffin@linaro.org>
 *
 * Common Clock Framework support for GS101.
 */

#include <linux/clk-provider.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <dt-bindings/clock/google,gs101.h>

#include "clk.h"
#include "clk-exynos-arm64.h"
#include "clk-pll.h"

/* NOTE: Must be equal to the last clock ID increased by one */
#define CLKS_NR_TOP	(CLK_GOUT_CMU_TPU_UART + 1)
#define CLKS_NR_APM	(CLK_APM_PLL_DIV16_APM + 1)
#define CLKS_NR_HSI0	(CLK_GOUT_HSI0_XIU_P_HSI0_ACLK + 1)
#define CLKS_NR_HSI2	(CLK_GOUT_HSI2_XIU_P_HSI2_ACLK + 1)
#define CLKS_NR_MISC	(CLK_GOUT_MISC_XIU_D_MISC_ACLK + 1)
#define CLKS_NR_PERIC0	(CLK_GOUT_PERIC0_SYSREG_PERIC0_PCLK + 1)
#define CLKS_NR_PERIC1	(CLK_GOUT_PERIC1_SYSREG_PERIC1_PCLK + 1)

/* ---- CMU_TOP ------------------------------------------------------------- */

/* Register Offset definitions for CMU_TOP (0x1e080000) */
#define PLL_LOCKTIME_PLL_SHARED0			0x0000
#define PLL_LOCKTIME_PLL_SHARED1			0x0004
#define PLL_LOCKTIME_PLL_SHARED2			0x0008
#define PLL_LOCKTIME_PLL_SHARED3			0x000c
#define PLL_LOCKTIME_PLL_SPARE				0x0010
#define PLL_CON0_PLL_SHARED0				0x0100
#define PLL_CON1_PLL_SHARED0				0x0104
#define PLL_CON2_PLL_SHARED0				0x0108
#define PLL_CON3_PLL_SHARED0				0x010c
#define PLL_CON4_PLL_SHARED0				0x0110
#define PLL_CON0_PLL_SHARED1				0x0140
#define PLL_CON1_PLL_SHARED1				0x0144
#define PLL_CON2_PLL_SHARED1				0x0148
#define PLL_CON3_PLL_SHARED1				0x014c
#define PLL_CON4_PLL_SHARED1				0x0150
#define PLL_CON0_PLL_SHARED2				0x0180
#define PLL_CON1_PLL_SHARED2				0x0184
#define PLL_CON2_PLL_SHARED2				0x0188
#define PLL_CON3_PLL_SHARED2				0x018c
#define PLL_CON4_PLL_SHARED2				0x0190
#define PLL_CON0_PLL_SHARED3				0x01c0
#define PLL_CON1_PLL_SHARED3				0x01c4
#define PLL_CON2_PLL_SHARED3				0x01c8
#define PLL_CON3_PLL_SHARED3				0x01cc
#define PLL_CON4_PLL_SHARED3				0x01d0
#define PLL_CON0_PLL_SPARE				0x0200
#define PLL_CON1_PLL_SPARE				0x0204
#define PLL_CON2_PLL_SPARE				0x0208
#define PLL_CON3_PLL_SPARE				0x020c
#define PLL_CON4_PLL_SPARE				0x0210
#define CMU_CMU_TOP_CONTROLLER_OPTION			0x0800
#define CLKOUT_CON_BLK_CMU_CMU_TOP_CLKOUT0		0x0810
#define CMU_HCHGEN_CLKMUX_CMU_BOOST			0x0840
#define CMU_HCHGEN_CLKMUX_TOP_BOOST			0x0844
#define CMU_HCHGEN_CLKMUX				0x0850
#define POWER_FAIL_DETECT_PLL				0x0864
#define EARLY_WAKEUP_FORCED_0_ENABLE			0x0870
#define EARLY_WAKEUP_FORCED_1_ENABLE			0x0874
#define EARLY_WAKEUP_APM_CTRL				0x0878
#define EARLY_WAKEUP_CLUSTER0_CTRL			0x087c
#define EARLY_WAKEUP_DPU_CTRL				0x0880
#define EARLY_WAKEUP_CSIS_CTRL				0x0884
#define EARLY_WAKEUP_APM_DEST				0x0890
#define EARLY_WAKEUP_CLUSTER0_DEST			0x0894
#define EARLY_WAKEUP_DPU_DEST				0x0898
#define EARLY_WAKEUP_CSIS_DEST				0x089c
#define EARLY_WAKEUP_SW_TRIG_APM			0x08c0
#define EARLY_WAKEUP_SW_TRIG_APM_SET			0x08c4
#define EARLY_WAKEUP_SW_TRIG_APM_CLEAR			0x08c8
#define EARLY_WAKEUP_SW_TRIG_CLUSTER0			0x08d0
#define EARLY_WAKEUP_SW_TRIG_CLUSTER0_SET		0x08d4
#define EARLY_WAKEUP_SW_TRIG_CLUSTER0_CLEAR		0x08d8
#define EARLY_WAKEUP_SW_TRIG_DPU			0x08e0
#define EARLY_WAKEUP_SW_TRIG_DPU_SET			0x08e4
#define EARLY_WAKEUP_SW_TRIG_DPU_CLEAR			0x08e8
#define EARLY_WAKEUP_SW_TRIG_CSIS			0x08f0
#define EARLY_WAKEUP_SW_TRIG_CSIS_SET			0x08f4
#define EARLY_WAKEUP_SW_TRIG_CSIS_CLEAR			0x08f8
#define CLK_CON_MUX_MUX_CLKCMU_BO_BUS			0x1000
#define CLK_CON_MUX_MUX_CLKCMU_BUS0_BUS			0x1004
#define CLK_CON_MUX_MUX_CLKCMU_BUS1_BUS			0x1008
#define CLK_CON_MUX_MUX_CLKCMU_BUS2_BUS			0x100c
#define CLK_CON_MUX_MUX_CLKCMU_CIS_CLK0			0x1010
#define CLK_CON_MUX_MUX_CLKCMU_CIS_CLK1			0x1014
#define CLK_CON_MUX_MUX_CLKCMU_CIS_CLK2			0x1018
#define CLK_CON_MUX_MUX_CLKCMU_CIS_CLK3			0x101c
#define CLK_CON_MUX_MUX_CLKCMU_CIS_CLK4			0x1020
#define CLK_CON_MUX_MUX_CLKCMU_CIS_CLK5			0x1024
#define CLK_CON_MUX_MUX_CLKCMU_CIS_CLK6			0x1028
#define CLK_CON_MUX_MUX_CLKCMU_CIS_CLK7			0x102c
#define CLK_CON_MUX_MUX_CLKCMU_CMU_BOOST		0x1030
#define CLK_CON_MUX_MUX_CLKCMU_CMU_BOOST_OPTION1	0x1034
#define CLK_CON_MUX_MUX_CLKCMU_CORE_BUS			0x1038
#define CLK_CON_MUX_MUX_CLKCMU_CPUCL0_DBG		0x103c
#define CLK_CON_MUX_MUX_CLKCMU_CPUCL0_SWITCH		0x1040
#define CLK_CON_MUX_MUX_CLKCMU_CPUCL1_SWITCH		0x1044
#define CLK_CON_MUX_MUX_CLKCMU_CPUCL2_SWITCH		0x1048
#define CLK_CON_MUX_MUX_CLKCMU_CSIS_BUS			0x104c
#define CLK_CON_MUX_MUX_CLKCMU_DISP_BUS			0x1050
#define CLK_CON_MUX_MUX_CLKCMU_DNS_BUS			0x1054
#define CLK_CON_MUX_MUX_CLKCMU_DPU_BUS			0x1058
#define CLK_CON_MUX_MUX_CLKCMU_EH_BUS			0x105c
#define CLK_CON_MUX_MUX_CLKCMU_G2D_G2D			0x1060
#define CLK_CON_MUX_MUX_CLKCMU_G2D_MSCL			0x1064
#define CLK_CON_MUX_MUX_CLKCMU_G3AA_G3AA		0x1068
#define CLK_CON_MUX_MUX_CLKCMU_G3D_BUSD			0x106c
#define CLK_CON_MUX_MUX_CLKCMU_G3D_GLB			0x1070
#define CLK_CON_MUX_MUX_CLKCMU_G3D_SWITCH		0x1074
#define CLK_CON_MUX_MUX_CLKCMU_GDC_GDC0			0x1078
#define CLK_CON_MUX_MUX_CLKCMU_GDC_GDC1			0x107c
#define CLK_CON_MUX_MUX_CLKCMU_GDC_SCSC			0x1080
#define CLK_CON_MUX_MUX_CLKCMU_HPM			0x1084
#define CLK_CON_MUX_MUX_CLKCMU_HSI0_BUS			0x1088
#define CLK_CON_MUX_MUX_CLKCMU_HSI0_DPGTC		0x108c
#define CLK_CON_MUX_MUX_CLKCMU_HSI0_USB31DRD		0x1090
#define CLK_CON_MUX_MUX_CLKCMU_HSI0_USBDPDBG		0x1094
#define CLK_CON_MUX_MUX_CLKCMU_HSI1_BUS			0x1098
#define CLK_CON_MUX_MUX_CLKCMU_HSI1_PCIE		0x109c
#define CLK_CON_MUX_MUX_CLKCMU_HSI2_BUS			0x10a0
#define CLK_CON_MUX_MUX_CLKCMU_HSI2_MMC_CARD		0x10a4
#define CLK_CON_MUX_MUX_CLKCMU_HSI2_PCIE		0x10a8
#define CLK_CON_MUX_MUX_CLKCMU_HSI2_UFS_EMBD		0x10ac
#define CLK_CON_MUX_MUX_CLKCMU_IPP_BUS			0x10b0
#define CLK_CON_MUX_MUX_CLKCMU_ITP_BUS			0x10b4
#define CLK_CON_MUX_MUX_CLKCMU_MCSC_ITSC		0x10b8
#define CLK_CON_MUX_MUX_CLKCMU_MCSC_MCSC		0x10bc
#define CLK_CON_MUX_MUX_CLKCMU_MFC_MFC			0x10c0
#define CLK_CON_MUX_MUX_CLKCMU_MIF_BUSP			0x10c4
#define CLK_CON_MUX_MUX_CLKCMU_MIF_SWITCH		0x10c8
#define CLK_CON_MUX_MUX_CLKCMU_MISC_BUS			0x10cc
#define CLK_CON_MUX_MUX_CLKCMU_MISC_SSS			0x10d0
#define CLK_CON_MUX_MUX_CLKCMU_PDP_BUS			0x10d4
#define CLK_CON_MUX_MUX_CLKCMU_PDP_VRA			0x10d8
#define CLK_CON_MUX_MUX_CLKCMU_PERIC0_BUS		0x10dc
#define CLK_CON_MUX_MUX_CLKCMU_PERIC0_IP		0x10e0
#define CLK_CON_MUX_MUX_CLKCMU_PERIC1_BUS		0x10e4
#define CLK_CON_MUX_MUX_CLKCMU_PERIC1_IP		0x10e8
#define CLK_CON_MUX_MUX_CLKCMU_TNR_BUS			0x10ec
#define CLK_CON_MUX_MUX_CLKCMU_TOP_BOOST_OPTION1	0x10f0
#define CLK_CON_MUX_MUX_CLKCMU_TOP_CMUREF		0x10f4
#define CLK_CON_MUX_MUX_CLKCMU_TPU_BUS			0x10f8
#define CLK_CON_MUX_MUX_CLKCMU_TPU_TPU			0x10fc
#define CLK_CON_MUX_MUX_CLKCMU_TPU_TPUCTL		0x1100
#define CLK_CON_MUX_MUX_CLKCMU_TPU_UART			0x1104
#define CLK_CON_MUX_MUX_CMU_CMUREF			0x1108
#define CLK_CON_DIV_CLKCMU_BO_BUS			0x1800
#define CLK_CON_DIV_CLKCMU_BUS0_BUS			0x1804
#define CLK_CON_DIV_CLKCMU_BUS1_BUS			0x1808
#define CLK_CON_DIV_CLKCMU_BUS2_BUS			0x180c
#define CLK_CON_DIV_CLKCMU_CIS_CLK0			0x1810
#define CLK_CON_DIV_CLKCMU_CIS_CLK1			0x1814
#define CLK_CON_DIV_CLKCMU_CIS_CLK2			0x1818
#define CLK_CON_DIV_CLKCMU_CIS_CLK3			0x181c
#define CLK_CON_DIV_CLKCMU_CIS_CLK4			0x1820
#define CLK_CON_DIV_CLKCMU_CIS_CLK5			0x1824
#define CLK_CON_DIV_CLKCMU_CIS_CLK6			0x1828
#define CLK_CON_DIV_CLKCMU_CIS_CLK7			0x182c
#define CLK_CON_DIV_CLKCMU_CORE_BUS			0x1830
#define CLK_CON_DIV_CLKCMU_CPUCL0_DBG			0x1834
#define CLK_CON_DIV_CLKCMU_CPUCL0_SWITCH		0x1838
#define CLK_CON_DIV_CLKCMU_CPUCL1_SWITCH		0x183c
#define CLK_CON_DIV_CLKCMU_CPUCL2_SWITCH		0x1840
#define CLK_CON_DIV_CLKCMU_CSIS_BUS			0x1844
#define CLK_CON_DIV_CLKCMU_DISP_BUS			0x1848
#define CLK_CON_DIV_CLKCMU_DNS_BUS			0x184c
#define CLK_CON_DIV_CLKCMU_DPU_BUS			0x1850
#define CLK_CON_DIV_CLKCMU_EH_BUS			0x1854
#define CLK_CON_DIV_CLKCMU_G2D_G2D			0x1858
#define CLK_CON_DIV_CLKCMU_G2D_MSCL			0x185c
#define CLK_CON_DIV_CLKCMU_G3AA_G3AA			0x1860
#define CLK_CON_DIV_CLKCMU_G3D_BUSD			0x1864
#define CLK_CON_DIV_CLKCMU_G3D_GLB			0x1868
#define CLK_CON_DIV_CLKCMU_G3D_SWITCH			0x186c
#define CLK_CON_DIV_CLKCMU_GDC_GDC0			0x1870
#define CLK_CON_DIV_CLKCMU_GDC_GDC1			0x1874
#define CLK_CON_DIV_CLKCMU_GDC_SCSC			0x1878
#define CLK_CON_DIV_CLKCMU_HPM				0x187c
#define CLK_CON_DIV_CLKCMU_HSI0_BUS			0x1880
#define CLK_CON_DIV_CLKCMU_HSI0_DPGTC			0x1884
#define CLK_CON_DIV_CLKCMU_HSI0_USB31DRD		0x1888
#define CLK_CON_DIV_CLKCMU_HSI0_USBDPDBG		0x188c
#define CLK_CON_DIV_CLKCMU_HSI1_BUS			0x1890
#define CLK_CON_DIV_CLKCMU_HSI1_PCIE			0x1894
#define CLK_CON_DIV_CLKCMU_HSI2_BUS			0x1898
#define CLK_CON_DIV_CLKCMU_HSI2_MMC_CARD		0x189c
#define CLK_CON_DIV_CLKCMU_HSI2_PCIE			0x18a0
#define CLK_CON_DIV_CLKCMU_HSI2_UFS_EMBD		0x18a4
#define CLK_CON_DIV_CLKCMU_IPP_BUS			0x18a8
#define CLK_CON_DIV_CLKCMU_ITP_BUS			0x18ac
#define CLK_CON_DIV_CLKCMU_MCSC_ITSC			0x18b0
#define CLK_CON_DIV_CLKCMU_MCSC_MCSC			0x18b4
#define CLK_CON_DIV_CLKCMU_MFC_MFC			0x18b8
#define CLK_CON_DIV_CLKCMU_MIF_BUSP			0x18bc
#define CLK_CON_DIV_CLKCMU_MISC_BUS			0x18c0
#define CLK_CON_DIV_CLKCMU_MISC_SSS			0x18c4
#define CLK_CON_DIV_CLKCMU_OTP				0x18c8
#define CLK_CON_DIV_CLKCMU_PDP_BUS			0x18cc
#define CLK_CON_DIV_CLKCMU_PDP_VRA			0x18d0
#define CLK_CON_DIV_CLKCMU_PERIC0_BUS			0x18d4
#define CLK_CON_DIV_CLKCMU_PERIC0_IP			0x18d8
#define CLK_CON_DIV_CLKCMU_PERIC1_BUS			0x18dc
#define CLK_CON_DIV_CLKCMU_PERIC1_IP			0x18e0
#define CLK_CON_DIV_CLKCMU_TNR_BUS			0x18e4
#define CLK_CON_DIV_CLKCMU_TPU_BUS			0x18e8
#define CLK_CON_DIV_CLKCMU_TPU_TPU			0x18ec
#define CLK_CON_DIV_CLKCMU_TPU_TPUCTL			0x18f0
#define CLK_CON_DIV_CLKCMU_TPU_UART			0x18f4
#define CLK_CON_DIV_DIV_CLKCMU_CMU_BOOST		0x18f8
#define CLK_CON_DIV_DIV_CLK_CMU_CMUREF			0x18fc
#define CLK_CON_DIV_PLL_SHARED0_DIV2			0x1900
#define CLK_CON_DIV_PLL_SHARED0_DIV3			0x1904
#define CLK_CON_DIV_PLL_SHARED0_DIV4			0x1908
#define CLK_CON_DIV_PLL_SHARED0_DIV5			0x190c
#define CLK_CON_DIV_PLL_SHARED1_DIV2			0x1910
#define CLK_CON_DIV_PLL_SHARED1_DIV3			0x1914
#define CLK_CON_DIV_PLL_SHARED1_DIV4			0x1918
#define CLK_CON_DIV_PLL_SHARED2_DIV2			0x191c
#define CLK_CON_DIV_PLL_SHARED3_DIV2			0x1920
#define CLK_CON_GAT_CLKCMU_BUS0_BOOST			0x2000
#define CLK_CON_GAT_CLKCMU_BUS1_BOOST			0x2004
#define CLK_CON_GAT_CLKCMU_BUS2_BOOST			0x2008
#define CLK_CON_GAT_CLKCMU_CORE_BOOST			0x200c
#define CLK_CON_GAT_CLKCMU_CPUCL0_BOOST			0x2010
#define CLK_CON_GAT_CLKCMU_CPUCL1_BOOST			0x2014
#define CLK_CON_GAT_CLKCMU_CPUCL2_BOOST			0x2018
#define CLK_CON_GAT_CLKCMU_MIF_BOOST			0x201c
#define CLK_CON_GAT_CLKCMU_MIF_SWITCH			0x2020
#define CLK_CON_GAT_GATE_CLKCMU_BO_BUS			0x2024
#define CLK_CON_GAT_GATE_CLKCMU_BUS0_BUS		0x2028
#define CLK_CON_GAT_GATE_CLKCMU_BUS1_BUS		0x202c
#define CLK_CON_GAT_GATE_CLKCMU_BUS2_BUS		0x2030
#define CLK_CON_GAT_GATE_CLKCMU_CIS_CLK0		0x2034
#define CLK_CON_GAT_GATE_CLKCMU_CIS_CLK1		0x2038
#define CLK_CON_GAT_GATE_CLKCMU_CIS_CLK2		0x203c
#define CLK_CON_GAT_GATE_CLKCMU_CIS_CLK3		0x2040
#define CLK_CON_GAT_GATE_CLKCMU_CIS_CLK4		0x2044
#define CLK_CON_GAT_GATE_CLKCMU_CIS_CLK5		0x2048
#define CLK_CON_GAT_GATE_CLKCMU_CIS_CLK6		0x204c
#define CLK_CON_GAT_GATE_CLKCMU_CIS_CLK7		0x2050
#define CLK_CON_GAT_GATE_CLKCMU_CMU_BOOST		0x2054
#define CLK_CON_GAT_GATE_CLKCMU_CORE_BUS		0x2058
#define CLK_CON_GAT_GATE_CLKCMU_CPUCL0_DBG_BUS		0x205c
#define CLK_CON_GAT_GATE_CLKCMU_CPUCL0_SWITCH		0x2060
#define CLK_CON_GAT_GATE_CLKCMU_CPUCL1_SWITCH		0x2064
#define CLK_CON_GAT_GATE_CLKCMU_CPUCL2_SWITCH		0x2068
#define CLK_CON_GAT_GATE_CLKCMU_CSIS_BUS		0x206c
#define CLK_CON_GAT_GATE_CLKCMU_DISP_BUS		0x2070
#define CLK_CON_GAT_GATE_CLKCMU_DNS_BUS			0x2074
#define CLK_CON_GAT_GATE_CLKCMU_DPU_BUS			0x2078
#define CLK_CON_GAT_GATE_CLKCMU_EH_BUS			0x207c
#define CLK_CON_GAT_GATE_CLKCMU_G2D_G2D			0x2080
#define CLK_CON_GAT_GATE_CLKCMU_G2D_MSCL		0x2084
#define CLK_CON_GAT_GATE_CLKCMU_G3AA_G3AA		0x2088
#define CLK_CON_GAT_GATE_CLKCMU_G3D_BUSD		0x208c
#define CLK_CON_GAT_GATE_CLKCMU_G3D_GLB			0x2090
#define CLK_CON_GAT_GATE_CLKCMU_G3D_SWITCH		0x2094
#define CLK_CON_GAT_GATE_CLKCMU_GDC_GDC0		0x2098
#define CLK_CON_GAT_GATE_CLKCMU_GDC_GDC1		0x209c
#define CLK_CON_GAT_GATE_CLKCMU_GDC_SCSC		0x20a0
#define CLK_CON_GAT_GATE_CLKCMU_HPM			0x20a4
#define CLK_CON_GAT_GATE_CLKCMU_HSI0_BUS		0x20a8
#define CLK_CON_GAT_GATE_CLKCMU_HSI0_DPGTC		0x20ac
#define CLK_CON_GAT_GATE_CLKCMU_HSI0_USB31DRD		0x20b0
#define CLK_CON_GAT_GATE_CLKCMU_HSI0_USBDPDBG		0x20b4
#define CLK_CON_GAT_GATE_CLKCMU_HSI1_BUS		0x20b8
#define CLK_CON_GAT_GATE_CLKCMU_HSI1_PCIE		0x20bc
#define CLK_CON_GAT_GATE_CLKCMU_HSI2_BUS		0x20c0
#define CLK_CON_GAT_GATE_CLKCMU_HSI2_MMCCARD		0x20c4
#define CLK_CON_GAT_GATE_CLKCMU_HSI2_PCIE		0x20c8
#define CLK_CON_GAT_GATE_CLKCMU_HSI2_UFS_EMBD		0x20cc
#define CLK_CON_GAT_GATE_CLKCMU_IPP_BUS			0x20d0
#define CLK_CON_GAT_GATE_CLKCMU_ITP_BUS			0x20d4
#define CLK_CON_GAT_GATE_CLKCMU_MCSC_ITSC		0x20d8
#define CLK_CON_GAT_GATE_CLKCMU_MCSC_MCSC		0x20dc
#define CLK_CON_GAT_GATE_CLKCMU_MFC_MFC			0x20e0
#define CLK_CON_GAT_GATE_CLKCMU_MIF_BUSP		0x20e4
#define CLK_CON_GAT_GATE_CLKCMU_MISC_BUS		0x20e8
#define CLK_CON_GAT_GATE_CLKCMU_MISC_SSS		0x20ec
#define CLK_CON_GAT_GATE_CLKCMU_PDP_BUS			0x20f0
#define CLK_CON_GAT_GATE_CLKCMU_PDP_VRA			0x20f4
#define CLK_CON_GAT_GATE_CLKCMU_PERIC0_BUS		0x20f8
#define CLK_CON_GAT_GATE_CLKCMU_PERIC0_IP		0x20fc
#define CLK_CON_GAT_GATE_CLKCMU_PERIC1_BUS		0x2100
#define CLK_CON_GAT_GATE_CLKCMU_PERIC1_IP		0x2104
#define CLK_CON_GAT_GATE_CLKCMU_TNR_BUS			0x2108
#define CLK_CON_GAT_GATE_CLKCMU_TOP_CMUREF		0x210c
#define CLK_CON_GAT_GATE_CLKCMU_TPU_BUS			0x2110
#define CLK_CON_GAT_GATE_CLKCMU_TPU_TPU			0x2114
#define CLK_CON_GAT_GATE_CLKCMU_TPU_TPUCTL		0x2118
#define CLK_CON_GAT_GATE_CLKCMU_TPU_UART		0x211c
#define DMYQCH_CON_CMU_TOP_CMUREF_QCH			0x3000
#define DMYQCH_CON_DFTMUX_CMU_QCH_CIS_CLK0		0x3004
#define DMYQCH_CON_DFTMUX_CMU_QCH_CIS_CLK1		0x3008
#define DMYQCH_CON_DFTMUX_CMU_QCH_CIS_CLK2		0x300c
#define DMYQCH_CON_DFTMUX_CMU_QCH_CIS_CLK3		0x3010
#define DMYQCH_CON_DFTMUX_CMU_QCH_CIS_CLK4		0x3014
#define DMYQCH_CON_DFTMUX_CMU_QCH_CIS_CLK5		0x3018
#define DMYQCH_CON_DFTMUX_CMU_QCH_CIS_CLK6		0x301c
#define DMYQCH_CON_DFTMUX_CMU_QCH_CIS_CLK7		0x3020
#define DMYQCH_CON_OTP_QCH				0x3024
#define QUEUE_CTRL_REG_BLK_CMU_CMU_TOP			0x3c00
#define QUEUE_ENTRY0_BLK_CMU_CMU_TOP			0x3c10
#define QUEUE_ENTRY1_BLK_CMU_CMU_TOP			0x3c14
#define QUEUE_ENTRY2_BLK_CMU_CMU_TOP			0x3c18
#define QUEUE_ENTRY3_BLK_CMU_CMU_TOP			0x3c1c
#define QUEUE_ENTRY4_BLK_CMU_CMU_TOP			0x3c20
#define QUEUE_ENTRY5_BLK_CMU_CMU_TOP			0x3c24
#define QUEUE_ENTRY6_BLK_CMU_CMU_TOP			0x3c28
#define QUEUE_ENTRY7_BLK_CMU_CMU_TOP			0x3c2c
#define MIFMIRROR_QUEUE_CTRL_REG			0x3e00
#define MIFMIRROR_QUEUE_ENTRY0				0x3e10
#define MIFMIRROR_QUEUE_ENTRY1				0x3e14
#define MIFMIRROR_QUEUE_ENTRY2				0x3e18
#define MIFMIRROR_QUEUE_ENTRY3				0x3e1c
#define MIFMIRROR_QUEUE_ENTRY4				0x3e20
#define MIFMIRROR_QUEUE_ENTRY5				0x3e24
#define MIFMIRROR_QUEUE_ENTRY6				0x3e28
#define MIFMIRROR_QUEUE_ENTRY7				0x3e2c
#define MIFMIRROR_QUEUE_BUSY				0x3e30
#define GENERALIO_ACD_CHANNEL_0				0x3f00
#define GENERALIO_ACD_CHANNEL_1				0x3f04
#define GENERALIO_ACD_CHANNEL_2				0x3f08
#define GENERALIO_ACD_CHANNEL_3				0x3f0c
#define GENERALIO_ACD_MASK				0x3f14

static const unsigned long cmu_top_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_SHARED0,
	PLL_LOCKTIME_PLL_SHARED1,
	PLL_LOCKTIME_PLL_SHARED2,
	PLL_LOCKTIME_PLL_SHARED3,
	PLL_LOCKTIME_PLL_SPARE,
	PLL_CON0_PLL_SHARED0,
	PLL_CON1_PLL_SHARED0,
	PLL_CON2_PLL_SHARED0,
	PLL_CON3_PLL_SHARED0,
	PLL_CON4_PLL_SHARED0,
	PLL_CON0_PLL_SHARED1,
	PLL_CON1_PLL_SHARED1,
	PLL_CON2_PLL_SHARED1,
	PLL_CON3_PLL_SHARED1,
	PLL_CON4_PLL_SHARED1,
	PLL_CON0_PLL_SHARED2,
	PLL_CON1_PLL_SHARED2,
	PLL_CON2_PLL_SHARED2,
	PLL_CON3_PLL_SHARED2,
	PLL_CON4_PLL_SHARED2,
	PLL_CON0_PLL_SHARED3,
	PLL_CON1_PLL_SHARED3,
	PLL_CON2_PLL_SHARED3,
	PLL_CON3_PLL_SHARED3,
	PLL_CON4_PLL_SHARED3,
	PLL_CON0_PLL_SPARE,
	PLL_CON1_PLL_SPARE,
	PLL_CON2_PLL_SPARE,
	PLL_CON3_PLL_SPARE,
	PLL_CON4_PLL_SPARE,
	CMU_CMU_TOP_CONTROLLER_OPTION,
	CLKOUT_CON_BLK_CMU_CMU_TOP_CLKOUT0,
	CMU_HCHGEN_CLKMUX_CMU_BOOST,
	CMU_HCHGEN_CLKMUX_TOP_BOOST,
	CMU_HCHGEN_CLKMUX,
	POWER_FAIL_DETECT_PLL,
	EARLY_WAKEUP_FORCED_0_ENABLE,
	EARLY_WAKEUP_FORCED_1_ENABLE,
	EARLY_WAKEUP_APM_CTRL,
	EARLY_WAKEUP_CLUSTER0_CTRL,
	EARLY_WAKEUP_DPU_CTRL,
	EARLY_WAKEUP_CSIS_CTRL,
	EARLY_WAKEUP_APM_DEST,
	EARLY_WAKEUP_CLUSTER0_DEST,
	EARLY_WAKEUP_DPU_DEST,
	EARLY_WAKEUP_CSIS_DEST,
	EARLY_WAKEUP_SW_TRIG_APM,
	EARLY_WAKEUP_SW_TRIG_CLUSTER0,
	EARLY_WAKEUP_SW_TRIG_DPU,
	EARLY_WAKEUP_SW_TRIG_CSIS,
	CLK_CON_MUX_MUX_CLKCMU_BO_BUS,
	CLK_CON_MUX_MUX_CLKCMU_BUS0_BUS,
	CLK_CON_MUX_MUX_CLKCMU_BUS1_BUS,
	CLK_CON_MUX_MUX_CLKCMU_BUS2_BUS,
	CLK_CON_MUX_MUX_CLKCMU_CIS_CLK0,
	CLK_CON_MUX_MUX_CLKCMU_CIS_CLK1,
	CLK_CON_MUX_MUX_CLKCMU_CIS_CLK2,
	CLK_CON_MUX_MUX_CLKCMU_CIS_CLK3,
	CLK_CON_MUX_MUX_CLKCMU_CIS_CLK4,
	CLK_CON_MUX_MUX_CLKCMU_CIS_CLK5,
	CLK_CON_MUX_MUX_CLKCMU_CIS_CLK6,
	CLK_CON_MUX_MUX_CLKCMU_CIS_CLK7,
	CLK_CON_MUX_MUX_CLKCMU_CMU_BOOST,
	CLK_CON_MUX_MUX_CLKCMU_CMU_BOOST_OPTION1,
	CLK_CON_MUX_MUX_CLKCMU_CORE_BUS,
	CLK_CON_MUX_MUX_CLKCMU_CPUCL0_DBG,
	CLK_CON_MUX_MUX_CLKCMU_CPUCL0_SWITCH,
	CLK_CON_MUX_MUX_CLKCMU_CPUCL1_SWITCH,
	CLK_CON_MUX_MUX_CLKCMU_CPUCL2_SWITCH,
	CLK_CON_MUX_MUX_CLKCMU_CSIS_BUS,
	CLK_CON_MUX_MUX_CLKCMU_DISP_BUS,
	CLK_CON_MUX_MUX_CLKCMU_DNS_BUS,
	CLK_CON_MUX_MUX_CLKCMU_DPU_BUS,
	CLK_CON_MUX_MUX_CLKCMU_EH_BUS,
	CLK_CON_MUX_MUX_CLKCMU_G2D_G2D,
	CLK_CON_MUX_MUX_CLKCMU_G2D_MSCL,
	CLK_CON_MUX_MUX_CLKCMU_G3AA_G3AA,
	CLK_CON_MUX_MUX_CLKCMU_G3D_BUSD,
	CLK_CON_MUX_MUX_CLKCMU_G3D_GLB,
	CLK_CON_MUX_MUX_CLKCMU_G3D_SWITCH,
	CLK_CON_MUX_MUX_CLKCMU_GDC_GDC0,
	CLK_CON_MUX_MUX_CLKCMU_GDC_GDC1,
	CLK_CON_MUX_MUX_CLKCMU_GDC_SCSC,
	CLK_CON_MUX_MUX_CLKCMU_HPM,
	CLK_CON_MUX_MUX_CLKCMU_HSI0_BUS,
	CLK_CON_MUX_MUX_CLKCMU_HSI0_DPGTC,
	CLK_CON_MUX_MUX_CLKCMU_HSI0_USB31DRD,
	CLK_CON_MUX_MUX_CLKCMU_HSI0_USBDPDBG,
	CLK_CON_MUX_MUX_CLKCMU_HSI1_BUS,
	CLK_CON_MUX_MUX_CLKCMU_HSI1_PCIE,
	CLK_CON_MUX_MUX_CLKCMU_HSI2_BUS,
	CLK_CON_MUX_MUX_CLKCMU_HSI2_MMC_CARD,
	CLK_CON_MUX_MUX_CLKCMU_HSI2_PCIE,
	CLK_CON_MUX_MUX_CLKCMU_HSI2_UFS_EMBD,
	CLK_CON_MUX_MUX_CLKCMU_IPP_BUS,
	CLK_CON_MUX_MUX_CLKCMU_ITP_BUS,
	CLK_CON_MUX_MUX_CLKCMU_MCSC_ITSC,
	CLK_CON_MUX_MUX_CLKCMU_MCSC_MCSC,
	CLK_CON_MUX_MUX_CLKCMU_MFC_MFC,
	CLK_CON_MUX_MUX_CLKCMU_MIF_BUSP,
	CLK_CON_MUX_MUX_CLKCMU_MIF_SWITCH,
	CLK_CON_MUX_MUX_CLKCMU_MISC_BUS,
	CLK_CON_MUX_MUX_CLKCMU_MISC_SSS,
	CLK_CON_MUX_MUX_CLKCMU_PDP_BUS,
	CLK_CON_MUX_MUX_CLKCMU_PDP_VRA,
	CLK_CON_MUX_MUX_CLKCMU_PERIC0_BUS,
	CLK_CON_MUX_MUX_CLKCMU_PERIC0_IP,
	CLK_CON_MUX_MUX_CLKCMU_PERIC1_BUS,
	CLK_CON_MUX_MUX_CLKCMU_PERIC1_IP,
	CLK_CON_MUX_MUX_CLKCMU_TNR_BUS,
	CLK_CON_MUX_MUX_CLKCMU_TOP_BOOST_OPTION1,
	CLK_CON_MUX_MUX_CLKCMU_TOP_CMUREF,
	CLK_CON_MUX_MUX_CLKCMU_TPU_BUS,
	CLK_CON_MUX_MUX_CLKCMU_TPU_TPU,
	CLK_CON_MUX_MUX_CLKCMU_TPU_TPUCTL,
	CLK_CON_MUX_MUX_CLKCMU_TPU_UART,
	CLK_CON_MUX_MUX_CMU_CMUREF,
	CLK_CON_DIV_CLKCMU_BO_BUS,
	CLK_CON_DIV_CLKCMU_BUS0_BUS,
	CLK_CON_DIV_CLKCMU_BUS1_BUS,
	CLK_CON_DIV_CLKCMU_BUS2_BUS,
	CLK_CON_DIV_CLKCMU_CIS_CLK0,
	CLK_CON_DIV_CLKCMU_CIS_CLK1,
	CLK_CON_DIV_CLKCMU_CIS_CLK2,
	CLK_CON_DIV_CLKCMU_CIS_CLK3,
	CLK_CON_DIV_CLKCMU_CIS_CLK4,
	CLK_CON_DIV_CLKCMU_CIS_CLK5,
	CLK_CON_DIV_CLKCMU_CIS_CLK6,
	CLK_CON_DIV_CLKCMU_CIS_CLK7,
	CLK_CON_DIV_CLKCMU_CORE_BUS,
	CLK_CON_DIV_CLKCMU_CPUCL0_DBG,
	CLK_CON_DIV_CLKCMU_CPUCL0_SWITCH,
	CLK_CON_DIV_CLKCMU_CPUCL1_SWITCH,
	CLK_CON_DIV_CLKCMU_CPUCL2_SWITCH,
	CLK_CON_DIV_CLKCMU_CSIS_BUS,
	CLK_CON_DIV_CLKCMU_DISP_BUS,
	CLK_CON_DIV_CLKCMU_DNS_BUS,
	CLK_CON_DIV_CLKCMU_DPU_BUS,
	CLK_CON_DIV_CLKCMU_EH_BUS,
	CLK_CON_DIV_CLKCMU_G2D_G2D,
	CLK_CON_DIV_CLKCMU_G2D_MSCL,
	CLK_CON_DIV_CLKCMU_G3AA_G3AA,
	CLK_CON_DIV_CLKCMU_G3D_BUSD,
	CLK_CON_DIV_CLKCMU_G3D_GLB,
	CLK_CON_DIV_CLKCMU_G3D_SWITCH,
	CLK_CON_DIV_CLKCMU_GDC_GDC0,
	CLK_CON_DIV_CLKCMU_GDC_GDC1,
	CLK_CON_DIV_CLKCMU_GDC_SCSC,
	CLK_CON_DIV_CLKCMU_HPM,
	CLK_CON_DIV_CLKCMU_HSI0_BUS,
	CLK_CON_DIV_CLKCMU_HSI0_DPGTC,
	CLK_CON_DIV_CLKCMU_HSI0_USB31DRD,
	CLK_CON_DIV_CLKCMU_HSI0_USBDPDBG,
	CLK_CON_DIV_CLKCMU_HSI1_BUS,
	CLK_CON_DIV_CLKCMU_HSI1_PCIE,
	CLK_CON_DIV_CLKCMU_HSI2_BUS,
	CLK_CON_DIV_CLKCMU_HSI2_MMC_CARD,
	CLK_CON_DIV_CLKCMU_HSI2_PCIE,
	CLK_CON_DIV_CLKCMU_HSI2_UFS_EMBD,
	CLK_CON_DIV_CLKCMU_IPP_BUS,
	CLK_CON_DIV_CLKCMU_ITP_BUS,
	CLK_CON_DIV_CLKCMU_MCSC_ITSC,
	CLK_CON_DIV_CLKCMU_MCSC_MCSC,
	CLK_CON_DIV_CLKCMU_MFC_MFC,
	CLK_CON_DIV_CLKCMU_MIF_BUSP,
	CLK_CON_DIV_CLKCMU_MISC_BUS,
	CLK_CON_DIV_CLKCMU_MISC_SSS,
	CLK_CON_DIV_CLKCMU_OTP,
	CLK_CON_DIV_CLKCMU_PDP_BUS,
	CLK_CON_DIV_CLKCMU_PDP_VRA,
	CLK_CON_DIV_CLKCMU_PERIC0_BUS,
	CLK_CON_DIV_CLKCMU_PERIC0_IP,
	CLK_CON_DIV_CLKCMU_PERIC1_BUS,
	CLK_CON_DIV_CLKCMU_PERIC1_IP,
	CLK_CON_DIV_CLKCMU_TNR_BUS,
	CLK_CON_DIV_CLKCMU_TPU_BUS,
	CLK_CON_DIV_CLKCMU_TPU_TPU,
	CLK_CON_DIV_CLKCMU_TPU_TPUCTL,
	CLK_CON_DIV_CLKCMU_TPU_UART,
	CLK_CON_DIV_DIV_CLKCMU_CMU_BOOST,
	CLK_CON_DIV_DIV_CLK_CMU_CMUREF,
	CLK_CON_DIV_PLL_SHARED0_DIV2,
	CLK_CON_DIV_PLL_SHARED0_DIV3,
	CLK_CON_DIV_PLL_SHARED0_DIV4,
	CLK_CON_DIV_PLL_SHARED0_DIV5,
	CLK_CON_DIV_PLL_SHARED1_DIV2,
	CLK_CON_DIV_PLL_SHARED1_DIV3,
	CLK_CON_DIV_PLL_SHARED1_DIV4,
	CLK_CON_DIV_PLL_SHARED2_DIV2,
	CLK_CON_DIV_PLL_SHARED3_DIV2,
	CLK_CON_GAT_CLKCMU_BUS0_BOOST,
	CLK_CON_GAT_CLKCMU_BUS1_BOOST,
	CLK_CON_GAT_CLKCMU_BUS2_BOOST,
	CLK_CON_GAT_CLKCMU_CORE_BOOST,
	CLK_CON_GAT_CLKCMU_CPUCL0_BOOST,
	CLK_CON_GAT_CLKCMU_CPUCL1_BOOST,
	CLK_CON_GAT_CLKCMU_CPUCL2_BOOST,
	CLK_CON_GAT_CLKCMU_MIF_BOOST,
	CLK_CON_GAT_CLKCMU_MIF_SWITCH,
	CLK_CON_GAT_GATE_CLKCMU_BO_BUS,
	CLK_CON_GAT_GATE_CLKCMU_BUS0_BUS,
	CLK_CON_GAT_GATE_CLKCMU_BUS1_BUS,
	CLK_CON_GAT_GATE_CLKCMU_BUS2_BUS,
	CLK_CON_GAT_GATE_CLKCMU_CIS_CLK0,
	CLK_CON_GAT_GATE_CLKCMU_CIS_CLK1,
	CLK_CON_GAT_GATE_CLKCMU_CIS_CLK2,
	CLK_CON_GAT_GATE_CLKCMU_CIS_CLK3,
	CLK_CON_GAT_GATE_CLKCMU_CIS_CLK4,
	CLK_CON_GAT_GATE_CLKCMU_CIS_CLK5,
	CLK_CON_GAT_GATE_CLKCMU_CIS_CLK6,
	CLK_CON_GAT_GATE_CLKCMU_CIS_CLK7,
	CLK_CON_GAT_GATE_CLKCMU_CMU_BOOST,
	CLK_CON_GAT_GATE_CLKCMU_CORE_BUS,
	CLK_CON_GAT_GATE_CLKCMU_CPUCL0_DBG_BUS,
	CLK_CON_GAT_GATE_CLKCMU_CPUCL0_SWITCH,
	CLK_CON_GAT_GATE_CLKCMU_CPUCL1_SWITCH,
	CLK_CON_GAT_GATE_CLKCMU_CPUCL2_SWITCH,
	CLK_CON_GAT_GATE_CLKCMU_CSIS_BUS,
	CLK_CON_GAT_GATE_CLKCMU_DISP_BUS,
	CLK_CON_GAT_GATE_CLKCMU_DNS_BUS,
	CLK_CON_GAT_GATE_CLKCMU_DPU_BUS,
	CLK_CON_GAT_GATE_CLKCMU_EH_BUS,
	CLK_CON_GAT_GATE_CLKCMU_G2D_G2D,
	CLK_CON_GAT_GATE_CLKCMU_G2D_MSCL,
	CLK_CON_GAT_GATE_CLKCMU_G3AA_G3AA,
	CLK_CON_GAT_GATE_CLKCMU_G3D_BUSD,
	CLK_CON_GAT_GATE_CLKCMU_G3D_GLB,
	CLK_CON_GAT_GATE_CLKCMU_G3D_SWITCH,
	CLK_CON_GAT_GATE_CLKCMU_GDC_GDC0,
	CLK_CON_GAT_GATE_CLKCMU_GDC_GDC1,
	CLK_CON_GAT_GATE_CLKCMU_GDC_SCSC,
	CLK_CON_GAT_GATE_CLKCMU_HPM,
	CLK_CON_GAT_GATE_CLKCMU_HSI0_BUS,
	CLK_CON_GAT_GATE_CLKCMU_HSI0_DPGTC,
	CLK_CON_GAT_GATE_CLKCMU_HSI0_USB31DRD,
	CLK_CON_GAT_GATE_CLKCMU_HSI0_USBDPDBG,
	CLK_CON_GAT_GATE_CLKCMU_HSI1_BUS,
	CLK_CON_GAT_GATE_CLKCMU_HSI1_PCIE,
	CLK_CON_GAT_GATE_CLKCMU_HSI2_BUS,
	CLK_CON_GAT_GATE_CLKCMU_HSI2_MMCCARD,
	CLK_CON_GAT_GATE_CLKCMU_HSI2_PCIE,
	CLK_CON_GAT_GATE_CLKCMU_HSI2_UFS_EMBD,
	CLK_CON_GAT_GATE_CLKCMU_IPP_BUS,
	CLK_CON_GAT_GATE_CLKCMU_ITP_BUS,
	CLK_CON_GAT_GATE_CLKCMU_MCSC_ITSC,
	CLK_CON_GAT_GATE_CLKCMU_MCSC_MCSC,
	CLK_CON_GAT_GATE_CLKCMU_MFC_MFC,
	CLK_CON_GAT_GATE_CLKCMU_MIF_BUSP,
	CLK_CON_GAT_GATE_CLKCMU_MISC_BUS,
	CLK_CON_GAT_GATE_CLKCMU_MISC_SSS,
	CLK_CON_GAT_GATE_CLKCMU_PDP_BUS,
	CLK_CON_GAT_GATE_CLKCMU_PDP_VRA,
	CLK_CON_GAT_GATE_CLKCMU_PERIC0_BUS,
	CLK_CON_GAT_GATE_CLKCMU_PERIC0_IP,
	CLK_CON_GAT_GATE_CLKCMU_PERIC1_BUS,
	CLK_CON_GAT_GATE_CLKCMU_PERIC1_IP,
	CLK_CON_GAT_GATE_CLKCMU_TNR_BUS,
	CLK_CON_GAT_GATE_CLKCMU_TOP_CMUREF,
	CLK_CON_GAT_GATE_CLKCMU_TPU_BUS,
	CLK_CON_GAT_GATE_CLKCMU_TPU_TPU,
	CLK_CON_GAT_GATE_CLKCMU_TPU_TPUCTL,
	CLK_CON_GAT_GATE_CLKCMU_TPU_UART,
	DMYQCH_CON_CMU_TOP_CMUREF_QCH,
	DMYQCH_CON_DFTMUX_CMU_QCH_CIS_CLK0,
	DMYQCH_CON_DFTMUX_CMU_QCH_CIS_CLK1,
	DMYQCH_CON_DFTMUX_CMU_QCH_CIS_CLK2,
	DMYQCH_CON_DFTMUX_CMU_QCH_CIS_CLK3,
	DMYQCH_CON_DFTMUX_CMU_QCH_CIS_CLK4,
	DMYQCH_CON_DFTMUX_CMU_QCH_CIS_CLK5,
	DMYQCH_CON_DFTMUX_CMU_QCH_CIS_CLK6,
	DMYQCH_CON_DFTMUX_CMU_QCH_CIS_CLK7,
	DMYQCH_CON_OTP_QCH,
	QUEUE_CTRL_REG_BLK_CMU_CMU_TOP,
	QUEUE_ENTRY0_BLK_CMU_CMU_TOP,
	QUEUE_ENTRY1_BLK_CMU_CMU_TOP,
	QUEUE_ENTRY2_BLK_CMU_CMU_TOP,
	QUEUE_ENTRY3_BLK_CMU_CMU_TOP,
	QUEUE_ENTRY4_BLK_CMU_CMU_TOP,
	QUEUE_ENTRY5_BLK_CMU_CMU_TOP,
	QUEUE_ENTRY6_BLK_CMU_CMU_TOP,
	QUEUE_ENTRY7_BLK_CMU_CMU_TOP,
	MIFMIRROR_QUEUE_CTRL_REG,
	MIFMIRROR_QUEUE_ENTRY0,
	MIFMIRROR_QUEUE_ENTRY1,
	MIFMIRROR_QUEUE_ENTRY2,
	MIFMIRROR_QUEUE_ENTRY3,
	MIFMIRROR_QUEUE_ENTRY4,
	MIFMIRROR_QUEUE_ENTRY5,
	MIFMIRROR_QUEUE_ENTRY6,
	MIFMIRROR_QUEUE_ENTRY7,
	MIFMIRROR_QUEUE_BUSY,
	GENERALIO_ACD_CHANNEL_0,
	GENERALIO_ACD_CHANNEL_1,
	GENERALIO_ACD_CHANNEL_2,
	GENERALIO_ACD_CHANNEL_3,
	GENERALIO_ACD_MASK,
};

static const struct samsung_pll_clock cmu_top_pll_clks[] __initconst = {
	/* CMU_TOP_PURECLKCOMP */
	PLL(pll_0517x, CLK_FOUT_SHARED0_PLL, "fout_shared0_pll", "oscclk",
	    PLL_LOCKTIME_PLL_SHARED0, PLL_CON3_PLL_SHARED0,
	    NULL),
	PLL(pll_0517x, CLK_FOUT_SHARED1_PLL, "fout_shared1_pll", "oscclk",
	    PLL_LOCKTIME_PLL_SHARED1, PLL_CON3_PLL_SHARED1,
	    NULL),
	PLL(pll_0518x, CLK_FOUT_SHARED2_PLL, "fout_shared2_pll", "oscclk",
	    PLL_LOCKTIME_PLL_SHARED2, PLL_CON3_PLL_SHARED2,
	    NULL),
	PLL(pll_0518x, CLK_FOUT_SHARED3_PLL, "fout_shared3_pll", "oscclk",
	    PLL_LOCKTIME_PLL_SHARED3, PLL_CON3_PLL_SHARED3,
	    NULL),
	PLL(pll_0518x, CLK_FOUT_SPARE_PLL, "fout_spare_pll", "oscclk",
	    PLL_LOCKTIME_PLL_SPARE, PLL_CON3_PLL_SPARE,
	    NULL),
};

/* List of parent clocks for Muxes in CMU_TOP */
PNAME(mout_pll_shared0_p)	= { "oscclk", "fout_shared0_pll" };
PNAME(mout_pll_shared1_p)	= { "oscclk", "fout_shared1_pll" };
PNAME(mout_pll_shared2_p)	= { "oscclk", "fout_shared2_pll" };
PNAME(mout_pll_shared3_p)	= { "oscclk", "fout_shared3_pll" };
PNAME(mout_pll_spare_p)		= { "oscclk", "fout_spare_pll" };
PNAME(mout_cmu_bo_bus_p)	= { "fout_shared2_pll", "dout_cmu_shared0_div3",
				    "fout_shared3_pll", "dout_cmu_shared1_div3",
				    "dout_cmu_shared0_div4",
				    "dout_cmu_shared1_div4",
				    "fout_spare_pll", "oscclk" };
PNAME(mout_cmu_bus0_bus_p)	= { "dout_cmu_shared0_div4",
				    "dout_cmu_shared1_div4",
				    "dout_cmu_shared2_div2",
				    "dout_cmu_shared3_div2",
				    "fout_spare_pll", "oscclk",
				    "oscclk", "oscclk" };
PNAME(mout_cmu_bus1_bus_p)	= { "dout_cmu_shared0_div3", "fout_shared3_pll",
				    "dout_cmu_shared1_div3",
				    "dout_cmu_shared0_div4",
				    "dout_cmu_shared1_div4",
				    "dout_cmu_shared2_div2",
				    "fout_spare_pll", "oscclk" };
PNAME(mout_cmu_bus2_bus_p)	= { "dout_cmu_shared0_div2",
				    "dout_cmu_shared1_div2",
				    "fout_shared2_pll", "fout_shared3_pll",
				    "dout_cmu_shared0_div3",
				    "dout_cmu_shared1_div3",
				    "dout_cmu_shared0_div5", "fout_spare_pll" };
PNAME(mout_cmu_cis_clk0_7_p)	= { "oscclk", "dout_cmu_shared0_div3",
				    "dout_cmu_shared1_div3",
				    "dout_cmu_shared2_div2",
				    "dout_cmu_shared3_div2", "fout_spare_pll",
				    "oscclk", "oscclk" };
PNAME(mout_cmu_cmu_boost_p)	= { "dout_cmu_shared0_div4",
				    "dout_cmu_shared1_div4",
				    "dout_cmu_shared2_div2",
				    "dout_cmu_shared3_div2" };
PNAME(mout_cmu_cmu_boost_option1_p) = { "dout_cmu_cmu_boost",
					"gout_cmu_boost_option1" };
PNAME(mout_cmu_core_bus_p)	= { "dout_cmu_shared0_div2",
				    "dout_cmu_shared1_div2",
				    "fout_shared2_pll", "fout_shared3_pll",
				    "dout_cmu_shared0_div3",
				    "dout_cmu_shared1_div3",
				    "dout_cmu_shared0_div5", "fout_spare_pll" };
PNAME(mout_cmu_cpucl0_dbg_p)	= { "fout_shared2_pll", "fout_shared3_pll",
				    "dout_cmu_shared0_div4",
				    "dout_cmu_shared1_div4",
				    "dout_cmu_shared2_div2", "fout_spare_pll",
				    "oscclk", "oscclk" };
PNAME(mout_cmu_cpucl0_switch_p)	= { "fout_shared1_pll", "dout_cmu_shared0_div2",
				    "dout_cmu_shared1_div2", "fout_shared2_pll",
				    "fout_shared3_pll", "dout_cmu_shared0_div3",
				    "dout_cmu_shared1_div3", "fout_spare_pll" };
PNAME(mout_cmu_cpucl1_switch_p)	= { "fout_shared1_pll", "dout_cmu_shared0_div2",
				    "dout_cmu_shared1_div2", "fout_shared2_pll",
				    "fout_shared3_pll", "dout_cmu_shared0_div3",
				    "dout_cmu_shared1_div3", "fout_spare_pll" };
PNAME(mout_cmu_cpucl2_switch_p)	= { "fout_shared1_pll", "dout_cmu_shared0_div2",
				    "dout_cmu_shared1_div2", "fout_shared2_pll",
				    "fout_shared3_pll", "dout_cmu_shared0_div3",
				    "dout_cmu_shared1_div3", "fout_spare_pll" };
PNAME(mout_cmu_csis_bus_p)	= { "dout_cmu_shared0_div3", "fout_shared3_pll",
				    "dout_cmu_shared1_div3",
				    "dout_cmu_shared0_div4",
				    "dout_cmu_shared1_div4",
				    "dout_cmu_shared2_div2",
				    "fout_spare_pll", "oscclk" };
PNAME(mout_cmu_disp_bus_p)	= { "dout_cmu_shared0_div3", "fout_shared3_pll",
				    "dout_cmu_shared1_div3",
				    "dout_cmu_shared0_div4",
				    "dout_cmu_shared1_div4",
				    "dout_cmu_shared2_div2",
				    "fout_spare_pll", "oscclk" };
PNAME(mout_cmu_dns_bus_p)	= { "dout_cmu_shared0_div3", "fout_shared3_pll",
				    "dout_cmu_shared1_div3",
				    "dout_cmu_shared0_div4",
				    "dout_cmu_shared1_div4",
				    "dout_cmu_shared2_div2",
				    "fout_spare_pll", "oscclk" };
PNAME(mout_cmu_dpu_p)		= { "dout_cmu_shared0_div3",
				    "fout_shared3_pll",
				    "dout_cmu_shared1_div3",
				    "dout_cmu_shared0_div4",
				    "dout_cmu_shared1_div4",
				    "dout_cmu_shared2_div2",
				    "fout_spare_pll", "oscclk" };
PNAME(mout_cmu_eh_bus_p)	= { "dout_cmu_shared0_div2",
				    "dout_cmu_shared1_div2",
				    "fout_shared2_pll", "fout_shared3_pll",
				    "dout_cmu_shared0_div3",
				    "dout_cmu_shared1_div3",
				    "dout_cmu_shared0_div5", "fout_spare_pll" };
PNAME(mout_cmu_g2d_g2d_p)	= { "dout_cmu_shared0_div3", "fout_shared3_pll",
				    "dout_cmu_shared1_div3",
				    "dout_cmu_shared0_div4",
				    "dout_cmu_shared1_div4",
				    "dout_cmu_shared2_div2",
				    "fout_spare_pll", "oscclk" };
PNAME(mout_cmu_g2d_mscl_p)	= { "dout_cmu_shared0_div4",
				    "dout_cmu_shared1_div4",
				    "dout_cmu_shared2_div2",
				    "dout_cmu_shared3_div2",
				    "fout_spare_pll", "oscclk",
				    "oscclk", "oscclk" };
PNAME(mout_cmu_g3aa_g3aa_p)	= { "dout_cmu_shared0_div3", "fout_shared3_pll",
				    "dout_cmu_shared1_div3",
				    "dout_cmu_shared0_div4",
				    "dout_cmu_shared1_div4",
				    "dout_cmu_shared2_div2",
				    "fout_spare_pll", "oscclk" };
PNAME(mout_cmu_g3d_busd_p)	= { "dout_cmu_shared0_div2",
				    "dout_cmu_shared1_div2",
				    "fout_shared2_pll", "fout_shared3_pll",
				    "dout_cmu_shared0_div3",
				    "dout_cmu_shared1_div3",
				    "dout_cmu_shared0_div4", "fout_spare_pll" };
PNAME(mout_cmu_g3d_glb_p)	= { "dout_cmu_shared0_div2",
				    "dout_cmu_shared1_div2",
				    "fout_shared2_pll", "fout_shared3_pll",
				    "dout_cmu_shared0_div3",
				    "dout_cmu_shared1_div3",
				    "dout_cmu_shared0_div4", "fout_spare_pll" };
PNAME(mout_cmu_g3d_switch_p)	= { "fout_shared2_pll", "dout_cmu_shared0_div3",
				    "fout_shared3_pll", "dout_cmu_shared1_div3",
				    "dout_cmu_shared0_div4",
				    "dout_cmu_shared1_div4",
				    "fout_spare_pll", "fout_spare_pll"};
PNAME(mout_cmu_gdc_gdc0_p)	= { "dout_cmu_shared0_div3", "fout_shared3_pll",
				    "dout_cmu_shared1_div3",
				    "dout_cmu_shared0_div4",
				    "dout_cmu_shared1_div4",
				    "dout_cmu_shared2_div2",
				    "fout_spare_pll", "oscclk" };
PNAME(mout_cmu_gdc_gdc1_p)	= { "dout_cmu_shared0_div3", "fout_shared3_pll",
				    "dout_cmu_shared1_div3",
				    "dout_cmu_shared0_div4",
				    "dout_cmu_shared1_div4",
				    "dout_cmu_shared2_div2",
				    "fout_spare_pll", "oscclk" };
PNAME(mout_cmu_gdc_scsc_p)	= { "dout_cmu_shared0_div3", "fout_shared3_pll",
				    "dout_cmu_shared1_div3",
				    "dout_cmu_shared0_div4",
				    "dout_cmu_shared1_div4",
				    "dout_cmu_shared2_div2",
				    "fout_spare_pll", "oscclk" };
PNAME(mout_cmu_hpm_p)		= { "oscclk", "dout_cmu_shared1_div3",
				    "dout_cmu_shared0_div4",
				    "dout_cmu_shared2_div2" };
PNAME(mout_cmu_hsi0_bus_p)	= { "dout_cmu_shared0_div4",
				    "dout_cmu_shared1_div4",
				    "dout_cmu_shared2_div2",
				    "dout_cmu_shared3_div2",
				    "fout_spare_pll", "oscclk",
				    "oscclk", "oscclk" };
PNAME(mout_cmu_hsi0_dpgtc_p)	= { "oscclk", "dout_cmu_shared0_div4",
				    "dout_cmu_shared2_div2", "fout_spare_pll" };
PNAME(mout_cmu_hsi0_usb31drd_p)	= { "oscclk", "dout_cmu_shared2_div2" };
PNAME(mout_cmu_hsi0_usbdpdbg_p)	= { "oscclk", "dout_cmu_shared2_div2" };
PNAME(mout_cmu_hsi1_bus_p)	= { "dout_cmu_shared0_div4",
				    "dout_cmu_shared1_div4",
				    "dout_cmu_shared2_div2",
				    "dout_cmu_shared3_div2",
				    "fout_spare_pll" };
PNAME(mout_cmu_hsi1_pcie_p)	= { "oscclk", "dout_cmu_shared2_div2" };
PNAME(mout_cmu_hsi2_bus_p)	= { "dout_cmu_shared0_div4",
				    "dout_cmu_shared1_div4",
				    "dout_cmu_shared2_div2",
				    "dout_cmu_shared3_div2",
				    "fout_spare_pll", "oscclk",
				    "oscclk", "oscclk" };
PNAME(mout_cmu_hsi2_mmc_card_p)	= { "fout_shared2_pll", "fout_shared3_pll",
				    "dout_cmu_shared0_div4", "fout_spare_pll" };
PNAME(mout_cmu_hsi2_pcie0_p)	= { "oscclk", "dout_cmu_shared2_div2" };
PNAME(mout_cmu_hsi2_ufs_embd_p)	= { "oscclk", "dout_cmu_shared0_div4",
				    "dout_cmu_shared2_div2", "fout_spare_pll" };
PNAME(mout_cmu_ipp_bus_p)	= { "dout_cmu_shared0_div3", "fout_shared3_pll",
				    "dout_cmu_shared1_div3",
				    "dout_cmu_shared0_div4",
				    "dout_cmu_shared1_div4",
				    "dout_cmu_shared2_div2",
				    "fout_spare_pll", "oscclk" };
PNAME(mout_cmu_itp_bus_p)	= { "dout_cmu_shared0_div3", "fout_shared3_pll",
				    "dout_cmu_shared1_div3",
				    "dout_cmu_shared0_div4",
				    "dout_cmu_shared1_div4",
				    "dout_cmu_shared2_div2",
				    "fout_spare_pll", "oscclk" };
PNAME(mout_cmu_mcsc_itsc_p)	= { "dout_cmu_shared0_div3", "fout_shared3_pll",
				    "dout_cmu_shared1_div3",
				    "dout_cmu_shared0_div4",
				    "dout_cmu_shared1_div4",
				    "dout_cmu_shared2_div2",
				    "fout_spare_pll", "oscclk" };
PNAME(mout_cmu_mcsc_mcsc_p)	= { "dout_cmu_shared0_div3", "fout_shared3_pll",
				    "dout_cmu_shared1_div3",
				    "dout_cmu_shared0_div4",
				    "dout_cmu_shared1_div4",
				    "dout_cmu_shared2_div2",
				    "fout_spare_pll", "oscclk" };
PNAME(mout_cmu_mfc_mfc_p)	= { "dout_cmu_shared0_div3", "fout_shared3_pll",
				    "dout_cmu_shared0_div4",
				    "dout_cmu_shared1_div4",
				    "dout_cmu_shared2_div2", "fout_spare_pll",
				    "oscclk", "oscclk" };
PNAME(mout_cmu_mif_busp_p)	= { "dout_cmu_shared0_div4",
				    "dout_cmu_shared1_div4",
				    "dout_cmu_shared0_div5", "fout_spare_pll" };
PNAME(mout_cmu_mif_switch_p)	= { "fout_shared0_pll", "fout_shared1_pll",
				    "dout_cmu_shared0_div2",
				    "dout_cmu_shared1_div2",
				    "fout_shared2_pll", "dout_cmu_shared0_div3",
				    "fout_shared3_pll", "fout_spare_pll" };
PNAME(mout_cmu_misc_bus_p)	= { "dout_cmu_shared0_div4",
				    "dout_cmu_shared2_div2",
				    "dout_cmu_shared3_div2", "fout_spare_pll" };
PNAME(mout_cmu_misc_sss_p)	= { "dout_cmu_shared0_div4",
				    "dout_cmu_shared2_div2",
				    "dout_cmu_shared3_div2", "fout_spare_pll" };
PNAME(mout_cmu_pdp_bus_p)	= { "dout_cmu_shared0_div3", "fout_shared3_pll",
				    "dout_cmu_shared1_div3",
				    "dout_cmu_shared0_div4",
				    "dout_cmu_shared1_div4",
				    "dout_cmu_shared2_div2",
				    "fout_spare_pll", "oscclk" };
PNAME(mout_cmu_pdp_vra_p)	= { "fout_shared2_pll", "dout_cmu_shared0_div3",
				    "fout_shared3_pll", "dout_cmu_shared1_div3",
				    "dout_cmu_shared0_div4",
				    "dout_cmu_shared1_div4",
				    "fout_spare_pll", "oscclk" };
PNAME(mout_cmu_peric0_bus_p)	= { "dout_cmu_shared0_div4",
				    "dout_cmu_shared2_div2",
				    "dout_cmu_shared3_div2", "fout_spare_pll" };
PNAME(mout_cmu_peric0_ip_p)	= { "dout_cmu_shared0_div4",
				    "dout_cmu_shared2_div2",
				    "dout_cmu_shared3_div2", "fout_spare_pll" };
PNAME(mout_cmu_peric1_bus_p)	= { "dout_cmu_shared0_div4",
				    "dout_cmu_shared2_div2",
				    "dout_cmu_shared3_div2", "fout_spare_pll" };
PNAME(mout_cmu_peric1_ip_p)	= { "dout_cmu_shared0_div4",
				    "dout_cmu_shared2_div2",
				    "dout_cmu_shared3_div2", "fout_spare_pll" };
PNAME(mout_cmu_tnr_bus_p)	= { "dout_cmu_shared0_div3", "fout_shared3_pll",
				    "dout_cmu_shared1_div3",
				    "dout_cmu_shared0_div4",
				    "dout_cmu_shared1_div4",
				    "dout_cmu_shared2_div2",
				    "fout_spare_pll", "oscclk" };
PNAME(mout_cmu_top_boost_option1_p) = { "oscclk",
					"gout_cmu_boost_option1" };
PNAME(mout_cmu_top_cmuref_p)	= { "dout_cmu_shared0_div4",
				    "dout_cmu_shared1_div4",
				    "dout_cmu_shared2_div2",
				    "dout_cmu_shared3_div2" };
PNAME(mout_cmu_tpu_bus_p)	= { "dout_cmu_shared0_div2",
				    "dout_cmu_shared1_div2",
				    "fout_shared2_pll",
				    "fout_shared3_pll",
				    "dout_cmu_shared0_div3",
				    "dout_cmu_shared1_div3",
				    "dout_cmu_shared0_div4",
				    "fout_spare_pll" };
PNAME(mout_cmu_tpu_tpu_p)	= { "dout_cmu_shared0_div2",
				    "dout_cmu_shared1_div2",
				    "fout_shared2_pll",
				    "fout_shared3_pll",
				    "dout_cmu_shared0_div3",
				    "dout_cmu_shared1_div3",
				    "dout_cmu_shared0_div4", "fout_spare_pll" };
PNAME(mout_cmu_tpu_tpuctl_p)	= { "dout_cmu_shared0_div2",
				    "dout_cmu_shared1_div2",
				    "fout_shared2_pll", "fout_shared3_pll",
				    "dout_cmu_shared0_div3",
				    "dout_cmu_shared1_div3",
				    "dout_cmu_shared0_div4", "fout_spare_pll" };
PNAME(mout_cmu_tpu_uart_p)	= { "dout_cmu_shared0_div4",
				    "dout_cmu_shared2_div2",
				    "dout_cmu_shared3_div2", "fout_spare_pll" };
PNAME(mout_cmu_cmuref_p)	= { "mout_cmu_top_boost_option1",
				    "dout_cmu_cmuref" };

/*
 * Register name to clock name mangling strategy used in this file
 *
 * Replace PLL_CON0_PLL	           with CLK_MOUT_PLL and mout_pll
 * Replace CLK_CON_MUX_MUX_CLKCMU  with CLK_MOUT_CMU and mout_cmu
 * Replace CLK_CON_DIV_CLKCMU      with CLK_DOUT_CMU and dout_cmu
 * Replace CLK_CON_DIV_DIV_CLKCMU  with CLK_DOUT_CMU and dout_cmu
 * Replace CLK_CON_GAT_CLKCMU      with CLK_GOUT_CMU and gout_cmu
 * Replace CLK_CON_GAT_GATE_CLKCMU with CLK_GOUT_CMU and gout_cmu
 *
 * For gates remove _UID _BLK _IPCLKPORT and _RSTNSYNC
 */

static const struct samsung_mux_clock cmu_top_mux_clks[] __initconst = {
	MUX(CLK_MOUT_PLL_SHARED0, "mout_pll_shared0", mout_pll_shared0_p,
	    PLL_CON0_PLL_SHARED0, 4, 1),
	MUX(CLK_MOUT_PLL_SHARED1, "mout_pll_shared1", mout_pll_shared1_p,
	    PLL_CON0_PLL_SHARED1, 4, 1),
	MUX(CLK_MOUT_PLL_SHARED2, "mout_pll_shared2", mout_pll_shared2_p,
	    PLL_CON0_PLL_SHARED2, 4, 1),
	MUX(CLK_MOUT_PLL_SHARED3, "mout_pll_shared3", mout_pll_shared3_p,
	    PLL_CON0_PLL_SHARED3, 4, 1),
	MUX(CLK_MOUT_PLL_SPARE, "mout_pll_spare", mout_pll_spare_p,
	    PLL_CON0_PLL_SPARE, 4, 1),
	MUX(CLK_MOUT_CMU_BO_BUS, "mout_cmu_bo_bus", mout_cmu_bo_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_BO_BUS, 0, 3),
	MUX(CLK_MOUT_CMU_BUS0_BUS, "mout_cmu_bus0_bus", mout_cmu_bus0_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_BUS0_BUS, 0, 3),
	MUX(CLK_MOUT_CMU_BUS1_BUS, "mout_cmu_bus1_bus", mout_cmu_bus1_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_BUS1_BUS, 0, 3),
	MUX(CLK_MOUT_CMU_BUS2_BUS, "mout_cmu_bus2_bus", mout_cmu_bus2_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_BUS2_BUS, 0, 3),
	MUX(CLK_MOUT_CMU_CIS_CLK0, "mout_cmu_cis_clk0", mout_cmu_cis_clk0_7_p,
	    CLK_CON_MUX_MUX_CLKCMU_CIS_CLK0, 0, 3),
	MUX(CLK_MOUT_CMU_CIS_CLK1, "mout_cmu_cis_clk1", mout_cmu_cis_clk0_7_p,
	    CLK_CON_MUX_MUX_CLKCMU_CIS_CLK1, 0, 3),
	MUX(CLK_MOUT_CMU_CIS_CLK2, "mout_cmu_cis_clk2", mout_cmu_cis_clk0_7_p,
	    CLK_CON_MUX_MUX_CLKCMU_CIS_CLK2, 0, 3),
	MUX(CLK_MOUT_CMU_CIS_CLK3, "mout_cmu_cis_clk3", mout_cmu_cis_clk0_7_p,
	    CLK_CON_MUX_MUX_CLKCMU_CIS_CLK3, 0, 3),
	MUX(CLK_MOUT_CMU_CIS_CLK4, "mout_cmu_cis_clk4", mout_cmu_cis_clk0_7_p,
	    CLK_CON_MUX_MUX_CLKCMU_CIS_CLK4, 0, 3),
	MUX(CLK_MOUT_CMU_CIS_CLK5, "mout_cmu_cis_clk5", mout_cmu_cis_clk0_7_p,
	    CLK_CON_MUX_MUX_CLKCMU_CIS_CLK5, 0, 3),
	MUX(CLK_MOUT_CMU_CIS_CLK6, "mout_cmu_cis_clk6", mout_cmu_cis_clk0_7_p,
	    CLK_CON_MUX_MUX_CLKCMU_CIS_CLK6, 0, 3),
	MUX(CLK_MOUT_CMU_CIS_CLK7, "mout_cmu_cis_clk7", mout_cmu_cis_clk0_7_p,
	    CLK_CON_MUX_MUX_CLKCMU_CIS_CLK7, 0, 3),
	MUX(CLK_MOUT_CMU_CMU_BOOST, "mout_cmu_cmu_boost", mout_cmu_cmu_boost_p,
	    CLK_CON_MUX_MUX_CLKCMU_CMU_BOOST, 0, 2),
	MUX(CLK_MOUT_CMU_BOOST_OPTION1, "mout_cmu_boost_option1",
	    mout_cmu_cmu_boost_option1_p,
	    CLK_CON_MUX_MUX_CLKCMU_CMU_BOOST_OPTION1, 0, 1),
	MUX(CLK_MOUT_CMU_CORE_BUS, "mout_cmu_core_bus", mout_cmu_core_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_CORE_BUS, 0, 3),
	MUX(CLK_MOUT_CMU_CPUCL0_DBG, "mout_cmu_cpucl0_dbg",
	    mout_cmu_cpucl0_dbg_p, CLK_CON_DIV_CLKCMU_CPUCL0_DBG, 0, 3),
	MUX(CLK_MOUT_CMU_CPUCL0_SWITCH, "mout_cmu_cpucl0_switch",
	    mout_cmu_cpucl0_switch_p, CLK_CON_MUX_MUX_CLKCMU_CPUCL0_SWITCH,
	    0, 3),
	MUX(CLK_MOUT_CMU_CPUCL1_SWITCH, "mout_cmu_cpucl1_switch",
	    mout_cmu_cpucl1_switch_p, CLK_CON_MUX_MUX_CLKCMU_CPUCL1_SWITCH,
	    0, 3),
	MUX(CLK_MOUT_CMU_CPUCL2_SWITCH, "mout_cmu_cpucl2_switch",
	    mout_cmu_cpucl2_switch_p, CLK_CON_MUX_MUX_CLKCMU_CPUCL2_SWITCH,
	    0, 3),
	MUX(CLK_MOUT_CMU_CSIS_BUS, "mout_cmu_csis_bus", mout_cmu_csis_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_CSIS_BUS, 0, 3),
	MUX(CLK_MOUT_CMU_DISP_BUS, "mout_cmu_disp_bus", mout_cmu_disp_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_DISP_BUS, 0, 3),
	MUX(CLK_MOUT_CMU_DNS_BUS, "mout_cmu_dns_bus", mout_cmu_dns_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_DNS_BUS, 0, 3),
	MUX(CLK_MOUT_CMU_DPU_BUS, "mout_cmu_dpu_bus", mout_cmu_dpu_p,
	    CLK_CON_MUX_MUX_CLKCMU_DPU_BUS, 0, 3),
	MUX(CLK_MOUT_CMU_EH_BUS, "mout_cmu_eh_bus", mout_cmu_eh_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_EH_BUS, 0, 3),
	MUX(CLK_MOUT_CMU_G2D_G2D, "mout_cmu_g2d_g2d", mout_cmu_g2d_g2d_p,
	    CLK_CON_MUX_MUX_CLKCMU_G2D_G2D, 0, 3),
	MUX(CLK_MOUT_CMU_G2D_MSCL, "mout_cmu_g2d_mscl", mout_cmu_g2d_mscl_p,
	    CLK_CON_MUX_MUX_CLKCMU_G2D_MSCL, 0, 3),
	MUX(CLK_MOUT_CMU_G3AA_G3AA, "mout_cmu_g3aa_g3aa", mout_cmu_g3aa_g3aa_p,
	    CLK_CON_MUX_MUX_CLKCMU_G3AA_G3AA, 0, 3),
	MUX(CLK_MOUT_CMU_G3D_BUSD, "mout_cmu_g3d_busd", mout_cmu_g3d_busd_p,
	    CLK_CON_MUX_MUX_CLKCMU_G3D_BUSD, 0, 3),
	MUX(CLK_MOUT_CMU_G3D_GLB, "mout_cmu_g3d_glb", mout_cmu_g3d_glb_p,
	    CLK_CON_MUX_MUX_CLKCMU_G3D_GLB, 0, 3),
	MUX(CLK_MOUT_CMU_G3D_SWITCH, "mout_cmu_g3d_switch",
	    mout_cmu_g3d_switch_p, CLK_CON_MUX_MUX_CLKCMU_G3D_SWITCH, 0, 3),
	MUX(CLK_MOUT_CMU_GDC_GDC0, "mout_cmu_gdc_gdc0", mout_cmu_gdc_gdc0_p,
	    CLK_CON_MUX_MUX_CLKCMU_GDC_GDC0, 0, 3),
	MUX(CLK_MOUT_CMU_GDC_GDC1, "mout_cmu_gdc_gdc1", mout_cmu_gdc_gdc1_p,
	    CLK_CON_MUX_MUX_CLKCMU_GDC_GDC1, 0, 3),
	MUX(CLK_MOUT_CMU_GDC_SCSC, "mout_cmu_gdc_scsc", mout_cmu_gdc_scsc_p,
	    CLK_CON_MUX_MUX_CLKCMU_GDC_SCSC, 0, 3),
	MUX(CLK_MOUT_CMU_HPM, "mout_cmu_hpm", mout_cmu_hpm_p,
	    CLK_CON_MUX_MUX_CLKCMU_HPM, 0, 2),
	MUX(CLK_MOUT_CMU_HSI0_BUS, "mout_cmu_hsi0_bus", mout_cmu_hsi0_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_HSI0_BUS, 0, 3),
	MUX(CLK_MOUT_CMU_HSI0_DPGTC, "mout_cmu_hsi0_dpgtc",
	    mout_cmu_hsi0_dpgtc_p, CLK_CON_MUX_MUX_CLKCMU_HSI0_DPGTC, 0, 2),
	MUX(CLK_MOUT_CMU_HSI0_USB31DRD, "mout_cmu_hsi0_usb31drd",
	    mout_cmu_hsi0_usb31drd_p, CLK_CON_MUX_MUX_CLKCMU_HSI0_USB31DRD,
	    0, 1),
	MUX(CLK_MOUT_CMU_HSI0_USBDPDBG, "mout_cmu_hsi0_usbdpdbg",
	    mout_cmu_hsi0_usbdpdbg_p, CLK_CON_MUX_MUX_CLKCMU_HSI0_USBDPDBG,
	    0, 1),
	MUX(CLK_MOUT_CMU_HSI1_BUS, "mout_cmu_hsi1_bus", mout_cmu_hsi1_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_HSI1_BUS, 0, 3),
	MUX(CLK_MOUT_CMU_HSI1_PCIE, "mout_cmu_hsi1_pcie", mout_cmu_hsi1_pcie_p,
	    CLK_CON_MUX_MUX_CLKCMU_HSI1_PCIE, 0, 1),
	MUX(CLK_MOUT_CMU_HSI2_BUS, "mout_cmu_hsi2_bus", mout_cmu_hsi2_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_HSI2_BUS, 0, 3),
	MUX(CLK_MOUT_CMU_HSI2_MMC_CARD, "mout_cmu_hsi2_mmc_card",
	    mout_cmu_hsi2_mmc_card_p, CLK_CON_MUX_MUX_CLKCMU_HSI2_MMC_CARD,
	    0, 2),
	MUX(CLK_MOUT_CMU_HSI2_PCIE, "mout_cmu_hsi2_pcie", mout_cmu_hsi2_pcie0_p,
	    CLK_CON_MUX_MUX_CLKCMU_HSI2_PCIE, 0, 1),
	MUX(CLK_MOUT_CMU_HSI2_UFS_EMBD, "mout_cmu_hsi2_ufs_embd",
	    mout_cmu_hsi2_ufs_embd_p, CLK_CON_MUX_MUX_CLKCMU_HSI2_UFS_EMBD,
	    0, 2),
	MUX(CLK_MOUT_CMU_IPP_BUS, "mout_cmu_ipp_bus", mout_cmu_ipp_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_IPP_BUS, 0, 3),
	MUX(CLK_MOUT_CMU_ITP_BUS, "mout_cmu_itp_bus", mout_cmu_itp_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_ITP_BUS, 0, 3),
	MUX(CLK_MOUT_CMU_MCSC_ITSC, "mout_cmu_mcsc_itsc", mout_cmu_mcsc_itsc_p,
	    CLK_CON_MUX_MUX_CLKCMU_MCSC_ITSC, 0, 3),
	MUX(CLK_MOUT_CMU_MCSC_MCSC, "mout_cmu_mcsc_mcsc", mout_cmu_mcsc_mcsc_p,
	    CLK_CON_MUX_MUX_CLKCMU_MCSC_MCSC, 0, 3),
	MUX(CLK_MOUT_CMU_MFC_MFC, "mout_cmu_mfc_mfc", mout_cmu_mfc_mfc_p,
	    CLK_CON_MUX_MUX_CLKCMU_MFC_MFC, 0, 3),
	MUX(CLK_MOUT_CMU_MIF_BUSP, "mout_cmu_mif_busp", mout_cmu_mif_busp_p,
	    CLK_CON_MUX_MUX_CLKCMU_MIF_BUSP, 0, 2),
	MUX(CLK_MOUT_CMU_MIF_SWITCH, "mout_cmu_mif_switch",
	    mout_cmu_mif_switch_p, CLK_CON_MUX_MUX_CLKCMU_MIF_SWITCH, 0, 3),
	MUX(CLK_MOUT_CMU_MISC_BUS, "mout_cmu_misc_bus", mout_cmu_misc_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_MISC_BUS, 0, 2),
	MUX(CLK_MOUT_CMU_MISC_SSS, "mout_cmu_misc_sss", mout_cmu_misc_sss_p,
	    CLK_CON_MUX_MUX_CLKCMU_MISC_SSS, 0, 2),
	MUX(CLK_MOUT_CMU_PDP_BUS, "mout_cmu_pdp_bus", mout_cmu_pdp_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_PDP_BUS, 0, 3),
	MUX(CLK_MOUT_CMU_PDP_VRA, "mout_cmu_pdp_vra", mout_cmu_pdp_vra_p,
	    CLK_CON_MUX_MUX_CLKCMU_PDP_VRA, 0, 3),
	MUX(CLK_MOUT_CMU_PERIC0_BUS, "mout_cmu_peric0_bus",
	    mout_cmu_peric0_bus_p, CLK_CON_MUX_MUX_CLKCMU_PERIC0_BUS, 0, 2),
	MUX(CLK_MOUT_CMU_PERIC0_IP, "mout_cmu_peric0_ip", mout_cmu_peric0_ip_p,
	    CLK_CON_MUX_MUX_CLKCMU_PERIC0_IP, 0, 2),
	MUX(CLK_MOUT_CMU_PERIC1_BUS, "mout_cmu_peric1_bus",
	    mout_cmu_peric1_bus_p, CLK_CON_MUX_MUX_CLKCMU_PERIC1_BUS, 0, 2),
	MUX(CLK_MOUT_CMU_PERIC1_IP, "mout_cmu_peric1_ip", mout_cmu_peric1_ip_p,
	    CLK_CON_MUX_MUX_CLKCMU_PERIC1_IP, 0, 2),
	MUX(CLK_MOUT_CMU_TNR_BUS, "mout_cmu_tnr_bus", mout_cmu_tnr_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_TNR_BUS, 0, 3),
	MUX(CLK_MOUT_CMU_TOP_BOOST_OPTION1, "mout_cmu_top_boost_option1",
	    mout_cmu_top_boost_option1_p,
	    CLK_CON_MUX_MUX_CLKCMU_TOP_BOOST_OPTION1, 0, 1),
	MUX(CLK_MOUT_CMU_TOP_CMUREF, "mout_cmu_top_cmuref",
	    mout_cmu_top_cmuref_p, CLK_CON_MUX_MUX_CLKCMU_TOP_CMUREF, 0, 2),
	MUX(CLK_MOUT_CMU_TPU_BUS, "mout_cmu_tpu_bus", mout_cmu_tpu_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_TPU_BUS, 0, 3),
	MUX(CLK_MOUT_CMU_TPU_TPU, "mout_cmu_tpu_tpu", mout_cmu_tpu_tpu_p,
	    CLK_CON_MUX_MUX_CLKCMU_TPU_TPU, 0, 3),
	MUX(CLK_MOUT_CMU_TPU_TPUCTL, "mout_cmu_tpu_tpuctl",
	    mout_cmu_tpu_tpuctl_p, CLK_CON_MUX_MUX_CLKCMU_TPU_TPUCTL, 0, 3),
	MUX(CLK_MOUT_CMU_TPU_UART, "mout_cmu_tpu_uart", mout_cmu_tpu_uart_p,
	    CLK_CON_MUX_MUX_CLKCMU_TPU_UART, 0, 2),
	MUX(CLK_MOUT_CMU_CMUREF, "mout_cmu_cmuref", mout_cmu_cmuref_p,
	    CLK_CON_MUX_MUX_CMU_CMUREF, 0, 1),
};

static const struct samsung_div_clock cmu_top_div_clks[] __initconst = {
	DIV(CLK_DOUT_CMU_BO_BUS, "dout_cmu_bo_bus", "gout_cmu_bo_bus",
	    CLK_CON_DIV_CLKCMU_BO_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_BUS0_BUS, "dout_cmu_bus0_bus", "gout_cmu_bus0_bus",
	    CLK_CON_DIV_CLKCMU_BUS0_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_BUS1_BUS, "dout_cmu_bus1_bus", "gout_cmu_bus1_bus",
	    CLK_CON_DIV_CLKCMU_BUS1_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_BUS2_BUS, "dout_cmu_bus2_bus", "gout_cmu_bus2_bus",
	    CLK_CON_DIV_CLKCMU_BUS2_BUS, 0, 4),
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
	DIV(CLK_DOUT_CMU_CIS_CLK6, "dout_cmu_cis_clk6", "gout_cmu_cis_clk6",
	    CLK_CON_DIV_CLKCMU_CIS_CLK6, 0, 5),
	DIV(CLK_DOUT_CMU_CIS_CLK7, "dout_cmu_cis_clk7", "gout_cmu_cis_clk7",
	    CLK_CON_DIV_CLKCMU_CIS_CLK7, 0, 5),
	DIV(CLK_DOUT_CMU_CORE_BUS, "dout_cmu_core_bus", "gout_cmu_core_bus",
	    CLK_CON_DIV_CLKCMU_CORE_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_CPUCL0_DBG, "dout_cmu_cpucl0_dbg",
	    "gout_cmu_cpucl0_dbg", CLK_CON_DIV_CLKCMU_CPUCL0_DBG, 0, 4),
	DIV(CLK_DOUT_CMU_CPUCL0_SWITCH, "dout_cmu_cpucl0_switch",
	    "gout_cmu_cpucl0_switch", CLK_CON_DIV_CLKCMU_CPUCL0_SWITCH, 0, 3),
	DIV(CLK_DOUT_CMU_CPUCL1_SWITCH, "dout_cmu_cpucl1_switch",
	    "gout_cmu_cpucl1_switch", CLK_CON_DIV_CLKCMU_CPUCL1_SWITCH, 0, 3),
	DIV(CLK_DOUT_CMU_CPUCL2_SWITCH, "dout_cmu_cpucl2_switch",
	    "gout_cmu_cpucl2_switch", CLK_CON_DIV_CLKCMU_CPUCL2_SWITCH, 0, 3),
	DIV(CLK_DOUT_CMU_CSIS_BUS, "dout_cmu_csis_bus", "gout_cmu_csis_bus",
	    CLK_CON_DIV_CLKCMU_CSIS_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_DISP_BUS, "dout_cmu_disp_bus", "gout_cmu_disp_bus",
	    CLK_CON_DIV_CLKCMU_DISP_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_DNS_BUS, "dout_cmu_dns_bus", "gout_cmu_dns_bus",
	    CLK_CON_DIV_CLKCMU_DNS_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_DPU_BUS, "dout_cmu_dpu_bus", "gout_cmu_dpu_bus",
	    CLK_CON_DIV_CLKCMU_DPU_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_EH_BUS, "dout_cmu_eh_bus", "gout_cmu_eh_bus",
	    CLK_CON_DIV_CLKCMU_EH_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_G2D_G2D, "dout_cmu_g2d_g2d", "gout_cmu_g2d_g2d",
	    CLK_CON_DIV_CLKCMU_G2D_G2D, 0, 4),
	DIV(CLK_DOUT_CMU_G2D_MSCL, "dout_cmu_g2d_mscl", "gout_cmu_g2d_mscl",
	    CLK_CON_DIV_CLKCMU_G2D_MSCL, 0, 4),
	DIV(CLK_DOUT_CMU_G3AA_G3AA, "dout_cmu_g3aa_g3aa", "gout_cmu_g3aa_g3aa",
	    CLK_CON_DIV_CLKCMU_G3AA_G3AA, 0, 4),
	DIV(CLK_DOUT_CMU_G3D_BUSD, "dout_cmu_g3d_busd", "gout_cmu_g3d_busd",
	    CLK_CON_DIV_CLKCMU_G3D_BUSD, 0, 4),
	DIV(CLK_DOUT_CMU_G3D_GLB, "dout_cmu_g3d_glb", "gout_cmu_g3d_glb",
	    CLK_CON_DIV_CLKCMU_G3D_GLB, 0, 4),
	DIV(CLK_DOUT_CMU_G3D_SWITCH, "dout_cmu_g3d_switch",
	    "gout_cmu_g3d_switch", CLK_CON_DIV_CLKCMU_G3D_SWITCH, 0, 3),
	DIV(CLK_DOUT_CMU_GDC_GDC0, "dout_cmu_gdc_gdc0", "gout_cmu_gdc_gdc0",
	    CLK_CON_DIV_CLKCMU_GDC_GDC0, 0, 4),
	DIV(CLK_DOUT_CMU_GDC_GDC1, "dout_cmu_gdc_gdc1", "gout_cmu_gdc_gdc1",
	    CLK_CON_DIV_CLKCMU_GDC_GDC1, 0, 4),
	DIV(CLK_DOUT_CMU_GDC_SCSC, "dout_cmu_gdc_scsc", "gout_cmu_gdc_scsc",
	    CLK_CON_DIV_CLKCMU_GDC_SCSC, 0, 4),
	DIV(CLK_DOUT_CMU_CMU_HPM, "dout_cmu_hpm", "gout_cmu_hpm",
	    CLK_CON_DIV_CLKCMU_HPM, 0, 2),
	DIV(CLK_DOUT_CMU_HSI0_BUS, "dout_cmu_hsi0_bus", "gout_cmu_hsi0_bus",
	    CLK_CON_DIV_CLKCMU_HSI0_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_HSI0_DPGTC, "dout_cmu_hsi0_dpgtc",
	    "gout_cmu_hsi0_dpgtc", CLK_CON_DIV_CLKCMU_HSI0_DPGTC, 0, 4),
	DIV(CLK_DOUT_CMU_HSI0_USB31DRD, "dout_cmu_hsi0_usb31drd",
	    "gout_cmu_hsi0_usb31drd", CLK_CON_DIV_CLKCMU_HSI0_USB31DRD, 0, 5),
	DIV(CLK_DOUT_CMU_HSI1_BUS, "dout_cmu_hsi1_bus", "gout_cmu_hsi1_bus",
	    CLK_CON_DIV_CLKCMU_HSI1_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_HSI1_PCIE, "dout_cmu_hsi1_pcie", "gout_cmu_hsi1_pcie",
	    CLK_CON_DIV_CLKCMU_HSI1_PCIE, 0, 3),
	DIV(CLK_DOUT_CMU_HSI2_BUS, "dout_cmu_hsi2_bus", "gout_cmu_hsi2_bus",
	    CLK_CON_DIV_CLKCMU_HSI2_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_HSI2_MMC_CARD, "dout_cmu_hsi2_mmc_card",
	    "gout_cmu_hsi2_mmc_card", CLK_CON_DIV_CLKCMU_HSI2_MMC_CARD, 0, 9),
	DIV(CLK_DOUT_CMU_HSI2_PCIE, "dout_cmu_hsi2_pcie", "gout_cmu_hsi2_pcie",
	    CLK_CON_DIV_CLKCMU_HSI2_PCIE, 0, 3),
	DIV(CLK_DOUT_CMU_HSI2_UFS_EMBD, "dout_cmu_hsi2_ufs_embd",
	    "gout_cmu_hsi2_ufs_embd", CLK_CON_DIV_CLKCMU_HSI2_UFS_EMBD, 0, 4),
	DIV(CLK_DOUT_CMU_IPP_BUS, "dout_cmu_ipp_bus", "gout_cmu_ipp_bus",
	    CLK_CON_DIV_CLKCMU_IPP_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_ITP_BUS, "dout_cmu_itp_bus", "gout_cmu_itp_bus",
	    CLK_CON_DIV_CLKCMU_ITP_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_MCSC_ITSC, "dout_cmu_mcsc_itsc", "gout_cmu_mcsc_itsc",
	    CLK_CON_DIV_CLKCMU_MCSC_ITSC, 0, 4),
	DIV(CLK_DOUT_CMU_MCSC_MCSC, "dout_cmu_mcsc_mcsc", "gout_cmu_mcsc_mcsc",
	    CLK_CON_DIV_CLKCMU_MCSC_MCSC, 0, 4),
	DIV(CLK_DOUT_CMU_MFC_MFC, "dout_cmu_mfc_mfc", "gout_cmu_mfc_mfc",
	    CLK_CON_DIV_CLKCMU_MFC_MFC, 0, 4),
	DIV(CLK_DOUT_CMU_MIF_BUSP, "dout_cmu_mif_busp", "gout_cmu_mif_busp",
	    CLK_CON_DIV_CLKCMU_MIF_BUSP, 0, 4),
	DIV(CLK_DOUT_CMU_MISC_BUS, "dout_cmu_misc_bus", "gout_cmu_misc_bus",
	    CLK_CON_DIV_CLKCMU_MISC_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_MISC_SSS, "dout_cmu_misc_sss", "gout_cmu_misc_sss",
	    CLK_CON_DIV_CLKCMU_MISC_SSS, 0, 4),
	DIV(CLK_DOUT_CMU_PDP_BUS, "dout_cmu_pdp_bus", "gout_cmu_pdp_bus",
	    CLK_CON_DIV_CLKCMU_PDP_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_PDP_VRA, "dout_cmu_pdp_vra", "gout_cmu_pdp_vra",
	    CLK_CON_DIV_CLKCMU_PDP_VRA, 0, 4),
	DIV(CLK_DOUT_CMU_PERIC0_BUS, "dout_cmu_peric0_bus",
	    "gout_cmu_peric0_bus", CLK_CON_DIV_CLKCMU_PERIC0_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_PERIC0_IP, "dout_cmu_peric0_ip", "gout_cmu_peric0_ip",
	    CLK_CON_DIV_CLKCMU_PERIC0_IP, 0, 4),
	DIV(CLK_DOUT_CMU_PERIC1_BUS, "dout_cmu_peric1_bus",
	    "gout_cmu_peric1_bus", CLK_CON_DIV_CLKCMU_PERIC1_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_PERIC1_IP, "dout_cmu_peric1_ip", "gout_cmu_peric1_ip",
	    CLK_CON_DIV_CLKCMU_PERIC1_IP, 0, 4),
	DIV(CLK_DOUT_CMU_TNR_BUS, "dout_cmu_tnr_bus", "gout_cmu_tnr_bus",
	    CLK_CON_DIV_CLKCMU_TNR_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_TPU_BUS, "dout_cmu_tpu_bus", "gout_cmu_tpu_bus",
	    CLK_CON_DIV_CLKCMU_TPU_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_TPU_TPU, "dout_cmu_tpu_tpu", "gout_cmu_tpu_tpu",
	    CLK_CON_DIV_CLKCMU_TPU_TPU, 0, 4),
	DIV(CLK_DOUT_CMU_TPU_TPUCTL, "dout_cmu_tpu_tpuctl",
	    "gout_cmu_tpu_tpuctl", CLK_CON_DIV_CLKCMU_TPU_TPUCTL, 0, 4),
	DIV(CLK_DOUT_CMU_TPU_UART, "dout_cmu_tpu_uart", "gout_cmu_tpu_uart",
	    CLK_CON_DIV_CLKCMU_TPU_UART, 0, 4),
	DIV(CLK_DOUT_CMU_CMU_BOOST, "dout_cmu_cmu_boost", "gout_cmu_cmu_boost",
	    CLK_CON_DIV_DIV_CLKCMU_CMU_BOOST, 0, 2),
	DIV(CLK_DOUT_CMU_CMU_CMUREF, "dout_cmu_cmuref", "gout_cmu_cmuref",
	    CLK_CON_DIV_DIV_CLK_CMU_CMUREF, 0, 2),
	DIV(CLK_DOUT_CMU_SHARED0_DIV2, "dout_cmu_shared0_div2",
	    "mout_pll_shared0", CLK_CON_DIV_PLL_SHARED0_DIV2, 0, 1),
	DIV(CLK_DOUT_CMU_SHARED0_DIV3, "dout_cmu_shared0_div3",
	    "mout_pll_shared0", CLK_CON_DIV_PLL_SHARED0_DIV3, 0, 2),
	DIV(CLK_DOUT_CMU_SHARED0_DIV4, "dout_cmu_shared0_div4",
	    "dout_cmu_shared0_div2", CLK_CON_DIV_PLL_SHARED0_DIV4, 0, 1),
	DIV(CLK_DOUT_CMU_SHARED0_DIV5, "dout_cmu_shared0_div5",
	    "mout_pll_shared0", CLK_CON_DIV_PLL_SHARED0_DIV5, 0, 3),
	DIV(CLK_DOUT_CMU_SHARED1_DIV2, "dout_cmu_shared1_div2",
	    "mout_pll_shared1", CLK_CON_DIV_PLL_SHARED1_DIV2, 0, 1),
	DIV(CLK_DOUT_CMU_SHARED1_DIV3, "dout_cmu_shared1_div3",
	    "mout_pll_shared1", CLK_CON_DIV_PLL_SHARED1_DIV3, 0, 2),
	DIV(CLK_DOUT_CMU_SHARED1_DIV4, "dout_cmu_shared1_div4",
	    "mout_pll_shared1", CLK_CON_DIV_PLL_SHARED1_DIV4, 0, 1),
	DIV(CLK_DOUT_CMU_SHARED2_DIV2, "dout_cmu_shared2_div2",
	    "mout_pll_shared2", CLK_CON_DIV_PLL_SHARED2_DIV2, 0, 1),
	DIV(CLK_DOUT_CMU_SHARED3_DIV2, "dout_cmu_shared3_div2",
	    "mout_pll_shared3", CLK_CON_DIV_PLL_SHARED3_DIV2, 0, 1),
};

static const struct samsung_fixed_factor_clock cmu_top_ffactor[] __initconst = {
	FFACTOR(CLK_DOUT_CMU_HSI0_USBDPDBG, "dout_cmu_hsi0_usbdpdbg",
		"gout_cmu_hsi0_usbdpdbg", 1, 4, 0),
	FFACTOR(CLK_DOUT_CMU_OTP, "dout_cmu_otp", "oscclk", 1, 8, 0),
};

static const struct samsung_gate_clock cmu_top_gate_clks[] __initconst = {
	GATE(CLK_GOUT_CMU_BUS0_BOOST, "gout_cmu_bus0_boost",
	     "mout_cmu_boost_option1", CLK_CON_GAT_CLKCMU_BUS0_BOOST, 21, 0, 0),
	GATE(CLK_GOUT_CMU_BUS1_BOOST, "gout_cmu_bus1_boost",
	     "mout_cmu_boost_option1", CLK_CON_GAT_CLKCMU_BUS1_BOOST, 21, 0, 0),
	GATE(CLK_GOUT_CMU_BUS2_BOOST, "gout_cmu_bus2_boost",
	     "mout_cmu_boost_option1", CLK_CON_GAT_CLKCMU_BUS2_BOOST, 21, 0, 0),
	GATE(CLK_GOUT_CMU_CORE_BOOST, "gout_cmu_core_boost",
	     "mout_cmu_boost_option1", CLK_CON_GAT_CLKCMU_CORE_BOOST, 21, 0, 0),
	GATE(CLK_GOUT_CMU_CPUCL0_BOOST, "gout_cmu_cpucl0_boost",
	     "mout_cmu_boost_option1", CLK_CON_GAT_CLKCMU_CPUCL0_BOOST,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_CPUCL1_BOOST, "gout_cmu_cpucl1_boost",
	     "mout_cmu_boost_option1", CLK_CON_GAT_CLKCMU_CPUCL1_BOOST,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_CPUCL2_BOOST, "gout_cmu_cpucl2_boost",
	     "mout_cmu_boost_option1", CLK_CON_GAT_CLKCMU_CPUCL2_BOOST,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_MIF_BOOST, "gout_cmu_mif_boost",
	     "mout_cmu_boost_option1", CLK_CON_GAT_CLKCMU_MIF_BOOST,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_MIF_SWITCH, "gout_cmu_mif_switch",
	     "mout_cmu_mif_switch", CLK_CON_GAT_CLKCMU_MIF_SWITCH, 21, 0, 0),
	GATE(CLK_GOUT_CMU_BO_BUS, "gout_cmu_bo_bus", "mout_cmu_bo_bus",
	     CLK_CON_GAT_GATE_CLKCMU_BO_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CMU_BUS0_BUS, "gout_cmu_bus0_bus", "mout_cmu_bus0_bus",
	     CLK_CON_GAT_GATE_CLKCMU_BUS0_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CMU_BUS1_BUS, "gout_cmu_bus1_bus", "mout_cmu_bus1_bus",
	     CLK_CON_GAT_GATE_CLKCMU_BUS1_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CMU_BUS2_BUS, "gout_cmu_bus2_bus", "mout_cmu_bus2_bus",
	     CLK_CON_GAT_GATE_CLKCMU_BUS2_BUS, 21, 0, 0),
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
	GATE(CLK_GOUT_CMU_CIS_CLK6, "gout_cmu_cis_clk6", "mout_cmu_cis_clk6",
	     CLK_CON_GAT_GATE_CLKCMU_CIS_CLK6, 21, 0, 0),
	GATE(CLK_GOUT_CMU_CIS_CLK7, "gout_cmu_cis_clk7", "mout_cmu_cis_clk7",
	     CLK_CON_GAT_GATE_CLKCMU_CIS_CLK7, 21, 0, 0),
	GATE(CLK_GOUT_CMU_CMU_BOOST, "gout_cmu_cmu_boost", "mout_cmu_cmu_boost",
	     CLK_CON_GAT_GATE_CLKCMU_CMU_BOOST, 21, 0, 0),
	GATE(CLK_GOUT_CMU_CORE_BUS, "gout_cmu_core_bus", "mout_cmu_core_bus",
	     CLK_CON_GAT_GATE_CLKCMU_CORE_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CMU_CPUCL0_DBG, "gout_cmu_cpucl0_dbg",
	     "mout_cmu_cpucl0_dbg", CLK_CON_GAT_GATE_CLKCMU_CPUCL0_DBG_BUS,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_CPUCL0_SWITCH, "gout_cmu_cpucl0_switch",
	     "mout_cmu_cpucl0_switch", CLK_CON_GAT_GATE_CLKCMU_CPUCL0_SWITCH,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_CPUCL1_SWITCH, "gout_cmu_cpucl1_switch",
	     "mout_cmu_cpucl1_switch", CLK_CON_GAT_GATE_CLKCMU_CPUCL1_SWITCH,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_CPUCL2_SWITCH, "gout_cmu_cpucl2_switch",
	     "mout_cmu_cpucl2_switch", CLK_CON_GAT_GATE_CLKCMU_CPUCL2_SWITCH,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_CSIS_BUS, "gout_cmu_csis_bus", "mout_cmu_csis_bus",
	     CLK_CON_GAT_GATE_CLKCMU_CSIS_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CMU_DISP_BUS, "gout_cmu_disp_bus", "mout_cmu_disp_bus",
	     CLK_CON_GAT_GATE_CLKCMU_DISP_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CMU_DNS_BUS, "gout_cmu_dns_bus", "mout_cmu_dns_bus",
	     CLK_CON_GAT_GATE_CLKCMU_DNS_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CMU_DPU_BUS, "gout_cmu_dpu_bus", "mout_cmu_dpu_bus",
	     CLK_CON_GAT_GATE_CLKCMU_DPU_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CMU_EH_BUS, "gout_cmu_eh_bus", "mout_cmu_eh_bus",
	     CLK_CON_GAT_GATE_CLKCMU_EH_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CMU_G2D_G2D, "gout_cmu_g2d_g2d", "mout_cmu_g2d_g2d",
	     CLK_CON_GAT_GATE_CLKCMU_G2D_G2D, 21, 0, 0),
	GATE(CLK_GOUT_CMU_G2D_MSCL, "gout_cmu_g2d_mscl", "mout_cmu_g2d_mscl",
	     CLK_CON_GAT_GATE_CLKCMU_G2D_MSCL, 21, 0, 0),
	GATE(CLK_GOUT_CMU_G3AA_G3AA, "gout_cmu_g3aa_g3aa", "mout_cmu_g3aa_g3aa",
	     CLK_CON_MUX_MUX_CLKCMU_G3AA_G3AA, 21, 0, 0),
	GATE(CLK_GOUT_CMU_G3D_BUSD, "gout_cmu_g3d_busd", "mout_cmu_g3d_busd",
	     CLK_CON_GAT_GATE_CLKCMU_G3D_BUSD, 21, 0, 0),
	GATE(CLK_GOUT_CMU_G3D_GLB, "gout_cmu_g3d_glb", "mout_cmu_g3d_glb",
	     CLK_CON_GAT_GATE_CLKCMU_G3D_GLB, 21, 0, 0),
	GATE(CLK_GOUT_CMU_G3D_SWITCH, "gout_cmu_g3d_switch",
	     "mout_cmu_g3d_switch", CLK_CON_GAT_GATE_CLKCMU_G3D_SWITCH,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_GDC_GDC0, "gout_cmu_gdc_gdc0", "mout_cmu_gdc_gdc0",
	     CLK_CON_GAT_GATE_CLKCMU_GDC_GDC0, 21, 0, 0),
	GATE(CLK_GOUT_CMU_GDC_GDC1, "gout_cmu_gdc_gdc1", "mout_cmu_gdc_gdc1",
	     CLK_CON_GAT_GATE_CLKCMU_GDC_GDC1, 21, 0, 0),
	GATE(CLK_GOUT_CMU_GDC_SCSC, "gout_cmu_gdc_scsc", "mout_cmu_gdc_scsc",
	     CLK_CON_GAT_GATE_CLKCMU_GDC_SCSC, 21, 0, 0),
	GATE(CLK_GOUT_CMU_HPM, "gout_cmu_hpm", "mout_cmu_hpm",
	     CLK_CON_GAT_GATE_CLKCMU_HPM, 21, 0, 0),
	GATE(CLK_GOUT_CMU_HSI0_BUS, "gout_cmu_hsi0_bus", "mout_cmu_hsi0_bus",
	     CLK_CON_GAT_GATE_CLKCMU_HSI0_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CMU_HSI0_DPGTC, "gout_cmu_hsi0_dpgtc",
	     "mout_cmu_hsi0_dpgtc", CLK_CON_GAT_GATE_CLKCMU_HSI0_DPGTC,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_HSI0_USB31DRD, "gout_cmu_hsi0_usb31drd",
	     "mout_cmu_hsi0_usb31drd", CLK_CON_GAT_GATE_CLKCMU_HSI0_USB31DRD,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_HSI0_USBDPDBG, "gout_cmu_hsi0_usbdpdbg",
	     "mout_cmu_hsi0_usbdpdbg", CLK_CON_GAT_GATE_CLKCMU_HSI0_USBDPDBG,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_HSI1_BUS, "gout_cmu_hsi1_bus", "mout_cmu_hsi1_bus",
	     CLK_CON_GAT_GATE_CLKCMU_HSI1_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CMU_HSI1_PCIE, "gout_cmu_hsi1_pcie", "mout_cmu_hsi1_pcie",
	     CLK_CON_GAT_GATE_CLKCMU_HSI1_PCIE, 21, 0, 0),
	GATE(CLK_GOUT_CMU_HSI2_BUS, "gout_cmu_hsi2_bus", "mout_cmu_hsi2_bus",
	     CLK_CON_GAT_GATE_CLKCMU_HSI2_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CMU_HSI2_MMC_CARD, "gout_cmu_hsi2_mmc_card",
	     "mout_cmu_hsi2_mmc_card", CLK_CON_GAT_GATE_CLKCMU_HSI2_MMCCARD,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_HSI2_PCIE, "gout_cmu_hsi2_pcie", "mout_cmu_hsi2_pcie",
	     CLK_CON_GAT_GATE_CLKCMU_HSI2_PCIE, 21, 0, 0),
	GATE(CLK_GOUT_CMU_HSI2_UFS_EMBD, "gout_cmu_hsi2_ufs_embd",
	     "mout_cmu_hsi2_ufs_embd", CLK_CON_GAT_GATE_CLKCMU_HSI2_UFS_EMBD,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_IPP_BUS, "gout_cmu_ipp_bus", "mout_cmu_ipp_bus",
	     CLK_CON_GAT_GATE_CLKCMU_IPP_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CMU_ITP_BUS, "gout_cmu_itp_bus", "mout_cmu_itp_bus",
	     CLK_CON_GAT_GATE_CLKCMU_ITP_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CMU_MCSC_ITSC, "gout_cmu_mcsc_itsc", "mout_cmu_mcsc_itsc",
	     CLK_CON_GAT_GATE_CLKCMU_MCSC_ITSC, 21, 0, 0),
	GATE(CLK_GOUT_CMU_MCSC_MCSC, "gout_cmu_mcsc_mcsc", "mout_cmu_mcsc_mcsc",
	     CLK_CON_GAT_GATE_CLKCMU_MCSC_MCSC, 21, 0, 0),
	GATE(CLK_GOUT_CMU_MFC_MFC, "gout_cmu_mfc_mfc", "mout_cmu_mfc_mfc",
	     CLK_CON_GAT_GATE_CLKCMU_MFC_MFC, 21, 0, 0),
	GATE(CLK_GOUT_CMU_MIF_BUSP, "gout_cmu_mif_busp", "mout_cmu_mif_busp",
	     CLK_CON_GAT_GATE_CLKCMU_MIF_BUSP, 21, 0, 0),
	GATE(CLK_GOUT_CMU_MISC_BUS, "gout_cmu_misc_bus", "mout_cmu_misc_bus",
	     CLK_CON_GAT_GATE_CLKCMU_MISC_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CMU_MISC_SSS, "gout_cmu_misc_sss", "mout_cmu_misc_sss",
	     CLK_CON_GAT_GATE_CLKCMU_MISC_SSS, 21, 0, 0),
	GATE(CLK_GOUT_CMU_PDP_BUS, "gout_cmu_pdp_bus", "mout_cmu_pdp_bus",
	     CLK_CON_GAT_GATE_CLKCMU_PDP_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CMU_PDP_VRA, "gout_cmu_pdp_vra", "mout_cmu_pdp_vra",
	     CLK_CON_GAT_GATE_CLKCMU_PDP_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CMU_PERIC0_BUS, "gout_cmu_peric0_bus",
	     "mout_cmu_peric0_bus", CLK_CON_GAT_GATE_CLKCMU_PERIC0_BUS,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_PERIC0_IP, "gout_cmu_peric0_ip", "mout_cmu_peric0_ip",
	     CLK_CON_GAT_GATE_CLKCMU_PERIC0_IP, 21, 0, 0),
	GATE(CLK_GOUT_CMU_PERIC1_BUS, "gout_cmu_peric1_bus",
	     "mout_cmu_peric1_bus", CLK_CON_GAT_GATE_CLKCMU_PERIC1_BUS,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_PERIC1_IP, "gout_cmu_peric1_ip", "mout_cmu_peric1_ip",
	     CLK_CON_GAT_GATE_CLKCMU_PERIC1_IP, 21, 0, 0),
	GATE(CLK_GOUT_CMU_TNR_BUS, "gout_cmu_tnr_bus", "mout_cmu_tnr_bus",
	     CLK_CON_GAT_GATE_CLKCMU_TNR_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CMU_TOP_CMUREF, "gout_cmu_top_cmuref",
	     "mout_cmu_top_cmuref", CLK_CON_GAT_GATE_CLKCMU_TOP_CMUREF,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_TPU_BUS, "gout_cmu_tpu_bus", "mout_cmu_tpu_bus",
	     CLK_CON_GAT_GATE_CLKCMU_TPU_BUS, 21, 0, 0),
	GATE(CLK_GOUT_CMU_TPU_TPU, "gout_cmu_tpu_tpu", "mout_cmu_tpu_tpu",
	     CLK_CON_GAT_GATE_CLKCMU_TPU_TPU, 21, 0, 0),
	GATE(CLK_GOUT_CMU_TPU_TPUCTL, "gout_cmu_tpu_tpuctl",
	     "mout_cmu_tpu_tpuctl", CLK_CON_GAT_GATE_CLKCMU_TPU_TPUCTL,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_TPU_UART, "gout_cmu_tpu_uart", "mout_cmu_tpu_uart",
	     CLK_CON_GAT_GATE_CLKCMU_TPU_UART, 21, 0, 0),
};

static const struct samsung_cmu_info top_cmu_info __initconst = {
	.pll_clks		= cmu_top_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(cmu_top_pll_clks),
	.mux_clks		= cmu_top_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(cmu_top_mux_clks),
	.div_clks		= cmu_top_div_clks,
	.nr_div_clks		= ARRAY_SIZE(cmu_top_div_clks),
	.fixed_factor_clks	= cmu_top_ffactor,
	.nr_fixed_factor_clks	= ARRAY_SIZE(cmu_top_ffactor),
	.gate_clks		= cmu_top_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(cmu_top_gate_clks),
	.nr_clk_ids		= CLKS_NR_TOP,
	.clk_regs		= cmu_top_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(cmu_top_clk_regs),
};

static void __init gs101_cmu_top_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &top_cmu_info);
}

/* Register CMU_TOP early, as it's a dependency for other early domains */
CLK_OF_DECLARE(gs101_cmu_top, "google,gs101-cmu-top",
	       gs101_cmu_top_init);

/* ---- CMU_APM ------------------------------------------------------------- */

/* Register Offset definitions for CMU_APM (0x17400000) */
#define APM_CMU_APM_CONTROLLER_OPTION							0x0800
#define CLKOUT_CON_BLK_APM_CMU_APM_CLKOUT0						0x0810
#define CLK_CON_MUX_MUX_CLKCMU_APM_FUNC							0x1000
#define CLK_CON_MUX_MUX_CLKCMU_APM_FUNCSRC						0x1004
#define CLK_CON_DIV_DIV_CLK_APM_BOOST							0x1800
#define CLK_CON_DIV_DIV_CLK_APM_USI0_UART						0x1804
#define CLK_CON_DIV_DIV_CLK_APM_USI0_USI						0x1808
#define CLK_CON_DIV_DIV_CLK_APM_USI1_UART						0x180c
#define CLK_CON_GAT_CLK_BLK_APM_UID_APM_CMU_APM_IPCLKPORT_PCLK				0x2000
#define CLK_CON_GAT_CLK_BUS0_BOOST_OPTION1						0x2004
#define CLK_CON_GAT_CLK_CMU_BOOST_OPTION1						0x2008
#define CLK_CON_GAT_CLK_CORE_BOOST_OPTION1						0x200c
#define CLK_CON_GAT_GATE_CLKCMU_APM_FUNC						0x2010
#define CLK_CON_GAT_GOUT_BLK_APM_UID_APBIF_GPIO_ALIVE_IPCLKPORT_PCLK			0x2014
#define CLK_CON_GAT_GOUT_BLK_APM_UID_APBIF_GPIO_FAR_ALIVE_IPCLKPORT_PCLK		0x2018
#define CLK_CON_GAT_GOUT_BLK_APM_UID_APBIF_PMU_ALIVE_IPCLKPORT_PCLK			0x201c
#define CLK_CON_GAT_GOUT_BLK_APM_UID_APBIF_RTC_IPCLKPORT_PCLK				0x2020
#define CLK_CON_GAT_GOUT_BLK_APM_UID_APBIF_TRTC_IPCLKPORT_PCLK				0x2024
#define CLK_CON_GAT_GOUT_BLK_APM_UID_APM_USI0_UART_IPCLKPORT_IPCLK			0x2028
#define CLK_CON_GAT_GOUT_BLK_APM_UID_APM_USI0_UART_IPCLKPORT_PCLK			0x202c
#define CLK_CON_GAT_GOUT_BLK_APM_UID_APM_USI0_USI_IPCLKPORT_IPCLK			0x2030
#define CLK_CON_GAT_GOUT_BLK_APM_UID_APM_USI0_USI_IPCLKPORT_PCLK			0x2034
#define CLK_CON_GAT_GOUT_BLK_APM_UID_APM_USI1_UART_IPCLKPORT_IPCLK			0x2038
#define CLK_CON_GAT_GOUT_BLK_APM_UID_APM_USI1_UART_IPCLKPORT_PCLK			0x203c
#define CLK_CON_GAT_GOUT_BLK_APM_UID_D_TZPC_APM_IPCLKPORT_PCLK				0x2040
#define CLK_CON_GAT_GOUT_BLK_APM_UID_GPC_APM_IPCLKPORT_PCLK				0x2044
#define CLK_CON_GAT_GOUT_BLK_APM_UID_GREBEINTEGRATION_IPCLKPORT_HCLK			0x2048
#define CLK_CON_GAT_GOUT_BLK_APM_UID_INTMEM_IPCLKPORT_ACLK				0x204c
#define CLK_CON_GAT_GOUT_BLK_APM_UID_INTMEM_IPCLKPORT_PCLK				0x2050
#define CLK_CON_GAT_GOUT_BLK_APM_UID_LHM_AXI_G_SWD_IPCLKPORT_I_CLK			0x2054
#define CLK_CON_GAT_GOUT_BLK_APM_UID_LHM_AXI_P_AOCAPM_IPCLKPORT_I_CLK			0x2058
#define CLK_CON_GAT_GOUT_BLK_APM_UID_LHM_AXI_P_APM_IPCLKPORT_I_CLK			0x205c
#define CLK_CON_GAT_GOUT_BLK_APM_UID_LHS_AXI_D_APM_IPCLKPORT_I_CLK			0x2060
#define CLK_CON_GAT_GOUT_BLK_APM_UID_LHS_AXI_G_DBGCORE_IPCLKPORT_I_CLK			0x2064
#define CLK_CON_GAT_GOUT_BLK_APM_UID_LHS_AXI_G_SCAN2DRAM_IPCLKPORT_I_CLK		0x2068
#define CLK_CON_GAT_GOUT_BLK_APM_UID_MAILBOX_APM_AOC_IPCLKPORT_PCLK			0x206c
#define CLK_CON_GAT_GOUT_BLK_APM_UID_MAILBOX_APM_AP_IPCLKPORT_PCLK			0x2070
#define CLK_CON_GAT_GOUT_BLK_APM_UID_MAILBOX_APM_GSA_IPCLKPORT_PCLK			0x2074
#define CLK_CON_GAT_GOUT_BLK_APM_UID_MAILBOX_APM_SWD_IPCLKPORT_PCLK			0x207c
#define CLK_CON_GAT_GOUT_BLK_APM_UID_MAILBOX_APM_TPU_IPCLKPORT_PCLK			0x2080
#define CLK_CON_GAT_GOUT_BLK_APM_UID_MAILBOX_AP_AOC_IPCLKPORT_PCLK			0x2084
#define CLK_CON_GAT_GOUT_BLK_APM_UID_MAILBOX_AP_DBGCORE_IPCLKPORT_PCLK			0x2088
#define CLK_CON_GAT_GOUT_BLK_APM_UID_PMU_INTR_GEN_IPCLKPORT_PCLK			0x208c
#define CLK_CON_GAT_GOUT_BLK_APM_UID_ROM_CRC32_HOST_IPCLKPORT_ACLK			0x2090
#define CLK_CON_GAT_GOUT_BLK_APM_UID_ROM_CRC32_HOST_IPCLKPORT_PCLK			0x2094
#define CLK_CON_GAT_GOUT_BLK_APM_UID_RSTNSYNC_CLK_APM_BUS_IPCLKPORT_CLK			0x2098
#define CLK_CON_GAT_GOUT_BLK_APM_UID_RSTNSYNC_CLK_APM_USI0_UART_IPCLKPORT_CLK		0x209c
#define CLK_CON_GAT_GOUT_BLK_APM_UID_RSTNSYNC_CLK_APM_USI0_USI_IPCLKPORT_CLK		0x20a0
#define CLK_CON_GAT_GOUT_BLK_APM_UID_RSTNSYNC_CLK_APM_USI1_UART_IPCLKPORT_CLK		0x20a4
#define CLK_CON_GAT_GOUT_BLK_APM_UID_SPEEDY_APM_IPCLKPORT_PCLK				0x20a8
#define CLK_CON_GAT_GOUT_BLK_APM_UID_SPEEDY_SUB_APM_IPCLKPORT_PCLK			0x20ac
#define CLK_CON_GAT_GOUT_BLK_APM_UID_SSMT_D_APM_IPCLKPORT_ACLK				0x20b0
#define CLK_CON_GAT_GOUT_BLK_APM_UID_SSMT_D_APM_IPCLKPORT_PCLK				0x20b4
#define CLK_CON_GAT_GOUT_BLK_APM_UID_SSMT_G_DBGCORE_IPCLKPORT_ACLK			0x20b8
#define CLK_CON_GAT_GOUT_BLK_APM_UID_SSMT_G_DBGCORE_IPCLKPORT_PCLK			0x20bc
#define CLK_CON_GAT_GOUT_BLK_APM_UID_SS_DBGCORE_IPCLKPORT_SS_DBGCORE_IPCLKPORT_HCLK	0x20c0
#define CLK_CON_GAT_GOUT_BLK_APM_UID_SYSMMU_D_APM_IPCLKPORT_CLK_S2			0x20c4
#define CLK_CON_GAT_GOUT_BLK_APM_UID_SYSREG_APM_IPCLKPORT_PCLK				0x20cc
#define CLK_CON_GAT_GOUT_BLK_APM_UID_UASC_APM_IPCLKPORT_ACLK				0x20d0
#define CLK_CON_GAT_GOUT_BLK_APM_UID_UASC_APM_IPCLKPORT_PCLK				0x20d4
#define CLK_CON_GAT_GOUT_BLK_APM_UID_UASC_DBGCORE_IPCLKPORT_ACLK			0x20d8
#define CLK_CON_GAT_GOUT_BLK_APM_UID_UASC_DBGCORE_IPCLKPORT_PCLK			0x20dc
#define CLK_CON_GAT_GOUT_BLK_APM_UID_UASC_G_SWD_IPCLKPORT_ACLK				0x20e0
#define CLK_CON_GAT_GOUT_BLK_APM_UID_UASC_G_SWD_IPCLKPORT_PCLK				0x20e4
#define CLK_CON_GAT_GOUT_BLK_APM_UID_UASC_P_AOCAPM_IPCLKPORT_ACLK			0x20e8
#define CLK_CON_GAT_GOUT_BLK_APM_UID_UASC_P_AOCAPM_IPCLKPORT_PCLK			0x20ec
#define CLK_CON_GAT_GOUT_BLK_APM_UID_UASC_P_APM_IPCLKPORT_ACLK				0x20f0
#define CLK_CON_GAT_GOUT_BLK_APM_UID_UASC_P_APM_IPCLKPORT_PCLK				0x20f4
#define CLK_CON_GAT_GOUT_BLK_APM_UID_WDT_APM_IPCLKPORT_PCLK				0x20f8
#define CLK_CON_GAT_GOUT_BLK_APM_UID_XIU_DP_APM_IPCLKPORT_ACLK				0x20fc
#define PCH_CON_LHM_AXI_G_SWD_PCH							0x3000
#define PCH_CON_LHM_AXI_P_AOCAPM_PCH							0x3004
#define PCH_CON_LHM_AXI_P_APM_PCH							0x3008
#define PCH_CON_LHS_AXI_D_APM_PCH							0x300c
#define PCH_CON_LHS_AXI_G_DBGCORE_PCH							0x3010
#define PCH_CON_LHS_AXI_G_SCAN2DRAM_PCH							0x3014
#define QCH_CON_APBIF_GPIO_ALIVE_QCH							0x3018
#define QCH_CON_APBIF_GPIO_FAR_ALIVE_QCH						0x301c
#define QCH_CON_APBIF_PMU_ALIVE_QCH							0x3020
#define QCH_CON_APBIF_RTC_QCH								0x3024
#define QCH_CON_APBIF_TRTC_QCH								0x3028
#define QCH_CON_APM_CMU_APM_QCH								0x302c
#define QCH_CON_APM_USI0_UART_QCH							0x3030
#define QCH_CON_APM_USI0_USI_QCH							0x3034
#define QCH_CON_APM_USI1_UART_QCH							0x3038
#define QCH_CON_D_TZPC_APM_QCH								0x303c
#define QCH_CON_GPC_APM_QCH								0x3040
#define QCH_CON_GREBEINTEGRATION_QCH_DBG						0x3044
#define QCH_CON_GREBEINTEGRATION_QCH_GREBE						0x3048
#define QCH_CON_INTMEM_QCH								0x304c
#define QCH_CON_LHM_AXI_G_SWD_QCH							0x3050
#define QCH_CON_LHM_AXI_P_AOCAPM_QCH							0x3054
#define QCH_CON_LHM_AXI_P_APM_QCH							0x3058
#define QCH_CON_LHS_AXI_D_APM_QCH							0x305c
#define QCH_CON_LHS_AXI_G_DBGCORE_QCH							0x3060
#define QCH_CON_LHS_AXI_G_SCAN2DRAM_QCH							0x3064
#define QCH_CON_MAILBOX_APM_AOC_QCH							0x3068
#define QCH_CON_MAILBOX_APM_AP_QCH							0x306c
#define QCH_CON_MAILBOX_APM_GSA_QCH							0x3070
#define QCH_CON_MAILBOX_APM_SWD_QCH							0x3078
#define QCH_CON_MAILBOX_APM_TPU_QCH							0x307c
#define QCH_CON_MAILBOX_AP_AOC_QCH							0x3080
#define QCH_CON_MAILBOX_AP_DBGCORE_QCH							0x3084
#define QCH_CON_PMU_INTR_GEN_QCH							0x3088
#define QCH_CON_ROM_CRC32_HOST_QCH							0x308c
#define QCH_CON_RSTNSYNC_CLK_APM_BUS_QCH_GREBE						0x3090
#define QCH_CON_RSTNSYNC_CLK_APM_BUS_QCH_GREBE_DBG					0x3094
#define QCH_CON_SPEEDY_APM_QCH								0x3098
#define QCH_CON_SPEEDY_SUB_APM_QCH							0x309c
#define QCH_CON_SSMT_D_APM_QCH								0x30a0
#define QCH_CON_SSMT_G_DBGCORE_QCH							0x30a4
#define QCH_CON_SS_DBGCORE_QCH_DBG							0x30a8
#define QCH_CON_SS_DBGCORE_QCH_GREBE							0x30ac
#define QCH_CON_SYSMMU_D_APM_QCH							0x30b0
#define QCH_CON_SYSREG_APM_QCH								0x30b8
#define QCH_CON_UASC_APM_QCH								0x30bc
#define QCH_CON_UASC_DBGCORE_QCH							0x30c0
#define QCH_CON_UASC_G_SWD_QCH								0x30c4
#define QCH_CON_UASC_P_AOCAPM_QCH							0x30c8
#define QCH_CON_UASC_P_APM_QCH								0x30cc
#define QCH_CON_WDT_APM_QCH								0x30d0
#define QUEUE_CTRL_REG_BLK_APM_CMU_APM							0x3c00

static const unsigned long apm_clk_regs[] __initconst = {
	APM_CMU_APM_CONTROLLER_OPTION,
	CLKOUT_CON_BLK_APM_CMU_APM_CLKOUT0,
	CLK_CON_MUX_MUX_CLKCMU_APM_FUNC,
	CLK_CON_MUX_MUX_CLKCMU_APM_FUNCSRC,
	CLK_CON_DIV_DIV_CLK_APM_BOOST,
	CLK_CON_DIV_DIV_CLK_APM_USI0_UART,
	CLK_CON_DIV_DIV_CLK_APM_USI0_USI,
	CLK_CON_DIV_DIV_CLK_APM_USI1_UART,
	CLK_CON_GAT_CLK_BLK_APM_UID_APM_CMU_APM_IPCLKPORT_PCLK,
	CLK_CON_GAT_CLK_BUS0_BOOST_OPTION1,
	CLK_CON_GAT_CLK_CMU_BOOST_OPTION1,
	CLK_CON_GAT_CLK_CORE_BOOST_OPTION1,
	CLK_CON_GAT_GATE_CLKCMU_APM_FUNC,
	CLK_CON_GAT_GOUT_BLK_APM_UID_APBIF_GPIO_ALIVE_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_APBIF_GPIO_FAR_ALIVE_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_APBIF_PMU_ALIVE_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_APBIF_RTC_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_APBIF_TRTC_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_APM_USI0_UART_IPCLKPORT_IPCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_APM_USI0_UART_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_APM_USI0_USI_IPCLKPORT_IPCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_APM_USI0_USI_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_APM_USI1_UART_IPCLKPORT_IPCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_APM_USI1_UART_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_D_TZPC_APM_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_GPC_APM_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_GREBEINTEGRATION_IPCLKPORT_HCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_INTMEM_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_INTMEM_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_LHM_AXI_G_SWD_IPCLKPORT_I_CLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_LHM_AXI_P_AOCAPM_IPCLKPORT_I_CLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_LHM_AXI_P_APM_IPCLKPORT_I_CLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_LHS_AXI_D_APM_IPCLKPORT_I_CLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_LHS_AXI_G_DBGCORE_IPCLKPORT_I_CLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_LHS_AXI_G_SCAN2DRAM_IPCLKPORT_I_CLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_MAILBOX_APM_AOC_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_MAILBOX_APM_AP_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_MAILBOX_APM_GSA_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_MAILBOX_APM_SWD_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_MAILBOX_APM_TPU_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_MAILBOX_AP_AOC_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_MAILBOX_AP_DBGCORE_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_PMU_INTR_GEN_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_ROM_CRC32_HOST_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_ROM_CRC32_HOST_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_RSTNSYNC_CLK_APM_BUS_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_RSTNSYNC_CLK_APM_USI0_UART_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_RSTNSYNC_CLK_APM_USI0_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_RSTNSYNC_CLK_APM_USI1_UART_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_SPEEDY_APM_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_SPEEDY_SUB_APM_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_SSMT_D_APM_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_SSMT_D_APM_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_SSMT_G_DBGCORE_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_SSMT_G_DBGCORE_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_SS_DBGCORE_IPCLKPORT_SS_DBGCORE_IPCLKPORT_HCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_SYSMMU_D_APM_IPCLKPORT_CLK_S2,
	CLK_CON_GAT_GOUT_BLK_APM_UID_SYSREG_APM_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_UASC_APM_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_UASC_APM_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_UASC_DBGCORE_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_UASC_DBGCORE_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_UASC_G_SWD_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_UASC_G_SWD_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_UASC_P_AOCAPM_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_UASC_P_AOCAPM_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_UASC_P_APM_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_UASC_P_APM_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_WDT_APM_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_APM_UID_XIU_DP_APM_IPCLKPORT_ACLK,
};

PNAME(mout_apm_func_p)		= { "oscclk", "mout_apm_funcsrc",
				    "pad_clk_apm", "oscclk" };
PNAME(mout_apm_funcsrc_p)	= { "pll_alv_div2_apm", "pll_alv_div4_apm",
				    "pll_alv_div16_apm" };

static const struct samsung_fixed_rate_clock apm_fixed_clks[] __initconst = {
	FRATE(CLK_APM_PLL_DIV2_APM, "pll_alv_div2_apm", NULL, 0, 393216000),
	FRATE(CLK_APM_PLL_DIV4_APM, "pll_alv_div4_apm", NULL, 0, 196608000),
	FRATE(CLK_APM_PLL_DIV16_APM, "pll_alv_div16_apm", NULL, 0, 49152000),
};

static const struct samsung_mux_clock apm_mux_clks[] __initconst = {
	MUX(CLK_MOUT_APM_FUNC, "mout_apm_func", mout_apm_func_p,
	    CLK_CON_MUX_MUX_CLKCMU_APM_FUNC, 4, 1),
	MUX(CLK_MOUT_APM_FUNCSRC, "mout_apm_funcsrc", mout_apm_funcsrc_p,
	    CLK_CON_MUX_MUX_CLKCMU_APM_FUNCSRC, 3, 1),
};

static const struct samsung_div_clock apm_div_clks[] __initconst = {
	DIV(CLK_DOUT_APM_BOOST, "dout_apm_boost", "gout_apm_func",
	    CLK_CON_DIV_DIV_CLK_APM_BOOST, 0, 1),
	DIV(CLK_DOUT_APM_USI0_UART, "dout_apm_usi0_uart", "gout_apm_func",
	    CLK_CON_DIV_DIV_CLK_APM_USI0_UART, 0, 7),
	DIV(CLK_DOUT_APM_USI0_USI, "dout_apm_usi0_usi", "gout_apm_func",
	    CLK_CON_DIV_DIV_CLK_APM_USI0_USI, 0, 7),
	DIV(CLK_DOUT_APM_USI1_UART, "dout_apm_usi1_uart", "gout_apm_func",
	    CLK_CON_DIV_DIV_CLK_APM_USI1_UART, 0, 7),
};

static const struct samsung_gate_clock apm_gate_clks[] __initconst = {
	GATE(CLK_GOUT_APM_APM_CMU_APM_PCLK,
	     "gout_apm_apm_cmu_apm_pclk", "mout_apm_func",
	     CLK_CON_GAT_CLK_BLK_APM_UID_APM_CMU_APM_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_BUS0_BOOST_OPTION1, "gout_bus0_boost_option1",
	     "dout_apm_boost", CLK_CON_GAT_CLK_BUS0_BOOST_OPTION1, 21, 0, 0),
	GATE(CLK_GOUT_CMU_BOOST_OPTION1, "gout_cmu_boost_option1",
	     "dout_apm_boost", CLK_CON_GAT_CLK_CMU_BOOST_OPTION1, 21, 0, 0),
	GATE(CLK_GOUT_CORE_BOOST_OPTION1, "gout_core_boost_option1",
	     "dout_apm_boost", CLK_CON_GAT_CLK_CORE_BOOST_OPTION1, 21, 0, 0),
	GATE(CLK_GOUT_APM_FUNC, "gout_apm_func", "mout_apm_func",
	     CLK_CON_GAT_GATE_CLKCMU_APM_FUNC, 21, 0, 0),
	GATE(CLK_GOUT_APM_APBIF_GPIO_ALIVE_PCLK,
	     "gout_apm_apbif_gpio_alive_pclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_APBIF_GPIO_ALIVE_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_APBIF_GPIO_FAR_ALIVE_PCLK,
	     "gout_apm_apbif_gpio_far_alive_pclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_APBIF_GPIO_FAR_ALIVE_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_APBIF_PMU_ALIVE_PCLK,
	     "gout_apm_apbif_pmu_alive_pclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_APBIF_PMU_ALIVE_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_APBIF_RTC_PCLK,
	     "gout_apm_apbif_rtc_pclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_APBIF_RTC_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_APBIF_TRTC_PCLK,
	     "gout_apm_apbif_trtc_pclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_APBIF_TRTC_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_APM_USI0_UART_IPCLK,
	     "gout_apm_apm_usi0_uart_ipclk", "dout_apm_usi0_uart",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_APM_USI0_UART_IPCLKPORT_IPCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_APM_USI0_UART_PCLK,
	     "gout_apm_apm_usi0_uart_pclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_APM_USI0_UART_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_APM_USI0_USI_IPCLK,
	     "gout_apm_apm_usi0_usi_ipclk", "dout_apm_usi0_usi",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_APM_USI0_USI_IPCLKPORT_IPCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_APM_USI0_USI_PCLK,
	     "gout_apm_apm_usi0_usi_pclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_APM_USI0_USI_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_APM_USI1_UART_IPCLK,
	     "gout_apm_apm_usi1_uart_ipclk", "dout_apm_usi1_uart",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_APM_USI1_UART_IPCLKPORT_IPCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_APM_USI1_UART_PCLK,
	     "gout_apm_apm_usi1_uart_pclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_APM_USI1_UART_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_D_TZPC_APM_PCLK,
	     "gout_apm_d_tzpc_apm_pclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_D_TZPC_APM_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_GPC_APM_PCLK,
	     "gout_apm_gpc_apm_pclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_GPC_APM_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_GREBEINTEGRATION_HCLK,
	     "gout_apm_grebeintegration_hclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_GREBEINTEGRATION_IPCLKPORT_HCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_INTMEM_ACLK,
	     "gout_apm_intmem_aclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_INTMEM_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_INTMEM_PCLK,
	     "gout_apm_intmem_pclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_INTMEM_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_LHM_AXI_G_SWD_I_CLK,
	     "gout_apm_lhm_axi_g_swd_i_clk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_LHM_AXI_G_SWD_IPCLKPORT_I_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_LHM_AXI_P_AOCAPM_I_CLK,
	     "gout_apm_lhm_axi_p_aocapm_i_clk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_LHM_AXI_P_AOCAPM_IPCLKPORT_I_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_LHM_AXI_P_APM_I_CLK,
	     "gout_apm_lhm_axi_p_apm_i_clk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_LHS_AXI_D_APM_IPCLKPORT_I_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_LHS_AXI_D_APM_I_CLK,
	     "gout_apm_lhs_axi_d_apm_i_clk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_LHS_AXI_D_APM_IPCLKPORT_I_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_LHS_AXI_G_DBGCORE_I_CLK,
	     "gout_apm_lhs_axi_g_dbgcore_i_clk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_LHS_AXI_G_DBGCORE_IPCLKPORT_I_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_LHS_AXI_G_SCAN2DRAM_I_CLK,
	     "gout_apm_lhs_axi_g_scan2dram_i_clk",
	     "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_LHS_AXI_G_SCAN2DRAM_IPCLKPORT_I_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_APM_AOC_PCLK,
	     "gout_apm_mailbox_apm_aoc_pclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_MAILBOX_APM_AOC_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_APM_AP_PCLK,
	     "gout_apm_mailbox_apm_ap_pclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_MAILBOX_APM_AP_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_APM_GSA_PCLK,
	     "gout_apm_mailbox_apm_gsa_pclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_MAILBOX_APM_GSA_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_APM_SWD_PCLK,
	     "gout_apm_mailbox_apm_swd_pclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_MAILBOX_APM_SWD_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_APM_TPU_PCLK,
	     "gout_apm_mailbox_apm_tpu_pclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_MAILBOX_APM_TPU_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_AP_AOC_PCLK,
	     "gout_apm_mailbox_ap_aoc_pclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_MAILBOX_AP_AOC_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_MAILBOX_AP_DBGCORE_PCLK,
	     "gout_apm_mailbox_ap_dbgcore_pclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_MAILBOX_AP_DBGCORE_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_PMU_INTR_GEN_PCLK,
	     "gout_apm_pmu_intr_gen_pclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_PMU_INTR_GEN_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_ROM_CRC32_HOST_ACLK,
	     "gout_apm_rom_crc32_host_aclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_ROM_CRC32_HOST_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_ROM_CRC32_HOST_PCLK,
	     "gout_apm_rom_crc32_host_pclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_ROM_CRC32_HOST_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_CLK_APM_BUS_CLK,
	     "gout_apm_clk_apm_bus_clk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_RSTNSYNC_CLK_APM_BUS_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_CLK_APM_USI0_UART_CLK,
	     "gout_apm_clk_apm_usi0_uart_clk",
	     "dout_apm_usi0_uart",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_RSTNSYNC_CLK_APM_USI0_UART_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_CLK_APM_USI0_USI_CLK,
	     "gout_apm_clk_apm_usi0_usi_clk",
	     "dout_apm_usi0_usi",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_RSTNSYNC_CLK_APM_USI0_UART_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_CLK_APM_USI1_UART_CLK,
	     "gout_apm_clk_apm_usi1_uart_clk",
	     "dout_apm_usi1_uart",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_RSTNSYNC_CLK_APM_USI1_UART_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_SPEEDY_APM_PCLK,
	     "gout_apm_speedy_apm_pclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_SPEEDY_APM_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_SPEEDY_SUB_APM_PCLK,
	     "gout_apm_speedy_sub_apm_pclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_SPEEDY_SUB_APM_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_SSMT_D_APM_ACLK,
	     "gout_apm_ssmt_d_apm_aclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_SSMT_D_APM_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_SSMT_D_APM_PCLK,
	     "gout_apm_ssmt_d_apm_pclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_SSMT_D_APM_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_SSMT_G_DBGCORE_ACLK,
	     "gout_apm_ssmt_g_dbgcore_aclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_SSMT_G_DBGCORE_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_SSMT_G_DBGCORE_PCLK,
	     "gout_apm_ssmt_g_dbgcore_pclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_SSMT_G_DBGCORE_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_SS_DBGCORE_SS_DBGCORE_HCLK,
	     "gout_apm_ss_dbgcore_ss_dbgcore_hclk",
	     "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_SS_DBGCORE_IPCLKPORT_SS_DBGCORE_IPCLKPORT_HCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_SYSMMU_D_APM_CLK_S2,
	     "gout_apm_sysmmu_d_dpm_clk_s2", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_SYSMMU_D_APM_IPCLKPORT_CLK_S2,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_SYSREG_APM_PCLK,
	     "gout_apm_sysreg_apm_pclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_SYSREG_APM_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_UASC_APM_ACLK,
	     "gout_apm_uasc_apm_aclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_UASC_APM_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_UASC_APM_PCLK,
	     "gout_apm_uasc_apm_pclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_UASC_APM_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_UASC_DBGCORE_ACLK,
	     "gout_apm_uasc_dbgcore_aclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_UASC_DBGCORE_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_UASC_DBGCORE_PCLK,
	     "gout_apm_uasc_dbgcore_pclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_UASC_DBGCORE_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_UASC_G_SWD_ACLK,
	     "gout_apm_uasc_g_swd_aclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_UASC_G_SWD_IPCLKPORT_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_UASC_G_SWD_PCLK,
	     "gout_apm_uasc_g_swd_pclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_UASC_G_SWD_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_UASC_P_AOCAPM_ACLK,
	     "gout_apm_uasc_p_aocapm_aclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_UASC_P_AOCAPM_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_APM_UASC_P_AOCAPM_PCLK,
	     "gout_apm_uasc_p_aocapm_pclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_UASC_G_SWD_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_UASC_P_APM_ACLK,
	     "gout_apm_uasc_p_apm_aclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_UASC_P_APM_IPCLKPORT_ACLK, 21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_APM_UASC_P_APM_PCLK,
	     "gout_apm_uasc_p_apm_pclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_UASC_P_APM_IPCLKPORT_PCLK, 21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_APM_WDT_APM_PCLK,
	     "gout_apm_wdt_apm_pclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_WDT_APM_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_APM_XIU_DP_APM_ACLK,
	     "gout_apm_xiu_dp_apm_aclk", "gout_apm_func",
	     CLK_CON_GAT_GOUT_BLK_APM_UID_XIU_DP_APM_IPCLKPORT_ACLK, 21, CLK_IS_CRITICAL, 0),
};

static const struct samsung_cmu_info apm_cmu_info __initconst = {
	.mux_clks		= apm_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(apm_mux_clks),
	.div_clks		= apm_div_clks,
	.nr_div_clks		= ARRAY_SIZE(apm_div_clks),
	.gate_clks		= apm_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(apm_gate_clks),
	.fixed_clks		= apm_fixed_clks,
	.nr_fixed_clks		= ARRAY_SIZE(apm_fixed_clks),
	.nr_clk_ids		= CLKS_NR_APM,
	.clk_regs		= apm_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(apm_clk_regs),
};

/* ---- CMU_HSI0 ------------------------------------------------------------ */

/* Register Offset definitions for CMU_HSI0 (0x11000000) */
#define PLL_LOCKTIME_PLL_USB								0x0004
#define PLL_CON0_PLL_USB								0x0140
#define PLL_CON1_PLL_USB								0x0144
#define PLL_CON2_PLL_USB								0x0148
#define PLL_CON3_PLL_USB								0x014c
#define PLL_CON4_PLL_USB								0x0150
#define PLL_CON0_MUX_CLKCMU_HSI0_ALT_USER						0x0600
#define PLL_CON1_MUX_CLKCMU_HSI0_ALT_USER						0x0604
#define PLL_CON0_MUX_CLKCMU_HSI0_BUS_USER						0x0610
#define PLL_CON1_MUX_CLKCMU_HSI0_BUS_USER						0x0614
#define PLL_CON0_MUX_CLKCMU_HSI0_DPGTC_USER						0x0620
#define PLL_CON1_MUX_CLKCMU_HSI0_DPGTC_USER						0x0624
#define PLL_CON0_MUX_CLKCMU_HSI0_TCXO_USER						0x0630
#define PLL_CON1_MUX_CLKCMU_HSI0_TCXO_USER						0x0634
#define PLL_CON0_MUX_CLKCMU_HSI0_USB20_USER						0x0640
#define PLL_CON1_MUX_CLKCMU_HSI0_USB20_USER						0x0644
#define PLL_CON0_MUX_CLKCMU_HSI0_USB31DRD_USER						0x0650
#define PLL_CON1_MUX_CLKCMU_HSI0_USB31DRD_USER						0x0654
#define PLL_CON0_MUX_CLKCMU_HSI0_USPDPDBG_USER						0x0660
#define PLL_CON1_MUX_CLKCMU_HSI0_USPDPDBG_USER						0x0664
#define HSI0_CMU_HSI0_CONTROLLER_OPTION							0x0800
#define CLKOUT_CON_BLK_HSI0_CMU_HSI0_CLKOUT0						0x0810
#define CLK_CON_MUX_MUX_CLK_HSI0_BUS							0x1000
#define CLK_CON_MUX_MUX_CLK_HSI0_USB20_REF						0x1004
#define CLK_CON_MUX_MUX_CLK_HSI0_USB31DRD						0x1008
#define CLK_CON_DIV_DIV_CLK_HSI0_USB31DRD						0x1800
#define CLK_CON_GAT_CLK_BLK_HSI0_UID_HSI0_CMU_HSI0_IPCLKPORT_PCLK			0x2000
#define CLK_CON_GAT_CLK_BLK_HSI0_UID_USB31DRD_IPCLKPORT_I_USB31DRD_SUSPEND_CLK_26	0x2004
#define CLK_CON_GAT_CLK_HSI0_ALT							0x2008
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_DP_LINK_IPCLKPORT_I_DP_GTC_CLK			0x200c
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_DP_LINK_IPCLKPORT_I_PCLK				0x2010
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_D_TZPC_HSI0_IPCLKPORT_PCLK			0x2014
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_ETR_MIU_IPCLKPORT_I_ACLK				0x2018
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_ETR_MIU_IPCLKPORT_I_PCLK				0x201c
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_GPC_HSI0_IPCLKPORT_PCLK				0x2020
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_LHM_AXI_G_ETR_HSI0_IPCLKPORT_I_CLK		0x2024
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_LHM_AXI_P_AOCHSI0_IPCLKPORT_I_CLK			0x2028
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_LHM_AXI_P_HSI0_IPCLKPORT_I_CLK			0x202c
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_LHS_ACEL_D_HSI0_IPCLKPORT_I_CLK			0x2030
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_LHS_AXI_D_HSI0AOC_IPCLKPORT_I_CLK			0x2034
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_PPMU_HSI0_AOC_IPCLKPORT_ACLK			0x2038
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_PPMU_HSI0_AOC_IPCLKPORT_PCLK			0x203c
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_PPMU_HSI0_BUS0_IPCLKPORT_ACLK			0x2040
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_PPMU_HSI0_BUS0_IPCLKPORT_PCLK			0x2044
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_RSTNSYNC_CLK_HSI0_BUS_IPCLKPORT_CLK		0x2048
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_SSMT_USB_IPCLKPORT_ACLK				0x204c
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_SSMT_USB_IPCLKPORT_PCLK				0x2050
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_SYSMMU_USB_IPCLKPORT_CLK_S2			0x2054
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_SYSREG_HSI0_IPCLKPORT_PCLK			0x2058
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_UASC_HSI0_CTRL_IPCLKPORT_ACLK			0x205c
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_UASC_HSI0_CTRL_IPCLKPORT_PCLK			0x2060
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_UASC_HSI0_LINK_IPCLKPORT_ACLK			0x2064
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_UASC_HSI0_LINK_IPCLKPORT_PCLK			0x2068
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_ACLK_PHYCTRL			0x206c
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_BUS_CLK_EARLY			0x2070
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_I_USB20_PHY_REFCLK_26		0x2074
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_I_USB31DRD_REF_CLK_40		0x2078
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_I_USBDPPHY_REF_SOC_PLL		0x207c
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_I_USBDPPHY_SCL_APB_PCLK	0x2080
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_I_USBPCS_APB_CLK		0x2084
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_USBDPPHY_I_ACLK		0x2088
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_USBDPPHY_UDBG_I_APB_PCLK	0x208c
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_XIU_D0_HSI0_IPCLKPORT_ACLK			0x2090
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_XIU_D1_HSI0_IPCLKPORT_ACLK			0x2094
#define CLK_CON_GAT_GOUT_BLK_HSI0_UID_XIU_P_HSI0_IPCLKPORT_ACLK				0x2098
#define DMYQCH_CON_USB31DRD_QCH								0x3000
#define DMYQCH_CON_USB31DRD_QCH_REF							0x3004
#define PCH_CON_LHM_AXI_G_ETR_HSI0_PCH							0x3008
#define PCH_CON_LHM_AXI_P_AOCHSI0_PCH							0x300c
#define PCH_CON_LHM_AXI_P_HSI0_PCH							0x3010
#define PCH_CON_LHS_ACEL_D_HSI0_PCH							0x3014
#define PCH_CON_LHS_AXI_D_HSI0AOC_PCH							0x3018
#define QCH_CON_DP_LINK_QCH_GTC_CLK							0x301c
#define QCH_CON_DP_LINK_QCH_PCLK							0x3020
#define QCH_CON_D_TZPC_HSI0_QCH								0x3024
#define QCH_CON_ETR_MIU_QCH_ACLK							0x3028
#define QCH_CON_ETR_MIU_QCH_PCLK							0x302c
#define QCH_CON_GPC_HSI0_QCH								0x3030
#define QCH_CON_HSI0_CMU_HSI0_QCH							0x3034
#define QCH_CON_LHM_AXI_G_ETR_HSI0_QCH							0x3038
#define QCH_CON_LHM_AXI_P_AOCHSI0_QCH							0x303c
#define QCH_CON_LHM_AXI_P_HSI0_QCH							0x3040
#define QCH_CON_LHS_ACEL_D_HSI0_QCH							0x3044
#define QCH_CON_LHS_AXI_D_HSI0AOC_QCH							0x3048
#define QCH_CON_PPMU_HSI0_AOC_QCH							0x304c
#define QCH_CON_PPMU_HSI0_BUS0_QCH							0x3050
#define QCH_CON_SSMT_USB_QCH								0x3054
#define QCH_CON_SYSMMU_USB_QCH								0x3058
#define QCH_CON_SYSREG_HSI0_QCH								0x305c
#define QCH_CON_UASC_HSI0_CTRL_QCH							0x3060
#define QCH_CON_UASC_HSI0_LINK_QCH							0x3064
#define QCH_CON_USB31DRD_QCH_APB							0x3068
#define QCH_CON_USB31DRD_QCH_DBG							0x306c
#define QCH_CON_USB31DRD_QCH_PCS							0x3070
#define QCH_CON_USB31DRD_QCH_SLV_CTRL							0x3074
#define QCH_CON_USB31DRD_QCH_SLV_LINK							0x3078
#define QUEUE_CTRL_REG_BLK_HSI0_CMU_HSI0						0x3c00

static const unsigned long hsi0_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_USB,
	PLL_CON0_PLL_USB,
	PLL_CON1_PLL_USB,
	PLL_CON2_PLL_USB,
	PLL_CON3_PLL_USB,
	PLL_CON4_PLL_USB,
	PLL_CON0_MUX_CLKCMU_HSI0_ALT_USER,
	PLL_CON1_MUX_CLKCMU_HSI0_ALT_USER,
	PLL_CON0_MUX_CLKCMU_HSI0_BUS_USER,
	PLL_CON1_MUX_CLKCMU_HSI0_BUS_USER,
	PLL_CON0_MUX_CLKCMU_HSI0_DPGTC_USER,
	PLL_CON1_MUX_CLKCMU_HSI0_DPGTC_USER,
	PLL_CON0_MUX_CLKCMU_HSI0_TCXO_USER,
	PLL_CON1_MUX_CLKCMU_HSI0_TCXO_USER,
	PLL_CON0_MUX_CLKCMU_HSI0_USB20_USER,
	PLL_CON1_MUX_CLKCMU_HSI0_USB20_USER,
	PLL_CON0_MUX_CLKCMU_HSI0_USB31DRD_USER,
	PLL_CON1_MUX_CLKCMU_HSI0_USB31DRD_USER,
	PLL_CON0_MUX_CLKCMU_HSI0_USPDPDBG_USER,
	PLL_CON1_MUX_CLKCMU_HSI0_USPDPDBG_USER,
	HSI0_CMU_HSI0_CONTROLLER_OPTION,
	CLKOUT_CON_BLK_HSI0_CMU_HSI0_CLKOUT0,
	CLK_CON_MUX_MUX_CLK_HSI0_BUS,
	CLK_CON_MUX_MUX_CLK_HSI0_USB20_REF,
	CLK_CON_MUX_MUX_CLK_HSI0_USB31DRD,
	CLK_CON_DIV_DIV_CLK_HSI0_USB31DRD,
	CLK_CON_GAT_CLK_BLK_HSI0_UID_HSI0_CMU_HSI0_IPCLKPORT_PCLK,
	CLK_CON_GAT_CLK_BLK_HSI0_UID_USB31DRD_IPCLKPORT_I_USB31DRD_SUSPEND_CLK_26,
	CLK_CON_GAT_CLK_HSI0_ALT,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_DP_LINK_IPCLKPORT_I_DP_GTC_CLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_DP_LINK_IPCLKPORT_I_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_D_TZPC_HSI0_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_ETR_MIU_IPCLKPORT_I_ACLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_ETR_MIU_IPCLKPORT_I_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_GPC_HSI0_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_LHM_AXI_G_ETR_HSI0_IPCLKPORT_I_CLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_LHM_AXI_P_AOCHSI0_IPCLKPORT_I_CLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_LHM_AXI_P_HSI0_IPCLKPORT_I_CLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_LHS_ACEL_D_HSI0_IPCLKPORT_I_CLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_LHS_AXI_D_HSI0AOC_IPCLKPORT_I_CLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_PPMU_HSI0_AOC_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_PPMU_HSI0_AOC_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_PPMU_HSI0_BUS0_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_PPMU_HSI0_BUS0_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_RSTNSYNC_CLK_HSI0_BUS_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_SSMT_USB_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_SSMT_USB_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_SYSMMU_USB_IPCLKPORT_CLK_S2,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_SYSREG_HSI0_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_UASC_HSI0_CTRL_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_UASC_HSI0_CTRL_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_UASC_HSI0_LINK_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_UASC_HSI0_LINK_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_ACLK_PHYCTRL,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_BUS_CLK_EARLY,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_I_USB20_PHY_REFCLK_26,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_I_USB31DRD_REF_CLK_40,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_I_USBDPPHY_REF_SOC_PLL,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_I_USBDPPHY_SCL_APB_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_I_USBPCS_APB_CLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_USBDPPHY_I_ACLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_USBDPPHY_UDBG_I_APB_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_XIU_D0_HSI0_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_XIU_D1_HSI0_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_HSI0_UID_XIU_P_HSI0_IPCLKPORT_ACLK,
	DMYQCH_CON_USB31DRD_QCH,
	DMYQCH_CON_USB31DRD_QCH_REF,
	PCH_CON_LHM_AXI_G_ETR_HSI0_PCH,
	PCH_CON_LHM_AXI_P_AOCHSI0_PCH,
	PCH_CON_LHM_AXI_P_HSI0_PCH,
	PCH_CON_LHS_ACEL_D_HSI0_PCH,
	PCH_CON_LHS_AXI_D_HSI0AOC_PCH,
	QCH_CON_DP_LINK_QCH_GTC_CLK,
	QCH_CON_DP_LINK_QCH_PCLK,
	QCH_CON_D_TZPC_HSI0_QCH,
	QCH_CON_ETR_MIU_QCH_ACLK,
	QCH_CON_ETR_MIU_QCH_PCLK,
	QCH_CON_GPC_HSI0_QCH,
	QCH_CON_HSI0_CMU_HSI0_QCH,
	QCH_CON_LHM_AXI_G_ETR_HSI0_QCH,
	QCH_CON_LHM_AXI_P_AOCHSI0_QCH,
	QCH_CON_LHM_AXI_P_HSI0_QCH,
	QCH_CON_LHS_ACEL_D_HSI0_QCH,
	QCH_CON_LHS_AXI_D_HSI0AOC_QCH,
	QCH_CON_PPMU_HSI0_AOC_QCH,
	QCH_CON_PPMU_HSI0_BUS0_QCH,
	QCH_CON_SSMT_USB_QCH,
	QCH_CON_SYSMMU_USB_QCH,
	QCH_CON_SYSREG_HSI0_QCH,
	QCH_CON_UASC_HSI0_CTRL_QCH,
	QCH_CON_UASC_HSI0_LINK_QCH,
	QCH_CON_USB31DRD_QCH_APB,
	QCH_CON_USB31DRD_QCH_DBG,
	QCH_CON_USB31DRD_QCH_PCS,
	QCH_CON_USB31DRD_QCH_SLV_CTRL,
	QCH_CON_USB31DRD_QCH_SLV_LINK,
	QUEUE_CTRL_REG_BLK_HSI0_CMU_HSI0,
};

/* List of parent clocks for Muxes in CMU_HSI0 */
PNAME(mout_pll_usb_p)			= { "oscclk", "fout_usb_pll" };
PNAME(mout_hsi0_alt_user_p)		= { "oscclk",
					    "gout_hsi0_clk_hsi0_alt" };
PNAME(mout_hsi0_bus_user_p)		= { "oscclk", "dout_cmu_hsi0_bus" };
PNAME(mout_hsi0_dpgtc_user_p)		= { "oscclk", "dout_cmu_hsi0_dpgtc" };
PNAME(mout_hsi0_tcxo_user_p)		= { "oscclk", "tcxo_hsi1_hsi0" };
PNAME(mout_hsi0_usb20_user_p)		= { "oscclk", "usb20phy_phy_clock" };
PNAME(mout_hsi0_usb31drd_user_p)	= { "oscclk",
					    "dout_cmu_hsi0_usb31drd" };
PNAME(mout_hsi0_usbdpdbg_user_p)	= { "oscclk",
					    "dout_cmu_hsi0_usbdpdbg" };
PNAME(mout_hsi0_bus_p)			= { "mout_hsi0_bus_user",
					    "mout_hsi0_alt_user" };
PNAME(mout_hsi0_usb20_ref_p)		= { "mout_pll_usb",
					    "mout_hsi0_tcxo_user" };
PNAME(mout_hsi0_usb31drd_p)		= { "fout_usb_pll",
					    "mout_hsi0_usb31drd_user",
					    "dout_hsi0_usb31drd",
					    "fout_usb_pll" };

static const struct samsung_pll_rate_table cmu_hsi0_usb_pll_rates[] __initconst = {
	PLL_35XX_RATE(24576000, 19200000, 150, 6, 5),
	{ /* sentinel */ }
};

static const struct samsung_pll_clock cmu_hsi0_pll_clks[] __initconst = {
	PLL(pll_0518x, CLK_FOUT_USB_PLL, "fout_usb_pll", "oscclk",
	    PLL_LOCKTIME_PLL_USB, PLL_CON3_PLL_USB,
	    cmu_hsi0_usb_pll_rates),
};

static const struct samsung_mux_clock hsi0_mux_clks[] __initconst = {
	MUX(CLK_MOUT_PLL_USB,
	    "mout_pll_usb", mout_pll_usb_p,
	    PLL_CON0_PLL_USB, 4, 1),
	MUX(CLK_MOUT_HSI0_ALT_USER,
	    "mout_hsi0_alt_user", mout_hsi0_alt_user_p,
	    PLL_CON0_MUX_CLKCMU_HSI0_ALT_USER, 4, 1),
	MUX(CLK_MOUT_HSI0_BUS_USER,
	    "mout_hsi0_bus_user", mout_hsi0_bus_user_p,
	    PLL_CON0_MUX_CLKCMU_HSI0_BUS_USER, 4, 1),
	MUX(CLK_MOUT_HSI0_DPGTC_USER,
	    "mout_hsi0_dpgtc_user", mout_hsi0_dpgtc_user_p,
	    PLL_CON0_MUX_CLKCMU_HSI0_DPGTC_USER, 4, 1),
	MUX(CLK_MOUT_HSI0_TCXO_USER,
	    "mout_hsi0_tcxo_user", mout_hsi0_tcxo_user_p,
	    PLL_CON0_MUX_CLKCMU_HSI0_TCXO_USER, 4, 1),
	MUX(CLK_MOUT_HSI0_USB20_USER,
	    "mout_hsi0_usb20_user", mout_hsi0_usb20_user_p,
	    PLL_CON0_MUX_CLKCMU_HSI0_USB20_USER, 4, 1),
	MUX(CLK_MOUT_HSI0_USB31DRD_USER,
	    "mout_hsi0_usb31drd_user", mout_hsi0_usb31drd_user_p,
	    PLL_CON0_MUX_CLKCMU_HSI0_USB31DRD_USER, 4, 1),
	MUX(CLK_MOUT_HSI0_USBDPDBG_USER,
	    "mout_hsi0_usbdpdbg_user", mout_hsi0_usbdpdbg_user_p,
	    PLL_CON0_MUX_CLKCMU_HSI0_USPDPDBG_USER, 4, 1),
	MUX(CLK_MOUT_HSI0_BUS,
	    "mout_hsi0_bus", mout_hsi0_bus_p,
	    CLK_CON_MUX_MUX_CLK_HSI0_BUS, 0, 1),
	MUX(CLK_MOUT_HSI0_USB20_REF,
	    "mout_hsi0_usb20_ref", mout_hsi0_usb20_ref_p,
	    CLK_CON_MUX_MUX_CLK_HSI0_USB20_REF, 0, 1),
	MUX(CLK_MOUT_HSI0_USB31DRD,
	    "mout_hsi0_usb31drd", mout_hsi0_usb31drd_p,
	    CLK_CON_MUX_MUX_CLK_HSI0_USB31DRD, 0, 2),
};

static const struct samsung_div_clock hsi0_div_clks[] __initconst = {
	DIV(CLK_DOUT_HSI0_USB31DRD,
	    "dout_hsi0_usb31drd", "mout_hsi0_usb20_user",
	    CLK_CON_DIV_DIV_CLK_HSI0_USB31DRD, 0, 3),
};

static const struct samsung_gate_clock hsi0_gate_clks[] __initconst = {
	/* TODO: should have a driver for this */
	GATE(CLK_GOUT_HSI0_PCLK,
	     "gout_hsi0_hsi0_pclk", "mout_hsi0_bus",
	     CLK_CON_GAT_CLK_BLK_HSI0_UID_HSI0_CMU_HSI0_IPCLKPORT_PCLK,
	     21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_HSI0_USB31DRD_I_USB31DRD_SUSPEND_CLK_26,
	     "gout_hsi0_usb31drd_i_usb31drd_suspend_clk_26",
	     "mout_hsi0_usb20_ref",
	     CLK_CON_GAT_CLK_BLK_HSI0_UID_USB31DRD_IPCLKPORT_I_USB31DRD_SUSPEND_CLK_26,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_CLK_HSI0_ALT,
	     "gout_hsi0_clk_hsi0_alt", "ioclk_clk_hsi0_alt",
	     CLK_CON_GAT_CLK_HSI0_ALT, 21, 0, 0),
	GATE(CLK_GOUT_HSI0_DP_LINK_I_DP_GTC_CLK,
	     "gout_hsi0_dp_link_i_dp_gtc_clk", "mout_hsi0_dpgtc_user",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_DP_LINK_IPCLKPORT_I_DP_GTC_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_DP_LINK_I_PCLK,
	     "gout_hsi0_dp_link_i_pclk", "mout_hsi0_bus",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_DP_LINK_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_HSI0_D_TZPC_HSI0_PCLK,
	     "gout_hsi0_d_tzpc_hsi0_pclk", "mout_hsi0_bus",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_D_TZPC_HSI0_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_ETR_MIU_I_ACLK,
	     "gout_hsi0_etr_miu_i_aclk", "mout_hsi0_bus",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_ETR_MIU_IPCLKPORT_I_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_HSI0_ETR_MIU_I_PCLK,
	     "gout_hsi0_etr_miu_i_pclk", "mout_hsi0_bus",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_ETR_MIU_IPCLKPORT_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_HSI0_GPC_HSI0_PCLK,
	     "gout_hsi0_gpc_hsi0_pclk", "mout_hsi0_bus",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_GPC_HSI0_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_HSI0_LHM_AXI_G_ETR_HSI0_I_CLK,
	     "gout_hsi0_lhm_axi_g_etr_hsi0_i_clk", "mout_hsi0_bus",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_LHM_AXI_G_ETR_HSI0_IPCLKPORT_I_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_LHM_AXI_P_AOCHSI0_I_CLK,
	     "gout_hsi0_lhm_axi_p_aochsi0_i_clk", "mout_hsi0_bus",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_LHM_AXI_P_AOCHSI0_IPCLKPORT_I_CLK,
	     21, 0, 0),
	/* TODO: should have a driver for this */
	GATE(CLK_GOUT_HSI0_LHM_AXI_P_HSI0_I_CLK,
	     "gout_hsi0_lhm_axi_p_hsi0_i_clk", "mout_hsi0_bus",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_LHM_AXI_P_HSI0_IPCLKPORT_I_CLK,
	     21, CLK_IGNORE_UNUSED, 0),
	/* TODO: should have a driver for this */
	GATE(CLK_GOUT_HSI0_LHS_ACEL_D_HSI0_I_CLK,
	     "gout_hsi0_lhs_acel_d_hsi0_i_clk", "mout_hsi0_bus",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_LHS_ACEL_D_HSI0_IPCLKPORT_I_CLK,
	     21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_HSI0_LHS_AXI_D_HSI0AOC_I_CLK,
	     "gout_hsi0_lhs_axi_d_hsi0aoc_i_clk", "mout_hsi0_bus",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_LHS_AXI_D_HSI0AOC_IPCLKPORT_I_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_PPMU_HSI0_AOC_ACLK,
	     "gout_hsi0_ppmu_hsi0_aoc_aclk", "mout_hsi0_bus",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_PPMU_HSI0_AOC_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_PPMU_HSI0_AOC_PCLK,
	     "gout_hsi0_ppmu_hsi0_aoc_pclk", "mout_hsi0_bus",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_PPMU_HSI0_AOC_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_PPMU_HSI0_BUS0_ACLK,
	     "gout_hsi0_ppmu_hsi0_bus0_aclk", "mout_hsi0_bus",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_PPMU_HSI0_BUS0_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_PPMU_HSI0_BUS0_PCLK,
	     "gout_hsi0_ppmu_hsi0_bus0_pclk", "mout_hsi0_bus",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_PPMU_HSI0_BUS0_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_CLK_HSI0_BUS_CLK,
	     "gout_hsi0_clk_hsi0_bus_clk", "mout_hsi0_bus",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_RSTNSYNC_CLK_HSI0_BUS_IPCLKPORT_CLK,
	     21, 0, 0),
	/* TODO: should have a driver for this */
	GATE(CLK_GOUT_HSI0_SSMT_USB_ACLK,
	     "gout_hsi0_ssmt_usb_aclk", "mout_hsi0_bus",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_SSMT_USB_IPCLKPORT_ACLK,
	     21, CLK_IGNORE_UNUSED, 0),
	/* TODO: should have a driver for this */
	GATE(CLK_GOUT_HSI0_SSMT_USB_PCLK,
	     "gout_hsi0_ssmt_usb_pclk", "mout_hsi0_bus",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_SSMT_USB_IPCLKPORT_PCLK,
	     21, CLK_IGNORE_UNUSED, 0),
	/* TODO: should have a driver for this */
	GATE(CLK_GOUT_HSI0_SYSMMU_USB_CLK_S2,
	     "gout_hsi0_sysmmu_usb_clk_s2", "mout_hsi0_bus",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_SYSMMU_USB_IPCLKPORT_CLK_S2,
	     21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_HSI0_SYSREG_HSI0_PCLK,
	     "gout_hsi0_sysreg_hsi0_pclk", "mout_hsi0_bus",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_SYSREG_HSI0_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_UASC_HSI0_CTRL_ACLK,
	     "gout_hsi0_uasc_hsi0_ctrl_aclk", "mout_hsi0_bus",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_UASC_HSI0_CTRL_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_UASC_HSI0_CTRL_PCLK,
	     "gout_hsi0_uasc_hsi0_ctrl_pclk", "mout_hsi0_bus",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_UASC_HSI0_CTRL_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_UASC_HSI0_LINK_ACLK,
	     "gout_hsi0_uasc_hsi0_link_aclk", "mout_hsi0_bus",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_UASC_HSI0_LINK_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_UASC_HSI0_LINK_PCLK,
	     "gout_hsi0_uasc_hsi0_link_pclk", "mout_hsi0_bus",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_UASC_HSI0_LINK_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_USB31DRD_ACLK_PHYCTRL,
	     "gout_hsi0_usb31drd_aclk_phyctrl", "mout_hsi0_bus",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_ACLK_PHYCTRL,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_USB31DRD_BUS_CLK_EARLY,
	     "gout_hsi0_usb31drd_bus_clk_early", "mout_hsi0_bus",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_BUS_CLK_EARLY,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_USB31DRD_I_USB20_PHY_REFCLK_26,
	     "gout_hsi0_usb31drd_i_usb20_phy_refclk_26", "mout_hsi0_usb20_ref",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_I_USB20_PHY_REFCLK_26,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_USB31DRD_I_USB31DRD_REF_CLK_40,
	     "gout_hsi0_usb31drd_i_usb31drd_ref_clk_40", "mout_hsi0_usb31drd",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_I_USB31DRD_REF_CLK_40,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_USB31DRD_I_USBDPPHY_REF_SOC_PLL,
	     "gout_hsi0_usb31drd_i_usbdpphy_ref_soc_pll",
	     "mout_hsi0_usbdpdbg_user",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_I_USBDPPHY_REF_SOC_PLL,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_USB31DRD_I_USBDPPHY_SCL_APB_PCLK,
	     "gout_hsi0_usb31drd_i_usbdpphy_scl_apb_pclk", "mout_hsi0_bus",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_I_USBDPPHY_SCL_APB_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_USB31DRD_I_USBPCS_APB_CLK,
	     "gout_hsi0_usb31drd_i_usbpcs_apb_clk", "mout_hsi0_bus",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_I_USBPCS_APB_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_USB31DRD_USBDPPHY_I_ACLK,
	     "gout_hsi0_usb31drd_usbdpphy_i_aclk", "mout_hsi0_bus",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_USBDPPHY_I_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI0_USB31DRD_USBDPPHY_UDBG_I_APB_PCLK,
	     "gout_hsi0_usb31drd_usbdpphy_udbg_i_apb_pclk", "mout_hsi0_bus",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_USB31DRD_IPCLKPORT_USBDPPHY_UDBG_I_APB_PCLK,
	     21, 0, 0),
	/* TODO: should have a driver for this */
	GATE(CLK_GOUT_HSI0_XIU_D0_HSI0_ACLK,
	     "gout_hsi0_xiu_d0_hsi0_aclk", "mout_hsi0_bus",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_XIU_D0_HSI0_IPCLKPORT_ACLK,
	     21, CLK_IGNORE_UNUSED, 0),
	/* TODO: should have a driver for this */
	GATE(CLK_GOUT_HSI0_XIU_D1_HSI0_ACLK,
	     "gout_hsi0_xiu_d1_hsi0_aclk", "mout_hsi0_bus",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_XIU_D1_HSI0_IPCLKPORT_ACLK,
	     21, CLK_IGNORE_UNUSED, 0),
	/* TODO: should have a driver for this */
	GATE(CLK_GOUT_HSI0_XIU_P_HSI0_ACLK,
	     "gout_hsi0_xiu_p_hsi0_aclk", "mout_hsi0_bus",
	     CLK_CON_GAT_GOUT_BLK_HSI0_UID_XIU_P_HSI0_IPCLKPORT_ACLK,
	     21, CLK_IGNORE_UNUSED, 0),
};

static const struct samsung_fixed_rate_clock hsi0_fixed_clks[] __initconst = {
	FRATE(0, "tcxo_hsi1_hsi0", NULL, 0, 26000000),
	FRATE(0, "usb20phy_phy_clock", NULL, 0, 120000000),
	/* until we implement APMGSA */
	FRATE(0, "ioclk_clk_hsi0_alt", NULL, 0, 213000000),
};

static const struct samsung_cmu_info hsi0_cmu_info __initconst = {
	.pll_clks		= cmu_hsi0_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(cmu_hsi0_pll_clks),
	.mux_clks		= hsi0_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(hsi0_mux_clks),
	.div_clks		= hsi0_div_clks,
	.nr_div_clks		= ARRAY_SIZE(hsi0_div_clks),
	.gate_clks		= hsi0_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(hsi0_gate_clks),
	.fixed_clks		= hsi0_fixed_clks,
	.nr_fixed_clks		= ARRAY_SIZE(hsi0_fixed_clks),
	.nr_clk_ids		= CLKS_NR_HSI0,
	.clk_regs		= hsi0_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(hsi0_clk_regs),
	.clk_name		= "bus",
};

/* ---- CMU_HSI2 ------------------------------------------------------------ */

/* Register Offset definitions for CMU_HSI2 (0x14400000) */
#define PLL_CON0_MUX_CLKCMU_HSI2_BUS_USER												0x0600
#define PLL_CON1_MUX_CLKCMU_HSI2_BUS_USER												0x0604
#define PLL_CON0_MUX_CLKCMU_HSI2_MMC_CARD_USER												0x0610
#define PLL_CON1_MUX_CLKCMU_HSI2_MMC_CARD_USER												0x0614
#define PLL_CON0_MUX_CLKCMU_HSI2_PCIE_USER												0x0620
#define PLL_CON1_MUX_CLKCMU_HSI2_PCIE_USER												0x0624
#define PLL_CON0_MUX_CLKCMU_HSI2_UFS_EMBD_USER												0x0630
#define PLL_CON1_MUX_CLKCMU_HSI2_UFS_EMBD_USER												0x0634
#define HSI2_CMU_HSI2_CONTROLLER_OPTION													0x0800
#define CLKOUT_CON_BLK_HSI2_CMU_HSI2_CLKOUT0												0x0810
#define CLK_CON_GAT_CLK_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCIE_003_PCIE_SUB_CTRL_INST_0_PHY_REFCLK_IN					0x2000
#define CLK_CON_GAT_CLK_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCIE_004_PCIE_SUB_CTRL_INST_0_PHY_REFCLK_IN					0x2004
#define CLK_CON_GAT_CLK_BLK_HSI2_UID_SSMT_PCIE_IA_GEN4A_1_IPCLKPORT_ACLK								0x2008
#define CLK_CON_GAT_CLK_BLK_HSI2_UID_SSMT_PCIE_IA_GEN4A_1_IPCLKPORT_PCLK								0x200c
#define CLK_CON_GAT_CLK_BLK_HSI2_UID_SSMT_PCIE_IA_GEN4B_1_IPCLKPORT_ACLK								0x2010
#define CLK_CON_GAT_CLK_BLK_HSI2_UID_SSMT_PCIE_IA_GEN4B_1_IPCLKPORT_PCLK								0x2014
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_D_TZPC_HSI2_IPCLKPORT_PCLK									0x201c
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_GPC_HSI2_IPCLKPORT_PCLK										0x2020
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_GPIO_HSI2_IPCLKPORT_PCLK										0x2024
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_HSI2_CMU_HSI2_IPCLKPORT_PCLK									0x2028
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_LHM_AXI_P_HSI2_IPCLKPORT_I_CLK									0x202c
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_LHS_ACEL_D_HSI2_IPCLKPORT_I_CLK									0x2030
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_MMC_CARD_IPCLKPORT_I_ACLK										0x2034
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_MMC_CARD_IPCLKPORT_SDCLKIN									0x2038
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCIE_003_G4X2_DWC_PCIE_CTL_INST_0_DBI_ACLK_UG				0x203c
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCIE_003_G4X2_DWC_PCIE_CTL_INST_0_MSTR_ACLK_UG				0x2040
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCIE_003_G4X2_DWC_PCIE_CTL_INST_0_SLV_ACLK_UG				0x2044
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCIE_003_PCIE_SUB_CTRL_INST_0_I_DRIVER_APB_CLK				0x2048
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCIE_004_G4X1_DWC_PCIE_CTL_INST_0_DBI_ACLK_UG				0x204c
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCIE_004_G4X1_DWC_PCIE_CTL_INST_0_MSTR_ACLK_UG				0x2050
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCIE_004_G4X1_DWC_PCIE_CTL_INST_0_SLV_ACLK_UG				0x2054
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCIE_004_PCIE_SUB_CTRL_INST_0_I_DRIVER_APB_CLK				0x2058
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCS_PMA_INST_0_PHY_UDBG_I_APB_PCLK						0x205c
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCS_PMA_INST_0_PIPE_PAL_PCIE_INST_0_I_APB_PCLK				0x2060
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCS_PMA_INST_0_SF_PCIEPHY210X2_LN05LPE_QCH_TM_WRAPPER_INST_0_I_APB_PCLK	0x2064
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_IA_GEN4A_1_IPCLKPORT_I_CLK									0x2068
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_IA_GEN4B_1_IPCLKPORT_I_CLK									0x206c
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_PPMU_HSI2_IPCLKPORT_ACLK										0x2070
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_PPMU_HSI2_IPCLKPORT_PCLK										0x2074
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_QE_MMC_CARD_HSI2_IPCLKPORT_ACLK									0x2078
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_QE_MMC_CARD_HSI2_IPCLKPORT_PCLK									0x207c
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_QE_PCIE_GEN4A_HSI2_IPCLKPORT_ACLK									0x2080
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_QE_PCIE_GEN4A_HSI2_IPCLKPORT_PCLK									0x2084
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_QE_PCIE_GEN4B_HSI2_IPCLKPORT_ACLK									0x2088
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_QE_PCIE_GEN4B_HSI2_IPCLKPORT_PCLK									0x208c
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_QE_UFS_EMBD_HSI2_IPCLKPORT_ACLK									0x2090
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_QE_UFS_EMBD_HSI2_IPCLKPORT_PCLK									0x2094
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_RSTNSYNC_CLK_HSI2_BUS_IPCLKPORT_CLK								0x2098
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_RSTNSYNC_CLK_HSI2_OSCCLK_IPCLKPORT_CLK								0x209c
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_SSMT_HSI2_IPCLKPORT_ACLK										0x20a0
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_SSMT_HSI2_IPCLKPORT_PCLK										0x20a4
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_SYSMMU_HSI2_IPCLKPORT_CLK_S2									0x20a8
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_SYSREG_HSI2_IPCLKPORT_PCLK									0x20ac
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_UASC_PCIE_GEN4A_DBI_1_IPCLKPORT_ACLK								0x20b0
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_UASC_PCIE_GEN4A_DBI_1_IPCLKPORT_PCLK								0x20b4
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_UASC_PCIE_GEN4A_SLV_1_IPCLKPORT_ACLK								0x20b8
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_UASC_PCIE_GEN4A_SLV_1_IPCLKPORT_PCLK								0x20bc
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_UASC_PCIE_GEN4B_DBI_1_IPCLKPORT_ACLK								0x20c0
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_UASC_PCIE_GEN4B_DBI_1_IPCLKPORT_PCLK								0x20c4
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_UASC_PCIE_GEN4B_SLV_1_IPCLKPORT_ACLK								0x20c8
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_UASC_PCIE_GEN4B_SLV_1_IPCLKPORT_PCLK								0x20cc
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_UFS_EMBD_IPCLKPORT_I_ACLK										0x20d0
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_UFS_EMBD_IPCLKPORT_I_CLK_UNIPRO									0x20d4
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_UFS_EMBD_IPCLKPORT_I_FMP_CLK									0x20d8
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_XIU_D_HSI2_IPCLKPORT_ACLK										0x20dc
#define CLK_CON_GAT_GOUT_BLK_HSI2_UID_XIU_P_HSI2_IPCLKPORT_ACLK										0x20e0
#define DMYQCH_CON_PCIE_GEN4_1_QCH_SCLK_1												0x3000
#define PCH_CON_LHM_AXI_P_HSI2_PCH													0x3008
#define PCH_CON_LHS_ACEL_D_HSI2_PCH													0x300c
#define QCH_CON_D_TZPC_HSI2_QCH														0x3010
#define QCH_CON_GPC_HSI2_QCH														0x3014
#define QCH_CON_GPIO_HSI2_QCH														0x3018
#define QCH_CON_HSI2_CMU_HSI2_QCH													0x301c
#define QCH_CON_LHM_AXI_P_HSI2_QCH													0x3020
#define QCH_CON_LHS_ACEL_D_HSI2_QCH													0x3024
#define QCH_CON_MMC_CARD_QCH														0x3028
#define QCH_CON_PCIE_GEN4_1_QCH_APB_1													0x302c
#define QCH_CON_PCIE_GEN4_1_QCH_APB_2													0x3030
#define QCH_CON_PCIE_GEN4_1_QCH_AXI_1													0x3034
#define QCH_CON_PCIE_GEN4_1_QCH_AXI_2													0x3038
#define QCH_CON_PCIE_GEN4_1_QCH_DBG_1													0x303c
#define QCH_CON_PCIE_GEN4_1_QCH_DBG_2													0x3040
#define QCH_CON_PCIE_GEN4_1_QCH_PCS_APB													0x3044
#define QCH_CON_PCIE_GEN4_1_QCH_PMA_APB													0x3048
#define QCH_CON_PCIE_GEN4_1_QCH_UDBG													0x304c
#define QCH_CON_PCIE_IA_GEN4A_1_QCH													0x3050
#define QCH_CON_PCIE_IA_GEN4B_1_QCH													0x3054
#define QCH_CON_PPMU_HSI2_QCH														0x3058
#define QCH_CON_QE_MMC_CARD_HSI2_QCH													0x305c
#define QCH_CON_QE_PCIE_GEN4A_HSI2_QCH													0x3060
#define QCH_CON_QE_PCIE_GEN4B_HSI2_QCH													0x3064
#define QCH_CON_QE_UFS_EMBD_HSI2_QCH													0x3068
#define QCH_CON_SSMT_HSI2_QCH														0x306c
#define QCH_CON_SSMT_PCIE_IA_GEN4A_1_QCH												0x3070
#define QCH_CON_SSMT_PCIE_IA_GEN4B_1_QCH												0x3074
#define QCH_CON_SYSMMU_HSI2_QCH														0x3078
#define QCH_CON_SYSREG_HSI2_QCH														0x307c
#define QCH_CON_UASC_PCIE_GEN4A_DBI_1_QCH												0x3080
#define QCH_CON_UASC_PCIE_GEN4A_SLV_1_QCH												0x3084
#define QCH_CON_UASC_PCIE_GEN4B_DBI_1_QCH												0x3088
#define QCH_CON_UASC_PCIE_GEN4B_SLV_1_QCH												0x308c
#define QCH_CON_UFS_EMBD_QCH														0x3090
#define QCH_CON_UFS_EMBD_QCH_FMP													0x3094
#define QUEUE_CTRL_REG_BLK_HSI2_CMU_HSI2												0x3c00

static const unsigned long cmu_hsi2_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_HSI2_BUS_USER,
	PLL_CON1_MUX_CLKCMU_HSI2_BUS_USER,
	PLL_CON0_MUX_CLKCMU_HSI2_MMC_CARD_USER,
	PLL_CON1_MUX_CLKCMU_HSI2_MMC_CARD_USER,
	PLL_CON0_MUX_CLKCMU_HSI2_PCIE_USER,
	PLL_CON1_MUX_CLKCMU_HSI2_PCIE_USER,
	PLL_CON0_MUX_CLKCMU_HSI2_UFS_EMBD_USER,
	PLL_CON1_MUX_CLKCMU_HSI2_UFS_EMBD_USER,
	HSI2_CMU_HSI2_CONTROLLER_OPTION,
	CLKOUT_CON_BLK_HSI2_CMU_HSI2_CLKOUT0,
	CLK_CON_GAT_CLK_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCIE_003_PCIE_SUB_CTRL_INST_0_PHY_REFCLK_IN,
	CLK_CON_GAT_CLK_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCIE_004_PCIE_SUB_CTRL_INST_0_PHY_REFCLK_IN,
	CLK_CON_GAT_CLK_BLK_HSI2_UID_SSMT_PCIE_IA_GEN4A_1_IPCLKPORT_ACLK,
	CLK_CON_GAT_CLK_BLK_HSI2_UID_SSMT_PCIE_IA_GEN4A_1_IPCLKPORT_PCLK,
	CLK_CON_GAT_CLK_BLK_HSI2_UID_SSMT_PCIE_IA_GEN4B_1_IPCLKPORT_ACLK,
	CLK_CON_GAT_CLK_BLK_HSI2_UID_SSMT_PCIE_IA_GEN4B_1_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_D_TZPC_HSI2_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_GPC_HSI2_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_GPIO_HSI2_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_HSI2_CMU_HSI2_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_LHM_AXI_P_HSI2_IPCLKPORT_I_CLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_LHS_ACEL_D_HSI2_IPCLKPORT_I_CLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_MMC_CARD_IPCLKPORT_I_ACLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_MMC_CARD_IPCLKPORT_SDCLKIN,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCIE_003_G4X2_DWC_PCIE_CTL_INST_0_DBI_ACLK_UG,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCIE_003_G4X2_DWC_PCIE_CTL_INST_0_MSTR_ACLK_UG,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCIE_003_G4X2_DWC_PCIE_CTL_INST_0_SLV_ACLK_UG,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCIE_003_PCIE_SUB_CTRL_INST_0_I_DRIVER_APB_CLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCIE_004_G4X1_DWC_PCIE_CTL_INST_0_DBI_ACLK_UG,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCIE_004_G4X1_DWC_PCIE_CTL_INST_0_MSTR_ACLK_UG,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCIE_004_G4X1_DWC_PCIE_CTL_INST_0_SLV_ACLK_UG,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCIE_004_PCIE_SUB_CTRL_INST_0_I_DRIVER_APB_CLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCS_PMA_INST_0_PHY_UDBG_I_APB_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCS_PMA_INST_0_PIPE_PAL_PCIE_INST_0_I_APB_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCS_PMA_INST_0_SF_PCIEPHY210X2_LN05LPE_QCH_TM_WRAPPER_INST_0_I_APB_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_IA_GEN4A_1_IPCLKPORT_I_CLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_IA_GEN4B_1_IPCLKPORT_I_CLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_PPMU_HSI2_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_PPMU_HSI2_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_QE_MMC_CARD_HSI2_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_QE_MMC_CARD_HSI2_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_QE_PCIE_GEN4A_HSI2_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_QE_PCIE_GEN4A_HSI2_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_QE_PCIE_GEN4B_HSI2_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_QE_PCIE_GEN4B_HSI2_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_QE_UFS_EMBD_HSI2_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_QE_UFS_EMBD_HSI2_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_RSTNSYNC_CLK_HSI2_BUS_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_RSTNSYNC_CLK_HSI2_OSCCLK_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_SSMT_HSI2_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_SSMT_HSI2_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_SYSMMU_HSI2_IPCLKPORT_CLK_S2,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_SYSREG_HSI2_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_UASC_PCIE_GEN4A_DBI_1_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_UASC_PCIE_GEN4A_DBI_1_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_UASC_PCIE_GEN4A_SLV_1_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_UASC_PCIE_GEN4A_SLV_1_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_UASC_PCIE_GEN4B_DBI_1_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_UASC_PCIE_GEN4B_DBI_1_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_UASC_PCIE_GEN4B_SLV_1_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_UASC_PCIE_GEN4B_SLV_1_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_UFS_EMBD_IPCLKPORT_I_ACLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_UFS_EMBD_IPCLKPORT_I_CLK_UNIPRO,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_UFS_EMBD_IPCLKPORT_I_FMP_CLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_XIU_D_HSI2_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_HSI2_UID_XIU_P_HSI2_IPCLKPORT_ACLK,
	DMYQCH_CON_PCIE_GEN4_1_QCH_SCLK_1,
	PCH_CON_LHM_AXI_P_HSI2_PCH,
	PCH_CON_LHS_ACEL_D_HSI2_PCH,
	QCH_CON_D_TZPC_HSI2_QCH,
	QCH_CON_GPC_HSI2_QCH,
	QCH_CON_GPIO_HSI2_QCH,
	QCH_CON_HSI2_CMU_HSI2_QCH,
	QCH_CON_LHM_AXI_P_HSI2_QCH,
	QCH_CON_LHS_ACEL_D_HSI2_QCH,
	QCH_CON_MMC_CARD_QCH,
	QCH_CON_PCIE_GEN4_1_QCH_APB_1,
	QCH_CON_PCIE_GEN4_1_QCH_APB_2,
	QCH_CON_PCIE_GEN4_1_QCH_AXI_1,
	QCH_CON_PCIE_GEN4_1_QCH_AXI_2,
	QCH_CON_PCIE_GEN4_1_QCH_DBG_1,
	QCH_CON_PCIE_GEN4_1_QCH_DBG_2,
	QCH_CON_PCIE_GEN4_1_QCH_PCS_APB,
	QCH_CON_PCIE_GEN4_1_QCH_PMA_APB,
	QCH_CON_PCIE_GEN4_1_QCH_UDBG,
	QCH_CON_PCIE_IA_GEN4A_1_QCH,
	QCH_CON_PCIE_IA_GEN4B_1_QCH,
	QCH_CON_PPMU_HSI2_QCH,
	QCH_CON_QE_MMC_CARD_HSI2_QCH,
	QCH_CON_QE_PCIE_GEN4A_HSI2_QCH,
	QCH_CON_QE_PCIE_GEN4B_HSI2_QCH,
	QCH_CON_QE_UFS_EMBD_HSI2_QCH,
	QCH_CON_SSMT_HSI2_QCH,
	QCH_CON_SSMT_PCIE_IA_GEN4A_1_QCH,
	QCH_CON_SSMT_PCIE_IA_GEN4B_1_QCH,
	QCH_CON_SYSMMU_HSI2_QCH,
	QCH_CON_SYSREG_HSI2_QCH,
	QCH_CON_UASC_PCIE_GEN4A_DBI_1_QCH,
	QCH_CON_UASC_PCIE_GEN4A_SLV_1_QCH,
	QCH_CON_UASC_PCIE_GEN4B_DBI_1_QCH,
	QCH_CON_UASC_PCIE_GEN4B_SLV_1_QCH,
	QCH_CON_UFS_EMBD_QCH,
	QCH_CON_UFS_EMBD_QCH_FMP,
	QUEUE_CTRL_REG_BLK_HSI2_CMU_HSI2,
};

PNAME(mout_hsi2_bus_user_p)	= { "oscclk", "dout_cmu_hsi2_bus" };
PNAME(mout_hsi2_mmc_card_user_p) = { "oscclk", "dout_cmu_hsi2_mmc_card" };
PNAME(mout_hsi2_pcie_user_p)	= { "oscclk", "dout_cmu_hsi2_pcie" };
PNAME(mout_hsi2_ufs_embd_user_p) = { "oscclk", "dout_cmu_hsi2_ufs_embd" };

static const struct samsung_mux_clock hsi2_mux_clks[] __initconst = {
	MUX(CLK_MOUT_HSI2_BUS_USER, "mout_hsi2_bus_user", mout_hsi2_bus_user_p,
	    PLL_CON0_MUX_CLKCMU_HSI2_BUS_USER, 4, 1),
	MUX(CLK_MOUT_HSI2_MMC_CARD_USER, "mout_hsi2_mmc_card_user",
	    mout_hsi2_mmc_card_user_p, PLL_CON0_MUX_CLKCMU_HSI2_MMC_CARD_USER,
	    4, 1),
	MUX(CLK_MOUT_HSI2_PCIE_USER, "mout_hsi2_pcie_user",
	    mout_hsi2_pcie_user_p, PLL_CON0_MUX_CLKCMU_HSI2_PCIE_USER,
	    4, 1),
	MUX(CLK_MOUT_HSI2_UFS_EMBD_USER, "mout_hsi2_ufs_embd_user",
	    mout_hsi2_ufs_embd_user_p, PLL_CON0_MUX_CLKCMU_HSI2_UFS_EMBD_USER,
	    4, 1),
};

static const struct samsung_gate_clock hsi2_gate_clks[] __initconst = {
	GATE(CLK_GOUT_HSI2_PCIE_GEN4_1_PCIE_003_PHY_REFCLK_IN,
	     "gout_hsi2_pcie_gen4_1_pcie_003_phy_refclk_in",
	     "mout_hsi2_pcie_user",
	     CLK_CON_GAT_CLK_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCIE_003_PCIE_SUB_CTRL_INST_0_PHY_REFCLK_IN,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_PCIE_GEN4_1_PCIE_004_PHY_REFCLK_IN,
	     "gout_hsi2_pcie_gen4_1_pcie_004_phy_refclk_in",
	     "mout_hsi2_pcie_user",
	     CLK_CON_GAT_CLK_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCIE_004_PCIE_SUB_CTRL_INST_0_PHY_REFCLK_IN,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_SSMT_PCIE_IA_GEN4A_1_ACLK,
	     "gout_hsi2_ssmt_pcie_ia_gen4a_1_aclk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_CLK_BLK_HSI2_UID_SSMT_PCIE_IA_GEN4A_1_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_SSMT_PCIE_IA_GEN4A_1_PCLK,
	     "gout_hsi2_ssmt_pcie_ia_gen4a_1_pclk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_CLK_BLK_HSI2_UID_SSMT_PCIE_IA_GEN4A_1_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_SSMT_PCIE_IA_GEN4B_1_ACLK,
	     "gout_hsi2_ssmt_pcie_ia_gen4b_1_aclk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_CLK_BLK_HSI2_UID_SSMT_PCIE_IA_GEN4B_1_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_SSMT_PCIE_IA_GEN4B_1_PCLK,
	     "gout_hsi2_ssmt_pcie_ia_gen4b_1_pclk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_CLK_BLK_HSI2_UID_SSMT_PCIE_IA_GEN4B_1_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_D_TZPC_HSI2_PCLK,
	     "gout_hsi2_d_tzpc_hsi2_pclk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_D_TZPC_HSI2_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_GPC_HSI2_PCLK,
	     "gout_hsi2_gpc_hsi2_pclk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_GPC_HSI2_IPCLKPORT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_HSI2_GPIO_HSI2_PCLK,
	     "gout_hsi2_gpio_hsi2_pclk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_GPIO_HSI2_IPCLKPORT_PCLK, 21,
	     CLK_IGNORE_UNUSED, 0),
	/* Disabling this clock makes the system hang. Mark the clock as critical. */
	GATE(CLK_GOUT_HSI2_HSI2_CMU_HSI2_PCLK,
	     "gout_hsi2_hsi2_cmu_hsi2_pclk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_HSI2_CMU_HSI2_IPCLKPORT_PCLK,
	     21, CLK_IS_CRITICAL, 0),
	/* Disabling this clock makes the system hang. Mark the clock as critical. */
	GATE(CLK_GOUT_HSI2_LHM_AXI_P_HSI2_I_CLK,
	     "gout_hsi2_lhm_axi_p_hsi2_i_clk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_LHM_AXI_P_HSI2_IPCLKPORT_I_CLK,
	     21, CLK_IS_CRITICAL, 0),
	/* TODO: should have a driver for this */
	GATE(CLK_GOUT_HSI2_LHS_ACEL_D_HSI2_I_CLK,
	     "gout_hsi2_lhs_acel_d_hsi2_i_clk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_LHS_ACEL_D_HSI2_IPCLKPORT_I_CLK,
	     21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_HSI2_MMC_CARD_I_ACLK,
	     "gout_hsi2_mmc_card_i_aclk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_MMC_CARD_IPCLKPORT_I_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_MMC_CARD_SDCLKIN,
	     "gout_hsi2_mmc_card_sdclkin", "mout_hsi2_mmc_card_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_MMC_CARD_IPCLKPORT_SDCLKIN,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_PCIE_GEN4_1_PCIE_003_DBI_ACLK_UG,
	     "gout_hsi2_pcie_gen4_1_pcie_003_dbi_aclk_ug", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCIE_003_G4X2_DWC_PCIE_CTL_INST_0_DBI_ACLK_UG,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_PCIE_GEN4_1_PCIE_003_MSTR_ACLK_UG,
	     "gout_hsi2_pcie_gen4_1_pcie_003_mstr_aclk_ug",
	     "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCIE_003_G4X2_DWC_PCIE_CTL_INST_0_MSTR_ACLK_UG,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_PCIE_GEN4_1_PCIE_003_SLV_ACLK_UG,
	     "gout_hsi2_pcie_gen4_1_pcie_003_slv_aclk_ug", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCIE_003_G4X2_DWC_PCIE_CTL_INST_0_SLV_ACLK_UG,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_PCIE_GEN4_1_PCIE_003_I_DRIVER_APB_CLK,
	     "gout_hsi2_pcie_gen4_1_pcie_003_i_driver_apb_clk",
	     "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCIE_003_PCIE_SUB_CTRL_INST_0_I_DRIVER_APB_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_PCIE_GEN4_1_PCIE_004_DBI_ACLK_UG,
	     "gout_hsi2_pcie_gen4_1_pcie_004_dbi_aclk_ug", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCIE_004_G4X1_DWC_PCIE_CTL_INST_0_DBI_ACLK_UG,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_PCIE_GEN4_1_PCIE_004_MSTR_ACLK_UG,
	     "gout_hsi2_pcie_gen4_1_pcie_004_mstr_aclk_ug",
	     "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCIE_004_G4X1_DWC_PCIE_CTL_INST_0_MSTR_ACLK_UG,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_PCIE_GEN4_1_PCIE_004_SLV_ACLK_UG,
	     "gout_hsi2_pcie_gen4_1_pcie_004_slv_aclk_ug", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCIE_004_G4X1_DWC_PCIE_CTL_INST_0_SLV_ACLK_UG,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_PCIE_GEN4_1_PCIE_004_I_DRIVER_APB_CLK,
	     "gout_hsi2_pcie_gen4_1_pcie_004_i_driver_apb_clk",
	     "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCIE_004_PCIE_SUB_CTRL_INST_0_I_DRIVER_APB_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_PCIE_GEN4_1_PCS_PMA_PHY_UDBG_I_APB_PCLK,
	     "gout_hsi2_pcie_gen4_1_pcs_pma_phy_udbg_i_apb_pclk",
	     "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCS_PMA_INST_0_PHY_UDBG_I_APB_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_PCIE_GEN4_1_PCS_PMA_PIPE_PAL_PCIE_I_APB_PCLK,
	     "gout_hsi2_pcie_gen4_1_pcs_pma_pipe_pal_pcie_i_apb_pclk",
	     "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCS_PMA_INST_0_PIPE_PAL_PCIE_INST_0_I_APB_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_PCIE_GEN4_1_PCS_PMA_PCIEPHY210X2_QCH_I_APB_PCLK,
	     "gout_hsi2_pcie_gen4_1_pcs_pma_pciephy210x2_qch_i_apb_pclk",
	     "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_GEN4_1_IPCLKPORT_PCS_PMA_INST_0_SF_PCIEPHY210X2_LN05LPE_QCH_TM_WRAPPER_INST_0_I_APB_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_PCIE_IA_GEN4A_1_I_CLK,
	     "gout_hsi2_pcie_ia_gen4a_1_i_clk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_IA_GEN4A_1_IPCLKPORT_I_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_PCIE_IA_GEN4B_1_I_CLK,
	     "gout_hsi2_pcie_ia_gen4b_1_i_clk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_PCIE_IA_GEN4B_1_IPCLKPORT_I_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_PPMU_HSI2_ACLK,
	     "gout_hsi2_ppmu_hsi2_aclk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_PPMU_HSI2_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_PPMU_HSI2_PCLK,
	     "gout_hsi2_ppmu_hsi2_pclk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_PPMU_HSI2_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_QE_MMC_CARD_HSI2_ACLK,
	     "gout_hsi2_qe_mmc_card_hsi2_aclk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_QE_MMC_CARD_HSI2_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_QE_MMC_CARD_HSI2_PCLK,
	     "gout_hsi2_qe_mmc_card_hsi2_pclk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_QE_MMC_CARD_HSI2_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_QE_PCIE_GEN4A_HSI2_ACLK,
	     "gout_hsi2_qe_pcie_gen4a_hsi2_aclk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_QE_PCIE_GEN4A_HSI2_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_QE_PCIE_GEN4A_HSI2_PCLK,
	     "gout_hsi2_qe_pcie_gen4a_hsi2_pclk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_QE_PCIE_GEN4A_HSI2_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_QE_PCIE_GEN4B_HSI2_ACLK,
	     "gout_hsi2_qe_pcie_gen4b_hsi2_aclk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_QE_PCIE_GEN4B_HSI2_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_QE_PCIE_GEN4B_HSI2_PCLK,
	     "gout_hsi2_qe_pcie_gen4b_hsi2_pclk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_QE_PCIE_GEN4B_HSI2_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_QE_UFS_EMBD_HSI2_ACLK,
	     "gout_hsi2_qe_ufs_embd_hsi2_aclk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_QE_UFS_EMBD_HSI2_IPCLKPORT_ACLK,
	     21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_HSI2_QE_UFS_EMBD_HSI2_PCLK,
	     "gout_hsi2_qe_ufs_embd_hsi2_pclk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_QE_UFS_EMBD_HSI2_IPCLKPORT_PCLK,
	     21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_HSI2_CLK_HSI2_BUS_CLK,
	     "gout_hsi2_clk_hsi2_bus_clk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_RSTNSYNC_CLK_HSI2_BUS_IPCLKPORT_CLK,
	     21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_HSI2_CLK_HSI2_OSCCLK_CLK,
	     "gout_hsi2_clk_hsi2_oscclk_clk", "oscclk",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_RSTNSYNC_CLK_HSI2_OSCCLK_IPCLKPORT_CLK,
	     21, 0, 0),
	/* TODO: should have a driver for this */
	GATE(CLK_GOUT_HSI2_SSMT_HSI2_ACLK,
	     "gout_hsi2_ssmt_hsi2_aclk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_SSMT_HSI2_IPCLKPORT_ACLK,
	     21, CLK_IGNORE_UNUSED, 0),
	/* TODO: should have a driver for this */
	GATE(CLK_GOUT_HSI2_SSMT_HSI2_PCLK,
	     "gout_hsi2_ssmt_hsi2_pclk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_SSMT_HSI2_IPCLKPORT_PCLK,
	     21, CLK_IGNORE_UNUSED, 0),
	/* TODO: should have a driver for this */
	GATE(CLK_GOUT_HSI2_SYSMMU_HSI2_CLK_S2,
	     "gout_hsi2_sysmmu_hsi2_clk_s2", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_SYSMMU_HSI2_IPCLKPORT_CLK_S2,
	     21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_HSI2_SYSREG_HSI2_PCLK,
	     "gout_hsi2_sysreg_hsi2_pclk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_SYSREG_HSI2_IPCLKPORT_PCLK,
	     21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_HSI2_UASC_PCIE_GEN4A_DBI_1_ACLK,
	     "gout_hsi2_uasc_pcie_gen4a_dbi_1_aclk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_UASC_PCIE_GEN4A_DBI_1_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_UASC_PCIE_GEN4A_DBI_1_PCLK,
	     "gout_hsi2_uasc_pcie_gen4a_dbi_1_pclk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_UASC_PCIE_GEN4A_DBI_1_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_UASC_PCIE_GEN4A_SLV_1_ACLK,
	     "gout_hsi2_uasc_pcie_gen4a_slv_1_aclk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_UASC_PCIE_GEN4A_SLV_1_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_UASC_PCIE_GEN4A_SLV_1_PCLK,
	     "gout_hsi2_uasc_pcie_gen4a_slv_1_pclk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_UASC_PCIE_GEN4A_SLV_1_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_UASC_PCIE_GEN4B_DBI_1_ACLK,
	     "gout_hsi2_uasc_pcie_gen4b_dbi_1_aclk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_UASC_PCIE_GEN4B_DBI_1_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_UASC_PCIE_GEN4B_DBI_1_PCLK,
	     "gout_hsi2_uasc_pcie_gen4b_dbi_1_pclk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_UASC_PCIE_GEN4B_DBI_1_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_UASC_PCIE_GEN4B_SLV_1_ACLK,
	     "gout_hsi2_uasc_pcie_gen4b_slv_1_aclk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_UASC_PCIE_GEN4B_SLV_1_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_UASC_PCIE_GEN4B_SLV_1_PCLK,
	     "gout_hsi2_uasc_pcie_gen4b_slv_1_pclk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_UASC_PCIE_GEN4B_SLV_1_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_HSI2_UFS_EMBD_I_ACLK,
	     "gout_hsi2_ufs_embd_i_aclk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_UFS_EMBD_IPCLKPORT_I_ACLK,
	     21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_HSI2_UFS_EMBD_I_CLK_UNIPRO,
	     "gout_hsi2_ufs_embd_i_clk_unipro", "mout_hsi2_ufs_embd_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_UFS_EMBD_IPCLKPORT_I_CLK_UNIPRO,
	     21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_HSI2_UFS_EMBD_I_FMP_CLK,
	     "gout_hsi2_ufs_embd_i_fmp_clk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_UFS_EMBD_IPCLKPORT_I_FMP_CLK,
	     21, CLK_IS_CRITICAL, 0),
	/* TODO: should have a driver for this */
	GATE(CLK_GOUT_HSI2_XIU_D_HSI2_ACLK,
	     "gout_hsi2_xiu_d_hsi2_aclk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_XIU_D_HSI2_IPCLKPORT_ACLK,
	     21, CLK_IGNORE_UNUSED, 0),
	/* TODO: should have a driver for this */
	GATE(CLK_GOUT_HSI2_XIU_P_HSI2_ACLK,
	     "gout_hsi2_xiu_p_hsi2_aclk", "mout_hsi2_bus_user",
	     CLK_CON_GAT_GOUT_BLK_HSI2_UID_XIU_P_HSI2_IPCLKPORT_ACLK,
	     21, CLK_IGNORE_UNUSED, 0),
};

static const struct samsung_cmu_info hsi2_cmu_info __initconst = {
	.mux_clks		= hsi2_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(hsi2_mux_clks),
	.gate_clks		= hsi2_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(hsi2_gate_clks),
	.nr_clk_ids		= CLKS_NR_HSI2,
	.clk_regs		= cmu_hsi2_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(cmu_hsi2_clk_regs),
	.clk_name		= "bus",
};

/* ---- CMU_MISC ------------------------------------------------------------ */

/* Register Offset definitions for CMU_MISC (0x10010000) */
#define PLL_CON0_MUX_CLKCMU_MISC_BUS_USER					0x0600
#define PLL_CON1_MUX_CLKCMU_MISC_BUS_USER					0x0604
#define PLL_CON0_MUX_CLKCMU_MISC_SSS_USER					0x0610
#define PLL_CON1_MUX_CLKCMU_MISC_SSS_USER					0x0614
#define MISC_CMU_MISC_CONTROLLER_OPTION						0x0800
#define CLKOUT_CON_BLK_MISC_CMU_MISC_CLKOUT0					0x0810
#define CLK_CON_MUX_MUX_CLK_MISC_GIC						0x1000
#define CLK_CON_DIV_DIV_CLK_MISC_BUSP						0x1800
#define CLK_CON_DIV_DIV_CLK_MISC_GIC						0x1804
#define CLK_CON_GAT_CLK_BLK_MISC_UID_MISC_CMU_MISC_IPCLKPORT_PCLK		0x2000
#define CLK_CON_GAT_CLK_BLK_MISC_UID_OTP_CON_BIRA_IPCLKPORT_I_OSCCLK		0x2004
#define CLK_CON_GAT_CLK_BLK_MISC_UID_OTP_CON_BISR_IPCLKPORT_I_OSCCLK		0x2008
#define CLK_CON_GAT_CLK_BLK_MISC_UID_OTP_CON_TOP_IPCLKPORT_I_OSCCLK		0x200c
#define CLK_CON_GAT_CLK_BLK_MISC_UID_RSTNSYNC_CLK_MISC_OSCCLK_IPCLKPORT_CLK	0x2010
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_ADM_AHB_SSS_IPCLKPORT_HCLKM		0x2014
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_AD_APB_DIT_IPCLKPORT_PCLKM		0x2018
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_AD_APB_PUF_IPCLKPORT_PCLKM		0x201c
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_DIT_IPCLKPORT_ICLKL2A			0x2020
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_D_TZPC_MISC_IPCLKPORT_PCLK		0x2024
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_GIC_IPCLKPORT_GICCLK			0x2028
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_GPC_MISC_IPCLKPORT_PCLK			0x202c
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_LHM_AST_ICC_CPUGIC_IPCLKPORT_I_CLK	0x2030
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_LHM_AXI_D_SSS_IPCLKPORT_I_CLK		0x2034
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_LHM_AXI_P_GIC_IPCLKPORT_I_CLK		0x2038
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_LHM_AXI_P_MISC_IPCLKPORT_I_CLK		0x203c
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_LHS_ACEL_D_MISC_IPCLKPORT_I_CLK		0x2040
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_LHS_AST_IRI_GICCPU_IPCLKPORT_I_CLK	0x2044
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_LHS_AXI_D_SSS_IPCLKPORT_I_CLK		0x2048
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_MCT_IPCLKPORT_PCLK			0x204c
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_OTP_CON_BIRA_IPCLKPORT_PCLK		0x2050
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_OTP_CON_BISR_IPCLKPORT_PCLK		0x2054
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_OTP_CON_TOP_IPCLKPORT_PCLK		0x2058
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_PDMA_IPCLKPORT_ACLK			0x205c
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_PPMU_DMA_IPCLKPORT_ACLK			0x2060
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_PPMU_MISC_IPCLKPORT_ACLK			0x2064
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_PPMU_MISC_IPCLKPORT_PCLK			0x2068
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_PUF_IPCLKPORT_I_CLK			0x206c
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_DIT_IPCLKPORT_ACLK			0x2070
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_DIT_IPCLKPORT_PCLK			0x2074
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_PDMA_IPCLKPORT_ACLK			0x2078
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_PDMA_IPCLKPORT_PCLK			0x207c
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_PPMU_DMA_IPCLKPORT_ACLK		0x2080
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_PPMU_DMA_IPCLKPORT_PCLK		0x2084
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_RTIC_IPCLKPORT_ACLK			0x2088
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_RTIC_IPCLKPORT_PCLK			0x208c
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_SPDMA_IPCLKPORT_ACLK			0x2090
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_SPDMA_IPCLKPORT_PCLK			0x2094
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_SSS_IPCLKPORT_ACLK			0x2098
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_SSS_IPCLKPORT_PCLK			0x209c
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_RSTNSYNC_CLK_MISC_BUSD_IPCLKPORT_CLK	0x20a0
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_RSTNSYNC_CLK_MISC_BUSP_IPCLKPORT_CLK	0x20a4
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_RSTNSYNC_CLK_MISC_GIC_IPCLKPORT_CLK	0x20a8
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_RSTNSYNC_CLK_MISC_SSS_IPCLKPORT_CLK	0x20ac
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_RTIC_IPCLKPORT_I_ACLK			0x20b0
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_RTIC_IPCLKPORT_I_PCLK			0x20b4
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_SPDMA_IPCLKPORT_ACLK			0x20b8
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_DIT_IPCLKPORT_ACLK			0x20bc
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_DIT_IPCLKPORT_PCLK			0x20c0
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_PDMA_IPCLKPORT_ACLK			0x20c4
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_PDMA_IPCLKPORT_PCLK			0x20c8
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_PPMU_DMA_IPCLKPORT_ACLK		0x20cc
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_PPMU_DMA_IPCLKPORT_PCLK		0x20d0
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_RTIC_IPCLKPORT_ACLK			0x20d4
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_RTIC_IPCLKPORT_PCLK			0x20d8
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_SPDMA_IPCLKPORT_ACLK			0x20dc
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_SPDMA_IPCLKPORT_PCLK			0x20e0
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_SSS_IPCLKPORT_ACLK			0x20e4
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_SSS_IPCLKPORT_PCLK			0x20e8
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_SSS_IPCLKPORT_I_ACLK			0x20ec
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_SSS_IPCLKPORT_I_PCLK			0x20f0
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_SYSMMU_MISC_IPCLKPORT_CLK_S2		0x20f4
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_SYSMMU_SSS_IPCLKPORT_CLK_S1		0x20f8
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_SYSREG_MISC_IPCLKPORT_PCLK		0x20fc
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_TMU_SUB_IPCLKPORT_PCLK			0x2100
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_TMU_TOP_IPCLKPORT_PCLK			0x2104
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_WDT_CLUSTER0_IPCLKPORT_PCLK		0x2108
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_WDT_CLUSTER1_IPCLKPORT_PCLK		0x210c
#define CLK_CON_GAT_GOUT_BLK_MISC_UID_XIU_D_MISC_IPCLKPORT_ACLK			0x2110
#define DMYQCH_CON_PPMU_DMA_QCH							0x3000
#define DMYQCH_CON_PUF_QCH							0x3004
#define PCH_CON_LHM_AXI_D_SSS_PCH						0x300c
#define PCH_CON_LHM_AXI_P_GIC_PCH						0x3010
#define PCH_CON_LHM_AXI_P_MISC_PCH						0x3014
#define PCH_CON_LHS_ACEL_D_MISC_PCH						0x3018
#define PCH_CON_LHS_AST_IRI_GICCPU_PCH						0x301c
#define PCH_CON_LHS_AXI_D_SSS_PCH						0x3020
#define QCH_CON_ADM_AHB_SSS_QCH							0x3024
#define QCH_CON_DIT_QCH								0x3028
#define QCH_CON_GIC_QCH								0x3030
#define QCH_CON_LHM_AST_ICC_CPUGIC_QCH						0x3038
#define QCH_CON_LHM_AXI_D_SSS_QCH						0x303c
#define QCH_CON_LHM_AXI_P_GIC_QCH						0x3040
#define QCH_CON_LHM_AXI_P_MISC_QCH						0x3044
#define QCH_CON_LHS_ACEL_D_MISC_QCH						0x3048
#define QCH_CON_LHS_AST_IRI_GICCPU_QCH						0x304c
#define QCH_CON_LHS_AXI_D_SSS_QCH						0x3050
#define QCH_CON_MCT_QCH								0x3054
#define QCH_CON_MISC_CMU_MISC_QCH						0x3058
#define QCH_CON_OTP_CON_BIRA_QCH						0x305c
#define QCH_CON_OTP_CON_BISR_QCH						0x3060
#define QCH_CON_OTP_CON_TOP_QCH							0x3064
#define QCH_CON_PDMA_QCH							0x3068
#define QCH_CON_PPMU_MISC_QCH							0x306c
#define QCH_CON_QE_DIT_QCH							0x3070
#define QCH_CON_QE_PDMA_QCH							0x3074
#define QCH_CON_QE_PPMU_DMA_QCH							0x3078
#define QCH_CON_QE_RTIC_QCH							0x307c
#define QCH_CON_QE_SPDMA_QCH							0x3080
#define QCH_CON_QE_SSS_QCH							0x3084
#define QCH_CON_RTIC_QCH							0x3088
#define QCH_CON_SPDMA_QCH							0x308c
#define QCH_CON_SSMT_DIT_QCH							0x3090
#define QCH_CON_SSMT_PDMA_QCH							0x3094
#define QCH_CON_SSMT_PPMU_DMA_QCH						0x3098
#define QCH_CON_SSMT_RTIC_QCH							0x309c
#define QCH_CON_SSMT_SPDMA_QCH							0x30a0
#define QCH_CON_SSMT_SSS_QCH							0x30a4
#define QCH_CON_SSS_QCH								0x30a8
#define QCH_CON_SYSMMU_MISC_QCH							0x30ac
#define QCH_CON_SYSMMU_SSS_QCH							0x30b0
#define QCH_CON_SYSREG_MISC_QCH							0x30b4
#define QCH_CON_TMU_SUB_QCH							0x30b8
#define QCH_CON_TMU_TOP_QCH							0x30bc
#define QCH_CON_WDT_CLUSTER0_QCH						0x30c0
#define QCH_CON_WDT_CLUSTER1_QCH						0x30c4
#define QUEUE_CTRL_REG_BLK_MISC_CMU_MISC					0x3c00

static const unsigned long misc_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_MISC_BUS_USER,
	PLL_CON1_MUX_CLKCMU_MISC_BUS_USER,
	PLL_CON0_MUX_CLKCMU_MISC_SSS_USER,
	PLL_CON1_MUX_CLKCMU_MISC_SSS_USER,
	MISC_CMU_MISC_CONTROLLER_OPTION,
	CLKOUT_CON_BLK_MISC_CMU_MISC_CLKOUT0,
	CLK_CON_MUX_MUX_CLK_MISC_GIC,
	CLK_CON_DIV_DIV_CLK_MISC_BUSP,
	CLK_CON_DIV_DIV_CLK_MISC_GIC,
	CLK_CON_GAT_CLK_BLK_MISC_UID_MISC_CMU_MISC_IPCLKPORT_PCLK,
	CLK_CON_GAT_CLK_BLK_MISC_UID_OTP_CON_BIRA_IPCLKPORT_I_OSCCLK,
	CLK_CON_GAT_CLK_BLK_MISC_UID_OTP_CON_BISR_IPCLKPORT_I_OSCCLK,
	CLK_CON_GAT_CLK_BLK_MISC_UID_OTP_CON_TOP_IPCLKPORT_I_OSCCLK,
	CLK_CON_GAT_CLK_BLK_MISC_UID_RSTNSYNC_CLK_MISC_OSCCLK_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_ADM_AHB_SSS_IPCLKPORT_HCLKM,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_AD_APB_DIT_IPCLKPORT_PCLKM,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_AD_APB_PUF_IPCLKPORT_PCLKM,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_DIT_IPCLKPORT_ICLKL2A,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_D_TZPC_MISC_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_GIC_IPCLKPORT_GICCLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_GPC_MISC_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_LHM_AST_ICC_CPUGIC_IPCLKPORT_I_CLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_LHM_AXI_D_SSS_IPCLKPORT_I_CLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_LHM_AXI_P_GIC_IPCLKPORT_I_CLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_LHM_AXI_P_MISC_IPCLKPORT_I_CLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_LHS_ACEL_D_MISC_IPCLKPORT_I_CLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_LHS_AST_IRI_GICCPU_IPCLKPORT_I_CLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_LHS_AXI_D_SSS_IPCLKPORT_I_CLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_MCT_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_OTP_CON_BIRA_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_OTP_CON_BISR_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_OTP_CON_TOP_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_PDMA_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_PPMU_DMA_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_PPMU_MISC_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_PPMU_MISC_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_PUF_IPCLKPORT_I_CLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_DIT_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_DIT_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_PDMA_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_PDMA_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_PPMU_DMA_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_PPMU_DMA_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_RTIC_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_RTIC_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_SPDMA_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_SPDMA_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_SSS_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_SSS_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_RSTNSYNC_CLK_MISC_BUSD_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_RSTNSYNC_CLK_MISC_BUSP_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_RSTNSYNC_CLK_MISC_GIC_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_RSTNSYNC_CLK_MISC_SSS_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_RTIC_IPCLKPORT_I_ACLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_RTIC_IPCLKPORT_I_PCLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_SPDMA_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_DIT_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_DIT_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_PDMA_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_PDMA_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_PPMU_DMA_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_PPMU_DMA_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_RTIC_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_RTIC_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_SPDMA_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_SPDMA_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_SSS_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_SSS_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_SSS_IPCLKPORT_I_ACLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_SSS_IPCLKPORT_I_PCLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_SYSMMU_MISC_IPCLKPORT_CLK_S2,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_SYSMMU_SSS_IPCLKPORT_CLK_S1,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_SYSREG_MISC_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_TMU_SUB_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_TMU_TOP_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_WDT_CLUSTER0_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_WDT_CLUSTER1_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_MISC_UID_XIU_D_MISC_IPCLKPORT_ACLK,
	DMYQCH_CON_PPMU_DMA_QCH,
	DMYQCH_CON_PUF_QCH,
	PCH_CON_LHM_AXI_D_SSS_PCH,
	PCH_CON_LHM_AXI_P_GIC_PCH,
	PCH_CON_LHM_AXI_P_MISC_PCH,
	PCH_CON_LHS_ACEL_D_MISC_PCH,
	PCH_CON_LHS_AST_IRI_GICCPU_PCH,
	PCH_CON_LHS_AXI_D_SSS_PCH,
	QCH_CON_ADM_AHB_SSS_QCH,
	QCH_CON_DIT_QCH,
	QCH_CON_GIC_QCH,
	QCH_CON_LHM_AST_ICC_CPUGIC_QCH,
	QCH_CON_LHM_AXI_D_SSS_QCH,
	QCH_CON_LHM_AXI_P_GIC_QCH,
	QCH_CON_LHM_AXI_P_MISC_QCH,
	QCH_CON_LHS_ACEL_D_MISC_QCH,
	QCH_CON_LHS_AST_IRI_GICCPU_QCH,
	QCH_CON_LHS_AXI_D_SSS_QCH,
	QCH_CON_MCT_QCH,
	QCH_CON_MISC_CMU_MISC_QCH,
	QCH_CON_OTP_CON_BIRA_QCH,
	QCH_CON_OTP_CON_BISR_QCH,
	QCH_CON_OTP_CON_TOP_QCH,
	QCH_CON_PDMA_QCH,
	QCH_CON_PPMU_MISC_QCH,
	QCH_CON_QE_DIT_QCH,
	QCH_CON_QE_PDMA_QCH,
	QCH_CON_QE_PPMU_DMA_QCH,
	QCH_CON_QE_RTIC_QCH,
	QCH_CON_QE_SPDMA_QCH,
	QCH_CON_QE_SSS_QCH,
	QCH_CON_RTIC_QCH,
	QCH_CON_SPDMA_QCH,
	QCH_CON_SSMT_DIT_QCH,
	QCH_CON_SSMT_PDMA_QCH,
	QCH_CON_SSMT_PPMU_DMA_QCH,
	QCH_CON_SSMT_RTIC_QCH,
	QCH_CON_SSMT_SPDMA_QCH,
	QCH_CON_SSMT_SSS_QCH,
	QCH_CON_SSS_QCH,
	QCH_CON_SYSMMU_MISC_QCH,
	QCH_CON_SYSMMU_SSS_QCH,
	QCH_CON_SYSREG_MISC_QCH,
	QCH_CON_TMU_SUB_QCH,
	QCH_CON_TMU_TOP_QCH,
	QCH_CON_WDT_CLUSTER0_QCH,
	QCH_CON_WDT_CLUSTER1_QCH,
	QUEUE_CTRL_REG_BLK_MISC_CMU_MISC,
};

 /* List of parent clocks for Muxes in CMU_MISC */
PNAME(mout_misc_bus_user_p)		= { "oscclk", "dout_cmu_misc_bus" };
PNAME(mout_misc_sss_user_p)		= { "oscclk", "dout_cmu_misc_sss" };
PNAME(mout_misc_gic_p)			= { "dout_misc_gic", "oscclk" };

static const struct samsung_mux_clock misc_mux_clks[] __initconst = {
	MUX(CLK_MOUT_MISC_BUS_USER, "mout_misc_bus_user", mout_misc_bus_user_p,
	    PLL_CON0_MUX_CLKCMU_MISC_BUS_USER, 4, 1),
	MUX(CLK_MOUT_MISC_SSS_USER, "mout_misc_sss_user", mout_misc_sss_user_p,
	    PLL_CON0_MUX_CLKCMU_MISC_SSS_USER, 4, 1),
	MUX(CLK_MOUT_MISC_GIC, "mout_misc_gic", mout_misc_gic_p,
	    CLK_CON_MUX_MUX_CLK_MISC_GIC, 0, 0),
};

static const struct samsung_div_clock misc_div_clks[] __initconst = {
	DIV(CLK_DOUT_MISC_BUSP, "dout_misc_busp", "mout_misc_bus_user",
	    CLK_CON_DIV_DIV_CLK_MISC_BUSP, 0, 3),
	DIV(CLK_DOUT_MISC_GIC, "dout_misc_gic", "mout_misc_bus_user",
	    CLK_CON_DIV_DIV_CLK_MISC_GIC, 0, 3),
};

static const struct samsung_gate_clock misc_gate_clks[] __initconst = {
	GATE(CLK_GOUT_MISC_MISC_CMU_MISC_PCLK,
	     "gout_misc_misc_cmu_misc_pclk", "dout_misc_busp",
	     CLK_CON_GAT_CLK_BLK_MISC_UID_MISC_CMU_MISC_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_OTP_CON_BIRA_I_OSCCLK,
	     "gout_misc_otp_con_bira_i_oscclk", "oscclk",
	     CLK_CON_GAT_CLK_BLK_MISC_UID_OTP_CON_BIRA_IPCLKPORT_I_OSCCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_OTP_CON_BISR_I_OSCCLK,
	     "gout_misc_otp_con_bisr_i_oscclk", "oscclk",
	     CLK_CON_GAT_CLK_BLK_MISC_UID_OTP_CON_BISR_IPCLKPORT_I_OSCCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_OTP_CON_TOP_I_OSCCLK,
	     "gout_misc_otp_con_top_i_oscclk", "oscclk",
	     CLK_CON_GAT_CLK_BLK_MISC_UID_OTP_CON_TOP_IPCLKPORT_I_OSCCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_CLK_MISC_OSCCLK_CLK,
	     "gout_misc_clk_misc_oscclk_clk", "oscclk",
	     CLK_CON_GAT_CLK_BLK_MISC_UID_RSTNSYNC_CLK_MISC_OSCCLK_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_ADM_AHB_SSS_HCLKM,
	     "gout_misc_adm_ahb_sss_hclkm", "mout_misc_sss_user",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_ADM_AHB_SSS_IPCLKPORT_HCLKM,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_AD_APB_DIT_PCLKM,
	     "gout_misc_ad_apb_dit_pclkm", "mout_misc_bus_user",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_AD_APB_DIT_IPCLKPORT_PCLKM,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_D_TZPC_MISC_PCLK,
	     "gout_misc_d_tzpc_misc_pclk", "dout_misc_busp",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_D_TZPC_MISC_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_GIC_GICCLK,
	     "gout_misc_gic_gicclk", "mout_misc_gic",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_GIC_IPCLKPORT_GICCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_GPC_MISC_PCLK,
	     "gout_misc_gpc_misc_pclk", "dout_misc_busp",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_GPC_MISC_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_LHM_AST_ICC_CPUGIC_I_CLK,
	     "gout_misc_lhm_ast_icc_gpugic_i_clk", "mout_misc_gic",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_LHM_AST_ICC_CPUGIC_IPCLKPORT_I_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_LHM_AXI_D_SSS_I_CLK,
	     "gout_misc_lhm_axi_d_sss_i_clk", "mout_misc_bus_user",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_LHM_AXI_D_SSS_IPCLKPORT_I_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_LHM_AXI_P_GIC_I_CLK,
	     "gout_misc_lhm_axi_p_gic_i_clk", "mout_misc_gic",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_LHM_AXI_P_GIC_IPCLKPORT_I_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_LHM_AXI_P_MISC_I_CLK,
	     "gout_misc_lhm_axi_p_misc_i_clk", "dout_misc_busp",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_LHM_AXI_P_MISC_IPCLKPORT_I_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_LHS_ACEL_D_MISC_I_CLK,
	     "gout_misc_lhs_acel_d_misc_i_clk", "mout_misc_bus_user",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_LHS_ACEL_D_MISC_IPCLKPORT_I_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_LHS_AST_IRI_GICCPU_I_CLK,
	     "gout_misc_lhs_ast_iri_giccpu_i_clk", "mout_misc_gic",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_LHS_AST_IRI_GICCPU_IPCLKPORT_I_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_LHS_AXI_D_SSS_I_CLK,
	     "gout_misc_lhs_axi_d_sss_i_clk", "mout_misc_sss_user",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_LHS_AXI_D_SSS_IPCLKPORT_I_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_MCT_PCLK, "gout_misc_mct_pclk",
	     "dout_misc_busp",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_MCT_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_OTP_CON_BIRA_PCLK,
	     "gout_misc_otp_con_bira_pclk", "dout_misc_busp",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_OTP_CON_BIRA_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_OTP_CON_BISR_PCLK,
	     "gout_misc_otp_con_bisr_pclk", "dout_misc_busp",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_OTP_CON_BISR_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_OTP_CON_TOP_PCLK,
	     "gout_misc_otp_con_top_pclk", "dout_misc_busp",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_OTP_CON_TOP_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_PDMA_ACLK, "gout_misc_pdma_aclk",
	     "mout_misc_bus_user",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_PDMA_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_PPMU_MISC_ACLK,
	     "gout_misc_ppmu_misc_aclk", "mout_misc_bus_user",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_PPMU_MISC_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_PPMU_MISC_PCLK,
	     "gout_misc_ppmu_misc_pclk", "dout_misc_busp",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_PPMU_MISC_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_PUF_I_CLK,
	     "gout_misc_puf_i_clk", "mout_misc_sss_user",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_PUF_IPCLKPORT_I_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_QE_DIT_ACLK,
	     "gout_misc_qe_dit_aclk", "mout_misc_bus_user",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_DIT_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_QE_DIT_PCLK,
	     "gout_misc_qe_dit_pclk", "dout_misc_busp",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_DIT_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_QE_PDMA_ACLK,
	     "gout_misc_qe_pdma_aclk", "mout_misc_bus_user",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_PDMA_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_QE_PDMA_PCLK,
	     "gout_misc_qe_pdma_pclk", "dout_misc_busp",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_PDMA_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_QE_PPMU_DMA_ACLK,
	     "gout_misc_qe_ppmu_dma_aclk", "mout_misc_bus_user",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_PPMU_DMA_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_QE_PPMU_DMA_PCLK,
	     "gout_misc_qe_ppmu_dma_pclk", "dout_misc_busp",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_PPMU_DMA_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_QE_RTIC_ACLK,
	     "gout_misc_qe_rtic_aclk", "mout_misc_bus_user",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_RTIC_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_QE_RTIC_PCLK,
	     "gout_misc_qe_rtic_pclk", "dout_misc_busp",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_RTIC_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_QE_SPDMA_ACLK,
	     "gout_misc_qe_spdma_aclk", "mout_misc_bus_user",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_SPDMA_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_QE_SPDMA_PCLK,
	     "gout_misc_qe_spdma_pclk", "dout_misc_busp",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_SPDMA_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_QE_SSS_ACLK,
	     "gout_misc_qe_sss_aclk", "mout_misc_sss_user",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_SSS_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_QE_SSS_PCLK,
	     "gout_misc_qe_sss_pclk", "dout_misc_busp",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_QE_SSS_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_CLK_MISC_BUSD_CLK,
	     "gout_misc_clk_misc_busd_clk", "mout_misc_bus_user",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_RSTNSYNC_CLK_MISC_BUSD_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_CLK_MISC_BUSP_CLK,
	     "gout_misc_clk_misc_busp_clk", "dout_misc_busp",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_RSTNSYNC_CLK_MISC_BUSP_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_CLK_MISC_GIC_CLK,
	     "gout_misc_clk_misc_gic_clk", "mout_misc_gic",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_RSTNSYNC_CLK_MISC_GIC_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_CLK_MISC_SSS_CLK,
	     "gout_misc_clk_misc_sss_clk", "mout_misc_sss_user",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_RSTNSYNC_CLK_MISC_SSS_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_RTIC_I_ACLK,
	     "gout_misc_rtic_i_aclk", "mout_misc_bus_user",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_RTIC_IPCLKPORT_I_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_RTIC_I_PCLK, "gout_misc_rtic_i_pclk",
	     "dout_misc_busp",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_RTIC_IPCLKPORT_I_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_SPDMA_ACLK,
	     "gout_misc_spdma_ipclockport_aclk", "mout_misc_bus_user",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_SPDMA_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_SSMT_DIT_ACLK,
	     "gout_misc_ssmt_dit_aclk", "mout_misc_bus_user",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_DIT_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_SSMT_DIT_PCLK,
	     "gout_misc_ssmt_dit_pclk", "dout_misc_busp",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_DIT_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_SSMT_PDMA_ACLK,
	     "gout_misc_ssmt_pdma_aclk", "mout_misc_bus_user",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_PDMA_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_SSMT_PDMA_PCLK,
	     "gout_misc_ssmt_pdma_pclk", "dout_misc_busp",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_PDMA_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_SSMT_PPMU_DMA_ACLK,
	     "gout_misc_ssmt_ppmu_dma_aclk", "mout_misc_bus_user",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_PPMU_DMA_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_SSMT_PPMU_DMA_PCLK,
	     "gout_misc_ssmt_ppmu_dma_pclk", "dout_misc_busp",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_PPMU_DMA_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_SSMT_RTIC_ACLK,
	     "gout_misc_ssmt_rtic_aclk", "mout_misc_bus_user",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_RTIC_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_SSMT_RTIC_PCLK,
	     "gout_misc_ssmt_rtic_pclk", "dout_misc_busp",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_RTIC_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_SSMT_SPDMA_ACLK,
	     "gout_misc_ssmt_spdma_aclk", "mout_misc_bus_user",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_SPDMA_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_SSMT_SPDMA_PCLK,
	     "gout_misc_ssmt_spdma_pclk", "dout_misc_busp",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_SPDMA_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_SSMT_SSS_ACLK,
	     "gout_misc_ssmt_sss_aclk", "mout_misc_bus_user",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_SSS_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_SSMT_SSS_PCLK,
	     "gout_misc_ssmt_sss_pclk", "dout_misc_busp",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_SSMT_SSS_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_SSS_I_ACLK,
	     "gout_misc_sss_i_aclk", "mout_misc_bus_user",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_SSS_IPCLKPORT_I_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_SSS_I_PCLK,
	     "gout_misc_sss_i_pclk", "dout_misc_busp",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_SSS_IPCLKPORT_I_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_SYSMMU_MISC_CLK_S2,
	     "gout_misc_sysmmu_misc_clk_s2", "mout_misc_bus_user",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_SYSMMU_MISC_IPCLKPORT_CLK_S2,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_SYSMMU_SSS_CLK_S1,
	     "gout_misc_sysmmu_sss_clk_s1", "mout_misc_sss_user",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_SYSMMU_SSS_IPCLKPORT_CLK_S1,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_SYSREG_MISC_PCLK,
	     "gout_misc_sysreg_misc_pclk", "dout_misc_busp",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_SYSREG_MISC_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_TMU_SUB_PCLK,
	     "gout_misc_tmu_sub_pclk", "dout_misc_busp",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_TMU_SUB_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_TMU_TOP_PCLK,
	     "gout_misc_tmu_top_pclk", "dout_misc_busp",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_TMU_TOP_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_WDT_CLUSTER0_PCLK,
	     "gout_misc_wdt_cluster0_pclk", "dout_misc_busp",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_WDT_CLUSTER0_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_WDT_CLUSTER1_PCLK,
	     "gout_misc_wdt_cluster1_pclk", "dout_misc_busp",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_WDT_CLUSTER1_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MISC_XIU_D_MISC_ACLK,
	     "gout_misc_xiu_d_misc_aclk", "mout_misc_bus_user",
	     CLK_CON_GAT_GOUT_BLK_MISC_UID_XIU_D_MISC_IPCLKPORT_ACLK,
	     21, 0, 0),
};

static const struct samsung_cmu_info misc_cmu_info __initconst = {
	.mux_clks		= misc_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(misc_mux_clks),
	.div_clks		= misc_div_clks,
	.nr_div_clks		= ARRAY_SIZE(misc_div_clks),
	.gate_clks		= misc_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(misc_gate_clks),
	.nr_clk_ids		= CLKS_NR_MISC,
	.clk_regs		= misc_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(misc_clk_regs),
	.clk_name		= "bus",
};

static void __init gs101_cmu_misc_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &misc_cmu_info);
}

/* Register CMU_MISC early, as it's needed for MCT timer */
CLK_OF_DECLARE(gs101_cmu_misc, "google,gs101-cmu-misc",
	       gs101_cmu_misc_init);

/* ---- CMU_PERIC0 ---------------------------------------------------------- */

/* Register Offset definitions for CMU_PERIC0 (0x10800000) */
#define PLL_CON0_MUX_CLKCMU_PERIC0_BUS_USER		0x0600
#define PLL_CON1_MUX_CLKCMU_PERIC0_BUS_USER		0x0604
#define PLL_CON0_MUX_CLKCMU_PERIC0_I3C_USER		0x0610
#define PLL_CON1_MUX_CLKCMU_PERIC0_I3C_USER		0x0614
#define PLL_CON0_MUX_CLKCMU_PERIC0_USI0_UART_USER	0x0620
#define PLL_CON1_MUX_CLKCMU_PERIC0_USI0_UART_USER	0x0624
#define PLL_CON0_MUX_CLKCMU_PERIC0_USI14_USI_USER	0x0640
#define PLL_CON1_MUX_CLKCMU_PERIC0_USI14_USI_USER	0x0644
#define PLL_CON0_MUX_CLKCMU_PERIC0_USI1_USI_USER	0x0650
#define PLL_CON1_MUX_CLKCMU_PERIC0_USI1_USI_USER	0x0654
#define PLL_CON0_MUX_CLKCMU_PERIC0_USI2_USI_USER	0x0660
#define PLL_CON1_MUX_CLKCMU_PERIC0_USI2_USI_USER	0x0664
#define PLL_CON0_MUX_CLKCMU_PERIC0_USI3_USI_USER	0x0670
#define PLL_CON1_MUX_CLKCMU_PERIC0_USI3_USI_USER	0x0674
#define PLL_CON0_MUX_CLKCMU_PERIC0_USI4_USI_USER	0x0680
#define PLL_CON1_MUX_CLKCMU_PERIC0_USI4_USI_USER	0x0684
#define PLL_CON0_MUX_CLKCMU_PERIC0_USI5_USI_USER	0x0690
#define PLL_CON1_MUX_CLKCMU_PERIC0_USI5_USI_USER	0x0694
#define PLL_CON0_MUX_CLKCMU_PERIC0_USI6_USI_USER	0x06a0
#define PLL_CON1_MUX_CLKCMU_PERIC0_USI6_USI_USER	0x06a4
#define PLL_CON0_MUX_CLKCMU_PERIC0_USI7_USI_USER	0x06b0
#define PLL_CON1_MUX_CLKCMU_PERIC0_USI7_USI_USER	0x06b4
#define PLL_CON0_MUX_CLKCMU_PERIC0_USI8_USI_USER	0x06c0
#define PLL_CON1_MUX_CLKCMU_PERIC0_USI8_USI_USER	0x06c4
#define PERIC0_CMU_PERIC0_CONTROLLER_OPTION		0x0800
#define CLKOUT_CON_BLK_PERIC0_CMU_PERIC0_CLKOUT0	0x0810
#define CLK_CON_DIV_DIV_CLK_PERIC0_I3C			0x1800
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI0_UART		0x1804
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI14_USI		0x180c
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI1_USI		0x1810
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI2_USI		0x1814
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI3_USI		0x1820
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI4_USI		0x1824
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI5_USI		0x1828
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI6_USI		0x182c
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI7_USI		0x1830
#define CLK_CON_DIV_DIV_CLK_PERIC0_USI8_USI		0x1834
#define CLK_CON_BUF_CLKBUF_PERIC0_IP			0x2000
#define CLK_CON_GAT_CLK_BLK_PERIC0_UID_PERIC0_CMU_PERIC0_IPCLKPORT_PCLK			0x2004
#define CLK_CON_GAT_CLK_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_OSCCLK_IPCLKPORT_CLK		0x2008
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_D_TZPC_PERIC0_IPCLKPORT_PCLK			0x200c
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_GPC_PERIC0_IPCLKPORT_PCLK			0x2010
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_GPIO_PERIC0_IPCLKPORT_PCLK			0x2014
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_LHM_AXI_P_PERIC0_IPCLKPORT_I_CLK		0x2018
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_0			0x201c
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_1			0x2020
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_10			0x2024
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_11			0x2028
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_12			0x202c
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_13			0x2030
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_14			0x2034
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_15			0x2038
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_2			0x203c
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_3			0x2040
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_4			0x2044
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_5			0x2048
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_6			0x204c
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_7			0x2050
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_8			0x2054
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_9			0x2058
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_0			0x205c
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_1			0x2060
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_10			0x2064
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_11			0x2068
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_12			0x206c
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_13			0x2070
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_14			0x2074
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_15			0x2078
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_2			0x207c
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_3			0x2080
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_4			0x2084
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_5			0x2088
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_6			0x208c
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_7			0x2090
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_8			0x2094
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_9			0x2098
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_IPCLK_0			0x209c
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_IPCLK_2			0x20a4
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_PCLK_0			0x20a8
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_PCLK_2			0x20b0
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_BUSP_IPCLKPORT_CLK		0x20b4
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_I3C_IPCLKPORT_CLK		0x20b8
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI0_UART_IPCLKPORT_CLK	0x20bc
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI14_USI_IPCLKPORT_CLK	0x20c4
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI1_USI_IPCLKPORT_CLK	0x20c8
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI2_USI_IPCLKPORT_CLK	0x20cc
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI3_USI_IPCLKPORT_CLK	0x20d0
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI4_USI_IPCLKPORT_CLK	0x20d4
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI5_USI_IPCLKPORT_CLK	0x20d8
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI6_USI_IPCLKPORT_CLK	0x20dc
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI7_USI_IPCLKPORT_CLK	0x20e0
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI8_USI_IPCLKPORT_CLK	0x20e4
#define CLK_CON_GAT_GOUT_BLK_PERIC0_UID_SYSREG_PERIC0_IPCLKPORT_PCLK			0x20e8
#define DMYQCH_CON_PERIC0_TOP0_QCH_S1			0x3000
#define DMYQCH_CON_PERIC0_TOP0_QCH_S2			0x3004
#define DMYQCH_CON_PERIC0_TOP0_QCH_S3			0x3008
#define DMYQCH_CON_PERIC0_TOP0_QCH_S4			0x300c
#define DMYQCH_CON_PERIC0_TOP0_QCH_S5			0x3010
#define DMYQCH_CON_PERIC0_TOP0_QCH_S6			0x3014
#define DMYQCH_CON_PERIC0_TOP0_QCH_S7			0x3018
#define DMYQCH_CON_PERIC0_TOP0_QCH_S8			0x301c
#define PCH_CON_LHM_AXI_P_PERIC0_PCH			0x3020
#define QCH_CON_D_TZPC_PERIC0_QCH			0x3024
#define QCH_CON_GPC_PERIC0_QCH				0x3028
#define QCH_CON_GPIO_PERIC0_QCH				0x302c
#define QCH_CON_LHM_AXI_P_PERIC0_QCH			0x3030
#define QCH_CON_PERIC0_CMU_PERIC0_QCH			0x3034
#define QCH_CON_PERIC0_TOP0_QCH_I3C1			0x3038
#define QCH_CON_PERIC0_TOP0_QCH_I3C2			0x303c
#define QCH_CON_PERIC0_TOP0_QCH_I3C3			0x3040
#define QCH_CON_PERIC0_TOP0_QCH_I3C4			0x3044
#define QCH_CON_PERIC0_TOP0_QCH_I3C5			0x3048
#define QCH_CON_PERIC0_TOP0_QCH_I3C6			0x304c
#define QCH_CON_PERIC0_TOP0_QCH_I3C7			0x3050
#define QCH_CON_PERIC0_TOP0_QCH_I3C8			0x3054
#define QCH_CON_PERIC0_TOP0_QCH_USI1_USI		0x3058
#define QCH_CON_PERIC0_TOP0_QCH_USI2_USI		0x305c
#define QCH_CON_PERIC0_TOP0_QCH_USI3_USI		0x3060
#define QCH_CON_PERIC0_TOP0_QCH_USI4_USI		0x3064
#define QCH_CON_PERIC0_TOP0_QCH_USI5_USI		0x3068
#define QCH_CON_PERIC0_TOP0_QCH_USI6_USI		0x306c
#define QCH_CON_PERIC0_TOP0_QCH_USI7_USI		0x3070
#define QCH_CON_PERIC0_TOP0_QCH_USI8_USI		0x3074
#define QCH_CON_PERIC0_TOP1_QCH_USI0_UART		0x3078
#define QCH_CON_PERIC0_TOP1_QCH_USI14_UART		0x307c
#define QCH_CON_SYSREG_PERIC0_QCH			0x3080
#define QUEUE_CTRL_REG_BLK_PERIC0_CMU_PERIC0		0x3c00

static const unsigned long peric0_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_PERIC0_BUS_USER,
	PLL_CON1_MUX_CLKCMU_PERIC0_BUS_USER,
	PLL_CON0_MUX_CLKCMU_PERIC0_I3C_USER,
	PLL_CON1_MUX_CLKCMU_PERIC0_I3C_USER,
	PLL_CON0_MUX_CLKCMU_PERIC0_USI0_UART_USER,
	PLL_CON1_MUX_CLKCMU_PERIC0_USI0_UART_USER,
	PLL_CON0_MUX_CLKCMU_PERIC0_USI14_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC0_USI14_USI_USER,
	PLL_CON0_MUX_CLKCMU_PERIC0_USI1_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC0_USI1_USI_USER,
	PLL_CON0_MUX_CLKCMU_PERIC0_USI2_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC0_USI2_USI_USER,
	PLL_CON0_MUX_CLKCMU_PERIC0_USI3_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC0_USI3_USI_USER,
	PLL_CON0_MUX_CLKCMU_PERIC0_USI4_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC0_USI4_USI_USER,
	PLL_CON0_MUX_CLKCMU_PERIC0_USI5_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC0_USI5_USI_USER,
	PLL_CON0_MUX_CLKCMU_PERIC0_USI6_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC0_USI6_USI_USER,
	PLL_CON0_MUX_CLKCMU_PERIC0_USI7_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC0_USI7_USI_USER,
	PLL_CON0_MUX_CLKCMU_PERIC0_USI8_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC0_USI8_USI_USER,
	PERIC0_CMU_PERIC0_CONTROLLER_OPTION,
	CLKOUT_CON_BLK_PERIC0_CMU_PERIC0_CLKOUT0,
	CLK_CON_DIV_DIV_CLK_PERIC0_I3C,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI0_UART,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI14_USI,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI1_USI,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI2_USI,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI3_USI,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI4_USI,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI5_USI,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI6_USI,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI6_USI,
	CLK_CON_DIV_DIV_CLK_PERIC0_USI8_USI,
	CLK_CON_BUF_CLKBUF_PERIC0_IP,
	CLK_CON_GAT_CLK_BLK_PERIC0_UID_PERIC0_CMU_PERIC0_IPCLKPORT_PCLK,
	CLK_CON_GAT_CLK_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_OSCCLK_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_D_TZPC_PERIC0_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_GPC_PERIC0_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_GPIO_PERIC0_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_LHM_AXI_P_PERIC0_IPCLKPORT_I_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_0,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_1,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_10,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_11,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_12,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_13,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_14,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_15,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_2,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_3,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_4,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_5,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_6,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_7,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_8,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_9,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_0,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_1,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_10,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_11,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_12,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_13,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_14,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_15,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_2,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_3,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_4,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_5,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_6,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_7,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_8,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_9,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_IPCLK_0,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_IPCLK_2,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_PCLK_0,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_PCLK_2,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_BUSP_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_I3C_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI0_UART_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI14_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI1_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI2_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI3_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI4_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI5_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI6_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI7_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI8_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC0_UID_SYSREG_PERIC0_IPCLKPORT_PCLK,
	DMYQCH_CON_PERIC0_TOP0_QCH_S1,
	DMYQCH_CON_PERIC0_TOP0_QCH_S2,
	DMYQCH_CON_PERIC0_TOP0_QCH_S3,
	DMYQCH_CON_PERIC0_TOP0_QCH_S4,
	DMYQCH_CON_PERIC0_TOP0_QCH_S5,
	DMYQCH_CON_PERIC0_TOP0_QCH_S6,
	DMYQCH_CON_PERIC0_TOP0_QCH_S7,
	DMYQCH_CON_PERIC0_TOP0_QCH_S8,
	PCH_CON_LHM_AXI_P_PERIC0_PCH,
	QCH_CON_D_TZPC_PERIC0_QCH,
	QCH_CON_GPC_PERIC0_QCH,
	QCH_CON_GPIO_PERIC0_QCH,
	QCH_CON_LHM_AXI_P_PERIC0_QCH,
	QCH_CON_PERIC0_CMU_PERIC0_QCH,
	QCH_CON_PERIC0_TOP0_QCH_I3C1,
	QCH_CON_PERIC0_TOP0_QCH_I3C2,
	QCH_CON_PERIC0_TOP0_QCH_I3C3,
	QCH_CON_PERIC0_TOP0_QCH_I3C4,
	QCH_CON_PERIC0_TOP0_QCH_I3C5,
	QCH_CON_PERIC0_TOP0_QCH_I3C6,
	QCH_CON_PERIC0_TOP0_QCH_I3C7,
	QCH_CON_PERIC0_TOP0_QCH_I3C8,
	QCH_CON_PERIC0_TOP0_QCH_USI1_USI,
	QCH_CON_PERIC0_TOP0_QCH_USI2_USI,
	QCH_CON_PERIC0_TOP0_QCH_USI3_USI,
	QCH_CON_PERIC0_TOP0_QCH_USI4_USI,
	QCH_CON_PERIC0_TOP0_QCH_USI5_USI,
	QCH_CON_PERIC0_TOP0_QCH_USI6_USI,
	QCH_CON_PERIC0_TOP0_QCH_USI7_USI,
	QCH_CON_PERIC0_TOP0_QCH_USI8_USI,
	QCH_CON_PERIC0_TOP1_QCH_USI0_UART,
	QCH_CON_PERIC0_TOP1_QCH_USI14_UART,
	QCH_CON_SYSREG_PERIC0_QCH,
	QUEUE_CTRL_REG_BLK_PERIC0_CMU_PERIC0,
};

/* List of parent clocks for Muxes in CMU_PERIC0 */
PNAME(mout_peric0_bus_user_p)		= { "oscclk", "dout_cmu_peric0_bus" };
PNAME(mout_peric0_i3c_user_p)		= { "oscclk", "dout_cmu_peric0_ip" };
PNAME(mout_peric0_usi0_uart_user_p)	= { "oscclk", "dout_cmu_peric0_ip" };
PNAME(mout_peric0_usi_usi_user_p)	= { "oscclk", "dout_cmu_peric0_ip" };

static const struct samsung_mux_clock peric0_mux_clks[] __initconst = {
	MUX(CLK_MOUT_PERIC0_BUS_USER, "mout_peric0_bus_user",
	    mout_peric0_bus_user_p, PLL_CON0_MUX_CLKCMU_PERIC0_BUS_USER, 4, 1),
	MUX(CLK_MOUT_PERIC0_I3C_USER, "mout_peric0_i3c_user",
	    mout_peric0_i3c_user_p, PLL_CON0_MUX_CLKCMU_PERIC0_I3C_USER, 4, 1),
	MUX(CLK_MOUT_PERIC0_USI0_UART_USER,
	    "mout_peric0_usi0_uart_user", mout_peric0_usi0_uart_user_p,
	    PLL_CON0_MUX_CLKCMU_PERIC0_USI0_UART_USER, 4, 1),
	nMUX(CLK_MOUT_PERIC0_USI14_USI_USER,
	     "mout_peric0_usi14_usi_user", mout_peric0_usi_usi_user_p,
	     PLL_CON0_MUX_CLKCMU_PERIC0_USI14_USI_USER, 4, 1),
	nMUX(CLK_MOUT_PERIC0_USI1_USI_USER,
	     "mout_peric0_usi1_usi_user", mout_peric0_usi_usi_user_p,
	     PLL_CON0_MUX_CLKCMU_PERIC0_USI1_USI_USER, 4, 1),
	nMUX(CLK_MOUT_PERIC0_USI2_USI_USER,
	     "mout_peric0_usi2_usi_user", mout_peric0_usi_usi_user_p,
	     PLL_CON0_MUX_CLKCMU_PERIC0_USI2_USI_USER, 4, 1),
	nMUX(CLK_MOUT_PERIC0_USI3_USI_USER,
	     "mout_peric0_usi3_usi_user", mout_peric0_usi_usi_user_p,
	     PLL_CON0_MUX_CLKCMU_PERIC0_USI3_USI_USER, 4, 1),
	nMUX(CLK_MOUT_PERIC0_USI4_USI_USER,
	     "mout_peric0_usi4_usi_user", mout_peric0_usi_usi_user_p,
	     PLL_CON0_MUX_CLKCMU_PERIC0_USI4_USI_USER, 4, 1),
	nMUX(CLK_MOUT_PERIC0_USI5_USI_USER,
	     "mout_peric0_usi5_usi_user", mout_peric0_usi_usi_user_p,
	     PLL_CON0_MUX_CLKCMU_PERIC0_USI5_USI_USER, 4, 1),
	nMUX(CLK_MOUT_PERIC0_USI6_USI_USER,
	     "mout_peric0_usi6_usi_user", mout_peric0_usi_usi_user_p,
	     PLL_CON0_MUX_CLKCMU_PERIC0_USI6_USI_USER, 4, 1),
	nMUX(CLK_MOUT_PERIC0_USI7_USI_USER,
	     "mout_peric0_usi7_usi_user", mout_peric0_usi_usi_user_p,
	     PLL_CON0_MUX_CLKCMU_PERIC0_USI7_USI_USER, 4, 1),
	nMUX(CLK_MOUT_PERIC0_USI8_USI_USER,
	     "mout_peric0_usi8_usi_user", mout_peric0_usi_usi_user_p,
	     PLL_CON0_MUX_CLKCMU_PERIC0_USI8_USI_USER, 4, 1),
};

static const struct samsung_div_clock peric0_div_clks[] __initconst = {
	DIV(CLK_DOUT_PERIC0_I3C, "dout_peric0_i3c", "mout_peric0_i3c_user",
	    CLK_CON_DIV_DIV_CLK_PERIC0_I3C, 0, 4),
	DIV(CLK_DOUT_PERIC0_USI0_UART,
	    "dout_peric0_usi0_uart", "mout_peric0_usi0_uart_user",
	    CLK_CON_DIV_DIV_CLK_PERIC0_USI0_UART, 0, 4),
	DIV_F(CLK_DOUT_PERIC0_USI14_USI,
	      "dout_peric0_usi14_usi", "mout_peric0_usi14_usi_user",
	      CLK_CON_DIV_DIV_CLK_PERIC0_USI14_USI, 0, 4,
	      CLK_SET_RATE_PARENT, 0),
	DIV_F(CLK_DOUT_PERIC0_USI1_USI,
	      "dout_peric0_usi1_usi", "mout_peric0_usi1_usi_user",
	      CLK_CON_DIV_DIV_CLK_PERIC0_USI1_USI, 0, 4,
	      CLK_SET_RATE_PARENT, 0),
	DIV_F(CLK_DOUT_PERIC0_USI2_USI,
	      "dout_peric0_usi2_usi", "mout_peric0_usi2_usi_user",
	      CLK_CON_DIV_DIV_CLK_PERIC0_USI2_USI, 0, 4,
	      CLK_SET_RATE_PARENT, 0),
	DIV_F(CLK_DOUT_PERIC0_USI3_USI,
	      "dout_peric0_usi3_usi", "mout_peric0_usi3_usi_user",
	      CLK_CON_DIV_DIV_CLK_PERIC0_USI3_USI, 0, 4,
	      CLK_SET_RATE_PARENT, 0),
	DIV_F(CLK_DOUT_PERIC0_USI4_USI,
	      "dout_peric0_usi4_usi", "mout_peric0_usi4_usi_user",
	      CLK_CON_DIV_DIV_CLK_PERIC0_USI4_USI, 0, 4,
	      CLK_SET_RATE_PARENT, 0),
	DIV_F(CLK_DOUT_PERIC0_USI5_USI,
	      "dout_peric0_usi5_usi", "mout_peric0_usi5_usi_user",
	      CLK_CON_DIV_DIV_CLK_PERIC0_USI5_USI, 0, 4,
	      CLK_SET_RATE_PARENT, 0),
	DIV_F(CLK_DOUT_PERIC0_USI6_USI,
	      "dout_peric0_usi6_usi", "mout_peric0_usi6_usi_user",
	      CLK_CON_DIV_DIV_CLK_PERIC0_USI6_USI, 0, 4,
	      CLK_SET_RATE_PARENT, 0),
	DIV_F(CLK_DOUT_PERIC0_USI7_USI,
	      "dout_peric0_usi7_usi", "mout_peric0_usi7_usi_user",
	      CLK_CON_DIV_DIV_CLK_PERIC0_USI7_USI, 0, 4,
	      CLK_SET_RATE_PARENT, 0),
	DIV_F(CLK_DOUT_PERIC0_USI8_USI,
	      "dout_peric0_usi8_usi", "mout_peric0_usi8_usi_user",
	      CLK_CON_DIV_DIV_CLK_PERIC0_USI8_USI, 0, 4,
	      CLK_SET_RATE_PARENT, 0),
};

static const struct samsung_gate_clock peric0_gate_clks[] __initconst = {
	/* Disabling this clock makes the system hang. Mark the clock as critical. */
	GATE(CLK_GOUT_PERIC0_PERIC0_CMU_PERIC0_PCLK,
	     "gout_peric0_peric0_cmu_peric0_pclk", "mout_peric0_bus_user",
	     CLK_CON_GAT_CLK_BLK_PERIC0_UID_PERIC0_CMU_PERIC0_IPCLKPORT_PCLK,
	     21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_PERIC0_CLK_PERIC0_OSCCLK_CLK,
	     "gout_peric0_clk_peric0_oscclk_clk", "oscclk",
	     CLK_CON_GAT_CLK_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_OSCCLK_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_D_TZPC_PERIC0_PCLK,
	     "gout_peric0_d_tzpc_peric0_pclk", "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_D_TZPC_PERIC0_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_GPC_PERIC0_PCLK,
	     "gout_peric0_gpc_peric0_pclk", "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_GPC_PERIC0_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_GPIO_PERIC0_PCLK,
	     "gout_peric0_gpio_peric0_pclk", "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_GPIO_PERIC0_IPCLKPORT_PCLK,
	     21, CLK_IGNORE_UNUSED, 0),
	/* Disabling this clock makes the system hang. Mark the clock as critical. */
	GATE(CLK_GOUT_PERIC0_LHM_AXI_P_PERIC0_I_CLK,
	     "gout_peric0_lhm_axi_p_peric0_i_clk", "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_LHM_AXI_P_PERIC0_IPCLKPORT_I_CLK,
	     21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP0_IPCLK_0,
	     "gout_peric0_peric0_top0_ipclk_0", "dout_peric0_usi1_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_0,
	     21, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP0_IPCLK_1,
	     "gout_peric0_peric0_top0_ipclk_1", "dout_peric0_usi2_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_1,
	     21, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP0_IPCLK_10,
	     "gout_peric0_peric0_top0_ipclk_10", "dout_peric0_i3c",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_10,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP0_IPCLK_11,
	     "gout_peric0_peric0_top0_ipclk_11", "dout_peric0_i3c",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_11,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP0_IPCLK_12,
	     "gout_peric0_peric0_top0_ipclk_12", "dout_peric0_i3c",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_12,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP0_IPCLK_13,
	     "gout_peric0_peric0_top0_ipclk_13", "dout_peric0_i3c",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_13,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP0_IPCLK_14,
	     "gout_peric0_peric0_top0_ipclk_14", "dout_peric0_i3c",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_14,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP0_IPCLK_15,
	     "gout_peric0_peric0_top0_ipclk_15", "dout_peric0_i3c",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_15,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP0_IPCLK_2,
	     "gout_peric0_peric0_top0_ipclk_2", "dout_peric0_usi3_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_2,
	     21, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP0_IPCLK_3,
	     "gout_peric0_peric0_top0_ipclk_3", "dout_peric0_usi4_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_3,
	     21, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP0_IPCLK_4,
	     "gout_peric0_peric0_top0_ipclk_4", "dout_peric0_usi5_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_4,
	     21, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP0_IPCLK_5,
	     "gout_peric0_peric0_top0_ipclk_5", "dout_peric0_usi6_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_5,
	     21, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP0_IPCLK_6,
	     "gout_peric0_peric0_top0_ipclk_6", "dout_peric0_usi7_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_6,
	     21, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP0_IPCLK_7,
	     "gout_peric0_peric0_top0_ipclk_7", "dout_peric0_usi8_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_7,
	     21, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP0_IPCLK_8,
	     "gout_peric0_peric0_top0_ipclk_8", "dout_peric0_i3c",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_8,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP0_IPCLK_9,
	     "gout_peric0_peric0_top0_ipclk_9", "dout_peric0_i3c",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_IPCLK_9,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP0_PCLK_0,
	     "gout_peric0_peric0_top0_pclk_0", "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_0,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP0_PCLK_1,
	     "gout_peric0_peric0_top0_pclk_1", "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_1,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP0_PCLK_10,
	     "gout_peric0_peric0_top0_pclk_10", "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_10,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP0_PCLK_11,
	     "gout_peric0_peric0_top0_pclk_11", "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_11,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP0_PCLK_12,
	     "gout_peric0_peric0_top0_pclk_12", "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_12,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP0_PCLK_13,
	     "gout_peric0_peric0_top0_pclk_13", "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_13,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP0_PCLK_14,
	     "gout_peric0_peric0_top0_pclk_14", "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_14,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP0_PCLK_15,
	     "gout_peric0_peric0_top0_pclk_15", "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_15,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP0_PCLK_2,
	     "gout_peric0_peric0_top0_pclk_2", "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_2,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP0_PCLK_3,
	     "gout_peric0_peric0_top0_pclk_3", "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_3,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP0_PCLK_4,
	     "gout_peric0_peric0_top0_pclk_4", "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_4,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP0_PCLK_5,
	     "gout_peric0_peric0_top0_pclk_5", "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_5,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP0_PCLK_6,
	     "gout_peric0_peric0_top0_pclk_6", "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_6,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP0_PCLK_7,
	     "gout_peric0_peric0_top0_pclk_7", "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_7,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP0_PCLK_8,
	     "gout_peric0_peric0_top0_pclk_8", "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_8,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP0_PCLK_9,
	     "gout_peric0_peric0_top0_pclk_9", "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP0_IPCLKPORT_PCLK_9,
	     21, 0, 0),
	/* Disabling this clock makes the system hang. Mark the clock as critical. */
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP1_IPCLK_0,
	     "gout_peric0_peric0_top1_ipclk_0", "dout_peric0_usi0_uart",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_IPCLK_0,
	     21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP1_IPCLK_2,
	     "gout_peric0_peric0_top1_ipclk_2", "dout_peric0_usi14_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_IPCLK_2,
	     21, CLK_SET_RATE_PARENT, 0),
	/* Disabling this clock makes the system hang. Mark the clock as critical. */
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP1_PCLK_0,
	     "gout_peric0_peric0_top1_pclk_0", "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_PCLK_0,
	     21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_PERIC0_PERIC0_TOP1_PCLK_2,
	     "gout_peric0_peric0_top1_pclk_2", "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_PERIC0_TOP1_IPCLKPORT_PCLK_2,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_CLK_PERIC0_BUSP_CLK,
	     "gout_peric0_clk_peric0_busp_clk", "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_BUSP_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_CLK_PERIC0_I3C_CLK,
	     "gout_peric0_clk_peric0_i3c_clk", "dout_peric0_i3c",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_I3C_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_CLK_PERIC0_USI0_UART_CLK,
	     "gout_peric0_clk_peric0_usi0_uart_clk", "dout_peric0_usi0_uart",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI0_UART_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_CLK_PERIC0_USI14_USI_CLK,
	     "gout_peric0_clk_peric0_usi14_usi_clk", "dout_peric0_usi14_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI14_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_CLK_PERIC0_USI1_USI_CLK,
	     "gout_peric0_clk_peric0_usi1_usi_clk", "dout_peric0_usi1_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI1_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_CLK_PERIC0_USI2_USI_CLK,
	     "gout_peric0_clk_peric0_usi2_usi_clk", "dout_peric0_usi2_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI2_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_CLK_PERIC0_USI3_USI_CLK,
	     "gout_peric0_clk_peric0_usi3_usi_clk", "dout_peric0_usi3_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI3_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_CLK_PERIC0_USI4_USI_CLK,
	     "gout_peric0_clk_peric0_usi4_usi_clk", "dout_peric0_usi4_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI4_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_CLK_PERIC0_USI5_USI_CLK,
	     "gout_peric0_clk_peric0_usi5_usi_clk", "dout_peric0_usi5_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI5_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_CLK_PERIC0_USI6_USI_CLK,
	     "gout_peric0_clk_peric0_usi6_usi_clk", "dout_peric0_usi6_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI6_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_CLK_PERIC0_USI7_USI_CLK,
	     "gout_peric0_clk_peric0_usi7_usi_clk", "dout_peric0_usi7_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI7_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_CLK_PERIC0_USI8_USI_CLK,
	     "gout_peric0_clk_peric0_usi8_usi_clk", "dout_peric0_usi8_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_RSTNSYNC_CLK_PERIC0_USI8_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC0_SYSREG_PERIC0_PCLK,
	     "gout_peric0_sysreg_peric0_pclk", "mout_peric0_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC0_UID_SYSREG_PERIC0_IPCLKPORT_PCLK,
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
	.clk_name		= "bus",
};

/* ---- CMU_PERIC1 ---------------------------------------------------------- */

/* Register Offset definitions for CMU_PERIC1 (0x10c00000) */
#define PLL_CON0_MUX_CLKCMU_PERIC1_BUS_USER						0x0600
#define PLL_CON1_MUX_CLKCMU_PERIC1_BUS_USER						0x0604
#define PLL_CON0_MUX_CLKCMU_PERIC1_I3C_USER						0x0610
#define PLL_CON1_MUX_CLKCMU_PERIC1_I3C_USER						0x0614
#define PLL_CON0_MUX_CLKCMU_PERIC1_USI0_USI_USER					0x0620
#define PLL_CON1_MUX_CLKCMU_PERIC1_USI0_USI_USER					0x0624
#define PLL_CON0_MUX_CLKCMU_PERIC1_USI10_USI_USER					0x0630
#define PLL_CON1_MUX_CLKCMU_PERIC1_USI10_USI_USER					0x0634
#define PLL_CON0_MUX_CLKCMU_PERIC1_USI11_USI_USER					0x0640
#define PLL_CON1_MUX_CLKCMU_PERIC1_USI11_USI_USER					0x0644
#define PLL_CON0_MUX_CLKCMU_PERIC1_USI12_USI_USER					0x0650
#define PLL_CON1_MUX_CLKCMU_PERIC1_USI12_USI_USER					0x0654
#define PLL_CON0_MUX_CLKCMU_PERIC1_USI13_USI_USER					0x0660
#define PLL_CON1_MUX_CLKCMU_PERIC1_USI13_USI_USER					0x0664
#define PLL_CON0_MUX_CLKCMU_PERIC1_USI9_USI_USER					0x0670
#define PLL_CON1_MUX_CLKCMU_PERIC1_USI9_USI_USER					0x0674
#define PERIC1_CMU_PERIC1_CONTROLLER_OPTION						0x0800
#define CLKOUT_CON_BLK_PERIC1_CMU_PERIC1_CLKOUT0					0x0810
#define CLK_CON_DIV_DIV_CLK_PERIC1_I3C							0x1800
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI0_USI						0x1804
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI10_USI						0x1808
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI11_USI						0x180c
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI12_USI						0x1810
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI13_USI						0x1814
#define CLK_CON_DIV_DIV_CLK_PERIC1_USI9_USI						0x1818
#define CLK_CON_BUF_CLKBUF_PERIC1_IP							0x2000
#define CLK_CON_GAT_CLK_BLK_PERIC1_UID_PERIC1_CMU_PERIC1_IPCLKPORT_PCLK			0x2004
#define CLK_CON_GAT_CLK_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_I3C_IPCLKPORT_CLK		0x2008
#define CLK_CON_GAT_CLK_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_OSCCLK_IPCLKPORT_CLK		0x200c
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_D_TZPC_PERIC1_IPCLKPORT_PCLK			0x2010
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_GPC_PERIC1_IPCLKPORT_PCLK			0x2014
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_GPIO_PERIC1_IPCLKPORT_PCLK			0x2018
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_LHM_AXI_P_PERIC1_IPCLKPORT_I_CLK		0x201c
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_1			0x2020
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_2			0x2024
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_3			0x2028
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_4			0x202c
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_5			0x2030
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_6			0x2034
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_8			0x2038
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_1			0x203c
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_15			0x2040
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_2			0x2044
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_3			0x2048
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_4			0x204c
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_5			0x2050
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_6			0x2054
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_8			0x2058
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_BUSP_IPCLKPORT_CLK		0x205c
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI0_USI_IPCLKPORT_CLK	0x2060
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI10_USI_IPCLKPORT_CLK	0x2064
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI11_USI_IPCLKPORT_CLK	0x2068
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI12_USI_IPCLKPORT_CLK	0x206c
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI13_USI_IPCLKPORT_CLK	0x2070
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI9_USI_IPCLKPORT_CLK	0x2074
#define CLK_CON_GAT_GOUT_BLK_PERIC1_UID_SYSREG_PERIC1_IPCLKPORT_PCLK			0x2078
#define DMYQCH_CON_PERIC1_TOP0_QCH_S							0x3000
#define PCH_CON_LHM_AXI_P_PERIC1_PCH							0x3004
#define QCH_CON_D_TZPC_PERIC1_QCH							0x3008
#define QCH_CON_GPC_PERIC1_QCH								0x300c
#define QCH_CON_GPIO_PERIC1_QCH								0x3010
#define QCH_CON_LHM_AXI_P_PERIC1_QCH							0x3014
#define QCH_CON_PERIC1_CMU_PERIC1_QCH							0x3018
#define QCH_CON_PERIC1_TOP0_QCH_I3C0							0x301c
#define QCH_CON_PERIC1_TOP0_QCH_PWM							0x3020
#define QCH_CON_PERIC1_TOP0_QCH_USI0_USI						0x3024
#define QCH_CON_PERIC1_TOP0_QCH_USI10_USI						0x3028
#define QCH_CON_PERIC1_TOP0_QCH_USI11_USI						0x302c
#define QCH_CON_PERIC1_TOP0_QCH_USI12_USI						0x3030
#define QCH_CON_PERIC1_TOP0_QCH_USI13_USI						0x3034
#define QCH_CON_PERIC1_TOP0_QCH_USI9_USI						0x3038
#define QCH_CON_SYSREG_PERIC1_QCH							0x303c
#define QUEUE_CTRL_REG_BLK_PERIC1_CMU_PERIC1						0x3c00

static const unsigned long peric1_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_PERIC1_BUS_USER,
	PLL_CON1_MUX_CLKCMU_PERIC1_BUS_USER,
	PLL_CON0_MUX_CLKCMU_PERIC1_I3C_USER,
	PLL_CON1_MUX_CLKCMU_PERIC1_I3C_USER,
	PLL_CON0_MUX_CLKCMU_PERIC1_USI0_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC1_USI0_USI_USER,
	PLL_CON0_MUX_CLKCMU_PERIC1_USI10_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC1_USI10_USI_USER,
	PLL_CON0_MUX_CLKCMU_PERIC1_USI11_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC1_USI11_USI_USER,
	PLL_CON0_MUX_CLKCMU_PERIC1_USI12_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC1_USI12_USI_USER,
	PLL_CON0_MUX_CLKCMU_PERIC1_USI13_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC1_USI13_USI_USER,
	PLL_CON0_MUX_CLKCMU_PERIC1_USI9_USI_USER,
	PLL_CON1_MUX_CLKCMU_PERIC1_USI9_USI_USER,
	PERIC1_CMU_PERIC1_CONTROLLER_OPTION,
	CLKOUT_CON_BLK_PERIC1_CMU_PERIC1_CLKOUT0,
	CLK_CON_DIV_DIV_CLK_PERIC1_I3C,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI0_USI,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI10_USI,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI11_USI,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI12_USI,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI13_USI,
	CLK_CON_DIV_DIV_CLK_PERIC1_USI9_USI,
	CLK_CON_BUF_CLKBUF_PERIC1_IP,
	CLK_CON_GAT_CLK_BLK_PERIC1_UID_PERIC1_CMU_PERIC1_IPCLKPORT_PCLK,
	CLK_CON_GAT_CLK_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_I3C_IPCLKPORT_CLK,
	CLK_CON_GAT_CLK_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_OSCCLK_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_D_TZPC_PERIC1_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_GPC_PERIC1_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_GPIO_PERIC1_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_LHM_AXI_P_PERIC1_IPCLKPORT_I_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_1,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_2,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_3,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_4,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_5,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_6,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_8,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_1,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_15,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_2,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_3,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_4,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_5,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_6,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_8,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_BUSP_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI0_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI10_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI11_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI12_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI13_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI9_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_PERIC1_UID_SYSREG_PERIC1_IPCLKPORT_PCLK,
	DMYQCH_CON_PERIC1_TOP0_QCH_S,
	PCH_CON_LHM_AXI_P_PERIC1_PCH,
	QCH_CON_D_TZPC_PERIC1_QCH,
	QCH_CON_GPC_PERIC1_QCH,
	QCH_CON_GPIO_PERIC1_QCH,
	QCH_CON_LHM_AXI_P_PERIC1_QCH,
	QCH_CON_PERIC1_CMU_PERIC1_QCH,
	QCH_CON_PERIC1_TOP0_QCH_I3C0,
	QCH_CON_PERIC1_TOP0_QCH_PWM,
	QCH_CON_PERIC1_TOP0_QCH_USI0_USI,
	QCH_CON_PERIC1_TOP0_QCH_USI10_USI,
	QCH_CON_PERIC1_TOP0_QCH_USI11_USI,
	QCH_CON_PERIC1_TOP0_QCH_USI12_USI,
	QCH_CON_PERIC1_TOP0_QCH_USI13_USI,
	QCH_CON_PERIC1_TOP0_QCH_USI9_USI,
	QCH_CON_SYSREG_PERIC1_QCH,
	QUEUE_CTRL_REG_BLK_PERIC1_CMU_PERIC1,
};

/* List of parent clocks for Muxes in CMU_PERIC1 */
PNAME(mout_peric1_bus_user_p)		= { "oscclk", "dout_cmu_peric1_bus" };
PNAME(mout_peric1_nonbususer_p)		= { "oscclk", "dout_cmu_peric1_ip" };

static const struct samsung_mux_clock peric1_mux_clks[] __initconst = {
	MUX(CLK_MOUT_PERIC1_BUS_USER, "mout_peric1_bus_user",
	    mout_peric1_bus_user_p, PLL_CON0_MUX_CLKCMU_PERIC1_BUS_USER, 4, 1),
	MUX(CLK_MOUT_PERIC1_I3C_USER,
	    "mout_peric1_i3c_user", mout_peric1_nonbususer_p,
	    PLL_CON0_MUX_CLKCMU_PERIC1_I3C_USER, 4, 1),
	nMUX(CLK_MOUT_PERIC1_USI0_USI_USER,
	     "mout_peric1_usi0_usi_user", mout_peric1_nonbususer_p,
	     PLL_CON0_MUX_CLKCMU_PERIC1_USI0_USI_USER, 4, 1),
	nMUX(CLK_MOUT_PERIC1_USI10_USI_USER,
	     "mout_peric1_usi10_usi_user", mout_peric1_nonbususer_p,
	     PLL_CON0_MUX_CLKCMU_PERIC1_USI10_USI_USER, 4, 1),
	nMUX(CLK_MOUT_PERIC1_USI11_USI_USER,
	     "mout_peric1_usi11_usi_user", mout_peric1_nonbususer_p,
	     PLL_CON0_MUX_CLKCMU_PERIC1_USI11_USI_USER, 4, 1),
	nMUX(CLK_MOUT_PERIC1_USI12_USI_USER,
	     "mout_peric1_usi12_usi_user", mout_peric1_nonbususer_p,
	     PLL_CON0_MUX_CLKCMU_PERIC1_USI12_USI_USER, 4, 1),
	nMUX(CLK_MOUT_PERIC1_USI13_USI_USER,
	     "mout_peric1_usi13_usi_user", mout_peric1_nonbususer_p,
	     PLL_CON0_MUX_CLKCMU_PERIC1_USI13_USI_USER, 4, 1),
	nMUX(CLK_MOUT_PERIC1_USI9_USI_USER,
	     "mout_peric1_usi9_usi_user", mout_peric1_nonbususer_p,
	     PLL_CON0_MUX_CLKCMU_PERIC1_USI9_USI_USER, 4, 1),
};

static const struct samsung_div_clock peric1_div_clks[] __initconst = {
	DIV(CLK_DOUT_PERIC1_I3C, "dout_peric1_i3c", "mout_peric1_i3c_user",
	    CLK_CON_DIV_DIV_CLK_PERIC1_I3C, 0, 4),
	DIV_F(CLK_DOUT_PERIC1_USI0_USI,
	      "dout_peric1_usi0_usi", "mout_peric1_usi0_usi_user",
	      CLK_CON_DIV_DIV_CLK_PERIC1_USI0_USI, 0, 4,
	      CLK_SET_RATE_PARENT, 0),
	DIV_F(CLK_DOUT_PERIC1_USI10_USI,
	      "dout_peric1_usi10_usi", "mout_peric1_usi10_usi_user",
	      CLK_CON_DIV_DIV_CLK_PERIC1_USI10_USI, 0, 4,
	      CLK_SET_RATE_PARENT, 0),
	DIV_F(CLK_DOUT_PERIC1_USI11_USI,
	      "dout_peric1_usi11_usi", "mout_peric1_usi11_usi_user",
	      CLK_CON_DIV_DIV_CLK_PERIC1_USI11_USI, 0, 4,
	      CLK_SET_RATE_PARENT, 0),
	DIV_F(CLK_DOUT_PERIC1_USI12_USI,
	      "dout_peric1_usi12_usi", "mout_peric1_usi12_usi_user",
	      CLK_CON_DIV_DIV_CLK_PERIC1_USI12_USI, 0, 4,
	      CLK_SET_RATE_PARENT, 0),
	DIV_F(CLK_DOUT_PERIC1_USI13_USI,
	      "dout_peric1_usi13_usi", "mout_peric1_usi13_usi_user",
	      CLK_CON_DIV_DIV_CLK_PERIC1_USI13_USI, 0, 4,
	      CLK_SET_RATE_PARENT, 0),
	DIV_F(CLK_DOUT_PERIC1_USI9_USI,
	      "dout_peric1_usi9_usi", "mout_peric1_usi9_usi_user",
	      CLK_CON_DIV_DIV_CLK_PERIC1_USI9_USI, 0, 4,
	      CLK_SET_RATE_PARENT, 0),
};

static const struct samsung_gate_clock peric1_gate_clks[] __initconst = {
	GATE(CLK_GOUT_PERIC1_PCLK,
	     "gout_peric1_peric1_pclk", "mout_peric1_bus_user",
	     CLK_CON_GAT_CLK_BLK_PERIC1_UID_PERIC1_CMU_PERIC1_IPCLKPORT_PCLK,
	     21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_PERIC1_CLK_PERIC1_I3C_CLK,
	     "gout_peric1_clk_peric1_i3c_clk", "dout_peric1_i3c",
	     CLK_CON_GAT_CLK_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_I3C_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_CLK_PERIC1_OSCCLK_CLK,
	     "gout_peric1_clk_peric1_oscclk_clk", "oscclk",
	     CLK_CON_GAT_CLK_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_OSCCLK_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_D_TZPC_PERIC1_PCLK,
	     "gout_peric1_d_tzpc_peric1_pclk", "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_D_TZPC_PERIC1_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_GPC_PERIC1_PCLK,
	     "gout_peric1_gpc_peric1_pclk", "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_GPC_PERIC1_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_GPIO_PERIC1_PCLK,
	     "gout_peric1_gpio_peric1_pclk", "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_GPIO_PERIC1_IPCLKPORT_PCLK,
	     21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_PERIC1_LHM_AXI_P_PERIC1_I_CLK,
	     "gout_peric1_lhm_axi_p_peric1_i_clk", "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_LHM_AXI_P_PERIC1_IPCLKPORT_I_CLK,
	     21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_PERIC1_PERIC1_TOP0_IPCLK_1,
	     "gout_peric1_peric1_top0_ipclk_1", "dout_peric1_usi0_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_1,
	     21, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERIC1_PERIC1_TOP0_IPCLK_2,
	     "gout_peric1_peric1_top0_ipclk_2", "dout_peric1_usi9_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_2,
	     21, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERIC1_PERIC1_TOP0_IPCLK_3,
	     "gout_peric1_peric1_top0_ipclk_3", "dout_peric1_usi10_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_3,
	     21, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERIC1_PERIC1_TOP0_IPCLK_4,
	     "gout_peric1_peric1_top0_ipclk_4", "dout_peric1_usi11_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_4,
	     21, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERIC1_PERIC1_TOP0_IPCLK_5,
	     "gout_peric1_peric1_top0_ipclk_5", "dout_peric1_usi12_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_5,
	     21, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERIC1_PERIC1_TOP0_IPCLK_6,
	     "gout_peric1_peric1_top0_ipclk_6", "dout_peric1_usi13_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_6,
	     21, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERIC1_PERIC1_TOP0_IPCLK_8,
	     "gout_peric1_peric1_top0_ipclk_8", "dout_peric1_i3c",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_IPCLK_8,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_PERIC1_TOP0_PCLK_1,
	     "gout_peric1_peric1_top0_pclk_1", "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_1,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_PERIC1_TOP0_PCLK_15,
	     "gout_peric1_peric1_top0_pclk_15", "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_15,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_PERIC1_TOP0_PCLK_2,
	     "gout_peric1_peric1_top0_pclk_2", "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_2,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_PERIC1_TOP0_PCLK_3,
	     "gout_peric1_peric1_top0_pclk_3", "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_3,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_PERIC1_TOP0_PCLK_4,
	     "gout_peric1_peric1_top0_pclk_4", "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_4,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_PERIC1_TOP0_PCLK_5,
	     "gout_peric1_peric1_top0_pclk_5", "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_5,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_PERIC1_TOP0_PCLK_6,
	     "gout_peric1_peric1_top0_pclk_6", "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_6,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_PERIC1_TOP0_PCLK_8,
	     "gout_peric1_peric1_top0_pclk_8", "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_PERIC1_TOP0_IPCLKPORT_PCLK_8,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_CLK_PERIC1_BUSP_CLK,
	     "gout_peric1_clk_peric1_busp_clk", "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_BUSP_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_CLK_PERIC1_USI0_USI_CLK,
	     "gout_peric1_clk_peric1_usi0_usi_clk", "dout_peric1_usi0_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI0_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_CLK_PERIC1_USI10_USI_CLK,
	     "gout_peric1_clk_peric1_usi10_usi_clk", "dout_peric1_usi10_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI10_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_CLK_PERIC1_USI11_USI_CLK,
	     "gout_peric1_clk_peric1_usi11_usi_clk", "dout_peric1_usi11_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI11_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_CLK_PERIC1_USI12_USI_CLK,
	     "gout_peric1_clk_peric1_usi12_usi_clk", "dout_peric1_usi12_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI12_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_CLK_PERIC1_USI13_USI_CLK,
	     "gout_peric1_clk_peric1_usi13_usi_clk", "dout_peric1_usi13_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI13_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_CLK_PERIC1_USI9_USI_CLK,
	     "gout_peric1_clk_peric1_usi9_usi_clk", "dout_peric1_usi9_usi",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_RSTNSYNC_CLK_PERIC1_USI9_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERIC1_SYSREG_PERIC1_PCLK,
	     "gout_peric1_sysreg_peric1_pclk", "mout_peric1_bus_user",
	     CLK_CON_GAT_GOUT_BLK_PERIC1_UID_SYSREG_PERIC1_IPCLKPORT_PCLK,
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
	.clk_name		= "bus",
};

/* ---- platform_driver ----------------------------------------------------- */

static int __init gs101_cmu_probe(struct platform_device *pdev)
{
	const struct samsung_cmu_info *info;
	struct device *dev = &pdev->dev;

	info = of_device_get_match_data(dev);
	exynos_arm64_register_cmu(dev, dev->of_node, info);

	return 0;
}

static const struct of_device_id gs101_cmu_of_match[] = {
	{
		.compatible = "google,gs101-cmu-apm",
		.data = &apm_cmu_info,
	}, {
		.compatible = "google,gs101-cmu-hsi0",
		.data = &hsi0_cmu_info,
	}, {
		.compatible = "google,gs101-cmu-hsi2",
		.data = &hsi2_cmu_info,
	}, {
		.compatible = "google,gs101-cmu-peric0",
		.data = &peric0_cmu_info,
	}, {
		.compatible = "google,gs101-cmu-peric1",
		.data = &peric1_cmu_info,
	}, {
	},
};

static struct platform_driver gs101_cmu_driver __refdata = {
	.driver	= {
		.name = "gs101-cmu",
		.of_match_table = gs101_cmu_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = gs101_cmu_probe,
};

static int __init gs101_cmu_init(void)
{
	return platform_driver_register(&gs101_cmu_driver);
}
core_initcall(gs101_cmu_init);
