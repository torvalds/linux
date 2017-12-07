// SPDX-License-Identifier: GPL-2.0
//
// Spreadtrum divider clock driver
//
// Copyright (C) 2017 Spreadtrum, Inc.
// Author: Chunyan Zhang <chunyan.zhang@spreadtrum.com>

#ifndef _SPRD_DIV_H_
#define _SPRD_DIV_H_

#include "common.h"

/**
 * struct sprd_div_internal - Internal divider description
 * @shift: Bit offset of the divider in its register
 * @width: Width of the divider field in its register
 *
 * That structure represents a single divider, and is meant to be
 * embedded in other structures representing the various clock
 * classes.
 */
struct sprd_div_internal {
	u8	shift;
	u8	width;
};

#define _SPRD_DIV_CLK(_shift, _width)	\
	{				\
		.shift	= _shift,	\
		.width	= _width,	\
	}

struct sprd_div {
	struct sprd_div_internal	div;
	struct sprd_clk_common	common;
};

#define SPRD_DIV_CLK(_struct, _name, _parent, _reg,			\
			_shift, _width, _flags)				\
	struct sprd_div _struct = {					\
		.div	= _SPRD_DIV_CLK(_shift, _width),		\
		.common	= {						\
			.regmap		= NULL,				\
			.reg		= _reg,				\
			.hw.init	= CLK_HW_INIT(_name,		\
						      _parent,		\
						      &sprd_div_ops,	\
						      _flags),		\
		}							\
	}

static inline struct sprd_div *hw_to_sprd_div(const struct clk_hw *hw)
{
	struct sprd_clk_common *common = hw_to_sprd_clk_common(hw);

	return container_of(common, struct sprd_div, common);
}

long sprd_div_helper_round_rate(struct sprd_clk_common *common,
				const struct sprd_div_internal *div,
				unsigned long rate,
				unsigned long *parent_rate);

unsigned long sprd_div_helper_recalc_rate(struct sprd_clk_common *common,
					  const struct sprd_div_internal *div,
					  unsigned long parent_rate);

int sprd_div_helper_set_rate(const struct sprd_clk_common *common,
			     const struct sprd_div_internal *div,
			     unsigned long rate,
			     unsigned long parent_rate);

extern const struct clk_ops sprd_div_ops;

#endif /* _SPRD_DIV_H_ */
