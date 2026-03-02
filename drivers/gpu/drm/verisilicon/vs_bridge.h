/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Icenowy Zheng <uwu@icenowy.me>
 */

#ifndef _VS_BRIDGE_H_
#define _VS_BRIDGE_H_

#include <linux/types.h>

#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/drm_encoder.h>

struct vs_crtc;

enum vs_bridge_output_interface {
	VSDC_OUTPUT_INTERFACE_DPI = 0,
	VSDC_OUTPUT_INTERFACE_DP = 1
};

struct vs_bridge {
	struct drm_bridge base;
	struct drm_encoder *enc;
	struct drm_connector *conn;

	struct vs_crtc *crtc;
	struct drm_bridge *next_bridge;
	enum vs_bridge_output_interface intf;
};

static inline struct vs_bridge *drm_bridge_to_vs_bridge(struct drm_bridge *bridge)
{
	return container_of(bridge, struct vs_bridge, base);
}

struct vs_bridge *vs_bridge_init(struct drm_device *drm_dev,
				 struct vs_crtc *crtc);
#endif /* _VS_BRIDGE_H_ */
