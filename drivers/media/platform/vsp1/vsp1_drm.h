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

#include "vsp1_pipe.h"

struct vsp1_dl;

/**
 * vsp1_drm - State for the API exposed to the DRM driver
 * @dl: display list for DRM pipeline operation
 * @pipe: the VSP1 pipeline used for display
 * @num_inputs: number of active pipeline inputs at the beginning of an update
 * @update: the pipeline configuration has been updated
 */
struct vsp1_drm {
	struct vsp1_dl *dl;
	struct vsp1_pipeline pipe;
	unsigned int num_inputs;
	bool update;
};

int vsp1_drm_init(struct vsp1_device *vsp1);
void vsp1_drm_cleanup(struct vsp1_device *vsp1);
int vsp1_drm_create_links(struct vsp1_device *vsp1);

int vsp1_du_init(struct device *dev);
int vsp1_du_setup_lif(struct device *dev, unsigned int width,
		      unsigned int height);
void vsp1_du_atomic_begin(struct device *dev);
int vsp1_du_atomic_update(struct device *dev, unsigned int rpf_index,
			  u32 pixelformat, unsigned int pitch,
			  dma_addr_t mem[2], const struct v4l2_rect *src,
			  const struct v4l2_rect *dst);
void vsp1_du_atomic_flush(struct device *dev);


#endif /* __VSP1_DRM_H__ */
