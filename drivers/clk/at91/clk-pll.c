// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2013 Boris BREZILLON <b.brezillon@overkiz.com>
 */

#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/clk/at91_pmc.h>
#include <linux/of.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include "pmc.h"

#define PLL_STATUS_MASK(id)	(1 << (1 + (id)))
#define PLL_REG(id)		(AT91_CKGR_PLLAR + ((id) * 4))
#define PLL_DIV_MASK		0xff
#define PLL_DIV_MAX		PLL_DIV_MASK
#define PLL_DIV(reg)		((reg) & PLL_DIV_MASK)
#define PLL_MUL(reg, layout)	(((reg) >> (layout)->mul_shift) & \
				 (layout)->mul_mask)
#define PLL_MUL_MIN		2
#define PLL_MUL_MASK(layout)	((layout)->mul_mask)
#define PLL_MUL_MAX(layout)	(PLL_MUL_MASK(layout) + 1)
#define PLL_ICPR_SHIFT(id)	((id) * 16)
#define PLL_ICPR_MASK(id)	(0xffff << PLL_ICPR_SHIFT(id))
#define PLL_MAX_COUNT		0x3f
#define PLL_COUNT_SHIFT		8
#define PLL_OUT_SHIFT		14
#define PLL_MAX_ID		1

#define to_clk_pll(hw) container_of(hw, struct clk_pll, hw)

struct clk_pll {
	struct clk_hw hw;
	struct regmap *regmap;
	u8 id;
	u8 div;
	u8 range;
	u16 mul;
	const struct clk_pll_layout *layout;
	const struct clk_pll_characteristics *characteristics;
	struct at91_clk_pms pms;
};

static inline bool clk_pll_ready(struct regmap *regmap, int id)
{
	unsigned int status;

	regmap_read(regmap, AT91_PMC_SR, &status);

	return status & PLL_STATUS_MASK(id) ? 1 : 0;
}

static int clk_pll_prepare(struct clk_hw *hw)
{
	struct clk_pll *pll = to_clk_pll(hw);
	struct regmap *regmap = pll->regmap;
	const struct clk_pll_layout *layout = pll->layout;
	const struct clk_pll_characteristics *characteristics =
							pll->characteristics;
	u8 id = pll->id;
	u32 mask = PLL_STATUS_MASK(id);
	int offset = PLL_REG(id);
	u8 out = 0;
	unsigned int pllr;
	unsigned int status;
	u8 div;
	u16 mul;

	regmap_read(regmap, offset, &pllr);
	div = PLL_DIV(pllr);
	mul = PLL_MUL(pllr, layout);

	regmap_read(regmap, AT91_PMC_SR, &status);
	if ((status & mask) &&
	    (div == pll->div && mul == pll->mul))
		return 0;

	if (characteristics->out)
		out = characteristics->out[pll->range];

	if (characteristics->icpll)
		regmap_update_bits(regmap, AT91_PMC_PLLICPR, PLL_ICPR_MASK(id),
			characteristics->icpll[pll->range] << PLL_ICPR_SHIFT(id));

	regmap_update_bits(regmap, offset, layout->pllr_mask,
			pll->div | (PLL_MAX_COUNT << PLL_COUNT_SHIFT) |
			(out << PLL_OUT_SHIFT) |
			((pll->mul & layout->mul_mask) << layout->mul_shift));

	while (!clk_pll_ready(regmap, pll->id))
		cpu_relax();

	return 0;
}

static int clk_pll_is_prepared(struct clk_hw *hw)
{
	struct clk_pll *pll = to_clk_pll(hw);

	return clk_pll_ready(pll->regmap, pll->id);
}

static void clk_pll_unprepare(struct clk_hw *hw)
{
	struct clk_pll *pll = to_clk_pll(hw);
	unsigned int mask = pll->layout->pllr_mask;

	regmap_update_bits(pll->regmap, PLL_REG(pll->id), mask, ~mask);
}

static unsigned long clk_pll_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct clk_pll *pll = to_clk_pll(hw);

	if (!pll->div || !pll->mul)
		return 0;

	return (parent_rate / pll->div) * (pll->mul + 1);
}

