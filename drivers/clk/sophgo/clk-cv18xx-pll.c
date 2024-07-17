// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Inochi Amaoto <inochiama@outlook.com>
 */

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/limits.h>
#include <linux/spinlock.h>

#include "clk-cv18xx-pll.h"

static inline struct cv1800_clk_pll *hw_to_cv1800_clk_pll(struct clk_hw *hw)
{
	struct cv1800_clk_common *common = hw_to_cv1800_clk_common(hw);

	return container_of(common, struct cv1800_clk_pll, common);
}

static unsigned long ipll_calc_rate(unsigned long parent_rate,
				    unsigned long pre_div_sel,
				    unsigned long div_sel,
				    unsigned long post_div_sel)
{
	uint64_t rate = parent_rate;

	rate *= div_sel;
	do_div(rate, pre_div_sel * post_div_sel);

	return rate;
}

static unsigned long ipll_recalc_rate(struct clk_hw *hw,
				      unsigned long parent_rate)
{
	struct cv1800_clk_pll *pll = hw_to_cv1800_clk_pll(hw);
	u32 value;

	value = readl(pll->common.base + pll->pll_reg);

	return ipll_calc_rate(parent_rate,
			      PLL_GET_PRE_DIV_SEL(value),
			      PLL_GET_DIV_SEL(value),
			      PLL_GET_POST_DIV_SEL(value));
}

static int ipll_find_rate(const struct cv1800_clk_pll_limit *limit,
			  unsigned long prate, unsigned long *rate,
			  u32 *value)
{
	unsigned long best_rate = 0;
	unsigned long trate = *rate;
	unsigned long pre_div_sel = 0, div_sel = 0, post_div_sel = 0;
	unsigned long pre, div, post;
	u32 detected = *value;
	unsigned long tmp;

	for_each_pll_limit_range(pre, &limit->pre_div) {
		for_each_pll_limit_range(div, &limit->div) {
			for_each_pll_limit_range(post, &limit->post_div) {
				tmp = ipll_calc_rate(prate, pre, div, post);

				if (tmp > trate)
					continue;

				if ((trate - tmp) < (trate - best_rate)) {
					best_rate = tmp;
					pre_div_sel = pre;
					div_sel = div;
					post_div_sel = post;
				}
			}
		}
	}

	if (best_rate) {
		detected = PLL_SET_PRE_DIV_SEL(detected, pre_div_sel);
		detected = PLL_SET_POST_DIV_SEL(detected, post_div_sel);
		detected = PLL_SET_DIV_SEL(detected, div_sel);
		*value = detected;
		*rate = best_rate;
		return 0;
	}

	return -EINVAL;
}

static int ipll_determine_rate(struct clk_hw *hw, struct clk_rate_request *req)
{
	u32 val;
	struct cv1800_clk_pll *pll = hw_to_cv1800_clk_pll(hw);

	return ipll_find_rate(pll->pll_limit, req->best_parent_rate,
			      &req->rate, &val);
}

static void pll_get_mode_ctrl(unsigned long div_sel,
			      bool (*mode_ctrl_check)(unsigned long,
						      unsigned long,
						      unsigned long),
			      const struct cv1800_clk_pll_limit *limit,
			      u32 *value)
{
	unsigned long ictrl = 0, mode = 0;
	u32 detected = *value;

	for_each_pll_limit_range(mode, &limit->mode) {
		for_each_pll_limit_range(ictrl, &limit->ictrl) {
			if (mode_ctrl_check(div_sel, ictrl, mode)) {
				detected = PLL_SET_SEL_MODE(detected, mode);
				detected = PLL_SET_ICTRL(detected, ictrl);
				*value = detected;
				return;
			}
		}
	}
}

static bool ipll_check_mode_ctrl_restrict(unsigned long div_sel,
					  unsigned long ictrl,
					  unsigned long mode)
{
	unsigned long left_rest = 20 * div_sel;
	unsigned long right_rest = 35 * div_sel;
	unsigned long test = 184 * (1 + mode) * (1 + ictrl) / 2;

	return test > left_rest && test <= right_rest;
}

