// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 BayLibre, SAS.
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */

#include <linux/clk-provider.h>
#include <linux/module.h>

#include "clk-regmap.h"
#include "vid-pll-div.h"

static inline struct meson_vid_pll_div_data *
meson_vid_pll_div_data(struct clk_regmap *clk)
{
	return (struct meson_vid_pll_div_data *)clk->data;
}

/*
 * This vid_pll divided is a fully programmable fractionnal divider to
 * achieve complex video clock rates.
 *
 * Here are provided the commonly used fraction values provided by Amlogic.
 */

struct vid_pll_div {
	unsigned int shift_val;
	unsigned int shift_sel;
	unsigned int divider;
	unsigned int multiplier;
};

#define VID_PLL_DIV(_val, _sel, _ft, _fb)				\
	{								\
		.shift_val = (_val),					\
		.shift_sel = (_sel),					\
		.divider = (_ft),					\
		.multiplier = (_fb),					\
	}

static const struct vid_pll_div vid_pll_div_table[] = {
	VID_PLL_DIV(0x0aaa, 0, 2, 1),	/* 2/1  => /2 */
	VID_PLL_DIV(0x5294, 2, 5, 2),	/* 5/2  => /2.5 */
	VID_PLL_DIV(0x0db6, 0, 3, 1),	/* 3/1  => /3 */
	VID_PLL_DIV(0x36cc, 1, 7, 2),	/* 7/2  => /3.5 */
	VID_PLL_DIV(0x6666, 2, 15, 4),	/* 15/4 => /3.75 */
	VID_PLL_DIV(0x0ccc, 0, 4, 1),	/* 4/1  => /4 */
	VID_PLL_DIV(0x739c, 2, 5, 1),	/* 5/1  => /5 */
	VID_PLL_DIV(0x0e38, 0, 6, 1),	/* 6/1  => /6 */
	VID_PLL_DIV(0x0000, 3, 25, 4),	/* 25/4 => /6.25 */
	VID_PLL_DIV(0x3c78, 1, 7, 1),	/* 7/1  => /7 */
	VID_PLL_DIV(0x78f0, 2, 15, 2),	/* 15/2 => /7.5 */
	VID_PLL_DIV(0x0fc0, 0, 12, 1),	/* 12/1 => /12 */
	VID_PLL_DIV(0x3f80, 1, 14, 1),	/* 14/1 => /14 */
	VID_PLL_DIV(0x7f80, 2, 15, 1),	/* 15/1 => /15 */
};

#define to_meson_vid_pll_div(_hw) \
	container_of(_hw, struct meson_vid_pll_div, hw)

static const struct vid_pll_div *_get_table_val(unsigned int shift_val,
						unsigned int shift_sel)
{
	int i;

	for (i = 0 ; i < ARRAY_SIZE(vid_pll_div_table) ; ++i) {
		if (vid_pll_div_table[i].shift_val == shift_val &&
		    vid_pll_div_table[i].shift_sel == shift_sel)
			return &vid_pll_div_table[i];
	}

	return NULL;
}

static unsigned long meson_vid_pll_div_recalc_rate(struct clk_hw *hw,
						   unsigned long parent_rate)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_vid_pll_div_data *pll_div = meson_vid_pll_div_data(clk);
	const struct vid_pll_div *div;

	div = _get_table_val(meson_parm_read(clk->map, &pll_div->val),
			     meson_parm_read(clk->map, &pll_div->sel));
	if (!div || !div->divider) {
		pr_debug("%s: Invalid config value for vid_pll_div\n", __func__);
		return 0;
	}

	return DIV_ROUND_UP_ULL(parent_rate * div->multiplier, div->divider);
}

const struct clk_ops meson_vid_pll_div_ro_ops = {
	.recalc_rate	= meson_vid_pll_div_recalc_rate,
};
EXPORT_SYMBOL_NS_GPL(meson_vid_pll_div_ro_ops, CLK_MESON);

MODULE_DESCRIPTION("Amlogic video pll divider driver");
MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(CLK_MESON);
