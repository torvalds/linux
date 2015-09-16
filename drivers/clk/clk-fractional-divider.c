/*
 * Copyright (C) 2014 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Adjustable fractional divider clock implementation.
 * Output rate = (m / n) * parent_rate.
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/gcd.h>

#define to_clk_fd(_hw) container_of(_hw, struct clk_fractional_divider, hw)

static unsigned long clk_fd_recalc_rate(struct clk_hw *hw,
					unsigned long parent_rate)
{
	struct clk_fractional_divider *fd = to_clk_fd(hw);
	unsigned long flags = 0;
	u32 val, m, n;
	u64 ret;

	if (fd->lock)
		spin_lock_irqsave(fd->lock, flags);
	else
		__acquire(fd->lock);

	val = clk_readl(fd->reg);

	if (fd->lock)
		spin_unlock_irqrestore(fd->lock, flags);
	else
		__release(fd->lock);

	m = (val & fd->mmask) >> fd->mshift;
	n = (val & fd->nmask) >> fd->nshift;

	if (!n || !m)
		return parent_rate;

	ret = (u64)parent_rate * m;
	do_div(ret, n);

	return ret;
}

static long clk_fd_round_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long *prate)
{
	struct clk_fractional_divider *fd = to_clk_fd(hw);
	unsigned maxn = (fd->nmask >> fd->nshift) + 1;
	unsigned div;

	if (!rate || rate >= *prate)
		return *prate;

	div = gcd(*prate, rate);

	while ((*prate / div) > maxn) {
		div <<= 1;
		rate <<= 1;
	}

	return rate;
}

static int clk_fd_set_rate(struct clk_hw *hw, unsigned long rate,
			   unsigned long parent_rate)
{
	struct clk_fractional_divider *fd = to_clk_fd(hw);
	unsigned long flags = 0;
	unsigned long div;
	unsigned n, m;
	u32 val;

	div = gcd(parent_rate, rate);
	m = rate / div;
	n = parent_rate / div;

	if (fd->lock)
		spin_lock_irqsave(fd->lock, flags);
	else
		__acquire(fd->lock);

	val = clk_readl(fd->reg);
	val &= ~(fd->mmask | fd->nmask);
	val |= (m << fd->mshift) | (n << fd->nshift);
	clk_writel(val, fd->reg);

	if (fd->lock)
		spin_unlock_irqrestore(fd->lock, flags);
	else
		__release(fd->lock);

	return 0;
}

const struct clk_ops clk_fractional_divider_ops = {
	.recalc_rate = clk_fd_recalc_rate,
	.round_rate = clk_fd_round_rate,
	.set_rate = clk_fd_set_rate,
};
EXPORT_SYMBOL_GPL(clk_fractional_divider_ops);

struct clk *clk_register_fractional_divider(struct device *dev,
		const char *name, const char *parent_name, unsigned long flags,
		void __iomem *reg, u8 mshift, u8 mwidth, u8 nshift, u8 nwidth,
		u8 clk_divider_flags, spinlock_t *lock)
{
	struct clk_fractional_divider *fd;
	struct clk_init_data init;
	struct clk *clk;

	fd = kzalloc(sizeof(*fd), GFP_KERNEL);
	if (!fd)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &clk_fractional_divider_ops;
	init.flags = flags | CLK_IS_BASIC;
	init.parent_names = parent_name ? &parent_name : NULL;
	init.num_parents = parent_name ? 1 : 0;

	fd->reg = reg;
	fd->mshift = mshift;
	fd->mmask = (BIT(mwidth) - 1) << mshift;
	fd->nshift = nshift;
	fd->nmask = (BIT(nwidth) - 1) << nshift;
	fd->flags = clk_divider_flags;
	fd->lock = lock;
	fd->hw.init = &init;

	clk = clk_register(dev, &fd->hw);
	if (IS_ERR(clk))
		kfree(fd);

	return clk;
}
EXPORT_SYMBOL_GPL(clk_register_fractional_divider);
