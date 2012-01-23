/*
 * Samsung S5P G2D - 2D Graphics Accelerator Driver
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Kamil Debski, <k.debski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/version.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/interrupt.h>

#include <linux/platform_device.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include "g2d.h"
#include "g2d-regs.h"

#define fh2ctx(__fh) container_of(__fh, struct g2d_ctx, fh)

static struct g2d_fmt formats[] = {
	{
		.name	= "XRGB_8888",
		.fourcc	= V4L2_PIX_FMT_RGB32,
		.depth	= 32,
		.hw	= COLOR_MODE(ORDER_XRGB, MODE_XRGB_8888),
	},
	{
		.name	= "RGB_565",
		.fourcc	= V4L2_PIX_FMT_RGB565X,
		.depth	= 16,
		.hw	= COLOR_MODE(ORDER_XRGB, MODE_RGB_565),
	},
	{
		.name	= "XRGB_1555",
		.fourcc	= V4L2_PIX_FMT_RGB555X,
		.depth	= 16,
		.hw	= COLOR_MODE(ORDER_XRGB, MODE_XRGB_1555),
	},
	{
		.name	= "XRGB_4444",
		.fourcc	= V4L2_PIX_FMT_RGB444,
		.depth	= 16,
		.hw	= COLOR_MODE(ORDER_XRGB, MODE_XRGB_4444),
	},
	{
		.name	= "PACKED_RGB_888",
		.fourcc	= V4L2_PIX_FMT_RGB24,
		.depth	= 24,
		.hw	= COLOR_MODE(ORDER_XRGB, MODE_PACKED_RGB_888),
	},
};
#define NUM_FORMATS ARRAY_SIZE(formats)

struct g2d_frame def_frame = {
	.width		= DEFAULT_WIDTH,
	.height		= DEFAULT_HEIGHT,
	.c_width	= DEFAULT_WIDTH,
	.c_height	= DEFAULT_HEIGHT,
	.o_width	= 0,
	.o_height	= 0,
	.fmt		= &formats[0],
	.right		= DEFAULT_WIDTH,
	.bottom		= DEFAULT_HEIGHT,
};

struct g2d_fmt *find_fmt(struct v4l2_format *f)
{
	unsigned int i;
	for (i = 0; i < NUM_FORMATS; i++) {
		if (formats[i].fourcc == f->fmt.pix.pixelformat)
			return &formats[i];
	}
	return NULL;
}


static struct g2d_frame *get_frame(struct g2d_ctx *ctx,
							enum v4l2_buf_type type)
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

static int g2d_queue_setup(struct vb2_queue *vq, const struct v4l2_format *fmt,
			   unsigned int *nbuffers, unsigned int *nplanes,
			   unsigned int sizes[], void *alloc_ctxs[])
{
	struct g2d_ctx *ctx = vb2_get_drv_priv(vq);
	struct g2d_frame *f = get_frame(ctx, vq->type);

	if (IS_ERR(f))
		return PTR_ERR(f);

	sizes[0] = f->size;
	*nplanes = 1;
	alloc_ctxs[0] = ctx->dev->alloc_ctx;

	if (*nbuffers == 0)
		*nbuffers = 1;

	return 0;
}

static int g2d_buf_prepare(struct vb2_buffer *vb)
{
	struct g2d_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct g2d_frame *f = get_frame(ctx, vb->vb2_queue->type);

	if (IS_ERR(f))
		return PTR_ERR(f);
	vb2_set_plane_payload(vb, 0, f->size);
	return 0;
}

static void g2d_buf_queue(struct vb2_buffer *vb)
{
	struct g2d_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	v4l2_m2m_buf_queue(ctx->m2m_ctx, vb);
}


static struct vb2_ops g2d_qops = {
	.queue_setup	= g2d_queue_setup,
	.buf_prepare	= g2d_buf_prepare,
	.buf_queue	= g2d_buf_queue,
};

static int queue_init(void *priv, struct vb2_queue *src_vq,
						struct vb2_queue *dst_vq)
{
	struct g2d_ctx *ctx = priv;
	int ret;

	memset(src_vq, 0, sizeof(*src_vq));
	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	src_vq->io_modes = VB2_MMAP | VB2_USERPTR;
	src_vq->drv_priv = ctx;
	src_vq->ops = &g2d_qops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	memset(dst_vq, 0, sizeof(*dst_vq));
	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dst_vq->io_modes = VB2_MMAP | VB2_USERPTR;
	dst_vq->drv_priv = ctx;
	dst_vq->ops = &g2d_qops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);

	return vb2_queue_init(dst_vq);
}

static int g2d_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct g2d_ctx *ctx = container_of(ctrl->handler, struct g2d_ctx,
								ctrl_handler);
	switch (ctrl->id) {
	case V4L2_CID_COLORFX:
		if (ctrl->val == V4L2_COLORFX_NEGATIVE)
			ctx->rop = ROP4_INVERT;
		else
			ctx->rop = ROP4_COPY;
		break;
	default:
		v4l2_err(&ctx->dev->v4l2_dev, "unknown control\n");
		return -EINVAL;
	}
	return 0;
}

static const struct v4l2_ctrl_ops g2d_ctrl_ops = {
	.s_ctrl		= g2d_s_ctrl,
};

int g2d_setup_ctrls(struct g2d_ctx *ctx)
{
	struct g2d_dev *dev = ctx->dev;

	v4l2_ctrl_handler_init(&ctx->ctrl_handler, 1);
	if (ctx->ctrl_handler.error) {
		v4l2_err(&dev->v4l2_dev, "v4l2_ctrl_handler_init failed\n");
		return ctx->ctrl_handler.error;
	}

	v4l2_ctrl_new_std_menu(
		&ctx->ctrl_handler,
		&g2d_ctrl_ops,
		V4L2_CID_COLORFX,
		V4L2_COLORFX_NEGATIVE,
		~((1 << V4L2_COLORFX_NONE) | (1 << V4L2_COLORFX_NEGATIVE)),
		V4L2_COLORFX_NONE);

	if (ctx->ctrl_handler.error) {
		v4l2_err(&dev->v4l2_dev, "v4l2_ctrl_handler_init failed\n");
		return ctx->ctrl_handler.error;
	}

	return 0;
}

static int g2d_open(struct file *file)
{
	struct g2d_dev *dev = video_drvdata(file);
	struct g2d_ctx *ctx = NULL;
	int ret = 0;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	ctx->dev = dev;
	/* Set default formats */
	ctx->in		= def_frame;
	ctx->out	= def_frame;

	ctx->m2m_ctx = v4l2_m2m_ctx_init(dev->m2m_dev, ctx, &queue_init);
	if (IS_ERR(ctx->m2m_ctx)) {
		ret = PTR_ERR(ctx->m2m_ctx);
		kfree(ctx);
		return ret;
	}
	v4l2_fh_init(&ctx->fh, video_devdata(file));
	file->private_data = &ctx->fh;
	v4l2_fh_add(&ctx->fh);

	g2d_setup_ctrls(ctx);

	/* Write the default values to the ctx struct */
	v4l2_ctrl_handler_setup(&ctx->ctrl_handler);

	ctx->fh.ctrl_handler = &ctx->ctrl_handler;

	v4l2_info(&dev->v4l2_dev, "instance opened\n");
	return 0;
}

