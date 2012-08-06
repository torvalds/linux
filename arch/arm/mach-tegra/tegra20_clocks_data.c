/*
 * arch/arm/mach-tegra/tegra2_clocks.c
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/clkdev.h>
#include <linux/clk.h>

#include <mach/iomap.h>
#include <mach/suspend.h>

#include "clock.h"
#include "fuse.h"
#include "tegra2_emc.h"
#include "tegra20_clocks.h"

/* Clock definitions */
static struct clk tegra_clk_32k = {
	.name = "clk_32k",
	.rate = 32768,
	.ops  = NULL,
	.max_rate = 32768,
};

static struct clk_pll_freq_table tegra_pll_s_freq_table[] = {
	{32768, 12000000, 366, 1, 1, 0},
	{32768, 13000000, 397, 1, 1, 0},
	{32768, 19200000, 586, 1, 1, 0},
	{32768, 26000000, 793, 1, 1, 0},
	{0, 0, 0, 0, 0, 0},
};

static struct clk tegra_pll_s = {
	.name      = "pll_s",
	.flags     = PLL_ALT_MISC_REG,
	.ops       = &tegra_pll_ops,
	.parent    = &tegra_clk_32k,
	.max_rate  = 26000000,
	.reg       = 0xf0,
	.u.pll = {
		.input_min = 32768,
		.input_max = 32768,
		.cf_min    = 0, /* FIXME */
		.cf_max    = 0, /* FIXME */
		.vco_min   = 12000000,
		.vco_max   = 26000000,
		.freq_table = tegra_pll_s_freq_table,
		.lock_delay = 300,
	},
};

static struct clk_mux_sel tegra_clk_m_sel[] = {
	{ .input = &tegra_clk_32k, .value = 0},
	{ .input = &tegra_pll_s,  .value = 1},
	{ NULL , 0},
};

static struct clk tegra_clk_m = {
	.name      = "clk_m",
	.flags     = ENABLE_ON_INIT,
	.ops       = &tegra_clk_m_ops,
	.inputs    = tegra_clk_m_sel,
	.reg       = 0x1fc,
	.reg_shift = 28,
	.max_rate  = 26000000,
};

static struct clk_pll_freq_table tegra_pll_c_freq_table[] = {
	{ 12000000, 600000000, 600, 12, 1, 8 },
	{ 13000000, 600000000, 600, 13, 1, 8 },
	{ 19200000, 600000000, 500, 16, 1, 6 },
	{ 26000000, 600000000, 600, 26, 1, 8 },
	{ 0, 0, 0, 0, 0, 0 },
};

static struct clk tegra_pll_c = {
	.name      = "pll_c",
	.flags	   = PLL_HAS_CPCON,
	.ops       = &tegra_pll_ops,
	.reg       = 0x80,
	.parent    = &tegra_clk_m,
	.max_rate  = 600000000,
	.u.pll = {
		.input_min = 2000000,
		.input_max = 31000000,
		.cf_min    = 1000000,
		.cf_max    = 6000000,
		.vco_min   = 20000000,
		.vco_max   = 1400000000,
		.freq_table = tegra_pll_c_freq_table,
		.lock_delay = 300,
	},
};

static struct clk tegra_pll_c_out1 = {
	.name      = "pll_c_out1",
	.ops       = &tegra_pll_div_ops,
	.flags     = DIV_U71,
	.parent    = &tegra_pll_c,
	.reg       = 0x84,
	.reg_shift = 0,
	.max_rate  = 600000000,
};

static struct clk_pll_freq_table tegra_pll_m_freq_table[] = {
	{ 12000000, 666000000, 666, 12, 1, 8},
	{ 13000000, 666000000, 666, 13, 1, 8},
	{ 19200000, 666000000, 555, 16, 1, 8},
	{ 26000000, 666000000, 666, 26, 1, 8},
	{ 12000000, 600000000, 600, 12, 1, 8},
	{ 13000000, 600000000, 600, 13, 1, 8},
	{ 19200000, 600000000, 375, 12, 1, 6},
	{ 26000000, 600000000, 600, 26, 1, 8},
	{ 0, 0, 0, 0, 0, 0 },
};

static struct clk tegra_pll_m = {
	.name      = "pll_m",
	.flags     = PLL_HAS_CPCON,
	.ops       = &tegra_pll_ops,
	.reg       = 0x90,
	.parent    = &tegra_clk_m,
	.max_rate  = 800000000,
	.u.pll = {
		.input_min = 2000000,
		.input_max = 31000000,
		.cf_min    = 1000000,
		.cf_max    = 6000000,
		.vco_min   = 20000000,
		.vco_max   = 1200000000,
		.freq_table = tegra_pll_m_freq_table,
		.lock_delay = 300,
	},
};

static struct clk tegra_pll_m_out1 = {
	.name      = "pll_m_out1",
	.ops       = &tegra_pll_div_ops,
	.flags     = DIV_U71,
	.parent    = &tegra_pll_m,
	.reg       = 0x94,
	.reg_shift = 0,
	.max_rate  = 600000000,
};

