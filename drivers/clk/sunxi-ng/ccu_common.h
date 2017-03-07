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

#ifndef _COMMON_H_
#define _COMMON_H_

#include <linux/compiler.h>
#include <linux/clk-provider.h>

#define CCU_FEATURE_FRACTIONAL		BIT(0)
#define CCU_FEATURE_VARIABLE_PREDIV	BIT(1)
#define CCU_FEATURE_FIXED_PREDIV	BIT(2)
#define CCU_FEATURE_FIXED_POSTDIV	BIT(3)
#define CCU_FEATURE_ALL_PREDIV		BIT(4)
#define CCU_FEATURE_LOCK_REG		BIT(5)

struct device_node;

#define CLK_HW_INIT(_name, _parent, _ops, _flags)			\
	&(struct clk_init_data) {					\
		.flags		= _flags,				\
		.name		= _name,				\
		.parent_names	= (const char *[]) { _parent },		\
		.num_parents	= 1,					\
		.ops 		= _ops,					\
	}

#define CLK_HW_INIT_PARENTS(_name, _parents, _ops, _flags)		\
	&(struct clk_init_data) {					\
		.flags		= _flags,				\
		.name		= _name,				\
		.parent_names	= _parents,				\
		.num_parents	= ARRAY_SIZE(_parents),			\
		.ops 		= _ops,					\
	}

#define CLK_FIXED_FACTOR(_struct, _name, _parent,			\
			_div, _mult, _flags)				\
	struct clk_fixed_factor _struct = {				\
		.div		= _div,					\
		.mult		= _mult,				\
		.hw.init	= CLK_HW_INIT(_name,			\
					      _parent,			\
					      &clk_fixed_factor_ops,	\
					      _flags),			\
	}

struct ccu_common {
	void __iomem	*base;
	u16		reg;
	u16		lock_reg;
	u32		prediv;

	unsigned long	features;
	spinlock_t	*lock;
	struct clk_hw	hw;
};

static inline struct ccu_common *hw_to_ccu_common(struct clk_hw *hw)
{
	return container_of(hw, struct ccu_common, hw);
}

struct sunxi_ccu_desc {
	struct ccu_common		**ccu_clks;
	unsigned long			num_ccu_clks;

	struct clk_hw_onecell_data	*hw_clks;

	struct ccu_reset_map		*resets;
	unsigned long			num_resets;
};

void ccu_helper_wait_for_lock(struct ccu_common *common, u32 lock);

int sunxi_ccu_probe(struct device_node *node, void __iomem *reg,
		    const struct sunxi_ccu_desc *desc);

#endif /* _COMMON_H_ */
