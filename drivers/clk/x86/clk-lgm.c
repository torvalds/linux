// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2022 MaxLinear, Inc.
 * Copyright (C) 2020 Intel Corporation.
 * Zhu Yixin <yzhu@maxlinear.com>
 * Rahul Tanwar <rtanwar@maxlinear.com>
 */
#include <linux/clk-provider.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/intel,lgm-clk.h>
#include "clk-cgu.h"

#define PLL_DIV_WIDTH		4
#define PLL_DDIV_WIDTH		3

/* Gate0 clock shift */
#define G_C55_SHIFT		7
#define G_QSPI_SHIFT		9
#define G_EIP197_SHIFT		11
#define G_VAULT130_SHIFT	12
#define G_TOE_SHIFT		13
#define G_SDXC_SHIFT		14
#define G_EMMC_SHIFT		15
#define G_SPIDBG_SHIFT		17
#define G_DMA3_SHIFT		28

/* Gate1 clock shift */
#define G_DMA0_SHIFT		0
#define G_LEDC0_SHIFT		1
#define G_LEDC1_SHIFT		2
#define G_I2S0_SHIFT		3
#define G_I2S1_SHIFT		4
#define G_EBU_SHIFT		5
#define G_PWM_SHIFT		6
#define G_I2C0_SHIFT		7
#define G_I2C1_SHIFT		8
#define G_I2C2_SHIFT		9
#define G_I2C3_SHIFT		10

#define G_SSC0_SHIFT		12
#define G_SSC1_SHIFT		13
#define G_SSC2_SHIFT		14
#define G_SSC3_SHIFT		15

#define G_GPTC0_SHIFT		17
#define G_GPTC1_SHIFT		18
#define G_GPTC2_SHIFT		19
#define G_GPTC3_SHIFT		20

#define G_ASC0_SHIFT		22
#define G_ASC1_SHIFT		23
#define G_ASC2_SHIFT		24
#define G_ASC3_SHIFT		25

#define G_PCM0_SHIFT		27
#define G_PCM1_SHIFT		28
#define G_PCM2_SHIFT		29

/* Gate2 clock shift */
#define G_PCIE10_SHIFT		1
#define G_PCIE11_SHIFT		2
#define G_PCIE30_SHIFT		3
#define G_PCIE31_SHIFT		4
#define G_PCIE20_SHIFT		5
#define G_PCIE21_SHIFT		6
#define G_PCIE40_SHIFT		7
#define G_PCIE41_SHIFT		8

#define G_XPCS0_SHIFT		10
#define G_XPCS1_SHIFT		11
#define G_XPCS2_SHIFT		12
#define G_XPCS3_SHIFT		13
#define G_SATA0_SHIFT		14
#define G_SATA1_SHIFT		15
#define G_SATA2_SHIFT		16
#define G_SATA3_SHIFT		17

/* Gate3 clock shift */
#define G_ARCEM4_SHIFT		0
#define G_IDMAR1_SHIFT		2
#define G_IDMAT0_SHIFT		3
#define G_IDMAT1_SHIFT		4
#define G_IDMAT2_SHIFT		5

#define G_PPV4_SHIFT		8
#define G_GSWIPO_SHIFT		9
#define G_CQEM_SHIFT		10
#define G_XPCS5_SHIFT		14
#define G_USB1_SHIFT		25
#define G_USB2_SHIFT		26


/* Register definition */
#define CGU_PLL0CZ_CFG0		0x000
#define CGU_PLL0CM0_CFG0	0x020
#define CGU_PLL0CM1_CFG0	0x040
#define CGU_PLL0B_CFG0		0x060
#define CGU_PLL1_CFG0		0x080
#define CGU_PLL2_CFG0		0x0A0
#define CGU_PLLPP_CFG0		0x0C0
#define CGU_LJPLL3_CFG0		0x0E0
#define CGU_LJPLL4_CFG0		0x100
#define CGU_C55_PCMCR		0x18C
#define CGU_PCMCR		0x190
#define CGU_IF_CLK1		0x1A0
#define CGU_IF_CLK2		0x1A4
#define CGU_GATE0		0x300
#define CGU_GATE1		0x310
#define CGU_GATE2		0x320
#define CGU_GATE3		0x310

#define PLL_DIV(x)		((x) + 0x04)
#define PLL_SSC(x)		((x) + 0x10)

