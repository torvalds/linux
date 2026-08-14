// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2026, Beijing ESWIN Computing Technology Co., Ltd..
 * All rights reserved.
 *
 * ESWIN EIC7700 Clk Provider Driver
 *
 * Authors:
 *	Yifeng Huang <huangyifeng@eswincomputing.com>
 *	Xuyang Dong <dongxuyang@eswincomputing.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include <dt-bindings/clock/eswin,eic7700-clock.h>

#include "common.h"

/* REG OFFSET OF SYS-CRG */
#define EIC7700_REG_OFFSET_SPLL0_CFG_0		0x0
#define EIC7700_REG_OFFSET_SPLL0_CFG_1		0x4
#define EIC7700_REG_OFFSET_SPLL0_CFG_2		0x8
#define EIC7700_REG_OFFSET_SPLL0_DSKEWCAL	0xC
#define EIC7700_REG_OFFSET_SPLL0_SSC		0x10
#define EIC7700_REG_OFFSET_SPLL1_CFG_0		0x14
#define EIC7700_REG_OFFSET_SPLL1_CFG_1		0x18
#define EIC7700_REG_OFFSET_SPLL1_CFG_2		0x1C
#define EIC7700_REG_OFFSET_SPLL1_DSKEWCAL	0x20
#define EIC7700_REG_OFFSET_SPLL1_SSC		0x24
#define EIC7700_REG_OFFSET_SPLL2_CFG_0		0x28
#define EIC7700_REG_OFFSET_SPLL2_CFG_1		0x2C
#define EIC7700_REG_OFFSET_SPLL2_CFG_2		0x30
#define EIC7700_REG_OFFSET_SPLL2_DSKEWCAL	0x34
#define EIC7700_REG_OFFSET_SPLL2_SSC		0x38
#define EIC7700_REG_OFFSET_VPLL_CFG_0		0x3C
#define EIC7700_REG_OFFSET_VPLL_CFG_1		0x40
#define EIC7700_REG_OFFSET_VPLL_CFG_2		0x44
#define EIC7700_REG_OFFSET_VPLL_DSKEWCAL	0x48
#define EIC7700_REG_OFFSET_VPLL_SSC		0x4C
#define EIC7700_REG_OFFSET_APLL_CFG_0		0x50
#define EIC7700_REG_OFFSET_APLL_CFG_1		0x54
#define EIC7700_REG_OFFSET_APLL_CFG_2		0x58
#define EIC7700_REG_OFFSET_APLL_DSKEWCAL	0x5C
#define EIC7700_REG_OFFSET_APLL_SSC		0x60
#define EIC7700_REG_OFFSET_MCPUT_PLL_CFG_0	0x64
#define EIC7700_REG_OFFSET_MCPUT_PLL_CFG_1	0x68
#define EIC7700_REG_OFFSET_MCPUT_PLL_CFG_2	0x6C
#define EIC7700_REG_OFFSET_MCPUT_PLL_DSKEWCAL	0x70
#define EIC7700_REG_OFFSET_MCPUT_PLL_SSC	0x74
#define EIC7700_REG_OFFSET_DDRT_PLL_CFG_0	0x78
#define EIC7700_REG_OFFSET_DDRT_PLL_CFG_1	0x7C
#define EIC7700_REG_OFFSET_DDRT_PLL_CFG_2	0x80
#define EIC7700_REG_OFFSET_DDRT_PLL_DSKEWCAL	0x84
#define EIC7700_REG_OFFSET_DDRT_PLL_SSC		0x88
#define EIC7700_REG_OFFSET_PLL_STATUS		0xA4
#define EIC7700_REG_OFFSET_NOC			0x100
#define EIC7700_REG_OFFSET_BOOTSPI		0x104
#define EIC7700_REG_OFFSET_BOOTSPI_CFGCLK	0x108
#define EIC7700_REG_OFFSET_SCPU_CORE		0x10C
#define EIC7700_REG_OFFSET_SCPU_BUSCLK		0x110
#define EIC7700_REG_OFFSET_LPCPU_CORE		0x114
#define EIC7700_REG_OFFSET_LPCPU_BUSCLK		0x118
#define EIC7700_REG_OFFSET_TCU_ACLK		0x11C
#define EIC7700_REG_OFFSET_TCU_CFG		0x120
#define EIC7700_REG_OFFSET_DDR			0x124
#define EIC7700_REG_OFFSET_DDR1			0x128
#define EIC7700_REG_OFFSET_GPU_ACLK		0x12C
#define EIC7700_REG_OFFSET_GPU_CFG		0x130
#define EIC7700_REG_OFFSET_GPU_GRAY		0x134
#define EIC7700_REG_OFFSET_DSP_ACLK		0x138
#define EIC7700_REG_OFFSET_DSP_CFG		0x13C
#define EIC7700_REG_OFFSET_D2D_ACLK		0x140
#define EIC7700_REG_OFFSET_D2D_CFG		0x144
#define EIC7700_REG_OFFSET_HSP_ACLK		0x148
#define EIC7700_REG_OFFSET_HSP_CFG		0x14C
#define EIC7700_REG_OFFSET_SATA_RBC		0x150
#define EIC7700_REG_OFFSET_SATA_OOB		0x154
#define EIC7700_REG_OFFSET_ETH0			0x158
#define EIC7700_REG_OFFSET_ETH1			0x15C
#define EIC7700_REG_OFFSET_MSHC0_CORE		0x160
#define EIC7700_REG_OFFSET_MSHC1_CORE		0x164
#define EIC7700_REG_OFFSET_MSHC2_CORE		0x168
#define EIC7700_REG_OFFSET_MSHC_USB_SLWCLK	0x16C
#define EIC7700_REG_OFFSET_PCIE_ACLK		0x170
#define EIC7700_REG_OFFSET_PCIE_CFG		0x174
#define EIC7700_REG_OFFSET_NPU_ACLK		0x178
#define EIC7700_REG_OFFSET_NPU_LLC		0x17C
#define EIC7700_REG_OFFSET_NPU_CORE		0x180
#define EIC7700_REG_OFFSET_VI_DWCLK		0x184
#define EIC7700_REG_OFFSET_VI_ACLK		0x188
#define EIC7700_REG_OFFSET_VI_DIG_ISP		0x18C
#define EIC7700_REG_OFFSET_VI_DVP		0x190
#define EIC7700_REG_OFFSET_VI_SHUTTER0		0x194
#define EIC7700_REG_OFFSET_VI_SHUTTER1		0x198
#define EIC7700_REG_OFFSET_VI_SHUTTER2		0x19C
#define EIC7700_REG_OFFSET_VI_SHUTTER3		0x1A0
#define EIC7700_REG_OFFSET_VI_SHUTTER4		0x1A4
#define EIC7700_REG_OFFSET_VI_SHUTTER5		0x1A8
#define EIC7700_REG_OFFSET_VI_PHY		0x1AC
#define EIC7700_REG_OFFSET_VO_ACLK		0x1B0
#define EIC7700_REG_OFFSET_VO_IESMCLK		0x1B4
#define EIC7700_REG_OFFSET_VO_PIXEL		0x1B8
#define EIC7700_REG_OFFSET_VO_MCLK		0x1BC
#define EIC7700_REG_OFFSET_VO_PHY_CLK		0x1C0
#define EIC7700_REG_OFFSET_VC_ACLK		0x1C4
#define EIC7700_REG_OFFSET_VCDEC_ROOT		0x1C8
#define EIC7700_REG_OFFSET_G2D			0x1CC
#define EIC7700_REG_OFFSET_VC_CLKEN		0x1D0
#define EIC7700_REG_OFFSET_JE			0x1D4
#define EIC7700_REG_OFFSET_JD			0x1D8
#define EIC7700_REG_OFFSET_VD			0x1DC
#define EIC7700_REG_OFFSET_VE			0x1E0
#define EIC7700_REG_OFFSET_AON_DMA		0x1E4
#define EIC7700_REG_OFFSET_TIMER		0x1E8
#define EIC7700_REG_OFFSET_RTC			0x1EC
#define EIC7700_REG_OFFSET_PKA			0x1F0
#define EIC7700_REG_OFFSET_SPACC		0x1F4
#define EIC7700_REG_OFFSET_TRNG			0x1F8
#define EIC7700_REG_OFFSET_OTP			0x1FC
#define EIC7700_REG_OFFSET_LSP_EN0		0x200
#define EIC7700_REG_OFFSET_LSP_EN1		0x204
#define EIC7700_REG_OFFSET_U84			0x208
#define EIC7700_REG_OFFSET_SYSCFG		0x20C
#define EIC7700_REG_OFFSET_I2C0			0x210
#define EIC7700_REG_OFFSET_I2C1			0x214

#define EIC7700_NR_CLKS				(EIC7700_CLK_GATE_NOC_WDREF + 1)

/*
 * The 24 MHz oscillator, the root of most of the clock tree.
 */
static const struct clk_parent_data xtal24M[] = {
	{ .index = 0, }
};

