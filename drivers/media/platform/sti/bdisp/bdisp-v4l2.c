// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics SA 2014
 * Authors: Fabien Dessenne <fabien.dessenne@st.com> for STMicroelectronics.
 */

#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>

#include "bdisp.h"

#define BDISP_MAX_CTRL_NUM      10

#define BDISP_WORK_TIMEOUT      ((100 * HZ) / 1000)

/* User configuration change */
#define BDISP_PARAMS            BIT(0) /* Config updated */
#define BDISP_SRC_FMT           BIT(1) /* Source set */
#define BDISP_DST_FMT           BIT(2) /* Destination set */
#define BDISP_CTX_STOP_REQ      BIT(3) /* Stop request */
#define BDISP_CTX_ABORT         BIT(4) /* Abort while device run */

#define BDISP_MIN_W             1
#define BDISP_MAX_W             8191
#define BDISP_MIN_H             1
#define BDISP_MAX_H             8191

#define fh_to_ctx(__fh) container_of(__fh, struct bdisp_ctx, fh)

enum bdisp_dev_flags {
	ST_M2M_OPEN,            /* Driver opened */
	ST_M2M_RUNNING,         /* HW device running */
	ST_M2M_SUSPENDED,       /* Driver suspended */
	ST_M2M_SUSPENDING,      /* Driver being suspended */
};

static const struct bdisp_fmt bdisp_formats[] = {
	/* ARGB888. [31:0] A:R:G:B 8:8:8:8 little endian */
	{
		.pixelformat    = V4L2_PIX_FMT_ABGR32, /* is actually ARGB */
		.nb_planes      = 1,
		.bpp            = 32,
		.bpp_plane0     = 32,
		.w_align        = 1,
		.h_align        = 1
	},
	/* XRGB888. [31:0] x:R:G:B 8:8:8:8 little endian */
	{
		.pixelformat    = V4L2_PIX_FMT_XBGR32, /* is actually xRGB */
		.nb_planes      = 1,
		.bpp            = 32,
		.bpp_plane0     = 32,
		.w_align        = 1,
		.h_align        = 1
	},
	/* RGB565. [15:0] R:G:B 5:6:5 little endian */
	{
		.pixelformat    = V4L2_PIX_FMT_RGB565,
		.nb_planes      = 1,
		.bpp            = 16,
		.bpp_plane0     = 16,
		.w_align        = 1,
		.h_align        = 1
	},
	/* NV12. YUV420SP - 1 plane for Y + 1 plane for (CbCr) */
	{
		.pixelformat    = V4L2_PIX_FMT_NV12,
		.nb_planes      = 2,
		.bpp            = 12,
		.bpp_plane0     = 8,
		.w_align        = 2,
		.h_align        = 2
	},
	/* RGB888. [23:0] B:G:R 8:8:8 little endian */
	{
		.pixelformat    = V4L2_PIX_FMT_RGB24,
		.nb_planes      = 1,
		.bpp            = 24,
		.bpp_plane0     = 24,
		.w_align        = 1,
		.h_align        = 1
	},
	/* YU12. YUV420P - 1 plane for Y + 1 plane for Cb + 1 plane for Cr
	 * To keep as the LAST element of this table (no support on capture)
	 */
	{
		.pixelformat    = V4L2_PIX_FMT_YUV420,
		.nb_planes      = 3,
		.bpp            = 12,
		.bpp_plane0     = 8,
		.w_align        = 2,
		.h_align        = 2
	}
};

/* Default format : HD ARGB32*/
#define BDISP_DEF_WIDTH         1920
#define BDISP_DEF_HEIGHT        1080

static const struct bdisp_frame bdisp_dflt_fmt = {
		.width          = BDISP_DEF_WIDTH,
		.height         = BDISP_DEF_HEIGHT,
		.fmt            = &bdisp_formats[0],
		.field          = V4L2_FIELD_NONE,
		.bytesperline   = BDISP_DEF_WIDTH * 4,
		.sizeimage      = BDISP_DEF_WIDTH * BDISP_DEF_HEIGHT * 4,
		.colorspace     = V4L2_COLORSPACE_REC709,
		.crop           = {0, 0, BDISP_DEF_WIDTH, BDISP_DEF_HEIGHT},
		.paddr          = {0, 0, 0, 0}
};

static inline void bdisp_ctx_state_lock_set(u32 state, struct bdisp_ctx *ctx)
{
	unsigned long flags;

	spin_lock_irqsave(&ctx->bdisp_dev->slock, flags);
	ctx->state |= state;
	spin_unlock_irqrestore(&ctx->bdisp_dev->slock, flags);
}

static inline void bdisp_ctx_state_lock_clear(u32 state, struct bdisp_ctx *ctx)
{
	unsigned long flags;

	spin_lock_irqsave(&ctx->bdisp_dev->slock, flags);
	ctx->state &= ~state;
	spin_unlock_irqrestore(&ctx->bdisp_dev->slock, flags);
}

static inline bool bdisp_ctx_state_is_set(u32 mask, struct bdisp_ctx *ctx)
{
	unsigned long flags;
	bool ret;

	spin_lock_irqsave(&ctx->bdisp_dev->slock, flags);
	ret = (ctx->state & mask) == mask;
	spin_unlock_irqrestore(&ctx->bdisp_dev->slock, flags);

	return ret;
}

static const struct bdisp_fmt *bdisp_find_fmt(u32 pixelformat)
{
	const struct bdisp_fmt *fmt;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(bdisp_formats); i++) {
		fmt = &bdisp_formats[i];
		if (fmt->pixelformat == pixelformat)
			return fmt;
	}

	return NULL;
}

static struct bdisp_frame *ctx_get_frame(struct bdisp_ctx *ctx,
					 enum v4l2_buf_type type)
{
	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		return &ctx->src;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		return &ctx->dst;
	default:
		dev_err(ctx->bdisp_dev->dev,
			"Wrong buffer/video queue type (%d)\n", type);
		break;
	}

	return ERR_PTR(-EINVAL);
}

