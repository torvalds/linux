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

#include <dt-bindings/clock/sun55i-a523-ccu.h>
#include <dt-bindings/reset/sun55i-a523-ccu.h>

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
static const struct clk_hw *pll_periph0_150M_hws[] = {
	&pll_periph0_150M_clk.hw
};

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
	.min_rate	= 90000000U,
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
static const struct clk_hw *ahb_hws[] = { &ahb_clk.common.hw };

static SUNXI_CCU_M_DATA_WITH_MUX(apb0_clk, "apb0", ahb_apb0_parents, 0x520,
				 0, 5,		/* M */
				 24, 2,	/* mux */
				 0);
static const struct clk_hw *apb0_hws[] = { &apb0_clk.common.hw };

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
static const struct clk_hw *apb1_hws[] = { &apb1_clk.common.hw };

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
					    CLK_IS_CRITICAL,
					    CCU_FEATURE_UPDATE_BIT);

static const struct clk_hw *mbus_hws[] = { &mbus_clk.common.hw };

/**************************************************************************
 *                          mod clocks with gates                         *
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

static SUNXI_CCU_GATE_HWS(bus_de_clk, "bus-de", ahb_hws, 0x60c, BIT(0), 0);

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

static SUNXI_CCU_GATE_HWS(bus_di_clk, "bus-di", ahb_hws, 0x62c, BIT(0), 0);

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

static SUNXI_CCU_GATE_HWS(bus_g2d_clk, "bus-g2d", ahb_hws, 0x63c, BIT(0), 0);

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

static SUNXI_CCU_GATE_HWS(bus_gpu_clk, "bus-gpu", ahb_hws, 0x67c, BIT(0), 0);

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

static SUNXI_CCU_GATE_HWS(bus_ce_clk, "bus-ce", ahb_hws, 0x68c, BIT(0), 0);
static SUNXI_CCU_GATE_HWS(bus_ce_sys_clk, "bus-ce-sys", ahb_hws, 0x68c,
			  BIT(1), 0);

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

static SUNXI_CCU_GATE_HWS(bus_ve_clk, "bus-ve", ahb_hws, 0x69c, BIT(0), 0);

static const struct clk_hw *npu_parents[] = {
	&pll_periph0_480M_clk.common.hw,
	&pll_periph0_600M_clk.hw,
	&pll_periph0_800M_clk.common.hw,
	&pll_npu_2x_clk.hw,
};
static SUNXI_CCU_M_HW_WITH_MUX_GATE(npu_clk, "npu", npu_parents, 0x6e0,
				    0, 5,	/* M */
				    24, 3,	/* mux */
				    BIT(31),	/* gate */
				    CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE_HWS(bus_dma_clk, "bus-dma", ahb_hws, 0x70c, BIT(0), 0);

static SUNXI_CCU_GATE_HWS(bus_msgbox_clk, "bus-msgbox", ahb_hws, 0x71c,
			  BIT(0), 0);

static SUNXI_CCU_GATE_HWS(bus_spinlock_clk, "bus-spinlock", ahb_hws, 0x72c,
			  BIT(0), 0);

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

static SUNXI_CCU_GATE_HWS(bus_hstimer_clk, "bus-hstimer", ahb_hws, 0x74c,
			  BIT(0), 0);

static SUNXI_CCU_GATE_HWS(bus_dbg_clk, "bus-dbg", ahb_hws, 0x78c,
			  BIT(0), 0);

static SUNXI_CCU_GATE_HWS(bus_pwm0_clk, "bus-pwm0", apb1_hws, 0x7ac, BIT(0), 0);
static SUNXI_CCU_GATE_HWS(bus_pwm1_clk, "bus-pwm1", apb1_hws, 0x7ac, BIT(1), 0);

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

static SUNXI_CCU_GATE_HWS(bus_iommu_clk, "bus-iommu", apb0_hws, 0x7bc,
			  BIT(0), 0);

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

static SUNXI_CCU_GATE_HWS(mbus_dma_clk, "mbus-dma", mbus_hws,
			  0x804, BIT(0), 0);
static SUNXI_CCU_GATE_HWS(mbus_ve_clk, "mbus-ve", mbus_hws,
			  0x804, BIT(1), 0);
static SUNXI_CCU_GATE_HWS(mbus_ce_clk, "mbus-ce", mbus_hws,
			  0x804, BIT(2), 0);
static SUNXI_CCU_GATE_HWS(mbus_nand_clk, "mbus-nand", mbus_hws,
			  0x804, BIT(5), 0);
static SUNXI_CCU_GATE_HWS(mbus_usb3_clk, "mbus-usb3", mbus_hws,
			  0x804, BIT(6), 0);
static SUNXI_CCU_GATE_HWS(mbus_csi_clk, "mbus-csi", mbus_hws,
			  0x804, BIT(8), 0);
static SUNXI_CCU_GATE_HWS(mbus_isp_clk, "mbus-isp", mbus_hws,
			  0x804, BIT(9), 0);
static SUNXI_CCU_GATE_HWS(mbus_gmac1_clk, "mbus-gmac1", mbus_hws,
			  0x804, BIT(12), 0);

static SUNXI_CCU_GATE_HWS(bus_dram_clk, "bus-dram", ahb_hws, 0x80c,
			  BIT(0), CLK_IS_CRITICAL);

static const struct clk_parent_data nand_mmc_parents[] = {
	{ .fw_name = "hosc" },
	{ .hw = &pll_periph0_400M_clk.hw },
	{ .hw = &pll_periph0_300M_clk.hw },
	{ .hw = &pll_periph1_400M_clk.hw },
	{ .hw = &pll_periph1_300M_clk.hw },
};

static SUNXI_CCU_M_DATA_WITH_MUX_GATE(nand0_clk, "nand0", nand_mmc_parents,
				    0x810,
				    0, 5,	/* M */
				    24, 3,	/* mux */
				    BIT(31),	/* gate */
				    0);

static SUNXI_CCU_M_DATA_WITH_MUX_GATE(nand1_clk, "nand1", nand_mmc_parents,
				    0x814,
				    0, 5,	/* M */
				    24, 3,	/* mux */
				    BIT(31),	/* gate */
				    0);

