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
 * Pawel Osciak, <p.osciak@samsung.com>
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
#include <linux/version.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <linux/platform_device.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf-vmalloc.h>

#define MEM2MEM_TEST_MODULE_NAME "mem2mem-testdev"

MODULE_DESCRIPTION("Virtual device for mem2mem framework testing");
MODULE_AUTHOR("Pawel Osciak, <p.osciak@samsung.com>");
MODULE_LICENSE("GPL");


#define MIN_W 32
#define MIN_H 32
#define MAX_W 640
#define MAX_H 480
#define DIM_ALIGN_MASK 0x08 /* 8-alignment for dimensions */

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

#define dprintk(dev, fmt, arg...) \
	v4l2_dbg(1, 1, &dev->v4l2_dev, "%s: " fmt, __func__, ## arg)


void m2mtest_dev_release(struct device *dev)
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

/* Source and destination queue data */
static struct m2mtest_q_data q_data[2];

static struct m2mtest_q_data *get_q_data(enum v4l2_buf_type type)
{
	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		return &q_data[V4L2_M2M_SRC];
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		return &q_data[V4L2_M2M_DST];
	default:
		BUG();
	}
	return NULL;
}

#define V4L2_CID_TRANS_TIME_MSEC	V4L2_CID_PRIVATE_BASE
#define V4L2_CID_TRANS_NUM_BUFS		(V4L2_CID_PRIVATE_BASE + 1)

static struct v4l2_queryctrl m2mtest_ctrls[] = {
	{
		.id		= V4L2_CID_TRANS_TIME_MSEC,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Transaction time (msec)",
		.minimum	= 1,
		.maximum	= 10000,
		.step		= 100,
		.default_value	= 1000,
		.flags		= 0,
	}, {
		.id		= V4L2_CID_TRANS_NUM_BUFS,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Buffers per transaction",
		.minimum	= 1,
		.maximum	= MEM2MEM_DEF_NUM_BUFS,
		.step		= 1,
		.default_value	= 1,
		.flags		= 0,
	},
};

#define NUM_FORMATS ARRAY_SIZE(formats)

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
	struct m2mtest_dev	*dev;

	/* Processed buffers in this transaction */
	u8			num_processed;

	/* Transaction length (i.e. how many buffers per transaction) */
	u32			translen;
	/* Transaction time (i.e. simulated processing time) in milliseconds */
	u32			transtime;

	/* Abort requested by m2m */
	int			aborting;

	struct v4l2_m2m_ctx	*m2m_ctx;
};

struct m2mtest_buffer {
	/* vb must be first! */
	struct videobuf_buffer	vb;
};

static struct v4l2_queryctrl *get_ctrl(int id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(m2mtest_ctrls); ++i) {
		if (id == m2mtest_ctrls[i].id)
			return &m2mtest_ctrls[i];
	}

	return NULL;
}

static int device_process(struct m2mtest_ctx *ctx,
			  struct m2mtest_buffer *in_buf,
			  struct m2mtest_buffer *out_buf)
{
	struct m2mtest_dev *dev = ctx->dev;
	u8 *p_in, *p_out;
	int x, y, t, w;
	int tile_w, bytes_left;
	struct videobuf_queue *src_q;
	struct videobuf_queue *dst_q;

	src_q = v4l2_m2m_get_src_vq(ctx->m2m_ctx);
	dst_q = v4l2_m2m_get_dst_vq(ctx->m2m_ctx);
	p_in = videobuf_queue_to_vaddr(src_q, &in_buf->vb);
	p_out = videobuf_queue_to_vaddr(dst_q, &out_buf->vb);
	if (!p_in || !p_out) {
		v4l2_err(&dev->v4l2_dev,
			 "Acquiring kernel pointers to buffers failed\n");
		return -EFAULT;
	}

	if (in_buf->vb.size > out_buf->vb.size) {
		v4l2_err(&dev->v4l2_dev, "Output buffer is too small\n");
		return -EINVAL;
	}

