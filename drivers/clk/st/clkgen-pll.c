/*
 * Copyright (C) 2014 STMicroelectronics (R&D) Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

/*
 * Authors:
 * Stephen Gallimore <stephen.gallimore@st.com>,
 * Pankaj Dev <pankaj.dev@st.com>.
 */

#include <linux/slab.h>
#include <linux/of_address.h>
#include <linux/clk-provider.h>

#include "clkgen.h"

static DEFINE_SPINLOCK(clkgena_c32_odf_lock);

/*
 * Common PLL configuration register bits for PLL800 and PLL1600 C65
 */
#define C65_MDIV_PLL800_MASK	(0xff)
#define C65_MDIV_PLL1600_MASK	(0x7)
#define C65_NDIV_MASK		(0xff)
#define C65_PDIV_MASK		(0x7)

/*
 * PLL configuration register bits for PLL3200 C32
 */
#define C32_NDIV_MASK (0xff)
#define C32_IDF_MASK (0x7)
#define C32_ODF_MASK (0x3f)
#define C32_LDF_MASK (0x7f)

#define C32_MAX_ODFS (4)

struct clkgen_pll_data {
	struct clkgen_field pdn_status;
	struct clkgen_field locked_status;
	struct clkgen_field mdiv;
	struct clkgen_field ndiv;
	struct clkgen_field pdiv;
	struct clkgen_field idf;
	struct clkgen_field ldf;
	unsigned int num_odfs;
	struct clkgen_field odf[C32_MAX_ODFS];
	struct clkgen_field odf_gate[C32_MAX_ODFS];
	const struct clk_ops *ops;
};

static const struct clk_ops st_pll1600c65_ops;
static const struct clk_ops st_pll800c65_ops;
static const struct clk_ops stm_pll3200c32_ops;
static const struct clk_ops st_pll1200c32_ops;

static const struct clkgen_pll_data st_pll1600c65_ax = {
	.pdn_status	= CLKGEN_FIELD(0x0, 0x1,			19),
	.locked_status	= CLKGEN_FIELD(0x0, 0x1,			31),
	.mdiv		= CLKGEN_FIELD(0x0, C65_MDIV_PLL1600_MASK,	0),
	.ndiv		= CLKGEN_FIELD(0x0, C65_NDIV_MASK,		8),
	.ops		= &st_pll1600c65_ops
};

static const struct clkgen_pll_data st_pll800c65_ax = {
	.pdn_status	= CLKGEN_FIELD(0x0,	0x1,			19),
	.locked_status	= CLKGEN_FIELD(0x0,	0x1,			31),
	.mdiv		= CLKGEN_FIELD(0x0,	C65_MDIV_PLL800_MASK,	0),
	.ndiv		= CLKGEN_FIELD(0x0,	C65_NDIV_MASK,		8),
	.pdiv		= CLKGEN_FIELD(0x0,	C65_PDIV_MASK,		16),
	.ops		= &st_pll800c65_ops
};

static const struct clkgen_pll_data st_pll3200c32_a1x_0 = {
	.pdn_status	= CLKGEN_FIELD(0x0,	0x1,			31),
	.locked_status	= CLKGEN_FIELD(0x4,	0x1,			31),
	.ndiv		= CLKGEN_FIELD(0x0,	C32_NDIV_MASK,		0x0),
	.idf		= CLKGEN_FIELD(0x4,	C32_IDF_MASK,		0x0),
	.num_odfs = 4,
	.odf =	{	CLKGEN_FIELD(0x54,	C32_ODF_MASK,		4),
			CLKGEN_FIELD(0x54,	C32_ODF_MASK,		10),
			CLKGEN_FIELD(0x54,	C32_ODF_MASK,		16),
			CLKGEN_FIELD(0x54,	C32_ODF_MASK,		22) },
	.odf_gate = {	CLKGEN_FIELD(0x54,	0x1,			0),
			CLKGEN_FIELD(0x54,	0x1,			1),
			CLKGEN_FIELD(0x54,	0x1,			2),
			CLKGEN_FIELD(0x54,	0x1,			3) },
	.ops		= &stm_pll3200c32_ops,
};

