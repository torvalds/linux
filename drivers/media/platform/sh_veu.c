/*
 * sh-mobile VEU mem2mem driver
 *
 * Copyright (C) 2012 Renesas Electronics Corporation
 * Author: Guennadi Liakhovetski, <g.liakhovetski@gmx.de>
 * Copyright (C) 2008 Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the version 2 of the GNU General Public License as
 * published by the Free Software Foundation
 */

#include <linux/err.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/videodev2.h>

#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#define VEU_STR 0x00 /* start register */
#define VEU_SWR 0x10 /* src: line length */
#define VEU_SSR 0x14 /* src: image size */
#define VEU_SAYR 0x18 /* src: y/rgb plane address */
#define VEU_SACR 0x1c /* src: c plane address */
#define VEU_BSSR 0x20 /* bundle mode register */
#define VEU_EDWR 0x30 /* dst: line length */
#define VEU_DAYR 0x34 /* dst: y/rgb plane address */
#define VEU_DACR 0x38 /* dst: c plane address */
#define VEU_TRCR 0x50 /* transform control */
#define VEU_RFCR 0x54 /* resize scale */
#define VEU_RFSR 0x58 /* resize clip */
#define VEU_ENHR 0x5c /* enhance */
#define VEU_FMCR 0x70 /* filter mode */
#define VEU_VTCR 0x74 /* lowpass vertical */
#define VEU_HTCR 0x78 /* lowpass horizontal */
#define VEU_APCR 0x80 /* color match */
#define VEU_ECCR 0x84 /* color replace */
#define VEU_AFXR 0x90 /* fixed mode */
#define VEU_SWPR 0x94 /* swap */
#define VEU_EIER 0xa0 /* interrupt mask */
#define VEU_EVTR 0xa4 /* interrupt event */
#define VEU_STAR 0xb0 /* status */
#define VEU_BSRR 0xb4 /* reset */

#define VEU_MCR00 0x200 /* color conversion matrix coefficient 00 */
#define VEU_MCR01 0x204 /* color conversion matrix coefficient 01 */
#define VEU_MCR02 0x208 /* color conversion matrix coefficient 02 */
#define VEU_MCR10 0x20c /* color conversion matrix coefficient 10 */
#define VEU_MCR11 0x210 /* color conversion matrix coefficient 11 */
#define VEU_MCR12 0x214 /* color conversion matrix coefficient 12 */
#define VEU_MCR20 0x218 /* color conversion matrix coefficient 20 */
#define VEU_MCR21 0x21c /* color conversion matrix coefficient 21 */
#define VEU_MCR22 0x220 /* color conversion matrix coefficient 22 */
#define VEU_COFFR 0x224 /* color conversion offset */
#define VEU_CBR   0x228 /* color conversion clip */

/*
 * 4092x4092 max size is the normal case. In some cases it can be reduced to
 * 2048x2048, in other cases it can be 4092x8188 or even 8188x8188.
 */
#define MAX_W 4092
#define MAX_H 4092
#define MIN_W 8
#define MIN_H 8
#define ALIGN_W 4

/* 3 buffers of 2048 x 1536 - 3 megapixels @ 16bpp */
#define VIDEO_MEM_LIMIT ALIGN(2048 * 1536 * 2 * 3, 1024 * 1024)

#define MEM2MEM_DEF_TRANSLEN 1

struct sh_veu_dev;

struct sh_veu_file {
	struct sh_veu_dev *veu_dev;
	bool cfg_needed;
};

struct sh_veu_format {
	char *name;
	u32 fourcc;
	unsigned int depth;
	unsigned int ydepth;
};

/* video data format */
struct sh_veu_vfmt {
	/* Replace with v4l2_rect */
	struct v4l2_rect		frame;
	unsigned int			bytesperline;
	unsigned int			offset_y;
	unsigned int			offset_c;
	const struct sh_veu_format	*fmt;
};

struct sh_veu_dev {
	struct v4l2_device v4l2_dev;
	struct video_device vdev;
	struct v4l2_m2m_dev *m2m_dev;
	struct device *dev;
	struct v4l2_m2m_ctx *m2m_ctx;
	struct sh_veu_vfmt vfmt_out;
	struct sh_veu_vfmt vfmt_in;
	/* Only single user per direction so far */
	struct sh_veu_file *capture;
	struct sh_veu_file *output;
	struct mutex fop_lock;
	void __iomem *base;
	struct vb2_alloc_ctx *alloc_ctx;
	spinlock_t lock;
	bool is_2h;
	unsigned int xaction;
	bool aborting;
};

enum sh_veu_fmt_idx {
	SH_VEU_FMT_NV12,
	SH_VEU_FMT_NV16,
	SH_VEU_FMT_NV24,
	SH_VEU_FMT_RGB332,
	SH_VEU_FMT_RGB444,
	SH_VEU_FMT_RGB565,
	SH_VEU_FMT_RGB666,
	SH_VEU_FMT_RGB24,
};

#define VGA_WIDTH	640
#define VGA_HEIGHT	480

#define DEFAULT_IN_WIDTH	VGA_WIDTH
#define DEFAULT_IN_HEIGHT	VGA_HEIGHT
#define DEFAULT_IN_FMTIDX	SH_VEU_FMT_NV12
#define DEFAULT_OUT_WIDTH	VGA_WIDTH
#define DEFAULT_OUT_HEIGHT	VGA_HEIGHT
#define DEFAULT_OUT_FMTIDX	SH_VEU_FMT_RGB565

