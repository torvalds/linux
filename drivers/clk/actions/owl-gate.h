/* SPDX-License-Identifier: GPL-2.0+ */
//
// OWL gate clock driver
//
// Copyright (c) 2014 Actions Semi Inc.
// Author: David Liu <liuwei@actions-semi.com>
//
// Copyright (c) 2018 Linaro Ltd.
// Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>

#ifndef _OWL_GATE_H_
#define _OWL_GATE_H_

#include "owl-common.h"

struct owl_gate_hw {
	u32			reg;
	u8			bit_idx;
	u8			gate_flags;
};

struct owl_gate {
	struct owl_gate_hw	gate_hw;
	struct owl_clk_common	common;
};

#define OWL_GATE_HW(_reg, _bit_idx, _gate_flags)	\
	{						\
		.reg		= _reg,			\
		.bit_idx	= _bit_idx,		\
		.gate_flags	= _gate_flags,		\
	}

#define OWL_GATE(_struct, _name, _parent, _reg,				\
		_bit_idx, _gate_flags, _flags)				\
	struct owl_gate _struct = {					\
		.gate_hw = OWL_GATE_HW(_reg, _bit_idx, _gate_flags),	\
		.common = {						\
			.regmap		= NULL,				\
			.hw.init	= CLK_HW_INIT(_name,		\
						      _parent,		\
						      &owl_gate_ops,	\
						      _flags),		\
		}							\
	}								\

#define OWL_GATE_NO_PARENT(_struct, _name, _reg,			\
		_bit_idx, _gate_flags, _flags)				\
	struct owl_gate _struct = {					\
		.gate_hw = OWL_GATE_HW(_reg, _bit_idx, _gate_flags),	\
		.common = {						\
			.regmap		= NULL,				\
			.hw.init	= CLK_HW_INIT_NO_PARENT(_name,	\
						      &owl_gate_ops,	\
						      _flags),		\
		},							\
	}								\

static inline struct owl_gate *hw_to_owl_gate(const struct clk_hw *hw)
{
	struct owl_clk_common *common = hw_to_owl_clk_common(hw);

	return container_of(common, struct owl_gate, common);
}

void owl_gate_set(const struct owl_clk_common *common,
		 const struct owl_gate_hw *gate_hw, bool enable);
int owl_gate_clk_is_enabled(const struct owl_clk_common *common,
		   const struct owl_gate_hw *gate_hw);

extern const struct clk_ops owl_gate_ops;

#endif /* _OWL_GATE_H_ */