static int g2d_release(struct file *file)
{
	struct g2d_dev *dev = video_drvdata(file);
	struct g2d_ctx *ctx = fh2ctx(file->private_data);

	v4l2_ctrl_handler_free(&ctx->ctrl_handler);
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	kfree(ctx);
	v4l2_info(&dev->v4l2_dev, "instance closed\n");
	return 0;
}


static int vidioc_querycap(struct file *file, void *priv,
				struct v4l2_capability *cap)
{
	strncpy(cap->driver, G2D_NAME, sizeof(cap->driver) - 1);
	strncpy(cap->card, G2D_NAME, sizeof(cap->card) - 1);
	cap->bus_info[0] = 0;
	cap->version = KERNEL_VERSION(1, 0, 0);
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_OUTPUT
							| V4L2_CAP_STREAMING;
	return 0;
}

static int vidioc_enum_fmt(struct file *file, void *prv, struct v4l2_fmtdesc *f)
{
	struct g2d_fmt *fmt;
	if (f->index >= NUM_FORMATS)
		return -EINVAL;
	fmt = &formats[f->index];
	f->pixelformat = fmt->fourcc;
	strncpy(f->description, fmt->name, sizeof(f->description) - 1);
	return 0;
}

static int vidioc_g_fmt(struct file *file, void *prv, struct v4l2_format *f)
{
	struct g2d_ctx *ctx = prv;
	struct vb2_queue *vq;
	struct g2d_frame *frm;

	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;
	frm = get_frame(ctx, f->type);
	if (IS_ERR(frm))
		return PTR_ERR(frm);

	f->fmt.pix.width		= frm->width;
	f->fmt.pix.height		= frm->height;
	f->fmt.pix.field		= V4L2_FIELD_NONE;
	f->fmt.pix.pixelformat		= frm->fmt->fourcc;
	f->fmt.pix.bytesperline		= (frm->width * frm->fmt->depth) >> 3;
	f->fmt.pix.sizeimage		= frm->size;
	return 0;
}

