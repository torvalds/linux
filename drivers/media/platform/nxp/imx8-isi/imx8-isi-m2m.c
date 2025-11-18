// SPDX-License-Identifier: GPL-2.0
/*
 * ISI V4L2 memory to memory driver for i.MX8QXP/QM platform
 *
 * ISI is a Image Sensor Interface of i.MX8QXP/QM platform, which
 * used to process image from camera sensor or memory to memory or DC
 *
 * Copyright (c) 2019 NXP Semiconductor
 */

#include <linux/container_of.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/minmax.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/videodev2.h>

#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include "imx8-isi-core.h"

struct mxc_isi_m2m_buffer {
	struct v4l2_m2m_buffer buf;
	dma_addr_t dma_addrs[3];
};

struct mxc_isi_m2m_ctx_queue_data {
	struct v4l2_pix_format_mplane format;
	const struct mxc_isi_format_info *info;
	u32 sequence;
};

struct mxc_isi_m2m_ctx {
	struct v4l2_fh fh;
	struct mxc_isi_m2m *m2m;

	/* Protects the m2m vb2 queues */
	struct mutex vb2_lock;

	struct {
		struct mxc_isi_m2m_ctx_queue_data out;
		struct mxc_isi_m2m_ctx_queue_data cap;
	} queues;

	struct {
		struct v4l2_ctrl_handler handler;
		unsigned int alpha;
		bool hflip;
		bool vflip;
	} ctrls;

	bool chained;
};

static inline struct mxc_isi_m2m_buffer *
to_isi_m2m_buffer(struct vb2_v4l2_buffer *buf)
{
	return container_of(buf, struct mxc_isi_m2m_buffer, buf.vb);
}

static inline struct mxc_isi_m2m_ctx *file_to_isi_m2m_ctx(struct file *filp)
{
	return container_of(file_to_v4l2_fh(filp), struct mxc_isi_m2m_ctx, fh);
}

static inline struct mxc_isi_m2m_ctx_queue_data *
mxc_isi_m2m_ctx_qdata(struct mxc_isi_m2m_ctx *ctx, enum v4l2_buf_type type)
{
	if (V4L2_TYPE_IS_OUTPUT(type))
		return &ctx->queues.out;
	else
		return &ctx->queues.cap;
}

/* -----------------------------------------------------------------------------
 * V4L2 M2M device operations
 */

static void mxc_isi_m2m_frame_write_done(struct mxc_isi_pipe *pipe, u32 status)
{
	struct mxc_isi_m2m *m2m = &pipe->isi->m2m;
	struct vb2_v4l2_buffer *src_vbuf, *dst_vbuf;
	struct mxc_isi_m2m_ctx *ctx;

	ctx = v4l2_m2m_get_curr_priv(m2m->m2m_dev);
	if (!ctx) {
		dev_err(m2m->isi->dev,
			"Instance released before the end of transaction\n");
		return;
	}

	src_vbuf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	dst_vbuf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);

	v4l2_m2m_buf_copy_metadata(src_vbuf, dst_vbuf, false);

	src_vbuf->sequence = ctx->queues.out.sequence++;
	dst_vbuf->sequence = ctx->queues.cap.sequence++;

	v4l2_m2m_buf_done(src_vbuf, VB2_BUF_STATE_DONE);
	v4l2_m2m_buf_done(dst_vbuf, VB2_BUF_STATE_DONE);

	v4l2_m2m_job_finish(m2m->m2m_dev, ctx->fh.m2m_ctx);
}

