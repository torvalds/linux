/*
 * arch/arm/mach-tegra/tegra30_clocks.c
 *
 * Copyright (c) 2010-2012 NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include <linux/clk-private.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/cpufreq.h>

#include "clock.h"
#include "fuse.h"
#include "tegra30_clocks.h"
#include "tegra_cpu_car.h"

#define DEFINE_CLK_TEGRA(_name, _rate, _ops, _flags,		\
		   _parent_names, _parents, _parent)		\
	static struct clk tegra_##_name = {			\
		.hw = &tegra_##_name##_hw.hw,			\
		.name = #_name,					\
		.rate = _rate,					\
		.ops = _ops,					\
		.flags = _flags,				\
		.parent_names = _parent_names,			\
		.parents = _parents,				\
		.num_parents = ARRAY_SIZE(_parent_names),	\
		.parent	= _parent,				\
	};

static struct clk tegra_clk_32k;
static struct clk_tegra tegra_clk_32k_hw = {
	.hw = {
		.clk = &tegra_clk_32k,
	},
	.fixed_rate = 32768,
};
static struct clk tegra_clk_32k = {
	.name = "clk_32k",
	.hw = &tegra_clk_32k_hw.hw,
	.ops = &tegra30_clk_32k_ops,
	.flags = CLK_IS_ROOT,
};

static struct clk tegra_clk_m;
static struct clk_tegra tegra_clk_m_hw = {
	.hw = {
		.clk = &tegra_clk_m,
	},
	.flags = ENABLE_ON_INIT,
	.reg = 0x1fc,
	.reg_shift = 28,
	.max_rate = 48000000,
};
static struct clk tegra_clk_m = {
	.name = "clk_m",
	.hw = &tegra_clk_m_hw.hw,
	.ops = &tegra30_clk_m_ops,
	.flags = CLK_IS_ROOT | CLK_IGNORE_UNUSED,
};

static const char *clk_m_div_parent_names[] = {
	"clk_m",
};

static struct clk *clk_m_div_parents[] = {
	&tegra_clk_m,
};

static struct clk tegra_clk_m_div2;
static struct clk_tegra tegra_clk_m_div2_hw = {
	.hw = {
		.clk = &tegra_clk_m_div2,
	},
	.mul = 1,
	.div = 2,
	.max_rate = 24000000,
};
DEFINE_CLK_TEGRA(clk_m_div2, 0, &tegra_clk_m_div_ops, 0,
		clk_m_div_parent_names, clk_m_div_parents, &tegra_clk_m);

static struct clk tegra_clk_m_div4;
static struct clk_tegra tegra_clk_m_div4_hw = {
	.hw = {
		.clk = &tegra_clk_m_div4,
	},
	.mul = 1,
	.div = 4,
	.max_rate = 12000000,
};
DEFINE_CLK_TEGRA(clk_m_div4, 0, &tegra_clk_m_div_ops, 0,
		clk_m_div_parent_names, clk_m_div_parents, &tegra_clk_m);

static struct clk tegra_pll_ref;
static struct clk_tegra tegra_pll_ref_hw = {
	.hw = {
		.clk = &tegra_pll_ref,
	},
	.flags = ENABLE_ON_INIT,
	.max_rate = 26000000,
};
DEFINE_CLK_TEGRA(pll_ref, 0, &tegra_pll_ref_ops, 0, clk_m_div_parent_names,
		clk_m_div_parents, &tegra_clk_m);

#define DEFINE_PLL(_name, _flags, _reg, _max_rate, _input_min,	\
		   _input_max, _cf_min, _cf_max, _vco_min,	\
		   _vco_max, _freq_table, _lock_delay, _ops,	\
		   _fixed_rate, _clk_cfg_ex, _parent)		\
	static struct clk tegra_##_name;			\
	static const char *_name##_parent_names[] = {		\
		#_parent,					\
	};							\
	static struct clk *_name##_parents[] = {		\
		&tegra_##_parent,				\
	};							\
	static struct clk_tegra tegra_##_name##_hw = {		\
		.hw = {						\
			.clk = &tegra_##_name,			\
		},						\
		.flags = _flags,				\
		.reg = _reg,					\
		.max_rate = _max_rate,				\
		.u.pll = {					\
			.input_min = _input_min,		\
			.input_max = _input_max,		\
			.cf_min = _cf_min,			\
			.cf_max = _cf_max,			\
			.vco_min = _vco_min,			\
			.vco_max = _vco_max,			\
			.freq_table = _freq_table,		\
			.lock_delay = _lock_delay,		\
			.fixed_rate = _fixed_rate,		\
		},						\
		.clk_cfg_ex = _clk_cfg_ex,			\
	};							\
	DEFINE_CLK_TEGRA(_name, 0, &_ops, CLK_IGNORE_UNUSED,	\
			 _name##_parent_names, _name##_parents,	\
			&tegra_##_parent);

#define DEFINE_PLL_OUT(_name, _flags, _reg, _reg_shift,		\
		_max_rate, _ops, _parent, _clk_flags)		\
	static const char *_name##_parent_names[] = {		\
		#_parent,					\
	};							\
	static struct clk *_name##_parents[] = {		\
		&tegra_##_parent,				\
	};							\
	static struct clk tegra_##_name;			\
	static struct clk_tegra tegra_##_name##_hw = {		\
		.hw = {						\
			.clk = &tegra_##_name,			\
		},						\
		.flags = _flags,				\
		.reg = _reg,					\
		.max_rate = _max_rate,				\
		.reg_shift = _reg_shift,			\
	};							\
	DEFINE_CLK_TEGRA(_name, 0, &tegra30_pll_div_ops,	\
		_clk_flags,  _name##_parent_names,		\
		_name##_parents, &tegra_##_parent);

static struct clk_pll_freq_table tegra_pll_c_freq_table[] = {
	{ 12000000, 1040000000, 520,  6, 1, 8},
	{ 13000000, 1040000000, 480,  6, 1, 8},
	{ 16800000, 1040000000, 495,  8, 1, 8},	/* actual: 1039.5 MHz */
	{ 19200000, 1040000000, 325,  6, 1, 6},
	{ 26000000, 1040000000, 520, 13, 1, 8},

	{ 12000000, 832000000, 416,  6, 1, 8},
	{ 13000000, 832000000, 832, 13, 1, 8},
	{ 16800000, 832000000, 396,  8, 1, 8},	/* actual: 831.6 MHz */
	{ 19200000, 832000000, 260,  6, 1, 8},
	{ 26000000, 832000000, 416, 13, 1, 8},

	{ 12000000, 624000000, 624, 12, 1, 8},
	{ 13000000, 624000000, 624, 13, 1, 8},
	{ 16800000, 600000000, 520, 14, 1, 8},
	{ 19200000, 624000000, 520, 16, 1, 8},
	{ 26000000, 624000000, 624, 26, 1, 8},

	{ 12000000, 600000000, 600, 12, 1, 8},
	{ 13000000, 600000000, 600, 13, 1, 8},
	{ 16800000, 600000000, 500, 14, 1, 8},
	{ 19200000, 600000000, 375, 12, 1, 6},
	{ 26000000, 600000000, 600, 26, 1, 8},

	{ 12000000, 520000000, 520, 12, 1, 8},
	{ 13000000, 520000000, 520, 13, 1, 8},
	{ 16800000, 520000000, 495, 16, 1, 8},	/* actual: 519.75 MHz */
	{ 19200000, 520000000, 325, 12, 1, 6},
	{ 26000000, 520000000, 520, 26, 1, 8},

	{ 12000000, 416000000, 416, 12, 1, 8},
	{ 13000000, 416000000, 416, 13, 1, 8},
	{ 16800000, 416000000, 396, 16, 1, 8},	/* actual: 415.8 MHz */
	{ 19200000, 416000000, 260, 12, 1, 6},
	{ 26000000, 416000000, 416, 26, 1, 8},
	{ 0, 0, 0, 0, 0, 0 },
};

DEFINE_PLL(pll_c, PLL_HAS_CPCON, 0x80, 1400000000, 2000000, 31000000, 1000000,
		6000000, 20000000, 1400000000, tegra_pll_c_freq_table, 300,
		tegra30_pll_ops, 0, NULL, pll_ref);

