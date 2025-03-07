// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023-2024 Arm Ltd.
 * Based on the D1 CCU driver:
 *   Copyright (c) 2020 huangzhenwei@allwinnertech.com
 *   Copyright (C) 2021 Samuel Holland <samuel@sholland.org>
 */

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "../clk.h"

#include "ccu_common.h"
#include "ccu_reset.h"

#include "ccu_div.h"
#include "ccu_gate.h"
#include "ccu_mp.h"
#include "ccu_mult.h"
#include "ccu_nk.h"
#include "ccu_nkm.h"
#include "ccu_nkmp.h"
#include "ccu_nm.h"

#include "ccu-sun55i-a523.h"

/*
 * The 24 MHz oscillator, the root of most of the clock tree.
 * .fw_name is the string used in the DT "clock-names" property, used to
 * identify the corresponding clock in the "clocks" property.
 */
static const struct clk_parent_data osc24M[] = {
	{ .fw_name = "hosc" }
};

/**************************************************************************
 *                              PLLs                                      *
 **************************************************************************/

/* Some PLLs are input * N / div1 / P. Model them as NKMP with no K */
#define SUN55I_A523_PLL_DDR0_REG		0x010
static struct ccu_nkmp pll_ddr_clk = {
	.enable		= BIT(27),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 11),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(0, 1), /* output divider */
	.common		= {
		.reg		= 0x010,
		.hw.init	= CLK_HW_INIT_PARENTS_DATA("pll-ddr0", osc24M,
							   &ccu_nkmp_ops,
							   CLK_SET_RATE_GATE |
							   CLK_IS_CRITICAL),
	},
};

/*
 * There is no actual clock output with that frequency (2.4 GHz), instead it
 * has multiple outputs with adjustable dividers from that base frequency.
 * Model them separately as divider clocks based on that parent here.
 */
#define SUN55I_A523_PLL_PERIPH0_REG	0x020
static struct ccu_nm pll_periph0_4x_clk = {
	.enable		= BIT(27),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 11),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.common		= {
		.reg		= 0x020,
		.hw.init	= CLK_HW_INIT_PARENTS_DATA("pll-periph0-4x",
							   osc24M, &ccu_nm_ops,
							   CLK_SET_RATE_GATE),
	},
};
/*
 * Most clock-defining macros expect an *array* of parent clocks, even if
 * they do not contain a muxer to select between different parents.
 * The macros ending in just _HW take a simple clock pointer, but then create
 * a single-entry array out of that. The macros using _HWS take such an
 * array (even when it is a single entry one), this avoids having those
 * helper arrays created inside *every* clock definition.
 * This means for every clock that is referenced more than once it is
 * useful to create such a dummy array and use _HWS.
 */
static const struct clk_hw *pll_periph0_4x_hws[] = {
	&pll_periph0_4x_clk.common.hw
};

static SUNXI_CCU_M_HWS(pll_periph0_2x_clk, "pll-periph0-2x",
		       pll_periph0_4x_hws, 0x020, 16, 3, 0);
static const struct clk_hw *pll_periph0_2x_hws[] = {
	&pll_periph0_2x_clk.common.hw
};
static SUNXI_CCU_M_HWS(pll_periph0_800M_clk, "pll-periph0-800M",
		       pll_periph0_4x_hws, 0x020, 20, 3, 0);
static SUNXI_CCU_M_HWS(pll_periph0_480M_clk, "pll-periph0-480M",
		       pll_periph0_4x_hws, 0x020, 2, 3, 0);
static const struct clk_hw *pll_periph0_480M_hws[] = {
	&pll_periph0_480M_clk.common.hw
};
static CLK_FIXED_FACTOR_HWS(pll_periph0_600M_clk, "pll-periph0-600M",
			    pll_periph0_2x_hws, 2, 1, 0);
static CLK_FIXED_FACTOR_HWS(pll_periph0_400M_clk, "pll-periph0-400M",
			    pll_periph0_2x_hws, 3, 1, 0);
static CLK_FIXED_FACTOR_HWS(pll_periph0_300M_clk, "pll-periph0-300M",
			    pll_periph0_2x_hws, 4, 1, 0);
static CLK_FIXED_FACTOR_HWS(pll_periph0_200M_clk, "pll-periph0-200M",
			    pll_periph0_2x_hws, 6, 1, 0);
static CLK_FIXED_FACTOR_HWS(pll_periph0_150M_clk, "pll-periph0-150M",
			    pll_periph0_2x_hws, 8, 1, 0);
static CLK_FIXED_FACTOR_HWS(pll_periph0_160M_clk, "pll-periph0-160M",
			    pll_periph0_480M_hws, 3, 1, 0);

#define SUN55I_A523_PLL_PERIPH1_REG	0x028
static struct ccu_nm pll_periph1_4x_clk = {
	.enable		= BIT(27),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 11),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.common		= {
		.reg		= 0x028,
		.hw.init	= CLK_HW_INIT_PARENTS_DATA("pll-periph1-4x",
							   osc24M, &ccu_nm_ops,
							   CLK_SET_RATE_GATE),
	},
};

