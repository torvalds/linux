// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Fuzhou Rockchip Electronics Co.Ltd
 * Author: Jacob Chen <jacob-chen@iotwrt.com>
 */

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/timer.h>

#include <linux/platform_device.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-sg.h>
#include <media/videobuf2-v4l2.h>

#include "rga-hw.h"
#include "rga.h"

static int debug;
module_param(debug, int, 0644);

static void device_run(void *prv)
{
	struct rga_ctx *ctx = prv;
	struct rockchip_rga *rga = ctx->rga;
	struct vb2_v4l2_buffer *src, *dst;
	unsigned long flags;

	spin_lock_irqsave(&rga->ctrl_lock, flags);

	rga->curr = ctx;

	src = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	dst = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);

	rga_buf_map(&src->vb2_buf);
	rga_buf_map(&dst->vb2_buf);

	rga_hw_start(rga);

	spin_unlock_irqrestore(&rga->ctrl_lock, flags);
}

static irqreturn_t rga_isr(int irq, void *prv)
{
	struct rockchip_rga *rga = prv;
	int intr;

	intr = rga_read(rga, RGA_INT) & 0xf;

	rga_mod(rga, RGA_INT, intr << 4, 0xf << 4);

	if (intr & 0x04) {
		struct vb2_v4l2_buffer *src, *dst;
		struct rga_ctx *ctx = rga->curr;

		WARN_ON(!ctx);

		rga->curr = NULL;

		src = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		dst = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);

		WARN_ON(!src);
		WARN_ON(!dst);

		dst->timecode = src->timecode;
		dst->vb2_buf.timestamp = src->vb2_buf.timestamp;
		dst->flags &= ~V4L2_BUF_FLAG_TSTAMP_SRC_MASK;
		dst->flags |= src->flags & V4L2_BUF_FLAG_TSTAMP_SRC_MASK;

		v4l2_m2m_buf_done(src, VB2_BUF_STATE_DONE);
		v4l2_m2m_buf_done(dst, VB2_BUF_STATE_DONE);
		v4l2_m2m_job_finish(rga->m2m_dev, ctx->fh.m2m_ctx);
	}

	return IRQ_HANDLED;
}

static const struct v4l2_m2m_ops rga_m2m_ops = {
	.device_run = device_run,
};

static int
queue_init(void *priv, struct vb2_queue *src_vq, struct vb2_queue *dst_vq)
{
	struct rga_ctx *ctx = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->drv_priv = ctx;
	src_vq->ops = &rga_qops;
	src_vq->mem_ops = &vb2_dma_sg_memops;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &ctx->rga->mutex;
	src_vq->dev = ctx->rga->v4l2_dev.dev;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->ops = &rga_qops;
	dst_vq->mem_ops = &vb2_dma_sg_memops;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &ctx->rga->mutex;
	dst_vq->dev = ctx->rga->v4l2_dev.dev;

	return vb2_queue_init(dst_vq);
}

static int rga_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct rga_ctx *ctx = container_of(ctrl->handler, struct rga_ctx,
					   ctrl_handler);
	unsigned long flags;

	spin_lock_irqsave(&ctx->rga->ctrl_lock, flags);
	switch (ctrl->id) {
	case V4L2_CID_HFLIP:
		ctx->hflip = ctrl->val;
		break;
	case V4L2_CID_VFLIP:
		ctx->vflip = ctrl->val;
		break;
	case V4L2_CID_ROTATE:
		ctx->rotate = ctrl->val;
		break;
	case V4L2_CID_BG_COLOR:
		ctx->fill_color = ctrl->val;
		break;
	}
	spin_unlock_irqrestore(&ctx->rga->ctrl_lock, flags);
	return 0;
}

static const struct v4l2_ctrl_ops rga_ctrl_ops = {
	.s_ctrl = rga_s_ctrl,
};

