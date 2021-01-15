/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_HDMI_H__
#define __INTEL_HDMI_H__

#include <linux/hdmi.h>
#include <linux/types.h>

#include "i915_reg.h"

struct drm_connector;
struct drm_encoder;
struct drm_i915_private;
struct intel_connector;
struct intel_digital_port;
struct intel_encoder;
struct intel_crtc_state;
struct intel_hdmi;
struct drm_connector_state;
union hdmi_infoframe;
enum port;

void intel_hdmi_init(struct drm_i915_private *dev_priv, i915_reg_t hdmi_reg,
		     enum port port);
void intel_hdmi_init_connector(struct intel_digital_port *dig_port,
			       struct intel_connector *intel_connector);
struct intel_hdmi *enc_to_intel_hdmi(struct intel_encoder *encoder);
int intel_hdmi_compute_config(struct intel_encoder *encoder,
			      struct intel_crtc_state *pipe_config,
			      struct drm_connector_state *conn_state);
bool intel_hdmi_handle_sink_scrambling(struct intel_encoder *encoder,
				       struct drm_connector *connector,
				       bool high_tmds_clock_ratio,
				       bool scrambling);
void intel_dp_dual_mode_set_tmds_output(struct intel_hdmi *hdmi, bool enable);
void intel_infoframe_init(struct intel_digital_port *dig_port);
u32 intel_hdmi_infoframes_enabled(struct intel_encoder *encoder,
				  const struct intel_crtc_state *crtc_state);
u32 intel_hdmi_infoframe_enable(unsigned int type);
void intel_hdmi_read_gcp_infoframe(struct intel_encoder *encoder,
				   struct intel_crtc_state *crtc_state);
void intel_read_infoframe(struct intel_encoder *encoder,
			  const struct intel_crtc_state *crtc_state,
			  enum hdmi_infoframe_type type,
			  union hdmi_infoframe *frame);
bool intel_hdmi_limited_color_range(const struct intel_crtc_state *crtc_state,
				    const struct drm_connector_state *conn_state);
bool intel_hdmi_deep_color_possible(const struct intel_crtc_state *crtc_state, int bpc,
				    bool has_hdmi_sink, bool ycbcr420_output);
int intel_hdmi_dsc_get_bpp(int src_fractional_bpp, int slice_width,
			   int num_slices, int output_format, bool hdmi_all_bpp,
			   int hdmi_max_chunk_bytes);
int intel_hdmi_dsc_get_num_slices(const struct intel_crtc_state *crtc_state,
				  int src_max_slices, int src_max_slice_width,
				  int hdmi_max_slices, int hdmi_throughput);
int intel_hdmi_dsc_get_slice_height(int vactive);

#endif /* __INTEL_HDMI_H__ */