#define CLK_NR_CLKS		(LGM_GCLK_USB2 + 1)

/*
 * Below table defines the pair's of regval & effective dividers.
 * It's more efficient to provide an explicit table due to non-linear
 * relation between values.
 */
static const struct clk_div_table pll_div[] = {
	{ .val = 0, .div = 1 },
	{ .val = 1, .div = 2 },
	{ .val = 2, .div = 3 },
	{ .val = 3, .div = 4 },
	{ .val = 4, .div = 5 },
	{ .val = 5, .div = 6 },
	{ .val = 6, .div = 8 },
	{ .val = 7, .div = 10 },
	{ .val = 8, .div = 12 },
	{ .val = 9, .div = 16 },
	{ .val = 10, .div = 20 },
	{ .val = 11, .div = 24 },
	{ .val = 12, .div = 32 },
	{ .val = 13, .div = 40 },
	{ .val = 14, .div = 48 },
	{ .val = 15, .div = 64 },
	{}
};

static const struct clk_div_table dcl_div[] = {
	{ .val = 0, .div = 6  },
	{ .val = 1, .div = 12 },
	{ .val = 2, .div = 24 },
	{ .val = 3, .div = 32 },
	{ .val = 4, .div = 48 },
	{ .val = 5, .div = 96 },
	{}
};

static const struct clk_parent_data pll_p[] = {
	{ .fw_name = "osc", .name = "osc" },
};
static const struct clk_parent_data pllcm_p[] = {
	{ .fw_name = "cpu_cm", .name = "cpu_cm" },
};
static const struct clk_parent_data emmc_p[] = {
	{ .fw_name = "emmc4", .name = "emmc4" },
	{ .fw_name = "noc4", .name = "noc4" },
};
static const struct clk_parent_data sdxc_p[] = {
	{ .fw_name = "sdxc3", .name = "sdxc3" },
	{ .fw_name = "sdxc2", .name = "sdxc2" },
};
static const struct clk_parent_data pcm_p[] = {
	{ .fw_name = "v_docsis", .name = "v_docsis" },
	{ .fw_name = "dcl", .name = "dcl" },
};
static const struct clk_parent_data cbphy_p[] = {
	{ .fw_name = "dd_serdes", .name = "dd_serdes" },
	{ .fw_name = "dd_pcie", .name = "dd_pcie" },
};

static const struct lgm_pll_clk_data lgm_pll_clks[] = {
	LGM_PLL(LGM_CLK_PLL0CZ, "pll0cz", pll_p, CLK_IGNORE_UNUSED,
		CGU_PLL0CZ_CFG0, TYPE_ROPLL),
	LGM_PLL(LGM_CLK_PLL0CM0, "pll0cm0", pllcm_p, CLK_IGNORE_UNUSED,
		CGU_PLL0CM0_CFG0, TYPE_ROPLL),
	LGM_PLL(LGM_CLK_PLL0CM1, "pll0cm1", pllcm_p, CLK_IGNORE_UNUSED,
		CGU_PLL0CM1_CFG0, TYPE_ROPLL),
	LGM_PLL(LGM_CLK_PLL0B, "pll0b", pll_p, CLK_IGNORE_UNUSED,
		CGU_PLL0B_CFG0, TYPE_ROPLL),
	LGM_PLL(LGM_CLK_PLL1, "pll1", pll_p, 0, CGU_PLL1_CFG0, TYPE_ROPLL),
	LGM_PLL(LGM_CLK_PLL2, "pll2", pll_p, CLK_IGNORE_UNUSED,
		CGU_PLL2_CFG0, TYPE_ROPLL),
	LGM_PLL(LGM_CLK_PLLPP, "pllpp", pll_p, 0, CGU_PLLPP_CFG0, TYPE_ROPLL),
	LGM_PLL(LGM_CLK_LJPLL3, "ljpll3", pll_p, 0, CGU_LJPLL3_CFG0, TYPE_LJPLL),
	LGM_PLL(LGM_CLK_LJPLL4, "ljpll4", pll_p, 0, CGU_LJPLL4_CFG0, TYPE_LJPLL),
};

