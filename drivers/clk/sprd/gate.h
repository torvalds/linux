/* SPDX-License-Identifier: GPL-2.0 */
//
// Spreadtrum gate clock driver
//
// Copyright (C) 2017 Spreadtrum, Inc.
// Author: Chunyan Zhang <chunyan.zhang@spreadtrum.com>

#ifndef _SPRD_GATE_H_
#define _SPRD_GATE_H_

#include "common.h"

struct sprd_gate {
	u32			enable_mask;
	u16			flags;
	u16			sc_offset;
	u16			udelay;

	struct sprd_clk_common	common;
};

#define SPRD_SC_GATE_CLK_HW_INIT_FN(_struct, _name, _parent, _reg,	\
				    _sc_offset, _enable_mask, _flags,	\
				    _gate_flags, _udelay, _ops, _fn)	\
	struct sprd_gate _struct = {					\
		.enable_mask	= _enable_mask,				\
		.sc_offset	= _sc_offset,				\
		.flags		= _gate_flags,				\
		.udelay		= _udelay,				\
		.common	= {						\
			.regmap		= NULL,				\
			.reg		= _reg,				\
			.hw.init	= _fn(_name, _parent,		\
					      _ops, _flags),		\
		}							\
	}

#define SPRD_SC_GATE_CLK_OPS_UDELAY(_struct, _name, _parent, _reg,	\
				    _sc_offset, _enable_mask, _flags,	\
				    _gate_flags, _udelay, _ops)		\
	SPRD_SC_GATE_CLK_HW_INIT_FN(_struct, _name, _parent, _reg,	\
				    _sc_offset, _enable_mask, _flags,	\
				    _gate_flags, _udelay, _ops, CLK_HW_INIT)

#define SPRD_SC_GATE_CLK_OPS(_struct, _name, _parent, _reg, _sc_offset,	\
			     _enable_mask, _flags, _gate_flags, _ops)	\
	SPRD_SC_GATE_CLK_OPS_UDELAY(_struct, _name, _parent, _reg,	\
				    _sc_offset, _enable_mask, _flags,	\
				    _gate_flags, 0, _ops)

#define SPRD_SC_GATE_CLK(_struct, _name, _parent, _reg, _sc_offset,	\
			 _enable_mask, _flags, _gate_flags)		\
	SPRD_SC_GATE_CLK_OPS(_struct, _name, _parent, _reg, _sc_offset,	\
			     _enable_mask, _flags, _gate_flags,		\
			     &sprd_sc_gate_ops)

#define SPRD_GATE_CLK(_struct, _name, _parent, _reg,			\
		      _enable_mask, _flags, _gate_flags)		\
	SPRD_SC_GATE_CLK_OPS(_struct, _name, _parent, _reg, 0,		\
			     _enable_mask, _flags, _gate_flags,		\
			     &sprd_gate_ops)

#define SPRD_PLL_SC_GATE_CLK(_struct, _name, _parent, _reg, _sc_offset,	\
			     _enable_mask, _flags, _gate_flags,		\
			     _udelay)					\
	SPRD_SC_GATE_CLK_OPS_UDELAY(_struct, _name, _parent, _reg,	\
				    _sc_offset,	_enable_mask, _flags,	\
				    _gate_flags, _udelay,		\
				    &sprd_pll_sc_gate_ops)


#define SPRD_SC_GATE_CLK_HW_OPS_UDELAY(_struct, _name, _parent, _reg,	\
				       _sc_offset, _enable_mask,	\
				       _flags, _gate_flags,		\
				       _udelay, _ops)			\
	SPRD_SC_GATE_CLK_HW_INIT_FN(_struct, _name, _parent, _reg,	\
				    _sc_offset, _enable_mask, _flags,	\
				    _gate_flags, _udelay, _ops,		\
				    CLK_HW_INIT_HW)

#define SPRD_SC_GATE_CLK_HW_OPS(_struct, _name, _parent, _reg,		\
				_sc_offset, _enable_mask, _flags,	\
				_gate_flags, _ops)			\
	SPRD_SC_GATE_CLK_HW_OPS_UDELAY(_struct, _name, _parent, _reg,	\
				       _sc_offset, _enable_mask,	\
				       _flags, _gate_flags, 0, _ops)

