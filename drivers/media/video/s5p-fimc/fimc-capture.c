/*
 * Samsung S5P/EXYNOS4 SoC series camera interface (camera capture) driver
 *
 * Copyright (C) 2010 - 2011 Samsung Electronics Co., Ltd.
 * Author: Sylwester Nawrocki, <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/bug.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/pm_runtime.h>
#include <linux/list.h>
#include <linux/slab.h>

#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include "fimc-mdevice.h"
#include "fimc-core.h"

static int fimc_init_capture(struct fimc_dev *fimc)
{
	struct fimc_ctx *ctx = fimc->vid_cap.ctx;
	struct fimc_sensor_info *sensor;
	unsigned long flags;
	int ret = 0;

	if (fimc->pipeline.sensor == NULL || ctx == NULL)
		return -ENXIO;
	if (ctx->s_frame.fmt == NULL)
		return -EINVAL;

	sensor = v4l2_get_subdev_hostdata(fimc->pipeline.sensor);

	spin_lock_irqsave(&fimc->slock, flags);
	fimc_prepare_dma_offset(ctx, &ctx->d_frame);
	fimc_set_yuv_order(ctx);

	fimc_hw_set_camera_polarity(fimc, sensor->pdata);
	fimc_hw_set_camera_type(fimc, sensor->pdata);
	fimc_hw_set_camera_source(fimc, sensor->pdata);
	fimc_hw_set_camera_offset(fimc, &ctx->s_frame);

	ret = fimc_set_scaler_info(ctx);
	if (!ret) {
		fimc_hw_set_input_path(ctx);
		fimc_hw_set_prescaler(ctx);
		fimc_hw_set_mainscaler(ctx);
		fimc_hw_set_target_format(ctx);
		fimc_hw_set_rotation(ctx);
		fimc_hw_set_effect(ctx);
		fimc_hw_set_output_path(ctx);
		fimc_hw_set_out_dma(ctx);
	}
	spin_unlock_irqrestore(&fimc->slock, flags);
	return ret;
}

static void fimc_capture_state_cleanup(struct fimc_dev *fimc)
{
	struct fimc_vid_cap *cap = &fimc->vid_cap;
	struct fimc_vid_buffer *buf;
	unsigned long flags;

	spin_lock_irqsave(&fimc->slock, flags);
	fimc->state &= ~(1 << ST_CAPT_RUN | 1 << ST_CAPT_PEND |
			 1 << ST_CAPT_SHUT | 1 << ST_CAPT_STREAM);

	fimc->vid_cap.active_buf_cnt = 0;

	/* Release buffers that were enqueued in the driver by videobuf2. */
	while (!list_empty(&cap->pending_buf_q)) {
		buf = pending_queue_pop(cap);
		vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
	}

	while (!list_empty(&cap->active_buf_q)) {
		buf = active_queue_pop(cap);
		vb2_buffer_done(&buf->vb, VB2_BUF_STATE_ERROR);
	}

	spin_unlock_irqrestore(&fimc->slock, flags);
}

static int fimc_stop_capture(struct fimc_dev *fimc)
{
	struct fimc_vid_cap *cap = &fimc->vid_cap;
	unsigned long flags;

	if (!fimc_capture_active(fimc))
		return 0;

	spin_lock_irqsave(&fimc->slock, flags);
	set_bit(ST_CAPT_SHUT, &fimc->state);
	fimc_deactivate_capture(fimc);
	spin_unlock_irqrestore(&fimc->slock, flags);

	wait_event_timeout(fimc->irq_queue,
			   !test_bit(ST_CAPT_SHUT, &fimc->state),
			   FIMC_SHUTDOWN_TIMEOUT);

	v4l2_subdev_call(cap->sd, video, s_stream, 0);

	fimc_capture_state_cleanup(fimc);
	dbg("state: 0x%lx", fimc->state);
	return 0;
}


static int start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct fimc_ctx *ctx = q->drv_priv;
	struct fimc_dev *fimc = ctx->fimc_dev;
	struct fimc_vid_cap *vid_cap = &fimc->vid_cap;
	int min_bufs;
	int ret;

	fimc_hw_reset(fimc);
	vid_cap->frame_count = 0;

	ret = fimc_init_capture(fimc);
	if (ret)
		goto error;

	set_bit(ST_CAPT_PEND, &fimc->state);

	min_bufs = fimc->vid_cap.reqbufs_count > 1 ? 2 : 1;

	if (fimc->vid_cap.active_buf_cnt >= min_bufs)
		fimc_activate_capture(ctx);

	return 0;
