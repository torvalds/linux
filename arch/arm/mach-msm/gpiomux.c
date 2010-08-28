/* Copyright (c) 2010, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */
#include <linux/module.h>
#include <linux/spinlock.h>
#include "gpiomux.h"

static DEFINE_SPINLOCK(gpiomux_lock);

int msm_gpiomux_write(unsigned gpio,
		      gpiomux_config_t active,
		      gpiomux_config_t suspended)
{
	struct msm_gpiomux_config *cfg = msm_gpiomux_configs + gpio;
	unsigned long irq_flags;
	gpiomux_config_t setting;

	if (gpio >= GPIOMUX_NGPIOS)
		return -EINVAL;

	spin_lock_irqsave(&gpiomux_lock, irq_flags);

	if (active & GPIOMUX_VALID)
		cfg->active = active;

	if (suspended & GPIOMUX_VALID)
		cfg->suspended = suspended;

	setting = cfg->ref ? active : suspended;
	if (setting & GPIOMUX_VALID)
		__msm_gpiomux_write(gpio, setting);

	spin_unlock_irqrestore(&gpiomux_lock, irq_flags);
	return 0;
}
EXPORT_SYMBOL(msm_gpiomux_write);

int msm_gpiomux_get(unsigned gpio)
{
	struct msm_gpiomux_config *cfg = msm_gpiomux_configs + gpio;
	unsigned long irq_flags;

	if (gpio >= GPIOMUX_NGPIOS)
		return -EINVAL;

	spin_lock_irqsave(&gpiomux_lock, irq_flags);
	if (cfg->ref++ == 0 && cfg->active & GPIOMUX_VALID)
		__msm_gpiomux_write(gpio, cfg->active);
	spin_unlock_irqrestore(&gpiomux_lock, irq_flags);
	return 0;
}
EXPORT_SYMBOL(msm_gpiomux_get);

int msm_gpiomux_put(unsigned gpio)
{
	struct msm_gpiomux_config *cfg = msm_gpiomux_configs + gpio;
	unsigned long irq_flags;

	if (gpio >= GPIOMUX_NGPIOS)
		return -EINVAL;

	spin_lock_irqsave(&gpiomux_lock, irq_flags);
	BUG_ON(cfg->ref == 0);
	if (--cfg->ref == 0 && cfg->suspended & GPIOMUX_VALID)
		__msm_gpiomux_write(gpio, cfg->suspended);
	spin_unlock_irqrestore(&gpiomux_lock, irq_flags);
	return 0;
}
EXPORT_SYMBOL(msm_gpiomux_put);

static int __init gpiomux_init(void)
{
	unsigned n;

	for (n = 0; n < GPIOMUX_NGPIOS; ++n) {
		msm_gpiomux_configs[n].ref = 0;
		if (!(msm_gpiomux_configs[n].suspended & GPIOMUX_VALID))
			continue;
		__msm_gpiomux_write(n, msm_gpiomux_configs[n].suspended);
	}
	return 0;
}
postcore_initcall(gpiomux_init);
