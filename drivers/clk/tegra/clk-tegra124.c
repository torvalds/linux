/*
 * Copyright (c) 2012-2014 NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/io.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/clk/tegra.h>
#include <dt-bindings/clock/tegra124-car.h>
#include <dt-bindings/reset/tegra124-car.h>

#include "clk.h"
#include "clk-id.h"

/*
 * TEGRA124_CAR_BANK_COUNT: the number of peripheral clock register
 * banks present in the Tegra124/132 CAR IP block.  The banks are
 * identified by single letters, e.g.: L, H, U, V, W, X.  See
 * periph_regs[] in drivers/clk/tegra/clk.c
 */
#define TEGRA124_CAR_BANK_COUNT			6

#define CLK_SOURCE_CSITE 0x1d4
#define CLK_SOURCE_EMC 0x19c

#define RST_DFLL_DVCO			0x2f4
#define DVFS_DFLL_RESET_SHIFT		0

#define PLLC_BASE 0x80
#define PLLC_OUT 0x84
#define PLLC_MISC2 0x88
#define PLLC_MISC 0x8c
#define PLLC2_BASE 0x4e8
#define PLLC2_MISC 0x4ec
#define PLLC3_BASE 0x4fc
#define PLLC3_MISC 0x500
#define PLLM_BASE 0x90
#define PLLM_OUT 0x94
#define PLLM_MISC 0x9c
#define PLLP_BASE 0xa0
#define PLLP_MISC 0xac
#define PLLA_BASE 0xb0
#define PLLA_MISC 0xbc
#define PLLD_BASE 0xd0
#define PLLD_MISC 0xdc
#define PLLU_BASE 0xc0
#define PLLU_MISC 0xcc
#define PLLX_BASE 0xe0
#define PLLX_MISC 0xe4
#define PLLX_MISC2 0x514
#define PLLX_MISC3 0x518
#define PLLE_BASE 0xe8
#define PLLE_MISC 0xec
#define PLLD2_BASE 0x4b8
#define PLLD2_MISC 0x4bc
#define PLLE_AUX 0x48c
#define PLLRE_BASE 0x4c4
#define PLLRE_MISC 0x4c8
#define PLLDP_BASE 0x590
#define PLLDP_MISC 0x594
#define PLLC4_BASE 0x5a4
#define PLLC4_MISC 0x5a8

#define PLLC_IDDQ_BIT 26
#define PLLRE_IDDQ_BIT 16
#define PLLSS_IDDQ_BIT 19

#define PLL_BASE_LOCK BIT(27)
#define PLLE_MISC_LOCK BIT(11)
#define PLLRE_MISC_LOCK BIT(24)

#define PLL_MISC_LOCK_ENABLE 18
#define PLLC_MISC_LOCK_ENABLE 24
#define PLLDU_MISC_LOCK_ENABLE 22
#define PLLE_MISC_LOCK_ENABLE 9
#define PLLRE_MISC_LOCK_ENABLE 30
#define PLLSS_MISC_LOCK_ENABLE 30

#define PLLXC_SW_MAX_P 6

#define PMC_PLLM_WB0_OVERRIDE 0x1dc
#define PMC_PLLM_WB0_OVERRIDE_2 0x2b0

#define CCLKG_BURST_POLICY 0x368

/* Tegra CPU clock and reset control regs */
#define CLK_RST_CONTROLLER_CPU_CMPLX_STATUS	0x470

#ifdef CONFIG_PM_SLEEP
static struct cpu_clk_suspend_context {
	u32 clk_csite_src;
	u32 cclkg_burst;
	u32 cclkg_divider;
} tegra124_cpu_clk_sctx;
#endif

static void __iomem *clk_base;
static void __iomem *pmc_base;

static unsigned long osc_freq;
static unsigned long pll_ref_freq;

static DEFINE_SPINLOCK(pll_d_lock);
static DEFINE_SPINLOCK(pll_e_lock);
static DEFINE_SPINLOCK(pll_re_lock);
static DEFINE_SPINLOCK(pll_u_lock);
static DEFINE_SPINLOCK(emc_lock);

/* possible OSC frequencies in Hz */
static unsigned long tegra124_input_freq[] = {
	[ 0] = 13000000,
	[ 1] = 16800000,
	[ 4] = 19200000,
	[ 5] = 38400000,
	[ 8] = 12000000,
	[ 9] = 48000000,
	[12] = 26000000,
};

static struct div_nmp pllxc_nmp = {
	.divm_shift = 0,
	.divm_width = 8,
	.divn_shift = 8,
	.divn_width = 8,
	.divp_shift = 20,
	.divp_width = 4,
};

static const struct pdiv_map pllxc_p[] = {
	{ .pdiv =  1, .hw_val =  0 },
	{ .pdiv =  2, .hw_val =  1 },
	{ .pdiv =  3, .hw_val =  2 },
	{ .pdiv =  4, .hw_val =  3 },
	{ .pdiv =  5, .hw_val =  4 },
	{ .pdiv =  6, .hw_val =  5 },
	{ .pdiv =  8, .hw_val =  6 },
	{ .pdiv = 10, .hw_val =  7 },
	{ .pdiv = 12, .hw_val =  8 },
	{ .pdiv = 16, .hw_val =  9 },
	{ .pdiv = 12, .hw_val = 10 },
	{ .pdiv = 16, .hw_val = 11 },
	{ .pdiv = 20, .hw_val = 12 },
	{ .pdiv = 24, .hw_val = 13 },
	{ .pdiv = 32, .hw_val = 14 },
	{ .pdiv =  0, .hw_val =  0 },
};

static struct tegra_clk_pll_freq_table pll_x_freq_table[] = {
	/* 1 GHz */
	{ 12000000, 1000000000, 83, 1, 1, 0 }, /* actual: 996.0 MHz */
	{ 13000000, 1000000000, 76, 1, 1, 0 }, /* actual: 988.0 MHz */
	{ 16800000, 1000000000, 59, 1, 1, 0 }, /* actual: 991.2 MHz */
	{ 19200000, 1000000000, 52, 1, 1, 0 }, /* actual: 998.4 MHz */
	{ 26000000, 1000000000, 76, 2, 1, 0 }, /* actual: 988.0 MHz */
	{        0,          0,  0, 0, 0, 0 },
};

static struct tegra_clk_pll_params pll_x_params = {
	.input_min = 12000000,
	.input_max = 800000000,
	.cf_min = 12000000,
	.cf_max = 19200000,	/* s/w policy, h/w capability 50 MHz */
	.vco_min = 700000000,
	.vco_max = 3000000000UL,
	.base_reg = PLLX_BASE,
	.misc_reg = PLLX_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLL_MISC_LOCK_ENABLE,
	.lock_delay = 300,
	.iddq_reg = PLLX_MISC3,
	.iddq_bit_idx = 3,
	.max_p = 6,
	.dyn_ramp_reg = PLLX_MISC2,
	.stepa_shift = 16,
	.stepb_shift = 24,
	.pdiv_tohw = pllxc_p,
	.div_nmp = &pllxc_nmp,
	.freq_table = pll_x_freq_table,
	.flags = TEGRA_PLL_USE_LOCK | TEGRA_PLL_HAS_LOCK_ENABLE,
};

static struct tegra_clk_pll_freq_table pll_c_freq_table[] = {
	{ 12000000, 624000000, 104, 1, 2, 0 },
	{ 12000000, 600000000, 100, 1, 2, 0 },
	{ 13000000, 600000000,  92, 1, 2, 0 }, /* actual: 598.0 MHz */
	{ 16800000, 600000000,  71, 1, 2, 0 }, /* actual: 596.4 MHz */
	{ 19200000, 600000000,  62, 1, 2, 0 }, /* actual: 595.2 MHz */
	{ 26000000, 600000000,  92, 2, 2, 0 }, /* actual: 598.0 MHz */
	{        0,         0,   0, 0, 0, 0 },
};

static struct tegra_clk_pll_params pll_c_params = {
	.input_min = 12000000,
	.input_max = 800000000,
	.cf_min = 12000000,
	.cf_max = 19200000, /* s/w policy, h/w capability 50 MHz */
	.vco_min = 600000000,
	.vco_max = 1400000000,
	.base_reg = PLLC_BASE,
	.misc_reg = PLLC_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLLC_MISC_LOCK_ENABLE,
	.lock_delay = 300,
	.iddq_reg = PLLC_MISC,
	.iddq_bit_idx = PLLC_IDDQ_BIT,
	.max_p = PLLXC_SW_MAX_P,
	.dyn_ramp_reg = PLLC_MISC2,
	.stepa_shift = 17,
	.stepb_shift = 9,
	.pdiv_tohw = pllxc_p,
	.div_nmp = &pllxc_nmp,
	.freq_table = pll_c_freq_table,
	.flags = TEGRA_PLL_USE_LOCK | TEGRA_PLL_HAS_LOCK_ENABLE,
};

static struct div_nmp pllcx_nmp = {
	.divm_shift = 0,
	.divm_width = 2,
	.divn_shift = 8,
	.divn_width = 8,
	.divp_shift = 20,
	.divp_width = 3,
};

static const struct pdiv_map pllc_p[] = {
	{ .pdiv =  1, .hw_val = 0 },
	{ .pdiv =  2, .hw_val = 1 },
	{ .pdiv =  3, .hw_val = 2 },
	{ .pdiv =  4, .hw_val = 3 },
	{ .pdiv =  6, .hw_val = 4 },
	{ .pdiv =  8, .hw_val = 5 },
	{ .pdiv = 12, .hw_val = 6 },
	{ .pdiv = 16, .hw_val = 7 },
	{ .pdiv =  0, .hw_val = 0 },
};

static struct tegra_clk_pll_freq_table pll_cx_freq_table[] = {
	{ 12000000, 600000000, 100, 1, 2, 0 },
	{ 13000000, 600000000,  92, 1, 2, 0 }, /* actual: 598.0 MHz */
	{ 16800000, 600000000,  71, 1, 2, 0 }, /* actual: 596.4 MHz */
	{ 19200000, 600000000,  62, 1, 2, 0 }, /* actual: 595.2 MHz */
	{ 26000000, 600000000,  92, 2, 2, 0 }, /* actual: 598.0 MHz */
	{        0,         0,   0, 0, 0, 0 },
};

static struct tegra_clk_pll_params pll_c2_params = {
	.input_min = 12000000,
	.input_max = 48000000,
	.cf_min = 12000000,
	.cf_max = 19200000,
	.vco_min = 600000000,
	.vco_max = 1200000000,
	.base_reg = PLLC2_BASE,
	.misc_reg = PLLC2_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLL_MISC_LOCK_ENABLE,
	.lock_delay = 300,
	.pdiv_tohw = pllc_p,
	.div_nmp = &pllcx_nmp,
	.max_p = 7,
	.ext_misc_reg[0] = 0x4f0,
	.ext_misc_reg[1] = 0x4f4,
	.ext_misc_reg[2] = 0x4f8,
	.freq_table = pll_cx_freq_table,
	.flags = TEGRA_PLL_USE_LOCK,
};