static void bdisp_job_finish(struct bdisp_ctx *ctx, int vb_state)
{
	struct vb2_v4l2_buffer *src_vb, *dst_vb;

	if (WARN(!ctx || !ctx->fh.m2m_ctx, "Null hardware context\n"))
		return;

	dev_dbg(ctx->bdisp_dev->dev, "%s\n", __func__);

	src_vb = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	dst_vb = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);

	if (src_vb && dst_vb) {
		dst_vb->vb2_buf.timestamp = src_vb->vb2_buf.timestamp;
		dst_vb->timecode = src_vb->timecode;
		dst_vb->flags &= ~V4L2_BUF_FLAG_TSTAMP_SRC_MASK;
		dst_vb->flags |= src_vb->flags &
					  V4L2_BUF_FLAG_TSTAMP_SRC_MASK;

		v4l2_m2m_buf_done(src_vb, vb_state);
		v4l2_m2m_buf_done(dst_vb, vb_state);

		v4l2_m2m_job_finish(ctx->bdisp_dev->m2m.m2m_dev,
				    ctx->fh.m2m_ctx);
	}
}

static int bdisp_ctx_stop_req(struct bdisp_ctx *ctx)
{
	struct bdisp_ctx *curr_ctx;
	struct bdisp_dev *bdisp = ctx->bdisp_dev;
	int ret;

	dev_dbg(ctx->bdisp_dev->dev, "%s\n", __func__);

	cancel_delayed_work(&bdisp->timeout_work);

	curr_ctx = v4l2_m2m_get_curr_priv(bdisp->m2m.m2m_dev);
	if (!test_bit(ST_M2M_RUNNING, &bdisp->state) || (curr_ctx != ctx))
		return 0;

	bdisp_ctx_state_lock_set(BDISP_CTX_STOP_REQ, ctx);

	ret = wait_event_timeout(bdisp->irq_queue,
			!bdisp_ctx_state_is_set(BDISP_CTX_STOP_REQ, ctx),
			BDISP_WORK_TIMEOUT);

	if (!ret) {
		dev_err(ctx->bdisp_dev->dev, "%s IRQ timeout\n", __func__);
		return -ETIMEDOUT;
	}

	return 0;
}

static void __bdisp_job_abort(struct bdisp_ctx *ctx)
{
	int ret;

	ret = bdisp_ctx_stop_req(ctx);
	if ((ret == -ETIMEDOUT) || (ctx->state & BDISP_CTX_ABORT)) {
		bdisp_ctx_state_lock_clear(BDISP_CTX_STOP_REQ | BDISP_CTX_ABORT,
					   ctx);
		bdisp_job_finish(ctx, VB2_BUF_STATE_ERROR);
	}
}

static void bdisp_job_abort(void *priv)
{
	__bdisp_job_abort((struct bdisp_ctx *)priv);
}

static int bdisp_get_addr(struct bdisp_ctx *ctx, struct vb2_buffer *vb,
			  struct bdisp_frame *frame, dma_addr_t *paddr)
{
	if (!vb || !frame)
		return -EINVAL;

	paddr[0] = vb2_dma_contig_plane_dma_addr(vb, 0);

	if (frame->fmt->nb_planes > 1)
		/* UV (NV12) or U (420P) */
		paddr[1] = (dma_addr_t)(paddr[0] +
				frame->bytesperline * frame->height);

	if (frame->fmt->nb_planes > 2)
		/* V (420P) */
		paddr[2] = (dma_addr_t)(paddr[1] +
				(frame->bytesperline * frame->height) / 4);

	if (frame->fmt->nb_planes > 3)
		dev_dbg(ctx->bdisp_dev->dev, "ignoring some planes\n");

	dev_dbg(ctx->bdisp_dev->dev,
		"%s plane[0]=%pad plane[1]=%pad plane[2]=%pad\n",
		__func__, &paddr[0], &paddr[1], &paddr[2]);

	return 0;
}

static int bdisp_get_bufs(struct bdisp_ctx *ctx)
{
	struct bdisp_frame *src, *dst;
	struct vb2_v4l2_buffer *src_vb, *dst_vb;
	int ret;

	src = &ctx->src;
	dst = &ctx->dst;

	src_vb = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	ret = bdisp_get_addr(ctx, &src_vb->vb2_buf, src, src->paddr);
	if (ret)
		return ret;

	dst_vb = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
	ret = bdisp_get_addr(ctx, &dst_vb->vb2_buf, dst, dst->paddr);
	if (ret)
		return ret;

	dst_vb->vb2_buf.timestamp = src_vb->vb2_buf.timestamp;

	return 0;
}

static void bdisp_device_run(void *priv)
{
	struct bdisp_ctx *ctx = priv;
	struct bdisp_dev *bdisp;
	unsigned long flags;
	int err = 0;

	if (WARN(!ctx, "Null hardware context\n"))
		return;

	bdisp = ctx->bdisp_dev;
	dev_dbg(bdisp->dev, "%s\n", __func__);
	spin_lock_irqsave(&bdisp->slock, flags);

	if (bdisp->m2m.ctx != ctx) {
		dev_dbg(bdisp->dev, "ctx updated: %p -> %p\n",
			bdisp->m2m.ctx, ctx);
		ctx->state |= BDISP_PARAMS;
		bdisp->m2m.ctx = ctx;
	}

	if (ctx->state & BDISP_CTX_STOP_REQ) {
		ctx->state &= ~BDISP_CTX_STOP_REQ;
		ctx->state |= BDISP_CTX_ABORT;
		wake_up(&bdisp->irq_queue);
		goto out;
	}

	err = bdisp_get_bufs(ctx);
	if (err) {
		dev_err(bdisp->dev, "cannot get address\n");
		goto out;
	}

	bdisp_dbg_perf_begin(bdisp);

	err = bdisp_hw_reset(bdisp);
	if (err) {
		dev_err(bdisp->dev, "could not get HW ready\n");
		goto out;
	}

	err = bdisp_hw_update(ctx);
	if (err) {
		dev_err(bdisp->dev, "could not send HW request\n");
		goto out;
	}

	queue_delayed_work(bdisp->work_queue, &bdisp->timeout_work,
			   BDISP_WORK_TIMEOUT);
	set_bit(ST_M2M_RUNNING, &bdisp->state);
out:
	ctx->state &= ~BDISP_PARAMS;
	spin_unlock_irqrestore(&bdisp->slock, flags);
	if (err)
		bdisp_job_finish(ctx, VB2_BUF_STATE_ERROR);
}

