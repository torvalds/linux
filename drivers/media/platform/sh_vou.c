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
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>

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

#define VOU_MIN_IMAGE_WIDTH	16
#define VOU_MAX_IMAGE_WIDTH	720
#define VOU_MIN_IMAGE_HEIGHT	16

struct sh_vou_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
};

static inline struct
sh_vou_buffer *to_sh_vou_buffer(struct vb2_v4l2_buffer *vb2)
{
	return container_of(vb2, struct sh_vou_buffer, vb);
}

struct sh_vou_device {
	struct v4l2_device v4l2_dev;
	struct video_device vdev;
	struct sh_vou_pdata *pdata;
	spinlock_t lock;
	void __iomem *base;
	/* State information */
	struct v4l2_pix_format pix;
	struct v4l2_rect rect;
	struct list_head buf_list;
	v4l2_std_id std;
	int pix_idx;
	struct vb2_queue queue;
	struct vb2_alloc_ctx *alloc_ctx;
	struct sh_vou_buffer *active;
	enum sh_vou_status status;
	unsigned sequence;
	struct mutex fop_lock;
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
	unsigned char	bpl;
	unsigned char	rgb;
	unsigned char	yf;
	unsigned char	pkf;
};

/* Further pixel formats can be added */
static struct sh_vou_fmt vou_fmt[] = {
	{
		.pfmt	= V4L2_PIX_FMT_NV12,
		.bpp	= 12,
		.bpl	= 1,
		.desc	= "YVU420 planar",
		.yf	= 0,
		.rgb	= 0,
	},
	{
		.pfmt	= V4L2_PIX_FMT_NV16,
		.bpp	= 16,
		.bpl	= 1,
		.desc	= "YVYU planar",
		.yf	= 1,
		.rgb	= 0,
	},
	{
		.pfmt	= V4L2_PIX_FMT_RGB24,
		.bpp	= 24,
		.bpl	= 3,
		.desc	= "RGB24",
		.pkf	= 2,
		.rgb	= 1,
	},
	{
		.pfmt	= V4L2_PIX_FMT_RGB565,
		.bpp	= 16,
		.bpl	= 2,
		.desc	= "RGB565",
		.pkf	= 3,
		.rgb	= 1,
	},
	{
		.pfmt	= V4L2_PIX_FMT_RGB565X,
		.bpp	= 16,
		.bpl	= 2,
		.desc	= "RGB565 byteswapped",
		.pkf	= 3,
		.rgb	= 1,
	},
};

static void sh_vou_schedule_next(struct sh_vou_device *vou_dev,
				 struct vb2_v4l2_buffer *vbuf)
{
	dma_addr_t addr1, addr2;

	addr1 = vb2_dma_contig_plane_dma_addr(&vbuf->vb2_buf, 0);
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

static void sh_vou_stream_config(struct sh_vou_device *vou_dev)
{
	unsigned int row_coeff;
#ifdef __LITTLE_ENDIAN
	u32 dataswap = 7;
#else
	u32 dataswap = 0;
#endif

	switch (vou_dev->pix.pixelformat) {
	default:
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
}

/* Locking: caller holds fop_lock mutex */
static int sh_vou_queue_setup(struct vb2_queue *vq, const void *parg,
		       unsigned int *nbuffers, unsigned int *nplanes,
		       unsigned int sizes[], void *alloc_ctxs[])
{
	const struct v4l2_format *fmt = parg;
	struct sh_vou_device *vou_dev = vb2_get_drv_priv(vq);
	struct v4l2_pix_format *pix = &vou_dev->pix;
	int bytes_per_line = vou_fmt[vou_dev->pix_idx].bpp * pix->width / 8;

	dev_dbg(vou_dev->v4l2_dev.dev, "%s()\n", __func__);

	if (fmt && fmt->fmt.pix.sizeimage < pix->height * bytes_per_line)
		return -EINVAL;
	*nplanes = 1;
	sizes[0] = fmt ? fmt->fmt.pix.sizeimage : pix->height * bytes_per_line;
	alloc_ctxs[0] = vou_dev->alloc_ctx;
	return 0;
}

static int sh_vou_buf_prepare(struct vb2_buffer *vb)
{
	struct sh_vou_device *vou_dev = vb2_get_drv_priv(vb->vb2_queue);
	struct v4l2_pix_format *pix = &vou_dev->pix;
	unsigned bytes_per_line = vou_fmt[vou_dev->pix_idx].bpp * pix->width / 8;
	unsigned size = pix->height * bytes_per_line;

	dev_dbg(vou_dev->v4l2_dev.dev, "%s()\n", __func__);

	if (vb2_plane_size(vb, 0) < size) {
		/* User buffer too small */
		dev_warn(vou_dev->v4l2_dev.dev, "buffer too small (%lu < %u)\n",
			 vb2_plane_size(vb, 0), size);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, size);
	return 0;
}

/* Locking: caller holds fop_lock mutex and vq->irqlock spinlock */
static void sh_vou_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct sh_vou_device *vou_dev = vb2_get_drv_priv(vb->vb2_queue);
	struct sh_vou_buffer *shbuf = to_sh_vou_buffer(vbuf);
	unsigned long flags;

	spin_lock_irqsave(&vou_dev->lock, flags);
	list_add_tail(&shbuf->list, &vou_dev->buf_list);
	spin_unlock_irqrestore(&vou_dev->lock, flags);
}

static int sh_vou_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct sh_vou_device *vou_dev = vb2_get_drv_priv(vq);
	struct sh_vou_buffer *buf, *node;
	int ret;

