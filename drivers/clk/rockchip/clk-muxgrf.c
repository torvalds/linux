/*
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/regmap.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include "clk.h"

struct rockchip_muxgrf_clock {
	struct clk_hw		hw;
	struct regmap		*regmap;
	u32			reg;
	u32			shift;
	u32			width;
	int			flags;
};

#define to_muxgrf_clock(_hw) container_of(_hw, struct rockchip_muxgrf_clock, hw)

static u8 rockchip_muxgrf_get_parent(struct clk_hw *hw)
{
	struct rockchip_muxgrf_clock *mux = to_muxgrf_clock(hw);
	unsigned int mask = GENMASK(mux->width - 1, 0);
	unsigned int val;

	regmap_read(mux->regmap, mux->reg, &val);

	val >>= mux->shift;
	val &= mask;

	return val;
}

static int rockchip_muxgrf_set_parent(struct clk_hw *hw, u8 index)
{
	struct rockchip_muxgrf_clock *mux = to_muxgrf_clock(hw);
	unsigned int mask = GENMASK(mux->width + mux->shift - 1, mux->shift);
	unsigned int val;

	val = index;
	val <<= mux->shift;

	if (mux->flags & CLK_MUX_HIWORD_MASK)
		return regmap_write(mux->regmap, mux->reg, val | (mask << 16));
	else
		return regmap_update_bits(mux->regmap, mux->reg, mask, val);
}

static const struct clk_ops rockchip_muxgrf_clk_ops = {
	.get_parent = rockchip_muxgrf_get_parent,
	.set_parent = rockchip_muxgrf_set_parent,
	.determine_rate = __clk_mux_determine_rate,
};

struct clk *rockchip_clk_register_muxgrf(const char *name,
				const char *const *parent_names, u8 num_parents,
				int flags, struct regmap *regmap, int reg,
				int shift, int width, int mux_flags)
{
	struct rockchip_muxgrf_clock *muxgrf_clock;
	struct clk_init_data init;
	struct clk *clk;

	if (IS_ERR(regmap)) {
		pr_err("%s: regmap not available\n", __func__);
		return ERR_PTR(-ENOTSUPP);
	}

	muxgrf_clock = kmalloc(sizeof(*muxgrf_clock), GFP_KERNEL);
	if (!muxgrf_clock)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.flags = flags;
	init.num_parents = num_parents;
	init.parent_names = parent_names;
	init.ops = &rockchip_muxgrf_clk_ops;

	muxgrf_clock->hw.init = &init;
	muxgrf_clock->regmap = regmap;
	muxgrf_clock->reg = reg;
	muxgrf_clock->shift = shift;
	muxgrf_clock->width = width;
	muxgrf_clock->flags = mux_flags;

	clk = clk_register(NULL, &muxgrf_clock->hw);
	if (IS_ERR(clk))
		kfree(muxgrf_clock);

	return clk;
}
