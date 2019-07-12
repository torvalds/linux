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

#ifndef _CCU_GATE_H_
#define _CCU_GATE_H_

#include <linux/clk-provider.h>

#include "ccu_common.h"

struct ccu_gate {
	u32			enable;

	struct ccu_common	common;
};

#define SUNXI_CCU_GATE(_struct, _name, _parent, _reg, _gate, _flags)	\
	struct ccu_gate _struct = {					\
		.enable	= _gate,					\
		.common	= {						\
			.reg		= _reg,				\
			.hw.init	= CLK_HW_INIT(_name,		\
						      _parent,		\
						      &ccu_gate_ops,	\
						      _flags),		\
		}							\
	}

#define SUNXI_CCU_GATE_HW(_struct, _name, _parent, _reg, _gate, _flags)	\
	struct ccu_gate _struct = {					\
		.enable	= _gate,					\
		.common	= {						\
			.reg		= _reg,				\
			.hw.init	= CLK_HW_INIT_HW(_name,		\
							 _parent,	\
							 &ccu_gate_ops,	\
							 _flags),	\
		}							\
	}

#define SUNXI_CCU_GATE_FW(_struct, _name, _parent, _reg, _gate, _flags)	\
	struct ccu_gate _struct = {					\
		.enable	= _gate,					\
		.common	= {						\
			.reg		= _reg,				\
			.hw.init	= CLK_HW_INIT_FW_NAME(_name,	\
							      _parent,	\
							      &ccu_gate_ops, \
							      _flags),	\
		}							\
	}

/*
 * The following two macros allow the re-use of the data structure
 * holding the parent info.
 */
#define SUNXI_CCU_GATE_HWS(_struct, _name, _parent, _reg, _gate, _flags) \
	struct ccu_gate _struct = {					\
		.enable	= _gate,					\
		.common	= {						\
			.reg		= _reg,				\
			.hw.init	= CLK_HW_INIT_HWS(_name,	\
							  _parent,	\
							  &ccu_gate_ops, \
							  _flags),	\
		}							\
	}

#define SUNXI_CCU_GATE_DATA(_struct, _name, _data, _reg, _gate, _flags)	\
	struct ccu_gate _struct = {					\
		.enable	= _gate,					\
		.common	= {						\
			.reg		= _reg,				\
			.hw.init	=				\
				CLK_HW_INIT_PARENTS_DATA(_name,		\
							 _data,		\
							 &ccu_gate_ops,	\
							 _flags),	\
		}							\
	}

static inline struct ccu_gate *hw_to_ccu_gate(struct clk_hw *hw)
{
	struct ccu_common *common = hw_to_ccu_common(hw);

	return container_of(common, struct ccu_gate, common);
}

void ccu_gate_helper_disable(struct ccu_common *common, u32 gate);
int ccu_gate_helper_enable(struct ccu_common *common, u32 gate);
int ccu_gate_helper_is_enabled(struct ccu_common *common, u32 gate);

extern const struct clk_ops ccu_gate_ops;

#endif /* _CCU_GATE_H_ */