/* fixed rate clocks */
static struct eswin_fixed_rate_clock eic7700_fixed_rate_clks[] = {
	ESWIN_FIXED(EIC7700_CLK_XTAL_32K, "fixed_rate_clk_xtal_32k", 0, 32768),
	ESWIN_FIXED(EIC7700_CLK_SPLL0_FOUT1, "fixed_rate_clk_spll0_fout1", 0,
		    1600000000),
	ESWIN_FIXED(EIC7700_CLK_SPLL0_FOUT2, "fixed_rate_clk_spll0_fout2", 0,
		    800000000),
	ESWIN_FIXED(EIC7700_CLK_SPLL0_FOUT3, "fixed_rate_clk_spll0_fout3", 0,
		    400000000),
	ESWIN_FIXED(EIC7700_CLK_SPLL1_FOUT1, "fixed_rate_clk_spll1_fout1", 0,
		    1500000000),
	ESWIN_FIXED(EIC7700_CLK_SPLL1_FOUT2, "fixed_rate_clk_spll1_fout2", 0,
		    300000000),
	ESWIN_FIXED(EIC7700_CLK_SPLL1_FOUT3, "fixed_rate_clk_spll1_fout3", 0,
		    250000000),
	ESWIN_FIXED(EIC7700_CLK_SPLL2_FOUT1, "fixed_rate_clk_spll2_fout1", 0,
		    2080000000),
	ESWIN_FIXED(EIC7700_CLK_SPLL2_FOUT2, "fixed_rate_clk_spll2_fout2", 0,
		    1040000000),
	ESWIN_FIXED(EIC7700_CLK_SPLL2_FOUT3, "fixed_rate_clk_spll2_fout3", 0,
		    416000000),
	ESWIN_FIXED(EIC7700_CLK_VPLL_FOUT1, "fixed_rate_clk_vpll_fout1", 0,
		    1188000000),
	ESWIN_FIXED(EIC7700_CLK_VPLL_FOUT2, "fixed_rate_clk_vpll_fout2", 0,
		    594000000),
	ESWIN_FIXED(EIC7700_CLK_VPLL_FOUT3, "fixed_rate_clk_vpll_fout3", 0,
		    49500000),
	ESWIN_FIXED(EIC7700_CLK_APLL_FOUT2, "fixed_rate_clk_apll_fout2", 0, 0),
	ESWIN_FIXED(EIC7700_CLK_APLL_FOUT3, "fixed_rate_clk_apll_fout3", 0, 0),
	ESWIN_FIXED(EIC7700_CLK_EXT_MCLK, "fixed_rate_ext_mclk", 0, 0),
	ESWIN_FIXED(EIC7700_CLK_LPDDR_REF_BAK, "fixed_rate_lpddr_ref_bak", 0,
		    50000000),
};

/* pll clocks */
static struct eswin_pll_clock eic7700_pll_clks[] = {
	ESWIN_PLL(EIC7700_CLK_APLL_FOUT1, "clk_apll_fout1", xtal24M,
		  EIC7700_REG_OFFSET_APLL_CFG_0, 20,
		  EIC7700_REG_OFFSET_APLL_CFG_1, 4,
		  EIC7700_REG_OFFSET_APLL_CFG_2, EIC7700_REG_OFFSET_PLL_STATUS,
		  4, 1, APLL_HIGH_FREQ, APLL_LOW_FREQ),
	ESWIN_PLL(EIC7700_CLK_PLL_CPU, "clk_pll_cpu", xtal24M,
		  EIC7700_REG_OFFSET_MCPUT_PLL_CFG_0, 20,
		  EIC7700_REG_OFFSET_MCPUT_PLL_CFG_1, 4,
		  EIC7700_REG_OFFSET_MCPUT_PLL_CFG_2,
		  EIC7700_REG_OFFSET_PLL_STATUS, 5, 1, PLL_HIGH_FREQ,
		  PLL_LOW_FREQ),
};

/* fixed factor clocks */
static struct eswin_fixed_factor_clock eic7700_factor_clks[] = {
	ESWIN_FACTOR(EIC7700_CLK_FIXED_FACTOR_CLK_1M_DIV24,
		     "fixed_factor_clk_1m_div24", xtal24M, 1, 24, 0),
	ESWIN_FACTOR(EIC7700_CLK_FIXED_FACTOR_PVT_DIV20,
		     "fixed_factor_pvt_div20", xtal24M, 1, 20, 0),
};

/* divider clocks */
static struct eswin_divider_clock eic7700_div_clks[] = {
	ESWIN_DIV(EIC7700_CLK_DIV_U84_RTC_TOGGLE_DYNM,
		  "divider_u84_rtc_toggle_dynm", xtal24M, 0,
		  EIC7700_REG_OFFSET_RTC, 16, 5,
		  CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO, 0),
	ESWIN_DIV(EIC7700_CLK_DIV_NOC_WDREF_DYNM, "divider_noc_wdref_dynm",
		  xtal24M, 0, EIC7700_REG_OFFSET_NOC, 4, 16,
		  CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO, 0),
};

/* gate clocks */
static struct eswin_gate_clock eic7700_gate_clks[] = {
	ESWIN_GATE(EIC7700_CLK_GATE_GPU_GRAY_CLK, "gate_gpu_gray_clk", xtal24M,
		   CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_GPU_GRAY, 31, 0),
	ESWIN_GATE(EIC7700_CLK_GATE_VI_PHY_CFG, "gate_vi_phy_cfg", xtal24M,
		   CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_VI_PHY, 1, 0),
	ESWIN_GATE(EIC7700_CLK_GATE_TIMER_CLK_0, "gate_time_clk_0", xtal24M,
		   CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_TIMER, 0, 0),
	ESWIN_GATE(EIC7700_CLK_GATE_TIMER_CLK_1, "gate_time_clk_1", xtal24M,
		   CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_TIMER, 1, 0),
	ESWIN_GATE(EIC7700_CLK_GATE_TIMER_CLK_2, "gate_time_clk_2", xtal24M,
		   CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_TIMER, 2, 0),
	ESWIN_GATE(EIC7700_CLK_GATE_TIMER_CLK_3, "gate_time_clk_3", xtal24M,
		   CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_TIMER, 3, 0),
};

/* Define the early clocks as the parent clocks of the mux clocks. */
static struct eswin_clk_info eic7700_early_clks[] = {
	ESWIN_FACTOR_TYPE(EIC7700_CLK_FIXED_FACTOR_HSP_RMII_REF_DIV6,
			  "fixed_factor_hsp_rmii_ref_div6",
			  EIC7700_CLK_SPLL1_FOUT2, 1, 6, 0),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_NPU_LLC_SRC0_DYNM,
		       "divider_npu_llc_src0_div_dynm",
		       EIC7700_CLK_SPLL0_FOUT1, 0, EIC7700_REG_OFFSET_NPU_LLC,
		       4, 4, 0, ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_NPU_LLC_SRC1_DYNM,
		       "divider_npu_llc_src1_div_dynm",
		       EIC7700_CLK_SPLL2_FOUT1, 0, EIC7700_REG_OFFSET_NPU_LLC,
		       8, 4, 0, ESWIN_PRIV_DIV_MIN_2),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_SPLL0_FOUT2, "gate_clk_spll0_fout2",
			EIC7700_CLK_SPLL0_FOUT2,
			CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
			EIC7700_REG_OFFSET_SPLL0_CFG_2, 31, 0),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_BOOTSPI_DYNM, "divider_bootspi_div_dynm",
		       EIC7700_CLK_GATE_SPLL0_FOUT2, 0,
		       EIC7700_REG_OFFSET_BOOTSPI, 4, 6, 0,
		       ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_SCPU_CORE_DYNM,
		       "divider_scpu_core_div_dynm", EIC7700_CLK_SPLL0_FOUT1, 0,
		       EIC7700_REG_OFFSET_SCPU_CORE, 4, 4, 0,
		       ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_LPCPU_CORE_DYNM,
		       "divider_lpcpu_core_div_dynm", EIC7700_CLK_SPLL0_FOUT1,
		       0, EIC7700_REG_OFFSET_LPCPU_CORE, 4, 4, 0,
		       ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_VO_MCLK_DYNM, "divider_vo_mclk_div_dynm",
		       EIC7700_CLK_APLL_FOUT1, 0, EIC7700_REG_OFFSET_VO_MCLK, 4,
		       8, 0, ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_AONDMA_AXI_DYNM,
		       "divider_aondma_axi_div_dynm", EIC7700_CLK_SPLL0_FOUT1,
		       0, EIC7700_REG_OFFSET_AON_DMA, 4, 4, 0,
		       ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_SATA_PHY_REF_DYNM,
		       "divider_sata_phy_ref_div_dynm",
		       EIC7700_CLK_SPLL1_FOUT2, 0, EIC7700_REG_OFFSET_SATA_OOB,
		       0, 4, 0, ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_SYS_CFG_DYNM, "divider_sys_cfg_div_dynm",
		       EIC7700_CLK_SPLL0_FOUT3, 0, EIC7700_REG_OFFSET_SYSCFG, 4,
		       3, 0, ESWIN_PRIV_DIV_MIN_2),
	ESWIN_FACTOR_TYPE(EIC7700_CLK_FIXED_FACTOR_U84_CORE_LP_DIV2,
			  "fixed_factor_u84_core_lp_div2",
			  EIC7700_CLK_GATE_SPLL0_FOUT2, 1, 2, 0),
};

static const struct clk_parent_data dsp_aclk_root_2mux1_gfree_mux_p[] = {
	{ .hw = &eic7700_fixed_rate_clks[7].hw },
	{ .hw = &eic7700_fixed_rate_clks[1].hw },
};

static const struct clk_parent_data d2d_aclk_root_2mux1_gfree_mux_p[] = {
	{ .hw = &eic7700_fixed_rate_clks[7].hw },
	{ .hw = &eic7700_fixed_rate_clks[1].hw },
};

