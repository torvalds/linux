// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/bitfield.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/reset.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/regmap.h>

#include <linux/platform_device.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-ctrls.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>

#include "ge2d-regs.h"

#define GE2D_NAME	"meson-ge2d"

#define DEFAULT_WIDTH	128
#define DEFAULT_HEIGHT	128
#define DEFAULT_STRIDE	512

#define MAX_WIDTH	8191
#define MAX_HEIGHT	8191

/*
 * Missing features:
 * - Scaling
 * - Simple 1/2 vertical scaling
 * - YUV input support
 * - Source global alpha
 * - Colorspace conversion
 */

struct ge2d_fmt {
	u32 fourcc;
	bool alpha;
	bool le;
	unsigned int depth;
	unsigned int hw_fmt;
	unsigned int hw_map;
};

struct ge2d_frame {
	struct vb2_v4l2_buffer *buf;

	/* Image Format */
	struct v4l2_pix_format pix_fmt;

	/* Crop */
	struct v4l2_rect crop;

	/* Image format */
	const struct ge2d_fmt *fmt;
};

struct ge2d_ctx {
	struct v4l2_fh fh;
	struct meson_ge2d *ge2d;
	struct ge2d_frame in;
	struct ge2d_frame out;
	struct v4l2_ctrl_handler ctrl_handler;

	unsigned long sequence_out, sequence_cap;

	/* Control values */
	u32 hflip;
	u32 vflip;
	u32 xy_swap;
};

struct meson_ge2d {
	struct v4l2_device v4l2_dev;
	struct v4l2_m2m_dev *m2m_dev;
	struct video_device *vfd;

	struct device *dev;
	struct regmap *map;
	struct clk *clk;

	/* vb2 queue lock */
	struct mutex mutex;

	struct ge2d_ctx *curr;
};

#define FMT(_fourcc, _alpha, _depth, _map)		\
{							\
	.fourcc = _fourcc,				\
	.alpha = (_alpha),				\
	.depth = (_depth),				\
	.hw_fmt = GE2D_FORMAT_ ## _depth ## BIT,	\
	.hw_map = GE2D_COLOR_MAP_ ## _map,		\
}

/* TOFIX Handle the YUV input formats */
static const struct ge2d_fmt formats[] = {
	/*  FOURCC Alpha  HW FMT  HW MAP */
	FMT(V4L2_PIX_FMT_XRGB32, false, 32, BGRA8888),
	FMT(V4L2_PIX_FMT_RGB32, true, 32, BGRA8888),
	FMT(V4L2_PIX_FMT_ARGB32, true, 32, BGRA8888),
	FMT(V4L2_PIX_FMT_RGBX32, false, 32, ABGR8888),
	FMT(V4L2_PIX_FMT_RGBA32, true, 32, ABGR8888),
	FMT(V4L2_PIX_FMT_BGRX32, false, 32, RGBA8888),
	FMT(V4L2_PIX_FMT_BGRA32, true, 32, RGBA8888),
	FMT(V4L2_PIX_FMT_BGR32, true, 32, ARGB8888),
	FMT(V4L2_PIX_FMT_ABGR32, true, 32, ARGB8888),
	FMT(V4L2_PIX_FMT_XBGR32, false, 32, ARGB8888),

	FMT(V4L2_PIX_FMT_RGB24, false, 24, BGR888),
	FMT(V4L2_PIX_FMT_BGR24, false, 24, RGB888),

	FMT(V4L2_PIX_FMT_XRGB555X, false, 16, ARGB1555),
	FMT(V4L2_PIX_FMT_ARGB555X, true, 16, ARGB1555),
	FMT(V4L2_PIX_FMT_RGB565, false, 16, RGB565),
	FMT(V4L2_PIX_FMT_RGBX444, false, 16, RGBA4444),
	FMT(V4L2_PIX_FMT_RGBA444, true, 16, RGBA4444),
	FMT(V4L2_PIX_FMT_XRGB444, false, 16, ARGB4444),
	FMT(V4L2_PIX_FMT_ARGB444, true, 16, ARGB4444),
};

#define NUM_FORMATS ARRAY_SIZE(formats)