static const struct clk_hw *pll_periph1_4x_hws[] = {
	&pll_periph1_4x_clk.common.hw
};
static SUNXI_CCU_M_HWS(pll_periph1_2x_clk, "pll-periph1-2x",
		       pll_periph1_4x_hws, 0x028, 16, 3, 0);
static SUNXI_CCU_M_HWS(pll_periph1_800M_clk, "pll-periph1-800M",
		       pll_periph1_4x_hws, 0x028, 20, 3, 0);
static SUNXI_CCU_M_HWS(pll_periph1_480M_clk, "pll-periph1-480M",
		       pll_periph1_4x_hws, 0x028, 2, 3, 0);

static const struct clk_hw *pll_periph1_2x_hws[] = {
	&pll_periph1_2x_clk.common.hw
};
static CLK_FIXED_FACTOR_HWS(pll_periph1_600M_clk, "pll-periph1-600M",
			    pll_periph1_2x_hws, 2, 1, 0);
static CLK_FIXED_FACTOR_HWS(pll_periph1_400M_clk, "pll-periph1-400M",
			    pll_periph1_2x_hws, 3, 1, 0);
static CLK_FIXED_FACTOR_HWS(pll_periph1_300M_clk, "pll-periph1-300M",
			    pll_periph1_2x_hws, 4, 1, 0);
static CLK_FIXED_FACTOR_HWS(pll_periph1_200M_clk, "pll-periph1-200M",
			    pll_periph1_2x_hws, 6, 1, 0);
static CLK_FIXED_FACTOR_HWS(pll_periph1_150M_clk, "pll-periph1-150M",
			    pll_periph1_2x_hws, 8, 1, 0);
static const struct clk_hw *pll_periph1_480M_hws[] = {
	&pll_periph1_480M_clk.common.hw
};
static CLK_FIXED_FACTOR_HWS(pll_periph1_160M_clk, "pll-periph1-160M",
			    pll_periph1_480M_hws, 3, 1, 0);

#define SUN55I_A523_PLL_GPU_REG		0x030
static struct ccu_nkmp pll_gpu_clk = {
	.enable		= BIT(27),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 11),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(0, 1), /* output divider */
	.common		= {
		.reg		= 0x030,
		.hw.init	= CLK_HW_INIT_PARENTS_DATA("pll-gpu", osc24M,
							   &ccu_nkmp_ops,
							   CLK_SET_RATE_GATE),
	},
};

#define SUN55I_A523_PLL_VIDEO0_REG	0x040
static struct ccu_nm pll_video0_8x_clk = {
	.enable		= BIT(27),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 11),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.common		= {
		.reg		= 0x040,
		.hw.init	= CLK_HW_INIT_PARENTS_DATA("pll-video0-8x",
							   osc24M, &ccu_nm_ops,
							   CLK_SET_RATE_GATE),
	},
};

static const struct clk_hw *pll_video0_8x_hws[] = {
	&pll_video0_8x_clk.common.hw
};
static SUNXI_CCU_M_HWS(pll_video0_4x_clk, "pll-video0-4x",
		       pll_video0_8x_hws, 0x040, 0, 1, 0);
static CLK_FIXED_FACTOR_HWS(pll_video0_3x_clk, "pll-video0-3x",
			    pll_video0_8x_hws, 3, 1, CLK_SET_RATE_PARENT);

#define SUN55I_A523_PLL_VIDEO1_REG	0x048
static struct ccu_nm pll_video1_8x_clk = {
	.enable		= BIT(27),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 11),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.common		= {
		.reg		= 0x048,
		.hw.init	= CLK_HW_INIT_PARENTS_DATA("pll-video1-8x",
							   osc24M, &ccu_nm_ops,
							   CLK_SET_RATE_GATE),
	},
};

static const struct clk_hw *pll_video1_8x_hws[] = {
	&pll_video1_8x_clk.common.hw
};
static SUNXI_CCU_M_HWS(pll_video1_4x_clk, "pll-video1-4x",
		       pll_video1_8x_hws, 0x048, 0, 1, 0);
static CLK_FIXED_FACTOR_HWS(pll_video1_3x_clk, "pll-video1-3x",
			    pll_video1_8x_hws, 3, 1, CLK_SET_RATE_PARENT);

#define SUN55I_A523_PLL_VIDEO2_REG	0x050
static struct ccu_nm pll_video2_8x_clk = {
	.enable		= BIT(27),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 11),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.common		= {
		.reg		= 0x050,
		.hw.init	= CLK_HW_INIT_PARENTS_DATA("pll-video2-8x",
							   osc24M, &ccu_nm_ops,
							   CLK_SET_RATE_GATE),
	},
};

static const struct clk_hw *pll_video2_8x_hws[] = {
	&pll_video2_8x_clk.common.hw
};
static SUNXI_CCU_M_HWS(pll_video2_4x_clk, "pll-video2-4x",
		       pll_video2_8x_hws, 0x050, 0, 1, 0);
static CLK_FIXED_FACTOR_HWS(pll_video2_3x_clk, "pll-video2-3x",
			    pll_video2_8x_hws, 3, 1, CLK_SET_RATE_PARENT);

