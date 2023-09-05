// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2014 Marvell Technology Group Ltd.
 *
 * Alexandre Belloni <alexandre.belloni@free-electrons.com>
 * Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 */
#include <linux/bitops.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "berlin2-div.h"

/*
 * Clock dividers in Berlin2 SoCs comprise a complex cell to select
 * input pll and divider. The virtual structure as it is used in Marvell
 * BSP code can be seen as:
 *
 *                      +---+
 * pll0 --------------->| 0 |                   +---+
 *           +---+      |(B)|--+--------------->| 0 |      +---+
 * pll1.0 -->| 0 |  +-->| 1 |  |   +--------+   |(E)|----->| 0 |   +---+
 * pll1.1 -->| 1 |  |   +---+  +-->|(C) 1:M |-->| 1 |      |(F)|-->|(G)|->
 * ...    -->|(A)|--+          |   +--------+   +---+  +-->| 1 |   +---+
 * ...    -->|   |             +-->|(D) 1:3 |----------+   +---+
 * pll1.N -->| N |                 +---------
 *           +---+
 *
 * (A) input pll clock mux controlled by               <PllSelect[1:n]>
 * (B) input pll bypass mux controlled by              <PllSwitch>
 * (C) programmable clock divider controlled by        <Select[1:n]>
 * (D) constant div-by-3 clock divider
 * (E) programmable clock divider bypass controlled by <Switch>
 * (F) constant div-by-3 clock mux controlled by       <D3Switch>
 * (G) clock gate controlled by                        <Enable>
 *
 * For whatever reason, above control signals come in two flavors:
 * - single register dividers with all bits in one register
 * - shared register dividers with bits spread over multiple registers
 *   (including signals for the same cell spread over consecutive registers)
 *
 * Also, clock gate and pll mux is not available on every div cell, so
 * we have to deal with those, too. We reuse common clock composite driver
 * for it.
 */

#define PLL_SELECT_MASK	0x7
#define DIV_SELECT_MASK	0x7

struct berlin2_div {
	struct clk_hw hw;
	void __iomem *base;
	struct berlin2_div_map map;
	spinlock_t *lock;
};

#define to_berlin2_div(hw) container_of(hw, struct berlin2_div, hw)

static u8 clk_div[] = { 1, 2, 4, 6, 8, 12, 1, 1 };

static int berlin2_div_is_enabled(struct clk_hw *hw)
{
	struct berlin2_div *div = to_berlin2_div(hw);
	struct berlin2_div_map *map = &div->map;
	u32 reg;

	if (div->lock)
		spin_lock(div->lock);

	reg = readl_relaxed(div->base + map->gate_offs);
	reg >>= map->gate_shift;

	if (div->lock)
		spin_unlock(div->lock);

	return (reg & 0x1);
}

static int berlin2_div_enable(struct clk_hw *hw)
{
	struct berlin2_div *div = to_berlin2_div(hw);
	struct berlin2_div_map *map = &div->map;
	u32 reg;

	if (div->lock)
		spin_lock(div->lock);

	reg = readl_relaxed(div->base + map->gate_offs);
	reg |= BIT(map->gate_shift);
	writel_relaxed(reg, div->base + map->gate_offs);

	if (div->lock)
		spin_unlock(div->lock);

	return 0;
}

static void berlin2_div_disable(struct clk_hw *hw)
{
	struct berlin2_div *div = to_berlin2_div(hw);
	struct berlin2_div_map *map = &div->map;
	u32 reg;

	if (div->lock)
		spin_lock(div->lock);

	reg = readl_relaxed(div->base + map->gate_offs);
	reg &= ~BIT(map->gate_shift);
	writel_relaxed(reg, div->base + map->gate_offs);

	if (div->lock)
		spin_unlock(div->lock);
}

