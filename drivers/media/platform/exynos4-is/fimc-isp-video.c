/*
 * Samsung EXYNOS4x12 FIMC-IS (Imaging Subsystem) driver
 *
 * FIMC-IS ISP video input and video output DMA interface driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 * Author: Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * The hardware handling code derived from a driver written by
 * Younghwan Joo <yhwan.joo@samsung.com>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/printk.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/videodev2.h>

#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>
#include <media/exynos-fimc.h>

#include "common.h"
#include "media-dev.h"
#include "fimc-is.h"
#include "fimc-isp-video.h"
#include "fimc-is-param.h"

static int isp_video_capture_queue_setup(struct vb2_queue *vq,
			const void *parg,
			unsigned int *num_buffers, unsigned int *num_planes,
			unsigned int sizes[], void *allocators[])
{
	const struct v4l2_format *pfmt = parg;
	struct fimc_isp *isp = vb2_get_drv_priv(vq);
	struct v4l2_pix_format_mplane *vid_fmt = &isp->video_capture.pixfmt;
	const struct v4l2_pix_format_mplane *pixm = NULL;
	const struct fimc_fmt *fmt;
	unsigned int wh, i;

	if (pfmt) {
		pixm = &pfmt->fmt.pix_mp;
		fmt = fimc_isp_find_format(&pixm->pixelformat, NULL, -1);
		wh = pixm->width * pixm->height;
	} else {
		fmt = isp->video_capture.format;
		wh = vid_fmt->width * vid_fmt->height;
	}

	if (fmt == NULL)
		return -EINVAL;

	*num_buffers = clamp_t(u32, *num_buffers, FIMC_ISP_REQ_BUFS_MIN,
						FIMC_ISP_REQ_BUFS_MAX);
	*num_planes = fmt->memplanes;

	for (i = 0; i < fmt->memplanes; i++) {
		unsigned int size = (wh * fmt->depth[i]) / 8;
		if (pixm)
			sizes[i] = max(size, pixm->plane_fmt[i].sizeimage);
		else
			sizes[i] = size;
		allocators[i] = isp->alloc_ctx;
	}

	return 0;
}

static inline struct param_dma_output *__get_isp_dma2(struct fimc_is *is)
{
	return &__get_curr_is_config(is)->isp.dma2_output;
}

static int isp_video_capture_start_streaming(struct vb2_queue *q,
						unsigned int count)
{
	struct fimc_isp *isp = vb2_get_drv_priv(q);
	struct fimc_is *is = fimc_isp_to_is(isp);
	struct param_dma_output *dma = __get_isp_dma2(is);
	struct fimc_is_video *video = &isp->video_capture;
	int ret;

	if (!test_bit(ST_ISP_VID_CAP_BUF_PREP, &isp->state) ||
	    test_bit(ST_ISP_VID_CAP_STREAMING, &isp->state))
		return 0;


	dma->cmd = DMA_OUTPUT_COMMAND_ENABLE;
	dma->notify_dma_done = DMA_OUTPUT_NOTIFY_DMA_DONE_ENABLE;
	dma->buffer_address = is->is_dma_p_region +
				DMA2_OUTPUT_ADDR_ARRAY_OFFS;
	dma->buffer_number = video->reqbufs_count;
	dma->dma_out_mask = video->buf_mask;

	isp_dbg(2, &video->ve.vdev,
		"buf_count: %d, planes: %d, dma addr table: %#x\n",
		video->buf_count, video->format->memplanes,
		dma->buffer_address);

	fimc_is_mem_barrier();

	fimc_is_set_param_bit(is, PARAM_ISP_DMA2_OUTPUT);
	__fimc_is_hw_update_param(is, PARAM_ISP_DMA2_OUTPUT);

	ret = fimc_is_itf_s_param(is, false);
	if (ret < 0)
		return ret;

	ret = fimc_pipeline_call(&video->ve, set_stream, 1);
	if (ret < 0)
		return ret;

	set_bit(ST_ISP_VID_CAP_STREAMING, &isp->state);
	return ret;
}

static void isp_video_capture_stop_streaming(struct vb2_queue *q)
{
	struct fimc_isp *isp = vb2_get_drv_priv(q);
	struct fimc_is *is = fimc_isp_to_is(isp);
	struct param_dma_output *dma = __get_isp_dma2(is);
	int ret;

	ret = fimc_pipeline_call(&isp->video_capture.ve, set_stream, 0);
	if (ret < 0)
		return;

	dma->cmd = DMA_OUTPUT_COMMAND_DISABLE;
	dma->notify_dma_done = DMA_OUTPUT_NOTIFY_DMA_DONE_DISABLE;
	dma->buffer_number = 0;
	dma->buffer_address = 0;
	dma->dma_out_mask = 0;

	fimc_is_set_param_bit(is, PARAM_ISP_DMA2_OUTPUT);
	__fimc_is_hw_update_param(is, PARAM_ISP_DMA2_OUTPUT);

	ret = fimc_is_itf_s_param(is, false);
	if (ret < 0)
		dev_warn(&is->pdev->dev, "%s: DMA stop failed\n", __func__);

	fimc_is_hw_set_isp_buf_mask(is, 0);

	clear_bit(ST_ISP_VID_CAP_BUF_PREP, &isp->state);
	clear_bit(ST_ISP_VID_CAP_STREAMING, &isp->state);

	isp->video_capture.buf_count = 0;
}

static int isp_video_capture_buffer_prepare(struct vb2_buffer *vb)
{
	struct fimc_isp *isp = vb2_get_drv_priv(vb->vb2_queue);
	struct fimc_is_video *video = &isp->video_capture;
	int i;

	if (video->format == NULL)
		return -EINVAL;

	for (i = 0; i < video->format->memplanes; i++) {
		unsigned long size = video->pixfmt.plane_fmt[i].sizeimage;

		if (vb2_plane_size(vb, i) < size) {
			v4l2_err(&video->ve.vdev,
				 "User buffer too small (%ld < %ld)\n",
				 vb2_plane_size(vb, i), size);
			return -EINVAL;
		}
		vb2_set_plane_payload(vb, i, size);
	}

	/* Check if we get one of the already known buffers. */
	if (test_bit(ST_ISP_VID_CAP_BUF_PREP, &isp->state)) {
		dma_addr_t dma_addr = vb2_dma_contig_plane_dma_addr(vb, 0);
		int i;

		for (i = 0; i < video->buf_count; i++)
			if (video->buffers[i]->dma_addr[0] == dma_addr)
				return 0;
		return -ENXIO;
	}

	return 0;
}

