/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2015-2021, The Linux Foundation. All rights reserved.
 */

#ifndef _DPU_HW_LM_H
#define _DPU_HW_LM_H

#include "dpu_hw_mdss.h"
#include "dpu_hw_util.h"

struct dpu_hw_mixer;
struct dpu_hw_stage_cfg;

struct dpu_hw_mixer_cfg {
	u32 out_width;
	u32 out_height;
	bool right_mixer;
	int flags;
};

struct dpu_hw_color3_cfg {
	u8 keep_fg[DPU_STAGE_MAX];
};

/**
 *
 * struct dpu_hw_lm_ops : Interface to the mixer Hw driver functions
 *  Assumption is these functions will be called after clocks are enabled
 */
struct dpu_hw_lm_ops {
	/*
	 * Sets up mixer output width and height
	 * and border color if enabled
	 */
	void (*setup_mixer_out)(struct dpu_hw_mixer *ctx,
		struct dpu_hw_mixer_cfg *cfg);

	/*
	 * Alpha blending configuration
	 * for the specified stage
	 */
	void (*setup_blend_config)(struct dpu_hw_mixer *ctx, uint32_t stage,
		uint32_t fg_alpha, uint32_t bg_alpha, uint32_t blend_op);

	/*
	 * Alpha color component selection from either fg or bg
	 */
	void (*setup_alpha_out)(struct dpu_hw_mixer *ctx, uint32_t mixer_op);

	/**
	 * Clear layer mixer to pipe configuration
	 * @ctx		: mixer ctx pointer
	 * Returns: 0 on success or -error
	 */
	int (*clear_all_blendstages)(struct dpu_hw_mixer *ctx);

	/**
	 * Configure layer mixer to pipe configuration
	 * @ctx		: mixer ctx pointer
	 * @lm		: layer mixer enumeration
	 * @stage_cfg	: blend stage configuration
	 * Returns: 0 on success or -error
	 */
	int (*setup_blendstage)(struct dpu_hw_mixer *ctx, enum dpu_lm lm,
				struct dpu_hw_stage_cfg *stage_cfg);

	/**
	 * setup_border_color : enable/disable border color
	 */
	void (*setup_border_color)(struct dpu_hw_mixer *ctx,
		struct dpu_mdss_color *color,
		u8 border_en);

	/**
	 * setup_misr: Enable/disable MISR
	 */
	void (*setup_misr)(struct dpu_hw_mixer *ctx);

	/**
	 * collect_misr: Read MISR signature
	 */
	int (*collect_misr)(struct dpu_hw_mixer *ctx, u32 *misr_value);
};

struct dpu_hw_mixer {
	struct dpu_hw_blk base;
	struct dpu_hw_blk_reg_map hw;

	/* lm */
	enum dpu_lm  idx;
	const struct dpu_lm_cfg   *cap;
	const struct dpu_mdp_cfg  *mdp;
	const struct dpu_ctl_cfg  *ctl;

	/* ops */
	struct dpu_hw_lm_ops ops;

	/* store mixer info specific to display */
	struct dpu_hw_mixer_cfg cfg;
};

/**
 * to_dpu_hw_mixer - convert base object dpu_hw_base to container
 * @hw: Pointer to base hardware block
 * return: Pointer to hardware block container
 */
static inline struct dpu_hw_mixer *to_dpu_hw_mixer(struct dpu_hw_blk *hw)
{
	return container_of(hw, struct dpu_hw_mixer, base);
}

struct dpu_hw_mixer *dpu_hw_lm_init(struct drm_device *dev,
				    const struct dpu_lm_cfg *cfg,
				    void __iomem *addr,
				    const struct dpu_mdss_version *mdss_ver);

#endif /*_DPU_HW_LM_H */
