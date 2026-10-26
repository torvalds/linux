// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2026 Aiden Isik <aidenisik+git@member.fsf.org>
 * Author: Aiden Isik <aidenisik+git@member.fsf.org>
 *
 * Common Clock Framework support for Exynos5515 SoC.
 */

#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <dt-bindings/clock/samsung,exynos5515-cmu.h>

#include "clk.h"
#include "clk-exynos-arm64.h"
#include "clk-pll.h"

/* NOTE: Must be equal to the last clock ID increased by one */
#define CLKS_NR_TOP	(CLK_GOUT_CMU_PERI_UART + 1)
#define CLKS_NR_PERI	(CLK_GOUT_PERI_USI00_I2C + 1)
#define CLKS_NR_FSYS	(CLK_GOUT_FSYS_XIU_P_ACLK + 1)

#define EXYNOS5515_GATE_DBG_OFFSET	0x4000
#define EXYNOS5515_DRCG_EN_OFFSET	0x0104
#define EXYNOS5515_MEMCLK_OFFSET	0x0108

/* ---- CMU_TOP ------------------------------------------------------------- */

/*
 * CMU_TOP:	0x15410000
 * SYSREG_TOP:	0x15430000
 */

/* Register Offset definitions for CMU_TOP */
#define PLL_LOCKTIME_PLL_AUD			0x0000
#define PLL_LOCKTIME_PLL_SHARED0		0x0004
#define PLL_LOCKTIME_PLL_SHARED1		0x0008
#define PLL_CON3_PLL_AUD			0x010c
#define PLL_CON8_PLL_AUD			0x0120
#define PLL_CON9_PLL_AUD			0x0124
#define PLL_CON3_PLL_SHARED0			0x014c
#define PLL_CON8_PLL_SHARED0			0x0160
#define PLL_CON9_PLL_SHARED0			0x0164
#define PLL_CON3_PLL_SHARED1			0x018c
#define PLL_CON8_PLL_SHARED1			0x01a0
#define PLL_CON9_PLL_SHARED1			0x01a4
#define CMU_CMU_TOP_CONTROLLER_OPTION		0x0800
#define CLK_CON_MUX_MUX_CLKCMU_APM_BUS		0x1000
#define CLK_CON_MUX_MUX_CLKCMU_CORE_BUS		0x1004
#define CLK_CON_MUX_MUX_CLKCMU_CORE_SSS		0x1008
#define CLK_CON_MUX_MUX_CLKCMU_CPUCL0_BUSP	0x100c
#define CLK_CON_MUX_MUX_CLKCMU_CPUCL0_CPU	0x1010
#define CLK_CON_MUX_MUX_CLKCMU_CPUCL0_DBG	0x1014
#define CLK_CON_MUX_MUX_CLKCMU_DPU_AUDIF	0x1018
#define CLK_CON_MUX_MUX_CLKCMU_DPU_AUD_BUS	0x101c
#define CLK_CON_MUX_MUX_CLKCMU_DPU_AUD_CPU	0x1020
#define CLK_CON_MUX_MUX_CLKCMU_DPU_BUS		0x1024
#define CLK_CON_MUX_MUX_CLKCMU_FSYS_BUS		0x1028
#define CLK_CON_MUX_MUX_CLKCMU_FSYS_MMC_CARD	0x102c
#define CLK_CON_MUX_MUX_CLKCMU_FSYS_MMC_EMBD	0x1030
#define CLK_CON_MUX_MUX_CLKCMU_FSYS_USB20DRD	0x1034
#define CLK_CON_MUX_MUX_CLKCMU_G3D_BUS		0x1038
#define CLK_CON_MUX_MUX_CLKCMU_MFC_BUS		0x103c
#define CLK_CON_MUX_MUX_CLKCMU_MIF_BUSP		0x1040
#define CLK_CON_MUX_MUX_CLKCMU_MIF_SWITCH	0x1044
#define CLK_CON_MUX_MUX_CLKCMU_MODEM_SHARED1	0x1048
#define CLK_CON_MUX_MUX_CLKCMU_PERI_BUS		0x104c
#define CLK_CON_MUX_MUX_CLKCMU_PERI_IP		0x1050
#define CLK_CON_MUX_MUX_CLKCMU_PERI_UART	0x1054
#define CLK_CON_MUX_MUX_CMU_CMUREF		0x1058
#define CLK_CON_DIV_AP2CP_SHARED0_PLL_CLK	0x1800
#define CLK_CON_DIV_AP2CP_SHARED1_PLL_CLK	0x1804
#define CLK_CON_DIV_AP2CP_SHARED2_PLL_CLK	0x1808
#define CLK_CON_DIV_CLKCMU_APM_BUS		0x180c
#define CLK_CON_DIV_CLKCMU_CMU_BOOST		0x1810
#define CLK_CON_DIV_CLKCMU_CORE_BUS		0x1814
#define CLK_CON_DIV_CLKCMU_CORE_SSS		0x1818
#define CLK_CON_DIV_CLKCMU_CPUCL0_BUSP		0x181c
#define CLK_CON_DIV_CLKCMU_CPUCL0_CPU		0x1820
#define CLK_CON_DIV_CLKCMU_CPUCL0_DBG		0x1824
#define CLK_CON_DIV_CLKCMU_DPU_AUDIF		0x1828
#define CLK_CON_DIV_CLKCMU_DPU_AUD_BUS		0x182c
#define CLK_CON_DIV_CLKCMU_DPU_AUD_CPU		0x1830
#define CLK_CON_DIV_CLKCMU_DPU_BUS		0x1834
#define CLK_CON_DIV_CLKCMU_FSYS_BUS		0x1838
#define CLK_CON_DIV_CLKCMU_FSYS_MMC_CARD	0x183c
#define CLK_CON_DIV_CLKCMU_FSYS_MMC_EMBD	0x1840
#define CLK_CON_DIV_CLKCMU_FSYS_USB20DRD	0x1844
#define CLK_CON_DIV_CLKCMU_G3D_BUS		0x1848
#define CLK_CON_DIV_CLKCMU_MFC_BUS		0x184c
#define CLK_CON_DIV_CLKCMU_MIF_BUSP		0x1850
#define CLK_CON_DIV_CLKCMU_PERI_BUS		0x1854
#define CLK_CON_DIV_CLKCMU_PERI_IP		0x1858
#define CLK_CON_DIV_CLKCMU_PERI_UART		0x185c
#define CLK_CON_DIV_DIV_CLK_CMU_CMUREF		0x1860
#define CLK_CON_GAT_CLKCMU_MIF_SWITCH		0x2030
#define CLK_CON_GAT_GATE_CLKCMU_APM_BUS		0x2034
#define CLK_CON_GAT_GATE_CLKCMU_CORE_BUS	0x2038
#define CLK_CON_GAT_GATE_CLKCMU_CORE_SSS	0x203c
#define CLK_CON_GAT_GATE_CLKCMU_CPUCL0_BUSP	0x2040
#define CLK_CON_GAT_GATE_CLKCMU_CPUCL0_CPU	0x2044
#define CLK_CON_GAT_GATE_CLKCMU_CPUCL0_DBG	0x2048
#define CLK_CON_GAT_GATE_CLKCMU_DPU_AUDIF	0x204c
#define CLK_CON_GAT_GATE_CLKCMU_DPU_AUD_BUS	0x2050
#define CLK_CON_GAT_GATE_CLKCMU_DPU_AUD_CPU	0x2054
#define CLK_CON_GAT_GATE_CLKCMU_DPU_BUS		0x2058
#define CLK_CON_GAT_GATE_CLKCMU_FSYS_BUS	0x205c
#define CLK_CON_GAT_GATE_CLKCMU_FSYS_MMC_CARD	0x2060
#define CLK_CON_GAT_GATE_CLKCMU_FSYS_MMC_EMBD	0x2064
#define CLK_CON_GAT_GATE_CLKCMU_FSYS_USB20DRD	0x2068
#define CLK_CON_GAT_GATE_CLKCMU_G3D_BUS		0x206c
#define CLK_CON_GAT_GATE_CLKCMU_MFC_BUS		0x2070
#define CLK_CON_GAT_GATE_CLKCMU_MIF_BUSP	0x2074
#define CLK_CON_GAT_GATE_CLKCMU_MODEM_SHARED0	0x2078
#define CLK_CON_GAT_GATE_CLKCMU_MODEM_SHARED1	0x207c
#define CLK_CON_GAT_GATE_CLKCMU_MODEM_SHARED2	0x2080
#define CLK_CON_GAT_GATE_CLKCMU_PERI_BUS	0x2084
#define CLK_CON_GAT_GATE_CLKCMU_PERI_IP		0x2088
#define CLK_CON_GAT_GATE_CLKCMU_PERI_UART	0x208c

