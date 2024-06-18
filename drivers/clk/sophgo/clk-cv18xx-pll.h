/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 Inochi Amaoto <inochiama@outlook.com>
 */

#ifndef _CLK_SOPHGO_CV1800_PLL_H_
#define _CLK_SOPHGO_CV1800_PLL_H_

#include "clk-cv18xx-common.h"

struct cv1800_clk_pll_limit {
	struct {
		u8 min;
		u8 max;
	} pre_div, div, post_div, ictrl, mode;
};

#define _CV1800_PLL_LIMIT(_min, _max)	\
	{				\
		.min = _min,		\
		.max = _max,		\
	}				\

#define for_each_pll_limit_range(_var, _restrict) \
	for (_var = (_restrict)->min; _var <= (_restrict)->max; _var++)

struct cv1800_clk_pll_synthesizer {
	struct cv1800_clk_regbit	en;
	struct cv1800_clk_regbit	clk_half;
	u32				ctrl;
	u32				set;
};

#define _PLL_PRE_DIV_SEL_FIELD		GENMASK(6, 0)
#define _PLL_POST_DIV_SEL_FIELD		GENMASK(14, 8)
#define _PLL_SEL_MODE_FIELD		GENMASK(16, 15)
#define _PLL_DIV_SEL_FIELD		GENMASK(23, 17)
#define _PLL_ICTRL_FIELD		GENMASK(26, 24)

#define _PLL_ALL_FIELD_MASK \
	(_PLL_PRE_DIV_SEL_FIELD | \
	 _PLL_POST_DIV_SEL_FIELD | \
	 _PLL_SEL_MODE_FIELD | \
	 _PLL_DIV_SEL_FIELD | \
	 _PLL_ICTRL_FIELD)

#define PLL_COPY_REG(_dest, _src) \
	(((_dest) & (~_PLL_ALL_FIELD_MASK)) | ((_src) & _PLL_ALL_FIELD_MASK))

#define PLL_GET_PRE_DIV_SEL(_reg) \
	FIELD_GET(_PLL_PRE_DIV_SEL_FIELD, (_reg))
#define PLL_GET_POST_DIV_SEL(_reg) \
	FIELD_GET(_PLL_POST_DIV_SEL_FIELD, (_reg))
#define PLL_GET_SEL_MODE(_reg) \
	FIELD_GET(_PLL_SEL_MODE_FIELD, (_reg))
#define PLL_GET_DIV_SEL(_reg) \
	FIELD_GET(_PLL_DIV_SEL_FIELD, (_reg))
#define PLL_GET_ICTRL(_reg) \
	FIELD_GET(_PLL_ICTRL_FIELD, (_reg))

#define PLL_SET_PRE_DIV_SEL(_reg, _val) \
	_CV1800_SET_FIELD((_reg), (_val), _PLL_PRE_DIV_SEL_FIELD)
#define PLL_SET_POST_DIV_SEL(_reg, _val) \
	_CV1800_SET_FIELD((_reg), (_val), _PLL_POST_DIV_SEL_FIELD)
#define PLL_SET_SEL_MODE(_reg, _val) \
	_CV1800_SET_FIELD((_reg), (_val), _PLL_SEL_MODE_FIELD)
#define PLL_SET_DIV_SEL(_reg, _val) \
	_CV1800_SET_FIELD((_reg), (_val), _PLL_DIV_SEL_FIELD)
#define PLL_SET_ICTRL(_reg, _val) \
	_CV1800_SET_FIELD((_reg), (_val), _PLL_ICTRL_FIELD)

struct cv1800_clk_pll {
	struct cv1800_clk_common		common;
	u32					pll_reg;
	struct cv1800_clk_regbit		pll_pwd;
	struct cv1800_clk_regbit		pll_status;
	const struct cv1800_clk_pll_limit	*pll_limit;
	struct cv1800_clk_pll_synthesizer	*pll_syn;
};

#define CV1800_INTEGRAL_PLL(_name, _parent, _pll_reg,			\
			     _pll_pwd_reg, _pll_pwd_shift,		\
			     _pll_status_reg, _pll_status_shift,	\
			     _pll_limit, _flags)			\
	struct cv1800_clk_pll _name = {					\
		.common		= CV1800_CLK_COMMON(#_name, _parent,	\
						    &cv1800_clk_ipll_ops,\
						    _flags),		\
		.pll_reg	= _pll_reg,				\
		.pll_pwd	= CV1800_CLK_BIT(_pll_pwd_reg,		\
					       _pll_pwd_shift),		\
		.pll_status	= CV1800_CLK_BIT(_pll_status_reg,	\
					       _pll_status_shift),	\
		.pll_limit	= _pll_limit,				\
		.pll_syn	= NULL,					\
	}

#define CV1800_FACTIONAL_PLL(_name, _parent, _pll_reg,			\
			     _pll_pwd_reg, _pll_pwd_shift,		\
			     _pll_status_reg, _pll_status_shift,	\
			     _pll_limit, _pll_syn, _flags)		\
	struct cv1800_clk_pll _name = {					\
		.common		= CV1800_CLK_COMMON(#_name, _parent,	\
						    &cv1800_clk_fpll_ops,\
						    _flags),		\
		.pll_reg	= _pll_reg,				\
		.pll_pwd	= CV1800_CLK_BIT(_pll_pwd_reg,		\
					       _pll_pwd_shift),		\
		.pll_status	= CV1800_CLK_BIT(_pll_status_reg,	\
					       _pll_status_shift),	\
		.pll_limit	= _pll_limit,				\
		.pll_syn	= _pll_syn,				\
	}

extern const struct clk_ops cv1800_clk_ipll_ops;
extern const struct clk_ops cv1800_clk_fpll_ops;

#endif // _CLK_SOPHGO_CV1800_PLL_H_
