/* SPDX-License-Identifier: GPL-2.0 */
/*
 * A virtual stateless device for stateless uAPI development purposes.
 *
 * This tool's objective is to help the development and testing of userspace
 * applications that use the V4L2 stateless API to decode media.
 *
 * A userspace implementation can use visl to run a decoding loop even when no
 * hardware is available or when the kernel uAPI for the codec has not been
 * upstreamed yet. This can reveal bugs at an early stage.
 *
 * This driver can also trace the contents of the V4L2 controls submitted to it.
 * It can also dump the contents of the vb2 buffers through a debugfs
 * interface. This is in many ways similar to the tracing infrastructure
 * available for other popular encode/decode APIs out there and can help develop
 * a userspace application by using another (working) one as a reference.
 *
 * Note that no actual decoding of video frames is performed by visl. The V4L2
 * test pattern generator is used to write various debug information to the
 * capture buffers instead.
 *
 * Copyright (C) 2022 Collabora, Ltd.
 *
 * Based on the vim2m driver, that is:
 *
 * Copyright (c) 2009-2010 Samsung Electronics Co., Ltd.
 * Pawel Osciak, <pawel@osciak.com>
 * Marek Szyprowski, <m.szyprowski@samsung.com>
 *
 * Based on the vicodec driver, that is:
 *
 * Copyright 2018 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * Based on the Cedrus VPU driver, that is:
 *
 * Copyright (C) 2016 Florent Revest <florent.revest@free-electrons.com>
 * Copyright (C) 2018 Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 * Copyright (C) 2018 Bootlin
 */

#ifndef _VISL_H_
#define _VISL_H_

#include <linux/debugfs.h>
#include <linux/list.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/tpg/v4l2-tpg.h>

#define VISL_NAME		"visl"
#define VISL_M2M_NQUEUES	2

#define TPG_STR_BUF_SZ		2048

extern unsigned int visl_transtime_ms;

struct visl_ctrls {
	const struct visl_ctrl_desc *ctrls;
	unsigned int num_ctrls;
};

struct visl_coded_format_desc {
	u32 pixelformat;
	struct v4l2_frmsize_stepwise frmsize;
	const struct visl_ctrls *ctrls;
	unsigned int num_decoded_fmts;
	const u32 *decoded_fmts;
};

extern const struct visl_coded_format_desc visl_coded_fmts[];
extern const size_t num_coded_fmts;

enum {
	V4L2_M2M_SRC = 0,
	V4L2_M2M_DST = 1,
};

extern unsigned int visl_debug;
#define dprintk(dev, fmt, arg...) \
	v4l2_dbg(1, visl_debug, &(dev)->v4l2_dev, "%s: " fmt, __func__, ## arg)

extern int visl_dprintk_frame_start;
extern unsigned int visl_dprintk_nframes;
extern bool keep_bitstream_buffers;
extern int bitstream_trace_frame_start;
extern unsigned int bitstream_trace_nframes;
extern bool tpg_verbose;

#define frame_dprintk(dev, current, fmt, arg...) \
	do { \
		if (visl_dprintk_frame_start > -1 && \
		    (current) >= visl_dprintk_frame_start && \
		    (current) < visl_dprintk_frame_start + visl_dprintk_nframes) \
			dprintk(dev, fmt, ## arg); \
	} while (0) \

struct visl_q_data {
	unsigned int		sequence;
};

struct visl_dev {
	struct v4l2_device	v4l2_dev;
	struct video_device	vfd;
#ifdef CONFIG_MEDIA_CONTROLLER
	struct media_device	mdev;
#endif

	struct mutex		dev_mutex;

	struct v4l2_m2m_dev	*m2m_dev;

#ifdef CONFIG_VISL_DEBUGFS
	struct dentry		*debugfs_root;
	struct dentry		*bitstream_debugfs;
	struct list_head	bitstream_blobs;

	/* Protects the "blob" list */
	struct mutex		bitstream_lock;
#endif
};

enum visl_codec {
	VISL_CODEC_NONE,
	VISL_CODEC_FWHT,
	VISL_CODEC_MPEG2,
	VISL_CODEC_VP8,
	VISL_CODEC_VP9,
	VISL_CODEC_H264,
	VISL_CODEC_HEVC,
	VISL_CODEC_AV1,
};

struct visl_blob {
	struct list_head list;
	struct dentry *dentry;
	struct debugfs_blob_wrapper blob;
};

struct visl_ctx {
	struct v4l2_fh		fh;
	struct visl_dev	*dev;
	struct v4l2_ctrl_handler hdl;

	struct mutex		vb_mutex;

	struct visl_q_data	q_data[VISL_M2M_NQUEUES];
	enum   visl_codec	current_codec;

	const struct visl_coded_format_desc *coded_format_desc;

	struct v4l2_format	coded_fmt;
	struct v4l2_format	decoded_fmt;

	struct tpg_data		tpg;
	u64			capture_streamon_jiffies;
	char			*tpg_str_buf;
};

struct visl_ctrl_desc {
	struct v4l2_ctrl_config cfg;
};

static inline struct visl_ctx *visl_file_to_ctx(struct file *file)
{
	return container_of(file_to_v4l2_fh(file), struct visl_ctx, fh);
}

void *visl_find_control_data(struct visl_ctx *ctx, u32 id);
struct v4l2_ctrl *visl_find_control(struct visl_ctx *ctx, u32 id);
u32 visl_control_num_elems(struct visl_ctx *ctx, u32 id);

#endif /* _VISL_H_ */
