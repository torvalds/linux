/*
 * Samsung S5P/EXYNOS4 SoC series FIMC (video postprocessor) driver
 *
 * Copyright (C) 2012 - 2013 Samsung Electronics Co., Ltd.
 * Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 2 of the License,
 * or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/bug.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/list.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>

#include "common.h"
#include "fimc-core.h"
#include "fimc-reg.h"
#include "media-dev.h"

static unsigned int get_m2m_fmt_flags(unsigned int stream_type)
{
	if (stream_type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return FMT_FLAGS_M2M_IN;
	else
		return FMT_FLAGS_M2M_OUT;
}

void fimc_m2m_job_finish(struct fimc_ctx *ctx, int vb_state)
{
	struct vb2_v4l2_buffer *src_vb, *dst_vb;

	if (!ctx || !ctx->fh.m2m_ctx)
		return;

	src_vb = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	dst_vb = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);

	if (src_vb)
		v4l2_m2m_buf_done(src_vb, vb_state);
	if (dst_vb)
		v4l2_m2m_buf_done(dst_vb, vb_state);
	if (src_vb && dst_vb)
		v4l2_m2m_job_finish(ctx->fimc_dev->m2m.m2m_dev,
				    ctx->fh.m2m_ctx);
}

/* Complete the transaction which has been scheduled for execution. */
static void fimc_m2m_shutdown(struct fimc_ctx *ctx)
{
	struct fimc_dev *fimc = ctx->fimc_dev;

	if (!fimc_m2m_pending(fimc))
		return;

	fimc_ctx_state_set(FIMC_CTX_SHUT, ctx);

	wait_event_timeout(fimc->irq_queue,
			!fimc_ctx_state_is_set(FIMC_CTX_SHUT, ctx),
			FIMC_SHUTDOWN_TIMEOUT);
}

static int start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct fimc_ctx *ctx = q->drv_priv;
	int ret;

	ret = pm_runtime_get_sync(&ctx->fimc_dev->pdev->dev);
	return ret > 0 ? 0 : ret;
}

static void stop_streaming(struct vb2_queue *q)
{
	struct fimc_ctx *ctx = q->drv_priv;


	fimc_m2m_shutdown(ctx);
	fimc_m2m_job_finish(ctx, VB2_BUF_STATE_ERROR);
	pm_runtime_put(&ctx->fimc_dev->pdev->dev);
}

static void fimc_device_run(void *priv)
{
	struct vb2_v4l2_buffer *src_vb, *dst_vb;
	struct fimc_ctx *ctx = priv;
	struct fimc_frame *sf, *df;
	struct fimc_dev *fimc;
	unsigned long flags;
	int ret;

	if (WARN(!ctx, "Null context\n"))
		return;

	fimc = ctx->fimc_dev;
	spin_lock_irqsave(&fimc->slock, flags);

	set_bit(ST_M2M_PEND, &fimc->state);
	sf = &ctx->s_frame;
	df = &ctx->d_frame;

	if (ctx->state & FIMC_PARAMS) {
		/* Prepare the DMA offsets for scaler */
		fimc_prepare_dma_offset(ctx, sf);
		fimc_prepare_dma_offset(ctx, df);
	}

	src_vb = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	ret = fimc_prepare_addr(ctx, &src_vb->vb2_buf, sf, &sf->paddr);
	if (ret)
		goto dma_unlock;

	dst_vb = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
	ret = fimc_prepare_addr(ctx, &dst_vb->vb2_buf, df, &df->paddr);
	if (ret)
		goto dma_unlock;

	dst_vb->vb2_buf.timestamp = src_vb->vb2_buf.timestamp;
	dst_vb->flags &= ~V4L2_BUF_FLAG_TSTAMP_SRC_MASK;
	dst_vb->flags |=
		src_vb->flags & V4L2_BUF_FLAG_TSTAMP_SRC_MASK;

	/* Reconfigure hardware if the context has changed. */
	if (fimc->m2m.ctx != ctx) {
		ctx->state |= FIMC_PARAMS;
		fimc->m2m.ctx = ctx;
	}

	if (ctx->state & FIMC_PARAMS) {
		fimc_set_yuv_order(ctx);
		fimc_hw_set_input_path(ctx);
		fimc_hw_set_in_dma(ctx);
		ret = fimc_set_scaler_info(ctx);
		if (ret)
			goto dma_unlock;
		fimc_hw_set_prescaler(ctx);
		fimc_hw_set_mainscaler(ctx);
		fimc_hw_set_target_format(ctx);
		fimc_hw_set_rotation(ctx);
		fimc_hw_set_effect(ctx);
		fimc_hw_set_out_dma(ctx);
		if (fimc->drv_data->alpha_color)
			fimc_hw_set_rgb_alpha(ctx);
		fimc_hw_set_output_path(ctx);
	}
	fimc_hw_set_input_addr(fimc, &sf->paddr);
	fimc_hw_set_output_addr(fimc, &df->paddr, -1);

	fimc_activate_capture(ctx);
	ctx->state &= (FIMC_CTX_M2M | FIMC_CTX_CAP);
	fimc_hw_activate_input_dma(fimc, true);

dma_unlock:
	spin_unlock_irqrestore(&fimc->slock, flags);
}