static const struct lgm_clk_branch lgm_branch_clks[] = {
	LGM_DIV(LGM_CLK_PP_HW, "pp_hw", "pllpp", 0, PLL_DIV(CGU_PLLPP_CFG0),
		0, PLL_DIV_WIDTH, 24, 1, 0, 0, pll_div),
	LGM_DIV(LGM_CLK_PP_UC, "pp_uc", "pllpp", 0, PLL_DIV(CGU_PLLPP_CFG0),
		4, PLL_DIV_WIDTH, 25, 1, 0, 0, pll_div),
	LGM_DIV(LGM_CLK_PP_FXD, "pp_fxd", "pllpp", 0, PLL_DIV(CGU_PLLPP_CFG0),
		8, PLL_DIV_WIDTH, 26, 1, 0, 0, pll_div),
	LGM_DIV(LGM_CLK_PP_TBM, "pp_tbm", "pllpp", 0, PLL_DIV(CGU_PLLPP_CFG0),
		12, PLL_DIV_WIDTH, 27, 1, 0, 0, pll_div),
	LGM_DIV(LGM_CLK_DDR, "ddr", "pll2", CLK_IGNORE_UNUSED,
		PLL_DIV(CGU_PLL2_CFG0), 0, PLL_DIV_WIDTH, 24, 1, 0, 0,
		pll_div),
	LGM_DIV(LGM_CLK_CM, "cpu_cm", "pll0cz", 0, PLL_DIV(CGU_PLL0CZ_CFG0),
		0, PLL_DIV_WIDTH, 24, 1, 0, 0, pll_div),

	LGM_DIV(LGM_CLK_IC, "cpu_ic", "pll0cz", CLK_IGNORE_UNUSED,
		PLL_DIV(CGU_PLL0CZ_CFG0), 4, PLL_DIV_WIDTH, 25,
		1, 0, 0, pll_div),

	LGM_DIV(LGM_CLK_SDXC3, "sdxc3", "pll0cz", 0, PLL_DIV(CGU_PLL0CZ_CFG0),
		8, PLL_DIV_WIDTH, 26, 1, 0, 0, pll_div),

	LGM_DIV(LGM_CLK_CPU0, "cm0", "pll0cm0",
		CLK_IGNORE_UNUSED, PLL_DIV(CGU_PLL0CM0_CFG0),
		0, PLL_DIV_WIDTH, 24, 1, 0, 0, pll_div),
	LGM_DIV(LGM_CLK_CPU1, "cm1", "pll0cm1",
		CLK_IGNORE_UNUSED, PLL_DIV(CGU_PLL0CM1_CFG0),
		0, PLL_DIV_WIDTH, 24, 1, 0, 0, pll_div),

	/*
	 * Marking ngi_clk (next generation interconnect) and noc_clk
	 * (network on chip peripheral clk) as critical clocks because
	 * these are shared parent clock sources for many different
	 * peripherals.
	 */
	LGM_DIV(LGM_CLK_NGI, "ngi", "pll0b",
		(CLK_IGNORE_UNUSED|CLK_IS_CRITICAL), PLL_DIV(CGU_PLL0B_CFG0),
		0, PLL_DIV_WIDTH, 24, 1, 0, 0, pll_div),
	LGM_DIV(LGM_CLK_NOC4, "noc4", "pll0b",
		(CLK_IGNORE_UNUSED|CLK_IS_CRITICAL), PLL_DIV(CGU_PLL0B_CFG0),
		4, PLL_DIV_WIDTH, 25, 1, 0, 0, pll_div),
	LGM_DIV(LGM_CLK_SW, "switch", "pll0b", 0, PLL_DIV(CGU_PLL0B_CFG0),
		8, PLL_DIV_WIDTH, 26, 1, 0, 0, pll_div),
	LGM_DIV(LGM_CLK_QSPI, "qspi", "pll0b", 0, PLL_DIV(CGU_PLL0B_CFG0),
		12, PLL_DIV_WIDTH, 27, 1, 0, 0, pll_div),
	LGM_DIV(LGM_CLK_CT, "v_ct", "pll1", 0, PLL_DIV(CGU_PLL1_CFG0),
		0, PLL_DIV_WIDTH, 24, 1, 0, 0, pll_div),
	LGM_DIV(LGM_CLK_DSP, "v_dsp", "pll1", 0, PLL_DIV(CGU_PLL1_CFG0),
		8, PLL_DIV_WIDTH, 26, 1, 0, 0, pll_div),
	LGM_DIV(LGM_CLK_VIF, "v_ifclk", "pll1", 0, PLL_DIV(CGU_PLL1_CFG0),
		12, PLL_DIV_WIDTH, 27, 1, 0, 0, pll_div),

	LGM_FIXED_FACTOR(LGM_CLK_EMMC4, "emmc4", "sdxc3", 0,  0,
			 0, 0, 0, 0, 1, 4),
	LGM_FIXED_FACTOR(LGM_CLK_SDXC2, "sdxc2", "noc4", 0,  0,
			 0, 0, 0, 0, 1, 4),
	LGM_MUX(LGM_CLK_EMMC, "emmc", emmc_p, 0, CGU_IF_CLK1,
		0, 1, CLK_MUX_ROUND_CLOSEST, 0),
	LGM_MUX(LGM_CLK_SDXC, "sdxc", sdxc_p, 0, CGU_IF_CLK1,
		1, 1, CLK_MUX_ROUND_CLOSEST, 0),
	LGM_FIXED(LGM_CLK_OSC, "osc", NULL, 0, 0, 0, 0, 0, 40000000, 0),
	LGM_FIXED(LGM_CLK_SLIC, "slic", NULL, 0, CGU_IF_CLK1,
		  8, 2, CLOCK_FLAG_VAL_INIT, 8192000, 2),
	LGM_FIXED(LGM_CLK_DOCSIS, "v_docsis", NULL, 0, 0, 0, 0, 0, 16000000, 0),
	LGM_DIV(LGM_CLK_DCL, "dcl", "v_ifclk", CLK_SET_RATE_PARENT, CGU_PCMCR,
		25, 3, 0, 0, DIV_CLK_NO_MASK, 0, dcl_div),
	LGM_MUX(LGM_CLK_PCM, "pcm", pcm_p, 0, CGU_C55_PCMCR,
		0, 1, CLK_MUX_ROUND_CLOSEST, 0),
	LGM_FIXED_FACTOR(LGM_CLK_DDR_PHY, "ddr_phy", "ddr",
			 CLK_IGNORE_UNUSED, 0,
			 0, 0, 0, 0, 2, 1),
	LGM_FIXED_FACTOR(LGM_CLK_PONDEF, "pondef", "dd_pool",
			 CLK_SET_RATE_PARENT, 0, 0, 0, 0, 0, 1, 2),
	LGM_MUX(LGM_CLK_CBPHY0, "cbphy0", cbphy_p, 0, 0,
		0, 0, MUX_CLK_SW | CLK_MUX_ROUND_CLOSEST, 0),
	LGM_MUX(LGM_CLK_CBPHY1, "cbphy1", cbphy_p, 0, 0,
		0, 0, MUX_CLK_SW | CLK_MUX_ROUND_CLOSEST, 0),
	LGM_MUX(LGM_CLK_CBPHY2, "cbphy2", cbphy_p, 0, 0,
		0, 0, MUX_CLK_SW | CLK_MUX_ROUND_CLOSEST, 0),
	LGM_MUX(LGM_CLK_CBPHY3, "cbphy3", cbphy_p, 0, 0,
		0, 0, MUX_CLK_SW | CLK_MUX_ROUND_CLOSEST, 0),

	LGM_GATE(LGM_GCLK_C55, "g_c55", NULL, 0, CGU_GATE0,
		 G_C55_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_QSPI, "g_qspi", "qspi", 0, CGU_GATE0,
		 G_QSPI_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_EIP197, "g_eip197", NULL, 0, CGU_GATE0,
		 G_EIP197_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_VAULT, "g_vault130", NULL, 0, CGU_GATE0,
		 G_VAULT130_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_TOE, "g_toe", NULL, 0, CGU_GATE0,
		 G_TOE_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_SDXC, "g_sdxc", "sdxc", 0, CGU_GATE0,
		 G_SDXC_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_EMMC, "g_emmc", "emmc", 0, CGU_GATE0,
		 G_EMMC_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_SPI_DBG, "g_spidbg", NULL, 0, CGU_GATE0,
		 G_SPIDBG_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_DMA3, "g_dma3", NULL, 0, CGU_GATE0,
		 G_DMA3_SHIFT, 0, 0),

	LGM_GATE(LGM_GCLK_DMA0, "g_dma0", NULL, 0, CGU_GATE1,
		 G_DMA0_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_LEDC0, "g_ledc0", NULL, 0, CGU_GATE1,
		 G_LEDC0_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_LEDC1, "g_ledc1", NULL, 0, CGU_GATE1,
		 G_LEDC1_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_I2S0, "g_i2s0", NULL, 0, CGU_GATE1,
		 G_I2S0_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_I2S1, "g_i2s1", NULL, 0, CGU_GATE1,
		 G_I2S1_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_EBU, "g_ebu", NULL, 0, CGU_GATE1,
		 G_EBU_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_PWM, "g_pwm", NULL, 0, CGU_GATE1,
		 G_PWM_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_I2C0, "g_i2c0", NULL, 0, CGU_GATE1,
		 G_I2C0_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_I2C1, "g_i2c1", NULL, 0, CGU_GATE1,
		 G_I2C1_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_I2C2, "g_i2c2", NULL, 0, CGU_GATE1,
		 G_I2C2_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_I2C3, "g_i2c3", NULL, 0, CGU_GATE1,
		 G_I2C3_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_SSC0, "g_ssc0", "noc4", 0, CGU_GATE1,
		 G_SSC0_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_SSC1, "g_ssc1", "noc4", 0, CGU_GATE1,
		 G_SSC1_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_SSC2, "g_ssc2", "noc4", 0, CGU_GATE1,
		 G_SSC2_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_SSC3, "g_ssc3", "noc4", 0, CGU_GATE1,
		 G_SSC3_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_GPTC0, "g_gptc0", "noc4", 0, CGU_GATE1,
		 G_GPTC0_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_GPTC1, "g_gptc1", "noc4", 0, CGU_GATE1,
		 G_GPTC1_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_GPTC2, "g_gptc2", "noc4", 0, CGU_GATE1,
		 G_GPTC2_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_GPTC3, "g_gptc3", "osc", 0, CGU_GATE1,
		 G_GPTC3_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_ASC0, "g_asc0", "noc4", 0, CGU_GATE1,
		 G_ASC0_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_ASC1, "g_asc1", "noc4", 0, CGU_GATE1,
		 G_ASC1_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_ASC2, "g_asc2", "noc4", 0, CGU_GATE1,
		 G_ASC2_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_ASC3, "g_asc3", "osc", 0, CGU_GATE1,
		 G_ASC3_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_PCM0, "g_pcm0", NULL, 0, CGU_GATE1,
		 G_PCM0_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_PCM1, "g_pcm1", NULL, 0, CGU_GATE1,
		 G_PCM1_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_PCM2, "g_pcm2", NULL, 0, CGU_GATE1,
		 G_PCM2_SHIFT, 0, 0),

	LGM_GATE(LGM_GCLK_PCIE10, "g_pcie10", NULL, 0, CGU_GATE2,
		 G_PCIE10_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_PCIE11, "g_pcie11", NULL, 0, CGU_GATE2,
		 G_PCIE11_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_PCIE30, "g_pcie30", NULL, 0, CGU_GATE2,
		 G_PCIE30_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_PCIE31, "g_pcie31", NULL, 0, CGU_GATE2,
		 G_PCIE31_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_PCIE20, "g_pcie20", NULL, 0, CGU_GATE2,
		 G_PCIE20_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_PCIE21, "g_pcie21", NULL, 0, CGU_GATE2,
		 G_PCIE21_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_PCIE40, "g_pcie40", NULL, 0, CGU_GATE2,
		 G_PCIE40_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_PCIE41, "g_pcie41", NULL, 0, CGU_GATE2,
		 G_PCIE41_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_XPCS0, "g_xpcs0", NULL, 0, CGU_GATE2,
		 G_XPCS0_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_XPCS1, "g_xpcs1", NULL, 0, CGU_GATE2,
		 G_XPCS1_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_XPCS2, "g_xpcs2", NULL, 0, CGU_GATE2,
		 G_XPCS2_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_XPCS3, "g_xpcs3", NULL, 0, CGU_GATE2,
		 G_XPCS3_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_SATA0, "g_sata0", NULL, 0, CGU_GATE2,
		 G_SATA0_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_SATA1, "g_sata1", NULL, 0, CGU_GATE2,
		 G_SATA1_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_SATA2, "g_sata2", NULL, 0, CGU_GATE2,
		 G_SATA2_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_SATA3, "g_sata3", NULL, 0, CGU_GATE2,
		 G_SATA3_SHIFT, 0, 0),

	LGM_GATE(LGM_GCLK_ARCEM4, "g_arcem4", NULL, 0, CGU_GATE3,
		 G_ARCEM4_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_IDMAR1, "g_idmar1", NULL, 0, CGU_GATE3,
		 G_IDMAR1_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_IDMAT0, "g_idmat0", NULL, 0, CGU_GATE3,
		 G_IDMAT0_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_IDMAT1, "g_idmat1", NULL, 0, CGU_GATE3,
		 G_IDMAT1_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_IDMAT2, "g_idmat2", NULL, 0, CGU_GATE3,
		 G_IDMAT2_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_PPV4, "g_ppv4", NULL, 0, CGU_GATE3,
		 G_PPV4_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_GSWIPO, "g_gswipo", "switch", 0, CGU_GATE3,
		 G_GSWIPO_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_CQEM, "g_cqem", "switch", 0, CGU_GATE3,
		 G_CQEM_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_XPCS5, "g_xpcs5", NULL, 0, CGU_GATE3,
		 G_XPCS5_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_USB1, "g_usb1", NULL, 0, CGU_GATE3,
		 G_USB1_SHIFT, 0, 0),
	LGM_GATE(LGM_GCLK_USB2, "g_usb2", NULL, 0, CGU_GATE3,
		 G_USB2_SHIFT, 0, 0),
};


