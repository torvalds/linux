// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2013 Freescale Semiconductor, Inc.
 *
 * clock driver for Freescale QorIQ SoCs.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/fsl/guts.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of.h>
#include <linux/slab.h>

#define PLL_DIV1	0
#define PLL_DIV2	1
#define PLL_DIV3	2
#define PLL_DIV4	3

#define PLATFORM_PLL	0
#define CGA_PLL1	1
#define CGA_PLL2	2
#define CGA_PLL3	3
#define CGA_PLL4	4	/* only on clockgen-1.0, which lacks CGB */
#define CGB_PLL1	4
#define CGB_PLL2	5
#define MAX_PLL_DIV	16

struct clockgen_pll_div {
	struct clk *clk;
	char name[32];
};

struct clockgen_pll {
	struct clockgen_pll_div div[MAX_PLL_DIV];
};

#define CLKSEL_VALID	1
#define CLKSEL_80PCT	2	/* Only allowed if PLL <= 80% of max cpu freq */

struct clockgen_sourceinfo {
	u32 flags;	/* CLKSEL_xxx */
	int pll;	/* CGx_PLLn */
	int div;	/* PLL_DIVn */
};

#define NUM_MUX_PARENTS	16

struct clockgen_muxinfo {
	struct clockgen_sourceinfo clksel[NUM_MUX_PARENTS];
};

#define NUM_HWACCEL	5
#define NUM_CMUX	8

struct clockgen;

/*
 * cmux freq must be >= platform pll.
 * If not set, cmux freq must be >= platform pll/2
 */
#define CG_CMUX_GE_PLAT		1

#define CG_PLL_8BIT		2	/* PLLCnGSR[CFG] is 8 bits, not 6 */
#define CG_VER3			4	/* version 3 cg: reg layout different */
#define CG_LITTLE_ENDIAN	8

struct clockgen_chipinfo {
	const char *compat, *guts_compat;
	const struct clockgen_muxinfo *cmux_groups[2];
	const struct clockgen_muxinfo *hwaccel[NUM_HWACCEL];
	void (*init_periph)(struct clockgen *cg);
	int cmux_to_group[NUM_CMUX + 1]; /* array should be -1 terminated */
	u32 pll_mask;	/* 1 << n bit set if PLL n is valid */
	u32 flags;	/* CG_xxx */
};

struct clockgen {
	struct device_node *node;
	void __iomem *regs;
	struct clockgen_chipinfo info; /* mutable copy */
	struct clk *sysclk, *coreclk;
	struct clockgen_pll pll[6];
	struct clk *cmux[NUM_CMUX];
	struct clk *hwaccel[NUM_HWACCEL];
	struct clk *fman[2];
	struct ccsr_guts __iomem *guts;
};

static struct clockgen clockgen;

static void cg_out(struct clockgen *cg, u32 val, u32 __iomem *reg)
{
	if (cg->info.flags & CG_LITTLE_ENDIAN)
		iowrite32(val, reg);
	else
		iowrite32be(val, reg);
}

static u32 cg_in(struct clockgen *cg, u32 __iomem *reg)
{
	u32 val;

	if (cg->info.flags & CG_LITTLE_ENDIAN)
		val = ioread32(reg);
	else
		val = ioread32be(reg);

	return val;
}

static const struct clockgen_muxinfo p2041_cmux_grp1 = {
	{
		[0] = { CLKSEL_VALID, CGA_PLL1, PLL_DIV1 },
		[1] = { CLKSEL_VALID, CGA_PLL1, PLL_DIV2 },
		[4] = { CLKSEL_VALID, CGA_PLL2, PLL_DIV1 },
	}
};

static const struct clockgen_muxinfo p2041_cmux_grp2 = {
	{
		[0] = { CLKSEL_VALID, CGA_PLL1, PLL_DIV1 },
		[4] = { CLKSEL_VALID, CGA_PLL2, PLL_DIV1 },
		[5] = { CLKSEL_VALID, CGA_PLL2, PLL_DIV2 },
	}
};

static const struct clockgen_muxinfo p5020_cmux_grp1 = {
	{
		[0] = { CLKSEL_VALID, CGA_PLL1, PLL_DIV1 },
		[1] = { CLKSEL_VALID, CGA_PLL1, PLL_DIV2 },
		[4] = { CLKSEL_VALID | CLKSEL_80PCT, CGA_PLL2, PLL_DIV1 },
	}
};

static const struct clockgen_muxinfo p5020_cmux_grp2 = {
	{
		[0] = { CLKSEL_VALID | CLKSEL_80PCT, CGA_PLL1, PLL_DIV1 },
		[4] = { CLKSEL_VALID, CGA_PLL2, PLL_DIV1 },
		[5] = { CLKSEL_VALID, CGA_PLL2, PLL_DIV2 },
	}
};

static const struct clockgen_muxinfo p5040_cmux_grp1 = {
	{
		[0] = { CLKSEL_VALID, CGA_PLL1, PLL_DIV1 },
		[1] = { CLKSEL_VALID, CGA_PLL1, PLL_DIV2 },
		[4] = { CLKSEL_VALID | CLKSEL_80PCT, CGA_PLL2, PLL_DIV1 },
		[5] = { CLKSEL_VALID | CLKSEL_80PCT, CGA_PLL2, PLL_DIV2 },
	}
};

static const struct clockgen_muxinfo p5040_cmux_grp2 = {
	{
		[0] = { CLKSEL_VALID | CLKSEL_80PCT, CGA_PLL1, PLL_DIV1 },
		[1] = { CLKSEL_VALID | CLKSEL_80PCT, CGA_PLL1, PLL_DIV2 },
		[4] = { CLKSEL_VALID, CGA_PLL2, PLL_DIV1 },
		[5] = { CLKSEL_VALID, CGA_PLL2, PLL_DIV2 },
	}
};

static const struct clockgen_muxinfo p4080_cmux_grp1 = {
	{
		[0] = { CLKSEL_VALID, CGA_PLL1, PLL_DIV1 },
		[1] = { CLKSEL_VALID, CGA_PLL1, PLL_DIV2 },
		[4] = { CLKSEL_VALID, CGA_PLL2, PLL_DIV1 },
		[5] = { CLKSEL_VALID, CGA_PLL2, PLL_DIV2 },
		[8] = { CLKSEL_VALID | CLKSEL_80PCT, CGA_PLL3, PLL_DIV1 },
	}
};

