/*
 * Copyright (c) 2014 The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __MDP5_CTL_H__
#define __MDP5_CTL_H__

#include "msm_drv.h"

/*
 * CTL Manager prototypes:
 * mdp5_ctlm_init() returns a ctlm (CTL Manager) handler,
 * which is then used to call the other mdp5_ctlm_*(ctlm, ...) functions.
 */
struct mdp5_ctl_manager;
struct mdp5_ctl_manager *mdp5_ctlm_init(struct drm_device *dev,
		void __iomem *mmio_base, const struct mdp5_cfg_hw *hw_cfg);
void mdp5_ctlm_hw_reset(struct mdp5_ctl_manager *ctlm);
void mdp5_ctlm_destroy(struct mdp5_ctl_manager *ctlm);

/*
 * CTL prototypes:
 * mdp5_ctl_request(ctlm, ...) returns a ctl (CTL resource) handler,
 * which is then used to call the other mdp5_ctl_*(ctl, ...) functions.
 */
struct mdp5_ctl *mdp5_ctlm_request(struct mdp5_ctl_manager *ctlm, struct drm_crtc *crtc);

int mdp5_ctl_set_intf(struct mdp5_ctl *ctl, int intf);

int mdp5_ctl_set_cursor(struct mdp5_ctl *ctl, bool enable);

/* @blend_cfg: see LM blender config definition below */
int mdp5_ctl_blend(struct mdp5_ctl *ctl, u32 lm, u32 blend_cfg);

/* @flush_mask: see CTL flush masks definitions below */
int mdp5_ctl_commit(struct mdp5_ctl *ctl, u32 flush_mask);
u32 mdp5_ctl_get_flush(struct mdp5_ctl *ctl);

void mdp5_ctl_release(struct mdp5_ctl *ctl);

/*
 * blend_cfg (LM blender config):
 *
 * The function below allows the caller of mdp5_ctl_blend() to specify how pipes
 * are being blended according to their stage (z-order), through @blend_cfg arg.
 */
static inline u32 mdp_ctl_blend_mask(enum mdp5_pipe pipe,
		enum mdp_mixer_stage_id stage)
{
	switch (pipe) {
	case SSPP_VIG0: return MDP5_CTL_LAYER_REG_VIG0(stage);
	case SSPP_VIG1: return MDP5_CTL_LAYER_REG_VIG1(stage);
	case SSPP_VIG2: return MDP5_CTL_LAYER_REG_VIG2(stage);
	case SSPP_RGB0: return MDP5_CTL_LAYER_REG_RGB0(stage);
	case SSPP_RGB1: return MDP5_CTL_LAYER_REG_RGB1(stage);
	case SSPP_RGB2: return MDP5_CTL_LAYER_REG_RGB2(stage);
	case SSPP_DMA0: return MDP5_CTL_LAYER_REG_DMA0(stage);
	case SSPP_DMA1: return MDP5_CTL_LAYER_REG_DMA1(stage);
	case SSPP_VIG3: return MDP5_CTL_LAYER_REG_VIG3(stage);
	case SSPP_RGB3: return MDP5_CTL_LAYER_REG_RGB3(stage);
	default:	return 0;
	}
}

/*
 * flush_mask (CTL flush masks):
 *
 * The following functions allow each DRM entity to get and store
 * their own flush mask.
 * Once stored, these masks will then be accessed through each DRM's
 * interface and used by the caller of mdp5_ctl_commit() to specify
 * which block(s) need to be flushed through @flush_mask parameter.
 */

#define MDP5_CTL_FLUSH_CURSOR_DUMMY	0x80000000

static inline u32 mdp_ctl_flush_mask_cursor(int cursor_id)
{
	/* TODO: use id once multiple cursor support is present */
	(void)cursor_id;

	return MDP5_CTL_FLUSH_CURSOR_DUMMY;
}

static inline u32 mdp_ctl_flush_mask_lm(int lm)
{
	switch (lm) {
	case 0:  return MDP5_CTL_FLUSH_LM0;
	case 1:  return MDP5_CTL_FLUSH_LM1;
	case 2:  return MDP5_CTL_FLUSH_LM2;
	case 5:  return MDP5_CTL_FLUSH_LM5;
	default: return 0;
	}
}

static inline u32 mdp_ctl_flush_mask_pipe(enum mdp5_pipe pipe)
{
	switch (pipe) {
	case SSPP_VIG0: return MDP5_CTL_FLUSH_VIG0;
	case SSPP_VIG1: return MDP5_CTL_FLUSH_VIG1;
	case SSPP_VIG2: return MDP5_CTL_FLUSH_VIG2;
	case SSPP_RGB0: return MDP5_CTL_FLUSH_RGB0;
	case SSPP_RGB1: return MDP5_CTL_FLUSH_RGB1;
	case SSPP_RGB2: return MDP5_CTL_FLUSH_RGB2;
	case SSPP_DMA0: return MDP5_CTL_FLUSH_DMA0;
	case SSPP_DMA1: return MDP5_CTL_FLUSH_DMA1;
	case SSPP_VIG3: return MDP5_CTL_FLUSH_VIG3;
	case SSPP_RGB3: return MDP5_CTL_FLUSH_RGB3;
	default:        return 0;
	}
}

#endif /* __MDP5_CTL_H__ */