#define SUN55I_A523_PLL_VE_REG		0x058
static struct ccu_nkmp pll_ve_clk = {
	.enable		= BIT(27),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 11),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.p		= _SUNXI_CCU_DIV(0, 1), /* output divider */
	.common		= {
		.reg		= 0x058,
		.hw.init	= CLK_HW_INIT_PARENTS_DATA("pll-ve", osc24M,
							   &ccu_nkmp_ops,
							   CLK_SET_RATE_GATE),
	},
};

#define SUN55I_A523_PLL_VIDEO3_REG	0x068
static struct ccu_nm pll_video3_8x_clk = {
	.enable		= BIT(27),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 11),
	.m		= _SUNXI_CCU_DIV(1, 1), /* input divider */
	.common		= {
		.reg		= 0x068,
		.hw.init	= CLK_HW_INIT_PARENTS_DATA("pll-video3-8x",
							   osc24M, &ccu_nm_ops,
							   CLK_SET_RATE_GATE),
	},
};

static const struct clk_hw *pll_video3_8x_hws[] = {
	&pll_video3_8x_clk.common.hw
};
static SUNXI_CCU_M_HWS(pll_video3_4x_clk, "pll-video3-4x",
		       pll_video3_8x_hws, 0x068, 0, 1, 0);
static CLK_FIXED_FACTOR_HWS(pll_video3_3x_clk, "pll-video3-3x",
			    pll_video3_8x_hws, 3, 1, CLK_SET_RATE_PARENT);

/*
 * PLL_AUDIO0 has m0, m1 dividers in addition to the usual N, M factors.
 * Since we only need some fixed frequency from this PLL (22.5792MHz x 4 and
 * 24.576MHz x 4), ignore those dividers and force both of them to 1 (encoded
 * as 0), in the probe function below.
 * The M factor must be an even number to produce a 50% duty cycle output.
 */
#define SUN55I_A523_PLL_AUDIO0_REG		0x078
static struct ccu_sdm_setting pll_audio0_sdm_table[] = {
	{ .rate = 90316800, .pattern = 0xc000872b, .m = 20, .n = 75 },
	{ .rate = 98304000, .pattern = 0xc0004dd3, .m = 12, .n = 49 },

};

static struct ccu_nm pll_audio0_4x_clk = {
	.enable		= BIT(27),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 11),
	.m		= _SUNXI_CCU_DIV(16, 6),
	.sdm		= _SUNXI_CCU_SDM(pll_audio0_sdm_table, BIT(24),
					 0x178, BIT(31)),
	.min_rate	= 180000000U,
	.max_rate	= 3000000000U,
	.common		= {
		.reg		= 0x078,
		.features	= CCU_FEATURE_SIGMA_DELTA_MOD,
		.hw.init	= CLK_HW_INIT_PARENTS_DATA("pll-audio0-4x",
							   osc24M, &ccu_nm_ops,
							   CLK_SET_RATE_GATE),
	},
};

static CLK_FIXED_FACTOR_HW(pll_audio0_2x_clk, "pll-audio0-2x",
			   &pll_audio0_4x_clk.common.hw, 2, 1, 0);
static CLK_FIXED_FACTOR_HW(pll_audio0_clk, "pll-audio0",
			   &pll_audio0_4x_clk.common.hw, 4, 1, 0);

#define SUN55I_A523_PLL_NPU_REG			0x080
static struct ccu_nm pll_npu_4x_clk = {
	.enable		= BIT(27),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 11),
	.m		= _SUNXI_CCU_DIV(1, 1),	/* input divider */
	.common		= {
		.reg		= 0x0080,
		.hw.init	= CLK_HW_INIT_PARENTS_DATA("pll-npu-4x",
							   osc24M, &ccu_nm_ops,
							   CLK_SET_RATE_GATE),
	},
};
static CLK_FIXED_FACTOR_HW(pll_npu_2x_clk, "pll-npu-2x",
			   &pll_npu_4x_clk.common.hw, 2, 1, CLK_SET_RATE_PARENT);

static CLK_FIXED_FACTOR_HW(pll_npu_1x_clk, "pll-npu-1x",
			   &pll_npu_4x_clk.common.hw, 4, 1, 0);


/**************************************************************************
 *                           bus clocks                                   *
 **************************************************************************/

static const struct clk_parent_data ahb_apb0_parents[] = {
	{ .fw_name = "hosc" },
	{ .fw_name = "losc" },
	{ .fw_name = "iosc" },
	{ .hw = &pll_periph0_600M_clk.hw },
};

static SUNXI_CCU_M_DATA_WITH_MUX(ahb_clk, "ahb", ahb_apb0_parents, 0x510,
				 0, 5,		/* M */
				 24, 2,		/* mux */
				 0);
static SUNXI_CCU_M_DATA_WITH_MUX(apb0_clk, "apb0", ahb_apb0_parents, 0x520,
				 0, 5,		/* M */
				 24, 2,	/* mux */
				 0);

