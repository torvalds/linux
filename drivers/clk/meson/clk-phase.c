// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Copyright (c) 2018 BayLibre, SAS.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 */

#include <linux/clk-provider.h>
#include "clkc.h"

#define phase_step(_width) (360 / (1 << (_width)))

static inline struct meson_clk_phase_data *
meson_clk_phase_data(struct clk_regmap *clk)
{
	return (struct meson_clk_phase_data *)clk->data;
}

int meson_clk_degrees_from_val(unsigned int val, unsigned int width)
{
	return phase_step(width) * val;
}
EXPORT_SYMBOL_GPL(meson_clk_degrees_from_val);

unsigned int meson_clk_degrees_to_val(int degrees, unsigned int width)
{
	unsigned int val = DIV_ROUND_CLOSEST(degrees, phase_step(width));

	/*
	 * This last calculation is here for cases when degrees is rounded
	 * to 360, in which case val == (1 << width).
	 */
	return val % (1 << width);
}
EXPORT_SYMBOL_GPL(meson_clk_degrees_to_val);

static int meson_clk_phase_get_phase(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_phase_data *phase = meson_clk_phase_data(clk);
	unsigned int val;

	val = meson_parm_read(clk->map, &phase->ph);

	return meson_clk_degrees_from_val(val, phase->ph.width);
}

static int meson_clk_phase_set_phase(struct clk_hw *hw, int degrees)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_phase_data *phase = meson_clk_phase_data(clk);
	unsigned int val;

	val = meson_clk_degrees_to_val(degrees, phase->ph.width);
	meson_parm_write(clk->map, &phase->ph, val);

	return 0;
}

const struct clk_ops meson_clk_phase_ops = {
	.get_phase	= meson_clk_phase_get_phase,
	.set_phase	= meson_clk_phase_set_phase,
};
EXPORT_SYMBOL_GPL(meson_clk_phase_ops);