static const struct ge2d_fmt *find_fmt(struct v4l2_format *f)
{
	unsigned int i;

	for (i = 0; i < NUM_FORMATS; i++) {
		if (formats[i].fourcc == f->fmt.pix.pixelformat)
			return &formats[i];
	}

	/*
	 * TRY_FMT/S_FMT should never return an error when the requested format
	 * is not supported. Drivers should always return a valid format,
	 * preferably a format that is as widely supported by applications as
	 * possible.
	 */
	return &formats[0];
}

static struct ge2d_frame *get_frame(struct ge2d_ctx *ctx,
				    enum v4l2_buf_type type)
{
	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		return &ctx->in;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		return &ctx->out;
	default:
		/* This should never happen, warn and return OUTPUT frame */
		dev_warn(ctx->ge2d->dev, "%s: invalid buffer type\n", __func__);
		return &ctx->in;
	}
}

static void ge2d_hw_start(struct meson_ge2d *ge2d)
{
	struct ge2d_ctx *ctx = ge2d->curr;
	u32 reg;

	/* Reset */
	regmap_update_bits(ge2d->map, GE2D_GEN_CTRL1,
			   GE2D_SOFT_RST, GE2D_SOFT_RST);
	regmap_update_bits(ge2d->map, GE2D_GEN_CTRL1,
			   GE2D_SOFT_RST, 0);

	usleep_range(100, 200);

	/* Implement CANVAS for non-AXG */
	regmap_write(ge2d->map, GE2D_SRC1_BADDR_CTRL,
		     (vb2_dma_contig_plane_dma_addr(&ctx->in.buf->vb2_buf, 0) + 7) >> 3);
	regmap_write(ge2d->map, GE2D_SRC1_STRIDE_CTRL,
		     (ctx->in.pix_fmt.bytesperline + 7) >> 3);
	regmap_write(ge2d->map, GE2D_SRC2_BADDR_CTRL,
		     (vb2_dma_contig_plane_dma_addr(&ctx->out.buf->vb2_buf, 0) + 7) >> 3);
	regmap_write(ge2d->map, GE2D_SRC2_STRIDE_CTRL,
		     (ctx->out.pix_fmt.bytesperline + 7) >> 3);
	regmap_write(ge2d->map, GE2D_DST1_BADDR_CTRL,
		     (vb2_dma_contig_plane_dma_addr(&ctx->out.buf->vb2_buf, 0) + 7) >> 3);
	regmap_write(ge2d->map, GE2D_DST1_STRIDE_CTRL,
		     (ctx->out.pix_fmt.bytesperline + 7) >> 3);

	regmap_write(ge2d->map, GE2D_GEN_CTRL0, 0);
	regmap_write(ge2d->map, GE2D_GEN_CTRL1,
		     FIELD_PREP(GE2D_INTERRUPT_CTRL, 2) |
		     FIELD_PREP(GE2D_SRC2_BURST_SIZE_CTRL, 3) |
		     FIELD_PREP(GE2D_SRC1_BURST_SIZE_CTRL, 0x3f));

	regmap_write(ge2d->map, GE2D_GEN_CTRL2,
		     GE2D_SRC1_LITTLE_ENDIAN |
		     GE2D_SRC2_LITTLE_ENDIAN |
		     GE2D_DST_LITTLE_ENDIAN |
		     FIELD_PREP(GE2D_DST1_COLOR_MAP, ctx->out.fmt->hw_map) |
		     FIELD_PREP(GE2D_DST1_FORMAT, ctx->out.fmt->hw_fmt) |
		     FIELD_PREP(GE2D_SRC2_COLOR_MAP, ctx->out.fmt->hw_map) |
		     FIELD_PREP(GE2D_SRC2_FORMAT, ctx->out.fmt->hw_fmt) |
		     FIELD_PREP(GE2D_SRC1_COLOR_MAP, ctx->in.fmt->hw_map) |
		     FIELD_PREP(GE2D_SRC1_FORMAT, ctx->in.fmt->hw_fmt));
	regmap_write(ge2d->map, GE2D_GEN_CTRL3,
		     GE2D_DST1_ENABLE);

	regmap_write(ge2d->map, GE2D_SRC1_CLIPY_START_END,
		     FIELD_PREP(GE2D_START, ctx->in.crop.top) |
		     FIELD_PREP(GE2D_END, ctx->in.crop.top + ctx->in.crop.height - 1));
	regmap_write(ge2d->map, GE2D_SRC1_CLIPX_START_END,
		     FIELD_PREP(GE2D_START, ctx->in.crop.left) |
		     FIELD_PREP(GE2D_END, ctx->in.crop.left + ctx->in.crop.width - 1));
	regmap_write(ge2d->map, GE2D_SRC2_CLIPY_START_END,
		     FIELD_PREP(GE2D_START, ctx->out.crop.top) |
		     FIELD_PREP(GE2D_END, ctx->out.crop.top + ctx->out.crop.height - 1));
	regmap_write(ge2d->map, GE2D_SRC2_CLIPX_START_END,
		     FIELD_PREP(GE2D_START, ctx->out.crop.left) |
		     FIELD_PREP(GE2D_END, ctx->out.crop.left + ctx->out.crop.width - 1));
	regmap_write(ge2d->map, GE2D_DST_CLIPY_START_END,
		     FIELD_PREP(GE2D_START, ctx->out.crop.top) |
		     FIELD_PREP(GE2D_END, ctx->out.crop.top + ctx->out.crop.height - 1));
	regmap_write(ge2d->map, GE2D_DST_CLIPX_START_END,
		     FIELD_PREP(GE2D_START, ctx->out.crop.left) |
		     FIELD_PREP(GE2D_END, ctx->out.crop.left + ctx->out.crop.width - 1));

	regmap_write(ge2d->map, GE2D_SRC1_Y_START_END,
		     FIELD_PREP(GE2D_END, ctx->in.pix_fmt.height - 1));
	regmap_write(ge2d->map, GE2D_SRC1_X_START_END,
		     FIELD_PREP(GE2D_END, ctx->in.pix_fmt.width - 1));
	regmap_write(ge2d->map, GE2D_SRC2_Y_START_END,
		     FIELD_PREP(GE2D_END, ctx->out.pix_fmt.height - 1));
	regmap_write(ge2d->map, GE2D_SRC2_X_START_END,
		     FIELD_PREP(GE2D_END, ctx->out.pix_fmt.width - 1));
	regmap_write(ge2d->map, GE2D_DST_Y_START_END,
		     FIELD_PREP(GE2D_END, ctx->out.pix_fmt.height - 1));
	regmap_write(ge2d->map, GE2D_DST_X_START_END,
		     FIELD_PREP(GE2D_END, ctx->out.pix_fmt.width - 1));

	/* Color, no blend, use source color */
	reg = GE2D_ALU_DO_COLOR_OPERATION_LOGIC(LOGIC_OPERATION_COPY,
						COLOR_FACTOR_SRC_COLOR);

	if (ctx->in.fmt->alpha && ctx->out.fmt->alpha)
		/* Take source alpha */
		reg |= GE2D_ALU_DO_ALPHA_OPERATION_LOGIC(LOGIC_OPERATION_COPY,
							 COLOR_FACTOR_SRC_ALPHA);
	else if (!ctx->out.fmt->alpha)
		/* Set alpha to 0 */
		reg |= GE2D_ALU_DO_ALPHA_OPERATION_LOGIC(LOGIC_OPERATION_SET,
							 COLOR_FACTOR_ZERO);
	else
		/* Keep original alpha */
		reg |= GE2D_ALU_DO_ALPHA_OPERATION_LOGIC(LOGIC_OPERATION_COPY,
							 COLOR_FACTOR_DST_ALPHA);

	regmap_write(ge2d->map, GE2D_ALU_OP_CTRL, reg);

	/* Start */
	regmap_write(ge2d->map, GE2D_CMD_CTRL,
		     (ctx->xy_swap ? GE2D_DST_XY_SWAP : 0) |
		     (ctx->hflip ? GE2D_SRC1_Y_REV : 0) |
		     (ctx->vflip ? GE2D_SRC1_X_REV : 0) |
		     GE2D_CBUS_CMD_WR);
}

