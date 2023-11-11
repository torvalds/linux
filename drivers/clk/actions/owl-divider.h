/* SPDX-License-Identifier: GPL-2.0+ */
//
// OWL divider clock driver
//
// Copyright (c) 2014 Actions Semi Inc.
// Author: David Liu <liuwei@actions-semi.com>
//
// Copyright (c) 2018 Linaro Ltd.
// Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>

#ifndef _OWL_DIVIDER_H_
#define _OWL_DIVIDER_H_

#include "owl-common.h"

struct owl_divider_hw {
	u32			reg;
	u8			shift;
	u8			width;
	u8			div_flags;
	struct clk_div_table	*table;
};

struct owl_divider {
	struct owl_divider_hw	div_hw;
	struct owl_clk_common	common;
};

#define OWL_DIVIDER_HW(_reg, _shift, _width, _div_flags, _table)	\
	{								\
		.reg		= _reg,					\
		.shift		= _shift,				\
		.width		= _width,				\
		.div_flags	= _div_flags,				\
		.table		= _table,				\
	}

#define OWL_DIVIDER(_struct, _name, _parent, _reg,			\
		    _shift, _width, _table, _div_flags, _flags)		\
	struct owl_divider _struct = {					\
		.div_hw	= OWL_DIVIDER_HW(_reg, _shift, _width,		\
					 _div_flags, _table),		\
		.common = {						\
			.regmap		= NULL,				\
			.hw.init	= CLK_HW_INIT(_name,		\
						      _parent,		\
						      &owl_divider_ops,	\
						      _flags),		\
		},							\
	}

static inline struct owl_divider *hw_to_owl_divider(const struct clk_hw *hw)
{
	struct owl_clk_common *common = hw_to_owl_clk_common(hw);

	return container_of(common, struct owl_divider, common);
}

long owl_divider_helper_round_rate(struct owl_clk_common *common,
				const struct owl_divider_hw *div_hw,
				unsigned long rate,
				unsigned long *parent_rate);

unsigned long owl_divider_helper_recalc_rate(struct owl_clk_common *common,
					 const struct owl_divider_hw *div_hw,
					 unsigned long parent_rate);

int owl_divider_helper_set_rate(const struct owl_clk_common *common,
				const struct owl_divider_hw *div_hw,
				unsigned long rate,
				unsigned long parent_rate);

extern const struct clk_ops owl_divider_ops;

#endif /* _OWL_DIVIDER_H_ */
