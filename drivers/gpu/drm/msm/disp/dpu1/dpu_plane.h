/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#ifndef _DPU_PLANE_H_
#define _DPU_PLANE_H_

#include <drm/drm_crtc.h>

#include "dpu_kms.h"
#include "dpu_hw_mdss.h"
#include "dpu_hw_sspp.h"

/**
 * struct dpu_plane_state: Define dpu extension of drm plane state object
 * @base:	base drm plane state object
 * @pipe:	software pipe description
 * @r_pipe:	software pipe description of the second pipe
 * @pipe_cfg:	software pipe configuration
 * @r_pipe_cfg:	software pipe configuration for the second pipe
 * @stage:	assigned by crtc blender
 * @needs_qos_remap: qos remap settings need to be updated
 * @multirect_index: index of the rectangle of SSPP
 * @multirect_mode: parallel or time multiplex multirect mode
 * @pending:	whether the current update is still pending
 * @plane_fetch_bw: calculated BW per plane
 * @plane_clk: calculated clk per plane
 * @needs_dirtyfb: whether attached CRTC needs pixel data explicitly flushed
 * @layout:     framebuffer memory layout
 */
struct dpu_plane_state {
	struct drm_plane_state base;
	struct dpu_sw_pipe pipe;
	struct dpu_sw_pipe r_pipe;
	struct dpu_sw_pipe_cfg pipe_cfg;
	struct dpu_sw_pipe_cfg r_pipe_cfg;
	enum dpu_stage stage;
	bool needs_qos_remap;
	bool pending;

	u64 plane_fetch_bw;
	u64 plane_clk;

	bool needs_dirtyfb;

	struct dpu_hw_fmt_layout layout;
};

#define to_dpu_plane_state(x) \
	container_of(x, struct dpu_plane_state, base)

void dpu_plane_flush(struct drm_plane *plane);

void dpu_plane_set_error(struct drm_plane *plane, bool error);

struct drm_plane *dpu_plane_init(struct drm_device *dev,
		uint32_t pipe, enum drm_plane_type type,
		unsigned long possible_crtcs);

struct drm_plane *dpu_plane_init_virtual(struct drm_device *dev,
					 enum drm_plane_type type,
					 unsigned long possible_crtcs);

int dpu_plane_color_fill(struct drm_plane *plane,
		uint32_t color, uint32_t alpha);

#ifdef CONFIG_DEBUG_FS
void dpu_plane_danger_signal_ctrl(struct drm_plane *plane, bool enable);
#else
static inline void dpu_plane_danger_signal_ctrl(struct drm_plane *plane, bool enable) {}
#endif

int dpu_assign_plane_resources(struct dpu_global_state *global_state,
			       struct drm_atomic_state *state,
			       struct drm_crtc *crtc,
			       struct drm_plane_state **states,
			       unsigned int num_planes);

#endif /* _DPU_PLANE_H_ */