static void fimc_job_abort(void *priv)
{
	fimc_m2m_shutdown(priv);
}

static int fimc_queue_setup(struct vb2_queue *vq,
			    unsigned int *num_buffers, unsigned int *num_planes,
			    unsigned int sizes[], struct device *alloc_devs[])
{
	struct fimc_ctx *ctx = vb2_get_drv_priv(vq);
	struct fimc_frame *f;
	int i;

	f = ctx_get_frame(ctx, vq->type);
	if (IS_ERR(f))
		return PTR_ERR(f);
	/*
	 * Return number of non-contiguous planes (plane buffers)
	 * depending on the configured color format.
	 */
	if (!f->fmt)
		return -EINVAL;

	*num_planes = f->fmt->memplanes;
	for (i = 0; i < f->fmt->memplanes; i++)
		sizes[i] = f->payload[i];
	return 0;
}

static int fimc_buf_prepare(struct vb2_buffer *vb)
{
	struct fimc_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct fimc_frame *frame;
	int i;

	frame = ctx_get_frame(ctx, vb->vb2_queue->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	for (i = 0; i < frame->fmt->memplanes; i++)
		vb2_set_plane_payload(vb, i, frame->payload[i]);

	return 0;
}

static void fimc_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct fimc_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
}

static const struct vb2_ops fimc_qops = {
	.queue_setup	 = fimc_queue_setup,
	.buf_prepare	 = fimc_buf_prepare,
	.buf_queue	 = fimc_buf_queue,
	.wait_prepare	 = vb2_ops_wait_prepare,
	.wait_finish	 = vb2_ops_wait_finish,
	.stop_streaming	 = stop_streaming,
	.start_streaming = start_streaming,
};

/*
 * V4L2 ioctl handlers
 */
static int fimc_m2m_querycap(struct file *file, void *fh,
				     struct v4l2_capability *cap)
{
	struct fimc_dev *fimc = video_drvdata(file);
	unsigned int caps = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_M2M_MPLANE;

	__fimc_vidioc_querycap(&fimc->pdev->dev, cap, caps);
	return 0;
}

static int fimc_m2m_enum_fmt_mplane(struct file *file, void *priv,
				    struct v4l2_fmtdesc *f)
{
	struct fimc_fmt *fmt;

	fmt = fimc_find_format(NULL, NULL, get_m2m_fmt_flags(f->type),
			       f->index);
	if (!fmt)
		return -EINVAL;

	strncpy(f->description, fmt->name, sizeof(f->description) - 1);
	f->pixelformat = fmt->fourcc;
	return 0;
}

static int fimc_m2m_g_fmt_mplane(struct file *file, void *fh,
				 struct v4l2_format *f)
{
	struct fimc_ctx *ctx = fh_to_ctx(fh);
	struct fimc_frame *frame = ctx_get_frame(ctx, f->type);

	if (IS_ERR(frame))
		return PTR_ERR(frame);

	__fimc_get_format(frame, f);
	return 0;
}

