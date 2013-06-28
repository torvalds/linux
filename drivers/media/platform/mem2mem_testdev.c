/*
 * A virtual v4l2-mem2mem example device.
 *
 * This is a virtual device driver for testing mem-to-mem videobuf framework.
 * It simulates a device that uses memory buffers for both source and
 * destination, processes the data and issues an "irq" (simulated by a timer).
 * The device is capable of multi-instance, multi-buffer-per-transaction
 * operation (via the mem2mem framework).
 *
 * Copyright (c) 2009-2010 Samsung Electronics Co., Ltd.
 * Pawel Osciak, <pawel@osciak.com>
 * Marek Szyprowski, <m.szyprowski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version
 */
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <linux/platform_device.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/videobuf2-vmalloc.h>

#define MEM2MEM_TEST_MODULE_NAME "mem2mem-testdev"

MODULE_DESCRIPTION("Virtual device for mem2mem framework testing");
MODULE_AUTHOR("Pawel Osciak, <pawel@osciak.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1.1");

static unsigned debug;
module_param(debug, uint, 0644);
MODULE_PARM_DESC(debug, "activates debug info");

#define MIN_W 32
#define MIN_H 32
#define MAX_W 640
#define MAX_H 480
#define DIM_ALIGN_MASK 7 /* 8-byte alignment for line length */

/* Flags that indicate a format can be used for capture/output */
#define MEM2MEM_CAPTURE	(1 << 0)
#define MEM2MEM_OUTPUT	(1 << 1)

#define MEM2MEM_NAME		"m2m-testdev"

/* Per queue */
#define MEM2MEM_DEF_NUM_BUFS	VIDEO_MAX_FRAME
/* In bytes, per queue */
#define MEM2MEM_VID_MEM_LIMIT	(16 * 1024 * 1024)

/* Default transaction time in msec */
#define MEM2MEM_DEF_TRANSTIME	1000
/* Default number of buffers per transaction */
#define MEM2MEM_DEF_TRANSLEN	1
#define MEM2MEM_COLOR_STEP	(0xff >> 4)
#define MEM2MEM_NUM_TILES	8

/* Flags that indicate processing mode */
#define MEM2MEM_HFLIP	(1 << 0)
#define MEM2MEM_VFLIP	(1 << 1)

#define dprintk(dev, fmt, arg...) \
	v4l2_dbg(1, debug, &dev->v4l2_dev, "%s: " fmt, __func__, ## arg)


static void m2mtest_dev_release(struct device *dev)
{}

static struct platform_device m2mtest_pdev = {
	.name		= MEM2MEM_NAME,
	.dev.release	= m2mtest_dev_release,
};

struct m2mtest_fmt {
	char	*name;
	u32	fourcc;
	int	depth;
	/* Types the format can be used for */
	u32	types;
};

static struct m2mtest_fmt formats[] = {
	{
		.name	= "RGB565 (BE)",
		.fourcc	= V4L2_PIX_FMT_RGB565X, /* rrrrrggg gggbbbbb */
		.depth	= 16,
		/* Both capture and output format */
		.types	= MEM2MEM_CAPTURE | MEM2MEM_OUTPUT,
	},
	{
		.name	= "4:2:2, packed, YUYV",
		.fourcc	= V4L2_PIX_FMT_YUYV,
		.depth	= 16,
		/* Output-only format */
		.types	= MEM2MEM_OUTPUT,
	},
};

#define NUM_FORMATS ARRAY_SIZE(formats)

/* Per-queue, driver-specific private data */
struct m2mtest_q_data {
	unsigned int		width;
	unsigned int		height;
	unsigned int		sizeimage;
	struct m2mtest_fmt	*fmt;
};

enum {
	V4L2_M2M_SRC = 0,
	V4L2_M2M_DST = 1,
};

#define V4L2_CID_TRANS_TIME_MSEC	(V4L2_CID_USER_BASE + 0x1000)
#define V4L2_CID_TRANS_NUM_BUFS		(V4L2_CID_USER_BASE + 0x1001)

static struct m2mtest_fmt *find_format(struct v4l2_format *f)
{
	struct m2mtest_fmt *fmt;
	unsigned int k;

	for (k = 0; k < NUM_FORMATS; k++) {
		fmt = &formats[k];
		if (fmt->fourcc == f->fmt.pix.pixelformat)
			break;
	}

	if (k == NUM_FORMATS)
		return NULL;

	return &formats[k];
}

struct m2mtest_dev {
	struct v4l2_device	v4l2_dev;
	struct video_device	*vfd;

	atomic_t		num_inst;
	struct mutex		dev_mutex;
	spinlock_t		irqlock;

	struct timer_list	timer;

	struct v4l2_m2m_dev	*m2m_dev;
};

struct m2mtest_ctx {
	struct v4l2_fh		fh;
	struct m2mtest_dev	*dev;

	struct v4l2_ctrl_handler hdl;

	/* Processed buffers in this transaction */
	u8			num_processed;

	/* Transaction length (i.e. how many buffers per transaction) */
	u32			translen;
	/* Transaction time (i.e. simulated processing time) in milliseconds */
	u32			transtime;

	/* Abort requested by m2m */
	int			aborting;

	/* Processing mode */
	int			mode;

	enum v4l2_colorspace	colorspace;

	struct v4l2_m2m_ctx	*m2m_ctx;

	/* Source and destination queue data */
	struct m2mtest_q_data   q_data[2];
};

static inline struct m2mtest_ctx *file2ctx(struct file *file)
{
	return container_of(file->private_data, struct m2mtest_ctx, fh);
}

static struct m2mtest_q_data *get_q_data(struct m2mtest_ctx *ctx,
					 enum v4l2_buf_type type)
{
	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		return &ctx->q_data[V4L2_M2M_SRC];
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		return &ctx->q_data[V4L2_M2M_DST];
	default:
		BUG();
	}
	return NULL;
}