static const struct clockgen_muxinfo p4080_cmux_grp2 = {
	{
		[0] = { CLKSEL_VALID | CLKSEL_80PCT, CGA_PLL1, PLL_DIV1 },
		[8] = { CLKSEL_VALID, CGA_PLL3, PLL_DIV1 },
		[9] = { CLKSEL_VALID, CGA_PLL3, PLL_DIV2 },
		[12] = { CLKSEL_VALID, CGA_PLL4, PLL_DIV1 },
		[13] = { CLKSEL_VALID, CGA_PLL4, PLL_DIV2 },
	}
};

static const struct clockgen_muxinfo t1023_cmux = {
	{
		[0] = { CLKSEL_VALID, CGA_PLL1, PLL_DIV1 },
		[1] = { CLKSEL_VALID, CGA_PLL1, PLL_DIV2 },
	}
};

static const struct clockgen_muxinfo t1040_cmux = {
	{
		[0] = { CLKSEL_VALID, CGA_PLL1, PLL_DIV1 },
		[1] = { CLKSEL_VALID, CGA_PLL1, PLL_DIV2 },
		[4] = { CLKSEL_VALID, CGA_PLL2, PLL_DIV1 },
		[5] = { CLKSEL_VALID, CGA_PLL2, PLL_DIV2 },
	}
};


static const struct clockgen_muxinfo clockgen2_cmux_cga = {
	{
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV1 },
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV2 },
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV4 },
		{},
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV1 },
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV2 },
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV4 },
		{},
		{ CLKSEL_VALID, CGA_PLL3, PLL_DIV1 },
		{ CLKSEL_VALID, CGA_PLL3, PLL_DIV2 },
		{ CLKSEL_VALID, CGA_PLL3, PLL_DIV4 },
	},
};

static const struct clockgen_muxinfo clockgen2_cmux_cga12 = {
	{
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV1 },
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV2 },
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV4 },
		{},
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV1 },
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV2 },
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV4 },
	},
};

static const struct clockgen_muxinfo clockgen2_cmux_cgb = {
	{
		{ CLKSEL_VALID, CGB_PLL1, PLL_DIV1 },
		{ CLKSEL_VALID, CGB_PLL1, PLL_DIV2 },
		{ CLKSEL_VALID, CGB_PLL1, PLL_DIV4 },
		{},
		{ CLKSEL_VALID, CGB_PLL2, PLL_DIV1 },
		{ CLKSEL_VALID, CGB_PLL2, PLL_DIV2 },
		{ CLKSEL_VALID, CGB_PLL2, PLL_DIV4 },
	},
};

static const struct clockgen_muxinfo ls1028a_hwa1 = {
	{
		{ CLKSEL_VALID, PLATFORM_PLL, PLL_DIV1 },
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV1 },
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV2 },
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV3 },
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV4 },
		{},
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV2 },
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV3 },
	},
};

static const struct clockgen_muxinfo ls1028a_hwa2 = {
	{
		{ CLKSEL_VALID, PLATFORM_PLL, PLL_DIV1 },
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV1 },
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV2 },
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV3 },
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV4 },
		{},
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV2 },
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV3 },
	},
};

static const struct clockgen_muxinfo ls1028a_hwa3 = {
	{
		{ CLKSEL_VALID, PLATFORM_PLL, PLL_DIV1 },
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV1 },
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV2 },
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV3 },
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV4 },
		{},
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV2 },
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV3 },
	},
};

static const struct clockgen_muxinfo ls1028a_hwa4 = {
	{
		{ CLKSEL_VALID, PLATFORM_PLL, PLL_DIV1 },
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV1 },
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV2 },
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV3 },
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV4 },
		{},
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV2 },
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV3 },
	},
};

static const struct clockgen_muxinfo ls1043a_hwa1 = {
	{
		{},
		{},
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV2 },
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV3 },
		{},
		{},
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV2 },
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV3 },
	},
};

static const struct clockgen_muxinfo ls1043a_hwa2 = {
	{
		{},
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV1 },
		{},
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV3 },
	},
};

static const struct clockgen_muxinfo ls1046a_hwa1 = {
	{
		{},
		{},
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV2 },
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV3 },
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV4 },
		{ CLKSEL_VALID, PLATFORM_PLL, PLL_DIV1 },
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV2 },
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV3 },
	},
};

static const struct clockgen_muxinfo ls1046a_hwa2 = {
	{
		{},
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV1 },
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV2 },
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV3 },
		{},
		{},
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV2 },
	},
};

static const struct clockgen_muxinfo ls1012a_cmux = {
	{
		[0] = { CLKSEL_VALID, CGA_PLL1, PLL_DIV1 },
		{},
		[2] = { CLKSEL_VALID, CGA_PLL1, PLL_DIV2 },
	}
};

static const struct clockgen_muxinfo t1023_hwa1 = {
	{
		{},
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV1 },
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV2 },
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV3 },
	},
};

static const struct clockgen_muxinfo t1023_hwa2 = {
	{
		[6] = { CLKSEL_VALID, CGA_PLL1, PLL_DIV2 },
	},
};

static const struct clockgen_muxinfo t2080_hwa1 = {
	{
		{},
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV1 },
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV2 },
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV3 },
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV4 },
		{ CLKSEL_VALID, PLATFORM_PLL, PLL_DIV1 },
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV2 },
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV3 },
	},
};

static const struct clockgen_muxinfo t2080_hwa2 = {
	{
		{},
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV1 },
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV2 },
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV3 },
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV4 },
		{ CLKSEL_VALID, PLATFORM_PLL, PLL_DIV1 },
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV2 },
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV3 },
	},
};

static const struct clockgen_muxinfo t4240_hwa1 = {
	{
		{ CLKSEL_VALID, PLATFORM_PLL, PLL_DIV2 },
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV1 },
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV2 },
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV3 },
		{ CLKSEL_VALID, CGA_PLL1, PLL_DIV4 },
		{},
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV2 },
		{ CLKSEL_VALID, CGA_PLL2, PLL_DIV3 },
	},
};