static int berlin2_div_set_parent(struct clk_hw *hw, u8 index)
{
	struct berlin2_div *div = to_berlin2_div(hw);
	struct berlin2_div_map *map = &div->map;
	u32 reg;

	if (div->lock)
		spin_lock(div->lock);

	/* index == 0 is PLL_SWITCH */
	reg = readl_relaxed(div->base + map->pll_switch_offs);
	if (index == 0)
		reg &= ~BIT(map->pll_switch_shift);
	else
		reg |= BIT(map->pll_switch_shift);
	writel_relaxed(reg, div->base + map->pll_switch_offs);

	/* index > 0 is PLL_SELECT */
	if (index > 0) {
		reg = readl_relaxed(div->base + map->pll_select_offs);
		reg &= ~(PLL_SELECT_MASK << map->pll_select_shift);
		reg |= (index - 1) << map->pll_select_shift;
		writel_relaxed(reg, div->base + map->pll_select_offs);
	}

	if (div->lock)
		spin_unlock(div->lock);

	return 0;
}

static u8 berlin2_div_get_parent(struct clk_hw *hw)
{
	struct berlin2_div *div = to_berlin2_div(hw);
	struct berlin2_div_map *map = &div->map;
	u32 reg;
	u8 index = 0;

	if (div->lock)
		spin_lock(div->lock);

	/* PLL_SWITCH == 0 is index 0 */
	reg = readl_relaxed(div->base + map->pll_switch_offs);
	reg &= BIT(map->pll_switch_shift);
	if (reg) {
		reg = readl_relaxed(div->base + map->pll_select_offs);
		reg >>= map->pll_select_shift;
		reg &= PLL_SELECT_MASK;
		index = 1 + reg;
	}

	if (div->lock)
		spin_unlock(div->lock);

	return index;
}

static unsigned long berlin2_div_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	struct berlin2_div *div = to_berlin2_div(hw);
	struct berlin2_div_map *map = &div->map;
	u32 divsw, div3sw, divider = 1;

	if (div->lock)
		spin_lock(div->lock);

	divsw = readl_relaxed(div->base + map->div_switch_offs) &
		(1 << map->div_switch_shift);
	div3sw = readl_relaxed(div->base + map->div3_switch_offs) &
		(1 << map->div3_switch_shift);

	/* constant divide-by-3 (dominant) */
	if (div3sw != 0) {
		divider = 3;
	/* divider can be bypassed with DIV_SWITCH == 0 */
	} else if (divsw == 0) {
		divider = 1;
	/* clock divider determined by DIV_SELECT */
	} else {
		u32 reg;
		reg = readl_relaxed(div->base + map->div_select_offs);
		reg >>= map->div_select_shift;
		reg &= DIV_SELECT_MASK;
		divider = clk_div[reg];
	}

	if (div->lock)
		spin_unlock(div->lock);

	return parent_rate / divider;
}

static const struct clk_ops berlin2_div_rate_ops = {
	.determine_rate	= clk_hw_determine_rate_no_reparent,
	.recalc_rate	= berlin2_div_recalc_rate,
};

static const struct clk_ops berlin2_div_gate_ops = {
	.is_enabled	= berlin2_div_is_enabled,
	.enable		= berlin2_div_enable,
	.disable	= berlin2_div_disable,
};

static const struct clk_ops berlin2_div_mux_ops = {
	.set_parent	= berlin2_div_set_parent,
	.get_parent	= berlin2_div_get_parent,
};

struct clk_hw * __init
berlin2_div_register(const struct berlin2_div_map *map,
		     void __iomem *base, const char *name, u8 div_flags,
		     const char **parent_names, int num_parents,
		     unsigned long flags, spinlock_t *lock)
{
	const struct clk_ops *mux_ops = &berlin2_div_mux_ops;
	const struct clk_ops *rate_ops = &berlin2_div_rate_ops;
	const struct clk_ops *gate_ops = &berlin2_div_gate_ops;
	struct berlin2_div *div;

	div = kzalloc(sizeof(*div), GFP_KERNEL);
	if (!div)
		return ERR_PTR(-ENOMEM);

	/* copy div_map to allow __initconst */
	memcpy(&div->map, map, sizeof(*map));
	div->base = base;
	div->lock = lock;

	if ((div_flags & BERLIN2_DIV_HAS_GATE) == 0)
		gate_ops = NULL;
	if ((div_flags & BERLIN2_DIV_HAS_MUX) == 0)
		mux_ops = NULL;

	return clk_hw_register_composite(NULL, name, parent_names, num_parents,
				      &div->hw, mux_ops, &div->hw, rate_ops,
				      &div->hw, gate_ops, flags);
}
