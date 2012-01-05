/*
 * Samsung S5P/EXYNOS4 SoC series camera interface (video postprocessor) driver
 *
 * Copyright (C) 2010-2011 Samsung Electronics Co., Ltd.
 * Contact: Sylwester Nawrocki, <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 2 of the License,
 * or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/bug.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/list.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>

#include "fimc-core.h"
#include "fimc-mdevice.h"

static char *fimc_clocks[MAX_FIMC_CLOCKS] = {
	"sclk_fimc", "fimc"
};

static struct fimc_fmt fimc_formats[] = {
	{
		.name		= "RGB565",
		.fourcc		= V4L2_PIX_FMT_RGB565X,
		.depth		= { 16 },
		.color		= S5P_FIMC_RGB565,
		.memplanes	= 1,
		.colplanes	= 1,
		.flags		= FMT_FLAGS_M2M,
	}, {
		.name		= "BGR666",
		.fourcc		= V4L2_PIX_FMT_BGR666,
		.depth		= { 32 },
		.color		= S5P_FIMC_RGB666,
		.memplanes	= 1,
		.colplanes	= 1,
		.flags		= FMT_FLAGS_M2M,
	}, {
		.name		= "XRGB-8-8-8-8, 32 bpp",
		.fourcc		= V4L2_PIX_FMT_RGB32,
		.depth		= { 32 },
		.color		= S5P_FIMC_RGB888,
		.memplanes	= 1,
		.colplanes	= 1,
		.flags		= FMT_FLAGS_M2M,
	}, {
		.name		= "YUV 4:2:2 packed, YCbYCr",
		.fourcc		= V4L2_PIX_FMT_YUYV,
		.depth		= { 16 },
		.color		= S5P_FIMC_YCBYCR422,
		.memplanes	= 1,
		.colplanes	= 1,
		.mbus_code	= V4L2_MBUS_FMT_YUYV8_2X8,
		.flags		= FMT_FLAGS_M2M | FMT_FLAGS_CAM,
	}, {
		.name		= "YUV 4:2:2 packed, CbYCrY",
		.fourcc		= V4L2_PIX_FMT_UYVY,
		.depth		= { 16 },
		.color		= S5P_FIMC_CBYCRY422,
		.memplanes	= 1,
		.colplanes	= 1,
		.mbus_code	= V4L2_MBUS_FMT_UYVY8_2X8,
		.flags		= FMT_FLAGS_M2M | FMT_FLAGS_CAM,
	}, {
		.name		= "YUV 4:2:2 packed, CrYCbY",
		.fourcc		= V4L2_PIX_FMT_VYUY,
		.depth		= { 16 },
		.color		= S5P_FIMC_CRYCBY422,
		.memplanes	= 1,
		.colplanes	= 1,
		.mbus_code	= V4L2_MBUS_FMT_VYUY8_2X8,
		.flags		= FMT_FLAGS_M2M | FMT_FLAGS_CAM,
	}, {
		.name		= "YUV 4:2:2 packed, YCrYCb",
		.fourcc		= V4L2_PIX_FMT_YVYU,
		.depth		= { 16 },
		.color		= S5P_FIMC_YCRYCB422,
		.memplanes	= 1,
		.colplanes	= 1,
		.mbus_code	= V4L2_MBUS_FMT_YVYU8_2X8,
		.flags		= FMT_FLAGS_M2M | FMT_FLAGS_CAM,
	}, {
		.name		= "YUV 4:2:2 planar, Y/Cb/Cr",
		.fourcc		= V4L2_PIX_FMT_YUV422P,
		.depth		= { 12 },
		.color		= S5P_FIMC_YCBYCR422,
		.memplanes	= 1,
		.colplanes	= 3,
		.flags		= FMT_FLAGS_M2M,
	}, {
		.name		= "YUV 4:2:2 planar, Y/CbCr",
		.fourcc		= V4L2_PIX_FMT_NV16,
		.depth		= { 16 },
		.color		= S5P_FIMC_YCBYCR422,
		.memplanes	= 1,
		.colplanes	= 2,
		.flags		= FMT_FLAGS_M2M,
	}, {
		.name		= "YUV 4:2:2 planar, Y/CrCb",
		.fourcc		= V4L2_PIX_FMT_NV61,
		.depth		= { 16 },
		.color		= S5P_FIMC_YCRYCB422,
		.memplanes	= 1,
		.colplanes	= 2,
		.flags		= FMT_FLAGS_M2M,
	}, {
		.name		= "YUV 4:2:0 planar, YCbCr",
		.fourcc		= V4L2_PIX_FMT_YUV420,
		.depth		= { 12 },
		.color		= S5P_FIMC_YCBCR420,
		.memplanes	= 1,
		.colplanes	= 3,
		.flags		= FMT_FLAGS_M2M,
	}, {
		.name		= "YUV 4:2:0 planar, Y/CbCr",
		.fourcc		= V4L2_PIX_FMT_NV12,
		.depth		= { 12 },
		.color		= S5P_FIMC_YCBCR420,
		.memplanes	= 1,
		.colplanes	= 2,
		.flags		= FMT_FLAGS_M2M,
	}, {
		.name		= "YUV 4:2:0 non-contiguous 2-planar, Y/CbCr",
		.fourcc		= V4L2_PIX_FMT_NV12M,
		.color		= S5P_FIMC_YCBCR420,
		.depth		= { 8, 4 },
		.memplanes	= 2,
		.colplanes	= 2,
		.flags		= FMT_FLAGS_M2M,
	}, {
		.name		= "YUV 4:2:0 non-contiguous 3-planar, Y/Cb/Cr",
		.fourcc		= V4L2_PIX_FMT_YUV420M,
		.color		= S5P_FIMC_YCBCR420,
		.depth		= { 8, 2, 2 },
		.memplanes	= 3,
		.colplanes	= 3,
		.flags		= FMT_FLAGS_M2M,
	}, {
		.name		= "YUV 4:2:0 non-contiguous 2-planar, Y/CbCr, tiled",
		.fourcc		= V4L2_PIX_FMT_NV12MT,
		.color		= S5P_FIMC_YCBCR420,
		.depth		= { 8, 4 },
		.memplanes	= 2,
		.colplanes	= 2,
		.flags		= FMT_FLAGS_M2M,
	}, {
		.name		= "JPEG encoded data",
		.fourcc		= V4L2_PIX_FMT_JPEG,
		.color		= S5P_FIMC_JPEG,
		.depth		= { 8 },
		.memplanes	= 1,
		.colplanes	= 1,
		.mbus_code	= V4L2_MBUS_FMT_JPEG_1X8,
		.flags		= FMT_FLAGS_CAM,
	},
};

int fimc_check_scaler_ratio(struct fimc_ctx *ctx, int sw, int sh,
			    int dw, int dh, int rotation)
{
	if (rotation == 90 || rotation == 270)
		swap(dw, dh);

	if (!ctx->scaler.enabled)
		return (sw == dw && sh == dh) ? 0 : -EINVAL;

	if ((sw >= SCALER_MAX_HRATIO * dw) || (sh >= SCALER_MAX_VRATIO * dh))
		return -EINVAL;

	return 0;
}

static int fimc_get_scaler_factor(u32 src, u32 tar, u32 *ratio, u32 *shift)
{
	u32 sh = 6;

	if (src >= 64 * tar)
		return -EINVAL;

	while (sh--) {
		u32 tmp = 1 << sh;
		if (src >= tar * tmp) {
			*shift = sh, *ratio = tmp;
			return 0;
		}
	}
	*shift = 0, *ratio = 1;
	return 0;
}

int fimc_set_scaler_info(struct fimc_ctx *ctx)
{
	struct samsung_fimc_variant *variant = ctx->fimc_dev->variant;
	struct device *dev = &ctx->fimc_dev->pdev->dev;
	struct fimc_scaler *sc = &ctx->scaler;
	struct fimc_frame *s_frame = &ctx->s_frame;
	struct fimc_frame *d_frame = &ctx->d_frame;
	int tx, ty, sx, sy;
	int ret;

	if (ctx->rotation == 90 || ctx->rotation == 270) {
		ty = d_frame->width;
		tx = d_frame->height;
	} else {
		tx = d_frame->width;
		ty = d_frame->height;
	}
	if (tx <= 0 || ty <= 0) {
		dev_err(dev, "Invalid target size: %dx%d", tx, ty);
		return -EINVAL;
	}

	sx = s_frame->width;
	sy = s_frame->height;
	if (sx <= 0 || sy <= 0) {
		dev_err(dev, "Invalid source size: %dx%d", sx, sy);
		return -EINVAL;
	}
	sc->real_width = sx;
	sc->real_height = sy;

	ret = fimc_get_scaler_factor(sx, tx, &sc->pre_hratio, &sc->hfactor);
	if (ret)
		return ret;

	ret = fimc_get_scaler_factor(sy, ty,  &sc->pre_vratio, &sc->vfactor);
	if (ret)
		return ret;

	sc->pre_dst_width = sx / sc->pre_hratio;
	sc->pre_dst_height = sy / sc->pre_vratio;

	if (variant->has_mainscaler_ext) {
		sc->main_hratio = (sx << 14) / (tx << sc->hfactor);
		sc->main_vratio = (sy << 14) / (ty << sc->vfactor);
	} else {
		sc->main_hratio = (sx << 8) / (tx << sc->hfactor);
		sc->main_vratio = (sy << 8) / (ty << sc->vfactor);

	}

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

static void fimc_m2m_job_finish(struct fimc_ctx *ctx, int vb_state)
{
	struct vb2_buffer *src_vb, *dst_vb;

	if (!ctx || !ctx->m2m_ctx)
		return;

	src_vb = v4l2_m2m_src_buf_remove(ctx->m2m_ctx);
	dst_vb = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx);

	if (src_vb && dst_vb) {
		v4l2_m2m_buf_done(src_vb, vb_state);
		v4l2_m2m_buf_done(dst_vb, vb_state);
		v4l2_m2m_job_finish(ctx->fimc_dev->m2m.m2m_dev,
				    ctx->m2m_ctx);
	}
}

/* Complete the transaction which has been scheduled for execution. */
static int fimc_m2m_shutdown(struct fimc_ctx *ctx)
{
	struct fimc_dev *fimc = ctx->fimc_dev;
	int ret;

	if (!fimc_m2m_pending(fimc))
		return 0;

	fimc_ctx_state_lock_set(FIMC_CTX_SHUT, ctx);

	ret = wait_event_timeout(fimc->irq_queue,
			   !fimc_ctx_state_is_set(FIMC_CTX_SHUT, ctx),
			   FIMC_SHUTDOWN_TIMEOUT);

	return ret == 0 ? -ETIMEDOUT : ret;
}

