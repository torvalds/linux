/* SPDX-License-Identifier: GPL-2.0 */
/*
 * stf_video.h
 *
 * StarFive Camera Subsystem - V4L2 device node
 *
 * Copyright (C) 2021-2023 StarFive Technology Co., Ltd.
 */

#ifndef STF_VIDEO_H
#define STF_VIDEO_H

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/videodev2.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>

#define STFCAMSS_FRAME_MIN_WIDTH		64
#define STFCAMSS_FRAME_MAX_WIDTH		1920
#define STFCAMSS_FRAME_MIN_HEIGHT		64
#define STFCAMSS_FRAME_MAX_HEIGHT		1080
#define STFCAMSS_FRAME_WIDTH_ALIGN_8		8
#define STFCAMSS_FRAME_WIDTH_ALIGN_128		128
#define STFCAMSS_MIN_BUFFERS			2

#define STFCAMSS_MAX_ENTITY_NAME_LEN		27

enum stf_v_line_id {
	STF_V_LINE_WR = 0,
	STF_V_LINE_ISP,
	STF_V_LINE_MAX,
};

enum stf_capture_type {
	STF_CAPTURE_RAW = 0,
	STF_CAPTURE_YUV,
	STF_CAPTURE_NUM,
};

struct stfcamss_buffer {
	struct vb2_v4l2_buffer vb;
	dma_addr_t addr[2];
	struct list_head queue;
};

struct fract {
	u8 numerator;
	u8 denominator;
};

/*
 * struct stfcamss_format_info - ISP media bus format information
 * @code: V4L2 media bus format code
 * @pixelformat: V4L2 pixel format FCC identifier
 * @planes: Number of planes
 * @vsub: Vertical subsampling (for each plane)
 * @bpp: Bits per pixel when stored in memory (for each plane)
 */
struct stfcamss_format_info {
	u32 code;
	u32 pixelformat;
	u8 planes;
	u8 vsub[3];
	u8 bpp;
};

struct stfcamss_video {
	struct stfcamss *stfcamss;
	struct vb2_queue vb2_q;
	struct video_device vdev;
	struct media_pad pad;
	struct v4l2_format active_fmt;
	enum v4l2_buf_type type;
	const struct stfcamss_video_ops *ops;
	struct mutex lock;	 /* serialize device access */
	struct mutex q_lock;	 /* protects the queue */
	unsigned int bpl_alignment;
	const struct stfcamss_format_info *formats;
	unsigned int nformats;
	struct v4l2_subdev *source_subdev;
};

struct stfcamss_video_ops {
	int (*queue_buffer)(struct stfcamss_video *video,
			    struct stfcamss_buffer *buf);
	int (*flush_buffers)(struct stfcamss_video *video,
			     enum vb2_buffer_state state);
	void (*start_streaming)(struct stfcamss_video *video);
	void (*stop_streaming)(struct stfcamss_video *video);
};

int stf_video_register(struct stfcamss_video *video,
		       struct v4l2_device *v4l2_dev, const char *name);

void stf_video_unregister(struct stfcamss_video *video);

#endif /* STF_VIDEO_H */