DEFINE_PLL_OUT(pll_c_out1, DIV_U71, 0x84, 0, 700000000,
		tegra30_pll_div_ops, pll_c, CLK_IGNORE_UNUSED);

static struct clk_pll_freq_table tegra_pll_m_freq_table[] = {
	{ 12000000, 666000000, 666, 12, 1, 8},
	{ 13000000, 666000000, 666, 13, 1, 8},
	{ 16800000, 666000000, 555, 14, 1, 8},
	{ 19200000, 666000000, 555, 16, 1, 8},
	{ 26000000, 666000000, 666, 26, 1, 8},
	{ 12000000, 600000000, 600, 12, 1, 8},
	{ 13000000, 600000000, 600, 13, 1, 8},
	{ 16800000, 600000000, 500, 14, 1, 8},
	{ 19200000, 600000000, 375, 12, 1, 6},
	{ 26000000, 600000000, 600, 26, 1, 8},
	{ 0, 0, 0, 0, 0, 0 },
};

DEFINE_PLL(pll_m, PLL_HAS_CPCON | PLLM, 0x90, 800000000, 2000000, 31000000,
		1000000, 6000000, 20000000, 1200000000, tegra_pll_m_freq_table,
		300, tegra30_pll_ops, 0, NULL, pll_ref);

DEFINE_PLL_OUT(pll_m_out1, DIV_U71, 0x94, 0, 600000000,
		tegra30_pll_div_ops, pll_m, CLK_IGNORE_UNUSED);

static struct clk_pll_freq_table tegra_pll_p_freq_table[] = {
	{ 12000000, 216000000, 432, 12, 2, 8},
	{ 13000000, 216000000, 432, 13, 2, 8},
	{ 16800000, 216000000, 360, 14, 2, 8},
	{ 19200000, 216000000, 360, 16, 2, 8},
	{ 26000000, 216000000, 432, 26, 2, 8},
	{ 0, 0, 0, 0, 0, 0 },
};

DEFINE_PLL(pll_p, ENABLE_ON_INIT | PLL_FIXED | PLL_HAS_CPCON, 0xa0, 432000000,
		2000000, 31000000, 1000000, 6000000, 20000000, 1400000000,
		tegra_pll_p_freq_table, 300, tegra30_pll_ops, 408000000, NULL,
		pll_ref);

DEFINE_PLL_OUT(pll_p_out1, ENABLE_ON_INIT | DIV_U71 | DIV_U71_FIXED, 0xa4,
		0, 432000000, tegra30_pll_div_ops, pll_p, CLK_IGNORE_UNUSED);
DEFINE_PLL_OUT(pll_p_out2, ENABLE_ON_INIT | DIV_U71 | DIV_U71_FIXED, 0xa4,
		16, 432000000, tegra30_pll_div_ops, pll_p, CLK_IGNORE_UNUSED);
DEFINE_PLL_OUT(pll_p_out3, ENABLE_ON_INIT | DIV_U71 | DIV_U71_FIXED, 0xa8,
		0, 432000000, tegra30_pll_div_ops, pll_p, CLK_IGNORE_UNUSED);
DEFINE_PLL_OUT(pll_p_out4, ENABLE_ON_INIT | DIV_U71 | DIV_U71_FIXED, 0xa8,
		16, 432000000, tegra30_pll_div_ops, pll_p, CLK_IGNORE_UNUSED);

static struct clk_pll_freq_table tegra_pll_a_freq_table[] = {
	{ 9600000, 564480000, 294, 5, 1, 4},
	{ 9600000, 552960000, 288, 5, 1, 4},
	{ 9600000, 24000000,  5,   2, 1, 1},

	{ 28800000, 56448000, 49, 25, 1, 1},
	{ 28800000, 73728000, 64, 25, 1, 1},
	{ 28800000, 24000000,  5,  6, 1, 1},
	{ 0, 0, 0, 0, 0, 0 },
};

DEFINE_PLL(pll_a, PLL_HAS_CPCON, 0xb0, 700000000, 2000000, 31000000, 1000000,
		6000000, 20000000, 1400000000, tegra_pll_a_freq_table,
		300, tegra30_pll_ops, 0, NULL, pll_p_out1);

DEFINE_PLL_OUT(pll_a_out0, DIV_U71, 0xb4, 0, 100000000, tegra30_pll_div_ops,
		pll_a, CLK_IGNORE_UNUSED);

static struct clk_pll_freq_table tegra_pll_d_freq_table[] = {
	{ 12000000, 216000000, 216, 12, 1, 4},
	{ 13000000, 216000000, 216, 13, 1, 4},
	{ 16800000, 216000000, 180, 14, 1, 4},
	{ 19200000, 216000000, 180, 16, 1, 4},
	{ 26000000, 216000000, 216, 26, 1, 4},

	{ 12000000, 594000000, 594, 12, 1, 8},
	{ 13000000, 594000000, 594, 13, 1, 8},
	{ 16800000, 594000000, 495, 14, 1, 8},
	{ 19200000, 594000000, 495, 16, 1, 8},
	{ 26000000, 594000000, 594, 26, 1, 8},

	{ 12000000, 1000000000, 1000, 12, 1, 12},
	{ 13000000, 1000000000, 1000, 13, 1, 12},
	{ 19200000, 1000000000, 625,  12, 1, 8},
	{ 26000000, 1000000000, 1000, 26, 1, 12},

	{ 0, 0, 0, 0, 0, 0 },
};

DEFINE_PLL(pll_d, PLL_HAS_CPCON | PLLD, 0xd0, 1000000000, 2000000, 40000000,
		1000000, 6000000, 40000000, 1000000000, tegra_pll_d_freq_table,
		1000, tegra30_pll_ops, 0, tegra30_plld_clk_cfg_ex, pll_ref);

DEFINE_PLL_OUT(pll_d_out0, DIV_2 | PLLD, 0, 0, 500000000, tegra30_pll_div_ops,
		pll_d, CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED);

DEFINE_PLL(pll_d2, PLL_HAS_CPCON | PLL_ALT_MISC_REG | PLLD, 0x4b8, 1000000000,
		2000000, 40000000, 1000000, 6000000, 40000000, 1000000000,
		tegra_pll_d_freq_table, 1000, tegra30_pll_ops, 0, NULL,
		pll_ref);

DEFINE_PLL_OUT(pll_d2_out0, DIV_2 | PLLD, 0, 0, 500000000, tegra30_pll_div_ops,
		pll_d2, CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED);

static struct clk_pll_freq_table tegra_pll_u_freq_table[] = {
	{ 12000000, 480000000, 960, 12, 2, 12},
	{ 13000000, 480000000, 960, 13, 2, 12},
	{ 16800000, 480000000, 400, 7,  2, 5},
	{ 19200000, 480000000, 200, 4,  2, 3},
	{ 26000000, 480000000, 960, 26, 2, 12},
	{ 0, 0, 0, 0, 0, 0 },
};

DEFINE_PLL(pll_u, PLL_HAS_CPCON | PLLU, 0xc0, 480000000, 2000000, 40000000,
		1000000, 6000000, 48000000, 960000000, tegra_pll_u_freq_table,
		1000, tegra30_pll_ops, 0, NULL, pll_ref);

static struct clk_pll_freq_table tegra_pll_x_freq_table[] = {
	/* 1.7 GHz */
	{ 12000000, 1700000000, 850,  6,  1, 8},
	{ 13000000, 1700000000, 915,  7,  1, 8},	/* actual: 1699.2 MHz */
	{ 16800000, 1700000000, 708,  7,  1, 8},	/* actual: 1699.2 MHz */
	{ 19200000, 1700000000, 885,  10, 1, 8},	/* actual: 1699.2 MHz */
	{ 26000000, 1700000000, 850,  13, 1, 8},

