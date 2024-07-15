// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Inochi Amaoto <inochiama@outlook.com>
 */

#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/spinlock.h>
#include <linux/bug.h>

#include "clk-cv18xx-common.h"

int cv1800_clk_setbit(struct cv1800_clk_common *common,
		      struct cv1800_clk_regbit *field)
{
	u32 mask = BIT(field->shift);
	u32 value;
	unsigned long flags;

	spin_lock_irqsave(common->lock, flags);

	value = readl(common->base + field->reg);
	writel(value | mask, common->base + field->reg);

	spin_unlock_irqrestore(common->lock, flags);

	return 0;
}

int cv1800_clk_clearbit(struct cv1800_clk_common *common,
			struct cv1800_clk_regbit *field)
{
	u32 mask = BIT(field->shift);
	u32 value;
	unsigned long flags;

	spin_lock_irqsave(common->lock, flags);

	value = readl(common->base + field->reg);
	writel(value & ~mask, common->base + field->reg);

	spin_unlock_irqrestore(common->lock, flags);

	return 0;
}

int cv1800_clk_checkbit(struct cv1800_clk_common *common,
			struct cv1800_clk_regbit *field)
{
	return readl(common->base + field->reg) & BIT(field->shift);
}

#define PLL_LOCK_TIMEOUT_US	(200 * 1000)

void cv1800_clk_wait_for_lock(struct cv1800_clk_common *common,
			      u32 reg, u32 lock)
{
	void __iomem *addr = common->base + reg;
	u32 regval;

	if (!lock)
		return;

	WARN_ON(readl_relaxed_poll_timeout(addr, regval, regval & lock,
					   100, PLL_LOCK_TIMEOUT_US));
}
