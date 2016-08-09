/*
 * Support eMMa-PrP through mem2mem framework.
 *
 * eMMa-PrP is a piece of HW that allows fetching buffers
 * from one memory location and do several operations on
 * them such as scaling or format conversion giving, as a result
 * a new processed buffer in another memory location.
 *
 * Based on mem2mem_testdev.c by Pawel Osciak.
 *
 * Copyright (c) 2011 Vista Silicon S.L.
 * Javier Martin <javier.martin@vista-silicon.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version
 */
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/io.h>

#include <linux/platform_device.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-dma-contig.h>
#include <linux/sizes.h>

#define EMMAPRP_MODULE_NAME "mem2mem-emmaprp"

MODULE_DESCRIPTION("Mem-to-mem device which supports eMMa-PrP present in mx2 SoCs");
MODULE_AUTHOR("Javier Martin <javier.martin@vista-silicon.com");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.0.1");

static bool debug;
module_param(debug, bool, 0644);

#define MIN_W 32
#define MIN_H 32
#define MAX_W 2040
#define MAX_H 2046

#define S_ALIGN		1 /* multiple of 2 */
#define W_ALIGN_YUV420	3 /* multiple of 8 */
#define W_ALIGN_OTHERS	2 /* multiple of 4 */
#define H_ALIGN		1 /* multiple of 2 */

/* Flags that indicate a format can be used for capture/output */
#define MEM2MEM_CAPTURE	(1 << 0)
#define MEM2MEM_OUTPUT	(1 << 1)

#define MEM2MEM_NAME		"m2m-emmaprp"

/* In bytes, per queue */
#define MEM2MEM_VID_MEM_LIMIT	SZ_16M

