// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025 Samsung Electronics Co., Ltd.
 *             https://www.samsung.com
 * Copyright (c) 2025 Axis Communications AB.
 *             https://www.axis.com
 *
 * Common Clock Framework support for ARTPEC-8 SoC.
 */

#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/axis,artpec8-clk.h>

#include "clk.h"
#include "clk-exynos-arm64.h"

/* NOTE: Must be equal to the last clock ID increased by one */
#define CMU_CMU_NR_CLK				(CLK_DOUT_CMU_VPP_CORE + 1)
#define CMU_BUS_NR_CLK				(CLK_DOUT_BUS_PCLK + 1)
#define CMU_CORE_NR_CLK				(CLK_DOUT_CORE_PCLK + 1)
#define CMU_CPUCL_NR_CLK			(CLK_GOUT_CPUCL_CSSYS_IPCLKPORT_ATCLK + 1)
#define CMU_FSYS_NR_CLK				(CLK_GOUT_FSYS_QSPI_IPCLKPORT_SSI_CLK + 1)
#define CMU_IMEM_NR_CLK				(CLK_GOUT_IMEM_PCLK_TMU0_APBIF + 1)
#define CMU_PERI_NR_CLK				(CLK_GOUT_PERI_DMA4DSIM_IPCLKPORT_CLK_AXI_CLK + 1)

/* Register Offset definitions for CMU_CMU (0x12400000) */
#define PLL_LOCKTIME_PLL_AUDIO				0x0000
#define PLL_LOCKTIME_PLL_SHARED0			0x0004
#define PLL_LOCKTIME_PLL_SHARED1			0x0008
#define PLL_CON0_PLL_AUDIO				0x0100
#define PLL_CON0_PLL_SHARED0				0x0120
#define PLL_CON0_PLL_SHARED1				0x0140
#define CLK_CON_MUX_CLKCMU_2D				0x1000
#define CLK_CON_MUX_CLKCMU_3D				0x1004
#define CLK_CON_MUX_CLKCMU_BUS				0x1008
#define CLK_CON_MUX_CLKCMU_BUS_DLP			0x100c
#define CLK_CON_MUX_CLKCMU_CDC_CORE			0x1010
#define CLK_CON_MUX_CLKCMU_FSYS_SCAN0			0x1014
#define CLK_CON_MUX_CLKCMU_FSYS_SCAN1			0x1018
#define CLK_CON_MUX_CLKCMU_IMEM_JPEG			0x101c
#define CLK_CON_MUX_CLKCMU_PERI_DISP			0x1020
#define CLK_CON_MUX_CLKCMU_CORE_BUS			0x1024
#define CLK_CON_MUX_CLKCMU_CORE_DLP			0x1028
#define CLK_CON_MUX_CLKCMU_CPUCL_SWITCH			0x1030
#define CLK_CON_MUX_CLKCMU_DLP_CORE			0x1034
#define CLK_CON_MUX_CLKCMU_FSYS_BUS			0x1038
#define CLK_CON_MUX_CLKCMU_FSYS_IP			0x103c
#define CLK_CON_MUX_CLKCMU_IMEM_ACLK			0x1054
#define CLK_CON_MUX_CLKCMU_MIF_BUSP			0x1080
#define CLK_CON_MUX_CLKCMU_MIF_SWITCH			0x1084
#define CLK_CON_MUX_CLKCMU_PERI_IP			0x1088
#define CLK_CON_MUX_CLKCMU_RSP_CORE			0x108c
#define CLK_CON_MUX_CLKCMU_TRFM_CORE			0x1090
#define CLK_CON_MUX_CLKCMU_VCA_ACE			0x1094
#define CLK_CON_MUX_CLKCMU_VCA_OD			0x1098
#define CLK_CON_MUX_CLKCMU_VIO_CORE			0x109c
#define CLK_CON_MUX_CLKCMU_VIP0_CORE			0x10a0
#define CLK_CON_MUX_CLKCMU_VIP1_CORE			0x10a4
#define CLK_CON_MUX_CLKCMU_VPP_CORE			0x10a8

#define CLK_CON_DIV_CLKCMU_BUS				0x1800
#define CLK_CON_DIV_CLKCMU_BUS_DLP			0x1804
#define CLK_CON_DIV_CLKCMU_CDC_CORE			0x1808
#define CLK_CON_DIV_CLKCMU_FSYS_SCAN0			0x180c
#define CLK_CON_DIV_CLKCMU_FSYS_SCAN1			0x1810
#define CLK_CON_DIV_CLKCMU_IMEM_JPEG			0x1814
#define CLK_CON_DIV_CLKCMU_MIF_SWITCH			0x1818
#define CLK_CON_DIV_CLKCMU_CORE_DLP			0x181c
#define CLK_CON_DIV_CLKCMU_CORE_MAIN			0x1820
#define CLK_CON_DIV_CLKCMU_PERI_DISP			0x1824
#define CLK_CON_DIV_CLKCMU_CPUCL_SWITCH			0x1828
#define CLK_CON_DIV_CLKCMU_DLP_CORE			0x182c
#define CLK_CON_DIV_CLKCMU_FSYS_BUS			0x1830
#define CLK_CON_DIV_CLKCMU_FSYS_IP			0x1834
#define CLK_CON_DIV_CLKCMU_VIO_AUDIO			0x1838
#define CLK_CON_DIV_CLKCMU_GPU_2D			0x1848
#define CLK_CON_DIV_CLKCMU_GPU_3D			0x184c
#define CLK_CON_DIV_CLKCMU_IMEM_ACLK			0x1854
#define CLK_CON_DIV_CLKCMU_MIF_BUSP			0x1884
#define CLK_CON_DIV_CLKCMU_PERI_AUDIO			0x1890
#define CLK_CON_DIV_CLKCMU_PERI_IP			0x1894
#define CLK_CON_DIV_CLKCMU_RSP_CORE			0x1898
#define CLK_CON_DIV_CLKCMU_TRFM_CORE			0x189c
#define CLK_CON_DIV_CLKCMU_VCA_ACE			0x18a0
#define CLK_CON_DIV_CLKCMU_VCA_OD			0x18a4
#define CLK_CON_DIV_CLKCMU_VIO_CORE			0x18ac
#define CLK_CON_DIV_CLKCMU_VIP0_CORE			0x18b0
#define CLK_CON_DIV_CLKCMU_VIP1_CORE			0x18b4
#define CLK_CON_DIV_CLKCMU_VPP_CORE			0x18b8
#define CLK_CON_DIV_PLL_SHARED0_DIV2			0x18bc
#define CLK_CON_DIV_PLL_SHARED0_DIV3			0x18c0
#define CLK_CON_DIV_PLL_SHARED0_DIV4			0x18c4
#define CLK_CON_DIV_PLL_SHARED1_DIV2			0x18c8
#define CLK_CON_DIV_PLL_SHARED1_DIV3			0x18cc
#define CLK_CON_DIV_PLL_SHARED1_DIV4			0x18d0

static const unsigned long cmu_cmu_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_AUDIO,
	PLL_LOCKTIME_PLL_SHARED0,
	PLL_LOCKTIME_PLL_SHARED1,
	PLL_CON0_PLL_AUDIO,
	PLL_CON0_PLL_SHARED0,
	PLL_CON0_PLL_SHARED1,
	CLK_CON_MUX_CLKCMU_2D,
	CLK_CON_MUX_CLKCMU_3D,
	CLK_CON_MUX_CLKCMU_BUS,
	CLK_CON_MUX_CLKCMU_BUS_DLP,
	CLK_CON_MUX_CLKCMU_CDC_CORE,
	CLK_CON_MUX_CLKCMU_FSYS_SCAN0,
	CLK_CON_MUX_CLKCMU_FSYS_SCAN1,
	CLK_CON_MUX_CLKCMU_IMEM_JPEG,
	CLK_CON_MUX_CLKCMU_PERI_DISP,
	CLK_CON_MUX_CLKCMU_CORE_BUS,
	CLK_CON_MUX_CLKCMU_CORE_DLP,
	CLK_CON_MUX_CLKCMU_CPUCL_SWITCH,
	CLK_CON_MUX_CLKCMU_DLP_CORE,
	CLK_CON_MUX_CLKCMU_FSYS_BUS,
	CLK_CON_MUX_CLKCMU_FSYS_IP,
	CLK_CON_MUX_CLKCMU_IMEM_ACLK,
	CLK_CON_MUX_CLKCMU_MIF_BUSP,
	CLK_CON_MUX_CLKCMU_MIF_SWITCH,
	CLK_CON_MUX_CLKCMU_PERI_IP,
	CLK_CON_MUX_CLKCMU_RSP_CORE,
	CLK_CON_MUX_CLKCMU_TRFM_CORE,
	CLK_CON_MUX_CLKCMU_VCA_ACE,
	CLK_CON_MUX_CLKCMU_VCA_OD,
	CLK_CON_MUX_CLKCMU_VIO_CORE,
	CLK_CON_MUX_CLKCMU_VIP0_CORE,
	CLK_CON_MUX_CLKCMU_VIP1_CORE,
	CLK_CON_MUX_CLKCMU_VPP_CORE,
	CLK_CON_DIV_CLKCMU_BUS,
	CLK_CON_DIV_CLKCMU_BUS_DLP,
	CLK_CON_DIV_CLKCMU_CDC_CORE,
	CLK_CON_DIV_CLKCMU_FSYS_SCAN0,
	CLK_CON_DIV_CLKCMU_FSYS_SCAN1,
	CLK_CON_DIV_CLKCMU_IMEM_JPEG,
	CLK_CON_DIV_CLKCMU_MIF_SWITCH,
	CLK_CON_DIV_CLKCMU_CORE_DLP,
	CLK_CON_DIV_CLKCMU_CORE_MAIN,
	CLK_CON_DIV_CLKCMU_PERI_DISP,
	CLK_CON_DIV_CLKCMU_CPUCL_SWITCH,
	CLK_CON_DIV_CLKCMU_DLP_CORE,
	CLK_CON_DIV_CLKCMU_FSYS_BUS,
	CLK_CON_DIV_CLKCMU_FSYS_IP,
	CLK_CON_DIV_CLKCMU_VIO_AUDIO,
	CLK_CON_DIV_CLKCMU_GPU_2D,
	CLK_CON_DIV_CLKCMU_GPU_3D,
	CLK_CON_DIV_CLKCMU_IMEM_ACLK,
	CLK_CON_DIV_CLKCMU_MIF_BUSP,
	CLK_CON_DIV_CLKCMU_PERI_AUDIO,
	CLK_CON_DIV_CLKCMU_PERI_IP,
	CLK_CON_DIV_CLKCMU_RSP_CORE,
	CLK_CON_DIV_CLKCMU_TRFM_CORE,
	CLK_CON_DIV_CLKCMU_VCA_ACE,
	CLK_CON_DIV_CLKCMU_VCA_OD,
	CLK_CON_DIV_CLKCMU_VIO_CORE,
	CLK_CON_DIV_CLKCMU_VIP0_CORE,
	CLK_CON_DIV_CLKCMU_VIP1_CORE,
	CLK_CON_DIV_CLKCMU_VPP_CORE,
	CLK_CON_DIV_PLL_SHARED0_DIV2,
	CLK_CON_DIV_PLL_SHARED0_DIV3,
	CLK_CON_DIV_PLL_SHARED0_DIV4,
	CLK_CON_DIV_PLL_SHARED1_DIV2,
	CLK_CON_DIV_PLL_SHARED1_DIV3,
	CLK_CON_DIV_PLL_SHARED1_DIV4,
};

