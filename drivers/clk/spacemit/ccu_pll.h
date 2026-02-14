/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024 SpacemiT Technology Co. Ltd
 * Copyright (c) 2024-2025 Haylen Chu <heylenay@4d2.org>
 */

#ifndef _CCU_PLL_H_
#define _CCU_PLL_H_

#include <linux/clk-provider.h>

#include "ccu_common.h"

/**
 * struct ccu_pll_rate_tbl - Structure mapping between PLL rate and register
 * configuration.
 *
 * @rate:	PLL rate
 * @swcr1:	Value of register PLLx_SW1_CTRL.
 * @swcr2:	Value of register PLLAx_SW2_CTRL.
 * @swcr3:	value of register PLLx_SW3_CTRL.
 *
 * See below tables for the register used in PPL/PPLA clocks
 *
 * Regular PLL type
 *  | Enable | swcr3 | PLLx_SW3_CTRL - BIT[31]    |
 *  -----------------------------------------------
 *  | Config | swcr1 | PLLx_SW1_CTRL - BIT[31:0]  |
 *  |        | swcr2 | Not used                   |
 *  |        | swcr3 | PLLx_SW3_CTRL - BIT[30:0]  |
 *
 * Special PLL type A
 *  | Enable | swcr2 | PLLAx_SW2_CTRL - BIT[16]   |
 *  -----------------------------------------------
 *  | Config | swcr1 | PLLAx_SW1_CTRL - BIT[31:0] |
 *  |        | swcr2 | PLLAx_SW2_CTRL - BIT[15:8] |
 *  |        | swcr3 | PLLAx_SW3_CTRL - BIT[31:0] |
 *
 */
struct ccu_pll_rate_tbl {
	unsigned long rate;
	u32 swcr1;
	u32 swcr2;
	u32 swcr3;
};

struct ccu_pll_config {
	const struct ccu_pll_rate_tbl *rate_tbl;
	u32 tbl_num;
	u32 reg_lock;
	u32 mask_lock;
};

#define CCU_PLL_RATE(_rate, _swcr1, _swcr3) \
	{									\
		.rate	= _rate,						\
		.swcr1	= _swcr1,						\
		.swcr3	= _swcr3,						\
	}

#define CCU_PLLA_RATE(_rate, _swcr1, _swcr2, _swcr3) \
	{									\
		.rate	= _rate,						\
		.swcr1	= _swcr1,						\
		.swcr2	= _swcr2,						\
		.swcr3	= _swcr3,						\
	}

struct ccu_pll {
	struct ccu_common	common;
	struct ccu_pll_config	config;
};

#define CCU_PLL_CONFIG(_table, _reg_lock, _mask_lock) \
	{									\
		.rate_tbl	= _table,					\
		.tbl_num	= ARRAY_SIZE(_table),				\
		.reg_lock	= (_reg_lock),					\
		.mask_lock	= (_mask_lock),					\
	}

#define CCU_PLL_COMMON_HWINIT(_name, _ops, _flags)				\
	(&(struct clk_init_data) {						\
		.name		= #_name,					\
		.ops		= _ops,						\
		.parent_data	= &(struct clk_parent_data) { .index = 0 },	\
		.num_parents	= 1,						\
		.flags		= _flags,					\
	})

#define CCU_PLL_X_DEFINE(_name, _table, _reg_swcr1, _reg_swcr2, _reg_swcr3,	\
		       _reg_lock, _mask_lock, _ops, _flags)			\
static struct ccu_pll _name = {							\
	.config	= CCU_PLL_CONFIG(_table, _reg_lock, _mask_lock),		\
	.common = {								\
		.reg_swcr1	= _reg_swcr1,					\
		.reg_swcr2	= _reg_swcr2,					\
		.reg_swcr3	= _reg_swcr3,					\
		.hw.init	= CCU_PLL_COMMON_HWINIT(_name, _ops, _flags)	\
	}									\
}

#define CCU_PLL_DEFINE(_name, _table, _reg_swcr1, _reg_swcr3, _reg_lock,	\
		       _mask_lock, _flags)					\
	CCU_PLL_X_DEFINE(_name, _table, _reg_swcr1, 0, _reg_swcr3,		\
		       _reg_lock, _mask_lock, &spacemit_ccu_pll_ops, _flags)

#define CCU_PLLA_DEFINE(_name, _table, _reg_swcr1, _reg_swcr2, _reg_swcr3,	\
		       _reg_lock, _mask_lock, _flags)				\
	CCU_PLL_X_DEFINE(_name, _table, _reg_swcr1, _reg_swcr2, _reg_swcr3,	\
		       _reg_lock, _mask_lock, &spacemit_ccu_plla_ops, _flags)

static inline struct ccu_pll *hw_to_ccu_pll(struct clk_hw *hw)
{
	struct ccu_common *common = hw_to_ccu_common(hw);

	return container_of(common, struct ccu_pll, common);
}

extern const struct clk_ops spacemit_ccu_pll_ops;
extern const struct clk_ops spacemit_ccu_plla_ops;

#endif