/*
 * Alignment: Y-plane should be 4-byte aligned for NV12 and NV16, and 8-byte
 * aligned for NV24.
 */
static const struct sh_veu_format sh_veu_fmt[] = {
	[SH_VEU_FMT_NV12]   = { .ydepth = 8, .depth = 12, .name = "NV12", .fourcc = V4L2_PIX_FMT_NV12 },
	[SH_VEU_FMT_NV16]   = { .ydepth = 8, .depth = 16, .name = "NV16", .fourcc = V4L2_PIX_FMT_NV16 },
	[SH_VEU_FMT_NV24]   = { .ydepth = 8, .depth = 24, .name = "NV24", .fourcc = V4L2_PIX_FMT_NV24 },
	[SH_VEU_FMT_RGB332] = { .ydepth = 8, .depth = 8, .name = "RGB332", .fourcc = V4L2_PIX_FMT_RGB332 },
	[SH_VEU_FMT_RGB444] = { .ydepth = 16, .depth = 16, .name = "RGB444", .fourcc = V4L2_PIX_FMT_RGB444 },
	[SH_VEU_FMT_RGB565] = { .ydepth = 16, .depth = 16, .name = "RGB565", .fourcc = V4L2_PIX_FMT_RGB565 },
	[SH_VEU_FMT_RGB666] = { .ydepth = 32, .depth = 32, .name = "BGR666", .fourcc = V4L2_PIX_FMT_BGR666 },
	[SH_VEU_FMT_RGB24]  = { .ydepth = 24, .depth = 24, .name = "RGB24", .fourcc = V4L2_PIX_FMT_RGB24 },
};

#define DEFAULT_IN_VFMT (struct sh_veu_vfmt){						\
	.frame = {									\
		.width = VGA_WIDTH,							\
		.height = VGA_HEIGHT,							\
	},										\
	.bytesperline = (VGA_WIDTH * sh_veu_fmt[DEFAULT_IN_FMTIDX].ydepth) >> 3,	\
	.fmt = &sh_veu_fmt[DEFAULT_IN_FMTIDX],						\
}

#define DEFAULT_OUT_VFMT (struct sh_veu_vfmt){						\
	.frame = {									\
		.width = VGA_WIDTH,							\
		.height = VGA_HEIGHT,							\
	},										\
	.bytesperline = (VGA_WIDTH * sh_veu_fmt[DEFAULT_OUT_FMTIDX].ydepth) >> 3,	\
	.fmt = &sh_veu_fmt[DEFAULT_OUT_FMTIDX],						\
}

/*
 * TODO: add support for further output formats:
 *	SH_VEU_FMT_NV12,
 *	SH_VEU_FMT_NV16,
 *	SH_VEU_FMT_NV24,
 *	SH_VEU_FMT_RGB332,
 *	SH_VEU_FMT_RGB444,
 *	SH_VEU_FMT_RGB666,
 *	SH_VEU_FMT_RGB24,
 */

static const int sh_veu_fmt_out[] = {
	SH_VEU_FMT_RGB565,
};

/*
 * TODO: add support for further input formats:
 *	SH_VEU_FMT_NV16,
 *	SH_VEU_FMT_NV24,
 *	SH_VEU_FMT_RGB565,
 *	SH_VEU_FMT_RGB666,
 *	SH_VEU_FMT_RGB24,
 */
static const int sh_veu_fmt_in[] = {
	SH_VEU_FMT_NV12,
};

static enum v4l2_colorspace sh_veu_4cc2cspace(u32 fourcc)
{
	switch (fourcc) {
	default:
		BUG();
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV24:
		return V4L2_COLORSPACE_JPEG;
	case V4L2_PIX_FMT_RGB332:
	case V4L2_PIX_FMT_RGB444:
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_BGR666:
	case V4L2_PIX_FMT_RGB24:
		return V4L2_COLORSPACE_SRGB;
	}
}

static u32 sh_veu_reg_read(struct sh_veu_dev *veu, unsigned int reg)
{
	return ioread32(veu->base + reg);
}

static void sh_veu_reg_write(struct sh_veu_dev *veu, unsigned int reg,
			     u32 value)
{
	iowrite32(value, veu->base + reg);
}

		/* ========== mem2mem callbacks ========== */

static void sh_veu_job_abort(void *priv)
{
	struct sh_veu_dev *veu = priv;

	/* Will cancel the transaction in the next interrupt handler */
	veu->aborting = true;
}

static void sh_veu_lock(void *priv)
{
	struct sh_veu_dev *veu = priv;

	mutex_lock(&veu->fop_lock);
}

static void sh_veu_unlock(void *priv)
{
	struct sh_veu_dev *veu = priv;

	mutex_unlock(&veu->fop_lock);
}

static void sh_veu_process(struct sh_veu_dev *veu,
			   struct vb2_buffer *src_buf,
			   struct vb2_buffer *dst_buf)
{
	dma_addr_t addr = vb2_dma_contig_plane_dma_addr(dst_buf, 0);

	sh_veu_reg_write(veu, VEU_DAYR, addr + veu->vfmt_out.offset_y);
	sh_veu_reg_write(veu, VEU_DACR, veu->vfmt_out.offset_c ?
			 addr + veu->vfmt_out.offset_c : 0);
	dev_dbg(veu->dev, "%s(): dst base %lx, y: %x, c: %x\n", __func__,
		(unsigned long)addr,
		veu->vfmt_out.offset_y, veu->vfmt_out.offset_c);

