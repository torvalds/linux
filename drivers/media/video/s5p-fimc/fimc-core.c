/*
 * S5P camera interface (video postprocessor) driver
 *
 * Copyright (c) 2010 Samsung Electronics
 *
 * Sylwester Nawrocki, <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 2 of the License,
 * or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/bug.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/list.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf-dma-contig.h>

#include "fimc-core.h"

static char *fimc_clock_name[NUM_FIMC_CLOCKS] = { "sclk_fimc", "fimc" };

static struct fimc_fmt fimc_formats[] = {
	{
		.name	= "RGB565",
		.fourcc	= V4L2_PIX_FMT_RGB565X,
		.depth	= 16,
		.color	= S5P_FIMC_RGB565,
		.buff_cnt = 1,
		.planes_cnt = 1
	}, {
		.name	= "BGR666",
		.fourcc	= V4L2_PIX_FMT_BGR666,
		.depth	= 32,
		.color	= S5P_FIMC_RGB666,
		.buff_cnt = 1,
		.planes_cnt = 1
	}, {
		.name = "XRGB-8-8-8-8, 24 bpp",
		.fourcc	= V4L2_PIX_FMT_RGB24,
		.depth = 32,
		.color	= S5P_FIMC_RGB888,
		.buff_cnt = 1,
		.planes_cnt = 1
	}, {
		.name	= "YUV 4:2:2 packed, YCbYCr",
		.fourcc	= V4L2_PIX_FMT_YUYV,
		.depth	= 16,
		.color	= S5P_FIMC_YCBYCR422,
		.buff_cnt = 1,
		.planes_cnt = 1
		}, {
		.name	= "YUV 4:2:2 packed, CbYCrY",
		.fourcc	= V4L2_PIX_FMT_UYVY,
		.depth	= 16,
		.color	= S5P_FIMC_CBYCRY422,
		.buff_cnt = 1,
		.planes_cnt = 1
	}, {
		.name	= "YUV 4:2:2 packed, CrYCbY",
		.fourcc	= V4L2_PIX_FMT_VYUY,
		.depth	= 16,
		.color	= S5P_FIMC_CRYCBY422,
		.buff_cnt = 1,
		.planes_cnt = 1
	}, {
		.name	= "YUV 4:2:2 packed, YCrYCb",
		.fourcc	= V4L2_PIX_FMT_YVYU,
		.depth	= 16,
		.color	= S5P_FIMC_YCRYCB422,
		.buff_cnt = 1,
		.planes_cnt = 1
	}, {
		.name	= "YUV 4:2:2 planar, Y/Cb/Cr",
		.fourcc	= V4L2_PIX_FMT_YUV422P,
		.depth	= 12,
		.color	= S5P_FIMC_YCBCR422,
		.buff_cnt = 1,
		.planes_cnt = 3
	}, {
		.name	= "YUV 4:2:2 planar, Y/CbCr",
		.fourcc	= V4L2_PIX_FMT_NV16,
		.depth	= 16,
		.color	= S5P_FIMC_YCBCR422,
		.buff_cnt = 1,
		.planes_cnt = 2
	}, {
		.name	= "YUV 4:2:2 planar, Y/CrCb",
		.fourcc	= V4L2_PIX_FMT_NV61,
		.depth	= 16,
		.color	= S5P_FIMC_RGB565,
		.buff_cnt = 1,
		.planes_cnt = 2
	}, {
		.name	= "YUV 4:2:0 planar, YCbCr",
		.fourcc	= V4L2_PIX_FMT_YUV420,
		.depth	= 12,
		.color	= S5P_FIMC_YCBCR420,
		.buff_cnt = 1,
		.planes_cnt = 3
	}, {
		.name	= "YUV 4:2:0 planar, Y/CbCr",
		.fourcc	= V4L2_PIX_FMT_NV12,
		.depth	= 12,
		.color	= S5P_FIMC_YCBCR420,
		.buff_cnt = 1,
		.planes_cnt = 2
	}
 };

static struct v4l2_queryctrl fimc_ctrls[] = {
	{
		.id		= V4L2_CID_HFLIP,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Horizontal flip",
		.minimum	= 0,
		.maximum	= 1,
		.default_value	= 0,
	},
	{
		.id		= V4L2_CID_VFLIP,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Vertical flip",
		.minimum	= 0,
		.maximum	= 1,
		.default_value	= 0,
	},
	{
		.id		= V4L2_CID_ROTATE,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Rotation (CCW)",
		.minimum	= 0,
		.maximum	= 270,
		.step		= 90,
		.default_value	= 0,
	},
};


static struct v4l2_queryctrl *get_ctrl(int id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(fimc_ctrls); ++i)
		if (id == fimc_ctrls[i].id)
			return &fimc_ctrls[i];
	return NULL;
}

static int fimc_check_scaler_ratio(struct v4l2_rect *r, struct fimc_frame *f)
{
	if (r->width > f->width) {
		if (f->width > (r->width * SCALER_MAX_HRATIO))
			return -EINVAL;
	} else {
		if ((f->width * SCALER_MAX_HRATIO) < r->width)
			return -EINVAL;
	}

	if (r->height > f->height) {
		if (f->height > (r->height * SCALER_MAX_VRATIO))
			return -EINVAL;
	} else {
		if ((f->height * SCALER_MAX_VRATIO) < r->height)
			return -EINVAL;
	}

	return 0;
}

static int fimc_get_scaler_factor(u32 src, u32 tar, u32 *ratio, u32 *shift)
{
	if (src >= tar * 64) {
		return -EINVAL;
	} else if (src >= tar * 32) {
		*ratio = 32;
		*shift = 5;
	} else if (src >= tar * 16) {
		*ratio = 16;
		*shift = 4;
	} else if (src >= tar * 8) {
		*ratio = 8;
		*shift = 3;
	} else if (src >= tar * 4) {
		*ratio = 4;
		*shift = 2;
	} else if (src >= tar * 2) {
		*ratio = 2;
		*shift = 1;
	} else {
		*ratio = 1;
		*shift = 0;
	}

	return 0;
}

static int fimc_set_scaler_info(struct fimc_ctx *ctx)
{
	struct fimc_scaler *sc = &ctx->scaler;
	struct fimc_frame *s_frame = &ctx->s_frame;
	struct fimc_frame *d_frame = &ctx->d_frame;
	int tx, ty, sx, sy;
	int ret;

	tx = d_frame->width;
	ty = d_frame->height;
	if (tx <= 0 || ty <= 0) {
		v4l2_err(&ctx->fimc_dev->m2m.v4l2_dev,
			"invalid target size: %d x %d", tx, ty);
		return -EINVAL;
	}

	sx = s_frame->width;
	sy = s_frame->height;
	if (sx <= 0 || sy <= 0) {
		err("invalid source size: %d x %d", sx, sy);
		return -EINVAL;
	}

	sc->real_width = sx;
	sc->real_height = sy;
	dbg("sx= %d, sy= %d, tx= %d, ty= %d", sx, sy, tx, ty);

	ret = fimc_get_scaler_factor(sx, tx, &sc->pre_hratio, &sc->hfactor);
	if (ret)
		return ret;

	ret = fimc_get_scaler_factor(sy, ty,  &sc->pre_vratio, &sc->vfactor);
	if (ret)
		return ret;

	sc->pre_dst_width = sx / sc->pre_hratio;
	sc->pre_dst_height = sy / sc->pre_vratio;

	sc->main_hratio = (sx << 8) / (tx << sc->hfactor);
	sc->main_vratio = (sy << 8) / (ty << sc->vfactor);

	sc->scaleup_h = (tx >= sx) ? 1 : 0;
	sc->scaleup_v = (ty >= sy) ? 1 : 0;

	/* check to see if input and output size/format differ */
	if (s_frame->fmt->color == d_frame->fmt->color
		&& s_frame->width == d_frame->width
		&& s_frame->height == d_frame->height)
		sc->copy_mode = 1;
	else
		sc->copy_mode = 0;

	return 0;
}


