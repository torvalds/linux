/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_CONNECTOR_H__
#define __INTEL_CONNECTOR_H__

#include <linux/types.h>

struct drm_connector;
struct drm_edid;
struct i2c_adapter;
struct intel_connector;
struct intel_encoder;

int intel_connector_init(struct intel_connector *connector);
struct intel_connector *intel_connector_alloc(void);
void intel_connector_free(struct intel_connector *connector);
void intel_connector_destroy(struct drm_connector *connector);
int intel_connector_register(struct drm_connector *connector);
void intel_connector_unregister(struct drm_connector *connector);
void intel_connector_attach_encoder(struct intel_connector *connector,
				    struct intel_encoder *encoder);
bool intel_connector_get_hw_state(struct intel_connector *connector);
enum pipe intel_connector_get_pipe(struct intel_connector *connector);
int intel_connector_update_modes(struct drm_connector *connector,
				 const struct drm_edid *drm_edid);
int intel_ddc_get_modes(struct drm_connector *c, struct i2c_adapter *adapter);
void intel_attach_force_audio_property(struct drm_connector *connector);
void intel_attach_broadcast_rgb_property(struct drm_connector *connector);
void intel_attach_aspect_ratio_property(struct drm_connector *connector);
void intel_attach_hdmi_colorspace_property(struct drm_connector *connector);
void intel_attach_dp_colorspace_property(struct drm_connector *connector);
void intel_attach_scaling_mode_property(struct drm_connector *connector);

#endif /* __INTEL_CONNECTOR_H__ */
