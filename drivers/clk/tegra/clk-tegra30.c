// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/clk/tegra.h>

#include <soc/tegra/pmc.h>

#include <dt-bindings/clock/tegra30-car.h>

#include "clk.h"
#include "clk-id.h"

#define OSC_CTRL			0x50
#define OSC_CTRL_OSC_FREQ_MASK		(0xF<<28)
#define OSC_CTRL_OSC_FREQ_13MHZ		(0X0<<28)
#define OSC_CTRL_OSC_FREQ_19_2MHZ	(0X4<<28)
#define OSC_CTRL_OSC_FREQ_12MHZ		(0X8<<28)
#define OSC_CTRL_OSC_FREQ_26MHZ		(0XC<<28)
#define OSC_CTRL_OSC_FREQ_16_8MHZ	(0X1<<28)
#define OSC_CTRL_OSC_FREQ_38_4MHZ	(0X5<<28)
#define OSC_CTRL_OSC_FREQ_48MHZ		(0X9<<28)
#define OSC_CTRL_MASK			(0x3f2 | OSC_CTRL_OSC_FREQ_MASK)

#define OSC_CTRL_PLL_REF_DIV_MASK	(3<<26)
#define OSC_CTRL_PLL_REF_DIV_1		(0<<26)
#define OSC_CTRL_PLL_REF_DIV_2		(1<<26)
#define OSC_CTRL_PLL_REF_DIV_4		(2<<26)

#define OSC_FREQ_DET			0x58
#define OSC_FREQ_DET_TRIG		BIT(31)

#define OSC_FREQ_DET_STATUS		0x5c
#define OSC_FREQ_DET_BUSY		BIT(31)
#define OSC_FREQ_DET_CNT_MASK		0xffff

#define CCLKG_BURST_POLICY 0x368
#define SUPER_CCLKG_DIVIDER 0x36c
#define CCLKLP_BURST_POLICY 0x370
#define SUPER_CCLKLP_DIVIDER 0x374
#define SCLK_BURST_POLICY 0x028
#define SUPER_SCLK_DIVIDER 0x02c

#define SYSTEM_CLK_RATE 0x030

#define TEGRA30_CLK_PERIPH_BANKS	5

#define PLLC_BASE 0x80
#define PLLC_MISC 0x8c
#define PLLM_BASE 0x90
#define PLLM_MISC 0x9c
#define PLLP_BASE 0xa0
#define PLLP_MISC 0xac
#define PLLX_BASE 0xe0
#define PLLX_MISC 0xe4
#define PLLD_BASE 0xd0
#define PLLD_MISC 0xdc
#define PLLD2_BASE 0x4b8
#define PLLD2_MISC 0x4bc
#define PLLE_BASE 0xe8
#define PLLE_MISC 0xec
#define PLLA_BASE 0xb0
#define PLLA_MISC 0xbc
#define PLLU_BASE 0xc0
#define PLLU_MISC 0xcc

#define PLL_MISC_LOCK_ENABLE 18
#define PLLDU_MISC_LOCK_ENABLE 22
#define PLLE_MISC_LOCK_ENABLE 9

#define PLL_BASE_LOCK BIT(27)
#define PLLE_MISC_LOCK BIT(11)

#define PLLE_AUX 0x48c
#define PLLC_OUT 0x84
#define PLLM_OUT 0x94
#define PLLP_OUTA 0xa4
#define PLLP_OUTB 0xa8
#define PLLA_OUT 0xb4

#define AUDIO_SYNC_CLK_I2S0 0x4a0
#define AUDIO_SYNC_CLK_I2S1 0x4a4
#define AUDIO_SYNC_CLK_I2S2 0x4a8
#define AUDIO_SYNC_CLK_I2S3 0x4ac
#define AUDIO_SYNC_CLK_I2S4 0x4b0
#define AUDIO_SYNC_CLK_SPDIF 0x4b4

#define CLK_SOURCE_SPDIF_OUT 0x108
#define CLK_SOURCE_PWM 0x110
#define CLK_SOURCE_D_AUDIO 0x3d0
#define CLK_SOURCE_DAM0 0x3d8
#define CLK_SOURCE_DAM1 0x3dc
#define CLK_SOURCE_DAM2 0x3e0
#define CLK_SOURCE_3D2 0x3b0
#define CLK_SOURCE_2D 0x15c
#define CLK_SOURCE_HDMI 0x18c
#define CLK_SOURCE_DSIB 0xd0
#define CLK_SOURCE_SE 0x42c
#define CLK_SOURCE_EMC 0x19c

#define AUDIO_SYNC_DOUBLER 0x49c

/* Tegra CPU clock and reset control regs */
#define TEGRA_CLK_RST_CONTROLLER_CLK_CPU_CMPLX		0x4c
#define TEGRA_CLK_RST_CONTROLLER_RST_CPU_CMPLX_SET	0x340
#define TEGRA_CLK_RST_CONTROLLER_RST_CPU_CMPLX_CLR	0x344
#define TEGRA30_CLK_RST_CONTROLLER_CLK_CPU_CMPLX_CLR	0x34c
#define TEGRA30_CLK_RST_CONTROLLER_CPU_CMPLX_STATUS	0x470

#define CPU_CLOCK(cpu)	(0x1 << (8 + cpu))
#define CPU_RESET(cpu)	(0x1111ul << (cpu))

#define CLK_RESET_CCLK_BURST	0x20
#define CLK_RESET_CCLK_DIVIDER	0x24
#define CLK_RESET_PLLX_BASE	0xe0
#define CLK_RESET_PLLX_MISC	0xe4

#define CLK_RESET_SOURCE_CSITE	0x1d4

#define CLK_RESET_CCLK_BURST_POLICY_SHIFT	28
#define CLK_RESET_CCLK_RUN_POLICY_SHIFT		4
#define CLK_RESET_CCLK_IDLE_POLICY_SHIFT	0
#define CLK_RESET_CCLK_IDLE_POLICY		1
#define CLK_RESET_CCLK_RUN_POLICY		2
#define CLK_RESET_CCLK_BURST_POLICY_PLLX	8

/* PLLM override registers */
#define PMC_PLLM_WB0_OVERRIDE 0x1dc

#ifdef CONFIG_PM_SLEEP
static struct cpu_clk_suspend_context {
	u32 pllx_misc;
	u32 pllx_base;

	u32 cpu_burst;
	u32 clk_csite_src;
	u32 cclk_divider;
} tegra30_cpu_clk_sctx;
#endif

static void __iomem *clk_base;
static void __iomem *pmc_base;
static unsigned long input_freq;

static DEFINE_SPINLOCK(cml_lock);
static DEFINE_SPINLOCK(pll_d_lock);

#define TEGRA_INIT_DATA_MUX(_name, _parents, _offset,	\
			    _clk_num, _gate_flags, _clk_id)	\
	TEGRA_INIT_DATA(_name, NULL, NULL, _parents, _offset,	\
			30, 2, 0, 0, 8, 1, TEGRA_DIVIDER_ROUND_UP, \
			_clk_num, _gate_flags, _clk_id)

#define TEGRA_INIT_DATA_MUX8(_name, _parents, _offset, \
			     _clk_num, _gate_flags, _clk_id)	\
	TEGRA_INIT_DATA(_name, NULL, NULL, _parents, _offset,	\
			29, 3, 0, 0, 8, 1, TEGRA_DIVIDER_ROUND_UP, \
			_clk_num, _gate_flags, _clk_id)

#define TEGRA_INIT_DATA_INT(_name, _parents, _offset,	\
			    _clk_num, _gate_flags, _clk_id)	\
	TEGRA_INIT_DATA(_name, NULL, NULL, _parents, _offset,	\
			30, 2, 0, 0, 8, 1, TEGRA_DIVIDER_INT |		\
			TEGRA_DIVIDER_ROUND_UP, _clk_num,	\
			_gate_flags, _clk_id)

#define TEGRA_INIT_DATA_NODIV(_name, _parents, _offset, \
			      _mux_shift, _mux_width, _clk_num, \
			      _gate_flags, _clk_id)			\
	TEGRA_INIT_DATA(_name, NULL, NULL, _parents, _offset,	\
			_mux_shift, _mux_width, 0, 0, 0, 0, 0,\
			_clk_num, _gate_flags,	\
			_clk_id)

static struct clk **clks;

static struct tegra_clk_pll_freq_table pll_c_freq_table[] = {
	{ 12000000, 1040000000, 520,  6, 1, 8 },
	{ 13000000, 1040000000, 480,  6, 1, 8 },
	{ 16800000, 1040000000, 495,  8, 1, 8 }, /* actual: 1039.5 MHz */
	{ 19200000, 1040000000, 325,  6, 1, 6 },
	{ 26000000, 1040000000, 520, 13, 1, 8 },
	{ 12000000,  832000000, 416,  6, 1, 8 },
	{ 13000000,  832000000, 832, 13, 1, 8 },
	{ 16800000,  832000000, 396,  8, 1, 8 }, /* actual: 831.6 MHz */
	{ 19200000,  832000000, 260,  6, 1, 8 },
	{ 26000000,  832000000, 416, 13, 1, 8 },
	{ 12000000,  624000000, 624, 12, 1, 8 },
	{ 13000000,  624000000, 624, 13, 1, 8 },
	{ 16800000,  600000000, 520, 14, 1, 8 },
	{ 19200000,  624000000, 520, 16, 1, 8 },
	{ 26000000,  624000000, 624, 26, 1, 8 },
	{ 12000000,  600000000, 600, 12, 1, 8 },
	{ 13000000,  600000000, 600, 13, 1, 8 },
	{ 16800000,  600000000, 500, 14, 1, 8 },
	{ 19200000,  600000000, 375, 12, 1, 6 },
	{ 26000000,  600000000, 600, 26, 1, 8 },
	{ 12000000,  520000000, 520, 12, 1, 8 },
	{ 13000000,  520000000, 520, 13, 1, 8 },
	{ 16800000,  520000000, 495, 16, 1, 8 }, /* actual: 519.75 MHz */
	{ 19200000,  520000000, 325, 12, 1, 6 },
	{ 26000000,  520000000, 520, 26, 1, 8 },
	{ 12000000,  416000000, 416, 12, 1, 8 },
	{ 13000000,  416000000, 416, 13, 1, 8 },
	{ 16800000,  416000000, 396, 16, 1, 8 }, /* actual: 415.8 MHz */
	{ 19200000,  416000000, 260, 12, 1, 6 },
	{ 26000000,  416000000, 416, 26, 1, 8 },
	{        0,          0,   0,  0, 0, 0 },
};

