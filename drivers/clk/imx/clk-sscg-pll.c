// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Copyright 2018 NXP.
 *
 * This driver supports the SCCG plls found in the imx8m SOCs
 *
 * Documentation for this SCCG pll can be found at:
 *   https://www.nxp.com/docs/en/reference-manual/IMX8MDQLQRM.pdf#page=834
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/slab.h>
#include <linux/bitfield.h>

#include "clk.h"

/* PLL CFGs */
#define PLL_CFG0		0x0
#define PLL_CFG1		0x4
#define PLL_CFG2		0x8

#define PLL_DIVF1_MASK		GENMASK(18, 13)
#define PLL_DIVF2_MASK		GENMASK(12, 7)
#define PLL_DIVR1_MASK		GENMASK(27, 25)
#define PLL_DIVR2_MASK		GENMASK(24, 19)
#define PLL_DIVQ_MASK           GENMASK(6, 1)
#define PLL_REF_MASK		GENMASK(2, 0)

#define PLL_LOCK_MASK		BIT(31)
#define PLL_PD_MASK		BIT(7)

/* These are the specification limits for the SSCG PLL */
#define PLL_REF_MIN_FREQ		25000000UL
#define PLL_REF_MAX_FREQ		235000000UL

#define PLL_STAGE1_MIN_FREQ		1600000000UL
#define PLL_STAGE1_MAX_FREQ		2400000000UL

#define PLL_STAGE1_REF_MIN_FREQ		25000000UL
#define PLL_STAGE1_REF_MAX_FREQ		54000000UL

#define PLL_STAGE2_MIN_FREQ		1200000000UL
#define PLL_STAGE2_MAX_FREQ		2400000000UL

#define PLL_STAGE2_REF_MIN_FREQ		54000000UL
#define PLL_STAGE2_REF_MAX_FREQ		75000000UL

#define PLL_OUT_MIN_FREQ		20000000UL
#define PLL_OUT_MAX_FREQ		1200000000UL

#define PLL_DIVR1_MAX			7
#define PLL_DIVR2_MAX			63
#define PLL_DIVF1_MAX			63
#define PLL_DIVF2_MAX			63
#define PLL_DIVQ_MAX			63

#define PLL_BYPASS_NONE			0x0
#define PLL_BYPASS1			0x2
#define PLL_BYPASS2			0x1

#define SSCG_PLL_BYPASS1_MASK           BIT(5)
#define SSCG_PLL_BYPASS2_MASK           BIT(4)
#define SSCG_PLL_BYPASS_MASK		GENMASK(5, 4)

#define PLL_SCCG_LOCK_TIMEOUT		70

struct clk_sscg_pll_setup {
	int divr1, divf1;
	int divr2, divf2;
	int divq;
	int bypass;

	uint64_t vco1;
	uint64_t vco2;
	uint64_t fout;
	uint64_t ref;
	uint64_t ref_div1;
	uint64_t ref_div2;
	uint64_t fout_request;
	int fout_error;
};

struct clk_sscg_pll {
	struct clk_hw	hw;
	const struct clk_ops  ops;

	void __iomem *base;

	struct clk_sscg_pll_setup setup;

	u8 parent;
	u8 bypass1;
	u8 bypass2;
};

#define to_clk_sscg_pll(_hw) container_of(_hw, struct clk_sscg_pll, hw)

static int clk_sscg_pll_wait_lock(struct clk_sscg_pll *pll)
{
	u32 val;

	val = readl_relaxed(pll->base + PLL_CFG0);

	/* don't wait for lock if all plls are bypassed */
	if (!(val & SSCG_PLL_BYPASS2_MASK))
		return readl_poll_timeout(pll->base, val, val & PLL_LOCK_MASK,
						0, PLL_SCCG_LOCK_TIMEOUT);

	return 0;
}

static int clk_sscg_pll2_check_match(struct clk_sscg_pll_setup *setup,
					struct clk_sscg_pll_setup *temp_setup)
{
	int new_diff = temp_setup->fout - temp_setup->fout_request;
	int diff = temp_setup->fout_error;

	if (abs(diff) > abs(new_diff)) {
		temp_setup->fout_error = new_diff;
		memcpy(setup, temp_setup, sizeof(struct clk_sscg_pll_setup));

		if (temp_setup->fout_request == temp_setup->fout)
			return 0;
	}
	return -1;
}

static int clk_sscg_divq_lookup(struct clk_sscg_pll_setup *setup,
				struct clk_sscg_pll_setup *temp_setup)
{
	int ret = -EINVAL;