static void mxc_isi_m2m_device_run(void *priv)
{
	struct mxc_isi_m2m_ctx *ctx = priv;
	struct mxc_isi_m2m *m2m = ctx->m2m;
	struct vb2_v4l2_buffer *src_vbuf, *dst_vbuf;
	struct mxc_isi_m2m_buffer *src_buf, *dst_buf;

	mxc_isi_channel_disable(m2m->pipe);

	mutex_lock(&m2m->lock);

	/* If the context has changed, reconfigure the channel. */
	if (m2m->last_ctx != ctx) {
		const struct v4l2_area in_size = {
			.width = ctx->queues.out.format.width,
			.height = ctx->queues.out.format.height,
		};
		const struct v4l2_area scale = {
			.width = ctx->queues.cap.format.width,
			.height = ctx->queues.cap.format.height,
		};
		const struct v4l2_rect crop = {
			.width = ctx->queues.cap.format.width,
			.height = ctx->queues.cap.format.height,
		};

		mxc_isi_channel_config(m2m->pipe, MXC_ISI_INPUT_MEM,
				       &in_size, &scale, &crop,
				       ctx->queues.out.info->encoding,
				       ctx->queues.cap.info->encoding);
		mxc_isi_channel_set_input_format(m2m->pipe,
						 ctx->queues.out.info,
						 &ctx->queues.out.format);
		mxc_isi_channel_set_output_format(m2m->pipe,
						  ctx->queues.cap.info,
						  &ctx->queues.cap.format);

		m2m->last_ctx = ctx;
	}

	mutex_unlock(&m2m->lock);

	mutex_lock(ctx->ctrls.handler.lock);
	mxc_isi_channel_set_alpha(m2m->pipe, ctx->ctrls.alpha);
	mxc_isi_channel_set_flip(m2m->pipe, ctx->ctrls.hflip, ctx->ctrls.vflip);
	mutex_unlock(ctx->ctrls.handler.lock);

	src_vbuf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	dst_vbuf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);

	src_buf = to_isi_m2m_buffer(src_vbuf);
	dst_buf = to_isi_m2m_buffer(dst_vbuf);

	mxc_isi_channel_set_inbuf(m2m->pipe, src_buf->dma_addrs[0]);
	mxc_isi_channel_set_outbuf(m2m->pipe, dst_buf->dma_addrs, MXC_ISI_BUF1);
	mxc_isi_channel_set_outbuf(m2m->pipe, dst_buf->dma_addrs, MXC_ISI_BUF2);

	mxc_isi_channel_enable(m2m->pipe);

	mxc_isi_channel_m2m_start(m2m->pipe);
}

static const struct v4l2_m2m_ops mxc_isi_m2m_ops = {
	.device_run = mxc_isi_m2m_device_run,
};

/* -----------------------------------------------------------------------------
 * videobuf2 queue operations
 */

static int mxc_isi_m2m_vb2_queue_setup(struct vb2_queue *q,
				       unsigned int *num_buffers,
				       unsigned int *num_planes,
				       unsigned int sizes[],
				       struct device *alloc_devs[])
{
	struct mxc_isi_m2m_ctx *ctx = vb2_get_drv_priv(q);
	const struct mxc_isi_m2m_ctx_queue_data *qdata =
		mxc_isi_m2m_ctx_qdata(ctx, q->type);

	return mxc_isi_video_queue_setup(&qdata->format, qdata->info,
					 num_buffers, num_planes, sizes);
}

static int mxc_isi_m2m_vb2_buffer_init(struct vb2_buffer *vb2)
{
	struct vb2_queue *vq = vb2->vb2_queue;
	struct mxc_isi_m2m_buffer *buf = to_isi_m2m_buffer(to_vb2_v4l2_buffer(vb2));
	struct mxc_isi_m2m_ctx *ctx = vb2_get_drv_priv(vb2->vb2_queue);
	const struct mxc_isi_m2m_ctx_queue_data *qdata =
		mxc_isi_m2m_ctx_qdata(ctx, vq->type);

	mxc_isi_video_buffer_init(vb2, buf->dma_addrs, qdata->info,
				  &qdata->format);

	return 0;
}

static int mxc_isi_m2m_vb2_buffer_prepare(struct vb2_buffer *vb2)
{
	struct vb2_queue *vq = vb2->vb2_queue;
	struct mxc_isi_m2m_ctx *ctx = vb2_get_drv_priv(vq);
	const struct mxc_isi_m2m_ctx_queue_data *qdata =
		mxc_isi_m2m_ctx_qdata(ctx, vq->type);

	return mxc_isi_video_buffer_prepare(ctx->m2m->isi, vb2, qdata->info,
					    &qdata->format);
}

static void mxc_isi_m2m_vb2_buffer_queue(struct vb2_buffer *vb2)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb2);
	struct mxc_isi_m2m_ctx *ctx = vb2_get_drv_priv(vb2->vb2_queue);

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
}