	/* 1.6 GHz */
	{ 12000000, 1600000000, 800,  6,  1, 8},
	{ 13000000, 1600000000, 738,  6,  1, 8},	/* actual: 1599.0 MHz */
	{ 16800000, 1600000000, 857,  9,  1, 8},	/* actual: 1599.7 MHz */
	{ 19200000, 1600000000, 500,  6,  1, 8},
	{ 26000000, 1600000000, 800,  13, 1, 8},

	/* 1.5 GHz */
	{ 12000000, 1500000000, 750,  6,  1, 8},
	{ 13000000, 1500000000, 923,  8,  1, 8},	/* actual: 1499.8 MHz */
	{ 16800000, 1500000000, 625,  7,  1, 8},
	{ 19200000, 1500000000, 625,  8,  1, 8},
	{ 26000000, 1500000000, 750,  13, 1, 8},

	/* 1.4 GHz */
	{ 12000000, 1400000000, 700,  6,  1, 8},
	{ 13000000, 1400000000, 969,  9,  1, 8},	/* actual: 1399.7 MHz */
	{ 16800000, 1400000000, 1000, 12, 1, 8},
	{ 19200000, 1400000000, 875,  12, 1, 8},
	{ 26000000, 1400000000, 700,  13, 1, 8},

	/* 1.3 GHz */
	{ 12000000, 1300000000, 975,  9,  1, 8},
	{ 13000000, 1300000000, 1000, 10, 1, 8},
	{ 16800000, 1300000000, 928,  12, 1, 8},	/* actual: 1299.2 MHz */
	{ 19200000, 1300000000, 812,  12, 1, 8},	/* actual: 1299.2 MHz */
	{ 26000000, 1300000000, 650,  13, 1, 8},

	/* 1.2 GHz */
	{ 12000000, 1200000000, 1000, 10, 1, 8},
	{ 13000000, 1200000000, 923,  10, 1, 8},	/* actual: 1199.9 MHz */
	{ 16800000, 1200000000, 1000, 14, 1, 8},
	{ 19200000, 1200000000, 1000, 16, 1, 8},
	{ 26000000, 1200000000, 600,  13, 1, 8},

	/* 1.1 GHz */
	{ 12000000, 1100000000, 825,  9,  1, 8},
	{ 13000000, 1100000000, 846,  10, 1, 8},	/* actual: 1099.8 MHz */
	{ 16800000, 1100000000, 982,  15, 1, 8},	/* actual: 1099.8 MHz */
	{ 19200000, 1100000000, 859,  15, 1, 8},	/* actual: 1099.5 MHz */
	{ 26000000, 1100000000, 550,  13, 1, 8},

	/* 1 GHz */
	{ 12000000, 1000000000, 1000, 12, 1, 8},
	{ 13000000, 1000000000, 1000, 13, 1, 8},
	{ 16800000, 1000000000, 833,  14, 1, 8},	/* actual: 999.6 MHz */
	{ 19200000, 1000000000, 625,  12, 1, 8},
	{ 26000000, 1000000000, 1000, 26, 1, 8},

	{ 0, 0, 0, 0, 0, 0 },
};

DEFINE_PLL(pll_x, PLL_HAS_CPCON | PLL_ALT_MISC_REG | PLLX, 0xe0, 1700000000,
		2000000, 31000000, 1000000, 6000000, 20000000, 1700000000,
		tegra_pll_x_freq_table, 300, tegra30_pll_ops, 0, NULL, pll_ref);

DEFINE_PLL_OUT(pll_x_out0, DIV_2 | PLLX, 0, 0, 850000000, tegra30_pll_div_ops,
		pll_x, CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED);

static struct clk_pll_freq_table tegra_pll_e_freq_table[] = {
	/* PLLE special case: use cpcon field to store cml divider value */
	{ 12000000,  100000000, 150, 1,  18, 11},
	{ 216000000, 100000000, 200, 18, 24, 13},
	{ 0, 0, 0, 0, 0, 0 },
};

DEFINE_PLL(pll_e, PLL_ALT_MISC_REG, 0xe8, 100000000, 2000000, 216000000,
		12000000, 12000000, 1200000000, 2400000000U,
		tegra_pll_e_freq_table, 300, tegra30_plle_ops, 100000000, NULL,
		pll_ref);

static const char *mux_plle[] = {
	"pll_e",
};

static struct clk *mux_plle_p[] = {
	&tegra_pll_e,
};

static struct clk tegra_cml0;
static struct clk_tegra tegra_cml0_hw = {
	.hw = {
		.clk = &tegra_cml0,
	},
	.reg = 0x48c,
	.fixed_rate = 100000000,
	.u.periph = {
		.clk_num = 0,
	},
};
DEFINE_CLK_TEGRA(cml0, 0, &tegra_cml_clk_ops, 0, mux_plle,
		mux_plle_p, &tegra_pll_e);

static struct clk tegra_cml1;
static struct clk_tegra tegra_cml1_hw = {
	.hw = {
		.clk = &tegra_cml1,
	},
	.reg = 0x48c,
	.fixed_rate = 100000000,
	.u.periph = {
		.clk_num = 1,
	},
};
DEFINE_CLK_TEGRA(cml1, 0, &tegra_cml_clk_ops, 0, mux_plle,
		mux_plle_p, &tegra_pll_e);

static struct clk tegra_pciex;
static struct clk_tegra tegra_pciex_hw = {
	.hw = {
		.clk = &tegra_pciex,
	},
	.reg = 0x48c,
	.fixed_rate = 100000000,
	.reset = tegra30_periph_clk_reset,
	.u.periph = {
		.clk_num = 74,
	},
};
DEFINE_CLK_TEGRA(pciex, 0, &tegra_pciex_clk_ops, 0, mux_plle,
		mux_plle_p, &tegra_pll_e);

#define SYNC_SOURCE(_name)					\
	static struct clk tegra_##_name##_sync;			\
	static struct clk_tegra tegra_##_name##_sync_hw = {	\
		.hw = {						\
			.clk = &tegra_##_name##_sync,		\
		},						\
		.max_rate = 24000000,				\
		.fixed_rate = 24000000,				\
	};							\
	static struct clk tegra_##_name##_sync = {		\
		.name = #_name "_sync",				\
		.hw = &tegra_##_name##_sync_hw.hw,		\
		.ops = &tegra_sync_source_ops,			\
		.flags = CLK_IS_ROOT,				\
	};

SYNC_SOURCE(spdif_in);
SYNC_SOURCE(i2s0);
SYNC_SOURCE(i2s1);
SYNC_SOURCE(i2s2);
SYNC_SOURCE(i2s3);
SYNC_SOURCE(i2s4);
SYNC_SOURCE(vimclk);

static struct clk *tegra_sync_source_list[] = {
	&tegra_spdif_in_sync,
	&tegra_i2s0_sync,
	&tegra_i2s1_sync,
	&tegra_i2s2_sync,
	&tegra_i2s3_sync,
	&tegra_i2s4_sync,
	&tegra_vimclk_sync,
};

static const char *mux_audio_sync_clk[] = {
	"spdif_in_sync",
	"i2s0_sync",
	"i2s1_sync",
	"i2s2_sync",
	"i2s3_sync",
	"i2s4_sync",
	"vimclk_sync",
};

#define AUDIO_SYNC_CLK(_name, _index)				\
	static struct clk tegra_##_name;			\
	static struct clk_tegra tegra_##_name##_hw = {		\
		.hw = {						\
			.clk = &tegra_##_name,			\
		},						\
		.max_rate = 24000000,				\
		.reg = 0x4A0 + (_index) * 4,			\
	};							\
	static struct clk tegra_##_name = {			\
		.name = #_name,					\
		.ops = &tegra30_audio_sync_clk_ops,		\
		.hw = &tegra_##_name##_hw.hw,			\
		.parent_names = mux_audio_sync_clk,		\
		.parents = tegra_sync_source_list,		\
		.num_parents = ARRAY_SIZE(mux_audio_sync_clk),	\
	};

AUDIO_SYNC_CLK(audio0, 0);
AUDIO_SYNC_CLK(audio1, 1);
AUDIO_SYNC_CLK(audio2, 2);
AUDIO_SYNC_CLK(audio3, 3);
AUDIO_SYNC_CLK(audio4, 4);
AUDIO_SYNC_CLK(audio5, 5);

