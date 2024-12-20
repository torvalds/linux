// SPDX-License-Identifier: GPL-2.0
/*
 * SAMA7D65 PMC code.
 *
 * Copyright (C) 2024 Microchip Technology Inc. and its subsidiaries
 *
 * Author: Ryan Wanner <ryan.wanner@microchip.com>
 */
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/mfd/syscon.h>
#include <linux/slab.h>

#include <dt-bindings/clock/at91.h>

#include "pmc.h"

static DEFINE_SPINLOCK(pmc_pll_lock);
static DEFINE_SPINLOCK(pmc_mck0_lock);
static DEFINE_SPINLOCK(pmc_mckX_lock);

#define PMC_INDEX_MAX	25

/*
 * PLL clocks identifiers
 * @PLL_ID_CPU:		CPU PLL identifier
 * @PLL_ID_SYS:		System PLL identifier
 * @PLL_ID_DDR:		DDR PLL identifier
 * @PLL_ID_GPU:		Graphics subsystem PLL identifier
 * @PLL_ID_BAUD:	Baud PLL identifier
 * @PLL_ID_AUDIO:	Audio PLL identifier
 * @PLL_ID_ETH:		Ethernet PLL identifier
 * @PLL_ID_LVDS:	LVDS PLL identifier
 * @PLL_ID_USB:		USB PLL identifier
 */
enum pll_ids {
	PLL_ID_CPU,
	PLL_ID_SYS,
	PLL_ID_DDR,
	PLL_ID_GPU,
	PLL_ID_BAUD,
	PLL_ID_AUDIO,
	PLL_ID_ETH,
	PLL_ID_LVDS,
	PLL_ID_USB,
	PLL_ID_MAX
};

/*
 * PLL component identifier
 * @PLL_COMPID_FRAC: Fractional PLL component identifier
 * @PLL_COMPID_DIV0: 1st PLL divider component identifier
 * @PLL_COMPID_DIV1: 2nd PLL divider component identifier
 */
enum pll_component_id {
	PLL_COMPID_FRAC,
	PLL_COMPID_DIV0,
	PLL_COMPID_DIV1,
	PLL_COMPID_MAX
};

/*
 * PLL type identifiers
 * @PLL_TYPE_FRAC:	fractional PLL identifier
 * @PLL_TYPE_DIV:	divider PLL identifier
 */
enum pll_type {
	PLL_TYPE_FRAC,
	PLL_TYPE_DIV
};

/* Layout for fractional PLLs. */
static const struct clk_pll_layout pll_layout_frac = {
	.mul_mask	= GENMASK(31, 24),
	.frac_mask	= GENMASK(21, 0),
	.mul_shift	= 24,
	.frac_shift	= 0,
};

/* Layout for DIVPMC dividers. */
static const struct clk_pll_layout pll_layout_divpmc = {
	.div_mask	= GENMASK(7, 0),
	.endiv_mask	= BIT(29),
	.div_shift	= 0,
	.endiv_shift	= 29,
};

/* Layout for DIVIO dividers. */
static const struct clk_pll_layout pll_layout_divio = {
	.div_mask	= GENMASK(19, 12),
	.endiv_mask	= BIT(30),
	.div_shift	= 12,
	.endiv_shift	= 30,
};

/*
 * CPU PLL output range.
 * Notice: The upper limit has been setup to 1000000002 due to hardware
 * block which cannot output exactly 1GHz.
 */
static const struct clk_range cpu_pll_outputs[] = {
	{ .min = 2343750, .max = 1000000002 },
};

/* PLL output range. */
static const struct clk_range pll_outputs[] = {
	{ .min = 2343750, .max = 1200000000 },
};

/*
 * Min: fCOREPLLCK = 600 MHz, PMC_PLL_CTRL0.DIVPMC = 255
 * Max: fCOREPLLCK = 800 MHz, PMC_PLL_CTRL0.DIVPMC = 0
 */
static const struct clk_range lvdspll_outputs[] = {
	{ .min = 16406250, .max = 800000000 },
};

static const struct clk_range upll_outputs[] = {
	{ .min = 480000000, .max = 480000000 },
};

/* Fractional PLL core output range. */
static const struct clk_range core_outputs[] = {
	{ .min = 600000000, .max = 1200000000 },
};

static const struct clk_range lvdspll_core_outputs[] = {
	{ .min = 600000000, .max = 1200000000 },
};

static const struct clk_range upll_core_outputs[] = {
	{ .min = 600000000, .max = 1200000000 },
};

/* CPU PLL characteristics. */
static const struct clk_pll_characteristics cpu_pll_characteristics = {
	.input = { .min = 12000000, .max = 50000000 },
	.num_output = ARRAY_SIZE(cpu_pll_outputs),
	.output = cpu_pll_outputs,
	.core_output = core_outputs,
};

/* PLL characteristics. */
static const struct clk_pll_characteristics pll_characteristics = {
	.input = { .min = 12000000, .max = 50000000 },
	.num_output = ARRAY_SIZE(pll_outputs),
	.output = pll_outputs,
	.core_output = core_outputs,
};

static const struct clk_pll_characteristics lvdspll_characteristics = {
	.input = { .min = 12000000, .max = 50000000 },
	.num_output = ARRAY_SIZE(lvdspll_outputs),
	.output = lvdspll_outputs,
	.core_output = lvdspll_core_outputs,
};

static const struct clk_pll_characteristics upll_characteristics = {
	.input = { .min = 20000000, .max = 50000000 },
	.num_output = ARRAY_SIZE(upll_outputs),
	.output = upll_outputs,
	.core_output = upll_core_outputs,
	.upll = true,
};

/*
 * SAMA7D65 PLL possible parents
 * @SAMA7D65_PLL_PARENT_MAINCK: MAINCK is PLL a parent
 * @SAMA7D65_PLL_PARENT_MAIN_XTAL: MAIN XTAL is a PLL parent
 * @SAMA7D65_PLL_PARENT_FRACCK: Frac PLL is a PLL parent (for PLL dividers)
 */
enum sama7d65_pll_parent {
	SAMA7D65_PLL_PARENT_MAINCK,
	SAMA7D65_PLL_PARENT_MAIN_XTAL,
	SAMA7D65_PLL_PARENT_FRACCK
};

/*
 * PLL clocks description
 * @n:		clock name
 * @l:		clock layout
 * @c:		clock characteristics
 * @hw:		pointer to clk_hw
 * @t:		clock type
 * @f:		clock flags
 * @p:		clock parent
 * @eid:	export index in sama7d65->chws[] array
 * @safe_div:	intermediate divider need to be set on PRE_RATE_CHANGE
 *		notification
 */