static int device_process(struct m2mtest_ctx *ctx,
			  struct vb2_buffer *in_vb,
			  struct vb2_buffer *out_vb)
{
	struct m2mtest_dev *dev = ctx->dev;
	struct m2mtest_q_data *q_data;
	u8 *p_in, *p_out;
	int x, y, t, w;
	int tile_w, bytes_left;
	int width, height, bytesperline;

	q_data = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);

	width	= q_data->width;
	height	= q_data->height;
	bytesperline	= (q_data->width * q_data->fmt->depth) >> 3;

	p_in = vb2_plane_vaddr(in_vb, 0);
	p_out = vb2_plane_vaddr(out_vb, 0);
	if (!p_in || !p_out) {
		v4l2_err(&dev->v4l2_dev,
			 "Acquiring kernel pointers to buffers failed\n");
		return -EFAULT;
	}

	if (vb2_plane_size(in_vb, 0) > vb2_plane_size(out_vb, 0)) {
		v4l2_err(&dev->v4l2_dev, "Output buffer is too small\n");
		return -EINVAL;
	}

	tile_w = (width * (q_data[V4L2_M2M_DST].fmt->depth >> 3))
		/ MEM2MEM_NUM_TILES;
	bytes_left = bytesperline - tile_w * MEM2MEM_NUM_TILES;
	w = 0;

	memcpy(&out_vb->v4l2_buf.timestamp,
			&in_vb->v4l2_buf.timestamp,
			sizeof(struct timeval));

	switch (ctx->mode) {
	case MEM2MEM_HFLIP | MEM2MEM_VFLIP:
		p_out += bytesperline * height - bytes_left;
		for (y = 0; y < height; ++y) {
			for (t = 0; t < MEM2MEM_NUM_TILES; ++t) {
				if (w & 0x1) {
					for (x = 0; x < tile_w; ++x)
						*--p_out = *p_in++ +
							MEM2MEM_COLOR_STEP;
				} else {
					for (x = 0; x < tile_w; ++x)
						*--p_out = *p_in++ -
							MEM2MEM_COLOR_STEP;
				}
				++w;
			}
			p_in += bytes_left;
			p_out -= bytes_left;
		}
		break;

	case MEM2MEM_HFLIP:
		for (y = 0; y < height; ++y) {
			p_out += MEM2MEM_NUM_TILES * tile_w;
			for (t = 0; t < MEM2MEM_NUM_TILES; ++t) {
				if (w & 0x01) {
					for (x = 0; x < tile_w; ++x)
						*--p_out = *p_in++ +
							MEM2MEM_COLOR_STEP;
				} else {
					for (x = 0; x < tile_w; ++x)
						*--p_out = *p_in++ -
							MEM2MEM_COLOR_STEP;
				}
				++w;
			}
			p_in += bytes_left;
			p_out += bytesperline;
		}
		break;

	case MEM2MEM_VFLIP:
		p_out += bytesperline * (height - 1);
		for (y = 0; y < height; ++y) {
			for (t = 0; t < MEM2MEM_NUM_TILES; ++t) {
				if (w & 0x1) {
					for (x = 0; x < tile_w; ++x)
						*p_out++ = *p_in++ +
							MEM2MEM_COLOR_STEP;
				} else {
					for (x = 0; x < tile_w; ++x)
						*p_out++ = *p_in++ -
							MEM2MEM_COLOR_STEP;
				}
				++w;
			}
			p_in += bytes_left;
			p_out += bytes_left - 2 * bytesperline;
		}
		break;

	default:
		for (y = 0; y < height; ++y) {
			for (t = 0; t < MEM2MEM_NUM_TILES; ++t) {
				if (w & 0x1) {
					for (x = 0; x < tile_w; ++x)
						*p_out++ = *p_in++ +
							MEM2MEM_COLOR_STEP;
				} else {
					for (x = 0; x < tile_w; ++x)
						*p_out++ = *p_in++ -
							MEM2MEM_COLOR_STEP;
				}
				++w;
			}
			p_in += bytes_left;
			p_out += bytes_left;
		}
	}

	return 0;
}

