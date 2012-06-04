/* linux/drivers/media/video/s5p-jpeg/jpeg-core.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrzej Pietrasiewicz <andrzej.p@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include "jpeg-core.h"
#include "jpeg-hw.h"

static struct s5p_jpeg_fmt formats_enc[] = {
	{
		.name		= "JPEG JFIF",
		.fourcc		= V4L2_PIX_FMT_JPEG,
		.colplanes	= 1,
		.types		= MEM2MEM_CAPTURE,
	},
	{
		.name		= "YUV 4:2:2 packed, YCbYCr",
		.fourcc		= V4L2_PIX_FMT_YUYV,
		.depth		= 16,
		.colplanes	= 1,
		.types		= MEM2MEM_OUTPUT,
	},
	{
		.name		= "RGB565",
		.fourcc		= V4L2_PIX_FMT_RGB565,
		.depth		= 16,
		.colplanes	= 1,
		.types		= MEM2MEM_OUTPUT,
	},
};
#define NUM_FORMATS_ENC ARRAY_SIZE(formats_enc)

static struct s5p_jpeg_fmt formats_dec[] = {
	{
		.name		= "YUV 4:2:0 planar, YCbCr",
		.fourcc		= V4L2_PIX_FMT_YUV420,
		.depth		= 12,
		.colplanes	= 3,
		.h_align	= 4,
		.v_align	= 4,
		.types		= MEM2MEM_CAPTURE,
	},
	{
		.name		= "YUV 4:2:2 packed, YCbYCr",
		.fourcc		= V4L2_PIX_FMT_YUYV,
		.depth		= 16,
		.colplanes	= 1,
		.h_align	= 4,
		.v_align	= 3,
		.types		= MEM2MEM_CAPTURE,
	},
	{
		.name		= "JPEG JFIF",
		.fourcc		= V4L2_PIX_FMT_JPEG,
		.colplanes	= 1,
		.types		= MEM2MEM_OUTPUT,
	},
};
#define NUM_FORMATS_DEC ARRAY_SIZE(formats_dec)

static const unsigned char qtbl_luminance[4][64] = {
	{/* level 1 - high quality */
		 8,  6,  6,  8, 12, 14, 16, 17,
		 6,  6,  6,  8, 10, 13, 12, 15,
		 6,  6,  7,  8, 13, 14, 18, 24,
		 8,  8,  8, 14, 13, 19, 24, 35,
		12, 10, 13, 13, 20, 26, 34, 39,
		14, 13, 14, 19, 26, 34, 39, 39,
		16, 12, 18, 24, 34, 39, 39, 39,
		17, 15, 24, 35, 39, 39, 39, 39
	},
	{/* level 2 */
		12,  8,  8, 12, 17, 21, 24, 23,
		 8,  9,  9, 11, 15, 19, 18, 23,
		 8,  9, 10, 12, 19, 20, 27, 36,
		12, 11, 12, 21, 20, 28, 36, 53,
		17, 15, 19, 20, 30, 39, 51, 59,
		21, 19, 20, 28, 39, 51, 59, 59,
		24, 18, 27, 36, 51, 59, 59, 59,
		23, 23, 36, 53, 59, 59, 59, 59
	},
	{/* level 3 */
		16, 11, 11, 16, 23, 27, 31, 30,
		11, 12, 12, 15, 20, 23, 23, 30,
		11, 12, 13, 16, 23, 26, 35, 47,
		16, 15, 16, 23, 26, 37, 47, 64,
		23, 20, 23, 26, 39, 51, 64, 64,
		27, 23, 26, 37, 51, 64, 64, 64,
		31, 23, 35, 47, 64, 64, 64, 64,
		30, 30, 47, 64, 64, 64, 64, 64
	},
	{/*level 4 - low quality */
		20, 16, 25, 39, 50, 46, 62, 68,
		16, 18, 23, 38, 38, 53, 65, 68,
		25, 23, 31, 38, 53, 65, 68, 68,
		39, 38, 38, 53, 65, 68, 68, 68,
		50, 38, 53, 65, 68, 68, 68, 68,
		46, 53, 65, 68, 68, 68, 68, 68,
		62, 65, 68, 68, 68, 68, 68, 68,
		68, 68, 68, 68, 68, 68, 68, 68
	}
};

static const unsigned char qtbl_chrominance[4][64] = {
	{/* level 1 - high quality */
		 9,  8,  9, 11, 14, 17, 19, 24,
		 8, 10,  9, 11, 14, 13, 17, 22,
		 9,  9, 13, 14, 13, 15, 23, 26,
		11, 11, 14, 14, 15, 20, 26, 33,
		14, 14, 13, 15, 20, 24, 33, 39,
		17, 13, 15, 20, 24, 32, 39, 39,
		19, 17, 23, 26, 33, 39, 39, 39,
		24, 22, 26, 33, 39, 39, 39, 39
	},
	{/* level 2 */
		13, 11, 13, 16, 20, 20, 29, 37,
		11, 14, 14, 14, 16, 20, 26, 32,
		13, 14, 15, 17, 20, 23, 35, 40,
		16, 14, 17, 21, 23, 30, 40, 50,
		20, 16, 20, 23, 30, 37, 50, 59,
		20, 20, 23, 30, 37, 48, 59, 59,
		29, 26, 35, 40, 50, 59, 59, 59,
		37, 32, 40, 50, 59, 59, 59, 59
	},
	{/* level 3 */
		17, 15, 17, 21, 20, 26, 38, 48,
		15, 19, 18, 17, 20, 26, 35, 43,
		17, 18, 20, 22, 26, 30, 46, 53,
		21, 17, 22, 28, 30, 39, 53, 64,
		20, 20, 26, 30, 39, 48, 64, 64,
		26, 26, 30, 39, 48, 63, 64, 64,
		38, 35, 46, 53, 64, 64, 64, 64,
		48, 43, 53, 64, 64, 64, 64, 64
	},
	{/*level 4 - low quality */
		21, 25, 32, 38, 54, 68, 68, 68,
		25, 28, 24, 38, 54, 68, 68, 68,
		32, 24, 32, 43, 66, 68, 68, 68,
		38, 38, 43, 53, 68, 68, 68, 68,
		54, 54, 66, 68, 68, 68, 68, 68,
		68, 68, 68, 68, 68, 68, 68, 68,
		68, 68, 68, 68, 68, 68, 68, 68,
		68, 68, 68, 68, 68, 68, 68, 68
	}
};

static const unsigned char hdctbl0[16] = {
	0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0
};

static const unsigned char hdctblg0[12] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb
};
static const unsigned char hactbl0[16] = {
	0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d
};
static const unsigned char hactblg0[162] = {
	0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
	0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
	0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
	0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
	0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16,
	0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
	0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
	0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
	0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
	0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
	0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
	0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
	0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
	0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
	0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
	0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
	0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
	0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
	0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
	0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
	0xf9, 0xfa
};