static const struct clk_parent_data apb1_parents[] = {
	{ .fw_name = "hosc" },
	{ .fw_name = "losc" },
	{ .fw_name = "iosc" },
	{ .hw = &pll_periph0_600M_clk.hw },
	{ .hw = &pll_periph0_480M_clk.common.hw },
};
static SUNXI_CCU_M_DATA_WITH_MUX(apb1_clk, "apb1", apb1_parents, 0x524,
				 0, 5,		/* M */
				 24, 3,		/* mux */
				 0);

static const struct clk_parent_data mbus_parents[] = {
	{ .hw = &pll_ddr_clk.common.hw },
	{ .hw = &pll_periph1_600M_clk.hw },
	{ .hw = &pll_periph1_480M_clk.common.hw },
	{ .hw = &pll_periph1_400M_clk.hw },
	{ .hw = &pll_periph1_150M_clk.hw },
	{ .fw_name = "hosc" },
};
static SUNXI_CCU_MP_DATA_WITH_MUX_GATE_FEAT(mbus_clk, "mbus", mbus_parents,
					    0x540,
					    0, 5,		/* M */
					    0, 0,		/* no P */
					    24, 3,	/* mux */
					    BIT(31),	/* gate */
					    0, CCU_FEATURE_UPDATE_BIT);

/**************************************************************************
 *                          mod clocks                                    *
 **************************************************************************/

static const struct clk_hw *de_parents[] = {
	&pll_periph0_300M_clk.hw,
	&pll_periph0_400M_clk.hw,
	&pll_video3_4x_clk.common.hw,
	&pll_video3_3x_clk.hw,
};

static SUNXI_CCU_M_HW_WITH_MUX_GATE(de_clk, "de", de_parents, 0x600,
				    0, 5,	/* M */
				    24, 3,	/* mux */
				    BIT(31),	/* gate */
				    CLK_SET_RATE_PARENT);

static const struct clk_hw *di_parents[] = {
	&pll_periph0_300M_clk.hw,
	&pll_periph0_400M_clk.hw,
	&pll_video0_4x_clk.common.hw,
	&pll_video1_4x_clk.common.hw,
};

static SUNXI_CCU_M_HW_WITH_MUX_GATE(di_clk, "di", di_parents, 0x620,
				    0, 5,	/* M */
				    24, 3,	/* mux */
				    BIT(31),	/* gate */
				    CLK_SET_RATE_PARENT);

static const struct clk_hw *g2d_parents[] = {
	&pll_periph0_400M_clk.hw,
	&pll_periph0_300M_clk.hw,
	&pll_video0_4x_clk.common.hw,
	&pll_video1_4x_clk.common.hw,
};

static SUNXI_CCU_M_HW_WITH_MUX_GATE(g2d_clk, "g2d", g2d_parents, 0x630,
				    0, 5,	/* M */
				    24, 3,	/* mux */
				    BIT(31),	/* gate */
				    0);

static const struct clk_hw *gpu_parents[] = {
	&pll_gpu_clk.common.hw,
	&pll_periph0_800M_clk.common.hw,
	&pll_periph0_600M_clk.hw,
	&pll_periph0_400M_clk.hw,
	&pll_periph0_300M_clk.hw,
	&pll_periph0_200M_clk.hw,
};

static SUNXI_CCU_M_HW_WITH_MUX_GATE(gpu_clk, "gpu", gpu_parents, 0x670,
				    0, 4,	/* M */
				    24, 3,	/* mux */
				    BIT(31),	/* gate */
				    CLK_SET_RATE_PARENT);

static const struct clk_parent_data ce_parents[] = {
	{ .fw_name = "hosc" },
	{ .hw = &pll_periph0_480M_clk.common.hw },
	{ .hw = &pll_periph0_400M_clk.hw },
	{ .hw = &pll_periph0_300M_clk.hw },
};
static SUNXI_CCU_M_DATA_WITH_MUX_GATE(ce_clk, "ce", ce_parents, 0x680,
				       0, 5,	/* M */
				       24, 3,	/* mux */
				       BIT(31),	/* gate */
				       0);

static const struct clk_hw *ve_parents[] = {
	&pll_ve_clk.common.hw,
	&pll_periph0_480M_clk.common.hw,
	&pll_periph0_400M_clk.hw,
	&pll_periph0_300M_clk.hw,
};
static SUNXI_CCU_M_HW_WITH_MUX_GATE(ve_clk, "ve", ve_parents, 0x690,
				    0, 5,	/* M */
				    24, 3,	/* mux */
				    BIT(31),	/* gate */
				    CLK_SET_RATE_PARENT);

static const struct clk_parent_data hstimer_parents[] = {
	{ .fw_name = "hosc" },
	{ .fw_name = "iosc" },
	{ .fw_name = "losc" },
	{ .hw = &pll_periph0_200M_clk.hw },
};
static SUNXI_CCU_MP_DATA_WITH_MUX_GATE(hstimer0_clk, "hstimer0",
				       hstimer_parents, 0x730,
				       0, 0,	/* M */
				       0, 3,	/* P */
				       24, 3,	/* mux */
				       BIT(31),	/* gate */
				       0);