static void schedule_irq(struct m2mtest_dev *dev, int msec_timeout)
{
	dprintk(dev, "Scheduling a simulated irq\n");
	mod_timer(&dev->timer, jiffies + msecs_to_jiffies(msec_timeout));
}

/*
 * mem2mem callbacks
 */

/**
 * job_ready() - check whether an instance is ready to be scheduled to run
 */
static int job_ready(void *priv)
{
	struct m2mtest_ctx *ctx = priv;

	if (v4l2_m2m_num_src_bufs_ready(ctx->m2m_ctx) < ctx->translen
	    || v4l2_m2m_num_dst_bufs_ready(ctx->m2m_ctx) < ctx->translen) {
		dprintk(ctx->dev, "Not enough buffers available\n");
		return 0;
	}

	return 1;
}

static void job_abort(void *priv)
{
	struct m2mtest_ctx *ctx = priv;

	/* Will cancel the transaction in the next interrupt handler */
	ctx->aborting = 1;
}

static void m2mtest_lock(void *priv)
{
	struct m2mtest_ctx *ctx = priv;
	struct m2mtest_dev *dev = ctx->dev;
	mutex_lock(&dev->dev_mutex);
}

static void m2mtest_unlock(void *priv)
{
	struct m2mtest_ctx *ctx = priv;
	struct m2mtest_dev *dev = ctx->dev;
	mutex_unlock(&dev->dev_mutex);
}


/* device_run() - prepares and starts the device
 *
 * This simulates all the immediate preparations required before starting
 * a device. This will be called by the framework when it decides to schedule
 * a particular instance.
 */
static void device_run(void *priv)
{
	struct m2mtest_ctx *ctx = priv;
	struct m2mtest_dev *dev = ctx->dev;
	struct vb2_buffer *src_buf, *dst_buf;

	src_buf = v4l2_m2m_next_src_buf(ctx->m2m_ctx);
	dst_buf = v4l2_m2m_next_dst_buf(ctx->m2m_ctx);

	device_process(ctx, src_buf, dst_buf);

	/* Run a timer, which simulates a hardware irq  */
	schedule_irq(dev, ctx->transtime);
}

static void device_isr(unsigned long priv)
{
	struct m2mtest_dev *m2mtest_dev = (struct m2mtest_dev *)priv;
	struct m2mtest_ctx *curr_ctx;
	struct vb2_buffer *src_vb, *dst_vb;
	unsigned long flags;

	curr_ctx = v4l2_m2m_get_curr_priv(m2mtest_dev->m2m_dev);

	if (NULL == curr_ctx) {
		pr_err("Instance released before the end of transaction\n");
		return;
	}

	src_vb = v4l2_m2m_src_buf_remove(curr_ctx->m2m_ctx);
	dst_vb = v4l2_m2m_dst_buf_remove(curr_ctx->m2m_ctx);

	curr_ctx->num_processed++;

	spin_lock_irqsave(&m2mtest_dev->irqlock, flags);
	v4l2_m2m_buf_done(src_vb, VB2_BUF_STATE_DONE);
	v4l2_m2m_buf_done(dst_vb, VB2_BUF_STATE_DONE);
	spin_unlock_irqrestore(&m2mtest_dev->irqlock, flags);

	if (curr_ctx->num_processed == curr_ctx->translen
	    || curr_ctx->aborting) {
		dprintk(curr_ctx->dev, "Finishing transaction\n");
		curr_ctx->num_processed = 0;
		v4l2_m2m_job_finish(m2mtest_dev->m2m_dev, curr_ctx->m2m_ctx);
	} else {
		device_run(curr_ctx);
	}
}

