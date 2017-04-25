/*
 * Copyright (c) 2011 - 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Samsung EXYNOS5 SoC series G-Scaler driver
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
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/list.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/clk.h>

#include <media/v4l2-ioctl.h>

#include "gsc-core.h"

static int gsc_m2m_ctx_stop_req(struct gsc_ctx *ctx)
{
	struct gsc_ctx *curr_ctx;
	struct gsc_dev *gsc = ctx->gsc_dev;
	int ret;

	curr_ctx = v4l2_m2m_get_curr_priv(gsc->m2m.m2m_dev);
	if (!gsc_m2m_pending(gsc) || (curr_ctx != ctx))
		return 0;

	gsc_ctx_state_lock_set(GSC_CTX_STOP_REQ, ctx);
	ret = wait_event_timeout(gsc->irq_queue,
			!gsc_ctx_state_is_set(GSC_CTX_STOP_REQ, ctx),
			GSC_SHUTDOWN_TIMEOUT);

	return ret == 0 ? -ETIMEDOUT : ret;
}

static void __gsc_m2m_job_abort(struct gsc_ctx *ctx)
{
	int ret;

	ret = gsc_m2m_ctx_stop_req(ctx);
	if ((ret == -ETIMEDOUT) || (ctx->state & GSC_CTX_ABORT)) {
		gsc_ctx_state_lock_clear(GSC_CTX_STOP_REQ | GSC_CTX_ABORT, ctx);
		gsc_m2m_job_finish(ctx, VB2_BUF_STATE_ERROR);
	}
}

static int gsc_m2m_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct gsc_ctx *ctx = q->drv_priv;
	int ret;

	ret = pm_runtime_get_sync(&ctx->gsc_dev->pdev->dev);
	return ret > 0 ? 0 : ret;
}

static void __gsc_m2m_cleanup_queue(struct gsc_ctx *ctx)
{
	struct vb2_v4l2_buffer *src_vb, *dst_vb;

	while (v4l2_m2m_num_src_bufs_ready(ctx->m2m_ctx) > 0) {
		src_vb = v4l2_m2m_src_buf_remove(ctx->m2m_ctx);
		v4l2_m2m_buf_done(src_vb, VB2_BUF_STATE_ERROR);
	}

	while (v4l2_m2m_num_dst_bufs_ready(ctx->m2m_ctx) > 0) {
		dst_vb = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx);
		v4l2_m2m_buf_done(dst_vb, VB2_BUF_STATE_ERROR);
	}
}

static void gsc_m2m_stop_streaming(struct vb2_queue *q)
{
	struct gsc_ctx *ctx = q->drv_priv;

	__gsc_m2m_job_abort(ctx);

	__gsc_m2m_cleanup_queue(ctx);

	pm_runtime_put(&ctx->gsc_dev->pdev->dev);
}

void gsc_m2m_job_finish(struct gsc_ctx *ctx, int vb_state)
{
	struct vb2_v4l2_buffer *src_vb, *dst_vb;

	if (!ctx || !ctx->m2m_ctx)
		return;

	src_vb = v4l2_m2m_src_buf_remove(ctx->m2m_ctx);
	dst_vb = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx);

	if (src_vb && dst_vb) {
		dst_vb->vb2_buf.timestamp = src_vb->vb2_buf.timestamp;
		dst_vb->timecode = src_vb->timecode;
		dst_vb->flags &= ~V4L2_BUF_FLAG_TSTAMP_SRC_MASK;
		dst_vb->flags |=
			src_vb->flags
			& V4L2_BUF_FLAG_TSTAMP_SRC_MASK;

		v4l2_m2m_buf_done(src_vb, vb_state);
		v4l2_m2m_buf_done(dst_vb, vb_state);

		v4l2_m2m_job_finish(ctx->gsc_dev->m2m.m2m_dev,
				    ctx->m2m_ctx);
	}
}

static void gsc_m2m_job_abort(void *priv)
{
	__gsc_m2m_job_abort((struct gsc_ctx *)priv);
}

static int gsc_get_bufs(struct gsc_ctx *ctx)
{
	struct gsc_frame *s_frame, *d_frame;
	struct vb2_v4l2_buffer *src_vb, *dst_vb;
	int ret;

	s_frame = &ctx->s_frame;
	d_frame = &ctx->d_frame;

	src_vb = v4l2_m2m_next_src_buf(ctx->m2m_ctx);
	ret = gsc_prepare_addr(ctx, &src_vb->vb2_buf, s_frame, &s_frame->addr);
	if (ret)
		return ret;

	dst_vb = v4l2_m2m_next_dst_buf(ctx->m2m_ctx);
	ret = gsc_prepare_addr(ctx, &dst_vb->vb2_buf, d_frame, &d_frame->addr);
	if (ret)
		return ret;

	dst_vb->vb2_buf.timestamp = src_vb->vb2_buf.timestamp;

	return 0;
}

static void gsc_m2m_device_run(void *priv)
{
	struct gsc_ctx *ctx = priv;
	struct gsc_dev *gsc;
	unsigned long flags;
	int ret;
	bool is_set = false;

	if (WARN(!ctx, "null hardware context\n"))
		return;

	gsc = ctx->gsc_dev;
	spin_lock_irqsave(&gsc->slock, flags);

	set_bit(ST_M2M_PEND, &gsc->state);

	/* Reconfigure hardware if the context has changed. */
	if (gsc->m2m.ctx != ctx) {
		pr_debug("gsc->m2m.ctx = 0x%p, current_ctx = 0x%p",
				gsc->m2m.ctx, ctx);
		ctx->state |= GSC_PARAMS;
		gsc->m2m.ctx = ctx;
	}

	is_set = ctx->state & GSC_CTX_STOP_REQ;
	if (is_set) {
		ctx->state &= ~GSC_CTX_STOP_REQ;
		ctx->state |= GSC_CTX_ABORT;
		wake_up(&gsc->irq_queue);
		goto put_device;
	}

	ret = gsc_get_bufs(ctx);
	if (ret) {
		pr_err("Wrong address");
		goto put_device;
	}

	gsc_set_prefbuf(gsc, &ctx->s_frame);
	gsc_hw_set_input_addr(gsc, &ctx->s_frame.addr, GSC_M2M_BUF_NUM);
	gsc_hw_set_output_addr(gsc, &ctx->d_frame.addr, GSC_M2M_BUF_NUM);

	if (ctx->state & GSC_PARAMS) {
		gsc_hw_set_input_buf_masking(gsc, GSC_M2M_BUF_NUM, false);
		gsc_hw_set_output_buf_masking(gsc, GSC_M2M_BUF_NUM, false);
		gsc_hw_set_frm_done_irq_mask(gsc, false);
		gsc_hw_set_gsc_irq_enable(gsc, true);

		if (gsc_set_scaler_info(ctx)) {
			pr_err("Scaler setup error");
			goto put_device;
		}

		gsc_hw_set_input_path(ctx);
		gsc_hw_set_in_size(ctx);
		gsc_hw_set_in_image_format(ctx);

		gsc_hw_set_output_path(ctx);
		gsc_hw_set_out_size(ctx);
		gsc_hw_set_out_image_format(ctx);

		gsc_hw_set_prescaler(ctx);
		gsc_hw_set_mainscaler(ctx);
		gsc_hw_set_rotation(ctx);
		gsc_hw_set_global_alpha(ctx);
	}

	/* update shadow registers */
	gsc_hw_set_sfr_update(ctx);

	ctx->state &= ~GSC_PARAMS;
	gsc_hw_enable_control(gsc, true);

	spin_unlock_irqrestore(&gsc->slock, flags);
	return;

