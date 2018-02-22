/*
 * vsp1_histo.h  --  R-Car VSP1 Histogram API
 *
 * Copyright (C) 2016 Renesas Electronics Corporation
 * Copyright (C) 2016 Laurent Pinchart
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __VSP1_HISTO_H__
#define __VSP1_HISTO_H__

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>

#include <media/media-entity.h>
#include <media/v4l2-dev.h>
#include <media/videobuf2-v4l2.h>

#include "vsp1_entity.h"

struct vsp1_device;

#define HISTO_PAD_SINK				0
#define HISTO_PAD_SOURCE			1

struct vsp1_histogram_buffer {
	struct vb2_v4l2_buffer buf;
	struct list_head queue;
	void *addr;
};

struct vsp1_histogram {
	struct vsp1_entity entity;
	struct video_device video;
	struct media_pad pad;

	const u32 *formats;
	unsigned int num_formats;
	size_t data_size;
	u32 meta_format;

	struct mutex lock;
	struct vb2_queue queue;

	spinlock_t irqlock;
	struct list_head irqqueue;

	wait_queue_head_t wait_queue;
	bool readout;
};

static inline struct vsp1_histogram *vdev_to_histo(struct video_device *vdev)
{
	return container_of(vdev, struct vsp1_histogram, video);
}

static inline struct vsp1_histogram *subdev_to_histo(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct vsp1_histogram, entity.subdev);
}

int vsp1_histogram_init(struct vsp1_device *vsp1, struct vsp1_histogram *histo,
			enum vsp1_entity_type type, const char *name,
			const struct vsp1_entity_operations *ops,
			const unsigned int *formats, unsigned int num_formats,
			size_t data_size, u32 meta_format);
void vsp1_histogram_destroy(struct vsp1_entity *entity);

struct vsp1_histogram_buffer *
vsp1_histogram_buffer_get(struct vsp1_histogram *histo);
void vsp1_histogram_buffer_complete(struct vsp1_histogram *histo,
				    struct vsp1_histogram_buffer *buf,
				    size_t size);

#endif /* __VSP1_HISTO_H__ */
