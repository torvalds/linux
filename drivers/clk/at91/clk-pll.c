/*
 *  Copyright (C) 2013 Boris BREZILLON <b.brezillon@overkiz.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
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

struct clk_pll_characteristics {
	struct clk_range input;
	int num_output;
	struct clk_range *output;
	u16 *icpll;
	u8 *out;
};

struct clk_pll_layout {
	u32 pllr_mask;
	u16 mul_mask;
	u8 mul_shift;
};

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

static long clk_pll_round_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long *parent_rate)
{
	struct clk_pll *pll = to_clk_pll(hw);

	return clk_pll_get_best_div_mul(pll, rate, *parent_rate,
					NULL, NULL, NULL);
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

static const struct clk_ops pll_ops = {
	.prepare = clk_pll_prepare,
	.unprepare = clk_pll_unprepare,
	.is_prepared = clk_pll_is_prepared,
	.recalc_rate = clk_pll_recalc_rate,
	.round_rate = clk_pll_round_rate,
	.set_rate = clk_pll_set_rate,
};

static struct clk_hw * __init
at91_clk_register_pll(struct regmap *regmap, const char *name,
		      const char *parent_name, u8 id,
		      const struct clk_pll_layout *layout,
		      const struct clk_pll_characteristics *characteristics)
{
	struct clk_pll *pll;
	struct clk_hw *hw;
	struct clk_init_data init = {};
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


static const struct clk_pll_layout at91rm9200_pll_layout = {
	.pllr_mask = 0x7FFFFFF,
	.mul_shift = 16,
	.mul_mask = 0x7FF,
};

static const struct clk_pll_layout at91sam9g45_pll_layout = {
	.pllr_mask = 0xFFFFFF,
	.mul_shift = 16,
	.mul_mask = 0xFF,
};

static const struct clk_pll_layout at91sam9g20_pllb_layout = {
	.pllr_mask = 0x3FFFFF,
	.mul_shift = 16,
	.mul_mask = 0x3F,
};

static const struct clk_pll_layout sama5d3_pll_layout = {
	.pllr_mask = 0x1FFFFFF,
	.mul_shift = 18,
	.mul_mask = 0x7F,
};


static struct clk_pll_characteristics * __init
of_at91_clk_pll_get_characteristics(struct device_node *np)
{
	int i;
	int offset;
	u32 tmp;
	int num_output;
	u32 num_cells;
	struct clk_range input;
	struct clk_range *output;
	u8 *out = NULL;
	u16 *icpll = NULL;
	struct clk_pll_characteristics *characteristics;

	if (of_at91_get_clk_range(np, "atmel,clk-input-range", &input))
		return NULL;

	if (of_property_read_u32(np, "#atmel,pll-clk-output-range-cells",
				 &num_cells))
		return NULL;

	if (num_cells < 2 || num_cells > 4)
		return NULL;

	if (!of_get_property(np, "atmel,pll-clk-output-ranges", &tmp))
		return NULL;
	num_output = tmp / (sizeof(u32) * num_cells);

	characteristics = kzalloc(sizeof(*characteristics), GFP_KERNEL);
	if (!characteristics)
		return NULL;

	output = kcalloc(num_output, sizeof(*output), GFP_KERNEL);
	if (!output)
		goto out_free_characteristics;

	if (num_cells > 2) {
		out = kcalloc(num_output, sizeof(*out), GFP_KERNEL);
		if (!out)
			goto out_free_output;
	}

	if (num_cells > 3) {
		icpll = kcalloc(num_output, sizeof(*icpll), GFP_KERNEL);
		if (!icpll)
			goto out_free_output;
	}

	for (i = 0; i < num_output; i++) {
		offset = i * num_cells;
		if (of_property_read_u32_index(np,
					       "atmel,pll-clk-output-ranges",
					       offset, &tmp))
			goto out_free_output;
		output[i].min = tmp;
		if (of_property_read_u32_index(np,
					       "atmel,pll-clk-output-ranges",
					       offset + 1, &tmp))
			goto out_free_output;
		output[i].max = tmp;

		if (num_cells == 2)
			continue;

		if (of_property_read_u32_index(np,
					       "atmel,pll-clk-output-ranges",
					       offset + 2, &tmp))
			goto out_free_output;
		out[i] = tmp;

		if (num_cells == 3)
			continue;

		if (of_property_read_u32_index(np,
					       "atmel,pll-clk-output-ranges",
					       offset + 3, &tmp))
			goto out_free_output;
		icpll[i] = tmp;
	}

	characteristics->input = input;
	characteristics->num_output = num_output;
	characteristics->output = output;
	characteristics->out = out;
	characteristics->icpll = icpll;
	return characteristics;

out_free_output:
	kfree(icpll);
	kfree(out);
	kfree(output);
out_free_characteristics:
	kfree(characteristics);
	return NULL;
}

static void __init
of_at91_clk_pll_setup(struct device_node *np,
		      const struct clk_pll_layout *layout)
{
	u32 id;
	struct clk_hw *hw;
	struct regmap *regmap;
	const char *parent_name;
	const char *name = np->name;
	struct clk_pll_characteristics *characteristics;

	if (of_property_read_u32(np, "reg", &id))
		return;

	parent_name = of_clk_get_parent_name(np, 0);

	of_property_read_string(np, "clock-output-names", &name);

	regmap = syscon_node_to_regmap(of_get_parent(np));
	if (IS_ERR(regmap))
		return;

	characteristics = of_at91_clk_pll_get_characteristics(np);
	if (!characteristics)
		return;

	hw = at91_clk_register_pll(regmap, name, parent_name, id, layout,
				    characteristics);
	if (IS_ERR(hw))
		goto out_free_characteristics;

	of_clk_add_hw_provider(np, of_clk_hw_simple_get, hw);
	return;

out_free_characteristics:
	kfree(characteristics);
}

static void __init of_at91rm9200_clk_pll_setup(struct device_node *np)
{
	of_at91_clk_pll_setup(np, &at91rm9200_pll_layout);
}
CLK_OF_DECLARE(at91rm9200_clk_pll, "atmel,at91rm9200-clk-pll",
	       of_at91rm9200_clk_pll_setup);

static void __init of_at91sam9g45_clk_pll_setup(struct device_node *np)
{
	of_at91_clk_pll_setup(np, &at91sam9g45_pll_layout);
}
CLK_OF_DECLARE(at91sam9g45_clk_pll, "atmel,at91sam9g45-clk-pll",
	       of_at91sam9g45_clk_pll_setup);

static void __init of_at91sam9g20_clk_pllb_setup(struct device_node *np)
{
	of_at91_clk_pll_setup(np, &at91sam9g20_pllb_layout);
}
CLK_OF_DECLARE(at91sam9g20_clk_pllb, "atmel,at91sam9g20-clk-pllb",
	       of_at91sam9g20_clk_pllb_setup);

static void __init of_sama5d3_clk_pll_setup(struct device_node *np)
{
	of_at91_clk_pll_setup(np, &sama5d3_pll_layout);
}
CLK_OF_DECLARE(sama5d3_clk_pll, "atmel,sama5d3-clk-pll",
	       of_sama5d3_clk_pll_setup);