#define dprintk(dev, fmt, arg...) \
	v4l2_dbg(1, debug, &dev->v4l2_dev, "%s: " fmt, __func__, ## arg)

/* EMMA PrP */
#define PRP_CNTL                        0x00
#define PRP_INTR_CNTL                   0x04
#define PRP_INTRSTATUS                  0x08
#define PRP_SOURCE_Y_PTR                0x0c
#define PRP_SOURCE_CB_PTR               0x10
#define PRP_SOURCE_CR_PTR               0x14
#define PRP_DEST_RGB1_PTR               0x18
#define PRP_DEST_RGB2_PTR               0x1c
#define PRP_DEST_Y_PTR                  0x20
#define PRP_DEST_CB_PTR                 0x24
#define PRP_DEST_CR_PTR                 0x28
#define PRP_SRC_FRAME_SIZE              0x2c
#define PRP_DEST_CH1_LINE_STRIDE        0x30
#define PRP_SRC_PIXEL_FORMAT_CNTL       0x34
#define PRP_CH1_PIXEL_FORMAT_CNTL       0x38
#define PRP_CH1_OUT_IMAGE_SIZE          0x3c
#define PRP_CH2_OUT_IMAGE_SIZE          0x40
#define PRP_SRC_LINE_STRIDE             0x44
#define PRP_CSC_COEF_012                0x48
#define PRP_CSC_COEF_345                0x4c
#define PRP_CSC_COEF_678                0x50
#define PRP_CH1_RZ_HORI_COEF1           0x54
#define PRP_CH1_RZ_HORI_COEF2           0x58
#define PRP_CH1_RZ_HORI_VALID           0x5c
#define PRP_CH1_RZ_VERT_COEF1           0x60
#define PRP_CH1_RZ_VERT_COEF2           0x64
#define PRP_CH1_RZ_VERT_VALID           0x68
#define PRP_CH2_RZ_HORI_COEF1           0x6c
#define PRP_CH2_RZ_HORI_COEF2           0x70
#define PRP_CH2_RZ_HORI_VALID           0x74
#define PRP_CH2_RZ_VERT_COEF1           0x78
#define PRP_CH2_RZ_VERT_COEF2           0x7c
#define PRP_CH2_RZ_VERT_VALID           0x80

#define PRP_CNTL_CH1EN          (1 << 0)
#define PRP_CNTL_CH2EN          (1 << 1)
#define PRP_CNTL_CSIEN          (1 << 2)
#define PRP_CNTL_DATA_IN_YUV420 (0 << 3)
#define PRP_CNTL_DATA_IN_YUV422 (1 << 3)
#define PRP_CNTL_DATA_IN_RGB16  (2 << 3)
#define PRP_CNTL_DATA_IN_RGB32  (3 << 3)
#define PRP_CNTL_CH1_OUT_RGB8   (0 << 5)
#define PRP_CNTL_CH1_OUT_RGB16  (1 << 5)
#define PRP_CNTL_CH1_OUT_RGB32  (2 << 5)
#define PRP_CNTL_CH1_OUT_YUV422 (3 << 5)
#define PRP_CNTL_CH2_OUT_YUV420 (0 << 7)
#define PRP_CNTL_CH2_OUT_YUV422 (1 << 7)
#define PRP_CNTL_CH2_OUT_YUV444 (2 << 7)
#define PRP_CNTL_CH1_LEN        (1 << 9)
#define PRP_CNTL_CH2_LEN        (1 << 10)
#define PRP_CNTL_SKIP_FRAME     (1 << 11)
#define PRP_CNTL_SWRST          (1 << 12)
#define PRP_CNTL_CLKEN          (1 << 13)
#define PRP_CNTL_WEN            (1 << 14)
#define PRP_CNTL_CH1BYP         (1 << 15)
#define PRP_CNTL_IN_TSKIP(x)    ((x) << 16)
#define PRP_CNTL_CH1_TSKIP(x)   ((x) << 19)
#define PRP_CNTL_CH2_TSKIP(x)   ((x) << 22)
#define PRP_CNTL_INPUT_FIFO_LEVEL(x)    ((x) << 25)
#define PRP_CNTL_RZ_FIFO_LEVEL(x)       ((x) << 27)
#define PRP_CNTL_CH2B1EN        (1 << 29)
#define PRP_CNTL_CH2B2EN        (1 << 30)
#define PRP_CNTL_CH2FEN         (1 << 31)

#define PRP_SIZE_HEIGHT(x)	(x)
#define PRP_SIZE_WIDTH(x)	((x) << 16)

/* IRQ Enable and status register */
#define PRP_INTR_RDERR          (1 << 0)
#define PRP_INTR_CH1WERR        (1 << 1)
#define PRP_INTR_CH2WERR        (1 << 2)
#define PRP_INTR_CH1FC          (1 << 3)
#define PRP_INTR_CH2FC          (1 << 5)
#define PRP_INTR_LBOVF          (1 << 7)
#define PRP_INTR_CH2OVF         (1 << 8)

#define PRP_INTR_ST_RDERR	(1 << 0)
#define PRP_INTR_ST_CH1WERR	(1 << 1)
#define PRP_INTR_ST_CH2WERR	(1 << 2)
#define PRP_INTR_ST_CH2B2CI	(1 << 3)
#define PRP_INTR_ST_CH2B1CI	(1 << 4)
#define PRP_INTR_ST_CH1B2CI	(1 << 5)
#define PRP_INTR_ST_CH1B1CI	(1 << 6)
#define PRP_INTR_ST_LBOVF	(1 << 7)
#define PRP_INTR_ST_CH2OVF	(1 << 8)

struct emmaprp_fmt {
	char	*name;
	u32	fourcc;
	/* Types the format can be used for */
	u32	types;
};

static struct emmaprp_fmt formats[] = {
	{
		.name	= "YUV 4:2:0 Planar",
		.fourcc	= V4L2_PIX_FMT_YUV420,
		.types	= MEM2MEM_CAPTURE,
	},
	{
		.name	= "4:2:2, packed, YUYV",
		.fourcc	= V4L2_PIX_FMT_YUYV,
		.types	= MEM2MEM_OUTPUT,
	},
};

/* Per-queue, driver-specific private data */
struct emmaprp_q_data {
	unsigned int		width;
	unsigned int		height;
	unsigned int		sizeimage;
	struct emmaprp_fmt	*fmt;
};

enum {
	V4L2_M2M_SRC = 0,
	V4L2_M2M_DST = 1,
};

#define NUM_FORMATS ARRAY_SIZE(formats)

static struct emmaprp_fmt *find_format(struct v4l2_format *f)
{
	struct emmaprp_fmt *fmt;
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

struct emmaprp_dev {
	struct v4l2_device	v4l2_dev;
	struct video_device	*vfd;

	struct mutex		dev_mutex;
	spinlock_t		irqlock;

	void __iomem		*base_emma;
	struct clk		*clk_emma_ahb, *clk_emma_ipg;

	struct v4l2_m2m_dev	*m2m_dev;
};

struct emmaprp_ctx {
	struct emmaprp_dev	*dev;
	/* Abort requested by m2m */
	int			aborting;
	struct emmaprp_q_data	q_data[2];
	struct v4l2_m2m_ctx	*m2m_ctx;
};

static struct emmaprp_q_data *get_q_data(struct emmaprp_ctx *ctx,
					 enum v4l2_buf_type type)
{
	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		return &(ctx->q_data[V4L2_M2M_SRC]);
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		return &(ctx->q_data[V4L2_M2M_DST]);
	default:
		BUG();
	}
	return NULL;
}

/*
 * mem2mem callbacks
 */
static void emmaprp_job_abort(void *priv)
{
	struct emmaprp_ctx *ctx = priv;
	struct emmaprp_dev *pcdev = ctx->dev;

	ctx->aborting = 1;

	dprintk(pcdev, "Aborting task\n");

	v4l2_m2m_job_finish(pcdev->m2m_dev, ctx->m2m_ctx);
}

static void emmaprp_lock(void *priv)
{
	struct emmaprp_ctx *ctx = priv;
	struct emmaprp_dev *pcdev = ctx->dev;
	mutex_lock(&pcdev->dev_mutex);
}

static void emmaprp_unlock(void *priv)
{
	struct emmaprp_ctx *ctx = priv;
	struct emmaprp_dev *pcdev = ctx->dev;
	mutex_unlock(&pcdev->dev_mutex);
}

static inline void emmaprp_dump_regs(struct emmaprp_dev *pcdev)
{
	dprintk(pcdev,
		"eMMa-PrP Registers:\n"
		"  SOURCE_Y_PTR = 0x%08X\n"
		"  SRC_FRAME_SIZE = 0x%08X\n"
		"  DEST_Y_PTR = 0x%08X\n"
		"  DEST_CR_PTR = 0x%08X\n"
		"  DEST_CB_PTR = 0x%08X\n"
		"  CH2_OUT_IMAGE_SIZE = 0x%08X\n"
		"  CNTL = 0x%08X\n",
		readl(pcdev->base_emma + PRP_SOURCE_Y_PTR),
		readl(pcdev->base_emma + PRP_SRC_FRAME_SIZE),
		readl(pcdev->base_emma + PRP_DEST_Y_PTR),
		readl(pcdev->base_emma + PRP_DEST_CR_PTR),
		readl(pcdev->base_emma + PRP_DEST_CB_PTR),
		readl(pcdev->base_emma + PRP_CH2_OUT_IMAGE_SIZE),
		readl(pcdev->base_emma + PRP_CNTL));
}

static void emmaprp_device_run(void *priv)
{
	struct emmaprp_ctx *ctx = priv;
	struct emmaprp_q_data *s_q_data, *d_q_data;
	struct vb2_buffer *src_buf, *dst_buf;
	struct emmaprp_dev *pcdev = ctx->dev;
	unsigned int s_width, s_height;
	unsigned int d_width, d_height;
	unsigned int d_size;
	dma_addr_t p_in, p_out;
	u32 tmp;

	src_buf = v4l2_m2m_next_src_buf(ctx->m2m_ctx);
	dst_buf = v4l2_m2m_next_dst_buf(ctx->m2m_ctx);

	s_q_data = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
	s_width	= s_q_data->width;
	s_height = s_q_data->height;

	d_q_data = get_q_data(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);
	d_width = d_q_data->width;
	d_height = d_q_data->height;
	d_size = d_width * d_height;

	p_in = vb2_dma_contig_plane_dma_addr(src_buf, 0);
	p_out = vb2_dma_contig_plane_dma_addr(dst_buf, 0);
	if (!p_in || !p_out) {
		v4l2_err(&pcdev->v4l2_dev,
			 "Acquiring kernel pointers to buffers failed\n");
		return;
	}

	/* Input frame parameters */
	writel(p_in, pcdev->base_emma + PRP_SOURCE_Y_PTR);
	writel(PRP_SIZE_WIDTH(s_width) | PRP_SIZE_HEIGHT(s_height),
	       pcdev->base_emma + PRP_SRC_FRAME_SIZE);

	/* Output frame parameters */
	writel(p_out, pcdev->base_emma + PRP_DEST_Y_PTR);
	writel(p_out + d_size, pcdev->base_emma + PRP_DEST_CB_PTR);
	writel(p_out + d_size + (d_size >> 2),
	       pcdev->base_emma + PRP_DEST_CR_PTR);
	writel(PRP_SIZE_WIDTH(d_width) | PRP_SIZE_HEIGHT(d_height),
	       pcdev->base_emma + PRP_CH2_OUT_IMAGE_SIZE);

	/* IRQ configuration */
	tmp = readl(pcdev->base_emma + PRP_INTR_CNTL);
	writel(tmp | PRP_INTR_RDERR |
		PRP_INTR_CH2WERR |
		PRP_INTR_CH2FC,
		pcdev->base_emma + PRP_INTR_CNTL);

	emmaprp_dump_regs(pcdev);

	/* Enable transfer */
	tmp = readl(pcdev->base_emma + PRP_CNTL);
	writel(tmp | PRP_CNTL_CH2_OUT_YUV420 |
		PRP_CNTL_DATA_IN_YUV422 |
		PRP_CNTL_CH2EN,
		pcdev->base_emma + PRP_CNTL);
}

static irqreturn_t emmaprp_irq(int irq_emma, void *data)
{
	struct emmaprp_dev *pcdev = data;
	struct emmaprp_ctx *curr_ctx;
	struct vb2_v4l2_buffer *src_vb, *dst_vb;
	unsigned long flags;
	u32 irqst;

	/* Check irq flags and clear irq */
	irqst = readl(pcdev->base_emma + PRP_INTRSTATUS);
	writel(irqst, pcdev->base_emma + PRP_INTRSTATUS);
	dprintk(pcdev, "irqst = 0x%08x\n", irqst);

	curr_ctx = v4l2_m2m_get_curr_priv(pcdev->m2m_dev);
	if (curr_ctx == NULL) {
		pr_err("Instance released before the end of transaction\n");
		return IRQ_HANDLED;
	}

	if (!curr_ctx->aborting) {
		if ((irqst & PRP_INTR_ST_RDERR) ||
		(irqst & PRP_INTR_ST_CH2WERR)) {
			pr_err("PrP bus error occurred, this transfer is probably corrupted\n");
			writel(PRP_CNTL_SWRST, pcdev->base_emma + PRP_CNTL);
		} else if (irqst & PRP_INTR_ST_CH2B1CI) { /* buffer ready */
			src_vb = v4l2_m2m_src_buf_remove(curr_ctx->m2m_ctx);
			dst_vb = v4l2_m2m_dst_buf_remove(curr_ctx->m2m_ctx);

			dst_vb->vb2_buf.timestamp = src_vb->vb2_buf.timestamp;
			dst_vb->flags &=
				~V4L2_BUF_FLAG_TSTAMP_SRC_MASK;
			dst_vb->flags |=
				src_vb->flags
				& V4L2_BUF_FLAG_TSTAMP_SRC_MASK;
			dst_vb->timecode = src_vb->timecode;

			spin_lock_irqsave(&pcdev->irqlock, flags);
			v4l2_m2m_buf_done(src_vb, VB2_BUF_STATE_DONE);
			v4l2_m2m_buf_done(dst_vb, VB2_BUF_STATE_DONE);
			spin_unlock_irqrestore(&pcdev->irqlock, flags);
		}
	}

	v4l2_m2m_job_finish(pcdev->m2m_dev, curr_ctx->m2m_ctx);
	return IRQ_HANDLED;
}

/*
 * video ioctls
 */
static int vidioc_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	strncpy(cap->driver, MEM2MEM_NAME, sizeof(cap->driver) - 1);
	strncpy(cap->card, MEM2MEM_NAME, sizeof(cap->card) - 1);
	cap->device_caps = V4L2_CAP_VIDEO_M2M | V4L2_CAP_STREAMING;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int enum_fmt(struct v4l2_fmtdesc *f, u32 type)
{
	int i, num;
	struct emmaprp_fmt *fmt;

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
		strlcpy(f->description, fmt->name, sizeof(f->description) - 1);
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

static int vidioc_g_fmt(struct emmaprp_ctx *ctx, struct v4l2_format *f)
{
	struct vb2_queue *vq;
	struct emmaprp_q_data *q_data;

	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	q_data = get_q_data(ctx, f->type);

	f->fmt.pix.width	= q_data->width;
	f->fmt.pix.height	= q_data->height;
	f->fmt.pix.field	= V4L2_FIELD_NONE;
	f->fmt.pix.pixelformat	= q_data->fmt->fourcc;
	if (f->fmt.pix.pixelformat == V4L2_PIX_FMT_YUV420)
		f->fmt.pix.bytesperline = q_data->width * 3 / 2;
	else /* YUYV */
		f->fmt.pix.bytesperline = q_data->width * 2;
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

static int vidioc_try_fmt(struct v4l2_format *f)
{
	enum v4l2_field field;


	if (!find_format(f))
		return -EINVAL;

	field = f->fmt.pix.field;
	if (field == V4L2_FIELD_ANY)
		field = V4L2_FIELD_NONE;
	else if (V4L2_FIELD_NONE != field)
		return -EINVAL;

	/* V4L2 specification suggests the driver corrects the format struct
	 * if any of the dimensions is unsupported */
	f->fmt.pix.field = field;

	if (f->fmt.pix.pixelformat == V4L2_PIX_FMT_YUV420) {
		v4l_bound_align_image(&f->fmt.pix.width, MIN_W, MAX_W,
				      W_ALIGN_YUV420, &f->fmt.pix.height,
				      MIN_H, MAX_H, H_ALIGN, S_ALIGN);
		f->fmt.pix.bytesperline = f->fmt.pix.width * 3 / 2;
	} else {
		v4l_bound_align_image(&f->fmt.pix.width, MIN_W, MAX_W,
				      W_ALIGN_OTHERS, &f->fmt.pix.height,
				      MIN_H, MAX_H, H_ALIGN, S_ALIGN);
		f->fmt.pix.bytesperline = f->fmt.pix.width * 2;
	}
	f->fmt.pix.sizeimage = f->fmt.pix.height * f->fmt.pix.bytesperline;

	return 0;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct emmaprp_fmt *fmt;
	struct emmaprp_ctx *ctx = priv;

	fmt = find_format(f);
	if (!fmt || !(fmt->types & MEM2MEM_CAPTURE)) {
		v4l2_err(&ctx->dev->v4l2_dev,
			 "Fourcc format (0x%08x) invalid.\n",
			 f->fmt.pix.pixelformat);
		return -EINVAL;
	}

	return vidioc_try_fmt(f);
}

static int vidioc_try_fmt_vid_out(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct emmaprp_fmt *fmt;
	struct emmaprp_ctx *ctx = priv;

	fmt = find_format(f);
	if (!fmt || !(fmt->types & MEM2MEM_OUTPUT)) {
		v4l2_err(&ctx->dev->v4l2_dev,
			 "Fourcc format (0x%08x) invalid.\n",
			 f->fmt.pix.pixelformat);
		return -EINVAL;
	}

	return vidioc_try_fmt(f);
}

static int vidioc_s_fmt(struct emmaprp_ctx *ctx, struct v4l2_format *f)
{
	struct emmaprp_q_data *q_data;
	struct vb2_queue *vq;
	int ret;

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

	ret = vidioc_try_fmt(f);
	if (ret)
		return ret;

	q_data->fmt		= find_format(f);
	q_data->width		= f->fmt.pix.width;
	q_data->height		= f->fmt.pix.height;
	if (q_data->fmt->fourcc == V4L2_PIX_FMT_YUV420)
		q_data->sizeimage = q_data->width * q_data->height * 3 / 2;
	else /* YUYV */
		q_data->sizeimage = q_data->width * q_data->height * 2;

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
	struct emmaprp_ctx *ctx = priv;

	return v4l2_m2m_reqbufs(file, ctx->m2m_ctx, reqbufs);
}

static int vidioc_querybuf(struct file *file, void *priv,
			   struct v4l2_buffer *buf)
{
	struct emmaprp_ctx *ctx = priv;

	return v4l2_m2m_querybuf(file, ctx->m2m_ctx, buf);
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct emmaprp_ctx *ctx = priv;

	return v4l2_m2m_qbuf(file, ctx->m2m_ctx, buf);
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct emmaprp_ctx *ctx = priv;

	return v4l2_m2m_dqbuf(file, ctx->m2m_ctx, buf);
}

static int vidioc_streamon(struct file *file, void *priv,
			   enum v4l2_buf_type type)
{
	struct emmaprp_ctx *ctx = priv;

	return v4l2_m2m_streamon(file, ctx->m2m_ctx, type);
}

static int vidioc_streamoff(struct file *file, void *priv,
			    enum v4l2_buf_type type)
{
	struct emmaprp_ctx *ctx = priv;

	return v4l2_m2m_streamoff(file, ctx->m2m_ctx, type);
}

static const struct v4l2_ioctl_ops emmaprp_ioctl_ops = {
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
};


/*
 * Queue operations
 */
static int emmaprp_queue_setup(struct vb2_queue *vq,
				unsigned int *nbuffers, unsigned int *nplanes,
				unsigned int sizes[], struct device *alloc_devs[])
{
	struct emmaprp_ctx *ctx = vb2_get_drv_priv(vq);
	struct emmaprp_q_data *q_data;
	unsigned int size, count = *nbuffers;

	q_data = get_q_data(ctx, vq->type);

	if (q_data->fmt->fourcc == V4L2_PIX_FMT_YUV420)
		size = q_data->width * q_data->height * 3 / 2;
	else
		size = q_data->width * q_data->height * 2;

	while (size * count > MEM2MEM_VID_MEM_LIMIT)
		(count)--;

	*nplanes = 1;
	*nbuffers = count;
	sizes[0] = size;

	dprintk(ctx->dev, "get %d buffer(s) of size %d each.\n", count, size);

	return 0;
}

static int emmaprp_buf_prepare(struct vb2_buffer *vb)
{
	struct emmaprp_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct emmaprp_q_data *q_data;

	dprintk(ctx->dev, "type: %d\n", vb->vb2_queue->type);

	q_data = get_q_data(ctx, vb->vb2_queue->type);

	if (vb2_plane_size(vb, 0) < q_data->sizeimage) {
		dprintk(ctx->dev, "%s data will not fit into plane"
				  "(%lu < %lu)\n", __func__,
				  vb2_plane_size(vb, 0),
				  (long)q_data->sizeimage);
		return -EINVAL;
	}

	vb2_set_plane_payload(vb, 0, q_data->sizeimage);

	return 0;
}

static void emmaprp_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct emmaprp_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	v4l2_m2m_buf_queue(ctx->m2m_ctx, vbuf);
}

static struct vb2_ops emmaprp_qops = {
	.queue_setup	 = emmaprp_queue_setup,
	.buf_prepare	 = emmaprp_buf_prepare,
	.buf_queue	 = emmaprp_buf_queue,
};

static int queue_init(void *priv, struct vb2_queue *src_vq,
		      struct vb2_queue *dst_vq)
{
	struct emmaprp_ctx *ctx = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	src_vq->io_modes = VB2_MMAP | VB2_USERPTR;
	src_vq->drv_priv = ctx;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->ops = &emmaprp_qops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->dev = ctx->dev->v4l2_dev.dev;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dst_vq->io_modes = VB2_MMAP | VB2_USERPTR;
	dst_vq->drv_priv = ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->ops = &emmaprp_qops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->dev = ctx->dev->v4l2_dev.dev;

	return vb2_queue_init(dst_vq);
}

/*
 * File operations
 */
static int emmaprp_open(struct file *file)
{
	struct emmaprp_dev *pcdev = video_drvdata(file);
	struct emmaprp_ctx *ctx;

	ctx = kzalloc(sizeof *ctx, GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	file->private_data = ctx;
	ctx->dev = pcdev;

	if (mutex_lock_interruptible(&pcdev->dev_mutex)) {
		kfree(ctx);
		return -ERESTARTSYS;
	}

	ctx->m2m_ctx = v4l2_m2m_ctx_init(pcdev->m2m_dev, ctx, &queue_init);

	if (IS_ERR(ctx->m2m_ctx)) {
		int ret = PTR_ERR(ctx->m2m_ctx);

		mutex_unlock(&pcdev->dev_mutex);
		kfree(ctx);
		return ret;
	}

	clk_prepare_enable(pcdev->clk_emma_ipg);
	clk_prepare_enable(pcdev->clk_emma_ahb);
	ctx->q_data[V4L2_M2M_SRC].fmt = &formats[1];
	ctx->q_data[V4L2_M2M_DST].fmt = &formats[0];
	mutex_unlock(&pcdev->dev_mutex);

	dprintk(pcdev, "Created instance %p, m2m_ctx: %p\n", ctx, ctx->m2m_ctx);

	return 0;
}

static int emmaprp_release(struct file *file)
{
	struct emmaprp_dev *pcdev = video_drvdata(file);
	struct emmaprp_ctx *ctx = file->private_data;

	dprintk(pcdev, "Releasing instance %p\n", ctx);

	mutex_lock(&pcdev->dev_mutex);
	clk_disable_unprepare(pcdev->clk_emma_ahb);
	clk_disable_unprepare(pcdev->clk_emma_ipg);
	v4l2_m2m_ctx_release(ctx->m2m_ctx);
	mutex_unlock(&pcdev->dev_mutex);
	kfree(ctx);

	return 0;
}

static unsigned int emmaprp_poll(struct file *file,
				 struct poll_table_struct *wait)
{
	struct emmaprp_dev *pcdev = video_drvdata(file);
	struct emmaprp_ctx *ctx = file->private_data;
	unsigned int res;

	mutex_lock(&pcdev->dev_mutex);
	res = v4l2_m2m_poll(file, ctx->m2m_ctx, wait);
	mutex_unlock(&pcdev->dev_mutex);
	return res;
}

static int emmaprp_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct emmaprp_dev *pcdev = video_drvdata(file);
	struct emmaprp_ctx *ctx = file->private_data;
	int ret;

	if (mutex_lock_interruptible(&pcdev->dev_mutex))
		return -ERESTARTSYS;
	ret = v4l2_m2m_mmap(file, ctx->m2m_ctx, vma);
	mutex_unlock(&pcdev->dev_mutex);
	return ret;
}

static const struct v4l2_file_operations emmaprp_fops = {
	.owner		= THIS_MODULE,
	.open		= emmaprp_open,
	.release	= emmaprp_release,
	.poll		= emmaprp_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= emmaprp_mmap,
};

static struct video_device emmaprp_videodev = {
	.name		= MEM2MEM_NAME,
	.fops		= &emmaprp_fops,
	.ioctl_ops	= &emmaprp_ioctl_ops,
	.minor		= -1,
	.release	= video_device_release,
	.vfl_dir	= VFL_DIR_M2M,
};

static struct v4l2_m2m_ops m2m_ops = {
	.device_run	= emmaprp_device_run,
	.job_abort	= emmaprp_job_abort,
	.lock		= emmaprp_lock,
	.unlock		= emmaprp_unlock,
};

static int emmaprp_probe(struct platform_device *pdev)
{
	struct emmaprp_dev *pcdev;
	struct video_device *vfd;
	struct resource *res;
	int irq, ret;

	pcdev = devm_kzalloc(&pdev->dev, sizeof(*pcdev), GFP_KERNEL);
	if (!pcdev)
		return -ENOMEM;

	spin_lock_init(&pcdev->irqlock);

	pcdev->clk_emma_ipg = devm_clk_get(&pdev->dev, "ipg");
	if (IS_ERR(pcdev->clk_emma_ipg)) {
		return PTR_ERR(pcdev->clk_emma_ipg);
	}

	pcdev->clk_emma_ahb = devm_clk_get(&pdev->dev, "ahb");
	if (IS_ERR(pcdev->clk_emma_ahb))
		return PTR_ERR(pcdev->clk_emma_ahb);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pcdev->base_emma = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pcdev->base_emma))
		return PTR_ERR(pcdev->base_emma);

	ret = v4l2_device_register(&pdev->dev, &pcdev->v4l2_dev);
	if (ret)
		return ret;

	mutex_init(&pcdev->dev_mutex);

	vfd = video_device_alloc();
	if (!vfd) {
		v4l2_err(&pcdev->v4l2_dev, "Failed to allocate video device\n");
		ret = -ENOMEM;
		goto unreg_dev;
	}

	*vfd = emmaprp_videodev;
	vfd->lock = &pcdev->dev_mutex;
	vfd->v4l2_dev = &pcdev->v4l2_dev;

	video_set_drvdata(vfd, pcdev);
	snprintf(vfd->name, sizeof(vfd->name), "%s", emmaprp_videodev.name);
	pcdev->vfd = vfd;
	v4l2_info(&pcdev->v4l2_dev, EMMAPRP_MODULE_NAME
			" Device registered as /dev/video%d\n", vfd->num);

	platform_set_drvdata(pdev, pcdev);

	irq = platform_get_irq(pdev, 0);
	ret = devm_request_irq(&pdev->dev, irq, emmaprp_irq, 0,
			       dev_name(&pdev->dev), pcdev);
	if (ret)
		goto rel_vdev;

	pcdev->m2m_dev = v4l2_m2m_init(&m2m_ops);
	if (IS_ERR(pcdev->m2m_dev)) {
		v4l2_err(&pcdev->v4l2_dev, "Failed to init mem2mem device\n");
		ret = PTR_ERR(pcdev->m2m_dev);
		goto rel_vdev;
	}

	ret = video_register_device(vfd, VFL_TYPE_GRABBER, 0);
	if (ret) {
		v4l2_err(&pcdev->v4l2_dev, "Failed to register video device\n");
		goto rel_m2m;
	}

	return 0;


rel_m2m:
	v4l2_m2m_release(pcdev->m2m_dev);
rel_vdev:
	video_device_release(vfd);
unreg_dev:
	v4l2_device_unregister(&pcdev->v4l2_dev);

	mutex_destroy(&pcdev->dev_mutex);

	return ret;
}

static int emmaprp_remove(struct platform_device *pdev)
{
	struct emmaprp_dev *pcdev = platform_get_drvdata(pdev);

	v4l2_info(&pcdev->v4l2_dev, "Removing " EMMAPRP_MODULE_NAME);

	video_unregister_device(pcdev->vfd);
	v4l2_m2m_release(pcdev->m2m_dev);
	v4l2_device_unregister(&pcdev->v4l2_dev);
	mutex_destroy(&pcdev->dev_mutex);

	return 0;
}

static struct platform_driver emmaprp_pdrv = {
	.probe		= emmaprp_probe,
	.remove		= emmaprp_remove,
	.driver		= {
		.name	= MEM2MEM_NAME,
	},
};
module_platform_driver(emmaprp_pdrv);