static const struct clkgen_pll_data st_pll3200c32_a1x_1 = {
	.pdn_status	= CLKGEN_FIELD(0xC,	0x1,			31),
	.locked_status	= CLKGEN_FIELD(0x10,	0x1,			31),
	.ndiv		= CLKGEN_FIELD(0xC,	C32_NDIV_MASK,		0x0),
	.idf		= CLKGEN_FIELD(0x10,	C32_IDF_MASK,		0x0),
	.num_odfs = 4,
	.odf = {	CLKGEN_FIELD(0x58,	C32_ODF_MASK,		4),
			CLKGEN_FIELD(0x58,	C32_ODF_MASK,		10),
			CLKGEN_FIELD(0x58,	C32_ODF_MASK,		16),
			CLKGEN_FIELD(0x58,	C32_ODF_MASK,		22) },
	.odf_gate = {	CLKGEN_FIELD(0x58,	0x1,			0),
			CLKGEN_FIELD(0x58,	0x1,			1),
			CLKGEN_FIELD(0x58,	0x1,			2),
			CLKGEN_FIELD(0x58,	0x1,			3) },
	.ops		= &stm_pll3200c32_ops,
};

/* 415 specific */
static const struct clkgen_pll_data st_pll3200c32_a9_415 = {
	.pdn_status	= CLKGEN_FIELD(0x0,	0x1,			0),
	.locked_status	= CLKGEN_FIELD(0x6C,	0x1,			0),
	.ndiv		= CLKGEN_FIELD(0x0,	C32_NDIV_MASK,		9),
	.idf		= CLKGEN_FIELD(0x0,	C32_IDF_MASK,		22),
	.num_odfs = 1,
	.odf =		{ CLKGEN_FIELD(0x0,	C32_ODF_MASK,		3) },
	.odf_gate =	{ CLKGEN_FIELD(0x0,	0x1,			28) },
	.ops		= &stm_pll3200c32_ops,
};

static const struct clkgen_pll_data st_pll3200c32_ddr_415 = {
	.pdn_status	= CLKGEN_FIELD(0x0,	0x1,			0),
	.locked_status	= CLKGEN_FIELD(0x100,	0x1,			0),
	.ndiv		= CLKGEN_FIELD(0x8,	C32_NDIV_MASK,		0),
	.idf		= CLKGEN_FIELD(0x0,	C32_IDF_MASK,		25),
	.num_odfs = 2,
	.odf		= { CLKGEN_FIELD(0x8,	C32_ODF_MASK,		8),
			    CLKGEN_FIELD(0x8,	C32_ODF_MASK,		14) },
	.odf_gate	= { CLKGEN_FIELD(0x4,	0x1,			28),
			    CLKGEN_FIELD(0x4,	0x1,			29) },
	.ops		= &stm_pll3200c32_ops,
};

static const struct clkgen_pll_data st_pll1200c32_gpu_415 = {
	.pdn_status	= CLKGEN_FIELD(0x144,	0x1,			3),
	.locked_status	= CLKGEN_FIELD(0x168,	0x1,			0),
	.ldf		= CLKGEN_FIELD(0x0,	C32_LDF_MASK,		3),
	.idf		= CLKGEN_FIELD(0x0,	C32_IDF_MASK,		0),
	.num_odfs = 0,
	.odf		= { CLKGEN_FIELD(0x0,	C32_ODF_MASK,		10) },
	.ops		= &st_pll1200c32_ops,
};

/* 416 specific */
static const struct clkgen_pll_data st_pll3200c32_a9_416 = {
	.pdn_status	= CLKGEN_FIELD(0x0,	0x1,			0),
	.locked_status	= CLKGEN_FIELD(0x6C,	0x1,			0),
	.ndiv		= CLKGEN_FIELD(0x8,	C32_NDIV_MASK,		0),
	.idf		= CLKGEN_FIELD(0x0,	C32_IDF_MASK,		25),
	.num_odfs = 1,
	.odf		= { CLKGEN_FIELD(0x8,	C32_ODF_MASK,		8) },
	.odf_gate	= { CLKGEN_FIELD(0x4,	0x1,			28) },
	.ops		= &stm_pll3200c32_ops,
};