static int vidioc_try_fmt(struct file *file, void *prv, struct v4l2_format *f)
{
	struct g2d_fmt *fmt;
	enum v4l2_field *field;

	fmt = find_fmt(f);
	if (!fmt)
		return -EINVAL;

	field = &f->fmt.pix.field;
	if (*field == V4L2_FIELD_ANY)
		*field = V4L2_FIELD_NONE;
	else if (*field != V4L2_FIELD_NONE)
		return -EINVAL;

	if (f->fmt.pix.width > MAX_WIDTH)
		f->fmt.pix.width = MAX_WIDTH;
	if (f->fmt.pix.height > MAX_HEIGHT)
		f->fmt.pix.height = MAX_HEIGHT;

	if (f->fmt.pix.width < 1)
		f->fmt.pix.width = 1;
	if (f->fmt.pix.height < 1)
		f->fmt.pix.height = 1;

	f->fmt.pix.bytesperline = (f->fmt.pix.width * fmt->depth) >> 3;
	f->fmt.pix.sizeimage = f->fmt.pix.height * f->fmt.pix.bytesperline;
	return 0;
}

static int vidioc_s_fmt(struct file *file, void *prv, struct v4l2_format *f)
{
	struct g2d_ctx *ctx = prv;
	struct g2d_dev *dev = ctx->dev;
	struct vb2_queue *vq;
	struct g2d_frame *frm;
	struct g2d_fmt *fmt;
	int ret = 0;

	/* Adjust all values accordingly to the hardware capabilities
	 * and chosen format. */
	ret = vidioc_try_fmt(file, prv, f);
	if (ret)
		return ret;
	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, f->type);
	if (vb2_is_busy(vq)) {
		v4l2_err(&dev->v4l2_dev, "queue (%d) bust\n", f->type);
		return -EBUSY;
	}
	frm = get_frame(ctx, f->type);
	if (IS_ERR(frm))
		return PTR_ERR(frm);
	fmt = find_fmt(f);
	if (!fmt)
		return -EINVAL;
	frm->width	= f->fmt.pix.width;
	frm->height	= f->fmt.pix.height;
	frm->size	= f->fmt.pix.sizeimage;
	/* Reset crop settings */
	frm->o_width	= 0;
	frm->o_height	= 0;
	frm->c_width	= frm->width;
	frm->c_height	= frm->height;
	frm->right	= frm->width;
	frm->bottom	= frm->height;
	frm->fmt	= fmt;
	frm->stride	= f->fmt.pix.bytesperline;
	return 0;
}

static unsigned int g2d_poll(struct file *file, struct poll_table_struct *wait)
{
	struct g2d_ctx *ctx = fh2ctx(file->private_data);
	return v4l2_m2m_poll(file, ctx->m2m_ctx, wait);
}

static int g2d_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct g2d_ctx *ctx = fh2ctx(file->private_data);
	return v4l2_m2m_mmap(file, ctx->m2m_ctx, vma);
}

static int vidioc_reqbufs(struct file *file, void *priv,
			struct v4l2_requestbuffers *reqbufs)
{
	struct g2d_ctx *ctx = priv;
	return v4l2_m2m_reqbufs(file, ctx->m2m_ctx, reqbufs);
}

static int vidioc_querybuf(struct file *file, void *priv,
			struct v4l2_buffer *buf)
{
	struct g2d_ctx *ctx = priv;
	return v4l2_m2m_querybuf(file, ctx->m2m_ctx, buf);
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct g2d_ctx *ctx = priv;
	return v4l2_m2m_qbuf(file, ctx->m2m_ctx, buf);
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct g2d_ctx *ctx = priv;
	return v4l2_m2m_dqbuf(file, ctx->m2m_ctx, buf);
}


