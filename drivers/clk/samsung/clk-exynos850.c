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
#include <linux/platform_device.h>

#include <dt-bindings/clock/exynos850.h>

#include "clk.h"
#include "clk-cpu.h"
#include "clk-exynos-arm64.h"

/* NOTE: Must be equal to the last clock ID increased by one */
#define CLKS_NR_TOP			(CLK_DOUT_CPUCL1_SWITCH + 1)
#define CLKS_NR_APM			(CLK_GOUT_SYSREG_APM_PCLK + 1)
#define CLKS_NR_AUD			(CLK_GOUT_AUD_CMU_AUD_PCLK + 1)
#define CLKS_NR_CMGP			(CLK_GOUT_SYSREG_CMGP_PCLK + 1)
#define CLKS_NR_CPUCL0			(CLK_CLUSTER0_SCLK + 1)
#define CLKS_NR_CPUCL1			(CLK_CLUSTER1_SCLK + 1)
#define CLKS_NR_G3D			(CLK_GOUT_G3D_SYSREG_PCLK + 1)
#define CLKS_NR_HSI			(CLK_GOUT_HSI_CMU_HSI_PCLK + 1)
#define CLKS_NR_IS			(CLK_GOUT_IS_SYSREG_PCLK + 1)
#define CLKS_NR_MFCMSCL			(CLK_GOUT_MFCMSCL_SYSREG_PCLK + 1)
#define CLKS_NR_PERI			(CLK_GOUT_BUSIF_TMU_PCLK + 1)
#define CLKS_NR_CORE			(CLK_GOUT_SPDMA_CORE_ACLK + 1)
#define CLKS_NR_DPU			(CLK_GOUT_DPU_SYSREG_PCLK + 1)

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
#define CLK_CON_MUX_MUX_CLKCMU_APM_BUS		0x1000
#define CLK_CON_MUX_MUX_CLKCMU_AUD		0x1004
#define CLK_CON_MUX_MUX_CLKCMU_CORE_BUS		0x1014
#define CLK_CON_MUX_MUX_CLKCMU_CORE_CCI		0x1018
#define CLK_CON_MUX_MUX_CLKCMU_CORE_MMC_EMBD	0x101c
#define CLK_CON_MUX_MUX_CLKCMU_CORE_SSS		0x1020
#define CLK_CON_MUX_MUX_CLKCMU_CPUCL0_DBG	0x1024
#define CLK_CON_MUX_MUX_CLKCMU_CPUCL0_SWITCH	0x1028
#define CLK_CON_MUX_MUX_CLKCMU_CPUCL1_DBG	0x102c
#define CLK_CON_MUX_MUX_CLKCMU_CPUCL1_SWITCH	0x1030
#define CLK_CON_MUX_MUX_CLKCMU_DPU		0x1034
#define CLK_CON_MUX_MUX_CLKCMU_G3D_SWITCH	0x1038
#define CLK_CON_MUX_MUX_CLKCMU_HSI_BUS		0x103c
#define CLK_CON_MUX_MUX_CLKCMU_HSI_MMC_CARD	0x1040
#define CLK_CON_MUX_MUX_CLKCMU_HSI_USB20DRD	0x1044
#define CLK_CON_MUX_MUX_CLKCMU_IS_BUS		0x1048
#define CLK_CON_MUX_MUX_CLKCMU_IS_GDC		0x104c
#define CLK_CON_MUX_MUX_CLKCMU_IS_ITP		0x1050
#define CLK_CON_MUX_MUX_CLKCMU_IS_VRA		0x1054
#define CLK_CON_MUX_MUX_CLKCMU_MFCMSCL_JPEG	0x1058
#define CLK_CON_MUX_MUX_CLKCMU_MFCMSCL_M2M	0x105c
#define CLK_CON_MUX_MUX_CLKCMU_MFCMSCL_MCSC	0x1060
#define CLK_CON_MUX_MUX_CLKCMU_MFCMSCL_MFC	0x1064
#define CLK_CON_MUX_MUX_CLKCMU_PERI_BUS		0x1070
#define CLK_CON_MUX_MUX_CLKCMU_PERI_IP		0x1074
#define CLK_CON_MUX_MUX_CLKCMU_PERI_UART	0x1078
#define CLK_CON_DIV_CLKCMU_APM_BUS		0x180c
#define CLK_CON_DIV_CLKCMU_AUD			0x1810
#define CLK_CON_DIV_CLKCMU_CORE_BUS		0x1820
#define CLK_CON_DIV_CLKCMU_CORE_CCI		0x1824
#define CLK_CON_DIV_CLKCMU_CORE_MMC_EMBD	0x1828
#define CLK_CON_DIV_CLKCMU_CORE_SSS		0x182c
#define CLK_CON_DIV_CLKCMU_CPUCL0_DBG		0x1830
#define CLK_CON_DIV_CLKCMU_CPUCL0_SWITCH	0x1834
#define CLK_CON_DIV_CLKCMU_CPUCL1_DBG		0x1838
#define CLK_CON_DIV_CLKCMU_CPUCL1_SWITCH	0x183c
#define CLK_CON_DIV_CLKCMU_DPU			0x1840
#define CLK_CON_DIV_CLKCMU_G3D_SWITCH		0x1844
#define CLK_CON_DIV_CLKCMU_HSI_BUS		0x1848
#define CLK_CON_DIV_CLKCMU_HSI_MMC_CARD		0x184c
#define CLK_CON_DIV_CLKCMU_HSI_USB20DRD		0x1850
#define CLK_CON_DIV_CLKCMU_IS_BUS		0x1854
#define CLK_CON_DIV_CLKCMU_IS_GDC		0x1858
#define CLK_CON_DIV_CLKCMU_IS_ITP		0x185c
#define CLK_CON_DIV_CLKCMU_IS_VRA		0x1860
#define CLK_CON_DIV_CLKCMU_MFCMSCL_JPEG		0x1864
#define CLK_CON_DIV_CLKCMU_MFCMSCL_M2M		0x1868
#define CLK_CON_DIV_CLKCMU_MFCMSCL_MCSC		0x186c
#define CLK_CON_DIV_CLKCMU_MFCMSCL_MFC		0x1870
#define CLK_CON_DIV_CLKCMU_PERI_BUS		0x187c
#define CLK_CON_DIV_CLKCMU_PERI_IP		0x1880
#define CLK_CON_DIV_CLKCMU_PERI_UART		0x1884
#define CLK_CON_DIV_PLL_SHARED0_DIV2		0x188c
#define CLK_CON_DIV_PLL_SHARED0_DIV3		0x1890
#define CLK_CON_DIV_PLL_SHARED0_DIV4		0x1894
#define CLK_CON_DIV_PLL_SHARED1_DIV2		0x1898
#define CLK_CON_DIV_PLL_SHARED1_DIV3		0x189c
#define CLK_CON_DIV_PLL_SHARED1_DIV4		0x18a0
#define CLK_CON_GAT_GATE_CLKCMU_APM_BUS		0x2008
#define CLK_CON_GAT_GATE_CLKCMU_AUD		0x200c
#define CLK_CON_GAT_GATE_CLKCMU_CORE_BUS	0x201c
#define CLK_CON_GAT_GATE_CLKCMU_CORE_CCI	0x2020
#define CLK_CON_GAT_GATE_CLKCMU_CORE_MMC_EMBD	0x2024
#define CLK_CON_GAT_GATE_CLKCMU_CORE_SSS	0x2028
#define CLK_CON_GAT_GATE_CLKCMU_CPUCL0_DBG	0x202c
#define CLK_CON_GAT_GATE_CLKCMU_CPUCL0_SWITCH	0x2030
#define CLK_CON_GAT_GATE_CLKCMU_CPUCL1_DBG	0x2034
#define CLK_CON_GAT_GATE_CLKCMU_CPUCL1_SWITCH	0x2038
#define CLK_CON_GAT_GATE_CLKCMU_DPU		0x203c
#define CLK_CON_GAT_GATE_CLKCMU_G3D_SWITCH	0x2040
#define CLK_CON_GAT_GATE_CLKCMU_HSI_BUS		0x2044
#define CLK_CON_GAT_GATE_CLKCMU_HSI_MMC_CARD	0x2048
#define CLK_CON_GAT_GATE_CLKCMU_HSI_USB20DRD	0x204c
#define CLK_CON_GAT_GATE_CLKCMU_IS_BUS		0x2050
#define CLK_CON_GAT_GATE_CLKCMU_IS_GDC		0x2054
#define CLK_CON_GAT_GATE_CLKCMU_IS_ITP		0x2058
#define CLK_CON_GAT_GATE_CLKCMU_IS_VRA		0x205c
#define CLK_CON_GAT_GATE_CLKCMU_MFCMSCL_JPEG	0x2060
#define CLK_CON_GAT_GATE_CLKCMU_MFCMSCL_M2M	0x2064
#define CLK_CON_GAT_GATE_CLKCMU_MFCMSCL_MCSC	0x2068
#define CLK_CON_GAT_GATE_CLKCMU_MFCMSCL_MFC	0x206c
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
	CLK_CON_MUX_MUX_CLKCMU_APM_BUS,
	CLK_CON_MUX_MUX_CLKCMU_AUD,
	CLK_CON_MUX_MUX_CLKCMU_CORE_BUS,
	CLK_CON_MUX_MUX_CLKCMU_CORE_CCI,
	CLK_CON_MUX_MUX_CLKCMU_CORE_MMC_EMBD,
	CLK_CON_MUX_MUX_CLKCMU_CORE_SSS,
	CLK_CON_MUX_MUX_CLKCMU_CPUCL0_DBG,
	CLK_CON_MUX_MUX_CLKCMU_CPUCL0_SWITCH,
	CLK_CON_MUX_MUX_CLKCMU_CPUCL1_DBG,
	CLK_CON_MUX_MUX_CLKCMU_CPUCL1_SWITCH,
	CLK_CON_MUX_MUX_CLKCMU_DPU,
	CLK_CON_MUX_MUX_CLKCMU_G3D_SWITCH,
	CLK_CON_MUX_MUX_CLKCMU_HSI_BUS,
	CLK_CON_MUX_MUX_CLKCMU_HSI_MMC_CARD,
	CLK_CON_MUX_MUX_CLKCMU_HSI_USB20DRD,
	CLK_CON_MUX_MUX_CLKCMU_IS_BUS,
	CLK_CON_MUX_MUX_CLKCMU_IS_GDC,
	CLK_CON_MUX_MUX_CLKCMU_IS_ITP,
	CLK_CON_MUX_MUX_CLKCMU_IS_VRA,
	CLK_CON_MUX_MUX_CLKCMU_MFCMSCL_JPEG,
	CLK_CON_MUX_MUX_CLKCMU_MFCMSCL_M2M,
	CLK_CON_MUX_MUX_CLKCMU_MFCMSCL_MCSC,
	CLK_CON_MUX_MUX_CLKCMU_MFCMSCL_MFC,
	CLK_CON_MUX_MUX_CLKCMU_PERI_BUS,
	CLK_CON_MUX_MUX_CLKCMU_PERI_IP,
	CLK_CON_MUX_MUX_CLKCMU_PERI_UART,
	CLK_CON_DIV_CLKCMU_APM_BUS,
	CLK_CON_DIV_CLKCMU_AUD,
	CLK_CON_DIV_CLKCMU_CORE_BUS,
	CLK_CON_DIV_CLKCMU_CORE_CCI,
	CLK_CON_DIV_CLKCMU_CORE_MMC_EMBD,
	CLK_CON_DIV_CLKCMU_CORE_SSS,
	CLK_CON_DIV_CLKCMU_CPUCL0_DBG,
	CLK_CON_DIV_CLKCMU_CPUCL0_SWITCH,
	CLK_CON_DIV_CLKCMU_CPUCL1_DBG,
	CLK_CON_DIV_CLKCMU_CPUCL1_SWITCH,
	CLK_CON_DIV_CLKCMU_DPU,
	CLK_CON_DIV_CLKCMU_G3D_SWITCH,
	CLK_CON_DIV_CLKCMU_HSI_BUS,
	CLK_CON_DIV_CLKCMU_HSI_MMC_CARD,
	CLK_CON_DIV_CLKCMU_HSI_USB20DRD,
	CLK_CON_DIV_CLKCMU_IS_BUS,
	CLK_CON_DIV_CLKCMU_IS_GDC,
	CLK_CON_DIV_CLKCMU_IS_ITP,
	CLK_CON_DIV_CLKCMU_IS_VRA,
	CLK_CON_DIV_CLKCMU_MFCMSCL_JPEG,
	CLK_CON_DIV_CLKCMU_MFCMSCL_M2M,
	CLK_CON_DIV_CLKCMU_MFCMSCL_MCSC,
	CLK_CON_DIV_CLKCMU_MFCMSCL_MFC,
	CLK_CON_DIV_CLKCMU_PERI_BUS,
	CLK_CON_DIV_CLKCMU_PERI_IP,
	CLK_CON_DIV_CLKCMU_PERI_UART,
	CLK_CON_DIV_PLL_SHARED0_DIV2,
	CLK_CON_DIV_PLL_SHARED0_DIV3,
	CLK_CON_DIV_PLL_SHARED0_DIV4,
	CLK_CON_DIV_PLL_SHARED1_DIV2,
	CLK_CON_DIV_PLL_SHARED1_DIV3,
	CLK_CON_DIV_PLL_SHARED1_DIV4,
	CLK_CON_GAT_GATE_CLKCMU_APM_BUS,
	CLK_CON_GAT_GATE_CLKCMU_AUD,
	CLK_CON_GAT_GATE_CLKCMU_CORE_BUS,
	CLK_CON_GAT_GATE_CLKCMU_CORE_CCI,
	CLK_CON_GAT_GATE_CLKCMU_CORE_MMC_EMBD,
	CLK_CON_GAT_GATE_CLKCMU_CORE_SSS,
	CLK_CON_GAT_GATE_CLKCMU_CPUCL0_DBG,
	CLK_CON_GAT_GATE_CLKCMU_CPUCL0_SWITCH,
	CLK_CON_GAT_GATE_CLKCMU_CPUCL1_DBG,
	CLK_CON_GAT_GATE_CLKCMU_CPUCL1_SWITCH,
	CLK_CON_GAT_GATE_CLKCMU_DPU,
	CLK_CON_GAT_GATE_CLKCMU_G3D_SWITCH,
	CLK_CON_GAT_GATE_CLKCMU_HSI_BUS,
	CLK_CON_GAT_GATE_CLKCMU_HSI_MMC_CARD,
	CLK_CON_GAT_GATE_CLKCMU_HSI_USB20DRD,
	CLK_CON_GAT_GATE_CLKCMU_IS_BUS,
	CLK_CON_GAT_GATE_CLKCMU_IS_GDC,
	CLK_CON_GAT_GATE_CLKCMU_IS_ITP,
	CLK_CON_GAT_GATE_CLKCMU_IS_VRA,
	CLK_CON_GAT_GATE_CLKCMU_MFCMSCL_JPEG,
	CLK_CON_GAT_GATE_CLKCMU_MFCMSCL_M2M,
	CLK_CON_GAT_GATE_CLKCMU_MFCMSCL_MCSC,
	CLK_CON_GAT_GATE_CLKCMU_MFCMSCL_MFC,
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
/* List of parent clocks for Muxes in CMU_TOP: for CMU_APM */
PNAME(mout_clkcmu_apm_bus_p)	= { "dout_shared0_div4", "pll_shared1_div4" };
/* List of parent clocks for Muxes in CMU_TOP: for CMU_AUD */
PNAME(mout_aud_p)		= { "fout_shared1_pll", "dout_shared0_div2",
				    "dout_shared1_div2", "dout_shared0_div3" };
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
/* List of parent clocks for Muxes in CMU_TOP: for CMU_CPUCL0 */
PNAME(mout_cpucl0_switch_p)	= { "fout_shared0_pll", "fout_shared1_pll",
				    "dout_shared0_div2", "dout_shared1_div2" };
PNAME(mout_cpucl0_dbg_p)	= { "dout_shared0_div4", "dout_shared1_div4" };
/* List of parent clocks for Muxes in CMU_TOP: for CMU_CPUCL1 */
PNAME(mout_cpucl1_switch_p)	= { "fout_shared0_pll", "fout_shared1_pll",
				    "dout_shared0_div2", "dout_shared1_div2" };
PNAME(mout_cpucl1_dbg_p)	= { "dout_shared0_div4", "dout_shared1_div4" };
/* List of parent clocks for Muxes in CMU_TOP: for CMU_G3D */
PNAME(mout_g3d_switch_p)	= { "dout_shared0_div2", "dout_shared1_div2",
				    "dout_shared0_div3", "dout_shared1_div3" };
/* List of parent clocks for Muxes in CMU_TOP: for CMU_HSI */
PNAME(mout_hsi_bus_p)		= { "dout_shared0_div2", "dout_shared1_div2" };
PNAME(mout_hsi_mmc_card_p)	= { "oscclk", "dout_shared0_div2",
				    "dout_shared1_div2", "dout_shared0_div3",
				    "dout_shared1_div3", "mout_mmc_pll",
				    "oscclk", "oscclk" };
PNAME(mout_hsi_usb20drd_p)	= { "oscclk", "dout_shared0_div4",
				    "dout_shared1_div4", "oscclk" };
/* List of parent clocks for Muxes in CMU_TOP: for CMU_IS */
PNAME(mout_is_bus_p)		= { "dout_shared0_div2", "dout_shared1_div2",
				    "dout_shared0_div3", "dout_shared1_div3" };
PNAME(mout_is_itp_p)		= { "dout_shared0_div2", "dout_shared1_div2",
				    "dout_shared0_div3", "dout_shared1_div3" };
