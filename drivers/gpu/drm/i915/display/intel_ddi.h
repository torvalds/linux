/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_DDI_H__
#define __INTEL_DDI_H__

#include "i915_reg_defs.h"

struct drm_connector_state;
struct drm_i915_private;
struct intel_atomic_state;
struct intel_bios_encoder_data;
struct intel_connector;
struct intel_crtc;
struct intel_crtc_state;
struct intel_display;
struct intel_dp;
struct intel_dpll_hw_state;
struct intel_encoder;
struct intel_shared_dpll;
enum pipe;
enum port;
enum transcoder;

i915_reg_t dp_tp_ctl_reg(struct intel_encoder *encoder,
			 const struct intel_crtc_state *crtc_state);
i915_reg_t dp_tp_status_reg(struct intel_encoder *encoder,
			    const struct intel_crtc_state *crtc_state);
i915_reg_t hsw_chicken_trans_reg(struct drm_i915_private *i915,
				 enum transcoder cpu_transcoder);
void intel_ddi_fdi_post_disable(struct intel_atomic_state *state,
				struct intel_encoder *intel_encoder,
				const struct intel_crtc_state *old_crtc_state,
				const struct drm_connector_state *old_conn_state);
void intel_ddi_enable_clock(struct intel_encoder *encoder,
			    const struct intel_crtc_state *crtc_state);
void intel_ddi_disable_clock(struct intel_encoder *encoder);
void intel_ddi_get_clock(struct intel_encoder *encoder,
			 struct intel_crtc_state *crtc_state,
			 struct intel_shared_dpll *pll);
void hsw_ddi_enable_clock(struct intel_encoder *encoder,
			  const struct intel_crtc_state *crtc_state);
void hsw_ddi_disable_clock(struct intel_encoder *encoder);
bool hsw_ddi_is_clock_enabled(struct intel_encoder *encoder);
enum icl_port_dpll_id
intel_ddi_port_pll_type(struct intel_encoder *encoder,
			const struct intel_crtc_state *crtc_state);
void hsw_ddi_get_config(struct intel_encoder *encoder,
			struct intel_crtc_state *crtc_state);
struct intel_shared_dpll *icl_ddi_combo_get_pll(struct intel_encoder *encoder);
void hsw_prepare_dp_ddi_buffers(struct intel_encoder *encoder,
				const struct intel_crtc_state *crtc_state);
void intel_wait_ddi_buf_idle(struct drm_i915_private *dev_priv,
			     enum port port);
void intel_ddi_init(struct intel_display *display,
		    const struct intel_bios_encoder_data *devdata);
bool intel_ddi_get_hw_state(struct intel_encoder *encoder, enum pipe *pipe);
void intel_ddi_enable_transcoder_func(struct intel_encoder *encoder,
				      const struct intel_crtc_state *crtc_state);
void intel_ddi_disable_transcoder_func(const struct intel_crtc_state *crtc_state);
void intel_ddi_enable_transcoder_clock(struct intel_encoder *encoder,
				       const struct intel_crtc_state *crtc_state);
void intel_ddi_disable_transcoder_clock(const  struct intel_crtc_state *crtc_state);
void intel_ddi_wait_for_fec_status(struct intel_encoder *encoder,
				   const struct intel_crtc_state *crtc_state,
				   bool enabled);
void intel_ddi_set_dp_msa(const struct intel_crtc_state *crtc_state,
			  const struct drm_connector_state *conn_state);
bool intel_ddi_connector_get_hw_state(struct intel_connector *intel_connector);
void intel_ddi_set_vc_payload_alloc(const struct intel_crtc_state *crtc_state,
				    bool state);
void intel_ddi_compute_min_voltage_level(struct intel_crtc_state *crtc_state);
int intel_ddi_toggle_hdcp_bits(struct intel_encoder *intel_encoder,
			       enum transcoder cpu_transcoder,
			       bool enable, u32 hdcp_mask);
void intel_ddi_sanitize_encoder_pll_mapping(struct intel_encoder *encoder);
int intel_ddi_level(struct intel_encoder *encoder,
		    const struct intel_crtc_state *crtc_state,
		    int lane);
void intel_ddi_update_active_dpll(struct intel_atomic_state *state,
				  struct intel_encoder *encoder,
				  struct intel_crtc *crtc);

#endif /* __INTEL_DDI_H__ */