static void isp_video_capture_buffer_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct fimc_isp *isp = vb2_get_drv_priv(vb->vb2_queue);
	struct fimc_is_video *video = &isp->video_capture;
	struct fimc_is *is = fimc_isp_to_is(isp);
	struct isp_video_buf *ivb = to_isp_video_buf(vbuf);
	unsigned long flags;
	unsigned int i;

	if (test_bit(ST_ISP_VID_CAP_BUF_PREP, &isp->state)) {
		spin_lock_irqsave(&is->slock, flags);
		video->buf_mask |= BIT(ivb->index);
		spin_unlock_irqrestore(&is->slock, flags);
	} else {
		unsigned int num_planes = video->format->memplanes;

		ivb->index = video->buf_count;
		video->buffers[ivb->index] = ivb;

		for (i = 0; i < num_planes; i++) {
			int buf_index = ivb->index * num_planes + i;

			ivb->dma_addr[i] = vb2_dma_contig_plane_dma_addr(vb, i);
			is->is_p_region->shared[32 + buf_index] =
							ivb->dma_addr[i];

			isp_dbg(2, &video->ve.vdev,
				"dma_buf %pad (%d/%d/%d) addr: %pad\n",
				&buf_index, ivb->index, i, vb->index,
				&ivb->dma_addr[i]);
		}

		if (++video->buf_count < video->reqbufs_count)
			return;

		video->buf_mask = (1UL << video->buf_count) - 1;
		set_bit(ST_ISP_VID_CAP_BUF_PREP, &isp->state);
	}

	if (!test_bit(ST_ISP_VID_CAP_STREAMING, &isp->state))
		isp_video_capture_start_streaming(vb->vb2_queue, 0);
}

/*
 * FIMC-IS ISP input and output DMA interface interrupt handler.
 * Locking: called with is->slock spinlock held.
 */
void fimc_isp_video_irq_handler(struct fimc_is *is)
{
	struct fimc_is_video *video = &is->isp.video_capture;
	struct vb2_v4l2_buffer *vbuf;
	int buf_index;

	/* TODO: Ensure the DMA is really stopped in stop_streaming callback */
	if (!test_bit(ST_ISP_VID_CAP_STREAMING, &is->isp.state))
		return;

	buf_index = (is->i2h_cmd.args[1] - 1) % video->buf_count;
	vbuf = &video->buffers[buf_index]->vb;

	v4l2_get_timestamp(&vbuf->timestamp);
	vb2_buffer_done(&vbuf->vb2_buf, VB2_BUF_STATE_DONE);

	video->buf_mask &= ~BIT(buf_index);
	fimc_is_hw_set_isp_buf_mask(is, video->buf_mask);
}