static SUNXI_CCU_MP_DATA_WITH_MUX_GATE(hstimer1_clk, "hstimer1",
				       hstimer_parents,
				       0x734,
				       0, 0,	/* M */
				       0, 3,	/* P */
				       24, 3,	/* mux */
				       BIT(31),	/* gate */
				       0);

static SUNXI_CCU_MP_DATA_WITH_MUX_GATE(hstimer2_clk, "hstimer2",
				       hstimer_parents,
				       0x738,
				       0, 0,	/* M */
				       0, 3,	/* P */
				       24, 3,	/* mux */
				       BIT(31),	/* gate */
				       0);

static SUNXI_CCU_MP_DATA_WITH_MUX_GATE(hstimer3_clk, "hstimer3",
				       hstimer_parents,
				       0x73c,
				       0, 0,	/* M */
				       0, 3,	/* P */
				       24, 3,	/* mux */
				       BIT(31),	/* gate */
				       0);

static SUNXI_CCU_MP_DATA_WITH_MUX_GATE(hstimer4_clk, "hstimer4",
				       hstimer_parents,
				       0x740,
				       0, 0,	/* M */
				       0, 3,	/* P */
				       24, 3,	/* mux */
				       BIT(31),	/* gate */
				       0);

static SUNXI_CCU_MP_DATA_WITH_MUX_GATE(hstimer5_clk, "hstimer5",
				       hstimer_parents,
				       0x744,
				       0, 0,	/* M */
				       0, 3,	/* P */
				       24, 3,	/* mux */
				       BIT(31),	/* gate */
				       0);

static const struct clk_parent_data iommu_parents[] = {
	{ .hw = &pll_periph0_600M_clk.hw },
	{ .hw = &pll_ddr_clk.common.hw },
	{ .hw = &pll_periph0_480M_clk.common.hw },
	{ .hw = &pll_periph0_400M_clk.hw },
	{ .hw = &pll_periph0_150M_clk.hw },
	{ .fw_name = "hosc" },
};

static SUNXI_CCU_MP_DATA_WITH_MUX_GATE_FEAT(iommu_clk, "iommu", iommu_parents,
					    0x7b0,
					    0, 5,	/* M */
					    0, 0,	/* no P */
					    24, 3,	/* mux */
					    BIT(31),	/* gate */
					    CLK_SET_RATE_PARENT,
					    CCU_FEATURE_UPDATE_BIT);

static const struct clk_parent_data dram_parents[] = {
	{ .hw = &pll_ddr_clk.common.hw },
	{ .hw = &pll_periph0_600M_clk.hw },
	{ .hw = &pll_periph0_480M_clk.common.hw },
	{ .hw = &pll_periph0_400M_clk.hw },
	{ .hw = &pll_periph0_150M_clk.hw },
};
static SUNXI_CCU_MP_DATA_WITH_MUX_GATE_FEAT(dram_clk, "dram", dram_parents,
					    0x800,
					    0, 5,	/* M */
					    0, 0,	/* no P */
					    24, 3,	/* mux */
					    BIT(31),	/* gate */
					    CLK_IS_CRITICAL,
					    CCU_FEATURE_UPDATE_BIT);

static const struct clk_parent_data losc_hosc_parents[] = {
	{ .fw_name = "hosc" },
	{ .fw_name = "losc" },
};

static SUNXI_CCU_M_DATA_WITH_MUX_GATE(pcie_aux_clk, "pcie-aux",
				      losc_hosc_parents, 0xaa0,
				      0, 5,	/* M */
				      24, 1,	/* mux */
				      BIT(31),	/* gate */
				      0);

static SUNXI_CCU_GATE_DATA(hdmi_24M_clk, "hdmi-24M", osc24M, 0xb04, BIT(31), 0);

static SUNXI_CCU_GATE_HWS_WITH_PREDIV(hdmi_cec_32k_clk, "hdmi-cec-32k",
				      pll_periph0_2x_hws,
				      0xb10, BIT(30), 36621, 0);

static const struct clk_parent_data hdmi_cec_parents[] = {
	{ .fw_name = "losc" },
	{ .hw = &hdmi_cec_32k_clk.common.hw },
};
static SUNXI_CCU_MUX_DATA_WITH_GATE(hdmi_cec_clk, "hdmi-cec", hdmi_cec_parents,
				    0xb10,
				    24, 1,	/* mux */
				    BIT(31),	/* gate */
				    0);

static const struct clk_parent_data mipi_dsi_parents[] = {
	{ .fw_name = "hosc" },
	{ .hw = &pll_periph0_200M_clk.hw },
	{ .hw = &pll_periph0_150M_clk.hw },
};
static SUNXI_CCU_M_DATA_WITH_MUX_GATE(mipi_dsi0_clk, "mipi-dsi0",
				      mipi_dsi_parents, 0xb24,
				      0, 5,	/* M */
				      24, 3,	/* mux */
				      BIT(31),	/* gate */
				      0);

static SUNXI_CCU_M_DATA_WITH_MUX_GATE(mipi_dsi1_clk, "mipi-dsi1",
				      mipi_dsi_parents, 0xb28,
				      0, 5,	/* M */
				      24, 3,	/* mux */
				      BIT(31),	/* gate */
				      0);

