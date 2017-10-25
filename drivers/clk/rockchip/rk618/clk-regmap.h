/*
 * Copyright (c) 2017 Rockchip Electronics Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __CLK_REGMAP_H__
#define __CLK_REGMAP_H__

#include <linux/regmap.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/delay.h>

#define UPDATE(x, h, l)		(((x) << (l)) & GENMASK((h), (l)))
#define HIWORD_UPDATE(v, h, l)	(((v) << (l)) | (GENMASK((h), (l)) << 16))

struct clk_regmap_divider {
	struct clk_hw hw;
	struct device *dev;
	struct regmap *regmap;
	u32 reg;
	u8 shift;
	u8 width;
};

struct clk_regmap_gate {
	struct clk_hw hw;
	struct device *dev;
	struct regmap *regmap;
	u32 reg;
	u8 shift;
};

struct clk_regmap_mux {
	struct clk_hw hw;
	struct device *dev;
	struct regmap *regmap;
	u32 reg;
	u32 mask;
	u8 shift;
};

extern const struct clk_ops clk_regmap_mux_ops;
extern const struct clk_ops clk_regmap_divider_ops;
extern const struct clk_ops clk_regmap_gate_ops;

struct clk *
devm_clk_regmap_register_pll(struct device *dev, const char *name,
			     const char *parent_name,
			     struct regmap *regmap, u32 reg,
			     unsigned long flags);

struct clk *
devm_clk_regmap_register_mux(struct device *dev, const char *name,
			     const char * const *parent_names, u8 num_parents,
			     struct regmap *regmap, u32 reg, u8 shift, u8 width,
			     unsigned long flags);

struct clk *
devm_clk_regmap_register_divider(struct device *dev, const char *name,
				 const char *parent_name, struct regmap *regmap,
				 u32 reg, u8 shift, u8 width,
				 unsigned long flags);

struct clk *
devm_clk_regmap_register_gate(struct device *dev, const char *name,
			      const char *parent_name,
			      struct regmap *regmap, u32 reg, u8 shift,
			      unsigned long flags);

struct clk *
devm_clk_regmap_register_composite(struct device *dev, const char *name,
				   const char *const *parent_names,
				   u8 num_parents, struct regmap *regmap,
				   u32 mux_reg, u8 mux_shift, u8 mux_width,
				   u32 div_reg, u8 div_shift, u8 div_width,
				   u32 gate_reg, u8 gate_shift,
				   unsigned long flags);

#endif
