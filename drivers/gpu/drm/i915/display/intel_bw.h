/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_BW_H__
#define __INTEL_BW_H__

#include <drm/drm_atomic.h>

struct intel_atomic_state;
struct intel_bw_state;
struct intel_crtc;
struct intel_crtc_state;
struct intel_display;
struct intel_global_state;

struct intel_bw_state *to_intel_bw_state(struct intel_global_state *obj_state);

struct intel_bw_state *
intel_atomic_get_old_bw_state(struct intel_atomic_state *state);

struct intel_bw_state *
intel_atomic_get_new_bw_state(struct intel_atomic_state *state);

struct intel_bw_state *
intel_atomic_get_bw_state(struct intel_atomic_state *state);

void intel_bw_init_hw(struct intel_display *display);
int intel_bw_init(struct intel_display *display);
int intel_bw_atomic_check(struct intel_atomic_state *state, bool any_ms);
int intel_bw_calc_min_cdclk(struct intel_atomic_state *state,
			    bool *need_cdclk_calc);
int intel_bw_min_cdclk(struct intel_display *display,
		       const struct intel_bw_state *bw_state);
void intel_bw_update_hw_state(struct intel_display *display);
void intel_bw_crtc_disable_noatomic(struct intel_crtc *crtc);

bool intel_bw_pmdemand_needs_update(struct intel_atomic_state *state);
bool intel_bw_can_enable_sagv(struct intel_display *display,
			      const struct intel_bw_state *bw_state);
void icl_sagv_pre_plane_update(struct intel_atomic_state *state);
void icl_sagv_post_plane_update(struct intel_atomic_state *state);
int intel_bw_qgv_point_peakbw(const struct intel_bw_state *bw_state);

#endif /* __INTEL_BW_H__ */