static const struct clk_parent_data ddr_aclk_root_2mux1_gfree_mux_p[] = {
	{ .hw = &eic7700_fixed_rate_clks[7].hw },
	{ .hw = &eic7700_fixed_rate_clks[1].hw },
};

static const struct clk_parent_data mshcore_root_3mux1_0_mux_p[] = {
	{ .hw = &eic7700_fixed_rate_clks[3].hw },
	{ .hw = &eic7700_fixed_rate_clks[9].hw },
};

static const struct clk_parent_data mshcore_root_3mux1_1_mux_p[] = {
	{ .hw = &eic7700_fixed_rate_clks[3].hw },
	{ .hw = &eic7700_fixed_rate_clks[9].hw },
};

static const struct clk_parent_data mshcore_root_3mux1_2_mux_p[] = {
	{ .hw = &eic7700_fixed_rate_clks[3].hw },
	{ .hw = &eic7700_fixed_rate_clks[9].hw },
};

static const struct clk_parent_data npu_core_3mux1_gfree_mux_p[] = {
	{ .hw = &eic7700_fixed_rate_clks[4].hw },
	{ .hw = &eic7700_fixed_rate_clks[10].hw },
	{ .hw = &eic7700_fixed_rate_clks[8].hw },
};

static const struct clk_parent_data npu_e31_3mux1_gfree_mux_p[] = {
	{ .hw = &eic7700_fixed_rate_clks[4].hw },
	{ .hw = &eic7700_fixed_rate_clks[10].hw },
	{ .hw = &eic7700_fixed_rate_clks[8].hw },
};

static const struct clk_parent_data vi_aclk_root_2mux1_gfree_mux_p[] = {
	{ .hw = &eic7700_fixed_rate_clks[1].hw },
	{ .hw = &eic7700_fixed_rate_clks[7].hw },
};

static const struct clk_parent_data mux_vi_dw_root_2mux1_p[] = {
	{ .hw = &eic7700_fixed_rate_clks[10].hw },
	{ .hw = &eic7700_fixed_rate_clks[1].hw },
};

static const struct clk_parent_data mux_vi_dvp_root_2mux1_gfree_p[] = {
	{ .hw = &eic7700_fixed_rate_clks[10].hw },
	{ .hw = &eic7700_fixed_rate_clks[1].hw },
};

static const struct clk_parent_data mux_vi_dig_isp_root_2mux1_gfree_p[] = {
	{ .hw = &eic7700_fixed_rate_clks[10].hw },
	{ .hw = &eic7700_fixed_rate_clks[1].hw },
};

static const struct clk_parent_data mux_vo_aclk_root_2mux1_gfree_p[] = {
	{ .hw = &eic7700_fixed_rate_clks[1].hw },
	{ .hw = &eic7700_fixed_rate_clks[7].hw },
};

static const struct clk_parent_data mux_vo_pixel_root_2mux1_p[] = {
	{ .hw = &eic7700_fixed_rate_clks[10].hw },
	{ .hw = &eic7700_fixed_rate_clks[8].hw },
};

static const struct clk_parent_data mux_vcdec_root_2mux1_gfree_p[] = {
	{ .hw = &eic7700_fixed_rate_clks[1].hw },
	{ .hw = &eic7700_fixed_rate_clks[7].hw },
};

static const struct clk_parent_data mux_vcaclk_root_2mux1_gfree_p[] = {
	{ .hw = &eic7700_fixed_rate_clks[1].hw },
	{ .hw = &eic7700_fixed_rate_clks[7].hw },
};

static const struct clk_parent_data npu_llclk_3mux1_gfree_mux_p[] = {
	{ .hw = &eic7700_early_clks[1].hw },
	{ .hw = &eic7700_early_clks[2].hw },
	{ .hw = &eic7700_fixed_rate_clks[10].hw },
};

static const struct clk_parent_data mux_bootspi_clk_2mux1_gfree_p[] = {
	{ .hw = &eic7700_early_clks[4].hw },
	{ .index = 0 },
};

static const struct clk_parent_data mux_scpu_core_clk_2mux1_gfree_p[] = {
	{ .hw = &eic7700_early_clks[5].hw },
	{ .index = 0 },
};

static const struct clk_parent_data mux_lpcpu_core_clk_2mux1_gfree_p[] = {
	{ .hw = &eic7700_early_clks[6].hw },
	{ .index = 0 },
};

static const struct clk_parent_data mux_vo_mclk_2mux_ext_mclk_p[] = {
	{ .hw = &eic7700_early_clks[7].hw },
	{ .hw = &eic7700_fixed_rate_clks[15].hw },
};

static const struct clk_parent_data mux_aondma_axi2mux1_gfree_p[] = {
	{ .hw = &eic7700_early_clks[8].hw },
	{ .index = 0 },
};

static const struct clk_parent_data mux_rmii_ref_2mux1_p[] = {
	{ .hw = &eic7700_early_clks[0].hw },
	{ .hw = &eic7700_fixed_rate_clks[16].hw },
};

static const struct clk_parent_data mux_eth_core_2mux1_p[] = {
	{ .hw = &eic7700_fixed_rate_clks[6].hw },
	{ .hw = &eic7700_fixed_rate_clks[16].hw },
};

static const struct clk_parent_data mux_sata_phy_2mux1_p[] = {
	{ .hw = &eic7700_early_clks[9].hw },
	{ .hw = &eic7700_fixed_rate_clks[16].hw },
};

static const struct clk_parent_data mux_syscfg_clk_root_2mux1_gfree_p[] = {
	{ .hw = &eic7700_early_clks[10].hw },
	{ .index = 0 },
};

static const struct clk_parent_data mux_cpu_root_3mux1_gfree_p[] = {
	{ .hw = &eic7700_pll_clks[1].hw },
	{ .hw = &eic7700_early_clks[11].hw },
	{ .index = 0 },
};

