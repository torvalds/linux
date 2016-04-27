/*
 *  Copyright (C) 2013 Boris BREZILLON <b.brezillon@overkiz.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/clk/at91_pmc.h>
#include <linux/of.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include <asm/proc-fns.h>

#include "pmc.h"

int of_at91_get_clk_range(struct device_node *np, const char *propname,
			  struct clk_range *range)
{
	u32 min, max;
	int ret;

	ret = of_property_read_u32_index(np, propname, 0, &min);
	if (ret)
		return ret;

	ret = of_property_read_u32_index(np, propname, 1, &max);
	if (ret)
		return ret;

	if (range) {
		range->min = min;
		range->max = max;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(of_at91_get_clk_range);
