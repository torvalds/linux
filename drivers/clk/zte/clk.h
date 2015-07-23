/*
 * Copyright 2015 Linaro Ltd.
 * Copyright (C) 2014 ZTE Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ZTE_CLK_H
#define __ZTE_CLK_H
#include <linux/clk-provider.h>
#include <linux/spinlock.h>

struct zx_pll_config {
	unsigned long rate;
	u32 cfg0;
	u32 cfg1;
};

struct clk_zx_pll {
	struct clk_hw hw;
	void __iomem *reg_base;
	const struct zx_pll_config *lookup_table; /* order by rate asc */
	int count;
	spinlock_t *lock;
};

struct clk *clk_register_zx_pll(const char *name, const char *parent_name,
	unsigned long flags, void __iomem *reg_base,
	const struct zx_pll_config *lookup_table, int count, spinlock_t *lock);

struct clk_zx_audio {
	struct clk_hw hw;
	void __iomem *reg_base;
};

struct clk *clk_register_zx_audio(const char *name,
				  const char * const parent_name,
				  unsigned long flags, void __iomem *reg_base);
#endif