static struct clk *tegra_clk_audio_list[] = {
	&tegra_audio0,
	&tegra_audio1,
	&tegra_audio2,
	&tegra_audio3,
	&tegra_audio4,
	&tegra_audio5,	/* SPDIF */
};

#define AUDIO_SYNC_2X_CLK(_name, _index)			\
	static const char *_name##_parent_names[] = {		\
		"tegra_" #_name,				\
	};							\
	static struct clk *_name##_parents[] = {		\
		&tegra_##_name,					\
	};							\
	static struct clk tegra_##_name##_2x;			\
	static struct clk_tegra tegra_##_name##_2x_hw = {	\
		.hw = {						\
			.clk = &tegra_##_name##_2x,		\
		},						\
		.flags = PERIPH_NO_RESET,			\
		.max_rate = 48000000,				\
		.reg = 0x49C,					\
		.reg_shift = 24 + (_index),			\
		.u.periph = {					\
			.clk_num = 113 + (_index),		\
		},						\
	};							\
	static struct clk tegra_##_name##_2x = {		\
		.name = #_name "_2x",				\
		.ops = &tegra30_clk_double_ops,			\
		.hw = &tegra_##_name##_2x_hw.hw,		\
		.parent_names = _name##_parent_names,		\
		.parents = _name##_parents,			\
		.parent = &tegra_##_name,			\
		.num_parents = 1,				\
	};

AUDIO_SYNC_2X_CLK(audio0, 0);
AUDIO_SYNC_2X_CLK(audio1, 1);
AUDIO_SYNC_2X_CLK(audio2, 2);
AUDIO_SYNC_2X_CLK(audio3, 3);
AUDIO_SYNC_2X_CLK(audio4, 4);
AUDIO_SYNC_2X_CLK(audio5, 5);	/* SPDIF */

static struct clk *tegra_clk_audio_2x_list[] = {
	&tegra_audio0_2x,
	&tegra_audio1_2x,
	&tegra_audio2_2x,
	&tegra_audio3_2x,
	&tegra_audio4_2x,
	&tegra_audio5_2x,	/* SPDIF */
};

#define MUX_I2S_SPDIF(_id)					\
static const char *mux_pllaout0_##_id##_2x_pllp_clkm[] = {	\
	"pll_a_out0",						\
	#_id "_2x",						\
	"pll_p",						\
	"clk_m",						\
};								\
static struct clk *mux_pllaout0_##_id##_2x_pllp_clkm_p[] = {	\
	&tegra_pll_a_out0,					\
	&tegra_##_id##_2x,					\
	&tegra_pll_p,						\
	&tegra_clk_m,						\
};

MUX_I2S_SPDIF(audio0);
MUX_I2S_SPDIF(audio1);
MUX_I2S_SPDIF(audio2);
MUX_I2S_SPDIF(audio3);
MUX_I2S_SPDIF(audio4);
MUX_I2S_SPDIF(audio5);		/* SPDIF */

static struct clk tegra_extern1;
static struct clk tegra_extern2;
static struct clk tegra_extern3;

/* External clock outputs (through PMC) */
#define MUX_EXTERN_OUT(_id)					\
static const char *mux_clkm_clkm2_clkm4_extern##_id[] = {	\
	"clk_m",						\
	"clk_m_div2",						\
	"clk_m_div4",						\
	"extern" #_id,						\
};								\
static struct clk *mux_clkm_clkm2_clkm4_extern##_id##_p[] = {	\
	&tegra_clk_m,						\
	&tegra_clk_m_div2,					\
	&tegra_clk_m_div4,					\
	&tegra_extern##_id,					\
};

MUX_EXTERN_OUT(1);
MUX_EXTERN_OUT(2);
MUX_EXTERN_OUT(3);

#define CLK_OUT_CLK(_name, _index)					\
	static struct clk tegra_##_name;				\
	static struct clk_tegra tegra_##_name##_hw = {			\
		.hw = {							\
			.clk = &tegra_##_name,				\
		},							\
		.lookup = {						\
			.dev_id	= #_name,				\
			.con_id	= "extern" #_index,			\
		},							\
		.flags = MUX_CLK_OUT,					\
		.fixed_rate = 216000000,					\
		.reg = 0x1a8,						\
		.u.periph = {						\
			.clk_num = (_index - 1) * 8 + 2,		\
		},							\
	};								\
	static struct clk tegra_##_name = {				\
		.name = #_name,						\
		.ops = &tegra_clk_out_ops,				\
		.hw = &tegra_##_name##_hw.hw,				\
		.parent_names = mux_clkm_clkm2_clkm4_extern##_index,	\
		.parents = mux_clkm_clkm2_clkm4_extern##_index##_p,	\
		.num_parents = ARRAY_SIZE(mux_clkm_clkm2_clkm4_extern##_index),\
	};

CLK_OUT_CLK(clk_out_1, 1);
CLK_OUT_CLK(clk_out_2, 2);
CLK_OUT_CLK(clk_out_3, 3);

static struct clk *tegra_clk_out_list[] = {
	&tegra_clk_out_1,
	&tegra_clk_out_2,
	&tegra_clk_out_3,
};

static const char *mux_sclk[] = {
	"clk_m",
	"pll_c_out1",
	"pll_p_out4",
	"pll_p_out3",
	"pll_p_out2",
	"dummy",
	"clk_32k",
	"pll_m_out1",
};

static struct clk *mux_sclk_p[] = {
	&tegra_clk_m,
	&tegra_pll_c_out1,
	&tegra_pll_p_out4,
	&tegra_pll_p_out3,
	&tegra_pll_p_out2,
	NULL,
	&tegra_clk_32k,
	&tegra_pll_m_out1,
};

static struct clk tegra_clk_sclk;
static struct clk_tegra tegra_clk_sclk_hw = {
	.hw = {
		.clk = &tegra_clk_sclk,
	},
	.reg = 0x28,
	.max_rate = 334000000,
	.min_rate = 40000000,
};

static struct clk tegra_clk_sclk = {
	.name = "sclk",
	.ops = &tegra30_super_ops,
	.hw = &tegra_clk_sclk_hw.hw,
	.parent_names = mux_sclk,
	.parents = mux_sclk_p,
	.num_parents = ARRAY_SIZE(mux_sclk),
};

static const char *mux_blink[] = {
	"clk_32k",
};

static struct clk *mux_blink_p[] = {
	&tegra_clk_32k,
};

static struct clk tegra_clk_blink;
static struct clk_tegra tegra_clk_blink_hw = {
	.hw = {
		.clk = &tegra_clk_blink,
	},
	.reg = 0x40,
	.max_rate = 32768,
};
static struct clk tegra_clk_blink = {
	.name = "blink",
	.ops = &tegra30_blink_clk_ops,
	.hw = &tegra_clk_blink_hw.hw,
	.parent = &tegra_clk_32k,
	.parent_names = mux_blink,
	.parents = mux_blink_p,
	.num_parents = ARRAY_SIZE(mux_blink),
};

static const char *mux_pllm_pllc_pllp_plla[] = {
	"pll_m",
	"pll_c",
	"pll_p",
	"pll_a_out0",
};

static const char *mux_pllp_pllc_pllm_clkm[] = {
	"pll_p",
	"pll_c",
	"pll_m",
	"clk_m",
};

static const char *mux_pllp_clkm[] = {
	"pll_p",
	"dummy",
	"dummy",
	"clk_m",
};

static const char *mux_pllp_plld_pllc_clkm[] = {
	"pll_p",
	"pll_d_out0",
	"pll_c",
	"clk_m",
};

static const char *mux_pllp_pllm_plld_plla_pllc_plld2_clkm[] = {
	"pll_p",
	"pll_m",
	"pll_d_out0",
	"pll_a_out0",
	"pll_c",
	"pll_d2_out0",
	"clk_m",
};

static const char *mux_plla_pllc_pllp_clkm[] = {
	"pll_a_out0",
	"dummy",
	"pll_p",
	"clk_m"
};

