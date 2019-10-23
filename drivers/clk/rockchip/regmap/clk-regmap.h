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

struct clk_pll_data {
	unsigned int id;
	const char *name;
	const char *parent_name;
	u32 reg;
	u8 pd_shift;
	u8 dsmpd_shift;
	u8 lock_shift;
	unsigned long flags;
};

#define PLL(_id, _name, _parent_name, _reg, _pd_shift, _dsmpd_shift, \
	    _lock_shift, _flags) \
{ \
	.id = _id, \
	.name = _name, \
	.parent_name = _parent_name, \
	.reg = _reg, \
	.pd_shift = _pd_shift, \
	.dsmpd_shift = _dsmpd_shift, \
	.lock_shift = _lock_shift, \
	.flags = _flags, \
}

#define RK618_PLL(_id, _name, _parent_name, _reg, _flags) \
	PLL(_id, _name, _parent_name, _reg, 10, 9, 15, _flags)

#define RK628_PLL(_id, _name, _parent_name, _reg, _flags) \
	PLL(_id, _name, _parent_name, _reg, 13, 12, 10, _flags)

struct clk_mux_data {
	unsigned int id;
	const char *name;
	const char *const *parent_names;
	u8 num_parents;
	u32 reg;
	u8 shift;
	u8 width;
	unsigned long flags;
};

#define MUX(_id, _name, _parent_names, _reg, _shift, _width, _flags) \
{ \
	.id = _id, \
	.name = _name, \
	.parent_names = _parent_names, \
	.num_parents = ARRAY_SIZE(_parent_names), \
	.reg = _reg, \
	.shift = _shift, \
	.width = _width, \
	.flags = _flags, \
}

struct clk_gate_data {
	unsigned int id;
	const char *name;
	const char *parent_name;
	u32 reg;
	u8 shift;
	unsigned long flags;
};

#define GATE(_id, _name, _parent_name, _reg, _shift, _flags) \
{ \
	.id = _id, \
	.name = _name, \
	.parent_name = _parent_name, \
	.reg = _reg, \
	.shift = _shift, \
	.flags = _flags, \
}

struct clk_divider_data {
	unsigned int id;
	const char *name;
	const char *parent_name;
	u32 reg;
	u8 shift;
	u8 width;
	unsigned long flags;
};

#define DIV(_id, _name, _parent_name, _reg, _shift, _width, _flags) \
{ \
	.id = _id, \
	.name = _name, \
	.parent_name = _parent_name, \
	.reg = _reg, \
	.shift = _shift, \
	.width = _width, \
	.flags = _flags, \
}

struct clk_composite_data {
	unsigned int id;
	const char *name;
	const char *const *parent_names;
	u8 num_parents;
	u32 mux_reg;
	u8 mux_shift;
	u8 mux_width;
	u32 div_reg;
	u8 div_shift;
	u8 div_width;
	u8 div_flags;
	u32 gate_reg;
	u8 gate_shift;
	unsigned long flags;
};

#define COMPOSITE(_id, _name, _parent_names, \
		  _mux_reg, _mux_shift, _mux_width, \
		  _div_reg, _div_shift, _div_width, \
		  _gate_reg, _gate_shift, _flags) \
{ \
	.id = _id, \
	.name = _name, \
	.parent_names = _parent_names, \
	.num_parents = ARRAY_SIZE(_parent_names), \
	.mux_reg = _mux_reg, \
	.mux_shift = _mux_shift, \
	.mux_width = _mux_width, \
	.div_reg = _div_reg, \
	.div_shift = _div_shift, \
	.div_width = _div_width, \
	.div_flags = CLK_DIVIDER_HIWORD_MASK, \
	.gate_reg = _gate_reg, \
	.gate_shift = _gate_shift, \
	.flags = _flags, \
}

#define COMPOSITE_NOMUX(_id, _name, _parent_name, \
			_div_reg, _div_shift, _div_width, \
			_gate_reg, _gate_shift, _flags) \
{ \
	.id = _id, \
	.name = _name, \
	.parent_names = (const char *[]){ _parent_name }, \
	.num_parents = 1, \
	.div_reg = _div_reg, \
	.div_shift = _div_shift, \
	.div_width = _div_width, \
	.div_flags = CLK_DIVIDER_HIWORD_MASK, \
	.gate_reg = _gate_reg, \
	.gate_shift = _gate_shift, \
	.flags = _flags, \
}

#define COMPOSITE_NODIV(_id, _name, _parent_names, \
			_mux_reg, _mux_shift, _mux_width, \
			_gate_reg, _gate_shift, _flags) \
	COMPOSITE(_id, _name, _parent_names, \
		 _mux_reg, _mux_shift, _mux_width, \
		 0, 0, 0, \
		 _gate_reg, _gate_shift, _flags)

#define COMPOSITE_FRAC(_id, _name, _parent_names, \
		       _mux_reg, _mux_shift, _mux_width, \
		       _div_reg, \
		       _gate_reg, _gate_shift, _flags) \
{ \
	.id = _id, \
	.name = _name, \
	.parent_names = _parent_names, \
	.num_parents = ARRAY_SIZE(_parent_names), \
	.mux_reg = _mux_reg, \
	.mux_shift = _mux_shift, \
	.mux_width = _mux_width, \
	.div_reg = _div_reg, \
	.gate_reg = _gate_reg, \
	.gate_shift = _gate_shift, \
	.flags = _flags, \
}

#define COMPOSITE_FRAC_NOMUX(_id, _name, _parent_name, \
			     _div_reg, \
			     _gate_reg, _gate_shift, _flags) \
{ \
	.id = _id, \
	.name = _name, \
	.parent_names = (const char *[]){ _parent_name }, \
	.num_parents = 1, \
	.div_reg = _div_reg, \
	.gate_reg = _gate_reg, \
	.gate_shift = _gate_shift, \
	.flags = _flags, \
}

#define COMPOSITE_FRAC_NOGATE(_id, _name, _parent_names, \
			      _mux_reg, _mux_shift, _mux_width, \
			      _div_reg, \
			      _flags) \
	COMPOSITE_FRAC(_id, _name, _parent_names, \
		       _mux_reg, _mux_shift, _mux_width, \
			_div_reg, 0, 0, _flags)

struct clk_regmap_fractional_divider {
	struct clk_hw hw;
	struct device *dev;
	struct regmap *regmap;
	u32 reg;
	u8 mshift;
	u8 mwidth;
	u32 mmask;
	u8 nshift;
	u8 nwidth;
	u32 nmask;
};

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
extern const struct clk_ops clk_regmap_fractional_divider_ops;

struct clk *
devm_clk_regmap_register_pll(struct device *dev, const char *name,
			     const char *parent_name,
			     struct regmap *regmap, u32 reg, u8 pd_shift,
			     u8 dsmpd_shift, u8 lock_shift,
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
devm_clk_regmap_register_fractional_divider(struct device *dev,
					    const char *name,
					    const char *parent_name,
					    struct regmap *regmap,
					    u32 reg, unsigned long flags);

struct clk *
devm_clk_regmap_register_composite(struct device *dev, const char *name,
				   const char *const *parent_names,
				   u8 num_parents, struct regmap *regmap,
				   u32 mux_reg, u8 mux_shift, u8 mux_width,
				   u32 div_reg, u8 div_shift, u8 div_width,
				   u8 div_flags,
				   u32 gate_reg, u8 gate_shift,
				   unsigned long flags);

#endif