error:
	fimc_capture_state_cleanup(fimc);
	return ret;
}

static int stop_streaming(struct vb2_queue *q)
{
	struct fimc_ctx *ctx = q->drv_priv;
	struct fimc_dev *fimc = ctx->fimc_dev;

	if (!fimc_capture_active(fimc))
		return -EINVAL;

	return fimc_stop_capture(fimc);
}

int fimc_capture_suspend(struct fimc_dev *fimc)
{
	return -EBUSY;
}

int fimc_capture_resume(struct fimc_dev *fimc)
{
	return 0;
}

static unsigned int get_plane_size(struct fimc_frame *fr, unsigned int plane)
{
	if (!fr || plane >= fr->fmt->memplanes)
		return 0;
	return fr->f_width * fr->f_height * fr->fmt->depth[plane] / 8;
}

static int queue_setup(struct vb2_queue *vq, unsigned int *num_buffers,
		       unsigned int *num_planes, unsigned int sizes[],
		       void *allocators[])
{
	struct fimc_ctx *ctx = vq->drv_priv;
	struct fimc_fmt *fmt = ctx->d_frame.fmt;
	int i;

	if (!fmt)
		return -EINVAL;

	*num_planes = fmt->memplanes;

	for (i = 0; i < fmt->memplanes; i++) {
		sizes[i] = get_plane_size(&ctx->d_frame, i);
		allocators[i] = ctx->fimc_dev->alloc_ctx;
	}

	return 0;
}

