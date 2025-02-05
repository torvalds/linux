/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 Inochi Amaoto <inochiama@outlook.com>
 */

#ifndef _CLK_SOPHGO_CV1800_IP_H_
#define _CLK_SOPHGO_CV1800_IP_H_

#include "clk-cv18xx-common.h"

struct cv1800_clk_gate {
	struct cv1800_clk_common	common;
	struct cv1800_clk_regbit	gate;
};

struct cv1800_clk_div_data {
	u32		reg;
	u32		mask;
	u32		width;
	u32		init;
	u32		flags;
};

struct cv1800_clk_div {
	struct cv1800_clk_common	common;
	struct cv1800_clk_regbit	gate;
	struct cv1800_clk_regfield	div;
};

struct cv1800_clk_bypass_div {
	struct cv1800_clk_div		div;
	struct cv1800_clk_regbit	bypass;
};

struct cv1800_clk_mux {
	struct cv1800_clk_common	common;
	struct cv1800_clk_regbit	gate;
	struct cv1800_clk_regfield	div;
	struct cv1800_clk_regfield	mux;
};

struct cv1800_clk_bypass_mux {
	struct cv1800_clk_mux		mux;
	struct cv1800_clk_regbit	bypass;
};

struct cv1800_clk_mmux {
	struct cv1800_clk_common	common;
	struct cv1800_clk_regbit	gate;
	struct cv1800_clk_regfield	div[2];
	struct cv1800_clk_regfield	mux[2];
	struct cv1800_clk_regbit	bypass;
	struct cv1800_clk_regbit	clk_sel;
	const s8			*parent2sel;
	const u8			*sel2parent[2];
};

struct cv1800_clk_audio {
	struct cv1800_clk_common	common;
	struct cv1800_clk_regbit	src_en;
	struct cv1800_clk_regbit	output_en;
	struct cv1800_clk_regbit	div_en;
	struct cv1800_clk_regbit	div_up;
	struct cv1800_clk_regfield	m;
	struct cv1800_clk_regfield	n;
	u32				target_rate;
};

#define CV1800_GATE(_name, _parent, _gate_reg, _gate_shift, _flags)	\
	struct cv1800_clk_gate _name = {				\
		.common	= CV1800_CLK_COMMON(#_name, _parent,		\
					    &cv1800_clk_gate_ops,	\
					    _flags),			\
		.gate	= CV1800_CLK_BIT(_gate_reg, _gate_shift),	\
	}

#define _CV1800_DIV(_name, _parent, _gate_reg, _gate_shift,		\
		    _div_reg, _div_shift, _div_width, _div_init,	\
		    _div_flag, _ops, _flags)				\
	{								\
		.common		= CV1800_CLK_COMMON(#_name, _parent,	\
						    _ops, _flags),	\
		.gate		= CV1800_CLK_BIT(_gate_reg,		\
						 _gate_shift),		\
		.div		= CV1800_CLK_REG(_div_reg, _div_shift,	\
						 _div_width, _div_init,	\
						 _div_flag),		\
	}

#define _CV1800_FIXED_DIV_FLAG	\
	(CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ROUND_CLOSEST)

#define _CV1800_FIXED_DIV(_name, _parent, _gate_reg, _gate_shift,	\
			  _fix_div, _ops, _flags)			\
	{								\
		.common		= CV1800_CLK_COMMON(#_name, _parent,	\
						    _ops, _flags),	\
		.gate		= CV1800_CLK_BIT(_gate_reg,		\
						 _gate_shift),		\
		.div		= CV1800_CLK_REG(0, 0, 0,		\
						 _fix_div,		\
						 _CV1800_FIXED_DIV_FLAG),\
	}

#define CV1800_DIV(_name, _parent, _gate_reg, _gate_shift,		\
		   _div_reg, _div_shift, _div_width, _div_init,		\
		   _div_flag, _flags)					\
	struct cv1800_clk_div _name =					\
		_CV1800_DIV(_name, _parent, _gate_reg, _gate_shift,	\
			    _div_reg, _div_shift, _div_width, _div_init,\
			    _div_flag, &cv1800_clk_div_ops, _flags)