/*
 * video ioctls
 */
static int vidioc_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	strncpy(cap->driver, MEM2MEM_NAME, sizeof(cap->driver) - 1);
	strncpy(cap->card, MEM2MEM_NAME, sizeof(cap->card) - 1);
	snprintf(cap->bus_info, sizeof(cap->bus_info),
			"platform:%s", MEM2MEM_NAME);
	cap->device_caps = V4L2_CAP_VIDEO_M2M | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int enum_fmt(struct v4l2_fmtdesc *f, u32 type)
{
	int i, num;
	struct m2mtest_fmt *fmt;

	num = 0;

	for (i = 0; i < NUM_FORMATS; ++i) {
		if (formats[i].types & type) {
			/* index-th format of type type found ? */
			if (num == f->index)
				break;
			/* Correct type but haven't reached our index yet,
			 * just increment per-type index */
			++num;
		}
	}

	if (i < NUM_FORMATS) {
		/* Format found */
		fmt = &formats[i];
		strncpy(f->description, fmt->name, sizeof(f->description) - 1);
		f->pixelformat = fmt->fourcc;
		return 0;
	}

	/* Format not found */
	return -EINVAL;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	return enum_fmt(f, MEM2MEM_CAPTURE);
}

static int vidioc_enum_fmt_vid_out(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	return enum_fmt(f, MEM2MEM_OUTPUT);
}

static int vidioc_g_fmt(struct m2mtest_ctx *ctx, struct v4l2_format *f)
{
	struct vb2_queue *vq;
	struct m2mtest_q_data *q_data;

	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	q_data = get_q_data(ctx, f->type);

	f->fmt.pix.width	= q_data->width;
	f->fmt.pix.height	= q_data->height;
	f->fmt.pix.field	= V4L2_FIELD_NONE;
	f->fmt.pix.pixelformat	= q_data->fmt->fourcc;
	f->fmt.pix.bytesperline	= (q_data->width * q_data->fmt->depth) >> 3;
	f->fmt.pix.sizeimage	= q_data->sizeimage;
	f->fmt.pix.colorspace	= ctx->colorspace;

	return 0;
}

static int vidioc_g_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *f)
{
	return vidioc_g_fmt(file2ctx(file), f);
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	return vidioc_g_fmt(file2ctx(file), f);
}

static int vidioc_try_fmt(struct v4l2_format *f, struct m2mtest_fmt *fmt)
{
	enum v4l2_field field;

	field = f->fmt.pix.field;

	if (field == V4L2_FIELD_ANY)
		field = V4L2_FIELD_NONE;
	else if (V4L2_FIELD_NONE != field)
		return -EINVAL;

	/* V4L2 specification suggests the driver corrects the format struct
	 * if any of the dimensions is unsupported */
	f->fmt.pix.field = field;

	if (f->fmt.pix.height < MIN_H)
		f->fmt.pix.height = MIN_H;
	else if (f->fmt.pix.height > MAX_H)
		f->fmt.pix.height = MAX_H;

	if (f->fmt.pix.width < MIN_W)
		f->fmt.pix.width = MIN_W;
	else if (f->fmt.pix.width > MAX_W)
		f->fmt.pix.width = MAX_W;

	f->fmt.pix.width &= ~DIM_ALIGN_MASK;
	f->fmt.pix.bytesperline = (f->fmt.pix.width * fmt->depth) >> 3;
	f->fmt.pix.sizeimage = f->fmt.pix.height * f->fmt.pix.bytesperline;

	return 0;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct m2mtest_fmt *fmt;
	struct m2mtest_ctx *ctx = file2ctx(file);

	fmt = find_format(f);
	if (!fmt || !(fmt->types & MEM2MEM_CAPTURE)) {
		v4l2_err(&ctx->dev->v4l2_dev,
			 "Fourcc format (0x%08x) invalid.\n",
			 f->fmt.pix.pixelformat);
		return -EINVAL;
	}
	f->fmt.pix.colorspace = ctx->colorspace;

	return vidioc_try_fmt(f, fmt);
}