static const struct clkgen_pll_data st_pll3200c32_ddr_416 = {
	.pdn_status	= CLKGEN_FIELD(0x0,	0x1,			0),
	.locked_status	= CLKGEN_FIELD(0x10C,	0x1,			0),
	.ndiv		= CLKGEN_FIELD(0x8,	C32_NDIV_MASK,		0),
	.idf		= CLKGEN_FIELD(0x0,	C32_IDF_MASK,		25),
	.num_odfs = 2,
	.odf		= { CLKGEN_FIELD(0x8,	C32_ODF_MASK,		8),
			    CLKGEN_FIELD(0x8,	C32_ODF_MASK,		14) },
	.odf_gate	= { CLKGEN_FIELD(0x4,	0x1,			28),
			    CLKGEN_FIELD(0x4,	0x1,			29) },
	.ops		= &stm_pll3200c32_ops,
};

static const struct clkgen_pll_data st_pll1200c32_gpu_416 = {
	.pdn_status	= CLKGEN_FIELD(0x8E4,	0x1,			3),
	.locked_status	= CLKGEN_FIELD(0x90C,	0x1,			0),
	.ldf		= CLKGEN_FIELD(0x0,	C32_LDF_MASK,		3),
	.idf		= CLKGEN_FIELD(0x0,	C32_IDF_MASK,		0),
	.num_odfs = 0,
	.odf		= { CLKGEN_FIELD(0x0,	C32_ODF_MASK,		10) },
	.ops		= &st_pll1200c32_ops,
};

static const struct clkgen_pll_data st_pll3200c32_407_a0 = {
	/* 407 A0 */
	.pdn_status	= CLKGEN_FIELD(0x2a0,	0x1,			8),
	.locked_status	= CLKGEN_FIELD(0x2a0,	0x1,			24),
	.ndiv		= CLKGEN_FIELD(0x2a4,	C32_NDIV_MASK,		16),
	.idf		= CLKGEN_FIELD(0x2a4,	C32_IDF_MASK,		0x0),
	.num_odfs = 1,
	.odf		= { CLKGEN_FIELD(0x2b4, C32_ODF_MASK,		0) },
	.odf_gate	= { CLKGEN_FIELD(0x2b4,	0x1,			6) },
	.ops		= &stm_pll3200c32_ops,
};

static const struct clkgen_pll_data st_pll3200c32_407_c0_0 = {
	/* 407 C0 PLL0 */
	.pdn_status	= CLKGEN_FIELD(0x2a0,	0x1,			8),
	.locked_status	= CLKGEN_FIELD(0x2a0,	0x1,			24),
	.ndiv		= CLKGEN_FIELD(0x2a4,	C32_NDIV_MASK,		16),
	.idf		= CLKGEN_FIELD(0x2a4,	C32_IDF_MASK,		0x0),
	.num_odfs = 1,
	.odf		= { CLKGEN_FIELD(0x2b4, C32_ODF_MASK,		0) },
	.odf_gate	= { CLKGEN_FIELD(0x2b4, 0x1,			6) },
	.ops		= &stm_pll3200c32_ops,
};

static const struct clkgen_pll_data st_pll3200c32_407_c0_1 = {
	/* 407 C0 PLL1 */
	.pdn_status	= CLKGEN_FIELD(0x2c8,	0x1,			8),
	.locked_status	= CLKGEN_FIELD(0x2c8,	0x1,			24),
	.ndiv		= CLKGEN_FIELD(0x2cc,	C32_NDIV_MASK,		16),
	.idf		= CLKGEN_FIELD(0x2cc,	C32_IDF_MASK,		0x0),
	.num_odfs = 1,
	.odf		= { CLKGEN_FIELD(0x2dc, C32_ODF_MASK,		0) },
	.odf_gate	= { CLKGEN_FIELD(0x2dc, 0x1,			6) },
	.ops		= &stm_pll3200c32_ops,
};