static int buffer_prepare(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct fimc_ctx *ctx = vq->drv_priv;
	int i;

	if (!ctx->d_frame.fmt || vq->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	for (i = 0; i < ctx->d_frame.fmt->memplanes; i++) {
		unsigned long size = get_plane_size(&ctx->d_frame, i);

		if (vb2_plane_size(vb, i) < size) {
			v4l2_err(ctx->fimc_dev->vid_cap.vfd,
				 "User buffer too small (%ld < %ld)\n",
				 vb2_plane_size(vb, i), size);
			return -EINVAL;
		}

		vb2_set_plane_payload(vb, i, size);
	}

	return 0;
}

static void buffer_queue(struct vb2_buffer *vb)
{
	struct fimc_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct fimc_dev *fimc = ctx->fimc_dev;
	struct fimc_vid_buffer *buf
		= container_of(vb, struct fimc_vid_buffer, vb);
	struct fimc_vid_cap *vid_cap = &fimc->vid_cap;
	unsigned long flags;
	int min_bufs;

	spin_lock_irqsave(&fimc->slock, flags);
	fimc_prepare_addr(ctx, &buf->vb, &ctx->d_frame, &buf->paddr);

	if (!test_bit(ST_CAPT_STREAM, &fimc->state)
	     && vid_cap->active_buf_cnt < FIMC_MAX_OUT_BUFS) {
		/* Setup the buffer directly for processing. */
		int buf_id = (vid_cap->reqbufs_count == 1) ? -1 :
				vid_cap->buf_index;

		fimc_hw_set_output_addr(fimc, &buf->paddr, buf_id);
		buf->index = vid_cap->buf_index;
		active_queue_add(vid_cap, buf);

		if (++vid_cap->buf_index >= FIMC_MAX_OUT_BUFS)
			vid_cap->buf_index = 0;
	} else {
		fimc_pending_queue_add(vid_cap, buf);
	}

	min_bufs = vid_cap->reqbufs_count > 1 ? 2 : 1;

	if (vb2_is_streaming(&vid_cap->vbq) &&
	    vid_cap->active_buf_cnt >= min_bufs &&
	    !test_and_set_bit(ST_CAPT_STREAM, &fimc->state))
		fimc_activate_capture(ctx);

	spin_unlock_irqrestore(&fimc->slock, flags);
}

static void fimc_lock(struct vb2_queue *vq)
{
	struct fimc_ctx *ctx = vb2_get_drv_priv(vq);
	mutex_lock(&ctx->fimc_dev->lock);
}

static void fimc_unlock(struct vb2_queue *vq)
{
	struct fimc_ctx *ctx = vb2_get_drv_priv(vq);
	mutex_unlock(&ctx->fimc_dev->lock);
}

static struct vb2_ops fimc_capture_qops = {
	.queue_setup		= queue_setup,
	.buf_prepare		= buffer_prepare,
	.buf_queue		= buffer_queue,
	.wait_prepare		= fimc_unlock,
	.wait_finish		= fimc_lock,
	.start_streaming	= start_streaming,
	.stop_streaming		= stop_streaming,
};

/**
 * fimc_capture_ctrls_create - initialize the control handler
 * Initialize the capture video node control handler and fill it
 * with the FIMC controls. Inherit any sensor's controls if the
 * 'user_subdev_api' flag is false (default behaviour).
 * This function need to be called with the graph mutex held.
 */
int fimc_capture_ctrls_create(struct fimc_dev *fimc)
{
	struct fimc_vid_cap *vid_cap = &fimc->vid_cap;
	int ret;

	if (WARN_ON(vid_cap->ctx == NULL))
		return -ENXIO;
	if (vid_cap->ctx->ctrls_rdy)
		return 0;

	ret = fimc_ctrls_create(vid_cap->ctx);
	if (ret || vid_cap->user_subdev_api)
		return ret;

	return v4l2_ctrl_add_handler(&vid_cap->ctx->ctrl_handler,
				    fimc->pipeline.sensor->ctrl_handler);
}

static int fimc_capture_open(struct file *file)
{
	struct fimc_dev *fimc = video_drvdata(file);
	int ret = v4l2_fh_open(file);

	if (ret)
		return ret;

	dbg("pid: %d, state: 0x%lx", task_pid_nr(current), fimc->state);

	/* Return if the corresponding video mem2mem node is already opened. */
	if (fimc_m2m_active(fimc))
		return -EBUSY;

	ret = pm_runtime_get_sync(&fimc->pdev->dev);
	if (ret < 0) {
		v4l2_fh_release(file);
		return ret;
	}

	if (++fimc->vid_cap.refcnt == 1)
		ret = fimc_capture_ctrls_create(fimc);

	return ret;
}

static int fimc_capture_close(struct file *file)
{
	struct fimc_dev *fimc = video_drvdata(file);

	dbg("pid: %d, state: 0x%lx", task_pid_nr(current), fimc->state);

	if (--fimc->vid_cap.refcnt == 0) {
		fimc_stop_capture(fimc);
		fimc_ctrls_delete(fimc->vid_cap.ctx);
		vb2_queue_release(&fimc->vid_cap.vbq);
	}

	pm_runtime_put(&fimc->pdev->dev);

	return v4l2_fh_release(file);
}

static unsigned int fimc_capture_poll(struct file *file,
				      struct poll_table_struct *wait)
{
	struct fimc_dev *fimc = video_drvdata(file);

	return vb2_poll(&fimc->vid_cap.vbq, file, wait);
}

static int fimc_capture_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct fimc_dev *fimc = video_drvdata(file);

	return vb2_mmap(&fimc->vid_cap.vbq, vma);
}

/* video device file operations */
static const struct v4l2_file_operations fimc_capture_fops = {
	.owner		= THIS_MODULE,
	.open		= fimc_capture_open,
	.release	= fimc_capture_close,
	.poll		= fimc_capture_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= fimc_capture_mmap,
};

static int fimc_vidioc_querycap_capture(struct file *file, void *priv,
					struct v4l2_capability *cap)
{
	struct fimc_dev *fimc = video_drvdata(file);

	strncpy(cap->driver, fimc->pdev->name, sizeof(cap->driver) - 1);
	strncpy(cap->card, fimc->pdev->name, sizeof(cap->card) - 1);
	cap->bus_info[0] = 0;
	cap->capabilities = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_CAPTURE |
			    V4L2_CAP_VIDEO_CAPTURE_MPLANE;

	return 0;
}

/* Synchronize formats of the camera interface input and attached  sensor. */
static int sync_capture_fmt(struct fimc_ctx *ctx)
{
	struct fimc_frame *frame = &ctx->s_frame;
	struct fimc_dev *fimc = ctx->fimc_dev;
	struct v4l2_mbus_framefmt *fmt = &fimc->vid_cap.fmt;
	int ret;

	fmt->width  = ctx->d_frame.o_width;
	fmt->height = ctx->d_frame.o_height;

	ret = v4l2_subdev_call(fimc->vid_cap.sd, video, s_mbus_fmt, fmt);
	if (ret == -ENOIOCTLCMD) {
		err("s_mbus_fmt failed");
		return ret;
	}
	dbg("w: %d, h: %d, code= %d", fmt->width, fmt->height, fmt->code);

	frame->fmt = find_mbus_format(fmt, FMT_FLAGS_CAM);
	if (!frame->fmt) {
		err("fimc source format not found\n");
		return -EINVAL;
	}

	frame->f_width	= fmt->width;
	frame->f_height = fmt->height;
	frame->width	= fmt->width;
	frame->height	= fmt->height;
	frame->o_width	= fmt->width;
	frame->o_height = fmt->height;
	frame->offs_h	= 0;
	frame->offs_v	= 0;

	return 0;
}

