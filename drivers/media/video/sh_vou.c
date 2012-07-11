/*
 * SuperH Video Output Unit (VOU) driver
 *
 * Copyright (C) 2010, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/module.h>

#include <media/sh_vou.h>
#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mediabus.h>
#include <media/videobuf-dma-contig.h>

/* Mirror addresses are not available for all registers */
#define VOUER	0
#define VOUCR	4
#define VOUSTR	8
#define VOUVCR	0xc
#define VOUISR	0x10
#define VOUBCR	0x14
#define VOUDPR	0x18
#define VOUDSR	0x1c
#define VOUVPR	0x20
#define VOUIR	0x24
#define VOUSRR	0x28
#define VOUMSR	0x2c
#define VOUHIR	0x30
#define VOUDFR	0x34
#define VOUAD1R	0x38
#define VOUAD2R	0x3c
#define VOUAIR	0x40
#define VOUSWR	0x44
#define VOURCR	0x48
#define VOURPR	0x50

enum sh_vou_status {
	SH_VOU_IDLE,
	SH_VOU_INITIALISING,
	SH_VOU_RUNNING,
};

#define VOU_MAX_IMAGE_WIDTH	720
#define VOU_MAX_IMAGE_HEIGHT	576

struct sh_vou_device {
	struct v4l2_device v4l2_dev;
	struct video_device *vdev;
	atomic_t use_count;
	struct sh_vou_pdata *pdata;
	spinlock_t lock;
	void __iomem *base;
	/* State information */
	struct v4l2_pix_format pix;
	struct v4l2_rect rect;
	struct list_head queue;
	v4l2_std_id std;
	int pix_idx;
	struct videobuf_buffer *active;
	enum sh_vou_status status;
	struct mutex fop_lock;
};

struct sh_vou_file {
	struct videobuf_queue vbq;
};

/* Register access routines for sides A, B and mirror addresses */
static void sh_vou_reg_a_write(struct sh_vou_device *vou_dev, unsigned int reg,
			       u32 value)
{
	__raw_writel(value, vou_dev->base + reg);
}

static void sh_vou_reg_ab_write(struct sh_vou_device *vou_dev, unsigned int reg,
				u32 value)
{
	__raw_writel(value, vou_dev->base + reg);
	__raw_writel(value, vou_dev->base + reg + 0x1000);
}

static void sh_vou_reg_m_write(struct sh_vou_device *vou_dev, unsigned int reg,
			       u32 value)
{
	__raw_writel(value, vou_dev->base + reg + 0x2000);
}

static u32 sh_vou_reg_a_read(struct sh_vou_device *vou_dev, unsigned int reg)
{
	return __raw_readl(vou_dev->base + reg);
}

static void sh_vou_reg_a_set(struct sh_vou_device *vou_dev, unsigned int reg,
			     u32 value, u32 mask)
{
	u32 old = __raw_readl(vou_dev->base + reg);

	value = (value & mask) | (old & ~mask);
	__raw_writel(value, vou_dev->base + reg);
}

static void sh_vou_reg_b_set(struct sh_vou_device *vou_dev, unsigned int reg,
			     u32 value, u32 mask)
{
	sh_vou_reg_a_set(vou_dev, reg + 0x1000, value, mask);
}

static void sh_vou_reg_ab_set(struct sh_vou_device *vou_dev, unsigned int reg,
			      u32 value, u32 mask)
{
	sh_vou_reg_a_set(vou_dev, reg, value, mask);
	sh_vou_reg_b_set(vou_dev, reg, value, mask);
}

struct sh_vou_fmt {
	u32		pfmt;
	char		*desc;
	unsigned char	bpp;
	unsigned char	rgb;
	unsigned char	yf;
	unsigned char	pkf;
};

/* Further pixel formats can be added */
static struct sh_vou_fmt vou_fmt[] = {
	{
		.pfmt	= V4L2_PIX_FMT_NV12,
		.bpp	= 12,
		.desc	= "YVU420 planar",
		.yf	= 0,
		.rgb	= 0,
	},
	{
		.pfmt	= V4L2_PIX_FMT_NV16,
		.bpp	= 16,
		.desc	= "YVYU planar",
		.yf	= 1,
		.rgb	= 0,
	},
	{
		.pfmt	= V4L2_PIX_FMT_RGB24,
		.bpp	= 24,
		.desc	= "RGB24",
		.pkf	= 2,
		.rgb	= 1,
	},
	{
		.pfmt	= V4L2_PIX_FMT_RGB565,
		.bpp	= 16,
		.desc	= "RGB565",
		.pkf	= 3,
		.rgb	= 1,
	},
	{
		.pfmt	= V4L2_PIX_FMT_RGB565X,
		.bpp	= 16,
		.desc	= "RGB565 byteswapped",
		.pkf	= 3,
		.rgb	= 1,
	},
};

static void sh_vou_schedule_next(struct sh_vou_device *vou_dev,
				 struct videobuf_buffer *vb)
{
	dma_addr_t addr1, addr2;

	addr1 = videobuf_to_dma_contig(vb);
	switch (vou_dev->pix.pixelformat) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV16:
		addr2 = addr1 + vou_dev->pix.width * vou_dev->pix.height;
		break;
	default:
		addr2 = 0;
	}

	sh_vou_reg_m_write(vou_dev, VOUAD1R, addr1);
	sh_vou_reg_m_write(vou_dev, VOUAD2R, addr2);
}

static void sh_vou_stream_start(struct sh_vou_device *vou_dev,
				struct videobuf_buffer *vb)
{
	unsigned int row_coeff;
#ifdef __LITTLE_ENDIAN
	u32 dataswap = 7;
#else
	u32 dataswap = 0;
#endif

	switch (vou_dev->pix.pixelformat) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV16:
		row_coeff = 1;
		break;
	case V4L2_PIX_FMT_RGB565:
		dataswap ^= 1;
	case V4L2_PIX_FMT_RGB565X:
		row_coeff = 2;
		break;
	case V4L2_PIX_FMT_RGB24:
		row_coeff = 3;
		break;
	}

	sh_vou_reg_a_write(vou_dev, VOUSWR, dataswap);
	sh_vou_reg_ab_write(vou_dev, VOUAIR, vou_dev->pix.width * row_coeff);
	sh_vou_schedule_next(vou_dev, vb);
}

static void free_buffer(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
	BUG_ON(in_interrupt());

	/* Wait until this buffer is no longer in STATE_QUEUED or STATE_ACTIVE */
	videobuf_waiton(vq, vb, 0, 0);
	videobuf_dma_contig_free(vq, vb);
	vb->state = VIDEOBUF_NEEDS_INIT;
}

