/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * vsp1_rwpf.h  --  R-Car VSP1 Read and Write Pixel Formatters
 *
 * Copyright (C) 2013-2014 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */
#ifndef __VSP1_RWPF_H__
#define __VSP1_RWPF_H__

#include <linux/spinlock.h>

#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#include "vsp1.h"
#include "vsp1_entity.h"

#define RWPF_PAD_SINK				0
#define RWPF_PAD_SOURCE				1

struct v4l2_ctrl;
struct vsp1_dl_manager;
struct vsp1_rwpf;
struct vsp1_video;

struct vsp1_rwpf_memory {
	dma_addr_t addr[3];
};

struct vsp1_rwpf {
	struct vsp1_entity entity;
	struct v4l2_ctrl_handler ctrls;

	struct vsp1_video *video;

	unsigned int max_width;
	unsigned int max_height;

	struct v4l2_pix_format_mplane format;
	const struct vsp1_format_info *fmtinfo;
	unsigned int brx_input;

	unsigned int alpha;

	u32 mult_alpha;
	u32 outfmt;

	struct {
		spinlock_t lock;
		struct {
			struct v4l2_ctrl *vflip;
			struct v4l2_ctrl *hflip;
			struct v4l2_ctrl *rotate;
		} ctrls;
		unsigned int pending;
		unsigned int active;
		bool rotate;
	} flip;

	struct vsp1_rwpf_memory mem;
	bool writeback;

	struct vsp1_dl_manager *dlm;
};

static inline struct vsp1_rwpf *to_rwpf(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct vsp1_rwpf, entity.subdev);
}

static inline struct vsp1_rwpf *entity_to_rwpf(struct vsp1_entity *entity)
{
	return container_of(entity, struct vsp1_rwpf, entity);
}

struct vsp1_rwpf *vsp1_rpf_create(struct vsp1_device *vsp1, unsigned int index);
struct vsp1_rwpf *vsp1_wpf_create(struct vsp1_device *vsp1, unsigned int index);

void vsp1_wpf_stop(struct vsp1_rwpf *wpf);

int vsp1_rwpf_init_ctrls(struct vsp1_rwpf *rwpf, unsigned int ncontrols);

extern const struct v4l2_subdev_ops vsp1_rwpf_subdev_ops;

struct v4l2_rect *vsp1_rwpf_get_crop(struct vsp1_rwpf *rwpf,
				     struct v4l2_subdev_state *sd_state);

#endif /* __VSP1_RWPF_H__ */
