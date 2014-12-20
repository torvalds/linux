/*
 * clk-max-gen.h - Generic clock driver for Maxim PMICs clocks
 *
 * Copyright (C) 2014 Google, Inc
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __CLK_MAX_GEN_H__
#define __CLK_MAX_GEN_H__

#include <linux/types.h>
#include <linux/device.h>
#include <linux/clkdev.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>

int max_gen_clk_probe(struct platform_device *pdev, struct regmap *regmap,
		      u32 reg, struct clk_init_data *clks_init, int num_init);
int max_gen_clk_remove(struct platform_device *pdev, int num_init);
extern struct clk_ops max_gen_clk_ops;

#endif /* __CLK_MAX_GEN_H__ */
