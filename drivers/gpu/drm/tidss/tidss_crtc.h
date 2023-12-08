/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Texas Instruments Incorporated - https://www.ti.com/
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 */

#ifndef __TIDSS_CRTC_H__
#define __TIDSS_CRTC_H__

#include <linux/completion.h>
#include <linux/wait.h>

#include <drm/drm_crtc.h>

#define to_tidss_crtc(c) container_of((c), struct tidss_crtc, crtc)

struct tidss_device;

struct tidss_crtc {
	struct drm_crtc crtc;

	u32 hw_videoport;

	struct drm_pending_vblank_event *event;

	struct completion framedone_completion;
};

#define to_tidss_crtc_state(x) container_of(x, struct tidss_crtc_state, base)

struct tidss_crtc_state {
	/* Must be first. */
	struct drm_crtc_state base;

	bool plane_pos_changed;

	u32 bus_format;
	u32 bus_flags;
};

void tidss_crtc_vblank_irq(struct drm_crtc *crtc);
void tidss_crtc_framedone_irq(struct drm_crtc *crtc);
void tidss_crtc_error_irq(struct drm_crtc *crtc, u64 irqstatus);

struct tidss_crtc *tidss_crtc_create(struct tidss_device *tidss,
				     u32 hw_videoport,
				     struct drm_plane *primary);
#endif