	vou_dev->sequence = 0;
	ret = v4l2_device_call_until_err(&vou_dev->v4l2_dev, 0,
					 video, s_stream, 1);
	if (ret < 0 && ret != -ENOIOCTLCMD) {
		list_for_each_entry_safe(buf, node, &vou_dev->buf_list, list) {
			vb2_buffer_done(&buf->vb.vb2_buf,
					VB2_BUF_STATE_QUEUED);
			list_del(&buf->list);
		}
		vou_dev->active = NULL;
		return ret;
	}

	buf = list_entry(vou_dev->buf_list.next, struct sh_vou_buffer, list);

	vou_dev->active = buf;

	/* Start from side A: we use mirror addresses, so, set B */
	sh_vou_reg_a_write(vou_dev, VOURPR, 1);
	dev_dbg(vou_dev->v4l2_dev.dev, "%s: first buffer status 0x%x\n",
		__func__, sh_vou_reg_a_read(vou_dev, VOUSTR));
	sh_vou_schedule_next(vou_dev, &buf->vb);

	buf = list_entry(buf->list.next, struct sh_vou_buffer, list);

	/* Second buffer - initialise register side B */
	sh_vou_reg_a_write(vou_dev, VOURPR, 0);
	sh_vou_schedule_next(vou_dev, &buf->vb);

	/* Register side switching with frame VSYNC */
	sh_vou_reg_a_write(vou_dev, VOURCR, 5);

	sh_vou_stream_config(vou_dev);
	/* Enable End-of-Frame (VSYNC) interrupts */
	sh_vou_reg_a_write(vou_dev, VOUIR, 0x10004);

	/* Two buffers on the queue - activate the hardware */
	vou_dev->status = SH_VOU_RUNNING;
	sh_vou_reg_a_write(vou_dev, VOUER, 0x107);
	return 0;
}

static void sh_vou_stop_streaming(struct vb2_queue *vq)
{
	struct sh_vou_device *vou_dev = vb2_get_drv_priv(vq);
	struct sh_vou_buffer *buf, *node;
	unsigned long flags;

	v4l2_device_call_until_err(&vou_dev->v4l2_dev, 0,
					 video, s_stream, 0);
	/* disable output */
	sh_vou_reg_a_set(vou_dev, VOUER, 0, 1);
	/* ...but the current frame will complete */
	sh_vou_reg_a_set(vou_dev, VOUIR, 0, 0x30000);
	msleep(50);
	spin_lock_irqsave(&vou_dev->lock, flags);
	list_for_each_entry_safe(buf, node, &vou_dev->buf_list, list) {
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		list_del(&buf->list);
	}
	vou_dev->active = NULL;
	spin_unlock_irqrestore(&vou_dev->lock, flags);
}

static struct vb2_ops sh_vou_qops = {
	.queue_setup		= sh_vou_queue_setup,
	.buf_prepare		= sh_vou_buf_prepare,
	.buf_queue		= sh_vou_buf_queue,
	.start_streaming	= sh_vou_start_streaming,
	.stop_streaming		= sh_vou_stop_streaming,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
};