	addr = vb2_dma_contig_plane_dma_addr(src_buf, 0);
	sh_veu_reg_write(veu, VEU_SAYR, addr + veu->vfmt_in.offset_y);
	sh_veu_reg_write(veu, VEU_SACR, veu->vfmt_in.offset_c ?
			 addr + veu->vfmt_in.offset_c : 0);
	dev_dbg(veu->dev, "%s(): src base %lx, y: %x, c: %x\n", __func__,
		(unsigned long)addr,
		veu->vfmt_in.offset_y, veu->vfmt_in.offset_c);

	sh_veu_reg_write(veu, VEU_STR, 1);

	sh_veu_reg_write(veu, VEU_EIER, 1); /* enable interrupt in VEU */
}

/**
 * sh_veu_device_run() - prepares and starts the device
 *
 * This will be called by the framework when it decides to schedule a particular
 * instance.
 */
static void sh_veu_device_run(void *priv)
{
	struct sh_veu_dev *veu = priv;
	struct vb2_buffer *src_buf, *dst_buf;

	src_buf = v4l2_m2m_next_src_buf(veu->m2m_ctx);
	dst_buf = v4l2_m2m_next_dst_buf(veu->m2m_ctx);

	if (src_buf && dst_buf)
		sh_veu_process(veu, src_buf, dst_buf);
}

		/* ========== video ioctls ========== */

static bool sh_veu_is_streamer(struct sh_veu_dev *veu, struct sh_veu_file *veu_file,
			       enum v4l2_buf_type type)
{
	return (type == V4L2_BUF_TYPE_VIDEO_CAPTURE &&
		veu_file == veu->capture) ||
		(type == V4L2_BUF_TYPE_VIDEO_OUTPUT &&
		 veu_file == veu->output);
}

static int sh_veu_queue_init(void *priv, struct vb2_queue *src_vq,
			     struct vb2_queue *dst_vq);

/*
 * It is not unusual to have video nodes open()ed multiple times. While some
 * V4L2 operations are non-intrusive, like querying formats and various
 * parameters, others, like setting formats, starting and stopping streaming,
 * queuing and dequeuing buffers, directly affect hardware configuration and /
 * or execution. This function verifies availability of the requested interface
 * and, if available, reserves it for the requesting user.
 */
static int sh_veu_stream_init(struct sh_veu_dev *veu, struct sh_veu_file *veu_file,
			      enum v4l2_buf_type type)
{
	struct sh_veu_file **stream;

	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		stream = &veu->capture;
		break;
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		stream = &veu->output;
		break;
	default:
		return -EINVAL;
	}

	if (*stream == veu_file)
		return 0;

	if (*stream)
		return -EBUSY;

	*stream = veu_file;

	return 0;
}

static int sh_veu_context_init(struct sh_veu_dev *veu)
{
	if (veu->m2m_ctx)
		return 0;

	veu->m2m_ctx = v4l2_m2m_ctx_init(veu->m2m_dev, veu,
					 sh_veu_queue_init);

	return PTR_RET(veu->m2m_ctx);
}

static int sh_veu_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	strlcpy(cap->driver, "sh-veu", sizeof(cap->driver));
	strlcpy(cap->card, "sh-mobile VEU", sizeof(cap->card));
	strlcpy(cap->bus_info, "platform:sh-veu", sizeof(cap->bus_info));
	cap->device_caps = V4L2_CAP_VIDEO_M2M | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	return 0;
}

static int sh_veu_enum_fmt(struct v4l2_fmtdesc *f, const int *fmt, int fmt_num)
{
	if (f->index >= fmt_num)
		return -EINVAL;

	strlcpy(f->description, sh_veu_fmt[fmt[f->index]].name, sizeof(f->description));
	f->pixelformat = sh_veu_fmt[fmt[f->index]].fourcc;
	return 0;
}

static int sh_veu_enum_fmt_vid_cap(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	return sh_veu_enum_fmt(f, sh_veu_fmt_out, ARRAY_SIZE(sh_veu_fmt_out));
}

static int sh_veu_enum_fmt_vid_out(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	return sh_veu_enum_fmt(f, sh_veu_fmt_in, ARRAY_SIZE(sh_veu_fmt_in));
}

static struct sh_veu_vfmt *sh_veu_get_vfmt(struct sh_veu_dev *veu,
					   enum v4l2_buf_type type)
{
	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		return &veu->vfmt_out;
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		return &veu->vfmt_in;
	default:
		return NULL;
	}
}

static int sh_veu_g_fmt(struct sh_veu_file *veu_file, struct v4l2_format *f)
{
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct sh_veu_dev *veu = veu_file->veu_dev;
	struct sh_veu_vfmt *vfmt;

	vfmt = sh_veu_get_vfmt(veu, f->type);

	pix->width		= vfmt->frame.width;
	pix->height		= vfmt->frame.height;
	pix->field		= V4L2_FIELD_NONE;
	pix->pixelformat	= vfmt->fmt->fourcc;
	pix->colorspace		= sh_veu_4cc2cspace(pix->pixelformat);
	pix->bytesperline	= vfmt->bytesperline;
	pix->sizeimage		= vfmt->bytesperline * pix->height *
		vfmt->fmt->depth / vfmt->fmt->ydepth;
	pix->priv		= 0;
	dev_dbg(veu->dev, "%s(): type: %d, size %u @ %ux%u, fmt %x\n", __func__,
		f->type, pix->sizeimage, pix->width, pix->height, pix->pixelformat);

	return 0;
}

static int sh_veu_g_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *f)
{
	return sh_veu_g_fmt(priv, f);
}

