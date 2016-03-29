/*
 * vsp1_rwpf.h  --  R-Car VSP1 Read and Write Pixel Formatters
 *
 * Copyright (C) 2013-2014 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __VSP1_RWPF_H__
#define __VSP1_RWPF_H__

#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#include "vsp1.h"
#include "vsp1_entity.h"

#define RWPF_PAD_SINK				0
#define RWPF_PAD_SOURCE				1

struct v4l2_ctrl;
struct vsp1_rwpf;
struct vsp1_video;

struct vsp1_rwpf_memory {
	unsigned int num_planes;
	dma_addr_t addr[3];
	unsigned int length[3];
};

struct vsp1_rwpf_operations {
	void (*set_memory)(struct vsp1_rwpf *rwpf,
			   struct vsp1_rwpf_memory *mem);
};

struct vsp1_rwpf {
	struct vsp1_entity entity;
	struct v4l2_ctrl_handler ctrls;
	struct v4l2_ctrl *alpha;

	struct vsp1_video *video;

	const struct vsp1_rwpf_operations *ops;

	unsigned int max_width;
	unsigned int max_height;

	struct v4l2_pix_format_mplane format;
	const struct vsp1_format_info *fmtinfo;
	struct {
		unsigned int left;
		unsigned int top;
	} location;
	struct v4l2_rect crop;

	unsigned int offsets[2];
	dma_addr_t buf_addr[3];
};

static inline struct vsp1_rwpf *to_rwpf(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct vsp1_rwpf, entity.subdev);
}

struct vsp1_rwpf *vsp1_rpf_create(struct vsp1_device *vsp1, unsigned int index);
struct vsp1_rwpf *vsp1_wpf_create(struct vsp1_device *vsp1, unsigned int index);

int vsp1_rwpf_enum_mbus_code(struct v4l2_subdev *subdev,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_mbus_code_enum *code);
int vsp1_rwpf_enum_frame_size(struct v4l2_subdev *subdev,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_frame_size_enum *fse);
int vsp1_rwpf_get_format(struct v4l2_subdev *subdev, struct v4l2_subdev_pad_config *cfg,
			 struct v4l2_subdev_format *fmt);
int vsp1_rwpf_set_format(struct v4l2_subdev *subdev, struct v4l2_subdev_pad_config *cfg,
			 struct v4l2_subdev_format *fmt);
int vsp1_rwpf_get_selection(struct v4l2_subdev *subdev,
			    struct v4l2_subdev_pad_config *cfg,
			    struct v4l2_subdev_selection *sel);
int vsp1_rwpf_set_selection(struct v4l2_subdev *subdev,
			    struct v4l2_subdev_pad_config *cfg,
			    struct v4l2_subdev_selection *sel);

#endif /* __VSP1_RWPF_H__ */
