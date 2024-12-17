// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved
 */

#include <drm/drm_managed.h>
#include "dpu_hw_cwb.h"

#include <linux/bitfield.h>

#define CWB_MUX              0x000
#define CWB_MODE             0x004

/* CWB mux block bit definitions */
#define CWB_MUX_MASK         GENMASK(3, 0)
#define CWB_MODE_MASK        GENMASK(2, 0)

static void dpu_hw_cwb_config(struct dpu_hw_cwb *ctx,
			      struct dpu_hw_cwb_setup_cfg *cwb_cfg)
{
	struct dpu_hw_blk_reg_map *c = &ctx->hw;
	int cwb_mux_cfg = 0xF;
	enum dpu_pingpong pp;
	enum cwb_mode_input input;

	if (!cwb_cfg)
		return;

	input = cwb_cfg->input;
	pp = cwb_cfg->pp_idx;

	if (input >= INPUT_MODE_MAX)
		return;

	/*
	 * The CWB_MUX register takes the pingpong index for the real-time
	 * display
	 */
	if ((pp != PINGPONG_NONE) && (pp < PINGPONG_MAX))
		cwb_mux_cfg = FIELD_PREP(CWB_MUX_MASK, pp - PINGPONG_0);

	input = FIELD_PREP(CWB_MODE_MASK, input);

	DPU_REG_WRITE(c, CWB_MUX, cwb_mux_cfg);
	DPU_REG_WRITE(c, CWB_MODE, input);
}

/**
 * dpu_hw_cwb_init() - Initializes the writeback hw driver object with cwb.
 * @dev:  Corresponding device for devres management
 * @cfg:  wb_path catalog entry for which driver object is required
 * @addr: mapped register io address of MDP
 * Return: Error code or allocated dpu_hw_wb context
 */
struct dpu_hw_cwb *dpu_hw_cwb_init(struct drm_device *dev,
				   const struct dpu_cwb_cfg *cfg,
				   void __iomem *addr)
{
	struct dpu_hw_cwb *c;

	if (!addr)
		return ERR_PTR(-EINVAL);

	c = drmm_kzalloc(dev, sizeof(*c), GFP_KERNEL);
	if (!c)
		return ERR_PTR(-ENOMEM);

	c->hw.blk_addr = addr + cfg->base;
	c->hw.log_mask = DPU_DBG_MASK_CWB;

	c->idx = cfg->id;
	c->ops.config_cwb = dpu_hw_cwb_config;

	return c;
}
