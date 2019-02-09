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

#ifndef _CCU_NK_H_
#define _CCU_NK_H_

#include <linux/clk-provider.h>

#include "ccu_common.h"
#include "ccu_div.h"
#include "ccu_mult.h"

/*
 * struct ccu_nk - Definition of an N-K clock
 *
 * Clocks based on the formula parent * N * K
 */
struct ccu_nk {
	u16			reg;
	u32			enable;
	u32			lock;

	struct ccu_mult_internal	n;
	struct ccu_mult_internal	k;

	unsigned int		fixed_post_div;

	struct ccu_common	common;
};

#define SUNXI_CCU_NK_WITH_GATE_LOCK_POSTDIV(_struct, _name, _parent, _reg, \
					    _nshift, _nwidth,		\
					    _kshift, _kwidth,		\
					    _gate, _lock, _postdiv,	\
					    _flags)			\
	struct ccu_nk _struct = {					\
		.enable		= _gate,				\
		.lock		= _lock,				\
		.k		= _SUNXI_CCU_MULT(_kshift, _kwidth),	\
		.n		= _SUNXI_CCU_MULT(_nshift, _nwidth),	\
		.fixed_post_div	= _postdiv,				\
		.common		= {					\
			.reg		= _reg,				\
			.features	= CCU_FEATURE_FIXED_POSTDIV,	\
			.hw.init	= CLK_HW_INIT(_name,		\
						      _parent,		\
						      &ccu_nk_ops,	\
						      _flags),		\
		},							\
	}

static inline struct ccu_nk *hw_to_ccu_nk(struct clk_hw *hw)
{
	struct ccu_common *common = hw_to_ccu_common(hw);

	return container_of(common, struct ccu_nk, common);
}

extern const struct clk_ops ccu_nk_ops;

#endif /* _CCU_NK_H_ */
