/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_DP_H__
#define __INTEL_DP_H__

#include <linux/types.h>

enum intel_output_format;
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
	int min_rate, max_rate;
	int min_lane_count, max_lane_count;
	int min_bpp, max_bpp;
};

void intel_edp_fixup_vbt_bpp(struct intel_encoder *encoder, int pipe_bpp);
void intel_dp_adjust_compliance_config(struct intel_dp *intel_dp,
				       struct intel_crtc_state *pipe_config,
				       struct link_config_limits *limits);
bool intel_dp_limited_color_range(const struct intel_crtc_state *crtc_state,
				  const struct drm_connector_state *conn_state);
int intel_dp_min_bpp(enum intel_output_format output_format);
bool intel_dp_init_connector(struct intel_digital_port *dig_port,
			     struct intel_connector *intel_connector);
void intel_dp_set_link_params(struct intel_dp *intel_dp,
			      int link_rate, int lane_count);
int intel_dp_get_link_train_fallback_values(struct intel_dp *intel_dp,
					    int link_rate, u8 lane_count);
int intel_dp_get_active_pipes(struct intel_dp *intel_dp,
			      struct drm_modeset_acquire_ctx *ctx,
			      u8 *pipe_mask);
int intel_dp_retrain_link(struct intel_encoder *encoder,
			  struct drm_modeset_acquire_ctx *ctx);
void intel_dp_set_power(struct intel_dp *intel_dp, u8 mode);
void intel_dp_configure_protocol_converter(struct intel_dp *intel_dp,
					   const struct intel_crtc_state *crtc_state);
void intel_dp_sink_set_decompression_state(struct intel_dp *intel_dp,
					   const struct intel_crtc_state *crtc_state,
					   bool enable);
void intel_dp_encoder_suspend(struct intel_encoder *intel_encoder);
void intel_dp_encoder_shutdown(struct intel_encoder *intel_encoder);
void intel_dp_encoder_flush_work(struct drm_encoder *encoder);
int intel_dp_compute_config(struct intel_encoder *encoder,
			    struct intel_crtc_state *pipe_config,
			    struct drm_connector_state *conn_state);
int intel_dp_dsc_compute_config(struct intel_dp *intel_dp,
				struct intel_crtc_state *pipe_config,
				struct drm_connector_state *conn_state,
				struct link_config_limits *limits,
				int timeslots,
				bool recompute_pipe_bpp);
bool intel_dp_has_hdmi_sink(struct intel_dp *intel_dp);
bool intel_dp_is_edp(struct intel_dp *intel_dp);
bool intel_dp_is_uhbr(const struct intel_crtc_state *crtc_state);
bool intel_dp_is_port_edp(struct drm_i915_private *dev_priv, enum port port);
enum irqreturn intel_dp_hpd_pulse(struct intel_digital_port *dig_port,
				  bool long_hpd);
void intel_edp_backlight_on(const struct intel_crtc_state *crtc_state,
			    const struct drm_connector_state *conn_state);
void intel_edp_backlight_off(const struct drm_connector_state *conn_state);
void intel_edp_fixup_vbt_bpp(struct intel_encoder *encoder, int pipe_bpp);
void intel_dp_mst_suspend(struct drm_i915_private *dev_priv);
void intel_dp_mst_resume(struct drm_i915_private *dev_priv);
int intel_dp_max_link_rate(struct intel_dp *intel_dp);
int intel_dp_max_lane_count(struct intel_dp *intel_dp);
int intel_dp_rate_select(struct intel_dp *intel_dp, int rate);

void intel_dp_compute_rate(struct intel_dp *intel_dp, int port_clock,
			   u8 *link_bw, u8 *rate_select);
bool intel_dp_source_supports_tps3(struct drm_i915_private *i915);
bool intel_dp_source_supports_tps4(struct drm_i915_private *i915);

bool intel_dp_get_colorimetry_status(struct intel_dp *intel_dp);
int intel_dp_link_required(int pixel_clock, int bpp);
int intel_dp_max_data_rate(int max_link_rate, int max_lanes);
bool intel_dp_can_bigjoiner(struct intel_dp *intel_dp);
bool intel_dp_needs_vsc_sdp(const struct intel_crtc_state *crtc_state,
			    const struct drm_connector_state *conn_state);
void intel_dp_compute_psr_vsc_sdp(struct intel_dp *intel_dp,
				  const struct intel_crtc_state *crtc_state,
				  const struct drm_connector_state *conn_state,
				  struct drm_dp_vsc_sdp *vsc);
void intel_write_dp_vsc_sdp(struct intel_encoder *encoder,
			    const struct intel_crtc_state *crtc_state,
			    const struct drm_dp_vsc_sdp *vsc);
void intel_dp_set_infoframes(struct intel_encoder *encoder, bool enable,
			     const struct intel_crtc_state *crtc_state,
			     const struct drm_connector_state *conn_state);
void intel_read_dp_sdp(struct intel_encoder *encoder,
		       struct intel_crtc_state *crtc_state,
		       unsigned int type);
bool intel_digital_port_connected(struct intel_encoder *encoder);
int intel_dp_dsc_compute_bpp(struct intel_dp *intel_dp, u8 dsc_max_bpc);
u16 intel_dp_dsc_get_output_bpp(struct drm_i915_private *i915,
				u32 link_clock, u32 lane_count,
				u32 mode_clock, u32 mode_hdisplay,
				bool bigjoiner,
				u32 pipe_bpp,
				u32 timeslots);
u8 intel_dp_dsc_get_slice_count(struct intel_dp *intel_dp,
				int mode_clock, int mode_hdisplay,
				bool bigjoiner);
bool intel_dp_need_bigjoiner(struct intel_dp *intel_dp,
			     int hdisplay, int clock);

static inline unsigned int intel_dp_unused_lane_mask(int lane_count)
{
	return ~((1 << lane_count) - 1) & 0xf;
}

u32 intel_dp_mode_to_fec_clock(u32 mode_clock);
u32 intel_dp_dsc_nearest_valid_bpp(struct drm_i915_private *i915, u32 bpp, u32 pipe_bpp);

void intel_ddi_update_pipe(struct intel_atomic_state *state,
			   struct intel_encoder *encoder,
			   const struct intel_crtc_state *crtc_state,
			   const struct drm_connector_state *conn_state);

bool intel_dp_initial_fastset_check(struct intel_encoder *encoder,
				    struct intel_crtc_state *crtc_state);
void intel_dp_sync_state(struct intel_encoder *encoder,
			 const struct intel_crtc_state *crtc_state);

void intel_dp_check_frl_training(struct intel_dp *intel_dp);
void intel_dp_pcon_dsc_configure(struct intel_dp *intel_dp,
				 const struct intel_crtc_state *crtc_state);
void intel_dp_phy_test(struct intel_encoder *encoder);

void intel_dp_wait_source_oui(struct intel_dp *intel_dp);

#endif /* __INTEL_DP_H__ */