static int sh_veu_g_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	return sh_veu_g_fmt(priv, f);
}

static int sh_veu_try_fmt(struct v4l2_format *f, const struct sh_veu_format *fmt)
{
	struct v4l2_pix_format *pix = &f->fmt.pix;
	unsigned int y_bytes_used;

	/*
	 * V4L2 specification suggests, that the driver should correct the
	 * format struct if any of the dimensions is unsupported
	 */
	switch (pix->field) {
	default:
	case V4L2_FIELD_ANY:
		pix->field = V4L2_FIELD_NONE;
		/* fall through: continue handling V4L2_FIELD_NONE */
	case V4L2_FIELD_NONE:
		break;
	}

	v4l_bound_align_image(&pix->width, MIN_W, MAX_W, ALIGN_W,
			      &pix->height, MIN_H, MAX_H, 0, 0);

	y_bytes_used = (pix->width * fmt->ydepth) >> 3;

	if (pix->bytesperline < y_bytes_used)
		pix->bytesperline = y_bytes_used;
	pix->sizeimage = pix->height * pix->bytesperline * fmt->depth / fmt->ydepth;

	pix->pixelformat	= fmt->fourcc;
	pix->colorspace		= sh_veu_4cc2cspace(pix->pixelformat);
	pix->priv		= 0;

	pr_debug("%s(): type: %d, size %u\n", __func__, f->type, pix->sizeimage);

	return 0;
}

static const struct sh_veu_format *sh_veu_find_fmt(const struct v4l2_format *f)
{
	const int *fmt;
	int i, n, dflt;

	pr_debug("%s(%d;%d)\n", __func__, f->type, f->fmt.pix.field);

	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		fmt = sh_veu_fmt_out;
		n = ARRAY_SIZE(sh_veu_fmt_out);
		dflt = DEFAULT_OUT_FMTIDX;
		break;
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
	default:
		fmt = sh_veu_fmt_in;
		n = ARRAY_SIZE(sh_veu_fmt_in);
		dflt = DEFAULT_IN_FMTIDX;
		break;
	}

	for (i = 0; i < n; i++)
		if (sh_veu_fmt[fmt[i]].fourcc == f->fmt.pix.pixelformat)
			return &sh_veu_fmt[fmt[i]];

	return &sh_veu_fmt[dflt];
}

static int sh_veu_try_fmt_vid_cap(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	const struct sh_veu_format *fmt;

	fmt = sh_veu_find_fmt(f);
	if (!fmt)
		/* wrong buffer type */
		return -EINVAL;

	return sh_veu_try_fmt(f, fmt);
}

static int sh_veu_try_fmt_vid_out(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	const struct sh_veu_format *fmt;

	fmt = sh_veu_find_fmt(f);
	if (!fmt)
		/* wrong buffer type */
		return -EINVAL;

	return sh_veu_try_fmt(f, fmt);
}

static void sh_veu_colour_offset(struct sh_veu_dev *veu, struct sh_veu_vfmt *vfmt)
{
	/* dst_left and dst_top validity will be verified in CROP / COMPOSE */
	unsigned int left = vfmt->frame.left & ~0x03;
	unsigned int top = vfmt->frame.top;
	dma_addr_t offset = ((left * veu->vfmt_out.fmt->depth) >> 3) +
		top * veu->vfmt_out.bytesperline;
	unsigned int y_line;

	vfmt->offset_y = offset;

	switch (vfmt->fmt->fourcc) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV16:
	case V4L2_PIX_FMT_NV24:
		y_line = ALIGN(vfmt->frame.width, 16);
		vfmt->offset_c = offset + y_line * vfmt->frame.height;
		break;
	case V4L2_PIX_FMT_RGB332:
	case V4L2_PIX_FMT_RGB444:
	case V4L2_PIX_FMT_RGB565:
	case V4L2_PIX_FMT_BGR666:
	case V4L2_PIX_FMT_RGB24:
		vfmt->offset_c = 0;
		break;
	default:
		BUG();
	}
}

static int sh_veu_s_fmt(struct sh_veu_file *veu_file, struct v4l2_format *f)
{
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct sh_veu_dev *veu = veu_file->veu_dev;
	struct sh_veu_vfmt *vfmt;
	struct vb2_queue *vq;
	int ret = sh_veu_context_init(veu);
	if (ret < 0)
		return ret;

	vq = v4l2_m2m_get_vq(veu->m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	if (vb2_is_busy(vq)) {
		v4l2_err(&veu_file->veu_dev->v4l2_dev, "%s queue busy\n", __func__);
		return -EBUSY;
	}

	vfmt = sh_veu_get_vfmt(veu, f->type);
	/* called after try_fmt(), hence vfmt != NULL. Implicit BUG_ON() below */

	vfmt->fmt		= sh_veu_find_fmt(f);
	/* vfmt->fmt != NULL following the same argument as above */
	vfmt->frame.width	= pix->width;
	vfmt->frame.height	= pix->height;
	vfmt->bytesperline	= pix->bytesperline;

	sh_veu_colour_offset(veu, vfmt);

	/*
	 * We could also verify and require configuration only if any parameters
	 * actually have changed, but it is unlikely, that the user requests the
	 * same configuration several times without closing the device.
	 */
	veu_file->cfg_needed = true;

	dev_dbg(veu->dev,
		"Setting format for type %d, wxh: %dx%d, fmt: %x\n",
		f->type, pix->width, pix->height, vfmt->fmt->fourcc);

	return 0;
}

static int sh_veu_s_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	int ret = sh_veu_try_fmt_vid_cap(file, priv, f);
	if (ret)
		return ret;

	return sh_veu_s_fmt(priv, f);
}