PNAME(mout_is_vra_p)		= { "dout_shared0_div2", "dout_shared1_div2",
				    "dout_shared0_div3", "dout_shared1_div3" };
PNAME(mout_is_gdc_p)		= { "dout_shared0_div2", "dout_shared1_div2",
				    "dout_shared0_div3", "dout_shared1_div3" };
/* List of parent clocks for Muxes in CMU_TOP: for CMU_MFCMSCL */
PNAME(mout_mfcmscl_mfc_p)	= { "dout_shared1_div2", "dout_shared0_div3",
				    "dout_shared1_div3", "dout_shared0_div4" };
PNAME(mout_mfcmscl_m2m_p)	= { "dout_shared1_div2", "dout_shared0_div3",
				    "dout_shared1_div3", "dout_shared0_div4" };
PNAME(mout_mfcmscl_mcsc_p)	= { "dout_shared1_div2", "dout_shared0_div3",
				    "dout_shared1_div3", "dout_shared0_div4" };
PNAME(mout_mfcmscl_jpeg_p)	= { "dout_shared0_div3", "dout_shared1_div3",
				    "dout_shared0_div4", "dout_shared1_div4" };
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

	/* APM */
	MUX(CLK_MOUT_CLKCMU_APM_BUS, "mout_clkcmu_apm_bus",
	    mout_clkcmu_apm_bus_p, CLK_CON_MUX_MUX_CLKCMU_APM_BUS, 0, 1),

	/* AUD */
	MUX(CLK_MOUT_AUD, "mout_aud", mout_aud_p,
	    CLK_CON_MUX_MUX_CLKCMU_AUD, 0, 2),

	/* CORE */
	MUX(CLK_MOUT_CORE_BUS, "mout_core_bus", mout_core_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_CORE_BUS, 0, 2),
	MUX(CLK_MOUT_CORE_CCI, "mout_core_cci", mout_core_cci_p,
	    CLK_CON_MUX_MUX_CLKCMU_CORE_CCI, 0, 2),
	MUX(CLK_MOUT_CORE_MMC_EMBD, "mout_core_mmc_embd", mout_core_mmc_embd_p,
	    CLK_CON_MUX_MUX_CLKCMU_CORE_MMC_EMBD, 0, 3),
	MUX(CLK_MOUT_CORE_SSS, "mout_core_sss", mout_core_sss_p,
	    CLK_CON_MUX_MUX_CLKCMU_CORE_SSS, 0, 2),

	/* CPUCL0 */
	MUX(CLK_MOUT_CPUCL0_DBG, "mout_cpucl0_dbg", mout_cpucl0_dbg_p,
	    CLK_CON_MUX_MUX_CLKCMU_CPUCL0_DBG, 0, 1),
	MUX(CLK_MOUT_CPUCL0_SWITCH, "mout_cpucl0_switch", mout_cpucl0_switch_p,
	    CLK_CON_MUX_MUX_CLKCMU_CPUCL0_SWITCH, 0, 2),

	/* CPUCL1 */
	MUX(CLK_MOUT_CPUCL1_DBG, "mout_cpucl1_dbg", mout_cpucl1_dbg_p,
	    CLK_CON_MUX_MUX_CLKCMU_CPUCL1_DBG, 0, 1),
	MUX(CLK_MOUT_CPUCL1_SWITCH, "mout_cpucl1_switch", mout_cpucl1_switch_p,
	    CLK_CON_MUX_MUX_CLKCMU_CPUCL1_SWITCH, 0, 2),

	/* DPU */
	MUX(CLK_MOUT_DPU, "mout_dpu", mout_dpu_p,
	    CLK_CON_MUX_MUX_CLKCMU_DPU, 0, 2),

	/* G3D */
	MUX(CLK_MOUT_G3D_SWITCH, "mout_g3d_switch", mout_g3d_switch_p,
	    CLK_CON_MUX_MUX_CLKCMU_G3D_SWITCH, 0, 2),

	/* HSI */
	MUX(CLK_MOUT_HSI_BUS, "mout_hsi_bus", mout_hsi_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_HSI_BUS, 0, 1),
	MUX(CLK_MOUT_HSI_MMC_CARD, "mout_hsi_mmc_card", mout_hsi_mmc_card_p,
	    CLK_CON_MUX_MUX_CLKCMU_HSI_MMC_CARD, 0, 3),
	MUX(CLK_MOUT_HSI_USB20DRD, "mout_hsi_usb20drd", mout_hsi_usb20drd_p,
	    CLK_CON_MUX_MUX_CLKCMU_HSI_USB20DRD, 0, 2),

	/* IS */
	MUX(CLK_MOUT_IS_BUS, "mout_is_bus", mout_is_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_IS_BUS, 0, 2),
	MUX(CLK_MOUT_IS_ITP, "mout_is_itp", mout_is_itp_p,
	    CLK_CON_MUX_MUX_CLKCMU_IS_ITP, 0, 2),
	MUX(CLK_MOUT_IS_VRA, "mout_is_vra", mout_is_vra_p,
	    CLK_CON_MUX_MUX_CLKCMU_IS_VRA, 0, 2),
	MUX(CLK_MOUT_IS_GDC, "mout_is_gdc", mout_is_gdc_p,
	    CLK_CON_MUX_MUX_CLKCMU_IS_GDC, 0, 2),

	/* MFCMSCL */
	MUX(CLK_MOUT_MFCMSCL_MFC, "mout_mfcmscl_mfc", mout_mfcmscl_mfc_p,
	    CLK_CON_MUX_MUX_CLKCMU_MFCMSCL_MFC, 0, 2),
	MUX(CLK_MOUT_MFCMSCL_M2M, "mout_mfcmscl_m2m", mout_mfcmscl_m2m_p,
	    CLK_CON_MUX_MUX_CLKCMU_MFCMSCL_M2M, 0, 2),
	MUX(CLK_MOUT_MFCMSCL_MCSC, "mout_mfcmscl_mcsc", mout_mfcmscl_mcsc_p,
	    CLK_CON_MUX_MUX_CLKCMU_MFCMSCL_MCSC, 0, 2),
	MUX(CLK_MOUT_MFCMSCL_JPEG, "mout_mfcmscl_jpeg", mout_mfcmscl_jpeg_p,
	    CLK_CON_MUX_MUX_CLKCMU_MFCMSCL_JPEG, 0, 2),

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

	/* APM */
	DIV(CLK_DOUT_CLKCMU_APM_BUS, "dout_clkcmu_apm_bus",
	    "gout_clkcmu_apm_bus", CLK_CON_DIV_CLKCMU_APM_BUS, 0, 3),

	/* AUD */
	DIV(CLK_DOUT_AUD, "dout_aud", "gout_aud",
	    CLK_CON_DIV_CLKCMU_AUD, 0, 4),

	/* CORE */
	DIV(CLK_DOUT_CORE_BUS, "dout_core_bus", "gout_core_bus",
	    CLK_CON_DIV_CLKCMU_CORE_BUS, 0, 4),
	DIV(CLK_DOUT_CORE_CCI, "dout_core_cci", "gout_core_cci",
	    CLK_CON_DIV_CLKCMU_CORE_CCI, 0, 4),
	DIV(CLK_DOUT_CORE_MMC_EMBD, "dout_core_mmc_embd", "gout_core_mmc_embd",
	    CLK_CON_DIV_CLKCMU_CORE_MMC_EMBD, 0, 9),
	DIV(CLK_DOUT_CORE_SSS, "dout_core_sss", "gout_core_sss",
	    CLK_CON_DIV_CLKCMU_CORE_SSS, 0, 4),

	/* CPUCL0 */
	DIV(CLK_DOUT_CPUCL0_DBG, "dout_cpucl0_dbg", "gout_cpucl0_dbg",
	    CLK_CON_DIV_CLKCMU_CPUCL0_DBG, 0, 3),
	DIV(CLK_DOUT_CPUCL0_SWITCH, "dout_cpucl0_switch", "gout_cpucl0_switch",
	    CLK_CON_DIV_CLKCMU_CPUCL0_SWITCH, 0, 3),

	/* CPUCL1 */
	DIV(CLK_DOUT_CPUCL1_DBG, "dout_cpucl1_dbg", "gout_cpucl1_dbg",
	    CLK_CON_DIV_CLKCMU_CPUCL1_DBG, 0, 3),
	DIV(CLK_DOUT_CPUCL1_SWITCH, "dout_cpucl1_switch", "gout_cpucl1_switch",
	    CLK_CON_DIV_CLKCMU_CPUCL1_SWITCH, 0, 3),

	/* DPU */
	DIV(CLK_DOUT_DPU, "dout_dpu", "gout_dpu",
	    CLK_CON_DIV_CLKCMU_DPU, 0, 4),

	/* G3D */
	DIV(CLK_DOUT_G3D_SWITCH, "dout_g3d_switch", "gout_g3d_switch",
	    CLK_CON_DIV_CLKCMU_G3D_SWITCH, 0, 3),

	/* HSI */
	DIV(CLK_DOUT_HSI_BUS, "dout_hsi_bus", "gout_hsi_bus",
	    CLK_CON_DIV_CLKCMU_HSI_BUS, 0, 4),
	DIV(CLK_DOUT_HSI_MMC_CARD, "dout_hsi_mmc_card", "gout_hsi_mmc_card",
	    CLK_CON_DIV_CLKCMU_HSI_MMC_CARD, 0, 9),
	DIV(CLK_DOUT_HSI_USB20DRD, "dout_hsi_usb20drd", "gout_hsi_usb20drd",
	    CLK_CON_DIV_CLKCMU_HSI_USB20DRD, 0, 4),

	/* IS */
	DIV(CLK_DOUT_IS_BUS, "dout_is_bus", "gout_is_bus",
	    CLK_CON_DIV_CLKCMU_IS_BUS, 0, 4),
	DIV(CLK_DOUT_IS_ITP, "dout_is_itp", "gout_is_itp",
	    CLK_CON_DIV_CLKCMU_IS_ITP, 0, 4),
	DIV(CLK_DOUT_IS_VRA, "dout_is_vra", "gout_is_vra",
	    CLK_CON_DIV_CLKCMU_IS_VRA, 0, 4),
	DIV(CLK_DOUT_IS_GDC, "dout_is_gdc", "gout_is_gdc",
	    CLK_CON_DIV_CLKCMU_IS_GDC, 0, 4),

	/* MFCMSCL */
	DIV(CLK_DOUT_MFCMSCL_MFC, "dout_mfcmscl_mfc", "gout_mfcmscl_mfc",
	    CLK_CON_DIV_CLKCMU_MFCMSCL_MFC, 0, 4),
	DIV(CLK_DOUT_MFCMSCL_M2M, "dout_mfcmscl_m2m", "gout_mfcmscl_m2m",
	    CLK_CON_DIV_CLKCMU_MFCMSCL_M2M, 0, 4),
	DIV(CLK_DOUT_MFCMSCL_MCSC, "dout_mfcmscl_mcsc", "gout_mfcmscl_mcsc",
	    CLK_CON_DIV_CLKCMU_MFCMSCL_MCSC, 0, 4),
	DIV(CLK_DOUT_MFCMSCL_JPEG, "dout_mfcmscl_jpeg", "gout_mfcmscl_jpeg",
	    CLK_CON_DIV_CLKCMU_MFCMSCL_JPEG, 0, 4),

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

	/* APM */
	GATE(CLK_GOUT_CLKCMU_APM_BUS, "gout_clkcmu_apm_bus",
	     "mout_clkcmu_apm_bus", CLK_CON_GAT_GATE_CLKCMU_APM_BUS, 21, 0, 0),

	/* AUD */
	GATE(CLK_GOUT_AUD, "gout_aud", "mout_aud",
	     CLK_CON_GAT_GATE_CLKCMU_AUD, 21, 0, 0),

	/* CPUCL0 */
	GATE(CLK_GOUT_CPUCL0_DBG, "gout_cpucl0_dbg", "mout_cpucl0_dbg",
	     CLK_CON_GAT_GATE_CLKCMU_CPUCL0_DBG, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL0_SWITCH, "gout_cpucl0_switch", "mout_cpucl0_switch",
	     CLK_CON_GAT_GATE_CLKCMU_CPUCL0_SWITCH, 21, 0, 0),

	/* CPUCL1 */
	GATE(CLK_GOUT_CPUCL1_DBG, "gout_cpucl1_dbg", "mout_cpucl1_dbg",
	     CLK_CON_GAT_GATE_CLKCMU_CPUCL1_DBG, 21, 0, 0),
	GATE(CLK_GOUT_CPUCL1_SWITCH, "gout_cpucl1_switch", "mout_cpucl1_switch",
	     CLK_CON_GAT_GATE_CLKCMU_CPUCL1_SWITCH, 21, 0, 0),

	/* DPU */
	GATE(CLK_GOUT_DPU, "gout_dpu", "mout_dpu",
	     CLK_CON_GAT_GATE_CLKCMU_DPU, 21, 0, 0),

	/* G3D */
	GATE(CLK_GOUT_G3D_SWITCH, "gout_g3d_switch", "mout_g3d_switch",
	     CLK_CON_GAT_GATE_CLKCMU_G3D_SWITCH, 21, 0, 0),

	/* HSI */
	GATE(CLK_GOUT_HSI_BUS, "gout_hsi_bus", "mout_hsi_bus",
	     CLK_CON_GAT_GATE_CLKCMU_HSI_BUS, 21, 0, 0),
	GATE(CLK_GOUT_HSI_MMC_CARD, "gout_hsi_mmc_card", "mout_hsi_mmc_card",
	     CLK_CON_GAT_GATE_CLKCMU_HSI_MMC_CARD, 21, 0, 0),
	GATE(CLK_GOUT_HSI_USB20DRD, "gout_hsi_usb20drd", "mout_hsi_usb20drd",
	     CLK_CON_GAT_GATE_CLKCMU_HSI_USB20DRD, 21, 0, 0),

	/* IS */
	/* TODO: These clocks have to be always enabled to access CMU_IS regs */
	GATE(CLK_GOUT_IS_BUS, "gout_is_bus", "mout_is_bus",
	     CLK_CON_GAT_GATE_CLKCMU_IS_BUS, 21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_IS_ITP, "gout_is_itp", "mout_is_itp",
	     CLK_CON_GAT_GATE_CLKCMU_IS_ITP, 21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_IS_VRA, "gout_is_vra", "mout_is_vra",
	     CLK_CON_GAT_GATE_CLKCMU_IS_VRA, 21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_IS_GDC, "gout_is_gdc", "mout_is_gdc",
	     CLK_CON_GAT_GATE_CLKCMU_IS_GDC, 21, CLK_IS_CRITICAL, 0),

	/* MFCMSCL */
	/* TODO: These have to be always enabled to access CMU_MFCMSCL regs */
	GATE(CLK_GOUT_MFCMSCL_MFC, "gout_mfcmscl_mfc", "mout_mfcmscl_mfc",
	     CLK_CON_GAT_GATE_CLKCMU_MFCMSCL_MFC, 21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_MFCMSCL_M2M, "gout_mfcmscl_m2m", "mout_mfcmscl_m2m",
	     CLK_CON_GAT_GATE_CLKCMU_MFCMSCL_M2M, 21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_MFCMSCL_MCSC, "gout_mfcmscl_mcsc", "mout_mfcmscl_mcsc",
	     CLK_CON_GAT_GATE_CLKCMU_MFCMSCL_MCSC, 21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_MFCMSCL_JPEG, "gout_mfcmscl_jpeg", "mout_mfcmscl_jpeg",
	     CLK_CON_GAT_GATE_CLKCMU_MFCMSCL_JPEG, 21, CLK_IS_CRITICAL, 0),

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
	.nr_clk_ids		= CLKS_NR_TOP,
	.clk_regs		= top_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(top_clk_regs),
};

static void __init exynos850_cmu_top_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &top_cmu_info);
}

/* Register CMU_TOP early, as it's a dependency for other early domains */
CLK_OF_DECLARE(exynos850_cmu_top, "samsung,exynos850-cmu-top",
	       exynos850_cmu_top_init);

/* ---- CMU_APM ------------------------------------------------------------- */

/* Register Offset definitions for CMU_APM (0x11800000) */
#define PLL_CON0_MUX_CLKCMU_APM_BUS_USER		0x0600
#define PLL_CON0_MUX_CLK_RCO_APM_I3C_USER		0x0610
#define PLL_CON0_MUX_CLK_RCO_APM_USER			0x0620
#define PLL_CON0_MUX_DLL_USER				0x0630
#define CLK_CON_MUX_MUX_CLKCMU_CHUB_BUS			0x1000
#define CLK_CON_MUX_MUX_CLK_APM_BUS			0x1004
#define CLK_CON_MUX_MUX_CLK_APM_I3C			0x1008
#define CLK_CON_DIV_CLKCMU_CHUB_BUS			0x1800
#define CLK_CON_DIV_DIV_CLK_APM_BUS			0x1804
#define CLK_CON_DIV_DIV_CLK_APM_I3C			0x1808
#define CLK_CON_GAT_CLKCMU_CMGP_BUS			0x2000
#define CLK_CON_GAT_GATE_CLKCMU_CHUB_BUS		0x2014
#define CLK_CON_GAT_GOUT_APM_APBIF_GPIO_ALIVE_PCLK	0x2018
#define CLK_CON_GAT_GOUT_APM_APBIF_PMU_ALIVE_PCLK	0x2020
#define CLK_CON_GAT_GOUT_APM_APBIF_RTC_PCLK		0x2024
#define CLK_CON_GAT_GOUT_APM_APBIF_TOP_RTC_PCLK		0x2028
#define CLK_CON_GAT_GOUT_APM_I3C_APM_PMIC_I_PCLK	0x2034
#define CLK_CON_GAT_GOUT_APM_I3C_APM_PMIC_I_SCLK	0x2038
#define CLK_CON_GAT_GOUT_APM_SPEEDY_APM_PCLK		0x20bc
#define CLK_CON_GAT_GOUT_APM_SYSREG_APM_PCLK		0x20c0

