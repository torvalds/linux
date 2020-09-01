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

#define MASTER_PRES_MASK	0x7
#define MASTER_PRES_MAX		MASTER_PRES_MASK
#define MASTER_DIV_SHIFT	8
#define MASTER_DIV_MASK		0x3

#define PMC_MCR			0x30
#define PMC_MCR_ID_MSK		GENMASK(3, 0)
#define PMC_MCR_CMD		BIT(7)
#define PMC_MCR_DIV		GENMASK(10, 8)
#define PMC_MCR_CSS		GENMASK(20, 16)
#define PMC_MCR_CSS_SHIFT	(16)
#define PMC_MCR_EN		BIT(28)

#define PMC_MCR_ID(x)		((x) & PMC_MCR_ID_MSK)

#define MASTER_MAX_ID		4

#define to_clk_master(hw) container_of(hw, struct clk_master, hw)

struct clk_master {
	struct clk_hw hw;
	struct regmap *regmap;
	spinlock_t *lock;
	const struct clk_master_layout *layout;
	const struct clk_master_characteristics *characteristics;
	u32 *mux_table;
	u32 mckr;
	int chg_pid;
	u8 id;
	u8 parent;
	u8 div;
};

static inline bool clk_master_ready(struct clk_master *master)
{
	unsigned int bit = master->id ? AT91_PMC_MCKXRDY : AT91_PMC_MCKRDY;
	unsigned int status;

	regmap_read(master->regmap, AT91_PMC_SR, &status);

	return !!(status & bit);
}

static int clk_master_prepare(struct clk_hw *hw)
{
	struct clk_master *master = to_clk_master(hw);

	while (!clk_master_ready(master))
		cpu_relax();

	return 0;
}

static int clk_master_is_prepared(struct clk_hw *hw)
{
	struct clk_master *master = to_clk_master(hw);

	return clk_master_ready(master);
}

static unsigned long clk_master_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	u8 pres;
	u8 div;
	unsigned long rate = parent_rate;
	struct clk_master *master = to_clk_master(hw);
	const struct clk_master_layout *layout = master->layout;
	const struct clk_master_characteristics *characteristics =
						master->characteristics;
	unsigned int mckr;

	regmap_read(master->regmap, master->layout->offset, &mckr);
	mckr &= layout->mask;

	pres = (mckr >> layout->pres_shift) & MASTER_PRES_MASK;
	div = (mckr >> MASTER_DIV_SHIFT) & MASTER_DIV_MASK;

	if (characteristics->have_div3_pres && pres == MASTER_PRES_MAX)
		rate /= 3;
	else
		rate >>= pres;

	rate /= characteristics->divisors[div];

	if (rate < characteristics->output.min)
		pr_warn("master clk is underclocked");
	else if (rate > characteristics->output.max)
		pr_warn("master clk is overclocked");

	return rate;
}

static u8 clk_master_get_parent(struct clk_hw *hw)
{
	struct clk_master *master = to_clk_master(hw);
	unsigned int mckr;

	regmap_read(master->regmap, master->layout->offset, &mckr);

	return mckr & AT91_PMC_CSS;
}

static const struct clk_ops master_ops = {
	.prepare = clk_master_prepare,
	.is_prepared = clk_master_is_prepared,
	.recalc_rate = clk_master_recalc_rate,
	.get_parent = clk_master_get_parent,
};

struct clk_hw * __init
at91_clk_register_master(struct regmap *regmap,
		const char *name, int num_parents,
		const char **parent_names,
		const struct clk_master_layout *layout,
		const struct clk_master_characteristics *characteristics)
{
	struct clk_master *master;
	struct clk_init_data init;
	struct clk_hw *hw;
	int ret;

	if (!name || !num_parents || !parent_names)
		return ERR_PTR(-EINVAL);

	master = kzalloc(sizeof(*master), GFP_KERNEL);
	if (!master)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &master_ops;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	init.flags = 0;

	master->hw.init = &init;
	master->layout = layout;
	master->characteristics = characteristics;
	master->regmap = regmap;

	hw = &master->hw;
	ret = clk_hw_register(NULL, &master->hw);
	if (ret) {
		kfree(master);
		hw = ERR_PTR(ret);
	}

	return hw;
}

static unsigned long
clk_sama7g5_master_recalc_rate(struct clk_hw *hw,
			       unsigned long parent_rate)
{
	struct clk_master *master = to_clk_master(hw);

	return DIV_ROUND_CLOSEST_ULL(parent_rate, (1 << master->div));
}