/* Video IOCTLs */
static int sh_vou_querycap(struct file *file, void  *priv,
			   struct v4l2_capability *cap)
{
	struct sh_vou_device *vou_dev = video_drvdata(file);

	dev_dbg(vou_dev->v4l2_dev.dev, "%s()\n", __func__);

	strlcpy(cap->card, "SuperH VOU", sizeof(cap->card));
	strlcpy(cap->driver, "sh-vou", sizeof(cap->driver));
	strlcpy(cap->bus_info, "platform:sh-vou", sizeof(cap->bus_info));
	cap->device_caps = V4L2_CAP_VIDEO_OUTPUT | V4L2_CAP_READWRITE |
			   V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

/* Enumerate formats, that the device can accept from the user */
static int sh_vou_enum_fmt_vid_out(struct file *file, void  *priv,
				   struct v4l2_fmtdesc *fmt)
{
	struct sh_vou_device *vou_dev = video_drvdata(file);

	if (fmt->index >= ARRAY_SIZE(vou_fmt))
		return -EINVAL;

	dev_dbg(vou_dev->v4l2_dev.dev, "%s()\n", __func__);

	fmt->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	strlcpy(fmt->description, vou_fmt[fmt->index].desc,
		sizeof(fmt->description));
	fmt->pixelformat = vou_fmt[fmt->index].pfmt;

	return 0;
}

static int sh_vou_g_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *fmt)
{
	struct sh_vou_device *vou_dev = video_drvdata(file);

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
	unsigned int black_left, black_top, width_max,
		frame_in_height, frame_out_height, frame_out_top;
	struct v4l2_rect *rect = &vou_dev->rect;
	struct v4l2_pix_format *pix = &vou_dev->pix;
	u32 vouvcr = 0, dsr_h, dsr_v;

	if (vou_dev->std & V4L2_STD_525_60) {
		width_max = 858;
		/* height_max = 262; */
	} else {
		width_max = 864;
		/* height_max = 312; */
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
	v4l_bound_align_image(&geo->in_width,
			      VOU_MIN_IMAGE_WIDTH, VOU_MAX_IMAGE_WIDTH, 2,
			      &geo->in_height,
			      VOU_MIN_IMAGE_HEIGHT, img_height_max, 1, 0);

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
	unsigned int best_err = UINT_MAX, best = geo->in_width,
		width_max, height_max, img_height_max;
	int i, idx_h = 0, idx_v = 0;

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
			idx_h = i;
			best = found;
		}
		if (!err)
			break;
	}

	geo->output.width = best;
	geo->scale_idx_h = idx_h;
	if (geo->output.left + best > width_max)
		geo->output.left = width_max - best;

	pr_debug("%s(): W %u * %u/%u = %u\n", __func__, geo->in_width,
		 vou_scale_h_num[idx_h], vou_scale_h_den[idx_h], best);

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
			idx_v = i;
			best = found;
		}
		if (!err)
			break;
	}

	geo->output.height = best;
	geo->scale_idx_v = idx_v;
	if (geo->output.top + best > height_max)
		geo->output.top = height_max - best;

	pr_debug("%s(): H %u * %u/%u = %u\n", __func__, geo->in_height,
		 vou_scale_v_num[idx_v], vou_scale_v_den[idx_v], best);
}

static int sh_vou_try_fmt_vid_out(struct file *file, void *priv,
				  struct v4l2_format *fmt)
{
	struct sh_vou_device *vou_dev = video_drvdata(file);
	struct v4l2_pix_format *pix = &fmt->fmt.pix;
	unsigned int img_height_max;
	int pix_idx;

	dev_dbg(vou_dev->v4l2_dev.dev, "%s()\n", __func__);

	pix->field = V4L2_FIELD_INTERLACED;
	pix->colorspace = V4L2_COLORSPACE_SMPTE170M;
	pix->ycbcr_enc = pix->quantization = 0;

	for (pix_idx = 0; pix_idx < ARRAY_SIZE(vou_fmt); pix_idx++)
		if (vou_fmt[pix_idx].pfmt == pix->pixelformat)
			break;

	if (pix_idx == ARRAY_SIZE(vou_fmt))
		return -EINVAL;

	if (vou_dev->std & V4L2_STD_525_60)
		img_height_max = 480;
	else
		img_height_max = 576;