static int vidioc_streamon(struct file *file, void *priv,
					enum v4l2_buf_type type)
{
	struct g2d_ctx *ctx = priv;
	return v4l2_m2m_streamon(file, ctx->m2m_ctx, type);
}

static int vidioc_streamoff(struct file *file, void *priv,
					enum v4l2_buf_type type)
{
	struct g2d_ctx *ctx = priv;
	return v4l2_m2m_streamoff(file, ctx->m2m_ctx, type);
}

static int vidioc_cropcap(struct file *file, void *priv,
					struct v4l2_cropcap *cr)
{
	struct g2d_ctx *ctx = priv;
	struct g2d_frame *f;

	f = get_frame(ctx, cr->type);
	if (IS_ERR(f))
		return PTR_ERR(f);

	cr->bounds.left		= 0;
	cr->bounds.top		= 0;
	cr->bounds.width	= f->width;
	cr->bounds.height	= f->height;
	cr->defrect		= cr->bounds;
	return 0;
}

static int vidioc_g_crop(struct file *file, void *prv, struct v4l2_crop *cr)
{
	struct g2d_ctx *ctx = prv;
	struct g2d_frame *f;

	f = get_frame(ctx, cr->type);
	if (IS_ERR(f))
		return PTR_ERR(f);

	cr->c.left	= f->o_height;
	cr->c.top	= f->o_width;
	cr->c.width	= f->c_width;
	cr->c.height	= f->c_height;
	return 0;
}

static int vidioc_try_crop(struct file *file, void *prv, struct v4l2_crop *cr)
{
	struct g2d_ctx *ctx = prv;
	struct g2d_dev *dev = ctx->dev;
	struct g2d_frame *f;

	f = get_frame(ctx, cr->type);
	if (IS_ERR(f))
		return PTR_ERR(f);

	if (cr->c.top < 0 || cr->c.left < 0) {
		v4l2_err(&dev->v4l2_dev,
			"doesn't support negative values for top & left\n");
		return -EINVAL;
	}

	return 0;
}

static int vidioc_s_crop(struct file *file, void *prv, struct v4l2_crop *cr)
{
	struct g2d_ctx *ctx = prv;
	struct g2d_frame *f;
	int ret;

	ret = vidioc_try_crop(file, prv, cr);
	if (ret)
		return ret;
	f = get_frame(ctx, cr->type);
	if (IS_ERR(f))
		return PTR_ERR(f);

	f->c_width	= cr->c.width;
	f->c_height	= cr->c.height;
	f->o_width	= cr->c.left;
	f->o_height	= cr->c.top;
	f->bottom	= f->o_height + f->c_height;
	f->right	= f->o_width + f->c_width;
	return 0;
}

static void g2d_lock(void *prv)
{
	struct g2d_ctx *ctx = prv;
	struct g2d_dev *dev = ctx->dev;
	mutex_lock(&dev->mutex);
}

static void g2d_unlock(void *prv)
{
	struct g2d_ctx *ctx = prv;
	struct g2d_dev *dev = ctx->dev;
	mutex_unlock(&dev->mutex);
}

static void job_abort(void *prv)
{
	struct g2d_ctx *ctx = prv;
	struct g2d_dev *dev = ctx->dev;
	int ret;

	if (dev->curr == 0) /* No job currently running */
		return;

	ret = wait_event_timeout(dev->irq_queue,
		dev->curr == 0,
		msecs_to_jiffies(G2D_TIMEOUT));
}

static void device_run(void *prv)
{
	struct g2d_ctx *ctx = prv;
	struct g2d_dev *dev = ctx->dev;
	struct vb2_buffer *src, *dst;
	u32 cmd = 0;

	dev->curr = ctx;

	src = v4l2_m2m_next_src_buf(ctx->m2m_ctx);
	dst = v4l2_m2m_next_dst_buf(ctx->m2m_ctx);

	clk_enable(dev->gate);
	g2d_reset(dev);

	g2d_set_src_size(dev, &ctx->in);
	g2d_set_src_addr(dev, vb2_dma_contig_plane_dma_addr(src, 0));

	g2d_set_dst_size(dev, &ctx->out);
	g2d_set_dst_addr(dev, vb2_dma_contig_plane_dma_addr(dst, 0));

	g2d_set_rop4(dev, ctx->rop);
	if (ctx->in.c_width != ctx->out.c_width ||
		ctx->in.c_height != ctx->out.c_height)
		cmd |= g2d_cmd_stretch(1);
	g2d_set_cmd(dev, cmd);
	g2d_start(dev);
}