static const struct v4l2_m2m_ops bdisp_m2m_ops = {
	.device_run     = bdisp_device_run,
	.job_abort      = bdisp_job_abort,
};

static int __bdisp_s_ctrl(struct bdisp_ctx *ctx, struct v4l2_ctrl *ctrl)
{
	if (ctrl->flags & V4L2_CTRL_FLAG_INACTIVE)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_HFLIP:
		ctx->hflip = ctrl->val;
		break;
	case V4L2_CID_VFLIP:
		ctx->vflip = ctrl->val;
		break;
	default:
		dev_err(ctx->bdisp_dev->dev, "unknown control %d\n", ctrl->id);
		return -EINVAL;
	}

	ctx->state |= BDISP_PARAMS;

	return 0;
}

static int bdisp_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct bdisp_ctx *ctx = container_of(ctrl->handler, struct bdisp_ctx,
						ctrl_handler);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&ctx->bdisp_dev->slock, flags);
	ret = __bdisp_s_ctrl(ctx, ctrl);
	spin_unlock_irqrestore(&ctx->bdisp_dev->slock, flags);

	return ret;
}

static const struct v4l2_ctrl_ops bdisp_c_ops = {
	.s_ctrl = bdisp_s_ctrl,
};

static int bdisp_ctrls_create(struct bdisp_ctx *ctx)
{
	if (ctx->ctrls_rdy)
		return 0;

	v4l2_ctrl_handler_init(&ctx->ctrl_handler, BDISP_MAX_CTRL_NUM);

	ctx->bdisp_ctrls.hflip = v4l2_ctrl_new_std(&ctx->ctrl_handler,
				&bdisp_c_ops, V4L2_CID_HFLIP, 0, 1, 1, 0);
	ctx->bdisp_ctrls.vflip = v4l2_ctrl_new_std(&ctx->ctrl_handler,
				&bdisp_c_ops, V4L2_CID_VFLIP, 0, 1, 1, 0);

	if (ctx->ctrl_handler.error) {
		int err = ctx->ctrl_handler.error;

		v4l2_ctrl_handler_free(&ctx->ctrl_handler);
		return err;
	}

	ctx->ctrls_rdy = true;

	return 0;
}

static void bdisp_ctrls_delete(struct bdisp_ctx *ctx)
{
	if (ctx->ctrls_rdy) {
		v4l2_ctrl_handler_free(&ctx->ctrl_handler);
		ctx->ctrls_rdy = false;
	}
}

static int bdisp_queue_setup(struct vb2_queue *vq,
			     unsigned int *nb_buf, unsigned int *nb_planes,
			     unsigned int sizes[], struct device *alloc_devs[])
{
	struct bdisp_ctx *ctx = vb2_get_drv_priv(vq);
	struct bdisp_frame *frame = ctx_get_frame(ctx, vq->type);

	if (IS_ERR(frame)) {
		dev_err(ctx->bdisp_dev->dev, "Invalid frame (%p)\n", frame);
		return PTR_ERR(frame);
	}

	if (!frame->fmt) {
		dev_err(ctx->bdisp_dev->dev, "Invalid format\n");
		return -EINVAL;
	}

	if (*nb_planes)
		return sizes[0] < frame->sizeimage ? -EINVAL : 0;

	*nb_planes = 1;
	sizes[0] = frame->sizeimage;

	return 0;
}

static int bdisp_buf_prepare(struct vb2_buffer *vb)
{
	struct bdisp_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct bdisp_frame *frame = ctx_get_frame(ctx, vb->vb2_queue->type);

	if (IS_ERR(frame)) {
		dev_err(ctx->bdisp_dev->dev, "Invalid frame (%p)\n", frame);
		return PTR_ERR(frame);
	}

	if (vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		vb2_set_plane_payload(vb, 0, frame->sizeimage);

	return 0;
}

static void bdisp_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct bdisp_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	/* return to V4L2 any 0-size buffer so it can be dequeued by user */
	if (!vb2_get_plane_payload(vb, 0)) {
		dev_dbg(ctx->bdisp_dev->dev, "0 data buffer, skip it\n");
		vb2_buffer_done(vb, VB2_BUF_STATE_DONE);
		return;
	}

	if (ctx->fh.m2m_ctx)
		v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
}

static int bdisp_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct bdisp_ctx *ctx = q->drv_priv;
	struct vb2_v4l2_buffer *buf;
	int ret = pm_runtime_get_sync(ctx->bdisp_dev->dev);

	if (ret < 0) {
		dev_err(ctx->bdisp_dev->dev, "failed to set runtime PM\n");

		if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
			while ((buf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx)))
				v4l2_m2m_buf_done(buf, VB2_BUF_STATE_QUEUED);
		} else {
			while ((buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx)))
				v4l2_m2m_buf_done(buf, VB2_BUF_STATE_QUEUED);
		}

		return ret;
	}

	return 0;
}

static void bdisp_stop_streaming(struct vb2_queue *q)
{
	struct bdisp_ctx *ctx = q->drv_priv;

	__bdisp_job_abort(ctx);

	pm_runtime_put(ctx->bdisp_dev->dev);
}

