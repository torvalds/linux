// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2022 Samsung Electronics Co., Ltd.
 *             https://www.samsung.com
 * Copyright (c) 2017-2022 Tesla, Inc.
 *             https://www.tesla.com
 *
 * Common Clock Framework support for FSD SoC.
 */

#include <linux/clk-provider.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <dt-bindings/clock/fsd-clk.h>

#include "clk.h"
#include "clk-exynos-arm64.h"

/* Register Offset definitions for CMU_CMU (0x11c10000) */
#define PLL_LOCKTIME_PLL_SHARED0			0x0
#define PLL_LOCKTIME_PLL_SHARED1			0x4
#define PLL_LOCKTIME_PLL_SHARED2			0x8
#define PLL_LOCKTIME_PLL_SHARED3			0xc
#define PLL_CON0_PLL_SHARED0				0x100
#define PLL_CON0_PLL_SHARED1				0x120
#define PLL_CON0_PLL_SHARED2				0x140
#define PLL_CON0_PLL_SHARED3				0x160
#define MUX_CMU_CIS0_CLKMUX				0x1000
#define MUX_CMU_CIS1_CLKMUX				0x1004
#define MUX_CMU_CIS2_CLKMUX				0x1008
#define MUX_CMU_CPUCL_SWITCHMUX				0x100c
#define MUX_CMU_FSYS1_ACLK_MUX				0x1014
#define MUX_PLL_SHARED0_MUX				0x1020
#define MUX_PLL_SHARED1_MUX				0x1024
#define DIV_CMU_CIS0_CLK				0x1800
#define DIV_CMU_CIS1_CLK				0x1804
#define DIV_CMU_CIS2_CLK				0x1808
#define DIV_CMU_CMU_ACLK				0x180c
#define DIV_CMU_CPUCL_SWITCH				0x1810
#define DIV_CMU_FSYS0_SHARED0DIV4			0x181c
#define DIV_CMU_FSYS0_SHARED1DIV3			0x1820
#define DIV_CMU_FSYS0_SHARED1DIV4			0x1824
#define DIV_CMU_FSYS1_SHARED0DIV4			0x1828
#define DIV_CMU_FSYS1_SHARED0DIV8			0x182c
#define DIV_CMU_IMEM_ACLK				0x1834
#define DIV_CMU_IMEM_DMACLK				0x1838
#define DIV_CMU_IMEM_TCUCLK				0x183c
#define DIV_CMU_PERIC_SHARED0DIV20			0x1844
#define DIV_CMU_PERIC_SHARED0DIV3_TBUCLK		0x1848
#define DIV_CMU_PERIC_SHARED1DIV36			0x184c
#define DIV_CMU_PERIC_SHARED1DIV4_DMACLK		0x1850
#define DIV_PLL_SHARED0_DIV2				0x1858
#define DIV_PLL_SHARED0_DIV3				0x185c
#define DIV_PLL_SHARED0_DIV4				0x1860
#define DIV_PLL_SHARED0_DIV6				0x1864
#define DIV_PLL_SHARED1_DIV3				0x1868
#define DIV_PLL_SHARED1_DIV36				0x186c
#define DIV_PLL_SHARED1_DIV4				0x1870
#define DIV_PLL_SHARED1_DIV9				0x1874
#define GAT_CMU_CIS0_CLKGATE				0x2000
#define GAT_CMU_CIS1_CLKGATE				0x2004
#define GAT_CMU_CIS2_CLKGATE				0x2008
#define GAT_CMU_CPUCL_SWITCH_GATE			0x200c
#define GAT_CMU_FSYS0_SHARED0DIV4_GATE			0x2018
#define GAT_CMU_FSYS0_SHARED1DIV4_CLK			0x201c
#define GAT_CMU_FSYS0_SHARED1DIV4_GATE			0x2020
#define GAT_CMU_FSYS1_SHARED0DIV4_GATE			0x2024
#define GAT_CMU_FSYS1_SHARED1DIV4_GATE			0x2028
#define GAT_CMU_IMEM_ACLK_GATE				0x2030
#define GAT_CMU_IMEM_DMACLK_GATE			0x2034
#define GAT_CMU_IMEM_TCUCLK_GATE			0x2038
#define GAT_CMU_PERIC_SHARED0DIVE3_TBUCLK_GATE		0x2040
#define GAT_CMU_PERIC_SHARED0DIVE4_GATE			0x2044
#define GAT_CMU_PERIC_SHARED1DIV4_DMACLK_GATE		0x2048
#define GAT_CMU_PERIC_SHARED1DIVE4_GATE			0x204c
#define GAT_CMU_CMU_CMU_IPCLKPORT_PCLK			0x2054
#define GAT_CMU_AXI2APB_CMU_IPCLKPORT_ACLK		0x2058
#define GAT_CMU_NS_BRDG_CMU_IPCLKPORT_CLK__PSOC_CMU__CLK_CMU	0x205c
#define GAT_CMU_SYSREG_CMU_IPCLKPORT_PCLK		0x2060

/* NOTE: Must be equal to the last clock ID increased by one */
#define CLKS_NR_CMU		(GAT_CMU_FSYS0_SHARED0DIV4 + 1)
#define CLKS_NR_PERIC		(PERIC_DOUT_RGMII_CLK + 1)
#define CLKS_NR_FSYS0		(FSYS0_DOUT_FSYS0_PERIBUS_GRP + 1)
#define CLKS_NR_FSYS1		(PCIE_LINK1_IPCLKPORT_SLV_ACLK + 1)
#define CLKS_NR_IMEM		(IMEM_TMU_GT_IPCLKPORT_I_CLK_TS + 1)
#define CLKS_NR_MFC		(MFC_MFC_IPCLKPORT_ACLK + 1)
#define CLKS_NR_CAM_CSI		(CAM_CSI2_3_IPCLKPORT_I_ACLK + 1)

static const unsigned long cmu_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_SHARED0,
	PLL_LOCKTIME_PLL_SHARED1,
	PLL_LOCKTIME_PLL_SHARED2,
	PLL_LOCKTIME_PLL_SHARED3,
	PLL_CON0_PLL_SHARED0,
	PLL_CON0_PLL_SHARED1,
	PLL_CON0_PLL_SHARED2,
	PLL_CON0_PLL_SHARED3,
	MUX_CMU_CIS0_CLKMUX,
	MUX_CMU_CIS1_CLKMUX,
	MUX_CMU_CIS2_CLKMUX,
	MUX_CMU_CPUCL_SWITCHMUX,
	MUX_CMU_FSYS1_ACLK_MUX,
	MUX_PLL_SHARED0_MUX,
	MUX_PLL_SHARED1_MUX,
	DIV_CMU_CIS0_CLK,
	DIV_CMU_CIS1_CLK,
	DIV_CMU_CIS2_CLK,
	DIV_CMU_CMU_ACLK,
	DIV_CMU_CPUCL_SWITCH,
	DIV_CMU_FSYS0_SHARED0DIV4,
	DIV_CMU_FSYS0_SHARED1DIV3,
	DIV_CMU_FSYS0_SHARED1DIV4,
	DIV_CMU_FSYS1_SHARED0DIV4,
	DIV_CMU_FSYS1_SHARED0DIV8,
	DIV_CMU_IMEM_ACLK,
	DIV_CMU_IMEM_DMACLK,
	DIV_CMU_IMEM_TCUCLK,
	DIV_CMU_PERIC_SHARED0DIV20,
	DIV_CMU_PERIC_SHARED0DIV3_TBUCLK,
	DIV_CMU_PERIC_SHARED1DIV36,
	DIV_CMU_PERIC_SHARED1DIV4_DMACLK,
	DIV_PLL_SHARED0_DIV2,
	DIV_PLL_SHARED0_DIV3,
	DIV_PLL_SHARED0_DIV4,
	DIV_PLL_SHARED0_DIV6,
	DIV_PLL_SHARED1_DIV3,
	DIV_PLL_SHARED1_DIV36,
	DIV_PLL_SHARED1_DIV4,
	DIV_PLL_SHARED1_DIV9,
	GAT_CMU_CIS0_CLKGATE,
	GAT_CMU_CIS1_CLKGATE,
	GAT_CMU_CIS2_CLKGATE,
	GAT_CMU_CPUCL_SWITCH_GATE,
	GAT_CMU_FSYS0_SHARED0DIV4_GATE,
	GAT_CMU_FSYS0_SHARED1DIV4_CLK,
	GAT_CMU_FSYS0_SHARED1DIV4_GATE,
	GAT_CMU_FSYS1_SHARED0DIV4_GATE,
	GAT_CMU_FSYS1_SHARED1DIV4_GATE,
	GAT_CMU_IMEM_ACLK_GATE,
	GAT_CMU_IMEM_DMACLK_GATE,
	GAT_CMU_IMEM_TCUCLK_GATE,
	GAT_CMU_PERIC_SHARED0DIVE3_TBUCLK_GATE,
	GAT_CMU_PERIC_SHARED0DIVE4_GATE,
	GAT_CMU_PERIC_SHARED1DIV4_DMACLK_GATE,
	GAT_CMU_PERIC_SHARED1DIVE4_GATE,
	GAT_CMU_CMU_CMU_IPCLKPORT_PCLK,
	GAT_CMU_AXI2APB_CMU_IPCLKPORT_ACLK,
	GAT_CMU_NS_BRDG_CMU_IPCLKPORT_CLK__PSOC_CMU__CLK_CMU,
	GAT_CMU_SYSREG_CMU_IPCLKPORT_PCLK,
};

static const struct samsung_pll_rate_table pll_shared0_rate_table[] __initconst = {
	PLL_35XX_RATE(24 * MHZ, 2000000000U, 250, 3, 0),
};

static const struct samsung_pll_rate_table pll_shared1_rate_table[] __initconst = {
	PLL_35XX_RATE(24 * MHZ, 2400000000U, 200, 2, 0),
};

static const struct samsung_pll_rate_table pll_shared2_rate_table[] __initconst = {
	PLL_35XX_RATE(24 * MHZ, 2400000000U, 200, 2, 0),
};

static const struct samsung_pll_rate_table pll_shared3_rate_table[] __initconst = {
	PLL_35XX_RATE(24 * MHZ, 1800000000U, 150, 2, 0),
};

static const struct samsung_pll_clock cmu_pll_clks[] __initconst = {
	PLL(pll_142xx, 0, "fout_pll_shared0", "fin_pll", PLL_LOCKTIME_PLL_SHARED0,
	    PLL_CON0_PLL_SHARED0, pll_shared0_rate_table),
	PLL(pll_142xx, 0, "fout_pll_shared1", "fin_pll", PLL_LOCKTIME_PLL_SHARED1,
	    PLL_CON0_PLL_SHARED1, pll_shared1_rate_table),
	PLL(pll_142xx, 0, "fout_pll_shared2", "fin_pll", PLL_LOCKTIME_PLL_SHARED2,
	    PLL_CON0_PLL_SHARED2, pll_shared2_rate_table),
	PLL(pll_142xx, 0, "fout_pll_shared3", "fin_pll", PLL_LOCKTIME_PLL_SHARED3,
	    PLL_CON0_PLL_SHARED3, pll_shared3_rate_table),
};

/* List of parent clocks for Muxes in CMU_CMU */
PNAME(mout_cmu_shared0_pll_p) = { "fin_pll", "fout_pll_shared0" };
PNAME(mout_cmu_shared1_pll_p) = { "fin_pll", "fout_pll_shared1" };
PNAME(mout_cmu_shared2_pll_p) = { "fin_pll", "fout_pll_shared2" };
PNAME(mout_cmu_shared3_pll_p) = { "fin_pll", "fout_pll_shared3" };
PNAME(mout_cmu_cis0_clkmux_p) = { "fin_pll", "dout_cmu_pll_shared0_div4" };
PNAME(mout_cmu_cis1_clkmux_p) = { "fin_pll", "dout_cmu_pll_shared0_div4" };
PNAME(mout_cmu_cis2_clkmux_p) = { "fin_pll", "dout_cmu_pll_shared0_div4" };
PNAME(mout_cmu_cpucl_switchmux_p) = { "mout_cmu_pll_shared2", "mout_cmu_pll_shared0_mux" };
PNAME(mout_cmu_fsys1_aclk_mux_p) = { "dout_cmu_pll_shared0_div4", "fin_pll" };
PNAME(mout_cmu_pll_shared0_mux_p) = { "fin_pll", "mout_cmu_pll_shared0" };
PNAME(mout_cmu_pll_shared1_mux_p) = { "fin_pll", "mout_cmu_pll_shared1" };

static const struct samsung_mux_clock cmu_mux_clks[] __initconst = {
	MUX(0, "mout_cmu_pll_shared0", mout_cmu_shared0_pll_p, PLL_CON0_PLL_SHARED0, 4, 1),
	MUX(0, "mout_cmu_pll_shared1", mout_cmu_shared1_pll_p, PLL_CON0_PLL_SHARED1, 4, 1),
	MUX(0, "mout_cmu_pll_shared2", mout_cmu_shared2_pll_p, PLL_CON0_PLL_SHARED2, 4, 1),
	MUX(0, "mout_cmu_pll_shared3", mout_cmu_shared3_pll_p, PLL_CON0_PLL_SHARED3, 4, 1),
	MUX(0, "mout_cmu_cis0_clkmux", mout_cmu_cis0_clkmux_p, MUX_CMU_CIS0_CLKMUX, 0, 1),
	MUX(0, "mout_cmu_cis1_clkmux", mout_cmu_cis1_clkmux_p, MUX_CMU_CIS1_CLKMUX, 0, 1),
	MUX(0, "mout_cmu_cis2_clkmux", mout_cmu_cis2_clkmux_p, MUX_CMU_CIS2_CLKMUX, 0, 1),
	MUX(0, "mout_cmu_cpucl_switchmux", mout_cmu_cpucl_switchmux_p,
	    MUX_CMU_CPUCL_SWITCHMUX, 0, 1),
	MUX(0, "mout_cmu_fsys1_aclk_mux", mout_cmu_fsys1_aclk_mux_p, MUX_CMU_FSYS1_ACLK_MUX, 0, 1),
	MUX(0, "mout_cmu_pll_shared0_mux", mout_cmu_pll_shared0_mux_p, MUX_PLL_SHARED0_MUX, 0, 1),
	MUX(0, "mout_cmu_pll_shared1_mux", mout_cmu_pll_shared1_mux_p, MUX_PLL_SHARED1_MUX, 0, 1),
};

static const struct samsung_div_clock cmu_div_clks[] __initconst = {
	DIV(0, "dout_cmu_cis0_clk", "cmu_cis0_clkgate", DIV_CMU_CIS0_CLK, 0, 4),
	DIV(0, "dout_cmu_cis1_clk", "cmu_cis1_clkgate", DIV_CMU_CIS1_CLK, 0, 4),
	DIV(0, "dout_cmu_cis2_clk", "cmu_cis2_clkgate", DIV_CMU_CIS2_CLK, 0, 4),
	DIV(0, "dout_cmu_cmu_aclk", "dout_cmu_pll_shared1_div9", DIV_CMU_CMU_ACLK, 0, 4),
	DIV(0, "dout_cmu_cpucl_switch", "cmu_cpucl_switch_gate", DIV_CMU_CPUCL_SWITCH, 0, 4),
	DIV(DOUT_CMU_FSYS0_SHARED0DIV4, "dout_cmu_fsys0_shared0div4", "cmu_fsys0_shared0div4_gate",
	    DIV_CMU_FSYS0_SHARED0DIV4, 0, 4),
	DIV(0, "dout_cmu_fsys0_shared1div3", "cmu_fsys0_shared1div4_clk",
	    DIV_CMU_FSYS0_SHARED1DIV3, 0, 4),
	DIV(DOUT_CMU_FSYS0_SHARED1DIV4, "dout_cmu_fsys0_shared1div4", "cmu_fsys0_shared1div4_gate",
	    DIV_CMU_FSYS0_SHARED1DIV4, 0, 4),
	DIV(DOUT_CMU_FSYS1_SHARED0DIV4, "dout_cmu_fsys1_shared0div4", "cmu_fsys1_shared0div4_gate",
	    DIV_CMU_FSYS1_SHARED0DIV4, 0, 4),
	DIV(DOUT_CMU_FSYS1_SHARED0DIV8, "dout_cmu_fsys1_shared0div8", "cmu_fsys1_shared1div4_gate",
	    DIV_CMU_FSYS1_SHARED0DIV8, 0, 4),
	DIV(DOUT_CMU_IMEM_ACLK, "dout_cmu_imem_aclk", "cmu_imem_aclk_gate",
	    DIV_CMU_IMEM_ACLK, 0, 4),
	DIV(DOUT_CMU_IMEM_DMACLK, "dout_cmu_imem_dmaclk", "cmu_imem_dmaclk_gate",
	    DIV_CMU_IMEM_DMACLK, 0, 4),
	DIV(DOUT_CMU_IMEM_TCUCLK, "dout_cmu_imem_tcuclk", "cmu_imem_tcuclk_gate",
	    DIV_CMU_IMEM_TCUCLK, 0, 4),
	DIV(DOUT_CMU_PERIC_SHARED0DIV20, "dout_cmu_peric_shared0div20",
	    "cmu_peric_shared0dive4_gate", DIV_CMU_PERIC_SHARED0DIV20, 0, 4),
	DIV(DOUT_CMU_PERIC_SHARED0DIV3_TBUCLK, "dout_cmu_peric_shared0div3_tbuclk",
	    "cmu_peric_shared0dive3_tbuclk_gate", DIV_CMU_PERIC_SHARED0DIV3_TBUCLK, 0, 4),
	DIV(DOUT_CMU_PERIC_SHARED1DIV36, "dout_cmu_peric_shared1div36",
	    "cmu_peric_shared1dive4_gate", DIV_CMU_PERIC_SHARED1DIV36, 0, 4),
	DIV(DOUT_CMU_PERIC_SHARED1DIV4_DMACLK, "dout_cmu_peric_shared1div4_dmaclk",
	    "cmu_peric_shared1div4_dmaclk_gate", DIV_CMU_PERIC_SHARED1DIV4_DMACLK, 0, 4),
	DIV(0, "dout_cmu_pll_shared0_div2", "mout_cmu_pll_shared0_mux",
	    DIV_PLL_SHARED0_DIV2, 0, 4),
	DIV(0, "dout_cmu_pll_shared0_div3", "mout_cmu_pll_shared0_mux",
	    DIV_PLL_SHARED0_DIV3, 0, 4),
	DIV(DOUT_CMU_PLL_SHARED0_DIV4, "dout_cmu_pll_shared0_div4", "dout_cmu_pll_shared0_div2",
	    DIV_PLL_SHARED0_DIV4, 0, 4),
	DIV(DOUT_CMU_PLL_SHARED0_DIV6, "dout_cmu_pll_shared0_div6", "dout_cmu_pll_shared0_div3",
	    DIV_PLL_SHARED0_DIV6, 0, 4),
	DIV(0, "dout_cmu_pll_shared1_div3", "mout_cmu_pll_shared1_mux",
	    DIV_PLL_SHARED1_DIV3, 0, 4),
	DIV(0, "dout_cmu_pll_shared1_div36", "dout_cmu_pll_shared1_div9",
	    DIV_PLL_SHARED1_DIV36, 0, 4),
	DIV(0, "dout_cmu_pll_shared1_div4", "mout_cmu_pll_shared1_mux",
	    DIV_PLL_SHARED1_DIV4, 0, 4),
	DIV(0, "dout_cmu_pll_shared1_div9", "dout_cmu_pll_shared1_div3",
	    DIV_PLL_SHARED1_DIV9, 0, 4),
};