static long clk_pll_get_best_div_mul(struct clk_pll *pll, unsigned long rate,
				     unsigned long parent_rate,
				     u32 *div, u32 *mul,
				     u32 *index) {
	const struct clk_pll_layout *layout = pll->layout;
	const struct clk_pll_characteristics *characteristics =
							pll->characteristics;
	unsigned long bestremainder = ULONG_MAX;
	unsigned long maxdiv, mindiv, tmpdiv;
	long bestrate = -ERANGE;
	unsigned long bestdiv;
	unsigned long bestmul;
	int i = 0;

	/* Check if parent_rate is a valid input rate */
	if (parent_rate < characteristics->input.min)
		return -ERANGE;

	/*
	 * Calculate minimum divider based on the minimum multiplier, the
	 * parent_rate and the requested rate.
	 * Should always be 2 according to the input and output characteristics
	 * of the PLL blocks.
	 */
	mindiv = (parent_rate * PLL_MUL_MIN) / rate;
	if (!mindiv)
		mindiv = 1;

	if (parent_rate > characteristics->input.max) {
		tmpdiv = DIV_ROUND_UP(parent_rate, characteristics->input.max);
		if (tmpdiv > PLL_DIV_MAX)
			return -ERANGE;

		if (tmpdiv > mindiv)
			mindiv = tmpdiv;
	}

	/*
	 * Calculate the maximum divider which is limited by PLL register
	 * layout (limited by the MUL or DIV field size).
	 */
	maxdiv = DIV_ROUND_UP(parent_rate * PLL_MUL_MAX(layout), rate);
	if (maxdiv > PLL_DIV_MAX)
		maxdiv = PLL_DIV_MAX;

	/*
	 * Iterate over the acceptable divider values to find the best
	 * divider/multiplier pair (the one that generates the closest
	 * rate to the requested one).
	 */
	for (tmpdiv = mindiv; tmpdiv <= maxdiv; tmpdiv++) {
		unsigned long remainder;
		unsigned long tmprate;
		unsigned long tmpmul;

		/*
		 * Calculate the multiplier associated with the current
		 * divider that provide the closest rate to the requested one.
		 */
		tmpmul = DIV_ROUND_CLOSEST(rate, parent_rate / tmpdiv);
		tmprate = (parent_rate / tmpdiv) * tmpmul;
		if (tmprate > rate)
			remainder = tmprate - rate;
		else
			remainder = rate - tmprate;

		/*
		 * Compare the remainder with the best remainder found until
		 * now and elect a new best multiplier/divider pair if the
		 * current remainder is smaller than the best one.
		 */
		if (remainder < bestremainder) {
			bestremainder = remainder;
			bestdiv = tmpdiv;
			bestmul = tmpmul;
			bestrate = tmprate;
		}

		/*
		 * We've found a perfect match!
		 * Stop searching now and use this multiplier/divider pair.
		 */
		if (!remainder)
			break;
	}

	/* We haven't found any multiplier/divider pair => return -ERANGE */
	if (bestrate < 0)
		return bestrate;

	/* Check if bestrate is a valid output rate  */
	for (i = 0; i < characteristics->num_output; i++) {
		if (bestrate >= characteristics->output[i].min &&
		    bestrate <= characteristics->output[i].max)
			break;
	}

	if (i >= characteristics->num_output)
		return -ERANGE;

	if (div)
		*div = bestdiv;
	if (mul)
		*mul = bestmul - 1;
	if (index)
		*index = i;

	return bestrate;
}

static int clk_pll_determine_rate(struct clk_hw *hw,
				  struct clk_rate_request *req)
{
	struct clk_pll *pll = to_clk_pll(hw);

	req->rate = clk_pll_get_best_div_mul(pll, req->rate, req->best_parent_rate,
					     NULL, NULL, NULL);

	return 0;
}

static int clk_pll_set_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long parent_rate)
{
	struct clk_pll *pll = to_clk_pll(hw);
	long ret;
	u32 div;
	u32 mul;
	u32 index;

	ret = clk_pll_get_best_div_mul(pll, rate, parent_rate,
				       &div, &mul, &index);
	if (ret < 0)
		return ret;

	pll->range = index;
	pll->div = div;
	pll->mul = mul;

	return 0;
}

