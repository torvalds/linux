/* linux/drivers/media/video/exynos/gsc/gsc-m2m.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Samsung EXYNOS5 SoC series G-scaler driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 2 of the License,
 * or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
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

static int gsc_ctx_stop_req(struct gsc_ctx *ctx)
{
	struct gsc_ctx *curr_ctx;
	struct gsc_dev *gsc = ctx->gsc_dev;
	int ret = 0;

	curr_ctx = v4l2_m2m_get_curr_priv(gsc->m2m.m2m_dev);
	if (!gsc_m2m_run(gsc) || (curr_ctx != ctx))
		return 0;
	ctx->state |= GSC_CTX_STOP_REQ;
	ret = wait_event_timeout(gsc->irq_queue,
			!gsc_ctx_state_is_set(GSC_CTX_STOP_REQ, ctx),
			GSC_SHUTDOWN_TIMEOUT);
	if (!ret)
		ret = -EBUSY;

	return ret;
}

static int gsc_m2m_stop_streaming(struct vb2_queue *q)
{
	struct gsc_ctx *ctx = q->drv_priv;
	struct gsc_dev *gsc = ctx->gsc_dev;
	int ret;

	ret = gsc_ctx_stop_req(ctx);
	/* FIXME: need to add v4l2_m2m_job_finish(fail) if ret is timeout */
	if (ret < 0)
		dev_err(&gsc->pdev->dev, "wait timeout : %s\n", __func__);

	v4l2_m2m_get_next_job(gsc->m2m.m2m_dev, ctx->m2m_ctx);

	return 0;
}

static void gsc_m2m_job_abort(void *priv)
{
	struct gsc_ctx *ctx = priv;
	struct gsc_dev *gsc = ctx->gsc_dev;
	int ret;

	ret = gsc_ctx_stop_req(ctx);
	/* FIXME: need to add v4l2_m2m_job_finish(fail) if ret is timeout */
	if (ret < 0)
		dev_err(&gsc->pdev->dev, "wait timeout : %s\n", __func__);

	v4l2_m2m_get_next_job(gsc->m2m.m2m_dev, ctx->m2m_ctx);
}

int gsc_fill_addr(struct gsc_ctx *ctx)
{
	struct gsc_frame *s_frame, *d_frame;
	struct vb2_buffer *vb = NULL;
	int ret = 0;

	s_frame = &ctx->s_frame;
	d_frame = &ctx->d_frame;

	vb = v4l2_m2m_next_src_buf(ctx->m2m_ctx);
	ret = gsc_prepare_addr(ctx, vb, s_frame, &s_frame->addr);
	if (ret)
		return ret;

	vb = v4l2_m2m_next_dst_buf(ctx->m2m_ctx);
	ret = gsc_prepare_addr(ctx, vb, d_frame, &d_frame->addr);

	return ret;
}

