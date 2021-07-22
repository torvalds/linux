// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018, The Linux Foundation. All rights reserved.

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/regmap.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/clk-provider.h>
#include <linux/spinlock.h>

#include "clk-regmap.h"
#include "clk-hfpll.h"

#define PLL_OUTCTRL	BIT(0)
#define PLL_BYPASSNL	BIT(1)
#define PLL_RESET_N	BIT(2)

/* Initialize a HFPLL at a given rate and enable it. */
static void __clk_hfpll_init_once(struct clk_hw *hw)
{
	struct clk_hfpll *h = to_clk_hfpll(hw);
	struct hfpll_data const *hd = h->d;
	struct regmap *regmap = h->clkr.regmap;

	if (likely(h->init_done))
		return;

	/* Configure PLL parameters for integer mode. */
	if (hd->config_val)
		regmap_write(regmap, hd->config_reg, hd->config_val);
	regmap_write(regmap, hd->m_reg, 0);
	regmap_write(regmap, hd->n_reg, 1);

	if (hd->user_reg) {
		u32 regval = hd->user_val;
		unsigned long rate;

		rate = clk_hw_get_rate(hw);

		/* Pick the right VCO. */
		if (hd->user_vco_mask && rate > hd->low_vco_max_rate)
			regval |= hd->user_vco_mask;
		regmap_write(regmap, hd->user_reg, regval);
	}

	if (hd->droop_reg)
		regmap_write(regmap, hd->droop_reg, hd->droop_val);

	h->init_done = true;
}

static void __clk_hfpll_enable(struct clk_hw *hw)
{
	struct clk_hfpll *h = to_clk_hfpll(hw);
	struct hfpll_data const *hd = h->d;
	struct regmap *regmap = h->clkr.regmap;
	u32 val;

	__clk_hfpll_init_once(hw);

	/* Disable PLL bypass mode. */
	regmap_update_bits(regmap, hd->mode_reg, PLL_BYPASSNL, PLL_BYPASSNL);

	/*
	 * H/W requires a 5us delay between disabling the bypass and
	 * de-asserting the reset. Delay 10us just to be safe.
	 */
	udelay(10);

	/* De-assert active-low PLL reset. */
	regmap_update_bits(regmap, hd->mode_reg, PLL_RESET_N, PLL_RESET_N);

	/* Wait for PLL to lock. */
	if (hd->status_reg) {
		do {
			regmap_read(regmap, hd->status_reg, &val);
		} while (!(val & BIT(hd->lock_bit)));
	} else {
		udelay(60);
	}

	/* Enable PLL output. */
	regmap_update_bits(regmap, hd->mode_reg, PLL_OUTCTRL, PLL_OUTCTRL);
}

/* Enable an already-configured HFPLL. */
static int clk_hfpll_enable(struct clk_hw *hw)
{
	unsigned long flags;
	struct clk_hfpll *h = to_clk_hfpll(hw);
	struct hfpll_data const *hd = h->d;
	struct regmap *regmap = h->clkr.regmap;
	u32 mode;

	spin_lock_irqsave(&h->lock, flags);
	regmap_read(regmap, hd->mode_reg, &mode);
	if (!(mode & (PLL_BYPASSNL | PLL_RESET_N | PLL_OUTCTRL)))
		__clk_hfpll_enable(hw);
	spin_unlock_irqrestore(&h->lock, flags);

	return 0;
}

static void __clk_hfpll_disable(struct clk_hfpll *h)
{
	struct hfpll_data const *hd = h->d;
	struct regmap *regmap = h->clkr.regmap;

	/*
	 * Disable the PLL output, disable test mode, enable the bypass mode,
	 * and assert the reset.
	 */
	regmap_update_bits(regmap, hd->mode_reg,
			   PLL_BYPASSNL | PLL_RESET_N | PLL_OUTCTRL, 0);
}

static void clk_hfpll_disable(struct clk_hw *hw)
{
	struct clk_hfpll *h = to_clk_hfpll(hw);
	unsigned long flags;

	spin_lock_irqsave(&h->lock, flags);
	__clk_hfpll_disable(h);
	spin_unlock_irqrestore(&h->lock, flags);
}

