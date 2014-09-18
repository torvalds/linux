/*
 * TI OMAP4 ISS V4L2 Driver - Generic video node
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 *
 * Author: Sergio Aguirre <sergio.a.aguirre@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef OMAP4_ISS_VIDEO_H
#define OMAP4_ISS_VIDEO_H

#include <linux/v4l2-mediabus.h>
#include <media/media-entity.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-fh.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#define ISS_VIDEO_DRIVER_NAME		"issvideo"
#define ISS_VIDEO_DRIVER_VERSION	"0.0.2"

struct iss_device;
struct iss_video;
struct v4l2_mbus_framefmt;
struct v4l2_pix_format;

/*
 * struct iss_format_info - ISS media bus format information
 * @code: V4L2 media bus format code
 * @truncated: V4L2 media bus format code for the same format truncated to 10
 *	bits. Identical to @code if the format is 10 bits wide or less.
 * @uncompressed: V4L2 media bus format code for the corresponding uncompressed
 *	format. Identical to @code if the format is not DPCM compressed.
 * @flavor: V4L2 media bus format code for the same pixel layout but
 *	shifted to be 8 bits per pixel. =0 if format is not shiftable.
 * @pixelformat: V4L2 pixel format FCC identifier
 * @bpp: Bits per pixel
 * @description: Human-readable format description
 */
struct iss_format_info {
	enum v4l2_mbus_pixelcode code;
	enum v4l2_mbus_pixelcode truncated;
	enum v4l2_mbus_pixelcode uncompressed;
	enum v4l2_mbus_pixelcode flavor;
	u32 pixelformat;
	unsigned int bpp;
	const char *description;
};

enum iss_pipeline_stream_state {
	ISS_PIPELINE_STREAM_STOPPED = 0,
	ISS_PIPELINE_STREAM_CONTINUOUS = 1,
	ISS_PIPELINE_STREAM_SINGLESHOT = 2,
};

enum iss_pipeline_state {
	/* The stream has been started on the input video node. */
	ISS_PIPELINE_STREAM_INPUT = 1,
	/* The stream has been started on the output video node. */
	ISS_PIPELINE_STREAM_OUTPUT = (1 << 1),
	/* At least one buffer is queued on the input video node. */
	ISS_PIPELINE_QUEUE_INPUT = (1 << 2),
	/* At least one buffer is queued on the output video node. */
	ISS_PIPELINE_QUEUE_OUTPUT = (1 << 3),
	/* The input entity is idle, ready to be started. */
	ISS_PIPELINE_IDLE_INPUT = (1 << 4),
	/* The output entity is idle, ready to be started. */
	ISS_PIPELINE_IDLE_OUTPUT = (1 << 5),
	/* The pipeline is currently streaming. */
	ISS_PIPELINE_STREAM = (1 << 6),
};

/*
 * struct iss_pipeline - An OMAP4 ISS hardware pipeline
 * @entities: Bitmask of entities in the pipeline (indexed by entity ID)
 * @error: A hardware error occurred during capture
 */
struct iss_pipeline {
	struct media_pipeline pipe;
	spinlock_t lock;		/* Pipeline state and queue flags */
	unsigned int state;
	enum iss_pipeline_stream_state stream_state;
	struct iss_video *input;
	struct iss_video *output;
	unsigned int entities;
	atomic_t frame_number;
	bool do_propagation; /* of frame number */
	bool error;
	struct v4l2_fract max_timeperframe;
	struct v4l2_subdev *external;
	unsigned int external_rate;
	int external_bpp;
};

#define to_iss_pipeline(__e) \
	container_of((__e)->pipe, struct iss_pipeline, pipe)

static inline int iss_pipeline_ready(struct iss_pipeline *pipe)
{
	return pipe->state == (ISS_PIPELINE_STREAM_INPUT |
			       ISS_PIPELINE_STREAM_OUTPUT |
			       ISS_PIPELINE_QUEUE_INPUT |
			       ISS_PIPELINE_QUEUE_OUTPUT |
			       ISS_PIPELINE_IDLE_INPUT |
			       ISS_PIPELINE_IDLE_OUTPUT);
}

/*
 * struct iss_buffer - ISS buffer
 * @buffer: ISS video buffer
 * @iss_addr: Physical address of the buffer.
 */
struct iss_buffer {
	/* common v4l buffer stuff -- must be first */
	struct vb2_buffer	vb;
	struct list_head	list;
	dma_addr_t iss_addr;
};

#define to_iss_buffer(buf)	container_of(buf, struct iss_buffer, buffer)

enum iss_video_dmaqueue_flags {
	/* Set if DMA queue becomes empty when ISS_PIPELINE_STREAM_CONTINUOUS */
	ISS_VIDEO_DMAQUEUE_UNDERRUN = (1 << 0),
	/* Set when queuing buffer to an empty DMA queue */
	ISS_VIDEO_DMAQUEUE_QUEUED = (1 << 1),
};

#define iss_video_dmaqueue_flags_clr(video)	\
			({ (video)->dmaqueue_flags = 0; })

/*
 * struct iss_video_operations - ISS video operations
 * @queue:	Resume streaming when a buffer is queued. Called on VIDIOC_QBUF
 *		if there was no buffer previously queued.
 */
struct iss_video_operations {
	int (*queue)(struct iss_video *video, struct iss_buffer *buffer);
};

struct iss_video {
	struct video_device video;
	enum v4l2_buf_type type;
	struct media_pad pad;

	struct mutex mutex;		/* format and crop settings */
	atomic_t active;

	struct iss_device *iss;

	unsigned int capture_mem;
	unsigned int bpl_alignment;	/* alignment value */
	unsigned int bpl_zero_padding;	/* whether the alignment is optional */
	unsigned int bpl_max;		/* maximum bytes per line value */
	unsigned int bpl_value;		/* bytes per line value */
	unsigned int bpl_padding;	/* padding at end of line */

	/* Pipeline state */
	struct iss_pipeline pipe;
	struct mutex stream_lock;	/* pipeline and stream states */
	bool error;

	/* Video buffers queue */
	struct vb2_queue *queue;
	spinlock_t qlock;		/* protects dmaqueue and error */
	struct list_head dmaqueue;
	enum iss_video_dmaqueue_flags dmaqueue_flags;
	struct vb2_alloc_ctx *alloc_ctx;

	const struct iss_video_operations *ops;
};

#define to_iss_video(vdev)	container_of(vdev, struct iss_video, video)

struct iss_video_fh {
	struct v4l2_fh vfh;
	struct iss_video *video;
	struct vb2_queue queue;
	struct v4l2_format format;
	struct v4l2_fract timeperframe;
};

#define to_iss_video_fh(fh)	container_of(fh, struct iss_video_fh, vfh)
#define iss_video_queue_to_iss_video_fh(q) \
				container_of(q, struct iss_video_fh, queue)

int omap4iss_video_init(struct iss_video *video, const char *name);
void omap4iss_video_cleanup(struct iss_video *video);
int omap4iss_video_register(struct iss_video *video,
			    struct v4l2_device *vdev);
void omap4iss_video_unregister(struct iss_video *video);
struct iss_buffer *omap4iss_video_buffer_next(struct iss_video *video);
void omap4iss_video_cancel_stream(struct iss_video *video);
struct media_pad *omap4iss_video_remote_pad(struct iss_video *video);

const struct iss_format_info *
omap4iss_video_format_info(enum v4l2_mbus_pixelcode code);

#endif /* OMAP4_ISS_VIDEO_H */
