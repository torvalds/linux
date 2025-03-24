/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved
 */

#ifndef _DPU_HW_CWB_H
#define _DPU_HW_CWB_H

#include "dpu_hw_util.h"

struct dpu_hw_cwb;

enum cwb_mode_input {
	INPUT_MODE_LM_OUT,
	INPUT_MODE_DSPP_OUT,
	INPUT_MODE_MAX
};

/**
 * struct dpu_hw_cwb_setup_cfg : Describes configuration for CWB mux
 * @pp_idx:        Index of the real-time pinpong that the CWB mux will
 *                 feed the CWB mux
 * @input:         Input tap point
 */
struct dpu_hw_cwb_setup_cfg {
	enum dpu_pingpong pp_idx;
	enum cwb_mode_input input;
};

/**
 *
 * struct dpu_hw_cwb_ops : Interface to the cwb hw driver functions
 * @config_cwb: configure CWB mux
 */
struct dpu_hw_cwb_ops {
	void (*config_cwb)(struct dpu_hw_cwb *ctx,
			   struct dpu_hw_cwb_setup_cfg *cwb_cfg);
};

/**
 * struct dpu_hw_cwb : CWB mux driver object
 * @base: Hardware block base structure
 * @hw: Block hardware details
 * @idx: CWB index
 * @ops: handle to operations possible for this CWB
 */
struct dpu_hw_cwb {
	struct dpu_hw_blk base;
	struct dpu_hw_blk_reg_map hw;

	enum dpu_cwb idx;

	struct dpu_hw_cwb_ops ops;
};

/**
 * dpu_hw_cwb - convert base object dpu_hw_base to container
 * @hw: Pointer to base hardware block
 * return: Pointer to hardware block container
 */
static inline struct dpu_hw_cwb *to_dpu_hw_cwb(struct dpu_hw_blk *hw)
{
	return container_of(hw, struct dpu_hw_cwb, base);
}

struct dpu_hw_cwb *dpu_hw_cwb_init(struct drm_device *dev,
				   const struct dpu_cwb_cfg *cfg,
				   void __iomem *addr);

#endif /*_DPU_HW_CWB_H */