/* Locking: caller holds fop_lock mutex */
static int sh_vou_buf_setup(struct videobuf_queue *vq, unsigned int *count,
			    unsigned int *size)
{
	struct video_device *vdev = vq->priv_data;
	struct sh_vou_device *vou_dev = video_get_drvdata(vdev);

	*size = vou_fmt[vou_dev->pix_idx].bpp * vou_dev->pix.width *
		vou_dev->pix.height / 8;

	if (*count < 2)
		*count = 2;

	/* Taking into account maximum frame size, *count will stay >= 2 */
	if (PAGE_ALIGN(*size) * *count > 4 * 1024 * 1024)
		*count = 4 * 1024 * 1024 / PAGE_ALIGN(*size);

	dev_dbg(vq->dev, "%s(): count=%d, size=%d\n", __func__, *count, *size);

	return 0;
}

/* Locking: caller holds fop_lock mutex */
static int sh_vou_buf_prepare(struct videobuf_queue *vq,
			      struct videobuf_buffer *vb,
			      enum v4l2_field field)
{
	struct video_device *vdev = vq->priv_data;
	struct sh_vou_device *vou_dev = video_get_drvdata(vdev);
	struct v4l2_pix_format *pix = &vou_dev->pix;
	int bytes_per_line = vou_fmt[vou_dev->pix_idx].bpp * pix->width / 8;
	int ret;

	dev_dbg(vq->dev, "%s()\n", __func__);

	if (vb->width	!= pix->width ||
	    vb->height	!= pix->height ||
	    vb->field	!= pix->field) {
		vb->width	= pix->width;
		vb->height	= pix->height;
		vb->field	= field;
		if (vb->state != VIDEOBUF_NEEDS_INIT)
			free_buffer(vq, vb);
	}

	vb->size = vb->height * bytes_per_line;
	if (vb->baddr && vb->bsize < vb->size) {
		/* User buffer too small */
		dev_warn(vq->dev, "User buffer too small: [%u] @ %lx\n",
			 vb->bsize, vb->baddr);
		return -EINVAL;
	}

	if (vb->state == VIDEOBUF_NEEDS_INIT) {
		ret = videobuf_iolock(vq, vb, NULL);
		if (ret < 0) {
			dev_warn(vq->dev, "IOLOCK buf-type %d: %d\n",
				 vb->memory, ret);
			return ret;
		}
		vb->state = VIDEOBUF_PREPARED;
	}

	dev_dbg(vq->dev,
		"%s(): fmt #%d, %u bytes per line, phys 0x%x, type %d, state %d\n",
		__func__, vou_dev->pix_idx, bytes_per_line,
		videobuf_to_dma_contig(vb), vb->memory, vb->state);

	return 0;
}

/* Locking: caller holds fop_lock mutex and vq->irqlock spinlock */
static void sh_vou_buf_queue(struct videobuf_queue *vq,
			     struct videobuf_buffer *vb)
{
	struct video_device *vdev = vq->priv_data;
	struct sh_vou_device *vou_dev = video_get_drvdata(vdev);

	dev_dbg(vq->dev, "%s()\n", __func__);

	vb->state = VIDEOBUF_QUEUED;
	list_add_tail(&vb->queue, &vou_dev->queue);

	if (vou_dev->status == SH_VOU_RUNNING) {
		return;
	} else if (!vou_dev->active) {
		vou_dev->active = vb;
		/* Start from side A: we use mirror addresses, so, set B */
		sh_vou_reg_a_write(vou_dev, VOURPR, 1);
		dev_dbg(vq->dev, "%s: first buffer status 0x%x\n", __func__,
			sh_vou_reg_a_read(vou_dev, VOUSTR));
		sh_vou_schedule_next(vou_dev, vb);
		/* Only activate VOU after the second buffer */
	} else if (vou_dev->active->queue.next == &vb->queue) {
		/* Second buffer - initialise register side B */
		sh_vou_reg_a_write(vou_dev, VOURPR, 0);
		sh_vou_stream_start(vou_dev, vb);

		/* Register side switching with frame VSYNC */
		sh_vou_reg_a_write(vou_dev, VOURCR, 5);
		dev_dbg(vq->dev, "%s: second buffer status 0x%x\n", __func__,
			sh_vou_reg_a_read(vou_dev, VOUSTR));

		/* Enable End-of-Frame (VSYNC) interrupts */
		sh_vou_reg_a_write(vou_dev, VOUIR, 0x10004);
		/* Two buffers on the queue - activate the hardware */

		vou_dev->status = SH_VOU_RUNNING;
		sh_vou_reg_a_write(vou_dev, VOUER, 0x107);
	}
}

static void sh_vou_buf_release(struct videobuf_queue *vq,
			       struct videobuf_buffer *vb)
{
	struct video_device *vdev = vq->priv_data;
	struct sh_vou_device *vou_dev = video_get_drvdata(vdev);
	unsigned long flags;

	dev_dbg(vq->dev, "%s()\n", __func__);

	spin_lock_irqsave(&vou_dev->lock, flags);

	if (vou_dev->active == vb) {
		/* disable output */
		sh_vou_reg_a_set(vou_dev, VOUER, 0, 1);
		/* ...but the current frame will complete */
		sh_vou_reg_a_set(vou_dev, VOUIR, 0, 0x30000);
		vou_dev->active = NULL;
	}

	if ((vb->state == VIDEOBUF_ACTIVE || vb->state == VIDEOBUF_QUEUED)) {
		vb->state = VIDEOBUF_ERROR;
		list_del(&vb->queue);
	}

	spin_unlock_irqrestore(&vou_dev->lock, flags);

	free_buffer(vq, vb);
}

static struct videobuf_queue_ops sh_vou_video_qops = {
	.buf_setup	= sh_vou_buf_setup,
	.buf_prepare	= sh_vou_buf_prepare,
	.buf_queue	= sh_vou_buf_queue,
	.buf_release	= sh_vou_buf_release,
};

/* Video IOCTLs */
static int sh_vou_querycap(struct file *file, void  *priv,
			   struct v4l2_capability *cap)
{
	struct sh_vou_file *vou_file = priv;

	dev_dbg(vou_file->vbq.dev, "%s()\n", __func__);

	strlcpy(cap->card, "SuperH VOU", sizeof(cap->card));
	cap->capabilities = V4L2_CAP_VIDEO_OUTPUT | V4L2_CAP_STREAMING;
	return 0;
}

/* Enumerate formats, that the device can accept from the user */
static int sh_vou_enum_fmt_vid_out(struct file *file, void  *priv,
				   struct v4l2_fmtdesc *fmt)
{
	struct sh_vou_file *vou_file = priv;

	if (fmt->index >= ARRAY_SIZE(vou_fmt))
		return -EINVAL;