static const struct clkgen_pll_data st_pll3200c32_407_a9 = {
	/* 407 A9 */
	.pdn_status	= CLKGEN_FIELD(0x1a8,	0x1,			0),
	.locked_status	= CLKGEN_FIELD(0x87c,	0x1,			0),
	.ndiv		= CLKGEN_FIELD(0x1b0,	C32_NDIV_MASK,		0),
	.idf		= CLKGEN_FIELD(0x1a8,	C32_IDF_MASK,		25),
	.num_odfs = 1,
	.odf		= { CLKGEN_FIELD(0x1b0, C32_ODF_MASK,		8) },
	.odf_gate	= { CLKGEN_FIELD(0x1ac, 0x1,			28) },
	.ops		= &stm_pll3200c32_ops,
};

/**
 * DOC: Clock Generated by PLL, rate set and enabled by bootloader
 *
 * Traits of this clock:
 * prepare - clk_(un)prepare only ensures parent is (un)prepared
 * enable - clk_enable/disable only ensures parent is enabled
 * rate - rate is fixed. No clk_set_rate support
 * parent - fixed parent.  No clk_set_parent support
 */

/**
 * PLL clock that is integrated in the ClockGenA instances on the STiH415
 * and STiH416.
 *
 * @hw: handle between common and hardware-specific interfaces.
 * @type: PLL instance type.
 * @regs_base: base of the PLL configuration register(s).
 *
 */
struct clkgen_pll {
	struct clk_hw		hw;
	struct clkgen_pll_data	*data;
	void __iomem		*regs_base;
};

#define to_clkgen_pll(_hw) container_of(_hw, struct clkgen_pll, hw)

static int clkgen_pll_is_locked(struct clk_hw *hw)
{
	struct clkgen_pll *pll = to_clkgen_pll(hw);
	u32 locked = CLKGEN_READ(pll, locked_status);

	return !!locked;
}

static int clkgen_pll_is_enabled(struct clk_hw *hw)
{
	struct clkgen_pll *pll = to_clkgen_pll(hw);
	u32 poweroff = CLKGEN_READ(pll, pdn_status);
	return !poweroff;
}

static unsigned long recalc_stm_pll800c65(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clkgen_pll *pll = to_clkgen_pll(hw);
	unsigned long mdiv, ndiv, pdiv;
	unsigned long rate;
	uint64_t res;

	if (!clkgen_pll_is_enabled(hw) || !clkgen_pll_is_locked(hw))
		return 0;

	pdiv = CLKGEN_READ(pll, pdiv);
	mdiv = CLKGEN_READ(pll, mdiv);
	ndiv = CLKGEN_READ(pll, ndiv);

	if (!mdiv)
		mdiv++; /* mdiv=0 or 1 => MDIV=1 */

	res = (uint64_t)2 * (uint64_t)parent_rate * (uint64_t)ndiv;
	rate = (unsigned long)div64_u64(res, mdiv * (1 << pdiv));

	pr_debug("%s:%s rate %lu\n", __clk_get_name(hw->clk), __func__, rate);

	return rate;

}

static unsigned long recalc_stm_pll1600c65(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clkgen_pll *pll = to_clkgen_pll(hw);
	unsigned long mdiv, ndiv;
	unsigned long rate;

	if (!clkgen_pll_is_enabled(hw) || !clkgen_pll_is_locked(hw))
		return 0;

	mdiv = CLKGEN_READ(pll, mdiv);
	ndiv = CLKGEN_READ(pll, ndiv);

	if (!mdiv)
		mdiv = 1;

	/* Note: input is divided by 1000 to avoid overflow */
	rate = ((2 * (parent_rate / 1000) * ndiv) / mdiv) * 1000;

	pr_debug("%s:%s rate %lu\n", __clk_get_name(hw->clk), __func__, rate);

	return rate;
}

static unsigned long recalc_stm_pll3200c32(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clkgen_pll *pll = to_clkgen_pll(hw);
	unsigned long ndiv, idf;
	unsigned long rate = 0;

	if (!clkgen_pll_is_enabled(hw) || !clkgen_pll_is_locked(hw))
		return 0;

	ndiv = CLKGEN_READ(pll, ndiv);
	idf = CLKGEN_READ(pll, idf);

	if (idf)
		/* Note: input is divided to avoid overflow */
		rate = ((2 * (parent_rate/1000) * ndiv) / idf) * 1000;

	pr_debug("%s:%s rate %lu\n", __clk_get_name(hw->clk), __func__, rate);

	return rate;
}

