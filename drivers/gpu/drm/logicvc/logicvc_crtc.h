/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2019-2022 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#ifndef _LOGICVC_CRTC_H_
#define _LOGICVC_CRTC_H_

struct drm_pending_vblank_event;
struct logicvc_drm;

struct logicvc_crtc {
	struct drm_crtc drm_crtc;
	struct drm_pending_vblank_event *event;
};

void logicvc_crtc_vblank_handler(struct logicvc_drm *logicvc);
int logicvc_crtc_init(struct logicvc_drm *logicvc);

#endif