static int ipll_set_rate(struct clk_hw *hw, unsigned long rate,
			 unsigned long parent_rate)
{
	u32 regval, detected = 0;
	unsigned long flags;
	struct cv1800_clk_pll *pll = hw_to_cv1800_clk_pll(hw);

	ipll_find_rate(pll->pll_limit, parent_rate, &rate, &detected);
	pll_get_mode_ctrl(PLL_GET_DIV_SEL(detected),
			  ipll_check_mode_ctrl_restrict,
			  pll->pll_limit, &detected);

	spin_lock_irqsave(pll->common.lock, flags);

	regval = readl(pll->common.base + pll->pll_reg);
	regval = PLL_COPY_REG(regval, detected);

	writel(regval, pll->common.base + pll->pll_reg);

	spin_unlock_irqrestore(pll->common.lock, flags);

	cv1800_clk_wait_for_lock(&pll->common, pll->pll_status.reg,
			      BIT(pll->pll_status.shift));

	return 0;
}

static int pll_enable(struct clk_hw *hw)
{
	struct cv1800_clk_pll *pll = hw_to_cv1800_clk_pll(hw);

	return cv1800_clk_clearbit(&pll->common, &pll->pll_pwd);
}

static void pll_disable(struct clk_hw *hw)
{
	struct cv1800_clk_pll *pll = hw_to_cv1800_clk_pll(hw);

	cv1800_clk_setbit(&pll->common, &pll->pll_pwd);
}

static int pll_is_enable(struct clk_hw *hw)
{
	struct cv1800_clk_pll *pll = hw_to_cv1800_clk_pll(hw);

	return cv1800_clk_checkbit(&pll->common, &pll->pll_pwd) == 0;
}

const struct clk_ops cv1800_clk_ipll_ops = {
	.disable = pll_disable,
	.enable = pll_enable,
	.is_enabled = pll_is_enable,

	.recalc_rate = ipll_recalc_rate,
	.determine_rate = ipll_determine_rate,
	.set_rate = ipll_set_rate,
};

#define PLL_SYN_FACTOR_DOT_POS		26
#define PLL_SYN_FACTOR_MINIMUM		((4 << PLL_SYN_FACTOR_DOT_POS) + 1)

static bool fpll_is_factional_mode(struct cv1800_clk_pll *pll)
{
	return cv1800_clk_checkbit(&pll->common, &pll->pll_syn->en);
}

static unsigned long fpll_calc_rate(unsigned long parent_rate,
				    unsigned long pre_div_sel,
				    unsigned long div_sel,
				    unsigned long post_div_sel,
				    unsigned long ssc_syn_set,
				    bool is_full_parent)
{
	u64 dividend = parent_rate * div_sel;
	u64 factor = ssc_syn_set * pre_div_sel * post_div_sel;
	unsigned long rate;

	dividend <<= PLL_SYN_FACTOR_DOT_POS - 1;
	rate = div64_u64_rem(dividend, factor, &dividend);

	if (is_full_parent) {
		dividend <<= 1;
		rate <<= 1;
	}

	rate += DIV64_U64_ROUND_CLOSEST(dividend, factor);

	return rate;
}

static unsigned long fpll_recalc_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	struct cv1800_clk_pll *pll = hw_to_cv1800_clk_pll(hw);
	u32 value;
	bool clk_full;
	u32 syn_set;

	if (!fpll_is_factional_mode(pll))
		return ipll_recalc_rate(hw, parent_rate);

	syn_set = readl(pll->common.base + pll->pll_syn->set);

	if (syn_set == 0)
		return 0;

	clk_full = cv1800_clk_checkbit(&pll->common,
					  &pll->pll_syn->clk_half);

	value = readl(pll->common.base + pll->pll_reg);

	return fpll_calc_rate(parent_rate,
			      PLL_GET_PRE_DIV_SEL(value),
			      PLL_GET_DIV_SEL(value),
			      PLL_GET_POST_DIV_SEL(value),
			      syn_set, clk_full);
}

static unsigned long fpll_find_synthesizer(unsigned long parent,
					   unsigned long rate,
					   unsigned long pre_div,
					   unsigned long div,
					   unsigned long post_div,
					   bool is_full_parent,
					   u32 *ssc_syn_set)
{
	u32 test_max = U32_MAX, test_min = PLL_SYN_FACTOR_MINIMUM;
	unsigned long trate;

	while (test_min < test_max) {
		u32 tssc = (test_max + test_min) / 2;

		trate = fpll_calc_rate(parent, pre_div, div, post_div,
				       tssc, is_full_parent);

		if (trate == rate) {
			test_min = tssc;
			break;
		}

		if (trate > rate)
			test_min = tssc + 1;
		else
			test_max = tssc - 1;
	}

	if (trate != 0)
		*ssc_syn_set = test_min;

	return trate;
}