static int start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct fimc_ctx *ctx = q->drv_priv;
	int ret;

	ret = pm_runtime_get_sync(&ctx->fimc_dev->pdev->dev);
	return ret > 0 ? 0 : ret;
}

static int stop_streaming(struct vb2_queue *q)
{
	struct fimc_ctx *ctx = q->drv_priv;
	int ret;

	ret = fimc_m2m_shutdown(ctx);
	if (ret == -ETIMEDOUT)
		fimc_m2m_job_finish(ctx, VB2_BUF_STATE_ERROR);

	pm_runtime_put(&ctx->fimc_dev->pdev->dev);
	return 0;
}

void fimc_capture_irq_handler(struct fimc_dev *fimc, bool final)
{
	struct fimc_vid_cap *cap = &fimc->vid_cap;
	struct fimc_vid_buffer *v_buf;
	struct timeval *tv;
	struct timespec ts;

	if (test_and_clear_bit(ST_CAPT_SHUT, &fimc->state)) {
		wake_up(&fimc->irq_queue);
		return;
	}

	if (!list_empty(&cap->active_buf_q) &&
	    test_bit(ST_CAPT_RUN, &fimc->state) && final) {
		ktime_get_real_ts(&ts);

		v_buf = fimc_active_queue_pop(cap);

		tv = &v_buf->vb.v4l2_buf.timestamp;
		tv->tv_sec = ts.tv_sec;
		tv->tv_usec = ts.tv_nsec / NSEC_PER_USEC;
		v_buf->vb.v4l2_buf.sequence = cap->frame_count++;

		vb2_buffer_done(&v_buf->vb, VB2_BUF_STATE_DONE);
	}

	if (!list_empty(&cap->pending_buf_q)) {

		v_buf = fimc_pending_queue_pop(cap);
		fimc_hw_set_output_addr(fimc, &v_buf->paddr, cap->buf_index);
		v_buf->index = cap->buf_index;

		/* Move the buffer to the capture active queue */
		fimc_active_queue_add(cap, v_buf);

		dbg("next frame: %d, done frame: %d",
		    fimc_hw_get_frame_index(fimc), v_buf->index);

		if (++cap->buf_index >= FIMC_MAX_OUT_BUFS)
			cap->buf_index = 0;
	}

	if (cap->active_buf_cnt == 0) {
		if (final)
			clear_bit(ST_CAPT_RUN, &fimc->state);

		if (++cap->buf_index >= FIMC_MAX_OUT_BUFS)
			cap->buf_index = 0;
	} else {
		set_bit(ST_CAPT_RUN, &fimc->state);
	}

	fimc_capture_config_update(cap->ctx);

	dbg("frame: %d, active_buf_cnt: %d",
	    fimc_hw_get_frame_index(fimc), cap->active_buf_cnt);
}

static irqreturn_t fimc_irq_handler(int irq, void *priv)
{
	struct fimc_dev *fimc = priv;
	struct fimc_vid_cap *cap = &fimc->vid_cap;
	struct fimc_ctx *ctx;

	fimc_hw_clear_irq(fimc);

	spin_lock(&fimc->slock);

	if (test_and_clear_bit(ST_M2M_PEND, &fimc->state)) {
		if (test_and_clear_bit(ST_M2M_SUSPENDING, &fimc->state)) {
			set_bit(ST_M2M_SUSPENDED, &fimc->state);
			wake_up(&fimc->irq_queue);
			goto out;
		}
		ctx = v4l2_m2m_get_curr_priv(fimc->m2m.m2m_dev);
		if (ctx != NULL) {
			spin_unlock(&fimc->slock);
			fimc_m2m_job_finish(ctx, VB2_BUF_STATE_DONE);

			spin_lock(&ctx->slock);
			if (ctx->state & FIMC_CTX_SHUT) {
				ctx->state &= ~FIMC_CTX_SHUT;
				wake_up(&fimc->irq_queue);
			}
			spin_unlock(&ctx->slock);
		}
		return IRQ_HANDLED;
	} else if (test_bit(ST_CAPT_PEND, &fimc->state)) {
		fimc_capture_irq_handler(fimc,
				 !test_bit(ST_CAPT_JPEG, &fimc->state));
		if (cap->active_buf_cnt == 1) {
			fimc_deactivate_capture(fimc);
			clear_bit(ST_CAPT_STREAM, &fimc->state);
		}
	}
out:
	spin_unlock(&fimc->slock);
	return IRQ_HANDLED;
}

/* The color format (colplanes, memplanes) must be already configured. */
int fimc_prepare_addr(struct fimc_ctx *ctx, struct vb2_buffer *vb,
		      struct fimc_frame *frame, struct fimc_addr *paddr)
{
	int ret = 0;
	u32 pix_size;

	if (vb == NULL || frame == NULL)
		return -EINVAL;

	pix_size = frame->width * frame->height;

	dbg("memplanes= %d, colplanes= %d, pix_size= %d",
		frame->fmt->memplanes, frame->fmt->colplanes, pix_size);

	paddr->y = vb2_dma_contig_plane_dma_addr(vb, 0);

