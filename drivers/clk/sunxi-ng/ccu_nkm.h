/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016 Maxime Ripard. All rights reserved.
 */

#ifndef _CCU_NKM_H_
#define _CCU_NKM_H_

#include <linux/clk-provider.h>

#include "ccu_common.h"
#include "ccu_div.h"
#include "ccu_mult.h"

/*
 * struct ccu_nkm - Definition of an N-K-M clock
 *
 * Clocks based on the formula parent * N * K / M
 */
struct ccu_nkm {
	u32			enable;
	u32			lock;

	struct ccu_mult_internal	n;
	struct ccu_mult_internal	k;
	struct ccu_div_internal		m;
	struct ccu_mux_internal	mux;

	unsigned int		fixed_post_div;

	struct ccu_common	common;
};

#define SUNXI_CCU_NKM_WITH_MUX_GATE_LOCK(_struct, _name, _parents, _reg, \
					 _nshift, _nwidth,		\
					 _kshift, _kwidth,		\
					 _mshift, _mwidth,		\
					 _muxshift, _muxwidth,		\
					 _gate, _lock, _flags)		\
	struct ccu_nkm _struct = {					\
		.enable		= _gate,				\
		.lock		= _lock,				\
		.k		= _SUNXI_CCU_MULT(_kshift, _kwidth),	\
		.n		= _SUNXI_CCU_MULT(_nshift, _nwidth),	\
		.m		= _SUNXI_CCU_DIV(_mshift, _mwidth),	\
		.mux		= _SUNXI_CCU_MUX(_muxshift, _muxwidth),	\
		.common		= {					\
			.reg		= _reg,				\
			.hw.init	= CLK_HW_INIT_PARENTS(_name,	\
						      _parents,		\
						      &ccu_nkm_ops,	\
						      _flags),		\
		},							\
	}

#define SUNXI_CCU_NKM_WITH_GATE_LOCK(_struct, _name, _parent, _reg,	\
				     _nshift, _nwidth,			\
				     _kshift, _kwidth,			\
				     _mshift, _mwidth,			\
				     _gate, _lock, _flags)		\
	struct ccu_nkm _struct = {					\
		.enable		= _gate,				\
		.lock		= _lock,				\
		.k		= _SUNXI_CCU_MULT(_kshift, _kwidth),	\
		.n		= _SUNXI_CCU_MULT(_nshift, _nwidth),	\
		.m		= _SUNXI_CCU_DIV(_mshift, _mwidth),	\
		.common		= {					\
			.reg		= _reg,				\
			.hw.init	= CLK_HW_INIT(_name,		\
						      _parent,		\
						      &ccu_nkm_ops,	\
						      _flags),		\
		},							\
	}

static inline struct ccu_nkm *hw_to_ccu_nkm(struct clk_hw *hw)
{
	struct ccu_common *common = hw_to_ccu_common(hw);

	return container_of(common, struct ccu_nkm, common);
}

extern const struct clk_ops ccu_nkm_ops;

#endif /* _CCU_NKM_H_ */