static const struct samsung_pll_rate_table artpec8_pll_audio_rates[] __initconst = {
	PLL_36XX_RATE(25 * MHZ, 589823913U, 47, 1, 1, 12184),
	PLL_36XX_RATE(25 * MHZ, 393215942U, 47, 3, 0, 12184),
	PLL_36XX_RATE(25 * MHZ, 294911956U, 47, 1, 2, 12184),
	PLL_36XX_RATE(25 * MHZ, 100000000U, 32, 2, 2, 0),
	PLL_36XX_RATE(25 * MHZ,  98303985U, 47, 3, 2, 12184),
	PLL_36XX_RATE(25 * MHZ,  49151992U, 47, 3, 3, 12184),
};

static const struct samsung_pll_clock cmu_cmu_pll_clks[] __initconst = {
	PLL(pll_1017x, CLK_FOUT_SHARED0_PLL, "fout_pll_shared0", "fin_pll",
	    PLL_LOCKTIME_PLL_SHARED0, PLL_CON0_PLL_SHARED0, NULL),
	PLL(pll_1017x, CLK_FOUT_SHARED1_PLL, "fout_pll_shared1", "fin_pll",
	    PLL_LOCKTIME_PLL_SHARED1, PLL_CON0_PLL_SHARED1, NULL),
	PLL(pll_1031x, CLK_FOUT_AUDIO_PLL, "fout_pll_audio", "fin_pll",
	    PLL_LOCKTIME_PLL_AUDIO, PLL_CON0_PLL_AUDIO, artpec8_pll_audio_rates),
};

PNAME(mout_clkcmu_bus_bus_p) = { "dout_pll_shared1_div2", "dout_pll_shared0_div3",
				 "dout_pll_shared1_div3", "dout_pll_shared1_div4" };
PNAME(mout_clkcmu_bus_dlp_p) = { "dout_pll_shared0_div2", "dout_pll_shared0_div4",
				 "dout_pll_shared1_div2", "dout_pll_shared1_div4" };
PNAME(mout_clkcmu_core_bus_p) = { "dout_pll_shared1_div2", "dout_pll_shared0_div3",
				  "dout_pll_shared0_div4", "dout_pll_shared1_div3" };
PNAME(mout_clkcmu_core_dlp_p) = { "dout_pll_shared0_div2", "dout_pll_shared1_div2",
				  "dout_pll_shared0_div3", "dout_pll_shared1_div3" };
PNAME(mout_clkcmu_cpucl_switch_p) = { "dout_pll_shared0_div2", "dout_pll_shared1_div2",
				      "dout_pll_shared0_div3", "dout_pll_shared1_div3" };
PNAME(mout_clkcmu_fsys_bus_p) = { "dout_pll_shared1_div2", "dout_pll_shared0_div2",
				  "dout_pll_shared1_div4", "dout_pll_shared1_div3" };
PNAME(mout_clkcmu_fsys_ip_p) = { "dout_pll_shared0_div2", "dout_pll_shared1_div3",
				 "dout_pll_shared1_div2", "dout_pll_shared0_div3" };
PNAME(mout_clkcmu_fsys_scan0_p) = { "dout_pll_shared0_div4", "dout_pll_shared1_div4" };
PNAME(mout_clkcmu_fsys_scan1_p) = { "dout_pll_shared0_div4", "dout_pll_shared1_div4" };
PNAME(mout_clkcmu_imem_imem_p) = { "dout_pll_shared1_div4", "dout_pll_shared0_div3",
				   "dout_pll_shared1_div3", "dout_pll_shared1_div2" };
PNAME(mout_clkcmu_imem_jpeg_p) = { "dout_pll_shared0_div2", "dout_pll_shared0_div3",
				   "dout_pll_shared1_div2", "dout_pll_shared1_div3" };
PNAME(mout_clkcmu_cdc_core_p) = { "dout_pll_shared1_div2", "dout_pll_shared0_div3",
				  "dout_pll_shared1_div3", "dout_pll_shared1_div4" };
PNAME(mout_clkcmu_dlp_core_p) = { "dout_pll_shared0_div2", "dout_pll_shared1_div2",
				  "dout_pll_shared0_div3", "dout_pll_shared1_div3" };
PNAME(mout_clkcmu_3d_p) = { "dout_pll_shared0_div2", "dout_pll_shared1_div2",
			    "dout_pll_shared0_div3", "dout_pll_shared1_div3" };
PNAME(mout_clkcmu_2d_p) = { "dout_pll_shared0_div2", "dout_pll_shared1_div2",
			    "dout_pll_shared0_div3", "dout_pll_shared1_div3" };
PNAME(mout_clkcmu_mif_switch_p) = { "dout_pll_shared0", "dout_pll_shared1",
				    "dout_pll_shared0_div2", "dout_pll_shared0_div3" };
PNAME(mout_clkcmu_mif_busp_p) = { "dout_pll_shared0_div3", "dout_pll_shared1_div4",
				  "dout_pll_shared0_div4", "dout_pll_shared0_div2" };
PNAME(mout_clkcmu_peri_disp_p) = { "dout_pll_shared1_div2", "dout_pll_shared0_div2",
				   "dout_pll_shared1_div4", "dout_pll_shared1_div3" };
PNAME(mout_clkcmu_peri_ip_p) = { "dout_pll_shared1_div2", "dout_pll_shared0_div4",
				 "dout_pll_shared1_div4", "dout_pll_shared0_div2" };
PNAME(mout_clkcmu_rsp_core_p) = { "dout_pll_shared1_div2", "dout_pll_shared0_div3",
				  "dout_pll_shared1_div3", "dout_pll_shared1_div4" };
PNAME(mout_clkcmu_trfm_core_p) = { "dout_pll_shared1_div2", "dout_pll_shared0_div3",
				   "dout_pll_shared1_div3", "dout_pll_shared1_div4" };
PNAME(mout_clkcmu_vca_ace_p) = { "dout_pll_shared1_div2", "dout_pll_shared0_div3",
				 "dout_pll_shared1_div3", "dout_pll_shared1_div4" };
PNAME(mout_clkcmu_vca_od_p) = { "dout_pll_shared1_div2", "dout_pll_shared0_div3",
				"dout_pll_shared1_div3", "dout_pll_shared1_div4" };
PNAME(mout_clkcmu_vio_core_p) = { "dout_pll_shared0_div3", "dout_pll_shared0_div2",
				  "dout_pll_shared1_div2", "dout_pll_shared1_div3" };
PNAME(mout_clkcmu_vip0_core_p) = { "dout_pll_shared1_div2", "dout_pll_shared0_div3",
				   "dout_pll_shared1_div3", "dout_pll_shared1_div4" };
PNAME(mout_clkcmu_vip1_core_p) = { "dout_pll_shared1_div2", "dout_pll_shared0_div3",
				   "dout_pll_shared1_div3", "dout_pll_shared1_div4" };
PNAME(mout_clkcmu_vpp_core_p) = { "dout_pll_shared1_div2", "dout_pll_shared0_div3",
				  "dout_pll_shared1_div3", "dout_pll_shared1_div4" };
PNAME(mout_clkcmu_pll_shared0_p) = { "fin_pll", "fout_pll_shared0" };
PNAME(mout_clkcmu_pll_shared1_p) = { "fin_pll", "fout_pll_shared1" };
PNAME(mout_clkcmu_pll_audio_p) = { "fin_pll", "fout_pll_audio" };

static const struct samsung_fixed_factor_clock cmu_fixed_factor_clks[] __initconst = {
	FFACTOR(CLK_DOUT_CMU_OTP, "dout_clkcmu_otp", "fin_pll", 1, 8, 0),
};