	for (temp_setup->divq = 0; temp_setup->divq <= PLL_DIVQ_MAX;
	     temp_setup->divq++) {
		temp_setup->vco2 = temp_setup->vco1;
		do_div(temp_setup->vco2, temp_setup->divr2 + 1);
		temp_setup->vco2 *= 2;
		temp_setup->vco2 *= temp_setup->divf2 + 1;
		if (temp_setup->vco2 >= PLL_STAGE2_MIN_FREQ &&
				temp_setup->vco2 <= PLL_STAGE2_MAX_FREQ) {
			temp_setup->fout = temp_setup->vco2;
			do_div(temp_setup->fout, 2 * (temp_setup->divq + 1));

			ret = clk_sscg_pll2_check_match(setup, temp_setup);
			if (!ret) {
				temp_setup->bypass = PLL_BYPASS1;
				return ret;
			}
		}
	}

	return ret;
}

static int clk_sscg_divf2_lookup(struct clk_sscg_pll_setup *setup,
					struct clk_sscg_pll_setup *temp_setup)
{
	int ret = -EINVAL;

	for (temp_setup->divf2 = 0; temp_setup->divf2 <= PLL_DIVF2_MAX;
	     temp_setup->divf2++) {
		ret = clk_sscg_divq_lookup(setup, temp_setup);
		if (!ret)
			return ret;
	}

	return ret;
}

static int clk_sscg_divr2_lookup(struct clk_sscg_pll_setup *setup,
				struct clk_sscg_pll_setup *temp_setup)
{
	int ret = -EINVAL;

	for (temp_setup->divr2 = 0; temp_setup->divr2 <= PLL_DIVR2_MAX;
	     temp_setup->divr2++) {
		temp_setup->ref_div2 = temp_setup->vco1;
		do_div(temp_setup->ref_div2, temp_setup->divr2 + 1);
		if (temp_setup->ref_div2 >= PLL_STAGE2_REF_MIN_FREQ &&
		    temp_setup->ref_div2 <= PLL_STAGE2_REF_MAX_FREQ) {
			ret = clk_sscg_divf2_lookup(setup, temp_setup);
			if (!ret)
				return ret;
		}
	}

	return ret;
}

static int clk_sscg_pll2_find_setup(struct clk_sscg_pll_setup *setup,
					struct clk_sscg_pll_setup *temp_setup,
					uint64_t ref)
{

	int ret;

	if (ref < PLL_STAGE1_MIN_FREQ || ref > PLL_STAGE1_MAX_FREQ)
		return -EINVAL;

	temp_setup->vco1 = ref;

	ret = clk_sscg_divr2_lookup(setup, temp_setup);
	return ret;
}

static int clk_sscg_divf1_lookup(struct clk_sscg_pll_setup *setup,
				struct clk_sscg_pll_setup *temp_setup)
{
	int ret = -EINVAL;

	for (temp_setup->divf1 = 0; temp_setup->divf1 <= PLL_DIVF1_MAX;
	     temp_setup->divf1++) {
		uint64_t vco1 = temp_setup->ref;

		do_div(vco1, temp_setup->divr1 + 1);
		vco1 *= 2;
		vco1 *= temp_setup->divf1 + 1;

		ret = clk_sscg_pll2_find_setup(setup, temp_setup, vco1);
		if (!ret) {
			temp_setup->bypass = PLL_BYPASS_NONE;
			return ret;
		}
	}

	return ret;
}

static int clk_sscg_divr1_lookup(struct clk_sscg_pll_setup *setup,
				struct clk_sscg_pll_setup *temp_setup)
{
	int ret = -EINVAL;

	for (temp_setup->divr1 = 0; temp_setup->divr1 <= PLL_DIVR1_MAX;
	     temp_setup->divr1++) {
		temp_setup->ref_div1 = temp_setup->ref;
		do_div(temp_setup->ref_div1, temp_setup->divr1 + 1);
		if (temp_setup->ref_div1 >= PLL_STAGE1_REF_MIN_FREQ &&
		    temp_setup->ref_div1 <= PLL_STAGE1_REF_MAX_FREQ) {
			ret = clk_sscg_divf1_lookup(setup, temp_setup);
			if (!ret)
				return ret;
		}
	}

	return ret;
}

static int clk_sscg_pll1_find_setup(struct clk_sscg_pll_setup *setup,
					struct clk_sscg_pll_setup *temp_setup,
					uint64_t ref)
{

	int ret;

	if (ref < PLL_REF_MIN_FREQ || ref > PLL_REF_MAX_FREQ)
		return -EINVAL;

	temp_setup->ref = ref;

	ret = clk_sscg_divr1_lookup(setup, temp_setup);

	return ret;
}

