/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_PANEL_H__
#define __INTEL_PANEL_H__

#include <linux/types.h>

enum drm_connector_status;
enum drrs_type;
struct drm_connector;
struct drm_connector_state;
struct drm_display_mode;
struct drm_edid;
struct intel_connector;
struct intel_crtc_state;
struct intel_display;
struct intel_encoder;

void intel_panel_init_alloc(struct intel_connector *connector);
int intel_panel_init(struct intel_connector *connector,
		     const struct drm_edid *fixed_edid);
void intel_panel_fini(struct intel_connector *connector);
enum drm_connector_status
intel_panel_detect(struct drm_connector *connector, bool force);
bool intel_panel_use_ssc(struct intel_display *display);
const struct drm_display_mode *
intel_panel_preferred_fixed_mode(struct intel_connector *connector);
const struct drm_display_mode *
intel_panel_fixed_mode(struct intel_connector *connector,
		       const struct drm_display_mode *mode);
const struct drm_display_mode *
intel_panel_downclock_mode(struct intel_connector *connector,
			   const struct drm_display_mode *adjusted_mode);
const struct drm_display_mode *
intel_panel_highest_mode(struct intel_connector *connector,
			 const struct drm_display_mode *adjusted_mode);
int intel_panel_get_modes(struct intel_connector *connector);
enum drrs_type intel_panel_drrs_type(struct intel_connector *connector);
enum drm_mode_status
intel_panel_mode_valid(struct intel_connector *connector,
		       const struct drm_display_mode *mode);
int intel_panel_compute_config(struct intel_connector *connector,
			       struct drm_display_mode *adjusted_mode);
void intel_panel_add_edid_fixed_modes(struct intel_connector *connector,
				      bool use_alt_fixed_modes);
void intel_panel_add_vbt_lfp_fixed_mode(struct intel_connector *connector);
void intel_panel_add_vbt_sdvo_fixed_mode(struct intel_connector *connector);
void intel_panel_add_encoder_fixed_mode(struct intel_connector *connector,
					struct intel_encoder *encoder);

#endif /* __INTEL_PANEL_H__ */
