/*
 * Copyright (C) 2014 Google, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#ifndef __PISTACHIO_CLK_H
#define __PISTACHIO_CLK_H

#include <linux/clk-provider.h>

struct pistachio_gate {
	unsigned int id;
	unsigned long reg;
	unsigned int shift;
	const char *name;
	const char *parent;
};

#define GATE(_id, _name, _pname, _reg, _shift)	\
	{					\
		.id	= _id,			\
		.reg	= _reg,			\
		.shift	= _shift,		\
		.name	= _name,		\
		.parent = _pname,		\
	}

struct pistachio_mux {
	unsigned int id;
	unsigned long reg;
	unsigned int shift;
	unsigned int num_parents;
	const char *name;
	const char **parents;
};

#define PNAME(x) static const char *x[] __initconst

#define MUX(_id, _name, _pnames, _reg, _shift)			\
	{							\
		.id		= _id,				\
		.reg		= _reg,				\
		.shift		= _shift,			\
		.name		= _name,			\
		.parents	= _pnames,			\
		.num_parents	= ARRAY_SIZE(_pnames)		\
	}


struct pistachio_div {
	unsigned int id;
	unsigned long reg;
	unsigned int width;
	unsigned int div_flags;
	const char *name;
	const char *parent;
};

#define DIV(_id, _name, _pname, _reg, _width)			\
	{							\
		.id		= _id,				\
		.reg		= _reg,				\
		.width		= _width,			\
		.div_flags	= 0,				\
		.name		= _name,			\
		.parent		= _pname,			\
	}

#define DIV_F(_id, _name, _pname, _reg, _width, _div_flags)	\
	{							\
		.id		= _id,				\
		.reg		= _reg,				\
		.width		= _width,			\
		.div_flags	= _div_flags,			\
		.name		= _name,			\
		.parent		= _pname,			\
	}

struct pistachio_fixed_factor {
	unsigned int id;
	unsigned int div;
	const char *name;
	const char *parent;
};

#define FIXED_FACTOR(_id, _name, _pname, _div)			\
	{							\
		.id		= _id,				\
		.div		= _div,				\
		.name		= _name,			\
		.parent		= _pname,			\
	}

struct pistachio_pll_rate_table {
	unsigned long long fref;
	unsigned long long fout;
	unsigned long long refdiv;
	unsigned long long fbdiv;
	unsigned long long postdiv1;
	unsigned long long postdiv2;
	unsigned long long frac;
};

enum pistachio_pll_type {
	PLL_GF40LP_LAINT,
	PLL_GF40LP_FRAC,
};

struct pistachio_pll {
	unsigned int id;
	unsigned long reg_base;
	enum pistachio_pll_type type;
	struct pistachio_pll_rate_table *rates;
	unsigned int nr_rates;
	const char *name;
	const char *parent;
};

#define PLL(_id, _name, _pname, _type, _reg, _rates)		\
	{							\
		.id		= _id,				\
		.reg_base	= _reg,				\
		.type		= _type,			\
		.rates		= _rates,			\
		.nr_rates	= ARRAY_SIZE(_rates),		\
		.name		= _name,			\
		.parent		= _pname,			\
	}

#define PLL_FIXED(_id, _name, _pname, _type, _reg)		\
	{							\
		.id		= _id,				\
		.reg_base	= _reg,				\
		.type		= _type,			\
		.rates		= NULL,				\
		.nr_rates	= 0,				\
		.name		= _name,			\
		.parent		= _pname,			\
	}

struct pistachio_clk_provider {
	struct device_node *node;
	void __iomem *base;
	struct clk_onecell_data clk_data;
};

extern struct pistachio_clk_provider *
pistachio_clk_alloc_provider(struct device_node *node, unsigned int num_clks);
extern void pistachio_clk_register_provider(struct pistachio_clk_provider *p);

extern void pistachio_clk_register_gate(struct pistachio_clk_provider *p,
					struct pistachio_gate *gate,
					unsigned int num);
extern void pistachio_clk_register_mux(struct pistachio_clk_provider *p,
				       struct pistachio_mux *mux,
				       unsigned int num);
extern void pistachio_clk_register_div(struct pistachio_clk_provider *p,
				       struct pistachio_div *div,
				       unsigned int num);
extern void
pistachio_clk_register_fixed_factor(struct pistachio_clk_provider *p,
				    struct pistachio_fixed_factor *ff,
				    unsigned int num);
extern void pistachio_clk_register_pll(struct pistachio_clk_provider *p,
				       struct pistachio_pll *pll,
				       unsigned int num);

extern void pistachio_clk_force_enable(struct pistachio_clk_provider *p,
				       unsigned int *clk_ids, unsigned int num);

#endif