static struct eswin_mux_clock eic7700_mux_clks[] = {
	ESWIN_MUX(EIC7700_CLK_MUX_CPU_ROOT_3MUX1_GFREE,
		  "mux_cpu_root_3mux1_gfree", mux_cpu_root_3mux1_gfree_p,
		  ARRAY_SIZE(mux_cpu_root_3mux1_gfree_p),
		  CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_U84, 0, 2,
		  CLK_MUX_ROUND_CLOSEST),
	ESWIN_MUX(EIC7700_CLK_MUX_RMII_REF_2MUX, "mux_rmii_ref_2mux1",
		  mux_rmii_ref_2mux1_p, ARRAY_SIZE(mux_rmii_ref_2mux1_p),
		  CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_ETH0, 2, 1,
		  CLK_MUX_ROUND_CLOSEST),
	ESWIN_MUX(EIC7700_CLK_MUX_DSP_ACLK_ROOT_2MUX1_GFREE,
		  "mux_dsp_aclk_root_2mux1_gfree",
		  dsp_aclk_root_2mux1_gfree_mux_p,
		  ARRAY_SIZE(dsp_aclk_root_2mux1_gfree_mux_p),
		  CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_DSP_ACLK, 0, 1,
		  CLK_MUX_ROUND_CLOSEST),
	ESWIN_MUX(EIC7700_CLK_MUX_D2D_ACLK_ROOT_2MUX1_GFREE,
		  "mux_d2d_aclk_root_2mux1_gfree",
		  d2d_aclk_root_2mux1_gfree_mux_p,
		  ARRAY_SIZE(d2d_aclk_root_2mux1_gfree_mux_p),
		  CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_D2D_ACLK, 0, 1,
		  CLK_MUX_ROUND_CLOSEST),
	ESWIN_MUX(EIC7700_CLK_MUX_DDR_ACLK_ROOT_2MUX1_GFREE,
		  "mux_ddr_aclk_root_2mux1_gfree",
		  ddr_aclk_root_2mux1_gfree_mux_p,
		  ARRAY_SIZE(ddr_aclk_root_2mux1_gfree_mux_p),
		  CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_DDR, 16, 1,
		  CLK_MUX_ROUND_CLOSEST),
	ESWIN_MUX(EIC7700_CLK_MUX_MSHCORE_ROOT_3MUX1_0,
		  "mux_mshcore_root_3mux1_0", mshcore_root_3mux1_0_mux_p,
		  ARRAY_SIZE(mshcore_root_3mux1_0_mux_p), CLK_SET_RATE_PARENT,
		  EIC7700_REG_OFFSET_MSHC0_CORE, 0, 1, CLK_MUX_ROUND_CLOSEST),
	ESWIN_MUX(EIC7700_CLK_MUX_MSHCORE_ROOT_3MUX1_1,
		  "mux_mshcore_root_3mux1_1", mshcore_root_3mux1_1_mux_p,
		  ARRAY_SIZE(mshcore_root_3mux1_1_mux_p), CLK_SET_RATE_PARENT,
		  EIC7700_REG_OFFSET_MSHC1_CORE, 0, 1, CLK_MUX_ROUND_CLOSEST),
	ESWIN_MUX(EIC7700_CLK_MUX_MSHCORE_ROOT_3MUX1_2,
		  "mux_mshcore_root_3mux1_2", mshcore_root_3mux1_2_mux_p,
		  ARRAY_SIZE(mshcore_root_3mux1_2_mux_p), CLK_SET_RATE_PARENT,
		  EIC7700_REG_OFFSET_MSHC2_CORE, 0, 1, CLK_MUX_ROUND_CLOSEST),
	ESWIN_MUX(EIC7700_CLK_MUX_NPU_LLCLK_3MUX1_GFREE,
		  "mux_npu_llclk_3mux1_gfree", npu_llclk_3mux1_gfree_mux_p,
		  ARRAY_SIZE(npu_llclk_3mux1_gfree_mux_p),
		  CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_NPU_LLC, 0, 2,
		  CLK_MUX_ROUND_CLOSEST),
	ESWIN_MUX(EIC7700_CLK_MUX_NPU_CORE_3MUX1_GFREE,
		  "mux_npu_core_3mux1_gfree", npu_core_3mux1_gfree_mux_p,
		  ARRAY_SIZE(npu_core_3mux1_gfree_mux_p), CLK_SET_RATE_PARENT,
		  EIC7700_REG_OFFSET_NPU_CORE, 0, 2, CLK_MUX_ROUND_CLOSEST),
	ESWIN_MUX(EIC7700_CLK_MUX_NPU_E31_3MUX1_GFREE,
		  "mux_npu_e31_3mux1_gfree", npu_e31_3mux1_gfree_mux_p,
		  ARRAY_SIZE(npu_e31_3mux1_gfree_mux_p), CLK_SET_RATE_PARENT,
		  EIC7700_REG_OFFSET_NPU_CORE, 8, 2, CLK_MUX_ROUND_CLOSEST),
	ESWIN_MUX(EIC7700_CLK_MUX_VI_ACLK_ROOT_2MUX1_GFREE,
		  "mux_vi_aclk_root_2mux1_gfree",
		  vi_aclk_root_2mux1_gfree_mux_p,
		  ARRAY_SIZE(vi_aclk_root_2mux1_gfree_mux_p),
		  CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_VI_ACLK, 0, 1,
		  CLK_MUX_ROUND_CLOSEST),
	ESWIN_MUX(EIC7700_CLK_MUX_VI_DW_ROOT_2MUX1, "mux_vi_dw_root_2mux1",
		  mux_vi_dw_root_2mux1_p, ARRAY_SIZE(mux_vi_dw_root_2mux1_p),
		  CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_VI_DWCLK, 0, 1,
		  CLK_MUX_ROUND_CLOSEST),
	ESWIN_MUX(EIC7700_CLK_MUX_VI_DVP_ROOT_2MUX1_GFREE,
		  "mux_vi_dvp_root_2mux1_gfree",
		  mux_vi_dvp_root_2mux1_gfree_p,
		  ARRAY_SIZE(mux_vi_dvp_root_2mux1_gfree_p),
		  CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_VI_DVP, 0, 1,
		  CLK_MUX_ROUND_CLOSEST),
	ESWIN_MUX(EIC7700_CLK_MUX_VI_DIG_ISP_ROOT_2MUX1_GFREE,
		  "mux_vi_dig_isp_root_2mux1_gfree",
		  mux_vi_dig_isp_root_2mux1_gfree_p,
		  ARRAY_SIZE(mux_vi_dig_isp_root_2mux1_gfree_p),
		  CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_VI_DIG_ISP, 0, 1,
		  CLK_MUX_ROUND_CLOSEST),
	ESWIN_MUX(EIC7700_CLK_MUX_VO_ACLK_ROOT_2MUX1_GFREE,
		  "mux_vo_aclk_root_2mux1_gfree",
		  mux_vo_aclk_root_2mux1_gfree_p,
		  ARRAY_SIZE(mux_vo_aclk_root_2mux1_gfree_p),
		  CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_VO_ACLK, 0, 1,
		  CLK_MUX_ROUND_CLOSEST),
	ESWIN_MUX(EIC7700_CLK_MUX_VO_PIXEL_ROOT_2MUX1,
		  "mux_vo_pixel_root_2mux1", mux_vo_pixel_root_2mux1_p,
		  ARRAY_SIZE(mux_vo_pixel_root_2mux1_p), CLK_SET_RATE_PARENT,
		  EIC7700_REG_OFFSET_VO_PIXEL, 0, 1, CLK_MUX_ROUND_CLOSEST),
	ESWIN_MUX(EIC7700_CLK_MUX_VCDEC_ROOT_2MUX1_GFREE,
		  "mux_vcdec_root_2mux1_gfree", mux_vcdec_root_2mux1_gfree_p,
		  ARRAY_SIZE(mux_vcdec_root_2mux1_gfree_p),
		  CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_VCDEC_ROOT, 0, 1,
		  CLK_MUX_ROUND_CLOSEST),
	ESWIN_MUX(EIC7700_CLK_MUX_VCACLK_ROOT_2MUX1_GFREE,
		  "mux_vcaclk_root_2mux1_gfree",
		  mux_vcaclk_root_2mux1_gfree_p,
		  ARRAY_SIZE(mux_vcaclk_root_2mux1_gfree_p),
		  CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_VC_ACLK, 0, 1,
		  CLK_MUX_ROUND_CLOSEST),
	ESWIN_MUX(EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
		  "mux_syscfg_clk_root_2mux1_gfree",
		  mux_syscfg_clk_root_2mux1_gfree_p,
		  ARRAY_SIZE(mux_syscfg_clk_root_2mux1_gfree_p),
		  CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_SYSCFG, 0, 1,
		  CLK_MUX_ROUND_CLOSEST),
	ESWIN_MUX(EIC7700_CLK_MUX_BOOTSPI_CLK_2MUX1_GFREE,
		  "mux_bootspi_clk_2mux1_gfree",
		  mux_bootspi_clk_2mux1_gfree_p,
		  ARRAY_SIZE(mux_bootspi_clk_2mux1_gfree_p),
		  CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_BOOTSPI, 0, 1,
		  CLK_MUX_ROUND_CLOSEST),
	ESWIN_MUX(EIC7700_CLK_MUX_SCPU_CORE_CLK_2MUX1_GFREE,
		  "mux_scpu_core_clk_2mux1_gfree",
		  mux_scpu_core_clk_2mux1_gfree_p,
		  ARRAY_SIZE(mux_scpu_core_clk_2mux1_gfree_p),
		  CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_SCPU_CORE, 0, 1,
		  CLK_MUX_ROUND_CLOSEST),
	ESWIN_MUX(EIC7700_CLK_MUX_LPCPU_CORE_CLK_2MUX1_GFREE,
		  "mux_lpcpu_core_clk_2mux1_gfree",
		  mux_lpcpu_core_clk_2mux1_gfree_p,
		  ARRAY_SIZE(mux_lpcpu_core_clk_2mux1_gfree_p),
		  CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LPCPU_CORE, 0, 1,
		  CLK_MUX_ROUND_CLOSEST),
	ESWIN_MUX(EIC7700_CLK_MUX_VO_MCLK_2MUX_EXT_MCLK,
		  "mux_vo_mclk_2mux_ext_mclk", mux_vo_mclk_2mux_ext_mclk_p,
		  ARRAY_SIZE(mux_vo_mclk_2mux_ext_mclk_p),
		  CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_VO_MCLK, 0, 1,
		  CLK_MUX_ROUND_CLOSEST),
	ESWIN_MUX(EIC7700_CLK_MUX_AONDMA_AXI2MUX1_GFREE,
		  "mux_aondma_axi2mux1_gfree", mux_aondma_axi2mux1_gfree_p,
		  ARRAY_SIZE(mux_aondma_axi2mux1_gfree_p),
		  CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_AON_DMA, 0, 1,
		  CLK_MUX_ROUND_CLOSEST),
	ESWIN_MUX(EIC7700_CLK_MUX_ETH_CORE_2MUX1, "mux_eth_core_2mux1",
		  mux_eth_core_2mux1_p, ARRAY_SIZE(mux_eth_core_2mux1_p),
		  CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_ETH0, 1, 1,
		  CLK_MUX_ROUND_CLOSEST),
	ESWIN_MUX(EIC7700_CLK_MUX_SATA_PHY_2MUX1, "mux_sata_phy_2mux1",
		  mux_sata_phy_2mux1_p, ARRAY_SIZE(mux_sata_phy_2mux1_p),
		  CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_SATA_OOB, 9, 1,
		  CLK_MUX_ROUND_CLOSEST),
};

static const struct clk_parent_data mux_cpu_aclk_2mux1_gfree_p[] = {
	{ .hw = &eic7700_mux_clks[1].hw },
	{ .hw = &eic7700_mux_clks[0].hw },
};

