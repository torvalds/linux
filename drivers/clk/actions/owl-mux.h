// SPDX-License-Identifier: GPL-2.0+
//
// OWL mux clock driver
//
// Copyright (c) 2014 Actions Semi Inc.
// Author: David Liu <liuwei@actions-semi.com>
//
// Copyright (c) 2018 Linaro Ltd.
// Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>

#ifndef _OWL_MUX_H_
#define _OWL_MUX_H_

#include "owl-common.h"

struct owl_mux_hw {
	u32			reg;
	u8			shift;
	u8			width;
};

struct owl_mux {
	struct owl_mux_hw	mux_hw;
	struct owl_clk_common	common;
};

#define OWL_MUX_HW(_reg, _shift, _width)		\
	{						\
		.reg	= _reg,				\
		.shift	= _shift,			\
		.width	= _width,			\
	}

#define OWL_MUX(_struct, _name, _parents, _reg,				\
		_shift, _width, _flags)					\
	struct owl_mux _struct = {					\
		.mux_hw	= OWL_MUX_HW(_reg, _shift, _width),		\
		.common = {						\
			.regmap = NULL,					\
			.hw.init = CLK_HW_INIT_PARENTS(_name,		\
						       _parents,	\
						       &owl_mux_ops,	\
						       _flags),		\
		},							\
	}

static inline struct owl_mux *hw_to_owl_mux(const struct clk_hw *hw)
{
	struct owl_clk_common *common = hw_to_owl_clk_common(hw);

	return container_of(common, struct owl_mux, common);
}

u8 owl_mux_helper_get_parent(const struct owl_clk_common *common,
			     const struct owl_mux_hw *mux_hw);
int owl_mux_helper_set_parent(const struct owl_clk_common *common,
			      struct owl_mux_hw *mux_hw, u8 index);

extern const struct clk_ops owl_mux_ops;

#endif /* _OWL_MUX_H_ */