static const unsigned long top_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_AUD,
	PLL_LOCKTIME_PLL_SHARED0,
	PLL_LOCKTIME_PLL_SHARED1,
	PLL_CON3_PLL_AUD,
	PLL_CON8_PLL_AUD,
	PLL_CON9_PLL_AUD,
	PLL_CON3_PLL_SHARED0,
	PLL_CON8_PLL_SHARED0,
	PLL_CON9_PLL_SHARED0,
	PLL_CON3_PLL_SHARED1,
	PLL_CON8_PLL_SHARED1,
	PLL_CON9_PLL_SHARED1,
	CMU_CMU_TOP_CONTROLLER_OPTION,
	CLK_CON_MUX_MUX_CLKCMU_APM_BUS,
	CLK_CON_MUX_MUX_CLKCMU_CORE_BUS,
	CLK_CON_MUX_MUX_CLKCMU_CORE_SSS,
	CLK_CON_MUX_MUX_CLKCMU_CPUCL0_BUSP,
	CLK_CON_MUX_MUX_CLKCMU_CPUCL0_CPU,
	CLK_CON_MUX_MUX_CLKCMU_CPUCL0_DBG,
	CLK_CON_MUX_MUX_CLKCMU_DPU_AUDIF,
	CLK_CON_MUX_MUX_CLKCMU_DPU_AUD_BUS,
	CLK_CON_MUX_MUX_CLKCMU_DPU_AUD_CPU,
	CLK_CON_MUX_MUX_CLKCMU_DPU_BUS,
	CLK_CON_MUX_MUX_CLKCMU_FSYS_BUS,
	CLK_CON_MUX_MUX_CLKCMU_FSYS_MMC_CARD,
	CLK_CON_MUX_MUX_CLKCMU_FSYS_MMC_EMBD,
	CLK_CON_MUX_MUX_CLKCMU_FSYS_USB20DRD,
	CLK_CON_MUX_MUX_CLKCMU_G3D_BUS,
	CLK_CON_MUX_MUX_CLKCMU_MFC_BUS,
	CLK_CON_MUX_MUX_CLKCMU_MIF_BUSP,
	CLK_CON_MUX_MUX_CLKCMU_MIF_SWITCH,
	CLK_CON_MUX_MUX_CLKCMU_MODEM_SHARED1,
	CLK_CON_MUX_MUX_CLKCMU_PERI_BUS,
	CLK_CON_MUX_MUX_CLKCMU_PERI_IP,
	CLK_CON_MUX_MUX_CLKCMU_PERI_UART,
	CLK_CON_MUX_MUX_CMU_CMUREF,
	CLK_CON_DIV_AP2CP_SHARED0_PLL_CLK,
	CLK_CON_DIV_AP2CP_SHARED1_PLL_CLK,
	CLK_CON_DIV_AP2CP_SHARED2_PLL_CLK,
	CLK_CON_DIV_CLKCMU_APM_BUS,
	CLK_CON_DIV_CLKCMU_CMU_BOOST,
	CLK_CON_DIV_CLKCMU_CORE_BUS,
	CLK_CON_DIV_CLKCMU_CORE_SSS,
	CLK_CON_DIV_CLKCMU_CPUCL0_BUSP,
	CLK_CON_DIV_CLKCMU_CPUCL0_CPU,
	CLK_CON_DIV_CLKCMU_CPUCL0_DBG,
	CLK_CON_DIV_CLKCMU_DPU_AUDIF,
	CLK_CON_DIV_CLKCMU_DPU_AUD_BUS,
	CLK_CON_DIV_CLKCMU_DPU_AUD_CPU,
	CLK_CON_DIV_CLKCMU_DPU_BUS,
	CLK_CON_DIV_CLKCMU_FSYS_BUS,
	CLK_CON_DIV_CLKCMU_FSYS_MMC_CARD,
	CLK_CON_DIV_CLKCMU_FSYS_MMC_EMBD,
	CLK_CON_DIV_CLKCMU_FSYS_USB20DRD,
	CLK_CON_DIV_CLKCMU_G3D_BUS,
	CLK_CON_DIV_CLKCMU_MFC_BUS,
	CLK_CON_DIV_CLKCMU_MIF_BUSP,
	CLK_CON_DIV_CLKCMU_PERI_BUS,
	CLK_CON_DIV_CLKCMU_PERI_IP,
	CLK_CON_DIV_CLKCMU_PERI_UART,
	CLK_CON_DIV_DIV_CLK_CMU_CMUREF,
	CLK_CON_GAT_CLKCMU_MIF_SWITCH,
	CLK_CON_GAT_GATE_CLKCMU_APM_BUS,
	CLK_CON_GAT_GATE_CLKCMU_CORE_BUS,
	CLK_CON_GAT_GATE_CLKCMU_CORE_SSS,
	CLK_CON_GAT_GATE_CLKCMU_CPUCL0_BUSP,
	CLK_CON_GAT_GATE_CLKCMU_CPUCL0_CPU,
	CLK_CON_GAT_GATE_CLKCMU_CPUCL0_DBG,
	CLK_CON_GAT_GATE_CLKCMU_DPU_AUDIF,
	CLK_CON_GAT_GATE_CLKCMU_DPU_AUD_BUS,
	CLK_CON_GAT_GATE_CLKCMU_DPU_AUD_CPU,
	CLK_CON_GAT_GATE_CLKCMU_DPU_BUS,
	CLK_CON_GAT_GATE_CLKCMU_FSYS_BUS,
	CLK_CON_GAT_GATE_CLKCMU_FSYS_MMC_CARD,
	CLK_CON_GAT_GATE_CLKCMU_FSYS_MMC_EMBD,
	CLK_CON_GAT_GATE_CLKCMU_FSYS_USB20DRD,
	CLK_CON_GAT_GATE_CLKCMU_G3D_BUS,
	CLK_CON_GAT_GATE_CLKCMU_MFC_BUS,
	CLK_CON_GAT_GATE_CLKCMU_MIF_BUSP,
	CLK_CON_GAT_GATE_CLKCMU_MODEM_SHARED0,
	CLK_CON_GAT_GATE_CLKCMU_MODEM_SHARED1,
	CLK_CON_GAT_GATE_CLKCMU_MODEM_SHARED2,
	CLK_CON_GAT_GATE_CLKCMU_PERI_BUS,
	CLK_CON_GAT_GATE_CLKCMU_PERI_IP,
	CLK_CON_GAT_GATE_CLKCMU_PERI_UART,
};

static const struct samsung_pll_clock top_pll_clks[] __initconst = {
	PLL(pll_309, CLK_FOUT_AUD_PLL, "fout_aud_pll", "oscclk",
	    PLL_LOCKTIME_PLL_AUD, PLL_CON3_PLL_AUD, NULL),
	PLL(pll_309, CLK_FOUT_SHARED0_PLL, "fout_shared0_pll", "oscclk",
	    PLL_LOCKTIME_PLL_SHARED0, PLL_CON3_PLL_SHARED0, NULL),
	PLL(pll_309, CLK_FOUT_SHARED1_PLL, "fout_shared1_pll", "oscclk",
	    PLL_LOCKTIME_PLL_SHARED1, PLL_CON3_PLL_SHARED1, NULL),
};

/* Parent clock list for CMU_TOP muxes */
PNAME(mout_cmu_cmuref_p)	= { "oscclk",
				    "dout_cmu_cmuref" };

/* Parent clock list for CMU_TOP muxes: for CMU_APM */
PNAME(mout_cmu_apm_bus_p)	= { "dout_shared0_div2",
				    "dout_shared0_div3" };

/* Parent clock list for CMU_TOP muxes: for CMU_CORE */
PNAME(mout_cmu_core_bus_p)	= { "dout_shared0_div2",
				    "dout_shared1_div3",
				    "dout_shared0_div3",
				    "dout_aud_div3" };

PNAME(mout_cmu_core_sss_p)	= { "dout_shared0_div2",
				    "dout_shared1_div3",
				    "dout_shared0_div3",
				    "dout_aud_div3"};

/* Parent clock list for CMU_TOP muxes: for CMU_CPUCL0 */
PNAME(mout_cmu_cpucl0_busp_p)	= { "dout_shared0_div3",
				    "dout_shared0_div4" };

PNAME(mout_cmu_cpucl0_cpu_p)	= { "fout_aud_pll",
				    "dout_shared1_div2",
				    "dout_shared0_div2",
				    "dout_shared1_div3",
				    "dout_aud_div2",
				    "dout_shared0_div3",
				    "dout_shared1_div4",
				    "dout_shared0_div4" };

PNAME(mout_cmu_cpucl0_dbg_p)	= { "dout_shared0_div3",
				    "dout_shared0_div4" };

/* Parent clock list for CMU_TOP muxes: for CMU_DPU */
PNAME(mout_cmu_dpu_audif_p)	= { "dout_aud_div2",
				    "dout_aud_div3" };

PNAME(mout_cmu_dpu_aud_bus_p)	= { "dout_aud_div2",
				    "dout_shared0_div3",
				    "dout_aud_div3",
				    "dout_shared0_div4" };

PNAME(mout_cmu_dpu_aud_cpu_p)	= { "fout_aud_pll",
				    "dout_shared1_div2",
				    "dout_shared0_div2",
				    "dout_shared1_div3",
				    "dout_aud_div2",
				    "dout_shared0_div3",
				    "dout_aud_div3",
				    "dout_aud_div4" };

PNAME(mout_cmu_dpu_bus_p)	= { "dout_shared1_div3",
				    "dout_shared0_div3",
				    "dout_shared0_div4" };

/* Parent clock list for CMU_TOP muxes: for CMU_FSYS */
PNAME(mout_cmu_fsys_bus_p)	= { "dout_shared0_div2",
				    "dout_shared1_div3" };

PNAME(mout_cmu_fsys_mmc_card_p)	= { "oscclk",
				    "dout_shared0_div2",
				    "dout_shared1_div3",
				    "dout_shared0_div3",
				    "dout_aud_div2" };

PNAME(mout_cmu_fsys_mmc_embd_p)	= { "oscclk",
				    "dout_shared0_div2",
				    "dout_shared1_div3",
				    "dout_shared0_div3",
				    "dout_aud_div2" };

PNAME(mout_cmu_fsys_usb20drd_p)	= { "oscclk",
				    "dout_shared0_div4" };

/* Parent clock list for CMU_TOP muxes: for CMU_G3D */
PNAME(mout_cmu_g3d_bus_p)	= { "dout_shared1_div2",
				    "dout_shared0_div2",
				    "dout_shared1_div3",
				    "dout_shared0_div3" };

/* Parent clock list for CMU_TOP muxes: for CMU_MFC */
PNAME(mout_cmu_mfc_bus_p)	= { "dout_shared1_div3",
				    "dout_shared0_div3",
				    "dout_shared1_div4",
				    "dout_shared0_div4" };

/* Parent clock list for CMU_TOP muxes: for CMU_MIF */
PNAME(mout_cmu_mif_busp_p)	= { "dout_shared0_div3",
				    "dout_shared0_div4" };

PNAME(mout_cmu_mif_switch_p)	= { "fout_shared1_pll",
				    "fout_shared0_pll",
				    "dout_shared1_div2",
				    "dout_shared0_div2",
				    "dout_shared1_div3",
				    "dout_shared0_div3",
				    "dout_shared1_div4",
				    "dout_shared0_div4" };

/* Parent clock list for CMU_TOP muxes: for CMU_MODEM */
PNAME(mout_cmu_modem_shared1_p)	= { "dout_shared1_div2",
				    "dout_aud_div2",
				    "dout_shared1_div3",
				    "dout_shared0_div3" };

/* Parent clock list for CMU_TOP muxes: for CMU_PERI */
PNAME(mout_cmu_peri_bus_p)	= { "dout_shared0_div3",
				    "dout_shared0_div4" };

PNAME(mout_cmu_peri_ip_p)	= { "oscclk",
				    "dout_shared0_div4",
				    "dout_shared1_div4",
				    "dout_aud_div3" };

PNAME(mout_cmu_peri_uart_p)	= { "oscclk",
				    "dout_shared0_div4",
				    "dout_shared1_div4",
				    "dout_aud_div3" };