static struct sama7d65_pll {
	const char *n;
	const struct clk_pll_layout *l;
	const struct clk_pll_characteristics *c;
	struct clk_hw *hw;
	unsigned long f;
	enum sama7d65_pll_parent p;
	u8 t;
	u8 eid;
	u8 safe_div;
} sama7d65_plls[][PLL_COMPID_MAX] = {
	[PLL_ID_CPU] = {
		[PLL_COMPID_FRAC] = {
			.n = "cpupll_fracck",
			.p = SAMA7D65_PLL_PARENT_MAINCK,
			.l = &pll_layout_frac,
			.c = &cpu_pll_characteristics,
			.t = PLL_TYPE_FRAC,
			/*
			 * This feeds cpupll_divpmcck which feeds CPU. It should
			 * not be disabled.
			 */
			.f = CLK_IS_CRITICAL,
		},

		[PLL_COMPID_DIV0] = {
			.n = "cpupll_divpmcck",
			.p = SAMA7D65_PLL_PARENT_FRACCK,
			.l = &pll_layout_divpmc,
			.c = &cpu_pll_characteristics,
			.t = PLL_TYPE_DIV,
			/* This feeds CPU. It should not be disabled. */
			.f = CLK_IS_CRITICAL | CLK_SET_RATE_PARENT,
			.eid = PMC_CPUPLL,
			/*
			 * Safe div=15 should be safe even for switching b/w 1GHz and
			 * 90MHz (frac pll might go up to 1.2GHz).
			 */
			.safe_div = 15,
		},
	},

	[PLL_ID_SYS] = {
		[PLL_COMPID_FRAC] = {
			.n = "syspll_fracck",
			.p = SAMA7D65_PLL_PARENT_MAINCK,
			.l = &pll_layout_frac,
			.c = &pll_characteristics,
			.t = PLL_TYPE_FRAC,
			/*
			 * This feeds syspll_divpmcck which may feed critical parts
			 * of the systems like timers. Therefore it should not be
			 * disabled.
			 */
			.f = CLK_IS_CRITICAL | CLK_SET_RATE_GATE,
		},

		[PLL_COMPID_DIV0] = {
			.n = "syspll_divpmcck",
			.p = SAMA7D65_PLL_PARENT_FRACCK,
			.l = &pll_layout_divpmc,
			.c = &pll_characteristics,
			.t = PLL_TYPE_DIV,
			/*
			 * This may feed critical parts of the systems like timers.
			 * Therefore it should not be disabled.
			 */
			.f = CLK_IS_CRITICAL | CLK_SET_RATE_GATE,
			.eid = PMC_SYSPLL,
		},
	},

	[PLL_ID_DDR] = {
		[PLL_COMPID_FRAC] = {
			.n = "ddrpll_fracck",
			.p = SAMA7D65_PLL_PARENT_MAINCK,
			.l = &pll_layout_frac,
			.c = &pll_characteristics,
			.t = PLL_TYPE_FRAC,
			/*
			 * This feeds ddrpll_divpmcck which feeds DDR. It should not
			 * be disabled.
			 */
			.f = CLK_IS_CRITICAL | CLK_SET_RATE_GATE,
		},

		[PLL_COMPID_DIV0] = {
			.n = "ddrpll_divpmcck",
			.p = SAMA7D65_PLL_PARENT_FRACCK,
			.l = &pll_layout_divpmc,
			.c = &pll_characteristics,
			.t = PLL_TYPE_DIV,
			/* This feeds DDR. It should not be disabled. */
			.f = CLK_IS_CRITICAL | CLK_SET_RATE_GATE,
		},
	},

	[PLL_ID_GPU] = {
		[PLL_COMPID_FRAC] = {
			.n = "gpupll_fracck",
			.p = SAMA7D65_PLL_PARENT_MAINCK,
			.l = &pll_layout_frac,
			.c = &pll_characteristics,
			.t = PLL_TYPE_FRAC,
			.f = CLK_SET_RATE_GATE,
		},

		[PLL_COMPID_DIV0] = {
			.n = "gpupll_divpmcck",
			.p = SAMA7D65_PLL_PARENT_FRACCK,
			.l = &pll_layout_divpmc,
			.c = &pll_characteristics,
			.t = PLL_TYPE_DIV,
			.f = CLK_SET_RATE_GATE | CLK_SET_PARENT_GATE |
			     CLK_SET_RATE_PARENT,
		},
	},

	[PLL_ID_BAUD] = {
		[PLL_COMPID_FRAC] = {
			.n = "baudpll_fracck",
			.p = SAMA7D65_PLL_PARENT_MAINCK,
			.l = &pll_layout_frac,
			.c = &pll_characteristics,
			.t = PLL_TYPE_FRAC,
			.f = CLK_SET_RATE_GATE,
		},

		[PLL_COMPID_DIV0] = {
			.n = "baudpll_divpmcck",
			.p = SAMA7D65_PLL_PARENT_FRACCK,
			.l = &pll_layout_divpmc,
			.c = &pll_characteristics,
			.t = PLL_TYPE_DIV,
			.f = CLK_SET_RATE_GATE | CLK_SET_PARENT_GATE |
			     CLK_SET_RATE_PARENT,
			.eid = PMC_BAUDPLL,
		},
	},

	[PLL_ID_AUDIO] = {
		[PLL_COMPID_FRAC] = {
			.n = "audiopll_fracck",
			.p = SAMA7D65_PLL_PARENT_MAIN_XTAL,
			.l = &pll_layout_frac,
			.c = &pll_characteristics,
			.t = PLL_TYPE_FRAC,
			.f = CLK_SET_RATE_GATE,
		},

		[PLL_COMPID_DIV0] = {
			.n = "audiopll_divpmcck",
			.p = SAMA7D65_PLL_PARENT_FRACCK,
			.l = &pll_layout_divpmc,
			.c = &pll_characteristics,
			.t = PLL_TYPE_DIV,
			.f = CLK_SET_RATE_GATE | CLK_SET_PARENT_GATE |
			     CLK_SET_RATE_PARENT,
			.eid = PMC_AUDIOPMCPLL,
		},

		[PLL_COMPID_DIV1] = {
			.n = "audiopll_diviock",
			.p = SAMA7D65_PLL_PARENT_FRACCK,
			.l = &pll_layout_divio,
			.c = &pll_characteristics,
			.t = PLL_TYPE_DIV,
			.f = CLK_SET_RATE_GATE | CLK_SET_PARENT_GATE |
			     CLK_SET_RATE_PARENT,
			.eid = PMC_AUDIOIOPLL,
		},
	},

	[PLL_ID_ETH] = {
		[PLL_COMPID_FRAC] = {
			.n = "ethpll_fracck",
			.p = SAMA7D65_PLL_PARENT_MAIN_XTAL,
			.l = &pll_layout_frac,
			.c = &pll_characteristics,
			.t = PLL_TYPE_FRAC,
			.f = CLK_SET_RATE_GATE,
		},

		[PLL_COMPID_DIV0] = {
			.n = "ethpll_divpmcck",
			.p = SAMA7D65_PLL_PARENT_FRACCK,
			.l = &pll_layout_divpmc,
			.c = &pll_characteristics,
			.t = PLL_TYPE_DIV,
			.f = CLK_SET_RATE_GATE | CLK_SET_PARENT_GATE |
			     CLK_SET_RATE_PARENT,
			.eid = PMC_ETHPLL,
		},
	},

	[PLL_ID_LVDS] = {
		[PLL_COMPID_FRAC] = {
			.n = "lvdspll_fracck",
			.p = SAMA7D65_PLL_PARENT_MAIN_XTAL,
			.l = &pll_layout_frac,
			.c = &lvdspll_characteristics,
			.t = PLL_TYPE_FRAC,
			.f = CLK_SET_RATE_GATE,
		},

		[PLL_COMPID_DIV0] = {
			.n = "lvdspll_divpmcck",
			.p = SAMA7D65_PLL_PARENT_FRACCK,
			.l = &pll_layout_divpmc,
			.c = &lvdspll_characteristics,
			.t = PLL_TYPE_DIV,
			.f = CLK_SET_RATE_GATE | CLK_SET_PARENT_GATE |
			     CLK_SET_RATE_PARENT,
			.eid = PMC_LVDSPLL,
		},
	},

	[PLL_ID_USB] = {
		[PLL_COMPID_FRAC] = {
			.n = "usbpll_fracck",
			.p = SAMA7D65_PLL_PARENT_MAIN_XTAL,
			.l = &pll_layout_frac,
			.c = &upll_characteristics,
			.t = PLL_TYPE_FRAC,
			.f = CLK_SET_RATE_GATE,
		},

		[PLL_COMPID_DIV0] = {
			.n = "usbpll_divpmcck",
			.p = SAMA7D65_PLL_PARENT_FRACCK,
			.l = &pll_layout_divpmc,
			.c = &upll_characteristics,
			.t = PLL_TYPE_DIV,
			.f = CLK_SET_RATE_GATE | CLK_SET_PARENT_GATE |
			     CLK_SET_RATE_PARENT,
			.eid = PMC_UTMI,
		},
	},
};