static const struct vb2_ops isp_video_capture_qops = {
	.queue_setup	 = isp_video_capture_queue_setup,
	.buf_prepare	 = isp_video_capture_buffer_prepare,
	.buf_queue	 = isp_video_capture_buffer_queue,
	.wait_prepare	 = vb2_ops_wait_prepare,
	.wait_finish	 = vb2_ops_wait_finish,
	.start_streaming = isp_video_capture_start_streaming,
	.stop_streaming	 = isp_video_capture_stop_streaming,
};

static int isp_video_open(struct file *file)
{
	struct fimc_isp *isp = video_drvdata(file);
	struct exynos_video_entity *ve = &isp->video_capture.ve;
	struct media_entity *me = &ve->vdev.entity;
	int ret;

	if (mutex_lock_interruptible(&isp->video_lock))
		return -ERESTARTSYS;

	ret = v4l2_fh_open(file);
	if (ret < 0)
		goto unlock;

	ret = pm_runtime_get_sync(&isp->pdev->dev);
	if (ret < 0)
		goto rel_fh;

	if (v4l2_fh_is_singular_file(file)) {
		mutex_lock(&me->parent->graph_mutex);

		ret = fimc_pipeline_call(ve, open, me, true);

		/* Mark the video pipeline as in use. */
		if (ret == 0)
			me->use_count++;

		mutex_unlock(&me->parent->graph_mutex);
	}
	if (!ret)
		goto unlock;
rel_fh:
	v4l2_fh_release(file);
unlock:
	mutex_unlock(&isp->video_lock);
	return ret;
}

static int isp_video_release(struct file *file)
{
	struct fimc_isp *isp = video_drvdata(file);
	struct fimc_is_video *ivc = &isp->video_capture;
	struct media_entity *entity = &ivc->ve.vdev.entity;
	struct media_device *mdev = entity->parent;

	mutex_lock(&isp->video_lock);

	if (v4l2_fh_is_singular_file(file) && ivc->streaming) {
		media_entity_pipeline_stop(entity);
		ivc->streaming = 0;
	}

	vb2_fop_release(file);

	if (v4l2_fh_is_singular_file(file)) {
		fimc_pipeline_call(&ivc->ve, close);

		mutex_lock(&mdev->graph_mutex);
		entity->use_count--;
		mutex_unlock(&mdev->graph_mutex);
	}

	pm_runtime_put(&isp->pdev->dev);
	mutex_unlock(&isp->video_lock);

	return 0;
}

static const struct v4l2_file_operations isp_video_fops = {
	.owner		= THIS_MODULE,
	.open		= isp_video_open,
	.release	= isp_video_release,
	.poll		= vb2_fop_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= vb2_fop_mmap,
};

/*
 * Video node ioctl operations
 */
static int isp_video_querycap(struct file *file, void *priv,
					struct v4l2_capability *cap)
{
	struct fimc_isp *isp = video_drvdata(file);

	__fimc_vidioc_querycap(&isp->pdev->dev, cap, V4L2_CAP_STREAMING);
	return 0;
}

static int isp_video_enum_fmt_mplane(struct file *file, void *priv,
					struct v4l2_fmtdesc *f)
{
	const struct fimc_fmt *fmt;

	if (f->index >= FIMC_ISP_NUM_FORMATS)
		return -EINVAL;

	fmt = fimc_isp_find_format(NULL, NULL, f->index);
	if (WARN_ON(fmt == NULL))
		return -EINVAL;

	strlcpy(f->description, fmt->name, sizeof(f->description));
	f->pixelformat = fmt->fourcc;

	return 0;
}

static int isp_video_g_fmt_mplane(struct file *file, void *fh,
					struct v4l2_format *f)
{
	struct fimc_isp *isp = video_drvdata(file);

	f->fmt.pix_mp = isp->video_capture.pixfmt;
	return 0;
}