static inline struct s5p_jpeg_ctx *ctrl_to_ctx(struct v4l2_ctrl *c)
{
	return container_of(c->handler, struct s5p_jpeg_ctx, ctrl_handler);
}

static inline struct s5p_jpeg_ctx *fh_to_ctx(struct v4l2_fh *fh)
{
	return container_of(fh, struct s5p_jpeg_ctx, fh);
}

static inline void jpeg_set_qtbl(void __iomem *regs, const unsigned char *qtbl,
		   unsigned long tab, int len)
{
	int i;

	for (i = 0; i < len; i++)
		writel((unsigned int)qtbl[i], regs + tab + (i * 0x04));
}

static inline void jpeg_set_qtbl_lum(void __iomem *regs, int quality)
{
	/* this driver fills quantisation table 0 with data for luma */
	jpeg_set_qtbl(regs, qtbl_luminance[quality], S5P_JPG_QTBL_CONTENT(0),
		      ARRAY_SIZE(qtbl_luminance[quality]));
}

static inline void jpeg_set_qtbl_chr(void __iomem *regs, int quality)
{
	/* this driver fills quantisation table 1 with data for chroma */
	jpeg_set_qtbl(regs, qtbl_chrominance[quality], S5P_JPG_QTBL_CONTENT(1),
		      ARRAY_SIZE(qtbl_chrominance[quality]));
}

static inline void jpeg_set_htbl(void __iomem *regs, const unsigned char *htbl,
		   unsigned long tab, int len)
{
	int i;

	for (i = 0; i < len; i++)
		writel((unsigned int)htbl[i], regs + tab + (i * 0x04));
}

static inline void jpeg_set_hdctbl(void __iomem *regs)
{
	/* this driver fills table 0 for this component */
	jpeg_set_htbl(regs, hdctbl0, S5P_JPG_HDCTBL(0), ARRAY_SIZE(hdctbl0));
}

static inline void jpeg_set_hdctblg(void __iomem *regs)
{
	/* this driver fills table 0 for this component */
	jpeg_set_htbl(regs, hdctblg0, S5P_JPG_HDCTBLG(0), ARRAY_SIZE(hdctblg0));
}

static inline void jpeg_set_hactbl(void __iomem *regs)
{
	/* this driver fills table 0 for this component */
	jpeg_set_htbl(regs, hactbl0, S5P_JPG_HACTBL(0), ARRAY_SIZE(hactbl0));
}

static inline void jpeg_set_hactblg(void __iomem *regs)
{
	/* this driver fills table 0 for this component */
	jpeg_set_htbl(regs, hactblg0, S5P_JPG_HACTBLG(0), ARRAY_SIZE(hactblg0));
}

/*
 * ============================================================================
 * Device file operations
 * ============================================================================
 */

static int queue_init(void *priv, struct vb2_queue *src_vq,
		      struct vb2_queue *dst_vq);
static struct s5p_jpeg_fmt *s5p_jpeg_find_format(unsigned int mode,
						 __u32 pixelformat);
static int s5p_jpeg_controls_create(struct s5p_jpeg_ctx *ctx);

static int s5p_jpeg_open(struct file *file)
{
	struct s5p_jpeg *jpeg = video_drvdata(file);
	struct video_device *vfd = video_devdata(file);
	struct s5p_jpeg_ctx *ctx;
	struct s5p_jpeg_fmt *out_fmt;
	int ret = 0;

	ctx = kzalloc(sizeof *ctx, GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	v4l2_fh_init(&ctx->fh, vfd);
	/* Use separate control handler per file handle */
	ctx->fh.ctrl_handler = &ctx->ctrl_handler;
	file->private_data = &ctx->fh;
	v4l2_fh_add(&ctx->fh);

	ctx->jpeg = jpeg;
	if (vfd == jpeg->vfd_encoder) {
		ctx->mode = S5P_JPEG_ENCODE;
		out_fmt = s5p_jpeg_find_format(ctx->mode, V4L2_PIX_FMT_RGB565);
	} else {
		ctx->mode = S5P_JPEG_DECODE;
		out_fmt = s5p_jpeg_find_format(ctx->mode, V4L2_PIX_FMT_JPEG);
	}

	ret = s5p_jpeg_controls_create(ctx);
	if (ret < 0)
		goto error;

	ctx->m2m_ctx = v4l2_m2m_ctx_init(jpeg->m2m_dev, ctx, queue_init);
	if (IS_ERR(ctx->m2m_ctx)) {
		ret = PTR_ERR(ctx->m2m_ctx);
		goto error;
	}

	ctx->out_q.fmt = out_fmt;
	ctx->cap_q.fmt = s5p_jpeg_find_format(ctx->mode, V4L2_PIX_FMT_YUYV);
	return 0;

error:
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	kfree(ctx);
	return ret;
}

static int s5p_jpeg_release(struct file *file)
{
	struct s5p_jpeg_ctx *ctx = fh_to_ctx(file->private_data);

	v4l2_m2m_ctx_release(ctx->m2m_ctx);
	v4l2_ctrl_handler_free(&ctx->ctrl_handler);
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	kfree(ctx);

	return 0;
}

static unsigned int s5p_jpeg_poll(struct file *file,
				 struct poll_table_struct *wait)
{
	struct s5p_jpeg_ctx *ctx = fh_to_ctx(file->private_data);

	return v4l2_m2m_poll(file, ctx->m2m_ctx, wait);
}

static int s5p_jpeg_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct s5p_jpeg_ctx *ctx = fh_to_ctx(file->private_data);

	return v4l2_m2m_mmap(file, ctx->m2m_ctx, vma);
}

static const struct v4l2_file_operations s5p_jpeg_fops = {
	.owner		= THIS_MODULE,
	.open		= s5p_jpeg_open,
	.release	= s5p_jpeg_release,
	.poll		= s5p_jpeg_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= s5p_jpeg_mmap,
};

/*
 * ============================================================================
 * video ioctl operations
 * ============================================================================
 */

static int get_byte(struct s5p_jpeg_buffer *buf)
{
	if (buf->curr >= buf->size)
		return -1;

	return ((unsigned char *)buf->data)[buf->curr++];
}

static int get_word_be(struct s5p_jpeg_buffer *buf, unsigned int *word)
{
	unsigned int temp;
	int byte;

	byte = get_byte(buf);
	if (byte == -1)
		return -1;
	temp = byte << 8;
	byte = get_byte(buf);
	if (byte == -1)
		return -1;
	*word = (unsigned int)byte | temp;
	return 0;
}

static void skip(struct s5p_jpeg_buffer *buf, long len)
{
	if (len <= 0)
		return;

	while (len--)
		get_byte(buf);
}