static void clk_sama7g5_master_best_diff(struct clk_rate_request *req,
					 struct clk_hw *parent,
					 unsigned long parent_rate,
					 long *best_rate,
					 long *best_diff,
					 u32 div)
{
	unsigned long tmp_rate, tmp_diff;

	if (div == MASTER_PRES_MAX)
		tmp_rate = parent_rate / 3;
	else
		tmp_rate = parent_rate >> div;

	tmp_diff = abs(req->rate - tmp_rate);

	if (*best_diff < 0 || *best_diff >= tmp_diff) {
		*best_rate = tmp_rate;
		*best_diff = tmp_diff;
		req->best_parent_rate = parent_rate;
		req->best_parent_hw = parent;
	}
}

static int clk_sama7g5_master_determine_rate(struct clk_hw *hw,
					     struct clk_rate_request *req)
{
	struct clk_master *master = to_clk_master(hw);
	struct clk_rate_request req_parent = *req;
	struct clk_hw *parent;
	long best_rate = LONG_MIN, best_diff = LONG_MIN;
	unsigned long parent_rate;
	unsigned int div, i;

	/* First: check the dividers of MCR. */
	for (i = 0; i < clk_hw_get_num_parents(hw); i++) {
		parent = clk_hw_get_parent_by_index(hw, i);
		if (!parent)
			continue;

		parent_rate = clk_hw_get_rate(parent);
		if (!parent_rate)
			continue;

		for (div = 0; div < MASTER_PRES_MAX + 1; div++) {
			clk_sama7g5_master_best_diff(req, parent, parent_rate,
						     &best_rate, &best_diff,
						     div);
			if (!best_diff)
				break;
		}

		if (!best_diff)
			break;
	}

	/* Second: try to request rate form changeable parent. */
	if (master->chg_pid < 0)
		goto end;

	parent = clk_hw_get_parent_by_index(hw, master->chg_pid);
	if (!parent)
		goto end;

	for (div = 0; div < MASTER_PRES_MAX + 1; div++) {
		if (div == MASTER_PRES_MAX)
			req_parent.rate = req->rate * 3;
		else
			req_parent.rate = req->rate << div;

		if (__clk_determine_rate(parent, &req_parent))
			continue;

		clk_sama7g5_master_best_diff(req, parent, req_parent.rate,
					     &best_rate, &best_diff, div);

		if (!best_diff)
			break;
	}

end:
	pr_debug("MCK: %s, best_rate = %ld, parent clk: %s @ %ld\n",
		 __func__, best_rate,
		 __clk_get_name((req->best_parent_hw)->clk),
		req->best_parent_rate);

	if (best_rate < 0)
		return -EINVAL;

	req->rate = best_rate;

	return 0;
}

static u8 clk_sama7g5_master_get_parent(struct clk_hw *hw)
{
	struct clk_master *master = to_clk_master(hw);
	unsigned long flags;
	u8 index;

	spin_lock_irqsave(master->lock, flags);
	index = clk_mux_val_to_index(&master->hw, master->mux_table, 0,
				     master->parent);
	spin_unlock_irqrestore(master->lock, flags);

	return index;
}

static int clk_sama7g5_master_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_master *master = to_clk_master(hw);
	unsigned long flags;

	if (index >= clk_hw_get_num_parents(hw))
		return -EINVAL;

	spin_lock_irqsave(master->lock, flags);
	master->parent = clk_mux_index_to_val(master->mux_table, 0, index);
	spin_unlock_irqrestore(master->lock, flags);

	return 0;
}

static int clk_sama7g5_master_enable(struct clk_hw *hw)
{
	struct clk_master *master = to_clk_master(hw);
	unsigned long flags;
	unsigned int val, cparent;

	spin_lock_irqsave(master->lock, flags);

	regmap_write(master->regmap, PMC_MCR, PMC_MCR_ID(master->id));
	regmap_read(master->regmap, PMC_MCR, &val);
	regmap_update_bits(master->regmap, PMC_MCR,
			   PMC_MCR_EN | PMC_MCR_CSS | PMC_MCR_DIV |
			   PMC_MCR_CMD | PMC_MCR_ID_MSK,
			   PMC_MCR_EN | (master->parent << PMC_MCR_CSS_SHIFT) |
			   (master->div << MASTER_DIV_SHIFT) |
			   PMC_MCR_CMD | PMC_MCR_ID(master->id));

	cparent = (val & PMC_MCR_CSS) >> PMC_MCR_CSS_SHIFT;

	/* Wait here only if parent is being changed. */
	while ((cparent != master->parent) && !clk_master_ready(master))
		cpu_relax();

	spin_unlock_irqrestore(master->lock, flags);

	return 0;
}

