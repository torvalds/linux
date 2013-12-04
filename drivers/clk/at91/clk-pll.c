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
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/io.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include "pmc.h"

#define PLL_STATUS_MASK(id)	(1 << (1 + (id)))
#define PLL_REG(id)		(AT91_CKGR_PLLAR + ((id) * 4))
#define PLL_DIV_MASK		0xff
#define PLL_DIV_MAX		PLL_DIV_MASK
#define PLL_DIV(reg)		((reg) & PLL_DIV_MASK)
#define PLL_MUL(reg, layout)	(((reg) >> (layout)->mul_shift) & \
				 (layout)->mul_mask)
#define PLL_ICPR_SHIFT(id)	((id) * 16)
#define PLL_ICPR_MASK(id)	(0xffff << PLL_ICPR_SHIFT(id))
#define PLL_MAX_COUNT		0x3ff
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
	struct at91_pmc *pmc;
	unsigned int irq;
	wait_queue_head_t wait;
	u8 id;
	u8 div;
	u8 range;
	u16 mul;
	const struct clk_pll_layout *layout;
	const struct clk_pll_characteristics *characteristics;
};

static irqreturn_t clk_pll_irq_handler(int irq, void *dev_id)
{
	struct clk_pll *pll = (struct clk_pll *)dev_id;

	wake_up(&pll->wait);
	disable_irq_nosync(pll->irq);

	return IRQ_HANDLED;
}

static int clk_pll_prepare(struct clk_hw *hw)
{
	struct clk_pll *pll = to_clk_pll(hw);
	struct at91_pmc *pmc = pll->pmc;
	const struct clk_pll_layout *layout = pll->layout;
	const struct clk_pll_characteristics *characteristics =
							pll->characteristics;
	u8 id = pll->id;
	u32 mask = PLL_STATUS_MASK(id);
	int offset = PLL_REG(id);
	u8 out = 0;
	u32 pllr, icpr;
	u8 div;
	u16 mul;

	pllr = pmc_read(pmc, offset);
	div = PLL_DIV(pllr);
	mul = PLL_MUL(pllr, layout);

	if ((pmc_read(pmc, AT91_PMC_SR) & mask) &&
	    (div == pll->div && mul == pll->mul))
		return 0;

	if (characteristics->out)
		out = characteristics->out[pll->range];
	if (characteristics->icpll) {
		icpr = pmc_read(pmc, AT91_PMC_PLLICPR) & ~PLL_ICPR_MASK(id);
		icpr |= (characteristics->icpll[pll->range] <<
			PLL_ICPR_SHIFT(id));
		pmc_write(pmc, AT91_PMC_PLLICPR, icpr);
	}

	pllr &= ~layout->pllr_mask;
	pllr |= layout->pllr_mask &
	       (pll->div | (PLL_MAX_COUNT << PLL_COUNT_SHIFT) |
		(out << PLL_OUT_SHIFT) |
		((pll->mul & layout->mul_mask) << layout->mul_shift));
	pmc_write(pmc, offset, pllr);

	while (!(pmc_read(pmc, AT91_PMC_SR) & mask)) {
		enable_irq(pll->irq);
		wait_event(pll->wait,
			   pmc_read(pmc, AT91_PMC_SR) & mask);
	}

	return 0;
}

static int clk_pll_is_prepared(struct clk_hw *hw)
{
	struct clk_pll *pll = to_clk_pll(hw);
	struct at91_pmc *pmc = pll->pmc;

	return !!(pmc_read(pmc, AT91_PMC_SR) &
		  PLL_STATUS_MASK(pll->id));
}

static void clk_pll_unprepare(struct clk_hw *hw)
{
	struct clk_pll *pll = to_clk_pll(hw);
	struct at91_pmc *pmc = pll->pmc;
	const struct clk_pll_layout *layout = pll->layout;
	int offset = PLL_REG(pll->id);
	u32 tmp = pmc_read(pmc, offset) & ~(layout->pllr_mask);

	pmc_write(pmc, offset, tmp);
}

static unsigned long clk_pll_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct clk_pll *pll = to_clk_pll(hw);
	const struct clk_pll_layout *layout = pll->layout;
	struct at91_pmc *pmc = pll->pmc;
	int offset = PLL_REG(pll->id);
	u32 tmp = pmc_read(pmc, offset) & layout->pllr_mask;
	u8 div = PLL_DIV(tmp);
	u16 mul = PLL_MUL(tmp, layout);
	if (!div || !mul)
		return 0;

	return (parent_rate * (mul + 1)) / div;
}