static bool s5p_jpeg_parse_hdr(struct s5p_jpeg_q_data *result,
			       unsigned long buffer, unsigned long size)
{
	int c, components, notfound;
	unsigned int height, width, word;
	long length;
	struct s5p_jpeg_buffer jpeg_buffer;

	jpeg_buffer.size = size;
	jpeg_buffer.data = buffer;
	jpeg_buffer.curr = 0;

	notfound = 1;
	while (notfound) {
		c = get_byte(&jpeg_buffer);
		if (c == -1)
			break;
		if (c != 0xff)
			continue;
		do
			c = get_byte(&jpeg_buffer);
		while (c == 0xff);
		if (c == -1)
			break;
		if (c == 0)
			continue;
		length = 0;
		switch (c) {
		/* SOF0: baseline JPEG */
		case SOF0:
			if (get_word_be(&jpeg_buffer, &word))
				break;
			if (get_byte(&jpeg_buffer) == -1)
				break;
			if (get_word_be(&jpeg_buffer, &height))
				break;
			if (get_word_be(&jpeg_buffer, &width))
				break;
			components = get_byte(&jpeg_buffer);
			if (components == -1)
				break;
			notfound = 0;

			skip(&jpeg_buffer, components * 3);
			break;

		/* skip payload-less markers */
		case RST ... RST + 7:
		case SOI:
		case EOI:
		case TEM:
			break;

		/* skip uninteresting payload markers */
		default:
			if (get_word_be(&jpeg_buffer, &word))
				break;
			length = (long)word - 2;
			skip(&jpeg_buffer, length);
			break;
		}
	}
	result->w = width;
	result->h = height;
	result->size = components;
	return !notfound;
}

static int s5p_jpeg_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	struct s5p_jpeg_ctx *ctx = fh_to_ctx(priv);

	if (ctx->mode == S5P_JPEG_ENCODE) {
		strlcpy(cap->driver, S5P_JPEG_M2M_NAME " encoder",
			sizeof(cap->driver));
		strlcpy(cap->card, S5P_JPEG_M2M_NAME " encoder",
			sizeof(cap->card));
	} else {
		strlcpy(cap->driver, S5P_JPEG_M2M_NAME " decoder",
			sizeof(cap->driver));
		strlcpy(cap->card, S5P_JPEG_M2M_NAME " decoder",
			sizeof(cap->card));
	}
	cap->bus_info[0] = 0;
	cap->capabilities = V4L2_CAP_STREAMING |
			    V4L2_CAP_VIDEO_CAPTURE |
			    V4L2_CAP_VIDEO_OUTPUT;
	return 0;
}

static int enum_fmt(struct s5p_jpeg_fmt *formats, int n,
		    struct v4l2_fmtdesc *f, u32 type)
{
	int i, num = 0;

	for (i = 0; i < n; ++i) {
		if (formats[i].types & type) {
			/* index-th format of type type found ? */
			if (num == f->index)
				break;
			/* Correct type but haven't reached our index yet,
			 * just increment per-type index */
			++num;
		}
	}

	/* Format not found */
	if (i >= n)
		return -EINVAL;

	strlcpy(f->description, formats[i].name, sizeof(f->description));
	f->pixelformat = formats[i].fourcc;

	return 0;
}

static int s5p_jpeg_enum_fmt_vid_cap(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	struct s5p_jpeg_ctx *ctx = fh_to_ctx(priv);

	if (ctx->mode == S5P_JPEG_ENCODE)
		return enum_fmt(formats_enc, NUM_FORMATS_ENC, f,
				MEM2MEM_CAPTURE);

	return enum_fmt(formats_dec, NUM_FORMATS_DEC, f, MEM2MEM_CAPTURE);
}

static int s5p_jpeg_enum_fmt_vid_out(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	struct s5p_jpeg_ctx *ctx = fh_to_ctx(priv);

	if (ctx->mode == S5P_JPEG_ENCODE)
		return enum_fmt(formats_enc, NUM_FORMATS_ENC, f,
				MEM2MEM_OUTPUT);

	return enum_fmt(formats_dec, NUM_FORMATS_DEC, f, MEM2MEM_OUTPUT);
}

static struct s5p_jpeg_q_data *get_q_data(struct s5p_jpeg_ctx *ctx,
					  enum v4l2_buf_type type)
{
	if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return &ctx->out_q;
	if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return &ctx->cap_q;

	return NULL;
}

static int s5p_jpeg_g_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct vb2_queue *vq;
	struct s5p_jpeg_q_data *q_data = NULL;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct s5p_jpeg_ctx *ct = fh_to_ctx(priv);

	vq = v4l2_m2m_get_vq(ct->m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE &&
	    ct->mode == S5P_JPEG_DECODE && !ct->hdr_parsed)
		return -EINVAL;
	q_data = get_q_data(ct, f->type);
	BUG_ON(q_data == NULL);

	pix->width = q_data->w;
	pix->height = q_data->h;
	pix->field = V4L2_FIELD_NONE;
	pix->pixelformat = q_data->fmt->fourcc;
	pix->bytesperline = 0;
	if (q_data->fmt->fourcc != V4L2_PIX_FMT_JPEG) {
		u32 bpl = q_data->w;
		if (q_data->fmt->colplanes == 1)
			bpl = (bpl * q_data->fmt->depth) >> 3;
		pix->bytesperline = bpl;
	}
	pix->sizeimage = q_data->size;

	return 0;
}

static struct s5p_jpeg_fmt *s5p_jpeg_find_format(unsigned int mode,
						 u32 pixelformat)
{
	unsigned int k;
	struct s5p_jpeg_fmt *formats;
	int n;

	if (mode == S5P_JPEG_ENCODE) {
		formats = formats_enc;
		n = NUM_FORMATS_ENC;
	} else {
		formats = formats_dec;
		n = NUM_FORMATS_DEC;
	}

	for (k = 0; k < n; k++) {
		struct s5p_jpeg_fmt *fmt = &formats[k];
		if (fmt->fourcc == pixelformat)
			return fmt;
	}

	return NULL;

}

static void jpeg_bound_align_image(u32 *w, unsigned int wmin, unsigned int wmax,
				   unsigned int walign,
				   u32 *h, unsigned int hmin, unsigned int hmax,
				   unsigned int halign)
{
	int width, height, w_step, h_step;

	width = *w;
	height = *h;

	w_step = 1 << walign;
	h_step = 1 << halign;
	v4l_bound_align_image(w, wmin, wmax, walign, h, hmin, hmax, halign, 0);

	if (*w < width && (*w + w_step) < wmax)
		*w += w_step;
	if (*h < height && (*h + h_step) < hmax)
		*h += h_step;

}

static int vidioc_try_fmt(struct v4l2_format *f, struct s5p_jpeg_fmt *fmt,
			  struct s5p_jpeg_ctx *ctx, int q_type)
{
	struct v4l2_pix_format *pix = &f->fmt.pix;