static const struct clockgen_muxinfo t4240_hwa4 = {
	{
		[2] = { CLKSEL_VALID, CGB_PLL1, PLL_DIV2 },
		[3] = { CLKSEL_VALID, CGB_PLL1, PLL_DIV3 },
		[4] = { CLKSEL_VALID, CGB_PLL1, PLL_DIV4 },
		[5] = { CLKSEL_VALID, PLATFORM_PLL, PLL_DIV1 },
		[6] = { CLKSEL_VALID, CGB_PLL2, PLL_DIV2 },
	},
};

static const struct clockgen_muxinfo t4240_hwa5 = {
	{
		[2] = { CLKSEL_VALID, CGB_PLL2, PLL_DIV2 },
		[3] = { CLKSEL_VALID, CGB_PLL2, PLL_DIV3 },
		[4] = { CLKSEL_VALID, CGB_PLL2, PLL_DIV4 },
		[5] = { CLKSEL_VALID, PLATFORM_PLL, PLL_DIV1 },
		[6] = { CLKSEL_VALID, CGB_PLL1, PLL_DIV2 },
		[7] = { CLKSEL_VALID, CGB_PLL1, PLL_DIV3 },
	},
};

#define RCWSR7_FM1_CLK_SEL	0x40000000
#define RCWSR7_FM2_CLK_SEL	0x20000000
#define RCWSR7_HWA_ASYNC_DIV	0x04000000

static void __init p2041_init_periph(struct clockgen *cg)
{
	u32 reg;

	reg = ioread32be(&cg->guts->rcwsr[7]);

	if (reg & RCWSR7_FM1_CLK_SEL)
		cg->fman[0] = cg->pll[CGA_PLL2].div[PLL_DIV2].clk;
	else
		cg->fman[0] = cg->pll[PLATFORM_PLL].div[PLL_DIV2].clk;
}

static void __init p4080_init_periph(struct clockgen *cg)
{
	u32 reg;

	reg = ioread32be(&cg->guts->rcwsr[7]);

	if (reg & RCWSR7_FM1_CLK_SEL)
		cg->fman[0] = cg->pll[CGA_PLL3].div[PLL_DIV2].clk;
	else
		cg->fman[0] = cg->pll[PLATFORM_PLL].div[PLL_DIV2].clk;

	if (reg & RCWSR7_FM2_CLK_SEL)
		cg->fman[1] = cg->pll[CGA_PLL3].div[PLL_DIV2].clk;
	else
		cg->fman[1] = cg->pll[PLATFORM_PLL].div[PLL_DIV2].clk;
}

static void __init p5020_init_periph(struct clockgen *cg)
{
	u32 reg;
	int div = PLL_DIV2;

	reg = ioread32be(&cg->guts->rcwsr[7]);
	if (reg & RCWSR7_HWA_ASYNC_DIV)
		div = PLL_DIV4;

	if (reg & RCWSR7_FM1_CLK_SEL)
		cg->fman[0] = cg->pll[CGA_PLL2].div[div].clk;
	else
		cg->fman[0] = cg->pll[PLATFORM_PLL].div[PLL_DIV2].clk;
}

static void __init p5040_init_periph(struct clockgen *cg)
{
	u32 reg;
	int div = PLL_DIV2;

	reg = ioread32be(&cg->guts->rcwsr[7]);
	if (reg & RCWSR7_HWA_ASYNC_DIV)
		div = PLL_DIV4;

	if (reg & RCWSR7_FM1_CLK_SEL)
		cg->fman[0] = cg->pll[CGA_PLL3].div[div].clk;
	else
		cg->fman[0] = cg->pll[PLATFORM_PLL].div[PLL_DIV2].clk;

	if (reg & RCWSR7_FM2_CLK_SEL)
		cg->fman[1] = cg->pll[CGA_PLL3].div[div].clk;
	else
		cg->fman[1] = cg->pll[PLATFORM_PLL].div[PLL_DIV2].clk;
}

static void __init t1023_init_periph(struct clockgen *cg)
{
	cg->fman[0] = cg->hwaccel[1];
}

static void __init t1040_init_periph(struct clockgen *cg)
{
	cg->fman[0] = cg->pll[PLATFORM_PLL].div[PLL_DIV1].clk;
}

static void __init t2080_init_periph(struct clockgen *cg)
{
	cg->fman[0] = cg->hwaccel[0];
}

static void __init t4240_init_periph(struct clockgen *cg)
{
	cg->fman[0] = cg->hwaccel[3];
	cg->fman[1] = cg->hwaccel[4];
}

