/*
 * Copyright (C) 2016 Maxime Ripard
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/reset-controller.h>

#include "ccu_reset.h"

static int ccu_reset_assert(struct reset_controller_dev *rcdev,
			    unsigned long id)
{
	struct ccu_reset *ccu = rcdev_to_ccu_reset(rcdev);
	const struct ccu_reset_map *map = &ccu->reset_map[id];
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(ccu->lock, flags);

	reg = readl(ccu->base + map->reg);
	writel(reg & ~map->bit, ccu->base + map->reg);

	spin_unlock_irqrestore(ccu->lock, flags);

	return 0;
}

static int ccu_reset_deassert(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	struct ccu_reset *ccu = rcdev_to_ccu_reset(rcdev);
	const struct ccu_reset_map *map = &ccu->reset_map[id];
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(ccu->lock, flags);

	reg = readl(ccu->base + map->reg);
	writel(reg | map->bit, ccu->base + map->reg);

	spin_unlock_irqrestore(ccu->lock, flags);

	return 0;
}

static int ccu_reset_reset(struct reset_controller_dev *rcdev,
			   unsigned long id)
{
	ccu_reset_assert(rcdev, id);
	udelay(10);
	ccu_reset_deassert(rcdev, id);

	return 0;
}

static int ccu_reset_status(struct reset_controller_dev *rcdev,
			    unsigned long id)
{
	struct ccu_reset *ccu = rcdev_to_ccu_reset(rcdev);
	const struct ccu_reset_map *map = &ccu->reset_map[id];

	/*
	 * The reset control API expects 0 if reset is not asserted,
	 * which is the opposite of what our hardware uses.
	 */
	return !(map->bit & readl(ccu->base + map->reg));
}

const struct reset_control_ops ccu_reset_ops = {
	.assert		= ccu_reset_assert,
	.deassert	= ccu_reset_deassert,
	.reset		= ccu_reset_reset,
	.status		= ccu_reset_status,
};