/* Used to create an array entry identifying a PLL by its components. */
#define PLL_IDS_TO_ARR_ENTRY(_id, _comp) { PLL_ID_##_id, PLL_COMPID_##_comp}

/*
 * Master clock (MCK[0..9]) description
 * @n:			clock name
 * @ep_chg_chg_id:	index in parents array that specifies the changeable
 * @ep:			extra parents names array (entry formed by PLL components
 *			identifiers (see enum pll_component_id))
 * @hw:			pointer to clk_hw
 *			parent
 * @ep_count:		extra parents count
 * @ep_mux_table:	mux table for extra parents
 * @id:			clock id
 * @eid:		export index in sama7d65->chws[] array
 * @c:			true if clock is critical and cannot be disabled
 */
static struct {
	const char *n;
	struct {
		int pll_id;
		int pll_compid;
	} ep[4];
	struct clk_hw *hw;
	int ep_chg_id;
	u8 ep_count;
	u8 ep_mux_table[4];
	u8 id;
	u8 eid;
	u8 c;
} sama7d65_mckx[] = {
	{ .n = "mck0", }, /* Dummy entry for MCK0 to store hw in probe. */
	{ .n = "mck1",
	  .id = 1,
	  .ep = { PLL_IDS_TO_ARR_ENTRY(SYS, DIV0), },
	  .ep_mux_table = { 5, },
	  .ep_count = 1,
	  .ep_chg_id = INT_MIN,
	  .eid = PMC_MCK1,
	  .c = 1, },

	{ .n = "mck2",
	  .id = 2,
	  .ep = { PLL_IDS_TO_ARR_ENTRY(SYS, DIV0), PLL_IDS_TO_ARR_ENTRY(DDR, DIV0), },
	  .ep_mux_table = { 5, 6, },
	  .ep_count = 2,
	  .ep_chg_id = INT_MIN,
	  .c = 1, },

	{ .n = "mck3",
	  .id = 3,
	  .ep = { PLL_IDS_TO_ARR_ENTRY(SYS, DIV0), PLL_IDS_TO_ARR_ENTRY(DDR, DIV0), },
	  .ep_mux_table = { 5, 6, },
	  .ep_count = 2,
	  .ep_chg_id = INT_MIN,
	  .eid = PMC_MCK3,
	  .c = 1, },

	{ .n = "mck4",
	  .id = 4,
	  .ep = { PLL_IDS_TO_ARR_ENTRY(SYS, DIV0), },
	  .ep_mux_table = { 5, },
	  .ep_count = 1,
	  .ep_chg_id = INT_MIN,
	  .c = 1, },

	{ .n = "mck5",
	  .id = 5,
	  .ep = { PLL_IDS_TO_ARR_ENTRY(SYS, DIV0), },
	  .ep_mux_table = { 5, },
	  .ep_count = 1,
	  .ep_chg_id = INT_MIN,
	  .eid = PMC_MCK5,
	  .c = 1, },

	{ .n = "mck6",
	  .id = 6,
	  .ep = { PLL_IDS_TO_ARR_ENTRY(SYS, DIV0), },
	  .ep_mux_table = { 5, },
	  .ep_chg_id = INT_MIN,
	  .ep_count = 1,
	  .c = 1, },

	{ .n = "mck7",
	  .id = 7,
	  .ep = { PLL_IDS_TO_ARR_ENTRY(SYS, DIV0), },
	  .ep_mux_table = { 5, },
	  .ep_chg_id = INT_MIN,
	  .ep_count = 1, },

	{ .n = "mck8",
	  .id = 8,
	  .ep = { PLL_IDS_TO_ARR_ENTRY(SYS, DIV0), },
	  .ep_mux_table = { 5, },
	  .ep_chg_id = INT_MIN,
	  .ep_count = 1, },

	{ .n = "mck9",
	  .id = 9,
	  .ep = { PLL_IDS_TO_ARR_ENTRY(SYS, DIV0), },
	  .ep_mux_table = { 5, },
	  .ep_chg_id = INT_MIN,
	  .ep_count = 1, },
};

/*
 * System clock description
 * @n:	clock name
 * @p:	clock parent name
 * @id: clock id
 */
static const struct {
	const char *n;
	const char *p;
	u8 id;
} sama7d65_systemck[] = {
	{ .n = "uhpck",		.p = "usbck", .id = 6 },
	{ .n = "pck0",		.p = "prog0", .id = 8, },
	{ .n = "pck1",		.p = "prog1", .id = 9, },
	{ .n = "pck2",		.p = "prog2", .id = 10, },
	{ .n = "pck3",		.p = "prog3", .id = 11, },
	{ .n = "pck4",		.p = "prog4", .id = 12, },
	{ .n = "pck5",		.p = "prog5", .id = 13, },
	{ .n = "pck6",		.p = "prog6", .id = 14, },
	{ .n = "pck7",		.p = "prog7", .id = 15, },
};

/* Mux table for programmable clocks. */
static u32 sama7d65_prog_mux_table[] = { 0, 1, 2, 5, 7, 8, 9, 10, 12 };