static struct tegra_clk_pll_freq_table pll_m_freq_table[] = {
	{ 12000000, 666000000, 666, 12, 1, 8 },
	{ 13000000, 666000000, 666, 13, 1, 8 },
	{ 16800000, 666000000, 555, 14, 1, 8 },
	{ 19200000, 666000000, 555, 16, 1, 8 },
	{ 26000000, 666000000, 666, 26, 1, 8 },
	{ 12000000, 600000000, 600, 12, 1, 8 },
	{ 13000000, 600000000, 600, 13, 1, 8 },
	{ 16800000, 600000000, 500, 14, 1, 8 },
	{ 19200000, 600000000, 375, 12, 1, 6 },
	{ 26000000, 600000000, 600, 26, 1, 8 },
	{        0,         0,   0,  0, 0, 0 },
};

static struct tegra_clk_pll_freq_table pll_p_freq_table[] = {
	{ 12000000, 216000000, 432, 12, 2, 8 },
	{ 13000000, 216000000, 432, 13, 2, 8 },
	{ 16800000, 216000000, 360, 14, 2, 8 },
	{ 19200000, 216000000, 360, 16, 2, 8 },
	{ 26000000, 216000000, 432, 26, 2, 8 },
	{        0,         0,   0,  0, 0, 0 },
};

static struct tegra_clk_pll_freq_table pll_a_freq_table[] = {
	{  9600000, 564480000, 294,  5, 1, 4 },
	{  9600000, 552960000, 288,  5, 1, 4 },
	{  9600000,  24000000,   5,  2, 1, 1 },
	{ 28800000,  56448000,  49, 25, 1, 1 },
	{ 28800000,  73728000,  64, 25, 1, 1 },
	{ 28800000,  24000000,   5,  6, 1, 1 },
	{        0,         0,   0,  0, 0, 0 },
};

static struct tegra_clk_pll_freq_table pll_d_freq_table[] = {
	{ 12000000,  216000000,  216, 12, 1,  4 },
	{ 13000000,  216000000,  216, 13, 1,  4 },
	{ 16800000,  216000000,  180, 14, 1,  4 },
	{ 19200000,  216000000,  180, 16, 1,  4 },
	{ 26000000,  216000000,  216, 26, 1,  4 },
	{ 12000000,  594000000,  594, 12, 1,  8 },
	{ 13000000,  594000000,  594, 13, 1,  8 },
	{ 16800000,  594000000,  495, 14, 1,  8 },
	{ 19200000,  594000000,  495, 16, 1,  8 },
	{ 26000000,  594000000,  594, 26, 1,  8 },
	{ 12000000, 1000000000, 1000, 12, 1, 12 },
	{ 13000000, 1000000000, 1000, 13, 1, 12 },
	{ 19200000, 1000000000,  625, 12, 1,  8 },
	{ 26000000, 1000000000, 1000, 26, 1, 12 },
	{        0,          0,    0,  0, 0,  0 },
};

static const struct pdiv_map pllu_p[] = {
	{ .pdiv = 1, .hw_val = 1 },
	{ .pdiv = 2, .hw_val = 0 },
	{ .pdiv = 0, .hw_val = 0 },
};

static struct tegra_clk_pll_freq_table pll_u_freq_table[] = {
	{ 12000000, 480000000, 960, 12, 2, 12 },
	{ 13000000, 480000000, 960, 13, 2, 12 },
	{ 16800000, 480000000, 400,  7, 2,  5 },
	{ 19200000, 480000000, 200,  4, 2,  3 },
	{ 26000000, 480000000, 960, 26, 2, 12 },
	{        0,         0,   0,  0, 0,  0 },
};

static struct tegra_clk_pll_freq_table pll_x_freq_table[] = {
	/* 1.7 GHz */
	{ 12000000, 1700000000, 850,   6, 1, 8 },
	{ 13000000, 1700000000, 915,   7, 1, 8 }, /* actual: 1699.2 MHz */
	{ 16800000, 1700000000, 708,   7, 1, 8 }, /* actual: 1699.2 MHz */
	{ 19200000, 1700000000, 885,  10, 1, 8 }, /* actual: 1699.2 MHz */
	{ 26000000, 1700000000, 850,  13, 1, 8 },
	/* 1.6 GHz */
	{ 12000000, 1600000000, 800,   6, 1, 8 },
	{ 13000000, 1600000000, 738,   6, 1, 8 }, /* actual: 1599.0 MHz */
	{ 16800000, 1600000000, 857,   9, 1, 8 }, /* actual: 1599.7 MHz */
	{ 19200000, 1600000000, 500,   6, 1, 8 },
	{ 26000000, 1600000000, 800,  13, 1, 8 },
	/* 1.5 GHz */
	{ 12000000, 1500000000, 750,   6, 1, 8 },
	{ 13000000, 1500000000, 923,   8, 1, 8 }, /* actual: 1499.8 MHz */
	{ 16800000, 1500000000, 625,   7, 1, 8 },
	{ 19200000, 1500000000, 625,   8, 1, 8 },
	{ 26000000, 1500000000, 750,  13, 1, 8 },
	/* 1.4 GHz */
	{ 12000000, 1400000000,  700,  6, 1, 8 },
	{ 13000000, 1400000000,  969,  9, 1, 8 }, /* actual: 1399.7 MHz */
	{ 16800000, 1400000000, 1000, 12, 1, 8 },
	{ 19200000, 1400000000,  875, 12, 1, 8 },
	{ 26000000, 1400000000,  700, 13, 1, 8 },
	/* 1.3 GHz */
	{ 12000000, 1300000000,  975,  9, 1, 8 },
	{ 13000000, 1300000000, 1000, 10, 1, 8 },
	{ 16800000, 1300000000,  928, 12, 1, 8 }, /* actual: 1299.2 MHz */
	{ 19200000, 1300000000,  812, 12, 1, 8 }, /* actual: 1299.2 MHz */
	{ 26000000, 1300000000,  650, 13, 1, 8 },
	/* 1.2 GHz */
	{ 12000000, 1200000000, 1000, 10, 1, 8 },
	{ 13000000, 1200000000,  923, 10, 1, 8 }, /* actual: 1199.9 MHz */
	{ 16800000, 1200000000, 1000, 14, 1, 8 },
	{ 19200000, 1200000000, 1000, 16, 1, 8 },
	{ 26000000, 1200000000,  600, 13, 1, 8 },
	/* 1.1 GHz */
	{ 12000000, 1100000000, 825,   9, 1, 8 },
	{ 13000000, 1100000000, 846,  10, 1, 8 }, /* actual: 1099.8 MHz */
	{ 16800000, 1100000000, 982,  15, 1, 8 }, /* actual: 1099.8 MHz */
	{ 19200000, 1100000000, 859,  15, 1, 8 }, /* actual: 1099.5 MHz */
	{ 26000000, 1100000000, 550,  13, 1, 8 },
	/* 1 GHz */
	{ 12000000, 1000000000, 1000, 12, 1, 8 },
	{ 13000000, 1000000000, 1000, 13, 1, 8 },
	{ 16800000, 1000000000,  833, 14, 1, 8 }, /* actual: 999.6 MHz */
	{ 19200000, 1000000000,  625, 12, 1, 8 },
	{ 26000000, 1000000000, 1000, 26, 1, 8 },
	{        0,          0,    0,  0, 0, 0 },
};

static const struct pdiv_map plle_p[] = {
	{ .pdiv = 18, .hw_val = 18 },
	{ .pdiv = 24, .hw_val = 24 },
	{ .pdiv =  0, .hw_val =  0 },
};

static struct tegra_clk_pll_freq_table pll_e_freq_table[] = {
	/* PLLE special case: use cpcon field to store cml divider value */
	{  12000000, 100000000, 150,  1, 18, 11 },
	{ 216000000, 100000000, 200, 18, 24, 13 },
	{         0,         0,   0,  0,  0,  0 },
};

/* PLL parameters */
static struct tegra_clk_pll_params pll_c_params __ro_after_init = {
	.input_min = 2000000,
	.input_max = 31000000,
	.cf_min = 1000000,
	.cf_max = 6000000,
	.vco_min = 20000000,
	.vco_max = 1400000000,
	.base_reg = PLLC_BASE,
	.misc_reg = PLLC_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLL_MISC_LOCK_ENABLE,
	.lock_delay = 300,
	.freq_table = pll_c_freq_table,
	.flags = TEGRA_PLL_HAS_CPCON | TEGRA_PLL_USE_LOCK |
		 TEGRA_PLL_HAS_LOCK_ENABLE,
};

static struct div_nmp pllm_nmp = {
	.divn_shift = 8,
	.divn_width = 10,
	.override_divn_shift = 5,
	.divm_shift = 0,
	.divm_width = 5,
	.override_divm_shift = 0,
	.divp_shift = 20,
	.divp_width = 3,
	.override_divp_shift = 15,
};

static struct tegra_clk_pll_params pll_m_params __ro_after_init = {
	.input_min = 2000000,
	.input_max = 31000000,
	.cf_min = 1000000,
	.cf_max = 6000000,
	.vco_min = 20000000,
	.vco_max = 1200000000,
	.base_reg = PLLM_BASE,
	.misc_reg = PLLM_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLL_MISC_LOCK_ENABLE,
	.lock_delay = 300,
	.div_nmp = &pllm_nmp,
	.pmc_divnm_reg = PMC_PLLM_WB0_OVERRIDE,
	.pmc_divp_reg = PMC_PLLM_WB0_OVERRIDE,
	.freq_table = pll_m_freq_table,
	.flags = TEGRA_PLLM | TEGRA_PLL_HAS_CPCON |
		 TEGRA_PLL_SET_DCCON | TEGRA_PLL_USE_LOCK |
		 TEGRA_PLL_HAS_LOCK_ENABLE | TEGRA_PLL_FIXED,
};

static struct tegra_clk_pll_params pll_p_params __ro_after_init = {
	.input_min = 2000000,
	.input_max = 31000000,
	.cf_min = 1000000,
	.cf_max = 6000000,
	.vco_min = 20000000,
	.vco_max = 1400000000,
	.base_reg = PLLP_BASE,
	.misc_reg = PLLP_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLL_MISC_LOCK_ENABLE,
	.lock_delay = 300,
	.freq_table = pll_p_freq_table,
	.flags = TEGRA_PLL_FIXED | TEGRA_PLL_HAS_CPCON | TEGRA_PLL_USE_LOCK |
		 TEGRA_PLL_HAS_LOCK_ENABLE,
	.fixed_rate = 408000000,
};

static struct tegra_clk_pll_params pll_a_params = {
	.input_min = 2000000,
	.input_max = 31000000,
	.cf_min = 1000000,
	.cf_max = 6000000,
	.vco_min = 20000000,
	.vco_max = 1400000000,
	.base_reg = PLLA_BASE,
	.misc_reg = PLLA_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLL_MISC_LOCK_ENABLE,
	.lock_delay = 300,
	.freq_table = pll_a_freq_table,
	.flags = TEGRA_PLL_HAS_CPCON | TEGRA_PLL_USE_LOCK |
		 TEGRA_PLL_HAS_LOCK_ENABLE,
};

