// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MMP PMU power island support
 *
 * Copyright (C) 2020 Lubomir Rintel <lkundrak@v3.sk>
 */

#include <linux/pm_domain.h>
#include <linux/slab.h>
#include <linux/io.h>

#include "clk.h"

#define to_mmp_pm_domain(genpd) container_of(genpd, struct mmp_pm_domain, genpd)

struct mmp_pm_domain {
	struct generic_pm_domain genpd;
	void __iomem *reg;
	spinlock_t *lock;
	u32 power_on;
	u32 reset;
	u32 clock_enable;
	unsigned int flags;
};

static int mmp_pm_domain_power_on(struct generic_pm_domain *genpd)
{
	struct mmp_pm_domain *pm_domain = to_mmp_pm_domain(genpd);
	unsigned long flags = 0;
	u32 val;

	if (pm_domain->lock)
		spin_lock_irqsave(pm_domain->lock, flags);

	val = readl(pm_domain->reg);

	/* Turn on the power island */
	val |= pm_domain->power_on;
	writel(val, pm_domain->reg);

	/* Disable isolation */
	val |= 0x100;
	writel(val, pm_domain->reg);

	/* Some blocks need to be reset after a power up */
	if (pm_domain->reset || pm_domain->clock_enable) {
		u32 after_power_on = val;

		val &= ~pm_domain->reset;
		writel(val, pm_domain->reg);

		val |= pm_domain->clock_enable;
		writel(val, pm_domain->reg);

		val |= pm_domain->reset;
		writel(val, pm_domain->reg);

		writel(after_power_on, pm_domain->reg);
	}

	if (pm_domain->lock)
		spin_unlock_irqrestore(pm_domain->lock, flags);

	return 0;
}

static int mmp_pm_domain_power_off(struct generic_pm_domain *genpd)
{
	struct mmp_pm_domain *pm_domain = to_mmp_pm_domain(genpd);
	unsigned long flags = 0;
	u32 val;

	if (pm_domain->flags & MMP_PM_DOMAIN_NO_DISABLE)
		return 0;

	if (pm_domain->lock)
		spin_lock_irqsave(pm_domain->lock, flags);

	/* Turn off and isolate the the power island. */
	val = readl(pm_domain->reg);
	val &= ~pm_domain->power_on;
	val &= ~0x100;
	writel(val, pm_domain->reg);

	if (pm_domain->lock)
		spin_unlock_irqrestore(pm_domain->lock, flags);

	return 0;
}

struct generic_pm_domain *mmp_pm_domain_register(const char *name,
		void __iomem *reg,
		u32 power_on, u32 reset, u32 clock_enable,
		unsigned int flags, spinlock_t *lock)
{
	struct mmp_pm_domain *pm_domain;

	pm_domain = kzalloc(sizeof(*pm_domain), GFP_KERNEL);
	if (!pm_domain)
		return ERR_PTR(-ENOMEM);

	pm_domain->reg = reg;
	pm_domain->power_on = power_on;
	pm_domain->reset = reset;
	pm_domain->clock_enable = clock_enable;
	pm_domain->flags = flags;
	pm_domain->lock = lock;

	pm_genpd_init(&pm_domain->genpd, NULL, true);
	pm_domain->genpd.name = name;
	pm_domain->genpd.power_on = mmp_pm_domain_power_on;
	pm_domain->genpd.power_off = mmp_pm_domain_power_off;

	return &pm_domain->genpd;
}