static int fimc_try_fmt_mplane(struct fimc_ctx *ctx, struct v4l2_format *f)
{
	struct fimc_dev *fimc = ctx->fimc_dev;
	const struct fimc_variant *variant = fimc->variant;
	struct v4l2_pix_format_mplane *pix = &f->fmt.pix_mp;
	struct fimc_fmt *fmt;
	u32 max_w, mod_x, mod_y;

	if (!IS_M2M(f->type))
		return -EINVAL;

	fmt = fimc_find_format(&pix->pixelformat, NULL,
			       get_m2m_fmt_flags(f->type), 0);
	if (WARN(fmt == NULL, "Pixel format lookup failed"))
		return -EINVAL;

	if (pix->field == V4L2_FIELD_ANY)
		pix->field = V4L2_FIELD_NONE;
	else if (pix->field != V4L2_FIELD_NONE)
		return -EINVAL;

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		max_w = variant->pix_limit->scaler_dis_w;
		mod_x = ffs(variant->min_inp_pixsize) - 1;
	} else {
		max_w = variant->pix_limit->out_rot_dis_w;
		mod_x = ffs(variant->min_out_pixsize) - 1;
	}

	if (tiled_fmt(fmt)) {
		mod_x = 6; /* 64 x 32 pixels tile */
		mod_y = 5;
	} else {
		if (variant->min_vsize_align == 1)
			mod_y = fimc_fmt_is_rgb(fmt->color) ? 0 : 1;
		else
			mod_y = ffs(variant->min_vsize_align) - 1;
	}

	v4l_bound_align_image(&pix->width, 16, max_w, mod_x,
		&pix->height, 8, variant->pix_limit->scaler_dis_w, mod_y, 0);

	fimc_adjust_mplane_format(fmt, pix->width, pix->height, &f->fmt.pix_mp);
	return 0;
}

static int fimc_m2m_try_fmt_mplane(struct file *file, void *fh,
				   struct v4l2_format *f)
{
	struct fimc_ctx *ctx = fh_to_ctx(fh);
	return fimc_try_fmt_mplane(ctx, f);
}

static void __set_frame_format(struct fimc_frame *frame, struct fimc_fmt *fmt,
			       struct v4l2_pix_format_mplane *pixm)
{
	int i;

	for (i = 0; i < fmt->memplanes; i++) {
		frame->bytesperline[i] = pixm->plane_fmt[i].bytesperline;
		frame->payload[i] = pixm->plane_fmt[i].sizeimage;
	}

	frame->f_width = pixm->width;
	frame->f_height	= pixm->height;
	frame->o_width = pixm->width;
	frame->o_height = pixm->height;
	frame->width = pixm->width;
	frame->height = pixm->height;
	frame->offs_h = 0;
	frame->offs_v = 0;
	frame->fmt = fmt;
}

static int fimc_m2m_s_fmt_mplane(struct file *file, void *fh,
				 struct v4l2_format *f)
{
	struct fimc_ctx *ctx = fh_to_ctx(fh);
	struct fimc_dev *fimc = ctx->fimc_dev;
	struct fimc_fmt *fmt;
	struct vb2_queue *vq;
	struct fimc_frame *frame;
	int ret;

	ret = fimc_try_fmt_mplane(ctx, f);
	if (ret)
		return ret;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);

	if (vb2_is_busy(vq)) {
		v4l2_err(&fimc->m2m.vfd, "queue (%d) busy\n", f->type);
		return -EBUSY;
	}

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		frame = &ctx->s_frame;
	else
		frame = &ctx->d_frame;

	fmt = fimc_find_format(&f->fmt.pix_mp.pixelformat, NULL,
			       get_m2m_fmt_flags(f->type), 0);
	if (!fmt)
		return -EINVAL;

	__set_frame_format(frame, fmt, &f->fmt.pix_mp);

	/* Update RGB Alpha control state and value range */
	fimc_alpha_ctrl_update(ctx);

	return 0;
}

static int fimc_m2m_g_selection(struct file *file, void *fh,
				struct v4l2_selection *s)
{
	struct fimc_ctx *ctx = fh_to_ctx(fh);
	struct fimc_frame *frame;