static void clk_sama7g5_master_disable(struct clk_hw *hw)
{
	struct clk_master *master = to_clk_master(hw);
	unsigned long flags;

	spin_lock_irqsave(master->lock, flags);

	regmap_write(master->regmap, PMC_MCR, master->id);
	regmap_update_bits(master->regmap, PMC_MCR,
			   PMC_MCR_EN | PMC_MCR_CMD | PMC_MCR_ID_MSK,
			   PMC_MCR_CMD | PMC_MCR_ID(master->id));

	spin_unlock_irqrestore(master->lock, flags);
}

static int clk_sama7g5_master_is_enabled(struct clk_hw *hw)
{
	struct clk_master *master = to_clk_master(hw);
	unsigned long flags;
	unsigned int val;

	spin_lock_irqsave(master->lock, flags);

	regmap_write(master->regmap, PMC_MCR, master->id);
	regmap_read(master->regmap, PMC_MCR, &val);

	spin_unlock_irqrestore(master->lock, flags);

	return !!(val & PMC_MCR_EN);
}

static int clk_sama7g5_master_set_rate(struct clk_hw *hw, unsigned long rate,
				       unsigned long parent_rate)
{
	struct clk_master *master = to_clk_master(hw);
	unsigned long div, flags;

	div = DIV_ROUND_CLOSEST(parent_rate, rate);
	if ((div > (1 << (MASTER_PRES_MAX - 1))) || (div & (div - 1)))
		return -EINVAL;

	if (div == 3)
		div = MASTER_PRES_MAX;
	else
		div = ffs(div) - 1;

	spin_lock_irqsave(master->lock, flags);
	master->div = div;
	spin_unlock_irqrestore(master->lock, flags);

	return 0;
}

static const struct clk_ops sama7g5_master_ops = {
	.enable = clk_sama7g5_master_enable,
	.disable = clk_sama7g5_master_disable,
	.is_enabled = clk_sama7g5_master_is_enabled,
	.recalc_rate = clk_sama7g5_master_recalc_rate,
	.determine_rate = clk_sama7g5_master_determine_rate,
	.set_rate = clk_sama7g5_master_set_rate,
	.get_parent = clk_sama7g5_master_get_parent,
	.set_parent = clk_sama7g5_master_set_parent,
};

struct clk_hw * __init
at91_clk_sama7g5_register_master(struct regmap *regmap,
				 const char *name, int num_parents,
				 const char **parent_names,
				 u32 *mux_table,
				 spinlock_t *lock, u8 id,
				 bool critical, int chg_pid)
{
	struct clk_master *master;
	struct clk_hw *hw;
	struct clk_init_data init;
	unsigned long flags;
	unsigned int val;
	int ret;

	if (!name || !num_parents || !parent_names || !mux_table ||
	    !lock || id > MASTER_MAX_ID)
		return ERR_PTR(-EINVAL);

	master = kzalloc(sizeof(*master), GFP_KERNEL);
	if (!master)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &sama7g5_master_ops;
	init.parent_names = parent_names;
	init.num_parents = num_parents;
	init.flags = CLK_SET_RATE_GATE | CLK_SET_PARENT_GATE;
	if (chg_pid >= 0)
		init.flags |= CLK_SET_RATE_PARENT;
	if (critical)
		init.flags |= CLK_IS_CRITICAL;

	master->hw.init = &init;
	master->regmap = regmap;
	master->id = id;
	master->chg_pid = chg_pid;
	master->lock = lock;
	master->mux_table = mux_table;

	spin_lock_irqsave(master->lock, flags);
	regmap_write(master->regmap, PMC_MCR, master->id);
	regmap_read(master->regmap, PMC_MCR, &val);
	master->parent = (val & PMC_MCR_CSS) >> PMC_MCR_CSS_SHIFT;
	master->div = (val & PMC_MCR_DIV) >> MASTER_DIV_SHIFT;
	spin_unlock_irqrestore(master->lock, flags);

	hw = &master->hw;
	ret = clk_hw_register(NULL, &master->hw);
	if (ret) {
		kfree(master);
		hw = ERR_PTR(ret);
	}

	return hw;
}

const struct clk_master_layout at91rm9200_master_layout = {
	.mask = 0x31F,
	.pres_shift = 2,
	.offset = AT91_PMC_MCKR,
};

const struct clk_master_layout at91sam9x5_master_layout = {
	.mask = 0x373,
	.pres_shift = 4,
	.offset = AT91_PMC_MCKR,
};
