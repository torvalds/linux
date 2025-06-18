/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef __INTEL_WM_H__
#define __INTEL_WM_H__

#include <linux/types.h>

struct intel_atomic_state;
struct intel_crtc;
struct intel_crtc_state;
struct intel_display;
struct intel_plane_state;

void intel_update_watermarks(struct intel_display *display);
int intel_wm_compute(struct intel_atomic_state *state,
		     struct intel_crtc *crtc);
bool intel_initial_watermarks(struct intel_atomic_state *state,
			      struct intel_crtc *crtc);
void intel_atomic_update_watermarks(struct intel_atomic_state *state,
				    struct intel_crtc *crtc);
void intel_optimize_watermarks(struct intel_atomic_state *state,
			       struct intel_crtc *crtc);
int intel_compute_global_watermarks(struct intel_atomic_state *state);
void intel_wm_get_hw_state(struct intel_display *display);
void intel_wm_sanitize(struct intel_display *display);
bool intel_wm_plane_visible(const struct intel_crtc_state *crtc_state,
			    const struct intel_plane_state *plane_state);
void intel_print_wm_latency(struct intel_display *display,
			    const char *name, const u16 wm[]);
void intel_wm_init(struct intel_display *display);
void intel_wm_debugfs_register(struct intel_display *display);

#endif /* __INTEL_WM_H__ */