	if (frame->fmt->memplanes == 1) {
		switch (frame->fmt->colplanes) {
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
	} else {
		if (frame->fmt->memplanes >= 2)
			paddr->cb = vb2_dma_contig_plane_dma_addr(vb, 1);

		if (frame->fmt->memplanes == 3)
			paddr->cr = vb2_dma_contig_plane_dma_addr(vb, 2);
	}

	dbg("PHYS_ADDR: y= 0x%X  cb= 0x%X cr= 0x%X ret= %d",
	    paddr->y, paddr->cb, paddr->cr, ret);

	return ret;
}

/* Set order for 1 and 2 plane YCBCR 4:2:2 formats. */
void fimc_set_yuv_order(struct fimc_ctx *ctx)
{
	/* The one only mode supported in SoC. */
	ctx->in_order_2p = S5P_FIMC_LSB_CRCB;
	ctx->out_order_2p = S5P_FIMC_LSB_CRCB;

	/* Set order for 1 plane input formats. */
	switch (ctx->s_frame.fmt->color) {
	case S5P_FIMC_YCRYCB422:
		ctx->in_order_1p = S5P_MSCTRL_ORDER422_CBYCRY;
		break;
	case S5P_FIMC_CBYCRY422:
		ctx->in_order_1p = S5P_MSCTRL_ORDER422_YCRYCB;
		break;
	case S5P_FIMC_CRYCBY422:
		ctx->in_order_1p = S5P_MSCTRL_ORDER422_YCBYCR;
		break;
	case S5P_FIMC_YCBYCR422:
	default:
		ctx->in_order_1p = S5P_MSCTRL_ORDER422_CRYCBY;
		break;
	}
	dbg("ctx->in_order_1p= %d", ctx->in_order_1p);

	switch (ctx->d_frame.fmt->color) {
	case S5P_FIMC_YCRYCB422:
		ctx->out_order_1p = S5P_CIOCTRL_ORDER422_CBYCRY;
		break;
	case S5P_FIMC_CBYCRY422:
		ctx->out_order_1p = S5P_CIOCTRL_ORDER422_YCRYCB;
		break;
	case S5P_FIMC_CRYCBY422:
		ctx->out_order_1p = S5P_CIOCTRL_ORDER422_YCBYCR;
		break;
	case S5P_FIMC_YCBYCR422:
	default:
		ctx->out_order_1p = S5P_CIOCTRL_ORDER422_CRYCBY;
		break;
	}
	dbg("ctx->out_order_1p= %d", ctx->out_order_1p);
}

void fimc_prepare_dma_offset(struct fimc_ctx *ctx, struct fimc_frame *f)
{
	struct samsung_fimc_variant *variant = ctx->fimc_dev->variant;
	u32 i, depth = 0;

	for (i = 0; i < f->fmt->colplanes; i++)
		depth += f->fmt->depth[i];

	f->dma_offset.y_h = f->offs_h;
	if (!variant->pix_hoff)
		f->dma_offset.y_h *= (depth >> 3);

	f->dma_offset.y_v = f->offs_v;

	f->dma_offset.cb_h = f->offs_h;
	f->dma_offset.cb_v = f->offs_v;

	f->dma_offset.cr_h = f->offs_h;
	f->dma_offset.cr_v = f->offs_v;

	if (!variant->pix_hoff) {
		if (f->fmt->colplanes == 3) {
			f->dma_offset.cb_h >>= 1;
			f->dma_offset.cr_h >>= 1;
		}
		if (f->fmt->color == S5P_FIMC_YCBCR420) {
			f->dma_offset.cb_v >>= 1;
			f->dma_offset.cr_v >>= 1;
		}
	}

	dbg("in_offset: color= %d, y_h= %d, y_v= %d",
	    f->fmt->color, f->dma_offset.y_h, f->dma_offset.y_v);
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
int fimc_prepare_config(struct fimc_ctx *ctx, u32 flags)
{
	struct fimc_frame *s_frame, *d_frame;
	struct vb2_buffer *vb = NULL;
	int ret = 0;

	s_frame = &ctx->s_frame;
	d_frame = &ctx->d_frame;

	if (flags & FIMC_PARAMS) {
		/* Prepare the DMA offset ratios for scaler. */
		fimc_prepare_dma_offset(ctx, &ctx->s_frame);
		fimc_prepare_dma_offset(ctx, &ctx->d_frame);

		if (s_frame->height > (SCALER_MAX_VRATIO * d_frame->height) ||
		    s_frame->width > (SCALER_MAX_HRATIO * d_frame->width)) {
			err("out of scaler range");
			return -EINVAL;
		}
		fimc_set_yuv_order(ctx);
	}

	if (flags & FIMC_SRC_ADDR) {
		vb = v4l2_m2m_next_src_buf(ctx->m2m_ctx);
		ret = fimc_prepare_addr(ctx, vb, s_frame, &s_frame->paddr);
		if (ret)
			return ret;
	}

	if (flags & FIMC_DST_ADDR) {
		vb = v4l2_m2m_next_dst_buf(ctx->m2m_ctx);
		ret = fimc_prepare_addr(ctx, vb, d_frame, &d_frame->paddr);
	}

	return ret;
}

static void fimc_dma_run(void *priv)
{
	struct fimc_ctx *ctx = priv;
	struct fimc_dev *fimc;
	unsigned long flags;
	u32 ret;

	if (WARN(!ctx, "null hardware context\n"))
		return;

	fimc = ctx->fimc_dev;
	spin_lock_irqsave(&fimc->slock, flags);
	set_bit(ST_M2M_PEND, &fimc->state);

	spin_lock(&ctx->slock);
	ctx->state |= (FIMC_SRC_ADDR | FIMC_DST_ADDR);
	ret = fimc_prepare_config(ctx, ctx->state);
	if (ret)
		goto dma_unlock;

	/* Reconfigure hardware if the context has changed. */
	if (fimc->m2m.ctx != ctx) {
		ctx->state |= FIMC_PARAMS;
		fimc->m2m.ctx = ctx;
	}
	fimc_hw_set_input_addr(fimc, &ctx->s_frame.paddr);

	if (ctx->state & FIMC_PARAMS) {
		fimc_hw_set_input_path(ctx);
		fimc_hw_set_in_dma(ctx);
		ret = fimc_set_scaler_info(ctx);
		if (ret) {
			spin_unlock(&fimc->slock);
			goto dma_unlock;
		}
		fimc_hw_set_prescaler(ctx);
		fimc_hw_set_mainscaler(ctx);
		fimc_hw_set_target_format(ctx);
		fimc_hw_set_rotation(ctx);
		fimc_hw_set_effect(ctx, false);
	}

	fimc_hw_set_output_path(ctx);
	if (ctx->state & (FIMC_DST_ADDR | FIMC_PARAMS))
		fimc_hw_set_output_addr(fimc, &ctx->d_frame.paddr, -1);

	if (ctx->state & FIMC_PARAMS)
		fimc_hw_set_out_dma(ctx);

	fimc_activate_capture(ctx);

	ctx->state &= (FIMC_CTX_M2M | FIMC_CTX_CAP |
		       FIMC_SRC_FMT | FIMC_DST_FMT);
	fimc_hw_activate_input_dma(fimc, true);
dma_unlock:
	spin_unlock(&ctx->slock);
	spin_unlock_irqrestore(&fimc->slock, flags);
}

static void fimc_job_abort(void *priv)
{
	fimc_m2m_shutdown(priv);
}

static int fimc_queue_setup(struct vb2_queue *vq, const struct v4l2_format *fmt,
			    unsigned int *num_buffers, unsigned int *num_planes,
			    unsigned int sizes[], void *allocators[])
{
	struct fimc_ctx *ctx = vb2_get_drv_priv(vq);
	struct fimc_frame *f;
	int i;

	f = ctx_get_frame(ctx, vq->type);
	if (IS_ERR(f))
		return PTR_ERR(f);
	/*
	 * Return number of non-contigous planes (plane buffers)
	 * depending on the configured color format.
	 */
	if (!f->fmt)
		return -EINVAL;

	*num_planes = f->fmt->memplanes;
	for (i = 0; i < f->fmt->memplanes; i++) {
		sizes[i] = (f->f_width * f->f_height * f->fmt->depth[i]) / 8;
		allocators[i] = ctx->fimc_dev->alloc_ctx;
	}
	return 0;
}

static int fimc_buf_prepare(struct vb2_buffer *vb)
{
	struct fimc_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct fimc_frame *frame;
	int i;

	frame = ctx_get_frame(ctx, vb->vb2_queue->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	for (i = 0; i < frame->fmt->memplanes; i++)
		vb2_set_plane_payload(vb, i, frame->payload[i]);

	return 0;
}

static void fimc_buf_queue(struct vb2_buffer *vb)
{
	struct fimc_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	dbg("ctx: %p, ctx->state: 0x%x", ctx, ctx->state);

	if (ctx->m2m_ctx)
		v4l2_m2m_buf_queue(ctx->m2m_ctx, vb);
}

static void fimc_lock(struct vb2_queue *vq)
{
	struct fimc_ctx *ctx = vb2_get_drv_priv(vq);
	mutex_lock(&ctx->fimc_dev->lock);
}

static void fimc_unlock(struct vb2_queue *vq)
{
	struct fimc_ctx *ctx = vb2_get_drv_priv(vq);
	mutex_unlock(&ctx->fimc_dev->lock);
}

static struct vb2_ops fimc_qops = {
	.queue_setup	 = fimc_queue_setup,
	.buf_prepare	 = fimc_buf_prepare,
	.buf_queue	 = fimc_buf_queue,
	.wait_prepare	 = fimc_unlock,
	.wait_finish	 = fimc_lock,
	.stop_streaming	 = stop_streaming,
	.start_streaming = start_streaming,
};

/*
 * V4L2 controls handling
 */
#define ctrl_to_ctx(__ctrl) \
	container_of((__ctrl)->handler, struct fimc_ctx, ctrl_handler)

static int fimc_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct fimc_ctx *ctx = ctrl_to_ctx(ctrl);
	struct fimc_dev *fimc = ctx->fimc_dev;
	struct samsung_fimc_variant *variant = fimc->variant;
	unsigned long flags;
	int ret = 0;

	if (ctrl->flags & V4L2_CTRL_FLAG_INACTIVE)
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_HFLIP:
		spin_lock_irqsave(&ctx->slock, flags);
		ctx->hflip = ctrl->val;
		break;

	case V4L2_CID_VFLIP:
		spin_lock_irqsave(&ctx->slock, flags);
		ctx->vflip = ctrl->val;
		break;

	case V4L2_CID_ROTATE:
		if (fimc_capture_pending(fimc) ||
		    fimc_ctx_state_is_set(FIMC_DST_FMT | FIMC_SRC_FMT, ctx)) {
			ret = fimc_check_scaler_ratio(ctx, ctx->s_frame.width,
					ctx->s_frame.height, ctx->d_frame.width,
					ctx->d_frame.height, ctrl->val);
		}
		if (ret) {
			v4l2_err(fimc->m2m.vfd, "Out of scaler range\n");
			return -EINVAL;
		}
		if ((ctrl->val == 90 || ctrl->val == 270) &&
		    !variant->has_out_rot)
			return -EINVAL;
		spin_lock_irqsave(&ctx->slock, flags);
		ctx->rotation = ctrl->val;
		break;

	default:
		v4l2_err(fimc->v4l2_dev, "Invalid control: 0x%X\n", ctrl->id);
		return -EINVAL;
	}
	ctx->state |= FIMC_PARAMS;
	set_bit(ST_CAPT_APPLY_CFG, &fimc->state);
	spin_unlock_irqrestore(&ctx->slock, flags);
	return 0;
}

static const struct v4l2_ctrl_ops fimc_ctrl_ops = {
	.s_ctrl = fimc_s_ctrl,
};

int fimc_ctrls_create(struct fimc_ctx *ctx)
{
	if (ctx->ctrls_rdy)
		return 0;
	v4l2_ctrl_handler_init(&ctx->ctrl_handler, 3);

	ctx->ctrl_rotate = v4l2_ctrl_new_std(&ctx->ctrl_handler, &fimc_ctrl_ops,
				     V4L2_CID_HFLIP, 0, 1, 1, 0);
	ctx->ctrl_hflip = v4l2_ctrl_new_std(&ctx->ctrl_handler, &fimc_ctrl_ops,
				    V4L2_CID_VFLIP, 0, 1, 1, 0);
	ctx->ctrl_vflip = v4l2_ctrl_new_std(&ctx->ctrl_handler, &fimc_ctrl_ops,
				    V4L2_CID_ROTATE, 0, 270, 90, 0);
	ctx->ctrls_rdy = ctx->ctrl_handler.error == 0;

	return ctx->ctrl_handler.error;
}

void fimc_ctrls_delete(struct fimc_ctx *ctx)
{
	if (ctx->ctrls_rdy) {
		v4l2_ctrl_handler_free(&ctx->ctrl_handler);
		ctx->ctrls_rdy = false;
	}
}

void fimc_ctrls_activate(struct fimc_ctx *ctx, bool active)
{
	if (!ctx->ctrls_rdy)
		return;

	mutex_lock(&ctx->ctrl_handler.lock);
	v4l2_ctrl_activate(ctx->ctrl_rotate, active);
	v4l2_ctrl_activate(ctx->ctrl_hflip, active);
	v4l2_ctrl_activate(ctx->ctrl_vflip, active);

	if (active) {
		ctx->rotation = ctx->ctrl_rotate->val;
		ctx->hflip    = ctx->ctrl_hflip->val;
		ctx->vflip    = ctx->ctrl_vflip->val;
	} else {
		ctx->rotation = 0;
		ctx->hflip    = 0;
		ctx->vflip    = 0;
	}
	mutex_unlock(&ctx->ctrl_handler.lock);
}

/*
 * V4L2 ioctl handlers
 */
static int fimc_m2m_querycap(struct file *file, void *fh,
			     struct v4l2_capability *cap)
{
	struct fimc_ctx *ctx = fh_to_ctx(fh);
	struct fimc_dev *fimc = ctx->fimc_dev;