static void device_run(void *priv)
{
	struct ge2d_ctx *ctx = priv;
	struct meson_ge2d *ge2d = ctx->ge2d;

	ge2d->curr = ctx;

	ctx->in.buf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	ctx->out.buf = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);

	ge2d_hw_start(ge2d);
}

static irqreturn_t ge2d_isr(int irq, void *priv)
{
	struct meson_ge2d *ge2d = priv;
	u32 intr;

	regmap_read(ge2d->map, GE2D_STATUS0, &intr);

	if (!(intr & GE2D_GE2D_BUSY)) {
		struct vb2_v4l2_buffer *src, *dst;
		struct ge2d_ctx *ctx = ge2d->curr;

		ge2d->curr = NULL;

		src = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		dst = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);

		src->sequence = ctx->sequence_out++;
		dst->sequence = ctx->sequence_cap++;

		dst->timecode = src->timecode;
		dst->vb2_buf.timestamp = src->vb2_buf.timestamp;
		dst->flags = src->flags;

		v4l2_m2m_buf_done(src, VB2_BUF_STATE_DONE);
		v4l2_m2m_buf_done(dst, VB2_BUF_STATE_DONE);
		v4l2_m2m_job_finish(ge2d->m2m_dev, ctx->fh.m2m_ctx);
	}

	return IRQ_HANDLED;
}