static const struct samsung_mux_clock cmu_cmu_mux_clks[] __initconst = {
	MUX(0, "mout_clkcmu_pll_shared0", mout_clkcmu_pll_shared0_p, PLL_CON0_PLL_SHARED0, 4, 1),
	MUX(0, "mout_clkcmu_pll_shared1", mout_clkcmu_pll_shared1_p, PLL_CON0_PLL_SHARED1, 4, 1),
	MUX(0, "mout_clkcmu_pll_audio", mout_clkcmu_pll_audio_p, PLL_CON0_PLL_AUDIO, 4, 1),
	MUX(0, "mout_clkcmu_bus_bus", mout_clkcmu_bus_bus_p, CLK_CON_MUX_CLKCMU_BUS, 0, 2),
	MUX(0, "mout_clkcmu_bus_dlp", mout_clkcmu_bus_dlp_p, CLK_CON_MUX_CLKCMU_BUS_DLP, 0, 2),
	MUX(0, "mout_clkcmu_core_bus", mout_clkcmu_core_bus_p, CLK_CON_MUX_CLKCMU_CORE_BUS, 0, 2),
	MUX(0, "mout_clkcmu_core_dlp", mout_clkcmu_core_dlp_p, CLK_CON_MUX_CLKCMU_CORE_DLP, 0, 2),
	MUX(0, "mout_clkcmu_cpucl_switch", mout_clkcmu_cpucl_switch_p,
	    CLK_CON_MUX_CLKCMU_CPUCL_SWITCH, 0, 3),
	MUX(0, "mout_clkcmu_fsys_bus", mout_clkcmu_fsys_bus_p, CLK_CON_MUX_CLKCMU_FSYS_BUS, 0, 2),
	MUX(0, "mout_clkcmu_fsys_ip", mout_clkcmu_fsys_ip_p, CLK_CON_MUX_CLKCMU_FSYS_IP, 0, 2),
	MUX(0, "mout_clkcmu_fsys_scan0", mout_clkcmu_fsys_scan0_p,
	    CLK_CON_MUX_CLKCMU_FSYS_SCAN0, 0, 1),
	MUX(0, "mout_clkcmu_fsys_scan1", mout_clkcmu_fsys_scan1_p,
	    CLK_CON_MUX_CLKCMU_FSYS_SCAN1, 0, 1),
	MUX(0, "mout_clkcmu_imem_imem", mout_clkcmu_imem_imem_p,
	    CLK_CON_MUX_CLKCMU_IMEM_ACLK, 0, 2),
	MUX(0, "mout_clkcmu_imem_jpeg", mout_clkcmu_imem_jpeg_p,
	    CLK_CON_MUX_CLKCMU_IMEM_JPEG, 0, 2),
	nMUX(0, "mout_clkcmu_cdc_core", mout_clkcmu_cdc_core_p, CLK_CON_MUX_CLKCMU_CDC_CORE, 0, 2),
	nMUX(0, "mout_clkcmu_dlp_core", mout_clkcmu_dlp_core_p, CLK_CON_MUX_CLKCMU_DLP_CORE, 0, 2),
	MUX(0, "mout_clkcmu_3d", mout_clkcmu_3d_p, CLK_CON_MUX_CLKCMU_3D, 0, 2),
	MUX(0, "mout_clkcmu_2d", mout_clkcmu_2d_p, CLK_CON_MUX_CLKCMU_2D, 0, 2),
	MUX(0, "mout_clkcmu_mif_switch", mout_clkcmu_mif_switch_p,
	    CLK_CON_MUX_CLKCMU_MIF_SWITCH, 0, 2),
	MUX(0, "mout_clkcmu_mif_busp", mout_clkcmu_mif_busp_p, CLK_CON_MUX_CLKCMU_MIF_BUSP, 0, 2),
	MUX(0, "mout_clkcmu_peri_disp", mout_clkcmu_peri_disp_p,
	    CLK_CON_MUX_CLKCMU_PERI_DISP, 0, 2),
	MUX(0, "mout_clkcmu_peri_ip", mout_clkcmu_peri_ip_p, CLK_CON_MUX_CLKCMU_PERI_IP, 0, 2),
	MUX(0, "mout_clkcmu_rsp_core", mout_clkcmu_rsp_core_p, CLK_CON_MUX_CLKCMU_RSP_CORE, 0, 2),
	nMUX(0, "mout_clkcmu_trfm_core", mout_clkcmu_trfm_core_p,
	     CLK_CON_MUX_CLKCMU_TRFM_CORE, 0, 2),
	MUX(0, "mout_clkcmu_vca_ace", mout_clkcmu_vca_ace_p, CLK_CON_MUX_CLKCMU_VCA_ACE, 0, 2),
	MUX(0, "mout_clkcmu_vca_od", mout_clkcmu_vca_od_p, CLK_CON_MUX_CLKCMU_VCA_OD, 0, 2),
	MUX(0, "mout_clkcmu_vio_core", mout_clkcmu_vio_core_p, CLK_CON_MUX_CLKCMU_VIO_CORE, 0, 2),
	nMUX(0, "mout_clkcmu_vip0_core", mout_clkcmu_vip0_core_p,
	     CLK_CON_MUX_CLKCMU_VIP0_CORE, 0, 2),
	nMUX(0, "mout_clkcmu_vip1_core", mout_clkcmu_vip1_core_p,
	     CLK_CON_MUX_CLKCMU_VIP1_CORE, 0, 2),
	nMUX(0, "mout_clkcmu_vpp_core", mout_clkcmu_vpp_core_p, CLK_CON_MUX_CLKCMU_VPP_CORE, 0, 2),
};

static const struct samsung_div_clock cmu_cmu_div_clks[] __initconst = {
	DIV(CLK_DOUT_SHARED0_DIV2, "dout_pll_shared0_div2",
	    "mout_clkcmu_pll_shared0", CLK_CON_DIV_PLL_SHARED0_DIV2, 0, 1),
	DIV(CLK_DOUT_SHARED0_DIV3, "dout_pll_shared0_div3",
	    "mout_clkcmu_pll_shared0", CLK_CON_DIV_PLL_SHARED0_DIV3, 0, 2),
	DIV(CLK_DOUT_SHARED0_DIV4, "dout_pll_shared0_div4",
	    "dout_pll_shared0_div2", CLK_CON_DIV_PLL_SHARED0_DIV4, 0, 1),
	DIV(CLK_DOUT_SHARED1_DIV2, "dout_pll_shared1_div2",
	    "mout_clkcmu_pll_shared1", CLK_CON_DIV_PLL_SHARED1_DIV2, 0, 1),
	DIV(CLK_DOUT_SHARED1_DIV3, "dout_pll_shared1_div3",
	    "mout_clkcmu_pll_shared1", CLK_CON_DIV_PLL_SHARED1_DIV3, 0, 2),
	DIV(CLK_DOUT_SHARED1_DIV4, "dout_pll_shared1_div4",
	    "dout_pll_shared1_div2", CLK_CON_DIV_PLL_SHARED1_DIV4, 0, 1),
	DIV(CLK_DOUT_CMU_BUS, "dout_clkcmu_bus",
	    "mout_clkcmu_bus_bus", CLK_CON_DIV_CLKCMU_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_BUS_DLP, "dout_clkcmu_bus_dlp",
	    "mout_clkcmu_bus_dlp", CLK_CON_DIV_CLKCMU_BUS_DLP, 0, 4),
	DIV(CLK_DOUT_CMU_CORE_MAIN, "dout_clkcmu_core_main",
	    "mout_clkcmu_core_bus", CLK_CON_DIV_CLKCMU_CORE_MAIN, 0, 4),
	DIV(CLK_DOUT_CMU_CORE_DLP, "dout_clkcmu_core_dlp",
	    "mout_clkcmu_core_dlp", CLK_CON_DIV_CLKCMU_CORE_DLP, 0, 4),
	DIV(CLK_DOUT_CMU_CPUCL_SWITCH, "dout_clkcmu_cpucl_switch",
	    "mout_clkcmu_cpucl_switch", CLK_CON_DIV_CLKCMU_CPUCL_SWITCH, 0, 3),
	DIV(CLK_DOUT_CMU_FSYS_BUS, "dout_clkcmu_fsys_bus",
	    "mout_clkcmu_fsys_bus", CLK_CON_DIV_CLKCMU_FSYS_BUS, 0, 4),
	DIV(CLK_DOUT_CMU_FSYS_IP, "dout_clkcmu_fsys_ip",
	    "mout_clkcmu_fsys_ip", CLK_CON_DIV_CLKCMU_FSYS_IP, 0, 9),
	DIV(CLK_DOUT_CMU_FSYS_SCAN0, "dout_clkcmu_fsys_scan0",
	    "mout_clkcmu_fsys_scan0", CLK_CON_DIV_CLKCMU_FSYS_SCAN0, 0, 4),
	DIV(CLK_DOUT_CMU_FSYS_SCAN1, "dout_clkcmu_fsys_scan1",
	    "mout_clkcmu_fsys_scan1", CLK_CON_DIV_CLKCMU_FSYS_SCAN1, 0, 4),
	DIV(CLK_DOUT_CMU_IMEM_ACLK, "dout_clkcmu_imem_aclk",
	    "mout_clkcmu_imem_imem", CLK_CON_DIV_CLKCMU_IMEM_ACLK, 0, 4),
	DIV(CLK_DOUT_CMU_IMEM_JPEG, "dout_clkcmu_imem_jpeg",
	    "mout_clkcmu_imem_jpeg", CLK_CON_DIV_CLKCMU_IMEM_JPEG, 0, 4),
	DIV_F(CLK_DOUT_CMU_CDC_CORE, "dout_clkcmu_cdc_core",
	      "mout_clkcmu_cdc_core", CLK_CON_DIV_CLKCMU_CDC_CORE, 0, 4, CLK_SET_RATE_PARENT, 0),
	DIV_F(CLK_DOUT_CMU_DLP_CORE, "dout_clkcmu_dlp_core",
	      "mout_clkcmu_dlp_core", CLK_CON_DIV_CLKCMU_DLP_CORE, 0, 4, CLK_SET_RATE_PARENT, 0),
	DIV(CLK_DOUT_CMU_GPU_3D, "dout_clkcmu_gpu_3d",
	    "mout_clkcmu_3d", CLK_CON_DIV_CLKCMU_GPU_3D, 0, 3),
	DIV(CLK_DOUT_CMU_GPU_2D, "dout_clkcmu_gpu_2d",
	    "mout_clkcmu_2d", CLK_CON_DIV_CLKCMU_GPU_2D, 0, 4),
	DIV(CLK_DOUT_CMU_MIF_SWITCH, "dout_clkcmu_mif_switch",
	    "mout_clkcmu_mif_switch", CLK_CON_DIV_CLKCMU_MIF_SWITCH, 0, 4),
	DIV(CLK_DOUT_CMU_MIF_BUSP, "dout_clkcmu_mif_busp",
	    "mout_clkcmu_mif_busp", CLK_CON_DIV_CLKCMU_MIF_BUSP, 0, 3),
	DIV(CLK_DOUT_CMU_PERI_DISP, "dout_clkcmu_peri_disp",
	    "mout_clkcmu_peri_disp", CLK_CON_DIV_CLKCMU_PERI_DISP, 0, 4),
	DIV(CLK_DOUT_CMU_PERI_IP, "dout_clkcmu_peri_ip",
	    "mout_clkcmu_peri_ip", CLK_CON_DIV_CLKCMU_PERI_IP, 0, 4),
	DIV(CLK_DOUT_CMU_PERI_AUDIO, "dout_clkcmu_peri_audio",
	    "mout_clkcmu_pll_audio", CLK_CON_DIV_CLKCMU_PERI_AUDIO, 0, 4),
	DIV(CLK_DOUT_CMU_RSP_CORE, "dout_clkcmu_rsp_core",
	    "mout_clkcmu_rsp_core", CLK_CON_DIV_CLKCMU_RSP_CORE, 0, 4),
	DIV_F(CLK_DOUT_CMU_TRFM_CORE, "dout_clkcmu_trfm_core",
	      "mout_clkcmu_trfm_core", CLK_CON_DIV_CLKCMU_TRFM_CORE, 0, 4, CLK_SET_RATE_PARENT, 0),
	DIV(CLK_DOUT_CMU_VCA_ACE, "dout_clkcmu_vca_ace",
	    "mout_clkcmu_vca_ace", CLK_CON_DIV_CLKCMU_VCA_ACE, 0, 4),
	DIV(CLK_DOUT_CMU_VCA_OD, "dout_clkcmu_vca_od",
	    "mout_clkcmu_vca_od", CLK_CON_DIV_CLKCMU_VCA_OD, 0, 4),
	DIV(CLK_DOUT_CMU_VIO_CORE, "dout_clkcmu_vio_core",
	    "mout_clkcmu_vio_core", CLK_CON_DIV_CLKCMU_VIO_CORE, 0, 4),
	DIV(CLK_DOUT_CMU_VIO_AUDIO, "dout_clkcmu_vio_audio",
	    "mout_clkcmu_pll_audio", CLK_CON_DIV_CLKCMU_VIO_AUDIO, 0, 4),
	DIV_F(CLK_DOUT_CMU_VIP0_CORE, "dout_clkcmu_vip0_core",
	      "mout_clkcmu_vip0_core", CLK_CON_DIV_CLKCMU_VIP0_CORE, 0, 4, CLK_SET_RATE_PARENT, 0),
	DIV_F(CLK_DOUT_CMU_VIP1_CORE, "dout_clkcmu_vip1_core",
	      "mout_clkcmu_vip1_core", CLK_CON_DIV_CLKCMU_VIP1_CORE, 0, 4, CLK_SET_RATE_PARENT, 0),
	DIV_F(CLK_DOUT_CMU_VPP_CORE, "dout_clkcmu_vpp_core",
	      "mout_clkcmu_vpp_core", CLK_CON_DIV_CLKCMU_VPP_CORE, 0, 4, CLK_SET_RATE_PARENT, 0),
};