static irqreturn_t g2d_isr(int irq, void *prv)
{
	struct g2d_dev *dev = prv;
	struct g2d_ctx *ctx = dev->curr;
	struct vb2_buffer *src, *dst;

	g2d_clear_int(dev);
	clk_disable(dev->gate);

	BUG_ON(ctx == 0);

	src = v4l2_m2m_src_buf_remove(ctx->m2m_ctx);
	dst = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx);

	BUG_ON(src == 0);
	BUG_ON(dst == 0);

	v4l2_m2m_buf_done(src, VB2_BUF_STATE_DONE);
	v4l2_m2m_buf_done(dst, VB2_BUF_STATE_DONE);
	v4l2_m2m_job_finish(dev->m2m_dev, ctx->m2m_ctx);

	dev->curr = 0;
	wake_up(&dev->irq_queue);
	return IRQ_HANDLED;
}

static const struct v4l2_file_operations g2d_fops = {
	.owner		= THIS_MODULE,
	.open		= g2d_open,
	.release	= g2d_release,
	.poll		= g2d_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= g2d_mmap,
};

static const struct v4l2_ioctl_ops g2d_ioctl_ops = {
	.vidioc_querycap	= vidioc_querycap,

	.vidioc_enum_fmt_vid_cap	= vidioc_enum_fmt,
	.vidioc_g_fmt_vid_cap		= vidioc_g_fmt,
	.vidioc_try_fmt_vid_cap		= vidioc_try_fmt,
	.vidioc_s_fmt_vid_cap		= vidioc_s_fmt,

	.vidioc_enum_fmt_vid_out	= vidioc_enum_fmt,
	.vidioc_g_fmt_vid_out		= vidioc_g_fmt,
	.vidioc_try_fmt_vid_out		= vidioc_try_fmt,
	.vidioc_s_fmt_vid_out		= vidioc_s_fmt,

	.vidioc_reqbufs			= vidioc_reqbufs,
	.vidioc_querybuf		= vidioc_querybuf,

	.vidioc_qbuf			= vidioc_qbuf,
	.vidioc_dqbuf			= vidioc_dqbuf,

	.vidioc_streamon		= vidioc_streamon,
	.vidioc_streamoff		= vidioc_streamoff,

	.vidioc_g_crop			= vidioc_g_crop,
	.vidioc_s_crop			= vidioc_s_crop,
	.vidioc_cropcap			= vidioc_cropcap,
};

static struct video_device g2d_videodev = {
	.name		= G2D_NAME,
	.fops		= &g2d_fops,
	.ioctl_ops	= &g2d_ioctl_ops,
	.minor		= -1,
	.release	= video_device_release,
};

static struct v4l2_m2m_ops g2d_m2m_ops = {
	.device_run	= device_run,
	.job_abort	= job_abort,
	.lock		= g2d_lock,
	.unlock		= g2d_unlock,
};

