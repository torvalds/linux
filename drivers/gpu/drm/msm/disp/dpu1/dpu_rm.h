/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 */

#ifndef __DPU_RM_H__
#define __DPU_RM_H__

#include <linux/list.h>

#include "msm_kms.h"
#include "dpu_hw_top.h"

struct dpu_global_state;

/**
 * struct dpu_rm - DPU dynamic hardware resource manager
 * @pingpong_blks: array of pingpong hardware resources
 * @mixer_blks: array of layer mixer hardware resources
 * @ctl_blks: array of ctl hardware resources
 * @intf_blks: array of intf hardware resources
 * @lm_max_width: cached layer mixer maximum width
 * @rm_lock: resource manager mutex
 */
struct dpu_rm {
	struct dpu_hw_blk *pingpong_blks[PINGPONG_MAX - PINGPONG_0];
	struct dpu_hw_blk *mixer_blks[LM_MAX - LM_0];
	struct dpu_hw_blk *ctl_blks[CTL_MAX - CTL_0];
	struct dpu_hw_blk *intf_blks[INTF_MAX - INTF_0];

	uint32_t lm_max_width;
};

/**
 * dpu_rm_init - Read hardware catalog and create reservation tracking objects
 *	for all HW blocks.
 * @rm: DPU Resource Manager handle
 * @cat: Pointer to hardware catalog
 * @mmio: mapped register io address of MDP
 * @Return: 0 on Success otherwise -ERROR
 */
int dpu_rm_init(struct dpu_rm *rm,
		struct dpu_mdss_cfg *cat,
		void __iomem *mmio);

/**
 * dpu_rm_destroy - Free all memory allocated by dpu_rm_init
 * @rm: DPU Resource Manager handle
 * @Return: 0 on Success otherwise -ERROR
 */
int dpu_rm_destroy(struct dpu_rm *rm);

/**
 * dpu_rm_reserve - Given a CRTC->Encoder->Connector display chain, analyze
 *	the use connections and user requirements, specified through related
 *	topology control properties, and reserve hardware blocks to that
 *	display chain.
 *	HW blocks can then be accessed through dpu_rm_get_* functions.
 *	HW Reservations should be released via dpu_rm_release_hw.
 * @rm: DPU Resource Manager handle
 * @drm_enc: DRM Encoder handle
 * @crtc_state: Proposed Atomic DRM CRTC State handle
 * @topology: Pointer to topology info for the display
 * @Return: 0 on Success otherwise -ERROR
 */
int dpu_rm_reserve(struct dpu_rm *rm,
		struct dpu_global_state *global_state,
		struct drm_encoder *drm_enc,
		struct drm_crtc_state *crtc_state,
		struct msm_display_topology topology);

/**
 * dpu_rm_reserve - Given the encoder for the display chain, release any
 *	HW blocks previously reserved for that use case.
 * @rm: DPU Resource Manager handle
 * @enc: DRM Encoder handle
 * @Return: 0 on Success otherwise -ERROR
 */
void dpu_rm_release(struct dpu_global_state *global_state,
		struct drm_encoder *enc);

/**
 * Get hw resources of the given type that are assigned to this encoder.
 */
int dpu_rm_get_assigned_resources(struct dpu_rm *rm,
	struct dpu_global_state *global_state, uint32_t enc_id,
	enum dpu_hw_blk_type type, struct dpu_hw_blk **blks, int blks_size);
#endif /* __DPU_RM_H__ */

