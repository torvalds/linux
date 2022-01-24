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
#include <linux/of.h>

#include <dt-bindings/clock/fsd-clk.h>

#include "clk.h"

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
	.nr_clk_ids		= CMU_NR_CLK,
	.clk_regs		= cmu_clk_regs,
	.nr_clk_regs		= ARRAY_SIZE(cmu_clk_regs),
};

static void __init fsd_clk_cmu_init(struct device_node *np)
{
	samsung_cmu_register_one(np, &cmu_cmu_info);
}

CLK_OF_DECLARE(fsd_clk_cmu, "tesla,fsd-clock-cmu", fsd_clk_cmu_init);
