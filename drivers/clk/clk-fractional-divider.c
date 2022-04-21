// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2014 Intel Corporation
 *
 * Adjustable fractional divider clock implementation.
 * Uses rational best approximation algorithm.
 *
 * Output is calculated as
 *
 *	rate = (m / n) * parent_rate				(1)
 *
 * This is useful when we have a prescaler block which asks for
 * m (numerator) and n (denominator) values to be provided to satisfy
 * the (1) as much as possible.
 *
 * Since m and n have the limitation by a range, e.g.
 *
 *	n >= 1, n < N_width, where N_width = 2^nwidth		(2)
 *
 * for some cases the output may be saturated. Hence, from (1) and (2),
 * assuming the worst case when m = 1, the inequality
 *
 *	floor(log2(parent_rate / rate)) <= nwidth		(3)
 *
 * may be derived. Thus, in cases when
 *
 *	(parent_rate / rate) >> N_width				(4)
 *
 * we might scale up the rate by 2^scale (see the description of
 * CLK_FRAC_DIVIDER_POWER_OF_TWO_PS for additional information), where
 *
 *	scale = floor(log2(parent_rate / rate)) - nwidth	(5)
 *
 * and assume that the IP, that needs m and n, has also its own
 * prescaler, which is capable to divide by 2^scale. In this way
 * we get the denominator to satisfy the desired range (2) and
 * at the same time a much better result of m and n than simple
 * saturated values.
 */

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/rational.h>

#include "clk-fractional-divider.h"

static inline u32 clk_fd_readl(struct clk_fractional_divider *fd)
{
	if (fd->flags & CLK_FRAC_DIVIDER_BIG_ENDIAN)
		return ioread32be(fd->reg);

	return readl(fd->reg);
}

static inline void clk_fd_writel(struct clk_fractional_divider *fd, u32 val)
{
	if (fd->flags & CLK_FRAC_DIVIDER_BIG_ENDIAN)
		iowrite32be(val, fd->reg);
	else
		writel(val, fd->reg);
}

static unsigned long clk_fd_recalc_rate(struct clk_hw *hw,
					unsigned long parent_rate)
{
	struct clk_fractional_divider *fd = to_clk_fd(hw);
	unsigned long flags = 0;
	unsigned long m, n;
	u32 val;
	u64 ret;

	if (fd->lock)
		spin_lock_irqsave(fd->lock, flags);
	else
		__acquire(fd->lock);

	val = clk_fd_readl(fd);

	if (fd->lock)
		spin_unlock_irqrestore(fd->lock, flags);
	else
		__release(fd->lock);

	m = (val & fd->mmask) >> fd->mshift;
	n = (val & fd->nmask) >> fd->nshift;

	if (fd->flags & CLK_FRAC_DIVIDER_ZERO_BASED) {
		m++;
		n++;
	}

	if (!n || !m)
		return parent_rate;

	ret = (u64)parent_rate * m;
	do_div(ret, n);

	return ret;
}

void clk_fractional_divider_general_approximation(struct clk_hw *hw,
						  unsigned long rate,
						  unsigned long *parent_rate,
						  unsigned long *m, unsigned long *n)
{
	struct clk_fractional_divider *fd = to_clk_fd(hw);

	/*
	 * Get rate closer to *parent_rate to guarantee there is no overflow
	 * for m and n. In the result it will be the nearest rate left shifted
	 * by (scale - fd->nwidth) bits.
	 *
	 * For the detailed explanation see the top comment in this file.
	 */
	if (fd->flags & CLK_FRAC_DIVIDER_POWER_OF_TWO_PS) {
		unsigned long scale = fls_long(*parent_rate / rate - 1);

		if (scale > fd->nwidth)
			rate <<= scale - fd->nwidth;
	}

	rational_best_approximation(rate, *parent_rate,
			GENMASK(fd->mwidth - 1, 0), GENMASK(fd->nwidth - 1, 0),
			m, n);
}

