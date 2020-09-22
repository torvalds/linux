/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_DP_H__
#define __INTEL_DP_H__

#include <linux/types.h>

#include "i915_reg.h"

enum pipe;
enum port;
struct drm_connector_state;
struct drm_encoder;
struct drm_i915_private;
struct drm_modeset_acquire_ctx;
struct drm_dp_vsc_sdp;
struct intel_atomic_state;
struct intel_connector;
struct intel_crtc_state;
struct intel_digital_port;
struct intel_dp;
struct intel_encoder;

struct link_config_limits {
	int min_clock, max_clock;
	int min_lane_count, max_lane_count;
	int min_bpp, max_bpp;
};

void intel_dp_adjust_compliance_config(struct intel_dp *intel_dp,
				       struct intel_crtc_state *pipe_config,
				       struct link_config_limits *limits);
bool intel_dp_limited_color_range(const struct intel_crtc_state *crtc_state,
				  const struct drm_connector_state *conn_state);
int intel_dp_min_bpp(const struct intel_crtc_state *crtc_state);
bool intel_dp_port_enabled(struct drm_i915_private *dev_priv,
			   i915_reg_t dp_reg, enum port port,
			   enum pipe *pipe);
bool intel_dp_init(struct drm_i915_private *dev_priv, i915_reg_t output_reg,
		   enum port port);
bool intel_dp_init_connector(struct intel_digital_port *dig_port,
			     struct intel_connector *intel_connector);
void intel_dp_set_link_params(struct intel_dp *intel_dp,
			      int link_rate, u8 lane_count,
			      bool link_mst);
int intel_dp_get_link_train_fallback_values(struct intel_dp *intel_dp,
					    int link_rate, u8 lane_count);
int intel_dp_retrain_link(struct intel_encoder *encoder,
			  struct drm_modeset_acquire_ctx *ctx);
void intel_dp_sink_dpms(struct intel_dp *intel_dp, int mode);
void intel_dp_configure_protocol_converter(struct intel_dp *intel_dp);
void intel_dp_sink_set_decompression_state(struct intel_dp *intel_dp,
					   const struct intel_crtc_state *crtc_state,
					   bool enable);
void intel_dp_encoder_reset(struct drm_encoder *encoder);
void intel_dp_encoder_suspend(struct intel_encoder *intel_encoder);
void intel_dp_encoder_flush_work(struct drm_encoder *encoder);
int intel_dp_compute_config(struct intel_encoder *encoder,
			    struct intel_crtc_state *pipe_config,
			    struct drm_connector_state *conn_state);
bool intel_dp_is_edp(struct intel_dp *intel_dp);
bool intel_dp_is_port_edp(struct drm_i915_private *dev_priv, enum port port);
enum irqreturn intel_dp_hpd_pulse(struct intel_digital_port *dig_port,
				  bool long_hpd);
void intel_edp_backlight_on(const struct intel_crtc_state *crtc_state,
			    const struct drm_connector_state *conn_state);
void intel_edp_backlight_off(const struct drm_connector_state *conn_state);
void intel_edp_panel_vdd_on(struct intel_dp *intel_dp);
void intel_edp_panel_on(struct intel_dp *intel_dp);
void intel_edp_panel_off(struct intel_dp *intel_dp);
void intel_dp_mst_suspend(struct drm_i915_private *dev_priv);
void intel_dp_mst_resume(struct drm_i915_private *dev_priv);
int intel_dp_max_link_rate(struct intel_dp *intel_dp);
int intel_dp_max_lane_count(struct intel_dp *intel_dp);
int intel_dp_rate_select(struct intel_dp *intel_dp, int rate);
void intel_power_sequencer_reset(struct drm_i915_private *dev_priv);
u32 intel_dp_pack_aux(const u8 *src, int src_bytes);

void intel_edp_drrs_enable(struct intel_dp *intel_dp,
			   const struct intel_crtc_state *crtc_state);
void intel_edp_drrs_disable(struct intel_dp *intel_dp,
			    const struct intel_crtc_state *crtc_state);
void intel_edp_drrs_update(struct intel_dp *intel_dp,
			   const struct intel_crtc_state *crtc_state);
void intel_edp_drrs_invalidate(struct drm_i915_private *dev_priv,
			       unsigned int frontbuffer_bits);
void intel_edp_drrs_flush(struct drm_i915_private *dev_priv,
			  unsigned int frontbuffer_bits);

void
intel_dp_program_link_training_pattern(struct intel_dp *intel_dp,
				       u8 dp_train_pat);
void
intel_dp_set_signal_levels(struct intel_dp *intel_dp);
void intel_dp_set_idle_link_train(struct intel_dp *intel_dp);
void intel_dp_compute_rate(struct intel_dp *intel_dp, int port_clock,
			   u8 *link_bw, u8 *rate_select);
bool intel_dp_source_supports_hbr2(struct intel_dp *intel_dp);
bool intel_dp_source_supports_hbr3(struct intel_dp *intel_dp);
bool
intel_dp_get_link_status(struct intel_dp *intel_dp, u8 *link_status);

bool intel_dp_get_colorimetry_status(struct intel_dp *intel_dp);
int intel_dp_link_required(int pixel_clock, int bpp);
int intel_dp_max_data_rate(int max_link_clock, int max_lanes);
bool intel_dp_needs_vsc_sdp(const struct intel_crtc_state *crtc_state,
			    const struct drm_connector_state *conn_state);
void intel_dp_compute_psr_vsc_sdp(struct intel_dp *intel_dp,
				  const struct intel_crtc_state *crtc_state,
				  const struct drm_connector_state *conn_state,
				  struct drm_dp_vsc_sdp *vsc);
void intel_write_dp_vsc_sdp(struct intel_encoder *encoder,
			    const struct intel_crtc_state *crtc_state,
			    struct drm_dp_vsc_sdp *vsc);
void intel_dp_set_infoframes(struct intel_encoder *encoder, bool enable,
			     const struct intel_crtc_state *crtc_state,
			     const struct drm_connector_state *conn_state);
void intel_read_dp_sdp(struct intel_encoder *encoder,
		       struct intel_crtc_state *crtc_state,
		       unsigned int type);
bool intel_digital_port_connected(struct intel_encoder *encoder);
void intel_dp_process_phy_request(struct intel_dp *intel_dp);

static inline unsigned int intel_dp_unused_lane_mask(int lane_count)
{
	return ~((1 << lane_count) - 1) & 0xf;
}

u32 intel_dp_mode_to_fec_clock(u32 mode_clock);

void intel_ddi_update_pipe(struct intel_atomic_state *state,
			   struct intel_encoder *encoder,
			   const struct intel_crtc_state *crtc_state,
			   const struct drm_connector_state *conn_state);

int intel_dp_init_hdcp(struct intel_digital_port *dig_port,
		       struct intel_connector *intel_connector);

#endif /* __INTEL_DP_H__ */