static irqreturn_t fimc_isr(int irq, void *priv)
{
	struct fimc_vid_buffer *src_buf, *dst_buf;
	struct fimc_dev *fimc = (struct fimc_dev *)priv;
	struct fimc_ctx *ctx;

	BUG_ON(!fimc);
	fimc_hw_clear_irq(fimc);

	spin_lock(&fimc->slock);

	if (test_and_clear_bit(ST_M2M_PEND, &fimc->state)) {
		ctx = v4l2_m2m_get_curr_priv(fimc->m2m.m2m_dev);
		if (!ctx || !ctx->m2m_ctx)
			goto isr_unlock;
		src_buf = v4l2_m2m_src_buf_remove(ctx->m2m_ctx);
		dst_buf = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx);
		if (src_buf && dst_buf) {
			spin_lock(&fimc->irqlock);
			src_buf->vb.state = dst_buf->vb.state =  VIDEOBUF_DONE;
			wake_up(&src_buf->vb.done);
			wake_up(&dst_buf->vb.done);
			spin_unlock(&fimc->irqlock);
			v4l2_m2m_job_finish(fimc->m2m.m2m_dev, ctx->m2m_ctx);
		}
	}

isr_unlock:
	spin_unlock(&fimc->slock);
	return IRQ_HANDLED;
}

/* The color format (planes_cnt, buff_cnt) must be already configured. */
static int fimc_prepare_addr(struct fimc_ctx *ctx,
		struct fimc_vid_buffer *buf, enum v4l2_buf_type type)
{
	struct fimc_frame *frame;
	struct fimc_addr *paddr;
	u32 pix_size;
	int ret = 0;

	frame = ctx_m2m_get_frame(ctx, type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);
	paddr = &frame->paddr;

	if (!buf)
		return -EINVAL;

	pix_size = frame->width * frame->height;

	dbg("buff_cnt= %d, planes_cnt= %d, frame->size= %d, pix_size= %d",
		frame->fmt->buff_cnt, frame->fmt->planes_cnt,
		frame->size, pix_size);

	if (frame->fmt->buff_cnt == 1) {
		paddr->y = videobuf_to_dma_contig(&buf->vb);
		switch (frame->fmt->planes_cnt) {
		case 1:
			paddr->cb = 0;
			paddr->cr = 0;
			break;
		case 2:
			/* decompose Y into Y/Cb */
			paddr->cb = (u32)(paddr->y + pix_size);
			paddr->cr = 0;
			break;
		case 3:
			paddr->cb = (u32)(paddr->y + pix_size);
			/* decompose Y into Y/Cb/Cr */
			if (S5P_FIMC_YCBCR420 == frame->fmt->color)
				paddr->cr = (u32)(paddr->cb
						+ (pix_size >> 2));
			else /* 422 */
				paddr->cr = (u32)(paddr->cb
						+ (pix_size >> 1));
			break;
		default:
			return -EINVAL;
		}
	}

	dbg("PHYS_ADDR: type= %d, y= 0x%X  cb= 0x%X cr= 0x%X ret= %d",
	type, paddr->y, paddr->cb, paddr->cr, ret);

	return ret;
}

/* Set order for 1 and 2 plane YCBCR 4:2:2 formats. */
static void fimc_set_yuv_order(struct fimc_ctx *ctx)
{
	/* The one only mode supported in SoC. */
	ctx->in_order_2p = S5P_FIMC_LSB_CRCB;
	ctx->out_order_2p = S5P_FIMC_LSB_CRCB;

	/* Set order for 1 plane input formats. */
	switch (ctx->s_frame.fmt->color) {
	case S5P_FIMC_YCRYCB422:
		ctx->in_order_1p = S5P_FIMC_IN_YCRYCB;
		break;
	case S5P_FIMC_CBYCRY422:
		ctx->in_order_1p = S5P_FIMC_IN_CBYCRY;
		break;
	case S5P_FIMC_CRYCBY422:
		ctx->in_order_1p = S5P_FIMC_IN_CRYCBY;
		break;
	case S5P_FIMC_YCBYCR422:
	default:
		ctx->in_order_1p = S5P_FIMC_IN_YCBYCR;
		break;
	}
	dbg("ctx->in_order_1p= %d", ctx->in_order_1p);

	switch (ctx->d_frame.fmt->color) {
	case S5P_FIMC_YCRYCB422:
		ctx->out_order_1p = S5P_FIMC_OUT_YCRYCB;
		break;
	case S5P_FIMC_CBYCRY422:
		ctx->out_order_1p = S5P_FIMC_OUT_CBYCRY;
		break;
	case S5P_FIMC_CRYCBY422:
		ctx->out_order_1p = S5P_FIMC_OUT_CRYCBY;
		break;
	case S5P_FIMC_YCBYCR422:
	default:
		ctx->out_order_1p = S5P_FIMC_OUT_YCBYCR;
		break;
	}
	dbg("ctx->out_order_1p= %d", ctx->out_order_1p);
}

/**
 * fimc_prepare_config - check dimensions, operation and color mode
 *			 and pre-calculate offset and the scaling coefficients.
 *
 * @ctx: hardware context information
 * @flags: flags indicating which parameters to check/update
 *
 * Return: 0 if dimensions are valid or non zero otherwise.
 */