	strncpy(cap->driver, fimc->pdev->name, sizeof(cap->driver) - 1);
	strncpy(cap->card, fimc->pdev->name, sizeof(cap->card) - 1);
	cap->bus_info[0] = 0;
	cap->capabilities = V4L2_CAP_STREAMING |
		V4L2_CAP_VIDEO_CAPTURE_MPLANE | V4L2_CAP_VIDEO_OUTPUT_MPLANE;

	return 0;
}

static int fimc_m2m_enum_fmt_mplane(struct file *file, void *priv,
				    struct v4l2_fmtdesc *f)
{
	struct fimc_fmt *fmt;

	fmt = fimc_find_format(NULL, NULL, FMT_FLAGS_M2M, f->index);
	if (!fmt)
		return -EINVAL;

	strncpy(f->description, fmt->name, sizeof(f->description) - 1);
	f->pixelformat = fmt->fourcc;
	return 0;
}

int fimc_fill_format(struct fimc_frame *frame, struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pixm = &f->fmt.pix_mp;
	int i;

	pixm->width = frame->o_width;
	pixm->height = frame->o_height;
	pixm->field = V4L2_FIELD_NONE;
	pixm->pixelformat = frame->fmt->fourcc;
	pixm->colorspace = V4L2_COLORSPACE_JPEG;
	pixm->num_planes = frame->fmt->memplanes;

	for (i = 0; i < pixm->num_planes; ++i) {
		int bpl = frame->f_width;
		if (frame->fmt->colplanes == 1) /* packed formats */
			bpl = (bpl * frame->fmt->depth[0]) / 8;
		pixm->plane_fmt[i].bytesperline = bpl;
		pixm->plane_fmt[i].sizeimage = (frame->o_width *
			frame->o_height * frame->fmt->depth[i]) / 8;
	}
	return 0;
}

void fimc_fill_frame(struct fimc_frame *frame, struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pixm = &f->fmt.pix_mp;

	frame->f_width  = pixm->plane_fmt[0].bytesperline;
	if (frame->fmt->colplanes == 1)
		frame->f_width = (frame->f_width * 8) / frame->fmt->depth[0];
	frame->f_height	= pixm->height;
	frame->width    = pixm->width;
	frame->height   = pixm->height;
	frame->o_width  = pixm->width;
	frame->o_height = pixm->height;
	frame->offs_h   = 0;
	frame->offs_v   = 0;
}

/**
 * fimc_adjust_mplane_format - adjust bytesperline/sizeimage for each plane
 * @fmt: fimc pixel format description (input)
 * @width: requested pixel width
 * @height: requested pixel height
 * @pix: multi-plane format to adjust
 */
void fimc_adjust_mplane_format(struct fimc_fmt *fmt, u32 width, u32 height,
			       struct v4l2_pix_format_mplane *pix)
{
	u32 bytesperline = 0;
	int i;

	pix->colorspace	= V4L2_COLORSPACE_JPEG;
	pix->field = V4L2_FIELD_NONE;
	pix->num_planes = fmt->memplanes;
	pix->height = height;
	pix->width = width;

	for (i = 0; i < pix->num_planes; ++i) {
		u32 bpl = pix->plane_fmt[i].bytesperline;
		u32 *sizeimage = &pix->plane_fmt[i].sizeimage;

		if (fmt->colplanes > 1 && (bpl == 0 || bpl < pix->width))
			bpl = pix->width; /* Planar */

		if (fmt->colplanes == 1 && /* Packed */
		    (bpl == 0 || ((bpl * 8) / fmt->depth[i]) < pix->width))
			bpl = (pix->width * fmt->depth[0]) / 8;

		if (i == 0) /* Same bytesperline for each plane. */
			bytesperline = bpl;

		pix->plane_fmt[i].bytesperline = bytesperline;
		*sizeimage = (pix->width * pix->height * fmt->depth[i]) / 8;
	}
}

static int fimc_m2m_g_fmt_mplane(struct file *file, void *fh,
				 struct v4l2_format *f)
{
	struct fimc_ctx *ctx = fh_to_ctx(fh);
	struct fimc_frame *frame = ctx_get_frame(ctx, f->type);

