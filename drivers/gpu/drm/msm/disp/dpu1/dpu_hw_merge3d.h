/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#ifndef _DPU_HW_MERGE3D_H
#define _DPU_HW_MERGE3D_H

#include "dpu_hw_catalog.h"
#include "dpu_hw_mdss.h"
#include "dpu_hw_util.h"

struct dpu_hw_merge_3d;

/**
 *
 * struct dpu_hw_merge_3d_ops : Interface to the merge_3d Hw driver functions
 *  Assumption is these functions will be called after clocks are enabled
 *  @setup_3d_mode : enable 3D merge
 */
struct dpu_hw_merge_3d_ops {
	void (*setup_3d_mode)(struct dpu_hw_merge_3d *merge_3d,
			enum dpu_3d_blend_mode mode_3d);

};

struct dpu_hw_merge_3d {
	struct dpu_hw_blk base;
	struct dpu_hw_blk_reg_map hw;

	/* merge_3d */
	enum dpu_merge_3d idx;
	const struct dpu_merge_3d_cfg *caps;

	/* ops */
	struct dpu_hw_merge_3d_ops ops;
};

/**
 * to_dpu_hw_merge_3d - convert base object dpu_hw_base to container
 * @hw: Pointer to base hardware block
 * return: Pointer to hardware block container
 */
static inline struct dpu_hw_merge_3d *to_dpu_hw_merge_3d(struct dpu_hw_blk *hw)
{
	return container_of(hw, struct dpu_hw_merge_3d, base);
}

/**
 * dpu_hw_merge_3d_init() - Initializes the merge_3d driver for the passed
 * merge3d catalog entry.
 * @dev:  Corresponding device for devres management
 * @cfg:  Pingpong catalog entry for which driver object is required
 * @addr: Mapped register io address of MDP
 * Return: Error code or allocated dpu_hw_merge_3d context
 */
struct dpu_hw_merge_3d *dpu_hw_merge_3d_init(struct drm_device *dev,
					     const struct dpu_merge_3d_cfg *cfg,
					     void __iomem *addr);

#endif /*_DPU_HW_MERGE3D_H */