static int fimc_cap_g_fmt_mplane(struct file *file, void *fh,
				 struct v4l2_format *f)
{
	struct fimc_dev *fimc = video_drvdata(file);
	struct fimc_ctx *ctx = fimc->vid_cap.ctx;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	return fimc_fill_format(&ctx->d_frame, f);
}

static int fimc_cap_try_fmt_mplane(struct file *file, void *fh,
				   struct v4l2_format *f)
{
	struct fimc_dev *fimc = video_drvdata(file);
	struct fimc_ctx *ctx = fimc->vid_cap.ctx;

	return fimc_try_fmt_mplane(ctx, f);
}

static int fimc_cap_s_fmt_mplane(struct file *file, void *priv,
				 struct v4l2_format *f)
{
	struct fimc_dev *fimc = video_drvdata(file);
	struct fimc_ctx *ctx = fimc->vid_cap.ctx;
	struct v4l2_pix_format_mplane *pix;
	struct fimc_frame *frame;
	int ret;
	int i;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	ret = fimc_try_fmt_mplane(ctx, f);
	if (ret)
		return ret;

	if (vb2_is_busy(&fimc->vid_cap.vbq) || fimc_capture_active(fimc))
		return -EBUSY;

	frame = &ctx->d_frame;

	pix = &f->fmt.pix_mp;
	frame->fmt = find_format(f, FMT_FLAGS_M2M | FMT_FLAGS_CAM);
	if (!frame->fmt) {
		v4l2_err(fimc->vid_cap.vfd,
			 "Not supported capture (FIMC target) color format\n");
		return -EINVAL;
	}

	for (i = 0; i < frame->fmt->colplanes; i++) {
		frame->payload[i] =
			(pix->width * pix->height * frame->fmt->depth[i]) >> 3;
	}

	/* Output DMA frame pixel size and offsets. */
	frame->f_width = pix->plane_fmt[0].bytesperline * 8
			/ frame->fmt->depth[0];
	frame->f_height = pix->height;
	frame->width	= pix->width;
	frame->height	= pix->height;
	frame->o_width	= pix->width;
	frame->o_height = pix->height;
	frame->offs_h	= 0;
	frame->offs_v	= 0;

	ctx->state |= (FIMC_PARAMS | FIMC_DST_FMT);

	ret = sync_capture_fmt(ctx);
	return ret;
}

static int fimc_cap_enum_input(struct file *file, void *priv,
			       struct v4l2_input *i)
{
	struct fimc_dev *fimc = video_drvdata(file);

	if (i->index != 0)
		return -EINVAL;


	i->type = V4L2_INPUT_TYPE_CAMERA;
	return 0;
}

static int fimc_cap_s_input(struct file *file, void *priv, unsigned int i)
{
	return i == 0 ? i : -EINVAL;
}

static int fimc_cap_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int fimc_cap_streamon(struct file *file, void *priv,
			     enum v4l2_buf_type type)
{
	struct fimc_dev *fimc = video_drvdata(file);
	struct fimc_ctx *ctx = fimc->vid_cap.ctx;

	if (fimc_capture_active(fimc) || !fimc->vid_cap.sd)
		return -EBUSY;

	if (!(ctx->state & FIMC_DST_FMT)) {
		v4l2_err(fimc->vid_cap.vfd, "Format is not set\n");
		return -EINVAL;
	}

	return vb2_streamon(&fimc->vid_cap.vbq, type);
}

static int fimc_cap_streamoff(struct file *file, void *priv,
			    enum v4l2_buf_type type)
{
	struct fimc_dev *fimc = video_drvdata(file);

	return vb2_streamoff(&fimc->vid_cap.vbq, type);
}

static int fimc_cap_reqbufs(struct file *file, void *priv,
			    struct v4l2_requestbuffers *reqbufs)
{
	struct fimc_dev *fimc = video_drvdata(file);
	int ret = vb2_reqbufs(&fimc->vid_cap.vbq, reqbufs);

