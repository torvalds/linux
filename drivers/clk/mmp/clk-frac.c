// SPDX-License-Identifier: GPL-2.0-only
/*
 * mmp factor clock operation source file
 *
 * Copyright (C) 2012 Marvell
 * Chao Xie <xiechao.mail@gmail.com>
 */

#include <linux/clk-provider.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/err.h>

#include "clk.h"
/*
 * It is M/N clock
 *
 * Fout from synthesizer can be given from two equations:
 * numerator/denominator = Fin / (Fout * factor)
 */

#define to_clk_factor(hw) container_of(hw, struct mmp_clk_factor, hw)

static int clk_factor_determine_rate(struct clk_hw *hw,
				     struct clk_rate_request *req)
{
	struct mmp_clk_factor *factor = to_clk_factor(hw);
	u64 rate = 0, prev_rate;
	struct u32_fract *d;
	int i;

	for (i = 0; i < factor->ftbl_cnt; i++) {
		d = &factor->ftbl[i];

		prev_rate = rate;
		rate = (u64)(req->best_parent_rate) * d->denominator;
		do_div(rate, d->numerator * factor->masks->factor);
		if (rate > req->rate)
			break;
	}

	if ((i == 0) || (i == factor->ftbl_cnt))
		req->rate = rate;
	else if ((req->rate - prev_rate) > (rate - req->rate))
		req->rate = rate;
	else
		req->rate = prev_rate;

	return 0;
}

static unsigned long clk_factor_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct mmp_clk_factor *factor = to_clk_factor(hw);
	struct mmp_clk_factor_masks *masks = factor->masks;
	struct u32_fract d;
	unsigned int val;
	u64 rate;

	val = readl_relaxed(factor->base);

	/* calculate numerator */
	d.numerator = (val >> masks->num_shift) & masks->num_mask;

	/* calculate denominator */
	d.denominator = (val >> masks->den_shift) & masks->den_mask;
	if (!d.denominator)
		return 0;

	rate = (u64)parent_rate * d.denominator;
	do_div(rate, d.numerator * factor->masks->factor);

	return rate;
}

/* Configures new clock rate*/
static int clk_factor_set_rate(struct clk_hw *hw, unsigned long drate,
				unsigned long prate)
{
	struct mmp_clk_factor *factor = to_clk_factor(hw);
	struct mmp_clk_factor_masks *masks = factor->masks;
	int i;
	unsigned long val;
	unsigned long flags = 0;
	struct u32_fract *d;
	u64 rate = 0;

	for (i = 0; i < factor->ftbl_cnt; i++) {
		d = &factor->ftbl[i];

		rate = (u64)prate * d->denominator;
		do_div(rate, d->numerator * factor->masks->factor);
		if (rate > drate)
			break;
	}
	d = i ? &factor->ftbl[i - 1] : &factor->ftbl[0];

	if (factor->lock)
		spin_lock_irqsave(factor->lock, flags);

	val = readl_relaxed(factor->base);

	val &= ~(masks->num_mask << masks->num_shift);
	val |= (d->numerator & masks->num_mask) << masks->num_shift;

	val &= ~(masks->den_mask << masks->den_shift);
	val |= (d->denominator & masks->den_mask) << masks->den_shift;

	writel_relaxed(val, factor->base);

	if (factor->lock)
		spin_unlock_irqrestore(factor->lock, flags);

	return 0;
}

static int clk_factor_init(struct clk_hw *hw)
{
	struct mmp_clk_factor *factor = to_clk_factor(hw);
	struct mmp_clk_factor_masks *masks = factor->masks;
	struct u32_fract d;
	u32 val;
	int i;
	unsigned long flags = 0;

	if (factor->lock)
		spin_lock_irqsave(factor->lock, flags);

	val = readl(factor->base);

	/* calculate numerator */
	d.numerator = (val >> masks->num_shift) & masks->num_mask;

	/* calculate denominator */
	d.denominator = (val >> masks->den_shift) & masks->den_mask;

	for (i = 0; i < factor->ftbl_cnt; i++)
		if (d.denominator == factor->ftbl[i].denominator &&
		    d.numerator == factor->ftbl[i].numerator)
			break;

	if (i >= factor->ftbl_cnt) {
		val &= ~(masks->num_mask << masks->num_shift);
		val |= (factor->ftbl[0].numerator & masks->num_mask) << masks->num_shift;

		val &= ~(masks->den_mask << masks->den_shift);
		val |= (factor->ftbl[0].denominator & masks->den_mask) << masks->den_shift;
	}

	if (!(val & masks->enable_mask) || i >= factor->ftbl_cnt) {
		val |= masks->enable_mask;
		writel(val, factor->base);
	}

	if (factor->lock)
		spin_unlock_irqrestore(factor->lock, flags);

	return 0;
}

static const struct clk_ops clk_factor_ops = {
	.recalc_rate = clk_factor_recalc_rate,
	.determine_rate = clk_factor_determine_rate,
	.set_rate = clk_factor_set_rate,
	.init = clk_factor_init,
};

struct clk *mmp_clk_register_factor(const char *name, const char *parent_name,
		unsigned long flags, void __iomem *base,
		struct mmp_clk_factor_masks *masks,
		struct u32_fract *ftbl, unsigned int ftbl_cnt, spinlock_t *lock)
{
	struct mmp_clk_factor *factor;
	struct clk_init_data init;
	struct clk *clk;

	if (!masks) {
		pr_err("%s: must pass a clk_factor_mask\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	factor = kzalloc(sizeof(*factor), GFP_KERNEL);
	if (!factor)
		return ERR_PTR(-ENOMEM);

	/* struct clk_aux assignments */
	factor->base = base;
	factor->masks = masks;
	factor->ftbl = ftbl;
	factor->ftbl_cnt = ftbl_cnt;
	factor->hw.init = &init;
	factor->lock = lock;

	init.name = name;
	init.ops = &clk_factor_ops;
	init.flags = flags;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	clk = clk_register(NULL, &factor->hw);
	if (IS_ERR_OR_NULL(clk))
		kfree(factor);

	return clk;
}