static struct eswin_clk_info eic7700_clks[] = {
	ESWIN_MUX_TYPE(EIC7700_CLK_MUX_CPU_ACLK_2MUX1_GFREE,
		       "mux_cpu_aclk_2mux1_gfree", mux_cpu_aclk_2mux1_gfree_p,
		       ARRAY_SIZE(mux_cpu_aclk_2mux1_gfree_p),
		       CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_U84, 20, 1,
		       CLK_MUX_ROUND_CLOSEST, NULL),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_CPU_TRACE_COM_CLK,
			"gate_clk_cpu_trace_com_clk",
			EIC7700_CLK_MUX_CPU_ACLK_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_U84, 23, 0),
	ESWIN_FACTOR_TYPE(EIC7700_CLK_FIXED_FACTOR_CPU_DIV2,
			  "fixed_factor_cpu_div2",
			  EIC7700_CLK_MUX_CPU_ROOT_3MUX1_GFREE, 1, 2, 0),
	ESWIN_FACTOR_TYPE(EIC7700_CLK_FIXED_FACTOR_MIPI_TXESC_DIV10,
			  "fixed_factor_mipi_txesc_div10",
			  EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE, 1, 10,
			  0),
	ESWIN_FACTOR_TYPE(EIC7700_CLK_FIXED_FACTOR_SCPU_BUS_DIV2,
			  "fixed_factor_scpu_bus_div2",
			  EIC7700_CLK_MUX_SCPU_CORE_CLK_2MUX1_GFREE, 1, 2, 0),
	ESWIN_FACTOR_TYPE(EIC7700_CLK_FIXED_FACTOR_LPCPU_BUS_DIV2,
			  "fixed_factor_lpcpu_bus_div2",
			  EIC7700_CLK_MUX_LPCPU_CORE_CLK_2MUX1_GFREE, 1, 2, 0),
	ESWIN_FACTOR_TYPE(EIC7700_CLK_FIXED_FACTOR_PCIE_CR_DIV2,
			  "fixed_factor_pcie_cr_div2",
			  EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE, 1, 2, 0),
	ESWIN_FACTOR_TYPE(EIC7700_CLK_FIXED_FACTOR_PCIE_AUX_DIV4,
			  "fixed_factor_pcie_aux_div4",
			  EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE, 1, 4, 0),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_D2D_ACLK_DYNM,
		       "divider_d2d_aclk_div_dynm",
		       EIC7700_CLK_MUX_D2D_ACLK_ROOT_2MUX1_GFREE, 0,
		       EIC7700_REG_OFFSET_D2D_ACLK, 4, 4, 0,
		       ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_DSP_ACLK_DYNM,
		       "divider_dsp_aclk_div_dynm",
		       EIC7700_CLK_MUX_DSP_ACLK_ROOT_2MUX1_GFREE, 0,
		       EIC7700_REG_OFFSET_DSP_ACLK, 4, 4, 0,
		       ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_DDR_ACLK_DYNM,
		       "divider_ddr_aclk_div_dynm",
		       EIC7700_CLK_MUX_DDR_ACLK_ROOT_2MUX1_GFREE, 0,
		       EIC7700_REG_OFFSET_DDR, 20, 4, 0, ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_ETH_TXCLK_DYNM_0,
		       "divider_eth_txclk_div_dynm_0",
		       EIC7700_CLK_MUX_ETH_CORE_2MUX1, 0,
		       EIC7700_REG_OFFSET_ETH0, 4, 7, 0, ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_ETH_TXCLK_DYNM_1,
		       "divider_eth_txclk_div_dynm_1",
		       EIC7700_CLK_MUX_ETH_CORE_2MUX1, 0,
		       EIC7700_REG_OFFSET_ETH1, 4, 7, 0, ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_MSHC_CORE_DYNM_0,
		       "divider_mshc_core_div_dynm_0",
		       EIC7700_CLK_MUX_MSHCORE_ROOT_3MUX1_0,
		       0, EIC7700_REG_OFFSET_MSHC0_CORE, 4, 12,
		       CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO, 0),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_MSHC_CORE_DYNM_1,
		       "divider_mshc_core_div_dynm_1",
		       EIC7700_CLK_MUX_MSHCORE_ROOT_3MUX1_1,
		       0, EIC7700_REG_OFFSET_MSHC1_CORE, 4, 12,
		       CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO, 0),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_MSHC_CORE_DYNM_2,
		       "divider_mshc_core_div_dynm_2",
		       EIC7700_CLK_MUX_MSHCORE_ROOT_3MUX1_2,
		       0, EIC7700_REG_OFFSET_MSHC2_CORE, 4, 12,
		       CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO, 0),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_NPU_CORECLK_DYNM,
		       "divider_npu_coreclk_div_dynm",
		       EIC7700_CLK_MUX_NPU_CORE_3MUX1_GFREE,
		       0, EIC7700_REG_OFFSET_NPU_CORE, 4, 4,
		       CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO, 0),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_NPU_E31_DYNM, "divider_npu_e31_div_dynm",
		       EIC7700_CLK_MUX_NPU_E31_3MUX1_GFREE, 0,
		       EIC7700_REG_OFFSET_NPU_CORE, 12, 4,
		       CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO, 0),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_VI_ACLK_DYNM, "divider_vi_aclk_div_dynm",
		       EIC7700_CLK_MUX_VI_ACLK_ROOT_2MUX1_GFREE, 0,
		       EIC7700_REG_OFFSET_VI_ACLK, 4, 4, 0,
		       ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_VI_DW_DYNM, "divider_vi_dw_div_dynm",
		       EIC7700_CLK_MUX_VI_DW_ROOT_2MUX1, 0,
		       EIC7700_REG_OFFSET_VI_DWCLK, 4, 4, 0,
		       ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_VI_DVP_DYNM, "divider_vi_dvp_div_dynm",
		       EIC7700_CLK_MUX_VI_DVP_ROOT_2MUX1_GFREE, 0,
		       EIC7700_REG_OFFSET_VI_DVP, 4, 4, 0,
		       ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_VI_DIG_ISP_DYNM,
		       "divider_vi_dig_isp_div_dynm",
		       EIC7700_CLK_MUX_VI_DIG_ISP_ROOT_2MUX1_GFREE, 0,
		       EIC7700_REG_OFFSET_VI_DIG_ISP, 4, 4, 0,
		       ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_VO_ACLK_DYNM, "divider_vo_aclk_div_dynm",
		       EIC7700_CLK_MUX_VO_ACLK_ROOT_2MUX1_GFREE, 0,
		       EIC7700_REG_OFFSET_VO_ACLK, 4, 4, 0,
		       ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_VO_PIXEL_DYNM,
		       "divider_vo_pixel_div_dynm",
		       EIC7700_CLK_MUX_VO_PIXEL_ROOT_2MUX1, 0,
		       EIC7700_REG_OFFSET_VO_PIXEL, 4, 6, 0,
		       ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_VC_ACLK_DYNM, "divider_vc_aclk_div_dynm",
		       EIC7700_CLK_MUX_VCACLK_ROOT_2MUX1_GFREE, 0,
		       EIC7700_REG_OFFSET_VC_ACLK, 4, 4, 0,
		       ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_JD_DYNM, "divider_jd_div_dynm",
		       EIC7700_CLK_MUX_VCDEC_ROOT_2MUX1_GFREE, 0,
		       EIC7700_REG_OFFSET_JD, 4, 4, 0, ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_JE_DYNM, "divider_je_div_dynm",
		       EIC7700_CLK_MUX_VCDEC_ROOT_2MUX1_GFREE, 0,
		       EIC7700_REG_OFFSET_JE, 4, 4, 0, ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_VE_DYNM, "divider_ve_div_dynm",
		       EIC7700_CLK_MUX_VCDEC_ROOT_2MUX1_GFREE, 0,
		       EIC7700_REG_OFFSET_VE, 4, 4, 0, ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_VD_DYNM, "divider_vd_div_dynm",
		       EIC7700_CLK_MUX_VCDEC_ROOT_2MUX1_GFREE, 0,
		       EIC7700_REG_OFFSET_VD, 4, 4, 0, ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_G2D_DYNM, "divider_g2d_div_dynm",
		       EIC7700_CLK_MUX_DSP_ACLK_ROOT_2MUX1_GFREE, 0,
		       EIC7700_REG_OFFSET_G2D, 4, 4, 0, ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_NOC_NSP_DYNM, "divider_noc_nsp_div_dynm",
		       EIC7700_CLK_SPLL2_FOUT1, 0, EIC7700_REG_OFFSET_NOC, 0, 3,
		       0, ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_GPU_ACLK_DYNM,
		       "divider_gpu_aclk_div_dynm", EIC7700_CLK_SPLL0_FOUT1, 0,
		       EIC7700_REG_OFFSET_GPU_ACLK, 4, 4, 0,
		       ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_HSP_ACLK_DYNM,
		       "divider_hsp_aclk_div_dynm", EIC7700_CLK_SPLL0_FOUT1, 0,
		       EIC7700_REG_OFFSET_HSP_ACLK, 4, 4, 0,
		       ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_PCIE_ACLK_DYNM,
		       "divider_pcie_aclk_div_dynm", EIC7700_CLK_SPLL2_FOUT2, 0,
		       EIC7700_REG_OFFSET_PCIE_ACLK, 4, 4, 0,
		       ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_NPU_ACLK_DYNM,
		       "divider_npu_aclk_div_dynm", EIC7700_CLK_SPLL0_FOUT1, 0,
		       EIC7700_REG_OFFSET_NPU_ACLK, 4,  4, 0,
		       ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_VI_SHUTTER_DYNM_0,
		       "divider_vi_shutter_div_dynm_0",
		       EIC7700_CLK_VPLL_FOUT2, 0,
		       EIC7700_REG_OFFSET_VI_SHUTTER0, 4, 7, 0,
		       ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_VI_SHUTTER_DYNM_1,
		       "divider_vi_shutter_div_dynm_1",
		       EIC7700_CLK_VPLL_FOUT2, 0,
		       EIC7700_REG_OFFSET_VI_SHUTTER1, 4, 7, 0,
		       ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_VI_SHUTTER_DYNM_2,
		       "divider_vi_shutter_div_dynm_2",
		       EIC7700_CLK_VPLL_FOUT2, 0,
		       EIC7700_REG_OFFSET_VI_SHUTTER2, 4, 7, 0,
		       ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_VI_SHUTTER_DYNM_3,
		       "divider_vi_shutter_div_dynm_3",
		       EIC7700_CLK_VPLL_FOUT2, 0,
		       EIC7700_REG_OFFSET_VI_SHUTTER3, 4, 7, 0,
		       ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_VI_SHUTTER_DYNM_4,
		       "divider_vi_shutter_div_dynm_4",
		       EIC7700_CLK_VPLL_FOUT2, 0,
		       EIC7700_REG_OFFSET_VI_SHUTTER4, 4, 7, 0,
		       ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_VI_SHUTTER_DYNM_5,
		       "divider_vi_shutter_div_dynm_5",
		       EIC7700_CLK_VPLL_FOUT2, 0,
		       EIC7700_REG_OFFSET_VI_SHUTTER5, 4, 7, 0,
		       ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_IESMCLK_DYNM, "divider_iesmclk_div_dynm",
		       EIC7700_CLK_SPLL0_FOUT3, 0,
		       EIC7700_REG_OFFSET_VO_IESMCLK, 4, 4, 0,
		       ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_VO_CEC_DYNM, "divider_vo_cec_div_dynm",
		       EIC7700_CLK_VPLL_FOUT2, 0,
		       EIC7700_REG_OFFSET_VO_PHY_CLK, 16, 16, 0,
		       ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_CRYPTO_DYNM, "divider_crypto_div_dynm",
		       EIC7700_CLK_SPLL0_FOUT1, 0,
		       EIC7700_REG_OFFSET_SPACC, 4, 4, 0, ESWIN_PRIV_DIV_MIN_2),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_AON_RTC_DYNM, "divider_aon_rtc_div_dynm",
		       EIC7700_CLK_FIXED_FACTOR_CLK_1M_DIV24, 0,
		       EIC7700_REG_OFFSET_RTC, 21, 11, 0,
		       ESWIN_PRIV_DIV_MIN_2),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_DSPT_ACLK, "gate_dspt_aclk",
			EIC7700_CLK_DIV_DSP_ACLK_DYNM, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_DSP_ACLK, 31, 0),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_DSP_0_ACLK_DYNM,
		       "divider_dsp_0_aclk_div_dynm",
		       EIC7700_CLK_GATE_DSPT_ACLK, 0,
		       EIC7700_REG_OFFSET_DSP_CFG, 19, 1, 0, 0),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_DSP_1_ACLK_DYNM,
		       "divider_dsp_1_aclk_div_dynm",
		       EIC7700_CLK_GATE_DSPT_ACLK, 0,
		       EIC7700_REG_OFFSET_DSP_CFG, 20, 1, 0, 0),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_DSP_2_ACLK_DYNM,
		       "divider_dsp_2_aclk_div_dynm",
		       EIC7700_CLK_GATE_DSPT_ACLK, 0,
		       EIC7700_REG_OFFSET_DSP_CFG, 21, 1, 0, 0),
	ESWIN_DIV_TYPE(EIC7700_CLK_DIV_DSP_3_ACLK_DYNM,
		       "divider_dsp_3_aclk_div_dynm",
		       EIC7700_CLK_GATE_DSPT_ACLK, 0,
		       EIC7700_REG_OFFSET_DSP_CFG, 22, 1, 0, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_CPU_EXT_SRC_CORE_CLK_0,
			"gate_clk_cpu_ext_src_core_clk_0",
			EIC7700_CLK_MUX_CPU_ROOT_3MUX1_GFREE,
			CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
			EIC7700_REG_OFFSET_U84, 28, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_CPU_EXT_SRC_CORE_CLK_1,
			"gate_clk_cpu_ext_src_core_clk_1",
			EIC7700_CLK_MUX_CPU_ROOT_3MUX1_GFREE,
			CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
			EIC7700_REG_OFFSET_U84, 29, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_CPU_EXT_SRC_CORE_CLK_2,
			"gate_clk_cpu_ext_src_core_clk_2",
			EIC7700_CLK_MUX_CPU_ROOT_3MUX1_GFREE,
			CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
			EIC7700_REG_OFFSET_U84, 30, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_CPU_EXT_SRC_CORE_CLK_3,
			"gate_clk_cpu_ext_src_core_clk_3",
			EIC7700_CLK_MUX_CPU_ROOT_3MUX1_GFREE,
			CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
			EIC7700_REG_OFFSET_U84, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_CPU_TRACE_CLK_0,
			"gate_clk_cpu_trace_clk_0",
			EIC7700_CLK_MUX_CPU_ROOT_3MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_U84, 24, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_CPU_TRACE_CLK_1,
			"gate_clk_cpu_trace_clk_1",
			EIC7700_CLK_MUX_CPU_ROOT_3MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_U84, 25, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_CPU_TRACE_CLK_2,
			"gate_clk_cpu_trace_clk_2",
			EIC7700_CLK_MUX_CPU_ROOT_3MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_U84, 26, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_CPU_TRACE_CLK_3,
			"gate_clk_cpu_trace_clk_3",
			EIC7700_CLK_MUX_CPU_ROOT_3MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_U84, 27, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_NOC_NSP_CLK, "gate_noc_nsp_clk",
			EIC7700_CLK_DIV_NOC_NSP_DYNM,
			CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
			EIC7700_REG_OFFSET_NOC, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_BOOTSPI, "gate_clk_bootspi",
			EIC7700_CLK_MUX_BOOTSPI_CLK_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_BOOTSPI, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_BOOTSPI_CFG, "gate_clk_bootspi_cfg",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_BOOTSPI_CFGCLK,
			31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_SCPU_CORE, "gate_clk_scpu_core",
			EIC7700_CLK_MUX_SCPU_CORE_CLK_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_SCPU_CORE, 31,
			0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_SCPU_BUS, "gate_clk_scpu_bus",
			EIC7700_CLK_FIXED_FACTOR_SCPU_BUS_DIV2,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_SCPU_BUSCLK, 31,
			0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_LPCPU_CORE, "gate_clk_lpcpu_core",
			EIC7700_CLK_MUX_LPCPU_CORE_CLK_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LPCPU_CORE, 31,
			0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_LPCPU_BUS, "gate_clk_lpcpu_bus",
			EIC7700_CLK_FIXED_FACTOR_LPCPU_BUS_DIV2,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LPCPU_BUSCLK,
			31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_GPU_ACLK, "gate_gpu_aclk",
			EIC7700_CLK_DIV_GPU_ACLK_DYNM, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_GPU_ACLK, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_GPU_CFG_CLK, "gate_gpu_cfg_clk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_GPU_CFG, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_DSPT_CFG_CLK, "gate_dspt_cfg_clk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_DSP_CFG, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_D2D_ACLK, "gate_d2d_aclk",
			EIC7700_CLK_DIV_D2D_ACLK_DYNM, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_D2D_ACLK, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_D2D_CFG_CLK, "gate_d2d_cfg_clk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_D2D_CFG, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_TCU_ACLK, "gate_tcu_aclk",
			EIC7700_CLK_DIV_DDR_ACLK_DYNM, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_TCU_ACLK, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_TCU_CFG_CLK, "gate_tcu_cfg_clk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_TCU_CFG, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_DDRT_CFG_CLK, "gate_ddrt_cfg_clk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_DDR, 9, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_DDRT0_P0_ACLK, "gate_ddrt0_p0_aclk",
			EIC7700_CLK_DIV_DDR_ACLK_DYNM,
			CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
			EIC7700_REG_OFFSET_DDR, 4, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_DDRT0_P1_ACLK, "gate_ddrt0_p1_aclk",
			EIC7700_CLK_DIV_DDR_ACLK_DYNM,
			CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
			EIC7700_REG_OFFSET_DDR, 5, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_DDRT0_P2_ACLK, "gate_ddrt0_p2_aclk",
			EIC7700_CLK_DIV_DDR_ACLK_DYNM,
			CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
			EIC7700_REG_OFFSET_DDR, 6, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_DDRT0_P3_ACLK, "gate_ddrt0_p3_aclk",
			EIC7700_CLK_DIV_DDR_ACLK_DYNM,
			CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
			EIC7700_REG_OFFSET_DDR, 7, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_DDRT0_P4_ACLK, "gate_ddrt0_p4_aclk",
			EIC7700_CLK_DIV_DDR_ACLK_DYNM,
			CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
			EIC7700_REG_OFFSET_DDR, 8, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_DDRT1_P0_ACLK, "gate_ddrt1_p0_aclk",
			EIC7700_CLK_DIV_DDR_ACLK_DYNM,
			CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
			EIC7700_REG_OFFSET_DDR1, 4, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_DDRT1_P1_ACLK, "gate_ddrt1_p1_aclk",
			EIC7700_CLK_DIV_DDR_ACLK_DYNM,
			CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
			EIC7700_REG_OFFSET_DDR1, 5, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_DDRT1_P2_ACLK, "gate_ddrt1_p2_aclk",
			EIC7700_CLK_DIV_DDR_ACLK_DYNM,
			CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
			EIC7700_REG_OFFSET_DDR1, 6, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_DDRT1_P3_ACLK, "gate_ddrt1_p3_aclk",
			EIC7700_CLK_DIV_DDR_ACLK_DYNM,
			CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
			EIC7700_REG_OFFSET_DDR1, 7, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_DDRT1_P4_ACLK, "gate_ddrt1_p4_aclk",
			EIC7700_CLK_DIV_DDR_ACLK_DYNM,
			CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
			EIC7700_REG_OFFSET_DDR1, 8, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_HSP_ACLK, "gate_clk_hsp_aclk",
			EIC7700_CLK_DIV_HSP_ACLK_DYNM, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_HSP_ACLK, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_HSP_CFG_CLK, "gate_clk_hsp_cfg_clk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_HSP_CFG, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_PCIET_ACLK, "gate_pciet_aclk",
			EIC7700_CLK_DIV_PCIE_ACLK_DYNM, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_PCIE_ACLK, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_PCIET_CFG_CLK, "gate_pciet_cfg_clk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_PCIE_CFG, 31,
			0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_PCIET_CR_CLK, "gate_pciet_cr_clk",
			EIC7700_CLK_FIXED_FACTOR_PCIE_CR_DIV2,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_PCIE_CFG, 0, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_PCIET_AUX_CLK, "gate_pciet_aux_clk",
			EIC7700_CLK_FIXED_FACTOR_PCIE_AUX_DIV4,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_PCIE_CFG, 1, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_NPU_ACLK, "gate_npu_aclk",
			EIC7700_CLK_DIV_NPU_ACLK_DYNM, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_NPU_ACLK, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_NPU_CFG_CLK, "gate_npu_cfg_clk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_NPU_ACLK, 30,
			0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_NPU_LLC_ACLK, "gate_npu_llc_aclk",
			EIC7700_CLK_MUX_NPU_LLCLK_3MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_NPU_LLC, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_NPU_CLK, "gate_npu_clk",
			EIC7700_CLK_DIV_NPU_CORECLK_DYNM, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_NPU_CORE, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_NPU_E31_CLK, "gate_npu_e31_clk",
			EIC7700_CLK_DIV_NPU_E31_DYNM, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_NPU_CORE, 30, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_VI_ACLK, "gate_vi_aclk",
			EIC7700_CLK_DIV_VI_ACLK_DYNM, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_VI_ACLK, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_VI_CFG_CLK, "gate_vi_cfg_clk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_VI_ACLK, 30, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_VI_DIG_DW_CLK, "gate_vi_dig_dw_clk",
			EIC7700_CLK_DIV_VI_DW_DYNM, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_VI_DWCLK, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_VI_DVP_CLK, "gate_vi_dvp_clk",
			EIC7700_CLK_DIV_VI_DVP_DYNM, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_VI_DVP, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_VI_DIG_ISP_CLK, "gate_vi_dig_isp_clk",
			EIC7700_CLK_DIV_VI_DIG_ISP_DYNM, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_VI_DIG_ISP, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_VI_SHUTTER_0, "gate_vi_shutter_0",
			EIC7700_CLK_DIV_VI_SHUTTER_DYNM_0, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_VI_SHUTTER0, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_VI_SHUTTER_1, "gate_vi_shutter_1",
			EIC7700_CLK_DIV_VI_SHUTTER_DYNM_1, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_VI_SHUTTER1, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_VI_SHUTTER_2, "gate_vi_shutter_2",
			EIC7700_CLK_DIV_VI_SHUTTER_DYNM_2, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_VI_SHUTTER2, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_VI_SHUTTER_3, "gate_vi_shutter_3",
			EIC7700_CLK_DIV_VI_SHUTTER_DYNM_3, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_VI_SHUTTER3, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_VI_SHUTTER_4, "gate_vi_shutter_4",
			EIC7700_CLK_DIV_VI_SHUTTER_DYNM_4, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_VI_SHUTTER4, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_VI_SHUTTER_5, "gate_vi_shutter_5",
			EIC7700_CLK_DIV_VI_SHUTTER_DYNM_5, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_VI_SHUTTER5, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_VI_PHY_TXCLKESC,
			"gate_vi_phy_txclkesc",
			EIC7700_CLK_FIXED_FACTOR_MIPI_TXESC_DIV10,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_VI_PHY, 0, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_VO_ACLK, "gate_vo_aclk",
			EIC7700_CLK_DIV_VO_ACLK_DYNM, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_VO_ACLK, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_VO_CFG_CLK, "gate_vo_cfg_clk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_VO_ACLK, 30, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_VO_HDMI_IESMCLK,
			"gate_vo_hdmi_iesmclk", EIC7700_CLK_DIV_IESMCLK_DYNM,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_VO_IESMCLK, 31,
			0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_VO_PIXEL_CLK, "gate_vo_pixel_clk",
			EIC7700_CLK_DIV_VO_PIXEL_DYNM, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_VO_PIXEL, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_VO_I2S_MCLK, "gate_vo_i2s_mclk",
			EIC7700_CLK_MUX_VO_MCLK_2MUX_EXT_MCLK,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_VO_MCLK, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_VO_CR_CLK, "gate_vo_cr_clk",
			EIC7700_CLK_FIXED_FACTOR_MIPI_TXESC_DIV10,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_VO_PHY_CLK, 1,
			0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_VC_ACLK, "gate_vc_aclk",
			EIC7700_CLK_DIV_VC_ACLK_DYNM, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_VC_ACLK, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_VC_CFG_CLK, "gate_vc_cfg_clk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_VC_CLKEN, 0, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_VC_JE_CLK, "gate_vc_je_clk",
			EIC7700_CLK_DIV_JE_DYNM, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_JE, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_VC_JD_CLK, "gate_vc_jd_clk",
			EIC7700_CLK_DIV_JD_DYNM, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_JD, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_VC_VE_CLK, "gate_vc_ve_clk",
			EIC7700_CLK_DIV_VE_DYNM, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_VE, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_VC_VD_CLK, "gate_vc_vd_clk",
			EIC7700_CLK_DIV_VD_DYNM, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_VD, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_G2D_CFG_CLK, "gate_g2d_cfg_clk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_G2D, 28, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_G2D_CLK, "gate_g2d_clk",
			EIC7700_CLK_DIV_G2D_DYNM, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_G2D, 30, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_G2D_ACLK, "gate_g2d_aclk",
			EIC7700_CLK_DIV_G2D_DYNM, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_G2D, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_AONDMA_CFG, "gate_clk_aondma_cfg",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_AON_DMA, 30, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_AONDMA_ACLK, "gate_aondma_aclk",
			EIC7700_CLK_MUX_AONDMA_AXI2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_AON_DMA, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_AON_ACLK, "gate_aon_aclk",
			EIC7700_CLK_MUX_AONDMA_AXI2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_AON_DMA, 29, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_TIMER_PCLK_0, "gate_timer_pclk_0",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_TIMER, 4, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_TIMER_PCLK_1, "gate_timer_pclk_1",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_TIMER, 5, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_TIMER_PCLK_2, "gate_timer_pclk_2",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_TIMER, 6, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_TIMER_PCLK_3, "gate_timer_pclk_3",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_TIMER, 7, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_TIMER3_CLK8, "gate_timer3_clk8",
			EIC7700_CLK_VPLL_FOUT3, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_TIMER, 8, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_RTC_CFG, "gate_clk_rtc_cfg",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_RTC, 2, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_RTC, "gate_clk_rtc",
			EIC7700_CLK_DIV_AON_RTC_DYNM, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_RTC, 1, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_HSP_RMII_REF_0, "gate_hsp_rmii_ref_0",
			EIC7700_CLK_MUX_RMII_REF_2MUX, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_ETH0, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_HSP_RMII_REF_1, "gate_hsp_rmii_ref_1",
			EIC7700_CLK_MUX_RMII_REF_2MUX, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_ETH1, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_PKA_CFG, "gate_clk_pka_cfg",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_PKA, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_SPACC_CFG, "gate_clk_spacc_cfg",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_SPACC, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_CRYPTO, "gate_clk_crypto",
			EIC7700_CLK_DIV_CRYPTO_DYNM, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_SPACC, 30, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_TRNG_CFG, "gate_clk_trng_cfg",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_TRNG, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_OTP_CFG, "gate_clk_otp_cfg",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_OTP, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_MAILBOX_0, "gate_clk_mailbox_0",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN1, 0, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_MAILBOX_1, "gate_clk_mailbox_1",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN1, 1, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_MAILBOX_2, "gate_clk_mailbox_2",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN1, 2, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_MAILBOX_3, "gate_clk_mailbox_3",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN1, 3, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_MAILBOX_4, "gate_clk_mailbox_4",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN1, 4, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_MAILBOX_5, "gate_clk_mailbox_5",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN1, 5, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_MAILBOX_6, "gate_clk_mailbox_6",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN1, 6, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_MAILBOX_7, "gate_clk_mailbox_7",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN1, 7, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_MAILBOX_8, "gate_clk_mailbox_8",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN1, 8, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_MAILBOX_9, "gate_clk_mailbox_9",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN1, 9, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_MAILBOX_10, "gate_clk_mailbox_10",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN1, 10, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_MAILBOX_11, "gate_clk_mailbox_11",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN1, 11, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_MAILBOX_12, "gate_clk_mailbox_12",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN1, 12, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_MAILBOX_13, "gate_clk_mailbox_13",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN1, 13, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_MAILBOX_14, "gate_clk_mailbox_14",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN1, 14, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_MAILBOX_15, "gate_clk_mailbox_15",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN1, 15, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_LSP_I2C0_PCLK, "gate_i2c0_pclk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN0, 7, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_LSP_I2C1_PCLK, "gate_i2c1_pclk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN0, 8, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_LSP_I2C2_PCLK, "gate_i2c2_pclk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN0, 9, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_LSP_I2C3_PCLK, "gate_i2c3_pclk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN0, 10, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_LSP_I2C4_PCLK, "gate_i2c4_pclk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN0, 11, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_LSP_I2C5_PCLK, "gate_i2c5_pclk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN0, 12, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_LSP_I2C6_PCLK, "gate_i2c6_pclk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN0, 13, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_LSP_I2C7_PCLK, "gate_i2c7_pclk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN0, 14, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_LSP_I2C8_PCLK, "gate_i2c8_pclk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN0, 15, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_LSP_I2C9_PCLK, "gate_i2c9_pclk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN0, 16, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_LSP_WDT0_PCLK, "gate_lsp_wdt0_pclk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN0, 28, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_LSP_WDT1_PCLK, "gate_lsp_wdt1_pclk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN0, 29, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_LSP_WDT2_PCLK, "gate_lsp_wdt2_pclk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN0, 30, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_LSP_WDT3_PCLK, "gate_lsp_wdt3_pclk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN0, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_LSP_SSI0_PCLK, "gate_lsp_ssi0_pclk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN0, 26, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_LSP_SSI1_PCLK, "gate_lsp_ssi1_pclk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN0, 27, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_LSP_UART0_PCLK, "gate_lsp_uart0_pclk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
			EIC7700_REG_OFFSET_LSP_EN0, 17, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_LSP_UART1_PCLK, "gate_lsp_uart1_pclk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN0, 18, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_LSP_UART2_PCLK, "gate_lsp_uart2_pclk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
			EIC7700_REG_OFFSET_LSP_EN0, 19, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_LSP_UART3_PCLK, "gate_lsp_uart3_pclk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN0, 20, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_LSP_UART4_PCLK, "gate_lsp_uart4_pclk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN0, 21, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_LSP_TIMER_PCLK, "gate_lsp_timer_pclk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
			EIC7700_REG_OFFSET_LSP_EN0, 25, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_LSP_FAN_PCLK, "gate_lsp_fan_pclk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN0, 0, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_LSP_PVT_PCLK, "gate_lsp_pvt_pclk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_LSP_EN0, 1, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_LSP_PVT0_CLK, "gate_pvt0_clk",
			EIC7700_CLK_FIXED_FACTOR_PVT_DIV20, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_LSP_EN1, 16, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_LSP_PVT1_CLK, "gate_pvt1_clk",
			EIC7700_CLK_FIXED_FACTOR_PVT_DIV20, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_LSP_EN1, 17, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_VC_JE_PCLK, "gate_vc_je_pclk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_VC_CLKEN, 2, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_VC_JD_PCLK, "gate_vc_jd_pclk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_VC_CLKEN, 1, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_VC_VE_PCLK, "gate_vc_ve_pclk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_VC_CLKEN, 5, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_VC_VD_PCLK, "gate_vc_vd_pclk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_VC_CLKEN, 4, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_VC_MON_PCLK, "gate_vc_mon_pclk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_VC_CLKEN, 3, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_HSP_MSHC0_CORE_CLK,
			"gate_hsp_mshc0_core_clk",
			EIC7700_CLK_DIV_MSHC_CORE_DYNM_0, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_MSHC0_CORE, 16, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_HSP_MSHC1_CORE_CLK,
			"gate_hsp_mshc1_core_clk",
			EIC7700_CLK_DIV_MSHC_CORE_DYNM_1, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_MSHC1_CORE, 16, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_HSP_MSHC2_CORE_CLK,
			"gate_hsp_mshc2_core_clk",
			EIC7700_CLK_DIV_MSHC_CORE_DYNM_2, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_MSHC2_CORE, 16, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_HSP_SATA_RBC_CLK,
			"gate_hsp_sata_rbc_clk", EIC7700_CLK_SPLL1_FOUT2,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_SATA_RBC,
			0, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_HSP_SATA_OOB_CLK,
			"gate_hsp_sata_oob_clk", EIC7700_CLK_MUX_SATA_PHY_2MUX1,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_SATA_OOB, 31,
			0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_HSP_DMA0_CLK_TEST,
			"gate_hsp_dma0_clk_test", EIC7700_CLK_GATE_HSP_ACLK,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_HSP_ACLK, 1, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_HSP_DMA0_CLK, "gate_hsp_dma0_clk",
			EIC7700_CLK_GATE_HSP_ACLK, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_HSP_ACLK, 0, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_HSP_ETH0_CORE_CLK,
			"gate_hsp_eth0_core_clk",
			EIC7700_CLK_DIV_ETH_TXCLK_DYNM_0, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_ETH0, 0, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_HSP_ETH1_CORE_CLK,
			"gate_hsp_eth1_core_clk",
			EIC7700_CLK_DIV_ETH_TXCLK_DYNM_1, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_ETH1, 0, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_AON_I2C0_PCLK, "gate_aon_i2c0_pclk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_I2C0, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_AON_I2C1_PCLK, "gate_aon_i2c1_pclk",
			EIC7700_CLK_MUX_SYSCFG_CLK_ROOT_2MUX1_GFREE,
			CLK_SET_RATE_PARENT, EIC7700_REG_OFFSET_I2C1, 31, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_DDR0_TRACE, "gate_ddr0_trace",
			EIC7700_CLK_DIV_DDR_ACLK_DYNM, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_DDR, 0, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_DDR1_TRACE, "gate_ddr1_trace",
			EIC7700_CLK_DIV_DDR_ACLK_DYNM, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_DDR1, 0, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_RNOC_NSP, "gate_rnoc_nsp",
			EIC7700_CLK_DIV_NOC_NSP_DYNM, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_NOC, 29, 0),
	ESWIN_GATE_TYPE(EIC7700_CLK_GATE_NOC_WDREF, "gate_noc_wdref",
			EIC7700_CLK_DIV_NOC_WDREF_DYNM, CLK_SET_RATE_PARENT,
			EIC7700_REG_OFFSET_NOC, 30, 0),
};