static SUNXI_CCU_GATE_HWS(bus_nand_clk, "bus-nand", ahb_hws, 0x82c,
			  BIT(0), 0);

static SUNXI_CCU_MP_MUX_GATE_POSTDIV_DUALDIV(mmc0_clk, "mmc0", nand_mmc_parents,
					     0x830,
					     0, 5,	/* M */
					     8, 5,	/* P */
					     24, 3,	/* mux */
					     BIT(31),	/* gate */
					     2,		/* post div */
					     0);

static SUNXI_CCU_MP_MUX_GATE_POSTDIV_DUALDIV(mmc1_clk, "mmc1", nand_mmc_parents,
					     0x834,
					     0, 5,	/* M */
					     8, 5,	/* P */
					     24, 3,	/* mux */
					     BIT(31),	/* gate */
					     2,		/* post div */
					     0);

static const struct clk_parent_data mmc2_parents[] = {
	{ .fw_name = "hosc" },
	{ .hw = &pll_periph0_800M_clk.common.hw },
	{ .hw = &pll_periph0_600M_clk.hw },
	{ .hw = &pll_periph1_800M_clk.common.hw },
	{ .hw = &pll_periph1_600M_clk.hw },
};

static SUNXI_CCU_MP_MUX_GATE_POSTDIV_DUALDIV(mmc2_clk, "mmc2", mmc2_parents,
					     0x838,
					     0, 5,	/* M */
					     8, 5,	/* P */
					     24, 3,	/* mux */
					     BIT(31),	/* gate */
					     2,		/* post div */
					     0);

static SUNXI_CCU_GATE_HWS(bus_mmc0_clk, "bus-mmc0", ahb_hws, 0x84c, BIT(0), 0);
static SUNXI_CCU_GATE_HWS(bus_mmc1_clk, "bus-mmc1", ahb_hws, 0x84c, BIT(1), 0);
static SUNXI_CCU_GATE_HWS(bus_mmc2_clk, "bus-mmc2", ahb_hws, 0x84c, BIT(2), 0);

static SUNXI_CCU_GATE_HWS(bus_sysdap_clk, "bus-sysdap", apb1_hws, 0x88c,
			  BIT(0), 0);

static SUNXI_CCU_GATE_HWS(bus_uart0_clk, "bus-uart0", apb1_hws, 0x90c,
			  BIT(0), 0);
static SUNXI_CCU_GATE_HWS(bus_uart1_clk, "bus-uart1", apb1_hws, 0x90c,
			  BIT(1), 0);
static SUNXI_CCU_GATE_HWS(bus_uart2_clk, "bus-uart2", apb1_hws, 0x90c,
			  BIT(2), 0);
static SUNXI_CCU_GATE_HWS(bus_uart3_clk, "bus-uart3", apb1_hws, 0x90c,
			  BIT(3), 0);
static SUNXI_CCU_GATE_HWS(bus_uart4_clk, "bus-uart4", apb1_hws, 0x90c,
			  BIT(4), 0);
static SUNXI_CCU_GATE_HWS(bus_uart5_clk, "bus-uart5", apb1_hws, 0x90c,
			  BIT(5), 0);
static SUNXI_CCU_GATE_HWS(bus_uart6_clk, "bus-uart6", apb1_hws, 0x90c,
			  BIT(6), 0);
static SUNXI_CCU_GATE_HWS(bus_uart7_clk, "bus-uart7", apb1_hws, 0x90c,
			  BIT(7), 0);

static SUNXI_CCU_GATE_HWS(bus_i2c0_clk, "bus-i2c0", apb1_hws, 0x91c, BIT(0), 0);
static SUNXI_CCU_GATE_HWS(bus_i2c1_clk, "bus-i2c1", apb1_hws, 0x91c, BIT(1), 0);
static SUNXI_CCU_GATE_HWS(bus_i2c2_clk, "bus-i2c2", apb1_hws, 0x91c, BIT(2), 0);
static SUNXI_CCU_GATE_HWS(bus_i2c3_clk, "bus-i2c3", apb1_hws, 0x91c, BIT(3), 0);
static SUNXI_CCU_GATE_HWS(bus_i2c4_clk, "bus-i2c4", apb1_hws, 0x91c, BIT(4), 0);
static SUNXI_CCU_GATE_HWS(bus_i2c5_clk, "bus-i2c5", apb1_hws, 0x91c, BIT(5), 0);

static SUNXI_CCU_GATE_HWS(bus_can_clk, "bus-can", apb1_hws, 0x92c, BIT(0), 0);

static const struct clk_parent_data spi_parents[] = {
	{ .fw_name = "hosc" },
	{ .hw = &pll_periph0_300M_clk.hw },
	{ .hw = &pll_periph0_200M_clk.hw },
	{ .hw = &pll_periph1_300M_clk.hw },
	{ .hw = &pll_periph1_200M_clk.hw },
};
static SUNXI_CCU_DUALDIV_MUX_GATE(spi0_clk, "spi0", spi_parents, 0x940,
				  0, 5,		/* M */
				  8, 5,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  0);
static SUNXI_CCU_DUALDIV_MUX_GATE(spi1_clk, "spi1", spi_parents, 0x944,
				  0, 5,		/* M */
				  8, 5,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  0);
static SUNXI_CCU_DUALDIV_MUX_GATE(spi2_clk, "spi2", spi_parents, 0x948,
				  0, 5,		/* M */
				  8, 5,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  0);
static SUNXI_CCU_DUALDIV_MUX_GATE(spifc_clk, "spifc", nand_mmc_parents, 0x950,
				  0, 5,		/* M */
				  8, 5,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  0);
static SUNXI_CCU_GATE_HWS(bus_spi0_clk, "bus-spi0", ahb_hws, 0x96c, BIT(0), 0);
static SUNXI_CCU_GATE_HWS(bus_spi1_clk, "bus-spi1", ahb_hws, 0x96c, BIT(1), 0);
static SUNXI_CCU_GATE_HWS(bus_spi2_clk, "bus-spi2", ahb_hws, 0x96c, BIT(2), 0);
static SUNXI_CCU_GATE_HWS(bus_spifc_clk, "bus-spifc", ahb_hws, 0x96c,
			  BIT(3), 0);

