// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Copyright (c) 2018 BayLibre, SAS.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 */

#include <linux/clk-provider.h>
#include "clkc-audio.h"

/*
 * This is a special clock for the audio controller.
 * The phase of mst_sclk clock output can be controlled independently
 * for the outside world (ph0), the tdmout (ph1) and tdmin (ph2).
 * Controlling these 3 phases as just one makes things simpler and
 * give the same clock view to all the element on the i2s bus.
 * If necessary, we can still control the phase in the tdm block
 * which makes these independent control redundant.
 */
static inline struct meson_clk_triphase_data *
meson_clk_triphase_data(struct clk_regmap *clk)
{
	return (struct meson_clk_triphase_data *)clk->data;
}

static void meson_clk_triphase_sync(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_triphase_data *tph = meson_clk_triphase_data(clk);
	unsigned int val;

	/* Get phase 0 and sync it to phase 1 and 2 */
	val = meson_parm_read(clk->map, &tph->ph0);
	meson_parm_write(clk->map, &tph->ph1, val);
	meson_parm_write(clk->map, &tph->ph2, val);
}

static int meson_clk_triphase_get_phase(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_triphase_data *tph = meson_clk_triphase_data(clk);
	unsigned int val;

	/* Phase are in sync, reading phase 0 is enough */
	val = meson_parm_read(clk->map, &tph->ph0);

	return meson_clk_degrees_from_val(val, tph->ph0.width);
}

static int meson_clk_triphase_set_phase(struct clk_hw *hw, int degrees)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_triphase_data *tph = meson_clk_triphase_data(clk);
	unsigned int val;

	val = meson_clk_degrees_to_val(degrees, tph->ph0.width);
	meson_parm_write(clk->map, &tph->ph0, val);
	meson_parm_write(clk->map, &tph->ph1, val);
	meson_parm_write(clk->map, &tph->ph2, val);

	return 0;
}

const struct clk_ops meson_clk_triphase_ops = {
	.init		= meson_clk_triphase_sync,
	.get_phase	= meson_clk_triphase_get_phase,
	.set_phase	= meson_clk_triphase_set_phase,
};
EXPORT_SYMBOL_GPL(meson_clk_triphase_ops);