put_device:
	ctx->state &= ~GSC_PARAMS;
	spin_unlock_irqrestore(&gsc->slock, flags);
}

static int gsc_m2m_queue_setup(struct vb2_queue *vq,
			unsigned int *num_buffers, unsigned int *num_planes,
			unsigned int sizes[], struct device *alloc_devs[])
{
	struct gsc_ctx *ctx = vb2_get_drv_priv(vq);
	struct gsc_frame *frame;
	int i;

	frame = ctx_get_frame(ctx, vq->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	if (!frame->fmt)
		return -EINVAL;

	*num_planes = frame->fmt->num_planes;
	for (i = 0; i < frame->fmt->num_planes; i++)
		sizes[i] = frame->payload[i];
	return 0;
}

static int gsc_m2m_buf_prepare(struct vb2_buffer *vb)
{
	struct gsc_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct gsc_frame *frame;
	int i;

	frame = ctx_get_frame(ctx, vb->vb2_queue->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	if (!V4L2_TYPE_IS_OUTPUT(vb->vb2_queue->type)) {
		for (i = 0; i < frame->fmt->num_planes; i++)
			vb2_set_plane_payload(vb, i, frame->payload[i]);
	}

	return 0;
}

static void gsc_m2m_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct gsc_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	pr_debug("ctx: %p, ctx->state: 0x%x", ctx, ctx->state);

	if (ctx->m2m_ctx)
		v4l2_m2m_buf_queue(ctx->m2m_ctx, vbuf);
}

static const struct vb2_ops gsc_m2m_qops = {
	.queue_setup	 = gsc_m2m_queue_setup,
	.buf_prepare	 = gsc_m2m_buf_prepare,
	.buf_queue	 = gsc_m2m_buf_queue,
	.wait_prepare	 = vb2_ops_wait_prepare,
	.wait_finish	 = vb2_ops_wait_finish,
	.stop_streaming	 = gsc_m2m_stop_streaming,
	.start_streaming = gsc_m2m_start_streaming,
};

static int gsc_m2m_querycap(struct file *file, void *fh,
			   struct v4l2_capability *cap)
{
	struct gsc_ctx *ctx = fh_to_ctx(fh);
	struct gsc_dev *gsc = ctx->gsc_dev;

	strlcpy(cap->driver, GSC_MODULE_NAME, sizeof(cap->driver));
	strlcpy(cap->card, GSC_MODULE_NAME " gscaler", sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s",
		 dev_name(&gsc->pdev->dev));
	cap->device_caps = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_M2M_MPLANE |
		V4L2_CAP_VIDEO_CAPTURE_MPLANE |	V4L2_CAP_VIDEO_OUTPUT_MPLANE;

	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int gsc_m2m_enum_fmt_mplane(struct file *file, void *priv,
				struct v4l2_fmtdesc *f)
{
	return gsc_enum_fmt_mplane(f);
}

static int gsc_m2m_g_fmt_mplane(struct file *file, void *fh,
			     struct v4l2_format *f)
{
	struct gsc_ctx *ctx = fh_to_ctx(fh);

	return gsc_g_fmt_mplane(ctx, f);
}

static int gsc_m2m_try_fmt_mplane(struct file *file, void *fh,
				  struct v4l2_format *f)
{
	struct gsc_ctx *ctx = fh_to_ctx(fh);

	return gsc_try_fmt_mplane(ctx, f);
}

static int gsc_m2m_s_fmt_mplane(struct file *file, void *fh,
				 struct v4l2_format *f)
{
	struct gsc_ctx *ctx = fh_to_ctx(fh);
	struct vb2_queue *vq;
	struct gsc_frame *frame;
	struct v4l2_pix_format_mplane *pix;
	int i, ret = 0;

	ret = gsc_m2m_try_fmt_mplane(file, fh, f);
	if (ret)
		return ret;

	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, f->type);

	if (vb2_is_streaming(vq)) {
		pr_err("queue (%d) busy", f->type);
		return -EBUSY;
	}

	if (V4L2_TYPE_IS_OUTPUT(f->type))
		frame = &ctx->s_frame;
	else
		frame = &ctx->d_frame;

	pix = &f->fmt.pix_mp;
	frame->fmt = find_fmt(&pix->pixelformat, NULL, 0);
	frame->colorspace = pix->colorspace;
	if (!frame->fmt)
		return -EINVAL;

	for (i = 0; i < frame->fmt->num_planes; i++)
		frame->payload[i] = pix->plane_fmt[i].sizeimage;

	gsc_set_frame_size(frame, pix->width, pix->height);

	if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		gsc_ctx_state_lock_set(GSC_PARAMS | GSC_DST_FMT, ctx);
	else
		gsc_ctx_state_lock_set(GSC_PARAMS | GSC_SRC_FMT, ctx);

	pr_debug("f_w: %d, f_h: %d", frame->f_width, frame->f_height);

	return 0;
}

static int gsc_m2m_reqbufs(struct file *file, void *fh,
			  struct v4l2_requestbuffers *reqbufs)
{
	struct gsc_ctx *ctx = fh_to_ctx(fh);
	struct gsc_dev *gsc = ctx->gsc_dev;
	u32 max_cnt;

	max_cnt = (reqbufs->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) ?
		gsc->variant->in_buf_cnt : gsc->variant->out_buf_cnt;
	if (reqbufs->count > max_cnt)
		return -EINVAL;

	return v4l2_m2m_reqbufs(file, ctx->m2m_ctx, reqbufs);
}

static int gsc_m2m_expbuf(struct file *file, void *fh,
				struct v4l2_exportbuffer *eb)
{
	struct gsc_ctx *ctx = fh_to_ctx(fh);
	return v4l2_m2m_expbuf(file, ctx->m2m_ctx, eb);
}

static int gsc_m2m_querybuf(struct file *file, void *fh,
					struct v4l2_buffer *buf)
{
	struct gsc_ctx *ctx = fh_to_ctx(fh);
	return v4l2_m2m_querybuf(file, ctx->m2m_ctx, buf);
}

static int gsc_m2m_qbuf(struct file *file, void *fh,
			  struct v4l2_buffer *buf)
{
	struct gsc_ctx *ctx = fh_to_ctx(fh);
	return v4l2_m2m_qbuf(file, ctx->m2m_ctx, buf);
}

static int gsc_m2m_dqbuf(struct file *file, void *fh,
			   struct v4l2_buffer *buf)
{
	struct gsc_ctx *ctx = fh_to_ctx(fh);
	return v4l2_m2m_dqbuf(file, ctx->m2m_ctx, buf);
}

static int gsc_m2m_streamon(struct file *file, void *fh,
			   enum v4l2_buf_type type)
{
	struct gsc_ctx *ctx = fh_to_ctx(fh);

	/* The source and target color format need to be set */
	if (V4L2_TYPE_IS_OUTPUT(type)) {
		if (!gsc_ctx_state_is_set(GSC_SRC_FMT, ctx))
			return -EINVAL;
	} else if (!gsc_ctx_state_is_set(GSC_DST_FMT, ctx)) {
		return -EINVAL;
	}

	return v4l2_m2m_streamon(file, ctx->m2m_ctx, type);
}

static int gsc_m2m_streamoff(struct file *file, void *fh,
			    enum v4l2_buf_type type)
{
	struct gsc_ctx *ctx = fh_to_ctx(fh);
	return v4l2_m2m_streamoff(file, ctx->m2m_ctx, type);
}

/* Return 1 if rectangle a is enclosed in rectangle b, or 0 otherwise. */
static int is_rectangle_enclosed(struct v4l2_rect *a, struct v4l2_rect *b)
{
	if (a->left < b->left || a->top < b->top)
		return 0;

	if (a->left + a->width > b->left + b->width)
		return 0;

	if (a->top + a->height > b->top + b->height)
		return 0;

	return 1;
}

static int gsc_m2m_g_selection(struct file *file, void *fh,
			struct v4l2_selection *s)
{
	struct gsc_frame *frame;
	struct gsc_ctx *ctx = fh_to_ctx(fh);

	if ((s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) &&
	    (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE))
		return -EINVAL;

	frame = ctx_get_frame(ctx, s->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	switch (s->target) {
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_CROP_DEFAULT:
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = frame->f_width;
		s->r.height = frame->f_height;
		return 0;

	case V4L2_SEL_TGT_COMPOSE:
	case V4L2_SEL_TGT_CROP:
		s->r.left = frame->crop.left;
		s->r.top = frame->crop.top;
		s->r.width = frame->crop.width;
		s->r.height = frame->crop.height;
		return 0;
	}

	return -EINVAL;
}

static int gsc_m2m_s_selection(struct file *file, void *fh,
				struct v4l2_selection *s)
{
	struct gsc_frame *frame;
	struct gsc_ctx *ctx = fh_to_ctx(fh);
	struct v4l2_crop cr;
	struct gsc_variant *variant = ctx->gsc_dev->variant;
	int ret;

	cr.type = s->type;
	cr.c = s->r;

	if ((s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) &&
	    (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE))
		return -EINVAL;

	ret = gsc_try_crop(ctx, &cr);
	if (ret)
		return ret;

	if (s->flags & V4L2_SEL_FLAG_LE &&
	    !is_rectangle_enclosed(&cr.c, &s->r))
		return -ERANGE;

	if (s->flags & V4L2_SEL_FLAG_GE &&
	    !is_rectangle_enclosed(&s->r, &cr.c))
		return -ERANGE;

	s->r = cr.c;

	switch (s->target) {
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE:
		frame = &ctx->s_frame;
		break;

	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_CROP:
	case V4L2_SEL_TGT_CROP_DEFAULT:
		frame = &ctx->d_frame;
		break;

	default:
		return -EINVAL;
	}

	/* Check to see if scaling ratio is within supported range */
	if (gsc_ctx_state_is_set(GSC_DST_FMT | GSC_SRC_FMT, ctx)) {
		if (s->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
			ret = gsc_check_scaler_ratio(variant, cr.c.width,
				cr.c.height, ctx->d_frame.crop.width,
				ctx->d_frame.crop.height,
				ctx->gsc_ctrls.rotate->val, ctx->out_path);
		} else {
			ret = gsc_check_scaler_ratio(variant,
				ctx->s_frame.crop.width,
				ctx->s_frame.crop.height, cr.c.width,
				cr.c.height, ctx->gsc_ctrls.rotate->val,
				ctx->out_path);
		}

		if (ret) {
			pr_err("Out of scaler range");
			return -EINVAL;
		}
	}

	frame->crop = cr.c;

	gsc_ctx_state_lock_set(GSC_PARAMS, ctx);
	return 0;
}

static const struct v4l2_ioctl_ops gsc_m2m_ioctl_ops = {
	.vidioc_querycap		= gsc_m2m_querycap,
	.vidioc_enum_fmt_vid_cap_mplane	= gsc_m2m_enum_fmt_mplane,
	.vidioc_enum_fmt_vid_out_mplane	= gsc_m2m_enum_fmt_mplane,
	.vidioc_g_fmt_vid_cap_mplane	= gsc_m2m_g_fmt_mplane,
	.vidioc_g_fmt_vid_out_mplane	= gsc_m2m_g_fmt_mplane,
	.vidioc_try_fmt_vid_cap_mplane	= gsc_m2m_try_fmt_mplane,
	.vidioc_try_fmt_vid_out_mplane	= gsc_m2m_try_fmt_mplane,
	.vidioc_s_fmt_vid_cap_mplane	= gsc_m2m_s_fmt_mplane,
	.vidioc_s_fmt_vid_out_mplane	= gsc_m2m_s_fmt_mplane,
	.vidioc_reqbufs			= gsc_m2m_reqbufs,
	.vidioc_expbuf                  = gsc_m2m_expbuf,
	.vidioc_querybuf		= gsc_m2m_querybuf,
	.vidioc_qbuf			= gsc_m2m_qbuf,
	.vidioc_dqbuf			= gsc_m2m_dqbuf,
	.vidioc_streamon		= gsc_m2m_streamon,
	.vidioc_streamoff		= gsc_m2m_streamoff,
	.vidioc_g_selection		= gsc_m2m_g_selection,
	.vidioc_s_selection		= gsc_m2m_s_selection
};

static int queue_init(void *priv, struct vb2_queue *src_vq,
			struct vb2_queue *dst_vq)
{
	struct gsc_ctx *ctx = priv;
	int ret;

	memset(src_vq, 0, sizeof(*src_vq));
	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	src_vq->drv_priv = ctx;
	src_vq->ops = &gsc_m2m_qops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &ctx->gsc_dev->lock;
	src_vq->dev = &ctx->gsc_dev->pdev->dev;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	memset(dst_vq, 0, sizeof(*dst_vq));
	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->ops = &gsc_m2m_qops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &ctx->gsc_dev->lock;
	dst_vq->dev = &ctx->gsc_dev->pdev->dev;

	return vb2_queue_init(dst_vq);
}

static int gsc_m2m_open(struct file *file)
{
	struct gsc_dev *gsc = video_drvdata(file);
	struct gsc_ctx *ctx = NULL;
	int ret;

	pr_debug("pid: %d, state: 0x%lx", task_pid_nr(current), gsc->state);

	if (mutex_lock_interruptible(&gsc->lock))
		return -ERESTARTSYS;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		ret = -ENOMEM;
		goto unlock;
	}

	v4l2_fh_init(&ctx->fh, gsc->m2m.vfd);
	ret = gsc_ctrls_create(ctx);
	if (ret)
		goto error_fh;

	/* Use separate control handler per file handle */
	ctx->fh.ctrl_handler = &ctx->ctrl_handler;
	file->private_data = &ctx->fh;
	v4l2_fh_add(&ctx->fh);

	ctx->gsc_dev = gsc;
	/* Default color format */
	ctx->s_frame.fmt = get_format(0);
	ctx->d_frame.fmt = get_format(0);
	/* Setup the device context for mem2mem mode. */
	ctx->state = GSC_CTX_M2M;
	ctx->flags = 0;
	ctx->in_path = GSC_DMA;
	ctx->out_path = GSC_DMA;

	ctx->m2m_ctx = v4l2_m2m_ctx_init(gsc->m2m.m2m_dev, ctx, queue_init);
	if (IS_ERR(ctx->m2m_ctx)) {
		pr_err("Failed to initialize m2m context");
		ret = PTR_ERR(ctx->m2m_ctx);
		goto error_ctrls;
	}

	if (gsc->m2m.refcnt++ == 0)
		set_bit(ST_M2M_OPEN, &gsc->state);

	pr_debug("gsc m2m driver is opened, ctx(0x%p)", ctx);

	mutex_unlock(&gsc->lock);
	return 0;

error_ctrls:
	gsc_ctrls_delete(ctx);
	v4l2_fh_del(&ctx->fh);
error_fh:
	v4l2_fh_exit(&ctx->fh);
	kfree(ctx);
unlock:
	mutex_unlock(&gsc->lock);
	return ret;
}

static int gsc_m2m_release(struct file *file)
{
	struct gsc_ctx *ctx = fh_to_ctx(file->private_data);
	struct gsc_dev *gsc = ctx->gsc_dev;

	pr_debug("pid: %d, state: 0x%lx, refcnt= %d",
		task_pid_nr(current), gsc->state, gsc->m2m.refcnt);

	mutex_lock(&gsc->lock);

	v4l2_m2m_ctx_release(ctx->m2m_ctx);
	gsc_ctrls_delete(ctx);
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);

	if (--gsc->m2m.refcnt <= 0)
		clear_bit(ST_M2M_OPEN, &gsc->state);
	kfree(ctx);

	mutex_unlock(&gsc->lock);
	return 0;
}