	if (IS_ERR(frame))
		return PTR_ERR(frame);

	return fimc_fill_format(frame, f);
}

/**
 * fimc_find_format - lookup fimc color format by fourcc or media bus format
 * @pixelformat: fourcc to match, ignored if null
 * @mbus_code: media bus code to match, ignored if null
 * @mask: the color flags to match
 * @index: offset in the fimc_formats array, ignored if negative
 */
struct fimc_fmt *fimc_find_format(u32 *pixelformat, u32 *mbus_code,
				  unsigned int mask, int index)
{
	struct fimc_fmt *fmt, *def_fmt = NULL;
	unsigned int i;
	int id = 0;

	if (index >= ARRAY_SIZE(fimc_formats))
		return NULL;

	for (i = 0; i < ARRAY_SIZE(fimc_formats); ++i) {
		fmt = &fimc_formats[i];
		if (!(fmt->flags & mask))
			continue;
		if (pixelformat && fmt->fourcc == *pixelformat)
			return fmt;
		if (mbus_code && fmt->mbus_code == *mbus_code)
			return fmt;
		if (index == id)
			def_fmt = fmt;
		id++;
	}
	return def_fmt;
}

static int fimc_try_fmt_mplane(struct fimc_ctx *ctx, struct v4l2_format *f)
{
	struct fimc_dev *fimc = ctx->fimc_dev;
	struct samsung_fimc_variant *variant = fimc->variant;
	struct v4l2_pix_format_mplane *pix = &f->fmt.pix_mp;
	struct fimc_fmt *fmt;
	u32 max_w, mod_x, mod_y;

	if (!IS_M2M(f->type))
		return -EINVAL;

	dbg("w: %d, h: %d", pix->width, pix->height);

	fmt = fimc_find_format(&pix->pixelformat, NULL, FMT_FLAGS_M2M, 0);
	if (WARN(fmt == NULL, "Pixel format lookup failed"))
		return -EINVAL;

	if (pix->field == V4L2_FIELD_ANY)
		pix->field = V4L2_FIELD_NONE;
	else if (pix->field != V4L2_FIELD_NONE)
		return -EINVAL;

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		max_w = variant->pix_limit->scaler_dis_w;
		mod_x = ffs(variant->min_inp_pixsize) - 1;
	} else {
		max_w = variant->pix_limit->out_rot_dis_w;
		mod_x = ffs(variant->min_out_pixsize) - 1;
	}

	if (tiled_fmt(fmt)) {
		mod_x = 6; /* 64 x 32 pixels tile */
		mod_y = 5;
	} else {
		if (fimc->id == 1 && variant->pix_hoff)
			mod_y = fimc_fmt_is_rgb(fmt->color) ? 0 : 1;
		else
			mod_y = mod_x;
	}
	dbg("mod_x: %d, mod_y: %d, max_w: %d", mod_x, mod_y, max_w);

	v4l_bound_align_image(&pix->width, 16, max_w, mod_x,
		&pix->height, 8, variant->pix_limit->scaler_dis_w, mod_y, 0);

	fimc_adjust_mplane_format(fmt, pix->width, pix->height, &f->fmt.pix_mp);
	return 0;
}

static int fimc_m2m_try_fmt_mplane(struct file *file, void *fh,
				   struct v4l2_format *f)
{
	struct fimc_ctx *ctx = fh_to_ctx(fh);

	return fimc_try_fmt_mplane(ctx, f);
}

static int fimc_m2m_s_fmt_mplane(struct file *file, void *fh,
				 struct v4l2_format *f)
{
	struct fimc_ctx *ctx = fh_to_ctx(fh);
	struct fimc_dev *fimc = ctx->fimc_dev;
	struct vb2_queue *vq;
	struct fimc_frame *frame;
	struct v4l2_pix_format_mplane *pix;
	int i, ret = 0;

	ret = fimc_try_fmt_mplane(ctx, f);
	if (ret)
		return ret;

	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, f->type);

	if (vb2_is_busy(vq)) {
		v4l2_err(fimc->m2m.vfd, "queue (%d) busy\n", f->type);
		return -EBUSY;
	}

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		frame = &ctx->s_frame;
	else
		frame = &ctx->d_frame;

	pix = &f->fmt.pix_mp;
	frame->fmt = fimc_find_format(&pix->pixelformat, NULL,
				      FMT_FLAGS_M2M, 0);
	if (!frame->fmt)
		return -EINVAL;

	for (i = 0; i < frame->fmt->colplanes; i++) {
		frame->payload[i] =
			(pix->width * pix->height * frame->fmt->depth[i]) / 8;
	}

	fimc_fill_frame(frame, f);

	ctx->scaler.enabled = 1;

	if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		fimc_ctx_state_lock_set(FIMC_PARAMS | FIMC_DST_FMT, ctx);
	else
		fimc_ctx_state_lock_set(FIMC_PARAMS | FIMC_SRC_FMT, ctx);

	dbg("f_w: %d, f_h: %d", frame->f_width, frame->f_height);

	return 0;
}

static int fimc_m2m_reqbufs(struct file *file, void *fh,
			    struct v4l2_requestbuffers *reqbufs)
{
	struct fimc_ctx *ctx = fh_to_ctx(fh);

	return v4l2_m2m_reqbufs(file, ctx->m2m_ctx, reqbufs);
}

static int fimc_m2m_querybuf(struct file *file, void *fh,
			     struct v4l2_buffer *buf)
{
	struct fimc_ctx *ctx = fh_to_ctx(fh);

	return v4l2_m2m_querybuf(file, ctx->m2m_ctx, buf);
}

static int fimc_m2m_qbuf(struct file *file, void *fh,
			 struct v4l2_buffer *buf)
{
	struct fimc_ctx *ctx = fh_to_ctx(fh);

	return v4l2_m2m_qbuf(file, ctx->m2m_ctx, buf);
}

static int fimc_m2m_dqbuf(struct file *file, void *fh,
			  struct v4l2_buffer *buf)
{
	struct fimc_ctx *ctx = fh_to_ctx(fh);

	return v4l2_m2m_dqbuf(file, ctx->m2m_ctx, buf);
}

static int fimc_m2m_streamon(struct file *file, void *fh,
			     enum v4l2_buf_type type)
{
	struct fimc_ctx *ctx = fh_to_ctx(fh);

	/* The source and target color format need to be set */
	if (V4L2_TYPE_IS_OUTPUT(type)) {
		if (!fimc_ctx_state_is_set(FIMC_SRC_FMT, ctx))
			return -EINVAL;
	} else if (!fimc_ctx_state_is_set(FIMC_DST_FMT, ctx)) {
		return -EINVAL;
	}

	return v4l2_m2m_streamon(file, ctx->m2m_ctx, type);
}

static int fimc_m2m_streamoff(struct file *file, void *fh,
			    enum v4l2_buf_type type)
{
	struct fimc_ctx *ctx = fh_to_ctx(fh);

	return v4l2_m2m_streamoff(file, ctx->m2m_ctx, type);
}

static int fimc_m2m_cropcap(struct file *file, void *fh,
			    struct v4l2_cropcap *cr)
{
	struct fimc_ctx *ctx = fh_to_ctx(fh);
	struct fimc_frame *frame;