static void gsc_m2m_device_run(void *priv)
{
	struct gsc_ctx *ctx = priv;
	struct gsc_dev *gsc;
	unsigned long flags;
	u32 ret;
	bool is_set = false;

	if (WARN(!ctx, "null hardware context\n"))
		return;

	gsc = ctx->gsc_dev;
	pm_runtime_get_sync(&gsc->pdev->dev);

	spin_lock_irqsave(&ctx->slock, flags);
	/* Reconfigure hardware if the context has changed. */
	if (gsc->m2m.ctx != ctx) {
		gsc_dbg("gsc->m2m.ctx = 0x%p, current_ctx = 0x%p",
			  gsc->m2m.ctx, ctx);
		ctx->state |= GSC_PARAMS;
		gsc->m2m.ctx = ctx;
	}

	is_set = (ctx->state & GSC_CTX_STOP_REQ) ? 1 : 0;
	ctx->state &= ~GSC_CTX_STOP_REQ;
	if (is_set) {
		wake_up(&gsc->irq_queue);
		goto put_device;
	}

	ret = gsc_fill_addr(ctx);
	if (ret) {
		gsc_err("Wrong address");
		goto put_device;
	}

	gsc_set_prefbuf(gsc, ctx->s_frame);
	gsc_hw_set_input_addr(gsc, &ctx->s_frame.addr, GSC_M2M_BUF_NUM);
	gsc_hw_set_output_addr(gsc, &ctx->d_frame.addr, GSC_M2M_BUF_NUM);

	if (ctx->state & GSC_PARAMS) {
		gsc_hw_set_input_buf_masking(gsc, GSC_M2M_BUF_NUM, false);
		gsc_hw_set_output_buf_masking(gsc, GSC_M2M_BUF_NUM, false);
		gsc_hw_set_frm_done_irq_mask(gsc, false);
		gsc_hw_set_gsc_irq_enable(gsc, true);

		if (gsc_set_scaler_info(ctx)) {
			gsc_err("Scaler setup error");
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
	/* When you update SFRs in the middle of operating
	gsc_hw_set_sfr_update(ctx);
	*/

	ctx->state &= ~GSC_PARAMS;

	if (!test_and_set_bit(ST_M2M_RUN, &gsc->state)) {
		/* One frame mode sequence
		 GSCALER_ON on -> GSCALER_OP_STATUS is operating ->
		 GSCALER_ON off */
		gsc_hw_enable_control(gsc, true);
		ret = gsc_wait_operating(gsc);
		if (ret < 0) {
			gsc_err("gscaler wait operating timeout");
			goto put_device;
		}
		gsc_hw_enable_control(gsc, false);
	}

	spin_unlock_irqrestore(&ctx->slock, flags);
	return;

put_device:
	ctx->state &= ~GSC_PARAMS;
	spin_unlock_irqrestore(&ctx->slock, flags);
	pm_runtime_put_sync(&gsc->pdev->dev);
}

static int gsc_m2m_queue_setup(struct vb2_queue *vq, unsigned int *num_buffers,
			       unsigned int *num_planes, unsigned long sizes[],
			       void *allocators[])
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
	for (i = 0; i < frame->fmt->num_planes; i++) {
		sizes[i] = get_plane_size(frame, i);
		allocators[i] = ctx->gsc_dev->alloc_ctx;
	}
	return 0;
}

static int gsc_m2m_buf_prepare(struct vb2_buffer *vb)
{
	struct gsc_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct gsc_dev *gsc = ctx->gsc_dev;
	struct gsc_frame *frame;
	int i;

	frame = ctx_get_frame(ctx, vb->vb2_queue->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	if (!V4L2_TYPE_IS_OUTPUT(vb->vb2_queue->type)) {
		for (i = 0; i < frame->fmt->num_planes; i++)
			vb2_set_plane_payload(vb, i, frame->payload[i]);
	}

	if (frame->cacheable)
		gsc->vb2->cache_flush(vb, frame->fmt->num_planes);

	return 0;
}

static void gsc_m2m_buf_queue(struct vb2_buffer *vb)
{
	struct gsc_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	gsc_dbg("ctx: %p, ctx->state: 0x%x", ctx, ctx->state);

	if (ctx->m2m_ctx)
		v4l2_m2m_buf_queue(ctx->m2m_ctx, vb);
}

struct vb2_ops gsc_m2m_qops = {
	.queue_setup	 = gsc_m2m_queue_setup,
	.buf_prepare	 = gsc_m2m_buf_prepare,
	.buf_queue	 = gsc_m2m_buf_queue,
	.wait_prepare	 = gsc_unlock,
	.wait_finish	 = gsc_lock,
	.stop_streaming	 = gsc_m2m_stop_streaming,
};

static int gsc_m2m_querycap(struct file *file, void *fh,
			   struct v4l2_capability *cap)
{
	struct gsc_ctx *ctx = fh_to_ctx(fh);
	struct gsc_dev *gsc = ctx->gsc_dev;

	strncpy(cap->driver, gsc->pdev->name, sizeof(cap->driver) - 1);
	strncpy(cap->card, gsc->pdev->name, sizeof(cap->card) - 1);
	cap->bus_info[0] = 0;
	cap->capabilities = V4L2_CAP_STREAMING |
		V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_OUTPUT |
		V4L2_CAP_VIDEO_CAPTURE_MPLANE | V4L2_CAP_VIDEO_OUTPUT_MPLANE;

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

	if ((f->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) &&
	    (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE))
		return -EINVAL;

	return gsc_g_fmt_mplane(ctx, f);
}

static int gsc_m2m_try_fmt_mplane(struct file *file, void *fh,
				  struct v4l2_format *f)
{
	struct gsc_ctx *ctx = fh_to_ctx(fh);

	if ((f->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) &&
	    (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE))
		return -EINVAL;

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
		gsc_err("queue (%d) busy", f->type);
		return -EBUSY;
	}

	if (V4L2_TYPE_IS_OUTPUT(f->type)) {
		frame = &ctx->s_frame;
	} else {
		frame = &ctx->d_frame;
	}

	pix = &f->fmt.pix_mp;
	frame->fmt = find_format(&pix->pixelformat, NULL, 0);
	if (!frame->fmt)
		return -EINVAL;

	for (i = 0; i < frame->fmt->num_planes; i++)
		frame->payload[i] = pix->plane_fmt[i].sizeimage;

	gsc_set_frame_size(frame, pix->width, pix->height);

	if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		gsc_ctx_state_lock_set(GSC_PARAMS | GSC_DST_FMT, ctx);
	else
		gsc_ctx_state_lock_set(GSC_PARAMS | GSC_SRC_FMT, ctx);

	gsc_dbg("f_w: %d, f_h: %d", frame->f_width, frame->f_height);

	return 0;
}

static int gsc_m2m_reqbufs(struct file *file, void *fh,
			  struct v4l2_requestbuffers *reqbufs)
{
	struct gsc_ctx *ctx = fh_to_ctx(fh);
	struct gsc_dev *gsc = ctx->gsc_dev;
	struct gsc_frame *frame;
	u32 max_cnt;

	max_cnt = (reqbufs->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) ?
		gsc->variant->in_buf_cnt : gsc->variant->out_buf_cnt;
	if (reqbufs->count > max_cnt)
		return -EINVAL;
	else if (reqbufs->count == 0) {
		if (reqbufs->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
			gsc_ctx_state_lock_clear(GSC_SRC_FMT, ctx);
		else
			gsc_ctx_state_lock_clear(GSC_DST_FMT, ctx);
	}

	frame = ctx_get_frame(ctx, reqbufs->type);
	frame->cacheable = ctx->gsc_ctrls.cacheable->val;
	gsc->vb2->set_cacheable(gsc->alloc_ctx, frame->cacheable);

	return v4l2_m2m_reqbufs(file, ctx->m2m_ctx, reqbufs);
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

static int gsc_m2m_cropcap(struct file *file, void *fh,
			struct v4l2_cropcap *cr)
{
	struct gsc_frame *frame;
	struct gsc_ctx *ctx = fh_to_ctx(fh);

	frame = ctx_get_frame(ctx, cr->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	cr->bounds.left		= 0;
	cr->bounds.top		= 0;
	cr->bounds.width	= frame->f_width;
	cr->bounds.height	= frame->f_height;
	cr->defrect		= cr->bounds;

	return 0;
}

static int gsc_m2m_g_crop(struct file *file, void *fh, struct v4l2_crop *cr)
{
	struct gsc_ctx *ctx = fh_to_ctx(fh);

	return gsc_g_crop(ctx, cr);
}

static int gsc_m2m_s_crop(struct file *file, void *fh, struct v4l2_crop *cr)
{
	struct gsc_ctx *ctx = fh_to_ctx(fh);
	struct gsc_variant *variant = ctx->gsc_dev->variant;
	struct gsc_frame *f;
	int ret;

	ret = gsc_try_crop(ctx, cr);
	if (ret)
		return ret;

	f = (cr->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) ?
		&ctx->s_frame : &ctx->d_frame;

	/* Check to see if scaling ratio is within supported range */
	if (gsc_ctx_state_is_set(GSC_DST_FMT | GSC_SRC_FMT, ctx)) {
		if (cr->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
			ret = gsc_check_scaler_ratio(variant, cr->c.width,
					cr->c.height, ctx->d_frame.crop.width,
					ctx->d_frame.crop.height,
					ctx->gsc_ctrls.rotate->val, ctx->out_path);
		} else {
			ret = gsc_check_scaler_ratio(variant, ctx->s_frame.crop.width,
					ctx->s_frame.crop.height, cr->c.width,
					cr->c.height, ctx->gsc_ctrls.rotate->val,
					ctx->out_path);
		}
		if (ret) {
			gsc_err("Out of scaler range");
			return -EINVAL;
		}
	}

	f->crop.left = cr->c.left;
	f->crop.top = cr->c.top;
	f->crop.width  = cr->c.width;
	f->crop.height = cr->c.height;

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
	.vidioc_querybuf		= gsc_m2m_querybuf,

	.vidioc_qbuf			= gsc_m2m_qbuf,
	.vidioc_dqbuf			= gsc_m2m_dqbuf,

	.vidioc_streamon		= gsc_m2m_streamon,
	.vidioc_streamoff		= gsc_m2m_streamoff,

	.vidioc_g_crop			= gsc_m2m_g_crop,
	.vidioc_s_crop			= gsc_m2m_s_crop,
	.vidioc_cropcap			= gsc_m2m_cropcap

};

static int queue_init(void *priv, struct vb2_queue *src_vq,
		      struct vb2_queue *dst_vq)
{
	struct gsc_ctx *ctx = priv;
	int ret;

	memset(src_vq, 0, sizeof(*src_vq));
	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_USERPTR;
	src_vq->drv_priv = ctx;
	src_vq->ops = &gsc_m2m_qops;
	src_vq->mem_ops = ctx->gsc_dev->vb2->ops;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	memset(dst_vq, 0, sizeof(*dst_vq));
	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_USERPTR;
	dst_vq->drv_priv = ctx;
	dst_vq->ops = &gsc_m2m_qops;
	dst_vq->mem_ops = ctx->gsc_dev->vb2->ops;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);

	return vb2_queue_init(dst_vq);
}

static int gsc_m2m_open(struct file *file)
{
	struct gsc_dev *gsc = video_drvdata(file);
	struct gsc_ctx *ctx = NULL;
	int ret;

	gsc_dbg("pid: %d, state: 0x%lx", task_pid_nr(current), gsc->state);

	if (gsc_out_opened(gsc) || gsc_cap_opened(gsc))
		return -EBUSY;

	ctx = kzalloc(sizeof *ctx, GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

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
	ctx->state |= GSC_CTX_M2M;
	ctx->flags = 0;
	ctx->in_path = GSC_DMA;
	ctx->out_path = GSC_DMA;
	spin_lock_init(&ctx->slock);

	ctx->m2m_ctx = v4l2_m2m_ctx_init(gsc->m2m.m2m_dev, ctx, queue_init);
	if (IS_ERR(ctx->m2m_ctx)) {
		gsc_err("Failed to initialize m2m context");
		ret = PTR_ERR(ctx->m2m_ctx);
		goto error_fh;
	}

	if (gsc->m2m.refcnt++ == 0)
		set_bit(ST_M2M_OPEN, &gsc->state);

	gsc_dbg("gsc m2m driver is opened, ctx(0x%p)", ctx);
	return 0;

error_fh:
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	kfree(ctx);
	return ret;
}

static int gsc_m2m_release(struct file *file)
{
	struct gsc_ctx *ctx = fh_to_ctx(file->private_data);
	struct gsc_dev *gsc = ctx->gsc_dev;

	gsc_dbg("pid: %d, state: 0x%lx, refcnt= %d",
		task_pid_nr(current), gsc->state, gsc->m2m.refcnt);

	v4l2_m2m_ctx_release(ctx->m2m_ctx);
	gsc_ctrls_delete(ctx);
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);

	if (--gsc->m2m.refcnt <= 0)
		clear_bit(ST_M2M_OPEN, &gsc->state);
	kfree(ctx);
	return 0;
}

static unsigned int gsc_m2m_poll(struct file *file,
				     struct poll_table_struct *wait)
{
	struct gsc_ctx *ctx = fh_to_ctx(file->private_data);

	return v4l2_m2m_poll(file, ctx->m2m_ctx, wait);
}


static int gsc_m2m_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct gsc_ctx *ctx = fh_to_ctx(file->private_data);

	return v4l2_m2m_mmap(file, ctx->m2m_ctx, vma);
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
	struct video_device *vfd;
	struct platform_device *pdev;
	int ret = 0;

	if (!gsc)
		return -ENODEV;

	pdev = gsc->pdev;

	vfd = video_device_alloc();
	if (!vfd) {
		dev_err(&pdev->dev, "Failed to allocate video device\n");
		return -ENOMEM;
	}

	vfd->fops	= &gsc_m2m_fops;
	vfd->ioctl_ops	= &gsc_m2m_ioctl_ops;
	vfd->release	= video_device_release;
	vfd->lock	= &gsc->lock;
	snprintf(vfd->name, sizeof(vfd->name), "%s:m2m", dev_name(&pdev->dev));

	video_set_drvdata(vfd, gsc);

	gsc->m2m.vfd = vfd;
	gsc->m2m.m2m_dev = v4l2_m2m_init(&gsc_m2m_ops);
	if (IS_ERR(gsc->m2m.m2m_dev)) {
		dev_err(&pdev->dev, "failed to initialize v4l2-m2m device\n");
		ret = PTR_ERR(gsc->m2m.m2m_dev);
		goto err_m2m_r1;
	}

	ret = video_register_device(vfd, VFL_TYPE_GRABBER,
				    EXYNOS_VIDEONODE_GSC_M2M(gsc->id));
	if (ret) {
		dev_err(&pdev->dev,
			 "%s(): failed to register video device\n", __func__);
		goto err_m2m_r2;
	}

	gsc_dbg("gsc m2m driver registered as /dev/video%d", vfd->num);

	return 0;

err_m2m_r2:
	v4l2_m2m_release(gsc->m2m.m2m_dev);
err_m2m_r1:
	video_device_release(gsc->m2m.vfd);

	return ret;
}

void gsc_unregister_m2m_device(struct gsc_dev *gsc)
{
	if (gsc)
		v4l2_m2m_release(gsc->m2m.m2m_dev);
}