static int mxc_isi_m2m_vb2_prepare_streaming(struct vb2_queue *q)
{
	struct mxc_isi_m2m_ctx *ctx = vb2_get_drv_priv(q);
	const struct v4l2_pix_format_mplane *out_pix = &ctx->queues.out.format;
	const struct v4l2_pix_format_mplane *cap_pix = &ctx->queues.cap.format;
	const struct mxc_isi_format_info *cap_info = ctx->queues.cap.info;
	const struct mxc_isi_format_info *out_info = ctx->queues.out.info;
	struct mxc_isi_m2m *m2m = ctx->m2m;
	int ret;

	guard(mutex)(&m2m->lock);

	if (m2m->usage_count == INT_MAX)
		return -EOVERFLOW;

	ret = pm_runtime_resume_and_get(m2m->isi->dev);
	if (ret)
		return ret;

	/*
	 * Acquire the pipe and initialize the channel with the first user of
	 * the M2M device.
	 */
	if (m2m->usage_count == 0) {
		bool bypass = cap_pix->width == out_pix->width &&
			      cap_pix->height == out_pix->height &&
			      cap_info->encoding == out_info->encoding;

		ret = mxc_isi_channel_acquire(m2m->pipe,
					      &mxc_isi_m2m_frame_write_done,
					      bypass);
		if (ret)
			goto err_pm;

		mxc_isi_channel_get(m2m->pipe);
	}

	m2m->usage_count++;

	/*
	 * Allocate resources for the channel, counting how many users require
	 * buffer chaining.
	 */
	if (!ctx->chained && out_pix->width > MXC_ISI_MAX_WIDTH_UNCHAINED) {
		ret = mxc_isi_channel_chain(m2m->pipe);
		if (ret)
			goto err_deinit;

		m2m->chained_count++;
		ctx->chained = true;
	}

	return 0;

err_deinit:
	if (--m2m->usage_count == 0) {
		mxc_isi_channel_put(m2m->pipe);
		mxc_isi_channel_release(m2m->pipe);
	}
err_pm:
	pm_runtime_put(m2m->isi->dev);
	return ret;
}

static int mxc_isi_m2m_vb2_start_streaming(struct vb2_queue *q,
					   unsigned int count)
{
	struct mxc_isi_m2m_ctx *ctx = vb2_get_drv_priv(q);
	struct mxc_isi_m2m_ctx_queue_data *qdata =
		mxc_isi_m2m_ctx_qdata(ctx, q->type);

	qdata->sequence = 0;

	return 0;
}

static void mxc_isi_m2m_vb2_stop_streaming(struct vb2_queue *q)
{
	struct mxc_isi_m2m_ctx *ctx = vb2_get_drv_priv(q);
	struct vb2_v4l2_buffer *vbuf;

	for (;;) {
		if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
			vbuf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		else
			vbuf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
		if (!vbuf)
			break;

		v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_ERROR);
	}
}

static void mxc_isi_m2m_vb2_unprepare_streaming(struct vb2_queue *q)
{
	struct mxc_isi_m2m_ctx *ctx = vb2_get_drv_priv(q);
	struct mxc_isi_m2m *m2m = ctx->m2m;

	guard(mutex)(&m2m->lock);

	/*
	 * If the last context is this one, reset it to make sure the device
	 * will be reconfigured when streaming is restarted.
	 */
	if (m2m->last_ctx == ctx)
		m2m->last_ctx = NULL;

	/* Free the channel resources if this is the last chained context. */
	if (ctx->chained && --m2m->chained_count == 0)
		mxc_isi_channel_unchain(m2m->pipe);
	ctx->chained = false;

	/* Turn off the light with the last user. */
	if (--m2m->usage_count == 0) {
		mxc_isi_channel_disable(m2m->pipe);
		mxc_isi_channel_put(m2m->pipe);
		mxc_isi_channel_release(m2m->pipe);
	}

	WARN_ON(m2m->usage_count < 0);

	pm_runtime_put(m2m->isi->dev);
}

static const struct vb2_ops mxc_isi_m2m_vb2_qops = {
	.queue_setup		= mxc_isi_m2m_vb2_queue_setup,
	.buf_init		= mxc_isi_m2m_vb2_buffer_init,
	.buf_prepare		= mxc_isi_m2m_vb2_buffer_prepare,
	.buf_queue		= mxc_isi_m2m_vb2_buffer_queue,
	.prepare_streaming	= mxc_isi_m2m_vb2_prepare_streaming,
	.start_streaming	= mxc_isi_m2m_vb2_start_streaming,
	.stop_streaming		= mxc_isi_m2m_vb2_stop_streaming,
	.unprepare_streaming	= mxc_isi_m2m_vb2_unprepare_streaming,
};

