/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author: Jacob Chen <jacob-chen@iotwrt.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/pm_runtime.h>

#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-sg.h>
#include <media/videobuf2-v4l2.h>

#include "rga-hw.h"
#include "rga.h"

static int
rga_queue_setup(struct vb2_queue *vq, const void *parg,
		unsigned int *nbuffers, unsigned int *nplanes,
		unsigned int sizes[], void *alloc_devs[])
{
	struct rga_ctx *ctx = vb2_get_drv_priv(vq);
	struct rockchip_rga *rga = ctx->rga;
	struct rga_frame *f = rga_get_frame(ctx, vq->type);

	if (IS_ERR(f))
		return PTR_ERR(f);

	sizes[0] = f->size;
	*nplanes = 1;

	if (*nbuffers == 0)
		*nbuffers = 1;
	alloc_devs[0] = rga->alloc_ctx;

	return 0;
}

static int rga_buf_prepare(struct vb2_buffer *vb)
{
	struct rga_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct rga_frame *f = rga_get_frame(ctx, vb->vb2_queue->type);

	if (IS_ERR(f))
		return PTR_ERR(f);

	vb2_set_plane_payload(vb, 0, f->size);

	return 0;
}

static void rga_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rga_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
}

static int rga_buf_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct rga_ctx *ctx = vb2_get_drv_priv(q);
	struct rockchip_rga *rga = ctx->rga;
	int ret;

	ret = pm_runtime_get_sync(rga->dev);
	return ret > 0 ? 0 : ret;
}

static void rga_buf_stop_streaming(struct vb2_queue *q)
{
	struct rga_ctx *ctx = vb2_get_drv_priv(q);
	struct rockchip_rga *rga = ctx->rga;
	struct vb2_v4l2_buffer *vbuf;

	for (;;) {
		if (V4L2_TYPE_IS_OUTPUT(q->type))
			vbuf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		else
			vbuf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
		if (!vbuf)
			break;
		v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_ERROR);
	}

	pm_runtime_put(rga->dev);
}

const struct vb2_ops rga_qops = {
	.queue_setup = rga_queue_setup,
	.buf_prepare = rga_buf_prepare,
	.buf_queue = rga_buf_queue,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.start_streaming = rga_buf_start_streaming,
	.stop_streaming = rga_buf_stop_streaming,
};

/* RGA MMU is a 1-Level MMU, so it can't be used through the IOMMU API.
 * We use it more like a scatter-gather list.
 */
void rga_buf_map(struct vb2_buffer *vb)
{
	struct rga_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct rockchip_rga *rga = ctx->rga;
	struct sg_table *sgt;
	struct scatterlist *sgl;
	unsigned int *pages;
	unsigned int address, len, i, p;
	unsigned int mapped_size = 0;

	if (vb->type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
		pages = rga->src_mmu_pages;
	else
		pages = rga->dst_mmu_pages;

	/* Create local MMU table for RGA */
	sgt = vb2_plane_cookie(vb, 0);

	for_each_sg(sgt->sgl, sgl, sgt->nents, i) {
		len = sg_dma_len(sgl) >> PAGE_SHIFT;
		address = sg_phys(sgl);

		for (p = 0; p < len; p++) {
			dma_addr_t phys = address + (p << PAGE_SHIFT);

			pages[mapped_size + p] = phys;
		}

		mapped_size += len;
	}

	/* sync local MMU table for RGA */
	dma_sync_single_for_device(rga->dev, virt_to_phys(pages),
				   8 * PAGE_SIZE, DMA_BIDIRECTIONAL);
}