static long clk_pll_get_best_div_mul(struct clk_pll *pll, unsigned long rate,
				     unsigned long parent_rate,
				     u32 *div, u32 *mul,
				     u32 *index) {
	unsigned long maxrate;
	unsigned long minrate;
	unsigned long divrate;
	unsigned long bestdiv = 1;
	unsigned long bestmul;
	unsigned long tmpdiv;
	unsigned long roundup;
	unsigned long rounddown;
	unsigned long remainder;
	unsigned long bestremainder;
	unsigned long maxmul;
	unsigned long maxdiv;
	unsigned long mindiv;
	int i = 0;
	const struct clk_pll_layout *layout = pll->layout;
	const struct clk_pll_characteristics *characteristics =
							pll->characteristics;

	/* Minimum divider = 1 */
	/* Maximum multiplier = max_mul */
	maxmul = layout->mul_mask + 1;
	maxrate = (parent_rate * maxmul) / 1;

	/* Maximum divider = max_div */
	/* Minimum multiplier = 2 */
	maxdiv = PLL_DIV_MAX;
	minrate = (parent_rate * 2) / maxdiv;

	if (parent_rate < characteristics->input.min ||
	    parent_rate < characteristics->input.max)
		return -ERANGE;

	if (parent_rate < minrate || parent_rate > maxrate)
		return -ERANGE;

	for (i = 0; i < characteristics->num_output; i++) {
		if (parent_rate >= characteristics->output[i].min &&
		    parent_rate <= characteristics->output[i].max)
			break;
	}

	if (i >= characteristics->num_output)
		return -ERANGE;

	bestmul = rate / parent_rate;
	rounddown = parent_rate % rate;
	roundup = rate - rounddown;
	bestremainder = roundup < rounddown ? roundup : rounddown;

	if (!bestremainder) {
		if (div)
			*div = bestdiv;
		if (mul)
			*mul = bestmul;
		if (index)
			*index = i;
		return rate;
	}

	maxdiv = 255 / (bestmul + 1);
	if (parent_rate / maxdiv < characteristics->input.min)
		maxdiv = parent_rate / characteristics->input.min;
	mindiv = parent_rate / characteristics->input.max;
	if (parent_rate % characteristics->input.max)
		mindiv++;

	for (tmpdiv = mindiv; tmpdiv < maxdiv; tmpdiv++) {
		divrate = parent_rate / tmpdiv;

		rounddown = rate % divrate;
		roundup = divrate - rounddown;
		remainder = roundup < rounddown ? roundup : rounddown;

		if (remainder < bestremainder) {
			bestremainder = remainder;
			bestmul = rate / divrate;
			bestdiv = tmpdiv;
		}

		if (!remainder)
			break;
	}

	rate = (parent_rate / bestdiv) * bestmul;

	if (div)
		*div = bestdiv;
	if (mul)
		*mul = bestmul;
	if (index)
		*index = i;

	return rate;
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

static struct clk * __init
at91_clk_register_pll(struct at91_pmc *pmc, unsigned int irq, const char *name,
		      const char *parent_name, u8 id,
		      const struct clk_pll_layout *layout,
		      const struct clk_pll_characteristics *characteristics)
{
	struct clk_pll *pll;
	struct clk *clk = NULL;
	struct clk_init_data init;
	int ret;
	int offset = PLL_REG(id);
	u32 tmp;

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
	pll->pmc = pmc;
	pll->irq = irq;
	tmp = pmc_read(pmc, offset) & layout->pllr_mask;
	pll->div = PLL_DIV(tmp);
	pll->mul = PLL_MUL(tmp, layout);
	init_waitqueue_head(&pll->wait);
	irq_set_status_flags(pll->irq, IRQ_NOAUTOEN);
	ret = request_irq(pll->irq, clk_pll_irq_handler, IRQF_TRIGGER_HIGH,
			  id ? "clk-pllb" : "clk-plla", pll);
	if (ret)
		return ERR_PTR(ret);

	clk = clk_register(NULL, &pll->hw);
	if (IS_ERR(clk))
		kfree(pll);

	return clk;
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

	output = kzalloc(sizeof(*output) * num_output, GFP_KERNEL);
	if (!output)
		goto out_free_characteristics;

	if (num_cells > 2) {
		out = kzalloc(sizeof(*out) * num_output, GFP_KERNEL);
		if (!out)
			goto out_free_output;
	}

	if (num_cells > 3) {
		icpll = kzalloc(sizeof(*icpll) * num_output, GFP_KERNEL);
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
of_at91_clk_pll_setup(struct device_node *np, struct at91_pmc *pmc,
		      const struct clk_pll_layout *layout)
{
	u32 id;
	unsigned int irq;
	struct clk *clk;
	const char *parent_name;
	const char *name = np->name;
	struct clk_pll_characteristics *characteristics;

	if (of_property_read_u32(np, "reg", &id))
		return;

	parent_name = of_clk_get_parent_name(np, 0);

	of_property_read_string(np, "clock-output-names", &name);

	characteristics = of_at91_clk_pll_get_characteristics(np);
	if (!characteristics)
		return;

	irq = irq_of_parse_and_map(np, 0);
	if (!irq)
		return;

	clk = at91_clk_register_pll(pmc, irq, name, parent_name, id, layout,
				    characteristics);
	if (IS_ERR(clk))
		goto out_free_characteristics;

	of_clk_add_provider(np, of_clk_src_simple_get, clk);
	return;

out_free_characteristics:
	kfree(characteristics);
}

void __init of_at91rm9200_clk_pll_setup(struct device_node *np,
					       struct at91_pmc *pmc)
{
	of_at91_clk_pll_setup(np, pmc, &at91rm9200_pll_layout);
}

void __init of_at91sam9g45_clk_pll_setup(struct device_node *np,
						struct at91_pmc *pmc)
{
	of_at91_clk_pll_setup(np, pmc, &at91sam9g45_pll_layout);
}

void __init of_at91sam9g20_clk_pllb_setup(struct device_node *np,
						 struct at91_pmc *pmc)
{
	of_at91_clk_pll_setup(np, pmc, &at91sam9g20_pllb_layout);
}

void __init of_sama5d3_clk_pll_setup(struct device_node *np,
					    struct at91_pmc *pmc)
{
	of_at91_clk_pll_setup(np, pmc, &sama5d3_pll_layout);
}