static int mxc_isi_m2m_queue_init(void *priv, struct vb2_queue *src_vq,
				  struct vb2_queue *dst_vq)
{
	struct mxc_isi_m2m_ctx *ctx = priv;
	struct mxc_isi_m2m *m2m = ctx->m2m;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct mxc_isi_m2m_buffer);
	src_vq->ops = &mxc_isi_m2m_vb2_qops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &ctx->vb2_lock;
	src_vq->dev = m2m->isi->dev;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct mxc_isi_m2m_buffer);
	dst_vq->ops = &mxc_isi_m2m_vb2_qops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &ctx->vb2_lock;
	dst_vq->dev = m2m->isi->dev;

	return vb2_queue_init(dst_vq);
}

/* -----------------------------------------------------------------------------
 * V4L2 controls
 */

static inline struct mxc_isi_m2m_ctx *
ctrl_to_mxc_isi_m2m_ctx(struct v4l2_ctrl *ctrl)
{
	return container_of(ctrl->handler, struct mxc_isi_m2m_ctx, ctrls.handler);
}

static int mxc_isi_m2m_ctx_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mxc_isi_m2m_ctx *ctx = ctrl_to_mxc_isi_m2m_ctx(ctrl);

	switch (ctrl->id) {
	case V4L2_CID_HFLIP:
		ctx->ctrls.hflip = ctrl->val;
		break;

	case V4L2_CID_VFLIP:
		ctx->ctrls.vflip = ctrl->val;
		break;

	case V4L2_CID_ALPHA_COMPONENT:
		ctx->ctrls.alpha = ctrl->val;
		break;
	}

	return 0;
}

static const struct v4l2_ctrl_ops mxc_isi_m2m_ctx_ctrl_ops = {
	.s_ctrl = mxc_isi_m2m_ctx_s_ctrl,
};