static int g2d_probe(struct platform_device *pdev)
{
	struct g2d_dev *dev;
	struct video_device *vfd;
	struct resource *res;
	int ret = 0;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	spin_lock_init(&dev->irqlock);
	mutex_init(&dev->mutex);
	atomic_set(&dev->num_inst, 0);
	init_waitqueue_head(&dev->irq_queue);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to find registers\n");
		ret = -ENOENT;
		goto free_dev;
	}

	dev->res_regs = request_mem_region(res->start, resource_size(res),
						dev_name(&pdev->dev));

	if (!dev->res_regs) {
		dev_err(&pdev->dev, "failed to obtain register region\n");
		ret = -ENOENT;
		goto free_dev;
	}

	dev->regs = ioremap(res->start, resource_size(res));
	if (!dev->regs) {
		dev_err(&pdev->dev, "failed to map registers\n");
		ret = -ENOENT;
		goto rel_res_regs;
	}

	dev->clk = clk_get(&pdev->dev, "sclk_fimg2d");
	if (IS_ERR_OR_NULL(dev->clk)) {
		dev_err(&pdev->dev, "failed to get g2d clock\n");
		ret = -ENXIO;
		goto unmap_regs;
	}

	dev->gate = clk_get(&pdev->dev, "fimg2d");
	if (IS_ERR_OR_NULL(dev->gate)) {
		dev_err(&pdev->dev, "failed to get g2d clock gate\n");
		ret = -ENXIO;
		goto put_clk;
	}

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to find IRQ\n");
		ret = -ENXIO;
		goto put_clk_gate;
	}

	dev->irq = res->start;

	ret = request_irq(dev->irq, g2d_isr, 0, pdev->name, dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to install IRQ\n");
		goto put_clk_gate;
	}

	dev->alloc_ctx = vb2_dma_contig_init_ctx(&pdev->dev);
	if (IS_ERR(dev->alloc_ctx)) {
		ret = PTR_ERR(dev->alloc_ctx);
		goto rel_irq;
	}

	ret = v4l2_device_register(&pdev->dev, &dev->v4l2_dev);
	if (ret)
		goto alloc_ctx_cleanup;
	vfd = video_device_alloc();
	if (!vfd) {
		v4l2_err(&dev->v4l2_dev, "Failed to allocate video device\n");
		ret = -ENOMEM;
		goto unreg_v4l2_dev;
	}
	*vfd = g2d_videodev;
	vfd->lock = &dev->mutex;
	ret = video_register_device(vfd, VFL_TYPE_GRABBER, 0);
	if (ret) {
		v4l2_err(&dev->v4l2_dev, "Failed to register video device\n");
		goto rel_vdev;
	}
	video_set_drvdata(vfd, dev);
	snprintf(vfd->name, sizeof(vfd->name), "%s", g2d_videodev.name);
	dev->vfd = vfd;
	v4l2_info(&dev->v4l2_dev, "device registered as /dev/video%d\n",
								vfd->num);
	platform_set_drvdata(pdev, dev);
	dev->m2m_dev = v4l2_m2m_init(&g2d_m2m_ops);
	if (IS_ERR(dev->m2m_dev)) {
		v4l2_err(&dev->v4l2_dev, "Failed to init mem2mem device\n");
		ret = PTR_ERR(dev->m2m_dev);
		goto unreg_video_dev;
	}

	def_frame.stride = (def_frame.width * def_frame.fmt->depth) >> 3;

	return 0;

unreg_video_dev:
	video_unregister_device(dev->vfd);
rel_vdev:
	video_device_release(vfd);
unreg_v4l2_dev:
	v4l2_device_unregister(&dev->v4l2_dev);
alloc_ctx_cleanup:
	vb2_dma_contig_cleanup_ctx(dev->alloc_ctx);
rel_irq:
	free_irq(dev->irq, dev);
put_clk_gate:
	clk_put(dev->gate);
put_clk:
	clk_put(dev->clk);
unmap_regs:
	iounmap(dev->regs);
rel_res_regs:
	release_resource(dev->res_regs);
free_dev:
	kfree(dev);
	return ret;
}

static int g2d_remove(struct platform_device *pdev)
{
	struct g2d_dev *dev = (struct g2d_dev *)platform_get_drvdata(pdev);

	v4l2_info(&dev->v4l2_dev, "Removing " G2D_NAME);
	v4l2_m2m_release(dev->m2m_dev);
	video_unregister_device(dev->vfd);
	v4l2_device_unregister(&dev->v4l2_dev);
	vb2_dma_contig_cleanup_ctx(dev->alloc_ctx);
	free_irq(dev->irq, dev);
	clk_put(dev->gate);
	clk_put(dev->clk);
	iounmap(dev->regs);
	release_resource(dev->res_regs);
	kfree(dev);
	return 0;
}

static struct platform_driver g2d_pdrv = {
	.probe		= g2d_probe,
	.remove		= g2d_remove,
	.driver		= {
		.name = G2D_NAME,
		.owner = THIS_MODULE,
	},
};

module_platform_driver(g2d_pdrv);

MODULE_AUTHOR("Kamil Debski <k.debski@samsung.com>");
MODULE_DESCRIPTION("S5P G2D 2d graphics accelerator driver");
MODULE_LICENSE("GPL");