static int sh_veu_s_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *f)
{
	int ret = sh_veu_try_fmt_vid_out(file, priv, f);
	if (ret)
		return ret;

	return sh_veu_s_fmt(priv, f);
}

static int sh_veu_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *reqbufs)
{
	struct sh_veu_file *veu_file = priv;
	struct sh_veu_dev *veu = veu_file->veu_dev;
	int ret = sh_veu_context_init(veu);
	if (ret < 0)
		return ret;

	ret = sh_veu_stream_init(veu, veu_file, reqbufs->type);
	if (ret < 0)
		return ret;

	return v4l2_m2m_reqbufs(file, veu->m2m_ctx, reqbufs);
}

static int sh_veu_querybuf(struct file *file, void *priv,
			   struct v4l2_buffer *buf)
{
	struct sh_veu_file *veu_file = priv;

	if (!sh_veu_is_streamer(veu_file->veu_dev, veu_file, buf->type))
		return -EBUSY;

	return v4l2_m2m_querybuf(file, veu_file->veu_dev->m2m_ctx, buf);
}

static int sh_veu_qbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct sh_veu_file *veu_file = priv;

	dev_dbg(veu_file->veu_dev->dev, "%s(%d)\n", __func__, buf->type);
	if (!sh_veu_is_streamer(veu_file->veu_dev, veu_file, buf->type))
		return -EBUSY;

	return v4l2_m2m_qbuf(file, veu_file->veu_dev->m2m_ctx, buf);
}

static int sh_veu_dqbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct sh_veu_file *veu_file = priv;

	dev_dbg(veu_file->veu_dev->dev, "%s(%d)\n", __func__, buf->type);
	if (!sh_veu_is_streamer(veu_file->veu_dev, veu_file, buf->type))
		return -EBUSY;

	return v4l2_m2m_dqbuf(file, veu_file->veu_dev->m2m_ctx, buf);
}

static void sh_veu_calc_scale(struct sh_veu_dev *veu,
			      int size_in, int size_out, int crop_out,
			      u32 *mant, u32 *frac, u32 *rep)
{
	u32 fixpoint;

	/* calculate FRAC and MANT */
	*rep = *mant = *frac = 0;

	if (size_in == size_out) {
		if (crop_out != size_out)
			*mant = 1; /* needed for cropping */
		return;
	}

	/* VEU2H special upscale */
	if (veu->is_2h && size_out > size_in) {
		u32 fixpoint = (4096 * size_in) / size_out;
		*mant = fixpoint / 4096;
		*frac = (fixpoint - (*mant * 4096)) & ~0x07;

		switch (*frac) {
		case 0x800:
			*rep = 1;
			break;
		case 0x400:
			*rep = 3;
			break;
		case 0x200:
			*rep = 7;
			break;
		}
		if (*rep)
			return;
	}

	fixpoint = (4096 * (size_in - 1)) / (size_out + 1);
	*mant = fixpoint / 4096;
	*frac = fixpoint - (*mant * 4096);

	if (*frac & 0x07) {
		/*
		 * FIXME: do we really have to round down twice in the
		 * up-scaling case?
		 */
		*frac &= ~0x07;
		if (size_out > size_in)
			*frac -= 8; /* round down if scaling up */
		else
			*frac += 8; /* round up if scaling down */
	}
}

static unsigned long sh_veu_scale_v(struct sh_veu_dev *veu,
				    int size_in, int size_out, int crop_out)
{
	u32 mant, frac, value, rep;

	sh_veu_calc_scale(veu, size_in, size_out, crop_out, &mant, &frac, &rep);

	/* set scale */
	value = (sh_veu_reg_read(veu, VEU_RFCR) & ~0xffff0000) |
		(((mant << 12) | frac) << 16);

	sh_veu_reg_write(veu, VEU_RFCR, value);

	/* set clip */
	value = (sh_veu_reg_read(veu, VEU_RFSR) & ~0xffff0000) |
		(((rep << 12) | crop_out) << 16);

	sh_veu_reg_write(veu, VEU_RFSR, value);

	return ALIGN((size_in * crop_out) / size_out, 4);
}

static unsigned long sh_veu_scale_h(struct sh_veu_dev *veu,
				    int size_in, int size_out, int crop_out)
{
	u32 mant, frac, value, rep;

	sh_veu_calc_scale(veu, size_in, size_out, crop_out, &mant, &frac, &rep);

	/* set scale */
	value = (sh_veu_reg_read(veu, VEU_RFCR) & ~0xffff) |
		(mant << 12) | frac;

	sh_veu_reg_write(veu, VEU_RFCR, value);

	/* set clip */
	value = (sh_veu_reg_read(veu, VEU_RFSR) & ~0xffff) |
		(rep << 12) | crop_out;

	sh_veu_reg_write(veu, VEU_RFSR, value);

	return ALIGN((size_in * crop_out) / size_out, 4);
}