	if (pix->field == V4L2_FIELD_ANY)
		pix->field = V4L2_FIELD_NONE;
	else if (pix->field != V4L2_FIELD_NONE)
		return -EINVAL;

	/* V4L2 specification suggests the driver corrects the format struct
	 * if any of the dimensions is unsupported */
	if (q_type == MEM2MEM_OUTPUT)
		jpeg_bound_align_image(&pix->width, S5P_JPEG_MIN_WIDTH,
				       S5P_JPEG_MAX_WIDTH, 0,
				       &pix->height, S5P_JPEG_MIN_HEIGHT,
				       S5P_JPEG_MAX_HEIGHT, 0);
	else
		jpeg_bound_align_image(&pix->width, S5P_JPEG_MIN_WIDTH,
				       S5P_JPEG_MAX_WIDTH, fmt->h_align,
				       &pix->height, S5P_JPEG_MIN_HEIGHT,
				       S5P_JPEG_MAX_HEIGHT, fmt->v_align);

	if (fmt->fourcc == V4L2_PIX_FMT_JPEG) {
		if (pix->sizeimage <= 0)
			pix->sizeimage = PAGE_SIZE;
		pix->bytesperline = 0;
	} else {
		u32 bpl = pix->bytesperline;

		if (fmt->colplanes > 1 && bpl < pix->width)
			bpl = pix->width; /* planar */

		if (fmt->colplanes == 1 && /* packed */
		    (bpl << 3) * fmt->depth < pix->width)
			bpl = (pix->width * fmt->depth) >> 3;

		pix->bytesperline = bpl;
		pix->sizeimage = (pix->width * pix->height * fmt->depth) >> 3;
	}

	return 0;
}

static int s5p_jpeg_try_fmt_vid_cap(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct s5p_jpeg_ctx *ctx = fh_to_ctx(priv);
	struct s5p_jpeg_fmt *fmt;

	fmt = s5p_jpeg_find_format(ctx->mode, f->fmt.pix.pixelformat);
	if (!fmt || !(fmt->types & MEM2MEM_CAPTURE)) {
		v4l2_err(&ctx->jpeg->v4l2_dev,
			 "Fourcc format (0x%08x) invalid.\n",
			 f->fmt.pix.pixelformat);
		return -EINVAL;
	}

	return vidioc_try_fmt(f, fmt, ctx, MEM2MEM_CAPTURE);
}

static int s5p_jpeg_try_fmt_vid_out(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct s5p_jpeg_ctx *ctx = fh_to_ctx(priv);
	struct s5p_jpeg_fmt *fmt;

	fmt = s5p_jpeg_find_format(ctx->mode, f->fmt.pix.pixelformat);
	if (!fmt || !(fmt->types & MEM2MEM_OUTPUT)) {
		v4l2_err(&ctx->jpeg->v4l2_dev,
			 "Fourcc format (0x%08x) invalid.\n",
			 f->fmt.pix.pixelformat);
		return -EINVAL;
	}

	return vidioc_try_fmt(f, fmt, ctx, MEM2MEM_OUTPUT);
}

static int s5p_jpeg_s_fmt(struct s5p_jpeg_ctx *ct, struct v4l2_format *f)
{
	struct vb2_queue *vq;
	struct s5p_jpeg_q_data *q_data = NULL;
	struct v4l2_pix_format *pix = &f->fmt.pix;

	vq = v4l2_m2m_get_vq(ct->m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	q_data = get_q_data(ct, f->type);
	BUG_ON(q_data == NULL);

	if (vb2_is_busy(vq)) {
		v4l2_err(&ct->jpeg->v4l2_dev, "%s queue busy\n", __func__);
		return -EBUSY;
	}

	q_data->fmt = s5p_jpeg_find_format(ct->mode, pix->pixelformat);
	q_data->w = pix->width;
	q_data->h = pix->height;
	if (q_data->fmt->fourcc != V4L2_PIX_FMT_JPEG)
		q_data->size = q_data->w * q_data->h * q_data->fmt->depth >> 3;
	else
		q_data->size = pix->sizeimage;

	return 0;
}

static int s5p_jpeg_s_fmt_vid_cap(struct file *file, void *priv,
				struct v4l2_format *f)
{
	int ret;

	ret = s5p_jpeg_try_fmt_vid_cap(file, priv, f);
	if (ret)
		return ret;

	return s5p_jpeg_s_fmt(fh_to_ctx(priv), f);
}

static int s5p_jpeg_s_fmt_vid_out(struct file *file, void *priv,
				struct v4l2_format *f)
{
	int ret;

	ret = s5p_jpeg_try_fmt_vid_out(file, priv, f);
	if (ret)
		return ret;

	return s5p_jpeg_s_fmt(fh_to_ctx(priv), f);
}

static int s5p_jpeg_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *reqbufs)
{
	struct s5p_jpeg_ctx *ctx = fh_to_ctx(priv);

	return v4l2_m2m_reqbufs(file, ctx->m2m_ctx, reqbufs);
}

static int s5p_jpeg_querybuf(struct file *file, void *priv,
			   struct v4l2_buffer *buf)
{
	struct s5p_jpeg_ctx *ctx = fh_to_ctx(priv);

	return v4l2_m2m_querybuf(file, ctx->m2m_ctx, buf);
}

static int s5p_jpeg_qbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct s5p_jpeg_ctx *ctx = fh_to_ctx(priv);

	return v4l2_m2m_qbuf(file, ctx->m2m_ctx, buf);
}

static int s5p_jpeg_dqbuf(struct file *file, void *priv,
			  struct v4l2_buffer *buf)
{
	struct s5p_jpeg_ctx *ctx = fh_to_ctx(priv);

	return v4l2_m2m_dqbuf(file, ctx->m2m_ctx, buf);
}

static int s5p_jpeg_streamon(struct file *file, void *priv,
			   enum v4l2_buf_type type)
{
	struct s5p_jpeg_ctx *ctx = fh_to_ctx(priv);

	return v4l2_m2m_streamon(file, ctx->m2m_ctx, type);
}

static int s5p_jpeg_streamoff(struct file *file, void *priv,
			    enum v4l2_buf_type type)
{
	struct s5p_jpeg_ctx *ctx = fh_to_ctx(priv);

	return v4l2_m2m_streamoff(file, ctx->m2m_ctx, type);
}

static int s5p_jpeg_g_selection(struct file *file, void *priv,
			 struct v4l2_selection *s)
{
	struct s5p_jpeg_ctx *ctx = fh_to_ctx(priv);

	if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT &&
	    s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	/* For JPEG blob active == default == bounds */
	switch (s->target) {
	case V4L2_SEL_TGT_CROP_ACTIVE:
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE_ACTIVE:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
		s->r.width = ctx->out_q.w;
		s->r.height = ctx->out_q.h;
		break;
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
	case V4L2_SEL_TGT_COMPOSE_PADDED:
		s->r.width = ctx->cap_q.w;
		s->r.height = ctx->cap_q.h;
		break;
	default:
		return -EINVAL;
	}
	s->r.left = 0;
	s->r.top = 0;
	return 0;
}

