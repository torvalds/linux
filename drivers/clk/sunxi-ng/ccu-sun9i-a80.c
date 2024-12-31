// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016 Chen-Yu Tsai. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "ccu_common.h"
#include "ccu_reset.h"

#include "ccu_div.h"
#include "ccu_gate.h"
#include "ccu_mp.h"
#include "ccu_nkmp.h"
#include "ccu_nm.h"
#include "ccu_phase.h"

#include "ccu-sun9i-a80.h"

#define CCU_SUN9I_LOCK_REG	0x09c

/*
 * The CPU PLLs are actually NP clocks, with P being /1 or /4. However
 * P should only be used for output frequencies lower than 228 MHz.
 * Neither mainline Linux, U-boot, nor the vendor BSPs use these.
 *
 * For now we can just model it as a multiplier clock, and force P to /1.
 */
#define SUN9I_A80_PLL_C0CPUX_REG	0x000
#define SUN9I_A80_PLL_C1CPUX_REG	0x004

static struct ccu_mult pll_c0cpux_clk = {
	.enable		= BIT(31),
	.lock		= BIT(0),
	.mult		= _SUNXI_CCU_MULT_OFFSET_MIN_MAX(8, 8, 0, 12, 0),
	.common		= {
		.reg		= SUN9I_A80_PLL_C0CPUX_REG,
		.lock_reg	= CCU_SUN9I_LOCK_REG,
		.features	= CCU_FEATURE_LOCK_REG,
		.hw.init	= CLK_HW_INIT("pll-c0cpux", "osc24M",
					      &ccu_mult_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

static struct ccu_mult pll_c1cpux_clk = {
	.enable		= BIT(31),
	.lock		= BIT(1),
	.mult		= _SUNXI_CCU_MULT_OFFSET_MIN_MAX(8, 8, 0, 12, 0),
	.common		= {
		.reg		= SUN9I_A80_PLL_C1CPUX_REG,
		.lock_reg	= CCU_SUN9I_LOCK_REG,
		.features	= CCU_FEATURE_LOCK_REG,
		.hw.init	= CLK_HW_INIT("pll-c1cpux", "osc24M",
					      &ccu_mult_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

/*
 * The Audio PLL has d1, d2 dividers in addition to the usual N, M
 * factors. Since we only need 2 frequencies from this PLL: 22.5792 MHz
 * and 24.576 MHz, ignore them for now. Enforce d1 = 0 and d2 = 0.
 */
#define SUN9I_A80_PLL_AUDIO_REG	0x008

static struct ccu_nm pll_audio_clk = {
	.enable		= BIT(31),
	.lock		= BIT(2),
	.n		= _SUNXI_CCU_MULT_OFFSET_MIN_MAX(8, 8, 0, 12, 0),
	.m		= _SUNXI_CCU_DIV_OFFSET(0, 6, 0),
	.common		= {
		.reg		= 0x008,
		.lock_reg	= CCU_SUN9I_LOCK_REG,
		.features	= CCU_FEATURE_LOCK_REG,
		.hw.init	= CLK_HW_INIT("pll-audio", "osc24M",
					      &ccu_nm_ops, CLK_SET_RATE_UNGATE),
	},
};

/* Some PLLs are input * N / div1 / div2. Model them as NKMP with no K */
static struct ccu_nkmp pll_periph0_clk = {
	.enable		= BIT(31),
	.lock		= BIT(3),
	.n		= _SUNXI_CCU_MULT_OFFSET_MIN_MAX(8, 8, 0, 12, 0),
	.m		= _SUNXI_CCU_DIV(16, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(18, 1), /* output divider */
	.common		= {
		.reg		= 0x00c,
		.lock_reg	= CCU_SUN9I_LOCK_REG,
		.features	= CCU_FEATURE_LOCK_REG,
		.hw.init	= CLK_HW_INIT("pll-periph0", "osc24M",
					      &ccu_nkmp_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

static struct ccu_nkmp pll_ve_clk = {
	.enable		= BIT(31),
	.lock		= BIT(4),
	.n		= _SUNXI_CCU_MULT_OFFSET_MIN_MAX(8, 8, 0, 12, 0),
	.m		= _SUNXI_CCU_DIV(16, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(18, 1), /* output divider */
	.common		= {
		.reg		= 0x010,
		.lock_reg	= CCU_SUN9I_LOCK_REG,
		.features	= CCU_FEATURE_LOCK_REG,
		.hw.init	= CLK_HW_INIT("pll-ve", "osc24M",
					      &ccu_nkmp_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

static struct ccu_nkmp pll_ddr_clk = {
	.enable		= BIT(31),
	.lock		= BIT(5),
	.n		= _SUNXI_CCU_MULT_OFFSET_MIN_MAX(8, 8, 0, 12, 0),
	.m		= _SUNXI_CCU_DIV(16, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(18, 1), /* output divider */
	.common		= {
		.reg		= 0x014,
		.lock_reg	= CCU_SUN9I_LOCK_REG,
		.features	= CCU_FEATURE_LOCK_REG,
		.hw.init	= CLK_HW_INIT("pll-ddr", "osc24M",
					      &ccu_nkmp_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

static struct ccu_nm pll_video0_clk = {
	.enable		= BIT(31),
	.lock		= BIT(6),
	.n		= _SUNXI_CCU_MULT_OFFSET_MIN_MAX(8, 8, 0, 12, 0),
	.m		= _SUNXI_CCU_DIV(16, 1), /* input divider */
	.common		= {
		.reg		= 0x018,
		.lock_reg	= CCU_SUN9I_LOCK_REG,
		.features	= CCU_FEATURE_LOCK_REG,
		.hw.init	= CLK_HW_INIT("pll-video0", "osc24M",
					      &ccu_nm_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

static struct ccu_nkmp pll_video1_clk = {
	.enable		= BIT(31),
	.lock		= BIT(7),
	.n		= _SUNXI_CCU_MULT_OFFSET_MIN_MAX(8, 8, 0, 12, 0),
	.m		= _SUNXI_CCU_DIV(16, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(0, 2), /* external divider p */
	.common		= {
		.reg		= 0x01c,
		.lock_reg	= CCU_SUN9I_LOCK_REG,
		.features	= CCU_FEATURE_LOCK_REG,
		.hw.init	= CLK_HW_INIT("pll-video1", "osc24M",
					      &ccu_nkmp_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

static struct ccu_nkmp pll_gpu_clk = {
	.enable		= BIT(31),
	.lock		= BIT(8),
	.n		= _SUNXI_CCU_MULT_OFFSET_MIN_MAX(8, 8, 0, 12, 0),
	.m		= _SUNXI_CCU_DIV(16, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(18, 1), /* output divider */
	.common		= {
		.reg		= 0x020,
		.lock_reg	= CCU_SUN9I_LOCK_REG,
		.features	= CCU_FEATURE_LOCK_REG,
		.hw.init	= CLK_HW_INIT("pll-gpu", "osc24M",
					      &ccu_nkmp_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

static struct ccu_nkmp pll_de_clk = {
	.enable		= BIT(31),
	.lock		= BIT(9),
	.n		= _SUNXI_CCU_MULT_OFFSET_MIN_MAX(8, 8, 0, 12, 0),
	.m		= _SUNXI_CCU_DIV(16, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(18, 1), /* output divider */
	.common		= {
		.reg		= 0x024,
		.lock_reg	= CCU_SUN9I_LOCK_REG,
		.features	= CCU_FEATURE_LOCK_REG,
		.hw.init	= CLK_HW_INIT("pll-de", "osc24M",
					      &ccu_nkmp_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

static struct ccu_nkmp pll_isp_clk = {
	.enable		= BIT(31),
	.lock		= BIT(10),
	.n		= _SUNXI_CCU_MULT_OFFSET_MIN_MAX(8, 8, 0, 12, 0),
	.m		= _SUNXI_CCU_DIV(16, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(18, 1), /* output divider */
	.common		= {
		.reg		= 0x028,
		.lock_reg	= CCU_SUN9I_LOCK_REG,
		.features	= CCU_FEATURE_LOCK_REG,
		.hw.init	= CLK_HW_INIT("pll-isp", "osc24M",
					      &ccu_nkmp_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

static struct ccu_nkmp pll_periph1_clk = {
	.enable		= BIT(31),
	.lock		= BIT(11),
	.n		= _SUNXI_CCU_MULT_OFFSET_MIN_MAX(8, 8, 0, 12, 0),
	.m		= _SUNXI_CCU_DIV(16, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(18, 1), /* output divider */
	.common		= {
		.reg		= 0x028,
		.lock_reg	= CCU_SUN9I_LOCK_REG,
		.features	= CCU_FEATURE_LOCK_REG,
		.hw.init	= CLK_HW_INIT("pll-periph1", "osc24M",
					      &ccu_nkmp_ops,
					      CLK_SET_RATE_UNGATE),
	},
};

static const char * const c0cpux_parents[] = { "osc24M", "pll-c0cpux" };
static SUNXI_CCU_MUX(c0cpux_clk, "c0cpux", c0cpux_parents,
		     0x50, 0, 1, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL);

static const char * const c1cpux_parents[] = { "osc24M", "pll-c1cpux" };
static SUNXI_CCU_MUX(c1cpux_clk, "c1cpux", c1cpux_parents,
		     0x50, 8, 1, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL);

static struct clk_div_table axi_div_table[] = {
	{ .val = 0, .div = 1 },
	{ .val = 1, .div = 2 },
	{ .val = 2, .div = 3 },
	{ .val = 3, .div = 4 },
	{ .val = 4, .div = 4 },
	{ .val = 5, .div = 4 },
	{ .val = 6, .div = 4 },
	{ .val = 7, .div = 4 },
	{ /* Sentinel */ },
};

static SUNXI_CCU_M(atb0_clk, "atb0", "c0cpux", 0x054, 8, 2, 0);

static SUNXI_CCU_DIV_TABLE(axi0_clk, "axi0", "c0cpux",
			   0x054, 0, 3, axi_div_table, 0);

static SUNXI_CCU_M(atb1_clk, "atb1", "c1cpux", 0x058, 8, 2, 0);

static SUNXI_CCU_DIV_TABLE(axi1_clk, "axi1", "c1cpux",
			   0x058, 0, 3, axi_div_table, 0);

static const char * const gtbus_parents[] = { "osc24M", "pll-periph0",
					      "pll-periph1", "pll-periph1" };
static SUNXI_CCU_M_WITH_MUX(gtbus_clk, "gtbus", gtbus_parents,
			    0x05c, 0, 2, 24, 2, CLK_IS_CRITICAL);

static const char * const ahb_parents[] = { "gtbus", "pll-periph0",
					    "pll-periph1", "pll-periph1" };
static struct ccu_div ahb0_clk = {
	.div		= _SUNXI_CCU_DIV_FLAGS(0, 2, CLK_DIVIDER_POWER_OF_TWO),
	.mux		= _SUNXI_CCU_MUX(24, 2),
	.common		= {
		.reg		= 0x060,
		.hw.init	= CLK_HW_INIT_PARENTS("ahb0",
						      ahb_parents,
						      &ccu_div_ops,
						      0),
	},
};

static struct ccu_div ahb1_clk = {
	.div		= _SUNXI_CCU_DIV_FLAGS(0, 2, CLK_DIVIDER_POWER_OF_TWO),
	.mux		= _SUNXI_CCU_MUX(24, 2),
	.common		= {
		.reg		= 0x064,
		.hw.init	= CLK_HW_INIT_PARENTS("ahb1",
						      ahb_parents,
						      &ccu_div_ops,
						      0),
	},
};

static struct ccu_div ahb2_clk = {
	.div		= _SUNXI_CCU_DIV_FLAGS(0, 2, CLK_DIVIDER_POWER_OF_TWO),
	.mux		= _SUNXI_CCU_MUX(24, 2),
	.common		= {
		.reg		= 0x068,
		.hw.init	= CLK_HW_INIT_PARENTS("ahb2",
						      ahb_parents,
						      &ccu_div_ops,
						      0),
	},
};

static const char * const apb_parents[] = { "osc24M", "pll-periph0" };

static struct ccu_div apb0_clk = {
	.div		= _SUNXI_CCU_DIV_FLAGS(0, 2, CLK_DIVIDER_POWER_OF_TWO),
	.mux		= _SUNXI_CCU_MUX(24, 1),
	.common		= {
		.reg		= 0x070,
		.hw.init	= CLK_HW_INIT_PARENTS("apb0",
						      apb_parents,
						      &ccu_div_ops,
						      0),
	},
};

static struct ccu_div apb1_clk = {
	.div		= _SUNXI_CCU_DIV_FLAGS(0, 2, CLK_DIVIDER_POWER_OF_TWO),
	.mux		= _SUNXI_CCU_MUX(24, 1),
	.common		= {
		.reg		= 0x074,
		.hw.init	= CLK_HW_INIT_PARENTS("apb1",
						      apb_parents,
						      &ccu_div_ops,
						      0),
	},
};

static struct ccu_div cci400_clk = {
	.div		= _SUNXI_CCU_DIV_FLAGS(0, 2, CLK_DIVIDER_POWER_OF_TWO),
	.mux		= _SUNXI_CCU_MUX(24, 2),
	.common		= {
		.reg		= 0x078,
		.hw.init	= CLK_HW_INIT_PARENTS("cci400",
						      ahb_parents,
						      &ccu_div_ops,
						      CLK_IS_CRITICAL),
	},
};

static SUNXI_CCU_M_WITH_MUX_GATE(ats_clk, "ats", apb_parents,
				 0x080, 0, 3, 24, 2, BIT(31), 0);

static SUNXI_CCU_M_WITH_MUX_GATE(trace_clk, "trace", apb_parents,
				 0x084, 0, 3, 24, 2, BIT(31), 0);

static const char * const out_parents[] = { "osc24M", "osc32k", "osc24M" };
static const struct ccu_mux_fixed_prediv out_prediv = {
	.index = 0, .div = 750
};

static struct ccu_mp out_a_clk = {
	.enable		= BIT(31),
	.m		= _SUNXI_CCU_DIV(8, 5),
	.p		= _SUNXI_CCU_DIV(20, 2),
	.mux		= {
		.shift		= 24,
		.width		= 4,
		.fixed_predivs	= &out_prediv,
		.n_predivs	= 1,
	},
	.common		= {
		.reg		= 0x180,
		.features	= CCU_FEATURE_FIXED_PREDIV,
		.hw.init	= CLK_HW_INIT_PARENTS("out-a",
						      out_parents,
						      &ccu_mp_ops,
						      0),
	},
};

static struct ccu_mp out_b_clk = {
	.enable		= BIT(31),
	.m		= _SUNXI_CCU_DIV(8, 5),
	.p		= _SUNXI_CCU_DIV(20, 2),
	.mux		= {
		.shift		= 24,
		.width		= 4,
		.fixed_predivs	= &out_prediv,
		.n_predivs	= 1,
	},
	.common		= {
		.reg		= 0x184,
		.features	= CCU_FEATURE_FIXED_PREDIV,
		.hw.init	= CLK_HW_INIT_PARENTS("out-b",
						      out_parents,
						      &ccu_mp_ops,
						      0),
	},
};

static const char * const mod0_default_parents[] = { "osc24M", "pll-periph0" };

static SUNXI_CCU_MP_WITH_MUX_GATE(nand0_0_clk, "nand0-0", mod0_default_parents,
				  0x400,
				  0, 4,		/* M */
				  16, 2,	/* P */
				  24, 4,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_MP_WITH_MUX_GATE(nand0_1_clk, "nand0-1", mod0_default_parents,
				  0x404,
				  0, 4,		/* M */
				  16, 2,	/* P */
				  24, 4,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_MP_WITH_MUX_GATE(nand1_0_clk, "nand1-0", mod0_default_parents,
				  0x408,
				  0, 4,		/* M */
				  16, 2,	/* P */
				  24, 4,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_MP_WITH_MUX_GATE(nand1_1_clk, "nand1-1", mod0_default_parents,
				  0x40c,
				  0, 4,		/* M */
				  16, 2,	/* P */
				  24, 4,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_MP_WITH_MUX_GATE(mmc0_clk, "mmc0", mod0_default_parents,
				  0x410,
				  0, 4,		/* M */
				  16, 2,	/* P */
				  24, 4,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_PHASE(mmc0_sample_clk, "mmc0-sample", "mmc0",
		       0x410, 20, 3, 0);
static SUNXI_CCU_PHASE(mmc0_output_clk, "mmc0-output", "mmc0",
		       0x410, 8, 3, 0);

static SUNXI_CCU_MP_WITH_MUX_GATE(mmc1_clk, "mmc1", mod0_default_parents,
				  0x414,
				  0, 4,		/* M */
				  16, 2,	/* P */
				  24, 4,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_PHASE(mmc1_sample_clk, "mmc1-sample", "mmc1",
		       0x414, 20, 3, 0);
static SUNXI_CCU_PHASE(mmc1_output_clk, "mmc1-output", "mmc1",
		       0x414, 8, 3, 0);

static SUNXI_CCU_MP_WITH_MUX_GATE(mmc2_clk, "mmc2", mod0_default_parents,
				  0x418,
				  0, 4,		/* M */
				  16, 2,	/* P */
				  24, 4,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_PHASE(mmc2_sample_clk, "mmc2-sample", "mmc2",
		       0x418, 20, 3, 0);
static SUNXI_CCU_PHASE(mmc2_output_clk, "mmc2-output", "mmc2",
		       0x418, 8, 3, 0);

static SUNXI_CCU_MP_WITH_MUX_GATE(mmc3_clk, "mmc3", mod0_default_parents,
				  0x41c,
				  0, 4,		/* M */
				  16, 2,	/* P */
				  24, 4,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_PHASE(mmc3_sample_clk, "mmc3-sample", "mmc3",
		       0x41c, 20, 3, 0);
static SUNXI_CCU_PHASE(mmc3_output_clk, "mmc3-output", "mmc3",
		       0x41c, 8, 3, 0);

static SUNXI_CCU_MP_WITH_MUX_GATE(ts_clk, "ts", mod0_default_parents,
				  0x428,
				  0, 4,		/* M */
				  16, 2,	/* P */
				  24, 4,	/* mux */
				  BIT(31),	/* gate */
				  0);

static const char * const ss_parents[] = { "osc24M", "pll-periph",
					   "pll-periph1" };
static const u8 ss_table[] = { 0, 1, 13 };
static struct ccu_mp ss_clk = {
	.enable		= BIT(31),
	.m		= _SUNXI_CCU_DIV(0, 4),
	.p		= _SUNXI_CCU_DIV(16, 2),
	.mux		= _SUNXI_CCU_MUX_TABLE(24, 4, ss_table),
	.common		= {
		.reg		= 0x42c,
		.hw.init	= CLK_HW_INIT_PARENTS("ss",
						      ss_parents,
						      &ccu_mp_ops,
						      0),
	},
};

static SUNXI_CCU_MP_WITH_MUX_GATE(spi0_clk, "spi0", mod0_default_parents,
				  0x430,
				  0, 4,		/* M */
				  16, 2,	/* P */
				  24, 4,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_MP_WITH_MUX_GATE(spi1_clk, "spi1", mod0_default_parents,
				  0x434,
				  0, 4,		/* M */
				  16, 2,	/* P */
				  24, 4,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_MP_WITH_MUX_GATE(spi2_clk, "spi2", mod0_default_parents,
				  0x438,
				  0, 4,		/* M */
				  16, 2,	/* P */
				  24, 4,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_MP_WITH_MUX_GATE(spi3_clk, "spi3", mod0_default_parents,
				  0x43c,
				  0, 4,		/* M */
				  16, 2,	/* P */
				  24, 4,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_M_WITH_GATE(i2s0_clk, "i2s0", "pll-audio",
			     0x440, 0, 4, BIT(31), CLK_SET_RATE_PARENT);
static SUNXI_CCU_M_WITH_GATE(i2s1_clk, "i2s1", "pll-audio",
			     0x444, 0, 4, BIT(31), CLK_SET_RATE_PARENT);
static SUNXI_CCU_M_WITH_GATE(spdif_clk, "spdif", "pll-audio",
			     0x44c, 0, 4, BIT(31), CLK_SET_RATE_PARENT);

static const char * const sdram_parents[] = { "pll-periph0", "pll-ddr" };
static const u8 sdram_table[] = { 0, 3 };

static SUNXI_CCU_M_WITH_MUX_TABLE_GATE(sdram_clk, "sdram",
				       sdram_parents, sdram_table,
				       0x484,
				       8, 4,	/* M */
				       12, 4,	/* mux */
				       0,	/* no gate */
				       CLK_IS_CRITICAL);

static SUNXI_CCU_M_WITH_GATE(de_clk, "de", "pll-de", 0x490,
			     0, 4, BIT(31), CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(edp_clk, "edp", "osc24M", 0x494, BIT(31), 0);

static const char * const mp_parents[] = { "pll-video1", "pll-gpu", "pll-de" };
static const u8 mp_table[] = { 9, 10, 11 };
static SUNXI_CCU_M_WITH_MUX_TABLE_GATE(mp_clk, "mp", mp_parents, mp_table,
				       0x498,
				       0, 4,	/* M */
				       24, 4,	/* mux */
				       BIT(31),	/* gate */
				       0);

static const char * const display_parents[] = { "pll-video0", "pll-video1" };
static const u8 display_table[] = { 8, 9 };

static SUNXI_CCU_M_WITH_MUX_TABLE_GATE(lcd0_clk, "lcd0",
				       display_parents, display_table,
				       0x49c,
				       0, 4,	/* M */
				       24, 4,	/* mux */
				       BIT(31),	/* gate */
				       CLK_SET_RATE_NO_REPARENT |
				       CLK_SET_RATE_PARENT);

static SUNXI_CCU_M_WITH_MUX_TABLE_GATE(lcd1_clk, "lcd1",
				       display_parents, display_table,
				       0x4a0,
				       0, 4,	/* M */
				       24, 4,	/* mux */
				       BIT(31),	/* gate */
				       CLK_SET_RATE_NO_REPARENT |
				       CLK_SET_RATE_PARENT);

static SUNXI_CCU_M_WITH_MUX_TABLE_GATE(mipi_dsi0_clk, "mipi-dsi0",
				       display_parents, display_table,
				       0x4a8,
				       0, 4,	/* M */
				       24, 4,	/* mux */
				       BIT(31),	/* gate */
				       CLK_SET_RATE_PARENT);

static const char * const mipi_dsi1_parents[] = { "osc24M", "pll-video1" };
static const u8 mipi_dsi1_table[] = { 0, 9 };
static SUNXI_CCU_M_WITH_MUX_TABLE_GATE(mipi_dsi1_clk, "mipi-dsi1",
				       mipi_dsi1_parents, mipi_dsi1_table,
				       0x4ac,
				       0, 4,	/* M */
				       24, 4,	/* mux */
				       BIT(31),	/* gate */
				       CLK_SET_RATE_PARENT);

static SUNXI_CCU_M_WITH_MUX_TABLE_GATE(hdmi_clk, "hdmi",
				       display_parents, display_table,
				       0x4b0,
				       0, 4,	/* M */
				       24, 4,	/* mux */
				       BIT(31),	/* gate */
				       CLK_SET_RATE_NO_REPARENT |
				       CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(hdmi_slow_clk, "hdmi-slow", "osc24M", 0x4b4, BIT(31), 0);

static SUNXI_CCU_M_WITH_GATE(mipi_csi_clk, "mipi-csi", "osc24M", 0x4bc,
			     0, 4, BIT(31), 0);

static SUNXI_CCU_M_WITH_GATE(csi_isp_clk, "csi-isp", "pll-isp", 0x4c0,
			     0, 4, BIT(31), CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(csi_misc_clk, "csi-misc", "osc24M", 0x4c0, BIT(16), 0);

static SUNXI_CCU_M_WITH_MUX_TABLE_GATE(csi0_mclk_clk, "csi0-mclk",
				       mipi_dsi1_parents, mipi_dsi1_table,
				       0x4c4,
				       0, 4,	/* M */
				       24, 4,	/* mux */
				       BIT(31),	/* gate */
				       CLK_SET_RATE_PARENT);

static SUNXI_CCU_M_WITH_MUX_TABLE_GATE(csi1_mclk_clk, "csi1-mclk",
				       mipi_dsi1_parents, mipi_dsi1_table,
				       0x4c8,
				       0, 4,	/* M */
				       24, 4,	/* mux */
				       BIT(31),	/* gate */
				       CLK_SET_RATE_PARENT);

static const char * const fd_parents[] = { "pll-periph0", "pll-isp" };
static const u8 fd_table[] = { 1, 12 };
static SUNXI_CCU_M_WITH_MUX_TABLE_GATE(fd_clk, "fd", fd_parents, fd_table,
				       0x4cc,
				       0, 4,	/* M */
				       24, 4,	/* mux */
				       BIT(31),	/* gate */
				       0);
static SUNXI_CCU_M_WITH_GATE(ve_clk, "ve", "pll-ve", 0x4d0,
			     16, 3, BIT(31), CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE(avs_clk, "avs", "osc24M", 0x4d4, BIT(31), 0);

static SUNXI_CCU_M_WITH_GATE(gpu_core_clk, "gpu-core", "pll-gpu", 0x4f0,
			     0, 3, BIT(31), CLK_SET_RATE_PARENT);
static SUNXI_CCU_M_WITH_GATE(gpu_memory_clk, "gpu-memory", "pll-gpu", 0x4f4,
			     0, 3, BIT(31), CLK_SET_RATE_PARENT);

static const char * const gpu_axi_parents[] = { "pll-periph0", "pll-gpu" };
static const u8 gpu_axi_table[] = { 1, 10 };
static SUNXI_CCU_M_WITH_MUX_TABLE_GATE(gpu_axi_clk, "gpu-axi",
				       gpu_axi_parents, gpu_axi_table,
				       0x4f8,
				       0, 4,	/* M */
				       24, 4,	/* mux */
				       BIT(31),	/* gate */
				       CLK_SET_RATE_PARENT);

static SUNXI_CCU_M_WITH_GATE(sata_clk, "sata", "pll-periph0", 0x500,
			     0, 4, BIT(31), 0);

static SUNXI_CCU_M_WITH_GATE(ac97_clk, "ac97", "pll-audio",
			     0x504, 0, 4, BIT(31), CLK_SET_RATE_PARENT);

static SUNXI_CCU_M_WITH_MUX_GATE(mipi_hsi_clk, "mipi-hsi",
				 mod0_default_parents, 0x508,
				 0, 4,		/* M */
				 24, 4,		/* mux */
				 BIT(31),	/* gate */
				 0);

static const char * const gpadc_parents[] = { "osc24M", "pll-audio", "osc32k" };
static const u8 gpadc_table[] = { 0, 4, 7 };
static struct ccu_mp gpadc_clk = {
	.enable		= BIT(31),
	.m		= _SUNXI_CCU_DIV(0, 4),
	.p		= _SUNXI_CCU_DIV(16, 2),
	.mux		= _SUNXI_CCU_MUX_TABLE(24, 4, gpadc_table),
	.common		= {
		.reg		= 0x50c,
		.hw.init	= CLK_HW_INIT_PARENTS("gpadc",
						      gpadc_parents,
						      &ccu_mp_ops,
						      0),
	},
};

static const char * const cir_tx_parents[] = { "osc24M", "osc32k" };
static const u8 cir_tx_table[] = { 0, 7 };
static struct ccu_mp cir_tx_clk = {
	.enable		= BIT(31),
	.m		= _SUNXI_CCU_DIV(0, 4),
	.p		= _SUNXI_CCU_DIV(16, 2),
	.mux		= _SUNXI_CCU_MUX_TABLE(24, 4, cir_tx_table),
	.common		= {
		.reg		= 0x510,
		.hw.init	= CLK_HW_INIT_PARENTS("cir-tx",
						      cir_tx_parents,
						      &ccu_mp_ops,
						      0),
	},
};

/* AHB0 bus gates */
static SUNXI_CCU_GATE(bus_fd_clk,	"bus-fd",	"ahb0",
		      0x580, BIT(0), 0);
static SUNXI_CCU_GATE(bus_ve_clk,	"bus-ve",	"ahb0",
		      0x580, BIT(1), 0);
static SUNXI_CCU_GATE(bus_gpu_ctrl_clk,	"bus-gpu-ctrl",	"ahb0",
		      0x580, BIT(3), 0);
static SUNXI_CCU_GATE(bus_ss_clk,	"bus-ss",	"ahb0",
		      0x580, BIT(5), 0);
static SUNXI_CCU_GATE(bus_mmc_clk,	"bus-mmc",	"ahb0",
		      0x580, BIT(8), 0);
static SUNXI_CCU_GATE(bus_nand0_clk,	"bus-nand0",	"ahb0",
		      0x580, BIT(12), 0);
static SUNXI_CCU_GATE(bus_nand1_clk,	"bus-nand1",	"ahb0",
		      0x580, BIT(13), 0);
static SUNXI_CCU_GATE(bus_sdram_clk,	"bus-sdram",	"ahb0",
		      0x580, BIT(14), 0);
static SUNXI_CCU_GATE(bus_mipi_hsi_clk,	"bus-mipi-hsi",	"ahb0",
		      0x580, BIT(15), 0);
static SUNXI_CCU_GATE(bus_sata_clk,	"bus-sata",	"ahb0",
		      0x580, BIT(16), 0);
static SUNXI_CCU_GATE(bus_ts_clk,	"bus-ts",	"ahb0",
		      0x580, BIT(18), 0);
static SUNXI_CCU_GATE(bus_spi0_clk,	"bus-spi0",	"ahb0",
		      0x580, BIT(20), 0);
static SUNXI_CCU_GATE(bus_spi1_clk,	"bus-spi1",	"ahb0",
		      0x580, BIT(21), 0);
static SUNXI_CCU_GATE(bus_spi2_clk,	"bus-spi2",	"ahb0",
		      0x580, BIT(22), 0);
static SUNXI_CCU_GATE(bus_spi3_clk,	"bus-spi3",	"ahb0",
		      0x580, BIT(23), 0);

/* AHB1 bus gates */
static SUNXI_CCU_GATE(bus_otg_clk,	"bus-otg",	"ahb1",
		      0x584, BIT(0), 0);
static SUNXI_CCU_GATE(bus_usb_clk,	"bus-usb",	"ahb1",
		      0x584, BIT(1), 0);
static SUNXI_CCU_GATE(bus_gmac_clk,	"bus-gmac",	"ahb1",
		      0x584, BIT(17), 0);
static SUNXI_CCU_GATE(bus_msgbox_clk,	"bus-msgbox",	"ahb1",
		      0x584, BIT(21), 0);
static SUNXI_CCU_GATE(bus_spinlock_clk,	"bus-spinlock",	"ahb1",
		      0x584, BIT(22), 0);
static SUNXI_CCU_GATE(bus_hstimer_clk,	"bus-hstimer",	"ahb1",
		      0x584, BIT(23), 0);
static SUNXI_CCU_GATE(bus_dma_clk,	"bus-dma",	"ahb1",
		      0x584, BIT(24), 0);

/* AHB2 bus gates */
static SUNXI_CCU_GATE(bus_lcd0_clk,	"bus-lcd0",	"ahb2",
		      0x588, BIT(0), 0);
static SUNXI_CCU_GATE(bus_lcd1_clk,	"bus-lcd1",	"ahb2",
		      0x588, BIT(1), 0);
static SUNXI_CCU_GATE(bus_edp_clk,	"bus-edp",	"ahb2",
		      0x588, BIT(2), 0);
static SUNXI_CCU_GATE(bus_csi_clk,	"bus-csi",	"ahb2",
		      0x588, BIT(4), 0);
static SUNXI_CCU_GATE(bus_hdmi_clk,	"bus-hdmi",	"ahb2",
		      0x588, BIT(5), 0);
static SUNXI_CCU_GATE(bus_de_clk,	"bus-de",	"ahb2",
		      0x588, BIT(7), 0);
static SUNXI_CCU_GATE(bus_mp_clk,	"bus-mp",	"ahb2",
		      0x588, BIT(8), 0);
static SUNXI_CCU_GATE(bus_mipi_dsi_clk,	"bus-mipi-dsi",	"ahb2",
		      0x588, BIT(11), 0);

/* APB0 bus gates */
static SUNXI_CCU_GATE(bus_spdif_clk,	"bus-spdif",	"apb0",
		      0x590, BIT(1), 0);
static SUNXI_CCU_GATE(bus_pio_clk,	"bus-pio",	"apb0",
		      0x590, BIT(5), 0);
static SUNXI_CCU_GATE(bus_ac97_clk,	"bus-ac97",	"apb0",
		      0x590, BIT(11), 0);
static SUNXI_CCU_GATE(bus_i2s0_clk,	"bus-i2s0",	"apb0",
		      0x590, BIT(12), 0);
static SUNXI_CCU_GATE(bus_i2s1_clk,	"bus-i2s1",	"apb0",
		      0x590, BIT(13), 0);
static SUNXI_CCU_GATE(bus_lradc_clk,	"bus-lradc",	"apb0",
		      0x590, BIT(15), 0);
static SUNXI_CCU_GATE(bus_gpadc_clk,	"bus-gpadc",	"apb0",
		      0x590, BIT(17), 0);
static SUNXI_CCU_GATE(bus_twd_clk,	"bus-twd",	"apb0",
		      0x590, BIT(18), 0);
static SUNXI_CCU_GATE(bus_cir_tx_clk,	"bus-cir-tx",	"apb0",
		      0x590, BIT(19), 0);

/* APB1 bus gates */
static SUNXI_CCU_GATE(bus_i2c0_clk,	"bus-i2c0",	"apb1",
		      0x594, BIT(0), 0);
static SUNXI_CCU_GATE(bus_i2c1_clk,	"bus-i2c1",	"apb1",
		      0x594, BIT(1), 0);
static SUNXI_CCU_GATE(bus_i2c2_clk,	"bus-i2c2",	"apb1",
		      0x594, BIT(2), 0);
static SUNXI_CCU_GATE(bus_i2c3_clk,	"bus-i2c3",	"apb1",
		      0x594, BIT(3), 0);
static SUNXI_CCU_GATE(bus_i2c4_clk,	"bus-i2c4",	"apb1",
		      0x594, BIT(4), 0);
static SUNXI_CCU_GATE(bus_uart0_clk,	"bus-uart0",	"apb1",
		      0x594, BIT(16), 0);
static SUNXI_CCU_GATE(bus_uart1_clk,	"bus-uart1",	"apb1",
		      0x594, BIT(17), 0);
static SUNXI_CCU_GATE(bus_uart2_clk,	"bus-uart2",	"apb1",
		      0x594, BIT(18), 0);
static SUNXI_CCU_GATE(bus_uart3_clk,	"bus-uart3",	"apb1",
		      0x594, BIT(19), 0);
static SUNXI_CCU_GATE(bus_uart4_clk,	"bus-uart4",	"apb1",
		      0x594, BIT(20), 0);
static SUNXI_CCU_GATE(bus_uart5_clk,	"bus-uart5",	"apb1",
		      0x594, BIT(21), 0);

static struct ccu_common *sun9i_a80_ccu_clks[] = {
	&pll_c0cpux_clk.common,
	&pll_c1cpux_clk.common,
	&pll_audio_clk.common,
	&pll_periph0_clk.common,
	&pll_ve_clk.common,
	&pll_ddr_clk.common,
	&pll_video0_clk.common,
	&pll_video1_clk.common,
	&pll_gpu_clk.common,
	&pll_de_clk.common,
	&pll_isp_clk.common,
	&pll_periph1_clk.common,
	&c0cpux_clk.common,
	&c1cpux_clk.common,
	&atb0_clk.common,
	&axi0_clk.common,
	&atb1_clk.common,
	&axi1_clk.common,
	&gtbus_clk.common,
	&ahb0_clk.common,
	&ahb1_clk.common,
	&ahb2_clk.common,
	&apb0_clk.common,
	&apb1_clk.common,
	&cci400_clk.common,
	&ats_clk.common,
	&trace_clk.common,

	&out_a_clk.common,
	&out_b_clk.common,

	/* module clocks */
	&nand0_0_clk.common,
	&nand0_1_clk.common,
	&nand1_0_clk.common,
	&nand1_1_clk.common,
	&mmc0_clk.common,
	&mmc0_sample_clk.common,
	&mmc0_output_clk.common,
	&mmc1_clk.common,
	&mmc1_sample_clk.common,
	&mmc1_output_clk.common,
	&mmc2_clk.common,
	&mmc2_sample_clk.common,
	&mmc2_output_clk.common,
	&mmc3_clk.common,
	&mmc3_sample_clk.common,
	&mmc3_output_clk.common,
	&ts_clk.common,
	&ss_clk.common,
	&spi0_clk.common,
	&spi1_clk.common,
	&spi2_clk.common,
	&spi3_clk.common,
	&i2s0_clk.common,
	&i2s1_clk.common,
	&spdif_clk.common,
	&sdram_clk.common,
	&de_clk.common,
	&edp_clk.common,
	&mp_clk.common,
	&lcd0_clk.common,
	&lcd1_clk.common,
	&mipi_dsi0_clk.common,
	&mipi_dsi1_clk.common,
	&hdmi_clk.common,
	&hdmi_slow_clk.common,
	&mipi_csi_clk.common,
	&csi_isp_clk.common,
	&csi_misc_clk.common,
	&csi0_mclk_clk.common,
	&csi1_mclk_clk.common,
	&fd_clk.common,
	&ve_clk.common,
	&avs_clk.common,
	&gpu_core_clk.common,
	&gpu_memory_clk.common,
	&gpu_axi_clk.common,
	&sata_clk.common,
	&ac97_clk.common,
	&mipi_hsi_clk.common,
	&gpadc_clk.common,
	&cir_tx_clk.common,

	/* AHB0 bus gates */
	&bus_fd_clk.common,
	&bus_ve_clk.common,
	&bus_gpu_ctrl_clk.common,
	&bus_ss_clk.common,
	&bus_mmc_clk.common,
	&bus_nand0_clk.common,
	&bus_nand1_clk.common,
	&bus_sdram_clk.common,
	&bus_mipi_hsi_clk.common,
	&bus_sata_clk.common,
	&bus_ts_clk.common,
	&bus_spi0_clk.common,
	&bus_spi1_clk.common,
	&bus_spi2_clk.common,
	&bus_spi3_clk.common,

	/* AHB1 bus gates */
	&bus_otg_clk.common,
	&bus_usb_clk.common,
	&bus_gmac_clk.common,
	&bus_msgbox_clk.common,
	&bus_spinlock_clk.common,
	&bus_hstimer_clk.common,
	&bus_dma_clk.common,

	/* AHB2 bus gates */
	&bus_lcd0_clk.common,
	&bus_lcd1_clk.common,
	&bus_edp_clk.common,
	&bus_csi_clk.common,
	&bus_hdmi_clk.common,
	&bus_de_clk.common,
	&bus_mp_clk.common,
	&bus_mipi_dsi_clk.common,

	/* APB0 bus gates */
	&bus_spdif_clk.common,
	&bus_pio_clk.common,
	&bus_ac97_clk.common,
	&bus_i2s0_clk.common,
	&bus_i2s1_clk.common,
	&bus_lradc_clk.common,
	&bus_gpadc_clk.common,
	&bus_twd_clk.common,
	&bus_cir_tx_clk.common,

	/* APB1 bus gates */
	&bus_i2c0_clk.common,
	&bus_i2c1_clk.common,
	&bus_i2c2_clk.common,
	&bus_i2c3_clk.common,
	&bus_i2c4_clk.common,
	&bus_uart0_clk.common,
	&bus_uart1_clk.common,
	&bus_uart2_clk.common,
	&bus_uart3_clk.common,
	&bus_uart4_clk.common,
	&bus_uart5_clk.common,
};

static struct clk_hw_onecell_data sun9i_a80_hw_clks = {
	.hws	= {
		[CLK_PLL_C0CPUX]	= &pll_c0cpux_clk.common.hw,
		[CLK_PLL_C1CPUX]	= &pll_c1cpux_clk.common.hw,
		[CLK_PLL_AUDIO]		= &pll_audio_clk.common.hw,
		[CLK_PLL_PERIPH0]	= &pll_periph0_clk.common.hw,
		[CLK_PLL_VE]		= &pll_ve_clk.common.hw,
		[CLK_PLL_DDR]		= &pll_ddr_clk.common.hw,
		[CLK_PLL_VIDEO0]	= &pll_video0_clk.common.hw,
		[CLK_PLL_VIDEO1]	= &pll_video1_clk.common.hw,
		[CLK_PLL_GPU]		= &pll_gpu_clk.common.hw,
		[CLK_PLL_DE]		= &pll_de_clk.common.hw,
		[CLK_PLL_ISP]		= &pll_isp_clk.common.hw,
		[CLK_PLL_PERIPH1]	= &pll_periph1_clk.common.hw,
		[CLK_C0CPUX]		= &c0cpux_clk.common.hw,
		[CLK_C1CPUX]		= &c1cpux_clk.common.hw,
		[CLK_ATB0]		= &atb0_clk.common.hw,
		[CLK_AXI0]		= &axi0_clk.common.hw,
		[CLK_ATB1]		= &atb1_clk.common.hw,
		[CLK_AXI1]		= &axi1_clk.common.hw,
		[CLK_GTBUS]		= &gtbus_clk.common.hw,
		[CLK_AHB0]		= &ahb0_clk.common.hw,
		[CLK_AHB1]		= &ahb1_clk.common.hw,
		[CLK_AHB2]		= &ahb2_clk.common.hw,
		[CLK_APB0]		= &apb0_clk.common.hw,
		[CLK_APB1]		= &apb1_clk.common.hw,
		[CLK_CCI400]		= &cci400_clk.common.hw,
		[CLK_ATS]		= &ats_clk.common.hw,
		[CLK_TRACE]		= &trace_clk.common.hw,

		[CLK_OUT_A]		= &out_a_clk.common.hw,
		[CLK_OUT_B]		= &out_b_clk.common.hw,

		[CLK_NAND0_0]		= &nand0_0_clk.common.hw,
		[CLK_NAND0_1]		= &nand0_1_clk.common.hw,
		[CLK_NAND1_0]		= &nand1_0_clk.common.hw,
		[CLK_NAND1_1]		= &nand1_1_clk.common.hw,
		[CLK_MMC0]		= &mmc0_clk.common.hw,
		[CLK_MMC0_SAMPLE]	= &mmc0_sample_clk.common.hw,
		[CLK_MMC0_OUTPUT]	= &mmc0_output_clk.common.hw,
		[CLK_MMC1]		= &mmc1_clk.common.hw,
		[CLK_MMC1_SAMPLE]	= &mmc1_sample_clk.common.hw,
		[CLK_MMC1_OUTPUT]	= &mmc1_output_clk.common.hw,
		[CLK_MMC2]		= &mmc2_clk.common.hw,
		[CLK_MMC2_SAMPLE]	= &mmc2_sample_clk.common.hw,
		[CLK_MMC2_OUTPUT]	= &mmc2_output_clk.common.hw,
		[CLK_MMC3]		= &mmc3_clk.common.hw,
		[CLK_MMC3_SAMPLE]	= &mmc3_sample_clk.common.hw,
		[CLK_MMC3_OUTPUT]	= &mmc3_output_clk.common.hw,
		[CLK_TS]		= &ts_clk.common.hw,
		[CLK_SS]		= &ss_clk.common.hw,
		[CLK_SPI0]		= &spi0_clk.common.hw,
		[CLK_SPI1]		= &spi1_clk.common.hw,
		[CLK_SPI2]		= &spi2_clk.common.hw,
		[CLK_SPI3]		= &spi3_clk.common.hw,
		[CLK_I2S0]		= &i2s0_clk.common.hw,
		[CLK_I2S1]		= &i2s1_clk.common.hw,
		[CLK_SPDIF]		= &spdif_clk.common.hw,
		[CLK_SDRAM]		= &sdram_clk.common.hw,
		[CLK_DE]		= &de_clk.common.hw,
		[CLK_EDP]		= &edp_clk.common.hw,
		[CLK_MP]		= &mp_clk.common.hw,
		[CLK_LCD0]		= &lcd0_clk.common.hw,
		[CLK_LCD1]		= &lcd1_clk.common.hw,
		[CLK_MIPI_DSI0]		= &mipi_dsi0_clk.common.hw,
		[CLK_MIPI_DSI1]		= &mipi_dsi1_clk.common.hw,
		[CLK_HDMI]		= &hdmi_clk.common.hw,
		[CLK_HDMI_SLOW]		= &hdmi_slow_clk.common.hw,
		[CLK_MIPI_CSI]		= &mipi_csi_clk.common.hw,
		[CLK_CSI_ISP]		= &csi_isp_clk.common.hw,
		[CLK_CSI_MISC]		= &csi_misc_clk.common.hw,
		[CLK_CSI0_MCLK]		= &csi0_mclk_clk.common.hw,
		[CLK_CSI1_MCLK]		= &csi1_mclk_clk.common.hw,
		[CLK_FD]		= &fd_clk.common.hw,
		[CLK_VE]		= &ve_clk.common.hw,
		[CLK_AVS]		= &avs_clk.common.hw,
		[CLK_GPU_CORE]		= &gpu_core_clk.common.hw,
		[CLK_GPU_MEMORY]	= &gpu_memory_clk.common.hw,
		[CLK_GPU_AXI]		= &gpu_axi_clk.common.hw,
		[CLK_SATA]		= &sata_clk.common.hw,
		[CLK_AC97]		= &ac97_clk.common.hw,
		[CLK_MIPI_HSI]		= &mipi_hsi_clk.common.hw,
		[CLK_GPADC]		= &gpadc_clk.common.hw,
		[CLK_CIR_TX]		= &cir_tx_clk.common.hw,

		[CLK_BUS_FD]		= &bus_fd_clk.common.hw,
		[CLK_BUS_VE]		= &bus_ve_clk.common.hw,
		[CLK_BUS_GPU_CTRL]	= &bus_gpu_ctrl_clk.common.hw,
		[CLK_BUS_SS]		= &bus_ss_clk.common.hw,
		[CLK_BUS_MMC]		= &bus_mmc_clk.common.hw,
		[CLK_BUS_NAND0]		= &bus_nand0_clk.common.hw,
		[CLK_BUS_NAND1]		= &bus_nand1_clk.common.hw,
		[CLK_BUS_SDRAM]		= &bus_sdram_clk.common.hw,
		[CLK_BUS_MIPI_HSI]	= &bus_mipi_hsi_clk.common.hw,
		[CLK_BUS_SATA]		= &bus_sata_clk.common.hw,
		[CLK_BUS_TS]		= &bus_ts_clk.common.hw,
		[CLK_BUS_SPI0]		= &bus_spi0_clk.common.hw,
		[CLK_BUS_SPI1]		= &bus_spi1_clk.common.hw,
		[CLK_BUS_SPI2]		= &bus_spi2_clk.common.hw,
		[CLK_BUS_SPI3]		= &bus_spi3_clk.common.hw,

		[CLK_BUS_OTG]		= &bus_otg_clk.common.hw,
		[CLK_BUS_USB]		= &bus_usb_clk.common.hw,
		[CLK_BUS_GMAC]		= &bus_gmac_clk.common.hw,
		[CLK_BUS_MSGBOX]	= &bus_msgbox_clk.common.hw,
		[CLK_BUS_SPINLOCK]	= &bus_spinlock_clk.common.hw,
		[CLK_BUS_HSTIMER]	= &bus_hstimer_clk.common.hw,
		[CLK_BUS_DMA]		= &bus_dma_clk.common.hw,

		[CLK_BUS_LCD0]		= &bus_lcd0_clk.common.hw,
		[CLK_BUS_LCD1]		= &bus_lcd1_clk.common.hw,
		[CLK_BUS_EDP]		= &bus_edp_clk.common.hw,
		[CLK_BUS_CSI]		= &bus_csi_clk.common.hw,
		[CLK_BUS_HDMI]		= &bus_hdmi_clk.common.hw,
		[CLK_BUS_DE]		= &bus_de_clk.common.hw,
		[CLK_BUS_MP]		= &bus_mp_clk.common.hw,
		[CLK_BUS_MIPI_DSI]	= &bus_mipi_dsi_clk.common.hw,

		[CLK_BUS_SPDIF]		= &bus_spdif_clk.common.hw,
		[CLK_BUS_PIO]		= &bus_pio_clk.common.hw,
		[CLK_BUS_AC97]		= &bus_ac97_clk.common.hw,
		[CLK_BUS_I2S0]		= &bus_i2s0_clk.common.hw,
		[CLK_BUS_I2S1]		= &bus_i2s1_clk.common.hw,
		[CLK_BUS_LRADC]		= &bus_lradc_clk.common.hw,
		[CLK_BUS_GPADC]		= &bus_gpadc_clk.common.hw,
		[CLK_BUS_TWD]		= &bus_twd_clk.common.hw,
		[CLK_BUS_CIR_TX]	= &bus_cir_tx_clk.common.hw,

		[CLK_BUS_I2C0]		= &bus_i2c0_clk.common.hw,
		[CLK_BUS_I2C1]		= &bus_i2c1_clk.common.hw,
		[CLK_BUS_I2C2]		= &bus_i2c2_clk.common.hw,
		[CLK_BUS_I2C3]		= &bus_i2c3_clk.common.hw,
		[CLK_BUS_I2C4]		= &bus_i2c4_clk.common.hw,
		[CLK_BUS_UART0]		= &bus_uart0_clk.common.hw,
		[CLK_BUS_UART1]		= &bus_uart1_clk.common.hw,
		[CLK_BUS_UART2]		= &bus_uart2_clk.common.hw,
		[CLK_BUS_UART3]		= &bus_uart3_clk.common.hw,
		[CLK_BUS_UART4]		= &bus_uart4_clk.common.hw,
		[CLK_BUS_UART5]		= &bus_uart5_clk.common.hw,
	},
	.num	= CLK_NUMBER,
};

static const struct ccu_reset_map sun9i_a80_ccu_resets[] = {
	/* AHB0 reset controls */
	[RST_BUS_FD]		= { 0x5a0, BIT(0) },
	[RST_BUS_VE]		= { 0x5a0, BIT(1) },
	[RST_BUS_GPU_CTRL]	= { 0x5a0, BIT(3) },
	[RST_BUS_SS]		= { 0x5a0, BIT(5) },
	[RST_BUS_MMC]		= { 0x5a0, BIT(8) },
	[RST_BUS_NAND0]		= { 0x5a0, BIT(12) },
	[RST_BUS_NAND1]		= { 0x5a0, BIT(13) },
	[RST_BUS_SDRAM]		= { 0x5a0, BIT(14) },
	[RST_BUS_SATA]		= { 0x5a0, BIT(16) },
	[RST_BUS_TS]		= { 0x5a0, BIT(18) },
	[RST_BUS_SPI0]		= { 0x5a0, BIT(20) },
	[RST_BUS_SPI1]		= { 0x5a0, BIT(21) },
	[RST_BUS_SPI2]		= { 0x5a0, BIT(22) },
	[RST_BUS_SPI3]		= { 0x5a0, BIT(23) },

	/* AHB1 reset controls */
	[RST_BUS_OTG]		= { 0x5a4, BIT(0) },
	[RST_BUS_OTG_PHY]	= { 0x5a4, BIT(1) },
	[RST_BUS_MIPI_HSI]	= { 0x5a4, BIT(9) },
	[RST_BUS_GMAC]		= { 0x5a4, BIT(17) },
	[RST_BUS_MSGBOX]	= { 0x5a4, BIT(21) },
	[RST_BUS_SPINLOCK]	= { 0x5a4, BIT(22) },
	[RST_BUS_HSTIMER]	= { 0x5a4, BIT(23) },
	[RST_BUS_DMA]		= { 0x5a4, BIT(24) },

	/* AHB2 reset controls */
	[RST_BUS_LCD0]		= { 0x5a8, BIT(0) },
	[RST_BUS_LCD1]		= { 0x5a8, BIT(1) },
	[RST_BUS_EDP]		= { 0x5a8, BIT(2) },
	[RST_BUS_LVDS]		= { 0x5a8, BIT(3) },
	[RST_BUS_CSI]		= { 0x5a8, BIT(4) },
	[RST_BUS_HDMI0]		= { 0x5a8, BIT(5) },
	[RST_BUS_HDMI1]		= { 0x5a8, BIT(6) },
	[RST_BUS_DE]		= { 0x5a8, BIT(7) },
	[RST_BUS_MP]		= { 0x5a8, BIT(8) },
	[RST_BUS_GPU]		= { 0x5a8, BIT(9) },
	[RST_BUS_MIPI_DSI]	= { 0x5a8, BIT(11) },

	/* APB0 reset controls */
	[RST_BUS_SPDIF]		= { 0x5b0, BIT(1) },
	[RST_BUS_AC97]		= { 0x5b0, BIT(11) },
	[RST_BUS_I2S0]		= { 0x5b0, BIT(12) },
	[RST_BUS_I2S1]		= { 0x5b0, BIT(13) },
	[RST_BUS_LRADC]		= { 0x5b0, BIT(15) },
	[RST_BUS_GPADC]		= { 0x5b0, BIT(17) },
	[RST_BUS_CIR_TX]	= { 0x5b0, BIT(19) },

	/* APB1 reset controls */
	[RST_BUS_I2C0]		= { 0x5b4, BIT(0) },
	[RST_BUS_I2C1]		= { 0x5b4, BIT(1) },
	[RST_BUS_I2C2]		= { 0x5b4, BIT(2) },
	[RST_BUS_I2C3]		= { 0x5b4, BIT(3) },
	[RST_BUS_I2C4]		= { 0x5b4, BIT(4) },
	[RST_BUS_UART0]		= { 0x5b4, BIT(16) },
	[RST_BUS_UART1]		= { 0x5b4, BIT(17) },
	[RST_BUS_UART2]		= { 0x5b4, BIT(18) },
	[RST_BUS_UART3]		= { 0x5b4, BIT(19) },
	[RST_BUS_UART4]		= { 0x5b4, BIT(20) },
	[RST_BUS_UART5]		= { 0x5b4, BIT(21) },
};

static const struct sunxi_ccu_desc sun9i_a80_ccu_desc = {
	.ccu_clks	= sun9i_a80_ccu_clks,
	.num_ccu_clks	= ARRAY_SIZE(sun9i_a80_ccu_clks),

	.hw_clks	= &sun9i_a80_hw_clks,

	.resets		= sun9i_a80_ccu_resets,
	.num_resets	= ARRAY_SIZE(sun9i_a80_ccu_resets),
};

#define SUN9I_A80_PLL_P_SHIFT	16
#define SUN9I_A80_PLL_N_SHIFT	8
#define SUN9I_A80_PLL_N_WIDTH	8

static void sun9i_a80_cpu_pll_fixup(void __iomem *reg)
{
	u32 val = readl(reg);

	/* bail out if P divider is not used */
	if (!(val & BIT(SUN9I_A80_PLL_P_SHIFT)))
		return;

	/*
	 * If P is used, output should be less than 288 MHz. When we
	 * set P to 1, we should also decrease the multiplier so the
	 * output doesn't go out of range, but not too much such that
	 * the multiplier stays above 12, the minimal operation value.
	 *
	 * To keep it simple, set the multiplier to 17, the reset value.
	 */
	val &= ~GENMASK(SUN9I_A80_PLL_N_SHIFT + SUN9I_A80_PLL_N_WIDTH - 1,
			SUN9I_A80_PLL_N_SHIFT);
	val |= 17 << SUN9I_A80_PLL_N_SHIFT;

	/* And clear P */
	val &= ~BIT(SUN9I_A80_PLL_P_SHIFT);

	writel(val, reg);
}

static int sun9i_a80_ccu_probe(struct platform_device *pdev)
{
	void __iomem *reg;
	u32 val;

	reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	/* Enforce d1 = 0, d2 = 0 for Audio PLL */
	val = readl(reg + SUN9I_A80_PLL_AUDIO_REG);
	val &= ~(BIT(16) | BIT(18));
	writel(val, reg + SUN9I_A80_PLL_AUDIO_REG);

	/* Enforce P = 1 for both CPU cluster PLLs */
	sun9i_a80_cpu_pll_fixup(reg + SUN9I_A80_PLL_C0CPUX_REG);
	sun9i_a80_cpu_pll_fixup(reg + SUN9I_A80_PLL_C1CPUX_REG);

	return devm_sunxi_ccu_probe(&pdev->dev, reg, &sun9i_a80_ccu_desc);
}

static const struct of_device_id sun9i_a80_ccu_ids[] = {
	{ .compatible = "allwinner,sun9i-a80-ccu" },
	{ }
};
MODULE_DEVICE_TABLE(of, sun9i_a80_ccu_ids);

static struct platform_driver sun9i_a80_ccu_driver = {
	.probe	= sun9i_a80_ccu_probe,
	.driver	= {
		.name	= "sun9i-a80-ccu",
		.suppress_bind_attrs = true,
		.of_match_table	= sun9i_a80_ccu_ids,
	},
};
module_platform_driver(sun9i_a80_ccu_driver);

MODULE_IMPORT_NS("SUNXI_CCU");
MODULE_DESCRIPTION("Support for the Allwinner A80 CCU");
MODULE_LICENSE("GPL");
