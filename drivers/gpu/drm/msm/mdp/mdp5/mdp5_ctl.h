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
int mdp5_ctl_get_ctl_id(struct mdp5_ctl *ctl);

struct mdp5_interface;
int mdp5_ctl_set_intf(struct mdp5_ctl *ctl, struct mdp5_interface *intf);
int mdp5_ctl_set_encoder_state(struct mdp5_ctl *ctl, bool enabled);

int mdp5_ctl_set_cursor(struct mdp5_ctl *ctl, int cursor_id, bool enable);

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
 * mdp5_ctl_blend() - Blend multiple layers on a Layer Mixer (LM)
 *
 * @blend_cfg: see LM blender config definition below
 *
 * Note:
 * CTL registers need to be flushed after calling this function
 * (call mdp5_ctl_commit() with mdp_ctl_flush_mask_ctl() mask)
 */
int mdp5_ctl_blend(struct mdp5_ctl *ctl, u32 lm, u32 blend_cfg);

/**
 * mdp_ctl_flush_mask...() - Register FLUSH masks
 *
 * These masks are used to specify which block(s) need to be flushed
 * through @flush_mask parameter in mdp5_ctl_commit(.., flush_mask).
 */
u32 mdp_ctl_flush_mask_lm(int lm);
u32 mdp_ctl_flush_mask_pipe(enum mdp5_pipe pipe);
u32 mdp_ctl_flush_mask_cursor(int cursor_id);
u32 mdp_ctl_flush_mask_encoder(struct mdp5_interface *intf);

/* @flush_mask: see CTL flush masks definitions below */
int mdp5_ctl_commit(struct mdp5_ctl *ctl, u32 flush_mask);

void mdp5_ctl_release(struct mdp5_ctl *ctl);



#endif /* __MDP5_CTL_H__ */
