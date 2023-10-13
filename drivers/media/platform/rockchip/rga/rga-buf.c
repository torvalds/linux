// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co.Ltd
 * Author: Jacob Chen <jacob-chen@iotwrt.com>
 */

#include <linux/pm_runtime.h>
#include <linux/scatterlist.h>

#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-sg.h>
#include <media/videobuf2-v4l2.h>

#include "rga-hw.h"
#include "rga.h"

static size_t fill_descriptors(struct rga_dma_desc *desc, struct sg_table *sgt)
{
	struct sg_dma_page_iter iter;
	struct rga_dma_desc *tmp = desc;
	size_t n_desc = 0;
	dma_addr_t addr;

	for_each_sgtable_dma_page(sgt, &iter, 0) {
		addr = sg_page_iter_dma_address(&iter);
		tmp->addr = lower_32_bits(addr);
		tmp++;
		n_desc++;
	}

	return n_desc;
}

static int
rga_queue_setup(struct vb2_queue *vq,
		unsigned int *nbuffers, unsigned int *nplanes,
		unsigned int sizes[], struct device *alloc_devs[])
{
	struct rga_ctx *ctx = vb2_get_drv_priv(vq);
	struct rga_frame *f = rga_get_frame(ctx, vq->type);

	if (IS_ERR(f))
		return PTR_ERR(f);

	if (*nplanes)
		return sizes[0] < f->size ? -EINVAL : 0;

	sizes[0] = f->size;
	*nplanes = 1;

	return 0;
}

static int rga_buf_init(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rga_vb_buffer *rbuf = vb_to_rga(vbuf);
	struct rga_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct rockchip_rga *rga = ctx->rga;
	struct rga_frame *f = rga_get_frame(ctx, vb->vb2_queue->type);
	size_t n_desc = 0;

	n_desc = DIV_ROUND_UP(f->size, PAGE_SIZE);

	rbuf->n_desc = n_desc;
	rbuf->dma_desc = dma_alloc_coherent(rga->dev,
					    rbuf->n_desc * sizeof(*rbuf->dma_desc),
					    &rbuf->dma_desc_pa, GFP_KERNEL);
	if (!rbuf->dma_desc)
		return -ENOMEM;

	return 0;
}

static int rga_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rga_vb_buffer *rbuf = vb_to_rga(vbuf);
	struct rga_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct rga_frame *f = rga_get_frame(ctx, vb->vb2_queue->type);

	if (IS_ERR(f))
		return PTR_ERR(f);

	vb2_set_plane_payload(vb, 0, f->size);

	/* Create local MMU table for RGA */
	fill_descriptors(rbuf->dma_desc, vb2_dma_sg_plane_desc(vb, 0));

	return 0;
}

static void rga_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rga_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
}

static void rga_buf_cleanup(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rga_vb_buffer *rbuf = vb_to_rga(vbuf);
	struct rga_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct rockchip_rga *rga = ctx->rga;

	dma_free_coherent(rga->dev, rbuf->n_desc * sizeof(*rbuf->dma_desc),
			  rbuf->dma_desc, rbuf->dma_desc_pa);
}

static void rga_buf_return_buffers(struct vb2_queue *q,
				   enum vb2_buffer_state state)
{
	struct rga_ctx *ctx = vb2_get_drv_priv(q);
	struct vb2_v4l2_buffer *vbuf;

	for (;;) {
		if (V4L2_TYPE_IS_OUTPUT(q->type))
			vbuf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		else
			vbuf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
		if (!vbuf)
			break;
		v4l2_m2m_buf_done(vbuf, state);
	}
}

static int rga_buf_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct rga_ctx *ctx = vb2_get_drv_priv(q);
	struct rockchip_rga *rga = ctx->rga;
	int ret;

	ret = pm_runtime_resume_and_get(rga->dev);
	if (ret < 0) {
		rga_buf_return_buffers(q, VB2_BUF_STATE_QUEUED);
		return ret;
	}

	return 0;
}

static void rga_buf_stop_streaming(struct vb2_queue *q)
{
	struct rga_ctx *ctx = vb2_get_drv_priv(q);
	struct rockchip_rga *rga = ctx->rga;

	rga_buf_return_buffers(q, VB2_BUF_STATE_ERROR);
	pm_runtime_put(rga->dev);
}

const struct vb2_ops rga_qops = {
	.queue_setup = rga_queue_setup,
	.buf_init = rga_buf_init,
	.buf_prepare = rga_buf_prepare,
	.buf_queue = rga_buf_queue,
	.buf_cleanup = rga_buf_cleanup,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.start_streaming = rga_buf_start_streaming,
	.stop_streaming = rga_buf_stop_streaming,
};
