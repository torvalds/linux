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
		void __iomem *mmio_base, struct mdp5_cfg_handler *cfg_hnd);
void mdp5_ctlm_hw_reset(struct mdp5_ctl_manager *ctlm);
void mdp5_ctlm_destroy(struct mdp5_ctl_manager *ctlm);

/*
 * CTL prototypes:
 * mdp5_ctl_request(ctlm, ...) returns a ctl (CTL resource) handler,
 * which is then used to call the other mdp5_ctl_*(ctl, ...) functions.
 */
struct mdp5_ctl *mdp5_ctlm_request(struct mdp5_ctl_manager *ctlm, int intf_num);

int mdp5_ctl_get_ctl_id(struct mdp5_ctl *ctl);

struct mdp5_interface;
struct mdp5_pipeline;
int mdp5_ctl_set_pipeline(struct mdp5_ctl *ctl, struct mdp5_pipeline *p);
int mdp5_ctl_set_encoder_state(struct mdp5_ctl *ctl, struct mdp5_pipeline *p,
			       bool enabled);

int mdp5_ctl_set_cursor(struct mdp5_ctl *ctl, struct mdp5_pipeline *pipeline,
			int cursor_id, bool enable);
int mdp5_ctl_pair(struct mdp5_ctl *ctlx, struct mdp5_ctl *ctly, bool enable);

#define MAX_PIPE_STAGE		2

/*
 * mdp5_ctl_blend() - Blend multiple layers on a Layer Mixer (LM)
 *
 * @stage: array to contain the pipe num for each stage
 * @stage_cnt: valid stage number in stage array
 * @ctl_blend_op_flags: blender operation mode flags
 *
 * Note:
 * CTL registers need to be flushed after calling this function
 * (call mdp5_ctl_commit() with mdp_ctl_flush_mask_ctl() mask)
 */
#define MDP5_CTL_BLEND_OP_FLAG_BORDER_OUT	BIT(0)
int mdp5_ctl_blend(struct mdp5_ctl *ctl, struct mdp5_pipeline *pipeline,
		   enum mdp5_pipe stage[][MAX_PIPE_STAGE],
		   enum mdp5_pipe r_stage[][MAX_PIPE_STAGE],
		   u32 stage_cnt, u32 ctl_blend_op_flags);

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
u32 mdp5_ctl_commit(struct mdp5_ctl *ctl, struct mdp5_pipeline *pipeline,
		    u32 flush_mask, bool start);
u32 mdp5_ctl_get_commit_status(struct mdp5_ctl *ctl);



#endif /* __MDP5_CTL_H__ */
