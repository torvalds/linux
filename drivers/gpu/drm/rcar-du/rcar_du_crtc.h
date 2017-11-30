/*
 * rcar_du_crtc.h  --  R-Car Display Unit CRTCs
 *
 * Copyright (C) 2013-2015 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __RCAR_DU_CRTC_H__
#define __RCAR_DU_CRTC_H__

#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/wait.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>

struct rcar_du_group;
struct rcar_du_vsp;

/**
 * struct rcar_du_crtc - the CRTC, representing a DU superposition processor
 * @crtc: base DRM CRTC
 * @clock: the CRTC functional clock
 * @extclock: external pixel dot clock (optional)
 * @mmio_offset: offset of the CRTC registers in the DU MMIO block
 * @index: CRTC software and hardware index
 * @initialized: whether the CRTC has been initialized and clocks enabled
 * @vblank_enable: whether vblank events are enabled on this CRTC
 * @event: event to post when the pending page flip completes
 * @flip_wait: wait queue used to signal page flip completion
 * @vblank_lock: protects vblank_wait and vblank_count
 * @vblank_wait: wait queue used to signal vertical blanking
 * @vblank_count: number of vertical blanking interrupts to wait for
 * @outputs: bitmask of the outputs (enum rcar_du_output) driven by this CRTC
 * @group: CRTC group this CRTC belongs to
 * @vsp: VSP feeding video to this CRTC
 * @vsp_pipe: index of the VSP pipeline feeding video to this CRTC
 */
struct rcar_du_crtc {
	struct drm_crtc crtc;

	struct clk *clock;
	struct clk *extclock;
	unsigned int mmio_offset;
	unsigned int index;
	bool initialized;

	bool vblank_enable;
	struct drm_pending_vblank_event *event;
	wait_queue_head_t flip_wait;

	spinlock_t vblank_lock;
	wait_queue_head_t vblank_wait;
	unsigned int vblank_count;

	unsigned int outputs;

	struct rcar_du_group *group;
	struct rcar_du_vsp *vsp;
	unsigned int vsp_pipe;
};

#define to_rcar_crtc(c)	container_of(c, struct rcar_du_crtc, crtc)

enum rcar_du_output {
	RCAR_DU_OUTPUT_DPAD0,
	RCAR_DU_OUTPUT_DPAD1,
	RCAR_DU_OUTPUT_LVDS0,
	RCAR_DU_OUTPUT_LVDS1,
	RCAR_DU_OUTPUT_HDMI0,
	RCAR_DU_OUTPUT_HDMI1,
	RCAR_DU_OUTPUT_TCON,
	RCAR_DU_OUTPUT_MAX,
};

int rcar_du_crtc_create(struct rcar_du_group *rgrp, unsigned int index);
void rcar_du_crtc_suspend(struct rcar_du_crtc *rcrtc);
void rcar_du_crtc_resume(struct rcar_du_crtc *rcrtc);

void rcar_du_crtc_route_output(struct drm_crtc *crtc,
			       enum rcar_du_output output);
void rcar_du_crtc_finish_page_flip(struct rcar_du_crtc *rcrtc);

#endif /* __RCAR_DU_CRTC_H__ */