static struct clk_pll_freq_table tegra_pll_p_freq_table[] = {
	{ 12000000, 216000000, 432, 12, 2, 8},
	{ 13000000, 216000000, 432, 13, 2, 8},
	{ 19200000, 216000000, 90,   4, 2, 1},
	{ 26000000, 216000000, 432, 26, 2, 8},
	{ 12000000, 432000000, 432, 12, 1, 8},
	{ 13000000, 432000000, 432, 13, 1, 8},
	{ 19200000, 432000000, 90,   4, 1, 1},
	{ 26000000, 432000000, 432, 26, 1, 8},
	{ 0, 0, 0, 0, 0, 0 },
};

static struct clk tegra_pll_p = {
	.name      = "pll_p",
	.flags     = ENABLE_ON_INIT | PLL_FIXED | PLL_HAS_CPCON,
	.ops       = &tegra_pll_ops,
	.reg       = 0xa0,
	.parent    = &tegra_clk_m,
	.max_rate  = 432000000,
	.u.pll = {
		.input_min = 2000000,
		.input_max = 31000000,
		.cf_min    = 1000000,
		.cf_max    = 6000000,
		.vco_min   = 20000000,
		.vco_max   = 1400000000,
		.freq_table = tegra_pll_p_freq_table,
		.lock_delay = 300,
	},
};

static struct clk tegra_pll_p_out1 = {
	.name      = "pll_p_out1",
	.ops       = &tegra_pll_div_ops,
	.flags     = ENABLE_ON_INIT | DIV_U71 | DIV_U71_FIXED,
	.parent    = &tegra_pll_p,
	.reg       = 0xa4,
	.reg_shift = 0,
	.max_rate  = 432000000,
};

static struct clk tegra_pll_p_out2 = {
	.name      = "pll_p_out2",
	.ops       = &tegra_pll_div_ops,
	.flags     = ENABLE_ON_INIT | DIV_U71 | DIV_U71_FIXED,
	.parent    = &tegra_pll_p,
	.reg       = 0xa4,
	.reg_shift = 16,
	.max_rate  = 432000000,
};

static struct clk tegra_pll_p_out3 = {
	.name      = "pll_p_out3",
	.ops       = &tegra_pll_div_ops,
	.flags     = ENABLE_ON_INIT | DIV_U71 | DIV_U71_FIXED,
	.parent    = &tegra_pll_p,
	.reg       = 0xa8,
	.reg_shift = 0,
	.max_rate  = 432000000,
};

static struct clk tegra_pll_p_out4 = {
	.name      = "pll_p_out4",
	.ops       = &tegra_pll_div_ops,
	.flags     = ENABLE_ON_INIT | DIV_U71 | DIV_U71_FIXED,
	.parent    = &tegra_pll_p,
	.reg       = 0xa8,
	.reg_shift = 16,
	.max_rate  = 432000000,
};

static struct clk_pll_freq_table tegra_pll_a_freq_table[] = {
	{ 28800000, 56448000, 49, 25, 1, 1},
	{ 28800000, 73728000, 64, 25, 1, 1},
	{ 28800000, 24000000,  5,  6, 1, 1},
	{ 0, 0, 0, 0, 0, 0 },
};

static struct clk tegra_pll_a = {
	.name      = "pll_a",
	.flags     = PLL_HAS_CPCON,
	.ops       = &tegra_pll_ops,
	.reg       = 0xb0,
	.parent    = &tegra_pll_p_out1,
	.max_rate  = 73728000,
	.u.pll = {
		.input_min = 2000000,
		.input_max = 31000000,
		.cf_min    = 1000000,
		.cf_max    = 6000000,
		.vco_min   = 20000000,
		.vco_max   = 1400000000,
		.freq_table = tegra_pll_a_freq_table,
		.lock_delay = 300,
	},
};

static struct clk tegra_pll_a_out0 = {
	.name      = "pll_a_out0",
	.ops       = &tegra_pll_div_ops,
	.flags     = DIV_U71,
	.parent    = &tegra_pll_a,
	.reg       = 0xb4,
	.reg_shift = 0,
	.max_rate  = 73728000,
};

static struct clk_pll_freq_table tegra_pll_d_freq_table[] = {
	{ 12000000, 216000000, 216, 12, 1, 4},
	{ 13000000, 216000000, 216, 13, 1, 4},
	{ 19200000, 216000000, 135, 12, 1, 3},
	{ 26000000, 216000000, 216, 26, 1, 4},

	{ 12000000, 594000000, 594, 12, 1, 8},
	{ 13000000, 594000000, 594, 13, 1, 8},
	{ 19200000, 594000000, 495, 16, 1, 8},
	{ 26000000, 594000000, 594, 26, 1, 8},

	{ 12000000, 1000000000, 1000, 12, 1, 12},
	{ 13000000, 1000000000, 1000, 13, 1, 12},
	{ 19200000, 1000000000, 625,  12, 1, 8},
	{ 26000000, 1000000000, 1000, 26, 1, 12},

	{ 0, 0, 0, 0, 0, 0 },
};

static struct clk tegra_pll_d = {
	.name      = "pll_d",
	.flags     = PLL_HAS_CPCON | PLLD,
	.ops       = &tegra_pll_ops,
	.reg       = 0xd0,
	.parent    = &tegra_clk_m,
	.max_rate  = 1000000000,
	.u.pll = {
		.input_min = 2000000,
		.input_max = 40000000,
		.cf_min    = 1000000,
		.cf_max    = 6000000,
		.vco_min   = 40000000,
		.vco_max   = 1000000000,
		.freq_table = tegra_pll_d_freq_table,
		.lock_delay = 1000,
	},
};

