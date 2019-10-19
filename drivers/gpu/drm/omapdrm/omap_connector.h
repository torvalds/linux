/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * omap_connector.h -- OMAP DRM Connector
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Rob Clark <rob@ti.com>
 */

#ifndef __OMAPDRM_CONNECTOR_H__
#define __OMAPDRM_CONNECTOR_H__

#include <linux/types.h>

enum drm_mode_status;

struct drm_connector;
struct drm_device;
struct drm_encoder;
struct omap_dss_device;

struct drm_connector *omap_connector_init(struct drm_device *dev,
					  struct omap_dss_device *output,
					  struct drm_encoder *encoder);
bool omap_connector_get_hdmi_mode(struct drm_connector *connector);
void omap_connector_enable_hpd(struct drm_connector *connector);
void omap_connector_disable_hpd(struct drm_connector *connector);
enum drm_mode_status omap_connector_mode_fixup(struct omap_dss_device *dssdev,
					const struct drm_display_mode *mode,
					struct drm_display_mode *adjusted_mode);

#endif /* __OMAPDRM_CONNECTOR_H__ */