static struct tegra_clk_pll_params pll_c3_params = {
	.input_min = 12000000,
	.input_max = 48000000,
	.cf_min = 12000000,
	.cf_max = 19200000,
	.vco_min = 600000000,
	.vco_max = 1200000000,
	.base_reg = PLLC3_BASE,
	.misc_reg = PLLC3_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLL_MISC_LOCK_ENABLE,
	.lock_delay = 300,
	.pdiv_tohw = pllc_p,
	.div_nmp = &pllcx_nmp,
	.max_p = 7,
	.ext_misc_reg[0] = 0x504,
	.ext_misc_reg[1] = 0x508,
	.ext_misc_reg[2] = 0x50c,
	.freq_table = pll_cx_freq_table,
	.flags = TEGRA_PLL_USE_LOCK,
};

static struct div_nmp pllss_nmp = {
	.divm_shift = 0,
	.divm_width = 8,
	.divn_shift = 8,
	.divn_width = 8,
	.divp_shift = 20,
	.divp_width = 4,
};

static const struct pdiv_map pll12g_ssd_esd_p[] = {
	{ .pdiv =  1, .hw_val =  0 },
	{ .pdiv =  2, .hw_val =  1 },
	{ .pdiv =  3, .hw_val =  2 },
	{ .pdiv =  4, .hw_val =  3 },
	{ .pdiv =  5, .hw_val =  4 },
	{ .pdiv =  6, .hw_val =  5 },
	{ .pdiv =  8, .hw_val =  6 },
	{ .pdiv = 10, .hw_val =  7 },
	{ .pdiv = 12, .hw_val =  8 },
	{ .pdiv = 16, .hw_val =  9 },
	{ .pdiv = 12, .hw_val = 10 },
	{ .pdiv = 16, .hw_val = 11 },
	{ .pdiv = 20, .hw_val = 12 },
	{ .pdiv = 24, .hw_val = 13 },
	{ .pdiv = 32, .hw_val = 14 },
	{ .pdiv =  0, .hw_val =  0 },
};

static struct tegra_clk_pll_freq_table pll_c4_freq_table[] = {
	{ 12000000, 600000000, 100, 1, 2, 0 },
	{ 13000000, 600000000,  92, 1, 2, 0 }, /* actual: 598.0 MHz */
	{ 16800000, 600000000,  71, 1, 2, 0 }, /* actual: 596.4 MHz */
	{ 19200000, 600000000,  62, 1, 2, 0 }, /* actual: 595.2 MHz */
	{ 26000000, 600000000,  92, 2, 2, 0 }, /* actual: 598.0 MHz */
	{        0,         0,   0, 0, 0, 0 },
};

static struct tegra_clk_pll_params pll_c4_params = {
	.input_min = 12000000,
	.input_max = 1000000000,
	.cf_min = 12000000,
	.cf_max = 19200000, /* s/w policy, h/w capability 38 MHz */
	.vco_min = 600000000,
	.vco_max = 1200000000,
	.base_reg = PLLC4_BASE,
	.misc_reg = PLLC4_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLLSS_MISC_LOCK_ENABLE,
	.lock_delay = 300,
	.iddq_reg = PLLC4_BASE,
	.iddq_bit_idx = PLLSS_IDDQ_BIT,
	.pdiv_tohw = pll12g_ssd_esd_p,
	.div_nmp = &pllss_nmp,
	.ext_misc_reg[0] = 0x5ac,
	.ext_misc_reg[1] = 0x5b0,
	.ext_misc_reg[2] = 0x5b4,
	.freq_table = pll_c4_freq_table,
	.flags = TEGRA_PLL_USE_LOCK | TEGRA_PLL_HAS_LOCK_ENABLE,
};

static const struct pdiv_map pllm_p[] = {
	{ .pdiv =  1, .hw_val =  0 },
	{ .pdiv =  2, .hw_val =  1 },
	{ .pdiv =  3, .hw_val =  2 },
	{ .pdiv =  4, .hw_val =  3 },
	{ .pdiv =  5, .hw_val =  4 },
	{ .pdiv =  6, .hw_val =  5 },
	{ .pdiv =  8, .hw_val =  6 },
	{ .pdiv = 10, .hw_val =  7 },
	{ .pdiv = 12, .hw_val =  8 },
	{ .pdiv = 16, .hw_val =  9 },
	{ .pdiv = 12, .hw_val = 10 },
	{ .pdiv = 16, .hw_val = 11 },
	{ .pdiv = 20, .hw_val = 12 },
	{ .pdiv = 24, .hw_val = 13 },
	{ .pdiv = 32, .hw_val = 14 },
	{ .pdiv =  0, .hw_val =  0 },
};

static struct tegra_clk_pll_freq_table pll_m_freq_table[] = {
	{ 12000000, 800000000, 66, 1, 1, 0 }, /* actual: 792.0 MHz */
	{ 13000000, 800000000, 61, 1, 1, 0 }, /* actual: 793.0 MHz */
	{ 16800000, 800000000, 47, 1, 1, 0 }, /* actual: 789.6 MHz */
	{ 19200000, 800000000, 41, 1, 1, 0 }, /* actual: 787.2 MHz */
	{ 26000000, 800000000, 61, 2, 1, 0 }, /* actual: 793.0 MHz */
	{        0,         0,  0, 0, 0, 0},
};

static struct div_nmp pllm_nmp = {
	.divm_shift = 0,
	.divm_width = 8,
	.override_divm_shift = 0,
	.divn_shift = 8,
	.divn_width = 8,
	.override_divn_shift = 8,
	.divp_shift = 20,
	.divp_width = 1,
	.override_divp_shift = 27,
};

static struct tegra_clk_pll_params pll_m_params = {
	.input_min = 12000000,
	.input_max = 500000000,
	.cf_min = 12000000,
	.cf_max = 19200000,	/* s/w policy, h/w capability 50 MHz */
	.vco_min = 400000000,
	.vco_max = 1066000000,
	.base_reg = PLLM_BASE,
	.misc_reg = PLLM_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLL_MISC_LOCK_ENABLE,
	.lock_delay = 300,
	.max_p = 5,
	.pdiv_tohw = pllm_p,
	.div_nmp = &pllm_nmp,
	.pmc_divnm_reg = PMC_PLLM_WB0_OVERRIDE,
	.pmc_divp_reg = PMC_PLLM_WB0_OVERRIDE_2,
	.freq_table = pll_m_freq_table,
	.flags = TEGRA_PLL_USE_LOCK | TEGRA_PLL_HAS_LOCK_ENABLE,
};

static struct tegra_clk_pll_freq_table pll_e_freq_table[] = {
	/* PLLE special case: use cpcon field to store cml divider value */
	{ 336000000, 100000000, 100, 21, 16, 11 },
	{ 312000000, 100000000, 200, 26, 24, 13 },
	{  13000000, 100000000, 200,  1, 26, 13 },
	{  12000000, 100000000, 200,  1, 24, 13 },
	{         0,         0,   0,  0,  0,  0 },
};

static const struct pdiv_map plle_p[] = {
	{ .pdiv =  1, .hw_val =  0 },
	{ .pdiv =  2, .hw_val =  1 },
	{ .pdiv =  3, .hw_val =  2 },
	{ .pdiv =  4, .hw_val =  3 },
	{ .pdiv =  5, .hw_val =  4 },
	{ .pdiv =  6, .hw_val =  5 },
	{ .pdiv =  8, .hw_val =  6 },
	{ .pdiv = 10, .hw_val =  7 },
	{ .pdiv = 12, .hw_val =  8 },
	{ .pdiv = 16, .hw_val =  9 },
	{ .pdiv = 12, .hw_val = 10 },
	{ .pdiv = 16, .hw_val = 11 },
	{ .pdiv = 20, .hw_val = 12 },
	{ .pdiv = 24, .hw_val = 13 },
	{ .pdiv = 32, .hw_val = 14 },
	{ .pdiv =  1, .hw_val =  0 },
};

static struct div_nmp plle_nmp = {
	.divm_shift = 0,
	.divm_width = 8,
	.divn_shift = 8,
	.divn_width = 8,
	.divp_shift = 24,
	.divp_width = 4,
};

static struct tegra_clk_pll_params pll_e_params = {
	.input_min = 12000000,
	.input_max = 1000000000,
	.cf_min = 12000000,
	.cf_max = 75000000,
	.vco_min = 1600000000,
	.vco_max = 2400000000U,
	.base_reg = PLLE_BASE,
	.misc_reg = PLLE_MISC,
	.aux_reg = PLLE_AUX,
	.lock_mask = PLLE_MISC_LOCK,
	.lock_enable_bit_idx = PLLE_MISC_LOCK_ENABLE,
	.lock_delay = 300,
	.pdiv_tohw = plle_p,
	.div_nmp = &plle_nmp,
	.freq_table = pll_e_freq_table,
	.flags = TEGRA_PLL_FIXED | TEGRA_PLL_HAS_LOCK_ENABLE,
	.fixed_rate = 100000000,
};

static const struct clk_div_table pll_re_div_table[] = {
	{ .val = 0, .div = 1 },
	{ .val = 1, .div = 2 },
	{ .val = 2, .div = 3 },
	{ .val = 3, .div = 4 },
	{ .val = 4, .div = 5 },
	{ .val = 5, .div = 6 },
	{ .val = 0, .div = 0 },
};

static struct div_nmp pllre_nmp = {
	.divm_shift = 0,
	.divm_width = 8,
	.divn_shift = 8,
	.divn_width = 8,
	.divp_shift = 16,
	.divp_width = 4,
};

static struct tegra_clk_pll_params pll_re_vco_params = {
	.input_min = 12000000,
	.input_max = 1000000000,
	.cf_min = 12000000,
	.cf_max = 19200000, /* s/w policy, h/w capability 38 MHz */
	.vco_min = 300000000,
	.vco_max = 600000000,
	.base_reg = PLLRE_BASE,
	.misc_reg = PLLRE_MISC,
	.lock_mask = PLLRE_MISC_LOCK,
	.lock_enable_bit_idx = PLLRE_MISC_LOCK_ENABLE,
	.lock_delay = 300,
	.iddq_reg = PLLRE_MISC,
	.iddq_bit_idx = PLLRE_IDDQ_BIT,
	.div_nmp = &pllre_nmp,
	.flags = TEGRA_PLL_USE_LOCK | TEGRA_PLL_HAS_LOCK_ENABLE |
		 TEGRA_PLL_LOCK_MISC,
};

static struct div_nmp pllp_nmp = {
	.divm_shift = 0,
	.divm_width = 5,
	.divn_shift = 8,
	.divn_width = 10,
	.divp_shift = 20,
	.divp_width = 3,
};