static int fimc_prepare_config(struct fimc_ctx *ctx, u32 flags)
{
	struct fimc_frame *s_frame, *d_frame;
	struct fimc_vid_buffer *buf = NULL;
	struct samsung_fimc_variant *variant = ctx->fimc_dev->variant;
	int ret = 0;

	s_frame = &ctx->s_frame;
	d_frame = &ctx->d_frame;

	if (flags & FIMC_PARAMS) {
		if ((ctx->out_path == FIMC_DMA) &&
			(ctx->rotation == 90 || ctx->rotation == 270)) {
			swap(d_frame->f_width, d_frame->f_height);
			swap(d_frame->width, d_frame->height);
		}

		/* Prepare the output offset ratios for scaler. */
		d_frame->dma_offset.y_h = d_frame->offs_h;
		if (!variant->pix_hoff)
			d_frame->dma_offset.y_h *= (d_frame->fmt->depth >> 3);

		d_frame->dma_offset.y_v = d_frame->offs_v;

		d_frame->dma_offset.cb_h = d_frame->offs_h;
		d_frame->dma_offset.cb_v = d_frame->offs_v;

		d_frame->dma_offset.cr_h = d_frame->offs_h;
		d_frame->dma_offset.cr_v = d_frame->offs_v;

		if (!variant->pix_hoff && d_frame->fmt->planes_cnt == 3) {
			d_frame->dma_offset.cb_h >>= 1;
			d_frame->dma_offset.cb_v >>= 1;
			d_frame->dma_offset.cr_h >>= 1;
			d_frame->dma_offset.cr_v >>= 1;
		}

		dbg("out offset: color= %d, y_h= %d, y_v= %d",
			d_frame->fmt->color,
			d_frame->dma_offset.y_h, d_frame->dma_offset.y_v);

		/* Prepare the input offset ratios for scaler. */
		s_frame->dma_offset.y_h = s_frame->offs_h;
		if (!variant->pix_hoff)
			s_frame->dma_offset.y_h *= (s_frame->fmt->depth >> 3);
		s_frame->dma_offset.y_v = s_frame->offs_v;

		s_frame->dma_offset.cb_h = s_frame->offs_h;
		s_frame->dma_offset.cb_v = s_frame->offs_v;

		s_frame->dma_offset.cr_h = s_frame->offs_h;
		s_frame->dma_offset.cr_v = s_frame->offs_v;

		if (!variant->pix_hoff && s_frame->fmt->planes_cnt == 3) {
			s_frame->dma_offset.cb_h >>= 1;
			s_frame->dma_offset.cb_v >>= 1;
			s_frame->dma_offset.cr_h >>= 1;
			s_frame->dma_offset.cr_v >>= 1;
		}

		dbg("in offset: color= %d, y_h= %d, y_v= %d",
			s_frame->fmt->color, s_frame->dma_offset.y_h,
			s_frame->dma_offset.y_v);

		fimc_set_yuv_order(ctx);

		/* Check against the scaler ratio. */
		if (s_frame->height > (SCALER_MAX_VRATIO * d_frame->height) ||
		    s_frame->width > (SCALER_MAX_HRATIO * d_frame->width)) {
			err("out of scaler range");
			return -EINVAL;
		}
	}

	/* Input DMA mode is not allowed when the scaler is disabled. */
	ctx->scaler.enabled = 1;

	if (flags & FIMC_SRC_ADDR) {
		buf = v4l2_m2m_next_src_buf(ctx->m2m_ctx);
		ret = fimc_prepare_addr(ctx, buf,
			V4L2_BUF_TYPE_VIDEO_OUTPUT);
		if (ret)
			return ret;
	}

	if (flags & FIMC_DST_ADDR) {
		buf = v4l2_m2m_next_dst_buf(ctx->m2m_ctx);
		ret = fimc_prepare_addr(ctx, buf,
			V4L2_BUF_TYPE_VIDEO_CAPTURE);
	}

	return ret;
}

static void fimc_dma_run(void *priv)
{
	struct fimc_ctx *ctx = priv;
	struct fimc_dev *fimc;
	unsigned long flags;
	u32 ret;

	if (WARN(!ctx, "null hardware context"))
		return;

	fimc = ctx->fimc_dev;

	spin_lock_irqsave(&ctx->slock, flags);
	set_bit(ST_M2M_PEND, &fimc->state);

	ctx->state |= (FIMC_SRC_ADDR | FIMC_DST_ADDR);
	ret = fimc_prepare_config(ctx, ctx->state);
	if (ret) {
		err("general configuration error");
		goto dma_unlock;
	}

	if (fimc->m2m.ctx != ctx)
		ctx->state |= FIMC_PARAMS;

	fimc_hw_set_input_addr(fimc, &ctx->s_frame.paddr);

	if (ctx->state & FIMC_PARAMS) {
		fimc_hw_set_input_path(ctx);
		fimc_hw_set_in_dma(ctx);
		if (fimc_set_scaler_info(ctx)) {
			err("scaler configuration error");
			goto dma_unlock;
		}
		fimc_hw_set_prescaler(ctx);
		fimc_hw_set_scaler(ctx);
		fimc_hw_set_target_format(ctx);
		fimc_hw_set_rotation(ctx);
		fimc_hw_set_effect(ctx);
	}

	fimc_hw_set_output_path(ctx);
	if (ctx->state & (FIMC_DST_ADDR | FIMC_PARAMS))
		fimc_hw_set_output_addr(fimc, &ctx->d_frame.paddr);

	if (ctx->state & FIMC_PARAMS)
		fimc_hw_set_out_dma(ctx);

	if (ctx->scaler.enabled)
		fimc_hw_start_scaler(fimc);
	fimc_hw_en_capture(ctx);

	ctx->state = 0;
	fimc_hw_start_in_dma(fimc);

	fimc->m2m.ctx = ctx;

dma_unlock:
	spin_unlock_irqrestore(&ctx->slock, flags);
}

static void fimc_job_abort(void *priv)
{
	/* Nothing done in job_abort. */
}

static void fimc_buf_release(struct videobuf_queue *vq,
				    struct videobuf_buffer *vb)
{
	videobuf_dma_contig_free(vq, vb);
	vb->state = VIDEOBUF_NEEDS_INIT;
}

