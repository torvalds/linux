/*
 * Copyright (C) 2009 Texas Instruments Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef VPIF_CAPTURE_H
#define VPIF_CAPTURE_H

/* Header files */
#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-device.h>

#include "vpif.h"

/* Macros */
#define VPIF_CAPTURE_VERSION		"0.0.2"

#define VPIF_VALID_FIELD(field)		(((V4L2_FIELD_ANY == field) || \
	(V4L2_FIELD_NONE == field)) || \
	(((V4L2_FIELD_INTERLACED == field) || \
	(V4L2_FIELD_SEQ_TB == field)) || \
	(V4L2_FIELD_SEQ_BT == field)))

#define VPIF_CAPTURE_MAX_DEVICES	2
#define VPIF_VIDEO_INDEX		0
#define VPIF_NUMBER_OF_OBJECTS		1

/* Enumerated data type to give id to each device per channel */
enum vpif_channel_id {
	VPIF_CHANNEL0_VIDEO = 0,
	VPIF_CHANNEL1_VIDEO,
};

struct video_obj {
	enum v4l2_field buf_field;
	/* Currently selected or default standard */
	v4l2_std_id stdid;
	struct v4l2_dv_timings dv_timings;
};

struct vpif_cap_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
};

struct common_obj {
	/* Pointer pointing to current v4l2_buffer */
	struct vpif_cap_buffer *cur_frm;
	/* Pointer pointing to current v4l2_buffer */
	struct vpif_cap_buffer *next_frm;
	/* Used to store pixel format */
	struct v4l2_format fmt;
	/* Buffer queue used in video-buf */
	struct vb2_queue buffer_queue;
	/* allocator-specific contexts for each plane */
	struct vb2_alloc_ctx *alloc_ctx;
	/* Queue of filled frames */
	struct list_head dma_queue;
	/* Used in video-buf */
	spinlock_t irqlock;
	/* lock used to access this structure */
	struct mutex lock;
	/* Function pointer to set the addresses */
	void (*set_addr) (unsigned long, unsigned long, unsigned long,
			  unsigned long);
	/* offset where Y top starts from the starting of the buffer */
	u32 ytop_off;
	/* offset where Y bottom starts from the starting of the buffer */
	u32 ybtm_off;
	/* offset where C top starts from the starting of the buffer */
	u32 ctop_off;
	/* offset where C bottom starts from the starting of the buffer */
	u32 cbtm_off;
	/* Indicates width of the image data */
	u32 width;
	/* Indicates height of the image data */
	u32 height;
};

struct channel_obj {
	/* Identifies video device for this channel */
	struct video_device video_dev;
	/* Indicates id of the field which is being displayed */
	u32 field_id;
	/* flag to indicate whether decoder is initialized */
	u8 initialized;
	/* Identifies channel */
	enum vpif_channel_id channel_id;
	/* Current input */
	u32 input_idx;
	/* subdev corresponding to the current input, may be NULL */
	struct v4l2_subdev *sd;
	/* vpif configuration params */
	struct vpif_params vpifparams;
	/* common object array */
	struct common_obj common[VPIF_NUMBER_OF_OBJECTS];
	/* video object */
	struct video_obj video;
};

struct vpif_device {
	struct v4l2_device v4l2_dev;
	struct channel_obj *dev[VPIF_CAPTURE_NUM_CHANNELS];
	struct v4l2_subdev **sd;
	struct v4l2_async_notifier notifier;
	struct vpif_capture_config *config;
};

#endif				/* VPIF_CAPTURE_H */