static const struct samsung_gate_clock cmu_gate_clks[] __initconst = {
	GATE(0, "cmu_cis0_clkgate", "mout_cmu_cis0_clkmux", GAT_CMU_CIS0_CLKGATE, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(0, "cmu_cis1_clkgate", "mout_cmu_cis1_clkmux", GAT_CMU_CIS1_CLKGATE, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(0, "cmu_cis2_clkgate", "mout_cmu_cis2_clkmux", GAT_CMU_CIS2_CLKGATE, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(CMU_CPUCL_SWITCH_GATE, "cmu_cpucl_switch_gate", "mout_cmu_cpucl_switchmux",
	     GAT_CMU_CPUCL_SWITCH_GATE, 21, CLK_IGNORE_UNUSED, 0),
	GATE(GAT_CMU_FSYS0_SHARED0DIV4, "cmu_fsys0_shared0div4_gate", "dout_cmu_pll_shared0_div4",
	     GAT_CMU_FSYS0_SHARED0DIV4_GATE, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "cmu_fsys0_shared1div4_clk", "dout_cmu_pll_shared1_div3",
	     GAT_CMU_FSYS0_SHARED1DIV4_CLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "cmu_fsys0_shared1div4_gate", "dout_cmu_pll_shared1_div4",
	     GAT_CMU_FSYS0_SHARED1DIV4_GATE, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "cmu_fsys1_shared0div4_gate", "mout_cmu_fsys1_aclk_mux",
	     GAT_CMU_FSYS1_SHARED0DIV4_GATE, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "cmu_fsys1_shared1div4_gate", "dout_cmu_fsys1_shared0div4",
	     GAT_CMU_FSYS1_SHARED1DIV4_GATE, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "cmu_imem_aclk_gate", "dout_cmu_pll_shared1_div9", GAT_CMU_IMEM_ACLK_GATE, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(0, "cmu_imem_dmaclk_gate", "mout_cmu_pll_shared1_mux", GAT_CMU_IMEM_DMACLK_GATE, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(0, "cmu_imem_tcuclk_gate", "dout_cmu_pll_shared0_div3", GAT_CMU_IMEM_TCUCLK_GATE, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(0, "cmu_peric_shared0dive3_tbuclk_gate", "dout_cmu_pll_shared0_div3",
	     GAT_CMU_PERIC_SHARED0DIVE3_TBUCLK_GATE, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "cmu_peric_shared0dive4_gate", "dout_cmu_pll_shared0_div4",
	     GAT_CMU_PERIC_SHARED0DIVE4_GATE, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "cmu_peric_shared1div4_dmaclk_gate", "dout_cmu_pll_shared1_div4",
	     GAT_CMU_PERIC_SHARED1DIV4_DMACLK_GATE, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "cmu_peric_shared1dive4_gate", "dout_cmu_pll_shared1_div36",
	     GAT_CMU_PERIC_SHARED1DIVE4_GATE, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "cmu_uid_cmu_cmu_cmu_ipclkport_pclk", "dout_cmu_cmu_aclk",
	     GAT_CMU_CMU_CMU_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "cmu_uid_axi2apb_cmu_ipclkport_aclk", "dout_cmu_cmu_aclk",
	     GAT_CMU_AXI2APB_CMU_IPCLKPORT_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "cmu_uid_ns_brdg_cmu_ipclkport_clk__psoc_cmu__clk_cmu", "dout_cmu_cmu_aclk",
	     GAT_CMU_NS_BRDG_CMU_IPCLKPORT_CLK__PSOC_CMU__CLK_CMU, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "cmu_uid_sysreg_cmu_ipclkport_pclk", "dout_cmu_cmu_aclk",
	     GAT_CMU_SYSREG_CMU_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
};

static const struct samsung_cmu_info cmu_cmu_info __initconst = {
	.pll_clks		= cmu_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(cmu_pll_clks),
	.mux_clks		= cmu_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(cmu_mux_clks),
	.div_clks		= cmu_div_clks,
	.nr_div_clks		= ARRAY_SIZE(cmu_div_clks),
	.gate_clks		= cmu_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(cmu_gate_clks),
	.nr_clk_ids		= CLKS_NR_CMU,
	.clk_regs		= cmu_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(cmu_clk_regs),
};

static void __init fsd_clk_cmu_init(struct device_node *np)
{
	samsung_cmu_register_one(np, &cmu_cmu_info);
}

CLK_OF_DECLARE(fsd_clk_cmu, "tesla,fsd-clock-cmu", fsd_clk_cmu_init);

/* Register Offset definitions for CMU_PERIC (0x14010000) */
#define PLL_CON0_PERIC_DMACLK_MUX		0x100
#define PLL_CON0_PERIC_EQOS_BUSCLK_MUX		0x120
#define PLL_CON0_PERIC_PCLK_MUX			0x140
#define PLL_CON0_PERIC_TBUCLK_MUX		0x160
#define PLL_CON0_SPI_CLK			0x180
#define PLL_CON0_SPI_PCLK			0x1a0
#define PLL_CON0_UART_CLK			0x1c0
#define PLL_CON0_UART_PCLK			0x1e0
#define MUX_PERIC_EQOS_PHYRXCLK			0x1000
#define DIV_EQOS_BUSCLK				0x1800
#define DIV_PERIC_MCAN_CLK			0x1804
#define DIV_RGMII_CLK				0x1808
#define DIV_RII_CLK				0x180c
#define DIV_RMII_CLK				0x1810
#define DIV_SPI_CLK				0x1814
#define DIV_UART_CLK				0x1818
#define GAT_EQOS_TOP_IPCLKPORT_CLK_PTP_REF_I	0x2000
#define GAT_GPIO_PERIC_IPCLKPORT_OSCCLK		0x2004
#define GAT_PERIC_ADC0_IPCLKPORT_I_OSCCLK	0x2008
#define GAT_PERIC_CMU_PERIC_IPCLKPORT_PCLK	0x200c
#define GAT_PERIC_PWM0_IPCLKPORT_I_OSCCLK	0x2010
#define GAT_PERIC_PWM1_IPCLKPORT_I_OSCCLK	0x2014
#define GAT_ASYNC_APB_DMA0_IPCLKPORT_PCLKM	0x2018
#define GAT_ASYNC_APB_DMA0_IPCLKPORT_PCLKS	0x201c
#define GAT_ASYNC_APB_DMA1_IPCLKPORT_PCLKM	0x2020
#define GAT_ASYNC_APB_DMA1_IPCLKPORT_PCLKS	0x2024
#define GAT_AXI2APB_PERIC0_IPCLKPORT_ACLK	0x2028
#define GAT_AXI2APB_PERIC1_IPCLKPORT_ACLK	0x202c
#define GAT_AXI2APB_PERIC2_IPCLKPORT_ACLK	0x2030
#define GAT_BUS_D_PERIC_IPCLKPORT_DMACLK	0x2034
#define GAT_BUS_D_PERIC_IPCLKPORT_EQOSCLK	0x2038
#define GAT_BUS_D_PERIC_IPCLKPORT_MAINCLK	0x203c
#define GAT_BUS_P_PERIC_IPCLKPORT_EQOSCLK	0x2040
#define GAT_BUS_P_PERIC_IPCLKPORT_MAINCLK	0x2044
#define GAT_BUS_P_PERIC_IPCLKPORT_SMMUCLK	0x2048
#define GAT_EQOS_TOP_IPCLKPORT_ACLK_I		0x204c
#define GAT_EQOS_TOP_IPCLKPORT_CLK_RX_I		0x2050
#define GAT_EQOS_TOP_IPCLKPORT_HCLK_I		0x2054
#define GAT_EQOS_TOP_IPCLKPORT_RGMII_CLK_I	0x2058
#define GAT_EQOS_TOP_IPCLKPORT_RII_CLK_I	0x205c
#define GAT_EQOS_TOP_IPCLKPORT_RMII_CLK_I	0x2060
#define GAT_GPIO_PERIC_IPCLKPORT_PCLK		0x2064
#define GAT_NS_BRDG_PERIC_IPCLKPORT_CLK__PSOC_PERIC__CLK_PERIC_D	0x2068
#define GAT_NS_BRDG_PERIC_IPCLKPORT_CLK__PSOC_PERIC__CLK_PERIC_P	0x206c
#define GAT_PERIC_ADC0_IPCLKPORT_PCLK_S0	0x2070
#define GAT_PERIC_DMA0_IPCLKPORT_ACLK		0x2074
#define GAT_PERIC_DMA1_IPCLKPORT_ACLK		0x2078
#define GAT_PERIC_I2C0_IPCLKPORT_I_PCLK		0x207c
#define GAT_PERIC_I2C1_IPCLKPORT_I_PCLK		0x2080
#define GAT_PERIC_I2C2_IPCLKPORT_I_PCLK		0x2084
#define GAT_PERIC_I2C3_IPCLKPORT_I_PCLK		0x2088
#define GAT_PERIC_I2C4_IPCLKPORT_I_PCLK		0x208c
#define GAT_PERIC_I2C5_IPCLKPORT_I_PCLK		0x2090
#define GAT_PERIC_I2C6_IPCLKPORT_I_PCLK		0x2094
#define GAT_PERIC_I2C7_IPCLKPORT_I_PCLK		0x2098
#define GAT_PERIC_MCAN0_IPCLKPORT_CCLK		0x209c
#define GAT_PERIC_MCAN0_IPCLKPORT_PCLK		0x20a0
#define GAT_PERIC_MCAN1_IPCLKPORT_CCLK		0x20a4
#define GAT_PERIC_MCAN1_IPCLKPORT_PCLK		0x20a8
#define GAT_PERIC_MCAN2_IPCLKPORT_CCLK		0x20ac
#define GAT_PERIC_MCAN2_IPCLKPORT_PCLK		0x20b0
#define GAT_PERIC_MCAN3_IPCLKPORT_CCLK		0x20b4
#define GAT_PERIC_MCAN3_IPCLKPORT_PCLK		0x20b8
#define GAT_PERIC_PWM0_IPCLKPORT_I_PCLK_S0	0x20bc
#define GAT_PERIC_PWM1_IPCLKPORT_I_PCLK_S0	0x20c0
#define GAT_PERIC_SMMU_IPCLKPORT_CCLK		0x20c4
#define GAT_PERIC_SMMU_IPCLKPORT_PERIC_BCLK	0x20c8
#define GAT_PERIC_SPI0_IPCLKPORT_I_PCLK		0x20cc
#define GAT_PERIC_SPI0_IPCLKPORT_I_SCLK_SPI	0x20d0
#define GAT_PERIC_SPI1_IPCLKPORT_I_PCLK		0x20d4
#define GAT_PERIC_SPI1_IPCLKPORT_I_SCLK_SPI	0x20d8
#define GAT_PERIC_SPI2_IPCLKPORT_I_PCLK		0x20dc
#define GAT_PERIC_SPI2_IPCLKPORT_I_SCLK_SPI	0x20e0
#define GAT_PERIC_TDM0_IPCLKPORT_HCLK_M		0x20e4
#define GAT_PERIC_TDM0_IPCLKPORT_PCLK		0x20e8
#define GAT_PERIC_TDM1_IPCLKPORT_HCLK_M		0x20ec
#define GAT_PERIC_TDM1_IPCLKPORT_PCLK		0x20f0
#define GAT_PERIC_UART0_IPCLKPORT_I_SCLK_UART	0x20f4
#define GAT_PERIC_UART0_IPCLKPORT_PCLK		0x20f8
#define GAT_PERIC_UART1_IPCLKPORT_I_SCLK_UART	0x20fc
#define GAT_PERIC_UART1_IPCLKPORT_PCLK		0x2100
#define GAT_SYSREG_PERI_IPCLKPORT_PCLK		0x2104

static const unsigned long peric_clk_regs[] __initconst = {
	PLL_CON0_PERIC_DMACLK_MUX,
	PLL_CON0_PERIC_EQOS_BUSCLK_MUX,
	PLL_CON0_PERIC_PCLK_MUX,
	PLL_CON0_PERIC_TBUCLK_MUX,
	PLL_CON0_SPI_CLK,
	PLL_CON0_SPI_PCLK,
	PLL_CON0_UART_CLK,
	PLL_CON0_UART_PCLK,
	MUX_PERIC_EQOS_PHYRXCLK,
	DIV_EQOS_BUSCLK,
	DIV_PERIC_MCAN_CLK,
	DIV_RGMII_CLK,
	DIV_RII_CLK,
	DIV_RMII_CLK,
	DIV_SPI_CLK,
	DIV_UART_CLK,
	GAT_EQOS_TOP_IPCLKPORT_CLK_PTP_REF_I,
	GAT_GPIO_PERIC_IPCLKPORT_OSCCLK,
	GAT_PERIC_ADC0_IPCLKPORT_I_OSCCLK,
	GAT_PERIC_CMU_PERIC_IPCLKPORT_PCLK,
	GAT_PERIC_PWM0_IPCLKPORT_I_OSCCLK,
	GAT_PERIC_PWM1_IPCLKPORT_I_OSCCLK,
	GAT_ASYNC_APB_DMA0_IPCLKPORT_PCLKM,
	GAT_ASYNC_APB_DMA0_IPCLKPORT_PCLKS,
	GAT_ASYNC_APB_DMA1_IPCLKPORT_PCLKM,
	GAT_ASYNC_APB_DMA1_IPCLKPORT_PCLKS,
	GAT_AXI2APB_PERIC0_IPCLKPORT_ACLK,
	GAT_AXI2APB_PERIC1_IPCLKPORT_ACLK,
	GAT_AXI2APB_PERIC2_IPCLKPORT_ACLK,
	GAT_BUS_D_PERIC_IPCLKPORT_DMACLK,
	GAT_BUS_D_PERIC_IPCLKPORT_EQOSCLK,
	GAT_BUS_D_PERIC_IPCLKPORT_MAINCLK,
	GAT_BUS_P_PERIC_IPCLKPORT_EQOSCLK,
	GAT_BUS_P_PERIC_IPCLKPORT_MAINCLK,
	GAT_BUS_P_PERIC_IPCLKPORT_SMMUCLK,
	GAT_EQOS_TOP_IPCLKPORT_ACLK_I,
	GAT_EQOS_TOP_IPCLKPORT_CLK_RX_I,
	GAT_EQOS_TOP_IPCLKPORT_HCLK_I,
	GAT_EQOS_TOP_IPCLKPORT_RGMII_CLK_I,
	GAT_EQOS_TOP_IPCLKPORT_RII_CLK_I,
	GAT_EQOS_TOP_IPCLKPORT_RMII_CLK_I,
	GAT_GPIO_PERIC_IPCLKPORT_PCLK,
	GAT_NS_BRDG_PERIC_IPCLKPORT_CLK__PSOC_PERIC__CLK_PERIC_D,
	GAT_NS_BRDG_PERIC_IPCLKPORT_CLK__PSOC_PERIC__CLK_PERIC_P,
	GAT_PERIC_ADC0_IPCLKPORT_PCLK_S0,
	GAT_PERIC_DMA0_IPCLKPORT_ACLK,
	GAT_PERIC_DMA1_IPCLKPORT_ACLK,
	GAT_PERIC_I2C0_IPCLKPORT_I_PCLK,
	GAT_PERIC_I2C1_IPCLKPORT_I_PCLK,
	GAT_PERIC_I2C2_IPCLKPORT_I_PCLK,
	GAT_PERIC_I2C3_IPCLKPORT_I_PCLK,
	GAT_PERIC_I2C4_IPCLKPORT_I_PCLK,
	GAT_PERIC_I2C5_IPCLKPORT_I_PCLK,
	GAT_PERIC_I2C6_IPCLKPORT_I_PCLK,
	GAT_PERIC_I2C7_IPCLKPORT_I_PCLK,
	GAT_PERIC_MCAN0_IPCLKPORT_CCLK,
	GAT_PERIC_MCAN0_IPCLKPORT_PCLK,
	GAT_PERIC_MCAN1_IPCLKPORT_CCLK,
	GAT_PERIC_MCAN1_IPCLKPORT_PCLK,
	GAT_PERIC_MCAN2_IPCLKPORT_CCLK,
	GAT_PERIC_MCAN2_IPCLKPORT_PCLK,
	GAT_PERIC_MCAN3_IPCLKPORT_CCLK,
	GAT_PERIC_MCAN3_IPCLKPORT_PCLK,
	GAT_PERIC_PWM0_IPCLKPORT_I_PCLK_S0,
	GAT_PERIC_PWM1_IPCLKPORT_I_PCLK_S0,
	GAT_PERIC_SMMU_IPCLKPORT_CCLK,
	GAT_PERIC_SMMU_IPCLKPORT_PERIC_BCLK,
	GAT_PERIC_SPI0_IPCLKPORT_I_PCLK,
	GAT_PERIC_SPI0_IPCLKPORT_I_SCLK_SPI,
	GAT_PERIC_SPI1_IPCLKPORT_I_PCLK,
	GAT_PERIC_SPI1_IPCLKPORT_I_SCLK_SPI,
	GAT_PERIC_SPI2_IPCLKPORT_I_PCLK,
	GAT_PERIC_SPI2_IPCLKPORT_I_SCLK_SPI,
	GAT_PERIC_TDM0_IPCLKPORT_HCLK_M,
	GAT_PERIC_TDM0_IPCLKPORT_PCLK,
	GAT_PERIC_TDM1_IPCLKPORT_HCLK_M,
	GAT_PERIC_TDM1_IPCLKPORT_PCLK,
	GAT_PERIC_UART0_IPCLKPORT_I_SCLK_UART,
	GAT_PERIC_UART0_IPCLKPORT_PCLK,
	GAT_PERIC_UART1_IPCLKPORT_I_SCLK_UART,
	GAT_PERIC_UART1_IPCLKPORT_PCLK,
	GAT_SYSREG_PERI_IPCLKPORT_PCLK,
};

static const struct samsung_fixed_rate_clock peric_fixed_clks[] __initconst = {
	FRATE(PERIC_EQOS_PHYRXCLK, "eqos_phyrxclk", NULL, 0, 125000000),
};

/* List of parent clocks for Muxes in CMU_PERIC */
PNAME(mout_peric_dmaclk_p) = { "fin_pll", "cmu_peric_shared1div4_dmaclk_gate" };
PNAME(mout_peric_eqos_busclk_p) = { "fin_pll", "dout_cmu_pll_shared0_div4" };
PNAME(mout_peric_pclk_p) = { "fin_pll", "dout_cmu_peric_shared1div36" };
PNAME(mout_peric_tbuclk_p) = { "fin_pll", "dout_cmu_peric_shared0div3_tbuclk" };
PNAME(mout_peric_spi_clk_p) = { "fin_pll", "dout_cmu_peric_shared0div20" };
PNAME(mout_peric_spi_pclk_p) = { "fin_pll", "dout_cmu_peric_shared1div36" };
PNAME(mout_peric_uart_clk_p) = { "fin_pll", "dout_cmu_peric_shared1div4_dmaclk" };
PNAME(mout_peric_uart_pclk_p) = { "fin_pll", "dout_cmu_peric_shared1div36" };
PNAME(mout_peric_eqos_phyrxclk_p) = { "dout_peric_rgmii_clk", "eqos_phyrxclk" };

static const struct samsung_mux_clock peric_mux_clks[] __initconst = {
	MUX(0, "mout_peric_dmaclk", mout_peric_dmaclk_p, PLL_CON0_PERIC_DMACLK_MUX, 4, 1),
	MUX(0, "mout_peric_eqos_busclk", mout_peric_eqos_busclk_p,
	    PLL_CON0_PERIC_EQOS_BUSCLK_MUX, 4, 1),
	MUX(0, "mout_peric_pclk", mout_peric_pclk_p, PLL_CON0_PERIC_PCLK_MUX, 4, 1),
	MUX(0, "mout_peric_tbuclk", mout_peric_tbuclk_p, PLL_CON0_PERIC_TBUCLK_MUX, 4, 1),
	MUX(0, "mout_peric_spi_clk", mout_peric_spi_clk_p, PLL_CON0_SPI_CLK, 4, 1),
	MUX(0, "mout_peric_spi_pclk", mout_peric_spi_pclk_p, PLL_CON0_SPI_PCLK, 4, 1),
	MUX(0, "mout_peric_uart_clk", mout_peric_uart_clk_p, PLL_CON0_UART_CLK, 4, 1),
	MUX(0, "mout_peric_uart_pclk", mout_peric_uart_pclk_p, PLL_CON0_UART_PCLK, 4, 1),
	MUX(PERIC_EQOS_PHYRXCLK_MUX, "mout_peric_eqos_phyrxclk", mout_peric_eqos_phyrxclk_p,
		MUX_PERIC_EQOS_PHYRXCLK, 0, 1),
};

static const struct samsung_div_clock peric_div_clks[] __initconst = {
	DIV(0, "dout_peric_eqos_busclk", "mout_peric_eqos_busclk", DIV_EQOS_BUSCLK, 0, 4),
	DIV(0, "dout_peric_mcan_clk", "mout_peric_dmaclk", DIV_PERIC_MCAN_CLK, 0, 4),
	DIV(PERIC_DOUT_RGMII_CLK, "dout_peric_rgmii_clk", "mout_peric_eqos_busclk",
		DIV_RGMII_CLK, 0, 4),
	DIV(0, "dout_peric_rii_clk", "dout_peric_rmii_clk", DIV_RII_CLK, 0, 4),
	DIV(0, "dout_peric_rmii_clk", "dout_peric_rgmii_clk", DIV_RMII_CLK, 0, 4),
	DIV(0, "dout_peric_spi_clk", "mout_peric_spi_clk", DIV_SPI_CLK, 0, 6),
	DIV(0, "dout_peric_uart_clk", "mout_peric_uart_clk", DIV_UART_CLK, 0, 6),
};

static const struct samsung_gate_clock peric_gate_clks[] __initconst = {
	GATE(PERIC_EQOS_TOP_IPCLKPORT_CLK_PTP_REF_I, "peric_eqos_top_ipclkport_clk_ptp_ref_i",
	     "fin_pll", GAT_EQOS_TOP_IPCLKPORT_CLK_PTP_REF_I, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "peric_gpio_peric_ipclkport_oscclk", "fin_pll", GAT_GPIO_PERIC_IPCLKPORT_OSCCLK,
	     21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_PCLK_ADCIF, "peric_adc0_ipclkport_i_oscclk", "fin_pll",
	     GAT_PERIC_ADC0_IPCLKPORT_I_OSCCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "peric_cmu_peric_ipclkport_pclk", "mout_peric_pclk",
	     GAT_PERIC_CMU_PERIC_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "peric_pwm0_ipclkport_i_oscclk", "fin_pll", GAT_PERIC_PWM0_IPCLKPORT_I_OSCCLK, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(0, "peric_pwm1_ipclkport_i_oscclk", "fin_pll", GAT_PERIC_PWM1_IPCLKPORT_I_OSCCLK, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(0, "peric_async_apb_dma0_ipclkport_pclkm", "mout_peric_dmaclk",
	     GAT_ASYNC_APB_DMA0_IPCLKPORT_PCLKM, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "peric_async_apb_dma0_ipclkport_pclks", "mout_peric_pclk",
	     GAT_ASYNC_APB_DMA0_IPCLKPORT_PCLKS, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "peric_async_apb_dma1_ipclkport_pclkm", "mout_peric_dmaclk",
	     GAT_ASYNC_APB_DMA1_IPCLKPORT_PCLKM, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "peric_async_apb_dma1_ipclkport_pclks", "mout_peric_pclk",
	     GAT_ASYNC_APB_DMA1_IPCLKPORT_PCLKS, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "peric_axi2apb_peric0_ipclkport_aclk", "mout_peric_pclk",
	     GAT_AXI2APB_PERIC0_IPCLKPORT_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "peric_axi2apb_peric1_ipclkport_aclk", "mout_peric_pclk",
	     GAT_AXI2APB_PERIC1_IPCLKPORT_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "peric_axi2apb_peric2_ipclkport_aclk", "mout_peric_pclk",
	     GAT_AXI2APB_PERIC2_IPCLKPORT_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "peric_bus_d_peric_ipclkport_dmaclk", "mout_peric_dmaclk",
	     GAT_BUS_D_PERIC_IPCLKPORT_DMACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_BUS_D_PERIC_IPCLKPORT_EQOSCLK, "peric_bus_d_peric_ipclkport_eqosclk",
	     "dout_peric_eqos_busclk", GAT_BUS_D_PERIC_IPCLKPORT_EQOSCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "peric_bus_d_peric_ipclkport_mainclk", "mout_peric_tbuclk",
	     GAT_BUS_D_PERIC_IPCLKPORT_MAINCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_BUS_P_PERIC_IPCLKPORT_EQOSCLK, "peric_bus_p_peric_ipclkport_eqosclk",
	     "dout_peric_eqos_busclk", GAT_BUS_P_PERIC_IPCLKPORT_EQOSCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "peric_bus_p_peric_ipclkport_mainclk", "mout_peric_pclk",
	     GAT_BUS_P_PERIC_IPCLKPORT_MAINCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "peric_bus_p_peric_ipclkport_smmuclk", "mout_peric_tbuclk",
	     GAT_BUS_P_PERIC_IPCLKPORT_SMMUCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_EQOS_TOP_IPCLKPORT_ACLK_I, "peric_eqos_top_ipclkport_aclk_i",
	     "dout_peric_eqos_busclk", GAT_EQOS_TOP_IPCLKPORT_ACLK_I, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_EQOS_TOP_IPCLKPORT_CLK_RX_I, "peric_eqos_top_ipclkport_clk_rx_i",
	     "mout_peric_eqos_phyrxclk", GAT_EQOS_TOP_IPCLKPORT_CLK_RX_I, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_EQOS_TOP_IPCLKPORT_HCLK_I, "peric_eqos_top_ipclkport_hclk_i",
	     "dout_peric_eqos_busclk", GAT_EQOS_TOP_IPCLKPORT_HCLK_I, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_EQOS_TOP_IPCLKPORT_RGMII_CLK_I, "peric_eqos_top_ipclkport_rgmii_clk_i",
	     "dout_peric_rgmii_clk", GAT_EQOS_TOP_IPCLKPORT_RGMII_CLK_I, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "peric_eqos_top_ipclkport_rii_clk_i", "dout_peric_rii_clk",
	     GAT_EQOS_TOP_IPCLKPORT_RII_CLK_I, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "peric_eqos_top_ipclkport_rmii_clk_i", "dout_peric_rmii_clk",
	     GAT_EQOS_TOP_IPCLKPORT_RMII_CLK_I, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "peric_gpio_peric_ipclkport_pclk", "mout_peric_pclk",
	     GAT_GPIO_PERIC_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "peric_ns_brdg_peric_ipclkport_clk__psoc_peric__clk_peric_d", "mout_peric_tbuclk",
	     GAT_NS_BRDG_PERIC_IPCLKPORT_CLK__PSOC_PERIC__CLK_PERIC_D, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "peric_ns_brdg_peric_ipclkport_clk__psoc_peric__clk_peric_p", "mout_peric_pclk",
	     GAT_NS_BRDG_PERIC_IPCLKPORT_CLK__PSOC_PERIC__CLK_PERIC_P, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "peric_adc0_ipclkport_pclk_s0", "mout_peric_pclk",
	     GAT_PERIC_ADC0_IPCLKPORT_PCLK_S0, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_DMA0_IPCLKPORT_ACLK, "peric_dma0_ipclkport_aclk", "mout_peric_dmaclk",
	     GAT_PERIC_DMA0_IPCLKPORT_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_DMA1_IPCLKPORT_ACLK, "peric_dma1_ipclkport_aclk", "mout_peric_dmaclk",
	     GAT_PERIC_DMA1_IPCLKPORT_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_PCLK_HSI2C0, "peric_i2c0_ipclkport_i_pclk", "mout_peric_pclk",
	     GAT_PERIC_I2C0_IPCLKPORT_I_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_PCLK_HSI2C1, "peric_i2c1_ipclkport_i_pclk", "mout_peric_pclk",
	     GAT_PERIC_I2C1_IPCLKPORT_I_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_PCLK_HSI2C2, "peric_i2c2_ipclkport_i_pclk", "mout_peric_pclk",
	     GAT_PERIC_I2C2_IPCLKPORT_I_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_PCLK_HSI2C3, "peric_i2c3_ipclkport_i_pclk", "mout_peric_pclk",
	     GAT_PERIC_I2C3_IPCLKPORT_I_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_PCLK_HSI2C4, "peric_i2c4_ipclkport_i_pclk", "mout_peric_pclk",
	     GAT_PERIC_I2C4_IPCLKPORT_I_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_PCLK_HSI2C5, "peric_i2c5_ipclkport_i_pclk", "mout_peric_pclk",
	     GAT_PERIC_I2C5_IPCLKPORT_I_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_PCLK_HSI2C6, "peric_i2c6_ipclkport_i_pclk", "mout_peric_pclk",
	     GAT_PERIC_I2C6_IPCLKPORT_I_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_PCLK_HSI2C7, "peric_i2c7_ipclkport_i_pclk", "mout_peric_pclk",
	     GAT_PERIC_I2C7_IPCLKPORT_I_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_MCAN0_IPCLKPORT_CCLK, "peric_mcan0_ipclkport_cclk", "dout_peric_mcan_clk",
	     GAT_PERIC_MCAN0_IPCLKPORT_CCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_MCAN0_IPCLKPORT_PCLK, "peric_mcan0_ipclkport_pclk", "mout_peric_pclk",
	     GAT_PERIC_MCAN0_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_MCAN1_IPCLKPORT_CCLK, "peric_mcan1_ipclkport_cclk", "dout_peric_mcan_clk",
	     GAT_PERIC_MCAN1_IPCLKPORT_CCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_MCAN1_IPCLKPORT_PCLK, "peric_mcan1_ipclkport_pclk", "mout_peric_pclk",
	     GAT_PERIC_MCAN1_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_MCAN2_IPCLKPORT_CCLK, "peric_mcan2_ipclkport_cclk", "dout_peric_mcan_clk",
	     GAT_PERIC_MCAN2_IPCLKPORT_CCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_MCAN2_IPCLKPORT_PCLK, "peric_mcan2_ipclkport_pclk", "mout_peric_pclk",
	     GAT_PERIC_MCAN2_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_MCAN3_IPCLKPORT_CCLK, "peric_mcan3_ipclkport_cclk", "dout_peric_mcan_clk",
	     GAT_PERIC_MCAN3_IPCLKPORT_CCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_MCAN3_IPCLKPORT_PCLK, "peric_mcan3_ipclkport_pclk", "mout_peric_pclk",
	     GAT_PERIC_MCAN3_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_PWM0_IPCLKPORT_I_PCLK_S0, "peric_pwm0_ipclkport_i_pclk_s0", "mout_peric_pclk",
	     GAT_PERIC_PWM0_IPCLKPORT_I_PCLK_S0, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_PWM1_IPCLKPORT_I_PCLK_S0, "peric_pwm1_ipclkport_i_pclk_s0", "mout_peric_pclk",
	     GAT_PERIC_PWM1_IPCLKPORT_I_PCLK_S0, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "peric_smmu_ipclkport_cclk", "mout_peric_tbuclk",
	     GAT_PERIC_SMMU_IPCLKPORT_CCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "peric_smmu_ipclkport_peric_bclk", "mout_peric_tbuclk",
	     GAT_PERIC_SMMU_IPCLKPORT_PERIC_BCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_PCLK_SPI0, "peric_spi0_ipclkport_i_pclk", "mout_peric_spi_pclk",
	     GAT_PERIC_SPI0_IPCLKPORT_I_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_SCLK_SPI0, "peric_spi0_ipclkport_i_sclk_spi", "dout_peric_spi_clk",
	     GAT_PERIC_SPI0_IPCLKPORT_I_SCLK_SPI, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_PCLK_SPI1, "peric_spi1_ipclkport_i_pclk", "mout_peric_spi_pclk",
	     GAT_PERIC_SPI1_IPCLKPORT_I_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_SCLK_SPI1, "peric_spi1_ipclkport_i_sclk_spi", "dout_peric_spi_clk",
	     GAT_PERIC_SPI1_IPCLKPORT_I_SCLK_SPI, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_PCLK_SPI2, "peric_spi2_ipclkport_i_pclk", "mout_peric_spi_pclk",
	     GAT_PERIC_SPI2_IPCLKPORT_I_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_SCLK_SPI2, "peric_spi2_ipclkport_i_sclk_spi", "dout_peric_spi_clk",
	     GAT_PERIC_SPI2_IPCLKPORT_I_SCLK_SPI, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_HCLK_TDM0, "peric_tdm0_ipclkport_hclk_m", "mout_peric_pclk",
	     GAT_PERIC_TDM0_IPCLKPORT_HCLK_M, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_PCLK_TDM0, "peric_tdm0_ipclkport_pclk", "mout_peric_pclk",
	     GAT_PERIC_TDM0_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_HCLK_TDM1, "peric_tdm1_ipclkport_hclk_m", "mout_peric_pclk",
	     GAT_PERIC_TDM1_IPCLKPORT_HCLK_M, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_PCLK_TDM1, "peric_tdm1_ipclkport_pclk", "mout_peric_pclk",
	     GAT_PERIC_TDM1_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_SCLK_UART0, "peric_uart0_ipclkport_i_sclk_uart", "dout_peric_uart_clk",
	     GAT_PERIC_UART0_IPCLKPORT_I_SCLK_UART, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_PCLK_UART0, "peric_uart0_ipclkport_pclk", "mout_peric_uart_pclk",
	     GAT_PERIC_UART0_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_SCLK_UART1, "peric_uart1_ipclkport_i_sclk_uart", "dout_peric_uart_clk",
	     GAT_PERIC_UART1_IPCLKPORT_I_SCLK_UART, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PERIC_PCLK_UART1, "peric_uart1_ipclkport_pclk", "mout_peric_uart_pclk",
	     GAT_PERIC_UART1_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "peric_sysreg_peri_ipclkport_pclk", "mout_peric_pclk",
	     GAT_SYSREG_PERI_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
};

static const struct samsung_cmu_info peric_cmu_info __initconst = {
	.mux_clks		= peric_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(peric_mux_clks),
	.div_clks		= peric_div_clks,
	.nr_div_clks		= ARRAY_SIZE(peric_div_clks),
	.gate_clks		= peric_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(peric_gate_clks),
	.fixed_clks		= peric_fixed_clks,
	.nr_fixed_clks		= ARRAY_SIZE(peric_fixed_clks),
	.nr_clk_ids		= CLKS_NR_PERIC,
	.clk_regs		= peric_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(peric_clk_regs),
	.clk_name		= "dout_cmu_pll_shared0_div4",
};

/* Register Offset definitions for CMU_FSYS0 (0x15010000) */
#define PLL_CON0_CLKCMU_FSYS0_UNIPRO		0x100
#define PLL_CON0_CLK_FSYS0_SLAVEBUSCLK		0x140
#define PLL_CON0_EQOS_RGMII_125_MUX1		0x160
#define DIV_CLK_UNIPRO				0x1800
#define DIV_EQS_RGMII_CLK_125			0x1804
#define DIV_PERIBUS_GRP				0x1808
#define DIV_EQOS_RII_CLK2O5			0x180c
#define DIV_EQOS_RMIICLK_25			0x1810
#define DIV_PCIE_PHY_OSCCLK			0x1814
#define GAT_FSYS0_EQOS_TOP0_IPCLKPORT_CLK_PTP_REF_I	0x2004
#define GAT_FSYS0_EQOS_TOP0_IPCLKPORT_CLK_RX_I	0x2008
#define GAT_FSYS0_FSYS0_CMU_FSYS0_IPCLKPORT_PCLK	0x200c
#define GAT_FSYS0_GPIO_FSYS0_IPCLKPORT_OSCCLK	0x2010
#define GAT_FSYS0_PCIE_TOP_IPCLKPORT_PCIEG3_PHY_X4_INST_0_PLL_REFCLK_FROM_XO	0x2014
#define GAT_FSYS0_PCIE_TOP_IPCLKPORT_PIPE_PAL_INST_0_I_IMMORTAL_CLK	0x2018
#define GAT_FSYS0_PCIE_TOP_IPCLKPORT_FSD_PCIE_SUB_CTRL_INST_0_AUX_CLK_SOC	0x201c
#define GAT_FSYS0_UFS_TOP0_IPCLKPORT_I_MPHY_REFCLK_IXTAL24	0x2020
#define GAT_FSYS0_UFS_TOP0_IPCLKPORT_I_MPHY_REFCLK_IXTAL26	0x2024
#define GAT_FSYS0_UFS_TOP1_IPCLKPORT_I_MPHY_REFCLK_IXTAL24	0x2028
#define GAT_FSYS0_UFS_TOP1_IPCLKPORT_I_MPHY_REFCLK_IXTAL26	0x202c
#define GAT_FSYS0_AHBBR_FSYS0_IPCLKPORT_HCLK	0x2038
#define GAT_FSYS0_AXI2APB_FSYS0_IPCLKPORT_ACLK	0x203c
#define GAT_FSYS0_BUS_D_FSYS0_IPCLKPORT_MAINCLK	0x2040
#define GAT_FSYS0_BUS_D_FSYS0_IPCLKPORT_PERICLK	0x2044
#define GAT_FSYS0_BUS_P_FSYS0_IPCLKPORT_MAINCLK	0x2048
#define GAT_FSYS0_BUS_P_FSYS0_IPCLKPORT_TCUCLK	0x204c
#define GAT_FSYS0_CPE425_IPCLKPORT_ACLK		0x2050
#define GAT_FSYS0_EQOS_TOP0_IPCLKPORT_ACLK_I	0x2054
#define GAT_FSYS0_EQOS_TOP0_IPCLKPORT_HCLK_I	0x2058
#define GAT_FSYS0_EQOS_TOP0_IPCLKPORT_RGMII_CLK_I	0x205c
#define GAT_FSYS0_EQOS_TOP0_IPCLKPORT_RII_CLK_I	0x2060
#define GAT_FSYS0_EQOS_TOP0_IPCLKPORT_RMII_CLK_I	0x2064
#define GAT_FSYS0_GPIO_FSYS0_IPCLKPORT_PCLK	0x2068
#define GAT_FSYS0_NS_BRDG_FSYS0_IPCLKPORT_CLK__PSOC_FSYS0__CLK_FSYS0_D	0x206c
#define GAT_FSYS0_NS_BRDG_FSYS0_IPCLKPORT_CLK__PSOC_FSYS0__CLK_FSYS0_D1	0x2070
#define GAT_FSYS0_NS_BRDG_FSYS0_IPCLKPORT_CLK__PSOC_FSYS0__CLK_FSYS0_P	0x2074
#define GAT_FSYS0_NS_BRDG_FSYS0_IPCLKPORT_CLK__PSOC_FSYS0__CLK_FSYS0_S	0x2078
#define GAT_FSYS0_PCIE_TOP_IPCLKPORT_PCIEG3_PHY_X4_INST_0_I_APB_PCLK	0x207c
#define GAT_FSYS0_PCIE_TOP_IPCLKPORT_PCIEG3_PHY_X4_INST_0_PLL_REFCLK_FROM_SYSPLL	0x2080
#define GAT_FSYS0_PCIE_TOP_IPCLKPORT_PIPE_PAL_INST_0_I_APB_PCLK_0	0x2084
#define GAT_FSYS0_PCIE_TOP_IPCLKPORT_FSD_PCIE_SUB_CTRL_INST_0_DBI_ACLK_SOC	0x2088
#define GAT_FSYS0_PCIE_TOP_IPCLKPORT_FSD_PCIE_SUB_CTRL_INST_0_I_DRIVER_APB_CLK	0x208c
#define GAT_FSYS0_PCIE_TOP_IPCLKPORT_FSD_PCIE_SUB_CTRL_INST_0_MSTR_ACLK_SOC	0x2090
#define GAT_FSYS0_PCIE_TOP_IPCLKPORT_FSD_PCIE_SUB_CTRL_INST_0_SLV_ACLK_SOC	0x2094
#define GAT_FSYS0_SMMU_FSYS0_IPCLKPORT_CCLK	0x2098
#define GAT_FSYS0_SMMU_FSYS0_IPCLKPORT_FSYS0_BCLK	0x209c
#define GAT_FSYS0_SYSREG_FSYS0_IPCLKPORT_PCLK	0x20a0
#define GAT_FSYS0_UFS_TOP0_IPCLKPORT_HCLK_BUS	0x20a4
#define GAT_FSYS0_UFS_TOP0_IPCLKPORT_I_ACLK	0x20a8
#define GAT_FSYS0_UFS_TOP0_IPCLKPORT_I_CLK_UNIPRO	0x20ac
#define GAT_FSYS0_UFS_TOP0_IPCLKPORT_I_FMP_CLK	0x20b0
#define GAT_FSYS0_UFS_TOP1_IPCLKPORT_HCLK_BUS	0x20b4
#define GAT_FSYS0_UFS_TOP1_IPCLKPORT_I_ACLK	0x20b8
#define GAT_FSYS0_UFS_TOP1_IPCLKPORT_I_CLK_UNIPRO	0x20bc
#define GAT_FSYS0_UFS_TOP1_IPCLKPORT_I_FMP_CLK	0x20c0
#define GAT_FSYS0_RII_CLK_DIVGATE			0x20d4

static const unsigned long fsys0_clk_regs[] __initconst = {
	PLL_CON0_CLKCMU_FSYS0_UNIPRO,
	PLL_CON0_CLK_FSYS0_SLAVEBUSCLK,
	PLL_CON0_EQOS_RGMII_125_MUX1,
	DIV_CLK_UNIPRO,
	DIV_EQS_RGMII_CLK_125,
	DIV_PERIBUS_GRP,
	DIV_EQOS_RII_CLK2O5,
	DIV_EQOS_RMIICLK_25,
	DIV_PCIE_PHY_OSCCLK,
	GAT_FSYS0_EQOS_TOP0_IPCLKPORT_CLK_PTP_REF_I,
	GAT_FSYS0_EQOS_TOP0_IPCLKPORT_CLK_RX_I,
	GAT_FSYS0_FSYS0_CMU_FSYS0_IPCLKPORT_PCLK,
	GAT_FSYS0_GPIO_FSYS0_IPCLKPORT_OSCCLK,
	GAT_FSYS0_PCIE_TOP_IPCLKPORT_PCIEG3_PHY_X4_INST_0_PLL_REFCLK_FROM_XO,
	GAT_FSYS0_PCIE_TOP_IPCLKPORT_PIPE_PAL_INST_0_I_IMMORTAL_CLK,
	GAT_FSYS0_PCIE_TOP_IPCLKPORT_FSD_PCIE_SUB_CTRL_INST_0_AUX_CLK_SOC,
	GAT_FSYS0_UFS_TOP0_IPCLKPORT_I_MPHY_REFCLK_IXTAL24,
	GAT_FSYS0_UFS_TOP0_IPCLKPORT_I_MPHY_REFCLK_IXTAL26,
	GAT_FSYS0_UFS_TOP1_IPCLKPORT_I_MPHY_REFCLK_IXTAL24,
	GAT_FSYS0_UFS_TOP1_IPCLKPORT_I_MPHY_REFCLK_IXTAL26,
	GAT_FSYS0_AHBBR_FSYS0_IPCLKPORT_HCLK,
	GAT_FSYS0_AXI2APB_FSYS0_IPCLKPORT_ACLK,
	GAT_FSYS0_BUS_D_FSYS0_IPCLKPORT_MAINCLK,
	GAT_FSYS0_BUS_D_FSYS0_IPCLKPORT_PERICLK,
	GAT_FSYS0_BUS_P_FSYS0_IPCLKPORT_MAINCLK,
	GAT_FSYS0_BUS_P_FSYS0_IPCLKPORT_TCUCLK,
	GAT_FSYS0_CPE425_IPCLKPORT_ACLK,
	GAT_FSYS0_EQOS_TOP0_IPCLKPORT_ACLK_I,
	GAT_FSYS0_EQOS_TOP0_IPCLKPORT_HCLK_I,
	GAT_FSYS0_EQOS_TOP0_IPCLKPORT_RGMII_CLK_I,
	GAT_FSYS0_EQOS_TOP0_IPCLKPORT_RII_CLK_I,
	GAT_FSYS0_EQOS_TOP0_IPCLKPORT_RMII_CLK_I,
	GAT_FSYS0_GPIO_FSYS0_IPCLKPORT_PCLK,
	GAT_FSYS0_NS_BRDG_FSYS0_IPCLKPORT_CLK__PSOC_FSYS0__CLK_FSYS0_D,
	GAT_FSYS0_NS_BRDG_FSYS0_IPCLKPORT_CLK__PSOC_FSYS0__CLK_FSYS0_D1,
	GAT_FSYS0_NS_BRDG_FSYS0_IPCLKPORT_CLK__PSOC_FSYS0__CLK_FSYS0_P,
	GAT_FSYS0_NS_BRDG_FSYS0_IPCLKPORT_CLK__PSOC_FSYS0__CLK_FSYS0_S,
	GAT_FSYS0_PCIE_TOP_IPCLKPORT_PCIEG3_PHY_X4_INST_0_I_APB_PCLK,
	GAT_FSYS0_PCIE_TOP_IPCLKPORT_PCIEG3_PHY_X4_INST_0_PLL_REFCLK_FROM_SYSPLL,
	GAT_FSYS0_PCIE_TOP_IPCLKPORT_PIPE_PAL_INST_0_I_APB_PCLK_0,
	GAT_FSYS0_PCIE_TOP_IPCLKPORT_FSD_PCIE_SUB_CTRL_INST_0_DBI_ACLK_SOC,
	GAT_FSYS0_PCIE_TOP_IPCLKPORT_FSD_PCIE_SUB_CTRL_INST_0_I_DRIVER_APB_CLK,
	GAT_FSYS0_PCIE_TOP_IPCLKPORT_FSD_PCIE_SUB_CTRL_INST_0_MSTR_ACLK_SOC,
	GAT_FSYS0_PCIE_TOP_IPCLKPORT_FSD_PCIE_SUB_CTRL_INST_0_SLV_ACLK_SOC,
	GAT_FSYS0_SMMU_FSYS0_IPCLKPORT_CCLK,
	GAT_FSYS0_SMMU_FSYS0_IPCLKPORT_FSYS0_BCLK,
	GAT_FSYS0_SYSREG_FSYS0_IPCLKPORT_PCLK,
	GAT_FSYS0_UFS_TOP0_IPCLKPORT_HCLK_BUS,
	GAT_FSYS0_UFS_TOP0_IPCLKPORT_I_ACLK,
	GAT_FSYS0_UFS_TOP0_IPCLKPORT_I_CLK_UNIPRO,
	GAT_FSYS0_UFS_TOP0_IPCLKPORT_I_FMP_CLK,
	GAT_FSYS0_UFS_TOP1_IPCLKPORT_HCLK_BUS,
	GAT_FSYS0_UFS_TOP1_IPCLKPORT_I_ACLK,
	GAT_FSYS0_UFS_TOP1_IPCLKPORT_I_CLK_UNIPRO,
	GAT_FSYS0_UFS_TOP1_IPCLKPORT_I_FMP_CLK,
	GAT_FSYS0_RII_CLK_DIVGATE,
};

static const struct samsung_fixed_rate_clock fsys0_fixed_clks[] __initconst = {
	FRATE(0, "pad_eqos0_phyrxclk", NULL, 0, 125000000),
	FRATE(0, "i_mphy_refclk_ixtal26", NULL, 0, 26000000),
	FRATE(0, "xtal_clk_pcie_phy", NULL, 0, 100000000),
};

/* List of parent clocks for Muxes in CMU_FSYS0 */
PNAME(mout_fsys0_clkcmu_fsys0_unipro_p) = { "fin_pll", "dout_cmu_pll_shared0_div6" };
PNAME(mout_fsys0_clk_fsys0_slavebusclk_p) = { "fin_pll", "dout_cmu_fsys0_shared1div4" };
PNAME(mout_fsys0_eqos_rgmii_125_mux1_p) = { "fin_pll", "dout_cmu_fsys0_shared0div4" };

static const struct samsung_mux_clock fsys0_mux_clks[] __initconst = {
	MUX(0, "mout_fsys0_clkcmu_fsys0_unipro", mout_fsys0_clkcmu_fsys0_unipro_p,
	    PLL_CON0_CLKCMU_FSYS0_UNIPRO, 4, 1),
	MUX(0, "mout_fsys0_clk_fsys0_slavebusclk", mout_fsys0_clk_fsys0_slavebusclk_p,
	    PLL_CON0_CLK_FSYS0_SLAVEBUSCLK, 4, 1),
	MUX(0, "mout_fsys0_eqos_rgmii_125_mux1", mout_fsys0_eqos_rgmii_125_mux1_p,
	    PLL_CON0_EQOS_RGMII_125_MUX1, 4, 1),
};

static const struct samsung_div_clock fsys0_div_clks[] __initconst = {
	DIV(0, "dout_fsys0_clk_unipro", "mout_fsys0_clkcmu_fsys0_unipro", DIV_CLK_UNIPRO, 0, 4),
	DIV(0, "dout_fsys0_eqs_rgmii_clk_125", "mout_fsys0_eqos_rgmii_125_mux1",
	    DIV_EQS_RGMII_CLK_125, 0, 4),
	DIV(FSYS0_DOUT_FSYS0_PERIBUS_GRP, "dout_fsys0_peribus_grp",
	    "mout_fsys0_clk_fsys0_slavebusclk", DIV_PERIBUS_GRP, 0, 4),
	DIV(0, "dout_fsys0_eqos_rii_clk2o5", "fsys0_rii_clk_divgate", DIV_EQOS_RII_CLK2O5, 0, 4),
	DIV(0, "dout_fsys0_eqos_rmiiclk_25", "mout_fsys0_eqos_rgmii_125_mux1",
	    DIV_EQOS_RMIICLK_25, 0, 5),
	DIV(0, "dout_fsys0_pcie_phy_oscclk", "mout_fsys0_eqos_rgmii_125_mux1",
	    DIV_PCIE_PHY_OSCCLK, 0, 4),
};

static const struct samsung_gate_clock fsys0_gate_clks[] __initconst = {
	GATE(FSYS0_EQOS_TOP0_IPCLKPORT_CLK_RX_I, "fsys0_eqos_top0_ipclkport_clk_rx_i",
	     "pad_eqos0_phyrxclk", GAT_FSYS0_EQOS_TOP0_IPCLKPORT_CLK_RX_I, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(PCIE_SUBCTRL_INST0_AUX_CLK_SOC,
	     "fsys0_pcie_top_ipclkport_fsd_pcie_sub_ctrl_inst_0_aux_clk_soc", "fin_pll",
	     GAT_FSYS0_PCIE_TOP_IPCLKPORT_FSD_PCIE_SUB_CTRL_INST_0_AUX_CLK_SOC, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys0_fsys0_cmu_fsys0_ipclkport_pclk", "dout_fsys0_peribus_grp",
	     GAT_FSYS0_FSYS0_CMU_FSYS0_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0,
	     "fsys0_pcie_top_ipclkport_pcieg3_phy_x4_inst_0_pll_refclk_from_xo",
	     "xtal_clk_pcie_phy",
	     GAT_FSYS0_PCIE_TOP_IPCLKPORT_PCIEG3_PHY_X4_INST_0_PLL_REFCLK_FROM_XO, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(UFS0_MPHY_REFCLK_IXTAL24, "fsys0_ufs_top0_ipclkport_i_mphy_refclk_ixtal24",
	     "i_mphy_refclk_ixtal26", GAT_FSYS0_UFS_TOP0_IPCLKPORT_I_MPHY_REFCLK_IXTAL24, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(UFS0_MPHY_REFCLK_IXTAL26, "fsys0_ufs_top0_ipclkport_i_mphy_refclk_ixtal26",
	     "i_mphy_refclk_ixtal26", GAT_FSYS0_UFS_TOP0_IPCLKPORT_I_MPHY_REFCLK_IXTAL26, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(UFS1_MPHY_REFCLK_IXTAL24, "fsys0_ufs_top1_ipclkport_i_mphy_refclk_ixtal24",
	     "i_mphy_refclk_ixtal26", GAT_FSYS0_UFS_TOP1_IPCLKPORT_I_MPHY_REFCLK_IXTAL24, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(UFS1_MPHY_REFCLK_IXTAL26, "fsys0_ufs_top1_ipclkport_i_mphy_refclk_ixtal26",
	     "i_mphy_refclk_ixtal26", GAT_FSYS0_UFS_TOP1_IPCLKPORT_I_MPHY_REFCLK_IXTAL26, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys0_ahbbr_fsys0_ipclkport_hclk", "dout_fsys0_peribus_grp",
	     GAT_FSYS0_AHBBR_FSYS0_IPCLKPORT_HCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys0_axi2apb_fsys0_ipclkport_aclk", "dout_fsys0_peribus_grp",
	     GAT_FSYS0_AXI2APB_FSYS0_IPCLKPORT_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys0_bus_d_fsys0_ipclkport_mainclk", "mout_fsys0_clk_fsys0_slavebusclk",
	     GAT_FSYS0_BUS_D_FSYS0_IPCLKPORT_MAINCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys0_bus_d_fsys0_ipclkport_periclk", "dout_fsys0_peribus_grp",
	     GAT_FSYS0_BUS_D_FSYS0_IPCLKPORT_PERICLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys0_bus_p_fsys0_ipclkport_mainclk", "dout_fsys0_peribus_grp",
	     GAT_FSYS0_BUS_P_FSYS0_IPCLKPORT_MAINCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys0_bus_p_fsys0_ipclkport_tcuclk", "mout_fsys0_eqos_rgmii_125_mux1",
	     GAT_FSYS0_BUS_P_FSYS0_IPCLKPORT_TCUCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys0_cpe425_ipclkport_aclk", "mout_fsys0_clk_fsys0_slavebusclk",
	     GAT_FSYS0_CPE425_IPCLKPORT_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(FSYS0_EQOS_TOP0_IPCLKPORT_ACLK_I, "fsys0_eqos_top0_ipclkport_aclk_i",
	     "dout_fsys0_peribus_grp", GAT_FSYS0_EQOS_TOP0_IPCLKPORT_ACLK_I, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(FSYS0_EQOS_TOP0_IPCLKPORT_HCLK_I, "fsys0_eqos_top0_ipclkport_hclk_i",
	     "dout_fsys0_peribus_grp", GAT_FSYS0_EQOS_TOP0_IPCLKPORT_HCLK_I, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(FSYS0_EQOS_TOP0_IPCLKPORT_RGMII_CLK_I, "fsys0_eqos_top0_ipclkport_rgmii_clk_i",
	      "dout_fsys0_eqs_rgmii_clk_125", GAT_FSYS0_EQOS_TOP0_IPCLKPORT_RGMII_CLK_I, 21,
	      CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys0_eqos_top0_ipclkport_rii_clk_i", "dout_fsys0_eqos_rii_clk2o5",
	     GAT_FSYS0_EQOS_TOP0_IPCLKPORT_RII_CLK_I, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys0_eqos_top0_ipclkport_rmii_clk_i", "dout_fsys0_eqos_rmiiclk_25",
	     GAT_FSYS0_EQOS_TOP0_IPCLKPORT_RMII_CLK_I, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys0_gpio_fsys0_ipclkport_pclk", "dout_fsys0_peribus_grp",
	     GAT_FSYS0_GPIO_FSYS0_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys0_gpio_fsys0_ipclkport_oscclk", "fin_pll",
	     GAT_FSYS0_GPIO_FSYS0_IPCLKPORT_OSCCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys0_ns_brdg_fsys0_ipclkport_clk__psoc_fsys0__clk_fsys0_d",
	     "mout_fsys0_clk_fsys0_slavebusclk",
	     GAT_FSYS0_NS_BRDG_FSYS0_IPCLKPORT_CLK__PSOC_FSYS0__CLK_FSYS0_D, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys0_ns_brdg_fsys0_ipclkport_clk__psoc_fsys0__clk_fsys0_d1",
	     "mout_fsys0_eqos_rgmii_125_mux1",
	     GAT_FSYS0_NS_BRDG_FSYS0_IPCLKPORT_CLK__PSOC_FSYS0__CLK_FSYS0_D1, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys0_ns_brdg_fsys0_ipclkport_clk__psoc_fsys0__clk_fsys0_p",
	     "dout_fsys0_peribus_grp",
	     GAT_FSYS0_NS_BRDG_FSYS0_IPCLKPORT_CLK__PSOC_FSYS0__CLK_FSYS0_P, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys0_ns_brdg_fsys0_ipclkport_clk__psoc_fsys0__clk_fsys0_s",
	     "mout_fsys0_clk_fsys0_slavebusclk",
	     GAT_FSYS0_NS_BRDG_FSYS0_IPCLKPORT_CLK__PSOC_FSYS0__CLK_FSYS0_S, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys0_pcie_top_ipclkport_pcieg3_phy_x4_inst_0_i_apb_pclk",
	     "dout_fsys0_peribus_grp",
	     GAT_FSYS0_PCIE_TOP_IPCLKPORT_PCIEG3_PHY_X4_INST_0_I_APB_PCLK, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(0,
	     "fsys0_pcie_top_ipclkport_pcieg3_phy_x4_inst_0_pll_refclk_from_syspll",
	     "dout_fsys0_pcie_phy_oscclk",
	     GAT_FSYS0_PCIE_TOP_IPCLKPORT_PCIEG3_PHY_X4_INST_0_PLL_REFCLK_FROM_SYSPLL,
	     21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys0_pcie_top_ipclkport_pipe_pal_inst_0_i_apb_pclk_0", "dout_fsys0_peribus_grp",
	     GAT_FSYS0_PCIE_TOP_IPCLKPORT_PIPE_PAL_INST_0_I_APB_PCLK_0, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys0_pcie_top_ipclkport_pipe_pal_inst_0_i_immortal_clk", "fin_pll",
	     GAT_FSYS0_PCIE_TOP_IPCLKPORT_PIPE_PAL_INST_0_I_IMMORTAL_CLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PCIE_SUBCTRL_INST0_DBI_ACLK_SOC,
	     "fsys0_pcie_top_ipclkport_fsd_pcie_sub_ctrl_inst_0_dbi_aclk_soc",
	     "dout_fsys0_peribus_grp",
	     GAT_FSYS0_PCIE_TOP_IPCLKPORT_FSD_PCIE_SUB_CTRL_INST_0_DBI_ACLK_SOC, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys0_pcie_top_ipclkport_fsd_pcie_sub_ctrl_inst_0_i_driver_apb_clk",
	     "dout_fsys0_peribus_grp",
	     GAT_FSYS0_PCIE_TOP_IPCLKPORT_FSD_PCIE_SUB_CTRL_INST_0_I_DRIVER_APB_CLK, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(PCIE_SUBCTRL_INST0_MSTR_ACLK_SOC,
	     "fsys0_pcie_top_ipclkport_fsd_pcie_sub_ctrl_inst_0_mstr_aclk_soc",
	     "mout_fsys0_clk_fsys0_slavebusclk",
	     GAT_FSYS0_PCIE_TOP_IPCLKPORT_FSD_PCIE_SUB_CTRL_INST_0_MSTR_ACLK_SOC, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(PCIE_SUBCTRL_INST0_SLV_ACLK_SOC,
	     "fsys0_pcie_top_ipclkport_fsd_pcie_sub_ctrl_inst_0_slv_aclk_soc",
	     "mout_fsys0_clk_fsys0_slavebusclk",
	     GAT_FSYS0_PCIE_TOP_IPCLKPORT_FSD_PCIE_SUB_CTRL_INST_0_SLV_ACLK_SOC, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys0_smmu_fsys0_ipclkport_cclk", "mout_fsys0_eqos_rgmii_125_mux1",
	     GAT_FSYS0_SMMU_FSYS0_IPCLKPORT_CCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys0_smmu_fsys0_ipclkport_fsys0_bclk", "mout_fsys0_clk_fsys0_slavebusclk",
	     GAT_FSYS0_SMMU_FSYS0_IPCLKPORT_FSYS0_BCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys0_sysreg_fsys0_ipclkport_pclk", "dout_fsys0_peribus_grp",
	     GAT_FSYS0_SYSREG_FSYS0_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(UFS0_TOP0_HCLK_BUS, "fsys0_ufs_top0_ipclkport_hclk_bus", "dout_fsys0_peribus_grp",
	     GAT_FSYS0_UFS_TOP0_IPCLKPORT_HCLK_BUS, 21, CLK_IGNORE_UNUSED, 0),
	GATE(UFS0_TOP0_ACLK, "fsys0_ufs_top0_ipclkport_i_aclk", "dout_fsys0_peribus_grp",
	     GAT_FSYS0_UFS_TOP0_IPCLKPORT_I_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(UFS0_TOP0_CLK_UNIPRO, "fsys0_ufs_top0_ipclkport_i_clk_unipro", "dout_fsys0_clk_unipro",
	     GAT_FSYS0_UFS_TOP0_IPCLKPORT_I_CLK_UNIPRO, 21, CLK_IGNORE_UNUSED, 0),
	GATE(UFS0_TOP0_FMP_CLK, "fsys0_ufs_top0_ipclkport_i_fmp_clk", "dout_fsys0_peribus_grp",
	     GAT_FSYS0_UFS_TOP0_IPCLKPORT_I_FMP_CLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(UFS1_TOP1_HCLK_BUS, "fsys0_ufs_top1_ipclkport_hclk_bus", "dout_fsys0_peribus_grp",
	     GAT_FSYS0_UFS_TOP1_IPCLKPORT_HCLK_BUS, 21, CLK_IGNORE_UNUSED, 0),
	GATE(UFS1_TOP1_ACLK, "fsys0_ufs_top1_ipclkport_i_aclk", "dout_fsys0_peribus_grp",
	     GAT_FSYS0_UFS_TOP1_IPCLKPORT_I_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(UFS1_TOP1_CLK_UNIPRO, "fsys0_ufs_top1_ipclkport_i_clk_unipro", "dout_fsys0_clk_unipro",
	     GAT_FSYS0_UFS_TOP1_IPCLKPORT_I_CLK_UNIPRO, 21, CLK_IGNORE_UNUSED, 0),
	GATE(UFS1_TOP1_FMP_CLK, "fsys0_ufs_top1_ipclkport_i_fmp_clk", "dout_fsys0_peribus_grp",
	     GAT_FSYS0_UFS_TOP1_IPCLKPORT_I_FMP_CLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys0_rii_clk_divgate", "dout_fsys0_eqos_rmiiclk_25", GAT_FSYS0_RII_CLK_DIVGATE,
	     21, CLK_IGNORE_UNUSED, 0),
	GATE(FSYS0_EQOS_TOP0_IPCLKPORT_CLK_PTP_REF_I, "fsys0_eqos_top0_ipclkport_clk_ptp_ref_i",
	     "fin_pll", GAT_FSYS0_EQOS_TOP0_IPCLKPORT_CLK_PTP_REF_I, 21, CLK_IGNORE_UNUSED, 0),
};

static const struct samsung_cmu_info fsys0_cmu_info __initconst = {
	.mux_clks		= fsys0_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(fsys0_mux_clks),
	.div_clks		= fsys0_div_clks,
	.nr_div_clks		= ARRAY_SIZE(fsys0_div_clks),
	.gate_clks		= fsys0_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(fsys0_gate_clks),
	.fixed_clks		= fsys0_fixed_clks,
	.nr_fixed_clks		= ARRAY_SIZE(fsys0_fixed_clks),
	.nr_clk_ids		= CLKS_NR_FSYS0,
	.clk_regs		= fsys0_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(fsys0_clk_regs),
	.clk_name		= "dout_cmu_fsys0_shared1div4",
};

/* Register Offset definitions for CMU_FSYS1 (0x16810000) */
#define PLL_CON0_ACLK_FSYS1_BUSP_MUX			0x100
#define PLL_CON0_PCLKL_FSYS1_BUSP_MUX			0x180
#define DIV_CLK_FSYS1_PHY0_OSCCLK			0x1800
#define DIV_CLK_FSYS1_PHY1_OSCCLK			0x1804
#define GAT_FSYS1_CMU_FSYS1_IPCLKPORT_PCLK	0x2000
#define GAT_FSYS1_PCIE_LINK0_IPCLKPORT_AUXCLK		0x2004
#define GAT_FSYS1_PCIE_LINK0_IPCLKPORT_I_SOC_REF_CLK	0x2008
#define GAT_FSYS1_PCIE_LINK1_IPCLKPORT_AUXCLK		0x200c
#define GAT_FSYS1_PCIE_PHY0_IPCLKPORT_I_REF_XTAL	0x202c
#define GAT_FSYS1_PHY0_OSCCLLK				0x2034
#define GAT_FSYS1_PHY1_OSCCLK				0x2038
#define GAT_FSYS1_AXI2APB_FSYS1_IPCLKPORT_ACLK		0x203c
#define GAT_FSYS1_BUS_D0_FSYS1_IPCLKPORT_MAINCLK	0x2040
#define GAT_FSYS1_BUS_S0_FSYS1_IPCLKPORT_M250CLK	0x2048
#define GAT_FSYS1_BUS_S0_FSYS1_IPCLKPORT_MAINCLK	0x204c
#define GAT_FSYS1_CPE425_0_FSYS1_IPCLKPORT_ACLK		0x2054
#define GAT_FSYS1_NS_BRDG_FSYS1_IPCLKPORT_CLK__PSOC_FSYS1__CLK_FSYS1_D0	0x205c
#define GAT_FSYS1_NS_BRDG_FSYS1_IPCLKPORT_CLK__PSOC_FSYS1__CLK_FSYS1_S0	0x2064
#define GAT_FSYS1_PCIE_LINK0_IPCLKPORT_DBI_ACLK		0x206c
#define GAT_FSYS1_PCIE_LINK0_IPCLKPORT_I_APB_CLK	0x2070
#define GAT_FSYS1_PCIE_LINK0_IPCLKPORT_I_DRIVER_APB_CLK	0x2074
#define GAT_FSYS1_PCIE_LINK0_IPCLKPORT_MSTR_ACLK	0x2078
#define GAT_FSYS1_PCIE_LINK0_IPCLKPORT_SLV_ACLK		0x207c
#define GAT_FSYS1_PCIE_LINK1_IPCLKPORT_DBI_ACLK		0x2080
#define GAT_FSYS1_PCIE_LINK1_IPCLKPORT_I_DRIVER_APB_CLK	0x2084
#define GAT_FSYS1_PCIE_LINK1_IPCLKPORT_MSTR_ACLK	0x2088
#define GAT_FSYS1_PCIE_LINK1_IPCLKPORT_SLV_ACLK		0x208c
#define GAT_FSYS1_PCIE_PHY0_IPCLKPORT_I_APB_CLK		0x20a4
#define GAT_FSYS1_PCIE_PHY0_IPCLKPORT_I_REF_SOC_PLL	0x20a8
#define GAT_FSYS1_SYSREG_FSYS1_IPCLKPORT_PCLK		0x20b4
#define GAT_FSYS1_TBU0_FSYS1_IPCLKPORT_ACLK		0x20b8

static const unsigned long fsys1_clk_regs[] __initconst = {
	PLL_CON0_ACLK_FSYS1_BUSP_MUX,
	PLL_CON0_PCLKL_FSYS1_BUSP_MUX,
	DIV_CLK_FSYS1_PHY0_OSCCLK,
	DIV_CLK_FSYS1_PHY1_OSCCLK,
	GAT_FSYS1_CMU_FSYS1_IPCLKPORT_PCLK,
	GAT_FSYS1_PCIE_LINK0_IPCLKPORT_AUXCLK,
	GAT_FSYS1_PCIE_LINK0_IPCLKPORT_I_SOC_REF_CLK,
	GAT_FSYS1_PCIE_LINK1_IPCLKPORT_AUXCLK,
	GAT_FSYS1_PCIE_PHY0_IPCLKPORT_I_REF_XTAL,
	GAT_FSYS1_PHY0_OSCCLLK,
	GAT_FSYS1_PHY1_OSCCLK,
	GAT_FSYS1_AXI2APB_FSYS1_IPCLKPORT_ACLK,
	GAT_FSYS1_BUS_D0_FSYS1_IPCLKPORT_MAINCLK,
	GAT_FSYS1_BUS_S0_FSYS1_IPCLKPORT_M250CLK,
	GAT_FSYS1_BUS_S0_FSYS1_IPCLKPORT_MAINCLK,
	GAT_FSYS1_CPE425_0_FSYS1_IPCLKPORT_ACLK,
	GAT_FSYS1_NS_BRDG_FSYS1_IPCLKPORT_CLK__PSOC_FSYS1__CLK_FSYS1_D0,
	GAT_FSYS1_NS_BRDG_FSYS1_IPCLKPORT_CLK__PSOC_FSYS1__CLK_FSYS1_S0,
	GAT_FSYS1_PCIE_LINK0_IPCLKPORT_DBI_ACLK,
	GAT_FSYS1_PCIE_LINK0_IPCLKPORT_I_APB_CLK,
	GAT_FSYS1_PCIE_LINK0_IPCLKPORT_I_DRIVER_APB_CLK,
	GAT_FSYS1_PCIE_LINK0_IPCLKPORT_MSTR_ACLK,
	GAT_FSYS1_PCIE_LINK0_IPCLKPORT_SLV_ACLK,
	GAT_FSYS1_PCIE_LINK1_IPCLKPORT_DBI_ACLK,
	GAT_FSYS1_PCIE_LINK1_IPCLKPORT_I_DRIVER_APB_CLK,
	GAT_FSYS1_PCIE_LINK1_IPCLKPORT_MSTR_ACLK,
	GAT_FSYS1_PCIE_LINK1_IPCLKPORT_SLV_ACLK,
	GAT_FSYS1_PCIE_PHY0_IPCLKPORT_I_APB_CLK,
	GAT_FSYS1_PCIE_PHY0_IPCLKPORT_I_REF_SOC_PLL,
	GAT_FSYS1_SYSREG_FSYS1_IPCLKPORT_PCLK,
	GAT_FSYS1_TBU0_FSYS1_IPCLKPORT_ACLK,
};

static const struct samsung_fixed_rate_clock fsys1_fixed_clks[] __initconst = {
	FRATE(0, "clk_fsys1_phy0_ref", NULL, 0, 100000000),
	FRATE(0, "clk_fsys1_phy1_ref", NULL, 0, 100000000),
};

/* List of parent clocks for Muxes in CMU_FSYS1 */
PNAME(mout_fsys1_pclkl_fsys1_busp_mux_p) = { "fin_pll", "dout_cmu_fsys1_shared0div8" };
PNAME(mout_fsys1_aclk_fsys1_busp_mux_p) = { "fin_pll", "dout_cmu_fsys1_shared0div4" };

static const struct samsung_mux_clock fsys1_mux_clks[] __initconst = {
	MUX(0, "mout_fsys1_pclkl_fsys1_busp_mux", mout_fsys1_pclkl_fsys1_busp_mux_p,
	    PLL_CON0_PCLKL_FSYS1_BUSP_MUX, 4, 1),
	MUX(0, "mout_fsys1_aclk_fsys1_busp_mux", mout_fsys1_aclk_fsys1_busp_mux_p,
	    PLL_CON0_ACLK_FSYS1_BUSP_MUX, 4, 1),
};

static const struct samsung_div_clock fsys1_div_clks[] __initconst = {
	DIV(0, "dout_fsys1_clk_fsys1_phy0_oscclk", "fsys1_phy0_osccllk",
	    DIV_CLK_FSYS1_PHY0_OSCCLK, 0, 4),
	DIV(0, "dout_fsys1_clk_fsys1_phy1_oscclk", "fsys1_phy1_oscclk",
	    DIV_CLK_FSYS1_PHY1_OSCCLK, 0, 4),
};

static const struct samsung_gate_clock fsys1_gate_clks[] __initconst = {
	GATE(0, "fsys1_cmu_fsys1_ipclkport_pclk", "mout_fsys1_pclkl_fsys1_busp_mux",
	     GAT_FSYS1_CMU_FSYS1_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys1_pcie_phy0_ipclkport_i_ref_xtal", "clk_fsys1_phy0_ref",
	     GAT_FSYS1_PCIE_PHY0_IPCLKPORT_I_REF_XTAL, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys1_phy0_osccllk", "mout_fsys1_aclk_fsys1_busp_mux",
	     GAT_FSYS1_PHY0_OSCCLLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys1_phy1_oscclk", "mout_fsys1_aclk_fsys1_busp_mux",
	     GAT_FSYS1_PHY1_OSCCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys1_axi2apb_fsys1_ipclkport_aclk", "mout_fsys1_pclkl_fsys1_busp_mux",
	     GAT_FSYS1_AXI2APB_FSYS1_IPCLKPORT_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys1_bus_d0_fsys1_ipclkport_mainclk", "mout_fsys1_aclk_fsys1_busp_mux",
	     GAT_FSYS1_BUS_D0_FSYS1_IPCLKPORT_MAINCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys1_bus_s0_fsys1_ipclkport_m250clk", "mout_fsys1_pclkl_fsys1_busp_mux",
	     GAT_FSYS1_BUS_S0_FSYS1_IPCLKPORT_M250CLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys1_bus_s0_fsys1_ipclkport_mainclk", "mout_fsys1_aclk_fsys1_busp_mux",
	     GAT_FSYS1_BUS_S0_FSYS1_IPCLKPORT_MAINCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys1_cpe425_0_fsys1_ipclkport_aclk", "mout_fsys1_aclk_fsys1_busp_mux",
	     GAT_FSYS1_CPE425_0_FSYS1_IPCLKPORT_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys1_ns_brdg_fsys1_ipclkport_clk__psoc_fsys1__clk_fsys1_d0",
	     "mout_fsys1_aclk_fsys1_busp_mux",
	     GAT_FSYS1_NS_BRDG_FSYS1_IPCLKPORT_CLK__PSOC_FSYS1__CLK_FSYS1_D0, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys1_ns_brdg_fsys1_ipclkport_clk__psoc_fsys1__clk_fsys1_s0",
	     "mout_fsys1_aclk_fsys1_busp_mux",
	     GAT_FSYS1_NS_BRDG_FSYS1_IPCLKPORT_CLK__PSOC_FSYS1__CLK_FSYS1_S0, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(PCIE_LINK0_IPCLKPORT_DBI_ACLK, "fsys1_pcie_link0_ipclkport_dbi_aclk",
	     "mout_fsys1_aclk_fsys1_busp_mux", GAT_FSYS1_PCIE_LINK0_IPCLKPORT_DBI_ACLK, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys1_pcie_link0_ipclkport_i_apb_clk", "mout_fsys1_pclkl_fsys1_busp_mux",
	     GAT_FSYS1_PCIE_LINK0_IPCLKPORT_I_APB_CLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys1_pcie_link0_ipclkport_i_soc_ref_clk", "fin_pll",
	     GAT_FSYS1_PCIE_LINK0_IPCLKPORT_I_SOC_REF_CLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys1_pcie_link0_ipclkport_i_driver_apb_clk", "mout_fsys1_pclkl_fsys1_busp_mux",
	     GAT_FSYS1_PCIE_LINK0_IPCLKPORT_I_DRIVER_APB_CLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PCIE_LINK0_IPCLKPORT_MSTR_ACLK, "fsys1_pcie_link0_ipclkport_mstr_aclk",
	     "mout_fsys1_aclk_fsys1_busp_mux", GAT_FSYS1_PCIE_LINK0_IPCLKPORT_MSTR_ACLK, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(PCIE_LINK0_IPCLKPORT_SLV_ACLK, "fsys1_pcie_link0_ipclkport_slv_aclk",
	     "mout_fsys1_aclk_fsys1_busp_mux", GAT_FSYS1_PCIE_LINK0_IPCLKPORT_SLV_ACLK, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(PCIE_LINK1_IPCLKPORT_DBI_ACLK, "fsys1_pcie_link1_ipclkport_dbi_aclk",
	     "mout_fsys1_aclk_fsys1_busp_mux", GAT_FSYS1_PCIE_LINK1_IPCLKPORT_DBI_ACLK, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys1_pcie_link1_ipclkport_i_driver_apb_clk", "mout_fsys1_pclkl_fsys1_busp_mux",
	     GAT_FSYS1_PCIE_LINK1_IPCLKPORT_I_DRIVER_APB_CLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PCIE_LINK1_IPCLKPORT_MSTR_ACLK, "fsys1_pcie_link1_ipclkport_mstr_aclk",
	     "mout_fsys1_aclk_fsys1_busp_mux", GAT_FSYS1_PCIE_LINK1_IPCLKPORT_MSTR_ACLK, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(PCIE_LINK1_IPCLKPORT_SLV_ACLK, "fsys1_pcie_link1_ipclkport_slv_aclk",
	     "mout_fsys1_aclk_fsys1_busp_mux", GAT_FSYS1_PCIE_LINK1_IPCLKPORT_SLV_ACLK, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys1_pcie_phy0_ipclkport_i_apb_clk", "mout_fsys1_pclkl_fsys1_busp_mux",
	     GAT_FSYS1_PCIE_PHY0_IPCLKPORT_I_APB_CLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PCIE_LINK0_IPCLKPORT_AUX_ACLK, "fsys1_pcie_link0_ipclkport_auxclk", "fin_pll",
	     GAT_FSYS1_PCIE_LINK0_IPCLKPORT_AUXCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(PCIE_LINK1_IPCLKPORT_AUX_ACLK, "fsys1_pcie_link1_ipclkport_auxclk", "fin_pll",
	     GAT_FSYS1_PCIE_LINK1_IPCLKPORT_AUXCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys1_pcie_phy0_ipclkport_i_ref_soc_pll", "dout_fsys1_clk_fsys1_phy0_oscclk",
	     GAT_FSYS1_PCIE_PHY0_IPCLKPORT_I_REF_SOC_PLL, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys1_sysreg_fsys1_ipclkport_pclk", "mout_fsys1_pclkl_fsys1_busp_mux",
	     GAT_FSYS1_SYSREG_FSYS1_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "fsys1_tbu0_fsys1_ipclkport_aclk", "mout_fsys1_aclk_fsys1_busp_mux",
	     GAT_FSYS1_TBU0_FSYS1_IPCLKPORT_ACLK, 21, CLK_IGNORE_UNUSED, 0),
};

static const struct samsung_cmu_info fsys1_cmu_info __initconst = {
	.mux_clks		= fsys1_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(fsys1_mux_clks),
	.div_clks		= fsys1_div_clks,
	.nr_div_clks		= ARRAY_SIZE(fsys1_div_clks),
	.gate_clks		= fsys1_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(fsys1_gate_clks),
	.fixed_clks		= fsys1_fixed_clks,
	.nr_fixed_clks		= ARRAY_SIZE(fsys1_fixed_clks),
	.nr_clk_ids		= CLKS_NR_FSYS1,
	.clk_regs		= fsys1_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(fsys1_clk_regs),
	.clk_name		= "dout_cmu_fsys1_shared0div4",
};

/* Register Offset definitions for CMU_IMEM (0x10010000) */
#define PLL_CON0_CLK_IMEM_ACLK				0x100
#define PLL_CON0_CLK_IMEM_INTMEMCLK			0x120
#define PLL_CON0_CLK_IMEM_TCUCLK			0x140
#define DIV_OSCCLK_IMEM_TMUTSCLK			0x1800
#define GAT_IMEM_IMEM_CMU_IMEM_IPCLKPORT_PCLK		0x2000
#define GAT_IMEM_MCT_IPCLKPORT_OSCCLK__ALO		0x2004
#define GAT_IMEM_OTP_CON_TOP_IPCLKPORT_I_OSCCLK		0x2008
#define GAT_IMEM_RSTNSYNC_OSCCLK_IPCLKPORT_CLK		0x200c
#define GAT_IMEM_TMU_CPU0_IPCLKPORT_I_CLK		0x2010
#define GAT_IMEM_TMU_CPU0_IPCLKPORT_I_CLK_TS		0x2014
#define GAT_IMEM_TMU_CPU2_IPCLKPORT_I_CLK		0x2018
#define GAT_IMEM_TMU_CPU2_IPCLKPORT_I_CLK_TS		0x201c
#define GAT_IMEM_TMU_GPU_IPCLKPORT_I_CLK		0x2020
#define GAT_IMEM_TMU_GPU_IPCLKPORT_I_CLK_TS		0x2024
#define GAT_IMEM_TMU_GT_IPCLKPORT_I_CLK			0x2028
#define GAT_IMEM_TMU_GT_IPCLKPORT_I_CLK_TS		0x202c
#define GAT_IMEM_TMU_TOP_IPCLKPORT_I_CLK		0x2030
#define GAT_IMEM_TMU_TOP_IPCLKPORT_I_CLK_TS		0x2034
#define GAT_IMEM_WDT0_IPCLKPORT_CLK			0x2038
#define GAT_IMEM_WDT1_IPCLKPORT_CLK			0x203c
#define GAT_IMEM_WDT2_IPCLKPORT_CLK			0x2040
#define GAT_IMEM_ADM_AXI4ST_I0_IMEM_IPCLKPORT_ACLKM	0x2044
#define GAT_IMEM_ADM_AXI4ST_I1_IMEM_IPCLKPORT_ACLKM	0x2048
#define GAT_IMEM_ADM_AXI4ST_I2_IMEM_IPCLKPORT_ACLKM	0x204c
#define GAT_IMEM_ADS_AXI4ST_I0_IMEM_IPCLKPORT_ACLKS	0x2050
#define GAT_IMEM_ADS_AXI4ST_I1_IMEM_IPCLKPORT_ACLKS	0x2054
#define GAT_IMEM_ADS_AXI4ST_I2_IMEM_IPCLKPORT_ACLKS	0x2058
#define GAT_IMEM_ASYNC_DMA0_IPCLKPORT_PCLKM		0x205c
#define GAT_IMEM_ASYNC_DMA0_IPCLKPORT_PCLKS		0x2060
#define GAT_IMEM_ASYNC_DMA1_IPCLKPORT_PCLKM		0x2064
#define GAT_IMEM_ASYNC_DMA1_IPCLKPORT_PCLKS		0x2068
#define GAT_IMEM_AXI2APB_IMEMP0_IPCLKPORT_ACLK		0x206c
#define GAT_IMEM_AXI2APB_IMEMP1_IPCLKPORT_ACLK		0x2070
#define GAT_IMEM_BUS_D_IMEM_IPCLKPORT_MAINCLK		0x2074
#define GAT_IMEM_BUS_P_IMEM_IPCLKPORT_MAINCLK		0x2078
#define GAT_IMEM_BUS_P_IMEM_IPCLKPORT_PERICLK		0x207c
#define GAT_IMEM_BUS_P_IMEM_IPCLKPORT_TCUCLK		0x2080
#define GAT_IMEM_DMA0_IPCLKPORT_ACLK			0x2084
#define GAT_IMEM_DMA1_IPCLKPORT_ACLK			0x2088
#define GAT_IMEM_GIC500_INPUT_SYNC_IPCLKPORT_CLK	0x208c
#define GAT_IMEM_GIC_IPCLKPORT_CLK			0x2090
#define GAT_IMEM_INTMEM_IPCLKPORT_ACLK			0x2094
#define GAT_IMEM_MAILBOX_SCS_CA72_IPCLKPORT_PCLK	0x2098
#define GAT_IMEM_MAILBOX_SMS_CA72_IPCLKPORT_PCLK	0x209c
#define GAT_IMEM_MCT_IPCLKPORT_PCLK			0x20a0
#define GAT_IMEM_NS_BRDG_IMEM_IPCLKPORT_CLK__PSCO_IMEM__CLK_IMEM_D	0x20a4
#define GAT_IMEM_NS_BRDG_IMEM_IPCLKPORT_CLK__PSCO_IMEM__CLK_IMEM_TCU	0x20a8
#define GAT_IMEM_NS_BRDG_IMEM_IPCLKPORT_CLK__PSOC_IMEM__CLK_IMEM_P	0x20ac
#define GAT_IMEM_OTP_CON_TOP_IPCLKPORT_PCLK		0x20b0
#define GAT_IMEM_RSTNSYNC_ACLK_IPCLKPORT_CLK		0x20b4
#define GAT_IMEM_RSTNSYNC_INTMEMCLK_IPCLKPORT_CLK	0x20b8
#define GAT_IMEM_RSTNSYNC_TCUCLK_IPCLKPORT_CLK		0x20bc
#define GAT_IMEM_SFRIF_TMU0_IMEM_IPCLKPORT_PCLK		0x20c0
#define GAT_IMEM_SFRIF_TMU1_IMEM_IPCLKPORT_PCLK		0x20c4
#define GAT_IMEM_SYSREG_IMEM_IPCLKPORT_PCLK		0x20c8
#define GAT_IMEM_TBU_IMEM_IPCLKPORT_ACLK		0x20cc
#define GAT_IMEM_TCU_IPCLKPORT_ACLK			0x20d0
#define GAT_IMEM_WDT0_IPCLKPORT_PCLK			0x20d4
#define GAT_IMEM_WDT1_IPCLKPORT_PCLK			0x20d8
#define GAT_IMEM_WDT2_IPCLKPORT_PCLK			0x20dc

static const unsigned long imem_clk_regs[] __initconst = {
	PLL_CON0_CLK_IMEM_ACLK,
	PLL_CON0_CLK_IMEM_INTMEMCLK,
	PLL_CON0_CLK_IMEM_TCUCLK,
	DIV_OSCCLK_IMEM_TMUTSCLK,
	GAT_IMEM_IMEM_CMU_IMEM_IPCLKPORT_PCLK,
	GAT_IMEM_MCT_IPCLKPORT_OSCCLK__ALO,
	GAT_IMEM_OTP_CON_TOP_IPCLKPORT_I_OSCCLK,
	GAT_IMEM_RSTNSYNC_OSCCLK_IPCLKPORT_CLK,
	GAT_IMEM_TMU_CPU0_IPCLKPORT_I_CLK,
	GAT_IMEM_TMU_CPU0_IPCLKPORT_I_CLK_TS,
	GAT_IMEM_TMU_CPU2_IPCLKPORT_I_CLK,
	GAT_IMEM_TMU_CPU2_IPCLKPORT_I_CLK_TS,
	GAT_IMEM_TMU_GPU_IPCLKPORT_I_CLK,
	GAT_IMEM_TMU_GPU_IPCLKPORT_I_CLK_TS,
	GAT_IMEM_TMU_GT_IPCLKPORT_I_CLK,
	GAT_IMEM_TMU_GT_IPCLKPORT_I_CLK_TS,
	GAT_IMEM_TMU_TOP_IPCLKPORT_I_CLK,
	GAT_IMEM_TMU_TOP_IPCLKPORT_I_CLK_TS,
	GAT_IMEM_WDT0_IPCLKPORT_CLK,
	GAT_IMEM_WDT1_IPCLKPORT_CLK,
	GAT_IMEM_WDT2_IPCLKPORT_CLK,
	GAT_IMEM_ADM_AXI4ST_I0_IMEM_IPCLKPORT_ACLKM,
	GAT_IMEM_ADM_AXI4ST_I1_IMEM_IPCLKPORT_ACLKM,
	GAT_IMEM_ADM_AXI4ST_I2_IMEM_IPCLKPORT_ACLKM,
	GAT_IMEM_ADS_AXI4ST_I0_IMEM_IPCLKPORT_ACLKS,
	GAT_IMEM_ADS_AXI4ST_I1_IMEM_IPCLKPORT_ACLKS,
	GAT_IMEM_ADS_AXI4ST_I2_IMEM_IPCLKPORT_ACLKS,
	GAT_IMEM_ASYNC_DMA0_IPCLKPORT_PCLKM,
	GAT_IMEM_ASYNC_DMA0_IPCLKPORT_PCLKS,
	GAT_IMEM_ASYNC_DMA1_IPCLKPORT_PCLKM,
	GAT_IMEM_ASYNC_DMA1_IPCLKPORT_PCLKS,
	GAT_IMEM_AXI2APB_IMEMP0_IPCLKPORT_ACLK,
	GAT_IMEM_AXI2APB_IMEMP1_IPCLKPORT_ACLK,
	GAT_IMEM_BUS_D_IMEM_IPCLKPORT_MAINCLK,
	GAT_IMEM_BUS_P_IMEM_IPCLKPORT_MAINCLK,
	GAT_IMEM_BUS_P_IMEM_IPCLKPORT_PERICLK,
	GAT_IMEM_BUS_P_IMEM_IPCLKPORT_TCUCLK,
	GAT_IMEM_DMA0_IPCLKPORT_ACLK,
	GAT_IMEM_DMA1_IPCLKPORT_ACLK,
	GAT_IMEM_GIC500_INPUT_SYNC_IPCLKPORT_CLK,
	GAT_IMEM_GIC_IPCLKPORT_CLK,
	GAT_IMEM_INTMEM_IPCLKPORT_ACLK,
	GAT_IMEM_MAILBOX_SCS_CA72_IPCLKPORT_PCLK,
	GAT_IMEM_MAILBOX_SMS_CA72_IPCLKPORT_PCLK,
	GAT_IMEM_MCT_IPCLKPORT_PCLK,
	GAT_IMEM_NS_BRDG_IMEM_IPCLKPORT_CLK__PSCO_IMEM__CLK_IMEM_D,
	GAT_IMEM_NS_BRDG_IMEM_IPCLKPORT_CLK__PSCO_IMEM__CLK_IMEM_TCU,
	GAT_IMEM_NS_BRDG_IMEM_IPCLKPORT_CLK__PSOC_IMEM__CLK_IMEM_P,
	GAT_IMEM_OTP_CON_TOP_IPCLKPORT_PCLK,
	GAT_IMEM_RSTNSYNC_ACLK_IPCLKPORT_CLK,
	GAT_IMEM_RSTNSYNC_INTMEMCLK_IPCLKPORT_CLK,
	GAT_IMEM_RSTNSYNC_TCUCLK_IPCLKPORT_CLK,
	GAT_IMEM_SFRIF_TMU0_IMEM_IPCLKPORT_PCLK,
	GAT_IMEM_SFRIF_TMU1_IMEM_IPCLKPORT_PCLK,
	GAT_IMEM_SYSREG_IMEM_IPCLKPORT_PCLK,
	GAT_IMEM_TBU_IMEM_IPCLKPORT_ACLK,
	GAT_IMEM_TCU_IPCLKPORT_ACLK,
	GAT_IMEM_WDT0_IPCLKPORT_PCLK,
	GAT_IMEM_WDT1_IPCLKPORT_PCLK,
	GAT_IMEM_WDT2_IPCLKPORT_PCLK,
};

PNAME(mout_imem_clk_imem_tcuclk_p) = { "fin_pll", "dout_cmu_imem_tcuclk" };
PNAME(mout_imem_clk_imem_aclk_p) = { "fin_pll", "dout_cmu_imem_aclk" };
PNAME(mout_imem_clk_imem_intmemclk_p) = { "fin_pll", "dout_cmu_imem_dmaclk" };

static const struct samsung_mux_clock imem_mux_clks[] __initconst = {
	MUX(0, "mout_imem_clk_imem_tcuclk", mout_imem_clk_imem_tcuclk_p,
	    PLL_CON0_CLK_IMEM_TCUCLK, 4, 1),
	MUX(0, "mout_imem_clk_imem_aclk", mout_imem_clk_imem_aclk_p, PLL_CON0_CLK_IMEM_ACLK, 4, 1),
	MUX(0, "mout_imem_clk_imem_intmemclk", mout_imem_clk_imem_intmemclk_p,
	    PLL_CON0_CLK_IMEM_INTMEMCLK, 4, 1),
};

static const struct samsung_div_clock imem_div_clks[] __initconst = {
	DIV(0, "dout_imem_oscclk_imem_tmutsclk", "fin_pll", DIV_OSCCLK_IMEM_TMUTSCLK, 0, 4),
};

static const struct samsung_gate_clock imem_gate_clks[] __initconst = {
	GATE(0, "imem_imem_cmu_imem_ipclkport_pclk", "mout_imem_clk_imem_aclk",
	     GAT_IMEM_IMEM_CMU_IMEM_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_otp_con_top_ipclkport_i_oscclk", "fin_pll",
	     GAT_IMEM_OTP_CON_TOP_IPCLKPORT_I_OSCCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_tmu_top_ipclkport_i_clk", "fin_pll",
	     GAT_IMEM_TMU_TOP_IPCLKPORT_I_CLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_tmu_gt_ipclkport_i_clk", "fin_pll",
	     GAT_IMEM_TMU_GT_IPCLKPORT_I_CLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_tmu_cpu0_ipclkport_i_clk", "fin_pll",
	     GAT_IMEM_TMU_CPU0_IPCLKPORT_I_CLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_tmu_gpu_ipclkport_i_clk", "fin_pll",
	     GAT_IMEM_TMU_GPU_IPCLKPORT_I_CLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_mct_ipclkport_oscclk__alo", "fin_pll",
	     GAT_IMEM_MCT_IPCLKPORT_OSCCLK__ALO, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_wdt0_ipclkport_clk", "fin_pll",
	     GAT_IMEM_WDT0_IPCLKPORT_CLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_wdt1_ipclkport_clk", "fin_pll",
	     GAT_IMEM_WDT1_IPCLKPORT_CLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_wdt2_ipclkport_clk", "fin_pll",
	     GAT_IMEM_WDT2_IPCLKPORT_CLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(IMEM_TMU_CPU0_IPCLKPORT_I_CLK_TS, "imem_tmu_cpu0_ipclkport_i_clk_ts",
	     "dout_imem_oscclk_imem_tmutsclk",
	     GAT_IMEM_TMU_CPU0_IPCLKPORT_I_CLK_TS, 21, CLK_IGNORE_UNUSED, 0),
	GATE(IMEM_TMU_CPU2_IPCLKPORT_I_CLK_TS, "imem_tmu_cpu2_ipclkport_i_clk_ts",
	     "dout_imem_oscclk_imem_tmutsclk",
	     GAT_IMEM_TMU_CPU2_IPCLKPORT_I_CLK_TS, 21, CLK_IGNORE_UNUSED, 0),
	GATE(IMEM_TMU_GPU_IPCLKPORT_I_CLK_TS, "imem_tmu_gpu_ipclkport_i_clk_ts",
	     "dout_imem_oscclk_imem_tmutsclk",
	     GAT_IMEM_TMU_GPU_IPCLKPORT_I_CLK_TS, 21, CLK_IGNORE_UNUSED, 0),
	GATE(IMEM_TMU_GT_IPCLKPORT_I_CLK_TS, "imem_tmu_gt_ipclkport_i_clk_ts",
	     "dout_imem_oscclk_imem_tmutsclk",
	     GAT_IMEM_TMU_GT_IPCLKPORT_I_CLK_TS, 21, CLK_IGNORE_UNUSED, 0),
	GATE(IMEM_TMU_TOP_IPCLKPORT_I_CLK_TS, "imem_tmu_top_ipclkport_i_clk_ts",
	     "dout_imem_oscclk_imem_tmutsclk",
	     GAT_IMEM_TMU_TOP_IPCLKPORT_I_CLK_TS, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_adm_axi4st_i0_imem_ipclkport_aclkm", "mout_imem_clk_imem_aclk",
	     GAT_IMEM_ADM_AXI4ST_I0_IMEM_IPCLKPORT_ACLKM, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_adm_axi4st_i1_imem_ipclkport_aclkm", "mout_imem_clk_imem_aclk",
	     GAT_IMEM_ADM_AXI4ST_I1_IMEM_IPCLKPORT_ACLKM, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_adm_axi4st_i2_imem_ipclkport_aclkm", "mout_imem_clk_imem_aclk",
	     GAT_IMEM_ADM_AXI4ST_I2_IMEM_IPCLKPORT_ACLKM, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_ads_axi4st_i0_imem_ipclkport_aclks", "mout_imem_clk_imem_aclk",
	     GAT_IMEM_ADS_AXI4ST_I0_IMEM_IPCLKPORT_ACLKS, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_ads_axi4st_i1_imem_ipclkport_aclks", "mout_imem_clk_imem_aclk",
	     GAT_IMEM_ADS_AXI4ST_I1_IMEM_IPCLKPORT_ACLKS, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_ads_axi4st_i2_imem_ipclkport_aclks", "mout_imem_clk_imem_aclk",
	     GAT_IMEM_ADS_AXI4ST_I2_IMEM_IPCLKPORT_ACLKS, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_async_dma0_ipclkport_pclkm", "mout_imem_clk_imem_tcuclk",
	     GAT_IMEM_ASYNC_DMA0_IPCLKPORT_PCLKM, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_async_dma0_ipclkport_pclks", "mout_imem_clk_imem_aclk",
	     GAT_IMEM_ASYNC_DMA0_IPCLKPORT_PCLKS, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_async_dma1_ipclkport_pclkm", "mout_imem_clk_imem_tcuclk",
	     GAT_IMEM_ASYNC_DMA1_IPCLKPORT_PCLKM, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_async_dma1_ipclkport_pclks", "mout_imem_clk_imem_aclk",
	     GAT_IMEM_ASYNC_DMA1_IPCLKPORT_PCLKS, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_axi2apb_imemp0_ipclkport_aclk", "mout_imem_clk_imem_aclk",
	     GAT_IMEM_AXI2APB_IMEMP0_IPCLKPORT_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_axi2apb_imemp1_ipclkport_aclk", "mout_imem_clk_imem_aclk",
	     GAT_IMEM_AXI2APB_IMEMP1_IPCLKPORT_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_bus_d_imem_ipclkport_mainclk", "mout_imem_clk_imem_tcuclk",
	     GAT_IMEM_BUS_D_IMEM_IPCLKPORT_MAINCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_bus_p_imem_ipclkport_mainclk", "mout_imem_clk_imem_aclk",
	     GAT_IMEM_BUS_P_IMEM_IPCLKPORT_MAINCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_bus_p_imem_ipclkport_pericclk", "mout_imem_clk_imem_aclk",
	     GAT_IMEM_BUS_P_IMEM_IPCLKPORT_PERICLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_bus_p_imem_ipclkport_tcuclk", "mout_imem_clk_imem_tcuclk",
	     GAT_IMEM_BUS_P_IMEM_IPCLKPORT_TCUCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(IMEM_DMA0_IPCLKPORT_ACLK, "imem_dma0_ipclkport_aclk", "mout_imem_clk_imem_tcuclk",
	     GAT_IMEM_DMA0_IPCLKPORT_ACLK, 21, CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0),
	GATE(IMEM_DMA1_IPCLKPORT_ACLK, "imem_dma1_ipclkport_aclk", "mout_imem_clk_imem_tcuclk",
	     GAT_IMEM_DMA1_IPCLKPORT_ACLK, 21, CLK_IGNORE_UNUSED | CLK_IS_CRITICAL, 0),
	GATE(0, "imem_gic500_input_sync_ipclkport_clk", "mout_imem_clk_imem_aclk",
	     GAT_IMEM_GIC500_INPUT_SYNC_IPCLKPORT_CLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_gic_ipclkport_clk", "mout_imem_clk_imem_aclk",
	     GAT_IMEM_GIC_IPCLKPORT_CLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_intmem_ipclkport_aclk", "mout_imem_clk_imem_intmemclk",
	     GAT_IMEM_INTMEM_IPCLKPORT_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_mailbox_scs_ca72_ipclkport_pclk", "mout_imem_clk_imem_aclk",
	     GAT_IMEM_MAILBOX_SCS_CA72_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_mailbox_sms_ca72_ipclkport_pclk", "mout_imem_clk_imem_aclk",
	     GAT_IMEM_MAILBOX_SMS_CA72_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(IMEM_MCT_PCLK, "imem_mct_ipclkport_pclk", "mout_imem_clk_imem_aclk",
	     GAT_IMEM_MCT_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_ns_brdg_imem_ipclkport_clk__psco_imem__clk_imem_d",
	     "mout_imem_clk_imem_tcuclk",
	     GAT_IMEM_NS_BRDG_IMEM_IPCLKPORT_CLK__PSCO_IMEM__CLK_IMEM_D, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_ns_brdg_imem_ipclkport_clk__psco_imem__clk_imem_tcu",
	     "mout_imem_clk_imem_tcuclk",
	     GAT_IMEM_NS_BRDG_IMEM_IPCLKPORT_CLK__PSCO_IMEM__CLK_IMEM_TCU, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_ns_brdg_imem_ipclkport_clk__psoc_imem__clk_imem_p", "mout_imem_clk_imem_aclk",
	     GAT_IMEM_NS_BRDG_IMEM_IPCLKPORT_CLK__PSOC_IMEM__CLK_IMEM_P, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_otp_con_top_ipclkport_pclk", "mout_imem_clk_imem_aclk",
	     GAT_IMEM_OTP_CON_TOP_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_rstnsync_aclk_ipclkport_clk", "mout_imem_clk_imem_aclk",
	     GAT_IMEM_RSTNSYNC_ACLK_IPCLKPORT_CLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_rstnsync_oscclk_ipclkport_clk", "fin_pll",
	     GAT_IMEM_RSTNSYNC_OSCCLK_IPCLKPORT_CLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_rstnsync_intmemclk_ipclkport_clk", "mout_imem_clk_imem_intmemclk",
	     GAT_IMEM_RSTNSYNC_INTMEMCLK_IPCLKPORT_CLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_rstnsync_tcuclk_ipclkport_clk", "mout_imem_clk_imem_tcuclk",
	     GAT_IMEM_RSTNSYNC_TCUCLK_IPCLKPORT_CLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_sfrif_tmu0_imem_ipclkport_pclk", "mout_imem_clk_imem_aclk",
	     GAT_IMEM_SFRIF_TMU0_IMEM_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_sfrif_tmu1_imem_ipclkport_pclk", "mout_imem_clk_imem_aclk",
	     GAT_IMEM_SFRIF_TMU1_IMEM_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_tmu_cpu2_ipclkport_i_clk", "fin_pll",
	     GAT_IMEM_TMU_CPU2_IPCLKPORT_I_CLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_sysreg_imem_ipclkport_pclk", "mout_imem_clk_imem_aclk",
	     GAT_IMEM_SYSREG_IMEM_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_tbu_imem_ipclkport_aclk", "mout_imem_clk_imem_tcuclk",
	     GAT_IMEM_TBU_IMEM_IPCLKPORT_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "imem_tcu_ipclkport_aclk", "mout_imem_clk_imem_tcuclk",
	     GAT_IMEM_TCU_IPCLKPORT_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(IMEM_WDT0_IPCLKPORT_PCLK, "imem_wdt0_ipclkport_pclk", "mout_imem_clk_imem_aclk",
	     GAT_IMEM_WDT0_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(IMEM_WDT1_IPCLKPORT_PCLK, "imem_wdt1_ipclkport_pclk", "mout_imem_clk_imem_aclk",
	     GAT_IMEM_WDT1_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(IMEM_WDT2_IPCLKPORT_PCLK, "imem_wdt2_ipclkport_pclk", "mout_imem_clk_imem_aclk",
	     GAT_IMEM_WDT2_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
};

static const struct samsung_cmu_info imem_cmu_info __initconst = {
	.mux_clks		= imem_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(imem_mux_clks),
	.div_clks		= imem_div_clks,
	.nr_div_clks		= ARRAY_SIZE(imem_div_clks),
	.gate_clks		= imem_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(imem_gate_clks),
	.nr_clk_ids		= CLKS_NR_IMEM,
	.clk_regs		= imem_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(imem_clk_regs),
};

static void __init fsd_clk_imem_init(struct device_node *np)
{
	samsung_cmu_register_one(np, &imem_cmu_info);
}

CLK_OF_DECLARE(fsd_clk_imem, "tesla,fsd-clock-imem", fsd_clk_imem_init);

/* Register Offset definitions for CMU_MFC (0x12810000) */
#define PLL_LOCKTIME_PLL_MFC					0x0
#define PLL_CON0_PLL_MFC					0x100
#define MUX_MFC_BUSD						0x1000
#define MUX_MFC_BUSP						0x1008
#define DIV_MFC_BUSD_DIV4					0x1800
#define GAT_MFC_CMU_MFC_IPCLKPORT_PCLK				0x2000
#define GAT_MFC_AS_P_MFC_IPCLKPORT_PCLKM			0x2004
#define GAT_MFC_AS_P_MFC_IPCLKPORT_PCLKS			0x2008
#define GAT_MFC_AXI2APB_MFC_IPCLKPORT_ACLK			0x200c
#define GAT_MFC_MFC_IPCLKPORT_ACLK				0x2010
#define GAT_MFC_NS_BRDG_MFC_IPCLKPORT_CLK__PMFC__CLK_MFC_D	0x2018
#define GAT_MFC_NS_BRDG_MFC_IPCLKPORT_CLK__PMFC__CLK_MFC_P	0x201c
#define GAT_MFC_PPMU_MFCD0_IPCLKPORT_ACLK			0x2028
#define GAT_MFC_PPMU_MFCD0_IPCLKPORT_PCLK			0x202c
#define GAT_MFC_PPMU_MFCD1_IPCLKPORT_ACLK			0x2030
#define GAT_MFC_PPMU_MFCD1_IPCLKPORT_PCLK			0x2034
#define GAT_MFC_SYSREG_MFC_IPCLKPORT_PCLK			0x2038
#define GAT_MFC_TBU_MFCD0_IPCLKPORT_CLK				0x203c
#define GAT_MFC_TBU_MFCD1_IPCLKPORT_CLK				0x2040
#define GAT_MFC_BUSD_DIV4_GATE					0x2044
#define GAT_MFC_BUSD_GATE					0x2048

static const unsigned long mfc_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_MFC,
	PLL_CON0_PLL_MFC,
	MUX_MFC_BUSD,
	MUX_MFC_BUSP,
	DIV_MFC_BUSD_DIV4,
	GAT_MFC_CMU_MFC_IPCLKPORT_PCLK,
	GAT_MFC_AS_P_MFC_IPCLKPORT_PCLKM,
	GAT_MFC_AS_P_MFC_IPCLKPORT_PCLKS,
	GAT_MFC_AXI2APB_MFC_IPCLKPORT_ACLK,
	GAT_MFC_MFC_IPCLKPORT_ACLK,
	GAT_MFC_NS_BRDG_MFC_IPCLKPORT_CLK__PMFC__CLK_MFC_D,
	GAT_MFC_NS_BRDG_MFC_IPCLKPORT_CLK__PMFC__CLK_MFC_P,
	GAT_MFC_PPMU_MFCD0_IPCLKPORT_ACLK,
	GAT_MFC_PPMU_MFCD0_IPCLKPORT_PCLK,
	GAT_MFC_PPMU_MFCD1_IPCLKPORT_ACLK,
	GAT_MFC_PPMU_MFCD1_IPCLKPORT_PCLK,
	GAT_MFC_SYSREG_MFC_IPCLKPORT_PCLK,
	GAT_MFC_TBU_MFCD0_IPCLKPORT_CLK,
	GAT_MFC_TBU_MFCD1_IPCLKPORT_CLK,
	GAT_MFC_BUSD_DIV4_GATE,
	GAT_MFC_BUSD_GATE,
};

static const struct samsung_pll_rate_table pll_mfc_rate_table[] __initconst = {
	PLL_35XX_RATE(24 * MHZ, 666000000U, 111, 4, 0),
};

static const struct samsung_pll_clock mfc_pll_clks[] __initconst = {
	PLL(pll_142xx, 0, "fout_pll_mfc", "fin_pll",
	    PLL_LOCKTIME_PLL_MFC, PLL_CON0_PLL_MFC, pll_mfc_rate_table),
};

PNAME(mout_mfc_pll_p) = { "fin_pll", "fout_pll_mfc" };
PNAME(mout_mfc_busp_p) = { "fin_pll", "dout_mfc_busd_div4" };
PNAME(mout_mfc_busd_p) = { "fin_pll", "mfc_busd_gate" };

static const struct samsung_mux_clock mfc_mux_clks[] __initconst = {
	MUX(0, "mout_mfc_pll", mout_mfc_pll_p, PLL_CON0_PLL_MFC, 4, 1),
	MUX(0, "mout_mfc_busp", mout_mfc_busp_p, MUX_MFC_BUSP, 0, 1),
	MUX(0, "mout_mfc_busd", mout_mfc_busd_p, MUX_MFC_BUSD, 0, 1),
};

static const struct samsung_div_clock mfc_div_clks[] __initconst = {
	DIV(0, "dout_mfc_busd_div4", "mfc_busd_div4_gate", DIV_MFC_BUSD_DIV4, 0, 4),
};

static const struct samsung_gate_clock mfc_gate_clks[] __initconst = {
	GATE(0, "mfc_cmu_mfc_ipclkport_pclk", "mout_mfc_busp",
	     GAT_MFC_CMU_MFC_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "mfc_as_p_mfc_ipclkport_pclkm", "mout_mfc_busd",
	     GAT_MFC_AS_P_MFC_IPCLKPORT_PCLKM, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "mfc_as_p_mfc_ipclkport_pclks", "mout_mfc_busp",
	     GAT_MFC_AS_P_MFC_IPCLKPORT_PCLKS, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "mfc_axi2apb_mfc_ipclkport_aclk", "mout_mfc_busp",
	     GAT_MFC_AXI2APB_MFC_IPCLKPORT_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(MFC_MFC_IPCLKPORT_ACLK, "mfc_mfc_ipclkport_aclk", "mout_mfc_busd",
	     GAT_MFC_MFC_IPCLKPORT_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "mfc_ns_brdg_mfc_ipclkport_clk__pmfc__clk_mfc_d", "mout_mfc_busd",
	     GAT_MFC_NS_BRDG_MFC_IPCLKPORT_CLK__PMFC__CLK_MFC_D, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "mfc_ns_brdg_mfc_ipclkport_clk__pmfc__clk_mfc_p", "mout_mfc_busp",
	     GAT_MFC_NS_BRDG_MFC_IPCLKPORT_CLK__PMFC__CLK_MFC_P, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "mfc_ppmu_mfcd0_ipclkport_aclk", "mout_mfc_busd",
	     GAT_MFC_PPMU_MFCD0_IPCLKPORT_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "mfc_ppmu_mfcd0_ipclkport_pclk", "mout_mfc_busp",
	     GAT_MFC_PPMU_MFCD0_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "mfc_ppmu_mfcd1_ipclkport_aclk", "mout_mfc_busd",
	     GAT_MFC_PPMU_MFCD1_IPCLKPORT_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "mfc_ppmu_mfcd1_ipclkport_pclk", "mout_mfc_busp",
	     GAT_MFC_PPMU_MFCD1_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "mfc_sysreg_mfc_ipclkport_pclk", "mout_mfc_busp",
	     GAT_MFC_SYSREG_MFC_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "mfc_tbu_mfcd0_ipclkport_clk", "mout_mfc_busd",
	     GAT_MFC_TBU_MFCD0_IPCLKPORT_CLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "mfc_tbu_mfcd1_ipclkport_clk", "mout_mfc_busd",
	     GAT_MFC_TBU_MFCD1_IPCLKPORT_CLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "mfc_busd_div4_gate", "mout_mfc_pll",
	     GAT_MFC_BUSD_DIV4_GATE, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "mfc_busd_gate", "mout_mfc_pll", GAT_MFC_BUSD_GATE, 21, CLK_IS_CRITICAL, 0),
};

static const struct samsung_cmu_info mfc_cmu_info __initconst = {
	.pll_clks		= mfc_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(mfc_pll_clks),
	.mux_clks		= mfc_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(mfc_mux_clks),
	.div_clks		= mfc_div_clks,
	.nr_div_clks		= ARRAY_SIZE(mfc_div_clks),
	.gate_clks		= mfc_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(mfc_gate_clks),
	.nr_clk_ids		= CLKS_NR_MFC,
	.clk_regs		= mfc_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(mfc_clk_regs),
};

/* Register Offset definitions for CMU_CAM_CSI (0x12610000) */
#define PLL_LOCKTIME_PLL_CAM_CSI		0x0
#define PLL_CON0_PLL_CAM_CSI			0x100
#define DIV_CAM_CSI0_ACLK			0x1800
#define DIV_CAM_CSI1_ACLK			0x1804
#define DIV_CAM_CSI2_ACLK			0x1808
#define DIV_CAM_CSI_BUSD			0x180c
#define DIV_CAM_CSI_BUSP			0x1810
#define GAT_CAM_CSI_CMU_CAM_CSI_IPCLKPORT_PCLK	0x2000
#define GAT_CAM_AXI2APB_CAM_CSI_IPCLKPORT_ACLK	0x2004
#define GAT_CAM_CSI_BUS_D_CAM_CSI_IPCLKPORT_CLK__SYSTEM__CLK_CSI0	0x2008
#define GAT_CAM_CSI_BUS_D_CAM_CSI_IPCLKPORT_CLK__SYSTEM__CLK_CSI1	0x200c
#define GAT_CAM_CSI_BUS_D_CAM_CSI_IPCLKPORT_CLK__SYSTEM__CLK_CSI2	0x2010
#define GAT_CAM_CSI_BUS_D_CAM_CSI_IPCLKPORT_CLK__SYSTEM__CLK_SOC_NOC	0x2014
#define GAT_CAM_CSI_BUS_D_CAM_CSI_IPCLKPORT_CLK__SYSTEM__NOC		0x2018
#define GAT_CAM_CSI0_0_IPCLKPORT_I_ACLK		0x201c
#define GAT_CAM_CSI0_0_IPCLKPORT_I_PCLK		0x2020
#define GAT_CAM_CSI0_1_IPCLKPORT_I_ACLK		0x2024
#define GAT_CAM_CSI0_1_IPCLKPORT_I_PCLK		0x2028
#define GAT_CAM_CSI0_2_IPCLKPORT_I_ACLK		0x202c
#define GAT_CAM_CSI0_2_IPCLKPORT_I_PCLK		0x2030
#define GAT_CAM_CSI0_3_IPCLKPORT_I_ACLK		0x2034
#define GAT_CAM_CSI0_3_IPCLKPORT_I_PCLK		0x2038
#define GAT_CAM_CSI1_0_IPCLKPORT_I_ACLK		0x203c
#define GAT_CAM_CSI1_0_IPCLKPORT_I_PCLK		0x2040
#define GAT_CAM_CSI1_1_IPCLKPORT_I_ACLK		0x2044
#define GAT_CAM_CSI1_1_IPCLKPORT_I_PCLK		0x2048
#define GAT_CAM_CSI1_2_IPCLKPORT_I_ACLK		0x204c
#define GAT_CAM_CSI1_2_IPCLKPORT_I_PCLK		0x2050
#define GAT_CAM_CSI1_3_IPCLKPORT_I_ACLK		0x2054
#define GAT_CAM_CSI1_3_IPCLKPORT_I_PCLK		0x2058
#define GAT_CAM_CSI2_0_IPCLKPORT_I_ACLK		0x205c
#define GAT_CAM_CSI2_0_IPCLKPORT_I_PCLK		0x2060
#define GAT_CAM_CSI2_1_IPCLKPORT_I_ACLK		0x2064
#define GAT_CAM_CSI2_1_IPCLKPORT_I_PCLK		0x2068
#define GAT_CAM_CSI2_2_IPCLKPORT_I_ACLK		0x206c
#define GAT_CAM_CSI2_2_IPCLKPORT_I_PCLK		0x2070
#define GAT_CAM_CSI2_3_IPCLKPORT_I_ACLK		0x2074
#define GAT_CAM_CSI2_3_IPCLKPORT_I_PCLK		0x2078
#define GAT_CAM_NS_BRDG_CAM_CSI_IPCLKPORT_CLK__PSOC_CAM_CSI__CLK_CAM_CSI_D	0x207c
#define GAT_CAM_NS_BRDG_CAM_CSI_IPCLKPORT_CLK__PSOC_CAM_CSI__CLK_CAM_CSI_P	0x2080
#define GAT_CAM_SYSREG_CAM_CSI_IPCLKPORT_PCLK	0x2084
#define GAT_CAM_TBU_CAM_CSI_IPCLKPORT_ACLK	0x2088

static const unsigned long cam_csi_clk_regs[] __initconst = {
	PLL_LOCKTIME_PLL_CAM_CSI,
	PLL_CON0_PLL_CAM_CSI,
	DIV_CAM_CSI0_ACLK,
	DIV_CAM_CSI1_ACLK,
	DIV_CAM_CSI2_ACLK,
	DIV_CAM_CSI_BUSD,
	DIV_CAM_CSI_BUSP,
	GAT_CAM_CSI_CMU_CAM_CSI_IPCLKPORT_PCLK,
	GAT_CAM_AXI2APB_CAM_CSI_IPCLKPORT_ACLK,
	GAT_CAM_CSI_BUS_D_CAM_CSI_IPCLKPORT_CLK__SYSTEM__CLK_CSI0,
	GAT_CAM_CSI_BUS_D_CAM_CSI_IPCLKPORT_CLK__SYSTEM__CLK_CSI1,
	GAT_CAM_CSI_BUS_D_CAM_CSI_IPCLKPORT_CLK__SYSTEM__CLK_CSI2,
	GAT_CAM_CSI_BUS_D_CAM_CSI_IPCLKPORT_CLK__SYSTEM__CLK_SOC_NOC,
	GAT_CAM_CSI_BUS_D_CAM_CSI_IPCLKPORT_CLK__SYSTEM__NOC,
	GAT_CAM_CSI0_0_IPCLKPORT_I_ACLK,
	GAT_CAM_CSI0_0_IPCLKPORT_I_PCLK,
	GAT_CAM_CSI0_1_IPCLKPORT_I_ACLK,
	GAT_CAM_CSI0_1_IPCLKPORT_I_PCLK,
	GAT_CAM_CSI0_2_IPCLKPORT_I_ACLK,
	GAT_CAM_CSI0_2_IPCLKPORT_I_PCLK,
	GAT_CAM_CSI0_3_IPCLKPORT_I_ACLK,
	GAT_CAM_CSI0_3_IPCLKPORT_I_PCLK,
	GAT_CAM_CSI1_0_IPCLKPORT_I_ACLK,
	GAT_CAM_CSI1_0_IPCLKPORT_I_PCLK,
	GAT_CAM_CSI1_1_IPCLKPORT_I_ACLK,
	GAT_CAM_CSI1_1_IPCLKPORT_I_PCLK,
	GAT_CAM_CSI1_2_IPCLKPORT_I_ACLK,
	GAT_CAM_CSI1_2_IPCLKPORT_I_PCLK,
	GAT_CAM_CSI1_3_IPCLKPORT_I_ACLK,
	GAT_CAM_CSI1_3_IPCLKPORT_I_PCLK,
	GAT_CAM_CSI2_0_IPCLKPORT_I_ACLK,
	GAT_CAM_CSI2_0_IPCLKPORT_I_PCLK,
	GAT_CAM_CSI2_1_IPCLKPORT_I_ACLK,
	GAT_CAM_CSI2_1_IPCLKPORT_I_PCLK,
	GAT_CAM_CSI2_2_IPCLKPORT_I_ACLK,
	GAT_CAM_CSI2_2_IPCLKPORT_I_PCLK,
	GAT_CAM_CSI2_3_IPCLKPORT_I_ACLK,
	GAT_CAM_CSI2_3_IPCLKPORT_I_PCLK,
	GAT_CAM_NS_BRDG_CAM_CSI_IPCLKPORT_CLK__PSOC_CAM_CSI__CLK_CAM_CSI_D,
	GAT_CAM_NS_BRDG_CAM_CSI_IPCLKPORT_CLK__PSOC_CAM_CSI__CLK_CAM_CSI_P,
	GAT_CAM_SYSREG_CAM_CSI_IPCLKPORT_PCLK,
	GAT_CAM_TBU_CAM_CSI_IPCLKPORT_ACLK,
};

static const struct samsung_pll_rate_table pll_cam_csi_rate_table[] __initconst = {
	PLL_35XX_RATE(24 * MHZ, 1066000000U, 533, 12, 0),
};

static const struct samsung_pll_clock cam_csi_pll_clks[] __initconst = {
	PLL(pll_142xx, 0, "fout_pll_cam_csi", "fin_pll",
	    PLL_LOCKTIME_PLL_CAM_CSI, PLL_CON0_PLL_CAM_CSI, pll_cam_csi_rate_table),
};

PNAME(mout_cam_csi_pll_p) = { "fin_pll", "fout_pll_cam_csi" };

static const struct samsung_mux_clock cam_csi_mux_clks[] __initconst = {
	MUX(0, "mout_cam_csi_pll", mout_cam_csi_pll_p, PLL_CON0_PLL_CAM_CSI, 4, 1),
};

static const struct samsung_div_clock cam_csi_div_clks[] __initconst = {
	DIV(0, "dout_cam_csi0_aclk", "mout_cam_csi_pll", DIV_CAM_CSI0_ACLK, 0, 4),
	DIV(0, "dout_cam_csi1_aclk", "mout_cam_csi_pll", DIV_CAM_CSI1_ACLK, 0, 4),
	DIV(0, "dout_cam_csi2_aclk", "mout_cam_csi_pll", DIV_CAM_CSI2_ACLK, 0, 4),
	DIV(0, "dout_cam_csi_busd", "mout_cam_csi_pll", DIV_CAM_CSI_BUSD, 0, 4),
	DIV(0, "dout_cam_csi_busp", "mout_cam_csi_pll", DIV_CAM_CSI_BUSP, 0, 4),
};

static const struct samsung_gate_clock cam_csi_gate_clks[] __initconst = {
	GATE(0, "cam_csi_cmu_cam_csi_ipclkport_pclk", "dout_cam_csi_busp",
	     GAT_CAM_CSI_CMU_CAM_CSI_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "cam_axi2apb_cam_csi_ipclkport_aclk", "dout_cam_csi_busp",
	     GAT_CAM_AXI2APB_CAM_CSI_IPCLKPORT_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "cam_csi_bus_d_cam_csi_ipclkport_clk__system__clk_csi0", "dout_cam_csi0_aclk",
	     GAT_CAM_CSI_BUS_D_CAM_CSI_IPCLKPORT_CLK__SYSTEM__CLK_CSI0, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "cam_csi_bus_d_cam_csi_ipclkport_clk__system__clk_csi1", "dout_cam_csi1_aclk",
	     GAT_CAM_CSI_BUS_D_CAM_CSI_IPCLKPORT_CLK__SYSTEM__CLK_CSI1, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "cam_csi_bus_d_cam_csi_ipclkport_clk__system__clk_csi2", "dout_cam_csi2_aclk",
	     GAT_CAM_CSI_BUS_D_CAM_CSI_IPCLKPORT_CLK__SYSTEM__CLK_CSI2, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "cam_csi_bus_d_cam_csi_ipclkport_clk__system__clk_soc_noc", "dout_cam_csi_busd",
	     GAT_CAM_CSI_BUS_D_CAM_CSI_IPCLKPORT_CLK__SYSTEM__CLK_SOC_NOC, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(0, "cam_csi_bus_d_cam_csi_ipclkport_clk__system__noc", "dout_cam_csi_busd",
	     GAT_CAM_CSI_BUS_D_CAM_CSI_IPCLKPORT_CLK__SYSTEM__NOC, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CAM_CSI0_0_IPCLKPORT_I_ACLK, "cam_csi0_0_ipclkport_i_aclk", "dout_cam_csi0_aclk",
	     GAT_CAM_CSI0_0_IPCLKPORT_I_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "cam_csi0_0_ipclkport_i_pclk", "dout_cam_csi_busp",
	     GAT_CAM_CSI0_0_IPCLKPORT_I_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CAM_CSI0_1_IPCLKPORT_I_ACLK, "cam_csi0_1_ipclkport_i_aclk", "dout_cam_csi0_aclk",
	     GAT_CAM_CSI0_1_IPCLKPORT_I_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "cam_csi0_1_ipclkport_i_pclk", "dout_cam_csi_busp",
	     GAT_CAM_CSI0_1_IPCLKPORT_I_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CAM_CSI0_2_IPCLKPORT_I_ACLK, "cam_csi0_2_ipclkport_i_aclk", "dout_cam_csi0_aclk",
	     GAT_CAM_CSI0_2_IPCLKPORT_I_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "cam_csi0_2_ipclkport_i_pclk", "dout_cam_csi_busp",
	     GAT_CAM_CSI0_2_IPCLKPORT_I_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CAM_CSI0_3_IPCLKPORT_I_ACLK, "cam_csi0_3_ipclkport_i_aclk", "dout_cam_csi0_aclk",
	     GAT_CAM_CSI0_3_IPCLKPORT_I_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "cam_csi0_3_ipclkport_i_pclk", "dout_cam_csi_busp",
	     GAT_CAM_CSI0_3_IPCLKPORT_I_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CAM_CSI1_0_IPCLKPORT_I_ACLK, "cam_csi1_0_ipclkport_i_aclk", "dout_cam_csi1_aclk",
	     GAT_CAM_CSI1_0_IPCLKPORT_I_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "cam_csi1_0_ipclkport_i_pclk", "dout_cam_csi_busp",
	     GAT_CAM_CSI1_0_IPCLKPORT_I_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CAM_CSI1_1_IPCLKPORT_I_ACLK, "cam_csi1_1_ipclkport_i_aclk", "dout_cam_csi1_aclk",
	     GAT_CAM_CSI1_1_IPCLKPORT_I_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "cam_csi1_1_ipclkport_i_pclk", "dout_cam_csi_busp",
	     GAT_CAM_CSI1_1_IPCLKPORT_I_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CAM_CSI1_2_IPCLKPORT_I_ACLK, "cam_csi1_2_ipclkport_i_aclk", "dout_cam_csi1_aclk",
	     GAT_CAM_CSI1_2_IPCLKPORT_I_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "cam_csi1_2_ipclkport_i_pclk", "dout_cam_csi_busp",
	     GAT_CAM_CSI1_2_IPCLKPORT_I_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CAM_CSI1_3_IPCLKPORT_I_ACLK, "cam_csi1_3_ipclkport_i_aclk", "dout_cam_csi1_aclk",
	     GAT_CAM_CSI1_3_IPCLKPORT_I_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "cam_csi1_3_ipclkport_i_pclk", "dout_cam_csi_busp",
	     GAT_CAM_CSI1_3_IPCLKPORT_I_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CAM_CSI2_0_IPCLKPORT_I_ACLK, "cam_csi2_0_ipclkport_i_aclk", "dout_cam_csi2_aclk",
	     GAT_CAM_CSI2_0_IPCLKPORT_I_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "cam_csi2_0_ipclkport_i_pclk", "dout_cam_csi_busp",
	     GAT_CAM_CSI2_0_IPCLKPORT_I_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CAM_CSI2_1_IPCLKPORT_I_ACLK, "cam_csi2_1_ipclkport_i_aclk", "dout_cam_csi2_aclk",
	     GAT_CAM_CSI2_1_IPCLKPORT_I_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "cam_csi2_1_ipclkport_i_pclk", "dout_cam_csi_busp",
	     GAT_CAM_CSI2_1_IPCLKPORT_I_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CAM_CSI2_2_IPCLKPORT_I_ACLK, "cam_csi2_2_ipclkport_i_aclk", "dout_cam_csi2_aclk",
	     GAT_CAM_CSI2_2_IPCLKPORT_I_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "cam_csi2_2_ipclkport_i_pclk", "dout_cam_csi_busp",
	     GAT_CAM_CSI2_2_IPCLKPORT_I_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(CAM_CSI2_3_IPCLKPORT_I_ACLK, "cam_csi2_3_ipclkport_i_aclk", "dout_cam_csi2_aclk",
	     GAT_CAM_CSI2_3_IPCLKPORT_I_ACLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "cam_csi2_3_ipclkport_i_pclk", "dout_cam_csi_busp",
	     GAT_CAM_CSI2_3_IPCLKPORT_I_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "cam_ns_brdg_cam_csi_ipclkport_clk__psoc_cam_csi__clk_cam_csi_d",
	     "dout_cam_csi_busd",
	     GAT_CAM_NS_BRDG_CAM_CSI_IPCLKPORT_CLK__PSOC_CAM_CSI__CLK_CAM_CSI_D, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(0, "cam_ns_brdg_cam_csi_ipclkport_clk__psoc_cam_csi__clk_cam_csi_p",
	     "dout_cam_csi_busp",
	     GAT_CAM_NS_BRDG_CAM_CSI_IPCLKPORT_CLK__PSOC_CAM_CSI__CLK_CAM_CSI_P, 21,
	     CLK_IGNORE_UNUSED, 0),
	GATE(0, "cam_sysreg_cam_csi_ipclkport_pclk", "dout_cam_csi_busp",
	     GAT_CAM_SYSREG_CAM_CSI_IPCLKPORT_PCLK, 21, CLK_IGNORE_UNUSED, 0),
	GATE(0, "cam_tbu_cam_csi_ipclkport_aclk", "dout_cam_csi_busd",
	     GAT_CAM_TBU_CAM_CSI_IPCLKPORT_ACLK, 21, CLK_IGNORE_UNUSED, 0),
};

static const struct samsung_cmu_info cam_csi_cmu_info __initconst = {
	.pll_clks		= cam_csi_pll_clks,
	.nr_pll_clks		= ARRAY_SIZE(cam_csi_pll_clks),
	.mux_clks		= cam_csi_mux_clks,
	.nr_mux_clks		= ARRAY_SIZE(cam_csi_mux_clks),
	.div_clks		= cam_csi_div_clks,
	.nr_div_clks		= ARRAY_SIZE(cam_csi_div_clks),
	.gate_clks		= cam_csi_gate_clks,
	.nr_gate_clks		= ARRAY_SIZE(cam_csi_gate_clks),
	.nr_clk_ids		= CLKS_NR_CAM_CSI,
	.clk_regs		= cam_csi_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(cam_csi_clk_regs),
};

/**
 * fsd_cmu_probe - Probe function for FSD platform clocks
 * @pdev: Pointer to platform device
 *
 * Configure clock hierarchy for clock domains of FSD platform
 */
static int __init fsd_cmu_probe(struct platform_device *pdev)
{
	const struct samsung_cmu_info *info;
	struct device *dev = &pdev->dev;

	info = of_device_get_match_data(dev);
	exynos_arm64_register_cmu(dev, dev->of_node, info);

	return 0;
}

/* CMUs which belong to Power Domains and need runtime PM to be implemented */
static const struct of_device_id fsd_cmu_of_match[] = {
	{
		.compatible = "tesla,fsd-clock-peric",
		.data = &peric_cmu_info,
	}, {
		.compatible = "tesla,fsd-clock-fsys0",
		.data = &fsys0_cmu_info,
	}, {
		.compatible = "tesla,fsd-clock-fsys1",
		.data = &fsys1_cmu_info,
	}, {
		.compatible = "tesla,fsd-clock-mfc",
		.data = &mfc_cmu_info,
	}, {
		.compatible = "tesla,fsd-clock-cam_csi",
		.data = &cam_csi_cmu_info,
	}, {
	},
};

static struct platform_driver fsd_cmu_driver __refdata = {
	.driver	= {
		.name = "fsd-cmu",
		.of_match_table = fsd_cmu_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = fsd_cmu_probe,
};

static int __init fsd_cmu_init(void)
{
	return platform_driver_register(&fsd_cmu_driver);
}
core_initcall(fsd_cmu_init);
