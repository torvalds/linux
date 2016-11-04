/*
 * Copyright (c) 2016 Maxime Ripard. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _CCU_DIV_H_
#define _CCU_DIV_H_

#include <linux/clk-provider.h>

#include "ccu_common.h"
#include "ccu_mux.h"

/**
 * struct _ccu_div - Internal divider description
 * @shift: Bit offset of the divider in its register
 * @width: Width of the divider field in its register
 * @max: Maximum value allowed for that divider. This is the
 *       arithmetic value, not the maximum value to be set in the
 *       register.
 * @flags: clk_divider flags to apply on this divider
 * @table: Divider table pointer (if applicable)
 *
 * That structure represents a single divider, and is meant to be
 * embedded in other structures representing the various clock
 * classes.
 *
 * It is basically a wrapper around the clk_divider functions
 * arguments.
 */
struct _ccu_div {
	u8			shift;
	u8			width;

	u32			max;

	u32			flags;

	struct clk_div_table	*table;
};

#define _SUNXI_CCU_DIV_TABLE_FLAGS(_shift, _width, _table, _flags)	\
	{								\
		.shift	= _shift,					\
		.width	= _width,					\
		.flags	= _flags,					\
		.table	= _table,					\
	}

#define _SUNXI_CCU_DIV_TABLE(_shift, _width, _table)			\
	_SUNXI_CCU_DIV_TABLE_FLAGS(_shift, _width, _table, 0)

#define _SUNXI_CCU_DIV_MAX_FLAGS(_shift, _width, _max, _flags) \
	{								\
		.shift	= _shift,					\
		.width	= _width,					\
		.flags	= _flags,					\
		.max	= _max,						\
	}

#define _SUNXI_CCU_DIV_FLAGS(_shift, _width, _flags)			\
	_SUNXI_CCU_DIV_MAX_FLAGS(_shift, _width, 0, _flags)

#define _SUNXI_CCU_DIV_MAX(_shift, _width, _max)			\
	_SUNXI_CCU_DIV_MAX_FLAGS(_shift, _width, _max, 0)

#define _SUNXI_CCU_DIV(_shift, _width)					\
	_SUNXI_CCU_DIV_FLAGS(_shift, _width, 0)

struct ccu_div {
	u32			enable;

	struct _ccu_div		div;
	struct ccu_mux_internal	mux;
	struct ccu_common	common;
};

#define SUNXI_CCU_DIV_TABLE_WITH_GATE(_struct, _name, _parent, _reg,	\
				      _shift, _width,			\
				      _table, _gate, _flags)		\
	struct ccu_div _struct = {					\
		.div		= _SUNXI_CCU_DIV_TABLE(_shift, _width,	\
						       _table),		\
		.enable		= _gate,				\
		.common	= {						\
			.reg		= _reg,				\
			.hw.init	= CLK_HW_INIT(_name,		\
						      _parent,		\
						      &ccu_div_ops,	\
						      _flags),		\
		}							\
	}


#define SUNXI_CCU_DIV_TABLE(_struct, _name, _parent, _reg,		\
			    _shift, _width,				\
			    _table, _flags)				\
	SUNXI_CCU_DIV_TABLE_WITH_GATE(_struct, _name, _parent, _reg,	\
				      _shift, _width, _table, 0,	\
				      _flags)

#define SUNXI_CCU_M_WITH_MUX_TABLE_GATE(_struct, _name,			\
					_parents, _table,		\
					_reg,				\
					_mshift, _mwidth,		\
					_muxshift, _muxwidth,		\
					_gate, _flags)			\
	struct ccu_div _struct = {					\
		.enable	= _gate,					\
		.div	= _SUNXI_CCU_DIV(_mshift, _mwidth),		\
		.mux	= _SUNXI_CCU_MUX_TABLE(_muxshift, _muxwidth, _table), \
		.common	= {						\
			.reg		= _reg,				\
			.hw.init	= CLK_HW_INIT_PARENTS(_name,	\
							      _parents, \
							      &ccu_div_ops, \
							      _flags),	\
		},							\
	}

#define SUNXI_CCU_M_WITH_MUX_GATE(_struct, _name, _parents, _reg,	\
				  _mshift, _mwidth, _muxshift, _muxwidth, \
				  _gate, _flags)			\
	SUNXI_CCU_M_WITH_MUX_TABLE_GATE(_struct, _name,			\
					_parents, NULL,			\
					_reg, _mshift, _mwidth,		\
					_muxshift, _muxwidth,		\
					_gate, _flags)

#define SUNXI_CCU_M_WITH_MUX(_struct, _name, _parents, _reg,		\
			     _mshift, _mwidth, _muxshift, _muxwidth,	\
			     _flags)					\
	SUNXI_CCU_M_WITH_MUX_TABLE_GATE(_struct, _name,			\
					_parents, NULL,			\
					_reg, _mshift, _mwidth,		\
					_muxshift, _muxwidth,		\
					0, _flags)


#define SUNXI_CCU_M_WITH_GATE(_struct, _name, _parent, _reg,		\
			      _mshift, _mwidth,	_gate,			\
			      _flags)					\
	struct ccu_div _struct = {					\
		.enable	= _gate,					\
		.div	= _SUNXI_CCU_DIV(_mshift, _mwidth),		\
		.common	= {						\
			.reg		= _reg,				\
			.hw.init	= CLK_HW_INIT(_name,		\
						      _parent,		\
						      &ccu_div_ops,	\
						      _flags),		\
		},							\
	}

#define SUNXI_CCU_M(_struct, _name, _parent, _reg, _mshift, _mwidth,	\
		    _flags)						\
	SUNXI_CCU_M_WITH_GATE(_struct, _name, _parent, _reg,		\
			      _mshift, _mwidth, 0, _flags)

static inline struct ccu_div *hw_to_ccu_div(struct clk_hw *hw)
{
	struct ccu_common *common = hw_to_ccu_common(hw);

	return container_of(common, struct ccu_div, common);
}

extern const struct clk_ops ccu_div_ops;

#endif /* _CCU_DIV_H_ */
