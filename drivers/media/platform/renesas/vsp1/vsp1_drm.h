/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * vsp1_drm.h  --  R-Car VSP1 DRM/KMS Interface
 *
 * Copyright (C) 2015 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */
#ifndef __VSP1_DRM_H__
#define __VSP1_DRM_H__

#include <linux/mutex.h>
#include <linux/videodev2.h>
#include <linux/wait.h>

#include <media/vsp1.h>

#include "vsp1_pipe.h"

/**
 * struct vsp1_drm_pipeline - State for the API exposed to the DRM driver
 * @pipe: the VSP1 pipeline used for display
 * @partition: the pre-calculated partition used by the pipeline
 * @width: output display width
 * @height: output display height
 * @force_brx_release: when set, release the BRx during the next reconfiguration
 * @wait_queue: wait queue to wait for BRx release completion
 * @uif: UIF entity if available for the pipeline
 * @crc: CRC computation configuration
 * @du_complete: frame completion callback for the DU driver (optional)
 * @du_private: data to be passed to the du_complete callback
 */
struct vsp1_drm_pipeline {
	struct vsp1_pipeline pipe;
	struct vsp1_partition partition;

	unsigned int width;
	unsigned int height;

	bool force_brx_release;
	wait_queue_head_t wait_queue;

	struct vsp1_entity *uif;
	struct vsp1_du_crc_config crc;

	/* Frame synchronisation */
	void (*du_complete)(void *data, unsigned int status, u32 crc);
	void *du_private;
};

/**
 * struct vsp1_drm - State for the API exposed to the DRM driver
 * @pipe: the VSP1 DRM pipeline used for display
 * @lock: protects the BRU and BRS allocation
 * @inputs: source crop rectangle, destination compose rectangle and z-order
 *	position for every input (indexed by RPF index)
 */
struct vsp1_drm {
	struct vsp1_drm_pipeline pipe[VSP1_MAX_LIF];
	struct mutex lock;

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