	tile_w = (in_buf->vb.width * (q_data[V4L2_M2M_DST].fmt->depth >> 3))
		/ MEM2MEM_NUM_TILES;
	bytes_left = in_buf->vb.bytesperline - tile_w * MEM2MEM_NUM_TILES;
	w = 0;

	for (y = 0; y < in_buf->vb.height; ++y) {
		for (t = 0; t < MEM2MEM_NUM_TILES; ++t) {
			if (w & 0x1) {
				for (x = 0; x < tile_w; ++x)
					*p_out++ = *p_in++ + MEM2MEM_COLOR_STEP;
			} else {
				for (x = 0; x < tile_w; ++x)
					*p_out++ = *p_in++ - MEM2MEM_COLOR_STEP;
			}
			++w;
		}
		p_in += bytes_left;
		p_out += bytes_left;
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
	struct m2mtest_buffer *src_buf, *dst_buf;

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
	struct m2mtest_buffer *src_buf, *dst_buf;
	unsigned long flags;

	curr_ctx = v4l2_m2m_get_curr_priv(m2mtest_dev->m2m_dev);

	if (NULL == curr_ctx) {
		printk(KERN_ERR
			"Instance released before the end of transaction\n");
		return;
	}

	src_buf = v4l2_m2m_src_buf_remove(curr_ctx->m2m_ctx);
	dst_buf = v4l2_m2m_dst_buf_remove(curr_ctx->m2m_ctx);
	curr_ctx->num_processed++;

	if (curr_ctx->num_processed == curr_ctx->translen
	    || curr_ctx->aborting) {
		dprintk(curr_ctx->dev, "Finishing transaction\n");
		curr_ctx->num_processed = 0;
		spin_lock_irqsave(&m2mtest_dev->irqlock, flags);
		src_buf->vb.state = dst_buf->vb.state = VIDEOBUF_DONE;
		wake_up(&src_buf->vb.done);
		wake_up(&dst_buf->vb.done);
		spin_unlock_irqrestore(&m2mtest_dev->irqlock, flags);
		v4l2_m2m_job_finish(m2mtest_dev->m2m_dev, curr_ctx->m2m_ctx);
	} else {
		spin_lock_irqsave(&m2mtest_dev->irqlock, flags);
		src_buf->vb.state = dst_buf->vb.state = VIDEOBUF_DONE;
		wake_up(&src_buf->vb.done);
		wake_up(&dst_buf->vb.done);
		spin_unlock_irqrestore(&m2mtest_dev->irqlock, flags);
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
	cap->bus_info[0] = 0;
	cap->version = KERNEL_VERSION(0, 1, 0);
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_OUTPUT
			  | V4L2_CAP_STREAMING;

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
	struct videobuf_queue *vq;
	struct m2mtest_q_data *q_data;

	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	q_data = get_q_data(f->type);

	f->fmt.pix.width	= q_data->width;
	f->fmt.pix.height	= q_data->height;
	f->fmt.pix.field	= vq->field;
	f->fmt.pix.pixelformat	= q_data->fmt->fourcc;
	f->fmt.pix.bytesperline	= (q_data->width * q_data->fmt->depth) >> 3;
	f->fmt.pix.sizeimage	= q_data->sizeimage;

	return 0;
}

static int vidioc_g_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *f)
{
	return vidioc_g_fmt(priv, f);
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	return vidioc_g_fmt(priv, f);
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
	struct m2mtest_ctx *ctx = priv;

	fmt = find_format(f);
	if (!fmt || !(fmt->types & MEM2MEM_CAPTURE)) {
		v4l2_err(&ctx->dev->v4l2_dev,
			 "Fourcc format (0x%08x) invalid.\n",
			 f->fmt.pix.pixelformat);
		return -EINVAL;
	}

	return vidioc_try_fmt(f, fmt);
}

static int vidioc_try_fmt_vid_out(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct m2mtest_fmt *fmt;
	struct m2mtest_ctx *ctx = priv;

	fmt = find_format(f);
	if (!fmt || !(fmt->types & MEM2MEM_OUTPUT)) {
		v4l2_err(&ctx->dev->v4l2_dev,
			 "Fourcc format (0x%08x) invalid.\n",
			 f->fmt.pix.pixelformat);
		return -EINVAL;
	}

	return vidioc_try_fmt(f, fmt);
}

static int vidioc_s_fmt(struct m2mtest_ctx *ctx, struct v4l2_format *f)
{
	struct m2mtest_q_data *q_data;
	struct videobuf_queue *vq;
	int ret = 0;

	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	q_data = get_q_data(f->type);
	if (!q_data)
		return -EINVAL;

	mutex_lock(&vq->vb_lock);

	if (videobuf_queue_is_busy(vq)) {
		v4l2_err(&ctx->dev->v4l2_dev, "%s queue busy\n", __func__);
		ret = -EBUSY;
		goto out;
	}

	q_data->fmt		= find_format(f);
	q_data->width		= f->fmt.pix.width;
	q_data->height		= f->fmt.pix.height;
	q_data->sizeimage	= q_data->width * q_data->height
				* q_data->fmt->depth >> 3;
	vq->field		= f->fmt.pix.field;

	dprintk(ctx->dev,
		"Setting format for type %d, wxh: %dx%d, fmt: %d\n",
		f->type, q_data->width, q_data->height, q_data->fmt->fourcc);

out:
	mutex_unlock(&vq->vb_lock);
	return ret;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	int ret;

	ret = vidioc_try_fmt_vid_cap(file, priv, f);
	if (ret)
		return ret;

	return vidioc_s_fmt(priv, f);
}

static int vidioc_s_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *f)
{
	int ret;

	ret = vidioc_try_fmt_vid_out(file, priv, f);
	if (ret)
		return ret;

	return vidioc_s_fmt(priv, f);
}

