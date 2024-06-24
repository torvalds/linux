/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022-2023 Intel Corporation
 */

#ifndef __INTEL_VBLANK_H__
#define __INTEL_VBLANK_H__

#include <linux/ktime.h>
#include <linux/types.h>

struct drm_crtc;
struct drm_display_mode;
struct intel_crtc;
struct intel_crtc_state;

struct intel_vblank_evade_ctx {
	struct intel_crtc *crtc;
	int min, max, vblank_start;
	bool need_vlv_dsi_wa;
};

int intel_mode_vdisplay(const struct drm_display_mode *mode);
int intel_mode_vblank_start(const struct drm_display_mode *mode);
int intel_mode_vblank_end(const struct drm_display_mode *mode);
int intel_mode_vtotal(const struct drm_display_mode *mode);

void intel_vblank_evade_init(const struct intel_crtc_state *old_crtc_state,
			     const struct intel_crtc_state *new_crtc_state,
			     struct intel_vblank_evade_ctx *evade);
/* must be called with vblank interrupt already enabled! */
int intel_vblank_evade(struct intel_vblank_evade_ctx *evade);

u32 i915_get_vblank_counter(struct drm_crtc *crtc);
u32 g4x_get_vblank_counter(struct drm_crtc *crtc);
bool intel_crtc_get_vblank_timestamp(struct drm_crtc *crtc, int *max_error,
				     ktime_t *vblank_time, bool in_vblank_irq);
int intel_get_crtc_scanline(struct intel_crtc *crtc);
void intel_wait_for_pipe_scanline_stopped(struct intel_crtc *crtc);
void intel_wait_for_pipe_scanline_moving(struct intel_crtc *crtc);
void intel_crtc_update_active_timings(const struct intel_crtc_state *crtc_state,
				      bool vrr_enable);
int intel_crtc_scanline_to_hw(const struct intel_crtc_state *crtc_state,
			      int scanline);

#endif /* __INTEL_VBLANK_H__ */
