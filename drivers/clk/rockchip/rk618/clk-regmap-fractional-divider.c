// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 Rockchip Electronics Co. Ltd.
 *
 * Base on code in drivers/clk/clk-fractional-divider.c.
 * See clk-fractional-divider.c for further copyright information.
 */

#include <linux/rational.h>

#include "clk-regmap.h"

#define to_clk_regmap_fractional_divider(_hw)	\
		container_of(_hw, struct clk_regmap_fractional_divider, hw)

static unsigned long
clk_regmap_fractional_divider_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	struct clk_regmap_fractional_divider *fd =
			to_clk_regmap_fractional_divider(hw);
	unsigned long m, n;
	u32 val;
	u64 ret;

	regmap_read(fd->regmap, fd->reg, &val);

	m = (val & fd->mmask) >> fd->mshift;
	n = (val & fd->nmask) >> fd->nshift;

	if (!n || !m)
		return parent_rate;

	ret = (u64)parent_rate * m;
	do_div(ret, n);

	return ret;
}

static void clk_regmap_fractional_divider_approximation(struct clk_hw *hw,
		unsigned long rate, unsigned long *parent_rate,
		unsigned long *m, unsigned long *n)
{
	struct clk_regmap_fractional_divider *fd =
			to_clk_regmap_fractional_divider(hw);
	unsigned long p_rate, p_parent_rate;
	struct clk_hw *p_parent;
	unsigned long scale;

	p_rate = clk_hw_get_rate(clk_hw_get_parent(hw));
	if ((rate * 20 > p_rate) && (p_rate % rate != 0)) {
		p_parent = clk_hw_get_parent(clk_hw_get_parent(hw));
		p_parent_rate = clk_hw_get_rate(p_parent);
		*parent_rate = p_parent_rate;
	}

	/*
	 * Get rate closer to *parent_rate to guarantee there is no overflow
	 * for m and n. In the result it will be the nearest rate left shifted
	 * by (scale - fd->nwidth) bits.
	 */
	scale = fls_long(*parent_rate / rate - 1);
	if (scale > fd->nwidth)
		rate <<= scale - fd->nwidth;

	rational_best_approximation(rate, *parent_rate,
				    GENMASK(fd->mwidth - 1, 0),
				    GENMASK(fd->nwidth - 1, 0),
				    m, n);
}

static long
clk_regmap_fractional_divider_round_rate(struct clk_hw *hw, unsigned long rate,
					 unsigned long *parent_rate)
{
	unsigned long m, n;
	u64 ret;

	if (!rate)
		return *parent_rate;

	if (rate >= *parent_rate)
		return *parent_rate;

	clk_regmap_fractional_divider_approximation(hw, rate, parent_rate,
						    &m, &n);

	ret = (u64)*parent_rate * m;
	do_div(ret, n);

	return ret;
}

static int
clk_regmap_fractional_divider_set_rate(struct clk_hw *hw, unsigned long rate,
				       unsigned long parent_rate)
{
	struct clk_regmap_fractional_divider *fd =
			to_clk_regmap_fractional_divider(hw);
	unsigned long m, n;
	u32 val;

	rational_best_approximation(rate, parent_rate,
			GENMASK(fd->mwidth - 1, 0), GENMASK(fd->nwidth - 1, 0),
			&m, &n);

	dev_dbg(fd->dev, "%s: parent_rate=%ld, m=%ld, n=%ld, rate=%ld\n",
		clk_hw_get_name(hw), parent_rate, m, n, rate);

	regmap_read(fd->regmap, fd->reg, &val);
	val &= ~(fd->mmask | fd->nmask);
	val |= (m << fd->mshift) | (n << fd->nshift);

	return regmap_write(fd->regmap, fd->reg, val);
}

const struct clk_ops clk_regmap_fractional_divider_ops = {
	.recalc_rate = clk_regmap_fractional_divider_recalc_rate,
	.round_rate = clk_regmap_fractional_divider_round_rate,
	.set_rate = clk_regmap_fractional_divider_set_rate,
};
EXPORT_SYMBOL_GPL(clk_regmap_fractional_divider_ops);

struct clk *
devm_clk_regmap_register_fractional_divider(struct device *dev,
					    const char *name,
					    const char *parent_name,
					    struct regmap *regmap,
					    u32 reg, unsigned long flags)
{
	struct clk_regmap_fractional_divider *fd;
	struct clk_init_data init;

	fd = devm_kzalloc(dev, sizeof(*fd), GFP_KERNEL);
	if (!fd)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &clk_regmap_fractional_divider_ops;
	init.flags = flags;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);

	fd->dev = dev;
	fd->regmap = regmap;
	fd->reg = reg;
	fd->mshift = 16;
	fd->mwidth = 16;
	fd->mmask = GENMASK(fd->mwidth - 1, 0) << fd->mshift;
	fd->nshift = 0;
	fd->nwidth = 16;
	fd->nmask = GENMASK(fd->nwidth - 1, 0) << fd->nshift;
	fd->hw.init = &init;

	return devm_clk_register(dev, &fd->hw);
}
EXPORT_SYMBOL_GPL(devm_clk_regmap_register_fractional_divider);