static int vidioc_try_fmt_vid_out(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct m2mtest_fmt *fmt;
	struct m2mtest_ctx *ctx = file2ctx(file);

	fmt = find_format(f);
	if (!fmt || !(fmt->types & MEM2MEM_OUTPUT)) {
		v4l2_err(&ctx->dev->v4l2_dev,
			 "Fourcc format (0x%08x) invalid.\n",
			 f->fmt.pix.pixelformat);
		return -EINVAL;
	}
	if (!f->fmt.pix.colorspace)
		f->fmt.pix.colorspace = V4L2_COLORSPACE_REC709;

	return vidioc_try_fmt(f, fmt);
}

static int vidioc_s_fmt(struct m2mtest_ctx *ctx, struct v4l2_format *f)
{
	struct m2mtest_q_data *q_data;
	struct vb2_queue *vq;

	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	q_data = get_q_data(ctx, f->type);
	if (!q_data)
		return -EINVAL;

	if (vb2_is_busy(vq)) {
		v4l2_err(&ctx->dev->v4l2_dev, "%s queue busy\n", __func__);
		return -EBUSY;
	}

	q_data->fmt		= find_format(f);
	q_data->width		= f->fmt.pix.width;
	q_data->height		= f->fmt.pix.height;
	q_data->sizeimage	= q_data->width * q_data->height
				* q_data->fmt->depth >> 3;

	dprintk(ctx->dev,
		"Setting format for type %d, wxh: %dx%d, fmt: %d\n",
		f->type, q_data->width, q_data->height, q_data->fmt->fourcc);

	return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	int ret;

	ret = vidioc_try_fmt_vid_cap(file, priv, f);
	if (ret)
		return ret;

	return vidioc_s_fmt(file2ctx(file), f);
}

static int vidioc_s_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct m2mtest_ctx *ctx = file2ctx(file);
	int ret;

	ret = vidioc_try_fmt_vid_out(file, priv, f);
	if (ret)
		return ret;

	ret = vidioc_s_fmt(file2ctx(file), f);
	if (!ret)
		ctx->colorspace = f->fmt.pix.colorspace;
	return ret;
}

static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *reqbufs)
{
	struct m2mtest_ctx *ctx = file2ctx(file);

	return v4l2_m2m_reqbufs(file, ctx->m2m_ctx, reqbufs);
}

static int vidioc_querybuf(struct file *file, void *priv,
			   struct v4l2_buffer *buf)
{
	struct m2mtest_ctx *ctx = file2ctx(file);

	return v4l2_m2m_querybuf(file, ctx->m2m_ctx, buf);
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct m2mtest_ctx *ctx = file2ctx(file);

	return v4l2_m2m_qbuf(file, ctx->m2m_ctx, buf);
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct m2mtest_ctx *ctx = file2ctx(file);

	return v4l2_m2m_dqbuf(file, ctx->m2m_ctx, buf);
}

static int vidioc_streamon(struct file *file, void *priv,
			   enum v4l2_buf_type type)
{
	struct m2mtest_ctx *ctx = file2ctx(file);

	return v4l2_m2m_streamon(file, ctx->m2m_ctx, type);
}

static int vidioc_streamoff(struct file *file, void *priv,
			    enum v4l2_buf_type type)
{
	struct m2mtest_ctx *ctx = file2ctx(file);

	return v4l2_m2m_streamoff(file, ctx->m2m_ctx, type);
}

static int m2mtest_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct m2mtest_ctx *ctx =
		container_of(ctrl->handler, struct m2mtest_ctx, hdl);

	switch (ctrl->id) {
	case V4L2_CID_HFLIP:
		if (ctrl->val)
			ctx->mode |= MEM2MEM_HFLIP;
		else
			ctx->mode &= ~MEM2MEM_HFLIP;
		break;

	case V4L2_CID_VFLIP:
		if (ctrl->val)
			ctx->mode |= MEM2MEM_VFLIP;
		else
			ctx->mode &= ~MEM2MEM_VFLIP;
		break;

	case V4L2_CID_TRANS_TIME_MSEC:
		ctx->transtime = ctrl->val;
		break;

	case V4L2_CID_TRANS_NUM_BUFS:
		ctx->translen = ctrl->val;
		break;

	default:
		v4l2_err(&ctx->dev->v4l2_dev, "Invalid control\n");
		return -EINVAL;
	}

	return 0;
}