static int rga_setup_ctrls(struct rga_ctx *ctx)
{
	struct rockchip_rga *rga = ctx->rga;

	v4l2_ctrl_handler_init(&ctx->ctrl_handler, 4);

	v4l2_ctrl_new_std(&ctx->ctrl_handler, &rga_ctrl_ops,
			  V4L2_CID_HFLIP, 0, 1, 1, 0);

	v4l2_ctrl_new_std(&ctx->ctrl_handler, &rga_ctrl_ops,
			  V4L2_CID_VFLIP, 0, 1, 1, 0);

	v4l2_ctrl_new_std(&ctx->ctrl_handler, &rga_ctrl_ops,
			  V4L2_CID_ROTATE, 0, 270, 90, 0);

	v4l2_ctrl_new_std(&ctx->ctrl_handler, &rga_ctrl_ops,
			  V4L2_CID_BG_COLOR, 0, 0xffffffff, 1, 0);

	if (ctx->ctrl_handler.error) {
		int err = ctx->ctrl_handler.error;

		v4l2_err(&rga->v4l2_dev, "%s failed\n", __func__);
		v4l2_ctrl_handler_free(&ctx->ctrl_handler);
		return err;
	}

	return 0;
}

static struct rga_fmt formats[] = {
	{
		.fourcc = V4L2_PIX_FMT_ARGB32,
		.color_swap = RGA_COLOR_RB_SWAP,
		.hw_format = RGA_COLOR_FMT_ABGR8888,
		.depth = 32,
		.uv_factor = 1,
		.y_div = 1,
		.x_div = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_XRGB32,
		.color_swap = RGA_COLOR_RB_SWAP,
		.hw_format = RGA_COLOR_FMT_XBGR8888,
		.depth = 32,
		.uv_factor = 1,
		.y_div = 1,
		.x_div = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_ABGR32,
		.color_swap = RGA_COLOR_ALPHA_SWAP,
		.hw_format = RGA_COLOR_FMT_ABGR8888,
		.depth = 32,
		.uv_factor = 1,
		.y_div = 1,
		.x_div = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_XBGR32,
		.color_swap = RGA_COLOR_ALPHA_SWAP,
		.hw_format = RGA_COLOR_FMT_XBGR8888,
		.depth = 32,
		.uv_factor = 1,
		.y_div = 1,
		.x_div = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_RGB24,
		.color_swap = RGA_COLOR_NONE_SWAP,
		.hw_format = RGA_COLOR_FMT_RGB888,
		.depth = 24,
		.uv_factor = 1,
		.y_div = 1,
		.x_div = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_BGR24,
		.color_swap = RGA_COLOR_RB_SWAP,
		.hw_format = RGA_COLOR_FMT_RGB888,
		.depth = 24,
		.uv_factor = 1,
		.y_div = 1,
		.x_div = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_ARGB444,
		.color_swap = RGA_COLOR_RB_SWAP,
		.hw_format = RGA_COLOR_FMT_ABGR4444,
		.depth = 16,
		.uv_factor = 1,
		.y_div = 1,
		.x_div = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_ARGB555,
		.color_swap = RGA_COLOR_RB_SWAP,
		.hw_format = RGA_COLOR_FMT_ABGR1555,
		.depth = 16,
		.uv_factor = 1,
		.y_div = 1,
		.x_div = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_RGB565,
		.color_swap = RGA_COLOR_RB_SWAP,
		.hw_format = RGA_COLOR_FMT_BGR565,
		.depth = 16,
		.uv_factor = 1,
		.y_div = 1,
		.x_div = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV21,
		.color_swap = RGA_COLOR_UV_SWAP,
		.hw_format = RGA_COLOR_FMT_YUV420SP,
		.depth = 12,
		.uv_factor = 4,
		.y_div = 2,
		.x_div = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV61,
		.color_swap = RGA_COLOR_UV_SWAP,
		.hw_format = RGA_COLOR_FMT_YUV422SP,
		.depth = 16,
		.uv_factor = 2,
		.y_div = 1,
		.x_div = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV12,
		.color_swap = RGA_COLOR_NONE_SWAP,
		.hw_format = RGA_COLOR_FMT_YUV420SP,
		.depth = 12,
		.uv_factor = 4,
		.y_div = 2,
		.x_div = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_NV16,
		.color_swap = RGA_COLOR_NONE_SWAP,
		.hw_format = RGA_COLOR_FMT_YUV422SP,
		.depth = 16,
		.uv_factor = 2,
		.y_div = 1,
		.x_div = 1,
	},
	{
		.fourcc = V4L2_PIX_FMT_YUV420,
		.color_swap = RGA_COLOR_NONE_SWAP,
		.hw_format = RGA_COLOR_FMT_YUV420P,
		.depth = 12,
		.uv_factor = 4,
		.y_div = 2,
		.x_div = 2,
	},
	{
		.fourcc = V4L2_PIX_FMT_YUV422P,
		.color_swap = RGA_COLOR_NONE_SWAP,
		.hw_format = RGA_COLOR_FMT_YUV422P,
		.depth = 16,
		.uv_factor = 2,
		.y_div = 1,
		.x_div = 2,
	},
	{
		.fourcc = V4L2_PIX_FMT_YVU420,
		.color_swap = RGA_COLOR_UV_SWAP,
		.hw_format = RGA_COLOR_FMT_YUV420P,
		.depth = 12,
		.uv_factor = 4,
		.y_div = 2,
		.x_div = 2,
	},
};

