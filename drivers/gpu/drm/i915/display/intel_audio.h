/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_AUDIO_H__
#define __INTEL_AUDIO_H__

#include <linux/types.h>

struct drm_connector_state;
struct intel_crtc_state;
struct intel_display;
struct intel_encoder;

void intel_audio_hooks_init(struct intel_display *display);
bool intel_audio_compute_config(struct intel_encoder *encoder,
				struct intel_crtc_state *crtc_state,
				struct drm_connector_state *conn_state);
void intel_audio_codec_enable(struct intel_encoder *encoder,
			      const struct intel_crtc_state *crtc_state,
			      const struct drm_connector_state *conn_state);
void intel_audio_codec_disable(struct intel_encoder *encoder,
			       const struct intel_crtc_state *old_crtc_state,
			       const struct drm_connector_state *old_conn_state);
void intel_audio_codec_get_config(struct intel_encoder *encoder,
				  struct intel_crtc_state *crtc_state);
void intel_audio_cdclk_change_pre(struct intel_display *display);
void intel_audio_cdclk_change_post(struct intel_display *display);
int intel_audio_min_cdclk(const struct intel_crtc_state *crtc_state);
void intel_audio_init(struct intel_display *display);
void intel_audio_register(struct intel_display *display);
void intel_audio_deinit(struct intel_display *display);
void intel_audio_sdp_split_update(const struct intel_crtc_state *crtc_state);

#endif /* __INTEL_AUDIO_H__ */