#define CV1800_BYPASS_DIV(_name, _parent, _gate_reg, _gate_shift,	\
			  _div_reg, _div_shift, _div_width, _div_init,	\
			  _div_flag, _bypass_reg, _bypass_shift, _flags)\
	struct cv1800_clk_bypass_div _name = {				\
		.div	= _CV1800_DIV(_name, _parent,			\
				      _gate_reg, _gate_shift,		\
				      _div_reg, _div_shift,		\
				      _div_width, _div_init, _div_flag,	\
				      &cv1800_clk_bypass_div_ops,	\
				      _flags),				\
		.bypass	= CV1800_CLK_BIT(_bypass_reg, _bypass_shift),	\
	}

#define CV1800_FIXED_DIV(_name, _parent, _gate_reg, _gate_shift,	\
			 _fix_div, _flags)				\
	struct cv1800_clk_div _name =					\
		_CV1800_FIXED_DIV(_name, _parent,			\
				  _gate_reg, _gate_shift,		\
				  _fix_div,				\
				  &cv1800_clk_div_ops, _flags)		\

#define CV1800_BYPASS_FIXED_DIV(_name, _parent, _gate_reg, _gate_shift,	\
				_fix_div, _bypass_reg, _bypass_shift,	\
				_flags)					\
	struct cv1800_clk_bypass_div _name = {				\
		.div	= _CV1800_FIXED_DIV(_name, _parent,		\
					    _gate_reg, _gate_shift,	\
					    _fix_div,			\
					    &cv1800_clk_bypass_div_ops,	\
					    _flags),			\
		.bypass	= CV1800_CLK_BIT(_bypass_reg, _bypass_shift),	\
	}

#define _CV1800_MUX(_name, _parent, _gate_reg, _gate_shift,		\
		    _div_reg, _div_shift, _div_width, _div_init,	\
		    _div_flag,						\
		    _mux_reg, _mux_shift, _mux_width,			\
		    _ops, _flags)					\
	{								\
		.common		= CV1800_CLK_COMMON(#_name, _parent,	\
						    _ops, _flags),	\
		.gate		= CV1800_CLK_BIT(_gate_reg,		\
						 _gate_shift),		\
		.div		= CV1800_CLK_REG(_div_reg, _div_shift,	\
						 _div_width, _div_init,	\
						 _div_flag),		\
		.mux		= CV1800_CLK_REG(_mux_reg, _mux_shift,	\
						 _mux_width, 0, 0),	\
	}

#define CV1800_MUX(_name, _parent, _gate_reg, _gate_shift,		\
		   _div_reg, _div_shift, _div_width, _div_init,		\
		   _div_flag,						\
		   _mux_reg, _mux_shift, _mux_width, _flags)		\
	struct cv1800_clk_mux _name =					\
		_CV1800_MUX(_name, _parent, _gate_reg, _gate_shift,	\
			    _div_reg, _div_shift, _div_width, _div_init,\
			    _div_flag, _mux_reg, _mux_shift, _mux_width,\
			    &cv1800_clk_mux_ops, _flags)

#define CV1800_BYPASS_MUX(_name, _parent, _gate_reg, _gate_shift,	\
			  _div_reg, _div_shift, _div_width, _div_init,	\
			  _div_flag,					\
			  _mux_reg, _mux_shift, _mux_width,		\
			  _bypass_reg, _bypass_shift, _flags)		\
	struct cv1800_clk_bypass_mux _name = {				\
		.mux	= _CV1800_MUX(_name, _parent,			\
				      _gate_reg, _gate_shift,		\
				      _div_reg, _div_shift, _div_width,	\
				      _div_init, _div_flag,		\
				      _mux_reg, _mux_shift, _mux_width,	\
				      &cv1800_clk_bypass_mux_ops,	\
				      _flags),				\
		.bypass	= CV1800_CLK_BIT(_bypass_reg, _bypass_shift),	\
	}

