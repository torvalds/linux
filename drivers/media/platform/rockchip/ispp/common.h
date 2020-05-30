/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISPP_COMMON_H
#define _RKISPP_COMMON_H

#include <linux/mutex.h>
#include <media/media-device.h>
#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-v4l2.h>

#include "../isp/isp_ispp.h"

#define RKISPP_PLANE_Y		0
#define RKISPP_PLANE_UV		1

#define RKISPP_MAX_WIDTH	4416
#define RKISPP_MAX_HEIGHT	3312
#define RKISPP_MIN_WIDTH	66
#define RKISPP_MIN_HEIGHT	258

struct rkispp_device;

/* One structure per video node */
struct rkispp_vdev_node {
	struct vb2_queue buf_queue;
	struct video_device vdev;
	struct media_pad pad;
};

struct rkispp_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head queue;
	union {
		u32 buff_addr[VIDEO_MAX_PLANES];
		void *vaddr[VIDEO_MAX_PLANES];
	};
};

struct rkispp_dummy_buffer {
	struct list_head list;
	struct dma_buf *dbuf;
	dma_addr_t dma_addr;
	void *mem_priv;
	void *vaddr;
	/* timestamp in ns */
	u64 timestamp;
	u32 size;
	u32 id;
	bool is_need_vaddr;
	bool is_need_dbuf;
};

extern int rkispp_debug;

static inline struct rkispp_vdev_node *vdev_to_node(struct video_device *vdev)
{
	return container_of(vdev, struct rkispp_vdev_node, vdev);
}

static inline struct rkispp_vdev_node *queue_to_node(struct vb2_queue *q)
{
	return container_of(q, struct rkispp_vdev_node, buf_queue);
}

static inline struct rkispp_buffer *to_rkispp_buffer(struct vb2_v4l2_buffer *vb)
{
	return container_of(vb, struct rkispp_buffer, vb);
}

static inline struct vb2_queue *to_vb2_queue(struct file *file)
{
	struct rkispp_vdev_node *vnode = video_drvdata(file);

	return &vnode->buf_queue;
}

static inline void rkispp_write(void __iomem *addr, u32 val)
{
	writel(val, addr);
}

static inline u32 rkispp_read(void __iomem *addr)
{
	return readl(addr);
}

static inline void rkispp_set_bits(void __iomem *addr, u32 bit_mask, u32 val)
{
	u32 tmp = rkispp_read(addr) & ~bit_mask;

	rkispp_write(addr, val | tmp);
}

static inline void rkispp_clear_bits(void __iomem *addr, u32 bit_mask)
{
	u32 val = rkispp_read(addr);

	rkispp_write(addr, val & ~bit_mask);
}

int rkispp_fh_open(struct file *filp);
int rkispp_fh_release(struct file *filp);
int rkispp_allow_buffer(struct rkispp_device *dev,
			struct rkispp_dummy_buffer *buf);
void rkispp_free_buffer(struct rkispp_device *dev,
			struct rkispp_dummy_buffer *buf);

#endif