static struct clk tegra_pll_d_out0 = {
	.name      = "pll_d_out0",
	.ops       = &tegra_pll_div_ops,
	.flags     = DIV_2 | PLLD,
	.parent    = &tegra_pll_d,
	.max_rate  = 500000000,
};

static struct clk_pll_freq_table tegra_pll_u_freq_table[] = {
	{ 12000000, 480000000, 960, 12, 2, 0},
	{ 13000000, 480000000, 960, 13, 2, 0},
	{ 19200000, 480000000, 200, 4,  2, 0},
	{ 26000000, 480000000, 960, 26, 2, 0},
	{ 0, 0, 0, 0, 0, 0 },
};

static struct clk tegra_pll_u = {
	.name      = "pll_u",
	.flags     = PLLU,
	.ops       = &tegra_pll_ops,
	.reg       = 0xc0,
	.parent    = &tegra_clk_m,
	.max_rate  = 480000000,
	.u.pll = {
		.input_min = 2000000,
		.input_max = 40000000,
		.cf_min    = 1000000,
		.cf_max    = 6000000,
		.vco_min   = 480000000,
		.vco_max   = 960000000,
		.freq_table = tegra_pll_u_freq_table,
		.lock_delay = 1000,
	},
};

static struct clk_pll_freq_table tegra_pll_x_freq_table[] = {
	/* 1 GHz */
	{ 12000000, 1000000000, 1000, 12, 1, 12},
	{ 13000000, 1000000000, 1000, 13, 1, 12},
	{ 19200000, 1000000000, 625,  12, 1, 8},
	{ 26000000, 1000000000, 1000, 26, 1, 12},

	/* 912 MHz */
	{ 12000000, 912000000,  912,  12, 1, 12},
	{ 13000000, 912000000,  912,  13, 1, 12},
	{ 19200000, 912000000,  760,  16, 1, 8},
	{ 26000000, 912000000,  912,  26, 1, 12},

	/* 816 MHz */
	{ 12000000, 816000000,  816,  12, 1, 12},
	{ 13000000, 816000000,  816,  13, 1, 12},
	{ 19200000, 816000000,  680,  16, 1, 8},
	{ 26000000, 816000000,  816,  26, 1, 12},

	/* 760 MHz */
	{ 12000000, 760000000,  760,  12, 1, 12},
	{ 13000000, 760000000,  760,  13, 1, 12},
	{ 19200000, 760000000,  950,  24, 1, 8},
	{ 26000000, 760000000,  760,  26, 1, 12},

	/* 750 MHz */
	{ 12000000, 750000000,  750,  12, 1, 12},
	{ 13000000, 750000000,  750,  13, 1, 12},
	{ 19200000, 750000000,  625,  16, 1, 8},
	{ 26000000, 750000000,  750,  26, 1, 12},

	/* 608 MHz */
	{ 12000000, 608000000,  608,  12, 1, 12},
	{ 13000000, 608000000,  608,  13, 1, 12},
	{ 19200000, 608000000,  380,  12, 1, 8},
	{ 26000000, 608000000,  608,  26, 1, 12},

	/* 456 MHz */
	{ 12000000, 456000000,  456,  12, 1, 12},
	{ 13000000, 456000000,  456,  13, 1, 12},
	{ 19200000, 456000000,  380,  16, 1, 8},
	{ 26000000, 456000000,  456,  26, 1, 12},

	/* 312 MHz */
	{ 12000000, 312000000,  312,  12, 1, 12},
	{ 13000000, 312000000,  312,  13, 1, 12},
	{ 19200000, 312000000,  260,  16, 1, 8},
	{ 26000000, 312000000,  312,  26, 1, 12},

	{ 0, 0, 0, 0, 0, 0 },
};

static struct clk tegra_pll_x = {
	.name      = "pll_x",
	.flags     = PLL_HAS_CPCON | PLL_ALT_MISC_REG,
	.ops       = &tegra_pllx_ops,
	.reg       = 0xe0,
	.parent    = &tegra_clk_m,
	.max_rate  = 1000000000,
	.u.pll = {
		.input_min = 2000000,
		.input_max = 31000000,
		.cf_min    = 1000000,
		.cf_max    = 6000000,
		.vco_min   = 20000000,
		.vco_max   = 1200000000,
		.freq_table = tegra_pll_x_freq_table,
		.lock_delay = 300,
	},
};

static struct clk_pll_freq_table tegra_pll_e_freq_table[] = {
	{ 12000000, 100000000,  200,  24, 1, 0 },
	{ 0, 0, 0, 0, 0, 0 },
};

static struct clk tegra_pll_e = {
	.name      = "pll_e",
	.flags	   = PLL_ALT_MISC_REG,
	.ops       = &tegra_plle_ops,
	.parent    = &tegra_clk_m,
	.reg       = 0xe8,
	.max_rate  = 100000000,
	.u.pll = {
		.input_min = 12000000,
		.input_max = 12000000,
		.freq_table = tegra_pll_e_freq_table,
	},
};