static const struct clockgen_chipinfo chipinfo[] = {
	{
		.compat = "fsl,b4420-clockgen",
		.guts_compat = "fsl,b4860-device-config",
		.init_periph = t2080_init_periph,
		.cmux_groups = {
			&clockgen2_cmux_cga12, &clockgen2_cmux_cgb
		},
		.hwaccel = {
			&t2080_hwa1
		},
		.cmux_to_group = {
			0, 1, 1, 1, -1
		},
		.pll_mask = 0x3f,
		.flags = CG_PLL_8BIT,
	},
	{
		.compat = "fsl,b4860-clockgen",
		.guts_compat = "fsl,b4860-device-config",
		.init_periph = t2080_init_periph,
		.cmux_groups = {
			&clockgen2_cmux_cga12, &clockgen2_cmux_cgb
		},
		.hwaccel = {
			&t2080_hwa1
		},
		.cmux_to_group = {
			0, 1, 1, 1, -1
		},
		.pll_mask = 0x3f,
		.flags = CG_PLL_8BIT,
	},
	{
		.compat = "fsl,ls1021a-clockgen",
		.cmux_groups = {
			&t1023_cmux
		},
		.cmux_to_group = {
			0, -1
		},
		.pll_mask = 0x03,
	},
	{
		.compat = "fsl,ls1028a-clockgen",
		.cmux_groups = {
			&clockgen2_cmux_cga12
		},
		.hwaccel = {
			&ls1028a_hwa1, &ls1028a_hwa2,
			&ls1028a_hwa3, &ls1028a_hwa4
		},
		.cmux_to_group = {
			0, 0, 0, 0, -1
		},
		.pll_mask = 0x07,
		.flags = CG_VER3 | CG_LITTLE_ENDIAN,
	},
	{
		.compat = "fsl,ls1043a-clockgen",
		.init_periph = t2080_init_periph,
		.cmux_groups = {
			&t1040_cmux
		},
		.hwaccel = {
			&ls1043a_hwa1, &ls1043a_hwa2
		},
		.cmux_to_group = {
			0, -1
		},
		.pll_mask = 0x07,
		.flags = CG_PLL_8BIT,
	},
	{
		.compat = "fsl,ls1046a-clockgen",
		.init_periph = t2080_init_periph,
		.cmux_groups = {
			&t1040_cmux
		},
		.hwaccel = {
			&ls1046a_hwa1, &ls1046a_hwa2
		},
		.cmux_to_group = {
			0, -1
		},
		.pll_mask = 0x07,
		.flags = CG_PLL_8BIT,
	},
	{
		.compat = "fsl,ls1088a-clockgen",
		.cmux_groups = {
			&clockgen2_cmux_cga12
		},
		.cmux_to_group = {
			0, 0, -1
		},
		.pll_mask = 0x07,
		.flags = CG_VER3 | CG_LITTLE_ENDIAN,
	},
	{
		.compat = "fsl,ls1012a-clockgen",
		.cmux_groups = {
			&ls1012a_cmux
		},
		.cmux_to_group = {
			0, -1
		},
		.pll_mask = 0x03,
	},
	{
		.compat = "fsl,ls2080a-clockgen",
		.cmux_groups = {
			&clockgen2_cmux_cga12, &clockgen2_cmux_cgb
		},
		.cmux_to_group = {
			0, 0, 1, 1, -1
		},
		.pll_mask = 0x37,
		.flags = CG_VER3 | CG_LITTLE_ENDIAN,
	},
	{
		.compat = "fsl,lx2160a-clockgen",
		.cmux_groups = {
			&clockgen2_cmux_cga12, &clockgen2_cmux_cgb
		},
		.cmux_to_group = {
			0, 0, 0, 0, 1, 1, 1, 1, -1
		},
		.pll_mask = 0x37,
		.flags = CG_VER3 | CG_LITTLE_ENDIAN,
	},
	{
		.compat = "fsl,p2041-clockgen",
		.guts_compat = "fsl,qoriq-device-config-1.0",
		.init_periph = p2041_init_periph,
		.cmux_groups = {
			&p2041_cmux_grp1, &p2041_cmux_grp2
		},
		.cmux_to_group = {
			0, 0, 1, 1, -1
		},
		.pll_mask = 0x07,
	},
	{
		.compat = "fsl,p3041-clockgen",
		.guts_compat = "fsl,qoriq-device-config-1.0",
		.init_periph = p2041_init_periph,
		.cmux_groups = {
			&p2041_cmux_grp1, &p2041_cmux_grp2
		},
		.cmux_to_group = {
			0, 0, 1, 1, -1
		},
		.pll_mask = 0x07,
	},
	{
		.compat = "fsl,p4080-clockgen",
		.guts_compat = "fsl,qoriq-device-config-1.0",
		.init_periph = p4080_init_periph,
		.cmux_groups = {
			&p4080_cmux_grp1, &p4080_cmux_grp2
		},
		.cmux_to_group = {
			0, 0, 0, 0, 1, 1, 1, 1, -1
		},
		.pll_mask = 0x1f,
	},
	{
		.compat = "fsl,p5020-clockgen",
		.guts_compat = "fsl,qoriq-device-config-1.0",
		.init_periph = p5020_init_periph,
		.cmux_groups = {
			&p5020_cmux_grp1, &p5020_cmux_grp2
		},
		.cmux_to_group = {
			0, 1, -1
		},
		.pll_mask = 0x07,
	},
	{
		.compat = "fsl,p5040-clockgen",
		.guts_compat = "fsl,p5040-device-config",
		.init_periph = p5040_init_periph,
		.cmux_groups = {
			&p5040_cmux_grp1, &p5040_cmux_grp2
		},
		.cmux_to_group = {
			0, 0, 1, 1, -1
		},
		.pll_mask = 0x0f,
	},
	{
		.compat = "fsl,t1023-clockgen",
		.guts_compat = "fsl,t1023-device-config",
		.init_periph = t1023_init_periph,
		.cmux_groups = {
			&t1023_cmux
		},
		.hwaccel = {
			&t1023_hwa1, &t1023_hwa2
		},
		.cmux_to_group = {
			0, 0, -1
		},
		.pll_mask = 0x03,
		.flags = CG_PLL_8BIT,
	},
	{
		.compat = "fsl,t1040-clockgen",
		.guts_compat = "fsl,t1040-device-config",
		.init_periph = t1040_init_periph,
		.cmux_groups = {
			&t1040_cmux
		},
		.cmux_to_group = {
			0, 0, 0, 0, -1
		},
		.pll_mask = 0x07,
		.flags = CG_PLL_8BIT,
	},
	{
		.compat = "fsl,t2080-clockgen",
		.guts_compat = "fsl,t2080-device-config",
		.init_periph = t2080_init_periph,
		.cmux_groups = {
			&clockgen2_cmux_cga12
		},
		.hwaccel = {
			&t2080_hwa1, &t2080_hwa2
		},
		.cmux_to_group = {
			0, -1
		},
		.pll_mask = 0x07,
		.flags = CG_PLL_8BIT,
	},
	{
		.compat = "fsl,t4240-clockgen",
		.guts_compat = "fsl,t4240-device-config",
		.init_periph = t4240_init_periph,
		.cmux_groups = {
			&clockgen2_cmux_cga, &clockgen2_cmux_cgb
		},
		.hwaccel = {
			&t4240_hwa1, NULL, NULL, &t4240_hwa4, &t4240_hwa5
		},
		.cmux_to_group = {
			0, 0, 1, -1
		},
		.pll_mask = 0x3f,
		.flags = CG_PLL_8BIT,
	},
	{},
};

struct mux_hwclock {
	struct clk_hw hw;
	struct clockgen *cg;
	const struct clockgen_muxinfo *info;
	u32 __iomem *reg;
	u8 parent_to_clksel[NUM_MUX_PARENTS];
	s8 clksel_to_parent[NUM_MUX_PARENTS];
	int num_parents;
};