static const unsigned long apm_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_APM_BUS_USER,
	PLL_CON0_MUX_CLK_RCO_APM_I3C_USER,
	PLL_CON0_MUX_CLK_RCO_APM_USER,
	PLL_CON0_MUX_DLL_USER,
	CLK_CON_MUX_MUX_CLKCMU_CHUB_BUS,
	CLK_CON_MUX_MUX_CLK_APM_BUS,
	CLK_CON_MUX_MUX_CLK_APM_I3C,
	CLK_CON_DIV_CLKCMU_CHUB_BUS,
	CLK_CON_DIV_DIV_CLK_APM_BUS,
	CLK_CON_DIV_DIV_CLK_APM_I3C,
	CLK_CON_GAT_CLKCMU_CMGP_BUS,
	CLK_CON_GAT_GATE_CLKCMU_CHUB_BUS,
	CLK_CON_GAT_GOUT_APM_APBIF_GPIO_ALIVE_PCLK,
	CLK_CON_GAT_GOUT_APM_APBIF_PMU_ALIVE_PCLK,
	CLK_CON_GAT_GOUT_APM_APBIF_RTC_PCLK,
	CLK_CON_GAT_GOUT_APM_APBIF_TOP_RTC_PCLK,
	CLK_CON_GAT_GOUT_APM_I3C_APM_PMIC_I_PCLK,
	CLK_CON_GAT_GOUT_APM_I3C_APM_PMIC_I_SCLK,
	CLK_CON_GAT_GOUT_APM_SPEEDY_APM_PCLK,
	CLK_CON_GAT_GOUT_APM_SYSREG_APM_PCLK,
};

/* List of parent clocks for Muxes in CMU_APM */
PNAME(mout_apm_bus_user_p)	= { "oscclk_rco_apm", "dout_clkcmu_apm_bus" };
PNAME(mout_rco_apm_i3c_user_p)	= { "oscclk_rco_apm", "clk_rco_i3c_pmic" };
PNAME(mout_rco_apm_user_p)	= { "oscclk_rco_apm", "clk_rco_apm__alv" };
PNAME(mout_dll_user_p)		= { "oscclk_rco_apm", "clk_dll_dco" };
PNAME(mout_clkcmu_chub_bus_p)	= { "mout_apm_bus_user", "mout_dll_user" };
PNAME(mout_apm_bus_p)		= { "mout_rco_apm_user", "mout_apm_bus_user",
				    "mout_dll_user", "oscclk_rco_apm" };
PNAME(mout_apm_i3c_p)		= { "dout_apm_i3c", "mout_rco_apm_i3c_user" };

static const struct samsung_fixed_rate_clock apm_fixed_clks[] __initconst = {
	FRATE(CLK_RCO_I3C_PMIC, "clk_rco_i3c_pmic", NULL, 0, 491520000),
	FRATE(OSCCLK_RCO_APM, "oscclk_rco_apm", NULL, 0, 24576000),
	FRATE(CLK_RCO_APM__ALV, "clk_rco_apm__alv", NULL, 0, 49152000),
	FRATE(CLK_DLL_DCO, "clk_dll_dco", NULL, 0, 360000000),
};

static const struct samsung_mux_clock apm_mux_clks[] __initconst = {
	MUX(CLK_MOUT_APM_BUS_USER, "mout_apm_bus_user", mout_apm_bus_user_p,
	    PLL_CON0_MUX_CLKCMU_APM_BUS_USER, 4, 1),
	MUX(CLK_MOUT_RCO_APM_I3C_USER, "mout_rco_apm_i3c_user",
	    mout_rco_apm_i3c_user_p, PLL_CON0_MUX_CLK_RCO_APM_I3C_USER, 4, 1),
	MUX(CLK_MOUT_RCO_APM_USER, "mout_rco_apm_user", mout_rco_apm_user_p,
	    PLL_CON0_MUX_CLK_RCO_APM_USER, 4, 1),
	MUX(CLK_MOUT_DLL_USER, "mout_dll_user", mout_dll_user_p,
	    PLL_CON0_MUX_DLL_USER, 4, 1),
	MUX(CLK_MOUT_CLKCMU_CHUB_BUS, "mout_clkcmu_chub_bus",
	    mout_clkcmu_chub_bus_p, CLK_CON_MUX_MUX_CLKCMU_CHUB_BUS, 0, 1),
	MUX(CLK_MOUT_APM_BUS, "mout_apm_bus", mout_apm_bus_p,
	    CLK_CON_MUX_MUX_CLK_APM_BUS, 0, 2),
	MUX(CLK_MOUT_APM_I3C, "mout_apm_i3c", mout_apm_i3c_p,
	    CLK_CON_MUX_MUX_CLK_APM_I3C, 0, 1),
};

static const struct samsung_div_clock apm_div_clks[] __initconst = {
	DIV(CLK_DOUT_CLKCMU_CHUB_BUS, "dout_clkcmu_chub_bus",
	    "gout_clkcmu_chub_bus",
	    CLK_CON_DIV_CLKCMU_CHUB_BUS, 0, 3),
	DIV(CLK_DOUT_APM_BUS, "dout_apm_bus", "mout_apm_bus",
	    CLK_CON_DIV_DIV_CLK_APM_BUS, 0, 3),
	DIV(CLK_DOUT_APM_I3C, "dout_apm_i3c", "mout_apm_bus",
	    CLK_CON_DIV_DIV_CLK_APM_I3C, 0, 3),
};

static const struct samsung_gate_clock apm_gate_clks[] __initconst = {
	GATE(CLK_GOUT_CLKCMU_CMGP_BUS, "gout_clkcmu_cmgp_bus", "dout_apm_bus",
	     CLK_CON_GAT_CLKCMU_CMGP_BUS, 21, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_CLKCMU_CHUB_BUS, "gout_clkcmu_chub_bus",
	     "mout_clkcmu_chub_bus",
	     CLK_CON_GAT_GATE_CLKCMU_CHUB_BUS, 21, 0, 0),
	GATE(CLK_GOUT_RTC_PCLK, "gout_rtc_pclk", "dout_apm_bus",
	     CLK_CON_GAT_GOUT_APM_APBIF_RTC_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_TOP_RTC_PCLK, "gout_top_rtc_pclk", "dout_apm_bus",
	     CLK_CON_GAT_GOUT_APM_APBIF_TOP_RTC_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_I3C_PCLK, "gout_i3c_pclk", "dout_apm_bus",
	     CLK_CON_GAT_GOUT_APM_I3C_APM_PMIC_I_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_I3C_SCLK, "gout_i3c_sclk", "mout_apm_i3c",
	     CLK_CON_GAT_GOUT_APM_I3C_APM_PMIC_I_SCLK, 21, 0, 0),
	GATE(CLK_GOUT_SPEEDY_PCLK, "gout_speedy_pclk", "dout_apm_bus",
	     CLK_CON_GAT_GOUT_APM_SPEEDY_APM_PCLK, 21, 0, 0),
	/* TODO: Should be enabled in GPIO driver (or made CLK_IS_CRITICAL) */
	GATE(CLK_GOUT_GPIO_ALIVE_PCLK, "gout_gpio_alive_pclk", "dout_apm_bus",
	     CLK_CON_GAT_GOUT_APM_APBIF_GPIO_ALIVE_PCLK, 21, CLK_IGNORE_UNUSED,
	     0),
	GATE(CLK_GOUT_PMU_ALIVE_PCLK, "gout_pmu_alive_pclk", "dout_apm_bus",
	     CLK_CON_GAT_GOUT_APM_APBIF_PMU_ALIVE_PCLK, 21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_SYSREG_APM_PCLK, "gout_sysreg_apm_pclk", "dout_apm_bus",
	     CLK_CON_GAT_GOUT_APM_SYSREG_APM_PCLK, 21, 0, 0),
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
	.clk_name		= "dout_clkcmu_apm_bus",
};

/* ---- CMU_AUD ------------------------------------------------------------- */

#define PLL_LOCKTIME_PLL_AUD			0x0000
#define PLL_CON0_PLL_AUD			0x0100
#define PLL_CON3_PLL_AUD			0x010c
#define PLL_CON0_MUX_CLKCMU_AUD_CPU_USER	0x0600
#define PLL_CON0_MUX_TICK_USB_USER		0x0610
#define CLK_CON_MUX_MUX_CLK_AUD_CPU		0x1000
#define CLK_CON_MUX_MUX_CLK_AUD_CPU_HCH		0x1004
#define CLK_CON_MUX_MUX_CLK_AUD_FM		0x1008
#define CLK_CON_MUX_MUX_CLK_AUD_UAIF0		0x100c
#define CLK_CON_MUX_MUX_CLK_AUD_UAIF1		0x1010
#define CLK_CON_MUX_MUX_CLK_AUD_UAIF2		0x1014
#define CLK_CON_MUX_MUX_CLK_AUD_UAIF3		0x1018
#define CLK_CON_MUX_MUX_CLK_AUD_UAIF4		0x101c
#define CLK_CON_MUX_MUX_CLK_AUD_UAIF5		0x1020
#define CLK_CON_MUX_MUX_CLK_AUD_UAIF6		0x1024
#define CLK_CON_DIV_DIV_CLK_AUD_MCLK		0x1800
#define CLK_CON_DIV_DIV_CLK_AUD_AUDIF		0x1804
#define CLK_CON_DIV_DIV_CLK_AUD_BUSD		0x1808
#define CLK_CON_DIV_DIV_CLK_AUD_BUSP		0x180c
#define CLK_CON_DIV_DIV_CLK_AUD_CNT		0x1810
#define CLK_CON_DIV_DIV_CLK_AUD_CPU		0x1814
#define CLK_CON_DIV_DIV_CLK_AUD_CPU_ACLK	0x1818
#define CLK_CON_DIV_DIV_CLK_AUD_CPU_PCLKDBG	0x181c
#define CLK_CON_DIV_DIV_CLK_AUD_FM		0x1820
#define CLK_CON_DIV_DIV_CLK_AUD_FM_SPDY		0x1824
#define CLK_CON_DIV_DIV_CLK_AUD_UAIF0		0x1828
#define CLK_CON_DIV_DIV_CLK_AUD_UAIF1		0x182c
#define CLK_CON_DIV_DIV_CLK_AUD_UAIF2		0x1830
#define CLK_CON_DIV_DIV_CLK_AUD_UAIF3		0x1834
#define CLK_CON_DIV_DIV_CLK_AUD_UAIF4		0x1838
#define CLK_CON_DIV_DIV_CLK_AUD_UAIF5		0x183c
#define CLK_CON_DIV_DIV_CLK_AUD_UAIF6		0x1840
#define CLK_CON_GAT_GOUT_AUD_ABOX_BCLK_CNT	0x2000
#define CLK_CON_GAT_GOUT_AUD_ABOX_BCLK_UAIF0	0x2004
#define CLK_CON_GAT_GOUT_AUD_ABOX_BCLK_UAIF1	0x2008
#define CLK_CON_GAT_GOUT_AUD_ABOX_BCLK_UAIF2	0x200c
#define CLK_CON_GAT_GOUT_AUD_ABOX_BCLK_UAIF3	0x2010
#define CLK_CON_GAT_GOUT_AUD_ABOX_BCLK_UAIF4	0x2014
#define CLK_CON_GAT_GOUT_AUD_ABOX_BCLK_UAIF5	0x2018
#define CLK_CON_GAT_GOUT_AUD_ABOX_BCLK_UAIF6	0x201c
#define CLK_CON_GAT_CLK_AUD_CMU_AUD_PCLK	0x2020
#define CLK_CON_GAT_GOUT_AUD_ABOX_ACLK		0x2048
#define CLK_CON_GAT_GOUT_AUD_ABOX_BCLK_SPDY	0x204c
#define CLK_CON_GAT_GOUT_AUD_ABOX_CCLK_ASB	0x2050
#define CLK_CON_GAT_GOUT_AUD_ABOX_CCLK_CA32	0x2054
#define CLK_CON_GAT_GOUT_AUD_ABOX_CCLK_DAP	0x2058
#define CLK_CON_GAT_GOUT_AUD_CODEC_MCLK		0x206c
#define CLK_CON_GAT_GOUT_AUD_TZPC_PCLK		0x2070
#define CLK_CON_GAT_GOUT_AUD_GPIO_PCLK		0x2074
#define CLK_CON_GAT_GOUT_AUD_PPMU_ACLK		0x2088
#define CLK_CON_GAT_GOUT_AUD_PPMU_PCLK		0x208c
#define CLK_CON_GAT_GOUT_AUD_SYSMMU_CLK_S1	0x20b4
#define CLK_CON_GAT_GOUT_AUD_SYSREG_PCLK	0x20b8
#define CLK_CON_GAT_GOUT_AUD_WDT_PCLK		0x20bc

static const unsigned long aud_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_AUD,
	PLL_CON0_PLL_AUD,
	PLL_CON3_PLL_AUD,
	PLL_CON0_MUX_CLKCMU_AUD_CPU_USER,
	PLL_CON0_MUX_TICK_USB_USER,
	CLK_CON_MUX_MUX_CLK_AUD_CPU,
	CLK_CON_MUX_MUX_CLK_AUD_CPU_HCH,
	CLK_CON_MUX_MUX_CLK_AUD_FM,
	CLK_CON_MUX_MUX_CLK_AUD_UAIF0,
	CLK_CON_MUX_MUX_CLK_AUD_UAIF1,
	CLK_CON_MUX_MUX_CLK_AUD_UAIF2,
	CLK_CON_MUX_MUX_CLK_AUD_UAIF3,
	CLK_CON_MUX_MUX_CLK_AUD_UAIF4,
	CLK_CON_MUX_MUX_CLK_AUD_UAIF5,
	CLK_CON_MUX_MUX_CLK_AUD_UAIF6,
	CLK_CON_DIV_DIV_CLK_AUD_MCLK,
	CLK_CON_DIV_DIV_CLK_AUD_AUDIF,
	CLK_CON_DIV_DIV_CLK_AUD_BUSD,
	CLK_CON_DIV_DIV_CLK_AUD_BUSP,
	CLK_CON_DIV_DIV_CLK_AUD_CNT,
	CLK_CON_DIV_DIV_CLK_AUD_CPU,
	CLK_CON_DIV_DIV_CLK_AUD_CPU_ACLK,
	CLK_CON_DIV_DIV_CLK_AUD_CPU_PCLKDBG,
	CLK_CON_DIV_DIV_CLK_AUD_FM,
	CLK_CON_DIV_DIV_CLK_AUD_FM_SPDY,
	CLK_CON_DIV_DIV_CLK_AUD_UAIF0,
	CLK_CON_DIV_DIV_CLK_AUD_UAIF1,
	CLK_CON_DIV_DIV_CLK_AUD_UAIF2,
	CLK_CON_DIV_DIV_CLK_AUD_UAIF3,
	CLK_CON_DIV_DIV_CLK_AUD_UAIF4,
	CLK_CON_DIV_DIV_CLK_AUD_UAIF5,
	CLK_CON_DIV_DIV_CLK_AUD_UAIF6,
	CLK_CON_GAT_GOUT_AUD_ABOX_BCLK_CNT,
	CLK_CON_GAT_GOUT_AUD_ABOX_BCLK_UAIF0,
	CLK_CON_GAT_GOUT_AUD_ABOX_BCLK_UAIF1,
	CLK_CON_GAT_GOUT_AUD_ABOX_BCLK_UAIF2,
	CLK_CON_GAT_GOUT_AUD_ABOX_BCLK_UAIF3,
	CLK_CON_GAT_GOUT_AUD_ABOX_BCLK_UAIF4,
	CLK_CON_GAT_GOUT_AUD_ABOX_BCLK_UAIF5,
	CLK_CON_GAT_GOUT_AUD_ABOX_BCLK_UAIF6,
	CLK_CON_GAT_CLK_AUD_CMU_AUD_PCLK,
	CLK_CON_GAT_GOUT_AUD_ABOX_ACLK,
	CLK_CON_GAT_GOUT_AUD_ABOX_BCLK_SPDY,
	CLK_CON_GAT_GOUT_AUD_ABOX_CCLK_ASB,
	CLK_CON_GAT_GOUT_AUD_ABOX_CCLK_CA32,
	CLK_CON_GAT_GOUT_AUD_ABOX_CCLK_DAP,
	CLK_CON_GAT_GOUT_AUD_CODEC_MCLK,
	CLK_CON_GAT_GOUT_AUD_TZPC_PCLK,
	CLK_CON_GAT_GOUT_AUD_GPIO_PCLK,
	CLK_CON_GAT_GOUT_AUD_PPMU_ACLK,
	CLK_CON_GAT_GOUT_AUD_PPMU_PCLK,
	CLK_CON_GAT_GOUT_AUD_SYSMMU_CLK_S1,
	CLK_CON_GAT_GOUT_AUD_SYSREG_PCLK,
	CLK_CON_GAT_GOUT_AUD_WDT_PCLK,
};