#define NUM_FORMATS ARRAY_SIZE(formats)

static struct rga_fmt *rga_fmt_find(struct v4l2_format *f)
{
	unsigned int i;

	for (i = 0; i < NUM_FORMATS; i++) {
		if (formats[i].fourcc == f->fmt.pix.pixelformat)
			return &formats[i];
	}
	return NULL;
}

static struct rga_frame def_frame = {
	.width = DEFAULT_WIDTH,
	.height = DEFAULT_HEIGHT,
	.colorspace = V4L2_COLORSPACE_DEFAULT,
	.crop.left = 0,
	.crop.top = 0,
	.crop.width = DEFAULT_WIDTH,
	.crop.height = DEFAULT_HEIGHT,
	.fmt = &formats[0],
};

struct rga_frame *rga_get_frame(struct rga_ctx *ctx, enum v4l2_buf_type type)
{
	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		return &ctx->in;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		return &ctx->out;
	default:
		return ERR_PTR(-EINVAL);
	}
}

static int rga_open(struct file *file)
{
	struct rockchip_rga *rga = video_drvdata(file);
	struct rga_ctx *ctx = NULL;
	int ret = 0;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	ctx->rga = rga;
	/* Set default formats */
	ctx->in = def_frame;
	ctx->out = def_frame;

	if (mutex_lock_interruptible(&rga->mutex)) {
		kfree(ctx);
		return -ERESTARTSYS;
	}
	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(rga->m2m_dev, ctx, &queue_init);
	if (IS_ERR(ctx->fh.m2m_ctx)) {
		ret = PTR_ERR(ctx->fh.m2m_ctx);
		mutex_unlock(&rga->mutex);
		kfree(ctx);
		return ret;
	}
	v4l2_fh_init(&ctx->fh, video_devdata(file));
	file->private_data = &ctx->fh;
	v4l2_fh_add(&ctx->fh);

	rga_setup_ctrls(ctx);

	/* Write the default values to the ctx struct */
	v4l2_ctrl_handler_setup(&ctx->ctrl_handler);

	ctx->fh.ctrl_handler = &ctx->ctrl_handler;
	mutex_unlock(&rga->mutex);

	return 0;
}

static int rga_release(struct file *file)
{
	struct rga_ctx *ctx =
		container_of(file->private_data, struct rga_ctx, fh);
	struct rockchip_rga *rga = ctx->rga;

	mutex_lock(&rga->mutex);

	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);

	v4l2_ctrl_handler_free(&ctx->ctrl_handler);
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	kfree(ctx);

	mutex_unlock(&rga->mutex);

	return 0;
}

static const struct v4l2_file_operations rga_fops = {
	.owner = THIS_MODULE,
	.open = rga_open,
	.release = rga_release,
	.poll = v4l2_m2m_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap = v4l2_m2m_fop_mmap,
};

static int
vidioc_querycap(struct file *file, void *priv, struct v4l2_capability *cap)
{
	strscpy(cap->driver, RGA_NAME, sizeof(cap->driver));
	strscpy(cap->card, "rockchip-rga", sizeof(cap->card));
	strscpy(cap->bus_info, "platform:rga", sizeof(cap->bus_info));

	return 0;
}

static int vidioc_enum_fmt(struct file *file, void *prv, struct v4l2_fmtdesc *f)
{
	struct rga_fmt *fmt;

	if (f->index >= NUM_FORMATS)
		return -EINVAL;

	fmt = &formats[f->index];
	f->pixelformat = fmt->fourcc;

	return 0;
}