static const struct samsung_cmu_info cmu_cmu_info __initconst = {
	.pll_clks		= cmu_cmu_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(cmu_cmu_pll_clks),
	.fixed_factor_clks	= cmu_fixed_factor_clks,
	.nr_fixed_factor_clks	= ARRAY_SIZE(cmu_fixed_factor_clks),
	.mux_clks		= cmu_cmu_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(cmu_cmu_mux_clks),
	.div_clks		= cmu_cmu_div_clks,
	.nr_div_clks		= ARRAY_SIZE(cmu_cmu_div_clks),
	.nr_clk_ids		= CMU_CMU_NR_CLK,
	.clk_regs		= cmu_cmu_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(cmu_cmu_clk_regs),
};

/* Register Offset definitions for CMU_BUS (0x12c10000) */
#define PLL_CON0_MUX_CLK_BUS_ACLK_USER			0x0100
#define PLL_CON0_MUX_CLK_BUS_DLP_USER			0x0120
#define CLK_CON_DIV_CLK_BUS_PCLK			0x1800

static const unsigned long cmu_bus_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLK_BUS_ACLK_USER,
	PLL_CON0_MUX_CLK_BUS_DLP_USER,
	CLK_CON_DIV_CLK_BUS_PCLK,
};

PNAME(mout_clk_bus_aclk_user_p) = { "fin_pll", "dout_clkcmu_bus" };
PNAME(mout_clk_bus_dlp_user_p) = { "fin_pll", "dout_clkcmu_bus_dlp" };

static const struct samsung_mux_clock cmu_bus_mux_clks[] __initconst = {
	MUX(CLK_MOUT_BUS_ACLK_USER, "mout_clk_bus_aclk_user",
	    mout_clk_bus_aclk_user_p, PLL_CON0_MUX_CLK_BUS_ACLK_USER, 4, 1),
	MUX(CLK_MOUT_BUS_DLP_USER, "mout_clk_bus_dlp_user",
	    mout_clk_bus_dlp_user_p, PLL_CON0_MUX_CLK_BUS_DLP_USER, 4, 1),
};

static const struct samsung_div_clock cmu_bus_div_clks[] __initconst = {
	DIV(CLK_DOUT_BUS_PCLK, "dout_clk_bus_pclk", "mout_clk_bus_aclk_user",
	    CLK_CON_DIV_CLK_BUS_PCLK, 0, 4),
};

static const struct samsung_cmu_info cmu_bus_info __initconst = {
	.mux_clks		= cmu_bus_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(cmu_bus_mux_clks),
	.div_clks		= cmu_bus_div_clks,
	.nr_div_clks		= ARRAY_SIZE(cmu_bus_div_clks),
	.nr_clk_ids		= CMU_BUS_NR_CLK,
	.clk_regs		= cmu_bus_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(cmu_bus_clk_regs),
};

/* Register Offset definitions for CMU_CORE (0x12410000) */
#define PLL_CON0_MUX_CLK_CORE_ACLK_USER			0x0100
#define PLL_CON0_MUX_CLK_CORE_DLP_USER			0x0120
#define CLK_CON_DIV_CLK_CORE_PCLK			0x1800

static const unsigned long cmu_core_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLK_CORE_ACLK_USER,
	PLL_CON0_MUX_CLK_CORE_DLP_USER,
	CLK_CON_DIV_CLK_CORE_PCLK,
};

PNAME(mout_clk_core_aclk_user_p) = { "fin_pll", "dout_clkcmu_core_main" };
PNAME(mout_clk_core_dlp_user_p) = { "fin_pll", "dout_clkcmu_core_dlp" };

static const struct samsung_mux_clock cmu_core_mux_clks[] __initconst = {
	MUX(CLK_MOUT_CORE_ACLK_USER, "mout_clk_core_aclk_user",
	    mout_clk_core_aclk_user_p, PLL_CON0_MUX_CLK_CORE_ACLK_USER, 4, 1),
	MUX(CLK_MOUT_CORE_DLP_USER, "mout_clk_core_dlp_user",
	    mout_clk_core_dlp_user_p, PLL_CON0_MUX_CLK_CORE_DLP_USER, 4, 1),
};

static const struct samsung_div_clock cmu_core_div_clks[] __initconst = {
	DIV(CLK_DOUT_CORE_PCLK, "dout_clk_core_pclk",
	    "mout_clk_core_aclk_user", CLK_CON_DIV_CLK_CORE_PCLK, 0, 4),
};

static const struct samsung_cmu_info cmu_core_info __initconst = {
	.mux_clks		= cmu_core_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(cmu_core_mux_clks),
	.div_clks		= cmu_core_div_clks,
	.nr_div_clks		= ARRAY_SIZE(cmu_core_div_clks),
	.nr_clk_ids		= CMU_CORE_NR_CLK,
	.clk_regs		= cmu_core_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(cmu_core_clk_regs),
};

/* Register Offset definitions for CMU_CPUCL (0x11410000) */
#define PLL_LOCKTIME_PLL_CPUCL				0x0000
#define PLL_CON0_MUX_CLKCMU_CPUCL_SWITCH_USER		0x0120
#define PLL_CON0_PLL_CPUCL				0x0140
#define CLK_CON_MUX_CLK_CPUCL_PLL			0x1000
#define CLK_CON_DIV_CLK_CLUSTER_ACLK			0x1800
#define CLK_CON_DIV_CLK_CLUSTER_CNTCLK			0x1804
#define CLK_CON_DIV_CLK_CLUSTER_PCLKDBG			0x1808
#define CLK_CON_DIV_CLK_CPUCL_CMUREF			0x180c
#define CLK_CON_DIV_CLK_CPUCL_PCLK			0x1814
#define CLK_CON_DIV_CLK_CLUSTER_ATCLK			0x1818
#define CLK_CON_DIV_CLK_CPUCL_DBG			0x181c
#define CLK_CON_DIV_CLK_CPUCL_PCLKDBG			0x1820
#define CLK_CON_GAT_CLK_CLUSTER_CPU			0x2008
#define CLK_CON_GAT_CLK_CPUCL_SHORTSTOP			0x200c
#define CLK_CON_DMYQCH_CON_CSSYS_QCH			0x3008

static const unsigned long cmu_cpucl_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_CPUCL,
	PLL_CON0_MUX_CLKCMU_CPUCL_SWITCH_USER,
	PLL_CON0_PLL_CPUCL,
	CLK_CON_MUX_CLK_CPUCL_PLL,
	CLK_CON_DIV_CLK_CLUSTER_ACLK,
	CLK_CON_DIV_CLK_CLUSTER_CNTCLK,
	CLK_CON_DIV_CLK_CLUSTER_PCLKDBG,
	CLK_CON_DIV_CLK_CPUCL_CMUREF,
	CLK_CON_DIV_CLK_CPUCL_PCLK,
	CLK_CON_DIV_CLK_CLUSTER_ATCLK,
	CLK_CON_DIV_CLK_CPUCL_DBG,
	CLK_CON_DIV_CLK_CPUCL_PCLKDBG,
	CLK_CON_GAT_CLK_CLUSTER_CPU,
	CLK_CON_GAT_CLK_CPUCL_SHORTSTOP,
	CLK_CON_DMYQCH_CON_CSSYS_QCH,
};

static const struct samsung_pll_clock cmu_cpucl_pll_clks[] __initconst = {
	PLL(pll_1017x, CLK_FOUT_CPUCL_PLL, "fout_pll_cpucl", "fin_pll",
	    PLL_LOCKTIME_PLL_CPUCL, PLL_CON0_PLL_CPUCL, NULL),
};

PNAME(mout_clkcmu_cpucl_switch_user_p) = { "fin_pll", "dout_clkcmu_cpucl_switch" };
PNAME(mout_pll_cpucl_p) = { "fin_pll", "fout_pll_cpucl" };
PNAME(mout_clk_cpucl_pll_p) = { "mout_pll_cpucl", "mout_clkcmu_cpucl_switch_user" };

static const struct samsung_mux_clock cmu_cpucl_mux_clks[] __initconst = {
	MUX_F(0, "mout_pll_cpucl", mout_pll_cpucl_p, PLL_CON0_PLL_CPUCL, 4, 1,
	      CLK_SET_RATE_PARENT | CLK_RECALC_NEW_RATES, 0),
	MUX(CLK_MOUT_CPUCL_SWITCH_USER, "mout_clkcmu_cpucl_switch_user",
	    mout_clkcmu_cpucl_switch_user_p, PLL_CON0_MUX_CLKCMU_CPUCL_SWITCH_USER, 4, 1),
	MUX_F(CLK_MOUT_CPUCL_PLL, "mout_clk_cpucl_pll", mout_clk_cpucl_pll_p,
	      CLK_CON_MUX_CLK_CPUCL_PLL, 0, 1, CLK_SET_RATE_PARENT, 0),
};

static const struct samsung_fixed_factor_clock cpucl_ffactor_clks[] __initconst = {
	FFACTOR(CLK_DOUT_CPUCL_CPU, "dout_clk_cpucl_cpu",
		"mout_clk_cpucl_pll", 1, 1, CLK_SET_RATE_PARENT),
};

