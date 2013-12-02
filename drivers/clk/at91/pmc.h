/*
 * drivers/clk/at91/pmc.h
 *
 *  Copyright (C) 2013 Boris BREZILLON <b.brezillon@overkiz.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __PMC_H_
#define __PMC_H_

#include <linux/io.h>
#include <linux/irqdomain.h>
#include <linux/spinlock.h>

struct clk_range {
	unsigned long min;
	unsigned long max;
};

#define CLK_RANGE(MIN, MAX) {.min = MIN, .max = MAX,}

struct at91_pmc_caps {
	u32 available_irqs;
};

struct at91_pmc {
	void __iomem *regbase;
	int virq;
	spinlock_t lock;
	const struct at91_pmc_caps *caps;
	struct irq_domain *irqdomain;
};

static inline void pmc_lock(struct at91_pmc *pmc)
{
	spin_lock(&pmc->lock);
}

static inline void pmc_unlock(struct at91_pmc *pmc)
{
	spin_unlock(&pmc->lock);
}

static inline u32 pmc_read(struct at91_pmc *pmc, int offset)
{
	return readl(pmc->regbase + offset);
}

static inline void pmc_write(struct at91_pmc *pmc, int offset, u32 value)
{
	writel(value, pmc->regbase + offset);
}

int of_at91_get_clk_range(struct device_node *np, const char *propname,
			  struct clk_range *range);

#endif /* __PMC_H_ */