static const struct v4l2_m2m_ops ge2d_m2m_ops = {
	.device_run = device_run,
};

static int ge2d_queue_setup(struct vb2_queue *vq,
			    unsigned int *nbuffers, unsigned int *nplanes,
			    unsigned int sizes[], struct device *alloc_devs[])
{
	struct ge2d_ctx *ctx = vb2_get_drv_priv(vq);
	struct ge2d_frame *f = get_frame(ctx, vq->type);

	if (*nplanes)
		return sizes[0] < f->pix_fmt.sizeimage ? -EINVAL : 0;

	sizes[0] = f->pix_fmt.sizeimage;
	*nplanes = 1;

	return 0;
}

static int ge2d_buf_prepare(struct vb2_buffer *vb)
{
	struct ge2d_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct ge2d_frame *f = get_frame(ctx, vb->vb2_queue->type);

	vbuf->field = V4L2_FIELD_NONE;

	vb2_set_plane_payload(vb, 0, f->pix_fmt.sizeimage);

	return 0;
}

static void ge2d_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct ge2d_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
}

static int ge2d_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct ge2d_ctx *ctx = vb2_get_drv_priv(vq);

	if (V4L2_TYPE_IS_OUTPUT(vq->type))
		ctx->sequence_out = 0;
	else
		ctx->sequence_cap = 0;

	return 0;
}

static void ge2d_stop_streaming(struct vb2_queue *vq)
{
	struct ge2d_ctx *ctx = vb2_get_drv_priv(vq);
	struct vb2_v4l2_buffer *vbuf;

	for (;;) {
		if (V4L2_TYPE_IS_OUTPUT(vq->type))
			vbuf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		else
			vbuf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
		if (!vbuf)
			break;
		v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_ERROR);
	}
}

static const struct vb2_ops ge2d_qops = {
	.queue_setup = ge2d_queue_setup,
	.buf_prepare = ge2d_buf_prepare,
	.buf_queue = ge2d_buf_queue,
	.start_streaming = ge2d_start_streaming,
	.stop_streaming = ge2d_stop_streaming,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
};

static int
queue_init(void *priv, struct vb2_queue *src_vq, struct vb2_queue *dst_vq)
{
	struct ge2d_ctx *ctx = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->drv_priv = ctx;
	src_vq->ops = &ge2d_qops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &ctx->ge2d->mutex;
	src_vq->dev = ctx->ge2d->v4l2_dev.dev;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->ops = &ge2d_qops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &ctx->ge2d->mutex;
	dst_vq->dev = ctx->ge2d->v4l2_dev.dev;

	return vb2_queue_init(dst_vq);
}

static int
vidioc_querycap(struct file *file, void *priv, struct v4l2_capability *cap)
{
	strscpy(cap->driver, GE2D_NAME, sizeof(cap->driver));
	strscpy(cap->card, GE2D_NAME, sizeof(cap->card));
	strscpy(cap->bus_info, "platform:" GE2D_NAME, sizeof(cap->bus_info));

	return 0;
}

static int vidioc_enum_fmt(struct file *file, void *priv, struct v4l2_fmtdesc *f)
{
	const struct ge2d_fmt *fmt;

	if (f->index >= NUM_FORMATS)
		return -EINVAL;

	fmt = &formats[f->index];
	f->pixelformat = fmt->fourcc;

	return 0;
}

