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
 * vsp1_drm_pipeline - State for the API exposed to the DRM driver
 * @pipe: the VSP1 pipeline used for display
 * @width: output display width
 * @height: output display height
 * @du_complete: frame completion callback for the DU driver (optional)
 * @du_private: data to be passed to the du_complete callback
 */
struct vsp1_drm_pipeline {
	struct vsp1_pipeline pipe;

	unsigned int width;
	unsigned int height;

	/* Frame synchronisation */
	void (*du_complete)(void *, bool);
	void *du_private;
};

/**
 * vsp1_drm - State for the API exposed to the DRM driver
 * @pipe: the VSP1 DRM pipeline used for display
 * @inputs: source crop rectangle, destination compose rectangle and z-order
 *	position for every input (indexed by RPF index)
 */
struct vsp1_drm {
	struct vsp1_drm_pipeline pipe[VSP1_MAX_LIF];

	struct {
		struct v4l2_rect crop;
		struct v4l2_rect compose;
		unsigned int zpos;
	} inputs[VSP1_MAX_RPF];
};

static inline struct vsp1_drm_pipeline *
to_vsp1_drm_pipeline(struct vsp1_pipeline *pipe)
{
	return container_of(pipe, struct vsp1_drm_pipeline, pipe);
}

int vsp1_drm_init(struct vsp1_device *vsp1);
void vsp1_drm_cleanup(struct vsp1_device *vsp1);

#endif /* __VSP1_DRM_H__ */