	frame = ctx_get_frame(ctx, s->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	switch (s->target) {
	case V4L2_SEL_TGT_CROP:
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
			return -EINVAL;
		break;
	case V4L2_SEL_TGT_COMPOSE:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	switch (s->target) {
	case V4L2_SEL_TGT_CROP:
	case V4L2_SEL_TGT_COMPOSE:
		s->r.left = frame->offs_h;
		s->r.top = frame->offs_v;
		s->r.width = frame->width;
		s->r.height = frame->height;
		break;
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = frame->o_width;
		s->r.height = frame->o_height;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int fimc_m2m_try_selection(struct fimc_ctx *ctx,
				  struct v4l2_selection *s)
{
	struct fimc_dev *fimc = ctx->fimc_dev;
	struct fimc_frame *f;
	u32 min_size, halign, depth = 0;
	int i;

	if (s->r.top < 0 || s->r.left < 0) {
		v4l2_err(&fimc->m2m.vfd,
			"doesn't support negative values for top & left\n");
		return -EINVAL;
	}
	if (s->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		f = &ctx->d_frame;
		if (s->target != V4L2_SEL_TGT_COMPOSE)
			return -EINVAL;
	} else if (s->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		f = &ctx->s_frame;
		if (s->target != V4L2_SEL_TGT_CROP)
			return -EINVAL;
	} else {
		return -EINVAL;
	}

	min_size = (f == &ctx->s_frame) ?
		fimc->variant->min_inp_pixsize : fimc->variant->min_out_pixsize;

	/* Get pixel alignment constraints. */
	if (fimc->variant->min_vsize_align == 1)
		halign = fimc_fmt_is_rgb(f->fmt->color) ? 0 : 1;
	else
		halign = ffs(fimc->variant->min_vsize_align) - 1;

	for (i = 0; i < f->fmt->memplanes; i++)
		depth += f->fmt->depth[i];

	v4l_bound_align_image(&s->r.width, min_size, f->o_width,
			      ffs(min_size) - 1,
			      &s->r.height, min_size, f->o_height,
			      halign, 64/(ALIGN(depth, 8)));

	/* adjust left/top if cropping rectangle is out of bounds */
	if (s->r.left + s->r.width > f->o_width)
		s->r.left = f->o_width - s->r.width;
	if (s->r.top + s->r.height > f->o_height)
		s->r.top = f->o_height - s->r.height;

	s->r.left = round_down(s->r.left, min_size);
	s->r.top  = round_down(s->r.top, fimc->variant->hor_offs_align);

	dbg("l:%d, t:%d, w:%d, h:%d, f_w: %d, f_h: %d",
	    s->r.left, s->r.top, s->r.width, s->r.height,
	    f->f_width, f->f_height);

	return 0;
}

static int fimc_m2m_s_selection(struct file *file, void *fh,
				struct v4l2_selection *s)
{
	struct fimc_ctx *ctx = fh_to_ctx(fh);
	struct fimc_dev *fimc = ctx->fimc_dev;
	struct fimc_frame *f;
	int ret;

	ret = fimc_m2m_try_selection(ctx, s);
	if (ret)
		return ret;

	f = (s->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) ?
		&ctx->s_frame : &ctx->d_frame;

	/* Check to see if scaling ratio is within supported range */
	if (s->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		ret = fimc_check_scaler_ratio(ctx, s->r.width,
				s->r.height, ctx->d_frame.width,
				ctx->d_frame.height, ctx->rotation);
	} else {
		ret = fimc_check_scaler_ratio(ctx, ctx->s_frame.width,
				ctx->s_frame.height, s->r.width,
				s->r.height, ctx->rotation);
	}
	if (ret) {
		v4l2_err(&fimc->m2m.vfd, "Out of scaler range\n");
		return -EINVAL;
	}

	f->offs_h = s->r.left;
	f->offs_v = s->r.top;
	f->width  = s->r.width;
	f->height = s->r.height;

	fimc_ctx_state_set(FIMC_PARAMS, ctx);

	return 0;
}

static const struct v4l2_ioctl_ops fimc_m2m_ioctl_ops = {
	.vidioc_querycap		= fimc_m2m_querycap,
	.vidioc_enum_fmt_vid_cap_mplane	= fimc_m2m_enum_fmt_mplane,
	.vidioc_enum_fmt_vid_out_mplane	= fimc_m2m_enum_fmt_mplane,
	.vidioc_g_fmt_vid_cap_mplane	= fimc_m2m_g_fmt_mplane,
	.vidioc_g_fmt_vid_out_mplane	= fimc_m2m_g_fmt_mplane,
	.vidioc_try_fmt_vid_cap_mplane	= fimc_m2m_try_fmt_mplane,
	.vidioc_try_fmt_vid_out_mplane	= fimc_m2m_try_fmt_mplane,
	.vidioc_s_fmt_vid_cap_mplane	= fimc_m2m_s_fmt_mplane,
	.vidioc_s_fmt_vid_out_mplane	= fimc_m2m_s_fmt_mplane,
	.vidioc_reqbufs			= v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf		= v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf			= v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf			= v4l2_m2m_ioctl_dqbuf,
	.vidioc_expbuf			= v4l2_m2m_ioctl_expbuf,
	.vidioc_streamon		= v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff		= v4l2_m2m_ioctl_streamoff,
	.vidioc_g_selection		= fimc_m2m_g_selection,
	.vidioc_s_selection		= fimc_m2m_s_selection,

};

static int queue_init(void *priv, struct vb2_queue *src_vq,
		      struct vb2_queue *dst_vq)
{
	struct fimc_ctx *ctx = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	src_vq->drv_priv = ctx;
	src_vq->ops = &fimc_qops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &ctx->fimc_dev->lock;
	src_vq->dev = &ctx->fimc_dev->pdev->dev;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->ops = &fimc_qops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &ctx->fimc_dev->lock;
	dst_vq->dev = &ctx->fimc_dev->pdev->dev;

	return vb2_queue_init(dst_vq);
}

static int fimc_m2m_set_default_format(struct fimc_ctx *ctx)
{
	struct v4l2_pix_format_mplane pixm = {
		.pixelformat	= V4L2_PIX_FMT_RGB32,
		.width		= 800,
		.height		= 600,
		.plane_fmt[0]	= {
			.bytesperline = 800 * 4,
			.sizeimage = 800 * 4 * 600,
		},
	};
	struct fimc_fmt *fmt;

	fmt = fimc_find_format(&pixm.pixelformat, NULL, FMT_FLAGS_M2M, 0);
	if (!fmt)
		return -EINVAL;

	__set_frame_format(&ctx->s_frame, fmt, &pixm);
	__set_frame_format(&ctx->d_frame, fmt, &pixm);

	return 0;
}

static int fimc_m2m_open(struct file *file)
{
	struct fimc_dev *fimc = video_drvdata(file);
	struct fimc_ctx *ctx;
	int ret = -EBUSY;

	pr_debug("pid: %d, state: %#lx\n", task_pid_nr(current), fimc->state);

	if (mutex_lock_interruptible(&fimc->lock))
		return -ERESTARTSYS;
	/*
	 * Don't allow simultaneous open() of the mem-to-mem and the
	 * capture video node that belong to same FIMC IP instance.
	 */
	if (test_bit(ST_CAPT_BUSY, &fimc->state))
		goto unlock;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		ret = -ENOMEM;
		goto unlock;
	}
	v4l2_fh_init(&ctx->fh, &fimc->m2m.vfd);
	ctx->fimc_dev = fimc;

	/* Default color format */
	ctx->s_frame.fmt = fimc_get_format(0);
	ctx->d_frame.fmt = fimc_get_format(0);

	ret = fimc_ctrls_create(ctx);
	if (ret)
		goto error_fh;

	/* Use separate control handler per file handle */
	ctx->fh.ctrl_handler = &ctx->ctrls.handler;
	file->private_data = &ctx->fh;
	v4l2_fh_add(&ctx->fh);

	/* Setup the device context for memory-to-memory mode */
	ctx->state = FIMC_CTX_M2M;
	ctx->flags = 0;
	ctx->in_path = FIMC_IO_DMA;
	ctx->out_path = FIMC_IO_DMA;
	ctx->scaler.enabled = 1;

	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(fimc->m2m.m2m_dev, ctx, queue_init);
	if (IS_ERR(ctx->fh.m2m_ctx)) {
		ret = PTR_ERR(ctx->fh.m2m_ctx);
		goto error_c;
	}

	if (fimc->m2m.refcnt++ == 0)
		set_bit(ST_M2M_RUN, &fimc->state);

	ret = fimc_m2m_set_default_format(ctx);
	if (ret < 0)
		goto error_m2m_ctx;

	mutex_unlock(&fimc->lock);
	return 0;

error_m2m_ctx:
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
error_c:
	fimc_ctrls_delete(ctx);
	v4l2_fh_del(&ctx->fh);
error_fh:
	v4l2_fh_exit(&ctx->fh);
	kfree(ctx);
unlock:
	mutex_unlock(&fimc->lock);
	return ret;
}

static int fimc_m2m_release(struct file *file)
{
	struct fimc_ctx *ctx = fh_to_ctx(file->private_data);
	struct fimc_dev *fimc = ctx->fimc_dev;

	dbg("pid: %d, state: 0x%lx, refcnt= %d",
		task_pid_nr(current), fimc->state, fimc->m2m.refcnt);

	mutex_lock(&fimc->lock);

	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
	fimc_ctrls_delete(ctx);
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);

	if (--fimc->m2m.refcnt <= 0)
		clear_bit(ST_M2M_RUN, &fimc->state);
	kfree(ctx);

	mutex_unlock(&fimc->lock);
	return 0;
}

static const struct v4l2_file_operations fimc_m2m_fops = {
	.owner		= THIS_MODULE,
	.open		= fimc_m2m_open,
	.release	= fimc_m2m_release,
	.poll		= v4l2_m2m_fop_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= v4l2_m2m_fop_mmap,
};

static const struct v4l2_m2m_ops m2m_ops = {
	.device_run	= fimc_device_run,
	.job_abort	= fimc_job_abort,
};

int fimc_register_m2m_device(struct fimc_dev *fimc,
			     struct v4l2_device *v4l2_dev)
{
	struct video_device *vfd = &fimc->m2m.vfd;
	int ret;

	fimc->v4l2_dev = v4l2_dev;

	memset(vfd, 0, sizeof(*vfd));
	vfd->fops = &fimc_m2m_fops;
	vfd->ioctl_ops = &fimc_m2m_ioctl_ops;
	vfd->v4l2_dev = v4l2_dev;
	vfd->minor = -1;
	vfd->release = video_device_release_empty;
	vfd->lock = &fimc->lock;
	vfd->vfl_dir = VFL_DIR_M2M;
	set_bit(V4L2_FL_QUIRK_INVERTED_CROP, &vfd->flags);

	snprintf(vfd->name, sizeof(vfd->name), "fimc.%d.m2m", fimc->id);
	video_set_drvdata(vfd, fimc);

	fimc->m2m.m2m_dev = v4l2_m2m_init(&m2m_ops);
	if (IS_ERR(fimc->m2m.m2m_dev)) {
		v4l2_err(v4l2_dev, "failed to initialize v4l2-m2m device\n");
		return PTR_ERR(fimc->m2m.m2m_dev);
	}

	ret = media_entity_pads_init(&vfd->entity, 0, NULL);
	if (ret)
		goto err_me;

	ret = video_register_device(vfd, VFL_TYPE_GRABBER, -1);
	if (ret)
		goto err_vd;

	v4l2_info(v4l2_dev, "Registered %s as /dev/%s\n",
		  vfd->name, video_device_node_name(vfd));
	return 0;

err_vd:
	media_entity_cleanup(&vfd->entity);
err_me:
	v4l2_m2m_release(fimc->m2m.m2m_dev);
	return ret;
}

void fimc_unregister_m2m_device(struct fimc_dev *fimc)
{
	if (!fimc)
		return;

	if (fimc->m2m.m2m_dev)
		v4l2_m2m_release(fimc->m2m.m2m_dev);

	if (video_is_registered(&fimc->m2m.vfd)) {
		video_unregister_device(&fimc->m2m.vfd);
		media_entity_cleanup(&fimc->m2m.vfd.entity);
	}
}