/* List of parent clocks for Muxes in CMU_AUD */
PNAME(mout_aud_pll_p)		= { "oscclk", "fout_aud_pll" };
PNAME(mout_aud_cpu_user_p)	= { "oscclk", "dout_aud" };
PNAME(mout_aud_cpu_p)		= { "dout_aud_cpu", "mout_aud_cpu_user" };
PNAME(mout_aud_cpu_hch_p)	= { "mout_aud_cpu", "oscclk" };
PNAME(mout_aud_uaif0_p)		= { "dout_aud_uaif0", "ioclk_audiocdclk0" };
PNAME(mout_aud_uaif1_p)		= { "dout_aud_uaif1", "ioclk_audiocdclk1" };
PNAME(mout_aud_uaif2_p)		= { "dout_aud_uaif2", "ioclk_audiocdclk2" };
PNAME(mout_aud_uaif3_p)		= { "dout_aud_uaif3", "ioclk_audiocdclk3" };
PNAME(mout_aud_uaif4_p)		= { "dout_aud_uaif4", "ioclk_audiocdclk4" };
PNAME(mout_aud_uaif5_p)		= { "dout_aud_uaif5", "ioclk_audiocdclk5" };
PNAME(mout_aud_uaif6_p)		= { "dout_aud_uaif6", "ioclk_audiocdclk6" };
PNAME(mout_aud_tick_usb_user_p)	= { "oscclk", "tick_usb" };
PNAME(mout_aud_fm_p)		= { "oscclk", "dout_aud_fm_spdy" };

/*
 * Do not provide PLL table to PLL_AUD, as MANUAL_PLL_CTRL bit is not set
 * for that PLL by default, so set_rate operation would fail.
 */
static const struct samsung_pll_clock aud_pll_clks[] __initconst = {
	PLL(pll_0831x, CLK_FOUT_AUD_PLL, "fout_aud_pll", "oscclk",
	    PLL_LOCKTIME_PLL_AUD, PLL_CON3_PLL_AUD, NULL),
};

static const struct samsung_fixed_rate_clock aud_fixed_clks[] __initconst = {
	FRATE(IOCLK_AUDIOCDCLK0, "ioclk_audiocdclk0", NULL, 0, 25000000),
	FRATE(IOCLK_AUDIOCDCLK1, "ioclk_audiocdclk1", NULL, 0, 25000000),
	FRATE(IOCLK_AUDIOCDCLK2, "ioclk_audiocdclk2", NULL, 0, 25000000),
	FRATE(IOCLK_AUDIOCDCLK3, "ioclk_audiocdclk3", NULL, 0, 25000000),
	FRATE(IOCLK_AUDIOCDCLK4, "ioclk_audiocdclk4", NULL, 0, 25000000),
	FRATE(IOCLK_AUDIOCDCLK5, "ioclk_audiocdclk5", NULL, 0, 25000000),
	FRATE(IOCLK_AUDIOCDCLK6, "ioclk_audiocdclk6", NULL, 0, 25000000),
	FRATE(TICK_USB, "tick_usb", NULL, 0, 60000000),
};

static const struct samsung_mux_clock aud_mux_clks[] __initconst = {
	MUX(CLK_MOUT_AUD_PLL, "mout_aud_pll", mout_aud_pll_p,
	    PLL_CON0_PLL_AUD, 4, 1),
	MUX(CLK_MOUT_AUD_CPU_USER, "mout_aud_cpu_user", mout_aud_cpu_user_p,
	    PLL_CON0_MUX_CLKCMU_AUD_CPU_USER, 4, 1),
	MUX(CLK_MOUT_AUD_TICK_USB_USER, "mout_aud_tick_usb_user",
	    mout_aud_tick_usb_user_p,
	    PLL_CON0_MUX_TICK_USB_USER, 4, 1),
	MUX(CLK_MOUT_AUD_CPU, "mout_aud_cpu", mout_aud_cpu_p,
	    CLK_CON_MUX_MUX_CLK_AUD_CPU, 0, 1),
	MUX(CLK_MOUT_AUD_CPU_HCH, "mout_aud_cpu_hch", mout_aud_cpu_hch_p,
	    CLK_CON_MUX_MUX_CLK_AUD_CPU_HCH, 0, 1),
	MUX(CLK_MOUT_AUD_UAIF0, "mout_aud_uaif0", mout_aud_uaif0_p,
	    CLK_CON_MUX_MUX_CLK_AUD_UAIF0, 0, 1),
	MUX(CLK_MOUT_AUD_UAIF1, "mout_aud_uaif1", mout_aud_uaif1_p,
	    CLK_CON_MUX_MUX_CLK_AUD_UAIF1, 0, 1),
	MUX(CLK_MOUT_AUD_UAIF2, "mout_aud_uaif2", mout_aud_uaif2_p,
	    CLK_CON_MUX_MUX_CLK_AUD_UAIF2, 0, 1),
	MUX(CLK_MOUT_AUD_UAIF3, "mout_aud_uaif3", mout_aud_uaif3_p,
	    CLK_CON_MUX_MUX_CLK_AUD_UAIF3, 0, 1),
	MUX(CLK_MOUT_AUD_UAIF4, "mout_aud_uaif4", mout_aud_uaif4_p,
	    CLK_CON_MUX_MUX_CLK_AUD_UAIF4, 0, 1),
	MUX(CLK_MOUT_AUD_UAIF5, "mout_aud_uaif5", mout_aud_uaif5_p,
	    CLK_CON_MUX_MUX_CLK_AUD_UAIF5, 0, 1),
	MUX(CLK_MOUT_AUD_UAIF6, "mout_aud_uaif6", mout_aud_uaif6_p,
	    CLK_CON_MUX_MUX_CLK_AUD_UAIF6, 0, 1),
	MUX(CLK_MOUT_AUD_FM, "mout_aud_fm", mout_aud_fm_p,
	    CLK_CON_MUX_MUX_CLK_AUD_FM, 0, 1),
};

static const struct samsung_div_clock aud_div_clks[] __initconst = {
	DIV(CLK_DOUT_AUD_CPU, "dout_aud_cpu", "mout_aud_pll",
	    CLK_CON_DIV_DIV_CLK_AUD_CPU, 0, 4),
	DIV(CLK_DOUT_AUD_BUSD, "dout_aud_busd", "mout_aud_pll",
	    CLK_CON_DIV_DIV_CLK_AUD_BUSD, 0, 4),
	DIV(CLK_DOUT_AUD_BUSP, "dout_aud_busp", "mout_aud_pll",
	    CLK_CON_DIV_DIV_CLK_AUD_BUSP, 0, 4),
	DIV(CLK_DOUT_AUD_AUDIF, "dout_aud_audif", "mout_aud_pll",
	    CLK_CON_DIV_DIV_CLK_AUD_AUDIF, 0, 9),
	DIV(CLK_DOUT_AUD_CPU_ACLK, "dout_aud_cpu_aclk", "mout_aud_cpu_hch",
	    CLK_CON_DIV_DIV_CLK_AUD_CPU_ACLK, 0, 3),
	DIV(CLK_DOUT_AUD_CPU_PCLKDBG, "dout_aud_cpu_pclkdbg",
	    "mout_aud_cpu_hch",
	    CLK_CON_DIV_DIV_CLK_AUD_CPU_PCLKDBG, 0, 3),
	DIV(CLK_DOUT_AUD_MCLK, "dout_aud_mclk", "dout_aud_audif",
	    CLK_CON_DIV_DIV_CLK_AUD_MCLK, 0, 2),
	DIV(CLK_DOUT_AUD_CNT, "dout_aud_cnt", "dout_aud_audif",
	    CLK_CON_DIV_DIV_CLK_AUD_CNT, 0, 10),
	DIV(CLK_DOUT_AUD_UAIF0, "dout_aud_uaif0", "dout_aud_audif",
	    CLK_CON_DIV_DIV_CLK_AUD_UAIF0, 0, 10),
	DIV(CLK_DOUT_AUD_UAIF1, "dout_aud_uaif1", "dout_aud_audif",
	    CLK_CON_DIV_DIV_CLK_AUD_UAIF1, 0, 10),
	DIV(CLK_DOUT_AUD_UAIF2, "dout_aud_uaif2", "dout_aud_audif",
	    CLK_CON_DIV_DIV_CLK_AUD_UAIF2, 0, 10),
	DIV(CLK_DOUT_AUD_UAIF3, "dout_aud_uaif3", "dout_aud_audif",
	    CLK_CON_DIV_DIV_CLK_AUD_UAIF3, 0, 10),
	DIV(CLK_DOUT_AUD_UAIF4, "dout_aud_uaif4", "dout_aud_audif",
	    CLK_CON_DIV_DIV_CLK_AUD_UAIF4, 0, 10),
	DIV(CLK_DOUT_AUD_UAIF5, "dout_aud_uaif5", "dout_aud_audif",
	    CLK_CON_DIV_DIV_CLK_AUD_UAIF5, 0, 10),
	DIV(CLK_DOUT_AUD_UAIF6, "dout_aud_uaif6", "dout_aud_audif",
	    CLK_CON_DIV_DIV_CLK_AUD_UAIF6, 0, 10),
	DIV(CLK_DOUT_AUD_FM_SPDY, "dout_aud_fm_spdy", "mout_aud_tick_usb_user",
	    CLK_CON_DIV_DIV_CLK_AUD_FM_SPDY, 0, 1),
	DIV(CLK_DOUT_AUD_FM, "dout_aud_fm", "mout_aud_fm",
	    CLK_CON_DIV_DIV_CLK_AUD_FM, 0, 10),
};

static const struct samsung_gate_clock aud_gate_clks[] __initconst = {
	GATE(CLK_GOUT_AUD_CMU_AUD_PCLK, "gout_aud_cmu_aud_pclk",
	     "dout_aud_busd",
	     CLK_CON_GAT_CLK_AUD_CMU_AUD_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_AUD_CA32_CCLK, "gout_aud_ca32_cclk", "mout_aud_cpu_hch",
	     CLK_CON_GAT_GOUT_AUD_ABOX_CCLK_CA32, 21, 0, 0),
	GATE(CLK_GOUT_AUD_ASB_CCLK, "gout_aud_asb_cclk", "dout_aud_cpu_aclk",
	     CLK_CON_GAT_GOUT_AUD_ABOX_CCLK_ASB, 21, 0, 0),
	GATE(CLK_GOUT_AUD_DAP_CCLK, "gout_aud_dap_cclk", "dout_aud_cpu_pclkdbg",
	     CLK_CON_GAT_GOUT_AUD_ABOX_CCLK_DAP, 21, 0, 0),
	/* TODO: Should be enabled in ABOX driver (or made CLK_IS_CRITICAL) */
	GATE(CLK_GOUT_AUD_ABOX_ACLK, "gout_aud_abox_aclk", "dout_aud_busd",
	     CLK_CON_GAT_GOUT_AUD_ABOX_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_AUD_GPIO_PCLK, "gout_aud_gpio_pclk", "dout_aud_busd",
	     CLK_CON_GAT_GOUT_AUD_GPIO_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_AUD_PPMU_ACLK, "gout_aud_ppmu_aclk", "dout_aud_busd",
	     CLK_CON_GAT_GOUT_AUD_PPMU_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_AUD_PPMU_PCLK, "gout_aud_ppmu_pclk", "dout_aud_busd",
	     CLK_CON_GAT_GOUT_AUD_PPMU_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_AUD_SYSMMU_CLK, "gout_aud_sysmmu_clk", "dout_aud_busd",
	     CLK_CON_GAT_GOUT_AUD_SYSMMU_CLK_S1, 21, 0, 0),
	GATE(CLK_GOUT_AUD_SYSREG_PCLK, "gout_aud_sysreg_pclk", "dout_aud_busd",
	     CLK_CON_GAT_GOUT_AUD_SYSREG_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_AUD_WDT_PCLK, "gout_aud_wdt_pclk", "dout_aud_busd",
	     CLK_CON_GAT_GOUT_AUD_WDT_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_AUD_TZPC_PCLK, "gout_aud_tzpc_pclk", "dout_aud_busp",
	     CLK_CON_GAT_GOUT_AUD_TZPC_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_AUD_CODEC_MCLK, "gout_aud_codec_mclk", "dout_aud_mclk",
	     CLK_CON_GAT_GOUT_AUD_CODEC_MCLK, 21, 0, 0),
	GATE(CLK_GOUT_AUD_CNT_BCLK, "gout_aud_cnt_bclk", "dout_aud_cnt",
	     CLK_CON_GAT_GOUT_AUD_ABOX_BCLK_CNT, 21, 0, 0),
	GATE(CLK_GOUT_AUD_UAIF0_BCLK, "gout_aud_uaif0_bclk", "mout_aud_uaif0",
	     CLK_CON_GAT_GOUT_AUD_ABOX_BCLK_UAIF0, 21, 0, 0),
	GATE(CLK_GOUT_AUD_UAIF1_BCLK, "gout_aud_uaif1_bclk", "mout_aud_uaif1",
	     CLK_CON_GAT_GOUT_AUD_ABOX_BCLK_UAIF1, 21, 0, 0),
	GATE(CLK_GOUT_AUD_UAIF2_BCLK, "gout_aud_uaif2_bclk", "mout_aud_uaif2",
	     CLK_CON_GAT_GOUT_AUD_ABOX_BCLK_UAIF2, 21, 0, 0),
	GATE(CLK_GOUT_AUD_UAIF3_BCLK, "gout_aud_uaif3_bclk", "mout_aud_uaif3",
	     CLK_CON_GAT_GOUT_AUD_ABOX_BCLK_UAIF3, 21, 0, 0),
	GATE(CLK_GOUT_AUD_UAIF4_BCLK, "gout_aud_uaif4_bclk", "mout_aud_uaif4",
	     CLK_CON_GAT_GOUT_AUD_ABOX_BCLK_UAIF4, 21, 0, 0),
	GATE(CLK_GOUT_AUD_UAIF5_BCLK, "gout_aud_uaif5_bclk", "mout_aud_uaif5",
	     CLK_CON_GAT_GOUT_AUD_ABOX_BCLK_UAIF5, 21, 0, 0),
	GATE(CLK_GOUT_AUD_UAIF6_BCLK, "gout_aud_uaif6_bclk", "mout_aud_uaif6",
	     CLK_CON_GAT_GOUT_AUD_ABOX_BCLK_UAIF6, 21, 0, 0),
	GATE(CLK_GOUT_AUD_SPDY_BCLK, "gout_aud_spdy_bclk", "dout_aud_fm",
	     CLK_CON_GAT_GOUT_AUD_ABOX_BCLK_SPDY, 21, 0, 0),
};

static const struct samsung_cmu_info aud_cmu_info __initconst = {
	.pll_clks		= aud_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(aud_pll_clks),
	.mux_clks		= aud_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(aud_mux_clks),
	.div_clks		= aud_div_clks,
	.nr_div_clks		= ARRAY_SIZE(aud_div_clks),
	.gate_clks		= aud_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(aud_gate_clks),
	.fixed_clks		= aud_fixed_clks,
	.nr_fixed_clks		= ARRAY_SIZE(aud_fixed_clks),
	.nr_clk_ids		= CLKS_NR_AUD,
	.clk_regs		= aud_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(aud_clk_regs),
	.clk_name		= "dout_aud",
};

/* ---- CMU_CMGP ------------------------------------------------------------ */

/* Register Offset definitions for CMU_CMGP (0x11c00000) */
#define CLK_CON_MUX_CLK_CMGP_ADC		0x1000
#define CLK_CON_MUX_MUX_CLK_CMGP_USI_CMGP0	0x1004
#define CLK_CON_MUX_MUX_CLK_CMGP_USI_CMGP1	0x1008
#define CLK_CON_DIV_DIV_CLK_CMGP_ADC		0x1800
#define CLK_CON_DIV_DIV_CLK_CMGP_USI_CMGP0	0x1804
#define CLK_CON_DIV_DIV_CLK_CMGP_USI_CMGP1	0x1808
#define CLK_CON_GAT_GOUT_CMGP_ADC_PCLK_S0	0x200c
#define CLK_CON_GAT_GOUT_CMGP_ADC_PCLK_S1	0x2010
#define CLK_CON_GAT_GOUT_CMGP_GPIO_PCLK		0x2018
#define CLK_CON_GAT_GOUT_CMGP_SYSREG_CMGP_PCLK	0x2040
#define CLK_CON_GAT_GOUT_CMGP_USI_CMGP0_IPCLK	0x2044
#define CLK_CON_GAT_GOUT_CMGP_USI_CMGP0_PCLK	0x2048
#define CLK_CON_GAT_GOUT_CMGP_USI_CMGP1_IPCLK	0x204c
#define CLK_CON_GAT_GOUT_CMGP_USI_CMGP1_PCLK	0x2050

static const unsigned long cmgp_clk_regs[] __initconst = {
	CLK_CON_MUX_CLK_CMGP_ADC,
	CLK_CON_MUX_MUX_CLK_CMGP_USI_CMGP0,
	CLK_CON_MUX_MUX_CLK_CMGP_USI_CMGP1,
	CLK_CON_DIV_DIV_CLK_CMGP_ADC,
	CLK_CON_DIV_DIV_CLK_CMGP_USI_CMGP0,
	CLK_CON_DIV_DIV_CLK_CMGP_USI_CMGP1,
	CLK_CON_GAT_GOUT_CMGP_ADC_PCLK_S0,
	CLK_CON_GAT_GOUT_CMGP_ADC_PCLK_S1,
	CLK_CON_GAT_GOUT_CMGP_GPIO_PCLK,
	CLK_CON_GAT_GOUT_CMGP_SYSREG_CMGP_PCLK,
	CLK_CON_GAT_GOUT_CMGP_USI_CMGP0_IPCLK,
	CLK_CON_GAT_GOUT_CMGP_USI_CMGP0_PCLK,
	CLK_CON_GAT_GOUT_CMGP_USI_CMGP1_IPCLK,
	CLK_CON_GAT_GOUT_CMGP_USI_CMGP1_PCLK,
};