static const struct samsung_mux_clock top_mux_clks[] __initconst = {
	/* APM (Active Power Management) */
	MUX(CLK_MOUT_CMU_APM_BUS, "mout_cmu_apm_bus",
	    mout_cmu_apm_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_APM_BUS,
	    0, 1),

	/* CMU (Clock Management Unit) */
	MUX(CLK_MOUT_CMU_CMUREF, "mout_cmu_cmuref",
	    mout_cmu_cmuref_p,
	    CLK_CON_MUX_MUX_CMU_CMUREF,
	    0, 1),

	/* CORE */
	MUX(CLK_MOUT_CMU_CORE_BUS, "mout_cmu_core_bus",
	    mout_cmu_core_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_CORE_BUS,
	    0, 2),
	MUX(CLK_MOUT_CMU_CORE_SSS, "mout_cmu_core_sss",
	    mout_cmu_core_sss_p,
	    CLK_CON_MUX_MUX_CLKCMU_CORE_SSS,
	    0, 2),

	/* CPUCL0 (CPU Cluster 0) */
	MUX(CLK_MOUT_CMU_CPUCL0_BUSP, "mout_cmu_cpucl0_busp",
	    mout_cmu_cpucl0_busp_p,
	    CLK_CON_MUX_MUX_CLKCMU_CPUCL0_BUSP,
	    0, 1),
	MUX(CLK_MOUT_CMU_CPUCL0_CPU, "mout_cmu_cpucl0_cpu",
	    mout_cmu_cpucl0_cpu_p,
	    CLK_CON_MUX_MUX_CLKCMU_CPUCL0_CPU,
	    0, 3),
	MUX(CLK_MOUT_CMU_CPUCL0_DBG, "mout_cmu_cpucl0_dbg",
	    mout_cmu_cpucl0_dbg_p,
	    CLK_CON_MUX_MUX_CLKCMU_CPUCL0_DBG,
	    0, 1),

	/* DPU (Display Processing Unit) */
	MUX(CLK_MOUT_CMU_DPU_AUDIF, "mout_cmu_dpu_audif",
	    mout_cmu_dpu_audif_p,
	    CLK_CON_MUX_MUX_CLKCMU_DPU_AUDIF,
	    0, 1),
	MUX(CLK_MOUT_CMU_DPU_AUD_BUS, "mout_cmu_dpu_aud_bus",
	    mout_cmu_dpu_aud_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_DPU_AUD_BUS,
	    0, 2),
	MUX(CLK_MOUT_CMU_DPU_AUD_CPU, "mout_cmu_dpu_aud_cpu",
	    mout_cmu_dpu_aud_cpu_p,
	    CLK_CON_MUX_MUX_CLKCMU_DPU_AUD_CPU,
	    0, 3),
	MUX(CLK_MOUT_CMU_DPU_BUS, "mout_cmu_dpu_bus",
	    mout_cmu_dpu_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_DPU_BUS,
	    0, 2),

	/* FSYS */
	MUX(CLK_MOUT_CMU_FSYS_BUS, "mout_cmu_fsys_bus",
	    mout_cmu_fsys_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_FSYS_BUS,
	    0, 1),
	MUX(CLK_MOUT_CMU_FSYS_MMC_CARD, "mout_cmu_fsys_mmc_card",
	    mout_cmu_fsys_mmc_card_p,
	    CLK_CON_MUX_MUX_CLKCMU_FSYS_MMC_CARD,
	    0, 3),
	MUX(CLK_MOUT_CMU_FSYS_MMC_EMBD, "mout_cmu_fsys_mmc_embd",
	    mout_cmu_fsys_mmc_embd_p,
	    CLK_CON_MUX_MUX_CLKCMU_FSYS_MMC_EMBD,
	    0, 3),
	MUX(CLK_MOUT_CMU_FSYS_USB20DRD, "mout_cmu_fsys_usb20drd",
	    mout_cmu_fsys_usb20drd_p,
	    CLK_CON_MUX_MUX_CLKCMU_FSYS_USB20DRD,
	    0, 1),

	/* G3D */
	MUX(CLK_MOUT_CMU_G3D_BUS, "mout_cmu_g3d_bus",
	    mout_cmu_g3d_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_G3D_BUS,
	    0, 2),

	/* MFC */
	MUX(CLK_MOUT_CMU_MFC_BUS, "mout_cmu_mfc_bus",
	    mout_cmu_mfc_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_MFC_BUS,
	    0, 2),

	/* MIF */
	MUX(CLK_MOUT_CMU_MIF_BUSP, "mout_cmu_mif_busp",
	    mout_cmu_mif_busp_p,
	    CLK_CON_MUX_MUX_CLKCMU_MIF_BUSP,
	    0, 1),
	MUX(CLK_MOUT_CMU_MIF_SWITCH, "mout_cmu_mif_switch",
	    mout_cmu_mif_switch_p,
	    CLK_CON_MUX_MUX_CLKCMU_MIF_SWITCH,
	    0, 3),

	/* MODEM */
	MUX(CLK_MOUT_CMU_MODEM_SHARED1, "mout_cmu_modem_shared1",
	    mout_cmu_modem_shared1_p,
	    CLK_CON_MUX_MUX_CLKCMU_MODEM_SHARED1,
	    0, 2),

	/* PERI */
	MUX(CLK_MOUT_CMU_PERI_BUS, "mout_cmu_peri_bus",
	    mout_cmu_peri_bus_p,
	    CLK_CON_MUX_MUX_CLKCMU_PERI_BUS,
	    0, 1),
	MUX(CLK_MOUT_CMU_PERI_IP, "mout_cmu_peri_ip",
	    mout_cmu_peri_ip_p,
	    CLK_CON_MUX_MUX_CLKCMU_PERI_IP,
	    0, 2),
	MUX(CLK_MOUT_CMU_PERI_UART, "mout_cmu_peri_uart",
	    mout_cmu_peri_uart_p,
	    CLK_CON_MUX_MUX_CLKCMU_PERI_UART,
	    0, 2),
};

static const struct samsung_div_clock top_div_clks[] __initconst = {
	/* APM */
	DIV(CLK_DOUT_CMU_APM_BUS, "dout_cmu_apm_bus",
	    "gout_cmu_apm_bus",
	    CLK_CON_DIV_CLKCMU_APM_BUS,
	    0, 3),

	/* CMU */
	DIV(CLK_DOUT_CMU_CMU_BOOST, "dout_cmu_cmu_boost",
	    "fout_shared0_pll",
	    CLK_CON_DIV_CLKCMU_CMU_BOOST,
	    0, 3),
	DIV(CLK_DOUT_CMU_CMUREF, "dout_cmu_cmuref",
	    "fout_shared0_pll",
	    CLK_CON_DIV_DIV_CLK_CMU_CMUREF,
	    0, 2),

	/* CORE */
	DIV(CLK_DOUT_CMU_CORE_BUS, "dout_cmu_core_bus",
	    "gout_cmu_core_bus",
	    CLK_CON_DIV_CLKCMU_CORE_BUS,
	    0, 4),
	DIV(CLK_DOUT_CMU_CORE_SSS, "dout_cmu_core_sss",
	    "gout_cmu_core_sss",
	    CLK_CON_DIV_CLKCMU_CORE_SSS,
	    0, 4),

	/* CPUCL0 */
	DIV(CLK_DOUT_CMU_CPUCL0_BUSP, "dout_cmu_cpucl0_busp",
	    "gout_cmu_cpucl0_busp",
	    CLK_CON_DIV_CLKCMU_CPUCL0_BUSP,
	    0, 3),
	DIV(CLK_DOUT_CMU_CPUCL0_CPU, "dout_cmu_cpucl0_cpu",
	    "gout_cmu_cpucl0_cpu",
	    CLK_CON_DIV_CLKCMU_CPUCL0_CPU,
	    0, 3),
	DIV(CLK_DOUT_CMU_CPUCL0_DBG, "dout_cmu_cpucl0_dbg",
	    "gout_cmu_cpucl0_dbg",
	    CLK_CON_DIV_CLKCMU_CPUCL0_DBG,
	    0, 3),

	/* DPU */
	DIV(CLK_DOUT_CMU_DPU_AUDIF, "dout_cmu_dpu_audif",
	    "gout_cmu_dpu_audif",
	    CLK_CON_DIV_CLKCMU_DPU_AUDIF,
	    0, 9),
	DIV(CLK_DOUT_CMU_DPU_AUD_BUS, "dout_cmu_dpu_aud_bus",
	    "gout_cmu_dpu_aud_bus",
	    CLK_CON_DIV_CLKCMU_DPU_AUD_BUS,
	    0, 3),
	DIV(CLK_DOUT_CMU_DPU_AUD_CPU, "dout_cmu_dpu_aud_cpu",
	    "gout_cmu_dpu_aud_cpu",
	    CLK_CON_DIV_CLKCMU_DPU_AUD_CPU,
	    0, 3),
	DIV(CLK_DOUT_CMU_DPU_BUS, "dout_cmu_dpu_bus",
	    "gout_cmu_dpu_bus",
	    CLK_CON_DIV_CLKCMU_DPU_BUS,
	    0, 3),

	/* FSYS */
	DIV(CLK_DOUT_CMU_FSYS_BUS, "dout_cmu_fsys_bus",
	    "gout_cmu_fsys_bus",
	    CLK_CON_DIV_CLKCMU_FSYS_BUS,
	    0, 4),
	DIV(CLK_DOUT_CMU_FSYS_MMC_CARD, "dout_cmu_fsys_mmc_card",
	    "gout_cmu_fsys_mmc_card",
	    CLK_CON_DIV_CLKCMU_FSYS_MMC_CARD,
	    0, 9),
	DIV(CLK_DOUT_CMU_FSYS_MMC_EMBD, "dout_cmu_fsys_mmc_embd",
	    "gout_cmu_fsys_mmc_embd",
	    CLK_CON_DIV_CLKCMU_FSYS_MMC_EMBD,
	    0, 9),
	DIV(CLK_DOUT_CMU_FSYS_USB20DRD, "dout_cmu_fsys_usb20drd",
	    "gout_cmu_fsys_usb20drd",
	    CLK_CON_DIV_CLKCMU_FSYS_USB20DRD,
	    0, 4),

	/* G3D */
	DIV(CLK_DOUT_CMU_G3D_BUS, "dout_cmu_g3d_bus",
	    "gout_cmu_g3d_bus",
	    CLK_CON_DIV_CLKCMU_G3D_BUS,
	    0, 3),

	/* MFC */
	DIV(CLK_DOUT_CMU_MFC_BUS, "dout_cmu_mfc_bus",
	    "gout_cmu_mfc_bus",
	    CLK_CON_DIV_CLKCMU_MFC_BUS,
	    0, 4),

	/* MIF */
	DIV(CLK_DOUT_CMU_MIF_BUSP, "dout_cmu_mif_busp",
	    "gout_cmu_mif_busp",
	    CLK_CON_DIV_CLKCMU_MIF_BUSP,
	    0, 3),

	/* PERI */
	DIV(CLK_DOUT_CMU_PERI_BUS, "dout_cmu_peri_bus",
	    "gout_cmu_peri_bus",
	    CLK_CON_DIV_CLKCMU_PERI_BUS,
	    0, 3),
	DIV(CLK_DOUT_CMU_PERI_IP, "dout_cmu_peri_ip",
	    "gout_cmu_peri_ip",
	    CLK_CON_DIV_CLKCMU_PERI_IP,
	    0, 4),
	DIV(CLK_DOUT_CMU_PERI_UART, "dout_cmu_peri_uart",
	    "gout_cmu_peri_uart",
	    CLK_CON_DIV_CLKCMU_PERI_UART,
	    0, 4),

	/* SHARED */
	DIV(CLK_DOUT_AP2CP_SHARED0_PLL, "dout_ap2cp_shared0_pll",
	    "gout_cmu_modem_shared0",
	    CLK_CON_DIV_AP2CP_SHARED0_PLL_CLK,
	    0, 3),
	DIV(CLK_DOUT_AP2CP_SHARED1_PLL, "dout_ap2cp_shared1_pll",
	    "gout_cmu_modem_shared1",
	    CLK_CON_DIV_AP2CP_SHARED1_PLL_CLK,
	    0, 3),
	DIV(CLK_DOUT_AP2CP_SHARED2_PLL, "dout_ap2cp_shared2_pll",
	    "gout_cmu_modem_shared2",
	    CLK_CON_DIV_AP2CP_SHARED2_PLL_CLK,
	    0, 3),
};