static struct tegra_clk_pll_freq_table pll_p_freq_table[] = {
	{ 12000000, 408000000, 408, 12, 1, 8 },
	{ 13000000, 408000000, 408, 13, 1, 8 },
	{ 16800000, 408000000, 340, 14, 1, 8 },
	{ 19200000, 408000000, 340, 16, 1, 8 },
	{ 26000000, 408000000, 408, 26, 1, 8 },
	{        0,         0,   0,  0, 0, 0 },
};

static struct tegra_clk_pll_params pll_p_params = {
	.input_min = 2000000,
	.input_max = 31000000,
	.cf_min = 1000000,
	.cf_max = 6000000,
	.vco_min = 200000000,
	.vco_max = 700000000,
	.base_reg = PLLP_BASE,
	.misc_reg = PLLP_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLL_MISC_LOCK_ENABLE,
	.lock_delay = 300,
	.div_nmp = &pllp_nmp,
	.freq_table = pll_p_freq_table,
	.fixed_rate = 408000000,
	.flags = TEGRA_PLL_FIXED | TEGRA_PLL_USE_LOCK |
		 TEGRA_PLL_HAS_LOCK_ENABLE,
};

static struct tegra_clk_pll_freq_table pll_a_freq_table[] = {
	{  9600000, 282240000, 147,  5, 1, 4 },
	{  9600000, 368640000, 192,  5, 1, 4 },
	{  9600000, 240000000, 200,  8, 1, 8 },
	{ 28800000, 282240000, 245, 25, 1, 8 },
	{ 28800000, 368640000, 320, 25, 1, 8 },
	{ 28800000, 240000000, 200, 24, 1, 8 },
	{        0,         0,   0,  0, 0, 0 },
};

static struct tegra_clk_pll_params pll_a_params = {
	.input_min = 2000000,
	.input_max = 31000000,
	.cf_min = 1000000,
	.cf_max = 6000000,
	.vco_min = 200000000,
	.vco_max = 700000000,
	.base_reg = PLLA_BASE,
	.misc_reg = PLLA_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLL_MISC_LOCK_ENABLE,
	.lock_delay = 300,
	.div_nmp = &pllp_nmp,
	.freq_table = pll_a_freq_table,
	.flags = TEGRA_PLL_HAS_CPCON | TEGRA_PLL_USE_LOCK |
		 TEGRA_PLL_HAS_LOCK_ENABLE,
};

static struct div_nmp plld_nmp = {
	.divm_shift = 0,
	.divm_width = 5,
	.divn_shift = 8,
	.divn_width = 11,
	.divp_shift = 20,
	.divp_width = 3,
};

static struct tegra_clk_pll_freq_table pll_d_freq_table[] = {
	{ 12000000,  216000000,  864, 12, 4, 12 },
	{ 13000000,  216000000,  864, 13, 4, 12 },
	{ 16800000,  216000000,  720, 14, 4, 12 },
	{ 19200000,  216000000,  720, 16, 4, 12 },
	{ 26000000,  216000000,  864, 26, 4, 12 },
	{ 12000000,  594000000,  594, 12, 1, 12 },
	{ 13000000,  594000000,  594, 13, 1, 12 },
	{ 16800000,  594000000,  495, 14, 1, 12 },
	{ 19200000,  594000000,  495, 16, 1, 12 },
	{ 26000000,  594000000,  594, 26, 1, 12 },
	{ 12000000, 1000000000, 1000, 12, 1, 12 },
	{ 13000000, 1000000000, 1000, 13, 1, 12 },
	{ 19200000, 1000000000,  625, 12, 1, 12 },
	{ 26000000, 1000000000, 1000, 26, 1, 12 },
	{        0,          0,    0,  0, 0,  0 },
};

static struct tegra_clk_pll_params pll_d_params = {
	.input_min = 2000000,
	.input_max = 40000000,
	.cf_min = 1000000,
	.cf_max = 6000000,
	.vco_min = 500000000,
	.vco_max = 1000000000,
	.base_reg = PLLD_BASE,
	.misc_reg = PLLD_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLLDU_MISC_LOCK_ENABLE,
	.lock_delay = 1000,
	.div_nmp = &plld_nmp,
	.freq_table = pll_d_freq_table,
	.flags = TEGRA_PLL_HAS_CPCON | TEGRA_PLL_SET_LFCON |
		 TEGRA_PLL_USE_LOCK | TEGRA_PLL_HAS_LOCK_ENABLE,
};

static struct tegra_clk_pll_freq_table tegra124_pll_d2_freq_table[] = {
	{ 12000000, 594000000, 99, 1, 2, 0 },
	{ 13000000, 594000000, 91, 1, 2, 0 }, /* actual: 591.5 MHz */
	{ 16800000, 594000000, 71, 1, 2, 0 }, /* actual: 596.4 MHz */
	{ 19200000, 594000000, 62, 1, 2, 0 }, /* actual: 595.2 MHz */
	{ 26000000, 594000000, 91, 2, 2, 0 }, /* actual: 591.5 MHz */
	{        0,         0,  0, 0, 0, 0 },
};

static struct tegra_clk_pll_params tegra124_pll_d2_params = {
	.input_min = 12000000,
	.input_max = 1000000000,
	.cf_min = 12000000,
	.cf_max = 19200000, /* s/w policy, h/w capability 38 MHz */
	.vco_min = 600000000,
	.vco_max = 1200000000,
	.base_reg = PLLD2_BASE,
	.misc_reg = PLLD2_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLLSS_MISC_LOCK_ENABLE,
	.lock_delay = 300,
	.iddq_reg = PLLD2_BASE,
	.iddq_bit_idx = PLLSS_IDDQ_BIT,
	.pdiv_tohw = pll12g_ssd_esd_p,
	.div_nmp = &pllss_nmp,
	.ext_misc_reg[0] = 0x570,
	.ext_misc_reg[1] = 0x574,
	.ext_misc_reg[2] = 0x578,
	.max_p = 15,
	.freq_table = tegra124_pll_d2_freq_table,
	.flags = TEGRA_PLL_USE_LOCK | TEGRA_PLL_HAS_LOCK_ENABLE,
};

static struct tegra_clk_pll_freq_table pll_dp_freq_table[] = {
	{ 12000000, 600000000, 100, 1, 2, 0 },
	{ 13000000, 600000000,  92, 1, 2, 0 }, /* actual: 598.0 MHz */
	{ 16800000, 600000000,  71, 1, 2, 0 }, /* actual: 596.4 MHz */
	{ 19200000, 600000000,  62, 1, 2, 0 }, /* actual: 595.2 MHz */
	{ 26000000, 600000000,  92, 2, 2, 0 }, /* actual: 598.0 MHz */
	{        0,         0,   0, 0, 0, 0 },
};

static struct tegra_clk_pll_params pll_dp_params = {
	.input_min = 12000000,
	.input_max = 1000000000,
	.cf_min = 12000000,
	.cf_max = 19200000, /* s/w policy, h/w capability 38 MHz */
	.vco_min = 600000000,
	.vco_max = 1200000000,
	.base_reg = PLLDP_BASE,
	.misc_reg = PLLDP_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLLSS_MISC_LOCK_ENABLE,
	.lock_delay = 300,
	.iddq_reg = PLLDP_BASE,
	.iddq_bit_idx = PLLSS_IDDQ_BIT,
	.pdiv_tohw = pll12g_ssd_esd_p,
	.div_nmp = &pllss_nmp,
	.ext_misc_reg[0] = 0x598,
	.ext_misc_reg[1] = 0x59c,
	.ext_misc_reg[2] = 0x5a0,
	.max_p = 5,
	.freq_table = pll_dp_freq_table,
	.flags = TEGRA_PLL_USE_LOCK | TEGRA_PLL_HAS_LOCK_ENABLE,
};

static const struct pdiv_map pllu_p[] = {
	{ .pdiv = 1, .hw_val = 1 },
	{ .pdiv = 2, .hw_val = 0 },
	{ .pdiv = 0, .hw_val = 0 },
};

static struct div_nmp pllu_nmp = {
	.divm_shift = 0,
	.divm_width = 5,
	.divn_shift = 8,
	.divn_width = 10,
	.divp_shift = 20,
	.divp_width = 1,
};

static struct tegra_clk_pll_freq_table pll_u_freq_table[] = {
	{ 12000000, 480000000, 960, 12, 2, 12 },
	{ 13000000, 480000000, 960, 13, 2, 12 },
	{ 16800000, 480000000, 400,  7, 2,  5 },
	{ 19200000, 480000000, 200,  4, 2,  3 },
	{ 26000000, 480000000, 960, 26, 2, 12 },
	{        0,         0,   0,  0, 0,  0 },
};

static struct tegra_clk_pll_params pll_u_params = {
	.input_min = 2000000,
	.input_max = 40000000,
	.cf_min = 1000000,
	.cf_max = 6000000,
	.vco_min = 480000000,
	.vco_max = 960000000,
	.base_reg = PLLU_BASE,
	.misc_reg = PLLU_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLLDU_MISC_LOCK_ENABLE,
	.lock_delay = 1000,
	.pdiv_tohw = pllu_p,
	.div_nmp = &pllu_nmp,
	.freq_table = pll_u_freq_table,
	.flags = TEGRA_PLLU | TEGRA_PLL_HAS_CPCON | TEGRA_PLL_SET_LFCON |
		 TEGRA_PLL_USE_LOCK | TEGRA_PLL_HAS_LOCK_ENABLE,
};