/*
 * V4L2 controls
 */

static int s5p_jpeg_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct s5p_jpeg_ctx *ctx = ctrl_to_ctx(ctrl);
	struct s5p_jpeg *jpeg = ctx->jpeg;
	unsigned long flags;

	switch (ctrl->id) {
	case V4L2_CID_JPEG_CHROMA_SUBSAMPLING:
		spin_lock_irqsave(&jpeg->slock, flags);

		WARN_ON(ctx->subsampling > S5P_SUBSAMPLING_MODE_GRAY);
		if (ctx->subsampling > 2)
			ctrl->val = V4L2_JPEG_CHROMA_SUBSAMPLING_GRAY;
		else
			ctrl->val = ctx->subsampling;
		spin_unlock_irqrestore(&jpeg->slock, flags);
		break;
	}

	return 0;
}

static int s5p_jpeg_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct s5p_jpeg_ctx *ctx = ctrl_to_ctx(ctrl);
	unsigned long flags;

	spin_lock_irqsave(&ctx->jpeg->slock, flags);

	switch (ctrl->id) {
	case V4L2_CID_JPEG_COMPRESSION_QUALITY:
		ctx->compr_quality = S5P_JPEG_COMPR_QUAL_WORST - ctrl->val;
		break;
	case V4L2_CID_JPEG_RESTART_INTERVAL:
		ctx->restart_interval = ctrl->val;
		break;
	case V4L2_CID_JPEG_CHROMA_SUBSAMPLING:
		ctx->subsampling = ctrl->val;
		break;
	}

	spin_unlock_irqrestore(&ctx->jpeg->slock, flags);
	return 0;
}

static const struct v4l2_ctrl_ops s5p_jpeg_ctrl_ops = {
	.g_volatile_ctrl	= s5p_jpeg_g_volatile_ctrl,
	.s_ctrl			= s5p_jpeg_s_ctrl,
};

static int s5p_jpeg_controls_create(struct s5p_jpeg_ctx *ctx)
{
	unsigned int mask = ~0x27; /* 444, 422, 420, GRAY */
	struct v4l2_ctrl *ctrl;

	v4l2_ctrl_handler_init(&ctx->ctrl_handler, 3);

	if (ctx->mode == S5P_JPEG_ENCODE) {
		v4l2_ctrl_new_std(&ctx->ctrl_handler, &s5p_jpeg_ctrl_ops,
				  V4L2_CID_JPEG_COMPRESSION_QUALITY,
				  0, 3, 1, 3);

		v4l2_ctrl_new_std(&ctx->ctrl_handler, &s5p_jpeg_ctrl_ops,
				  V4L2_CID_JPEG_RESTART_INTERVAL,
				  0, 3, 0xffff, 0);
		mask = ~0x06; /* 422, 420 */
	}

	ctrl = v4l2_ctrl_new_std_menu(&ctx->ctrl_handler, &s5p_jpeg_ctrl_ops,
				      V4L2_CID_JPEG_CHROMA_SUBSAMPLING,
				      V4L2_JPEG_CHROMA_SUBSAMPLING_GRAY, mask,
				      V4L2_JPEG_CHROMA_SUBSAMPLING_422);

	if (ctx->ctrl_handler.error)
		return ctx->ctrl_handler.error;

	if (ctx->mode == S5P_JPEG_DECODE)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE |
			V4L2_CTRL_FLAG_READ_ONLY;
	return 0;
}

static const struct v4l2_ioctl_ops s5p_jpeg_ioctl_ops = {
	.vidioc_querycap		= s5p_jpeg_querycap,

	.vidioc_enum_fmt_vid_cap	= s5p_jpeg_enum_fmt_vid_cap,
	.vidioc_enum_fmt_vid_out	= s5p_jpeg_enum_fmt_vid_out,

	.vidioc_g_fmt_vid_cap		= s5p_jpeg_g_fmt,
	.vidioc_g_fmt_vid_out		= s5p_jpeg_g_fmt,

	.vidioc_try_fmt_vid_cap		= s5p_jpeg_try_fmt_vid_cap,
	.vidioc_try_fmt_vid_out		= s5p_jpeg_try_fmt_vid_out,

	.vidioc_s_fmt_vid_cap		= s5p_jpeg_s_fmt_vid_cap,
	.vidioc_s_fmt_vid_out		= s5p_jpeg_s_fmt_vid_out,

	.vidioc_reqbufs			= s5p_jpeg_reqbufs,
	.vidioc_querybuf		= s5p_jpeg_querybuf,

	.vidioc_qbuf			= s5p_jpeg_qbuf,
	.vidioc_dqbuf			= s5p_jpeg_dqbuf,

	.vidioc_streamon		= s5p_jpeg_streamon,
	.vidioc_streamoff		= s5p_jpeg_streamoff,

	.vidioc_g_selection		= s5p_jpeg_g_selection,
};

/*
 * ============================================================================
 * mem2mem callbacks
 * ============================================================================
 */