static const struct samsung_div_clock cmu_cpucl_div_clks[] __initconst = {
	DIV(CLK_DOUT_CPUCL_CLUSTER_ACLK, "dout_clk_cluster_aclk",
	    "dout_clk_cpucl_cpu", CLK_CON_DIV_CLK_CLUSTER_ACLK, 0, 4),
	DIV(CLK_DOUT_CPUCL_CLUSTER_PCLKDBG, "dout_clk_cluster_pclkdbg",
	    "dout_clk_cpucl_cpu", CLK_CON_DIV_CLK_CLUSTER_PCLKDBG, 0, 4),
	DIV(CLK_DOUT_CPUCL_CLUSTER_CNTCLK, "dout_clk_cluster_cntclk",
	    "dout_clk_cpucl_cpu", CLK_CON_DIV_CLK_CLUSTER_CNTCLK, 0, 4),
	DIV(CLK_DOUT_CPUCL_CLUSTER_ATCLK, "dout_clk_cluster_atclk",
	    "dout_clk_cpucl_cpu", CLK_CON_DIV_CLK_CLUSTER_ATCLK, 0, 4),
	DIV(CLK_DOUT_CPUCL_PCLK, "dout_clk_cpucl_pclk",
	    "dout_clk_cpucl_cpu", CLK_CON_DIV_CLK_CPUCL_PCLK, 0, 4),
	DIV(CLK_DOUT_CPUCL_CMUREF, "dout_clk_cpucl_cmuref",
	    "dout_clk_cpucl_cpu", CLK_CON_DIV_CLK_CPUCL_CMUREF, 0, 3),
	DIV(CLK_DOUT_CPUCL_DBG, "dout_clk_cpucl_dbg",
	    "dout_clk_cpucl_cpu", CLK_CON_DIV_CLK_CPUCL_DBG, 0, 4),
	DIV(CLK_DOUT_CPUCL_PCLKDBG, "dout_clk_cpucl_pclkdbg",
	    "dout_clk_cpucl_dbg", CLK_CON_DIV_CLK_CPUCL_PCLKDBG, 0, 4),
};

static const struct samsung_gate_clock cmu_cpucl_gate_clks[] __initconst = {
	GATE(CLK_GOUT_CPUCL_CLUSTER_CPU, "clk_con_gat_clk_cluster_cpu",
	     "clk_con_gat_clk_cpucl_shortstop", CLK_CON_GAT_CLK_CLUSTER_CPU, 21,
	     CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_CPUCL_SHORTSTOP, "clk_con_gat_clk_cpucl_shortstop",
	     "dout_clk_cpucl_cpu", CLK_CON_GAT_CLK_CPUCL_SHORTSTOP, 21,
	     CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_CPUCL_CSSYS_IPCLKPORT_PCLKDBG, "cssys_ipclkport_pclkdbg",
	     "dout_clk_cpucl_pclkdbg", CLK_CON_DMYQCH_CON_CSSYS_QCH, 1,
	     CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_CPUCL_CSSYS_IPCLKPORT_ATCLK, "cssys_ipclkport_atclk",
	     "dout_clk_cpucl_dbg", CLK_CON_DMYQCH_CON_CSSYS_QCH, 1,
	     CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0),
};

static const struct samsung_cmu_info cmu_cpucl_info __initconst = {
	.pll_clks		= cmu_cpucl_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(cmu_cpucl_pll_clks),
	.fixed_factor_clks	= cpucl_ffactor_clks,
	.nr_fixed_factor_clks	= ARRAY_SIZE(cpucl_ffactor_clks),
	.mux_clks		= cmu_cpucl_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(cmu_cpucl_mux_clks),
	.div_clks		= cmu_cpucl_div_clks,
	.nr_div_clks		= ARRAY_SIZE(cmu_cpucl_div_clks),
	.gate_clks              = cmu_cpucl_gate_clks,
	.nr_gate_clks           = ARRAY_SIZE(cmu_cpucl_gate_clks),
	.nr_clk_ids		= CMU_CPUCL_NR_CLK,
	.clk_regs		= cmu_cpucl_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(cmu_cpucl_clk_regs),
};

/* Register Offset definitions for CMU_FSYS (0x16c10000) */
#define PLL_LOCKTIME_PLL_FSYS				0x0004
#define PLL_CON0_MUX_CLK_FSYS_BUS_USER			0x0120
#define PLL_CON0_MUX_CLK_FSYS_MMC_USER			0x0140
#define PLL_CON0_MUX_CLK_FSYS_SCAN0_USER		0x0160
#define PLL_CON0_MUX_CLK_FSYS_SCAN1_USER		0x0180
#define PLL_CON0_PLL_FSYS				0x01c0
#define CLK_CON_DIV_CLK_FSYS_ADC			0x1804
#define CLK_CON_DIV_CLK_FSYS_BUS300			0x1808
#define CLK_CON_DIV_CLK_FSYS_BUS_QSPI			0x180c
#define CLK_CON_DIV_CLK_FSYS_EQOS_25			0x1810
#define CLK_CON_DIV_CLK_FSYS_EQOS_2P5			0x1814
#define CLK_CON_DIV_CLK_FSYS_EQOS_500			0x1818
#define CLK_CON_DIV_CLK_FSYS_EQOS_INT125		0x181c
#define CLK_CON_DIV_CLK_FSYS_MMC_CARD0			0x1820
#define CLK_CON_DIV_CLK_FSYS_MMC_CARD1			0x1824
#define CLK_CON_DIV_CLK_FSYS_OTP_MEM			0x1828
#define CLK_CON_DIV_CLK_FSYS_PCIE_PHY_REFCLK_SYSPLL	0x182c
#define CLK_CON_DIV_CLK_FSYS_QSPI			0x1830
#define CLK_CON_DIV_CLK_FSYS_SCLK_UART			0x1834
#define CLK_CON_DIV_CLK_FSYS_SFMC_NAND			0x1838
#define CLK_CON_DIV_SCAN_CLK_FSYS_125			0x183c
#define CLK_CON_DIV_SCAN_CLK_FSYS_MMC			0x1840
#define CLK_CON_DIV_SCAN_CLK_FSYS_PCIE_PIPE		0x1844
#define CLK_CON_FSYS_I2C0_IPCLKPORT_I_PCLK		0x2044
#define CLK_CON_FSYS_I2C1_IPCLKPORT_I_PCLK		0x2048
#define CLK_CON_FSYS_UART0_IPCLKPORT_I_PCLK		0x204c
#define CLK_CON_FSYS_UART0_IPCLKPORT_I_SCLK_UART	0x2050
#define CLK_CON_MMC0_IPCLKPORT_I_ACLK			0x2070
#define CLK_CON_MMC1_IPCLKPORT_I_ACLK			0x2078
#define CLK_CON_DWC_PCIE_CTL_INST_0_DBI_ACLK_UG		0x208c
#define CLK_CON_DWC_PCIE_CTL_INST_0_MSTR_ACLK_UG	0x2090
#define CLK_CON_DWC_PCIE_CTL_INST_0_SLV_ACLK_UG		0x2094
#define CLK_CON_PWM_IPCLKPORT_I_PCLK_S0			0x20a0
#define CLK_CON_USB20DRD_IPCLKPORT_ACLK_PHYCTRL_20	0x20bc
#define CLK_CON_USB20DRD_IPCLKPORT_BUS_CLK_EARLY	0x20c0
#define CLK_CON_XHB_AHBBR_IPCLKPORT_CLK			0x20c4
#define CLK_CON_XHB_USB_IPCLKPORT_CLK			0x20cc
#define CLK_CON_BUS_P_FSYS_IPCLKPORT_QSPICLK		0x201c
#define CLK_CON_DMYQCH_CON_EQOS_TOP_QCH			0x3008
#define CLK_CON_DMYQCH_CON_MMC0_QCH			0x300c
#define CLK_CON_DMYQCH_CON_MMC1_QCH			0x3010
#define CLK_CON_DMYQCH_CON_PCIE_TOP_QCH			0x3018
#define CLK_CON_DMYQCH_CON_PCIE_TOP_QCH_REF		0x301c
#define CLK_CON_DMYQCH_CON_QSPI_QCH			0x3020
#define CLK_CON_DMYQCH_CON_SFMC_QCH			0x3024

static const unsigned long cmu_fsys_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_FSYS,
	PLL_CON0_MUX_CLK_FSYS_BUS_USER,
	PLL_CON0_MUX_CLK_FSYS_MMC_USER,
	PLL_CON0_MUX_CLK_FSYS_SCAN0_USER,
	PLL_CON0_MUX_CLK_FSYS_SCAN1_USER,
	PLL_CON0_PLL_FSYS,
	CLK_CON_DIV_CLK_FSYS_ADC,
	CLK_CON_DIV_CLK_FSYS_BUS300,
	CLK_CON_DIV_CLK_FSYS_BUS_QSPI,
	CLK_CON_DIV_CLK_FSYS_EQOS_25,
	CLK_CON_DIV_CLK_FSYS_EQOS_2P5,
	CLK_CON_DIV_CLK_FSYS_EQOS_500,
	CLK_CON_DIV_CLK_FSYS_EQOS_INT125,
	CLK_CON_DIV_CLK_FSYS_MMC_CARD0,
	CLK_CON_DIV_CLK_FSYS_MMC_CARD1,
	CLK_CON_DIV_CLK_FSYS_OTP_MEM,
	CLK_CON_DIV_CLK_FSYS_PCIE_PHY_REFCLK_SYSPLL,
	CLK_CON_DIV_CLK_FSYS_QSPI,
	CLK_CON_DIV_CLK_FSYS_SCLK_UART,
	CLK_CON_DIV_CLK_FSYS_SFMC_NAND,
	CLK_CON_DIV_SCAN_CLK_FSYS_125,
	CLK_CON_DIV_SCAN_CLK_FSYS_MMC,
	CLK_CON_DIV_SCAN_CLK_FSYS_PCIE_PIPE,
	CLK_CON_FSYS_I2C0_IPCLKPORT_I_PCLK,
	CLK_CON_FSYS_I2C1_IPCLKPORT_I_PCLK,
	CLK_CON_FSYS_UART0_IPCLKPORT_I_PCLK,
	CLK_CON_FSYS_UART0_IPCLKPORT_I_SCLK_UART,
	CLK_CON_MMC0_IPCLKPORT_I_ACLK,
	CLK_CON_MMC1_IPCLKPORT_I_ACLK,
	CLK_CON_DWC_PCIE_CTL_INST_0_DBI_ACLK_UG,
	CLK_CON_DWC_PCIE_CTL_INST_0_MSTR_ACLK_UG,
	CLK_CON_DWC_PCIE_CTL_INST_0_SLV_ACLK_UG,
	CLK_CON_PWM_IPCLKPORT_I_PCLK_S0,
	CLK_CON_USB20DRD_IPCLKPORT_ACLK_PHYCTRL_20,
	CLK_CON_USB20DRD_IPCLKPORT_BUS_CLK_EARLY,
	CLK_CON_XHB_AHBBR_IPCLKPORT_CLK,
	CLK_CON_XHB_USB_IPCLKPORT_CLK,
	CLK_CON_BUS_P_FSYS_IPCLKPORT_QSPICLK,
	CLK_CON_DMYQCH_CON_EQOS_TOP_QCH,
	CLK_CON_DMYQCH_CON_MMC0_QCH,
	CLK_CON_DMYQCH_CON_MMC1_QCH,
	CLK_CON_DMYQCH_CON_PCIE_TOP_QCH,
	CLK_CON_DMYQCH_CON_PCIE_TOP_QCH_REF,
	CLK_CON_DMYQCH_CON_QSPI_QCH,
	CLK_CON_DMYQCH_CON_SFMC_QCH,
};

