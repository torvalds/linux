// SPDX-License-Identifier: GPL-2.0+
//
// OWL fixed factor clock driver
//
// Copyright (c) 2014 Actions Semi Inc.
// Author: David Liu <liuwei@actions-semi.com>
//
// Copyright (c) 2018 Linaro Ltd.
// Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>

#ifndef _OWL_FIXED_FACTOR_H_
#define _OWL_FIXED_FACTOR_H_

#include "owl-common.h"

#define OWL_FIX_FACT(_struct, _name, _parent, _mul, _div, _flags)	\
	struct clk_fixed_factor _struct = {				\
		.mult		= _mul,					\
		.div		= _div,					\
		.hw.init	= CLK_HW_INIT(_name,			\
					      _parent,			\
					      &clk_fixed_factor_ops,	\
					      _flags),			\
	}

extern const struct clk_ops clk_fixed_factor_ops;

#endif /* _OWL_FIXED_FACTOR_H_ */
