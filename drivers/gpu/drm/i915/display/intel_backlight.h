/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __INTEL_BACKLIGHT_H__
#define __INTEL_BACKLIGHT_H__

#include <linux/types.h>

struct drm_connector_state;
struct intel_atomic_state;
struct intel_connector;
struct intel_crtc_state;
struct intel_encoder;
struct intel_panel;
enum pipe;

void intel_backlight_init_funcs(struct intel_panel *panel);
int intel_backlight_setup(struct intel_connector *connector, enum pipe pipe);
void intel_backlight_destroy(struct intel_panel *panel);

void intel_backlight_enable(const struct intel_crtc_state *crtc_state,
			    const struct drm_connector_state *conn_state);
void intel_backlight_update(struct intel_atomic_state *state,
			    struct intel_encoder *encoder,
			    const struct intel_crtc_state *crtc_state,
			    const struct drm_connector_state *conn_state);
void intel_backlight_disable(const struct drm_connector_state *old_conn_state);

void intel_backlight_set_acpi(const struct drm_connector_state *conn_state,
			      u32 level, u32 max);
void intel_backlight_set_pwm_level(const struct drm_connector_state *conn_state,
				   u32 level);
u32 intel_backlight_invert_pwm_level(struct intel_connector *connector, u32 level);
u32 intel_backlight_level_to_pwm(struct intel_connector *connector, u32 level);
u32 intel_backlight_level_from_pwm(struct intel_connector *connector, u32 val);

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
