/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2012-2016 Zhang, Keguang <keguang.zhang@gmail.com>
 */

#ifndef __LOONGSON1_CLK_H
#define __LOONGSON1_CLK_H

struct clk_hw *clk_hw_register_pll(struct device *dev,
				   const char *name,
				   const char *parent_name,
				   const struct clk_ops *ops,
				   unsigned long flags);

#endif /* __LOONGSON1_CLK_H */