/*
 * Peripheral clock parent hw identifier (used to index in sama7d65_mckx[])
 * @PCK_PARENT_HW_MCK0: pck parent hw identifier is MCK0
 * @PCK_PARENT_HW_MCK1: pck parent hw identifier is MCK1
 * @PCK_PARENT_HW_MCK2: pck parent hw identifier is MCK2
 * @PCK_PARENT_HW_MCK3: pck parent hw identifier is MCK3
 * @PCK_PARENT_HW_MCK4: pck parent hw identifier is MCK4
 * @PCK_PARENT_HW_MCK5: pck parent hw identifier is MCK5
 * @PCK_PARENT_HW_MCK6: pck parent hw identifier is MCK6
 * @PCK_PARENT_HW_MCK7: pck parent hw identifier is MCK7
 * @PCK_PARENT_HW_MCK8: pck parent hw identifier is MCK8
 * @PCK_PARENT_HW_MCK9: pck parent hw identifier is MCK9
 * @PCK_PARENT_HW_MAX: max identifier
 */
enum sama7d65_pck_parent_hw_id {
	PCK_PARENT_HW_MCK0,
	PCK_PARENT_HW_MCK1,
	PCK_PARENT_HW_MCK2,
	PCK_PARENT_HW_MCK3,
	PCK_PARENT_HW_MCK4,
	PCK_PARENT_HW_MCK5,
	PCK_PARENT_HW_MCK6,
	PCK_PARENT_HW_MCK7,
	PCK_PARENT_HW_MCK8,
	PCK_PARENT_HW_MCK9,
	PCK_PARENT_HW_MAX
};

/*
 * Peripheral clock description
 * @n:		clock name
 * @p:		clock parent hw id
 * @r:		clock range values
 * @id:		clock id
 * @chgp:	index in parent array of the changeable parent
 */
static struct {
	const char *n;
	enum sama7d65_pck_parent_hw_id p;
	struct clk_range r;
	u8 chgp;
	u8 id;
} sama7d65_periphck[] = {
	{ .n = "pioA_clk",	.p = PCK_PARENT_HW_MCK0, .id = 10, },
	{ .n = "securam_clk",	.p = PCK_PARENT_HW_MCK0, .id = 17, },
	{ .n = "sfr_clk",	.p = PCK_PARENT_HW_MCK7, .id = 18, },
	{ .n = "hsmc_clk",	.p = PCK_PARENT_HW_MCK5, .id = 20, },
	{ .n = "xdmac0_clk",	.p = PCK_PARENT_HW_MCK6, .id = 21, },
	{ .n = "xdmac1_clk",	.p = PCK_PARENT_HW_MCK6, .id = 22, },
	{ .n = "xdmac2_clk",	.p = PCK_PARENT_HW_MCK1, .id = 23, },
	{ .n = "acc_clk",	.p = PCK_PARENT_HW_MCK7, .id = 24, },
	{ .n = "aes_clk",	.p = PCK_PARENT_HW_MCK6, .id = 26, },
	{ .n = "tzaesbasc_clk",	.p = PCK_PARENT_HW_MCK8, .id = 27, },
	{ .n = "asrc_clk",	.p = PCK_PARENT_HW_MCK9, .id = 29, .r = { .max = 200000000, }, },
	{ .n = "cpkcc_clk",	.p = PCK_PARENT_HW_MCK0, .id = 30, },
	{ .n = "eic_clk",	.p = PCK_PARENT_HW_MCK7, .id = 33, },
	{ .n = "flex0_clk",	.p = PCK_PARENT_HW_MCK7, .id = 34, },
	{ .n = "flex1_clk",	.p = PCK_PARENT_HW_MCK7, .id = 35, },
	{ .n = "flex2_clk",	.p = PCK_PARENT_HW_MCK7, .id = 36, },
	{ .n = "flex3_clk",	.p = PCK_PARENT_HW_MCK7, .id = 37, },
	{ .n = "flex4_clk",	.p = PCK_PARENT_HW_MCK8, .id = 38, },
	{ .n = "flex5_clk",	.p = PCK_PARENT_HW_MCK8, .id = 39, },
	{ .n = "flex6_clk",	.p = PCK_PARENT_HW_MCK8, .id = 40, },
	{ .n = "flex7_clk",	.p = PCK_PARENT_HW_MCK8, .id = 41, },
	{ .n = "flex8_clk",	.p = PCK_PARENT_HW_MCK9, .id = 42, },
	{ .n = "flex9_clk",	.p = PCK_PARENT_HW_MCK9, .id = 43, },
	{ .n = "flex10_clk",	.p = PCK_PARENT_HW_MCK9, .id = 44, },
	{ .n = "gmac0_clk",	.p = PCK_PARENT_HW_MCK6, .id = 46, },
	{ .n = "gmac1_clk",	.p = PCK_PARENT_HW_MCK6, .id = 47, },
	{ .n = "gmac0_tsu_clk",	.p = PCK_PARENT_HW_MCK1, .id = 49, },
	{ .n = "gmac1_tsu_clk",	.p = PCK_PARENT_HW_MCK1, .id = 50, },
	{ .n = "icm_clk",	.p = PCK_PARENT_HW_MCK5, .id = 53, },
	{ .n = "i2smcc0_clk",	.p = PCK_PARENT_HW_MCK9, .id = 54, .r = { .max = 200000000, }, },
	{ .n = "i2smcc1_clk",	.p = PCK_PARENT_HW_MCK9, .id = 55, .r = { .max = 200000000, }, },
	{ .n = "lcd_clk",	.p = PCK_PARENT_HW_MCK3, .id = 56, },
	{ .n = "matrix_clk",	.p = PCK_PARENT_HW_MCK5, .id = 57, },
	{ .n = "mcan0_clk",	.p = PCK_PARENT_HW_MCK5, .id = 58, .r = { .max = 200000000, }, },
	{ .n = "mcan1_clk",	.p = PCK_PARENT_HW_MCK5, .id = 59, .r = { .max = 200000000, }, },
	{ .n = "mcan2_clk",	.p = PCK_PARENT_HW_MCK5, .id = 60, .r = { .max = 200000000, }, },
	{ .n = "mcan3_clk",	.p = PCK_PARENT_HW_MCK5, .id = 61, .r = { .max = 200000000, }, },
	{ .n = "mcan4_clk",	.p = PCK_PARENT_HW_MCK5, .id = 62, .r = { .max = 200000000, }, },
	{ .n = "pdmc0_clk",	.p = PCK_PARENT_HW_MCK9, .id = 64, .r = { .max = 200000000, }, },
	{ .n = "pdmc1_clk",	.p = PCK_PARENT_HW_MCK9, .id = 65, .r = { .max = 200000000, }, },
	{ .n = "pit64b0_clk",	.p = PCK_PARENT_HW_MCK7, .id = 66, },
	{ .n = "pit64b1_clk",	.p = PCK_PARENT_HW_MCK7, .id = 67, },
	{ .n = "pit64b2_clk",	.p = PCK_PARENT_HW_MCK7, .id = 68, },
	{ .n = "pit64b3_clk",	.p = PCK_PARENT_HW_MCK8, .id = 69, },
	{ .n = "pit64b4_clk",	.p = PCK_PARENT_HW_MCK8, .id = 70, },
	{ .n = "pit64b5_clk",	.p = PCK_PARENT_HW_MCK8, .id = 71, },
	{ .n = "pwm_clk",	.p = PCK_PARENT_HW_MCK7, .id = 72, },
	{ .n = "qspi0_clk",	.p = PCK_PARENT_HW_MCK5, .id = 73, },
	{ .n = "qspi1_clk",	.p = PCK_PARENT_HW_MCK5, .id = 74, },
	{ .n = "sdmmc0_clk",	.p = PCK_PARENT_HW_MCK1, .id = 75, },
	{ .n = "sdmmc1_clk",	.p = PCK_PARENT_HW_MCK1, .id = 76, },
	{ .n = "sdmmc2_clk",	.p = PCK_PARENT_HW_MCK1, .id = 77, },
	{ .n = "sha_clk",	.p = PCK_PARENT_HW_MCK6, .id = 78, },
	{ .n = "spdifrx_clk",	.p = PCK_PARENT_HW_MCK9, .id = 79, .r = { .max = 200000000, }, },
	{ .n = "spdiftx_clk",	.p = PCK_PARENT_HW_MCK9, .id = 80, .r = { .max = 200000000, }, },
	{ .n = "ssc0_clk",	.p = PCK_PARENT_HW_MCK7, .id = 81, .r = { .max = 200000000, }, },
	{ .n = "ssc1_clk",	.p = PCK_PARENT_HW_MCK8, .id = 82, .r = { .max = 200000000, }, },
	{ .n = "tcb0_ch0_clk",	.p = PCK_PARENT_HW_MCK8, .id = 83, .r = { .max = 200000000, }, },
	{ .n = "tcb0_ch1_clk",	.p = PCK_PARENT_HW_MCK8, .id = 84, .r = { .max = 200000000, }, },
	{ .n = "tcb0_ch2_clk",	.p = PCK_PARENT_HW_MCK8, .id = 85, .r = { .max = 200000000, }, },
	{ .n = "tcb1_ch0_clk",	.p = PCK_PARENT_HW_MCK5, .id = 86, .r = { .max = 200000000, }, },
	{ .n = "tcb1_ch1_clk",	.p = PCK_PARENT_HW_MCK5, .id = 87, .r = { .max = 200000000, }, },
	{ .n = "tcb1_ch2_clk",	.p = PCK_PARENT_HW_MCK5, .id = 88, .r = { .max = 200000000, }, },
	{ .n = "tcpca_clk",	.p = PCK_PARENT_HW_MCK5, .id = 89, },
	{ .n = "tcpcb_clk",	.p = PCK_PARENT_HW_MCK5, .id = 90, },
	{ .n = "tdes_clk",	.p = PCK_PARENT_HW_MCK6, .id = 91, },
	{ .n = "trng_clk",	.p = PCK_PARENT_HW_MCK6, .id = 92, },
	{ .n = "udphsa_clk",	.p = PCK_PARENT_HW_MCK5, .id = 99, },
	{ .n = "udphsb_clk",	.p = PCK_PARENT_HW_MCK5, .id = 100, },
	{ .n = "uhphs_clk",	.p = PCK_PARENT_HW_MCK5, .id = 101, },
	{ .n = "dsi_clk",	.p = PCK_PARENT_HW_MCK3, .id = 103, },
	{ .n = "lvdsc_clk",	.p = PCK_PARENT_HW_MCK3, .id = 104, },
};