static struct tegra_clk_pll_params pll_d_params __ro_after_init = {
	.input_min = 2000000,
	.input_max = 40000000,
	.cf_min = 1000000,
	.cf_max = 6000000,
	.vco_min = 40000000,
	.vco_max = 1000000000,
	.base_reg = PLLD_BASE,
	.misc_reg = PLLD_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLLDU_MISC_LOCK_ENABLE,
	.lock_delay = 1000,
	.freq_table = pll_d_freq_table,
	.flags = TEGRA_PLL_HAS_CPCON | TEGRA_PLL_SET_LFCON |
		 TEGRA_PLL_USE_LOCK | TEGRA_PLL_HAS_LOCK_ENABLE,
};

static struct tegra_clk_pll_params pll_d2_params __ro_after_init = {
	.input_min = 2000000,
	.input_max = 40000000,
	.cf_min = 1000000,
	.cf_max = 6000000,
	.vco_min = 40000000,
	.vco_max = 1000000000,
	.base_reg = PLLD2_BASE,
	.misc_reg = PLLD2_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLLDU_MISC_LOCK_ENABLE,
	.lock_delay = 1000,
	.freq_table = pll_d_freq_table,
	.flags = TEGRA_PLL_HAS_CPCON | TEGRA_PLL_SET_LFCON |
		 TEGRA_PLL_USE_LOCK | TEGRA_PLL_HAS_LOCK_ENABLE,
};

static struct tegra_clk_pll_params pll_u_params __ro_after_init = {
	.input_min = 2000000,
	.input_max = 40000000,
	.cf_min = 1000000,
	.cf_max = 6000000,
	.vco_min = 48000000,
	.vco_max = 960000000,
	.base_reg = PLLU_BASE,
	.misc_reg = PLLU_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLLDU_MISC_LOCK_ENABLE,
	.lock_delay = 1000,
	.pdiv_tohw = pllu_p,
	.freq_table = pll_u_freq_table,
	.flags = TEGRA_PLLU | TEGRA_PLL_HAS_CPCON | TEGRA_PLL_SET_LFCON |
		 TEGRA_PLL_HAS_LOCK_ENABLE,
};

static struct tegra_clk_pll_params pll_x_params __ro_after_init = {
	.input_min = 2000000,
	.input_max = 31000000,
	.cf_min = 1000000,
	.cf_max = 6000000,
	.vco_min = 20000000,
	.vco_max = 1700000000,
	.base_reg = PLLX_BASE,
	.misc_reg = PLLX_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLL_MISC_LOCK_ENABLE,
	.lock_delay = 300,
	.freq_table = pll_x_freq_table,
	.flags = TEGRA_PLL_HAS_CPCON | TEGRA_PLL_SET_DCCON |
		 TEGRA_PLL_USE_LOCK | TEGRA_PLL_HAS_LOCK_ENABLE,
};

static struct tegra_clk_pll_params pll_e_params __ro_after_init = {
	.input_min = 12000000,
	.input_max = 216000000,
	.cf_min = 12000000,
	.cf_max = 12000000,
	.vco_min = 1200000000,
	.vco_max = 2400000000U,
	.base_reg = PLLE_BASE,
	.misc_reg = PLLE_MISC,
	.lock_mask = PLLE_MISC_LOCK,
	.lock_enable_bit_idx = PLLE_MISC_LOCK_ENABLE,
	.lock_delay = 300,
	.pdiv_tohw = plle_p,
	.freq_table = pll_e_freq_table,
	.flags = TEGRA_PLLE_CONFIGURE | TEGRA_PLL_FIXED |
		 TEGRA_PLL_HAS_LOCK_ENABLE | TEGRA_PLL_LOCK_MISC,
	.fixed_rate = 100000000,
};

static unsigned long tegra30_input_freq[] = {
	[ 0] = 13000000,
	[ 1] = 16800000,
	[ 4] = 19200000,
	[ 5] = 38400000,
	[ 8] = 12000000,
	[ 9] = 48000000,
	[12] = 26000000,
};