static int vidioc_g_selection(struct file *file, void *priv,
			      struct v4l2_selection *s)
{
	struct ge2d_ctx *ctx = priv;
	struct ge2d_frame *f;
	bool use_frame = false;

	if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT &&
	    s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	f = get_frame(ctx, s->type);

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
		s->r.width = f->pix_fmt.width;
		s->r.height = f->pix_fmt.height;
	}

	return 0;
}

static int vidioc_s_selection(struct file *file, void *priv,
			      struct v4l2_selection *s)
{
	struct ge2d_ctx *ctx = priv;
	struct meson_ge2d *ge2d = ctx->ge2d;
	struct ge2d_frame *f;
	int ret = 0;

	if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT &&
	    s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	f = get_frame(ctx, s->type);

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
		v4l2_err(&ge2d->v4l2_dev,
			 "doesn't support negative values for top & left.\n");
		return -EINVAL;
	}

	if (s->r.left + s->r.width > f->pix_fmt.width ||
	    s->r.top + s->r.height > f->pix_fmt.height) {
		v4l2_err(&ge2d->v4l2_dev, "unsupported rectangle value.\n");
		return -EINVAL;
	}

	f->crop = s->r;

	return ret;
}

static void vidioc_setup_cap_fmt(struct ge2d_ctx *ctx, struct v4l2_pix_format *f)
{
	struct ge2d_frame *frm_out = get_frame(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);

	*f = frm_out->pix_fmt;

	if (ctx->xy_swap) {
		f->width = frm_out->pix_fmt.height;
		f->height = frm_out->pix_fmt.width;
	}
}

static int vidioc_try_fmt_cap(struct file *file, void *priv, struct v4l2_format *f)
{
	const struct ge2d_fmt *fmt = find_fmt(f);
	struct ge2d_ctx *ctx = priv;
	struct v4l2_pix_format fmt_cap;

	vidioc_setup_cap_fmt(ctx, &fmt_cap);

	fmt_cap.pixelformat = fmt->fourcc;

	fmt_cap.bytesperline = max(f->fmt.pix.bytesperline,
				   ALIGN((fmt_cap.width * fmt->depth) >> 3, 8));

	fmt_cap.sizeimage = max(f->fmt.pix.sizeimage,
				fmt_cap.height * fmt_cap.bytesperline);

	f->fmt.pix = fmt_cap;

	return 0;
}

static int vidioc_s_fmt_cap(struct file *file, void *priv, struct v4l2_format *f)
{
	struct ge2d_ctx *ctx = priv;
	struct meson_ge2d *ge2d = ctx->ge2d;
	struct vb2_queue *vq;
	struct ge2d_frame *frm;
	int ret = 0;

	/* Adjust all values accordingly to the hardware capabilities
	 * and chosen format.
	 */
	ret = vidioc_try_fmt_cap(file, priv, f);
	if (ret)
		return ret;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (vb2_is_busy(vq)) {
		v4l2_err(&ge2d->v4l2_dev, "queue (%d) bust\n", f->type);
		return -EBUSY;
	}

	frm = get_frame(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);

	frm->pix_fmt = f->fmt.pix;
	frm->fmt = find_fmt(f);
	f->fmt.pix.pixelformat = frm->fmt->fourcc;

	/* Reset crop settings */
	frm->crop.left = 0;
	frm->crop.top = 0;
	frm->crop.width = frm->pix_fmt.width;
	frm->crop.height = frm->pix_fmt.height;

	return 0;
}

static int vidioc_g_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct ge2d_ctx *ctx = priv;
	struct vb2_queue *vq;
	struct ge2d_frame *frm;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;

	frm = get_frame(ctx, f->type);

	f->fmt.pix = frm->pix_fmt;
	f->fmt.pix.pixelformat = frm->fmt->fourcc;

	return 0;
}