/* List of parent clocks for Muxes in CMU_CMGP */
PNAME(mout_cmgp_usi0_p)	= { "clk_rco_cmgp", "gout_clkcmu_cmgp_bus" };
PNAME(mout_cmgp_usi1_p)	= { "clk_rco_cmgp", "gout_clkcmu_cmgp_bus" };
PNAME(mout_cmgp_adc_p)	= { "oscclk", "dout_cmgp_adc" };

static const struct samsung_fixed_rate_clock cmgp_fixed_clks[] __initconst = {
	FRATE(CLK_RCO_CMGP, "clk_rco_cmgp", NULL, 0, 49152000),
};

static const struct samsung_mux_clock cmgp_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CMGP_ADC, "mout_cmgp_adc", mout_cmgp_adc_p,
	    CLK_CON_MUX_CLK_CMGP_ADC, 0, 1),
	MUX_F(CLK_MOUT_CMGP_USI0, "mout_cmgp_usi0", mout_cmgp_usi0_p,
	      CLK_CON_MUX_MUX_CLK_CMGP_USI_CMGP0, 0, 1, CLK_SET_RATE_PARENT, 0),
	MUX_F(CLK_MOUT_CMGP_USI1, "mout_cmgp_usi1", mout_cmgp_usi1_p,
	      CLK_CON_MUX_MUX_CLK_CMGP_USI_CMGP1, 0, 1, CLK_SET_RATE_PARENT, 0),
};

static const struct samsung_div_clock cmgp_div_clks[] __initconst = {
	DIV(CLK_DOUT_CMGP_ADC, "dout_cmgp_adc", "gout_clkcmu_cmgp_bus",
	    CLK_CON_DIV_DIV_CLK_CMGP_ADC, 0, 4),
	DIV_F(CLK_DOUT_CMGP_USI0, "dout_cmgp_usi0", "mout_cmgp_usi0",
	      CLK_CON_DIV_DIV_CLK_CMGP_USI_CMGP0, 0, 5, CLK_SET_RATE_PARENT, 0),
	DIV_F(CLK_DOUT_CMGP_USI1, "dout_cmgp_usi1", "mout_cmgp_usi1",
	      CLK_CON_DIV_DIV_CLK_CMGP_USI_CMGP1, 0, 5, CLK_SET_RATE_PARENT, 0),
};

static const struct samsung_gate_clock cmgp_gate_clks[] __initconst = {
	GATE(CLK_GOUT_CMGP_ADC_S0_PCLK, "gout_adc_s0_pclk",
	     "gout_clkcmu_cmgp_bus",
	     CLK_CON_GAT_GOUT_CMGP_ADC_PCLK_S0, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_ADC_S1_PCLK, "gout_adc_s1_pclk",
	     "gout_clkcmu_cmgp_bus",
	     CLK_CON_GAT_GOUT_CMGP_ADC_PCLK_S1, 21, 0, 0),
	/* TODO: Should be enabled in GPIO driver (or made CLK_IS_CRITICAL) */
	GATE(CLK_GOUT_CMGP_GPIO_PCLK, "gout_gpio_cmgp_pclk",
	     "gout_clkcmu_cmgp_bus",
	     CLK_CON_GAT_GOUT_CMGP_GPIO_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_CMGP_USI0_IPCLK, "gout_cmgp_usi0_ipclk", "dout_cmgp_usi0",
	     CLK_CON_GAT_GOUT_CMGP_USI_CMGP0_IPCLK, 21, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_CMGP_USI0_PCLK, "gout_cmgp_usi0_pclk",
	     "gout_clkcmu_cmgp_bus",
	     CLK_CON_GAT_GOUT_CMGP_USI_CMGP0_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_CMGP_USI1_IPCLK, "gout_cmgp_usi1_ipclk", "dout_cmgp_usi1",
	     CLK_CON_GAT_GOUT_CMGP_USI_CMGP1_IPCLK, 21, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_CMGP_USI1_PCLK, "gout_cmgp_usi1_pclk",
	     "gout_clkcmu_cmgp_bus",
	     CLK_CON_GAT_GOUT_CMGP_USI_CMGP1_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_SYSREG_CMGP_PCLK, "gout_sysreg_cmgp_pclk",
	     "gout_clkcmu_cmgp_bus",
	     CLK_CON_GAT_GOUT_CMGP_SYSREG_CMGP_PCLK, 21, 0, 0),
};

static const struct samsung_cmu_info cmgp_cmu_info __initconst = {
	.mux_clks		= cmgp_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(cmgp_mux_clks),
	.div_clks		= cmgp_div_clks,
	.nr_div_clks		= ARRAY_SIZE(cmgp_div_clks),
	.gate_clks		= cmgp_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(cmgp_gate_clks),
	.fixed_clks		= cmgp_fixed_clks,
	.nr_fixed_clks		= ARRAY_SIZE(cmgp_fixed_clks),
	.nr_clk_ids		= CLKS_NR_CMGP,
	.clk_regs		= cmgp_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(cmgp_clk_regs),
	.clk_name		= "gout_clkcmu_cmgp_bus",
};

/* ---- CMU_CPUCL0 ---------------------------------------------------------- */

/* Register Offset definitions for CMU_CPUCL0 (0x10900000) */
#define PLL_LOCKTIME_PLL_CPUCL0				0x0000
#define PLL_CON0_PLL_CPUCL0				0x0100
#define PLL_CON1_PLL_CPUCL0				0x0104
#define PLL_CON3_PLL_CPUCL0				0x010c
#define PLL_CON0_MUX_CLKCMU_CPUCL0_DBG_USER		0x0600
#define PLL_CON0_MUX_CLKCMU_CPUCL0_SWITCH_USER		0x0610
#define CLK_CON_MUX_MUX_CLK_CPUCL0_PLL			0x100c
#define CLK_CON_DIV_DIV_CLK_CLUSTER0_ACLK		0x1800
#define CLK_CON_DIV_DIV_CLK_CLUSTER0_ATCLK		0x1808
#define CLK_CON_DIV_DIV_CLK_CLUSTER0_PCLKDBG		0x180c
#define CLK_CON_DIV_DIV_CLK_CLUSTER0_PERIPHCLK		0x1810
#define CLK_CON_DIV_DIV_CLK_CPUCL0_CMUREF		0x1814
#define CLK_CON_DIV_DIV_CLK_CPUCL0_CPU			0x1818
#define CLK_CON_DIV_DIV_CLK_CPUCL0_PCLK			0x181c
#define CLK_CON_GAT_CLK_CPUCL0_CLUSTER0_ATCLK		0x2000
#define CLK_CON_GAT_CLK_CPUCL0_CLUSTER0_PCLK		0x2004
#define CLK_CON_GAT_CLK_CPUCL0_CLUSTER0_PERIPHCLK	0x2008
#define CLK_CON_GAT_CLK_CPUCL0_CLUSTER0_SCLK		0x200c
#define CLK_CON_GAT_CLK_CPUCL0_CMU_CPUCL0_PCLK		0x2010
#define CLK_CON_GAT_GATE_CLK_CPUCL0_CPU			0x2020

static const unsigned long cpucl0_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_CPUCL0,
	PLL_CON0_PLL_CPUCL0,
	PLL_CON1_PLL_CPUCL0,
	PLL_CON3_PLL_CPUCL0,
	PLL_CON0_MUX_CLKCMU_CPUCL0_DBG_USER,
	PLL_CON0_MUX_CLKCMU_CPUCL0_SWITCH_USER,
	CLK_CON_MUX_MUX_CLK_CPUCL0_PLL,
	CLK_CON_DIV_DIV_CLK_CLUSTER0_ACLK,
	CLK_CON_DIV_DIV_CLK_CLUSTER0_ATCLK,
	CLK_CON_DIV_DIV_CLK_CLUSTER0_PCLKDBG,
	CLK_CON_DIV_DIV_CLK_CLUSTER0_PERIPHCLK,
	CLK_CON_DIV_DIV_CLK_CPUCL0_CMUREF,
	CLK_CON_DIV_DIV_CLK_CPUCL0_CPU,
	CLK_CON_DIV_DIV_CLK_CPUCL0_PCLK,
	CLK_CON_GAT_CLK_CPUCL0_CLUSTER0_ATCLK,
	CLK_CON_GAT_CLK_CPUCL0_CLUSTER0_PCLK,
	CLK_CON_GAT_CLK_CPUCL0_CLUSTER0_PERIPHCLK,
	CLK_CON_GAT_CLK_CPUCL0_CLUSTER0_SCLK,
	CLK_CON_GAT_CLK_CPUCL0_CMU_CPUCL0_PCLK,
	CLK_CON_GAT_GATE_CLK_CPUCL0_CPU,
};

/* List of parent clocks for Muxes in CMU_CPUCL0 */
PNAME(mout_pll_cpucl0_p)		 = { "oscclk", "fout_cpucl0_pll" };
PNAME(mout_cpucl0_switch_user_p)	 = { "oscclk", "dout_cpucl0_switch" };
PNAME(mout_cpucl0_dbg_user_p)		 = { "oscclk", "dout_cpucl0_dbg" };
PNAME(mout_cpucl0_pll_p)		 = { "mout_pll_cpucl0",
					     "mout_cpucl0_switch_user" };

static const struct samsung_pll_rate_table cpu_pll_rates[] __initconst = {
	PLL_35XX_RATE(26 * MHZ, 2210000000U, 255, 3, 0),
	PLL_35XX_RATE(26 * MHZ, 2106000000U, 243, 3, 0),
	PLL_35XX_RATE(26 * MHZ, 2002000000U, 231, 3, 0),
	PLL_35XX_RATE(26 * MHZ, 1846000000U, 213, 3, 0),
	PLL_35XX_RATE(26 * MHZ, 1742000000U, 201, 3, 0),
	PLL_35XX_RATE(26 * MHZ, 1586000000U, 183, 3, 0),
	PLL_35XX_RATE(26 * MHZ, 1456000000U, 168, 3, 0),
	PLL_35XX_RATE(26 * MHZ, 1300000000U, 150, 3, 0),
	PLL_35XX_RATE(26 * MHZ, 1157000000U, 267, 3, 1),
	PLL_35XX_RATE(26 * MHZ, 1053000000U, 243, 3, 1),
	PLL_35XX_RATE(26 * MHZ, 949000000U,  219, 3, 1),
	PLL_35XX_RATE(26 * MHZ, 806000000U,  186, 3, 1),
	PLL_35XX_RATE(26 * MHZ, 650000000U,  150, 3, 1),
	PLL_35XX_RATE(26 * MHZ, 546000000U,  252, 3, 2),
	PLL_35XX_RATE(26 * MHZ, 442000000U,  204, 3, 2),
	PLL_35XX_RATE(26 * MHZ, 351000000U,  162, 3, 2),
	PLL_35XX_RATE(26 * MHZ, 247000000U,  114, 3, 2),
	PLL_35XX_RATE(26 * MHZ, 182000000U,  168, 3, 3),
	PLL_35XX_RATE(26 * MHZ, 130000000U,  120, 3, 3),
};

static const struct samsung_pll_clock cpucl0_pll_clks[] __initconst = {
	PLL(pll_0822x, CLK_FOUT_CPUCL0_PLL, "fout_cpucl0_pll", "oscclk",
	    PLL_LOCKTIME_PLL_CPUCL0, PLL_CON3_PLL_CPUCL0, cpu_pll_rates),
};

static const struct samsung_mux_clock cpucl0_mux_clks[] __initconst = {
	MUX_F(CLK_MOUT_PLL_CPUCL0, "mout_pll_cpucl0", mout_pll_cpucl0_p,
	      PLL_CON0_PLL_CPUCL0, 4, 1,
	      CLK_SET_RATE_PARENT | CLK_RECALC_NEW_RATES, 0),
	MUX_F(CLK_MOUT_CPUCL0_SWITCH_USER, "mout_cpucl0_switch_user",
	      mout_cpucl0_switch_user_p,
	      PLL_CON0_MUX_CLKCMU_CPUCL0_SWITCH_USER, 4, 1,
	      CLK_SET_RATE_PARENT, 0),
	MUX(CLK_MOUT_CPUCL0_DBG_USER, "mout_cpucl0_dbg_user",
	    mout_cpucl0_dbg_user_p,
	    PLL_CON0_MUX_CLKCMU_CPUCL0_DBG_USER, 4, 1),
	MUX_F(CLK_MOUT_CPUCL0_PLL, "mout_cpucl0_pll", mout_cpucl0_pll_p,
	      CLK_CON_MUX_MUX_CLK_CPUCL0_PLL, 0, 1, CLK_SET_RATE_PARENT, 0),
};

static const struct samsung_div_clock cpucl0_div_clks[] __initconst = {
	DIV_F(CLK_DOUT_CPUCL0_CPU, "dout_cpucl0_cpu", "mout_cpucl0_pll",
	      CLK_CON_DIV_DIV_CLK_CPUCL0_CPU, 0, 1,
	      CLK_GET_RATE_NOCACHE, CLK_DIVIDER_READ_ONLY),
	DIV_F(CLK_DOUT_CPUCL0_CMUREF, "dout_cpucl0_cmuref", "dout_cpucl0_cpu",
	      CLK_CON_DIV_DIV_CLK_CPUCL0_CMUREF, 0, 3,
	      CLK_GET_RATE_NOCACHE, CLK_DIVIDER_READ_ONLY),
	DIV_F(CLK_DOUT_CPUCL0_PCLK, "dout_cpucl0_pclk", "dout_cpucl0_cpu",
	      CLK_CON_DIV_DIV_CLK_CPUCL0_PCLK, 0, 4,
	      CLK_GET_RATE_NOCACHE, CLK_DIVIDER_READ_ONLY),

	/* EMBEDDED_CMU_CPUCL0 */
	DIV_F(CLK_DOUT_CLUSTER0_ACLK, "dout_cluster0_aclk", "gout_cluster0_cpu",
	      CLK_CON_DIV_DIV_CLK_CLUSTER0_ACLK, 0, 4,
	      CLK_GET_RATE_NOCACHE, CLK_DIVIDER_READ_ONLY),
	DIV_F(CLK_DOUT_CLUSTER0_ATCLK, "dout_cluster0_atclk",
	      "gout_cluster0_cpu", CLK_CON_DIV_DIV_CLK_CLUSTER0_ATCLK, 0, 4,
	      CLK_GET_RATE_NOCACHE, CLK_DIVIDER_READ_ONLY),
	DIV_F(CLK_DOUT_CLUSTER0_PCLKDBG, "dout_cluster0_pclkdbg",
	      "gout_cluster0_cpu", CLK_CON_DIV_DIV_CLK_CLUSTER0_PCLKDBG, 0, 4,
	      CLK_GET_RATE_NOCACHE, CLK_DIVIDER_READ_ONLY),
	DIV_F(CLK_DOUT_CLUSTER0_PERIPHCLK, "dout_cluster0_periphclk",
	      "gout_cluster0_cpu", CLK_CON_DIV_DIV_CLK_CLUSTER0_PERIPHCLK, 0, 4,
	      CLK_GET_RATE_NOCACHE, CLK_DIVIDER_READ_ONLY),
};

static const struct samsung_gate_clock cpucl0_gate_clks[] __initconst = {
	GATE(CLK_GOUT_CPUCL0_CMU_CPUCL0_PCLK, "gout_cpucl0_cmu_cpucl0_pclk",
	     "dout_cpucl0_pclk",
	     CLK_CON_GAT_CLK_CPUCL0_CMU_CPUCL0_PCLK, 21, CLK_IGNORE_UNUSED, 0),

	/* EMBEDDED_CMU_CPUCL0 */
	GATE(CLK_GOUT_CLUSTER0_CPU, "gout_cluster0_cpu", "dout_cpucl0_cpu",
	     CLK_CON_GAT_GATE_CLK_CPUCL0_CPU, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_CLUSTER0_SCLK, "gout_cluster0_sclk", "gout_cluster0_cpu",
	     CLK_CON_GAT_CLK_CPUCL0_CLUSTER0_SCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_CLUSTER0_ATCLK, "gout_cluster0_atclk",
	     "dout_cluster0_atclk",
	     CLK_CON_GAT_CLK_CPUCL0_CLUSTER0_ATCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_CLUSTER0_PERIPHCLK, "gout_cluster0_periphclk",
	     "dout_cluster0_periphclk",
	     CLK_CON_GAT_CLK_CPUCL0_CLUSTER0_PERIPHCLK, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_CLUSTER0_PCLK, "gout_cluster0_pclk",
	     "dout_cluster0_pclkdbg",
	     CLK_CON_GAT_CLK_CPUCL0_CLUSTER0_PCLK, 21, CLK_IGNORE_UNUSED, 0),
};

/*
 * Each parameter is going to be written into the corresponding DIV register. So
 * the actual divider value for each parameter will be 1/(param+1). All these
 * parameters must be in the range of 0..15, as the divider range for all of
 * these DIV clocks is 1..16. The default values for these dividers is
 * (1, 3, 3, 1).
 */
#define E850_CPU_DIV0(aclk, atclk, pclkdbg, periphclk) \
	(((aclk) << 16) | ((atclk) << 12) | ((pclkdbg) << 8) | \
	 ((periphclk) << 4))

