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

#ifndef _CCU_NKMP_H_
#define _CCU_NKMP_H_

#include <linux/clk-provider.h>

#include "ccu_common.h"
#include "ccu_div.h"
#include "ccu_mult.h"

/*
 * struct ccu_nkmp - Definition of an N-K-M-P clock
 *
 * Clocks based on the formula parent * N * K >> P / M
 */
struct ccu_nkmp {
	u32			enable;
	u32			lock;

	struct _ccu_mult	n;
	struct _ccu_mult	k;
	struct _ccu_div		m;
	struct _ccu_div		p;

	struct ccu_common	common;
};

#define SUNXI_CCU_NKMP_WITH_GATE_LOCK(_struct, _name, _parent, _reg,	\
				      _nshift, _nwidth,			\
				      _kshift, _kwidth,			\
				      _mshift, _mwidth,			\
				      _pshift, _pwidth,			\
				      _gate, _lock, _flags)		\
	struct ccu_nkmp _struct = {					\
		.enable		= _gate,				\
		.lock		= _lock,				\
		.n		= _SUNXI_CCU_MULT(_nshift, _nwidth),	\
		.k		= _SUNXI_CCU_MULT(_kshift, _kwidth),	\
		.m		= _SUNXI_CCU_DIV(_mshift, _mwidth),	\
		.p		= _SUNXI_CCU_DIV(_pshift, _pwidth),	\
		.common		= {					\
			.reg		= _reg,				\
			.hw.init	= CLK_HW_INIT(_name,		\
						      _parent,		\
						      &ccu_nkmp_ops,	\
						      _flags),		\
		},							\
	}

static inline struct ccu_nkmp *hw_to_ccu_nkmp(struct clk_hw *hw)
{
	struct ccu_common *common = hw_to_ccu_common(hw);

	return container_of(common, struct ccu_nkmp, common);
}

extern const struct clk_ops ccu_nkmp_ops;

#endif /* _CCU_NKMP_H_ */
