/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2012 Texas Instruments Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Contributors:
 *      Manjunath Hadli <manjunath.hadli@ti.com>
 *      Prabhakar Lad <prabhakar.lad@ti.com>
 */

#ifndef _DAVINCI_VPFE_VIDEO_H
#define _DAVINCI_VPFE_VIDEO_H

#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>

struct vpfe_device;

/*
 * struct vpfe_video_operations - VPFE video operations
 * @queue:	Resume streaming when a buffer is queued. Called on VIDIOC_QBUF
 *		if there was no buffer previously queued.
 */
struct vpfe_video_operations {
	int (*queue)(struct vpfe_device *vpfe_dev, unsigned long addr);
};

enum vpfe_pipeline_stream_state {
	VPFE_PIPELINE_STREAM_STOPPED = 0,
	VPFE_PIPELINE_STREAM_CONTINUOUS = 1,
	VPFE_PIPELINE_STREAM_SINGLESHOT = 2,
};

enum vpfe_video_state {
	/* indicates that buffer is not queued */
	VPFE_VIDEO_BUFFER_NOT_QUEUED = 0,
	/* indicates that buffer is queued */
	VPFE_VIDEO_BUFFER_QUEUED = 1,
};

struct vpfe_pipeline {
	/* media pipeline */
	struct media_pipeline		*pipe;
	struct media_graph	graph;
	/* state of the pipeline, continuous,
	 * single-shot or stopped
	 */
	enum vpfe_pipeline_stream_state	state;
	/* number of active input video entities */
	unsigned int			input_num;
	/* number of active output video entities */
	unsigned int			output_num;
	/* input video nodes in case of single-shot mode */
	struct vpfe_video_device	*inputs[10];
	/* capturing video nodes */
	struct vpfe_video_device	*outputs[10];
};

#define to_vpfe_pipeline(__e) \
	container_of((__e)->pipe, struct vpfe_pipeline, pipe)

#define to_vpfe_video(vdev) \
	container_of(vdev, struct vpfe_video_device, video_dev)

struct vpfe_cap_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
};

struct vpfe_video_device {
	/* vpfe device */
	struct vpfe_device			*vpfe_dev;
	/* video dev */
	struct video_device			video_dev;
	/* media pad of video entity */
	struct media_pad			pad;
	/* video operations supported by video device */
	const struct vpfe_video_operations	*ops;
	/* type of the video buffers used by user */
	enum v4l2_buf_type			type;
	/* Indicates id of the field which is being captured */
	u32					field_id;
	/* pipeline for which video device is part of */
	struct vpfe_pipeline			pipe;
	/* Indicates whether streaming started */
	u8					started;
	/* Indicates state of the stream */
	unsigned int				state;
	/* current input at the sub device */
	int					current_input;
	/*
	 * This field keeps track of type of buffer exchange mechanism
	 * user has selected
	 */
	enum v4l2_memory			memory;
	/* number of open instances of the channel */
	u32					usrs;
	/* flag to indicate whether decoder is initialized */
	u8					initialized;
	/* skip frame count */
	u8					skip_frame_count;
	/* skip frame count init value */
	u8					skip_frame_count_init;
	/* time per frame for skipping */
	struct v4l2_fract			timeperframe;
	/* ptr to currently selected sub device */
	struct vpfe_ext_subdev_info		*current_ext_subdev;
	/* Pointer pointing to current vpfe_cap_buffer */
	struct vpfe_cap_buffer			*cur_frm;
	/* Pointer pointing to next vpfe_cap_buffer */
	struct vpfe_cap_buffer			*next_frm;
	/* Used to store pixel format */
	struct v4l2_format			fmt;
	struct vb2_queue			buffer_queue;
	/* Queue of filled frames */
	struct list_head			dma_queue;
	spinlock_t				irqlock;
	/* IRQ lock for DMA queue */
	spinlock_t				dma_queue_lock;
	/* lock used to serialize all video4linux ioctls */
	struct mutex				lock;
	/* number of users performing IO */
	u32					io_usrs;
	/* Currently selected or default standard */
	v4l2_std_id				stdid;
	/*
	 * offset where second field starts from the starting of the
	 * buffer for field separated YCbCr formats
	 */
	u32					field_off;
};

int vpfe_video_is_pipe_ready(struct vpfe_pipeline *pipe);
void vpfe_video_unregister(struct vpfe_video_device *video);
int vpfe_video_register(struct vpfe_video_device *video,
			struct v4l2_device *vdev);
int vpfe_video_init(struct vpfe_video_device *video, const char *name);
void vpfe_video_process_buffer_complete(struct vpfe_video_device *video);
void vpfe_video_schedule_bottom_field(struct vpfe_video_device *video);
void vpfe_video_schedule_next_buffer(struct vpfe_video_device *video);

#endif		/* _DAVINCI_VPFE_VIDEO_H */