static unsigned int gsc_m2m_poll(struct file *file,
					struct poll_table_struct *wait)
{
	struct gsc_ctx *ctx = fh_to_ctx(file->private_data);
	struct gsc_dev *gsc = ctx->gsc_dev;
	unsigned int ret;

	if (mutex_lock_interruptible(&gsc->lock))
		return -ERESTARTSYS;

	ret = v4l2_m2m_poll(file, ctx->m2m_ctx, wait);
	mutex_unlock(&gsc->lock);

	return ret;
}

static int gsc_m2m_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct gsc_ctx *ctx = fh_to_ctx(file->private_data);
	struct gsc_dev *gsc = ctx->gsc_dev;
	int ret;

	if (mutex_lock_interruptible(&gsc->lock))
		return -ERESTARTSYS;

	ret = v4l2_m2m_mmap(file, ctx->m2m_ctx, vma);
	mutex_unlock(&gsc->lock);

	return ret;
}

static const struct v4l2_file_operations gsc_m2m_fops = {
	.owner		= THIS_MODULE,
	.open		= gsc_m2m_open,
	.release	= gsc_m2m_release,
	.poll		= gsc_m2m_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= gsc_m2m_mmap,
};

static struct v4l2_m2m_ops gsc_m2m_ops = {
	.device_run	= gsc_m2m_device_run,
	.job_abort	= gsc_m2m_job_abort,
};

