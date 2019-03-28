// SPDX-License-Identifier: GPL-2.0+
/*
 * A virtual v4l2-mem2mem example device.
 *
 * This is a virtual device driver for testing mem-to-mem videobuf framework.
 * It simulates a device that uses memory buffers for both source and
 * destination, processes the data and issues an "irq" (simulated by a delayed
 * workqueue).
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
#include <linux/sched.h>
#include <linux/slab.h>

#include <linux/platform_device.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/videobuf2-vmalloc.h>

MODULE_DESCRIPTION("Virtual device for mem2mem framework testing");
MODULE_AUTHOR("Pawel Osciak, <pawel@osciak.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.2");
MODULE_ALIAS("mem2mem_testdev");

static unsigned int debug;
module_param(debug, uint, 0644);
MODULE_PARM_DESC(debug, "debug level");

/* Default transaction time in msec */
static unsigned int default_transtime = 40; /* Max 25 fps */
module_param(default_transtime, uint, 0644);
MODULE_PARM_DESC(default_transtime, "default transaction time in ms");

#define MIN_W 32
#define MIN_H 32
#define MAX_W 640
#define MAX_H 480

/* Pixel alignment for non-bayer formats */
#define WIDTH_ALIGN 2
#define HEIGHT_ALIGN 1

/* Pixel alignment for bayer formats */
#define BAYER_WIDTH_ALIGN  2
#define BAYER_HEIGHT_ALIGN 2

/* Flags that indicate a format can be used for capture/output */
#define MEM2MEM_CAPTURE	BIT(0)
#define MEM2MEM_OUTPUT	BIT(1)

#define MEM2MEM_NAME		"vim2m"

/* Per queue */
#define MEM2MEM_DEF_NUM_BUFS	VIDEO_MAX_FRAME
/* In bytes, per queue */
#define MEM2MEM_VID_MEM_LIMIT	(16 * 1024 * 1024)

/* Flags that indicate processing mode */
#define MEM2MEM_HFLIP	BIT(0)
#define MEM2MEM_VFLIP	BIT(1)

#define dprintk(dev, lvl, fmt, arg...) \
	v4l2_dbg(lvl, debug, &(dev)->v4l2_dev, "%s: " fmt, __func__, ## arg)

static void vim2m_dev_release(struct device *dev)
{}

static struct platform_device vim2m_pdev = {
	.name		= MEM2MEM_NAME,
	.dev.release	= vim2m_dev_release,
};

struct vim2m_fmt {
	u32	fourcc;
	int	depth;
	/* Types the format can be used for */
	u32     types;
};

static struct vim2m_fmt formats[] = {
	{
		.fourcc	= V4L2_PIX_FMT_RGB565,  /* rrrrrggg gggbbbbb */
		.depth	= 16,
		.types  = MEM2MEM_CAPTURE | MEM2MEM_OUTPUT,
	}, {
		.fourcc	= V4L2_PIX_FMT_RGB565X, /* gggbbbbb rrrrrggg */
		.depth	= 16,
		.types  = MEM2MEM_CAPTURE | MEM2MEM_OUTPUT,
	}, {
		.fourcc	= V4L2_PIX_FMT_RGB24,
		.depth	= 24,
		.types  = MEM2MEM_CAPTURE | MEM2MEM_OUTPUT,
	}, {
		.fourcc	= V4L2_PIX_FMT_BGR24,
		.depth	= 24,
		.types  = MEM2MEM_CAPTURE | MEM2MEM_OUTPUT,
	}, {
		.fourcc	= V4L2_PIX_FMT_YUYV,
		.depth	= 16,
		.types  = MEM2MEM_CAPTURE,
	}, {
		.fourcc	= V4L2_PIX_FMT_SBGGR8,
		.depth	= 8,
		.types  = MEM2MEM_CAPTURE,
	}, {
		.fourcc	= V4L2_PIX_FMT_SGBRG8,
		.depth	= 8,
		.types  = MEM2MEM_CAPTURE,
	}, {
		.fourcc	= V4L2_PIX_FMT_SGRBG8,
		.depth	= 8,
		.types  = MEM2MEM_CAPTURE,
	}, {
		.fourcc	= V4L2_PIX_FMT_SRGGB8,
		.depth	= 8,
		.types  = MEM2MEM_CAPTURE,
	},
};

#define NUM_FORMATS ARRAY_SIZE(formats)

/* Per-queue, driver-specific private data */
struct vim2m_q_data {
	unsigned int		width;
	unsigned int		height;
	unsigned int		sizeimage;
	unsigned int		sequence;
	struct vim2m_fmt	*fmt;
};

enum {
	V4L2_M2M_SRC = 0,
	V4L2_M2M_DST = 1,
};

#define V4L2_CID_TRANS_TIME_MSEC	(V4L2_CID_USER_BASE + 0x1000)
#define V4L2_CID_TRANS_NUM_BUFS		(V4L2_CID_USER_BASE + 0x1001)

static struct vim2m_fmt *find_format(u32 fourcc)
{
	struct vim2m_fmt *fmt;
	unsigned int k;

	for (k = 0; k < NUM_FORMATS; k++) {
		fmt = &formats[k];
		if (fmt->fourcc == fourcc)
			break;
	}

	if (k == NUM_FORMATS)
		return NULL;

	return &formats[k];
}

static void get_alignment(u32 fourcc,
			  unsigned int *walign, unsigned int *halign)
{
	switch (fourcc) {
	case V4L2_PIX_FMT_SBGGR8:
	case V4L2_PIX_FMT_SGBRG8:
	case V4L2_PIX_FMT_SGRBG8:
	case V4L2_PIX_FMT_SRGGB8:
		*walign = BAYER_WIDTH_ALIGN;
		*halign = BAYER_HEIGHT_ALIGN;
		return;
	default:
		*walign = WIDTH_ALIGN;
		*halign = HEIGHT_ALIGN;
		return;
	}
}

struct vim2m_dev {
	struct v4l2_device	v4l2_dev;
	struct video_device	vfd;
#ifdef CONFIG_MEDIA_CONTROLLER
	struct media_device	mdev;
#endif

	atomic_t		num_inst;
	struct mutex		dev_mutex;

	struct v4l2_m2m_dev	*m2m_dev;
};

struct vim2m_ctx {
	struct v4l2_fh		fh;
	struct vim2m_dev	*dev;

	struct v4l2_ctrl_handler hdl;

	/* Processed buffers in this transaction */
	u8			num_processed;

	/* Transaction length (i.e. how many buffers per transaction) */
	u32			translen;
	/* Transaction time (i.e. simulated processing time) in milliseconds */
	u32			transtime;

	struct mutex		vb_mutex;
	struct delayed_work	work_run;
	spinlock_t		irqlock;

	/* Abort requested by m2m */
	int			aborting;

	/* Processing mode */
	int			mode;

	enum v4l2_colorspace	colorspace;
	enum v4l2_ycbcr_encoding ycbcr_enc;
	enum v4l2_xfer_func	xfer_func;
	enum v4l2_quantization	quant;

	/* Source and destination queue data */
	struct vim2m_q_data   q_data[2];
};

static inline struct vim2m_ctx *file2ctx(struct file *file)
{
	return container_of(file->private_data, struct vim2m_ctx, fh);
}

static struct vim2m_q_data *get_q_data(struct vim2m_ctx *ctx,
				       enum v4l2_buf_type type)
{
	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		return &ctx->q_data[V4L2_M2M_SRC];
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		return &ctx->q_data[V4L2_M2M_DST];
	default:
		return NULL;
	}
}