static int vidioc_g_fmt(struct file *file, void *prv, struct v4l2_format *f)
{
	struct rga_ctx *ctx = prv;
	struct vb2_queue *vq;
	struct rga_frame *frm;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;
	frm = rga_get_frame(ctx, f->type);
	if (IS_ERR(frm))
		return PTR_ERR(frm);

	f->fmt.pix.width = frm->width;
	f->fmt.pix.height = frm->height;
	f->fmt.pix.field = V4L2_FIELD_NONE;
	f->fmt.pix.pixelformat = frm->fmt->fourcc;
	f->fmt.pix.bytesperline = frm->stride;
	f->fmt.pix.sizeimage = frm->size;
	f->fmt.pix.colorspace = frm->colorspace;

	return 0;
}

static int vidioc_try_fmt(struct file *file, void *prv, struct v4l2_format *f)
{
	struct rga_fmt *fmt;

	fmt = rga_fmt_find(f);
	if (!fmt) {
		fmt = &formats[0];
		f->fmt.pix.pixelformat = fmt->fourcc;
	}

	f->fmt.pix.field = V4L2_FIELD_NONE;

	if (f->fmt.pix.width > MAX_WIDTH)
		f->fmt.pix.width = MAX_WIDTH;
	if (f->fmt.pix.height > MAX_HEIGHT)
		f->fmt.pix.height = MAX_HEIGHT;

	if (f->fmt.pix.width < MIN_WIDTH)
		f->fmt.pix.width = MIN_WIDTH;
	if (f->fmt.pix.height < MIN_HEIGHT)
		f->fmt.pix.height = MIN_HEIGHT;

	if (fmt->hw_format >= RGA_COLOR_FMT_YUV422SP)
		f->fmt.pix.bytesperline = f->fmt.pix.width;
	else
		f->fmt.pix.bytesperline = (f->fmt.pix.width * fmt->depth) >> 3;

	f->fmt.pix.sizeimage =
		f->fmt.pix.height * (f->fmt.pix.width * fmt->depth) >> 3;

	return 0;
}

static int vidioc_s_fmt(struct file *file, void *prv, struct v4l2_format *f)
{
	struct rga_ctx *ctx = prv;
	struct rockchip_rga *rga = ctx->rga;
	struct vb2_queue *vq;
	struct rga_frame *frm;
	struct rga_fmt *fmt;
	int ret = 0;

	/* Adjust all values accordingly to the hardware capabilities
	 * and chosen format.
	 */
	ret = vidioc_try_fmt(file, prv, f);
	if (ret)
		return ret;
	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (vb2_is_busy(vq)) {
		v4l2_err(&rga->v4l2_dev, "queue (%d) bust\n", f->type);
		return -EBUSY;
	}
	frm = rga_get_frame(ctx, f->type);
	if (IS_ERR(frm))
		return PTR_ERR(frm);
	fmt = rga_fmt_find(f);
	if (!fmt)
		return -EINVAL;
	frm->width = f->fmt.pix.width;
	frm->height = f->fmt.pix.height;
	frm->size = f->fmt.pix.sizeimage;
	frm->fmt = fmt;
	frm->stride = f->fmt.pix.bytesperline;
	frm->colorspace = f->fmt.pix.colorspace;

	/* Reset crop settings */
	frm->crop.left = 0;
	frm->crop.top = 0;
	frm->crop.width = frm->width;
	frm->crop.height = frm->height;

	return 0;
}

static int vidioc_g_selection(struct file *file, void *prv,
			      struct v4l2_selection *s)
{
	struct rga_ctx *ctx = prv;
	struct rga_frame *f;
	bool use_frame = false;

	f = rga_get_frame(ctx, s->type);
	if (IS_ERR(f))
		return PTR_ERR(f);

	switch (s->target) {
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		break;
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
			return -EINVAL;
		break;
	case V4L2_SEL_TGT_COMPOSE:
		if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		use_frame = true;
		break;
	case V4L2_SEL_TGT_CROP:
		if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
			return -EINVAL;
		use_frame = true;
		break;
	default:
		return -EINVAL;
	}

	if (use_frame) {
		s->r = f->crop;
	} else {
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = f->width;
		s->r.height = f->height;
	}

