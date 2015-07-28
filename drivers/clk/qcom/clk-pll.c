/*
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
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

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/bug.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/clk-provider.h>
#include <linux/regmap.h>

#include <asm/div64.h>

#include "clk-pll.h"

#define PLL_OUTCTRL		BIT(0)
#define PLL_BYPASSNL		BIT(1)
#define PLL_RESET_N		BIT(2)
#define PLL_LOCK_COUNT_SHIFT	8
#define PLL_LOCK_COUNT_MASK	0x3f
#define PLL_BIAS_COUNT_SHIFT	14
#define PLL_BIAS_COUNT_MASK	0x3f
#define PLL_VOTE_FSM_ENA	BIT(20)
#define PLL_VOTE_FSM_RESET	BIT(21)

static int clk_pll_enable(struct clk_hw *hw)
{
	struct clk_pll *pll = to_clk_pll(hw);
	int ret;
	u32 mask, val;

	mask = PLL_OUTCTRL | PLL_RESET_N | PLL_BYPASSNL;
	ret = regmap_read(pll->clkr.regmap, pll->mode_reg, &val);
	if (ret)
		return ret;

	/* Skip if already enabled or in FSM mode */
	if ((val & mask) == mask || val & PLL_VOTE_FSM_ENA)
		return 0;

	/* Disable PLL bypass mode. */
	ret = regmap_update_bits(pll->clkr.regmap, pll->mode_reg, PLL_BYPASSNL,
				 PLL_BYPASSNL);
	if (ret)
		return ret;

	/*
	 * H/W requires a 5us delay between disabling the bypass and
	 * de-asserting the reset. Delay 10us just to be safe.
	 */
	udelay(10);

	/* De-assert active-low PLL reset. */
	ret = regmap_update_bits(pll->clkr.regmap, pll->mode_reg, PLL_RESET_N,
				 PLL_RESET_N);
	if (ret)
		return ret;

	/* Wait until PLL is locked. */
	udelay(50);

	/* Enable PLL output. */
	return regmap_update_bits(pll->clkr.regmap, pll->mode_reg, PLL_OUTCTRL,
				 PLL_OUTCTRL);
}

static void clk_pll_disable(struct clk_hw *hw)
{
	struct clk_pll *pll = to_clk_pll(hw);
	u32 mask;
	u32 val;

	regmap_read(pll->clkr.regmap, pll->mode_reg, &val);
	/* Skip if in FSM mode */
	if (val & PLL_VOTE_FSM_ENA)
		return;
	mask = PLL_OUTCTRL | PLL_RESET_N | PLL_BYPASSNL;
	regmap_update_bits(pll->clkr.regmap, pll->mode_reg, mask, 0);
}

static unsigned long
clk_pll_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct clk_pll *pll = to_clk_pll(hw);
	u32 l, m, n, config;
	unsigned long rate;
	u64 tmp;

	regmap_read(pll->clkr.regmap, pll->l_reg, &l);
	regmap_read(pll->clkr.regmap, pll->m_reg, &m);
	regmap_read(pll->clkr.regmap, pll->n_reg, &n);

	l &= 0x3ff;
	m &= 0x7ffff;
	n &= 0x7ffff;

	rate = parent_rate * l;
	if (n) {
		tmp = parent_rate;
		tmp *= m;
		do_div(tmp, n);
		rate += tmp;
	}
	if (pll->post_div_width) {
		regmap_read(pll->clkr.regmap, pll->config_reg, &config);
		config >>= pll->post_div_shift;
		config &= BIT(pll->post_div_width) - 1;
		rate /= config + 1;
	}

	return rate;
}

static const
struct pll_freq_tbl *find_freq(const struct pll_freq_tbl *f, unsigned long rate)
{
	if (!f)
		return NULL;

	for (; f->freq; f++)
		if (rate <= f->freq)
			return f;

	return NULL;
}

