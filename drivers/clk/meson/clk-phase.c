// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Copyright (c) 2018 BayLibre, SAS.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 */

#include <linux/clk-provider.h>
#include <linux/module.h>

#include "clk-regmap.h"
#include "clk-phase.h"

#define phase_step(_width) (360 / (1 << (_width)))

static inline struct meson_clk_phase_data *
meson_clk_phase_data(struct clk_regmap *clk)
{
	return (struct meson_clk_phase_data *)clk->data;
}

static int meson_clk_degrees_from_val(unsigned int val, unsigned int width)
{
	return phase_step(width) * val;
}

static unsigned int meson_clk_degrees_to_val(int degrees, unsigned int width)
{
	unsigned int val = DIV_ROUND_CLOSEST(degrees, phase_step(width));

	/*
	 * This last calculation is here for cases when degrees is rounded
	 * to 360, in which case val == (1 << width).
	 */
	return val % (1 << width);
}

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

static int meson_clk_triphase_sync(struct clk_hw *hw)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_triphase_data *tph = meson_clk_triphase_data(clk);
	unsigned int val;

	/* Get phase 0 and sync it to phase 1 and 2 */
	val = meson_parm_read(clk->map, &tph->ph0);
	meson_parm_write(clk->map, &tph->ph1, val);
	meson_parm_write(clk->map, &tph->ph2, val);

	return 0;
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

MODULE_DESCRIPTION("Amlogic phase driver");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL v2");