static int fpll_find_rate(struct cv1800_clk_pll *pll,
			  const struct cv1800_clk_pll_limit *limit,
			  unsigned long prate,
			  unsigned long *rate,
			  u32 *value, u32 *ssc_syn_set)
{
	unsigned long best_rate = 0;
	unsigned long pre_div_sel = 0, div_sel = 0, post_div_sel = 0;
	unsigned long pre, div, post;
	unsigned long trate = *rate;
	u32 detected = *value;
	unsigned long tmp;
	bool clk_full = cv1800_clk_checkbit(&pll->common,
					       &pll->pll_syn->clk_half);

	for_each_pll_limit_range(pre, &limit->pre_div) {
		for_each_pll_limit_range(post, &limit->post_div) {
			for_each_pll_limit_range(div, &limit->div) {
				tmp = fpll_find_synthesizer(prate, trate,
							    pre, div, post,
							    clk_full,
							    ssc_syn_set);

				if ((trate - tmp) < (trate - best_rate)) {
					best_rate = tmp;
					pre_div_sel = pre;
					div_sel = div;
					post_div_sel = post;
				}
			}
		}
	}

	if (best_rate) {
		detected = PLL_SET_PRE_DIV_SEL(detected, pre_div_sel);
		detected = PLL_SET_POST_DIV_SEL(detected, post_div_sel);
		detected = PLL_SET_DIV_SEL(detected, div_sel);
		*value = detected;
		*rate = best_rate;
		return 0;
	}

	return -EINVAL;
}

static int fpll_determine_rate(struct clk_hw *hw, struct clk_rate_request *req)
{
	struct cv1800_clk_pll *pll = hw_to_cv1800_clk_pll(hw);
	u32 val, ssc_syn_set;

	if (!fpll_is_factional_mode(pll))
		return ipll_determine_rate(hw, req);

	fpll_find_rate(pll, &pll->pll_limit[2], req->best_parent_rate,
		       &req->rate, &val, &ssc_syn_set);

	return 0;
}

static bool fpll_check_mode_ctrl_restrict(unsigned long div_sel,
					  unsigned long ictrl,
					  unsigned long mode)
{
	unsigned long left_rest = 10 * div_sel;
	unsigned long right_rest = 24 * div_sel;
	unsigned long test = 184 * (1 + mode) * (1 + ictrl) / 2;

	return test > left_rest && test <= right_rest;
}

static int fpll_set_rate(struct clk_hw *hw, unsigned long rate,
			 unsigned long parent_rate)
{
	u32 regval;
	u32 detected = 0, detected_ssc = 0;
	unsigned long flags;
	struct cv1800_clk_pll *pll = hw_to_cv1800_clk_pll(hw);

	if (!fpll_is_factional_mode(pll))
		return ipll_set_rate(hw, rate, parent_rate);

	fpll_find_rate(pll, &pll->pll_limit[2], parent_rate,
		       &rate, &detected, &detected_ssc);
	pll_get_mode_ctrl(PLL_GET_DIV_SEL(detected),
			  fpll_check_mode_ctrl_restrict,
			  pll->pll_limit, &detected);

	spin_lock_irqsave(pll->common.lock, flags);

	writel(detected_ssc, pll->common.base + pll->pll_syn->set);

	regval = readl(pll->common.base + pll->pll_reg);
	regval = PLL_COPY_REG(regval, detected);

	writel(regval, pll->common.base + pll->pll_reg);

	spin_unlock_irqrestore(pll->common.lock, flags);

	cv1800_clk_wait_for_lock(&pll->common, pll->pll_status.reg,
			      BIT(pll->pll_status.shift));

	return 0;
}

static u8 fpll_get_parent(struct clk_hw *hw)
{
	struct cv1800_clk_pll *pll = hw_to_cv1800_clk_pll(hw);

	if (fpll_is_factional_mode(pll))
		return 1;

	return 0;
}

static int fpll_set_parent(struct clk_hw *hw, u8 index)
{
	struct cv1800_clk_pll *pll = hw_to_cv1800_clk_pll(hw);

	if (index)
		cv1800_clk_setbit(&pll->common, &pll->pll_syn->en);
	else
		cv1800_clk_clearbit(&pll->common, &pll->pll_syn->en);

	return 0;
}

const struct clk_ops cv1800_clk_fpll_ops = {
	.disable = pll_disable,
	.enable = pll_enable,
	.is_enabled = pll_is_enable,

	.recalc_rate = fpll_recalc_rate,
	.determine_rate = fpll_determine_rate,
	.set_rate = fpll_set_rate,

	.set_parent = fpll_set_parent,
	.get_parent = fpll_get_parent,
};