static SUNXI_CCU_GATE_HWS_WITH_PREDIV(emac0_25M_clk, "emac0-25M",
				      pll_periph0_150M_hws,
				      0x970, BIT(31) | BIT(30), 6, 0);
static SUNXI_CCU_GATE_HWS_WITH_PREDIV(emac1_25M_clk, "emac1-25M",
				      pll_periph0_150M_hws,
				      0x974, BIT(31) | BIT(30), 6, 0);
static SUNXI_CCU_GATE_HWS(bus_emac0_clk, "bus-emac0", ahb_hws, 0x97c,
			  BIT(0), 0);
static SUNXI_CCU_GATE_HWS(bus_emac1_clk, "bus-emac1", ahb_hws, 0x98c,
			  BIT(0), 0);

static const struct clk_parent_data ir_rx_parents[] = {
	{ .fw_name = "losc" },
	{ .fw_name = "hosc" },
};

static SUNXI_CCU_M_DATA_WITH_MUX_GATE(ir_rx_clk, "ir-rx", ir_rx_parents, 0x990,
				      0, 5,	/* M */
				      24, 1,	/* mux */
				      BIT(31),	/* gate */
				      0);
static SUNXI_CCU_GATE_HWS(bus_ir_rx_clk, "bus-ir-rx", apb0_hws, 0x99c,
			  BIT(0), 0);

static const struct clk_parent_data ir_tx_ledc_parents[] = {
	{ .fw_name = "hosc" },
	{ .hw = &pll_periph1_600M_clk.hw },
};
static SUNXI_CCU_M_DATA_WITH_MUX_GATE(ir_tx_clk, "ir-tx", ir_tx_ledc_parents,
				      0x9c0,
				      0, 5,	/* M */
				      24, 1,	/* mux */
				      BIT(31),	/* gate */
				      0);
static SUNXI_CCU_GATE_HWS(bus_ir_tx_clk, "bus-ir-tx", apb0_hws, 0x9cc,
			  BIT(0), 0);

static SUNXI_CCU_M_WITH_GATE(gpadc0_clk, "gpadc0", "hosc", 0x9e0,
				 0, 5,		/* M */
				 BIT(31),	/* gate */
				 0);
static SUNXI_CCU_M_WITH_GATE(gpadc1_clk, "gpadc1", "hosc", 0x9e4,
				 0, 5,		/* M */
				 BIT(31),	/* gate */
				 0);
static SUNXI_CCU_GATE_HWS(bus_gpadc0_clk, "bus-gpadc0", ahb_hws, 0x9ec,
			  BIT(0), 0);
static SUNXI_CCU_GATE_HWS(bus_gpadc1_clk, "bus-gpadc1", ahb_hws, 0x9ec,
			  BIT(1), 0);

static SUNXI_CCU_GATE_HWS(bus_ths_clk, "bus-ths", apb0_hws, 0x9fc, BIT(0), 0);

/*
 * The first parent is a 48 MHz input clock divided by 4. That 48 MHz clock is
 * a 2x multiplier from osc24M synchronized by pll-periph0, and is also used by
 * the OHCI module.
 */
static const struct clk_parent_data usb_ohci_parents[] = {
	{ .hw = &pll_periph0_4x_clk.common.hw },
	{ .fw_name = "hosc" },
	{ .fw_name = "losc" },
	{ .fw_name = "iosc" },
};
static const struct ccu_mux_fixed_prediv usb_ohci_predivs[] = {
	{ .index = 0, .div = 50 },
	{ .index = 1, .div = 2 },
};

static struct ccu_mux usb_ohci0_clk = {
	.enable		= BIT(31),
	.mux		= {
		.shift		= 24,
		.width		= 2,
		.fixed_predivs	= usb_ohci_predivs,
		.n_predivs	= ARRAY_SIZE(usb_ohci_predivs),
	},
	.common		= {
		.reg		= 0xa70,
		.features	= CCU_FEATURE_FIXED_PREDIV,
		.hw.init	= CLK_HW_INIT_PARENTS_DATA("usb-ohci0",
							   usb_ohci_parents,
							   &ccu_mux_ops,
							   0),
	},
};

static struct ccu_mux usb_ohci1_clk = {
	.enable		= BIT(31),
	.mux		= {
		.shift		= 24,
		.width		= 2,
		.fixed_predivs	= usb_ohci_predivs,
		.n_predivs	= ARRAY_SIZE(usb_ohci_predivs),
	},
	.common		= {
		.reg		= 0xa74,
		.features	= CCU_FEATURE_FIXED_PREDIV,
		.hw.init	= CLK_HW_INIT_PARENTS_DATA("usb-ohci1",
							   usb_ohci_parents,
							   &ccu_mux_ops,
							   0),
	},
};

static SUNXI_CCU_GATE_HWS(bus_ohci0_clk, "bus-ohci0", ahb_hws, 0xa8c,
			  BIT(0), 0);
static SUNXI_CCU_GATE_HWS(bus_ohci1_clk, "bus-ohci1", ahb_hws, 0xa8c,
			  BIT(1), 0);
static SUNXI_CCU_GATE_HWS(bus_ehci0_clk, "bus-ehci0", ahb_hws, 0xa8c,
			  BIT(4), 0);
static SUNXI_CCU_GATE_HWS(bus_ehci1_clk, "bus-ehci1", ahb_hws, 0xa8c,
			  BIT(5), 0);
static SUNXI_CCU_GATE_HWS(bus_otg_clk, "bus-otg", ahb_hws, 0xa8c, BIT(8), 0);

static SUNXI_CCU_GATE_HWS(bus_lradc_clk, "bus-lradc", apb0_hws, 0xa9c,
			  BIT(0), 0);

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

static SUNXI_CCU_GATE_HWS(bus_display0_top_clk, "bus-display0-top", ahb_hws,
			  0xabc, BIT(0), 0);
static SUNXI_CCU_GATE_HWS(bus_display1_top_clk, "bus-display1-top", ahb_hws,
			  0xacc, BIT(0), 0);

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

static SUNXI_CCU_GATE_HWS(bus_hdmi_clk, "bus-hdmi", ahb_hws, 0xb1c, BIT(0), 0);

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

static SUNXI_CCU_GATE_HWS(bus_mipi_dsi0_clk, "bus-mipi-dsi0", ahb_hws, 0xb4c,
			  BIT(0), 0);

