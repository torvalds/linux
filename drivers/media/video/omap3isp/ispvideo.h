/*
 * ispvideo.h
 *
 * TI OMAP3 ISP - Generic video node
 *
 * Copyright (C) 2009-2010 Nokia Corporation
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifndef OMAP3_ISP_VIDEO_H
#define OMAP3_ISP_VIDEO_H

#include <linux/v4l2-mediabus.h>
#include <media/media-entity.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-fh.h>

#include "ispqueue.h"

#define ISP_VIDEO_DRIVER_NAME		"ispvideo"
#define ISP_VIDEO_DRIVER_VERSION	"0.0.2"

struct isp_device;
struct isp_video;
struct v4l2_mbus_framefmt;
struct v4l2_pix_format;

/*
 * struct isp_format_info - ISP media bus format information
 * @code: V4L2 media bus format code
 * @truncated: V4L2 media bus format code for the same format truncated to 10
 *	bits. Identical to @code if the format is 10 bits wide or less.
 * @uncompressed: V4L2 media bus format code for the corresponding uncompressed
 *	format. Identical to @code if the format is not DPCM compressed.
 * @flavor: V4L2 media bus format code for the same pixel layout but
 *	shifted to be 8 bits per pixel. =0 if format is not shiftable.
 * @pixelformat: V4L2 pixel format FCC identifier
 * @bpp: Bits per pixel
 */
struct isp_format_info {
	enum v4l2_mbus_pixelcode code;
	enum v4l2_mbus_pixelcode truncated;
	enum v4l2_mbus_pixelcode uncompressed;
	enum v4l2_mbus_pixelcode flavor;
	u32 pixelformat;
	unsigned int bpp;
};

enum isp_pipeline_stream_state {
	ISP_PIPELINE_STREAM_STOPPED = 0,
	ISP_PIPELINE_STREAM_CONTINUOUS = 1,
	ISP_PIPELINE_STREAM_SINGLESHOT = 2,
};

enum isp_pipeline_state {
	/* The stream has been started on the input video node. */
	ISP_PIPELINE_STREAM_INPUT = 1,
	/* The stream has been started on the output video node. */
	ISP_PIPELINE_STREAM_OUTPUT = 2,
	/* At least one buffer is queued on the input video node. */
	ISP_PIPELINE_QUEUE_INPUT = 4,
	/* At least one buffer is queued on the output video node. */
	ISP_PIPELINE_QUEUE_OUTPUT = 8,
	/* The input entity is idle, ready to be started. */
	ISP_PIPELINE_IDLE_INPUT = 16,
	/* The output entity is idle, ready to be started. */
	ISP_PIPELINE_IDLE_OUTPUT = 32,
	/* The pipeline is currently streaming. */
	ISP_PIPELINE_STREAM = 64,
};

/*
 * struct isp_pipeline - An ISP hardware pipeline
 * @error: A hardware error occurred during capture
 * @entities: Bitmask of entities in the pipeline (indexed by entity ID)
 */
struct isp_pipeline {
	struct media_pipeline pipe;
	spinlock_t lock;		/* Pipeline state and queue flags */
	unsigned int state;
	enum isp_pipeline_stream_state stream_state;
	struct isp_video *input;
	struct isp_video *output;
	u32 entities;
	unsigned long l3_ick;
	unsigned int max_rate;
	atomic_t frame_number;
	bool do_propagation; /* of frame number */
	bool error;
	struct v4l2_fract max_timeperframe;
};

#define to_isp_pipeline(__e) \
	container_of((__e)->pipe, struct isp_pipeline, pipe)

static inline int isp_pipeline_ready(struct isp_pipeline *pipe)
{
	return pipe->state == (ISP_PIPELINE_STREAM_INPUT |
			       ISP_PIPELINE_STREAM_OUTPUT |
			       ISP_PIPELINE_QUEUE_INPUT |
			       ISP_PIPELINE_QUEUE_OUTPUT |
			       ISP_PIPELINE_IDLE_INPUT |
			       ISP_PIPELINE_IDLE_OUTPUT);
}

/*
 * struct isp_buffer - ISP buffer
 * @buffer: ISP video buffer
 * @isp_addr: MMU mapped address (a.k.a. device address) of the buffer.
 */
struct isp_buffer {
	struct isp_video_buffer buffer;
	dma_addr_t isp_addr;
};

#define to_isp_buffer(buf)	container_of(buf, struct isp_buffer, buffer)

enum isp_video_dmaqueue_flags {
	/* Set if DMA queue becomes empty when ISP_PIPELINE_STREAM_CONTINUOUS */
	ISP_VIDEO_DMAQUEUE_UNDERRUN = (1 << 0),
	/* Set when queuing buffer to an empty DMA queue */
	ISP_VIDEO_DMAQUEUE_QUEUED = (1 << 1),
};

#define isp_video_dmaqueue_flags_clr(video)	\
			({ (video)->dmaqueue_flags = 0; })

/*
 * struct isp_video_operations - ISP video operations
 * @queue:	Resume streaming when a buffer is queued. Called on VIDIOC_QBUF
 *		if there was no buffer previously queued.
 */
struct isp_video_operations {
	int(*queue)(struct isp_video *video, struct isp_buffer *buffer);
};

struct isp_video {
	struct video_device video;
	enum v4l2_buf_type type;
	struct media_pad pad;

	struct mutex mutex;		/* format and crop settings */
	atomic_t active;

	struct isp_device *isp;

	unsigned int capture_mem;
	unsigned int bpl_alignment;	/* alignment value */
	unsigned int bpl_zero_padding;	/* whether the alignment is optional */
	unsigned int bpl_max;		/* maximum bytes per line value */
	unsigned int bpl_value;		/* bytes per line value */
	unsigned int bpl_padding;	/* padding at end of line */

	/* Entity video node streaming */
	unsigned int streaming:1;

	/* Pipeline state */
	struct isp_pipeline pipe;
	struct mutex stream_lock;	/* pipeline and stream states */

	/* Video buffers queue */
	struct isp_video_queue *queue;
	struct list_head dmaqueue;
	enum isp_video_dmaqueue_flags dmaqueue_flags;

	const struct isp_video_operations *ops;
};

#define to_isp_video(vdev)	container_of(vdev, struct isp_video, video)

struct isp_video_fh {
	struct v4l2_fh vfh;
	struct isp_video *video;
	struct isp_video_queue queue;
	struct v4l2_format format;
	struct v4l2_fract timeperframe;
};

#define to_isp_video_fh(fh)	container_of(fh, struct isp_video_fh, vfh)
#define isp_video_queue_to_isp_video_fh(q) \
				container_of(q, struct isp_video_fh, queue)

int omap3isp_video_init(struct isp_video *video, const char *name);
void omap3isp_video_cleanup(struct isp_video *video);
int omap3isp_video_register(struct isp_video *video,
			    struct v4l2_device *vdev);
void omap3isp_video_unregister(struct isp_video *video);
struct isp_buffer *omap3isp_video_buffer_next(struct isp_video *video);
void omap3isp_video_resume(struct isp_video *video, int continuous);
struct media_pad *omap3isp_video_remote_pad(struct isp_video *video);

const struct isp_format_info *
omap3isp_video_format_info(enum v4l2_mbus_pixelcode code);

#endif /* OMAP3_ISP_VIDEO_H */