static struct tegra_devclk devclks[] __initdata = {
	{ .con_id = "pll_c", .dt_id = TEGRA30_CLK_PLL_C },
	{ .con_id = "pll_c_out1", .dt_id = TEGRA30_CLK_PLL_C_OUT1 },
	{ .con_id = "pll_p", .dt_id = TEGRA30_CLK_PLL_P },
	{ .con_id = "pll_p_out1", .dt_id = TEGRA30_CLK_PLL_P_OUT1 },
	{ .con_id = "pll_p_out2", .dt_id = TEGRA30_CLK_PLL_P_OUT2 },
	{ .con_id = "pll_p_out3", .dt_id = TEGRA30_CLK_PLL_P_OUT3 },
	{ .con_id = "pll_p_out4", .dt_id = TEGRA30_CLK_PLL_P_OUT4 },
	{ .con_id = "pll_m", .dt_id = TEGRA30_CLK_PLL_M },
	{ .con_id = "pll_m_out1", .dt_id = TEGRA30_CLK_PLL_M_OUT1 },
	{ .con_id = "pll_x", .dt_id = TEGRA30_CLK_PLL_X },
	{ .con_id = "pll_x_out0", .dt_id = TEGRA30_CLK_PLL_X_OUT0 },
	{ .con_id = "pll_u", .dt_id = TEGRA30_CLK_PLL_U },
	{ .con_id = "pll_d", .dt_id = TEGRA30_CLK_PLL_D },
	{ .con_id = "pll_d_out0", .dt_id = TEGRA30_CLK_PLL_D_OUT0 },
	{ .con_id = "pll_d2", .dt_id = TEGRA30_CLK_PLL_D2 },
	{ .con_id = "pll_d2_out0", .dt_id = TEGRA30_CLK_PLL_D2_OUT0 },
	{ .con_id = "pll_a", .dt_id = TEGRA30_CLK_PLL_A },
	{ .con_id = "pll_a_out0", .dt_id = TEGRA30_CLK_PLL_A_OUT0 },
	{ .con_id = "pll_e", .dt_id = TEGRA30_CLK_PLL_E },
	{ .con_id = "spdif_in_sync", .dt_id = TEGRA30_CLK_SPDIF_IN_SYNC },
	{ .con_id = "i2s0_sync", .dt_id = TEGRA30_CLK_I2S0_SYNC },
	{ .con_id = "i2s1_sync", .dt_id = TEGRA30_CLK_I2S1_SYNC },
	{ .con_id = "i2s2_sync", .dt_id = TEGRA30_CLK_I2S2_SYNC },
	{ .con_id = "i2s3_sync", .dt_id = TEGRA30_CLK_I2S3_SYNC },
	{ .con_id = "i2s4_sync", .dt_id = TEGRA30_CLK_I2S4_SYNC },
	{ .con_id = "vimclk_sync", .dt_id = TEGRA30_CLK_VIMCLK_SYNC },
	{ .con_id = "audio0", .dt_id = TEGRA30_CLK_AUDIO0 },
	{ .con_id = "audio1", .dt_id = TEGRA30_CLK_AUDIO1 },
	{ .con_id = "audio2", .dt_id = TEGRA30_CLK_AUDIO2 },
	{ .con_id = "audio3", .dt_id = TEGRA30_CLK_AUDIO3 },
	{ .con_id = "audio4", .dt_id = TEGRA30_CLK_AUDIO4 },
	{ .con_id = "spdif", .dt_id = TEGRA30_CLK_SPDIF },
	{ .con_id = "audio0_2x", .dt_id = TEGRA30_CLK_AUDIO0_2X },
	{ .con_id = "audio1_2x", .dt_id = TEGRA30_CLK_AUDIO1_2X },
	{ .con_id = "audio2_2x", .dt_id = TEGRA30_CLK_AUDIO2_2X },
	{ .con_id = "audio3_2x", .dt_id = TEGRA30_CLK_AUDIO3_2X },
	{ .con_id = "audio4_2x", .dt_id = TEGRA30_CLK_AUDIO4_2X },
	{ .con_id = "spdif_2x", .dt_id = TEGRA30_CLK_SPDIF_2X },
	{ .con_id = "extern1", .dev_id = "clk_out_1", .dt_id = TEGRA30_CLK_EXTERN1 },
	{ .con_id = "extern2", .dev_id = "clk_out_2", .dt_id = TEGRA30_CLK_EXTERN2 },
	{ .con_id = "extern3", .dev_id = "clk_out_3", .dt_id = TEGRA30_CLK_EXTERN3 },
	{ .con_id = "blink", .dt_id = TEGRA30_CLK_BLINK },
	{ .con_id = "cclk_g", .dt_id = TEGRA30_CLK_CCLK_G },
	{ .con_id = "cclk_lp", .dt_id = TEGRA30_CLK_CCLK_LP },
	{ .con_id = "sclk", .dt_id = TEGRA30_CLK_SCLK },
	{ .con_id = "hclk", .dt_id = TEGRA30_CLK_HCLK },
	{ .con_id = "pclk", .dt_id = TEGRA30_CLK_PCLK },
	{ .con_id = "twd", .dt_id = TEGRA30_CLK_TWD },
	{ .con_id = "emc", .dt_id = TEGRA30_CLK_EMC },
	{ .con_id = "clk_32k", .dt_id = TEGRA30_CLK_CLK_32K },
	{ .con_id = "clk_m_div2", .dt_id = TEGRA30_CLK_CLK_M_DIV2 },
	{ .con_id = "clk_m_div4", .dt_id = TEGRA30_CLK_CLK_M_DIV4 },
	{ .con_id = "cml0", .dt_id = TEGRA30_CLK_CML0 },
	{ .con_id = "cml1", .dt_id = TEGRA30_CLK_CML1 },
	{ .con_id = "clk_m", .dt_id = TEGRA30_CLK_CLK_M },
	{ .con_id = "pll_ref", .dt_id = TEGRA30_CLK_PLL_REF },
	{ .con_id = "csus", .dev_id = "tengra_camera", .dt_id = TEGRA30_CLK_CSUS },
	{ .con_id = "vcp", .dev_id = "tegra-avp", .dt_id = TEGRA30_CLK_VCP },
	{ .con_id = "bsea", .dev_id = "tegra-avp", .dt_id = TEGRA30_CLK_BSEA },
	{ .con_id = "bsev", .dev_id = "tegra-aes", .dt_id = TEGRA30_CLK_BSEV },
	{ .con_id = "dsia", .dev_id = "tegradc.0", .dt_id = TEGRA30_CLK_DSIA },
	{ .con_id = "csi", .dev_id = "tegra_camera", .dt_id = TEGRA30_CLK_CSI },
	{ .con_id = "isp", .dev_id = "tegra_camera", .dt_id = TEGRA30_CLK_ISP },
	{ .con_id = "pcie", .dev_id = "tegra-pcie", .dt_id = TEGRA30_CLK_PCIE },
	{ .con_id = "afi", .dev_id = "tegra-pcie", .dt_id = TEGRA30_CLK_AFI },
	{ .con_id = "fuse", .dt_id = TEGRA30_CLK_FUSE },
	{ .con_id = "fuse_burn", .dev_id = "fuse-tegra", .dt_id = TEGRA30_CLK_FUSE_BURN },
	{ .con_id = "apbif", .dev_id = "tegra30-ahub", .dt_id = TEGRA30_CLK_APBIF },
	{ .con_id = "hda2hdmi", .dev_id = "tegra30-hda", .dt_id = TEGRA30_CLK_HDA2HDMI },
	{ .dev_id = "tegra-apbdma", .dt_id = TEGRA30_CLK_APBDMA },
	{ .dev_id = "rtc-tegra", .dt_id = TEGRA30_CLK_RTC },
	{ .dev_id = "timer", .dt_id = TEGRA30_CLK_TIMER },
	{ .dev_id = "tegra-kbc", .dt_id = TEGRA30_CLK_KBC },
	{ .dev_id = "fsl-tegra-udc", .dt_id = TEGRA30_CLK_USBD },
	{ .dev_id = "tegra-ehci.1", .dt_id = TEGRA30_CLK_USB2 },
	{ .dev_id = "tegra-ehci.2", .dt_id = TEGRA30_CLK_USB2 },
	{ .dev_id = "kfuse-tegra", .dt_id = TEGRA30_CLK_KFUSE },
	{ .dev_id = "tegra_sata_cold", .dt_id = TEGRA30_CLK_SATA_COLD },
	{ .dev_id = "dtv", .dt_id = TEGRA30_CLK_DTV },
	{ .dev_id = "tegra30-i2s.0", .dt_id = TEGRA30_CLK_I2S0 },
	{ .dev_id = "tegra30-i2s.1", .dt_id = TEGRA30_CLK_I2S1 },
	{ .dev_id = "tegra30-i2s.2", .dt_id = TEGRA30_CLK_I2S2 },
	{ .dev_id = "tegra30-i2s.3", .dt_id = TEGRA30_CLK_I2S3 },
	{ .dev_id = "tegra30-i2s.4", .dt_id = TEGRA30_CLK_I2S4 },
	{ .con_id = "spdif_out", .dev_id = "tegra30-spdif", .dt_id = TEGRA30_CLK_SPDIF_OUT },
	{ .con_id = "spdif_in", .dev_id = "tegra30-spdif", .dt_id = TEGRA30_CLK_SPDIF_IN },
	{ .con_id = "d_audio", .dev_id = "tegra30-ahub", .dt_id = TEGRA30_CLK_D_AUDIO },
	{ .dev_id = "tegra30-dam.0", .dt_id = TEGRA30_CLK_DAM0 },
	{ .dev_id = "tegra30-dam.1", .dt_id = TEGRA30_CLK_DAM1 },
	{ .dev_id = "tegra30-dam.2", .dt_id = TEGRA30_CLK_DAM2 },
	{ .con_id = "hda", .dev_id = "tegra30-hda", .dt_id = TEGRA30_CLK_HDA },
	{ .con_id = "hda2codec_2x", .dev_id = "tegra30-hda", .dt_id = TEGRA30_CLK_HDA2CODEC_2X },
	{ .dev_id = "spi_tegra.0", .dt_id = TEGRA30_CLK_SBC1 },
	{ .dev_id = "spi_tegra.1", .dt_id = TEGRA30_CLK_SBC2 },
	{ .dev_id = "spi_tegra.2", .dt_id = TEGRA30_CLK_SBC3 },
	{ .dev_id = "spi_tegra.3", .dt_id = TEGRA30_CLK_SBC4 },
	{ .dev_id = "spi_tegra.4", .dt_id = TEGRA30_CLK_SBC5 },
	{ .dev_id = "spi_tegra.5", .dt_id = TEGRA30_CLK_SBC6 },
	{ .dev_id = "tegra_sata_oob", .dt_id = TEGRA30_CLK_SATA_OOB },
	{ .dev_id = "tegra_sata", .dt_id = TEGRA30_CLK_SATA },
	{ .dev_id = "tegra_nand", .dt_id = TEGRA30_CLK_NDFLASH },
	{ .dev_id = "tegra_nand_speed", .dt_id = TEGRA30_CLK_NDSPEED },
	{ .dev_id = "vfir", .dt_id = TEGRA30_CLK_VFIR },
	{ .dev_id = "csite", .dt_id = TEGRA30_CLK_CSITE },
	{ .dev_id = "la", .dt_id = TEGRA30_CLK_LA },
	{ .dev_id = "tegra_w1", .dt_id = TEGRA30_CLK_OWR },
	{ .dev_id = "mipi", .dt_id = TEGRA30_CLK_MIPI },
	{ .dev_id = "tegra-tsensor", .dt_id = TEGRA30_CLK_TSENSOR },
	{ .dev_id = "i2cslow", .dt_id = TEGRA30_CLK_I2CSLOW },
	{ .dev_id = "vde", .dt_id = TEGRA30_CLK_VDE },
	{ .con_id = "vi", .dev_id = "tegra_camera", .dt_id = TEGRA30_CLK_VI },
	{ .dev_id = "epp", .dt_id = TEGRA30_CLK_EPP },
	{ .dev_id = "mpe", .dt_id = TEGRA30_CLK_MPE },
	{ .dev_id = "host1x", .dt_id = TEGRA30_CLK_HOST1X },
	{ .dev_id = "3d", .dt_id = TEGRA30_CLK_GR3D },
	{ .dev_id = "3d2", .dt_id = TEGRA30_CLK_GR3D2 },
	{ .dev_id = "2d", .dt_id = TEGRA30_CLK_GR2D },
	{ .dev_id = "se", .dt_id = TEGRA30_CLK_SE },
	{ .dev_id = "mselect", .dt_id = TEGRA30_CLK_MSELECT },
	{ .dev_id = "tegra-nor", .dt_id = TEGRA30_CLK_NOR },
	{ .dev_id = "sdhci-tegra.0", .dt_id = TEGRA30_CLK_SDMMC1 },
	{ .dev_id = "sdhci-tegra.1", .dt_id = TEGRA30_CLK_SDMMC2 },
	{ .dev_id = "sdhci-tegra.2", .dt_id = TEGRA30_CLK_SDMMC3 },
	{ .dev_id = "sdhci-tegra.3", .dt_id = TEGRA30_CLK_SDMMC4 },
	{ .dev_id = "cve", .dt_id = TEGRA30_CLK_CVE },
	{ .dev_id = "tvo", .dt_id = TEGRA30_CLK_TVO },
	{ .dev_id = "tvdac", .dt_id = TEGRA30_CLK_TVDAC },
	{ .dev_id = "actmon", .dt_id = TEGRA30_CLK_ACTMON },
	{ .con_id = "vi_sensor", .dev_id = "tegra_camera", .dt_id = TEGRA30_CLK_VI_SENSOR },
	{ .con_id = "div-clk", .dev_id = "tegra-i2c.0", .dt_id = TEGRA30_CLK_I2C1 },
	{ .con_id = "div-clk", .dev_id = "tegra-i2c.1", .dt_id = TEGRA30_CLK_I2C2 },
	{ .con_id = "div-clk", .dev_id = "tegra-i2c.2", .dt_id = TEGRA30_CLK_I2C3 },
	{ .con_id = "div-clk", .dev_id = "tegra-i2c.3", .dt_id = TEGRA30_CLK_I2C4 },
	{ .con_id = "div-clk", .dev_id = "tegra-i2c.4", .dt_id = TEGRA30_CLK_I2C5 },
	{ .dev_id = "tegra_uart.0", .dt_id = TEGRA30_CLK_UARTA },
	{ .dev_id = "tegra_uart.1", .dt_id = TEGRA30_CLK_UARTB },
	{ .dev_id = "tegra_uart.2", .dt_id = TEGRA30_CLK_UARTC },
	{ .dev_id = "tegra_uart.3", .dt_id = TEGRA30_CLK_UARTD },
	{ .dev_id = "tegra_uart.4", .dt_id = TEGRA30_CLK_UARTE },
	{ .dev_id = "hdmi", .dt_id = TEGRA30_CLK_HDMI },
	{ .dev_id = "extern1", .dt_id = TEGRA30_CLK_EXTERN1 },
	{ .dev_id = "extern2", .dt_id = TEGRA30_CLK_EXTERN2 },
	{ .dev_id = "extern3", .dt_id = TEGRA30_CLK_EXTERN3 },
	{ .dev_id = "pwm", .dt_id = TEGRA30_CLK_PWM },
	{ .dev_id = "tegradc.0", .dt_id = TEGRA30_CLK_DISP1 },
	{ .dev_id = "tegradc.1", .dt_id = TEGRA30_CLK_DISP2 },
	{ .dev_id = "tegradc.1", .dt_id = TEGRA30_CLK_DSIB },
};