static int fimc_buf_setup(struct videobuf_queue *vq, unsigned int *count,
				unsigned int *size)
{
	struct fimc_ctx *ctx = vq->priv_data;
	struct fimc_frame *frame;

	frame = ctx_m2m_get_frame(ctx, vq->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	*size = (frame->width * frame->height * frame->fmt->depth) >> 3;
	if (0 == *count)
		*count = 1;
	return 0;
}

static int fimc_buf_prepare(struct videobuf_queue *vq,
		struct videobuf_buffer *vb, enum v4l2_field field)
{
	struct fimc_ctx *ctx = vq->priv_data;
	struct v4l2_device *v4l2_dev = &ctx->fimc_dev->m2m.v4l2_dev;
	struct fimc_frame *frame;
	int ret;

	frame = ctx_m2m_get_frame(ctx, vq->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	if (vb->baddr) {
		if (vb->bsize < frame->size) {
			v4l2_err(v4l2_dev,
				"User-provided buffer too small (%d < %d)\n",
				 vb->bsize, frame->size);
			WARN_ON(1);
			return -EINVAL;
		}
	} else if (vb->state != VIDEOBUF_NEEDS_INIT
		   && vb->bsize < frame->size) {
		return -EINVAL;
	}

	vb->width       = frame->width;
	vb->height      = frame->height;
	vb->bytesperline = (frame->width * frame->fmt->depth) >> 3;
	vb->size        = frame->size;
	vb->field       = field;

	if (VIDEOBUF_NEEDS_INIT == vb->state) {
		ret = videobuf_iolock(vq, vb, NULL);
		if (ret) {
			v4l2_err(v4l2_dev, "Iolock failed\n");
			fimc_buf_release(vq, vb);
			return ret;
		}
	}
	vb->state = VIDEOBUF_PREPARED;

	return 0;
}

static void fimc_buf_queue(struct videobuf_queue *vq,
				  struct videobuf_buffer *vb)
{
	struct fimc_ctx *ctx = vq->priv_data;
	v4l2_m2m_buf_queue(ctx->m2m_ctx, vq, vb);
}

static struct videobuf_queue_ops fimc_qops = {
	.buf_setup	= fimc_buf_setup,
	.buf_prepare	= fimc_buf_prepare,
	.buf_queue	= fimc_buf_queue,
	.buf_release	= fimc_buf_release,
};

static int fimc_m2m_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	struct fimc_ctx *ctx = file->private_data;
	struct fimc_dev *fimc = ctx->fimc_dev;

	strncpy(cap->driver, fimc->pdev->name, sizeof(cap->driver) - 1);
	strncpy(cap->card, fimc->pdev->name, sizeof(cap->card) - 1);
	cap->bus_info[0] = 0;
	cap->version = KERNEL_VERSION(1, 0, 0);
	cap->capabilities = V4L2_CAP_STREAMING |
		V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_OUTPUT;

	return 0;
}

static int fimc_m2m_enum_fmt(struct file *file, void *priv,
				struct v4l2_fmtdesc *f)
{
	struct fimc_fmt *fmt;

	if (f->index >= ARRAY_SIZE(fimc_formats))
		return -EINVAL;

	fmt = &fimc_formats[f->index];
	strncpy(f->description, fmt->name, sizeof(f->description) - 1);
	f->pixelformat = fmt->fourcc;
	return 0;
}

static int fimc_m2m_g_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct fimc_ctx *ctx = priv;
	struct fimc_frame *frame;

	frame = ctx_m2m_get_frame(ctx, f->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	f->fmt.pix.width	= frame->width;
	f->fmt.pix.height	= frame->height;
	f->fmt.pix.field	= V4L2_FIELD_NONE;
	f->fmt.pix.pixelformat	= frame->fmt->fourcc;

	return 0;
}

static struct fimc_fmt *find_format(struct v4l2_format *f)
{
	struct fimc_fmt *fmt;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(fimc_formats); ++i) {
		fmt = &fimc_formats[i];
		if (fmt->fourcc == f->fmt.pix.pixelformat)
			break;
	}
	if (i == ARRAY_SIZE(fimc_formats))
		return NULL;

	return fmt;
}

static int fimc_m2m_try_fmt(struct file *file, void *priv,
			     struct v4l2_format *f)
{
	struct fimc_fmt *fmt;
	u32 max_width, max_height, mod_x, mod_y;
	struct fimc_ctx *ctx = priv;
	struct fimc_dev *fimc = ctx->fimc_dev;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	struct samsung_fimc_variant *variant = fimc->variant;

	fmt = find_format(f);
	if (!fmt) {
		v4l2_err(&fimc->m2m.v4l2_dev,
			 "Fourcc format (0x%X) invalid.\n",  pix->pixelformat);
		return -EINVAL;
	}

	if (pix->field == V4L2_FIELD_ANY)
		pix->field = V4L2_FIELD_NONE;
	else if (V4L2_FIELD_NONE != pix->field)
		return -EINVAL;

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		max_width = variant->scaler_dis_w;
		max_height = variant->scaler_dis_w;
		mod_x = variant->min_inp_pixsize;
		mod_y = variant->min_inp_pixsize;
	} else if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		max_width = variant->out_rot_dis_w;
		max_height = variant->out_rot_dis_w;
		mod_x = variant->min_out_pixsize;
		mod_y = variant->min_out_pixsize;
	} else {
		err("Wrong stream type (%d)", f->type);
		return -EINVAL;
	}

	dbg("max_w= %d, max_h= %d", max_width, max_height);

	if (pix->height > max_height)
		pix->height = max_height;
	if (pix->width > max_width)
		pix->width = max_width;

	if (tiled_fmt(fmt)) {
		mod_x = 64; /* 64x32 tile */
		mod_y = 32;
	}

	dbg("mod_x= 0x%X, mod_y= 0x%X", mod_x, mod_y);

	pix->width = (pix->width == 0) ? mod_x : ALIGN(pix->width, mod_x);
	pix->height = (pix->height == 0) ? mod_y : ALIGN(pix->height, mod_y);

	if (pix->bytesperline == 0 ||
	    pix->bytesperline * 8 / fmt->depth > pix->width)
		pix->bytesperline = (pix->width * fmt->depth) >> 3;

	if (pix->sizeimage == 0)
		pix->sizeimage = pix->height * pix->bytesperline;

	dbg("pix->bytesperline= %d, fmt->depth= %d",
	    pix->bytesperline, fmt->depth);

	return 0;
}