static SUNXI_CCU_GATE_HWS(bus_mipi_dsi1_clk, "bus-mipi-dsi1", ahb_hws, 0xb4c,
			  BIT(1), 0);

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

static SUNXI_CCU_GATE_HWS(bus_tcon_lcd0_clk, "bus-tcon-lcd0", ahb_hws, 0xb7c,
			  BIT(0), 0);
static SUNXI_CCU_GATE_HWS(bus_tcon_lcd1_clk, "bus-tcon-lcd1", ahb_hws, 0xb7c,
			  BIT(1), 0);
static SUNXI_CCU_GATE_HWS(bus_tcon_lcd2_clk, "bus-tcon-lcd2", ahb_hws, 0xb7c,
			  BIT(2), 0);

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

static SUNXI_CCU_GATE_HWS(bus_tcon_tv0_clk, "bus-tcon-tv0", ahb_hws, 0xb9c,
			  BIT(0), 0);
static SUNXI_CCU_GATE_HWS(bus_tcon_tv1_clk, "bus-tcon-tv1", ahb_hws, 0xb9c,
			  BIT(1), 0);

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

static SUNXI_CCU_GATE_HWS(bus_edp_clk, "bus-edp", ahb_hws, 0xbbc, BIT(0), 0);

static SUNXI_CCU_M_DATA_WITH_MUX_GATE(ledc_clk, "ledc", ir_tx_ledc_parents,
				      0xbf0,
				      0, 4,	/* M */
				      24, 1,	/* mux */
				      BIT(31),	/* gate */
				      0);

static SUNXI_CCU_GATE_HWS(bus_ledc_clk, "bus-ledc", apb0_hws, 0xbfc, BIT(0), 0);

static const struct clk_hw *csi_top_parents[] = {
	&pll_periph0_300M_clk.hw,
	&pll_periph0_400M_clk.hw,
	&pll_periph0_480M_clk.common.hw,
	&pll_video3_4x_clk.common.hw,
	&pll_video3_3x_clk.hw,
};
static SUNXI_CCU_M_HW_WITH_MUX_GATE(csi_top_clk, "csi-top", csi_top_parents,
				    0xc04,
				    0, 5,	/* M */
				    24, 3,	/* mux */
				    BIT(31),	/* gate */
				    0);

