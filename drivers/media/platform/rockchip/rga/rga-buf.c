// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 Rockchip Electronics Co., Ltd.
 * Author: Jacob Chen <jacob-chen@iotwrt.com>
 */

#include <linux/pm_runtime.h>
#include <linux/scatterlist.h>

#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-sg.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-v4l2.h>

#include "rga.h"

static ssize_t fill_descriptors(struct rga_dma_desc *desc, size_t max_desc,
				struct sg_table *sgt)
{
	struct sg_dma_page_iter iter;
	struct rga_dma_desc *tmp = desc;
	size_t n_desc = 0;
	dma_addr_t addr;

	for_each_sgtable_dma_page(sgt, &iter, 0) {
		if (n_desc > max_desc)
			return -EINVAL;
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
	const struct v4l2_pix_format_mplane *pix_fmt;
	int i;

	if (IS_ERR(f))
		return PTR_ERR(f);

	pix_fmt = &f->pix;

	if (*nplanes) {
		if (*nplanes != pix_fmt->num_planes)
			return -EINVAL;

		for (i = 0; i < pix_fmt->num_planes; i++)
			if (sizes[i] < pix_fmt->plane_fmt[i].sizeimage)
				return -EINVAL;

		return 0;
	}

	*nplanes = pix_fmt->num_planes;

	for (i = 0; i < pix_fmt->num_planes; i++)
		sizes[i] = pix_fmt->plane_fmt[i].sizeimage;

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
	u32 size = 0;
	u8 i;

	if (IS_ERR(f))
		return PTR_ERR(f);

	if (!rga_has_internal_iommu(rga))
		return 0;

	for (i = 0; i < f->pix.num_planes; i++)
		size += f->pix.plane_fmt[i].sizeimage;
	n_desc = DIV_ROUND_UP(size, PAGE_SIZE);

	rbuf->n_desc = n_desc;
	rbuf->dma_desc = dma_alloc_coherent(rga->dev,
					    rbuf->n_desc * sizeof(*rbuf->dma_desc),
					    &rbuf->dma_desc_pa, GFP_KERNEL);
	if (!rbuf->dma_desc)
		return -ENOMEM;

	return 0;
}

static int get_plane_offset(struct rga_frame *f,
			    const struct v4l2_format_info *info,
			    int plane)
{
	u32 stride = f->pix.plane_fmt[0].bytesperline;

	if (plane == 0)
		return 0;
	if (plane == 1)
		return stride * f->pix.height;
	if (plane == 2)
		return stride * f->pix.height +
		       (stride * f->pix.height / info->hdiv / info->vdiv);

	return -EINVAL;
}

static int rga_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rga_vb_buffer *rbuf = vb_to_rga(vbuf);
	struct rga_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct rga_frame *f = rga_get_frame(ctx, vb->vb2_queue->type);
	ssize_t n_desc = 0;
	size_t curr_desc = 0;
	int i;
	const struct v4l2_format_info *info;
	dma_addr_t dma_addrs[VIDEO_MAX_PLANES];

	if (IS_ERR(f))
		return PTR_ERR(f);

	if (V4L2_TYPE_IS_OUTPUT(vb->vb2_queue->type)) {
		if (vbuf->field == V4L2_FIELD_ANY)
			vbuf->field = V4L2_FIELD_NONE;
		if (vbuf->field != V4L2_FIELD_NONE)
			return -EINVAL;
	}

	for (i = 0; i < vb->num_planes; i++) {
		vb2_set_plane_payload(vb, i, f->pix.plane_fmt[i].sizeimage);

		if (rga_has_internal_iommu(ctx->rga)) {
			/* Create local MMU table for RGA */
			n_desc = fill_descriptors(&rbuf->dma_desc[curr_desc],
						  rbuf->n_desc - curr_desc,
						  vb2_dma_sg_plane_desc(vb, i));
			if (n_desc < 0) {
				v4l2_err(&ctx->rga->v4l2_dev,
					 "Failed to map video buffer to RGA\n");
				return n_desc;
			}
			dma_addrs[i] = curr_desc << PAGE_SHIFT;
			curr_desc += n_desc;
		} else {
			dma_addrs[i] = vb2_dma_contig_plane_dma_addr(vb, i);
		}
	}

	/* Fill the remaining planes */
	info = v4l2_format_info(f->pix.pixelformat);
	for (i = info->mem_planes; i < info->comp_planes; i++)
		dma_addrs[i] = dma_addrs[0] + get_plane_offset(f, info, i);

	rbuf->dma_addrs.y_addr = dma_addrs[0];
	rbuf->dma_addrs.u_addr = dma_addrs[1];
	rbuf->dma_addrs.v_addr = dma_addrs[2];

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

	if (!rga_has_internal_iommu(rga))
		return;

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

static int rga_buf_prepare_streaming(struct vb2_queue *q)
{
	struct rga_ctx *ctx = vb2_get_drv_priv(q);
	const struct rga_hw *hw = ctx->rga->hw;
	int ret;

	/* It's safe to check the streaming state of the other queue,
	 * as the streamon ioctl's can't race due to the lock set in
	 * the queue_init function.
	 */
	if ((V4L2_TYPE_IS_OUTPUT(q->type) &&
	     vb2_is_streaming(v4l2_m2m_get_dst_vq(ctx->fh.m2m_ctx))) ||
	    (V4L2_TYPE_IS_CAPTURE(q->type) &&
	     vb2_is_streaming(v4l2_m2m_get_src_vq(ctx->fh.m2m_ctx)))) {
		/*
		 * As the other side is already streaming,
		 * check that the max scaling factor isn't exceeded.
		 */
		ret = rga_check_scaling(hw, &ctx->in.crop, &ctx->out.crop,
					ctx->rotate);
		if (ret < 0)
			return ret;
	}

	return 0;
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

	if (V4L2_TYPE_IS_OUTPUT(q->type))
		ctx->osequence = 0;
	else
		ctx->csequence = 0;

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
	.prepare_streaming = rga_buf_prepare_streaming,
	.start_streaming = rga_buf_start_streaming,
	.stop_streaming = rga_buf_stop_streaming,
};