static const struct exynos_cpuclk_cfg_data exynos850_cluster_clk_d[] __initconst
= {
	{ 2210000, E850_CPU_DIV0(1, 3, 3, 1) },
	{ 2106000, E850_CPU_DIV0(1, 3, 3, 1) },
	{ 2002000, E850_CPU_DIV0(1, 3, 3, 1) },
	{ 1846000, E850_CPU_DIV0(1, 3, 3, 1) },
	{ 1742000, E850_CPU_DIV0(1, 3, 3, 1) },
	{ 1586000, E850_CPU_DIV0(1, 3, 3, 1) },
	{ 1456000, E850_CPU_DIV0(1, 3, 3, 1) },
	{ 1300000, E850_CPU_DIV0(1, 3, 3, 1) },
	{ 1157000, E850_CPU_DIV0(1, 3, 3, 1) },
	{ 1053000, E850_CPU_DIV0(1, 3, 3, 1) },
	{ 949000,  E850_CPU_DIV0(1, 3, 3, 1) },
	{ 806000,  E850_CPU_DIV0(1, 3, 3, 1) },
	{ 650000,  E850_CPU_DIV0(1, 3, 3, 1) },
	{ 546000,  E850_CPU_DIV0(1, 3, 3, 1) },
	{ 442000,  E850_CPU_DIV0(1, 3, 3, 1) },
	{ 351000,  E850_CPU_DIV0(1, 3, 3, 1) },
	{ 247000,  E850_CPU_DIV0(1, 3, 3, 1) },
	{ 182000,  E850_CPU_DIV0(1, 3, 3, 1) },
	{ 130000,  E850_CPU_DIV0(1, 3, 3, 1) },
	{ 0 }
};

static const struct samsung_cpu_clock cpucl0_cpu_clks[] __initconst = {
	CPU_CLK(CLK_CLUSTER0_SCLK, "cluster0_clk", CLK_MOUT_PLL_CPUCL0,
		CLK_MOUT_CPUCL0_SWITCH_USER, 0, 0x0, CPUCLK_LAYOUT_E850_CL0,
		exynos850_cluster_clk_d),
};

static const struct samsung_cmu_info cpucl0_cmu_info __initconst = {
	.pll_clks		= cpucl0_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(cpucl0_pll_clks),
	.mux_clks		= cpucl0_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(cpucl0_mux_clks),
	.div_clks		= cpucl0_div_clks,
	.nr_div_clks		= ARRAY_SIZE(cpucl0_div_clks),
	.gate_clks		= cpucl0_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(cpucl0_gate_clks),
	.cpu_clks		= cpucl0_cpu_clks,
	.nr_cpu_clks		= ARRAY_SIZE(cpucl0_cpu_clks),
	.nr_clk_ids		= CLKS_NR_CPUCL0,
	.clk_regs		= cpucl0_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(cpucl0_clk_regs),
	.clk_name		= "dout_cpucl0_switch",
	.manual_plls		= true,
};

static void __init exynos850_cmu_cpucl0_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &cpucl0_cmu_info);
}

/* Register CMU_CPUCL0 early, as CPU clocks should be available ASAP */
CLK_OF_DECLARE(exynos850_cmu_cpucl0, "samsung,exynos850-cmu-cpucl0",
	       exynos850_cmu_cpucl0_init);

/* ---- CMU_CPUCL1 ---------------------------------------------------------- */

/* Register Offset definitions for CMU_CPUCL1 (0x10800000) */
#define PLL_LOCKTIME_PLL_CPUCL1				0x0000
#define PLL_CON0_PLL_CPUCL1				0x0100
#define PLL_CON1_PLL_CPUCL1				0x0104
#define PLL_CON3_PLL_CPUCL1				0x010c
#define PLL_CON0_MUX_CLKCMU_CPUCL1_DBG_USER		0x0600
#define PLL_CON0_MUX_CLKCMU_CPUCL1_SWITCH_USER		0x0610
#define CLK_CON_MUX_MUX_CLK_CPUCL1_PLL			0x1000
#define CLK_CON_DIV_DIV_CLK_CLUSTER1_ACLK		0x1800
#define CLK_CON_DIV_DIV_CLK_CLUSTER1_ATCLK		0x1808
#define CLK_CON_DIV_DIV_CLK_CLUSTER1_PCLKDBG		0x180c
#define CLK_CON_DIV_DIV_CLK_CLUSTER1_PERIPHCLK		0x1810
#define CLK_CON_DIV_DIV_CLK_CPUCL1_CMUREF		0x1814
#define CLK_CON_DIV_DIV_CLK_CPUCL1_CPU			0x1818
#define CLK_CON_DIV_DIV_CLK_CPUCL1_PCLK			0x181c
#define CLK_CON_GAT_CLK_CPUCL1_CLUSTER1_ATCLK		0x2000
#define CLK_CON_GAT_CLK_CPUCL1_CLUSTER1_PCLK		0x2004
#define CLK_CON_GAT_CLK_CPUCL1_CLUSTER1_PERIPHCLK	0x2008
#define CLK_CON_GAT_CLK_CPUCL1_CLUSTER1_SCLK		0x200c
#define CLK_CON_GAT_CLK_CPUCL1_CMU_CPUCL1_PCLK		0x2010
#define CLK_CON_GAT_GATE_CLK_CPUCL1_CPU			0x2020

static const unsigned long cpucl1_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_CPUCL1,
	PLL_CON0_PLL_CPUCL1,
	PLL_CON1_PLL_CPUCL1,
	PLL_CON3_PLL_CPUCL1,
	PLL_CON0_MUX_CLKCMU_CPUCL1_DBG_USER,
	PLL_CON0_MUX_CLKCMU_CPUCL1_SWITCH_USER,
	CLK_CON_MUX_MUX_CLK_CPUCL1_PLL,
	CLK_CON_DIV_DIV_CLK_CLUSTER1_ACLK,
	CLK_CON_DIV_DIV_CLK_CLUSTER1_ATCLK,
	CLK_CON_DIV_DIV_CLK_CLUSTER1_PCLKDBG,
	CLK_CON_DIV_DIV_CLK_CLUSTER1_PERIPHCLK,
	CLK_CON_DIV_DIV_CLK_CPUCL1_CMUREF,
	CLK_CON_DIV_DIV_CLK_CPUCL1_CPU,
	CLK_CON_DIV_DIV_CLK_CPUCL1_PCLK,
	CLK_CON_GAT_CLK_CPUCL1_CLUSTER1_ATCLK,
	CLK_CON_GAT_CLK_CPUCL1_CLUSTER1_PCLK,
	CLK_CON_GAT_CLK_CPUCL1_CLUSTER1_PERIPHCLK,
	CLK_CON_GAT_CLK_CPUCL1_CLUSTER1_SCLK,
	CLK_CON_GAT_CLK_CPUCL1_CMU_CPUCL1_PCLK,
	CLK_CON_GAT_GATE_CLK_CPUCL1_CPU,
};

/* List of parent clocks for Muxes in CMU_CPUCL0 */
PNAME(mout_pll_cpucl1_p)		 = { "oscclk", "fout_cpucl1_pll" };
PNAME(mout_cpucl1_switch_user_p)	 = { "oscclk", "dout_cpucl1_switch" };
PNAME(mout_cpucl1_dbg_user_p)		 = { "oscclk", "dout_cpucl1_dbg" };
PNAME(mout_cpucl1_pll_p)		 = { "mout_pll_cpucl1",
					     "mout_cpucl1_switch_user" };

static const struct samsung_pll_clock cpucl1_pll_clks[] __initconst = {
	PLL(pll_0822x, CLK_FOUT_CPUCL1_PLL, "fout_cpucl1_pll", "oscclk",
	    PLL_LOCKTIME_PLL_CPUCL1, PLL_CON3_PLL_CPUCL1, cpu_pll_rates),
};

static const struct samsung_mux_clock cpucl1_mux_clks[] __initconst = {
	MUX_F(CLK_MOUT_PLL_CPUCL1, "mout_pll_cpucl1", mout_pll_cpucl1_p,
	      PLL_CON0_PLL_CPUCL1, 4, 1,
	      CLK_SET_RATE_PARENT | CLK_RECALC_NEW_RATES, 0),
	MUX_F(CLK_MOUT_CPUCL1_SWITCH_USER, "mout_cpucl1_switch_user",
	      mout_cpucl1_switch_user_p,
	      PLL_CON0_MUX_CLKCMU_CPUCL1_SWITCH_USER, 4, 1,
	      CLK_SET_RATE_PARENT, 0),
	MUX(CLK_MOUT_CPUCL1_DBG_USER, "mout_cpucl1_dbg_user",
	    mout_cpucl1_dbg_user_p,
	    PLL_CON0_MUX_CLKCMU_CPUCL1_DBG_USER, 4, 1),
	MUX_F(CLK_MOUT_CPUCL1_PLL, "mout_cpucl1_pll", mout_cpucl1_pll_p,
	      CLK_CON_MUX_MUX_CLK_CPUCL1_PLL, 0, 1, CLK_SET_RATE_PARENT, 0),
};

static const struct samsung_div_clock cpucl1_div_clks[] __initconst = {
	DIV_F(CLK_DOUT_CPUCL1_CPU, "dout_cpucl1_cpu", "mout_cpucl1_pll",
	      CLK_CON_DIV_DIV_CLK_CPUCL1_CPU, 0, 1,
	      CLK_GET_RATE_NOCACHE, CLK_DIVIDER_READ_ONLY),
	DIV_F(CLK_DOUT_CPUCL1_CMUREF, "dout_cpucl1_cmuref", "dout_cpucl1_cpu",
	      CLK_CON_DIV_DIV_CLK_CPUCL1_CMUREF, 0, 3,
	      CLK_GET_RATE_NOCACHE, CLK_DIVIDER_READ_ONLY),
	DIV_F(CLK_DOUT_CPUCL1_PCLK, "dout_cpucl1_pclk", "dout_cpucl1_cpu",
	      CLK_CON_DIV_DIV_CLK_CPUCL1_PCLK, 0, 4,
	      CLK_GET_RATE_NOCACHE, CLK_DIVIDER_READ_ONLY),

	/* EMBEDDED_CMU_CPUCL1 */
	DIV_F(CLK_DOUT_CLUSTER1_ACLK, "dout_cluster1_aclk", "gout_cluster1_cpu",
	      CLK_CON_DIV_DIV_CLK_CLUSTER1_ACLK, 0, 4,
	      CLK_GET_RATE_NOCACHE, CLK_DIVIDER_READ_ONLY),
	DIV_F(CLK_DOUT_CLUSTER1_ATCLK, "dout_cluster1_atclk",
	      "gout_cluster1_cpu", CLK_CON_DIV_DIV_CLK_CLUSTER1_ATCLK, 0, 4,
	      CLK_GET_RATE_NOCACHE, CLK_DIVIDER_READ_ONLY),
	DIV_F(CLK_DOUT_CLUSTER1_PCLKDBG, "dout_cluster1_pclkdbg",
	      "gout_cluster1_cpu", CLK_CON_DIV_DIV_CLK_CLUSTER1_PCLKDBG, 0, 4,
	      CLK_GET_RATE_NOCACHE, CLK_DIVIDER_READ_ONLY),
	DIV_F(CLK_DOUT_CLUSTER1_PERIPHCLK, "dout_cluster1_periphclk",
	      "gout_cluster1_cpu", CLK_CON_DIV_DIV_CLK_CLUSTER1_PERIPHCLK, 0, 4,
	      CLK_GET_RATE_NOCACHE, CLK_DIVIDER_READ_ONLY),
};

static const struct samsung_gate_clock cpucl1_gate_clks[] __initconst = {
	GATE(CLK_GOUT_CPUCL1_CMU_CPUCL1_PCLK, "gout_cpucl1_cmu_cpucl1_pclk",
	     "dout_cpucl1_pclk",
	     CLK_CON_GAT_CLK_CPUCL1_CMU_CPUCL1_PCLK, 21, CLK_IGNORE_UNUSED, 0),

	/* EMBEDDED_CMU_CPUCL1 */
	GATE(CLK_GOUT_CLUSTER1_CPU, "gout_cluster1_cpu", "dout_cpucl1_cpu",
	     CLK_CON_GAT_GATE_CLK_CPUCL1_CPU, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_CLUSTER1_SCLK, "gout_cluster1_sclk", "gout_cluster1_cpu",
	     CLK_CON_GAT_CLK_CPUCL1_CLUSTER1_SCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_CLUSTER1_ATCLK, "gout_cluster1_atclk",
	     "dout_cluster1_atclk",
	     CLK_CON_GAT_CLK_CPUCL1_CLUSTER1_ATCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_CLUSTER1_PERIPHCLK, "gout_cluster1_periphclk",
	     "dout_cluster1_periphclk",
	     CLK_CON_GAT_CLK_CPUCL1_CLUSTER1_PERIPHCLK, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_CLUSTER1_PCLK, "gout_cluster1_pclk",
	     "dout_cluster1_pclkdbg",
	     CLK_CON_GAT_CLK_CPUCL1_CLUSTER1_PCLK, 21, CLK_IGNORE_UNUSED, 0),
};

static const struct samsung_cpu_clock cpucl1_cpu_clks[] __initconst = {
	CPU_CLK(CLK_CLUSTER1_SCLK, "cluster1_clk", CLK_MOUT_PLL_CPUCL1,
		CLK_MOUT_CPUCL1_SWITCH_USER, 0, 0x0, CPUCLK_LAYOUT_E850_CL1,
		exynos850_cluster_clk_d),
};

static const struct samsung_cmu_info cpucl1_cmu_info __initconst = {
	.pll_clks		= cpucl1_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(cpucl1_pll_clks),
	.mux_clks		= cpucl1_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(cpucl1_mux_clks),
	.div_clks		= cpucl1_div_clks,
	.nr_div_clks		= ARRAY_SIZE(cpucl1_div_clks),
	.gate_clks		= cpucl1_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(cpucl1_gate_clks),
	.cpu_clks		= cpucl1_cpu_clks,
	.nr_cpu_clks		= ARRAY_SIZE(cpucl1_cpu_clks),
	.nr_clk_ids		= CLKS_NR_CPUCL1,
	.clk_regs		= cpucl1_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(cpucl1_clk_regs),
	.clk_name		= "dout_cpucl1_switch",
	.manual_plls		= true,
};

static void __init exynos850_cmu_cpucl1_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &cpucl1_cmu_info);
}

/* Register CMU_CPUCL1 early, as CPU clocks should be available ASAP */
CLK_OF_DECLARE(exynos850_cmu_cpucl1, "samsung,exynos850-cmu-cpucl1",
	       exynos850_cmu_cpucl1_init);

/* ---- CMU_G3D ------------------------------------------------------------- */

/* Register Offset definitions for CMU_G3D (0x11400000) */
#define PLL_LOCKTIME_PLL_G3D			0x0000
#define PLL_CON0_PLL_G3D			0x0100
#define PLL_CON3_PLL_G3D			0x010c
#define PLL_CON0_MUX_CLKCMU_G3D_SWITCH_USER	0x0600
#define CLK_CON_MUX_MUX_CLK_G3D_BUSD		0x1000
#define CLK_CON_DIV_DIV_CLK_G3D_BUSP		0x1804
#define CLK_CON_GAT_CLK_G3D_CMU_G3D_PCLK	0x2000
#define CLK_CON_GAT_CLK_G3D_GPU_CLK		0x2004
#define CLK_CON_GAT_GOUT_G3D_TZPC_PCLK		0x200c
#define CLK_CON_GAT_GOUT_G3D_GRAY2BIN_CLK	0x2010
#define CLK_CON_GAT_GOUT_G3D_BUSD_CLK		0x2024
#define CLK_CON_GAT_GOUT_G3D_BUSP_CLK		0x2028
#define CLK_CON_GAT_GOUT_G3D_SYSREG_PCLK	0x202c

static const unsigned long g3d_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_G3D,
	PLL_CON0_PLL_G3D,
	PLL_CON3_PLL_G3D,
	PLL_CON0_MUX_CLKCMU_G3D_SWITCH_USER,
	CLK_CON_MUX_MUX_CLK_G3D_BUSD,
	CLK_CON_DIV_DIV_CLK_G3D_BUSP,
	CLK_CON_GAT_CLK_G3D_CMU_G3D_PCLK,
	CLK_CON_GAT_CLK_G3D_GPU_CLK,
	CLK_CON_GAT_GOUT_G3D_TZPC_PCLK,
	CLK_CON_GAT_GOUT_G3D_GRAY2BIN_CLK,
	CLK_CON_GAT_GOUT_G3D_BUSD_CLK,
	CLK_CON_GAT_GOUT_G3D_BUSP_CLK,
	CLK_CON_GAT_GOUT_G3D_SYSREG_PCLK,
};

/* List of parent clocks for Muxes in CMU_G3D */
PNAME(mout_g3d_pll_p)		= { "oscclk", "fout_g3d_pll" };
PNAME(mout_g3d_switch_user_p)	= { "oscclk", "dout_g3d_switch" };
PNAME(mout_g3d_busd_p)		= { "mout_g3d_pll", "mout_g3d_switch_user" };

/*
 * Do not provide PLL table to PLL_G3D, as MANUAL_PLL_CTRL bit is not set
 * for that PLL by default, so set_rate operation would fail.
 */
static const struct samsung_pll_clock g3d_pll_clks[] __initconst = {
	PLL(pll_0818x, CLK_FOUT_G3D_PLL, "fout_g3d_pll", "oscclk",
	    PLL_LOCKTIME_PLL_G3D, PLL_CON3_PLL_G3D, NULL),
};

