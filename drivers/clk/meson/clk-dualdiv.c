// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2017 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 */

/*
 * The AO Domain embeds a dual/divider to generate a more precise
 * 32,768KHz clock for low-power suspend mode and CEC.
 *     ______   ______
 *    |      | |      |
 *    | Div1 |-| Cnt1 |
 *   /|______| |______|\
 * -|  ______   ______  X--> Out
 *   \|      | |      |/
 *    | Div2 |-| Cnt2 |
 *    |______| |______|
 *
 * The dividing can be switched to single or dual, with a counter
 * for each divider to set when the switching is done.
 */

#include <linux/clk-provider.h>
#include <linux/module.h>

#include "clk-regmap.h"
#include "clk-dualdiv.h"

static inline struct meson_clk_dualdiv_data *
meson_clk_dualdiv_data(struct clk_regmap *clk)
{
	return (struct meson_clk_dualdiv_data *)clk->data;
}

static unsigned long
__dualdiv_param_to_rate(unsigned long parent_rate,
			const struct meson_clk_dualdiv_param *p)
{
	if (!p->dual)
		return DIV_ROUND_CLOSEST(parent_rate, p->n1);

	return DIV_ROUND_CLOSEST(parent_rate * (p->m1 + p->m2),
				 p->n1 * p->m1 + p->n2 * p->m2);
}

static unsigned long meson_clk_dualdiv_recalc_rate(struct clk_hw *hw,
						   unsigned long parent_rate)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_dualdiv_data *dualdiv = meson_clk_dualdiv_data(clk);
	struct meson_clk_dualdiv_param setting;

	setting.dual = meson_parm_read(clk->map, &dualdiv->dual);
	setting.n1 = meson_parm_read(clk->map, &dualdiv->n1) + 1;
	setting.m1 = meson_parm_read(clk->map, &dualdiv->m1) + 1;
	setting.n2 = meson_parm_read(clk->map, &dualdiv->n2) + 1;
	setting.m2 = meson_parm_read(clk->map, &dualdiv->m2) + 1;

	return __dualdiv_param_to_rate(parent_rate, &setting);
}

static const struct meson_clk_dualdiv_param *
__dualdiv_get_setting(unsigned long rate, unsigned long parent_rate,
		      struct meson_clk_dualdiv_data *dualdiv)
{
	const struct meson_clk_dualdiv_param *table = dualdiv->table;
	unsigned long best = 0, now = 0;
	unsigned int i, best_i = 0;

	if (!table)
		return NULL;

	for (i = 0; table[i].n1; i++) {
		now = __dualdiv_param_to_rate(parent_rate, &table[i]);

		/* If we get an exact match, don't bother any further */
		if (now == rate) {
			return &table[i];
		} else if (abs(now - rate) < abs(best - rate)) {
			best = now;
			best_i = i;
		}
	}

	return (struct meson_clk_dualdiv_param *)&table[best_i];
}

static int meson_clk_dualdiv_determine_rate(struct clk_hw *hw,
					    struct clk_rate_request *req)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_dualdiv_data *dualdiv = meson_clk_dualdiv_data(clk);
	const struct meson_clk_dualdiv_param *setting;

	setting = __dualdiv_get_setting(req->rate, req->best_parent_rate,
					dualdiv);
	if (setting)
		req->rate = __dualdiv_param_to_rate(req->best_parent_rate,
						    setting);
	else
		req->rate = meson_clk_dualdiv_recalc_rate(hw,
							  req->best_parent_rate);

	return 0;
}

static int meson_clk_dualdiv_set_rate(struct clk_hw *hw, unsigned long rate,
				      unsigned long parent_rate)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_dualdiv_data *dualdiv = meson_clk_dualdiv_data(clk);
	const struct meson_clk_dualdiv_param *setting =
		__dualdiv_get_setting(rate, parent_rate, dualdiv);

	if (!setting)
		return -EINVAL;

	meson_parm_write(clk->map, &dualdiv->dual, setting->dual);
	meson_parm_write(clk->map, &dualdiv->n1, setting->n1 - 1);
	meson_parm_write(clk->map, &dualdiv->m1, setting->m1 - 1);
	meson_parm_write(clk->map, &dualdiv->n2, setting->n2 - 1);
	meson_parm_write(clk->map, &dualdiv->m2, setting->m2 - 1);

	return 0;
}

const struct clk_ops meson_clk_dualdiv_ops = {
	.init		= clk_regmap_init,
	.recalc_rate	= meson_clk_dualdiv_recalc_rate,
	.determine_rate	= meson_clk_dualdiv_determine_rate,
	.set_rate	= meson_clk_dualdiv_set_rate,
};
EXPORT_SYMBOL_NS_GPL(meson_clk_dualdiv_ops, "CLK_MESON");

const struct clk_ops meson_clk_dualdiv_ro_ops = {
	.init		= clk_regmap_init,
	.recalc_rate	= meson_clk_dualdiv_recalc_rate,
};
EXPORT_SYMBOL_NS_GPL(meson_clk_dualdiv_ro_ops, "CLK_MESON");

MODULE_DESCRIPTION("Amlogic dual divider driver");
MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("CLK_MESON");