static const char *type_name(enum v4l2_buf_type type)
{
	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		return "Output";
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		return "Capture";
	default:
		return "Invalid";
	}
}

#define CLIP(__color) \
	(u8)(((__color) > 0xff) ? 0xff : (((__color) < 0) ? 0 : (__color)))

static void copy_line(struct vim2m_q_data *q_data_out,
		      u8 *src, u8 *dst, bool reverse)
{
	int x, depth = q_data_out->fmt->depth >> 3;

	if (!reverse) {
		memcpy(dst, src, q_data_out->width * depth);
	} else {
		for (x = 0; x < q_data_out->width >> 1; x++) {
			memcpy(dst, src, depth);
			memcpy(dst + depth, src - depth, depth);
			src -= depth << 1;
			dst += depth << 1;
		}
		return;
	}
}

static void copy_two_pixels(struct vim2m_q_data *q_data_in,
			    struct vim2m_q_data *q_data_out,
			    u8 *src[2], u8 **dst, int ypos, bool reverse)
{
	struct vim2m_fmt *out = q_data_out->fmt;
	struct vim2m_fmt *in = q_data_in->fmt;
	u8 _r[2], _g[2], _b[2], *r, *g, *b;
	int i;

	/* Step 1: read two consecutive pixels from src pointer */

	r = _r;
	g = _g;
	b = _b;

	switch (in->fourcc) {
	case V4L2_PIX_FMT_RGB565: /* rrrrrggg gggbbbbb */
		for (i = 0; i < 2; i++) {
			u16 pix = le16_to_cpu(*(__le16 *)(src[i]));

			*r++ = (u8)(((pix & 0xf800) >> 11) << 3) | 0x07;
			*g++ = (u8)((((pix & 0x07e0) >> 5)) << 2) | 0x03;
			*b++ = (u8)((pix & 0x1f) << 3) | 0x07;
		}
		break;
	case V4L2_PIX_FMT_RGB565X: /* gggbbbbb rrrrrggg */
		for (i = 0; i < 2; i++) {
			u16 pix = be16_to_cpu(*(__be16 *)(src[i]));

			*r++ = (u8)(((pix & 0xf800) >> 11) << 3) | 0x07;
			*g++ = (u8)((((pix & 0x07e0) >> 5)) << 2) | 0x03;
			*b++ = (u8)((pix & 0x1f) << 3) | 0x07;
		}
		break;
	default:
	case V4L2_PIX_FMT_RGB24:
		for (i = 0; i < 2; i++) {
			*r++ = src[i][0];
			*g++ = src[i][1];
			*b++ = src[i][2];
		}
		break;
	case V4L2_PIX_FMT_BGR24:
		for (i = 0; i < 2; i++) {
			*b++ = src[i][0];
			*g++ = src[i][1];
			*r++ = src[i][2];
		}
		break;
	}

	/* Step 2: store two consecutive points, reversing them if needed */

	r = _r;
	g = _g;
	b = _b;

	switch (out->fourcc) {
	case V4L2_PIX_FMT_RGB565: /* rrrrrggg gggbbbbb */
		for (i = 0; i < 2; i++) {
			u16 pix;
			__le16 *dst_pix = (__le16 *)*dst;

			pix = ((*r << 8) & 0xf800) | ((*g << 3) & 0x07e0) |
			      (*b >> 3);

			*dst_pix = cpu_to_le16(pix);

			*dst += 2;
		}
		return;
	case V4L2_PIX_FMT_RGB565X: /* gggbbbbb rrrrrggg */
		for (i = 0; i < 2; i++) {
			u16 pix;
			__be16 *dst_pix = (__be16 *)*dst;

			pix = ((*r << 8) & 0xf800) | ((*g << 3) & 0x07e0) |
			      (*b >> 3);

			*dst_pix = cpu_to_be16(pix);

			*dst += 2;
		}
		return;
	case V4L2_PIX_FMT_RGB24:
		for (i = 0; i < 2; i++) {
			*(*dst)++ = *r++;
			*(*dst)++ = *g++;
			*(*dst)++ = *b++;
		}
		return;
	case V4L2_PIX_FMT_BGR24:
		for (i = 0; i < 2; i++) {
			*(*dst)++ = *b++;
			*(*dst)++ = *g++;
			*(*dst)++ = *r++;
		}
		return;
	case V4L2_PIX_FMT_YUYV:
	default:
	{
		u8 y, y1, u, v;

		y = ((8453  * (*r) + 16594 * (*g) +  3223 * (*b)
		     + 524288) >> 15);
		u = ((-4878 * (*r) - 9578  * (*g) + 14456 * (*b)
		     + 4210688) >> 15);
		v = ((14456 * (*r++) - 12105 * (*g++) - 2351 * (*b++)
		     + 4210688) >> 15);
		y1 = ((8453 * (*r) + 16594 * (*g) +  3223 * (*b)
		     + 524288) >> 15);

		*(*dst)++ = y;
		*(*dst)++ = u;

		*(*dst)++ = y1;
		*(*dst)++ = v;
		return;
	}
	case V4L2_PIX_FMT_SBGGR8:
		if (!(ypos & 1)) {
			*(*dst)++ = *b;
			*(*dst)++ = *++g;
		} else {
			*(*dst)++ = *g;
			*(*dst)++ = *++r;
		}
		return;
	case V4L2_PIX_FMT_SGBRG8:
		if (!(ypos & 1)) {
			*(*dst)++ = *g;
			*(*dst)++ = *++b;
		} else {
			*(*dst)++ = *r;
			*(*dst)++ = *++g;
		}
		return;
	case V4L2_PIX_FMT_SGRBG8:
		if (!(ypos & 1)) {
			*(*dst)++ = *g;
			*(*dst)++ = *++r;
		} else {
			*(*dst)++ = *b;
			*(*dst)++ = *++g;
		}
		return;
	case V4L2_PIX_FMT_SRGGB8:
		if (!(ypos & 1)) {
			*(*dst)++ = *r;
			*(*dst)++ = *++g;
		} else {
			*(*dst)++ = *g;
			*(*dst)++ = *++b;
		}
		return;
	}
}