static int fimc_m2m_s_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct fimc_ctx *ctx = priv;
	struct v4l2_device *v4l2_dev = &ctx->fimc_dev->m2m.v4l2_dev;
	struct videobuf_queue *src_vq, *dst_vq;
	struct fimc_frame *frame;
	struct v4l2_pix_format *pix;
	unsigned long flags;
	int ret = 0;

	BUG_ON(!ctx);

	ret = fimc_m2m_try_fmt(file, priv, f);
	if (ret)
		return ret;

	mutex_lock(&ctx->fimc_dev->lock);

	src_vq = v4l2_m2m_get_src_vq(ctx->m2m_ctx);
	dst_vq = v4l2_m2m_get_dst_vq(ctx->m2m_ctx);

	mutex_lock(&src_vq->vb_lock);
	mutex_lock(&dst_vq->vb_lock);

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		if (videobuf_queue_is_busy(src_vq)) {
			v4l2_err(v4l2_dev, "%s queue busy\n", __func__);
			ret = -EBUSY;
			goto s_fmt_out;
		}
		frame = &ctx->s_frame;
		spin_lock_irqsave(&ctx->slock, flags);
		ctx->state |= FIMC_SRC_FMT;
		spin_unlock_irqrestore(&ctx->slock, flags);

	} else if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		if (videobuf_queue_is_busy(dst_vq)) {
			v4l2_err(v4l2_dev, "%s queue busy\n", __func__);
			ret = -EBUSY;
			goto s_fmt_out;
		}
		frame = &ctx->d_frame;
		spin_lock_irqsave(&ctx->slock, flags);
		ctx->state |= FIMC_DST_FMT;
		spin_unlock_irqrestore(&ctx->slock, flags);
	} else {
		v4l2_err(&ctx->fimc_dev->m2m.v4l2_dev,
			 "Wrong buffer/video queue type (%d)\n", f->type);
		ret = -EINVAL;
		goto s_fmt_out;
	}

	pix = &f->fmt.pix;
	frame->fmt = find_format(f);
	if (!frame->fmt) {
		ret = -EINVAL;
		goto s_fmt_out;
	}

	frame->f_width = pix->bytesperline * 8 / frame->fmt->depth;
	frame->f_height = pix->sizeimage/pix->bytesperline;
	frame->width = pix->width;
	frame->height = pix->height;
	frame->o_width = pix->width;
	frame->o_height = pix->height;
	frame->offs_h = 0;
	frame->offs_v = 0;
	frame->size = (pix->width * pix->height * frame->fmt->depth) >> 3;
	src_vq->field = dst_vq->field = pix->field;
	spin_lock_irqsave(&ctx->slock, flags);
	ctx->state |= FIMC_PARAMS;
	spin_unlock_irqrestore(&ctx->slock, flags);

	dbg("f_width= %d, f_height= %d", frame->f_width, frame->f_height);

s_fmt_out:
	mutex_unlock(&dst_vq->vb_lock);
	mutex_unlock(&src_vq->vb_lock);
	mutex_unlock(&ctx->fimc_dev->lock);
	return ret;
}

static int fimc_m2m_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *reqbufs)
{
	struct fimc_ctx *ctx = priv;
	return v4l2_m2m_reqbufs(file, ctx->m2m_ctx, reqbufs);
}

static int fimc_m2m_querybuf(struct file *file, void *priv,
			   struct v4l2_buffer *buf)
{
	struct fimc_ctx *ctx = priv;
	return v4l2_m2m_querybuf(file, ctx->m2m_ctx, buf);
}

static int fimc_m2m_qbuf(struct file *file, void *priv,
			  struct v4l2_buffer *buf)
{
	struct fimc_ctx *ctx = priv;

	return v4l2_m2m_qbuf(file, ctx->m2m_ctx, buf);
}

static int fimc_m2m_dqbuf(struct file *file, void *priv,
			   struct v4l2_buffer *buf)
{
	struct fimc_ctx *ctx = priv;
	return v4l2_m2m_dqbuf(file, ctx->m2m_ctx, buf);
}

static int fimc_m2m_streamon(struct file *file, void *priv,
			   enum v4l2_buf_type type)
{
	struct fimc_ctx *ctx = priv;
	return v4l2_m2m_streamon(file, ctx->m2m_ctx, type);
}

static int fimc_m2m_streamoff(struct file *file, void *priv,
			    enum v4l2_buf_type type)
{
	struct fimc_ctx *ctx = priv;
	return v4l2_m2m_streamoff(file, ctx->m2m_ctx, type);
}

int fimc_m2m_queryctrl(struct file *file, void *priv,
			    struct v4l2_queryctrl *qc)
{
	struct v4l2_queryctrl *c;
	c = get_ctrl(qc->id);
	if (!c)
		return -EINVAL;
	*qc = *c;
	return 0;
}

int fimc_m2m_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct fimc_ctx *ctx = priv;

	switch (ctrl->id) {
	case V4L2_CID_HFLIP:
		ctrl->value = (FLIP_X_AXIS & ctx->flip) ? 1 : 0;
		break;
	case V4L2_CID_VFLIP:
		ctrl->value = (FLIP_Y_AXIS & ctx->flip) ? 1 : 0;
		break;
	case V4L2_CID_ROTATE:
		ctrl->value = ctx->rotation;
		break;
	default:
		v4l2_err(&ctx->fimc_dev->m2m.v4l2_dev, "Invalid control\n");
		return -EINVAL;
	}
	dbg("ctrl->value= %d", ctrl->value);
	return 0;
}

static int check_ctrl_val(struct fimc_ctx *ctx,
			  struct v4l2_control *ctrl)
{
	struct v4l2_queryctrl *c;
	c = get_ctrl(ctrl->id);
	if (!c)
		return -EINVAL;

	if (ctrl->value < c->minimum || ctrl->value > c->maximum
		|| (c->step != 0 && ctrl->value % c->step != 0)) {
		v4l2_err(&ctx->fimc_dev->m2m.v4l2_dev,
		"Invalid control value\n");
		return -ERANGE;
	}

	return 0;
}

int fimc_m2m_s_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
	struct fimc_ctx *ctx = priv;
	struct samsung_fimc_variant *variant = ctx->fimc_dev->variant;
	unsigned long flags;
	int ret = 0;

	ret = check_ctrl_val(ctx, ctrl);
	if (ret)
		return ret;

	switch (ctrl->id) {
	case V4L2_CID_HFLIP:
		if (ctx->rotation != 0)
			return 0;
		if (ctrl->value)
			ctx->flip |= FLIP_X_AXIS;
		else
			ctx->flip &= ~FLIP_X_AXIS;
		break;

	case V4L2_CID_VFLIP:
		if (ctx->rotation != 0)
			return 0;
		if (ctrl->value)
			ctx->flip |= FLIP_Y_AXIS;
		else
			ctx->flip &= ~FLIP_Y_AXIS;
		break;

	case V4L2_CID_ROTATE:
		if (ctrl->value == 90 || ctrl->value == 270) {
			if (ctx->out_path == FIMC_LCDFIFO &&
			    !variant->has_inp_rot) {
				return -EINVAL;
			} else if (ctx->in_path == FIMC_DMA &&
				   !variant->has_out_rot) {
				return -EINVAL;
			}
		}
		ctx->rotation = ctrl->value;
		if (ctrl->value == 180)
			ctx->flip = FLIP_XY_AXIS;
		break;

	default:
		v4l2_err(&ctx->fimc_dev->m2m.v4l2_dev, "Invalid control\n");
		return -EINVAL;
	}
	spin_lock_irqsave(&ctx->slock, flags);
	ctx->state |= FIMC_PARAMS;
	spin_unlock_irqrestore(&ctx->slock, flags);
	return 0;
}