	return 0;
}

static int vidioc_s_selection(struct file *file, void *prv,
			      struct v4l2_selection *s)
{
	struct rga_ctx *ctx = prv;
	struct rockchip_rga *rga = ctx->rga;
	struct rga_frame *f;
	int ret = 0;

	f = rga_get_frame(ctx, s->type);
	if (IS_ERR(f))
		return PTR_ERR(f);

	switch (s->target) {
	case V4L2_SEL_TGT_COMPOSE:
		/*
		 * COMPOSE target is only valid for capture buffer type, return
		 * error for output buffer type
		 */
		if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		break;
	case V4L2_SEL_TGT_CROP:
		/*
		 * CROP target is only valid for output buffer type, return
		 * error for capture buffer type
		 */
		if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
			return -EINVAL;
		break;
	/*
	 * bound and default crop/compose targets are invalid targets to
	 * try/set
	 */
	default:
		return -EINVAL;
	}

	if (s->r.top < 0 || s->r.left < 0) {
		v4l2_dbg(debug, 1, &rga->v4l2_dev,
			 "doesn't support negative values for top & left.\n");
		return -EINVAL;
	}

	if (s->r.left + s->r.width > f->width ||
	    s->r.top + s->r.height > f->height ||
	    s->r.width < MIN_WIDTH || s->r.height < MIN_HEIGHT) {
		v4l2_dbg(debug, 1, &rga->v4l2_dev, "unsupported crop value.\n");
		return -EINVAL;
	}

	f->crop = s->r;

	return ret;
}

static const struct v4l2_ioctl_ops rga_ioctl_ops = {
	.vidioc_querycap = vidioc_querycap,

	.vidioc_enum_fmt_vid_cap = vidioc_enum_fmt,
	.vidioc_g_fmt_vid_cap = vidioc_g_fmt,
	.vidioc_try_fmt_vid_cap = vidioc_try_fmt,
	.vidioc_s_fmt_vid_cap = vidioc_s_fmt,

	.vidioc_enum_fmt_vid_out = vidioc_enum_fmt,
	.vidioc_g_fmt_vid_out = vidioc_g_fmt,
	.vidioc_try_fmt_vid_out = vidioc_try_fmt,
	.vidioc_s_fmt_vid_out = vidioc_s_fmt,

	.vidioc_reqbufs = v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf = v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf = v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf = v4l2_m2m_ioctl_dqbuf,
	.vidioc_prepare_buf = v4l2_m2m_ioctl_prepare_buf,
	.vidioc_create_bufs = v4l2_m2m_ioctl_create_bufs,
	.vidioc_expbuf = v4l2_m2m_ioctl_expbuf,

	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,

	.vidioc_streamon = v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff = v4l2_m2m_ioctl_streamoff,

	.vidioc_g_selection = vidioc_g_selection,
	.vidioc_s_selection = vidioc_s_selection,
};

static const struct video_device rga_videodev = {
	.name = "rockchip-rga",
	.fops = &rga_fops,
	.ioctl_ops = &rga_ioctl_ops,
	.minor = -1,
	.release = video_device_release,
	.vfl_dir = VFL_DIR_M2M,
	.device_caps = V4L2_CAP_VIDEO_M2M | V4L2_CAP_STREAMING,
};

