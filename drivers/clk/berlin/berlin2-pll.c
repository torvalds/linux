// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2014 Marvell Technology Group Ltd.
 *
 * Alexandre Belloni <alexandre.belloni@free-electrons.com>
 * Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 */
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <asm/div64.h>

#include "berlin2-div.h"
#include "berlin2-pll.h"

struct berlin2_pll {
	struct clk_hw hw;
	void __iomem *base;
	struct berlin2_pll_map map;
};

#define to_berlin2_pll(hw) container_of(hw, struct berlin2_pll, hw)

#define SPLL_CTRL0	0x00
#define SPLL_CTRL1	0x04
#define SPLL_CTRL2	0x08
#define SPLL_CTRL3	0x0c
#define SPLL_CTRL4	0x10

#define FBDIV_MASK	0x1ff
#define RFDIV_MASK	0x1f
#define DIVSEL_MASK	0xf

/*
 * The output frequency formula for the pll is:
 * clkout = fbdiv / refdiv * parent / vcodiv
 */
static unsigned long
berlin2_pll_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct berlin2_pll *pll = to_berlin2_pll(hw);
	struct berlin2_pll_map *map = &pll->map;
	u32 val, fbdiv, rfdiv, vcodivsel, vcodiv;
	u64 rate = parent_rate;

	val = readl_relaxed(pll->base + SPLL_CTRL0);
	fbdiv = (val >> map->fbdiv_shift) & FBDIV_MASK;
	rfdiv = (val >> map->rfdiv_shift) & RFDIV_MASK;
	if (rfdiv == 0) {
		pr_warn("%s has zero rfdiv\n", clk_hw_get_name(hw));
		rfdiv = 1;
	}

	val = readl_relaxed(pll->base + SPLL_CTRL1);
	vcodivsel = (val >> map->divsel_shift) & DIVSEL_MASK;
	vcodiv = map->vcodiv[vcodivsel];
	if (vcodiv == 0) {
		pr_warn("%s has zero vcodiv (index %d)\n",
			clk_hw_get_name(hw), vcodivsel);
		vcodiv = 1;
	}

	rate *= fbdiv * map->mult;
	do_div(rate, rfdiv * vcodiv);

	return (unsigned long)rate;
}

static const struct clk_ops berlin2_pll_ops = {
	.recalc_rate	= berlin2_pll_recalc_rate,
};

int __init
berlin2_pll_register(const struct berlin2_pll_map *map,
		     void __iomem *base, const char *name,
		     const char *parent_name, unsigned long flags)
{
	struct clk_init_data init = {};
	struct berlin2_pll *pll;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return -ENOMEM;

	/* copy pll_map to allow __initconst */
	memcpy(&pll->map, map, sizeof(*map));
	pll->base = base;
	pll->hw.init = &init;
	init.name = name;
	init.ops = &berlin2_pll_ops;
	init.parent_names = &parent_name;
	init.num_parents = 1;
	init.flags = flags;

	return clk_hw_register(NULL, &pll->hw);
}
