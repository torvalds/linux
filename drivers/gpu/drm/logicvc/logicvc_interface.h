/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2019-2022 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#ifndef _LOGICVC_INTERFACE_H_
#define _LOGICVC_INTERFACE_H_

#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/drm_encoder.h>
#include <drm/drm_panel.h>

struct logicvc_drm;

struct logicvc_interface {
	struct drm_encoder drm_encoder;
	struct drm_connector drm_connector;

	struct drm_panel *drm_panel;
	struct drm_bridge *drm_bridge;
};

void logicvc_interface_attach_crtc(struct logicvc_drm *logicvc);
int logicvc_interface_init(struct logicvc_drm *logicvc);

#endif