	dev_dbg(vou_file->vbq.dev, "%s()\n", __func__);

	fmt->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	strlcpy(fmt->description, vou_fmt[fmt->index].desc,
		sizeof(fmt->description));
	fmt->pixelformat = vou_fmt[fmt->index].pfmt;

	return 0;
}

static int sh_vou_g_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *fmt)
{
	struct video_device *vdev = video_devdata(file);
	struct sh_vou_device *vou_dev = video_get_drvdata(vdev);

	dev_dbg(vou_dev->v4l2_dev.dev, "%s()\n", __func__);

	fmt->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	fmt->fmt.pix = vou_dev->pix;

	return 0;
}

static const unsigned char vou_scale_h_num[] = {1, 9, 2, 9, 4};
static const unsigned char vou_scale_h_den[] = {1, 8, 1, 4, 1};
static const unsigned char vou_scale_h_fld[] = {0, 2, 1, 3};
static const unsigned char vou_scale_v_num[] = {1, 2, 4};
static const unsigned char vou_scale_v_den[] = {1, 1, 1};
static const unsigned char vou_scale_v_fld[] = {0, 1};

static void sh_vou_configure_geometry(struct sh_vou_device *vou_dev,
				      int pix_idx, int w_idx, int h_idx)
{
	struct sh_vou_fmt *fmt = vou_fmt + pix_idx;
	unsigned int black_left, black_top, width_max, height_max,
		frame_in_height, frame_out_height, frame_out_top;
	struct v4l2_rect *rect = &vou_dev->rect;
	struct v4l2_pix_format *pix = &vou_dev->pix;
	u32 vouvcr = 0, dsr_h, dsr_v;

	if (vou_dev->std & V4L2_STD_525_60) {
		width_max = 858;
		height_max = 262;
	} else {
		width_max = 864;
		height_max = 312;
	}

	frame_in_height = pix->height / 2;
	frame_out_height = rect->height / 2;
	frame_out_top = rect->top / 2;

	/*
	 * Cropping scheme: max useful image is 720x480, and the total video
	 * area is 858x525 (NTSC) or 864x625 (PAL). AK8813 / 8814 starts
	 * sampling data beginning with fixed 276th (NTSC) / 288th (PAL) clock,
	 * of which the first 33 / 25 clocks HSYNC must be held active. This
	 * has to be configured in CR[HW]. 1 pixel equals 2 clock periods.
	 * This gives CR[HW] = 16 / 12, VPR[HVP] = 138 / 144, which gives
	 * exactly 858 - 138 = 864 - 144 = 720! We call the out-of-display area,
	 * beyond DSR, specified on the left and top by the VPR register "black
	 * pixels" and out-of-image area (DPR) "background pixels." We fix VPR
	 * at 138 / 144 : 20, because that's the HSYNC timing, that our first
	 * client requires, and that's exactly what leaves us 720 pixels for the
	 * image; we leave VPR[VVP] at default 20 for now, because the client
	 * doesn't seem to have any special requirements for it. Otherwise we
	 * could also set it to max - 240 = 22 / 72. Thus VPR depends only on
	 * the selected standard, and DPR and DSR are selected according to
	 * cropping. Q: how does the client detect the first valid line? Does
	 * HSYNC stay inactive during invalid (black) lines?
	 */
	black_left = width_max - VOU_MAX_IMAGE_WIDTH;
	black_top = 20;

	dsr_h = rect->width + rect->left;
	dsr_v = frame_out_height + frame_out_top;

	dev_dbg(vou_dev->v4l2_dev.dev,
		"image %ux%u, black %u:%u, offset %u:%u, display %ux%u\n",
		pix->width, frame_in_height, black_left, black_top,
		rect->left, frame_out_top, dsr_h, dsr_v);

	/* VOUISR height - half of a frame height in frame mode */
	sh_vou_reg_ab_write(vou_dev, VOUISR, (pix->width << 16) | frame_in_height);
	sh_vou_reg_ab_write(vou_dev, VOUVPR, (black_left << 16) | black_top);
	sh_vou_reg_ab_write(vou_dev, VOUDPR, (rect->left << 16) | frame_out_top);
	sh_vou_reg_ab_write(vou_dev, VOUDSR, (dsr_h << 16) | dsr_v);

	/*
	 * if necessary, we could set VOUHIR to
	 * max(black_left + dsr_h, width_max) here
	 */

	if (w_idx)
		vouvcr |= (1 << 15) | (vou_scale_h_fld[w_idx - 1] << 4);
	if (h_idx)
		vouvcr |= (1 << 14) | vou_scale_v_fld[h_idx - 1];

	dev_dbg(vou_dev->v4l2_dev.dev, "%s: scaling 0x%x\n", fmt->desc, vouvcr);

	/* To produce a colour bar for testing set bit 23 of VOUVCR */
	sh_vou_reg_ab_write(vou_dev, VOUVCR, vouvcr);
	sh_vou_reg_ab_write(vou_dev, VOUDFR,
			    fmt->pkf | (fmt->yf << 8) | (fmt->rgb << 16));
}

struct sh_vou_geometry {
	struct v4l2_rect output;
	unsigned int in_width;
	unsigned int in_height;
	int scale_idx_h;
	int scale_idx_v;
};

/*
 * Find input geometry, that we can use to produce output, closest to the
 * requested rectangle, using VOU scaling
 */
static void vou_adjust_input(struct sh_vou_geometry *geo, v4l2_std_id std)
{
	/* The compiler cannot know, that best and idx will indeed be set */
	unsigned int best_err = UINT_MAX, best = 0, img_height_max;
	int i, idx = 0;

	if (std & V4L2_STD_525_60)
		img_height_max = 480;
	else
		img_height_max = 576;

	/* Image width must be a multiple of 4 */
	v4l_bound_align_image(&geo->in_width, 0, VOU_MAX_IMAGE_WIDTH, 2,
			      &geo->in_height, 0, img_height_max, 1, 0);

	/* Select scales to come as close as possible to the output image */
	for (i = ARRAY_SIZE(vou_scale_h_num) - 1; i >= 0; i--) {
		unsigned int err;
		unsigned int found = geo->output.width * vou_scale_h_den[i] /
			vou_scale_h_num[i];

		if (found > VOU_MAX_IMAGE_WIDTH)
			/* scales increase */
			break;

		err = abs(found - geo->in_width);
		if (err < best_err) {
			best_err = err;
			idx = i;
			best = found;
		}
		if (!err)
			break;
	}

	geo->in_width = best;
	geo->scale_idx_h = idx;

	best_err = UINT_MAX;

	/* This loop can be replaced with one division */
	for (i = ARRAY_SIZE(vou_scale_v_num) - 1; i >= 0; i--) {
		unsigned int err;
		unsigned int found = geo->output.height * vou_scale_v_den[i] /
			vou_scale_v_num[i];

		if (found > img_height_max)
			/* scales increase */
			break;

		err = abs(found - geo->in_height);
		if (err < best_err) {
			best_err = err;
			idx = i;
			best = found;
		}
		if (!err)
			break;
	}

	geo->in_height = best;
	geo->scale_idx_v = idx;
}

