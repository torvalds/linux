/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016 Maxime Ripard. All rights reserved.
 */

#ifndef _CCU_PHASE_H_
#define _CCU_PHASE_H_

#include <linux/clk-provider.h>

#include "ccu_common.h"

struct ccu_phase {
	u8			shift;
	u8			width;

	struct ccu_common	common;
};

#define SUNXI_CCU_PHASE(_struct, _name, _parent, _reg, _shift, _width, _flags) \
	struct ccu_phase _struct = {					\
		.shift	= _shift,					\
		.width	= _width,					\
		.common	= {						\
			.reg		= _reg,				\
			.hw.init	= CLK_HW_INIT(_name,		\
						      _parent,		\
						      &ccu_phase_ops,	\
						      _flags),		\
		}							\
	}

static inline struct ccu_phase *hw_to_ccu_phase(struct clk_hw *hw)
{
	struct ccu_common *common = hw_to_ccu_common(hw);

	return container_of(common, struct ccu_phase, common);
}

extern const struct clk_ops ccu_phase_ops;

#endif /* _CCU_PHASE_H_ */