static const struct samsung_mux_clock g3d_mux_clks[] __initconst = {
	MUX(CLK_MOUT_G3D_PLL, "mout_g3d_pll", mout_g3d_pll_p,
	    PLL_CON0_PLL_G3D, 4, 1),
	MUX(CLK_MOUT_G3D_SWITCH_USER, "mout_g3d_switch_user",
	    mout_g3d_switch_user_p,
	    PLL_CON0_MUX_CLKCMU_G3D_SWITCH_USER, 4, 1),
	MUX(CLK_MOUT_G3D_BUSD, "mout_g3d_busd", mout_g3d_busd_p,
	    CLK_CON_MUX_MUX_CLK_G3D_BUSD, 0, 1),
};

static const struct samsung_div_clock g3d_div_clks[] __initconst = {
	DIV(CLK_DOUT_G3D_BUSP, "dout_g3d_busp", "mout_g3d_busd",
	    CLK_CON_DIV_DIV_CLK_G3D_BUSP, 0, 3),
};

static const struct samsung_gate_clock g3d_gate_clks[] __initconst = {
	GATE(CLK_GOUT_G3D_CMU_G3D_PCLK, "gout_g3d_cmu_g3d_pclk",
	     "dout_g3d_busp",
	     CLK_CON_GAT_CLK_G3D_CMU_G3D_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_G3D_GPU_CLK, "gout_g3d_gpu_clk", "mout_g3d_busd",
	     CLK_CON_GAT_CLK_G3D_GPU_CLK, 21, 0, 0),
	GATE(CLK_GOUT_G3D_TZPC_PCLK, "gout_g3d_tzpc_pclk", "dout_g3d_busp",
	     CLK_CON_GAT_GOUT_G3D_TZPC_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_G3D_GRAY2BIN_CLK, "gout_g3d_gray2bin_clk",
	     "mout_g3d_busd",
	     CLK_CON_GAT_GOUT_G3D_GRAY2BIN_CLK, 21, 0, 0),
	GATE(CLK_GOUT_G3D_BUSD_CLK, "gout_g3d_busd_clk", "mout_g3d_busd",
	     CLK_CON_GAT_GOUT_G3D_BUSD_CLK, 21, 0, 0),
	GATE(CLK_GOUT_G3D_BUSP_CLK, "gout_g3d_busp_clk", "dout_g3d_busp",
	     CLK_CON_GAT_GOUT_G3D_BUSP_CLK, 21, 0, 0),
	GATE(CLK_GOUT_G3D_SYSREG_PCLK, "gout_g3d_sysreg_pclk", "dout_g3d_busp",
	     CLK_CON_GAT_GOUT_G3D_SYSREG_PCLK, 21, 0, 0),
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
	.nr_clk_ids		= CLKS_NR_G3D,
	.clk_regs		= g3d_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(g3d_clk_regs),
	.clk_name		= "dout_g3d_switch",
};

/* ---- CMU_HSI ------------------------------------------------------------- */

/* Register Offset definitions for CMU_HSI (0x13400000) */
#define PLL_CON0_MUX_CLKCMU_HSI_BUS_USER			0x0600
#define PLL_CON0_MUX_CLKCMU_HSI_MMC_CARD_USER			0x0610
#define PLL_CON0_MUX_CLKCMU_HSI_USB20DRD_USER			0x0620
#define CLK_CON_MUX_MUX_CLK_HSI_RTC				0x1000
#define CLK_CON_GAT_CLK_HSI_CMU_HSI_PCLK			0x2000
#define CLK_CON_GAT_HSI_USB20DRD_TOP_I_RTC_CLK__ALV		0x2008
#define CLK_CON_GAT_HSI_USB20DRD_TOP_I_REF_CLK_50		0x200c
#define CLK_CON_GAT_HSI_USB20DRD_TOP_I_PHY_REFCLK_26		0x2010
#define CLK_CON_GAT_GOUT_HSI_GPIO_HSI_PCLK			0x2018
#define CLK_CON_GAT_GOUT_HSI_MMC_CARD_I_ACLK			0x2024
#define CLK_CON_GAT_GOUT_HSI_MMC_CARD_SDCLKIN			0x2028
#define CLK_CON_GAT_GOUT_HSI_PPMU_ACLK				0x202c
#define CLK_CON_GAT_GOUT_HSI_PPMU_PCLK				0x2030
#define CLK_CON_GAT_GOUT_HSI_SYSREG_HSI_PCLK			0x2038
#define CLK_CON_GAT_GOUT_HSI_USB20DRD_TOP_ACLK_PHYCTRL_20	0x203c
#define CLK_CON_GAT_GOUT_HSI_USB20DRD_TOP_BUS_CLK_EARLY		0x2040

static const unsigned long hsi_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_HSI_BUS_USER,
	PLL_CON0_MUX_CLKCMU_HSI_MMC_CARD_USER,
	PLL_CON0_MUX_CLKCMU_HSI_USB20DRD_USER,
	CLK_CON_MUX_MUX_CLK_HSI_RTC,
	CLK_CON_GAT_CLK_HSI_CMU_HSI_PCLK,
	CLK_CON_GAT_HSI_USB20DRD_TOP_I_RTC_CLK__ALV,
	CLK_CON_GAT_HSI_USB20DRD_TOP_I_REF_CLK_50,
	CLK_CON_GAT_HSI_USB20DRD_TOP_I_PHY_REFCLK_26,
	CLK_CON_GAT_GOUT_HSI_GPIO_HSI_PCLK,
	CLK_CON_GAT_GOUT_HSI_MMC_CARD_I_ACLK,
	CLK_CON_GAT_GOUT_HSI_MMC_CARD_SDCLKIN,
	CLK_CON_GAT_GOUT_HSI_PPMU_ACLK,
	CLK_CON_GAT_GOUT_HSI_PPMU_PCLK,
	CLK_CON_GAT_GOUT_HSI_SYSREG_HSI_PCLK,
	CLK_CON_GAT_GOUT_HSI_USB20DRD_TOP_ACLK_PHYCTRL_20,
	CLK_CON_GAT_GOUT_HSI_USB20DRD_TOP_BUS_CLK_EARLY,
};

/* List of parent clocks for Muxes in CMU_HSI */
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
	/* TODO: Should be enabled in corresponding driver */
	GATE(CLK_GOUT_HSI_CMU_HSI_PCLK, "gout_hsi_cmu_hsi_pclk",
	     "mout_hsi_bus_user",
	     CLK_CON_GAT_CLK_HSI_CMU_HSI_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_USB_RTC_CLK, "gout_usb_rtc", "mout_hsi_rtc",
	     CLK_CON_GAT_HSI_USB20DRD_TOP_I_RTC_CLK__ALV, 21, 0, 0),
	GATE(CLK_GOUT_USB_REF_CLK, "gout_usb_ref", "mout_hsi_usb20drd_user",
	     CLK_CON_GAT_HSI_USB20DRD_TOP_I_REF_CLK_50, 21, 0, 0),
	GATE(CLK_GOUT_USB_PHY_REF_CLK, "gout_usb_phy_ref", "oscclk",
	     CLK_CON_GAT_HSI_USB20DRD_TOP_I_PHY_REFCLK_26, 21, 0, 0),
	/* TODO: Should be enabled in GPIO driver (or made CLK_IS_CRITICAL) */
	GATE(CLK_GOUT_GPIO_HSI_PCLK, "gout_gpio_hsi_pclk", "mout_hsi_bus_user",
	     CLK_CON_GAT_GOUT_HSI_GPIO_HSI_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_MMC_CARD_ACLK, "gout_mmc_card_aclk", "mout_hsi_bus_user",
	     CLK_CON_GAT_GOUT_HSI_MMC_CARD_I_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_MMC_CARD_SDCLKIN, "gout_mmc_card_sdclkin",
	     "mout_hsi_mmc_card_user",
	     CLK_CON_GAT_GOUT_HSI_MMC_CARD_SDCLKIN, 21, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_HSI_PPMU_ACLK, "gout_hsi_ppmu_aclk", "mout_hsi_bus_user",
	     CLK_CON_GAT_GOUT_HSI_PPMU_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_HSI_PPMU_PCLK, "gout_hsi_ppmu_pclk", "mout_hsi_bus_user",
	     CLK_CON_GAT_GOUT_HSI_PPMU_PCLK, 21, 0, 0),
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
	.nr_clk_ids		= CLKS_NR_HSI,
	.clk_regs		= hsi_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(hsi_clk_regs),
	.clk_name		= "dout_hsi_bus",
};

/* ---- CMU_IS -------------------------------------------------------------- */

#define PLL_CON0_MUX_CLKCMU_IS_BUS_USER		0x0600
#define PLL_CON0_MUX_CLKCMU_IS_GDC_USER		0x0610
#define PLL_CON0_MUX_CLKCMU_IS_ITP_USER		0x0620
#define PLL_CON0_MUX_CLKCMU_IS_VRA_USER		0x0630
#define CLK_CON_DIV_DIV_CLK_IS_BUSP		0x1800
#define CLK_CON_GAT_CLK_IS_CMU_IS_PCLK		0x2000
#define CLK_CON_GAT_GOUT_IS_CSIS0_ACLK		0x2040
#define CLK_CON_GAT_GOUT_IS_CSIS1_ACLK		0x2044
#define CLK_CON_GAT_GOUT_IS_CSIS2_ACLK		0x2048
#define CLK_CON_GAT_GOUT_IS_TZPC_PCLK		0x204c
#define CLK_CON_GAT_GOUT_IS_CLK_CSIS_DMA	0x2050
#define CLK_CON_GAT_GOUT_IS_CLK_GDC		0x2054
#define CLK_CON_GAT_GOUT_IS_CLK_IPP		0x2058
#define CLK_CON_GAT_GOUT_IS_CLK_ITP		0x205c
#define CLK_CON_GAT_GOUT_IS_CLK_MCSC		0x2060
#define CLK_CON_GAT_GOUT_IS_CLK_VRA		0x2064
#define CLK_CON_GAT_GOUT_IS_PPMU_IS0_ACLK	0x2074
#define CLK_CON_GAT_GOUT_IS_PPMU_IS0_PCLK	0x2078
#define CLK_CON_GAT_GOUT_IS_PPMU_IS1_ACLK	0x207c
#define CLK_CON_GAT_GOUT_IS_PPMU_IS1_PCLK	0x2080
#define CLK_CON_GAT_GOUT_IS_SYSMMU_IS0_CLK_S1	0x2098
#define CLK_CON_GAT_GOUT_IS_SYSMMU_IS1_CLK_S1	0x209c
#define CLK_CON_GAT_GOUT_IS_SYSREG_PCLK		0x20a0

static const unsigned long is_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_IS_BUS_USER,
	PLL_CON0_MUX_CLKCMU_IS_GDC_USER,
	PLL_CON0_MUX_CLKCMU_IS_ITP_USER,
	PLL_CON0_MUX_CLKCMU_IS_VRA_USER,
	CLK_CON_DIV_DIV_CLK_IS_BUSP,
	CLK_CON_GAT_CLK_IS_CMU_IS_PCLK,
	CLK_CON_GAT_GOUT_IS_CSIS0_ACLK,
	CLK_CON_GAT_GOUT_IS_CSIS1_ACLK,
	CLK_CON_GAT_GOUT_IS_CSIS2_ACLK,
	CLK_CON_GAT_GOUT_IS_TZPC_PCLK,
	CLK_CON_GAT_GOUT_IS_CLK_CSIS_DMA,
	CLK_CON_GAT_GOUT_IS_CLK_GDC,
	CLK_CON_GAT_GOUT_IS_CLK_IPP,
	CLK_CON_GAT_GOUT_IS_CLK_ITP,
	CLK_CON_GAT_GOUT_IS_CLK_MCSC,
	CLK_CON_GAT_GOUT_IS_CLK_VRA,
	CLK_CON_GAT_GOUT_IS_PPMU_IS0_ACLK,
	CLK_CON_GAT_GOUT_IS_PPMU_IS0_PCLK,
	CLK_CON_GAT_GOUT_IS_PPMU_IS1_ACLK,
	CLK_CON_GAT_GOUT_IS_PPMU_IS1_PCLK,
	CLK_CON_GAT_GOUT_IS_SYSMMU_IS0_CLK_S1,
	CLK_CON_GAT_GOUT_IS_SYSMMU_IS1_CLK_S1,
	CLK_CON_GAT_GOUT_IS_SYSREG_PCLK,
};

/* List of parent clocks for Muxes in CMU_IS */
PNAME(mout_is_bus_user_p)	= { "oscclk", "dout_is_bus" };
PNAME(mout_is_itp_user_p)	= { "oscclk", "dout_is_itp" };
PNAME(mout_is_vra_user_p)	= { "oscclk", "dout_is_vra" };
PNAME(mout_is_gdc_user_p)	= { "oscclk", "dout_is_gdc" };

static const struct samsung_mux_clock is_mux_clks[] __initconst = {
	MUX(CLK_MOUT_IS_BUS_USER, "mout_is_bus_user", mout_is_bus_user_p,
	    PLL_CON0_MUX_CLKCMU_IS_BUS_USER, 4, 1),
	MUX(CLK_MOUT_IS_ITP_USER, "mout_is_itp_user", mout_is_itp_user_p,
	    PLL_CON0_MUX_CLKCMU_IS_ITP_USER, 4, 1),
	MUX(CLK_MOUT_IS_VRA_USER, "mout_is_vra_user", mout_is_vra_user_p,
	    PLL_CON0_MUX_CLKCMU_IS_VRA_USER, 4, 1),
	MUX(CLK_MOUT_IS_GDC_USER, "mout_is_gdc_user", mout_is_gdc_user_p,
	    PLL_CON0_MUX_CLKCMU_IS_GDC_USER, 4, 1),
};

static const struct samsung_div_clock is_div_clks[] __initconst = {
	DIV(CLK_DOUT_IS_BUSP, "dout_is_busp", "mout_is_bus_user",
	    CLK_CON_DIV_DIV_CLK_IS_BUSP, 0, 2),
};

static const struct samsung_gate_clock is_gate_clks[] __initconst = {
	/* TODO: Should be enabled in IS driver */
	GATE(CLK_GOUT_IS_CMU_IS_PCLK, "gout_is_cmu_is_pclk", "dout_is_busp",
	     CLK_CON_GAT_CLK_IS_CMU_IS_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_IS_CSIS0_ACLK, "gout_is_csis0_aclk", "mout_is_bus_user",
	     CLK_CON_GAT_GOUT_IS_CSIS0_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_IS_CSIS1_ACLK, "gout_is_csis1_aclk", "mout_is_bus_user",
	     CLK_CON_GAT_GOUT_IS_CSIS1_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_IS_CSIS2_ACLK, "gout_is_csis2_aclk", "mout_is_bus_user",
	     CLK_CON_GAT_GOUT_IS_CSIS2_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_IS_TZPC_PCLK, "gout_is_tzpc_pclk", "dout_is_busp",
	     CLK_CON_GAT_GOUT_IS_TZPC_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_IS_CSIS_DMA_CLK, "gout_is_csis_dma_clk",
	     "mout_is_bus_user",
	     CLK_CON_GAT_GOUT_IS_CLK_CSIS_DMA, 21, 0, 0),
	GATE(CLK_GOUT_IS_GDC_CLK, "gout_is_gdc_clk", "mout_is_gdc_user",
	     CLK_CON_GAT_GOUT_IS_CLK_GDC, 21, 0, 0),
	GATE(CLK_GOUT_IS_IPP_CLK, "gout_is_ipp_clk", "mout_is_bus_user",
	     CLK_CON_GAT_GOUT_IS_CLK_IPP, 21, 0, 0),
	GATE(CLK_GOUT_IS_ITP_CLK, "gout_is_itp_clk", "mout_is_itp_user",
	     CLK_CON_GAT_GOUT_IS_CLK_ITP, 21, 0, 0),
	GATE(CLK_GOUT_IS_MCSC_CLK, "gout_is_mcsc_clk", "mout_is_itp_user",
	     CLK_CON_GAT_GOUT_IS_CLK_MCSC, 21, 0, 0),
	GATE(CLK_GOUT_IS_VRA_CLK, "gout_is_vra_clk", "mout_is_vra_user",
	     CLK_CON_GAT_GOUT_IS_CLK_VRA, 21, 0, 0),
	GATE(CLK_GOUT_IS_PPMU_IS0_ACLK, "gout_is_ppmu_is0_aclk",
	     "mout_is_bus_user",
	     CLK_CON_GAT_GOUT_IS_PPMU_IS0_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_IS_PPMU_IS0_PCLK, "gout_is_ppmu_is0_pclk", "dout_is_busp",
	     CLK_CON_GAT_GOUT_IS_PPMU_IS0_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_IS_PPMU_IS1_ACLK, "gout_is_ppmu_is1_aclk",
	     "mout_is_itp_user",
	     CLK_CON_GAT_GOUT_IS_PPMU_IS1_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_IS_PPMU_IS1_PCLK, "gout_is_ppmu_is1_pclk", "dout_is_busp",
	     CLK_CON_GAT_GOUT_IS_PPMU_IS1_PCLK, 21, 0, 0),
	GATE(CLK_GOUT_IS_SYSMMU_IS0_CLK, "gout_is_sysmmu_is0_clk",
	     "mout_is_bus_user",
	     CLK_CON_GAT_GOUT_IS_SYSMMU_IS0_CLK_S1, 21, 0, 0),
	GATE(CLK_GOUT_IS_SYSMMU_IS1_CLK, "gout_is_sysmmu_is1_clk",
	     "mout_is_itp_user",
	     CLK_CON_GAT_GOUT_IS_SYSMMU_IS1_CLK_S1, 21, 0, 0),
	GATE(CLK_GOUT_IS_SYSREG_PCLK, "gout_is_sysreg_pclk", "dout_is_busp",
	     CLK_CON_GAT_GOUT_IS_SYSREG_PCLK, 21, 0, 0),
};