static struct tegra_clk tegra124_clks[tegra_clk_max] __initdata = {
	[tegra_clk_ispb] = { .dt_id = TEGRA124_CLK_ISPB, .present = true },
	[tegra_clk_rtc] = { .dt_id = TEGRA124_CLK_RTC, .present = true },
	[tegra_clk_timer] = { .dt_id = TEGRA124_CLK_TIMER, .present = true },
	[tegra_clk_uarta] = { .dt_id = TEGRA124_CLK_UARTA, .present = true },
	[tegra_clk_sdmmc2_8] = { .dt_id = TEGRA124_CLK_SDMMC2, .present = true },
	[tegra_clk_i2s1] = { .dt_id = TEGRA124_CLK_I2S1, .present = true },
	[tegra_clk_i2c1] = { .dt_id = TEGRA124_CLK_I2C1, .present = true },
	[tegra_clk_sdmmc1_8] = { .dt_id = TEGRA124_CLK_SDMMC1, .present = true },
	[tegra_clk_sdmmc4_8] = { .dt_id = TEGRA124_CLK_SDMMC4, .present = true },
	[tegra_clk_pwm] = { .dt_id = TEGRA124_CLK_PWM, .present = true },
	[tegra_clk_i2s2] = { .dt_id = TEGRA124_CLK_I2S2, .present = true },
	[tegra_clk_usbd] = { .dt_id = TEGRA124_CLK_USBD, .present = true },
	[tegra_clk_isp_8] = { .dt_id = TEGRA124_CLK_ISP, .present = true },
	[tegra_clk_disp2] = { .dt_id = TEGRA124_CLK_DISP2, .present = true },
	[tegra_clk_disp1] = { .dt_id = TEGRA124_CLK_DISP1, .present = true },
	[tegra_clk_host1x_8] = { .dt_id = TEGRA124_CLK_HOST1X, .present = true },
	[tegra_clk_vcp] = { .dt_id = TEGRA124_CLK_VCP, .present = true },
	[tegra_clk_i2s0] = { .dt_id = TEGRA124_CLK_I2S0, .present = true },
	[tegra_clk_apbdma] = { .dt_id = TEGRA124_CLK_APBDMA, .present = true },
	[tegra_clk_kbc] = { .dt_id = TEGRA124_CLK_KBC, .present = true },
	[tegra_clk_kfuse] = { .dt_id = TEGRA124_CLK_KFUSE, .present = true },
	[tegra_clk_sbc1] = { .dt_id = TEGRA124_CLK_SBC1, .present = true },
	[tegra_clk_nor] = { .dt_id = TEGRA124_CLK_NOR, .present = true },
	[tegra_clk_sbc2] = { .dt_id = TEGRA124_CLK_SBC2, .present = true },
	[tegra_clk_sbc3] = { .dt_id = TEGRA124_CLK_SBC3, .present = true },
	[tegra_clk_i2c5] = { .dt_id = TEGRA124_CLK_I2C5, .present = true },
	[tegra_clk_mipi] = { .dt_id = TEGRA124_CLK_MIPI, .present = true },
	[tegra_clk_hdmi] = { .dt_id = TEGRA124_CLK_HDMI, .present = true },
	[tegra_clk_csi] = { .dt_id = TEGRA124_CLK_CSI, .present = true },
	[tegra_clk_i2c2] = { .dt_id = TEGRA124_CLK_I2C2, .present = true },
	[tegra_clk_uartc] = { .dt_id = TEGRA124_CLK_UARTC, .present = true },
	[tegra_clk_mipi_cal] = { .dt_id = TEGRA124_CLK_MIPI_CAL, .present = true },
	[tegra_clk_usb2] = { .dt_id = TEGRA124_CLK_USB2, .present = true },
	[tegra_clk_usb3] = { .dt_id = TEGRA124_CLK_USB3, .present = true },
	[tegra_clk_vde_8] = { .dt_id = TEGRA124_CLK_VDE, .present = true },
	[tegra_clk_bsea] = { .dt_id = TEGRA124_CLK_BSEA, .present = true },
	[tegra_clk_bsev] = { .dt_id = TEGRA124_CLK_BSEV, .present = true },
	[tegra_clk_uartd] = { .dt_id = TEGRA124_CLK_UARTD, .present = true },
	[tegra_clk_i2c3] = { .dt_id = TEGRA124_CLK_I2C3, .present = true },
	[tegra_clk_sbc4] = { .dt_id = TEGRA124_CLK_SBC4, .present = true },
	[tegra_clk_sdmmc3_8] = { .dt_id = TEGRA124_CLK_SDMMC3, .present = true },
	[tegra_clk_pcie] = { .dt_id = TEGRA124_CLK_PCIE, .present = true },
	[tegra_clk_owr] = { .dt_id = TEGRA124_CLK_OWR, .present = true },
	[tegra_clk_afi] = { .dt_id = TEGRA124_CLK_AFI, .present = true },
	[tegra_clk_csite] = { .dt_id = TEGRA124_CLK_CSITE, .present = true },
	[tegra_clk_la] = { .dt_id = TEGRA124_CLK_LA, .present = true },
	[tegra_clk_trace] = { .dt_id = TEGRA124_CLK_TRACE, .present = true },
	[tegra_clk_soc_therm] = { .dt_id = TEGRA124_CLK_SOC_THERM, .present = true },
	[tegra_clk_dtv] = { .dt_id = TEGRA124_CLK_DTV, .present = true },
	[tegra_clk_i2cslow] = { .dt_id = TEGRA124_CLK_I2CSLOW, .present = true },
	[tegra_clk_tsec] = { .dt_id = TEGRA124_CLK_TSEC, .present = true },
	[tegra_clk_xusb_host] = { .dt_id = TEGRA124_CLK_XUSB_HOST, .present = true },
	[tegra_clk_msenc] = { .dt_id = TEGRA124_CLK_MSENC, .present = true },
	[tegra_clk_csus] = { .dt_id = TEGRA124_CLK_CSUS, .present = true },
	[tegra_clk_mselect] = { .dt_id = TEGRA124_CLK_MSELECT, .present = true },
	[tegra_clk_tsensor] = { .dt_id = TEGRA124_CLK_TSENSOR, .present = true },
	[tegra_clk_i2s3] = { .dt_id = TEGRA124_CLK_I2S3, .present = true },
	[tegra_clk_i2s4] = { .dt_id = TEGRA124_CLK_I2S4, .present = true },
	[tegra_clk_i2c4] = { .dt_id = TEGRA124_CLK_I2C4, .present = true },
	[tegra_clk_sbc5] = { .dt_id = TEGRA124_CLK_SBC5, .present = true },
	[tegra_clk_sbc6] = { .dt_id = TEGRA124_CLK_SBC6, .present = true },
	[tegra_clk_d_audio] = { .dt_id = TEGRA124_CLK_D_AUDIO, .present = true },
	[tegra_clk_apbif] = { .dt_id = TEGRA124_CLK_APBIF, .present = true },
	[tegra_clk_dam0] = { .dt_id = TEGRA124_CLK_DAM0, .present = true },
	[tegra_clk_dam1] = { .dt_id = TEGRA124_CLK_DAM1, .present = true },
	[tegra_clk_dam2] = { .dt_id = TEGRA124_CLK_DAM2, .present = true },
	[tegra_clk_hda2codec_2x] = { .dt_id = TEGRA124_CLK_HDA2CODEC_2X, .present = true },
	[tegra_clk_audio0_2x] = { .dt_id = TEGRA124_CLK_AUDIO0_2X, .present = true },
	[tegra_clk_audio1_2x] = { .dt_id = TEGRA124_CLK_AUDIO1_2X, .present = true },
	[tegra_clk_audio2_2x] = { .dt_id = TEGRA124_CLK_AUDIO2_2X, .present = true },
	[tegra_clk_audio3_2x] = { .dt_id = TEGRA124_CLK_AUDIO3_2X, .present = true },
	[tegra_clk_audio4_2x] = { .dt_id = TEGRA124_CLK_AUDIO4_2X, .present = true },
	[tegra_clk_spdif_2x] = { .dt_id = TEGRA124_CLK_SPDIF_2X, .present = true },
	[tegra_clk_actmon] = { .dt_id = TEGRA124_CLK_ACTMON, .present = true },
	[tegra_clk_extern1] = { .dt_id = TEGRA124_CLK_EXTERN1, .present = true },
	[tegra_clk_extern2] = { .dt_id = TEGRA124_CLK_EXTERN2, .present = true },
	[tegra_clk_extern3] = { .dt_id = TEGRA124_CLK_EXTERN3, .present = true },
	[tegra_clk_sata_oob] = { .dt_id = TEGRA124_CLK_SATA_OOB, .present = true },
	[tegra_clk_sata] = { .dt_id = TEGRA124_CLK_SATA, .present = true },
	[tegra_clk_hda] = { .dt_id = TEGRA124_CLK_HDA, .present = true },
	[tegra_clk_se] = { .dt_id = TEGRA124_CLK_SE, .present = true },
	[tegra_clk_hda2hdmi] = { .dt_id = TEGRA124_CLK_HDA2HDMI, .present = true },
	[tegra_clk_sata_cold] = { .dt_id = TEGRA124_CLK_SATA_COLD, .present = true },
	[tegra_clk_cilab] = { .dt_id = TEGRA124_CLK_CILAB, .present = true },
	[tegra_clk_cilcd] = { .dt_id = TEGRA124_CLK_CILCD, .present = true },
	[tegra_clk_cile] = { .dt_id = TEGRA124_CLK_CILE, .present = true },
	[tegra_clk_dsialp] = { .dt_id = TEGRA124_CLK_DSIALP, .present = true },
	[tegra_clk_dsiblp] = { .dt_id = TEGRA124_CLK_DSIBLP, .present = true },
	[tegra_clk_entropy] = { .dt_id = TEGRA124_CLK_ENTROPY, .present = true },
	[tegra_clk_dds] = { .dt_id = TEGRA124_CLK_DDS, .present = true },
	[tegra_clk_dp2] = { .dt_id = TEGRA124_CLK_DP2, .present = true },
	[tegra_clk_amx] = { .dt_id = TEGRA124_CLK_AMX, .present = true },
	[tegra_clk_adx] = { .dt_id = TEGRA124_CLK_ADX, .present = true },
	[tegra_clk_xusb_ss] = { .dt_id = TEGRA124_CLK_XUSB_SS, .present = true },
	[tegra_clk_i2c6] = { .dt_id = TEGRA124_CLK_I2C6, .present = true },
	[tegra_clk_vim2_clk] = { .dt_id = TEGRA124_CLK_VIM2_CLK, .present = true },
	[tegra_clk_hdmi_audio] = { .dt_id = TEGRA124_CLK_HDMI_AUDIO, .present = true },
	[tegra_clk_clk72Mhz] = { .dt_id = TEGRA124_CLK_CLK72MHZ, .present = true },
	[tegra_clk_vic03] = { .dt_id = TEGRA124_CLK_VIC03, .present = true },
	[tegra_clk_adx1] = { .dt_id = TEGRA124_CLK_ADX1, .present = true },
	[tegra_clk_dpaux] = { .dt_id = TEGRA124_CLK_DPAUX, .present = true },
	[tegra_clk_sor0] = { .dt_id = TEGRA124_CLK_SOR0, .present = true },
	[tegra_clk_sor0_lvds] = { .dt_id = TEGRA124_CLK_SOR0_LVDS, .present = true },
	[tegra_clk_gpu] = { .dt_id = TEGRA124_CLK_GPU, .present = true },
	[tegra_clk_amx1] = { .dt_id = TEGRA124_CLK_AMX1, .present = true },
	[tegra_clk_uartb] = { .dt_id = TEGRA124_CLK_UARTB, .present = true },
	[tegra_clk_vfir] = { .dt_id = TEGRA124_CLK_VFIR, .present = true },
	[tegra_clk_spdif_in] = { .dt_id = TEGRA124_CLK_SPDIF_IN, .present = true },
	[tegra_clk_spdif_out] = { .dt_id = TEGRA124_CLK_SPDIF_OUT, .present = true },
	[tegra_clk_vi_9] = { .dt_id = TEGRA124_CLK_VI, .present = true },
	[tegra_clk_vi_sensor_8] = { .dt_id = TEGRA124_CLK_VI_SENSOR, .present = true },
	[tegra_clk_fuse] = { .dt_id = TEGRA124_CLK_FUSE, .present = true },
	[tegra_clk_fuse_burn] = { .dt_id = TEGRA124_CLK_FUSE_BURN, .present = true },
	[tegra_clk_clk_32k] = { .dt_id = TEGRA124_CLK_CLK_32K, .present = true },
	[tegra_clk_clk_m] = { .dt_id = TEGRA124_CLK_CLK_M, .present = true },
	[tegra_clk_clk_m_div2] = { .dt_id = TEGRA124_CLK_CLK_M_DIV2, .present = true },
	[tegra_clk_clk_m_div4] = { .dt_id = TEGRA124_CLK_CLK_M_DIV4, .present = true },
	[tegra_clk_pll_ref] = { .dt_id = TEGRA124_CLK_PLL_REF, .present = true },
	[tegra_clk_pll_c] = { .dt_id = TEGRA124_CLK_PLL_C, .present = true },
	[tegra_clk_pll_c_out1] = { .dt_id = TEGRA124_CLK_PLL_C_OUT1, .present = true },
	[tegra_clk_pll_c2] = { .dt_id = TEGRA124_CLK_PLL_C2, .present = true },
	[tegra_clk_pll_c3] = { .dt_id = TEGRA124_CLK_PLL_C3, .present = true },
	[tegra_clk_pll_m] = { .dt_id = TEGRA124_CLK_PLL_M, .present = true },
	[tegra_clk_pll_m_out1] = { .dt_id = TEGRA124_CLK_PLL_M_OUT1, .present = true },
	[tegra_clk_pll_p] = { .dt_id = TEGRA124_CLK_PLL_P, .present = true },
	[tegra_clk_pll_p_out1] = { .dt_id = TEGRA124_CLK_PLL_P_OUT1, .present = true },
	[tegra_clk_pll_p_out2] = { .dt_id = TEGRA124_CLK_PLL_P_OUT2, .present = true },
	[tegra_clk_pll_p_out3] = { .dt_id = TEGRA124_CLK_PLL_P_OUT3, .present = true },
	[tegra_clk_pll_p_out4] = { .dt_id = TEGRA124_CLK_PLL_P_OUT4, .present = true },
	[tegra_clk_pll_a] = { .dt_id = TEGRA124_CLK_PLL_A, .present = true },
	[tegra_clk_pll_a_out0] = { .dt_id = TEGRA124_CLK_PLL_A_OUT0, .present = true },
	[tegra_clk_pll_d] = { .dt_id = TEGRA124_CLK_PLL_D, .present = true },
	[tegra_clk_pll_d_out0] = { .dt_id = TEGRA124_CLK_PLL_D_OUT0, .present = true },
	[tegra_clk_pll_d2] = { .dt_id = TEGRA124_CLK_PLL_D2, .present = true },
	[tegra_clk_pll_d2_out0] = { .dt_id = TEGRA124_CLK_PLL_D2_OUT0, .present = true },
	[tegra_clk_pll_u] = { .dt_id = TEGRA124_CLK_PLL_U, .present = true },
	[tegra_clk_pll_u_480m] = { .dt_id = TEGRA124_CLK_PLL_U_480M, .present = true },
	[tegra_clk_pll_u_60m] = { .dt_id = TEGRA124_CLK_PLL_U_60M, .present = true },
	[tegra_clk_pll_u_48m] = { .dt_id = TEGRA124_CLK_PLL_U_48M, .present = true },
	[tegra_clk_pll_u_12m] = { .dt_id = TEGRA124_CLK_PLL_U_12M, .present = true },
	[tegra_clk_pll_x] = { .dt_id = TEGRA124_CLK_PLL_X, .present = true },
	[tegra_clk_pll_x_out0] = { .dt_id = TEGRA124_CLK_PLL_X_OUT0, .present = true },
	[tegra_clk_pll_re_vco] = { .dt_id = TEGRA124_CLK_PLL_RE_VCO, .present = true },
	[tegra_clk_pll_re_out] = { .dt_id = TEGRA124_CLK_PLL_RE_OUT, .present = true },
	[tegra_clk_spdif_in_sync] = { .dt_id = TEGRA124_CLK_SPDIF_IN_SYNC, .present = true },
	[tegra_clk_i2s0_sync] = { .dt_id = TEGRA124_CLK_I2S0_SYNC, .present = true },
	[tegra_clk_i2s1_sync] = { .dt_id = TEGRA124_CLK_I2S1_SYNC, .present = true },
	[tegra_clk_i2s2_sync] = { .dt_id = TEGRA124_CLK_I2S2_SYNC, .present = true },
	[tegra_clk_i2s3_sync] = { .dt_id = TEGRA124_CLK_I2S3_SYNC, .present = true },
	[tegra_clk_i2s4_sync] = { .dt_id = TEGRA124_CLK_I2S4_SYNC, .present = true },
	[tegra_clk_vimclk_sync] = { .dt_id = TEGRA124_CLK_VIMCLK_SYNC, .present = true },
	[tegra_clk_audio0] = { .dt_id = TEGRA124_CLK_AUDIO0, .present = true },
	[tegra_clk_audio1] = { .dt_id = TEGRA124_CLK_AUDIO1, .present = true },
	[tegra_clk_audio2] = { .dt_id = TEGRA124_CLK_AUDIO2, .present = true },
	[tegra_clk_audio3] = { .dt_id = TEGRA124_CLK_AUDIO3, .present = true },
	[tegra_clk_audio4] = { .dt_id = TEGRA124_CLK_AUDIO4, .present = true },
	[tegra_clk_spdif] = { .dt_id = TEGRA124_CLK_SPDIF, .present = true },
	[tegra_clk_clk_out_1] = { .dt_id = TEGRA124_CLK_CLK_OUT_1, .present = true },
	[tegra_clk_clk_out_2] = { .dt_id = TEGRA124_CLK_CLK_OUT_2, .present = true },
	[tegra_clk_clk_out_3] = { .dt_id = TEGRA124_CLK_CLK_OUT_3, .present = true },
	[tegra_clk_blink] = { .dt_id = TEGRA124_CLK_BLINK, .present = true },
	[tegra_clk_xusb_host_src] = { .dt_id = TEGRA124_CLK_XUSB_HOST_SRC, .present = true },
	[tegra_clk_xusb_falcon_src] = { .dt_id = TEGRA124_CLK_XUSB_FALCON_SRC, .present = true },
	[tegra_clk_xusb_fs_src] = { .dt_id = TEGRA124_CLK_XUSB_FS_SRC, .present = true },
	[tegra_clk_xusb_ss_src] = { .dt_id = TEGRA124_CLK_XUSB_SS_SRC, .present = true },
	[tegra_clk_xusb_ss_div2] = { .dt_id = TEGRA124_CLK_XUSB_SS_DIV2, .present = true },
	[tegra_clk_xusb_dev_src] = { .dt_id = TEGRA124_CLK_XUSB_DEV_SRC, .present = true },
	[tegra_clk_xusb_dev] = { .dt_id = TEGRA124_CLK_XUSB_DEV, .present = true },
	[tegra_clk_xusb_hs_src] = { .dt_id = TEGRA124_CLK_XUSB_HS_SRC, .present = true },
	[tegra_clk_sclk] = { .dt_id = TEGRA124_CLK_SCLK, .present = true },
	[tegra_clk_hclk] = { .dt_id = TEGRA124_CLK_HCLK, .present = true },
	[tegra_clk_pclk] = { .dt_id = TEGRA124_CLK_PCLK, .present = true },
	[tegra_clk_cclk_g] = { .dt_id = TEGRA124_CLK_CCLK_G, .present = true },
	[tegra_clk_cclk_lp] = { .dt_id = TEGRA124_CLK_CCLK_LP, .present = true },
	[tegra_clk_dfll_ref] = { .dt_id = TEGRA124_CLK_DFLL_REF, .present = true },
	[tegra_clk_dfll_soc] = { .dt_id = TEGRA124_CLK_DFLL_SOC, .present = true },
	[tegra_clk_vi_sensor2] = { .dt_id = TEGRA124_CLK_VI_SENSOR2, .present = true },
	[tegra_clk_pll_p_out5] = { .dt_id = TEGRA124_CLK_PLL_P_OUT5, .present = true },
	[tegra_clk_pll_c4] = { .dt_id = TEGRA124_CLK_PLL_C4, .present = true },
	[tegra_clk_pll_dp] = { .dt_id = TEGRA124_CLK_PLL_DP, .present = true },
	[tegra_clk_audio0_mux] = { .dt_id = TEGRA124_CLK_AUDIO0_MUX, .present = true },
	[tegra_clk_audio1_mux] = { .dt_id = TEGRA124_CLK_AUDIO1_MUX, .present = true },
	[tegra_clk_audio2_mux] = { .dt_id = TEGRA124_CLK_AUDIO2_MUX, .present = true },
	[tegra_clk_audio3_mux] = { .dt_id = TEGRA124_CLK_AUDIO3_MUX, .present = true },
	[tegra_clk_audio4_mux] = { .dt_id = TEGRA124_CLK_AUDIO4_MUX, .present = true },
	[tegra_clk_spdif_mux] = { .dt_id = TEGRA124_CLK_SPDIF_MUX, .present = true },
	[tegra_clk_clk_out_1_mux] = { .dt_id = TEGRA124_CLK_CLK_OUT_1_MUX, .present = true },
	[tegra_clk_clk_out_2_mux] = { .dt_id = TEGRA124_CLK_CLK_OUT_2_MUX, .present = true },
	[tegra_clk_clk_out_3_mux] = { .dt_id = TEGRA124_CLK_CLK_OUT_3_MUX, .present = true },
	[tegra_clk_cec] = { .dt_id = TEGRA124_CLK_CEC, .present = true },
};