static const struct samsung_fixed_factor_clock top_fixed_factor_clks[] __initconst = {
	FFACTOR(CLK_DOUT_SHARED0_DIV2, "dout_shared0_div2", "fout_shared0_pll",
		1, 2, 0),
	FFACTOR(CLK_DOUT_SHARED0_DIV3, "dout_shared0_div3", "fout_shared0_pll",
		1, 3, 0),
	FFACTOR(CLK_DOUT_SHARED0_DIV4, "dout_shared0_div4", "fout_shared0_pll",
		1, 4, 0),
	FFACTOR(CLK_DOUT_SHARED1_DIV2, "dout_shared1_div2", "fout_shared1_pll",
		1, 2, 0),
	FFACTOR(CLK_DOUT_SHARED1_DIV3, "dout_shared1_div3", "fout_shared1_pll",
		1, 3, 0),
	FFACTOR(CLK_DOUT_SHARED1_DIV4, "dout_shared1_div4", "fout_shared1_pll",
		1, 4, 0),
	FFACTOR(CLK_DOUT_AUD_DIV2, "dout_aud_div2", "fout_aud_pll",
		1, 2, 0),
	FFACTOR(CLK_DOUT_AUD_DIV3, "dout_aud_div3", "fout_aud_pll",
		1, 3, 0),
	FFACTOR(CLK_DOUT_AUD_DIV4, "dout_aud_div4", "fout_aud_pll",
		1, 4, 0),
};

static const struct samsung_gate_clock top_gate_clks[] __initconst = {
	/* APM */
	GATE(CLK_GOUT_CMU_APM_BUS, "gout_cmu_apm_bus",
	     "mout_cmu_apm_bus",
	     CLK_CON_GAT_GATE_CLKCMU_APM_BUS,
	     21, 0, 0),

	/* CORE */
	GATE(CLK_GOUT_CMU_CORE_BUS, "gout_cmu_core_bus",
	     "mout_cmu_core_bus",
	     CLK_CON_GAT_GATE_CLKCMU_CORE_BUS,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_CORE_SSS, "gout_cmu_core_sss",
	     "mout_cmu_core_sss",
	     CLK_CON_GAT_GATE_CLKCMU_CORE_SSS,
	     21, 0, 0),

	/* CPUCL0 */
	GATE(CLK_GOUT_CMU_CPUCL0_BUSP, "gout_cmu_cpucl0_busp",
	     "mout_cmu_cpucl0_busp",
	     CLK_CON_GAT_GATE_CLKCMU_CPUCL0_BUSP,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_CPUCL0_CPU, "gout_cmu_cpucl0_cpu",
	     "mout_cmu_cpucl0_cpu",
	     CLK_CON_GAT_GATE_CLKCMU_CPUCL0_CPU,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_CPUCL0_DBG, "gout_cmu_cpucl0_dbg",
	     "mout_cmu_cpucl0_dbg",
	     CLK_CON_GAT_GATE_CLKCMU_CPUCL0_DBG,
	     21, 0, 0),

	/* DPU */
	GATE(CLK_GOUT_CMU_DPU_AUDIF, "gout_cmu_dpu_audif",
	     "mout_cmu_dpu_audif",
	     CLK_CON_GAT_GATE_CLKCMU_DPU_AUDIF,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_DPU_AUD_BUS, "gout_cmu_dpu_aud_bus",
	     "mout_cmu_dpu_aud_bus",
	     CLK_CON_GAT_GATE_CLKCMU_DPU_AUD_BUS,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_DPU_AUD_CPU, "gout_cmu_dpu_aud_cpu",
	     "mout_cmu_dpu_aud_cpu",
	     CLK_CON_GAT_GATE_CLKCMU_DPU_AUD_CPU,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_DPU_BUS, "gout_cmu_dpu_bus",
	     "mout_cmu_dpu_bus",
	     CLK_CON_GAT_GATE_CLKCMU_DPU_BUS,
	     21, 0, 0),

	/* FSYS */
	GATE(CLK_GOUT_CMU_FSYS_BUS, "gout_cmu_fsys_bus",
	     "mout_cmu_fsys_bus",
	     CLK_CON_GAT_GATE_CLKCMU_FSYS_BUS,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_FSYS_MMC_CARD, "gout_cmu_fsys_mmc_card",
	     "mout_cmu_fsys_mmc_card",
	     CLK_CON_GAT_GATE_CLKCMU_FSYS_MMC_CARD,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_FSYS_MMC_EMBD, "gout_cmu_fsys_mmc_embd",
	     "mout_cmu_fsys_mmc_embd",
	     CLK_CON_GAT_GATE_CLKCMU_FSYS_MMC_EMBD,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_FSYS_USB20DRD, "gout_cmu_fsys_usb20drd",
	     "mout_cmu_fsys_usb20drd",
	     CLK_CON_GAT_GATE_CLKCMU_FSYS_USB20DRD,
	     21, 0, 0),

	/* G3D */
	GATE(CLK_GOUT_CMU_G3D_BUS, "gout_cmu_g3d_bus",
	     "mout_cmu_g3d_bus",
	     CLK_CON_GAT_GATE_CLKCMU_G3D_BUS,
	     21, 0, 0),

	/* MFC */
	GATE(CLK_GOUT_CMU_MFC_BUS, "gout_cmu_mfc_bus",
	     "mout_cmu_mfc_bus",
	     CLK_CON_GAT_GATE_CLKCMU_MFC_BUS,
	     21, 0, 0),

	/* MIF */
	GATE(CLK_GOUT_CMU_MIF_SWITCH, "gout_cmu_mif_switch",
	     "mout_cmu_mif_switch",
	     CLK_CON_GAT_CLKCMU_MIF_SWITCH,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_MIF_BUSP, "gout_cmu_mif_busp",
	     "mout_cmu_mif_busp",
	     CLK_CON_GAT_GATE_CLKCMU_MIF_BUSP,
	     21, 0, 0),

	/* MODEM */
	GATE(CLK_GOUT_CMU_MODEM_SHARED0, "gout_cmu_modem_shared0",
	     "fout_shared0_pll",
	     CLK_CON_GAT_GATE_CLKCMU_MODEM_SHARED0,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_MODEM_SHARED1, "gout_cmu_modem_shared1",
	     "mout_cmu_modem_shared1",
	     CLK_CON_GAT_GATE_CLKCMU_MODEM_SHARED1,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_MODEM_SHARED2, "gout_cmu_modem_shared2",
	     "fout_shared0_pll",
	     CLK_CON_GAT_GATE_CLKCMU_MODEM_SHARED2,
	     21, 0, 0),

	/* PERI */
	GATE(CLK_GOUT_CMU_PERI_BUS, "gout_cmu_peri_bus",
	     "mout_cmu_peri_bus",
	     CLK_CON_GAT_GATE_CLKCMU_PERI_BUS,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_PERI_IP, "gout_cmu_peri_ip",
	     "mout_cmu_peri_ip",
	     CLK_CON_GAT_GATE_CLKCMU_PERI_IP,
	     21, 0, 0),
	GATE(CLK_GOUT_CMU_PERI_UART, "gout_cmu_peri_uart",
	     "mout_cmu_peri_uart",
	     CLK_CON_GAT_GATE_CLKCMU_PERI_UART,
	     21, 0, 0),
};

static const unsigned long drcg_sysreg[] __initconst = {
	EXYNOS5515_DRCG_EN_OFFSET,
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
	.sysreg_clk_regs	= drcg_sysreg,
	.nr_sysreg_clk_regs	= ARRAY_SIZE(drcg_sysreg),
	.auto_clock_gate	= true,
	.gate_dbg_offset	= EXYNOS5515_GATE_DBG_OFFSET,
	.option_offset		= CMU_CMU_TOP_CONTROLLER_OPTION,
	.drcg_offset		= EXYNOS5515_DRCG_EN_OFFSET,
};

static void __init exynos5515_cmu_top_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &top_cmu_info);
}

/* Register CMU_TOP early, as it's a dependency for other early domains */
CLK_OF_DECLARE(exynos5515_cmu_top, "samsung,exynos5515-cmu-top",
	       exynos5515_cmu_top_init);

/* ---- CMU_PERI ------------------------------------------------------------ */

/*
 * CMU_PERI:	0x10030000
 * SYSREG_PERI:	0x10020000
 */