static void sh_veu_configure(struct sh_veu_dev *veu)
{
	u32 src_width, src_stride, src_height;
	u32 dst_width, dst_stride, dst_height;
	u32 real_w, real_h;

	/* reset VEU */
	sh_veu_reg_write(veu, VEU_BSRR, 0x100);

	src_width = veu->vfmt_in.frame.width;
	src_height = veu->vfmt_in.frame.height;
	src_stride = ALIGN(veu->vfmt_in.frame.width, 16);

	dst_width = real_w = veu->vfmt_out.frame.width;
	dst_height = real_h = veu->vfmt_out.frame.height;
	/* Datasheet is unclear - whether it's always number of bytes or not */
	dst_stride = veu->vfmt_out.bytesperline;

	/*
	 * So far real_w == dst_width && real_h == dst_height, but it wasn't
	 * necessarily the case in the original vidix driver, so, it may change
	 * here in the future too.
	 */
	src_width = sh_veu_scale_h(veu, src_width, real_w, dst_width);
	src_height = sh_veu_scale_v(veu, src_height, real_h, dst_height);

	sh_veu_reg_write(veu, VEU_SWR, src_stride);
	sh_veu_reg_write(veu, VEU_SSR, src_width | (src_height << 16));
	sh_veu_reg_write(veu, VEU_BSSR, 0); /* not using bundle mode */

	sh_veu_reg_write(veu, VEU_EDWR, dst_stride);
	sh_veu_reg_write(veu, VEU_DACR, 0); /* unused for RGB */

	sh_veu_reg_write(veu, VEU_SWPR, 0x67);
	sh_veu_reg_write(veu, VEU_TRCR, (6 << 16) | (0 << 14) | 2 | 4);

	if (veu->is_2h) {
		sh_veu_reg_write(veu, VEU_MCR00, 0x0cc5);
		sh_veu_reg_write(veu, VEU_MCR01, 0x0950);
		sh_veu_reg_write(veu, VEU_MCR02, 0x0000);

		sh_veu_reg_write(veu, VEU_MCR10, 0x397f);
		sh_veu_reg_write(veu, VEU_MCR11, 0x0950);
		sh_veu_reg_write(veu, VEU_MCR12, 0x3ccd);

		sh_veu_reg_write(veu, VEU_MCR20, 0x0000);
		sh_veu_reg_write(veu, VEU_MCR21, 0x0950);
		sh_veu_reg_write(veu, VEU_MCR22, 0x1023);

		sh_veu_reg_write(veu, VEU_COFFR, 0x00800010);
	}
}

static int sh_veu_streamon(struct file *file, void *priv,
			   enum v4l2_buf_type type)
{
	struct sh_veu_file *veu_file = priv;

	if (!sh_veu_is_streamer(veu_file->veu_dev, veu_file, type))
		return -EBUSY;

	if (veu_file->cfg_needed) {
		struct sh_veu_dev *veu = veu_file->veu_dev;
		veu_file->cfg_needed = false;
		sh_veu_configure(veu_file->veu_dev);
		veu->xaction = 0;
		veu->aborting = false;
	}

	return v4l2_m2m_streamon(file, veu_file->veu_dev->m2m_ctx, type);
}

static int sh_veu_streamoff(struct file *file, void *priv,
			    enum v4l2_buf_type type)
{
	struct sh_veu_file *veu_file = priv;

	if (!sh_veu_is_streamer(veu_file->veu_dev, veu_file, type))
		return -EBUSY;

	return v4l2_m2m_streamoff(file, veu_file->veu_dev->m2m_ctx, type);
}

static const struct v4l2_ioctl_ops sh_veu_ioctl_ops = {
	.vidioc_querycap	= sh_veu_querycap,

	.vidioc_enum_fmt_vid_cap = sh_veu_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap	= sh_veu_g_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap	= sh_veu_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap	= sh_veu_s_fmt_vid_cap,

	.vidioc_enum_fmt_vid_out = sh_veu_enum_fmt_vid_out,
	.vidioc_g_fmt_vid_out	= sh_veu_g_fmt_vid_out,
	.vidioc_try_fmt_vid_out	= sh_veu_try_fmt_vid_out,
	.vidioc_s_fmt_vid_out	= sh_veu_s_fmt_vid_out,

	.vidioc_reqbufs		= sh_veu_reqbufs,
	.vidioc_querybuf	= sh_veu_querybuf,

	.vidioc_qbuf		= sh_veu_qbuf,
	.vidioc_dqbuf		= sh_veu_dqbuf,

	.vidioc_streamon	= sh_veu_streamon,
	.vidioc_streamoff	= sh_veu_streamoff,
};

		/* ========== Queue operations ========== */

static int sh_veu_queue_setup(struct vb2_queue *vq,
			      const struct v4l2_format *f,
			      unsigned int *nbuffers, unsigned int *nplanes,
			      unsigned int sizes[], void *alloc_ctxs[])
{
	struct sh_veu_dev *veu = vb2_get_drv_priv(vq);
	struct sh_veu_vfmt *vfmt;
	unsigned int size, count = *nbuffers;

	if (f) {
		const struct v4l2_pix_format *pix = &f->fmt.pix;
		const struct sh_veu_format *fmt = sh_veu_find_fmt(f);
		struct v4l2_format ftmp = *f;

		if (fmt->fourcc != pix->pixelformat)
			return -EINVAL;
		sh_veu_try_fmt(&ftmp, fmt);
		if (ftmp.fmt.pix.width != pix->width ||
		    ftmp.fmt.pix.height != pix->height)
			return -EINVAL;
		size = pix->bytesperline ? pix->bytesperline * pix->height * fmt->depth / fmt->ydepth :
			pix->width * pix->height * fmt->depth / fmt->ydepth;
	} else {
		vfmt = sh_veu_get_vfmt(veu, vq->type);
		size = vfmt->bytesperline * vfmt->frame.height * vfmt->fmt->depth / vfmt->fmt->ydepth;
	}

	if (count < 2)
		*nbuffers = count = 2;

	if (size * count > VIDEO_MEM_LIMIT) {
		count = VIDEO_MEM_LIMIT / size;
		*nbuffers = count;
	}

	*nplanes = 1;
	sizes[0] = size;
	alloc_ctxs[0] = veu->alloc_ctx;

	dev_dbg(veu->dev, "get %d buffer(s) of size %d each.\n", count, size);

	return 0;
}