static long clk_fd_round_rate(struct clk_hw *hw, unsigned long rate,
			      unsigned long *parent_rate)
{
	struct clk_fractional_divider *fd = to_clk_fd(hw);
	unsigned long m, n;
	u64 ret;

	if (!rate || (!clk_hw_can_set_rate_parent(hw) && rate >= *parent_rate))
		return *parent_rate;

	if (fd->approximation)
		fd->approximation(hw, rate, parent_rate, &m, &n);
	else
		clk_fractional_divider_general_approximation(hw, rate, parent_rate, &m, &n);

	ret = (u64)*parent_rate * m;
	do_div(ret, n);

	return ret;
}

static int clk_fd_set_rate(struct clk_hw *hw, unsigned long rate,
			   unsigned long parent_rate)
{
	struct clk_fractional_divider *fd = to_clk_fd(hw);
	unsigned long flags = 0;
	unsigned long m, n;
	u32 val;

	rational_best_approximation(rate, parent_rate,
			GENMASK(fd->mwidth - 1, 0), GENMASK(fd->nwidth - 1, 0),
			&m, &n);

	if (fd->flags & CLK_FRAC_DIVIDER_ZERO_BASED) {
		m--;
		n--;
	}

	if (fd->lock)
		spin_lock_irqsave(fd->lock, flags);
	else
		__acquire(fd->lock);

	val = clk_fd_readl(fd);
	val &= ~(fd->mmask | fd->nmask);
	val |= (m << fd->mshift) | (n << fd->nshift);
	clk_fd_writel(fd, val);

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

struct clk_hw *clk_hw_register_fractional_divider(struct device *dev,
		const char *name, const char *parent_name, unsigned long flags,
		void __iomem *reg, u8 mshift, u8 mwidth, u8 nshift, u8 nwidth,
		u8 clk_divider_flags, spinlock_t *lock)
{
	struct clk_fractional_divider *fd;
	struct clk_init_data init;
	struct clk_hw *hw;
	int ret;

	fd = kzalloc(sizeof(*fd), GFP_KERNEL);
	if (!fd)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &clk_fractional_divider_ops;
	init.flags = flags;
	init.parent_names = parent_name ? &parent_name : NULL;
	init.num_parents = parent_name ? 1 : 0;

	fd->reg = reg;
	fd->mshift = mshift;
	fd->mwidth = mwidth;
	fd->mmask = GENMASK(mwidth - 1, 0) << mshift;
	fd->nshift = nshift;
	fd->nwidth = nwidth;
	fd->nmask = GENMASK(nwidth - 1, 0) << nshift;
	fd->flags = clk_divider_flags;
	fd->lock = lock;
	fd->hw.init = &init;

	hw = &fd->hw;
	ret = clk_hw_register(dev, hw);
	if (ret) {
		kfree(fd);
		hw = ERR_PTR(ret);
	}

	return hw;
}
EXPORT_SYMBOL_GPL(clk_hw_register_fractional_divider);

struct clk *clk_register_fractional_divider(struct device *dev,
		const char *name, const char *parent_name, unsigned long flags,
		void __iomem *reg, u8 mshift, u8 mwidth, u8 nshift, u8 nwidth,
		u8 clk_divider_flags, spinlock_t *lock)
{
	struct clk_hw *hw;

	hw = clk_hw_register_fractional_divider(dev, name, parent_name, flags,
			reg, mshift, mwidth, nshift, nwidth, clk_divider_flags,
			lock);
	if (IS_ERR(hw))
		return ERR_CAST(hw);
	return hw->clk;
}
EXPORT_SYMBOL_GPL(clk_register_fractional_divider);

void clk_hw_unregister_fractional_divider(struct clk_hw *hw)
{
	struct clk_fractional_divider *fd;

	fd = to_clk_fd(hw);

	clk_hw_unregister(hw);
	kfree(fd);
}
