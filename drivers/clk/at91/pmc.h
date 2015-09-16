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
#include <linux/regmap.h>
#include <linux/spinlock.h>

extern spinlock_t pmc_pcr_lock;

struct clk_range {
	unsigned long min;
	unsigned long max;
};

#define CLK_RANGE(MIN, MAX) {.min = MIN, .max = MAX,}

struct at91_pmc_caps {
	u32 available_irqs;
};

struct at91_pmc {
	struct regmap *regmap;
	const struct at91_pmc_caps *caps;
};

int of_at91_get_clk_range(struct device_node *np, const char *propname,
			  struct clk_range *range);

#endif /* __PMC_H_ */
