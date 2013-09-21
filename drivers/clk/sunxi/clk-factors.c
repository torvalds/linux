/*
 * Copyright (C) 2013 Emilio López <emilio@elopez.com.ar>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Adjustable factor-based clock implementation
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/string.h>

#include <linux/delay.h>

#include "clk-factors.h"

/*
 * DOC: basic adjustable factor-based clock that cannot gate
 *
 * Traits of this clock:
 * prepare - clk_prepare only ensures that parents are prepared
 * enable - clk_enable only ensures that parents are enabled
 * rate - rate is adjustable.
 *        clk->rate = (parent->rate * N * (K + 1) >> P) / (M + 1)
 * parent - fixed parent.  No clk_set_parent support
 */

struct clk_factors {
	struct clk_hw hw;
	void __iomem *reg;
	struct clk_factors_config *config;
	void (*get_factors) (u32 *rate, u32 parent, u8 *n, u8 *k, u8 *m, u8 *p);
	spinlock_t *lock;
};

#define to_clk_factors(_hw) container_of(_hw, struct clk_factors, hw)

#define SETMASK(len, pos)		(((1U << (len)) - 1) << (pos))
#define CLRMASK(len, pos)		(~(SETMASK(len, pos)))
#define FACTOR_GET(bit, len, reg)	(((reg) & SETMASK(len, bit)) >> (bit))

#define FACTOR_SET(bit, len, reg, val) \
	(((reg) & CLRMASK(len, bit)) | (val << (bit)))

static unsigned long clk_factors_recalc_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	u8 n = 1, k = 0, p = 0, m = 0;
	u32 reg;
	unsigned long rate;
	struct clk_factors *factors = to_clk_factors(hw);
	struct clk_factors_config *config = factors->config;

	/* Fetch the register value */
	reg = readl(factors->reg);

	/* Get each individual factor if applicable */
	if (config->nwidth != SUNXI_FACTORS_NOT_APPLICABLE)
		n = FACTOR_GET(config->nshift, config->nwidth, reg);
	if (config->kwidth != SUNXI_FACTORS_NOT_APPLICABLE)
		k = FACTOR_GET(config->kshift, config->kwidth, reg);
	if (config->mwidth != SUNXI_FACTORS_NOT_APPLICABLE)
		m = FACTOR_GET(config->mshift, config->mwidth, reg);
	if (config->pwidth != SUNXI_FACTORS_NOT_APPLICABLE)
		p = FACTOR_GET(config->pshift, config->pwidth, reg);

	/* Calculate the rate */
	rate = (parent_rate * n * (k + 1) >> p) / (m + 1);

	return rate;
}

static long clk_factors_round_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long *parent_rate)
{
	struct clk_factors *factors = to_clk_factors(hw);
	factors->get_factors((u32 *)&rate, (u32)*parent_rate,
			     NULL, NULL, NULL, NULL);

	return rate;
}

static int clk_factors_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	u8 n, k, m, p;
	u32 reg;
	struct clk_factors *factors = to_clk_factors(hw);
	struct clk_factors_config *config = factors->config;
	unsigned long flags = 0;

	factors->get_factors((u32 *)&rate, (u32)parent_rate, &n, &k, &m, &p);

	if (factors->lock)
		spin_lock_irqsave(factors->lock, flags);

	/* Fetch the register value */
	reg = readl(factors->reg);

	/* Set up the new factors - macros do not do anything if width is 0 */
	reg = FACTOR_SET(config->nshift, config->nwidth, reg, n);
	reg = FACTOR_SET(config->kshift, config->kwidth, reg, k);
	reg = FACTOR_SET(config->mshift, config->mwidth, reg, m);
	reg = FACTOR_SET(config->pshift, config->pwidth, reg, p);

	/* Apply them now */
	writel(reg, factors->reg);

	/* delay 500us so pll stabilizes */
	__delay((rate >> 20) * 500 / 2);

	if (factors->lock)
		spin_unlock_irqrestore(factors->lock, flags);

	return 0;
}

static const struct clk_ops clk_factors_ops = {
	.recalc_rate = clk_factors_recalc_rate,
	.round_rate = clk_factors_round_rate,
	.set_rate = clk_factors_set_rate,
};

/**
 * clk_register_factors - register a factors clock with
 * the clock framework
 * @dev: device registering this clock
 * @name: name of this clock
 * @parent_name: name of clock's parent
 * @flags: framework-specific flags
 * @reg: register address to adjust factors
 * @config: shift and width of factors n, k, m and p
 * @get_factors: function to calculate the factors for a given frequency
 * @lock: shared register lock for this clock
 */
struct clk *clk_register_factors(struct device *dev, const char *name,
				 const char *parent_name,
				 unsigned long flags, void __iomem *reg,
				 struct clk_factors_config *config,
				 void (*get_factors)(u32 *rate, u32 parent,
						     u8 *n, u8 *k, u8 *m, u8 *p),
				 spinlock_t *lock)
{
	struct clk_factors *factors;
	struct clk *clk;
	struct clk_init_data init;

	/* allocate the factors */
	factors = kzalloc(sizeof(struct clk_factors), GFP_KERNEL);
	if (!factors) {
		pr_err("%s: could not allocate factors clk\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	init.name = name;
	init.ops = &clk_factors_ops;
	init.flags = flags;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);

	/* struct clk_factors assignments */
	factors->reg = reg;
	factors->config = config;
	factors->lock = lock;
	factors->hw.init = &init;
	factors->get_factors = get_factors;

	/* register the clock */
	clk = clk_register(dev, &factors->hw);

	if (IS_ERR(clk))
		kfree(factors);

	return clk;
}
