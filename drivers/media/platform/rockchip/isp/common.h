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

#ifndef _RKISP_COMMON_H
#define _RKISP_COMMON_H

#include <linux/mutex.h>
#include <linux/media.h>
#include <media/media-device.h>
#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-mc.h>

#define RKISP_DEFAULT_WIDTH		800
#define RKISP_DEFAULT_HEIGHT		600

#define RKISP_MAX_STREAM		6
#define RKISP_STREAM_MP			0
#define RKISP_STREAM_SP			1
#define RKISP_STREAM_DMATX0		2
#define RKISP_STREAM_DMATX1		3
#define RKISP_STREAM_DMATX2		4
#define RKISP_STREAM_DMATX3		5

#define RKISP_PLANE_Y			0
#define RKISP_PLANE_CB			1
#define RKISP_PLANE_CR			2

#define RKISP_EMDDATA_FIFO_MAX		4
#define RKISP_DMATX_CHECK              0xA5A5A5A5

enum rkisp_sd_type {
	RKISP_SD_SENSOR,
	RKISP_SD_PHY_CSI,
	RKISP_SD_VCM,
	RKISP_SD_FLASH,
	RKISP_SD_MAX,
};

/* One structure per video node */
struct rkisp_vdev_node {
	struct vb2_queue buf_queue;
	struct video_device vdev;
	struct media_pad pad;
};

enum rkisp_fmt_pix_type {
	FMT_YUV,
	FMT_RGB,
	FMT_BAYER,
	FMT_JPEG,
	FMT_MAX
};

enum rkisp_fmt_raw_pat_type {
	RAW_RGGB = 0,
	RAW_GRBG,
	RAW_GBRG,
	RAW_BGGR,
};

struct rkisp_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head queue;
	union {
		u32 buff_addr[VIDEO_MAX_PLANES];
		void *vaddr[VIDEO_MAX_PLANES];
	};
};

struct rkisp_dummy_buffer {
	struct list_head queue;
	struct dma_buf *dbuf;
	dma_addr_t dma_addr;
	void *mem_priv;
	void *vaddr;
	u32 size;
	bool is_need_vaddr;
	bool is_need_dbuf;
};

extern int rkisp_debug;

static inline
struct rkisp_vdev_node *vdev_to_node(struct video_device *vdev)
{
	return container_of(vdev, struct rkisp_vdev_node, vdev);
}

static inline struct rkisp_vdev_node *queue_to_node(struct vb2_queue *q)
{
	return container_of(q, struct rkisp_vdev_node, buf_queue);
}

static inline struct rkisp_buffer *to_rkisp_buffer(struct vb2_v4l2_buffer *vb)
{
	return container_of(vb, struct rkisp_buffer, vb);
}

static inline struct vb2_queue *to_vb2_queue(struct file *file)
{
	struct rkisp_vdev_node *vnode = video_drvdata(file);

	return &vnode->buf_queue;
}

static inline int rkisp_alloc_buffer(struct device *dev,
				     struct rkisp_dummy_buffer *buf)
{
	const struct vb2_mem_ops *ops = &vb2_dma_contig_memops;
	unsigned long attrs = buf->is_need_vaddr ? 0 : DMA_ATTR_NO_KERNEL_MAPPING;
	void *mem_priv;
	int ret = 0;

	if (!buf->size) {
		ret = -EINVAL;
		goto err;
	}

	mem_priv = ops->alloc(dev, attrs, buf->size,
			      DMA_BIDIRECTIONAL, GFP_KERNEL);
	if (IS_ERR_OR_NULL(mem_priv)) {
		ret = -ENOMEM;
		goto err;
	}

	buf->mem_priv = mem_priv;
	buf->dma_addr = *((dma_addr_t *)ops->cookie(mem_priv));
	if (!attrs)
		buf->vaddr = ops->vaddr(mem_priv);
	if (buf->is_need_dbuf)
		buf->dbuf = ops->get_dmabuf(mem_priv, O_RDWR);
	if (rkisp_debug)
		dev_info(dev, "%s buf:0x%x~0x%x size:%d\n", __func__,
			 (u32)buf->dma_addr, (u32)buf->dma_addr + buf->size, buf->size);

	return ret;
err:
	dev_err(dev, "%s failed ret:%d\n", __func__, ret);
	return ret;
}

static inline void rkisp_free_buffer(struct device *dev,
				     struct rkisp_dummy_buffer *buf)
{
	const struct vb2_mem_ops *ops = &vb2_dma_contig_memops;

	if (buf && buf->mem_priv) {
		if (rkisp_debug)
			dev_info(dev, "%s buf:0x%x~0x%x\n", __func__,
				 (u32)buf->dma_addr, (u32)buf->dma_addr + buf->size);
		if (buf->dbuf)
			dma_buf_put(buf->dbuf);
		ops->put(buf->mem_priv);
		buf->size = 0;
		buf->dbuf = NULL;
		buf->vaddr = NULL;
		buf->mem_priv = NULL;
		buf->is_need_dbuf = false;
		buf->is_need_vaddr = false;
	}
}

#endif /* _RKISP_COMMON_H */
