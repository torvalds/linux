// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017 Chen-Yu Tsai. All rights reserved.
 */

#include <linux/clk-provider.h>
#include <linux/clk/sunxi-ng.h>
#include <linux/io.h>

#include "ccu_common.h"

/**
 * sunxi_ccu_set_mmc_timing_mode - Configure the MMC clock timing mode
 * @clk: clock to be configured
 * @new_mode: true for new timing mode introduced in A83T and later
 *
 * Return: %0 on success, %-ENOTSUPP if the clock does not support
 * switching modes.
 */
int sunxi_ccu_set_mmc_timing_mode(struct clk *clk, bool new_mode)
{
	struct clk_hw *hw = __clk_get_hw(clk);
	struct ccu_common *cm = hw_to_ccu_common(hw);
	unsigned long flags;
	u32 val;

	if (!(cm->features & CCU_FEATURE_MMC_TIMING_SWITCH))
		return -ENOTSUPP;

	spin_lock_irqsave(cm->lock, flags);

	val = readl(cm->base + cm->reg);
	if (new_mode)
		val |= CCU_MMC_NEW_TIMING_MODE;
	else
		val &= ~CCU_MMC_NEW_TIMING_MODE;
	writel(val, cm->base + cm->reg);

	spin_unlock_irqrestore(cm->lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(sunxi_ccu_set_mmc_timing_mode);

/**
 * sunxi_ccu_set_mmc_timing_mode: Get the current MMC clock timing mode
 * @clk: clock to query
 *
 * Return: %0 if the clock is in old timing mode, > %0 if it is in
 * new timing mode, and %-ENOTSUPP if the clock does not support
 * this function.
 */
int sunxi_ccu_get_mmc_timing_mode(struct clk *clk)
{
	struct clk_hw *hw = __clk_get_hw(clk);
	struct ccu_common *cm = hw_to_ccu_common(hw);

	if (!(cm->features & CCU_FEATURE_MMC_TIMING_SWITCH))
		return -ENOTSUPP;

	return !!(readl(cm->base + cm->reg) & CCU_MMC_NEW_TIMING_MODE);
}
EXPORT_SYMBOL_GPL(sunxi_ccu_get_mmc_timing_mode);
