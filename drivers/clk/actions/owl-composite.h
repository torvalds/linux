/* SPDX-License-Identifier: GPL-2.0+ */
//
// OWL composite clock driver
//
// Copyright (c) 2014 Actions Semi Inc.
// Author: David Liu <liuwei@actions-semi.com>
//
// Copyright (c) 2018 Linaro Ltd.
// Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>

#ifndef _OWL_COMPOSITE_H_
#define _OWL_COMPOSITE_H_

#include "owl-common.h"
#include "owl-mux.h"
#include "owl-gate.h"
#include "owl-factor.h"
#include "owl-fixed-factor.h"
#include "owl-divider.h"

union owl_rate {
	struct owl_divider_hw	div_hw;
	struct owl_factor_hw	factor_hw;
	struct clk_fixed_factor	fix_fact_hw;
};

struct owl_composite {
	struct owl_mux_hw	mux_hw;
	struct owl_gate_hw	gate_hw;
	union owl_rate		rate;

	const struct clk_ops	*fix_fact_ops;

	struct owl_clk_common	common;
};

#define OWL_COMP_DIV(_struct, _name, _parent,				\
		     _mux, _gate, _div, _flags)				\
	struct owl_composite _struct = {				\
		.mux_hw		= _mux,					\
		.gate_hw	= _gate,				\
		.rate.div_hw	= _div,					\
		.common = {						\
			.regmap		= NULL,				\
			.hw.init	= CLK_HW_INIT_PARENTS(_name,	\
						     _parent,		\
						      &owl_comp_div_ops,\
						     _flags),		\
		},							\
	}

#define OWL_COMP_DIV_FIXED(_struct, _name, _parent,			\
		     _gate, _div, _flags)				\
	struct owl_composite _struct = {				\
		.gate_hw	= _gate,				\
		.rate.div_hw	= _div,					\
		.common = {						\
			.regmap		= NULL,				\
			.hw.init	= CLK_HW_INIT(_name,		\
						     _parent,		\
						      &owl_comp_div_ops,\
						     _flags),		\
		},							\
	}

#define OWL_COMP_FACTOR(_struct, _name, _parent,			\
			_mux, _gate, _factor, _flags)			\
	struct owl_composite _struct = {				\
		.mux_hw		= _mux,					\
		.gate_hw	= _gate,				\
		.rate.factor_hw	= _factor,				\
		.common = {						\
			.regmap		= NULL,				\
			.hw.init	= CLK_HW_INIT_PARENTS(_name,	\
						     _parent,		\
						     &owl_comp_fact_ops,\
						     _flags),		\
		},							\
	}

#define OWL_COMP_FIXED_FACTOR(_struct, _name, _parent,			\
			_gate, _mul, _div, _flags)			\
	struct owl_composite _struct = {				\
		.gate_hw		= _gate,			\
		.rate.fix_fact_hw.mult	= _mul,				\
		.rate.fix_fact_hw.div	= _div,				\
		.fix_fact_ops		= &clk_fixed_factor_ops,	\
		.common = {						\
			.regmap		= NULL,				\
			.hw.init	= CLK_HW_INIT(_name,		\
						 _parent,		\
						 &owl_comp_fix_fact_ops,\
						 _flags),		\
		},							\
	}

#define OWL_COMP_PASS(_struct, _name, _parent,				\
		      _mux, _gate, _flags)				\
	struct owl_composite _struct = {				\
		.mux_hw		= _mux,					\
		.gate_hw	= _gate,				\
		.common = {						\
			.regmap		= NULL,				\
			.hw.init	= CLK_HW_INIT_PARENTS(_name,	\
						     _parent,		\
						     &owl_comp_pass_ops,\
						     _flags),		\
		},							\
	}

static inline struct owl_composite *hw_to_owl_comp(const struct clk_hw *hw)
{
	struct owl_clk_common *common = hw_to_owl_clk_common(hw);

	return container_of(common, struct owl_composite, common);
}

extern const struct clk_ops owl_comp_div_ops;
extern const struct clk_ops owl_comp_fact_ops;
extern const struct clk_ops owl_comp_fix_fact_ops;
extern const struct clk_ops owl_comp_pass_ops;
extern const struct clk_ops clk_fixed_factor_ops;

#endif /* _OWL_COMPOSITE_H_ */
