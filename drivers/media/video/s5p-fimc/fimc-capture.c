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
#include <linux/platform_device.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/i2c.h>

#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include "fimc-core.h"

static struct v4l2_subdev *fimc_subdev_register(struct fimc_dev *fimc,
					    struct s5p_fimc_isp_info *isp_info)
{
	struct i2c_adapter *i2c_adap;
	struct fimc_vid_cap *vid_cap = &fimc->vid_cap;
	struct v4l2_subdev *sd = NULL;

	i2c_adap = i2c_get_adapter(isp_info->i2c_bus_num);
	if (!i2c_adap)
		return ERR_PTR(-ENOMEM);

	sd = v4l2_i2c_new_subdev_board(&vid_cap->v4l2_dev, i2c_adap,
				       isp_info->board_info, NULL);
	if (!sd) {
		v4l2_err(&vid_cap->v4l2_dev, "failed to acquire subdev\n");
		return NULL;
	}

	v4l2_info(&vid_cap->v4l2_dev, "subdevice %s registered successfuly\n",
		isp_info->board_info->type);

	return sd;
}

static void fimc_subdev_unregister(struct fimc_dev *fimc)
{
	struct fimc_vid_cap *vid_cap = &fimc->vid_cap;
	struct i2c_client *client;

	if (vid_cap->input_index < 0)
		return;	/* Subdevice already released or not registered. */

	if (vid_cap->sd) {
		v4l2_device_unregister_subdev(vid_cap->sd);
		client = v4l2_get_subdevdata(vid_cap->sd);
		i2c_unregister_device(client);
		i2c_put_adapter(client->adapter);
		vid_cap->sd = NULL;
	}

	vid_cap->input_index = -1;
}

/**
 * fimc_subdev_attach - attach v4l2_subdev to camera host interface
 *
 * @fimc: FIMC device information
 * @index: index to the array of available subdevices,
 *	   -1 for full array search or non negative value
 *	   to select specific subdevice
 */
static int fimc_subdev_attach(struct fimc_dev *fimc, int index)
{
	struct fimc_vid_cap *vid_cap = &fimc->vid_cap;
	struct s5p_platform_fimc *pdata = fimc->pdata;
	struct s5p_fimc_isp_info *isp_info;
	struct v4l2_subdev *sd;
	int i;

	for (i = 0; i < pdata->num_clients; ++i) {
		isp_info = &pdata->isp_info[i];

		if (index >= 0 && i != index)
			continue;

		sd = fimc_subdev_register(fimc, isp_info);
		if (!IS_ERR_OR_NULL(sd)) {
			vid_cap->sd = sd;
			vid_cap->input_index = i;

			return 0;
		}
	}

	vid_cap->input_index = -1;
	vid_cap->sd = NULL;
	v4l2_err(&vid_cap->v4l2_dev, "fimc%d: sensor attach failed\n",
		 fimc->id);
	return -ENODEV;
}

static int fimc_isp_subdev_init(struct fimc_dev *fimc, unsigned int index)
{
	struct s5p_fimc_isp_info *isp_info;
	struct s5p_platform_fimc *pdata = fimc->pdata;
	int ret;

	if (index >= pdata->num_clients)
		return -EINVAL;

	isp_info = &pdata->isp_info[index];

	if (isp_info->clk_frequency)
		clk_set_rate(fimc->clock[CLK_CAM], isp_info->clk_frequency);

	ret = clk_enable(fimc->clock[CLK_CAM]);
	if (ret)
		return ret;

	ret = fimc_subdev_attach(fimc, index);
	if (ret)
		return ret;

	ret = fimc_hw_set_camera_polarity(fimc, isp_info);
	if (ret)
		return ret;

	ret = v4l2_subdev_call(fimc->vid_cap.sd, core, s_power, 1);
	if (!ret)
		return ret;

	/* enabling power failed so unregister subdev */
	fimc_subdev_unregister(fimc);

	v4l2_err(&fimc->vid_cap.v4l2_dev, "ISP initialization failed: %d\n",
		 ret);

	return ret;
}