static const struct clk_parent_data csi_mclk_parents[] = {
	{ .fw_name = "hosc" },
	{ .hw = &pll_video3_4x_clk.common.hw },
	{ .hw = &pll_video0_4x_clk.common.hw },
	{ .hw = &pll_video1_4x_clk.common.hw },
	{ .hw = &pll_video2_4x_clk.common.hw },
};
static SUNXI_CCU_DUALDIV_MUX_GATE(csi_mclk0_clk, "csi-mclk0", csi_mclk_parents,
				  0xc08,
				  0, 5,		/* M */
				  8, 5,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_DUALDIV_MUX_GATE(csi_mclk1_clk, "csi-mclk1", csi_mclk_parents,
				  0xc0c,
				  0, 5,		/* M */
				  8, 5,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_DUALDIV_MUX_GATE(csi_mclk2_clk, "csi-mclk2", csi_mclk_parents,
				  0xc10,
				  0, 5,		/* M */
				  8, 5,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_DUALDIV_MUX_GATE(csi_mclk3_clk, "csi-mclk3", csi_mclk_parents,
				  0xc14,
				  0, 5,		/* M */
				  8, 5,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_GATE_HWS(bus_csi_clk, "bus-csi", ahb_hws, 0xc1c, BIT(0), 0);

static const struct clk_hw *isp_parents[] = {
	&pll_periph0_300M_clk.hw,
	&pll_periph0_400M_clk.hw,
	&pll_video2_4x_clk.common.hw,
	&pll_video3_4x_clk.common.hw,
};
static SUNXI_CCU_M_HW_WITH_MUX_GATE(isp_clk, "isp", isp_parents, 0xc20,
				    0, 5,	/* M */
				    24, 3,	/* mux */
				    BIT(31),	/* gate */
				    0);

static const struct clk_parent_data dsp_parents[] = {
	{ .fw_name = "hosc" },
	{ .fw_name = "losc" },
	{ .fw_name = "iosc" },
	{ .hw = &pll_periph0_2x_clk.common.hw },
	{ .hw = &pll_periph0_480M_clk.common.hw, },
};
static SUNXI_CCU_M_DATA_WITH_MUX_GATE(dsp_clk, "dsp", dsp_parents, 0xc70,
				      0, 5,	/* M */
				      24, 3,	/* mux */
				      BIT(31),	/* gate */
				      0);

static SUNXI_CCU_GATE_DATA(fanout_24M_clk, "fanout-24M", osc24M,
			   0xf30, BIT(0), 0);
static SUNXI_CCU_GATE_DATA_WITH_PREDIV(fanout_12M_clk, "fanout-12M", osc24M,
				       0xf30, BIT(1), 2, 0);
static SUNXI_CCU_GATE_HWS_WITH_PREDIV(fanout_16M_clk, "fanout-16M",
				      pll_periph0_480M_hws,
				      0xf30, BIT(2), 30, 0);
static SUNXI_CCU_GATE_HWS_WITH_PREDIV(fanout_25M_clk, "fanout-25M",
				      pll_periph0_2x_hws,
				      0xf30, BIT(3), 48, 0);
static SUNXI_CCU_GATE_HWS_WITH_PREDIV(fanout_50M_clk, "fanout-50M",
				      pll_periph0_2x_hws,
				      0xf30, BIT(4), 24, 0);

static const struct clk_parent_data fanout_27M_parents[] = {
	{ .hw = &pll_video0_4x_clk.common.hw },
	{ .hw = &pll_video1_4x_clk.common.hw },
	{ .hw = &pll_video2_4x_clk.common.hw },
	{ .hw = &pll_video3_4x_clk.common.hw },
};
static SUNXI_CCU_DUALDIV_MUX_GATE(fanout_27M_clk, "fanout-27M",
				  fanout_27M_parents, 0xf34,
				  0, 5,		/* div0 */
				  8, 5,		/* div1 */
				  24, 2,	/* mux */
				  BIT(31),	/* gate */
				  0);

static const struct clk_parent_data fanout_pclk_parents[] = {
	{ .hw = &apb0_clk.common.hw }
};
static SUNXI_CCU_DUALDIV_MUX_GATE(fanout_pclk_clk, "fanout-pclk",
				  fanout_pclk_parents,
				  0xf38,
				  0, 5,		/* div0 */
				  5, 5,		/* div1 */
				  0, 0,		/* mux */
				  BIT(31),	/* gate */
				  0);

static const struct clk_parent_data fanout_parents[] = {
	{ .fw_name = "losc-fanout" },
	{ .hw = &fanout_12M_clk.common.hw, },
	{ .hw = &fanout_16M_clk.common.hw, },
	{ .hw = &fanout_24M_clk.common.hw, },
	{ .hw = &fanout_25M_clk.common.hw, },
	{ .hw = &fanout_27M_clk.common.hw, },
	{ .hw = &fanout_pclk_clk.common.hw, },
	{ .hw = &fanout_50M_clk.common.hw, },
};
static SUNXI_CCU_MUX_DATA_WITH_GATE(fanout0_clk, "fanout0", fanout_parents,
				    0xf3c,
				    0, 3,	/* mux */
				    BIT(21),	/* gate */
				    0);
static SUNXI_CCU_MUX_DATA_WITH_GATE(fanout1_clk, "fanout1", fanout_parents,
				    0xf3c,
				    3, 3,	/* mux */
				    BIT(22),	/* gate */
				    0);
static SUNXI_CCU_MUX_DATA_WITH_GATE(fanout2_clk, "fanout2", fanout_parents,
				    0xf3c,
				    6, 3,	/* mux */
				    BIT(23),	/* gate */
				    0);

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
	&bus_de_clk.common,
	&di_clk.common,
	&bus_di_clk.common,
	&g2d_clk.common,
	&bus_g2d_clk.common,
	&gpu_clk.common,
	&bus_gpu_clk.common,
	&ce_clk.common,
	&bus_ce_clk.common,
	&bus_ce_sys_clk.common,
	&ve_clk.common,
	&bus_ve_clk.common,
	&npu_clk.common,
	&bus_dma_clk.common,
	&bus_msgbox_clk.common,
	&bus_spinlock_clk.common,
	&hstimer0_clk.common,
	&hstimer1_clk.common,
	&hstimer2_clk.common,
	&hstimer3_clk.common,
	&hstimer4_clk.common,
	&hstimer5_clk.common,
	&bus_hstimer_clk.common,
	&bus_dbg_clk.common,
	&bus_pwm0_clk.common,
	&bus_pwm1_clk.common,
	&iommu_clk.common,
	&bus_iommu_clk.common,
	&dram_clk.common,
	&mbus_dma_clk.common,
	&mbus_ve_clk.common,
	&mbus_ce_clk.common,
	&mbus_nand_clk.common,
	&mbus_usb3_clk.common,
	&mbus_csi_clk.common,
	&mbus_isp_clk.common,
	&mbus_gmac1_clk.common,
	&bus_dram_clk.common,
	&nand0_clk.common,
	&nand1_clk.common,
	&bus_nand_clk.common,
	&mmc0_clk.common,
	&mmc1_clk.common,
	&mmc2_clk.common,
	&bus_sysdap_clk.common,
	&bus_mmc0_clk.common,
	&bus_mmc1_clk.common,
	&bus_mmc2_clk.common,
	&bus_uart0_clk.common,
	&bus_uart1_clk.common,
	&bus_uart2_clk.common,
	&bus_uart3_clk.common,
	&bus_uart4_clk.common,
	&bus_uart5_clk.common,
	&bus_uart6_clk.common,
	&bus_uart7_clk.common,
	&bus_i2c0_clk.common,
	&bus_i2c1_clk.common,
	&bus_i2c2_clk.common,
	&bus_i2c3_clk.common,
	&bus_i2c4_clk.common,
	&bus_i2c5_clk.common,
	&bus_can_clk.common,
	&spi0_clk.common,
	&spi1_clk.common,
	&spi2_clk.common,
	&spifc_clk.common,
	&bus_spi0_clk.common,
	&bus_spi1_clk.common,
	&bus_spi2_clk.common,
	&bus_spifc_clk.common,
	&emac0_25M_clk.common,
	&emac1_25M_clk.common,
	&bus_emac0_clk.common,
	&bus_emac1_clk.common,
	&ir_rx_clk.common,
	&bus_ir_rx_clk.common,
	&ir_tx_clk.common,
	&bus_ir_tx_clk.common,
	&gpadc0_clk.common,
	&gpadc1_clk.common,
	&bus_gpadc0_clk.common,
	&bus_gpadc1_clk.common,
	&bus_ths_clk.common,
	&usb_ohci0_clk.common,
	&usb_ohci1_clk.common,
	&bus_ohci0_clk.common,
	&bus_ohci1_clk.common,
	&bus_ehci0_clk.common,
	&bus_ehci1_clk.common,
	&bus_otg_clk.common,
	&bus_lradc_clk.common,
	&pcie_aux_clk.common,
	&bus_display0_top_clk.common,
	&bus_display1_top_clk.common,
	&hdmi_24M_clk.common,
	&hdmi_cec_32k_clk.common,
	&hdmi_cec_clk.common,
	&bus_hdmi_clk.common,
	&mipi_dsi0_clk.common,
	&mipi_dsi1_clk.common,
	&bus_mipi_dsi0_clk.common,
	&bus_mipi_dsi1_clk.common,
	&tcon_lcd0_clk.common,
	&tcon_lcd1_clk.common,
	&tcon_lcd2_clk.common,
	&combophy_dsi0_clk.common,
	&combophy_dsi1_clk.common,
	&bus_tcon_lcd0_clk.common,
	&bus_tcon_lcd1_clk.common,
	&bus_tcon_lcd2_clk.common,
	&tcon_tv0_clk.common,
	&tcon_tv1_clk.common,
	&bus_tcon_tv0_clk.common,
	&bus_tcon_tv1_clk.common,
	&edp_clk.common,
	&bus_edp_clk.common,
	&ledc_clk.common,
	&bus_ledc_clk.common,
	&csi_top_clk.common,
	&csi_mclk0_clk.common,
	&csi_mclk1_clk.common,
	&csi_mclk2_clk.common,
	&csi_mclk3_clk.common,
	&bus_csi_clk.common,
	&isp_clk.common,
	&dsp_clk.common,
	&fanout_24M_clk.common,
	&fanout_12M_clk.common,
	&fanout_16M_clk.common,
	&fanout_25M_clk.common,
	&fanout_27M_clk.common,
	&fanout_pclk_clk.common,
	&fanout0_clk.common,
	&fanout1_clk.common,
	&fanout2_clk.common,
};

static struct clk_hw_onecell_data sun55i_a523_hw_clks = {
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
		[CLK_BUS_DE]		= &bus_de_clk.common.hw,
		[CLK_DI]		= &di_clk.common.hw,
		[CLK_BUS_DI]		= &bus_di_clk.common.hw,
		[CLK_G2D]		= &g2d_clk.common.hw,
		[CLK_BUS_G2D]		= &bus_g2d_clk.common.hw,
		[CLK_GPU]		= &gpu_clk.common.hw,
		[CLK_BUS_GPU]		= &bus_gpu_clk.common.hw,
		[CLK_CE]		= &ce_clk.common.hw,
		[CLK_BUS_CE]		= &bus_ce_clk.common.hw,
		[CLK_BUS_CE_SYS]	= &bus_ce_sys_clk.common.hw,
		[CLK_VE]		= &ve_clk.common.hw,
		[CLK_BUS_VE]		= &bus_ve_clk.common.hw,
		[CLK_BUS_DMA]		= &bus_dma_clk.common.hw,
		[CLK_BUS_MSGBOX]	= &bus_msgbox_clk.common.hw,
		[CLK_BUS_SPINLOCK]	= &bus_spinlock_clk.common.hw,
		[CLK_HSTIMER0]		= &hstimer0_clk.common.hw,
		[CLK_HSTIMER1]		= &hstimer1_clk.common.hw,
		[CLK_HSTIMER2]		= &hstimer2_clk.common.hw,
		[CLK_HSTIMER3]		= &hstimer3_clk.common.hw,
		[CLK_HSTIMER4]		= &hstimer4_clk.common.hw,
		[CLK_HSTIMER5]		= &hstimer5_clk.common.hw,
		[CLK_BUS_HSTIMER]	= &bus_hstimer_clk.common.hw,
		[CLK_BUS_DBG]		= &bus_dbg_clk.common.hw,
		[CLK_BUS_PWM0]		= &bus_pwm0_clk.common.hw,
		[CLK_BUS_PWM1]		= &bus_pwm1_clk.common.hw,
		[CLK_IOMMU]		= &iommu_clk.common.hw,
		[CLK_BUS_IOMMU]		= &bus_iommu_clk.common.hw,
		[CLK_DRAM]		= &dram_clk.common.hw,
		[CLK_MBUS_DMA]		= &mbus_dma_clk.common.hw,
		[CLK_MBUS_VE]		= &mbus_ve_clk.common.hw,
		[CLK_MBUS_CE]		= &mbus_ce_clk.common.hw,
		[CLK_MBUS_CSI]		= &mbus_csi_clk.common.hw,
		[CLK_MBUS_ISP]		= &mbus_isp_clk.common.hw,
		[CLK_MBUS_EMAC1]	= &mbus_gmac1_clk.common.hw,
		[CLK_BUS_DRAM]		= &bus_dram_clk.common.hw,
		[CLK_NAND0]		= &nand0_clk.common.hw,
		[CLK_NAND1]		= &nand1_clk.common.hw,
		[CLK_BUS_NAND]		= &bus_nand_clk.common.hw,
		[CLK_MMC0]		= &mmc0_clk.common.hw,
		[CLK_MMC1]		= &mmc1_clk.common.hw,
		[CLK_MMC2]		= &mmc2_clk.common.hw,
		[CLK_BUS_SYSDAP]	= &bus_sysdap_clk.common.hw,
		[CLK_BUS_MMC0]		= &bus_mmc0_clk.common.hw,
		[CLK_BUS_MMC1]		= &bus_mmc1_clk.common.hw,
		[CLK_BUS_MMC2]		= &bus_mmc2_clk.common.hw,
		[CLK_BUS_UART0]		= &bus_uart0_clk.common.hw,
		[CLK_BUS_UART1]		= &bus_uart1_clk.common.hw,
		[CLK_BUS_UART2]		= &bus_uart2_clk.common.hw,
		[CLK_BUS_UART3]		= &bus_uart3_clk.common.hw,
		[CLK_BUS_UART4]		= &bus_uart4_clk.common.hw,
		[CLK_BUS_UART5]		= &bus_uart5_clk.common.hw,
		[CLK_BUS_UART6]		= &bus_uart6_clk.common.hw,
		[CLK_BUS_UART7]		= &bus_uart7_clk.common.hw,
		[CLK_BUS_I2C0]		= &bus_i2c0_clk.common.hw,
		[CLK_BUS_I2C1]		= &bus_i2c1_clk.common.hw,
		[CLK_BUS_I2C2]		= &bus_i2c2_clk.common.hw,
		[CLK_BUS_I2C3]		= &bus_i2c3_clk.common.hw,
		[CLK_BUS_I2C4]		= &bus_i2c4_clk.common.hw,
		[CLK_BUS_I2C5]		= &bus_i2c5_clk.common.hw,
		[CLK_BUS_CAN]		= &bus_can_clk.common.hw,
		[CLK_SPI0]		= &spi0_clk.common.hw,
		[CLK_SPI1]		= &spi1_clk.common.hw,
		[CLK_SPI2]		= &spi2_clk.common.hw,
		[CLK_SPIFC]		= &spifc_clk.common.hw,
		[CLK_BUS_SPI0]		= &bus_spi0_clk.common.hw,
		[CLK_BUS_SPI1]		= &bus_spi1_clk.common.hw,
		[CLK_BUS_SPI2]		= &bus_spi2_clk.common.hw,
		[CLK_BUS_SPIFC]		= &bus_spifc_clk.common.hw,
		[CLK_EMAC0_25M]		= &emac0_25M_clk.common.hw,
		[CLK_EMAC1_25M]		= &emac1_25M_clk.common.hw,
		[CLK_BUS_EMAC0]		= &bus_emac0_clk.common.hw,
		[CLK_BUS_EMAC1]		= &bus_emac1_clk.common.hw,
		[CLK_IR_RX]		= &ir_rx_clk.common.hw,
		[CLK_BUS_IR_RX]		= &bus_ir_rx_clk.common.hw,
		[CLK_IR_TX]		= &ir_tx_clk.common.hw,
		[CLK_BUS_IR_TX]		= &bus_ir_tx_clk.common.hw,
		[CLK_GPADC0]		= &gpadc0_clk.common.hw,
		[CLK_GPADC1]		= &gpadc1_clk.common.hw,
		[CLK_BUS_GPADC0]	= &bus_gpadc0_clk.common.hw,
		[CLK_BUS_GPADC1]	= &bus_gpadc1_clk.common.hw,
		[CLK_BUS_THS]		= &bus_ths_clk.common.hw,
		[CLK_USB_OHCI0]		= &usb_ohci0_clk.common.hw,
		[CLK_USB_OHCI1]		= &usb_ohci1_clk.common.hw,
		[CLK_BUS_OHCI0]		= &bus_ohci0_clk.common.hw,
		[CLK_BUS_OHCI1]		= &bus_ohci1_clk.common.hw,
		[CLK_BUS_EHCI0]		= &bus_ehci0_clk.common.hw,
		[CLK_BUS_EHCI1]		= &bus_ehci1_clk.common.hw,
		[CLK_BUS_OTG]		= &bus_otg_clk.common.hw,
		[CLK_BUS_LRADC]		= &bus_lradc_clk.common.hw,
		[CLK_PCIE_AUX]		= &pcie_aux_clk.common.hw,
		[CLK_BUS_DISPLAY0_TOP]	= &bus_display0_top_clk.common.hw,
		[CLK_BUS_DISPLAY1_TOP]	= &bus_display1_top_clk.common.hw,
		[CLK_HDMI_24M]		= &hdmi_24M_clk.common.hw,
		[CLK_HDMI_CEC_32K]	= &hdmi_cec_32k_clk.common.hw,
		[CLK_HDMI_CEC]		= &hdmi_cec_clk.common.hw,
		[CLK_BUS_HDMI]		= &bus_hdmi_clk.common.hw,
		[CLK_MIPI_DSI0]		= &mipi_dsi0_clk.common.hw,
		[CLK_MIPI_DSI1]		= &mipi_dsi1_clk.common.hw,
		[CLK_BUS_MIPI_DSI0]	= &bus_mipi_dsi0_clk.common.hw,
		[CLK_BUS_MIPI_DSI1]	= &bus_mipi_dsi1_clk.common.hw,
		[CLK_TCON_LCD0]		= &tcon_lcd0_clk.common.hw,
		[CLK_TCON_LCD1]		= &tcon_lcd1_clk.common.hw,
		[CLK_TCON_LCD2]		= &tcon_lcd2_clk.common.hw,
		[CLK_COMBOPHY_DSI0]	= &combophy_dsi0_clk.common.hw,
		[CLK_COMBOPHY_DSI1]	= &combophy_dsi1_clk.common.hw,
		[CLK_BUS_TCON_LCD0]	= &bus_tcon_lcd0_clk.common.hw,
		[CLK_BUS_TCON_LCD1]	= &bus_tcon_lcd1_clk.common.hw,
		[CLK_BUS_TCON_LCD2]	= &bus_tcon_lcd2_clk.common.hw,
		[CLK_TCON_TV0]		= &tcon_tv0_clk.common.hw,
		[CLK_TCON_TV1]		= &tcon_tv1_clk.common.hw,
		[CLK_BUS_TCON_TV0]	= &bus_tcon_tv0_clk.common.hw,
		[CLK_BUS_TCON_TV1]	= &bus_tcon_tv1_clk.common.hw,
		[CLK_EDP]		= &edp_clk.common.hw,
		[CLK_BUS_EDP]		= &bus_edp_clk.common.hw,
		[CLK_LEDC]		= &ledc_clk.common.hw,
		[CLK_BUS_LEDC]		= &bus_ledc_clk.common.hw,
		[CLK_CSI_TOP]		= &csi_top_clk.common.hw,
		[CLK_CSI_MCLK0]		= &csi_mclk0_clk.common.hw,
		[CLK_CSI_MCLK1]		= &csi_mclk1_clk.common.hw,
		[CLK_CSI_MCLK2]		= &csi_mclk2_clk.common.hw,
		[CLK_CSI_MCLK3]		= &csi_mclk3_clk.common.hw,
		[CLK_BUS_CSI]		= &bus_csi_clk.common.hw,
		[CLK_ISP]		= &isp_clk.common.hw,
		[CLK_DSP]		= &dsp_clk.common.hw,
		[CLK_FANOUT_24M]	= &fanout_24M_clk.common.hw,
		[CLK_FANOUT_12M]	= &fanout_12M_clk.common.hw,
		[CLK_FANOUT_16M]	= &fanout_16M_clk.common.hw,
		[CLK_FANOUT_25M]	= &fanout_25M_clk.common.hw,
		[CLK_FANOUT_27M]	= &fanout_27M_clk.common.hw,
		[CLK_FANOUT_PCLK]	= &fanout_pclk_clk.common.hw,
		[CLK_FANOUT0]		= &fanout0_clk.common.hw,
		[CLK_FANOUT1]		= &fanout1_clk.common.hw,
		[CLK_FANOUT2]		= &fanout2_clk.common.hw,
		[CLK_NPU]		= &npu_clk.common.hw,
	},
	.num	= CLK_NPU + 1,
};

static struct ccu_reset_map sun55i_a523_ccu_resets[] = {
	[RST_MBUS]		= { 0x540, BIT(30) },
	[RST_BUS_NSI]		= { 0x54c, BIT(16) },
	[RST_BUS_DE]		= { 0x60c, BIT(16) },
	[RST_BUS_DI]		= { 0x62c, BIT(16) },
	[RST_BUS_G2D]		= { 0x63c, BIT(16) },
	[RST_BUS_SYS]		= { 0x64c, BIT(16) },
	[RST_BUS_GPU]		= { 0x67c, BIT(16) },
	[RST_BUS_CE]		= { 0x68c, BIT(16) },
	[RST_BUS_SYS_CE]	= { 0x68c, BIT(17) },
	[RST_BUS_VE]		= { 0x69c, BIT(16) },
	[RST_BUS_DMA]		= { 0x70c, BIT(16) },
	[RST_BUS_MSGBOX]	= { 0x71c, BIT(16) },
	[RST_BUS_SPINLOCK]	= { 0x72c, BIT(16) },
	[RST_BUS_CPUXTIMER]	= { 0x74c, BIT(16) },
	[RST_BUS_DBG]		= { 0x78c, BIT(16) },
	[RST_BUS_PWM0]		= { 0x7ac, BIT(16) },
	[RST_BUS_PWM1]		= { 0x7ac, BIT(17) },
	[RST_BUS_DRAM]		= { 0x80c, BIT(16) },
	[RST_BUS_NAND]		= { 0x82c, BIT(16) },
	[RST_BUS_MMC0]		= { 0x84c, BIT(16) },
	[RST_BUS_MMC1]		= { 0x84c, BIT(17) },
	[RST_BUS_MMC2]		= { 0x84c, BIT(18) },
	[RST_BUS_SYSDAP]	= { 0x88c, BIT(16) },
	[RST_BUS_UART0]		= { 0x90c, BIT(16) },
	[RST_BUS_UART1]		= { 0x90c, BIT(17) },
	[RST_BUS_UART2]		= { 0x90c, BIT(18) },
	[RST_BUS_UART3]		= { 0x90c, BIT(19) },
	[RST_BUS_UART4]		= { 0x90c, BIT(20) },
	[RST_BUS_UART5]		= { 0x90c, BIT(21) },
	[RST_BUS_UART6]		= { 0x90c, BIT(22) },
	[RST_BUS_UART7]		= { 0x90c, BIT(23) },
	[RST_BUS_I2C0]		= { 0x91c, BIT(16) },
	[RST_BUS_I2C1]		= { 0x91c, BIT(17) },
	[RST_BUS_I2C2]		= { 0x91c, BIT(18) },
	[RST_BUS_I2C3]		= { 0x91c, BIT(19) },
	[RST_BUS_I2C4]		= { 0x91c, BIT(20) },
	[RST_BUS_I2C5]		= { 0x91c, BIT(21) },
	[RST_BUS_CAN]		= { 0x92c, BIT(16) },
	[RST_BUS_SPI0]		= { 0x96c, BIT(16) },
	[RST_BUS_SPI1]		= { 0x96c, BIT(17) },
	[RST_BUS_SPI2]		= { 0x96c, BIT(18) },
	[RST_BUS_SPIFC]		= { 0x96c, BIT(19) },
	[RST_BUS_EMAC0]		= { 0x97c, BIT(16) },
	[RST_BUS_EMAC1]		= { 0x98c, BIT(16) | BIT(17) },	/* GMAC1-AXI */
	[RST_BUS_IR_RX]		= { 0x99c, BIT(16) },
	[RST_BUS_IR_TX]		= { 0x9cc, BIT(16) },
	[RST_BUS_GPADC0]	= { 0x9ec, BIT(16) },
	[RST_BUS_GPADC1]	= { 0x9ec, BIT(17) },
	[RST_BUS_THS]		= { 0x9fc, BIT(16) },
	[RST_USB_PHY0]		= { 0xa70, BIT(30) },
	[RST_USB_PHY1]		= { 0xa74, BIT(30) },
	[RST_BUS_OHCI0]		= { 0xa8c, BIT(16) },
	[RST_BUS_OHCI1]		= { 0xa8c, BIT(17) },
	[RST_BUS_EHCI0]		= { 0xa8c, BIT(20) },
	[RST_BUS_EHCI1]		= { 0xa8c, BIT(21) },
	[RST_BUS_OTG]		= { 0xa8c, BIT(24) },
	[RST_BUS_3]		= { 0xa8c, BIT(25) },	/* BSP + register */
	[RST_BUS_LRADC]		= { 0xa9c, BIT(16) },
	[RST_BUS_PCIE_USB3]	= { 0xaac, BIT(16) },
	[RST_BUS_DISPLAY0_TOP]	= { 0xabc, BIT(16) },
	[RST_BUS_DISPLAY1_TOP]	= { 0xacc, BIT(16) },
	[RST_BUS_HDMI_MAIN]	= { 0xb1c, BIT(16) },
	[RST_BUS_HDMI_SUB]	= { 0xb1c, BIT(17) },
	[RST_BUS_MIPI_DSI0]	= { 0xb4c, BIT(16) },
	[RST_BUS_MIPI_DSI1]	= { 0xb4c, BIT(17) },
	[RST_BUS_TCON_LCD0]	= { 0xb7c, BIT(16) },
	[RST_BUS_TCON_LCD1]	= { 0xb7c, BIT(17) },
	[RST_BUS_TCON_LCD2]	= { 0xb7c, BIT(18) },
	[RST_BUS_TCON_TV0]	= { 0xb9c, BIT(16) },
	[RST_BUS_TCON_TV1]	= { 0xb9c, BIT(17) },
	[RST_BUS_LVDS0]		= { 0xbac, BIT(16) },
	[RST_BUS_LVDS1]		= { 0xbac, BIT(17) },
	[RST_BUS_EDP]		= { 0xbbc, BIT(16) },
	[RST_BUS_VIDEO_OUT0]	= { 0xbcc, BIT(16) },
	[RST_BUS_VIDEO_OUT1]	= { 0xbcc, BIT(17) },
	[RST_BUS_LEDC]		= { 0xbfc, BIT(16) },
	[RST_BUS_CSI]		= { 0xc1c, BIT(16) },
	[RST_BUS_ISP]		= { 0xc2c, BIT(16) },	/* BSP + register */
};

static const struct sunxi_ccu_desc sun55i_a523_ccu_desc = {
	.ccu_clks	= sun55i_a523_ccu_clks,
	.num_ccu_clks	= ARRAY_SIZE(sun55i_a523_ccu_clks),

	.hw_clks	= &sun55i_a523_hw_clks,

	.resets		= sun55i_a523_ccu_resets,
	.num_resets	= ARRAY_SIZE(sun55i_a523_ccu_resets),
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