static int clk_sscg_pll_find_setup(struct clk_sscg_pll_setup *setup,
					uint64_t prate,
					uint64_t rate, int try_bypass)
{
	struct clk_sscg_pll_setup temp_setup;
	int ret = -EINVAL;

	memset(&temp_setup, 0, sizeof(struct clk_sscg_pll_setup));
	memset(setup, 0, sizeof(struct clk_sscg_pll_setup));

	temp_setup.fout_error = PLL_OUT_MAX_FREQ;
	temp_setup.fout_request = rate;

	switch (try_bypass) {

	case PLL_BYPASS2:
		if (prate == rate) {
			setup->bypass = PLL_BYPASS2;
			setup->fout = rate;
			ret = 0;
		}
		break;

	case PLL_BYPASS1:
		ret = clk_sscg_pll2_find_setup(setup, &temp_setup, prate);
		break;

	case PLL_BYPASS_NONE:
		ret = clk_sscg_pll1_find_setup(setup, &temp_setup, prate);
		break;
	}

	return ret;
}


static int clk_sscg_pll_is_prepared(struct clk_hw *hw)
{
	struct clk_sscg_pll *pll = to_clk_sscg_pll(hw);

	u32 val = readl_relaxed(pll->base + PLL_CFG0);

	return (val & PLL_PD_MASK) ? 0 : 1;
}

static int clk_sscg_pll_prepare(struct clk_hw *hw)
{
	struct clk_sscg_pll *pll = to_clk_sscg_pll(hw);
	u32 val;

	val = readl_relaxed(pll->base + PLL_CFG0);
	val &= ~PLL_PD_MASK;
	writel_relaxed(val, pll->base + PLL_CFG0);

	return clk_sscg_pll_wait_lock(pll);
}

static void clk_sscg_pll_unprepare(struct clk_hw *hw)
{
	struct clk_sscg_pll *pll = to_clk_sscg_pll(hw);
	u32 val;

	val = readl_relaxed(pll->base + PLL_CFG0);
	val |= PLL_PD_MASK;
	writel_relaxed(val, pll->base + PLL_CFG0);
}

static unsigned long clk_sscg_pll_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct clk_sscg_pll *pll = to_clk_sscg_pll(hw);
	u32 val, divr1, divf1, divr2, divf2, divq;
	u64 temp64;

	val = readl_relaxed(pll->base + PLL_CFG2);
	divr1 = FIELD_GET(PLL_DIVR1_MASK, val);
	divr2 = FIELD_GET(PLL_DIVR2_MASK, val);
	divf1 = FIELD_GET(PLL_DIVF1_MASK, val);
	divf2 = FIELD_GET(PLL_DIVF2_MASK, val);
	divq = FIELD_GET(PLL_DIVQ_MASK, val);

	temp64 = parent_rate;

	val = readl(pll->base + PLL_CFG0);
	if (val & SSCG_PLL_BYPASS2_MASK) {
		temp64 = parent_rate;
	} else if (val & SSCG_PLL_BYPASS1_MASK) {
		temp64 *= divf2;
		do_div(temp64, (divr2 + 1) * (divq + 1));
	} else {
		temp64 *= 2;
		temp64 *= (divf1 + 1) * (divf2 + 1);
		do_div(temp64, (divr1 + 1) * (divr2 + 1) * (divq + 1));
	}

	return temp64;
}

static int clk_sscg_pll_set_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long parent_rate)
{
	struct clk_sscg_pll *pll = to_clk_sscg_pll(hw);
	struct clk_sscg_pll_setup *setup = &pll->setup;
	u32 val;

	/* set bypass here too since the parent might be the same */
	val = readl(pll->base + PLL_CFG0);
	val &= ~SSCG_PLL_BYPASS_MASK;
	val |= FIELD_PREP(SSCG_PLL_BYPASS_MASK, setup->bypass);
	writel(val, pll->base + PLL_CFG0);

	val = readl_relaxed(pll->base + PLL_CFG2);
	val &= ~(PLL_DIVF1_MASK | PLL_DIVF2_MASK);
	val &= ~(PLL_DIVR1_MASK | PLL_DIVR2_MASK | PLL_DIVQ_MASK);
	val |= FIELD_PREP(PLL_DIVF1_MASK, setup->divf1);
	val |= FIELD_PREP(PLL_DIVF2_MASK, setup->divf2);
	val |= FIELD_PREP(PLL_DIVR1_MASK, setup->divr1);
	val |= FIELD_PREP(PLL_DIVR2_MASK, setup->divr2);
	val |= FIELD_PREP(PLL_DIVQ_MASK, setup->divq);
	writel_relaxed(val, pll->base + PLL_CFG2);

	return clk_sscg_pll_wait_lock(pll);
}