static int vidioc_try_fmt_out(struct file *file, void *priv, struct v4l2_format *f)
{
	const struct ge2d_fmt *fmt = find_fmt(f);

	f->fmt.pix.field = V4L2_FIELD_NONE;
	f->fmt.pix.pixelformat = fmt->fourcc;

	if (f->fmt.pix.width > MAX_WIDTH)
		f->fmt.pix.width = MAX_WIDTH;
	if (f->fmt.pix.height > MAX_HEIGHT)
		f->fmt.pix.height = MAX_HEIGHT;

	f->fmt.pix.bytesperline = max(f->fmt.pix.bytesperline,
				      ALIGN((f->fmt.pix.width * fmt->depth) >> 3, 8));

	f->fmt.pix.sizeimage = max(f->fmt.pix.sizeimage,
				   f->fmt.pix.height * f->fmt.pix.bytesperline);

	return 0;
}

static int vidioc_s_fmt_out(struct file *file, void *priv, struct v4l2_format *f)
{
	struct ge2d_ctx *ctx = priv;
	struct meson_ge2d *ge2d = ctx->ge2d;
	struct vb2_queue *vq;
	struct ge2d_frame *frm, *frm_cap;
	int ret = 0;

	/* Adjust all values accordingly to the hardware capabilities
	 * and chosen format.
	 */
	ret = vidioc_try_fmt_out(file, priv, f);
	if (ret)
		return ret;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (vb2_is_busy(vq)) {
		v4l2_err(&ge2d->v4l2_dev, "queue (%d) bust\n", f->type);
		return -EBUSY;
	}

	frm = get_frame(ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT);
	frm_cap = get_frame(ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);

	frm->pix_fmt = f->fmt.pix;
	frm->fmt = find_fmt(f);
	f->fmt.pix.pixelformat = frm->fmt->fourcc;

	/* Reset crop settings */
	frm->crop.left = 0;
	frm->crop.top = 0;
	frm->crop.width = frm->pix_fmt.width;
	frm->crop.height = frm->pix_fmt.height;

	/* Propagate settings to capture */
	vidioc_setup_cap_fmt(ctx, &frm_cap->pix_fmt);

	return 0;
}

static const struct v4l2_ioctl_ops ge2d_ioctl_ops = {
	.vidioc_querycap = vidioc_querycap,

	.vidioc_enum_fmt_vid_cap = vidioc_enum_fmt,
	.vidioc_g_fmt_vid_cap = vidioc_g_fmt,
	.vidioc_try_fmt_vid_cap = vidioc_try_fmt_cap,
	.vidioc_s_fmt_vid_cap = vidioc_s_fmt_cap,

	.vidioc_enum_fmt_vid_out = vidioc_enum_fmt,
	.vidioc_g_fmt_vid_out = vidioc_g_fmt,
	.vidioc_try_fmt_vid_out = vidioc_try_fmt_out,
	.vidioc_s_fmt_vid_out = vidioc_s_fmt_out,

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

static int ge2d_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ge2d_ctx *ctx = container_of(ctrl->handler, struct ge2d_ctx,
					   ctrl_handler);
	struct v4l2_pix_format fmt;
	struct vb2_queue *vq;

	switch (ctrl->id) {
	case V4L2_CID_HFLIP:
		ctx->hflip = ctrl->val;
		break;
	case V4L2_CID_VFLIP:
		ctx->vflip = ctrl->val;
		break;
	case V4L2_CID_ROTATE:
		vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE);
		if (vb2_is_busy(vq))
			return -EBUSY;

		if (ctrl->val == 90) {
			ctx->hflip = 0;
			ctx->vflip = 1;
			ctx->xy_swap = 1;
		} else if (ctrl->val == 180) {
			ctx->hflip = 1;
			ctx->vflip = 1;
			ctx->xy_swap = 0;
		} else if (ctrl->val == 270) {
			ctx->hflip = 1;
			ctx->vflip = 0;
			ctx->xy_swap = 1;
		} else {
			ctx->hflip = 0;
			ctx->vflip = 0;
			ctx->xy_swap = 0;
		}

		vidioc_setup_cap_fmt(ctx, &fmt);

		/*
		 * If the rotation parameter changes the OUTPUT frames
		 * parameters, take them in account
		 */
		ctx->out.pix_fmt = fmt;

		break;
	}

	return 0;
}

static const struct v4l2_ctrl_ops ge2d_ctrl_ops = {
	.s_ctrl = ge2d_s_ctrl,
};