/*
 * Generic clock description
 * @n:			clock name
 * @pp:			PLL parents (entry formed by PLL components identifiers
 *			(see enum pll_component_id))
 * @pp_mux_table:	PLL parents mux table
 * @r:			clock output range
 * @pp_chg_id:		id in parent array of changeable PLL parent
 * @pp_count:		PLL parents count
 * @id:			clock id
 */
static const struct {
	const char *n;
	struct {
		int pll_id;
		int pll_compid;
	} pp[8];
	const char pp_mux_table[8];
	struct clk_range r;
	int pp_chg_id;
	u8 pp_count;
	u8 id;
} sama7d65_gck[] = {
	{ .n  = "adc_gclk",
	  .id = 25,
	  .r = { .max = 100000000, },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(BAUD, DIV0), PLL_IDS_TO_ARR_ENTRY(AUDIO, DIV0), },
	  .pp_mux_table = { 8, 9, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "asrc_gclk",
	  .id = 29,
	  .r = { .max = 200000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(AUDIO, DIV0), },
	  .pp_mux_table = { 9, },
	  .pp_count = 1,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex0_gclk",
	  .id = 34,
	  .r = { .max = 34000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(BAUD, DIV0), },
	  .pp_mux_table = {8, },
	  .pp_count = 1,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex1_gclk",
	  .id = 35,
	  .r = { .max = 34000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(BAUD, DIV0), },
	  .pp_mux_table = {8, },
	  .pp_count = 1,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex2_gclk",
	  .id = 36,
	  .r = { .max = 34000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(BAUD, DIV0), },
	  .pp_mux_table = {8, },
	  .pp_count = 1,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex3_gclk",
	  .id = 37,
	  .r = { .max = 34000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(BAUD, DIV0), },
	  .pp_mux_table = {8, },
	  .pp_count = 1,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex4_gclk",
	  .id = 38,
	  .r = { .max = 34000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(BAUD, DIV0), },
	  .pp_mux_table = { 8, },
	  .pp_count = 1,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex5_gclk",
	  .id = 39,
	  .r = { .max = 34000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(BAUD, DIV0), },
	  .pp_mux_table = { 8, },
	  .pp_count = 1,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex6_gclk",
	  .id = 40,
	  .r = { .max = 34000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(BAUD, DIV0), },
	  .pp_mux_table = { 8, },
	  .pp_count = 1,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex7_gclk",
	  .id = 41,
	  .r = { .max = 34000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(BAUD, DIV0), },
	  .pp_mux_table = { 8, },
	  .pp_count = 1,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex8_gclk",
	  .id = 42,
	  .r = { .max = 34000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(BAUD, DIV0), },
	  .pp_mux_table = { 8, },
	  .pp_count = 1,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex9_gclk",
	  .id = 43,
	  .r = { .max = 34000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(BAUD, DIV0), },
	  .pp_mux_table = { 8, },
	  .pp_count = 1,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "flex10_gclk",
	  .id = 44,
	  .r = { .max = 34000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(BAUD, DIV0), },
	  .pp_mux_table = { 8, },
	  .pp_count = 1,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "gmac0_gclk",
	  .id = 46,
	  .r = { .max = 125000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(ETH, DIV0), },
	  .pp_mux_table = { 10, },
	  .pp_count = 1,
	  .pp_chg_id = 4, },

	{ .n  = "gmac1_gclk",
	  .id = 47,
	  .r = { .max = 125000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(ETH, DIV0), },
	  .pp_mux_table = { 10, },
	  .pp_count = 1,
	  .pp_chg_id = 4, },

	{ .n  = "gmac0_tsu_gclk",
	  .id = 49,
	  .r = { .max = 400000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(ETH, DIV0), },
	  .pp_mux_table = {10, },
	  .pp_count = 1,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "gmac1_tsu_gclk",
	  .id = 50,
	  .r = { .max = 400000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(ETH, DIV0), },
	  .pp_mux_table = { 10, },
	  .pp_count = 1,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "i2smcc0_gclk",
	  .id = 54,
	  .r = { .max = 100000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(AUDIO, DIV0), },
	  .pp_mux_table = { 9, },
	  .pp_count = 1,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "i2smcc1_gclk",
	  .id = 55,
	  .r = { .max = 100000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(AUDIO, DIV0), },
	  .pp_mux_table = { 9, },
	  .pp_count = 1,
	  .pp_chg_id = INT_MIN, },

	{ .n = "lcdc_gclk",
	  .id = 56,
	  .r = { .max = 90000000 },
	  .pp_count = 0,
	  .pp_chg_id = INT_MIN,
	},

	{ .n  = "mcan0_gclk",
	  .id = 58,
	  .r = { .max = 80000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(USB, DIV0), },
	  .pp_mux_table = { 12 },
	  .pp_count = 1,
	  .pp_chg_id = 4, },

	{ .n  = "mcan1_gclk",
	  .id = 59,
	  .r = { .max = 80000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(USB, DIV0), },
	  .pp_mux_table = { 12 },
	  .pp_count = 1,
	  .pp_chg_id = 4, },

	{ .n  = "mcan2_gclk",
	  .id = 60,
	  .r = { .max = 80000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(USB, DIV0), },
	  .pp_mux_table = { 12 },
	  .pp_count = 1,
	  .pp_chg_id = 4, },

	{ .n  = "mcan3_gclk",
	  .id = 61,
	  .r = { .max = 80000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(USB, DIV0), },
	  .pp_mux_table = { 12 },
	  .pp_count = 1,
	  .pp_chg_id = 4, },

	{ .n  = "mcan4_gclk",
	  .id = 62,
	  .r = { .max = 80000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(USB, DIV0), },
	  .pp_mux_table = { 12 },
	  .pp_count = 1,
	  .pp_chg_id = 4, },

	{ .n  = "pdmc0_gclk",
	  .id = 64,
	  .r = { .max = 80000000  },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(AUDIO, DIV0), },
	  .pp_mux_table = { 9 },
	  .pp_count = 1,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "pdmc1_gclk",
	  .id = 65,
	  .r = { .max = 80000000, },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(AUDIO, DIV0), },
	  .pp_mux_table = { 9, },
	  .pp_count = 1,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "pit64b0_gclk",
	  .id = 66,
	  .r = { .max = 34000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(BAUD, DIV0), PLL_IDS_TO_ARR_ENTRY(AUDIO, DIV0),
		  PLL_IDS_TO_ARR_ENTRY(ETH, DIV0), },
	  .pp_mux_table = { 8, 9, 10, },
	  .pp_count = 3,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "pit64b1_gclk",
	  .id = 67,
	  .r = { .max = 34000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(BAUD, DIV0), PLL_IDS_TO_ARR_ENTRY(AUDIO, DIV0),
		  PLL_IDS_TO_ARR_ENTRY(ETH, DIV0), },
	  .pp_mux_table = { 8, 9, 10, },
	  .pp_count = 3,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "pit64b2_gclk",
	  .id = 68,
	  .r = { .max = 34000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(BAUD, DIV0), PLL_IDS_TO_ARR_ENTRY(AUDIO, DIV0),
		  PLL_IDS_TO_ARR_ENTRY(ETH, DIV0), },
	  .pp_mux_table = { 8, 9, 10, },
	  .pp_count = 3,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "pit64b3_gclk",
	  .id = 69,
	  .r = { .max = 34000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(BAUD, DIV0), PLL_IDS_TO_ARR_ENTRY(AUDIO, DIV0),
		  PLL_IDS_TO_ARR_ENTRY(ETH, DIV0), },
	  .pp_mux_table = {8, 9, 10, },
	  .pp_count = 3,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "pit64b4_gclk",
	  .id = 70,
	  .r = { .max = 34000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(BAUD, DIV0), PLL_IDS_TO_ARR_ENTRY(AUDIO, DIV0),
		  PLL_IDS_TO_ARR_ENTRY(ETH, DIV0), },
	  .pp_mux_table = {8, 9, 10, },
	  .pp_count = 3,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "pit64b5_gclk",
	  .id = 71,
	  .r = { .max = 34000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(BAUD, DIV0), PLL_IDS_TO_ARR_ENTRY(AUDIO, DIV0),
		  PLL_IDS_TO_ARR_ENTRY(ETH, DIV0), },
	  .pp_mux_table = {8, 9, 10, },
	  .pp_count = 3,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "qspi0_gclk",
	  .id = 73,
	  .r = { .max = 400000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(SYS, DIV0), PLL_IDS_TO_ARR_ENTRY(BAUD, DIV0), },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "qspi1_gclk",
	  .id = 74,
	  .r = { .max = 266000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(SYS, DIV0), PLL_IDS_TO_ARR_ENTRY(BAUD, DIV0), },
	  .pp_mux_table = { 5, 8, },
	  .pp_count = 2,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "sdmmc0_gclk",
	  .id = 75,
	  .r = { .max = 208000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(BAUD, DIV0), PLL_IDS_TO_ARR_ENTRY(ETH, DIV0), },
	  .pp_mux_table = { 8, 10, },
	  .pp_count = 2,
	  .pp_chg_id = 4, },

	{ .n  = "sdmmc1_gclk",
	  .id = 76,
	  .r = { .max = 208000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(BAUD, DIV0), PLL_IDS_TO_ARR_ENTRY(ETH, DIV0), },
	  .pp_mux_table = { 8, 10, },
	  .pp_count = 2,
	  .pp_chg_id = 4, },

	{ .n  = "sdmmc2_gclk",
	  .id = 77,
	  .r = { .max = 208000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(BAUD, DIV0), PLL_IDS_TO_ARR_ENTRY(ETH, DIV0), },
	  .pp_mux_table = { 8, 10 },
	  .pp_count = 2,
	  .pp_chg_id = 4, },

	{ .n  = "spdifrx_gclk",
	  .id = 79,
	  .r = { .max = 150000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(AUDIO, DIV0), },
	  .pp_mux_table = { 9, },
	  .pp_count = 1,
	  .pp_chg_id = INT_MIN, },

	{ .n = "spdiftx_gclk",
	  .id = 80,
	  .r = { .max = 25000000  },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(AUDIO, DIV0), },
	  .pp_mux_table = { 9, },
	  .pp_count = 1,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "tcb0_ch0_gclk",
	  .id = 83,
	  .r = { .max = 34000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(BAUD, DIV0), PLL_IDS_TO_ARR_ENTRY(AUDIO, DIV0),
		  PLL_IDS_TO_ARR_ENTRY(ETH, DIV0), },
	  .pp_mux_table = { 8, 9, 10, },
	  .pp_count = 3,
	  .pp_chg_id = INT_MIN, },

	{ .n  = "tcb1_ch0_gclk",
	  .id = 86,
	  .r = { .max = 67000000 },
	  .pp = { PLL_IDS_TO_ARR_ENTRY(BAUD, DIV0), PLL_IDS_TO_ARR_ENTRY(AUDIO, DIV0),
		  PLL_IDS_TO_ARR_ENTRY(ETH, DIV0), },
	  .pp_mux_table = { 8, 9, 10, },
	  .pp_count = 3,
	  .pp_chg_id = INT_MIN, },

	{ .n = "DSI_gclk",
	  .id = 103,
	  .r = {.max = 27000000},
	  .pp = { PLL_IDS_TO_ARR_ENTRY(SYS, DIV0), },
	  .pp_mux_table = {5},
	  .pp_count = 1,
	  .pp_chg_id = INT_MIN, },

	{ .n = "I3CC_gclk",
	  .id = 105,
	  .r = {.max = 125000000},
	  .pp = { PLL_IDS_TO_ARR_ENTRY(BAUD, DIV0), PLL_IDS_TO_ARR_ENTRY(AUDIO, DIV0),
		  PLL_IDS_TO_ARR_ENTRY(ETH, DIV0), },
	  .pp_mux_table = {8, 9, 10, },
	  .pp_count = 3,
	  .pp_chg_id = INT_MIN, },
};