static unsigned long recalc_stm_pll1200c32(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clkgen_pll *pll = to_clkgen_pll(hw);
	unsigned long odf, ldf, idf;
	unsigned long rate;

	if (!clkgen_pll_is_enabled(hw) || !clkgen_pll_is_locked(hw))
		return 0;

	odf = CLKGEN_READ(pll, odf[0]);
	ldf = CLKGEN_READ(pll, ldf);
	idf = CLKGEN_READ(pll, idf);

	if (!idf) /* idf==0 means 1 */
		idf = 1;
	if (!odf) /* odf==0 means 1 */
		odf = 1;

	/* Note: input is divided by 1000 to avoid overflow */
	rate = (((parent_rate / 1000) * ldf) / (odf * idf)) * 1000;

	pr_debug("%s:%s rate %lu\n", __clk_get_name(hw->clk), __func__, rate);

	return rate;
}

static const struct clk_ops st_pll1600c65_ops = {
	.is_enabled	= clkgen_pll_is_enabled,
	.recalc_rate	= recalc_stm_pll1600c65,
};

static const struct clk_ops st_pll800c65_ops = {
	.is_enabled	= clkgen_pll_is_enabled,
	.recalc_rate	= recalc_stm_pll800c65,
};

static const struct clk_ops stm_pll3200c32_ops = {
	.is_enabled	= clkgen_pll_is_enabled,
	.recalc_rate	= recalc_stm_pll3200c32,
};

static const struct clk_ops st_pll1200c32_ops = {
	.is_enabled	= clkgen_pll_is_enabled,
	.recalc_rate	= recalc_stm_pll1200c32,
};

static struct clk * __init clkgen_pll_register(const char *parent_name,
				struct clkgen_pll_data	*pll_data,
				void __iomem *reg,
				const char *clk_name)
{
	struct clkgen_pll *pll;
	struct clk *clk;
	struct clk_init_data init;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	init.name = clk_name;
	init.ops = pll_data->ops;

	init.flags = CLK_IS_BASIC | CLK_GET_RATE_NOCACHE;
	init.parent_names = &parent_name;
	init.num_parents  = 1;

	pll->data = pll_data;
	pll->regs_base = reg;
	pll->hw.init = &init;

	clk = clk_register(NULL, &pll->hw);
	if (IS_ERR(clk)) {
		kfree(pll);
		return clk;
	}

	pr_debug("%s: parent %s rate %lu\n",
			__clk_get_name(clk),
			__clk_get_name(clk_get_parent(clk)),
			clk_get_rate(clk));

	return clk;
}

static struct clk * __init clkgen_c65_lsdiv_register(const char *parent_name,
						     const char *clk_name)
{
	struct clk *clk;

	clk = clk_register_fixed_factor(NULL, clk_name, parent_name, 0, 1, 2);
	if (IS_ERR(clk))
		return clk;

	pr_debug("%s: parent %s rate %lu\n",
			__clk_get_name(clk),
			__clk_get_name(clk_get_parent(clk)),
			clk_get_rate(clk));
	return clk;
}

static void __iomem * __init clkgen_get_register_base(
				struct device_node *np)
{
	struct device_node *pnode;
	void __iomem *reg = NULL;

	pnode = of_get_parent(np);
	if (!pnode)
		return NULL;

	reg = of_iomap(pnode, 0);

	of_node_put(pnode);
	return reg;
}

#define CLKGENAx_PLL0_OFFSET 0x0
#define CLKGENAx_PLL1_OFFSET 0x4