static int fimc_stop_capture(struct fimc_dev *fimc)
{
	unsigned long flags;
	struct fimc_vid_cap *cap;
	struct fimc_vid_buffer *buf;

	cap = &fimc->vid_cap;

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

	dbg("state: 0x%lx", fimc->state);
	return 0;
}

static int start_streaming(struct vb2_queue *q)
{
	struct fimc_ctx *ctx = q->drv_priv;
	struct fimc_dev *fimc = ctx->fimc_dev;
	struct s5p_fimc_isp_info *isp_info;
	int ret;

	fimc_hw_reset(fimc);

	ret = v4l2_subdev_call(fimc->vid_cap.sd, video, s_stream, 1);
	if (ret && ret != -ENOIOCTLCMD)
		return ret;

	ret = fimc_prepare_config(ctx, ctx->state);
	if (ret)
		return ret;

	isp_info = &fimc->pdata->isp_info[fimc->vid_cap.input_index];
	fimc_hw_set_camera_type(fimc, isp_info);
	fimc_hw_set_camera_source(fimc, isp_info);
	fimc_hw_set_camera_offset(fimc, &ctx->s_frame);

	if (ctx->state & FIMC_PARAMS) {
		ret = fimc_set_scaler_info(ctx);
		if (ret) {
			err("Scaler setup error");
			return ret;
		}
		fimc_hw_set_input_path(ctx);
		fimc_hw_set_prescaler(ctx);
		fimc_hw_set_mainscaler(ctx);
		fimc_hw_set_target_format(ctx);
		fimc_hw_set_rotation(ctx);
		fimc_hw_set_effect(ctx);
	}

	fimc_hw_set_output_path(ctx);
	fimc_hw_set_out_dma(ctx);

	INIT_LIST_HEAD(&fimc->vid_cap.pending_buf_q);
	INIT_LIST_HEAD(&fimc->vid_cap.active_buf_q);
	fimc->vid_cap.active_buf_cnt = 0;
	fimc->vid_cap.frame_count = 0;
	fimc->vid_cap.buf_index = 0;

	set_bit(ST_CAPT_PEND, &fimc->state);

	return 0;
}

static int stop_streaming(struct vb2_queue *q)
{
	struct fimc_ctx *ctx = q->drv_priv;
	struct fimc_dev *fimc = ctx->fimc_dev;

	if (!fimc_capture_active(fimc))
		return -EINVAL;

	return fimc_stop_capture(fimc);
}

static unsigned int get_plane_size(struct fimc_frame *fr, unsigned int plane)
{
	if (!fr || plane >= fr->fmt->memplanes)
		return 0;
	return fr->f_width * fr->f_height * fr->fmt->depth[plane] / 8;
}