static void __isp_video_try_fmt(struct fimc_isp *isp,
				struct v4l2_pix_format_mplane *pixm,
				const struct fimc_fmt **fmt)
{
	const struct fimc_fmt *__fmt;

	__fmt = fimc_isp_find_format(&pixm->pixelformat, NULL, 2);

	if (fmt)
		*fmt = __fmt;

	pixm->colorspace = V4L2_COLORSPACE_SRGB;
	pixm->field = V4L2_FIELD_NONE;
	pixm->num_planes = __fmt->memplanes;
	pixm->pixelformat = __fmt->fourcc;
	/*
	 * TODO: double check with the docmentation these width/height
	 * constraints are correct.
	 */
	v4l_bound_align_image(&pixm->width, FIMC_ISP_SOURCE_WIDTH_MIN,
			      FIMC_ISP_SOURCE_WIDTH_MAX, 3,
			      &pixm->height, FIMC_ISP_SOURCE_HEIGHT_MIN,
			      FIMC_ISP_SOURCE_HEIGHT_MAX, 0, 0);
}

static int isp_video_try_fmt_mplane(struct file *file, void *fh,
					struct v4l2_format *f)
{
	struct fimc_isp *isp = video_drvdata(file);

	__isp_video_try_fmt(isp, &f->fmt.pix_mp, NULL);
	return 0;
}

static int isp_video_s_fmt_mplane(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct fimc_isp *isp = video_drvdata(file);
	struct fimc_is *is = fimc_isp_to_is(isp);
	struct v4l2_pix_format_mplane *pixm = &f->fmt.pix_mp;
	const struct fimc_fmt *ifmt = NULL;
	struct param_dma_output *dma = __get_isp_dma2(is);

	__isp_video_try_fmt(isp, pixm, &ifmt);

	if (WARN_ON(ifmt == NULL))
		return -EINVAL;

	dma->format = DMA_OUTPUT_FORMAT_BAYER;
	dma->order = DMA_OUTPUT_ORDER_GB_BG;
	dma->plane = ifmt->memplanes;
	dma->bitwidth = ifmt->depth[0];
	dma->width = pixm->width;
	dma->height = pixm->height;

	fimc_is_mem_barrier();

	isp->video_capture.format = ifmt;
	isp->video_capture.pixfmt = *pixm;

	return 0;
}

/*
 * Check for source/sink format differences at each link.
 * Return 0 if the formats match or -EPIPE otherwise.
 */
static int isp_video_pipeline_validate(struct fimc_isp *isp)
{
	struct v4l2_subdev *sd = &isp->subdev;
	struct v4l2_subdev_format sink_fmt, src_fmt;
	struct media_pad *pad;
	int ret;

	while (1) {
		/* Retrieve format at the sink pad */
		pad = &sd->entity.pads[0];
		if (!(pad->flags & MEDIA_PAD_FL_SINK))
			break;
		sink_fmt.pad = pad->index;
		sink_fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
		ret = v4l2_subdev_call(sd, pad, get_fmt, NULL, &sink_fmt);
		if (ret < 0 && ret != -ENOIOCTLCMD)
			return -EPIPE;

		/* Retrieve format at the source pad */
		pad = media_entity_remote_pad(pad);
		if (pad == NULL ||
		    media_entity_type(pad->entity) != MEDIA_ENT_T_V4L2_SUBDEV)
			break;

		sd = media_entity_to_v4l2_subdev(pad->entity);
		src_fmt.pad = pad->index;
		src_fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
		ret = v4l2_subdev_call(sd, pad, get_fmt, NULL, &src_fmt);
		if (ret < 0 && ret != -ENOIOCTLCMD)
			return -EPIPE;

		if (src_fmt.format.width != sink_fmt.format.width ||
		    src_fmt.format.height != sink_fmt.format.height ||
		    src_fmt.format.code != sink_fmt.format.code)
			return -EPIPE;
	}

	return 0;
}

static int isp_video_streamon(struct file *file, void *priv,
				      enum v4l2_buf_type type)
{
	struct fimc_isp *isp = video_drvdata(file);
	struct exynos_video_entity *ve = &isp->video_capture.ve;
	struct media_entity *me = &ve->vdev.entity;
	int ret;

	ret = media_entity_pipeline_start(me, &ve->pipe->mp);
	if (ret < 0)
		return ret;

	ret = isp_video_pipeline_validate(isp);
	if (ret < 0)
		goto p_stop;

	ret = vb2_ioctl_streamon(file, priv, type);
	if (ret < 0)
		goto p_stop;

	isp->video_capture.streaming = 1;
	return 0;
p_stop:
	media_entity_pipeline_stop(me);
	return ret;
}

static int isp_video_streamoff(struct file *file, void *priv,
					enum v4l2_buf_type type)
{
	struct fimc_isp *isp = video_drvdata(file);
	struct fimc_is_video *video = &isp->video_capture;
	int ret;