static void s5p_jpeg_device_run(void *priv)
{
	struct s5p_jpeg_ctx *ctx = priv;
	struct s5p_jpeg *jpeg = ctx->jpeg;
	struct vb2_buffer *src_buf, *dst_buf;
	unsigned long src_addr, dst_addr;

	src_buf = v4l2_m2m_next_src_buf(ctx->m2m_ctx);
	dst_buf = v4l2_m2m_next_dst_buf(ctx->m2m_ctx);
	src_addr = vb2_dma_contig_plane_dma_addr(src_buf, 0);
	dst_addr = vb2_dma_contig_plane_dma_addr(dst_buf, 0);

	jpeg_reset(jpeg->regs);
	jpeg_poweron(jpeg->regs);
	jpeg_proc_mode(jpeg->regs, ctx->mode);
	if (ctx->mode == S5P_JPEG_ENCODE) {
		if (ctx->out_q.fmt->fourcc == V4L2_PIX_FMT_RGB565)
			jpeg_input_raw_mode(jpeg->regs, S5P_JPEG_RAW_IN_565);
		else
			jpeg_input_raw_mode(jpeg->regs, S5P_JPEG_RAW_IN_422);
		jpeg_subsampling_mode(jpeg->regs, ctx->subsampling);
		jpeg_dri(jpeg->regs, ctx->restart_interval);
		jpeg_x(jpeg->regs, ctx->out_q.w);
		jpeg_y(jpeg->regs, ctx->out_q.h);
		jpeg_imgadr(jpeg->regs, src_addr);
		jpeg_jpgadr(jpeg->regs, dst_addr);

		/* ultimately comes from sizeimage from userspace */
		jpeg_enc_stream_int(jpeg->regs, ctx->cap_q.size);

		/* JPEG RGB to YCbCr conversion matrix */
		jpeg_coef(jpeg->regs, 1, 1, S5P_JPEG_COEF11);
		jpeg_coef(jpeg->regs, 1, 2, S5P_JPEG_COEF12);
		jpeg_coef(jpeg->regs, 1, 3, S5P_JPEG_COEF13);
		jpeg_coef(jpeg->regs, 2, 1, S5P_JPEG_COEF21);
		jpeg_coef(jpeg->regs, 2, 2, S5P_JPEG_COEF22);
		jpeg_coef(jpeg->regs, 2, 3, S5P_JPEG_COEF23);
		jpeg_coef(jpeg->regs, 3, 1, S5P_JPEG_COEF31);
		jpeg_coef(jpeg->regs, 3, 2, S5P_JPEG_COEF32);
		jpeg_coef(jpeg->regs, 3, 3, S5P_JPEG_COEF33);

		/*
		 * JPEG IP allows storing 4 quantization tables
		 * We fill table 0 for luma and table 1 for chroma
		 */
		jpeg_set_qtbl_lum(jpeg->regs, ctx->compr_quality);
		jpeg_set_qtbl_chr(jpeg->regs, ctx->compr_quality);
		/* use table 0 for Y */
		jpeg_qtbl(jpeg->regs, 1, 0);
		/* use table 1 for Cb and Cr*/
		jpeg_qtbl(jpeg->regs, 2, 1);
		jpeg_qtbl(jpeg->regs, 3, 1);

		/* Y, Cb, Cr use Huffman table 0 */
		jpeg_htbl_ac(jpeg->regs, 1);
		jpeg_htbl_dc(jpeg->regs, 1);
		jpeg_htbl_ac(jpeg->regs, 2);
		jpeg_htbl_dc(jpeg->regs, 2);
		jpeg_htbl_ac(jpeg->regs, 3);
		jpeg_htbl_dc(jpeg->regs, 3);
	} else { /* S5P_JPEG_DECODE */
		jpeg_rst_int_enable(jpeg->regs, true);
		jpeg_data_num_int_enable(jpeg->regs, true);
		jpeg_final_mcu_num_int_enable(jpeg->regs, true);
		if (ctx->cap_q.fmt->fourcc == V4L2_PIX_FMT_YUYV)
			jpeg_outform_raw(jpeg->regs, S5P_JPEG_RAW_OUT_422);
		else
			jpeg_outform_raw(jpeg->regs, S5P_JPEG_RAW_OUT_420);
		jpeg_jpgadr(jpeg->regs, src_addr);
		jpeg_imgadr(jpeg->regs, dst_addr);
	}

	jpeg_start(jpeg->regs);
}

static int s5p_jpeg_job_ready(void *priv)
{
	struct s5p_jpeg_ctx *ctx = priv;

	if (ctx->mode == S5P_JPEG_DECODE)
		return ctx->hdr_parsed;
	return 1;
}

static void s5p_jpeg_job_abort(void *priv)
{
}

static struct v4l2_m2m_ops s5p_jpeg_m2m_ops = {
	.device_run	= s5p_jpeg_device_run,
	.job_ready	= s5p_jpeg_job_ready,
	.job_abort	= s5p_jpeg_job_abort,
};

/*
 * ============================================================================
 * Queue operations
 * ============================================================================
 */

static int s5p_jpeg_queue_setup(struct vb2_queue *vq,
			   const struct v4l2_format *fmt,
			   unsigned int *nbuffers, unsigned int *nplanes,
			   unsigned int sizes[], void *alloc_ctxs[])
{
	struct s5p_jpeg_ctx *ctx = vb2_get_drv_priv(vq);
	struct s5p_jpeg_q_data *q_data = NULL;
	unsigned int size, count = *nbuffers;

	q_data = get_q_data(ctx, vq->type);
	BUG_ON(q_data == NULL);

	size = q_data->size;

	/*
	 * header is parsed during decoding and parsed information stored
	 * in the context so we do not allow another buffer to overwrite it
	 */
	if (ctx->mode == S5P_JPEG_DECODE)
		count = 1;

	*nbuffers = count;
	*nplanes = 1;
	sizes[0] = size;
	alloc_ctxs[0] = ctx->jpeg->alloc_ctx;

	return 0;
}

static int s5p_jpeg_buf_prepare(struct vb2_buffer *vb)
{
	struct s5p_jpeg_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct s5p_jpeg_q_data *q_data = NULL;

	q_data = get_q_data(ctx, vb->vb2_queue->type);
	BUG_ON(q_data == NULL);

	if (vb2_plane_size(vb, 0) < q_data->size) {
		pr_err("%s data will not fit into plane (%lu < %lu)\n",
				__func__, vb2_plane_size(vb, 0),
				(long)q_data->size);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, q_data->size);

	return 0;
}

static void s5p_jpeg_buf_queue(struct vb2_buffer *vb)
{
	struct s5p_jpeg_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	if (ctx->mode == S5P_JPEG_DECODE &&
	    vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		struct s5p_jpeg_q_data tmp, *q_data;
		ctx->hdr_parsed = s5p_jpeg_parse_hdr(&tmp,
		     (unsigned long)vb2_plane_vaddr(vb, 0),
		     min((unsigned long)ctx->out_q.size,
			 vb2_get_plane_payload(vb, 0)));
		if (!ctx->hdr_parsed) {
			vb2_buffer_done(vb, VB2_BUF_STATE_ERROR);
			return;
		}

		q_data = &ctx->out_q;
		q_data->w = tmp.w;
		q_data->h = tmp.h;

		q_data = &ctx->cap_q;
		q_data->w = tmp.w;
		q_data->h = tmp.h;

		jpeg_bound_align_image(&q_data->w, S5P_JPEG_MIN_WIDTH,
				       S5P_JPEG_MAX_WIDTH, q_data->fmt->h_align,
				       &q_data->h, S5P_JPEG_MIN_HEIGHT,
				       S5P_JPEG_MAX_HEIGHT, q_data->fmt->v_align
				      );
		q_data->size = q_data->w * q_data->h * q_data->fmt->depth >> 3;
	}
	if (ctx->m2m_ctx)
		v4l2_m2m_buf_queue(ctx->m2m_ctx, vb);
}

static void s5p_jpeg_wait_prepare(struct vb2_queue *vq)
{
	struct s5p_jpeg_ctx *ctx = vb2_get_drv_priv(vq);

	mutex_unlock(&ctx->jpeg->lock);
}

static void s5p_jpeg_wait_finish(struct vb2_queue *vq)
{
	struct s5p_jpeg_ctx *ctx = vb2_get_drv_priv(vq);

	mutex_lock(&ctx->jpeg->lock);
}

