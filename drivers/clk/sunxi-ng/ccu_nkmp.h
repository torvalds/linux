/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016 Maxime Ripard. All rights reserved.
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

	struct ccu_mult_internal	n;
	struct ccu_mult_internal	k;
	struct ccu_div_internal		m;
	struct ccu_div_internal		p;

	unsigned int		fixed_post_div;
	unsigned int		max_rate;

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