static int clk_pll_save_context(struct clk_hw *hw)
{
	struct clk_pll *pll = to_clk_pll(hw);
	struct clk_hw *parent_hw = clk_hw_get_parent(hw);

	pll->pms.parent_rate = clk_hw_get_rate(parent_hw);
	pll->pms.rate = clk_pll_recalc_rate(&pll->hw, pll->pms.parent_rate);
	pll->pms.status = clk_pll_ready(pll->regmap, PLL_REG(pll->id));

	return 0;
}

static void clk_pll_restore_context(struct clk_hw *hw)
{
	struct clk_pll *pll = to_clk_pll(hw);
	unsigned long calc_rate;
	unsigned int pllr, pllr_out, pllr_count;
	u8 out = 0;

	if (pll->characteristics->out)
		out = pll->characteristics->out[pll->range];

	regmap_read(pll->regmap, PLL_REG(pll->id), &pllr);

	calc_rate = (pll->pms.parent_rate / PLL_DIV(pllr)) *
		     (PLL_MUL(pllr, pll->layout) + 1);
	pllr_count = (pllr >> PLL_COUNT_SHIFT) & PLL_MAX_COUNT;
	pllr_out = (pllr >> PLL_OUT_SHIFT) & out;

	if (pll->pms.rate != calc_rate ||
	    pll->pms.status != clk_pll_ready(pll->regmap, PLL_REG(pll->id)) ||
	    pllr_count != PLL_MAX_COUNT ||
	    (out && pllr_out != out))
		pr_warn("PLLAR was not configured properly by firmware\n");
}

static const struct clk_ops pll_ops = {
	.prepare = clk_pll_prepare,
	.unprepare = clk_pll_unprepare,
	.is_prepared = clk_pll_is_prepared,
	.recalc_rate = clk_pll_recalc_rate,
	.determine_rate = clk_pll_determine_rate,
	.set_rate = clk_pll_set_rate,
	.save_context = clk_pll_save_context,
	.restore_context = clk_pll_restore_context,
};

struct clk_hw * __init
at91_clk_register_pll(struct regmap *regmap, const char *name,
		      const char *parent_name, u8 id,
		      const struct clk_pll_layout *layout,
		      const struct clk_pll_characteristics *characteristics)
{
	struct clk_pll *pll;
	struct clk_hw *hw;
	struct clk_init_data init;
	int offset = PLL_REG(id);
	unsigned int pllr;
	int ret;

	if (id > PLL_MAX_ID)
		return ERR_PTR(-EINVAL);

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &pll_ops;
	init.parent_names = &parent_name;
	init.num_parents = 1;
	init.flags = CLK_SET_RATE_GATE;

	pll->id = id;
	pll->hw.init = &init;
	pll->layout = layout;
	pll->characteristics = characteristics;
	pll->regmap = regmap;
	regmap_read(regmap, offset, &pllr);
	pll->div = PLL_DIV(pllr);
	pll->mul = PLL_MUL(pllr, layout);

	hw = &pll->hw;
	ret = clk_hw_register(NULL, &pll->hw);
	if (ret) {
		kfree(pll);
		hw = ERR_PTR(ret);
	}

	return hw;
}


const struct clk_pll_layout at91rm9200_pll_layout = {
	.pllr_mask = 0x7FFFFFF,
	.mul_shift = 16,
	.mul_mask = 0x7FF,
};

const struct clk_pll_layout at91sam9g45_pll_layout = {
	.pllr_mask = 0xFFFFFF,
	.mul_shift = 16,
	.mul_mask = 0xFF,
};

const struct clk_pll_layout at91sam9g20_pllb_layout = {
	.pllr_mask = 0x3FFFFF,
	.mul_shift = 16,
	.mul_mask = 0x3F,
};

const struct clk_pll_layout sama5d3_pll_layout = {
	.pllr_mask = 0x1FFFFFF,
	.mul_shift = 18,
	.mul_mask = 0x7F,
};