static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *reqbufs)
{
	struct m2mtest_ctx *ctx = priv;

	return v4l2_m2m_reqbufs(file, ctx->m2m_ctx, reqbufs);
}

static int vidioc_querybuf(struct file *file, void *priv,
			   struct v4l2_buffer *buf)
{
	struct m2mtest_ctx *ctx = priv;

	return v4l2_m2m_querybuf(file, ctx->m2m_ctx, buf);
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct m2mtest_ctx *ctx = priv;

	return v4l2_m2m_qbuf(file, ctx->m2m_ctx, buf);
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct m2mtest_ctx *ctx = priv;

	return v4l2_m2m_dqbuf(file, ctx->m2m_ctx, buf);
}

static int vidioc_streamon(struct file *file, void *priv,
			   enum v4l2_buf_type type)
{
	struct m2mtest_ctx *ctx = priv;

	return v4l2_m2m_streamon(file, ctx->m2m_ctx, type);
}

static int vidioc_streamoff(struct file *file, void *priv,
			    enum v4l2_buf_type type)
{
	struct m2mtest_ctx *ctx = priv;

	return v4l2_m2m_streamoff(file, ctx->m2m_ctx, type);
}

static int vidioc_queryctrl(struct file *file, void *priv,
			    struct v4l2_queryctrl *qc)
{
	struct v4l2_queryctrl *c;

	c = get_ctrl(qc->id);
	if (!c)
		return -EINVAL;

	*qc = *c;
	return 0;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct m2mtest_ctx *ctx = priv;

	switch (ctrl->id) {
	case V4L2_CID_TRANS_TIME_MSEC:
		ctrl->value = ctx->transtime;
		break;

	case V4L2_CID_TRANS_NUM_BUFS:
		ctrl->value = ctx->translen;
		break;

	default:
		v4l2_err(&ctx->dev->v4l2_dev, "Invalid control\n");
		return -EINVAL;
	}

	return 0;
}

