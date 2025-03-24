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
 * @hw_intf: array of intf hardware resources
 * @hw_wb: array of wb hardware resources
 * @hw_cwb: array of cwb hardware resources
 * @dspp_blks: array of dspp hardware resources
 * @hw_sspp: array of sspp hardware resources
 * @cdm_blk: cdm hardware resource
 */
struct dpu_rm {
	struct dpu_hw_blk *pingpong_blks[PINGPONG_MAX - PINGPONG_0];
	struct dpu_hw_blk *mixer_blks[LM_MAX - LM_0];
	struct dpu_hw_blk *ctl_blks[CTL_MAX - CTL_0];
	struct dpu_hw_intf *hw_intf[INTF_MAX - INTF_0];
	struct dpu_hw_wb *hw_wb[WB_MAX - WB_0];
	struct dpu_hw_blk *cwb_blks[CWB_MAX - CWB_0];
	struct dpu_hw_blk *dspp_blks[DSPP_MAX - DSPP_0];
	struct dpu_hw_blk *merge_3d_blks[MERGE_3D_MAX - MERGE_3D_0];
	struct dpu_hw_blk *dsc_blks[DSC_MAX - DSC_0];
	struct dpu_hw_sspp *hw_sspp[SSPP_MAX - SSPP_NONE];
	struct dpu_hw_blk *cdm_blk;
};

struct dpu_rm_sspp_requirements {
	bool yuv;
	bool scale;
	bool rot90;
};

/**
 * struct msm_display_topology - defines a display topology pipeline
 * @num_lm:       number of layer mixers used
 * @num_intf:     number of interfaces the panel is mounted on
 * @num_dspp:     number of dspp blocks used
 * @num_dsc:      number of Display Stream Compression (DSC) blocks used
 * @needs_cdm:    indicates whether cdm block is needed for this display topology
 */
struct msm_display_topology {
	u32 num_lm;
	u32 num_intf;
	u32 num_dspp;
	u32 num_dsc;
	bool needs_cdm;
};

int dpu_rm_init(struct drm_device *dev,
		struct dpu_rm *rm,
		const struct dpu_mdss_cfg *cat,
		const struct msm_mdss_data *mdss_data,
		void __iomem *mmio);

int dpu_rm_reserve(struct dpu_rm *rm,
		struct dpu_global_state *global_state,
		struct drm_encoder *drm_enc,
		struct drm_crtc_state *crtc_state,
		struct msm_display_topology *topology);

void dpu_rm_release(struct dpu_global_state *global_state,
		struct drm_encoder *enc);

struct dpu_hw_sspp *dpu_rm_reserve_sspp(struct dpu_rm *rm,
					struct dpu_global_state *global_state,
					struct drm_crtc *crtc,
					struct dpu_rm_sspp_requirements *reqs);

void dpu_rm_release_all_sspp(struct dpu_global_state *global_state,
			     struct drm_crtc *crtc);

int dpu_rm_get_assigned_resources(struct dpu_rm *rm,
	struct dpu_global_state *global_state, uint32_t enc_id,
	enum dpu_hw_blk_type type, struct dpu_hw_blk **blks, int blks_size);

void dpu_rm_print_state(struct drm_printer *p,
			const struct dpu_global_state *global_state);

/**
 * dpu_rm_get_intf - Return a struct dpu_hw_intf instance given it's index.
 * @rm: DPU Resource Manager handle
 * @intf_idx: INTF's index
 */
static inline struct dpu_hw_intf *dpu_rm_get_intf(struct dpu_rm *rm, enum dpu_intf intf_idx)
{
	return rm->hw_intf[intf_idx - INTF_0];
}

/**
 * dpu_rm_get_wb - Return a struct dpu_hw_wb instance given it's index.
 * @rm: DPU Resource Manager handle
 * @wb_idx: WB index
 */
static inline struct dpu_hw_wb *dpu_rm_get_wb(struct dpu_rm *rm, enum dpu_wb wb_idx)
{
	return rm->hw_wb[wb_idx - WB_0];
}

/**
 * dpu_rm_get_sspp - Return a struct dpu_hw_sspp instance given it's index.
 * @rm: DPU Resource Manager handle
 * @sspp_idx: SSPP index
 */
static inline struct dpu_hw_sspp *dpu_rm_get_sspp(struct dpu_rm *rm, enum dpu_sspp sspp_idx)
{
	return rm->hw_sspp[sspp_idx - SSPP_NONE];
}

#endif /* __DPU_RM_H__ */