static const struct samsung_cmu_info is_cmu_info __initconst = {
	.mux_clks		= is_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(is_mux_clks),
	.div_clks		= is_div_clks,
	.nr_div_clks		= ARRAY_SIZE(is_div_clks),
	.gate_clks		= is_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(is_gate_clks),
	.nr_clk_ids		= CLKS_NR_IS,
	.clk_regs		= is_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(is_clk_regs),
	.clk_name		= "dout_is_bus",
};

/* ---- CMU_MFCMSCL --------------------------------------------------------- */

#define PLL_CON0_MUX_CLKCMU_MFCMSCL_JPEG_USER		0x0600
#define PLL_CON0_MUX_CLKCMU_MFCMSCL_M2M_USER		0x0610
#define PLL_CON0_MUX_CLKCMU_MFCMSCL_MCSC_USER		0x0620
#define PLL_CON0_MUX_CLKCMU_MFCMSCL_MFC_USER		0x0630
#define CLK_CON_DIV_DIV_CLK_MFCMSCL_BUSP		0x1800
#define CLK_CON_GAT_CLK_MFCMSCL_CMU_MFCMSCL_PCLK	0x2000
#define CLK_CON_GAT_GOUT_MFCMSCL_TZPC_PCLK		0x2038
#define CLK_CON_GAT_GOUT_MFCMSCL_JPEG_ACLK		0x203c
#define CLK_CON_GAT_GOUT_MFCMSCL_M2M_ACLK		0x2048
#define CLK_CON_GAT_GOUT_MFCMSCL_MCSC_I_CLK		0x204c
#define CLK_CON_GAT_GOUT_MFCMSCL_MFC_ACLK		0x2050
#define CLK_CON_GAT_GOUT_MFCMSCL_PPMU_ACLK		0x2054
#define CLK_CON_GAT_GOUT_MFCMSCL_PPMU_PCLK		0x2058
#define CLK_CON_GAT_GOUT_MFCMSCL_SYSMMU_CLK_S1		0x2074
#define CLK_CON_GAT_GOUT_MFCMSCL_SYSREG_PCLK		0x2078

static const unsigned long mfcmscl_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_MFCMSCL_JPEG_USER,
	PLL_CON0_MUX_CLKCMU_MFCMSCL_M2M_USER,
	PLL_CON0_MUX_CLKCMU_MFCMSCL_MCSC_USER,
	PLL_CON0_MUX_CLKCMU_MFCMSCL_MFC_USER,
	CLK_CON_DIV_DIV_CLK_MFCMSCL_BUSP,
	CLK_CON_GAT_CLK_MFCMSCL_CMU_MFCMSCL_PCLK,
	CLK_CON_GAT_GOUT_MFCMSCL_TZPC_PCLK,
	CLK_CON_GAT_GOUT_MFCMSCL_JPEG_ACLK,
	CLK_CON_GAT_GOUT_MFCMSCL_M2M_ACLK,
	CLK_CON_GAT_GOUT_MFCMSCL_MCSC_I_CLK,
	CLK_CON_GAT_GOUT_MFCMSCL_MFC_ACLK,
	CLK_CON_GAT_GOUT_MFCMSCL_PPMU_ACLK,
	CLK_CON_GAT_GOUT_MFCMSCL_PPMU_PCLK,
	CLK_CON_GAT_GOUT_MFCMSCL_SYSMMU_CLK_S1,
	CLK_CON_GAT_GOUT_MFCMSCL_SYSREG_PCLK,
};

/* List of parent clocks for Muxes in CMU_MFCMSCL */
PNAME(mout_mfcmscl_mfc_user_p)	= { "oscclk", "dout_mfcmscl_mfc" };
PNAME(mout_mfcmscl_m2m_user_p)	= { "oscclk", "dout_mfcmscl_m2m" };
PNAME(mout_mfcmscl_mcsc_user_p)	= { "oscclk", "dout_mfcmscl_mcsc" };
PNAME(mout_mfcmscl_jpeg_user_p)	= { "oscclk", "dout_mfcmscl_jpeg" };

static const struct samsung_mux_clock mfcmscl_mux_clks[] __initconst = {
	MUX(CLK_MOUT_MFCMSCL_MFC_USER, "mout_mfcmscl_mfc_user",
	    mout_mfcmscl_mfc_user_p,
	    PLL_CON0_MUX_CLKCMU_MFCMSCL_MFC_USER, 4, 1),
	MUX(CLK_MOUT_MFCMSCL_M2M_USER, "mout_mfcmscl_m2m_user",
	    mout_mfcmscl_m2m_user_p,
	    PLL_CON0_MUX_CLKCMU_MFCMSCL_M2M_USER, 4, 1),
	MUX(CLK_MOUT_MFCMSCL_MCSC_USER, "mout_mfcmscl_mcsc_user",
	    mout_mfcmscl_mcsc_user_p,
	    PLL_CON0_MUX_CLKCMU_MFCMSCL_MCSC_USER, 4, 1),
	MUX(CLK_MOUT_MFCMSCL_JPEG_USER, "mout_mfcmscl_jpeg_user",
	    mout_mfcmscl_jpeg_user_p,
	    PLL_CON0_MUX_CLKCMU_MFCMSCL_JPEG_USER, 4, 1),
};

static const struct samsung_div_clock mfcmscl_div_clks[] __initconst = {
	DIV(CLK_DOUT_MFCMSCL_BUSP, "dout_mfcmscl_busp", "mout_mfcmscl_mfc_user",
	    CLK_CON_DIV_DIV_CLK_MFCMSCL_BUSP, 0, 3),
};

static const struct samsung_gate_clock mfcmscl_gate_clks[] __initconst = {
	/* TODO: Should be enabled in MFC driver */
	GATE(CLK_GOUT_MFCMSCL_CMU_MFCMSCL_PCLK, "gout_mfcmscl_cmu_mfcmscl_pclk",
	     "dout_mfcmscl_busp", CLK_CON_GAT_CLK_MFCMSCL_CMU_MFCMSCL_PCLK,
	     21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_MFCMSCL_TZPC_PCLK, "gout_mfcmscl_tzpc_pclk",
	     "dout_mfcmscl_busp", CLK_CON_GAT_GOUT_MFCMSCL_TZPC_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MFCMSCL_JPEG_ACLK, "gout_mfcmscl_jpeg_aclk",
	     "mout_mfcmscl_jpeg_user", CLK_CON_GAT_GOUT_MFCMSCL_JPEG_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MFCMSCL_M2M_ACLK, "gout_mfcmscl_m2m_aclk",
	     "mout_mfcmscl_m2m_user", CLK_CON_GAT_GOUT_MFCMSCL_M2M_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MFCMSCL_MCSC_CLK, "gout_mfcmscl_mcsc_clk",
	     "mout_mfcmscl_mcsc_user", CLK_CON_GAT_GOUT_MFCMSCL_MCSC_I_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MFCMSCL_MFC_ACLK, "gout_mfcmscl_mfc_aclk",
	     "mout_mfcmscl_mfc_user", CLK_CON_GAT_GOUT_MFCMSCL_MFC_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MFCMSCL_PPMU_ACLK, "gout_mfcmscl_ppmu_aclk",
	     "mout_mfcmscl_mfc_user", CLK_CON_GAT_GOUT_MFCMSCL_PPMU_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MFCMSCL_PPMU_PCLK, "gout_mfcmscl_ppmu_pclk",
	     "dout_mfcmscl_busp", CLK_CON_GAT_GOUT_MFCMSCL_PPMU_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_MFCMSCL_SYSMMU_CLK, "gout_mfcmscl_sysmmu_clk",
	     "mout_mfcmscl_mfc_user", CLK_CON_GAT_GOUT_MFCMSCL_SYSMMU_CLK_S1,
	     21, 0, 0),
	GATE(CLK_GOUT_MFCMSCL_SYSREG_PCLK, "gout_mfcmscl_sysreg_pclk",
	     "dout_mfcmscl_busp", CLK_CON_GAT_GOUT_MFCMSCL_SYSREG_PCLK,
	     21, 0, 0),
};

static const struct samsung_cmu_info mfcmscl_cmu_info __initconst = {
	.mux_clks		= mfcmscl_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(mfcmscl_mux_clks),
	.div_clks		= mfcmscl_div_clks,
	.nr_div_clks		= ARRAY_SIZE(mfcmscl_div_clks),
	.gate_clks		= mfcmscl_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(mfcmscl_gate_clks),
	.nr_clk_ids		= CLKS_NR_MFCMSCL,
	.clk_regs		= mfcmscl_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(mfcmscl_clk_regs),
	.clk_name		= "dout_mfcmscl_mfc",
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
#define CLK_CON_GAT_GOUT_PERI_BUSIF_TMU_PCLK	0x2018
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
	CLK_CON_GAT_GOUT_PERI_BUSIF_TMU_PCLK,
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
	MUX_F(CLK_MOUT_PERI_SPI_USER, "mout_peri_spi_user",
	      mout_peri_spi_user_p, PLL_CON0_MUX_CLKCMU_PERI_SPI_USER, 4, 1,
	      CLK_SET_RATE_PARENT, 0),
};

static const struct samsung_div_clock peri_div_clks[] __initconst = {
	DIV(CLK_DOUT_PERI_HSI2C0, "dout_peri_hsi2c0", "gout_peri_hsi2c0",
	    CLK_CON_DIV_DIV_CLK_PERI_HSI2C_0, 0, 5),
	DIV(CLK_DOUT_PERI_HSI2C1, "dout_peri_hsi2c1", "gout_peri_hsi2c1",
	    CLK_CON_DIV_DIV_CLK_PERI_HSI2C_1, 0, 5),
	DIV(CLK_DOUT_PERI_HSI2C2, "dout_peri_hsi2c2", "gout_peri_hsi2c2",
	    CLK_CON_DIV_DIV_CLK_PERI_HSI2C_2, 0, 5),
	DIV_F(CLK_DOUT_PERI_SPI0, "dout_peri_spi0", "mout_peri_spi_user",
	      CLK_CON_DIV_DIV_CLK_PERI_SPI_0, 0, 5, CLK_SET_RATE_PARENT, 0),
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
	     CLK_CON_GAT_GOUT_PERI_SPI_0_IPCLK, 21, CLK_SET_RATE_PARENT, 0),
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
	/* TODO: Should be enabled in GPIO driver (or made CLK_IS_CRITICAL) */
	GATE(CLK_GOUT_GPIO_PERI_PCLK, "gout_gpio_peri_pclk",
	     "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_GPIO_PERI_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_BUSIF_TMU_PCLK, "gout_busif_tmu_pclk",
	     "mout_peri_bus_user",
	     CLK_CON_GAT_GOUT_PERI_BUSIF_TMU_PCLK, 21, 0, 0),
};

static const struct samsung_cmu_info peri_cmu_info __initconst = {
	.mux_clks		= peri_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(peri_mux_clks),
	.div_clks		= peri_div_clks,
	.nr_div_clks		= ARRAY_SIZE(peri_div_clks),
	.gate_clks		= peri_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(peri_gate_clks),
	.nr_clk_ids		= CLKS_NR_PERI,
	.clk_regs		= peri_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(peri_clk_regs),
	.clk_name		= "dout_peri_bus",
};

static void __init exynos850_cmu_peri_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &peri_cmu_info);
}

/* Register CMU_PERI early, as it's needed for MCT timer */
CLK_OF_DECLARE(exynos850_cmu_peri, "samsung,exynos850-cmu-peri",
	       exynos850_cmu_peri_init);

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
#define CLK_CON_GAT_GOUT_CORE_GPIO_CORE_PCLK	0x2044
#define CLK_CON_GAT_GOUT_CORE_MMC_EMBD_I_ACLK	0x20e8
#define CLK_CON_GAT_GOUT_CORE_MMC_EMBD_SDCLKIN	0x20ec
#define CLK_CON_GAT_GOUT_CORE_PDMA_ACLK		0x20f0
#define CLK_CON_GAT_GOUT_CORE_SPDMA_ACLK	0x2124
#define CLK_CON_GAT_GOUT_CORE_SSS_I_ACLK	0x2128
#define CLK_CON_GAT_GOUT_CORE_SSS_I_PCLK	0x212c
#define CLK_CON_GAT_GOUT_CORE_SYSREG_CORE_PCLK	0x2130

static const unsigned long core_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_CORE_BUS_USER,
	PLL_CON0_MUX_CLKCMU_CORE_CCI_USER,
	PLL_CON0_MUX_CLKCMU_CORE_MMC_EMBD_USER,
	PLL_CON0_MUX_CLKCMU_CORE_SSS_USER,
	CLK_CON_MUX_MUX_CLK_CORE_GIC,
	CLK_CON_DIV_DIV_CLK_CORE_BUSP,
	CLK_CON_GAT_GOUT_CORE_CCI_550_ACLK,
	CLK_CON_GAT_GOUT_CORE_GIC_CLK,
	CLK_CON_GAT_GOUT_CORE_GPIO_CORE_PCLK,
	CLK_CON_GAT_GOUT_CORE_MMC_EMBD_I_ACLK,
	CLK_CON_GAT_GOUT_CORE_MMC_EMBD_SDCLKIN,
	CLK_CON_GAT_GOUT_CORE_PDMA_ACLK,
	CLK_CON_GAT_GOUT_CORE_SPDMA_ACLK,
	CLK_CON_GAT_GOUT_CORE_SSS_I_ACLK,
	CLK_CON_GAT_GOUT_CORE_SSS_I_PCLK,
	CLK_CON_GAT_GOUT_CORE_SYSREG_CORE_PCLK,
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
	/* CCI (interconnect) clock must be always running */
	GATE(CLK_GOUT_CCI_ACLK, "gout_cci_aclk", "mout_core_cci_user",
	     CLK_CON_GAT_GOUT_CORE_CCI_550_ACLK, 21, CLK_IS_CRITICAL, 0),
	/* GIC (interrupt controller) clock must be always running */
	GATE(CLK_GOUT_GIC_CLK, "gout_gic_clk", "mout_core_gic",
	     CLK_CON_GAT_GOUT_CORE_GIC_CLK, 21, CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_MMC_EMBD_ACLK, "gout_mmc_embd_aclk", "dout_core_busp",
	     CLK_CON_GAT_GOUT_CORE_MMC_EMBD_I_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_MMC_EMBD_SDCLKIN, "gout_mmc_embd_sdclkin",
	     "mout_core_mmc_embd_user", CLK_CON_GAT_GOUT_CORE_MMC_EMBD_SDCLKIN,
	     21, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PDMA_CORE_ACLK, "gout_pdma_core_aclk",
	     "mout_core_bus_user", CLK_CON_GAT_GOUT_CORE_PDMA_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_SPDMA_CORE_ACLK, "gout_spdma_core_aclk",
	     "mout_core_bus_user", CLK_CON_GAT_GOUT_CORE_SPDMA_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_SSS_ACLK, "gout_sss_aclk", "mout_core_sss_user",
	     CLK_CON_GAT_GOUT_CORE_SSS_I_ACLK, 21, 0, 0),
	GATE(CLK_GOUT_SSS_PCLK, "gout_sss_pclk", "dout_core_busp",
	     CLK_CON_GAT_GOUT_CORE_SSS_I_PCLK, 21, 0, 0),
	/* TODO: Should be enabled in GPIO driver (or made CLK_IS_CRITICAL) */
	GATE(CLK_GOUT_GPIO_CORE_PCLK, "gout_gpio_core_pclk", "dout_core_busp",
	     CLK_CON_GAT_GOUT_CORE_GPIO_CORE_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_SYSREG_CORE_PCLK, "gout_sysreg_core_pclk",
	     "dout_core_busp",
	     CLK_CON_GAT_GOUT_CORE_SYSREG_CORE_PCLK, 21, 0, 0),
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

/* List of parent clocks for Muxes in CMU_DPU */
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
	/* TODO: Should be enabled in DSIM driver */
	GATE(CLK_GOUT_DPU_CMU_DPU_PCLK, "gout_dpu_cmu_dpu_pclk",
	     "dout_dpu_busp",
	     CLK_CON_GAT_CLK_DPU_CMU_DPU_PCLK, 21, CLK_IGNORE_UNUSED, 0),
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
	.nr_clk_ids		= CLKS_NR_DPU,
	.clk_regs		= dpu_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(dpu_clk_regs),
	.clk_name		= "dout_dpu",
};

/* ---- platform_driver ----------------------------------------------------- */

static int __init exynos850_cmu_probe(struct platform_device *pdev)
{
	const struct samsung_cmu_info *info;
	struct device *dev = &pdev->dev;

	info = of_device_get_match_data(dev);
	exynos_arm64_register_cmu(dev, dev->of_node, info);

	return 0;
}

static const struct of_device_id exynos850_cmu_of_match[] = {
	{
		.compatible = "samsung,exynos850-cmu-apm",
		.data = &apm_cmu_info,
	}, {
		.compatible = "samsung,exynos850-cmu-aud",
		.data = &aud_cmu_info,
	}, {
		.compatible = "samsung,exynos850-cmu-cmgp",
		.data = &cmgp_cmu_info,
	}, {
		.compatible = "samsung,exynos850-cmu-g3d",
		.data = &g3d_cmu_info,
	}, {
		.compatible = "samsung,exynos850-cmu-hsi",
		.data = &hsi_cmu_info,
	}, {
		.compatible = "samsung,exynos850-cmu-is",
		.data = &is_cmu_info,
	}, {
		.compatible = "samsung,exynos850-cmu-mfcmscl",
		.data = &mfcmscl_cmu_info,
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