static int check_ctrl_val(struct m2mtest_ctx *ctx, struct v4l2_control *ctrl)
{
	struct v4l2_queryctrl *c;

	c = get_ctrl(ctrl->id);
	if (!c)
		return -EINVAL;

	if (ctrl->value < c->minimum || ctrl->value > c->maximum) {
		v4l2_err(&ctx->dev->v4l2_dev, "Value out of range\n");
		return -ERANGE;
	}

	return 0;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct m2mtest_ctx *ctx = priv;
	int ret = 0;

	ret = check_ctrl_val(ctx, ctrl);
	if (ret != 0)
		return ret;

	switch (ctrl->id) {
	case V4L2_CID_TRANS_TIME_MSEC:
		ctx->transtime = ctrl->value;
		break;

	case V4L2_CID_TRANS_NUM_BUFS:
		ctx->translen = ctrl->value;
		break;

	default:
		v4l2_err(&ctx->dev->v4l2_dev, "Invalid control\n");
		return -EINVAL;
	}

	return 0;
}


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

	.vidioc_queryctrl	= vidioc_queryctrl,
	.vidioc_g_ctrl		= vidioc_g_ctrl,
	.vidioc_s_ctrl		= vidioc_s_ctrl,
};


/*
 * Queue operations
 */

static void m2mtest_buf_release(struct videobuf_queue *vq,
				struct videobuf_buffer *vb)
{
	struct m2mtest_ctx *ctx = vq->priv_data;

	dprintk(ctx->dev, "type: %d, index: %d, state: %d\n",
		vq->type, vb->i, vb->state);

	videobuf_vmalloc_free(vb);
	vb->state = VIDEOBUF_NEEDS_INIT;
}

static int m2mtest_buf_setup(struct videobuf_queue *vq, unsigned int *count,
			  unsigned int *size)
{
	struct m2mtest_ctx *ctx = vq->priv_data;
	struct m2mtest_q_data *q_data;

	q_data = get_q_data(vq->type);

	*size = q_data->width * q_data->height * q_data->fmt->depth >> 3;
	dprintk(ctx->dev, "size:%d, w/h %d/%d, depth: %d\n",
		*size, q_data->width, q_data->height, q_data->fmt->depth);

	if (0 == *count)
		*count = MEM2MEM_DEF_NUM_BUFS;

	while (*size * *count > MEM2MEM_VID_MEM_LIMIT)
		(*count)--;

	v4l2_info(&ctx->dev->v4l2_dev,
		  "%d buffers of size %d set up.\n", *count, *size);

	return 0;
}

static int m2mtest_buf_prepare(struct videobuf_queue *vq,
			       struct videobuf_buffer *vb,
			       enum v4l2_field field)
{
	struct m2mtest_ctx *ctx = vq->priv_data;
	struct m2mtest_q_data *q_data;
	int ret;

	dprintk(ctx->dev, "type: %d, index: %d, state: %d\n",
		vq->type, vb->i, vb->state);

	q_data = get_q_data(vq->type);

	if (vb->baddr) {
		/* User-provided buffer */
		if (vb->bsize < q_data->sizeimage) {
			/* Buffer too small to fit a frame */
			v4l2_err(&ctx->dev->v4l2_dev,
				 "User-provided buffer too small\n");
			return -EINVAL;
		}
	} else if (vb->state != VIDEOBUF_NEEDS_INIT
			&& vb->bsize < q_data->sizeimage) {
		/* We provide the buffer, but it's already been initialized
		 * and is too small */
		return -EINVAL;
	}

	vb->width	= q_data->width;
	vb->height	= q_data->height;
	vb->bytesperline = (q_data->width * q_data->fmt->depth) >> 3;
	vb->size	= q_data->sizeimage;
	vb->field	= field;

	if (VIDEOBUF_NEEDS_INIT == vb->state) {
		ret = videobuf_iolock(vq, vb, NULL);
		if (ret) {
			v4l2_err(&ctx->dev->v4l2_dev,
				 "Iolock failed\n");
			goto fail;
		}
	}

	vb->state = VIDEOBUF_PREPARED;

	return 0;
fail:
	m2mtest_buf_release(vq, vb);
	return ret;
}

static void m2mtest_buf_queue(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
	struct m2mtest_ctx *ctx = vq->priv_data;

	v4l2_m2m_buf_queue(ctx->m2m_ctx, vq, vb);
}

static struct videobuf_queue_ops m2mtest_qops = {
	.buf_setup	= m2mtest_buf_setup,
	.buf_prepare	= m2mtest_buf_prepare,
	.buf_queue	= m2mtest_buf_queue,
	.buf_release	= m2mtest_buf_release,
};