static int ge2d_setup_ctrls(struct ge2d_ctx *ctx)
{
	struct meson_ge2d *ge2d = ctx->ge2d;

	v4l2_ctrl_handler_init(&ctx->ctrl_handler, 4);

	v4l2_ctrl_new_std(&ctx->ctrl_handler, &ge2d_ctrl_ops,
			  V4L2_CID_HFLIP, 0, 1, 1, 0);

	v4l2_ctrl_new_std(&ctx->ctrl_handler, &ge2d_ctrl_ops,
			  V4L2_CID_VFLIP, 0, 1, 1, 0);

	v4l2_ctrl_new_std(&ctx->ctrl_handler, &ge2d_ctrl_ops,
			  V4L2_CID_ROTATE, 0, 270, 90, 0);

	if (ctx->ctrl_handler.error) {
		int err = ctx->ctrl_handler.error;

		v4l2_err(&ge2d->v4l2_dev, "%s failed\n", __func__);
		v4l2_ctrl_handler_free(&ctx->ctrl_handler);
		return err;
	}

	return 0;
}

static const struct ge2d_frame def_frame = {
	.pix_fmt = {
		.width = DEFAULT_WIDTH,
		.height = DEFAULT_HEIGHT,
		.bytesperline = DEFAULT_STRIDE,
		.sizeimage = DEFAULT_STRIDE * DEFAULT_HEIGHT,
		.field = V4L2_FIELD_NONE,
	},
	.crop.width = DEFAULT_WIDTH,
	.crop.height = DEFAULT_HEIGHT,
	.fmt = &formats[0],
};

static int ge2d_open(struct file *file)
{
	struct meson_ge2d *ge2d = video_drvdata(file);
	struct ge2d_ctx *ctx = NULL;
	int ret = 0;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	ctx->ge2d = ge2d;

	/* Set default formats */
	ctx->in = def_frame;
	ctx->out = def_frame;

	if (mutex_lock_interruptible(&ge2d->mutex)) {
		kfree(ctx);
		return -ERESTARTSYS;
	}
	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(ge2d->m2m_dev, ctx, &queue_init);
	if (IS_ERR(ctx->fh.m2m_ctx)) {
		ret = PTR_ERR(ctx->fh.m2m_ctx);
		mutex_unlock(&ge2d->mutex);
		kfree(ctx);
		return ret;
	}
	v4l2_fh_init(&ctx->fh, video_devdata(file));
	file->private_data = &ctx->fh;
	v4l2_fh_add(&ctx->fh);

	ge2d_setup_ctrls(ctx);

	/* Write the default values to the ctx struct */
	v4l2_ctrl_handler_setup(&ctx->ctrl_handler);

	ctx->fh.ctrl_handler = &ctx->ctrl_handler;
	mutex_unlock(&ge2d->mutex);

	return 0;
}

static int ge2d_release(struct file *file)
{
	struct ge2d_ctx *ctx =
		container_of(file->private_data, struct ge2d_ctx, fh);
	struct meson_ge2d *ge2d = ctx->ge2d;

	mutex_lock(&ge2d->mutex);

	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);

	v4l2_ctrl_handler_free(&ctx->ctrl_handler);
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	kfree(ctx);

	mutex_unlock(&ge2d->mutex);

	return 0;
}

static const struct v4l2_file_operations ge2d_fops = {
	.owner = THIS_MODULE,
	.open = ge2d_open,
	.release = ge2d_release,
	.poll = v4l2_m2m_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap = v4l2_m2m_fop_mmap,
};

static const struct video_device ge2d_videodev = {
	.name = "meson-ge2d",
	.fops = &ge2d_fops,
	.ioctl_ops = &ge2d_ioctl_ops,
	.minor = -1,
	.release = video_device_release,
	.vfl_dir = VFL_DIR_M2M,
	.device_caps = V4L2_CAP_VIDEO_M2M | V4L2_CAP_STREAMING,
};

static const struct regmap_config meson_ge2d_regmap_conf = {
	.reg_bits = 8,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = GE2D_SRC2_STRIDE_CTRL,
};