#define to_mux_hwclock(p)	container_of(p, struct mux_hwclock, hw)
#define CLKSEL_MASK		0x78000000
#define	CLKSEL_SHIFT		27

static int mux_set_parent(struct clk_hw *hw, u8 idx)
{
	struct mux_hwclock *hwc = to_mux_hwclock(hw);
	u32 clksel;

	if (idx >= hwc->num_parents)
		return -EINVAL;

	clksel = hwc->parent_to_clksel[idx];
	cg_out(hwc->cg, (clksel << CLKSEL_SHIFT) & CLKSEL_MASK, hwc->reg);

	return 0;
}

static u8 mux_get_parent(struct clk_hw *hw)
{
	struct mux_hwclock *hwc = to_mux_hwclock(hw);
	u32 clksel;
	s8 ret;

	clksel = (cg_in(hwc->cg, hwc->reg) & CLKSEL_MASK) >> CLKSEL_SHIFT;

	ret = hwc->clksel_to_parent[clksel];
	if (ret < 0) {
		pr_err("%s: mux at %p has bad clksel\n", __func__, hwc->reg);
		return 0;
	}

	return ret;
}

static const struct clk_ops cmux_ops = {
	.get_parent = mux_get_parent,
	.set_parent = mux_set_parent,
};

/*
 * Don't allow setting for now, as the clock options haven't been
 * sanitized for additional restrictions.
 */
static const struct clk_ops hwaccel_ops = {
	.get_parent = mux_get_parent,
};

static const struct clockgen_pll_div *get_pll_div(struct clockgen *cg,
						  struct mux_hwclock *hwc,
						  int idx)
{
	int pll, div;

	if (!(hwc->info->clksel[idx].flags & CLKSEL_VALID))
		return NULL;

	pll = hwc->info->clksel[idx].pll;
	div = hwc->info->clksel[idx].div;

	return &cg->pll[pll].div[div];
}

static struct clk * __init create_mux_common(struct clockgen *cg,
					     struct mux_hwclock *hwc,
					     const struct clk_ops *ops,
					     unsigned long min_rate,
					     unsigned long max_rate,
					     unsigned long pct80_rate,
					     const char *fmt, int idx)
{
	struct clk_init_data init = {};
	struct clk *clk;
	const struct clockgen_pll_div *div;
	const char *parent_names[NUM_MUX_PARENTS];
	char name[32];
	int i, j;

	snprintf(name, sizeof(name), fmt, idx);

	for (i = 0, j = 0; i < NUM_MUX_PARENTS; i++) {
		unsigned long rate;

		hwc->clksel_to_parent[i] = -1;

		div = get_pll_div(cg, hwc, i);
		if (!div)
			continue;

		rate = clk_get_rate(div->clk);

		if (hwc->info->clksel[i].flags & CLKSEL_80PCT &&
		    rate > pct80_rate)
			continue;
		if (rate < min_rate)
			continue;
		if (rate > max_rate)
			continue;

		parent_names[j] = div->name;
		hwc->parent_to_clksel[j] = i;
		hwc->clksel_to_parent[i] = j;
		j++;
	}

	init.name = name;
	init.ops = ops;
	init.parent_names = parent_names;
	init.num_parents = hwc->num_parents = j;
	init.flags = 0;
	hwc->hw.init = &init;
	hwc->cg = cg;

	clk = clk_register(NULL, &hwc->hw);
	if (IS_ERR(clk)) {
		pr_err("%s: Couldn't register %s: %ld\n", __func__, name,
		       PTR_ERR(clk));
		kfree(hwc);
		return NULL;
	}

	return clk;
}

static struct clk * __init create_one_cmux(struct clockgen *cg, int idx)
{
	struct mux_hwclock *hwc;
	const struct clockgen_pll_div *div;
	unsigned long plat_rate, min_rate;
	u64 max_rate, pct80_rate;
	u32 clksel;

	hwc = kzalloc(sizeof(*hwc), GFP_KERNEL);
	if (!hwc)
		return NULL;

	if (cg->info.flags & CG_VER3)
		hwc->reg = cg->regs + 0x70000 + 0x20 * idx;
	else
		hwc->reg = cg->regs + 0x20 * idx;

	hwc->info = cg->info.cmux_groups[cg->info.cmux_to_group[idx]];

	/*
	 * Find the rate for the default clksel, and treat it as the
	 * maximum rated core frequency.  If this is an incorrect
	 * assumption, certain clock options (possibly including the
	 * default clksel) may be inappropriately excluded on certain
	 * chips.
	 */
	clksel = (cg_in(cg, hwc->reg) & CLKSEL_MASK) >> CLKSEL_SHIFT;
	div = get_pll_div(cg, hwc, clksel);
	if (!div) {
		kfree(hwc);
		return NULL;
	}

	max_rate = clk_get_rate(div->clk);
	pct80_rate = max_rate * 8;
	do_div(pct80_rate, 10);

	plat_rate = clk_get_rate(cg->pll[PLATFORM_PLL].div[PLL_DIV1].clk);

	if (cg->info.flags & CG_CMUX_GE_PLAT)
		min_rate = plat_rate;
	else
		min_rate = plat_rate / 2;

	return create_mux_common(cg, hwc, &cmux_ops, min_rate, max_rate,
				 pct80_rate, "cg-cmux%d", idx);
}

static struct clk * __init create_one_hwaccel(struct clockgen *cg, int idx)
{
	struct mux_hwclock *hwc;

	hwc = kzalloc(sizeof(*hwc), GFP_KERNEL);
	if (!hwc)
		return NULL;

	hwc->reg = cg->regs + 0x20 * idx + 0x10;
	hwc->info = cg->info.hwaccel[idx];

	return create_mux_common(cg, hwc, &hwaccel_ops, 0, ULONG_MAX, 0,
				 "cg-hwaccel%d", idx);
}