static const struct v4l2_ctrl_ops m2mtest_ctrl_ops = {
	.s_ctrl = m2mtest_s_ctrl,
};


static const struct v4l2_ioctl_ops m2mtest_ioctl_ops = {
	.vidioc_querycap	= vidioc_querycap,

	.vidioc_enum_fmt_vid_cap = vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap	= vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap	= vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap	= vidioc_s_fmt_vid_cap,

	.vidioc_enum_fmt_vid_out = vidioc_enum_fmt_vid_out,
	.vidioc_g_fmt_vid_out	= vidioc_g_fmt_vid_out,
	.vidioc_try_fmt_vid_out	= vidioc_try_fmt_vid_out,
	.vidioc_s_fmt_vid_out	= vidioc_s_fmt_vid_out,

	.vidioc_reqbufs		= vidioc_reqbufs,
	.vidioc_querybuf	= vidioc_querybuf,

	.vidioc_qbuf		= vidioc_qbuf,
	.vidioc_dqbuf		= vidioc_dqbuf,

	.vidioc_streamon	= vidioc_streamon,
	.vidioc_streamoff	= vidioc_streamoff,
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};


/*
 * Queue operations
 */

static int m2mtest_queue_setup(struct vb2_queue *vq,
				const struct v4l2_format *fmt,
				unsigned int *nbuffers, unsigned int *nplanes,
				unsigned int sizes[], void *alloc_ctxs[])
{
	struct m2mtest_ctx *ctx = vb2_get_drv_priv(vq);
	struct m2mtest_q_data *q_data;
	unsigned int size, count = *nbuffers;

	q_data = get_q_data(ctx, vq->type);

	size = q_data->width * q_data->height * q_data->fmt->depth >> 3;

	while (size * count > MEM2MEM_VID_MEM_LIMIT)
		(count)--;

	*nplanes = 1;
	*nbuffers = count;
	sizes[0] = size;

	/*
	 * videobuf2-vmalloc allocator is context-less so no need to set
	 * alloc_ctxs array.
	 */

	dprintk(ctx->dev, "get %d buffer(s) of size %d each.\n", count, size);

	return 0;
}

static int m2mtest_buf_prepare(struct vb2_buffer *vb)
{
	struct m2mtest_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct m2mtest_q_data *q_data;

	dprintk(ctx->dev, "type: %d\n", vb->vb2_queue->type);

	q_data = get_q_data(ctx, vb->vb2_queue->type);

	if (vb2_plane_size(vb, 0) < q_data->sizeimage) {
		dprintk(ctx->dev, "%s data will not fit into plane (%lu < %lu)\n",
				__func__, vb2_plane_size(vb, 0), (long)q_data->sizeimage);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, q_data->sizeimage);

	return 0;
}

static void m2mtest_buf_queue(struct vb2_buffer *vb)
{
	struct m2mtest_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	v4l2_m2m_buf_queue(ctx->m2m_ctx, vb);
}

static void m2mtest_wait_prepare(struct vb2_queue *q)
{
	struct m2mtest_ctx *ctx = vb2_get_drv_priv(q);
	m2mtest_unlock(ctx);
}

static void m2mtest_wait_finish(struct vb2_queue *q)
{
	struct m2mtest_ctx *ctx = vb2_get_drv_priv(q);
	m2mtest_lock(ctx);
}

static struct vb2_ops m2mtest_qops = {
	.queue_setup	 = m2mtest_queue_setup,
	.buf_prepare	 = m2mtest_buf_prepare,
	.buf_queue	 = m2mtest_buf_queue,
	.wait_prepare	 = m2mtest_wait_prepare,
	.wait_finish	 = m2mtest_wait_finish,
};

static int queue_init(void *priv, struct vb2_queue *src_vq, struct vb2_queue *dst_vq)
{
	struct m2mtest_ctx *ctx = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->ops = &m2mtest_qops;
	src_vq->mem_ops = &vb2_vmalloc_memops;
	src_vq->timestamp_type = V4L2_BUF_FLAG_TIMESTAMP_COPY;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->ops = &m2mtest_qops;
	dst_vq->mem_ops = &vb2_vmalloc_memops;
	dst_vq->timestamp_type = V4L2_BUF_FLAG_TIMESTAMP_COPY;

	return vb2_queue_init(dst_vq);
}

