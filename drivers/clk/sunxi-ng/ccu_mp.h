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

#ifndef _CCU_MP_H_
#define _CCU_MP_H_

#include <linux/clk-provider.h>

#include "ccu_common.h"
#include "ccu_div.h"
#include "ccu_mult.h"
#include "ccu_mux.h"

/*
 * struct ccu_mp - Definition of an M-P clock
 *
 * Clocks based on the formula parent >> P / M
 */
struct ccu_mp {
	u32			enable;

	struct ccu_div_internal		m;
	struct ccu_div_internal		p;
	struct ccu_mux_internal	mux;
	struct ccu_common	common;
};

#define SUNXI_CCU_MP_WITH_MUX_GATE(_struct, _name, _parents, _reg,	\
				   _mshift, _mwidth,			\
				   _pshift, _pwidth,			\
				   _muxshift, _muxwidth,		\
				   _gate, _flags)			\
	struct ccu_mp _struct = {					\
		.enable	= _gate,					\
		.m	= _SUNXI_CCU_DIV(_mshift, _mwidth),		\
		.p	= _SUNXI_CCU_DIV(_pshift, _pwidth),		\
		.mux	= _SUNXI_CCU_MUX(_muxshift, _muxwidth),		\
		.common	= {						\
			.reg		= _reg,				\
			.hw.init	= CLK_HW_INIT_PARENTS(_name,	\
							      _parents, \
							      &ccu_mp_ops, \
							      _flags),	\
		}							\
	}

#define SUNXI_CCU_MP_WITH_MUX(_struct, _name, _parents, _reg,		\
			      _mshift, _mwidth,				\
			      _pshift, _pwidth,				\
			      _muxshift, _muxwidth,			\
			      _flags)					\
	SUNXI_CCU_MP_WITH_MUX_GATE(_struct, _name, _parents, _reg,	\
				   _mshift, _mwidth,			\
				   _pshift, _pwidth,			\
				   _muxshift, _muxwidth,		\
				   0, _flags)

static inline struct ccu_mp *hw_to_ccu_mp(struct clk_hw *hw)
{
	struct ccu_common *common = hw_to_ccu_common(hw);

	return container_of(common, struct ccu_mp, common);
}

extern const struct clk_ops ccu_mp_ops;

#endif /* _CCU_MP_H_ */
