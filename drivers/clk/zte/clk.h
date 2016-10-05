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

#define PNAME(x) static const char *x[]

#define CLK_HW_INIT(_name, _parent, _ops, _flags)			\
	&(struct clk_init_data) {					\
		.flags		= _flags,				\
		.name		= _name,				\
		.parent_names	= (const char *[]) { _parent },		\
		.num_parents	= 1,					\
		.ops		= _ops,					\
	}

#define CLK_HW_INIT_PARENTS(_name, _parents, _ops, _flags)		\
	&(struct clk_init_data) {					\
		.flags		= _flags,				\
		.name		= _name,				\
		.parent_names	= _parents,				\
		.num_parents	= ARRAY_SIZE(_parents),			\
		.ops		= _ops,					\
	}

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
	u8 pd_bit;		/* power down bit */
	u8 lock_bit;		/* pll lock flag bit */
};

#define PLL_RATE(_rate, _cfg0, _cfg1)	\
{					\
	.rate = _rate,			\
	.cfg0 = _cfg0,			\
	.cfg1 = _cfg1,			\
}

#define ZX_PLL(_name, _parent, _reg, _table, _pd, _lock)		\
{									\
	.reg_base	= (void __iomem *) _reg,			\
	.lookup_table	= _table,					\
	.count		= ARRAY_SIZE(_table),				\
	.pd_bit		= _pd,						\
	.lock_bit	= _lock,					\
	.hw.init	 = CLK_HW_INIT(_name, _parent, &zx_pll_ops,	\
				CLK_GET_RATE_NOCACHE),			\
}

#define ZX296718_PLL(_name, _parent, _reg, _table)			\
ZX_PLL(_name, _parent, _reg, _table, 0, 30)

struct zx_clk_gate {
	struct clk_gate gate;
	u16		id;
};

#define GATE(_id, _name, _parent, _reg, _bit, _flag, _gflags)		\
{									\
	.gate = {							\
		.reg = (void __iomem *) _reg,				\
		.bit_idx = (_bit),					\
		.flags = _gflags,					\
		.lock = &clk_lock,					\
		.hw.init = CLK_HW_INIT(_name,				\
					_parent,			\
					&clk_gate_ops,			\
					_flag | CLK_IGNORE_UNUSED),	\
	},								\
	.id	= _id,							\
}

struct zx_clk_fixed_factor {
	struct clk_fixed_factor factor;
	u16	id;
};

#define FFACTOR(_id, _name, _parent, _mult, _div, _flag)		\
{									\
	.factor = {							\
		.div		= _div,					\
		.mult		= _mult,				\
		.hw.init	= CLK_HW_INIT(_name,			\
					      _parent,			\
					      &clk_fixed_factor_ops,	\
					      _flag),			\
	},								\
	.id = _id,							\
}

struct zx_clk_mux {
	struct clk_mux mux;
	u16	id;
};

#define MUX_F(_id, _name, _parent, _reg, _shift, _width, _flag, _mflag)	\
{									\
	.mux = {							\
		.reg		= (void __iomem *) _reg,		\
		.mask		= BIT(_width) - 1,			\
		.shift		= _shift,				\
		.flags		= _mflag,				\
		.lock		= &clk_lock,				\
		.hw.init	= CLK_HW_INIT_PARENTS(_name,		\
						      _parent,		\
						      &clk_mux_ops,	\
						      _flag),		\
	},								\
	.id = _id,							\
}

#define MUX(_id, _name, _parent, _reg, _shift, _width)			\
MUX_F(_id, _name, _parent, _reg, _shift, _width, 0, 0)

struct zx_clk_div {
	struct clk_divider div;
	u16	id;
};

#define DIV_T(_id, _name, _parent, _reg, _shift, _width, _flag, _table)	\
{									\
	.div = {							\
		.reg		= (void __iomem *) _reg,		\
		.shift		= _shift,				\
		.width		= _width,				\
		.flags		= 0,					\
		.table		= _table,				\
		.lock		= &clk_lock,				\
		.hw.init	= CLK_HW_INIT(_name,			\
					      _parent,			\
					      &clk_divider_ops,		\
					      _flag),			\
	},								\
	.id = _id,							\
}

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

extern const struct clk_ops zx_pll_ops;
#endif