static struct tegra_devclk devclks[] __initdata = {
	{ .con_id = "clk_m", .dt_id = TEGRA124_CLK_CLK_M },
	{ .con_id = "pll_ref", .dt_id = TEGRA124_CLK_PLL_REF },
	{ .con_id = "clk_32k", .dt_id = TEGRA124_CLK_CLK_32K },
	{ .con_id = "clk_m_div2", .dt_id = TEGRA124_CLK_CLK_M_DIV2 },
	{ .con_id = "clk_m_div4", .dt_id = TEGRA124_CLK_CLK_M_DIV4 },
	{ .con_id = "pll_c", .dt_id = TEGRA124_CLK_PLL_C },
	{ .con_id = "pll_c_out1", .dt_id = TEGRA124_CLK_PLL_C_OUT1 },
	{ .con_id = "pll_c2", .dt_id = TEGRA124_CLK_PLL_C2 },
	{ .con_id = "pll_c3", .dt_id = TEGRA124_CLK_PLL_C3 },
	{ .con_id = "pll_p", .dt_id = TEGRA124_CLK_PLL_P },
	{ .con_id = "pll_p_out1", .dt_id = TEGRA124_CLK_PLL_P_OUT1 },
	{ .con_id = "pll_p_out2", .dt_id = TEGRA124_CLK_PLL_P_OUT2 },
	{ .con_id = "pll_p_out3", .dt_id = TEGRA124_CLK_PLL_P_OUT3 },
	{ .con_id = "pll_p_out4", .dt_id = TEGRA124_CLK_PLL_P_OUT4 },
	{ .con_id = "pll_m", .dt_id = TEGRA124_CLK_PLL_M },
	{ .con_id = "pll_m_out1", .dt_id = TEGRA124_CLK_PLL_M_OUT1 },
	{ .con_id = "pll_x", .dt_id = TEGRA124_CLK_PLL_X },
	{ .con_id = "pll_x_out0", .dt_id = TEGRA124_CLK_PLL_X_OUT0 },
	{ .con_id = "pll_u", .dt_id = TEGRA124_CLK_PLL_U },
	{ .con_id = "pll_u_480M", .dt_id = TEGRA124_CLK_PLL_U_480M },
	{ .con_id = "pll_u_60M", .dt_id = TEGRA124_CLK_PLL_U_60M },
	{ .con_id = "pll_u_48M", .dt_id = TEGRA124_CLK_PLL_U_48M },
	{ .con_id = "pll_u_12M", .dt_id = TEGRA124_CLK_PLL_U_12M },
	{ .con_id = "pll_d", .dt_id = TEGRA124_CLK_PLL_D },
	{ .con_id = "pll_d_out0", .dt_id = TEGRA124_CLK_PLL_D_OUT0 },
	{ .con_id = "pll_d2", .dt_id = TEGRA124_CLK_PLL_D2 },
	{ .con_id = "pll_d2_out0", .dt_id = TEGRA124_CLK_PLL_D2_OUT0 },
	{ .con_id = "pll_a", .dt_id = TEGRA124_CLK_PLL_A },
	{ .con_id = "pll_a_out0", .dt_id = TEGRA124_CLK_PLL_A_OUT0 },
	{ .con_id = "pll_re_vco", .dt_id = TEGRA124_CLK_PLL_RE_VCO },
	{ .con_id = "pll_re_out", .dt_id = TEGRA124_CLK_PLL_RE_OUT },
	{ .con_id = "spdif_in_sync", .dt_id = TEGRA124_CLK_SPDIF_IN_SYNC },
	{ .con_id = "i2s0_sync", .dt_id = TEGRA124_CLK_I2S0_SYNC },
	{ .con_id = "i2s1_sync", .dt_id = TEGRA124_CLK_I2S1_SYNC },
	{ .con_id = "i2s2_sync", .dt_id = TEGRA124_CLK_I2S2_SYNC },
	{ .con_id = "i2s3_sync", .dt_id = TEGRA124_CLK_I2S3_SYNC },
	{ .con_id = "i2s4_sync", .dt_id = TEGRA124_CLK_I2S4_SYNC },
	{ .con_id = "vimclk_sync", .dt_id = TEGRA124_CLK_VIMCLK_SYNC },
	{ .con_id = "audio0", .dt_id = TEGRA124_CLK_AUDIO0 },
	{ .con_id = "audio1", .dt_id = TEGRA124_CLK_AUDIO1 },
	{ .con_id = "audio2", .dt_id = TEGRA124_CLK_AUDIO2 },
	{ .con_id = "audio3", .dt_id = TEGRA124_CLK_AUDIO3 },
	{ .con_id = "audio4", .dt_id = TEGRA124_CLK_AUDIO4 },
	{ .con_id = "spdif", .dt_id = TEGRA124_CLK_SPDIF },
	{ .con_id = "audio0_2x", .dt_id = TEGRA124_CLK_AUDIO0_2X },
	{ .con_id = "audio1_2x", .dt_id = TEGRA124_CLK_AUDIO1_2X },
	{ .con_id = "audio2_2x", .dt_id = TEGRA124_CLK_AUDIO2_2X },
	{ .con_id = "audio3_2x", .dt_id = TEGRA124_CLK_AUDIO3_2X },
	{ .con_id = "audio4_2x", .dt_id = TEGRA124_CLK_AUDIO4_2X },
	{ .con_id = "spdif_2x", .dt_id = TEGRA124_CLK_SPDIF_2X },
	{ .con_id = "extern1", .dev_id = "clk_out_1", .dt_id = TEGRA124_CLK_EXTERN1 },
	{ .con_id = "extern2", .dev_id = "clk_out_2", .dt_id = TEGRA124_CLK_EXTERN2 },
	{ .con_id = "extern3", .dev_id = "clk_out_3", .dt_id = TEGRA124_CLK_EXTERN3 },
	{ .con_id = "blink", .dt_id = TEGRA124_CLK_BLINK },
	{ .con_id = "cclk_g", .dt_id = TEGRA124_CLK_CCLK_G },
	{ .con_id = "cclk_lp", .dt_id = TEGRA124_CLK_CCLK_LP },
	{ .con_id = "sclk", .dt_id = TEGRA124_CLK_SCLK },
	{ .con_id = "hclk", .dt_id = TEGRA124_CLK_HCLK },
	{ .con_id = "pclk", .dt_id = TEGRA124_CLK_PCLK },
	{ .con_id = "fuse", .dt_id = TEGRA124_CLK_FUSE },
	{ .dev_id = "rtc-tegra", .dt_id = TEGRA124_CLK_RTC },
	{ .dev_id = "timer", .dt_id = TEGRA124_CLK_TIMER },
	{ .con_id = "hda", .dt_id = TEGRA124_CLK_HDA },
	{ .con_id = "hda2codec_2x", .dt_id = TEGRA124_CLK_HDA2CODEC_2X },
	{ .con_id = "hda2hdmi", .dt_id = TEGRA124_CLK_HDA2HDMI },
};