#define PLL_CON0_MUX_CLKCMU_PERI_BUS_USER					0x0600
#define PLL_CON1_MUX_CLKCMU_PERI_BUS_USER					0x0604
#define PLL_CON0_MUX_CLKCMU_PERI_IP_USER					0x0610
#define PLL_CON1_MUX_CLKCMU_PERI_IP_USER					0x0614
#define PLL_CON0_MUX_CLKCMU_PERI_UART_USER					0x0620
#define PLL_CON1_MUX_CLKCMU_PERI_UART_USER					0x0624
#define PERI_CMU_PERI_CONTROLLER_OPTION						0x0800
#define CLK_CON_MUX_MUX_CLK_PERI_SPI						0x1000
#define CLK_CON_MUX_MUX_CLK_PERI_USI00_USI					0x1004
#define CLK_CON_DIV_CLKCMU_OTP							0x1800
#define CLK_CON_DIV_DIV_CLK_PERI_I2C_0						0x1804
#define CLK_CON_DIV_DIV_CLK_PERI_I2C_1						0x1808
#define CLK_CON_DIV_DIV_CLK_PERI_I2C_2						0x180c
#define CLK_CON_DIV_DIV_CLK_PERI_I2C_3						0x1810
#define CLK_CON_DIV_DIV_CLK_PERI_SPI						0x1814
#define CLK_CON_DIV_DIV_CLK_PERI_USI00_I2C					0x1818
#define CLK_CON_DIV_DIV_CLK_PERI_USI00_USI					0x181c
#define CLK_CON_GAT_CLK_BLK_PERI_UID_BUSIF_TMU_IPCLKPORT_PCLK			0x2000
#define CLK_CON_GAT_CLK_BLK_PERI_UID_D_TZPC_PERI_IPCLKPORT_PCLK			0x2004
#define CLK_CON_GAT_CLK_BLK_PERI_UID_GPIO_PERI_IPCLKPORT_PCLK			0x2008
#define CLK_CON_GAT_CLK_BLK_PERI_UID_I2C_0_IPCLKPORT_IPCLK			0x200c
#define CLK_CON_GAT_CLK_BLK_PERI_UID_I2C_0_IPCLKPORT_PCLK			0x2010
#define CLK_CON_GAT_CLK_BLK_PERI_UID_I2C_1_IPCLKPORT_IPCLK			0x2014
#define CLK_CON_GAT_CLK_BLK_PERI_UID_I2C_1_IPCLKPORT_PCLK			0x2018
#define CLK_CON_GAT_CLK_BLK_PERI_UID_I2C_2_IPCLKPORT_IPCLK			0x201c
#define CLK_CON_GAT_CLK_BLK_PERI_UID_I2C_2_IPCLKPORT_PCLK			0x2020
#define CLK_CON_GAT_CLK_BLK_PERI_UID_I2C_3_IPCLKPORT_IPCLK			0x2024
#define CLK_CON_GAT_CLK_BLK_PERI_UID_I2C_3_IPCLKPORT_PCLK			0x2028
#define CLK_CON_GAT_CLK_BLK_PERI_UID_MCT_IPCLKPORT_PCLK				0x202c
#define CLK_CON_GAT_CLK_BLK_PERI_UID_OTP_CON_TOP_IPCLKPORT_I_OSCCLK		0x2030
#define CLK_CON_GAT_CLK_BLK_PERI_UID_OTP_CON_TOP_IPCLKPORT_PCLK			0x2034
#define CLK_CON_GAT_CLK_BLK_PERI_UID_PERI_CMU_PERI_IPCLKPORT_PCLK		0x2038
#define CLK_CON_GAT_CLK_BLK_PERI_UID_PWM_MOTOR_IPCLKPORT_I_PCLK_S0		0x203c
#define CLK_CON_GAT_CLK_BLK_PERI_UID_RSTNSYNC_CLK_PERI_BUS_IPCLKPORT_CLK	0x2040
#define CLK_CON_GAT_CLK_BLK_PERI_UID_RSTNSYNC_CLK_PERI_I2C_0_IPCLKPORT_CLK	0x2044
#define CLK_CON_GAT_CLK_BLK_PERI_UID_RSTNSYNC_CLK_PERI_I2C_1_IPCLKPORT_CLK	0x2048
#define CLK_CON_GAT_CLK_BLK_PERI_UID_RSTNSYNC_CLK_PERI_I2C_2_IPCLKPORT_CLK	0x204c
#define CLK_CON_GAT_CLK_BLK_PERI_UID_RSTNSYNC_CLK_PERI_I2C_3_IPCLKPORT_CLK	0x2050
#define CLK_CON_GAT_CLK_BLK_PERI_UID_RSTNSYNC_CLK_PERI_OSCCLK_IPCLKPORT_CLK	0x2054
#define CLK_CON_GAT_CLK_BLK_PERI_UID_RSTNSYNC_CLK_PERI_SPI_IPCLKPORT_CLK	0x2058
#define CLK_CON_GAT_CLK_BLK_PERI_UID_RSTNSYNC_CLK_PERI_UART_IPCLKPORT_CLK	0x205c
#define CLK_CON_GAT_CLK_BLK_PERI_UID_RSTNSYNC_CLK_PERI_USI00_I2C_IPCLKPORT_CLK	0x2060
#define CLK_CON_GAT_CLK_BLK_PERI_UID_RSTNSYNC_CLK_PERI_USI00_USI_IPCLKPORT_CLK	0x2064
#define CLK_CON_GAT_CLK_BLK_PERI_UID_SLH_AXI_MI_P_PERI_IPCLKPORT_I_CLK		0x2068
#define CLK_CON_GAT_CLK_BLK_PERI_UID_SYSREG_PERI_IPCLKPORT_PCLK			0x206c
#define CLK_CON_GAT_CLK_BLK_PERI_UID_USI00_I2C_IPCLKPORT_IPCLK			0x2070
#define CLK_CON_GAT_CLK_BLK_PERI_UID_USI00_I2C_IPCLKPORT_PCLK			0x2074
#define CLK_CON_GAT_CLK_BLK_PERI_UID_USI00_USI_IPCLKPORT_IPCLK			0x2078
#define CLK_CON_GAT_CLK_BLK_PERI_UID_USI00_USI_IPCLKPORT_PCLK			0x207c
#define CLK_CON_GAT_CLK_BLK_PERI_UID_USI_SPI_IPCLKPORT_IPCLK			0x2080
#define CLK_CON_GAT_CLK_BLK_PERI_UID_USI_SPI_IPCLKPORT_PCLK			0x2084
#define CLK_CON_GAT_CLK_BLK_PERI_UID_USI_UART_IPCLKPORT_IPCLK			0x2088
#define CLK_CON_GAT_CLK_BLK_PERI_UID_USI_UART_IPCLKPORT_PCLK			0x208c
#define CLK_CON_GAT_CLK_BLK_PERI_UID_WDT0_IPCLKPORT_PCLK			0x2090
#define CLK_CON_GAT_CLK_BLK_PERI_UID_WDT1_IPCLKPORT_PCLK			0x2094
#define CLK_CON_GAT_GATE_CLK_PERI_I2C_0						0x2098
#define CLK_CON_GAT_GATE_CLK_PERI_I2C_1						0x209c
#define CLK_CON_GAT_GATE_CLK_PERI_I2C_2						0x20a0
#define CLK_CON_GAT_GATE_CLK_PERI_I2C_3						0x20a4
#define CLK_CON_GAT_GATE_CLK_PERI_USI00_I2C					0x20a8

static const unsigned long peri_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_PERI_BUS_USER,
	PLL_CON1_MUX_CLKCMU_PERI_BUS_USER,
	PLL_CON0_MUX_CLKCMU_PERI_IP_USER,
	PLL_CON1_MUX_CLKCMU_PERI_IP_USER,
	PLL_CON0_MUX_CLKCMU_PERI_UART_USER,
	PLL_CON1_MUX_CLKCMU_PERI_UART_USER,
	PERI_CMU_PERI_CONTROLLER_OPTION,
	CLK_CON_MUX_MUX_CLK_PERI_SPI,
	CLK_CON_MUX_MUX_CLK_PERI_USI00_USI,
	CLK_CON_DIV_CLKCMU_OTP,
	CLK_CON_DIV_DIV_CLK_PERI_I2C_0,
	CLK_CON_DIV_DIV_CLK_PERI_I2C_1,
	CLK_CON_DIV_DIV_CLK_PERI_I2C_2,
	CLK_CON_DIV_DIV_CLK_PERI_I2C_3,
	CLK_CON_DIV_DIV_CLK_PERI_SPI,
	CLK_CON_DIV_DIV_CLK_PERI_USI00_I2C,
	CLK_CON_DIV_DIV_CLK_PERI_USI00_USI,
	CLK_CON_GAT_CLK_BLK_PERI_UID_BUSIF_TMU_IPCLKPORT_PCLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_D_TZPC_PERI_IPCLKPORT_PCLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_GPIO_PERI_IPCLKPORT_PCLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_I2C_0_IPCLKPORT_IPCLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_I2C_0_IPCLKPORT_PCLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_I2C_1_IPCLKPORT_IPCLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_I2C_1_IPCLKPORT_PCLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_I2C_2_IPCLKPORT_IPCLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_I2C_2_IPCLKPORT_PCLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_I2C_3_IPCLKPORT_IPCLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_I2C_3_IPCLKPORT_PCLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_MCT_IPCLKPORT_PCLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_OTP_CON_TOP_IPCLKPORT_I_OSCCLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_OTP_CON_TOP_IPCLKPORT_PCLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_PERI_CMU_PERI_IPCLKPORT_PCLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_PWM_MOTOR_IPCLKPORT_I_PCLK_S0,
	CLK_CON_GAT_CLK_BLK_PERI_UID_RSTNSYNC_CLK_PERI_BUS_IPCLKPORT_CLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_RSTNSYNC_CLK_PERI_I2C_0_IPCLKPORT_CLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_RSTNSYNC_CLK_PERI_I2C_1_IPCLKPORT_CLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_RSTNSYNC_CLK_PERI_I2C_2_IPCLKPORT_CLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_RSTNSYNC_CLK_PERI_I2C_3_IPCLKPORT_CLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_RSTNSYNC_CLK_PERI_OSCCLK_IPCLKPORT_CLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_RSTNSYNC_CLK_PERI_SPI_IPCLKPORT_CLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_RSTNSYNC_CLK_PERI_UART_IPCLKPORT_CLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_RSTNSYNC_CLK_PERI_USI00_I2C_IPCLKPORT_CLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_RSTNSYNC_CLK_PERI_USI00_USI_IPCLKPORT_CLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_SLH_AXI_MI_P_PERI_IPCLKPORT_I_CLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_SYSREG_PERI_IPCLKPORT_PCLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_USI00_I2C_IPCLKPORT_IPCLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_USI00_I2C_IPCLKPORT_PCLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_USI00_USI_IPCLKPORT_IPCLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_USI00_USI_IPCLKPORT_PCLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_USI_SPI_IPCLKPORT_IPCLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_USI_SPI_IPCLKPORT_PCLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_USI_UART_IPCLKPORT_IPCLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_USI_UART_IPCLKPORT_PCLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_WDT0_IPCLKPORT_PCLK,
	CLK_CON_GAT_CLK_BLK_PERI_UID_WDT1_IPCLKPORT_PCLK,
	CLK_CON_GAT_GATE_CLK_PERI_I2C_0,
	CLK_CON_GAT_GATE_CLK_PERI_I2C_1,
	CLK_CON_GAT_GATE_CLK_PERI_I2C_2,
	CLK_CON_GAT_GATE_CLK_PERI_I2C_3,
	CLK_CON_GAT_GATE_CLK_PERI_USI00_I2C,
};

/* Parent clock list for CMU_PERI muxes */
PNAME(mout_pll_peri_bus_user_p)		= { "oscclk",
					    "mout_cmu_peri_bus" };