static void __init create_muxes(struct clockgen *cg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cg->cmux); i++) {
		if (cg->info.cmux_to_group[i] < 0)
			break;
		if (cg->info.cmux_to_group[i] >=
		    ARRAY_SIZE(cg->info.cmux_groups)) {
			WARN_ON_ONCE(1);
			continue;
		}

		cg->cmux[i] = create_one_cmux(cg, i);
	}

	for (i = 0; i < ARRAY_SIZE(cg->hwaccel); i++) {
		if (!cg->info.hwaccel[i])
			continue;

		cg->hwaccel[i] = create_one_hwaccel(cg, i);
	}
}

static void __init clockgen_init(struct device_node *np);

/*
 * Legacy nodes may get probed before the parent clockgen node.
 * It is assumed that device trees with legacy nodes will not
 * contain a "clocks" property -- otherwise the input clocks may
 * not be initialized at this point.
 */
static void __init legacy_init_clockgen(struct device_node *np)
{
	if (!clockgen.node)
		clockgen_init(of_get_parent(np));
}

/* Legacy node */
static void __init core_mux_init(struct device_node *np)
{
	struct clk *clk;
	struct resource res;
	int idx, rc;

	legacy_init_clockgen(np);

	if (of_address_to_resource(np, 0, &res))
		return;

	idx = (res.start & 0xf0) >> 5;
	clk = clockgen.cmux[idx];

	rc = of_clk_add_provider(np, of_clk_src_simple_get, clk);
	if (rc) {
		pr_err("%s: Couldn't register clk provider for node %pOFn: %d\n",
		       __func__, np, rc);
		return;
	}
}

static struct clk __init
*sysclk_from_fixed(struct device_node *node, const char *name)
{
	u32 rate;

	if (of_property_read_u32(node, "clock-frequency", &rate))
		return ERR_PTR(-ENODEV);

	return clk_register_fixed_rate(NULL, name, NULL, 0, rate);
}

static struct clk __init *input_clock(const char *name, struct clk *clk)
{
	const char *input_name;

	/* Register the input clock under the desired name. */
	input_name = __clk_get_name(clk);
	clk = clk_register_fixed_factor(NULL, name, input_name,
					0, 1, 1);
	if (IS_ERR(clk))
		pr_err("%s: Couldn't register %s: %ld\n", __func__, name,
		       PTR_ERR(clk));

	return clk;
}

static struct clk __init *input_clock_by_name(const char *name,
					      const char *dtname)
{
	struct clk *clk;

	clk = of_clk_get_by_name(clockgen.node, dtname);
	if (IS_ERR(clk))
		return clk;

	return input_clock(name, clk);
}

static struct clk __init *input_clock_by_index(const char *name, int idx)
{
	struct clk *clk;

	clk = of_clk_get(clockgen.node, 0);
	if (IS_ERR(clk))
		return clk;

	return input_clock(name, clk);
}

static struct clk * __init create_sysclk(const char *name)
{
	struct device_node *sysclk;
	struct clk *clk;

	clk = sysclk_from_fixed(clockgen.node, name);
	if (!IS_ERR(clk))
		return clk;

	clk = input_clock_by_name(name, "sysclk");
	if (!IS_ERR(clk))
		return clk;

	clk = input_clock_by_index(name, 0);
	if (!IS_ERR(clk))
		return clk;

	sysclk = of_get_child_by_name(clockgen.node, "sysclk");
	if (sysclk) {
		clk = sysclk_from_fixed(sysclk, name);
		if (!IS_ERR(clk))
			return clk;
	}

	pr_err("%s: No input sysclk\n", __func__);
	return NULL;
}

static struct clk * __init create_coreclk(const char *name)
{
	struct clk *clk;

	clk = input_clock_by_name(name, "coreclk");
	if (!IS_ERR(clk))
		return clk;

	/*
	 * This indicates a mix of legacy nodes with the new coreclk
	 * mechanism, which should never happen.  If this error occurs,
	 * don't use the wrong input clock just because coreclk isn't
	 * ready yet.
	 */
	if (WARN_ON(PTR_ERR(clk) == -EPROBE_DEFER))
		return clk;

	return NULL;
}

/* Legacy node */
static void __init sysclk_init(struct device_node *node)
{
	struct clk *clk;

	legacy_init_clockgen(node);

	clk = clockgen.sysclk;
	if (clk)
		of_clk_add_provider(node, of_clk_src_simple_get, clk);
}

#define PLL_KILL BIT(31)

static void __init create_one_pll(struct clockgen *cg, int idx)
{
	u32 __iomem *reg;
	u32 mult;
	struct clockgen_pll *pll = &cg->pll[idx];
	const char *input = "cg-sysclk";
	int i;

	if (!(cg->info.pll_mask & (1 << idx)))
		return;

	if (cg->coreclk && idx != PLATFORM_PLL) {
		if (IS_ERR(cg->coreclk))
			return;

		input = "cg-coreclk";
	}

	if (cg->info.flags & CG_VER3) {
		switch (idx) {
		case PLATFORM_PLL:
			reg = cg->regs + 0x60080;
			break;
		case CGA_PLL1:
			reg = cg->regs + 0x80;
			break;
		case CGA_PLL2:
			reg = cg->regs + 0xa0;
			break;
		case CGB_PLL1:
			reg = cg->regs + 0x10080;
			break;
		case CGB_PLL2:
			reg = cg->regs + 0x100a0;
			break;
		default:
			WARN_ONCE(1, "index %d\n", idx);
			return;
		}
	} else {
		if (idx == PLATFORM_PLL)
			reg = cg->regs + 0xc00;
		else
			reg = cg->regs + 0x800 + 0x20 * (idx - 1);
	}

	/* Get the multiple of PLL */
	mult = cg_in(cg, reg);

	/* Check if this PLL is disabled */
	if (mult & PLL_KILL) {
		pr_debug("%s(): pll %p disabled\n", __func__, reg);
		return;
	}

	if ((cg->info.flags & CG_VER3) ||
	    ((cg->info.flags & CG_PLL_8BIT) && idx != PLATFORM_PLL))
		mult = (mult & GENMASK(8, 1)) >> 1;
	else
		mult = (mult & GENMASK(6, 1)) >> 1;

	for (i = 0; i < ARRAY_SIZE(pll->div); i++) {
		struct clk *clk;
		int ret;

		/*
		 * For platform PLL, there are MAX_PLL_DIV divider clocks.
		 * For core PLL, there are 4 divider clocks at most.
		 */
		if (idx != PLATFORM_PLL && i >= 4)
			break;

		snprintf(pll->div[i].name, sizeof(pll->div[i].name),
			 "cg-pll%d-div%d", idx, i + 1);

		clk = clk_register_fixed_factor(NULL,
				pll->div[i].name, input, 0, mult, i + 1);
		if (IS_ERR(clk)) {
			pr_err("%s: %s: register failed %ld\n",
			       __func__, pll->div[i].name, PTR_ERR(clk));
			continue;
		}

		pll->div[i].clk = clk;
		ret = clk_register_clkdev(clk, pll->div[i].name, NULL);
		if (ret != 0)
			pr_err("%s: %s: register to lookup table failed %d\n",
			       __func__, pll->div[i].name, ret);

	}
}