static const struct samsung_pll_clock cmu_fsys_pll_clks[] __initconst = {
	PLL(pll_1017x, CLK_FOUT_FSYS_PLL, "fout_pll_fsys", "fin_pll",
	    PLL_LOCKTIME_PLL_FSYS, PLL_CON0_PLL_FSYS, NULL),
};

PNAME(mout_fsys_scan0_user_p) = { "fin_pll", "dout_clkcmu_fsys_scan0" };
PNAME(mout_fsys_scan1_user_p) = { "fin_pll", "dout_clkcmu_fsys_scan1" };
PNAME(mout_fsys_bus_user_p) = { "fin_pll", "dout_clkcmu_fsys_bus" };
PNAME(mout_fsys_mmc_user_p) = { "fin_pll", "dout_clkcmu_fsys_ip" };
PNAME(mout_fsys_pll_fsys_p) = { "fin_pll", "fout_pll_fsys" };

static const struct samsung_mux_clock cmu_fsys_mux_clks[] __initconst = {
	MUX(0, "mout_clk_pll_fsys", mout_fsys_pll_fsys_p, PLL_CON0_PLL_FSYS, 4, 1),
	MUX(CLK_MOUT_FSYS_SCAN0_USER, "mout_fsys_scan0_user",
	    mout_fsys_scan0_user_p, PLL_CON0_MUX_CLK_FSYS_SCAN0_USER, 4, 1),
	MUX(CLK_MOUT_FSYS_SCAN1_USER, "mout_fsys_scan1_user",
	    mout_fsys_scan1_user_p, PLL_CON0_MUX_CLK_FSYS_SCAN1_USER, 4, 1),
	MUX(CLK_MOUT_FSYS_BUS_USER, "mout_fsys_bus_user",
	    mout_fsys_bus_user_p, PLL_CON0_MUX_CLK_FSYS_BUS_USER, 4, 1),
	MUX(CLK_MOUT_FSYS_MMC_USER, "mout_fsys_mmc_user",
	    mout_fsys_mmc_user_p, PLL_CON0_MUX_CLK_FSYS_MMC_USER, 4, 1),
};

static const struct samsung_div_clock cmu_fsys_div_clks[] __initconst = {
	DIV(CLK_DOUT_FSYS_PCIE_PIPE, "dout_fsys_pcie_pipe", "mout_clk_pll_fsys",
	    CLK_CON_DIV_SCAN_CLK_FSYS_PCIE_PIPE, 0, 4),
	DIV(CLK_DOUT_FSYS_ADC, "dout_fsys_adc", "mout_clk_pll_fsys",
	    CLK_CON_DIV_CLK_FSYS_ADC, 0, 7),
	DIV(CLK_DOUT_FSYS_PCIE_PHY_REFCLK_SYSPLL, "dout_fsys_pcie_phy_refclk_syspll",
	    "mout_clk_pll_fsys", CLK_CON_DIV_CLK_FSYS_PCIE_PHY_REFCLK_SYSPLL, 0, 8),
	DIV(CLK_DOUT_FSYS_QSPI, "dout_fsys_qspi", "mout_fsys_mmc_user",
	    CLK_CON_DIV_CLK_FSYS_QSPI, 0, 4),
	DIV(CLK_DOUT_FSYS_EQOS_INT125, "dout_fsys_eqos_int125", "mout_clk_pll_fsys",
	    CLK_CON_DIV_CLK_FSYS_EQOS_INT125, 0, 4),
	DIV(CLK_DOUT_FSYS_OTP_MEM, "dout_fsys_otp_mem", "fin_pll",
	    CLK_CON_DIV_CLK_FSYS_OTP_MEM, 0, 9),
	DIV(CLK_DOUT_FSYS_SCLK_UART, "dout_fsys_sclk_uart", "mout_clk_pll_fsys",
	    CLK_CON_DIV_CLK_FSYS_SCLK_UART, 0, 10),
	DIV(CLK_DOUT_FSYS_SFMC_NAND, "dout_fsys_sfmc_nand", "mout_fsys_mmc_user",
	    CLK_CON_DIV_CLK_FSYS_SFMC_NAND, 0, 4),
	DIV(CLK_DOUT_SCAN_CLK_FSYS_125, "dout_scan_clk_fsys_125", "mout_clk_pll_fsys",
	    CLK_CON_DIV_SCAN_CLK_FSYS_125, 0, 4),
	DIV(CLK_DOUT_FSYS_SCAN_CLK_MMC, "dout_scan_clk_fsys_mmc", "fout_pll_fsys",
	    CLK_CON_DIV_SCAN_CLK_FSYS_MMC, 0, 4),
	DIV(CLK_DOUT_FSYS_EQOS_25, "dout_fsys_eqos_25", "dout_fsys_eqos_int125",
	    CLK_CON_DIV_CLK_FSYS_EQOS_25, 0, 4),
	DIV_F(CLK_DOUT_FSYS_EQOS_2p5, "dout_fsys_eqos_2p5", "dout_fsys_eqos_25",
	      CLK_CON_DIV_CLK_FSYS_EQOS_2P5, 0, 4, CLK_SET_RATE_PARENT, 0),
	DIV(0, "dout_fsys_eqos_500", "mout_clk_pll_fsys",
	    CLK_CON_DIV_CLK_FSYS_EQOS_500, 0, 4),
	DIV(CLK_DOUT_FSYS_BUS300, "dout_fsys_bus300", "mout_fsys_bus_user",
	    CLK_CON_DIV_CLK_FSYS_BUS300, 0, 4),
	DIV(CLK_DOUT_FSYS_BUS_QSPI, "dout_fsys_bus_qspi", "mout_fsys_mmc_user",
	    CLK_CON_DIV_CLK_FSYS_BUS_QSPI, 0, 4),
	DIV(CLK_DOUT_FSYS_MMC_CARD0, "dout_fsys_mmc_card0", "mout_fsys_mmc_user",
	    CLK_CON_DIV_CLK_FSYS_MMC_CARD0, 0, 10),
	DIV(CLK_DOUT_FSYS_MMC_CARD1, "dout_fsys_mmc_card1", "mout_fsys_mmc_user",
	    CLK_CON_DIV_CLK_FSYS_MMC_CARD1, 0, 10),
};

static const struct samsung_gate_clock cmu_fsys_gate_clks[] __initconst = {
	GATE(CLK_GOUT_FSYS_PCIE_PHY_REFCLK_IN, "pcie_sub_ctrl_inst_0_phy_refclk_in",
	     "dout_fsys_pcie_phy_refclk_syspll", CLK_CON_DMYQCH_CON_PCIE_TOP_QCH_REF, 1,
	     CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_FSYS_EQOS_TOP_IPCLKPORT_I_RGMII_TXCLK_2P5,
	     "eqos_top_ipclkport_i_rgmii_txclk_2p5",
	     "dout_fsys_eqos_2p5", CLK_CON_DMYQCH_CON_EQOS_TOP_QCH, 1, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_FSYS_EQOS_TOP_IPCLKPORT_ACLK_I, "eqos_top_ipclkport_aclk_i",
	     "dout_fsys_bus300", CLK_CON_DMYQCH_CON_EQOS_TOP_QCH, 1, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_FSYS_EQOS_TOP_IPCLKPORT_CLK_CSR_I, "eqos_top_ipclkport_clk_csr_i",
	     "dout_fsys_bus300", CLK_CON_DMYQCH_CON_EQOS_TOP_QCH, 1, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_FSYS_PIPE_PAL_INST_0_I_APB_PCLK, "pipe_pal_inst_0_i_apb_pclk",
	     "dout_fsys_bus300", CLK_CON_DMYQCH_CON_PCIE_TOP_QCH, 1, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_FSYS_QSPI_IPCLKPORT_HCLK, "qspi_ipclkport_hclk",
	     "dout_fsys_bus_qspi", CLK_CON_DMYQCH_CON_QSPI_QCH, 1, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_FSYS_QSPI_IPCLKPORT_SSI_CLK, "qspi_ipclkport_ssi_clk",
	     "dout_fsys_qspi", CLK_CON_DMYQCH_CON_QSPI_QCH, 1, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_FSYS_MMC0_IPCLKPORT_SDCLKIN, "mmc0_ipclkport_sdclkin",
	     "dout_fsys_mmc_card0", CLK_CON_DMYQCH_CON_MMC0_QCH, 1, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_FSYS_MMC1_IPCLKPORT_SDCLKIN, "mmc1_ipclkport_sdclkin",
	     "dout_fsys_mmc_card1", CLK_CON_DMYQCH_CON_MMC1_QCH, 1, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_FSYS_SFMC_IPCLKPORT_I_ACLK_NAND, "sfmc_ipclkport_i_aclk_nand",
	     "dout_fsys_sfmc_nand", CLK_CON_DMYQCH_CON_SFMC_QCH, 1, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_FSYS_UART0_SCLK_UART, "uart0_sclk", "dout_fsys_sclk_uart",
	     CLK_CON_FSYS_UART0_IPCLKPORT_I_SCLK_UART, 21,
	     CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_FSYS_DWC_PCIE_CTL_INST_0_MSTR_ACLK_UG, "dwc_pcie_ctl_inst_0_mstr_aclk_ug",
	     "mout_fsys_bus_user", CLK_CON_DWC_PCIE_CTL_INST_0_MSTR_ACLK_UG, 21,
	     CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_FSYS_DWC_PCIE_CTL_INXT_0_SLV_ACLK_UG, "dwc_pcie_ctl_inst_0_slv_aclk_ug",
	     "mout_fsys_bus_user", CLK_CON_DWC_PCIE_CTL_INST_0_SLV_ACLK_UG, 21,
	     CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_FSYS_I2C0_IPCLKPORT_I_PCLK, "fsys_i2c0_ipclkport_i_pclk", "dout_fsys_bus300",
	     CLK_CON_FSYS_I2C0_IPCLKPORT_I_PCLK, 21, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_FSYS_I2C1_IPCLKPORT_I_PCLK, "fsys_i2c1_ipclkport_i_pclk", "dout_fsys_bus300",
	     CLK_CON_FSYS_I2C1_IPCLKPORT_I_PCLK, 21, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_FSYS_UART0_PCLK, "uart0_pclk", "dout_fsys_bus300",
	     CLK_CON_FSYS_UART0_IPCLKPORT_I_PCLK, 21, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_FSYS_MMC0_IPCLKPORT_I_ACLK, "mmc0_ipclkport_i_aclk", "dout_fsys_bus300",
	     CLK_CON_MMC0_IPCLKPORT_I_ACLK, 21, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_FSYS_MMC1_IPCLKPORT_I_ACLK, "mmc1_ipclkport_i_aclk", "dout_fsys_bus300",
	     CLK_CON_MMC1_IPCLKPORT_I_ACLK, 21, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_FSYS_DWC_PCIE_CTL_INST_0_DBI_ACLK_UG, "dwc_pcie_ctl_inst_0_dbi_aclk_ug",
	     "dout_fsys_bus300", CLK_CON_DWC_PCIE_CTL_INST_0_DBI_ACLK_UG, 21,
	     CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_FSYS_PWM_IPCLKPORT_I_PCLK_S0, "pwm_ipclkport_i_pclk_s0", "dout_fsys_bus300",
	     CLK_CON_PWM_IPCLKPORT_I_PCLK_S0, 21, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_FSYS_USB20DRD_IPCLKPORT_ACLK_PHYCTRL_20, "usb20drd_ipclkport_aclk_phyctrl_20",
	     "dout_fsys_bus300", CLK_CON_USB20DRD_IPCLKPORT_ACLK_PHYCTRL_20, 21,
	     CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_FSYS_USB20DRD_IPCLKPORT_BUS_CLK_EARLY, "usb20drd_ipclkport_bus_clk_early",
	     "dout_fsys_bus300", CLK_CON_USB20DRD_IPCLKPORT_BUS_CLK_EARLY, 21,
	     CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_FSYS_XHB_AHBBR_IPCLKPORT_CLK, "xhb_ahbbr_ipclkport_clk", "dout_fsys_bus300",
	     CLK_CON_XHB_AHBBR_IPCLKPORT_CLK, 21, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_FSYS_XHB_USB_IPCLKPORT_CLK, "xhb_usb_ipclkport_clk", "dout_fsys_bus300",
	     CLK_CON_XHB_USB_IPCLKPORT_CLK, 21, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_FSYS_BUS_QSPI, "bus_p_fsys_ipclkport_qspiclk", "dout_fsys_bus_qspi",
	     CLK_CON_BUS_P_FSYS_IPCLKPORT_QSPICLK, 21, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0),
};