static struct clk **clks;

static __init void tegra124_periph_clk_init(void __iomem *clk_base,
					    void __iomem *pmc_base)
{
	struct clk *clk;

	/* xusb_ss_div2 */
	clk = clk_register_fixed_factor(NULL, "xusb_ss_div2", "xusb_ss_src", 0,
					1, 2);
	clks[TEGRA124_CLK_XUSB_SS_DIV2] = clk;

	clk = tegra_clk_register_periph_fixed("dpaux", "pll_p", 0, clk_base,
					      1, 17, 181);
	clks[TEGRA124_CLK_DPAUX] = clk;

	clk = clk_register_gate(NULL, "pll_d_dsi_out", "pll_d_out0", 0,
				clk_base + PLLD_MISC, 30, 0, &pll_d_lock);
	clks[TEGRA124_CLK_PLL_D_DSI_OUT] = clk;

	clk = tegra_clk_register_periph_gate("dsia", "pll_d_dsi_out", 0,
					     clk_base, 0, 48,
					     periph_clk_enb_refcnt);
	clks[TEGRA124_CLK_DSIA] = clk;

	clk = tegra_clk_register_periph_gate("dsib", "pll_d_dsi_out", 0,
					     clk_base, 0, 82,
					     periph_clk_enb_refcnt);
	clks[TEGRA124_CLK_DSIB] = clk;

	clk = tegra_clk_register_mc("mc", "emc", clk_base + CLK_SOURCE_EMC,
				    &emc_lock);
	clks[TEGRA124_CLK_MC] = clk;

	/* cml0 */
	clk = clk_register_gate(NULL, "cml0", "pll_e", 0, clk_base + PLLE_AUX,
				0, 0, &pll_e_lock);
	clk_register_clkdev(clk, "cml0", NULL);
	clks[TEGRA124_CLK_CML0] = clk;

	/* cml1 */
	clk = clk_register_gate(NULL, "cml1", "pll_e", 0, clk_base + PLLE_AUX,
				1, 0, &pll_e_lock);
	clk_register_clkdev(clk, "cml1", NULL);
	clks[TEGRA124_CLK_CML1] = clk;

	tegra_periph_clk_init(clk_base, pmc_base, tegra124_clks, &pll_p_params);
}