static int
clk_pll_determine_rate(struct clk_hw *hw, struct clk_rate_request *req)
{
	struct clk *parent = __clk_get_parent(hw->clk);
	struct clk_pll *pll = to_clk_pll(hw);
	const struct pll_freq_tbl *f;

	req->best_parent_hw = __clk_get_hw(parent);
	req->best_parent_rate = __clk_get_rate(parent);

	f = find_freq(pll->freq_tbl, req->rate);
	if (!f)
		req->rate = clk_pll_recalc_rate(hw, req->best_parent_rate);
	else
		req->rate = f->freq;

	return 0;
}

static int
clk_pll_set_rate(struct clk_hw *hw, unsigned long rate, unsigned long p_rate)
{
	struct clk_pll *pll = to_clk_pll(hw);
	const struct pll_freq_tbl *f;
	bool enabled;
	u32 mode;
	u32 enable_mask = PLL_OUTCTRL | PLL_BYPASSNL | PLL_RESET_N;

	f = find_freq(pll->freq_tbl, rate);
	if (!f)
		return -EINVAL;

	regmap_read(pll->clkr.regmap, pll->mode_reg, &mode);
	enabled = (mode & enable_mask) == enable_mask;

	if (enabled)
		clk_pll_disable(hw);

	regmap_update_bits(pll->clkr.regmap, pll->l_reg, 0x3ff, f->l);
	regmap_update_bits(pll->clkr.regmap, pll->m_reg, 0x7ffff, f->m);
	regmap_update_bits(pll->clkr.regmap, pll->n_reg, 0x7ffff, f->n);
	regmap_write(pll->clkr.regmap, pll->config_reg, f->ibits);

	if (enabled)
		clk_pll_enable(hw);

	return 0;
}

const struct clk_ops clk_pll_ops = {
	.enable = clk_pll_enable,
	.disable = clk_pll_disable,
	.recalc_rate = clk_pll_recalc_rate,
	.determine_rate = clk_pll_determine_rate,
	.set_rate = clk_pll_set_rate,
};
EXPORT_SYMBOL_GPL(clk_pll_ops);

static int wait_for_pll(struct clk_pll *pll)
{
	u32 val;
	int count;
	int ret;
	const char *name = __clk_get_name(pll->clkr.hw.clk);

	/* Wait for pll to enable. */
	for (count = 200; count > 0; count--) {
		ret = regmap_read(pll->clkr.regmap, pll->status_reg, &val);
		if (ret)
			return ret;
		if (val & BIT(pll->status_bit))
			return 0;
		udelay(1);
	}

	WARN(1, "%s didn't enable after voting for it!\n", name);
	return -ETIMEDOUT;
}

static int clk_pll_vote_enable(struct clk_hw *hw)
{
	int ret;
	struct clk_pll *p = to_clk_pll(__clk_get_hw(__clk_get_parent(hw->clk)));

	ret = clk_enable_regmap(hw);
	if (ret)
		return ret;

	return wait_for_pll(p);
}

const struct clk_ops clk_pll_vote_ops = {
	.enable = clk_pll_vote_enable,
	.disable = clk_disable_regmap,
};
EXPORT_SYMBOL_GPL(clk_pll_vote_ops);

static void
clk_pll_set_fsm_mode(struct clk_pll *pll, struct regmap *regmap, u8 lock_count)
{
	u32 val;
	u32 mask;

	/* De-assert reset to FSM */
	regmap_update_bits(regmap, pll->mode_reg, PLL_VOTE_FSM_RESET, 0);

	/* Program bias count and lock count */
	val = 1 << PLL_BIAS_COUNT_SHIFT | lock_count << PLL_LOCK_COUNT_SHIFT;
	mask = PLL_BIAS_COUNT_MASK << PLL_BIAS_COUNT_SHIFT;
	mask |= PLL_LOCK_COUNT_MASK << PLL_LOCK_COUNT_SHIFT;
	regmap_update_bits(regmap, pll->mode_reg, mask, val);

	/* Enable PLL FSM voting */
	regmap_update_bits(regmap, pll->mode_reg, PLL_VOTE_FSM_ENA,
		PLL_VOTE_FSM_ENA);
}

