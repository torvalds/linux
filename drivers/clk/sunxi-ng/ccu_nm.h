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

#ifndef _CCU_NM_H_
#define _CCU_NM_H_

#include <linux/clk-provider.h>

#include "ccu_common.h"
#include "ccu_div.h"
#include "ccu_frac.h"
#include "ccu_mult.h"
#include "ccu_sdm.h"

/*
 * struct ccu_nm - Definition of an N-M clock
 *
 * Clocks based on the formula parent * N / M
 */
struct ccu_nm {
	u32			enable;
	u32			lock;

	struct ccu_mult_internal	n;
	struct ccu_div_internal		m;
	struct ccu_frac_internal	frac;
	struct ccu_sdm_internal		sdm;

	unsigned int		fixed_post_div;
	unsigned int		min_rate;
	unsigned int		max_rate;

	struct ccu_common	common;
};

#define SUNXI_CCU_NM_WITH_SDM_GATE_LOCK(_struct, _name, _parent, _reg,	\
					_nshift, _nwidth,		\
					_mshift, _mwidth,		\
					_sdm_table, _sdm_en,		\
					_sdm_reg, _sdm_reg_en,		\
					_gate, _lock, _flags)		\
	struct ccu_nm _struct = {					\
		.enable		= _gate,				\
		.lock		= _lock,				\
		.n		= _SUNXI_CCU_MULT(_nshift, _nwidth),	\
		.m		= _SUNXI_CCU_DIV(_mshift, _mwidth),	\
		.sdm		= _SUNXI_CCU_SDM(_sdm_table, _sdm_en,	\
						 _sdm_reg, _sdm_reg_en),\
		.common		= {					\
			.reg		= _reg,				\
			.features	= CCU_FEATURE_SIGMA_DELTA_MOD,	\
			.hw.init	= CLK_HW_INIT(_name,		\
						      _parent,		\
						      &ccu_nm_ops,	\
						      _flags),		\
		},							\
	}

#define SUNXI_CCU_NM_WITH_FRAC_GATE_LOCK(_struct, _name, _parent, _reg,	\
					 _nshift, _nwidth,		\
					 _mshift, _mwidth,		\
					 _frac_en, _frac_sel,		\
					 _frac_rate_0, _frac_rate_1,	\
					 _gate, _lock, _flags)		\
	struct ccu_nm _struct = {					\
		.enable		= _gate,				\
		.lock		= _lock,				\
		.n		= _SUNXI_CCU_MULT(_nshift, _nwidth),	\
		.m		= _SUNXI_CCU_DIV(_mshift, _mwidth),	\
		.frac		= _SUNXI_CCU_FRAC(_frac_en, _frac_sel,	\
						  _frac_rate_0,		\
						  _frac_rate_1),	\
		.common		= {					\
			.reg		= _reg,				\
			.features	= CCU_FEATURE_FRACTIONAL,	\
			.hw.init	= CLK_HW_INIT(_name,		\
						      _parent,		\
						      &ccu_nm_ops,	\
						      _flags),		\
		},							\
	}

#define SUNXI_CCU_NM_WITH_FRAC_GATE_LOCK_MIN(_struct, _name, _parent,	\
					     _reg, _min_rate,		\
					     _nshift, _nwidth,		\
					     _mshift, _mwidth,		\
					     _frac_en, _frac_sel,	\
					     _frac_rate_0, _frac_rate_1,\
					     _gate, _lock, _flags)	\
	struct ccu_nm _struct = {					\
		.enable		= _gate,				\
		.lock		= _lock,				\
		.n		= _SUNXI_CCU_MULT(_nshift, _nwidth),	\
		.m		= _SUNXI_CCU_DIV(_mshift, _mwidth),	\
		.frac		= _SUNXI_CCU_FRAC(_frac_en, _frac_sel,	\
						  _frac_rate_0,		\
						  _frac_rate_1),	\
		.min_rate	= _min_rate,				\
		.common		= {					\
			.reg		= _reg,				\
			.features	= CCU_FEATURE_FRACTIONAL,	\
			.hw.init	= CLK_HW_INIT(_name,		\
						      _parent,		\
						      &ccu_nm_ops,	\
						      _flags),		\
		},							\
	}

#define SUNXI_CCU_NM_WITH_FRAC_GATE_LOCK_MIN_MAX(_struct, _name,	\
						 _parent, _reg,		\
						 _min_rate, _max_rate,	\
						 _nshift, _nwidth,	\
						 _mshift, _mwidth,	\
						 _frac_en, _frac_sel,	\
						 _frac_rate_0,		\
						 _frac_rate_1,		\
						 _gate, _lock, _flags)	\
	struct ccu_nm _struct = {					\
		.enable		= _gate,				\
		.lock		= _lock,				\
		.n		= _SUNXI_CCU_MULT(_nshift, _nwidth),	\
		.m		= _SUNXI_CCU_DIV(_mshift, _mwidth),	\
		.frac		= _SUNXI_CCU_FRAC(_frac_en, _frac_sel,	\
						  _frac_rate_0,		\
						  _frac_rate_1),	\
		.min_rate	= _min_rate,				\
		.max_rate	= _max_rate,				\
		.common		= {					\
			.reg		= _reg,				\
			.features	= CCU_FEATURE_FRACTIONAL,	\
			.hw.init	= CLK_HW_INIT(_name,		\
						      _parent,		\
						      &ccu_nm_ops,	\
						      _flags),		\
		},							\
	}

#define SUNXI_CCU_NM_WITH_GATE_LOCK(_struct, _name, _parent, _reg,	\
				    _nshift, _nwidth,			\
				    _mshift, _mwidth,			\
				    _gate, _lock, _flags)		\
	struct ccu_nm _struct = {					\
		.enable		= _gate,				\
		.lock		= _lock,				\
		.n		= _SUNXI_CCU_MULT(_nshift, _nwidth),	\
		.m		= _SUNXI_CCU_DIV(_mshift, _mwidth),	\
		.common		= {					\
			.reg		= _reg,				\
			.hw.init	= CLK_HW_INIT(_name,		\
						      _parent,		\
						      &ccu_nm_ops,	\
						      _flags),		\
		},							\
	}

static inline struct ccu_nm *hw_to_ccu_nm(struct clk_hw *hw)
{
	struct ccu_common *common = hw_to_ccu_common(hw);

	return container_of(common, struct ccu_nm, common);
}

extern const struct clk_ops ccu_nm_ops;

#endif /* _CCU_NM_H_ */
