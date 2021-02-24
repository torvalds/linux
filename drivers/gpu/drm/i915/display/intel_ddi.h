/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_DDI_H__
#define __INTEL_DDI_H__

#include "intel_display.h"
#include "i915_reg.h"

struct drm_connector_state;
struct drm_i915_private;
struct intel_connector;
struct intel_crtc;
struct intel_crtc_state;
struct intel_dp;
struct intel_dpll_hw_state;
struct intel_encoder;
enum transcoder;

i915_reg_t dp_tp_ctl_reg(struct intel_encoder *encoder,
			 const struct intel_crtc_state *crtc_state);
i915_reg_t dp_tp_status_reg(struct intel_encoder *encoder,
			    const struct intel_crtc_state *crtc_state);
void intel_ddi_fdi_post_disable(struct intel_atomic_state *state,
				struct intel_encoder *intel_encoder,
				const struct intel_crtc_state *old_crtc_state,
				const struct drm_connector_state *old_conn_state);
void hsw_fdi_link_train(struct intel_encoder *encoder,
			const struct intel_crtc_state *crtc_state);
void intel_ddi_init(struct drm_i915_private *dev_priv, enum port port);
bool intel_ddi_get_hw_state(struct intel_encoder *encoder, enum pipe *pipe);
void intel_ddi_enable_transcoder_func(struct intel_encoder *encoder,
				      const struct intel_crtc_state *crtc_state);
void intel_ddi_disable_transcoder_func(const struct intel_crtc_state *crtc_state);
void intel_ddi_enable_pipe_clock(struct intel_encoder *encoder,
				 const struct intel_crtc_state *crtc_state);
void intel_ddi_disable_pipe_clock(const  struct intel_crtc_state *crtc_state);
void intel_ddi_set_dp_msa(const struct intel_crtc_state *crtc_state,
			  const struct drm_connector_state *conn_state);
bool intel_ddi_connector_get_hw_state(struct intel_connector *intel_connector);
void intel_ddi_get_config(struct intel_encoder *encoder,
			  struct intel_crtc_state *pipe_config);
void intel_ddi_set_vc_payload_alloc(const struct intel_crtc_state *crtc_state,
				    bool state);
void intel_ddi_compute_min_voltage_level(struct drm_i915_private *dev_priv,
					 struct intel_crtc_state *crtc_state);
u32 bxt_signal_levels(struct intel_dp *intel_dp,
		      const struct intel_crtc_state *crtc_state);
u32 ddi_signal_levels(struct intel_dp *intel_dp,
		      const struct intel_crtc_state *crtc_state);
int intel_ddi_toggle_hdcp_signalling(struct intel_encoder *intel_encoder,
				     enum transcoder cpu_transcoder,
				     bool enable);
void icl_sanitize_encoder_pll_mapping(struct intel_encoder *encoder);

#endif /* __INTEL_DDI_H__ */
