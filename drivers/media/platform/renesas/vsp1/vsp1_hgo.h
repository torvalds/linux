/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * vsp1_hgo.h  --  R-Car VSP1 Histogram Generator 1D
 *
 * Copyright (C) 2016 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */
#ifndef __VSP1_HGO_H__
#define __VSP1_HGO_H__

#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#include "vsp1_histo.h"

struct vsp1_device;

struct vsp1_hgo {
	struct vsp1_histogram histo;

	struct {
		struct v4l2_ctrl_handler handler;
		struct v4l2_ctrl *max_rgb;
		struct v4l2_ctrl *num_bins;
	} ctrls;

	bool max_rgb;
	unsigned int num_bins;
};

static inline struct vsp1_hgo *to_hgo(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct vsp1_hgo, histo.entity.subdev);
}

struct vsp1_hgo *vsp1_hgo_create(struct vsp1_device *vsp1);
void vsp1_hgo_frame_end(struct vsp1_entity *hgo);

#endif /* __VSP1_HGO_H__ */