static const struct vb2_ops bdisp_qops = {
	.queue_setup     = bdisp_queue_setup,
	.buf_prepare     = bdisp_buf_prepare,
	.buf_queue       = bdisp_buf_queue,
	.wait_prepare    = vb2_ops_wait_prepare,
	.wait_finish     = vb2_ops_wait_finish,
	.stop_streaming  = bdisp_stop_streaming,
	.start_streaming = bdisp_start_streaming,
};

static int queue_init(void *priv,
		      struct vb2_queue *src_vq, struct vb2_queue *dst_vq)
{
	struct bdisp_ctx *ctx = priv;
	int ret;

	memset(src_vq, 0, sizeof(*src_vq));
	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->drv_priv = ctx;
	src_vq->ops = &bdisp_qops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &ctx->bdisp_dev->lock;
	src_vq->dev = ctx->bdisp_dev->v4l2_dev.dev;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	memset(dst_vq, 0, sizeof(*dst_vq));
	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->ops = &bdisp_qops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &ctx->bdisp_dev->lock;
	dst_vq->dev = ctx->bdisp_dev->v4l2_dev.dev;

	return vb2_queue_init(dst_vq);
}

static int bdisp_open(struct file *file)
{
	struct bdisp_dev *bdisp = video_drvdata(file);
	struct bdisp_ctx *ctx = NULL;
	int ret;

	if (mutex_lock_interruptible(&bdisp->lock))
		return -ERESTARTSYS;

	/* Allocate memory for both context and node */
	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		ret = -ENOMEM;
		goto unlock;
	}
	ctx->bdisp_dev = bdisp;

	if (bdisp_hw_alloc_nodes(ctx)) {
		dev_err(bdisp->dev, "no memory for nodes\n");
		ret = -ENOMEM;
		goto mem_ctx;
	}

	v4l2_fh_init(&ctx->fh, bdisp->m2m.vdev);

	ret = bdisp_ctrls_create(ctx);
	if (ret) {
		dev_err(bdisp->dev, "Failed to create control\n");
		goto error_fh;
	}

	/* Use separate control handler per file handle */
	ctx->fh.ctrl_handler = &ctx->ctrl_handler;
	file->private_data = &ctx->fh;
	v4l2_fh_add(&ctx->fh);

	/* Default format */
	ctx->src = bdisp_dflt_fmt;
	ctx->dst = bdisp_dflt_fmt;

	/* Setup the device context for mem2mem mode. */
	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(bdisp->m2m.m2m_dev, ctx,
					    queue_init);
	if (IS_ERR(ctx->fh.m2m_ctx)) {
		dev_err(bdisp->dev, "Failed to initialize m2m context\n");
		ret = PTR_ERR(ctx->fh.m2m_ctx);
		goto error_ctrls;
	}

	bdisp->m2m.refcnt++;
	set_bit(ST_M2M_OPEN, &bdisp->state);

	dev_dbg(bdisp->dev, "driver opened, ctx = 0x%p\n", ctx);

	mutex_unlock(&bdisp->lock);

	return 0;

error_ctrls:
	bdisp_ctrls_delete(ctx);
	v4l2_fh_del(&ctx->fh);
error_fh:
	v4l2_fh_exit(&ctx->fh);
	bdisp_hw_free_nodes(ctx);
mem_ctx:
	kfree(ctx);
unlock:
	mutex_unlock(&bdisp->lock);

	return ret;
}

static int bdisp_release(struct file *file)
{
	struct bdisp_ctx *ctx = fh_to_ctx(file->private_data);
	struct bdisp_dev *bdisp = ctx->bdisp_dev;

	dev_dbg(bdisp->dev, "%s\n", __func__);

	mutex_lock(&bdisp->lock);

	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);

	bdisp_ctrls_delete(ctx);

	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);

	if (--bdisp->m2m.refcnt <= 0)
		clear_bit(ST_M2M_OPEN, &bdisp->state);

	bdisp_hw_free_nodes(ctx);

	kfree(ctx);

	mutex_unlock(&bdisp->lock);

	return 0;
}

static const struct v4l2_file_operations bdisp_fops = {
	.owner          = THIS_MODULE,
	.open           = bdisp_open,
	.release        = bdisp_release,
	.poll           = v4l2_m2m_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap           = v4l2_m2m_fop_mmap,
};

static int bdisp_querycap(struct file *file, void *fh,
			  struct v4l2_capability *cap)
{
	struct bdisp_ctx *ctx = fh_to_ctx(fh);
	struct bdisp_dev *bdisp = ctx->bdisp_dev;

	strscpy(cap->driver, bdisp->pdev->name, sizeof(cap->driver));
	strscpy(cap->card, bdisp->pdev->name, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:%s%d",
		 BDISP_NAME, bdisp->id);
	return 0;
}

static int bdisp_enum_fmt(struct file *file, void *fh, struct v4l2_fmtdesc *f)
{
	struct bdisp_ctx *ctx = fh_to_ctx(fh);
	const struct bdisp_fmt *fmt;

	if (f->index >= ARRAY_SIZE(bdisp_formats))
		return -EINVAL;

	fmt = &bdisp_formats[f->index];

	if ((fmt->pixelformat == V4L2_PIX_FMT_YUV420) &&
	    (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)) {
		dev_dbg(ctx->bdisp_dev->dev, "No YU12 on capture\n");
		return -EINVAL;
	}
	f->pixelformat = fmt->pixelformat;

	return 0;
}