PNAME(mout_pll_peri_ip_user_p)		= { "oscclk",
					    "mout_cmu_peri_ip" };

/* Parent clock list for CMU_PERI muxes: for SPI */
PNAME(mout_peri_spi_p)			= { "oscclk",
					    "mout_pll_peri_ip_user" };

/* Parent clock list for CMU_PERI muxes: for UART */
PNAME(mout_pll_peri_uart_user_p)	= { "oscclk",
					    "mout_cmu_peri_uart" };

/* Parent clock list for CMU_PERI muxes: for USI */
PNAME(mout_peri_usi00_usi_p)		= { "oscclk",
					    "mout_pll_peri_ip_user" };

static const struct samsung_mux_clock peri_mux_clks[] __initconst = {
	/* BUS */
	MUX(CLK_MOUT_PLL_PERI_BUS_USER, "mout_pll_peri_bus_user",
	    mout_pll_peri_bus_user_p,
	    PLL_CON0_MUX_CLKCMU_PERI_BUS_USER,
	    4, 1),

	/* IP */
	MUX(CLK_MOUT_PLL_PERI_IP_USER, "mout_pll_peri_ip_user",
	    mout_pll_peri_ip_user_p,
	    PLL_CON0_MUX_CLKCMU_PERI_IP_USER,
	    4, 1),

	/* SPI */
	MUX(CLK_MOUT_PERI_SPI, "mout_peri_spi",
	    mout_peri_spi_p,
	    CLK_CON_MUX_MUX_CLK_PERI_SPI,
	    0, 1),

	/* UART */
	MUX(CLK_MOUT_PLL_PERI_UART_USER, "mout_pll_peri_uart_user",
	    mout_pll_peri_uart_user_p,
	    PLL_CON0_MUX_CLKCMU_PERI_UART_USER,
	    4, 1),

	/* USI (Universal Serial Interface) */
	MUX(CLK_MOUT_PERI_USI00_USI, "mout_peri_usi00_usi",
	    mout_peri_usi00_usi_p,
	    CLK_CON_MUX_MUX_CLK_PERI_USI00_USI,
	    0, 1),
};

static const struct samsung_div_clock peri_div_clks[] __initconst = {
	/* I2C */
	DIV(CLK_DOUT_PERI_I2C_0, "dout_peri_i2c_0",
	    "gout_peri_i2c_0",
	    CLK_CON_DIV_DIV_CLK_PERI_I2C_0,
	    0, 4),
	DIV(CLK_DOUT_PERI_I2C_1, "dout_peri_i2c_1",
	    "gout_peri_i2c_1",
	    CLK_CON_DIV_DIV_CLK_PERI_I2C_1,
	    0, 4),
	DIV(CLK_DOUT_PERI_I2C_2, "dout_peri_i2c_2",
	    "gout_peri_i2c_2",
	    CLK_CON_DIV_DIV_CLK_PERI_I2C_2,
	    0, 4),
	DIV(CLK_DOUT_PERI_I2C_3, "dout_peri_i2c_3",
	    "gout_peri_i2c_3",
	    CLK_CON_DIV_DIV_CLK_PERI_I2C_3,
	    0, 4),

	/* SPI */
	DIV(CLK_DOUT_PERI_SPI, "dout_peri_spi",
	    "mout_peri_spi",
	    CLK_CON_DIV_DIV_CLK_PERI_SPI,
	    0, 5),

	/* USI */
	DIV(CLK_DOUT_PERI_USI00_I2C, "dout_peri_usi00_i2c",
	    "gout_peri_usi00_i2c",
	    CLK_CON_DIV_DIV_CLK_PERI_USI00_I2C,
	    0, 4),
	DIV(CLK_DOUT_PERI_USI00_USI, "dout_peri_usi00_usi",
	    "mout_peri_usi00_usi",
	    CLK_CON_DIV_DIV_CLK_PERI_USI00_USI,
	    0, 5),
};

static const struct samsung_fixed_factor_clock peri_fixed_factor_clks[] __initconst = {
	/* OTP (One-Time Programmable eFuses) */
	FFACTOR(CLK_DOUT_PERI_OTP, "dout_peri_otp", "oscclk",
		1, 8, 0),
};

static const struct samsung_gate_clock peri_gate_clks[] __initconst = {
	/* BUS */
	GATE(CLK_GOUT_PERI_BUSIF_TMU_PCLK, "gout_peri_busif_tmu_pclk",
	     "mout_pll_peri_bus_user",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_BUSIF_TMU_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERI_BUS_CLK, "gout_peri_bus_clk",
	     "mout_pll_peri_bus_user",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_RSTNSYNC_CLK_PERI_BUS_IPCLKPORT_CLK,
	     21, 0, 0),

	/* CMU */
	GATE(CLK_GOUT_PERI_CMU_PCLK, "gout_peri_cmu_pclk",
	     "mout_pll_peri_bus_user",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_PERI_CMU_PERI_IPCLKPORT_PCLK,
	     21, 0, 0),

	/* GPIO */
	GATE(CLK_GOUT_PERI_GPIO_PCLK, "gout_peri_gpio_pclk",
	     "mout_pll_peri_bus_user",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_GPIO_PERI_IPCLKPORT_PCLK,
	     21, 0, 0),

	/* I2C */
	GATE(CLK_GOUT_PERI_I2C_0_IPCLK, "gout_peri_i2c_0_ipclk",
	     "dout_peri_i2c_0",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_I2C_0_IPCLKPORT_IPCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERI_I2C_0_PCLK, "gout_peri_i2c_0_pclk",
	     "mout_pll_peri_bus_user",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_I2C_0_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERI_I2C_1_IPCLK, "gout_peri_i2c_1_ipclk",
	     "dout_peri_i2c_1",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_I2C_1_IPCLKPORT_IPCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERI_I2C_1_PCLK, "gout_peri_i2c_1_pclk",
	     "mout_pll_peri_bus_user",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_I2C_1_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERI_I2C_2_IPCLK, "gout_peri_i2c_2_ipclk",
	     "dout_peri_i2c_2",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_I2C_2_IPCLKPORT_IPCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERI_I2C_2_PCLK, "gout_peri_i2c_2_pclk",
	     "mout_pll_peri_bus_user",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_I2C_2_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERI_I2C_3_IPCLK, "gout_peri_i2c_3_ipclk",
	     "dout_peri_i2c_3",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_I2C_3_IPCLKPORT_IPCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERI_I2C_3_PCLK, "gout_peri_i2c_3_pclk",
	     "mout_pll_peri_bus_user",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_I2C_3_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERI_I2C_0_CLK, "gout_peri_i2c_0_clk",
	     "dout_peri_i2c_0",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_RSTNSYNC_CLK_PERI_I2C_0_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERI_I2C_1_CLK, "gout_peri_i2c_1_clk",
	     "dout_peri_i2c_1",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_RSTNSYNC_CLK_PERI_I2C_1_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERI_I2C_2_CLK, "gout_peri_i2c_2_clk",
	     "dout_peri_i2c_2",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_RSTNSYNC_CLK_PERI_I2C_2_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERI_I2C_3_CLK, "gout_peri_i2c_3_clk",
	     "dout_peri_i2c_3",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_RSTNSYNC_CLK_PERI_I2C_3_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERI_I2C_0, "gout_peri_i2c_0",
	     "mout_pll_peri_ip_user",
	     CLK_CON_GAT_GATE_CLK_PERI_I2C_0,
	     21, 0, 0),
	GATE(CLK_GOUT_PERI_I2C_1, "gout_peri_i2c_1",
	     "mout_pll_peri_ip_user",
	     CLK_CON_GAT_GATE_CLK_PERI_I2C_1,
	     21, 0, 0),
	GATE(CLK_GOUT_PERI_I2C_2, "gout_peri_i2c_2",
	     "mout_pll_peri_ip_user",
	     CLK_CON_GAT_GATE_CLK_PERI_I2C_2,
	     21, 0, 0),
	GATE(CLK_GOUT_PERI_I2C_3, "gout_peri_i2c_3",
	     "mout_pll_peri_ip_user",
	     CLK_CON_GAT_GATE_CLK_PERI_I2C_3,
	     21, 0, 0),

	/* MCT (Multi-Core Timer) */
	GATE(CLK_GOUT_PERI_MCT_PCLK, "gout_peri_mct_pclk",
	     "mout_pll_peri_bus_user",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_MCT_IPCLKPORT_PCLK,
	     21, 0, 0),

	/* OTP */
	GATE(CLK_GOUT_PERI_OTP_OSCCLK, "gout_peri_otp_oscclk",
	     "oscclk",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_OTP_CON_TOP_IPCLKPORT_I_OSCCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERI_OTP_PCLK, "gout_peri_otp_pclk",
	     "mout_pll_peri_bus_user",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_OTP_CON_TOP_IPCLKPORT_PCLK,
	     21, 0, 0),

	/* OSCCLK */
	GATE(CLK_GOUT_PERI_OSCCLK, "gout_peri_oscclk",
	     "oscclk",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_RSTNSYNC_CLK_PERI_OSCCLK_IPCLKPORT_CLK,
	     21, 0, 0),

	/* PWM */
	GATE(CLK_GOUT_PERI_PWM_MOTOR_PCLK, "gout_peri_pwm_motor_pclk",
	     "mout_pll_peri_bus_user",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_PWM_MOTOR_IPCLKPORT_I_PCLK_S0,
	     21, 0, 0),

	/* SPI */
	GATE(CLK_GOUT_PERI_SPI_CLK, "gout_peri_spi_clk",
	     "dout_peri_spi",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_RSTNSYNC_CLK_PERI_SPI_IPCLKPORT_CLK,
	     21, 0, 0),

	/* SYSREG */
	GATE(CLK_GOUT_PERI_SYSREG_PCLK, "gout_peri_sysreg_pclk",
	     "mout_pll_peri_bus_user",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_SYSREG_PERI_IPCLKPORT_PCLK,
	     21, 0, 0),

	/* TZ */
	GATE(CLK_GOUT_PERI_D_TZPC_PCLK, "gout_peri_d_tzpc_pclk",
	     "mout_pll_peri_bus_user",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_D_TZPC_PERI_IPCLKPORT_PCLK,
	     21, 0, 0),

	/* UART */
	GATE(CLK_GOUT_PERI_UART_CLK, "gout_peri_uart_clk",
	     "mout_pll_peri_uart_user",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_RSTNSYNC_CLK_PERI_UART_IPCLKPORT_CLK,
	     21, 0, 0),

	/* USI */
	GATE(CLK_GOUT_PERI_USI00_I2C_CLK, "gout_peri_usi00_i2c_clk",
	     "dout_peri_usi00_i2c",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_RSTNSYNC_CLK_PERI_USI00_I2C_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERI_USI00_USI_CLK, "gout_peri_usi00_usi_clk",
	     "dout_peri_usi00_usi",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_RSTNSYNC_CLK_PERI_USI00_USI_IPCLKPORT_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERI_USI00_I2C_IPCLK, "gout_peri_usi00_i2c_ipclk",
	     "dout_peri_usi00_i2c",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_USI00_I2C_IPCLKPORT_IPCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERI_USI00_I2C_PCLK, "gout_peri_usi00_i2c_pclk",
	     "mout_pll_peri_bus_user",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_USI00_I2C_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERI_USI00_USI_IPCLK, "gout_peri_usi00_usi_ipclk",
	     "dout_peri_usi00_usi",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_USI00_USI_IPCLKPORT_IPCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERI_USI00_USI_PCLK, "gout_peri_usi00_usi_pclk",
	     "mout_pll_peri_bus_user",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_USI00_USI_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERI_USI_SPI_IPCLK, "gout_peri_usi_spi_ipclk",
	     "dout_peri_spi",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_USI_SPI_IPCLKPORT_IPCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERI_USI_SPI_PCLK, "gout_peri_usi_spi_pclk",
	     "mout_pll_peri_bus_user",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_USI_SPI_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERI_USI_UART_IPCLK, "gout_peri_usi_uart_ipclk",
	     "mout_pll_peri_uart_user",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_USI_UART_IPCLKPORT_IPCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERI_USI_UART_PCLK, "gout_peri_usi_uart_pclk",
	     "mout_pll_peri_bus_user",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_USI_UART_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERI_USI00_I2C, "gout_peri_usi00_i2c",
	     "mout_pll_peri_ip_user",
	     CLK_CON_GAT_GATE_CLK_PERI_USI00_I2C,
	     21, 0, 0),

	/* WDT (Watchdog Timer) */
	GATE(CLK_GOUT_PERI_WDT0_PCLK, "gout_peri_wdt0_pclk",
	     "mout_pll_peri_bus_user",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_WDT0_IPCLKPORT_PCLK,
	     21, 0, 0),
	GATE(CLK_GOUT_PERI_WDT1_PCLK, "gout_peri_wdt1_pclk",
	     "mout_pll_peri_bus_user",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_WDT1_IPCLKPORT_PCLK,
	     21, 0, 0),

	/* UNKNOWN */
	GATE(CLK_GOUT_PERI_SLH_AXI_MI_P_CLK, "gout_peri_slh_axi_mi_p_clk",
	     "mout_pll_peri_bus_user",
	     CLK_CON_GAT_CLK_BLK_PERI_UID_SLH_AXI_MI_P_PERI_IPCLKPORT_I_CLK,
	     21, 0, 0),
};