static const struct v4l2_ctrl_config m2mtest_ctrl_trans_time_msec = {
	.ops = &m2mtest_ctrl_ops,
	.id = V4L2_CID_TRANS_TIME_MSEC,
	.name = "Transaction Time (msec)",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.def = 1001,
	.min = 1,
	.max = 10001,
	.step = 100,
};

static const struct v4l2_ctrl_config m2mtest_ctrl_trans_num_bufs = {
	.ops = &m2mtest_ctrl_ops,
	.id = V4L2_CID_TRANS_NUM_BUFS,
	.name = "Buffers Per Transaction",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.def = 1,
	.min = 1,
	.max = MEM2MEM_DEF_NUM_BUFS,
	.step = 1,
};

/*
 * File operations
 */
static int m2mtest_open(struct file *file)
{
	struct m2mtest_dev *dev = video_drvdata(file);
	struct m2mtest_ctx *ctx = NULL;
	struct v4l2_ctrl_handler *hdl;
	int rc = 0;

	if (mutex_lock_interruptible(&dev->dev_mutex))
		return -ERESTARTSYS;
	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		rc = -ENOMEM;
		goto open_unlock;
	}

	v4l2_fh_init(&ctx->fh, video_devdata(file));
	file->private_data = &ctx->fh;
	ctx->dev = dev;
	hdl = &ctx->hdl;
	v4l2_ctrl_handler_init(hdl, 4);
	v4l2_ctrl_new_std(hdl, &m2mtest_ctrl_ops, V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(hdl, &m2mtest_ctrl_ops, V4L2_CID_VFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_custom(hdl, &m2mtest_ctrl_trans_time_msec, NULL);
	v4l2_ctrl_new_custom(hdl, &m2mtest_ctrl_trans_num_bufs, NULL);
	if (hdl->error) {
		rc = hdl->error;
		v4l2_ctrl_handler_free(hdl);
		goto open_unlock;
	}
	ctx->fh.ctrl_handler = hdl;
	v4l2_ctrl_handler_setup(hdl);

	ctx->q_data[V4L2_M2M_SRC].fmt = &formats[0];
	ctx->q_data[V4L2_M2M_SRC].width = 640;
	ctx->q_data[V4L2_M2M_SRC].height = 480;
	ctx->q_data[V4L2_M2M_SRC].sizeimage =
		ctx->q_data[V4L2_M2M_SRC].width *
		ctx->q_data[V4L2_M2M_SRC].height *
		(ctx->q_data[V4L2_M2M_SRC].fmt->depth >> 3);
	ctx->q_data[V4L2_M2M_DST] = ctx->q_data[V4L2_M2M_SRC];
	ctx->colorspace = V4L2_COLORSPACE_REC709;

	ctx->m2m_ctx = v4l2_m2m_ctx_init(dev->m2m_dev, ctx, &queue_init);

	if (IS_ERR(ctx->m2m_ctx)) {
		rc = PTR_ERR(ctx->m2m_ctx);

		v4l2_ctrl_handler_free(hdl);
		kfree(ctx);
		goto open_unlock;
	}

	v4l2_fh_add(&ctx->fh);
	atomic_inc(&dev->num_inst);

	dprintk(dev, "Created instance %p, m2m_ctx: %p\n", ctx, ctx->m2m_ctx);

open_unlock:
	mutex_unlock(&dev->dev_mutex);
	return rc;
}

static int m2mtest_release(struct file *file)
{
	struct m2mtest_dev *dev = video_drvdata(file);
	struct m2mtest_ctx *ctx = file2ctx(file);

	dprintk(dev, "Releasing instance %p\n", ctx);

	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	v4l2_ctrl_handler_free(&ctx->hdl);
	mutex_lock(&dev->dev_mutex);
	v4l2_m2m_ctx_release(ctx->m2m_ctx);
	mutex_unlock(&dev->dev_mutex);
	kfree(ctx);

	atomic_dec(&dev->num_inst);

	return 0;
}

static unsigned int m2mtest_poll(struct file *file,
				 struct poll_table_struct *wait)
{
	struct m2mtest_ctx *ctx = file2ctx(file);

	return v4l2_m2m_poll(file, ctx->m2m_ctx, wait);
}

static int m2mtest_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct m2mtest_dev *dev = video_drvdata(file);
	struct m2mtest_ctx *ctx = file2ctx(file);
	int res;

	if (mutex_lock_interruptible(&dev->dev_mutex))
		return -ERESTARTSYS;
	res = v4l2_m2m_mmap(file, ctx->m2m_ctx, vma);
	mutex_unlock(&dev->dev_mutex);
	return res;
}

