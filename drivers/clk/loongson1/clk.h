/*
 * Copyright (c) 2012-2016 Zhang, Keguang <keguang.zhang@gmail.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __LOONGSON1_CLK_H
#define __LOONGSON1_CLK_H

struct clk_hw *clk_hw_register_pll(struct device *dev,
				   const char *name,
				   const char *parent_name,
				   const struct clk_ops *ops,
				   unsigned long flags);

#endif /* __LOONGSON1_CLK_H */