	frame = ctx_get_frame(ctx, cr->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	cr->bounds.left		= 0;
	cr->bounds.top		= 0;
	cr->bounds.width	= frame->o_width;
	cr->bounds.height	= frame->o_height;
	cr->defrect		= cr->bounds;

	return 0;
}

static int fimc_m2m_g_crop(struct file *file, void *fh, struct v4l2_crop *cr)
{
	struct fimc_ctx *ctx = fh_to_ctx(fh);
	struct fimc_frame *frame;

	frame = ctx_get_frame(ctx, cr->type);
	if (IS_ERR(frame))
		return PTR_ERR(frame);

	cr->c.left = frame->offs_h;
	cr->c.top = frame->offs_v;
	cr->c.width = frame->width;
	cr->c.height = frame->height;

	return 0;
}

static int fimc_m2m_try_crop(struct fimc_ctx *ctx, struct v4l2_crop *cr)
{
	struct fimc_dev *fimc = ctx->fimc_dev;
	struct fimc_frame *f;
	u32 min_size, halign, depth = 0;
	int i;

	if (cr->c.top < 0 || cr->c.left < 0) {
		v4l2_err(fimc->m2m.vfd,
			"doesn't support negative values for top & left\n");
		return -EINVAL;
	}
	if (cr->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		f = &ctx->d_frame;
	else if (cr->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		f = &ctx->s_frame;
	else
		return -EINVAL;

	min_size = (f == &ctx->s_frame) ?
		fimc->variant->min_inp_pixsize : fimc->variant->min_out_pixsize;

	/* Get pixel alignment constraints. */
	if (fimc->id == 1 && fimc->variant->pix_hoff)
		halign = fimc_fmt_is_rgb(f->fmt->color) ? 0 : 1;
	else
		halign = ffs(min_size) - 1;

	for (i = 0; i < f->fmt->colplanes; i++)
		depth += f->fmt->depth[i];

	v4l_bound_align_image(&cr->c.width, min_size, f->o_width,
			      ffs(min_size) - 1,
			      &cr->c.height, min_size, f->o_height,
			      halign, 64/(ALIGN(depth, 8)));

	/* adjust left/top if cropping rectangle is out of bounds */
	if (cr->c.left + cr->c.width > f->o_width)
		cr->c.left = f->o_width - cr->c.width;
	if (cr->c.top + cr->c.height > f->o_height)
		cr->c.top = f->o_height - cr->c.height;

	cr->c.left = round_down(cr->c.left, min_size);
	cr->c.top  = round_down(cr->c.top, fimc->variant->hor_offs_align);

	dbg("l:%d, t:%d, w:%d, h:%d, f_w: %d, f_h: %d",
	    cr->c.left, cr->c.top, cr->c.width, cr->c.height,
	    f->f_width, f->f_height);

	return 0;
}

static int fimc_m2m_s_crop(struct file *file, void *fh, struct v4l2_crop *cr)
{
	struct fimc_ctx *ctx = fh_to_ctx(fh);
	struct fimc_dev *fimc = ctx->fimc_dev;
	struct fimc_frame *f;
	int ret;

	ret = fimc_m2m_try_crop(ctx, cr);
	if (ret)
		return ret;

	f = (cr->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) ?
		&ctx->s_frame : &ctx->d_frame;

	/* Check to see if scaling ratio is within supported range */
	if (fimc_ctx_state_is_set(FIMC_DST_FMT | FIMC_SRC_FMT, ctx)) {
		if (cr->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
			ret = fimc_check_scaler_ratio(ctx, cr->c.width,
					cr->c.height, ctx->d_frame.width,
					ctx->d_frame.height, ctx->rotation);
		} else {
			ret = fimc_check_scaler_ratio(ctx, ctx->s_frame.width,
					ctx->s_frame.height, cr->c.width,
					cr->c.height, ctx->rotation);
		}
		if (ret) {
			v4l2_err(fimc->m2m.vfd, "Out of scaler range\n");
			return -EINVAL;
		}
	}

	f->offs_h = cr->c.left;
	f->offs_v = cr->c.top;
	f->width  = cr->c.width;
	f->height = cr->c.height;

	fimc_ctx_state_lock_set(FIMC_PARAMS, ctx);

	return 0;
}

static const struct v4l2_ioctl_ops fimc_m2m_ioctl_ops = {
	.vidioc_querycap		= fimc_m2m_querycap,

	.vidioc_enum_fmt_vid_cap_mplane	= fimc_m2m_enum_fmt_mplane,
	.vidioc_enum_fmt_vid_out_mplane	= fimc_m2m_enum_fmt_mplane,

	.vidioc_g_fmt_vid_cap_mplane	= fimc_m2m_g_fmt_mplane,
	.vidioc_g_fmt_vid_out_mplane	= fimc_m2m_g_fmt_mplane,

	.vidioc_try_fmt_vid_cap_mplane	= fimc_m2m_try_fmt_mplane,
	.vidioc_try_fmt_vid_out_mplane	= fimc_m2m_try_fmt_mplane,

	.vidioc_s_fmt_vid_cap_mplane	= fimc_m2m_s_fmt_mplane,
	.vidioc_s_fmt_vid_out_mplane	= fimc_m2m_s_fmt_mplane,

	.vidioc_reqbufs			= fimc_m2m_reqbufs,
	.vidioc_querybuf		= fimc_m2m_querybuf,

	.vidioc_qbuf			= fimc_m2m_qbuf,
	.vidioc_dqbuf			= fimc_m2m_dqbuf,

	.vidioc_streamon		= fimc_m2m_streamon,
	.vidioc_streamoff		= fimc_m2m_streamoff,

	.vidioc_g_crop			= fimc_m2m_g_crop,
	.vidioc_s_crop			= fimc_m2m_s_crop,
	.vidioc_cropcap			= fimc_m2m_cropcap

};

static int queue_init(void *priv, struct vb2_queue *src_vq,
		      struct vb2_queue *dst_vq)
{
	struct fimc_ctx *ctx = priv;
	int ret;

	memset(src_vq, 0, sizeof(*src_vq));
	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_USERPTR;
	src_vq->drv_priv = ctx;
	src_vq->ops = &fimc_qops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	memset(dst_vq, 0, sizeof(*dst_vq));
	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_USERPTR;
	dst_vq->drv_priv = ctx;
	dst_vq->ops = &fimc_qops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);

	return vb2_queue_init(dst_vq);
}

static int fimc_m2m_open(struct file *file)
{
	struct fimc_dev *fimc = video_drvdata(file);
	struct fimc_ctx *ctx;
	int ret;

	dbg("pid: %d, state: 0x%lx, refcnt: %d",
		task_pid_nr(current), fimc->state, fimc->vid_cap.refcnt);

	/*
	 * Return if the corresponding video capture node
	 * is already opened.
	 */
	if (fimc->vid_cap.refcnt > 0)
		return -EBUSY;

	ctx = kzalloc(sizeof *ctx, GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	v4l2_fh_init(&ctx->fh, fimc->m2m.vfd);
	ret = fimc_ctrls_create(ctx);
	if (ret)
		goto error_fh;

	/* Use separate control handler per file handle */
	ctx->fh.ctrl_handler = &ctx->ctrl_handler;
	file->private_data = &ctx->fh;
	v4l2_fh_add(&ctx->fh);

	ctx->fimc_dev = fimc;
	/* Default color format */
	ctx->s_frame.fmt = &fimc_formats[0];
	ctx->d_frame.fmt = &fimc_formats[0];
	/* Setup the device context for memory-to-memory mode */
	ctx->state = FIMC_CTX_M2M;
	ctx->flags = 0;
	ctx->in_path = FIMC_DMA;
	ctx->out_path = FIMC_DMA;
	spin_lock_init(&ctx->slock);

	ctx->m2m_ctx = v4l2_m2m_ctx_init(fimc->m2m.m2m_dev, ctx, queue_init);
	if (IS_ERR(ctx->m2m_ctx)) {
		ret = PTR_ERR(ctx->m2m_ctx);
		goto error_c;
	}

	if (fimc->m2m.refcnt++ == 0)
		set_bit(ST_M2M_RUN, &fimc->state);
	return 0;

error_c:
	fimc_ctrls_delete(ctx);
error_fh:
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	kfree(ctx);
	return ret;
}

static int fimc_m2m_release(struct file *file)
{
	struct fimc_ctx *ctx = fh_to_ctx(file->private_data);
	struct fimc_dev *fimc = ctx->fimc_dev;

	dbg("pid: %d, state: 0x%lx, refcnt= %d",
		task_pid_nr(current), fimc->state, fimc->m2m.refcnt);

	v4l2_m2m_ctx_release(ctx->m2m_ctx);
	fimc_ctrls_delete(ctx);
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);

	if (--fimc->m2m.refcnt <= 0)
		clear_bit(ST_M2M_RUN, &fimc->state);
	kfree(ctx);
	return 0;
}

static unsigned int fimc_m2m_poll(struct file *file,
				  struct poll_table_struct *wait)
{
	struct fimc_ctx *ctx = fh_to_ctx(file->private_data);

	return v4l2_m2m_poll(file, ctx->m2m_ctx, wait);
}


static int fimc_m2m_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct fimc_ctx *ctx = fh_to_ctx(file->private_data);

	return v4l2_m2m_mmap(file, ctx->m2m_ctx, vma);
}

static const struct v4l2_file_operations fimc_m2m_fops = {
	.owner		= THIS_MODULE,
	.open		= fimc_m2m_open,
	.release	= fimc_m2m_release,
	.poll		= fimc_m2m_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= fimc_m2m_mmap,
};

static struct v4l2_m2m_ops m2m_ops = {
	.device_run	= fimc_dma_run,
	.job_abort	= fimc_job_abort,
};

int fimc_register_m2m_device(struct fimc_dev *fimc,
			     struct v4l2_device *v4l2_dev)
{
	struct video_device *vfd;
	struct platform_device *pdev;
	int ret = 0;