static int sh_veu_buf_prepare(struct vb2_buffer *vb)
{
	struct sh_veu_dev *veu = vb2_get_drv_priv(vb->vb2_queue);
	struct sh_veu_vfmt *vfmt;
	unsigned int sizeimage;

	vfmt = sh_veu_get_vfmt(veu, vb->vb2_queue->type);
	sizeimage = vfmt->bytesperline * vfmt->frame.height *
		vfmt->fmt->depth / vfmt->fmt->ydepth;

	if (vb2_plane_size(vb, 0) < sizeimage) {
		dev_dbg(veu->dev, "%s data will not fit into plane (%lu < %u)\n",
			__func__, vb2_plane_size(vb, 0), sizeimage);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, sizeimage);

	return 0;
}

static void sh_veu_buf_queue(struct vb2_buffer *vb)
{
	struct sh_veu_dev *veu = vb2_get_drv_priv(vb->vb2_queue);
	dev_dbg(veu->dev, "%s(%d)\n", __func__, vb->v4l2_buf.type);
	v4l2_m2m_buf_queue(veu->m2m_ctx, vb);
}

static void sh_veu_wait_prepare(struct vb2_queue *q)
{
	sh_veu_unlock(vb2_get_drv_priv(q));
}

static void sh_veu_wait_finish(struct vb2_queue *q)
{
	sh_veu_lock(vb2_get_drv_priv(q));
}

static const struct vb2_ops sh_veu_qops = {
	.queue_setup	 = sh_veu_queue_setup,
	.buf_prepare	 = sh_veu_buf_prepare,
	.buf_queue	 = sh_veu_buf_queue,
	.wait_prepare	 = sh_veu_wait_prepare,
	.wait_finish	 = sh_veu_wait_finish,
};

static int sh_veu_queue_init(void *priv, struct vb2_queue *src_vq,
			     struct vb2_queue *dst_vq)
{
	int ret;

	memset(src_vq, 0, sizeof(*src_vq));
	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	src_vq->io_modes = VB2_MMAP | VB2_USERPTR;
	src_vq->drv_priv = priv;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->ops = &sh_veu_qops;
	src_vq->mem_ops = &vb2_dma_contig_memops;

	ret = vb2_queue_init(src_vq);
	if (ret < 0)
		return ret;

	memset(dst_vq, 0, sizeof(*dst_vq));
	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dst_vq->io_modes = VB2_MMAP | VB2_USERPTR;
	dst_vq->drv_priv = priv;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->ops = &sh_veu_qops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;

	return vb2_queue_init(dst_vq);
}

		/* ========== File operations ========== */

static int sh_veu_open(struct file *file)
{
	struct sh_veu_dev *veu = video_drvdata(file);
	struct sh_veu_file *veu_file;

	veu_file = kzalloc(sizeof(*veu_file), GFP_KERNEL);
	if (!veu_file)
		return -ENOMEM;

	veu_file->veu_dev = veu;
	veu_file->cfg_needed = true;

	file->private_data = veu_file;

	pm_runtime_get_sync(veu->dev);

	dev_dbg(veu->dev, "Created instance %p\n", veu_file);

	return 0;
}

static int sh_veu_release(struct file *file)
{
	struct sh_veu_dev *veu = video_drvdata(file);
	struct sh_veu_file *veu_file = file->private_data;

	dev_dbg(veu->dev, "Releasing instance %p\n", veu_file);

	if (veu_file == veu->capture) {
		veu->capture = NULL;
		vb2_queue_release(v4l2_m2m_get_vq(veu->m2m_ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE));
	}

	if (veu_file == veu->output) {
		veu->output = NULL;
		vb2_queue_release(v4l2_m2m_get_vq(veu->m2m_ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT));
	}

	if (!veu->output && !veu->capture && veu->m2m_ctx) {
		v4l2_m2m_ctx_release(veu->m2m_ctx);
		veu->m2m_ctx = NULL;
	}

	pm_runtime_put(veu->dev);

	kfree(veu_file);

	return 0;
}

static unsigned int sh_veu_poll(struct file *file,
				struct poll_table_struct *wait)
{
	struct sh_veu_file *veu_file = file->private_data;

	return v4l2_m2m_poll(file, veu_file->veu_dev->m2m_ctx, wait);
}

static int sh_veu_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct sh_veu_file *veu_file = file->private_data;

	return v4l2_m2m_mmap(file, veu_file->veu_dev->m2m_ctx, vma);
}

static const struct v4l2_file_operations sh_veu_fops = {
	.owner		= THIS_MODULE,
	.open		= sh_veu_open,
	.release	= sh_veu_release,
	.poll		= sh_veu_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= sh_veu_mmap,
};

static const struct video_device sh_veu_videodev = {
	.name		= "sh-veu",
	.fops		= &sh_veu_fops,
	.ioctl_ops	= &sh_veu_ioctl_ops,
	.minor		= -1,
	.release	= video_device_release_empty,
	.vfl_dir	= VFL_DIR_M2M,
};

static const struct v4l2_m2m_ops sh_veu_m2m_ops = {
	.device_run	= sh_veu_device_run,
	.job_abort	= sh_veu_job_abort,
};