static const struct samsung_cmu_info cmu_fsys_info __initconst = {
	.pll_clks		= cmu_fsys_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(cmu_fsys_pll_clks),
	.mux_clks		= cmu_fsys_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(cmu_fsys_mux_clks),
	.div_clks		= cmu_fsys_div_clks,
	.nr_div_clks		= ARRAY_SIZE(cmu_fsys_div_clks),
	.gate_clks              = cmu_fsys_gate_clks,
	.nr_gate_clks           = ARRAY_SIZE(cmu_fsys_gate_clks),
	.nr_clk_ids		= CMU_FSYS_NR_CLK,
	.clk_regs		= cmu_fsys_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(cmu_fsys_clk_regs),
};

/* Register Offset definitions for CMU_IMEM (0x10010000) */
#define PLL_CON0_MUX_CLK_IMEM_ACLK_USER			0x0100
#define PLL_CON0_MUX_CLK_IMEM_JPEG_USER			0x0120
#define CLK_CON_MUX_CLK_IMEM_GIC_CA53			0x1000
#define CLK_CON_MUX_CLK_IMEM_GIC_CA5			0x1008
#define CLK_CON_MCT_IPCLKPORT_PCLK			0x2038
#define CLK_CON_SFRIF_TMU_IMEM_IPCLKPORT_PCLK		0x2044

static const unsigned long cmu_imem_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLK_IMEM_ACLK_USER,
	PLL_CON0_MUX_CLK_IMEM_JPEG_USER,
	CLK_CON_MUX_CLK_IMEM_GIC_CA53,
	CLK_CON_MUX_CLK_IMEM_GIC_CA5,
	CLK_CON_MCT_IPCLKPORT_PCLK,
	CLK_CON_SFRIF_TMU_IMEM_IPCLKPORT_PCLK,
};

PNAME(mout_imem_aclk_user_p) = { "fin_pll", "dout_clkcmu_imem_aclk" };
PNAME(mout_imem_gic_ca53_p) = { "mout_imem_aclk_user", "fin_pll" };
PNAME(mout_imem_gic_ca5_p) = { "mout_imem_aclk_user", "fin_pll" };
PNAME(mout_imem_jpeg_user_p) = { "fin_pll", "dout_clkcmu_imem_jpeg" };

static const struct samsung_mux_clock cmu_imem_mux_clks[] __initconst = {
	MUX(CLK_MOUT_IMEM_ACLK_USER, "mout_imem_aclk_user",
	    mout_imem_aclk_user_p, PLL_CON0_MUX_CLK_IMEM_ACLK_USER, 4, 1),
	MUX(CLK_MOUT_IMEM_GIC_CA53, "mout_imem_gic_ca53",
	    mout_imem_gic_ca53_p, CLK_CON_MUX_CLK_IMEM_GIC_CA53, 0, 1),
	MUX(CLK_MOUT_IMEM_GIC_CA5, "mout_imem_gic_ca5",
	    mout_imem_gic_ca5_p, CLK_CON_MUX_CLK_IMEM_GIC_CA5, 0, 1),
	MUX(CLK_MOUT_IMEM_JPEG_USER, "mout_imem_jpeg_user",
	    mout_imem_jpeg_user_p, PLL_CON0_MUX_CLK_IMEM_JPEG_USER, 4, 1),
};

static const struct samsung_gate_clock cmu_imem_gate_clks[] __initconst = {
	GATE(CLK_GOUT_IMEM_MCT_PCLK, "mct_pclk", "mout_imem_aclk_user",
	     CLK_CON_MCT_IPCLKPORT_PCLK, 21, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_IMEM_PCLK_TMU0_APBIF, "sfrif_tmu_imem_ipclkport_pclk", "mout_imem_aclk_user",
	     CLK_CON_SFRIF_TMU_IMEM_IPCLKPORT_PCLK, 21, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0),
};

static const struct samsung_cmu_info cmu_imem_info __initconst = {
	.mux_clks		= cmu_imem_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(cmu_imem_mux_clks),
	.gate_clks              = cmu_imem_gate_clks,
	.nr_gate_clks           = ARRAY_SIZE(cmu_imem_gate_clks),
	.nr_clk_ids		= CMU_IMEM_NR_CLK,
	.clk_regs		= cmu_imem_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(cmu_imem_clk_regs),
};

static void __init artpec8_clk_cmu_imem_init(struct device_node *np)
{
	samsung_cmu_register_one(np, &cmu_imem_info);
}

CLK_OF_DECLARE(artpec8_clk_cmu_imem, "axis,artpec8-cmu-imem", artpec8_clk_cmu_imem_init);

/* Register Offset definitions for CMU_PERI (0x16410000) */
#define PLL_CON0_MUX_CLK_PERI_AUDIO_USER		0x0100
#define PLL_CON0_MUX_CLK_PERI_DISP_USER			0x0120
#define PLL_CON0_MUX_CLK_PERI_IP_USER			0x0140
#define CLK_CON_MUX_CLK_PERI_I2S0			0x1000
#define CLK_CON_MUX_CLK_PERI_I2S1			0x1004
#define CLK_CON_DIV_CLK_PERI_DSIM			0x1800
#define CLK_CON_DIV_CLK_PERI_I2S0			0x1804
#define CLK_CON_DIV_CLK_PERI_I2S1			0x1808
#define CLK_CON_DIV_CLK_PERI_PCLK			0x180c
#define CLK_CON_DIV_CLK_PERI_SPI			0x1810
#define CLK_CON_DIV_CLK_PERI_UART1			0x1814
#define CLK_CON_DIV_CLK_PERI_UART2			0x1818
#define CLK_CON_APB_ASYNC_DSIM_IPCLKPORT_PCLKS		0x2004
#define CLK_CON_PERI_I2C2_IPCLKPORT_I_PCLK		0x2030
#define CLK_CON_PERI_I2C3_IPCLKPORT_I_PCLK		0x2034
#define CLK_CON_PERI_SPI0_IPCLKPORT_I_PCLK		0x2048
#define CLK_CON_PERI_SPI0_IPCLKPORT_I_SCLK_SPI		0x204c
#define CLK_CON_PERI_UART1_IPCLKPORT_I_PCLK		0x2050
#define CLK_CON_PERI_UART1_IPCLKPORT_I_SCLK_UART	0x2054
#define CLK_CON_PERI_UART2_IPCLKPORT_I_PCLK		0x2058
#define CLK_CON_PERI_UART2_IPCLKPORT_I_SCLK_UART	0x205c
#define CLK_CON_DMYQCH_CON_AUDIO_OUT_QCH		0x3000
#define CLK_CON_DMYQCH_CON_DMA4DSIM_QCH			0x3004
#define CLK_CON_DMYQCH_CON_PERI_I2SSC0_QCH		0x3008
#define CLK_CON_DMYQCH_CON_PERI_I2SSC1_QCH		0x300c

