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

#ifdef __KERNEL__

/* Header files */
#include <linux/videodev2.h>
#include <linux/version.h>
#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/videobuf-core.h>
#include <media/videobuf-dma-contig.h>
#include <mach/dm646x.h>

#include "vpif.h"

/* Macros */
#define VPIF_MAJOR_RELEASE		0
#define VPIF_MINOR_RELEASE		0
#define VPIF_BUILD			1
#define VPIF_CAPTURE_VERSION_CODE	((VPIF_MAJOR_RELEASE << 16) | \
	(VPIF_MINOR_RELEASE << 8) | VPIF_BUILD)

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
	u32 dv_preset;
	struct v4l2_bt_timings bt_timings;
	/* This is to track the last input that is passed to application */
	u32 input_idx;
};

struct common_obj {
	/* Pointer pointing to current v4l2_buffer */
	struct videobuf_buffer *cur_frm;
	/* Pointer pointing to current v4l2_buffer */
	struct videobuf_buffer *next_frm;
	/*
	 * This field keeps track of type of buffer exchange mechanism
	 * user has selected
	 */
	enum v4l2_memory memory;
	/* Used to store pixel format */
	struct v4l2_format fmt;
	/* Buffer queue used in video-buf */
	struct videobuf_queue buffer_queue;
	/* Queue of filled frames */
	struct list_head dma_queue;
	/* Used in video-buf */
	spinlock_t irqlock;
	/* lock used to access this structure */
	struct mutex lock;
	/* number of users performing IO */
	u32 io_usrs;
	/* Indicates whether streaming started */
	u8 started;
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
	struct video_device *video_dev;
	/* Used to keep track of state of the priority */
	struct v4l2_prio_state prio;
	/* number of open instances of the channel */
	int usrs;
	/* Indicates id of the field which is being displayed */
	u32 field_id;
	/* flag to indicate whether decoder is initialized */
	u8 initialized;
	/* Identifies channel */
	enum vpif_channel_id channel_id;
	/* index into sd table */
	int curr_sd_index;
	/* ptr to current sub device information */
	struct vpif_subdev_info *curr_subdev_info;
	/* vpif configuration params */
	struct vpif_params vpifparams;
	/* common object array */
	struct common_obj common[VPIF_NUMBER_OF_OBJECTS];
	/* video object */
	struct video_obj video;
};

/* File handle structure */
struct vpif_fh {
	/* pointer to channel object for opened device */
	struct channel_obj *channel;
	/* Indicates whether this file handle is doing IO */
	u8 io_allowed[VPIF_NUMBER_OF_OBJECTS];
	/* Used to keep track priority of this instance */
	enum v4l2_priority prio;
	/* Used to indicate channel is initialize or not */
	u8 initialized;
};

struct vpif_device {
	struct v4l2_device v4l2_dev;
	struct channel_obj *dev[VPIF_CAPTURE_NUM_CHANNELS];
	struct v4l2_subdev **sd;
};

struct vpif_config_params {
	u8 min_numbuffers;
	u8 numbuffers[VPIF_CAPTURE_NUM_CHANNELS];
	s8 device_type;
	u32 min_bufsize[VPIF_CAPTURE_NUM_CHANNELS];
	u32 channel_bufsize[VPIF_CAPTURE_NUM_CHANNELS];
	u8 default_device[VPIF_CAPTURE_NUM_CHANNELS];
	u8 max_device_type;
};
/* Struct which keeps track of the line numbers for the sliced vbi service */
struct vpif_service_line {
	u16 service_id;
	u16 service_line[2];
};
#endif				/* End of __KERNEL__ */
#endif				/* VPIF_CAPTURE_H */