static int bdisp_g_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct bdisp_ctx *ctx = fh_to_ctx(fh);
	struct v4l2_pix_format *pix;
	struct bdisp_frame *frame  = ctx_get_frame(ctx, f->type);

	if (IS_ERR(frame)) {
		dev_err(ctx->bdisp_dev->dev, "Invalid frame (%p)\n", frame);
		return PTR_ERR(frame);
	}

	pix = &f->fmt.pix;
	pix->width = frame->width;
	pix->height = frame->height;
	pix->pixelformat = frame->fmt->pixelformat;
	pix->field = frame->field;
	pix->bytesperline = frame->bytesperline;
	pix->sizeimage = frame->sizeimage;
	pix->colorspace = (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) ?
				frame->colorspace : bdisp_dflt_fmt.colorspace;

	return 0;
}

static int bdisp_try_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct bdisp_ctx *ctx = fh_to_ctx(fh);
	struct v4l2_pix_format *pix = &f->fmt.pix;
	const struct bdisp_fmt *format;
	u32 in_w, in_h;

	format = bdisp_find_fmt(pix->pixelformat);
	if (!format) {
		dev_dbg(ctx->bdisp_dev->dev, "Unknown format 0x%x\n",
			pix->pixelformat);
		return -EINVAL;
	}

	/* YUV420P only supported for VIDEO_OUTPUT */
	if ((format->pixelformat == V4L2_PIX_FMT_YUV420) &&
	    (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)) {
		dev_dbg(ctx->bdisp_dev->dev, "No YU12 on capture\n");
		return -EINVAL;
	}

	/* Field (interlaced only supported on OUTPUT) */
	if ((f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) ||
	    (pix->field != V4L2_FIELD_INTERLACED))
		pix->field = V4L2_FIELD_NONE;

	/* Adjust width & height */
	in_w = pix->width;
	in_h = pix->height;
	v4l_bound_align_image(&pix->width,
			      BDISP_MIN_W, BDISP_MAX_W,
			      ffs(format->w_align) - 1,
			      &pix->height,
			      BDISP_MIN_H, BDISP_MAX_H,
			      ffs(format->h_align) - 1,
			      0);
	if ((pix->width != in_w) || (pix->height != in_h))
		dev_dbg(ctx->bdisp_dev->dev,
			"%s size updated: %dx%d -> %dx%d\n", __func__,
			in_w, in_h, pix->width, pix->height);

	pix->bytesperline = (pix->width * format->bpp_plane0) / 8;
	pix->sizeimage = (pix->width * pix->height * format->bpp) / 8;

	if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		pix->colorspace = bdisp_dflt_fmt.colorspace;

	return 0;
}

static int bdisp_s_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct bdisp_ctx *ctx = fh_to_ctx(fh);
	struct vb2_queue *vq;
	struct bdisp_frame *frame;
	struct v4l2_pix_format *pix;
	int ret;
	u32 state;

	ret = bdisp_try_fmt(file, fh, f);
	if (ret) {
		dev_err(ctx->bdisp_dev->dev, "Cannot set format\n");
		return ret;
	}

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (vb2_is_streaming(vq)) {
		dev_err(ctx->bdisp_dev->dev, "queue (%d) busy\n", f->type);
		return -EBUSY;
	}

	frame = (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) ?
			&ctx->src : &ctx->dst;
	pix = &f->fmt.pix;
	frame->fmt = bdisp_find_fmt(pix->pixelformat);
	if (!frame->fmt) {
		dev_err(ctx->bdisp_dev->dev, "Unknown format 0x%x\n",
			pix->pixelformat);
		return -EINVAL;
	}

	frame->width = pix->width;
	frame->height = pix->height;
	frame->bytesperline = pix->bytesperline;
	frame->sizeimage = pix->sizeimage;
	frame->field = pix->field;
	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
		frame->colorspace = pix->colorspace;

	frame->crop.width = frame->width;
	frame->crop.height = frame->height;
	frame->crop.left = 0;
	frame->crop.top = 0;

	state = BDISP_PARAMS;
	state |= (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) ?
			BDISP_DST_FMT : BDISP_SRC_FMT;
	bdisp_ctx_state_lock_set(state, ctx);

	return 0;
}

static int bdisp_g_selection(struct file *file, void *fh,
			     struct v4l2_selection *s)
{
	struct bdisp_frame *frame;
	struct bdisp_ctx *ctx = fh_to_ctx(fh);

	frame = ctx_get_frame(ctx, s->type);
	if (IS_ERR(frame)) {
		dev_err(ctx->bdisp_dev->dev, "Invalid frame (%p)\n", frame);
		return PTR_ERR(frame);
	}

	switch (s->type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		switch (s->target) {
		case V4L2_SEL_TGT_CROP:
			/* cropped frame */
			s->r = frame->crop;
			break;
		case V4L2_SEL_TGT_CROP_DEFAULT:
		case V4L2_SEL_TGT_CROP_BOUNDS:
			/* complete frame */
			s->r.left = 0;
			s->r.top = 0;
			s->r.width = frame->width;
			s->r.height = frame->height;
			break;
		default:
			dev_err(ctx->bdisp_dev->dev, "Invalid target\n");
			return -EINVAL;
		}
		break;

	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		switch (s->target) {
		case V4L2_SEL_TGT_COMPOSE:
		case V4L2_SEL_TGT_COMPOSE_PADDED:
			/* composed (cropped) frame */
			s->r = frame->crop;
			break;
		case V4L2_SEL_TGT_COMPOSE_DEFAULT:
		case V4L2_SEL_TGT_COMPOSE_BOUNDS:
			/* complete frame */
			s->r.left = 0;
			s->r.top = 0;
			s->r.width = frame->width;
			s->r.height = frame->height;
			break;
		default:
			dev_err(ctx->bdisp_dev->dev, "Invalid target\n");
			return -EINVAL;
		}
		break;

	default:
		dev_err(ctx->bdisp_dev->dev, "Invalid type\n");
		return -EINVAL;
	}

	return 0;
}

static int is_rect_enclosed(struct v4l2_rect *a, struct v4l2_rect *b)
{
	/* Return 1 if a is enclosed in b, or 0 otherwise. */

	if (a->left < b->left || a->top < b->top)
		return 0;

	if (a->left + a->width > b->left + b->width)
		return 0;

	if (a->top + a->height > b->top + b->height)
		return 0;

	return 1;
}

