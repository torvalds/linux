// SPDX-License-Identifier: GPL-2.0
//
// Spreadtrum composite clock driver
//
// Copyright (C) 2017 Spreadtrum, Inc.
// Author: Chunyan Zhang <chunyan.zhang@spreadtrum.com>

#ifndef _SPRD_COMPOSITE_H_
#define _SPRD_COMPOSITE_H_

#include "common.h"
#include "mux.h"
#include "div.h"

struct sprd_comp {
	struct sprd_mux_ssel	mux;
	struct sprd_div_internal	div;
	struct sprd_clk_common	common;
};

#define SPRD_COMP_CLK_TABLE(_struct, _name, _parent, _reg, _table,	\
			_mshift, _mwidth, _dshift, _dwidth, _flags)	\
	struct sprd_comp _struct = {					\
		.mux	= _SPRD_MUX_CLK(_mshift, _mwidth, _table),	\
		.div	= _SPRD_DIV_CLK(_dshift, _dwidth),		\
		.common = {						\
			.regmap		= NULL,				\
			.reg		= _reg,				\
			.hw.init = CLK_HW_INIT_PARENTS(_name,		\
						       _parent,		\
						       &sprd_comp_ops,	\
						       _flags),		\
			 }						\
	}

#define SPRD_COMP_CLK(_struct, _name, _parent, _reg, _mshift,	\
			_mwidth, _dshift, _dwidth, _flags)	\
	SPRD_COMP_CLK_TABLE(_struct, _name, _parent, _reg,	\
			    NULL, _mshift, _mwidth,		\
			    _dshift, _dwidth, _flags)

static inline struct sprd_comp *hw_to_sprd_comp(const struct clk_hw *hw)
{
	struct sprd_clk_common *common = hw_to_sprd_clk_common(hw);

	return container_of(common, struct sprd_comp, common);
}

extern const struct clk_ops sprd_comp_ops;

#endif /* _SPRD_COMPOSITE_H_ */