static struct tegra_clk tegra30_clks[tegra_clk_max] __initdata = {
	[tegra_clk_clk_32k] = { .dt_id = TEGRA30_CLK_CLK_32K, .present = true },
	[tegra_clk_clk_m] = { .dt_id = TEGRA30_CLK_CLK_M, .present = true },
	[tegra_clk_clk_m_div2] = { .dt_id = TEGRA30_CLK_CLK_M_DIV2, .present = true },
	[tegra_clk_clk_m_div4] = { .dt_id = TEGRA30_CLK_CLK_M_DIV4, .present = true },
	[tegra_clk_pll_ref] = { .dt_id = TEGRA30_CLK_PLL_REF, .present = true },
	[tegra_clk_spdif_in_sync] = { .dt_id = TEGRA30_CLK_SPDIF_IN_SYNC, .present = true },
	[tegra_clk_i2s0_sync] = { .dt_id = TEGRA30_CLK_I2S0_SYNC, .present = true },
	[tegra_clk_i2s1_sync] = { .dt_id = TEGRA30_CLK_I2S1_SYNC, .present = true },
	[tegra_clk_i2s2_sync] = { .dt_id = TEGRA30_CLK_I2S2_SYNC, .present = true },
	[tegra_clk_i2s3_sync] = { .dt_id = TEGRA30_CLK_I2S3_SYNC, .present = true },
	[tegra_clk_i2s4_sync] = { .dt_id = TEGRA30_CLK_I2S4_SYNC, .present = true },
	[tegra_clk_vimclk_sync] = { .dt_id = TEGRA30_CLK_VIMCLK_SYNC, .present = true },
	[tegra_clk_audio0] = { .dt_id = TEGRA30_CLK_AUDIO0, .present = true },
	[tegra_clk_audio1] = { .dt_id = TEGRA30_CLK_AUDIO1, .present = true },
	[tegra_clk_audio2] = { .dt_id = TEGRA30_CLK_AUDIO2, .present = true },
	[tegra_clk_audio3] = { .dt_id = TEGRA30_CLK_AUDIO3, .present = true },
	[tegra_clk_audio4] = { .dt_id = TEGRA30_CLK_AUDIO4, .present = true },
	[tegra_clk_spdif] = { .dt_id = TEGRA30_CLK_SPDIF, .present = true },
	[tegra_clk_audio0_mux] = { .dt_id = TEGRA30_CLK_AUDIO0_MUX, .present = true },
	[tegra_clk_audio1_mux] = { .dt_id = TEGRA30_CLK_AUDIO1_MUX, .present = true },
	[tegra_clk_audio2_mux] = { .dt_id = TEGRA30_CLK_AUDIO2_MUX, .present = true },
	[tegra_clk_audio3_mux] = { .dt_id = TEGRA30_CLK_AUDIO3_MUX, .present = true },
	[tegra_clk_audio4_mux] = { .dt_id = TEGRA30_CLK_AUDIO4_MUX, .present = true },
	[tegra_clk_spdif_mux] = { .dt_id = TEGRA30_CLK_SPDIF_MUX, .present = true },
	[tegra_clk_audio0_2x] = { .dt_id = TEGRA30_CLK_AUDIO0_2X, .present = true },
	[tegra_clk_audio1_2x] = { .dt_id = TEGRA30_CLK_AUDIO1_2X, .present = true },
	[tegra_clk_audio2_2x] = { .dt_id = TEGRA30_CLK_AUDIO2_2X, .present = true },
	[tegra_clk_audio3_2x] = { .dt_id = TEGRA30_CLK_AUDIO3_2X, .present = true },
	[tegra_clk_audio4_2x] = { .dt_id = TEGRA30_CLK_AUDIO4_2X, .present = true },
	[tegra_clk_spdif_2x] = { .dt_id = TEGRA30_CLK_SPDIF_2X, .present = true },
	[tegra_clk_clk_out_1] = { .dt_id = TEGRA30_CLK_CLK_OUT_1, .present = true },
	[tegra_clk_clk_out_2] = { .dt_id = TEGRA30_CLK_CLK_OUT_2, .present = true },
	[tegra_clk_clk_out_3] = { .dt_id = TEGRA30_CLK_CLK_OUT_3, .present = true },
	[tegra_clk_blink] = { .dt_id = TEGRA30_CLK_BLINK, .present = true },
	[tegra_clk_clk_out_1_mux] = { .dt_id = TEGRA30_CLK_CLK_OUT_1_MUX, .present = true },
	[tegra_clk_clk_out_2_mux] = { .dt_id = TEGRA30_CLK_CLK_OUT_2_MUX, .present = true },
	[tegra_clk_clk_out_3_mux] = { .dt_id = TEGRA30_CLK_CLK_OUT_3_MUX, .present = true },
	[tegra_clk_hclk] = { .dt_id = TEGRA30_CLK_HCLK, .present = true },
	[tegra_clk_pclk] = { .dt_id = TEGRA30_CLK_PCLK, .present = true },
	[tegra_clk_i2s0] = { .dt_id = TEGRA30_CLK_I2S0, .present = true },
	[tegra_clk_i2s1] = { .dt_id = TEGRA30_CLK_I2S1, .present = true },
	[tegra_clk_i2s2] = { .dt_id = TEGRA30_CLK_I2S2, .present = true },
	[tegra_clk_i2s3] = { .dt_id = TEGRA30_CLK_I2S3, .present = true },
	[tegra_clk_i2s4] = { .dt_id = TEGRA30_CLK_I2S4, .present = true },
	[tegra_clk_spdif_in] = { .dt_id = TEGRA30_CLK_SPDIF_IN, .present = true },
	[tegra_clk_hda] = { .dt_id = TEGRA30_CLK_HDA, .present = true },
	[tegra_clk_hda2codec_2x] = { .dt_id = TEGRA30_CLK_HDA2CODEC_2X, .present = true },
	[tegra_clk_sbc1] = { .dt_id = TEGRA30_CLK_SBC1, .present = true },
	[tegra_clk_sbc2] = { .dt_id = TEGRA30_CLK_SBC2, .present = true },
	[tegra_clk_sbc3] = { .dt_id = TEGRA30_CLK_SBC3, .present = true },
	[tegra_clk_sbc4] = { .dt_id = TEGRA30_CLK_SBC4, .present = true },
	[tegra_clk_sbc5] = { .dt_id = TEGRA30_CLK_SBC5, .present = true },
	[tegra_clk_sbc6] = { .dt_id = TEGRA30_CLK_SBC6, .present = true },
	[tegra_clk_ndflash] = { .dt_id = TEGRA30_CLK_NDFLASH, .present = true },
	[tegra_clk_ndspeed] = { .dt_id = TEGRA30_CLK_NDSPEED, .present = true },
	[tegra_clk_vfir] = { .dt_id = TEGRA30_CLK_VFIR, .present = true },
	[tegra_clk_la] = { .dt_id = TEGRA30_CLK_LA, .present = true },
	[tegra_clk_csite] = { .dt_id = TEGRA30_CLK_CSITE, .present = true },
	[tegra_clk_owr] = { .dt_id = TEGRA30_CLK_OWR, .present = true },
	[tegra_clk_mipi] = { .dt_id = TEGRA30_CLK_MIPI, .present = true },
	[tegra_clk_tsensor] = { .dt_id = TEGRA30_CLK_TSENSOR, .present = true },
	[tegra_clk_i2cslow] = { .dt_id = TEGRA30_CLK_I2CSLOW, .present = true },
	[tegra_clk_vde] = { .dt_id = TEGRA30_CLK_VDE, .present = true },
	[tegra_clk_vi] = { .dt_id = TEGRA30_CLK_VI, .present = true },
	[tegra_clk_epp] = { .dt_id = TEGRA30_CLK_EPP, .present = true },
	[tegra_clk_mpe] = { .dt_id = TEGRA30_CLK_MPE, .present = true },
	[tegra_clk_host1x] = { .dt_id = TEGRA30_CLK_HOST1X, .present = true },
	[tegra_clk_gr2d] = { .dt_id = TEGRA30_CLK_GR2D, .present = true },
	[tegra_clk_gr3d] = { .dt_id = TEGRA30_CLK_GR3D, .present = true },
	[tegra_clk_mselect] = { .dt_id = TEGRA30_CLK_MSELECT, .present = true },
	[tegra_clk_nor] = { .dt_id = TEGRA30_CLK_NOR, .present = true },
	[tegra_clk_sdmmc1] = { .dt_id = TEGRA30_CLK_SDMMC1, .present = true },
	[tegra_clk_sdmmc2] = { .dt_id = TEGRA30_CLK_SDMMC2, .present = true },
	[tegra_clk_sdmmc3] = { .dt_id = TEGRA30_CLK_SDMMC3, .present = true },
	[tegra_clk_sdmmc4] = { .dt_id = TEGRA30_CLK_SDMMC4, .present = true },
	[tegra_clk_cve] = { .dt_id = TEGRA30_CLK_CVE, .present = true },
	[tegra_clk_tvo] = { .dt_id = TEGRA30_CLK_TVO, .present = true },
	[tegra_clk_tvdac] = { .dt_id = TEGRA30_CLK_TVDAC, .present = true },
	[tegra_clk_actmon] = { .dt_id = TEGRA30_CLK_ACTMON, .present = true },
	[tegra_clk_vi_sensor] = { .dt_id = TEGRA30_CLK_VI_SENSOR, .present = true },
	[tegra_clk_i2c1] = { .dt_id = TEGRA30_CLK_I2C1, .present = true },
	[tegra_clk_i2c2] = { .dt_id = TEGRA30_CLK_I2C2, .present = true },
	[tegra_clk_i2c3] = { .dt_id = TEGRA30_CLK_I2C3, .present = true },
	[tegra_clk_i2c4] = { .dt_id = TEGRA30_CLK_I2C4, .present = true },
	[tegra_clk_i2c5] = { .dt_id = TEGRA30_CLK_I2C5, .present = true },
	[tegra_clk_uarta] = { .dt_id = TEGRA30_CLK_UARTA, .present = true },
	[tegra_clk_uartb] = { .dt_id = TEGRA30_CLK_UARTB, .present = true },
	[tegra_clk_uartc] = { .dt_id = TEGRA30_CLK_UARTC, .present = true },
	[tegra_clk_uartd] = { .dt_id = TEGRA30_CLK_UARTD, .present = true },
	[tegra_clk_uarte] = { .dt_id = TEGRA30_CLK_UARTE, .present = true },
	[tegra_clk_extern1] = { .dt_id = TEGRA30_CLK_EXTERN1, .present = true },
	[tegra_clk_extern2] = { .dt_id = TEGRA30_CLK_EXTERN2, .present = true },
	[tegra_clk_extern3] = { .dt_id = TEGRA30_CLK_EXTERN3, .present = true },
	[tegra_clk_disp1] = { .dt_id = TEGRA30_CLK_DISP1, .present = true },
	[tegra_clk_disp2] = { .dt_id = TEGRA30_CLK_DISP2, .present = true },
	[tegra_clk_ahbdma] = { .dt_id = TEGRA30_CLK_AHBDMA, .present = true },
	[tegra_clk_apbdma] = { .dt_id = TEGRA30_CLK_APBDMA, .present = true },
	[tegra_clk_rtc] = { .dt_id = TEGRA30_CLK_RTC, .present = true },
	[tegra_clk_timer] = { .dt_id = TEGRA30_CLK_TIMER, .present = true },
	[tegra_clk_kbc] = { .dt_id = TEGRA30_CLK_KBC, .present = true },
	[tegra_clk_csus] = { .dt_id = TEGRA30_CLK_CSUS, .present = true },
	[tegra_clk_vcp] = { .dt_id = TEGRA30_CLK_VCP, .present = true },
	[tegra_clk_bsea] = { .dt_id = TEGRA30_CLK_BSEA, .present = true },
	[tegra_clk_bsev] = { .dt_id = TEGRA30_CLK_BSEV, .present = true },
	[tegra_clk_usbd] = { .dt_id = TEGRA30_CLK_USBD, .present = true },
	[tegra_clk_usb2] = { .dt_id = TEGRA30_CLK_USB2, .present = true },
	[tegra_clk_usb3] = { .dt_id = TEGRA30_CLK_USB3, .present = true },
	[tegra_clk_csi] = { .dt_id = TEGRA30_CLK_CSI, .present = true },
	[tegra_clk_isp] = { .dt_id = TEGRA30_CLK_ISP, .present = true },
	[tegra_clk_kfuse] = { .dt_id = TEGRA30_CLK_KFUSE, .present = true },
	[tegra_clk_fuse] = { .dt_id = TEGRA30_CLK_FUSE, .present = true },
	[tegra_clk_fuse_burn] = { .dt_id = TEGRA30_CLK_FUSE_BURN, .present = true },
	[tegra_clk_apbif] = { .dt_id = TEGRA30_CLK_APBIF, .present = true },
	[tegra_clk_hda2hdmi] = { .dt_id = TEGRA30_CLK_HDA2HDMI, .present = true },
	[tegra_clk_sata_cold] = { .dt_id = TEGRA30_CLK_SATA_COLD, .present = true },
	[tegra_clk_sata_oob] = { .dt_id = TEGRA30_CLK_SATA_OOB, .present = true },
	[tegra_clk_sata] = { .dt_id = TEGRA30_CLK_SATA, .present = true },
	[tegra_clk_dtv] = { .dt_id = TEGRA30_CLK_DTV, .present = true },
	[tegra_clk_pll_p] = { .dt_id = TEGRA30_CLK_PLL_P, .present = true },
	[tegra_clk_pll_p_out1] = { .dt_id = TEGRA30_CLK_PLL_P_OUT1, .present = true },
	[tegra_clk_pll_p_out2] = { .dt_id = TEGRA30_CLK_PLL_P_OUT2, .present = true },
	[tegra_clk_pll_p_out3] = { .dt_id = TEGRA30_CLK_PLL_P_OUT3, .present = true },
	[tegra_clk_pll_p_out4] = { .dt_id = TEGRA30_CLK_PLL_P_OUT4, .present = true },
	[tegra_clk_pll_a] = { .dt_id = TEGRA30_CLK_PLL_A, .present = true },
	[tegra_clk_pll_a_out0] = { .dt_id = TEGRA30_CLK_PLL_A_OUT0, .present = true },
	[tegra_clk_cec] = { .dt_id = TEGRA30_CLK_CEC, .present = true },
	[tegra_clk_emc] = { .dt_id = TEGRA30_CLK_EMC, .present = false },
};