static int bdisp_s_selection(struct file *file, void *fh,
			     struct v4l2_selection *s)
{
	struct bdisp_frame *frame;
	struct bdisp_ctx *ctx = fh_to_ctx(fh);
	struct v4l2_rect *in, out;
	bool valid = false;

	if ((s->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) &&
	    (s->target == V4L2_SEL_TGT_CROP))
		valid = true;

	if ((s->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) &&
	    (s->target == V4L2_SEL_TGT_COMPOSE))
		valid = true;

	if (!valid) {
		dev_err(ctx->bdisp_dev->dev, "Invalid type / target\n");
		return -EINVAL;
	}

	frame = ctx_get_frame(ctx, s->type);
	if (IS_ERR(frame)) {
		dev_err(ctx->bdisp_dev->dev, "Invalid frame (%p)\n", frame);
		return PTR_ERR(frame);
	}

	in = &s->r;
	out = *in;

	/* Align and check origin */
	out.left = ALIGN(in->left, frame->fmt->w_align);
	out.top = ALIGN(in->top, frame->fmt->h_align);

	if ((out.left < 0) || (out.left >= frame->width) ||
	    (out.top < 0) || (out.top >= frame->height)) {
		dev_err(ctx->bdisp_dev->dev,
			"Invalid crop: %dx%d@(%d,%d) vs frame: %dx%d\n",
			out.width, out.height, out.left, out.top,
			frame->width, frame->height);
		return -EINVAL;
	}

	/* Align and check size */
	out.width = ALIGN(in->width, frame->fmt->w_align);
	out.height = ALIGN(in->height, frame->fmt->w_align);

	if (((out.left + out.width) > frame->width) ||
	    ((out.top + out.height) > frame->height)) {
		dev_err(ctx->bdisp_dev->dev,
			"Invalid crop: %dx%d@(%d,%d) vs frame: %dx%d\n",
			out.width, out.height, out.left, out.top,
			frame->width, frame->height);
		return -EINVAL;
	}

	/* Checks adjust constraints flags */
	if (s->flags & V4L2_SEL_FLAG_LE && !is_rect_enclosed(&out, in))
		return -ERANGE;

	if (s->flags & V4L2_SEL_FLAG_GE && !is_rect_enclosed(in, &out))
		return -ERANGE;

	if ((out.left != in->left) || (out.top != in->top) ||
	    (out.width != in->width) || (out.height != in->height)) {
		dev_dbg(ctx->bdisp_dev->dev,
			"%s crop updated: %dx%d@(%d,%d) -> %dx%d@(%d,%d)\n",
			__func__, in->width, in->height, in->left, in->top,
			out.width, out.height, out.left, out.top);
		*in = out;
	}

	frame->crop = out;

	bdisp_ctx_state_lock_set(BDISP_PARAMS, ctx);

	return 0;
}

static int bdisp_streamon(struct file *file, void *fh, enum v4l2_buf_type type)
{
	struct bdisp_ctx *ctx = fh_to_ctx(fh);

	if ((type == V4L2_BUF_TYPE_VIDEO_OUTPUT) &&
	    !bdisp_ctx_state_is_set(BDISP_SRC_FMT, ctx)) {
		dev_err(ctx->bdisp_dev->dev, "src not defined\n");
		return -EINVAL;
	}

	if ((type == V4L2_BUF_TYPE_VIDEO_CAPTURE) &&
	    !bdisp_ctx_state_is_set(BDISP_DST_FMT, ctx)) {
		dev_err(ctx->bdisp_dev->dev, "dst not defined\n");
		return -EINVAL;
	}

	return v4l2_m2m_streamon(file, ctx->fh.m2m_ctx, type);
}

static const struct v4l2_ioctl_ops bdisp_ioctl_ops = {
	.vidioc_querycap                = bdisp_querycap,
	.vidioc_enum_fmt_vid_cap        = bdisp_enum_fmt,
	.vidioc_enum_fmt_vid_out        = bdisp_enum_fmt,
	.vidioc_g_fmt_vid_cap           = bdisp_g_fmt,
	.vidioc_g_fmt_vid_out           = bdisp_g_fmt,
	.vidioc_try_fmt_vid_cap         = bdisp_try_fmt,
	.vidioc_try_fmt_vid_out         = bdisp_try_fmt,
	.vidioc_s_fmt_vid_cap           = bdisp_s_fmt,
	.vidioc_s_fmt_vid_out           = bdisp_s_fmt,
	.vidioc_g_selection		= bdisp_g_selection,
	.vidioc_s_selection		= bdisp_s_selection,
	.vidioc_reqbufs                 = v4l2_m2m_ioctl_reqbufs,
	.vidioc_create_bufs             = v4l2_m2m_ioctl_create_bufs,
	.vidioc_expbuf                  = v4l2_m2m_ioctl_expbuf,
	.vidioc_querybuf                = v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf                    = v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf                   = v4l2_m2m_ioctl_dqbuf,
	.vidioc_streamon                = bdisp_streamon,
	.vidioc_streamoff               = v4l2_m2m_ioctl_streamoff,
	.vidioc_subscribe_event         = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event       = v4l2_event_unsubscribe,
};

