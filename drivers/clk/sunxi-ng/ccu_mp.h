/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016 Maxime Ripard. All rights reserved.
 */

#ifndef _CCU_MP_H_
#define _CCU_MP_H_

#include <linux/bitops.h>
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

	unsigned int		fixed_post_div;

	struct ccu_common	common;
};

#define SUNXI_CCU_MP_WITH_MUX_GATE_POSTDIV(_struct, _name, _parents, _reg, \
					   _mshift, _mwidth,		\
					   _pshift, _pwidth,		\
					   _muxshift, _muxwidth,	\
					   _gate, _postdiv, _flags)	\
	struct ccu_mp _struct = {					\
		.enable	= _gate,					\
		.m	= _SUNXI_CCU_DIV(_mshift, _mwidth),		\
		.p	= _SUNXI_CCU_DIV(_pshift, _pwidth),		\
		.mux	= _SUNXI_CCU_MUX(_muxshift, _muxwidth),		\
		.fixed_post_div	= _postdiv,				\
		.common	= {						\
			.reg		= _reg,				\
			.features	= CCU_FEATURE_FIXED_POSTDIV,	\
			.hw.init	= CLK_HW_INIT_PARENTS(_name,	\
							      _parents, \
							      &ccu_mp_ops, \
							      _flags),	\
		}							\
	}

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

#define SUNXI_CCU_MP_DATA_WITH_MUX_GATE(_struct, _name, _parents, _reg,	\
					_mshift, _mwidth,		\
					_pshift, _pwidth,		\
					_muxshift, _muxwidth,		\
					_gate, _flags)			\
	struct ccu_mp _struct = {					\
		.enable	= _gate,					\
		.m	= _SUNXI_CCU_DIV(_mshift, _mwidth),		\
		.p	= _SUNXI_CCU_DIV(_pshift, _pwidth),		\
		.mux	= _SUNXI_CCU_MUX(_muxshift, _muxwidth),		\
		.common	= {						\
			.reg		= _reg,				\
			.hw.init	= CLK_HW_INIT_PARENTS_DATA(_name, \
								   _parents, \
								   &ccu_mp_ops, \
								   _flags), \
		}							\
	}

#define SUNXI_CCU_MP_DATA_WITH_MUX(_struct, _name, _parents, _reg,	\
				   _mshift, _mwidth,			\
				   _pshift, _pwidth,			\
				   _muxshift, _muxwidth,		\
				   _flags)				\
	SUNXI_CCU_MP_DATA_WITH_MUX_GATE(_struct, _name, _parents, _reg,	\
					_mshift, _mwidth,		\
					_pshift, _pwidth,		\
					_muxshift, _muxwidth,		\
					0, _flags)

#define SUNXI_CCU_MP_HW_WITH_MUX_GATE(_struct, _name, _parents, _reg,	\
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
			.hw.init	= CLK_HW_INIT_PARENTS_HW(_name, \
								 _parents, \
								 &ccu_mp_ops, \
								 _flags), \
		}							\
	}

static inline struct ccu_mp *hw_to_ccu_mp(struct clk_hw *hw)
{
	struct ccu_common *common = hw_to_ccu_common(hw);

	return container_of(common, struct ccu_mp, common);
}

extern const struct clk_ops ccu_mp_ops;

/*
 * Special class of M-P clock that supports MMC timing modes
 *
 * Since the MMC clock registers all follow the same layout, we can
 * simplify the macro for this particular case. In addition, as
 * switching modes also affects the output clock rate, we need to
 * have CLK_GET_RATE_NOCACHE for all these types of clocks.
 */

#define SUNXI_CCU_MP_MMC_WITH_MUX_GATE(_struct, _name, _parents, _reg,	\
				       _flags)				\
	struct ccu_mp _struct = {					\
		.enable	= BIT(31),					\
		.m	= _SUNXI_CCU_DIV(0, 4),				\
		.p	= _SUNXI_CCU_DIV(16, 2),			\
		.mux	= _SUNXI_CCU_MUX(24, 2),			\
		.common	= {						\
			.reg		= _reg,				\
			.features	= CCU_FEATURE_MMC_TIMING_SWITCH, \
			.hw.init	= CLK_HW_INIT_PARENTS(_name,	\
							      _parents, \
							      &ccu_mp_mmc_ops, \
							      CLK_GET_RATE_NOCACHE | \
							      _flags),	\
		}							\
	}

extern const struct clk_ops ccu_mp_mmc_ops;

#endif /* _CCU_MP_H_ */
