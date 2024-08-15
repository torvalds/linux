// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 Maxime Ripard
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/spinlock.h>

#include "ccu_phase.h"

static int ccu_phase_get_phase(struct clk_hw *hw)
{
	struct ccu_phase *phase = hw_to_ccu_phase(hw);
	struct clk_hw *parent, *grandparent;
	unsigned int parent_rate, grandparent_rate;
	u16 step, parent_div;
	u32 reg;
	u8 delay;

	reg = readl(phase->common.base + phase->common.reg);
	delay = (reg >> phase->shift);
	delay &= (1 << phase->width) - 1;

	if (!delay)
		return 180;

	/* Get our parent clock, it's the one that can adjust its rate */
	parent = clk_hw_get_parent(hw);
	if (!parent)
		return -EINVAL;

	/* And its rate */
	parent_rate = clk_hw_get_rate(parent);
	if (!parent_rate)
		return -EINVAL;

	/* Now, get our parent's parent (most likely some PLL) */
	grandparent = clk_hw_get_parent(parent);
	if (!grandparent)
		return -EINVAL;

	/* And its rate */
	grandparent_rate = clk_hw_get_rate(grandparent);
	if (!grandparent_rate)
		return -EINVAL;

	/* Get our parent clock divider */
	parent_div = grandparent_rate / parent_rate;

	step = DIV_ROUND_CLOSEST(360, parent_div);
	return delay * step;
}

static int ccu_phase_set_phase(struct clk_hw *hw, int degrees)
{
	struct ccu_phase *phase = hw_to_ccu_phase(hw);
	struct clk_hw *parent, *grandparent;
	unsigned int parent_rate, grandparent_rate;
	unsigned long flags;
	u32 reg;
	u8 delay;

	/* Get our parent clock, it's the one that can adjust its rate */
	parent = clk_hw_get_parent(hw);
	if (!parent)
		return -EINVAL;

	/* And its rate */
	parent_rate = clk_hw_get_rate(parent);
	if (!parent_rate)
		return -EINVAL;

	/* Now, get our parent's parent (most likely some PLL) */
	grandparent = clk_hw_get_parent(parent);
	if (!grandparent)
		return -EINVAL;

	/* And its rate */
	grandparent_rate = clk_hw_get_rate(grandparent);
	if (!grandparent_rate)
		return -EINVAL;

	if (degrees != 180) {
		u16 step, parent_div;

		/* Get our parent divider */
		parent_div = grandparent_rate / parent_rate;

		/*
		 * We can only outphase the clocks by multiple of the
		 * PLL's period.
		 *
		 * Since our parent clock is only a divider, and the
		 * formula to get the outphasing in degrees is deg =
		 * 360 * delta / period
		 *
		 * If we simplify this formula, we can see that the
		 * only thing that we're concerned about is the number
		 * of period we want to outphase our clock from, and
		 * the divider set by our parent clock.
		 */
		step = DIV_ROUND_CLOSEST(360, parent_div);
		delay = DIV_ROUND_CLOSEST(degrees, step);
	} else {
		delay = 0;
	}

	spin_lock_irqsave(phase->common.lock, flags);
	reg = readl(phase->common.base + phase->common.reg);
	reg &= ~GENMASK(phase->width + phase->shift - 1, phase->shift);
	writel(reg | (delay << phase->shift),
	       phase->common.base + phase->common.reg);
	spin_unlock_irqrestore(phase->common.lock, flags);

	return 0;
}

const struct clk_ops ccu_phase_ops = {
	.get_phase	= ccu_phase_get_phase,
	.set_phase	= ccu_phase_set_phase,
};
EXPORT_SYMBOL_NS_GPL(ccu_phase_ops, SUNXI_CCU);