	if (!fimc)
		return -ENODEV;

	pdev = fimc->pdev;
	fimc->v4l2_dev = v4l2_dev;

	vfd = video_device_alloc();
	if (!vfd) {
		v4l2_err(v4l2_dev, "Failed to allocate video device\n");
		return -ENOMEM;
	}

	vfd->fops	= &fimc_m2m_fops;
	vfd->ioctl_ops	= &fimc_m2m_ioctl_ops;
	vfd->v4l2_dev	= v4l2_dev;
	vfd->minor	= -1;
	vfd->release	= video_device_release;
	vfd->lock	= &fimc->lock;

	snprintf(vfd->name, sizeof(vfd->name), "%s.m2m", dev_name(&pdev->dev));
	video_set_drvdata(vfd, fimc);

	fimc->m2m.vfd = vfd;
	fimc->m2m.m2m_dev = v4l2_m2m_init(&m2m_ops);
	if (IS_ERR(fimc->m2m.m2m_dev)) {
		v4l2_err(v4l2_dev, "failed to initialize v4l2-m2m device\n");
		ret = PTR_ERR(fimc->m2m.m2m_dev);
		goto err_init;
	}

	ret = media_entity_init(&vfd->entity, 0, NULL, 0);
	if (!ret)
		return 0;

	v4l2_m2m_release(fimc->m2m.m2m_dev);
err_init:
	video_device_release(fimc->m2m.vfd);
	return ret;
}

void fimc_unregister_m2m_device(struct fimc_dev *fimc)
{
	if (!fimc)
		return;

	if (fimc->m2m.m2m_dev)
		v4l2_m2m_release(fimc->m2m.m2m_dev);
	if (fimc->m2m.vfd) {
		media_entity_cleanup(&fimc->m2m.vfd->entity);
		/* Can also be called if video device wasn't registered */
		video_unregister_device(fimc->m2m.vfd);
	}
}

static void fimc_clk_put(struct fimc_dev *fimc)
{
	int i;
	for (i = 0; i < fimc->num_clocks; i++) {
		if (fimc->clock[i])
			clk_put(fimc->clock[i]);
	}
}

static int fimc_clk_get(struct fimc_dev *fimc)
{
	int i;
	for (i = 0; i < fimc->num_clocks; i++) {
		fimc->clock[i] = clk_get(&fimc->pdev->dev, fimc_clocks[i]);
		if (!IS_ERR_OR_NULL(fimc->clock[i]))
			continue;
		dev_err(&fimc->pdev->dev, "failed to get fimc clock: %s\n",
			fimc_clocks[i]);
		return -ENXIO;
	}

	return 0;
}

static int fimc_m2m_suspend(struct fimc_dev *fimc)
{
	unsigned long flags;
	int timeout;

	spin_lock_irqsave(&fimc->slock, flags);
	if (!fimc_m2m_pending(fimc)) {
		spin_unlock_irqrestore(&fimc->slock, flags);
		return 0;
	}
	clear_bit(ST_M2M_SUSPENDED, &fimc->state);
	set_bit(ST_M2M_SUSPENDING, &fimc->state);
	spin_unlock_irqrestore(&fimc->slock, flags);

	timeout = wait_event_timeout(fimc->irq_queue,
			     test_bit(ST_M2M_SUSPENDED, &fimc->state),
			     FIMC_SHUTDOWN_TIMEOUT);

	clear_bit(ST_M2M_SUSPENDING, &fimc->state);
	return timeout == 0 ? -EAGAIN : 0;
}

static int fimc_m2m_resume(struct fimc_dev *fimc)
{
	unsigned long flags;

	spin_lock_irqsave(&fimc->slock, flags);
	/* Clear for full H/W setup in first run after resume */
	fimc->m2m.ctx = NULL;
	spin_unlock_irqrestore(&fimc->slock, flags);

	if (test_and_clear_bit(ST_M2M_SUSPENDED, &fimc->state))
		fimc_m2m_job_finish(fimc->m2m.ctx,
				    VB2_BUF_STATE_ERROR);
	return 0;
}

static int fimc_probe(struct platform_device *pdev)
{
	struct fimc_dev *fimc;
	struct resource *res;
	struct samsung_fimc_driverdata *drv_data;
	struct s5p_platform_fimc *pdata;
	int ret = 0;

	dev_dbg(&pdev->dev, "%s():\n", __func__);

	drv_data = (struct samsung_fimc_driverdata *)
		platform_get_device_id(pdev)->driver_data;

	if (pdev->id >= drv_data->num_entities) {
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
	pdata = pdev->dev.platform_data;
	fimc->pdata = pdata;

	set_bit(ST_LPM, &fimc->state);

	init_waitqueue_head(&fimc->irq_queue);
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

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to get IRQ resource\n");
		ret = -ENXIO;
		goto err_regs_unmap;
	}
	fimc->irq = res->start;

	fimc->num_clocks = MAX_FIMC_CLOCKS;
	ret = fimc_clk_get(fimc);
	if (ret)
		goto err_regs_unmap;
	clk_set_rate(fimc->clock[CLK_BUS], drv_data->lclk_frequency);
	clk_enable(fimc->clock[CLK_BUS]);

	platform_set_drvdata(pdev, fimc);

	ret = request_irq(fimc->irq, fimc_irq_handler, 0, pdev->name, fimc);
	if (ret) {
		dev_err(&pdev->dev, "failed to install irq (%d)\n", ret);
		goto err_clk;
	}

	pm_runtime_enable(&pdev->dev);
	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0)
		goto err_irq;
	/* Initialize contiguous memory allocator */
	fimc->alloc_ctx = vb2_dma_contig_init_ctx(&pdev->dev);
	if (IS_ERR(fimc->alloc_ctx)) {
		ret = PTR_ERR(fimc->alloc_ctx);
		goto err_pm;
	}

	dev_dbg(&pdev->dev, "FIMC.%d registered successfully\n", fimc->id);

	pm_runtime_put(&pdev->dev);
	return 0;

err_pm:
	pm_runtime_put(&pdev->dev);
err_irq:
	free_irq(fimc->irq, fimc);
err_clk:
	fimc_clk_put(fimc);
err_regs_unmap:
	iounmap(fimc->regs);
err_req_region:
	release_resource(fimc->regs_res);
	kfree(fimc->regs_res);
err_info:
	kfree(fimc);
	return ret;
}

static int fimc_runtime_resume(struct device *dev)
{
	struct fimc_dev *fimc =	dev_get_drvdata(dev);

	dbg("fimc%d: state: 0x%lx", fimc->id, fimc->state);

	/* Enable clocks and perform basic initalization */
	clk_enable(fimc->clock[CLK_GATE]);
	fimc_hw_reset(fimc);
	if (fimc->variant->out_buf_count > 4)
		fimc_hw_set_dma_seq(fimc, 0xF);

	/* Resume the capture or mem-to-mem device */
	if (fimc_capture_busy(fimc))
		return fimc_capture_resume(fimc);
	else if (fimc_m2m_pending(fimc))
		return fimc_m2m_resume(fimc);
	return 0;
}