static void __init clkgena_c65_pll_setup(struct device_node *np)
{
	const int num_pll_outputs = 3;
	struct clk_onecell_data *clk_data;
	const char *parent_name;
	void __iomem *reg;
	const char *clk_name;

	parent_name = of_clk_get_parent_name(np, 0);
	if (!parent_name)
		return;

	reg = clkgen_get_register_base(np);
	if (!reg)
		return;

	clk_data = kzalloc(sizeof(*clk_data), GFP_KERNEL);
	if (!clk_data)
		return;

	clk_data->clk_num = num_pll_outputs;
	clk_data->clks = kzalloc(clk_data->clk_num * sizeof(struct clk *),
				 GFP_KERNEL);

	if (!clk_data->clks)
		goto err;

	if (of_property_read_string_index(np, "clock-output-names",
					  0, &clk_name))
		goto err;

	/*
	 * PLL0 HS (high speed) output
	 */
	clk_data->clks[0] = clkgen_pll_register(parent_name,
			(struct clkgen_pll_data *) &st_pll1600c65_ax,
			reg + CLKGENAx_PLL0_OFFSET, clk_name);

	if (IS_ERR(clk_data->clks[0]))
		goto err;

	if (of_property_read_string_index(np, "clock-output-names",
					  1, &clk_name))
		goto err;

	/*
	 * PLL0 LS (low speed) output, which is a fixed divide by 2 of the
	 * high speed output.
	 */
	clk_data->clks[1] = clkgen_c65_lsdiv_register(__clk_get_name
						      (clk_data->clks[0]),
						      clk_name);

	if (IS_ERR(clk_data->clks[1]))
		goto err;

	if (of_property_read_string_index(np, "clock-output-names",
					  2, &clk_name))
		goto err;

	/*
	 * PLL1 output
	 */
	clk_data->clks[2] = clkgen_pll_register(parent_name,
			(struct clkgen_pll_data *) &st_pll800c65_ax,
			reg + CLKGENAx_PLL1_OFFSET, clk_name);

	if (IS_ERR(clk_data->clks[2]))
		goto err;

	of_clk_add_provider(np, of_clk_src_onecell_get, clk_data);
	return;

err:
	kfree(clk_data->clks);
	kfree(clk_data);
}
CLK_OF_DECLARE(clkgena_c65_plls,
	       "st,clkgena-plls-c65", clkgena_c65_pll_setup);

static struct clk * __init clkgen_odf_register(const char *parent_name,
					       void __iomem *reg,
					       struct clkgen_pll_data *pll_data,
					       int odf,
					       spinlock_t *odf_lock,
					       const char *odf_name)
{
	struct clk *clk;
	unsigned long flags;
	struct clk_gate *gate;
	struct clk_divider *div;

	flags = CLK_GET_RATE_NOCACHE | CLK_SET_RATE_GATE;

	gate = kzalloc(sizeof(*gate), GFP_KERNEL);
	if (!gate)
		return ERR_PTR(-ENOMEM);

	gate->flags = CLK_GATE_SET_TO_DISABLE;
	gate->reg = reg + pll_data->odf_gate[odf].offset;
	gate->bit_idx = pll_data->odf_gate[odf].shift;
	gate->lock = odf_lock;

	div = kzalloc(sizeof(*div), GFP_KERNEL);
	if (!div) {
		kfree(gate);
		return ERR_PTR(-ENOMEM);
	}

	div->flags = CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO;
	div->reg = reg + pll_data->odf[odf].offset;
	div->shift = pll_data->odf[odf].shift;
	div->width = fls(pll_data->odf[odf].mask);
	div->lock = odf_lock;

	clk = clk_register_composite(NULL, odf_name, &parent_name, 1,
				     NULL, NULL,
				     &div->hw, &clk_divider_ops,
				     &gate->hw, &clk_gate_ops,
				     flags);
	if (IS_ERR(clk))
		return clk;

	pr_debug("%s: parent %s rate %lu\n",
			__clk_get_name(clk),
			__clk_get_name(clk_get_parent(clk)),
			clk_get_rate(clk));
	return clk;
}

static const struct of_device_id c32_pll_of_match[] = {
	{
		.compatible = "st,plls-c32-a1x-0",
		.data = &st_pll3200c32_a1x_0,
	},
	{
		.compatible = "st,plls-c32-a1x-1",
		.data = &st_pll3200c32_a1x_1,
	},
	{
		.compatible = "st,stih415-plls-c32-a9",
		.data = &st_pll3200c32_a9_415,
	},
	{
		.compatible = "st,stih415-plls-c32-ddr",
		.data = &st_pll3200c32_ddr_415,
	},
	{
		.compatible = "st,stih416-plls-c32-a9",
		.data = &st_pll3200c32_a9_416,
	},
	{
		.compatible = "st,stih416-plls-c32-ddr",
		.data = &st_pll3200c32_ddr_416,
	},
	{
		.compatible = "st,stih407-plls-c32-a0",
		.data = &st_pll3200c32_407_a0,
	},
	{
		.compatible = "st,stih407-plls-c32-c0_0",
		.data = &st_pll3200c32_407_c0_0,
	},
	{
		.compatible = "st,stih407-plls-c32-c0_1",
		.data = &st_pll3200c32_407_c0_1,
	},
	{
		.compatible = "st,stih407-plls-c32-a9",
		.data = &st_pll3200c32_407_a9,
	},
	{}
};