static struct clk tegra_clk_d = {
	.name      = "clk_d",
	.flags     = PERIPH_NO_RESET,
	.ops       = &tegra_clk_double_ops,
	.reg       = 0x34,
	.reg_shift = 12,
	.parent    = &tegra_clk_m,
	.max_rate  = 52000000,
	.u.periph  = {
		.clk_num = 90,
	},
};

/* dap_mclk1, belongs to the cdev1 pingroup. */
static struct clk tegra_clk_cdev1 = {
	.name      = "cdev1",
	.ops       = &tegra_cdev_clk_ops,
	.rate      = 26000000,
	.max_rate  = 26000000,
	.u.periph  = {
		.clk_num = 94,
	},
};

/* dap_mclk2, belongs to the cdev2 pingroup. */
static struct clk tegra_clk_cdev2 = {
	.name      = "cdev2",
	.ops       = &tegra_cdev_clk_ops,
	.rate      = 26000000,
	.max_rate  = 26000000,
	.u.periph  = {
		.clk_num   = 93,
	},
};

/* initialized before peripheral clocks */
static struct clk_mux_sel mux_audio_sync_clk[8+1];
static const struct audio_sources {
	const char *name;
	int value;
} mux_audio_sync_clk_sources[] = {
	{ .name = "spdif_in", .value = 0 },
	{ .name = "i2s1", .value = 1 },
	{ .name = "i2s2", .value = 2 },
	{ .name = "pll_a_out0", .value = 4 },
#if 0 /* FIXME: not implemented */
	{ .name = "ac97", .value = 3 },
	{ .name = "ext_audio_clk2", .value = 5 },
	{ .name = "ext_audio_clk1", .value = 6 },
	{ .name = "ext_vimclk", .value = 7 },
#endif
	{ NULL, 0 }
};

static struct clk tegra_clk_audio = {
	.name      = "audio",
	.inputs    = mux_audio_sync_clk,
	.reg       = 0x38,
	.max_rate  = 73728000,
	.ops       = &tegra_audio_sync_clk_ops
};

static struct clk tegra_clk_audio_2x = {
	.name      = "audio_2x",
	.flags     = PERIPH_NO_RESET,
	.max_rate  = 48000000,
	.ops       = &tegra_clk_double_ops,
	.reg       = 0x34,
	.reg_shift = 8,
	.parent    = &tegra_clk_audio,
	.u.periph = {
		.clk_num = 89,
	},
};

static struct clk_lookup tegra_audio_clk_lookups[] = {
	{ .con_id = "audio", .clk = &tegra_clk_audio },
	{ .con_id = "audio_2x", .clk = &tegra_clk_audio_2x }
};

/* This is called after peripheral clocks are initialized, as the
 * audio_sync clock depends on some of the peripheral clocks.
 */

static void init_audio_sync_clock_mux(void)
{
	int i;
	struct clk_mux_sel *sel = mux_audio_sync_clk;
	const struct audio_sources *src = mux_audio_sync_clk_sources;
	struct clk_lookup *lookup;

	for (i = 0; src->name; i++, sel++, src++) {
		sel->input = tegra_get_clock_by_name(src->name);
		if (!sel->input)
			pr_err("%s: could not find clk %s\n", __func__,
				src->name);
		sel->value = src->value;
	}

	lookup = tegra_audio_clk_lookups;
	for (i = 0; i < ARRAY_SIZE(tegra_audio_clk_lookups); i++, lookup++) {
		clk_init(lookup->clk);
		clkdev_add(lookup);
	}
}

static struct clk_mux_sel mux_cclk[] = {
	{ .input = &tegra_clk_m,	.value = 0},
	{ .input = &tegra_pll_c,	.value = 1},
	{ .input = &tegra_clk_32k,	.value = 2},
	{ .input = &tegra_pll_m,	.value = 3},
	{ .input = &tegra_pll_p,	.value = 4},
	{ .input = &tegra_pll_p_out4,	.value = 5},
	{ .input = &tegra_pll_p_out3,	.value = 6},
	{ .input = &tegra_clk_d,	.value = 7},
	{ .input = &tegra_pll_x,	.value = 8},
	{ NULL, 0},
};

static struct clk_mux_sel mux_sclk[] = {
	{ .input = &tegra_clk_m,	.value = 0},
	{ .input = &tegra_pll_c_out1,	.value = 1},
	{ .input = &tegra_pll_p_out4,	.value = 2},
	{ .input = &tegra_pll_p_out3,	.value = 3},
	{ .input = &tegra_pll_p_out2,	.value = 4},
	{ .input = &tegra_clk_d,	.value = 5},
	{ .input = &tegra_clk_32k,	.value = 6},
	{ .input = &tegra_pll_m_out1,	.value = 7},
	{ NULL, 0},
};

static struct clk tegra_clk_cclk = {
	.name	= "cclk",
	.inputs	= mux_cclk,
	.reg	= 0x20,
	.ops	= &tegra_super_ops,
	.max_rate = 1000000000,
};

static struct clk tegra_clk_sclk = {
	.name	= "sclk",
	.inputs	= mux_sclk,
	.reg	= 0x28,
	.ops	= &tegra_super_ops,
	.max_rate = 240000000,
	.min_rate = 120000000,
};

