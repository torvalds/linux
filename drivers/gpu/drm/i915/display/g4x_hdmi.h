/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef _G4X_HDMI_H_
#define _G4X_HDMI_H_

#include <linux/types.h>

#include "intel_display_reg_defs.h"

enum port;
struct drm_atomic_commit;
struct drm_connector;
struct intel_display;

#ifdef I915
bool g4x_hdmi_init(struct intel_display *display,
		   intel_reg_t hdmi_reg, enum port port);
int g4x_hdmi_connector_atomic_check(struct drm_connector *connector,
				    struct drm_atomic_commit *state);
#else
static inline bool g4x_hdmi_init(struct intel_display *display,
				 intel_reg_t hdmi_reg, int port)
{
	return false;
}
static inline int g4x_hdmi_connector_atomic_check(struct drm_connector *connector,
						  struct drm_atomic_commit *state)
{
	return 0;
}
#endif

#endif
