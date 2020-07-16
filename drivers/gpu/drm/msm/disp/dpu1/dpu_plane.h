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
 * @aspace:	pointer to address space for input/output buffers
 * @stage:	assigned by crtc blender
 * @multirect_index: index of the rectangle of SSPP
 * @multirect_mode: parallel or time multiplex multirect mode
 * @pending:	whether the current update is still pending
 * @scaler3_cfg: configuration data for scaler3
 * @pixel_ext: configuration data for pixel extensions
 * @cdp_cfg:	CDP configuration
 * @plane_fetch_bw: calculated BW per plane
 * @plane_clk: calculated clk per plane
 */
struct dpu_plane_state {
	struct drm_plane_state base;
	struct msm_gem_address_space *aspace;
	enum dpu_stage stage;
	uint32_t multirect_index;
	uint32_t multirect_mode;
	bool pending;

	/* scaler configuration */
	struct dpu_hw_scaler3_cfg scaler3_cfg;
	struct dpu_hw_pixel_ext pixel_ext;

	struct dpu_hw_pipe_cdp_cfg cdp_cfg;
	u64 plane_fetch_bw;
	u64 plane_clk;
};

/**
 * struct dpu_multirect_plane_states: Defines multirect pair of drm plane states
 * @r0: drm plane configured on rect 0
 * @r1: drm plane configured on rect 1
 */
struct dpu_multirect_plane_states {
	const struct drm_plane_state *r0;
	const struct drm_plane_state *r1;
};

#define to_dpu_plane_state(x) \
	container_of(x, struct dpu_plane_state, base)

/**
 * dpu_plane_pipe - return sspp identifier for the given plane
 * @plane:   Pointer to DRM plane object
 * Returns: sspp identifier of the given plane
 */
enum dpu_sspp dpu_plane_pipe(struct drm_plane *plane);

/**
 * is_dpu_plane_virtual - check for virtual plane
 * @plane: Pointer to DRM plane object
 * returns: true - if the plane is virtual
 *          false - if the plane is primary
 */
bool is_dpu_plane_virtual(struct drm_plane *plane);

/**
 * dpu_plane_get_ctl_flush - get control flush mask
 * @plane:   Pointer to DRM plane object
 * @ctl: Pointer to control hardware
 * @flush_sspp: Pointer to sspp flush control word
 */
void dpu_plane_get_ctl_flush(struct drm_plane *plane, struct dpu_hw_ctl *ctl,
		u32 *flush_sspp);

/**
 * dpu_plane_restore - restore hw state if previously power collapsed
 * @plane: Pointer to drm plane structure
 */
void dpu_plane_restore(struct drm_plane *plane);

/**
 * dpu_plane_flush - final plane operations before commit flush
 * @plane: Pointer to drm plane structure
 */
void dpu_plane_flush(struct drm_plane *plane);

/**
 * dpu_plane_set_error: enable/disable error condition
 * @plane: pointer to drm_plane structure
 */
void dpu_plane_set_error(struct drm_plane *plane, bool error);

/**
 * dpu_plane_init - create new dpu plane for the given pipe
 * @dev:   Pointer to DRM device
 * @pipe:  dpu hardware pipe identifier
 * @type:  Plane type - PRIMARY/OVERLAY/CURSOR
 * @possible_crtcs: bitmask of crtc that can be attached to the given pipe
 * @master_plane_id: primary plane id of a multirect pipe. 0 value passed for
 *                   a regular plane initialization. A non-zero primary plane
 *                   id will be passed for a virtual pipe initialization.
 *
 */
struct drm_plane *dpu_plane_init(struct drm_device *dev,
		uint32_t pipe, enum drm_plane_type type,
		unsigned long possible_crtcs, u32 master_plane_id);

/**
 * dpu_plane_validate_multirecti_v2 - validate the multirect planes
 *				      against hw limitations
 * @plane: drm plate states of the multirect pair
 */
int dpu_plane_validate_multirect_v2(struct dpu_multirect_plane_states *plane);

/**
 * dpu_plane_clear_multirect - clear multirect bits for the given pipe
 * @drm_state: Pointer to DRM plane state
 */
void dpu_plane_clear_multirect(const struct drm_plane_state *drm_state);

/**
 * dpu_plane_color_fill - enables color fill on plane
 * @plane:  Pointer to DRM plane object
 * @color:  RGB fill color value, [23..16] Blue, [15..8] Green, [7..0] Red
 * @alpha:  8-bit fill alpha value, 255 selects 100% alpha
 * Returns: 0 on success
 */
int dpu_plane_color_fill(struct drm_plane *plane,
		uint32_t color, uint32_t alpha);

#endif /* _DPU_PLANE_H_ */