static const struct lgm_clk_ddiv_data lgm_ddiv_clks[] = {
	LGM_DDIV(LGM_CLK_CML, "dd_cml", "ljpll3", 0,
		 PLL_DIV(CGU_LJPLL3_CFG0), 0, PLL_DDIV_WIDTH,
		 3, PLL_DDIV_WIDTH, 24, 1, 29, 0),
	LGM_DDIV(LGM_CLK_SERDES, "dd_serdes", "ljpll3", 0,
		 PLL_DIV(CGU_LJPLL3_CFG0), 6, PLL_DDIV_WIDTH,
		 9, PLL_DDIV_WIDTH, 25, 1, 28, 0),
	LGM_DDIV(LGM_CLK_POOL, "dd_pool", "ljpll3", 0,
		 PLL_DIV(CGU_LJPLL3_CFG0), 12, PLL_DDIV_WIDTH,
		 15, PLL_DDIV_WIDTH, 26, 1, 28, 0),
	LGM_DDIV(LGM_CLK_PTP, "dd_ptp", "ljpll3", 0,
		 PLL_DIV(CGU_LJPLL3_CFG0), 18, PLL_DDIV_WIDTH,
		 21, PLL_DDIV_WIDTH, 27, 1, 28, 0),
	LGM_DDIV(LGM_CLK_PCIE, "dd_pcie", "ljpll4", 0,
		 PLL_DIV(CGU_LJPLL4_CFG0), 0, PLL_DDIV_WIDTH,
		 3, PLL_DDIV_WIDTH, 24, 1, 29, 0),
};