static int mxc_isi_m2m_ctx_ctrls_create(struct mxc_isi_m2m_ctx *ctx)
{
	struct v4l2_ctrl_handler *handler = &ctx->ctrls.handler;
	int ret;

	v4l2_ctrl_handler_init(handler, 3);

	v4l2_ctrl_new_std(handler, &mxc_isi_m2m_ctx_ctrl_ops,
			  V4L2_CID_ALPHA_COMPONENT, 0, 255, 1, 0);
	v4l2_ctrl_new_std(handler, &mxc_isi_m2m_ctx_ctrl_ops,
			  V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(handler, &mxc_isi_m2m_ctx_ctrl_ops,
			  V4L2_CID_VFLIP, 0, 1, 1, 0);

	if (handler->error) {
		ret = handler->error;
		v4l2_ctrl_handler_free(handler);
		return ret;
	}

	ctx->fh.ctrl_handler = handler;

	return 0;
}

static void mxc_isi_m2m_ctx_ctrls_delete(struct mxc_isi_m2m_ctx *ctx)
{
	v4l2_ctrl_handler_free(&ctx->ctrls.handler);
}

/* -----------------------------------------------------------------------------
 * V4L2 ioctls
 */

static int mxc_isi_m2m_querycap(struct file *file, void *fh,
				struct v4l2_capability *cap)
{
	strscpy(cap->driver, MXC_ISI_DRIVER_NAME, sizeof(cap->driver));
	strscpy(cap->card, MXC_ISI_M2M, sizeof(cap->card));
	cap->device_caps = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_M2M_MPLANE;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	return 0;
}

static int mxc_isi_m2m_enum_fmt_vid(struct file *file, void *fh,
				    struct v4l2_fmtdesc *f)
{
	const enum mxc_isi_video_type type =
		f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE ?
		MXC_ISI_VIDEO_M2M_OUT : MXC_ISI_VIDEO_M2M_CAP;
	const struct mxc_isi_format_info *info;

	info = mxc_isi_format_enum(f->index, type);
	if (!info)
		return -EINVAL;

	f->pixelformat = info->fourcc;
	f->flags |= V4L2_FMT_FLAG_CSC_COLORSPACE | V4L2_FMT_FLAG_CSC_YCBCR_ENC
		 |  V4L2_FMT_FLAG_CSC_QUANTIZATION | V4L2_FMT_FLAG_CSC_XFER_FUNC;

	return 0;
}

static const struct mxc_isi_format_info *
__mxc_isi_m2m_try_fmt_vid(struct mxc_isi_m2m_ctx *ctx,
			  struct v4l2_pix_format_mplane *pix,
			  const enum mxc_isi_video_type type)
{
	if (type == MXC_ISI_VIDEO_M2M_CAP) {
		/* Downscaling only  */
		pix->width = min(pix->width, ctx->queues.out.format.width);
		pix->height = min(pix->height, ctx->queues.out.format.height);
	}

	return mxc_isi_format_try(ctx->m2m->pipe, pix, type);
}

static int mxc_isi_m2m_try_fmt_vid(struct file *file, void *fh,
				   struct v4l2_format *f)
{
	const enum mxc_isi_video_type type =
		f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE ?
		MXC_ISI_VIDEO_M2M_OUT : MXC_ISI_VIDEO_M2M_CAP;
	struct mxc_isi_m2m_ctx *ctx = file_to_isi_m2m_ctx(file);

	__mxc_isi_m2m_try_fmt_vid(ctx, &f->fmt.pix_mp, type);

	return 0;
}

static int mxc_isi_m2m_g_fmt_vid(struct file *file, void *fh,
				 struct v4l2_format *f)
{
	struct mxc_isi_m2m_ctx *ctx = file_to_isi_m2m_ctx(file);
	const struct mxc_isi_m2m_ctx_queue_data *qdata =
		mxc_isi_m2m_ctx_qdata(ctx, f->type);

	f->fmt.pix_mp = qdata->format;

	return 0;
}

static int mxc_isi_m2m_s_fmt_vid(struct file *file, void *fh,
				 struct v4l2_format *f)
{
	const enum mxc_isi_video_type type =
		f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE ?
		MXC_ISI_VIDEO_M2M_OUT : MXC_ISI_VIDEO_M2M_CAP;
	struct mxc_isi_m2m_ctx *ctx = file_to_isi_m2m_ctx(file);
	struct v4l2_pix_format_mplane *pix = &f->fmt.pix_mp;
	const struct mxc_isi_format_info *info;
	struct vb2_queue *vq;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	if (vb2_is_busy(vq))
		return -EBUSY;

	info = __mxc_isi_m2m_try_fmt_vid(ctx, pix, type);

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		ctx->queues.out.format = *pix;
		ctx->queues.out.info = info;
	}

	/*
	 * Always set the format on the capture side, due to either format
	 * propagation or direct setting.
	 */
	ctx->queues.cap.format = *pix;
	ctx->queues.cap.info = info;

	return 0;
}

static const struct v4l2_ioctl_ops mxc_isi_m2m_ioctl_ops = {
	.vidioc_querycap		= mxc_isi_m2m_querycap,

	.vidioc_enum_fmt_vid_cap	= mxc_isi_m2m_enum_fmt_vid,
	.vidioc_enum_fmt_vid_out	= mxc_isi_m2m_enum_fmt_vid,
	.vidioc_g_fmt_vid_cap_mplane	= mxc_isi_m2m_g_fmt_vid,
	.vidioc_g_fmt_vid_out_mplane	= mxc_isi_m2m_g_fmt_vid,
	.vidioc_s_fmt_vid_cap_mplane	= mxc_isi_m2m_s_fmt_vid,
	.vidioc_s_fmt_vid_out_mplane	= mxc_isi_m2m_s_fmt_vid,
	.vidioc_try_fmt_vid_cap_mplane	= mxc_isi_m2m_try_fmt_vid,
	.vidioc_try_fmt_vid_out_mplane	= mxc_isi_m2m_try_fmt_vid,

	.vidioc_reqbufs			= v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf		= v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf			= v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf			= v4l2_m2m_ioctl_dqbuf,
	.vidioc_expbuf			= v4l2_m2m_ioctl_expbuf,
	.vidioc_prepare_buf		= v4l2_m2m_ioctl_prepare_buf,
	.vidioc_create_bufs		= v4l2_m2m_ioctl_create_bufs,

	.vidioc_streamon		= v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff		= v4l2_m2m_ioctl_streamoff,

	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
};