static const struct clk_hw *tcon_parents[] = {
	&pll_video0_4x_clk.common.hw,
	&pll_video1_4x_clk.common.hw,
	&pll_video2_4x_clk.common.hw,
	&pll_video3_4x_clk.common.hw,
	&pll_periph0_2x_clk.common.hw,
	&pll_video0_3x_clk.hw,
	&pll_video1_3x_clk.hw,
};
static SUNXI_CCU_M_HW_WITH_MUX_GATE(tcon_lcd0_clk, "tcon-lcd0", tcon_parents,
				    0xb60,
				    0,  5,	/* M */
				    24, 3,	/* mux */
				    BIT(31),	/* gate */
				    CLK_SET_RATE_PARENT);

static SUNXI_CCU_M_HW_WITH_MUX_GATE(tcon_lcd1_clk, "tcon-lcd1", tcon_parents,
				    0xb64,
				    0,  5,	/* M */
				    24, 3,	/* mux */
				    BIT(31),	/* gate */
				    CLK_SET_RATE_PARENT);

static const struct clk_hw *tcon_tv_parents[] = {
	&pll_video0_4x_clk.common.hw,
	&pll_video1_4x_clk.common.hw,
	&pll_video2_4x_clk.common.hw,
	&pll_video3_4x_clk.common.hw,
	&pll_periph0_2x_clk.common.hw,
};
static SUNXI_CCU_M_HW_WITH_MUX_GATE(tcon_lcd2_clk, "tcon-lcd2",
				    tcon_tv_parents, 0xb68,
				    0,  5,	/* M */
				    24, 3,	/* mux */
				    BIT(31),	/* gate */
				    CLK_SET_RATE_PARENT);

static SUNXI_CCU_M_HW_WITH_MUX_GATE(combophy_dsi0_clk, "combophy-dsi0",
				    tcon_parents, 0xb6c,
				    0,  5,	/* M */
				    24, 3,	/* mux */
				    BIT(31),	/* gate */
				    CLK_SET_RATE_PARENT);

static SUNXI_CCU_M_HW_WITH_MUX_GATE(combophy_dsi1_clk, "combophy-dsi1",
				    tcon_parents, 0xb70,
				    0,  5,	/* M */
				    24, 3,	/* mux */
				    BIT(31),	/* gate */
				    CLK_SET_RATE_PARENT);

static SUNXI_CCU_M_HW_WITH_MUX_GATE(tcon_tv0_clk, "tcon-tv0", tcon_tv_parents,
				    0xb80,
				    0, 4,	/* M */
				    24, 3,	/* mux */
				    BIT(31),	/* gate */
				    CLK_SET_RATE_PARENT);

static SUNXI_CCU_M_HW_WITH_MUX_GATE(tcon_tv1_clk, "tcon-tv1", tcon_tv_parents,
				    0xb84,
				    0, 4,	/* M */
				    24, 3,	/* mux */
				    BIT(31),	/* gate */
				    CLK_SET_RATE_PARENT);

static const struct clk_hw *edp_parents[] = {
	&pll_video0_4x_clk.common.hw,
	&pll_video1_4x_clk.common.hw,
	&pll_video2_4x_clk.common.hw,
	&pll_video3_4x_clk.common.hw,
	&pll_periph0_2x_clk.common.hw,
};
static SUNXI_CCU_M_HW_WITH_MUX_GATE(edp_clk, "edp", edp_parents, 0xbb0,
				    0, 4,	/* M */
				    24, 3,	/* mux */
				    BIT(31),	/* gate */
				    CLK_SET_RATE_PARENT);

/*
 * Contains all clocks that are controlled by a hardware register. They
 * have a (sunxi) .common member, which needs to be initialised by the common
 * sunxi CCU code, to be filled with the MMIO base address and the shared lock.
 */
static struct ccu_common *sun55i_a523_ccu_clks[] = {
	&pll_ddr_clk.common,
	&pll_periph0_4x_clk.common,
	&pll_periph0_2x_clk.common,
	&pll_periph0_800M_clk.common,
	&pll_periph0_480M_clk.common,
	&pll_periph1_4x_clk.common,
	&pll_periph1_2x_clk.common,
	&pll_periph1_800M_clk.common,
	&pll_periph1_480M_clk.common,
	&pll_gpu_clk.common,
	&pll_video0_8x_clk.common,
	&pll_video0_4x_clk.common,
	&pll_video1_8x_clk.common,
	&pll_video1_4x_clk.common,
	&pll_video2_8x_clk.common,
	&pll_video2_4x_clk.common,
	&pll_video3_8x_clk.common,
	&pll_video3_4x_clk.common,
	&pll_ve_clk.common,
	&pll_audio0_4x_clk.common,
	&pll_npu_4x_clk.common,
	&ahb_clk.common,
	&apb0_clk.common,
	&apb1_clk.common,
	&mbus_clk.common,
	&de_clk.common,
	&di_clk.common,
	&g2d_clk.common,
	&gpu_clk.common,
	&ce_clk.common,
	&ve_clk.common,
	&hstimer0_clk.common,
	&hstimer1_clk.common,
	&hstimer2_clk.common,
	&hstimer3_clk.common,
	&hstimer4_clk.common,
	&hstimer5_clk.common,
	&iommu_clk.common,
	&dram_clk.common,
	&pcie_aux_clk.common,
	&hdmi_24M_clk.common,
	&hdmi_cec_32k_clk.common,
	&hdmi_cec_clk.common,
	&mipi_dsi0_clk.common,
	&mipi_dsi1_clk.common,
	&tcon_lcd0_clk.common,
	&tcon_lcd1_clk.common,
	&tcon_lcd2_clk.common,
	&tcon_tv0_clk.common,
	&tcon_tv1_clk.common,
	&edp_clk.common,
};