/*
 * This clock notifier is called when the rate of clk_pll_cpu clock is to be
 * changed. The mux_cpu_root_3mux1_gfree clock should save the current parent
 * clock and switch its parent clock to fixed_factor_u84_core_lp_div2 before
 * clk_pll_cpu rate will be changed. Then switch its parent clock back after
 * the clk_pll_cpu rate is completed.
 */
static int eic7700_clk_pll_cpu_notifier_cb(struct notifier_block *nb,
					   unsigned long action, void *data)
{
	struct eswin_clock_data *pdata;
	struct clk_hw *mux_clk;
	struct clk_hw *lp_clk;
	int ret = 0;

	pdata = container_of(nb, struct eswin_clock_data, pll_nb);

	mux_clk = &eic7700_mux_clks[0].hw;
	lp_clk = &eic7700_early_clks[11].hw;

	if (action == PRE_RATE_CHANGE) {
		pdata->original_clk = clk_hw_get_parent(mux_clk);
		ret = clk_hw_set_parent(mux_clk, lp_clk);
	} else if (action == POST_RATE_CHANGE) {
		ret = clk_hw_set_parent(mux_clk, pdata->original_clk);
	}

	return notifier_from_errno(ret);
}

static int eic7700_clk_probe(struct platform_device *pdev)
{
	struct eswin_clock_data *clk_data;
	struct device *dev = &pdev->dev;
	struct clk *pll_clk;
	int ret;

	clk_data = eswin_clk_init(pdev, EIC7700_NR_CLKS);
	if (IS_ERR(clk_data))
		return dev_err_probe(dev, PTR_ERR(clk_data),
				     "failed to get clk data!\n");

	ret = eswin_clk_register_fixed_rate(dev, eic7700_fixed_rate_clks,
					    ARRAY_SIZE(eic7700_fixed_rate_clks),
					    clk_data);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register fixed rate clock\n");

	ret = eswin_clk_register_pll(dev, eic7700_pll_clks,
				     ARRAY_SIZE(eic7700_pll_clks),
				     clk_data);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register pll clock\n");

	pll_clk = devm_clk_hw_get_clk(dev, &eic7700_pll_clks[1].hw,
				      "clk_pll_cpu");
	if (IS_ERR(pll_clk))
		return dev_err_probe(dev, PTR_ERR(pll_clk),
				     "failed to get clk_pll_cpu\n");

	clk_data->pll_nb.notifier_call = eic7700_clk_pll_cpu_notifier_cb;
	ret = devm_clk_notifier_register(dev, pll_clk, &clk_data->pll_nb);
	if (ret)
		return ret;

	ret = eswin_clk_register_fixed_factor(dev, eic7700_factor_clks,
					      ARRAY_SIZE(eic7700_factor_clks),
					      clk_data);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register fixed factor clock\n");

	ret = eswin_clk_register_divider(dev, eic7700_div_clks,
					 ARRAY_SIZE(eic7700_div_clks),
					 clk_data);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register divider clock\n");

	ret = eswin_clk_register_gate(dev, eic7700_gate_clks,
				      ARRAY_SIZE(eic7700_gate_clks), clk_data);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register gate clock\n");

	ret = eswin_clk_register_clks(dev, eic7700_early_clks,
				      ARRAY_SIZE(eic7700_early_clks), clk_data);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register clock\n");

	ret = eswin_clk_register_mux(dev, eic7700_mux_clks,
				     ARRAY_SIZE(eic7700_mux_clks), clk_data);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register mux clock\n");

	ret = eswin_clk_register_clks(dev, eic7700_clks,
				      ARRAY_SIZE(eic7700_clks), clk_data);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register clock\n");

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get,
					   &clk_data->clk_data);
}

static const struct of_device_id eic7700_clock_dt_ids[] = {
	{ .compatible = "eswin,eic7700-clock", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, eic7700_clock_dt_ids);

static struct platform_driver eic7700_clock_driver = {
	.probe	= eic7700_clk_probe,
	.driver = {
		.name	= "eic7700-clock",
		.of_match_table	= eic7700_clock_dt_ids,
	},
};
module_platform_driver(eic7700_clock_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yifeng Huang <huangyifeng@eswincomputing.com>");
MODULE_AUTHOR("Xuyang Dong <dongxuyang@eswincomputing.com>");
MODULE_DESCRIPTION("ESWIN EIC7700 clock controller driver");
