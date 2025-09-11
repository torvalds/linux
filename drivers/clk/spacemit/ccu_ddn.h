/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024 SpacemiT Technology Co. Ltd
 * Copyright (c) 2024-2025 Haylen Chu <heylenay@4d2.org>
 */

#ifndef _CCU_DDN_H_
#define _CCU_DDN_H_

#include <linux/bitops.h>
#include <linux/clk-provider.h>

#include "ccu_common.h"

struct ccu_ddn {
	struct ccu_common common;
	unsigned int num_mask;
	unsigned int num_shift;
	unsigned int den_mask;
	unsigned int den_shift;
	unsigned int pre_div;
};

#define CCU_DDN_INIT(_name, _parent, _flags) \
	CLK_HW_INIT_HW(#_name, &_parent.common.hw, &spacemit_ccu_ddn_ops, _flags)

#define CCU_DDN_DEFINE(_name, _parent, _reg_ctrl, _num_shift, _num_width,	\
		       _den_shift, _den_width, _pre_div, _flags)		\
static struct ccu_ddn _name = {							\
	.common = {								\
		.reg_ctrl	= _reg_ctrl,					\
		.hw.init	= CCU_DDN_INIT(_name, _parent, _flags),		\
	},									\
	.num_mask	= GENMASK(_num_shift + _num_width - 1, _num_shift),	\
	.num_shift	= _num_shift,						\
	.den_mask	= GENMASK(_den_shift + _den_width - 1, _den_shift),	\
	.den_shift	= _den_shift,						\
	.pre_div	= _pre_div,						\
}

static inline struct ccu_ddn *hw_to_ccu_ddn(struct clk_hw *hw)
{
	struct ccu_common *common = hw_to_ccu_common(hw);

	return container_of(common, struct ccu_ddn, common);
}

extern const struct clk_ops spacemit_ccu_ddn_ops;

#endif