static int rga_enable_clocks(struct rockchip_rga *rga)
{
	int ret;

	ret = clk_prepare_enable(rga->sclk);
	if (ret) {
		dev_err(rga->dev, "Cannot enable rga sclk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(rga->aclk);
	if (ret) {
		dev_err(rga->dev, "Cannot enable rga aclk: %d\n", ret);
		goto err_disable_sclk;
	}

	ret = clk_prepare_enable(rga->hclk);
	if (ret) {
		dev_err(rga->dev, "Cannot enable rga hclk: %d\n", ret);
		goto err_disable_aclk;
	}

	return 0;

err_disable_sclk:
	clk_disable_unprepare(rga->sclk);
err_disable_aclk:
	clk_disable_unprepare(rga->aclk);

	return ret;
}

static void rga_disable_clocks(struct rockchip_rga *rga)
{
	clk_disable_unprepare(rga->sclk);
	clk_disable_unprepare(rga->hclk);
	clk_disable_unprepare(rga->aclk);
}

static int rga_parse_dt(struct rockchip_rga *rga)
{
	struct reset_control *core_rst, *axi_rst, *ahb_rst;

	core_rst = devm_reset_control_get(rga->dev, "core");
	if (IS_ERR(core_rst)) {
		dev_err(rga->dev, "failed to get core reset controller\n");
		return PTR_ERR(core_rst);
	}

	axi_rst = devm_reset_control_get(rga->dev, "axi");
	if (IS_ERR(axi_rst)) {
		dev_err(rga->dev, "failed to get axi reset controller\n");
		return PTR_ERR(axi_rst);
	}

	ahb_rst = devm_reset_control_get(rga->dev, "ahb");
	if (IS_ERR(ahb_rst)) {
		dev_err(rga->dev, "failed to get ahb reset controller\n");
		return PTR_ERR(ahb_rst);
	}

	reset_control_assert(core_rst);
	udelay(1);
	reset_control_deassert(core_rst);

	reset_control_assert(axi_rst);
	udelay(1);
	reset_control_deassert(axi_rst);

	reset_control_assert(ahb_rst);
	udelay(1);
	reset_control_deassert(ahb_rst);

	rga->sclk = devm_clk_get(rga->dev, "sclk");
	if (IS_ERR(rga->sclk)) {
		dev_err(rga->dev, "failed to get sclk clock\n");
		return PTR_ERR(rga->sclk);
	}

	rga->aclk = devm_clk_get(rga->dev, "aclk");
	if (IS_ERR(rga->aclk)) {
		dev_err(rga->dev, "failed to get aclk clock\n");
		return PTR_ERR(rga->aclk);
	}

	rga->hclk = devm_clk_get(rga->dev, "hclk");
	if (IS_ERR(rga->hclk)) {
		dev_err(rga->dev, "failed to get hclk clock\n");
		return PTR_ERR(rga->hclk);
	}

	return 0;
}

static int rga_probe(struct platform_device *pdev)
{
	struct rockchip_rga *rga;
	struct video_device *vfd;
	int ret = 0;
	int irq;

	if (!pdev->dev.of_node)
		return -ENODEV;

	rga = devm_kzalloc(&pdev->dev, sizeof(*rga), GFP_KERNEL);
	if (!rga)
		return -ENOMEM;

	rga->dev = &pdev->dev;
	spin_lock_init(&rga->ctrl_lock);
	mutex_init(&rga->mutex);

	ret = rga_parse_dt(rga);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Unable to parse OF data\n");

	pm_runtime_enable(rga->dev);

	rga->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rga->regs)) {
		ret = PTR_ERR(rga->regs);
		goto err_put_clk;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto err_put_clk;
	}

	ret = devm_request_irq(rga->dev, irq, rga_isr, 0,
			       dev_name(rga->dev), rga);
	if (ret < 0) {
		dev_err(rga->dev, "failed to request irq\n");
		goto err_put_clk;
	}

	ret = v4l2_device_register(&pdev->dev, &rga->v4l2_dev);
	if (ret)
		goto err_put_clk;
	vfd = video_device_alloc();
	if (!vfd) {
		v4l2_err(&rga->v4l2_dev, "Failed to allocate video device\n");
		ret = -ENOMEM;
		goto unreg_v4l2_dev;
	}
	*vfd = rga_videodev;
	vfd->lock = &rga->mutex;
	vfd->v4l2_dev = &rga->v4l2_dev;

	video_set_drvdata(vfd, rga);
	rga->vfd = vfd;

	platform_set_drvdata(pdev, rga);
	rga->m2m_dev = v4l2_m2m_init(&rga_m2m_ops);
	if (IS_ERR(rga->m2m_dev)) {
		v4l2_err(&rga->v4l2_dev, "Failed to init mem2mem device\n");
		ret = PTR_ERR(rga->m2m_dev);
		goto rel_vdev;
	}

	ret = pm_runtime_resume_and_get(rga->dev);
	if (ret < 0)
		goto rel_m2m;

	rga->version.major = (rga_read(rga, RGA_VERSION_INFO) >> 24) & 0xFF;
	rga->version.minor = (rga_read(rga, RGA_VERSION_INFO) >> 20) & 0x0F;

	v4l2_info(&rga->v4l2_dev, "HW Version: 0x%02x.%02x\n",
		  rga->version.major, rga->version.minor);

	pm_runtime_put(rga->dev);

	/* Create CMD buffer */
	rga->cmdbuf_virt = dma_alloc_attrs(rga->dev, RGA_CMDBUF_SIZE,
					   &rga->cmdbuf_phy, GFP_KERNEL,
					   DMA_ATTR_WRITE_COMBINE);
	if (!rga->cmdbuf_virt) {
		ret = -ENOMEM;
		goto rel_m2m;
	}

	rga->src_mmu_pages =
		(unsigned int *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, 3);
	if (!rga->src_mmu_pages) {
		ret = -ENOMEM;
		goto free_dma;
	}
	rga->dst_mmu_pages =
		(unsigned int *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, 3);
	if (!rga->dst_mmu_pages) {
		ret = -ENOMEM;
		goto free_src_pages;
	}

	def_frame.stride = (def_frame.width * def_frame.fmt->depth) >> 3;
	def_frame.size = def_frame.stride * def_frame.height;

	ret = video_register_device(vfd, VFL_TYPE_VIDEO, -1);
	if (ret) {
		v4l2_err(&rga->v4l2_dev, "Failed to register video device\n");
		goto free_dst_pages;
	}

	v4l2_info(&rga->v4l2_dev, "Registered %s as /dev/%s\n",
		  vfd->name, video_device_node_name(vfd));

	return 0;

free_dst_pages:
	free_pages((unsigned long)rga->dst_mmu_pages, 3);
free_src_pages:
	free_pages((unsigned long)rga->src_mmu_pages, 3);
free_dma:
	dma_free_attrs(rga->dev, RGA_CMDBUF_SIZE, rga->cmdbuf_virt,
		       rga->cmdbuf_phy, DMA_ATTR_WRITE_COMBINE);
rel_m2m:
	v4l2_m2m_release(rga->m2m_dev);
rel_vdev:
	video_device_release(vfd);
unreg_v4l2_dev:
	v4l2_device_unregister(&rga->v4l2_dev);
err_put_clk:
	pm_runtime_disable(rga->dev);

	return ret;
}