/*
 * Find output geometry, that we can produce, using VOU scaling, closest to
 * the requested rectangle
 */
static void vou_adjust_output(struct sh_vou_geometry *geo, v4l2_std_id std)
{
	unsigned int best_err = UINT_MAX, best, width_max, height_max,
		img_height_max;
	int i, idx;

	if (std & V4L2_STD_525_60) {
		width_max = 858;
		height_max = 262 * 2;
		img_height_max = 480;
	} else {
		width_max = 864;
		height_max = 312 * 2;
		img_height_max = 576;
	}

	/* Select scales to come as close as possible to the output image */
	for (i = 0; i < ARRAY_SIZE(vou_scale_h_num); i++) {
		unsigned int err;
		unsigned int found = geo->in_width * vou_scale_h_num[i] /
			vou_scale_h_den[i];

		if (found > VOU_MAX_IMAGE_WIDTH)
			/* scales increase */
			break;

		err = abs(found - geo->output.width);
		if (err < best_err) {
			best_err = err;
			idx = i;
			best = found;
		}
		if (!err)
			break;
	}

	geo->output.width = best;
	geo->scale_idx_h = idx;
	if (geo->output.left + best > width_max)
		geo->output.left = width_max - best;

	pr_debug("%s(): W %u * %u/%u = %u\n", __func__, geo->in_width,
		 vou_scale_h_num[idx], vou_scale_h_den[idx], best);

	best_err = UINT_MAX;

	/* This loop can be replaced with one division */
	for (i = 0; i < ARRAY_SIZE(vou_scale_v_num); i++) {
		unsigned int err;
		unsigned int found = geo->in_height * vou_scale_v_num[i] /
			vou_scale_v_den[i];

		if (found > img_height_max)
			/* scales increase */
			break;

		err = abs(found - geo->output.height);
		if (err < best_err) {
			best_err = err;
			idx = i;
			best = found;
		}
		if (!err)
			break;
	}

	geo->output.height = best;
	geo->scale_idx_v = idx;
	if (geo->output.top + best > height_max)
		geo->output.top = height_max - best;

	pr_debug("%s(): H %u * %u/%u = %u\n", __func__, geo->in_height,
		 vou_scale_v_num[idx], vou_scale_v_den[idx], best);
}

static int sh_vou_s_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *fmt)
{
	struct video_device *vdev = video_devdata(file);
	struct sh_vou_device *vou_dev = video_get_drvdata(vdev);
	struct v4l2_pix_format *pix = &fmt->fmt.pix;
	unsigned int img_height_max;
	int pix_idx;
	struct sh_vou_geometry geo;
	struct v4l2_mbus_framefmt mbfmt = {
		/* Revisit: is this the correct code? */
		.code = V4L2_MBUS_FMT_YUYV8_2X8,
		.field = V4L2_FIELD_INTERLACED,
		.colorspace = V4L2_COLORSPACE_SMPTE170M,
	};
	int ret;

	dev_dbg(vou_dev->v4l2_dev.dev, "%s(): %ux%u -> %ux%u\n", __func__,
		vou_dev->rect.width, vou_dev->rect.height,
		pix->width, pix->height);

	if (pix->field == V4L2_FIELD_ANY)
		pix->field = V4L2_FIELD_NONE;

	if (fmt->type != V4L2_BUF_TYPE_VIDEO_OUTPUT ||
	    pix->field != V4L2_FIELD_NONE)
		return -EINVAL;

	for (pix_idx = 0; pix_idx < ARRAY_SIZE(vou_fmt); pix_idx++)
		if (vou_fmt[pix_idx].pfmt == pix->pixelformat)
			break;

	if (pix_idx == ARRAY_SIZE(vou_fmt))
		return -EINVAL;

	if (vou_dev->std & V4L2_STD_525_60)
		img_height_max = 480;
	else
		img_height_max = 576;

	/* Image width must be a multiple of 4 */
	v4l_bound_align_image(&pix->width, 0, VOU_MAX_IMAGE_WIDTH, 2,
			      &pix->height, 0, img_height_max, 1, 0);

	geo.in_width = pix->width;
	geo.in_height = pix->height;
	geo.output = vou_dev->rect;

	vou_adjust_output(&geo, vou_dev->std);

	mbfmt.width = geo.output.width;
	mbfmt.height = geo.output.height;
	ret = v4l2_device_call_until_err(&vou_dev->v4l2_dev, 0, video,
					 s_mbus_fmt, &mbfmt);
	/* Must be implemented, so, don't check for -ENOIOCTLCMD */
	if (ret < 0)
		return ret;

	dev_dbg(vou_dev->v4l2_dev.dev, "%s(): %ux%u -> %ux%u\n", __func__,
		geo.output.width, geo.output.height, mbfmt.width, mbfmt.height);

	/* Sanity checks */
	if ((unsigned)mbfmt.width > VOU_MAX_IMAGE_WIDTH ||
	    (unsigned)mbfmt.height > img_height_max ||
	    mbfmt.code != V4L2_MBUS_FMT_YUYV8_2X8)
		return -EIO;

	if (mbfmt.width != geo.output.width ||
	    mbfmt.height != geo.output.height) {
		geo.output.width = mbfmt.width;
		geo.output.height = mbfmt.height;

		vou_adjust_input(&geo, vou_dev->std);
	}

	/* We tried to preserve output rectangle, but it could have changed */
	vou_dev->rect = geo.output;
	pix->width = geo.in_width;
	pix->height = geo.in_height;

	dev_dbg(vou_dev->v4l2_dev.dev, "%s(): %ux%u\n", __func__,
		pix->width, pix->height);

	vou_dev->pix_idx = pix_idx;

	vou_dev->pix = *pix;

	sh_vou_configure_geometry(vou_dev, pix_idx,
				  geo.scale_idx_h, geo.scale_idx_v);

	return 0;
}

static int sh_vou_try_fmt_vid_out(struct file *file, void *priv,
				  struct v4l2_format *fmt)
{
	struct sh_vou_file *vou_file = priv;
	struct v4l2_pix_format *pix = &fmt->fmt.pix;
	int i;

	dev_dbg(vou_file->vbq.dev, "%s()\n", __func__);

	fmt->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	pix->field = V4L2_FIELD_NONE;