static const char *pll_e_parents[] = { "pll_ref", "pll_p" };

static void __init tegra30_pll_init(void)
{
	struct clk *clk;

	/* PLLC */
	clk = tegra_clk_register_pll("pll_c", "pll_ref", clk_base, pmc_base, 0,
				     &pll_c_params, NULL);
	clks[TEGRA30_CLK_PLL_C] = clk;

	/* PLLC_OUT1 */
	clk = tegra_clk_register_divider("pll_c_out1_div", "pll_c",
				clk_base + PLLC_OUT, 0, TEGRA_DIVIDER_ROUND_UP,
				8, 8, 1, NULL);
	clk = tegra_clk_register_pll_out("pll_c_out1", "pll_c_out1_div",
				clk_base + PLLC_OUT, 1, 0, CLK_SET_RATE_PARENT,
				0, NULL);
	clks[TEGRA30_CLK_PLL_C_OUT1] = clk;

	/* PLLM */
	clk = tegra_clk_register_pll("pll_m", "pll_ref", clk_base, pmc_base,
			    CLK_SET_RATE_GATE, &pll_m_params, NULL);
	clks[TEGRA30_CLK_PLL_M] = clk;

	/* PLLM_OUT1 */
	clk = tegra_clk_register_divider("pll_m_out1_div", "pll_m",
				clk_base + PLLM_OUT, 0, TEGRA_DIVIDER_ROUND_UP,
				8, 8, 1, NULL);
	clk = tegra_clk_register_pll_out("pll_m_out1", "pll_m_out1_div",
				clk_base + PLLM_OUT, 1, 0,
				CLK_SET_RATE_PARENT, 0, NULL);
	clks[TEGRA30_CLK_PLL_M_OUT1] = clk;

	/* PLLX */
	clk = tegra_clk_register_pll("pll_x", "pll_ref", clk_base, pmc_base, 0,
			    &pll_x_params, NULL);
	clks[TEGRA30_CLK_PLL_X] = clk;

	/* PLLX_OUT0 */
	clk = clk_register_fixed_factor(NULL, "pll_x_out0", "pll_x",
					CLK_SET_RATE_PARENT, 1, 2);
	clks[TEGRA30_CLK_PLL_X_OUT0] = clk;

	/* PLLU */
	clk = tegra_clk_register_pllu("pll_u", "pll_ref", clk_base, 0,
				      &pll_u_params, NULL);
	clks[TEGRA30_CLK_PLL_U] = clk;

	/* PLLD */
	clk = tegra_clk_register_pll("pll_d", "pll_ref", clk_base, pmc_base, 0,
			    &pll_d_params, &pll_d_lock);
	clks[TEGRA30_CLK_PLL_D] = clk;

	/* PLLD_OUT0 */
	clk = clk_register_fixed_factor(NULL, "pll_d_out0", "pll_d",
					CLK_SET_RATE_PARENT, 1, 2);
	clks[TEGRA30_CLK_PLL_D_OUT0] = clk;

	/* PLLD2 */
	clk = tegra_clk_register_pll("pll_d2", "pll_ref", clk_base, pmc_base, 0,
			    &pll_d2_params, NULL);
	clks[TEGRA30_CLK_PLL_D2] = clk;

	/* PLLD2_OUT0 */
	clk = clk_register_fixed_factor(NULL, "pll_d2_out0", "pll_d2",
					CLK_SET_RATE_PARENT, 1, 2);
	clks[TEGRA30_CLK_PLL_D2_OUT0] = clk;

	/* PLLE */
	clk = clk_register_mux(NULL, "pll_e_mux", pll_e_parents,
			       ARRAY_SIZE(pll_e_parents),
			       CLK_SET_RATE_NO_REPARENT,
			       clk_base + PLLE_AUX, 2, 1, 0, NULL);
	clk = tegra_clk_register_plle("pll_e", "pll_e_mux", clk_base, pmc_base,
			     CLK_GET_RATE_NOCACHE, &pll_e_params, NULL);
	clks[TEGRA30_CLK_PLL_E] = clk;
}

static const char *cclk_g_parents[] = { "clk_m", "pll_c", "clk_32k", "pll_m",
					"pll_p_cclkg", "pll_p_out4_cclkg",
					"pll_p_out3_cclkg", "unused", "pll_x" };
static const char *cclk_lp_parents[] = { "clk_m", "pll_c", "clk_32k", "pll_m",
					 "pll_p_cclklp", "pll_p_out4_cclklp",
					 "pll_p_out3_cclklp", "unused", "pll_x",
					 "pll_x_out0" };
static const char *sclk_parents[] = { "clk_m", "pll_c_out1", "pll_p_out4",
				      "pll_p_out3", "pll_p_out2", "unused",
				      "clk_32k", "pll_m_out1" };

static void __init tegra30_super_clk_init(void)
{
	struct clk *clk;

	/*
	 * Clock input to cclk_g divided from pll_p using
	 * U71 divider of cclk_g.
	 */
	clk = tegra_clk_register_divider("pll_p_cclkg", "pll_p",
				clk_base + SUPER_CCLKG_DIVIDER, 0,
				TEGRA_DIVIDER_INT, 16, 8, 1, NULL);
	clk_register_clkdev(clk, "pll_p_cclkg", NULL);

	/*
	 * Clock input to cclk_g divided from pll_p_out3 using
	 * U71 divider of cclk_g.
	 */
	clk = tegra_clk_register_divider("pll_p_out3_cclkg", "pll_p_out3",
				clk_base + SUPER_CCLKG_DIVIDER, 0,
				TEGRA_DIVIDER_INT, 16, 8, 1, NULL);
	clk_register_clkdev(clk, "pll_p_out3_cclkg", NULL);

	/*
	 * Clock input to cclk_g divided from pll_p_out4 using
	 * U71 divider of cclk_g.
	 */
	clk = tegra_clk_register_divider("pll_p_out4_cclkg", "pll_p_out4",
				clk_base + SUPER_CCLKG_DIVIDER, 0,
				TEGRA_DIVIDER_INT, 16, 8, 1, NULL);
	clk_register_clkdev(clk, "pll_p_out4_cclkg", NULL);

	/* CCLKG */
	clk = tegra_clk_register_super_mux("cclk_g", cclk_g_parents,
				  ARRAY_SIZE(cclk_g_parents),
				  CLK_SET_RATE_PARENT,
				  clk_base + CCLKG_BURST_POLICY,
				  0, 4, 0, 0, NULL);
	clks[TEGRA30_CLK_CCLK_G] = clk;

	/*
	 * Clock input to cclk_lp divided from pll_p using
	 * U71 divider of cclk_lp.
	 */
	clk = tegra_clk_register_divider("pll_p_cclklp", "pll_p",
				clk_base + SUPER_CCLKLP_DIVIDER, 0,
				TEGRA_DIVIDER_INT, 16, 8, 1, NULL);
	clk_register_clkdev(clk, "pll_p_cclklp", NULL);

	/*
	 * Clock input to cclk_lp divided from pll_p_out3 using
	 * U71 divider of cclk_lp.
	 */
	clk = tegra_clk_register_divider("pll_p_out3_cclklp", "pll_p_out3",
				clk_base + SUPER_CCLKLP_DIVIDER, 0,
				TEGRA_DIVIDER_INT, 16, 8, 1, NULL);
	clk_register_clkdev(clk, "pll_p_out3_cclklp", NULL);

	/*
	 * Clock input to cclk_lp divided from pll_p_out4 using
	 * U71 divider of cclk_lp.
	 */
	clk = tegra_clk_register_divider("pll_p_out4_cclklp", "pll_p_out4",
				clk_base + SUPER_CCLKLP_DIVIDER, 0,
				TEGRA_DIVIDER_INT, 16, 8, 1, NULL);
	clk_register_clkdev(clk, "pll_p_out4_cclklp", NULL);

	/* CCLKLP */
	clk = tegra_clk_register_super_mux("cclk_lp", cclk_lp_parents,
				  ARRAY_SIZE(cclk_lp_parents),
				  CLK_SET_RATE_PARENT,
				  clk_base + CCLKLP_BURST_POLICY,
				  TEGRA_DIVIDER_2, 4, 8, 9,
			      NULL);
	clks[TEGRA30_CLK_CCLK_LP] = clk;

	/* SCLK */
	clk = tegra_clk_register_super_mux("sclk", sclk_parents,
				  ARRAY_SIZE(sclk_parents),
				  CLK_SET_RATE_PARENT | CLK_IS_CRITICAL,
				  clk_base + SCLK_BURST_POLICY,
				  0, 4, 0, 0, NULL);
	clks[TEGRA30_CLK_SCLK] = clk;

	/* twd */
	clk = clk_register_fixed_factor(NULL, "twd", "cclk_g",
					CLK_SET_RATE_PARENT, 1, 2);
	clks[TEGRA30_CLK_TWD] = clk;

	tegra_super_clk_gen4_init(clk_base, pmc_base, tegra30_clks, NULL);
}