/* -----------------------------------------------------------------------------
 * Video device file operations
 */

static void mxc_isi_m2m_init_format(struct mxc_isi_m2m_ctx *ctx,
				    struct mxc_isi_m2m_ctx_queue_data *qdata,
				    enum mxc_isi_video_type type)
{
	qdata->format.width = MXC_ISI_DEF_WIDTH;
	qdata->format.height = MXC_ISI_DEF_HEIGHT;
	qdata->format.pixelformat = MXC_ISI_DEF_PIXEL_FORMAT;

	qdata->info = mxc_isi_format_try(ctx->m2m->pipe, &qdata->format, type);
}

static int mxc_isi_m2m_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct mxc_isi_m2m *m2m = video_drvdata(file);
	struct mxc_isi_m2m_ctx *ctx;
	int ret;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->m2m = m2m;
	mutex_init(&ctx->vb2_lock);

	v4l2_fh_init(&ctx->fh, vdev);

	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(m2m->m2m_dev, ctx,
					    &mxc_isi_m2m_queue_init);
	if (IS_ERR(ctx->fh.m2m_ctx)) {
		ret = PTR_ERR(ctx->fh.m2m_ctx);
		ctx->fh.m2m_ctx = NULL;
		goto err_fh;
	}

	mxc_isi_m2m_init_format(ctx, &ctx->queues.out, MXC_ISI_VIDEO_M2M_OUT);
	mxc_isi_m2m_init_format(ctx, &ctx->queues.cap, MXC_ISI_VIDEO_M2M_CAP);

	ret = mxc_isi_m2m_ctx_ctrls_create(ctx);
	if (ret)
		goto err_ctx;

	v4l2_fh_add(&ctx->fh, file);

	return 0;

err_ctx:
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
err_fh:
	v4l2_fh_exit(&ctx->fh);
	mutex_destroy(&ctx->vb2_lock);
	kfree(ctx);
	return ret;
}

static int mxc_isi_m2m_release(struct file *file)
{
	struct mxc_isi_m2m_ctx *ctx = file_to_isi_m2m_ctx(file);

	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
	mxc_isi_m2m_ctx_ctrls_delete(ctx);

	v4l2_fh_del(&ctx->fh, file);
	v4l2_fh_exit(&ctx->fh);

	mutex_destroy(&ctx->vb2_lock);
	kfree(ctx);

	return 0;
}

static const struct v4l2_file_operations mxc_isi_m2m_fops = {
	.owner		= THIS_MODULE,
	.open		= mxc_isi_m2m_open,
	.release	= mxc_isi_m2m_release,
	.poll		= v4l2_m2m_fop_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= v4l2_m2m_fop_mmap,
};

/* -----------------------------------------------------------------------------
 * Suspend & resume
 */

void mxc_isi_m2m_suspend(struct mxc_isi_m2m *m2m)
{
	if (m2m->usage_count == 0)
		return;

	v4l2_m2m_suspend(m2m->m2m_dev);

	if (m2m->chained_count > 0)
		mxc_isi_channel_unchain(m2m->pipe);

	mxc_isi_channel_disable(m2m->pipe);
	mxc_isi_channel_put(m2m->pipe);
}

int mxc_isi_m2m_resume(struct mxc_isi_m2m *m2m)
{
	if (m2m->usage_count == 0)
		return 0;

	mxc_isi_channel_get(m2m->pipe);

	if (m2m->chained_count > 0)
		mxc_isi_channel_chain(m2m->pipe);

	m2m->last_ctx = NULL;
	v4l2_m2m_resume(m2m->m2m_dev);

	return 0;
}

/* -----------------------------------------------------------------------------
 * Registration
 */