static int s5p_jpeg_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct s5p_jpeg_ctx *ctx = vb2_get_drv_priv(q);
	int ret;

	ret = pm_runtime_get_sync(ctx->jpeg->dev);

	return ret > 0 ? 0 : ret;
}

static int s5p_jpeg_stop_streaming(struct vb2_queue *q)
{
	struct s5p_jpeg_ctx *ctx = vb2_get_drv_priv(q);

	pm_runtime_put(ctx->jpeg->dev);

	return 0;
}

static struct vb2_ops s5p_jpeg_qops = {
	.queue_setup		= s5p_jpeg_queue_setup,
	.buf_prepare		= s5p_jpeg_buf_prepare,
	.buf_queue		= s5p_jpeg_buf_queue,
	.wait_prepare		= s5p_jpeg_wait_prepare,
	.wait_finish		= s5p_jpeg_wait_finish,
	.start_streaming	= s5p_jpeg_start_streaming,
	.stop_streaming		= s5p_jpeg_stop_streaming,
};

static int queue_init(void *priv, struct vb2_queue *src_vq,
		      struct vb2_queue *dst_vq)
{
	struct s5p_jpeg_ctx *ctx = priv;
	int ret;

	memset(src_vq, 0, sizeof(*src_vq));
	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	src_vq->io_modes = VB2_MMAP | VB2_USERPTR;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->ops = &s5p_jpeg_qops;
	src_vq->mem_ops = &vb2_dma_contig_memops;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	memset(dst_vq, 0, sizeof(*dst_vq));
	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dst_vq->io_modes = VB2_MMAP | VB2_USERPTR;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->ops = &s5p_jpeg_qops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;

	return vb2_queue_init(dst_vq);
}

/*
 * ============================================================================
 * ISR
 * ============================================================================
 */

static irqreturn_t s5p_jpeg_irq(int irq, void *dev_id)
{
	struct s5p_jpeg *jpeg = dev_id;
	struct s5p_jpeg_ctx *curr_ctx;
	struct vb2_buffer *src_buf, *dst_buf;
	unsigned long payload_size = 0;
	enum vb2_buffer_state state = VB2_BUF_STATE_DONE;
	bool enc_jpeg_too_large = false;
	bool timer_elapsed = false;
	bool op_completed = false;

	spin_lock(&jpeg->slock);

	curr_ctx = v4l2_m2m_get_curr_priv(jpeg->m2m_dev);

	src_buf = v4l2_m2m_src_buf_remove(curr_ctx->m2m_ctx);
	dst_buf = v4l2_m2m_dst_buf_remove(curr_ctx->m2m_ctx);

	if (curr_ctx->mode == S5P_JPEG_ENCODE)
		enc_jpeg_too_large = jpeg_enc_stream_stat(jpeg->regs);
	timer_elapsed = jpeg_timer_stat(jpeg->regs);
	op_completed = jpeg_result_stat_ok(jpeg->regs);
	if (curr_ctx->mode == S5P_JPEG_DECODE)
		op_completed = op_completed && jpeg_stream_stat_ok(jpeg->regs);

	if (enc_jpeg_too_large) {
		state = VB2_BUF_STATE_ERROR;
		jpeg_clear_enc_stream_stat(jpeg->regs);
	} else if (timer_elapsed) {
		state = VB2_BUF_STATE_ERROR;
		jpeg_clear_timer_stat(jpeg->regs);
	} else if (!op_completed) {
		state = VB2_BUF_STATE_ERROR;
	} else {
		payload_size = jpeg_compressed_size(jpeg->regs);
	}

	v4l2_m2m_buf_done(src_buf, state);
	if (curr_ctx->mode == S5P_JPEG_ENCODE)
		vb2_set_plane_payload(dst_buf, 0, payload_size);
	v4l2_m2m_buf_done(dst_buf, state);
	v4l2_m2m_job_finish(jpeg->m2m_dev, curr_ctx->m2m_ctx);

	curr_ctx->subsampling = jpeg_get_subsampling_mode(jpeg->regs);
	spin_unlock(&jpeg->slock);

	jpeg_clear_int(jpeg->regs);

	return IRQ_HANDLED;
}

/*
 * ============================================================================
 * Driver basic infrastructure
 * ============================================================================
 */

