/*
 * DM646x display header file
 *
 * Copyright (C) 2009 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed .as is. WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef DAVINCIHD_DISPLAY_H
#define DAVINCIHD_DISPLAY_H

/* Header files */
#include <linux/videodev2.h>
#include <linux/version.h>
#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/videobuf-core.h>
#include <media/videobuf-dma-contig.h>

#include "vpif.h"

/* Macros */
#define VPIF_MAJOR_RELEASE	(0)
#define VPIF_MINOR_RELEASE	(0)
#define VPIF_BUILD		(1)

#define VPIF_DISPLAY_VERSION_CODE \
	((VPIF_MAJOR_RELEASE << 16) | (VPIF_MINOR_RELEASE << 8) | VPIF_BUILD)

#define VPIF_VALID_FIELD(field) \
	(((V4L2_FIELD_ANY == field) || (V4L2_FIELD_NONE == field)) || \
	(((V4L2_FIELD_INTERLACED == field) || (V4L2_FIELD_SEQ_TB == field)) || \
	(V4L2_FIELD_SEQ_BT == field)))

#define VPIF_DISPLAY_MAX_DEVICES	(2)
#define VPIF_SLICED_BUF_SIZE		(256)
#define VPIF_SLICED_MAX_SERVICES	(3)
#define VPIF_VIDEO_INDEX		(0)
#define VPIF_VBI_INDEX			(1)
#define VPIF_HBI_INDEX			(2)

/* Setting it to 1 as HBI/VBI support yet to be added , else 3*/
#define VPIF_NUMOBJECTS	(1)

/* Macros */
#define ISALIGNED(a)    (0 == ((a) & 7))

/* enumerated data types */
/* Enumerated data type to give id to each device per channel */
enum vpif_channel_id {
	VPIF_CHANNEL2_VIDEO = 0,	/* Channel2 Video */
	VPIF_CHANNEL3_VIDEO,		/* Channel3 Video */
};

/* structures */

struct video_obj {
	enum v4l2_field buf_field;
	u32 latest_only;		/* indicate whether to return
					 * most recent displayed frame only */
	v4l2_std_id stdid;		/* Currently selected or default
					 * standard */
	u32 dv_preset;
	struct v4l2_bt_timings bt_timings;
	u32 output_id;			/* Current output id */
};

struct vbi_obj {
	int num_services;
	struct vpif_vbi_params vbiparams;	/* vpif parameters for the raw
						 * vbi data */
};

struct common_obj {
	/* Buffer specific parameters */
	u8 *fbuffers[VIDEO_MAX_FRAME];		/* List of buffer pointers for
						 * storing frames */
	u32 numbuffers;				/* number of buffers */
	struct videobuf_buffer *cur_frm;	/* Pointer pointing to current
						 * videobuf_buffer */
	struct videobuf_buffer *next_frm;	/* Pointer pointing to next
						 * videobuf_buffer */
	enum v4l2_memory memory;		/* This field keeps track of
						 * type of buffer exchange
						 * method user has selected */
	struct v4l2_format fmt;			/* Used to store the format */
	struct videobuf_queue buffer_queue;	/* Buffer queue used in
						 * video-buf */
	struct list_head dma_queue;		/* Queue of filled frames */
	spinlock_t irqlock;			/* Used in video-buf */

	/* channel specific parameters */
	struct mutex lock;			/* lock used to access this
						 * structure */
	u32 io_usrs;				/* number of users performing
						 * IO */
	u8 started;				/* Indicates whether streaming
						 * started */
	u32 ytop_off;				/* offset of Y top from the
						 * starting of the buffer */
	u32 ybtm_off;				/* offset of Y bottom from the
						 * starting of the buffer */
	u32 ctop_off;				/* offset of C top from the
						 * starting of the buffer */
	u32 cbtm_off;				/* offset of C bottom from the
						 * starting of the buffer */
	/* Function pointer to set the addresses */
	void (*set_addr) (unsigned long, unsigned long,
				unsigned long, unsigned long);
	u32 height;
	u32 width;
};

struct channel_obj {
	/* V4l2 specific parameters */
	struct video_device *video_dev;	/* Identifies video device for
					 * this channel */
	struct v4l2_prio_state prio;	/* Used to keep track of state of
					 * the priority */
	atomic_t usrs;			/* number of open instances of
					 * the channel */
	u32 field_id;			/* Indicates id of the field
					 * which is being displayed */
	u8 initialized;			/* flag to indicate whether
					 * encoder is initialized */

	enum vpif_channel_id channel_id;/* Identifies channel */
	struct vpif_params vpifparams;
	struct common_obj common[VPIF_NUMOBJECTS];
	struct video_obj video;
	struct vbi_obj vbi;
};

/* File handle structure */
struct vpif_fh {
	struct channel_obj *channel;	/* pointer to channel object for
					 * opened device */
	u8 io_allowed[VPIF_NUMOBJECTS];	/* Indicates whether this file handle
					 * is doing IO */
	enum v4l2_priority prio;	/* Used to keep track priority of
					 * this instance */
	u8 initialized;			/* Used to keep track of whether this
					 * file handle has initialized
					 * channel or not */
};

/* vpif device structure */
struct vpif_device {
	struct v4l2_device v4l2_dev;
	struct channel_obj *dev[VPIF_DISPLAY_NUM_CHANNELS];
	struct v4l2_subdev **sd;

};

struct vpif_config_params {
	u32 min_bufsize[VPIF_DISPLAY_NUM_CHANNELS];
	u32 channel_bufsize[VPIF_DISPLAY_NUM_CHANNELS];
	u8 numbuffers[VPIF_DISPLAY_NUM_CHANNELS];
	u8 min_numbuffers;
};

/* Struct which keeps track of the line numbers for the sliced vbi service */
struct vpif_service_line {
	u16 service_id;
	u16 service_line[2];
	u16 enc_service_id;
	u8 bytestowrite;
};

#endif				/* DAVINCIHD_DISPLAY_H */