/* MCK0 characteristics. */
static const struct clk_master_characteristics mck0_characteristics = {
	.output = { .min = 32768, .max = 200000000 },
	.divisors = { 1, 2, 4, 3, 5 },
	.have_div3_pres = 1,
};

/* MCK0 layout. */
static const struct clk_master_layout mck0_layout = {
	.mask = 0x773,
	.pres_shift = 4,
	.offset = 0x28,
};

/* Programmable clock layout. */
static const struct clk_programmable_layout programmable_layout = {
	.pres_mask = 0xff,
	.pres_shift = 8,
	.css_mask = 0x1f,
	.have_slck_mck = 0,
	.is_pres_direct = 1,
};

/* Peripheral clock layout. */
static const struct clk_pcr_layout sama7d65_pcr_layout = {
	.offset = 0x88,
	.cmd = BIT(31),
	.gckcss_mask = GENMASK(12, 8),
	.pid_mask = GENMASK(6, 0),
};

static void __init sama7d65_pmc_setup(struct device_node *np)
{
	const char *main_xtal_name = "main_xtal";
	struct pmc_data *sama7d65_pmc;
	const char *parent_names[11];
	void **alloc_mem = NULL;
	int alloc_mem_size = 0;
	struct regmap *regmap;
	struct clk_hw *hw, *main_rc_hw, *main_osc_hw, *main_xtal_hw;
	struct clk_hw *td_slck_hw, *md_slck_hw;
	static struct clk_parent_data parent_data;
	struct clk_hw *parent_hws[10];
	bool bypass;
	int i, j;

	td_slck_hw = __clk_get_hw(of_clk_get_by_name(np, "td_slck"));
	md_slck_hw = __clk_get_hw(of_clk_get_by_name(np, "md_slck"));
	main_xtal_hw = __clk_get_hw(of_clk_get_by_name(np, main_xtal_name));

	if (!td_slck_hw || !md_slck_hw || !main_xtal_hw)
		return;

	regmap = device_node_to_regmap(np);
	if (IS_ERR(regmap))
		return;

	sama7d65_pmc = pmc_data_allocate(PMC_INDEX_MAX,
					 nck(sama7d65_systemck),
					 nck(sama7d65_periphck),
					 nck(sama7d65_gck), 8);
	if (!sama7d65_pmc)
		return;

	alloc_mem = kmalloc(sizeof(void *) *
			    (ARRAY_SIZE(sama7d65_mckx) + ARRAY_SIZE(sama7d65_gck)),
			    GFP_KERNEL);
	if (!alloc_mem)
		goto err_free;

	main_rc_hw = at91_clk_register_main_rc_osc(regmap, "main_rc_osc", 12000000,
						   50000000);
	if (IS_ERR(main_rc_hw))
		goto err_free;

	bypass = of_property_read_bool(np, "atmel,osc-bypass");

	parent_data.name = main_xtal_name;
	parent_data.fw_name = main_xtal_name;
	main_osc_hw = at91_clk_register_main_osc(regmap, "main_osc", NULL,
						 &parent_data, bypass);
	if (IS_ERR(main_osc_hw))
		goto err_free;

	parent_hws[0] = main_rc_hw;
	parent_hws[1] = main_osc_hw;
	hw = at91_clk_register_sam9x5_main(regmap, "mainck", NULL, parent_hws, 2);
	if (IS_ERR(hw))
		goto err_free;

	sama7d65_pmc->chws[PMC_MAIN] = hw;

	for (i = 0; i < PLL_ID_MAX; i++) {
		for (j = 0; j < PLL_COMPID_MAX; j++) {
			struct clk_hw *parent_hw;

			if (!sama7d65_plls[i][j].n)
				continue;

			switch (sama7d65_plls[i][j].t) {
			case PLL_TYPE_FRAC:
				switch (sama7d65_plls[i][j].p) {
				case SAMA7D65_PLL_PARENT_MAINCK:
					parent_hw = sama7d65_pmc->chws[PMC_MAIN];
					break;
				case SAMA7D65_PLL_PARENT_MAIN_XTAL:
					parent_hw = main_xtal_hw;
					break;
				default:
					/* Should not happen. */
					parent_hw = NULL;
					break;
				}

				hw = sam9x60_clk_register_frac_pll(regmap,
					&pmc_pll_lock, sama7d65_plls[i][j].n,
					NULL, parent_hw, i,
					sama7d65_plls[i][j].c,
					sama7d65_plls[i][j].l,
					sama7d65_plls[i][j].f);
				break;

			case PLL_TYPE_DIV:
				hw = sam9x60_clk_register_div_pll(regmap,
					&pmc_pll_lock, sama7d65_plls[i][j].n,
					NULL, sama7d65_plls[i][0].hw, i,
					sama7d65_plls[i][j].c,
					sama7d65_plls[i][j].l,
					sama7d65_plls[i][j].f,
					sama7d65_plls[i][j].safe_div);
				break;

			default:
				continue;
			}

			if (IS_ERR(hw))
				goto err_free;

			sama7d65_plls[i][j].hw = hw;
			if (sama7d65_plls[i][j].eid)
				sama7d65_pmc->chws[sama7d65_plls[i][j].eid] = hw;
		}
	}

	hw = at91_clk_register_master_div(regmap, "mck0", NULL,
					  sama7d65_plls[PLL_ID_CPU][1].hw,
					  &mck0_layout, &mck0_characteristics,
					  &pmc_mck0_lock, CLK_GET_RATE_NOCACHE, 5);
	if (IS_ERR(hw))
		goto err_free;

	sama7d65_pmc->chws[PMC_MCK] = hw;
	sama7d65_mckx[PCK_PARENT_HW_MCK0].hw = hw;

	parent_hws[0] = md_slck_hw;
	parent_hws[1] = td_slck_hw;
	parent_hws[2] = sama7d65_pmc->chws[PMC_MAIN];
	for (i = PCK_PARENT_HW_MCK1; i < ARRAY_SIZE(sama7d65_mckx); i++) {
		u8 num_parents = 3 + sama7d65_mckx[i].ep_count;
		struct clk_hw *tmp_parent_hws[8];
		u32 *mux_table;

		mux_table = kmalloc_array(num_parents, sizeof(*mux_table),
					  GFP_KERNEL);
		if (!mux_table)
			goto err_free;

		alloc_mem[alloc_mem_size++] = mux_table;

		PMC_INIT_TABLE(mux_table, 3);
		PMC_FILL_TABLE(&mux_table[3], sama7d65_mckx[i].ep_mux_table,
			       sama7d65_mckx[i].ep_count);
		for (j = 0; j < sama7d65_mckx[i].ep_count; j++) {
			u8 pll_id = sama7d65_mckx[i].ep[j].pll_id;
			u8 pll_compid = sama7d65_mckx[i].ep[j].pll_compid;

			tmp_parent_hws[j] = sama7d65_plls[pll_id][pll_compid].hw;
		}
		PMC_FILL_TABLE(&parent_hws[3], tmp_parent_hws,
			       sama7d65_mckx[i].ep_count);

		hw = at91_clk_sama7g5_register_master(regmap, sama7d65_mckx[i].n,
						      num_parents, NULL, parent_hws,
						      mux_table, &pmc_mckX_lock,
						      sama7d65_mckx[i].id,
						      sama7d65_mckx[i].c,
						      sama7d65_mckx[i].ep_chg_id);

		if (IS_ERR(hw))
			goto err_free;

		sama7d65_mckx[i].hw = hw;
		if (sama7d65_mckx[i].eid)
			sama7d65_pmc->chws[sama7d65_mckx[i].eid] = hw;
	}

	parent_names[0] = "syspll_divpmcck";
	parent_names[1] = "usbpll_divpmcck";
	parent_names[2] = "main_osc";
	hw = sam9x60_clk_register_usb(regmap, "usbck", parent_names, 3);
	if (IS_ERR(hw))
		goto err_free;

	parent_hws[0] = md_slck_hw;
	parent_hws[1] = td_slck_hw;
	parent_hws[2] = sama7d65_pmc->chws[PMC_MAIN];
	parent_hws[3] = sama7d65_plls[PLL_ID_SYS][PLL_COMPID_DIV0].hw;
	parent_hws[4] = sama7d65_plls[PLL_ID_DDR][PLL_COMPID_DIV0].hw;
	parent_hws[5] = sama7d65_plls[PLL_ID_GPU][PLL_COMPID_DIV0].hw;
	parent_hws[6] = sama7d65_plls[PLL_ID_BAUD][PLL_COMPID_DIV0].hw;
	parent_hws[7] = sama7d65_plls[PLL_ID_AUDIO][PLL_COMPID_DIV0].hw;
	parent_hws[8] = sama7d65_plls[PLL_ID_ETH][PLL_COMPID_DIV0].hw;

	for (i = 0; i < 8; i++) {
		char name[6];

		snprintf(name, sizeof(name), "prog%d", i);

		hw = at91_clk_register_programmable(regmap, name, NULL, parent_hws,
						    9, i,
						    &programmable_layout,
						    sama7d65_prog_mux_table);
		if (IS_ERR(hw))
			goto err_free;

		sama7d65_pmc->pchws[i] = hw;
	}

	for (i = 0; i < ARRAY_SIZE(sama7d65_systemck); i++) {
		hw = at91_clk_register_system(regmap, sama7d65_systemck[i].n,
					      sama7d65_systemck[i].p, NULL,
					      sama7d65_systemck[i].id, 0);
		if (IS_ERR(hw))
			goto err_free;

		sama7d65_pmc->shws[sama7d65_systemck[i].id] = hw;
	}

	for (i = 0; i < ARRAY_SIZE(sama7d65_periphck); i++) {
		hw = at91_clk_register_sam9x5_peripheral(regmap, &pmc_pcr_lock,
							 &sama7d65_pcr_layout,
							 sama7d65_periphck[i].n,
							 NULL,
							 sama7d65_mckx[sama7d65_periphck[i].p].hw,
							 sama7d65_periphck[i].id,
							 &sama7d65_periphck[i].r,
							 sama7d65_periphck[i].chgp ? 0 :
							 INT_MIN, 0);
		if (IS_ERR(hw))
			goto err_free;

		sama7d65_pmc->phws[sama7d65_periphck[i].id] = hw;
	}

	parent_hws[0] = md_slck_hw;
	parent_hws[1] = td_slck_hw;
	parent_hws[2] = sama7d65_pmc->chws[PMC_MAIN];
	parent_hws[3] = sama7d65_pmc->chws[PMC_MCK1];
	for (i = 0; i < ARRAY_SIZE(sama7d65_gck); i++) {
		u8 num_parents = 4 + sama7d65_gck[i].pp_count;
		struct clk_hw *tmp_parent_hws[8];
		u32 *mux_table;

		mux_table = kmalloc_array(num_parents, sizeof(*mux_table),
					  GFP_KERNEL);
		if (!mux_table)
			goto err_free;

		alloc_mem[alloc_mem_size++] = mux_table;

		PMC_INIT_TABLE(mux_table, 4);
		PMC_FILL_TABLE(&mux_table[4], sama7d65_gck[i].pp_mux_table,
			       sama7d65_gck[i].pp_count);
		for (j = 0; j < sama7d65_gck[i].pp_count; j++) {
			u8 pll_id = sama7d65_gck[i].pp[j].pll_id;
			u8 pll_compid = sama7d65_gck[i].pp[j].pll_compid;

			tmp_parent_hws[j] = sama7d65_plls[pll_id][pll_compid].hw;
		}
		PMC_FILL_TABLE(&parent_hws[4], tmp_parent_hws,
			       sama7d65_gck[i].pp_count);

		hw = at91_clk_register_generated(regmap, &pmc_pcr_lock,
						 &sama7d65_pcr_layout,
						 sama7d65_gck[i].n, NULL,
						 parent_hws, mux_table,
						 num_parents,
						 sama7d65_gck[i].id,
						 &sama7d65_gck[i].r,
						 sama7d65_gck[i].pp_chg_id);
		if (IS_ERR(hw))
			goto err_free;

		sama7d65_pmc->ghws[sama7d65_gck[i].id] = hw;
	}

	of_clk_add_hw_provider(np, of_clk_hw_pmc_get, sama7d65_pmc);
	kfree(alloc_mem);

	return;

err_free:
	if (alloc_mem) {
		for (i = 0; i < alloc_mem_size; i++)
			kfree(alloc_mem[i]);
		kfree(alloc_mem);
	}

	kfree(sama7d65_pmc);
}

/* Some clks are used for a clocksource */
CLK_OF_DECLARE(sama7d65_pmc, "microchip,sama7d65-pmc", sama7d65_pmc_setup);