#define SPRD_SC_GATE_CLK_HW(_struct, _name, _parent, _reg,		\
			    _sc_offset, _enable_mask, _flags,		\
			    _gate_flags)				\
	SPRD_SC_GATE_CLK_HW_OPS(_struct, _name, _parent, _reg,		\
				_sc_offset, _enable_mask, _flags,	\
				_gate_flags, &sprd_sc_gate_ops)

#define SPRD_GATE_CLK_HW(_struct, _name, _parent, _reg,			\
			 _enable_mask, _flags, _gate_flags)		\
	SPRD_SC_GATE_CLK_HW_OPS(_struct, _name, _parent, _reg, 0,	\
				_enable_mask, _flags, _gate_flags,	\
				&sprd_gate_ops)

#define SPRD_PLL_SC_GATE_CLK_HW(_struct, _name, _parent, _reg,		\
				_sc_offset, _enable_mask, _flags,	\
				_gate_flags, _udelay)			\
	SPRD_SC_GATE_CLK_HW_OPS_UDELAY(_struct, _name, _parent, _reg,	\
				       _sc_offset, _enable_mask,	\
				       _flags, _gate_flags, _udelay,	\
				       &sprd_pll_sc_gate_ops)

#define SPRD_SC_GATE_CLK_FW_NAME_OPS_UDELAY(_struct, _name, _parent,	\
					    _reg, _sc_offset,		\
					    _enable_mask, _flags,	\
					    _gate_flags, _udelay, _ops)	\
	SPRD_SC_GATE_CLK_HW_INIT_FN(_struct, _name, _parent, _reg,	\
				    _sc_offset, _enable_mask, _flags,	\
				    _gate_flags, _udelay, _ops,		\
				    CLK_HW_INIT_FW_NAME)

#define SPRD_SC_GATE_CLK_FW_NAME_OPS(_struct, _name, _parent, _reg,	\
				     _sc_offset, _enable_mask, _flags,	\
				     _gate_flags, _ops)			\
	SPRD_SC_GATE_CLK_FW_NAME_OPS_UDELAY(_struct, _name, _parent,	\
					    _reg, _sc_offset,		\
					    _enable_mask, _flags,	\
					    _gate_flags, 0, _ops)

#define SPRD_SC_GATE_CLK_FW_NAME(_struct, _name, _parent, _reg,		\
				 _sc_offset, _enable_mask, _flags,	\
				 _gate_flags)				\
	SPRD_SC_GATE_CLK_FW_NAME_OPS(_struct, _name, _parent, _reg,	\
				     _sc_offset, _enable_mask, _flags,	\
				     _gate_flags, &sprd_sc_gate_ops)

#define SPRD_GATE_CLK_FW_NAME(_struct, _name, _parent, _reg,		\
			      _enable_mask, _flags, _gate_flags)	\
	SPRD_SC_GATE_CLK_FW_NAME_OPS(_struct, _name, _parent, _reg, 0,	\
				     _enable_mask, _flags, _gate_flags,	\
				     &sprd_gate_ops)

#define SPRD_PLL_SC_GATE_CLK_FW_NAME(_struct, _name, _parent, _reg,	\
				     _sc_offset, _enable_mask, _flags,	\
				     _gate_flags, _udelay)		\
	SPRD_SC_GATE_CLK_FW_NAME_OPS_UDELAY(_struct, _name, _parent,	\
					    _reg, _sc_offset,		\
					    _enable_mask, _flags,	\
					    _gate_flags, _udelay,	\
					    &sprd_pll_sc_gate_ops)

static inline struct sprd_gate *hw_to_sprd_gate(const struct clk_hw *hw)
{
	struct sprd_clk_common *common = hw_to_sprd_clk_common(hw);

	return container_of(common, struct sprd_gate, common);
}

extern const struct clk_ops sprd_gate_ops;
extern const struct clk_ops sprd_sc_gate_ops;
extern const struct clk_ops sprd_pll_sc_gate_ops;

#endif /* _SPRD_GATE_H_ */