static long clk_hfpll_round_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long *parent_rate)
{
	struct clk_hfpll *h = to_clk_hfpll(hw);
	struct hfpll_data const *hd = h->d;
	unsigned long rrate;

	rate = clamp(rate, hd->min_rate, hd->max_rate);

	rrate = DIV_ROUND_UP(rate, *parent_rate) * *parent_rate;
	if (rrate > hd->max_rate)
		rrate -= *parent_rate;

	return rrate;
}

/*
 * For optimization reasons, assumes no downstream clocks are actively using
 * it.
 */
static int clk_hfpll_set_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long parent_rate)
{
	struct clk_hfpll *h = to_clk_hfpll(hw);
	struct hfpll_data const *hd = h->d;
	struct regmap *regmap = h->clkr.regmap;
	unsigned long flags;
	u32 l_val, val;
	bool enabled;

	l_val = rate / parent_rate;

	spin_lock_irqsave(&h->lock, flags);

	enabled = __clk_is_enabled(hw->clk);
	if (enabled)
		__clk_hfpll_disable(h);

	/* Pick the right VCO. */
	if (hd->user_reg && hd->user_vco_mask) {
		regmap_read(regmap, hd->user_reg, &val);
		if (rate <= hd->low_vco_max_rate)
			val &= ~hd->user_vco_mask;
		else
			val |= hd->user_vco_mask;
		regmap_write(regmap, hd->user_reg, val);
	}

	regmap_write(regmap, hd->l_reg, l_val);

	if (enabled)
		__clk_hfpll_enable(hw);

	spin_unlock_irqrestore(&h->lock, flags);

	return 0;
}

static unsigned long clk_hfpll_recalc_rate(struct clk_hw *hw,
					   unsigned long parent_rate)
{
	struct clk_hfpll *h = to_clk_hfpll(hw);
	struct hfpll_data const *hd = h->d;
	struct regmap *regmap = h->clkr.regmap;
	u32 l_val;

	regmap_read(regmap, hd->l_reg, &l_val);

	return l_val * parent_rate;
}

static int clk_hfpll_init(struct clk_hw *hw)
{
	struct clk_hfpll *h = to_clk_hfpll(hw);
	struct hfpll_data const *hd = h->d;
	struct regmap *regmap = h->clkr.regmap;
	u32 mode, status;

	regmap_read(regmap, hd->mode_reg, &mode);
	if (mode != (PLL_BYPASSNL | PLL_RESET_N | PLL_OUTCTRL)) {
		__clk_hfpll_init_once(hw);
		return 0;
	}

	if (hd->status_reg) {
		regmap_read(regmap, hd->status_reg, &status);
		if (!(status & BIT(hd->lock_bit))) {
			WARN(1, "HFPLL %s is ON, but not locked!\n",
			     __clk_get_name(hw->clk));
			clk_hfpll_disable(hw);
			__clk_hfpll_init_once(hw);
		}
	}

	return 0;
}

static int hfpll_is_enabled(struct clk_hw *hw)
{
	struct clk_hfpll *h = to_clk_hfpll(hw);
	struct hfpll_data const *hd = h->d;
	struct regmap *regmap = h->clkr.regmap;
	u32 mode;

	regmap_read(regmap, hd->mode_reg, &mode);
	mode &= 0x7;
	return mode == (PLL_BYPASSNL | PLL_RESET_N | PLL_OUTCTRL);
}

const struct clk_ops clk_ops_hfpll = {
	.enable = clk_hfpll_enable,
	.disable = clk_hfpll_disable,
	.is_enabled = hfpll_is_enabled,
	.round_rate = clk_hfpll_round_rate,
	.set_rate = clk_hfpll_set_rate,
	.recalc_rate = clk_hfpll_recalc_rate,
	.init = clk_hfpll_init,
};
EXPORT_SYMBOL_GPL(clk_ops_hfpll);
