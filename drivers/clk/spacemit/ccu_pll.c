// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024 SpacemiT Technology Co. Ltd
 * Copyright (c) 2024-2025 Haylen Chu <heylenay@4d2.org>
 */

#include <linux/clk-provider.h>
#include <linux/math.h>
#include <linux/regmap.h>

#include "ccu_common.h"
#include "ccu_pll.h"

#define PLL_TIMEOUT_US		3000
#define PLL_DELAY_US		5

#define PLL_SWCR3_EN		((u32)BIT(31))
#define PLL_SWCR3_MASK		GENMASK(30, 0)

static const struct ccu_pll_rate_tbl *ccu_pll_lookup_best_rate(struct ccu_pll *pll,
							       unsigned long rate)
{
	struct ccu_pll_config *config = &pll->config;
	const struct ccu_pll_rate_tbl *best_entry;
	unsigned long best_delta = ULONG_MAX;
	int i;

	for (i = 0; i < config->tbl_num; i++) {
		const struct ccu_pll_rate_tbl *entry = &config->rate_tbl[i];
		unsigned long delta = abs_diff(entry->rate, rate);

		if (delta < best_delta) {
			best_delta = delta;
			best_entry = entry;
		}
	}

	return best_entry;
}

static const struct ccu_pll_rate_tbl *ccu_pll_lookup_matched_entry(struct ccu_pll *pll)
{
	struct ccu_pll_config *config = &pll->config;
	u32 swcr1, swcr3;
	int i;

	swcr1 = ccu_read(&pll->common, swcr1);
	swcr3 = ccu_read(&pll->common, swcr3);
	swcr3 &= PLL_SWCR3_MASK;

	for (i = 0; i < config->tbl_num; i++) {
		const struct ccu_pll_rate_tbl *entry = &config->rate_tbl[i];

		if (swcr1 == entry->swcr1 && swcr3 == entry->swcr3)
			return entry;
	}

	return NULL;
}

static void ccu_pll_update_param(struct ccu_pll *pll, const struct ccu_pll_rate_tbl *entry)
{
	struct ccu_common *common = &pll->common;

	regmap_write(common->regmap, common->reg_swcr1, entry->swcr1);
	ccu_update(common, swcr3, PLL_SWCR3_MASK, entry->swcr3);
}

static int ccu_pll_is_enabled(struct clk_hw *hw)
{
	struct ccu_common *common = hw_to_ccu_common(hw);

	return ccu_read(common, swcr3) & PLL_SWCR3_EN;
}

static int ccu_pll_enable(struct clk_hw *hw)
{
	struct ccu_pll *pll = hw_to_ccu_pll(hw);
	struct ccu_common *common = &pll->common;
	unsigned int tmp;

	ccu_update(common, swcr3, PLL_SWCR3_EN, PLL_SWCR3_EN);

	/* check lock status */
	return regmap_read_poll_timeout_atomic(common->lock_regmap,
					       pll->config.reg_lock,
					       tmp,
					       tmp & pll->config.mask_lock,
					       PLL_DELAY_US, PLL_TIMEOUT_US);
}

static void ccu_pll_disable(struct clk_hw *hw)
{
	struct ccu_common *common = hw_to_ccu_common(hw);

	ccu_update(common, swcr3, PLL_SWCR3_EN, 0);
}

/*
 * PLLs must be gated before changing rate, which is ensured by
 * flag CLK_SET_RATE_GATE.
 */
static int ccu_pll_set_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long parent_rate)
{
	struct ccu_pll *pll = hw_to_ccu_pll(hw);
	const struct ccu_pll_rate_tbl *entry;

	entry = ccu_pll_lookup_best_rate(pll, rate);
	ccu_pll_update_param(pll, entry);

	return 0;
}

static unsigned long ccu_pll_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct ccu_pll *pll = hw_to_ccu_pll(hw);
	const struct ccu_pll_rate_tbl *entry;

	entry = ccu_pll_lookup_matched_entry(pll);

	WARN_ON_ONCE(!entry);

	return entry ? entry->rate : 0;
}

static int ccu_pll_determine_rate(struct clk_hw *hw,
				  struct clk_rate_request *req)
{
	struct ccu_pll *pll = hw_to_ccu_pll(hw);

	req->rate = ccu_pll_lookup_best_rate(pll, req->rate)->rate;

	return 0;
}

static int ccu_pll_init(struct clk_hw *hw)
{
	struct ccu_pll *pll = hw_to_ccu_pll(hw);

	if (ccu_pll_lookup_matched_entry(pll))
		return 0;

	ccu_pll_disable(hw);
	ccu_pll_update_param(pll, &pll->config.rate_tbl[0]);

	return 0;
}

const struct clk_ops spacemit_ccu_pll_ops = {
	.init		= ccu_pll_init,
	.enable		= ccu_pll_enable,
	.disable	= ccu_pll_disable,
	.set_rate	= ccu_pll_set_rate,
	.recalc_rate	= ccu_pll_recalc_rate,
	.determine_rate = ccu_pll_determine_rate,
	.is_enabled	= ccu_pll_is_enabled,
};