static const char *mux_pllp_pllc_clk32_clkm[] = {
	"pll_p",
	"pll_c",
	"clk_32k",
	"clk_m",
};

static const char *mux_pllp_pllc_clkm_clk32[] = {
	"pll_p",
	"pll_c",
	"clk_m",
	"clk_32k",
};

static const char *mux_pllp_pllc_pllm[] = {
	"pll_p",
	"pll_c",
	"pll_m",
};

static const char *mux_clk_m[] = {
	"clk_m",
};

static const char *mux_pllp_out3[] = {
	"pll_p_out3",
};

static const char *mux_plld_out0[] = {
	"pll_d_out0",
};

static const char *mux_plld_out0_plld2_out0[] = {
	"pll_d_out0",
	"pll_d2_out0",
};

static const char *mux_clk_32k[] = {
	"clk_32k",
};

static const char *mux_plla_clk32_pllp_clkm_plle[] = {
	"pll_a_out0",
	"clk_32k",
	"pll_p",
	"clk_m",
	"pll_e",
};

static const char *mux_cclk_g[] = {
	"clk_m",
	"pll_c",
	"clk_32k",
	"pll_m",
	"pll_p",
	"pll_p_out4",
	"pll_p_out3",
	"dummy",
	"pll_x",
};

static struct clk *mux_pllm_pllc_pllp_plla_p[] = {
	&tegra_pll_m,
	&tegra_pll_c,
	&tegra_pll_p,
	&tegra_pll_a_out0,
};

static struct clk *mux_pllp_pllc_pllm_clkm_p[] = {
	&tegra_pll_p,
	&tegra_pll_c,
	&tegra_pll_m,
	&tegra_clk_m,
};

static struct clk *mux_pllp_clkm_p[] = {
	&tegra_pll_p,
	NULL,
	NULL,
	&tegra_clk_m,
};

static struct clk *mux_pllp_plld_pllc_clkm_p[] = {
	&tegra_pll_p,
	&tegra_pll_d_out0,
	&tegra_pll_c,
	&tegra_clk_m,
};

static struct clk *mux_pllp_pllm_plld_plla_pllc_plld2_clkm_p[] = {
	&tegra_pll_p,
	&tegra_pll_m,
	&tegra_pll_d_out0,
	&tegra_pll_a_out0,
	&tegra_pll_c,
	&tegra_pll_d2_out0,
	&tegra_clk_m,
};

static struct clk *mux_plla_pllc_pllp_clkm_p[] = {
	&tegra_pll_a_out0,
	NULL,
	&tegra_pll_p,
	&tegra_clk_m,
};

static struct clk *mux_pllp_pllc_clk32_clkm_p[] = {
	&tegra_pll_p,
	&tegra_pll_c,
	&tegra_clk_32k,
	&tegra_clk_m,
};

static struct clk *mux_pllp_pllc_clkm_clk32_p[] = {
	&tegra_pll_p,
	&tegra_pll_c,
	&tegra_clk_m,
	&tegra_clk_32k,
};

static struct clk *mux_pllp_pllc_pllm_p[] = {
	&tegra_pll_p,
	&tegra_pll_c,
	&tegra_pll_m,
};

static struct clk *mux_clk_m_p[] = {
	&tegra_clk_m,
};

static struct clk *mux_pllp_out3_p[] = {
	&tegra_pll_p_out3,
};

static struct clk *mux_plld_out0_p[] = {
	&tegra_pll_d_out0,
};

static struct clk *mux_plld_out0_plld2_out0_p[] = {
	&tegra_pll_d_out0,
	&tegra_pll_d2_out0,
};

static struct clk *mux_clk_32k_p[] = {
	&tegra_clk_32k,
};

static struct clk *mux_plla_clk32_pllp_clkm_plle_p[] = {
	&tegra_pll_a_out0,
	&tegra_clk_32k,
	&tegra_pll_p,
	&tegra_clk_m,
	&tegra_pll_e,
};

static struct clk *mux_cclk_g_p[] = {
	&tegra_clk_m,
	&tegra_pll_c,
	&tegra_clk_32k,
	&tegra_pll_m,
	&tegra_pll_p,
	&tegra_pll_p_out4,
	&tegra_pll_p_out3,
	NULL,
	&tegra_pll_x,
};

static struct clk tegra_clk_cclk_g;
static struct clk_tegra tegra_clk_cclk_g_hw = {
	.hw = {
		.clk = &tegra_clk_cclk_g,
	},
	.flags = DIV_U71 | DIV_U71_INT,
	.reg = 0x368,
	.max_rate = 1700000000,
};
static struct clk tegra_clk_cclk_g = {
	.name = "cclk_g",
	.ops = &tegra30_super_ops,
	.hw = &tegra_clk_cclk_g_hw.hw,
	.parent_names = mux_cclk_g,
	.parents = mux_cclk_g_p,
	.num_parents = ARRAY_SIZE(mux_cclk_g),
};

static const char *mux_twd[] = {
	"cclk_g",
};

static struct clk *mux_twd_p[] = {
	&tegra_clk_cclk_g,
};

static struct clk tegra30_clk_twd;
static struct clk_tegra tegra30_clk_twd_hw = {
	.hw = {
		.clk = &tegra30_clk_twd,
	},
	.max_rate = 1400000000,
	.mul = 1,
	.div = 2,
};

static struct clk tegra30_clk_twd = {
	.name = "twd",
	.ops = &tegra30_twd_ops,
	.hw = &tegra30_clk_twd_hw.hw,
	.parent = &tegra_clk_cclk_g,
	.parent_names = mux_twd,
	.parents = mux_twd_p,
	.num_parents = ARRAY_SIZE(mux_twd),
};

#define PERIPH_CLK(_name, _dev, _con, _clk_num, _reg,	\
		_max, _inputs, _flags)	 		\
	static struct clk tegra_##_name;		\
	static struct clk_tegra tegra_##_name##_hw = {	\
		.hw = {					\
			.clk = &tegra_##_name,		\
		},					\
		.lookup = {				\
			.dev_id	= _dev,			\
			.con_id	= _con,			\
		},					\
		.reg = _reg,				\
		.flags = _flags,			\
		.max_rate = _max,			\
		.u.periph = {				\
			.clk_num = _clk_num,		\
		},					\
		.reset = &tegra30_periph_clk_reset,	\
	};						\
	static struct clk tegra_##_name = {		\
		.name = #_name,				\
		.ops = &tegra30_periph_clk_ops,		\
		.hw = &tegra_##_name##_hw.hw,		\
		.parent_names = _inputs,		\
		.parents = _inputs##_p,			\
		.num_parents = ARRAY_SIZE(_inputs),	\
	};