static void clk_pll_configure(struct clk_pll *pll, struct regmap *regmap,
	const struct pll_config *config)
{
	u32 val;
	u32 mask;

	regmap_write(regmap, pll->l_reg, config->l);
	regmap_write(regmap, pll->m_reg, config->m);
	regmap_write(regmap, pll->n_reg, config->n);

	val = config->vco_val;
	val |= config->pre_div_val;
	val |= config->post_div_val;
	val |= config->mn_ena_mask;
	val |= config->main_output_mask;
	val |= config->aux_output_mask;

	mask = config->vco_mask;
	mask |= config->pre_div_mask;
	mask |= config->post_div_mask;
	mask |= config->mn_ena_mask;
	mask |= config->main_output_mask;
	mask |= config->aux_output_mask;

	regmap_update_bits(regmap, pll->config_reg, mask, val);
}

void clk_pll_configure_sr(struct clk_pll *pll, struct regmap *regmap,
		const struct pll_config *config, bool fsm_mode)
{
	clk_pll_configure(pll, regmap, config);
	if (fsm_mode)
		clk_pll_set_fsm_mode(pll, regmap, 8);
}
EXPORT_SYMBOL_GPL(clk_pll_configure_sr);

void clk_pll_configure_sr_hpm_lp(struct clk_pll *pll, struct regmap *regmap,
		const struct pll_config *config, bool fsm_mode)
{
	clk_pll_configure(pll, regmap, config);
	if (fsm_mode)
		clk_pll_set_fsm_mode(pll, regmap, 0);
}
EXPORT_SYMBOL_GPL(clk_pll_configure_sr_hpm_lp);

static int clk_pll_sr2_enable(struct clk_hw *hw)
{
	struct clk_pll *pll = to_clk_pll(hw);
	int ret;
	u32 mode;

	ret = regmap_read(pll->clkr.regmap, pll->mode_reg, &mode);
	if (ret)
		return ret;

	/* Disable PLL bypass mode. */
	ret = regmap_update_bits(pll->clkr.regmap, pll->mode_reg, PLL_BYPASSNL,
				 PLL_BYPASSNL);
	if (ret)
		return ret;

	/*
	 * H/W requires a 5us delay between disabling the bypass and
	 * de-asserting the reset. Delay 10us just to be safe.
	 */
	udelay(10);

	/* De-assert active-low PLL reset. */
	ret = regmap_update_bits(pll->clkr.regmap, pll->mode_reg, PLL_RESET_N,
				 PLL_RESET_N);
	if (ret)
		return ret;

	ret = wait_for_pll(pll);
	if (ret)
		return ret;

	/* Enable PLL output. */
	return regmap_update_bits(pll->clkr.regmap, pll->mode_reg, PLL_OUTCTRL,
				 PLL_OUTCTRL);
}

static int
clk_pll_sr2_set_rate(struct clk_hw *hw, unsigned long rate, unsigned long prate)
{
	struct clk_pll *pll = to_clk_pll(hw);
	const struct pll_freq_tbl *f;
	bool enabled;
	u32 mode;
	u32 enable_mask = PLL_OUTCTRL | PLL_BYPASSNL | PLL_RESET_N;

	f = find_freq(pll->freq_tbl, rate);
	if (!f)
		return -EINVAL;

	regmap_read(pll->clkr.regmap, pll->mode_reg, &mode);
	enabled = (mode & enable_mask) == enable_mask;

	if (enabled)
		clk_pll_disable(hw);

	regmap_update_bits(pll->clkr.regmap, pll->l_reg, 0x3ff, f->l);
	regmap_update_bits(pll->clkr.regmap, pll->m_reg, 0x7ffff, f->m);
	regmap_update_bits(pll->clkr.regmap, pll->n_reg, 0x7ffff, f->n);

	if (enabled)
		clk_pll_sr2_enable(hw);

	return 0;
}

const struct clk_ops clk_pll_sr2_ops = {
	.enable = clk_pll_sr2_enable,
	.disable = clk_pll_disable,
	.set_rate = clk_pll_sr2_set_rate,
	.recalc_rate = clk_pll_recalc_rate,
	.determine_rate = clk_pll_determine_rate,
};
EXPORT_SYMBOL_GPL(clk_pll_sr2_ops);