static int ge2d_probe(struct platform_device *pdev)
{
	struct reset_control *rst;
	struct video_device *vfd;
	struct meson_ge2d *ge2d;
	void __iomem *regs;
	int ret = 0;
	int irq;

	if (!pdev->dev.of_node)
		return -ENODEV;

	ge2d = devm_kzalloc(&pdev->dev, sizeof(*ge2d), GFP_KERNEL);
	if (!ge2d)
		return -ENOMEM;

	ge2d->dev = &pdev->dev;
	mutex_init(&ge2d->mutex);

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	ge2d->map = devm_regmap_init_mmio(ge2d->dev, regs,
					  &meson_ge2d_regmap_conf);
	if (IS_ERR(ge2d->map))
		return PTR_ERR(ge2d->map);

	irq = platform_get_irq(pdev, 0);
	ret = devm_request_irq(ge2d->dev, irq, ge2d_isr, 0,
			       dev_name(ge2d->dev), ge2d);
	if (ret < 0) {
		dev_err(ge2d->dev, "failed to request irq\n");
		return ret;
	}

	rst = devm_reset_control_get(ge2d->dev, NULL);
	if (IS_ERR(rst)) {
		dev_err(ge2d->dev, "failed to get core reset controller\n");
		return PTR_ERR(rst);
	}

	ge2d->clk = devm_clk_get(ge2d->dev, NULL);
	if (IS_ERR(ge2d->clk)) {
		dev_err(ge2d->dev, "failed to get clock\n");
		return PTR_ERR(ge2d->clk);
	}

	reset_control_assert(rst);
	udelay(1);
	reset_control_deassert(rst);

	ret = clk_prepare_enable(ge2d->clk);
	if (ret) {
		dev_err(ge2d->dev, "Cannot enable ge2d sclk: %d\n", ret);
		return ret;
	}

	ret = v4l2_device_register(&pdev->dev, &ge2d->v4l2_dev);
	if (ret)
		goto disable_clks;

	vfd = video_device_alloc();
	if (!vfd) {
		v4l2_err(&ge2d->v4l2_dev, "Failed to allocate video device\n");
		ret = -ENOMEM;
		goto unreg_v4l2_dev;
	}

	*vfd = ge2d_videodev;
	vfd->lock = &ge2d->mutex;
	vfd->v4l2_dev = &ge2d->v4l2_dev;

	video_set_drvdata(vfd, ge2d);
	ge2d->vfd = vfd;

	platform_set_drvdata(pdev, ge2d);
	ge2d->m2m_dev = v4l2_m2m_init(&ge2d_m2m_ops);
	if (IS_ERR(ge2d->m2m_dev)) {
		v4l2_err(&ge2d->v4l2_dev, "Failed to init mem2mem device\n");
		ret = PTR_ERR(ge2d->m2m_dev);
		goto rel_vdev;
	}

	ret = video_register_device(vfd, VFL_TYPE_VIDEO, -1);
	if (ret) {
		v4l2_err(&ge2d->v4l2_dev, "Failed to register video device\n");
		goto rel_m2m;
	}

	v4l2_info(&ge2d->v4l2_dev, "Registered %s as /dev/%s\n",
		  vfd->name, video_device_node_name(vfd));

	return 0;

rel_m2m:
	v4l2_m2m_release(ge2d->m2m_dev);
rel_vdev:
	video_device_release(ge2d->vfd);
unreg_v4l2_dev:
	v4l2_device_unregister(&ge2d->v4l2_dev);
disable_clks:
	clk_disable_unprepare(ge2d->clk);

	return ret;
}

static int ge2d_remove(struct platform_device *pdev)
{
	struct meson_ge2d *ge2d = platform_get_drvdata(pdev);

	video_unregister_device(ge2d->vfd);
	v4l2_m2m_release(ge2d->m2m_dev);
	video_device_release(ge2d->vfd);
	v4l2_device_unregister(&ge2d->v4l2_dev);
	clk_disable_unprepare(ge2d->clk);

	return 0;
}

static const struct of_device_id meson_ge2d_match[] = {
	{
		.compatible = "amlogic,axg-ge2d",
	},
	{},
};

MODULE_DEVICE_TABLE(of, meson_ge2d_match);

static struct platform_driver ge2d_drv = {
	.probe = ge2d_probe,
	.remove = ge2d_remove,
	.driver = {
		.name = "meson-ge2d",
		.of_match_table = meson_ge2d_match,
	},
};

module_platform_driver(ge2d_drv);

MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_DESCRIPTION("Amlogic 2D Graphic Acceleration Unit");
MODULE_LICENSE("GPL");