static int rga_remove(struct platform_device *pdev)
{
	struct rockchip_rga *rga = platform_get_drvdata(pdev);

	dma_free_attrs(rga->dev, RGA_CMDBUF_SIZE, rga->cmdbuf_virt,
		       rga->cmdbuf_phy, DMA_ATTR_WRITE_COMBINE);

	free_pages((unsigned long)rga->src_mmu_pages, 3);
	free_pages((unsigned long)rga->dst_mmu_pages, 3);

	v4l2_info(&rga->v4l2_dev, "Removing\n");

	v4l2_m2m_release(rga->m2m_dev);
	video_unregister_device(rga->vfd);
	v4l2_device_unregister(&rga->v4l2_dev);

	pm_runtime_disable(rga->dev);

	return 0;
}

static int __maybe_unused rga_runtime_suspend(struct device *dev)
{
	struct rockchip_rga *rga = dev_get_drvdata(dev);

	rga_disable_clocks(rga);

	return 0;
}

static int __maybe_unused rga_runtime_resume(struct device *dev)
{
	struct rockchip_rga *rga = dev_get_drvdata(dev);

	return rga_enable_clocks(rga);
}

static const struct dev_pm_ops rga_pm = {
	SET_RUNTIME_PM_OPS(rga_runtime_suspend,
			   rga_runtime_resume, NULL)
};

static const struct of_device_id rockchip_rga_match[] = {
	{
		.compatible = "rockchip,rk3288-rga",
	},
	{
		.compatible = "rockchip,rk3399-rga",
	},
	{},
};

MODULE_DEVICE_TABLE(of, rockchip_rga_match);

static struct platform_driver rga_pdrv = {
	.probe = rga_probe,
	.remove = rga_remove,
	.driver = {
		.name = RGA_NAME,
		.pm = &rga_pm,
		.of_match_table = rockchip_rga_match,
	},
};

module_platform_driver(rga_pdrv);

MODULE_AUTHOR("Jacob Chen <jacob-chen@iotwrt.com>");
MODULE_DESCRIPTION("Rockchip Raster 2d Graphic Acceleration Unit");
MODULE_LICENSE("GPL");
