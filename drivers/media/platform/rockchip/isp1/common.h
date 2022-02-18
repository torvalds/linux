/*
 * Rockchip isp1 driver
 *
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _RKISP1_COMMON_H
#define _RKISP1_COMMON_H

#include <linux/mutex.h>
#include <linux/media.h>
#include <linux/rk-video-format.h>
#include <media/media-device.h>
#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-mc.h>

#define RKISP1_DEFAULT_WIDTH		800
#define RKISP1_DEFAULT_HEIGHT		600

#define RKISP1_MAX_STREAM		3
#define RKISP1_STREAM_MP		0
#define RKISP1_STREAM_SP		1
#define RKISP1_STREAM_RAW		2

#define RKISP1_PLANE_Y			0
#define RKISP1_PLANE_CB			1
#define RKISP1_PLANE_CR			2

#define RKISP1_EMDDATA_FIFO_MAX		4
#define RKISP1_DMATX_CHECK              0xA5A5A5A5
#define RKISP1_RK3326_USE_OLDMIPI	0

enum rkisp1_sd_type {
	RKISP1_SD_SENSOR,
	RKISP1_SD_PHY_CSI,
	RKISP1_SD_VCM,
	RKISP1_SD_FLASH,
	RKISP1_SD_MAX,
};

/* One structure per video node */
struct rkisp1_vdev_node {
	struct vb2_queue buf_queue;
	struct video_device vdev;
	struct media_pad pad;
};

enum rkisp1_fmt_pix_type {
	FMT_YUV,
	FMT_RGB,
	FMT_BAYER,
	FMT_JPEG,
	FMT_MAX
};

enum rkisp1_fmt_raw_pat_type {
	RAW_RGGB = 0,
	RAW_GRBG,
	RAW_GBRG,
	RAW_BGGR,
};

struct rkisp1_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head queue;
	union {
		u32 buff_addr[VIDEO_MAX_PLANES];
		void *vaddr[VIDEO_MAX_PLANES];
	};
};

struct rkisp1_dummy_buffer {
	void *vaddr;
	dma_addr_t dma_addr;
	u32 size;
};

extern int rkisp1_debug;

static inline
struct rkisp1_vdev_node *vdev_to_node(struct video_device *vdev)
{
	return container_of(vdev, struct rkisp1_vdev_node, vdev);
}

static inline struct rkisp1_vdev_node *queue_to_node(struct vb2_queue *q)
{
	return container_of(q, struct rkisp1_vdev_node, buf_queue);
}

static inline struct rkisp1_buffer *to_rkisp1_buffer(struct vb2_v4l2_buffer *vb)
{
	return container_of(vb, struct rkisp1_buffer, vb);
}

static inline struct vb2_queue *to_vb2_queue(struct file *file)
{
	struct rkisp1_vdev_node *vnode = video_drvdata(file);

	return &vnode->buf_queue;
}

#endif /* _RKISP1_COMMON_H */