int gsc_register_m2m_device(struct gsc_dev *gsc)
{
	struct platform_device *pdev;
	int ret;

	if (!gsc)
		return -ENODEV;

	pdev = gsc->pdev;

	gsc->vdev.fops		= &gsc_m2m_fops;
	gsc->vdev.ioctl_ops	= &gsc_m2m_ioctl_ops;
	gsc->vdev.release	= video_device_release_empty;
	gsc->vdev.lock		= &gsc->lock;
	gsc->vdev.vfl_dir	= VFL_DIR_M2M;
	gsc->vdev.v4l2_dev	= &gsc->v4l2_dev;
	snprintf(gsc->vdev.name, sizeof(gsc->vdev.name), "%s.%d:m2m",
					GSC_MODULE_NAME, gsc->id);

	video_set_drvdata(&gsc->vdev, gsc);

	gsc->m2m.vfd = &gsc->vdev;
	gsc->m2m.m2m_dev = v4l2_m2m_init(&gsc_m2m_ops);
	if (IS_ERR(gsc->m2m.m2m_dev)) {
		dev_err(&pdev->dev, "failed to initialize v4l2-m2m device\n");
		return PTR_ERR(gsc->m2m.m2m_dev);
	}

	ret = video_register_device(&gsc->vdev, VFL_TYPE_GRABBER, -1);
	if (ret) {
		dev_err(&pdev->dev,
			 "%s(): failed to register video device\n", __func__);
		goto err_m2m_release;
	}

	pr_debug("gsc m2m driver registered as /dev/video%d", gsc->vdev.num);
	return 0;

err_m2m_release:
	v4l2_m2m_release(gsc->m2m.m2m_dev);

	return ret;
}

void gsc_unregister_m2m_device(struct gsc_dev *gsc)
{
	if (gsc) {
		v4l2_m2m_release(gsc->m2m.m2m_dev);
		video_unregister_device(&gsc->vdev);
	}
}
