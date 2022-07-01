/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2021 StarFive Technology Co., Ltd.
 */
#ifndef STF_VIDEO_H
#define STF_VIDEO_H

#include <linux/mutex.h>
#include <media/videobuf2-v4l2.h>
#include <linux/videodev2.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>

#define STFCAMSS_FRAME_MIN_WIDTH           64
#define STFCAMSS_FRAME_MAX_WIDTH           8191
#define STFCAMSS_FRAME_MIN_HEIGHT          64
#define STFCAMSS_FRAME_MAX_HEIGHT          8191
#define STFCAMSS_FRAME_WIDTH_ALIGN_8       8
#define STFCAMSS_FRAME_WIDTH_ALIGN_128     128
#define STFCAMSS_MIN_BUFFERS               2

struct stfcamss_buffer {
	struct vb2_v4l2_buffer vb;
	dma_addr_t addr[3];
	struct list_head queue;
	int sizeimage;
};

struct stfcamss_video;

struct stfcamss_video_ops {
	int (*queue_buffer)(struct stfcamss_video *vid,
			struct stfcamss_buffer *buf);
	int (*flush_buffers)(struct stfcamss_video *vid,
			enum vb2_buffer_state state);
};

struct fract {
	u8 numerator;
	u8 denominator;
};

struct stfcamss_format_info {
	u32 code;
	u32 pixelformat;
	u8 planes;
	struct fract hsub[3];
	struct fract vsub[3];
	u8 bpp[3];
};

struct stfcamss_video {
	struct stfcamss *stfcamss;
	u8 id;
	struct vb2_queue vb2_q;
	struct video_device vdev;
	struct media_pad pad;
	struct media_pipeline pipe;
	struct v4l2_format active_fmt;
	enum v4l2_buf_type type;
	const struct stfcamss_video_ops *ops;
	struct mutex lock;
	struct mutex q_lock;
	unsigned int bpl_alignment;
	const struct stfcamss_format_info *formats;
	unsigned int nformats;
	unsigned int is_mp;
};

int stf_video_register(struct stfcamss_video *video,
		struct v4l2_device *v4l2_dev, const char *name, int is_mp);

void stf_video_unregister(struct stfcamss_video *video);

#endif /* STF_VIDEO_H */