static void __init tegra124_pll_init(void __iomem *clk_base,
				     void __iomem *pmc)
{
	struct clk *clk;

	/* PLLC */
	clk = tegra_clk_register_pllxc("pll_c", "pll_ref", clk_base,
			pmc, 0, &pll_c_params, NULL);
	clk_register_clkdev(clk, "pll_c", NULL);
	clks[TEGRA124_CLK_PLL_C] = clk;

	/* PLLC_OUT1 */
	clk = tegra_clk_register_divider("pll_c_out1_div", "pll_c",
			clk_base + PLLC_OUT, 0, TEGRA_DIVIDER_ROUND_UP,
			8, 8, 1, NULL);
	clk = tegra_clk_register_pll_out("pll_c_out1", "pll_c_out1_div",
				clk_base + PLLC_OUT, 1, 0,
				CLK_SET_RATE_PARENT, 0, NULL);
	clk_register_clkdev(clk, "pll_c_out1", NULL);
	clks[TEGRA124_CLK_PLL_C_OUT1] = clk;

	/* PLLC_UD */
	clk = clk_register_fixed_factor(NULL, "pll_c_ud", "pll_c",
					CLK_SET_RATE_PARENT, 1, 1);
	clk_register_clkdev(clk, "pll_c_ud", NULL);
	clks[TEGRA124_CLK_PLL_C_UD] = clk;

	/* PLLC2 */
	clk = tegra_clk_register_pllc("pll_c2", "pll_ref", clk_base, pmc, 0,
			     &pll_c2_params, NULL);
	clk_register_clkdev(clk, "pll_c2", NULL);
	clks[TEGRA124_CLK_PLL_C2] = clk;

	/* PLLC3 */
	clk = tegra_clk_register_pllc("pll_c3", "pll_ref", clk_base, pmc, 0,
			     &pll_c3_params, NULL);
	clk_register_clkdev(clk, "pll_c3", NULL);
	clks[TEGRA124_CLK_PLL_C3] = clk;

	/* PLLM */
	clk = tegra_clk_register_pllm("pll_m", "pll_ref", clk_base, pmc,
			     CLK_SET_RATE_GATE, &pll_m_params, NULL);
	clk_register_clkdev(clk, "pll_m", NULL);
	clks[TEGRA124_CLK_PLL_M] = clk;

	/* PLLM_OUT1 */
	clk = tegra_clk_register_divider("pll_m_out1_div", "pll_m",
				clk_base + PLLM_OUT, 0, TEGRA_DIVIDER_ROUND_UP,
				8, 8, 1, NULL);
	clk = tegra_clk_register_pll_out("pll_m_out1", "pll_m_out1_div",
				clk_base + PLLM_OUT, 1, 0,
				CLK_SET_RATE_PARENT, 0, NULL);
	clk_register_clkdev(clk, "pll_m_out1", NULL);
	clks[TEGRA124_CLK_PLL_M_OUT1] = clk;

	/* PLLM_UD */
	clk = clk_register_fixed_factor(NULL, "pll_m_ud", "pll_m",
					CLK_SET_RATE_PARENT, 1, 1);
	clk_register_clkdev(clk, "pll_m_ud", NULL);
	clks[TEGRA124_CLK_PLL_M_UD] = clk;

	/* PLLU */
	clk = tegra_clk_register_pllu_tegra114("pll_u", "pll_ref", clk_base, 0,
					       &pll_u_params, &pll_u_lock);
	clk_register_clkdev(clk, "pll_u", NULL);
	clks[TEGRA124_CLK_PLL_U] = clk;

	/* PLLU_480M */
	clk = clk_register_gate(NULL, "pll_u_480M", "pll_u",
				CLK_SET_RATE_PARENT, clk_base + PLLU_BASE,
				22, 0, &pll_u_lock);
	clk_register_clkdev(clk, "pll_u_480M", NULL);
	clks[TEGRA124_CLK_PLL_U_480M] = clk;

	/* PLLU_60M */
	clk = clk_register_fixed_factor(NULL, "pll_u_60M", "pll_u",
					CLK_SET_RATE_PARENT, 1, 8);
	clk_register_clkdev(clk, "pll_u_60M", NULL);
	clks[TEGRA124_CLK_PLL_U_60M] = clk;

	/* PLLU_48M */
	clk = clk_register_fixed_factor(NULL, "pll_u_48M", "pll_u",
					CLK_SET_RATE_PARENT, 1, 10);
	clk_register_clkdev(clk, "pll_u_48M", NULL);
	clks[TEGRA124_CLK_PLL_U_48M] = clk;

	/* PLLU_12M */
	clk = clk_register_fixed_factor(NULL, "pll_u_12M", "pll_u",
					CLK_SET_RATE_PARENT, 1, 40);
	clk_register_clkdev(clk, "pll_u_12M", NULL);
	clks[TEGRA124_CLK_PLL_U_12M] = clk;

	/* PLLD */
	clk = tegra_clk_register_pll("pll_d", "pll_ref", clk_base, pmc, 0,
			    &pll_d_params, &pll_d_lock);
	clk_register_clkdev(clk, "pll_d", NULL);
	clks[TEGRA124_CLK_PLL_D] = clk;

	/* PLLD_OUT0 */
	clk = clk_register_fixed_factor(NULL, "pll_d_out0", "pll_d",
					CLK_SET_RATE_PARENT, 1, 2);
	clk_register_clkdev(clk, "pll_d_out0", NULL);
	clks[TEGRA124_CLK_PLL_D_OUT0] = clk;

	/* PLLRE */
	clk = tegra_clk_register_pllre("pll_re_vco", "pll_ref", clk_base, pmc,
			     0, &pll_re_vco_params, &pll_re_lock, pll_ref_freq);
	clk_register_clkdev(clk, "pll_re_vco", NULL);
	clks[TEGRA124_CLK_PLL_RE_VCO] = clk;

	clk = clk_register_divider_table(NULL, "pll_re_out", "pll_re_vco", 0,
					 clk_base + PLLRE_BASE, 16, 4, 0,
					 pll_re_div_table, &pll_re_lock);
	clk_register_clkdev(clk, "pll_re_out", NULL);
	clks[TEGRA124_CLK_PLL_RE_OUT] = clk;

	/* PLLE */
	clk = tegra_clk_register_plle_tegra114("pll_e", "pll_ref",
				      clk_base, 0, &pll_e_params, NULL);
	clk_register_clkdev(clk, "pll_e", NULL);
	clks[TEGRA124_CLK_PLL_E] = clk;

	/* PLLC4 */
	clk = tegra_clk_register_pllss("pll_c4", "pll_ref", clk_base, 0,
					&pll_c4_params, NULL);
	clk_register_clkdev(clk, "pll_c4", NULL);
	clks[TEGRA124_CLK_PLL_C4] = clk;

	/* PLLDP */
	clk = tegra_clk_register_pllss("pll_dp", "pll_ref", clk_base, 0,
					&pll_dp_params, NULL);
	clk_register_clkdev(clk, "pll_dp", NULL);
	clks[TEGRA124_CLK_PLL_DP] = clk;

	/* PLLD2 */
	clk = tegra_clk_register_pllss("pll_d2", "pll_ref", clk_base, 0,
					&tegra124_pll_d2_params, NULL);
	clk_register_clkdev(clk, "pll_d2", NULL);
	clks[TEGRA124_CLK_PLL_D2] = clk;

	/* PLLD2_OUT0 */
	clk = clk_register_fixed_factor(NULL, "pll_d2_out0", "pll_d2",
					CLK_SET_RATE_PARENT, 1, 1);
	clk_register_clkdev(clk, "pll_d2_out0", NULL);
	clks[TEGRA124_CLK_PLL_D2_OUT0] = clk;

}

/* Tegra124 CPU clock and reset control functions */
static void tegra124_wait_cpu_in_reset(u32 cpu)
{
	unsigned int reg;

	do {
		reg = readl(clk_base + CLK_RST_CONTROLLER_CPU_CMPLX_STATUS);
		cpu_relax();
	} while (!(reg & (1 << cpu)));  /* check CPU been reset or not */
}

static void tegra124_disable_cpu_clock(u32 cpu)
{
	/* flow controller would take care in the power sequence. */
}

#ifdef CONFIG_PM_SLEEP
static void tegra124_cpu_clock_suspend(void)
{
	/* switch coresite to clk_m, save off original source */
	tegra124_cpu_clk_sctx.clk_csite_src =
				readl(clk_base + CLK_SOURCE_CSITE);
	writel(3 << 30, clk_base + CLK_SOURCE_CSITE);

	tegra124_cpu_clk_sctx.cclkg_burst =
				readl(clk_base + CCLKG_BURST_POLICY);
	tegra124_cpu_clk_sctx.cclkg_divider =
				readl(clk_base + CCLKG_BURST_POLICY + 4);
}

static void tegra124_cpu_clock_resume(void)
{
	writel(tegra124_cpu_clk_sctx.clk_csite_src,
				clk_base + CLK_SOURCE_CSITE);

	writel(tegra124_cpu_clk_sctx.cclkg_burst,
					clk_base + CCLKG_BURST_POLICY);
	writel(tegra124_cpu_clk_sctx.cclkg_divider,
					clk_base + CCLKG_BURST_POLICY + 4);
}
#endif

static struct tegra_cpu_car_ops tegra124_cpu_car_ops = {
	.wait_for_reset	= tegra124_wait_cpu_in_reset,
	.disable_clock	= tegra124_disable_cpu_clock,
#ifdef CONFIG_PM_SLEEP
	.suspend	= tegra124_cpu_clock_suspend,
	.resume		= tegra124_cpu_clock_resume,
#endif
};

static const struct of_device_id pmc_match[] __initconst = {
	{ .compatible = "nvidia,tegra124-pmc" },
	{ },
};

static struct tegra_clk_init_table common_init_table[] __initdata = {
	{ TEGRA124_CLK_UARTA, TEGRA124_CLK_PLL_P, 408000000, 0 },
	{ TEGRA124_CLK_UARTB, TEGRA124_CLK_PLL_P, 408000000, 0 },
	{ TEGRA124_CLK_UARTC, TEGRA124_CLK_PLL_P, 408000000, 0 },
	{ TEGRA124_CLK_UARTD, TEGRA124_CLK_PLL_P, 408000000, 0 },
	{ TEGRA124_CLK_PLL_A, TEGRA124_CLK_CLK_MAX, 564480000, 1 },
	{ TEGRA124_CLK_PLL_A_OUT0, TEGRA124_CLK_CLK_MAX, 11289600, 1 },
	{ TEGRA124_CLK_EXTERN1, TEGRA124_CLK_PLL_A_OUT0, 0, 1 },
	{ TEGRA124_CLK_CLK_OUT_1_MUX, TEGRA124_CLK_EXTERN1, 0, 1 },
	{ TEGRA124_CLK_CLK_OUT_1, TEGRA124_CLK_CLK_MAX, 0, 1 },
	{ TEGRA124_CLK_I2S0, TEGRA124_CLK_PLL_A_OUT0, 11289600, 0 },
	{ TEGRA124_CLK_I2S1, TEGRA124_CLK_PLL_A_OUT0, 11289600, 0 },
	{ TEGRA124_CLK_I2S2, TEGRA124_CLK_PLL_A_OUT0, 11289600, 0 },
	{ TEGRA124_CLK_I2S3, TEGRA124_CLK_PLL_A_OUT0, 11289600, 0 },
	{ TEGRA124_CLK_I2S4, TEGRA124_CLK_PLL_A_OUT0, 11289600, 0 },
	{ TEGRA124_CLK_VDE, TEGRA124_CLK_CLK_MAX, 600000000, 0 },
	{ TEGRA124_CLK_HOST1X, TEGRA124_CLK_PLL_P, 136000000, 1 },
	{ TEGRA124_CLK_DSIALP, TEGRA124_CLK_PLL_P, 68000000, 0 },
	{ TEGRA124_CLK_DSIBLP, TEGRA124_CLK_PLL_P, 68000000, 0 },
	{ TEGRA124_CLK_SCLK, TEGRA124_CLK_PLL_P_OUT2, 102000000, 0 },
	{ TEGRA124_CLK_DFLL_SOC, TEGRA124_CLK_PLL_P, 51000000, 1 },
	{ TEGRA124_CLK_DFLL_REF, TEGRA124_CLK_PLL_P, 51000000, 1 },
	{ TEGRA124_CLK_PLL_C, TEGRA124_CLK_CLK_MAX, 768000000, 0 },
	{ TEGRA124_CLK_PLL_C_OUT1, TEGRA124_CLK_CLK_MAX, 100000000, 0 },
	{ TEGRA124_CLK_SBC4, TEGRA124_CLK_PLL_P, 12000000, 1 },
	{ TEGRA124_CLK_TSEC, TEGRA124_CLK_PLL_C3, 0, 0 },
	{ TEGRA124_CLK_MSENC, TEGRA124_CLK_PLL_C3, 0, 0 },
	{ TEGRA124_CLK_PLL_RE_VCO, TEGRA124_CLK_CLK_MAX, 672000000, 0 },
	{ TEGRA124_CLK_XUSB_SS_SRC, TEGRA124_CLK_PLL_U_480M, 120000000, 0 },
	{ TEGRA124_CLK_XUSB_FS_SRC, TEGRA124_CLK_PLL_U_48M, 48000000, 0 },
	{ TEGRA124_CLK_XUSB_HS_SRC, TEGRA124_CLK_PLL_U_60M, 60000000, 0 },
	{ TEGRA124_CLK_XUSB_FALCON_SRC, TEGRA124_CLK_PLL_RE_OUT, 224000000, 0 },
	{ TEGRA124_CLK_XUSB_HOST_SRC, TEGRA124_CLK_PLL_RE_OUT, 112000000, 0 },
	{ TEGRA124_CLK_SATA, TEGRA124_CLK_PLL_P, 104000000, 0 },
	{ TEGRA124_CLK_SATA_OOB, TEGRA124_CLK_PLL_P, 204000000, 0 },
	{ TEGRA124_CLK_MSELECT, TEGRA124_CLK_CLK_MAX, 0, 1 },
	{ TEGRA124_CLK_CSITE, TEGRA124_CLK_CLK_MAX, 0, 1 },
	{ TEGRA124_CLK_TSENSOR, TEGRA124_CLK_CLK_M, 400000, 0 },
	/* must be the last entry */
	{ TEGRA124_CLK_CLK_MAX, TEGRA124_CLK_CLK_MAX, 0, 0 },
};