static int fimc_m2m_cropcap(struct file *file, void *fh,
			     struct v4l2_cropcap *cr)
{
	struct fimc_frame *frame;
	struct fimc_ctx *ctx = fh;

	frame = ctx_m2m_get_frame(ctx, cr->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	cr->bounds.left = 0;
	cr->bounds.top = 0;
	cr->bounds.width = frame->f_width;
	cr->bounds.height = frame->f_height;
	cr->defrect.left = frame->offs_h;
	cr->defrect.top = frame->offs_v;
	cr->defrect.width = frame->o_width;
	cr->defrect.height = frame->o_height;
	return 0;
}

static int fimc_m2m_g_crop(struct file *file, void *fh, struct v4l2_crop *cr)
{
	struct fimc_frame *frame;
	struct fimc_ctx *ctx = file->private_data;

	frame = ctx_m2m_get_frame(ctx, cr->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	cr->c.left = frame->offs_h;
	cr->c.top = frame->offs_v;
	cr->c.width = frame->width;
	cr->c.height = frame->height;

	return 0;
}

static int fimc_m2m_s_crop(struct file *file, void *fh, struct v4l2_crop *cr)
{
	struct fimc_ctx *ctx = file->private_data;
	struct fimc_dev *fimc = ctx->fimc_dev;
	unsigned long flags;
	struct fimc_frame *f;
	u32 min_size;
	int ret = 0;

	if (cr->c.top < 0 || cr->c.left < 0) {
		v4l2_err(&fimc->m2m.v4l2_dev,
			"doesn't support negative values for top & left\n");
		return -EINVAL;
	}

	if (cr->c.width  <= 0 || cr->c.height <= 0) {
		v4l2_err(&fimc->m2m.v4l2_dev,
			"crop width and height must be greater than 0\n");
		return -EINVAL;
	}

	f = ctx_m2m_get_frame(ctx, cr->type);
	if (IS_ERR(f))
		return PTR_ERR(f);

	/* Adjust to required pixel boundary. */
	min_size = (cr->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) ?
		fimc->variant->min_inp_pixsize : fimc->variant->min_out_pixsize;

	cr->c.width = round_down(cr->c.width, min_size);
	cr->c.height = round_down(cr->c.height, min_size);
	cr->c.left = round_down(cr->c.left + 1, min_size);
	cr->c.top = round_down(cr->c.top + 1, min_size);

	if ((cr->c.left + cr->c.width > f->o_width)
		|| (cr->c.top + cr->c.height > f->o_height)) {
		v4l2_err(&fimc->m2m.v4l2_dev, "Error in S_CROP params\n");
		return -EINVAL;
	}

	spin_lock_irqsave(&ctx->slock, flags);
	if ((ctx->state & FIMC_SRC_FMT) && (ctx->state & FIMC_DST_FMT)) {
		/* Check for the pixel scaling ratio when cropping input img. */
		if (cr->type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
			ret = fimc_check_scaler_ratio(&cr->c, &ctx->d_frame);
		else if (cr->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
			ret = fimc_check_scaler_ratio(&cr->c, &ctx->s_frame);

		if (ret) {
			spin_unlock_irqrestore(&ctx->slock, flags);
			v4l2_err(&fimc->m2m.v4l2_dev,  "Out of scaler range");
			return -EINVAL;
		}
	}
	ctx->state |= FIMC_PARAMS;
	spin_unlock_irqrestore(&ctx->slock, flags);

	f->offs_h = cr->c.left;
	f->offs_v = cr->c.top;
	f->width = cr->c.width;
	f->height = cr->c.height;
	return 0;
}

static const struct v4l2_ioctl_ops fimc_m2m_ioctl_ops = {
	.vidioc_querycap		= fimc_m2m_querycap,

	.vidioc_enum_fmt_vid_cap	= fimc_m2m_enum_fmt,
	.vidioc_enum_fmt_vid_out	= fimc_m2m_enum_fmt,

	.vidioc_g_fmt_vid_cap		= fimc_m2m_g_fmt,
	.vidioc_g_fmt_vid_out		= fimc_m2m_g_fmt,

	.vidioc_try_fmt_vid_cap		= fimc_m2m_try_fmt,
	.vidioc_try_fmt_vid_out		= fimc_m2m_try_fmt,

	.vidioc_s_fmt_vid_cap		= fimc_m2m_s_fmt,
	.vidioc_s_fmt_vid_out		= fimc_m2m_s_fmt,

	.vidioc_reqbufs			= fimc_m2m_reqbufs,
	.vidioc_querybuf		= fimc_m2m_querybuf,

	.vidioc_qbuf			= fimc_m2m_qbuf,
	.vidioc_dqbuf			= fimc_m2m_dqbuf,

	.vidioc_streamon		= fimc_m2m_streamon,
	.vidioc_streamoff		= fimc_m2m_streamoff,

	.vidioc_queryctrl		= fimc_m2m_queryctrl,
	.vidioc_g_ctrl			= fimc_m2m_g_ctrl,
	.vidioc_s_ctrl			= fimc_m2m_s_ctrl,

	.vidioc_g_crop			= fimc_m2m_g_crop,
	.vidioc_s_crop			= fimc_m2m_s_crop,
	.vidioc_cropcap			= fimc_m2m_cropcap

};

static void queue_init(void *priv, struct videobuf_queue *vq,
		       enum v4l2_buf_type type)
{
	struct fimc_ctx *ctx = priv;
	struct fimc_dev *fimc = ctx->fimc_dev;

	videobuf_queue_dma_contig_init(vq, &fimc_qops,
		fimc->m2m.v4l2_dev.dev,
		&fimc->irqlock, type, V4L2_FIELD_NONE,
		sizeof(struct fimc_vid_buffer), priv);
}

static int fimc_m2m_open(struct file *file)
{
	struct fimc_dev *fimc = video_drvdata(file);
	struct fimc_ctx *ctx = NULL;
	int err = 0;

	mutex_lock(&fimc->lock);
	fimc->m2m.refcnt++;
	set_bit(ST_OUTDMA_RUN, &fimc->state);
	mutex_unlock(&fimc->lock);


	ctx = kzalloc(sizeof *ctx, GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	file->private_data = ctx;
	ctx->fimc_dev = fimc;
	/* default format */
	ctx->s_frame.fmt = &fimc_formats[0];
	ctx->d_frame.fmt = &fimc_formats[0];
	/* per user process device context initialization */
	ctx->state = 0;
	ctx->flags = 0;
	ctx->effect.type = S5P_FIMC_EFFECT_ORIGINAL;
	ctx->in_path = FIMC_DMA;
	ctx->out_path = FIMC_DMA;
	spin_lock_init(&ctx->slock);

	ctx->m2m_ctx = v4l2_m2m_ctx_init(ctx, fimc->m2m.m2m_dev, queue_init);
	if (IS_ERR(ctx->m2m_ctx)) {
		err = PTR_ERR(ctx->m2m_ctx);
		kfree(ctx);
	}
	return err;
}

static int fimc_m2m_release(struct file *file)
{
	struct fimc_ctx *ctx = file->private_data;
	struct fimc_dev *fimc = ctx->fimc_dev;

	v4l2_m2m_ctx_release(ctx->m2m_ctx);
	kfree(ctx);
	mutex_lock(&fimc->lock);
	if (--fimc->m2m.refcnt <= 0)
		clear_bit(ST_OUTDMA_RUN, &fimc->state);
	mutex_unlock(&fimc->lock);
	return 0;
}

static unsigned int fimc_m2m_poll(struct file *file,
				     struct poll_table_struct *wait)
{
	struct fimc_ctx *ctx = file->private_data;
	return v4l2_m2m_poll(file, ctx->m2m_ctx, wait);
}


static int fimc_m2m_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct fimc_ctx *ctx = file->private_data;
	return v4l2_m2m_mmap(file, ctx->m2m_ctx, vma);
}

static const struct v4l2_file_operations fimc_m2m_fops = {
	.owner		= THIS_MODULE,
	.open		= fimc_m2m_open,
	.release	= fimc_m2m_release,
	.poll		= fimc_m2m_poll,
	.ioctl		= video_ioctl2,
	.mmap		= fimc_m2m_mmap,
};

static struct v4l2_m2m_ops m2m_ops = {
	.device_run	= fimc_dma_run,
	.job_abort	= fimc_job_abort,
};


static int fimc_register_m2m_device(struct fimc_dev *fimc)
{
	struct video_device *vfd;
	struct platform_device *pdev;
	struct v4l2_device *v4l2_dev;
	int ret = 0;

	if (!fimc)
		return -ENODEV;

	pdev = fimc->pdev;
	v4l2_dev = &fimc->m2m.v4l2_dev;

	/* set name if it is empty */
	if (!v4l2_dev->name[0])
		snprintf(v4l2_dev->name, sizeof(v4l2_dev->name),
			 "%s.m2m", dev_name(&pdev->dev));

	ret = v4l2_device_register(&pdev->dev, v4l2_dev);
	if (ret)
		return ret;;

	vfd = video_device_alloc();
	if (!vfd) {
		v4l2_err(v4l2_dev, "Failed to allocate video device\n");
		goto err_m2m_r1;
	}

	vfd->fops	= &fimc_m2m_fops;
	vfd->ioctl_ops	= &fimc_m2m_ioctl_ops;
	vfd->minor	= -1;
	vfd->release	= video_device_release;

	snprintf(vfd->name, sizeof(vfd->name), "%s:m2m", dev_name(&pdev->dev));

	video_set_drvdata(vfd, fimc);
	platform_set_drvdata(pdev, fimc);

	fimc->m2m.vfd = vfd;
	fimc->m2m.m2m_dev = v4l2_m2m_init(&m2m_ops);
	if (IS_ERR(fimc->m2m.m2m_dev)) {
		v4l2_err(v4l2_dev, "failed to initialize v4l2-m2m device\n");
		ret = PTR_ERR(fimc->m2m.m2m_dev);
		goto err_m2m_r2;
	}

	ret = video_register_device(vfd, VFL_TYPE_GRABBER, -1);
	if (ret) {
		v4l2_err(v4l2_dev,
			 "%s(): failed to register video device\n", __func__);
		goto err_m2m_r3;
	}
	v4l2_info(v4l2_dev,
		  "FIMC m2m driver registered as /dev/video%d\n", vfd->num);

	return 0;

err_m2m_r3:
	v4l2_m2m_release(fimc->m2m.m2m_dev);
err_m2m_r2:
	video_device_release(fimc->m2m.vfd);
err_m2m_r1:
	v4l2_device_unregister(v4l2_dev);

	return ret;
}

static void fimc_unregister_m2m_device(struct fimc_dev *fimc)
{
	if (fimc) {
		v4l2_m2m_release(fimc->m2m.m2m_dev);
		video_unregister_device(fimc->m2m.vfd);
		video_device_release(fimc->m2m.vfd);
		v4l2_device_unregister(&fimc->m2m.v4l2_dev);
	}
}

static void fimc_clk_release(struct fimc_dev *fimc)
{
	int i;
	for (i = 0; i < NUM_FIMC_CLOCKS; i++) {
		if (fimc->clock[i]) {
			clk_disable(fimc->clock[i]);
			clk_put(fimc->clock[i]);
		}
	}
}

static int fimc_clk_get(struct fimc_dev *fimc)
{
	int i;
	for (i = 0; i < NUM_FIMC_CLOCKS; i++) {
		fimc->clock[i] = clk_get(&fimc->pdev->dev, fimc_clock_name[i]);
		if (IS_ERR(fimc->clock[i])) {
			dev_err(&fimc->pdev->dev,
				"failed to get fimc clock: %s\n",
				fimc_clock_name[i]);
			return -ENXIO;
		}
		clk_enable(fimc->clock[i]);
	}
	return 0;
}

static int fimc_probe(struct platform_device *pdev)
{
	struct fimc_dev *fimc;
	struct resource *res;
	struct samsung_fimc_driverdata *drv_data;
	int ret = 0;

	dev_dbg(&pdev->dev, "%s():\n", __func__);

	drv_data = (struct samsung_fimc_driverdata *)
		platform_get_device_id(pdev)->driver_data;

	if (pdev->id >= drv_data->devs_cnt) {
		dev_err(&pdev->dev, "Invalid platform device id: %d\n",
			pdev->id);
		return -EINVAL;
	}

	fimc = kzalloc(sizeof(struct fimc_dev), GFP_KERNEL);
	if (!fimc)
		return -ENOMEM;

	fimc->id = pdev->id;
	fimc->variant = drv_data->variant[fimc->id];
	fimc->pdev = pdev;
	fimc->state = ST_IDLE;

	spin_lock_init(&fimc->irqlock);
	spin_lock_init(&fimc->slock);

	mutex_init(&fimc->lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to find the registers\n");
		ret = -ENOENT;
		goto err_info;
	}

	fimc->regs_res = request_mem_region(res->start, resource_size(res),
			dev_name(&pdev->dev));
	if (!fimc->regs_res) {
		dev_err(&pdev->dev, "failed to obtain register region\n");
		ret = -ENOENT;
		goto err_info;
	}

	fimc->regs = ioremap(res->start, resource_size(res));
	if (!fimc->regs) {
		dev_err(&pdev->dev, "failed to map registers\n");
		ret = -ENXIO;
		goto err_req_region;
	}

	ret = fimc_clk_get(fimc);
	if (ret)
		goto err_regs_unmap;

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to get IRQ resource\n");
		ret = -ENXIO;
		goto err_clk;
	}
	fimc->irq = res->start;

	fimc_hw_reset(fimc);

	ret = request_irq(fimc->irq, fimc_isr, 0, pdev->name, fimc);
	if (ret) {
		dev_err(&pdev->dev, "failed to install irq (%d)\n", ret);
		goto err_clk;
	}

	fimc->work_queue = create_workqueue(dev_name(&fimc->pdev->dev));
	if (!fimc->work_queue) {
		ret = -ENOMEM;
		goto err_irq;
	}

	ret = fimc_register_m2m_device(fimc);
	if (ret)
		goto err_wq;

	fimc_hw_en_lastirq(fimc, true);

	dev_dbg(&pdev->dev, "%s(): fimc-%d registered successfully\n",
		__func__, fimc->id);

	return 0;

err_wq:
	destroy_workqueue(fimc->work_queue);
err_irq:
	free_irq(fimc->irq, fimc);
err_clk:
	fimc_clk_release(fimc);
err_regs_unmap:
	iounmap(fimc->regs);
err_req_region:
	release_resource(fimc->regs_res);
	kfree(fimc->regs_res);
err_info:
	kfree(fimc);
	dev_err(&pdev->dev, "failed to install\n");
	return ret;
}

static int __devexit fimc_remove(struct platform_device *pdev)
{
	struct fimc_dev *fimc =
		(struct fimc_dev *)platform_get_drvdata(pdev);

	v4l2_info(&fimc->m2m.v4l2_dev, "Removing %s\n", pdev->name);

	free_irq(fimc->irq, fimc);

	fimc_hw_reset(fimc);

	fimc_unregister_m2m_device(fimc);
	fimc_clk_release(fimc);
	iounmap(fimc->regs);
	release_resource(fimc->regs_res);
	kfree(fimc->regs_res);
	kfree(fimc);
	return 0;
}

static struct samsung_fimc_variant fimc01_variant_s5p = {
	.has_inp_rot	= 1,
	.has_out_rot	= 1,
	.min_inp_pixsize = 16,
	.min_out_pixsize = 16,

	.scaler_en_w	= 3264,
	.scaler_dis_w	= 8192,
	.in_rot_en_h	= 1920,
	.in_rot_dis_w	= 8192,
	.out_rot_en_w	= 1920,
	.out_rot_dis_w	= 4224,
};

static struct samsung_fimc_variant fimc2_variant_s5p = {
	.min_inp_pixsize = 16,
	.min_out_pixsize = 16,

	.scaler_en_w	= 4224,
	.scaler_dis_w	= 8192,
	.in_rot_en_h	= 1920,
	.in_rot_dis_w	= 8192,
	.out_rot_en_w	= 1920,
	.out_rot_dis_w	= 4224,
};

static struct samsung_fimc_variant fimc01_variant_s5pv210 = {
	.has_inp_rot	= 1,
	.has_out_rot	= 1,
	.min_inp_pixsize = 16,
	.min_out_pixsize = 32,

	.scaler_en_w	= 4224,
	.scaler_dis_w	= 8192,
	.in_rot_en_h	= 1920,
	.in_rot_dis_w	= 8192,
	.out_rot_en_w	= 1920,
	.out_rot_dis_w	= 4224,
};

static struct samsung_fimc_variant fimc2_variant_s5pv210 = {
	.min_inp_pixsize = 16,
	.min_out_pixsize = 32,

	.scaler_en_w	= 1920,
	.scaler_dis_w	= 8192,
	.in_rot_en_h	= 1280,
	.in_rot_dis_w	= 8192,
	.out_rot_en_w	= 1280,
	.out_rot_dis_w	= 1920,
};

static struct samsung_fimc_driverdata fimc_drvdata_s5p = {
	.variant = {
		[0] = &fimc01_variant_s5p,
		[1] = &fimc01_variant_s5p,
		[2] = &fimc2_variant_s5p,
	},
	.devs_cnt = 3
};

static struct samsung_fimc_driverdata fimc_drvdata_s5pv210 = {
	.variant = {
		[0] = &fimc01_variant_s5pv210,
		[1] = &fimc01_variant_s5pv210,
		[2] = &fimc2_variant_s5pv210,
	},
	.devs_cnt = 3
};

static struct platform_device_id fimc_driver_ids[] = {
	{
		.name		= "s5p-fimc",
		.driver_data	= (unsigned long)&fimc_drvdata_s5p,
	}, {
		.name		= "s5pv210-fimc",
		.driver_data	= (unsigned long)&fimc_drvdata_s5pv210,
	},
	{},
};
MODULE_DEVICE_TABLE(platform, fimc_driver_ids);

static struct platform_driver fimc_driver = {
	.probe		= fimc_probe,
	.remove	= __devexit_p(fimc_remove),
	.id_table	= fimc_driver_ids,
	.driver = {
		.name	= MODULE_NAME,
		.owner	= THIS_MODULE,
	}
};

static char banner[] __initdata = KERN_INFO
	"S5PC Camera Interface V4L2 Driver, (c) 2010 Samsung Electronics\n";

static int __init fimc_init(void)
{
	u32 ret;
	printk(banner);

	ret = platform_driver_register(&fimc_driver);
	if (ret) {
		printk(KERN_ERR "FIMC platform driver register failed\n");
		return -1;
	}
	return 0;
}

static void __exit fimc_exit(void)
{
	platform_driver_unregister(&fimc_driver);
}

module_init(fimc_init);
module_exit(fimc_exit);

MODULE_AUTHOR("Sylwester Nawrocki, s.nawrocki@samsung.com");
MODULE_DESCRIPTION("S3C/S5P FIMC (video postprocessor) driver");
MODULE_LICENSE("GPL");