PERIPH_CLK(apbdma,	"tegra-apbdma",		NULL,	34,	0,	26000000,  mux_clk_m,			0);
PERIPH_CLK(rtc,		"rtc-tegra",		NULL,	4,	0,	32768,     mux_clk_32k,			PERIPH_NO_RESET | PERIPH_ON_APB);
PERIPH_CLK(kbc,		"tegra-kbc",		NULL,	36,	0,	32768,     mux_clk_32k,			PERIPH_NO_RESET | PERIPH_ON_APB);
PERIPH_CLK(timer,	"timer",		NULL,	5,	0,	26000000,  mux_clk_m,			0);
PERIPH_CLK(kfuse,	"kfuse-tegra",		NULL,	40,	0,	26000000,  mux_clk_m,			0);
PERIPH_CLK(fuse,	"fuse-tegra",		"fuse",	39,	0,	26000000,  mux_clk_m,			PERIPH_ON_APB);
PERIPH_CLK(fuse_burn,	"fuse-tegra",		"fuse_burn",	39,	0,	26000000,  mux_clk_m,		PERIPH_ON_APB);
PERIPH_CLK(apbif,	"tegra30-ahub",		"apbif", 107,	0,	26000000,  mux_clk_m,			0);
PERIPH_CLK(i2s0,	"tegra30-i2s.0",	NULL,	30,	0x1d8,	26000000,  mux_pllaout0_audio0_2x_pllp_clkm,	MUX | DIV_U71 | PERIPH_ON_APB);
PERIPH_CLK(i2s1,	"tegra30-i2s.1",	NULL,	11,	0x100,	26000000,  mux_pllaout0_audio1_2x_pllp_clkm,	MUX | DIV_U71 | PERIPH_ON_APB);
PERIPH_CLK(i2s2,	"tegra30-i2s.2",	NULL,	18,	0x104,	26000000,  mux_pllaout0_audio2_2x_pllp_clkm,	MUX | DIV_U71 | PERIPH_ON_APB);
PERIPH_CLK(i2s3,	"tegra30-i2s.3",	NULL,	101,	0x3bc,	26000000,  mux_pllaout0_audio3_2x_pllp_clkm,	MUX | DIV_U71 | PERIPH_ON_APB);
PERIPH_CLK(i2s4,	"tegra30-i2s.4",	NULL,	102,	0x3c0,	26000000,  mux_pllaout0_audio4_2x_pllp_clkm,	MUX | DIV_U71 | PERIPH_ON_APB);
PERIPH_CLK(spdif_out,	"tegra30-spdif",	"spdif_out",	10,	0x108,	100000000, mux_pllaout0_audio5_2x_pllp_clkm,	MUX | DIV_U71 | PERIPH_ON_APB);
PERIPH_CLK(spdif_in,	"tegra30-spdif",	"spdif_in",	10,	0x10c,	100000000, mux_pllp_pllc_pllm,		MUX | DIV_U71 | PERIPH_ON_APB);
PERIPH_CLK(pwm,		"tegra-pwm",		NULL,	17,	0x110,	432000000, mux_pllp_pllc_clk32_clkm,	MUX | MUX_PWM | DIV_U71 | PERIPH_ON_APB);
PERIPH_CLK(d_audio,	"tegra30-ahub",		"d_audio", 106,	0x3d0,	48000000,  mux_plla_pllc_pllp_clkm,	MUX | DIV_U71);
PERIPH_CLK(dam0,	"tegra30-dam.0",	NULL,	108,	0x3d8,	48000000,  mux_plla_pllc_pllp_clkm,	MUX | DIV_U71);
PERIPH_CLK(dam1,	"tegra30-dam.1",	NULL,	109,	0x3dc,	48000000,  mux_plla_pllc_pllp_clkm,	MUX | DIV_U71);
PERIPH_CLK(dam2,	"tegra30-dam.2",	NULL,	110,	0x3e0,	48000000,  mux_plla_pllc_pllp_clkm,	MUX | DIV_U71);
PERIPH_CLK(hda,		"tegra30-hda",		"hda",	125,	0x428,	108000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71);
PERIPH_CLK(hda2codec_2x,	"tegra30-hda",	"hda2codec",	111,	0x3e4,	48000000,  mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71);
PERIPH_CLK(hda2hdmi,	"tegra30-hda",		"hda2hdmi",	128,	0,	48000000,  mux_clk_m,			0);
PERIPH_CLK(sbc1,	"spi_tegra.0",		NULL,	41,	0x134,	160000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71 | PERIPH_ON_APB);
PERIPH_CLK(sbc2,	"spi_tegra.1",		NULL,	44,	0x118,	160000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71 | PERIPH_ON_APB);
PERIPH_CLK(sbc3,	"spi_tegra.2",		NULL,	46,	0x11c,	160000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71 | PERIPH_ON_APB);
PERIPH_CLK(sbc4,	"spi_tegra.3",		NULL,	68,	0x1b4,	160000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71 | PERIPH_ON_APB);
PERIPH_CLK(sbc5,	"spi_tegra.4",		NULL,	104,	0x3c8,	160000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71 | PERIPH_ON_APB);
PERIPH_CLK(sbc6,	"spi_tegra.5",		NULL,	105,	0x3cc,	160000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71 | PERIPH_ON_APB);
PERIPH_CLK(sata_oob,	"tegra_sata_oob",	NULL,	123,	0x420,	216000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71);
PERIPH_CLK(sata,	"tegra_sata",		NULL,	124,	0x424,	216000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71);
PERIPH_CLK(sata_cold,	"tegra_sata_cold",	NULL,	129,	0,	48000000,  mux_clk_m,			0);
PERIPH_CLK(ndflash,	"tegra_nand",		NULL,	13,	0x160,	240000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71);
PERIPH_CLK(ndspeed,	"tegra_nand_speed",	NULL,	80,	0x3f8,	240000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71);
PERIPH_CLK(vfir,	"vfir",			NULL,	7,	0x168,	72000000,  mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71 | PERIPH_ON_APB);
PERIPH_CLK(sdmmc1,	"sdhci-tegra.0",	NULL,	14,	0x150,	208000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71); /* scales with voltage */
PERIPH_CLK(sdmmc2,	"sdhci-tegra.1",	NULL,	9,	0x154,	104000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71); /* scales with voltage */
PERIPH_CLK(sdmmc3,	"sdhci-tegra.2",	NULL,	69,	0x1bc,	208000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71); /* scales with voltage */
PERIPH_CLK(sdmmc4,	"sdhci-tegra.3",	NULL,	15,	0x164,	104000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71); /* scales with voltage */
PERIPH_CLK(vcp,		"tegra-avp",		"vcp",	29,	0,	250000000, mux_clk_m,			0);
PERIPH_CLK(bsea,	"tegra-avp",		"bsea",	62,	0,	250000000, mux_clk_m,			0);
PERIPH_CLK(bsev,	"tegra-aes",		"bsev",	63,	0,	250000000, mux_clk_m,			0);
PERIPH_CLK(vde,		"vde",			NULL,	61,	0x1c8,	520000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71 | DIV_U71_INT);
PERIPH_CLK(csite,	"csite",		NULL,	73,	0x1d4,	144000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71); /* max rate ??? */
PERIPH_CLK(la,		"la",			NULL,	76,	0x1f8,	26000000,  mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71);
PERIPH_CLK(owr,		"tegra_w1",		NULL,	71,	0x1cc,	26000000,  mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71 | PERIPH_ON_APB);
PERIPH_CLK(nor,		"nor",			NULL,	42,	0x1d0,	127000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71); /* requires min voltage */
PERIPH_CLK(mipi,	"mipi",			NULL,	50,	0x174,	60000000,  mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71 | PERIPH_ON_APB); /* scales with voltage */
PERIPH_CLK(i2c1,	"tegra-i2c.0",		NULL,	12,	0x124,	26000000,  mux_pllp_clkm,		MUX | DIV_U16 | PERIPH_ON_APB);
PERIPH_CLK(i2c2,	"tegra-i2c.1",		NULL,	54,	0x198,	26000000,  mux_pllp_clkm,		MUX | DIV_U16 | PERIPH_ON_APB);
PERIPH_CLK(i2c3,	"tegra-i2c.2",		NULL,	67,	0x1b8,	26000000,  mux_pllp_clkm,		MUX | DIV_U16 | PERIPH_ON_APB);
PERIPH_CLK(i2c4,	"tegra-i2c.3",		NULL,	103,	0x3c4,	26000000,  mux_pllp_clkm,		MUX | DIV_U16 | PERIPH_ON_APB);
PERIPH_CLK(i2c5,	"tegra-i2c.4",		NULL,	47,	0x128,	26000000,  mux_pllp_clkm,		MUX | DIV_U16 | PERIPH_ON_APB);
PERIPH_CLK(uarta,	"tegra-uart.0",		NULL,	6,	0x178,	800000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71 | DIV_U71_UART | PERIPH_ON_APB);
PERIPH_CLK(uartb,	"tegra-uart.1",		NULL,	7,	0x17c,	800000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71 | DIV_U71_UART | PERIPH_ON_APB);
PERIPH_CLK(uartc,	"tegra-uart.2",		NULL,	55,	0x1a0,	800000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71 | DIV_U71_UART | PERIPH_ON_APB);
PERIPH_CLK(uartd,	"tegra-uart.3",		NULL,	65,	0x1c0,	800000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71 | DIV_U71_UART | PERIPH_ON_APB);
PERIPH_CLK(uarte,	"tegra-uart.4",		NULL,	66,	0x1c4,	800000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71 | DIV_U71_UART | PERIPH_ON_APB);
PERIPH_CLK(vi,		"tegra_camera",		"vi",	20,	0x148,	425000000, mux_pllm_pllc_pllp_plla,	MUX | DIV_U71 | DIV_U71_INT);
PERIPH_CLK(3d,		"3d",			NULL,	24,	0x158,	520000000, mux_pllm_pllc_pllp_plla,	MUX | DIV_U71 | DIV_U71_INT | DIV_U71_IDLE | PERIPH_MANUAL_RESET);
PERIPH_CLK(3d2,		"3d2",			NULL,	98,	0x3b0,	520000000, mux_pllm_pllc_pllp_plla,	MUX | DIV_U71 | DIV_U71_INT | DIV_U71_IDLE | PERIPH_MANUAL_RESET);
PERIPH_CLK(2d,		"2d",			NULL,	21,	0x15c,	520000000, mux_pllm_pllc_pllp_plla,	MUX | DIV_U71 | DIV_U71_INT | DIV_U71_IDLE);
PERIPH_CLK(vi_sensor,	"tegra_camera",		"vi_sensor",	20,	0x1a8,	150000000, mux_pllm_pllc_pllp_plla,	MUX | DIV_U71 | PERIPH_NO_RESET);
PERIPH_CLK(epp,		"epp",			NULL,	19,	0x16c,	520000000, mux_pllm_pllc_pllp_plla,	MUX | DIV_U71 | DIV_U71_INT);
PERIPH_CLK(mpe,		"mpe",			NULL,	60,	0x170,	520000000, mux_pllm_pllc_pllp_plla,	MUX | DIV_U71 | DIV_U71_INT);
PERIPH_CLK(host1x,	"host1x",		NULL,	28,	0x180,	260000000, mux_pllm_pllc_pllp_plla,	MUX | DIV_U71 | DIV_U71_INT);
PERIPH_CLK(cve,		"cve",			NULL,	49,	0x140,	250000000, mux_pllp_plld_pllc_clkm,	MUX | DIV_U71); /* requires min voltage */
PERIPH_CLK(tvo,		"tvo",			NULL,	49,	0x188,	250000000, mux_pllp_plld_pllc_clkm,	MUX | DIV_U71); /* requires min voltage */
PERIPH_CLK(dtv,		"dtv",			NULL,	79,	0x1dc,	250000000, mux_clk_m,			0);
PERIPH_CLK(hdmi,	"hdmi",			NULL,	51,	0x18c,	148500000, mux_pllp_pllm_plld_plla_pllc_plld2_clkm,	MUX | MUX8 | DIV_U71);
PERIPH_CLK(tvdac,	"tvdac",		NULL,	53,	0x194,	220000000, mux_pllp_plld_pllc_clkm,	MUX | DIV_U71); /* requires min voltage */
PERIPH_CLK(disp1,	"tegradc.0",		NULL,	27,	0x138,	600000000, mux_pllp_pllm_plld_plla_pllc_plld2_clkm,	MUX | MUX8);
PERIPH_CLK(disp2,	"tegradc.1",		NULL,	26,	0x13c,	600000000, mux_pllp_pllm_plld_plla_pllc_plld2_clkm,	MUX | MUX8);
PERIPH_CLK(usbd,	"fsl-tegra-udc",	NULL,	22,	0,	480000000, mux_clk_m,			0); /* requires min voltage */
PERIPH_CLK(usb2,	"tegra-ehci.1",		NULL,	58,	0,	480000000, mux_clk_m,			0); /* requires min voltage */
PERIPH_CLK(usb3,	"tegra-ehci.2",		NULL,	59,	0,	480000000, mux_clk_m,			0); /* requires min voltage */
PERIPH_CLK(dsia,	"tegradc.0",		"dsia",	48,	0,	500000000, mux_plld_out0,		0);
PERIPH_CLK(csi,		"tegra_camera",		"csi",	52,	0,	102000000, mux_pllp_out3,		0);
PERIPH_CLK(isp,		"tegra_camera",		"isp",	23,	0,	150000000, mux_clk_m,			0); /* same frequency as VI */
PERIPH_CLK(csus,	"tegra_camera",		"csus",	92,	0,	150000000, mux_clk_m,			PERIPH_NO_RESET);
PERIPH_CLK(tsensor,	"tegra-tsensor",	NULL,	100,	0x3b8,	216000000, mux_pllp_pllc_clkm_clk32,	MUX | DIV_U71);
PERIPH_CLK(actmon,	"actmon",		NULL,	119,	0x3e8,	216000000, mux_pllp_pllc_clk32_clkm,	MUX | DIV_U71);
PERIPH_CLK(extern1,	"extern1",		NULL,	120,	0x3ec,	216000000, mux_plla_clk32_pllp_clkm_plle,	MUX | MUX8 | DIV_U71);
PERIPH_CLK(extern2,	"extern2",		NULL,	121,	0x3f0,	216000000, mux_plla_clk32_pllp_clkm_plle,	MUX | MUX8 | DIV_U71);
PERIPH_CLK(extern3,	"extern3",		NULL,	122,	0x3f4,	216000000, mux_plla_clk32_pllp_clkm_plle,	MUX | MUX8 | DIV_U71);
PERIPH_CLK(i2cslow,	"i2cslow",		NULL,	81,	0x3fc,	26000000,  mux_pllp_pllc_clk32_clkm,	MUX | DIV_U71 | PERIPH_ON_APB);
PERIPH_CLK(pcie,	"tegra-pcie",		"pcie",	70,	0,	250000000, mux_clk_m,			0);
PERIPH_CLK(afi,		"tegra-pcie",		"afi",	72,	0,	250000000, mux_clk_m,			0);
PERIPH_CLK(se,		"se",			NULL,	127,	0x42c,	520000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71 | DIV_U71_INT);