static struct tegra_clk_init_table tegra124_init_table[] __initdata = {
	{ TEGRA124_CLK_SOC_THERM, TEGRA124_CLK_PLL_P, 51000000, 0 },
	{ TEGRA124_CLK_CCLK_G, TEGRA124_CLK_CLK_MAX, 0, 1 },
	{ TEGRA124_CLK_HDA, TEGRA124_CLK_PLL_P, 102000000, 0 },
	{ TEGRA124_CLK_HDA2CODEC_2X, TEGRA124_CLK_PLL_P, 48000000, 0 },
	/* must be the last entry */
	{ TEGRA124_CLK_CLK_MAX, TEGRA124_CLK_CLK_MAX, 0, 0 },
};

/* Tegra132 requires the SOC_THERM clock to remain active */
static struct tegra_clk_init_table tegra132_init_table[] __initdata = {
	{ TEGRA124_CLK_SOC_THERM, TEGRA124_CLK_PLL_P, 51000000, 1 },
	/* must be the last entry */
	{ TEGRA124_CLK_CLK_MAX, TEGRA124_CLK_CLK_MAX, 0, 0 },
};

static struct tegra_audio_clk_info tegra124_audio_plls[] = {
	{ "pll_a", &pll_a_params, tegra_clk_pll_a, "pll_p_out1" },
};

/**
 * tegra124_clock_apply_init_table - initialize clocks on Tegra124 SoCs
 *
 * Program an initial clock rate and enable or disable clocks needed
 * by the rest of the kernel, for Tegra124 SoCs.  It is intended to be
 * called by assigning a pointer to it to tegra_clk_apply_init_table -
 * this will be called as an arch_initcall.  No return value.
 */
static void __init tegra124_clock_apply_init_table(void)
{
	tegra_init_from_table(common_init_table, clks, TEGRA124_CLK_CLK_MAX);
	tegra_init_from_table(tegra124_init_table, clks, TEGRA124_CLK_CLK_MAX);
}

/**
 * tegra124_car_barrier - wait for pending writes to the CAR to complete
 *
 * Wait for any outstanding writes to the CAR MMIO space from this CPU
 * to complete before continuing execution.  No return value.
 */
static void tegra124_car_barrier(void)
{
	readl_relaxed(clk_base + RST_DFLL_DVCO);
}

/**
 * tegra124_clock_assert_dfll_dvco_reset - assert the DFLL's DVCO reset
 *
 * Assert the reset line of the DFLL's DVCO.  No return value.
 */
static void tegra124_clock_assert_dfll_dvco_reset(void)
{
	u32 v;

	v = readl_relaxed(clk_base + RST_DFLL_DVCO);
	v |= (1 << DVFS_DFLL_RESET_SHIFT);
	writel_relaxed(v, clk_base + RST_DFLL_DVCO);
	tegra124_car_barrier();
}

/**
 * tegra124_clock_deassert_dfll_dvco_reset - deassert the DFLL's DVCO reset
 *
 * Deassert the reset line of the DFLL's DVCO, allowing the DVCO to
 * operate.  No return value.
 */
static void tegra124_clock_deassert_dfll_dvco_reset(void)
{
	u32 v;

	v = readl_relaxed(clk_base + RST_DFLL_DVCO);
	v &= ~(1 << DVFS_DFLL_RESET_SHIFT);
	writel_relaxed(v, clk_base + RST_DFLL_DVCO);
	tegra124_car_barrier();
}

static int tegra124_reset_assert(unsigned long id)
{
	if (id == TEGRA124_RST_DFLL_DVCO)
		tegra124_clock_assert_dfll_dvco_reset();
	else
		return -EINVAL;

	return 0;
}

static int tegra124_reset_deassert(unsigned long id)
{
	if (id == TEGRA124_RST_DFLL_DVCO)
		tegra124_clock_deassert_dfll_dvco_reset();
	else
		return -EINVAL;

	return 0;
}

/**
 * tegra132_clock_apply_init_table - initialize clocks on Tegra132 SoCs
 *
 * Program an initial clock rate and enable or disable clocks needed
 * by the rest of the kernel, for Tegra132 SoCs.  It is intended to be
 * called by assigning a pointer to it to tegra_clk_apply_init_table -
 * this will be called as an arch_initcall.  No return value.
 */
static void __init tegra132_clock_apply_init_table(void)
{
	tegra_init_from_table(common_init_table, clks, TEGRA124_CLK_CLK_MAX);
	tegra_init_from_table(tegra132_init_table, clks, TEGRA124_CLK_CLK_MAX);
}

/**
 * tegra124_132_clock_init_pre - clock initialization preamble for T124/T132
 * @np: struct device_node * of the DT node for the SoC CAR IP block
 *
 * Register most of the clocks controlled by the CAR IP block, along
 * with a few clocks controlled by the PMC IP block.  Everything in
 * this function should be common to Tegra124 and Tegra132.  XXX The
 * PMC clock initialization should probably be moved to PMC-specific
 * driver code.  No return value.
 */
static void __init tegra124_132_clock_init_pre(struct device_node *np)
{
	struct device_node *node;
	u32 plld_base;

	clk_base = of_iomap(np, 0);
	if (!clk_base) {
		pr_err("ioremap tegra124/tegra132 CAR failed\n");
		return;
	}

	node = of_find_matching_node(NULL, pmc_match);
	if (!node) {
		pr_err("Failed to find pmc node\n");
		WARN_ON(1);
		return;
	}

	pmc_base = of_iomap(node, 0);
	if (!pmc_base) {
		pr_err("Can't map pmc registers\n");
		WARN_ON(1);
		return;
	}

	clks = tegra_clk_init(clk_base, TEGRA124_CLK_CLK_MAX,
			      TEGRA124_CAR_BANK_COUNT);
	if (!clks)
		return;

	if (tegra_osc_clk_init(clk_base, tegra124_clks, tegra124_input_freq,
			       ARRAY_SIZE(tegra124_input_freq), 1, &osc_freq,
			       &pll_ref_freq) < 0)
		return;

	tegra_fixed_clk_init(tegra124_clks);
	tegra124_pll_init(clk_base, pmc_base);
	tegra124_periph_clk_init(clk_base, pmc_base);
	tegra_audio_clk_init(clk_base, pmc_base, tegra124_clks,
			     tegra124_audio_plls,
			     ARRAY_SIZE(tegra124_audio_plls));
	tegra_pmc_clk_init(pmc_base, tegra124_clks);

	/* For Tegra124 & Tegra132, PLLD is the only source for DSIA & DSIB */
	plld_base = clk_readl(clk_base + PLLD_BASE);
	plld_base &= ~BIT(25);
	clk_writel(plld_base, clk_base + PLLD_BASE);
}

/**
 * tegra124_132_clock_init_post - clock initialization postamble for T124/T132
 * @np: struct device_node * of the DT node for the SoC CAR IP block
 *
 * Register most of the along with a few clocks controlled by the PMC
 * IP block.  Everything in this function should be common to Tegra124
 * and Tegra132.  This function must be called after
 * tegra124_132_clock_init_pre(), otherwise clk_base and pmc_base will
 * not be set.  No return value.
 */
static void __init tegra124_132_clock_init_post(struct device_node *np)
{
	tegra_super_clk_gen4_init(clk_base, pmc_base, tegra124_clks,
				  &pll_x_params);
	tegra_init_special_resets(1, tegra124_reset_assert,
				  tegra124_reset_deassert);
	tegra_add_of_provider(np);

	clks[TEGRA124_CLK_EMC] = tegra_clk_register_emc(clk_base, np,
							&emc_lock);

	tegra_register_devclks(devclks, ARRAY_SIZE(devclks));

	tegra_cpu_car_ops = &tegra124_cpu_car_ops;
}

/**
 * tegra124_clock_init - Tegra124-specific clock initialization
 * @np: struct device_node * of the DT node for the SoC CAR IP block
 *
 * Register most SoC clocks for the Tegra124 system-on-chip.  Most of
 * this code is shared between the Tegra124 and Tegra132 SoCs,
 * although some of the initial clock settings and CPU clocks differ.
 * Intended to be called by the OF init code when a DT node with the
 * "nvidia,tegra124-car" string is encountered, and declared with
 * CLK_OF_DECLARE.  No return value.
 */
static void __init tegra124_clock_init(struct device_node *np)
{
	tegra124_132_clock_init_pre(np);
	tegra_clk_apply_init_table = tegra124_clock_apply_init_table;
	tegra124_132_clock_init_post(np);
}

/**
 * tegra132_clock_init - Tegra132-specific clock initialization
 * @np: struct device_node * of the DT node for the SoC CAR IP block
 *
 * Register most SoC clocks for the Tegra132 system-on-chip.  Most of
 * this code is shared between the Tegra124 and Tegra132 SoCs,
 * although some of the initial clock settings and CPU clocks differ.
 * Intended to be called by the OF init code when a DT node with the
 * "nvidia,tegra132-car" string is encountered, and declared with
 * CLK_OF_DECLARE.  No return value.
 */
static void __init tegra132_clock_init(struct device_node *np)
{
	tegra124_132_clock_init_pre(np);

	/*
	 * On Tegra132, these clocks are controlled by the
	 * CLUSTER_clocks IP block, located in the CPU complex
	 */
	tegra124_clks[tegra_clk_cclk_g].present = false;
	tegra124_clks[tegra_clk_cclk_lp].present = false;
	tegra124_clks[tegra_clk_pll_x].present = false;
	tegra124_clks[tegra_clk_pll_x_out0].present = false;

	tegra_clk_apply_init_table = tegra132_clock_apply_init_table;
	tegra124_132_clock_init_post(np);
}
CLK_OF_DECLARE(tegra124, "nvidia,tegra124-car", tegra124_clock_init);
CLK_OF_DECLARE(tegra132, "nvidia,tegra132-car", tegra132_clock_init);
