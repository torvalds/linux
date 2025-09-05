/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_PSR_H__
#define __INTEL_PSR_H__

#include <linux/types.h>

enum fb_op_origin;
struct drm_connector;
struct drm_connector_state;
struct intel_atomic_state;
struct intel_connector;
struct intel_crtc;
struct intel_crtc_state;
struct intel_display;
struct intel_dp;
struct intel_dsb;
struct intel_encoder;
struct intel_plane;
struct intel_plane_state;

#define CAN_PANEL_REPLAY(intel_dp) ((intel_dp)->psr.sink_panel_replay_support && \
				    (intel_dp)->psr.source_panel_replay_support)

bool intel_encoder_can_psr(struct intel_encoder *encoder);
bool intel_psr_needs_aux_io_power(struct intel_encoder *encoder,
				  const struct intel_crtc_state *crtc_state);
void intel_psr_init_dpcd(struct intel_dp *intel_dp);
void intel_psr_panel_replay_enable_sink(struct intel_dp *intel_dp);
void intel_psr_pre_plane_update(struct intel_atomic_state *state,
				struct intel_crtc *crtc);
void intel_psr_post_plane_update(struct intel_atomic_state *state,
				 struct intel_crtc *crtc);
void intel_psr_disable(struct intel_dp *intel_dp,
		       const struct intel_crtc_state *old_crtc_state);
int intel_psr_debug_set(struct intel_dp *intel_dp, u64 value);
void intel_psr_invalidate(struct intel_display *display,
			  unsigned frontbuffer_bits,
			  enum fb_op_origin origin);
void intel_psr_flush(struct intel_display *display,
		     unsigned frontbuffer_bits,
		     enum fb_op_origin origin);
void intel_psr_init(struct intel_dp *intel_dp);
void intel_psr_compute_config(struct intel_dp *intel_dp,
			      struct intel_crtc_state *crtc_state,
			      struct drm_connector_state *conn_state);
void intel_psr_get_config(struct intel_encoder *encoder,
			  struct intel_crtc_state *pipe_config);
void intel_psr_irq_handler(struct intel_dp *intel_dp, u32 psr_iir);
void intel_psr_short_pulse(struct intel_dp *intel_dp);
void intel_psr_wait_for_idle_locked(const struct intel_crtc_state *new_crtc_state);
void intel_psr_wait_for_idle_dsb(struct intel_dsb *dsb,
				 const struct intel_crtc_state *new_crtc_state);
bool intel_psr_enabled(struct intel_dp *intel_dp);
int intel_psr2_sel_fetch_update(struct intel_atomic_state *state,
				struct intel_crtc *crtc);
void intel_psr2_program_trans_man_trk_ctl(struct intel_dsb *dsb,
					  const struct intel_crtc_state *crtc_state);
void intel_psr2_panic_force_full_update(struct intel_display *display,
					struct intel_crtc_state *crtc_state);
void intel_psr_pause(struct intel_dp *intel_dp);
void intel_psr_resume(struct intel_dp *intel_dp);
bool intel_psr_needs_vblank_notification(const struct intel_crtc_state *crtc_state);
void intel_psr_notify_pipe_change(struct intel_atomic_state *state,
				  struct intel_crtc *crtc, bool enable);
void intel_psr_notify_dc5_dc6(struct intel_display *display);
void intel_psr_dc5_dc6_wa_init(struct intel_display *display);
void intel_psr_notify_vblank_enable_disable(struct intel_display *display,
					    bool enable);
bool intel_psr_link_ok(struct intel_dp *intel_dp);

void intel_psr_lock(const struct intel_crtc_state *crtc_state);
void intel_psr_unlock(const struct intel_crtc_state *crtc_state);
void intel_psr_trigger_frame_change_event(struct intel_dsb *dsb,
					  struct intel_atomic_state *state,
					  struct intel_crtc *crtc);
int intel_psr_min_vblank_delay(const struct intel_crtc_state *crtc_state);
void intel_psr_connector_debugfs_add(struct intel_connector *connector);
void intel_psr_debugfs_register(struct intel_display *display);
bool intel_psr_needs_alpm(struct intel_dp *intel_dp, const struct intel_crtc_state *crtc_state);
bool intel_psr_needs_alpm_aux_less(struct intel_dp *intel_dp,
				   const struct intel_crtc_state *crtc_state);

#endif /* __INTEL_PSR_H__ */