	if (!ret)
		fimc->vid_cap.reqbufs_count = reqbufs->count;
	return ret;
}

static int fimc_cap_querybuf(struct file *file, void *priv,
			   struct v4l2_buffer *buf)
{
	struct fimc_dev *fimc = video_drvdata(file);

	return vb2_querybuf(&fimc->vid_cap.vbq, buf);
}

static int fimc_cap_qbuf(struct file *file, void *priv,
			  struct v4l2_buffer *buf)
{
	struct fimc_dev *fimc = video_drvdata(file);

	return vb2_qbuf(&fimc->vid_cap.vbq, buf);
}

static int fimc_cap_dqbuf(struct file *file, void *priv,
			   struct v4l2_buffer *buf)
{
	struct fimc_dev *fimc = video_drvdata(file);

	return vb2_dqbuf(&fimc->vid_cap.vbq, buf, file->f_flags & O_NONBLOCK);
}

static int fimc_cap_cropcap(struct file *file, void *fh,
			    struct v4l2_cropcap *cr)
{
	struct fimc_dev *fimc = video_drvdata(file);
	struct fimc_frame *f = &fimc->vid_cap.ctx->s_frame;

	if (cr->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	cr->bounds.left		= 0;
	cr->bounds.top		= 0;
	cr->bounds.width	= f->o_width;
	cr->bounds.height	= f->o_height;
	cr->defrect		= cr->bounds;

	return 0;
}

static int fimc_cap_g_crop(struct file *file, void *fh, struct v4l2_crop *cr)
{
	struct fimc_dev *fimc = video_drvdata(file);
	struct fimc_frame *f = &fimc->vid_cap.ctx->s_frame;

	cr->c.left	= f->offs_h;
	cr->c.top	= f->offs_v;
	cr->c.width	= f->width;
	cr->c.height	= f->height;

	return 0;
}

static int fimc_cap_s_crop(struct file *file, void *fh, struct v4l2_crop *cr)
{
	struct fimc_dev *fimc = video_drvdata(file);
	struct fimc_ctx *ctx = fimc->vid_cap.ctx;
	struct fimc_frame *f;
	int ret = -EINVAL;

	if (fimc_capture_active(fimc))
		return -EBUSY;

	ret = fimc_try_crop(ctx, cr);
	if (ret)
		return ret;

	if (!(ctx->state & FIMC_DST_FMT)) {
		v4l2_err(fimc->vid_cap.vfd, "Capture format is not set\n");
		return -EINVAL;
	}

	f = &ctx->s_frame;
	/* Check for the pixel scaling ratio when cropping input image. */
	ret = fimc_check_scaler_ratio(cr->c.width, cr->c.height,
				      ctx->d_frame.width, ctx->d_frame.height,
				      ctx->rotation);
	if (ret) {
		v4l2_err(fimc->vid_cap.vfd, "Out of the scaler range\n");
		return ret;
	}

	f->offs_h = cr->c.left;
	f->offs_v = cr->c.top;
	f->width  = cr->c.width;
	f->height = cr->c.height;

	return 0;
}


static const struct v4l2_ioctl_ops fimc_capture_ioctl_ops = {
	.vidioc_querycap		= fimc_vidioc_querycap_capture,

	.vidioc_enum_fmt_vid_cap_mplane	= fimc_vidioc_enum_fmt_mplane,
	.vidioc_try_fmt_vid_cap_mplane	= fimc_cap_try_fmt_mplane,
	.vidioc_s_fmt_vid_cap_mplane	= fimc_cap_s_fmt_mplane,
	.vidioc_g_fmt_vid_cap_mplane	= fimc_cap_g_fmt_mplane,

	.vidioc_reqbufs			= fimc_cap_reqbufs,
	.vidioc_querybuf		= fimc_cap_querybuf,

	.vidioc_qbuf			= fimc_cap_qbuf,
	.vidioc_dqbuf			= fimc_cap_dqbuf,

	.vidioc_streamon		= fimc_cap_streamon,
	.vidioc_streamoff		= fimc_cap_streamoff,

	.vidioc_g_crop			= fimc_cap_g_crop,
	.vidioc_s_crop			= fimc_cap_s_crop,
	.vidioc_cropcap			= fimc_cap_cropcap,

	.vidioc_enum_input		= fimc_cap_enum_input,
	.vidioc_s_input			= fimc_cap_s_input,
	.vidioc_g_input			= fimc_cap_g_input,
};

/* Media operations */
static int fimc_link_setup(struct media_entity *entity,
			   const struct media_pad *local,
			   const struct media_pad *remote, u32 flags)
{
	struct video_device *vd = media_entity_to_video_device(entity);
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(remote->entity);
	struct fimc_dev *fimc = video_get_drvdata(vd);

	if (WARN_ON(fimc == NULL))
		return 0;

	dbg("%s --> %s, flags: 0x%x. input: 0x%x",
	    local->entity->name, remote->entity->name, flags,
	    fimc->vid_cap.input);

	if (flags & MEDIA_LNK_FL_ENABLED) {
		if (fimc->vid_cap.input != 0)
			return -EBUSY;
		fimc->vid_cap.input = sd->grp_id;
		return 0;
	}

	fimc->vid_cap.input = 0;
	return 0;
}

static const struct media_entity_operations fimc_media_ops = {
	.link_setup = fimc_link_setup,
};

/* fimc->lock must be already initialized */
int fimc_register_capture_device(struct fimc_dev *fimc,
				 struct v4l2_device *v4l2_dev)
{
	struct video_device *vfd;
	struct fimc_vid_cap *vid_cap;
	struct fimc_ctx *ctx;
	struct v4l2_format f;
	struct fimc_frame *fr;
	struct vb2_queue *q;
	int ret = -ENOMEM;

