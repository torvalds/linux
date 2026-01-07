/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef _INTEL_CRTC_H_
#define _INTEL_CRTC_H_

#include <linux/types.h>

enum i9xx_plane_id;
enum pipe;
struct drm_device;
struct drm_display_mode;
struct drm_file;
struct drm_pending_vblank_event;
struct intel_atomic_state;
struct intel_crtc;
struct intel_crtc_state;
struct intel_display;

/*
 * FIXME: We should instead only take spinlocks once for the entire update
 * instead of once per mmio.
 */
#if IS_ENABLED(CONFIG_PROVE_LOCKING)
#define VBLANK_EVASION_TIME_US 250
#else
#define VBLANK_EVASION_TIME_US 100
#endif

int intel_usecs_to_scanlines(const struct drm_display_mode *adjusted_mode,
			     int usecs);
int intel_scanlines_to_usecs(const struct drm_display_mode *adjusted_mode,
			     int scanlines);
void intel_crtc_arm_vblank_event(struct intel_crtc_state *crtc_state);
void intel_crtc_prepare_vblank_event(struct intel_crtc_state *crtc_state,
				     struct drm_pending_vblank_event **event);
u32 intel_crtc_max_vblank_count(const struct intel_crtc_state *crtc_state);
int intel_crtc_init(struct intel_display *display);
int intel_crtc_get_pipe_from_crtc_id_ioctl(struct drm_device *dev, void *data,
					   struct drm_file *file_priv);
struct intel_crtc_state *intel_crtc_state_alloc(struct intel_crtc *crtc);
void intel_crtc_state_reset(struct intel_crtc_state *crtc_state,
			    struct intel_crtc *crtc);
u32 intel_crtc_get_vblank_counter(struct intel_crtc *crtc);
void intel_crtc_vblank_on(const struct intel_crtc_state *crtc_state);
void intel_crtc_vblank_off(const struct intel_crtc_state *crtc_state);
void intel_pipe_update_start(struct intel_atomic_state *state,
			     struct intel_crtc *crtc);
void intel_pipe_update_end(struct intel_atomic_state *state,
			   struct intel_crtc *crtc);
void intel_wait_for_vblank_workers(struct intel_atomic_state *state);
struct intel_crtc *intel_first_crtc(struct intel_display *display);
struct intel_crtc *intel_crtc_for_pipe(struct intel_display *display,
				       enum pipe pipe);
void intel_wait_for_vblank_if_active(struct intel_display *display,
				     enum pipe pipe);
void intel_crtc_wait_for_next_vblank(struct intel_crtc *crtc);

bool intel_any_crtc_enable_changed(struct intel_atomic_state *state);
bool intel_crtc_enable_changed(const struct intel_crtc_state *old_crtc_state,
			       const struct intel_crtc_state *new_crtc_state);
bool intel_any_crtc_active_changed(struct intel_atomic_state *state);
bool intel_crtc_active_changed(const struct intel_crtc_state *old_crtc_state,
			       const struct intel_crtc_state *new_crtc_state);

unsigned int intel_crtc_bw_num_active_planes(const struct intel_crtc_state *crtc_state);
unsigned int intel_crtc_bw_data_rate(const struct intel_crtc_state *crtc_state);
int intel_crtc_bw_min_cdclk(const struct intel_crtc_state *crtc_state);

#endif