static int queue_setup(struct vb2_queue *vq, unsigned int *num_buffers,
		       unsigned int *num_planes, unsigned long sizes[],
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
	struct v4l2_device *v4l2_dev = &ctx->fimc_dev->m2m.v4l2_dev;
	int i;

	if (!ctx->d_frame.fmt || vq->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	for (i = 0; i < ctx->d_frame.fmt->memplanes; i++) {
		unsigned long size = get_plane_size(&ctx->d_frame, i);

		if (vb2_plane_size(vb, i) < size) {
			v4l2_err(v4l2_dev, "User buffer too small (%ld < %ld)\n",
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

	if (vid_cap->active_buf_cnt >= min_bufs &&
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

static int fimc_capture_open(struct file *file)
{
	struct fimc_dev *fimc = video_drvdata(file);
	int ret = 0;

	dbg("pid: %d, state: 0x%lx", task_pid_nr(current), fimc->state);

	/* Return if the corresponding video mem2mem node is already opened. */
	if (fimc_m2m_active(fimc))
		return -EBUSY;

	if (++fimc->vid_cap.refcnt == 1) {
		ret = fimc_isp_subdev_init(fimc, 0);
		if (ret) {
			fimc->vid_cap.refcnt--;
			return -EIO;
		}
	}

	file->private_data = fimc->vid_cap.ctx;

	return 0;
}

static int fimc_capture_close(struct file *file)
{
	struct fimc_dev *fimc = video_drvdata(file);

	dbg("pid: %d, state: 0x%lx", task_pid_nr(current), fimc->state);

	if (--fimc->vid_cap.refcnt == 0) {
		fimc_stop_capture(fimc);
		vb2_queue_release(&fimc->vid_cap.vbq);

		v4l2_err(&fimc->vid_cap.v4l2_dev, "releasing ISP\n");

		v4l2_subdev_call(fimc->vid_cap.sd, core, s_power, 0);
		clk_disable(fimc->clock[CLK_CAM]);
		fimc_subdev_unregister(fimc);
	}

	return 0;
}

static unsigned int fimc_capture_poll(struct file *file,
				      struct poll_table_struct *wait)
{
	struct fimc_ctx *ctx = file->private_data;
	struct fimc_dev *fimc = ctx->fimc_dev;

	return vb2_poll(&fimc->vid_cap.vbq, file, wait);
}

static int fimc_capture_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct fimc_ctx *ctx = file->private_data;
	struct fimc_dev *fimc = ctx->fimc_dev;

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
	struct fimc_ctx *ctx = file->private_data;
	struct fimc_dev *fimc = ctx->fimc_dev;

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

static int fimc_cap_s_fmt_mplane(struct file *file, void *priv,
				 struct v4l2_format *f)
{
	struct fimc_ctx *ctx = priv;
	struct fimc_dev *fimc = ctx->fimc_dev;
	struct fimc_frame *frame;
	struct v4l2_pix_format_mplane *pix;
	int ret;
	int i;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	ret = fimc_vidioc_try_fmt_mplane(file, priv, f);
	if (ret)
		return ret;

	if (vb2_is_busy(&fimc->vid_cap.vbq) || fimc_capture_active(fimc))
		return -EBUSY;

	frame = &ctx->d_frame;

	pix = &f->fmt.pix_mp;
	frame->fmt = find_format(f, FMT_FLAGS_M2M | FMT_FLAGS_CAM);
	if (!frame->fmt) {
		err("fimc target format not found\n");
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
	struct fimc_ctx *ctx = priv;
	struct s5p_platform_fimc *pldata = ctx->fimc_dev->pdata;
	struct s5p_fimc_isp_info *isp_info;

	if (i->index >= pldata->num_clients)
		return -EINVAL;

	isp_info = &pldata->isp_info[i->index];

	i->type = V4L2_INPUT_TYPE_CAMERA;
	strncpy(i->name, isp_info->board_info->type, 32);
	return 0;
}

static int fimc_cap_s_input(struct file *file, void *priv,
				  unsigned int i)
{
	struct fimc_ctx *ctx = priv;
	struct fimc_dev *fimc = ctx->fimc_dev;
	struct s5p_platform_fimc *pdata = fimc->pdata;

	if (fimc_capture_active(ctx->fimc_dev))
		return -EBUSY;

	if (i >= pdata->num_clients)
		return -EINVAL;


	if (fimc->vid_cap.sd) {
		int ret = v4l2_subdev_call(fimc->vid_cap.sd, core, s_power, 0);
		if (ret)
			err("s_power failed: %d", ret);

		clk_disable(fimc->clock[CLK_CAM]);
	}

	/* Release the attached sensor subdevice. */
	fimc_subdev_unregister(fimc);

	return fimc_isp_subdev_init(fimc, i);
}

static int fimc_cap_g_input(struct file *file, void *priv,
				       unsigned int *i)
{
	struct fimc_ctx *ctx = priv;
	struct fimc_vid_cap *cap = &ctx->fimc_dev->vid_cap;

	*i = cap->input_index;
	return 0;
}

static int fimc_cap_streamon(struct file *file, void *priv,
			     enum v4l2_buf_type type)
{
	struct fimc_ctx *ctx = priv;
	struct fimc_dev *fimc = ctx->fimc_dev;

	if (fimc_capture_active(fimc) || !fimc->vid_cap.sd)
		return -EBUSY;

	if (!(ctx->state & FIMC_DST_FMT)) {
		v4l2_err(&fimc->vid_cap.v4l2_dev, "Format is not set\n");
		return -EINVAL;
	}

	return vb2_streamon(&fimc->vid_cap.vbq, type);
}

static int fimc_cap_streamoff(struct file *file, void *priv,
			    enum v4l2_buf_type type)
{
	struct fimc_ctx *ctx = priv;
	struct fimc_dev *fimc = ctx->fimc_dev;

	return vb2_streamoff(&fimc->vid_cap.vbq, type);
}

static int fimc_cap_reqbufs(struct file *file, void *priv,
			    struct v4l2_requestbuffers *reqbufs)
{
	struct fimc_ctx *ctx = priv;
	struct fimc_vid_cap *cap = &ctx->fimc_dev->vid_cap;
	int ret;


	ret = vb2_reqbufs(&cap->vbq, reqbufs);
	if (!ret)
		cap->reqbufs_count = reqbufs->count;

	return ret;
}

static int fimc_cap_querybuf(struct file *file, void *priv,
			   struct v4l2_buffer *buf)
{
	struct fimc_ctx *ctx = priv;
	struct fimc_vid_cap *cap = &ctx->fimc_dev->vid_cap;

	return vb2_querybuf(&cap->vbq, buf);
}

static int fimc_cap_qbuf(struct file *file, void *priv,
			  struct v4l2_buffer *buf)
{
	struct fimc_ctx *ctx = priv;
	struct fimc_vid_cap *cap = &ctx->fimc_dev->vid_cap;
	return vb2_qbuf(&cap->vbq, buf);
}

static int fimc_cap_dqbuf(struct file *file, void *priv,
			   struct v4l2_buffer *buf)
{
	struct fimc_ctx *ctx = priv;
	return vb2_dqbuf(&ctx->fimc_dev->vid_cap.vbq, buf,
		file->f_flags & O_NONBLOCK);
}

static int fimc_cap_s_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct fimc_ctx *ctx = priv;
	int ret = -EINVAL;

	/* Allow any controls but 90/270 rotation while streaming */
	if (!fimc_capture_active(ctx->fimc_dev) ||
	    ctrl->id != V4L2_CID_ROTATE ||
	    (ctrl->value != 90 && ctrl->value != 270)) {
		ret = check_ctrl_val(ctx, ctrl);
		if (!ret) {
			ret = fimc_s_ctrl(ctx, ctrl);
			if (!ret)
				ctx->state |= FIMC_PARAMS;
		}
	}
	if (ret == -EINVAL)
		ret = v4l2_subdev_call(ctx->fimc_dev->vid_cap.sd,
				       core, s_ctrl, ctrl);
	return ret;
}

static int fimc_cap_cropcap(struct file *file, void *fh,
			    struct v4l2_cropcap *cr)
{
	struct fimc_frame *f;
	struct fimc_ctx *ctx = fh;

	if (cr->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	f = &ctx->s_frame;

	cr->bounds.left		= 0;
	cr->bounds.top		= 0;
	cr->bounds.width	= f->o_width;
	cr->bounds.height	= f->o_height;
	cr->defrect		= cr->bounds;

	return 0;
}

static int fimc_cap_g_crop(struct file *file, void *fh, struct v4l2_crop *cr)
{
	struct fimc_frame *f;
	struct fimc_ctx *ctx = file->private_data;

	f = &ctx->s_frame;

	cr->c.left	= f->offs_h;
	cr->c.top	= f->offs_v;
	cr->c.width	= f->width;
	cr->c.height	= f->height;

	return 0;
}

static int fimc_cap_s_crop(struct file *file, void *fh,
			       struct v4l2_crop *cr)
{
	struct fimc_frame *f;
	struct fimc_ctx *ctx = file->private_data;
	struct fimc_dev *fimc = ctx->fimc_dev;
	int ret = -EINVAL;

	if (fimc_capture_active(fimc))
		return -EBUSY;

	ret = fimc_try_crop(ctx, cr);
	if (ret)
		return ret;

	if (!(ctx->state & FIMC_DST_FMT)) {
		v4l2_err(&fimc->vid_cap.v4l2_dev,
			 "Capture color format not set\n");
		return -EINVAL; /* TODO: make sure this is the right value */
	}

	f = &ctx->s_frame;
	/* Check for the pixel scaling ratio when cropping input image. */
	ret = fimc_check_scaler_ratio(cr->c.width, cr->c.height,
				      ctx->d_frame.width, ctx->d_frame.height,
				      ctx->rotation);
	if (ret) {
		v4l2_err(&fimc->vid_cap.v4l2_dev, "Out of the scaler range\n");
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
	.vidioc_try_fmt_vid_cap_mplane	= fimc_vidioc_try_fmt_mplane,
	.vidioc_s_fmt_vid_cap_mplane	= fimc_cap_s_fmt_mplane,
	.vidioc_g_fmt_vid_cap_mplane	= fimc_vidioc_g_fmt_mplane,

	.vidioc_reqbufs			= fimc_cap_reqbufs,
	.vidioc_querybuf		= fimc_cap_querybuf,

	.vidioc_qbuf			= fimc_cap_qbuf,
	.vidioc_dqbuf			= fimc_cap_dqbuf,

	.vidioc_streamon		= fimc_cap_streamon,
	.vidioc_streamoff		= fimc_cap_streamoff,

	.vidioc_queryctrl		= fimc_vidioc_queryctrl,
	.vidioc_g_ctrl			= fimc_vidioc_g_ctrl,
	.vidioc_s_ctrl			= fimc_cap_s_ctrl,

	.vidioc_g_crop			= fimc_cap_g_crop,
	.vidioc_s_crop			= fimc_cap_s_crop,
	.vidioc_cropcap			= fimc_cap_cropcap,

	.vidioc_enum_input		= fimc_cap_enum_input,
	.vidioc_s_input			= fimc_cap_s_input,
	.vidioc_g_input			= fimc_cap_g_input,
};

/* fimc->lock must be already initialized */
int fimc_register_capture_device(struct fimc_dev *fimc)
{
	struct v4l2_device *v4l2_dev = &fimc->vid_cap.v4l2_dev;
	struct video_device *vfd;
	struct fimc_vid_cap *vid_cap;
	struct fimc_ctx *ctx;
	struct v4l2_format f;
	struct fimc_frame *fr;
	struct vb2_queue *q;
	int ret;

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

	if (!v4l2_dev->name[0])
		snprintf(v4l2_dev->name, sizeof(v4l2_dev->name),
			 "%s.capture", dev_name(&fimc->pdev->dev));

	ret = v4l2_device_register(NULL, v4l2_dev);
	if (ret)
		goto err_info;

	vfd = video_device_alloc();
	if (!vfd) {
		v4l2_err(v4l2_dev, "Failed to allocate video device\n");
		goto err_v4l2_reg;
	}

	snprintf(vfd->name, sizeof(vfd->name), "%s:cap",
		 dev_name(&fimc->pdev->dev));

	vfd->fops	= &fimc_capture_fops;
	vfd->ioctl_ops	= &fimc_capture_ioctl_ops;
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

	ret = video_register_device(vfd, VFL_TYPE_GRABBER, -1);
	if (ret) {
		v4l2_err(v4l2_dev, "Failed to register video device\n");
		goto err_vd_reg;
	}

	v4l2_info(v4l2_dev,
		  "FIMC capture driver registered as /dev/video%d\n",
		  vfd->num);

	return 0;

err_vd_reg:
	video_device_release(vfd);
err_v4l2_reg:
	v4l2_device_unregister(v4l2_dev);
err_info:
	kfree(ctx);
	dev_err(&fimc->pdev->dev, "failed to install\n");
	return ret;
}

void fimc_unregister_capture_device(struct fimc_dev *fimc)
{
	struct fimc_vid_cap *capture = &fimc->vid_cap;

	if (capture->vfd)
		video_unregister_device(capture->vfd);

	kfree(capture->ctx);
}