static u8 clk_sscg_pll_get_parent(struct clk_hw *hw)
{
	struct clk_sscg_pll *pll = to_clk_sscg_pll(hw);
	u32 val;
	u8 ret = pll->parent;

	val = readl(pll->base + PLL_CFG0);
	if (val & SSCG_PLL_BYPASS2_MASK)
		ret = pll->bypass2;
	else if (val & SSCG_PLL_BYPASS1_MASK)
		ret = pll->bypass1;
	return ret;
}

static int clk_sscg_pll_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_sscg_pll *pll = to_clk_sscg_pll(hw);
	u32 val;

	val = readl(pll->base + PLL_CFG0);
	val &= ~SSCG_PLL_BYPASS_MASK;
	val |= FIELD_PREP(SSCG_PLL_BYPASS_MASK, pll->setup.bypass);
	writel(val, pll->base + PLL_CFG0);

	return clk_sscg_pll_wait_lock(pll);
}

static int __clk_sscg_pll_determine_rate(struct clk_hw *hw,
					struct clk_rate_request *req,
					uint64_t min,
					uint64_t max,
					uint64_t rate,
					int bypass)
{
	struct clk_sscg_pll *pll = to_clk_sscg_pll(hw);
	struct clk_sscg_pll_setup *setup = &pll->setup;
	struct clk_hw *parent_hw = NULL;
	int bypass_parent_index;
	int ret;

	req->max_rate = max;
	req->min_rate = min;

	switch (bypass) {
	case PLL_BYPASS2:
		bypass_parent_index = pll->bypass2;
		break;
	case PLL_BYPASS1:
		bypass_parent_index = pll->bypass1;
		break;
	default:
		bypass_parent_index = pll->parent;
		break;
	}

	parent_hw = clk_hw_get_parent_by_index(hw, bypass_parent_index);
	ret = __clk_determine_rate(parent_hw, req);
	if (!ret) {
		ret = clk_sscg_pll_find_setup(setup, req->rate,
						rate, bypass);
	}

	req->best_parent_hw = parent_hw;
	req->best_parent_rate = req->rate;
	req->rate = setup->fout;

	return ret;
}

static int clk_sscg_pll_determine_rate(struct clk_hw *hw,
				       struct clk_rate_request *req)
{
	struct clk_sscg_pll *pll = to_clk_sscg_pll(hw);
	struct clk_sscg_pll_setup *setup = &pll->setup;
	uint64_t rate = req->rate;
	uint64_t min = req->min_rate;
	uint64_t max = req->max_rate;
	int ret;

	if (rate < PLL_OUT_MIN_FREQ || rate > PLL_OUT_MAX_FREQ)
		return -EINVAL;

	ret = __clk_sscg_pll_determine_rate(hw, req, req->rate, req->rate,
						rate, PLL_BYPASS2);
	if (!ret)
		return ret;

	ret = __clk_sscg_pll_determine_rate(hw, req, PLL_STAGE1_REF_MIN_FREQ,
						PLL_STAGE1_REF_MAX_FREQ, rate,
						PLL_BYPASS1);
	if (!ret)
		return ret;

	ret = __clk_sscg_pll_determine_rate(hw, req, PLL_REF_MIN_FREQ,
						PLL_REF_MAX_FREQ, rate,
						PLL_BYPASS_NONE);
	if (!ret)
		return ret;

	if (setup->fout >= min && setup->fout <= max)
		ret = 0;

	return ret;
}

static const struct clk_ops clk_sscg_pll_ops = {
	.prepare	= clk_sscg_pll_prepare,
	.unprepare	= clk_sscg_pll_unprepare,
	.is_prepared	= clk_sscg_pll_is_prepared,
	.recalc_rate	= clk_sscg_pll_recalc_rate,
	.set_rate	= clk_sscg_pll_set_rate,
	.set_parent	= clk_sscg_pll_set_parent,
	.get_parent	= clk_sscg_pll_get_parent,
	.determine_rate	= clk_sscg_pll_determine_rate,
};

struct clk_hw *imx_clk_hw_sscg_pll(const char *name,
				const char * const *parent_names,
				u8 num_parents,
				u8 parent, u8 bypass1, u8 bypass2,
				void __iomem *base,
				unsigned long flags)
{
	struct clk_sscg_pll *pll;
	struct clk_init_data init;
	struct clk_hw *hw;
	int ret;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	pll->parent = parent;
	pll->bypass1 = bypass1;
	pll->bypass2 = bypass2;

	pll->base = base;
	init.name = name;
	init.ops = &clk_sscg_pll_ops;

	init.flags = flags;
	init.parent_names = parent_names;
	init.num_parents = num_parents;

	pll->base = base;
	pll->hw.init = &init;

	hw = &pll->hw;

	ret = clk_hw_register(NULL, hw);
	if (ret) {
		kfree(pll);
		return ERR_PTR(ret);
	}

	return hw;
}