static int fimc_runtime_suspend(struct device *dev)
{
	struct fimc_dev *fimc =	dev_get_drvdata(dev);
	int ret = 0;

	if (fimc_capture_busy(fimc))
		ret = fimc_capture_suspend(fimc);
	else
		ret = fimc_m2m_suspend(fimc);
	if (!ret)
		clk_disable(fimc->clock[CLK_GATE]);

	dbg("fimc%d: state: 0x%lx", fimc->id, fimc->state);
	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int fimc_resume(struct device *dev)
{
	struct fimc_dev *fimc =	dev_get_drvdata(dev);
	unsigned long flags;

	dbg("fimc%d: state: 0x%lx", fimc->id, fimc->state);

	/* Do not resume if the device was idle before system suspend */
	spin_lock_irqsave(&fimc->slock, flags);
	if (!test_and_clear_bit(ST_LPM, &fimc->state) ||
	    (!fimc_m2m_active(fimc) && !fimc_capture_busy(fimc))) {
		spin_unlock_irqrestore(&fimc->slock, flags);
		return 0;
	}
	fimc_hw_reset(fimc);
	if (fimc->variant->out_buf_count > 4)
		fimc_hw_set_dma_seq(fimc, 0xF);
	spin_unlock_irqrestore(&fimc->slock, flags);

	if (fimc_capture_busy(fimc))
		return fimc_capture_resume(fimc);

	return fimc_m2m_resume(fimc);
}

static int fimc_suspend(struct device *dev)
{
	struct fimc_dev *fimc =	dev_get_drvdata(dev);

	dbg("fimc%d: state: 0x%lx", fimc->id, fimc->state);

	if (test_and_set_bit(ST_LPM, &fimc->state))
		return 0;
	if (fimc_capture_busy(fimc))
		return fimc_capture_suspend(fimc);

	return fimc_m2m_suspend(fimc);
}
#endif /* CONFIG_PM_SLEEP */

static int __devexit fimc_remove(struct platform_device *pdev)
{
	struct fimc_dev *fimc = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);
	fimc_runtime_suspend(&pdev->dev);
	pm_runtime_set_suspended(&pdev->dev);

	vb2_dma_contig_cleanup_ctx(fimc->alloc_ctx);

	clk_disable(fimc->clock[CLK_BUS]);
	fimc_clk_put(fimc);
	free_irq(fimc->irq, fimc);
	iounmap(fimc->regs);
	release_resource(fimc->regs_res);
	kfree(fimc->regs_res);
	kfree(fimc);

	dev_info(&pdev->dev, "driver unloaded\n");
	return 0;
}

/* Image pixel limits, similar across several FIMC HW revisions. */
static struct fimc_pix_limit s5p_pix_limit[4] = {
	[0] = {
		.scaler_en_w	= 3264,
		.scaler_dis_w	= 8192,
		.in_rot_en_h	= 1920,
		.in_rot_dis_w	= 8192,
		.out_rot_en_w	= 1920,
		.out_rot_dis_w	= 4224,
	},
	[1] = {
		.scaler_en_w	= 4224,
		.scaler_dis_w	= 8192,
		.in_rot_en_h	= 1920,
		.in_rot_dis_w	= 8192,
		.out_rot_en_w	= 1920,
		.out_rot_dis_w	= 4224,
	},
	[2] = {
		.scaler_en_w	= 1920,
		.scaler_dis_w	= 8192,
		.in_rot_en_h	= 1280,
		.in_rot_dis_w	= 8192,
		.out_rot_en_w	= 1280,
		.out_rot_dis_w	= 1920,
	},
	[3] = {
		.scaler_en_w	= 1920,
		.scaler_dis_w	= 8192,
		.in_rot_en_h	= 1366,
		.in_rot_dis_w	= 8192,
		.out_rot_en_w	= 1366,
		.out_rot_dis_w	= 1920,
	},
};

static struct samsung_fimc_variant fimc0_variant_s5p = {
	.has_inp_rot	 = 1,
	.has_out_rot	 = 1,
	.has_cam_if	 = 1,
	.min_inp_pixsize = 16,
	.min_out_pixsize = 16,
	.hor_offs_align	 = 8,
	.out_buf_count	 = 4,
	.pix_limit	 = &s5p_pix_limit[0],
};

static struct samsung_fimc_variant fimc2_variant_s5p = {
	.has_cam_if	 = 1,
	.min_inp_pixsize = 16,
	.min_out_pixsize = 16,
	.hor_offs_align	 = 8,
	.out_buf_count	 = 4,
	.pix_limit = &s5p_pix_limit[1],
};

static struct samsung_fimc_variant fimc0_variant_s5pv210 = {
	.pix_hoff	 = 1,
	.has_inp_rot	 = 1,
	.has_out_rot	 = 1,
	.has_cam_if	 = 1,
	.min_inp_pixsize = 16,
	.min_out_pixsize = 16,
	.hor_offs_align	 = 8,
	.out_buf_count	 = 4,
	.pix_limit	 = &s5p_pix_limit[1],
};

static struct samsung_fimc_variant fimc1_variant_s5pv210 = {
	.pix_hoff	 = 1,
	.has_inp_rot	 = 1,
	.has_out_rot	 = 1,
	.has_cam_if	 = 1,
	.has_mainscaler_ext = 1,
	.min_inp_pixsize = 16,
	.min_out_pixsize = 16,
	.hor_offs_align	 = 1,
	.out_buf_count	 = 4,
	.pix_limit	 = &s5p_pix_limit[2],
};

static struct samsung_fimc_variant fimc2_variant_s5pv210 = {
	.has_cam_if	 = 1,
	.pix_hoff	 = 1,
	.min_inp_pixsize = 16,
	.min_out_pixsize = 16,
	.hor_offs_align	 = 8,
	.out_buf_count	 = 4,
	.pix_limit	 = &s5p_pix_limit[2],
};

static struct samsung_fimc_variant fimc0_variant_exynos4 = {
	.pix_hoff	 = 1,
	.has_inp_rot	 = 1,
	.has_out_rot	 = 1,
	.has_cam_if	 = 1,
	.has_cistatus2	 = 1,
	.has_mainscaler_ext = 1,
	.min_inp_pixsize = 16,
	.min_out_pixsize = 16,
	.hor_offs_align	 = 2,
	.out_buf_count	 = 32,
	.pix_limit	 = &s5p_pix_limit[1],
};

static struct samsung_fimc_variant fimc3_variant_exynos4 = {
	.pix_hoff	 = 1,
	.has_cam_if	 = 1,
	.has_cistatus2	 = 1,
	.has_mainscaler_ext = 1,
	.min_inp_pixsize = 16,
	.min_out_pixsize = 16,
	.hor_offs_align	 = 2,
	.out_buf_count	 = 32,
	.pix_limit	 = &s5p_pix_limit[3],
};

/* S5PC100 */
static struct samsung_fimc_driverdata fimc_drvdata_s5p = {
	.variant = {
		[0] = &fimc0_variant_s5p,
		[1] = &fimc0_variant_s5p,
		[2] = &fimc2_variant_s5p,
	},
	.num_entities = 3,
	.lclk_frequency = 133000000UL,
};

/* S5PV210, S5PC110 */
static struct samsung_fimc_driverdata fimc_drvdata_s5pv210 = {
	.variant = {
		[0] = &fimc0_variant_s5pv210,
		[1] = &fimc1_variant_s5pv210,
		[2] = &fimc2_variant_s5pv210,
	},
	.num_entities = 3,
	.lclk_frequency = 166000000UL,
};

/* S5PV310, S5PC210 */
static struct samsung_fimc_driverdata fimc_drvdata_exynos4 = {
	.variant = {
		[0] = &fimc0_variant_exynos4,
		[1] = &fimc0_variant_exynos4,
		[2] = &fimc0_variant_exynos4,
		[3] = &fimc3_variant_exynos4,
	},
	.num_entities = 4,
	.lclk_frequency = 166000000UL,
};

static struct platform_device_id fimc_driver_ids[] = {
	{
		.name		= "s5p-fimc",
		.driver_data	= (unsigned long)&fimc_drvdata_s5p,
	}, {
		.name		= "s5pv210-fimc",
		.driver_data	= (unsigned long)&fimc_drvdata_s5pv210,
	}, {
		.name		= "exynos4-fimc",
		.driver_data	= (unsigned long)&fimc_drvdata_exynos4,
	},
	{},
};
MODULE_DEVICE_TABLE(platform, fimc_driver_ids);

static const struct dev_pm_ops fimc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(fimc_suspend, fimc_resume)
	SET_RUNTIME_PM_OPS(fimc_runtime_suspend, fimc_runtime_resume, NULL)
};

static struct platform_driver fimc_driver = {
	.probe		= fimc_probe,
	.remove		= __devexit_p(fimc_remove),
	.id_table	= fimc_driver_ids,
	.driver = {
		.name	= FIMC_MODULE_NAME,
		.owner	= THIS_MODULE,
		.pm     = &fimc_pm_ops,
	}
};

int __init fimc_register_driver(void)
{
	return platform_driver_probe(&fimc_driver, fimc_probe);
}

void __exit fimc_unregister_driver(void)
{
	platform_driver_unregister(&fimc_driver);
}
