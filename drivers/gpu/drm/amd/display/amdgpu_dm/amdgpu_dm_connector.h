/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#ifndef __AMDGPU_DM_CONNECTOR_H__
#define __AMDGPU_DM_CONNECTOR_H__

struct amdgpu_device;
struct amdgpu_dm_connector;
struct amdgpu_display_manager;
struct amdgpu_encoder;
struct amdgpu_i2c_adapter;
struct dc_crtc_timing;
struct dc_link;
enum signal_type;
struct dc_state;
struct dc_stream_state;
struct ddc_service;
struct dm_connector_state;
struct drm_atomic_commit;
struct drm_device;
struct drm_encoder_helper_funcs;
struct drm_connector;
struct drm_connector_state;
struct drm_crtc;
struct drm_device;
struct drm_display_mode;
struct drm_edid;
struct drm_property;

void amdgpu_dm_connector_funcs_reset(struct drm_connector *connector);

struct drm_connector_state *
amdgpu_dm_connector_atomic_duplicate_state(struct drm_connector *connector);

int amdgpu_dm_connector_atomic_set_property(struct drm_connector *connector,
					    struct drm_connector_state *connector_state,
					    struct drm_property *property,
					    uint64_t val);

int amdgpu_dm_connector_atomic_get_property(struct drm_connector *connector,
					    const struct drm_connector_state *state,
					    struct drm_property *property,
					    uint64_t *val);

void amdgpu_dm_connector_init_helper(struct amdgpu_display_manager *dm,
				     struct amdgpu_dm_connector *aconnector,
				     int connector_type,
				     struct dc_link *link,
				     int link_index);

enum drm_mode_status amdgpu_dm_connector_mode_valid(struct drm_connector *connector,
						    const struct drm_display_mode *mode);

void dm_restore_drm_connector_state(struct drm_device *dev,
				    struct drm_connector *connector);

void amdgpu_dm_update_freesync_caps(struct drm_connector *connector,
				    const struct drm_edid *drm_edid,
				    bool do_mccs);

void amdgpu_dm_update_connector_after_detect(
		struct amdgpu_dm_connector *aconnector);

void amdgpu_dm_hdmi_cec_set_edid(struct amdgpu_dm_connector *aconnector);
int amdgpu_dm_initialize_hdmi_connector(struct amdgpu_dm_connector *aconnector);

struct drm_connector *
amdgpu_dm_find_first_crtc_matching_connector(struct drm_atomic_commit *state,
					     struct drm_crtc *crtc);

int amdgpu_dm_convert_dc_color_depth_into_bpc(enum dc_color_depth display_color_depth);

struct dc_stream_state *
amdgpu_dm_create_validate_stream_for_sink(struct drm_connector *connector,
				const struct drm_display_mode *drm_mode,
				const struct dm_connector_state *dm_state,
				const struct dc_stream_state *old_stream);

int amdgpu_dm_connector_init(struct amdgpu_display_manager *dm,
			     struct amdgpu_dm_connector *amdgpu_dm_connector,
			     u32 link_index,
			     struct amdgpu_encoder *amdgpu_encoder);

void amdgpu_dm_s3_handle_hdmi_cec(struct drm_device *ddev, bool suspend);

int amdgpu_dm_detect_mst_link_for_all_connectors(struct drm_device *dev);

void amdgpu_set_panel_orientation(struct drm_connector *connector);

enum dc_color_depth
amdgpu_dm_convert_color_depth_from_display_info(const struct drm_connector *connector,
				      bool is_y420, int requested_bpc);

void amdgpu_dm_update_stream_scaling_settings(struct drm_device *dev,
				    const struct drm_display_mode *mode,
				    const struct dm_connector_state *dm_state,
				    struct dc_stream_state *stream);

bool amdgpu_dm_is_freesync_video_mode(const struct drm_display_mode *mode,
			    struct amdgpu_dm_connector *aconnector);

int amdgpu_dm_fill_hdr_info_packet(const struct drm_connector_state *state,
			 struct dc_info_packet *out);

enum dc_color_space
amdgpu_dm_get_output_color_space(const struct dc_crtc_timing *dc_crtc_timing,
		       const struct drm_connector_state *connector_state);

struct drm_display_mode *
amdgpu_dm_get_highest_refresh_rate_mode(struct amdgpu_dm_connector *aconnector,
			      bool use_probed_modes);

struct amdgpu_i2c_adapter *
amdgpu_dm_create_i2c(struct ddc_service *ddc_service, bool oem);

#define DDC_MANUFACTURERNAME_SAMSUNG 0x2D4C

/* Encoder functions */
extern const struct drm_encoder_helper_funcs amdgpu_dm_encoder_helper_funcs;
int amdgpu_dm_get_encoder_crtc_mask(struct amdgpu_device *adev);
int amdgpu_dm_encoder_init(struct drm_device *dev,
			   struct amdgpu_encoder *aencoder,
			   uint32_t link_index);

#if IS_ENABLED(CONFIG_DRM_AMD_DC_KUNIT_TEST)
enum drm_mode_subconnector get_subconnector_type(struct dc_link *link);
void update_subconnector_property(struct amdgpu_dm_connector *aconnector);
void amdgpu_dm_fbc_init(struct drm_connector *connector);
void amdgpu_dm_set_panel_type(struct amdgpu_dm_connector *aconnector);
void amdgpu_dm_update_cacp_caps(struct amdgpu_dm_connector *aconnector);
void fill_stream_properties_from_drm_display_mode(
	struct dc_stream_state *stream,
	const struct drm_display_mode *mode_in,
	const struct drm_connector *connector,
	const struct drm_connector_state *connector_state,
	const struct dc_stream_state *old_stream,
	int requested_bpc);
enum display_content_type
get_output_content_type(const struct drm_connector_state *connector_state);
bool adjust_colour_depth_from_display_info(struct dc_crtc_timing *timing_out,
					   const struct drm_display_info *info);

int to_drm_connector_type(enum signal_type st, uint32_t connector_id);
bool is_duplicate_mode(struct amdgpu_dm_connector *aconnector, struct drm_display_mode *mode);
enum dc_aspect_ratio get_aspect_ratio(const struct drm_display_mode *mode_in);
void copy_crtc_timing_for_drm_display_mode(const struct drm_display_mode *src_mode,
					   struct drm_display_mode *dst_mode);
void decide_crtc_timing_for_drm_display_mode(struct drm_display_mode *drm_mode,
					     const struct drm_display_mode *native_mode,
					     bool scale_enabled);
void amdgpu_dm_set_panel_type(struct amdgpu_dm_connector *aconnector);
void amdgpu_dm_update_cacp_caps(struct amdgpu_dm_connector *aconnector);
#endif
#endif /* __AMDGPU_DM_CONNECTOR_H__ */
