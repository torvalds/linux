// SPDX-License-Identifier: GPL-2.0+
//
// OWL pll clock driver
//
// Copyright (c) 2014 Actions Semi Inc.
// Author: David Liu <liuwei@actions-semi.com>
//
// Copyright (c) 2018 Linaro Ltd.
// Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>

#ifndef _OWL_PLL_H_
#define _OWL_PLL_H_

#include "owl-common.h"

#define OWL_PLL_DEF_DELAY	50

/* last entry should have rate = 0 */
struct clk_pll_table {
	unsigned int		val;
	unsigned long		rate;
};

struct owl_pll_hw {
	u32			reg;
	u32			bfreq;
	u8			bit_idx;
	u8			shift;
	u8			width;
	u8			min_mul;
	u8			max_mul;
	u8			delay;
	const struct clk_pll_table *table;
};

struct owl_pll {
	struct owl_pll_hw	pll_hw;
	struct owl_clk_common	common;
};

#define OWL_PLL_HW(_reg, _bfreq, _bit_idx, _shift,			\
		   _width, _min_mul, _max_mul, _delay, _table)		\
	{								\
		.reg		= _reg,					\
		.bfreq		= _bfreq,				\
		.bit_idx	= _bit_idx,				\
		.shift		= _shift,				\
		.width		= _width,				\
		.min_mul	= _min_mul,				\
		.max_mul	= _max_mul,				\
		.delay		= _delay,				\
		.table		= _table,				\
	}

#define OWL_PLL(_struct, _name, _parent, _reg, _bfreq, _bit_idx,	\
		_shift, _width, _min_mul, _max_mul, _table, _flags)	\
	struct owl_pll _struct = {					\
		.pll_hw	= OWL_PLL_HW(_reg, _bfreq, _bit_idx, _shift,	\
				     _width, _min_mul, _max_mul,	\
				     OWL_PLL_DEF_DELAY,	_table),	\
		.common = {						\
			.regmap = NULL,					\
			.hw.init = CLK_HW_INIT(_name,			\
					       _parent,			\
					       &owl_pll_ops,		\
					       _flags),			\
		},							\
	}

#define OWL_PLL_NO_PARENT(_struct, _name, _reg, _bfreq, _bit_idx,	\
		_shift, _width, _min_mul, _max_mul, _table, _flags)	\
	struct owl_pll _struct = {					\
		.pll_hw	= OWL_PLL_HW(_reg, _bfreq, _bit_idx, _shift,	\
				     _width, _min_mul, _max_mul,	\
				     OWL_PLL_DEF_DELAY,	_table),	\
		.common = {						\
			.regmap = NULL,					\
			.hw.init = CLK_HW_INIT_NO_PARENT(_name,		\
					       &owl_pll_ops,		\
					       _flags),			\
		},							\
	}

#define OWL_PLL_NO_PARENT_DELAY(_struct, _name, _reg, _bfreq, _bit_idx,	\
		_shift, _width, _min_mul, _max_mul, _delay, _table,	\
		_flags)							\
	struct owl_pll _struct = {					\
		.pll_hw	= OWL_PLL_HW(_reg, _bfreq, _bit_idx, _shift,	\
				     _width, _min_mul,  _max_mul,	\
				     _delay, _table),			\
		.common = {						\
			.regmap = NULL,					\
			.hw.init = CLK_HW_INIT_NO_PARENT(_name,		\
					       &owl_pll_ops,		\
					       _flags),			\
		},							\
	}

#define mul_mask(m)		((1 << ((m)->width)) - 1)

static inline struct owl_pll *hw_to_owl_pll(const struct clk_hw *hw)
{
	struct owl_clk_common *common = hw_to_owl_clk_common(hw);

	return container_of(common, struct owl_pll, common);
}

extern const struct clk_ops owl_pll_ops;

#endif /* _OWL_PLL_H_ */