static const struct samsung_cmu_info peri_cmu_info __initconst = {
	.mux_clks		= peri_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(peri_mux_clks),
	.div_clks		= peri_div_clks,
	.nr_div_clks		= ARRAY_SIZE(peri_div_clks),
	.fixed_factor_clks	= peri_fixed_factor_clks,
	.nr_fixed_factor_clks	= ARRAY_SIZE(peri_fixed_factor_clks),
	.gate_clks		= peri_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(peri_gate_clks),
	.nr_clk_ids		= CLKS_NR_PERI,
	.clk_regs		= peri_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(peri_clk_regs),
	.sysreg_clk_regs	= drcg_sysreg,
	.nr_sysreg_clk_regs	= ARRAY_SIZE(drcg_sysreg),
	.clk_name		= "bus",
	.auto_clock_gate	= true,
	.gate_dbg_offset	= EXYNOS5515_GATE_DBG_OFFSET,
	.option_offset		= PERI_CMU_PERI_CONTROLLER_OPTION,
	.drcg_offset		= EXYNOS5515_DRCG_EN_OFFSET,
};

static void __init exynos5515_cmu_peri_init(struct device_node *np)
{
	exynos_arm64_register_cmu(NULL, np, &peri_cmu_info);
}

/* Register CMU_PERI early, as it's a dependency for the MCT. */
CLK_OF_DECLARE(exynos5515_cmu_peri, "samsung,exynos5515-cmu-peri",
	       exynos5515_cmu_peri_init);

/* ---- CMU_FSYS ------------------------------------------------------------ */

/*
 * CMU_FSYS:	0x10400000
 * SYSREG_FSYS:	0x10420000
 */

#define PLL_CON0_MUX_CLKCMU_FSYS_BUS_USER						0x0600
#define PLL_CON1_MUX_CLKCMU_FSYS_BUS_USER						0x0604
#define PLL_CON0_MUX_CLKCMU_FSYS_MMC_CARD_USER						0x0610
#define PLL_CON1_MUX_CLKCMU_FSYS_MMC_CARD_USER						0x0614
#define PLL_CON0_MUX_CLKCMU_FSYS_MMC_EMBD_USER						0x0620
#define PLL_CON1_MUX_CLKCMU_FSYS_MMC_EMBD_USER						0x0624
#define PLL_CON0_MUX_CLKCMU_FSYS_USB20DRD_USER						0x0630
#define PLL_CON1_MUX_CLKCMU_FSYS_USB20DRD_USER						0x0634
#define FSYS_CMU_FSYS_CONTROLLER_OPTION							0x0800
#define CLK_CON_GAT_CLK_BLK_FSYS_UID_FSYS_CMU_FSYS_IPCLKPORT_PCLK			0x2000
#define CLK_CON_GAT_CLK_BLK_FSYS_UID_MMC_CARD_IPCLKPORT_SDCLKIN				0x2004
#define CLK_CON_GAT_CLK_BLK_FSYS_UID_MMC_EMBD_IPCLKPORT_SDCLKIN				0x2008
#define CLK_CON_GAT_CLK_BLK_FSYS_UID_RSTNSYNC_CLK_FSYS_OSCCLK_IPCLKPORT_CLK		0x200c
#define CLK_CON_GAT_CLK_BLK_FSYS_UID_USB20DRD_TOP_IPCLKPORT_I_USB20DRD_REF_CLK_50	0x2010
#define CLK_CON_GAT_CLK_BLK_FSYS_UID_USB20DRD_TOP_IPCLKPORT_I_USB20_PHY_REFCLK_26	0x2014
#define CLK_CON_GAT_GOUT_BLK_FSYS_UID_D_TZPC_FSYS_IPCLKPORT_PCLK			0x2018
#define CLK_CON_GAT_GOUT_BLK_FSYS_UID_GPIO_FSYS_IPCLKPORT_PCLK				0x201c
#define CLK_CON_GAT_GOUT_BLK_FSYS_UID_MMC_CARD_IPCLKPORT_I_ACLK				0x2020
#define CLK_CON_GAT_GOUT_BLK_FSYS_UID_MMC_EMBD_IPCLKPORT_I_ACLK				0x2024
#define CLK_CON_GAT_GOUT_BLK_FSYS_UID_PPMU_FSYS_IPCLKPORT_ACLK				0x2028
#define CLK_CON_GAT_GOUT_BLK_FSYS_UID_PPMU_FSYS_IPCLKPORT_PCLK				0x202c
#define CLK_CON_GAT_GOUT_BLK_FSYS_UID_RSTNSYNC_CLK_FSYS_BUS_IPCLKPORT_CLK		0x2030
#define CLK_CON_GAT_GOUT_BLK_FSYS_UID_SLH_AXI_MI_P_FSYS_IPCLKPORT_I_CLK			0x2034
#define CLK_CON_GAT_GOUT_BLK_FSYS_UID_SLH_AXI_SI_D_FSYS_IPCLKPORT_I_CLK			0x2038
#define CLK_CON_GAT_GOUT_BLK_FSYS_UID_SYSREG_FSYS_IPCLKPORT_PCLK			0x203c
#define CLK_CON_GAT_GOUT_BLK_FSYS_UID_USB20DRD_TOP_IPCLKPORT_ACLK_PHYCTRL_20		0x2040
#define CLK_CON_GAT_GOUT_BLK_FSYS_UID_USB20DRD_TOP_IPCLKPORT_BUS_CLK_EARLY		0x2044
#define CLK_CON_GAT_GOUT_BLK_FSYS_UID_US_64TO128_FSYS_IPCLKPORT_ACLK			0x2048
#define CLK_CON_GAT_GOUT_BLK_FSYS_UID_XIU_D_FSYS_IPCLKPORT_ACLK				0x204c
#define CLK_CON_GAT_GOUT_BLK_FSYS_UID_XIU_P_FSYS_IPCLKPORT_ACLK				0x2050

static const unsigned long fsys_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLKCMU_FSYS_BUS_USER,
	PLL_CON1_MUX_CLKCMU_FSYS_BUS_USER,
	PLL_CON0_MUX_CLKCMU_FSYS_MMC_CARD_USER,
	PLL_CON1_MUX_CLKCMU_FSYS_MMC_CARD_USER,
	PLL_CON0_MUX_CLKCMU_FSYS_MMC_EMBD_USER,
	PLL_CON1_MUX_CLKCMU_FSYS_MMC_EMBD_USER,
	PLL_CON0_MUX_CLKCMU_FSYS_USB20DRD_USER,
	PLL_CON1_MUX_CLKCMU_FSYS_USB20DRD_USER,
	FSYS_CMU_FSYS_CONTROLLER_OPTION,
	CLK_CON_GAT_CLK_BLK_FSYS_UID_FSYS_CMU_FSYS_IPCLKPORT_PCLK,
	CLK_CON_GAT_CLK_BLK_FSYS_UID_MMC_CARD_IPCLKPORT_SDCLKIN,
	CLK_CON_GAT_CLK_BLK_FSYS_UID_MMC_EMBD_IPCLKPORT_SDCLKIN,
	CLK_CON_GAT_CLK_BLK_FSYS_UID_RSTNSYNC_CLK_FSYS_OSCCLK_IPCLKPORT_CLK,
	CLK_CON_GAT_CLK_BLK_FSYS_UID_USB20DRD_TOP_IPCLKPORT_I_USB20DRD_REF_CLK_50,
	CLK_CON_GAT_CLK_BLK_FSYS_UID_USB20DRD_TOP_IPCLKPORT_I_USB20_PHY_REFCLK_26,
	CLK_CON_GAT_GOUT_BLK_FSYS_UID_D_TZPC_FSYS_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_FSYS_UID_GPIO_FSYS_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_FSYS_UID_MMC_CARD_IPCLKPORT_I_ACLK,
	CLK_CON_GAT_GOUT_BLK_FSYS_UID_MMC_EMBD_IPCLKPORT_I_ACLK,
	CLK_CON_GAT_GOUT_BLK_FSYS_UID_PPMU_FSYS_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_FSYS_UID_PPMU_FSYS_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_FSYS_UID_RSTNSYNC_CLK_FSYS_BUS_IPCLKPORT_CLK,
	CLK_CON_GAT_GOUT_BLK_FSYS_UID_SLH_AXI_MI_P_FSYS_IPCLKPORT_I_CLK,
	CLK_CON_GAT_GOUT_BLK_FSYS_UID_SLH_AXI_SI_D_FSYS_IPCLKPORT_I_CLK,
	CLK_CON_GAT_GOUT_BLK_FSYS_UID_SYSREG_FSYS_IPCLKPORT_PCLK,
	CLK_CON_GAT_GOUT_BLK_FSYS_UID_USB20DRD_TOP_IPCLKPORT_ACLK_PHYCTRL_20,
	CLK_CON_GAT_GOUT_BLK_FSYS_UID_USB20DRD_TOP_IPCLKPORT_BUS_CLK_EARLY,
	CLK_CON_GAT_GOUT_BLK_FSYS_UID_US_64TO128_FSYS_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_FSYS_UID_XIU_D_FSYS_IPCLKPORT_ACLK,
	CLK_CON_GAT_GOUT_BLK_FSYS_UID_XIU_P_FSYS_IPCLKPORT_ACLK,
};