static struct clk tegra_clk_virtual_cpu = {
	.name      = "cpu",
	.parent    = &tegra_clk_cclk,
	.ops       = &tegra_cpu_ops,
	.max_rate  = 1000000000,
	.u.cpu = {
		.main      = &tegra_pll_x,
		.backup    = &tegra_pll_p,
	},
};

static struct clk tegra_clk_cop = {
	.name      = "cop",
	.parent    = &tegra_clk_sclk,
	.ops       = &tegra_cop_ops,
	.max_rate  = 240000000,
};

static struct clk tegra_clk_hclk = {
	.name		= "hclk",
	.flags		= DIV_BUS,
	.parent		= &tegra_clk_sclk,
	.reg		= 0x30,
	.reg_shift	= 4,
	.ops		= &tegra_bus_ops,
	.max_rate       = 240000000,
};

static struct clk tegra_clk_pclk = {
	.name		= "pclk",
	.flags		= DIV_BUS,
	.parent		= &tegra_clk_hclk,
	.reg		= 0x30,
	.reg_shift	= 0,
	.ops		= &tegra_bus_ops,
	.max_rate       = 120000000,
};

static struct clk tegra_clk_blink = {
	.name		= "blink",
	.parent		= &tegra_clk_32k,
	.reg		= 0x40,
	.ops		= &tegra_blink_clk_ops,
	.max_rate	= 32768,
};

static struct clk_mux_sel mux_pllm_pllc_pllp_plla[] = {
	{ .input = &tegra_pll_m, .value = 0},
	{ .input = &tegra_pll_c, .value = 1},
	{ .input = &tegra_pll_p, .value = 2},
	{ .input = &tegra_pll_a_out0, .value = 3},
	{ NULL, 0},
};

static struct clk_mux_sel mux_pllm_pllc_pllp_clkm[] = {
	{ .input = &tegra_pll_m, .value = 0},
	{ .input = &tegra_pll_c, .value = 1},
	{ .input = &tegra_pll_p, .value = 2},
	{ .input = &tegra_clk_m, .value = 3},
	{ NULL, 0},
};

static struct clk_mux_sel mux_pllp_pllc_pllm_clkm[] = {
	{ .input = &tegra_pll_p, .value = 0},
	{ .input = &tegra_pll_c, .value = 1},
	{ .input = &tegra_pll_m, .value = 2},
	{ .input = &tegra_clk_m, .value = 3},
	{ NULL, 0},
};

static struct clk_mux_sel mux_pllaout0_audio2x_pllp_clkm[] = {
	{.input = &tegra_pll_a_out0, .value = 0},
	{.input = &tegra_clk_audio_2x, .value = 1},
	{.input = &tegra_pll_p, .value = 2},
	{.input = &tegra_clk_m, .value = 3},
	{ NULL, 0},
};

static struct clk_mux_sel mux_pllp_plld_pllc_clkm[] = {
	{.input = &tegra_pll_p, .value = 0},
	{.input = &tegra_pll_d_out0, .value = 1},
	{.input = &tegra_pll_c, .value = 2},
	{.input = &tegra_clk_m, .value = 3},
	{ NULL, 0},
};

static struct clk_mux_sel mux_pllp_pllc_audio_clkm_clk32[] = {
	{.input = &tegra_pll_p,     .value = 0},
	{.input = &tegra_pll_c,     .value = 1},
	{.input = &tegra_clk_audio,     .value = 2},
	{.input = &tegra_clk_m,     .value = 3},
	{.input = &tegra_clk_32k,   .value = 4},
	{ NULL, 0},
};

static struct clk_mux_sel mux_pllp_pllc_pllm[] = {
	{.input = &tegra_pll_p,     .value = 0},
	{.input = &tegra_pll_c,     .value = 1},
	{.input = &tegra_pll_m,     .value = 2},
	{ NULL, 0},
};

static struct clk_mux_sel mux_clk_m[] = {
	{ .input = &tegra_clk_m, .value = 0},
	{ NULL, 0},
};

static struct clk_mux_sel mux_pllp_out3[] = {
	{ .input = &tegra_pll_p_out3, .value = 0},
	{ NULL, 0},
};

static struct clk_mux_sel mux_plld[] = {
	{ .input = &tegra_pll_d, .value = 0},
	{ NULL, 0},
};

static struct clk_mux_sel mux_clk_32k[] = {
	{ .input = &tegra_clk_32k, .value = 0},
	{ NULL, 0},
};

static struct clk_mux_sel mux_pclk[] = {
	{ .input = &tegra_clk_pclk, .value = 0},
	{ NULL, 0},
};

static struct clk tegra_clk_emc = {
	.name = "emc",
	.ops = &tegra_emc_clk_ops,
	.reg = 0x19c,
	.max_rate = 800000000,
	.inputs = mux_pllm_pllc_pllp_clkm,
	.flags = MUX | DIV_U71 | PERIPH_EMC_ENB,
	.u.periph = {
		.clk_num = 57,
	},
};

#define PERIPH_CLK(_name, _dev, _con, _clk_num, _reg, _max, _inputs, _flags) \
	{						\
		.name      = _name,			\
		.lookup    = {				\
			.dev_id    = _dev,		\
			.con_id	   = _con,		\
		},					\
		.ops       = &tegra_periph_clk_ops,	\
		.reg       = _reg,			\
		.inputs    = _inputs,			\
		.flags     = _flags,			\
		.max_rate  = _max,			\
		.u.periph = {				\
			.clk_num   = _clk_num,		\
		},					\
	}

