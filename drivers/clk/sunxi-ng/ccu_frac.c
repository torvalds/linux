/*
 * Copyright (C) 2016 Maxime Ripard
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <linux/clk-provider.h>
#include <linux/spinlock.h>

#include "ccu_frac.h"

bool ccu_frac_helper_is_enabled(struct ccu_common *common,
				struct _ccu_frac *cf)
{
	if (!(common->features & CCU_FEATURE_FRACTIONAL))
		return false;

	return !(readl(common->base + common->reg) & cf->enable);
}

void ccu_frac_helper_enable(struct ccu_common *common,
			    struct _ccu_frac *cf)
{
	unsigned long flags;
	u32 reg;

	if (!(common->features & CCU_FEATURE_FRACTIONAL))
		return;

	spin_lock_irqsave(common->lock, flags);
	reg = readl(common->base + common->reg);
	writel(reg & ~cf->enable, common->base + common->reg);
	spin_unlock_irqrestore(common->lock, flags);
}

void ccu_frac_helper_disable(struct ccu_common *common,
			     struct _ccu_frac *cf)
{
	unsigned long flags;
	u32 reg;

	if (!(common->features & CCU_FEATURE_FRACTIONAL))
		return;

	spin_lock_irqsave(common->lock, flags);
	reg = readl(common->base + common->reg);
	writel(reg | cf->enable, common->base + common->reg);
	spin_unlock_irqrestore(common->lock, flags);
}

bool ccu_frac_helper_has_rate(struct ccu_common *common,
			      struct _ccu_frac *cf,
			      unsigned long rate)
{
	if (!(common->features & CCU_FEATURE_FRACTIONAL))
		return false;

	return (cf->rates[0] == rate) || (cf->rates[1] == rate);
}

unsigned long ccu_frac_helper_read_rate(struct ccu_common *common,
					struct _ccu_frac *cf)
{
	u32 reg;

	printk("%s: Read fractional\n", clk_hw_get_name(&common->hw));

	if (!(common->features & CCU_FEATURE_FRACTIONAL))
		return 0;

	printk("%s: clock is fractional (rates %lu and %lu)\n",
	       clk_hw_get_name(&common->hw), cf->rates[0], cf->rates[1]);

	reg = readl(common->base + common->reg);

	printk("%s: clock reg is 0x%x (select is 0x%x)\n",
	       clk_hw_get_name(&common->hw), reg, cf->select);

	return (reg & cf->select) ? cf->rates[1] : cf->rates[0];
}

int ccu_frac_helper_set_rate(struct ccu_common *common,
			     struct _ccu_frac *cf,
			     unsigned long rate)
{
	unsigned long flags;
	u32 reg, sel;

	if (!(common->features & CCU_FEATURE_FRACTIONAL))
		return -EINVAL;

	if (cf->rates[0] == rate)
		sel = 0;
	else if (cf->rates[1] == rate)
		sel = cf->select;
	else
		return -EINVAL;

	spin_lock_irqsave(common->lock, flags);
	reg = readl(common->base + common->reg);
	reg &= ~cf->select;
	writel(reg | sel, common->base + common->reg);
	spin_unlock_irqrestore(common->lock, flags);

	return 0;
}