static struct clk tegra_dsib;
static struct clk_tegra tegra_dsib_hw = {
	.hw = {
		.clk = &tegra_dsib,
	},
	.lookup = {
		.dev_id	= "tegradc.1",
		.con_id	= "dsib",
	},
	.reg = 0xd0,
	.flags = MUX | PLLD,
	.max_rate = 500000000,
	.u.periph = {
		.clk_num = 82,
	},
	.reset = &tegra30_periph_clk_reset,
};
static struct clk tegra_dsib = {
	.name = "dsib",
	.ops = &tegra30_dsib_clk_ops,
	.hw = &tegra_dsib_hw.hw,
	.parent_names = mux_plld_out0_plld2_out0,
	.parents = mux_plld_out0_plld2_out0_p,
	.num_parents = ARRAY_SIZE(mux_plld_out0_plld2_out0),
};

struct clk *tegra_list_clks[] = {
	&tegra_apbdma,
	&tegra_rtc,
	&tegra_kbc,
	&tegra_kfuse,
	&tegra_fuse,
	&tegra_fuse_burn,
	&tegra_apbif,
	&tegra_i2s0,
	&tegra_i2s1,
	&tegra_i2s2,
	&tegra_i2s3,
	&tegra_i2s4,
	&tegra_spdif_out,
	&tegra_spdif_in,
	&tegra_pwm,
	&tegra_d_audio,
	&tegra_dam0,
	&tegra_dam1,
	&tegra_dam2,
	&tegra_hda,
	&tegra_hda2codec_2x,
	&tegra_hda2hdmi,
	&tegra_sbc1,
	&tegra_sbc2,
	&tegra_sbc3,
	&tegra_sbc4,
	&tegra_sbc5,
	&tegra_sbc6,
	&tegra_sata_oob,
	&tegra_sata,
	&tegra_sata_cold,
	&tegra_ndflash,
	&tegra_ndspeed,
	&tegra_vfir,
	&tegra_sdmmc1,
	&tegra_sdmmc2,
	&tegra_sdmmc3,
	&tegra_sdmmc4,
	&tegra_vcp,
	&tegra_bsea,
	&tegra_bsev,
	&tegra_vde,
	&tegra_csite,
	&tegra_la,
	&tegra_owr,
	&tegra_nor,
	&tegra_mipi,
	&tegra_i2c1,
	&tegra_i2c2,
	&tegra_i2c3,
	&tegra_i2c4,
	&tegra_i2c5,
	&tegra_uarta,
	&tegra_uartb,
	&tegra_uartc,
	&tegra_uartd,
	&tegra_uarte,
	&tegra_vi,
	&tegra_3d,
	&tegra_3d2,
	&tegra_2d,
	&tegra_vi_sensor,
	&tegra_epp,
	&tegra_mpe,
	&tegra_host1x,
	&tegra_cve,
	&tegra_tvo,
	&tegra_dtv,
	&tegra_hdmi,
	&tegra_tvdac,
	&tegra_disp1,
	&tegra_disp2,
	&tegra_usbd,
	&tegra_usb2,
	&tegra_usb3,
	&tegra_dsia,
	&tegra_dsib,
	&tegra_csi,
	&tegra_isp,
	&tegra_csus,
	&tegra_tsensor,
	&tegra_actmon,
	&tegra_extern1,
	&tegra_extern2,
	&tegra_extern3,
	&tegra_i2cslow,
	&tegra_pcie,
	&tegra_afi,
	&tegra_se,
};