static int device_process(struct vim2m_ctx *ctx,
			  struct vb2_v4l2_buffer *in_vb,
			  struct vb2_v4l2_buffer *out_vb)
{
	struct vim2m_dev *dev = ctx->dev;
	struct vim2m_q_data *q_data_in, *q_data_out;
	u8 *p_in, *p_line, *p_in_x[2], *p, *p_out;
	unsigned int width, height, bytesperline, bytes_per_pixel;
	unsigned int x, y, y_in, y_out, x_int, x_fract, x_err, x_offset;
	int start, end, step;

	q_data_in = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
	if (!q_data_in)
		return 0;
	bytesperline = (q_data_in->width * q_data_in->fmt->depth) >> 3;
	bytes_per_pixel = q_data_in->fmt->depth >> 3;

	q_data_out = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);
	if (!q_data_out)
		return 0;

	/* As we're doing scaling, use the output dimensions here */
	height = q_data_out->height;
	width = q_data_out->width;

	p_in = vb2_plane_vaddr(&in_vb->vb2_buf, 0);
	p_out = vb2_plane_vaddr(&out_vb->vb2_buf, 0);
	if (!p_in || !p_out) {
		v4l2_err(&dev->v4l2_dev,
			 "Acquiring kernel pointers to buffers failed\n");
		return -EFAULT;
	}

	out_vb->sequence = q_data_out->sequence++;
	in_vb->sequence = q_data_in->sequence++;
	v4l2_m2m_buf_copy_metadata(in_vb, out_vb, true);

	if (ctx->mode & MEM2MEM_VFLIP) {
		start = height - 1;
		end = -1;
		step = -1;
	} else {
		start = 0;
		end = height;
		step = 1;
	}
	y_out = 0;

	/*
	 * When format and resolution are identical,
	 * we can use a faster copy logic
	 */
	if (q_data_in->fmt->fourcc == q_data_out->fmt->fourcc &&
	    q_data_in->width == q_data_out->width &&
	    q_data_in->height == q_data_out->height) {
		for (y = start; y != end; y += step, y_out++) {
			p = p_in + (y * bytesperline);
			if (ctx->mode & MEM2MEM_HFLIP)
				p += bytesperline - (q_data_in->fmt->depth >> 3);

			copy_line(q_data_out, p, p_out,
				  ctx->mode & MEM2MEM_HFLIP);

			p_out += bytesperline;
		}
		return 0;
	}

	/* Slower algorithm with format conversion, hflip, vflip and scaler */

	/* To speed scaler up, use Bresenham for X dimension */
	x_int = q_data_in->width / q_data_out->width;
	x_fract = q_data_in->width % q_data_out->width;

	for (y = start; y != end; y += step, y_out++) {
		y_in = (y * q_data_in->height) / q_data_out->height;
		x_offset = 0;
		x_err = 0;

		p_line = p_in + (y_in * bytesperline);
		if (ctx->mode & MEM2MEM_HFLIP)
			p_line += bytesperline - (q_data_in->fmt->depth >> 3);
		p_in_x[0] = p_line;

		for (x = 0; x < width >> 1; x++) {
			x_offset += x_int;
			x_err += x_fract;
			if (x_err > width) {
				x_offset++;
				x_err -= width;
			}

			if (ctx->mode & MEM2MEM_HFLIP)
				p_in_x[1] = p_line - x_offset * bytes_per_pixel;
			else
				p_in_x[1] = p_line + x_offset * bytes_per_pixel;

			copy_two_pixels(q_data_in, q_data_out,
					p_in_x, &p_out, y_out,
					ctx->mode & MEM2MEM_HFLIP);

			/* Calculate the next p_in_x0 */
			x_offset += x_int;
			x_err += x_fract;
			if (x_err > width) {
				x_offset++;
				x_err -= width;
			}

			if (ctx->mode & MEM2MEM_HFLIP)
				p_in_x[0] = p_line - x_offset * bytes_per_pixel;
			else
				p_in_x[0] = p_line + x_offset * bytes_per_pixel;
		}
	}

	return 0;
}