	ret = vb2_ioctl_streamoff(file, priv, type);
	if (ret < 0)
		return ret;

	media_entity_pipeline_stop(&video->ve.vdev.entity);
	video->streaming = 0;
	return 0;
}

static int isp_video_reqbufs(struct file *file, void *priv,
				struct v4l2_requestbuffers *rb)
{
	struct fimc_isp *isp = video_drvdata(file);
	int ret;

	ret = vb2_ioctl_reqbufs(file, priv, rb);
	if (ret < 0)
		return ret;

	if (rb->count && rb->count < FIMC_ISP_REQ_BUFS_MIN) {
		rb->count = 0;
		vb2_ioctl_reqbufs(file, priv, rb);
		ret = -ENOMEM;
	}

	isp->video_capture.reqbufs_count = rb->count;
	return ret;
}

static const struct v4l2_ioctl_ops isp_video_ioctl_ops = {
	.vidioc_querycap		= isp_video_querycap,
	.vidioc_enum_fmt_vid_cap_mplane	= isp_video_enum_fmt_mplane,
	.vidioc_try_fmt_vid_cap_mplane	= isp_video_try_fmt_mplane,
	.vidioc_s_fmt_vid_cap_mplane	= isp_video_s_fmt_mplane,
	.vidioc_g_fmt_vid_cap_mplane	= isp_video_g_fmt_mplane,
	.vidioc_reqbufs			= isp_video_reqbufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_streamon		= isp_video_streamon,
	.vidioc_streamoff		= isp_video_streamoff,
};

int fimc_isp_video_device_register(struct fimc_isp *isp,
				   struct v4l2_device *v4l2_dev,
				   enum v4l2_buf_type type)
{
	struct vb2_queue *q = &isp->video_capture.vb_queue;
	struct fimc_is_video *iv;
	struct video_device *vdev;
	int ret;

	if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		iv = &isp->video_capture;
	else
		return -ENOSYS;

	mutex_init(&isp->video_lock);
	INIT_LIST_HEAD(&iv->pending_buf_q);
	INIT_LIST_HEAD(&iv->active_buf_q);
	iv->format = fimc_isp_find_format(NULL, NULL, 0);
	iv->pixfmt.width = IS_DEFAULT_WIDTH;
	iv->pixfmt.height = IS_DEFAULT_HEIGHT;
	iv->pixfmt.pixelformat = iv->format->fourcc;
	iv->pixfmt.colorspace = V4L2_COLORSPACE_SRGB;
	iv->reqbufs_count = 0;

	memset(q, 0, sizeof(*q));
	q->type = type;
	q->io_modes = VB2_MMAP | VB2_USERPTR;
	q->ops = &isp_video_capture_qops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->buf_struct_size = sizeof(struct isp_video_buf);
	q->drv_priv = isp;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &isp->video_lock;

	ret = vb2_queue_init(q);
	if (ret < 0)
		return ret;

	vdev = &iv->ve.vdev;
	memset(vdev, 0, sizeof(*vdev));
	snprintf(vdev->name, sizeof(vdev->name), "fimc-is-isp.%s",
			type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ?
			"capture" : "output");
	vdev->queue = q;
	vdev->fops = &isp_video_fops;
	vdev->ioctl_ops = &isp_video_ioctl_ops;
	vdev->v4l2_dev = v4l2_dev;
	vdev->minor = -1;
	vdev->release = video_device_release_empty;
	vdev->lock = &isp->video_lock;

	iv->pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_init(&vdev->entity, 1, &iv->pad, 0);
	if (ret < 0)
		return ret;

	video_set_drvdata(vdev, isp);

	ret = video_register_device(vdev, VFL_TYPE_GRABBER, -1);
	if (ret < 0) {
		media_entity_cleanup(&vdev->entity);
		return ret;
	}

	v4l2_info(v4l2_dev, "Registered %s as /dev/%s\n",
		  vdev->name, video_device_node_name(vdev));

	return 0;
}

void fimc_isp_video_device_unregister(struct fimc_isp *isp,
				      enum v4l2_buf_type type)
{
	struct exynos_video_entity *ve;

	if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		ve = &isp->video_capture.ve;
	else
		return;

	mutex_lock(&isp->video_lock);

	if (video_is_registered(&ve->vdev)) {
		video_unregister_device(&ve->vdev);
		media_entity_cleanup(&ve->vdev.entity);
		ve->pipe = NULL;
	}

	mutex_unlock(&isp->video_lock);
}
