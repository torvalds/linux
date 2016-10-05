/*
 * Copyright (C) 2016 Socionext Inc.
 *   Author: Masahiro Yamada <yamada.masahiro@socionext.com>
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

#ifndef __CLK_UNIPHIER_H__
#define __CLK_UNIPHIER_H__

struct clk_hw;
struct device;
struct regmap;

#define UNIPHIER_CLK_MUX_MAX_PARENTS	8

enum uniphier_clk_type {
	UNIPHIER_CLK_TYPE_FIXED_FACTOR,
	UNIPHIER_CLK_TYPE_FIXED_RATE,
	UNIPHIER_CLK_TYPE_GATE,
	UNIPHIER_CLK_TYPE_MUX,
};

struct uniphier_clk_fixed_factor_data {
	const char *parent_name;
	unsigned int mult;
	unsigned int div;
};

struct uniphier_clk_fixed_rate_data {
	unsigned long fixed_rate;
};

struct uniphier_clk_gate_data {
	const char *parent_name;
	unsigned int reg;
	unsigned int bit;
};

struct uniphier_clk_mux_data {
	const char *parent_names[UNIPHIER_CLK_MUX_MAX_PARENTS];
	unsigned int num_parents;
	unsigned int reg;
	unsigned int masks[UNIPHIER_CLK_MUX_MAX_PARENTS];
	unsigned int vals[UNIPHIER_CLK_MUX_MAX_PARENTS];
};

struct uniphier_clk_data {
	const char *name;
	enum uniphier_clk_type type;
	int idx;
	union {
		struct uniphier_clk_fixed_factor_data factor;
		struct uniphier_clk_fixed_rate_data rate;
		struct uniphier_clk_gate_data gate;
		struct uniphier_clk_mux_data mux;
	} data;
};

#define UNIPHIER_CLK_FACTOR(_name, _idx, _parent, _mult, _div)	\
	{							\
		.name = (_name),				\
		.type = UNIPHIER_CLK_TYPE_FIXED_FACTOR,		\
		.idx = (_idx),					\
		.data.factor = {				\
			.parent_name = (_parent),		\
			.mult = (_mult),			\
			.div = (_div),				\
		},						\
	}


#define UNIPHIER_CLK_GATE(_name, _idx, _parent, _reg, _bit)	\
	{							\
		.name = (_name),				\
		.type = UNIPHIER_CLK_TYPE_GATE,			\
		.idx = (_idx),					\
		.data.gate = {					\
			.parent_name = (_parent),		\
			.reg = (_reg),				\
			.bit = (_bit),				\
		},						\
	}


struct clk_hw *uniphier_clk_register_fixed_factor(struct device *dev,
						  const char *name,
			const struct uniphier_clk_fixed_factor_data *data);
struct clk_hw *uniphier_clk_register_fixed_rate(struct device *dev,
						const char *name,
			const struct uniphier_clk_fixed_rate_data *data);
struct clk_hw *uniphier_clk_register_gate(struct device *dev,
					  struct regmap *regmap,
					  const char *name,
				const struct uniphier_clk_gate_data *data);
struct clk_hw *uniphier_clk_register_mux(struct device *dev,
					 struct regmap *regmap,
					 const char *name,
				const struct uniphier_clk_mux_data *data);

extern const struct uniphier_clk_data uniphier_sld3_sys_clk_data[];
extern const struct uniphier_clk_data uniphier_ld4_sys_clk_data[];
extern const struct uniphier_clk_data uniphier_pro4_sys_clk_data[];
extern const struct uniphier_clk_data uniphier_sld8_sys_clk_data[];
extern const struct uniphier_clk_data uniphier_pro5_sys_clk_data[];
extern const struct uniphier_clk_data uniphier_pxs2_sys_clk_data[];
extern const struct uniphier_clk_data uniphier_ld11_sys_clk_data[];
extern const struct uniphier_clk_data uniphier_ld20_sys_clk_data[];
extern const struct uniphier_clk_data uniphier_sld3_mio_clk_data[];
extern const struct uniphier_clk_data uniphier_pro5_mio_clk_data[];
extern const struct uniphier_clk_data uniphier_ld4_peri_clk_data[];
extern const struct uniphier_clk_data uniphier_pro4_peri_clk_data[];

#endif /* __CLK_UNIPHIER_H__ */