int mxc_isi_m2m_register(struct mxc_isi_dev *isi, struct v4l2_device *v4l2_dev)
{
	struct mxc_isi_m2m *m2m = &isi->m2m;
	struct video_device *vdev = &m2m->vdev;
	struct media_link *link;
	int ret;

	m2m->isi = isi;
	m2m->pipe = &isi->pipes[0];

	mutex_init(&m2m->lock);

	/* Initialize the video device and create controls. */
	snprintf(vdev->name, sizeof(vdev->name), "mxc_isi.m2m");

	vdev->fops	= &mxc_isi_m2m_fops;
	vdev->ioctl_ops	= &mxc_isi_m2m_ioctl_ops;
	vdev->v4l2_dev	= v4l2_dev;
	vdev->minor	= -1;
	vdev->release	= video_device_release_empty;
	vdev->vfl_dir	= VFL_DIR_M2M;

	vdev->device_caps = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_M2M_MPLANE;
	video_set_drvdata(vdev, m2m);

	/* Create the M2M device. */
	m2m->m2m_dev = v4l2_m2m_init(&mxc_isi_m2m_ops);
	if (IS_ERR(m2m->m2m_dev)) {
		dev_err(isi->dev, "failed to initialize m2m device\n");
		ret = PTR_ERR(m2m->m2m_dev);
		goto err_mutex;
	}

	/* Register the video device. */
	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret < 0) {
		dev_err(isi->dev, "failed to register m2m device\n");
		goto err_m2m;
	}

	/*
	 * Populate the media graph. We can't use the mem2mem helper
	 * v4l2_m2m_register_media_controller() as the M2M interface needs to
	 * be connected to the existing entities in the graph, so we have to
	 * wire things up manually:
	 *
	 * - The entity in the video_device, which isn't touched by the V4L2
	 *   core for M2M devices, is used as the source I/O entity in the
	 *   graph, connected to the crossbar switch.
	 *
	 * - The video device at the end of the pipeline provides the sink
	 *   entity, and is already wired up in the graph.
	 *
	 * - A new interface is created, pointing at both entities. The sink
	 *   entity will thus have two interfaces pointing to it.
	 */
	m2m->pad.flags = MEDIA_PAD_FL_SOURCE;
	vdev->entity.name = "mxc_isi.output";
	vdev->entity.function = MEDIA_ENT_F_IO_V4L;
	ret = media_entity_pads_init(&vdev->entity, 1, &m2m->pad);
	if (ret)
		goto err_video;

	ret = media_device_register_entity(v4l2_dev->mdev, &vdev->entity);
	if (ret)
		goto err_entity_cleanup;

	ret = media_create_pad_link(&vdev->entity, 0,
				    &m2m->isi->crossbar.sd.entity,
				    m2m->isi->crossbar.num_sinks - 1,
				    MEDIA_LNK_FL_IMMUTABLE |
				    MEDIA_LNK_FL_ENABLED);
	if (ret)
		goto err_entity_unreg;

	m2m->intf = media_devnode_create(v4l2_dev->mdev, MEDIA_INTF_T_V4L_VIDEO,
					 0, VIDEO_MAJOR, vdev->minor);
	if (!m2m->intf) {
		ret = -ENOMEM;
		goto err_entity_unreg;
	}

	link = media_create_intf_link(&vdev->entity, &m2m->intf->intf,
				      MEDIA_LNK_FL_IMMUTABLE |
				      MEDIA_LNK_FL_ENABLED);
	if (!link) {
		ret = -ENOMEM;
		goto err_devnode;
	}

	link = media_create_intf_link(&m2m->pipe->video.vdev.entity,
				      &m2m->intf->intf,
				      MEDIA_LNK_FL_IMMUTABLE |
				      MEDIA_LNK_FL_ENABLED);
	if (!link) {
		ret = -ENOMEM;
		goto err_devnode;
	}

	return 0;

err_devnode:
	media_devnode_remove(m2m->intf);
err_entity_unreg:
	media_device_unregister_entity(&vdev->entity);
err_entity_cleanup:
	media_entity_cleanup(&vdev->entity);
err_video:
	video_unregister_device(vdev);
err_m2m:
	v4l2_m2m_release(m2m->m2m_dev);
err_mutex:
	mutex_destroy(&m2m->lock);
	return ret;
}

int mxc_isi_m2m_unregister(struct mxc_isi_dev *isi)
{
	struct mxc_isi_m2m *m2m = &isi->m2m;
	struct video_device *vdev = &m2m->vdev;

	video_unregister_device(vdev);

	v4l2_m2m_release(m2m->m2m_dev);
	media_devnode_remove(m2m->intf);
	media_entity_cleanup(&vdev->entity);
	mutex_destroy(&m2m->lock);

	return 0;
}