static const char *mux_pllacp_clkm[] = { "pll_a_out0", "unused", "pll_p",
					 "clk_m" };
static const char *mux_pllpcm_clkm[] = { "pll_p", "pll_c", "pll_m", "clk_m" };
static const char *spdif_out_parents[] = { "pll_a_out0", "spdif_2x", "pll_p",
					   "clk_m" };
static const char *mux_pllmcpa[] = { "pll_m", "pll_c", "pll_p", "pll_a_out0" };
static const char *mux_pllpmdacd2_clkm[] = { "pll_p", "pll_m", "pll_d_out0",
					     "pll_a_out0", "pll_c",
					     "pll_d2_out0", "clk_m" };
static const char *mux_plld_out0_plld2_out0[] = { "pll_d_out0",
						  "pll_d2_out0" };
static const char *pwm_parents[] = { "pll_p", "pll_c", "clk_32k", "clk_m" };

static struct tegra_periph_init_data tegra_periph_clk_list[] = {
	TEGRA_INIT_DATA_MUX("spdif_out", spdif_out_parents, CLK_SOURCE_SPDIF_OUT, 10, TEGRA_PERIPH_ON_APB, TEGRA30_CLK_SPDIF_OUT),
	TEGRA_INIT_DATA_MUX("d_audio", mux_pllacp_clkm, CLK_SOURCE_D_AUDIO, 106, 0, TEGRA30_CLK_D_AUDIO),
	TEGRA_INIT_DATA_MUX("dam0", mux_pllacp_clkm, CLK_SOURCE_DAM0, 108, 0, TEGRA30_CLK_DAM0),
	TEGRA_INIT_DATA_MUX("dam1", mux_pllacp_clkm, CLK_SOURCE_DAM1, 109, 0, TEGRA30_CLK_DAM1),
	TEGRA_INIT_DATA_MUX("dam2", mux_pllacp_clkm, CLK_SOURCE_DAM2, 110, 0, TEGRA30_CLK_DAM2),
	TEGRA_INIT_DATA_INT("3d2", mux_pllmcpa, CLK_SOURCE_3D2, 98, TEGRA_PERIPH_MANUAL_RESET, TEGRA30_CLK_GR3D2),
	TEGRA_INIT_DATA_INT("se", mux_pllpcm_clkm, CLK_SOURCE_SE, 127, 0, TEGRA30_CLK_SE),
	TEGRA_INIT_DATA_MUX8("hdmi", mux_pllpmdacd2_clkm, CLK_SOURCE_HDMI, 51, 0, TEGRA30_CLK_HDMI),
	TEGRA_INIT_DATA("pwm", NULL, NULL, pwm_parents, CLK_SOURCE_PWM, 28, 2, 0, 0, 8, 1, 0, 17, TEGRA_PERIPH_ON_APB, TEGRA30_CLK_PWM),
};

static struct tegra_periph_init_data tegra_periph_nodiv_clk_list[] = {
	TEGRA_INIT_DATA_NODIV("dsib", mux_plld_out0_plld2_out0, CLK_SOURCE_DSIB, 25, 1, 82, 0, TEGRA30_CLK_DSIB),
};

static void __init tegra30_periph_clk_init(void)
{
	struct tegra_periph_init_data *data;
	struct clk *clk;
	unsigned int i;

	/* dsia */
	clk = tegra_clk_register_periph_gate("dsia", "pll_d_out0", 0, clk_base,
				    0, 48, periph_clk_enb_refcnt);
	clks[TEGRA30_CLK_DSIA] = clk;

	/* pcie */
	clk = tegra_clk_register_periph_gate("pcie", "clk_m", 0, clk_base, 0,
				    70, periph_clk_enb_refcnt);
	clks[TEGRA30_CLK_PCIE] = clk;

	/* afi */
	clk = tegra_clk_register_periph_gate("afi", "clk_m", 0, clk_base, 0, 72,
				    periph_clk_enb_refcnt);
	clks[TEGRA30_CLK_AFI] = clk;

	/* emc */
	clk = tegra20_clk_register_emc(clk_base + CLK_SOURCE_EMC, true);

	clks[TEGRA30_CLK_EMC] = clk;

	clk = tegra_clk_register_mc("mc", "emc", clk_base + CLK_SOURCE_EMC,
				    NULL);
	clks[TEGRA30_CLK_MC] = clk;

	/* cml0 */
	clk = clk_register_gate(NULL, "cml0", "pll_e", 0, clk_base + PLLE_AUX,
				0, 0, &cml_lock);
	clks[TEGRA30_CLK_CML0] = clk;

	/* cml1 */
	clk = clk_register_gate(NULL, "cml1", "pll_e", 0, clk_base + PLLE_AUX,
				1, 0, &cml_lock);
	clks[TEGRA30_CLK_CML1] = clk;

	for (i = 0; i < ARRAY_SIZE(tegra_periph_clk_list); i++) {
		data = &tegra_periph_clk_list[i];
		clk = tegra_clk_register_periph_data(clk_base, data);
		clks[data->clk_id] = clk;
	}

	for (i = 0; i < ARRAY_SIZE(tegra_periph_nodiv_clk_list); i++) {
		data = &tegra_periph_nodiv_clk_list[i];
		clk = tegra_clk_register_periph_nodiv(data->name,
					data->p.parent_names,
					data->num_parents, &data->periph,
					clk_base, data->offset);
		clks[data->clk_id] = clk;
	}

	tegra_periph_clk_init(clk_base, pmc_base, tegra30_clks, &pll_p_params);
}

/* Tegra30 CPU clock and reset control functions */
static void tegra30_wait_cpu_in_reset(u32 cpu)
{
	unsigned int reg;

	do {
		reg = readl(clk_base +
			    TEGRA30_CLK_RST_CONTROLLER_CPU_CMPLX_STATUS);
		cpu_relax();
	} while (!(reg & (1 << cpu)));	/* check CPU been reset or not */

	return;
}

static void tegra30_put_cpu_in_reset(u32 cpu)
{
	writel(CPU_RESET(cpu),
	       clk_base + TEGRA_CLK_RST_CONTROLLER_RST_CPU_CMPLX_SET);
	dmb();
}

static void tegra30_cpu_out_of_reset(u32 cpu)
{
	writel(CPU_RESET(cpu),
	       clk_base + TEGRA_CLK_RST_CONTROLLER_RST_CPU_CMPLX_CLR);
	wmb();
}

static void tegra30_enable_cpu_clock(u32 cpu)
{
	unsigned int reg;

	writel(CPU_CLOCK(cpu),
	       clk_base + TEGRA30_CLK_RST_CONTROLLER_CLK_CPU_CMPLX_CLR);
	reg = readl(clk_base +
		    TEGRA30_CLK_RST_CONTROLLER_CLK_CPU_CMPLX_CLR);
}

static void tegra30_disable_cpu_clock(u32 cpu)
{
	unsigned int reg;

	reg = readl(clk_base + TEGRA_CLK_RST_CONTROLLER_CLK_CPU_CMPLX);
	writel(reg | CPU_CLOCK(cpu),
	       clk_base + TEGRA_CLK_RST_CONTROLLER_CLK_CPU_CMPLX);
}

#ifdef CONFIG_PM_SLEEP
static bool tegra30_cpu_rail_off_ready(void)
{
	unsigned int cpu_rst_status;
	int cpu_pwr_status;

	cpu_rst_status = readl(clk_base +
				TEGRA30_CLK_RST_CONTROLLER_CPU_CMPLX_STATUS);
	cpu_pwr_status = tegra_pmc_cpu_is_powered(1) ||
			 tegra_pmc_cpu_is_powered(2) ||
			 tegra_pmc_cpu_is_powered(3);

	if (((cpu_rst_status & 0xE) != 0xE) || cpu_pwr_status)
		return false;

	return true;
}

static void tegra30_cpu_clock_suspend(void)
{
	/* switch coresite to clk_m, save off original source */
	tegra30_cpu_clk_sctx.clk_csite_src =
				readl(clk_base + CLK_RESET_SOURCE_CSITE);
	writel(3 << 30, clk_base + CLK_RESET_SOURCE_CSITE);

	tegra30_cpu_clk_sctx.cpu_burst =
				readl(clk_base + CLK_RESET_CCLK_BURST);
	tegra30_cpu_clk_sctx.pllx_base =
				readl(clk_base + CLK_RESET_PLLX_BASE);
	tegra30_cpu_clk_sctx.pllx_misc =
				readl(clk_base + CLK_RESET_PLLX_MISC);
	tegra30_cpu_clk_sctx.cclk_divider =
				readl(clk_base + CLK_RESET_CCLK_DIVIDER);
}

static void tegra30_cpu_clock_resume(void)
{
	unsigned int reg, policy;
	u32 misc, base;

	/* Is CPU complex already running on PLLX? */
	reg = readl(clk_base + CLK_RESET_CCLK_BURST);
	policy = (reg >> CLK_RESET_CCLK_BURST_POLICY_SHIFT) & 0xF;

	if (policy == CLK_RESET_CCLK_IDLE_POLICY)
		reg = (reg >> CLK_RESET_CCLK_IDLE_POLICY_SHIFT) & 0xF;
	else if (policy == CLK_RESET_CCLK_RUN_POLICY)
		reg = (reg >> CLK_RESET_CCLK_RUN_POLICY_SHIFT) & 0xF;
	else
		BUG();

	if (reg != CLK_RESET_CCLK_BURST_POLICY_PLLX) {
		misc = readl_relaxed(clk_base + CLK_RESET_PLLX_MISC);
		base = readl_relaxed(clk_base + CLK_RESET_PLLX_BASE);

		if (misc != tegra30_cpu_clk_sctx.pllx_misc ||
		    base != tegra30_cpu_clk_sctx.pllx_base) {
			/* restore PLLX settings if CPU is on different PLL */
			writel(tegra30_cpu_clk_sctx.pllx_misc,
						clk_base + CLK_RESET_PLLX_MISC);
			writel(tegra30_cpu_clk_sctx.pllx_base,
						clk_base + CLK_RESET_PLLX_BASE);

			/* wait for PLL stabilization if PLLX was enabled */
			if (tegra30_cpu_clk_sctx.pllx_base & (1 << 30))
				udelay(300);
		}
	}

	/*
	 * Restore original burst policy setting for calls resulting from CPU
	 * LP2 in idle or system suspend.
	 */
	writel(tegra30_cpu_clk_sctx.cclk_divider,
					clk_base + CLK_RESET_CCLK_DIVIDER);
	writel(tegra30_cpu_clk_sctx.cpu_burst,
					clk_base + CLK_RESET_CCLK_BURST);

	writel(tegra30_cpu_clk_sctx.clk_csite_src,
					clk_base + CLK_RESET_SOURCE_CSITE);
}
#endif

