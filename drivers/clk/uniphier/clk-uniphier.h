/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2016 Socionext Inc.
 *   Author: Masahiro Yamada <yamada.masahiro@socionext.com>
 */

#ifndef __CLK_UNIPHIER_H__
#define __CLK_UNIPHIER_H__

struct clk_hw;
struct device;
struct regmap;

#define UNIPHIER_CLK_CPUGEAR_MAX_PARENTS	16
#define UNIPHIER_CLK_MUX_MAX_PARENTS		8

enum uniphier_clk_type {
	UNIPHIER_CLK_TYPE_CPUGEAR,
	UNIPHIER_CLK_TYPE_FIXED_FACTOR,
	UNIPHIER_CLK_TYPE_FIXED_RATE,
	UNIPHIER_CLK_TYPE_GATE,
	UNIPHIER_CLK_TYPE_MUX,
};

struct uniphier_clk_cpugear_data {
	const char *parent_names[UNIPHIER_CLK_CPUGEAR_MAX_PARENTS];
	unsigned int num_parents;
	unsigned int regbase;
	unsigned int mask;
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
		struct uniphier_clk_cpugear_data cpugear;
		struct uniphier_clk_fixed_factor_data factor;
		struct uniphier_clk_fixed_rate_data rate;
		struct uniphier_clk_gate_data gate;
		struct uniphier_clk_mux_data mux;
	} data;
};

#define UNIPHIER_CLK_CPUGEAR(_name, _idx, _regbase, _mask,	\
			     _num_parents, ...)			\
	{							\
		.name = (_name),				\
		.type = UNIPHIER_CLK_TYPE_CPUGEAR,		\
		.idx = (_idx),					\
		.data.cpugear = {				\
			.parent_names = { __VA_ARGS__ },	\
			.num_parents = (_num_parents),		\
			.regbase = (_regbase),			\
			.mask = (_mask)				\
		 },						\
	}

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

#define UNIPHIER_CLK_DIV(parent, div)				\
	UNIPHIER_CLK_FACTOR(parent "/" #div, -1, parent, 1, div)

#define UNIPHIER_CLK_DIV2(parent, div0, div1)			\
	UNIPHIER_CLK_DIV(parent, div0),				\
	UNIPHIER_CLK_DIV(parent, div1)

#define UNIPHIER_CLK_DIV3(parent, div0, div1, div2)		\
	UNIPHIER_CLK_DIV2(parent, div0, div1),			\
	UNIPHIER_CLK_DIV(parent, div2)

#define UNIPHIER_CLK_DIV4(parent, div0, div1, div2, div3)	\
	UNIPHIER_CLK_DIV2(parent, div0, div1),			\
	UNIPHIER_CLK_DIV2(parent, div2, div3)

#define UNIPHIER_CLK_DIV5(parent, div0, div1, div2, div3, div4)	\
	UNIPHIER_CLK_DIV4(parent, div0, div1, div2, div3),	\
	UNIPHIER_CLK_DIV(parent, div4)

struct clk_hw *uniphier_clk_register_cpugear(struct device *dev,
					     struct regmap *regmap,
					     const char *name,
				const struct uniphier_clk_cpugear_data *data);
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

extern const struct uniphier_clk_data uniphier_ld4_sys_clk_data[];
extern const struct uniphier_clk_data uniphier_pro4_sys_clk_data[];
extern const struct uniphier_clk_data uniphier_sld8_sys_clk_data[];
extern const struct uniphier_clk_data uniphier_pro5_sys_clk_data[];
extern const struct uniphier_clk_data uniphier_pxs2_sys_clk_data[];
extern const struct uniphier_clk_data uniphier_ld11_sys_clk_data[];
extern const struct uniphier_clk_data uniphier_ld20_sys_clk_data[];
extern const struct uniphier_clk_data uniphier_pxs3_sys_clk_data[];
extern const struct uniphier_clk_data uniphier_nx1_sys_clk_data[];
extern const struct uniphier_clk_data uniphier_ld4_mio_clk_data[];
extern const struct uniphier_clk_data uniphier_pro5_sd_clk_data[];
extern const struct uniphier_clk_data uniphier_ld4_peri_clk_data[];
extern const struct uniphier_clk_data uniphier_pro4_peri_clk_data[];
extern const struct uniphier_clk_data uniphier_pro4_sg_clk_data[];

#endif /* __CLK_UNIPHIER_H__ */