static struct clk_hw_onecell_data sun55i_a523_hw_clks = {
	.num	= CLK_NUMBER,
	.hws	= {
		[CLK_PLL_DDR0]		= &pll_ddr_clk.common.hw,
		[CLK_PLL_PERIPH0_4X]	= &pll_periph0_4x_clk.common.hw,
		[CLK_PLL_PERIPH0_2X]	= &pll_periph0_2x_clk.common.hw,
		[CLK_PLL_PERIPH0_800M]	= &pll_periph0_800M_clk.common.hw,
		[CLK_PLL_PERIPH0_480M]	= &pll_periph0_480M_clk.common.hw,
		[CLK_PLL_PERIPH0_600M]	= &pll_periph0_600M_clk.hw,
		[CLK_PLL_PERIPH0_400M]	= &pll_periph0_400M_clk.hw,
		[CLK_PLL_PERIPH0_300M]	= &pll_periph0_300M_clk.hw,
		[CLK_PLL_PERIPH0_200M]	= &pll_periph0_200M_clk.hw,
		[CLK_PLL_PERIPH0_160M]	= &pll_periph0_160M_clk.hw,
		[CLK_PLL_PERIPH0_150M]	= &pll_periph0_150M_clk.hw,
		[CLK_PLL_PERIPH1_4X]	= &pll_periph1_4x_clk.common.hw,
		[CLK_PLL_PERIPH1_2X]	= &pll_periph1_2x_clk.common.hw,
		[CLK_PLL_PERIPH1_800M]	= &pll_periph1_800M_clk.common.hw,
		[CLK_PLL_PERIPH1_480M]	= &pll_periph1_480M_clk.common.hw,
		[CLK_PLL_PERIPH1_600M]	= &pll_periph1_600M_clk.hw,
		[CLK_PLL_PERIPH1_400M]	= &pll_periph1_400M_clk.hw,
		[CLK_PLL_PERIPH1_300M]	= &pll_periph1_300M_clk.hw,
		[CLK_PLL_PERIPH1_200M]	= &pll_periph1_200M_clk.hw,
		[CLK_PLL_PERIPH1_160M]	= &pll_periph1_160M_clk.hw,
		[CLK_PLL_PERIPH1_150M]	= &pll_periph1_150M_clk.hw,
		[CLK_PLL_GPU]		= &pll_gpu_clk.common.hw,
		[CLK_PLL_VIDEO0_8X]	= &pll_video0_8x_clk.common.hw,
		[CLK_PLL_VIDEO0_4X]	= &pll_video0_4x_clk.common.hw,
		[CLK_PLL_VIDEO0_3X]	= &pll_video0_3x_clk.hw,
		[CLK_PLL_VIDEO1_8X]	= &pll_video1_8x_clk.common.hw,
		[CLK_PLL_VIDEO1_4X]	= &pll_video1_4x_clk.common.hw,
		[CLK_PLL_VIDEO1_3X]	= &pll_video1_3x_clk.hw,
		[CLK_PLL_VIDEO2_8X]	= &pll_video2_8x_clk.common.hw,
		[CLK_PLL_VIDEO2_4X]	= &pll_video2_4x_clk.common.hw,
		[CLK_PLL_VIDEO2_3X]	= &pll_video2_3x_clk.hw,
		[CLK_PLL_VIDEO3_8X]	= &pll_video3_8x_clk.common.hw,
		[CLK_PLL_VIDEO3_4X]	= &pll_video3_4x_clk.common.hw,
		[CLK_PLL_VIDEO3_3X]	= &pll_video3_3x_clk.hw,
		[CLK_PLL_VE]		= &pll_ve_clk.common.hw,
		[CLK_PLL_AUDIO0_4X]	= &pll_audio0_4x_clk.common.hw,
		[CLK_PLL_AUDIO0_2X]	= &pll_audio0_2x_clk.hw,
		[CLK_PLL_AUDIO0]	= &pll_audio0_clk.hw,
		[CLK_PLL_NPU_4X]	= &pll_npu_4x_clk.common.hw,
		[CLK_PLL_NPU_2X]	= &pll_npu_2x_clk.hw,
		[CLK_PLL_NPU]		= &pll_npu_1x_clk.hw,
		[CLK_AHB]		= &ahb_clk.common.hw,
		[CLK_APB0]		= &apb0_clk.common.hw,
		[CLK_APB1]		= &apb1_clk.common.hw,
		[CLK_MBUS]		= &mbus_clk.common.hw,
		[CLK_DE]		= &de_clk.common.hw,
		[CLK_DI]		= &di_clk.common.hw,
		[CLK_G2D]		= &g2d_clk.common.hw,
		[CLK_GPU]		= &gpu_clk.common.hw,
		[CLK_CE]		= &ce_clk.common.hw,
		[CLK_VE]		= &ve_clk.common.hw,
		[CLK_HSTIMER0]		= &hstimer0_clk.common.hw,
		[CLK_HSTIMER1]		= &hstimer1_clk.common.hw,
		[CLK_HSTIMER2]		= &hstimer2_clk.common.hw,
		[CLK_HSTIMER3]		= &hstimer3_clk.common.hw,
		[CLK_HSTIMER4]		= &hstimer4_clk.common.hw,
		[CLK_HSTIMER5]		= &hstimer5_clk.common.hw,
		[CLK_IOMMU]		= &iommu_clk.common.hw,
		[CLK_DRAM]		= &dram_clk.common.hw,
		[CLK_PCIE_AUX]		= &pcie_aux_clk.common.hw,
		[CLK_HDMI_24M]		= &hdmi_24M_clk.common.hw,
		[CLK_HDMI_CEC_32K]	= &hdmi_cec_32k_clk.common.hw,
		[CLK_HDMI_CEC]		= &hdmi_cec_clk.common.hw,
		[CLK_MIPI_DSI0]		= &mipi_dsi0_clk.common.hw,
		[CLK_MIPI_DSI1]		= &mipi_dsi1_clk.common.hw,
		[CLK_TCON_LCD0]		= &tcon_lcd0_clk.common.hw,
		[CLK_TCON_LCD1]		= &tcon_lcd1_clk.common.hw,
		[CLK_TCON_LCD2]		= &tcon_lcd2_clk.common.hw,
		[CLK_COMBOPHY_DSI0]	= &combophy_dsi0_clk.common.hw,
		[CLK_COMBOPHY_DSI1]	= &combophy_dsi1_clk.common.hw,
		[CLK_TCON_TV0]		= &tcon_tv0_clk.common.hw,
		[CLK_TCON_TV1]		= &tcon_tv1_clk.common.hw,
		[CLK_EDP]		= &edp_clk.common.hw,
	},
};