#define CV1800_MMUX(_name, _parent, _gate_reg, _gate_shift,		\
		    _div0_reg, _div0_shift, _div0_width, _div0_init,	\
		    _div0_flag,						\
		    _div1_reg, _div1_shift, _div1_width, _div1_init,	\
		    _div1_flag,						\
		    _mux0_reg, _mux0_shift, _mux0_width,		\
		    _mux1_reg, _mux1_shift, _mux1_width,		\
		    _bypass_reg, _bypass_shift,				\
		    _clk_sel_reg, _clk_sel_shift,			\
		    _parent2sel, _sel2parent0, _sel2parent1, _flags)	\
	struct cv1800_clk_mmux _name = {				\
		.common		= CV1800_CLK_COMMON(#_name, _parent,	\
						    &cv1800_clk_mmux_ops,\
						    _flags),		\
		.gate		= CV1800_CLK_BIT(_gate_reg, _gate_shift),\
		.div		= {					\
			CV1800_CLK_REG(_div0_reg, _div0_shift,		\
				       _div0_width, _div0_init,		\
				       _div0_flag),			\
			CV1800_CLK_REG(_div1_reg, _div1_shift,		\
				       _div1_width, _div1_init,		\
				       _div1_flag),			\
		},							\
		.mux		= {					\
			CV1800_CLK_REG(_mux0_reg, _mux0_shift,		\
				       _mux0_width, 0, 0),		\
			CV1800_CLK_REG(_mux1_reg, _mux1_shift,		\
				       _mux1_width, 0, 0),		\
		},							\
		.bypass		= CV1800_CLK_BIT(_bypass_reg,		\
						 _bypass_shift),	\
		.clk_sel	= CV1800_CLK_BIT(_clk_sel_reg,		\
						 _clk_sel_shift),	\
		.parent2sel	= _parent2sel,				\
		.sel2parent	= { _sel2parent0, _sel2parent1 },	\
	}

#define CV1800_ACLK(_name, _parent,					\
		    _src_en_reg, _src_en_reg_shift,			\
		    _output_en_reg, _output_en_shift,			\
		    _div_en_reg, _div_en_reg_shift,			\
		    _div_up_reg, _div_up_reg_shift,			\
		    _m_reg, _m_shift, _m_width, _m_flag,		\
		    _n_reg, _n_shift, _n_width, _n_flag,		\
		    _target_rate, _flags)				\
	struct cv1800_clk_audio _name = {				\
		.common		= CV1800_CLK_COMMON(#_name, _parent,	\
						    &cv1800_clk_audio_ops,\
						    _flags),		\
		.src_en		= CV1800_CLK_BIT(_src_en_reg,		\
						 _src_en_reg_shift),	\
		.output_en	= CV1800_CLK_BIT(_output_en_reg,	\
						 _output_en_shift),	\
		.div_en		= CV1800_CLK_BIT(_div_en_reg,		\
						 _div_en_reg_shift),	\
		.div_up		= CV1800_CLK_BIT(_div_up_reg,		\
						 _div_up_reg_shift),	\
		.m		= CV1800_CLK_REG(_m_reg, _m_shift,	\
						 _m_width, 0, _m_flag),	\
		.n		= CV1800_CLK_REG(_n_reg, _n_shift,	\
						 _n_width, 0, _n_flag),	\
		.target_rate	= _target_rate,				\
	}

extern const struct clk_ops cv1800_clk_gate_ops;
extern const struct clk_ops cv1800_clk_div_ops;
extern const struct clk_ops cv1800_clk_bypass_div_ops;
extern const struct clk_ops cv1800_clk_mux_ops;
extern const struct clk_ops cv1800_clk_bypass_mux_ops;
extern const struct clk_ops cv1800_clk_mmux_ops;
extern const struct clk_ops cv1800_clk_audio_ops;

#endif // _CLK_SOPHGO_CV1800_IP_H_