/*
 * mem2mem callbacks
 */

/*
 * job_ready() - check whether an instance is ready to be scheduled to run
 */
static int job_ready(void *priv)
{
	struct vim2m_ctx *ctx = priv;

	if (v4l2_m2m_num_src_bufs_ready(ctx->fh.m2m_ctx) < ctx->translen
	    || v4l2_m2m_num_dst_bufs_ready(ctx->fh.m2m_ctx) < ctx->translen) {
		dprintk(ctx->dev, 1, "Not enough buffers available\n");
		return 0;
	}

	return 1;
}

static void job_abort(void *priv)
{
	struct vim2m_ctx *ctx = priv;

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
	struct vim2m_ctx *ctx = priv;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;

	src_buf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	dst_buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);

	/* Apply request controls if any */
	v4l2_ctrl_request_setup(src_buf->vb2_buf.req_obj.req,
				&ctx->hdl);

	device_process(ctx, src_buf, dst_buf);

	/* Complete request controls if any */
	v4l2_ctrl_request_complete(src_buf->vb2_buf.req_obj.req,
				   &ctx->hdl);

	/* Run delayed work, which simulates a hardware irq  */
	schedule_delayed_work(&ctx->work_run, msecs_to_jiffies(ctx->transtime));
}

static void device_work(struct work_struct *w)
{
	struct vim2m_ctx *curr_ctx;
	struct vim2m_dev *vim2m_dev;
	struct vb2_v4l2_buffer *src_vb, *dst_vb;
	unsigned long flags;

	curr_ctx = container_of(w, struct vim2m_ctx, work_run.work);

	if (!curr_ctx) {
		pr_err("Instance released before the end of transaction\n");
		return;
	}

	vim2m_dev = curr_ctx->dev;

	src_vb = v4l2_m2m_src_buf_remove(curr_ctx->fh.m2m_ctx);
	dst_vb = v4l2_m2m_dst_buf_remove(curr_ctx->fh.m2m_ctx);

	curr_ctx->num_processed++;

	spin_lock_irqsave(&curr_ctx->irqlock, flags);
	v4l2_m2m_buf_done(src_vb, VB2_BUF_STATE_DONE);
	v4l2_m2m_buf_done(dst_vb, VB2_BUF_STATE_DONE);
	spin_unlock_irqrestore(&curr_ctx->irqlock, flags);

	if (curr_ctx->num_processed == curr_ctx->translen
	    || curr_ctx->aborting) {
		dprintk(curr_ctx->dev, 2, "Finishing capture buffer fill\n");
		curr_ctx->num_processed = 0;
		v4l2_m2m_job_finish(vim2m_dev->m2m_dev, curr_ctx->fh.m2m_ctx);
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
	return 0;
}

static int enum_fmt(struct v4l2_fmtdesc *f, u32 type)
{
	int i, num;
	struct vim2m_fmt *fmt;

	num = 0;

	for (i = 0; i < NUM_FORMATS; ++i) {
		if (formats[i].types & type) {
			/* index-th format of type type found ? */
			if (num == f->index)
				break;
			/*
			 * Correct type but haven't reached our index yet,
			 * just increment per-type index
			 */
			++num;
		}
	}

	if (i < NUM_FORMATS) {
		/* Format found */
		fmt = &formats[i];
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

static int vidioc_enum_framesizes(struct file *file, void *priv,
				  struct v4l2_frmsizeenum *fsize)
{
	if (fsize->index != 0)
		return -EINVAL;

	if (!find_format(fsize->pixel_format))
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	fsize->stepwise.min_width = MIN_W;
	fsize->stepwise.min_height = MIN_H;
	fsize->stepwise.max_width = MAX_W;
	fsize->stepwise.max_height = MAX_H;

	get_alignment(fsize->pixel_format,
		      &fsize->stepwise.step_width,
		      &fsize->stepwise.step_height);
	return 0;
}

static int vidioc_g_fmt(struct vim2m_ctx *ctx, struct v4l2_format *f)
{
	struct vb2_queue *vq;
	struct vim2m_q_data *q_data;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	q_data = get_q_data(ctx, f->type);
	if (!q_data)
		return -EINVAL;

	f->fmt.pix.width	= q_data->width;
	f->fmt.pix.height	= q_data->height;
	f->fmt.pix.field	= V4L2_FIELD_NONE;
	f->fmt.pix.pixelformat	= q_data->fmt->fourcc;
	f->fmt.pix.bytesperline	= (q_data->width * q_data->fmt->depth) >> 3;
	f->fmt.pix.sizeimage	= q_data->sizeimage;
	f->fmt.pix.colorspace	= ctx->colorspace;
	f->fmt.pix.xfer_func	= ctx->xfer_func;
	f->fmt.pix.ycbcr_enc	= ctx->ycbcr_enc;
	f->fmt.pix.quantization	= ctx->quant;

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

static int vidioc_try_fmt(struct v4l2_format *f, struct vim2m_fmt *fmt)
{
	int walign, halign;
	/*
	 * V4L2 specification specifies the driver corrects the
	 * format struct if any of the dimensions is unsupported
	 */
	if (f->fmt.pix.height < MIN_H)
		f->fmt.pix.height = MIN_H;
	else if (f->fmt.pix.height > MAX_H)
		f->fmt.pix.height = MAX_H;

	if (f->fmt.pix.width < MIN_W)
		f->fmt.pix.width = MIN_W;
	else if (f->fmt.pix.width > MAX_W)
		f->fmt.pix.width = MAX_W;

	get_alignment(f->fmt.pix.pixelformat, &walign, &halign);
	f->fmt.pix.width &= ~(walign - 1);
	f->fmt.pix.height &= ~(halign - 1);
	f->fmt.pix.bytesperline = (f->fmt.pix.width * fmt->depth) >> 3;
	f->fmt.pix.sizeimage = f->fmt.pix.height * f->fmt.pix.bytesperline;
	f->fmt.pix.field = V4L2_FIELD_NONE;

	return 0;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct vim2m_fmt *fmt;
	struct vim2m_ctx *ctx = file2ctx(file);

	fmt = find_format(f->fmt.pix.pixelformat);
	if (!fmt) {
		f->fmt.pix.pixelformat = formats[0].fourcc;
		fmt = find_format(f->fmt.pix.pixelformat);
	}
	if (!(fmt->types & MEM2MEM_CAPTURE)) {
		v4l2_err(&ctx->dev->v4l2_dev,
			 "Fourcc format (0x%08x) invalid.\n",
			 f->fmt.pix.pixelformat);
		return -EINVAL;
	}
	f->fmt.pix.colorspace = ctx->colorspace;
	f->fmt.pix.xfer_func = ctx->xfer_func;
	f->fmt.pix.ycbcr_enc = ctx->ycbcr_enc;
	f->fmt.pix.quantization = ctx->quant;

	return vidioc_try_fmt(f, fmt);
}

static int vidioc_try_fmt_vid_out(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct vim2m_fmt *fmt;
	struct vim2m_ctx *ctx = file2ctx(file);

	fmt = find_format(f->fmt.pix.pixelformat);
	if (!fmt) {
		f->fmt.pix.pixelformat = formats[0].fourcc;
		fmt = find_format(f->fmt.pix.pixelformat);
	}
	if (!(fmt->types & MEM2MEM_OUTPUT)) {
		v4l2_err(&ctx->dev->v4l2_dev,
			 "Fourcc format (0x%08x) invalid.\n",
			 f->fmt.pix.pixelformat);
		return -EINVAL;
	}
	if (!f->fmt.pix.colorspace)
		f->fmt.pix.colorspace = V4L2_COLORSPACE_REC709;

	return vidioc_try_fmt(f, fmt);
}

static int vidioc_s_fmt(struct vim2m_ctx *ctx, struct v4l2_format *f)
{
	struct vim2m_q_data *q_data;
	struct vb2_queue *vq;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	q_data = get_q_data(ctx, f->type);
	if (!q_data)
		return -EINVAL;

	if (vb2_is_busy(vq)) {
		v4l2_err(&ctx->dev->v4l2_dev, "%s queue busy\n", __func__);
		return -EBUSY;
	}

	q_data->fmt		= find_format(f->fmt.pix.pixelformat);
	q_data->width		= f->fmt.pix.width;
	q_data->height		= f->fmt.pix.height;
	q_data->sizeimage	= q_data->width * q_data->height
				* q_data->fmt->depth >> 3;

	dprintk(ctx->dev, 1,
		"Format for type %s: %dx%d (%d bpp), fmt: %c%c%c%c\n",
		type_name(f->type), q_data->width, q_data->height,
		q_data->fmt->depth,
		(q_data->fmt->fourcc & 0xff),
		(q_data->fmt->fourcc >>  8) & 0xff,
		(q_data->fmt->fourcc >> 16) & 0xff,
		(q_data->fmt->fourcc >> 24) & 0xff);

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
	struct vim2m_ctx *ctx = file2ctx(file);
	int ret;

	ret = vidioc_try_fmt_vid_out(file, priv, f);
	if (ret)
		return ret;

	ret = vidioc_s_fmt(file2ctx(file), f);
	if (!ret) {
		ctx->colorspace = f->fmt.pix.colorspace;
		ctx->xfer_func = f->fmt.pix.xfer_func;
		ctx->ycbcr_enc = f->fmt.pix.ycbcr_enc;
		ctx->quant = f->fmt.pix.quantization;
	}
	return ret;
}

static int vim2m_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vim2m_ctx *ctx =
		container_of(ctrl->handler, struct vim2m_ctx, hdl);

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
		if (ctx->transtime < 1)
			ctx->transtime = 1;
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

static const struct v4l2_ctrl_ops vim2m_ctrl_ops = {
	.s_ctrl = vim2m_s_ctrl,
};

static const struct v4l2_ioctl_ops vim2m_ioctl_ops = {
	.vidioc_querycap	= vidioc_querycap,

	.vidioc_enum_fmt_vid_cap = vidioc_enum_fmt_vid_cap,
	.vidioc_enum_framesizes = vidioc_enum_framesizes,
	.vidioc_g_fmt_vid_cap	= vidioc_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap	= vidioc_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap	= vidioc_s_fmt_vid_cap,

	.vidioc_enum_fmt_vid_out = vidioc_enum_fmt_vid_out,
	.vidioc_g_fmt_vid_out	= vidioc_g_fmt_vid_out,
	.vidioc_try_fmt_vid_out	= vidioc_try_fmt_vid_out,
	.vidioc_s_fmt_vid_out	= vidioc_s_fmt_vid_out,

	.vidioc_reqbufs		= v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf	= v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf		= v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf		= v4l2_m2m_ioctl_dqbuf,
	.vidioc_prepare_buf	= v4l2_m2m_ioctl_prepare_buf,
	.vidioc_create_bufs	= v4l2_m2m_ioctl_create_bufs,
	.vidioc_expbuf		= v4l2_m2m_ioctl_expbuf,

	.vidioc_streamon	= v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff	= v4l2_m2m_ioctl_streamoff,

	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

/*
 * Queue operations
 */

static int vim2m_queue_setup(struct vb2_queue *vq,
			     unsigned int *nbuffers,
			     unsigned int *nplanes,
			     unsigned int sizes[],
			     struct device *alloc_devs[])
{
	struct vim2m_ctx *ctx = vb2_get_drv_priv(vq);
	struct vim2m_q_data *q_data;
	unsigned int size, count = *nbuffers;

	q_data = get_q_data(ctx, vq->type);
	if (!q_data)
		return -EINVAL;

	size = q_data->width * q_data->height * q_data->fmt->depth >> 3;

	while (size * count > MEM2MEM_VID_MEM_LIMIT)
		(count)--;
	*nbuffers = count;

	if (*nplanes)
		return sizes[0] < size ? -EINVAL : 0;

	*nplanes = 1;
	sizes[0] = size;

	dprintk(ctx->dev, 1, "%s: get %d buffer(s) of size %d each.\n",
		type_name(vq->type), count, size);

	return 0;
}

static int vim2m_buf_out_validate(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vim2m_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	if (vbuf->field == V4L2_FIELD_ANY)
		vbuf->field = V4L2_FIELD_NONE;
	if (vbuf->field != V4L2_FIELD_NONE) {
		dprintk(ctx->dev, 1, "%s field isn't supported\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static int vim2m_buf_prepare(struct vb2_buffer *vb)
{
	struct vim2m_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vim2m_q_data *q_data;

	dprintk(ctx->dev, 2, "type: %s\n", type_name(vb->vb2_queue->type));

	q_data = get_q_data(ctx, vb->vb2_queue->type);
	if (!q_data)
		return -EINVAL;
	if (vb2_plane_size(vb, 0) < q_data->sizeimage) {
		dprintk(ctx->dev, 1,
			"%s data will not fit into plane (%lu < %lu)\n",
			__func__, vb2_plane_size(vb, 0),
			(long)q_data->sizeimage);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, q_data->sizeimage);

	return 0;
}

static void vim2m_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vim2m_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
}

static int vim2m_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct vim2m_ctx *ctx = vb2_get_drv_priv(q);
	struct vim2m_q_data *q_data = get_q_data(ctx, q->type);

	if (!q_data)
		return -EINVAL;

	q_data->sequence = 0;
	return 0;
}

static void vim2m_stop_streaming(struct vb2_queue *q)
{
	struct vim2m_ctx *ctx = vb2_get_drv_priv(q);
	struct vb2_v4l2_buffer *vbuf;
	unsigned long flags;

	cancel_delayed_work_sync(&ctx->work_run);

	for (;;) {
		if (V4L2_TYPE_IS_OUTPUT(q->type))
			vbuf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		else
			vbuf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
		if (!vbuf)
			return;
		v4l2_ctrl_request_complete(vbuf->vb2_buf.req_obj.req,
					   &ctx->hdl);
		spin_lock_irqsave(&ctx->irqlock, flags);
		v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_ERROR);
		spin_unlock_irqrestore(&ctx->irqlock, flags);
	}
}

static void vim2m_buf_request_complete(struct vb2_buffer *vb)
{
	struct vim2m_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_ctrl_request_complete(vb->req_obj.req, &ctx->hdl);
}

static const struct vb2_ops vim2m_qops = {
	.queue_setup	 = vim2m_queue_setup,
	.buf_out_validate	 = vim2m_buf_out_validate,
	.buf_prepare	 = vim2m_buf_prepare,
	.buf_queue	 = vim2m_buf_queue,
	.start_streaming = vim2m_start_streaming,
	.stop_streaming  = vim2m_stop_streaming,
	.wait_prepare	 = vb2_ops_wait_prepare,
	.wait_finish	 = vb2_ops_wait_finish,
	.buf_request_complete = vim2m_buf_request_complete,
};

static int queue_init(void *priv, struct vb2_queue *src_vq,
		      struct vb2_queue *dst_vq)
{
	struct vim2m_ctx *ctx = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	src_vq->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->ops = &vim2m_qops;
	src_vq->mem_ops = &vb2_vmalloc_memops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &ctx->vb_mutex;
	src_vq->supports_requests = true;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dst_vq->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->ops = &vim2m_qops;
	dst_vq->mem_ops = &vb2_vmalloc_memops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &ctx->vb_mutex;

	return vb2_queue_init(dst_vq);
}

static struct v4l2_ctrl_config vim2m_ctrl_trans_time_msec = {
	.ops = &vim2m_ctrl_ops,
	.id = V4L2_CID_TRANS_TIME_MSEC,
	.name = "Transaction Time (msec)",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 1,
	.max = 10001,
	.step = 1,
};

static const struct v4l2_ctrl_config vim2m_ctrl_trans_num_bufs = {
	.ops = &vim2m_ctrl_ops,
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
static int vim2m_open(struct file *file)
{
	struct vim2m_dev *dev = video_drvdata(file);
	struct vim2m_ctx *ctx = NULL;
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
	v4l2_ctrl_new_std(hdl, &vim2m_ctrl_ops, V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(hdl, &vim2m_ctrl_ops, V4L2_CID_VFLIP, 0, 1, 1, 0);

	vim2m_ctrl_trans_time_msec.def = default_transtime;
	v4l2_ctrl_new_custom(hdl, &vim2m_ctrl_trans_time_msec, NULL);
	v4l2_ctrl_new_custom(hdl, &vim2m_ctrl_trans_num_bufs, NULL);
	if (hdl->error) {
		rc = hdl->error;
		v4l2_ctrl_handler_free(hdl);
		kfree(ctx);
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

	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(dev->m2m_dev, ctx, &queue_init);

	mutex_init(&ctx->vb_mutex);
	spin_lock_init(&ctx->irqlock);
	INIT_DELAYED_WORK(&ctx->work_run, device_work);

	if (IS_ERR(ctx->fh.m2m_ctx)) {
		rc = PTR_ERR(ctx->fh.m2m_ctx);

		v4l2_ctrl_handler_free(hdl);
		v4l2_fh_exit(&ctx->fh);
		kfree(ctx);
		goto open_unlock;
	}

	v4l2_fh_add(&ctx->fh);
	atomic_inc(&dev->num_inst);

	dprintk(dev, 1, "Created instance: %p, m2m_ctx: %p\n",
		ctx, ctx->fh.m2m_ctx);

open_unlock:
	mutex_unlock(&dev->dev_mutex);
	return rc;
}

static int vim2m_release(struct file *file)
{
	struct vim2m_dev *dev = video_drvdata(file);
	struct vim2m_ctx *ctx = file2ctx(file);

	dprintk(dev, 1, "Releasing instance %p\n", ctx);

	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	v4l2_ctrl_handler_free(&ctx->hdl);
	mutex_lock(&dev->dev_mutex);
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
	mutex_unlock(&dev->dev_mutex);
	kfree(ctx);

	atomic_dec(&dev->num_inst);

	return 0;
}

static void vim2m_device_release(struct video_device *vdev)
{
	struct vim2m_dev *dev = container_of(vdev, struct vim2m_dev, vfd);

	v4l2_device_unregister(&dev->v4l2_dev);
	v4l2_m2m_release(dev->m2m_dev);
	kfree(dev);
}

static const struct v4l2_file_operations vim2m_fops = {
	.owner		= THIS_MODULE,
	.open		= vim2m_open,
	.release	= vim2m_release,
	.poll		= v4l2_m2m_fop_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= v4l2_m2m_fop_mmap,
};

static const struct video_device vim2m_videodev = {
	.name		= MEM2MEM_NAME,
	.vfl_dir	= VFL_DIR_M2M,
	.fops		= &vim2m_fops,
	.ioctl_ops	= &vim2m_ioctl_ops,
	.minor		= -1,
	.release	= vim2m_device_release,
	.device_caps	= V4L2_CAP_VIDEO_M2M | V4L2_CAP_STREAMING,
};

static const struct v4l2_m2m_ops m2m_ops = {
	.device_run	= device_run,
	.job_ready	= job_ready,
	.job_abort	= job_abort,
};

static const struct media_device_ops m2m_media_ops = {
	.req_validate = vb2_request_validate,
	.req_queue = v4l2_m2m_request_queue,
};

static int vim2m_probe(struct platform_device *pdev)
{
	struct vim2m_dev *dev;
	struct video_device *vfd;
	int ret;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	ret = v4l2_device_register(&pdev->dev, &dev->v4l2_dev);
	if (ret)
		goto error_free;

	atomic_set(&dev->num_inst, 0);
	mutex_init(&dev->dev_mutex);

	dev->vfd = vim2m_videodev;
	vfd = &dev->vfd;
	vfd->lock = &dev->dev_mutex;
	vfd->v4l2_dev = &dev->v4l2_dev;

	ret = video_register_device(vfd, VFL_TYPE_GRABBER, 0);
	if (ret) {
		v4l2_err(&dev->v4l2_dev, "Failed to register video device\n");
		goto error_v4l2;
	}

	video_set_drvdata(vfd, dev);
	v4l2_info(&dev->v4l2_dev,
		  "Device registered as /dev/video%d\n", vfd->num);

	platform_set_drvdata(pdev, dev);

	dev->m2m_dev = v4l2_m2m_init(&m2m_ops);
	if (IS_ERR(dev->m2m_dev)) {
		v4l2_err(&dev->v4l2_dev, "Failed to init mem2mem device\n");
		ret = PTR_ERR(dev->m2m_dev);
		goto error_dev;
	}

#ifdef CONFIG_MEDIA_CONTROLLER
	dev->mdev.dev = &pdev->dev;
	strscpy(dev->mdev.model, "vim2m", sizeof(dev->mdev.model));
	strscpy(dev->mdev.bus_info, "platform:vim2m",
		sizeof(dev->mdev.bus_info));
	media_device_init(&dev->mdev);
	dev->mdev.ops = &m2m_media_ops;
	dev->v4l2_dev.mdev = &dev->mdev;

	ret = v4l2_m2m_register_media_controller(dev->m2m_dev, vfd,
						 MEDIA_ENT_F_PROC_VIDEO_SCALER);
	if (ret) {
		v4l2_err(&dev->v4l2_dev, "Failed to init mem2mem media controller\n");
		goto error_m2m;
	}

	ret = media_device_register(&dev->mdev);
	if (ret) {
		v4l2_err(&dev->v4l2_dev, "Failed to register mem2mem media device\n");
		goto error_m2m_mc;
	}
#endif
	return 0;

#ifdef CONFIG_MEDIA_CONTROLLER
error_m2m_mc:
	v4l2_m2m_unregister_media_controller(dev->m2m_dev);
error_m2m:
	v4l2_m2m_release(dev->m2m_dev);
#endif
error_dev:
	video_unregister_device(&dev->vfd);
error_v4l2:
	v4l2_device_unregister(&dev->v4l2_dev);
error_free:
	kfree(dev);

	return ret;
}

static int vim2m_remove(struct platform_device *pdev)
{
	struct vim2m_dev *dev = platform_get_drvdata(pdev);

	v4l2_info(&dev->v4l2_dev, "Removing " MEM2MEM_NAME);

#ifdef CONFIG_MEDIA_CONTROLLER
	media_device_unregister(&dev->mdev);
	v4l2_m2m_unregister_media_controller(dev->m2m_dev);
	media_device_cleanup(&dev->mdev);
#endif
	video_unregister_device(&dev->vfd);

	return 0;
}

static struct platform_driver vim2m_pdrv = {
	.probe		= vim2m_probe,
	.remove		= vim2m_remove,
	.driver		= {
		.name	= MEM2MEM_NAME,
	},
};

static void __exit vim2m_exit(void)
{
	platform_driver_unregister(&vim2m_pdrv);
	platform_device_unregister(&vim2m_pdev);
}

static int __init vim2m_init(void)
{
	int ret;

	ret = platform_device_register(&vim2m_pdev);
	if (ret)
		return ret;

	ret = platform_driver_register(&vim2m_pdrv);
	if (ret)
		platform_device_unregister(&vim2m_pdev);

	return ret;
}

module_init(vim2m_init);
module_exit(vim2m_exit);
