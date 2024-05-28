/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * RZ/G2L Display Unit CRTCs
 *
 * Copyright (C) 2023 Renesas Electronics Corporation
 *
 * Based on rcar_du_crtc.h
 */

#ifndef __RZG2L_DU_CRTC_H__
#define __RZG2L_DU_CRTC_H__

#include <linux/container_of.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/wait.h>

#include <drm/drm_crtc.h>
#include <drm/drm_writeback.h>

#include <media/vsp1.h>

struct clk;
struct reset_control;
struct rzg2l_du_vsp;
struct rzg2l_du_format_info;

/**
 * struct rzg2l_du_crtc - the CRTC, representing a DU superposition processor
 * @crtc: base DRM CRTC
 * @dev: the DU device
 * @initialized: whether the CRTC has been initialized and clocks enabled
 * @vblank_enable: whether vblank events are enabled on this CRTC
 * @event: event to post when the pending page flip completes
 * @flip_wait: wait queue used to signal page flip completion
 * @vsp: VSP feeding video to this CRTC
 * @vsp_pipe: index of the VSP pipeline feeding video to this CRTC
 * @rstc: reset controller
 * @rzg2l_clocks: the bus, main and video clock
 */
struct rzg2l_du_crtc {
	struct drm_crtc crtc;

	struct rzg2l_du_device *dev;
	bool initialized;

	bool vblank_enable;
	struct drm_pending_vblank_event *event;
	wait_queue_head_t flip_wait;

	struct rzg2l_du_vsp *vsp;
	unsigned int vsp_pipe;

	const char *const *sources;
	unsigned int sources_count;

	struct reset_control *rstc;
	struct {
		struct clk *aclk;
		struct clk *pclk;
		struct clk *dclk;
	} rzg2l_clocks;
};

static inline struct rzg2l_du_crtc *to_rzg2l_crtc(struct drm_crtc *c)
{
	return container_of(c, struct rzg2l_du_crtc, crtc);
}

/**
 * struct rzg2l_du_crtc_state - Driver-specific CRTC state
 * @state: base DRM CRTC state
 * @outputs: bitmask of the outputs (enum rzg2l_du_output) driven by this CRTC
 */
struct rzg2l_du_crtc_state {
	struct drm_crtc_state state;
	unsigned int outputs;
};

static inline struct rzg2l_du_crtc_state *to_rzg2l_crtc_state(struct drm_crtc_state *s)
{
	return container_of(s, struct rzg2l_du_crtc_state, state);
}

int rzg2l_du_crtc_create(struct rzg2l_du_device *rcdu);

void rzg2l_du_crtc_finish_page_flip(struct rzg2l_du_crtc *rcrtc);

#endif /* __RZG2L_DU_CRTC_H__ */