#define SHARED_CLK(_name, _dev, _con, _parent)		\
	{						\
		.name      = _name,			\
		.lookup    = {				\
			.dev_id    = _dev,		\
			.con_id    = _con,		\
		},					\
		.ops       = &tegra_clk_shared_bus_ops,	\
		.parent = _parent,			\
	}

static struct clk tegra_list_clks[] = {
	PERIPH_CLK("apbdma",	"tegra-apbdma",		NULL,	34,	0,	108000000, mux_pclk,			0),
	PERIPH_CLK("rtc",	"rtc-tegra",		NULL,	4,	0,	32768,     mux_clk_32k,			PERIPH_NO_RESET),
	PERIPH_CLK("timer",	"timer",		NULL,	5,	0,	26000000,  mux_clk_m,			0),
	PERIPH_CLK("i2s1",	"tegra20-i2s.0",	NULL,	11,	0x100,	26000000,  mux_pllaout0_audio2x_pllp_clkm,	MUX | DIV_U71),
	PERIPH_CLK("i2s2",	"tegra20-i2s.1",	NULL,	18,	0x104,	26000000,  mux_pllaout0_audio2x_pllp_clkm,	MUX | DIV_U71),
	PERIPH_CLK("spdif_out",	"spdif_out",		NULL,	10,	0x108,	100000000, mux_pllaout0_audio2x_pllp_clkm,	MUX | DIV_U71),
	PERIPH_CLK("spdif_in",	"spdif_in",		NULL,	10,	0x10c,	100000000, mux_pllp_pllc_pllm,		MUX | DIV_U71),
	PERIPH_CLK("pwm",	"tegra-pwm",		NULL,	17,	0x110,	432000000, mux_pllp_pllc_audio_clkm_clk32,	MUX | DIV_U71 | MUX_PWM),
	PERIPH_CLK("spi",	"spi",			NULL,	43,	0x114,	40000000,  mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("xio",	"xio",			NULL,	45,	0x120,	150000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("twc",	"twc",			NULL,	16,	0x12c,	150000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("sbc1",	"spi_tegra.0",		NULL,	41,	0x134,	160000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("sbc2",	"spi_tegra.1",		NULL,	44,	0x118,	160000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("sbc3",	"spi_tegra.2",		NULL,	46,	0x11c,	160000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("sbc4",	"spi_tegra.3",		NULL,	68,	0x1b4,	160000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("ide",	"ide",			NULL,	25,	0x144,	100000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71), /* requires min voltage */
	PERIPH_CLK("ndflash",	"tegra_nand",		NULL,	13,	0x160,	164000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71), /* scales with voltage */
	PERIPH_CLK("vfir",	"vfir",			NULL,	7,	0x168,	72000000,  mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("sdmmc1",	"sdhci-tegra.0",	NULL,	14,	0x150,	52000000,  mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71), /* scales with voltage */
	PERIPH_CLK("sdmmc2",	"sdhci-tegra.1",	NULL,	9,	0x154,	52000000,  mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71), /* scales with voltage */
	PERIPH_CLK("sdmmc3",	"sdhci-tegra.2",	NULL,	69,	0x1bc,	52000000,  mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71), /* scales with voltage */
	PERIPH_CLK("sdmmc4",	"sdhci-tegra.3",	NULL,	15,	0x164,	52000000,  mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71), /* scales with voltage */
	PERIPH_CLK("vcp",	"tegra-avp",		"vcp",	29,	0,	250000000, mux_clk_m,			0),
	PERIPH_CLK("bsea",	"tegra-avp",		"bsea",	62,	0,	250000000, mux_clk_m,			0),
	PERIPH_CLK("bsev",	"tegra-aes",		"bsev",	63,	0,	250000000, mux_clk_m,			0),
	PERIPH_CLK("vde",	"tegra-avp",		"vde",	61,	0x1c8,	250000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71), /* scales with voltage and process_id */
	PERIPH_CLK("csite",	"csite",		NULL,	73,	0x1d4,	144000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71), /* max rate ??? */
	/* FIXME: what is la? */
	PERIPH_CLK("la",	"la",			NULL,	76,	0x1f8,	26000000,  mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("owr",	"tegra_w1",		NULL,	71,	0x1cc,	26000000,  mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("nor",	"nor",			NULL,	42,	0x1d0,	92000000,  mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71), /* requires min voltage */
	PERIPH_CLK("mipi",	"mipi",			NULL,	50,	0x174,	60000000,  mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71), /* scales with voltage */
	PERIPH_CLK("i2c1",	"tegra-i2c.0",		NULL,	12,	0x124,	26000000,  mux_pllp_pllc_pllm_clkm,	MUX | DIV_U16),
	PERIPH_CLK("i2c2",	"tegra-i2c.1",		NULL,	54,	0x198,	26000000,  mux_pllp_pllc_pllm_clkm,	MUX | DIV_U16),
	PERIPH_CLK("i2c3",	"tegra-i2c.2",		NULL,	67,	0x1b8,	26000000,  mux_pllp_pllc_pllm_clkm,	MUX | DIV_U16),
	PERIPH_CLK("dvc",	"tegra-i2c.3",		NULL,	47,	0x128,	26000000,  mux_pllp_pllc_pllm_clkm,	MUX | DIV_U16),
	PERIPH_CLK("i2c1_i2c",	"tegra-i2c.0",		"i2c",	0,	0,	72000000,  mux_pllp_out3,			0),
	PERIPH_CLK("i2c2_i2c",	"tegra-i2c.1",		"i2c",	0,	0,	72000000,  mux_pllp_out3,			0),
	PERIPH_CLK("i2c3_i2c",	"tegra-i2c.2",		"i2c",	0,	0,	72000000,  mux_pllp_out3,			0),
	PERIPH_CLK("dvc_i2c",	"tegra-i2c.3",		"i2c",	0,	0,	72000000,  mux_pllp_out3,			0),
	PERIPH_CLK("uarta",	"tegra-uart.0",		NULL,	6,	0x178,	600000000, mux_pllp_pllc_pllm_clkm,	MUX),
	PERIPH_CLK("uartb",	"tegra-uart.1",		NULL,	7,	0x17c,	600000000, mux_pllp_pllc_pllm_clkm,	MUX),
	PERIPH_CLK("uartc",	"tegra-uart.2",		NULL,	55,	0x1a0,	600000000, mux_pllp_pllc_pllm_clkm,	MUX),
	PERIPH_CLK("uartd",	"tegra-uart.3",		NULL,	65,	0x1c0,	600000000, mux_pllp_pllc_pllm_clkm,	MUX),
	PERIPH_CLK("uarte",	"tegra-uart.4",		NULL,	66,	0x1c4,	600000000, mux_pllp_pllc_pllm_clkm,	MUX),
	PERIPH_CLK("3d",	"3d",			NULL,	24,	0x158,	300000000, mux_pllm_pllc_pllp_plla,	MUX | DIV_U71 | PERIPH_MANUAL_RESET), /* scales with voltage and process_id */
	PERIPH_CLK("2d",	"2d",			NULL,	21,	0x15c,	300000000, mux_pllm_pllc_pllp_plla,	MUX | DIV_U71), /* scales with voltage and process_id */
	PERIPH_CLK("vi",	"tegra_camera",		"vi",	20,	0x148,	150000000, mux_pllm_pllc_pllp_plla,	MUX | DIV_U71), /* scales with voltage and process_id */
	PERIPH_CLK("vi_sensor",	"tegra_camera",		"vi_sensor",	20,	0x1a8,	150000000, mux_pllm_pllc_pllp_plla,	MUX | DIV_U71 | PERIPH_NO_RESET), /* scales with voltage and process_id */
	PERIPH_CLK("epp",	"epp",			NULL,	19,	0x16c,	300000000, mux_pllm_pllc_pllp_plla,	MUX | DIV_U71), /* scales with voltage and process_id */
	PERIPH_CLK("mpe",	"mpe",			NULL,	60,	0x170,	250000000, mux_pllm_pllc_pllp_plla,	MUX | DIV_U71), /* scales with voltage and process_id */
	PERIPH_CLK("host1x",	"host1x",		NULL,	28,	0x180,	166000000, mux_pllm_pllc_pllp_plla,	MUX | DIV_U71), /* scales with voltage and process_id */
	PERIPH_CLK("cve",	"cve",			NULL,	49,	0x140,	250000000, mux_pllp_plld_pllc_clkm,	MUX | DIV_U71), /* requires min voltage */
	PERIPH_CLK("tvo",	"tvo",			NULL,	49,	0x188,	250000000, mux_pllp_plld_pllc_clkm,	MUX | DIV_U71), /* requires min voltage */
	PERIPH_CLK("hdmi",	"hdmi",			NULL,	51,	0x18c,	600000000, mux_pllp_plld_pllc_clkm,	MUX | DIV_U71), /* requires min voltage */
	PERIPH_CLK("tvdac",	"tvdac",		NULL,	53,	0x194,	250000000, mux_pllp_plld_pllc_clkm,	MUX | DIV_U71), /* requires min voltage */
	PERIPH_CLK("disp1",	"tegradc.0",		NULL,	27,	0x138,	600000000, mux_pllp_plld_pllc_clkm,	MUX), /* scales with voltage and process_id */
	PERIPH_CLK("disp2",	"tegradc.1",		NULL,	26,	0x13c,	600000000, mux_pllp_plld_pllc_clkm,	MUX), /* scales with voltage and process_id */
	PERIPH_CLK("usbd",	"fsl-tegra-udc",	NULL,	22,	0,	480000000, mux_clk_m,			0), /* requires min voltage */
	PERIPH_CLK("usb2",	"tegra-ehci.1",		NULL,	58,	0,	480000000, mux_clk_m,			0), /* requires min voltage */
	PERIPH_CLK("usb3",	"tegra-ehci.2",		NULL,	59,	0,	480000000, mux_clk_m,			0), /* requires min voltage */
	PERIPH_CLK("dsi",	"dsi",			NULL,	48,	0,	500000000, mux_plld,			0), /* scales with voltage */
	PERIPH_CLK("csi",	"tegra_camera",		"csi",	52,	0,	72000000,  mux_pllp_out3,		0),
	PERIPH_CLK("isp",	"tegra_camera",		"isp",	23,	0,	150000000, mux_clk_m,			0), /* same frequency as VI */
	PERIPH_CLK("csus",	"tegra_camera",		"csus",	92,	0,	150000000, mux_clk_m,			PERIPH_NO_RESET),
	PERIPH_CLK("pex",       NULL,			"pex",  70,     0,	26000000,  mux_clk_m,			PERIPH_MANUAL_RESET),
	PERIPH_CLK("afi",       NULL,			"afi",  72,     0,	26000000,  mux_clk_m,			PERIPH_MANUAL_RESET),
	PERIPH_CLK("pcie_xclk", NULL,		  "pcie_xclk",  74,     0,	26000000,  mux_clk_m,			PERIPH_MANUAL_RESET),

	SHARED_CLK("avp.sclk",	"tegra-avp",		"sclk",	&tegra_clk_sclk),
	SHARED_CLK("avp.emc",	"tegra-avp",		"emc",	&tegra_clk_emc),
	SHARED_CLK("cpu.emc",	"cpu",			"emc",	&tegra_clk_emc),
	SHARED_CLK("disp1.emc",	"tegradc.0",		"emc",	&tegra_clk_emc),
	SHARED_CLK("disp2.emc",	"tegradc.1",		"emc",	&tegra_clk_emc),
	SHARED_CLK("hdmi.emc",	"hdmi",			"emc",	&tegra_clk_emc),
	SHARED_CLK("host.emc",	"tegra_grhost",		"emc",	&tegra_clk_emc),
	SHARED_CLK("usbd.emc",	"fsl-tegra-udc",	"emc",	&tegra_clk_emc),
	SHARED_CLK("usb1.emc",	"tegra-ehci.0",		"emc",	&tegra_clk_emc),
	SHARED_CLK("usb2.emc",	"tegra-ehci.1",		"emc",	&tegra_clk_emc),
	SHARED_CLK("usb3.emc",	"tegra-ehci.2",		"emc",	&tegra_clk_emc),
};

#define CLK_DUPLICATE(_name, _dev, _con)		\
	{						\
		.name	= _name,			\
		.lookup	= {				\
			.dev_id	= _dev,			\
			.con_id		= _con,		\
		},					\
	}

/* Some clocks may be used by different drivers depending on the board
 * configuration.  List those here to register them twice in the clock lookup
 * table under two names.
 */
static struct clk_duplicate tegra_clk_duplicates[] = {
	CLK_DUPLICATE("uarta",	"serial8250.0",	NULL),
	CLK_DUPLICATE("uartb",	"serial8250.1",	NULL),
	CLK_DUPLICATE("uartc",	"serial8250.2",	NULL),
	CLK_DUPLICATE("uartd",	"serial8250.3",	NULL),
	CLK_DUPLICATE("uarte",	"serial8250.4",	NULL),
	CLK_DUPLICATE("usbd", "utmip-pad", NULL),
	CLK_DUPLICATE("usbd", "tegra-ehci.0", NULL),
	CLK_DUPLICATE("usbd", "tegra-otg", NULL),
	CLK_DUPLICATE("hdmi", "tegradc.0", "hdmi"),
	CLK_DUPLICATE("hdmi", "tegradc.1", "hdmi"),
	CLK_DUPLICATE("host1x", "tegra_grhost", "host1x"),
	CLK_DUPLICATE("2d", "tegra_grhost", "gr2d"),
	CLK_DUPLICATE("3d", "tegra_grhost", "gr3d"),
	CLK_DUPLICATE("epp", "tegra_grhost", "epp"),
	CLK_DUPLICATE("mpe", "tegra_grhost", "mpe"),
	CLK_DUPLICATE("cop", "tegra-avp", "cop"),
	CLK_DUPLICATE("vde", "tegra-aes", "vde"),
};

#define CLK(dev, con, ck)	\
	{			\
		.dev_id = dev,	\
		.con_id = con,	\
		.clk = ck,	\
	}

static struct clk *tegra_ptr_clks[] = {
	&tegra_clk_32k,
	&tegra_pll_s,
	&tegra_clk_m,
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
	&tegra_pll_u,
	&tegra_pll_x,
	&tegra_pll_e,
	&tegra_clk_cclk,
	&tegra_clk_sclk,
	&tegra_clk_hclk,
	&tegra_clk_pclk,
	&tegra_clk_d,
	&tegra_clk_cdev1,
	&tegra_clk_cdev2,
	&tegra_clk_virtual_cpu,
	&tegra_clk_blink,
	&tegra_clk_cop,
	&tegra_clk_emc,
};

static void tegra2_init_one_clock(struct clk *c)
{
	clk_init(c);
	INIT_LIST_HEAD(&c->shared_bus_list);
	if (!c->lookup.dev_id && !c->lookup.con_id)
		c->lookup.con_id = c->name;
	c->lookup.clk = c;
	clkdev_add(&c->lookup);
}

void __init tegra2_init_clocks(void)
{
	int i;
	struct clk *c;

	for (i = 0; i < ARRAY_SIZE(tegra_ptr_clks); i++)
		tegra2_init_one_clock(tegra_ptr_clks[i]);

	for (i = 0; i < ARRAY_SIZE(tegra_list_clks); i++)
		tegra2_init_one_clock(&tegra_list_clks[i]);

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

	init_audio_sync_clock_mux();
}
