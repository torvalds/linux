/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#ifndef _DPU_HW_DSPP_H
#define _DPU_HW_DSPP_H

#include "dpu_hw_blk.h"

struct dpu_hw_dspp;

/**
 * struct dpu_hw_dspp_ops - interface to the dspp hardware driver functions
 * Caller must call the init function to get the dspp context for each dspp
 * Assumption is these functions will be called after clocks are enabled
 */
struct dpu_hw_dspp_ops {

	void (*dummy)(struct dpu_hw_dspp *ctx);
};

/**
 * struct dpu_hw_dspp - dspp description
 * @base: Hardware block base structure
 * @hw: Block hardware details
 * @idx: DSPP index
 * @cap: Pointer to layer_cfg
 * @ops: Pointer to operations possible for this DSPP
 */
struct dpu_hw_dspp {
	struct dpu_hw_blk base;
	struct dpu_hw_blk_reg_map hw;

	/* dspp */
	int idx;
	const struct dpu_dspp_cfg *cap;

	/* Ops */
	struct dpu_hw_dspp_ops ops;
};

/**
 * dpu_hw_dspp - convert base object dpu_hw_base to container
 * @hw: Pointer to base hardware block
 * return: Pointer to hardware block container
 */
static inline struct dpu_hw_dspp *to_dpu_hw_dspp(struct dpu_hw_blk *hw)
{
	return container_of(hw, struct dpu_hw_dspp, base);
}

/**
 * dpu_hw_dspp_init - initializes the dspp hw driver object.
 * should be called once before accessing every dspp.
 * @idx:  DSPP index for which driver object is required
 * @addr: Mapped register io address of MDP
 * @Return: pointer to structure or ERR_PTR
 */
struct dpu_hw_dspp *dpu_hw_dspp_init(enum dpu_dspp idx,
	void __iomem *addr, const struct dpu_mdss_cfg *m);

/**
 * dpu_hw_dspp_destroy(): Destroys DSPP driver context
 * @dspp: Pointer to DSPP driver context
 */
void dpu_hw_dspp_destroy(struct dpu_hw_dspp *dspp);

#endif /*_DPU_HW_DSPP_H */