static void queue_init(void *priv, struct videobuf_queue *vq,
		       enum v4l2_buf_type type)
{
	struct m2mtest_ctx *ctx = priv;

	videobuf_queue_vmalloc_init(vq, &m2mtest_qops, ctx->dev->v4l2_dev.dev,
				    &ctx->dev->irqlock, type, V4L2_FIELD_NONE,
				    sizeof(struct m2mtest_buffer), priv);
}


/*
 * File operations
 */
static int m2mtest_open(struct file *file)
{
	struct m2mtest_dev *dev = video_drvdata(file);
	struct m2mtest_ctx *ctx = NULL;

	ctx = kzalloc(sizeof *ctx, GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	file->private_data = ctx;
	ctx->dev = dev;
	ctx->translen = MEM2MEM_DEF_TRANSLEN;
	ctx->transtime = MEM2MEM_DEF_TRANSTIME;
	ctx->num_processed = 0;

	ctx->m2m_ctx = v4l2_m2m_ctx_init(ctx, dev->m2m_dev, queue_init);
	if (IS_ERR(ctx->m2m_ctx)) {
		int ret = PTR_ERR(ctx->m2m_ctx);

		kfree(ctx);
		return ret;
	}

	atomic_inc(&dev->num_inst);

	dprintk(dev, "Created instance %p, m2m_ctx: %p\n", ctx, ctx->m2m_ctx);

	return 0;
}

static int m2mtest_release(struct file *file)
{
	struct m2mtest_dev *dev = video_drvdata(file);
	struct m2mtest_ctx *ctx = file->private_data;

	dprintk(dev, "Releasing instance %p\n", ctx);

	v4l2_m2m_ctx_release(ctx->m2m_ctx);
	kfree(ctx);

	atomic_dec(&dev->num_inst);

	return 0;
}

static unsigned int m2mtest_poll(struct file *file,
				 struct poll_table_struct *wait)
{
	struct m2mtest_ctx *ctx = file->private_data;

	return v4l2_m2m_poll(file, ctx->m2m_ctx, wait);
}

static int m2mtest_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct m2mtest_ctx *ctx = file->private_data;

	return v4l2_m2m_mmap(file, ctx->m2m_ctx, vma);
}

static const struct v4l2_file_operations m2mtest_fops = {
	.owner		= THIS_MODULE,
	.open		= m2mtest_open,
	.release	= m2mtest_release,
	.poll		= m2mtest_poll,
	.ioctl		= video_ioctl2,
	.mmap		= m2mtest_mmap,
};

static struct video_device m2mtest_videodev = {
	.name		= MEM2MEM_NAME,
	.fops		= &m2mtest_fops,
	.ioctl_ops	= &m2mtest_ioctl_ops,
	.minor		= -1,
	.release	= video_device_release,
};

static struct v4l2_m2m_ops m2m_ops = {
	.device_run	= device_run,
	.job_ready	= job_ready,
	.job_abort	= job_abort,
};

static int m2mtest_probe(struct platform_device *pdev)
{
	struct m2mtest_dev *dev;
	struct video_device *vfd;
	int ret;

	dev = kzalloc(sizeof *dev, GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	spin_lock_init(&dev->irqlock);

	ret = v4l2_device_register(&pdev->dev, &dev->v4l2_dev);
	if (ret)
		goto free_dev;

	atomic_set(&dev->num_inst, 0);
	mutex_init(&dev->dev_mutex);

	vfd = video_device_alloc();
	if (!vfd) {
		v4l2_err(&dev->v4l2_dev, "Failed to allocate video device\n");
		ret = -ENOMEM;
		goto unreg_dev;
	}

	*vfd = m2mtest_videodev;

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

	q_data[V4L2_M2M_SRC].fmt = &formats[0];
	q_data[V4L2_M2M_DST].fmt = &formats[0];

	return 0;

err_m2m:
	video_unregister_device(dev->vfd);
rel_vdev:
	video_device_release(vfd);
unreg_dev:
	v4l2_device_unregister(&dev->v4l2_dev);
free_dev:
	kfree(dev);

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
	kfree(dev);

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