static irqreturn_t sh_veu_bh(int irq, void *dev_id)
{
	struct sh_veu_dev *veu = dev_id;

	if (veu->xaction == MEM2MEM_DEF_TRANSLEN || veu->aborting) {
		v4l2_m2m_job_finish(veu->m2m_dev, veu->m2m_ctx);
		veu->xaction = 0;
	} else {
		sh_veu_device_run(veu);
	}

	return IRQ_HANDLED;
}

static irqreturn_t sh_veu_isr(int irq, void *dev_id)
{
	struct sh_veu_dev *veu = dev_id;
	struct vb2_buffer *dst;
	struct vb2_buffer *src;
	u32 status = sh_veu_reg_read(veu, VEU_EVTR);

	/* bundle read mode not used */
	if (!(status & 1))
		return IRQ_NONE;

	/* disable interrupt in VEU */
	sh_veu_reg_write(veu, VEU_EIER, 0);
	/* halt operation */
	sh_veu_reg_write(veu, VEU_STR, 0);
	/* ack int, write 0 to clear bits */
	sh_veu_reg_write(veu, VEU_EVTR, status & ~1);

	/* conversion completed */
	dst = v4l2_m2m_dst_buf_remove(veu->m2m_ctx);
	src = v4l2_m2m_src_buf_remove(veu->m2m_ctx);
	if (!src || !dst)
		return IRQ_NONE;

	spin_lock(&veu->lock);
	v4l2_m2m_buf_done(src, VB2_BUF_STATE_DONE);
	v4l2_m2m_buf_done(dst, VB2_BUF_STATE_DONE);
	spin_unlock(&veu->lock);

	veu->xaction++;

	return IRQ_WAKE_THREAD;
}

static int sh_veu_probe(struct platform_device *pdev)
{
	struct sh_veu_dev *veu;
	struct resource *reg_res;
	struct video_device *vdev;
	int irq, ret;

	reg_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);

	if (!reg_res || irq <= 0) {
		dev_err(&pdev->dev, "Insufficient VEU platform information.\n");
		return -ENODEV;
	}

	veu = devm_kzalloc(&pdev->dev, sizeof(*veu), GFP_KERNEL);
	if (!veu)
		return -ENOMEM;

	veu->is_2h = resource_size(reg_res) == 0x22c;

	veu->base = devm_ioremap_resource(&pdev->dev, reg_res);
	if (IS_ERR(veu->base))
		return PTR_ERR(veu->base);

	ret = devm_request_threaded_irq(&pdev->dev, irq, sh_veu_isr, sh_veu_bh,
					0, "veu", veu);
	if (ret < 0)
		return ret;

	ret = v4l2_device_register(&pdev->dev, &veu->v4l2_dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Error registering v4l2 device\n");
		return ret;
	}

	vdev = &veu->vdev;

	veu->alloc_ctx = vb2_dma_contig_init_ctx(&pdev->dev);
	if (IS_ERR(veu->alloc_ctx)) {
		ret = PTR_ERR(veu->alloc_ctx);
		goto einitctx;
	}

	*vdev = sh_veu_videodev;
	spin_lock_init(&veu->lock);
	mutex_init(&veu->fop_lock);
	vdev->lock = &veu->fop_lock;

	video_set_drvdata(vdev, veu);

	veu->dev	= &pdev->dev;
	veu->vfmt_out	= DEFAULT_OUT_VFMT;
	veu->vfmt_in	= DEFAULT_IN_VFMT;

	veu->m2m_dev = v4l2_m2m_init(&sh_veu_m2m_ops);
	if (IS_ERR(veu->m2m_dev)) {
		ret = PTR_ERR(veu->m2m_dev);
		v4l2_err(&veu->v4l2_dev, "Failed to init mem2mem device: %d\n", ret);
		goto em2minit;
	}

	pm_runtime_enable(&pdev->dev);
	pm_runtime_resume(&pdev->dev);

	ret = video_register_device(vdev, VFL_TYPE_GRABBER, -1);
	pm_runtime_suspend(&pdev->dev);
	if (ret < 0)
		goto evidreg;

	return ret;

evidreg:
	pm_runtime_disable(&pdev->dev);
	v4l2_m2m_release(veu->m2m_dev);
em2minit:
	vb2_dma_contig_cleanup_ctx(veu->alloc_ctx);
einitctx:
	v4l2_device_unregister(&veu->v4l2_dev);
	return ret;
}

static int sh_veu_remove(struct platform_device *pdev)
{
	struct v4l2_device *v4l2_dev = platform_get_drvdata(pdev);
	struct sh_veu_dev *veu = container_of(v4l2_dev,
					      struct sh_veu_dev, v4l2_dev);

	video_unregister_device(&veu->vdev);
	pm_runtime_disable(&pdev->dev);
	v4l2_m2m_release(veu->m2m_dev);
	vb2_dma_contig_cleanup_ctx(veu->alloc_ctx);
	v4l2_device_unregister(&veu->v4l2_dev);

	return 0;
}

static struct platform_driver __refdata sh_veu_pdrv = {
	.remove		= sh_veu_remove,
	.driver		= {
		.name	= "sh_veu",
		.owner	= THIS_MODULE,
	},
};

module_platform_driver_probe(sh_veu_pdrv, sh_veu_probe);

MODULE_DESCRIPTION("sh-mobile VEU mem2mem driver");
MODULE_AUTHOR("Guennadi Liakhovetski, <g.liakhovetski@gmx.de>");
MODULE_LICENSE("GPL v2");
