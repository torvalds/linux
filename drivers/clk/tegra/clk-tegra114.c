/*
 * Copyright (c) 2012, 2013, NVIDIA CORPORATION.  All rights reserved.
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
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/clk/tegra.h>
#include <dt-bindings/clock/tegra114-car.h>

#include "clk.h"
#include "clk-id.h"

#define RST_DFLL_DVCO			0x2F4
#define CPU_FINETRIM_SELECT		0x4d4	/* override default prop dlys */
#define CPU_FINETRIM_DR			0x4d8	/* rise->rise prop dly A */
#define CPU_FINETRIM_R			0x4e4	/* rise->rise prop dly inc A */

/* RST_DFLL_DVCO bitfields */
#define DVFS_DFLL_RESET_SHIFT		0

/* CPU_FINETRIM_SELECT and CPU_FINETRIM_DR bitfields */
#define CPU_FINETRIM_1_FCPU_1		BIT(0)	/* fcpu0 */
#define CPU_FINETRIM_1_FCPU_2		BIT(1)	/* fcpu1 */
#define CPU_FINETRIM_1_FCPU_3		BIT(2)	/* fcpu2 */
#define CPU_FINETRIM_1_FCPU_4		BIT(3)	/* fcpu3 */
#define CPU_FINETRIM_1_FCPU_5		BIT(4)	/* fl2 */
#define CPU_FINETRIM_1_FCPU_6		BIT(5)	/* ftop */

/* CPU_FINETRIM_R bitfields */
#define CPU_FINETRIM_R_FCPU_1_SHIFT	0		/* fcpu0 */
#define CPU_FINETRIM_R_FCPU_1_MASK	(0x3 << CPU_FINETRIM_R_FCPU_1_SHIFT)
#define CPU_FINETRIM_R_FCPU_2_SHIFT	2		/* fcpu1 */
#define CPU_FINETRIM_R_FCPU_2_MASK	(0x3 << CPU_FINETRIM_R_FCPU_2_SHIFT)
#define CPU_FINETRIM_R_FCPU_3_SHIFT	4		/* fcpu2 */
#define CPU_FINETRIM_R_FCPU_3_MASK	(0x3 << CPU_FINETRIM_R_FCPU_3_SHIFT)
#define CPU_FINETRIM_R_FCPU_4_SHIFT	6		/* fcpu3 */
#define CPU_FINETRIM_R_FCPU_4_MASK	(0x3 << CPU_FINETRIM_R_FCPU_4_SHIFT)
#define CPU_FINETRIM_R_FCPU_5_SHIFT	8		/* fl2 */
#define CPU_FINETRIM_R_FCPU_5_MASK	(0x3 << CPU_FINETRIM_R_FCPU_5_SHIFT)
#define CPU_FINETRIM_R_FCPU_6_SHIFT	10		/* ftop */
#define CPU_FINETRIM_R_FCPU_6_MASK	(0x3 << CPU_FINETRIM_R_FCPU_6_SHIFT)

#define TEGRA114_CLK_PERIPH_BANKS	5

#define PLLC_BASE 0x80
#define PLLC_MISC2 0x88
#define PLLC_MISC 0x8c
#define PLLC2_BASE 0x4e8
#define PLLC2_MISC 0x4ec
#define PLLC3_BASE 0x4fc
#define PLLC3_MISC 0x500
#define PLLM_BASE 0x90
#define PLLM_MISC 0x9c
#define PLLP_BASE 0xa0
#define PLLP_MISC 0xac
#define PLLX_BASE 0xe0
#define PLLX_MISC 0xe4
#define PLLX_MISC2 0x514
#define PLLX_MISC3 0x518
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
#define PLLRE_BASE 0x4c4
#define PLLRE_MISC 0x4c8

#define PLL_MISC_LOCK_ENABLE 18
#define PLLC_MISC_LOCK_ENABLE 24
#define PLLDU_MISC_LOCK_ENABLE 22
#define PLLE_MISC_LOCK_ENABLE 9
#define PLLRE_MISC_LOCK_ENABLE 30

#define PLLC_IDDQ_BIT 26
#define PLLX_IDDQ_BIT 3
#define PLLRE_IDDQ_BIT 16

#define PLL_BASE_LOCK BIT(27)
#define PLLE_MISC_LOCK BIT(11)
#define PLLRE_MISC_LOCK BIT(24)
#define PLLCX_BASE_LOCK (BIT(26)|BIT(27))

#define PLLE_AUX 0x48c
#define PLLC_OUT 0x84
#define PLLM_OUT 0x94

#define OSC_CTRL			0x50
#define OSC_CTRL_OSC_FREQ_SHIFT		28
#define OSC_CTRL_PLL_REF_DIV_SHIFT	26

#define PLLXC_SW_MAX_P			6

#define CCLKG_BURST_POLICY 0x368

#define UTMIP_PLL_CFG2 0x488
#define UTMIP_PLL_CFG2_STABLE_COUNT(x) (((x) & 0xffff) << 6)
#define UTMIP_PLL_CFG2_ACTIVE_DLY_COUNT(x) (((x) & 0x3f) << 18)
#define UTMIP_PLL_CFG2_FORCE_PD_SAMP_A_POWERDOWN BIT(0)
#define UTMIP_PLL_CFG2_FORCE_PD_SAMP_B_POWERDOWN BIT(2)
#define UTMIP_PLL_CFG2_FORCE_PD_SAMP_C_POWERDOWN BIT(4)

#define UTMIP_PLL_CFG1 0x484
#define UTMIP_PLL_CFG1_ENABLE_DLY_COUNT(x) (((x) & 0x1f) << 6)
#define UTMIP_PLL_CFG1_XTAL_FREQ_COUNT(x) (((x) & 0xfff) << 0)
#define UTMIP_PLL_CFG1_FORCE_PLLU_POWERUP BIT(17)
#define UTMIP_PLL_CFG1_FORCE_PLLU_POWERDOWN BIT(16)
#define UTMIP_PLL_CFG1_FORCE_PLL_ENABLE_POWERUP BIT(15)
#define UTMIP_PLL_CFG1_FORCE_PLL_ENABLE_POWERDOWN BIT(14)
#define UTMIP_PLL_CFG1_FORCE_PLL_ACTIVE_POWERDOWN BIT(12)

#define UTMIPLL_HW_PWRDN_CFG0			0x52c
#define UTMIPLL_HW_PWRDN_CFG0_SEQ_START_STATE	BIT(25)
#define UTMIPLL_HW_PWRDN_CFG0_SEQ_ENABLE	BIT(24)
#define UTMIPLL_HW_PWRDN_CFG0_USE_LOCKDET	BIT(6)
#define UTMIPLL_HW_PWRDN_CFG0_SEQ_RESET_INPUT_VALUE	BIT(5)
#define UTMIPLL_HW_PWRDN_CFG0_SEQ_IN_SWCTL	BIT(4)
#define UTMIPLL_HW_PWRDN_CFG0_CLK_ENABLE_SWCTL	BIT(2)
#define UTMIPLL_HW_PWRDN_CFG0_IDDQ_OVERRIDE	BIT(1)
#define UTMIPLL_HW_PWRDN_CFG0_IDDQ_SWCTL	BIT(0)

#define CLK_SOURCE_CSITE 0x1d4
#define CLK_SOURCE_EMC 0x19c

/* PLLM override registers */
#define PMC_PLLM_WB0_OVERRIDE 0x1dc
#define PMC_PLLM_WB0_OVERRIDE_2 0x2b0

/* Tegra CPU clock and reset control regs */
#define CLK_RST_CONTROLLER_CPU_CMPLX_STATUS	0x470

#define MUX8(_name, _parents, _offset, \
			     _clk_num, _gate_flags, _clk_id)	\
	TEGRA_INIT_DATA_TABLE(_name, NULL, NULL, _parents, _offset,\
			29, MASK(3), 0, 0, 8, 1, TEGRA_DIVIDER_ROUND_UP,\
			_clk_num, _gate_flags, _clk_id, _parents##_idx, 0,\
			NULL)

#ifdef CONFIG_PM_SLEEP
static struct cpu_clk_suspend_context {
	u32 clk_csite_src;
	u32 cclkg_burst;
	u32 cclkg_divider;
} tegra114_cpu_clk_sctx;
#endif

static void __iomem *clk_base;
static void __iomem *pmc_base;

static DEFINE_SPINLOCK(pll_d_lock);
static DEFINE_SPINLOCK(pll_d2_lock);
static DEFINE_SPINLOCK(pll_u_lock);
static DEFINE_SPINLOCK(pll_re_lock);
static DEFINE_SPINLOCK(emc_lock);

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
	{ .pdiv =  4, .hw_val = 3 },
	{ .pdiv =  8, .hw_val = 5 },
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

static const struct pdiv_map pllm_p[] = {
	{ .pdiv = 1, .hw_val = 0 },
	{ .pdiv = 2, .hw_val = 1 },
	{ .pdiv = 0, .hw_val = 0 },
};

