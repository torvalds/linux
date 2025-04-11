/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_FBC_H__
#define __INTEL_FBC_H__

#include <linux/types.h>

enum fb_op_origin;
struct intel_atomic_state;
struct intel_crtc;
struct intel_crtc_state;
struct intel_display;
struct intel_dsb;
struct intel_fbc;
struct intel_plane;
struct intel_plane_state;

enum intel_fbc_id {
	INTEL_FBC_A,
	INTEL_FBC_B,
	INTEL_FBC_C,
	INTEL_FBC_D,

	I915_MAX_FBCS,
};

int intel_fbc_atomic_check(struct intel_atomic_state *state);
bool intel_fbc_pre_update(struct intel_atomic_state *state,
			  struct intel_crtc *crtc);
void intel_fbc_post_update(struct intel_atomic_state *state,
			   struct intel_crtc *crtc);
void intel_fbc_init(struct intel_display *display);
void intel_fbc_cleanup(struct intel_display *display);
void intel_fbc_sanitize(struct intel_display *display);
void intel_fbc_update(struct intel_atomic_state *state,
		      struct intel_crtc *crtc);
void intel_fbc_disable(struct intel_crtc *crtc);
void intel_fbc_invalidate(struct intel_display *display,
			  unsigned int frontbuffer_bits,
			  enum fb_op_origin origin);
void intel_fbc_flush(struct intel_display *display,
		     unsigned int frontbuffer_bits, enum fb_op_origin origin);
void intel_fbc_add_plane(struct intel_fbc *fbc, struct intel_plane *plane);
void intel_fbc_handle_fifo_underrun_irq(struct intel_display *display);
void intel_fbc_reset_underrun(struct intel_display *display);
void intel_fbc_crtc_debugfs_add(struct intel_crtc *crtc);
void intel_fbc_debugfs_register(struct intel_display *display);
void intel_fbc_prepare_dirty_rect(struct intel_atomic_state *state,
				  struct intel_crtc *crtc);
void intel_fbc_dirty_rect_update_noarm(struct intel_dsb *dsb,
				       struct intel_plane *plane);

#endif /* __INTEL_FBC_H__ */