static int bdisp_register_device(struct bdisp_dev *bdisp)
{
	int ret;

	if (!bdisp)
		return -ENODEV;

	bdisp->vdev.fops        = &bdisp_fops;
	bdisp->vdev.ioctl_ops   = &bdisp_ioctl_ops;
	bdisp->vdev.release     = video_device_release_empty;
	bdisp->vdev.lock        = &bdisp->lock;
	bdisp->vdev.vfl_dir     = VFL_DIR_M2M;
	bdisp->vdev.v4l2_dev    = &bdisp->v4l2_dev;
	bdisp->vdev.device_caps = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_M2M;
	snprintf(bdisp->vdev.name, sizeof(bdisp->vdev.name), "%s.%d",
		 BDISP_NAME, bdisp->id);

	video_set_drvdata(&bdisp->vdev, bdisp);

	bdisp->m2m.vdev = &bdisp->vdev;
	bdisp->m2m.m2m_dev = v4l2_m2m_init(&bdisp_m2m_ops);
	if (IS_ERR(bdisp->m2m.m2m_dev)) {
		dev_err(bdisp->dev, "failed to initialize v4l2-m2m device\n");
		return PTR_ERR(bdisp->m2m.m2m_dev);
	}

	ret = video_register_device(&bdisp->vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(bdisp->dev,
			"%s(): failed to register video device\n", __func__);
		v4l2_m2m_release(bdisp->m2m.m2m_dev);
		return ret;
	}

	return 0;
}

static void bdisp_unregister_device(struct bdisp_dev *bdisp)
{
	if (!bdisp)
		return;

	if (bdisp->m2m.m2m_dev)
		v4l2_m2m_release(bdisp->m2m.m2m_dev);

	video_unregister_device(bdisp->m2m.vdev);
}

static irqreturn_t bdisp_irq_thread(int irq, void *priv)
{
	struct bdisp_dev *bdisp = priv;
	struct bdisp_ctx *ctx;

	spin_lock(&bdisp->slock);

	bdisp_dbg_perf_end(bdisp);

	cancel_delayed_work(&bdisp->timeout_work);

	if (!test_and_clear_bit(ST_M2M_RUNNING, &bdisp->state))
		goto isr_unlock;

	if (test_and_clear_bit(ST_M2M_SUSPENDING, &bdisp->state)) {
		set_bit(ST_M2M_SUSPENDED, &bdisp->state);
		wake_up(&bdisp->irq_queue);
		goto isr_unlock;
	}

	ctx = v4l2_m2m_get_curr_priv(bdisp->m2m.m2m_dev);
	if (!ctx || !ctx->fh.m2m_ctx)
		goto isr_unlock;

	spin_unlock(&bdisp->slock);

	bdisp_job_finish(ctx, VB2_BUF_STATE_DONE);

	if (bdisp_ctx_state_is_set(BDISP_CTX_STOP_REQ, ctx)) {
		bdisp_ctx_state_lock_clear(BDISP_CTX_STOP_REQ, ctx);
		wake_up(&bdisp->irq_queue);
	}

	return IRQ_HANDLED;

isr_unlock:
	spin_unlock(&bdisp->slock);

	return IRQ_HANDLED;
}

static irqreturn_t bdisp_irq_handler(int irq, void *priv)
{
	if (bdisp_hw_get_and_clear_irq((struct bdisp_dev *)priv))
		return IRQ_NONE;
	else
		return IRQ_WAKE_THREAD;
}

static void bdisp_irq_timeout(struct work_struct *ptr)
{
	struct delayed_work *twork = to_delayed_work(ptr);
	struct bdisp_dev *bdisp = container_of(twork, struct bdisp_dev,
			timeout_work);
	struct bdisp_ctx *ctx;

	ctx = v4l2_m2m_get_curr_priv(bdisp->m2m.m2m_dev);

	dev_err(ctx->bdisp_dev->dev, "Device work timeout\n");

	spin_lock(&bdisp->slock);
	clear_bit(ST_M2M_RUNNING, &bdisp->state);
	spin_unlock(&bdisp->slock);

	bdisp_hw_reset(bdisp);

	bdisp_job_finish(ctx, VB2_BUF_STATE_ERROR);
}

static int bdisp_m2m_suspend(struct bdisp_dev *bdisp)
{
	unsigned long flags;
	int timeout;

	spin_lock_irqsave(&bdisp->slock, flags);
	if (!test_bit(ST_M2M_RUNNING, &bdisp->state)) {
		spin_unlock_irqrestore(&bdisp->slock, flags);
		return 0;
	}
	clear_bit(ST_M2M_SUSPENDED, &bdisp->state);
	set_bit(ST_M2M_SUSPENDING, &bdisp->state);
	spin_unlock_irqrestore(&bdisp->slock, flags);

	timeout = wait_event_timeout(bdisp->irq_queue,
				     test_bit(ST_M2M_SUSPENDED, &bdisp->state),
				     BDISP_WORK_TIMEOUT);

	clear_bit(ST_M2M_SUSPENDING, &bdisp->state);

	if (!timeout) {
		dev_err(bdisp->dev, "%s IRQ timeout\n", __func__);
		return -EAGAIN;
	}

	return 0;
}

static int bdisp_m2m_resume(struct bdisp_dev *bdisp)
{
	struct bdisp_ctx *ctx;
	unsigned long flags;

	spin_lock_irqsave(&bdisp->slock, flags);
	ctx = bdisp->m2m.ctx;
	bdisp->m2m.ctx = NULL;
	spin_unlock_irqrestore(&bdisp->slock, flags);

	if (test_and_clear_bit(ST_M2M_SUSPENDED, &bdisp->state))
		bdisp_job_finish(ctx, VB2_BUF_STATE_ERROR);

	return 0;
}

static int bdisp_runtime_resume(struct device *dev)
{
	struct bdisp_dev *bdisp = dev_get_drvdata(dev);
	int ret = clk_enable(bdisp->clock);

	if (ret)
		return ret;

	return bdisp_m2m_resume(bdisp);
}

static int bdisp_runtime_suspend(struct device *dev)
{
	struct bdisp_dev *bdisp = dev_get_drvdata(dev);
	int ret = bdisp_m2m_suspend(bdisp);

	if (!ret)
		clk_disable(bdisp->clock);

	return ret;
}