static struct tegra_clk_pll_freq_table pll_m_freq_table[] = {
	{ 12000000, 800000000, 66, 1, 1, 0 }, /* actual: 792.0 MHz */
	{ 13000000, 800000000, 61, 1, 1, 0 }, /* actual: 793.0 MHz */
	{ 16800000, 800000000, 47, 1, 1, 0 }, /* actual: 789.6 MHz */
	{ 19200000, 800000000, 41, 1, 1, 0 }, /* actual: 787.2 MHz */
	{ 26000000, 800000000, 61, 2, 1, 0 }, /* actual: 793.0 MHz */
	{        0,         0,  0, 0, 0, 0 },
};

static struct tegra_clk_pll_params pll_m_params = {
	.input_min = 12000000,
	.input_max = 500000000,
	.cf_min = 12000000,
	.cf_max = 19200000, /* s/w policy, h/w capability 50 MHz */
	.vco_min = 400000000,
	.vco_max = 1066000000,
	.base_reg = PLLM_BASE,
	.misc_reg = PLLM_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLL_MISC_LOCK_ENABLE,
	.lock_delay = 300,
	.max_p = 2,
	.pdiv_tohw = pllm_p,
	.div_nmp = &pllm_nmp,
	.pmc_divnm_reg = PMC_PLLM_WB0_OVERRIDE,
	.pmc_divp_reg = PMC_PLLM_WB0_OVERRIDE_2,
	.freq_table = pll_m_freq_table,
	.flags = TEGRA_PLL_USE_LOCK | TEGRA_PLL_HAS_LOCK_ENABLE |
		 TEGRA_PLL_FIXED,
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
	{ 12000000, 216000000, 432, 12, 2, 8 },
	{ 13000000, 216000000, 432, 13, 2, 8 },
	{ 16800000, 216000000, 360, 14, 2, 8 },
	{ 19200000, 216000000, 360, 16, 2, 8 },
	{ 26000000, 216000000, 432, 26, 2, 8 },
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
	.flags = TEGRA_PLL_FIXED | TEGRA_PLL_USE_LOCK |
		 TEGRA_PLL_HAS_LOCK_ENABLE,
	.fixed_rate = 408000000,
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
	.div_nmp = &pllp_nmp,
	.freq_table = pll_d_freq_table,
	.flags = TEGRA_PLL_HAS_CPCON | TEGRA_PLL_SET_LFCON |
		 TEGRA_PLL_USE_LOCK | TEGRA_PLL_HAS_LOCK_ENABLE,
};

static struct tegra_clk_pll_params pll_d2_params = {
	.input_min = 2000000,
	.input_max = 40000000,
	.cf_min = 1000000,
	.cf_max = 6000000,
	.vco_min = 500000000,
	.vco_max = 1000000000,
	.base_reg = PLLD2_BASE,
	.misc_reg = PLLD2_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLLDU_MISC_LOCK_ENABLE,
	.lock_delay = 1000,
	.div_nmp = &pllp_nmp,
	.freq_table = pll_d_freq_table,
	.flags = TEGRA_PLL_HAS_CPCON | TEGRA_PLL_SET_LFCON |
		 TEGRA_PLL_USE_LOCK | TEGRA_PLL_HAS_LOCK_ENABLE,
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
	.cf_max = 19200000, /* s/w policy, h/w capability 50 MHz */
	.vco_min = 700000000,
	.vco_max = 2400000000U,
	.base_reg = PLLX_BASE,
	.misc_reg = PLLX_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLL_MISC_LOCK_ENABLE,
	.lock_delay = 300,
	.iddq_reg = PLLX_MISC3,
	.iddq_bit_idx = PLLX_IDDQ_BIT,
	.max_p = PLLXC_SW_MAX_P,
	.dyn_ramp_reg = PLLX_MISC2,
	.stepa_shift = 16,
	.stepb_shift = 24,
	.pdiv_tohw = pllxc_p,
	.div_nmp = &pllxc_nmp,
	.freq_table = pll_x_freq_table,
	.flags = TEGRA_PLL_USE_LOCK | TEGRA_PLL_HAS_LOCK_ENABLE,
};