static struct tegra_cpu_car_ops tegra30_cpu_car_ops = {
	.wait_for_reset	= tegra30_wait_cpu_in_reset,
	.put_in_reset	= tegra30_put_cpu_in_reset,
	.out_of_reset	= tegra30_cpu_out_of_reset,
	.enable_clock	= tegra30_enable_cpu_clock,
	.disable_clock	= tegra30_disable_cpu_clock,
#ifdef CONFIG_PM_SLEEP
	.rail_off_ready	= tegra30_cpu_rail_off_ready,
	.suspend	= tegra30_cpu_clock_suspend,
	.resume		= tegra30_cpu_clock_resume,
#endif
};

static struct tegra_clk_init_table init_table[] __initdata = {
	{ TEGRA30_CLK_UARTA, TEGRA30_CLK_PLL_P, 408000000, 0 },
	{ TEGRA30_CLK_UARTB, TEGRA30_CLK_PLL_P, 408000000, 0 },
	{ TEGRA30_CLK_UARTC, TEGRA30_CLK_PLL_P, 408000000, 0 },
	{ TEGRA30_CLK_UARTD, TEGRA30_CLK_PLL_P, 408000000, 0 },
	{ TEGRA30_CLK_UARTE, TEGRA30_CLK_PLL_P, 408000000, 0 },
	{ TEGRA30_CLK_PLL_A, TEGRA30_CLK_CLK_MAX, 564480000, 1 },
	{ TEGRA30_CLK_PLL_A_OUT0, TEGRA30_CLK_CLK_MAX, 11289600, 1 },
	{ TEGRA30_CLK_EXTERN1, TEGRA30_CLK_PLL_A_OUT0, 0, 1 },
	{ TEGRA30_CLK_CLK_OUT_1_MUX, TEGRA30_CLK_EXTERN1, 0, 0 },
	{ TEGRA30_CLK_CLK_OUT_1, TEGRA30_CLK_CLK_MAX, 0, 1 },
	{ TEGRA30_CLK_BLINK, TEGRA30_CLK_CLK_MAX, 0, 1 },
	{ TEGRA30_CLK_I2S0, TEGRA30_CLK_PLL_A_OUT0, 11289600, 0 },
	{ TEGRA30_CLK_I2S1, TEGRA30_CLK_PLL_A_OUT0, 11289600, 0 },
	{ TEGRA30_CLK_I2S2, TEGRA30_CLK_PLL_A_OUT0, 11289600, 0 },
	{ TEGRA30_CLK_I2S3, TEGRA30_CLK_PLL_A_OUT0, 11289600, 0 },
	{ TEGRA30_CLK_I2S4, TEGRA30_CLK_PLL_A_OUT0, 11289600, 0 },
	{ TEGRA30_CLK_SDMMC1, TEGRA30_CLK_PLL_P, 48000000, 0 },
	{ TEGRA30_CLK_SDMMC2, TEGRA30_CLK_PLL_P, 48000000, 0 },
	{ TEGRA30_CLK_SDMMC3, TEGRA30_CLK_PLL_P, 48000000, 0 },
	{ TEGRA30_CLK_CSITE, TEGRA30_CLK_CLK_MAX, 0, 1 },
	{ TEGRA30_CLK_MSELECT, TEGRA30_CLK_CLK_MAX, 0, 1 },
	{ TEGRA30_CLK_SBC1, TEGRA30_CLK_PLL_P, 100000000, 0 },
	{ TEGRA30_CLK_SBC2, TEGRA30_CLK_PLL_P, 100000000, 0 },
	{ TEGRA30_CLK_SBC3, TEGRA30_CLK_PLL_P, 100000000, 0 },
	{ TEGRA30_CLK_SBC4, TEGRA30_CLK_PLL_P, 100000000, 0 },
	{ TEGRA30_CLK_SBC5, TEGRA30_CLK_PLL_P, 100000000, 0 },
	{ TEGRA30_CLK_SBC6, TEGRA30_CLK_PLL_P, 100000000, 0 },
	{ TEGRA30_CLK_PLL_C, TEGRA30_CLK_CLK_MAX, 600000000, 0 },
	{ TEGRA30_CLK_HOST1X, TEGRA30_CLK_PLL_C, 150000000, 0 },
	{ TEGRA30_CLK_DISP1, TEGRA30_CLK_PLL_P, 600000000, 0 },
	{ TEGRA30_CLK_DISP2, TEGRA30_CLK_PLL_P, 600000000, 0 },
	{ TEGRA30_CLK_TWD, TEGRA30_CLK_CLK_MAX, 0, 1 },
	{ TEGRA30_CLK_GR2D, TEGRA30_CLK_PLL_C, 300000000, 0 },
	{ TEGRA30_CLK_GR3D, TEGRA30_CLK_PLL_C, 300000000, 0 },
	{ TEGRA30_CLK_GR3D2, TEGRA30_CLK_PLL_C, 300000000, 0 },
	{ TEGRA30_CLK_PLL_U, TEGRA30_CLK_CLK_MAX, 480000000, 0 },
	{ TEGRA30_CLK_VDE, TEGRA30_CLK_CLK_MAX, 600000000, 0 },
	{ TEGRA30_CLK_SPDIF_IN_SYNC, TEGRA30_CLK_CLK_MAX, 24000000, 0 },
	{ TEGRA30_CLK_I2S0_SYNC, TEGRA30_CLK_CLK_MAX, 24000000, 0 },
	{ TEGRA30_CLK_I2S1_SYNC, TEGRA30_CLK_CLK_MAX, 24000000, 0 },
	{ TEGRA30_CLK_I2S2_SYNC, TEGRA30_CLK_CLK_MAX, 24000000, 0 },
	{ TEGRA30_CLK_I2S3_SYNC, TEGRA30_CLK_CLK_MAX, 24000000, 0 },
	{ TEGRA30_CLK_I2S4_SYNC, TEGRA30_CLK_CLK_MAX, 24000000, 0 },
	{ TEGRA30_CLK_VIMCLK_SYNC, TEGRA30_CLK_CLK_MAX, 24000000, 0 },
	/* must be the last entry */
	{ TEGRA30_CLK_CLK_MAX, TEGRA30_CLK_CLK_MAX, 0, 0 },
};

static void __init tegra30_clock_apply_init_table(void)
{
	tegra_init_from_table(init_table, clks, TEGRA30_CLK_CLK_MAX);
}

/*
 * Some clocks may be used by different drivers depending on the board
 * configuration.  List those here to register them twice in the clock lookup
 * table under two names.
 */
static struct tegra_clk_duplicate tegra_clk_duplicates[] = {
	TEGRA_CLK_DUPLICATE(TEGRA30_CLK_USBD, "utmip-pad", NULL),
	TEGRA_CLK_DUPLICATE(TEGRA30_CLK_USBD, "tegra-ehci.0", NULL),
	TEGRA_CLK_DUPLICATE(TEGRA30_CLK_USBD, "tegra-otg", NULL),
	TEGRA_CLK_DUPLICATE(TEGRA30_CLK_BSEV, "tegra-avp", "bsev"),
	TEGRA_CLK_DUPLICATE(TEGRA30_CLK_BSEV, "nvavp", "bsev"),
	TEGRA_CLK_DUPLICATE(TEGRA30_CLK_VDE, "tegra-aes", "vde"),
	TEGRA_CLK_DUPLICATE(TEGRA30_CLK_BSEA, "tegra-aes", "bsea"),
	TEGRA_CLK_DUPLICATE(TEGRA30_CLK_BSEA, "nvavp", "bsea"),
	TEGRA_CLK_DUPLICATE(TEGRA30_CLK_CML1, "tegra_sata_cml", NULL),
	TEGRA_CLK_DUPLICATE(TEGRA30_CLK_CML0, "tegra_pcie", "cml"),
	TEGRA_CLK_DUPLICATE(TEGRA30_CLK_VCP, "nvavp", "vcp"),
	/* must be the last entry */
	TEGRA_CLK_DUPLICATE(TEGRA30_CLK_CLK_MAX, NULL, NULL),
};

static const struct of_device_id pmc_match[] __initconst = {
	{ .compatible = "nvidia,tegra30-pmc" },
	{ },
};

static struct tegra_audio_clk_info tegra30_audio_plls[] = {
	{ "pll_a", &pll_a_params, tegra_clk_pll_a, "pll_p_out1" },
};

static struct clk *tegra30_clk_src_onecell_get(struct of_phandle_args *clkspec,
					       void *data)
{
	struct clk_hw *hw;
	struct clk *clk;

	clk = of_clk_src_onecell_get(clkspec, data);
	if (IS_ERR(clk))
		return clk;

	hw = __clk_get_hw(clk);

	if (clkspec->args[0] == TEGRA30_CLK_EMC) {
		if (!tegra20_clk_emc_driver_available(hw))
			return ERR_PTR(-EPROBE_DEFER);
	}

	return clk;
}

static void __init tegra30_clock_init(struct device_node *np)
{
	struct device_node *node;

	clk_base = of_iomap(np, 0);
	if (!clk_base) {
		pr_err("ioremap tegra30 CAR failed\n");
		return;
	}

	node = of_find_matching_node(NULL, pmc_match);
	if (!node) {
		pr_err("Failed to find pmc node\n");
		BUG();
	}

	pmc_base = of_iomap(node, 0);
	if (!pmc_base) {
		pr_err("Can't map pmc registers\n");
		BUG();
	}

	clks = tegra_clk_init(clk_base, TEGRA30_CLK_CLK_MAX,
				TEGRA30_CLK_PERIPH_BANKS);
	if (!clks)
		return;

	if (tegra_osc_clk_init(clk_base, tegra30_clks, tegra30_input_freq,
			       ARRAY_SIZE(tegra30_input_freq), 1, &input_freq,
			       NULL) < 0)
		return;

	tegra_fixed_clk_init(tegra30_clks);
	tegra30_pll_init();
	tegra30_super_clk_init();
	tegra30_periph_clk_init();
	tegra_audio_clk_init(clk_base, pmc_base, tegra30_clks,
			     tegra30_audio_plls,
			     ARRAY_SIZE(tegra30_audio_plls), 24000000);
	tegra_pmc_clk_init(pmc_base, tegra30_clks);

	tegra_init_dup_clks(tegra_clk_duplicates, clks, TEGRA30_CLK_CLK_MAX);

	tegra_add_of_provider(np, tegra30_clk_src_onecell_get);
	tegra_register_devclks(devclks, ARRAY_SIZE(devclks));

	tegra_clk_apply_init_table = tegra30_clock_apply_init_table;

	tegra_cpu_car_ops = &tegra30_cpu_car_ops;
}
CLK_OF_DECLARE(tegra30, "nvidia,tegra30-car", tegra30_clock_init);