static int s5p_jpeg_probe(struct platform_device *pdev)
{
	struct s5p_jpeg *jpeg;
	struct resource *res;
	int ret;

	/* JPEG IP abstraction struct */
	jpeg = devm_kzalloc(&pdev->dev, sizeof(struct s5p_jpeg), GFP_KERNEL);
	if (!jpeg)
		return -ENOMEM;

	mutex_init(&jpeg->lock);
	spin_lock_init(&jpeg->slock);
	jpeg->dev = &pdev->dev;

	/* memory-mapped registers */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	jpeg->regs = devm_request_and_ioremap(&pdev->dev, res);
	if (jpeg->regs == NULL) {
		dev_err(&pdev->dev, "Failed to obtain io memory\n");
		return -ENOENT;
	}

	/* interrupt service routine registration */
	jpeg->irq = ret = platform_get_irq(pdev, 0);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot find IRQ\n");
		return ret;
	}

	ret = devm_request_irq(&pdev->dev, jpeg->irq, s5p_jpeg_irq, 0,
			dev_name(&pdev->dev), jpeg);
	if (ret) {
		dev_err(&pdev->dev, "cannot claim IRQ %d\n", jpeg->irq);
		return ret;
	}

	/* clocks */
	jpeg->clk = clk_get(&pdev->dev, "jpeg");
	if (IS_ERR(jpeg->clk)) {
		dev_err(&pdev->dev, "cannot get clock\n");
		ret = PTR_ERR(jpeg->clk);
		return ret;
	}
	dev_dbg(&pdev->dev, "clock source %p\n", jpeg->clk);
	clk_enable(jpeg->clk);

	/* v4l2 device */
	ret = v4l2_device_register(&pdev->dev, &jpeg->v4l2_dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register v4l2 device\n");
		goto clk_get_rollback;
	}

	/* mem2mem device */
	jpeg->m2m_dev = v4l2_m2m_init(&s5p_jpeg_m2m_ops);
	if (IS_ERR(jpeg->m2m_dev)) {
		v4l2_err(&jpeg->v4l2_dev, "Failed to init mem2mem device\n");
		ret = PTR_ERR(jpeg->m2m_dev);
		goto device_register_rollback;
	}

	jpeg->alloc_ctx = vb2_dma_contig_init_ctx(&pdev->dev);
	if (IS_ERR(jpeg->alloc_ctx)) {
		v4l2_err(&jpeg->v4l2_dev, "Failed to init memory allocator\n");
		ret = PTR_ERR(jpeg->alloc_ctx);
		goto m2m_init_rollback;
	}

	/* JPEG encoder /dev/videoX node */
	jpeg->vfd_encoder = video_device_alloc();
	if (!jpeg->vfd_encoder) {
		v4l2_err(&jpeg->v4l2_dev, "Failed to allocate video device\n");
		ret = -ENOMEM;
		goto vb2_allocator_rollback;
	}
	strlcpy(jpeg->vfd_encoder->name, S5P_JPEG_M2M_NAME,
		sizeof(jpeg->vfd_encoder->name));
	jpeg->vfd_encoder->fops		= &s5p_jpeg_fops;
	jpeg->vfd_encoder->ioctl_ops	= &s5p_jpeg_ioctl_ops;
	jpeg->vfd_encoder->minor	= -1;
	jpeg->vfd_encoder->release	= video_device_release;
	jpeg->vfd_encoder->lock		= &jpeg->lock;
	jpeg->vfd_encoder->v4l2_dev	= &jpeg->v4l2_dev;
	/* Locking in file operations other than ioctl should be done
	   by the driver, not the V4L2 core.
	   This driver needs auditing so that this flag can be removed. */
	set_bit(V4L2_FL_LOCK_ALL_FOPS, &jpeg->vfd_encoder->flags);

	ret = video_register_device(jpeg->vfd_encoder, VFL_TYPE_GRABBER, -1);
	if (ret) {
		v4l2_err(&jpeg->v4l2_dev, "Failed to register video device\n");
		goto enc_vdev_alloc_rollback;
	}

	video_set_drvdata(jpeg->vfd_encoder, jpeg);
	v4l2_info(&jpeg->v4l2_dev,
		  "encoder device registered as /dev/video%d\n",
		  jpeg->vfd_encoder->num);

	/* JPEG decoder /dev/videoX node */
	jpeg->vfd_decoder = video_device_alloc();
	if (!jpeg->vfd_decoder) {
		v4l2_err(&jpeg->v4l2_dev, "Failed to allocate video device\n");
		ret = -ENOMEM;
		goto enc_vdev_register_rollback;
	}
	strlcpy(jpeg->vfd_decoder->name, S5P_JPEG_M2M_NAME,
		sizeof(jpeg->vfd_decoder->name));
	jpeg->vfd_decoder->fops		= &s5p_jpeg_fops;
	jpeg->vfd_decoder->ioctl_ops	= &s5p_jpeg_ioctl_ops;
	jpeg->vfd_decoder->minor	= -1;
	jpeg->vfd_decoder->release	= video_device_release;
	jpeg->vfd_decoder->lock		= &jpeg->lock;
	jpeg->vfd_decoder->v4l2_dev	= &jpeg->v4l2_dev;
	/* Locking in file operations other than ioctl should be done by the driver,
	   not the V4L2 core.
	   This driver needs auditing so that this flag can be removed. */
	set_bit(V4L2_FL_LOCK_ALL_FOPS, &jpeg->vfd_decoder->flags);

	ret = video_register_device(jpeg->vfd_decoder, VFL_TYPE_GRABBER, -1);
	if (ret) {
		v4l2_err(&jpeg->v4l2_dev, "Failed to register video device\n");
		goto dec_vdev_alloc_rollback;
	}

	video_set_drvdata(jpeg->vfd_decoder, jpeg);
	v4l2_info(&jpeg->v4l2_dev,
		  "decoder device registered as /dev/video%d\n",
		  jpeg->vfd_decoder->num);

	/* final statements & power management */
	platform_set_drvdata(pdev, jpeg);

	pm_runtime_enable(&pdev->dev);

	v4l2_info(&jpeg->v4l2_dev, "Samsung S5P JPEG codec\n");

	return 0;

dec_vdev_alloc_rollback:
	video_device_release(jpeg->vfd_decoder);

enc_vdev_register_rollback:
	video_unregister_device(jpeg->vfd_encoder);

enc_vdev_alloc_rollback:
	video_device_release(jpeg->vfd_encoder);

vb2_allocator_rollback:
	vb2_dma_contig_cleanup_ctx(jpeg->alloc_ctx);

m2m_init_rollback:
	v4l2_m2m_release(jpeg->m2m_dev);

device_register_rollback:
	v4l2_device_unregister(&jpeg->v4l2_dev);

clk_get_rollback:
	clk_disable(jpeg->clk);
	clk_put(jpeg->clk);

	return ret;
}

static int s5p_jpeg_remove(struct platform_device *pdev)
{
	struct s5p_jpeg *jpeg = platform_get_drvdata(pdev);

	pm_runtime_disable(jpeg->dev);

	video_unregister_device(jpeg->vfd_decoder);
	video_device_release(jpeg->vfd_decoder);
	video_unregister_device(jpeg->vfd_encoder);
	video_device_release(jpeg->vfd_encoder);
	vb2_dma_contig_cleanup_ctx(jpeg->alloc_ctx);
	v4l2_m2m_release(jpeg->m2m_dev);
	v4l2_device_unregister(&jpeg->v4l2_dev);

	clk_disable(jpeg->clk);
	clk_put(jpeg->clk);

	return 0;
}

static int s5p_jpeg_runtime_suspend(struct device *dev)
{
	return 0;
}

static int s5p_jpeg_runtime_resume(struct device *dev)
{
	struct s5p_jpeg *jpeg = dev_get_drvdata(dev);
	/*
	 * JPEG IP allows storing two Huffman tables for each component
	 * We fill table 0 for each component
	 */
	jpeg_set_hdctbl(jpeg->regs);
	jpeg_set_hdctblg(jpeg->regs);
	jpeg_set_hactbl(jpeg->regs);
	jpeg_set_hactblg(jpeg->regs);
	return 0;
}

static const struct dev_pm_ops s5p_jpeg_pm_ops = {
	.runtime_suspend = s5p_jpeg_runtime_suspend,
	.runtime_resume	 = s5p_jpeg_runtime_resume,
};

static struct platform_driver s5p_jpeg_driver = {
	.probe = s5p_jpeg_probe,
	.remove = s5p_jpeg_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = S5P_JPEG_M2M_NAME,
		.pm = &s5p_jpeg_pm_ops,
	},
};

static int __init
s5p_jpeg_register(void)
{
	int ret;

	pr_info("S5P JPEG V4L2 Driver, (c) 2011 Samsung Electronics\n");

	ret = platform_driver_register(&s5p_jpeg_driver);

	if (ret)
		pr_err("%s: failed to register jpeg driver\n", __func__);

	return ret;
}

static void __exit
s5p_jpeg_unregister(void)
{
	platform_driver_unregister(&s5p_jpeg_driver);
}

module_init(s5p_jpeg_register);
module_exit(s5p_jpeg_unregister);

MODULE_AUTHOR("Andrzej Pietrasiewicz <andrzej.p@samsung.com>");
MODULE_DESCRIPTION("Samsung JPEG codec driver");
MODULE_LICENSE("GPL");