static struct tegra_clk_pll_freq_table pll_e_freq_table[] = {
	/* PLLE special case: use cpcon field to store cml divider value */
	{ 336000000, 100000000, 100, 21, 16, 11 },
	{ 312000000, 100000000, 200, 26, 24, 13 },
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
	{ .pdiv =  0, .hw_val =  0 }
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

/* possible OSC frequencies in Hz */
static unsigned long tegra114_input_freq[] = {
	[ 0] = 13000000,
	[ 1] = 16800000,
	[ 4] = 19200000,
	[ 5] = 38400000,
	[ 8] = 12000000,
	[ 9] = 48000000,
	[12] = 26000000,
};

#define MASK(x) (BIT(x) - 1)

struct utmi_clk_param {
	/* Oscillator Frequency in KHz */
	u32 osc_frequency;
	/* UTMIP PLL Enable Delay Count  */
	u8 enable_delay_count;
	/* UTMIP PLL Stable count */
	u8 stable_count;
	/*  UTMIP PLL Active delay count */
	u8 active_delay_count;
	/* UTMIP PLL Xtal frequency count */
	u8 xtal_freq_count;
};

static const struct utmi_clk_param utmi_parameters[] = {
	{
		.osc_frequency = 13000000, .enable_delay_count = 0x02,
		.stable_count = 0x33, .active_delay_count = 0x05,
		.xtal_freq_count = 0x7f
	}, {
		.osc_frequency = 19200000, .enable_delay_count = 0x03,
		.stable_count = 0x4b, .active_delay_count = 0x06,
		.xtal_freq_count = 0xbb
	}, {
		.osc_frequency = 12000000, .enable_delay_count = 0x02,
		.stable_count = 0x2f, .active_delay_count = 0x04,
		.xtal_freq_count = 0x76
	}, {
		.osc_frequency = 26000000, .enable_delay_count = 0x04,
		.stable_count = 0x66, .active_delay_count = 0x09,
		.xtal_freq_count = 0xfe
	}, {
		.osc_frequency = 16800000, .enable_delay_count = 0x03,
		.stable_count = 0x41, .active_delay_count = 0x0a,
		.xtal_freq_count = 0xa4
	},
};

/* peripheral mux definitions */

static const char *mux_plld_out0_plld2_out0[] = {
	"pll_d_out0", "pll_d2_out0",
};
#define mux_plld_out0_plld2_out0_idx NULL

static const char *mux_pllmcp_clkm[] = {
	"pll_m_out0", "pll_c_out0", "pll_p_out0", "clk_m", "pll_m_ud",
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

static struct tegra_clk tegra114_clks[tegra_clk_max] __initdata = {
	[tegra_clk_rtc] = { .dt_id = TEGRA114_CLK_RTC, .present = true },
	[tegra_clk_timer] = { .dt_id = TEGRA114_CLK_TIMER, .present = true },
	[tegra_clk_uarta] = { .dt_id = TEGRA114_CLK_UARTA, .present = true },
	[tegra_clk_uartd] = { .dt_id = TEGRA114_CLK_UARTD, .present = true },
	[tegra_clk_sdmmc2_8] = { .dt_id = TEGRA114_CLK_SDMMC2, .present = true },
	[tegra_clk_i2s1] = { .dt_id = TEGRA114_CLK_I2S1, .present = true },
	[tegra_clk_i2c1] = { .dt_id = TEGRA114_CLK_I2C1, .present = true },
	[tegra_clk_ndflash] = { .dt_id = TEGRA114_CLK_NDFLASH, .present = true },
	[tegra_clk_sdmmc1_8] = { .dt_id = TEGRA114_CLK_SDMMC1, .present = true },
	[tegra_clk_sdmmc4_8] = { .dt_id = TEGRA114_CLK_SDMMC4, .present = true },
	[tegra_clk_pwm] = { .dt_id = TEGRA114_CLK_PWM, .present = true },
	[tegra_clk_i2s0] = { .dt_id = TEGRA114_CLK_I2S0, .present = true },
	[tegra_clk_i2s2] = { .dt_id = TEGRA114_CLK_I2S2, .present = true },
	[tegra_clk_epp_8] = { .dt_id = TEGRA114_CLK_EPP, .present = true },
	[tegra_clk_gr2d_8] = { .dt_id = TEGRA114_CLK_GR2D, .present = true },
	[tegra_clk_usbd] = { .dt_id = TEGRA114_CLK_USBD, .present = true },
	[tegra_clk_isp] = { .dt_id = TEGRA114_CLK_ISP, .present = true },
	[tegra_clk_gr3d_8] = { .dt_id = TEGRA114_CLK_GR3D, .present = true },
	[tegra_clk_disp2] = { .dt_id = TEGRA114_CLK_DISP2, .present = true },
	[tegra_clk_disp1] = { .dt_id = TEGRA114_CLK_DISP1, .present = true },
	[tegra_clk_host1x_8] = { .dt_id = TEGRA114_CLK_HOST1X, .present = true },
	[tegra_clk_vcp] = { .dt_id = TEGRA114_CLK_VCP, .present = true },
	[tegra_clk_apbdma] = { .dt_id = TEGRA114_CLK_APBDMA, .present = true },
	[tegra_clk_kbc] = { .dt_id = TEGRA114_CLK_KBC, .present = true },
	[tegra_clk_kfuse] = { .dt_id = TEGRA114_CLK_KFUSE, .present = true },
	[tegra_clk_sbc1_8] = { .dt_id = TEGRA114_CLK_SBC1, .present = true },
	[tegra_clk_nor] = { .dt_id = TEGRA114_CLK_NOR, .present = true },
	[tegra_clk_sbc2_8] = { .dt_id = TEGRA114_CLK_SBC2, .present = true },
	[tegra_clk_sbc3_8] = { .dt_id = TEGRA114_CLK_SBC3, .present = true },
	[tegra_clk_i2c5] = { .dt_id = TEGRA114_CLK_I2C5, .present = true },
	[tegra_clk_mipi] = { .dt_id = TEGRA114_CLK_MIPI, .present = true },
	[tegra_clk_hdmi] = { .dt_id = TEGRA114_CLK_HDMI, .present = true },
	[tegra_clk_csi] = { .dt_id = TEGRA114_CLK_CSI, .present = true },
	[tegra_clk_i2c2] = { .dt_id = TEGRA114_CLK_I2C2, .present = true },
	[tegra_clk_uartc] = { .dt_id = TEGRA114_CLK_UARTC, .present = true },
	[tegra_clk_mipi_cal] = { .dt_id = TEGRA114_CLK_MIPI_CAL, .present = true },
	[tegra_clk_emc] = { .dt_id = TEGRA114_CLK_EMC, .present = true },
	[tegra_clk_usb2] = { .dt_id = TEGRA114_CLK_USB2, .present = true },
	[tegra_clk_usb3] = { .dt_id = TEGRA114_CLK_USB3, .present = true },
	[tegra_clk_vde_8] = { .dt_id = TEGRA114_CLK_VDE, .present = true },
	[tegra_clk_bsea] = { .dt_id = TEGRA114_CLK_BSEA, .present = true },
	[tegra_clk_bsev] = { .dt_id = TEGRA114_CLK_BSEV, .present = true },
	[tegra_clk_i2c3] = { .dt_id = TEGRA114_CLK_I2C3, .present = true },
	[tegra_clk_sbc4_8] = { .dt_id = TEGRA114_CLK_SBC4, .present = true },
	[tegra_clk_sdmmc3_8] = { .dt_id = TEGRA114_CLK_SDMMC3, .present = true },
	[tegra_clk_owr] = { .dt_id = TEGRA114_CLK_OWR, .present = true },
	[tegra_clk_csite] = { .dt_id = TEGRA114_CLK_CSITE, .present = true },
	[tegra_clk_la] = { .dt_id = TEGRA114_CLK_LA, .present = true },
	[tegra_clk_trace] = { .dt_id = TEGRA114_CLK_TRACE, .present = true },
	[tegra_clk_soc_therm] = { .dt_id = TEGRA114_CLK_SOC_THERM, .present = true },
	[tegra_clk_dtv] = { .dt_id = TEGRA114_CLK_DTV, .present = true },
	[tegra_clk_ndspeed] = { .dt_id = TEGRA114_CLK_NDSPEED, .present = true },
	[tegra_clk_i2cslow] = { .dt_id = TEGRA114_CLK_I2CSLOW, .present = true },
	[tegra_clk_tsec] = { .dt_id = TEGRA114_CLK_TSEC, .present = true },
	[tegra_clk_xusb_host] = { .dt_id = TEGRA114_CLK_XUSB_HOST, .present = true },
	[tegra_clk_msenc] = { .dt_id = TEGRA114_CLK_MSENC, .present = true },
	[tegra_clk_csus] = { .dt_id = TEGRA114_CLK_CSUS, .present = true },
	[tegra_clk_mselect] = { .dt_id = TEGRA114_CLK_MSELECT, .present = true },
	[tegra_clk_tsensor] = { .dt_id = TEGRA114_CLK_TSENSOR, .present = true },
	[tegra_clk_i2s3] = { .dt_id = TEGRA114_CLK_I2S3, .present = true },
	[tegra_clk_i2s4] = { .dt_id = TEGRA114_CLK_I2S4, .present = true },
	[tegra_clk_i2c4] = { .dt_id = TEGRA114_CLK_I2C4, .present = true },
	[tegra_clk_sbc5_8] = { .dt_id = TEGRA114_CLK_SBC5, .present = true },
	[tegra_clk_sbc6_8] = { .dt_id = TEGRA114_CLK_SBC6, .present = true },
	[tegra_clk_d_audio] = { .dt_id = TEGRA114_CLK_D_AUDIO, .present = true },
	[tegra_clk_apbif] = { .dt_id = TEGRA114_CLK_APBIF, .present = true },
	[tegra_clk_dam0] = { .dt_id = TEGRA114_CLK_DAM0, .present = true },
	[tegra_clk_dam1] = { .dt_id = TEGRA114_CLK_DAM1, .present = true },
	[tegra_clk_dam2] = { .dt_id = TEGRA114_CLK_DAM2, .present = true },
	[tegra_clk_hda2codec_2x] = { .dt_id = TEGRA114_CLK_HDA2CODEC_2X, .present = true },
	[tegra_clk_audio0_2x] = { .dt_id = TEGRA114_CLK_AUDIO0_2X, .present = true },
	[tegra_clk_audio1_2x] = { .dt_id = TEGRA114_CLK_AUDIO1_2X, .present = true },
	[tegra_clk_audio2_2x] = { .dt_id = TEGRA114_CLK_AUDIO2_2X, .present = true },
	[tegra_clk_audio3_2x] = { .dt_id = TEGRA114_CLK_AUDIO3_2X, .present = true },
	[tegra_clk_audio4_2x] = { .dt_id = TEGRA114_CLK_AUDIO4_2X, .present = true },
	[tegra_clk_spdif_2x] = { .dt_id = TEGRA114_CLK_SPDIF_2X, .present = true },
	[tegra_clk_actmon] = { .dt_id = TEGRA114_CLK_ACTMON, .present = true },
	[tegra_clk_extern1] = { .dt_id = TEGRA114_CLK_EXTERN1, .present = true },
	[tegra_clk_extern2] = { .dt_id = TEGRA114_CLK_EXTERN2, .present = true },
	[tegra_clk_extern3] = { .dt_id = TEGRA114_CLK_EXTERN3, .present = true },
	[tegra_clk_hda] = { .dt_id = TEGRA114_CLK_HDA, .present = true },
	[tegra_clk_se] = { .dt_id = TEGRA114_CLK_SE, .present = true },
	[tegra_clk_hda2hdmi] = { .dt_id = TEGRA114_CLK_HDA2HDMI, .present = true },
	[tegra_clk_cilab] = { .dt_id = TEGRA114_CLK_CILAB, .present = true },
	[tegra_clk_cilcd] = { .dt_id = TEGRA114_CLK_CILCD, .present = true },
	[tegra_clk_cile] = { .dt_id = TEGRA114_CLK_CILE, .present = true },
	[tegra_clk_dsialp] = { .dt_id = TEGRA114_CLK_DSIALP, .present = true },
	[tegra_clk_dsiblp] = { .dt_id = TEGRA114_CLK_DSIBLP, .present = true },
	[tegra_clk_dds] = { .dt_id = TEGRA114_CLK_DDS, .present = true },
	[tegra_clk_dp2] = { .dt_id = TEGRA114_CLK_DP2, .present = true },
	[tegra_clk_amx] = { .dt_id = TEGRA114_CLK_AMX, .present = true },
	[tegra_clk_adx] = { .dt_id = TEGRA114_CLK_ADX, .present = true },
	[tegra_clk_xusb_ss] = { .dt_id = TEGRA114_CLK_XUSB_SS, .present = true },
	[tegra_clk_uartb] = { .dt_id = TEGRA114_CLK_UARTB, .present = true },
	[tegra_clk_vfir] = { .dt_id = TEGRA114_CLK_VFIR, .present = true },
	[tegra_clk_spdif_in] = { .dt_id = TEGRA114_CLK_SPDIF_IN, .present = true },
	[tegra_clk_spdif_out] = { .dt_id = TEGRA114_CLK_SPDIF_OUT, .present = true },
	[tegra_clk_vi_8] = { .dt_id = TEGRA114_CLK_VI, .present = true },
	[tegra_clk_fuse] = { .dt_id = TEGRA114_CLK_FUSE, .present = true },
	[tegra_clk_fuse_burn] = { .dt_id = TEGRA114_CLK_FUSE_BURN, .present = true },
	[tegra_clk_clk_32k] = { .dt_id = TEGRA114_CLK_CLK_32K, .present = true },
	[tegra_clk_clk_m] = { .dt_id = TEGRA114_CLK_CLK_M, .present = true },
	[tegra_clk_clk_m_div2] = { .dt_id = TEGRA114_CLK_CLK_M_DIV2, .present = true },
	[tegra_clk_clk_m_div4] = { .dt_id = TEGRA114_CLK_CLK_M_DIV4, .present = true },
	[tegra_clk_pll_ref] = { .dt_id = TEGRA114_CLK_PLL_REF, .present = true },
	[tegra_clk_pll_c] = { .dt_id = TEGRA114_CLK_PLL_C, .present = true },
	[tegra_clk_pll_c_out1] = { .dt_id = TEGRA114_CLK_PLL_C_OUT1, .present = true },
	[tegra_clk_pll_c2] = { .dt_id = TEGRA114_CLK_PLL_C2, .present = true },
	[tegra_clk_pll_c3] = { .dt_id = TEGRA114_CLK_PLL_C3, .present = true },
	[tegra_clk_pll_m] = { .dt_id = TEGRA114_CLK_PLL_M, .present = true },
	[tegra_clk_pll_m_out1] = { .dt_id = TEGRA114_CLK_PLL_M_OUT1, .present = true },
	[tegra_clk_pll_p] = { .dt_id = TEGRA114_CLK_PLL_P, .present = true },
	[tegra_clk_pll_p_out1] = { .dt_id = TEGRA114_CLK_PLL_P_OUT1, .present = true },
	[tegra_clk_pll_p_out2_int] = { .dt_id = TEGRA114_CLK_PLL_P_OUT2, .present = true },
	[tegra_clk_pll_p_out3] = { .dt_id = TEGRA114_CLK_PLL_P_OUT3, .present = true },
	[tegra_clk_pll_p_out4] = { .dt_id = TEGRA114_CLK_PLL_P_OUT4, .present = true },
	[tegra_clk_pll_a] = { .dt_id = TEGRA114_CLK_PLL_A, .present = true },
	[tegra_clk_pll_a_out0] = { .dt_id = TEGRA114_CLK_PLL_A_OUT0, .present = true },
	[tegra_clk_pll_d] = { .dt_id = TEGRA114_CLK_PLL_D, .present = true },
	[tegra_clk_pll_d_out0] = { .dt_id = TEGRA114_CLK_PLL_D_OUT0, .present = true },
	[tegra_clk_pll_d2] = { .dt_id = TEGRA114_CLK_PLL_D2, .present = true },
	[tegra_clk_pll_d2_out0] = { .dt_id = TEGRA114_CLK_PLL_D2_OUT0, .present = true },
	[tegra_clk_pll_u] = { .dt_id = TEGRA114_CLK_PLL_U, .present = true },
	[tegra_clk_pll_u_480m] = { .dt_id = TEGRA114_CLK_PLL_U_480M, .present = true },
	[tegra_clk_pll_u_60m] = { .dt_id = TEGRA114_CLK_PLL_U_60M, .present = true },
	[tegra_clk_pll_u_48m] = { .dt_id = TEGRA114_CLK_PLL_U_48M, .present = true },
	[tegra_clk_pll_u_12m] = { .dt_id = TEGRA114_CLK_PLL_U_12M, .present = true },
	[tegra_clk_pll_x] = { .dt_id = TEGRA114_CLK_PLL_X, .present = true },
	[tegra_clk_pll_x_out0] = { .dt_id = TEGRA114_CLK_PLL_X_OUT0, .present = true },
	[tegra_clk_pll_re_vco] = { .dt_id = TEGRA114_CLK_PLL_RE_VCO, .present = true },
	[tegra_clk_pll_re_out] = { .dt_id = TEGRA114_CLK_PLL_RE_OUT, .present = true },
	[tegra_clk_pll_e_out0] = { .dt_id = TEGRA114_CLK_PLL_E_OUT0, .present = true },
	[tegra_clk_spdif_in_sync] = { .dt_id = TEGRA114_CLK_SPDIF_IN_SYNC, .present = true },
	[tegra_clk_i2s0_sync] = { .dt_id = TEGRA114_CLK_I2S0_SYNC, .present = true },
	[tegra_clk_i2s1_sync] = { .dt_id = TEGRA114_CLK_I2S1_SYNC, .present = true },
	[tegra_clk_i2s2_sync] = { .dt_id = TEGRA114_CLK_I2S2_SYNC, .present = true },
	[tegra_clk_i2s3_sync] = { .dt_id = TEGRA114_CLK_I2S3_SYNC, .present = true },
	[tegra_clk_i2s4_sync] = { .dt_id = TEGRA114_CLK_I2S4_SYNC, .present = true },
	[tegra_clk_vimclk_sync] = { .dt_id = TEGRA114_CLK_VIMCLK_SYNC, .present = true },
	[tegra_clk_audio0] = { .dt_id = TEGRA114_CLK_AUDIO0, .present = true },
	[tegra_clk_audio1] = { .dt_id = TEGRA114_CLK_AUDIO1, .present = true },
	[tegra_clk_audio2] = { .dt_id = TEGRA114_CLK_AUDIO2, .present = true },
	[tegra_clk_audio3] = { .dt_id = TEGRA114_CLK_AUDIO3, .present = true },
	[tegra_clk_audio4] = { .dt_id = TEGRA114_CLK_AUDIO4, .present = true },
	[tegra_clk_spdif] = { .dt_id = TEGRA114_CLK_SPDIF, .present = true },
	[tegra_clk_clk_out_1] = { .dt_id = TEGRA114_CLK_CLK_OUT_1, .present = true },
	[tegra_clk_clk_out_2] = { .dt_id = TEGRA114_CLK_CLK_OUT_2, .present = true },
	[tegra_clk_clk_out_3] = { .dt_id = TEGRA114_CLK_CLK_OUT_3, .present = true },
	[tegra_clk_blink] = { .dt_id = TEGRA114_CLK_BLINK, .present = true },
	[tegra_clk_xusb_host_src] = { .dt_id = TEGRA114_CLK_XUSB_HOST_SRC, .present = true },
	[tegra_clk_xusb_falcon_src] = { .dt_id = TEGRA114_CLK_XUSB_FALCON_SRC, .present = true },
	[tegra_clk_xusb_fs_src] = { .dt_id = TEGRA114_CLK_XUSB_FS_SRC, .present = true },
	[tegra_clk_xusb_ss_src] = { .dt_id = TEGRA114_CLK_XUSB_SS_SRC, .present = true },
	[tegra_clk_xusb_ss_div2] = { .dt_id = TEGRA114_CLK_XUSB_SS_DIV2, .present = true},
	[tegra_clk_xusb_dev_src] = { .dt_id = TEGRA114_CLK_XUSB_DEV_SRC, .present = true },
	[tegra_clk_xusb_dev] = { .dt_id = TEGRA114_CLK_XUSB_DEV, .present = true },
	[tegra_clk_xusb_hs_src] = { .dt_id = TEGRA114_CLK_XUSB_HS_SRC, .present = true },
	[tegra_clk_sclk] = { .dt_id = TEGRA114_CLK_SCLK, .present = true },
	[tegra_clk_hclk] = { .dt_id = TEGRA114_CLK_HCLK, .present = true },
	[tegra_clk_pclk] = { .dt_id = TEGRA114_CLK_PCLK, .present = true },
	[tegra_clk_cclk_g] = { .dt_id = TEGRA114_CLK_CCLK_G, .present = true },
	[tegra_clk_cclk_lp] = { .dt_id = TEGRA114_CLK_CCLK_LP, .present = true },
	[tegra_clk_dfll_ref] = { .dt_id = TEGRA114_CLK_DFLL_REF, .present = true },
	[tegra_clk_dfll_soc] = { .dt_id = TEGRA114_CLK_DFLL_SOC, .present = true },
	[tegra_clk_audio0_mux] = { .dt_id = TEGRA114_CLK_AUDIO0_MUX, .present = true },
	[tegra_clk_audio1_mux] = { .dt_id = TEGRA114_CLK_AUDIO1_MUX, .present = true },
	[tegra_clk_audio2_mux] = { .dt_id = TEGRA114_CLK_AUDIO2_MUX, .present = true },
	[tegra_clk_audio3_mux] = { .dt_id = TEGRA114_CLK_AUDIO3_MUX, .present = true },
	[tegra_clk_audio4_mux] = { .dt_id = TEGRA114_CLK_AUDIO4_MUX, .present = true },
	[tegra_clk_spdif_mux] = { .dt_id = TEGRA114_CLK_SPDIF_MUX, .present = true },
	[tegra_clk_clk_out_1_mux] = { .dt_id = TEGRA114_CLK_CLK_OUT_1_MUX, .present = true },
	[tegra_clk_clk_out_2_mux] = { .dt_id = TEGRA114_CLK_CLK_OUT_2_MUX, .present = true },
	[tegra_clk_clk_out_3_mux] = { .dt_id = TEGRA114_CLK_CLK_OUT_3_MUX, .present = true },
	[tegra_clk_dsia_mux] = { .dt_id = TEGRA114_CLK_DSIA_MUX, .present = true },
	[tegra_clk_dsib_mux] = { .dt_id = TEGRA114_CLK_DSIB_MUX, .present = true },
};

static struct tegra_devclk devclks[] __initdata = {
	{ .con_id = "clk_m", .dt_id = TEGRA114_CLK_CLK_M },
	{ .con_id = "pll_ref", .dt_id = TEGRA114_CLK_PLL_REF },
	{ .con_id = "clk_32k", .dt_id = TEGRA114_CLK_CLK_32K },
	{ .con_id = "clk_m_div2", .dt_id = TEGRA114_CLK_CLK_M_DIV2 },
	{ .con_id = "clk_m_div4", .dt_id = TEGRA114_CLK_CLK_M_DIV4 },
	{ .con_id = "pll_c", .dt_id = TEGRA114_CLK_PLL_C },
	{ .con_id = "pll_c_out1", .dt_id = TEGRA114_CLK_PLL_C_OUT1 },
	{ .con_id = "pll_c2", .dt_id = TEGRA114_CLK_PLL_C2 },
	{ .con_id = "pll_c3", .dt_id = TEGRA114_CLK_PLL_C3 },
	{ .con_id = "pll_p", .dt_id = TEGRA114_CLK_PLL_P },
	{ .con_id = "pll_p_out1", .dt_id = TEGRA114_CLK_PLL_P_OUT1 },
	{ .con_id = "pll_p_out2", .dt_id = TEGRA114_CLK_PLL_P_OUT2 },
	{ .con_id = "pll_p_out3", .dt_id = TEGRA114_CLK_PLL_P_OUT3 },
	{ .con_id = "pll_p_out4", .dt_id = TEGRA114_CLK_PLL_P_OUT4 },
	{ .con_id = "pll_m", .dt_id = TEGRA114_CLK_PLL_M },
	{ .con_id = "pll_m_out1", .dt_id = TEGRA114_CLK_PLL_M_OUT1 },
	{ .con_id = "pll_x", .dt_id = TEGRA114_CLK_PLL_X },
	{ .con_id = "pll_x_out0", .dt_id = TEGRA114_CLK_PLL_X_OUT0 },
	{ .con_id = "pll_u", .dt_id = TEGRA114_CLK_PLL_U },
	{ .con_id = "pll_u_480M", .dt_id = TEGRA114_CLK_PLL_U_480M },
	{ .con_id = "pll_u_60M", .dt_id = TEGRA114_CLK_PLL_U_60M },
	{ .con_id = "pll_u_48M", .dt_id = TEGRA114_CLK_PLL_U_48M },
	{ .con_id = "pll_u_12M", .dt_id = TEGRA114_CLK_PLL_U_12M },
	{ .con_id = "pll_d", .dt_id = TEGRA114_CLK_PLL_D },
	{ .con_id = "pll_d_out0", .dt_id = TEGRA114_CLK_PLL_D_OUT0 },
	{ .con_id = "pll_d2", .dt_id = TEGRA114_CLK_PLL_D2 },
	{ .con_id = "pll_d2_out0", .dt_id = TEGRA114_CLK_PLL_D2_OUT0 },
	{ .con_id = "pll_a", .dt_id = TEGRA114_CLK_PLL_A },
	{ .con_id = "pll_a_out0", .dt_id = TEGRA114_CLK_PLL_A_OUT0 },
	{ .con_id = "pll_re_vco", .dt_id = TEGRA114_CLK_PLL_RE_VCO },
	{ .con_id = "pll_re_out", .dt_id = TEGRA114_CLK_PLL_RE_OUT },
	{ .con_id = "pll_e_out0", .dt_id = TEGRA114_CLK_PLL_E_OUT0 },
	{ .con_id = "spdif_in_sync", .dt_id = TEGRA114_CLK_SPDIF_IN_SYNC },
	{ .con_id = "i2s0_sync", .dt_id = TEGRA114_CLK_I2S0_SYNC },
	{ .con_id = "i2s1_sync", .dt_id = TEGRA114_CLK_I2S1_SYNC },
	{ .con_id = "i2s2_sync", .dt_id = TEGRA114_CLK_I2S2_SYNC },
	{ .con_id = "i2s3_sync", .dt_id = TEGRA114_CLK_I2S3_SYNC },
	{ .con_id = "i2s4_sync", .dt_id = TEGRA114_CLK_I2S4_SYNC },
	{ .con_id = "vimclk_sync", .dt_id = TEGRA114_CLK_VIMCLK_SYNC },
	{ .con_id = "audio0", .dt_id = TEGRA114_CLK_AUDIO0 },
	{ .con_id = "audio1", .dt_id = TEGRA114_CLK_AUDIO1 },
	{ .con_id = "audio2", .dt_id = TEGRA114_CLK_AUDIO2 },
	{ .con_id = "audio3", .dt_id = TEGRA114_CLK_AUDIO3 },
	{ .con_id = "audio4", .dt_id = TEGRA114_CLK_AUDIO4 },
	{ .con_id = "spdif", .dt_id = TEGRA114_CLK_SPDIF },
	{ .con_id = "audio0_2x", .dt_id = TEGRA114_CLK_AUDIO0_2X },
	{ .con_id = "audio1_2x", .dt_id = TEGRA114_CLK_AUDIO1_2X },
	{ .con_id = "audio2_2x", .dt_id = TEGRA114_CLK_AUDIO2_2X },
	{ .con_id = "audio3_2x", .dt_id = TEGRA114_CLK_AUDIO3_2X },
	{ .con_id = "audio4_2x", .dt_id = TEGRA114_CLK_AUDIO4_2X },
	{ .con_id = "spdif_2x", .dt_id = TEGRA114_CLK_SPDIF_2X },
	{ .con_id = "extern1", .dev_id = "clk_out_1", .dt_id = TEGRA114_CLK_EXTERN1 },
	{ .con_id = "extern2", .dev_id = "clk_out_2", .dt_id = TEGRA114_CLK_EXTERN2 },
	{ .con_id = "extern3", .dev_id = "clk_out_3", .dt_id = TEGRA114_CLK_EXTERN3 },
	{ .con_id = "blink", .dt_id = TEGRA114_CLK_BLINK },
	{ .con_id = "cclk_g", .dt_id = TEGRA114_CLK_CCLK_G },
	{ .con_id = "cclk_lp", .dt_id = TEGRA114_CLK_CCLK_LP },
	{ .con_id = "sclk", .dt_id = TEGRA114_CLK_SCLK },
	{ .con_id = "hclk", .dt_id = TEGRA114_CLK_HCLK },
	{ .con_id = "pclk", .dt_id = TEGRA114_CLK_PCLK },
	{ .con_id = "fuse", .dt_id = TEGRA114_CLK_FUSE },
	{ .dev_id = "rtc-tegra", .dt_id = TEGRA114_CLK_RTC },
	{ .dev_id = "timer", .dt_id = TEGRA114_CLK_TIMER },
};

static const char *mux_pllm_pllc2_c_c3_pllp_plla[] = {
	"pll_m", "pll_c2", "pll_c", "pll_c3", "pll_p", "pll_a_out0"
};
static u32 mux_pllm_pllc2_c_c3_pllp_plla_idx[] = {
	[0] = 0, [1] = 1, [2] = 2, [3] = 3, [4] = 4, [5] = 6,
};

static struct tegra_audio_clk_info tegra114_audio_plls[] = {
	{ "pll_a", &pll_a_params, tegra_clk_pll_a, "pll_p_out1" },
};

static struct clk **clks;

static unsigned long osc_freq;
static unsigned long pll_ref_freq;

static void __init tegra114_fixed_clk_init(void __iomem *clk_base)
{
	struct clk *clk;

	/* clk_32k */
	clk = clk_register_fixed_rate(NULL, "clk_32k", NULL, 0, 32768);
	clks[TEGRA114_CLK_CLK_32K] = clk;

	/* clk_m_div2 */
	clk = clk_register_fixed_factor(NULL, "clk_m_div2", "clk_m",
					CLK_SET_RATE_PARENT, 1, 2);
	clks[TEGRA114_CLK_CLK_M_DIV2] = clk;

	/* clk_m_div4 */
	clk = clk_register_fixed_factor(NULL, "clk_m_div4", "clk_m",
					CLK_SET_RATE_PARENT, 1, 4);
	clks[TEGRA114_CLK_CLK_M_DIV4] = clk;

}

static __init void tegra114_utmi_param_configure(void __iomem *clk_base)
{
	unsigned int i;
	u32 reg;

	for (i = 0; i < ARRAY_SIZE(utmi_parameters); i++) {
		if (osc_freq == utmi_parameters[i].osc_frequency)
			break;
	}

	if (i >= ARRAY_SIZE(utmi_parameters)) {
		pr_err("%s: Unexpected oscillator freq %lu\n", __func__,
		       osc_freq);
		return;
	}

	reg = readl_relaxed(clk_base + UTMIP_PLL_CFG2);

	/* Program UTMIP PLL stable and active counts */
	/* [FIXME] arclk_rst.h says WRONG! This should be 1ms -> 0x50 Check! */
	reg &= ~UTMIP_PLL_CFG2_STABLE_COUNT(~0);
	reg |= UTMIP_PLL_CFG2_STABLE_COUNT(utmi_parameters[i].stable_count);

	reg &= ~UTMIP_PLL_CFG2_ACTIVE_DLY_COUNT(~0);

	reg |= UTMIP_PLL_CFG2_ACTIVE_DLY_COUNT(utmi_parameters[i].
					    active_delay_count);

	/* Remove power downs from UTMIP PLL control bits */
	reg &= ~UTMIP_PLL_CFG2_FORCE_PD_SAMP_A_POWERDOWN;
	reg &= ~UTMIP_PLL_CFG2_FORCE_PD_SAMP_B_POWERDOWN;
	reg &= ~UTMIP_PLL_CFG2_FORCE_PD_SAMP_C_POWERDOWN;

	writel_relaxed(reg, clk_base + UTMIP_PLL_CFG2);

	/* Program UTMIP PLL delay and oscillator frequency counts */
	reg = readl_relaxed(clk_base + UTMIP_PLL_CFG1);
	reg &= ~UTMIP_PLL_CFG1_ENABLE_DLY_COUNT(~0);

	reg |= UTMIP_PLL_CFG1_ENABLE_DLY_COUNT(utmi_parameters[i].
					    enable_delay_count);

	reg &= ~UTMIP_PLL_CFG1_XTAL_FREQ_COUNT(~0);
	reg |= UTMIP_PLL_CFG1_XTAL_FREQ_COUNT(utmi_parameters[i].
					   xtal_freq_count);

	/* Remove power downs from UTMIP PLL control bits */
	reg &= ~UTMIP_PLL_CFG1_FORCE_PLL_ENABLE_POWERDOWN;
	reg &= ~UTMIP_PLL_CFG1_FORCE_PLL_ACTIVE_POWERDOWN;
	reg &= ~UTMIP_PLL_CFG1_FORCE_PLLU_POWERUP;
	reg &= ~UTMIP_PLL_CFG1_FORCE_PLLU_POWERDOWN;
	writel_relaxed(reg, clk_base + UTMIP_PLL_CFG1);

	/* Setup HW control of UTMIPLL */
	reg = readl_relaxed(clk_base + UTMIPLL_HW_PWRDN_CFG0);
	reg |= UTMIPLL_HW_PWRDN_CFG0_USE_LOCKDET;
	reg &= ~UTMIPLL_HW_PWRDN_CFG0_CLK_ENABLE_SWCTL;
	reg |= UTMIPLL_HW_PWRDN_CFG0_SEQ_START_STATE;
	writel_relaxed(reg, clk_base + UTMIPLL_HW_PWRDN_CFG0);

	reg = readl_relaxed(clk_base + UTMIP_PLL_CFG1);
	reg &= ~UTMIP_PLL_CFG1_FORCE_PLL_ENABLE_POWERUP;
	reg &= ~UTMIP_PLL_CFG1_FORCE_PLL_ENABLE_POWERDOWN;
	writel_relaxed(reg, clk_base + UTMIP_PLL_CFG1);

	udelay(1);

	/* Setup SW override of UTMIPLL assuming USB2.0
	   ports are assigned to USB2 */
	reg = readl_relaxed(clk_base + UTMIPLL_HW_PWRDN_CFG0);
	reg |= UTMIPLL_HW_PWRDN_CFG0_IDDQ_SWCTL;
	reg &= ~UTMIPLL_HW_PWRDN_CFG0_IDDQ_OVERRIDE;
	writel_relaxed(reg, clk_base + UTMIPLL_HW_PWRDN_CFG0);

	udelay(1);

	/* Enable HW control UTMIPLL */
	reg = readl_relaxed(clk_base + UTMIPLL_HW_PWRDN_CFG0);
	reg |= UTMIPLL_HW_PWRDN_CFG0_SEQ_ENABLE;
	writel_relaxed(reg, clk_base + UTMIPLL_HW_PWRDN_CFG0);
}

static void __init tegra114_pll_init(void __iomem *clk_base,
				     void __iomem *pmc)
{
	u32 val;
	struct clk *clk;

	/* PLLC */
	clk = tegra_clk_register_pllxc("pll_c", "pll_ref", clk_base,
			pmc, 0, &pll_c_params, NULL);
	clks[TEGRA114_CLK_PLL_C] = clk;

	/* PLLC_OUT1 */
	clk = tegra_clk_register_divider("pll_c_out1_div", "pll_c",
			clk_base + PLLC_OUT, 0, TEGRA_DIVIDER_ROUND_UP,
			8, 8, 1, NULL);
	clk = tegra_clk_register_pll_out("pll_c_out1", "pll_c_out1_div",
				clk_base + PLLC_OUT, 1, 0,
				CLK_SET_RATE_PARENT, 0, NULL);
	clks[TEGRA114_CLK_PLL_C_OUT1] = clk;

	/* PLLC2 */
	clk = tegra_clk_register_pllc("pll_c2", "pll_ref", clk_base, pmc, 0,
			     &pll_c2_params, NULL);
	clks[TEGRA114_CLK_PLL_C2] = clk;

	/* PLLC3 */
	clk = tegra_clk_register_pllc("pll_c3", "pll_ref", clk_base, pmc, 0,
			     &pll_c3_params, NULL);
	clks[TEGRA114_CLK_PLL_C3] = clk;

	/* PLLM */
	clk = tegra_clk_register_pllm("pll_m", "pll_ref", clk_base, pmc,
			     CLK_IGNORE_UNUSED | CLK_SET_RATE_GATE,
			     &pll_m_params, NULL);
	clks[TEGRA114_CLK_PLL_M] = clk;

	/* PLLM_OUT1 */
	clk = tegra_clk_register_divider("pll_m_out1_div", "pll_m",
				clk_base + PLLM_OUT, 0, TEGRA_DIVIDER_ROUND_UP,
				8, 8, 1, NULL);
	clk = tegra_clk_register_pll_out("pll_m_out1", "pll_m_out1_div",
				clk_base + PLLM_OUT, 1, 0, CLK_IGNORE_UNUSED |
				CLK_SET_RATE_PARENT, 0, NULL);
	clks[TEGRA114_CLK_PLL_M_OUT1] = clk;

	/* PLLM_UD */
	clk = clk_register_fixed_factor(NULL, "pll_m_ud", "pll_m",
					CLK_SET_RATE_PARENT, 1, 1);

	/* PLLU */
	val = readl(clk_base + pll_u_params.base_reg);
	val &= ~BIT(24); /* disable PLLU_OVERRIDE */
	writel(val, clk_base + pll_u_params.base_reg);

	clk = tegra_clk_register_pll("pll_u", "pll_ref", clk_base, pmc, 0,
			    &pll_u_params, &pll_u_lock);
	clks[TEGRA114_CLK_PLL_U] = clk;

	tegra114_utmi_param_configure(clk_base);

	/* PLLU_480M */
	clk = clk_register_gate(NULL, "pll_u_480M", "pll_u",
				CLK_SET_RATE_PARENT, clk_base + PLLU_BASE,
				22, 0, &pll_u_lock);
	clks[TEGRA114_CLK_PLL_U_480M] = clk;

	/* PLLU_60M */
	clk = clk_register_fixed_factor(NULL, "pll_u_60M", "pll_u",
					CLK_SET_RATE_PARENT, 1, 8);
	clks[TEGRA114_CLK_PLL_U_60M] = clk;

	/* PLLU_48M */
	clk = clk_register_fixed_factor(NULL, "pll_u_48M", "pll_u",
					CLK_SET_RATE_PARENT, 1, 10);
	clks[TEGRA114_CLK_PLL_U_48M] = clk;

	/* PLLU_12M */
	clk = clk_register_fixed_factor(NULL, "pll_u_12M", "pll_u",
					CLK_SET_RATE_PARENT, 1, 40);
	clks[TEGRA114_CLK_PLL_U_12M] = clk;

	/* PLLD */
	clk = tegra_clk_register_pll("pll_d", "pll_ref", clk_base, pmc, 0,
			    &pll_d_params, &pll_d_lock);
	clks[TEGRA114_CLK_PLL_D] = clk;

	/* PLLD_OUT0 */
	clk = clk_register_fixed_factor(NULL, "pll_d_out0", "pll_d",
					CLK_SET_RATE_PARENT, 1, 2);
	clks[TEGRA114_CLK_PLL_D_OUT0] = clk;

	/* PLLD2 */
	clk = tegra_clk_register_pll("pll_d2", "pll_ref", clk_base, pmc, 0,
			    &pll_d2_params, &pll_d2_lock);
	clks[TEGRA114_CLK_PLL_D2] = clk;

	/* PLLD2_OUT0 */
	clk = clk_register_fixed_factor(NULL, "pll_d2_out0", "pll_d2",
					CLK_SET_RATE_PARENT, 1, 2);
	clks[TEGRA114_CLK_PLL_D2_OUT0] = clk;

	/* PLLRE */
	clk = tegra_clk_register_pllre("pll_re_vco", "pll_ref", clk_base, pmc,
			     0, &pll_re_vco_params, &pll_re_lock, pll_ref_freq);
	clks[TEGRA114_CLK_PLL_RE_VCO] = clk;

	clk = clk_register_divider_table(NULL, "pll_re_out", "pll_re_vco", 0,
					 clk_base + PLLRE_BASE, 16, 4, 0,
					 pll_re_div_table, &pll_re_lock);
	clks[TEGRA114_CLK_PLL_RE_OUT] = clk;

	/* PLLE */
	clk = tegra_clk_register_plle_tegra114("pll_e_out0", "pll_ref",
				      clk_base, 0, &pll_e_params, NULL);
	clks[TEGRA114_CLK_PLL_E_OUT0] = clk;
}

#define CLK_SOURCE_VI_SENSOR 0x1a8

static struct tegra_periph_init_data tegra_periph_clk_list[] = {
	MUX8("vi_sensor", mux_pllm_pllc2_c_c3_pllp_plla, CLK_SOURCE_VI_SENSOR, 20, TEGRA_PERIPH_NO_RESET, TEGRA114_CLK_VI_SENSOR),
};

static __init void tegra114_periph_clk_init(void __iomem *clk_base,
					    void __iomem *pmc_base)
{
	struct clk *clk;
	struct tegra_periph_init_data *data;
	unsigned int i;

	/* xusb_ss_div2 */
	clk = clk_register_fixed_factor(NULL, "xusb_ss_div2", "xusb_ss_src", 0,
					1, 2);
	clks[TEGRA114_CLK_XUSB_SS_DIV2] = clk;

	/* dsia mux */
	clk = clk_register_mux(NULL, "dsia_mux", mux_plld_out0_plld2_out0,
			       ARRAY_SIZE(mux_plld_out0_plld2_out0),
			       CLK_SET_RATE_NO_REPARENT,
			       clk_base + PLLD_BASE, 25, 1, 0, &pll_d_lock);
	clks[TEGRA114_CLK_DSIA_MUX] = clk;

	/* dsib mux */
	clk = clk_register_mux(NULL, "dsib_mux", mux_plld_out0_plld2_out0,
			       ARRAY_SIZE(mux_plld_out0_plld2_out0),
			       CLK_SET_RATE_NO_REPARENT,
			       clk_base + PLLD2_BASE, 25, 1, 0, &pll_d2_lock);
	clks[TEGRA114_CLK_DSIB_MUX] = clk;

	clk = tegra_clk_register_periph_gate("dsia", "dsia_mux", 0, clk_base,
					     0, 48, periph_clk_enb_refcnt);
	clks[TEGRA114_CLK_DSIA] = clk;

	clk = tegra_clk_register_periph_gate("dsib", "dsib_mux", 0, clk_base,
					     0, 82, periph_clk_enb_refcnt);
	clks[TEGRA114_CLK_DSIB] = clk;

	/* emc mux */
	clk = clk_register_mux(NULL, "emc_mux", mux_pllmcp_clkm,
			       ARRAY_SIZE(mux_pllmcp_clkm),
			       CLK_SET_RATE_NO_REPARENT,
			       clk_base + CLK_SOURCE_EMC,
			       29, 3, 0, &emc_lock);

	clk = tegra_clk_register_mc("mc", "emc_mux", clk_base + CLK_SOURCE_EMC,
				    &emc_lock);
	clks[TEGRA114_CLK_MC] = clk;

	for (i = 0; i < ARRAY_SIZE(tegra_periph_clk_list); i++) {
		data = &tegra_periph_clk_list[i];
		clk = tegra_clk_register_periph(data->name,
			data->p.parent_names, data->num_parents,
			&data->periph, clk_base, data->offset, data->flags);
		clks[data->clk_id] = clk;
	}

	tegra_periph_clk_init(clk_base, pmc_base, tegra114_clks,
				&pll_p_params);
}

/* Tegra114 CPU clock and reset control functions */
static void tegra114_wait_cpu_in_reset(u32 cpu)
{
	unsigned int reg;

	do {
		reg = readl(clk_base + CLK_RST_CONTROLLER_CPU_CMPLX_STATUS);
		cpu_relax();
	} while (!(reg & (1 << cpu)));  /* check CPU been reset or not */
}

static void tegra114_disable_cpu_clock(u32 cpu)
{
	/* flow controller would take care in the power sequence. */
}

#ifdef CONFIG_PM_SLEEP
static void tegra114_cpu_clock_suspend(void)
{
	/* switch coresite to clk_m, save off original source */
	tegra114_cpu_clk_sctx.clk_csite_src =
				readl(clk_base + CLK_SOURCE_CSITE);
	writel(3 << 30, clk_base + CLK_SOURCE_CSITE);

	tegra114_cpu_clk_sctx.cclkg_burst =
				readl(clk_base + CCLKG_BURST_POLICY);
	tegra114_cpu_clk_sctx.cclkg_divider =
				readl(clk_base + CCLKG_BURST_POLICY + 4);
}

static void tegra114_cpu_clock_resume(void)
{
	writel(tegra114_cpu_clk_sctx.clk_csite_src,
					clk_base + CLK_SOURCE_CSITE);

	writel(tegra114_cpu_clk_sctx.cclkg_burst,
					clk_base + CCLKG_BURST_POLICY);
	writel(tegra114_cpu_clk_sctx.cclkg_divider,
					clk_base + CCLKG_BURST_POLICY + 4);
}
#endif

static struct tegra_cpu_car_ops tegra114_cpu_car_ops = {
	.wait_for_reset	= tegra114_wait_cpu_in_reset,
	.disable_clock	= tegra114_disable_cpu_clock,
#ifdef CONFIG_PM_SLEEP
	.suspend	= tegra114_cpu_clock_suspend,
	.resume		= tegra114_cpu_clock_resume,
#endif
};

static const struct of_device_id pmc_match[] __initconst = {
	{ .compatible = "nvidia,tegra114-pmc" },
	{ },
};

/*
 * dfll_soc/dfll_ref apparently must be kept enabled, otherwise I2C5
 * breaks
 */
static struct tegra_clk_init_table init_table[] __initdata = {
	{ TEGRA114_CLK_UARTA, TEGRA114_CLK_PLL_P, 408000000, 0 },
	{ TEGRA114_CLK_UARTB, TEGRA114_CLK_PLL_P, 408000000, 0 },
	{ TEGRA114_CLK_UARTC, TEGRA114_CLK_PLL_P, 408000000, 0 },
	{ TEGRA114_CLK_UARTD, TEGRA114_CLK_PLL_P, 408000000, 0 },
	{ TEGRA114_CLK_PLL_A, TEGRA114_CLK_CLK_MAX, 564480000, 1 },
	{ TEGRA114_CLK_PLL_A_OUT0, TEGRA114_CLK_CLK_MAX, 11289600, 1 },
	{ TEGRA114_CLK_EXTERN1, TEGRA114_CLK_PLL_A_OUT0, 0, 1 },
	{ TEGRA114_CLK_CLK_OUT_1_MUX, TEGRA114_CLK_EXTERN1, 0, 1 },
	{ TEGRA114_CLK_CLK_OUT_1, TEGRA114_CLK_CLK_MAX, 0, 1 },
	{ TEGRA114_CLK_I2S0, TEGRA114_CLK_PLL_A_OUT0, 11289600, 0 },
	{ TEGRA114_CLK_I2S1, TEGRA114_CLK_PLL_A_OUT0, 11289600, 0 },
	{ TEGRA114_CLK_I2S2, TEGRA114_CLK_PLL_A_OUT0, 11289600, 0 },
	{ TEGRA114_CLK_I2S3, TEGRA114_CLK_PLL_A_OUT0, 11289600, 0 },
	{ TEGRA114_CLK_I2S4, TEGRA114_CLK_PLL_A_OUT0, 11289600, 0 },
	{ TEGRA114_CLK_HOST1X, TEGRA114_CLK_PLL_P, 136000000, 0 },
	{ TEGRA114_CLK_DFLL_SOC, TEGRA114_CLK_PLL_P, 51000000, 1 },
	{ TEGRA114_CLK_DFLL_REF, TEGRA114_CLK_PLL_P, 51000000, 1 },
	{ TEGRA114_CLK_DISP1, TEGRA114_CLK_PLL_P, 0, 0 },
	{ TEGRA114_CLK_DISP2, TEGRA114_CLK_PLL_P, 0, 0 },
	{ TEGRA114_CLK_GR2D, TEGRA114_CLK_PLL_C2, 300000000, 0 },
	{ TEGRA114_CLK_GR3D, TEGRA114_CLK_PLL_C2, 300000000, 0 },
	{ TEGRA114_CLK_DSIALP, TEGRA114_CLK_PLL_P, 68000000, 0 },
	{ TEGRA114_CLK_DSIBLP, TEGRA114_CLK_PLL_P, 68000000, 0 },
	{ TEGRA114_CLK_PLL_RE_VCO, TEGRA114_CLK_CLK_MAX, 612000000, 0 },
	{ TEGRA114_CLK_XUSB_SS_SRC, TEGRA114_CLK_PLL_RE_OUT, 122400000, 0 },
	{ TEGRA114_CLK_XUSB_FS_SRC, TEGRA114_CLK_PLL_U_48M, 48000000, 0 },
	{ TEGRA114_CLK_XUSB_HS_SRC, TEGRA114_CLK_XUSB_SS_DIV2, 61200000, 0 },
	{ TEGRA114_CLK_XUSB_FALCON_SRC, TEGRA114_CLK_PLL_P, 204000000, 0 },
	{ TEGRA114_CLK_XUSB_HOST_SRC, TEGRA114_CLK_PLL_P, 102000000, 0 },
	/* must be the last entry */
	{ TEGRA114_CLK_CLK_MAX, TEGRA114_CLK_CLK_MAX, 0, 0 },
};

static void __init tegra114_clock_apply_init_table(void)
{
	tegra_init_from_table(init_table, clks, TEGRA114_CLK_CLK_MAX);
}

/**
 * tegra114_car_barrier - wait for pending writes to the CAR to complete
 *
 * Wait for any outstanding writes to the CAR MMIO space from this CPU
 * to complete before continuing execution.  No return value.
 */
static void tegra114_car_barrier(void)
{
	wmb();		/* probably unnecessary */
	readl_relaxed(clk_base + CPU_FINETRIM_SELECT);
}

/**
 * tegra114_clock_tune_cpu_trimmers_high - use high-voltage propagation delays
 *
 * When the CPU rail voltage is in the high-voltage range, use the
 * built-in hardwired clock propagation delays in the CPU clock
 * shaper.  No return value.
 */
void tegra114_clock_tune_cpu_trimmers_high(void)
{
	u32 select = 0;

	/* Use hardwired rise->rise & fall->fall clock propagation delays */
	select |= ~(CPU_FINETRIM_1_FCPU_1 | CPU_FINETRIM_1_FCPU_2 |
		    CPU_FINETRIM_1_FCPU_3 | CPU_FINETRIM_1_FCPU_4 |
		    CPU_FINETRIM_1_FCPU_5 | CPU_FINETRIM_1_FCPU_6);
	writel_relaxed(select, clk_base + CPU_FINETRIM_SELECT);

	tegra114_car_barrier();
}
EXPORT_SYMBOL(tegra114_clock_tune_cpu_trimmers_high);

/**
 * tegra114_clock_tune_cpu_trimmers_low - use low-voltage propagation delays
 *
 * When the CPU rail voltage is in the low-voltage range, use the
 * extended clock propagation delays set by
 * tegra114_clock_tune_cpu_trimmers_init().  The intention is to
 * maintain the input clock duty cycle that the FCPU subsystem
 * expects.  No return value.
 */
void tegra114_clock_tune_cpu_trimmers_low(void)
{
	u32 select = 0;

	/*
	 * Use software-specified rise->rise & fall->fall clock
	 * propagation delays (from
	 * tegra114_clock_tune_cpu_trimmers_init()
	 */
	select |= (CPU_FINETRIM_1_FCPU_1 | CPU_FINETRIM_1_FCPU_2 |
		   CPU_FINETRIM_1_FCPU_3 | CPU_FINETRIM_1_FCPU_4 |
		   CPU_FINETRIM_1_FCPU_5 | CPU_FINETRIM_1_FCPU_6);
	writel_relaxed(select, clk_base + CPU_FINETRIM_SELECT);

	tegra114_car_barrier();
}
EXPORT_SYMBOL(tegra114_clock_tune_cpu_trimmers_low);

/**
 * tegra114_clock_tune_cpu_trimmers_init - set up and enable clk prop delays
 *
 * Program extended clock propagation delays into the FCPU clock
 * shaper and enable them.  XXX Define the purpose - peak current
 * reduction?  No return value.
 */
/* XXX Initial voltage rail state assumption issues? */
void tegra114_clock_tune_cpu_trimmers_init(void)
{
	u32 dr = 0, r = 0;

	/* Increment the rise->rise clock delay by four steps */
	r |= (CPU_FINETRIM_R_FCPU_1_MASK | CPU_FINETRIM_R_FCPU_2_MASK |
	      CPU_FINETRIM_R_FCPU_3_MASK | CPU_FINETRIM_R_FCPU_4_MASK |
	      CPU_FINETRIM_R_FCPU_5_MASK | CPU_FINETRIM_R_FCPU_6_MASK);
	writel_relaxed(r, clk_base + CPU_FINETRIM_R);

	/*
	 * Use the rise->rise clock propagation delay specified in the
	 * r field
	 */
	dr |= (CPU_FINETRIM_1_FCPU_1 | CPU_FINETRIM_1_FCPU_2 |
	       CPU_FINETRIM_1_FCPU_3 | CPU_FINETRIM_1_FCPU_4 |
	       CPU_FINETRIM_1_FCPU_5 | CPU_FINETRIM_1_FCPU_6);
	writel_relaxed(dr, clk_base + CPU_FINETRIM_DR);

	tegra114_clock_tune_cpu_trimmers_low();
}
EXPORT_SYMBOL(tegra114_clock_tune_cpu_trimmers_init);

/**
 * tegra114_clock_assert_dfll_dvco_reset - assert the DFLL's DVCO reset
 *
 * Assert the reset line of the DFLL's DVCO.  No return value.
 */
void tegra114_clock_assert_dfll_dvco_reset(void)
{
	u32 v;

	v = readl_relaxed(clk_base + RST_DFLL_DVCO);
	v |= (1 << DVFS_DFLL_RESET_SHIFT);
	writel_relaxed(v, clk_base + RST_DFLL_DVCO);
	tegra114_car_barrier();
}
EXPORT_SYMBOL(tegra114_clock_assert_dfll_dvco_reset);

/**
 * tegra114_clock_deassert_dfll_dvco_reset - deassert the DFLL's DVCO reset
 *
 * Deassert the reset line of the DFLL's DVCO, allowing the DVCO to
 * operate.  No return value.
 */
void tegra114_clock_deassert_dfll_dvco_reset(void)
{
	u32 v;

	v = readl_relaxed(clk_base + RST_DFLL_DVCO);
	v &= ~(1 << DVFS_DFLL_RESET_SHIFT);
	writel_relaxed(v, clk_base + RST_DFLL_DVCO);
	tegra114_car_barrier();
}
EXPORT_SYMBOL(tegra114_clock_deassert_dfll_dvco_reset);

static void __init tegra114_clock_init(struct device_node *np)
{
	struct device_node *node;

	clk_base = of_iomap(np, 0);
	if (!clk_base) {
		pr_err("ioremap tegra114 CAR failed\n");
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

	clks = tegra_clk_init(clk_base, TEGRA114_CLK_CLK_MAX,
				TEGRA114_CLK_PERIPH_BANKS);
	if (!clks)
		return;

	if (tegra_osc_clk_init(clk_base, tegra114_clks, tegra114_input_freq,
			       ARRAY_SIZE(tegra114_input_freq), 1, &osc_freq,
			       &pll_ref_freq) < 0)
		return;

	tegra114_fixed_clk_init(clk_base);
	tegra114_pll_init(clk_base, pmc_base);
	tegra114_periph_clk_init(clk_base, pmc_base);
	tegra_audio_clk_init(clk_base, pmc_base, tegra114_clks,
			     tegra114_audio_plls,
			     ARRAY_SIZE(tegra114_audio_plls));
	tegra_pmc_clk_init(pmc_base, tegra114_clks);
	tegra_super_clk_gen4_init(clk_base, pmc_base, tegra114_clks,
					&pll_x_params);

	tegra_add_of_provider(np);
	tegra_register_devclks(devclks, ARRAY_SIZE(devclks));

	tegra_clk_apply_init_table = tegra114_clock_apply_init_table;

	tegra_cpu_car_ops = &tegra114_cpu_car_ops;
}
CLK_OF_DECLARE(tegra114, "nvidia,tegra114-car", tegra114_clock_init);