	v4l_bound_align_image(&pix->width,
			      VOU_MIN_IMAGE_WIDTH, VOU_MAX_IMAGE_WIDTH, 2,
			      &pix->height,
			      VOU_MIN_IMAGE_HEIGHT, img_height_max, 1, 0);
	pix->bytesperline = pix->width * vou_fmt[pix_idx].bpl;
	pix->sizeimage = pix->height * ((pix->width * vou_fmt[pix_idx].bpp) >> 3);

	return 0;
}

static int sh_vou_set_fmt_vid_out(struct sh_vou_device *vou_dev,
				struct v4l2_pix_format *pix)
{
	unsigned int img_height_max;
	struct sh_vou_geometry geo;
	struct v4l2_subdev_format format = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		/* Revisit: is this the correct code? */
		.format.code = MEDIA_BUS_FMT_YUYV8_2X8,
		.format.field = V4L2_FIELD_INTERLACED,
		.format.colorspace = V4L2_COLORSPACE_SMPTE170M,
	};
	struct v4l2_mbus_framefmt *mbfmt = &format.format;
	int pix_idx;
	int ret;

	if (vb2_is_busy(&vou_dev->queue))
		return -EBUSY;

	for (pix_idx = 0; pix_idx < ARRAY_SIZE(vou_fmt); pix_idx++)
		if (vou_fmt[pix_idx].pfmt == pix->pixelformat)
			break;

	geo.in_width = pix->width;
	geo.in_height = pix->height;
	geo.output = vou_dev->rect;

	vou_adjust_output(&geo, vou_dev->std);

	mbfmt->width = geo.output.width;
	mbfmt->height = geo.output.height;
	ret = v4l2_device_call_until_err(&vou_dev->v4l2_dev, 0, pad,
					 set_fmt, NULL, &format);
	/* Must be implemented, so, don't check for -ENOIOCTLCMD */
	if (ret < 0)
		return ret;

	dev_dbg(vou_dev->v4l2_dev.dev, "%s(): %ux%u -> %ux%u\n", __func__,
		geo.output.width, geo.output.height, mbfmt->width, mbfmt->height);

	if (vou_dev->std & V4L2_STD_525_60)
		img_height_max = 480;
	else
		img_height_max = 576;

	/* Sanity checks */
	if ((unsigned)mbfmt->width > VOU_MAX_IMAGE_WIDTH ||
	    (unsigned)mbfmt->height > img_height_max ||
	    mbfmt->code != MEDIA_BUS_FMT_YUYV8_2X8)
		return -EIO;

	if (mbfmt->width != geo.output.width ||
	    mbfmt->height != geo.output.height) {
		geo.output.width = mbfmt->width;
		geo.output.height = mbfmt->height;

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

static int sh_vou_s_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *fmt)
{
	struct sh_vou_device *vou_dev = video_drvdata(file);
	int ret = sh_vou_try_fmt_vid_out(file, priv, fmt);

	if (ret)
		return ret;
	return sh_vou_set_fmt_vid_out(vou_dev, &fmt->fmt.pix);
}

static int sh_vou_enum_output(struct file *file, void *fh,
			      struct v4l2_output *a)
{
	struct sh_vou_device *vou_dev = video_drvdata(file);

	if (a->index)
		return -EINVAL;
	strlcpy(a->name, "Video Out", sizeof(a->name));
	a->type = V4L2_OUTPUT_TYPE_ANALOG;
	a->std = vou_dev->vdev.tvnorms;
	return 0;
}

