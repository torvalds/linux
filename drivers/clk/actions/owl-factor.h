/* SPDX-License-Identifier: GPL-2.0+ */
//
// OWL factor clock driver
//
// Copyright (c) 2014 Actions Semi Inc.
// Author: David Liu <liuwei@actions-semi.com>
//
// Copyright (c) 2018 Linaro Ltd.
// Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>

#ifndef _OWL_FACTOR_H_
#define _OWL_FACTOR_H_

#include "owl-common.h"

struct clk_factor_table {
	unsigned int		val;
	unsigned int		mul;
	unsigned int		div;
};

struct owl_factor_hw {
	u32			reg;
	u8			shift;
	u8			width;
	u8			fct_flags;
	struct clk_factor_table	*table;
};

struct owl_factor {
	struct owl_factor_hw	factor_hw;
	struct owl_clk_common	common;
};

#define OWL_FACTOR_HW(_reg, _shift, _width, _fct_flags, _table)		\
	{								\
		.reg		= _reg,					\
		.shift		= _shift,				\
		.width		= _width,				\
		.fct_flags	= _fct_flags,				\
		.table		= _table,				\
	}

#define OWL_FACTOR(_struct, _name, _parent, _reg,			\
		   _shift, _width, _table, _fct_flags, _flags)		\
	struct owl_factor _struct = {					\
		.factor_hw = OWL_FACTOR_HW(_reg, _shift,		\
					   _width, _fct_flags, _table),	\
		.common = {						\
			.regmap		= NULL,				\
			.hw.init	= CLK_HW_INIT(_name,		\
						      _parent,		\
						      &owl_factor_ops,	\
						      _flags),		\
		},							\
	}

#define div_mask(d) ((1 << ((d)->width)) - 1)

static inline struct owl_factor *hw_to_owl_factor(const struct clk_hw *hw)
{
	struct owl_clk_common *common = hw_to_owl_clk_common(hw);

	return container_of(common, struct owl_factor, common);
}

long owl_factor_helper_round_rate(struct owl_clk_common *common,
				const struct owl_factor_hw *factor_hw,
				unsigned long rate,
				unsigned long *parent_rate);

unsigned long owl_factor_helper_recalc_rate(struct owl_clk_common *common,
					 const struct owl_factor_hw *factor_hw,
					 unsigned long parent_rate);

int owl_factor_helper_set_rate(const struct owl_clk_common *common,
				const struct owl_factor_hw *factor_hw,
				unsigned long rate,
				unsigned long parent_rate);

extern const struct clk_ops owl_factor_ops;

#endif /* _OWL_FACTOR_H_ */