/* Parent clock list for CMU_FSYS muxes */
PNAME(mout_pll_fsys_bus_user_p)		= { "oscclk",
					    "dout_cmu_fsys_bus" };

/* Parent clock list for CMU_FSYS muxes: for MMC */
PNAME(mout_pll_fsys_mmc_card_user_p)	= { "oscclk",
					    "dout_cmu_fsys_mmc_card" };

PNAME(mout_pll_fsys_mmc_embd_user_p)	= { "oscclk",
					    "dout_cmu_fsys_mmc_embd" };

/* Parent clock list for CMU_FSYS muxes: for USB */
PNAME(mout_pll_fsys_usb20drd_user_p)	= { "oscclk",
					    "dout_cmu_fsys_usb20drd" };

static const struct samsung_mux_clock fsys_mux_clks[] __initconst = {
	/* BUS */
	MUX(CLK_MOUT_PLL_FSYS_BUS_USER, "mout_pll_fsys_bus_user",
	    mout_pll_fsys_bus_user_p,
	    PLL_CON0_MUX_CLKCMU_FSYS_BUS_USER,
	    4, 1),

	/* MMC */
	MUX(CLK_MOUT_PLL_FSYS_MMC_CARD_USER, "mout_pll_fsys_mmc_card_user",
	    mout_pll_fsys_mmc_card_user_p,
	    PLL_CON0_MUX_CLKCMU_FSYS_MMC_CARD_USER,
	    4, 1),
	MUX(CLK_MOUT_PLL_FSYS_MMC_EMBD_USER, "mout_pll_fsys_mmc_embd_user",
	    mout_pll_fsys_mmc_embd_user_p,
	    PLL_CON0_MUX_CLKCMU_FSYS_MMC_EMBD_USER,
	    4, 1),

	/* USB */
	MUX(CLK_MOUT_PLL_FSYS_USB20DRD_USER, "mout_pll_fsys_usb20drd_user",
	    mout_pll_fsys_usb20drd_user_p,
	    PLL_CON0_MUX_CLKCMU_FSYS_USB20DRD_USER,
	    4, 1),
};

static const struct samsung_gate_clock fsys_gate_clks[] __initconst = {
	/* BUS */
	GATE(CLK_GOUT_FSYS_BUS_CLK, "gout_fsys_bus_clk",
	     "mout_pll_fsys_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS_UID_RSTNSYNC_CLK_FSYS_BUS_IPCLKPORT_CLK,
	     21, 0, 0),

	/* CMU */
	GATE(CLK_GOUT_FSYS_CMU_PCLK, "gout_fsys_cmu_pclk",
	     "mout_pll_fsys_bus_user",
	     CLK_CON_GAT_CLK_BLK_FSYS_UID_FSYS_CMU_FSYS_IPCLKPORT_PCLK,
	     21, 0, 0),

	/* GPIO */
	GATE(CLK_GOUT_FSYS_GPIO_PCLK, "gout_fsys_gpio_pclk",
	     "mout_pll_fsys_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS_UID_GPIO_FSYS_IPCLKPORT_PCLK,
	     21, 0, 0),

	/* MMC */
	GATE(CLK_GOUT_FSYS_MMC_CARD_SDCLKIN, "gout_fsys_mmc_card_sdclkin",
	     "mout_pll_fsys_mmc_card_user",
	     CLK_CON_GAT_CLK_BLK_FSYS_UID_MMC_CARD_IPCLKPORT_SDCLKIN,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS_MMC_EMBD_SDCLKIN, "gout_fsys_mmc_embd_sdclkin",
	     "mout_pll_fsys_mmc_embd_user",
	     CLK_CON_GAT_CLK_BLK_FSYS_UID_MMC_EMBD_IPCLKPORT_SDCLKIN,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS_MMC_CARD_ACLK, "gout_fsys_mmc_card_aclk",
	     "mout_pll_fsys_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS_UID_MMC_CARD_IPCLKPORT_I_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS_MMC_EMBD_ACLK, "gout_fsys_mmc_embd_aclk",
	     "mout_pll_fsys_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS_UID_MMC_EMBD_IPCLKPORT_I_ACLK,
	     21, 0, 0),

	/* OSCCLK */
	GATE(CLK_GOUT_FSYS_OSCCLK, "gout_fsys_oscclk",
	     "oscclk",
	     CLK_CON_GAT_CLK_BLK_FSYS_UID_RSTNSYNC_CLK_FSYS_OSCCLK_IPCLKPORT_CLK,
	     21, 0, 0),

	/* PMU */
	GATE(CLK_GOUT_FSYS_PPMU_ACLK, "gout_fsys_ppmu_aclk",
	     "mout_pll_fsys_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS_UID_PPMU_FSYS_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS_PPMU_PCLK, "gout_fsys_ppmu_pclk",
	     "mout_pll_fsys_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS_UID_PPMU_FSYS_IPCLKPORT_PCLK,
	     21, 0, 0),

	/* SYSREG */
	GATE(CLK_GOUT_FSYS_SYSREG_PCLK, "gout_fsys_sysreg_pclk",
	     "mout_pll_fsys_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS_UID_SYSREG_FSYS_IPCLKPORT_PCLK,
	     21, 0, 0),

	/* TZ */
	GATE(CLK_GOUT_FSYS_D_TZPC_PCLK, "gout_fsys_d_tzpc_pclk",
	     "mout_pll_fsys_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS_UID_D_TZPC_FSYS_IPCLKPORT_PCLK,
	     21, 0, 0),

	/* USB */
	GATE(CLK_GOUT_FSYS_USB20DRD_REFCLK,
	     "gout_fsys_usb20drd_refclk",
	     "mout_pll_fsys_usb20drd_user",
	     CLK_CON_GAT_CLK_BLK_FSYS_UID_USB20DRD_TOP_IPCLKPORT_I_USB20DRD_REF_CLK_50,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS_USB20DRD_PHY_REFCLK,
	     "gout_fsys_usb20drd_phy_refclk",
	     "oscclk",
	     CLK_CON_GAT_CLK_BLK_FSYS_UID_USB20DRD_TOP_IPCLKPORT_I_USB20_PHY_REFCLK_26,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS_USB20DRD_ACLK_PHYCTRL,
	     "gout_fsys_usb20drd_aclk_phyctrl",
	     "mout_pll_fsys_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS_UID_USB20DRD_TOP_IPCLKPORT_ACLK_PHYCTRL_20,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS_USB20DRD_BUS_CLK_EARLY,
	     "gout_fsys_usb20drd_bus_clk_early",
	     "mout_pll_fsys_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS_UID_USB20DRD_TOP_IPCLKPORT_BUS_CLK_EARLY,
	     21, 0, 0),

	/* UNKNOWN */
	GATE(CLK_GOUT_FSYS_SLH_AXI_MI_P_CLK, "gout_fsys_slh_axi_mi_p_clk",
	     "mout_pll_fsys_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS_UID_SLH_AXI_MI_P_FSYS_IPCLKPORT_I_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS_SLH_AXI_SI_D_CLK, "gout_fsys_slh_axi_si_d_clk",
	     "mout_pll_fsys_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS_UID_SLH_AXI_SI_D_FSYS_IPCLKPORT_I_CLK,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS_US_64TO128_ACLK, "gout_fsys_us_64to128_aclk",
	     "mout_pll_fsys_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS_UID_US_64TO128_FSYS_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS_XIU_D_ACLK, "gout_fsys_xiu_d_aclk",
	     "mout_pll_fsys_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS_UID_XIU_D_FSYS_IPCLKPORT_ACLK,
	     21, 0, 0),
	GATE(CLK_GOUT_FSYS_XIU_P_ACLK, "gout_fsys_xiu_p_aclk",
	     "mout_pll_fsys_bus_user",
	     CLK_CON_GAT_GOUT_BLK_FSYS_UID_XIU_P_FSYS_IPCLKPORT_ACLK,
	     21, 0, 0),
};

static const struct samsung_cmu_info fsys_cmu_info __initconst = {
	.mux_clks		= fsys_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(fsys_mux_clks),
	.gate_clks		= fsys_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(fsys_gate_clks),
	.nr_clk_ids		= CLKS_NR_FSYS,
	.clk_regs		= fsys_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(fsys_clk_regs),
	.sysreg_clk_regs	= drcg_sysreg,
	.nr_sysreg_clk_regs	= ARRAY_SIZE(drcg_sysreg),
	.clk_name		= "bus",
	.auto_clock_gate	= true,
	.gate_dbg_offset	= EXYNOS5515_GATE_DBG_OFFSET,
	.option_offset		= FSYS_CMU_FSYS_CONTROLLER_OPTION,
	.drcg_offset		= EXYNOS5515_DRCG_EN_OFFSET,
};

/* ----- platform_driver ----- */

static int __init exynos5515_cmu_probe(struct platform_device *pdev)
{
	const struct samsung_cmu_info *info;
	struct device *dev = &pdev->dev;

	info = of_device_get_match_data(dev);
	exynos_arm64_register_cmu(dev, dev->of_node, info);

	return 0;
}

static const struct of_device_id exynos5515_cmu_of_match[] = {
	{
		.compatible = "samsung,exynos5515-cmu-fsys",
		.data = &fsys_cmu_info,
	},
	{ },
};

static struct platform_driver exynos5515_cmu_driver __refdata = {
	.driver = {
		.name = "exynos5515-cmu",
		.of_match_table = exynos5515_cmu_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = exynos5515_cmu_probe,
};

static int __init exynos5515_cmu_init(void)
{
	return platform_driver_register(&exynos5515_cmu_driver);
}

core_initcall(exynos5515_cmu_init);