static void __init create_plls(struct clockgen *cg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cg->pll); i++)
		create_one_pll(cg, i);
}

static void __init legacy_pll_init(struct device_node *np, int idx)
{
	struct clockgen_pll *pll;
	struct clk_onecell_data *onecell_data;
	struct clk **subclks;
	int count, rc;

	legacy_init_clockgen(np);

	pll = &clockgen.pll[idx];
	count = of_property_count_strings(np, "clock-output-names");

	BUILD_BUG_ON(ARRAY_SIZE(pll->div) < 4);
	subclks = kcalloc(4, sizeof(struct clk *), GFP_KERNEL);
	if (!subclks)
		return;

	onecell_data = kmalloc(sizeof(*onecell_data), GFP_KERNEL);
	if (!onecell_data)
		goto err_clks;

	if (count <= 3) {
		subclks[0] = pll->div[0].clk;
		subclks[1] = pll->div[1].clk;
		subclks[2] = pll->div[3].clk;
	} else {
		subclks[0] = pll->div[0].clk;
		subclks[1] = pll->div[1].clk;
		subclks[2] = pll->div[2].clk;
		subclks[3] = pll->div[3].clk;
	}

	onecell_data->clks = subclks;
	onecell_data->clk_num = count;

	rc = of_clk_add_provider(np, of_clk_src_onecell_get, onecell_data);
	if (rc) {
		pr_err("%s: Couldn't register clk provider for node %pOFn: %d\n",
		       __func__, np, rc);
		goto err_cell;
	}

	return;
err_cell:
	kfree(onecell_data);
err_clks:
	kfree(subclks);
}

/* Legacy node */
static void __init pltfrm_pll_init(struct device_node *np)
{
	legacy_pll_init(np, PLATFORM_PLL);
}

/* Legacy node */
static void __init core_pll_init(struct device_node *np)
{
	struct resource res;
	int idx;

	if (of_address_to_resource(np, 0, &res))
		return;

	if ((res.start & 0xfff) == 0xc00) {
		/*
		 * ls1021a devtree labels the platform PLL
		 * with the core PLL compatible
		 */
		pltfrm_pll_init(np);
	} else {
		idx = (res.start & 0xf0) >> 5;
		legacy_pll_init(np, CGA_PLL1 + idx);
	}
}

static struct clk *clockgen_clk_get(struct of_phandle_args *clkspec, void *data)
{
	struct clockgen *cg = data;
	struct clk *clk;
	struct clockgen_pll *pll;
	u32 type, idx;