static const unsigned long cmu_peri_clk_regs[] __initconst = {
	PLL_CON0_MUX_CLK_PERI_AUDIO_USER,
	PLL_CON0_MUX_CLK_PERI_DISP_USER,
	PLL_CON0_MUX_CLK_PERI_IP_USER,
	CLK_CON_MUX_CLK_PERI_I2S0,
	CLK_CON_MUX_CLK_PERI_I2S1,
	CLK_CON_DIV_CLK_PERI_DSIM,
	CLK_CON_DIV_CLK_PERI_I2S0,
	CLK_CON_DIV_CLK_PERI_I2S1,
	CLK_CON_DIV_CLK_PERI_PCLK,
	CLK_CON_DIV_CLK_PERI_SPI,
	CLK_CON_DIV_CLK_PERI_UART1,
	CLK_CON_DIV_CLK_PERI_UART2,
	CLK_CON_APB_ASYNC_DSIM_IPCLKPORT_PCLKS,
	CLK_CON_PERI_I2C2_IPCLKPORT_I_PCLK,
	CLK_CON_PERI_I2C3_IPCLKPORT_I_PCLK,
	CLK_CON_PERI_SPI0_IPCLKPORT_I_PCLK,
	CLK_CON_PERI_SPI0_IPCLKPORT_I_SCLK_SPI,
	CLK_CON_PERI_UART1_IPCLKPORT_I_PCLK,
	CLK_CON_PERI_UART1_IPCLKPORT_I_SCLK_UART,
	CLK_CON_PERI_UART2_IPCLKPORT_I_PCLK,
	CLK_CON_PERI_UART2_IPCLKPORT_I_SCLK_UART,
	CLK_CON_DMYQCH_CON_AUDIO_OUT_QCH,
	CLK_CON_DMYQCH_CON_DMA4DSIM_QCH,
	CLK_CON_DMYQCH_CON_PERI_I2SSC0_QCH,
	CLK_CON_DMYQCH_CON_PERI_I2SSC1_QCH,
};

static const struct samsung_fixed_rate_clock peri_fixed_clks[] __initconst = {
	FRATE(0, "clk_peri_audio", NULL, 0, 100000000),
};

PNAME(mout_peri_ip_user_p) = { "fin_pll", "dout_clkcmu_peri_ip" };
PNAME(mout_peri_audio_user_p) = { "fin_pll", "dout_clkcmu_peri_audio" };
PNAME(mout_peri_disp_user_p) = { "fin_pll", "dout_clkcmu_peri_disp" };
PNAME(mout_peri_i2s0_p) = { "dout_peri_i2s0", "clk_peri_audio" };
PNAME(mout_peri_i2s1_p) = { "dout_peri_i2s1", "clk_peri_audio" };

static const struct samsung_mux_clock cmu_peri_mux_clks[] __initconst = {
	MUX(CLK_MOUT_PERI_IP_USER, "mout_peri_ip_user", mout_peri_ip_user_p,
	    PLL_CON0_MUX_CLK_PERI_IP_USER, 4, 1),
	MUX(CLK_MOUT_PERI_AUDIO_USER, "mout_peri_audio_user",
	    mout_peri_audio_user_p, PLL_CON0_MUX_CLK_PERI_AUDIO_USER, 4, 1),
	MUX(CLK_MOUT_PERI_DISP_USER, "mout_peri_disp_user", mout_peri_disp_user_p,
	    PLL_CON0_MUX_CLK_PERI_DISP_USER, 4, 1),
	MUX(CLK_MOUT_PERI_I2S0, "mout_peri_i2s0", mout_peri_i2s0_p,
	    CLK_CON_MUX_CLK_PERI_I2S0, 0, 1),
	MUX(CLK_MOUT_PERI_I2S1, "mout_peri_i2s1", mout_peri_i2s1_p,
	    CLK_CON_MUX_CLK_PERI_I2S1, 0, 1),
};

static const struct samsung_div_clock cmu_peri_div_clks[] __initconst = {
	DIV(CLK_DOUT_PERI_SPI, "dout_peri_spi", "mout_peri_ip_user",
	    CLK_CON_DIV_CLK_PERI_SPI, 0, 10),
	DIV(CLK_DOUT_PERI_UART1, "dout_peri_uart1", "mout_peri_ip_user",
	    CLK_CON_DIV_CLK_PERI_UART1, 0, 10),
	DIV(CLK_DOUT_PERI_UART2, "dout_peri_uart2", "mout_peri_ip_user",
	    CLK_CON_DIV_CLK_PERI_UART2, 0, 10),
	DIV(CLK_DOUT_PERI_PCLK, "dout_peri_pclk", "mout_peri_ip_user",
	    CLK_CON_DIV_CLK_PERI_PCLK, 0, 4),
	DIV(CLK_DOUT_PERI_I2S0, "dout_peri_i2s0", "mout_peri_audio_user",
	    CLK_CON_DIV_CLK_PERI_I2S0, 0, 4),
	DIV(CLK_DOUT_PERI_I2S1, "dout_peri_i2s1", "mout_peri_audio_user",
	    CLK_CON_DIV_CLK_PERI_I2S1, 0, 4),
	DIV(CLK_DOUT_PERI_DSIM, "dout_peri_dsim", "mout_peri_disp_user",
	    CLK_CON_DIV_CLK_PERI_DSIM, 0, 4),
};

static const struct samsung_gate_clock cmu_peri_gate_clks[] __initconst = {
	GATE(CLK_GOUT_PERI_DMA4DSIM_IPCLKPORT_CLK_APB_CLK, "dma4dsim_ipclkport_clk_apb_clk",
	     "dout_peri_pclk", CLK_CON_DMYQCH_CON_DMA4DSIM_QCH, 1, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_I2SSC0_IPCLKPORT_CLK_HST, "i2ssc0_ipclkport_clk_hst", "dout_peri_pclk",
	     CLK_CON_DMYQCH_CON_PERI_I2SSC0_QCH, 1, CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_PERI_I2SSC1_IPCLKPORT_CLK_HST, "i2ssc1_ipclkport_clk_hst", "dout_peri_pclk",
	     CLK_CON_DMYQCH_CON_PERI_I2SSC1_QCH, 1, CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_PERI_AUDIO_OUT_IPCLKPORT_CLK, "audio_out_ipclkport_clk",
	     "mout_peri_audio_user", CLK_CON_DMYQCH_CON_AUDIO_OUT_QCH, 1, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_I2SSC0_IPCLKPORT_CLK, "peri_i2ssc0_ipclkport_clk", "mout_peri_i2s0",
	     CLK_CON_DMYQCH_CON_PERI_I2SSC0_QCH, 1, CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_PERI_I2SSC1_IPCLKPORT_CLK, "peri_i2ssc1_ipclkport_clk", "mout_peri_i2s1",
	     CLK_CON_DMYQCH_CON_PERI_I2SSC1_QCH, 1, CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED, 0),
	GATE(CLK_GOUT_PERI_DMA4DSIM_IPCLKPORT_CLK_AXI_CLK, "dma4dsim_ipclkport_clk_axi_clk",
	     "mout_peri_disp_user", CLK_CON_DMYQCH_CON_DMA4DSIM_QCH, 1, CLK_SET_RATE_PARENT, 0),
	GATE(CLK_GOUT_PERI_SPI0_SCLK_SPI, "peri_spi0_ipclkport_i_sclk_spi", "dout_peri_spi",
	     CLK_CON_PERI_SPI0_IPCLKPORT_I_SCLK_SPI, 21, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_PERI_UART1_SCLK_UART, "uart1_sclk", "dout_peri_uart1",
	     CLK_CON_PERI_UART1_IPCLKPORT_I_SCLK_UART, 21,
	     CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_PERI_UART2_SCLK_UART, "uart2_sclk", "dout_peri_uart2",
	     CLK_CON_PERI_UART2_IPCLKPORT_I_SCLK_UART, 21,
	     CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_PERI_APB_ASYNC_DSIM_IPCLKPORT_PCLKS, "apb_async_dsim_ipclkport_pclks",
	     "dout_peri_pclk", CLK_CON_APB_ASYNC_DSIM_IPCLKPORT_PCLKS, 21,
	     CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_PERI_I2C2_IPCLKPORT_I_PCLK, "peri_i2c2_ipclkport_i_pclk", "dout_peri_pclk",
	     CLK_CON_PERI_I2C2_IPCLKPORT_I_PCLK, 21, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_PERI_I2C3_IPCLKPORT_I_PCLK, "peri_i2c3_ipclkport_i_pclk", "dout_peri_pclk",
	     CLK_CON_PERI_I2C3_IPCLKPORT_I_PCLK, 21, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_PERI_SPI0_PCLK, "peri_spi0_ipclkport_i_pclk", "dout_peri_pclk",
	     CLK_CON_PERI_SPI0_IPCLKPORT_I_PCLK, 21, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_PERI_UART1_PCLK, "uart1_pclk", "dout_peri_pclk",
	     CLK_CON_PERI_UART1_IPCLKPORT_I_PCLK, 21, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0),
	GATE(CLK_GOUT_PERI_UART2_PCLK, "uart2_pclk", "dout_peri_pclk",
	     CLK_CON_PERI_UART2_IPCLKPORT_I_PCLK, 21, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL, 0),
};

static const struct samsung_cmu_info cmu_peri_info __initconst = {
	.mux_clks		= cmu_peri_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(cmu_peri_mux_clks),
	.div_clks		= cmu_peri_div_clks,
	.nr_div_clks		= ARRAY_SIZE(cmu_peri_div_clks),
	.gate_clks              = cmu_peri_gate_clks,
	.nr_gate_clks           = ARRAY_SIZE(cmu_peri_gate_clks),
	.fixed_clks		= peri_fixed_clks,
	.nr_fixed_clks		= ARRAY_SIZE(peri_fixed_clks),
	.nr_clk_ids		= CMU_PERI_NR_CLK,
	.clk_regs		= cmu_peri_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(cmu_peri_clk_regs),
};

/**
 * artpec8_cmu_probe - Probe function for ARTPEC platform clocks
 * @pdev: Pointer to platform device
 *
 * Configure clock hierarchy for clock domains of ARTPEC platform
 */
static int __init artpec8_cmu_probe(struct platform_device *pdev)
{
	const struct samsung_cmu_info *info;
	struct device *dev = &pdev->dev;

	info = of_device_get_match_data(dev);
	exynos_arm64_register_cmu(dev, dev->of_node, info);

	return 0;
}

static const struct of_device_id artpec8_cmu_of_match[] = {
	{
		.compatible = "axis,artpec8-cmu-cmu",
		.data = &cmu_cmu_info,
	}, {
		.compatible = "axis,artpec8-cmu-bus",
		.data = &cmu_bus_info,
	}, {
		.compatible = "axis,artpec8-cmu-core",
		.data = &cmu_core_info,
	}, {
		.compatible = "axis,artpec8-cmu-cpucl",
		.data = &cmu_cpucl_info,
	}, {
		.compatible = "axis,artpec8-cmu-fsys",
		.data = &cmu_fsys_info,
	}, {
		.compatible = "axis,artpec8-cmu-peri",
		.data = &cmu_peri_info,
	}, {
	},
};

static struct platform_driver artpec8_cmu_driver __refdata = {
	.driver	= {
		.name = "artpec8-cmu",
		.of_match_table = artpec8_cmu_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = artpec8_cmu_probe,
};

static int __init artpec8_cmu_init(void)
{
	return platform_driver_register(&artpec8_cmu_driver);
}
core_initcall(artpec8_cmu_init);