static int bdisp_resume(struct device *dev)
{
	struct bdisp_dev *bdisp = dev_get_drvdata(dev);
	unsigned long flags;
	int opened;

	spin_lock_irqsave(&bdisp->slock, flags);
	opened = test_bit(ST_M2M_OPEN, &bdisp->state);
	spin_unlock_irqrestore(&bdisp->slock, flags);

	if (!opened)
		return 0;

	if (!pm_runtime_suspended(dev))
		return bdisp_runtime_resume(dev);

	return 0;
}

static int bdisp_suspend(struct device *dev)
{
	if (!pm_runtime_suspended(dev))
		return bdisp_runtime_suspend(dev);

	return 0;
}

static const struct dev_pm_ops bdisp_pm_ops = {
	.suspend                = bdisp_suspend,
	.resume                 = bdisp_resume,
	.runtime_suspend        = bdisp_runtime_suspend,
	.runtime_resume         = bdisp_runtime_resume,
};

static int bdisp_remove(struct platform_device *pdev)
{
	struct bdisp_dev *bdisp = platform_get_drvdata(pdev);

	bdisp_unregister_device(bdisp);

	bdisp_hw_free_filters(bdisp->dev);

	pm_runtime_disable(&pdev->dev);

	bdisp_debugfs_remove(bdisp);

	v4l2_device_unregister(&bdisp->v4l2_dev);

	if (!IS_ERR(bdisp->clock))
		clk_unprepare(bdisp->clock);

	destroy_workqueue(bdisp->work_queue);

	dev_dbg(&pdev->dev, "%s driver unloaded\n", pdev->name);

	return 0;
}

static int bdisp_probe(struct platform_device *pdev)
{
	struct bdisp_dev *bdisp;
	struct resource *res;
	struct device *dev = &pdev->dev;
	int ret;

	dev_dbg(dev, "%s\n", __func__);

	bdisp = devm_kzalloc(dev, sizeof(struct bdisp_dev), GFP_KERNEL);
	if (!bdisp)
		return -ENOMEM;

	ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	bdisp->pdev = pdev;
	bdisp->dev = dev;
	platform_set_drvdata(pdev, bdisp);

	if (dev->of_node)
		bdisp->id = of_alias_get_id(pdev->dev.of_node, BDISP_NAME);
	else
		bdisp->id = pdev->id;

	init_waitqueue_head(&bdisp->irq_queue);
	INIT_DELAYED_WORK(&bdisp->timeout_work, bdisp_irq_timeout);
	bdisp->work_queue = create_workqueue(BDISP_NAME);

	spin_lock_init(&bdisp->slock);
	mutex_init(&bdisp->lock);

	/* get resources */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	bdisp->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(bdisp->regs)) {
		dev_err(dev, "failed to get regs\n");
		ret = PTR_ERR(bdisp->regs);
		goto err_wq;
	}

	bdisp->clock = devm_clk_get(dev, BDISP_NAME);
	if (IS_ERR(bdisp->clock)) {
		dev_err(dev, "failed to get clock\n");
		ret = PTR_ERR(bdisp->clock);
		goto err_wq;
	}

	ret = clk_prepare(bdisp->clock);
	if (ret < 0) {
		dev_err(dev, "clock prepare failed\n");
		bdisp->clock = ERR_PTR(-EINVAL);
		goto err_wq;
	}

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(dev, "failed to get IRQ resource\n");
		ret = -EINVAL;
		goto err_clk;
	}

	ret = devm_request_threaded_irq(dev, res->start, bdisp_irq_handler,
					bdisp_irq_thread, IRQF_ONESHOT,
					pdev->name, bdisp);
	if (ret) {
		dev_err(dev, "failed to install irq\n");
		goto err_clk;
	}

	/* v4l2 register */
	ret = v4l2_device_register(dev, &bdisp->v4l2_dev);
	if (ret) {
		dev_err(dev, "failed to register\n");
		goto err_clk;
	}

	/* Debug */
	ret = bdisp_debugfs_create(bdisp);
	if (ret) {
		dev_err(dev, "failed to create debugfs\n");
		goto err_v4l2;
	}

	/* Power management */
	pm_runtime_enable(dev);
	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_err(dev, "failed to set PM\n");
		goto err_dbg;
	}

	/* Filters */
	if (bdisp_hw_alloc_filters(bdisp->dev)) {
		dev_err(bdisp->dev, "no memory for filters\n");
		ret = -ENOMEM;
		goto err_pm;
	}

	/* Register */
	ret = bdisp_register_device(bdisp);
	if (ret) {
		dev_err(dev, "failed to register\n");
		goto err_filter;
	}

	dev_info(dev, "%s%d registered as /dev/video%d\n", BDISP_NAME,
		 bdisp->id, bdisp->vdev.num);

	pm_runtime_put(dev);

	return 0;

err_filter:
	bdisp_hw_free_filters(bdisp->dev);
err_pm:
	pm_runtime_put(dev);
err_dbg:
	bdisp_debugfs_remove(bdisp);
err_v4l2:
	v4l2_device_unregister(&bdisp->v4l2_dev);
err_clk:
	if (!IS_ERR(bdisp->clock))
		clk_unprepare(bdisp->clock);
err_wq:
	destroy_workqueue(bdisp->work_queue);
	return ret;
}

static const struct of_device_id bdisp_match_types[] = {
	{
		.compatible = "st,stih407-bdisp",
	},
	{ /* end node */ }
};

MODULE_DEVICE_TABLE(of, bdisp_match_types);

static struct platform_driver bdisp_driver = {
	.probe          = bdisp_probe,
	.remove         = bdisp_remove,
	.driver         = {
		.name           = BDISP_NAME,
		.of_match_table = bdisp_match_types,
		.pm             = &bdisp_pm_ops,
	},
};

module_platform_driver(bdisp_driver);

MODULE_DESCRIPTION("2D blitter for STMicroelectronics SoC");
MODULE_AUTHOR("Fabien Dessenne <fabien.dessenne@st.com>");
MODULE_LICENSE("GPL");