static void __init clkgen_c32_pll_setup(struct device_node *np)
{
	const struct of_device_id *match;
	struct clk *clk;
	const char *parent_name, *pll_name;
	void __iomem *pll_base;
	int num_odfs, odf;
	struct clk_onecell_data *clk_data;
	struct clkgen_pll_data	*data;

	match = of_match_node(c32_pll_of_match, np);
	if (!match) {
		pr_err("%s: No matching data\n", __func__);
		return;
	}

	data = (struct clkgen_pll_data *) match->data;

	parent_name = of_clk_get_parent_name(np, 0);
	if (!parent_name)
		return;

	pll_base = clkgen_get_register_base(np);
	if (!pll_base)
		return;

	clk = clkgen_pll_register(parent_name, data, pll_base, np->name);
	if (IS_ERR(clk))
		return;

	pll_name = __clk_get_name(clk);

	num_odfs = data->num_odfs;

	clk_data = kzalloc(sizeof(*clk_data), GFP_KERNEL);
	if (!clk_data)
		return;

	clk_data->clk_num = num_odfs;
	clk_data->clks = kzalloc(clk_data->clk_num * sizeof(struct clk *),
				 GFP_KERNEL);

	if (!clk_data->clks)
		goto err;

	for (odf = 0; odf < num_odfs; odf++) {
		struct clk *clk;
		const char *clk_name;

		if (of_property_read_string_index(np, "clock-output-names",
						  odf, &clk_name))
			return;

		clk = clkgen_odf_register(pll_name, pll_base, data,
				odf, &clkgena_c32_odf_lock, clk_name);
		if (IS_ERR(clk))
			goto err;

		clk_data->clks[odf] = clk;
	}

	of_clk_add_provider(np, of_clk_src_onecell_get, clk_data);
	return;

err:
	kfree(pll_name);
	kfree(clk_data->clks);
	kfree(clk_data);
}
CLK_OF_DECLARE(clkgen_c32_pll, "st,clkgen-plls-c32", clkgen_c32_pll_setup);

static const struct of_device_id c32_gpu_pll_of_match[] = {
	{
		.compatible = "st,stih415-gpu-pll-c32",
		.data = &st_pll1200c32_gpu_415,
	},
	{
		.compatible = "st,stih416-gpu-pll-c32",
		.data = &st_pll1200c32_gpu_416,
	},
	{}
};

static void __init clkgengpu_c32_pll_setup(struct device_node *np)
{
	const struct of_device_id *match;
	struct clk *clk;
	const char *parent_name;
	void __iomem *reg;
	const char *clk_name;
	struct clkgen_pll_data	*data;

	match = of_match_node(c32_gpu_pll_of_match, np);
	if (!match) {
		pr_err("%s: No matching data\n", __func__);
		return;
	}

	data = (struct clkgen_pll_data *)match->data;

	parent_name = of_clk_get_parent_name(np, 0);
	if (!parent_name)
		return;

	reg = clkgen_get_register_base(np);
	if (!reg)
		return;

	if (of_property_read_string_index(np, "clock-output-names",
					  0, &clk_name))
		return;

	/*
	 * PLL 1200MHz output
	 */
	clk = clkgen_pll_register(parent_name, data, reg, clk_name);

	if (!IS_ERR(clk))
		of_clk_add_provider(np, of_clk_src_simple_get, clk);

	return;
}
CLK_OF_DECLARE(clkgengpu_c32_pll,
	       "st,clkgengpu-pll-c32", clkgengpu_c32_pll_setup);