static const struct sunxi_ccu_desc sun55i_a523_ccu_desc = {
	.ccu_clks	= sun55i_a523_ccu_clks,
	.num_ccu_clks	= ARRAY_SIZE(sun55i_a523_ccu_clks),

	.hw_clks	= &sun55i_a523_hw_clks,
};

static const u32 pll_regs[] = {
	SUN55I_A523_PLL_DDR0_REG,
	SUN55I_A523_PLL_PERIPH0_REG,
	SUN55I_A523_PLL_PERIPH1_REG,
	SUN55I_A523_PLL_GPU_REG,
	SUN55I_A523_PLL_VIDEO0_REG,
	SUN55I_A523_PLL_VIDEO1_REG,
	SUN55I_A523_PLL_VIDEO2_REG,
	SUN55I_A523_PLL_VE_REG,
	SUN55I_A523_PLL_VIDEO3_REG,
	SUN55I_A523_PLL_AUDIO0_REG,
	SUN55I_A523_PLL_NPU_REG,
};

static int sun55i_a523_ccu_probe(struct platform_device *pdev)
{
	void __iomem *reg;
	u32 val;
	int i, ret;

	reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	/*
	 * The PLL clock code does not model all bits, for instance it does
	 * not support a separate enable and gate bit. We present the
	 * gate bit(27) as the enable bit, but then have to set the
	 * PLL Enable, LDO Enable, and Lock Enable bits on all PLLs here.
	 */
	for (i = 0; i < ARRAY_SIZE(pll_regs); i++) {
		val = readl(reg + pll_regs[i]);
		val |= BIT(31) | BIT(30) | BIT(29);
		writel(val, reg + pll_regs[i]);
	}

	/* Enforce m1 = 0, m0 = 0 for PLL_AUDIO0 */
	val = readl(reg + SUN55I_A523_PLL_AUDIO0_REG);
	val &= ~(BIT(1) | BIT(0));
	writel(val, reg + SUN55I_A523_PLL_AUDIO0_REG);

	ret = devm_sunxi_ccu_probe(&pdev->dev, reg, &sun55i_a523_ccu_desc);
	if (ret)
		return ret;

	return 0;
}

static const struct of_device_id sun55i_a523_ccu_ids[] = {
	{ .compatible = "allwinner,sun55i-a523-ccu" },
	{ }
};

static struct platform_driver sun55i_a523_ccu_driver = {
	.probe	= sun55i_a523_ccu_probe,
	.driver	= {
		.name			= "sun55i-a523-ccu",
		.suppress_bind_attrs	= true,
		.of_match_table		= sun55i_a523_ccu_ids,
	},
};
module_platform_driver(sun55i_a523_ccu_driver);

MODULE_IMPORT_NS("SUNXI_CCU");
MODULE_DESCRIPTION("Support for the Allwinner A523 CCU");
MODULE_LICENSE("GPL");