static int sh_vou_g_output(struct file *file, void *fh, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int sh_vou_s_output(struct file *file, void *fh, unsigned int i)
{
	return i ? -EINVAL : 0;
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

static int sh_vou_s_std(struct file *file, void *priv, v4l2_std_id std_id)
{
	struct sh_vou_device *vou_dev = video_drvdata(file);
	int ret;

	dev_dbg(vou_dev->v4l2_dev.dev, "%s(): 0x%llx\n", __func__, std_id);

	if (std_id == vou_dev->std)
		return 0;

	if (vb2_is_busy(&vou_dev->queue))
		return -EBUSY;

	ret = v4l2_device_call_until_err(&vou_dev->v4l2_dev, 0, video,
					 s_std_output, std_id);
	/* Shall we continue, if the subdev doesn't support .s_std_output()? */
	if (ret < 0 && ret != -ENOIOCTLCMD)
		return ret;

	vou_dev->rect.top = vou_dev->rect.left = 0;
	vou_dev->rect.width = VOU_MAX_IMAGE_WIDTH;
	if (std_id & V4L2_STD_525_60) {
		sh_vou_reg_ab_set(vou_dev, VOUCR,
			sh_vou_ntsc_mode(vou_dev->pdata->bus_fmt) << 29, 7 << 29);
		vou_dev->rect.height = 480;
	} else {
		sh_vou_reg_ab_set(vou_dev, VOUCR, 5 << 29, 7 << 29);
		vou_dev->rect.height = 576;
	}

	vou_dev->pix.width = vou_dev->rect.width;
	vou_dev->pix.height = vou_dev->rect.height;
	vou_dev->pix.bytesperline =
		vou_dev->pix.width * vou_fmt[vou_dev->pix_idx].bpl;
	vou_dev->pix.sizeimage = vou_dev->pix.height *
		((vou_dev->pix.width * vou_fmt[vou_dev->pix_idx].bpp) >> 3);
	vou_dev->std = std_id;
	sh_vou_set_fmt_vid_out(vou_dev, &vou_dev->pix);

	return 0;
}

static int sh_vou_g_std(struct file *file, void *priv, v4l2_std_id *std)
{
	struct sh_vou_device *vou_dev = video_drvdata(file);

	dev_dbg(vou_dev->v4l2_dev.dev, "%s()\n", __func__);

	*std = vou_dev->std;

	return 0;
}

static int sh_vou_log_status(struct file *file, void *priv)
{
	struct sh_vou_device *vou_dev = video_drvdata(file);

	pr_info("VOUER:   0x%08x\n", sh_vou_reg_a_read(vou_dev, VOUER));
	pr_info("VOUCR:   0x%08x\n", sh_vou_reg_a_read(vou_dev, VOUCR));
	pr_info("VOUSTR:  0x%08x\n", sh_vou_reg_a_read(vou_dev, VOUSTR));
	pr_info("VOUVCR:  0x%08x\n", sh_vou_reg_a_read(vou_dev, VOUVCR));
	pr_info("VOUISR:  0x%08x\n", sh_vou_reg_a_read(vou_dev, VOUISR));
	pr_info("VOUBCR:  0x%08x\n", sh_vou_reg_a_read(vou_dev, VOUBCR));
	pr_info("VOUDPR:  0x%08x\n", sh_vou_reg_a_read(vou_dev, VOUDPR));
	pr_info("VOUDSR:  0x%08x\n", sh_vou_reg_a_read(vou_dev, VOUDSR));
	pr_info("VOUVPR:  0x%08x\n", sh_vou_reg_a_read(vou_dev, VOUVPR));
	pr_info("VOUIR:   0x%08x\n", sh_vou_reg_a_read(vou_dev, VOUIR));
	pr_info("VOUSRR:  0x%08x\n", sh_vou_reg_a_read(vou_dev, VOUSRR));
	pr_info("VOUMSR:  0x%08x\n", sh_vou_reg_a_read(vou_dev, VOUMSR));
	pr_info("VOUHIR:  0x%08x\n", sh_vou_reg_a_read(vou_dev, VOUHIR));
	pr_info("VOUDFR:  0x%08x\n", sh_vou_reg_a_read(vou_dev, VOUDFR));
	pr_info("VOUAD1R: 0x%08x\n", sh_vou_reg_a_read(vou_dev, VOUAD1R));
	pr_info("VOUAD2R: 0x%08x\n", sh_vou_reg_a_read(vou_dev, VOUAD2R));
	pr_info("VOUAIR:  0x%08x\n", sh_vou_reg_a_read(vou_dev, VOUAIR));
	pr_info("VOUSWR:  0x%08x\n", sh_vou_reg_a_read(vou_dev, VOUSWR));
	pr_info("VOURCR:  0x%08x\n", sh_vou_reg_a_read(vou_dev, VOURCR));
	pr_info("VOURPR:  0x%08x\n", sh_vou_reg_a_read(vou_dev, VOURPR));
	return 0;
}

static int sh_vou_g_selection(struct file *file, void *fh,
			      struct v4l2_selection *sel)
{
	struct sh_vou_device *vou_dev = video_drvdata(file);

	if (sel->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;
	switch (sel->target) {
	case V4L2_SEL_TGT_COMPOSE:
		sel->r = vou_dev->rect;
		break;
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = VOU_MAX_IMAGE_WIDTH;
		if (vou_dev->std & V4L2_STD_525_60)
			sel->r.height = 480;
		else
			sel->r.height = 576;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/* Assume a dull encoder, do all the work ourselves. */
static int sh_vou_s_selection(struct file *file, void *fh,
			      struct v4l2_selection *sel)
{
	struct v4l2_rect *rect = &sel->r;
	struct sh_vou_device *vou_dev = video_drvdata(file);
	struct v4l2_crop sd_crop = {.type = V4L2_BUF_TYPE_VIDEO_OUTPUT};
	struct v4l2_pix_format *pix = &vou_dev->pix;
	struct sh_vou_geometry geo;
	struct v4l2_subdev_format format = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		/* Revisit: is this the correct code? */
		.format.code = MEDIA_BUS_FMT_YUYV8_2X8,
		.format.field = V4L2_FIELD_INTERLACED,
		.format.colorspace = V4L2_COLORSPACE_SMPTE170M,
	};
	unsigned int img_height_max;
	int ret;

	if (sel->type != V4L2_BUF_TYPE_VIDEO_OUTPUT ||
	    sel->target != V4L2_SEL_TGT_COMPOSE)
		return -EINVAL;

	if (vb2_is_busy(&vou_dev->queue))
		return -EBUSY;

	if (vou_dev->std & V4L2_STD_525_60)
		img_height_max = 480;
	else
		img_height_max = 576;

	v4l_bound_align_image(&rect->width,
			      VOU_MIN_IMAGE_WIDTH, VOU_MAX_IMAGE_WIDTH, 1,
			      &rect->height,
			      VOU_MIN_IMAGE_HEIGHT, img_height_max, 1, 0);

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
	format.format.width = geo.output.width;
	format.format.height = geo.output.height;
	ret = v4l2_device_call_until_err(&vou_dev->v4l2_dev, 0, pad,
					 set_fmt, NULL, &format);
	/* Must be implemented, so, don't check for -ENOIOCTLCMD */
	if (ret < 0)
		return ret;

	/* Sanity checks */
	if ((unsigned)format.format.width > VOU_MAX_IMAGE_WIDTH ||
	    (unsigned)format.format.height > img_height_max ||
	    format.format.code != MEDIA_BUS_FMT_YUYV8_2X8)
		return -EIO;

	geo.output.width = format.format.width;
	geo.output.height = format.format.height;

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

static irqreturn_t sh_vou_isr(int irq, void *dev_id)
{
	struct sh_vou_device *vou_dev = dev_id;
	static unsigned long j;
	struct sh_vou_buffer *vb;
	static int cnt;
	u32 irq_status = sh_vou_reg_a_read(vou_dev, VOUIR), masked;
	u32 vou_status = sh_vou_reg_a_read(vou_dev, VOUSTR);

	if (!(irq_status & 0x300)) {
		if (printk_timed_ratelimit(&j, 500))
			dev_warn(vou_dev->v4l2_dev.dev, "IRQ status 0x%x!\n",
				 irq_status);
		return IRQ_NONE;
	}

	spin_lock(&vou_dev->lock);
	if (!vou_dev->active || list_empty(&vou_dev->buf_list)) {
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
	/* side = vou_status & 0x10000; */

	/* Clear only set interrupts */
	sh_vou_reg_a_write(vou_dev, VOUIR, masked);

	vb = vou_dev->active;
	if (list_is_singular(&vb->list)) {
		/* Keep cycling while no next buffer is available */
		sh_vou_schedule_next(vou_dev, &vb->vb);
		spin_unlock(&vou_dev->lock);
		return IRQ_HANDLED;
	}

	list_del(&vb->list);

	v4l2_get_timestamp(&vb->vb.timestamp);
	vb->vb.sequence = vou_dev->sequence++;
	vb->vb.field = V4L2_FIELD_INTERLACED;
	vb2_buffer_done(&vb->vb.vb2_buf, VB2_BUF_STATE_DONE);

	vou_dev->active = list_entry(vou_dev->buf_list.next,
				     struct sh_vou_buffer, list);

	if (list_is_singular(&vou_dev->buf_list)) {
		/* Keep cycling while no next buffer is available */
		sh_vou_schedule_next(vou_dev, &vou_dev->active->vb);
	} else {
		struct sh_vou_buffer *new = list_entry(vou_dev->active->list.next,
						struct sh_vou_buffer, list);
		sh_vou_schedule_next(vou_dev, &new->vb);
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

	sh_vou_set_fmt_vid_out(vou_dev, &vou_dev->pix);

	return 0;
}

/* File operations */
static int sh_vou_open(struct file *file)
{
	struct sh_vou_device *vou_dev = video_drvdata(file);
	int err;

	if (mutex_lock_interruptible(&vou_dev->fop_lock))
		return -ERESTARTSYS;

	err = v4l2_fh_open(file);
	if (err)
		goto done_open;
	if (v4l2_fh_is_singular_file(file) &&
	    vou_dev->status == SH_VOU_INITIALISING) {
		/* First open */
		pm_runtime_get_sync(vou_dev->v4l2_dev.dev);
		err = sh_vou_hw_init(vou_dev);
		if (err < 0) {
			pm_runtime_put(vou_dev->v4l2_dev.dev);
			v4l2_fh_release(file);
		} else {
			vou_dev->status = SH_VOU_IDLE;
		}
	}
done_open:
	mutex_unlock(&vou_dev->fop_lock);
	return err;
}

static int sh_vou_release(struct file *file)
{
	struct sh_vou_device *vou_dev = video_drvdata(file);
	bool is_last;

	mutex_lock(&vou_dev->fop_lock);
	is_last = v4l2_fh_is_singular_file(file);
	_vb2_fop_release(file, NULL);
	if (is_last) {
		/* Last close */
		vou_dev->status = SH_VOU_INITIALISING;
		sh_vou_reg_a_set(vou_dev, VOUER, 0, 0x101);
		pm_runtime_put(vou_dev->v4l2_dev.dev);
	}
	mutex_unlock(&vou_dev->fop_lock);
	return 0;
}

/* sh_vou display ioctl operations */
static const struct v4l2_ioctl_ops sh_vou_ioctl_ops = {
	.vidioc_querycap        	= sh_vou_querycap,
	.vidioc_enum_fmt_vid_out	= sh_vou_enum_fmt_vid_out,
	.vidioc_g_fmt_vid_out		= sh_vou_g_fmt_vid_out,
	.vidioc_s_fmt_vid_out		= sh_vou_s_fmt_vid_out,
	.vidioc_try_fmt_vid_out		= sh_vou_try_fmt_vid_out,
	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_g_output		= sh_vou_g_output,
	.vidioc_s_output		= sh_vou_s_output,
	.vidioc_enum_output		= sh_vou_enum_output,
	.vidioc_s_std			= sh_vou_s_std,
	.vidioc_g_std			= sh_vou_g_std,
	.vidioc_g_selection		= sh_vou_g_selection,
	.vidioc_s_selection		= sh_vou_s_selection,
	.vidioc_log_status		= sh_vou_log_status,
};

static const struct v4l2_file_operations sh_vou_fops = {
	.owner		= THIS_MODULE,
	.open		= sh_vou_open,
	.release	= sh_vou_release,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= vb2_fop_mmap,
	.poll		= vb2_fop_poll,
	.write		= vb2_fop_write,
};

static const struct video_device sh_vou_video_template = {
	.name		= "sh_vou",
	.fops		= &sh_vou_fops,
	.ioctl_ops	= &sh_vou_ioctl_ops,
	.tvnorms	= V4L2_STD_525_60, /* PAL only supported in 8-bit non-bt656 mode */
	.vfl_dir	= VFL_DIR_TX,
};

static int sh_vou_probe(struct platform_device *pdev)
{
	struct sh_vou_pdata *vou_pdata = pdev->dev.platform_data;
	struct v4l2_rect *rect;
	struct v4l2_pix_format *pix;
	struct i2c_adapter *i2c_adap;
	struct video_device *vdev;
	struct sh_vou_device *vou_dev;
	struct resource *reg_res;
	struct v4l2_subdev *subdev;
	struct vb2_queue *q;
	int irq, ret;

	reg_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);

	if (!vou_pdata || !reg_res || irq <= 0) {
		dev_err(&pdev->dev, "Insufficient VOU platform information.\n");
		return -ENODEV;
	}

	vou_dev = devm_kzalloc(&pdev->dev, sizeof(*vou_dev), GFP_KERNEL);
	if (!vou_dev)
		return -ENOMEM;

	INIT_LIST_HEAD(&vou_dev->buf_list);
	spin_lock_init(&vou_dev->lock);
	mutex_init(&vou_dev->fop_lock);
	vou_dev->pdata = vou_pdata;
	vou_dev->status = SH_VOU_INITIALISING;
	vou_dev->pix_idx = 1;

	rect = &vou_dev->rect;
	pix = &vou_dev->pix;

	/* Fill in defaults */
	vou_dev->std		= V4L2_STD_NTSC_M;
	rect->left		= 0;
	rect->top		= 0;
	rect->width		= VOU_MAX_IMAGE_WIDTH;
	rect->height		= 480;
	pix->width		= VOU_MAX_IMAGE_WIDTH;
	pix->height		= 480;
	pix->pixelformat	= V4L2_PIX_FMT_NV16;
	pix->field		= V4L2_FIELD_INTERLACED;
	pix->bytesperline	= VOU_MAX_IMAGE_WIDTH;
	pix->sizeimage		= VOU_MAX_IMAGE_WIDTH * 2 * 480;
	pix->colorspace		= V4L2_COLORSPACE_SMPTE170M;

	vou_dev->base = devm_ioremap_resource(&pdev->dev, reg_res);
	if (IS_ERR(vou_dev->base))
		return PTR_ERR(vou_dev->base);

	ret = devm_request_irq(&pdev->dev, irq, sh_vou_isr, 0, "vou", vou_dev);
	if (ret < 0)
		return ret;

	ret = v4l2_device_register(&pdev->dev, &vou_dev->v4l2_dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Error registering v4l2 device\n");
		return ret;
	}

	vdev = &vou_dev->vdev;
	*vdev = sh_vou_video_template;
	if (vou_pdata->bus_fmt == SH_VOU_BUS_8BIT)
		vdev->tvnorms |= V4L2_STD_PAL;
	vdev->v4l2_dev = &vou_dev->v4l2_dev;
	vdev->release = video_device_release_empty;
	vdev->lock = &vou_dev->fop_lock;

	video_set_drvdata(vdev, vou_dev);

	/* Initialize the vb2 queue */
	q = &vou_dev->queue;
	q->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	q->io_modes = VB2_MMAP | VB2_DMABUF | VB2_WRITE;
	q->drv_priv = vou_dev;
	q->buf_struct_size = sizeof(struct sh_vou_buffer);
	q->ops = &sh_vou_qops;
	q->mem_ops = &vb2_dma_contig_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->min_buffers_needed = 2;
	q->lock = &vou_dev->fop_lock;
	ret = vb2_queue_init(q);
	if (ret)
		goto einitctx;

	vou_dev->alloc_ctx = vb2_dma_contig_init_ctx(&pdev->dev);
	if (IS_ERR(vou_dev->alloc_ctx)) {
		dev_err(&pdev->dev, "Can't allocate buffer context");
		ret = PTR_ERR(vou_dev->alloc_ctx);
		goto einitctx;
	}
	vdev->queue = q;
	INIT_LIST_HEAD(&vou_dev->buf_list);

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
	vb2_dma_contig_cleanup_ctx(vou_dev->alloc_ctx);
einitctx:
	pm_runtime_disable(&pdev->dev);
	v4l2_device_unregister(&vou_dev->v4l2_dev);
	return ret;
}

static int sh_vou_remove(struct platform_device *pdev)
{
	struct v4l2_device *v4l2_dev = platform_get_drvdata(pdev);
	struct sh_vou_device *vou_dev = container_of(v4l2_dev,
						struct sh_vou_device, v4l2_dev);
	struct v4l2_subdev *sd = list_entry(v4l2_dev->subdevs.next,
					    struct v4l2_subdev, list);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	pm_runtime_disable(&pdev->dev);
	video_unregister_device(&vou_dev->vdev);
	i2c_put_adapter(client->adapter);
	vb2_dma_contig_cleanup_ctx(vou_dev->alloc_ctx);
	v4l2_device_unregister(&vou_dev->v4l2_dev);
	return 0;
}

static struct platform_driver __refdata sh_vou = {
	.remove  = sh_vou_remove,
	.driver  = {
		.name	= "sh-vou",
	},
};

module_platform_driver_probe(sh_vou, sh_vou_probe);

MODULE_DESCRIPTION("SuperH VOU driver");
MODULE_AUTHOR("Guennadi Liakhovetski <g.liakhovetski@gmx.de>");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("0.1.0");
MODULE_ALIAS("platform:sh-vou");