	v4l_bound_align_image(&pix->width, 0, VOU_MAX_IMAGE_WIDTH, 1,
			      &pix->height, 0, VOU_MAX_IMAGE_HEIGHT, 1, 0);

	for (i = 0; ARRAY_SIZE(vou_fmt); i++)
		if (vou_fmt[i].pfmt == pix->pixelformat)
			return 0;

	pix->pixelformat = vou_fmt[0].pfmt;

	return 0;
}

static int sh_vou_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *req)
{
	struct sh_vou_file *vou_file = priv;

	dev_dbg(vou_file->vbq.dev, "%s()\n", __func__);

	if (req->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;

	return videobuf_reqbufs(&vou_file->vbq, req);
}

static int sh_vou_querybuf(struct file *file, void *priv,
			   struct v4l2_buffer *b)
{
	struct sh_vou_file *vou_file = priv;

	dev_dbg(vou_file->vbq.dev, "%s()\n", __func__);

	return videobuf_querybuf(&vou_file->vbq, b);
}

static int sh_vou_qbuf(struct file *file, void *priv, struct v4l2_buffer *b)
{
	struct sh_vou_file *vou_file = priv;

	dev_dbg(vou_file->vbq.dev, "%s()\n", __func__);

	return videobuf_qbuf(&vou_file->vbq, b);
}

static int sh_vou_dqbuf(struct file *file, void *priv, struct v4l2_buffer *b)
{
	struct sh_vou_file *vou_file = priv;

	dev_dbg(vou_file->vbq.dev, "%s()\n", __func__);

	return videobuf_dqbuf(&vou_file->vbq, b, file->f_flags & O_NONBLOCK);
}

static int sh_vou_streamon(struct file *file, void *priv,
			   enum v4l2_buf_type buftype)
{
	struct video_device *vdev = video_devdata(file);
	struct sh_vou_device *vou_dev = video_get_drvdata(vdev);
	struct sh_vou_file *vou_file = priv;
	int ret;

	dev_dbg(vou_file->vbq.dev, "%s()\n", __func__);

	ret = v4l2_device_call_until_err(&vou_dev->v4l2_dev, 0,
					 video, s_stream, 1);
	if (ret < 0 && ret != -ENOIOCTLCMD)
		return ret;

	/* This calls our .buf_queue() (== sh_vou_buf_queue) */
	return videobuf_streamon(&vou_file->vbq);
}

static int sh_vou_streamoff(struct file *file, void *priv,
			    enum v4l2_buf_type buftype)
{
	struct video_device *vdev = video_devdata(file);
	struct sh_vou_device *vou_dev = video_get_drvdata(vdev);
	struct sh_vou_file *vou_file = priv;

	dev_dbg(vou_file->vbq.dev, "%s()\n", __func__);

	/*
	 * This calls buf_release from host driver's videobuf_queue_ops for all
	 * remaining buffers. When the last buffer is freed, stop streaming
	 */
	videobuf_streamoff(&vou_file->vbq);
	v4l2_device_call_until_err(&vou_dev->v4l2_dev, 0, video, s_stream, 0);

	return 0;
}

static u32 sh_vou_ntsc_mode(enum sh_vou_bus_fmt bus_fmt)
{
	switch (bus_fmt) {
	default:
		pr_warning("%s(): Invalid bus-format code %d, using default 8-bit\n",
			   __func__, bus_fmt);
	case SH_VOU_BUS_8BIT:
		return 1;
	case SH_VOU_BUS_16BIT:
		return 0;
	case SH_VOU_BUS_BT656:
		return 3;
	}
}

static int sh_vou_s_std(struct file *file, void *priv, v4l2_std_id *std_id)
{
	struct video_device *vdev = video_devdata(file);
	struct sh_vou_device *vou_dev = video_get_drvdata(vdev);
	int ret;

	dev_dbg(vou_dev->v4l2_dev.dev, "%s(): 0x%llx\n", __func__, *std_id);

	if (*std_id & ~vdev->tvnorms)
		return -EINVAL;

	ret = v4l2_device_call_until_err(&vou_dev->v4l2_dev, 0, video,
					 s_std_output, *std_id);
	/* Shall we continue, if the subdev doesn't support .s_std_output()? */
	if (ret < 0 && ret != -ENOIOCTLCMD)
		return ret;

	if (*std_id & V4L2_STD_525_60)
		sh_vou_reg_ab_set(vou_dev, VOUCR,
			sh_vou_ntsc_mode(vou_dev->pdata->bus_fmt) << 29, 7 << 29);
	else
		sh_vou_reg_ab_set(vou_dev, VOUCR, 5 << 29, 7 << 29);

	vou_dev->std = *std_id;

	return 0;
}

static int sh_vou_g_std(struct file *file, void *priv, v4l2_std_id *std)
{
	struct video_device *vdev = video_devdata(file);
	struct sh_vou_device *vou_dev = video_get_drvdata(vdev);

	dev_dbg(vou_dev->v4l2_dev.dev, "%s()\n", __func__);

	*std = vou_dev->std;

	return 0;
}

static int sh_vou_g_crop(struct file *file, void *fh, struct v4l2_crop *a)
{
	struct video_device *vdev = video_devdata(file);
	struct sh_vou_device *vou_dev = video_get_drvdata(vdev);

	dev_dbg(vou_dev->v4l2_dev.dev, "%s()\n", __func__);

	a->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	a->c = vou_dev->rect;

	return 0;
}

/* Assume a dull encoder, do all the work ourselves. */
static int sh_vou_s_crop(struct file *file, void *fh, struct v4l2_crop *a)
{
	struct video_device *vdev = video_devdata(file);
	struct sh_vou_device *vou_dev = video_get_drvdata(vdev);
	struct v4l2_rect *rect = &a->c;
	struct v4l2_crop sd_crop = {.type = V4L2_BUF_TYPE_VIDEO_OUTPUT};
	struct v4l2_pix_format *pix = &vou_dev->pix;
	struct sh_vou_geometry geo;
	struct v4l2_mbus_framefmt mbfmt = {
		/* Revisit: is this the correct code? */
		.code = V4L2_MBUS_FMT_YUYV8_2X8,
		.field = V4L2_FIELD_INTERLACED,
		.colorspace = V4L2_COLORSPACE_SMPTE170M,
	};
	unsigned int img_height_max;
	int ret;

	dev_dbg(vou_dev->v4l2_dev.dev, "%s(): %ux%u@%u:%u\n", __func__,
		rect->width, rect->height, rect->left, rect->top);

	if (a->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;

	if (vou_dev->std & V4L2_STD_525_60)
		img_height_max = 480;
	else
		img_height_max = 576;

	v4l_bound_align_image(&rect->width, 0, VOU_MAX_IMAGE_WIDTH, 1,
			      &rect->height, 0, img_height_max, 1, 0);

	if (rect->width + rect->left > VOU_MAX_IMAGE_WIDTH)
		rect->left = VOU_MAX_IMAGE_WIDTH - rect->width;

	if (rect->height + rect->top > img_height_max)
		rect->top = img_height_max - rect->height;

	geo.output = *rect;
	geo.in_width = pix->width;
	geo.in_height = pix->height;

	/* Configure the encoder one-to-one, position at 0, ignore errors */
	sd_crop.c.width = geo.output.width;
	sd_crop.c.height = geo.output.height;
	/*
	 * We first issue a S_CROP, so that the subsequent S_FMT delivers the
	 * final encoder configuration.
	 */
	v4l2_device_call_until_err(&vou_dev->v4l2_dev, 0, video,
				   s_crop, &sd_crop);
	mbfmt.width = geo.output.width;
	mbfmt.height = geo.output.height;
	ret = v4l2_device_call_until_err(&vou_dev->v4l2_dev, 0, video,
					 s_mbus_fmt, &mbfmt);
	/* Must be implemented, so, don't check for -ENOIOCTLCMD */
	if (ret < 0)
		return ret;

	/* Sanity checks */
	if ((unsigned)mbfmt.width > VOU_MAX_IMAGE_WIDTH ||
	    (unsigned)mbfmt.height > img_height_max ||
	    mbfmt.code != V4L2_MBUS_FMT_YUYV8_2X8)
		return -EIO;

	geo.output.width = mbfmt.width;
	geo.output.height = mbfmt.height;

	/*
	 * No down-scaling. According to the API, current call has precedence:
	 * http://v4l2spec.bytesex.org/spec/x1904.htm#AEN1954 paragraph two.
	 */
	vou_adjust_input(&geo, vou_dev->std);

	/* We tried to preserve output rectangle, but it could have changed */
	vou_dev->rect = geo.output;
	pix->width = geo.in_width;
	pix->height = geo.in_height;

	sh_vou_configure_geometry(vou_dev, vou_dev->pix_idx,
				  geo.scale_idx_h, geo.scale_idx_v);

	return 0;
}

/*
 * Total field: NTSC 858 x 2 * 262/263, PAL 864 x 2 * 312/313, default rectangle
 * is the initial register values, height takes the interlaced format into
 * account. The actual image can only go up to 720 x 2 * 240, So, VOUVPR can
 * actually only meaningfully contain values <= 720 and <= 240 respectively, and
 * not <= 864 and <= 312.
 */
static int sh_vou_cropcap(struct file *file, void *priv,
			  struct v4l2_cropcap *a)
{
	struct sh_vou_file *vou_file = priv;

	dev_dbg(vou_file->vbq.dev, "%s()\n", __func__);

	a->type				= V4L2_BUF_TYPE_VIDEO_OUTPUT;
	a->bounds.left			= 0;
	a->bounds.top			= 0;
	a->bounds.width			= VOU_MAX_IMAGE_WIDTH;
	a->bounds.height		= VOU_MAX_IMAGE_HEIGHT;
	/* Default = max, set VOUDPR = 0, which is not hardware default */
	a->defrect.left			= 0;
	a->defrect.top			= 0;
	a->defrect.width		= VOU_MAX_IMAGE_WIDTH;
	a->defrect.height		= VOU_MAX_IMAGE_HEIGHT;
	a->pixelaspect.numerator	= 1;
	a->pixelaspect.denominator	= 1;

	return 0;
}

static irqreturn_t sh_vou_isr(int irq, void *dev_id)
{
	struct sh_vou_device *vou_dev = dev_id;
	static unsigned long j;
	struct videobuf_buffer *vb;
	static int cnt;
	static int side;
	u32 irq_status = sh_vou_reg_a_read(vou_dev, VOUIR), masked;
	u32 vou_status = sh_vou_reg_a_read(vou_dev, VOUSTR);

	if (!(irq_status & 0x300)) {
		if (printk_timed_ratelimit(&j, 500))
			dev_warn(vou_dev->v4l2_dev.dev, "IRQ status 0x%x!\n",
				 irq_status);
		return IRQ_NONE;
	}

	spin_lock(&vou_dev->lock);
	if (!vou_dev->active || list_empty(&vou_dev->queue)) {
		if (printk_timed_ratelimit(&j, 500))
			dev_warn(vou_dev->v4l2_dev.dev,
				 "IRQ without active buffer: %x!\n", irq_status);
		/* Just ack: buf_release will disable further interrupts */
		sh_vou_reg_a_set(vou_dev, VOUIR, 0, 0x300);
		spin_unlock(&vou_dev->lock);
		return IRQ_HANDLED;
	}

	masked = ~(0x300 & irq_status) & irq_status & 0x30304;
	dev_dbg(vou_dev->v4l2_dev.dev,
		"IRQ status 0x%x -> 0x%x, VOU status 0x%x, cnt %d\n",
		irq_status, masked, vou_status, cnt);

	cnt++;
	side = vou_status & 0x10000;

	/* Clear only set interrupts */
	sh_vou_reg_a_write(vou_dev, VOUIR, masked);

	vb = vou_dev->active;
	list_del(&vb->queue);

	vb->state = VIDEOBUF_DONE;
	do_gettimeofday(&vb->ts);
	vb->field_count++;
	wake_up(&vb->done);

	if (list_empty(&vou_dev->queue)) {
		/* Stop VOU */
		dev_dbg(vou_dev->v4l2_dev.dev, "%s: queue empty after %d\n",
			__func__, cnt);
		sh_vou_reg_a_set(vou_dev, VOUER, 0, 1);
		vou_dev->active = NULL;
		vou_dev->status = SH_VOU_INITIALISING;
		/* Disable End-of-Frame (VSYNC) interrupts */
		sh_vou_reg_a_set(vou_dev, VOUIR, 0, 0x30000);
		spin_unlock(&vou_dev->lock);
		return IRQ_HANDLED;
	}

	vou_dev->active = list_entry(vou_dev->queue.next,
				     struct videobuf_buffer, queue);

	if (vou_dev->active->queue.next != &vou_dev->queue) {
		struct videobuf_buffer *new = list_entry(vou_dev->active->queue.next,
						struct videobuf_buffer, queue);
		sh_vou_schedule_next(vou_dev, new);
	}

	spin_unlock(&vou_dev->lock);

	return IRQ_HANDLED;
}

static int sh_vou_hw_init(struct sh_vou_device *vou_dev)
{
	struct sh_vou_pdata *pdata = vou_dev->pdata;
	u32 voucr = sh_vou_ntsc_mode(pdata->bus_fmt) << 29;
	int i = 100;

	/* Disable all IRQs */
	sh_vou_reg_a_write(vou_dev, VOUIR, 0);

	/* Reset VOU interfaces - registers unaffected */
	sh_vou_reg_a_write(vou_dev, VOUSRR, 0x101);
	while (--i && (sh_vou_reg_a_read(vou_dev, VOUSRR) & 0x101))
		udelay(1);

	if (!i)
		return -ETIMEDOUT;

	dev_dbg(vou_dev->v4l2_dev.dev, "Reset took %dus\n", 100 - i);

	if (pdata->flags & SH_VOU_PCLK_FALLING)
		voucr |= 1 << 28;
	if (pdata->flags & SH_VOU_HSYNC_LOW)
		voucr |= 1 << 27;
	if (pdata->flags & SH_VOU_VSYNC_LOW)
		voucr |= 1 << 26;
	sh_vou_reg_ab_set(vou_dev, VOUCR, voucr, 0xfc000000);

	/* Manual register side switching at first */
	sh_vou_reg_a_write(vou_dev, VOURCR, 4);
	/* Default - fixed HSYNC length, can be made configurable is required */
	sh_vou_reg_ab_write(vou_dev, VOUMSR, 0x800000);

	return 0;
}

/* File operations */
static int sh_vou_open(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct sh_vou_device *vou_dev = video_get_drvdata(vdev);
	struct sh_vou_file *vou_file = kzalloc(sizeof(struct sh_vou_file),
					       GFP_KERNEL);

	if (!vou_file)
		return -ENOMEM;

	dev_dbg(vou_dev->v4l2_dev.dev, "%s()\n", __func__);

	file->private_data = vou_file;

	if (atomic_inc_return(&vou_dev->use_count) == 1) {
		int ret;
		/* First open */
		vou_dev->status = SH_VOU_INITIALISING;
		pm_runtime_get_sync(vdev->v4l2_dev->dev);
		ret = sh_vou_hw_init(vou_dev);
		if (ret < 0) {
			atomic_dec(&vou_dev->use_count);
			pm_runtime_put(vdev->v4l2_dev->dev);
			vou_dev->status = SH_VOU_IDLE;
			return ret;
		}
	}

	videobuf_queue_dma_contig_init(&vou_file->vbq, &sh_vou_video_qops,
				       vou_dev->v4l2_dev.dev, &vou_dev->lock,
				       V4L2_BUF_TYPE_VIDEO_OUTPUT,
				       V4L2_FIELD_NONE,
				       sizeof(struct videobuf_buffer), vdev,
				       &vou_dev->fop_lock);

	return 0;
}

static int sh_vou_release(struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct sh_vou_device *vou_dev = video_get_drvdata(vdev);
	struct sh_vou_file *vou_file = file->private_data;

	dev_dbg(vou_file->vbq.dev, "%s()\n", __func__);

	if (!atomic_dec_return(&vou_dev->use_count)) {
		/* Last close */
		vou_dev->status = SH_VOU_IDLE;
		sh_vou_reg_a_set(vou_dev, VOUER, 0, 0x101);
		pm_runtime_put(vdev->v4l2_dev->dev);
	}

	file->private_data = NULL;
	kfree(vou_file);

	return 0;
}

static int sh_vou_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct sh_vou_file *vou_file = file->private_data;

	dev_dbg(vou_file->vbq.dev, "%s()\n", __func__);

	return videobuf_mmap_mapper(&vou_file->vbq, vma);
}

static unsigned int sh_vou_poll(struct file *file, poll_table *wait)
{
	struct sh_vou_file *vou_file = file->private_data;

	dev_dbg(vou_file->vbq.dev, "%s()\n", __func__);

	return videobuf_poll_stream(file, &vou_file->vbq, wait);
}

static int sh_vou_g_chip_ident(struct file *file, void *fh,
				   struct v4l2_dbg_chip_ident *id)
{
	struct video_device *vdev = video_devdata(file);
	struct sh_vou_device *vou_dev = video_get_drvdata(vdev);

	return v4l2_device_call_until_err(&vou_dev->v4l2_dev, 0, core, g_chip_ident, id);
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int sh_vou_g_register(struct file *file, void *fh,
				 struct v4l2_dbg_register *reg)
{
	struct video_device *vdev = video_devdata(file);
	struct sh_vou_device *vou_dev = video_get_drvdata(vdev);

	return v4l2_device_call_until_err(&vou_dev->v4l2_dev, 0, core, g_register, reg);
}

static int sh_vou_s_register(struct file *file, void *fh,
				 struct v4l2_dbg_register *reg)
{
	struct video_device *vdev = video_devdata(file);
	struct sh_vou_device *vou_dev = video_get_drvdata(vdev);

	return v4l2_device_call_until_err(&vou_dev->v4l2_dev, 0, core, s_register, reg);
}
#endif

/* sh_vou display ioctl operations */
static const struct v4l2_ioctl_ops sh_vou_ioctl_ops = {
	.vidioc_querycap        	= sh_vou_querycap,
	.vidioc_enum_fmt_vid_out	= sh_vou_enum_fmt_vid_out,
	.vidioc_g_fmt_vid_out		= sh_vou_g_fmt_vid_out,
	.vidioc_s_fmt_vid_out		= sh_vou_s_fmt_vid_out,
	.vidioc_try_fmt_vid_out		= sh_vou_try_fmt_vid_out,
	.vidioc_reqbufs			= sh_vou_reqbufs,
	.vidioc_querybuf		= sh_vou_querybuf,
	.vidioc_qbuf			= sh_vou_qbuf,
	.vidioc_dqbuf			= sh_vou_dqbuf,
	.vidioc_streamon		= sh_vou_streamon,
	.vidioc_streamoff		= sh_vou_streamoff,
	.vidioc_s_std			= sh_vou_s_std,
	.vidioc_g_std			= sh_vou_g_std,
	.vidioc_cropcap			= sh_vou_cropcap,
	.vidioc_g_crop			= sh_vou_g_crop,
	.vidioc_s_crop			= sh_vou_s_crop,
	.vidioc_g_chip_ident		= sh_vou_g_chip_ident,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.vidioc_g_register		= sh_vou_g_register,
	.vidioc_s_register		= sh_vou_s_register,
#endif
};

static const struct v4l2_file_operations sh_vou_fops = {
	.owner		= THIS_MODULE,
	.open		= sh_vou_open,
	.release	= sh_vou_release,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= sh_vou_mmap,
	.poll		= sh_vou_poll,
};

static const struct video_device sh_vou_video_template = {
	.name		= "sh_vou",
	.fops		= &sh_vou_fops,
	.ioctl_ops	= &sh_vou_ioctl_ops,
	.tvnorms	= V4L2_STD_525_60, /* PAL only supported in 8-bit non-bt656 mode */
	.current_norm	= V4L2_STD_NTSC_M,
};

static int __devinit sh_vou_probe(struct platform_device *pdev)
{
	struct sh_vou_pdata *vou_pdata = pdev->dev.platform_data;
	struct v4l2_rect *rect;
	struct v4l2_pix_format *pix;
	struct i2c_adapter *i2c_adap;
	struct video_device *vdev;
	struct sh_vou_device *vou_dev;
	struct resource *reg_res, *region;
	struct v4l2_subdev *subdev;
	int irq, ret;

	reg_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);

	if (!vou_pdata || !reg_res || irq <= 0) {
		dev_err(&pdev->dev, "Insufficient VOU platform information.\n");
		return -ENODEV;
	}

	vou_dev = kzalloc(sizeof(*vou_dev), GFP_KERNEL);
	if (!vou_dev)
		return -ENOMEM;

	INIT_LIST_HEAD(&vou_dev->queue);
	spin_lock_init(&vou_dev->lock);
	mutex_init(&vou_dev->fop_lock);
	atomic_set(&vou_dev->use_count, 0);
	vou_dev->pdata = vou_pdata;
	vou_dev->status = SH_VOU_IDLE;

	rect = &vou_dev->rect;
	pix = &vou_dev->pix;

	/* Fill in defaults */
	vou_dev->std		= sh_vou_video_template.current_norm;
	rect->left		= 0;
	rect->top		= 0;
	rect->width		= VOU_MAX_IMAGE_WIDTH;
	rect->height		= 480;
	pix->width		= VOU_MAX_IMAGE_WIDTH;
	pix->height		= 480;
	pix->pixelformat	= V4L2_PIX_FMT_YVYU;
	pix->field		= V4L2_FIELD_NONE;
	pix->bytesperline	= VOU_MAX_IMAGE_WIDTH * 2;
	pix->sizeimage		= VOU_MAX_IMAGE_WIDTH * 2 * 480;
	pix->colorspace		= V4L2_COLORSPACE_SMPTE170M;

	region = request_mem_region(reg_res->start, resource_size(reg_res),
				    pdev->name);
	if (!region) {
		dev_err(&pdev->dev, "VOU region already claimed\n");
		ret = -EBUSY;
		goto ereqmemreg;
	}

	vou_dev->base = ioremap(reg_res->start, resource_size(reg_res));
	if (!vou_dev->base) {
		ret = -ENOMEM;
		goto emap;
	}

	ret = request_irq(irq, sh_vou_isr, 0, "vou", vou_dev);
	if (ret < 0)
		goto ereqirq;

	ret = v4l2_device_register(&pdev->dev, &vou_dev->v4l2_dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Error registering v4l2 device\n");
		goto ev4l2devreg;
	}

	/* Allocate memory for video device */
	vdev = video_device_alloc();
	if (vdev == NULL) {
		ret = -ENOMEM;
		goto evdevalloc;
	}

	*vdev = sh_vou_video_template;
	if (vou_pdata->bus_fmt == SH_VOU_BUS_8BIT)
		vdev->tvnorms |= V4L2_STD_PAL;
	vdev->v4l2_dev = &vou_dev->v4l2_dev;
	vdev->release = video_device_release;
	vdev->lock = &vou_dev->fop_lock;
	/* Locking in file operations other than ioctl should be done
	   by the driver, not the V4L2 core.
	   This driver needs auditing so that this flag can be removed. */
	set_bit(V4L2_FL_LOCK_ALL_FOPS, &vdev->flags);

	vou_dev->vdev = vdev;
	video_set_drvdata(vdev, vou_dev);

	pm_runtime_enable(&pdev->dev);
	pm_runtime_resume(&pdev->dev);

	i2c_adap = i2c_get_adapter(vou_pdata->i2c_adap);
	if (!i2c_adap) {
		ret = -ENODEV;
		goto ei2cgadap;
	}

	ret = sh_vou_hw_init(vou_dev);
	if (ret < 0)
		goto ereset;

	subdev = v4l2_i2c_new_subdev_board(&vou_dev->v4l2_dev, i2c_adap,
			vou_pdata->board_info, NULL);
	if (!subdev) {
		ret = -ENOMEM;
		goto ei2cnd;
	}

	ret = video_register_device(vdev, VFL_TYPE_GRABBER, -1);
	if (ret < 0)
		goto evregdev;

	return 0;

evregdev:
ei2cnd:
ereset:
	i2c_put_adapter(i2c_adap);
ei2cgadap:
	video_device_release(vdev);
	pm_runtime_disable(&pdev->dev);
evdevalloc:
	v4l2_device_unregister(&vou_dev->v4l2_dev);
ev4l2devreg:
	free_irq(irq, vou_dev);
ereqirq:
	iounmap(vou_dev->base);
emap:
	release_mem_region(reg_res->start, resource_size(reg_res));
ereqmemreg:
	kfree(vou_dev);
	return ret;
}

static int __devexit sh_vou_remove(struct platform_device *pdev)
{
	int irq = platform_get_irq(pdev, 0);
	struct v4l2_device *v4l2_dev = platform_get_drvdata(pdev);
	struct sh_vou_device *vou_dev = container_of(v4l2_dev,
						struct sh_vou_device, v4l2_dev);
	struct v4l2_subdev *sd = list_entry(v4l2_dev->subdevs.next,
					    struct v4l2_subdev, list);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct resource *reg_res;

	if (irq > 0)
		free_irq(irq, vou_dev);
	pm_runtime_disable(&pdev->dev);
	video_unregister_device(vou_dev->vdev);
	i2c_put_adapter(client->adapter);
	v4l2_device_unregister(&vou_dev->v4l2_dev);
	iounmap(vou_dev->base);
	reg_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (reg_res)
		release_mem_region(reg_res->start, resource_size(reg_res));
	kfree(vou_dev);
	return 0;
}

static struct platform_driver __refdata sh_vou = {
	.remove  = __devexit_p(sh_vou_remove),
	.driver  = {
		.name	= "sh-vou",
		.owner	= THIS_MODULE,
	},
};

static int __init sh_vou_init(void)
{
	return platform_driver_probe(&sh_vou, sh_vou_probe);
}

static void __exit sh_vou_exit(void)
{
	platform_driver_unregister(&sh_vou);
}

module_init(sh_vou_init);
module_exit(sh_vou_exit);

MODULE_DESCRIPTION("SuperH VOU driver");
MODULE_AUTHOR("Guennadi Liakhovetski <g.liakhovetski@gmx.de>");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("0.1.0");
MODULE_ALIAS("platform:sh-vou");