#define CLK_DUPLICATE(_name, _dev, _con)	\
	{					\
		.name	= _name,		\
		.lookup	= {			\
			.dev_id	= _dev,		\
			.con_id	= _con,		\
		},				\
	}

/* Some clocks may be used by different drivers depending on the board
 * configuration.  List those here to register them twice in the clock lookup
 * table under two names.
 */
struct clk_duplicate tegra_clk_duplicates[] = {
	CLK_DUPLICATE("uarta",  "serial8250.0", NULL),
	CLK_DUPLICATE("uartb",  "serial8250.1", NULL),
	CLK_DUPLICATE("uartc",  "serial8250.2", NULL),
	CLK_DUPLICATE("uartd",  "serial8250.3", NULL),
	CLK_DUPLICATE("uarte",  "serial8250.4", NULL),
	CLK_DUPLICATE("usbd", "utmip-pad", NULL),
	CLK_DUPLICATE("usbd", "tegra-ehci.0", NULL),
	CLK_DUPLICATE("usbd", "tegra-otg", NULL),
	CLK_DUPLICATE("hdmi", "tegradc.0", "hdmi"),
	CLK_DUPLICATE("hdmi", "tegradc.1", "hdmi"),
	CLK_DUPLICATE("dsib", "tegradc.0", "dsib"),
	CLK_DUPLICATE("dsia", "tegradc.1", "dsia"),
	CLK_DUPLICATE("bsev", "tegra-avp", "bsev"),
	CLK_DUPLICATE("bsev", "nvavp", "bsev"),
	CLK_DUPLICATE("vde", "tegra-aes", "vde"),
	CLK_DUPLICATE("bsea", "tegra-aes", "bsea"),
	CLK_DUPLICATE("bsea", "nvavp", "bsea"),
	CLK_DUPLICATE("cml1", "tegra_sata_cml", NULL),
	CLK_DUPLICATE("cml0", "tegra_pcie", "cml"),
	CLK_DUPLICATE("pciex", "tegra_pcie", "pciex"),
	CLK_DUPLICATE("i2c1", "tegra-i2c-slave.0", NULL),
	CLK_DUPLICATE("i2c2", "tegra-i2c-slave.1", NULL),
	CLK_DUPLICATE("i2c3", "tegra-i2c-slave.2", NULL),
	CLK_DUPLICATE("i2c4", "tegra-i2c-slave.3", NULL),
	CLK_DUPLICATE("i2c5", "tegra-i2c-slave.4", NULL),
	CLK_DUPLICATE("sbc1", "spi_slave_tegra.0", NULL),
	CLK_DUPLICATE("sbc2", "spi_slave_tegra.1", NULL),
	CLK_DUPLICATE("sbc3", "spi_slave_tegra.2", NULL),
	CLK_DUPLICATE("sbc4", "spi_slave_tegra.3", NULL),
	CLK_DUPLICATE("sbc5", "spi_slave_tegra.4", NULL),
	CLK_DUPLICATE("sbc6", "spi_slave_tegra.5", NULL),
	CLK_DUPLICATE("twd", "smp_twd", NULL),
	CLK_DUPLICATE("vcp", "nvavp", "vcp"),
	CLK_DUPLICATE("i2s0", NULL, "i2s0"),
	CLK_DUPLICATE("i2s1", NULL, "i2s1"),
	CLK_DUPLICATE("i2s2", NULL, "i2s2"),
	CLK_DUPLICATE("i2s3", NULL, "i2s3"),
	CLK_DUPLICATE("i2s4", NULL, "i2s4"),
	CLK_DUPLICATE("dam0", NULL, "dam0"),
	CLK_DUPLICATE("dam1", NULL, "dam1"),
	CLK_DUPLICATE("dam2", NULL, "dam2"),
	CLK_DUPLICATE("spdif_in", NULL, "spdif_in"),
};

struct clk *tegra_ptr_clks[] = {
	&tegra_clk_32k,
	&tegra_clk_m,
	&tegra_clk_m_div2,
	&tegra_clk_m_div4,
	&tegra_pll_ref,
	&tegra_pll_m,
	&tegra_pll_m_out1,
	&tegra_pll_c,
	&tegra_pll_c_out1,
	&tegra_pll_p,
	&tegra_pll_p_out1,
	&tegra_pll_p_out2,
	&tegra_pll_p_out3,
	&tegra_pll_p_out4,
	&tegra_pll_a,
	&tegra_pll_a_out0,
	&tegra_pll_d,
	&tegra_pll_d_out0,
	&tegra_pll_d2,
	&tegra_pll_d2_out0,
	&tegra_pll_u,
	&tegra_pll_x,
	&tegra_pll_x_out0,
	&tegra_pll_e,
	&tegra_clk_cclk_g,
	&tegra_cml0,
	&tegra_cml1,
	&tegra_pciex,
	&tegra_clk_sclk,
	&tegra_clk_blink,
	&tegra30_clk_twd,
};

static void tegra30_init_one_clock(struct clk *c)
{
	struct clk_tegra *clk = to_clk_tegra(c->hw);
	__clk_init(NULL, c);
	INIT_LIST_HEAD(&clk->shared_bus_list);
	if (!clk->lookup.dev_id && !clk->lookup.con_id)
		clk->lookup.con_id = c->name;
	clk->lookup.clk = c;
	clkdev_add(&clk->lookup);
	tegra_clk_add(c);
}

void __init tegra30_init_clocks(void)
{
	int i;
	struct clk *c;

	for (i = 0; i < ARRAY_SIZE(tegra_ptr_clks); i++)
		tegra30_init_one_clock(tegra_ptr_clks[i]);

	for (i = 0; i < ARRAY_SIZE(tegra_list_clks); i++)
		tegra30_init_one_clock(tegra_list_clks[i]);

	for (i = 0; i < ARRAY_SIZE(tegra_clk_duplicates); i++) {
		c = tegra_get_clock_by_name(tegra_clk_duplicates[i].name);
		if (!c) {
			pr_err("%s: Unknown duplicate clock %s\n", __func__,
				tegra_clk_duplicates[i].name);
			continue;
		}

		tegra_clk_duplicates[i].lookup.clk = c;
		clkdev_add(&tegra_clk_duplicates[i].lookup);
	}

	for (i = 0; i < ARRAY_SIZE(tegra_sync_source_list); i++)
		tegra30_init_one_clock(tegra_sync_source_list[i]);
	for (i = 0; i < ARRAY_SIZE(tegra_clk_audio_list); i++)
		tegra30_init_one_clock(tegra_clk_audio_list[i]);
	for (i = 0; i < ARRAY_SIZE(tegra_clk_audio_2x_list); i++)
		tegra30_init_one_clock(tegra_clk_audio_2x_list[i]);

	for (i = 0; i < ARRAY_SIZE(tegra_clk_out_list); i++)
		tegra30_init_one_clock(tegra_clk_out_list[i]);

	tegra30_cpu_car_ops_init();
}
