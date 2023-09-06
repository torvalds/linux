/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __INTEL_BACKLIGHT_H__
#define __INTEL_BACKLIGHT_H__

#include <linux/types.h>

struct drm_connector;
struct drm_connector_state;
struct intel_atomic_state;
struct intel_connector;
struct intel_crtc_state;
struct intel_encoder;
struct intel_panel;
enum pipe;

void intel_panel_init_backlight_funcs(struct intel_panel *panel);
void intel_panel_destroy_backlight(struct intel_panel *panel);
void intel_panel_set_backlight_acpi(const struct drm_connector_state *conn_state,
				    u32 level, u32 max);
int intel_panel_setup_backlight(struct drm_connector *connector,
				enum pipe pipe);
void intel_panel_enable_backlight(const struct intel_crtc_state *crtc_state,
				  const struct drm_connector_state *conn_state);
void intel_panel_update_backlight(struct intel_atomic_state *state,
				  struct intel_encoder *encoder,
				  const struct intel_crtc_state *crtc_state,
				  const struct drm_connector_state *conn_state);
void intel_panel_disable_backlight(const struct drm_connector_state *old_conn_state);
void intel_panel_set_pwm_level(const struct drm_connector_state *conn_state, u32 level);
u32 intel_panel_invert_pwm_level(struct intel_connector *connector, u32 level);
u32 intel_panel_backlight_level_to_pwm(struct intel_connector *connector, u32 level);
u32 intel_panel_backlight_level_from_pwm(struct intel_connector *connector, u32 val);

#if IS_ENABLED(CONFIG_BACKLIGHT_CLASS_DEVICE)
int intel_backlight_device_register(struct intel_connector *connector);
void intel_backlight_device_unregister(struct intel_connector *connector);
#else /* CONFIG_BACKLIGHT_CLASS_DEVICE */
static inline int intel_backlight_device_register(struct intel_connector *connector)
{
	return 0;
}
static inline void intel_backlight_device_unregister(struct intel_connector *connector)
{
}
#endif /* CONFIG_BACKLIGHT_CLASS_DEVICE */

#endif /* __INTEL_BACKLIGHT_H__ */