static int lgm_cgu_probe(struct platform_device *pdev)
{
	struct lgm_clk_provider *ctx;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	int ret;

	ctx = devm_kzalloc(dev, struct_size(ctx, clk_data.hws, CLK_NR_CLKS),
			   GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->clk_data.num = CLK_NR_CLKS;

	ctx->membase = syscon_node_to_regmap(np);
	if (IS_ERR(ctx->membase)) {
		dev_err(dev, "Failed to get clk CGU iomem\n");
		return PTR_ERR(ctx->membase);
	}


	ctx->np = np;
	ctx->dev = dev;

	ret = lgm_clk_register_plls(ctx, lgm_pll_clks,
				    ARRAY_SIZE(lgm_pll_clks));
	if (ret)
		return ret;

	ret = lgm_clk_register_branches(ctx, lgm_branch_clks,
					ARRAY_SIZE(lgm_branch_clks));
	if (ret)
		return ret;

	ret = lgm_clk_register_ddiv(ctx, lgm_ddiv_clks,
				    ARRAY_SIZE(lgm_ddiv_clks));
	if (ret)
		return ret;

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get,
					   &ctx->clk_data);
}

static const struct of_device_id of_lgm_cgu_match[] = {
	{ .compatible = "intel,cgu-lgm" },
	{}
};

static struct platform_driver lgm_cgu_driver = {
	.probe = lgm_cgu_probe,
	.driver = {
		   .name = "cgu-lgm",
		   .of_match_table = of_lgm_cgu_match,
	},
};
builtin_platform_driver(lgm_cgu_driver);