	ctx = kzalloc(sizeof *ctx, GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->fimc_dev	 = fimc;
	ctx->in_path	 = FIMC_CAMERA;
	ctx->out_path	 = FIMC_DMA;
	ctx->state	 = FIMC_CTX_CAP;

	/* Default format of the output frames */
	f.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB32;
	fr = &ctx->d_frame;
	fr->fmt = find_format(&f, FMT_FLAGS_M2M);
	fr->width = fr->f_width = fr->o_width = 640;
	fr->height = fr->f_height = fr->o_height = 480;

	vfd = video_device_alloc();
	if (!vfd) {
		v4l2_err(v4l2_dev, "Failed to allocate video device\n");
		goto err_vd_alloc;
	}

	snprintf(vfd->name, sizeof(vfd->name), "%s.capture",
		 dev_name(&fimc->pdev->dev));

	vfd->fops	= &fimc_capture_fops;
	vfd->ioctl_ops	= &fimc_capture_ioctl_ops;
	vfd->v4l2_dev	= v4l2_dev;
	vfd->minor	= -1;
	vfd->release	= video_device_release;
	vfd->lock	= &fimc->lock;
	video_set_drvdata(vfd, fimc);

	vid_cap = &fimc->vid_cap;
	vid_cap->vfd = vfd;
	vid_cap->active_buf_cnt = 0;
	vid_cap->reqbufs_count  = 0;
	vid_cap->refcnt = 0;
	/* Default color format for image sensor */
	vid_cap->fmt.code = V4L2_MBUS_FMT_YUYV8_2X8;

	INIT_LIST_HEAD(&vid_cap->pending_buf_q);
	INIT_LIST_HEAD(&vid_cap->active_buf_q);
	spin_lock_init(&ctx->slock);
	vid_cap->ctx = ctx;

	q = &fimc->vid_cap.vbq;
	memset(q, 0, sizeof(*q));
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	q->io_modes = VB2_MMAP | VB2_USERPTR;
	q->drv_priv = fimc->vid_cap.ctx;
	q->ops = &fimc_capture_qops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->buf_struct_size = sizeof(struct fimc_vid_buffer);

	vb2_queue_init(q);

	fimc->vid_cap.vd_pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_init(&vfd->entity, 1, &fimc->vid_cap.vd_pad, 0);
	if (ret)
		goto err_ent;

	vfd->entity.ops = &fimc_media_ops;
	vfd->ctrl_handler = &ctx->ctrl_handler;
	return 0;

err_ent:
	video_device_release(vfd);
err_vd_alloc:
	kfree(ctx);
	return ret;
}

void fimc_unregister_capture_device(struct fimc_dev *fimc)
{
	struct video_device *vfd = fimc->vid_cap.vfd;

	if (vfd) {
		media_entity_cleanup(&vfd->entity);
		/* Can also be called if video device was
		   not registered */
		video_unregister_device(vfd);
	}
	kfree(fimc->vid_cap.ctx);
	fimc->vid_cap.ctx = NULL;
}