	if (clkspec->args_count < 2) {
		pr_err("%s: insufficient phandle args\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	type = clkspec->args[0];
	idx = clkspec->args[1];

	switch (type) {
	case 0:
		if (idx != 0)
			goto bad_args;
		clk = cg->sysclk;
		break;
	case 1:
		if (idx >= ARRAY_SIZE(cg->cmux))
			goto bad_args;
		clk = cg->cmux[idx];
		break;
	case 2:
		if (idx >= ARRAY_SIZE(cg->hwaccel))
			goto bad_args;
		clk = cg->hwaccel[idx];
		break;
	case 3:
		if (idx >= ARRAY_SIZE(cg->fman))
			goto bad_args;
		clk = cg->fman[idx];
		break;
	case 4:
		pll = &cg->pll[PLATFORM_PLL];
		if (idx >= ARRAY_SIZE(pll->div))
			goto bad_args;
		clk = pll->div[idx].clk;
		break;
	case 5:
		if (idx != 0)
			goto bad_args;
		clk = cg->coreclk;
		if (IS_ERR(clk))
			clk = NULL;
		break;
	default:
		goto bad_args;
	}

	if (!clk)
		return ERR_PTR(-ENOENT);
	return clk;

bad_args:
	pr_err("%s: Bad phandle args %u %u\n", __func__, type, idx);
	return ERR_PTR(-EINVAL);
}

#ifdef CONFIG_PPC
#include <asm/mpc85xx.h>

static const u32 a4510_svrs[] __initconst = {
	(SVR_P2040 << 8) | 0x10,	/* P2040 1.0 */
	(SVR_P2040 << 8) | 0x11,	/* P2040 1.1 */
	(SVR_P2041 << 8) | 0x10,	/* P2041 1.0 */
	(SVR_P2041 << 8) | 0x11,	/* P2041 1.1 */
	(SVR_P3041 << 8) | 0x10,	/* P3041 1.0 */
	(SVR_P3041 << 8) | 0x11,	/* P3041 1.1 */
	(SVR_P4040 << 8) | 0x20,	/* P4040 2.0 */
	(SVR_P4080 << 8) | 0x20,	/* P4080 2.0 */
	(SVR_P5010 << 8) | 0x10,	/* P5010 1.0 */
	(SVR_P5010 << 8) | 0x20,	/* P5010 2.0 */
	(SVR_P5020 << 8) | 0x10,	/* P5020 1.0 */
	(SVR_P5021 << 8) | 0x10,	/* P5021 1.0 */
	(SVR_P5040 << 8) | 0x10,	/* P5040 1.0 */
};

#define SVR_SECURITY	0x80000	/* The Security (E) bit */

static bool __init has_erratum_a4510(void)
{
	u32 svr = mfspr(SPRN_SVR);
	int i;

	svr &= ~SVR_SECURITY;

	for (i = 0; i < ARRAY_SIZE(a4510_svrs); i++) {
		if (svr == a4510_svrs[i])
			return true;
	}

	return false;
}
#else
static bool __init has_erratum_a4510(void)
{
	return false;
}
#endif

static void __init clockgen_init(struct device_node *np)
{
	int i, ret;
	bool is_old_ls1021a = false;

	/* May have already been called by a legacy probe */
	if (clockgen.node)
		return;

	clockgen.node = np;
	clockgen.regs = of_iomap(np, 0);
	if (!clockgen.regs &&
	    of_device_is_compatible(of_root, "fsl,ls1021a")) {
		/* Compatibility hack for old, broken device trees */
		clockgen.regs = ioremap(0x1ee1000, 0x1000);
		is_old_ls1021a = true;
	}
	if (!clockgen.regs) {
		pr_err("%s(): %pOFn: of_iomap() failed\n", __func__, np);
		return;
	}

	for (i = 0; i < ARRAY_SIZE(chipinfo); i++) {
		if (of_device_is_compatible(np, chipinfo[i].compat))
			break;
		if (is_old_ls1021a &&
		    !strcmp(chipinfo[i].compat, "fsl,ls1021a-clockgen"))
			break;
	}

	if (i == ARRAY_SIZE(chipinfo)) {
		pr_err("%s: unknown clockgen node %pOF\n", __func__, np);
		goto err;
	}
	clockgen.info = chipinfo[i];

	if (clockgen.info.guts_compat) {
		struct device_node *guts;

		guts = of_find_compatible_node(NULL, NULL,
					       clockgen.info.guts_compat);
		if (guts) {
			clockgen.guts = of_iomap(guts, 0);
			if (!clockgen.guts) {
				pr_err("%s: Couldn't map %pOF regs\n", __func__,
				       guts);
			}
			of_node_put(guts);
		}

	}

	if (has_erratum_a4510())
		clockgen.info.flags |= CG_CMUX_GE_PLAT;

	clockgen.sysclk = create_sysclk("cg-sysclk");
	clockgen.coreclk = create_coreclk("cg-coreclk");
	create_plls(&clockgen);
	create_muxes(&clockgen);

	if (clockgen.info.init_periph)
		clockgen.info.init_periph(&clockgen);

	ret = of_clk_add_provider(np, clockgen_clk_get, &clockgen);
	if (ret) {
		pr_err("%s: Couldn't register clk provider for node %pOFn: %d\n",
		       __func__, np, ret);
	}

	return;
err:
	iounmap(clockgen.regs);
	clockgen.regs = NULL;
}

CLK_OF_DECLARE(qoriq_clockgen_1, "fsl,qoriq-clockgen-1.0", clockgen_init);
CLK_OF_DECLARE(qoriq_clockgen_2, "fsl,qoriq-clockgen-2.0", clockgen_init);
CLK_OF_DECLARE(qoriq_clockgen_b4420, "fsl,b4420-clockgen", clockgen_init);
CLK_OF_DECLARE(qoriq_clockgen_b4860, "fsl,b4860-clockgen", clockgen_init);
CLK_OF_DECLARE(qoriq_clockgen_ls1012a, "fsl,ls1012a-clockgen", clockgen_init);
CLK_OF_DECLARE(qoriq_clockgen_ls1021a, "fsl,ls1021a-clockgen", clockgen_init);
CLK_OF_DECLARE(qoriq_clockgen_ls1028a, "fsl,ls1028a-clockgen", clockgen_init);
CLK_OF_DECLARE(qoriq_clockgen_ls1043a, "fsl,ls1043a-clockgen", clockgen_init);
CLK_OF_DECLARE(qoriq_clockgen_ls1046a, "fsl,ls1046a-clockgen", clockgen_init);
CLK_OF_DECLARE(qoriq_clockgen_ls1088a, "fsl,ls1088a-clockgen", clockgen_init);
CLK_OF_DECLARE(qoriq_clockgen_ls2080a, "fsl,ls2080a-clockgen", clockgen_init);
CLK_OF_DECLARE(qoriq_clockgen_lx2160a, "fsl,lx2160a-clockgen", clockgen_init);
CLK_OF_DECLARE(qoriq_clockgen_p2041, "fsl,p2041-clockgen", clockgen_init);
CLK_OF_DECLARE(qoriq_clockgen_p3041, "fsl,p3041-clockgen", clockgen_init);
CLK_OF_DECLARE(qoriq_clockgen_p4080, "fsl,p4080-clockgen", clockgen_init);
CLK_OF_DECLARE(qoriq_clockgen_p5020, "fsl,p5020-clockgen", clockgen_init);
CLK_OF_DECLARE(qoriq_clockgen_p5040, "fsl,p5040-clockgen", clockgen_init);
CLK_OF_DECLARE(qoriq_clockgen_t1023, "fsl,t1023-clockgen", clockgen_init);
CLK_OF_DECLARE(qoriq_clockgen_t1040, "fsl,t1040-clockgen", clockgen_init);
CLK_OF_DECLARE(qoriq_clockgen_t2080, "fsl,t2080-clockgen", clockgen_init);
CLK_OF_DECLARE(qoriq_clockgen_t4240, "fsl,t4240-clockgen", clockgen_init);

/* Legacy nodes */
CLK_OF_DECLARE(qoriq_sysclk_1, "fsl,qoriq-sysclk-1.0", sysclk_init);
CLK_OF_DECLARE(qoriq_sysclk_2, "fsl,qoriq-sysclk-2.0", sysclk_init);
CLK_OF_DECLARE(qoriq_core_pll_1, "fsl,qoriq-core-pll-1.0", core_pll_init);
CLK_OF_DECLARE(qoriq_core_pll_2, "fsl,qoriq-core-pll-2.0", core_pll_init);
CLK_OF_DECLARE(qoriq_core_mux_1, "fsl,qoriq-core-mux-1.0", core_mux_init);
CLK_OF_DECLARE(qoriq_core_mux_2, "fsl,qoriq-core-mux-2.0", core_mux_init);
CLK_OF_DECLARE(qoriq_pltfrm_pll_1, "fsl,qoriq-platform-pll-1.0", pltfrm_pll_init);
CLK_OF_DECLARE(qoriq_pltfrm_pll_2, "fsl,qoriq-platform-pll-2.0", pltfrm_pll_init);