static const struct v4l2_file_operations m2mtest_fops = {
	.owner		= THIS_MODULE,
	.open		= m2mtest_open,
	.release	= m2mtest_release,
	.poll		= m2mtest_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= m2mtest_mmap,
};

static struct video_device m2mtest_videodev = {
	.name		= MEM2MEM_NAME,
	.vfl_dir	= VFL_DIR_M2M,
	.fops		= &m2mtest_fops,
	.ioctl_ops	= &m2mtest_ioctl_ops,
	.minor		= -1,
	.release	= video_device_release,
};

static struct v4l2_m2m_ops m2m_ops = {
	.device_run	= device_run,
	.job_ready	= job_ready,
	.job_abort	= job_abort,
	.lock		= m2mtest_lock,
	.unlock		= m2mtest_unlock,
};

static int m2mtest_probe(struct platform_device *pdev)
{
	struct m2mtest_dev *dev;
	struct video_device *vfd;
	int ret;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	spin_lock_init(&dev->irqlock);

	ret = v4l2_device_register(&pdev->dev, &dev->v4l2_dev);
	if (ret)
		return ret;

	atomic_set(&dev->num_inst, 0);
	mutex_init(&dev->dev_mutex);

	vfd = video_device_alloc();
	if (!vfd) {
		v4l2_err(&dev->v4l2_dev, "Failed to allocate video device\n");
		ret = -ENOMEM;
		goto unreg_dev;
	}

	*vfd = m2mtest_videodev;
	vfd->lock = &dev->dev_mutex;

	ret = video_register_device(vfd, VFL_TYPE_GRABBER, 0);
	if (ret) {
		v4l2_err(&dev->v4l2_dev, "Failed to register video device\n");
		goto rel_vdev;
	}

	video_set_drvdata(vfd, dev);
	snprintf(vfd->name, sizeof(vfd->name), "%s", m2mtest_videodev.name);
	dev->vfd = vfd;
	v4l2_info(&dev->v4l2_dev, MEM2MEM_TEST_MODULE_NAME
			"Device registered as /dev/video%d\n", vfd->num);

	setup_timer(&dev->timer, device_isr, (long)dev);
	platform_set_drvdata(pdev, dev);

	dev->m2m_dev = v4l2_m2m_init(&m2m_ops);
	if (IS_ERR(dev->m2m_dev)) {
		v4l2_err(&dev->v4l2_dev, "Failed to init mem2mem device\n");
		ret = PTR_ERR(dev->m2m_dev);
		goto err_m2m;
	}

	return 0;

err_m2m:
	v4l2_m2m_release(dev->m2m_dev);
	video_unregister_device(dev->vfd);
rel_vdev:
	video_device_release(vfd);
unreg_dev:
	v4l2_device_unregister(&dev->v4l2_dev);

	return ret;
}

static int m2mtest_remove(struct platform_device *pdev)
{
	struct m2mtest_dev *dev =
		(struct m2mtest_dev *)platform_get_drvdata(pdev);

	v4l2_info(&dev->v4l2_dev, "Removing " MEM2MEM_TEST_MODULE_NAME);
	v4l2_m2m_release(dev->m2m_dev);
	del_timer_sync(&dev->timer);
	video_unregister_device(dev->vfd);
	v4l2_device_unregister(&dev->v4l2_dev);

	return 0;
}

static struct platform_driver m2mtest_pdrv = {
	.probe		= m2mtest_probe,
	.remove		= m2mtest_remove,
	.driver		= {
		.name	= MEM2MEM_NAME,
		.owner	= THIS_MODULE,
	},
};

static void __exit m2mtest_exit(void)
{
	platform_driver_unregister(&m2mtest_pdrv);
	platform_device_unregister(&m2mtest_pdev);
}

static int __init m2mtest_init(void)
{
	int ret;

	ret = platform_device_register(&m2mtest_pdev);
	if (ret)
		return ret;

	ret = platform_driver_register(&m2mtest_pdrv);
	if (ret)
		platform_device_unregister(&m2mtest_pdev);

	return 0;
}

module_init(m2mtest_init);
module_exit(m2mtest_exit);

