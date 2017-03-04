/*
 * vsp1_drm.h  --  R-Car VSP1 DRM/KMS Interface
 *
 * Copyright (C) 2015 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __VSP1_DRM_H__
#define __VSP1_DRM_H__

#include <linux/videodev2.h>

#include "vsp1_pipe.h"

/**
 * vsp1_drm - State for the API exposed to the DRM driver
 * @pipe: the VSP1 pipeline used for display
 * @num_inputs: number of active pipeline inputs at the beginning of an update
 * @inputs: source crop rectangle, destination compose rectangle and z-order
 *	position for every input
 * @du_complete: frame completion callback for the DU driver (optional)
 * @du_private: data to be passed to the du_complete callback
 */
struct vsp1_drm {
	struct vsp1_pipeline pipe;
	unsigned int num_inputs;
	struct {
		bool enabled;
		struct v4l2_rect crop;
		struct v4l2_rect compose;
		unsigned int zpos;
	} inputs[VSP1_MAX_RPF];

	/* Frame synchronisation */
	void (*du_complete)(void *);
	void *du_private;
};

static inline struct vsp1_drm *to_vsp1_drm(struct vsp1_pipeline *pipe)
{
	return container_of(pipe, struct vsp1_drm, pipe);
}

int vsp1_drm_init(struct vsp1_device *vsp1);
void vsp1_drm_cleanup(struct vsp1_device *vsp1);
int vsp1_drm_create_links(struct vsp1_device *vsp1);

void vsp1_drm_display_start(struct vsp1_device *vsp1);

#endif /* __VSP1_DRM_H__ */
